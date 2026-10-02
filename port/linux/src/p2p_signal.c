/*
P2P_SIGNAL.C

Internet play's signalling (p2p.c): how a joiner and the host of an invite
tell each other where they can be reached, through public MQTT brokers
(network.signalling_brokers; MQTT 3.1.1 over TCP). Every broker is used at
once, so any one of them working is enough.

Everything that passes through them is sealed with a key derived from the
invite's token, and goes to topics that are hashes of it, so the brokers
(and anyone watching them) learn nothing and can join nothing:

- the host listens on hceu/1/<HMAC(token, "host" | host)>, where a joiner
  sends JOIN: its identifier, a nonce, and its addresses;
- the joiner listens on hceu/1/<HMAC(token, "joiner" | joiner)>, where the
  host answers ACCEPT: the nonce, a new random key for their tunnel, and
  its own addresses.

A joiner repeats its JOIN until the tunnel reaches the host.

Two ways to find an invite without being sent its link, for machines that
cannot paste one (the Vita) or for strangers:

- A short code (ABCD-EFGH: 40 bits from 32 letters and digits that cannot
  be mistaken for each other). While it hosts, a machine keeps its invite
  on the brokers as a retained message on hceu/1/<HMAC(code token,
  "code")>, sealed with a key from the code; a machine given the code
  subscribes there, gets the retained message at once, and joins the
  invite in it. The code is only as secret as 40 bits: anyone who collects
  the sealed records and tries every code finds the invites, so a code is
  a convenience, not a lock (the link's 128-bit token is). Nothing the code
  leads to is more than the invite, which still goes through JOIN/ACCEPT.
- A public lobby: a host that chose to be listed also keeps, retained and
  unsealed, hceu/1/lobby/<its identifier>: the network version, its code,
  a name and its machine count, sent again every LOBBY_INTERVAL. Its MQTT
  will (sent by the broker when the connection drops) is an empty retained
  message there, which removes the entry; a host that stops being listed
  sends that itself. A machine browsing subscribes to hceu/1/lobby/+ and
  lists what arrives; an entry the broker kept (retained) but that is not
  sent again within LOBBY_STALE_TIME is dropped, so a host that vanished
  without its will reaching the broker does not stay listed.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "p2p_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	MAXIMUM_BROKERS = 4,
	/* the joiners a host remembers, so a repeated request gets the same
	key: as many as it takes */
	MAXIMUM_JOINERS = P2P_MAXIMUM_PEERS + 1,
	TOPIC_SIZE = 7 + 32 + 1,
	NONCE_SIZE = 8,
	BUFFER_SIZE = 4096,
	MAXIMUM_MESSAGE_SIZE = 256,

	/* milliseconds */
	CONNECT_TIMEOUT = 10000,
	RETRY_INTERVAL = 15000,
	KEEP_ALIVE_SECONDS = 60,
	PING_INTERVAL = 30000,
	SILENCE_TIMEOUT = 90000,
	JOIN_INTERVAL = 2000,
	ANSWER_INTERVAL = 1000,
	/* a hosting machine's code record and lobby entry are sent again this
	often (a broker that restarted forgets retained messages) */
	CODE_INTERVAL = 60000,
	LOBBY_INTERVAL = 20000,
	LOBBY_STALE_TIME = 2 * LOBBY_INTERVAL + 5000,
	MAXIMUM_LOBBIES = 32,
	LOBBY_NAME_SIZE = 32,
};

/* the topics a broker can be subscribed to at once */
enum
{
	_topic_host,
	_topic_join,
	_topic_code,
	_topic_lobbies,
	NUMBER_OF_TOPICS,
};

#define LOBBY_TOPIC_PREFIX "hceu/1/lobby/"

enum
{
	_broker_idle,
	_broker_connecting,
	_broker_awaiting_acknowledgement,
	_broker_ready,
};

enum
{
	_message_join = 'J',
	_message_accept = 'A',
	/* a code's record: the host's identifier and the invite's token */
	_message_code = 'C',
	/* a lobby entry (not sealed) */
	_message_lobby = 'L',
	MESSAGE_VERSION = 1,
};

struct broker
{
	char host[128];
	unsigned short port;
	unsigned long address;
	int looked_up;
	int socket;
	int state;
	unsigned long state_time;
	unsigned long sent_time;
	unsigned long heard_time;
	int failures;
	unsigned short packet_identifier;
	/* the topics it has been asked for (_topic_host ...) */
	char topics[NUMBER_OF_TOPICS][TOPIC_SIZE];
	unsigned char input[BUFFER_SIZE];
	int input_size;
	unsigned char output[BUFFER_SIZE];
	int output_size;
};

struct joiner
{
	unsigned char identifier[P2P_IDENTIFIER_SIZE];
	unsigned char nonce[NONCE_SIZE];
	unsigned char key[P2P_SHA256_SIZE];
	unsigned long answered_time;
	int used;
};

static struct
{
	int started;
	struct broker brokers[MAXIMUM_BROKERS];
	int broker_count;
	char client_identifier[24];

