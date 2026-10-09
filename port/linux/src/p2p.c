/*
P2P.C

Internet play: machines that shared an invite reach each other's system
link games as if they were on one LAN, without a server of this project's.

- An invite is a link, halo://join/<host><token>: the hash of the X25519
  public key the hosting machine makes each run (16 bytes of it, which no
  other key can be found to have; the first 6 are the machine's identifier,
  which its XNADDR also carries) and a random 16-byte token. A machine
  makes one when its game starts hosting (the game listens for
  connections), logs it, puts it on the clipboard, and offers it through
  Discord (p2p_discord.c). Nothing about a game is published anywhere else:
  without an invite there is no way to find or join it.
- Signalling (p2p_signal.c) goes through public MQTT brokers, on topics
  that are hashes of the token, with messages sealed with a key derived
  from it (p2p_crypto.c). A joiner offers its public key and the addresses
  it can be reached at; the host answers with its own, and makes the
  session once the joiner, answered, proves that it holds its key. The
  secret of their session comes from the two keys (X25519) and a nonce of
  each, and never travels.
- The tunnel is one UDP socket. Each machine learns its public address from
  public STUN servers, and both then send to each other's addresses until
  packets get through (hole punching). Two machines whose NATs both map
  every destination to a new port cannot connect that way, unless a router
  forwards one of them a port, or a relay carries them (below). So a host
  asks its router to forward
  the tunnel's port (UPnP, posix_upnp.c) as soon as a player reaches out
  with its invite, and a joiner asks its own when it has not reached the
  host in a few seconds (network.allow_upnp); the forwarded port is one more
  of the addresses a machine offers (the joiner asks the host again every
  few seconds until they meet, and the host answers with them all). Every
  tunnel packet is sealed with a key of the session's for its direction,
  its header (the sender and the packet's number, the nonce) authenticated
  too, and a packet already received, or from further back than the last
  64, is dropped.
- Relays (port/relay, p2p_relay_protocol.h): a machine may name up to
  P2P_MAXIMUM_RELAYS of them (relays.txt beside brokers.txt, or
  network.relays; none by default), and offers them with its addresses in
  signalling. A peer not reached both ways (no answer to a ping) RELAY_DELAY
  after it was offered is also asked for through the relays, the host's
  first: each machine asks each relay for the allocation of their session
  (an identifier derived from its secret, which only the two of them have),
  and the relay passes their tunnel packets, still sealed, between the two
  once both did. A direct path stays preferred: its addresses are still
  tried a while, and the first answer from one moves the peer back to it.
- Each peer gets a virtual address in 100.64.0.0/10, which the game sees
  (XNetXnAddrToInAddr maps the peer's XNADDR to it). The game's datagrams
  to a peer go onto the tunnel from xnet.c at once (p2p_send_datagram);
  otherwise xnet.c rewrites the game's destinations there to local
  stand-ins: a UDP socket here per peer and port forwards datagrams (the
  peer's to the game, and the game's from a socket connected to the
  peer), and a TCP listener per peer and port takes the game's connections,
  carried reliably over the tunnel by KCP (port/third_party/kcp). Traffic
  arriving from a peer leaves these stand-ins, and xnet.c reports it as
  coming from the peer's address. The
  game's broadcasts also go to every peer, so a host's game shows up in its
  joiners' system link lists, and joining works as on a LAN. A peer reaches
  only the game's own ports (xnet.c says which it has), and has a few
  stand-ins and connections at a time.

The work happens on a thread of its own, under p2p_lock (let go of for
whatever may wait: DNS, the desktop's link handlers); the game's threads
only look up and create stand-ins.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "p2p_internal.h"
#include "p2p_relay_protocol.h"
#include "lang.h"
#include "ikcp.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum
{
	/* the tunnel's version 2 (0x68 was the first) */
	TUNNEL_MAGIC = P2P_TUNNEL_MAGIC,
	/* the magic, the sender's identifier, and the packet's number */
	TUNNEL_HEADER_SIZE = 1 + P2P_IDENTIFIER_SIZE + 8,
	/* a tunnel packet's plaintext: a type, and at most the game's largest
	datagram (WSAStartup's iMaxUdpDg) with its ports */
	MAXIMUM_INNER_SIZE = 1400,
	MAXIMUM_PACKET_SIZE = TUNNEL_HEADER_SIZE + MAXIMUM_INNER_SIZE + P2P_TAG_SIZE,
	/* the packets received out of order that are still taken */
	REPLAY_WINDOW = 64,
	/* a reached peer's packets from another address than its endpoint and
	those it offered (its NAT gave it a new port, or someone else sends in
	its name) whose seal is checked, a second: the rest are dropped unread */
	STRAY_PACKETS_PER_SECOND = 32,

	/* a host needs a UDP stand-in for two or three ports of every other
	machine, and a stream for each one's connection; one peer can have no
	more than these */
	MAXIMUM_PROXIES = 512,
	MAXIMUM_PEER_PROXIES = 4,
	MAXIMUM_LISTENERS = 64,
	MAXIMUM_STREAMS = 160,
	MAXIMUM_PEER_STREAMS = 4,
	MAXIMUM_PEER_OPENING_STREAMS = 2,
	/* the game's sockets' ports (xnet.c), and the ports it sent peers
	datagrams from */
	MAXIMUM_GAME_PORTS = 64,
	MAXIMUM_SENT_PORTS = 16,
	/* sessions that ended, which do not come back */
	MAXIMUM_RETIRED_SESSIONS = 16,
	/* the players a host is reaching at once (anyone with the invite can
	ask in any number of made-up names: each would have the host send to the
	addresses it gives) */
	MAXIMUM_OPENING_PEERS = 8,
	/* the hosts joined from the public lobby that are remembered as such */
	P2P_PUBLIC_HOSTS = 8,
	/* stand-ins closed lately, whose traffic the game may not have read yet
	(p2p_incoming) */
	MAXIMUM_CLOSED_PORTS = 128,
	CLOSED_PORT_TIME = 5000,
	/* a peer's stand-in used this lately is not closed for another: a peer
	sending from ever new ports has a few a second, not one a datagram */
	PROXY_REPLACE_TIME = 1000,
	MAXIMUM_STUN_SERVERS = 4,
	STREAM_BUFFER_SIZE = 16384,
	STREAM_CHUNK_SIZE = 1024,
	/* the most unacknowledged messages a stream sends ahead */
	STREAM_WINDOW = 128,
	KCP_MTU = 1200,
	/* a stream's least retransmission timeout (milliseconds; KCP's fast
	mode's is 30): with no congestion control, a window sent at once waits
	in a slow uplink's queue (128 KB is 256 ms at 4 Mbit/s), and the last of
	it timed out and was sent again: a map download over 8 Mbit/s with a
	100 ms round trip sent 45% again, carried 61% of the link (87% with
	this). A segment lost with more behind it is sent again sooner, on
	their acknowledgements (fast resend). Only while a stream has a bulk
	transfer queued (KCP_BULK_SEGMENTS): the game's own messages, a few at a
	time, keep fast mode's 30 ms, so a lost one is not held half a second */
	KCP_MINIMUM_RTO = 500,
	KCP_FAST_MINIMUM_RTO = 30,
	KCP_BULK_SEGMENTS = 32,

	/* milliseconds */
	LOOP_INTERVAL = 10,
	PUNCH_INTERVAL = 200,
	PING_INTERVAL = 1000,
	ENDPOINT_SWITCH_TIME = 3000,
	PEER_TIMEOUT = 20000,
	PUNCH_TIMEOUT = 30000,
	JOIN_TIMEOUT = 90000,
	/* a code's record is retained by the brokers, so it arrives as soon as
	one of them is reached */
	CODE_LOOKUP_TIMEOUT = 20000,
	STREAM_LINGER_TIME = 10000,
	PROXY_IDLE_TIME = 60000,
	STUN_RETRY_INTERVAL = 500,
	STUN_REFRESH_INTERVAL = 25000,
	STUN_ATTEMPTS = 6,
	/* a STUN server or relay whose name could not be looked up is looked up
	again after this */
	LOOKUP_RETRY_INTERVAL = 30000,
	/* the resolver cache's file is written at most this often (and only when
	an address changed: p2p_resolver_cache.c) */
	RESOLVER_CACHE_WRITE_INTERVAL = 10000,
	/* a joiner asks its router to forward the tunnel's port when it has not
	reached a peer in this long; a forwarding is renewed this often (its
	lease is an hour), and one refused asked for again this long after */
	UPNP_JOIN_DELAY = 5000,
	UPNP_RENEW_INTERVAL = 30 * 60 * 1000,
	UPNP_RETRY_INTERVAL = 5 * 60 * 1000,
	/* how long the game's exit waits for a request under way */
	UPNP_RELEASE_WAIT = 3000,
	/* relays: a peer not reached both ways in this long is also asked for
	through them; a relay's allocation is asked for this often until it is
	ready, refreshed this often while it carries a peer, and asked again
	this long after the relay refused it; and while a peer is reached only
	through a relay its addresses are still tried, this often for this long
	(a direct path is taken at its first answer) */
	RELAY_DELAY = 8000,
	RELAY_RETRY_INTERVAL = 500,
	RELAY_REFRESH_INTERVAL = 15000,
	RELAY_REFUSED_INTERVAL = 5000,
	RELAYED_PUNCH_INTERVAL = 1000,
	RELAYED_PUNCH_TIME = 300000,
	/* the relays a peer may be reached through: this machine's and the
	peer's */
	MAXIMUM_PEER_RELAYS = 2 * P2P_MAXIMUM_RELAYS,

	/* where a running copy of the game takes invites from another one
	started to open a link (127.0.0.1), sealed with a key of the user's */
	HANDOFF_PORT = 47315,
	/* the game's ports (network_game_protocol.h's NETWORK_GAME_SERVER_PORT and
	NETWORK_GAME_CLIENT_PORT), which the netcode and every machine use */
	P2P_GAME_SERVER_PORT = 0x141E,
	P2P_GAME_CLIENT_PORT = 0x141F,

#ifdef HALO_DEDICATED_SERVER
	/* a dedicated server, the internet's to reach (port/linux/DEDICATED_SERVER.md):
	a peer's packets a second past which the rest are dropped (a game sends
	a few score a second, a map download's acknowledgements some hundreds),
	and the peers one address may have connected (players behind one NAT
	share it; a relay's peers are told apart by the relay) */
	DEDICATED_PEER_PACKETS_PER_SECOND = 2000,
	DEDICATED_PEERS_PER_ADDRESS = 8,
#endif
};

enum
{
	_packet_ping = 1,
	_packet_pong,
	_packet_datagram,
	_packet_stream,
	_packet_bye,
	/* (the host to a client) every player's round trip as the host has it,
	for the scoreboard (ping_table_received). 1.1.0's betas drop a type they
	do not know unread (tunnel_received's switch has no default), so this
	is no network change */
	_packet_ping_table,
};

/* the messages of a stream, inside KCP */
enum
{
	_stream_open = 'O',
	_stream_data = 'D',
	_stream_close = 'C',
};

/* a relay's allocation for a peer: asked for, waiting for the peer to ask
too, carrying (DATA), refused (no room, or its side another's) */
enum
{
	_relay_asking,
	_relay_waiting,
	_relay_ready,
	_relay_refused,
};

struct peer_relay
{
	struct p2p_candidate address;
	int state;
	unsigned long channel;
	/* the nonce of this machine's requests, which the answers carry; and
	the cookie the relay gave */
	unsigned char nonce[P2P_RELAY_NONCE_SIZE];
	unsigned char cookie[P2P_RELAY_COOKIE_SIZE];
	unsigned long sent_time;
};

enum
{
	/* a peer's stream whose open has not arrived */
	_stream_awaiting_open,
	/* connecting to the local game */
	_stream_connecting,
	_stream_open_state,
};

struct peer
{
	int used;
	unsigned char identifier[P2P_IDENTIFIER_SIZE];
	char name[2 * P2P_IDENTIFIER_SIZE + 1];
	/* the session's secret, and the keys from it for each direction */
	unsigned char secret[P2P_SHA256_SIZE];
	unsigned char send_key[P2P_SHA256_SIZE];
	unsigned char receive_key[P2P_SHA256_SIZE];
	/* the last packet number sent; the highest received, and which of the
	REPLAY_WINDOW before it were (bit n: highest - n) */
	unsigned long long send_counter;
	unsigned long long receive_highest;
	unsigned long long receive_window;
	unsigned long virtual_address;
	int is_host;
	int connected;
	struct p2p_candidate candidates[P2P_MAXIMUM_CANDIDATES];
	int candidate_count;
	struct p2p_candidate endpoint;
	/* its UDP stand-ins (indices in p2p.proxies) */
	short proxies[MAXIMUM_PEER_PROXIES];
	int proxy_count;
	unsigned long offered_time;
	unsigned long heard_time;
	unsigned long endpoint_heard_time;
	unsigned long sent_time;
	unsigned long round_trip;
	/* packets from elsewhere than its endpoint and the addresses it offered
	that may still be checked (STRAY_PACKETS_PER_SECOND), as of when */
	int stray_budget;
	unsigned long stray_time;
	/* a pong came back: it is reached both ways */
	int two_way;
#ifdef HALO_DEDICATED_SERVER
	/* (a dedicated server) its packets this second, and since when; when an
	excess was last logged */
	int rate_count;
	unsigned long rate_time;
	unsigned long rate_logged_time;
#endif
	/* relays: those it offered; the allocation of the session (from its
	secret); whether it is asked for through relays, and through which; the
	endpoint is a relay's, since when; and when its addresses were last
	tried meanwhile */
	struct p2p_candidate offered_relays[P2P_MAXIMUM_RELAYS];
	int offered_relay_count;
	unsigned char relay_allocation[P2P_RELAY_ALLOCATION_SIZE];
	int relaying;
	struct peer_relay relays[MAXIMUM_PEER_RELAYS];
	int relay_count;
	int via_relay;
	unsigned long relayed_time;
	unsigned long punch_time;
};

/* a UDP stand-in for one port of a peer */
struct proxy
{
	int socket;
	int peer;
	unsigned short remote_port;
	unsigned short local_port;
	unsigned long used_time;
	/* the game's socket connected to it, or -1: while there is one, it
	stays */
	int pinned_socket;
};

/* a port of the game's (xnet.c) */
struct game_port
{
	int socket;
	/* 0: none */
	unsigned short port;
	unsigned char stream;
	unsigned char listening;
};

/* a stand-in closed lately (p2p_incoming) */
struct closed_port
{
	unsigned short local_port;
	unsigned short remote_port;
	int stream;
	unsigned long virtual_address;
	unsigned long time;
};

struct retired_session
{
	unsigned char secret[P2P_SHA256_SIZE];
	unsigned long virtual_address;
};

/* a TCP stand-in for one port of a peer: takes the game's connections */
struct listener
{
	int socket;
	int peer;
	unsigned short remote_port;
	unsigned short local_port;
};

/* one of the game's TCP connections, carried over the tunnel */
struct stream
{
	int used;
	int peer;
	IUINT32 conversation;
	ikcpcb *kcp;
	int state;
	/* the local end: the game's connection to a listener, or a connection
	to the game made for a peer's (whose port stands for the peer's) */
	int socket;
	unsigned short local_port;
	unsigned short remote_port;
	int local_closed;
	int remote_closed;
	unsigned long created_time;
	unsigned long closed_time;
	unsigned char pending[STREAM_BUFFER_SIZE];
	int pending_size;
};

struct stun_server
{
	char host[128];
	unsigned short port;
	unsigned long address;
	unsigned char transaction[12];
	int attempts;
	/* its name's lookups that failed */
	int lookup_failures;
	unsigned long sent_time;
	int has_mapped;
	struct p2p_candidate mapped;
};

pthread_mutex_t p2p_lock = PTHREAD_MUTEX_INITIALIZER;

static struct
{
	int running;
	unsigned long local_address;
	int tunnel_socket;
	unsigned short tunnel_port;
	int handoff_socket;

	struct peer peers[P2P_MAXIMUM_PEERS];
	struct retired_session retired[MAXIMUM_RETIRED_SESSIONS];
	int retired_next;
	struct proxy proxies[MAXIMUM_PROXIES];
	struct listener listeners[MAXIMUM_LISTENERS];
	struct stream streams[MAXIMUM_STREAMS];
	/* recently finished streams, whose late packets are ignored */
	IUINT32 finished[16];
	int finished_next;
	struct closed_port closed[MAXIMUM_CLOSED_PORTS];
	int closed_next;
	/* what peers may reach */
	struct game_port game_ports[MAXIMUM_GAME_PORTS];
	unsigned short sent_ports[MAXIMUM_SENT_PORTS];
	int sent_port_next;
	/* the key of invites handed over (handoff_readable) */
	int has_handoff_key;
	unsigned char handoff_key[P2P_SHA256_SIZE];

	struct stun_server stun[MAXIMUM_STUN_SERVERS];
	int stun_count;
	/* from the first time a game is hosted or joined */
	int stun_started;
	int reported_symmetric;

	/* hosting: while the game listens on hosting_socket and its server
	takes other machines (game_accepts_remote: p2p_set_game_accepts_remote;
	a Split Screen game's listens and takes none: game_local) */
	int hosting_socket;
	int game_accepts_remote;
	int hosting;
	int has_token;
	unsigned char token[P2P_TOKEN_SIZE];
	/* whether the server browser's exit hook is set (lobby_quit) */
	int lobby_quit_registered;
	/* whether the server browser listed this token (p2p_lobby.c): going
	private makes a new one */
	int token_listed;
	char invite[P2P_LINK_SIZE];
	int invite_copied;
	/* the game's players and its most (p2p_set_game_player_counts; 0: not
	said, and the machines the tunnel reaches are shown), and what Discord
	was told */
	int game_player_count;
	int game_player_maximum;
	int reported_player_count;
	int reported_player_maximum;

	/* joining: until the host is reached, or JOIN_TIMEOUT */
	int join_requested;
	int joining;
	unsigned char join_host[P2P_IDENTIFIER_SIZE];
	unsigned char join_host_hash[P2P_KEY_HASH_SIZE];
	unsigned char join_token[P2P_TOKEN_SIZE];
	unsigned long join_time;

	char clipboard[P2P_LINK_SIZE];
	int has_clipboard;

	/* hosting: the invite's short code (ABCD-EFGH), made with it (the
	run's: a new invite keeps it) */
	char code[P2P_CODE_SIZE];
	/* looking up a code's eight characters, until its record arrives or
	CODE_LOOKUP_TIMEOUT */
	int lookup_requested;
	int looking_up;
	char lookup_code[P2P_CODE_LENGTH + 1];
	unsigned long lookup_time;
	/* (a code whose record must be one host's: the host's identifier) */
	int lookup_has_host;
	unsigned char lookup_host[P2P_IDENTIFIER_SIZE];
	/* the code looked up is a public lobby's game's (p2p_join_lobby_code, p2p_lobby_join) */
	int lookup_public;
	/* the hosts last joined from the public lobby (a code or an invite
	joined since takes its host off): their games' map downloads are asked
	about with a warning (p2p_address_origin) */
	unsigned char public_hosts[P2P_PUBLIC_HOSTS][P2P_IDENTIFIER_SIZE];
	int public_host_next;

	/* what is happening, for a menu (p2p_status) */
	char status[96];
	/* (the same in the language chosen, lang.c: p2p_status_shown) */
	char status_shown[192];

	/* ad hoc play (network.adhoc, p2p_adhoc.c): the peers are the ad hoc
	group's machines, through local relays; nothing goes to the internet
	(no signalling, STUN or UPnP) */
	int adhoc;

	/* UPnP (posix_upnp.c): a thread asking the router; the port it forwards
	here, and when it was last asked */
	int upnp_working;
	int upnp_asked;
	int upnp_forwarded;
	int upnp_release_registered;
	int upnp_released;
	struct p2p_candidate upnp_candidate;
	unsigned long upnp_time;

