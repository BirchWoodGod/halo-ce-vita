/*
MCC_CONTENT.C

(port) The Halo content folder's names and loaders (mcc_content.h): MCC's stock
maps, their titles and pictures, and the level names that are MCC's. Plain
C; what is done with them is custom_edition_cache.c's (loading),
custom_edition_maps.c's and custom_edition_mods.c's (the level lists).
*/

#include "mcc_content.h"

#include <stdio.h>
#include <string.h>

struct mcc_content_map
{
	char const *stem;
	int kind;
	char const *title;
	/* the menus' picture frame, -1 for none */
	int frame;
};

/* the content folder's games, and the loader of each one's map files: the
cache header's 'head' and version choose (content_library_loader_for_header).
Halo CE's alone has a loader in this build; the others' are found and said
(custom_edition_maps.c), and plug in here, a row and a loader, as they come */
static struct content_library_game const content_library_games[] =
{
	{ "halo1", "Halo: Combat Evolved Anniversary", 13, _content_library_loader_halo1 },
	{ "halo2", "Halo 2", 0, _content_library_loader_none },
	{ "halo3", "Halo 3", 0, _content_library_loader_none },
	{ "halo3odst", "Halo 3: ODST", 0, _content_library_loader_none },
	{ "haloreach", "Halo: Reach", 0, _content_library_loader_none },
	{ "halo4", "Halo 4", 0, _content_library_loader_none },
};

#define CONTENT_LIBRARY_GAME_COUNT ((int)(sizeof(content_library_games) / sizeof(content_library_games[0])))
/* 'head', as the cache header's first four bytes read little-endian */
#define CACHE_HEADER_SIGNATURE 0x68656164UL

struct content_library_game const *content_library_game_get(int index)
{
	return index >= 0 && index < CONTENT_LIBRARY_GAME_COUNT ? &content_library_games[index] : NULL;
}

int content_library_loader_for_header(unsigned char const *header, unsigned long size, int *game_index)
{
	unsigned long signature, version;
	int index;

	if (game_index)
		*game_index = -1;
	if (!header || size < 8)
		return _content_library_loader_none;
	signature = (unsigned long)header[0] | (unsigned long)header[1] << 8 | (unsigned long)header[2] << 16 |
		(unsigned long)header[3] << 24;
	version = (unsigned long)header[4] | (unsigned long)header[5] << 8 | (unsigned long)header[6] << 16 |
		(unsigned long)header[7] << 24;
	if (signature != CACHE_HEADER_SIGNATURE)
		return _content_library_loader_none;
	for (index = 0; index < CONTENT_LIBRARY_GAME_COUNT; index++)
	{
		if (content_library_games[index].cache_version && content_library_games[index].cache_version == version)
		{
			if (game_index)
				*game_index = index;
			return content_library_games[index].loader;
		}
	}
	return _content_library_loader_none;
}

/* Halo CE's (halo1\maps): the campaign in its order, then the multiplayer maps
(the Xbox's thirteen in the menus' order, whose pictures are those frames
of the multiplayer level pictures, then Halo PC's six) */
static struct mcc_content_map const mcc_content_maps[] =
{
	{ "a10", _mcc_content_campaign, "The Pillar of Autumn", 0 },
	{ "a30", _mcc_content_campaign, "Halo", 1 },
	{ "a50", _mcc_content_campaign, "The Truth and Reconciliation", 2 },
	{ "b30", _mcc_content_campaign, "The Silent Cartographer", 3 },
	{ "b40", _mcc_content_campaign, "Assault on the Control Room", 4 },
	{ "c10", _mcc_content_campaign, "343 Guilty Spark", 5 },
	{ "c20", _mcc_content_campaign, "The Library", 6 },
	{ "c40", _mcc_content_campaign, "Two Betrayals", 7 },
	{ "d20", _mcc_content_campaign, "Keyes", 8 },
	{ "d40", _mcc_content_campaign, "The Maw", 9 },
	{ "beavercreek", _mcc_content_multiplayer, "Battle Creek", 0 },
	{ "sidewinder", _mcc_content_multiplayer, "Sidewinder", 1 },
	{ "damnation", _mcc_content_multiplayer, "Damnation", 2 },
	{ "ratrace", _mcc_content_multiplayer, "Rat Race", 3 },
	{ "prisoner", _mcc_content_multiplayer, "Prisoner", 4 },
	{ "hangemhigh", _mcc_content_multiplayer, "Hang 'Em High", 5 },
	{ "chillout", _mcc_content_multiplayer, "Chill Out", 6 },
	{ "carousel", _mcc_content_multiplayer, "Derelict", 7 },
	{ "boardingaction", _mcc_content_multiplayer, "Boarding Action", 8 },
	{ "bloodgulch", _mcc_content_multiplayer, "Blood Gulch", 9 },
	{ "wizard", _mcc_content_multiplayer, "Wizard", 10 },
	{ "putput", _mcc_content_multiplayer, "Chiron TL-34", 11 },
	{ "longest", _mcc_content_multiplayer, "Longest", 12 },
	{ "dangercanyon", _mcc_content_multiplayer, "Danger Canyon", -1 },
	{ "deathisland", _mcc_content_multiplayer, "Death Island", -1 },
	{ "gephyrophobia", _mcc_content_multiplayer, "Gephyrophobia", -1 },
	{ "icefields", _mcc_content_multiplayer, "Ice Fields", -1 },
	{ "infinity", _mcc_content_multiplayer, "Infinity", -1 },
	{ "timberland", _mcc_content_multiplayer, "Timberland", -1 },
};

