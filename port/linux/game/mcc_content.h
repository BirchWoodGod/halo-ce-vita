/*
MCC_CONTENT.H

(port) Halo CE content: MCC files (game.content = "mcc", HALO_CONTENT;
docs/mcc_cea_maps.md). The player's own Halo: The Master Chief Collection
halo1 folder, which the game names d:\mcc\ (port/linux/src/xbox_files.c finds
it: paths.mcc, the PS Vita's ux0:data/haloce-vita/mcc/halo1, Steam's
libraries), gives its stock maps as levels of their own beside the Xbox's:

- its ten campaign maps as the campaign's MCC levels, mods\mcc_a10 ... (the
  mods' machinery, custom_edition_mods.c: played alone, offline, saved in
  the custom campaign maps' save, savegame_custom.bin, never the campaign's
  savegame.bin nor the profile's progress);
- its multiplayer maps (the Xbox's thirteen and Halo PC's six more) as the
  multiplayer maps mcc_bloodgulch ... (levels\test\mcc_bloodgulch\
  mcc_bloodgulch, custom_edition_maps.c), played offline with bots, split
  screen, or hosted and joined over the network, where every machine must
  have the same file (its identity, as a custom map's: never shared, never
  downloaded).

A name of these (MCC_CONTENT_NAME_PREFIX and a stock map's name) is MCC's
alone: it is only ever looked for in MCC's maps folder, never the maps
folder, and map sharing neither sends nor takes one. The folder is read,
never written (xbox_files.c refuses every write there). Their sounds are in
FMOD banks, not in the maps: silent in this version.

This unit is plain C (the names and lists alone), for
port/vita/tests/mcc_content_test.c.
*/

#ifndef __MCC_CONTENT_H
#define __MCC_CONTENT_H

/* ---------- constants */

/* MCC's halo1 folder and its maps folder, as the game names them */
#define MCC_CONTENT_ROOT "d:\\mcc\\"
#define MCC_CONTENT_MAPS_DIRECTORY "d:\\mcc\\maps\\"
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

/* ---------- prototypes/MCC_CONTENT.C */

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
