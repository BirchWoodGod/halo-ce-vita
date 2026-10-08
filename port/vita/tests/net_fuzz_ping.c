/*
NET_FUZZ_PING.C

A fuzz target for the scoreboard's pings' rules (port/linux/game/
ping_protocol.c): the host's message of every player's ping as it comes off
the wire (the entries after the distributed netcode's header, which has
checked only that the count of them is there), the half second a client
keeps between two, and the value a host puts in. What is kept must be what
may be shown: each player named once, below the game's most players, at
most PING_MAXIMUM_MILLISECONDS or not known; a message refused keeps
nothing; no more messages taken than one a half second.
The sending and showing (network_distributed.c, latency_meter.c) are not
here.

An input is a list of records, each a kind, a 16-bit length and that many
bytes:
  0  a message: the game's most players (a byte, 1 to 256), then entries
     (struct ping_entry), as many as the bytes hold
  1  a client's clock: 32-bit times, each a message come
  2  a host's values: 32-bit numbers (signed), each made a message's
Built as net_fuzz_p2p.c is, by run_net_fuzz_test.sh.
*/

#include "../../linux/game/ping_protocol.c"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* a message read: taken whole as the rules say, or refused with the table
as it was */
static int fuzz_message(const unsigned char *data, int size)
{
	uint16_t table[256];
	uint8_t named[256];
	int players, count, index, taken;
	int valid = 1;

	if (size < 1)
		return 0;
	players = data[0] + 1;
	count = (size - 1) / (int)sizeof(struct ping_entry);
	for (index = 0; index < 256; index++)
		table[index] = 0x5A5A;
	taken = ping_entries_read(data + 1, count, table, players);
	/* (what the rules take, worked out here on their own) */
	memset(named, 0, sizeof(named));
	if (count > players)
		valid = 0;
	for (index = 0; valid && index < count; index++)
	{
		const unsigned char *entry = data + 1 + index * 4;
		int player = entry[0], milliseconds = entry[2] | (entry[3] << 8);

		if (player >= players || named[player] || entry[1] ||
			(milliseconds > PING_MAXIMUM_MILLISECONDS && milliseconds != (int)PING_UNKNOWN))
		{
			valid = 0;
		}
		else
			named[player] = 1;
	}
	if (!valid)
	{
		if (taken != -1)
			abort();
		for (index = 0; index < 256; index++)
			if (table[index] != 0x5A5A)
				abort();
		return -1;
	}
	if (taken != count)
		abort();
	for (index = 0; index < players; index++)
	{
		if (table[index] != PING_UNKNOWN && table[index] > PING_MAXIMUM_MILLISECONDS)
			abort();
		if (!named[index] && table[index] != PING_UNKNOWN)
			abort();
	}
	for (index = 0; index < count; index++)
	{
		const unsigned char *entry = data + 1 + index * 4;

		if (table[entry[0]] != (uint16_t)(entry[2] | (entry[3] << 8)))
			abort();
	}
	/* (past the game's players: untouched) */
	for (index = players; index < 256; index++)
		if (table[index] != 0x5A5A)
			abort();
	return taken;
}

static void fuzz_record(int kind, const unsigned char *data, int size)
{
	switch (kind)
	{
	case 0:
		fuzz_message(data, size);
		break;
	case 1:
	{
		struct ping_receiver receiver;
		uint32_t first = 0, previous = 0;
		int index, taken = 0, steps = size / 4, monotonic = 1;

		memset(&receiver, 0, sizeof(receiver));
		for (index = 0; index < steps; index++)
		{
			uint32_t now;

			memcpy(&now, data + index * 4, 4);
			if (index && now - previous > 86400000u)
				monotonic = 0;
			if (!index)
				first = now;
			previous = now;
			taken += ping_message_due(&receiver, now);
			/* (the first is always taken) */
			if (!receiver.taken || (index == 0 && (taken != 1 || receiver.last != now)))
				abort();
		}
		/* (a clock going forward: one a half second, and the first) */
		if (monotonic && steps && (uint32_t)taken > (previous - first) / PING_MINIMUM_INTERVAL_MILLISECONDS + 1)
			abort();
		break;
	}
	default:
	{
		int index;

		for (index = 0; index + 4 <= size; index++)
		{
			int32_t value;
			uint16_t told;

			memcpy(&value, data + index, 4);
			told = ping_entry_value((long)value);
			if (value < 0 ? told != PING_UNKNOWN : told > PING_MAXIMUM_MILLISECONDS ||
				told != (value > PING_MAXIMUM_MILLISECONDS ? PING_MAXIMUM_MILLISECONDS : value))
			{
				abort();
			}
		}
		break;
	}
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	size_t offset = 0;

	while (offset + 3 <= size)
	{
		int kind = data[offset] % 3;
		int length = data[offset + 1] | (data[offset + 2] << 8);

		offset += 3;
		if ((size_t)length > size - offset)
			length = (int)(size - offset);
		fuzz_record(kind, data + offset, length);
		offset += (size_t)length;
	}
	return 0;
}

/* (NET_FUZZ_SEED_DIR set: a record of each kind written there as seeds) */
void net_fuzz_write_seed(char const *folder, char const *name, void const *data, unsigned long size);

static int fuzz_check(int condition, char const *what)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	return condition ? 0 : 1;
}