	/* relays (relays.txt, network.relays): their names, looked up once
	when STUN starts; whether any may be used (network.allow_relay, not in
	ad hoc play) */
	int relay_allowed;
	char relay_hosts[P2P_MAXIMUM_RELAYS][128];
	unsigned short relay_ports[P2P_MAXIMUM_RELAYS];
	int relay_host_count;
	int relays_resolved;
	struct p2p_candidate relays[P2P_MAXIMUM_RELAYS];
	int relay_count;
	/* each name's: looked up; its lookups that failed; when they were last
	tried */
	int relay_host_found[P2P_MAXIMUM_RELAYS];
	int relay_lookup_failures[P2P_MAXIMUM_RELAYS];
	unsigned long relay_lookup_time;
} p2p = { 0, 0, -1, 0, -1, .hosting_socket = -1 };

/* the proxy (its index + 1) with each local port (all of theirs are on
p2p.local_address) */
static unsigned short proxy_by_port[65536];

static unsigned char identifier[P2P_IDENTIFIER_SIZE];
/* this run's X25519 keys, which the identifier comes from: from an Ed25519
seed, whose key signs the listing of a public game (p2p_lobby.c), so that a
listing's key is also the invite's */
static unsigned char seed[P2P_SEED_SIZE];
static unsigned char signing_key[P2P_KEY_SIZE];
static unsigned char secret_key[P2P_KEY_SIZE];
static unsigned char public_key[P2P_KEY_SIZE];
static int has_identifier;

/* ---------- helpers */

unsigned long p2p_now(void)
{
	return GetTickCount();
}

static int elapsed(unsigned long since, unsigned long time)
{
	/* (unsigned, as the clock wraps: a signed difference is negative for
	half of it, which had nothing lapse from a time of 0 from 24.8 days of
	uptime on) */
	return (unsigned int)(p2p_now() - since) >= (unsigned int)time;
}

/* now, as a time kept where 0 means none (never later than now, which
elapsed would take for long past) */
static unsigned long now_stamp(void)
{
	unsigned long now = p2p_now();

	return now ? now : (unsigned long)-1;
}

/* the resolver cache (p2p_resolver_cache.c): read from its file once, and
written when an address changed; the last lookup's failure, in words */
static struct
{
	int loaded;
	int dirty;
	unsigned long written_time;
	char error[64];
	/* the names whose stand-in address was logged, and when */
	char logged[P2P_RESOLVER_CACHE_ENTRIES][P2P_RESOLVER_HOST_SIZE];
	unsigned long logged_time[P2P_RESOLVER_CACHE_ENTRIES];
} resolver;

/* the resolver cache's file (network.resolver_cache_file, beside config.toml
unless a full path); 0 if there is none */
static int resolver_cache_path(char *path, int size)
{
	const char *name = config_string("network.resolver_cache_file");
	const char *colon = strchr(name, ':');

	if (!name[0])
		return 0;
	if (name[0] == '/' || name[0] == '\\' || (colon && !memchr(name, '/', (size_t)(colon - name))))
		snprintf(path, (size_t)size, "%s", name);
	else
	{
		config_folder(path, (size_t)size);
		snprintf(path + strlen(path), (size_t)size - strlen(path), "%s", name);
	}
	return 1;
}

static void resolver_cache_load(void)
{
	char path[1024];
	char *text;
	size_t size = 0;

	if (resolver.loaded)
		return;
	resolver.loaded = 1;
	if (!resolver_cache_path(path, sizeof(path)))
		return;
	text = config_file_read(path, &size);
	if (!text)
		return;
	p2p_resolver_cache_load(text, (unsigned long)time(NULL));
	free(text);
}

/* the file written, if an address changed (not too often); the p2p
thread's, under p2p_lock, which the writing lets go of */
static void resolver_cache_write(void)
{
	char path[1024];
	char text[P2P_RESOLVER_CACHE_ENTRIES * (P2P_RESOLVER_HOST_SIZE + 32) + 256];
	int length;

	if (!resolver.dirty || (resolver.written_time && !elapsed(resolver.written_time, RESOLVER_CACHE_WRITE_INTERVAL)))
		return;
	resolver.dirty = 0;
	resolver.written_time = now_stamp();
	if (!resolver_cache_path(path, sizeof(path)))
		return;
	length = p2p_resolver_cache_save(text, sizeof(text));
	pthread_mutex_unlock(&p2p_lock);
	if (!config_file_write(path, text, (size_t)length))
		platform_log("Internet play: cannot write the brokers' last good addresses to %s", path);
	pthread_mutex_lock(&p2p_lock);
}

unsigned long p2p_resolve(const char *host)
{
	unsigned long address, cached, cached_time = 0;
	unsigned long now = (unsigned long)time(NULL);
	char error[sizeof(resolver.error)];

	resolver_cache_load();
	/* DNS can take seconds, which the game's threads must not wait for */
	pthread_mutex_unlock(&p2p_lock);
	address = posix_resolve_ipv4(host);
	if (!address)
		posix_resolve_error(error, sizeof(error));
	pthread_mutex_lock(&p2p_lock);
	if (address)
	{
		/* (a lookup that works always wins, and is kept) */
		if (p2p_resolver_cache_store(host, address, now))
			resolver.dirty = 1;
		return address;
	}
	memcpy(resolver.error, error, sizeof(error));
	if (p2p_resolver_cache_lookup(host, now, &cached, &cached_time))
	{
		const unsigned char *bytes = (const unsigned char *)&cached;
		int index, slot = -1;

		/* (said once a name each FAILURE_LOG_INTERVAL: it is tried often) */
		for (index = 0; index < P2P_RESOLVER_CACHE_ENTRIES && slot < 0; index++)
		{
			if (!strcmp(resolver.logged[index], host) || !resolver.logged[index][0])
				slot = index;
		}
		if (slot < 0 || !resolver.logged_time[slot] || elapsed(resolver.logged_time[slot], 300000))
		{
			platform_log("Internet play: cannot look up %s (%s); using its last good address %u.%u.%u.%u (from %lu "
				"hours ago)", host, error, bytes[0], bytes[1], bytes[2], bytes[3],
				now > cached_time ? (now - cached_time) / 3600 : 0);
			if (slot >= 0)
			{
				snprintf(resolver.logged[slot], sizeof(resolver.logged[slot]), "%s", host);
				resolver.logged_time[slot] = now_stamp();
			}
		}
		return cached;
	}
	return 0;
}

const char *p2p_resolve_error(void)
{
	return resolver.error[0] ? resolver.error : "unknown";
}

void p2p_register_url_scheme(const char *scheme, const char *description)
{
	if (config_real("debug.exit_after") > 0.0 || config_boolean("debug.hidden_window") ||
		config_boolean("debug.null_renderer"))
		return;
	/* it may run a program and wait for it */
	pthread_mutex_unlock(&p2p_lock);
	posix_register_url_scheme(scheme, description);
	pthread_mutex_lock(&p2p_lock);
}

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < size; index++)
	{
		text[index * 2] = digits[bytes[index] >> 4];
		text[index * 2 + 1] = digits[bytes[index] & 15];
	}
	text[size * 2] = 0;
}

static int hex_value(char digit)
{
	if (digit >= '0' && digit <= '9')
		return digit - '0';
	if (digit >= 'a' && digit <= 'f')
		return digit - 'a' + 10;
	if (digit >= 'A' && digit <= 'F')
		return digit - 'A' + 10;
	return -1;
}

static unsigned long network_long(unsigned long value)
{
	return __builtin_bswap32(value);
}

static unsigned short network_short(unsigned short value)
{
	return (unsigned short)(value << 8 | value >> 8);
}

static void put_short(unsigned char *bytes, unsigned short value)
{
	/* value is in network byte order already */
	memcpy(bytes, &value, 2);
}

static unsigned short get_short(const unsigned char *bytes)
{
	unsigned short value;

	memcpy(&value, bytes, 2);
	return value;
}

static void make_address(struct sockaddr_in *address, unsigned long ip, unsigned short port)
{
	memset(address, 0, sizeof(*address));
	address->sin_family = AF_INET;
	address->sin_port = port;
	address->sin_addr.s_addr = ip;
}

static const char *address_text(unsigned long ip, unsigned short port, char *text)
{
	unsigned long value = network_long(ip);

	sprintf(text, "%lu.%lu.%lu.%lu:%u", value >> 24, (value >> 16) & 255, (value >> 8) & 255, value & 255,
		network_short(port));
	return text;
}

/* a UDP or TCP socket bound to ip:port (0 for any), not blocking; its port
through bound_port; -1 on failure */
static int open_socket(int type, unsigned long ip, unsigned short port, unsigned short *bound_port)
{
	struct sockaddr_in address;
	int length = sizeof(address);
	int result = posix_socket(AF_INET, type, 0);

	if (result < 0)
		return -1;
	make_address(&address, ip, port);
	if (posix_socket_bind(result, &address, sizeof(address)) < 0 ||
		posix_socket_getsockname(result, &address, &length) < 0 ||
		posix_socket_set_nonblocking(result, 1) < 0)
	{
		posix_socket_close(result);
		return -1;
	}
	/* (the game's connections, carried over the tunnel: nothing held back) */
	if (type == SOCK_STREAM)
		posix_socket_set_nodelay(result);
	if (bound_port)
		*bound_port = address.sin_port;
	return result;
}

static int would_block(void)
{
	int error = posix_socket_last_error();

	return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}

/* the status line (p2p_status) and the log, under p2p_lock */
/* the status line, in English (halo.log, p2p_status) and in the language
chosen (p2p_status_shown): `format` is an N_() marked English text */
static void set_status_arguments(const char *format, va_list arguments)
{
	va_list copy;

	va_copy(copy, arguments);
	vsnprintf(p2p.status, sizeof(p2p.status), format, arguments);
	vsnprintf(p2p.status_shown, sizeof(p2p.status_shown), T(format), copy);
	va_end(copy);
	platform_log("Internet play: %s", p2p.status);
}

static void set_status(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	set_status_arguments(format, arguments);
	va_end(arguments);
}

const unsigned char *p2p_identifier(void)
{
	/* its own lock: the p2p thread asks while holding p2p_lock */
	static pthread_mutex_t identifier_lock = PTHREAD_MUTEX_INITIALIZER;

	pthread_mutex_lock(&identifier_lock);
	if (!has_identifier)
	{
		posix_random_bytes(seed, sizeof(seed));
		p2p_ed25519_public(seed, signing_key, secret_key);
		p2p_x25519(public_key, secret_key, NULL);
		p2p_identifier_for(public_key, identifier);
		has_identifier = 1;
	}
	pthread_mutex_unlock(&identifier_lock);
	return identifier;
}

void p2p_key_hash(const unsigned char *key, unsigned char *hash)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(key, P2P_KEY_SIZE, digest);
	memcpy(hash, digest, P2P_KEY_HASH_SIZE);
}

void p2p_identifier_from_hash(const unsigned char *hash, unsigned char *result)
{
	memcpy(result, hash, P2P_IDENTIFIER_SIZE);
	/* like a locally administered unicast MAC address, as XNADDR's abEnet
	holds one */
	result[0] = (unsigned char)((result[0] & 0xFC) | 0x02);
}

void p2p_identifier_for(const unsigned char *key, unsigned char *result)
{
	unsigned char hash[P2P_KEY_HASH_SIZE];

	p2p_key_hash(key, hash);
	p2p_identifier_from_hash(hash, result);
}

const unsigned char *p2p_public_key(void)
{
	p2p_identifier();
	return public_key;
}

const unsigned char *p2p_signing_key(void)
{
	p2p_identifier();
	return signing_key;
}

void p2p_sign(const void *message, int size, unsigned char *signature)
{
	p2p_identifier();
	p2p_ed25519_sign(seed, signing_key, message, size, signature);
}

int p2p_shared_secret(const unsigned char *key, unsigned char *shared)
{
	static const unsigned char zero[P2P_KEY_SIZE];

	p2p_identifier();
	/* (milliseconds of arithmetic, which the game's threads need not wait
	for: nothing here changes meanwhile) */
	pthread_mutex_unlock(&p2p_lock);
	p2p_x25519(shared, secret_key, key);
	pthread_mutex_lock(&p2p_lock);
	/* a key of small order gives a secret anyone knows */
	return !p2p_equal(shared, zero, P2P_KEY_SIZE);
}

/* ---------- peers */

static struct peer *find_peer(const unsigned char *peer_identifier)
{
	int index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		if (p2p.peers[index].used && !memcmp(p2p.peers[index].identifier, peer_identifier, P2P_IDENTIFIER_SIZE))
			return &p2p.peers[index];
	}
	return NULL;
}

static struct peer *find_peer_by_address(unsigned long address)
{
	int index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		if (p2p.peers[index].used && p2p.peers[index].virtual_address == address)
			return &p2p.peers[index];
	}
	return NULL;
}

/* ---------- this machine's hardware id */

#if defined(_WIN32) || defined(HALO_VITA)
/* win32_p2p.c or vita_stubs.c: the SMBIOS system UUID, else the registry's MachineGuid */
int posix_hardware_id_source(char *text, int size);
#endif

/* what this machine is known by, as text (none: 0): Windows' SMBIOS UUID or
MachineGuid (win32_p2p.c); Linux's /etc/machine-id; Android's ANDROID_ID,
which only the app's Java can read and puts in hardware_id.txt
(LauncherActivity.java) */
static int hardware_id_source(char *text, int size)
{
#if defined(_WIN32) || defined(HALO_VITA)
	return posix_hardware_id_source(text, size);
#else
	static const char *const linux_paths[] = { "/etc/machine-id", "/var/lib/dbus/machine-id" };
	char android_path[1024];
	const char *paths[2];
	int path_count = 0;
	int index;

#ifdef HALO_ANDROID
	snprintf(android_path, sizeof(android_path), "%s/hardware_id.txt", platform_data_root());
	paths[path_count++] = android_path;
#else
	(void)android_path;
	paths[path_count++] = linux_paths[0];
	paths[path_count++] = linux_paths[1];
#endif
	for (index = 0; index < path_count; index++)
	{
		FILE *file = fopen(paths[index], "rb");
		size_t length;

		if (!file)
			continue;
		length = fread(text, 1, (size_t)size - 1, file);
		fclose(file);
		text[length] = 0;
		/* (the line, without its end) */
		text[strcspn(text, "\r\n")] = 0;
		if (text[0])
			return 1;
	}
	return 0;
#endif
}

/* this machine's hardware id, as hex (empty if it has none to tell): a hash
of what it is known by (hardware_id_source) keyed for this game, so that
what is told is no raw serial and is this game's alone; a host it joins
logs it, and refuses one it banned. Anyone with administrator or root can
change what it is known by: a stable id, not a proof */
void p2p_hardware_id(char *hex, int size)
{
	static const char key[] = "halo-ce-universal hardware id v1";
	static char cached[2 * P2P_HARDWARE_ID_BYTES + 1];
	static int computed;

	if (!computed)
	{
		char source[256];
		unsigned char digest[P2P_SHA256_SIZE];

		computed = 1;
		cached[0] = 0;
		if (hardware_id_source(source, sizeof(source)))
		{
			p2p_hmac_sha256((const unsigned char *)key, (int)sizeof(key) - 1, source, (int)strlen(source), digest);
			p2p_hex(digest, P2P_HARDWARE_ID_BYTES, cached);
		}
	}
	snprintf(hex, (size_t)size, "%s", cached);
}

void p2p_hardware_id_sanitize(char *destination, int size, const char *source)
{
	int length = 0;

	for (; source && *source && length < size - 1 && length < 2 * P2P_HARDWARE_ID_BYTES; source++)
	{
		char character = *source >= 'A' && *source <= 'F' ? *source - 'A' + 'a' : *source;

		if ((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))
			destination[length++] = character;
	}
	if (size > 0)
		destination[length] = 0;
}

void p2p_discord_identity(char *id, int id_size, char *name, int name_size)
{
	if (id_size > 0)
		id[0] = 0;
	if (name_size > 0)
		name[0] = 0;
	if (!p2p.running || id_size <= 0 || name_size <= 0)
		return;
	pthread_mutex_lock(&p2p_lock);
	p2p_discord_user(id, id_size, name, name_size);
	pthread_mutex_unlock(&p2p_lock);
}

unsigned long p2p_peer_endpoint_address(unsigned long virtual_address)
{
	struct peer *peer;
	unsigned long address = 0;

	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	peer = find_peer_by_address(virtual_address);
	if (peer && peer->endpoint.address && !peer->via_relay)
		address = peer->endpoint.address;
	else if (peer)
	{
		int index;

		/* (reached through a relay: the address it said it has on the
		internet, its last offered, rather than the relay's, which many
		share) */
		for (index = 0; index < peer->candidate_count; index++)
			address = peer->candidates[index].address;
	}
	pthread_mutex_unlock(&p2p_lock);
	return address;
}

static int is_virtual_address(unsigned long address)
{
	/* 100.64.0.0/10 */
	return (network_long(address) & 0xFFC00000) == 0x64400000;
}

/* an address in 100.64.0.0/10 from the identifier, not one another peer has */
static unsigned long virtual_address_for(const unsigned char *peer_identifier)
{
	unsigned char digest[P2P_SHA256_SIZE];
	unsigned long value;

	p2p_sha256(peer_identifier, P2P_IDENTIFIER_SIZE, digest);
	value = (unsigned long)digest[0] << 16 | (unsigned long)digest[1] << 8 | digest[2];
	for (;;)
	{
		unsigned long address = 0x64400000 | (value & 0x3FFFFF);

		/* no .0 or .255, which look like network and broadcast addresses */
		if ((address & 255) != 0 && (address & 255) != 255 && !find_peer_by_address(network_long(address)))
			return network_long(address);
		value++;
	}
}

/* a tunnel packet's number, from its header */
static unsigned long long packet_counter(const unsigned char *packet)
{
	unsigned long long counter = 0;
	int index;

	for (index = 7; index >= 0; index--)
		counter = counter << 8 | packet[1 + P2P_IDENTIFIER_SIZE + index];
	return counter;
}

/* its nonce: its number (each direction has a key of its own) */
static void packet_nonce(const unsigned char *packet, unsigned char *nonce)
{
	memset(nonce, 0, P2P_NONCE_SIZE - 8);
	memcpy(nonce + P2P_NONCE_SIZE - 8, packet + 1 + P2P_IDENTIFIER_SIZE, 8);
}

/* the peer's relay at that address that carries it now (its allocation
ready), or NULL */
static struct peer_relay *peer_relay_at(struct peer *peer, unsigned long address, unsigned short port)
{
	int index;

	for (index = 0; index < peer->relay_count; index++)
	{
		struct peer_relay *relay = &peer->relays[index];

		if (relay->state == _relay_ready && relay->address.address == address && relay->address.port == port)
			return relay;
	}
	return NULL;
}

