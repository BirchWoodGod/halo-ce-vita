/*
MCC_CONTENT.H

(port) The Halo content folder (paths.content, HALO_CONTENT_ROOT;
docs/mcc_cea_maps.md): the player's own Halo: The Master Chief Collection
folder, the one holding halo1 (halo2, halo3 ... beside it), which the game
names d:\mcc\ (port/linux/src/xbox_files.c finds it: the setting, the PS
Vita's ux0:data/haloce-vita/mcc, Steam's libraries). There is no switch:
when it is there, everything of it a loader of this build plays is listed
in the usual lists beside the Xbox's levels, each map file loaded by the
loader its own header asks for (content_library_loader_for_header, a
table: Halo CE's for now, the other games' loaders added there as they
come); without it, the Xbox maps alone, as before.

Halo CE's (halo1\maps):
- its ten campaign maps as the campaign's MCC levels, mods\mcc_a10 ... (the
  mods' machinery, custom_edition_mods.c: played alone, offline, saved in
  the custom campaign maps' save, savegame_custom.bin, never the campaign's
  savegame.bin nor the profile's progress); the Halo 3 campaign lobby's
  missions are these where the folder has them, else the Xbox's;
- its multiplayer maps (the Xbox's thirteen and Halo PC's six more) as the
  multiplayer maps mcc_bloodgulch ... (levels\test\mcc_bloodgulch\
  mcc_bloodgulch, custom_edition_maps.c), played offline with bots, split
  screen, or hosted and joined over the network, where every machine must
  have the same file (its identity, as a custom map's: never shared, never
  downloaded).

A name of these (MCC_CONTENT_NAME_PREFIX and a stock map's name) is the
content folder's alone: it is only ever looked for there, never in the maps
folder, and map sharing neither sends nor takes one. The folder is read,
never written (xbox_files.c refuses every write there). Halo CE's sounds are
in FMOD banks, not in its maps: the sound effects play from the IMA ADPCM
bank (mcc_sound.h), the dialogue and music (CELT banks) are silent in this
version.

This unit is plain C (the names, lists and the loaders' table alone), for
port/vita/tests/mcc_content_test.c.
*/

#ifndef __MCC_CONTENT_H
#define __MCC_CONTENT_H

/* ---------- constants */

/* the Halo content folder and Halo CE's maps folder in it, as the game names
them */
#define MCC_CONTENT_ROOT "d:\\mcc\\"
#define MCC_CONTENT_MAPS_DIRECTORY "d:\\mcc\\halo1\\maps\\"
/* the maps' names: mcc_bloodgulch; the campaign's mod key (mods\mcc_a10) */
#define MCC_CONTENT_NAME_PREFIX "mcc_"
#define MCC_CONTENT_MOD_KEY "mcc"
/* the longest stock map name (boardingaction, gephyrophobia), and a name
with its prefix */
#define MCC_CONTENT_STEM_BYTES 16
#define MCC_CONTENT_NAME_BYTES 24
#define MCC_CONTENT_CAMPAIGN_LEVELS 10

enum mcc_content_kind
{
	_mcc_content_none,
	_mcc_content_campaign,
	_mcc_content_multiplayer,
};

/* the loaders of the content folder's map files (content_library_games) */
enum content_library_loader
{
	/* a game folder this build has no loader for yet: found, said, not listed */
	_content_library_loader_none,
	/* Halo CE's (halo1): Halo: CE Anniversary caches, MCC's version 13
	(cache_file_formats.c, custom_edition_cache.c) */
	_content_library_loader_halo1,
	NUMBER_OF_CONTENT_LIBRARY_LOADERS
};

/* a game of the content folder: its folder (its maps in <folder>\maps), its
title, the cache header version of its map files (0: not known to this
build) and the loader that plays them */
struct content_library_game
{
	char const *folder;
	char const *title;
	unsigned long cache_version;
	int loader;
};

/* ---------- prototypes/MCC_CONTENT.C */

/* The content folder's games (`index` from 0 while it gives one), or NULL. */
struct content_library_game const *content_library_game_get(int index);

/* The loader a map file's first `size` bytes (its cache header: 'head'
little-endian, then its version) ask for, and its game's index into
`game_index` (may be NULL; -1 for none): _content_library_loader_none for
a file no game of the table has, or one without a loader yet. */
int content_library_loader_for_header(unsigned char const *header, unsigned long size, int *game_index);

/* What MCC's stock map `stem` ("bloodgulch", any case) is: a campaign map,
a multiplayer map, or none (any other name). */
int mcc_content_stem_kind(char const *stem);

/* When `name` (a level name, its last part taken: levels\test\
mcc_bloodgulch\mcc_bloodgulch, mods\mcc_a10) is one of MCC's
(MCC_CONTENT_NAME_PREFIX and a stock map's name, any case): its stock map's
name in lower case into `stem` (MCC_CONTENT_STEM_BYTES), and its kind; else
_mcc_content_none (and `stem` empty). */
int mcc_content_name_stem(char const *name, char *stem);

/* `stem`'s file in MCC's maps folder (d:\mcc\maps\<stem>.map) into `path`
(`path_size` bytes): FALSE for a name not of a stock map, or too long. */
int mcc_content_map_path(char const *stem, char *path, unsigned long path_size);

/* The stock map's title, as the menus name it ("Blood Gulch", "The Pillar
of Autumn"), or NULL. */
char const *mcc_content_title(char const *stem);

/* The menus' picture frame of the stock map (the Xbox's level pictures:
the multiplayer ones for the Xbox's thirteen, the campaign's for the ten
levels), or -1 for Halo PC's own maps. */
int mcc_content_picture_frame(char const *stem);

/* The campaign's levels in their order (0 to 9: a10 ... d40), and the
multiplayer maps (`index` from 0 while it gives one), or NULL. */
char const *mcc_content_campaign_stem(int index);
int mcc_content_campaign_index(char const *stem);
char const *mcc_content_multiplayer_stem(int index);

#endif
