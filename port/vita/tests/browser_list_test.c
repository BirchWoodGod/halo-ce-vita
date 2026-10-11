/*
BROWSER_LIST_TEST.C

The Server Browser's list (port/linux/game/browser_list.c): its sorts
(players: the most first, then games not failed, games taking players, the
name; name, map and gametype: letters of either case alike, then players)
and its rows, which scroll: steps down and up through more games than the
twelve rows (the menus wrapping their focus at the ends, as
widget_event_function_list_widget_goto_next_item does), past the list's
ends, a page at a time, the game chosen followed as it moves, the list
growing and shrinking under the rows, two games and none. Built by
run_browser_list_test.sh (with ASan and UBSan there).
*/

#include "../../linux/game/browser_list.c"

#include <stdio.h>
#include <string.h>

enum
{
	ROWS = 12,
};

static int checks, failures;

static void check(int good, const char *what)
{
	checks++;
	if (!good)
		failures++;
	printf("%s %s\n", good ? "PASS" : "FAIL", what);
}

/* the menus' own step: the next or previous row of those shown, wrapping */
static int menu_step(struct browser_view const *view, int direction)
{
	return (view->row + direction + view->visible) % view->visible;
}

/* a step down (1) or up (-1) as the player makes it: the menus move the
focus, then the browser follows */
static int step(struct browser_view *view, int count, int direction)
{
	return browser_view_follow(view, ROWS, count, menu_step(view, direction), -1, 0);
}

static void entry(struct p2p_lobby_entry *e, char const *id, char const *name, char const *map, char const *gametype,
	int players, int failed, int open)
{
	memset(e, 0, sizeof(*e));
	snprintf(e->id, sizeof(e->id), "%s", id);
	snprintf(e->name, sizeof(e->name), "%s", name);
	snprintf(e->map, sizeof(e->map), "%s", map);
	snprintf(e->gametype, sizeof(e->gametype), "%s", gametype);
	e->players = players;
	e->failed = failed;
	e->open = open;
}

static int order_is(struct p2p_lobby_entry const *entries, int count, char const *ids)
{
	char got[128] = "";
	int index;

	for (index = 0; index < count; index++)
	{
		strcat(got, entries[index].id);
		if (index + 1 < count)
			strcat(got, " ");
	}
	if (strcmp(got, ids))
		printf("  got \"%s\", wanted \"%s\"\n", got, ids);
	return !strcmp(got, ids);
}

static void sorts(void)
{
	struct p2p_lobby_entry entries[6];

	entry(&entries[0], "a", "zulu", "Wizard", "Slayer", 3, 0, 1);
	entry(&entries[1], "b", "Alpha", "Blood Gulch", "CTF", 3, 0, 0);
	entry(&entries[2], "c", "bravo", "blood gulch", "slayer", 9, 0, 1);
	entry(&entries[3], "d", "Charlie", "Hang 'Em High", "King", 3, 1, 1);
	entry(&entries[4], "e", "alpha", "Wizard", "Oddball", 0, 0, 1);
	entry(&entries[5], "f", "delta", "Prisoner", "CTF", 3, 0, 1);
	browser_list_sort(entries, 6, _browser_sort_players);
	/* (9 first; of the 3s: not failed, taking players, then by name) */
	check(order_is(entries, 6, "c f a b d e"), "by players: the most first, then not failed, open, the name");
	browser_list_sort(entries, 6, _browser_sort_name);
	/* (Alpha and alpha alike: players, then the id) */
	check(order_is(entries, 6, "b e c d f a"), "by name: either case alike, then players");
	browser_list_sort(entries, 6, _browser_sort_map);
	check(order_is(entries, 6, "c b d f a e"), "by map: either case alike, then players");
	browser_list_sort(entries, 6, _browser_sort_gametype);
	check(order_is(entries, 6, "f b d e c a"), "by gametype: either case alike, then players");
	browser_list_sort(entries, 6, 99);
	check(order_is(entries, 6, "c f a b d e"), "a sort out of range: by players");
	browser_list_sort(entries, 0, _browser_sort_name);
	browser_list_sort(NULL, 6, _browser_sort_name);
	check(1, "no games, or none given: nothing sorted");
}

