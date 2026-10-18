/*
MOD_MANAGER.H

(port) The mod manager: the player's custom content - the maps folder's
maps that are not the Xbox's own (modded Xbox maps, Halo Custom Edition
maps, OpenSauce .yelo maps) and the mods folder's Master Chief Collection
mods - listed with what each is, its size, where it came from, the modes it
plays in, whether it passed the game's own checks (and why not) and what it
needs; turned off and on (an off one stays in its folder but leaves every
map list, the bots' playlist and map sharing), deleted after a question.

The Halo 3 menus' MOD MANAGER screen (port/vita/app0/menus/halo3/mods.xml,
menu_functions.c) and the Vita settings panel's Mod manager page
(port/vita/host/vita_settings.c, through mod_manager_share.h) both use it.
Nothing is downloaded here.
*/

#ifndef __MOD_MANAGER_H
#define __MOD_MANAGER_H

/* ---------- headers */

#include "../src/mod_manager_share.h"

/* ---------- constants */

/* where map sharing's received maps are written down, in the data root */
#define MOD_MANAGER_RECEIVED_FILE "d:\\map_share_received.txt"

/* ---------- prototypes/MOD_MANAGER.C */

/* Looks through the maps folder and the mods folder anew, with the cache
file loader's and the mods folder's own checks (custom_edition_cache.c,
custom_edition_mods.c); returns how many entries there are: the maps in the
order of their names, then the mods in the order of their folders'. */
short mod_manager_scan(
	void);

/* the entry at `index` of the last look, or NULL past them */
struct halo_mod_entry const *mod_manager_entry(
	short index);
short mod_manager_count(
	void);

/* the space the entries take and the memory card's free space, in KB */
void mod_manager_totals(
	int *used_kilobytes,
	int *free_kilobytes);

/* The entry of the file `file` (a map's name and extension, a mod's folder)
of the kind `kind` (a map's or HALO_MOD_KIND_MCC_MOD), or NONE. */
short mod_manager_find(
	char const *file,
	int kind);

/* Turns the entry at `index` on or off (HALO_MAPS_DISABLED or
HALO_MODS_DISABLED, set as the settings panel sets a row, which keeps it in
settings.txt on the Vita); FALSE for an entry that cannot be turned off. */
boolean mod_manager_set_enabled(
	short index,
	boolean enabled);

/* Deletes the file `file` of the kind `kind` (a map of the maps folder,
with its picture and description, or a mod's folder with everything in
it), only inside our maps and mods folders, never following a link, never
one of the Xbox's own maps or a resource map, never one loaded now: a
HALO_MODS_ANSWER_* (DELETED, IN_USE or FAILED). Looks again afterwards. */
int mod_manager_delete(
	char const *file,
	int kind);

/* (map_share.c) the map `name` came through map sharing, as the file of
`extension` (".map" or ".yelo"): written down for the mod manager's "where
it came from" */
void mod_manager_map_received(
	char const *name,
	char const *extension);

/* (each frame, the main loop) the settings panel's requests answered */
void mod_manager_update(
	boolean main_menu_loaded);

/* What a kind, a source and the modes are called, for the player (T()'s
language). */
char const *mod_manager_kind_name(
	int kind);
char const *mod_manager_source_name(
	int source);
void mod_manager_modes_text(
	int modes,
	char *text,
	int size);

#endif
