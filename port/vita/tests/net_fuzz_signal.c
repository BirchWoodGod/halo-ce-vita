/*
NET_FUZZ_SIGNAL.C

A fuzz target for internet play's signalling (port/linux/src/p2p_signal.c,
included here whole for its statics): what anyone who can publish on the
public MQTT brokers can send a machine, which reads the brokers' packets
(MQTT 5, and 3.1.1 every other input), a host's JOIN requests, a joiner's
ACCEPT answers, a code's record and the server browser's slots and queries
(whose listings p2p_lobby.c reads: p2p_lobby_fuzz.c fuzzes that).

Each input starts with the machine connected to one broker, hosting (with a
code, and listed in the server browser), joining another host, looking up
a code and browsing the server browser at once, so that every kind of
message is read. An input
is a list of records, each a kind, a 16-bit length and that many bytes:
  0  bytes from the broker, as they come (the framing itself)
  1  a PUBLISH on the host's topic, sealed with its token's key: a JOIN
  2  an ACCEPT from the joined host, sealed and tagged as the real host
     would (its key, the joiner's nonce): the rest is the fuzzer's
  3  a PUBLISH on the code's topic, sealed with the code's key: a record
  4  a server browser's slot: the topic's end (its first byte is the
     length), then the listing, unsealed (anyone can send one)
  5  a JOIN proven as a real joiner would (its own key, the host's nonce
     and a tag): the rest, its nonce and addresses, the fuzzer's
  6  the clock moves on (the first byte, in tenths of a second), and a pass
     of the signalling's update runs
  7  the broker over TLS (posix_tls_*'s stand-ins): the first byte says how
     its sends go (all, part, none now, failed) and how much a read gives;
     the rest arrives through TLS, read as the socket allows or as what TLS
     holds decrypted (posix_tls_pending), over passes of the update
  8  a broker's failure, and the ways tried after it (plain 1883, TLS 8883):
     each byte an event (a failure before it was ready, after, of its name,
     a TLS handshake that fails or succeeds, a CONNACK, time passing), the
     invariants checked after each
  9  the brokers' list (brokers.txt's entries, "host:port/tlsport"): the
     rest as network.signalling_brokers, read by p2p_signal_start
An input whose length has bit 1 set runs on the crossplay wire
(network_wire.h: OpenCE's message version 4, its WebRTC block after the
addresses, no relays): its game hosted on that wire (a Vita's for Vitas and
PC, PC joiners' JOINs read), its join, its listing, its code's record
(version 3, the wire after the token).
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
/* the pairs' bases (pair_base), worked out once for each wire (their label
is the wire's): X25519 is slow */
static unsigned char fuzz_join_bases[2][P2P_SHA256_SIZE];
static unsigned char fuzz_host_bases[2][P2P_SHA256_SIZE];
#define fuzz_join_base (fuzz_join_bases[net_fuzz_crossplay != 0])
#define fuzz_host_base (fuzz_host_bases[net_fuzz_crossplay != 0])

unsigned long p2p_now(void) { return net_fuzz_clock; }

/* ---------- the brokers' TLS (posix_https.c's posix_tls_*), stood in for:
one session at a time, what its peer sends queued, its sends as the input
says */
static struct
{
	int open, handshake, closed_count, opened_count;
	/* how sends go: 0 all, 1 part, 2 none now, 3 failed; a read's most */
	int send_mode, read_most;
	/* the record of a send held (posix_tls_send's contract: the same bytes
	first again), the bytes sent */
	int pending;
	unsigned char pending_bytes[64];
	unsigned long sent_bytes;
	unsigned char input[8192];
	int input_size;
	char host[256];
} fuzz_tls;

