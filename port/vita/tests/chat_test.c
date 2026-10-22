/*
CHAT_TEST.C

A desktop test of the in-game chat's rules (port/linux/game/game_chat_protocol.c)
and its slur filter (port/linux/src/chat_filter.c with the shipped words,
port/vita/app0/chat/filter.txt):

- the filter: every word of the list caught as it is, in capitals, in a
  sentence, spelled with leet (4 a, 3 e, 1 i, 0 o, 5 s, @ a, $ s), spaced
  ("w o r d"), dotted ("w.o.r.d"), a letter repeated, and a whole word's
  plural; a whole word's never inside another word, a start word's never
  after another's start; Scunthorpe cases (common words and Halo's own that
  hold a word of the list: assassin, Scunthorpe, raccoon, cocoon, Japan,
  Pakistan, Mongoose, retardant, spicy, night, Egypt...) never caught; the
  mask covering the match alone; a line too long, empty, all symbols;
- the list itself: read whole (no line skipped), its words ROT13'd (no
  plain slur in it), the allowed words read;
- the rules: the text cleaned (control characters, the game's '|' codes,
  the markup characters, UTF-8 accents made plain, runs of spaces), links
  found (and words that are not links left), the host's and a sender's
  rates, the channels and who hears them, the wire's entries parsed and
  refused (count, size, flags, empty), the quick chat phrases, mutes by name
  in any case, and the filter's three strikes.

No slur is written in this file: the cases are made from the list's own
words at run time. Run port/vita/tests/run_chat_test.sh.
*/

#include "chat_filter.h"
#include "game_chat_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define check(condition, ...) \
	do \
	{ \
		checks++; \
		if (!(condition)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

/* the list's words, decoded (rule, word) */
struct word
{
	int rule;
	char text[64];
};

static struct word words[2048];
static int word_count;

static void rot13(char *text)
{
	for (; *text; text++)
	{
		if (*text >= 'a' && *text <= 'z')
			*text = (char)('a' + (*text - 'a' + 13) % 26);
	}
}

static char *read_file(const char *path, long *size)
{
	FILE *file = fopen(path, "rb");
	char *data;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	*size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)*size + 1);
	if (data && fread(data, 1, (size_t)*size, file) != (size_t)*size)
	{
		free(data);
		data = NULL;
	}
	if (data)
		data[*size] = 0;
	fclose(file);
	return data;
}

