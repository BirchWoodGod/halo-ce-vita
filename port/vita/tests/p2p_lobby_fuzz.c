/*
P2P_LOBBY_FUZZ.C

A fuzzer of the server browser's side of the Vita's listings
(port/linux/src/p2p_lobby.c): whatever the brokers carry, heard on a slot,
read, checked, taken (or not), shown and joined, with ASan and UBSan
(run_p2p_lobby_test.sh fuzz). The platform layer is 32-bit only and this
clang has no 32-bit libFuzzer, so main below is its own engine: inputs
mutated from real listings (and from nothing) by bit flips, byte and length
changes, cuts, growth, splices and replays, millions of them; built with
-DUSE_LIBFUZZER, LLVMFuzzerTestOneInput serves libFuzzer as it is. The
input's first byte says how:

	0  the rest, as it came, on the slot of the test host's key (garbage,
	   cut, replayed listings: they must fail the reader or the signature)
	1  the rest, signed by the test host as a host signs (so that the
	   reader, the taking, the entries and joining see every layout a host
	   could sign, malicious ones too)
	2  the rest split at its first 0: a slot's name, then a payload

and its second its flags: retained, a clock step (expiry, tombstones), a
query heard, the browser toggled. With the first byte's 0x40 the input is
heard on the crossplay wire (network_wire.h: OpenCE's listings, format 1,
"hceu-lobby-1", version 29; mode 1 signs it so). The p2p_lobby_test.c
stand-ins serve.
*/

#define P2P_LOBBY_FUZZ
#include "p2p_lobby_test.c"

#include <stdint.h>

/* the hosts whose keys sign (the first is this machine's) */
enum
{
	HOSTS = 4,
};
static unsigned char host_seeds[HOSTS][P2P_SEED_SIZE], host_keys[HOSTS][P2P_KEY_SIZE];
static char host_slots[HOSTS][2 * P2P_KEY_HASH_SIZE + 1];
/* inputs after which a game was listed, and entries shown */
static unsigned long listed_inputs, entries_shown, locked_shown;
/* ... of them on the crossplay wire */
static unsigned long crossplay_listed;

static void setup(void)
{
	static int done;
	int index;

	if (done)
		return;
	done = 1;
	for (index = 0; index < HOSTS; index++)
	{
		unsigned char secret[P2P_KEY_SIZE], public_key[P2P_KEY_SIZE], hash[P2P_KEY_HASH_SIZE];

		memset(host_seeds[index], 42 + index, P2P_SEED_SIZE);
		p2p_ed25519_public(host_seeds[index], host_keys[index], secret);
		p2p_x25519(public_key, secret, NULL);
		p2p_key_hash(public_key, hash);
		p2p_hex(hash, P2P_KEY_HASH_SIZE, host_slots[index]);
	}
	memcpy(seed, host_seeds[0], sizeof(seed));
	p2p_ed25519_public(seed, signing_key, x25519_secret);
	p2p_x25519(x25519_public, x25519_secret, NULL);
	p2p_lobby_browse(1);
}

