/*
CHAT_PROTOCOL.C

The rules of game chat that need nothing of the game (chat_protocol.h).
The link check is OpenCE PR #72's (smokeyllama), on the cleaned text.
*/

#include "chat_protocol.h"
#include "../src/chat_link.h"

#include <string.h>

/* (the Vita's menu lists the same phrases: chat_link.h) */
typedef char chat_phrases_listed[HALO_CHAT_PHRASE_COUNT == CHAT_PHRASE_COUNT ? 1 : -1];
typedef char chat_text_fits[HALO_CHAT_TEXT_SIZE == CHAT_TEXT_BYTES && HALO_CHAT_TEXT_LENGTH == CHAT_MAXIMUM_TEXT_LENGTH &&
	HALO_CHAT_NAME_SIZE == CHAT_NAME_BYTES ? 1 : -1];

/* ---------- globals */

/* the quick chat phrases, by their number on the wire: a number keeps its
phrase for good (a new one goes at the end) */
static char const *const chat_phrases[CHAT_PHRASE_COUNT] = { HALO_CHAT_PHRASES };

/* ---------- private code */

static int chat_letter(char letter)
{
	return letter >= 'a' && letter <= 'z';
}

static int chat_digit(char letter)
{
	return letter >= '0' && letter <= '9';
}

static char chat_lower(char letter)
{
	return letter >= 'A' && letter <= 'Z' ? (char)(letter + 'a' - 'A') : letter;
}

/* the text lowercased, with the ways of writing a dot that hide a link made
dots ("(.)", "[dot]", " dot ", ...), and no spaces about a dot (PR #72's
chat_link_normalize) */
static void chat_link_normalize(char const *text, char *normalized, int size)
{
	static char const *const dots[] =
	{
		"(.)", "[.]", "{.}", "<.>", "(dot)", "[dot]", "{dot}", "<dot>", " dot ",
	};
	int length = 0;
	int index = 0;

	while (text[index] && length < size - 1)
	{
		int dot = text[index] == '.';
		int dot_length = 1;
		int dot_index;

		for (dot_index = 0; !dot && dot_index < (int)(sizeof(dots) / sizeof(dots[0])); dot_index++)
		{
			int pattern_length = (int)strlen(dots[dot_index]);
			int character;

			/* (the text's end stops the comparison: its 0 matches no
			pattern's letter) */
			for (character = 0; character < pattern_length; character++)
			{
				if (chat_lower(text[index + character]) != dots[dot_index][character])
					break;
			}
			if (character == pattern_length)
			{
				dot = 1;
				dot_length = pattern_length;
			}
		}
		if (dot)
		{
			/* (no spaces before it, nor after) */
			while (length > 0 && normalized[length - 1] == ' ')
				length--;
			normalized[length++] = '.';
			index += dot_length;
			while (text[index] == ' ')
				index++;
		}
		else
		{
			normalized[length++] = chat_lower(text[index++]);
		}
	}
	normalized[length] = 0;
}

/* ---------- public code */

char const *chat_phrase_text(int phrase)
{
	return phrase >= 0 && phrase < CHAT_PHRASE_COUNT ? chat_phrases[phrase] : NULL;
}

int chat_phrase_count(void)
{
	return CHAT_PHRASE_COUNT;
}

int chat_text_clean(char *destination, int size, char const *source, int source_size)
{
	int length = 0;
	int index;
	int limit = size - 1 < CHAT_MAXIMUM_TEXT_LENGTH ? size - 1 : CHAT_MAXIMUM_TEXT_LENGTH;

	if (size <= 0)
		return 0;
	for (index = 0; source && index < source_size && source[index] && length < limit; index++)
	{
		char character = source[index];

		if (character < 0x20 || character > 0x7E)
			continue;
		if (character == '|')
			character = '/';
		/* (no spaces before the text, nor two together) */
		if (character == ' ' && (length == 0 || destination[length - 1] == ' '))
			continue;
		destination[length++] = character;
	}
	/* (nor after it) */
	while (length > 0 && destination[length - 1] == ' ')
		length--;
	destination[length] = 0;
	return length;
}

int chat_text_has_link(char const *text)
{
	/* the endings of web sites' names that links have (not every one: those
	that are words, as "is" and "so", would refuse sentences) */
	static char const *const endings[] =
	{
		"com", "net", "org", "gg", "io", "xyz", "co", "tv", "ly", "me", "info", "biz", "site", "online",
		"link", "app", "dev", "shop", "store", "club", "live", "fun", "top", "cc", "tk", "ml", "ga", "cf",
		"gq", "pw", "ws", "ru", "uk", "de", "fr", "us", "ca", "au", "cn", "nl", "br", "pl", "edu", "gov",
	};
	char normalized[CHAT_TEXT_BYTES * 2];
	int index;

	chat_link_normalize(text, normalized, (int)sizeof(normalized));
	if (strstr(normalized, "http") || strstr(normalized, "www.") || strstr(normalized, "://"))
		return 1;

	for (index = 0; normalized[index]; index++)
	{
		int start;
		int end;
		int ending_index;

		if (normalized[index] != '.' || index == 0)
			continue;

		/* an IP address: four numbers of up to three digits, with dots */
		{
			int position = index;
			int numbers;

			while (position > 0 && chat_digit(normalized[position - 1]))
				position--;
			if (position < index && index - position <= 3)
			{
				numbers = 1;
				while (numbers < 4 && normalized[position] && normalized[position] != ' ')
				{
					while (chat_digit(normalized[position]))
						position++;
					if (normalized[position] != '.' || !chat_digit(normalized[position + 1]))
						break;
					position++;
					numbers++;
				}
				if (numbers == 4)
					return 1;
			}
		}

		/* a name, a dot, and one of the endings, not followed by more of the
		word */
		if (!chat_letter(normalized[index - 1]) && !chat_digit(normalized[index - 1]) && normalized[index - 1] != '-')
			continue;
		start = index + 1;
		for (end = start; chat_letter(normalized[end]); end++)
			;
		if (end == start || chat_digit(normalized[end]))
			continue;
		for (ending_index = 0; ending_index < (int)(sizeof(endings) / sizeof(endings[0])); ending_index++)
		{
			if ((int)strlen(endings[ending_index]) == end - start &&
				!strncmp(&normalized[start], endings[ending_index], (size_t)(end - start)))
			{
				return 1;
			}
		}
	}
	return 0;
}