#define MCC_CONTENT_MAP_COUNT ((int)(sizeof(mcc_content_maps) / sizeof(mcc_content_maps[0])))

static char lower_case(char character)
{
	return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static int same_name(char const *first, char const *second)
{
	while (*first && *second && lower_case(*first) == lower_case(*second))
	{
		first++;
		second++;
	}
	return !*first && !*second;
}

static struct mcc_content_map const *mcc_content_map_find(char const *stem)
{
	int index;

	if (!stem)
		return NULL;
	for (index = 0; index < MCC_CONTENT_MAP_COUNT; index++)
	{
		if (same_name(stem, mcc_content_maps[index].stem))
			return &mcc_content_maps[index];
	}
	return NULL;
}

int mcc_content_stem_kind(char const *stem)
{
	struct mcc_content_map const *map = mcc_content_map_find(stem);

	return map ? map->kind : _mcc_content_none;
}

int mcc_content_name_stem(char const *name, char *stem)
{
	char const *last = name;
	char const *cursor;
	struct mcc_content_map const *map;
	unsigned long prefix_length = sizeof(MCC_CONTENT_NAME_PREFIX) - 1;
	unsigned long index;

	stem[0] = 0;
	if (!name)
		return _mcc_content_none;
	/* (the level name's last part, as the cache file loader takes it) */
	for (cursor = name; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			last = cursor + 1;
	}
	for (index = 0; index < prefix_length; index++)
	{
		if (lower_case(last[index]) != MCC_CONTENT_NAME_PREFIX[index])
			return _mcc_content_none;
	}
	map = mcc_content_map_find(last + prefix_length);
	if (!map)
		return _mcc_content_none;
	snprintf(stem, MCC_CONTENT_STEM_BYTES, "%s", map->stem);
	return map->kind;
}

static int menu_stem(char const *stem)
{
	return stem && same_name(stem, "ui");
}

int mcc_content_name_is_menu(char const *name)
{
	char const *last = name;
	char const *cursor;

	if (!name)
		return 0;
	for (cursor = name; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			last = cursor + 1;
	}
	return menu_stem(last);
}

int mcc_content_menu_source_choose(char const *setting, int xbox_ui, int mcc_ui, char const **reason)
{
	char const *why = "the Xbox's ui.map";
	int source = _mcc_content_menu_source_xbox;

	if (!setting)
		setting = "";
	if (same_name(setting, "xbox"))
		why = xbox_ui ? "paths.menu_source is xbox" : "paths.menu_source is xbox, but there is no ui.map in the maps folder";
	else if (same_name(setting, "mcc"))
	{
		if (mcc_ui)
		{
			source = _mcc_content_menu_source_mcc;
			why = "paths.menu_source is mcc";
		}
		else
			why = xbox_ui ? "paths.menu_source is mcc, but the Halo content folder has no halo1\\maps\\ui.map" :
				"no ui.map anywhere";
	}
	else if (xbox_ui)
		why = "the Xbox's ui.map, which comes first";
	else if (mcc_ui)
	{
		source = _mcc_content_menu_source_mcc;
		why = "no Xbox ui.map in the maps folder";
	}
	else
		why = "no ui.map anywhere";
	if (reason)
		*reason = why;
	return source;
}

int mcc_content_map_path(char const *stem, char *path, unsigned long path_size)
{
	struct mcc_content_map const *map = mcc_content_map_find(stem);
	int written;

	if (menu_stem(stem) && path && path_size)
	{
		written = snprintf(path, path_size, "%sui.map", MCC_CONTENT_MAPS_DIRECTORY);
		return written > 0 && (unsigned long)written < path_size;
	}
	if (!map || !path || !path_size)
		return 0;
	written = snprintf(path, path_size, "%s%s.map", MCC_CONTENT_MAPS_DIRECTORY, map->stem);
	return written > 0 && (unsigned long)written < path_size;
}

char const *mcc_content_title(char const *stem)
{
	struct mcc_content_map const *map = mcc_content_map_find(stem);

	return map ? map->title : NULL;
}

int mcc_content_picture_frame(char const *stem)
{
	struct mcc_content_map const *map = mcc_content_map_find(stem);

	return map ? map->frame : -1;
}

char const *mcc_content_campaign_stem(int index)
{
	return index >= 0 && index < MCC_CONTENT_CAMPAIGN_LEVELS ? mcc_content_maps[index].stem : NULL;
}

int mcc_content_campaign_index(char const *stem)
{
	struct mcc_content_map const *map = mcc_content_map_find(stem);

	return map && map->kind == _mcc_content_campaign ? (int)(map - mcc_content_maps) : -1;
}

char const *mcc_content_multiplayer_stem(int index)
{
	return index >= 0 && MCC_CONTENT_CAMPAIGN_LEVELS + index < MCC_CONTENT_MAP_COUNT ?
		mcc_content_maps[MCC_CONTENT_CAMPAIGN_LEVELS + index].stem : NULL;
}