/* signed as one of the hosts signs */
static int host_sign(int host, unsigned char *bytes, int size)
{
	unsigned char data[64 + P2P_MAXIMUM_LISTING_SIZE + 64];
	int signed_size = size - P2P_SIGNATURE_SIZE;
	const char *label = test_crossplay ? "hceu-lobby-1" : SIGNATURE_LABEL;
	int label_size = (int)strlen(label);

	memcpy(data, label, (size_t)label_size);
	memcpy(data + label_size, bytes, (size_t)signed_size);
	p2p_ed25519_sign(host_seeds[host], host_keys[host], data, label_size + signed_size, bytes + signed_size);
	return size;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	unsigned char payload[P2P_MAXIMUM_LISTING_SIZE + 64];
	unsigned char hash[P2P_KEY_HASH_SIZE];
	char own[2 * P2P_KEY_HASH_SIZE + 1];
	char slot[96];
	struct p2p_lobby_entry entry;
	int payload_size = 0, mode, flags, index;
	const char *heard_on = own;

	setup();
	if (size < 2)
		return 0;
	mode = (data[0] & 0x3F) % 3;
	test_crossplay = (data[0] & 0x40) != 0;
	/* (heard on that wire's topics; the hosted game on it too) */
	test_heard = test_crossplay;
	flags = data[1];
	data += 2;
	size -= 2;
	p2p_key_hash(x25519_public, hash);
	p2p_hex(hash, P2P_KEY_HASH_SIZE, own);
	if (mode == 0)
	{
		payload_size = size > sizeof(payload) ? (int)sizeof(payload) : (int)size;
		memcpy(payload, data, (size_t)payload_size);
	}
	else if (mode == 1)
	{
		/* (the signed part, as the host signs it) */
		int signed_size = size > sizeof(payload) - P2P_SIGNATURE_SIZE ? (int)(sizeof(payload) - P2P_SIGNATURE_SIZE) :
			(int)size;

		memcpy(payload, data, (size_t)signed_size);
		/* (most of these as a listing would begin: its tag, its format and
		version, the host's own key, a time of about now) */
		if (signed_size >= 6 + 8 + P2P_KEY_SIZE && !(flags & 0x80))
		{
			unsigned long now = (unsigned long)time(NULL);

			payload[0] = 'H';
			payload[1] = 'L';
			/* (on the crossplay wire half of them a listing of OpenCE's 39,
			whose flags' 0x20 is its dedicated server: by the first byte's
			lowest bit, which the tag then overwrites) */
			unsigned int version = !test_crossplay ? HALO_PORT_NETWORK_VERSION :
				(data[0] & 1) ? HALO_PORT_CROSSPLAY_NEWEST_NETWORK_VERSION : HALO_PORT_CROSSPLAY_NETWORK_VERSION;

			payload[2] = test_crossplay ? LISTING_FORMAT_CROSSPLAY : LISTING_FORMAT;
			payload[3] = (unsigned char)(version >> 8);
			payload[4] = (unsigned char)version;
			payload[10] = (unsigned char)(now >> 24);
			payload[11] = (unsigned char)(now >> 16);
			memcpy(payload + 14, host_keys[(flags >> 2) % HOSTS], P2P_KEY_SIZE);
		}
		payload_size = host_sign((flags >> 2) % HOSTS, payload, signed_size + P2P_SIGNATURE_SIZE);
		heard_on = host_slots[(flags >> 2) % HOSTS];
	}
	else
	{
		size_t length = 0;

		while (length < size && length < sizeof(slot) - 1 && data[length])
			length++;
		memcpy(slot, data, length);
		slot[length] = 0;
		heard_on = slot;
		if (length < size)
			length++;
		payload_size = size - length > sizeof(payload) ? (int)sizeof(payload) : (int)(size - length);
		memcpy(payload, data + length, (size_t)payload_size);
	}
	if ((flags & 0x22) == 0x22)
		clock_now += (unsigned long)(flags & 0x1C) * 4000;
	else
		clock_now += 10;
	if (flags & 0x40)
	{
		pthread_mutex_lock(&p2p_lock);
		p2p_lobby_query_heard(test_crossplay);
		pthread_mutex_unlock(&p2p_lock);
	}
	pthread_mutex_lock(&p2p_lock);
	p2p_lobby_slot_heard(test_heard, heard_on, payload, payload_size, flags & 1);
	pthread_mutex_unlock(&p2p_lock);
	lobby_update(NULL, 0, 0);
	/* everything shown, and the first game joined (an open one at once; a
	locked one's password is not tried: Argon2 per input would crawl) */
	if (p2p_lobby_entry(0, &entry))
	{
		listed_inputs++;
		crossplay_listed += test_crossplay != 0;
	}
	for (index = 0; p2p_lobby_entry(index, &entry); index++)
	{
		entries_shown++;
		locked_shown += entry.locked != 0;
		if (strlen(entry.id) != 32 || strlen(entry.rules) >= sizeof(entry.rules) ||
			strlen(entry.players_line) >= sizeof(entry.players_line) || strlen(entry.name) >= sizeof(entry.name))
		{
			__builtin_trap();
		}
		for (payload_size = 0; entry.name[payload_size]; payload_size++)
		{
			/* (printable ASCII only, and never a "|") */
			if (entry.name[payload_size] < 0x20 || entry.name[payload_size] > 0x7E || entry.name[payload_size] == '|')
				__builtin_trap();
		}
		if (index == 0 && !entry.locked)
			p2p_lobby_join(entry.id, NULL);
	}
	if (flags & 0x80 && flags & 0x40)
	{
		p2p_lobby_browse(0);
		p2p_lobby_browse(1);
	}
	return 0;
}