static void peer_send_to(struct peer *peer, const struct p2p_candidate *to, const unsigned char *inner, int size)
{
	/* (room ahead for a relay's DATA header) */
	unsigned char buffer[P2P_RELAY_DATA_HEADER_SIZE + MAXIMUM_PACKET_SIZE];
	unsigned char *packet = buffer + P2P_RELAY_DATA_HEADER_SIZE;
	unsigned char nonce[P2P_NONCE_SIZE];
	struct peer_relay const *relay = peer_relay_at(peer, to->address, to->port);
	struct sockaddr_in address;
	unsigned long long counter;
	int sealed;
	int index;

	if (p2p.tunnel_socket < 0 || size > MAXIMUM_INNER_SIZE)
		return;
	/* the header, authenticated with the rest */
	counter = ++peer->send_counter;
	packet[0] = TUNNEL_MAGIC;
	memcpy(packet + 1, identifier, P2P_IDENTIFIER_SIZE);
	for (index = 0; index < 8; index++)
		packet[1 + P2P_IDENTIFIER_SIZE + index] = (unsigned char)(counter >> (index * 8));
	packet_nonce(packet, nonce);
	sealed = p2p_aead_seal(peer->send_key, nonce, packet, TUNNEL_HEADER_SIZE, inner, size,
		packet + TUNNEL_HEADER_SIZE);
	make_address(&address, to->address, to->port);
	if (relay)
	{
		/* (to the relay, which passes it on as it is) */
		buffer[0] = P2P_RELAY_MAGIC;
		buffer[1] = _relay_data;
		for (index = 0; index < 4; index++)
			buffer[2 + index] = (unsigned char)(relay->channel >> (24 - index * 8));
		posix_socket_sendto(p2p.tunnel_socket, buffer, P2P_RELAY_DATA_HEADER_SIZE + TUNNEL_HEADER_SIZE + sealed, 0,
			&address, sizeof(address));
		return;
	}
	posix_socket_sendto(p2p.tunnel_socket, packet, TUNNEL_HEADER_SIZE + sealed, 0, &address, sizeof(address));
}

/* to a peer the tunnel has reached; dropped otherwise */
static void peer_send(struct peer *peer, const unsigned char *inner, int size)
{
	if (!peer->connected)
		return;
	peer_send_to(peer, &peer->endpoint, inner, size);
	peer->sent_time = p2p_now();
}

static void peer_ping(struct peer *peer, const struct p2p_candidate *to)
{
	unsigned char inner[5];
	unsigned int now = (unsigned int)p2p_now();

	inner[0] = _packet_ping;
	memcpy(inner + 1, &now, 4);
	peer_send_to(peer, to, inner, sizeof(inner));
}

static void stream_free(struct stream *stream);
static void proxy_close(struct proxy *proxy);
static void listener_close(struct listener *listener);

/* everything carrying traffic for the peer (of this index) */
static void release_peer_links(int peer_index, int streams_only)
{
	struct peer *peer = &p2p.peers[peer_index];
	int index;

	for (index = 0; index < MAXIMUM_STREAMS; index++)
	{
		if (p2p.streams[index].used && p2p.streams[index].peer == peer_index)
			stream_free(&p2p.streams[index]);
	}
	if (streams_only)
		return;
	while (peer->proxy_count > 0)
		proxy_close(&p2p.proxies[peer->proxies[peer->proxy_count - 1]]);
	for (index = 0; index < MAXIMUM_LISTENERS; index++)
	{
		if (p2p.listeners[index].socket >= 0 && p2p.listeners[index].peer == peer_index)
			listener_close(&p2p.listeners[index]);
	}
}

/* what the log calls a peer: in ad hoc play every machine of the group is
a peer alike (which is the tunnel's host only picks the keys) */
static const char *peer_role(int is_host)
{
	return p2p.adhoc ? "machine" : is_host ? "host" : "player";
}

static int session_retired(const unsigned char *secret)
{
	int index;

	for (index = 0; index < MAXIMUM_RETIRED_SESSIONS; index++)
	{
		if (!memcmp(p2p.retired[index].secret, secret, P2P_SHA256_SIZE))
			return 1;
	}
	return 0;
}

int p2p_session_retired(const unsigned char *secret)
{
	return session_retired(secret);
}

/* whether address was a peer's, recently */
static int address_retired(unsigned long address)
{
	int index;

	for (index = 0; index < MAXIMUM_RETIRED_SESSIONS; index++)
	{
		if (p2p.retired[index].virtual_address == address)
			return 1;
	}
	return 0;
}

static void drop_peer(struct peer *peer, const char *reason)
{
	struct retired_session *retired = &p2p.retired[p2p.retired_next++ % MAXIMUM_RETIRED_SESSIONS];
	int ask_again = p2p.joining && peer->is_host && !memcmp(peer->identifier, p2p.join_host, P2P_IDENTIFIER_SIZE);

	platform_log("Internet play: %s %s: %s", peer_role(peer->is_host), peer->name, reason);
	if (peer->connected)
	{
		unsigned char bye = _packet_bye;

		peer_send(peer, &bye, 1);
	}
	release_peer_links((int)(peer - p2p.peers), 0);
	/* a session that ended does not come back: its packets would pass
	again */
	memcpy(retired->secret, peer->secret, P2P_SHA256_SIZE);
	retired->virtual_address = peer->virtual_address;
	memset(peer, 0, sizeof(*peer));
	/* still joining: a new request, with a nonce of its own (the host
	makes no second session from one request, which anyone who saw it could
	send again) */
	if (ask_again)
		p2p_signal_join(p2p.join_host_hash, p2p.join_token);
}

static void add_candidates(struct peer *peer, const struct p2p_candidate *candidates, int count)
{
	int index;

	for (index = 0; index < count; index++)
	{
		int known;

		for (known = 0; known < peer->candidate_count; known++)
		{
			if (peer->candidates[known].address == candidates[index].address &&
				peer->candidates[known].port == candidates[index].port)
				break;
		}
		if (known < peer->candidate_count)
			continue;
		if (peer->candidate_count < P2P_MAXIMUM_CANDIDATES)
			peer->candidates[peer->candidate_count++] = candidates[index];
		else
			peer->candidates[P2P_MAXIMUM_CANDIDATES - 1] = candidates[index];
	}
}

/* a free peer, or NULL */
static struct peer *free_peer(void)
{
	int index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		if (!p2p.peers[index].used)
			return &p2p.peers[index];
	}
	return NULL;
}

/* the players this host is reaching and has not reached yet */
static int opening_peer_count(void)
{
	int count = 0;
	int index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
		count += p2p.peers[index].used && !p2p.peers[index].connected && !p2p.peers[index].is_host;
	return count;
}

int p2p_peer_turned_away(const unsigned char *peer_identifier, int is_host)
{
	static unsigned long logged_time;
	const char *reason = NULL;

	/* (another session with the machine waits until the one that lives
	lapses: anyone with the invite can ask in its name) */
	if (!memcmp(peer_identifier, identifier, P2P_IDENTIFIER_SIZE) || find_peer(peer_identifier))
		return 1;
	if (!free_peer())
		reason = "too many players";
	else if (!is_host && opening_peer_count() >= MAXIMUM_OPENING_PEERS)
		reason = "too many players connecting at once";
	if (!reason)
		return 0;
	/* (anyone with the invite can ask as often as they like) */
	if (!logged_time || elapsed(logged_time, 10000))
	{
		platform_log("Internet play: %s; one more was turned away (it asks again)", reason);
		logged_time = now_stamp();
	}
	return 1;
}

int p2p_peer_offered(const unsigned char *peer_identifier, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count, int is_host)
{
	struct peer *peer = find_peer(peer_identifier);

	if (!memcmp(peer_identifier, identifier, P2P_IDENTIFIER_SIZE))
		return 0;
	if (peer)
	{
		/* another session with the machine waits until this one lapses:
		anyone with the invite can ask in its name */
		if (memcmp(peer->secret, secret, P2P_SHA256_SIZE))
			return 0;
	}
	else
	{
		unsigned char joiner_key[P2P_SHA256_SIZE], host_key[P2P_SHA256_SIZE];

		if (session_retired(secret) || p2p_peer_turned_away(peer_identifier, is_host))
			return 0;
		peer = free_peer();
		memset(peer, 0, sizeof(*peer));
		peer->used = 1;
		memcpy(peer->identifier, peer_identifier, P2P_IDENTIFIER_SIZE);
		p2p_hex(peer_identifier, P2P_IDENTIFIER_SIZE, peer->name);
		/* a key for each direction, so that nothing sent one way passes the
		other */
		memcpy(peer->secret, secret, P2P_SHA256_SIZE);
		p2p_hmac_sha256(secret, P2P_SHA256_SIZE, "joiner", 6, joiner_key);
		p2p_hmac_sha256(secret, P2P_SHA256_SIZE, "host", 4, host_key);
		memcpy(peer->send_key, is_host ? joiner_key : host_key, P2P_SHA256_SIZE);
		memcpy(peer->receive_key, is_host ? host_key : joiner_key, P2P_SHA256_SIZE);
		/* the allocation a relay pairs the two machines of this session by:
		only they can work it out */
		{
			unsigned char digest[P2P_SHA256_SIZE];

			p2p_hmac_sha256(secret, P2P_SHA256_SIZE, P2P_SIGNAL_PREFIX " relay allocation", 21, digest);
			memcpy(peer->relay_allocation, digest, P2P_RELAY_ALLOCATION_SIZE);
		}
		/* (packets are numbered from 1) */
		peer->receive_window = 1;
		peer->virtual_address = virtual_address_for(peer_identifier);
		peer->is_host = is_host;
		peer->offered_time = p2p_now();
		platform_log("Internet play: reaching %s %s", peer_role(is_host), peer->name);
	}
	add_candidates(peer, candidates, count);
	return 1;
}

int p2p_peer_reoffered(const unsigned char *peer_identifier, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count)
{
	struct peer *peer = find_peer(peer_identifier);

	if (!peer || memcmp(peer->secret, secret, P2P_SHA256_SIZE))
		return 0;
	add_candidates(peer, candidates, count);
	return 1;
}

/* whether a packet of this number from the peer is new: not received yet,
nor from before the window */
static int packet_fresh(const struct peer *peer, unsigned long long counter)
{
	unsigned long long behind;

	if (counter > peer->receive_highest)
		return 1;
	behind = peer->receive_highest - counter;
	return behind < REPLAY_WINDOW && !((peer->receive_window >> behind) & 1);
}

static void packet_received(struct peer *peer, unsigned long long counter)
{
	if (counter > peer->receive_highest)
	{
		unsigned long long ahead = counter - peer->receive_highest;

		peer->receive_window = ahead >= REPLAY_WINDOW ? 0 : peer->receive_window << ahead;
		peer->receive_highest = counter;
	}
	peer->receive_window |= 1ULL << (peer->receive_highest - counter);
}

/* whether a packet from address and port comes from where the peer is
known to be: its endpoint, or an address it offered */
static int peer_known_address(const struct peer *peer, unsigned long address, unsigned short port)
{
	int index;

	if (peer->endpoint.address == address && peer->endpoint.port == port)
		return 1;
	for (index = 0; index < peer->candidate_count; index++)
	{
		if (peer->candidates[index].address == address && peer->candidates[index].port == port)
			return 1;
	}
	for (index = 0; index < peer->relay_count; index++)
	{
		if (peer->relays[index].state == _relay_ready && peer->relays[index].address.address == address &&
			peer->relays[index].address.port == port)
		{
			return 1;
		}
	}
	return 0;
}

/* the status line of a joiner that reached the host (directly, or through
a relay); its first clause is what the Vita's settings panel shows of it
(in its 38 characters) */
static void set_connected_status(int relayed)
{
	set_status(relayed ? N_("connected to the host via a relay: its game is listed under Multiplayer, System Link") :
		N_("connected to the host directly: its game is listed under Multiplayer, System Link"));
}

/* newest: the packet is the highest numbered yet (a replayed or delayed one
does not move the peer's endpoint); pong: it answers a ping of this
machine's, so that path carries both ways */
static void peer_heard(struct peer *peer, unsigned long address, unsigned short port, int newest, int pong)
{
	unsigned long now = p2p_now();
	int same = peer->endpoint.address == address && peer->endpoint.port == port;
	int relayed = peer_relay_at(peer, address, port) != NULL;
	char text[32];

	peer->heard_time = now;
	if (pong)
		peer->two_way = 1;
	if (!peer->connected)
	{
		peer->connected = 1;
		peer->endpoint.address = address;
		peer->endpoint.port = port;
		peer->endpoint_heard_time = now;
		peer->via_relay = relayed;
		peer->relayed_time = now;
		platform_log("Internet play: connected to %s %s at %s, %s", peer_role(peer->is_host), peer->name,
			address_text(address, port, text), relayed ? "through the relay" : "directly");
		if (p2p.adhoc)
		{
			set_status(N_("connected to a machine of the ad hoc group: its games are listed under Multiplayer, System Link"));
		}
		else if (peer->is_host)
		{
			if (p2p.joining && !memcmp(p2p.join_host, peer->identifier, P2P_IDENTIFIER_SIZE))
			{
				p2p.joining = 0;
				p2p_signal_stop_joining();
			}
			set_connected_status(relayed);
		}
	}
	else if (same)
	{
		peer->endpoint_heard_time = now;
	}
	else if (newest && ((peer->via_relay && !relayed && pong) || elapsed(peer->endpoint_heard_time,
		ENDPOINT_SWITCH_TIME)))
	{
		/* its address changed (a NAT's mapping, or a better path): a direct
		path that answers is taken from a relay at once */
		peer->endpoint.address = address;
		peer->endpoint.port = port;
		peer->endpoint_heard_time = now;
		if (relayed != peer->via_relay)
		{
			peer->via_relay = relayed;
			peer->relayed_time = now;
			platform_log("Internet play: %s %s is now reached %s (%s)", peer_role(peer->is_host), peer->name,
				relayed ? "through the relay" : "directly", address_text(address, port, text));
			if (peer->is_host && !p2p.adhoc)
				set_connected_status(relayed);
		}
	}
}

static void relay_update_peer(struct peer *peer);

/* pings the peer everywhere it may be reached: its endpoint, the addresses
it offered, and its relays that are ready */
static void peer_punch(struct peer *peer)
{
	int index;

	if (peer->connected)
		peer_ping(peer, &peer->endpoint);
	for (index = 0; index < peer->candidate_count; index++)
	{
		if (!peer->connected || peer->candidates[index].address != peer->endpoint.address ||
			peer->candidates[index].port != peer->endpoint.port)
		{
			peer_ping(peer, &peer->candidates[index]);
		}
	}
	for (index = 0; index < peer->relay_count; index++)
	{
		if (peer->relays[index].state == _relay_ready && (!peer->connected ||
			peer->relays[index].address.address != peer->endpoint.address ||
			peer->relays[index].address.port != peer->endpoint.port))
		{
			peer_ping(peer, &peer->relays[index].address);
		}
	}
}

static void update_peers(void)
{
	int index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		struct peer *peer = &p2p.peers[index];

		if (!peer->used)
			continue;
		relay_update_peer(peer);
		if (peer->connected)
		{
			if (elapsed(peer->heard_time, PEER_TIMEOUT))
				drop_peer(peer, "lost the connection");
			else if (!peer->two_way && peer->relaying && elapsed(peer->sent_time, PUNCH_INTERVAL))
			{
				/* (heard, but no answer has come back: every path, the
				relays' too) */
				peer_punch(peer);
				peer->sent_time = p2p_now();
			}
			else if (elapsed(peer->sent_time, PING_INTERVAL))
			{
				peer_ping(peer, &peer->endpoint);
				peer->sent_time = p2p_now();
			}
			/* (through a relay: a direct path is still looked for a while) */
			if (peer->used && peer->via_relay && !elapsed(peer->relayed_time, RELAYED_PUNCH_TIME) &&
				elapsed(peer->punch_time, RELAYED_PUNCH_INTERVAL))
			{
				int candidate;

				for (candidate = 0; candidate < peer->candidate_count; candidate++)
					peer_ping(peer, &peer->candidates[candidate]);
				peer->punch_time = p2p_now();
			}
		}
		else if (elapsed(peer->offered_time, PUNCH_TIMEOUT))
		{
			drop_peer(peer, peer->relay_count ? "could not connect, directly or through the relays (are the "
				"relays up? relays.txt, network.relays)" : "could not connect (both networks' NATs may be too "
				"strict for a direct connection; a relay in relays.txt (network.relays) carries such connections, "
				"and forwarding network.tunnel_port on one router helps, as UPnP does where the router allows it: "
				"network.allow_upnp)");
		}
		else if (elapsed(peer->sent_time, PUNCH_INTERVAL))
		{
			peer_punch(peer);
			peer->sent_time = p2p_now();
		}
	}
}

/* ---------- STUN (RFC 5389): this machine's public address */

static void stun_send(struct stun_server *server)
{
	unsigned char request[20];
	struct sockaddr_in address;

	if (!server->address)
		return;
	memset(request, 0, sizeof(request));
	request[1] = 0x01; /* binding request */
	request[4] = 0x21; request[5] = 0x12; request[6] = 0xA4; request[7] = 0x42;
	/* a retry is the same transaction (RFC 5389), so a late answer to an
	earlier one is taken */
	if (!server->attempts)
		posix_random_bytes(server->transaction, sizeof(server->transaction));
	memcpy(request + 8, server->transaction, sizeof(server->transaction));
	make_address(&address, server->address, server->port);
	posix_socket_sendto(p2p.tunnel_socket, request, sizeof(request), 0, &address, sizeof(address));
	server->sent_time = p2p_now();
	server->attempts++;
}

static void stun_setup(void)
{
	const char *text = config_string("network.stun_servers");

	while (*text && p2p.stun_count < MAXIMUM_STUN_SERVERS)
	{
		const char *end = text + strcspn(text, ",");
		struct stun_server *server = &p2p.stun[p2p.stun_count];
		const char *colon;
		int length;

		while (text < end && *text == ' ')
			text++;
		length = (int)(end - text);
		while (length > 0 && text[length - 1] == ' ')
			length--;
		if (length > 0 && length < (int)sizeof(server->host))
		{
			memset(server, 0, sizeof(*server));
			memcpy(server->host, text, (size_t)length);
			colon = strchr(server->host, ':');
			server->port = network_short(3478);
			if (colon)
			{
				server->port = network_short((unsigned short)atoi(colon + 1));
				server->host[colon - server->host] = 0;
			}
			/* (its address kept for when looking it up fails) */
			p2p_resolver_cache_allow(server->host);
			p2p.stun_count++;
		}
		text = *end ? end + 1 : end;
	}
}

static void stun_update(void)
{
	int index;

	if (!p2p.stun_started)
		return;
	for (index = 0; index < p2p.stun_count; index++)
	{
		struct stun_server *server = &p2p.stun[index];

		/* (one whose name could not be looked up: again after a while) */
		if (!server->address && server->attempts >= STUN_ATTEMPTS && elapsed(server->sent_time,
			LOOKUP_RETRY_INTERVAL))
		{
			server->attempts = 0;
		}
		if (!server->address && !server->attempts)
		{
			/* looked up here on the p2p thread (its last good address if that
			fails: p2p_resolve) */
			server->address = p2p_resolve(server->host);
			if (!server->address)
			{
				if (!server->lookup_failures++)
					platform_log("Internet play: cannot look up the STUN server %s (%s); trying again every %d s",
						server->host, p2p_resolve_error(), LOOKUP_RETRY_INTERVAL / 1000);
				server->attempts = STUN_ATTEMPTS;
				server->sent_time = p2p_now();
				continue;
			}
		}
		if (server->has_mapped)
		{
			/* also keeps the NAT's mapping of the tunnel alive */
			if (elapsed(server->sent_time, STUN_REFRESH_INTERVAL))
			{
				server->attempts = 0;
				stun_send(server);
			}
		}
		else if (server->attempts < STUN_ATTEMPTS && elapsed(server->sent_time, STUN_RETRY_INTERVAL))
		{
			stun_send(server);
		}
		else if (server->attempts >= STUN_ATTEMPTS && server->address && elapsed(server->sent_time, STUN_REFRESH_INTERVAL))
		{
			server->attempts = 0;
		}
	}
}