static void scrolling(void)
{
	struct browser_view view;
	int target, index, good;

	browser_view_reset(&view);
	target = browser_view_follow(&view, ROWS, 30, 0, -1, 0);
	check(target == 0 && view.offset == 0 && view.row == 0 && view.visible == ROWS, "30 games: the first, on row 1");
	for (index = 0, good = 1; index < 11; index++)
	{
		target = step(&view, 30, 1);
		good &= target == index + 1 && view.offset == 0 && view.row == index + 1;
	}
	check(good, "down through the rows shown: the rows stay");
	target = step(&view, 30, 1);
	check(target == 12 && view.offset == 1 && view.row == 11, "down on the last row: the 13th game, the rows scrolled one");
	for (index = 13, good = 1; index < 30; index++)
	{
		target = step(&view, 30, 1);
		good &= target == index && view.offset == index - 11 && view.row == 11;
	}
	check(good, "down to the last game: the rows scroll with it");
	target = step(&view, 30, 1);
	check(target == 0 && view.offset == 0 && view.row == 0, "down on the last game: back to the first");
	target = step(&view, 30, -1);
	check(target == 29 && view.offset == 18 && view.row == 11, "up on the first game: the last");
	for (index = 0; index < 11; index++)
		target = step(&view, 30, -1);
	check(target == 18 && view.offset == 18 && view.row == 0, "up through the rows shown: the rows stay");
	target = step(&view, 30, -1);
	check(target == 17 && view.offset == 17 && view.row == 0, "up on the first row: the game before, the rows scrolled");
	target = browser_view_follow(&view, ROWS, 30, view.row, -1, 1);
	check(target == 29 && view.offset == 18 && view.row == 11, "a page down: twelve on, no further than the last");
	target = browser_view_follow(&view, ROWS, 30, view.row, -1, -1);
	check(target == 17 && view.offset == 17 && view.row == 0, "a page up: twelve back");
	target = browser_view_follow(&view, ROWS, 30, view.row, -1, -1);
	check(target == 5 && view.offset == 5 && view.row == 0, "a page up again");
	target = browser_view_follow(&view, ROWS, 30, view.row, -1, -1);
	check(target == 0 && view.offset == 0 && view.row == 0, "a page up from the 6th: the first");

	/* the game chosen, moved by the list (no step of the player's) */
	target = browser_view_follow(&view, ROWS, 30, view.row, 20, 0);
	check(target == 20 && view.offset == 9 && view.row == 11, "the game chosen moved down the list: followed, just shown");
	target = browser_view_follow(&view, ROWS, 30, view.row, 3, 0);
	check(target == 3 && view.offset == 3 && view.row == 0, "and up: followed");
	target = browser_view_follow(&view, ROWS, 30, view.row, 7, 0);
	check(target == 7 && view.offset == 3 && view.row == 4, "within the rows shown: the rows stay");
	/* a step wins over where the chosen game went */
	target = browser_view_follow(&view, ROWS, 30, menu_step(&view, 1), 7, 0);
	check(target == 8 && view.row == 5, "a step of the player's: the next game, not the one chosen before");

	/* the list shrinking under the rows, the game chosen gone */
	target = browser_view_follow(&view, ROWS, 16, view.row, -1, 0);
	check(target == 8 && view.offset == 3 && view.visible == ROWS, "16 games: the same row");
	browser_view_follow(&view, ROWS, 30, view.row, 29, 0);
	target = browser_view_follow(&view, ROWS, 14, view.row, -1, 0);
	check(target == 13 && view.offset == 2 && view.row == 11 && view.visible == ROWS,
		"down to 14 games from the end of 30: the last game, the rows full");
	target = browser_view_follow(&view, ROWS, 3, view.row, -1, 0);
	check(target == 2 && view.offset == 0 && view.row == 2 && view.visible == 3, "3 games: the last, no scrolling");
	/* growing again: no scroll taken for a wrap */
	target = browser_view_follow(&view, ROWS, 30, menu_step(&view, 1), -1, 0);
	check(target == 0 && view.offset == 0, "down on the last of 3 as the list grows to 30: the first, not the 4th");

	/* two games: the menus' step is the browser's */
	browser_view_reset(&view);
	browser_view_follow(&view, ROWS, 2, 0, -1, 0);
	target = step(&view, 2, 1);
	check(target == 1 && view.row == 1, "2 games: down to the second");
	target = step(&view, 2, 1);
	check(target == 0 && view.row == 0, "2 games: down on the second, the first");
	target = step(&view, 2, -1);
	check(target == 1 && view.row == 1, "2 games: up on the first, the second");
	/* one game, twelve exactly */
	browser_view_reset(&view);
	target = browser_view_follow(&view, ROWS, 1, 0, -1, 1);
	check(target == 0 && view.visible == 1, "1 game: a page down stays on it");
	browser_view_reset(&view);
	browser_view_follow(&view, ROWS, 12, 0, -1, 0);
	target = step(&view, 12, -1);
	check(target == 11 && view.offset == 0, "12 games: up on the first, the last, no scrolling");
	target = step(&view, 12, 1);
	check(target == 0 && view.offset == 0, "12 games: down on the last, the first");

	/* none */
	target = browser_view_follow(&view, ROWS, 0, 3, 2, 1);
	check(target == -1 && view.row == -1 && view.offset == 0 && view.visible == 0, "no games: none chosen");
	target = browser_view_follow(&view, ROWS, 5, -1, -1, 0);
	check(target == 0 && view.row == 0, "games again: the first");
	/* a focused row past those shown (the menus' before the rows hid) */
	target = browser_view_follow(&view, ROWS, 5, 9, -1, 0);
	check(target == 0 && view.row == 0, "a row focused past the games shown: no step taken");
}

int main(void)
{
	sorts();
	scrolling();
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures != 0;
}
