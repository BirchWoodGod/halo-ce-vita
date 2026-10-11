/*
MOD_CAMPAIGN_FILES.C

(port) The campaign files of a Halo: The Master Chief Collection mod
(mod_campaign_files.h).

A small JSON reader: it walks the whole value, as RFC 8259 writes it, and
hands each string it meets, with the names of the members it lies in
("Title" then "Neutral"; "CampaignMaps" then an array's element), to the
file's own reader, which keeps the few it wants. Whitespace, numbers,
true, false and null are taken as JSON has them and kept nowhere. Every
read is within the text; nesting deeper than MAXIMUM_DEPTH ends the read.
*/

/* ---------- headers */

#include "mod_campaign_files.h"

#include <string.h>

/* ---------- constants */

/* objects and arrays within each other: the mods' files have three */
#define MAXIMUM_DEPTH 8
/* the member names kept while walking, each cut to this */
#define MEMBER_NAME_BYTES 32
/* an array's element, as a member name */
#define ELEMENT_NAME "#"

/* ---------- structures */

struct json_reader
{
	uint8_t const *text;
	uint32_t size;
	uint32_t position;
	/* the member names of the values being read, outermost first */
	int16_t depth;
	char names[MAXIMUM_DEPTH][MEMBER_NAME_BYTES];
	/* called with each string, its member names, and the context */
	void (*visit)(struct json_reader const *reader, char const *value, void *context);
	void *context;
};

/* ---------- globals */

static char const *const mod_campaign_file_status_descriptions[NUMBER_OF_MOD_CAMPAIGN_FILE_STATUSES] =
{
	"ok",
	"not a JSON object",
	"nested too deeply",
	"malformed JSON",
};

/* ---------- prototypes */

static enum mod_campaign_file_status json_value_read(
	struct json_reader *reader);

/* ---------- private code */

static void json_space_skip(
	struct json_reader *reader)
{
	while (reader->position < reader->size &&
		(reader->text[reader->position] == ' ' || reader->text[reader->position] == '\t' ||
		reader->text[reader->position] == '\n' || reader->text[reader->position] == '\r'))
	{
		reader->position++;
	}

	return;
}

/* the next character, or -1 at the end of the text */
static int json_peek(
	struct json_reader *reader)
{
	json_space_skip(reader);

	return reader->position < reader->size ? reader->text[reader->position] : -1;
}

/* a hexadecimal digit's value, or -1 */
static int hexadecimal_digit(
	uint8_t character)
{
	if (character >= '0' && character <= '9')
	{
		return character - '0';
	}
	if (character >= 'a' && character <= 'f')
	{
		return character - 'a' + 10;
	}
	if (character >= 'A' && character <= 'F')
	{
		return character - 'A' + 10;
	}

	return -1;
}

/* Reads a string (the reader at its opening quote) into `out` (`out_bytes`
of room, at least 1), as printable ASCII: each escaped or UTF-8 character
beyond it a '?', control characters refused, what does not fit dropped. */
static enum mod_campaign_file_status json_string_read(
	struct json_reader *reader,
	char *out,
	uint32_t out_bytes)
{
	uint32_t length = 0;

	reader->position++;
	while (reader->position < reader->size)
	{
		uint8_t character = reader->text[reader->position++];
		char kept;

		if (character == '"')
		{
			out[length] = 0;
			return _mod_campaign_file_status_ok;
		}
		if (character < 0x20)
		{
			return _mod_campaign_file_status_malformed;
		}
		if (character == '\\')
		{
			if (reader->position >= reader->size)
			{
				return _mod_campaign_file_status_malformed;
			}
			character = reader->text[reader->position++];
			switch (character)
			{
			case '"': case '\\': case '/':
				kept = (char)character;
				break;
			case 'b': case 'f': case 'n': case 'r': case 't':
				kept = ' ';
				break;
			case 'u':
			{
				uint32_t value = 0;
				int digit_index;

				if (reader->size - reader->position < 4)
				{
					return _mod_campaign_file_status_malformed;
				}
				for (digit_index = 0; digit_index < 4; digit_index++)
				{
					int digit = hexadecimal_digit(reader->text[reader->position++]);

					if (digit < 0)
					{
						return _mod_campaign_file_status_malformed;
					}
					value = value << 4 | (uint32_t)digit;
				}
				kept = value >= 0x20 && value < 0x7F ? (char)value : '?';
				/* (the second half of a pair, its own escape, is another '?':
				harmless) */
				break;
			}
			default:
				return _mod_campaign_file_status_malformed;
			}
		}
		else if (character < 0x7F)
		{
			kept = (char)character;
		}
		else if (character >= 0xC0)
		{
			/* the first byte of a character UTF-8 writes in several */
			kept = '?';
		}
		else
		{
			/* (the further bytes of one, or DEL) */
			continue;
		}
		if (length + 1 < out_bytes)
		{
			out[length++] = kept;
		}
	}

	return _mod_campaign_file_status_malformed;
}

