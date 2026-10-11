/*
RELAY.C

A relay for internet play's tunnel (port/linux/src/p2p.c): two machines
whose NATs keep them from reaching each other directly each send their
tunnel's packets here, and the relay passes each one to the other machine
as it came. The messages are p2p_relay_protocol.h's.

What it will and will not do, as it is open to the internet:

- It sends only to an address that showed it receives there: a request's
  cookie (a keyed hash of its address and the time, given in an answer
  smaller than the request) must come back before the relay keeps anything
  of it. So a request with a spoofed source makes the relay send one
  answer, smaller than the request, to that source, and nothing more.
- It forwards only between the two addresses of an allocation: one on the
  host's side and one on the joiner's, which both asked for it by its
  identifier, 16 bytes derived from their session's secret that no one else
  has (not the brokers, not the others holding the invite). It can reach no
  address that did not ask to be reached, so it is no proxy; and the packets
  are sealed with the session's keys, so it reads and changes nothing of
  the game.
- A side, once taken, is its address's until that address is silent for
  rebind_time (a machine whose NAT gave it a new port).
- Each allocation has a rate (bytes and packets a second), a byte limit, a
  longest life, and lapses when both sides are silent; one side alone waits
  half_open_time. The relay holds maximum_allocations at once, any address
  in maximum_per_address of them, and all of them together total_rate.
- Every packet that breaks a rule is dropped without an answer.
- Its log names no address: only a tag, a keyed hash of one with a key made
  each run, so lines of one run can be told apart and no run's linked to
  another's or to an address.
*/

#include "relay.h"

#include "monocypher.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	/* a cookie is taken in the period it was made in and the next */
	COOKIE_PERIOD = 60000,
};

void relay_default_config(struct relay_config *config)
{
	memset(config, 0, sizeof(*config));
	config->maximum_allocations = 256;
	config->maximum_per_address = 16;
	/* 4 Mbit/s: a game's traffic is a few dozen kbit/s each way; a map
	download (map sharing) takes what it is given */
	config->rate = 500000;
	config->burst = 256 * 1024;
	config->packet_rate = 1000;
	config->maximum_bytes = 1024ULL * 1024 * 1024;
	config->total_rate = 0;
	config->lifetime = 6ULL * 60 * 60 * 1000;
	config->half_open_time = 30000;
	config->idle_time = 60000;
	config->rebind_time = 15000;
	config->cookie_rate = 2000;
}

/* ---------- helpers */

static uint64_t since(uint64_t now, uint64_t time)
{
	return now > time ? now - time : 0;
}

static void put_long(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)(value >> 24);
	bytes[1] = (uint8_t)(value >> 16);
	bytes[2] = (uint8_t)(value >> 8);
	bytes[3] = (uint8_t)value;
}

static uint32_t get_long(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
}

/* a token bucket: refilled at rate a second up to burst; whether amount is
left (taken if so) */
static int bucket_take(uint64_t *tokens, uint64_t *time, uint64_t rate, uint64_t burst, uint64_t amount,
	uint64_t now)
{
	uint64_t elapsed = since(now, *time);

	*time = now;
	if (elapsed >= 1000000 || rate * elapsed / 1000 >= burst - *tokens)
		*tokens = burst;
	else
		*tokens += rate * elapsed / 1000;
	if (*tokens < amount)
		return 0;
	*tokens -= amount;
	return 1;
}

/* an address's tag in the log */
static void address_tag(const struct relay *relay, uint32_t address, char *text)
{
	uint8_t hash[4];

	crypto_blake2b_keyed(hash, sizeof(hash), relay->log_key, sizeof(relay->log_key), (const uint8_t *)&address,
		sizeof(address));
	snprintf(text, 9, "%02x%02x%02x%02x", hash[0], hash[1], hash[2], hash[3]);
}

