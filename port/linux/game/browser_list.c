/*
BROWSER_LIST.C

The Server Browser's list: its sorts and its scrolling rows (browser_list.h).
*/

#include "browser_list.h"

#include <stdlib.h>
#include <string.h>

/* ---------- private code */

static int lower(
	int character)
{
	return character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character;
}

/* names compared as the player reads them: letters of either case alike */
static int text_order(
	char const *a,
	char const *b)
{
	while (*a && lower((unsigned char)*a) == lower((unsigned char)*b))
	{
		a++;
		b++;
	}
	return lower((unsigned char)*a) - lower((unsigned char)*b);
}

/* p2p_lobby.c's listing_order: the most players first, then games joined
without failing, games taking players, the name */
static int players_order(
	struct p2p_lobby_entry const *a,
	struct p2p_lobby_entry const *b)
{
	if (a->players != b->players)
		return b->players - a->players;
	if (a->failed != b->failed)
		return a->failed - b->failed;
	if (a->open != b->open)
		return b->open - a->open;
	return text_order(a->name, b->name);
}

static int sort_chosen;

static int entry_order(
	void const *first,
	void const *second)
{
	struct p2p_lobby_entry const *a = first, *b = second;
	int order = 0;

	switch (sort_chosen)
	{
	case _browser_sort_name:
		order = text_order(a->name, b->name);
		break;
	case _browser_sort_map:
		order = text_order(a->map, b->map);
		break;
	case _browser_sort_gametype:
		order = text_order(a->gametype, b->gametype);
		break;
	}
	if (!order)
		order = players_order(a, b);
	/* (the same order every time: the id last) */
	return order ? order : strcmp(a->id, b->id);
}

static int clamp(
	int value,
	int minimum,
	int maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

/* ---------- public code */

void browser_list_sort(
	struct p2p_lobby_entry *entries,
	int count,
	int sort)
{
	if (!entries || count < 2)
		return;
	sort_chosen = sort >= 0 && sort < NUMBER_OF_BROWSER_SORTS ? sort : _browser_sort_players;
	qsort(entries, (size_t)count, sizeof(*entries), entry_order);
}

void browser_view_reset(
	struct browser_view *view)
{
	view->offset = 0;
	view->visible = 0;
	view->row = -1;
}

int browser_view_follow(
	struct browser_view *view,
	int rows,
	int count,
	int focused_row,
	int chosen,
	int page)
{
	int target;

	if (count <= 0 || rows <= 0)
	{
		browser_view_reset(view);
		return -1;
	}
	/* the player stepped to another row: past the last or the first of a
	full screen of rows (the menus wrap to the other end), the next game or
	the one before, wrapping at the list's ends; else the game on that row */
	if (focused_row >= 0 && view->row >= 0 && focused_row != view->row && focused_row < view->visible)
	{
		int last = view->visible - 1;

		if (view->visible == rows && view->row == last && focused_row == 0)
			target = view->offset + rows < count ? view->offset + rows : 0;
		else if (view->visible == rows && view->row == 0 && focused_row == last)
			target = view->offset > 0 ? view->offset - 1 : count - 1;
		else
			target = view->offset + focused_row;
	}
	/* else the game chosen, wherever it went; else the same row */
	else if (chosen >= 0)
		target = chosen;
	else
		target = view->offset + (view->row >= 0 ? view->row : 0);
	target = clamp(target + page * rows, 0, count - 1);
	/* the rows moved only as far as showing it takes */
	if (target < view->offset)
		view->offset = target;
	else if (target >= view->offset + rows)
		view->offset = target - rows + 1;
	view->offset = clamp(view->offset, 0, count > rows ? count - rows : 0);
	view->visible = count - view->offset < rows ? count - view->offset : rows;
	view->row = target - view->offset;
	return target;
}
