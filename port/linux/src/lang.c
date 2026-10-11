/*
LANG.C

The port's own text in the player's language: the settings panel, the
messages, questions and progress lines over the game, the server browser's
and co-op's lines, the missing data screen (lang.h). Each language other
than English is a file of UTF-8 text named by its code, read once at start
from the directory lang_init is given (the Vita: app0:lang, in the game's
package, port/vita/app0/lang/); the languages found are the settings
panel's Language row's choices. A line of a file is

	"English text" = "the text in the language"

with C's escapes in either (\n \r \t \" \\), and

	@name = "Español"

names the language, as its row shows it; a line starting with # is a
comment. The English is the key, as the code has it: lang_text("English
text") finds the line by it (a hash table a language), and a string it has
no line for stays English. A text with printf's conversions (%d, %.40s) is
taken only when its translation has the same ones in the same order, so a
file can never make the port read an argument it was not given; the
problem is reported (lang_problems, halo.log) and the English used.

A language's tables are made once, at lang_init, and never freed or
changed: lang_text gives out pointers into them, from any thread, and
lang_select only changes which table is read (atomically). The UTF-16 copy
of a text (lang_text_wide, the strings the port draws with Halo's fonts)
is made the first time it is asked for, and kept.

Halo's own text is the player's map files' (ui.map's string lists, the
levels' HUD messages and subtitles): none of it is here. See
port/vita/README.md, "Translations".
*/

#include "lang.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the codes a file is looked for under, in the Language row's order: one
for each of the Vita's system languages (vita_settings.c's
system_language_codes) */
static const char *const lang_file_codes[] = {
	"es", "fr", "de", "it", "pt", "pt-BR", "nl", "ru", "pl", "fi", "sv", "da", "no", "tr", "ja", "ko", "zh-TW",
	"zh-CN",
};

#define LANG_FILE_CODES ((int)(sizeof(lang_file_codes) / sizeof(lang_file_codes[0])))

struct lang_entry
{
	const char *english;
	const char *text;
	unsigned int hash;
	/* (the text as UTF-16, made when first asked for: lang_text_wide) */
	void *wide;
};

struct lang_table
{
	char code[8];
	char name[48];
	/* a power of two, at least twice the entries */
	unsigned int size;
	struct lang_entry *entries;
};

static struct lang_table tables[LANG_MAXIMUM];
static int table_count;
static int initialized;
/* the table read (NULL: English) */
static struct lang_table *current;

static char problems[2048];
static int problem_count;

/* the strings asked for that had no entry (lang_missing) */
#define MISSING_SIZE 2048
static char missing[MISSING_SIZE];
static int missing_count, missing_tracked;

static unsigned int lang_hash(const char *text)
{
	unsigned int hash = 2166136261u;

	for (; *text; text++)
		hash = (hash ^ (unsigned char)*text) * 16777619u;
	return hash;
}

static void problem(const char *file, int line, const char *what, const char *english)
{
	int used = (int)strlen(problems);

	problem_count++;
	if (used < (int)sizeof(problems) - 1)
		snprintf(problems + used, sizeof(problems) - (size_t)used, "%s:%d: %s%s%.40s%s\n", file, line, what,
			english ? " \"" : "", english ? english : "", english ? "\"" : "");
}

/* printf's conversions of `text`, each as written ("%.40s"), in order,
'|' after each (%% is none); 0 if they do not fit */
static int format_conversions(const char *text, char *out, int size)
{
	int used = 0;

	out[0] = 0;
	while ((text = strchr(text, '%')) != NULL)
	{
		const char *start = text++;
		size_t length;

		if (*text == '%')
		{
			text++;
			continue;
		}
		text += strspn(text, "-+ #0");
		text += strspn(text, "0123456789*");
		if (*text == '.')
		{
			text++;
			text += strspn(text, "0123456789*");
		}
		text += strspn(text, "hlLqjzt");
		if (*text)
			text++;
		length = (size_t)(text - start);
		if (used + (int)length + 2 > size)
			return 0;
		memcpy(out + used, start, length);
		used += (int)length;
		out[used++] = '|';
		out[used] = 0;
	}
	return 1;
}

