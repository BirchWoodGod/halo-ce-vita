/*
P2P_FRIENDS_FUZZ.C

A fuzzer of friends (port/linux/src/p2p_friends.c): every message the
brokers can carry to a machine's inbox and mailbox, and both of its files,
with ASan and UBSan (run_p2p_friends_test.sh fuzz). The platform layer is
32-bit only and this clang has no 32-bit libFuzzer, so main below is its own
engine (mutations of real messages and files: bit flips, byte and length
changes, cuts, growth, splices); built with -DUSE_LIBFUZZER,
LLVMFuzzerTestOneInput serves libFuzzer as it is. The machine under test
(T) has a friend (F), a request sent to a stranger (S) and S's request
pending, so that every handler is reached. The input's first byte says how:

	0  the rest, as it came, on T's inbox
	1  the rest, as it came, on T's mailbox
	2  the rest as a request's plaintext from S, its header S's, signed by S
	   and sealed with T's code's key (the request's every layout a sender
	   could sign)
	3  the rest as a pair message's plaintext from F (presence, invite,
	   remove), signed by F and sealed with the pair's key
	4  the rest as an accept's plaintext from S, signed by S, in a box to T
	5  the rest as T's list file (its hash made right with the second
	   byte's 1)
	6  the rest as T's identity file (its hash made right likewise)

and its second byte its flags: the clock stepped, the header's type and
recipient kept from the input, the list read after. After each input the
lists are read and every text checked bounded and printable.
*/

#define P2P_FRIENDS_FUZZ
#include "p2p_friends_test.c"

#include <stdint.h>

enum
{
	T = 0,
	F = 1,
	S = 2,
	MAXIMUM_INPUT = 2 + MAXIMUM_LIST_SIZE + 64,
};

static unsigned long inputs, lists_taken, requests_taken, friends_seen;
static char fuzz_root[512];

/* the three machines as the fuzz wants them: T and F friends, T's request
to S pending, S's to T pending */
static void setup(void)
{
	static int count;
	unsigned char friendship[FRIENDSHIP_LENGTH];
	int index;

	quiet = 1;
	for (index = 0; index < MACHINES; index++)
	{
		memset(&machines[index], 0, sizeof(machines[index]));
		snprintf(roots[index], sizeof(roots[index]), "%s/m%d_%d", fuzz_root, index, count & 7);
		mkdir(roots[index], 0755);
		hosts[index].online = 1;
		use(index);
		/* (the files of a run before are left: each machine starts as read) */
		{
			char code[P2P_FRIEND_CODE_SIZE];

			p2p_friends_code(code, sizeof(code));
		}
		friends->friend_count = friends->request_count = friends->outgoing_count = 0;
		friends->blocked_count = friends->tombstone_count = friends->muted_count = friends->invite_count = 0;
	}
	count++;
	memset(friendship, 7, sizeof(friendship));
	use(T);
	friend_add(machines[F].key, "Foxtrot", friendship, 0, 0);
	{
		struct outgoing_record *record = &machines[T].outgoing[machines[T].outgoing_count++];

		memset(record, 0, sizeof(*record));
		memcpy(record->code, machines[S].code, sizeof(record->code));
		memset(record->nonce, 0x33, NONCE_LENGTH);
	}
	memcpy(machines[T].requests[0].key, machines[S].key, P2P_KEY_SIZE);
	snprintf(machines[T].requests[0].name, sizeof(machines[T].requests[0].name), "Sierra");
	machines[T].request_count = 1;
	use(F);
	friend_add(machines[T].key, "Tango", friendship, 0, 0);
	use(T);
}

