/*
P2P_SIGNAL_FUZZ.C

A fuzzer of what a signalling broker sends (port/linux/src/p2p_signal.c,
which it includes): MQTT 5 and 3.1.1 packets as a broker might send them,
whole streams of them, cut anywhere, with ASan and UBSan
(run_p2p_lobby_test.sh signal). The brokers are other people's, so all of
this is untrusted: CONNACKs with any properties, PUBLISHes of any QoS,
topic and properties to any topic (the slots and queries of the server
browser, a host's or a joiner's or a code's topic), PUBACKs, DISCONNECTs.
The input's first byte says how:

	0-2  the rest as the stream, on a broker awaiting its CONNACK (MQTT 5),
	     ready (MQTT 5), or ready (3.1.1)
	3    the rest as a CONNACK's properties (MQTT 5)
	4    the rest sealed with the host's key, as a PUBLISH on its topic: a
	     JOIN, as one with the invite could send it
	5    the same with the joiner's key, on its topic: an ACCEPT
	6    the rest as a PUBLISH on a slot of the server browser (its listing
	     goes to p2p_lobby_slot_heard's stand-in, which checks its bounds)

Its own engine (the platform layer is 32-bit only, and the host's clang has
no 32-bit libFuzzer); -DUSE_LIBFUZZER builds LLVMFuzzerTestOneInput alone.
*/

#include "../../linux/src/p2p_signal.c"

#include <stdint.h>
#include <stdarg.h>

/* (the XDK headers' inline functions' state, never used) */
DWORD D3D__RenderState[D3DRS_MAX];

/* ---------- stand-ins */

static unsigned long clock_now = 100000;
static unsigned char x25519_secret[P2P_KEY_SIZE] = { 1, 2, 3 }, x25519_public[P2P_KEY_SIZE];
static unsigned char identifier[P2P_IDENTIFIER_SIZE] = { 2, 1, 1, 1, 1, 1 };
static unsigned long slot_payloads, queries, offers, codes_found;

