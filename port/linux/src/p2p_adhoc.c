/*
P2P_ADHOC.C

Ad hoc play (network.adhoc): machines in one ad hoc wireless group - the
Vita's, made by its system dialog, no router - play system link as
internet play's peers do (p2p.c), with the group in place of the internet.

The Vita's ad hoc network carries datagrams between 6-byte (MAC) addresses
on ad hoc ports (posix_adhoc_*: sceNetAdhocPdp on the Vita), not IP: the
game's own sockets cannot reach the other machines. Internet play already
carries everything the game sends between two machines (its datagrams,
and its streams over KCP) through one UDP socket, the tunnel, to an
address and port per peer. So this file bridges each machine of the group
to the tunnel:

- Every second each machine broadcasts a beacon in the group: "HCEV" (a
  Vita's; "HCEA" elsewhere: Vitas play only Vitas, halo_port_limits.h), a
  version, its internet play identifier, its network version and a nonce.
- A beacon from a new address gets a relay: a UDP socket on 127.0.0.1 that
  stands for that machine. p2p_adhoc_update offers the peer to p2p.c with
  the relay as its one address and a session secret both machines derive
  from their two identifiers and nonces (the group is as private as a LAN:
  anyone in it could join the game anyway); the machine with the lower
  identifier takes the tunnel's joiner side, the other the host's, so each
  direction has its key. The tunnel then pings the relay as it would a
  peer's internet address. A session that ended is never taken up again
  (p2p.c: its packet numbers would start over), so a machine that finds
  its pair's secret retired makes a new nonce, and the next beacons give
  both a new secret.
- What the tunnel sends to a relay goes to that machine's address in the
  group (as it is: a tunnel packet, sealed with the pair's key); what
  arrives from a machine in the group goes to the tunnel from its relay,
  so the tunnel sees it come from where it sends to that machine.

From there all is internet play's: each peer has a virtual address, the
game's broadcasts reach every peer, so a host's game shows in the others'
system link lists, and joining is as on a LAN. Signalling, STUN and UPnP,
which need the internet, are off in ad hoc play (p2p.c).

Two threads: one waits for the group's datagrams (and sends the beacons),
one for the relays'. The relays are never closed while the game runs (a
group has a handful of machines), so neither thread can find a socket gone
under it.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "p2p_internal.h"

#include <stdio.h>
#include <string.h>

enum
{
	/* the ad hoc port every machine's bridge uses (its own namespace: not
	a UDP port) */
	ADHOC_PORT = 2306,
	ADHOC_MAXIMUM_PEERS = 16,
	NONCE_SIZE = 8,
	BEACON_SIZE = 4 + 1 + P2P_IDENTIFIER_SIZE + 2 + NONCE_SIZE,
	/* 2: a nonce, for the session secret (1 made a key of the identifiers
	alone) */
	BEACON_VERSION = 2,
	/* a tunnel packet's largest, and some (p2p.c's MAXIMUM_PACKET_SIZE is
	1435; an ad hoc datagram's largest, SCE_NET_ADHOC_PDP_MFS, 1444) */
	PACKET_BUFFER_SIZE = 1500,

	/* milliseconds */
	BEACON_INTERVAL = 1000,
	/* a machine whose beacons stopped this long ago is no longer offered */
	PEER_SILENCE = 10000,
	OFFER_INTERVAL = 1000,
	NOT_READY_WAIT = 250,
	/* microseconds */
	RECEIVE_WAIT = 100000,
	RELAY_WAIT = 50000,
};

