/*
RELAY_TEST.C

The relay's rules (port/relay/relay.c), driven as the network would:
cookies, pairing, forwarding, the sides' binding, the limits, the rates,
lapsing, and that nothing it sends anyone is larger than what that one
sent (no amplification) or goes to an address that did not show it
receives there. Run by port/vita/tests/run_relay_test.sh, also with ASan
and UBSan.
*/

#include "../relay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
	failures++; } } while (0)

/* what the relay sent, last first */
struct sent
{
	uint32_t address;
	uint16_t port;
	uint8_t data[2048];
	size_t size;
};

static struct sent sent[16];
static int sent_count;
/* the request being handled: its source and size (sends back to it may be
no larger) */
static uint32_t request_address;
static uint16_t request_port;
static size_t request_size;
/* addresses that showed they receive (sent a request with their cookie) */
static struct { uint32_t address; uint16_t port; } verified[64];
static int verified_count;

static int is_verified(uint32_t address, uint16_t port)
{
	int index;

	for (index = 0; index < verified_count; index++)
		if (verified[index].address == address && verified[index].port == port)
			return 1;
	return 0;
}

static void on_send(void *context, uint32_t address, uint16_t port, const uint8_t *data, size_t size)
{
	(void)context;
	CHECK(size <= sizeof(sent[0].data));
	if (address == request_address && port == request_port)
		CHECK(size <= request_size);
	else
		CHECK(is_verified(address, port));
	if (sent_count < (int)(sizeof(sent) / sizeof(sent[0])))
	{
		sent[sent_count].address = address;
		sent[sent_count].port = port;
		memcpy(sent[sent_count].data, data, size);
		sent[sent_count].size = size;
		sent_count++;
	}
}

static void on_log(void *context, const char *line)
{
	(void)context;
	if (getenv("RELAY_TEST_VERBOSE"))
		printf("  log: %s\n", line);
}

static void deliver(struct relay *relay, const uint8_t *packet, size_t size, uint32_t address, uint16_t port,
	uint64_t now)
{
	sent_count = 0;
	request_address = address;
	request_port = port;
	request_size = size;
	relay_received(relay, packet, size, address, port, now);
}

static void make_allocate(uint8_t *packet, int role, uint8_t nonce, const uint8_t *cookie, uint8_t id)
{
	memset(packet, 0, P2P_RELAY_ALLOCATE_SIZE);
	packet[0] = P2P_RELAY_MAGIC;
	packet[1] = _relay_allocate;
	packet[2] = P2P_RELAY_VERSION;
	packet[3] = (uint8_t)role;
	memset(packet + P2P_RELAY_NONCE_OFFSET, nonce, P2P_RELAY_NONCE_SIZE);
	if (cookie)
		memcpy(packet + P2P_RELAY_COOKIE_OFFSET, cookie, P2P_RELAY_COOKIE_SIZE);
	memset(packet + P2P_RELAY_ALLOCATION_OFFSET, id, P2P_RELAY_ALLOCATION_SIZE);
}

/* asks for a cookie, then the allocation with it: the ALLOCATED answer's
status (-1: none), and the channel */
static int allocate(struct relay *relay, uint32_t address, uint16_t port, int role, uint8_t id, uint64_t now,
	uint32_t *channel)
{
	uint8_t packet[P2P_RELAY_ALLOCATE_SIZE];
	uint8_t cookie[P2P_RELAY_COOKIE_SIZE];
	const uint8_t nonce = (uint8_t)(address + port + role);

	make_allocate(packet, role, nonce, NULL, id);
	deliver(relay, packet, sizeof(packet), address, port, now);
	if (sent_count != 1 || sent[0].size != P2P_RELAY_COOKIE_MESSAGE_SIZE || sent[0].data[1] != _relay_cookie)
		return -1;
	CHECK(sent[0].data[P2P_RELAY_NONCE_OFFSET] == nonce);
	memcpy(cookie, sent[0].data + 4 + P2P_RELAY_NONCE_SIZE, sizeof(cookie));
	verified[verified_count].address = address;
	verified[verified_count].port = port;
	verified_count = (verified_count + 1) % 64;
	make_allocate(packet, role, nonce, cookie, id);
	deliver(relay, packet, sizeof(packet), address, port, now);
	if (sent_count < 1 || sent[0].size != P2P_RELAY_ALLOCATED_SIZE || sent[0].data[1] != _relay_allocated)
		return -1;
	CHECK(sent[0].address == address && sent[0].port == port);
	CHECK(sent[0].data[P2P_RELAY_NONCE_OFFSET] == nonce);
	if (channel)
		*channel = (uint32_t)sent[0].data[P2P_RELAY_CHANNEL_OFFSET] << 24 |
			(uint32_t)sent[0].data[P2P_RELAY_CHANNEL_OFFSET + 1] << 16 |
			(uint32_t)sent[0].data[P2P_RELAY_CHANNEL_OFFSET + 2] << 8 | sent[0].data[P2P_RELAY_CHANNEL_OFFSET + 3];
	return sent[0].data[3];
}