int posix_tls_open(int socket, const char *host, const char *ca_file, char *error, int error_size)
{
	(void)ca_file;
	if (socket < 0 || !host || strlen(host) >= sizeof(fuzz_tls.host))
		abort();
	if (fuzz_tls.open)
	{
		snprintf(error, (size_t)error_size, "too many TLS sessions");
		return -1;
	}
	snprintf(fuzz_tls.host, sizeof(fuzz_tls.host), "%s", host);
	fuzz_tls.open = 1;
	fuzz_tls.opened_count++;
	fuzz_tls.handshake = 0;
	fuzz_tls.pending = 0;
	fuzz_tls.input_size = 0;
	return 0;
}
int posix_tls_handshake(int handle, char *error, int error_size)
{
	if (handle != 0 || !fuzz_tls.open)
		abort();
	snprintf(error, (size_t)error_size, fuzz_tls.handshake < 0 ? "its certificate refused: fuzz" :
		fuzz_tls.handshake ? "TLS 1.3, fuzz, 1 ms" : "");
	return fuzz_tls.handshake;
}
int posix_tls_send(int handle, const void *buffer, int size)
{
	int taken;

	if (handle != 0 || !fuzz_tls.open || fuzz_tls.handshake != 1 || size <= 0 || !buffer)
		abort();
	/* (a held record's bytes must come first again, and as many) */
	if (fuzz_tls.pending)
	{
		int compare = fuzz_tls.pending < (int)sizeof(fuzz_tls.pending_bytes) ? fuzz_tls.pending :
			(int)sizeof(fuzz_tls.pending_bytes);

		if (size < fuzz_tls.pending || memcmp(buffer, fuzz_tls.pending_bytes, (size_t)compare))
			abort();
		if (fuzz_tls.send_mode == 2)
			return 0;
		taken = fuzz_tls.pending;
		fuzz_tls.pending = 0;
		fuzz_tls.sent_bytes += (unsigned long)taken;
		return taken;
	}
	switch (fuzz_tls.send_mode)
	{
	case 1:
		taken = size > 3 ? size / 2 : 1;
		break;
	case 2:
		fuzz_tls.pending = size > 100 ? 100 : size;
		memcpy(fuzz_tls.pending_bytes, buffer, (size_t)(fuzz_tls.pending < (int)sizeof(fuzz_tls.pending_bytes) ?
			fuzz_tls.pending : (int)sizeof(fuzz_tls.pending_bytes)));
		return 0;
	case 3:
		return -1;
	default:
		taken = size;
	}
	fuzz_tls.sent_bytes += (unsigned long)taken;
	return taken;
}
int posix_tls_receive(int handle, void *buffer, int size, char *error, int error_size)
{
	int count;

	error[0] = 0;
	if (handle != 0 || !fuzz_tls.open || fuzz_tls.handshake != 1 || size <= 0 || !buffer)
		abort();
	if (!fuzz_tls.input_size)
		return 0;
	count = fuzz_tls.input_size < size ? fuzz_tls.input_size : size;
	if (fuzz_tls.read_most && count > fuzz_tls.read_most)
		count = fuzz_tls.read_most;
	memcpy(buffer, fuzz_tls.input, (size_t)count);
	memmove(fuzz_tls.input, fuzz_tls.input + count, (size_t)(fuzz_tls.input_size - count));
	fuzz_tls.input_size -= count;
	/* (a peer that closes: an empty record's flag, the last byte 0xEE) */
	if (!fuzz_tls.input_size && count && ((unsigned char *)buffer)[count - 1] == 0xEE && fuzz_tls.read_most == 7)
	{
		snprintf(error, (size_t)error_size, "TLS: fuzz");
		return -2;
	}
	return count;
}
int posix_tls_pending(int handle)
{
	if (handle != 0 || !fuzz_tls.open)
		abort();
	return fuzz_tls.handshake == 1 && fuzz_tls.input_size > 0;
}
void posix_tls_close(int handle)
{
	if (handle != 0 || !fuzz_tls.open)
		abort();
	fuzz_tls.open = 0;
	fuzz_tls.closed_count++;
}
/* (fuzz_resolve_fails: the name cannot be looked up) */
static int fuzz_resolve_fails;
unsigned long p2p_resolve(const char *host) { (void)host; return fuzz_resolve_fails ? 0 : 0x01020304; }
const char *p2p_resolve_error(void) { return "fuzz"; }
void p2p_resolver_cache_allow(const char *host) { (void)host; }
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
/* (the relays: one offered, so a JOIN and an ACCEPT carry the block) */
int p2p_local_relays(struct p2p_candidate *relays, int maximum_count)
{
	(void)maximum_count;
	relays[0].address = 0x0200000A;
	relays[0].port = 0x5678;
	return 1;
}
void p2p_peer_relays(const unsigned char *identifier, const unsigned char *secret, const struct p2p_candidate *relays,
	int count)
{
	(void)identifier; (void)secret;
	if (count < 0 || count > P2P_MAXIMUM_RELAYS || (count && !relays))
		abort();
	if (count > 0)
		fuzz_offers += (int)(relays[count - 1].port & 1);
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
static int fuzz_code_wire = -1;
void p2p_code_found(const char *text, int crossplay)
{
	/* (the record's invite: 12 + 64 hex digits, ended; its wire 0 or 1) */
	if (strlen(text) != 12 + 2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE) || (crossplay != 0 && crossplay != 1))
		abort();
	fuzz_code_wire = crossplay;
}
static int fuzz_wire_known = -1;
void p2p_join_wire_known(int crossplay)
{
	if (crossplay != 0 && crossplay != 1)
		abort();
	fuzz_wire_known = crossplay;
}
void p2p_invite_received(const char *text) { (void)text; }

/* the server browser's (p2p_lobby.c): what reaches it is within bounds */
static int fuzz_slots_heard, fuzz_slot_wire;
void p2p_lobby_slot_heard(int crossplay, const char *hash_text, const unsigned char *payload, int size, int retained)
{
	static unsigned char copy[MAXIMUM_LISTING_SIZE];

	(void)retained;
	if (strlen(hash_text) >= TOPIC_SIZE || size < 0 || size > MAXIMUM_LISTING_SIZE || (crossplay != 0 && crossplay != 1))
		abort();
	fuzz_slot_wire = crossplay;
	if (size)
		memcpy(copy, payload, (size_t)size);
	fuzz_slots_heard++;
}
#define FRIENDS_STUB_TRAP() abort()
/* friends' (p2p_friends.c): their two topics subscribed, what is heard on
them handed on within bounds, and one message queued to publish */
static int friends_heard_count;
void p2p_friends_topics(char *inbox, char *mailbox, int size)
{
	snprintf(inbox, (size_t)size, "%s/f1/i/0123456789abcdef0123456789abcdef", P2P_SIGNAL_PREFIX);
	snprintf(mailbox, (size_t)size, "%s/f1/m/fedcba9876543210fedcba9876543210", P2P_SIGNAL_PREFIX);
}
void p2p_friends_heard(int which, const unsigned char *payload, int size)
{
	static unsigned char copy[MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD];

	if ((which != P2P_FRIENDS_INBOX && which != P2P_FRIENDS_MAILBOX) || size < 0 || size > (int)sizeof(copy))
		FRIENDS_STUB_TRAP();
	if (size)
		memcpy(copy, payload, (size_t)size);
	friends_heard_count++;
}
int p2p_friends_outgoing(int index, unsigned long *serial, char *topic, int topic_size, unsigned char *payload,
	int *sent_mask)
{
	if (index != 0)
		return -1;
	*serial = 1;
	snprintf(topic, (size_t)topic_size, "%s/f1/m/00112233445566778899aabbccddeeff", P2P_SIGNAL_PREFIX);
	memset(payload, 0x46, 300);
	*sent_mask = 0;
	return 300;
}
void p2p_friends_outgoing_sent(unsigned long serial, int broker) { (void)serial; (void)broker; }
void p2p_friends_outgoing_settle(int ready_mask) { (void)ready_mask; }

static int fuzz_queries[2];
void p2p_lobby_query_heard(int crossplay)
{
	if (crossplay != 0 && crossplay != 1)
		abort();
	fuzz_queries[crossplay]++;
}
void p2p_lobby_slot_topic(int crossplay, const unsigned char *key_hash, char *topic, int size)
{
	char text[2 * P2P_KEY_HASH_SIZE + 1];

	p2p_hex(key_hash, P2P_KEY_HASH_SIZE, text);
	snprintf(topic, (size_t)size, "%s%s", P2P_LOBBY_SLOT_PREFIX(crossplay), text);
}
void config_folder(char *path, size_t size) { snprintf(path, size, "./"); }
char *config_file_read(const char *path, size_t *size) { (void)path; (void)size; return NULL; }

/* ---------- the state each input starts from */

static void fuzz_reset_protocol(int protocol);

static void fuzz_reset(void)
{
	fuzz_reset_protocol(4);
}

