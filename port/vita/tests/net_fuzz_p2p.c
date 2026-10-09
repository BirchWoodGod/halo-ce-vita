/*
NET_FUZZ_P2P.C

A fuzz target for internet play's tunnel (port/linux/src/p2p.c, included
here whole for its statics): what any machine can send the tunnel's UDP
port, and what a peer holding a session's keys can send through it.

An input is a list of records, each a kind, a 16-bit length and that many
bytes (net_fuzz_next_record):
  0  a datagram to the tunnel from anywhere (raw: mostly turned away)
  1  a tunnel packet from the peer, sealed with its key: a ping, pong,
     datagram for the game, a KCP segment of a stream, a bye
  2  the same with the packet's number taken from the first 8 bytes
     (replays, the window's edges)
  3  a STUN answer to the request made (its transaction), from the server
  4  text given as an invite or a code (a link, the clipboard, a file)
  5  an invite handed over by another copy of the game (sealed)
  6  the clock moves on (the first byte, in tenths of a second)
  7  the game's datagram to the peer, and lookups of addresses
     (and the host's ping table: the peer is this machine's game's host,
     p2p_set_ping_table_host)
  8  (a kind byte of 0x88 only) a datagram from the peer's relay: as it is
     (an answer to an allocation's request), or, the first byte odd, the
     peer's tunnel packet sealed and passed on in a DATA message on the
     relay's channel (the rest of the bytes)
After each record the p2p thread's pass runs (its streams, peers, stand-ins),
as it would. Every input starts from the same state: one peer connected,
the game's UDP and TCP port 2302 open.

Built twice by run_net_fuzz_test.sh: with libFuzzer, and with
net_fuzz_main.c, which replays the cases kept in net_fuzz_cases/p2p and
runs the checks of net_fuzz_p2p_checks.
*/

#include "../../linux/src/p2p.c"
#include "../../linux/src/p2p_crypto.c"
#include "../../linux/src/p2p_resolver_cache.c"
#include "net_fuzz_common.h"

/* ---------- what p2p_signal.c, p2p_adhoc.c and p2p_discord.c give p2p.c */

void p2p_signal_start(void) {}
void p2p_signal_select_sets(int *read, int *read_count, int *write, int *write_count, int maximum_count)
{
	(void)read; (void)read_count; (void)write; (void)write_count; (void)maximum_count;
}
void p2p_signal_update(const int *read, int read_count, const int *write, int write_count)
{
	(void)read; (void)read_count; (void)write; (void)write_count;
}
void p2p_signal_host(const unsigned char *token, const char *code) { (void)token; (void)code; }
void p2p_signal_stop_hosting(void) {}
/* (the server browser's, p2p_lobby.c, and its signalling: nothing listed) */
void p2p_lobby_update(const unsigned char *token, int player_count, int maximum_player_count)
{
	(void)token; (void)player_count; (void)maximum_player_count;
}
int p2p_lobby_listed(void) { return 0; }
int p2p_lobby_browsing(void) { return 0; }
int p2p_lobby_brokers_wanted(void) { return 0; }
int p2p_lobby_hosting_status_locked(char *text, int size)
{
	if (size > 0)
		text[0] = 0;
	return P2P_LOBBY_HOSTING_NONE;
}
void p2p_signal_kick(void) {}
void p2p_lobby_quit(void) {}
void p2p_lobby_join_timed_out(const unsigned char *host_hash) { (void)host_hash; }
void p2p_signal_lookup_code(const char *code, const unsigned char *host) { (void)code; (void)host; }
void p2p_signal_stop_lookup(void) {}
void p2p_signal_join(const unsigned char *host_hash, const unsigned char *token) { (void)host_hash; (void)token; }
void p2p_signal_stop_joining(void) {}
int p2p_signal_connected(void) { return 1; }
int p2p_list_setting(const char *override_setting, const char *file_setting, char *text, size_t size)
{
	(void)override_setting; (void)file_setting;
	if (size)
		text[0] = 0;
	return 0;
}
void p2p_adhoc_start(unsigned short tunnel_port) { (void)tunnel_port; }
void p2p_adhoc_update(void) {}
void p2p_discord_update(void) {}
void p2p_discord_user(char *id, int id_size, char *name, int name_size)
{
	(void)id_size; (void)name_size;
	id[0] = name[0] = 0;
}
void p2p_discord_set_hosting(const char *secret, int player_count, int maximum_player_count)
{
	(void)secret; (void)player_count; (void)maximum_player_count;
}

/* ---------- the state each input starts from */

