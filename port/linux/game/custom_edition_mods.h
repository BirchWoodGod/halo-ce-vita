/*
CUSTOM_EDITION_MODS.H

(port) Halo: The Master Chief Collection campaign mods in the native builds
(docs/mcc_cea_maps.md). A player copies a mod's folder, as Steam keeps it
(its maps\*.map, Halo: CE Anniversary caches, MCC's version 13, and its
CampaignInfo.json, campaign\*.json and assets), into the data root's mods
folder: ux0:data/haloce-vita/mods/<mod>/ on the Vita. Its maps are listed
in the campaign's level list after the Custom Edition campaign maps, by
their campaign files' titles, in the order its CampaignInfo.json plays
them, and played alone, offline, as the stock levels are; won, one goes on
to the next its campaign lists. Their level name is mods\<mod>_<map>
(ruby_a10 for mods\ruby\maps\a10.map), which no stock level, Custom Edition
campaign map (custom_maps\) or network game has: the cache file loader
finds the map by the last part, and loads a version 13 cache only from
here, only for a game of this machine's own (custom_edition_cache.c).
Nothing here is offered to other players: map sharing sends none, a
network game hosts none, and no download is ever taken for one (map
sharing takes Xbox and Custom Edition caches only).
*/

#ifndef __CUSTOM_EDITION_MODS_H
#define __CUSTOM_EDITION_MODS_H

/* ---------- constants */

/* a mod map's level name is this and its name (CUSTOM_EDITION_MODS_*) */
#define CUSTOM_EDITION_MODS_LEVEL_NAME_PREFIX "mods\\"
/* the mods folder, in the data root (the file layer's d:\) */
#define CUSTOM_EDITION_MODS_DIRECTORY "d:\\mods\\"
/* the most mods and maps of them listed */
#define CUSTOM_EDITION_MODS_MAXIMUM 16
#define CUSTOM_EDITION_MODS_MAXIMUM_MAPS 64
/* a mod map's name (<mod>_<map>) is at most as long as a Custom Edition
map's (custom_edition_maps.c) */
#define CUSTOM_EDITION_MODS_MAXIMUM_NAME_LENGTH 25
/* each text's room, its terminator included */
#define CUSTOM_EDITION_MODS_TITLE_BYTES 64
#define CUSTOM_EDITION_MODS_DESCRIPTION_BYTES 256

/* ---------- prototypes/CUSTOM_EDITION_MODS.C */

/* Looks for the mods' maps anew (the campaign's level list opens, or a mod
map is asked for before any was looked for): returns how many there are,
in the order the list shows them (each mod's in its campaign's order, the
mods in the order of their folders' names). */
short custom_edition_mods_look_for(
	void);
/* Forgets what was found: the next question looks again. */
void custom_edition_mods_look_again(
	void);

/* The map at `map_index` of those found: its name (ruby_a10, the cache file
loader's), its title and its description for the menus (its campaign
file's, and its mod's title; empty when there are none), and the path of
its picture, the Windows bitmap <map>.bmp beside it (NULL without one).
NULL past the maps. */
char const *custom_edition_mods_map_name(
	short map_index);
char const *custom_edition_mods_map_title(
	short map_index);
char const *custom_edition_mods_map_description(
	short map_index);
char const *custom_edition_mods_map_picture_path(
	short map_index);
/* the title of the mod of the map at `map_index` (its CampaignInfo.json's;
empty without one), NULL past the maps */
char const *custom_edition_mods_map_mod_title(
	short map_index);

/* The file of the mod map `name` (a mod map's name, or its level name):
TRUE with its path in `path` (`path_size` characters). The mods are looked
for when they have not been. */
boolean custom_edition_mods_map_path(
	char const *name,
	char *path,
	long path_size);

/* Whether `level_name` is a mod map's level name (mods\ and a name map
sharing would take): no file is looked at. */
boolean custom_edition_mods_level_name(
	char const *level_name);

/* The level name of the map a mod map's campaign goes on to when
`level_name` is won: the next its campaign lists (or, for a mod without a
CampaignInfo.json, the next stock level's name of the same mod), or NULL
for its last and any other level. */
char const *custom_edition_mods_next_level(
	char const *level_name);

#endif
