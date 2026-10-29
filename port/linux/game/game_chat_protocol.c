/*
GAME_CHAT_PROTOCOL.C

The in-game chat's rules (game_chat_protocol.h). The wire, the rate a host
takes lines at and the link check are OpenCE's text chat's (its
port/linux/game/network_chat.c, by the OpenCE contributors, CC0 1.0: the
format and chat_text_has_link ported from its last commit before its
licence changed, 4b09168e^), so a PC player's OpenCE and this build agree on
every byte; the cleaning is stricter (the markup characters out, accented
letters made plain), the mutes and the filter's count of a sender's lines
are this build's own.
*/

#include "game_chat_protocol.h"

#include <string.h>

#define NUMBEROF_ARRAY(array) ((int)(sizeof(array) / sizeof((array)[0])))

static const char *const game_chat_presets[GAME_CHAT_PRESET_COUNT] =
{
	"Need backup!",
	"Enemy spotted!",
	"Flag taken!",
	"Our flag is taken!",
	"Follow me",
	"On my way",
	"Cover me",
	"Nice shot!",
	"Thanks!",
	"Sorry!",
	"Yes",
	"No",
	"GG",
};

/* ---------- text */

static int game_chat_markup(char character)
{
	return character == '|' || character == '%' || character == '^' || character == '<' || character == '>' ||
		character == '\\' || character == '`';
}

/* a two-byte UTF-8 character of Latin-1's (0xC2 or 0xC3 and its second
byte) as the plain letter: 0 to leave it out ('¿' '¡' and the like), '?'
for one with none */
static char game_chat_latin1(unsigned char lead, unsigned char second)
{
	static const char upper[65] =
		"AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
	unsigned int code = ((unsigned int)(lead & 0x1F) << 6) | (second & 0x3F);

	if (code == 0xA1 || code == 0xBF || code == 0xAB || code == 0xBB || code == 0xA0)
		return code == 0xA0 ? ' ' : 0;
	if (code >= 0xC0 && code <= 0xFF)
	{
		char plain = upper[code - 0xC0];

		return plain == 'x' || plain == '/' ? '?' : plain;
	}
	return '?';
}

int game_chat_text_clean(char *destination, int destination_size, const char *source, int source_size)
{
	int length = 0;
	int index = 0;

	if (!destination || destination_size <= 0)
		return 0;
	while (source && index < source_size && source[index] && length < destination_size - 1)
	{
		unsigned char byte = (unsigned char)source[index];
		char character;

		if (byte < 0x80)
		{
			character = (char)byte;
			index++;
			if (byte < ' ' || byte == 0x7F)
				character = byte == '\t' ? ' ' : '?';
			else if (game_chat_markup(character))
				continue;
		}
		else
		{
			/* (a UTF-8 sequence: its length by its first byte; a Latin-1
			letter its plain one, anything else one '?') */
			int sequence = byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : byte >= 0xC0 ? 2 : 1;
			int at;

			if ((byte == 0xC2 || byte == 0xC3) && index + 1 < source_size &&
				((unsigned char)source[index + 1] & 0xC0) == 0x80)
			{
				character = game_chat_latin1(byte, (unsigned char)source[index + 1]);
				index += 2;
				if (!character)
					continue;
			}
			else
			{
				character = '?';
				index++;
				for (at = 1; at < sequence && index < source_size && ((unsigned char)source[index] & 0xC0) == 0x80; at++)
					index++;
			}
		}
		if (character == ' ' && (length == 0 || destination[length - 1] == ' '))
			continue;
		destination[length++] = character;
	}
	while (length > 0 && destination[length - 1] == ' ')
		length--;
	destination[length] = 0;
	return length;
}

