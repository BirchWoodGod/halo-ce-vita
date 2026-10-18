/*
MOD_MANAGER_SHARE.H

The mod manager's inventory as the Vita settings panel sees it
(port/vita/host/vita_settings.c, its Mod manager page; the game's side:
port/linux/game/mod_manager.c). The panel asks, the game's main loop looks
through the maps and mods folders with the game's own checks (the cache
file loader's, the mods folder's) and answers. The two share these ints and
this structure, the panel's side being another ABI: chars and plain ints
only.

The panel turns content on and off itself (HALO_MAPS_DISABLED for the maps
folder's maps, HALO_MODS_DISABLED for the mods folder's mods, which it keeps
in settings.txt) and asks the game to delete, so that one unit, the game's,
decides what may be deleted.
*/

#ifndef __HALO_LINUX_MOD_MANAGER_SHARE_H
#define __HALO_LINUX_MOD_MANAGER_SHARE_H

/* the most entries listed: the maps folder's maps, then the mods */
#define HALO_MODS_MAXIMUM 96
#define HALO_MODS_NAME_BYTES 48
#define HALO_MODS_TEXT_BYTES 96

/* halo_mods_request: the panel sets it, the main loop answers in
halo_mods_answer and sets it back to HALO_MODS_REQUEST_NONE */
enum
{
	HALO_MODS_REQUEST_NONE,
	/* look through the folders again: halo_mods_inventory */
	HALO_MODS_REQUEST_SCAN,
	/* delete halo_mods_target (an entry's file: a map's name and extension,
	or a mod's folder) of the kind halo_mods_target_kind, then look again */
	HALO_MODS_REQUEST_DELETE,
};

enum
{
	HALO_MODS_ANSWER_NONE,
	/* looked through: halo_mods_inventory holds what is there */
	HALO_MODS_ANSWER_SCANNED,
	HALO_MODS_ANSWER_DELETED,
	/* the map (or one of the mod's) is the one loaded: not deleted */
	HALO_MODS_ANSWER_IN_USE,
	/* no such entry, a name this build does not delete, or a file that
	could not be deleted */
	HALO_MODS_ANSWER_FAILED,
};

/* what an entry is */
enum
{
	/* an Xbox cache: a modded or newly built Xbox multiplayer map */
	HALO_MOD_KIND_XBOX_MAP,
	/* a Halo Custom Edition cache of a multiplayer scenario */
	HALO_MOD_KIND_CE_MAP,
	/* a Halo Custom Edition cache of a solo scenario (a campaign map) */
	HALO_MOD_KIND_CE_CAMPAIGN,
	/* an OpenSauce .yelo map */
	HALO_MOD_KIND_OPENSAUCE,
	/* a folder of the mods folder: a Master Chief Collection mod */
	HALO_MOD_KIND_MCC_MOD,
	/* a file of the maps folder that is none of those */
	HALO_MOD_KIND_UNKNOWN,
};

/* where it came from */
enum
{
	/* copied in by hand */
	HALO_MOD_SOURCE_HAND,
	/* received from a host through map sharing (map_share.c) */
	HALO_MOD_SOURCE_MAP_SHARE,
};

/* the modes it plays in (flags) */
enum
{
	HALO_MOD_MODE_MULTIPLAYER = 1 << 0,
	HALO_MOD_MODE_CAMPAIGN = 1 << 1,
	HALO_MOD_MODE_COOP = 1 << 2,
	HALO_MOD_MODE_FORGE = 1 << 3,
};

struct halo_mod_entry
{
	/* what HALO_MAPS_DISABLED (a map: its file's name, no extension) or
	HALO_MODS_DISABLED (a mod: its key) names it by */
	char name[HALO_MODS_NAME_BYTES];
	/* its file: a map's name and extension, a mod's folder */
	char file[HALO_MODS_NAME_BYTES];
	/* its name for the player (a mod's CampaignInfo.json title) */
	char title[HALO_MODS_NAME_BYTES];
	int kind;
	int source;
	int modes;
	/* whether it passed the game's checks, and if not, why not */
	int valid;
	int enabled;
	/* whether this build deletes it (a name it takes, in our folders) */
	int deletable;
	int kilobytes;
	/* a mod's maps, and those of them the game takes */
	int maps;
	int maps_valid;
	char why[HALO_MODS_TEXT_BYTES];
	/* what it needs that is not there or not on ("" for nothing) */
	char needs[HALO_MODS_TEXT_BYTES];
};

struct halo_mods_inventory
{
	int count;
	/* the space the entries take, and the memory card's free space */
	int used_kilobytes;
	int free_kilobytes;
	struct halo_mod_entry entries[HALO_MODS_MAXIMUM];
};

extern volatile int halo_mods_request;
extern volatile int halo_mods_answer;
extern volatile int halo_mods_target_kind;
extern char halo_mods_target[HALO_MODS_NAME_BYTES];
extern struct halo_mods_inventory halo_mods_inventory;

#endif