static void fuzz_reset_protocol(int protocol)
{
	static int keys_made;
	struct broker *broker;
	unsigned char host_hash[P2P_KEY_HASH_SIZE];

	if (!keys_made)
	{
		int wire = net_fuzz_crossplay;

		p2p_x25519(fuzz_public_key, fuzz_secret_key, NULL);
		p2p_identifier_for(fuzz_public_key, fuzz_identifier);
		p2p_x25519(fuzz_host_public, fuzz_host_secret, NULL);
		p2p_x25519(fuzz_joiner_public, fuzz_joiner_secret, NULL);
		/* (as the joiner and the host each work them out, on each wire) */
		for (net_fuzz_crossplay = 0; net_fuzz_crossplay < 2; net_fuzz_crossplay++)
		{
			pair_base(net_fuzz_crossplay, fuzz_public_key, fuzz_host_public, fuzz_host_public, fuzz_join_base);
			pair_base(net_fuzz_crossplay, fuzz_joiner_public, fuzz_public_key, fuzz_joiner_public, fuzz_host_base);
		}
		net_fuzz_crossplay = wire;
		keys_made = 1;
	}
	memset(&signalling, 0, sizeof(signalling));
	memset(&fuzz_tls, 0, sizeof(fuzz_tls));
	fuzz_resolve_fails = 0;
	net_fuzz_clock = 1000000;
	net_fuzz_random_state = 0x9E3779B97F4A7C15ULL;
	p2p_signal_start();
	broker = &signalling.brokers[0];
	broker->socket = 9;
	broker->state = _broker_ready;
	broker->heard_time = broker->sent_time = net_fuzz_clock;
	broker->protocol = protocol;
	broker->retain_available = broker->wildcard_available = 1;
	broker->keep_alive = KEEP_ALIVE_SECONDS;
	broker->receive_maximum = MAXIMUM_IN_FLIGHT;
	broker->publish_tokens = PUBLISH_BURST;
	broker->publish_time = net_fuzz_clock;
	p2p_signal_host(fuzz_token, "ABCD-EFGH", net_fuzz_crossplay);
	p2p_signal_lobby_topics(1, net_fuzz_crossplay, 1);
	{
		static const unsigned char listing[] = "a listing";

		p2p_signal_lobby_publish(listing, sizeof(listing), 0);
	}
	p2p_key_hash(fuzz_host_public, host_hash);
	p2p_signal_join(host_hash, fuzz_join_token, net_fuzz_crossplay);
	p2p_signal_lookup_code("WXYZ2345", NULL);
	broker->input_size = 0;
	/* (the work of the two keys the harness's messages come from, done:
	as a joiner that asked before, and a host that answered before) */
	memcpy(signalling.join_host_public, fuzz_host_public, P2P_KEY_SIZE);
	memcpy(signalling.join_base, fuzz_join_base, P2P_SHA256_SIZE);
	signalling.join_has_base = 1;
	signalling.join_base_crossplay = net_fuzz_crossplay;
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

	/* (nothing is read from a broker still connecting or shaking hands) */
	if (broker->socket < 0 || broker->state == _broker_connecting || broker->state == _broker_handshaking ||
		remaining > BUFFER_SIZE - 8)
	{
		return;
	}
	/* (MQTT 5: no properties) */
	if (broker->protocol == 5)
		remaining++;
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
	if (broker->protocol == 5)
		packet[length++] = 0;
	memcpy(packet + length, payload, (size_t)size);
	length += size;
	if (broker->input_size + length > BUFFER_SIZE)
		broker->input_size = 0;
	memcpy(broker->input + broker->input_size, packet, (size_t)length);
	broker->input_size += length;
	broker_parse(broker);
}

/* what must hold of a broker whatever it was sent, and whatever failed */
static void fuzz_broker_invariants(const struct broker *broker)
{
	if ((broker->ways_failed & ~BOTH_WAYS) || broker->ways_failed == BOTH_WAYS || (broker->tls && !broker->tls_port) ||
		broker->rounds < 0 || broker->rounds > 1000 || retry_interval(broker) > MAXIMUM_RETRY_INTERVAL ||
		(broker->state == _broker_ready && (broker->exhausted || broker->rounds || broker->ways_failed)) ||
		(broker->tls_session != 0) != (fuzz_tls.open != 0) || (broker->tls_session && broker->tls_session != 1) ||
		(broker->state == _broker_handshaking && !broker->tls_session) ||
		(broker->retry_at_once && broker->state != _broker_idle) || broker->input_size < 0 ||
		broker->input_size > BUFFER_SIZE || broker->output_size < 0 || broker->output_size > BUFFER_SIZE ||
		strlen(broker->failure) >= sizeof(broker->failure))
	{
		abort();
	}
}

/* an update pass, the broker's socket readable and/or writable as said */
static void fuzz_pass(int readable, int writable)
{
	struct broker *broker = &signalling.brokers[0];
	int read[1], write[1];

	read[0] = write[0] = broker->socket;
	p2p_signal_update(read, readable && broker->socket >= 0, write, writable && broker->socket >= 0);
}

/* the broker's answer: plain (queued on its socket) or over TLS */
static void fuzz_answer(const unsigned char *bytes, int size)
{
	struct broker *broker = &signalling.brokers[0];

	if (broker->tls_session)
	{
		if (fuzz_tls.input_size + size <= (int)sizeof(fuzz_tls.input))
		{
			memcpy(fuzz_tls.input + fuzz_tls.input_size, bytes, (size_t)size);
			fuzz_tls.input_size += size;
		}
	}
	else if (broker->socket >= 0)
		net_fuzz_queue_receive(broker->socket, bytes, size, 0, 0);
}

/* a CONNACK as the broker's protocol frames it: its code */
static void fuzz_connack(int code)
{
	struct broker *broker = &signalling.brokers[0];
	unsigned char packet[5] = { 0x20, 2, 0, (unsigned char)code, 0 };

	if (broker->protocol == 5)
		packet[1] = 3;
	fuzz_answer(packet, broker->protocol == 5 ? 5 : 4);
}

/* the broker as a TLS one, ready (record 7) */
static void fuzz_tls_ready(void)
{
	struct broker *broker = &signalling.brokers[0];
	char error[64];

	if (broker->tls_session)
		return;
	if (!broker->tls_port)
		broker->tls_port = network_short(DEFAULT_TLS_PORT);
	broker->tls = 1;
	broker->tls_session = posix_tls_open(broker->socket >= 0 ? broker->socket : 9, broker->host, "", error,
		sizeof(error)) + 1;
	fuzz_tls.handshake = 1;
}

