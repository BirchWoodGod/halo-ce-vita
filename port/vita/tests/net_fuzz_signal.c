/*
NET_FUZZ_SIGNAL.C

A fuzz target for internet play's signalling (port/linux/src/p2p_signal.c,
included here whole for its statics): what anyone who can publish on the
public MQTT brokers can send a machine, which reads the brokers' packets
(MQTT 3.1.1), a host's JOIN requests, a joiner's ACCEPT answers, a code's
record and the public lobby's entries.

Each input starts with the machine connected to one broker, hosting (with a
code, and listed in the lobby), joining another host, looking up a code and
browsing the lobby at once, so that every kind of message is read. An input
is a list of records, each a kind, a 16-bit length and that many bytes:
  0  bytes from the broker, as they come (the framing itself)
  1  a PUBLISH on the host's topic, sealed with its token's key: a JOIN
  2  an ACCEPT from the joined host, sealed and tagged as the real host
     would (its key, the joiner's nonce): the rest is the fuzzer's
  3  a PUBLISH on the code's topic, sealed with the code's key: a record
  4  a lobby entry: the topic's end (its first byte is the length), then
     the entry, unsealed (anyone can send one)
  5  a JOIN proven as a real joiner would (its own key, the host's nonce
     and a tag): the rest, its nonce and addresses, the fuzzer's
  6  the clock moves on (the first byte, in tenths of a second), and a pass
     of the signalling's update runs
Built twice by run_net_fuzz_test.sh, as net_fuzz_p2p.c is.
*/

#include "../../linux/src/p2p_signal.c"
#include "../../linux/src/p2p_crypto.c"
#include "net_fuzz_common.h"

/* ---------- what p2p.c gives signalling */

static unsigned char fuzz_secret_key[P2P_KEY_SIZE] = { 0x40, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
static unsigned char fuzz_public_key[P2P_KEY_SIZE];
static unsigned char fuzz_identifier[P2P_IDENTIFIER_SIZE];
/* the other host this machine joins, and a joiner of this machine's game:
their keys */
static unsigned char fuzz_host_secret[P2P_KEY_SIZE] = { 0x48, 9, 9, 9 };
static unsigned char fuzz_host_public[P2P_KEY_SIZE];
static unsigned char fuzz_joiner_secret[P2P_KEY_SIZE] = { 0x50, 7, 7, 7 };
static unsigned char fuzz_joiner_public[P2P_KEY_SIZE];
static const unsigned char fuzz_token[P2P_TOKEN_SIZE] = { 't', 'o', 'k', 'e', 'n' };
static const unsigned char fuzz_join_token[P2P_TOKEN_SIZE] = { 'j', 'o', 'i', 'n' };
static int fuzz_offers;
/* the pairs' bases (pair_base), worked out once: X25519 is slow */
static unsigned char fuzz_join_base[P2P_SHA256_SIZE];
static unsigned char fuzz_host_base[P2P_SHA256_SIZE];

unsigned long p2p_now(void) { return net_fuzz_clock; }
unsigned long p2p_resolve(const char *host) { (void)host; return 0x01020304; }
void p2p_register_url_scheme(const char *scheme, const char *description) { (void)scheme; (void)description; }
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
int p2p_local_candidates(struct p2p_candidate *candidates, int maximum_count)
{
	(void)maximum_count;
	candidates[0].address = 0x0100000A;
	candidates[0].port = 0x1234;
	return 1;
}
const unsigned char *p2p_identifier(void) { return fuzz_identifier; }
const unsigned char *p2p_public_key(void) { return fuzz_public_key; }
void p2p_key_hash(const unsigned char *key, unsigned char *hash)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(key, P2P_KEY_SIZE, digest);
	memcpy(hash, digest, P2P_KEY_HASH_SIZE);
}
void p2p_identifier_from_hash(const unsigned char *hash, unsigned char *identifier)
{
	memcpy(identifier, hash, P2P_IDENTIFIER_SIZE);
	identifier[0] = (unsigned char)((identifier[0] & 0xFC) | 0x02);
}
void p2p_identifier_for(const unsigned char *key, unsigned char *identifier)
{
	unsigned char hash[P2P_KEY_HASH_SIZE];

	p2p_key_hash(key, hash);
	p2p_identifier_from_hash(hash, identifier);
}
int p2p_shared_secret(const unsigned char *key, unsigned char *shared)
{
	static const unsigned char zero[P2P_KEY_SIZE];

	p2p_x25519(shared, fuzz_secret_key, key);
	return !p2p_equal(shared, zero, P2P_KEY_SIZE);
}
int p2p_peer_offered(const unsigned char *identifier, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count, int is_host)
{
	(void)identifier; (void)secret; (void)is_host;
	if (count < 0 || count > P2P_MAXIMUM_CANDIDATES)
		abort();
	if (count > 0)
		fuzz_offers += (int)(candidates[count - 1].port & 1);
	return 1;
}
int p2p_peer_turned_away(const unsigned char *identifier, int is_host)
{
	(void)is_host;
	return !memcmp(identifier, fuzz_identifier, P2P_IDENTIFIER_SIZE);
}
int p2p_peer_reoffered(const unsigned char *identifier, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count)
{
	return p2p_peer_offered(identifier, secret, candidates, count, 0);
}
int p2p_session_retired(const unsigned char *secret) { (void)secret; return 0; }
void p2p_code_found(const char *text)
{
	/* (the record's invite: 12 + 64 hex digits, ended) */
	if (strlen(text) != 12 + 2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE))
		abort();
}
void p2p_invite_received(const char *text) { (void)text; }

