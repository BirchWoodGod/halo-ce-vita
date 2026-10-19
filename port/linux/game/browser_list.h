/*
BROWSER_LIST.H

The Server Browser's list (menu_functions.c): every public game listed
(p2p_lobby_entries, up to P2P_LOBBY_MAXIMUM_ENTRIES), the Vitas' games first
and the PCs' after (the owner, Oct 19 2026), each group under a header row
of its own with its count (VITA GAMES, PC GAMES) and sorted as the player
chose with Y (players, name, map, gametype), seen through the screen's rows,
which scroll: down on the last row shows the next game, up on the first the
one before (wrapping at the ends), the triggers a page at a time. The game
chosen stays chosen as the list changes under it (games come, go and change
places while it is open). Kept apart from the menus so that its rules are
tested on their own (port/vita/tests/browser_list_test.c).
*/

#ifndef __BROWSER_LIST_H
#define __BROWSER_LIST_H

#include "../src/p2p.h"

enum
{
	_browser_sort_players,
	_browser_sort_name,
	_browser_sort_map,
	_browser_sort_gametype,
	NUMBER_OF_BROWSER_SORTS
};

/* the rows as last shown: the first game's index, how many rows held a
game, and the focused row (-1: none yet) */
struct browser_view
{
	int offset;
	int visible;
	int row;
};

/* the games sorted: a Vita's first, then a PC build's (CROSSPLAY_PLATFORM_*
other than a Vita's), each group by the sort (players: the most first, then
those joined without failing, those taking players, the name; the others by
their column, then players) */
void browser_list_sort(struct p2p_lobby_entry *entries, int count, int sort);

/* whether the entry is a Vita's game (the first group) */
int browser_list_vita(struct p2p_lobby_entry const *entry);

/* a header row (entry->header) before each group that has games, its name
the group's format (vita_format, pc_format: a %d for the group's count),
in the sorted list of count games with room for capacity entries (two more
than the games). The new count; the games' order kept. Headers already in
the list are taken out first */
int browser_list_group(struct p2p_lobby_entry *entries, int count, int capacity, char const *vita_format,
	char const *pc_format);

/* how many headers, and the game number (1 on) of the entry at index (its
place among the games alone; 0 for a header) */
int browser_list_header_count(struct p2p_lobby_entry const *entries, int count);
int browser_list_game_number(struct p2p_lobby_entry const *entries, int count, int index);

/* the game to choose in place of a header row the player stepped onto
(target): the next game the way they went (from previous, the index of the
game chosen before, -1 none; page as browser_view_follow's), wrapping at the
list's ends as the rows do. target itself when it is a game; -1 with none */
int browser_list_skip_header(struct p2p_lobby_entry const *entries, int count, int target, int previous, int page);

/* the view moved up one row when the game shown at its top is the first of
its group, so that the group's header shows above it */
void browser_view_show_header(struct browser_view *view, struct p2p_lobby_entry const *entries, int rows, int count);

/* browser_view_follow over a grouped list (browser_list_group): a header
row the player steps onto passed over (browser_list_skip_header), the
header of the group at the top of the rows shown. The game's index, -1 with
none */
int browser_view_follow_groups(struct browser_view *view, struct p2p_lobby_entry const *entries, int rows, int count,
	int focused_row, int chosen, int page);

/* the view's first frame (the browser opened, or sorted again) */
void browser_view_reset(struct browser_view *view);

/* the index (in count games) of the game to choose and show, the view moved
so that its row (view->row) shows it: focused_row is the row the menus
focus now (-1: none), so that a step past the first or last row scrolls;
chosen the index the game chosen before has now (-1: gone, or none);
page -1 or 1 a page up or down (0: none). -1 with no games */
int browser_view_follow(struct browser_view *view, int rows, int count, int focused_row, int chosen, int page);

#endif