	/* hosting */
	int hosting;
	unsigned char host_token[P2P_TOKEN_SIZE];
	unsigned char host_key[P2P_SHA256_SIZE];
	char host_topic[TOPIC_SIZE];
	struct joiner joiners[MAXIMUM_JOINERS];
	int next_joiner;

	/* joining */
	int joining;
	unsigned char join_host[P2P_IDENTIFIER_SIZE];
	unsigned char join_key[P2P_SHA256_SIZE];
	unsigned char join_nonce[NONCE_SIZE];
	char join_host_topic[TOPIC_SIZE];
	char join_topic[TOPIC_SIZE];
	unsigned long join_sent_time;

	/* hosting: the code's record, kept on the brokers */
	int has_code;
	char code_topic[TOPIC_SIZE];
	unsigned char code_key[P2P_SHA256_SIZE];
	unsigned long code_sent_time;
	/* hosting: the lobby entry, while listed */
	int listed;
	char lobby_topic[TOPIC_SIZE];
	char lobby_code[P2P_CODE_SIZE];
	char lobby_name[LOBBY_NAME_SIZE];
	int lobby_players, lobby_maximum;
	unsigned long lobby_sent_time;

	/* looking up a code */
	int looking_up;
	char lookup_topic[TOPIC_SIZE];
	unsigned char lookup_key[P2P_SHA256_SIZE];

	/* browsing the lobbies */
	int browsing;
	struct lobby
	{
		int used;
		char identifier[2 * P2P_IDENTIFIER_SIZE + 1];
		char code[P2P_CODE_SIZE];
		char name[LOBBY_NAME_SIZE];
		int players, maximum, network_version;
		unsigned long heard_time;
	} lobbies[MAXIMUM_LOBBIES];
} signalling;

static int elapsed(unsigned long since, unsigned long time)
{
	return (long)(p2p_now() - since) >= (long)time;
}

static unsigned short network_short(unsigned short value)
{
	return (unsigned short)(value << 8 | value >> 8);
}

/* ---------- what is derived from a token */

static void derive(const unsigned char *token, const char *label, const unsigned char *identifier,
	unsigned char *digest)
{
	unsigned char data[16 + P2P_IDENTIFIER_SIZE];
	int size = (int)strlen(label);

	memcpy(data, label, (size_t)size);
	if (identifier)
	{
		memcpy(data + size, identifier, P2P_IDENTIFIER_SIZE);
		size += P2P_IDENTIFIER_SIZE;
	}
	p2p_hmac_sha256(token, P2P_TOKEN_SIZE, data, size, digest);
}

static void make_topic(const unsigned char *token, const char *label, const unsigned char *identifier,
	char *topic)
{
	unsigned char digest[P2P_SHA256_SIZE];
	char text[2 * P2P_SHA256_SIZE + 1];

	derive(token, label, identifier, digest);
	p2p_hex(digest, 16, text);
	snprintf(topic, TOPIC_SIZE, "hceu/1/%s", text);
}

/* ---------- MQTT */

static void broker_close(struct broker *broker, int failed)
{
	if (broker->socket >= 0)
		posix_socket_close(broker->socket);
	broker->socket = -1;
	broker->state = _broker_idle;
	broker->state_time = p2p_now();
	broker->input_size = 0;
	broker->output_size = 0;
	memset(broker->topics, 0, sizeof(broker->topics));
	if (failed)
		broker->failures++;
}

static void broker_flush(struct broker *broker)
{
	while (broker->output_size > 0)
	{
		int sent = posix_socket_send(broker->socket, broker->output, broker->output_size, 0);

		if (sent < 0)
		{
			int error = posix_socket_last_error();

			if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS)
				broker_close(broker, 1);
			return;
		}
		memmove(broker->output, broker->output + sent, (size_t)(broker->output_size - sent));
		broker->output_size -= sent;
	}
}

/* queues a packet: its fixed header's first byte, and its body */
static void broker_send(struct broker *broker, unsigned char type, const unsigned char *body, int size)
{
	unsigned char header[5];
	int header_size = 1;
	int remaining = size;

	if (broker->socket < 0)
		return;
	header[0] = type;
	do
	{
		unsigned char byte = (unsigned char)(remaining & 127);

		remaining >>= 7;
		header[header_size++] = (unsigned char)(remaining ? byte | 128 : byte);
	} while (remaining);
	if (broker->output_size + header_size + size > BUFFER_SIZE)
	{
		broker_close(broker, 1);
		return;
	}
	memcpy(broker->output + broker->output_size, header, (size_t)header_size);
	memcpy(broker->output + broker->output_size + header_size, body, (size_t)size);
	broker->output_size += header_size + size;
	broker->sent_time = p2p_now();
	broker_flush(broker);
}

static int put_string(unsigned char *body, const char *text)
{
	int size = (int)strlen(text);

	body[0] = (unsigned char)(size >> 8);
	body[1] = (unsigned char)size;
	memcpy(body + 2, text, (size_t)size);
	return size + 2;
}