static void stun_received(const unsigned char *packet, int size, const struct sockaddr_in *from)
{
	int index;
	int offset;

	if (size < 20 || packet[0] != 0x01 || packet[1] != 0x01)
		return;
	/* the answer of a server asked, from it */
	for (index = 0; index < p2p.stun_count; index++)
	{
		struct stun_server const *server = &p2p.stun[index];

		if (server->address && server->address == from->sin_addr.s_addr && server->port == from->sin_port &&
			!memcmp(packet + 8, server->transaction, 12))
		{
			break;
		}
	}
	if (index == p2p.stun_count)
		return;
	for (offset = 20; offset + 4 <= size; )
	{
		int type = packet[offset] << 8 | packet[offset + 1];
		int length = packet[offset + 2] << 8 | packet[offset + 3];
		const unsigned char *value = packet + offset + 4;

		if (offset + 4 + length > size)
			break;
		/* XOR-MAPPED-ADDRESS, or MAPPED-ADDRESS from an old server; IPv4 */
		if ((type == 0x0020 || type == 0x0001) && length >= 8 && value[1] == 0x01)
		{
			struct stun_server *server = &p2p.stun[index];
			unsigned char port[2], ip[4];
			int byte;

			memcpy(port, value + 2, 2);
			memcpy(ip, value + 4, 4);
			if (type == 0x0020)
			{
				for (byte = 0; byte < 2; byte++)
					port[byte] ^= packet[4 + byte];
				for (byte = 0; byte < 4; byte++)
					ip[byte] ^= packet[4 + byte];
			}
			if (!server->has_mapped)
			{
				char text[32];
				int other;

				memcpy(&server->mapped.address, ip, 4);
				memcpy(&server->mapped.port, port, 2);
				server->has_mapped = 1;
				platform_log("Internet play: this machine's public address is %s (from %s)",
					address_text(server->mapped.address, server->mapped.port, text), server->host);
				for (other = 0; other < p2p.stun_count; other++)
				{
					if (other != index && p2p.stun[other].has_mapped &&
						p2p.stun[other].mapped.port != server->mapped.port && !p2p.reported_symmetric)
					{
						platform_log("Internet play: this network's NAT gives each destination its own "
							"port, so it can only connect to machines behind more lenient ones");
						p2p.reported_symmetric = 1;
					}
				}
			}
			else
			{
				memcpy(&server->mapped.address, ip, 4);
				memcpy(&server->mapped.port, port, 2);
			}
			break;
		}
		offset += 4 + ((length + 3) & ~3);
	}
}

int p2p_local_candidates(struct p2p_candidate *candidates, int maximum_count)
{
	unsigned long lan = posix_local_ipv4_address();
	int count = 0;
	int index;

	if (lan && count < maximum_count)
	{
		candidates[count].address = lan;
		candidates[count++].port = p2p.tunnel_port;
	}
	/* (the port the router forwards here, UPnP) */
	if (p2p.upnp_forwarded && count < maximum_count)
		candidates[count++] = p2p.upnp_candidate;
#ifdef HALO_DEDICATED_SERVER
	/* (a dedicated server's public address as its operator gives it,
	sv_public_address: HALO_SERVER_PUBLIC_ADDRESS, a.b.c.d[:port], the port
	the router forwards to internet play's) */
	{
		const char *text = getenv("HALO_SERVER_PUBLIC_ADDRESS");
		unsigned int parts[4], port = 0;
		char end = 0;
		int fields = text ? sscanf(text, "%u.%u.%u.%u:%u%c", &parts[0], &parts[1], &parts[2], &parts[3], &port, &end) : 0;

		if ((fields == 4 || fields == 5) && parts[0] < 256 && parts[1] < 256 && parts[2] < 256 && parts[3] < 256 &&
			port < 65536 && count < maximum_count)
		{
			candidates[count].address = network_long((unsigned long)parts[0] << 24 | parts[1] << 16 | parts[2] << 8 |
				parts[3]);
			candidates[count++].port = fields == 5 && port ? network_short((unsigned short)port) : p2p.tunnel_port;
		}
	}
#endif
	for (index = 0; index < p2p.stun_count && count < maximum_count; index++)
	{
		int known;

		if (!p2p.stun[index].has_mapped)
			continue;
		for (known = 0; known < count; known++)
		{
			if (candidates[known].address == p2p.stun[index].mapped.address &&
				candidates[known].port == p2p.stun[index].mapped.port)
				break;
		}
		if (known == count)
			candidates[count++] = p2p.stun[index].mapped;
	}
	/* two copies of the game on one machine without a network */
	if (!count && maximum_count)
	{
		candidates[count].address = network_long(0x7F000001);
		candidates[count++].port = p2p.tunnel_port;
	}
	return count;
}

static int stun_settled(void)
{
	int index;

	for (index = 0; index < p2p.stun_count; index++)
	{
		if (!p2p.stun[index].has_mapped && p2p.stun[index].attempts < STUN_ATTEMPTS)
			return 0;
	}
	return 1;
}

/* ---------- relays (port/relay, p2p_relay_protocol.h) */

/* whether an address a peer offered as a relay may be one: not this
machine's own, nor nothing, a broadcast or a multicast group */
static int relay_address_usable(const struct p2p_candidate *relay)
{
	unsigned long value = network_long(relay->address);

	return relay->port && value && value != 0xFFFFFFFF && (value >> 24) != 127 && (value >> 28) != 14;
}

/* the relays named (network.relays, else network.relays_file), when
internet play starts: looked up later, on the p2p thread */
static void relay_setup(void)
{
	char text[1024];
	const char *entry;

	p2p.relay_allowed = config_boolean("network.allow_relay") && !p2p.adhoc;
	if (!p2p.relay_allowed)
		return;
	p2p_list_setting("network.relays", "network.relays_file", text, sizeof(text));
	for (entry = text; *entry && p2p.relay_host_count < P2P_MAXIMUM_RELAYS; )
	{
		const char *end = entry + strcspn(entry, ",");
		char *host = p2p.relay_hosts[p2p.relay_host_count];
		char *colon;
		int length;
		long port = 47320;

		while (entry < end && *entry == ' ')
			entry++;
		length = (int)(end - entry);
		while (length > 0 && entry[length - 1] == ' ')
			length--;
		if (length > 0 && length < (int)sizeof(p2p.relay_hosts[0]))
		{
			memcpy(host, entry, (size_t)length);
			host[length] = 0;
			colon = strchr(host, ':');
			if (colon)
			{
				port = strtol(colon + 1, NULL, 10);
				*colon = 0;
			}
			if (host[0] && port > 0 && port <= 65535)
			{
				p2p_resolver_cache_allow(host);
				p2p.relay_ports[p2p.relay_host_count++] = network_short((unsigned short)port);
			}
			else
				platform_log("Internet play: the relay \"%.*s\" is not a host:port; left out", length, entry);
		}
		entry = *end ? end + 1 : end;
	}
}

/* looks the relays up when STUN starts (a game hosted or joined), and one
whose name could not be looked up again each LOOKUP_RETRY_INTERVAL */
static void relay_resolve(void)
{
	int index;

	if (!p2p.stun_started || (p2p.relays_resolved && (p2p.relay_count == p2p.relay_host_count ||
		!elapsed(p2p.relay_lookup_time, LOOKUP_RETRY_INTERVAL))))
	{
		return;
	}
	p2p.relays_resolved = 1;
	p2p.relay_lookup_time = p2p_now();
	for (index = 0; index < p2p.relay_host_count && p2p.relay_count < P2P_MAXIMUM_RELAYS; index++)
	{
		struct p2p_candidate *relay = &p2p.relays[p2p.relay_count];
		char text[32];

		if (p2p.relay_host_found[index])
			continue;
		relay->address = p2p_resolve(p2p.relay_hosts[index]);
		relay->port = p2p.relay_ports[index];
		if (!relay->address)
		{
			if (!p2p.relay_lookup_failures[index]++)
				platform_log("Internet play: cannot look up the relay %s (%s); trying again every %d s",
					p2p.relay_hosts[index], p2p_resolve_error(), LOOKUP_RETRY_INTERVAL / 1000);
			continue;
		}
		p2p.relay_host_found[index] = 1;
		platform_log("Internet play: the relay %s is at %s", p2p.relay_hosts[index],
			address_text(relay->address, relay->port, text));
		p2p.relay_count++;
	}
}

int p2p_local_relays(struct p2p_candidate *relays, int maximum_count)
{
	int count = 0;

	for (; p2p.relay_allowed && count < p2p.relay_count && count < maximum_count; count++)
		relays[count] = p2p.relays[count];
	return count;
}

/* adds the relay to those the peer is asked for through, if it is not
there yet */
static void relay_add(struct peer *peer, const struct p2p_candidate *address)
{
	struct peer_relay *relay;
	int index;

	for (index = 0; index < peer->relay_count; index++)
	{
		if (peer->relays[index].address.address == address->address &&
			peer->relays[index].address.port == address->port)
		{
			return;
		}
	}
	if (peer->relay_count >= MAXIMUM_PEER_RELAYS)
		return;
	relay = &peer->relays[peer->relay_count++];
	memset(relay, 0, sizeof(*relay));
	relay->address = *address;
	relay->state = _relay_asking;
	posix_random_bytes(relay->nonce, sizeof(relay->nonce));
}

/* the relays a peer is asked for through: the host's first (both machines
put them in the same order), then the joiner's */
static void relay_choose(struct peer *peer)
{
	int index, side;

	for (side = 0; side < 2; side++)
	{
		/* (side 0: the host's; the peer is the host on a joiner) */
		int own = peer->is_host ? side == 1 : side == 0;

		if (own)
		{
			for (index = 0; index < p2p.relay_count; index++)
				relay_add(peer, &p2p.relays[index]);
		}
		else
		{
			for (index = 0; index < peer->offered_relay_count; index++)
				relay_add(peer, &peer->offered_relays[index]);
		}
	}
}

void p2p_peer_relays(const unsigned char *peer_identifier, const unsigned char *secret,
	const struct p2p_candidate *relays, int count)
{
	struct peer *peer = find_peer(peer_identifier);
	int index;

	if (!peer || memcmp(peer->secret, secret, P2P_SHA256_SIZE))
		return;
	peer->offered_relay_count = 0;
	for (index = 0; index < count && peer->offered_relay_count < P2P_MAXIMUM_RELAYS; index++)
	{
		if (relay_address_usable(&relays[index]))
			peer->offered_relays[peer->offered_relay_count++] = relays[index];
	}
	if (peer->relaying)
		relay_choose(peer);
}

/* asks a relay for the peer's allocation (with its cookie, once given) */
static void relay_ask(struct peer *peer, struct peer_relay *relay)
{
	unsigned char request[P2P_RELAY_ALLOCATE_SIZE];
	struct sockaddr_in address;

	memset(request, 0, sizeof(request));
	request[0] = P2P_RELAY_MAGIC;
	request[1] = _relay_allocate;
	request[2] = P2P_RELAY_VERSION;
	/* (0: the host's side; the peer is the host on a joiner) */
	request[P2P_RELAY_ROLE_OFFSET] = (unsigned char)(peer->is_host ? 1 : 0);
	memcpy(request + P2P_RELAY_NONCE_OFFSET, relay->nonce, P2P_RELAY_NONCE_SIZE);
	memcpy(request + P2P_RELAY_COOKIE_OFFSET, relay->cookie, P2P_RELAY_COOKIE_SIZE);
	memcpy(request + P2P_RELAY_ALLOCATION_OFFSET, peer->relay_allocation, P2P_RELAY_ALLOCATION_SIZE);
	make_address(&address, relay->address.address, relay->address.port);
	posix_socket_sendto(p2p.tunnel_socket, request, sizeof(request), 0, &address, sizeof(address));
	relay->sent_time = p2p_now();
}

/* each pass, for each peer: its relays asked for, kept, or let lapse */
static void relay_update_peer(struct peer *peer)
{
	int index;

	if (!p2p.relay_allowed || p2p.adhoc)
		return;
	if (!peer->relaying)
	{
		if (peer->two_way || !elapsed(peer->offered_time, RELAY_DELAY))
			return;
		relay_choose(peer);
		if (!peer->relay_count)
			return;
		peer->relaying = 1;
		platform_log("Internet play: %s %s not reached both ways in %d s: asking through %d relay%s too",
			peer_role(peer->is_host), peer->name, RELAY_DELAY / 1000, peer->relay_count,
			peer->relay_count == 1 ? "" : "s");
		if (peer->is_host && !peer->connected)
			set_status(N_("trying a relay to reach the host"));
	}
	for (index = 0; index < peer->relay_count; index++)
	{
		struct peer_relay *relay = &peer->relays[index];
		/* (the relay carries the peer now) */
		int in_use = peer->connected && relay->address.address == peer->endpoint.address &&
			relay->address.port == peer->endpoint.port;
		int interval;

		/* (once the peer is reached both ways elsewhere, an allocation
		lapses at the relay: no more requests) */
		if (peer->two_way && !in_use)
			continue;
		interval = relay->state == _relay_ready ? RELAY_REFRESH_INTERVAL :
			relay->state == _relay_refused ? RELAY_REFUSED_INTERVAL : RELAY_RETRY_INTERVAL;
		if (!relay->sent_time || elapsed(relay->sent_time, (unsigned long)interval))
			relay_ask(peer, relay);
	}
}

/* the relay of a peer at that address whose requests carry this nonce, and
its peer; NULL if none */
static struct peer_relay *relay_find(const struct sockaddr_in *from, const unsigned char *nonce, struct peer **owner)
{
	int index, entry;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		struct peer *peer = &p2p.peers[index];

		for (entry = 0; peer->used && entry < peer->relay_count; entry++)
		{
			struct peer_relay *relay = &peer->relays[entry];

			if (relay->address.address == from->sin_addr.s_addr && relay->address.port == from->sin_port &&
				p2p_equal(relay->nonce, nonce, P2P_RELAY_NONCE_SIZE))
			{
				*owner = peer;
				return relay;
			}
		}
	}
	return NULL;
}

static void tunnel_received(const unsigned char *packet, int size, const struct sockaddr_in *from);

/* a message of a relay's: an answer to a request, or a peer's tunnel
packet passed on (from a relay that carries the peer, on its channel) */
static void relay_received(const unsigned char *packet, int size, const struct sockaddr_in *from)
{
	struct peer_relay *relay;
	struct peer *peer = NULL;
	int index, entry;

	if (size < 3 || p2p.adhoc)
		return;
	if (packet[1] == _relay_data)
	{
		unsigned long channel;

		if (size < P2P_RELAY_DATA_HEADER_SIZE + P2P_RELAY_MINIMUM_TUNNEL_PACKET ||
			packet[P2P_RELAY_DATA_HEADER_SIZE] != TUNNEL_MAGIC)
		{
			return;
		}
		channel = (unsigned long)packet[2] << 24 | (unsigned long)packet[3] << 16 | (unsigned long)packet[4] << 8 |
			packet[5];
		for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
		{
			peer = &p2p.peers[index];
			for (entry = 0; peer->used && entry < peer->relay_count; entry++)
			{
				relay = &peer->relays[entry];
				/* (the sender in the packet's header must be the peer the
				channel is for: its seal is checked as any packet's) */
				if (relay->state == _relay_ready && relay->channel == channel &&
					relay->address.address == from->sin_addr.s_addr && relay->address.port == from->sin_port &&
					!memcmp(packet + P2P_RELAY_DATA_HEADER_SIZE + 1, peer->identifier, P2P_IDENTIFIER_SIZE))
				{
					tunnel_received(packet + P2P_RELAY_DATA_HEADER_SIZE, size - P2P_RELAY_DATA_HEADER_SIZE, from);
					return;
				}
			}
		}
		return;
	}
	if (packet[2] != P2P_RELAY_VERSION)
		return;
	if (packet[1] == _relay_cookie && size == P2P_RELAY_COOKIE_MESSAGE_SIZE)
	{
		const unsigned char *cookie = packet + 4 + P2P_RELAY_NONCE_SIZE;

		relay = relay_find(from, packet + 4, &peer);
		/* (a new cookie: asked again with it at once) */
		if (relay && !p2p_equal(relay->cookie, cookie, P2P_RELAY_COOKIE_SIZE))
		{
			memcpy(relay->cookie, cookie, P2P_RELAY_COOKIE_SIZE);
			if (!peer->two_way || relay->state == _relay_ready)
				relay_ask(peer, relay);
		}
	}
	else if (packet[1] == _relay_allocated && size == P2P_RELAY_ALLOCATED_SIZE)
	{
		int status = packet[3];
		char text[32];

		relay = relay_find(from, packet + P2P_RELAY_NONCE_OFFSET, &peer);
		if (!relay)
			return;
		relay->channel = (unsigned long)packet[P2P_RELAY_CHANNEL_OFFSET] << 24 |
			(unsigned long)packet[P2P_RELAY_CHANNEL_OFFSET + 1] << 16 |
			(unsigned long)packet[P2P_RELAY_CHANNEL_OFFSET + 2] << 8 | packet[P2P_RELAY_CHANNEL_OFFSET + 3];
		if (status == P2P_RELAY_READY && relay->channel)
		{
			if (relay->state != _relay_ready)
			{
				platform_log("Internet play: the relay %s carries %s %s",
					address_text(relay->address.address, relay->address.port, text), peer_role(peer->is_host),
					peer->name);
				relay->state = _relay_ready;
				/* (tried at once) */
				peer_ping(peer, &relay->address);
			}
		}
		else if (status == P2P_RELAY_WAITING && relay->channel)
			relay->state = _relay_waiting;
		else if (status == P2P_RELAY_BUSY || status == P2P_RELAY_TAKEN)
		{
			if (relay->state != _relay_refused)
				platform_log("Internet play: the relay %s refused %s %s (%s)",
					address_text(relay->address.address, relay->address.port, text), peer_role(peer->is_host),
					peer->name, status == P2P_RELAY_BUSY ? "it is full" : "another machine took its side");
			relay->state = _relay_refused;
		}
	}
}

/* ---------- stand-ins for peers' ports */

static void close_socket(int *socket)
{
	if (*socket >= 0)
		posix_socket_close(*socket);
	*socket = -1;
}

/* a stand-in of a peer's port closes: traffic from it that the game has not
read yet still comes from the peer (p2p_incoming) */
static void remember_closed(int stream, unsigned short local_port, int peer_index, unsigned short remote_port)
{
	struct closed_port *closed = &p2p.closed[p2p.closed_next++ % MAXIMUM_CLOSED_PORTS];

	closed->stream = stream;
	closed->local_port = local_port;
	closed->virtual_address = p2p.peers[peer_index].virtual_address;
	closed->remote_port = remote_port;
	closed->time = p2p_now();
}

/* the peer's address and port for a stand-in closed lately; 0 if none */
static int find_closed(int stream, unsigned long *address, unsigned short *port)
{
	int index;

	for (index = 0; index < MAXIMUM_CLOSED_PORTS; index++)
	{
		struct closed_port const *closed = &p2p.closed[index];

		/* (not for long: the system gives the port to other sockets again) */
		if (closed->local_port == *port && closed->stream == stream && closed->virtual_address &&
			!elapsed(closed->time, CLOSED_PORT_TIME))
		{
			*address = closed->virtual_address;
			*port = closed->remote_port;
			return 1;
		}
	}
	return 0;
}

/* a stand-in, or a socket of the game's, that has the port now: no closed
stand-in stands for it */
static void forget_closed(int stream, unsigned short local_port)
{
	int index;

	for (index = 0; index < MAXIMUM_CLOSED_PORTS; index++)
	{
		if (p2p.closed[index].local_port == local_port && p2p.closed[index].stream == stream)
			p2p.closed[index].virtual_address = 0;
	}
}

static void proxy_close(struct proxy *proxy)
{
	struct peer *peer = &p2p.peers[proxy->peer];
	int index = (int)(proxy - p2p.proxies);
	int entry;

	if (proxy->socket < 0)
		return;
	close_socket(&proxy->socket);
	proxy_by_port[proxy->local_port] = 0;
	remember_closed(0, proxy->local_port, proxy->peer, proxy->remote_port);
	for (entry = 0; entry < peer->proxy_count; entry++)
	{
		if (peer->proxies[entry] == index)
		{
			peer->proxies[entry] = peer->proxies[--peer->proxy_count];
			break;
		}
	}
}