void posix_random_bytes(void *buffer, unsigned long size)
{
	static unsigned char counter;
	unsigned char *bytes = buffer;
	unsigned long index;

	for (index = 0; index < size; index++)
		bytes[index] = (unsigned char)(counter++ * 13 + 1);
}
unsigned long p2p_now(void) { return clock_now; }
const char *config_string(const char *name) { (void)name; return ""; }
void config_folder(char *path, size_t size) { snprintf(path, size, "./"); }
char *config_file_read(const char *path, size_t *size) { (void)path; (void)size; return NULL; }
void platform_log(const char *format, ...) { (void)format; }
unsigned long p2p_resolve(const char *host) { (void)host; return 0x0100007F; }
int posix_socket(int family, int type, int protocol) { (void)family; (void)type; (void)protocol; return 3; }
int posix_socket_close(int socket) { (void)socket; return 0; }
int posix_socket_connect(int socket, const void *address, int length) { (void)socket; (void)address; (void)length; return 0; }
int posix_socket_set_nonblocking(int socket, int on) { (void)socket; (void)on; return 0; }
int posix_socket_last_error(void) { return 0; }
int posix_socket_recv(int socket, void *buffer, int length, int flags)
{
	(void)socket; (void)buffer; (void)length; (void)flags;
	return -1;
}
/* (what goes out is read whole, as a broker reads it) */
int posix_socket_send(int socket, const void *buffer, int length, int flags)
{
	static unsigned char sink[BUFFER_SIZE];

	(void)socket; (void)flags;
	memcpy(sink, buffer, (size_t)(length > BUFFER_SIZE ? BUFFER_SIZE : length));
	return length;
}

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	int index;

	for (index = 0; index < size; index++)
		sprintf(text + 2 * index, "%02x", bytes[index]);
	text[2 * size] = 0;
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
	result[0] = (unsigned char)((result[0] & 0xFC) | 0x02);
}
void p2p_identifier_for(const unsigned char *public_key, unsigned char *result)
{
	unsigned char hash[P2P_KEY_HASH_SIZE];

	p2p_key_hash(public_key, hash);
	p2p_identifier_from_hash(hash, result);
}
const unsigned char *p2p_identifier(void) { return identifier; }
const unsigned char *p2p_public_key(void) { return x25519_public; }
int p2p_shared_secret(const unsigned char *public_key, unsigned char *shared)
{
	p2p_x25519(shared, x25519_secret, public_key);
	return 1;
}
int p2p_local_candidates(struct p2p_candidate *candidates, int maximum_count)
{
	(void)maximum_count;
	candidates[0].address = 0x0100000A;
	candidates[0].port = 0x3412;
	return 1;
}
int p2p_peer_offered(const unsigned char *who, const unsigned char *secret, const struct p2p_candidate *candidates,
	int count, int is_host)
{
	(void)who; (void)secret; (void)is_host;
	if (count < 0 || count > P2P_MAXIMUM_CANDIDATES || (count && !candidates))
		__builtin_trap();
	offers++;
	return 1;
}
int p2p_peer_turned_away(const unsigned char *who, int is_host) { (void)who; (void)is_host; return 0; }
int p2p_local_relays(struct p2p_candidate *relays, int maximum_count)
{
	(void)maximum_count;
	relays[0].address = 0x0200000A;
	relays[0].port = 0x7856;
	return 1;
}
void p2p_peer_relays(const unsigned char *who, const unsigned char *secret, const struct p2p_candidate *relays,
	int count)
{
	(void)who; (void)secret;
	if (count < 0 || count > P2P_MAXIMUM_RELAYS || (count && !relays))
		__builtin_trap();
}
int p2p_peer_reoffered(const unsigned char *who, const unsigned char *secret, const struct p2p_candidate *candidates,
	int count)
{
	(void)who; (void)secret; (void)candidates;
	if (count < 0 || count > P2P_MAXIMUM_CANDIDATES)
		__builtin_trap();
	return 1;
}
void p2p_code_found(const char *text)
{
	if (strlen(text) != 12 + 2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE))
		__builtin_trap();
	codes_found++;
}
void p2p_lobby_slot_heard(const char *hash_text, const unsigned char *payload, int size, int retained)
{
	static unsigned char copy[MAXIMUM_LISTING_SIZE];

	(void)retained;
	/* (a slot's name ends, and its payload is within the listing's most) */
	if (strlen(hash_text) >= TOPIC_SIZE || size < 0 || size > MAXIMUM_LISTING_SIZE)
		__builtin_trap();
	if (size)
		memcpy(copy, payload, (size_t)size);
	slot_payloads++;
}
void p2p_lobby_query_heard(void) { queries++; }
void p2p_lobby_slot_topic(const unsigned char *key_hash, char *topic, int size)
{
	char text[2 * P2P_KEY_HASH_SIZE + 1];

	p2p_hex(key_hash, P2P_KEY_HASH_SIZE, text);
	snprintf(topic, (size_t)size, "%s%s", P2P_LOBBY_SLOT_PREFIX, text);
}

/* ---------- the harness */

static const unsigned char token[P2P_TOKEN_SIZE] = { 5, 4, 3, 2, 1 };

static void setup(void)
{
	static int done;
	unsigned char host_hash[P2P_KEY_HASH_SIZE] = { 9 };

	if (done)
		return;
	done = 1;
	p2p_x25519(x25519_public, x25519_secret, NULL);
	p2p_signal_start();
	signalling.broker_count = 1;
	memset(&signalling.brokers[0], 0, sizeof(signalling.brokers[0]));
	snprintf(signalling.brokers[0].host, sizeof(signalling.brokers[0].host), "broker");
	p2p_signal_host(token, "ABCD-EFGH");
	p2p_signal_join(host_hash, token);
	p2p_signal_lookup_code("ABCDEFGH");
	p2p_signal_lobby_topics(1, 1);
}