/* ---------- the engine (without libFuzzer), and the seeds */

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
	MAXIMUM_INPUT = 2 + P2P_MAXIMUM_LISTING_SIZE + 64,
	SEEDS = 64,
};

static unsigned char seeds[SEEDS][MAXIMUM_INPUT];
static int seed_sizes[SEEDS];

/* real listings, as hosts of every kind make them: the seeds */
static void make_seeds(void)
{
	static const char *const names[] = { "Dasher", "|||", "  ", "NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN", "" };
	int index;

	for (index = 0; index < SEEDS; index++)
	{
		unsigned char listing[P2P_MAXIMUM_LISTING_SIZE];
		int size;

		pthread_mutex_lock(&p2p_lock);
		snprintf(lobby.name, sizeof(lobby.name), "%s", names[index % 5]);
		snprintf(lobby.map, sizeof(lobby.map), "%s", index & 1 ? "bloodgulch" : "a10");
		snprintf(lobby.gametype, sizeof(lobby.gametype), "%s", index & 2 ? "Slayer" : "");
		lobby.listed_player_count = index % (LISTED_PLAYERS + 1);
		{
			int player;

			for (player = 0; player < lobby.listed_player_count; player++)
				snprintf(lobby.players[player], sizeof(lobby.players[player]), "p%d%s", player, index & 4 ? "|x" : "");
		}
		lobby.player_count = index * 7;
		lobby.maximum_player_count = 16;
		lobby.score_limit = index * 1000;
		lobby.difficulty = index & 8 ? index % 4 : 255;
		lobby.flags = index & 0x7F;
		lobby.has_password = (index & 16) != 0;
		size = listing_make(listing, index % 7 == 0 ? _listing_closed : 0);
		pthread_mutex_unlock(&p2p_lock);
		/* (a quarter OpenCE's, for the crossplay wire: format 1, signed under
		its label) */
		if ((index & 3) == 3)
		{
			static const unsigned char token[P2P_TOKEN_SIZE] = { 0x26 };

			size = opence_listing(listing, host_seeds[index % HOSTS], host_keys[index % HOSTS], 1,
				HALO_PORT_CROSSPLAY_NETWORK_VERSION, index * 13, (unsigned long)index, token, "hceu-lobby-1", 0);
		}
		/* (half as heard (mode 0), half to be signed again (mode 1)) */
		seeds[index][0] = (unsigned char)((index & 1 ? 0 : 1) | ((index & 3) == 3 ? 0x40 : 0));
		seeds[index][1] = (unsigned char)(index * 37);
		memcpy(seeds[index] + 2, listing, (size_t)(index & 1 ? size : size - P2P_SIGNATURE_SIZE));
		seed_sizes[index] = 2 + (index & 1 ? size : size - P2P_SIGNATURE_SIZE);
	}
}

#ifdef USE_LIBFUZZER
/* (run_net_fuzz_test.sh's target lobby: net_fuzz_main.c's checks; with
NET_FUZZ_SEED_DIR set, the first 16 seeds written there, as the other targets
write theirs) */
void net_fuzz_write_seed(const char *folder, const char *name, const void *data, unsigned long size);

