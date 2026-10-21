/*
NET_FUZZ_CHAT.C

A fuzz target for the in-game chat's rules (port/linux/game/game_chat_protocol.c)
and its slur filter (port/linux/src/chat_filter.c, with the shipped words,
port/vita/app0/chat/filter.txt): what another machine's lines and the host's
settings carry, as the host and a client parse them (kinds 87-89, OpenCE's
text chat), and the text each machine then cleans, checks for links and
masks. The handlers themselves (game_chat.c) need a running game:
run_netns_online_test.sh chat sends them floods and malformed lines.

An input is a list of records, each a kind, a 16-bit length and that many
bytes:
  0  a line's entries (the first byte its count): parsed as a host and a
     client parse them; a line taken is printable ASCII, no markup, at most
     63 characters, its flags known and one channel
  1  the host's settings' entries (the first byte its count)
  2  text cleaned into a buffer of the first byte's size (1-255): printable,
     no markup, no two spaces, none at its ends, ended within the buffer;
     then looked for links and masked by the filter (its length kept)
  3  a line made for the wire from the text, and read back as it was made
  4  a sender's or a host's rate: times (4 bytes each) taken from the
     bucket; never more than its burst at once
  5  mutes and the filter's strikes, by the names in the bytes
Built as the other targets are, by run_net_fuzz_test.sh.
*/

#include "../../linux/game/game_chat_protocol.c"
#include "../../linux/src/chat_filter.c"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fuzz_text_ok(const char *text, int size)
{
	int length = 0;

	while (length < size && text[length])
	{
		char character = text[length];

		if (character < ' ' || character > '~' || strchr("|%^<>\\`", character))
			return 0;
		if (character == ' ' && (length == 0 || text[length - 1] == ' '))
			return 0;
		length++;
	}
	return length < size && (length == 0 || text[length - 1] != ' ');
}

static void fuzz_filter_load(void)
{
	static int loaded;
	FILE *file;
	static char data[65536];
	size_t size;

	if (loaded)
		return;
	loaded = 1;
	file = fopen("port/vita/app0/chat/filter.txt", "rb");
	if (!file)
		return;
	size = fread(data, 1, sizeof(data) - 1, file);
	fclose(file);
	chat_filter_load(data, (long)size);
}