/* the broker made anew: connected, in this state and protocol */
static struct broker *fresh_broker(int state, int protocol)
{
	struct broker *broker = &signalling.brokers[0];

	broker_close(broker, 0);
	broker->socket = 3;
	broker->state = state;
	broker->protocol = protocol;
	broker->retain_available = broker->wildcard_available = 1;
	broker->keep_alive = KEEP_ALIVE_SECONDS;
	broker->receive_maximum = MAXIMUM_IN_FLIGHT;
	broker->publish_tokens = PUBLISH_BURST;
	broker->publish_time = p2p_now();
	broker->heard_time = p2p_now();
	if (state == _broker_ready)
		broker_sync_topics(broker);
	return broker;
}

/* a packet into the broker's input: its type, and its body */
static int put_packet(struct broker *broker, unsigned char type, const unsigned char *body, int size)
{
	unsigned char header[5];
	int header_size;

	header[0] = type;
	header_size = 1 + put_variable(header + 1, size);
	if (broker->input_size + header_size + size > BUFFER_SIZE)
		return 0;
	memcpy(broker->input + broker->input_size, header, (size_t)header_size);
	memcpy(broker->input + broker->input_size + header_size, body, (size_t)size);
	broker->input_size += header_size + size;
	return 1;
}

/* a PUBLISH (QoS 0, MQTT 5) of payload to topic */
static void put_publish(struct broker *broker, const char *topic, const unsigned char *payload, int size)
{
	unsigned char body[2 + TOPIC_SIZE + 1 + MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD];
	int body_size = put_string(body, topic);

	body[body_size++] = 0;
	memcpy(body + body_size, payload, (size_t)size);
	put_packet(broker, 0x30, body, body_size + size);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct broker *broker;
	int mode;

	setup();
	if (size < 1)
		return 0;
	mode = data[0] % 7;
	data++;
	size--;
	clock_now += 50;
	switch (mode)
	{
	case 0:
	case 1:
	case 2:
		broker = fresh_broker(mode == 0 ? _broker_awaiting_acknowledgement : _broker_ready, mode == 2 ? 4 : 5);
		broker->input_size = (int)(size > BUFFER_SIZE ? BUFFER_SIZE : size);
		memcpy(broker->input, data, (size_t)broker->input_size);
		break;
	case 3:
	{
		unsigned char body[3 + 256];
		int length = (int)(size > 256 ? 256 : size);

		broker = fresh_broker(_broker_awaiting_acknowledgement, 5);
		body[0] = 0;
		body[1] = length ? data[0] : 0;
		/* (its properties' length as given, or as it is) */
		memcpy(body + 2, data, (size_t)length);
		put_packet(broker, 0x20, body, 2 + length);
		break;
	}
	case 4:
	case 5:
	{
		unsigned char sealed[MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD];
		int length = (int)(size > MAXIMUM_MESSAGE_SIZE ? MAXIMUM_MESSAGE_SIZE : size);

		broker = fresh_broker(_broker_ready, 5);
		length = p2p_seal(mode == 4 ? signalling.host_key : signalling.join_key, data, length, sealed);
		put_publish(broker, mode == 4 ? signalling.host_topic : signalling.join_topic, sealed, length);
		break;
	}
	default:
	{
		char topic[TOPIC_SIZE];
		int length = (int)(size > MAXIMUM_LISTING_SIZE + 8 ? MAXIMUM_LISTING_SIZE + 8 : size);

		broker = fresh_broker(_broker_ready, 5);
		snprintf(topic, sizeof(topic), "%s%.*s", P2P_LOBBY_SLOT_PREFIX, (int)(length > 40 ? 40 : length),
			"0123456789abcdef0123456789abcdef0123456789");
		put_publish(broker, topic, data, length);
		put_publish(broker, P2P_LOBBY_QUERY_TOPIC, data, length > 8 ? 8 : length);
		break;
	}
	}
	broker_parse(broker);
	/* (and what follows: the listing's publishes, the timers) */
	if (broker->state == _broker_ready)
		broker_update_lobby(broker);
	return 0;
}