/* a quoted string at `at` (after spaces), its escapes undone into `out`
(at least as long as the line); the text after it, or NULL */
static char *quoted(char *at, char *out)
{
	while (*at == ' ' || *at == '\t')
		at++;
	if (*at++ != '"')
		return NULL;
	while (*at && *at != '"')
	{
		if (*at == '\\')
		{
			at++;
			switch (*at)
			{
			case 'n': *out++ = '\n'; break;
			case 'r': *out++ = '\r'; break;
			case 't': *out++ = '\t'; break;
			case '"': *out++ = '"'; break;
			case '\\': *out++ = '\\'; break;
			default: return NULL;
			}
			at++;
		}
		else
			*out++ = *at++;
	}
	if (*at != '"')
		return NULL;
	*out = 0;
	return at + 1;
}

/* the file of `code` read into a table; nonzero if there was one */
static int lang_load(const char *directory, const char *code, struct lang_table *table)
{
	char path[512], file_name[16];
	FILE *file;
	long length;
	char *text, *storage, *line, *next;
	size_t stored = 0;
	int count, line_number = 0;
	struct lang_entry *entries;

	snprintf(path, sizeof(path), "%s/%s.txt", directory, code);
	snprintf(file_name, sizeof(file_name), "%s.txt", code);
	file = fopen(path, "rb");
	if (!file)
		return 0;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (length <= 0 || length > 4 * 1024 * 1024)
	{
		fclose(file);
		return 0;
	}
	text = malloc((size_t)length + 1);
	/* (the texts unescaped: never longer than the file) */
	storage = malloc((size_t)length + 1);
	if (!text || !storage || fread(text, 1, (size_t)length, file) != (size_t)length)
	{
		free(text);
		free(storage);
		fclose(file);
		return 0;
	}
	fclose(file);
	text[length] = 0;
	/* (a byte order mark, left by some editors) */
	if (!memcmp(text, "\xEF\xBB\xBF", 3))
		memmove(text, text + 3, (size_t)length - 2);
	/* (the lines: as many entries at most) */
	for (line = text, count = 1; (line = strchr(line, '\n')) != NULL; line++)
		count++;
	memset(table, 0, sizeof(*table));
	snprintf(table->code, sizeof(table->code), "%s", code);
	snprintf(table->name, sizeof(table->name), "%s", code);
	for (table->size = 16; table->size < (unsigned int)count * 2; table->size *= 2)
		;
	entries = calloc(table->size, sizeof(*entries));
	if (!entries)
	{
		free(text);
		free(storage);
		return 0;
	}
	table->entries = entries;
	for (line = text; *line; line = next)
	{
		char *end = line + strcspn(line, "\n");
		char *english, *translation, *after;

		next = end + (*end == '\n');
		*end = 0;
		line_number++;
		if (end > line && end[-1] == '\r')
			end[-1] = 0;
		while (*line == ' ' || *line == '\t')
			line++;
		if (!*line || *line == '#')
			continue;
		if (*line == '@')
		{
			char *equals = strchr(line, '=');

			english = storage + stored;
			if (!strncmp(line, "@name", 5) && equals && quoted(equals + 1, english))
				snprintf(table->name, sizeof(table->name), "%s", english);
			else
				problem(file_name, line_number, "an unknown @ line", NULL);
			continue;
		}
		english = storage + stored;
		after = quoted(line, english);
		if (after)
		{
			stored += strlen(english) + 1;
			while (*after == ' ' || *after == '\t')
				after++;
		}
		if (!after || *after != '=')
		{
			problem(file_name, line_number, "not \"English\" = \"translation\"", NULL);
			if (after)
				stored -= strlen(english) + 1;
			continue;
		}
		translation = storage + stored;
		after = quoted(after + 1, translation);
		while (after && (*after == ' ' || *after == '\t'))
			after++;
		if (!after || (*after && *after != '#'))
		{
			problem(file_name, line_number, "the translation is not one quoted string", english);
			stored -= strlen(english) + 1;
			continue;
		}
		stored += strlen(translation) + 1;
		{
			char english_conversions[256], translation_conversions[256];

			if (!format_conversions(english, english_conversions, sizeof(english_conversions)) ||
				!format_conversions(translation, translation_conversions, sizeof(translation_conversions)) ||
				strcmp(english_conversions, translation_conversions))
			{
				problem(file_name, line_number, "its % conversions are not the English's", english);
				continue;
			}
		}
		{
			unsigned int hash = lang_hash(english), slot = hash & (table->size - 1);

			while (entries[slot].english && strcmp(entries[slot].english, english))
				slot = (slot + 1) & (table->size - 1);
			if (entries[slot].english)
				problem(file_name, line_number, "a second line for", english);
			entries[slot].english = english;
			entries[slot].text = translation;
			entries[slot].hash = hash;
		}
	}
	free(text);
	return 1;
}