static void fuzz_record(int kind, const unsigned char *data, int size)
{
	static unsigned char message[MAXIMUM_MESSAGE_SIZE + 64];
	static unsigned char sealed[MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD + 64];
	struct broker *broker = &signalling.brokers[0];

	switch (kind)
	{
	case 0:
		if (broker->socket < 0 || broker->state == _broker_connecting || broker->state == _broker_handshaking)
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
		message[length++] = (unsigned char)message_version_of(net_fuzz_crossplay);
		memcpy(message + length, fuzz_host_public, P2P_KEY_SIZE);
		length += P2P_KEY_SIZE;
		memcpy(message + length, signalling.join_nonce, NONCE_SIZE);
		length += NONCE_SIZE;
		memcpy(message + length, data, (size_t)size);
		length += size;
		message_tag(fuzz_join_base, "accept", message, length, message + length);
		length += TAG_SIZE;
		fuzz_publish(signalling.join_topics[net_fuzz_crossplay], sealed,
			p2p_seal(signalling.join_key, message, length, sealed));
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
		const char *prefix = P2P_LOBBY_SLOT_PREFIX(net_fuzz_crossplay);

		if (end > (int)sizeof(topic) - (int)strlen(prefix) - 1)
			end = (int)sizeof(topic) - (int)strlen(prefix) - 1;
		snprintf(topic, sizeof(topic), "%s", prefix);
		memcpy(topic + strlen(prefix), data + 1, (size_t)end);
		topic[strlen(prefix) + end] = 0;
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
		message[length++] = (unsigned char)message_version_of(net_fuzz_crossplay);
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
	case 7:
	{
		/* the broker over TLS: its sends and reads as the first byte says,
		the rest arriving through it */
		int pass;

		if (broker->socket < 0 || broker->state != _broker_ready)
			break;
		fuzz_tls_ready();
		fuzz_tls.send_mode = size ? data[0] & 3 : 0;
		fuzz_tls.read_most = size ? (data[0] >> 2) & 7 : 0;
		if (size > 1)
			fuzz_answer(data + 1, size - 1);
		/* (some of what goes out: a query, a ping) */
		if (size && (data[0] & 0x20))
			p2p_signal_lobby_query();
		for (pass = 0; pass < 8 && broker->socket >= 0; pass++)
		{
			if (size && (data[0] & 0x40))
				net_fuzz_clock += 16000;
			fuzz_pass(pass & 1, size && (data[0] & 0x80));
		}
		break;
	}
	case 8:
	{
		/* failures, and the ways tried after them */
		int index;

		for (index = 0; index < size && index < 48; index++)
		{
			int event = data[index] & 7, more = data[index] >> 3;

			switch (event)
			{
			case 0:
				/* (its name's, now and then) */
				broker->every_way = more == 31;
				broker_failed(broker, "fuzz %d", more);
				break;
			case 1:
				net_fuzz_clock += (unsigned long)more * 500UL;
				fuzz_pass(more & 1, more & 2);
				break;
			case 2:
				if (broker->state == _broker_handshaking)
					fuzz_tls.handshake = 1;
				fuzz_pass(0, 0);
				break;
			case 3:
				if (broker->state == _broker_handshaking)
					fuzz_tls.handshake = -1;
				fuzz_pass(0, 0);
				break;
			case 4:
				fuzz_connack(0);
				fuzz_pass(1, 0);
				break;
			case 5:
				fuzz_connack(more & 1 ? 0x89 : more & 2 ? 3 : 0x84);
				fuzz_pass(1, 0);
				break;
			case 6:
				fuzz_resolve_fails = more & 1;
				broker->looked_up = !(more & 2);
				break;
			default:
				if (more & 1)
					p2p_signal_kick();
				else
					p2p_signal_lobby_topics(more & 2, more & 8, more & 4);
				signalling.kick_time = 0;
				break;
			}
			fuzz_broker_invariants(broker);
		}
		break;
	}
	case 9:
	{
		/* the brokers' list */
		static char text[1100];
		const char *kept = net_fuzz_brokers;
		int length = size < 1023 ? size : 1023, index, protocol = broker->protocol == 5 ? 5 : 4;

		memcpy(text, data, (size_t)length);
		text[length] = 0;
		/* (closed first: a session open stays the stand-in's) */
		broker_close(broker, 0);
		net_fuzz_brokers = text;
		memset(&signalling, 0, sizeof(signalling));
		p2p_signal_start();
		if (signalling.broker_count < 0 || signalling.broker_count > MAXIMUM_BROKERS)
			abort();
		for (index = 0; index < signalling.broker_count; index++)
		{
			const struct broker *each = &signalling.brokers[index];

			if (memchr(each->host, 0, sizeof(each->host)) == NULL || !each->host[0] || strchr(each->host, ':') ||
				strchr(each->host, '/') || strchr(each->host, ',') || each->socket != -1 || each->tls_session ||
				each->tls)
			{
				abort();
			}
		}
		net_fuzz_brokers = kept;
		fuzz_reset_protocol(protocol);
		break;
	}
	}
	/* (a broker closed for what it sent is connected again, plain) */
	if (broker->socket < 0)
	{
		broker->socket = 9;
		broker->state = _broker_ready;
		broker->heard_time = broker->sent_time = net_fuzz_clock;
		broker->publish_tokens = PUBLISH_BURST;
		broker->tls = broker->rounds = broker->ways_failed = broker->exhausted = broker->retry_at_once = 0;
		fuzz_resolve_fails = 0;
	}
	fuzz_broker_invariants(broker);
}

static int net_fuzz_next_record(const unsigned char **cursor, const unsigned char *end, int *kind,
	const unsigned char **record, int *size)
{
	int length;

	if (end - *cursor < 3)
		return 0;
	*kind = (*cursor)[0] % 10;
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

	/* (MQTT 3.1.1, and 5 for an input of odd length; the crossplay wire for
	one whose length has bit 1) */
	net_fuzz_crossplay = (size & 2) != 0;
	fuzz_reset_protocol(size & 1 ? 5 : 4);
	while (count++ < 64 && net_fuzz_next_record(&cursor, end, &kind, &record, &record_size))
		fuzz_record(kind, record, record_size);
	return 0;
}

/* ---------- checks (net_fuzz_main.c runs them before the cases) */

static int fuzz_failures;

static void fuzz_check(int condition, const char *what)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	fuzz_failures += !condition;
}