/* the peer: its identifier, the session's secret, the address it sends
from, and the next number of the packets it seals */
static const unsigned char fuzz_peer_identifier[P2P_IDENTIFIER_SIZE] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const unsigned char fuzz_peer_secret[P2P_SHA256_SIZE] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
static const unsigned long fuzz_peer_address = 0x0100A8C0; /* 192.168.0.1 */
static const unsigned short fuzz_peer_port = 0x0F27;
static unsigned long long fuzz_peer_counter;
/* the peer's relay, its allocation ready: where it is, its channel, and the
nonce of the requests (its answers carry it) */
static const unsigned long fuzz_relay_address = 0x057100CB; /* 203.0.113.5 */
static const unsigned short fuzz_relay_port = 0xD8B8;
static const unsigned long fuzz_relay_channel = 0x12345001;
static int fuzz_game_udp, fuzz_game_tcp;

/* the peer's relay, ready (as after its ALLOCATED) */
static void fuzz_relay_setup(void)
{
	struct peer *peer = find_peer(fuzz_peer_identifier);
	struct peer_relay *relay;

	p2p.relay_allowed = 1;
	if (!peer)
		return;
	peer->relaying = 1;
	peer->relay_count = 1;
	relay = &peer->relays[0];
	memset(relay, 0, sizeof(*relay));
	relay->address.address = fuzz_relay_address;
	relay->address.port = fuzz_relay_port;
	relay->state = _relay_ready;
	relay->channel = fuzz_relay_channel;
	memset(relay->nonce, 0x77, sizeof(relay->nonce));
}

static void fuzz_reset(void)
{
	int index;

	for (index = 0; index < MAXIMUM_STREAMS; index++)
	{
		if (p2p.streams[index].kcp)
			ikcp_release(p2p.streams[index].kcp);
	}
	memset(&p2p, 0, sizeof(p2p));
	memset(proxy_by_port, 0, sizeof(proxy_by_port));
	p2p.tunnel_socket = 1;
	p2p.handoff_socket = 2;
	p2p.hosting_socket = -1;
	for (index = 0; index < MAXIMUM_PROXIES; index++)
		p2p.proxies[index].socket = -1;
	for (index = 0; index < MAXIMUM_LISTENERS; index++)
		p2p.listeners[index].socket = -1;
	for (index = 0; index < MAXIMUM_STREAMS; index++)
		p2p.streams[index].socket = -1;
	p2p.local_address = 0x0100007F;
	p2p.running = 1;
	p2p.stun_started = 1;
	p2p.has_handoff_key = handoff_key(p2p.handoff_key);
	stun_setup();
	net_fuzz_queue_count = 0;
	net_fuzz_clock = 1000000;
	/* the game's sockets: UDP 2302, and TCP 2302 listening (it hosts) */
	fuzz_game_udp = 50;
	fuzz_game_tcp = 51;
	p2p_set_game_accepts_remote(1);
	p2p_socket_port(fuzz_game_udp, 0, 0, network_short(2302));
	p2p_socket_port(fuzz_game_tcp, 1, 1, network_short(2302));
	/* the peer, reached */
	p2p_peer_offered(fuzz_peer_identifier, fuzz_peer_secret, NULL, 0, 0);
	{
		struct peer *peer = find_peer(fuzz_peer_identifier);

		peer_heard(peer, fuzz_peer_address, fuzz_peer_port, 1, 1);
	}
	fuzz_relay_setup();
	fuzz_peer_counter = 1;
	/* (the peer is the game's host: its ping tables are taken) */
	memset(&ping_table, 0, sizeof(ping_table));
	ping_table.host = find_peer(fuzz_peer_identifier)->virtual_address;
}

/* after each record: a table taken holds only pings it may (at most
P2P_PING_MAXIMUM, or none), and is read whole */
static void fuzz_ping_table_invariant(void)
{
	unsigned short pings[P2P_PING_TABLE_PLAYERS + 4];
	long tick = -1;
	int index;

	if (!ping_table.valid)
		return;
	if (p2p_ping_table(pings, P2P_PING_TABLE_PLAYERS + 4, &tick) < 0 || tick < 0)
		abort();
	for (index = 0; index < P2P_PING_TABLE_PLAYERS + 4; index++)
	{
		if (pings[index] != P2P_PING_UNKNOWN && (index >= P2P_PING_TABLE_PLAYERS || pings[index] > P2P_PING_MAXIMUM))
			abort();
	}
}

