/*
CHAT_FILTER.C

The in-game chat's slur filter (chat_filter.h). The matcher is OpenCE's
streamer mode matcher (port/linux/game/text_filter_match.c, by the OpenCE
contributors, CC0 1.0: ported from its last commit before OpenCE's licence
changed, 4b09168e^), cut down to the chat's text, which is printable ASCII
(game_chat_protocol.c cleans every line before it is shown), and given its
words from a data file rather than built in. Each character becomes an
element:

- a letter, of a-z, its case folded;
- a digit or symbol read as letters (leet: 0 o, 1 i or l, 3 e, 4 a, 5 s,
  7 t, 8 b, @ a, $ s, ! i, + t, ( c, | i or l...), or as any letter (* # % ?,
  the last of which is what the chat's cleaning leaves for a character it
  does not take), which may also be punctuation;
- a space or punctuation, which a word may be spelled across;
- anything else, which no word crosses.

Two elements together may read as one letter (ph f, vv w, \/ v, |< k, ()
o). Then each word is looked for from each element: a run of one letter
reads as one ("niiiice" is "nice"); punctuation may come between letters
("n.i.c.e"); spaces too where the pieces are a letter or two ("n i c e",
"ni ce") or the match is whole words. A word's rule says where it may be
(inside a word, at a word's start, or as the whole word, or it and "s" or
"es"), and the allowed words (Scunthorpe) are never masked. Any letter (* ?
#) counts once in a short word and twice in a longer one, never in a word of
three letters, and a third of a match's letters must be letters, not digits
or symbols, so that numbers are never read as words.
*/

#include "chat_filter.h"

#include <string.h>

#define MAXIMUM_TERMS 1024
#define MAXIMUM_TERM_CHARACTERS 16384
#define MAXIMUM_TERM_LENGTH 40
#define MAXIMUM_ALLOWED 512
#define MAXIMUM_ALLOWED_CHARACTERS 8192
#define MAXIMUM_GAP 4

#define ALL_LETTERS 0x03FFFFFFUL
#define LETTER(c) (1UL << ((c) - 'a'))

enum
{
	_element_letter,
	_element_digit,
	_element_symbol,
	_element_wildcard,
	_element_space,
	_element_punctuation,
	_element_break
};

struct element
{
	unsigned long letters;
	unsigned long pair;
	short index;
	unsigned char code;
	unsigned char kind;
};

struct term
{
	short offset;
	unsigned char length;
	unsigned char rule;
};

static struct
{
	long term_count;
	long character_count;
	struct term terms[MAXIMUM_TERMS];
	char characters[MAXIMUM_TERM_CHARACTERS];
	/* the terms by their first letter: first[letter] .. first[letter + 1] of
	order */
	short order[MAXIMUM_TERMS];
	short first[27];
	int sorted;
	long allowed_count;
	long allowed_character_count;
	short allowed[MAXIMUM_ALLOWED];
	char allowed_characters[MAXIMUM_ALLOWED_CHARACTERS];
} chat_filter_words;

/* ---------- the words */

/* a word's letters, a-z only (others dropped); their count */
static long chat_filter_letters(char *letters, long size, const char *text, long text_length)
{
	long length = 0;
	long index;

	for (index = 0; index < text_length && text[index]; index++)
	{
		char character = text[index];

		if (character >= 'A' && character <= 'Z')
			character = (char)(character - 'A' + 'a');
		if (character >= 'a' && character <= 'z' && length < size - 1)
			letters[length++] = character;
	}
	letters[length] = 0;
	return length;
}

static int chat_filter_store(const char *letters, long length, int rule)
{
	struct term *term;

	if (length < 2 || length > MAXIMUM_TERM_LENGTH || chat_filter_words.term_count >= MAXIMUM_TERMS ||
		chat_filter_words.character_count + length + 1 > MAXIMUM_TERM_CHARACTERS || rule < 0 ||
		rule >= CHAT_FILTER_RULE_COUNT)
	{
		return 0;
	}
	term = &chat_filter_words.terms[chat_filter_words.term_count++];
	term->offset = (short)chat_filter_words.character_count;
	term->length = (unsigned char)length;
	term->rule = (unsigned char)rule;
	memcpy(chat_filter_words.characters + chat_filter_words.character_count, letters, (size_t)length + 1);
	chat_filter_words.character_count += length + 1;
	chat_filter_words.sorted = 0;
	return 1;
}

void chat_filter_reset(void)
{
	memset(&chat_filter_words, 0, sizeof(chat_filter_words));
}