static void log_line(const struct relay *relay, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void log_line(const struct relay *relay, const char *format, ...)
{
	char line[256];
	va_list arguments;

	if (!relay->log)
		return;
	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	relay->log(relay->context, line);
}

void relay_cookie(const struct relay *relay, uint32_t address, uint16_t port, uint64_t epoch, uint8_t *cookie)
{
	uint8_t data[14];
	int index;

	for (index = 0; index < 8; index++)
		data[index] = (uint8_t)(epoch >> (index * 8));
	memcpy(data + 8, &address, 4);
	memcpy(data + 12, &port, 2);
	crypto_blake2b_keyed(cookie, P2P_RELAY_COOKIE_SIZE, relay->cookie_key, sizeof(relay->cookie_key), data,
		sizeof(data));
}

static int cookie_valid(const struct relay *relay, const uint8_t *cookie, uint32_t address, uint16_t port,
	uint64_t now)
{
	uint8_t expected[P2P_RELAY_COOKIE_SIZE];
	uint64_t epoch = now / COOKIE_PERIOD;
	int valid;

	relay_cookie(relay, address, port, epoch, expected);
	valid = !crypto_verify16(expected, cookie);
	if (!valid && epoch)
	{
		relay_cookie(relay, address, port, epoch - 1, expected);
		valid = !crypto_verify16(expected, cookie);
	}
	return valid;
}

/* ---------- allocations */

int relay_open_allocations(const struct relay *relay)
{
	return relay->allocation_count;
}

static int side_is(const struct relay_side *side, uint32_t address, uint16_t port)
{
	return side->used && side->address == address && side->port == port;
}

/* the sides any allocation gives this address */
static int address_sides(const struct relay *relay, uint32_t address)
{
	int count = 0;
	int index;

	for (index = 0; index < relay->config.maximum_allocations; index++)
	{
		const struct relay_allocation *allocation = &relay->allocations[index];

		if (allocation->used)
			count += (allocation->sides[0].used && allocation->sides[0].address == address) +
				(allocation->sides[1].used && allocation->sides[1].address == address);
	}
	return count;
}

static void allocation_close(struct relay *relay, struct relay_allocation *allocation, const char *reason,
	uint64_t now)
{
	log_line(relay, "allocation %u closed (%s) after %llu s: host side %llu B in %llu packets, joiner side "
		"%llu B in %llu packets, %llu dropped", (unsigned)(allocation - relay->allocations), reason,
		(unsigned long long)(since(now, allocation->created_time) / 1000),
		(unsigned long long)allocation->sides[0].bytes, (unsigned long long)allocation->sides[0].packets,
		(unsigned long long)allocation->sides[1].bytes, (unsigned long long)allocation->sides[1].packets,
		(unsigned long long)allocation->dropped);
	memset(allocation, 0, sizeof(*allocation));
	relay->allocation_count--;
}

static struct relay_allocation *allocation_find(struct relay *relay, const uint8_t *id)
{
	int index;

	for (index = 0; index < relay->config.maximum_allocations; index++)
	{
		struct relay_allocation *allocation = &relay->allocations[index];

		if (allocation->used && !memcmp(allocation->id, id, P2P_RELAY_ALLOCATION_SIZE))
			return allocation;
	}
	return NULL;
}

static struct relay_allocation *allocation_new(struct relay *relay, const uint8_t *id, uint64_t now)
{
	int index;

	for (index = 0; index < relay->config.maximum_allocations; index++)
	{
		struct relay_allocation *allocation = &relay->allocations[index];
		uint8_t counter[4], random[4];

		if (allocation->used)
			continue;
		memset(allocation, 0, sizeof(*allocation));
		allocation->used = 1;
		memcpy(allocation->id, id, P2P_RELAY_ALLOCATION_SIZE);
		/* a channel no one can guess (its high bits a keyed hash of a
		counter), never 0; its low bits the index */
		do
		{
			put_long(counter, ++relay->channel_counter);
			crypto_blake2b_keyed(random, sizeof(random), relay->cookie_key, sizeof(relay->cookie_key), counter,
				sizeof(counter));
			allocation->channel = (get_long(random) << RELAY_INDEX_BITS) | (uint32_t)index;
		} while (!allocation->channel);
		allocation->created_time = now;
		allocation->byte_time = now;
		allocation->packet_time = now;
		allocation->byte_tokens = relay->config.burst;
		allocation->packet_tokens = relay->config.packet_rate;
		relay->allocation_count++;
		relay->statistics.allocations_opened++;
		return allocation;
	}
	return NULL;
}

static void send_allocated(struct relay *relay, const struct relay_allocation *allocation, int status,
	const uint8_t *nonce, uint32_t address, uint16_t port, uint64_t now)
{
	uint8_t answer[P2P_RELAY_ALLOCATED_SIZE];
	uint64_t left = 0;

	memset(answer, 0, sizeof(answer));
	answer[0] = P2P_RELAY_MAGIC;
	answer[1] = _relay_allocated;
	answer[2] = P2P_RELAY_VERSION;
	answer[3] = (uint8_t)status;
	memcpy(answer + P2P_RELAY_NONCE_OFFSET, nonce, P2P_RELAY_NONCE_SIZE);
	if (allocation)
	{
		uint64_t age = since(now, allocation->created_time);

		put_long(answer + P2P_RELAY_CHANNEL_OFFSET, allocation->channel);
		left = age < relay->config.lifetime ? (relay->config.lifetime - age) / 1000 : 0;
		put_long(answer + P2P_RELAY_LIFETIME_OFFSET, left > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (uint32_t)left);
	}
	relay->send(relay->context, address, port, answer, sizeof(answer));
}

/* an ALLOCATE */
static void allocate_received(struct relay *relay, const uint8_t *packet, size_t size, uint32_t address,
	uint16_t port, uint64_t now)
{
	const uint8_t *nonce = packet + P2P_RELAY_NONCE_OFFSET;
	const uint8_t *id = packet + P2P_RELAY_ALLOCATION_OFFSET;
	struct relay_allocation *allocation;
	struct relay_side *side, *other;
	char tag[9];
	int role;
	int was_ready;
	size_t index;

	if (size != P2P_RELAY_ALLOCATE_SIZE || packet[2] != P2P_RELAY_VERSION || packet[P2P_RELAY_ROLE_OFFSET] > 1)
	{
		relay->statistics.ignored++;
		return;
	}
	for (index = P2P_RELAY_ALLOCATION_OFFSET + P2P_RELAY_ALLOCATION_SIZE; index < size; index++)
	{
		if (packet[index])
		{
			relay->statistics.ignored++;
			return;
		}
	}
	/* (a request without a valid cookie: one, which is smaller, and nothing
	kept; few a second, from anyone) */
	if (!cookie_valid(relay, packet + P2P_RELAY_COOKIE_OFFSET, address, port, now))
	{
		uint8_t answer[P2P_RELAY_COOKIE_MESSAGE_SIZE];

		if (!bucket_take(&relay->cookie_tokens, &relay->cookie_time, relay->config.cookie_rate,
			relay->config.cookie_rate, 1, now))
		{
			relay->statistics.dropped++;
			return;
		}
		answer[0] = P2P_RELAY_MAGIC;
		answer[1] = _relay_cookie;
		answer[2] = P2P_RELAY_VERSION;
		answer[3] = 0;
		memcpy(answer + 4, nonce, P2P_RELAY_NONCE_SIZE);
		relay_cookie(relay, address, port, now / COOKIE_PERIOD, answer + 4 + P2P_RELAY_NONCE_SIZE);
		relay->statistics.cookies++;
		relay->send(relay->context, address, port, answer, sizeof(answer));
		return;
	}
	role = packet[P2P_RELAY_ROLE_OFFSET];
	address_tag(relay, address, tag);
	allocation = allocation_find(relay, id);
	if (!allocation)
	{
		if (address_sides(relay, address) >= relay->config.maximum_per_address ||
			!(allocation = allocation_new(relay, id, now)))
		{
			relay->statistics.refused++;
			send_allocated(relay, NULL, P2P_RELAY_BUSY, nonce, address, port, now);
			return;
		}
		log_line(relay, "allocation %u opened by %s (%s side); %d open",
			(unsigned)(allocation - relay->allocations), tag, role ? "joiner's" : "host's",
			relay->allocation_count);
	}
	side = &allocation->sides[role];
	other = &allocation->sides[!role];
	was_ready = side->used && other->used;
	if (!side_is(side, address, port))
	{
		/* (the same address on both sides is no pair of machines) */
		if (side_is(other, address, port))
		{
			relay->statistics.refused++;
			send_allocated(relay, NULL, P2P_RELAY_TAKEN, nonce, address, port, now);
			return;
		}
		if (side->used && since(now, side->heard_time) < relay->config.rebind_time)
		{
			relay->statistics.refused++;
			send_allocated(relay, NULL, P2P_RELAY_TAKEN, nonce, address, port, now);
			return;
		}
		if ((!side->used || side->address != address) &&
			address_sides(relay, address) >= relay->config.maximum_per_address)
		{
			relay->statistics.refused++;
			send_allocated(relay, NULL, P2P_RELAY_BUSY, nonce, address, port, now);
			if (!other->used && !side->used)
				allocation_close(relay, allocation, "no room for its address", now);
			return;
		}
		if (side->used)
			log_line(relay, "allocation %u: its %s side moved to %s", (unsigned)(allocation - relay->allocations),
				role ? "joiner's" : "host's", tag);
		side->used = 1;
		side->address = address;
		side->port = port;
	}
	memcpy(side->nonce, nonce, P2P_RELAY_NONCE_SIZE);
	side->heard_time = now;
	if (!other->used)
	{
		send_allocated(relay, allocation, P2P_RELAY_WAITING, nonce, address, port, now);
		return;
	}
	send_allocated(relay, allocation, P2P_RELAY_READY, nonce, address, port, now);
	if (!was_ready)
	{
		/* (the machine that waited learns at once) */
		if (!allocation->ready_time)
		{
			allocation->ready_time = now;
			relay->statistics.allocations_ready++;
		}
		log_line(relay, "allocation %u ready: %s joined it (%s side)", (unsigned)(allocation - relay->allocations),
			tag, role ? "joiner's" : "host's");
		send_allocated(relay, allocation, P2P_RELAY_READY, other->nonce, other->address, other->port, now);
	}
}

/* a DATA message: to the allocation's other side, as it is */
static void data_received(struct relay *relay, const uint8_t *packet, size_t size, uint32_t address, uint16_t port,
	uint64_t now)
{
	uint32_t channel;
	struct relay_allocation *allocation;
	struct relay_side *from, *to;

	if (size < P2P_RELAY_DATA_HEADER_SIZE + P2P_RELAY_MINIMUM_TUNNEL_PACKET || size > P2P_RELAY_MAXIMUM_DATA_SIZE ||
		packet[P2P_RELAY_DATA_HEADER_SIZE] != P2P_TUNNEL_MAGIC)
	{
		relay->statistics.ignored++;
		return;
	}
	channel = get_long(packet + 2);
	allocation = &relay->allocations[channel & ((1U << RELAY_INDEX_BITS) - 1)];
	if ((channel & ((1U << RELAY_INDEX_BITS) - 1)) >= (uint32_t)relay->config.maximum_allocations ||
		!allocation->used || allocation->channel != channel || !allocation->sides[0].used ||
		!allocation->sides[1].used)
	{
		relay->statistics.ignored++;
		return;
	}
	if (side_is(&allocation->sides[0], address, port))
	{
		from = &allocation->sides[0];
		to = &allocation->sides[1];
	}
	else if (side_is(&allocation->sides[1], address, port))
	{
		from = &allocation->sides[1];
		to = &allocation->sides[0];
	}
	else
	{
		relay->statistics.ignored++;
		return;
	}
	from->heard_time = now;
	/* (over the rate: dropped, as a full link drops; the tunnel's streams
	send again) */
	if (!bucket_take(&allocation->packet_tokens, &allocation->packet_time, relay->config.packet_rate,
		relay->config.packet_rate, 1, now) ||
		!bucket_take(&allocation->byte_tokens, &allocation->byte_time, relay->config.rate, relay->config.burst, size,
		now) ||
		(relay->config.total_rate && !bucket_take(&relay->total_tokens, &relay->total_time, relay->config.total_rate,
		relay->config.total_rate, size, now)))
	{
		allocation->dropped++;
		relay->statistics.dropped++;
		return;
	}
	from->bytes += size;
	from->packets++;
	relay->statistics.bytes += size;
	relay->statistics.packets++;
	relay->send(relay->context, to->address, to->port, packet, size);
	if (relay->config.maximum_bytes && allocation->sides[0].bytes + allocation->sides[1].bytes >=
		relay->config.maximum_bytes)
	{
		allocation_close(relay, allocation, "its byte limit", now);
	}
}

void relay_received(struct relay *relay, const uint8_t *packet, size_t size, uint32_t address, uint16_t port,
	uint64_t now)
{
	if (size < 2 || packet[0] != P2P_RELAY_MAGIC || !port)
	{
		relay->statistics.ignored++;
		return;
	}
	switch (packet[1])
	{
	case _relay_allocate:
		allocate_received(relay, packet, size, address, port, now);
		break;
	case _relay_data:
		data_received(relay, packet, size, address, port, now);
		break;
	default:
		/* (COOKIE and ALLOCATED are the relay's own: never answered) */
		relay->statistics.ignored++;
		break;
	}
}

void relay_expire(struct relay *relay, uint64_t now)
{
	int index;

	for (index = 0; index < relay->config.maximum_allocations; index++)
	{
		struct relay_allocation *allocation = &relay->allocations[index];
		const struct relay_side *host = &allocation->sides[0], *joiner = &allocation->sides[1];

		if (!allocation->used)
			continue;
		if (since(now, allocation->created_time) >= relay->config.lifetime)
			allocation_close(relay, allocation, "its longest life", now);
		else if (host->used && joiner->used)
		{
			uint64_t heard = host->heard_time > joiner->heard_time ? host->heard_time : joiner->heard_time;

			if (since(now, heard) >= relay->config.idle_time)
				allocation_close(relay, allocation, "idle", now);
		}
		else if (since(now, host->used ? host->heard_time : joiner->heard_time) >= relay->config.half_open_time)
			allocation_close(relay, allocation, "the other machine never came", now);
	}
}

int relay_initialize(struct relay *relay, const struct relay_config *config, const uint8_t *secret,
	relay_send_function send, relay_log_function log, void *context)
{
	memset(relay, 0, sizeof(*relay));
	if (config->maximum_allocations < 1 || config->maximum_allocations > RELAY_MAXIMUM_ALLOCATIONS ||
		config->maximum_per_address < 1 || !config->rate || !config->burst || config->burst < P2P_RELAY_MAXIMUM_DATA_SIZE ||
		!config->packet_rate || !config->cookie_rate || !config->lifetime || !config->idle_time ||
		!config->half_open_time || config->rate > 1000000000ULL || config->burst > 1000000000ULL ||
		config->total_rate > 1000000000ULL || config->packet_rate > 1000000 || config->cookie_rate > 1000000)
	{
		return 0;
	}
	relay->config = *config;
	relay->allocations = calloc((size_t)config->maximum_allocations, sizeof(*relay->allocations));
	if (!relay->allocations)
		return 0;
	memcpy(relay->cookie_key, secret, RELAY_KEY_SIZE);
	memcpy(relay->log_key, secret + RELAY_KEY_SIZE, RELAY_KEY_SIZE);
	relay->channel_counter = get_long(secret);
	relay->cookie_tokens = config->cookie_rate;
	relay->total_tokens = config->total_rate;
	relay->send = send;
	relay->log = log;
	relay->context = context;
	return 1;
}

void relay_release(struct relay *relay)
{
	free(relay->allocations);
	crypto_wipe(relay->cookie_key, sizeof(relay->cookie_key));
	crypto_wipe(relay->log_key, sizeof(relay->log_key));
	memset(relay, 0, sizeof(*relay));
}
