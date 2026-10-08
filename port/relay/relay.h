/*
RELAY.H

The relay's state and rules (relay.c), apart from its socket (main.c), so
that the tests and the fuzzer drive it as the network would. Addresses and
ports are in network byte order; times are milliseconds of a monotonic
clock.
*/

#ifndef HALO_RELAY_H
#define HALO_RELAY_H

#include <stddef.h>
#include <stdint.h>

#include "../linux/src/p2p_relay_protocol.h"

enum
{
	/* the most allocations a relay can be set to hold: a channel's low
	bits are its allocation's index */
	RELAY_MAXIMUM_ALLOCATIONS = 4096,
	RELAY_INDEX_BITS = 12,
	RELAY_KEY_SIZE = 32,
};

struct relay_config
{
	/* allocations at once, and those any one IPv4 address may be in (a
	host whose players all come through the relay is in one for each) */
	int maximum_allocations;
	int maximum_per_address;
	/* an allocation's bytes a second (both ways together), its burst, its
	packets a second, and its bytes in all (0: no limit) */
	uint64_t rate;
	uint64_t burst;
	uint32_t packet_rate;
	uint64_t maximum_bytes;
	/* all allocations' bytes a second together (0: no limit) */
	uint64_t total_rate;
	/* milliseconds: an allocation's longest life; how long one with a
	single machine waits for the other; how long both may be silent; how
	long one side must be silent before another address may take it (a
	machine whose NAT gave it a new port) */
	uint64_t lifetime;
	uint64_t half_open_time;
	uint64_t idle_time;
	uint64_t rebind_time;
	/* cookies (COOKIE) answered a second, from anyone */
	uint32_t cookie_rate;
};

struct relay_side
{
	int used;
	uint32_t address;
	uint16_t port;
	/* the nonce of its latest ALLOCATE, which ALLOCATED carries */
	uint8_t nonce[P2P_RELAY_NONCE_SIZE];
	uint64_t heard_time;
	uint64_t bytes;
	uint64_t packets;
};

struct relay_allocation
{
	int used;
	uint8_t id[P2P_RELAY_ALLOCATION_SIZE];
	uint32_t channel;
	/* 0: the host's side, 1: the joiner's */
	struct relay_side sides[2];
	uint64_t created_time;
	uint64_t ready_time;
	/* its token buckets: bytes and packets */
	uint64_t byte_tokens;
	uint64_t byte_time;
	uint64_t packet_tokens;
	uint64_t packet_time;
	uint64_t dropped;
};

struct relay_statistics
{
	uint64_t allocations_opened;
	uint64_t allocations_ready;
	uint64_t bytes;
	uint64_t packets;
	uint64_t dropped;
	uint64_t cookies;
	uint64_t refused;
	uint64_t ignored;
};

/* what the relay sends: to an address and port */
typedef void (*relay_send_function)(void *context, uint32_t address, uint16_t port, const uint8_t *data, size_t size);
/* a line for the log (no addresses: a tag of a keyed hash of one, which
another run cannot link) */
typedef void (*relay_log_function)(void *context, const char *line);

struct relay
{
	struct relay_config config;
	struct relay_allocation *allocations;
	int allocation_count;
	uint8_t cookie_key[RELAY_KEY_SIZE];
	uint8_t log_key[RELAY_KEY_SIZE];
	/* the channels' high bits, which change for each allocation */
	uint32_t channel_counter;
	uint64_t cookie_tokens;
	uint64_t cookie_time;
	uint64_t total_tokens;
	uint64_t total_time;
	struct relay_statistics statistics;
	relay_send_function send;
	relay_log_function log;
	void *context;
};

/* the settings a relay starts with unless told otherwise (README.md) */
void relay_default_config(struct relay_config *config);
/* a relay with that configuration and a random secret (RELAY_KEY_SIZE * 2
bytes: its cookies' key and its log's); 0 if there is no memory, or the
configuration is out of range */
int relay_initialize(struct relay *relay, const struct relay_config *config, const uint8_t *secret,
	relay_send_function send, relay_log_function log, void *context);
void relay_release(struct relay *relay);
/* a datagram from address:port, at now */
void relay_received(struct relay *relay, const uint8_t *packet, size_t size, uint32_t address, uint16_t port,
	uint64_t now);
/* closes what has lapsed; call it about once a second */
void relay_expire(struct relay *relay, uint64_t now);
/* the cookie an address is given now (the tests') */
void relay_cookie(const struct relay *relay, uint32_t address, uint16_t port, uint64_t epoch, uint8_t *cookie);
/* the allocations open */
int relay_open_allocations(const struct relay *relay);

#endif