int chat_filter_add(const char *word, int rule)
{
	char letters[MAXIMUM_TERM_LENGTH + 2];

	return word && chat_filter_store(letters, chat_filter_letters(letters, sizeof(letters), word, (long)strlen(word)),
		rule);
}

static int chat_filter_allow_letters(const char *letters, long length)
{
	if (length < 2 || length > MAXIMUM_TERM_LENGTH || chat_filter_words.allowed_count >= MAXIMUM_ALLOWED ||
		chat_filter_words.allowed_character_count + length + 1 > MAXIMUM_ALLOWED_CHARACTERS)
	{
		return 0;
	}
	chat_filter_words.allowed[chat_filter_words.allowed_count++] = (short)chat_filter_words.allowed_character_count;
	memcpy(chat_filter_words.allowed_characters + chat_filter_words.allowed_character_count, letters,
		(size_t)length + 1);
	chat_filter_words.allowed_character_count += length + 1;
	return 1;
}

int chat_filter_allow(const char *word)
{
	char letters[MAXIMUM_TERM_LENGTH + 2];

	return word && chat_filter_allow_letters(letters,
		chat_filter_letters(letters, sizeof(letters), word, (long)strlen(word)));
}

int chat_filter_word_count(void)
{
	return (int)chat_filter_words.term_count;
}

/* ROT13 of a-z (the file's words) */
static char chat_filter_rot13(char character)
{
	if (character >= 'a' && character <= 'z')
		return (char)('a' + (character - 'a' + 13) % 26);
	if (character >= 'A' && character <= 'Z')
		return (char)('a' + (character - 'A' + 13) % 26);
	return character;
}

int chat_filter_load(const char *text, long size)
{
	long at = 0;
	int added = 0;

	if (!text)
		return 0;
	while (at < size)
	{
		const char *line = text + at;
		long length = 0, word_at, word_length;
		char letters[MAXIMUM_TERM_LENGTH + 2];
		int rule = -1, allow = 0;

		while (at + length < size && line[length] != '\n' && line[length])
			length++;
		at += length + 1;
		if (length > 0 && line[length - 1] == '\r')
			length--;
		if (!length || line[0] == '#')
			continue;
		if (length > 6 && !memcmp(line, "whole ", 6))
			rule = CHAT_FILTER_WHOLE, word_at = 6;
		else if (length > 6 && !memcmp(line, "start ", 6))
			rule = CHAT_FILTER_START, word_at = 6;
		else if (length > 9 && !memcmp(line, "anywhere ", 9))
			rule = CHAT_FILTER_ANYWHERE, word_at = 9;
		else if (length > 6 && !memcmp(line, "allow ", 6))
			allow = 1, word_at = 6;
		else
			continue;
		word_length = length - word_at;
		/* (one word of letters: anything else is not this file's) */
		{
			long index;
			int letters_only = word_length >= 2 && word_length <= MAXIMUM_TERM_LENGTH;

			for (index = 0; letters_only && index < word_length; index++)
			{
				char character = line[word_at + index];

				letters_only = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
			}
			if (!letters_only)
				continue;
		}
		chat_filter_letters(letters, sizeof(letters), line + word_at, word_length);
		if (allow)
		{
			added += chat_filter_allow_letters(letters, word_length);
			continue;
		}
		{
			long index;

			for (index = 0; index < word_length; index++)
				letters[index] = chat_filter_rot13(letters[index]);
		}
		added += chat_filter_store(letters, word_length, rule);
	}
	return added;
}

/* the terms by their first letter */
static void chat_filter_sort(void)
{
	long letter;
	long count = 0;

	for (letter = 0; letter < 26; letter++)
	{
		long index;

		chat_filter_words.first[letter] = (short)count;
		for (index = 0; index < chat_filter_words.term_count; index++)
		{
			if (chat_filter_words.characters[chat_filter_words.terms[index].offset] == 'a' + letter)
				chat_filter_words.order[count++] = (short)index;
		}
	}
	chat_filter_words.first[26] = (short)count;
	chat_filter_words.sorted = 1;
}

/* ---------- elements */