static size_t make_data(uint8_t *packet, uint32_t channel, size_t size, uint8_t fill)
{
	memset(packet, fill, size);
	packet[0] = P2P_RELAY_MAGIC;
	packet[1] = _relay_data;
	packet[2] = (uint8_t)(channel >> 24);
	packet[3] = (uint8_t)(channel >> 16);
	packet[4] = (uint8_t)(channel >> 8);
	packet[5] = (uint8_t)channel;
	packet[6] = P2P_TUNNEL_MAGIC;
	return size;
}

static void start(struct relay *relay, const struct relay_config *config)
{
	uint8_t secret[RELAY_KEY_SIZE * 2];
	int index;

	for (index = 0; index < (int)sizeof(secret); index++)
		secret[index] = (uint8_t)(index * 7 + 1);
	CHECK(relay_initialize(relay, config, secret, on_send, on_log, NULL));
	verified_count = 0;
}

enum
{
	HOST = 0x0201A8C0,     /* 192.168.1.2 */
	JOINER = 0x0202A8C0,   /* 192.168.2.2 */
	STRANGER = 0x0909A8C0, /* 192.168.9.9 */
	PORT_A = 0x3930,
	PORT_B = 0x3A30,
};

static void test_cookie_and_malformed(void)
{
	struct relay_config config;
	struct relay relay;
	uint8_t packet[P2P_RELAY_ALLOCATE_SIZE + 8];
	uint8_t cookie[P2P_RELAY_COOKIE_SIZE];

	relay_default_config(&config);
	start(&relay, &config);
	/* a request without a cookie: a cookie, smaller; nothing kept */
	make_allocate(packet, 0, 1, NULL, 9);
	deliver(&relay, packet, P2P_RELAY_ALLOCATE_SIZE, HOST, PORT_A, 1000);
	CHECK(sent_count == 1 && sent[0].size == P2P_RELAY_COOKIE_MESSAGE_SIZE);
	CHECK(sent[0].size < P2P_RELAY_ALLOCATE_SIZE);
	CHECK(relay_open_allocations(&relay) == 0);
	/* wrong sizes, version, role, padding, magic, types: no answer */
	deliver(&relay, packet, P2P_RELAY_ALLOCATE_SIZE - 1, HOST, PORT_A, 1000);
	CHECK(sent_count == 0);
	deliver(&relay, packet, P2P_RELAY_ALLOCATE_SIZE + 1, HOST, PORT_A, 1000);
	CHECK(sent_count == 0);
	packet[2] = 2;
	deliver(&relay, packet, P2P_RELAY_ALLOCATE_SIZE, HOST, PORT_A, 1000);
	CHECK(sent_count == 0);
	make_allocate(packet, 2, 1, NULL, 9);
	deliver(&relay, packet, P2P_RELAY_ALLOCATE_SIZE, HOST, PORT_A, 1000);
	CHECK(sent_count == 0);
	make_allocate(packet, 0, 1, NULL, 9);
	packet[P2P_RELAY_ALLOCATE_SIZE - 1] = 1;
	deliver(&relay, packet, P2P_RELAY_ALLOCATE_SIZE, HOST, PORT_A, 1000);
	CHECK(sent_count == 0);
	make_allocate(packet, 0, 1, NULL, 9);
	packet[0] = P2P_TUNNEL_MAGIC;
	deliver(&relay, packet, P2P_RELAY_ALLOCATE_SIZE, HOST, PORT_A, 1000);
	CHECK(sent_count == 0);
	for (int type = 0; type < 256; type++)
	{
		make_allocate(packet, 0, 1, NULL, 9);
		packet[1] = (uint8_t)type;
		if (type == _relay_allocate)
			continue;
		deliver(&relay, packet, P2P_RELAY_ALLOCATE_SIZE, HOST, PORT_A, 1000);
		CHECK(sent_count == 0);
	}
	deliver(&relay, packet, 0, HOST, PORT_A, 1000);
	CHECK(sent_count == 0);
	/* another address's cookie is no cookie (a new one comes back) */
	relay_cookie(&relay, STRANGER, PORT_A, 1000 / 60000, cookie);
	make_allocate(packet, 0, 1, cookie, 9);
	deliver(&relay, packet, P2P_RELAY_ALLOCATE_SIZE, HOST, PORT_A, 1000);
	CHECK(sent_count == 1 && sent[0].data[1] == _relay_cookie);
	CHECK(relay_open_allocations(&relay) == 0);
	/* a cookie lasts its period and the next, not longer */
	relay_cookie(&relay, HOST, PORT_A, 0, cookie);
	make_allocate(packet, 0, 1, cookie, 9);
	deliver(&relay, packet, P2P_RELAY_ALLOCATE_SIZE, HOST, PORT_A, 119999);
	CHECK(sent_count == 1 && sent[0].data[1] == _relay_allocated && sent[0].data[3] == P2P_RELAY_WAITING);
	make_allocate(packet, 0, 1, cookie, 10);
	deliver(&relay, packet, P2P_RELAY_ALLOCATE_SIZE, HOST, PORT_A, 120000);
	CHECK(sent_count == 1 && sent[0].data[1] == _relay_cookie);
	relay_release(&relay);
}