static void broker_topic(struct broker *broker, const char *topic, int subscribe)
{
	unsigned char body[4 + TOPIC_SIZE + 1];
	int size = 0;

	if (++broker->packet_identifier == 0)
		broker->packet_identifier = 1;
	body[size++] = (unsigned char)(broker->packet_identifier >> 8);
	body[size++] = (unsigned char)broker->packet_identifier;
	size += put_string(body + size, topic);
	if (subscribe)
		body[size++] = 0; /* at most once */
	broker_send(broker, subscribe ? 0x82 : 0xA2, body, size);
}

/* a PUBLISH at most once; retained: the broker keeps it for later
subscribers (an empty retained one removes what it kept) */
static void broker_publish(struct broker *broker, const char *topic, const unsigned char *payload, int payload_size,
	int retain)
{
	unsigned char body[2 + TOPIC_SIZE + MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD];
	int size = put_string(body, topic);

	if (payload_size)
		memcpy(body + size, payload, (size_t)payload_size);
	broker_send(broker, retain ? 0x31 : 0x30, body, size + payload_size);
}

/* the topics a ready broker should be subscribed to */
static void broker_sync_topics(struct broker *broker)
{
	const char *wanted[NUMBER_OF_TOPICS];
	int index;

	if (broker->state != _broker_ready)
		return;
	wanted[_topic_host] = signalling.hosting ? signalling.host_topic : "";
	wanted[_topic_join] = signalling.joining ? signalling.join_topic : "";
	wanted[_topic_code] = signalling.looking_up ? signalling.lookup_topic : "";
	wanted[_topic_lobbies] = signalling.browsing ? LOBBY_TOPIC_PREFIX "+" : "";
	for (index = 0; index < NUMBER_OF_TOPICS; index++)
	{
		char *had = broker->topics[index];

		if (!strcmp(wanted[index], had))
			continue;
		if (had[0])
			broker_topic(broker, had, 0);
		if (wanted[index][0])
			broker_topic(broker, wanted[index], 1);
		strcpy(had, wanted[index]);
	}
}

static void publish_everywhere(const char *topic, const unsigned char *payload, int size, int retain)
{
	int index;

	for (index = 0; index < signalling.broker_count; index++)
	{
		if (signalling.brokers[index].state == _broker_ready)
			broker_publish(&signalling.brokers[index], topic, payload, size, retain);
	}
}

/* this machine's lobby entry's topic (its will empties it) */
static void own_lobby_topic(char *topic)
{
	char text[2 * P2P_IDENTIFIER_SIZE + 1];

	p2p_hex(p2p_identifier(), P2P_IDENTIFIER_SIZE, text);
	snprintf(topic, TOPIC_SIZE, LOBBY_TOPIC_PREFIX "%s", text);
}

static void broker_connected(struct broker *broker)
{
	unsigned char body[128];
	char will_topic[TOPIC_SIZE];
	int size = 0;

	size += put_string(body, "MQTT");
	body[size++] = 4; /* 3.1.1 */
	/* a clean session; a will, retained: when the connection drops, the
	broker empties this machine's lobby entry, so a host that crashed or
	lost its network is not listed (a harmless no-op if it never was) */
	body[size++] = 0x02 | 0x04 | 0x20;
	body[size++] = 0;
	body[size++] = KEEP_ALIVE_SECONDS;
	size += put_string(body + size, signalling.client_identifier);
	own_lobby_topic(will_topic);
	size += put_string(body + size, will_topic);
	/* (the will's message: empty) */
	body[size++] = 0;
	body[size++] = 0;
	broker->state = _broker_awaiting_acknowledgement;
	broker->state_time = p2p_now();
	broker_send(broker, 0x10, body, size);
}

static void broker_connect(struct broker *broker)
{
	struct sockaddr_in address;

	if (!broker->looked_up || (!broker->address && broker->failures > 0))
	{
		/* may wait for DNS; only at the start, or after failing */
		broker->address = p2p_resolve(broker->host);
		broker->looked_up = 1;
		if (!broker->address)
		{
			if (!broker->failures)
				platform_log("Internet play: cannot look up the signalling broker %s", broker->host);
			broker_close(broker, 1);
			return;
		}
	}
	broker->socket = posix_socket(AF_INET, SOCK_STREAM, 0);
	if (broker->socket < 0)
	{
		broker_close(broker, 1);
		return;
	}
	posix_socket_set_nonblocking(broker->socket, 1);
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = broker->port;
	address.sin_addr.s_addr = broker->address;
	broker->state = _broker_connecting;
	broker->state_time = p2p_now();
	if (posix_socket_connect(broker->socket, &address, sizeof(address)) == 0)
	{
		broker_connected(broker);
	}
	else
	{
		int error = posix_socket_last_error();

		if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS)
			broker_close(broker, 1);
	}
}

/* ---------- the messages */

static int put_candidates(unsigned char *message)
{
	struct p2p_candidate candidates[P2P_MAXIMUM_CANDIDATES];
	int count = p2p_local_candidates(candidates, P2P_MAXIMUM_CANDIDATES);
	int index;

	message[0] = (unsigned char)count;
	for (index = 0; index < count; index++)
	{
		memcpy(message + 1 + index * 6, &candidates[index].address, 4);
		memcpy(message + 1 + index * 6 + 4, &candidates[index].port, 2);
	}
	return 1 + count * 6;
}