#ifndef USE_LIBFUZZER
static unsigned long long state = 88172645463325252ULL;

static unsigned int next_random(void)
{
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return (unsigned int)(state >> 11);
}

/* MQTT packets as brokers send them, to mutate */
static int seed_input(unsigned char *input)
{
	static const unsigned char connack5[] = { 0x20, 0x09, 0x00, 0x00, 0x06, 0x25, 0x01, 0x21, 0x00, 0x0A, 0x28 };
	static const unsigned char connack3[] = { 0x20, 0x02, 0x00, 0x00 };
	static const unsigned char refused5[] = { 0x20, 0x03, 0x00, 0x84, 0x00 };
	static const unsigned char puback5[] = { 0x40, 0x04, 0x00, 0x01, 0x97, 0x00 };
	static const unsigned char publish_qos1[] = { 0x32, 0x0C, 0x00, 0x05, 'a', '/', 'b', '/', 'c', 0x00, 0x07, 0x02,
		0x02, 0x00, 'x' };
	static const unsigned char disconnect[] = { 0xE0, 0x02, 0x00, 0x00 };
	static const unsigned char ping[] = { 0xD0, 0x00 };
	static const unsigned char *const packets[] = { connack5, connack3, refused5, puback5, publish_qos1, disconnect,
		ping };
	static const int sizes[] = { sizeof(connack5), sizeof(connack3), sizeof(refused5), sizeof(puback5),
		sizeof(publish_qos1), sizeof(disconnect), sizeof(ping) };
	int size = 1, count = 1 + (int)(next_random() % 4), index;

	input[0] = (unsigned char)next_random();
	for (index = 0; index < count; index++)
	{
		int packet = (int)(next_random() % 7);

		memcpy(input + size, packets[packet], (size_t)sizes[packet]);
		size += sizes[packet];
	}
	/* (a slot's or a sealed message's payload: bytes) */
	if (input[0] % 7 >= 3)
	{
		size = 1 + (int)(next_random() % 600);
		for (index = 1; index < size; index++)
			input[index] = (unsigned char)next_random();
		if (input[0] % 7 == 4 && size > 2)
		{
			/* (a JOIN's start: its tag and version) */
			input[1] = _message_join;
			input[2] = MESSAGE_VERSION;
		}
		else if (input[0] % 7 == 5 && size > 2)
		{
			input[1] = _message_accept;
			input[2] = MESSAGE_VERSION;
		}
	}
	return size;
}

int main(int count, char **arguments)
{
	unsigned long runs = count > 1 ? strtoul(arguments[1], NULL, 10) : 1000000;
	unsigned long run;
	static unsigned char input[BUFFER_SIZE + 64];

	for (run = 0; run < runs; run++)
	{
		int size = seed_input(input), changes = (int)(next_random() % 6), change;

		for (change = 0; change < changes && size > 1; change++)
		{
			int at = 1 + (int)(next_random() % (unsigned int)(size - 1));

			switch (next_random() % 4)
			{
			case 0:
				input[at] ^= (unsigned char)(1 << (next_random() % 8));
				break;
			case 1:
				input[at] = (unsigned char)next_random();
				break;
			case 2:
				size = at;
				break;
			default:
				input[at] = (unsigned char)(next_random() % 2 ? 0xFF : 0x80);
				break;
			}
		}
		LLVMFuzzerTestOneInput(input, (size_t)size);
	}
	printf("PASS: %lu inputs, no sanitizer report; slot payloads %lu, queries %lu, sessions offered %lu\n", runs,
		slot_payloads, queries, offers);
	return 0;
}
#endif