static void listener_close(struct listener *listener)
{
	if (listener->socket < 0)
		return;
	close_socket(&listener->socket);
	remember_closed(1, listener->local_port, listener->peer, listener->remote_port);
}

static struct proxy *find_proxy(int peer_index, unsigned short remote_port, int create)
{
	struct peer *peer = &p2p.peers[peer_index];
	struct proxy *free_proxy = NULL;
	struct proxy *oldest = NULL;
	int index;

	/* (a peer has only a few) */
	for (index = 0; index < peer->proxy_count; index++)
	{
		struct proxy *proxy = &p2p.proxies[peer->proxies[index]];

		if (proxy->remote_port == remote_port)
		{
			proxy->used_time = p2p_now();
			return proxy;
		}
		if (proxy->pinned_socket < 0 && (!oldest || (long)(proxy->used_time - oldest->used_time) < 0))
			oldest = proxy;
	}
	if (!create)
		return NULL;
	/* past a peer's few, its least used goes: no peer takes them all */
	if (peer->proxy_count >= MAXIMUM_PEER_PROXIES)
	{
		if (!oldest || !elapsed(oldest->used_time, PROXY_REPLACE_TIME))
			return NULL;
		proxy_close(oldest);
		free_proxy = oldest;
	}
	for (index = 0; index < MAXIMUM_PROXIES && !free_proxy; index++)
	{
		if (p2p.proxies[index].socket < 0)
			free_proxy = &p2p.proxies[index];
	}
	if (!free_proxy)
		return NULL;
	free_proxy->socket = open_socket(SOCK_DGRAM, p2p.local_address, 0, &free_proxy->local_port);
	if (free_proxy->socket < 0)
		return NULL;
	free_proxy->peer = peer_index;
	free_proxy->remote_port = remote_port;
	free_proxy->used_time = p2p_now();
	free_proxy->pinned_socket = -1;
	peer->proxies[peer->proxy_count++] = (short)(free_proxy - p2p.proxies);
	proxy_by_port[free_proxy->local_port] = (unsigned short)(free_proxy - p2p.proxies + 1);
	forget_closed(0, free_proxy->local_port);
	return free_proxy;
}

/* stand-ins unused for a while go (a peer's ports change with its game's
sockets) */
static void expire_proxies(void)
{
	int index;

	for (index = 0; index < MAXIMUM_PROXIES; index++)
	{
		struct proxy *proxy = &p2p.proxies[index];

		if (proxy->socket >= 0 && proxy->pinned_socket < 0 && elapsed(proxy->used_time, PROXY_IDLE_TIME))
			proxy_close(proxy);
	}
}

static struct listener *find_listener(int peer_index, unsigned short remote_port)
{
	struct listener *free_listener = NULL;
	int index;

	for (index = 0; index < MAXIMUM_LISTENERS; index++)
	{
		struct listener *listener = &p2p.listeners[index];

		if (listener->socket < 0)
		{
			if (!free_listener)
				free_listener = listener;
		}
		else if (listener->peer == peer_index && listener->remote_port == remote_port)
			return listener;
	}
	if (!free_listener)
		return NULL;
	free_listener->socket = open_socket(SOCK_STREAM, p2p.local_address, 0, &free_listener->local_port);
	if (free_listener->socket < 0)
		return NULL;
	if (posix_socket_listen(free_listener->socket, 8) < 0)
	{
		close_socket(&free_listener->socket);
		return NULL;
	}
	free_listener->peer = peer_index;
	free_listener->remote_port = remote_port;
	forget_closed(1, free_listener->local_port);
	return free_listener;
}