static int get_candidates(const unsigned char *message, int size, struct p2p_candidate *candidates)
{
	int count;
	int index;

	if (size < 1)
		return -1;
	count = message[0];
	if (count > P2P_MAXIMUM_CANDIDATES || size < 1 + count * 6)
		return -1;
	for (index = 0; index < count; index++)
	{
		unsigned int address;

		memcpy(&address, message + 1 + index * 6, 4);
		candidates[index].address = address;
		memcpy(&candidates[index].port, message + 1 + index * 6 + 4, 2);
	}
	return count;
}

static void send_join(void)
{
	unsigned char message[MAXIMUM_MESSAGE_SIZE];
	unsigned char sealed[MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD];
	int size = 0;

	message[size++] = _message_join;
	message[size++] = MESSAGE_VERSION;
	memcpy(message + size, p2p_identifier(), P2P_IDENTIFIER_SIZE);
	size += P2P_IDENTIFIER_SIZE;
	memcpy(message + size, signalling.join_nonce, NONCE_SIZE);
	size += NONCE_SIZE;
	size += put_candidates(message + size);
	size = p2p_seal(signalling.join_key, message, size, sealed);
	publish_everywhere(signalling.join_host_topic, sealed, size, 0);
	signalling.join_sent_time = p2p_now();
}

/* the host: a joiner asked */
static void join_received(const unsigned char *message, int size)
{
	struct p2p_candidate candidates[P2P_MAXIMUM_CANDIDATES];
	unsigned char answer[MAXIMUM_MESSAGE_SIZE];
	unsigned char sealed[MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD];
	const unsigned char *identifier = message + 2;
	const unsigned char *nonce = identifier + P2P_IDENTIFIER_SIZE;
	struct joiner *joiner = NULL;
	char topic[TOPIC_SIZE];
	int count;
	int index;
	int answer_size = 0;

	if (size < 2 + P2P_IDENTIFIER_SIZE + NONCE_SIZE + 1)
		return;
	count = get_candidates(nonce + NONCE_SIZE, size - (2 + P2P_IDENTIFIER_SIZE + NONCE_SIZE), candidates);
	if (count < 0)
		return;
	for (index = 0; index < MAXIMUM_JOINERS; index++)
	{
		if (signalling.joiners[index].used &&
			!memcmp(signalling.joiners[index].identifier, identifier, P2P_IDENTIFIER_SIZE))
		{
			joiner = &signalling.joiners[index];
			break;
		}
	}
	if (joiner && !memcmp(joiner->nonce, nonce, NONCE_SIZE))
	{
		/* the same request again, through another broker or repeated */
		if (!elapsed(joiner->answered_time, ANSWER_INTERVAL))
			return;
	}
	else
	{
		if (!joiner)
		{
			joiner = &signalling.joiners[signalling.next_joiner++ % MAXIMUM_JOINERS];
			memcpy(joiner->identifier, identifier, P2P_IDENTIFIER_SIZE);
			joiner->used = 1;
		}
		memcpy(joiner->nonce, nonce, NONCE_SIZE);
		posix_random_bytes(joiner->key, sizeof(joiner->key));
	}
	joiner->answered_time = p2p_now();
	p2p_peer_offered(identifier, joiner->key, candidates, count, 0);

	answer[answer_size++] = _message_accept;
	answer[answer_size++] = MESSAGE_VERSION;
	memcpy(answer + answer_size, p2p_identifier(), P2P_IDENTIFIER_SIZE);
	answer_size += P2P_IDENTIFIER_SIZE;
	memcpy(answer + answer_size, identifier, P2P_IDENTIFIER_SIZE);
	answer_size += P2P_IDENTIFIER_SIZE;
	memcpy(answer + answer_size, nonce, NONCE_SIZE);
	answer_size += NONCE_SIZE;
	memcpy(answer + answer_size, joiner->key, P2P_SHA256_SIZE);
	answer_size += P2P_SHA256_SIZE;
	answer_size += put_candidates(answer + answer_size);
	answer_size = p2p_seal(signalling.host_key, answer, answer_size, sealed);
	make_topic(signalling.host_token, "joiner", identifier, topic);
	publish_everywhere(topic, sealed, answer_size, 0);
}

/* the joiner: the host answered */
static void accept_received(const unsigned char *message, int size)
{
	struct p2p_candidate candidates[P2P_MAXIMUM_CANDIDATES];
	const unsigned char *host = message + 2;
	const unsigned char *joiner = host + P2P_IDENTIFIER_SIZE;
	const unsigned char *nonce = joiner + P2P_IDENTIFIER_SIZE;
	const unsigned char *key = nonce + NONCE_SIZE;
	int fixed = 2 + 2 * P2P_IDENTIFIER_SIZE + NONCE_SIZE + P2P_SHA256_SIZE;
	int count;

	if (size < fixed + 1 || memcmp(host, signalling.join_host, P2P_IDENTIFIER_SIZE) ||
		memcmp(joiner, p2p_identifier(), P2P_IDENTIFIER_SIZE) || memcmp(nonce, signalling.join_nonce, NONCE_SIZE))
		return;
	count = get_candidates(message + fixed, size - fixed, candidates);
	if (count >= 0)
		p2p_peer_offered(host, key, candidates, count, 1);
}