/* skips a number, true, false or null: the characters they are made of */
static enum mod_campaign_file_status json_literal_skip(
	struct json_reader *reader)
{
	uint32_t start = reader->position;

	while (reader->position < reader->size)
	{
		uint8_t character = reader->text[reader->position];

		if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') ||
			character == '-' || character == '+' || character == '.' || character == 'E'))
		{
			break;
		}
		reader->position++;
	}

	return reader->position > start ? _mod_campaign_file_status_ok : _mod_campaign_file_status_malformed;
}

static enum mod_campaign_file_status json_container_read(
	struct json_reader *reader,
	int object)
{
	enum mod_campaign_file_status status;
	int first = 1;

	if (reader->depth >= MAXIMUM_DEPTH)
	{
		return _mod_campaign_file_status_too_deep;
	}
	reader->position++;
	for (;;)
	{
		int character = json_peek(reader);

		if (character == (object ? '}' : ']'))
		{
			reader->position++;
			return _mod_campaign_file_status_ok;
		}
		if (!first)
		{
			if (character != ',')
			{
				return _mod_campaign_file_status_malformed;
			}
			reader->position++;
			character = json_peek(reader);
		}
		first = 0;
		if (object)
		{
			if (character != '"')
			{
				return _mod_campaign_file_status_malformed;
			}
			status = json_string_read(reader, reader->names[reader->depth], MEMBER_NAME_BYTES);
			if (status != _mod_campaign_file_status_ok)
			{
				return status;
			}
			if (json_peek(reader) != ':')
			{
				return _mod_campaign_file_status_malformed;
			}
			reader->position++;
		}
		else
		{
			strcpy(reader->names[reader->depth], ELEMENT_NAME);
		}
		reader->depth++;
		status = json_value_read(reader);
		reader->depth--;
		if (status != _mod_campaign_file_status_ok)
		{
			return status;
		}
	}
}

static enum mod_campaign_file_status json_value_read(
	struct json_reader *reader)
{
	int character = json_peek(reader);

	if (character == '{' || character == '[')
	{
		return json_container_read(reader, character == '{');
	}
	if (character == '"')
	{
		char value[MOD_CAMPAIGN_DESCRIPTION_BYTES];
		enum mod_campaign_file_status status = json_string_read(reader, value, sizeof(value));

		if (status == _mod_campaign_file_status_ok)
		{
			reader->visit(reader, value, reader->context);
		}
		return status;
	}
	if (character < 0)
	{
		return _mod_campaign_file_status_malformed;
	}

	return json_literal_skip(reader);
}