/* a tunnel packet the peer seals (with the key of its direction) */
static int fuzz_seal(const unsigned char *inner, int size, unsigned long long counter, unsigned char *packet)
{
	unsigned char joiner_key[P2P_SHA256_SIZE];
	unsigned char nonce[P2P_NONCE_SIZE];
	int index;

	if (size > MAXIMUM_INNER_SIZE)
		size = MAXIMUM_INNER_SIZE;
	/* (this machine took the host's side: the peer sends with the
	joiner's key) */
	p2p_hmac_sha256(fuzz_peer_secret, P2P_SHA256_SIZE, "joiner", 6, joiner_key);
	packet[0] = TUNNEL_MAGIC;
	memcpy(packet + 1, fuzz_peer_identifier, P2P_IDENTIFIER_SIZE);
	for (index = 0; index < 8; index++)
		packet[1 + P2P_IDENTIFIER_SIZE + index] = (unsigned char)(counter >> (index * 8));
	packet_nonce(packet, nonce);
	return TUNNEL_HEADER_SIZE + p2p_aead_seal(joiner_key, nonce, packet, TUNNEL_HEADER_SIZE, inner, size,
		packet + TUNNEL_HEADER_SIZE);
}

/* the p2p thread's pass (p2p_thread), without the sockets' waits */
static void fuzz_pass(void)
{
	int index;

	for (index = 0; index < MAXIMUM_STREAMS; index++)
	{
		struct stream *stream = &p2p.streams[index];

		if (stream->used && stream->state == _stream_connecting)
			stream_writeable(stream);
		if (stream->used && stream->socket >= 0 && stream->state == _stream_open_state)
			stream_readable(stream);
	}
	for (index = 0; index < MAXIMUM_STREAMS; index++)
	{
		if (p2p.streams[index].used)
			stream_update(&p2p.streams[index]);
	}
	for (index = 0; index < MAXIMUM_PROXIES; index++)
	{
		if (p2p.proxies[index].socket >= 0)
			proxy_readable(&p2p.proxies[index]);
	}
	update_peers();
	expire_proxies();
	/* (the peer back, if it left: the next records still have one) */
	if (!find_peer(fuzz_peer_identifier))
	{
		memset(p2p.retired, 0, sizeof(p2p.retired));
		p2p_peer_offered(fuzz_peer_identifier, fuzz_peer_secret, NULL, 0, 0);
		peer_heard(find_peer(fuzz_peer_identifier), fuzz_peer_address, fuzz_peer_port, 1, 1);
		fuzz_relay_setup();
		fuzz_peer_counter = 1;
		ping_table.host = find_peer(fuzz_peer_identifier)->virtual_address;
	}
}

static void fuzz_record(int kind, const unsigned char *data, int size)
{
	static unsigned char packet[MAXIMUM_PACKET_SIZE + 64];
	struct sockaddr_in from;

	memset(&from, 0, sizeof(from));
	from.sin_family = AF_INET;
	from.sin_addr.s_addr = fuzz_peer_address;
	from.sin_port = fuzz_peer_port;
	switch (kind)
	{
	case 0:
		if (size > 2048)
			size = 2048;
		tunnel_received(data, size, &from);
		break;
	case 1:
		tunnel_received(packet, fuzz_seal(data, size, ++fuzz_peer_counter, packet), &from);
		break;
	case 2:
	{
		unsigned long long counter = 0;

		if (size < 8)
			break;
		memcpy(&counter, data, 8);
		tunnel_received(packet, fuzz_seal(data + 8, size - 8, counter, packet), &from);
		break;
	}
	case 3:
	{
		struct stun_server *server = &p2p.stun[0];

		if (!p2p.stun_count || size > MAXIMUM_PACKET_SIZE - 20)
			break;
		server->address = 0x04030201;
		server->attempts = 1;
		memset(server->transaction, 0x5A, sizeof(server->transaction));
		packet[0] = 0x01;
		packet[1] = 0x01;
		packet[2] = (unsigned char)(size >> 8);
		packet[3] = (unsigned char)size;
		packet[4] = 0x21; packet[5] = 0x12; packet[6] = 0xA4; packet[7] = 0x42;
		memcpy(packet + 8, server->transaction, 12);
		memcpy(packet + 20, data, (size_t)size);
		from.sin_addr.s_addr = server->address;
		from.sin_port = server->port;
		tunnel_received(packet, size + 20, &from);
		break;
	}
	case 4:
	{
		char text[600];

		if (size > (int)sizeof(text) - 1)
			size = (int)sizeof(text) - 1;
		memcpy(text, data, (size_t)size);
		text[size] = 0;
		p2p_invite_received(text);
		p2p_join_code(text);
		if (p2p.lookup_requested)
		{
			p2p.looking_up = 1;
			p2p_code_found(text);
		}
		break;
	}
	case 5:
	{
		unsigned char message[12 + 256 + P2P_SEAL_OVERHEAD + 32];

		if (size > 256 + 16)
			size = 256 + 16;
		memcpy(message, "halo-invite ", 12);
		size = 12 + p2p_seal(p2p.handoff_key, data, size, message + 12);
		net_fuzz_queue_receive(p2p.handoff_socket, message, size, 0x0100007F, 0x1234);
		handoff_readable();
		net_fuzz_queue_count = 0;
		break;
	}
	case 6:
		net_fuzz_clock += size ? data[0] * 100UL : 100UL;
		break;
	case 7:
	{
		struct peer *peer = find_peer(fuzz_peer_identifier);
		unsigned long address;
		unsigned short port;

		if (!peer || size < 6)
			break;
		memcpy(&port, data, 2);
		p2p_send_datagram(port, peer->virtual_address, network_short(2302), data + 2, size - 2);
		memcpy(&address, data + 2, 4);
		p2p_outgoing(data[0] & 1, -1, &address, &port);
		memcpy(&address, data + 2, 4);
		address = (address & 0xFFFF) | (p2p.local_address & 0xFFFF0000);
		p2p_incoming(data[1] & 1, &address, &port);
		break;
	}
	case 8:
		from.sin_addr.s_addr = fuzz_relay_address;
		from.sin_port = fuzz_relay_port;
		if (size > 0 && (data[0] & 1))
		{
			int sealed = fuzz_seal(data + 1, size - 1, ++fuzz_peer_counter, packet + P2P_RELAY_DATA_HEADER_SIZE);

			packet[0] = P2P_RELAY_MAGIC;
			packet[1] = _relay_data;
			packet[2] = (unsigned char)(fuzz_relay_channel >> 24);
			packet[3] = (unsigned char)(fuzz_relay_channel >> 16);
			packet[4] = (unsigned char)(fuzz_relay_channel >> 8);
			packet[5] = (unsigned char)fuzz_relay_channel;
			tunnel_received(packet, P2P_RELAY_DATA_HEADER_SIZE + sealed, &from);
		}
		else
		{
			if (size > 2048)
				size = 2048;
			tunnel_received(data, size, &from);
		}
		break;
	}
}