/* ---------- codes and lobbies */

/* what a code's topic and key derive from, as a token does: a hash of the
code's eight characters */
static void code_token(const char *code, unsigned char *token)
{
	char text[16 + P2P_CODE_LENGTH];
	unsigned char digest[P2P_SHA256_SIZE];

	memcpy(text, "halo code ", 10);
	memcpy(text + 10, code, P2P_CODE_LENGTH);
	p2p_sha256(text, 10 + P2P_CODE_LENGTH, digest);
	memcpy(token, digest, P2P_TOKEN_SIZE);
}

/* the code's record (the host's identifier and the invite's token), sealed
with the code's key, retained on one broker or all (broker NULL) */
static void publish_code(struct broker *broker)
{
	unsigned char message[2 + P2P_IDENTIFIER_SIZE + P2P_TOKEN_SIZE];
	unsigned char sealed[sizeof(message) + P2P_SEAL_OVERHEAD];
	int size = 0;

	message[size++] = _message_code;
	message[size++] = MESSAGE_VERSION;
	memcpy(message + size, p2p_identifier(), P2P_IDENTIFIER_SIZE);
	size += P2P_IDENTIFIER_SIZE;
	memcpy(message + size, signalling.host_token, P2P_TOKEN_SIZE);
	size += P2P_TOKEN_SIZE;
	size = p2p_seal(signalling.code_key, message, size, sealed);
	if (broker)
		broker_publish(broker, signalling.code_topic, sealed, size, 1);
	else
		publish_everywhere(signalling.code_topic, sealed, size, 1);
	signalling.code_sent_time = p2p_now();
}

/* the lobby entry, retained on one broker or all (broker NULL): the
network version (big-endian), the code, the machine counts, the name */
static void publish_lobby(struct broker *broker)
{
	unsigned char message[6 + P2P_CODE_LENGTH + 2 + LOBBY_NAME_SIZE];
	int size = 0;
	int name_size = (int)strlen(signalling.lobby_name);

	message[size++] = _message_lobby;
	message[size++] = MESSAGE_VERSION;
	message[size++] = (unsigned char)(HALO_PORT_NETWORK_VERSION >> 8);
	message[size++] = (unsigned char)HALO_PORT_NETWORK_VERSION;
	/* (the code's eight characters, without its dash) */
	memcpy(message + size, signalling.lobby_code, 4);
	memcpy(message + size + 4, signalling.lobby_code + 5, 4);
	size += P2P_CODE_LENGTH;
	message[size++] = (unsigned char)signalling.lobby_players;
	message[size++] = (unsigned char)signalling.lobby_maximum;
	message[size++] = (unsigned char)name_size;
	memcpy(message + size, signalling.lobby_name, (size_t)name_size);
	size += name_size;
	if (broker)
		broker_publish(broker, signalling.lobby_topic, message, size, 1);
	else
		publish_everywhere(signalling.lobby_topic, message, size, 1);
	signalling.lobby_sent_time = p2p_now();
}

/* empties a retained topic everywhere */
static void clear_retained(const char *topic)
{
	publish_everywhere(topic, NULL, 0, 1);
}

/* a code looked up: its record names the invite */
static void code_received(const unsigned char *message, int size)
{
	char text[16 + 2 * (P2P_IDENTIFIER_SIZE + P2P_TOKEN_SIZE) + 1];

	if (size < 2 + P2P_IDENTIFIER_SIZE + P2P_TOKEN_SIZE)
		return;
	signalling.looking_up = 0;
	memcpy(text, "halo://join/", 12);
	p2p_hex(message + 2, P2P_IDENTIFIER_SIZE + P2P_TOKEN_SIZE, text + 12);
	{
		int index;

		for (index = 0; index < signalling.broker_count; index++)
			broker_sync_topics(&signalling.brokers[index]);
	}
	p2p_code_found(text);
}

static struct lobby *find_lobby(const char *identifier, int create)
{
	struct lobby *free_entry = NULL;
	int index;

	for (index = 0; index < MAXIMUM_LOBBIES; index++)
	{
		struct lobby *lobby = &signalling.lobbies[index];

		if (lobby->used && !strcmp(lobby->identifier, identifier))
			return lobby;
		if (!lobby->used && !free_entry)
			free_entry = lobby;
	}
	if (!create || !free_entry)
		return NULL;
	memset(free_entry, 0, sizeof(*free_entry));
	free_entry->used = 1;
	strcpy(free_entry->identifier, identifier);
	return free_entry;
}

