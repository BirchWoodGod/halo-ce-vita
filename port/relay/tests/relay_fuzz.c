/*
RELAY_FUZZ.C

A libFuzzer target for the relay (port/relay/relay.c), built with ASan and
UBSan by port/vita/tests/run_relay_test.sh. An input is a list of records,
each a kind byte, a source byte (one of 8 addresses and ports) and the
record's bytes up to the next record (a length byte first):
  0  a datagram from the source, as it is
  1  an ALLOCATE from the source with the cookie it was given (so pairing,
     binding and limits are reached): the role, nonce and one of 4
     allocations from the bytes
  2  a DATA message from the source on one of the channels handed out, the
     bytes its tunnel packet
  3  the clock moves on (the first byte, in seconds, times a second byte)
  4  the relay's lapsing pass
Each input starts from a new relay with small limits. Checked on every send:
nothing to the source larger than what it sent; nothing to any other address
that did not first send a request with its cookie; DATA forwarded only as it
came (its size and bytes).
*/

#include "../relay.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t *request;
static size_t request_size;
static uint32_t request_address;
static uint16_t request_port;
static uint8_t verified[8];
static uint32_t channels[16];
static int channel_count;

static uint32_t source_address(int source)
{
	return 0x0100000A + ((uint32_t)(source & 3) << 24);
}

static uint16_t source_port(int source)
{
	return (uint16_t)(0x1000 + (source >> 2));
}

static int source_of(uint32_t address, uint16_t port)
{
	int source;

	for (source = 0; source < 8; source++)
		if (source_address(source) == address && source_port(source) == port)
			return source;
	return -1;
}

static void on_send(void *context, uint32_t address, uint16_t port, const uint8_t *data, size_t size)
{
	int source = source_of(address, port);

	(void)context;
	if (source < 0)
		abort();
	if (address == request_address && port == request_port)
	{
		if (size > request_size)
			abort();
	}
	else if (!verified[source])
		abort();
	if (size >= 2 && data[0] == P2P_RELAY_MAGIC && data[1] == _relay_data &&
		(size != request_size || memcmp(data, request, size)))
	{
		abort();
	}
	if (size == P2P_RELAY_ALLOCATED_SIZE && data[1] == _relay_allocated && data[3] != P2P_RELAY_BUSY &&
		data[3] != P2P_RELAY_TAKEN && channel_count < 16)
	{
		channels[channel_count++] = (uint32_t)data[P2P_RELAY_CHANNEL_OFFSET] << 24 |
			(uint32_t)data[P2P_RELAY_CHANNEL_OFFSET + 1] << 16 | (uint32_t)data[P2P_RELAY_CHANNEL_OFFSET + 2] << 8 |
			data[P2P_RELAY_CHANNEL_OFFSET + 3];
	}
}

static void deliver(struct relay *relay, const uint8_t *packet, size_t size, int source, uint64_t now)
{
	request = packet;
	request_size = size;
	request_address = source_address(source);
	request_port = source_port(source);
	relay_received(relay, packet, size, request_address, request_port, now);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct relay_config config;
	struct relay relay;
	uint8_t secret[RELAY_KEY_SIZE * 2];
	uint64_t now = 1000;
	size_t offset = 0;

	memset(secret, 0x42, sizeof(secret));
	relay_default_config(&config);
	config.maximum_allocations = 4;
	config.maximum_per_address = 2;
	config.rate = 20000;
	config.burst = 4000;
	config.packet_rate = 20;
	config.maximum_bytes = 50000;
	config.cookie_rate = 10;
	if (!relay_initialize(&relay, &config, secret, on_send, NULL, NULL))
		abort();
	memset(verified, 0, sizeof(verified));
	channel_count = 0;
	while (offset + 3 <= size)
	{
		int kind = data[offset] % 5;
		int source = data[offset + 1] & 7;
		size_t length = data[offset + 2];
		const uint8_t *bytes = data + offset + 3;
		uint8_t packet[P2P_RELAY_MAXIMUM_DATA_SIZE + 64];

		if (length > size - offset - 3)
			length = size - offset - 3;
		offset += 3 + length;
		switch (kind)
		{
		case 0:
		{
			/* (a copy, as a datagram is the relay's own buffer) */
			uint8_t *copy = malloc(length ? length : 1);

			memcpy(copy, bytes, length);
			deliver(&relay, copy, length, source, now);
			free(copy);
			break;
		}
		case 1:
			memset(packet, 0, P2P_RELAY_ALLOCATE_SIZE);
			packet[0] = P2P_RELAY_MAGIC;
			packet[1] = _relay_allocate;
			packet[2] = P2P_RELAY_VERSION;
			packet[3] = length > 0 ? bytes[0] & 1 : 0;
			if (length > 1)
				memset(packet + P2P_RELAY_NONCE_OFFSET, bytes[1], P2P_RELAY_NONCE_SIZE);
			memset(packet + P2P_RELAY_ALLOCATION_OFFSET, length > 2 ? bytes[2] & 3 : 0, P2P_RELAY_ALLOCATION_SIZE);
			relay_cookie(&relay, source_address(source), source_port(source), now / 60000,
				packet + P2P_RELAY_COOKIE_OFFSET);
			verified[source] = 1;
			deliver(&relay, packet, P2P_RELAY_ALLOCATE_SIZE, source, now);
			break;
		case 2:
		{
			uint32_t channel = channel_count && length ? channels[bytes[0] % channel_count] : 0;
			size_t tunnel = length > 1 ? length - 1 : 0;

			packet[0] = P2P_RELAY_MAGIC;
			packet[1] = _relay_data;
			packet[2] = (uint8_t)(channel >> 24);
			packet[3] = (uint8_t)(channel >> 16);
			packet[4] = (uint8_t)(channel >> 8);
			packet[5] = (uint8_t)channel;
			if (tunnel)
				memcpy(packet + P2P_RELAY_DATA_HEADER_SIZE, bytes + 1, tunnel);
			/* (the tunnel's magic usually, and a size often past the least) */
			if (tunnel < P2P_RELAY_MINIMUM_TUNNEL_PACKET && length && (bytes[0] & 0x80))
			{
				memset(packet + P2P_RELAY_DATA_HEADER_SIZE + tunnel, 0, P2P_RELAY_MINIMUM_TUNNEL_PACKET - tunnel);
				tunnel = P2P_RELAY_MINIMUM_TUNNEL_PACKET;
			}
			if (tunnel && !(length && (bytes[0] & 0x40)))
				packet[P2P_RELAY_DATA_HEADER_SIZE] = P2P_TUNNEL_MAGIC;
			deliver(&relay, packet, P2P_RELAY_DATA_HEADER_SIZE + tunnel, source, now);
			break;
		}
		case 3:
			now += (uint64_t)(length > 0 ? bytes[0] : 1) * (length > 1 ? bytes[1] : 1) * 1000;
			break;
		case 4:
			relay_expire(&relay, now);
			break;
		}
		if (relay_open_allocations(&relay) < 0 || relay_open_allocations(&relay) > config.maximum_allocations)
			abort();
	}
	relay_release(&relay);
	return 0;
}
