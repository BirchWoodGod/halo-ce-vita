/*
UPDATE_CHECK.C

The update notice's parts with no system in them (update_check.h): version
numbers, the channel, GitHub's releases JSON and the cache file's text.
Everything read here comes from the network or a file and is held to fixed
bounds: the text is read by length (never past size, a NUL or not), nesting
at most JSON_DEPTH_MAXIMUM deep, a number at most 63 characters, the strings
kept at most their buffers (a release's notes, read past, as long as the
response), a version's text at most UPDATE_VERSION_SIZE - 1;
nothing read is ever run, opened or followed. tests/update_check_fuzz.c
throws arbitrary bytes at it under AddressSanitizer and UBSan.
*/

#include "update_check.h"

#include <string.h>

/* ---------- versions */

static int is_digit(char character)
{
	return character >= '0' && character <= '9';
}

static int is_identifier_character(char character)
{
	return is_digit(character) || (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
		character == '-';
}

/* a number of at most 9 digits at *text (no overflow); 1 if there was one */
static int read_number(const char **text, unsigned long *number)
{
	const char *cursor = *text;
	unsigned long value = 0;
	int digits = 0;

	while (is_digit(*cursor))
	{
		if (++digits > 9)
			return 0;
		value = value * 10 + (unsigned long)(*cursor++ - '0');
	}
	if (!digits)
		return 0;
	*text = cursor;
	*number = value;
	return 1;
}

int update_version_parse(const char *text, struct update_version *version)
{
	const char *cursor = text;
	size_t length = 0;

	if (!text || !version)
		return 0;
	memset(version, 0, sizeof(*version));
	if (*cursor == 'v' || *cursor == 'V')
		cursor++;
	if (!read_number(&cursor, &version->major) || *cursor++ != '.' || !read_number(&cursor, &version->minor) ||
		*cursor++ != '.' || !read_number(&cursor, &version->patch))
	{
		return 0;
	}
	if (*cursor == '-')
	{
		/* dot-separated identifiers, none empty */
		const char *start = ++cursor;

		for (;;)
		{
			const char *identifier = cursor;

			while (is_identifier_character(*cursor))
				cursor++;
			if (cursor == identifier)
				return 0;
			if (*cursor != '.')
				break;
			cursor++;
		}
		length = (size_t)(cursor - start);
		if (length >= sizeof(version->prerelease))
			return 0;
		memcpy(version->prerelease, start, length);
		version->prerelease[length] = 0;
	}
	if (*cursor == '+')
	{
		/* build metadata: read, not compared */
		const char *start = ++cursor;

		while (is_identifier_character(*cursor) || *cursor == '.')
			cursor++;
		if (cursor == start)
			return 0;
	}
	/* (the whole text, and a short one) */
	return *cursor == 0 && (size_t)(cursor - text) < UPDATE_VERSION_SIZE;
}

/* one pre-release identifier against another: numbers by value and below
words, words by their ASCII */
static int compare_identifier(const char *first, size_t first_length, const char *second, size_t second_length)
{
	int first_numeric = 1, second_numeric = 1;
	size_t index;

	for (index = 0; index < first_length; index++)
		first_numeric &= is_digit(first[index]);
	for (index = 0; index < second_length; index++)
		second_numeric &= is_digit(second[index]);
	if (first_numeric && second_numeric)
	{
		/* (leading zeros skipped, then the longer is larger) */
		while (first_length > 1 && *first == '0')
			first++, first_length--;
		while (second_length > 1 && *second == '0')
			second++, second_length--;
		if (first_length != second_length)
			return first_length < second_length ? -1 : 1;
		return memcmp(first, second, first_length);
	}
	if (first_numeric != second_numeric)
		return first_numeric ? -1 : 1;
	{
		size_t shorter = first_length < second_length ? first_length : second_length;
		int result = memcmp(first, second, shorter);

		if (result)
			return result;
		return first_length == second_length ? 0 : first_length < second_length ? -1 : 1;
	}
}

int update_version_compare(const struct update_version *first, const struct update_version *second)
{
	const char *a, *b;

	if (first->major != second->major)
		return first->major < second->major ? -1 : 1;
	if (first->minor != second->minor)
		return first->minor < second->minor ? -1 : 1;
	if (first->patch != second->patch)
		return first->patch < second->patch ? -1 : 1;
	/* a release is newer than its pre-releases */
	if (!first->prerelease[0] || !second->prerelease[0])
		return (first->prerelease[0] ? -1 : 0) + (second->prerelease[0] ? 1 : 0);
	a = first->prerelease;
	b = second->prerelease;
	for (;;)
	{
		size_t a_length = strcspn(a, "."), b_length = strcspn(b, ".");
		int result = compare_identifier(a, a_length, b, b_length);

		if (result)
			return result < 0 ? -1 : 1;
		a += a_length;
		b += b_length;
		/* (more identifiers: newer) */
		if (!*a || !*b)
			return *a ? 1 : *b ? -1 : 0;
		a++;
		b++;
	}
}

int update_is_newer(const char *latest, const char *current)
{
	struct update_version first, second;

	return update_version_parse(latest, &first) && update_version_parse(current, &second) &&
		update_version_compare(&first, &second) > 0;
}

int update_tag_valid(const char *tag)
{
	const char *cursor = tag;
	int part;

	if (!tag || *cursor++ != 'v')
		return 0;
	for (part = 0; part < 4; part++)
	{
		int digits = 0;

		while (is_digit(*cursor) && digits < 4)
			cursor++, digits++;
		if (digits < 1 || digits > 3)
			return 0;
		if (part < 2)
		{
			if (*cursor++ != '.')
				return 0;
		}
		else if (part == 2)
		{
			if (!*cursor)
				return 1;
			if (strncmp(cursor, "-beta.", 6))
				return 0;
			cursor += 6;
		}
	}
	return *cursor == 0;
}

/* ---------- the channel */

int update_build_is_prerelease(const char *current_version)
{
	struct update_version version;

	/* (not a version at all: as careful as a pre-release) */
	return !update_version_parse(current_version, &version) || version.prerelease[0];
}

int update_channel_default(const char *current_version)
{
	return update_build_is_prerelease(current_version) ? UPDATE_CHANNEL_EXPERIMENTAL : UPDATE_CHANNEL_STABLE;
}

int update_channel_allowed(int channel, const char *current_version)
{
	if (channel == UPDATE_CHANNEL_EXPERIMENTAL)
		return 1;
	return channel == UPDATE_CHANNEL_STABLE && !update_build_is_prerelease(current_version);
}

int update_version_release(const char *version, char *release, int size)
{
	struct update_version parsed;
	char text[40];
	char *cursor = text + sizeof(text);
	unsigned long parts[3];
	int index;

	if (!update_version_parse(version, &parsed) || size <= 0)
		return 0;
	parts[0] = parsed.major;
	parts[1] = parsed.minor;
	parts[2] = parsed.patch;
	*--cursor = 0;
	for (index = 2; index >= 0; index--)
	{
		unsigned long value = parts[index];

		do
		{
			*--cursor = (char)('0' + value % 10);
			value /= 10;
		} while (value);
		if (index)
			*--cursor = '.';
	}
	if (strlen(cursor) >= (size_t)size)
		return 0;
	strcpy(release, cursor);
	return 1;
}

static int same_word(const char *text, const char *word)
{
	for (; *word; text++, word++)
	{
		char character = *text;

		if (character >= 'A' && character <= 'Z')
			character = (char)(character - 'A' + 'a');
		if (character != *word)
			return 0;
	}
	return *text == 0;
}

int update_channel_parse(const char *text, int fallback)
{
	if (!text)
		return fallback;
	if (same_word(text, "stable"))
		return UPDATE_CHANNEL_STABLE;
	if (same_word(text, "experimental"))
		return UPDATE_CHANNEL_EXPERIMENTAL;
	return fallback;
}

const char *update_channel_name(int channel)
{
	return channel == UPDATE_CHANNEL_EXPERIMENTAL ? "experimental" : "stable";
}

/* ---------- the releases' JSON */

#define JSON_DEPTH_MAXIMUM 16
/* the most releases looked at in one answer (the rest are read past) */
#define RELEASES_MAXIMUM 32

struct json_reader
{
	const char *text;
	size_t size, at;
	int failed;
};

/* what a release object holds, of what is looked at */
struct release_fields
{
	char tag[UPDATE_VERSION_SIZE];
	int has_tag, has_prerelease, has_draft, prerelease, draft, duplicate, bad;
};

static void json_space(struct json_reader *reader)
{
	while (reader->at < reader->size)
	{
		char character = reader->text[reader->at];

		if (character != ' ' && character != '\t' && character != '\n' && character != '\r')
			break;
		reader->at++;
	}
}

static int json_peek(struct json_reader *reader)
{
	json_space(reader);
	return reader->at < reader->size ? (unsigned char)reader->text[reader->at] : -1;
}

static int json_expect(struct json_reader *reader, char character)
{
	if (json_peek(reader) != (unsigned char)character)
	{
		reader->failed = 1;
		return 0;
	}
	reader->at++;
	return 1;
}

static int hex_value(char character)
{
	if (is_digit(character))
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

/* a string; its characters (printable ASCII only: anything else is marked
by *wide) into out (at most out_size - 1 of them, *truncated when longer) if
out; 1 if it was a string */
static int json_string(struct json_reader *reader, char *out, size_t out_size, int *wide, int *truncated)
{
	size_t length = 0;

	if (out && out_size)
		out[0] = 0;
	if (!json_expect(reader, '"'))
		return 0;
	while (reader->at < reader->size)
	{
		char character = reader->text[reader->at++];
		unsigned int code;

		if (character == '"')
		{
			if (out && out_size)
				out[length] = 0;
			return 1;
		}
		if ((unsigned char)character < 0x20)
			break;
		code = (unsigned char)character;
		if (character == '\\')
		{
			if (reader->at >= reader->size)
				break;
			character = reader->text[reader->at++];
			switch (character)
			{
			case '"': case '\\': case '/':
				code = (unsigned char)character;
				break;
			case 'b': code = '\b'; break;
			case 'f': code = '\f'; break;
			case 'n': code = '\n'; break;
			case 'r': code = '\r'; break;
			case 't': code = '\t'; break;
			case 'u':
			{
				int index;

				code = 0;
				if (reader->size - reader->at < 4)
				{
					reader->failed = 1;
					return 0;
				}
				for (index = 0; index < 4; index++)
				{
					int value = hex_value(reader->text[reader->at++]);

					if (value < 0)
					{
						reader->failed = 1;
						return 0;
					}
					code = code << 4 | (unsigned int)value;
				}
				break;
			}
			default:
				reader->failed = 1;
				return 0;
			}
		}
		if (code < 0x20 || code > 0x7e)
		{
			if (wide)
				*wide = 1;
			continue;
		}
		if (out && out_size)
		{
			if (length + 1 < out_size)
				out[length++] = (char)code;
			else if (truncated)
				*truncated = 1;
		}
	}
	reader->failed = 1;
	return 0;
}

static int json_value(struct json_reader *reader, int depth, struct release_fields *release);

/* the rest of a literal (true, false, null) or a number: 1, and *boolean
1 or 0 for true or false (else -1) */
static int json_scalar(struct json_reader *reader, int *boolean)
{
	static const char *const literals[] = { "true", "false", "null" };
	size_t index, length;

	*boolean = -1;
	for (index = 0; index < 3; index++)
	{
		length = strlen(literals[index]);
		if (reader->size - reader->at >= length && !memcmp(reader->text + reader->at, literals[index], length))
		{
			reader->at += length;
			*boolean = index == 0 ? 1 : index == 1 ? 0 : -1;
			return 1;
		}
	}
	/* a number, as JSON writes one: -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?,
	at most 63 characters */
	{
		size_t start = reader->at, digits;

#define NUMBER_AT(offset) (reader->at + (offset) < reader->size ? reader->text[reader->at + (offset)] : 0)
		if (NUMBER_AT(0) == '-')
			reader->at++;
		for (digits = 0; is_digit(NUMBER_AT(0)); digits++)
			reader->at++;
		if (!digits || (digits > 1 && reader->text[reader->at - digits] == '0'))
		{
			reader->failed = 1;
			return 0;
		}
		if (NUMBER_AT(0) == '.')
		{
			reader->at++;
			for (digits = 0; is_digit(NUMBER_AT(0)); digits++)
				reader->at++;
			if (!digits)
			{
				reader->failed = 1;
				return 0;
			}
		}
		if (NUMBER_AT(0) == 'e' || NUMBER_AT(0) == 'E')
		{
			reader->at++;
			if (NUMBER_AT(0) == '+' || NUMBER_AT(0) == '-')
				reader->at++;
			for (digits = 0; is_digit(NUMBER_AT(0)); digits++)
				reader->at++;
			if (!digits)
			{
				reader->failed = 1;
				return 0;
			}
		}
#undef NUMBER_AT
		if (reader->at - start >= 64)
		{
			reader->failed = 1;
			return 0;
		}
	}
	return 1;
}

/* an object; release: this object is a release, whose fields go there */
static int json_object(struct json_reader *reader, int depth, struct release_fields *release)
{
	if (!json_expect(reader, '{'))
		return 0;
	if (json_peek(reader) == '}')
	{
		reader->at++;
		return 1;
	}
	for (;;)
	{
		char key[16];
		int wide = 0, truncated = 0;

		if (!json_string(reader, key, sizeof(key), &wide, &truncated) || !json_expect(reader, ':'))
			return 0;
		if (release && !wide && !truncated && !strcmp(key, "tag_name"))
		{
			int value_wide = 0, value_truncated = 0;

			if (json_peek(reader) != '"')
			{
				/* (not a string: the release is not taken) */
				release->bad = 1;
				if (!json_value(reader, depth + 1, NULL))
					return 0;
			}
			else
			{
				if (!json_string(reader, release->tag, sizeof(release->tag), &value_wide, &value_truncated))
					return 0;
				if (value_wide || value_truncated)
					release->bad = 1;
			}
			release->duplicate |= release->has_tag;
			release->has_tag = 1;
		}
		else if (release && !wide && !truncated && (!strcmp(key, "prerelease") || !strcmp(key, "draft")))
		{
			int is_prerelease = !strcmp(key, "prerelease");
			int *seen = is_prerelease ? &release->has_prerelease : &release->has_draft;
			int boolean = -1;

			json_space(reader);
			if (json_peek(reader) == '{' || json_peek(reader) == '[' || json_peek(reader) == '"')
			{
				if (!json_value(reader, depth + 1, NULL))
					return 0;
			}
			else if (!json_scalar(reader, &boolean))
			{
				return 0;
			}
			if (boolean < 0)
				release->bad = 1;
			else if (is_prerelease)
				release->prerelease = boolean;
			else
				release->draft = boolean;
			release->duplicate |= *seen;
			*seen = 1;
		}
		else if (!json_value(reader, depth + 1, NULL))
		{
			return 0;
		}
		if (json_peek(reader) == ',')
		{
			reader->at++;
			continue;
		}
		return json_expect(reader, '}');
	}
}

/* a release read: whether the channel takes it, and its version */
static int release_taken(const struct release_fields *release, int channel, struct update_version *version)
{
	if (!release->has_tag || release->bad || release->duplicate || release->draft || !update_tag_valid(release->tag) ||
		!update_version_parse(release->tag, version))
	{
		return 0;
	}
	if (channel == UPDATE_CHANNEL_STABLE && (release->prerelease || version->prerelease[0]))
		return 0;
	return channel == UPDATE_CHANNEL_STABLE || channel == UPDATE_CHANNEL_EXPERIMENTAL;
}

struct pick
{
	int channel, found, count;
	struct update_version best;
	char tag[UPDATE_VERSION_SIZE];
};

static int json_array(struct json_reader *reader, int depth, struct pick *pick)
{
	if (!json_expect(reader, '['))
		return 0;
	if (json_peek(reader) == ']')
	{
		reader->at++;
		return 1;
	}
	for (;;)
	{
		if (pick && pick->count < RELEASES_MAXIMUM && json_peek(reader) == '{')
		{
			struct release_fields release;
			struct update_version version;

			memset(&release, 0, sizeof(release));
			pick->count++;
			if (!json_object(reader, depth + 1, &release))
				return 0;
			if (release_taken(&release, pick->channel, &version) &&
				(!pick->found || update_version_compare(&version, &pick->best) > 0))
			{
				pick->best = version;
				pick->found = 1;
				memcpy(pick->tag, release.tag, sizeof(pick->tag));
			}
		}
		else if (!json_value(reader, depth + 1, NULL))
		{
			return 0;
		}
		if (json_peek(reader) == ',')
		{
			reader->at++;
			continue;
		}
		return json_expect(reader, ']');
	}
}

static int json_value(struct json_reader *reader, int depth, struct release_fields *release)
{
	int boolean;

	if (depth > JSON_DEPTH_MAXIMUM)
	{
		reader->failed = 1;
		return 0;
	}
	switch (json_peek(reader))
	{
	case '{':
		return json_object(reader, depth, release);
	case '[':
		return json_array(reader, depth, NULL);
	case '"':
		return json_string(reader, NULL, 0, NULL, NULL);
	case -1:
		reader->failed = 1;
		return 0;
	default:
		return json_scalar(reader, &boolean);
	}
}

int update_releases_pick(const char *json, size_t size, int channel, char *latest, int latest_size)
{
	struct json_reader reader;
	struct pick pick;
	int ok;

	if (latest && latest_size > 0)
		latest[0] = 0;
	if (!json || !latest || latest_size <= 0)
		return -1;
	memset(&reader, 0, sizeof(reader));
	memset(&pick, 0, sizeof(pick));
	reader.text = json;
	reader.size = size;
	pick.channel = channel;
	if (json_peek(&reader) == '[')
	{
		ok = json_array(&reader, 1, &pick);
	}
	else if (json_peek(&reader) == '{')
	{
		/* (one release: /releases/latest) */
		struct release_fields release;
		struct update_version version;

		memset(&release, 0, sizeof(release));
		ok = json_object(&reader, 1, &release);
		if (ok && release_taken(&release, channel, &version))
		{
			pick.found = 1;
			memcpy(pick.tag, release.tag, sizeof(pick.tag));
		}
	}
	else
	{
		return -1;
	}
	/* (nothing but space after it) */
	if (!ok || reader.failed || json_peek(&reader) != -1)
		return -1;
	if (!pick.found)
		return 0;
	{
		const char *tag = pick.tag[0] == 'v' || pick.tag[0] == 'V' ? pick.tag + 1 : pick.tag;
		size_t length = strlen(tag);

		if (length >= (size_t)latest_size)
			return 0;
		memcpy(latest, tag, length + 1);
	}
	return 1;
}

/* ---------- the cache file */

int update_cache_format(char *text, int size, const struct update_cache *cache)
{
	char number[24];
	char *cursor = number + sizeof(number);
	unsigned long long value = cache->checked > 0 ? (unsigned long long)cache->checked : 0;
	size_t length;
	static const char header[] = "# Halo CE for PS Vita: the last look for a new version (safe to delete)\n";
	const char *lines[5];
	int index, total = 0;

	*--cursor = 0;
	do
	{
		*--cursor = (char)('0' + value % 10);
		value /= 10;
	} while (value);
	lines[0] = header;
	lines[1] = "checked=";
	lines[2] = cursor;
	lines[3] = "\nchannel=";
	lines[4] = update_channel_name(cache->channel);
	if (size <= 0)
		return 0;
	text[0] = 0;
	for (index = 0; index < 5; index++)
	{
		length = strlen(lines[index]);
		if ((size_t)total + length >= (size_t)size)
			return 0;
		memcpy(text + total, lines[index], length + 1);
		total += (int)length;
	}
	{
		const char *result = cache->ok ? "\nresult=ok\nlatest=" : "\nresult=failed\nlatest=";
		const char *latest = cache->ok ? cache->latest : "";
		struct update_version version;

		if (latest[0] && !update_version_parse(latest, &version))
			latest = "";
		for (index = 0; index < 3; index++)
		{
			const char *part = index == 0 ? result : index == 1 ? latest : "\n";

			length = strlen(part);
			if ((size_t)total + length >= (size_t)size)
				return 0;
			memcpy(text + total, part, length + 1);
			total += (int)length;
		}
	}
	return total;
}

int update_cache_parse(const char *text, size_t size, struct update_cache *cache)
{
	size_t at = 0;
	int has_checked = 0, has_channel = 0, has_result = 0;

	memset(cache, 0, sizeof(*cache));
	if (!text || size > 4096)
		return 0;
	while (at < size)
	{
		char line[128];
		size_t length = 0;
		char *equals, *value;

		while (at < size && text[at] != '\n')
		{
			if (length + 1 >= sizeof(line))
				return 0;
			line[length++] = text[at++];
		}
		at++;
		while (length && (line[length - 1] == '\r' || line[length - 1] == ' '))
			length--;
		line[length] = 0;
		if (!length || line[0] == '#')
			continue;
		equals = strchr(line, '=');
		if (!equals)
			return 0;
		*equals = 0;
		value = equals + 1;
		if (!strcmp(line, "checked"))
		{
			long long checked = 0;
			int digits = 0;

			for (; *value; value++)
			{
				if (!is_digit(*value) || ++digits > 12)
					return 0;
				checked = checked * 10 + (*value - '0');
			}
			if (!digits)
				return 0;
			cache->checked = checked;
			has_checked = 1;
		}
		else if (!strcmp(line, "channel"))
		{
			cache->channel = update_channel_parse(value, -1);
			if (cache->channel < 0)
				return 0;
			has_channel = 1;
		}
		else if (!strcmp(line, "result"))
		{
			if (strcmp(value, "ok") && strcmp(value, "failed"))
				return 0;
			cache->ok = !strcmp(value, "ok");
			has_result = 1;
		}
		else if (!strcmp(line, "latest"))
		{
			struct update_version version;

			if (value[0] && !update_version_parse(value, &version))
				return 0;
			if (strlen(value) >= sizeof(cache->latest))
				return 0;
			strcpy(cache->latest, value);
		}
	}
	if (!cache->ok)
		cache->latest[0] = 0;
	return has_checked && has_channel && has_result;
}

int update_cache_fresh(long long checked, long long now, long seconds)
{
	return checked > 0 && checked <= now && now - checked < seconds;
}