int p2p_outgoing(int stream, int socket, unsigned long *address, unsigned short *port)
{
	struct peer *peer;
	int result = 0;

	if (!is_virtual_address(*address) || !p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	peer = find_peer_by_address(*address);
	/* a peer's, or one that was: never to the address itself (100.64.0.0/10
	is also a carrier's NAT's and some VPNs', which the game's traffic is not
	for) */
	if (!peer)
		result = address_retired(*address) ? -1 : 0;
	else if (!peer->connected)
		result = -1;
	else if (stream)
	{
		struct listener *listener = find_listener((int)(peer - p2p.peers), *port);

		result = -1;
		if (listener)
		{
			*address = p2p.local_address;
			*port = listener->local_port;
			result = 1;
		}
	}
	else
	{
		struct proxy *proxy = find_proxy((int)(peer - p2p.peers), *port, 1);

		result = -1;
		if (proxy)
		{
			*address = p2p.local_address;
			*port = proxy->local_port;
			if (socket >= 0)
				proxy->pinned_socket = socket;
			result = 1;
		}
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

int p2p_incoming(int stream, unsigned long *address, unsigned short *port)
{
	int result = 0;
	int index;

	if (!p2p.running || *address != p2p.local_address)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	if (stream)
	{
		for (index = 0; index < MAXIMUM_LISTENERS && !result; index++)
		{
			struct listener *listener = &p2p.listeners[index];

			if (listener->socket >= 0 && listener->local_port == *port)
			{
				*address = p2p.peers[listener->peer].virtual_address;
				*port = listener->remote_port;
				result = 1;
			}
		}
		for (index = 0; index < MAXIMUM_STREAMS && !result; index++)
		{
			struct stream *entry = &p2p.streams[index];

			if (entry->used && entry->local_port && entry->local_port == *port)
			{
				*address = p2p.peers[entry->peer].virtual_address;
				*port = entry->remote_port;
				result = 1;
			}
		}
	}
	else if (proxy_by_port[*port])
	{
		struct proxy const *proxy = &p2p.proxies[proxy_by_port[*port] - 1];

		*address = p2p.peers[proxy->peer].virtual_address;
		*port = proxy->remote_port;
		result = 1;
	}
	/* one that closed since (a peer's stand-ins past its few, a stream
	ended before the game took its connection) is still the peer's: as
	127.0.0.1 it would be this machine's own */
	if (!result)
		result = find_closed(stream, address, port);
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

int p2p_spoofed_source(unsigned long address)
{
	int result;

	/* (the game's every datagram: a peer's arrive from 127.0.0.1 or the
	local address, which is no virtual one, so the lock is never taken) */
	if (!p2p.running || !is_virtual_address(address))
		return 0;
	pthread_mutex_lock(&p2p_lock);
	result = find_peer_by_address(address) != NULL || address_retired(address);
	pthread_mutex_unlock(&p2p_lock);
	if (result)
	{
		static unsigned long logged_time;

		/* (anyone who can reach the game's port may send them as fast as they
		like) */
		if (!logged_time || elapsed(logged_time, 10000))
		{
			char text[32];

			platform_log("Internet play: dropped traffic to the game's port claiming to come from a peer's "
				"address %s (spoofed)", address_text(address, 0, text));
			logged_time = now_stamp();
		}
	}
	return result;
}

int p2p_broadcast_targets(unsigned short port, unsigned long *addresses, unsigned short *ports, int maximum_count)
{
	int count = 0;
	int index;

	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	for (index = 0; index < P2P_MAXIMUM_PEERS && count < maximum_count; index++)
	{
		struct proxy *proxy;

		if (!p2p.peers[index].used || !p2p.peers[index].connected)
			continue;
		proxy = find_proxy(index, port, 1);
		if (proxy)
		{
			addresses[count] = p2p.local_address;
			ports[count++] = proxy->local_port;
		}
	}
	pthread_mutex_unlock(&p2p_lock);
	return count;
}

static void datagram_send(struct peer *peer, unsigned short source_port, unsigned short port, const void *data,
	int size);

int p2p_send_datagram(unsigned short source_port, unsigned long address, unsigned short port, const void *data,
	int size)
{
	struct peer *peer;
	int result;

	if (!is_virtual_address(address) || !p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	peer = find_peer_by_address(address);
	if (!peer)
		result = address_retired(address) ? -1 : 0;
	else if (!peer->connected)
		result = -1;
	else
	{
		datagram_send(peer, source_port, port, data, size);
		result = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

int p2p_broadcast_datagram(unsigned short source_port, unsigned short port, const void *data, int size)
{
	int count = 0;
	int index;

	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		if (p2p.peers[index].used && p2p.peers[index].connected)
		{
			datagram_send(&p2p.peers[index], source_port, port, data, size);
			count++;
		}
	}
	pthread_mutex_unlock(&p2p_lock);
	return count;
}

int p2p_peer_address(const unsigned char *peer_identifier, unsigned long *address)
{
	struct peer *peer;
	int result = 0;

	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	/* reached or not (until it is, sending there fails: p2p_outgoing) */
	peer = find_peer(peer_identifier);
	if (peer)
	{
		*address = peer->virtual_address;
		result = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

/* ---------- the game's ports: all that peers may reach */

void p2p_socket_port(int socket, int stream, int listening, unsigned short port)
{
	struct game_port *entry = NULL;
	int index;

	if (!p2p.running || !port)
		return;
	pthread_mutex_lock(&p2p_lock);
	for (index = 0; index < MAXIMUM_GAME_PORTS; index++)
	{
		struct game_port *known = &p2p.game_ports[index];

		if (known->port && known->socket == socket)
		{
			entry = known;
			break;
		}
		if (!known->port && !entry)
			entry = known;
	}
	if (entry)
	{
		entry->socket = socket;
		entry->port = port;
		entry->stream = (unsigned char)(stream != 0);
		entry->listening |= (unsigned char)(listening != 0);
	}
	/* the game listens for connections while it hosts */
	if (stream && listening)
		p2p.hosting_socket = socket;
	/* (and no stand-in that closed has the port now) */
	forget_closed(stream != 0, port);
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_port_taken(int stream, unsigned short port)
{
	if (!p2p.running || !port)
		return;
	pthread_mutex_lock(&p2p_lock);
	forget_closed(stream != 0, port);
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_socket_closed(int socket, unsigned short datagram_port)
{
	int index;

	if (!p2p.running)
		return;
	pthread_mutex_lock(&p2p_lock);
	/* (a port it sent peers datagrams from, which the system gives again) */
	for (index = 0; index < MAXIMUM_SENT_PORTS && datagram_port; index++)
	{
		if (p2p.sent_ports[index] == datagram_port)
			p2p.sent_ports[index] = 0;
	}
	for (index = 0; index < MAXIMUM_GAME_PORTS; index++)
	{
		if (p2p.game_ports[index].port && p2p.game_ports[index].socket == socket)
			memset(&p2p.game_ports[index], 0, sizeof(p2p.game_ports[index]));
	}
	for (index = 0; index < MAXIMUM_PROXIES; index++)
	{
		if (p2p.proxies[index].socket >= 0 && p2p.proxies[index].pinned_socket == socket)
			p2p.proxies[index].pinned_socket = -1;
	}
	if (p2p.hosting_socket == socket)
		p2p.hosting_socket = -1;
	pthread_mutex_unlock(&p2p_lock);
}

/* the dedicated server's game ports (p2p.h): its server's port here, in
host byte order (5150: the game's own, nothing moved), read once from
HALO_NET_GAME_PORT (posix_dedicated_server.c sets it: sv_game_port,
-gameport, else one for sv_port) */
static unsigned short game_port_base(void)
{
#ifdef HALO_DEDICATED_SERVER
	static volatile int base;

	if (!base)
	{
		const char *text = getenv("HALO_NET_GAME_PORT");
		long value = text && *text ? strtol(text, NULL, 10) : 0;

		/* (two ports: the server's and the client's after it) */
		base = value >= 1 && value <= 65534 && value != P2P_GAME_SERVER_PORT - 1 && value != P2P_GAME_CLIENT_PORT ?
			(int)value : P2P_GAME_SERVER_PORT;
	}
	return (unsigned short)base;
#else
	return P2P_GAME_SERVER_PORT;
#endif
}

int p2p_game_ports_moved(void)
{
	return game_port_base() != P2P_GAME_SERVER_PORT;
}

/* (both ways: the game's 5150 and 5151 and the ports here swap, so that no
other number reaches the server's, nor the game's numbers another program
here that has them: another server's) */
static unsigned short game_port_swap(unsigned short port)
{
	unsigned short base = game_port_base();
	unsigned short value = network_short(port);

	if (base == P2P_GAME_SERVER_PORT)
		return port;
	if (value == P2P_GAME_SERVER_PORT || value == P2P_GAME_CLIENT_PORT)
		return network_short((unsigned short)(base + (value - P2P_GAME_SERVER_PORT)));
	if (value == base || value == base + 1)
		return network_short((unsigned short)(P2P_GAME_SERVER_PORT + (value - base)));
	return port;
}

unsigned short p2p_game_port_local(unsigned short port)
{
	return game_port_swap(port);
}

unsigned short p2p_game_port_wire(unsigned short port)
{
	return game_port_swap(port);
}

/* whether the game's server is a Split Screen game's: it listens, but takes
no other machine (p2p_set_game_accepts_remote). Nothing of it is hosted,
and no peer reaches the game: the game turns away a machine not on this one
itself (network_game_server_add_new_client), and does not answer searches
(network_server_message_handler.c), but a peer from before (a game hosted
earlier, a game joined) is kept out here too */
static int game_local(void)
{
	return p2p.hosting_socket >= 0 && !p2p.game_accepts_remote;
}

/* whether a peer may reach this port of the game's: one a socket of the
game's listens on (stream), or a datagram socket's, bound or sent from to a
peer */
static int game_port_open(int stream, unsigned short port)
{
	int index;

	for (index = 0; index < MAXIMUM_GAME_PORTS; index++)
	{
		struct game_port const *entry = &p2p.game_ports[index];

		if (entry->port == port && entry->stream == (stream != 0) && (!stream || entry->listening))
			return 1;
	}
	for (index = 0; index < MAXIMUM_SENT_PORTS && !stream; index++)
	{
		if (port && p2p.sent_ports[index] == port)
			return 1;
	}
	return 0;
}

/* ---------- datagrams */

/* a datagram from the game's port source_port to the peer's port */
static void datagram_send(struct peer *peer, unsigned short source_port, unsigned short port, const void *data,
	int size)
{
	unsigned char inner[MAXIMUM_INNER_SIZE];

	if (size < 0 || size > MAXIMUM_INNER_SIZE - 5)
		return;
	/* (the peer answers to that port) */
	if (!game_port_open(0, source_port))
		p2p.sent_ports[p2p.sent_port_next++ % MAXIMUM_SENT_PORTS] = source_port;
	inner[0] = _packet_datagram;
	/* (the game's port as the peer knows it: a dedicated server's own here
	are others, p2p_game_port_local) */
	put_short(inner + 1, p2p_game_port_wire(source_port));
	put_short(inner + 3, port);
	memcpy(inner + 5, data, (size_t)size);
	peer_send(peer, inner, size + 5);
}

/* a datagram from the game to a peer, through its stand-in (from a socket
connected to it, or not bound yet: p2p_send_datagram sends the rest) */
static void proxy_readable(struct proxy *proxy)
{
	unsigned char data[MAXIMUM_INNER_SIZE - 5];
	int count;

	for (count = 0; count < 64; count++)
	{
		struct sockaddr_in from;
		int from_length = sizeof(from);
		int size = posix_socket_recvfrom(proxy->socket, data, sizeof(data), 0, &from, &from_length);

		if (size < 0)
			break;
		/* only the game's own sockets use a stand-in */
		if (from.sin_addr.s_addr != p2p.local_address && from.sin_addr.s_addr != network_long(0x7F000001))
			continue;
		proxy->used_time = p2p_now();
		datagram_send(&p2p.peers[proxy->peer], from.sin_port, proxy->remote_port, data, size);
	}
}

/* a datagram from a peer to the game */
static void datagram_received(struct peer *peer, const unsigned char *inner, int size)
{
	struct proxy *proxy;
	struct sockaddr_in to;
	unsigned short port;

	if (size < 5)
		return;
	/* only to the game (at its ports here: p2p_game_port_local) */
	port = p2p_game_port_local(get_short(inner + 3));
	if (game_local() || !game_port_open(0, port))
		return;
	proxy = find_proxy((int)(peer - p2p.peers), get_short(inner + 1), 1);
	if (!proxy)
		return;
	make_address(&to, p2p.local_address, port);
	posix_socket_sendto(proxy->socket, inner + 5, size - 5, 0, &to, sizeof(to));
}

/* ---------- streams */

static int kcp_output(const char *buffer, int size, ikcpcb *kcp, void *user)
{
	struct stream *stream = user;
	unsigned char inner[MAXIMUM_INNER_SIZE];

	(void)kcp;
	if (size + 1 > (int)sizeof(inner))
		return -1;
	inner[0] = _packet_stream;
	memcpy(inner + 1, buffer, (size_t)size);
	peer_send(&p2p.peers[stream->peer], inner, size + 1);
	return 0;
}

static struct stream *stream_new(int peer_index, IUINT32 conversation)
{
	int index;

	for (index = 0; index < MAXIMUM_STREAMS; index++)
	{
		struct stream *stream = &p2p.streams[index];

		if (stream->used)
			continue;
		/* all but the buffer, which pending_size (after it) says is empty */
		memset(stream, 0, offsetof(struct stream, pending));
		stream->pending_size = 0;
		stream->kcp = ikcp_create(conversation, stream);
		if (!stream->kcp)
			return NULL;
		stream->used = 1;
		stream->peer = peer_index;
		stream->conversation = conversation;
		stream->socket = -1;
		stream->created_time = p2p_now();
		ikcp_setoutput(stream->kcp, kcp_output);
		ikcp_nodelay(stream->kcp, 1, LOOP_INTERVAL, 2, 1);
		ikcp_wndsize(stream->kcp, 256, 256);
		ikcp_setmtu(stream->kcp, KCP_MTU);
		stream->kcp->rx_minrto = KCP_MINIMUM_RTO;
		return stream;
	}
	return NULL;
}

static void stream_free(struct stream *stream)
{
	/* (the game may not have taken the connection made for it yet) */
	if (stream->local_port)
		remember_closed(1, stream->local_port, stream->peer, stream->remote_port);
	close_socket(&stream->socket);
	if (stream->kcp)
		ikcp_release(stream->kcp);
	p2p.finished[p2p.finished_next++ % 16] = stream->conversation;
	memset(stream, 0, offsetof(struct stream, pending));
	stream->pending_size = 0;
	stream->socket = -1;
}

static void stream_message(struct stream *stream, unsigned char type, const void *data, int size)
{
	unsigned char message[1 + STREAM_CHUNK_SIZE];

	message[0] = type;
	/* (a close has no data: NULL) */
	if (size > 0)
		memcpy(message + 1, data, (size_t)size);
	ikcp_send(stream->kcp, (const char *)message, size + 1);
}

static void stream_local_closed(struct stream *stream)
{
	close_socket(&stream->socket);
	/* (its port the system gives again now, not when the stream is let go
	once the peer has all it sent: remembered from now, and no longer the
	peer's while the stream lingers) */
	if (stream->local_port)
	{
		remember_closed(1, stream->local_port, stream->peer, stream->remote_port);
		stream->local_port = 0;
	}
	if (!stream->local_closed)
	{
		stream->local_closed = 1;
		stream->closed_time = p2p_now();
		if (!stream->remote_closed)
			stream_message(stream, _stream_close, NULL, 0);
	}
}

/* the game connected to a stand-in listener */
static void listener_readable(struct listener *listener)
{
	for (;;)
	{
		struct sockaddr_in from;
		int from_length = sizeof(from);
		int socket = posix_socket_accept(listener->socket, &from, &from_length);
		struct stream *stream;
		IUINT32 conversation;
		unsigned char open[4];

		if (socket < 0)
			return;
		/* only the game's own sockets use a stand-in (with network.address
		a LAN address, other machines could reach it) */
		if (from.sin_addr.s_addr != p2p.local_address && from.sin_addr.s_addr != network_long(0x7F000001))
		{
			posix_socket_close(socket);
			continue;
		}
		posix_socket_set_nonblocking(socket, 1);
		posix_socket_set_nodelay(socket);
		posix_random_bytes(&conversation, sizeof(conversation));
		stream = stream_new(listener->peer, conversation | 1);
		if (!stream)
		{
			posix_socket_close(socket);
			continue;
		}
		stream->socket = socket;
		stream->state = _stream_open_state;
		put_short(open, listener->remote_port);
		put_short(open + 2, p2p_game_port_wire(from.sin_port));
		stream_message(stream, _stream_open, open, sizeof(open));
	}
}

/* a peer's connection opened: connect to the game for it */
static void stream_opened(struct stream *stream, const unsigned char *data, int size)
{
	struct sockaddr_in to;
	unsigned short port;

	if (stream->state != _stream_awaiting_open || size < 4)
		return;
	stream->remote_port = get_short(data + 2);
	/* only to where the game listens (its port here: p2p_game_port_local):
	nothing else here is the peer's to reach */
	port = p2p_game_port_local(get_short(data));
	if (!game_local() && game_port_open(1, port))
	{
		stream->socket = open_socket(SOCK_STREAM, p2p.local_address, 0, &stream->local_port);
		if (stream->socket >= 0)
			forget_closed(1, stream->local_port);
	}
	make_address(&to, p2p.local_address, port);
	if (stream->socket < 0 || (posix_socket_connect(stream->socket, &to, sizeof(to)) < 0 && !would_block()))
	{
		stream->local_port = 0;
		stream_local_closed(stream);
		return;
	}
	stream->state = _stream_connecting;
}

static void stream_received(struct peer *peer, const unsigned char *data, int size)
{
	int peer_index = (int)(peer - p2p.peers);
	IUINT32 conversation;
	struct stream *stream = NULL;
	int count = 0, opening = 0;
	int index;

	if (size < 24)
		return;
	conversation = ikcp_getconv(data);
	for (index = 0; index < MAXIMUM_STREAMS && !stream; index++)
	{
		struct stream *entry = &p2p.streams[index];

		if (!entry->used || entry->peer != peer_index)
			continue;
		if (entry->conversation == conversation)
			stream = entry;
		count++;
		opening += entry->state == _stream_awaiting_open;
	}
	if (!stream)
	{
		for (index = 0; index < 16; index++)
		{
			if (p2p.finished[index] == conversation)
				return;
		}
		/* a peer has a few at a time: no peer takes them all */
		if (count >= MAXIMUM_PEER_STREAMS || opening >= MAXIMUM_PEER_OPENING_STREAMS)
			return;
		stream = stream_new(peer_index, conversation);
		if (!stream)
			return;
		stream->state = _stream_awaiting_open;
	}
	ikcp_input(stream->kcp, (const char *)data, size);
}

static void stream_flush_pending(struct stream *stream)
{
	while (stream->pending_size > 0 && stream->socket >= 0 && stream->state == _stream_open_state)
	{
		int sent = posix_socket_send(stream->socket, stream->pending, stream->pending_size, 0);

		if (sent < 0)
		{
			if (!would_block())
				stream_local_closed(stream);
			return;
		}
		memmove(stream->pending, stream->pending + sent, (size_t)(stream->pending_size - sent));
		stream->pending_size -= sent;
	}
}

static void stream_update(struct stream *stream)
{
	unsigned char message[1 + STREAM_CHUNK_SIZE];

	stream->kcp->rx_minrto = ikcp_waitsnd(stream->kcp) > KCP_BULK_SEGMENTS ? KCP_MINIMUM_RTO : KCP_FAST_MINIMUM_RTO;
	ikcp_update(stream->kcp, p2p_now());
	/* what the peer sent */
	for (;;)
	{
		int size = ikcp_peeksize(stream->kcp);

		if (size > (int)sizeof(message))
		{
			/* larger than any this sends: nothing behind it would get
			through */
			stream_local_closed(stream);
			stream->remote_closed = 1;
			break;
		}
		if (size <= 0)
			break;
		if (size - 1 > STREAM_BUFFER_SIZE - stream->pending_size)
			break;
		size = ikcp_recv(stream->kcp, (char *)message, size);
		if (size <= 0)
			break;
		switch (message[0])
		{
		case _stream_open:
			stream_opened(stream, message + 1, size - 1);
			break;
		case _stream_data:
			if (stream->socket >= 0)
			{
				memcpy(stream->pending + stream->pending_size, message + 1, (size_t)(size - 1));
				stream->pending_size += size - 1;
			}
			break;
		case _stream_close:
			stream->remote_closed = 1;
			if (!stream->closed_time)
				stream->closed_time = p2p_now();
			break;
		}
	}
	stream_flush_pending(stream);
	if (stream->remote_closed && !stream->pending_size && stream->socket >= 0)
		stream_local_closed(stream);
	/* finished: both ends closed and everything delivered, or the tunnel
	gave up on it */
	if ((stream->local_closed && stream->remote_closed && !ikcp_waitsnd(stream->kcp)) ||
		(stream->closed_time && elapsed(stream->closed_time, STREAM_LINGER_TIME)) ||
		(stream->state == _stream_awaiting_open && elapsed(stream->created_time, STREAM_LINGER_TIME)) ||
		stream->kcp->state == (IUINT32)-1)
	{
		stream_free(stream);
	}
}

static void stream_readable(struct stream *stream)
{
	unsigned char buffer[STREAM_CHUNK_SIZE];
	int count;

	for (count = 0; count < 8 && ikcp_waitsnd(stream->kcp) < STREAM_WINDOW && stream->socket >= 0; count++)
	{
		int size = posix_socket_recv(stream->socket, buffer, sizeof(buffer), 0);

		if (size == 0 || (size < 0 && !would_block()))
		{
			stream_local_closed(stream);
			return;
		}
		if (size < 0)
			return;
		stream_message(stream, _stream_data, buffer, size);
	}
}

static void stream_writeable(struct stream *stream)
{
	if (stream->state == _stream_connecting)
		stream->state = _stream_open_state;
	stream_flush_pending(stream);
}

/* ---------- the scoreboard's pings (p2p.h, network_distributed.c) */

/* A host's game tells each client machine every player's round trip as the
host measured it, every few seconds, in a tunnel packet of its own (the
game never sees it), so a client's scoreboard has the others' pings too:

  _packet_ping_table, a version (1), the host's tick it is of (4 bytes, big
  endian), the count of players named (at most P2P_PING_TABLE_PLAYERS), and
  for each its index (below P2P_PING_TABLE_PLAYERS, each once) and its ping
  in milliseconds (2 bytes, big endian, at most P2P_PING_MAXIMUM)

7 bytes and 3 a player: 55 for 16. A client takes one only from the peer
the game says is its host (p2p_set_ping_table_host), whole or not at all,
into a table of fixed size: nothing a peer sends allocates anything. A
table of another version is ignored, as the betas ignore the packet. */

enum
{
	PING_TABLE_VERSION = 1,
	PING_TABLE_HEADER_SIZE = 1 + 4 + 1,
	PING_TABLE_ENTRY_SIZE = 1 + 2,
};

static struct
{
	/* (a client) the host's virtual address, 0 for none; its latest table:
	whether there is one, when it came (p2p_now), the host's tick it is of,
	and each player's ping (P2P_PING_UNKNOWN for one it does not name) */
	unsigned long host;
	int valid;
	unsigned long received;
	long tick;
	unsigned short pings[P2P_PING_TABLE_PLAYERS];
	/* tables dropped as malformed, and whether one was logged */
	unsigned long dropped;
	int dropped_logged;
} ping_table;

/* a table's body (after its type) into pings: 1 if it is one, whole and
valid (version 1), 0 if it is not (pings may then be part written) */
static int ping_table_parse(const unsigned char *data, int size, long *tick, unsigned short *pings)
{
	unsigned char named[P2P_PING_TABLE_PLAYERS];
	unsigned long value;
	int count;
	int index;

	if (!data || size < PING_TABLE_HEADER_SIZE || data[0] != PING_TABLE_VERSION)
		return 0;
	value = (unsigned long)data[1] << 24 | (unsigned long)data[2] << 16 | (unsigned long)data[3] << 8 | data[4];
	count = data[5];
	if (value > 0x7FFFFFFFUL || count > P2P_PING_TABLE_PLAYERS ||
		size != PING_TABLE_HEADER_SIZE + count * PING_TABLE_ENTRY_SIZE)
	{
		return 0;
	}
	memset(named, 0, sizeof(named));
	for (index = 0; index < P2P_PING_TABLE_PLAYERS; index++)
		pings[index] = P2P_PING_UNKNOWN;
	for (index = 0; index < count; index++)
	{
		const unsigned char *entry = data + PING_TABLE_HEADER_SIZE + index * PING_TABLE_ENTRY_SIZE;
		int player = entry[0];
		unsigned short ping = (unsigned short)(entry[1] << 8 | entry[2]);

		if (player >= P2P_PING_TABLE_PLAYERS || named[player] || ping > P2P_PING_MAXIMUM)
			return 0;
		named[player] = 1;
		pings[player] = ping;
	}
	*tick = (long)value;
	return 1;
}

/* a table's body (after its type) of the pings named (those not
P2P_PING_UNKNOWN, at most P2P_PING_MAXIMUM), into data (room for
PING_TABLE_HEADER_SIZE + P2P_PING_TABLE_PLAYERS * PING_TABLE_ENTRY_SIZE):
its size */
static int ping_table_write(unsigned char *data, long tick, const unsigned short *pings, int count)
{
	int size = PING_TABLE_HEADER_SIZE;
	int named = 0;
	int index;

	data[0] = PING_TABLE_VERSION;
	data[1] = (unsigned char)((unsigned long)tick >> 24 & 0x7F);
	data[2] = (unsigned char)((unsigned long)tick >> 16);
	data[3] = (unsigned char)((unsigned long)tick >> 8);
	data[4] = (unsigned char)tick;
	for (index = 0; index < count && index < P2P_PING_TABLE_PLAYERS; index++)
	{
		unsigned short ping = pings[index];

		if (ping == P2P_PING_UNKNOWN)
			continue;
		if (ping > P2P_PING_MAXIMUM)
			ping = P2P_PING_MAXIMUM;
		data[size] = (unsigned char)index;
		data[size + 1] = (unsigned char)(ping >> 8);
		data[size + 2] = (unsigned char)ping;
		size += PING_TABLE_ENTRY_SIZE;
		named++;
	}
	data[5] = (unsigned char)named;
	return size;
}

/* a peer's table (its body, after the type): the host's alone is taken */
static void ping_table_received(struct peer *peer, const unsigned char *data, int size)
{
	unsigned short pings[P2P_PING_TABLE_PLAYERS];
	long tick;

	if (!ping_table.host || peer->virtual_address != ping_table.host)
		return;
	if (!ping_table_parse(data, size, &tick, pings))
	{
		/* (a newer version's is not malformed: left alone, unlogged) */
		if (size >= 1 && data[0] == PING_TABLE_VERSION)
		{
			ping_table.dropped++;
			if (!ping_table.dropped_logged)
			{
				ping_table.dropped_logged = 1;
				platform_log("Internet play: a ping table from %s %s was malformed (%d bytes): dropped",
					peer_role(peer->is_host), peer->name, size);
			}
		}
		return;
	}
	memcpy(ping_table.pings, pings, sizeof(pings));
	ping_table.tick = tick;
	ping_table.received = p2p_now();
	ping_table.valid = 1;
}

int p2p_send_ping_table(unsigned long virtual_address, long tick, const unsigned short *pings, int count)
{
	unsigned char inner[1 + PING_TABLE_HEADER_SIZE + P2P_PING_TABLE_PLAYERS * PING_TABLE_ENTRY_SIZE];
	struct peer *peer;
	int sent = 0;

	if (!p2p.running || !pings || count < 0 || tick < 0 || !is_virtual_address(virtual_address))
		return 0;
	inner[0] = _packet_ping_table;
	pthread_mutex_lock(&p2p_lock);
	peer = find_peer_by_address(virtual_address);
	if (peer && peer->connected)
	{
		peer_send(peer, inner, 1 + ping_table_write(inner + 1, tick, pings, count));
		sent = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
	return sent;
}

void p2p_set_ping_table_host(unsigned long virtual_address)
{
	if (!p2p.running)
		return;
	if (!is_virtual_address(virtual_address))
		virtual_address = 0;
	pthread_mutex_lock(&p2p_lock);
	if (ping_table.host != virtual_address)
	{
		ping_table.host = virtual_address;
		ping_table.valid = 0;
	}
	pthread_mutex_unlock(&p2p_lock);
}

long p2p_ping_table(unsigned short *pings, int count, long *tick)
{
	long age = -1;

	if (!p2p.running || !pings || count < 0)
		return -1;
	pthread_mutex_lock(&p2p_lock);
	if (ping_table.host && ping_table.valid)
	{
		int index;

		for (index = 0; index < count; index++)
			pings[index] = index < P2P_PING_TABLE_PLAYERS ? ping_table.pings[index] : P2P_PING_UNKNOWN;
		if (tick)
			*tick = ping_table.tick;
		age = (long)(unsigned long)((unsigned int)(p2p_now() - ping_table.received) & 0x7FFFFFFF);
	}
	pthread_mutex_unlock(&p2p_lock);
	return age;
}

/* ---------- the tunnel */

#ifdef HALO_DEDICATED_SERVER
/* (a dedicated server) a peer's sealed packet, from where it came: 0 if it
is dropped, as one of more than DEDICATED_PEER_PACKETS_PER_SECOND, or with
the peer, the first from an address that has DEDICATED_PEERS_PER_ADDRESS
peers connected already (not through a relay, whose peers share it) */
static int dedicated_peer_admitted(struct peer *peer, const struct sockaddr_in *from)
{
	unsigned long now = p2p_now();
	char text[32];

	if (!peer->connected && !peer_relay_at(peer, from->sin_addr.s_addr, from->sin_port))
	{
		int index, count = 0;

		for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
		{
			struct peer *other = &p2p.peers[index];

			if (other != peer && other->used && other->connected && !other->via_relay &&
				other->endpoint.address == from->sin_addr.s_addr)
			{
				count++;
			}
		}
		if (count >= DEDICATED_PEERS_PER_ADDRESS)
		{
			platform_log("Internet play: player %s refused: %d machines are connected from %s already", peer->name,
				count, address_text(from->sin_addr.s_addr, 0, text));
			drop_peer(peer, "too many machines from its address");
			return 0;
		}
	}
	if (elapsed(peer->rate_time, 1000))
	{
		peer->rate_time = now;
		peer->rate_count = 0;
	}
	if (++peer->rate_count > DEDICATED_PEER_PACKETS_PER_SECOND)
	{
		if (!peer->rate_logged_time || elapsed(peer->rate_logged_time, 10000))
		{
			peer->rate_logged_time = now ? now : 1;
			platform_log("Internet play: player %s sends more than %d packets a second; the rest are dropped",
				peer->name, DEDICATED_PEER_PACKETS_PER_SECOND);
		}
		return 0;
	}
	return 1;
}
#endif

static void tunnel_received(const unsigned char *packet, int size, const struct sockaddr_in *from)
{
	unsigned char inner[MAXIMUM_INNER_SIZE];
	unsigned char nonce[P2P_NONCE_SIZE];
	unsigned long long counter;
	struct peer *peer;
	int inner_size;
	int newest;

	/* (the magic first: a tunnel packet's bytes 4 to 7, of the sender and
	its number, can be STUN's magic cookie) */
	if (size >= 1 && packet[0] == P2P_RELAY_MAGIC)
	{
		relay_received(packet, size, from);
		return;
	}
	if (size < 1 || packet[0] != TUNNEL_MAGIC)
	{
		if (size >= 20 && packet[4] == 0x21 && packet[5] == 0x12 && packet[6] == 0xA4 && packet[7] == 0x42)
			stun_received(packet, size, from);
		return;
	}
	if (size < TUNNEL_HEADER_SIZE + P2P_TAG_SIZE + 1 || size - TUNNEL_HEADER_SIZE - P2P_TAG_SIZE > (int)sizeof(inner))
		return;
	peer = find_peer(packet + 1);
	if (!peer)
		return;
	/* each packet once, sealed by the peer for this direction, its header
	and all */
	counter = packet_counter(packet);
	if (!packet_fresh(peer, counter))
		return;
	/* (anyone can send packets in a peer's name from anywhere, each costing
	the work of its seal: from where the peer is not known to be, a few a
	second, which a peer whose address changed still gets through with) */
	if (peer->connected && !peer_known_address(peer, from->sin_addr.s_addr, from->sin_port))
	{
		unsigned long now = p2p_now();

		if (elapsed(peer->stray_time, 1000))
		{
			peer->stray_time = now;
			peer->stray_budget = STRAY_PACKETS_PER_SECOND;
		}
		if (peer->stray_budget <= 0)
			return;
		peer->stray_budget--;
	}
	packet_nonce(packet, nonce);
	inner_size = p2p_aead_open(peer->receive_key, nonce, packet, TUNNEL_HEADER_SIZE, packet + TUNNEL_HEADER_SIZE,
		size - TUNNEL_HEADER_SIZE, inner);
	if (inner_size < 1)
		return;
#ifdef HALO_DEDICATED_SERVER
	if (!dedicated_peer_admitted(peer, from))
		return;
#endif
	newest = counter > peer->receive_highest;
	packet_received(peer, counter);
	peer_heard(peer, from->sin_addr.s_addr, from->sin_port, newest, inner[0] == _packet_pong && inner_size >= 5);
	switch (inner[0])
	{
	case _packet_ping:
		if (inner_size >= 5)
		{
			struct p2p_candidate to;

			inner[0] = _packet_pong;
			to.address = from->sin_addr.s_addr;
			to.port = from->sin_port;
			peer_send_to(peer, &to, inner, 5);
		}
		break;
	case _packet_pong:
		if (inner_size >= 5)
		{
			/* (4 bytes of the clock, as the ping carries) */
			unsigned int sent;

			memcpy(&sent, inner + 1, 4);
			peer->round_trip = (unsigned int)p2p_now() - sent;
		}
		break;
	case _packet_datagram:
		datagram_received(peer, inner, inner_size);
		break;
	case _packet_stream:
		stream_received(peer, inner + 1, inner_size - 1);
		break;
	case _packet_bye:
		drop_peer(peer, "left");
		break;
	case _packet_ping_table:
		ping_table_received(peer, inner + 1, inner_size - 1);
		break;
	}
}

static void tunnel_readable(void)
{
	unsigned char packet[2048];
	int count;

	for (count = 0; count < 256; count++)
	{
		struct sockaddr_in from;
		int from_length = sizeof(from);
		int size = posix_socket_recvfrom(p2p.tunnel_socket, packet, sizeof(packet), 0, &from, &from_length);

		if (size < 0)
			break;
		tunnel_received(packet, size, &from);
	}
}

/* ---------- invites */

/* the host's key hash and the token in an invite link or code within text:
1 if it holds one, -1 if it holds an older version's (with the host's
identifier alone, which a key made to have it could pass for), else 0 */
static int parse_invite(const char *text, unsigned char *host_hash, unsigned char *token)
{
	unsigned char bytes[P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE];
	const char *start = NULL;
	const char *search;
	int digits;
	int index;

	for (search = text; *search && !start; search++)
	{
		static const char prefix[] = "halo://join/";
		int length;

		for (length = 0; prefix[length] && search[length] &&
			(search[length] | 0x20) == prefix[length]; length++)
			;
		if (!prefix[length])
			start = search + length;
	}
	if (!start)
	{
		/* a bare code: the text is its digits alone, spaces around them
		allowed */
		start = text;
		while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
			start++;
		for (search = start; hex_value(*search) >= 0; search++)
			;
		for (; *search; search++)
		{
			if (*search != ' ' && *search != '\t' && *search != '\r' && *search != '\n')
				return 0;
		}
	}
	for (digits = 0; hex_value(start[digits]) >= 0; digits++)
		;
	if (digits == 2 * (P2P_IDENTIFIER_SIZE + P2P_TOKEN_SIZE))
		return -1;
	if (digits != (int)sizeof(bytes) * 2)
		return 0;
	for (index = 0; index < (int)sizeof(bytes); index++)
		bytes[index] = (unsigned char)(hex_value(start[index * 2]) << 4 | hex_value(start[index * 2 + 1]));
	memcpy(host_hash, bytes, P2P_KEY_HASH_SIZE);
	memcpy(token, bytes + P2P_KEY_HASH_SIZE, P2P_TOKEN_SIZE);
	return 1;
}

/* a short code within text: eight characters of P2P_CODE_ALPHABET, any
case, a dash or a space after the fourth (optional unless dash_required:
on the clipboard or a command line an eight-letter word must not look up
a game), and nothing but spaces around them. The eight characters, upper
case, go to code */
static int parse_code(const char *text, char *code, int dash_required)
{
	int count = 0;

	while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
		text++;
	while (count < P2P_CODE_LENGTH)
	{
		char character;

		if (count == 4)
		{
			if (*text == '-' || *text == ' ')
				text++;
			else if (dash_required)
				return 0;
		}
		character = *text;
		if (character >= 'a' && character <= 'z')
			character = (char)(character - 'a' + 'A');
		if (!character || !strchr(P2P_CODE_ALPHABET, character))
			return 0;
		code[count++] = character;
		text++;
	}
	code[count] = 0;
	while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
		text++;
	return *text == 0;
}

/* under p2p_lock: the host `host` was joined from the public lobby
(public) or by an invite or a code */
static void join_origin_set(const unsigned char *host, int public)
{
	int index;

	for (index = 0; index < P2P_PUBLIC_HOSTS; index++)
		if (!memcmp(p2p.public_hosts[index], host, P2P_IDENTIFIER_SIZE))
			memset(p2p.public_hosts[index], 0, P2P_IDENTIFIER_SIZE);
	if (public)
	{
		memcpy(p2p.public_hosts[p2p.public_host_next], host, P2P_IDENTIFIER_SIZE);
		p2p.public_host_next = (p2p.public_host_next + 1) % P2P_PUBLIC_HOSTS;
	}
}

/* under p2p_lock; public: a public lobby's game; host: the identifier of
the host whose record alone is taken (a public lobby entry's), or NULL */
static int join_code(const char *code, int public, const unsigned char *host)
{
#ifdef HALO_DEDICATED_SERVER
	/* (a dedicated server joins nothing: it only hosts) */
	(void)public;
	(void)host;
	set_status(N_("a dedicated server joins no games (code %.4s-%.4s ignored)"), code, code + 4);
	return 1;
#endif
	if (p2p.code[0] && !memcmp(p2p.code, code, 4) && !memcmp(p2p.code + 5, code + 4, 4))
	{
		set_status(N_("that is this machine's own code"));
		return 1;
	}
	memcpy(p2p.lookup_code, code, sizeof(p2p.lookup_code));
	p2p.lookup_public = public;
	p2p.lookup_has_host = host != NULL;
	if (host)
		memcpy(p2p.lookup_host, host, P2P_IDENTIFIER_SIZE);
	p2p.lookup_requested = 1;
	return 1;
}

/* under p2p_lock: parse_invite's result (a code, with its dash, is looked
up); public: the invite a public lobby's code led to */
static int join_invite(const char *text, int public)
{
	unsigned char hash[P2P_KEY_HASH_SIZE], host[P2P_IDENTIFIER_SIZE], token[P2P_TOKEN_SIZE];
	char code[P2P_CODE_LENGTH + 1];
	struct peer *peer;
	int parsed = parse_invite(text, hash, token);

#ifdef HALO_DEDICATED_SERVER
	/* (a dedicated server joins nothing: it only hosts) */
	if (parsed > 0)
	{
		set_status(N_("a dedicated server joins no games (the invite is ignored)"));
		return 1;
	}
#endif
	if (!parsed && parse_code(text, code, 1))
		return join_code(code, 0, NULL);
	if (parsed < 0)
		set_status(N_("that invite is from an older version of the game, which this one cannot join"));
	if (parsed <= 0)
		return parsed;
	p2p_identifier_from_hash(hash, host);
	if (!memcmp(host, identifier, P2P_IDENTIFIER_SIZE))
		return 1;
	join_origin_set(host, public);
	peer = find_peer(host);
	if (peer && peer->connected)
	{
		platform_log("Internet play: already connected to that invite's host");
		return 1;
	}
	if ((p2p.joining || p2p.join_requested) && !memcmp(hash, p2p.join_host_hash, sizeof(hash)) &&
		!memcmp(token, p2p.join_token, sizeof(token)))
		return 1;
	memcpy(p2p.join_host, host, sizeof(host));
	memcpy(p2p.join_host_hash, hash, sizeof(hash));
	memcpy(p2p.join_token, token, sizeof(token));
	p2p.join_requested = 1;
	return 1;
}

int p2p_running(void)
{
	return p2p.running;
}

int p2p_join_invite_locked(const char *text)
{
	/* (the server browser's: its host is a stranger's) */
	int result = join_invite(text, 1);

	if (result > 0 && !p2p.running)
		platform_log("Internet play is off: the invite is ignored");
	return result > 0;
}

void p2p_set_status(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	set_status_arguments(format, arguments);
	va_end(arguments);
}

int p2p_join_invite(const char *text)
{
	int result;

	p2p_identifier();
	pthread_mutex_lock(&p2p_lock);
	result = join_invite(text, 0);
	pthread_mutex_unlock(&p2p_lock);
	if (result > 0 && !p2p.running)
		platform_log("Internet play is off (network.online in config.toml): the invite is ignored");
	return result > 0;
}

void p2p_invite_received(const char *text)
{
	/* (an older version's is logged as such) */
	if (!join_invite(text, 0))
		set_status(N_("that is not an invite"));
}

void p2p_code_found(const char *text)
{
	if (!p2p.looking_up)
		return;
	p2p.looking_up = 0;
	p2p_signal_stop_lookup();
	set_status(N_("code %.4s-%.4s found; reaching its host"), p2p.lookup_code, p2p.lookup_code + 4);
	join_invite(text, p2p.lookup_public);
}

static int join_code_from(const char *text, int public)
{
	char code[P2P_CODE_LENGTH + 1];
	int result;

	if (!parse_code(text, code, 0))
		return 0;
	if (!p2p.running)
	{
		platform_log("Internet play is off (Online in the settings): the code is ignored");
		return 1;
	}
	pthread_mutex_lock(&p2p_lock);
	result = join_code(code, public, NULL);
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

int p2p_join_code(const char *text)
{
	return join_code_from(text, 0);
}

int p2p_join_lobby_code(const char *text)
{
	return join_code_from(text, 1);
}

int p2p_address_origin(unsigned long address)
{
	struct peer *peer;
	int origin = P2P_ORIGIN_NONE, index;

	if (!p2p.running)
		return P2P_ORIGIN_NONE;
	pthread_mutex_lock(&p2p_lock);
	peer = find_peer_by_address(address);
	if (peer)
	{
		origin = p2p.adhoc ? P2P_ORIGIN_ADHOC : P2P_ORIGIN_PRIVATE;
		for (index = 0; !p2p.adhoc && index < P2P_PUBLIC_HOSTS; index++)
			if (!memcmp(p2p.public_hosts[index], peer->identifier, P2P_IDENTIFIER_SIZE))
				origin = P2P_ORIGIN_PUBLIC;
	}
	pthread_mutex_unlock(&p2p_lock);
	return origin;
}

static void update_joining(void)
{
	char name[2 * P2P_IDENTIFIER_SIZE + 1];

	if (p2p.lookup_requested)
	{
		p2p.lookup_requested = 0;
		p2p.looking_up = 1;
		p2p.lookup_time = p2p_now();
		set_status(N_("looking up code %.4s-%.4s"), p2p.lookup_code, p2p.lookup_code + 4);
		p2p_signal_start();
		p2p_signal_kick();
		p2p_signal_lookup_code(p2p.lookup_code, p2p.lookup_has_host ? p2p.lookup_host : NULL);
	}
	else if (p2p.looking_up && elapsed(p2p.lookup_time, CODE_LOOKUP_TIMEOUT))
	{
		p2p.looking_up = 0;
		p2p_signal_stop_lookup();
		set_status(p2p_signal_connected() ? N_("no game has code %.4s-%.4s (check it, or the host stopped hosting)") :
			N_("cannot reach the signalling brokers to look up code %.4s-%.4s"), p2p.lookup_code, p2p.lookup_code + 4);
	}
	if (p2p.join_requested)
	{
		/* the offer carries the public address, if there is one */
		p2p.stun_started = 1;
		/* (and the relays, which it carries too) */
		relay_resolve();
		if (!stun_settled())
			return;
		p2p.join_requested = 0;
		p2p.joining = 1;
		p2p.join_time = p2p_now();
		p2p_hex(p2p.join_host, P2P_IDENTIFIER_SIZE, name);
		set_status(N_("joining %s's game"), name);
		p2p_signal_start();
		p2p_signal_join(p2p.join_host_hash, p2p.join_token);
	}
	else if (p2p.joining && elapsed(p2p.join_time, JOIN_TIMEOUT))
	{
		p2p.joining = 0;
		p2p_signal_stop_joining();
		set_status(N_("no answer from the invite's host; it may have stopped hosting or quit"));
		p2p_lobby_join_timed_out(p2p.join_host_hash);
	}
}

static int connected_player_count(void)
{
	int count = 0;
	int index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
		count += p2p.peers[index].used && p2p.peers[index].connected && !p2p.peers[index].is_host;
	return count;
}

/* a new invite (a new token) */
static void make_invite(void)
{
	unsigned char bytes[P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE];
	char text[2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE) + 1];

	posix_random_bytes(p2p.token, sizeof(p2p.token));
	p2p.has_token = 1;
	p2p.token_listed = 0;
	p2p_key_hash(p2p_public_key(), bytes);
	memcpy(bytes + P2P_KEY_HASH_SIZE, p2p.token, P2P_TOKEN_SIZE);
	p2p_hex(bytes, sizeof(bytes), text);
	snprintf(p2p.invite, sizeof(p2p.invite), "halo://join/%s", text);
}

void p2p_new_invite_if_listed(void)
{
	if (!p2p.has_token || !p2p.token_listed)
		return;
	make_invite();
	/* (the invite is a bearer token: the log shows only the host's part) */
	platform_log("Internet play: a new invite, so that the one listed lets no one in (private, or a new password): "
		"halo://join/%.12s...", p2p.invite + strlen("halo://join/"));
	if (p2p.hosting)
	{
		/* (the code stays: it was never listed, and now leads to the new
		invite) */
		p2p_signal_host(p2p.token, p2p.code);
		memcpy(p2p.clipboard, p2p.invite, sizeof(p2p.clipboard));
		p2p.has_clipboard = 1;
		/* (Discord is told the new one) */
		p2p.reported_player_count = -1;
	}
}

/* the most players a game this machine hosts takes until its server says
(network_server_manager.c's network_game_server_port_maximum_players): a
Vita, and a build that plays as one, offers the Xbox's 16 at most */
#if defined(HALO_VITA) || defined(HALO_NET_AS_VITA)
#define UNSAID_MAXIMUM_PLAYERS 16
#else
#define UNSAID_MAXIMUM_PLAYERS (P2P_MAXIMUM_PEERS + 1)
#endif

/* the hosted game's players, as the game says; else the host and the
machines the tunnel reaches, of the build's most */
static void hosted_player_counts(int *count, int *maximum)
{
	*count = p2p.game_player_maximum > 0 ? p2p.game_player_count : connected_player_count() + 1;
	*maximum = p2p.game_player_maximum > 0 ? p2p.game_player_maximum : UNSAID_MAXIMUM_PLAYERS;
	if (*count > *maximum)
		*count = *maximum;
}

/* a Split Screen game began: the players who reached this machine for a
game it hosted before are let go (the tunnel would carry nothing of theirs
to the game: game_local), as the host they reached hosts no more. Not in ad
hoc play, whose peers are the group's machines */
static void drop_joiners(void)
{
	int index;

	if (p2p.adhoc)
		return;
	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		struct peer *peer = &p2p.peers[index];

		if (peer->used && !peer->is_host)
			drop_peer(peer, "this machine's game is a Split Screen one, which no one joins");
	}
}

static void update_hosting(void)
{
	/* (a Split Screen game's server listens too, and is not hosted) */
	int want = p2p.hosting_socket >= 0 && p2p.game_accepts_remote;
	int local = game_local();
	static int was_local;

	if (local && !was_local)
	{
		platform_log("Internet play: a Split Screen game: not hosted (no invite, code or server browser listing, "
			"and no one reaches it)");
		drop_joiners();
	}
	was_local = local;

	if (p2p.adhoc)
	{
		/* (the group's machines are already peers: the game's broadcasts
		reach them, and its game shows in their lists) */
		if (want != p2p.hosting)
		{
			p2p.hosting = want;
			set_status(want ? N_("hosting over ad hoc: the group's machines see the game under System Link") :
				N_("stopped hosting"));
		}
		return;
	}
	if (want && !p2p.hosting)
	{
		/* one invite for the whole run, so a link keeps working from game
		to game */
		if (!p2p.has_token)
		{
			make_invite();
			/* the short code: 40 random bits, five to a character */
			{
				unsigned char random[5];
				int index;

				posix_random_bytes(random, sizeof(random));
				for (index = 0; index < P2P_CODE_LENGTH; index++)
				{
					int bit = index * 5;
					int value = ((random[bit / 8] << 8 | (bit / 8 + 1 < 5 ? random[bit / 8 + 1] : 0)) >>
						(11 - bit % 8)) & 31;

					p2p.code[index < 4 ? index : index + 1] = P2P_CODE_ALPHABET[value];
				}
				p2p.code[4] = '-';
				p2p.code[9] = 0;
			}
		}
		p2p.hosting = 1;
		p2p.stun_started = 1;
		p2p_signal_start();
		/* (a broker waiting after a failure tries now) */
		p2p_signal_kick();
		p2p_signal_host(p2p.token, p2p.code);
		/* (the invite is a bearer token: anyone who reads it can join, so the
		log shows only the host's part; the link itself goes to the
		clipboard, or on the Vita to host_invite.txt) */
		platform_log("Internet play: hosting with the invite halo://join/%.12s... (the whole link only works while "
			"this copy of the game runs)", p2p.invite + strlen("halo://join/"));
		set_status(N_("hosting; others join with the code %s"), p2p.code);
		if (!p2p.invite_copied)
		{
			memcpy(p2p.clipboard, p2p.invite, sizeof(p2p.clipboard));
			p2p.has_clipboard = 1;
			p2p.invite_copied = 1;
		}
		p2p.reported_player_count = -1;
	}
	else if (!want && p2p.hosting)
	{
		p2p.hosting = 0;
		p2p_signal_stop_hosting();
		p2p_discord_set_hosting(NULL, 0, 0);
		/* (the code is no longer shown as this machine's) */
		set_status(N_("stopped hosting"));
	}
	if (p2p.hosting)
	{
		int count, maximum;

		hosted_player_counts(&count, &maximum);
		if (count != p2p.reported_player_count || maximum != p2p.reported_player_maximum)
		{
			p2p.reported_player_count = count;
			p2p.reported_player_maximum = maximum;
			p2p_discord_set_hosting(p2p.invite + strlen("halo://join/"), count, maximum);
		}
	}
}

void p2p_set_game_accepts_remote(int accepts)
{
	accepts = accepts != 0;
	if (accepts == p2p.game_accepts_remote)
		return;
	pthread_mutex_lock(&p2p_lock);
	p2p.game_accepts_remote = accepts;
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_set_game_player_counts(int count, int maximum)
{
	/* (only the game's server writes these: unchanged, it need not wait
	for the lock) */
	count = count < 0 ? 0 : count;
	maximum = maximum < 0 ? 0 : maximum;
	if (count == p2p.game_player_count && maximum == p2p.game_player_maximum)
		return;
	pthread_mutex_lock(&p2p_lock);
	p2p.game_player_count = count;
	p2p.game_player_maximum = maximum;
	pthread_mutex_unlock(&p2p_lock);
}

/* ---------- UPnP (posix_upnp.c): the router forwards a port here */

/* the router asked, on a thread of its own (it takes seconds) */
static void *upnp_thread(void *unused)
{
	unsigned short port, previous_port = 0;
	posix_ulong address = 0;
	unsigned short external_port = 0;
	char error[160] = "";
	int forwarded;

	(void)unused;
	pthread_mutex_lock(&p2p_lock);
	port = p2p.tunnel_port;
	if (p2p.upnp_forwarded)
		previous_port = p2p.upnp_candidate.port;
	pthread_mutex_unlock(&p2p_lock);
	/* a renewal asks for the port the router gave before; if it gives
	another, the old forwarding goes */
	forwarded = posix_upnp_forward_udp(port, previous_port ? previous_port : port, &address, &external_port,
		error, sizeof(error));
	if (forwarded && previous_port && previous_port != external_port)
		posix_upnp_stop_forwarding_udp(previous_port);
	pthread_mutex_lock(&p2p_lock);
	p2p.upnp_working = 0;
	p2p.upnp_time = p2p_now();
	if (forwarded)
	{
		char text[32];

		if (!p2p.upnp_forwarded || p2p.upnp_candidate.address != address ||
			p2p.upnp_candidate.port != external_port)
		{
			platform_log("Internet play: the router forwards %s to this machine (UPnP)",
				address_text(address, external_port, text));
		}
		p2p.upnp_forwarded = 1;
		p2p.upnp_candidate.address = address;
		p2p.upnp_candidate.port = external_port;
	}
	else if (!p2p.upnp_forwarded)
	{
		platform_log("Internet play: UPnP: %s", error);
	}
	pthread_mutex_unlock(&p2p_lock);
	return NULL;
}

/* quitting: a game listed in the server browser is taken out of it at once
(not when its listing lapses), as when it stops being hosted */
static void lobby_quit(void)
{
	pthread_mutex_lock(&p2p_lock);
	p2p_lobby_quit();
	pthread_mutex_unlock(&p2p_lock);
}

/* the game exits: the router forwards the port no longer (after a request
under way, which may forward one, if it ends soon) */
static void upnp_release(void)
{
	unsigned short external_port = 0;
	unsigned long start = p2p_now();

	pthread_mutex_lock(&p2p_lock);
	p2p.upnp_released = 1;
	while (p2p.upnp_working && !elapsed(start, UPNP_RELEASE_WAIT))
	{
		pthread_mutex_unlock(&p2p_lock);
		Sleep(50);
		pthread_mutex_lock(&p2p_lock);
	}
	/* (still under way: posix_upnp.c takes one caller at a time) */
	if (p2p.upnp_forwarded && !p2p.upnp_working)
	{
		external_port = p2p.upnp_candidate.port;
		p2p.upnp_forwarded = 0;
	}
	pthread_mutex_unlock(&p2p_lock);
	if (external_port)
		posix_upnp_stop_forwarding_udp(external_port);
}

/* whether a forwarded port would help: a player reaching this host (at
once: they may need it), or a peer not reached in a while (every copy of
the game listens, and so has an invite, from its start: a host asks only
when its invite is used) */
static int upnp_needed(void)
{
	int index;

	if (p2p.joining && elapsed(p2p.join_time, UPNP_JOIN_DELAY))
		return 1;
	/* a game in the server browser: at once (more joiners get through) */
	if (p2p_lobby_listed())
		return 1;
	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		struct peer const *peer = &p2p.peers[index];

		if (peer->used && !peer->connected &&
			((p2p.hosting && !peer->is_host) || elapsed(peer->offered_time, UPNP_JOIN_DELAY)))
		{
			return 1;
		}
	}
	return 0;
}

static void update_upnp(void)
{
	static int allowed = -1;
	int ask;
	pthread_t thread;

	if (allowed < 0)
		allowed = config_boolean("network.allow_upnp") && !p2p.adhoc ? 1 : 0;
	if (!allowed || p2p.upnp_working || p2p.upnp_released)
		return;
	if (p2p.upnp_forwarded)
		ask = elapsed(p2p.upnp_time, UPNP_RENEW_INTERVAL);
	else
		ask = upnp_needed() && (!p2p.upnp_asked || elapsed(p2p.upnp_time, UPNP_RETRY_INTERVAL));
	if (!ask)
		return;
	p2p.upnp_working = 1;
	p2p.upnp_asked = 1;
	if (!p2p.upnp_release_registered)
	{
		p2p.upnp_release_registered = 1;
		atexit(upnp_release);
	}
	if (pthread_create(&thread, NULL, upnp_thread, NULL) != 0)
	{
		p2p.upnp_working = 0;
		p2p.upnp_time = p2p_now();
		return;
	}
	pthread_detach(thread);
}

const char *p2p_take_clipboard_text(void)
{
	static char text[P2P_LINK_SIZE];
	const char *result = NULL;

	if (!p2p.running)
		return NULL;
	pthread_mutex_lock(&p2p_lock);
	if (p2p.has_clipboard)
	{
		memcpy(text, p2p.clipboard, sizeof(text));
		p2p.has_clipboard = 0;
		result = text;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

/* ---------- codes, the public lobby and the status, for menus */

int p2p_hosting_code(char *code, int size)
{
	int result = 0;

	if (!p2p.running || size < P2P_CODE_SIZE)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	if (p2p.hosting && p2p.code[0])
	{
		memcpy(code, p2p.code, P2P_CODE_SIZE);
		result = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

int p2p_hosting_invite(char *invite, int size)
{
	int result = 0;

	if (!p2p.running || size < P2P_LINK_SIZE)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	if (p2p.hosting && p2p.has_token)
	{
		memcpy(invite, p2p.invite, P2P_LINK_SIZE);
		result = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

/* the status line, English (`shown` 0) or in the language chosen */
static int status_line(char *text, int size, int shown)
{
	if (size <= 0)
		return 0;
	if (!p2p.running)
	{
		snprintf(text, (size_t)size, "%s", shown ? T("off") : "off");
		return 0;
	}
	pthread_mutex_lock(&p2p_lock);
	if (p2p.status[0])
		snprintf(text, (size_t)size, "%s", shown ? p2p.status_shown : p2p.status);
	else if (p2p_signal_connected())
		snprintf(text, (size_t)size, "%s", shown ? T("ready") : "ready");
	else
		snprintf(text, (size_t)size, "%s", shown ? T("starting") : "starting");
	/* (hosting a public game: whether the brokers hold its listing, in the
	language chosen either way) */
	if (p2p.hosting && !p2p.adhoc)
	{
		char listing[96];
		size_t length = strlen(text);

		if (p2p_lobby_hosting_status_locked(listing, sizeof(listing)) != P2P_LOBBY_HOSTING_NONE && listing[0] &&
			length + 3 < (size_t)size)
		{
			snprintf(text + length, (size_t)size - length, "; %s", listing);
		}
	}
	pthread_mutex_unlock(&p2p_lock);
	return 1;
}

int p2p_status(char *text, int size)
{
	return status_line(text, size, 0);
}

int p2p_status_shown(char *text, int size)
{
	return status_line(text, size, 1);
}

/* ---------- invites from elsewhere */

/* the first command line argument holding an invite or a code ABCD-EFGH
(or an older version's invite, which the copy that takes it says it cannot
join) */
static int command_line_invite(char *text, int size)
{
	int index;

	for (index = 1; posix_command_line_argument(index, text, (posix_ulong)size); index++)
	{
		unsigned char hash[P2P_KEY_HASH_SIZE], token[P2P_TOKEN_SIZE];
		char code[P2P_CODE_LENGTH + 1];

		if (parse_invite(text, hash, token) || parse_code(text, code, 1))
			return 1;
	}
	return 0;
}

/* the key of this user's copies of the game, from a secret only they can
read: another user's program may have the port, and must neither read the
invites nor pass its own */
static int handoff_key(unsigned char *key)
{
	unsigned char secret[P2P_SHA256_SIZE];

	if (!posix_user_secret(secret, sizeof(secret)))
		return 0;
	p2p_hmac_sha256(secret, sizeof(secret), "halo handoff", 12, key);
	return 1;
}

/* the answer to a handed over invite: that the copy that took it has the
key */
static void handoff_answer(const unsigned char *key, const unsigned char *message, unsigned char *answer)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_hmac_sha256(key, P2P_SHA256_SIZE, message, 12 + P2P_NONCE_SIZE, digest);
	memcpy(answer, digest, 16);
}

int p2p_hand_off_invite(void)
{
#ifdef HALO_ANDROID
	return 0;
#else
	char invite[256];
	unsigned char key[P2P_SHA256_SIZE];
	unsigned char message[12 + sizeof(invite) + P2P_SEAL_OVERHEAD];
	unsigned char answer[16];
	struct sockaddr_in to;
	int size;
	int socket;
	int attempt;
	int result = 0;

	if (!command_line_invite(invite, sizeof(invite)) || !handoff_key(key))
		return 0;
	socket = open_socket(SOCK_DGRAM, network_long(0x7F000001), 0, NULL);
	if (socket < 0)
		return 0;
	memcpy(message, "halo-invite ", 12);
	size = 12 + p2p_seal(key, invite, (int)strlen(invite), message + 12);
	handoff_answer(key, message, answer);
	make_address(&to, network_long(0x7F000001), network_short(HANDOFF_PORT));
	for (attempt = 0; attempt < 3 && !result; attempt++)
	{
		int read[1] = { socket };
		int read_count = 1, write_count = 0, error_count = 0;
		unsigned char reply[32];

		posix_socket_sendto(socket, message, size, 0, &to, sizeof(to));
		if (posix_socket_select(read, &read_count, NULL, &write_count, NULL, &error_count, 0, 150000, 0) > 0 &&
			posix_socket_recv(socket, reply, sizeof(reply), 0) == (int)sizeof(answer) &&
			p2p_equal(reply, answer, sizeof(answer)))
		{
			result = 1;
		}
	}
	posix_socket_close(socket);
	if (result)
		platform_log("Internet play: passed the invite to the copy of the game already running");
	return result;
#endif
}

static void handoff_readable(void)
{
	unsigned char message[12 + 256 + P2P_SEAL_OVERHEAD];
	char invite[257];
	unsigned char answer[16];
	struct sockaddr_in from;
	int from_length = sizeof(from);
	int size = posix_socket_recvfrom(p2p.handoff_socket, message, sizeof(message), 0, &from, &from_length);

	if (size < 12 + P2P_SEAL_OVERHEAD || from.sin_addr.s_addr != network_long(0x7F000001) ||
		memcmp(message, "halo-invite ", 12) || !p2p.has_handoff_key)
		return;
	size = p2p_open(p2p.handoff_key, message + 12, size - 12, (unsigned char *)invite);
	if (size < 0)
		return;
	invite[size] = 0;
	handoff_answer(p2p.handoff_key, message, answer);
	posix_socket_sendto(p2p.handoff_socket, answer, sizeof(answer), 0, &from, sizeof(from));
	p2p_invite_received(invite);
}

#if defined(HALO_ANDROID) || defined(HALO_VITA)
/* Android's activity, or VitaShell/FTP on the Vita, supplies an invite. */
static void poll_invite_file(void)
{
	static unsigned long checked_time;
	char path[512];
	char taken[512];
	char text[256];
	FILE *file;
	size_t size;

	if (!elapsed(checked_time, 1000))
		return;
	checked_time = p2p_now();
	#ifdef HALO_VITA
	snprintf(path, sizeof(path), "ux0:data/haloce-vita/join_link.txt");
	snprintf(taken, sizeof(taken), "ux0:data/haloce-vita/join_link.taken");
#else
	snprintf(path, sizeof(path), "%s/join_link.txt", platform_data_root());
	snprintf(taken, sizeof(taken), "%s/join_link.taken", platform_data_root());
#endif
	/* (taken first: a link the launcher writes while this reads is left for
	the next look, not removed unread; a .taken left by a run that stopped
	in between would make the rename fail on some file systems) */
	remove(taken);
	if (rename(path, taken) != 0)
		return;
	file = fopen(taken, "rb");
	if (!file)
		return;
	size = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	remove(taken);
	text[size] = 0;
	p2p_invite_received(text);
}
#endif

/* ---------- the thread */

static void *p2p_thread(void *unused)
{
	enum
	{
		MAXIMUM_SOCKETS = 2 + MAXIMUM_PROXIES + MAXIMUM_LISTENERS + MAXIMUM_STREAMS + 16,
#ifdef HALO_VITA
		/* the most one select waits on: the Vita's takes 128 (vita_net.c's
		SELECT_MAXIMUM) and drops the rest, so the tunnel and the brokers go
		first and stand-ins past it wait for a later pass (a Vita's game of
		16 machines needs a few dozen) */
		SELECT_LIMIT = 128,
#else
		SELECT_LIMIT = MAXIMUM_SOCKETS,
#endif
		/* what each socket waited for is (owners) */
		_owner_tunnel = 0,
		_owner_handoff,
		_owner_proxy,
		_owner_listener,
		_owner_stream,
		_owner_signal,
	};
	/* the sockets waited for, and those that are ready (the same order,
	which posix_socket_select keeps), and whose each is: its kind in the low
	byte, its index above */
	static int read[MAXIMUM_SOCKETS], write[MAXIMUM_SOCKETS];
	static int asked_read[MAXIMUM_SOCKETS], asked_write[MAXIMUM_SOCKETS];
	static int read_owners[MAXIMUM_SOCKETS], write_owners[MAXIMUM_SOCKETS];

	(void)unused;
	pthread_mutex_lock(&p2p_lock);
#ifndef HALO_ANDROID
	/* (here: it may wait for a program) */
	p2p_register_url_scheme("halo", "Halo: Combat Evolved invite");
#endif
	for (;;)
	{
		int read_count = 0, write_count = 0, error_count = 0;
		int asked_read_count, asked_write_count;
		/* KCP's clock needs a pass every LOOP_INTERVAL while it carries
		streams; otherwise the thread can sleep longer */
		int wait = LOOP_INTERVAL * 5;
		int index, asked;

		/* what to wait for: the tunnel, then the brokers (whose sockets are
		few), then the stand-ins, as many as a select takes */
		read_owners[read_count] = _owner_tunnel;
		read[read_count++] = p2p.tunnel_socket;
		if (p2p.handoff_socket >= 0)
		{
			read_owners[read_count] = _owner_handoff;
			read[read_count++] = p2p.handoff_socket;
		}
		asked_read_count = read_count;
		asked_write_count = write_count;
		p2p_signal_select_sets(read, &read_count, write, &write_count, SELECT_LIMIT - read_count - write_count);
		for (index = asked_read_count; index < read_count; index++)
			read_owners[index] = _owner_signal;
		for (index = asked_write_count; index < write_count; index++)
			write_owners[index] = _owner_signal;
		for (index = 0; index < MAXIMUM_PROXIES && read_count + write_count < SELECT_LIMIT; index++)
		{
			if (p2p.proxies[index].socket >= 0)
			{
				read_owners[read_count] = _owner_proxy | index << 8;
				read[read_count++] = p2p.proxies[index].socket;
			}
		}
		for (index = 0; index < MAXIMUM_LISTENERS && read_count + write_count < SELECT_LIMIT; index++)
		{
			if (p2p.listeners[index].socket >= 0)
			{
				read_owners[read_count] = _owner_listener | index << 8;
				read[read_count++] = p2p.listeners[index].socket;
			}
		}
		for (index = 0; index < MAXIMUM_STREAMS; index++)
		{
			struct stream *stream = &p2p.streams[index];

			if (stream->used)
				wait = LOOP_INTERVAL;
			if (!stream->used || stream->socket < 0 || read_count + write_count + 2 > SELECT_LIMIT)
				continue;
			/* (not while the tunnel's window is full, which stream_readable
			waits out, or the wait would return at once) */
			if (stream->state == _stream_open_state && ikcp_waitsnd(stream->kcp) < STREAM_WINDOW)
			{
				read_owners[read_count] = _owner_stream | index << 8;
				read[read_count++] = stream->socket;
			}
			if (stream->state == _stream_connecting || stream->pending_size)
			{
				write_owners[write_count] = _owner_stream | index << 8;
				write[write_count++] = stream->socket;
			}
		}
		asked_read_count = read_count;
		asked_write_count = write_count;
		memcpy(asked_read, read, sizeof(*read) * (size_t)read_count);
		memcpy(asked_write, write, sizeof(*write) * (size_t)write_count);

		pthread_mutex_unlock(&p2p_lock);
		if (posix_socket_select(read, &read_count, write, &write_count, NULL, &error_count, 0,
			wait * 1000, 0) < 0)
		{
			read_count = write_count = 0;
		}
		pthread_mutex_lock(&p2p_lock);

		/* what is ready (the lists now hold only ready sockets, in the order
		asked, so each one's owner is found going along both); a socket
		closed meanwhile is not its owner's any more */
		for (index = 0, asked = 0; index < read_count; index++)
		{
			int socket = read[index];
			int owner, entry;

			while (asked < asked_read_count && asked_read[asked] != socket)
				asked++;
			if (asked == asked_read_count)
				break;
			owner = read_owners[asked++];
			entry = owner >> 8;
			switch (owner & 255)
			{
			case _owner_tunnel:
				tunnel_readable();
				break;
			case _owner_handoff:
				if (socket == p2p.handoff_socket)
					handoff_readable();
				break;
			case _owner_proxy:
				if (p2p.proxies[entry].socket == socket)
					proxy_readable(&p2p.proxies[entry]);
				break;
			case _owner_listener:
				if (p2p.listeners[entry].socket == socket)
					listener_readable(&p2p.listeners[entry]);
				break;
			case _owner_stream:
				if (p2p.streams[entry].used && p2p.streams[entry].socket == socket)
					stream_readable(&p2p.streams[entry]);
				break;
			}
		}
		for (index = 0, asked = 0; index < write_count; index++)
		{
			int socket = write[index];
			int entry;

			while (asked < asked_write_count && asked_write[asked] != socket)
				asked++;
			if (asked == asked_write_count)
				break;
			entry = write_owners[asked] >> 8;
			if ((write_owners[asked++] & 255) == _owner_stream && p2p.streams[entry].used &&
				p2p.streams[entry].socket == socket)
			{
				stream_writeable(&p2p.streams[entry]);
			}
		}
		/* a connection to the game that failed is in neither list */
		for (index = 0; index < MAXIMUM_STREAMS; index++)
		{
			struct stream *stream = &p2p.streams[index];

			if (stream->used && stream->state == _stream_connecting && elapsed(stream->created_time, 5000))
				stream_local_closed(stream);
		}
		p2p_signal_update(read, read_count, write, write_count);

		for (index = 0; index < MAXIMUM_STREAMS; index++)
		{
			if (p2p.streams[index].used)
				stream_update(&p2p.streams[index]);
		}
		p2p_adhoc_update();
		update_peers();
		expire_proxies();
		stun_update();
		relay_resolve();
		update_hosting();
		/* the server browser (p2p_lobby.c): signalling while browsing too
		(not in ad hoc play, which reaches no broker) */
		if ((p2p_lobby_browsing() || p2p_lobby_brokers_wanted()) && !p2p.adhoc)
			p2p_signal_start();
		if (!p2p.adhoc)
		{
			int count, maximum;

			hosted_player_counts(&count, &maximum);
			/* (listed once the game's server has said its players: until
			then the listing would be the last game's, with the tunnel's
			count) */
			p2p_lobby_update(p2p.hosting && p2p.has_token && p2p.game_player_maximum > 0 ? p2p.token : NULL, count,
				maximum);
			if (p2p_lobby_listed())
			{
				p2p.token_listed = 1;
				if (!p2p.lobby_quit_registered)
				{
					p2p.lobby_quit_registered = 1;
					atexit(lobby_quit);
				}
			}
		}
		update_joining();
		update_upnp();
		p2p_discord_update();
		resolver_cache_write();
#if defined(HALO_ANDROID) || defined(HALO_VITA)
		poll_invite_file();
#endif
	}
	return NULL;
}

void p2p_initialize(unsigned long local_address)
{
	char invite[256];
	pthread_t thread;
	long tunnel_port = config_integer("network.tunnel_port");
	int index;

	p2p_identifier();
	if (p2p.running)
		return;
	/* (ad hoc play is internet play's tunnel without the internet) */
	if (!config_boolean("network.online") && !config_boolean("network.adhoc"))
	{
		platform_log("Internet play: disabled; enable Online and restart to use invites");
		return;
	}
	if (tunnel_port < 0 || tunnel_port > 65535)
	{
		platform_log("Internet play: network.tunnel_port %ld is not a port (0 to 65535); the game selects one",
			tunnel_port);
		tunnel_port = 0;
	}
	for (index = 0; index < MAXIMUM_PROXIES; index++)
		p2p.proxies[index].socket = -1;
	for (index = 0; index < MAXIMUM_LISTENERS; index++)
		p2p.listeners[index].socket = -1;
	for (index = 0; index < MAXIMUM_STREAMS; index++)
		p2p.streams[index].socket = -1;
	p2p.local_address = local_address;
	p2p.adhoc = config_boolean("network.adhoc");
	p2p.tunnel_socket = open_socket(SOCK_DGRAM, 0, network_short((unsigned short)tunnel_port), &p2p.tunnel_port);
	if (p2p.tunnel_socket < 0)
	{
		platform_log("Internet play: cannot open its socket (network.tunnel_port %ld in use?); it is off",
			tunnel_port);
		return;
	}
	{
		/* all of a host's traffic, up to a 128-machine game's, goes through
		this one socket: buffers as large as the game's own sockets' (at
		least 1 MB, transport_endpoint_winsock.c) */
		int size = 1 << 20;

		posix_socket_setsockopt(p2p.tunnel_socket, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
		posix_socket_setsockopt(p2p.tunnel_socket, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
	}
#if !defined(HALO_ANDROID) && !defined(HALO_VITA) && !defined(HALO_DEDICATED_SERVER)
	/* the first copy of the game takes the invites later ones are opened
	with (not a dedicated server, which joins nothing) */
	p2p.has_handoff_key = handoff_key(p2p.handoff_key);
	if (p2p.has_handoff_key)
		p2p.handoff_socket = open_socket(SOCK_DGRAM, network_long(0x7F000001), network_short(HANDOFF_PORT), NULL);
#endif
	stun_setup();
	relay_setup();
	{
		int error = pthread_create(&thread, NULL, p2p_thread, NULL);
		if (error != 0)
		{
			platform_log("Internet play: cannot start network thread: %d", error);
			close_socket(&p2p.tunnel_socket);
			close_socket(&p2p.handoff_socket);
			return;
		}
	}
	pthread_detach(thread);
	p2p.running = 1;
	platform_log("Internet play: network thread started");
	if (p2p.adhoc)
		p2p_adhoc_start(p2p.tunnel_port);
	if (command_line_invite(invite, sizeof(invite)))
		p2p_join_invite(invite);
}
