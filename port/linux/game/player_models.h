/*
PLAYER_MODELS.H

Player models from Halo Custom Edition maps (player_models.c): the player's
own Custom Edition maps looked through for multiplayer bipeds other than
the stock cyborg, each one a scene pack built on the player's machine from
the player's map (scene_pack_ce.c), chosen by profile (CUSTOMIZATION's
MODEL, the settings panel's Player model), shown in CUSTOMIZATION's preview
and played as in local games: offline multiplayer (split screen, bots) and
Forge. Never in a network game, which every machine would have to agree on:
the stock Spartan there.
*/

#ifndef __PLAYER_MODELS_H
#define __PLAYER_MODELS_H

#include "cseries.h"

enum
{
	MAXIMUM_PLAYER_MODELS = 23,
	PLAYER_MODEL_NAME_SIZE = 48,
	PLAYER_MODEL_PACK_SIZE = 32,
};

/* a model found */
struct player_model
{
	/* its scene pack's name (pm_<hash>), its map's file name (no ".map")
	and identity, its biped's tag name and the name shown */
	char pack[PLAYER_MODEL_PACK_SIZE];
	char map[32];
	unsigned long file_size;
	unsigned long checksum;
	char biped[128];
	char name[PLAYER_MODEL_NAME_SIZE];
};

/* the maps folder looked through (again, if again; else once a session):
the game's thread's, at the top of a frame or in the menus */
void player_models_scan(
	boolean again);

/* the models found (the stock Spartan is none of them), each by index */
short player_models_count(
	void);
boolean player_models_get(
	short index,
	struct player_model *model);

/* a profile's choice (its index in the list: NONE, the stock Spartan), and
the choice made for it, kept in t:\player_models.txt (blam.sav has no room
for it); a profile index the game's (saved_game_files.c) */
short player_models_profile_choice(
	long profile_index);
boolean player_models_profile_choose(
	long profile_index,
	short model_index);

/* a model's scene pack's name, its recipe registered with the packs
(scene_packs_recipe_register): NULL for NONE or a model not found */
char const *player_models_pack(
	short model_index);

/* (cache_files.c) a map's tags loaded: the model of the first local
player's profile loaded with them, in a local multiplayer game */
void player_models_map_loaded(
	long scenario_index);

/* (players.c player_spawn) the unit a player spawns as in a game engine's
game: the chosen model's biped for the first local player in a local game
whose pack is loaded, else unit_definition_index */
long player_models_unit(
	long player_index,
	long unit_definition_index);

/* (the settings panel, port/vita/host/vita_settings.c: its own thread) the
models as the panel lists them, the stock Spartan first (index 0), and the
first local player's profile's choice: read, and changed (taken by the game
at the top of a frame) */
int player_models_panel_count(void);
const char *player_models_panel_name(int index);
int player_models_panel_chosen(void);
void player_models_panel_choose(int index);

/* (main.c, the top of a frame: scene_packs_update) the scan made and the
panel's choice taken */
void player_models_update(
	void);

/* a console command that starts with "models" (hs.c): "list", "choose
<index>", "scan". TRUE if understood */
boolean player_models_command(
	char const *arguments);

#endif