/* the rules' own checks, then the seeds */
int net_fuzz_checks(void)
{
	char const *folder = getenv("NET_FUZZ_SEED_DIR");
	struct ping_entry entries[4];
	uint16_t table[16];
	struct ping_receiver receiver;
	int failures = 0;

	memset(entries, 0, sizeof(entries));
	entries[0].player_index = 0; entries[0].milliseconds = 0;
	entries[1].player_index = 3; entries[1].milliseconds = 85;
	entries[2].player_index = 7; entries[2].milliseconds = (uint16_t)PING_UNKNOWN;
	failures += fuzz_check(ping_entries_read(entries, 3, table, 16) == 3 && table[0] == 0 && table[3] == 85 &&
		table[7] == PING_UNKNOWN && table[1] == PING_UNKNOWN && table[15] == PING_UNKNOWN,
		"a host's pings: each player's taken, the rest not known");
	table[3] = 1234;
	entries[1].player_index = 16;
	failures += fuzz_check(ping_entries_read(entries, 3, table, 16) == -1 && table[3] == 1234,
		"a player past the game's most: the message refused, nothing kept");
	entries[1].player_index = 0;
	failures += fuzz_check(ping_entries_read(entries, 3, table, 16) == -1, "a player named twice: refused");
	entries[1].player_index = 3;
	entries[1].milliseconds = PING_MAXIMUM_MILLISECONDS + 1;
	failures += fuzz_check(ping_entries_read(entries, 3, table, 16) == -1, "a ping past 9999 ms: refused");
	entries[1].milliseconds = 85;
	entries[1].pad = 1;
	failures += fuzz_check(ping_entries_read(entries, 3, table, 16) == -1, "a pad not 0: refused");
	entries[1].pad = 0;
	failures += fuzz_check(ping_entries_read(entries, 17, table, 16) == -1, "more entries than players: refused");
	failures += fuzz_check(ping_entries_read(entries, 0, table, 16) == 0 && table[0] == PING_UNKNOWN,
		"an empty message: every player not known");
	failures += fuzz_check(ping_entry_value(-1) == PING_UNKNOWN && ping_entry_value(0) == 0 &&
		ping_entry_value(85) == 85 && ping_entry_value(100000) == PING_MAXIMUM_MILLISECONDS,
		"a host's values: not known, within 0 and 9999 ms");
	memset(&receiver, 0, sizeof(receiver));
	failures += fuzz_check(ping_message_due(&receiver, 0) && !ping_message_due(&receiver, 0) &&
		!ping_message_due(&receiver, 499) && ping_message_due(&receiver, 500) && !ping_message_due(&receiver, 600) &&
		ping_message_due(&receiver, 2500), "a client takes one message a half second (its clock at 0 too)");
	if (folder)
	{
		unsigned char seed[64];

		seed[0] = 0;
		seed[1] = 1 + 3 * sizeof(struct ping_entry);
		seed[2] = 0;
		seed[3] = 15;
		entries[1].milliseconds = 85;
		memcpy(seed + 4, entries, 3 * sizeof(struct ping_entry));
		net_fuzz_write_seed(folder, "seed-message", seed, 4 + 3 * sizeof(struct ping_entry));
		{
			static unsigned char const times[] =
			{
				1, 16, 0, 0x88, 0x13, 0, 0, 0xEC, 0x13, 0, 0, 0x7C, 0x15, 0, 0, 0x4C, 0x1D, 0, 0,
			};

			net_fuzz_write_seed(folder, "seed-clock", times, sizeof(times));
		}
		{
			static unsigned char const values[] =
			{
				2, 12, 0, 0xFF, 0xFF, 0xFF, 0xFF, 85, 0, 0, 0, 0xA0, 0x86, 0x01, 0,
			};

			net_fuzz_write_seed(folder, "seed-values", values, sizeof(values));
		}
	}
	return failures;
}