static void fuzz_record(int kind, const unsigned char *data, int size)
{
	switch (kind)
	{
	case 0:
	{
		struct game_chat_line line;
		int count = size ? data[0] : 0;
		int parsed;

		parsed = game_chat_parse_line(size > 1 ? data + 1 : NULL, (unsigned long)(size > 1 ? size - 1 : 0), count, &line);
		if (parsed == GAME_CHAT_PARSE_OK)
		{
			if (!fuzz_text_ok(line.text, sizeof(line.text)) || !line.text[0] || strlen(line.text) > GAME_CHAT_TEXT_LENGTH ||
				(line.flags & ~GAME_CHAT_LINE_FLAGS) ||
				((line.flags & GAME_CHAT_LINE_TEAM_BIT) && (line.flags & GAME_CHAT_LINE_VEHICLE_BIT)))
			{
				abort();
			}
			game_chat_text_has_link(line.text);
			chat_filter_count(line.text);
		}
		else if (parsed <= GAME_CHAT_PARSE_OK || parsed >= GAME_CHAT_PARSE_REASONS)
			abort();
		break;
	}
	case 1:
	{
		unsigned char flags = 0xFF;
		int parsed = game_chat_parse_config(size > 1 ? data + 1 : NULL, (unsigned long)(size > 1 ? size - 1 : 0),
			size ? data[0] : 0, &flags);

		if (parsed == GAME_CHAT_PARSE_OK && (flags & ~(GAME_CHAT_CONFIG_ALLOWED_BIT | GAME_CHAT_CONFIG_LINKS_BIT)))
			abort();
		if (parsed != GAME_CHAT_PARSE_OK && flags)
			abort();
		break;
	}
	case 2:
	{
		char text[256];
		int room = size ? (data[0] ? data[0] : 1) : 1;
		int length;
		char masked[256];

		memset(text, 'Z', sizeof(text));
		length = game_chat_text_clean(text, room, size > 1 ? (const char *)data + 1 : NULL, size > 1 ? size - 1 : 0);
		if (length < 0 || length > room - 1 || text[length] || (int)strlen(text) != length || !fuzz_text_ok(text, room))
			abort();
		/* (nothing written past the buffer it was given) */
		if (room < (int)sizeof(text) && text[room] != 'Z')
			abort();
		game_chat_text_has_link(text);
		memcpy(masked, text, (size_t)length + 1);
		chat_filter_mask(masked);
		if ((int)strlen(masked) != length)
			abort();
		break;
	}
	case 3:
	{
		unsigned char entry[GAME_CHAT_LINE_ENTRY_SIZE];
		char text[300];
		struct game_chat_line line;
		char cleaned[GAME_CHAT_TEXT_SIZE];

		memcpy(text, data, (size_t)(size < (int)sizeof(text) - 1 ? size : (int)sizeof(text) - 1));
		text[size < (int)sizeof(text) - 1 ? size : (int)sizeof(text) - 1] = 0;
		game_chat_line_entry(entry, size ? data[0] : 0, (unsigned char)(size > 1 ? data[1] : 0), text);
		game_chat_text_clean(cleaned, sizeof(cleaned), text, (int)strlen(text));
		if (game_chat_parse_line(entry, sizeof(entry), 1, &line) == GAME_CHAT_PARSE_OK && strcmp(line.text, cleaned))
			abort();
		if (entry[2] || entry[3] || entry[4 + GAME_CHAT_TEXT_LENGTH])
			abort();
		break;
	}
	case 4:
	{
		struct game_chat_rate rate;
		int index, taken = 0, burst = size ? 1 + data[0] % 8 : 1;
		uint32_t first = 0;

		memset(&rate, 0, sizeof(rate));
		for (index = 1; index + 4 <= size; index += 4)
		{
			uint32_t now;

			memcpy(&now, data + index, 4);
			if (index == 1)
				first = now;
			/* (a clock that only goes on: from the first time, a step a call) */
			now = first + (now % 600u) * (uint32_t)(index / 4);
			taken += game_chat_rate_take(&rate, now, burst, GAME_CHAT_HOST_REFILL_MILLISECONDS);
			if (rate.tokens < 0 || rate.tokens > burst)
				abort();
		}
		(void)taken;
		break;
	}
	case 5:
	{
		static struct game_chat_mutes mutes;
		static struct game_chat_trips trips;
		char name[24];
		int index;

		for (index = 0; index + 2 <= size; index += 2)
		{
			snprintf(name, sizeof(name), "n%02x", data[index]);
			switch (data[index + 1] % 4)
			{
			case 0: game_chat_mute_set(&mutes, name, 1, data[index + 1] & 4); break;
			case 1: game_chat_mute_set(&mutes, name, 0, 0); break;
			case 2: game_chat_trip(&trips, name, (unsigned long)data[index + 1] * 1000UL); break;
			default: game_chat_mute_find(&mutes, name); break;
			}
			if (mutes.count < 0 || mutes.count > GAME_CHAT_MUTES || trips.count < 0 || trips.count > GAME_CHAT_MUTES)
				abort();
		}
		break;
	}
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const unsigned char *cursor = data, *end = data + size;
	int count = 0;

	fuzz_filter_load();
	while (count++ < 64 && end - cursor >= 3)
	{
		int kind = cursor[0] % 6;
		int length = cursor[1] | cursor[2] << 8;

		cursor += 3;
		if (length > end - cursor)
			length = (int)(end - cursor);
		fuzz_record(kind, cursor, length);
		cursor += length;
	}
	return 0;
}

/* (without libFuzzer too) inputs made from a fixed seed, many of them near
a valid line: the run's own checks */
int net_fuzz_checks(void)
{
	static uint8_t input[1024];
	uint32_t state = 0x6A09E667u;
	int round;

	for (round = 0; round < 50000; round++)
	{
		int size = 0, records = 1 + round % 4, record;

		for (record = 0; record < records && size < (int)sizeof(input) - 80; record++)
		{
			int length, index;

			state = state * 1664525u + 1013904223u;
			length = (int)(state >> 24) % 76;
			input[size++] = (uint8_t)(state >> 8);
			input[size++] = (uint8_t)length;
			input[size++] = 0;
			for (index = 0; index < length; index++)
			{
				state = state * 1664525u + 1013904223u;
				/* (mostly printable, now and then anything) */
				input[size++] = (uint8_t)((state >> 28) ? 32 + (state >> 16) % 95 : state >> 16);
			}
			/* (a line's count 1 and its flags known, often) */
			if (length >= 3 && !(state & 3))
			{
				input[size - length] = 1;
				input[size - length + 2] &= 7;
			}
		}
		LLVMFuzzerTestOneInput(input, (size_t)size);
	}
	return 0;
}