static void check_entries(void)
{
	struct p2p_friend_entry entries[P2P_FRIEND_ENTRIES_MAXIMUM];
	char text[128];
	int count, index, at;

	use(T);
	count = p2p_friends_list(entries, P2P_FRIEND_ENTRIES_MAXIMUM);
	if (count < 0 || count > P2P_FRIEND_ENTRIES_MAXIMUM)
		__builtin_trap();
	friends_seen += count;
	for (index = 0; index < count; index++)
	{
		const struct p2p_friend_entry *entry = &entries[index];
		const char *texts[4];
		int which;

		texts[0] = entry->id;
		texts[1] = entry->name;
		texts[2] = entry->code;
		texts[3] = entry->activity;
		if (memchr(entry->id, 0, sizeof(entry->id)) == NULL || memchr(entry->name, 0, sizeof(entry->name)) == NULL ||
			memchr(entry->code, 0, sizeof(entry->code)) == NULL || memchr(entry->activity, 0, sizeof(entry->activity)) == NULL)
			__builtin_trap();
		for (which = 0; which < 4; which++)
			for (at = 0; texts[which][at]; at++)
				if (texts[which][at] < 0x20 || texts[which][at] > 0x7E)
					__builtin_trap();
		if (entry->kind < 0 || entry->kind >= P2P_FRIEND_KIND_COUNT || entry->state < 0 ||
			entry->state >= P2P_FRIEND_STATE_COUNT || entry->players > entry->maximum)
			__builtin_trap();
	}
	p2p_friends_status(text, sizeof(text));
	if (p2p_friends_take_invite(&entries[0]) && entries[0].kind != P2P_FRIEND_KIND_INVITE)
		__builtin_trap();
	if (machines[T].friend_count > P2P_FRIENDS_MAXIMUM || machines[T].request_count > P2P_FRIEND_REQUESTS_MAXIMUM ||
		machines[T].invite_count > P2P_FRIEND_INVITES_MAXIMUM || machines[T].inbound_count > MAXIMUM_INBOUND ||
		machines[T].outbound_count > MAXIMUM_OUTBOUND)
		__builtin_trap();
}

/* a plaintext from the input, its header the sender's (with the flag: the
input's type and recipient kept), signed by the sender */
static int plain_from(int sender, int type, const unsigned char *recipient, const uint8_t *data, size_t size,
	int keep, unsigned char *plain)
{
	int body = size > MAXIMUM_BODY_SIZE + 16 ? MAXIMUM_BODY_SIZE + 16 : (int)size;
	int plain_size = PLAIN_HEADER_SIZE + body;
	unsigned char signed_data[sizeof(signature_label) + MAXIMUM_PLAIN_SIZE + 64];

	memset(plain, 0, PLAIN_HEADER_SIZE);
	plain[0] = (unsigned char)type;
	memcpy(plain + 1, machines[sender].key, P2P_KEY_SIZE);
	if (recipient)
		memcpy(plain + 1 + P2P_KEY_SIZE, recipient, P2P_KEY_SIZE);
	put_u64(plain + 1 + 2 * P2P_KEY_SIZE, machines[sender].epoch);
	put_u32(plain + 1 + 2 * P2P_KEY_SIZE + 8, ++machines[sender].counter);
	if (keep && size > PLAIN_HEADER_SIZE)
	{
		plain[0] = data[0];
		memcpy(plain + 1 + P2P_KEY_SIZE, data + 1, P2P_KEY_SIZE);
	}
	memcpy(plain + PLAIN_HEADER_SIZE, data, (size_t)body);
	memcpy(signed_data, signature_label, sizeof(signature_label) - 1);
	memcpy(signed_data + sizeof(signature_label) - 1, plain, (size_t)plain_size);
	p2p_ed25519_sign(machines[sender].seed, machines[sender].key, signed_data,
		(int)sizeof(signature_label) - 1 + plain_size, plain + plain_size);
	return plain_size + P2P_SIGNATURE_SIZE;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	unsigned char plain[MAXIMUM_PLAIN_SIZE + 128], payload[MAXIMUM_PAYLOAD_SIZE + 256];
	int mode, flags, payload_size = 0, which = P2P_FRIENDS_MAILBOX;

	if (!fuzz_root[0])
		snprintf(fuzz_root, sizeof(fuzz_root), "%s", getenv("FRIENDS_TEST_ROOT") ? getenv("FRIENDS_TEST_ROOT") : ".");
	if (inputs++ % 5000 == 0)
		setup();
	if (size < 2)
		return 0;
	mode = data[0] % 7;
	flags = data[1];
	data += 2;
	size -= 2;
	use(T);
	switch (mode)
	{
	case 0:
	case 1:
		which = mode == 0 ? P2P_FRIENDS_INBOX : P2P_FRIENDS_MAILBOX;
		payload_size = size > sizeof(payload) ? (int)sizeof(payload) : (int)size;
		memcpy(payload, data, (size_t)payload_size);
		break;
	case 2:
	{
		int plain_size = plain_from(S, TYPE_REQUEST, machines[T].code_recipient, data, size, flags & 2, plain);

		use(T);
		payload_size = envelope_seal(KIND_REQUEST, machines[T].inbox_key, plain, plain_size > MAXIMUM_PLAIN_SIZE ?
			MAXIMUM_PLAIN_SIZE : plain_size, payload);
		which = P2P_FRIENDS_INBOX;
		break;
	}
	case 3:
	{
		static const unsigned char none[P2P_KEY_SIZE];
		int types[3] = { TYPE_PRESENCE, TYPE_INVITE, TYPE_REMOVE };
		int type = types[(flags >> 4) % 3];
		int plain_size = plain_from(F, type, type == TYPE_PRESENCE ? none : machines[T].key, data, size, flags & 2, plain);

		use(F);
		if (!machines[F].friend_count || !friend_keys(&machines[F].friends[0]))
			return 0;
		payload_size = envelope_seal(KIND_PAIR, machines[F].friends[0].send_key, plain, plain_size > MAXIMUM_PLAIN_SIZE ?
			MAXIMUM_PLAIN_SIZE : plain_size, payload);
		use(T);
		break;
	}
	case 4:
	{
		int plain_size = plain_from(S, TYPE_ACCEPT, machines[T].key, data, size, flags & 2, plain);

		use(S);
		payload_size = box_seal(machines[T].key, plain, plain_size > MAXIMUM_PLAIN_SIZE ? MAXIMUM_PLAIN_SIZE : plain_size,
			payload);
		use(T);
		break;
	}
	case 5:
	case 6:
	{
		unsigned char bytes[MAXIMUM_LIST_SIZE + 64];
		int length = size > sizeof(bytes) - P2P_SHA256_SIZE ? (int)(sizeof(bytes) - P2P_SHA256_SIZE) : (int)size;
		struct friends_state saved = machines[T];

		memcpy(bytes, data, (size_t)length);
		if ((flags & 1) && length >= P2P_SHA256_SIZE + 5)
		{
			memcpy(bytes, mode == 5 ? "HFRL" : "HFID", 4);
			bytes[4] = mode == 5 ? LIST_VERSION : IDENTITY_VERSION;
			p2p_sha256(bytes, length - P2P_SHA256_SIZE, bytes + length - P2P_SHA256_SIZE);
		}
		use(T);
		if (mode == 5)
		{
			lists_taken += list_decode(bytes, length);
			check_entries();
		}
		else
		{
			unsigned char seed[P2P_SEED_SIZE];
			unsigned long long epoch;

			identity_decode(bytes, length, seed, &epoch);
		}
		machines[T] = saved;
		return 0;
	}
	}
	if (payload_size > 0)
		p2p_friends_heard(which, payload, payload_size);
	clock_now += flags & 1 ? 4000 : 50;
	use(T);
	p2p_friends_update(&hosts[T]);
	requests_taken += machines[T].request_count;
	machines[T].outbound_count = 0;
	if (flags & 4)
		check_entries();
	return 0;
}

