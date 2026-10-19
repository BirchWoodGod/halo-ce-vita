/*
BROWSER_LIST_TEST.C

The Server Browser's list (port/linux/game/browser_list.c): its sorts
(players: the most first, then games not failed, games taking players, the
name; name, map and gametype: letters of either case alike, then players)
and its rows, which scroll: steps down and up through more games than the
twelve rows (the menus wrapping their focus at the ends, as
widget_event_function_list_widget_goto_next_item does), past the list's
ends, a page at a time, the game chosen followed as it moves, the list
growing and shrinking under the rows, two games and none; and the groups
(the Vitas' games first, then the PCs', a header row before each, never
chosen: stepped and paged over, shown above the group's first game).
Built by
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

/* a step over a grouped list, as browser_update makes it */
static int group_step(struct browser_view *view, struct p2p_lobby_entry const *entries, int count, int direction,
	int chosen)
{
	return browser_view_follow_groups(view, entries, ROWS, count, menu_step(view, direction), chosen, 0);
}

static void platform_entry(struct p2p_lobby_entry *e, char const *id, int platform, int players)
{
	entry(e, id, id, "Blood Gulch", "Slayer", players, 0, 1);
	e->platform = platform;
}

static void groups(void)
{
	struct p2p_lobby_entry entries[44];
	struct browser_view view;
	char id[16];
	int count, target, index;

	/* the Vitas' games first, each group by players; a header before each */
	platform_entry(&entries[0], "p1", CROSSPLAY_PLATFORM_OPENCE, 15);
	platform_entry(&entries[1], "v1", CROSSPLAY_PLATFORM_VITA, 3);
	platform_entry(&entries[2], "p2", CROSSPLAY_PLATFORM_CHUPA, 9);
	platform_entry(&entries[3], "v2", CROSSPLAY_PLATFORM_VITA, 7);
	platform_entry(&entries[4], "p3", CROSSPLAY_PLATFORM_PC, 1);
	platform_entry(&entries[5], "v3", CROSSPLAY_PLATFORM_VITA, 0);
	platform_entry(&entries[6], "p4", CROSSPLAY_PLATFORM_DAMNATION, 4);
	browser_list_sort(entries, 7, _browser_sort_players);
	check(order_is(entries, 7, "v2 v1 v3 p1 p2 p4 p3"), "the Vitas' games first, then the PCs', each by players");
	browser_list_sort(entries, 7, _browser_sort_name);
	check(order_is(entries, 7, "v1 v2 v3 p1 p2 p3 p4"), "by name: still the Vitas' first");
	browser_list_sort(entries, 7, _browser_sort_players);
	count = browser_list_group(entries, 7, 9, "VITA GAMES (%d)", "PC GAMES (%d)");
	check(count == 9 && order_is(entries, 9, "#vita v2 v1 v3 #pc p1 p2 p4 p3"), "a header before each group");
	check(entries[0].header && !strcmp(entries[0].name, "VITA GAMES (3)") && entries[4].header &&
		!strcmp(entries[4].name, "PC GAMES (4)") && !entries[1].header, "the headers' names and counts");
	check(browser_list_group(entries, count, 9, "VITA GAMES (%d)", "PC GAMES (%d)") == 9 &&
		order_is(entries, 9, "#vita v2 v1 v3 #pc p1 p2 p4 p3"), "grouped again: the same, no header twice");
	check(browser_list_header_count(entries, 9) == 2 && browser_list_game_number(entries, 9, 5) == 4 &&
		browser_list_game_number(entries, 9, 1) == 1 && browser_list_game_number(entries, 9, 4) == 0,
		"the game numbers: the headers not counted");
	/* the rows: the headers never chosen */
	browser_view_reset(&view);
	target = browser_view_follow_groups(&view, entries, ROWS, 9, -1, -1, 0);
	check(target == 1 && view.row == 1 && view.offset == 0, "opened: the first game, under its header");
	target = group_step(&view, entries, 9, 1, 1);
	target = group_step(&view, entries, 9, 1, target);
	check(target == 3, "down: the Vitas' games");
	target = group_step(&view, entries, 9, 1, target);
	check(target == 5 && view.row == 5, "down from the last Vita game: over PC GAMES, the first PC game");
	target = group_step(&view, entries, 9, -1, target);
	check(target == 3, "up from the first PC game: over the header, the last Vita game");
	browser_view_follow_groups(&view, entries, ROWS, 9, -1, 8, 0);
	target = group_step(&view, entries, 9, 1, 8);
	check(target == 1, "down on the last game: over VITA GAMES, the first game");
	target = group_step(&view, entries, 9, -1, 1);
	check(target == 8, "up on the first game: the last");
	/* one group alone (a filter): its header alone */
	for (index = 0; index < 4; index++)
		platform_entry(&entries[index], index ? "p" : "q", CROSSPLAY_PLATFORM_OPENCE, index);
	count = browser_list_group(entries, 4, 6, "VITA GAMES (%d)", "PC GAMES (%d)");
	check(count == 5 && entries[0].header && !strcmp(entries[0].name, "PC GAMES (4)") && !entries[1].header,
		"PC games alone: PC GAMES over them, no VITA GAMES");
	check(browser_list_group(entries, 0, 2, "V %d", "P %d") == 0, "no games: no header");
	/* no room for a header: the games kept */
	for (index = 0; index < 4; index++)
		platform_entry(&entries[index], index ? "p" : "q", CROSSPLAY_PLATFORM_OPENCE, index);
	check(browser_list_group(entries, 4, 4, "V %d", "P %d") == 4 && !entries[0].header,
		"no room: no header, every game kept");
	/* more than the rows: scrolled back up, the header shows again */
	for (index = 0; index < 40; index++)
	{
		snprintf(id, sizeof(id), "%c%02d", index < 20 ? 'v' : 'p', index);
		platform_entry(&entries[index], id, index < 20 ? CROSSPLAY_PLATFORM_VITA : CROSSPLAY_PLATFORM_OPENCE, 40 - index);
	}
	count = browser_list_group(entries, 40, 44, "VITA GAMES (%d)", "PC GAMES (%d)");
	check(count == 42 && entries[21].header, "40 games: 42 rows with the headers");
	browser_view_reset(&view);
	target = browser_view_follow_groups(&view, entries, ROWS, count, -1, -1, 0);
	for (index = 0; index < 14; index++)
		target = group_step(&view, entries, count, 1, target);
	check(target == 15 && view.offset == 4, "down fourteen: the rows scrolled");
	while (target > 1)
		target = group_step(&view, entries, count, -1, target);
	check(target == 1 && view.offset == 0 && view.row == 1, "back up to the first game: VITA GAMES shown above it");
	target = browser_view_follow_groups(&view, entries, ROWS, count, -1, 9, 1);
	check(target == 22 && !entries[target].header, "a page down onto PC GAMES: the first PC game");
	target = browser_view_follow_groups(&view, entries, ROWS, count, -1, 22, -1);
	check(target == 10, "a page up");
	target = browser_view_follow_groups(&view, entries, ROWS, count, -1, 10, -1);
	check(target == 1 && view.offset == 0, "a page up to the top: the first game, not the header nor the last");
	target = browser_view_follow_groups(&view, entries, ROWS, count, -1, 30, 0);
	check(target == 30 && view.offset == 19 && entries[view.offset].header == 0, "a PC game chosen: followed");
	for (index = 0; index < 8; index++)
		target = group_step(&view, entries, count, -1, target);
	check(target == 22 && view.offset <= 21 && view.offset + view.row == 22, "up to the first PC game: PC GAMES shown above it");
	browser_view_follow_groups(&view, entries, ROWS, count, -1, 35, 0);
	target = browser_view_follow_groups(&view, entries, ROWS, count, 0, 35, 0);
	while (target > 22)
		target = group_step(&view, entries, count, -1, target);
	check(target == 22 && view.offset == 21 && view.row == 1, "from further down, up to it: PC GAMES brought into the rows");
}

int main(void)
{
	sorts();
	scrolling();
	groups();
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures != 0;
}