/* ---------- the state each input starts from */

static void fuzz_reset(void)
{
	static int keys_made;
	struct broker *broker;
	unsigned char host_hash[P2P_KEY_HASH_SIZE];

	if (!keys_made)
	{
		p2p_x25519(fuzz_public_key, fuzz_secret_key, NULL);
		p2p_identifier_for(fuzz_public_key, fuzz_identifier);
		p2p_x25519(fuzz_host_public, fuzz_host_secret, NULL);
		p2p_x25519(fuzz_joiner_public, fuzz_joiner_secret, NULL);
		/* (as the joiner and the host each work them out) */
		pair_base(fuzz_public_key, fuzz_host_public, fuzz_host_public, fuzz_join_base);
		pair_base(fuzz_joiner_public, fuzz_public_key, fuzz_joiner_public, fuzz_host_base);
		keys_made = 1;
	}
	memset(&signalling, 0, sizeof(signalling));
	net_fuzz_clock = 1000000;
	net_fuzz_random_state = 0x9E3779B97F4A7C15ULL;
	p2p_signal_start();
	broker = &signalling.brokers[0];
	broker->socket = 9;
	broker->state = _broker_ready;
	broker->heard_time = broker->sent_time = net_fuzz_clock;
	p2p_signal_host(fuzz_token, "ABCD-EFGH");
	p2p_signal_set_lobby(1, "ABCD-EFGH", "fuzz", 1, 16);
	p2p_key_hash(fuzz_host_public, host_hash);
	p2p_signal_join(host_hash, fuzz_join_token);
	p2p_signal_lookup_code("WXYZ2345", NULL);
	p2p_signal_browse(1);
	broker->input_size = 0;
	/* (the work of the two keys the harness's messages come from, done:
	as a joiner that asked before, and a host that answered before) */
	memcpy(signalling.join_host_public, fuzz_host_public, P2P_KEY_SIZE);
	memcpy(signalling.join_base, fuzz_join_base, P2P_SHA256_SIZE);
	signalling.join_has_base = 1;
	signalling.askers[0].used = 1;
	p2p_identifier_for(fuzz_joiner_public, signalling.askers[0].identifier);
	memcpy(signalling.askers[0].public_key, fuzz_joiner_public, P2P_KEY_SIZE);
	memcpy(signalling.askers[0].base, fuzz_host_base, P2P_SHA256_SIZE);
	signalling.next_asker = 1;
}