/* ---------- the engine (without libFuzzer), and the seeds */

#ifndef USE_LIBFUZZER
static unsigned long long state = 88172645463325252ULL;

static unsigned int next_random(void)
{
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return (unsigned int)(state >> 11);
}

enum
{
	SEEDS = 32,
};

static unsigned char seeds[SEEDS][MAXIMUM_INPUT];
static int seed_sizes[SEEDS];

/* the bodies of real messages (each mode's plaintext after its header), and
real files */
static void make_seeds(void)
{
	struct message message;
	unsigned char plain[MAXIMUM_PLAIN_SIZE], bytes[MAXIMUM_LIST_SIZE];
	int index, size;

	setup();
	for (index = 0; index < SEEDS; index++)
	{
		int mode = index % 7;

		seeds[index][0] = (unsigned char)mode;
		seeds[index][1] = (unsigned char)(index * 37);
		use(F);
		memset(&message, 0, sizeof(message));
		message.game.difficulty = -1;
		snprintf(message.name, sizeof(message.name), index & 1 ? "Name|x" : "Foxtrot");
		snprintf(message.game.map, sizeof(message.game.map), index & 2 ? "bloodgulch" : "a10");
		snprintf(message.game.gametype, sizeof(message.game.gametype), "Slayer");
		switch (mode)
		{
		case 2:
			message.type = TYPE_REQUEST;
			break;
		case 3:
			message.type = index & 4 ? TYPE_PRESENCE : index & 8 ? TYPE_INVITE : TYPE_REMOVE;
			message.flags = PRESENCE_ONLINE | (index & 16 ? PRESENCE_JOINABLE : 0);
			message.game.state = P2P_FRIEND_STATE_MULTIPLAYER;
			message.game.maximum = 16;
			memset(message.friendship, 7, FRIENDSHIP_LENGTH);
			seeds[index][1] = (unsigned char)((message.type == TYPE_PRESENCE ? 0 : message.type == TYPE_INVITE ? 1 : 2) << 4);
			break;
		case 4:
			message.type = TYPE_ACCEPT;
			memset(message.nonce, 0x33, NONCE_LENGTH);
			break;
		default:
			message.type = TYPE_PRESENCE;
			break;
		}
		size = body_encode(&message, plain);
		if (mode == 5)
		{
			use(T);
			size = list_encode(bytes);
			memcpy(plain, bytes, (size_t)size);
		}
		else if (mode == 6)
			size = identity_encode(machines[T].seed, 12345, plain);
		else if (mode < 2)
		{
			/* (a real sealed message, as T would hear it) */
			use(F);
			message_start(&message, TYPE_PRESENCE, NULL);
			message.flags = PRESENCE_ONLINE;
			message.game.difficulty = -1;
			size = plain_encode(&message, plain);
			friend_keys(&machines[F].friends[0]);
			size = envelope_seal(KIND_PAIR, machines[F].friends[0].send_key, plain, size, bytes);
			memcpy(plain, bytes, (size_t)size);
		}
		memcpy(seeds[index] + 2, plain, (size_t)size);
		seed_sizes[index] = 2 + size;
	}
}