static char game_chat_lower(char character)
{
	return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static int game_chat_alphanumeric(char character)
{
	return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
}

/* how a top-level domain after a name and a dot counts (letters: count of
them, lower case): 2 always ("discord.gg", "example.com"); 1, a word as well
("me", "to", "is"), only with a path, a port or another domain after it
("youtu.be/x", "t.co/x"); 0 for none */
static int game_chat_link_domain(const char *letters, int count)
{
	static const char *const domains[] =
	{
		"com", "net", "org", "gg", "io", "tv", "uk", "ru", "de", "fr", "nl", "eu", "info", "biz", "xyz", "ly", "app",
		"dev", "cc", "ca", "au", "stream", "shop", "site", "online", "club", "gl", "ws", "cn", "jp", "kr", "br", "es",
		"pl", "se", "tk", "icu", "vip", "edu", "gov", "mil", "ai", "sh", "fm", "gd", "im", "st", "su", "mx", "ar",
	};
	static const char *const words[] =
		{ "me", "us", "to", "co", "be", "in", "is", "it", "fun", "top", "win", "pro", "lol", "live", "link", "int" };
	int index;

	for (index = 0; index < NUMBEROF_ARRAY(domains); index++)
	{
		if ((int)strlen(domains[index]) == count && !strncmp(letters, domains[index], (size_t)count))
			return 2;
	}
	for (index = 0; index < NUMBEROF_ARRAY(words); index++)
	{
		if ((int)strlen(words[index]) == count && !strncmp(letters, words[index], (size_t)count))
			return 1;
	}
	return 0;
}

int game_chat_text_has_link(const char *text)
{
	static const char *const dots[] = { "(dot)", "[dot]", "{dot}", "(.)", "[.]", " dot " };
	/* (the text in lower case, each dot written out a dot, without the
	spaces about it; room past its end for the reads below) */
	char line[2 * GAME_CHAT_TEXT_SIZE + 8];
	int length = 0;
	int index;

	if (!text)
		return 0;
	memset(line, 0, sizeof(line));
	for (index = 0; text[index] && length < (int)sizeof(line) - 8;)
	{
		int dot;
		int at = 0;

		for (dot = 0; dot < NUMBEROF_ARRAY(dots); dot++)
		{
			for (at = 0; dots[dot][at] && game_chat_lower(text[index + at]) == dots[dot][at]; at++)
				;
			if (!dots[dot][at])
				break;
		}
		if (dot < NUMBEROF_ARRAY(dots) && dots[dot][0] == ' ')
		{
			char letters[8];
			int count;

			for (count = 0; count < (int)sizeof(letters) && game_chat_lower(text[index + at + count]) >= 'a' &&
				game_chat_lower(text[index + at + count]) <= 'z'; count++)
			{
				letters[count] = game_chat_lower(text[index + at + count]);
			}
			if (count == (int)sizeof(letters) || game_chat_link_domain(letters, count) != 2)
				dot = NUMBEROF_ARRAY(dots);
		}
		if (dot == NUMBEROF_ARRAY(dots))
		{
			/* (a dot with spaces both sides) */
			if (text[index] != '.' || !length || line[length - 1] != ' ' || text[index + 1] != ' ')
			{
				line[length++] = game_chat_lower(text[index++]);
				continue;
			}
			at = 1;
		}
		index += at;
		while (length > 0 && line[length - 1] == ' ')
			length--;
		while (text[index] == ' ')
			index++;
		line[length++] = '.';
	}
	line[length] = 0;
	for (index = 0; index < length; index++)
	{
		if (!strncmp(line + index, "://", 3) ||
			(!strncmp(line + index, "www.", 4) && (index == 0 || !game_chat_alphanumeric(line[index - 1]))))
		{
			return 1;
		}
		/* (a name, a dot and a domain, all of its letters, which ends there) */
		if (line[index] == '.' && index > 0 && game_chat_alphanumeric(line[index - 1]))
		{
			int end;
			int domain;

			for (end = index + 1; line[end] >= 'a' && line[end] <= 'z'; end++)
				;
			if (end - index - 1 < 2 || game_chat_alphanumeric(line[end]))
				continue;
			domain = game_chat_link_domain(line + index + 1, end - index - 1);
			if (domain == 2 || (domain == 1 && (line[end] == '/' || line[end] == ':' ||
				(line[end] == '.' && line[end + 1] >= 'a' && line[end + 1] <= 'z'))))
			{
				return 1;
			}
		}
		/* (four numbers and three dots, and no more: "1.2.3.4.5" is a
		version's) */
		if (line[index] >= '0' && line[index] <= '9' &&
			(index == 0 || (!game_chat_alphanumeric(line[index - 1]) && line[index - 1] != '.')))
		{
			int at = index;
			int part;

			for (part = 0; part < 4; part++)
			{
				int digits;

				for (digits = 0; at < length && line[at] >= '0' && line[at] <= '9'; at++)
					digits++;
				if (!digits || digits > 3 || (part < 3 && (at >= length || line[at++] != '.')))
					break;
			}
			if (part == 4 && !(line[at] == '.' && line[at + 1] >= '0' && line[at + 1] <= '9'))
				return 1;
		}
	}
	return 0;
}

/* ---------- rates */

int game_chat_rate_take(struct game_chat_rate *rate, unsigned long now, int burst, unsigned long refill_milliseconds)
{
	unsigned long gained;

	if (!rate || burst <= 0 || !refill_milliseconds)
		return 0;
	if (!rate->started)
	{
		rate->started = 1;
		rate->tokens = burst;
		rate->refilled = now;
	}
	gained = (now - rate->refilled) / refill_milliseconds;
	if (gained > 0)
	{
		unsigned long tokens = (unsigned long)rate->tokens + gained;

		rate->tokens = tokens > (unsigned long)burst ? burst : (int)tokens;
		rate->refilled += gained * refill_milliseconds;
	}
	/* (full: the next token comes a whole period after one is spent) */
	if (rate->tokens >= burst)
		rate->refilled = now;
	if (rate->tokens <= 0)
		return 0;
	rate->tokens--;
	return 1;
}

/* ---------- channels */

int game_chat_channel_from_flags(unsigned char flags)
{
	if (flags & GAME_CHAT_LINE_NOTICE_BIT)
		return GAME_CHAT_CHANNEL_NOTICE;
	if (flags & GAME_CHAT_LINE_VEHICLE_BIT)
		return GAME_CHAT_CHANNEL_VEHICLE;
	return flags & GAME_CHAT_LINE_TEAM_BIT ? GAME_CHAT_CHANNEL_TEAM : GAME_CHAT_CHANNEL_ALL;
}

unsigned char game_chat_channel_flags(int channel)
{
	switch (channel)
	{
	case GAME_CHAT_CHANNEL_TEAM: return GAME_CHAT_LINE_TEAM_BIT;
	case GAME_CHAT_CHANNEL_VEHICLE: return GAME_CHAT_LINE_VEHICLE_BIT;
	case GAME_CHAT_CHANNEL_NOTICE: return GAME_CHAT_LINE_NOTICE_BIT;
	default: return 0;
	}
}

int game_chat_hears(int channel, int has_teams, long sender_team, long listener_team, long sender_vehicle,
	long listener_vehicle)
{
	switch (channel)
	{
	case GAME_CHAT_CHANNEL_TEAM:
		return !has_teams || sender_team == listener_team;
	case GAME_CHAT_CHANNEL_VEHICLE:
		return sender_vehicle != -1 && sender_vehicle == listener_vehicle;
	case GAME_CHAT_CHANNEL_ALL:
		return 1;
	default:
		return 0;
	}
}

/* ---------- the wire */

int game_chat_parse_line(const void *entries, unsigned long size, int count, struct game_chat_line *line)
{
	const unsigned char *bytes = (const unsigned char *)entries;

	memset(line, 0, sizeof(*line));
	/* (one line a message, as every build sends it: more is not a line) */
	if (count != 1)
		return GAME_CHAT_PARSE_COUNT;
	if (!bytes || size < GAME_CHAT_LINE_ENTRY_SIZE)
		return GAME_CHAT_PARSE_SIZE;
	if (bytes[1] & ~GAME_CHAT_LINE_FLAGS)
		return GAME_CHAT_PARSE_FLAGS;
	/* (team and vehicle at once is no channel) */
	if ((bytes[1] & GAME_CHAT_LINE_TEAM_BIT) && (bytes[1] & GAME_CHAT_LINE_VEHICLE_BIT))
		return GAME_CHAT_PARSE_FLAGS;
	line->player_index = bytes[0];
	line->flags = bytes[1];
	/* (the text: no further than its 64 bytes, ended or not) */
	if (!game_chat_text_clean(line->text, sizeof(line->text), (const char *)bytes + 4, GAME_CHAT_TEXT_SIZE))
		return GAME_CHAT_PARSE_EMPTY;
	return GAME_CHAT_PARSE_OK;
}

int game_chat_parse_config(const void *entries, unsigned long size, int count, unsigned char *flags)
{
	const unsigned char *bytes = (const unsigned char *)entries;

	*flags = 0;
	if (count != 1)
		return GAME_CHAT_PARSE_COUNT;
	if (!bytes || size < GAME_CHAT_CONFIG_ENTRY_SIZE)
		return GAME_CHAT_PARSE_SIZE;
	/* (bits a later build may give meaning: dropped, not refused) */
	*flags = (unsigned char)(bytes[0] & (GAME_CHAT_CONFIG_ALLOWED_BIT | GAME_CHAT_CONFIG_LINKS_BIT));
	return GAME_CHAT_PARSE_OK;
}

void game_chat_line_entry(unsigned char *entry, unsigned char player_index, unsigned char flags, const char *text)
{
	memset(entry, 0, GAME_CHAT_LINE_ENTRY_SIZE);
	entry[0] = player_index;
	entry[1] = (unsigned char)(flags & GAME_CHAT_LINE_FLAGS);
	game_chat_text_clean((char *)entry + 4, GAME_CHAT_TEXT_SIZE, text, text ? (int)strlen(text) : 0);
}

/* ---------- the quick chat */

const char *game_chat_preset_english(int preset)
{
	return preset >= 0 && preset < GAME_CHAT_PRESET_COUNT ? game_chat_presets[preset] : NULL;
}

int game_chat_preset_find(const char *text)
{
	int preset;

	for (preset = 0; text && preset < GAME_CHAT_PRESET_COUNT; preset++)
	{
		if (!strcmp(text, game_chat_presets[preset]))
			return preset;
	}
	return -1;
}

/* ---------- mutes */

void game_chat_name_key(char *key, const char *name)
{
	int length = 0;

	for (; name && *name && length < GAME_CHAT_NAME_SIZE - 1; name++)
		key[length++] = game_chat_lower(*name);
	key[length] = 0;
}

int game_chat_mute_find(const struct game_chat_mutes *mutes, const char *name)
{
	char key[GAME_CHAT_NAME_SIZE];
	int index;

	game_chat_name_key(key, name);
	if (!key[0])
		return -1;
	for (index = 0; index < mutes->count; index++)
	{
		if (!strcmp(mutes->entries[index].name, key))
			return index;
	}
	return -1;
}

int game_chat_mute_set(struct game_chat_mutes *mutes, const char *name, int muted, int automatic)
{
	int found = game_chat_mute_find(mutes, name);

	if (muted && found < 0)
	{
		char key[GAME_CHAT_NAME_SIZE];

		game_chat_name_key(key, name);
		if (!key[0])
			return mutes->count;
		/* (full: the oldest automatic mute gives way, else none is added) */
		if (mutes->count >= GAME_CHAT_MUTES)
		{
			int index;

			for (index = 0; index < mutes->count && !mutes->entries[index].automatic; index++)
				;
			if (index == mutes->count)
				return mutes->count;
			memmove(&mutes->entries[index], &mutes->entries[index + 1],
				sizeof(mutes->entries[0]) * (size_t)(mutes->count - index - 1));
			mutes->count--;
		}
		memcpy(mutes->entries[mutes->count].name, key, sizeof(key));
		mutes->entries[mutes->count].automatic = automatic;
		mutes->count++;
	}
	else if (!muted && found >= 0)
	{
		memmove(&mutes->entries[found], &mutes->entries[found + 1],
			sizeof(mutes->entries[0]) * (size_t)(mutes->count - found - 1));
		mutes->count--;
	}
	return mutes->count;
}

int game_chat_trip(struct game_chat_trips *trips, const char *name, unsigned long now)
{
	char key[GAME_CHAT_NAME_SIZE];
	int index;

	game_chat_name_key(key, name);
	if (!key[0])
		return 0;
	for (index = 0; index < trips->count && strcmp(trips->entries[index].name, key); index++)
		;
	if (index == trips->count)
	{
		/* (full: the oldest gives way) */
		if (trips->count >= GAME_CHAT_MUTES)
		{
			int oldest = 0, at;

			for (at = 1; at < trips->count; at++)
				if (now - trips->entries[at].first > now - trips->entries[oldest].first)
					oldest = at;
			index = oldest;
		}
		else
			trips->count++;
		memcpy(trips->entries[index].name, key, sizeof(key));
		trips->entries[index].trips = 0;
		trips->entries[index].first = now;
	}
	if (now - trips->entries[index].first > GAME_CHAT_FILTER_TRIP_MILLISECONDS)
	{
		trips->entries[index].trips = 0;
		trips->entries[index].first = now;
	}
	return ++trips->entries[index].trips == GAME_CHAT_FILTER_TRIPS;
}