/* the last publish's sealed message, opened with key (the JOIN this machine
sent): its size, or -1 */
static int fuzz_sent_opened(const unsigned char *key, unsigned char *message)
{
	int offset;

	for (offset = 0; offset < net_fuzz_sent_size; offset++)
	{
		int size = p2p_open(key, net_fuzz_sent + offset, net_fuzz_sent_size - offset, message);

		if (size >= 2)
			return size;
	}
	return -1;
}

/* the real host's ACCEPT of this machine's JOIN with the given block after
the addresses (one, of the harness's); whether the joiner took it */
static int fuzz_accept_taken(int version, const unsigned char *block, int block_size)
{
	static unsigned char message[MAXIMUM_MESSAGE_SIZE + 64];
	static unsigned char sealed[MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD + 64];
	int length = 0;

	fuzz_reset();
	message[length++] = _message_accept;
	message[length++] = (unsigned char)version;
	memcpy(message + length, fuzz_host_public, P2P_KEY_SIZE);
	length += P2P_KEY_SIZE;
	memcpy(message + length, signalling.join_nonce, NONCE_SIZE);
	length += NONCE_SIZE;
	memset(message + length, 0x31, NONCE_SIZE);
	length += NONCE_SIZE;
	message[length++] = 1;
	memcpy(message + length, "\x0a\x00\x00\x02\x13\x88", 6);
	length += 6;
	memcpy(message + length, block, (size_t)block_size);
	length += block_size;
	message_tag(fuzz_join_base, "accept", message, length, message + length);
	length += TAG_SIZE;
	fuzz_publish(signalling.join_topics[net_fuzz_crossplay], sealed, p2p_seal(signalling.join_key, message, length, sealed));
	return signalling.join_answered;
}

/* a PC joiner's JOIN of this machine's game (hosted on the harness's wire),
unproven: its key, a nonce, one address, then block (block_size bytes) -
the WebRTC block, or a legacy joiner's relays; whether the host answered */
static int fuzz_join_answered(int version, const unsigned char *block, int block_size)
{
	static unsigned char message[MAXIMUM_MESSAGE_SIZE + 64];
	static unsigned char sealed[MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD + 64];
	int length = 0, sent;

	fuzz_reset();
	/* (another key than the asker the harness keeps: its work is done
	here, as a stranger's) */
	signalling.askers[0].used = 0;
	sent = net_fuzz_sent_count;
	message[length++] = _message_join;
	message[length++] = (unsigned char)version;
	memcpy(message + length, fuzz_joiner_public, P2P_KEY_SIZE);
	length += P2P_KEY_SIZE;
	memset(message + length, 0x5A, NONCE_SIZE);
	length += NONCE_SIZE;
	message[length++] = 1;
	memcpy(message + length, "\x0a\x00\x00\x07\x13\x89", 6);
	length += 6;
	memcpy(message + length, block, (size_t)block_size);
	length += block_size;
	fuzz_publish(signalling.host_topic, sealed, p2p_seal(signalling.host_key, message, length, sealed));
	return net_fuzz_sent_count > sent;
}