static void test_pairing_and_forwarding(void)
{
	struct relay_config config;
	struct relay relay;
	uint8_t packet[P2P_RELAY_MAXIMUM_DATA_SIZE + 8];
	uint32_t channel, joiner_channel, stranger_channel;
	size_t size;

	relay_default_config(&config);
	start(&relay, &config);
	CHECK(allocate(&relay, HOST, PORT_A, 0, 1, 1000, &channel) == P2P_RELAY_WAITING);
	CHECK(channel != 0);
	/* DATA before the other side came: dropped */
	size = make_data(packet, channel, 100, 0x55);
	deliver(&relay, packet, size, HOST, PORT_A, 1100);
	CHECK(sent_count == 0);
	/* the joiner: ready, and the host is told (with its nonce) */
	CHECK(allocate(&relay, JOINER, PORT_B, 1, 1, 1200, &joiner_channel) == P2P_RELAY_READY);
	CHECK(joiner_channel == channel);
	CHECK(sent_count == 2 && sent[1].address == HOST && sent[1].port == PORT_A && sent[1].data[3] == P2P_RELAY_READY);
	CHECK(sent[1].data[P2P_RELAY_NONCE_OFFSET] == (uint8_t)(HOST + PORT_A));
	/* forwarded as it came, each way */
	size = make_data(packet, channel, 300, 0x5A);
	deliver(&relay, packet, size, HOST, PORT_A, 1300);
	CHECK(sent_count == 1 && sent[0].address == JOINER && sent[0].port == PORT_B && sent[0].size == size &&
		!memcmp(sent[0].data, packet, size));
	deliver(&relay, packet, size, JOINER, PORT_B, 1300);
	CHECK(sent_count == 1 && sent[0].address == HOST && sent[0].port == PORT_A && sent[0].size == size);
	/* not from another address, port, channel; not a tunnel packet; not too
	short or long */
	deliver(&relay, packet, size, STRANGER, PORT_A, 1300);
	CHECK(sent_count == 0);
	deliver(&relay, packet, size, HOST, PORT_B, 1300);
	CHECK(sent_count == 0);
	size = make_data(packet, channel ^ 0x1000, 300, 0x5A);
	deliver(&relay, packet, size, HOST, PORT_A, 1300);
	CHECK(sent_count == 0);
	size = make_data(packet, channel, 300, 0x5A);
	packet[6] = 0x01;
	deliver(&relay, packet, size, HOST, PORT_A, 1300);
	CHECK(sent_count == 0);
	size = make_data(packet, channel, P2P_RELAY_DATA_HEADER_SIZE + P2P_RELAY_MINIMUM_TUNNEL_PACKET - 1, 0);
	deliver(&relay, packet, size, HOST, PORT_A, 1300);
	CHECK(sent_count == 0);
	size = make_data(packet, channel, P2P_RELAY_MAXIMUM_DATA_SIZE + 1, 0);
	deliver(&relay, packet, size, HOST, PORT_A, 1300);
	CHECK(sent_count == 0);
	size = make_data(packet, channel, P2P_RELAY_MAXIMUM_DATA_SIZE, 0);
	deliver(&relay, packet, size, HOST, PORT_A, 1300);
	CHECK(sent_count == 1 && sent[0].size == P2P_RELAY_MAXIMUM_DATA_SIZE);
	/* a stranger asking for the same allocation's side while it is heard:
	taken; the same address on both sides: taken */
	CHECK(allocate(&relay, STRANGER, PORT_A, 0, 1, 2000, &stranger_channel) == P2P_RELAY_TAKEN);
	CHECK(allocate(&relay, HOST, PORT_A, 1, 1, 2000, NULL) == P2P_RELAY_TAKEN);
	/* asking again from its own address: still ready, the same channel */
	CHECK(allocate(&relay, HOST, PORT_A, 0, 1, 3000, &stranger_channel) == P2P_RELAY_READY);
	CHECK(stranger_channel == channel);
	/* the host silent past rebind_time (its NAT's port changed): its side
	moves to its new address */
	deliver(&relay, packet, size, JOINER, PORT_B, 10000);
	CHECK(allocate(&relay, HOST, PORT_B, 0, 1, 3000 + config.rebind_time, NULL) == P2P_RELAY_READY);
	deliver(&relay, packet, size, JOINER, PORT_B, 3000 + config.rebind_time);
	CHECK(sent_count == 1 && sent[0].address == HOST && sent[0].port == PORT_B);
	deliver(&relay, packet, size, HOST, PORT_A, 3000 + config.rebind_time);
	CHECK(sent_count == 0);
	relay_release(&relay);
}