int main(int count, char **arguments)
{
	unsigned long runs = count > 1 ? strtoul(arguments[1], NULL, 10) : 300000;
	unsigned long run;
	static unsigned char input[MAXIMUM_INPUT];

	snprintf(fuzz_root, sizeof(fuzz_root), "%s", getenv("FRIENDS_TEST_ROOT") ? getenv("FRIENDS_TEST_ROOT") : ".");
	make_seeds();
	for (run = 0; run < runs; run++)
	{
		int size, mutations, index;
		int seed = (int)(next_random() % SEEDS);

		memcpy(input, seeds[seed], (size_t)seed_sizes[seed]);
		size = seed_sizes[seed];
		mutations = 1 + (int)(next_random() % 8);
		for (index = 0; index < mutations; index++)
		{
			unsigned int at = size ? next_random() % (unsigned int)size : 0;

			switch (next_random() % 8)
			{
			case 0:
				if (size)
					input[at] ^= (unsigned char)(1 << (next_random() % 8));
				break;
			case 1:
				if (size)
					input[at] = (unsigned char)next_random();
				break;
			case 2:
				/* (a length byte's worth: 0, 255, one past) */
				if (size)
					input[at] = (unsigned char)(next_random() & 1 ? 0xFF : next_random() % 40);
				break;
			case 3:
				size = at;
				break;
			case 4:
				if (size < MAXIMUM_INPUT - 16)
				{
					int grow = 1 + (int)(next_random() % 16);

					memmove(input + at + grow, input + at, (size_t)(size - (int)at));
					for (; grow > 0; grow--, size++)
						input[at + (unsigned int)grow - 1] = (unsigned char)next_random();
				}
				break;
			case 5:
			{
				/* (a splice from another seed) */
				int other = (int)(next_random() % SEEDS);
				unsigned int from = seed_sizes[other] > 2 ? 2 + next_random() % (unsigned int)(seed_sizes[other] - 2) : 0;
				int length = (int)(next_random() % 32);

				if (from + (unsigned int)length <= (unsigned int)seed_sizes[other] && at + (unsigned int)length < MAXIMUM_INPUT)
				{
					memcpy(input + at, seeds[other] + from, (size_t)length);
					if ((int)at + length > size)
						size = (int)at + length;
				}
				break;
			}
			case 6:
				input[1] = (unsigned char)next_random();
				break;
			default:
				if (size > 2)
					input[0] = (unsigned char)(next_random() % 7);
				break;
			}
		}
		if (size < 0)
			size = 0;
		LLVMFuzzerTestOneInput(input, (size_t)size);
		if (run % 100000 == 99999)
			fprintf(stderr, "%lu inputs\n", run + 1);
	}
	printf("PASS (%lu inputs; lists read whole %lu times, %lu entries checked)\n", runs, lists_taken, friends_seen);
	return 0;
}
#endif