int net_fuzz_checks(void)
{
	const char *folder = getenv("NET_FUZZ_SEED_DIR");
	int index;

	setup();
	make_seeds();
	for (index = 0; folder && index < 16; index++)
	{
		char name[32];

		snprintf(name, sizeof(name), "seed-%02d%s", index, seeds[index][0] & 0x40 ? "-crossplay" : "");
		net_fuzz_write_seed(folder, name, seeds[index], (unsigned long)seed_sizes[index]);
	}
	return 0;
}
#else
static int mutate(unsigned char *input, int size)
{
	int count = 1 + next_random() % 8, change;

	for (change = 0; change < count; change++)
	{
		int at = size ? (int)(next_random() % (unsigned int)size) : 0;

		switch (next_random() % 10)
		{
		case 0: /* a bit */
			if (size)
				input[at] ^= (unsigned char)(1 << (next_random() % 8));
			break;
		case 1: /* a byte */
			if (size)
				input[at] = (unsigned char)next_random();
			break;
		case 2: /* a byte to a boundary */
		{
			static const unsigned char values[] = { 0, 1, 0x7F, 0x80, 0xFF, 8, 12, 24, 32, 33, 56, 64 };

			if (size)
				input[at] = values[next_random() % sizeof(values)];
			break;
		}
		case 3: /* cut */
			size = at;
			break;
		case 4: /* grow */
		{
			int more = (int)(next_random() % 64);

			while (more-- > 0 && size < MAXIMUM_INPUT)
				input[size++] = (unsigned char)next_random();
			break;
		}
		case 5: /* a run removed */
		{
			int length = (int)(next_random() % 16);

			if (at + length <= size)
			{
				memmove(input + at, input + at + length, (size_t)(size - at - length));
				size -= length;
			}
			break;
		}
		case 6: /* a run inserted */
		{
			int length = (int)(next_random() % 16);

			if (size + length <= MAXIMUM_INPUT)
			{
				memmove(input + at + length, input + at, (size_t)(size - at));
				memset(input + at, (int)next_random(), (size_t)length);
				size += length;
			}
			break;
		}
		case 7: /* spliced with another seed */
		{
			const unsigned char *other = seeds[next_random() % SEEDS];
			int length = (int)(next_random() % 64);

			if (at + length <= size)
				memcpy(input + at, other + at, (size_t)length);
			break;
		}
		case 8: /* the mode and flags */
			if (size >= 2)
			{
				input[0] = (unsigned char)next_random();
				input[1] = (unsigned char)next_random();
			}
			break;
		default: /* a length byte of a text, somewhere past the key */
			if (size > 110)
				input[105 + (int)(next_random() % 6)] = (unsigned char)(next_random() % 70);
			break;
		}
	}
	return size;
}

int main(int count, char **arguments)
{
	unsigned long runs = count > 1 ? strtoul(arguments[1], NULL, 10) : 1000000;
	unsigned long run;
	unsigned char input[MAXIMUM_INPUT];

	setup();
	make_seeds();
	for (run = 0; run < runs; run++)
	{
		int size;

		if (next_random() % 16 == 0)
		{
			/* from nothing */
			size = (int)(next_random() % MAXIMUM_INPUT);
			for (count = 0; count < size; count++)
				input[count] = (unsigned char)next_random();
		}
		else
		{
			int seed = (int)(next_random() % SEEDS);

			memcpy(input, seeds[seed], (size_t)seed_sizes[seed]);
			size = mutate(input, seed_sizes[seed]);
		}
		LLVMFuzzerTestOneInput(input, (size_t)size);
		/* (the browser starts afresh now and then, as one opened again) */
		if (run % 50000 == 49999)
		{
			p2p_lobby_browse(0);
			p2p_lobby_browse(1);
		}
		if (run % 200000 == 0)
		{
			struct p2p_lobby_entry entry;
			int games_listed = 0;

			while (p2p_lobby_entry(games_listed, &entry))
				games_listed++;
			printf("run %lu: %d games listed now; inputs after which one was: %lu; entries shown: %lu (%lu locked)\n",
				run, games_listed, listed_inputs, entries_shown, locked_shown);
			fflush(stdout);
		}
	}
	printf("PASS: %lu inputs, no sanitizer report; inputs after which a game was listed: %lu (%lu on the crossplay "
		"wire); entries shown: %lu (%lu locked)\n", runs, listed_inputs, crossplay_listed, entries_shown, locked_shown);
	return 0;
}
#endif
