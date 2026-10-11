/*
CROSSPLAY_GUARD_TEST.C

The crossplay guardrails' rules (port/linux/src/crossplay_guard.c,
docs/crossplay.md 6, T11): co-op refused, a large PC game (more than 16
players in it, or a maximum above 16) asked about and never refused, the
counts taken as any int (a PC peer's); the answers remembered by a game's
name (its letters and digits, case aside, at most 15), for two minutes, four
games at most, the oldest forgotten first; a name with nothing that names
it never matched; HALO_CROSSPLAY_LARGE_ANSWER; the badges. Built by
run_crossplay_guard_test.sh (with ASan and UBSan there).
*/

#include "../../linux/src/crossplay_guard.c"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *lang_text(const char *english)
{
	return english;
}

static int checks, failures;

static void check(int good, const char *what)
{
	checks++;
	if (!good)
		failures++;
	printf("%s %s\n", good ? "PASS" : "FAIL", what);
}

int main(void)
{
	unsigned long now = 1000;
	char long_name[300];
	int index;

	/* the verdict */
	check(crossplay_guard_judge(0, 16, 16) == CROSSPLAY_GUARD_OK, "16 of 16: an ordinary game");
	check(crossplay_guard_judge(0, 1, 17) == CROSSPLAY_GUARD_LARGE, "a maximum of 17: large");
	check(crossplay_guard_judge(0, 17, 16) == CROSSPLAY_GUARD_LARGE, "17 players (past its own maximum): large");
	check(crossplay_guard_judge(0, 5, 128) == CROSSPLAY_GUARD_LARGE, "OpenCE's fast setup (128): large");
	check(crossplay_guard_judge(1, 2, 4) == CROSSPLAY_GUARD_COOP && crossplay_guard_judge(1, 40, 128) == CROSSPLAY_GUARD_COOP,
		"co-op: refused, whatever its size");
	check(crossplay_guard_judge(0, INT_MIN, INT_MIN) == CROSSPLAY_GUARD_OK &&
		crossplay_guard_judge(0, INT_MAX, -1) == CROSSPLAY_GUARD_LARGE &&
		crossplay_guard_judge(0, -1, INT_MAX) == CROSSPLAY_GUARD_LARGE, "counts at the ends of an int: no refusal, no overflow");
	/* the Xbox's multiplayer levels: never co-op */
	check(crossplay_guard_stock_multiplayer_map("levels\\test\\bloodgulch\\bloodgulch", 128) &&
		crossplay_guard_stock_multiplayer_map("levels/test/wizard/wizard", 128) &&
		crossplay_guard_stock_multiplayer_map("bloodgulch", 32) && crossplay_guard_stock_multiplayer_map("BloodGulch", 32),
		"the Xbox's multiplayer levels, by path or a listing's bare name");
	check(!crossplay_guard_stock_multiplayer_map("custom_maps\\bloodgulch", 128) &&
		!crossplay_guard_stock_multiplayer_map("levels\\a10\\a10", 128) &&
		!crossplay_guard_stock_multiplayer_map("a10", 8) && !crossplay_guard_stock_multiplayer_map("", 8) &&
		!crossplay_guard_stock_multiplayer_map(NULL, 8) && !crossplay_guard_stock_multiplayer_map("bloodgulch", 5) &&
		!crossplay_guard_stock_multiplayer_map("levels\\test\\bloodgulchx\\bloodgulchx", 128),
		"not: Halo PC's custom_maps, a campaign level, nothing, a name cut by its size, a longer name");
	{
		char unended[40];

		memset(unended, 'b', sizeof(unended));
		check(!crossplay_guard_stock_multiplayer_map(unended, (int)sizeof(unended)), "an unended name: read within its size");
	}
	/* the answers */
	check(crossplay_guard_answered("XpHost", now) == -1, "nothing answered yet");
	crossplay_guard_answer("XpHost", 1, now);
	check(crossplay_guard_answered("XpHost", now) == 1 && crossplay_guard_answered("xp host!", now + 5) == 1 &&
		crossplay_guard_answered("X-P-H-O-S-T", now) == 1, "yes, remembered by the name's letters and digits, case aside");
	check(crossplay_guard_answered("XpHost2", now) == -1 && crossplay_guard_answered("Xp", now) == -1,
		"another name: not answered");
	check(crossplay_guard_answered("XpHost", now + CROSSPLAY_GUARD_ANSWER_MILLISECONDS - 1) == 1 &&
		crossplay_guard_answered("XpHost", now + CROSSPLAY_GUARD_ANSWER_MILLISECONDS) == -1,
		"two minutes, then forgotten");
	crossplay_guard_answer("XpHost", 1, now);
	crossplay_guard_answer("XpHost", 0, now + 10);
	check(crossplay_guard_answered("XpHost", now + 10) == 0, "a newer answer replaces it");
	crossplay_guard_answer("", 1, now);
	crossplay_guard_answer("!!! ---", 1, now);
	crossplay_guard_answer(NULL, 1, now);
	check(crossplay_guard_answered("", now) == -1 && crossplay_guard_answered("???", now) == -1 &&
		crossplay_guard_answered(NULL, now) == -1, "a name that names nothing: never answered, never matched");
	/* (bytes past ASCII, controls: not part of the key) */
	crossplay_guard_answer("\xff\x01Gulch\x80\x7f", 1, now);
	check(crossplay_guard_answered("gulch", now) == 1, "bytes past ASCII and controls left out of the key");
	/* (a name longer than a game's: its first 15 letters and digits) */
	memset(long_name, 'a', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = 0;
	crossplay_guard_answer(long_name, 1, now);
	check(crossplay_guard_answered("aaaaaaaaaaaaaaa", now) == 1, "a long name: its first 15");
	/* four games at most, the oldest forgotten */
	crossplay_guard_forget();
	for (index = 0; index < 5; index++)
	{
		char name[8];

		snprintf(name, sizeof(name), "game%d", index);
		crossplay_guard_answer(name, 1, now + (unsigned long)index);
	}
	check(crossplay_guard_answered("game0", now + 5) == -1 && crossplay_guard_answered("game1", now + 5) == 1 &&
		crossplay_guard_answered("game4", now + 5) == 1, "a fifth game: the oldest forgotten");
	crossplay_guard_forget();
	check(crossplay_guard_answered("game4", now) == -1, "forget: none left");
	/* the automated runs' answer */
	unsetenv("HALO_CROSSPLAY_LARGE_ANSWER");
	check(crossplay_guard_test_answer() == -1, "no HALO_CROSSPLAY_LARGE_ANSWER: the player is asked");
	setenv("HALO_CROSSPLAY_LARGE_ANSWER", "yes", 1);
	check(crossplay_guard_test_answer() == 1, "yes");
	setenv("HALO_CROSSPLAY_LARGE_ANSWER", "no", 1);
	check(crossplay_guard_test_answer() == 0, "no");
	setenv("HALO_CROSSPLAY_LARGE_ANSWER", "", 1);
	check(crossplay_guard_test_answer() == -1, "empty: asked");
	/* badges and texts */
	check(!strcmp(crossplay_guard_badge(1, 0), "PC") && !strcmp(crossplay_guard_badge(1, 1), "PC SERVER") &&
		!strcmp(crossplay_guard_badge(0, 0), "VITA") && !strcmp(crossplay_guard_badge(0, 1), "SERVER"), "the badges");
	check(!strcmp(crossplay_guard_large_question(),
		"Large PC game: more than 16 players can make the Vita lag. Join anyway?") &&
		strstr(crossplay_guard_coop_refusal(), "co-op") != NULL, "the owner's question, word for word; the co-op refusal");
	printf("%s (%d of %d checks failed)\n", failures ? "FAIL" : "PASS", failures, checks);
	return failures != 0;
}