/* the list's words, as the file has them, checked: ROT13'd (each decodes to
letters; none of the allowed words, which are plain), and read */
static void test_list(const char *path)
{
	long size = 0;
	char *data = read_file(path, &size);
	char *line, *save = NULL;
	int added, lines = 0, allowed = 0;

	check(data != NULL, "the list %s read", path);
	if (!data)
		return;
	chat_filter_reset();
	added = chat_filter_load(data, size);
	for (line = strtok_r(data, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
	{
		char rule[16], text[64];

		if (line[0] == '#' || !line[0])
			continue;
		lines++;
		if (sscanf(line, "%15s %63s", rule, text) != 2)
		{
			check(0, "a line of the list read: %.20s", line);
			continue;
		}
		if (!strcmp(rule, "allow"))
		{
			allowed++;
			continue;
		}
		check(word_count < (int)(sizeof(words) / sizeof(words[0])), "the list's size");
		if (word_count >= (int)(sizeof(words) / sizeof(words[0])))
			break;
		words[word_count].rule = !strcmp(rule, "whole") ? CHAT_FILTER_WHOLE : !strcmp(rule, "start") ?
			CHAT_FILTER_START : CHAT_FILTER_ANYWHERE;
		check(!strcmp(rule, "whole") || !strcmp(rule, "start") || !strcmp(rule, "anywhere"), "a rule: %s", rule);
		snprintf(words[word_count].text, sizeof(words[word_count].text), "%s", text);
		rot13(words[word_count].text);
		word_count++;
	}
	check(added == lines, "every line of the list taken (%d of %d)", added, lines);
	check(word_count >= 100, "the list has its words (%d)", word_count);
	check(allowed >= 40, "the list has its allowed words (%d)", allowed);
	check(chat_filter_word_count() == word_count, "the filter has the list's words (%d of %d)", chat_filter_word_count(),
		word_count);
	printf("the list: %d words, %d allowed\n", word_count, allowed);
	free(data);
}

static int caught(const char *text)
{
	return chat_filter_count(text) > 0;
}

/* a word in leet: its first a, e, i, o or s (after its first letter) a digit
or symbol, so a third of it stays letters */
static int leet(char *out, int size, const char *word, int symbols)
{
	int index, changed = 0;

	snprintf(out, (size_t)size, "%s", word);
	for (index = 1; out[index]; index++)
	{
		char replacement = 0;

		switch (out[index])
		{
		case 'a': replacement = symbols ? '@' : '4'; break;
		case 'e': replacement = '3'; break;
		case 'i': replacement = symbols ? '!' : '1'; break;
		case 'o': replacement = '0'; break;
		case 's': replacement = symbols ? '$' : '5'; break;
		}
		if (replacement)
		{
			out[index] = replacement;
			changed = 1;
			break;
		}
	}
	return changed;
}

/* the word with sep between its letters ("w o r d", "w.o.r.d") */
static void separated(char *out, int size, const char *word, char separator)
{
	int length = 0, index;

	for (index = 0; word[index] && length < size - 3; index++)
	{
		if (index)
			out[length++] = separator;
		out[length++] = word[index];
	}
	out[length] = 0;
}

/* a letter repeated: the word's last letter three times more, unless the
word ends with a double (then its first after the first letter) */
static void repeated(char *out, int size, const char *word)
{
	int length = (int)strlen(word);
	int at = length - 1, index, written = 0;

	if (length >= 2 && word[length - 1] == word[length - 2])
		at = 0;
	for (index = 0; index < length && written < size - 5; index++)
	{
		out[written++] = word[index];
		if (index == at)
		{
			out[written++] = word[index];
			out[written++] = word[index];
			out[written++] = word[index];
		}
	}
	out[written] = 0;
}

/* whether a word holds another of the list's that is caught inside any
word ("x" plus a word of the list is that word): such a word is caught
inside another word too, rightly */
static int contained[2048];

static void find_contained(const char *list)
{
	long size = 0;
	char *data;
	int index;

	chat_filter_reset();
	for (index = 0; index < word_count; index++)
	{
		if (words[index].rule == CHAT_FILTER_ANYWHERE)
			chat_filter_add(words[index].text, CHAT_FILTER_ANYWHERE);
	}
	for (index = 0; index < word_count; index++)
	{
		char text[96];

		snprintf(text, sizeof(text), "qz%sqz", words[index].text);
		contained[index] = words[index].rule != CHAT_FILTER_ANYWHERE && chat_filter_count(text) > 0;
	}
	/* (the whole list again) */
	data = read_file(list, &size);
	chat_filter_reset();
	if (data)
		chat_filter_load(data, size);
	free(data);
}

static void test_words(void)
{
	int index, missed = 0;

	for (index = 0; index < word_count; index++)
	{
		const char *word = words[index].text;
		char text[256], form[96];
		int length = (int)strlen(word);
		int before = failures;

		check(caught(word), "word %d caught as it is", index);
		snprintf(text, sizeof(text), "you are such a %s lol", word);
		check(caught(text), "word %d caught in a sentence", index);
		snprintf(form, sizeof(form), "%s", word);
		{
			int at;

			for (at = 0; form[at]; at++)
				form[at] = (char)toupper((unsigned char)form[at]);
		}
		check(caught(form), "word %d caught in capitals", index);
		if (leet(form, sizeof(form), word, 0))
		{
			snprintf(text, sizeof(text), "gg %s", form);
			check(caught(text), "word %d caught in leet (digits)", index);
		}
		if (leet(form, sizeof(form), word, 1))
			check(caught(form), "word %d caught in leet (symbols)", index);
		separated(form, sizeof(form), word, ' ');
		snprintf(text, sizeof(text), "lol %s", form);
		check(caught(text), "word %d caught spaced", index);
		separated(form, sizeof(form), word, '.');
		check(caught(form), "word %d caught dotted", index);
		separated(form, sizeof(form), word, '_');
		check(caught(form), "word %d caught with underscores", index);
		repeated(form, sizeof(form), word);
		check(caught(form), "word %d caught with a letter repeated", index);
		/* (the mask covers the word, and not the words about it) */
		snprintf(text, sizeof(text), "hey %s there", word);
		chat_filter_mask(text);
		check(!strncmp(text, "hey ", 4) && !strcmp(text + 4 + length, " there"), "word %d's mask stays on it", index);
		{
			int at;

			for (at = 0; at < length; at++)
				check(text[4 + at] == '*', "word %d's letters masked", index);
		}
		switch (words[index].rule)
		{
		case CHAT_FILTER_WHOLE:
			/* (its plural; never inside another word) */
			snprintf(text, sizeof(text), "%s%s", word, word[length - 1] == 's' ? "es" : "s");
			check(caught(text), "whole word %d caught with s", index);
			snprintf(text, sizeof(text), "qz%sqz", word);
			check(contained[index] || !caught(text), "whole word %d not caught inside a word", index);
			break;
		case CHAT_FILTER_START:
			snprintf(text, sizeof(text), "%sqz", word);
			check(caught(text), "start word %d caught at a word's start", index);
			snprintf(text, sizeof(text), "qz%s", word);
			check(contained[index] || !caught(text), "start word %d not caught after a word's start", index);
			break;
		default:
			snprintf(text, sizeof(text), "qz%sqz", word);
			check(caught(text), "anywhere word %d caught inside a word", index);
			break;
		}
		if (failures != before)
			missed++;
	}
	printf("the filter: %d words, %d with a form missed\n", word_count, missed);
}

/* words that hold a word of the list, and must never be masked
(Scunthorpe), and what players say in Halo */
static void test_false_positives(void)
{
	static const char *const lines[] =
	{
		"Scunthorpe", "assassin", "assassination", "classic", "class", "bass", "grass", "passage", "massive",
		"night", "nightly", "knight", "snigger", "sniggering", "niggle", "niggard", "Niger", "Nigeria",
		"spicy", "spice", "conspicuous", "despicable", "auspicious", "suspicion", "hospice",
		"raccoon", "cocoon", "tycoon", "lagoon", "coonhound",
		"Japan", "Japanese", "jape", "Pakistan", "Pakistani", "Mongoose", "mongoose", "Mongolia", "among",
		"retardant", "fire retardant", "tardy", "tardiness", "Egypt", "Egyptian", "gypsum",
		"about", "aboard", "abode", "abort", "abolish", "Hebrew", "hebetude", "wops", "swop",
		"chinks in the armor is a phrase too", "dyke in the Netherlands", "dike", "faggot of sticks",
		"coolant", "cool", "kikes", "kkk",
		"Banshee", "Warthog", "Ghost", "Scorpion", "Wraith", "Pelican", "Covenant", "Grunt", "Jackal",
		"Elite", "Hunter", "Flood", "Sentinel", "Spartan", "Master Chief", "Cortana", "Guilty Spark",
		"Blood Gulch", "Hang 'em High", "Sidewinder", "Damnation", "Boarding Action", "Gephyrophobia",
		"Infinity", "Danger Canyon", "Death Island", "Ice Fields", "Timberland", "Derelict", "Longest",
		"Chill Out", "Prisoner", "Wizard", "Chiron TL-34", "Rat Race", "Carousel", "Battle Creek",
		"need backup", "enemy spotted", "flag taken", "nice shot", "gg", "gg ez", "no scope", "snipe",
		"sniper", "rocket launcher", "plasma pistol", "needler", "frag grenade", "fuel rod",
		"I got the flag", "they have our flag", "spawn camping", "noob", "pwned", "headshot",
		"the night is dark", "pass the ball", "class act", "nice night", "the mongoose is fast",
		"buen tiro", "necesito refuerzos", "bandera tomada", "sigueme", "gracias", "lo siento",
		"I'm Niko", "Nikko", "Nigel", "snicker", "Snickers", "nigh", "spick and span",
		"homosapien", "therapist", "cocktail", "shiitake", "Matsushita", "assess", "assessment",
		"Dagobah", "Dagon", "dagoba", "coonskin", "hebe", "pikachu", "pakistan", "Japanophile",
		"chinchilla", "Chinook", "chinos", "Chinese", "China", "japanned",
		"1337 pwnage 9001", "8008135", "12345", "@#$%^&*", "...", "?!?!",
	};
	int index, flagged = 0;

	for (index = 0; index < (int)(sizeof(lines) / sizeof(lines[0])); index++)
	{
		int count = chat_filter_count(lines[index]);

		/* (a few of these are words of the list, said as the list has them:
		"hebe", "kikes", "kkk", "chinks", "dyke", "faggot" - caught, their
		false positives the list's choice, as OpenCE's review made it) */
		if (!strcmp(lines[index], "hebe") || !strcmp(lines[index], "kikes") || !strcmp(lines[index], "kkk") ||
			!strncmp(lines[index], "chinks", 6) || !strncmp(lines[index], "dyke", 4) ||
			!strncmp(lines[index], "faggot", 6) || !strcmp(lines[index], "wops"))
		{
			check(count > 0, "a word of the list caught: case %d", index);
			continue;
		}
		if (count)
		{
			flagged++;
			check(0, "not a slur, never masked: \"%s\"", lines[index]);
		}
	}
	printf("the false positive list: %d lines, %d masked\n", index, flagged);
}

static void test_filter_edges(void)
{
	char text[CHAT_FILTER_MAXIMUM_LENGTH + 64];
	int index;

	check(chat_filter_count("") == 0, "an empty line");
	check(chat_filter_mask(NULL) == 0 && chat_filter_count(NULL) == 0, "no line");
	memset(text, 'a', sizeof(text) - 1);
	text[sizeof(text) - 1] = 0;
	check(chat_filter_count(text) == 0, "a line longer than the filter reads");
	for (index = 0; index < (int)sizeof(text) - 1; index++)
		text[index] = (char)(32 + index % 95);
	text[sizeof(text) - 1] = 0;
	chat_filter_mask(text);
	check(strlen(text) == sizeof(text) - 1, "a masked line keeps its length");
	/* (each word inside the longest text the filter reads: found at its end) */
	if (word_count)
	{
		int length = (int)strlen(words[0].text);

		memset(text, ' ', CHAT_FILTER_MAXIMUM_LENGTH);
		memcpy(text + CHAT_FILTER_MAXIMUM_LENGTH - length, words[0].text, (size_t)length);
		text[CHAT_FILTER_MAXIMUM_LENGTH] = 0;
		check(chat_filter_count(text) > 0, "a word at the end of the longest line");
	}
	/* (a line made to try every way of reading symbols as letters: bounded,
	masked whole; an ordinary line far from the bound) */
	{
		static const char *const hard[] =
		{
			"!@]&)!\\[(]^$*[!|]#(/]$|!^[\\&&^<<!(**!*#!&#/$)*!#])*]#]#(#/$#&#|",
			"|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|!|",
			"(|)(|)(|)(|)(|)(|)(|)(|)(|)(|)(|)(|)(|)(|)(|)(|)(|)(|)(|)(|)(|)",
		};
		unsigned int seed = 7;
		long most = 0;

		for (index = 0; index < (int)(sizeof(hard) / sizeof(hard[0])); index++)
		{
			chat_filter_count(hard[index]);
			check(chat_filter_last_steps() <= CHAT_FILTER_MAXIMUM_STEPS + 1, "a hard line bounded (%ld steps)",
				chat_filter_last_steps());
		}
		/* (symbol soup at random: each bounded; masked whole only when the
		bound was reached) */
		for (index = 0; index < 2000; index++)
		{
			static const char symbols[] = "!@#$%^&*()|<>[]/\\";
			char line[64];
			int at;

			for (at = 0; at < 63; at++)
			{
				seed = seed * 1103515245u + 12345u;
				line[at] = symbols[(seed >> 8) % (sizeof(symbols) - 1)];
			}
			line[63] = 0;
			chat_filter_count(line);
			if (chat_filter_last_steps() > most)
				most = chat_filter_last_steps();
		}
		check(most <= CHAT_FILTER_MAXIMUM_STEPS + 1, "symbol soup bounded (%ld)", most);
		check(chat_filter_count("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!") == 0 &&
			chat_filter_count("???????????????????????????????????????????????????????????????") == 0 &&
			chat_filter_count("lol 1111111111111111111111111111111111111111111111111111111111") == 0,
			"runs of one symbol not masked");
		chat_filter_count("the quick brown fox jumps over the lazy dog, nice shot! gg wp");
		check(chat_filter_last_steps() < CHAT_FILTER_MAXIMUM_STEPS / 10, "an ordinary line's steps (%ld)",
			chat_filter_last_steps());
	}
	/* (the file's lines it cannot read: skipped) */
	chat_filter_reset();
	check(chat_filter_load("whole\nwhole x\nwhole ab cd\nbogus abc\nanywhere a1b\n# c\nallow 12\n", 63) == 0,
		"a file's lines it cannot read skipped");
	check(chat_filter_load("whole nop\r\nallow nope\r\n", 24) == 2, "a file with CRLF lines");
	check(chat_filter_word_count() == 1, "one word read (%d)", chat_filter_word_count());
	/* ("nop" ROT13'd is "abc") */
	check(chat_filter_count("abc") == 1 && chat_filter_count("ABC!") == 1, "a word read from ROT13");
	check(chat_filter_count("abcd") == 0, "a whole word not inside another");
	check(chat_filter_count("abcs") == 1, "a whole word's plural");
}

/* ---------- the rules */

static void test_clean(void)
{
	char out[GAME_CHAT_TEXT_SIZE];
	char big[300];
	int length;

	length = game_chat_text_clean(out, sizeof(out), "  hello   world  ", 100);
	check(length == 11 && !strcmp(out, "hello world"), "spaces trimmed and made one: \"%s\"", out);
	game_chat_text_clean(out, sizeof(out), "a|nb|", 100);
	check(!strcmp(out, "anb"), "the game's text codes out: \"%s\"", out);
	game_chat_text_clean(out, sizeof(out), "<b>%s^1\\`x", 100);
	check(!strcmp(out, "bs1x"), "markup out: \"%s\"", out);
	game_chat_text_clean(out, sizeof(out), "a\x01" "b\x7f" "c\td", 100);
	check(!strcmp(out, "a?b?c d"), "control characters: \"%s\"", out);
	game_chat_text_clean(out, sizeof(out), "\xc2\xbf" "D\xc3\xb3nde est\xc3\xa1s, compa\xc3\xb1" "ero?", 100);
	check(!strcmp(out, "Donde estas, companero?"), "Spanish made plain: \"%s\"", out);
	game_chat_text_clean(out, sizeof(out), "\xe2\x82\xac" "5 \xf0\x9f\x98\x80!", 100);
	check(!strcmp(out, "?5 ?!"), "other UTF-8 one '?' each: \"%s\"", out);
	game_chat_text_clean(out, sizeof(out), "\xc3", 1);
	check(!strcmp(out, "?"), "a sequence cut short: \"%s\"", out);
	game_chat_text_clean(out, sizeof(out), "\x80\x80\x80", 3);
	check(!strcmp(out, "???"), "stray continuation bytes: \"%s\"", out);
	memset(big, 'x', sizeof(big));
	length = game_chat_text_clean(out, sizeof(out), big, sizeof(big));
	check(length == GAME_CHAT_TEXT_LENGTH && out[GAME_CHAT_TEXT_LENGTH] == 0, "cut to the wire's length (%d)", length);
	length = game_chat_text_clean(out, sizeof(out), "abc", 2);
	check(length == 2 && !strcmp(out, "ab"), "no further than its size");
	check(game_chat_text_clean(out, sizeof(out), "   ", 3) == 0 && !out[0], "spaces alone: empty");
	check(game_chat_text_clean(out, 1, "abc", 3) == 0 && !out[0], "no room");
	{
		int index;

		/* (every byte: printable ASCII out, no markup) */
		for (index = 1; index < 256; index++)
		{
			char in[2] = { (char)index, 0 };
			const char *at;

			game_chat_text_clean(out, sizeof(out), in, 1);
			for (at = out; *at; at++)
				check(*at >= ' ' && *at <= '~' && !strchr("|%^<>\\`", *at), "byte %d cleaned", index);
		}
	}
}

static void test_links(void)
{
	static const char *const links[] =
	{
		"http://example.org", "go to https://x.y/z", "www.halo.com", "discord.gg/abc", "example.com",
		"join example (dot) com", "example[dot]net", "site . com", "1.2.3.4", "my ip 192.168.1.10 ok",
		"youtu.be/x", "EXAMPLE.COM", "ftp://a", "free vbucks at scam.xyz", "t.co/abc",
	};
	static const char *const not_links[] =
	{
		"gg", "nice shot", "version 1.1.1", "1.2.3.4.5", "go.to bed", "he.is here", "polka dot me", "3.14",
		"wait...", "e.g. this", "ok.", "Mr. Chief", "no.", "lol.lol", "I'm in.", "",
	};
	int index;

	for (index = 0; index < (int)(sizeof(links) / sizeof(links[0])); index++)
		check(game_chat_text_has_link(links[index]), "a link: %s", links[index]);
	for (index = 0; index < (int)(sizeof(not_links) / sizeof(not_links[0])); index++)
		check(!game_chat_text_has_link(not_links[index]), "not a link: %s", not_links[index]);
	check(!game_chat_text_has_link(NULL), "no text");
}

static void test_rates(void)
{
	struct game_chat_rate rate;
	int index, taken = 0;

	memset(&rate, 0, sizeof(rate));
	for (index = 0; index < 10; index++)
		taken += game_chat_rate_take(&rate, 1000, GAME_CHAT_HOST_BURST, GAME_CHAT_HOST_REFILL_MILLISECONDS);
	check(taken == GAME_CHAT_HOST_BURST, "a burst at once (%d)", taken);
	check(!game_chat_rate_take(&rate, 1000 + GAME_CHAT_HOST_REFILL_MILLISECONDS - 1, GAME_CHAT_HOST_BURST,
		GAME_CHAT_HOST_REFILL_MILLISECONDS), "none before the refill");
	check(game_chat_rate_take(&rate, 1000 + GAME_CHAT_HOST_REFILL_MILLISECONDS, GAME_CHAT_HOST_BURST,
		GAME_CHAT_HOST_REFILL_MILLISECONDS), "one at the refill");
	check(!game_chat_rate_take(&rate, 1000 + GAME_CHAT_HOST_REFILL_MILLISECONDS, GAME_CHAT_HOST_BURST,
		GAME_CHAT_HOST_REFILL_MILLISECONDS), "and no more");
	/* (a long wait: the burst again, no more; the clock wrapping) */
	check(game_chat_rate_take(&rate, 0xFFFFFF00UL, GAME_CHAT_HOST_BURST, GAME_CHAT_HOST_REFILL_MILLISECONDS),
		"after a long wait");
	memset(&rate, 0, sizeof(rate));
	taken = 0;
	for (index = 0; index < 100; index++)
		taken += game_chat_rate_take(&rate, 0xFFFFF000UL + (unsigned long)index * 100UL, GAME_CHAT_HOST_BURST,
			GAME_CHAT_HOST_REFILL_MILLISECONDS);
	check(taken >= GAME_CHAT_HOST_BURST && taken <= GAME_CHAT_HOST_BURST + 7, "across the clock's wrap (%d)", taken);
	/* (this machine's own: never more than a host takes) */
	check(GAME_CHAT_SEND_BURST < GAME_CHAT_HOST_BURST && GAME_CHAT_SEND_REFILL_MILLISECONDS >= GAME_CHAT_HOST_REFILL_MILLISECONDS,
		"a sender is slower than any host");
	check(!game_chat_rate_take(NULL, 0, 1, 1) && !game_chat_rate_take(&rate, 0, 0, 1), "no bucket");
}

static void test_channels(void)
{
	check(game_chat_channel_from_flags(0) == GAME_CHAT_CHANNEL_ALL, "flags: all");
	check(game_chat_channel_from_flags(GAME_CHAT_LINE_TEAM_BIT) == GAME_CHAT_CHANNEL_TEAM, "flags: team");
	check(game_chat_channel_from_flags(GAME_CHAT_LINE_VEHICLE_BIT) == GAME_CHAT_CHANNEL_VEHICLE, "flags: vehicle");
	check(game_chat_channel_from_flags(GAME_CHAT_LINE_NOTICE_BIT) == GAME_CHAT_CHANNEL_NOTICE, "flags: notice");
	check(game_chat_channel_flags(GAME_CHAT_CHANNEL_TEAM) == GAME_CHAT_LINE_TEAM_BIT, "team: flags");
	check(game_chat_channel_flags(GAME_CHAT_CHANNEL_PARTY) == 0, "the party's: never on the wire");
	check(game_chat_hears(GAME_CHAT_CHANNEL_ALL, 1, 0, 1, -1, -1), "all: everyone");
	check(game_chat_hears(GAME_CHAT_CHANNEL_TEAM, 1, 0, 0, -1, -1), "team: the team");
	check(!game_chat_hears(GAME_CHAT_CHANNEL_TEAM, 1, 0, 1, -1, -1), "team: not the other team");
	check(game_chat_hears(GAME_CHAT_CHANNEL_TEAM, 0, 0, 1, -1, -1), "team: everyone without teams");
	check(game_chat_hears(GAME_CHAT_CHANNEL_VEHICLE, 1, 0, 1, 7, 7), "vehicle: those in it");
	check(!game_chat_hears(GAME_CHAT_CHANNEL_VEHICLE, 1, 0, 0, -1, -1), "vehicle: no vehicle, no one");
	check(!game_chat_hears(GAME_CHAT_CHANNEL_PARTY, 1, 0, 0, -1, -1), "the party's: not the game's");
	check(!game_chat_hears(GAME_CHAT_CHANNEL_NOTICE, 1, 0, 0, -1, -1), "a notice: not a player's");
}

static void test_wire(void)
{
	unsigned char entry[GAME_CHAT_LINE_ENTRY_SIZE + 8];
	struct game_chat_line line;
	unsigned char flags;

	/* (OpenCE's sizes) */
	check(GAME_CHAT_LINE_ENTRY_SIZE == 68 && GAME_CHAT_CONFIG_ENTRY_SIZE == 4 && GAME_CHAT_TEXT_SIZE == 64,
		"the wire's sizes");
	check(GAME_CHAT_KIND_UP == 87 && GAME_CHAT_KIND_DOWN == 88 && GAME_CHAT_KIND_CONFIG == 89, "the kinds");
	game_chat_line_entry(entry, 3, GAME_CHAT_LINE_TEAM_BIT, "  hi |there  ");
	check(entry[0] == 3 && entry[1] == GAME_CHAT_LINE_TEAM_BIT && !entry[2] && !entry[3] &&
		!strcmp((char *)entry + 4, "hi there"), "an entry made");
	check(game_chat_parse_line(entry, GAME_CHAT_LINE_ENTRY_SIZE, 1, &line) == GAME_CHAT_PARSE_OK &&
		line.player_index == 3 && line.flags == GAME_CHAT_LINE_TEAM_BIT && !strcmp(line.text, "hi there"),
		"an entry read");
	check(game_chat_parse_line(entry, GAME_CHAT_LINE_ENTRY_SIZE, 0, &line) == GAME_CHAT_PARSE_COUNT, "no entry");
	check(game_chat_parse_line(entry, 2 * GAME_CHAT_LINE_ENTRY_SIZE, 2, &line) == GAME_CHAT_PARSE_COUNT,
		"two entries");
	check(game_chat_parse_line(entry, GAME_CHAT_LINE_ENTRY_SIZE - 1, 1, &line) == GAME_CHAT_PARSE_SIZE, "cut short");
	check(game_chat_parse_line(NULL, 0, 1, &line) == GAME_CHAT_PARSE_SIZE, "nothing");
	entry[1] = 0x80;
	check(game_chat_parse_line(entry, GAME_CHAT_LINE_ENTRY_SIZE, 1, &line) == GAME_CHAT_PARSE_FLAGS, "unknown flags");
	entry[1] = GAME_CHAT_LINE_TEAM_BIT | GAME_CHAT_LINE_VEHICLE_BIT;
	check(game_chat_parse_line(entry, GAME_CHAT_LINE_ENTRY_SIZE, 1, &line) == GAME_CHAT_PARSE_FLAGS,
		"team and vehicle at once");
	game_chat_line_entry(entry, 1, 0, "   ");
	check(game_chat_parse_line(entry, GAME_CHAT_LINE_ENTRY_SIZE, 1, &line) == GAME_CHAT_PARSE_EMPTY, "empty");
	/* (the text not ended: read to its 64 bytes, no further) */
	memset(entry, 'A', sizeof(entry));
	entry[1] = 0;
	check(game_chat_parse_line(entry, GAME_CHAT_LINE_ENTRY_SIZE, 1, &line) == GAME_CHAT_PARSE_OK &&
		strlen(line.text) == GAME_CHAT_TEXT_LENGTH, "a text not ended (%d)", (int)strlen(line.text));
	memset(entry, 0, sizeof(entry));
	entry[0] = GAME_CHAT_CONFIG_ALLOWED_BIT | 0xF0;
	check(game_chat_parse_config(entry, GAME_CHAT_CONFIG_ENTRY_SIZE, 1, &flags) == GAME_CHAT_PARSE_OK &&
		flags == GAME_CHAT_CONFIG_ALLOWED_BIT, "a config's flags, unknown ones dropped");
	check(game_chat_parse_config(entry, GAME_CHAT_CONFIG_ENTRY_SIZE - 1, 1, &flags) == GAME_CHAT_PARSE_SIZE,
		"a config cut short");
	check(game_chat_parse_config(entry, GAME_CHAT_CONFIG_ENTRY_SIZE, 2, &flags) == GAME_CHAT_PARSE_COUNT,
		"two configs");
}

static void test_presets(void)
{
	int index;

	for (index = 0; index < GAME_CHAT_PRESET_COUNT; index++)
	{
		const char *english = game_chat_preset_english(index);
		char cleaned[GAME_CHAT_TEXT_SIZE];

		check(english && game_chat_preset_find(english) == index, "phrase %d found by its text", index);
		/* (as the wire carries it: unchanged by the cleaning, no link, no
		filter word) */
		game_chat_text_clean(cleaned, sizeof(cleaned), english, (int)strlen(english));
		check(english && !strcmp(cleaned, english), "phrase %d unchanged on the wire", index);
		check(english && !game_chat_text_has_link(english) && !chat_filter_count(english), "phrase %d clean", index);
	}
	check(game_chat_preset_english(-1) == NULL && game_chat_preset_english(GAME_CHAT_PRESET_COUNT) == NULL,
		"no phrase past the list");
	check(game_chat_preset_find("need backup!") < 0 && game_chat_preset_find(NULL) < 0, "a phrase's text exactly");
}

static void test_mutes(void)
{
	struct game_chat_mutes mutes;
	struct game_chat_trips trips;
	int index;
	char name[16];

	memset(&mutes, 0, sizeof(mutes));
	check(game_chat_mute_set(&mutes, "Bravo", 1, 0) == 1 && game_chat_mute_find(&mutes, "BRAVO") >= 0,
		"a mute by name, in any case");
	check(game_chat_mute_set(&mutes, "bravo", 1, 0) == 1, "muted once");
	check(game_chat_mute_find(&mutes, "Charlie") < 0, "another not muted");
	check(game_chat_mute_set(&mutes, "BRAVO", 0, 0) == 0 && game_chat_mute_find(&mutes, "Bravo") < 0, "unmuted");
	check(game_chat_mute_set(&mutes, "", 1, 0) == 0 && game_chat_mute_find(&mutes, "") < 0, "no name, no mute");
	/* (full: an automatic mute gives way to a new one; the player's own stay) */
	for (index = 0; index < GAME_CHAT_MUTES; index++)
	{
		snprintf(name, sizeof(name), "p%d", index);
		game_chat_mute_set(&mutes, name, 1, index == 5);
	}
	check(mutes.count == GAME_CHAT_MUTES, "the mutes full");
	game_chat_mute_set(&mutes, "newcomer", 1, 0);
	check(game_chat_mute_find(&mutes, "newcomer") >= 0 && game_chat_mute_find(&mutes, "p5") < 0,
		"the automatic mute gave way");
	game_chat_mute_set(&mutes, "another", 1, 0);
	check(game_chat_mute_find(&mutes, "another") < 0 && mutes.count == GAME_CHAT_MUTES, "the player's own mutes kept");
	memset(&trips, 0, sizeof(trips));
	check(!game_chat_trip(&trips, "Echo", 1000) && !game_chat_trip(&trips, "echo", 2000) &&
		game_chat_trip(&trips, "ECHO", 3000), "the third strike mutes");
	check(!game_chat_trip(&trips, "Echo", 4000), "only the third");
	memset(&trips, 0, sizeof(trips));
	game_chat_trip(&trips, "Fox", 1000);
	game_chat_trip(&trips, "Fox", 2000);
	check(!game_chat_trip(&trips, "Fox", 2000 + GAME_CHAT_FILTER_TRIP_MILLISECONDS), "strikes forgotten after a while");
	for (index = 0; index < 3 * GAME_CHAT_MUTES; index++)
	{
		snprintf(name, sizeof(name), "q%d", index);
		game_chat_trip(&trips, name, (unsigned long)index);
	}
	check(trips.count == GAME_CHAT_MUTES, "the strikes' list bounded");
}

/* (--dictionary WORDS EXPECTED) every word of an English word list
(dwyl's english-words, the Unlicense: OpenCE's port/third_party has it) the
filter masks, ROT13'd, matched against the list expected: none masked that
is not a slur's form, none of those missed */
static int test_dictionary(const char *words_path, const char *expected_path)
{
	long size = 0, expected_size = 0, at;
	char *data = read_file(words_path, &size);
	char *expected = read_file(expected_path, &expected_size);
	int masked = 0, unexpected = 0, missing = 0, expected_count = 0;
	char *line, *save = NULL;

	check(data && expected, "the dictionary and its expected list read");
	if (!data || !expected)
		return 1;
	for (line = strtok_r(data, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save))
	{
		char key[96];
		char needle[100];

		if (!chat_filter_count(line))
			continue;
		masked++;
		snprintf(key, sizeof(key), "%s", line);
		rot13(key);
		snprintf(needle, sizeof(needle), "\n%s\n", key);
		if (!strstr(expected, needle))
		{
			unexpected++;
			printf("masked, not expected (ROT13): %s\n", key);
		}
	}
	for (at = 0; at < expected_size; at++)
	{
		/* (a line of a word: not a comment, not empty) */
		if ((at == 0 || expected[at - 1] == '\n') && expected[at] >= 'a' && expected[at] <= 'z')
			expected_count++;
	}
	missing = expected_count - (masked - unexpected);
	check(!unexpected, "%d dictionary words masked that are not expected", unexpected);
	check(!missing, "%d expected dictionary words not masked", missing);
	printf("the dictionary: %d words masked (%d expected)\n", masked, expected_count);
	free(data);
	free(expected);
	return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	const char *list = argc > 1 ? argv[1] : "filter.txt";

	if (argc > 4 && !strcmp(argv[2], "--dictionary"))
	{
		long size = 0;
		char *data = read_file(list, &size);

		chat_filter_reset();
		chat_filter_load(data, size);
		free(data);
		return test_dictionary(argv[3], argv[4]);
	}

	test_list(list);
	find_contained(list);
	test_words();
	test_false_positives();
	test_presets();
	test_filter_edges();
	test_clean();
	test_links();
	test_rates();
	test_channels();
	test_wire();
	test_mutes();
	printf("%d checks, %d failed\n", checks, failures);
	printf(failures ? "FAIL\n" : "PASS\n");
	return failures ? 1 : 0;
}