int chat_name_clean(char *destination, int size, uint16_t const *name, int count)
{
	int length = 0;
	int index;

	if (size <= 0)
		return 0;
	if (count > CHAT_NAME_CHARACTERS)
		count = CHAT_NAME_CHARACTERS;
	for (index = 0; name && index < count && name[index] && length < size - 1; index++)
	{
		uint16_t character = name[index];

		if (character == '|')
			character = '/';
		else if (character < 0x20 || character > 0x7E)
			character = '?';
		if (character == ' ' && (length == 0 || destination[length - 1] == ' '))
			continue;
		destination[length++] = (char)character;
	}
	while (length > 0 && destination[length - 1] == ' ')
		length--;
	if (!length && size > 1)
		destination[length++] = '?';
	destination[length] = 0;
	return length;
}

/* a message's kind, phrase, flags and text, as both directions have them */
static int chat_fields_valid(int kind, int phrase, int flags, char const *field, char *text)
{
	text[0] = 0;
	if (flags & ~CHAT_VALID_FLAGS)
		return 0;
	switch (kind)
	{
	case _chat_kind_quick:
		return chat_phrase_text(phrase) != NULL;
	case _chat_kind_typed:
		if (!chat_text_clean(text, CHAT_TEXT_BYTES, field, CHAT_TEXT_BYTES))
			return 0;
		if (chat_text_has_link(text))
		{
			text[0] = 0;
			return 0;
		}
		return 1;
	default:
		return 0;
	}
}

int chat_request_valid(struct chat_request_message const *request, char *text)
{
	text[0] = 0;
	return request->local_player >= 0 && request->local_player < 4 &&
		chat_fields_valid(request->kind, request->phrase, request->flags, request->text, text);
}

int chat_relay_valid(struct chat_relay_message const *relay, char *name, char *text)
{
	name[0] = 0;
	text[0] = 0;
	if (relay->team < -1 || relay->team > 15 || relay->player < 0 || relay->player > 127 ||
		!chat_fields_valid(relay->kind, relay->phrase, relay->flags, relay->text, text))
	{
		return 0;
	}
	chat_name_clean(name, CHAT_NAME_BYTES, relay->name, CHAT_NAME_CHARACTERS);
	return 1;
}

/* the bucket's credit now: the time it has saved up, at most burst
refills (a token is one refill's time, so no rounding loses any) */
static uint32_t chat_bucket_level(struct chat_bucket const *bucket, uint32_t now_milliseconds, int burst,
	uint32_t refill_milliseconds)
{
	uint32_t capacity = (uint32_t)burst * refill_milliseconds;
	uint32_t elapsed;

	if (!bucket->started)
		return capacity;
	/* (a clock that went back counts as no time) */
	elapsed = now_milliseconds - bucket->last_milliseconds;
	if (elapsed > 0x80000000u || bucket->credit_milliseconds > capacity)
		elapsed = 0;
	if (elapsed >= capacity - (bucket->credit_milliseconds > capacity ? capacity : bucket->credit_milliseconds))
		return capacity;
	return bucket->credit_milliseconds + elapsed;
}

int chat_bucket_take(struct chat_bucket *bucket, uint32_t now_milliseconds, int burst, uint32_t refill_milliseconds)
{
	uint32_t level = chat_bucket_level(bucket, now_milliseconds, burst, refill_milliseconds);
	uint32_t elapsed = now_milliseconds - bucket->last_milliseconds;

	/* (a clock that went back: the bucket's time stays where it was, so the
	time to come is not counted twice) */
	if (!bucket->started || elapsed <= 0x80000000u)
		bucket->last_milliseconds = now_milliseconds;
	bucket->started = 1;
	if (level < refill_milliseconds)
	{
		bucket->credit_milliseconds = level;
		return 0;
	}
	bucket->credit_milliseconds = level - refill_milliseconds;
	return 1;
}

uint32_t chat_bucket_wait(struct chat_bucket const *bucket, uint32_t now_milliseconds, int burst,
	uint32_t refill_milliseconds)
{
	uint32_t level = chat_bucket_level(bucket, now_milliseconds, burst, refill_milliseconds);

	return level >= refill_milliseconds ? 0 : refill_milliseconds - level;
}