/* the next record of an input: its kind and bytes; 0 at its end */
static int net_fuzz_next_record(const unsigned char **cursor, const unsigned char *end, int *kind,
	const unsigned char **record, int *size)
{
	int length;

	if (end - *cursor < 3)
		return 0;
	*kind = (*cursor)[0] == 0x88 ? 8 : (*cursor)[0] & 7;
	length = (*cursor)[1] | (*cursor)[2] << 8;
	*cursor += 3;
	if (length > end - *cursor)
		length = (int)(end - *cursor);
	*record = *cursor;
	*size = length;
	*cursor += length;
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const unsigned char *cursor = data, *end = data + size;
	const unsigned char *record;
	int kind, record_size, count = 0;

	fuzz_reset();
	while (count++ < 64 && net_fuzz_next_record(&cursor, end, &kind, &record, &record_size))
	{
		fuzz_record(kind, record, record_size);
		fuzz_pass();
		fuzz_ping_table_invariant();
	}
	return 0;
}

/* ---------- checks (net_fuzz_main.c runs them before the cases) */

static int fuzz_failures;

static void fuzz_check(int condition, const char *what)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	fuzz_failures += !condition;
}

int net_fuzz_checks(void)
{
	static unsigned char packet[MAXIMUM_PACKET_SIZE + 64];
	unsigned char ping[5] = { _packet_ping, 1, 2, 3, 4 };
	unsigned char hash[P2P_KEY_HASH_SIZE], token[P2P_TOKEN_SIZE];
	char code[P2P_CODE_LENGTH + 1];
	char text[128];
	struct sockaddr_in from;
	struct peer *peer;
	unsigned long virtual_address;
	unsigned long other;
	int sent, size;

	fuzz_reset();
	peer = find_peer(fuzz_peer_identifier);
	virtual_address = peer->virtual_address;
	other = virtual_address ^ network_long(0x00000100);
	fuzz_check(p2p_spoofed_source(virtual_address), "a peer's virtual address as a datagram's own source is spoofed");
	fuzz_check(!p2p_spoofed_source(other), "another address in 100.64.0.0/10 (a carrier's NAT, a VPN) is not");
	fuzz_check(!p2p_spoofed_source(network_long(0xC0A80105)) && !p2p_spoofed_source(p2p.local_address),
		"a LAN address and the stand-ins' address are not");

	memset(&from, 0, sizeof(from));
	from.sin_family = AF_INET;
	from.sin_addr.s_addr = fuzz_peer_address;
	from.sin_port = fuzz_peer_port;
	size = fuzz_seal(ping, sizeof(ping), 5, packet);
	sent = net_fuzz_sent_count;
	tunnel_received(packet, size, &from);
	fuzz_check(net_fuzz_sent_count == sent + 1, "a sealed ping from the peer is answered");
	tunnel_received(packet, size, &from);
	fuzz_check(net_fuzz_sent_count == sent + 1, "the same packet again (a replay) is not");
	from.sin_addr.s_addr = 0x0200A8C0;
	tunnel_received(packet, size, &from);
	fuzz_check(net_fuzz_sent_count == sent + 1 && find_peer(fuzz_peer_identifier)->endpoint.address == fuzz_peer_address,
		"nor from another address, which does not become the peer's");
	from.sin_addr.s_addr = fuzz_peer_address;
	size = fuzz_seal(ping, sizeof(ping), 100, packet);
	tunnel_received(packet, size, &from);
	size = fuzz_seal(ping, sizeof(ping), 6, packet);
	sent = net_fuzz_sent_count;
	tunnel_received(packet, size, &from);
	fuzz_check(net_fuzz_sent_count == sent, "a packet from before the last 64 is dropped");
	size = fuzz_seal(ping, sizeof(ping), 101, packet);
	packet[size - 1] ^= 1;
	tunnel_received(packet, size, &from);
	fuzz_check(net_fuzz_sent_count == sent, "an altered packet is dropped");

	/* packets in the peer's name from elsewhere: a few a second checked */
	{
		int index;

		from.sin_addr.s_addr = 0x0300A8C0;
		sent = net_fuzz_sent_count;
		for (index = 0; index < 2 * STRAY_PACKETS_PER_SECOND; index++)
		{
			size = fuzz_seal(ping, sizeof(ping), 200 + index, packet);
			tunnel_received(packet, size, &from);
		}
		fuzz_check(net_fuzz_sent_count == sent + STRAY_PACKETS_PER_SECOND,
			"of packets from an address the peer is not known at, a few a second are checked");
		from.sin_addr.s_addr = fuzz_peer_address;
		size = fuzz_seal(ping, sizeof(ping), 300, packet);
		tunnel_received(packet, size, &from);
		fuzz_check(net_fuzz_sent_count == sent + STRAY_PACKETS_PER_SECOND + 1, "and the peer's own still all");
	}

	/* a datagram for a port the game has not opened goes nowhere */
	{
		unsigned char datagram[9] = { _packet_datagram, 0x12, 0x34, 0x00, 0x16, 'h', 'i', '!', 0 };

		size = fuzz_seal(datagram, sizeof(datagram), 302, packet);
		sent = net_fuzz_sent_count;
		tunnel_received(packet, size, &from);
		fuzz_check(net_fuzz_sent_count == sent, "a peer's datagram to a port the game has not opened is dropped");
		datagram[3] = 0x08;
		datagram[4] = 0xFE;
		size = fuzz_seal(datagram, sizeof(datagram), 303, packet);
		tunnel_received(packet, size, &from);
		fuzz_check(net_fuzz_sent_count == sent + 1, "one to the game's port 2302 is passed on");
	}

	/* the relay: a ping it passes on is answered through it, on its channel;
	nothing on another channel, from another address, or in another
	machine's name; its cookie is taken at once */
	{
		unsigned char frame[P2P_RELAY_DATA_HEADER_SIZE + MAXIMUM_PACKET_SIZE];
		unsigned char answer[P2P_RELAY_ALLOCATED_SIZE];
		struct peer_relay *relay = &find_peer(fuzz_peer_identifier)->relays[0];

		from.sin_addr.s_addr = fuzz_relay_address;
		from.sin_port = fuzz_relay_port;
		frame[0] = P2P_RELAY_MAGIC;
		frame[1] = _relay_data;
		frame[2] = 0x12; frame[3] = 0x34; frame[4] = 0x50; frame[5] = 0x01;
		size = P2P_RELAY_DATA_HEADER_SIZE + fuzz_seal(ping, sizeof(ping), 400, frame + P2P_RELAY_DATA_HEADER_SIZE);
		sent = net_fuzz_sent_count;
		tunnel_received(frame, size, &from);
		fuzz_check(net_fuzz_sent_count == sent + 1 && net_fuzz_sent[0] == P2P_RELAY_MAGIC &&
			net_fuzz_sent[1] == _relay_data && !memcmp(net_fuzz_sent + 2, frame + 2, 4) &&
			net_fuzz_sent[P2P_RELAY_DATA_HEADER_SIZE] == TUNNEL_MAGIC,
			"a ping the relay passes on is answered through the relay, on its channel");
		size = P2P_RELAY_DATA_HEADER_SIZE + fuzz_seal(ping, sizeof(ping), 401, frame + P2P_RELAY_DATA_HEADER_SIZE);
		frame[5] = 0x02;
		tunnel_received(frame, size, &from);
		frame[5] = 0x01;
		from.sin_port ^= 1;
		tunnel_received(frame, size, &from);
		from.sin_port ^= 1;
		frame[P2P_RELAY_DATA_HEADER_SIZE + 1] ^= 1;
		tunnel_received(frame, size, &from);
		frame[P2P_RELAY_DATA_HEADER_SIZE + 1] ^= 1;
		fuzz_check(net_fuzz_sent_count == sent + 1,
			"not on another channel, from another port, or naming another machine");
		memset(answer, 0, sizeof(answer));
		answer[0] = P2P_RELAY_MAGIC;
		answer[1] = _relay_cookie;
		answer[2] = P2P_RELAY_VERSION;
		memset(answer + 4, 0x77, P2P_RELAY_NONCE_SIZE);
		{
			unsigned char cookie[P2P_RELAY_COOKIE_MESSAGE_SIZE];

			memset(cookie, 0, sizeof(cookie));
			memcpy(cookie, answer, 4 + P2P_RELAY_NONCE_SIZE);
			memset(cookie + 4 + P2P_RELAY_NONCE_SIZE, 0xC0, P2P_RELAY_COOKIE_SIZE);
			cookie[4] = 0x76;
			tunnel_received(cookie, sizeof(cookie), &from);
			fuzz_check(net_fuzz_sent_count == sent + 1, "a cookie for another nonce is not taken");
			cookie[4] = 0x77;
			tunnel_received(cookie, sizeof(cookie), &from);
			fuzz_check(net_fuzz_sent_count == sent + 2 && net_fuzz_sent_size == P2P_RELAY_ALLOCATE_SIZE &&
				net_fuzz_sent[1] == _relay_allocate && net_fuzz_sent[P2P_RELAY_COOKIE_OFFSET] == 0xC0 &&
				!memcmp(net_fuzz_sent + P2P_RELAY_ALLOCATION_OFFSET, find_peer(fuzz_peer_identifier)->relay_allocation,
				P2P_RELAY_ALLOCATION_SIZE), "the relay's cookie is taken: the allocation asked for again with it");
		}
		answer[1] = _relay_allocated;
		answer[3] = P2P_RELAY_BUSY;
		tunnel_received(answer, sizeof(answer), &from);
		fuzz_check(relay->state == _relay_refused, "a relay that is full refuses");
		answer[3] = P2P_RELAY_READY;
		answer[P2P_RELAY_CHANNEL_OFFSET + 3] = 9;
		tunnel_received(answer, sizeof(answer), &from);
		fuzz_check(relay->state == _relay_ready && relay->channel == 9, "and then carries, on the channel it gives");
		from.sin_addr.s_addr = fuzz_peer_address;
		from.sin_port = fuzz_peer_port;
	}

	/* the host's ping table (p2p_send_ping_table, ping_table_received) */
	{
		unsigned char table[1 + PING_TABLE_HEADER_SIZE + (P2P_PING_TABLE_PLAYERS + 1) * PING_TABLE_ENTRY_SIZE];
		unsigned short pings[P2P_PING_TABLE_PLAYERS];
		unsigned short sent_pings[P2P_PING_TABLE_PLAYERS];
		unsigned long counter = 500;
		long tick = 0;
		int body, index, lines;

		/* (a table of three players: 0 at 0 ms, 3 at 45, 15 at 9999) */
		table[0] = _packet_ping_table;
		for (index = 0; index < P2P_PING_TABLE_PLAYERS; index++)
			sent_pings[index] = P2P_PING_UNKNOWN;
		sent_pings[0] = 0;
		sent_pings[3] = 45;
		sent_pings[15] = 12000;
		body = ping_table_write(table + 1, 0x12345, sent_pings, P2P_PING_TABLE_PLAYERS);
		fuzz_check(body == PING_TABLE_HEADER_SIZE + 3 * PING_TABLE_ENTRY_SIZE, "a table of three players is 15 bytes");
		size = fuzz_seal(table, 1 + body, counter++, packet);
		tunnel_received(packet, size, &from);
		fuzz_check(p2p_ping_table(pings, P2P_PING_TABLE_PLAYERS, &tick) >= 0 && tick == 0x12345 && pings[0] == 0 &&
			pings[3] == 45 && pings[15] == P2P_PING_MAXIMUM && pings[1] == P2P_PING_UNKNOWN &&
			pings[127] == P2P_PING_UNKNOWN, "the host's table is taken (a ping past 9999 sent as 9999)");
		fuzz_check(find_peer(fuzz_peer_identifier) && find_peer(fuzz_peer_identifier)->connected,
			"and the host stays connected");

		/* malformed: each dropped whole, the table had kept */
		lines = net_fuzz_log_lines;
		{
			static const struct
			{
				int offset;
				int value;
				int size_change;
				const char *what;
			} bad[] = {
				{ 6, 4, 0, "a count past its entries" },
				{ 6, 2, 0, "entries past its count" },
				{ 0, 0, -1, "a byte short" },
				{ 0, 0, 1, "a byte more" },
				{ 7 + 3, 0, 0, "a player named twice" },
				{ 7 + 3, 128, 0, "a player index of 128" },
				{ 7 + 3, 255, 0, "a player index of 255" },
				{ 8 + 3, 0x27, 0, "a ping of 10000 ms or more (0x2710)" },
				{ 2, 0x80, 0, "a tick past 2^31" },
				{ 6, 255, 0, "a count of 255" },
			};
			int case_index;

			for (case_index = 0; case_index < (int)(sizeof(bad) / sizeof(bad[0])); case_index++)
			{
				unsigned char altered[sizeof(table)];
				int altered_size = 1 + body + bad[case_index].size_change;

				memcpy(altered, table, sizeof(table));
				if (bad[case_index].offset)
					altered[bad[case_index].offset] = (unsigned char)bad[case_index].value;
				/* (a ping of 0x27xx: its low byte past 0x0F) */
				if (bad[case_index].offset == 8 + 3)
					altered[9 + 3] = 0x10;
				size = fuzz_seal(altered, altered_size, counter++, packet);
				tunnel_received(packet, size, &from);
				p2p_ping_table(pings, P2P_PING_TABLE_PLAYERS, &tick);
				snprintf(text, sizeof(text), "a table with %s is dropped (the one had kept)", bad[case_index].what);
				fuzz_check(tick == 0x12345 && pings[3] == 45 && pings[15] == P2P_PING_MAXIMUM, text);
			}
		}
		fuzz_check(net_fuzz_log_lines <= lines + 1 && ping_table.dropped == 10,
			"malformed tables are counted, and logged once");
		/* another version's: ignored, unlogged (a later format) */
		table[1] = PING_TABLE_VERSION + 1;
		size = fuzz_seal(table, 1 + body, counter++, packet);
		tunnel_received(packet, size, &from);
		table[1] = PING_TABLE_VERSION;
		fuzz_check(ping_table.dropped == 10 && p2p_ping_table(pings, P2P_PING_TABLE_PLAYERS, &tick) >= 0 &&
			tick == 0x12345, "a table of another version is ignored");
		/* nothing but its type, and a table of no player */
		size = fuzz_seal(table, 1, counter++, packet);
		tunnel_received(packet, size, &from);
		sent_pings[0] = sent_pings[3] = sent_pings[15] = P2P_PING_UNKNOWN;
		body = ping_table_write(table + 1, 7, sent_pings, P2P_PING_TABLE_PLAYERS);
		size = fuzz_seal(table, 1 + body, counter++, packet);
		tunnel_received(packet, size, &from);
		fuzz_check(body == PING_TABLE_HEADER_SIZE && p2p_ping_table(pings, P2P_PING_TABLE_PLAYERS, &tick) >= 0 &&
			tick == 7 && pings[3] == P2P_PING_UNKNOWN, "an empty table is one (no player known)");
		/* every player */
		for (index = 0; index < P2P_PING_TABLE_PLAYERS; index++)
			sent_pings[index] = (unsigned short)(index * 3);
		body = ping_table_write(table + 1, 8, sent_pings, P2P_PING_TABLE_PLAYERS);
		size = fuzz_seal(table, 1 + body, counter++, packet);
		tunnel_received(packet, size, &from);
		fuzz_check(body == PING_TABLE_HEADER_SIZE + P2P_PING_TABLE_PLAYERS * PING_TABLE_ENTRY_SIZE &&
			p2p_ping_table(pings, P2P_PING_TABLE_PLAYERS, &tick) >= 0 && tick == 8 && pings[127] == 381,
			"a table of 128 players (390 bytes) is taken");
		/* a count of 128 with a 129th entry */
		table[1 + 5] = 128;
		memcpy(table + 1 + body, table + 1 + body - PING_TABLE_ENTRY_SIZE, PING_TABLE_ENTRY_SIZE);
		size = fuzz_seal(table, 1 + body + PING_TABLE_ENTRY_SIZE, counter++, packet);
		tunnel_received(packet, size, &from);
		fuzz_check(p2p_ping_table(pings, P2P_PING_TABLE_PLAYERS, &tick) >= 0 && tick == 8,
			"one with a 129th entry is not");
		/* only the game's host's: another peer (or none) is not */
		ping_table.host = other;
		body = ping_table_write(table + 1, 9, sent_pings, P2P_PING_TABLE_PLAYERS);
		size = fuzz_seal(table, 1 + body, counter++, packet);
		tunnel_received(packet, size, &from);
		ping_table.host = virtual_address;
		fuzz_check(p2p_ping_table(pings, P2P_PING_TABLE_PLAYERS, &tick) >= 0 && tick == 8,
			"a table from a peer that is not the game's host is not taken");
		p2p_set_ping_table_host(0);
		fuzz_check(p2p_ping_table(pings, P2P_PING_TABLE_PLAYERS, &tick) < 0, "no host: no table");
		size = fuzz_seal(table, 1 + body, counter++, packet);
		tunnel_received(packet, size, &from);
		fuzz_check(p2p_ping_table(pings, P2P_PING_TABLE_PLAYERS, &tick) < 0, "nor one taken");
		p2p_set_ping_table_host(virtual_address);
		size = fuzz_seal(table, 1 + body, counter++, packet);
		tunnel_received(packet, size, &from);
		net_fuzz_clock += 2500;
		fuzz_check(p2p_ping_table(pings, P2P_PING_TABLE_PLAYERS, &tick) == 2500 && tick == 9,
			"the host again: its table taken, its age told");
		p2p_set_ping_table_host(other);
		fuzz_check(p2p_ping_table(pings, P2P_PING_TABLE_PLAYERS, &tick) < 0, "another host: the table had is dropped");
		p2p_set_ping_table_host(virtual_address);

		/* the host's side: one packet, the table's size and the seal's */
		sent = net_fuzz_sent_count;
		fuzz_check(p2p_send_ping_table(virtual_address, 10, sent_pings, P2P_PING_TABLE_PLAYERS) == 1 &&
			net_fuzz_sent_count == sent + 1 &&
			net_fuzz_sent_size == TUNNEL_HEADER_SIZE + 1 + body + P2P_TAG_SIZE, "the host sends a client one packet");
		fuzz_check(!p2p_send_ping_table(other, 10, sent_pings, P2P_PING_TABLE_PLAYERS) &&
			!p2p_send_ping_table(network_long(0xC0A80105), 10, sent_pings, P2P_PING_TABLE_PLAYERS) &&
			!p2p_send_ping_table(virtual_address, -1, sent_pings, P2P_PING_TABLE_PLAYERS) &&
			net_fuzz_sent_count == sent + 1, "and nothing to an address no peer has (a LAN's), or of a tick below 0");

		/* a type this build does not know (as the betas do the table):
		dropped unread, the peer kept, nothing logged or answered */
		{
			unsigned char unknown[3] = { _packet_ping_table + 1, 1, 2 };

			lines = net_fuzz_log_lines;
			sent = net_fuzz_sent_count;
			size = fuzz_seal(unknown, sizeof(unknown), counter++, packet);
			tunnel_received(packet, size, &from);
			unknown[0] = 0xFF;
			size = fuzz_seal(unknown, sizeof(unknown), counter++, packet);
			tunnel_received(packet, size, &from);
			fuzz_check(net_fuzz_sent_count == sent && net_fuzz_log_lines == lines &&
				find_peer(fuzz_peer_identifier) && find_peer(fuzz_peer_identifier)->connected,
				"a packet of a type not known is dropped unread: no answer, no log, the peer kept");
		}
	}

	/* invites and codes */
	snprintf(text, sizeof(text), "join me: halo://join/%s%s please", "0123456789abcdef0123456789abcdef",
		"00112233445566778899aabbccddeeff");
	fuzz_check(parse_invite(text, hash, token) == 1 && hash[0] == 0x01 && token[15] == 0xFF, "an invite link within text");
	text[strlen("join me: halo://join/") + 63] = 'x';
	fuzz_check(parse_invite(text, hash, token) == 0, "one a digit short is not one");
	fuzz_check(parse_invite("halo://join/", hash, token) == 0 && parse_invite("", hash, token) == 0,
		"nor an empty one");
	fuzz_check(parse_code(" abcd-efgh\n", code, 1) && !strcmp(code, "ABCDEFGH"), "a code, any case, spaces around");
	fuzz_check(!parse_code("ABCDEFGH", code, 1) && parse_code("ABCDEFGH", code, 0),
		"its dash is wanted only where a word could pass for one");
	fuzz_check(!parse_code("ABCD-EFG1", code, 0) && !parse_code("ABCD-EFGHI", code, 0) && !parse_code("ABC", code, 0),
		"letters outside its alphabet, or too many or few, are not one");

	fuzz_reset();
	return fuzz_failures;
}