/* a lobby entry (or its removal: empty), retained (kept by the broker) or
live (sent now) */
static void lobby_received(const char *identifier, const unsigned char *message, int size, int retained)
{
	struct lobby *lobby;
	int name_size;
	int index;

	if (strlen(identifier) != 2 * P2P_IDENTIFIER_SIZE)
		return;
	if (size == 0)
	{
		lobby = find_lobby(identifier, 0);
		if (lobby)
			lobby->used = 0;
		return;
	}
	if (size < 4 + P2P_CODE_LENGTH + 3 || message[0] != _message_lobby || message[1] != MESSAGE_VERSION)
		return;
	name_size = message[4 + P2P_CODE_LENGTH + 2];
	if (size < 4 + P2P_CODE_LENGTH + 3 + name_size || name_size >= LOBBY_NAME_SIZE)
		return;
	lobby = find_lobby(identifier, 1);
	if (!lobby)
		return;
	lobby->network_version = message[2] << 8 | message[3];
	memcpy(lobby->code, message + 4, 4);
	lobby->code[4] = '-';
	memcpy(lobby->code + 5, message + 8, 4);
	lobby->code[9] = 0;
	lobby->players = message[4 + P2P_CODE_LENGTH];
	lobby->maximum = message[4 + P2P_CODE_LENGTH + 1];
	memcpy(lobby->name, message + 4 + P2P_CODE_LENGTH + 3, (size_t)name_size);
	lobby->name[name_size] = 0;
	/* (only what can be shown: the name is any bytes the host sent) */
	for (index = 0; index < name_size; index++)
	{
		if ((unsigned char)lobby->name[index] < 32 || (unsigned char)lobby->name[index] > 126)
			lobby->name[index] = '?';
	}
	/* (a retained entry counts as heard now: one its host no longer sends
	again goes LOBBY_STALE_TIME later) */
	(void)retained;
	lobby->heard_time = p2p_now();
}

static void publish_received(const char *topic, const unsigned char *payload, int size, int retained)
{
	unsigned char message[MAXIMUM_MESSAGE_SIZE];
	int message_size;

	if (size > MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD)
		return;
	if (signalling.hosting && !strcmp(topic, signalling.host_topic))
	{
		message_size = p2p_open(signalling.host_key, payload, size, message);
		if (message_size >= 2 && message[0] == _message_join && message[1] == MESSAGE_VERSION)
			join_received(message, message_size);
	}
	else if (signalling.joining && !strcmp(topic, signalling.join_topic))
	{
		message_size = p2p_open(signalling.join_key, payload, size, message);
		if (message_size >= 2 && message[0] == _message_accept && message[1] == MESSAGE_VERSION)
			accept_received(message, message_size);
	}
	else if (signalling.looking_up && !strcmp(topic, signalling.lookup_topic))
	{
		message_size = size ? p2p_open(signalling.lookup_key, payload, size, message) : -1;
		if (message_size >= 2 && message[0] == _message_code && message[1] == MESSAGE_VERSION)
			code_received(message, message_size);
	}
	else if (signalling.browsing && !strncmp(topic, LOBBY_TOPIC_PREFIX, strlen(LOBBY_TOPIC_PREFIX)))
	{
		lobby_received(topic + strlen(LOBBY_TOPIC_PREFIX), payload, size, retained);
	}
}

/* the packets that arrived whole */
static void broker_parse(struct broker *broker)
{
	for (;;)
	{
		int remaining = 0;
		int shift = 0;
		int header_size = 1;
		int total;
		unsigned char type;

		for (;;)
		{
			unsigned char byte;

			if (header_size >= broker->input_size)
				return;
			byte = broker->input[header_size++];
			remaining |= (byte & 127) << shift;
			shift += 7;
			if (!(byte & 128))
				break;
			if (shift > 21)
			{
				broker_close(broker, 1);
				return;
			}
		}
		total = header_size + remaining;
		if (total > BUFFER_SIZE)
		{
			broker_close(broker, 1);
			return;
		}
		if (total > broker->input_size)
			return;
		type = broker->input[0];
		if ((type & 0xF0) == 0x20 && remaining >= 2)
		{
			/* CONNACK */
			if (broker->input[header_size + 1] != 0)
			{
				platform_log("Internet play: the signalling broker %s refused the connection", broker->host);
				broker_close(broker, 1);
				return;
			}
			broker->state = _broker_ready;
			broker->failures = 0;
			broker_sync_topics(broker);
			/* a joiner's first request need not wait for the next repeat */
			if (signalling.joining)
				send_join();
			/* a host's retained records, on a broker that may never have
			had them (or forgot them) */
			if (signalling.hosting && signalling.has_code)
				publish_code(broker);
			if (signalling.listed)
				publish_lobby(broker);
		}
		else if ((type & 0xF0) == 0x30 && remaining >= 2)
		{
			/* PUBLISH */
			const unsigned char *body = broker->input + header_size;
			int topic_size = body[0] << 8 | body[1];
			int offset = 2 + topic_size + (((type >> 1) & 3) ? 2 : 0);

			if (topic_size < TOPIC_SIZE && offset <= remaining)
			{
				char topic[TOPIC_SIZE];

				memcpy(topic, body + 2, (size_t)topic_size);
				topic[topic_size] = 0;
				publish_received(topic, body + offset, remaining - offset, type & 1);
			}
		}
		memmove(broker->input, broker->input + total, (size_t)(broker->input_size - total));
		broker->input_size -= total;
		if (broker->socket < 0)
			return;
	}
}