/* what a character is (an _element_ kind; -1 for one not there), and the
letters it may read as */
static int chat_filter_classify(unsigned char code, unsigned long *letters)
{
	*letters = 0;
	if (code >= 'A' && code <= 'Z')
		code = (unsigned char)(code - 'A' + 'a');
	if (code >= 'a' && code <= 'z')
	{
		*letters = LETTER(code);
		return _element_letter;
	}
	switch (code)
	{
	case '0': *letters = LETTER('o'); return _element_digit;
	case '1': *letters = LETTER('i') | LETTER('l'); return _element_digit;
	case '2': *letters = LETTER('z'); return _element_digit;
	case '3': *letters = LETTER('e'); return _element_digit;
	case '4': *letters = LETTER('a'); return _element_digit;
	case '5': *letters = LETTER('s'); return _element_digit;
	case '6': *letters = LETTER('g') | LETTER('b'); return _element_digit;
	case '7': *letters = LETTER('t'); return _element_digit;
	case '8': *letters = LETTER('b'); return _element_digit;
	case '9': *letters = LETTER('g') | LETTER('q'); return _element_digit;
	case '@': *letters = LETTER('a'); return _element_symbol;
	case '$': *letters = LETTER('s'); return _element_symbol;
	case '!': *letters = LETTER('i'); return _element_symbol;
	case '+': *letters = LETTER('t'); return _element_symbol;
	case '(': *letters = LETTER('c'); return _element_symbol;
	case '<': *letters = LETTER('c'); return _element_symbol;
	case '|': *letters = LETTER('i') | LETTER('l'); return _element_symbol;
	case '*': case '#': case '%': case '?':
		*letters = ALL_LETTERS;
		return _element_wildcard;
	case ' ': case '\t': case '\r': case '\n':
		return _element_space;
	}
	if (code < ' ' || code == 0x7F)
		return -1;
	if (code > ' ' && code < 0x7F)
		return _element_punctuation;
	return _element_break;
}

/* the text's elements; their count */
static long chat_filter_elements(const char *text, long length, struct element *elements, long maximum)
{
	long count = 0;
	long index;

	for (index = 0; index < length && count < maximum; index++)
	{
		unsigned char code = (unsigned char)text[index];
		struct element *element = &elements[count];
		int kind = chat_filter_classify(code, &element->letters);

		if (kind < 0)
			continue;
		element->kind = (unsigned char)kind;
		element->code = code >= 'A' && code <= 'Z' ? (unsigned char)(code - 'A' + 'a') : code;
		element->index = (short)index;
		element->pair = 0;
		count++;
	}
	/* pairs read as one letter */
	for (index = 0; index < count; index++)
	{
		unsigned char first = elements[index].code;
		unsigned char second = index + 1 < count ? elements[index + 1].code : 0;
		unsigned long pair = 0;

		if (first == 'p' && second == 'h') pair = LETTER('f');
		else if (first == 'v' && second == 'v') pair = LETTER('w');
		else if (first == '\\' && second == '/') pair = LETTER('v');
		else if (first == '/' && second == '\\') pair = LETTER('a');
		else if (first == '|' && second == '<') pair = LETTER('k');
		else if (first == '>' && second == '<') pair = LETTER('x');
		else if (first == '(' && second == ')') pair = LETTER('o');
		else if (first == '[' && second == ']') pair = LETTER('o');
		else if (first == '|' && second == ')') pair = LETTER('d');
		elements[index].pair = pair;
	}
	return count;
}

/* ---------- matching */

struct attempt
{
	const struct element *elements;
	long count;
	const char *letters;
	long length;
	int rule;
	long start;
	long end;
	/* the line's steps so far (every attempt's), and whether they ran out */
	long *steps;
	int *exhausted;
};

/* what a match has crossed so far */
struct path
{
	long wildcards;
	long real_letters;
	long piece;
	long longest_piece;
	int spaced;
	int crossed;
};

static int chat_filter_try_letter(struct attempt *attempt, long term_index, long element_index, struct path path);

static int chat_filter_word_character(const struct element *element)
{
	return element->kind == _element_letter;
}

/* whether an allowed word covers the match, inside the word it is in */
static int chat_filter_allowed(const struct attempt *attempt)
{
	long word_start = attempt->start;
	long word_end = attempt->end;
	long index;

	while (word_start > 0 && (attempt->elements[word_start - 1].kind == _element_letter ||
		attempt->elements[word_start - 1].kind == _element_digit))
	{
		word_start--;
	}
	while (word_end < attempt->count && (attempt->elements[word_end].kind == _element_letter ||
		attempt->elements[word_end].kind == _element_digit))
	{
		word_end++;
	}
	for (index = 0; index < chat_filter_words.allowed_count; index++)
	{
		const char *allowed = chat_filter_words.allowed_characters + chat_filter_words.allowed[index];
		long length = (long)strlen(allowed);
		long at;

		for (at = word_start; at + length <= word_end; at++)
		{
			long offset;

			if (at > attempt->start || at + length < attempt->end)
				continue;
			for (offset = 0; offset < length; offset++)
			{
				if (!(attempt->elements[at + offset].letters & LETTER(allowed[offset])))
					break;
			}
			if (offset == length)
				return 1;
		}
	}
	return 0;
}