/* the crossplay wire (OpenCE's signalling, network_wire.h): a game joined
on it, a game hosted on it (a Vita's for Vitas and PC, PC joiners' JOINs
read), an invite link's host asked on both wires, the code's record that
says the wire, both wires' slots */
static void fuzz_crossplay_checks(void)
{
	static unsigned char message[MAXIMUM_MESSAGE_SIZE + 64];
	static unsigned char sealed[MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD + 64];
	static const unsigned char none[] = { 0 };
	/* (a native OpenCE host's: its certificate's hash, no ICE text, no
	names, as p2p_webrtc_describe makes it) */
	unsigned char native[1 + 32 + 1 + 1 + 1 + 1];
	/* (a browser's: ICE text and an mDNS name) */
	unsigned char browser[1 + 32 + 1 + 4 + 1 + 22 + 1 + 1 + 9 + 2];
	unsigned char bad[sizeof(browser)];
	unsigned char hash[P2P_KEY_HASH_SIZE];
	char topic[TOPIC_SIZE + 40];
	int size, offset, sent;

	net_fuzz_crossplay = 1;
	fuzz_reset();
	fuzz_check(signalling.hosting && signalling.host_crossplay && signalling.lobby_listed &&
		signalling.lobby_listed_crossplay && signalling.lobby_browsing,
		"crossplay: a game hosted on it, listed on it; browsing");
	fuzz_check(!strncmp(signalling.join_topics[1], "hceu/3/", 7) && !strncmp(signalling.join_host_topics[1], "hceu/3/", 7) &&
		!strncmp(signalling.host_topic, "hceu/3/", 7) && !strncmp(signalling.lobby_slot, "hceu/3/lobby/s/", 15) &&
		!strcmp(P2P_LOBBY_SLOT_PREFIX(1), "hceu/3/lobby/s/") && !strcmp(P2P_LOBBY_QUERY_TOPIC(1), "hceu/3/lobby/q") &&
		!strncmp(signalling.client_identifier, P2P_SIGNAL_PREFIX "-", 5),
		"crossplay: OpenCE's topics (hceu) for its game, this build's client identifier");
	fuzz_check(!strncmp(signalling.code_topic, P2P_SIGNAL_PREFIX "/3/", 5) &&
		!strncmp(signalling.own_slot, P2P_SIGNAL_PREFIX "/3/lobby/s/", 15),
		"crossplay: its code on this build's own topics; the will on the legacy slot");
	/* the JOIN sent: version 4, the addresses, a WebRTC block of kind none,
	no relays (though the harness offers one) */
	p2p_key_hash(fuzz_host_public, hash);
	p2p_signal_join(hash, fuzz_join_token, 1);
	size = fuzz_sent_opened(signalling.join_key, message);
	offset = 2 + P2P_KEY_SIZE + NONCE_SIZE;
	fuzz_check(size == offset + 1 + 6 + 1 && message[0] == _message_join && message[1] == 4 && message[offset] == 1 &&
		message[offset + 7] == 0, "crossplay: the JOIN is OpenCE's version 4: addresses, WebRTC kind none, no relays");
	/* the host's ACCEPT */
	fuzz_check(fuzz_accept_taken(4, none, sizeof(none)), "crossplay: an ACCEPT with WebRTC kind none is taken");
	size = fuzz_sent_opened(signalling.join_key, message);
	fuzz_check(size == offset + 1 + 6 + 1 + PROOF_SIZE && message[1] == 4 && message[offset + 7] == 0,
		"crossplay: its proof is version 4, kind none, then the host's nonce and the tag (no relays)");
	memset(native, 0, sizeof(native));
	native[0] = 1;
	memset(native + 1, 0xAB, 32);
	fuzz_check(fuzz_accept_taken(4, native, sizeof(native) - 1),
		"crossplay: a native host's block (certificate, no ICE text, no names) is taken");
	fuzz_check(!fuzz_accept_taken(4, native, sizeof(native)), "crossplay: one with a byte left over is not");
	size = 0;
	browser[size++] = 2;
	memset(browser + size, 0xCD, 32);
	size += 32;
	browser[size++] = 4;
	memcpy(browser + size, "ab+/", 4);
	size += 4;
	browser[size++] = 22;
	memcpy(browser + size, "0123456789abcdefghijkl", 22);
	size += 22;
	browser[size++] = 1;
	browser[size++] = 9;
	memcpy(browser + size, "a-1.local", 9);
	size += 9;
	browser[size++] = 0x13;
	browser[size++] = 0x88;
	fuzz_check(size == (int)sizeof(browser) && fuzz_accept_taken(4, browser, size),
		"crossplay: a browser's block (ICE text, an mDNS name and its port) is taken");
	memcpy(bad, browser, sizeof(bad));
	bad[1 + 32 + 1] = '!';
	fuzz_check(!fuzz_accept_taken(4, bad, size), "crossplay: ICE text with a character not ICE's is not");
	memcpy(bad, browser, sizeof(bad));
	bad[1 + 32] = 3;
	fuzz_check(!fuzz_accept_taken(4, bad, size), "crossplay: a username fragment shorter than 4 is not");
	memcpy(bad, browser, sizeof(bad));
	bad[1 + 32 + 1 + 4 + 1 + 22] = 3;
	fuzz_check(!fuzz_accept_taken(4, bad, size), "crossplay: more mDNS names than 2 is not");
	memcpy(bad, browser, sizeof(bad));
	bad[size - 4] = 'x';
	fuzz_check(!fuzz_accept_taken(4, bad, size), "crossplay: a name not \".local\" is not");
	memcpy(bad, browser, sizeof(bad));
	bad[size - 2] = bad[size - 1] = 0;
	fuzz_check(!fuzz_accept_taken(4, bad, size), "crossplay: a name's port 0 is not");
	bad[0] = 3;
	fuzz_check(!fuzz_accept_taken(4, bad, 1), "crossplay: a WebRTC kind past 2 is not");
	fuzz_check(!fuzz_accept_taken(4, none, 0), "crossplay: an ACCEPT without the block is not");
	{
		/* (a legacy host's: its relays where the block goes) */
		static const unsigned char relays[] = { 1, 10, 0, 0, 3, 0x13, 0x88 };

		fuzz_check(!fuzz_accept_taken(4, relays, sizeof(relays)), "crossplay: relays where the block goes are not");
		/* the host's side: a PC joiner's JOIN (docs/crossplay.md 7.4) */
		fuzz_check(fuzz_join_answered(4, none, sizeof(none)), "crossplay host: a PC joiner's JOIN, WebRTC kind none, answered");
		size = fuzz_sent_opened(signalling.host_key, message);
		fuzz_check(size == 2 + P2P_KEY_SIZE + 2 * NONCE_SIZE + 1 + 6 + 1 + TAG_SIZE && message[0] == _message_accept &&
			message[1] == 4 && message[2 + P2P_KEY_SIZE + 2 * NONCE_SIZE + 7] == 0,
			"crossplay host: its ACCEPT is OpenCE's version 4: addresses, WebRTC kind none exactly, no relays");
		fuzz_check(fuzz_join_answered(4, native, sizeof(native) - 1) && fuzz_join_answered(4, browser, sizeof(browser)),
			"crossplay host: a native or a browser joiner's block checked and stepped past (the tunnel plain UDP)");
		fuzz_check(!fuzz_join_answered(4, bad, 1), "crossplay host: a WebRTC kind past 2 is not answered");
		fuzz_check(!fuzz_join_answered(4, none, 0), "crossplay host: a JOIN without the block is not");
		fuzz_check(!fuzz_join_answered(4, native, sizeof(native)), "crossplay host: one with a byte after the block is not");
		fuzz_check(!fuzz_join_answered(4, relays, sizeof(relays)), "crossplay host: relays where the block goes are not");
		fuzz_check(!fuzz_join_answered(3, none, 0), "crossplay host: a JOIN of version 3 (the Vita wire's) is not");
	}
	fuzz_check(!fuzz_accept_taken(3, none, sizeof(none)), "crossplay: an ACCEPT of version 3 (the Vita wire's) is not");
	/* a code's record of a game on the crossplay wire: version 3, the wire */
	fuzz_reset();
	p2p_signal_lookup_code("WXYZ2345", NULL);
	size = 0;
	message[size++] = _message_code;
	message[size++] = RECORD_VERSION_WIRE;
	memset(message + size, 0x66, P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE);
	size += P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE;
	message[size++] = 7;
	fuzz_code_wire = -1;
	fuzz_publish(signalling.lookup_topic, sealed, p2p_seal(signalling.lookup_key, message, size, sealed));
	fuzz_check(signalling.looking_up && fuzz_code_wire < 0, "a record of version 3 whose wire is not 0 or 1 is not taken");
	message[size - 1] = 1;
	fuzz_publish(signalling.lookup_topic, sealed, p2p_seal(signalling.lookup_key, message, size + 1, sealed));
	fuzz_check(signalling.looking_up && fuzz_code_wire < 0, "nor one of version 3 with a byte more");
	fuzz_publish(signalling.lookup_topic, sealed, p2p_seal(signalling.lookup_key, message, size, sealed));
	fuzz_check(!signalling.looking_up && fuzz_code_wire == 1, "a record of version 3 with wire 1: joined on the crossplay wire");
	/* the code's record a crossplay host publishes: version 3 */
	signalling.code_sent_time = 0;
	sent = net_fuzz_sent_count;
	publish_code(NULL);
	size = fuzz_sent_opened(signalling.code_key, message);
	fuzz_check(net_fuzz_sent_count > sent && size == 2 + P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE + 1 &&
		message[1] == RECORD_VERSION_WIRE && message[size - 1] == 1,
		"a crossplay host's code record: version 3, its wire 1 after the token");
	/* an invite link's host: asked on both wires, kept on the one that
	answers */
	fuzz_reset();
	p2p_key_hash(fuzz_host_public, hash);
	p2p_signal_join(hash, fuzz_join_token, -1);
	fuzz_check(signalling.join_wire == -1 && signalling.brokers[0].topics[_topic_join][0] &&
		signalling.brokers[0].topics[_topic_join_crossplay][0], "an invite link: both wires' answers subscribed");
	signalling.join_has_base = 0;
	{
		int length = 0;

		message[length++] = _message_accept;
		message[length++] = 4;
		memcpy(message + length, fuzz_host_public, P2P_KEY_SIZE);
		length += P2P_KEY_SIZE;
		memcpy(message + length, signalling.join_nonce, NONCE_SIZE);
		length += NONCE_SIZE;
		memset(message + length, 0x31, NONCE_SIZE);
		length += NONCE_SIZE;
		message[length++] = 1;
		memcpy(message + length, "\x0a\x00\x00\x02\x13\x88\x00", 7);
		length += 7;
		message_tag(fuzz_join_bases[1], "accept", message, length, message + length);
		length += TAG_SIZE;
		fuzz_wire_known = -1;
		fuzz_publish(signalling.join_topics[1], sealed, p2p_seal(signalling.join_key, message, length, sealed));
	}
	fuzz_check(signalling.join_answered && signalling.join_wire == 1 && fuzz_wire_known == 1 &&
		!signalling.brokers[0].topics[_topic_join][0], "the crossplay wire's ACCEPT: kept, told, the other wire let go");
	/* both wires' slots: read, each told by its wire */
	fuzz_reset();
	fuzz_check(signalling.brokers[0].topics[_topic_slots][0] && signalling.brokers[0].topics[_topic_slots_crossplay][0],
		"browsing: both wires' slots subscribed");
	memset(message, 'L', 300);
	snprintf(topic, sizeof(topic), "%s0123456789abcdef0123456789abcdef", P2P_LOBBY_SLOT_PREFIX(1));
	fuzz_slots_heard = 0;
	fuzz_publish(topic, message, 300);
	fuzz_check(fuzz_slots_heard == 1 && fuzz_slot_wire == 1, "a crossplay slot's listing: passed on, of the crossplay wire");
	memset(fuzz_queries, 0, sizeof(fuzz_queries));
	fuzz_publish(P2P_LOBBY_QUERY_TOPIC(1), message, 8);
	fuzz_publish(P2P_LOBBY_QUERY_TOPIC(0), message, 8);
	fuzz_check(fuzz_queries[1] == 1 && fuzz_queries[0] == 1, "each wire's query heard as its own");
	/* and the legacy wire as it was: version 3, relays, no WebRTC */
	net_fuzz_crossplay = 0;
	fuzz_reset();
	fuzz_check(!strncmp(signalling.join_topics[0], P2P_SIGNAL_PREFIX "/3/", 5) && signalling.hosting &&
		!signalling.host_crossplay && !strncmp(signalling.lobby_slot, P2P_SIGNAL_PREFIX "/3/lobby/s/", 15),
		"legacy: its own topics (" P2P_SIGNAL_PREFIX "), hosting, listed");
	fuzz_check(!fuzz_accept_taken(4, none, sizeof(none)), "legacy: an ACCEPT of version 4 is not taken");
	fuzz_check(fuzz_accept_taken(3, none, 0), "legacy: one of version 3 without relays is");
	size = fuzz_sent_opened(signalling.join_key, message);
	fuzz_check(size > 1 && message[1] == 3, "legacy: its JOIN is version 3");
	fuzz_check(!fuzz_join_answered(4, none, sizeof(none)), "legacy host: a JOIN of version 4 is not answered");
	fuzz_check(fuzz_join_answered(3, none, 0), "legacy host: one of version 3 is");
	signalling.code_sent_time = 0;
	publish_code(NULL);
	size = fuzz_sent_opened(signalling.code_key, message);
	fuzz_check(size == 2 + P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE && message[1] == RECORD_VERSION,
		"legacy: its code record is version 2's, as builds before read it");
}