static void broker_readable(struct broker *broker)
{
	for (;;)
	{
		int size = posix_socket_recv(broker->socket, broker->input + broker->input_size,
			BUFFER_SIZE - broker->input_size, 0);

		if (size == 0)
		{
			broker_close(broker, 1);
			return;
		}
		if (size < 0)
		{
			int error = posix_socket_last_error();

			if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS)
				broker_close(broker, 1);
			return;
		}
		broker->input_size += size;
		broker->heard_time = p2p_now();
		broker_parse(broker);
		if (broker->socket < 0 || broker->input_size == BUFFER_SIZE)
			return;
	}
}

/* ---------- p2p.c's side */

void p2p_signal_start(void)
{
	const char *text;
	unsigned char random[8];
	char hex[17];

	if (signalling.started)
		return;
	signalling.started = 1;
	posix_random_bytes(random, sizeof(random));
	p2p_hex(random, sizeof(random), hex);
	snprintf(signalling.client_identifier, sizeof(signalling.client_identifier), "hceu-%s", hex);
	text = config_string("network.signalling_brokers");
	while (*text && signalling.broker_count < MAXIMUM_BROKERS)
	{
		const char *end = text + strcspn(text, ",");
		struct broker *broker = &signalling.brokers[signalling.broker_count];
		char *colon;
		int length;

		while (text < end && *text == ' ')
			text++;
		length = (int)(end - text);
		while (length > 0 && text[length - 1] == ' ')
			length--;
		if (length > 0 && length < (int)sizeof(broker->host))
		{
			memset(broker, 0, sizeof(*broker));
			memcpy(broker->host, text, (size_t)length);
			broker->socket = -1;
			broker->port = network_short(1883);
			colon = strchr(broker->host, ':');
			if (colon)
			{
				broker->port = network_short((unsigned short)atoi(colon + 1));
				*colon = 0;
			}
			/* connect at once */
			broker->state_time = p2p_now() - RETRY_INTERVAL;
			signalling.broker_count++;
		}
		text = *end ? end + 1 : end;
	}
	if (!signalling.broker_count)
		platform_log("Internet play: no signalling brokers (network.signalling_brokers), so invites cannot work");
}

void p2p_signal_select_sets(int *read, int *read_count, int *write, int *write_count, int maximum_count)
{
	int index;
	int added = 0;

	for (index = 0; index < signalling.broker_count && added < maximum_count; index++)
	{
		struct broker *broker = &signalling.brokers[index];

		if (broker->socket < 0)
			continue;
		read[(*read_count)++] = broker->socket;
		if (broker->state == _broker_connecting || broker->output_size)
			write[(*write_count)++] = broker->socket;
		added++;
	}
}

static int list_holds(const int *list, int count, int socket)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (list[index] == socket)
			return 1;
	}
	return 0;
}

void p2p_signal_update(const int *read, int read_count, const int *write, int write_count)
{
	int index;

	for (index = 0; index < signalling.broker_count; index++)
	{
		struct broker *broker = &signalling.brokers[index];
		int retry = RETRY_INTERVAL * (broker->failures < 4 ? broker->failures + 1 : 4);

		switch (broker->state)
		{
		case _broker_idle:
			if (elapsed(broker->state_time, (unsigned long)retry))
				broker_connect(broker);
			break;
		case _broker_connecting:
			if (list_holds(write, write_count, broker->socket))
				broker_connected(broker);
			else if (elapsed(broker->state_time, CONNECT_TIMEOUT))
				broker_close(broker, 1);
			break;
		case _broker_awaiting_acknowledgement:
			if (elapsed(broker->state_time, CONNECT_TIMEOUT))
				broker_close(broker, 1);
			break;
		}
		if (broker->socket < 0 || broker->state == _broker_connecting)
			continue;
		if (list_holds(write, write_count, broker->socket))
			broker_flush(broker);
		if (broker->socket >= 0 && list_holds(read, read_count, broker->socket))
			broker_readable(broker);
		if (broker->state != _broker_ready)
			continue;
		if (elapsed(broker->heard_time, SILENCE_TIMEOUT))
		{
			broker_close(broker, 1);
			continue;
		}
		if (elapsed(broker->sent_time, PING_INTERVAL))
			broker_send(broker, 0xC0, NULL, 0);
	}
	if (signalling.joining && elapsed(signalling.join_sent_time, JOIN_INTERVAL))
		send_join();
	if (signalling.hosting && signalling.has_code && elapsed(signalling.code_sent_time, CODE_INTERVAL))
		publish_code(NULL);
	if (signalling.listed && elapsed(signalling.lobby_sent_time, LOBBY_INTERVAL))
		publish_lobby(NULL);
	if (signalling.browsing)
	{
		for (index = 0; index < MAXIMUM_LOBBIES; index++)
		{
			struct lobby *lobby = &signalling.lobbies[index];

			if (lobby->used && elapsed(lobby->heard_time, LOBBY_STALE_TIME))
				lobby->used = 0;
		}
	}
}