static void test_limits(void)
{
	struct relay_config config;
	struct relay relay;
	int index;

	relay_default_config(&config);
	config.maximum_allocations = 8;
	config.maximum_per_address = 3;
	start(&relay, &config);
	/* one address in 3 allocations, not a fourth */
	for (index = 0; index < 3; index++)
		CHECK(allocate(&relay, HOST, PORT_A, 0, (uint8_t)(10 + index), 1000, NULL) == P2P_RELAY_WAITING);
	CHECK(allocate(&relay, HOST, PORT_A, 0, 20, 1000, NULL) == P2P_RELAY_BUSY);
	/* nor on the other side of another's */
	CHECK(allocate(&relay, JOINER, PORT_A, 0, 30, 1000, NULL) == P2P_RELAY_WAITING);
	CHECK(allocate(&relay, HOST, PORT_B, 1, 30, 1000, NULL) == P2P_RELAY_BUSY);
	CHECK(relay_open_allocations(&relay) == 4);
	/* 8 in all */
	for (index = 0; index < 4; index++)
		CHECK(allocate(&relay, (uint32_t)(STRANGER + (index << 24)), PORT_A, 0, (uint8_t)(40 + index), 1000, NULL) ==
			P2P_RELAY_WAITING);
	CHECK(allocate(&relay, (uint32_t)(STRANGER + (5 << 24)), PORT_A, 0, 50, 1000, NULL) == P2P_RELAY_BUSY);
	CHECK(relay_open_allocations(&relay) == 8);
	/* waiting alone lapses */
	relay_expire(&relay, 1000 + config.half_open_time - 1);
	CHECK(relay_open_allocations(&relay) == 8);
	relay_expire(&relay, 1000 + config.half_open_time);
	CHECK(relay_open_allocations(&relay) == 0);
	relay_release(&relay);
}