void lang_init(const char *directory)
{
	int index;

	if (initialized)
		return;
	initialized = 1;
	memset(tables, 0, sizeof(tables));
	snprintf(tables[0].code, sizeof(tables[0].code), "en");
	snprintf(tables[0].name, sizeof(tables[0].name), "English");
	table_count = 1;
	for (index = 0; index < LANG_FILE_CODES && table_count < LANG_MAXIMUM && directory; index++)
		if (lang_load(directory, lang_file_codes[index], &tables[table_count]))
			table_count++;
}

static struct lang_table *table_of(const char *code)
{
	int index;
	size_t length;

	if (!code || !*code)
		return NULL;
	for (index = 1; index < table_count; index++)
		if (!strcmp(tables[index].code, code))
			return &tables[index];
	/* (a code with a region: the language's file) */
	length = strcspn(code, "-_");
	for (index = 1; index < table_count; index++)
		if (strlen(tables[index].code) == length && !strncmp(tables[index].code, code, length))
			return &tables[index];
	return NULL;
}

int lang_select(const char *code)
{
	struct lang_table *table = table_of(code);

	__atomic_store_n(&current, table, __ATOMIC_RELEASE);
	return table != NULL;
}

const char *lang_current(void)
{
	struct lang_table *table = __atomic_load_n(&current, __ATOMIC_ACQUIRE);

	return table ? table->code : "en";
}

int lang_count(void)
{
	return table_count ? table_count : 1;
}

const char *lang_code(int index)
{
	return index > 0 && index < table_count ? tables[index].code : "en";
}

const char *lang_name(int index)
{
	return index > 0 && index < table_count ? tables[index].name : "English";
}

static const struct lang_entry *entry_of(const struct lang_table *table, const char *english)
{
	unsigned int hash = lang_hash(english), slot = hash & (table->size - 1);

	while (table->entries[slot].english)
	{
		if (table->entries[slot].hash == hash && !strcmp(table->entries[slot].english, english))
			return &table->entries[slot];
		slot = (slot + 1) & (table->size - 1);
	}
	return NULL;
}

/* (tests) a string with letters that the language has no entry for */
static void note_missing(const char *english)
{
	const char *letter;
	size_t length = strlen(english), used = strlen(missing);

	for (letter = english; *letter; letter++)
		if ((*letter >= 'a' && *letter <= 'z') || (*letter >= 'A' && *letter <= 'Z'))
			break;
	if (!*letter)
		return;
	/* (each once) */
	{
		const char *at = missing;

		while ((at = strstr(at, english)) != NULL)
		{
			if ((at == missing || at[-1] == '\n') && at[length] == '\n')
				return;
			at++;
		}
	}
	missing_count++;
	if (used + length + 2 < sizeof(missing))
		snprintf(missing + used, sizeof(missing) - used, "%s\n", english);
}