/* a PUBLISH from the broker, as it frames one */
static void fuzz_publish(const char *topic, const unsigned char *payload, int size)
{
	struct broker *broker = &signalling.brokers[0];
	unsigned char packet[BUFFER_SIZE];
	int topic_size = (int)strlen(topic);
	int remaining = 2 + topic_size + size;
	int length = 0;

	if (broker->socket < 0 || remaining > BUFFER_SIZE - 8)
		return;
	packet[length++] = 0x30;
	do
	{
		unsigned char byte = (unsigned char)(remaining & 127);

		remaining >>= 7;
		packet[length++] = (unsigned char)(remaining ? byte | 128 : byte);
	} while (remaining);
	packet[length++] = (unsigned char)(topic_size >> 8);
	packet[length++] = (unsigned char)topic_size;
	memcpy(packet + length, topic, (size_t)topic_size);
	length += topic_size;
	memcpy(packet + length, payload, (size_t)size);
	length += size;
	if (broker->input_size + length > BUFFER_SIZE)
		broker->input_size = 0;
	memcpy(broker->input + broker->input_size, packet, (size_t)length);
	broker->input_size += length;
	broker_parse(broker);
}

static void fuzz_record(int kind, const unsigned char *data, int size)
{
	static unsigned char message[MAXIMUM_MESSAGE_SIZE + 64];
	static unsigned char sealed[MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD + 64];
	struct broker *broker = &signalling.brokers[0];

	switch (kind)
	{
	case 0:
		if (broker->socket < 0)
			break;
		if (size > BUFFER_SIZE - broker->input_size)
			size = BUFFER_SIZE - broker->input_size;
		memcpy(broker->input + broker->input_size, data, (size_t)size);
		broker->input_size += size;
		broker_parse(broker);
		break;
	case 1:
		if (size > MAXIMUM_MESSAGE_SIZE)
			size = MAXIMUM_MESSAGE_SIZE;
		fuzz_publish(signalling.host_topic, sealed, p2p_seal(signalling.host_key, data, size, sealed));
		break;
	case 2:
	{
		/* the real host's ACCEPT: its key, the joiner's nonce, its nonce
		and addresses (the fuzzer's), a tag of their pair */
		int length = 0;

		if (size > MAXIMUM_MESSAGE_SIZE - 2 - P2P_KEY_SIZE - NONCE_SIZE - TAG_SIZE)
			size = MAXIMUM_MESSAGE_SIZE - 2 - P2P_KEY_SIZE - NONCE_SIZE - TAG_SIZE;
		message[length++] = _message_accept;
		message[length++] = MESSAGE_VERSION;
		memcpy(message + length, fuzz_host_public, P2P_KEY_SIZE);
		length += P2P_KEY_SIZE;
		memcpy(message + length, signalling.join_nonce, NONCE_SIZE);
		length += NONCE_SIZE;
		memcpy(message + length, data, (size_t)size);
		length += size;
		message_tag(fuzz_join_base, "accept", message, length, message + length);
		length += TAG_SIZE;
		fuzz_publish(signalling.join_topic, sealed, p2p_seal(signalling.join_key, message, length, sealed));
		break;
	}
	case 3:
		if (size > MAXIMUM_MESSAGE_SIZE)
			size = MAXIMUM_MESSAGE_SIZE;
		/* (the lookup ends with a record: started again) */
		if (!signalling.looking_up)
			p2p_signal_lookup_code("WXYZ2345", size && (data[0] & 1) ? fuzz_identifier : NULL);
		fuzz_publish(signalling.lookup_topic, sealed, p2p_seal(signalling.lookup_key, data, size, sealed));
		break;
	case 4:
	{
		char topic[TOPIC_SIZE + 64];
		int end = size ? data[0] : 0;

		if (end > size - 1)
			end = size > 0 ? size - 1 : 0;
		if (end > (int)sizeof(topic) - (int)strlen(LOBBY_TOPIC_PREFIX) - 1)
			end = (int)sizeof(topic) - (int)strlen(LOBBY_TOPIC_PREFIX) - 1;
		snprintf(topic, sizeof(topic), "%s", LOBBY_TOPIC_PREFIX);
		memcpy(topic + strlen(LOBBY_TOPIC_PREFIX), data + 1, (size_t)end);
		topic[strlen(LOBBY_TOPIC_PREFIX) + end] = 0;
		/* (topics never hold a 0: the rest is cut there) */
		fuzz_publish(topic, data + 1 + end, size > 0 ? size - 1 - end : 0);
		break;
	}
	case 5:
	{
		/* a real joiner's proven JOIN */
		int length = 0;

		if (size < NONCE_SIZE)
			break;
		if (size > MAXIMUM_MESSAGE_SIZE - 2 - P2P_KEY_SIZE - PROOF_SIZE)
			size = MAXIMUM_MESSAGE_SIZE - 2 - P2P_KEY_SIZE - PROOF_SIZE;
		message[length++] = _message_join;
		message[length++] = MESSAGE_VERSION;
		memcpy(message + length, fuzz_joiner_public, P2P_KEY_SIZE);
		length += P2P_KEY_SIZE;
		memcpy(message + length, data, (size_t)size);
		length += size;
		host_nonce_for(fuzz_joiner_public, data, p2p_now() / HOST_NONCE_PERIOD, message + length);
		length += NONCE_SIZE;
		message_tag(fuzz_host_base, "join", message, length, message + length);
		length += TAG_SIZE;
		fuzz_publish(signalling.host_topic, sealed, p2p_seal(signalling.host_key, message, length, sealed));
		break;
	}
	case 6:
	{
		int none[1];

		net_fuzz_clock += size ? data[0] * 100UL : 100UL;
		p2p_signal_update(none, 0, none, 0);
		break;
	}
	}
	/* (a broker closed for what it sent is connected again) */
	if (broker->socket < 0)
	{
		broker->socket = 9;
		broker->state = _broker_ready;
		broker->heard_time = broker->sent_time = net_fuzz_clock;
	}
}