static void test_rates_and_lapsing(void)
{
	struct relay_config config;
	struct relay relay;
	uint8_t packet[P2P_RELAY_MAXIMUM_DATA_SIZE];
	uint32_t channel;
	size_t size;
	int index, forwarded = 0;

	relay_default_config(&config);
	config.rate = 100000;
	config.burst = 10000;
	config.packet_rate = 50;
	config.maximum_bytes = 30000;
	start(&relay, &config);
	CHECK(allocate(&relay, HOST, PORT_A, 0, 1, 1000, NULL) == P2P_RELAY_WAITING);
	CHECK(allocate(&relay, JOINER, PORT_B, 1, 1, 1000, &channel) == P2P_RELAY_READY);
	/* a burst of 10000 bytes at once, then the rate */
	size = make_data(packet, channel, 1000, 0x11);
	for (index = 0; index < 20; index++)
	{
		deliver(&relay, packet, size, HOST, PORT_A, 1000);
		forwarded += sent_count;
	}
	CHECK(forwarded == 10);
	deliver(&relay, packet, size, HOST, PORT_A, 1010);
	CHECK(sent_count == 1);
	/* the packets' rate: 50 a second */
	size = make_data(packet, channel, 40, 0x11);
	forwarded = 0;
	for (index = 0; index < 100; index++)
	{
		deliver(&relay, packet, size, JOINER, PORT_B, 2000);
		forwarded += sent_count;
	}
	CHECK(forwarded > 0 && forwarded <= 50);
	/* the byte limit closes it */
	size = make_data(packet, channel, 1000, 0x11);
	for (index = 0; index < 60 && relay_open_allocations(&relay); index++)
		deliver(&relay, packet, size, HOST, PORT_A, 3000 + (uint64_t)index * 100);
	CHECK(relay_open_allocations(&relay) == 0);
	relay_release(&relay);

	/* idle, and the longest life */
	relay_default_config(&config);
	start(&relay, &config);
	CHECK(allocate(&relay, HOST, PORT_A, 0, 1, 1000, NULL) == P2P_RELAY_WAITING);
	CHECK(allocate(&relay, JOINER, PORT_B, 1, 1, 1000, &channel) == P2P_RELAY_READY);
	size = make_data(packet, channel, 100, 0x11);
	deliver(&relay, packet, size, HOST, PORT_A, 50000);
	relay_expire(&relay, 50000 + config.idle_time - 1);
	CHECK(relay_open_allocations(&relay) == 1);
	relay_expire(&relay, 50000 + config.idle_time);
	CHECK(relay_open_allocations(&relay) == 0);
	CHECK(allocate(&relay, HOST, PORT_A, 0, 2, 1000, NULL) == P2P_RELAY_WAITING);
	CHECK(allocate(&relay, JOINER, PORT_B, 1, 2, 1000, &channel) == P2P_RELAY_READY);
	for (uint64_t now = 1000; now < 1000 + config.lifetime; now += 30000)
	{
		deliver(&relay, packet, make_data(packet, channel, 100, 0x11), HOST, PORT_A, now);
		relay_expire(&relay, now);
	}
	CHECK(relay_open_allocations(&relay) == 1);
	relay_expire(&relay, 1000 + config.lifetime);
	CHECK(relay_open_allocations(&relay) == 0);
	relay_release(&relay);
}

static void test_cookie_rate(void)
{
	struct relay_config config;
	struct relay relay;
	uint8_t packet[P2P_RELAY_ALLOCATE_SIZE];
	int index, answered = 0;

	relay_default_config(&config);
	config.cookie_rate = 100;
	start(&relay, &config);
	make_allocate(packet, 0, 1, NULL, 1);
	for (index = 0; index < 1000; index++)
	{
		deliver(&relay, packet, sizeof(packet), (uint32_t)(HOST + index), PORT_A, 5000);
		answered += sent_count;
	}
	CHECK(answered == 100);
	deliver(&relay, packet, sizeof(packet), HOST, PORT_A, 6000);
	CHECK(sent_count == 1);
	relay_release(&relay);
}

int main(void)
{
	test_cookie_and_malformed();
	test_pairing_and_forwarding();
	test_limits();
	test_rates_and_lapsing();
	test_cookie_rate();
	if (failures)
	{
		printf("relay test: %d failures\n", failures);
		return 1;
	}
	printf("relay test: all passed\n");
	return 0;
}