static const unsigned char broadcast_address[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

#ifdef HALO_PORT_VITA_NETWORK
#define BEACON_MAGIC "HCEV"
#else
#define BEACON_MAGIC "HCEA"
#endif

struct adhoc_peer
{
	int used;
	unsigned char address[6];
	unsigned char identifier[P2P_IDENTIFIER_SIZE];
	unsigned char nonce[NONCE_SIZE];
	int network_version;
	/* the relay standing for it, and its port (network byte order) */
	int relay;
	unsigned short relay_port;
	unsigned long heard_time;
	unsigned long offered_time;
	int reported_version;
};

static struct
{
	int started;
	pthread_mutex_t lock;
	/* the tunnel's port (network byte order) */
	unsigned short tunnel_port;
	unsigned char identifier[P2P_IDENTIFIER_SIZE];
	unsigned char nonce[NONCE_SIZE];
	int ready;
	int opened;
	unsigned char address[6];
	struct adhoc_peer peers[ADHOC_MAXIMUM_PEERS];
	unsigned long beacon_time;
	unsigned long sent, received, dropped;
} adhoc = { 0, PTHREAD_MUTEX_INITIALIZER };

static int elapsed(unsigned long since, unsigned long time)
{
	return (long)(p2p_now() - since) >= (long)time;
}

static void address_text(const unsigned char *address, char *text)
{
	sprintf(text, "%02x:%02x:%02x:%02x:%02x:%02x", address[0], address[1], address[2], address[3], address[4],
		address[5]);
}

static unsigned long loopback(void)
{
	/* 127.0.0.1 in network byte order */
	return __builtin_bswap32(0x7F000001);
}

static void make_loopback_address(struct sockaddr_in *address, unsigned short port)
{
	memset(address, 0, sizeof(*address));
	address->sin_family = AF_INET;
	address->sin_port = port;
	address->sin_addr.s_addr = loopback();
}

/* a UDP socket on 127.0.0.1, not blocking; its port through port */
static int open_relay(unsigned short *port)
{
	struct sockaddr_in address;
	int length = sizeof(address);
	int result = posix_socket(AF_INET, SOCK_DGRAM, 0);

	if (result < 0)
		return -1;
	make_loopback_address(&address, 0);
	if (posix_socket_bind(result, &address, sizeof(address)) < 0 ||
		posix_socket_getsockname(result, &address, &length) < 0 ||
		posix_socket_set_nonblocking(result, 1) < 0)
	{
		posix_socket_close(result);
		return -1;
	}
	*port = address.sin_port;
	return result;
}

/* under adhoc.lock */
static struct adhoc_peer *find_peer(const unsigned char *address)
{
	int index;

	for (index = 0; index < ADHOC_MAXIMUM_PEERS; index++)
	{
		if (adhoc.peers[index].used && !memcmp(adhoc.peers[index].address, address, 6))
			return &adhoc.peers[index];
	}
	return NULL;
}

/* under adhoc.lock: a beacon from address */
static void beacon_received(const unsigned char *address, const unsigned char *beacon)
{
	struct adhoc_peer *peer;
	const unsigned char *identifier = beacon + 5;
	char text[24];
	int index;

	if (!memcmp(identifier, adhoc.identifier, P2P_IDENTIFIER_SIZE) || !memcmp(address, adhoc.address, 6))
		return;
	peer = find_peer(address);
	if (!peer)
	{
		int relay = -1;
		unsigned short relay_port = 0;

		for (index = 0; index < ADHOC_MAXIMUM_PEERS && adhoc.peers[index].used; index++)
			;
		/* full: a machine silent for PEER_SILENCE (which is no longer
		offered) gives its place, and its relay, which is never closed (a
		machine in the group could send beacons from many made-up addresses,
		and keep the group's real machines out for good) */
		if (index == ADHOC_MAXIMUM_PEERS)
		{
			for (index = 0; index < ADHOC_MAXIMUM_PEERS && !elapsed(adhoc.peers[index].heard_time, PEER_SILENCE);
				index++)
				;
			if (index == ADHOC_MAXIMUM_PEERS)
				return;
			relay = adhoc.peers[index].relay;
			relay_port = adhoc.peers[index].relay_port;
		}
		peer = &adhoc.peers[index];
		memset(peer, 0, sizeof(*peer));
		peer->relay = relay >= 0 ? relay : open_relay(&relay_port);
		peer->relay_port = relay_port;
		if (peer->relay < 0)
		{
			platform_log("ad hoc: cannot open a relay for a machine in the group");
			return;
		}
		memcpy(peer->address, address, 6);
		peer->used = 1;
		address_text(address, text);
		platform_log("ad hoc: machine %s is in the group", text);
	}
	memcpy(peer->identifier, identifier, P2P_IDENTIFIER_SIZE);
	peer->network_version = beacon[5 + P2P_IDENTIFIER_SIZE] << 8 | beacon[5 + P2P_IDENTIFIER_SIZE + 1];
	memcpy(peer->nonce, beacon + 5 + P2P_IDENTIFIER_SIZE + 2, NONCE_SIZE);
	peer->heard_time = p2p_now();
}

static void send_beacon(void)
{
	unsigned char beacon[BEACON_SIZE];

	memcpy(beacon, BEACON_MAGIC, 4);
	beacon[4] = BEACON_VERSION;
	memcpy(beacon + 5, adhoc.identifier, P2P_IDENTIFIER_SIZE);
	beacon[5 + P2P_IDENTIFIER_SIZE] = (unsigned char)(HALO_PORT_NETWORK_VERSION >> 8);
	beacon[5 + P2P_IDENTIFIER_SIZE + 1] = (unsigned char)HALO_PORT_NETWORK_VERSION;
	pthread_mutex_lock(&adhoc.lock);
	memcpy(beacon + 5 + P2P_IDENTIFIER_SIZE + 2, adhoc.nonce, NONCE_SIZE);
	pthread_mutex_unlock(&adhoc.lock);
	posix_adhoc_send(broadcast_address, ADHOC_PORT, beacon, sizeof(beacon));
	adhoc.beacon_time = p2p_now();
}

/* the group's datagrams: beacons, and tunnel packets for the tunnel */
static void *receive_thread(void *unused)
{
	unsigned char packet[PACKET_BUFFER_SIZE];

	(void)unused;
	for (;;)
	{
		unsigned char address[6];
		unsigned short port = 0;
		int size;

		if (!adhoc.opened)
		{
			char text[24];

			if (!posix_adhoc_ready(address))
			{
				Sleep(NOT_READY_WAIT);
				continue;
			}
			if (posix_adhoc_open(ADHOC_PORT) != 0)
			{
				platform_log("ad hoc: cannot open the group's port; trying again");
				Sleep(1000);
				continue;
			}
			pthread_mutex_lock(&adhoc.lock);
			memcpy(adhoc.address, address, 6);
			adhoc.opened = 1;
			pthread_mutex_unlock(&adhoc.lock);
			address_text(address, text);
			platform_log("ad hoc: in the group as %s; looking for the other machines", text);
		}
		if (elapsed(adhoc.beacon_time, BEACON_INTERVAL))
			send_beacon();
		size = posix_adhoc_receive(address, &port, packet, sizeof(packet), RECEIVE_WAIT);
		if (size < 0)
		{
			/* (the group is gone: the bridge waits for one again) */
			platform_log("ad hoc: the group's port failed; waiting for a group");
			pthread_mutex_lock(&adhoc.lock);
			adhoc.opened = 0;
			pthread_mutex_unlock(&adhoc.lock);
			posix_adhoc_close();
			Sleep(1000);
			continue;
		}
		if (size == 0)
			continue;
		pthread_mutex_lock(&adhoc.lock);
		if (size == BEACON_SIZE && !memcmp(packet, BEACON_MAGIC, 4) && packet[4] == BEACON_VERSION)
		{
			beacon_received(address, packet);
		}
		else
		{
			struct adhoc_peer const *peer = find_peer(address);

			if (peer)
			{
				struct sockaddr_in to;

				make_loopback_address(&to, adhoc.tunnel_port);
				posix_socket_sendto(peer->relay, packet, size, 0, &to, sizeof(to));
				adhoc.received++;
			}
			else
			{
				/* (its beacon has not come yet) */
				adhoc.dropped++;
			}
		}
		pthread_mutex_unlock(&adhoc.lock);
	}
	return NULL;
}

/* the relays: what the tunnel sends a machine of the group */
static void *relay_thread(void *unused)
{
	unsigned char packet[PACKET_BUFFER_SIZE];

	(void)unused;
	for (;;)
	{
		int read[ADHOC_MAXIMUM_PEERS];
		int read_count = 0, write_count = 0, error_count = 0;
		int index;

		pthread_mutex_lock(&adhoc.lock);
		for (index = 0; index < ADHOC_MAXIMUM_PEERS; index++)
		{
			if (adhoc.peers[index].used)
				read[read_count++] = adhoc.peers[index].relay;
		}
		pthread_mutex_unlock(&adhoc.lock);
		if (!read_count)
		{
			Sleep(RELAY_WAIT / 1000);
			continue;
		}
		if (posix_socket_select(read, &read_count, NULL, &write_count, NULL, &error_count, 0, RELAY_WAIT, 0) <= 0)
			continue;
		for (index = 0; index < read_count; index++)
		{
			for (;;)
			{
				struct sockaddr_in from;
				int from_length = sizeof(from);
				int size = posix_socket_recvfrom(read[index], packet, sizeof(packet), 0, &from, &from_length);
				struct adhoc_peer const *peer = NULL;
				int slot;

				if (size <= 0)
					break;
				/* (only the tunnel's: the relay's port is on 127.0.0.1, but any
				local program could send to it) */
				if (from.sin_addr.s_addr != loopback() || from.sin_port != adhoc.tunnel_port)
					continue;
				pthread_mutex_lock(&adhoc.lock);
				for (slot = 0; slot < ADHOC_MAXIMUM_PEERS; slot++)
				{
					if (adhoc.peers[slot].used && adhoc.peers[slot].relay == read[index])
						peer = &adhoc.peers[slot];
				}
				if (peer && adhoc.opened)
				{
					unsigned char address[6];

					memcpy(address, peer->address, 6);
					adhoc.sent++;
					pthread_mutex_unlock(&adhoc.lock);
					posix_adhoc_send(address, ADHOC_PORT, packet, size);
				}
				else
				{
					pthread_mutex_unlock(&adhoc.lock);
				}
			}
		}
	}
	return NULL;
}

void p2p_adhoc_start(unsigned short tunnel_port)
{
	pthread_t thread;

	if (adhoc.started)
		return;
	adhoc.started = 1;
	adhoc.tunnel_port = tunnel_port;
	memcpy(adhoc.identifier, p2p_identifier(), P2P_IDENTIFIER_SIZE);
	posix_random_bytes(adhoc.nonce, NONCE_SIZE);
	adhoc.beacon_time = p2p_now() - BEACON_INTERVAL;
	if (pthread_create(&thread, NULL, receive_thread, NULL) == 0)
		pthread_detach(thread);
	if (pthread_create(&thread, NULL, relay_thread, NULL) == 0)
		pthread_detach(thread);
	platform_log("ad hoc: on; waiting for this machine to be in an ad hoc group");
}

/* the session secret two machines' tunnel uses: from their identifiers
and nonces, the lower identifier's first, so both make the same */
static void pair_secret(const unsigned char *a, const unsigned char *a_nonce, const unsigned char *b,
	const unsigned char *b_nonce, unsigned char *secret)
{
	unsigned char data[12 + 2 * (P2P_IDENTIFIER_SIZE + NONCE_SIZE)];
	int a_first = memcmp(a, b, P2P_IDENTIFIER_SIZE) < 0;
	int size = 0;

	memcpy(data, BEACON_MAGIC " ad hoc ", 12);
	size = 12;
	memcpy(data + size, a_first ? a : b, P2P_IDENTIFIER_SIZE);
	size += P2P_IDENTIFIER_SIZE;
	memcpy(data + size, a_first ? b : a, P2P_IDENTIFIER_SIZE);
	size += P2P_IDENTIFIER_SIZE;
	memcpy(data + size, a_first ? a_nonce : b_nonce, NONCE_SIZE);
	size += NONCE_SIZE;
	memcpy(data + size, a_first ? b_nonce : a_nonce, NONCE_SIZE);
	size += NONCE_SIZE;
	p2p_sha256(data, size, secret);
}

void p2p_adhoc_update(void)
{
	int index;

	if (!adhoc.started)
		return;
	pthread_mutex_lock(&adhoc.lock);
	for (index = 0; index < ADHOC_MAXIMUM_PEERS; index++)
	{
		struct adhoc_peer *peer = &adhoc.peers[index];
		struct p2p_candidate candidate;
		unsigned char secret[P2P_SHA256_SIZE];

		if (!peer->used || elapsed(peer->heard_time, PEER_SILENCE) || !elapsed(peer->offered_time, OFFER_INTERVAL))
			continue;
		peer->offered_time = p2p_now();
		if (peer->network_version != HALO_PORT_NETWORK_VERSION)
		{
			/* (the game would turn it away; said once) */
			if (!peer->reported_version)
			{
				platform_log("ad hoc: a machine in the group plays network version %d, this one %d: update both "
					"to the same version", peer->network_version, HALO_PORT_NETWORK_VERSION);
				peer->reported_version = 1;
			}
		}
		/* (offered again every second while it is heard: p2p.c takes a
		repeat as the same peer, and a dropped one back) */
		pair_secret(adhoc.identifier, adhoc.nonce, peer->identifier, peer->nonce, secret);
		if (p2p_session_retired(secret))
		{
			/* (the session with it ended: a new one, from a new nonce, once
			its beacons carry this machine's) */
			posix_random_bytes(adhoc.nonce, NONCE_SIZE);
			platform_log("ad hoc: a session ended; starting a new one");
			continue;
		}
		candidate.address = loopback();
		candidate.port = peer->relay_port;
		/* (the lower identifier is the tunnel's joiner: is_host, the peer
		as the host, picks each direction's key) */
		p2p_peer_offered(peer->identifier, secret, &candidate, 1,
			memcmp(adhoc.identifier, peer->identifier, P2P_IDENTIFIER_SIZE) < 0);
	}
	pthread_mutex_unlock(&adhoc.lock);
}

int p2p_adhoc_status(char *text, int size)
{
	int count = 0;
	int index;

	if (!adhoc.started)
		return 0;
	pthread_mutex_lock(&adhoc.lock);
	for (index = 0; index < ADHOC_MAXIMUM_PEERS; index++)
		count += adhoc.peers[index].used && !elapsed(adhoc.peers[index].heard_time, PEER_SILENCE);
	if (!adhoc.opened)
		snprintf(text, (size_t)size, "not in an ad hoc group");
	else
		snprintf(text, (size_t)size, "in the group, %d other machine%s", count, count == 1 ? "" : "s");
	pthread_mutex_unlock(&adhoc.lock);
	return 1;
}