static int net_fuzz_next_record(const unsigned char **cursor, const unsigned char *end, int *kind,
	const unsigned char **record, int *size)
{
	int length;

	if (end - *cursor < 3)
		return 0;
	*kind = (*cursor)[0] % 7;
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
	struct p2p_lobby_entry entry;
	int index;

	fuzz_reset();
	while (count++ < 64 && net_fuzz_next_record(&cursor, end, &kind, &record, &record_size))
		fuzz_record(kind, record, record_size);
	/* what a menu shows of the lobby: ended, and only what draws */
	for (index = 0; p2p_signal_lobby_entry(index, &entry); index++)
	{
		const char *character;

		if (!memchr(entry.name, 0, sizeof(entry.name)) || !memchr(entry.code, 0, sizeof(entry.code)))
			abort();
		for (character = entry.name; *character; character++)
		{
			if ((unsigned char)*character < 32 || (unsigned char)*character > 126)
				abort();
		}
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

static int fuzz_lobby_count(void)
{
	struct p2p_lobby_entry entry;
	int count = 0;

	while (p2p_signal_lobby_entry(count, &entry))
		count++;
	return count;
}

int net_fuzz_checks(void)
{
	static unsigned char message[MAXIMUM_MESSAGE_SIZE + 64];
	static unsigned char sealed[MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD + 64];
	struct p2p_lobby_entry entry;
	char topic[TOPIC_SIZE + 16];
	int size, index, sent;

	/* the public lobby: unsealed, from anyone */
	fuzz_reset();
	size = 0;
	message[size++] = _message_lobby;
	message[size++] = RECORD_VERSION;
	message[size++] = 0;
	message[size++] = 17;
	memcpy(message + size, "ABCDEFGH", 8);
	size += 8;
	message[size++] = 200;
	message[size++] = 3;
	message[size++] = 9;
	memcpy(message + size, "x%s\n\x1b[2Jy", 9);
	size += 9;
	snprintf(topic, sizeof(topic), "%s0123456789ab", LOBBY_TOPIC_PREFIX);
	fuzz_publish(topic, message, size);
	fuzz_check(p2p_signal_lobby_entry(0, &entry) && !strcmp(entry.name, "x%s??[2Jy") && entry.players == 200 &&
		!strcmp(entry.host, "0123456789ab"), "a lobby entry's name is shown only as printable ASCII");
	message[4] = '1';
	snprintf(topic, sizeof(topic), "%s0123456789ac", LOBBY_TOPIC_PREFIX);
	fuzz_publish(topic, message, size);
	message[4] = 'A';
	message[size - 10] = 40;
	fuzz_publish(topic, message, size);
	snprintf(topic, sizeof(topic), "%s0123456789AB", LOBBY_TOPIC_PREFIX);
	fuzz_publish(topic, message, size);
	fuzz_check(fuzz_lobby_count() == 1, "nor a code with a letter not in its alphabet, a name past the entry, an identifier "
		"not in lower-case hexadecimal");
	for (index = 0; index < 2 * MAXIMUM_LOBBIES; index++)
	{
		message[size - 10] = 9;
		snprintf(topic, sizeof(topic), "%s%012x", LOBBY_TOPIC_PREFIX, 0x100000 + index);
		fuzz_publish(topic, message, size);
	}
	fuzz_check(fuzz_lobby_count() == MAXIMUM_LOBBIES, "a flood of entries keeps no more than the lobby's places");
	net_fuzz_clock += LOBBY_STALE_TIME + 1000;
	{
		int none[1];

		p2p_signal_update(none, 0, none, 0);
	}
	fuzz_check(fuzz_lobby_count() == 0, "and they go once their senders stop");

	/* a code's record, looked up for a lobby entry's host */
	fuzz_reset();
	p2p_signal_lookup_code("WXYZ2345", fuzz_identifier);
	size = 0;
	message[size++] = _message_code;
	message[size++] = RECORD_VERSION;
	memset(message + size, 0x77, P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE);
	size += P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE;
	fuzz_publish(signalling.lookup_topic, sealed, p2p_seal(signalling.lookup_key, message, size, sealed));
	fuzz_check(signalling.looking_up, "a record of another host than the lobby entry's is not taken");
	p2p_key_hash(fuzz_public_key, message + 2);
	fuzz_publish(signalling.lookup_topic, sealed, p2p_seal(signalling.lookup_key, message, size, sealed));
	fuzz_check(!signalling.looking_up, "the entry's own host's is");

	/* MQTT framing */
	fuzz_reset();
	{
		static const unsigned char oversized[] = { 0x30, 0xFF, 0xFF, 0xFF, 0x7F };
		static const unsigned char too_long[] = { 0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
		struct broker *broker = &signalling.brokers[0];

		memcpy(broker->input, oversized, sizeof(oversized));
		broker->input_size = sizeof(oversized);
		broker_parse(broker);
		fuzz_check(broker->socket < 0, "a packet longer than the buffer closes the broker's connection");
		fuzz_reset();
		broker = &signalling.brokers[0];
		memcpy(broker->input, too_long, sizeof(too_long));
		broker->input_size = sizeof(too_long);
		broker_parse(broker);
		fuzz_check(broker->socket < 0, "so does a length of more than four bytes");
	}
	/* a JOIN: its addresses */
	fuzz_reset();
	sent = net_fuzz_sent_count;
	size = 0;
	message[size++] = _message_join;
	message[size++] = MESSAGE_VERSION;
	memcpy(message + size, fuzz_joiner_public, P2P_KEY_SIZE);
	size += P2P_KEY_SIZE;
	memset(message + size, 0x42, NONCE_SIZE);
	size += NONCE_SIZE;
	message[size++] = P2P_MAXIMUM_CANDIDATES + 1;
	memset(message + size, 1, 6 * (P2P_MAXIMUM_CANDIDATES + 1));
	size += 6 * (P2P_MAXIMUM_CANDIDATES + 1);
	fuzz_publish(signalling.host_topic, sealed, p2p_seal(signalling.host_key, message, size, sealed));
	fuzz_check(net_fuzz_sent_count == sent, "a JOIN with more addresses than a machine has is not answered");
	message[2 + P2P_KEY_SIZE + NONCE_SIZE] = 1;
	size = 2 + P2P_KEY_SIZE + NONCE_SIZE + 1 + 6;
	fuzz_publish(signalling.host_topic, sealed, p2p_seal(signalling.host_key, message, size, sealed));
	fuzz_check(net_fuzz_sent_count == sent + 1, "one with one is answered");
	fuzz_publish(signalling.host_topic, sealed, p2p_seal(signalling.host_key, message, size, sealed));
	fuzz_check(net_fuzz_sent_count == sent + 1, "and the same again (a replay) not at once");
	fuzz_reset();
	return fuzz_failures;
}