/* the term read to the element before end: whether the match stands */
static int chat_filter_finish(struct attempt *attempt, long end, struct path path)
{
	const struct element *elements = attempt->elements;
	int start_boundary = attempt->start == 0 || !chat_filter_word_character(&elements[attempt->start - 1]);
	int end_boundary = end >= attempt->count || !chat_filter_word_character(&elements[end]);

	if (path.piece > path.longest_piece)
		path.longest_piece = path.piece;
	/* any letter (* ? #) once in a short word, twice in a longer one, and
	never in a word of three letters */
	if (path.wildcards && (attempt->length < 4 || path.wildcards > (attempt->length <= 5 ? 1 : 2)))
		return 0;
	/* a third of the letters letters, not digits or symbols */
	if (!path.real_letters || path.real_letters * 3 < attempt->length)
		return 0;
	/* across spaces: pieces of a letter or two, or whole words */
	if (path.spaced && path.longest_piece > 2 && !(start_boundary && end_boundary))
		return 0;
	switch (attempt->rule)
	{
	case CHAT_FILTER_START:
		if (!start_boundary)
			return 0;
		break;
	case CHAT_FILTER_WHOLE:
		if (!start_boundary)
			return 0;
		if (!end_boundary)
		{
			/* or with "s", or "es" after s, x, z, ch or sh */
			long after = end;
			char last = attempt->letters[attempt->length - 1];
			char before_last = attempt->length > 1 ? attempt->letters[attempt->length - 2] : 0;

			if ((last == 's' || last == 'x' || last == 'z' || (last == 'h' && (before_last == 'c' || before_last == 's'))) &&
				after < attempt->count && (elements[after].letters & LETTER('e')) &&
				elements[after].kind == _element_letter && after + 1 < attempt->count &&
				(elements[after + 1].letters & LETTER('s')))
			{
				after++;
			}
			if (after < attempt->count && (elements[after].letters & LETTER('s')) &&
				(elements[after].kind == _element_letter || elements[after].kind == _element_digit) &&
				(after + 1 >= attempt->count || !chat_filter_word_character(&elements[after + 1])))
			{
				end = after + 1;
			}
			else
			{
				return 0;
			}
		}
		break;
	}
	if (!path.crossed)
	{
		attempt->end = end;
		if (chat_filter_allowed(attempt))
			return 0;
	}
	attempt->end = end;
	return 1;
}

/* the term's letter at term_index from element_index on, after what came
before (spaces and punctuation between letters skipped, at most
MAXIMUM_GAP) */
static int chat_filter_next(struct attempt *attempt, long term_index, long element_index, long gap, int spaced,
	struct path path)
{
	const struct element *element;

	/* (a bound on the work: a line made to send the search down every way
	of reading it stops here, and is masked whole) */
	if (*attempt->exhausted || ++*attempt->steps > CHAT_FILTER_MAXIMUM_STEPS)
	{
		*attempt->exhausted = 1;
		return 0;
	}
	if (term_index == attempt->length)
		return chat_filter_finish(attempt, element_index, path);
	if (element_index >= attempt->count)
		return 0;
	element = &attempt->elements[element_index];
	if (gap > 0)
	{
		path.crossed = 1;
		if (spaced)
		{
			path.spaced = 1;
			if (path.piece > path.longest_piece)
				path.longest_piece = path.piece;
			path.piece = 0;
		}
	}
	if ((element->letters || element->pair) && chat_filter_try_letter(attempt, term_index, element_index, path))
		return 1;
	if (term_index > 0 && gap < MAXIMUM_GAP &&
		(element->kind == _element_space || element->kind == _element_punctuation ||
		element->kind == _element_symbol || element->kind == _element_wildcard))
	{
		return chat_filter_next(attempt, term_index, element_index + 1, gap + 1,
			spaced || element->kind == _element_space, path);
	}
	return 0;
}