/* the ways a broker is reached: plain, its TLS port at once when that
fails before it is ready, the round's wait when both have, TLS first while
it last worked; the brokers' list's TLS ports */
static void fuzz_tls_checks(void)
{
	struct broker *broker;
	struct p2p_signal_counts counts;
	const char *kept = net_fuzz_brokers;
	unsigned long sent;

	/* the list */
	net_fuzz_brokers = "a.test:1883/8884, b.test/0 ,:1883,/8883,c.test:1884,d.test:1/99999,e.test:2,a.test:1883,"
		"f.test,g.test,h.test";
	fuzz_reset();
	fuzz_check(signalling.broker_count == 6 && !strcmp(signalling.brokers[4].host, "e.test") &&
		!strcmp(signalling.brokers[5].host, "f.test"),
		"brokers: each once (a.test again left out), at most 6 (both files' lists)");
	fuzz_check(!strcmp(signalling.brokers[0].host, "a.test") &&
		signalling.brokers[0].port == network_short(1883) && signalling.brokers[0].tls_port == network_short(8884) &&
		!strcmp(signalling.brokers[1].host, "b.test") && signalling.brokers[1].port == network_short(1883) &&
		!signalling.brokers[1].tls_port && signalling.brokers[2].port == network_short(1884) &&
		signalling.brokers[2].tls_port == network_short(8883) && !strcmp(signalling.brokers[3].host, "d.test") &&
		!signalling.brokers[3].tls_port,
		"brokers.txt: \"host:port/tlsport\" its TLS port, \"/0\" or a bad one none, 8883 unless said; no "
		"entry without a name");
	net_fuzz_brokers = kept;

	/* plain busy: TLS at once */
	fuzz_reset();
	broker = &signalling.brokers[0];
	broker_close(broker, 0);
	broker->retry_at_once = 1;
	fuzz_pass(0, 0);
	fuzz_check(broker->state == _broker_connecting && !broker->tls && net_fuzz_connect_port == network_short(1883),
		"a broker is tried on its plain port first");
	fuzz_pass(0, 1);
	fuzz_connack(3);
	fuzz_pass(1, 0);
	fuzz_check(broker->state == _broker_idle && broker->tls && broker->retry_at_once && !broker->exhausted &&
		!broker->rounds && broker->ways_failed == WAY_PLAIN && strstr(broker->failure, "busy"),
		"its plain port busy (CONNACK 3): its TLS port next, at once, the broker not out of reach");
	fuzz_pass(0, 0);
	fuzz_check(broker->state == _broker_connecting && net_fuzz_connect_port == network_short(8883),
		"connecting to its TLS port, 8883");
	fuzz_pass(0, 1);
	fuzz_check(broker->state == _broker_handshaking && fuzz_tls.open && !strcmp(fuzz_tls.host, "broker.test"),
		"connected: the TLS handshake begun, for the broker's name");
	sent = fuzz_tls.sent_bytes;
	fuzz_pass(1, 1);
	fuzz_check(broker->state == _broker_handshaking && fuzz_tls.sent_bytes == sent, "nothing sent while it shakes hands");
	fuzz_tls.handshake = 1;
	fuzz_pass(0, 0);
	fuzz_check(broker->state == _broker_awaiting_acknowledgement && fuzz_tls.sent_bytes > sent,
		"the handshake done: CONNECT sent over TLS");
	fuzz_connack(0);
	fuzz_pass(0, 0);
	p2p_signal_counts(&counts);
	fuzz_check(broker->state == _broker_ready && broker->tls && broker->tls_ready_time && !broker->exhausted &&
		counts.ready == 1 && !counts.exhausted, "CONNACK over TLS (read as TLS holds it, the socket quiet): ready");
	/* a dropped TLS connection: TLS first again */
	broker_failed(broker, "fuzz");
	fuzz_check(broker->state == _broker_idle && broker->tls && !broker->retry_at_once && broker->rounds == 1 &&
		!broker->exhausted && fuzz_tls.closed_count == 1 && !fuzz_tls.open,
		"its TLS connection dropped: the session closed, TLS first again after the wait");
	net_fuzz_clock += MAXIMUM_RETRY_INTERVAL;
	fuzz_pass(0, 0);
	fuzz_pass(0, 1);
	fuzz_tls.handshake = -1;
	fuzz_pass(0, 0);
	fuzz_check(broker->state == _broker_idle && !broker->tls && broker->retry_at_once && strstr(broker->failure, "over TLS"),
		"its handshake failed (said to be TLS's): its plain port at once");
	fuzz_pass(0, 0);
	broker_failed(broker, "fuzz");
	p2p_signal_counts(&counts);
	fuzz_check(broker->state == _broker_idle && broker->exhausted && !broker->retry_at_once && broker->rounds == 2 &&
		!broker->ways_failed && counts.exhausted == 1, "both ways failed: out of reach, the round's wait");
	net_fuzz_clock += PLAIN_AGAIN_TIME;
	fuzz_check(!broker_prefers_tls(broker), "plain first again once TLS last worked long ago");
	/* its name: no port mends that */
	fuzz_reset();
	broker = &signalling.brokers[0];
	broker_close(broker, 0);
	broker->looked_up = 0;
	broker->retry_at_once = 1;
	fuzz_resolve_fails = 1;
	fuzz_pass(0, 0);
	fuzz_check(broker->state == _broker_idle && broker->exhausted && !broker->retry_at_once && !broker->tls &&
		!strstr(broker->failure, "TLS"), "a name that cannot be looked up: out of reach at once, TLS not tried");
	fuzz_resolve_fails = 0;
	/* a broker without a TLS port */
	net_fuzz_brokers = "plain.test:1883/0";
	fuzz_reset();
	broker = &signalling.brokers[0];
	broker_close(broker, 0);
	broker->retry_at_once = 1;
	fuzz_pass(0, 0);
	broker_failed(broker, "fuzz");
	fuzz_check(broker->exhausted && !broker->tls && !broker->retry_at_once,
		"a broker without a TLS port: out of reach once its plain port fails");
	net_fuzz_brokers = kept;
	/* sends over TLS that wait: the same bytes first again (the stand-in
	aborts otherwise) */
	fuzz_reset();
	broker = &signalling.brokers[0];
	fuzz_tls_ready();
	fuzz_tls.send_mode = 2;
	p2p_signal_lobby_query();
	broker_send(broker, 0xC0, NULL, 0);
	fuzz_check(broker->output_size > 0 && fuzz_tls.pending, "a send TLS cannot take now is kept, its record held");
	fuzz_tls.send_mode = 0;
	fuzz_pass(0, 1);
	fuzz_check(broker->output_size == 0 && !fuzz_tls.pending, "and goes once the socket takes it");
	fuzz_reset();
}