const char *lang_text(const char *english)
{
	struct lang_table *table = __atomic_load_n(&current, __ATOMIC_ACQUIRE);
	const struct lang_entry *entry;

	if (!table || !english || !*english)
		return english;
	entry = entry_of(table, english);
	if (entry)
		return entry->text;
	if (missing_tracked)
		note_missing(english);
	return english;
}

int lang_utf8_length(const char *text)
{
	int count = 0;

	for (; text && *text; text++)
		if (((unsigned char)*text & 0xC0) != 0x80)
			count++;
	return count;
}

int lang_problems(char *report, int size)
{
	if (report && size > 0)
		snprintf(report, (size_t)size, "%s", problems);
	return problem_count;
}

int lang_missing(char *report, int size)
{
	int count = missing_count;

	if (report && size > 0)
		snprintf(report, (size_t)size, "%s", missing);
	missing[0] = 0;
	missing_count = 0;
	missing_tracked = 1;
	return count;
}

#if defined(__SIZEOF_WCHAR_T__) && __SIZEOF_WCHAR_T__ == 2
const wchar_t *lang_text_wide(const wchar_t *english)
{
	struct lang_table *table = __atomic_load_n(&current, __ATOMIC_ACQUIRE);
	char key[1024];
	struct lang_entry *entry;
	size_t used = 0;
	const wchar_t *at;
	unsigned short *wide;
	const unsigned char *text;
	size_t length = 0;

	if (!table || !english)
		return english;
	/* the English as UTF-8 (the game's text is the Basic Multilingual
	Plane's) */
	for (at = english; *at && used + 4 < sizeof(key); at++)
	{
		unsigned int character = (unsigned short)*at;

		if (character < 0x80)
			key[used++] = (char)character;
		else if (character < 0x800)
		{
			key[used++] = (char)(0xC0 | (character >> 6));
			key[used++] = (char)(0x80 | (character & 0x3F));
		}
		else
		{
			key[used++] = (char)(0xE0 | (character >> 12));
			key[used++] = (char)(0x80 | ((character >> 6) & 0x3F));
			key[used++] = (char)(0x80 | (character & 0x3F));
		}
	}
	key[used] = 0;
	entry = (struct lang_entry *)entry_of(table, key);
	if (!entry)
	{
		if (missing_tracked)
			note_missing(key);
		return english;
	}
	wide = __atomic_load_n((unsigned short **)&entry->wide, __ATOMIC_ACQUIRE);
	if (wide)
		return (const wchar_t *)wide;
	/* the translation as UTF-16, once */
	for (text = (const unsigned char *)entry->text; *text; text++)
		length += (*text & 0xC0) != 0x80;
	wide = malloc((length + 1) * sizeof(*wide));
	if (!wide)
		return english;
	length = 0;
	for (text = (const unsigned char *)entry->text; *text; )
	{
		unsigned int character = *text++;

		if (character >= 0xE0 && text[0] && text[1])
		{
			character = ((character & 0x0F) << 12) | ((text[0] & 0x3Fu) << 6) | (text[1] & 0x3Fu);
			text += 2;
		}
		else if (character >= 0xC0 && text[0])
		{
			character = ((character & 0x1F) << 6) | (text[0] & 0x3Fu);
			text++;
		}
		while ((*text & 0xC0) == 0x80)
			text++;
		wide[length++] = (unsigned short)character;
	}
	wide[length] = 0;
	{
		void *expected = NULL;

		/* (another thread's copy, made meanwhile, is the one kept) */
		if (!__atomic_compare_exchange_n(&entry->wide, &expected, wide, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		{
			free(wide);
			wide = expected;
		}
	}
	return (const wchar_t *)wide;
}
#endif
