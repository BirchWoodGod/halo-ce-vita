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

/* the host's notices, by their number on the wire */
static char const *const chat_notices[NUMBER_OF_CHAT_NOTICES] =
{
	"Chat is off in this game",
	"Only quick chat in this game",
};

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

char const *chat_notice_text(int notice)
{
	return notice >= 0 && notice < NUMBER_OF_CHAT_NOTICES ? chat_notices[notice] : NULL;
}

int chat_host_notice(int host_mode, int kind)
{
	if (host_mode == HALO_CHAT_MODE_ON)
		return -1;
	if (host_mode == HALO_CHAT_MODE_QUICK)
		return kind == _chat_kind_quick ? -1 : _chat_notice_host_quick;
	return _chat_notice_host_off;
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
	/* (players.c's player_name_character_ascii: U+00C0 to U+017F) */
	static char const latin[] =
		"AAAAAAACEEEEIIIIDNOOOOO?OUUUUYTs"
		"aaaaaaaceeeeiiiidnooooo?ouuuuyty"
		"AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlL"
		"lLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
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
		else if (character >= 0xC0 && character < 0x180)
			character = (uint16_t)latin[character - 0xC0];
		else if (character < 0x20 || character > 0x7E)
			character = '?';
		if (character == ' ' && length == 0)
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

/* a message's kind, phrase, flags and text, as both directions have them
(a notice: the host's alone) */
static int chat_fields_valid(int kind, int phrase, int flags, char const *field, char *text, int notice)
{
	text[0] = 0;
	if (flags & ~CHAT_VALID_FLAGS)
		return 0;
	switch (kind)
	{
	case _chat_kind_quick:
		return chat_phrase_text(phrase) != NULL;
	case _chat_kind_notice:
		return notice && !flags && chat_notice_text(phrase) != NULL;
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
		chat_fields_valid(request->kind, request->phrase, request->flags, request->text, text, 0);
}

int chat_relay_valid(struct chat_relay_message const *relay, char *name, char *text)
{
	name[0] = 0;
	text[0] = 0;
	if (relay->team < -1 || relay->team > 15 || relay->player < 0 || relay->player > 127 ||
		!chat_fields_valid(relay->kind, relay->phrase, relay->flags, relay->text, text, 1))
	{
		return 0;
	}
	/* (a notice names nobody) */
	if (relay->kind != _chat_kind_notice)
		chat_name_clean(name, CHAT_NAME_BYTES, relay->name, CHAT_NAME_CHARACTERS);
	return 1;
}

static int chat_mute_names(struct chat_mute const *entry, char const *name)
{
	return name && name[0] && !strncmp(entry->name, name, CHAT_NAME_BYTES - 1);
}

static int chat_mute_is(struct chat_mute const *entry, int machine, int controller)
{
	return machine >= 0 && entry->machine == machine && entry->controller == controller;
}

int chat_mutes_set(struct chat_mutes *mutes, char const *name, int machine, int controller, int mute)
{
	int index;
	int changed = 0;

	if (!name || !name[0] || mutes->count < 0 || mutes->count > CHAT_MUTED_PLAYERS)
		return 0;
	if (mute)
	{
		if (chat_mutes_match(mutes, name, machine, controller) || mutes->count == CHAT_MUTED_PLAYERS)
			return 0;
		memset(&mutes->entries[mutes->count], 0, sizeof(mutes->entries[0]));
		strncpy(mutes->entries[mutes->count].name, name, CHAT_NAME_BYTES - 1);
		mutes->entries[mutes->count].machine = (int16_t)(machine >= 0 && machine < 0x8000 ? machine : -1);
		mutes->entries[mutes->count].controller = (int16_t)(machine >= 0 ? controller : -1);
		mutes->count++;
		return 1;
	}
	for (index = 0; index < mutes->count;)
	{
		if (chat_mute_names(&mutes->entries[index], name) || chat_mute_is(&mutes->entries[index], machine, controller))
		{
			mutes->count--;
			memmove(&mutes->entries[index], &mutes->entries[index + 1],
				(size_t)(mutes->count - index) * sizeof(mutes->entries[0]));
			changed = 1;
		}
		else
		{
			index++;
		}
	}
	return changed;
}

int chat_mutes_match(struct chat_mutes const *mutes, char const *name, int machine, int controller)
{
	int index;

	for (index = 0; index < mutes->count && index < CHAT_MUTED_PLAYERS; index++)
	{
		if (chat_mute_is(&mutes->entries[index], machine, controller) || chat_mute_names(&mutes->entries[index], name))
			return 1;
	}
	return 0;
}

void chat_mutes_player(struct chat_mutes *mutes, int machine, int controller, char const *name)
{
	int index;

	if (!name || !name[0])
		return;
	for (index = 0; index < mutes->count && index < CHAT_MUTED_PLAYERS; index++)
	{
		struct chat_mute *entry = &mutes->entries[index];

		if (chat_mute_is(entry, machine, controller))
		{
			memset(entry->name, 0, sizeof(entry->name));
			strncpy(entry->name, name, CHAT_NAME_BYTES - 1);
		}
		else if (chat_mute_names(entry, name))
		{
			/* (muted by name alone, back in the game: known by where the
			game has them again) */
			if (entry->machine < 0 && machine >= 0 && machine < 0x8000)
			{
				entry->machine = (int16_t)machine;
				entry->controller = (int16_t)controller;
			}
			/* (another now has the name a muted player left: not them) */
			else if (entry->machine >= 0)
			{
				memset(entry->name, 0, sizeof(entry->name));
			}
		}
	}
}

void chat_mutes_forget_machine(struct chat_mutes *mutes, int machine)
{
	int index;

	if (mutes->count < 0 || mutes->count > CHAT_MUTED_PLAYERS)
		mutes->count = 0;
	for (index = 0; index < mutes->count;)
	{
		struct chat_mute *entry = &mutes->entries[index];

		if (machine < 0 || entry->machine == machine)
		{
			entry->machine = -1;
			entry->controller = -1;
		}
		/* (one left with neither a name nor a place goes) */
		if (entry->machine < 0 && !entry->name[0])
		{
			mutes->count--;
			memmove(entry, entry + 1, (size_t)(mutes->count - index) * sizeof(*entry));
		}
		else
		{
			index++;
		}
	}
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
