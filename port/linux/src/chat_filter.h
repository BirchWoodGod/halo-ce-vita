/*
CHAT_FILTER.H

The in-game chat's slur filter (chat_filter.c): which letters of a line spell
a slur or hate phrase, however it is dressed (leet, spacing, punctuation,
repeated letters), masked as '*'. Applied where a line is shown, never where
it is sent: every line another player sends, PC players' too, passes through
it on the machine that shows it, and a player who turns it off (Settings >
Multiplayer > Chat > Chat filter) sees the lines as sent, alone.

The words are a data file (port/vita/app0/chat/filter.txt, made by
tools/chat_filter_words.py), read once. Nothing here knows the game: the
desktop tests build it alone (port/vita/tests/chat_filter_test.c).
*/

#ifndef PORT_CHAT_FILTER_H
#define PORT_CHAT_FILTER_H

#ifdef __cplusplus
extern "C" {
#endif

/* where a word may be found: inside any word, at a word's start, or as the
whole word (or it and "s" or "es") */
enum chat_filter_rule
{
	CHAT_FILTER_ANYWHERE,
	CHAT_FILTER_START,
	CHAT_FILTER_WHOLE,
	CHAT_FILTER_RULE_COUNT
};

/* the longest text looked at (the rest is masked by nothing) */
#define CHAT_FILTER_MAXIMUM_LENGTH 256

/* every word and allowed word let go */
void chat_filter_reset(void);
/* the data file's text (filter.txt's lines: "whole|start|anywhere <rot13
word>", "allow <word>", '#' comments): its words added; how many (a line
it cannot read is skipped) */
int chat_filter_load(const char *text, long size);
/* a word (a-z, plain), and a word never masked nor any part of it; whether
each was kept */
int chat_filter_add(const char *word, int rule);
int chat_filter_allow(const char *word);
/* the words there are */
int chat_filter_word_count(void);

/* the text's letters that spell a word masked in place as '*' (the
match's whole span, its spaces and punctuation too); how many matches */
int chat_filter_mask(char *text);
/* ... and only counted (text unchanged) */
int chat_filter_count(const char *text);

#ifdef __cplusplus
}
#endif

#endif