int p2p_signal_connected(void)
{
	int index;

	for (index = 0; index < signalling.broker_count; index++)
	{
		if (signalling.brokers[index].state == _broker_ready)
			return 1;
	}
	return 0;
}

static void sync_all_topics(void)
{
	int index;

	for (index = 0; index < signalling.broker_count; index++)
		broker_sync_topics(&signalling.brokers[index]);
}

void p2p_signal_host(const unsigned char *token, const char *code)
{
	memcpy(signalling.host_token, token, P2P_TOKEN_SIZE);
	derive(token, "seal", NULL, signalling.host_key);
	make_topic(token, "host", p2p_identifier(), signalling.host_topic);
	signalling.hosting = 1;
	signalling.has_code = code && strlen(code) == P2P_CODE_SIZE - 1;
	if (signalling.has_code)
	{
		unsigned char token_of_code[P2P_TOKEN_SIZE];
		char characters[P2P_CODE_LENGTH];

		memcpy(characters, code, 4);
		memcpy(characters + 4, code + 5, 4);
		code_token(characters, token_of_code);
		make_topic(token_of_code, "code", NULL, signalling.code_topic);
		derive(token_of_code, "seal", NULL, signalling.code_key);
		publish_code(NULL);
	}
	sync_all_topics();
}

void p2p_signal_stop_hosting(void)
{
	signalling.hosting = 0;
	memset(signalling.joiners, 0, sizeof(signalling.joiners));
	if (signalling.has_code)
		clear_retained(signalling.code_topic);
	signalling.has_code = 0;
	p2p_signal_set_lobby(0, NULL, NULL, 0, 0);
	sync_all_topics();
}

void p2p_signal_set_lobby(int listed, const char *code, const char *name, int players, int maximum)
{
	if (!listed || !code)
	{
		if (signalling.listed)
			clear_retained(signalling.lobby_topic);
		signalling.listed = 0;
		return;
	}
	if (signalling.listed && !strcmp(signalling.lobby_code, code) && !strcmp(signalling.lobby_name, name ? name : "") &&
		signalling.lobby_players == players && signalling.lobby_maximum == maximum)
		return;
	own_lobby_topic(signalling.lobby_topic);
	snprintf(signalling.lobby_code, sizeof(signalling.lobby_code), "%s", code);
	snprintf(signalling.lobby_name, sizeof(signalling.lobby_name), "%s", name && *name ? name : "Halo");
	signalling.lobby_players = players < 255 ? players : 255;
	signalling.lobby_maximum = maximum < 255 ? maximum : 255;
	signalling.listed = 1;
	publish_lobby(NULL);
}

void p2p_signal_lookup_code(const char *code)
{
	unsigned char token[P2P_TOKEN_SIZE];

	code_token(code, token);
	make_topic(token, "code", NULL, signalling.lookup_topic);
	derive(token, "seal", NULL, signalling.lookup_key);
	signalling.looking_up = 1;
	sync_all_topics();
}

void p2p_signal_stop_lookup(void)
{
	signalling.looking_up = 0;
	sync_all_topics();
}

void p2p_signal_browse(int on)
{
	if (on && !signalling.browsing)
		memset(signalling.lobbies, 0, sizeof(signalling.lobbies));
	signalling.browsing = on != 0;
	sync_all_topics();
}

int p2p_signal_lobby_entry(int index, struct p2p_lobby_entry *entry)
{
	int slot;

	/* the index-th entry in the table (whose order is arrival) */
	for (slot = 0; slot < MAXIMUM_LOBBIES; slot++)
	{
		struct lobby const *lobby = &signalling.lobbies[slot];

		if (!lobby->used || index-- > 0)
			continue;
		memset(entry, 0, sizeof(*entry));
		snprintf(entry->code, sizeof(entry->code), "%s", lobby->code);
		snprintf(entry->name, sizeof(entry->name), "%s", lobby->name);
		entry->players = lobby->players;
		entry->maximum = lobby->maximum;
		entry->compatible = lobby->network_version == HALO_PORT_NETWORK_VERSION;
		{
			char own[2 * P2P_IDENTIFIER_SIZE + 1];

			p2p_hex(p2p_identifier(), P2P_IDENTIFIER_SIZE, own);
			entry->own = !strcmp(lobby->identifier, own);
		}
		return 1;
	}
	return 0;
}

void p2p_signal_join(const unsigned char *host_identifier, const unsigned char *token)
{
	memcpy(signalling.join_host, host_identifier, P2P_IDENTIFIER_SIZE);
	derive(token, "seal", NULL, signalling.join_key);
	make_topic(token, "host", host_identifier, signalling.join_host_topic);
	make_topic(token, "joiner", p2p_identifier(), signalling.join_topic);
	posix_random_bytes(signalling.join_nonce, NONCE_SIZE);
	signalling.joining = 1;
	sync_all_topics();
	send_join();
}

void p2p_signal_stop_joining(void)
{
	signalling.joining = 0;
	sync_all_topics();
}