/* after the term's letter at term_index, read to element end: the same
letter again is part of it, unless the term has it twice; for a short whole
word, only three or more of it */
static int chat_filter_after(struct attempt *attempt, long term_index, long end, struct path path)
{
	char letter = attempt->letters[term_index];

	if (term_index + 1 >= attempt->length || attempt->letters[term_index + 1] != letter)
	{
		long repeated = end;

		while (repeated < attempt->count &&
			(attempt->elements[repeated].kind == _element_letter || attempt->elements[repeated].kind == _element_digit) &&
			(attempt->elements[repeated].letters & LETTER(letter)))
		{
			repeated++;
		}
		if (repeated != end && (attempt->rule != CHAT_FILTER_WHOLE || attempt->length > 4 || repeated - end >= 2))
		{
			struct path longer = path;

			longer.piece += repeated - end;
			if (chat_filter_next(attempt, term_index + 1, repeated, 0, 0, longer))
				return 1;
		}
	}
	return chat_filter_next(attempt, term_index + 1, end, 0, 0, path);
}

static int chat_filter_try_letter(struct attempt *attempt, long term_index, long element_index, struct path path)
{
	const struct element *element = &attempt->elements[element_index];
	unsigned long letter = LETTER(attempt->letters[term_index]);

	if (element->letters & letter)
	{
		struct path one = path;

		one.piece++;
		if (element->kind == _element_wildcard)
			one.wildcards++;
		else if (element->kind == _element_letter)
			one.real_letters++;
		if ((element->kind != _element_wildcard || term_index > 0) &&
			chat_filter_after(attempt, term_index, element_index + 1, one))
		{
			return 1;
		}
	}
	if (element->pair & letter)
	{
		struct path two = path;

		two.piece++;
		if (element->kind == _element_letter)
			two.real_letters++;
		if (chat_filter_after(attempt, term_index, element_index + 2, two))
			return 1;
	}
	return 0;
}

/* the text's characters that spell a word, marked in marked (as long as
the text); how many matches */
/* the steps the last line took (the tests') */
static long chat_filter_steps_last;

long chat_filter_last_steps(void)
{
	return chat_filter_steps_last;
}

static int chat_filter_find(const char *text, long length, unsigned char *marked)
{
	struct element elements[CHAT_FILTER_MAXIMUM_LENGTH];
	long count;
	long start;
	int matches = 0;
	long steps = 0;
	int exhausted = 0;

	memset(marked, 0, (size_t)length);
	if (!chat_filter_words.term_count)
		return 0;
	if (!chat_filter_words.sorted)
		chat_filter_sort();
	count = chat_filter_elements(text, length, elements, CHAT_FILTER_MAXIMUM_LENGTH);
	for (start = 0; start < count; start++)
	{
		unsigned long letters = elements[start].kind == _element_wildcard ? 0 :
			(elements[start].letters | elements[start].pair);
		long letter;

		for (letter = 0; letters && letter < 26; letter++)
		{
			long order;

			if (!(letters & (1UL << letter)))
				continue;
			for (order = chat_filter_words.first[letter]; order < chat_filter_words.first[letter + 1]; order++)
			{
				const struct term *term = &chat_filter_words.terms[chat_filter_words.order[order]];
				struct attempt attempt;
				struct path path;

				attempt.elements = elements;
				attempt.count = count;
				attempt.letters = chat_filter_words.characters + term->offset;
				attempt.length = term->length;
				attempt.rule = term->rule;
				attempt.start = start;
				attempt.end = start;
				attempt.steps = &steps;
				attempt.exhausted = &exhausted;
				memset(&path, 0, sizeof(path));
				if (chat_filter_try_letter(&attempt, 0, start, path))
				{
					long unit;

					for (unit = elements[start].index; unit <= elements[attempt.end - 1].index; unit++)
						marked[unit] = 1;
					matches++;
				}
				if (exhausted)
					break;
			}
			if (exhausted)
				break;
		}
		if (exhausted)
			break;
	}
	chat_filter_steps_last = steps;
	/* (the work ran out: the whole line masked, its spaces kept) */
	if (exhausted)
	{
		long unit;

		for (unit = 0; unit < length; unit++)
			marked[unit] = text[unit] != ' ';
		matches++;
	}
	return matches;
}

static long chat_filter_length(const char *text)
{
	long length = 0;

	while (length < CHAT_FILTER_MAXIMUM_LENGTH && text[length])
		length++;
	return length;
}

int chat_filter_mask(char *text)
{
	unsigned char marked[CHAT_FILTER_MAXIMUM_LENGTH];
	long length, index;
	int matches;

	if (!text)
		return 0;
	length = chat_filter_length(text);
	matches = chat_filter_find(text, length, marked);
	for (index = 0; matches && index < length; index++)
	{
		if (marked[index])
			text[index] = '*';
	}
	return matches;
}

int chat_filter_count(const char *text)
{
	unsigned char marked[CHAT_FILTER_MAXIMUM_LENGTH];

	return text ? chat_filter_find(text, chat_filter_length(text), marked) : 0;
}
