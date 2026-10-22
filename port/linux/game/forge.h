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
	FORGE_PACK_COUNT = 6,
	/* the player spawn points of a layout (forge_spawns.inc: the SPAWNING
	category's), within the objects' budget */
	FORGE_MAXIMUM_SPAWNS = 64,
};

/* the object browser's categories, in its order */
enum
{
	_forge_category_vehicles = 0,
	_forge_category_weapons,
	_forge_category_equipment,
	_forge_category_scenery,
	_forge_category_characters,
	/* (the building blocks the game makes: forge_blocks.c) */
	_forge_category_blocks,
	/* (the player spawn points, Halo 3's: RESPAWN POINT and INITIAL SPAWN,
	no tag of the map's: forge_spawns.inc) */
	_forge_category_spawning,
	NUMBER_OF_FORGE_CATEGORIES
};

/* a player spawn point's kind (the SPAWNING category's items, in its order) */
enum
{
	_forge_spawn_respawn = 0,
	_forge_spawn_initial,
	NUMBER_OF_FORGE_SPAWN_KINDS
};

/* an object's properties (the properties window's rows, in its order; a
character's AI team and brain only for a character): its exact angles
(yaw, pitch, roll: stepped by rotate mode's snapping, 15 degrees with
none) and their reset to none. A player spawn point's rows are the first
four: its team (any, red, blue), its game type in the respawn's row, its
kind (respawn point, initial spawn) in the place at start's, its yaw */
enum
{
	_forge_property_team = 0,
	_forge_property_respawn,
	_forge_property_place_at_start,
	_forge_property_yaw,
	_forge_property_pitch,
	_forge_property_roll,
	_forge_property_reset_rotation,
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
	/* its facing as yaw (from +x, about up), pitch (up from the ground)
	and roll (about forward), whole degrees */
	short yaw;
	short pitch;
	short roll;
	/* a character's: its AI team (forge_files.h FORGE_AI_TEAM_*: whom it
	fights) and its words, and whether its brain is on */
	boolean ai;
	short ai_team;
	char ai_team_name[32];
	boolean ai_on;
	/* a player spawn point's (forge_spawns.inc): its kind
	(_forge_spawn_*) and its game type, named */
	boolean spawn;
	short spawn_kind;
	char game_type_name[32];
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
	/* a character's side, the browser's AI category's groups ("Flood"),
	translated; else empty */
	char group[24];
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
/* whether the map's file is in the maps folder and can be played: its
map_checks.h state (_map_check_playable, _missing, _unusable) and, when it
cannot, a short reason; looked at once a run (map_checks.c) */
short forge_map_state(
	short map_index,
	char *why,
	long why_size);
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

/* (the Forge menu's AI row) the placed characters' AI as the row shows
it: all on, all off, or some of each (with none placed, the setting a new
one gets) */
enum
{
	_forge_ai_all_on,
	_forge_ai_all_off,
	_forge_ai_all_mixed,
};

/* (the Forge menu's AI row) every character's AI set at the next
forge_update: off when the row shows ON or MIXED, on when it shows OFF;
the characters placed after it get the same, those the game spawns too
(a carrier's infection forms) */
void forge_ai_all_request(
	void);
short forge_ai_all_state(
	void);
/* (the Forge menu's CLEAR DEAD AI, and CLEAR ALL AI after its question)
the layout's dead characters (all of them, alive or dead) taken out of the
layout and the world at the next forge_update: they come back no more with
a new round, nor in a map variant saved after; what the game spawned (a
carrier's infection forms) is left */
enum
{
	_forge_clear_ai_none,
	_forge_clear_ai_dead,
	_forge_clear_ai_all,
};
/* the loaded level's name as Forge says it (the Forge menu's title) */
void forge_level_name(
	char *name,
	short size);
void forge_clear_ai_request(
	boolean all);
/* (the settings panel's Forge and File Share page, vita_settings.c, of
another ABI: plain types only) the layout's characters alive and dead, and
whether a Forge game is played (its rows shown); the clearing asked */
int forge_panel_ai_counts(
	int *alive,
	int *dead);
void forge_panel_clear_ai(
	int all);

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

/* the budget: objects placed (the layout's, gone in play or not), AI
characters alive, and dead (they still hold their places: back with the
next round, until CLEAR DEAD AI; dead_ai may be NULL) */
void forge_budget_get(
	short *objects,
	short *ai,
	short *dead_ai);

/* ---------- prototypes/FORGE_NET.INC (network Forge, in forge.c: several
players forging one map, the host authoritative; forge_net_protocol.h) */

struct network_game_server;
struct network_game_server_client_machine;
struct network_game_client;
struct game_variant;

/* the Forge game's variant (free for all slayer, no score that can be
reached, no time limit, "Forge"), as forge_game_start plays it */
void forge_variant_build(
	struct game_variant *variant);

/* (the Forge lobby's live game, the settings panel, the network test) the
game this machine hosts is a Forge game of the map at map_path, with that
map variant of it (NULL, "" for none): joiners are asked to play it, and
those that cannot are refused from it; FALSE with why if it cannot be */
boolean forge_net_host_begin(
	char const *map_path,
	char const *variant_file,
	char *why,
	int why_size);
/* ... a Forge game no more (the lobby closed, another game chosen) */
void forge_net_host_end(
	void);
/* (the multiplayer lobby's MAP: a saved map variant played in a game of
the lobby's game type, not Forge) the game this machine hosts plays the
map variant variant_file of the map at map_path: its layout placed as the
game starts, joiners asked to play it as a network Forge game's are (one
that cannot is refused from it) and given the layout as Forge gives it,
nothing edited, no monitor. Vitas only (WHO CAN JOIN: Vitas). FALSE with
why if it cannot be; forge_net_host_end ends it */
boolean forge_play_host_begin(
	char const *map_path,
	char const *variant_file,
	char *why,
	int why_size);
/* ... the local game (split screen) about to start on the map at map_path
plays that map variant: placed once the player has spawned, if the game's
map is that one. FALSE with why if the variant could not be read */
boolean forge_play_local_request(
	char const *map_path,
	char const *variant_file,
	char *why,
	int why_size);
/* whether the game played now plays a map variant (forge_game_active: a
Forge game, the monitor's) */
boolean forge_variant_playing(
	void);

/* whether this machine hosts a Forge game, or plays the host's Forge game */
boolean forge_net_hosting(
	void);
boolean forge_net_client_session(
	void);
/* (network_server_manager.c, the countdown at 0; the game's lobby) whether
a joiner still building the host's packs holds the game's start: its
machine's name and how far */
boolean forge_net_server_holds_start(
	struct network_game_server *server,
	char *name,
	int name_size,
	short *percent);

/* (network_server_message_handler.c, network_client_message_handler.c)
the lobby's messages: a joiner's answer, the host's hello or refusal */
void forge_net_server_handle_lobby_message(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	word *message,
	short message_size);
void forge_net_client_handle_lobby_message(
	struct network_game_client *client,
	word *message,
	short message_size);

/* (network_distributed.c) a game's messages: a client's requests (its
machine), the host's updates, palette and layout */
void forge_net_handle_requests(
	long machine_index,
	void const *entries,
	short count);
void forge_net_handle_updates(
	void const *entries,
	short count);
void forge_net_handle_palette(
	void const *entries,
	short count);
void forge_net_handle_layout(
	void const *entries,
	short count);

/* (main.c, each frame) the lobby's hellos and answers */
void forge_net_frame(
	void);

/* (network_objects.c, a client) a host's tag index as this machine's (a
pack's tags by path), NONE when this machine has no such tag; and whether
it holds the object (its place its own while held) */
long forge_net_client_tag(
	long host_tag_index);
/* (network_objects.c, forge.c) whether an object is this machine's own
343 Guilty Spark shown for another player's monitor: never the netcode's */
boolean forge_net_local_object(
	long object_index);
boolean forge_net_client_holds(
	long object_index);

/* ---------- prototypes/FORGE_SPAWNS.INC (in forge.c) */

struct player_starting_location;

/* (players.c, player_spawn: a game's host, a local game) where the player
spawns in a Forge game or a map variant played that has player spawn
points of its team and game type: one of them chosen by the engine's own
rating (game_engine_get_starting_location_rating: the game type's, enemies
and vehicles near, teammates), the round's initial spawns first for its
first spawn. _forge_spawn_location_found, filled; _map when none fits (the
map's own spawns then); _wait when some fit but each is blocked (an enemy
on it: nothing spawned this tick, the map's after a few seconds) */
enum
{
	_forge_spawn_location_map = 0,
	_forge_spawn_location_found,
	_forge_spawn_location_wait,
};
short forge_spawn_location(
	long player_index,
	struct player_starting_location *location);

#endif
