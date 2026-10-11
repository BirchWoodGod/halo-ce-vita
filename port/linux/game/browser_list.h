/*
BROWSER_LIST.H

The Server Browser's list (menu_functions.c): every public game listed
(p2p_lobby_entries, up to P2P_LOBBY_MAXIMUM_ENTRIES), sorted as the player
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

/* the games sorted (players: the most first, then those joined without
failing, those taking players, the name; the others by their column, then
players) */
void browser_list_sort(struct p2p_lobby_entry *entries, int count, int sort);

/* the view's first frame (the browser opened, or sorted again) */
void browser_view_reset(struct browser_view *view);

/* the index (in count games) of the game to choose and show, the view moved
so that its row (view->row) shows it: focused_row is the row the menus
focus now (-1: none), so that a step past the first or last row scrolls;
chosen the index the game chosen before has now (-1: gone, or none);
page -1 or 1 a page up or down (0: none). -1 with no games */
int browser_view_follow(struct browser_view *view, int rows, int count, int focused_row, int chosen, int page);

#endif
