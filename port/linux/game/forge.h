/*
FORGE.H

Forge (forge.c): a sandbox in a multiplayer map, Halo 3's way. The Forge
lobby (menu_functions.c, halo3/forge_lobby.xml) starts a local game in
Forge mode; in it the player is the monitor, flying, and spawns, grabs,
turns and deletes the map's objects through the object browser
(halo3/forge.xml), within a budget, or plays the map as a Spartan, D-pad up
flipping between them. Nothing changes until it is started (or the
console's "forge" command asks for the monitor).
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

/* ---------- structures */

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
the game started as "main menu switch to solo game" starts it */
void forge_game_start(
	short map_index);

/* whether a Forge game is played (the session), and whether its player is
the monitor now */
boolean forge_game_active(
	void);
boolean forge_monitor_active(
	void);

/* (the Forge menu) monitor and Spartan flipped, as D-pad up does: asked
here, done at the next forge_update */
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
