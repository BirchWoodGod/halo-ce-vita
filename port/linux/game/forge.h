/*
FORGE.H

Forge (forge.c): a sandbox in a multiplayer map, Halo 3's way. The Forge
lobby (menu_functions.c, halo3/forge_lobby.xml) starts a local game in
Forge mode; in it the player is the monitor, flying, and spawns, grabs,
turns and deletes the map's objects through the object browser
(halo3/forge.xml), within a budget, or plays the map as a Spartan,
Select + D-pad up (Back + the right stick's click) flipping between them.
Nothing changes until it is started (or the console's "forge" command asks
for the monitor).
*/

#ifndef __FORGE_H
#define __FORGE_H

#include "cseries.h"
#include "real_math.h"

/* ---------- constants */

enum
{
	/* the budget of a session (the owner's, Oct 18 2026): objects placed
	(AI characters' units among them), AI characters */
	FORGE_MAXIMUM_OBJECTS = 256,
	FORGE_MAXIMUM_AI = 32,
	/* a readable name's bytes (UTF-8) */
	FORGE_NAME_SIZE = 64,
	/* the scene packs Forge offers (forge.c forge_packs) */
	FORGE_PACK_COUNT = 3,
};

/* the object browser's categories, in its order */
enum
{
	_forge_category_vehicles = 0,
	_forge_category_weapons,
	_forge_category_equipment,
	_forge_category_scenery,
	_forge_category_characters,
	NUMBER_OF_FORGE_CATEGORIES
};

/* an object's properties (the properties window's rows, in its order; a
character's AI team and brain only for a character) */
enum
{
	_forge_property_team = 0,
	_forge_property_respawn,
	_forge_property_place_at_start,
	_forge_property_ai_team,
	_forge_property_ai_brain,
	NUMBER_OF_FORGE_PROPERTIES
};

/* ---------- structures */

/* the properties of the object held */
struct forge_properties
{
	char name[FORGE_NAME_SIZE];
	/* NONE neutral, 0 red, 1 blue */
	short team;
	/* 0: never */
	short respawn_seconds;
	boolean place_at_start;
	/* a character's: its AI team (forge_files.h FORGE_AI_TEAM_*: whom it
	fights) and its words, and whether its brain is on */
	boolean ai;
	short ai_team;
	char ai_team_name[32];
	boolean ai_on;
};

/* what the monitor's controls ask of the flying camera this frame */
struct forge_camera_input
{
	real_euler_angles2d facing_delta;
	/* along the camera's yaw: i forward, j left, k up */
	real_vector3d translation;
};

/* an object the browser offers */
struct forge_item
{
	/* its readable name, in the language chosen (UTF-8) */
	char name[FORGE_NAME_SIZE];
	/* how many of it this session has placed that are still there */
	short placed;
};

/* ---------- prototypes/FORGE.C */

/* a console command that starts with "forge" (hs.c, as typed, init.txt,
HALO_TEST_COMMANDS): what follows the word. TRUE if it was understood */
boolean forge_command(
	char const *arguments);

/* (director.c, each frame before the cameras update) whether this local
player is the monitor: its flying camera then moves by input, and the
player's own controls are not read */
boolean forge_camera_input(
	short local_player_index,
	real seconds,
	struct forge_camera_input *input);

/* (main.c, each frame once the cameras have updated, before the tick
starts) the monitor's buttons and the object it holds */
void forge_update(
	void);

/* (cache_files.c) a map's tags loaded: a Forge game asked for on it starts,
anything of another map's is forgotten */
void forge_map_loaded(
	char const *scenario_name);

/* (hud.c, as the HUD draws) the monitor's HUD: the map, the budget, the
crosshair and what it is on, the notices */
void forge_draw(
	short local_player_index);

/* (director.c) puts a local player's director on the flying camera at
position, looking along forward (fly), or back on the player's own */
void director_forge_camera(
	short local_player_index,
	boolean fly,
	real_point3d const *position,
	real_vector3d const *forward);

/* the lobby's maps (the Xbox's multiplayer maps, in ui.map's picture
order: mp_map_grafix): how many, each one's scenario path and English
name, whether its file is in the maps folder */
short forge_map_count(
	void);
char const *forge_map_path(
	short map_index);
char const *forge_map_name(
	short map_index);
boolean forge_map_available(
	short map_index);

/* (the lobby's START FORGE) a local game of the map, in Forge mode: the
game variant (free for all, no score or time limit) and the map set, and
the game started as "main menu switch to solo game" starts it; with a map
variant's file of the map (variant_file, else NULL), that read and checked
first, and placed as the session starts. FALSE, with why, if the variant
could not be read: nothing is started */
boolean forge_game_start(
	short map_index,
	char const *variant_file,
	char *why,
	int why_size);

