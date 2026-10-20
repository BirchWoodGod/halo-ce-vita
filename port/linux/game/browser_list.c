/*
BROWSER_LIST.C

The Server Browser's list: its sorts and its scrolling rows (browser_list.h).
*/

#include "browser_list.h"
#include "../src/crossplay_guard.h"

#include <stdio.h>
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

	/* (the groups: the Vitas' games first) */
	if (browser_list_vita(a) != browser_list_vita(b))
		return browser_list_vita(b) - browser_list_vita(a);
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

int browser_list_vita(
	struct p2p_lobby_entry const *entry)
{
	/* (the PC build's games, VITA (PC), are the PCs') */
	return crossplay_guard_vita_game(entry->platform, entry->marks);
}

int browser_list_group(
	struct p2p_lobby_entry *entries,
	int count,
	int capacity,
	char const *vita_format,
	char const *pc_format)
{
	int index, kept = 0, vita = 0, group;

	if (!entries || count <= 0)
		return 0;
	/* (any header there already out, the games in their order) */
	for (index = 0; index < count; index++)
	{
		if (entries[index].header)
			continue;
		if (kept != index)
			entries[kept] = entries[index];
		vita += browser_list_vita(&entries[kept]);
		kept++;
	}
	count = kept;
	/* (each group's header before its first game: the PCs' first, so that
	the Vitas' stay where they are until theirs goes in) */
	for (group = 1; group >= 0; group--)
	{
		int games = group ? count - vita : vita;
		int at = group ? vita : 0;
		struct p2p_lobby_entry *header;

		if (!games || count >= capacity)
			continue;
		memmove(&entries[at + 1], &entries[at], (size_t)(count - at) * sizeof(*entries));
		count++;
		header = &entries[at];
		memset(header, 0, sizeof(*header));
		header->header = 1;
		header->platform = group ? CROSSPLAY_PLATFORM_PC : CROSSPLAY_PLATFORM_VITA;
		header->difficulty = -1;
		snprintf(header->id, sizeof(header->id), "#%s", group ? "pc" : "vita");
		snprintf(header->name, sizeof(header->name), group ? pc_format : vita_format, games);
	}
	return count;
}

int browser_list_header_count(
	struct p2p_lobby_entry const *entries,
	int count)
{
	int index, headers = 0;

	for (index = 0; entries && index < count; index++)
		headers += entries[index].header != 0;
	return headers;
}

int browser_list_game_number(
	struct p2p_lobby_entry const *entries,
	int count,
	int index)
{
	if (!entries || index < 0 || index >= count || entries[index].header)
		return 0;
	return index + 1 - browser_list_header_count(entries, index);
}

int browser_list_skip_header(
	struct p2p_lobby_entry const *entries,
	int count,
	int target,
	int previous,
	int page)
{
	int step, tries;

	if (!entries || count <= 0 || target < 0 || target >= count)
		return -1;
	if (!entries[target].header)
		return target;
	/* the way the player went: a page's; at the top (the first header) down,
	unless they stepped up from the first game (the rows wrap to the end);
	from the last game to the top (wrapped) down; else from the game before */
	if (target == 0)
		step = previous == 1 && !page ? -1 : 1;
	else if (page)
		step = page > 0 ? 1 : -1;
	else if (previous < 0)
		step = 1;
	else if (target == count - 1 && previous == 0)
		step = -1;
	else
		step = target >= previous ? 1 : -1;
	for (tries = 0; tries < count; tries++)
	{
		target = (target + step + count) % count;
		if (!entries[target].header)
			return target;
	}
	return -1;
}

void browser_view_show_header(
	struct browser_view *view,
	struct p2p_lobby_entry const *entries,
	int rows,
	int count)
{
	if (!view || !entries || view->offset <= 0 || view->offset >= count || !entries[view->offset - 1].header ||
		view->row + 1 >= rows)
	{
		return;
	}
	view->offset--;
	view->row++;
	view->visible = count - view->offset < rows ? count - view->offset : rows;
}

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

int browser_view_follow_groups(
	struct browser_view *view,
	struct p2p_lobby_entry const *entries,
	int rows,
	int count,
	int focused_row,
	int chosen,
	int page)
{
	int target = browser_view_follow(view, rows, count, focused_row, chosen, page);
	int game = browser_list_skip_header(entries, count, target, chosen, page);

	if (game >= 0 && game != target)
		target = browser_view_follow(view, rows, count, -1, game, 0);
	else if (game < 0)
		target = -1;
	if (target >= 0)
		browser_view_show_header(view, entries, rows, count);
	return target;
}