int net_fuzz_checks(void)
{
	static unsigned char message[MAXIMUM_MESSAGE_SIZE + 64];
	static unsigned char sealed[MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD + 64];
	char topic[TOPIC_SIZE + 16];
	int size, index, sent;

	/* the server browser's slots: what comes is passed on (p2p_lobby.c
	checks it), within bounds; a listing too long for one is not */
	fuzz_reset();
	memset(message, 'L', 300);
	snprintf(topic, sizeof(topic), "%s0123456789abcdef0123456789abcdef", P2P_LOBBY_SLOT_PREFIX(0));
	fuzz_slots_heard = 0;
	fuzz_publish(topic, message, 300);
	fuzz_check(fuzz_slots_heard == 1 && fuzz_slot_wire == 0, "a slot's listing is passed on to the server browser");
	fuzz_publish(topic, message, MAXIMUM_LISTING_SIZE + 1);
	fuzz_check(fuzz_slots_heard == 1, "one longer than a listing can be is not");

	/* a code's record, looked up for one host's record alone */
	fuzz_reset();
	p2p_signal_lookup_code("WXYZ2345", fuzz_identifier);
	size = 0;
	message[size++] = _message_code;
	message[size++] = RECORD_VERSION;
	memset(message + size, 0x77, P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE);
	size += P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE;
	fuzz_publish(signalling.lookup_topic, sealed, p2p_seal(signalling.lookup_key, message, size, sealed));
	fuzz_check(signalling.looking_up, "a record of another host than the one looked up for is not taken");
	p2p_key_hash(fuzz_public_key, message + 2);
	fuzz_publish(signalling.lookup_topic, sealed, p2p_seal(signalling.lookup_key, message, size, sealed));
	fuzz_check(!signalling.looking_up, "that host's own is");

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

	fuzz_tls_checks();
	fuzz_crossplay_checks();
	net_fuzz_crossplay = 0;
	fuzz_reset();
	return fuzz_failures;
}