/* a lobby map's short name (bloodgulch: its map variants' folder), and the
map of a short name (NONE if none) */
char const *forge_map_short(
	short map_index);
short forge_map_of_short(
	char const *map_name);

/* (the Forge menu's SAVE MAP VARIANT, its name typed) the session's
objects saved as a map variant titled title, at the next forge_update; the
session's own variant's file replaced when the title is its. What came of
it is said in the Forge menu (forge_menu_status) */
void forge_save_request(
	char const *title,
	char const *author);

/* (the settings panel's Forge and File Share page, vita_settings.c, of
another ABI: plain types only) the lobby's maps: how many, each one's name
(English), short name and whether its file is in the maps folder */
int forge_panel_map_count(
	void);
char const *forge_panel_map_name(
	int map_index);
char const *forge_panel_map_short(
	int map_index);
int forge_panel_map_available(
	int map_index);

/* the scene packs Forge offers (other maps' objects, built on the machine:
scene_packs.h), in their order: how many, each one's name (translated) and
description, whether the lobby brings it (the vehicles' are on by default),
and its state now (scene_packs.h's _scene_pack_state_*: building, with how
far; failed, with why), as built for the lobby */
short forge_pack_count(
	void);
char const *forge_pack_label(
	short pack);
char const *forge_pack_description(
	short pack);
boolean forge_pack_enabled(
	short pack);
void forge_pack_set(
	short pack,
	boolean enabled);
short forge_pack_status(
	short pack,
	long *progress,
	char *why,
	int why_size);

/* (the lobby, as it opens and each frame) the packs it brings built in the
background, if they are not (once each) */
void forge_packs_prebuild(
	void);

/* the first pack the lobby brings that is still being built (its label and
how far), FALSE if none is */
boolean forge_packs_building(
	char *label,
	int size,
	long *progress);

/* the map variant the session plays (loaded or last saved), "" for none */
char const *forge_variant_title(
	void);

/* (the Forge menu's UNDO and REDO) the last step undone or the next redone,
at the next forge_update; how many there are of each */
void forge_undo_request(
	boolean redo);
void forge_undo_available(
	short *undo,
	short *redo);

/* (the Forge menu's SNAP) snapping flipped at the next forge_update, and
whether it is on: the object held on a grid and turned in steps */
void forge_snap_toggle_request(
	void);
boolean forge_snap_enabled(
	void);

/* (the properties window, Square while holding an object) a property of
the object held stepped (step 1 or -1) at the next forge_update; its
properties (FALSE: none held, or one of the map's own) */
void forge_property_request(
	short property,
	short step);
boolean forge_properties_get(
	struct forge_properties *properties);

/* (the Forge menu's NEW ROUND, Halo 3's Restart Round) the round started
over, at the next forge_update with no menu over the game: the map's own
objects, the players and the scores as the engine resets a map
(main_reset_map), and the layout placed again where the editor put it (the
session's, saved or not: nothing is written), its destroyed vehicles and
dead characters whole again */
void forge_new_round_request(
	void);

/* (the Forge menu's AI row) every placed character's brain flipped (all
off if any is on, else all on), at the next forge_update; whether any is on
(TRUE when none is placed) */
void forge_ai_all_request(
	void);
boolean forge_ai_all_on(
	void);

/* (main.c, a map reset: main_reset_map) before the game's state goes, the
layout of a Forge game taken; after it has started again, placed again
once the player has spawned */
void forge_map_resetting(
	void);
void forge_map_reset(
	void);

/* what the last thing asked from the Forge menu came to (a save, an undo,
snapping), "" for nothing yet */
void forge_menu_status(
	char *status,
	int size);

/* whether a Forge game is played (the session), and whether its player is
the monitor now */
boolean forge_game_active(
	void);
boolean forge_monitor_active(
	void);

/* (the Forge menu) monitor and Spartan flipped, as Select + D-pad up does:
asked here, done at the next forge_update */
void forge_toggle_request(
	void);

/* (the browser's B and Square) the menus open over the game closed, at the
next forge_update */
void forge_menus_close_request(
	void);

/* (the Forge menu's LEAVE FORGE, asked) the main menu, with nothing saved,
once the button that asked is let go */
void forge_quit_request(
	void);

/* the object browser's lists: a category's items (its readable name, how
many it has), each item's name and placed count (FALSE past the end), and
an item chosen: spawned in the monitor's hand at the next forge_update */
char const *forge_category_name(
	short category);
short forge_category_item_count(
	short category);
boolean forge_category_item(
	short category,
	short item_index,
	struct forge_item *item);
void forge_browser_choose(
	short category,
	short item_index);

/* the budget: objects placed (still there) and AI characters alive */
void forge_budget_get(
	short *objects,
	short *ai);

#endif