/* Walks the `size` bytes at `text`, a JSON object (after a UTF-8 byte order
mark, if any), calling `visit` with each string in it. */
static enum mod_campaign_file_status json_walk(
	uint8_t const *text,
	uint32_t size,
	void (*visit)(struct json_reader const *reader, char const *value, void *context),
	void *context)
{
	struct json_reader reader;
	enum mod_campaign_file_status status;

	if (!text || !size || size > MOD_CAMPAIGN_FILE_MAXIMUM_BYTES)
	{
		return _mod_campaign_file_status_not_json;
	}
	memset(&reader, 0, sizeof(reader));
	reader.text = text;
	reader.size = size;
	reader.visit = visit;
	reader.context = context;
	if (size >= 3 && text[0] == 0xEF && text[1] == 0xBB && text[2] == 0xBF)
	{
		reader.position = 3;
	}
	if (json_peek(&reader) != '{')
	{
		return _mod_campaign_file_status_not_json;
	}
	status = json_value_read(&reader);
	if (status == _mod_campaign_file_status_ok && json_peek(&reader) >= 0)
	{
		status = _mod_campaign_file_status_malformed;
	}

	return status;
}

/* whether the value being visited lies in the members `first` then `second`
(NULL: directly in `first`) of the file's object */
static int json_member_is(
	struct json_reader const *reader,
	char const *first,
	char const *second)
{
	return second ?
		reader->depth == 2 && !strcmp(reader->names[0], first) && !strcmp(reader->names[1], second) :
		reader->depth == 1 && !strcmp(reader->names[0], first);
}

static void text_keep(
	char *out,
	uint32_t out_bytes,
	char const *value)
{
	size_t length = strlen(value);

	if (length >= out_bytes)
	{
		length = out_bytes - 1;
	}
	memcpy(out, value, length);
	out[length] = 0;

	return;
}

static void campaign_info_visit(
	struct json_reader const *reader,
	char const *value,
	void *context)
{
	struct mod_campaign_info *info = context;

	if (json_member_is(reader, "Title", "Neutral"))
	{
		text_keep(info->title, sizeof(info->title), value);
	}
	else if (json_member_is(reader, "CampaignMaps", ELEMENT_NAME) && info->level_count < MOD_CAMPAIGN_MAXIMUM_LEVELS)
	{
		text_keep(info->levels[info->level_count++], MOD_CAMPAIGN_FILE_NAME_BYTES, value);
	}

	return;
}

static void campaign_level_visit(
	struct json_reader const *reader,
	char const *value,
	void *context)
{
	struct mod_campaign_level *level = context;

	if (json_member_is(reader, "ScenarioFile", NULL))
	{
		text_keep(level->scenario_file, sizeof(level->scenario_file), value);
	}
	else if (json_member_is(reader, "Title", "Neutral"))
	{
		text_keep(level->title, sizeof(level->title), value);
	}
	else if (json_member_is(reader, "Description", "Neutral"))
	{
		text_keep(level->description, sizeof(level->description), value);
	}
	else if (json_member_is(reader, "Images", "Thumbnail"))
	{
		text_keep(level->thumbnail, sizeof(level->thumbnail), value);
	}
	else if (json_member_is(reader, "Images", "Large"))
	{
		text_keep(level->large_image, sizeof(level->large_image), value);
	}

	return;
}

/* ---------- public code */

char const *mod_campaign_file_status_describe(
	enum mod_campaign_file_status status)
{
	return status >= 0 && status < NUMBER_OF_MOD_CAMPAIGN_FILE_STATUSES ?
		mod_campaign_file_status_descriptions[status] :
		"unknown status";
}

enum mod_campaign_file_status mod_campaign_info_read(
	uint8_t const *text,
	uint32_t size,
	struct mod_campaign_info *info)
{
	memset(info, 0, sizeof(*info));

	return json_walk(text, size, campaign_info_visit, info);
}

enum mod_campaign_file_status mod_campaign_level_read(
	uint8_t const *text,
	uint32_t size,
	struct mod_campaign_level *level)
{
	memset(level, 0, sizeof(*level));

	return json_walk(text, size, campaign_level_visit, level);
}
