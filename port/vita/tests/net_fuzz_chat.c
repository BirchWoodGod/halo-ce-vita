/*
NET_FUZZ_CHAT.C

A fuzz target for game chat's rules (port/linux/game/chat_protocol.c):
a joiner's request and a host's relay as they come off the wire (after the
game's decoder: every byte of the structure the network's), the text and
the names they carry, the link check, and the flood limits over any clock.
What is kept must be what may be shown: printable ASCII, no '|', no spaces
at either end nor two together, within its cap; a line refused keeps
nothing; a limit lets no more through than its burst and its refills.
The sending and showing (chat.c) are not here.

An input is a list of records, each a kind, a 16-bit length and that many
bytes:
  0  a request (struct chat_request_message)
  1  a relay (struct chat_relay_message)
  2  text, cleaned and checked for a link
  3  a name (16-bit characters)
  4  a limit's clock: 32-bit times, each a line tried
Built as net_fuzz_p2p.c is, by run_net_fuzz_test.sh.
*/

#include "../../linux/game/chat_protocol.c"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* what may be shown: printable ASCII, no '|', trimmed, no two spaces, ended
within size, at most `most` characters */
static void fuzz_shown(char const *text, int size, int most)
{
	int length = 0;

	while (length < size && text[length])
	{
		char character = text[length];

		if (character < 0x20 || character > 0x7E || character == '|')
			abort();
		if (character == ' ' && (length == 0 || text[length - 1] == ' ' || !text[length + 1]))
			abort();
		length++;
	}
	if (length >= size || length > most)
		abort();
}

static void fuzz_record(int kind, const unsigned char *data, int size)
{
	switch (kind)
	{
	case 0:
	{
		struct chat_request_message request;
		char text[CHAT_TEXT_BYTES];

		memset(&request, 0, sizeof(request));
		memcpy(&request, data, (size_t)(size < (int)sizeof(request) ? size : (int)sizeof(request)));
		memset(text, 0x5A, sizeof(text));
		if (chat_request_valid(&request, text))
		{
			fuzz_shown(text, sizeof(text), CHAT_MAXIMUM_TEXT_LENGTH);
			if (request.kind == _chat_kind_quick ? !chat_phrase_text(request.phrase) || text[0] :
				!text[0] || chat_text_has_link(text))
			{
				abort();
			}
			if (request.local_player < 0 || request.local_player > 3 || (request.flags & ~CHAT_VALID_FLAGS))
				abort();
		}
		else if (text[0])
		{
			/* (a refused line keeps nothing) */
			abort();
		}
		break;
	}
	case 1:
	{
		struct chat_relay_message relay;
		char name[CHAT_NAME_BYTES];
		char text[CHAT_TEXT_BYTES];

		memset(&relay, 0, sizeof(relay));
		memcpy(&relay, data, (size_t)(size < (int)sizeof(relay) ? size : (int)sizeof(relay)));
		memset(name, 0x5A, sizeof(name));
		memset(text, 0x5A, sizeof(text));
		if (chat_relay_valid(&relay, name, text))
		{
			fuzz_shown(name, sizeof(name), CHAT_NAME_CHARACTERS);
			fuzz_shown(text, sizeof(text), CHAT_MAXIMUM_TEXT_LENGTH);
			if (!name[0] || (relay.kind == _chat_kind_typed && (!text[0] || chat_text_has_link(text))))
				abort();
			if (relay.team < -1 || relay.team > 15 || relay.player < 0 || relay.player > 127)
				abort();
		}
		else if (name[0] || text[0])
		{
			abort();
		}
		break;
	}
	case 2:
	{
		char text[CHAT_TEXT_BYTES];
		char small[5];
		int length = chat_text_clean(text, sizeof(text), (char const *)data, size);

		fuzz_shown(text, sizeof(text), CHAT_MAXIMUM_TEXT_LENGTH);
		if (length != (int)strlen(text))
			abort();
		chat_text_has_link(text);
		chat_text_clean(small, sizeof(small), (char const *)data, size);
		fuzz_shown(small, sizeof(small), 4);
		break;
	}
	case 3:
	{
		uint16_t characters[CHAT_NAME_CHARACTERS];
		char name[CHAT_NAME_BYTES];
		int count = size / 2 < CHAT_NAME_CHARACTERS ? size / 2 : CHAT_NAME_CHARACTERS;

		memset(characters, 0, sizeof(characters));
		memcpy(characters, data, (size_t)count * 2);
		chat_name_clean(name, sizeof(name), characters, count);
		fuzz_shown(name, sizeof(name), CHAT_NAME_CHARACTERS);
		if (!name[0])
			abort();
		break;
	}
	case 4:
	{
		struct chat_bucket bucket;
		int index, taken = 0, steps = size / 4;
		uint32_t first = 0, last = 0;
		int monotonic = 1;

		memset(&bucket, 0, sizeof(bucket));
		for (index = 0; index < steps; index++)
		{
			uint32_t now;

			memcpy(&now, data + index * 4, 4);
			/* (times as a clock gives them: forward by less than a day) */
			if (index && (now - last > 86400000u))
				monotonic = 0;
			if (!index)
				first = now;
			last = now;
			taken += chat_bucket_take(&bucket, now, CHAT_BURST, CHAT_REFILL_MILLISECONDS);
			if (chat_bucket_wait(&bucket, now, CHAT_BURST, CHAT_REFILL_MILLISECONDS) > CHAT_REFILL_MILLISECONDS)
				abort();
		}
		/* (a clock going forward: no more than the burst and its refills) */
		if (monotonic && steps && (uint32_t)taken > CHAT_BURST + (last - first) / CHAT_REFILL_MILLISECONDS + 1)
			abort();
		break;
	}
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	size_t offset = 0;

	while (offset + 3 <= size)
	{
		int kind = data[offset] % 5;
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

int net_fuzz_checks(void)
{
	char const *folder = getenv("NET_FUZZ_SEED_DIR");
	struct chat_request_message request;
	struct chat_relay_message relay;
	unsigned char seed[512];
	int failures = 0;

	if (!folder)
		return 0;
	memset(&request, 0, sizeof(request));
	request.kind = _chat_kind_typed;
	strcpy(request.text, "hello |n there");
	seed[0] = 0;
	seed[1] = sizeof(request);
	seed[2] = 0;
	memcpy(seed + 3, &request, sizeof(request));
	net_fuzz_write_seed(folder, "request", seed, 3 + sizeof(request));
	memset(&relay, 0, sizeof(relay));
	relay.kind = _chat_kind_quick;
	relay.phrase = 1;
	relay.team = -1;
	relay.name[0] = 'A';
	seed[0] = 1;
	seed[1] = sizeof(relay);
	seed[2] = 0;
	memcpy(seed + 3, &relay, sizeof(relay));
	net_fuzz_write_seed(folder, "relay", seed, 3 + sizeof(relay));
	return failures;
}
