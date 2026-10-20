/*
FORGE.C

Forge, version 1: a sandbox in a multiplayer map, as Halo 3's Forge (the
design: docs/superpowers/specs/2026-10-18-forge-design.md). Nothing here is
saved or sent over the network yet (phase b), and nothing changes until a
Forge game is started or the console asks for the monitor.

The session: the Forge lobby (halo3/forge_lobby.xml, menu_functions.c)
starts a local game of a multiplayer map in Forge mode (forge_game_start):
free for all slayer with no score that can be reached and no time limit,
the map's own vehicles and weapons, no saved game read or written. When the
map's tags load (forge_map_loaded) the session is that map's, and as soon
as the player has spawned it is the monitor. Another map, or the main menu,
ends it.

The monitor and the Spartan, flipped by a combo (Select + D-pad up on the
Vita, below; D-pad up alone stays the Spartan's zoom): as the monitor
the player's unit stays where it stood, hidden and unhurt, its controls not
read, and the director flies the flying camera (the beta's debug camera,
flying_camera.c) from where the player's eyes were, by the monitor's
controls (forge_camera_input). Flipped back, the Spartan stands where the
monitor was and plays as ever; flipped again, the monitor starts from the
Spartan's eyes. A Spartan that dies respawns as the game has it; the
monitor hides whichever unit the player has.

The monitor's controls, in the Xbox's terms (the Vita's default layout in
brackets, vita_controls.c; the keyboard's in braces, xinput_sdl.c, with
the keys of its own the layout has no room for: forge_keys):
- left stick [left stick] {W A S D}: fly along the ground; right stick
  [right stick] {mouse}: look (the Vita's gyro and the mouse as in play);
- A [Cross] {Space}: fly up; left stick click [D-pad down] {C, left Ctrl}:
  fly down, while held (the Vita's crouch toggle does not latch it here:
  forge_monitor_flying);
- left trigger [L] {right mouse button, G} held: thrust (four times as
  fast) {or Shift}; holding an object, rotate mode (forge_rotate_update):
  the camera stays, the right stick {the mouse; the Vita's gyro if on}
  turns the object about the camera's view - across yaws it (about the
  world's up), up and down pitch it (its top away from the camera as the
  stick is pushed up, whichever way the camera faces), and with the right
  trigger [R] {the left mouse button} held too, across rolls it (its top to
  the right) -, the left stick's up and down {W S, and the wheel at any
  time} push it away and pull it nearer, and White [D-pad right] {Q} steps
  its snapping (Off, 15, 45, 90 degrees: snapped, each flick of the stick,
  or each FORGE_ROTATE_MOUSE_STEP of the mouse, turns it one step, about the
  world's axis nearest the camera's), said on the HUD with its angles;
- right trigger [R] {left mouse button}: grab the object under the
  crosshair, and drop the one held (it falls where it is let go, as the
  game's physics have it: a vehicle or a character settles back on its
  wheels or feet, scenery stays as turned); in rotate mode, roll;
- Y [Triangle] {Delete}: delete the object held, or the one under the
  crosshair;
- X [Square] {E}: the object browser (halo3/forge.xml): the map's objects
  by category, the one chosen spawned in the monitor's hand; holding one,
  its properties (its exact yaw, pitch and roll among them);
- B [Circle] {F}: a copy of the object held or under the crosshair, held;
- White [D-pad right] {Q}: the movement speed (slow, normal, fast); in
  rotate mode, its snapping;
- Black [D-pad left] {X}: the object held (or under the crosshair) set
  upright, facing as it faces;
- Back and the right stick click together [Select + D-pad up, mirroring
  Select + D-pad down for the settings panel] {F1 + Z, or Tab}: the
  Spartan, and back to the monitor. xinput_sdl.c (forge_combo_filter)
  keeps the combo from the game while it is held, so it neither zooms nor
  shows the scoreboard; the click alone is the Spartan's zoom, as in any
  game, and nothing for the monitor;
- Start: the Forge menu (resume, monitor or Spartan, the controls, leave).

The palette is every tag of the loaded map that the browser offers, with a
readable name each (forge_names: our own names for the multiplayer maps'
objects, translated; else the tag's own name made words): vehicles,
weapons (grenades among them; not a vehicle's gun, the flag or the ball),
equipment, scenery (scenery, machines, controls), and characters - the
actor variants that name their unit, which spawn it with a free actor
(ai_attach_free) and fight as the map's own do; a multiplayer map has none.
Only the loaded map's: other maps' objects are the design's later phases.
And blocks (BLOCKS): the building blocks the game makes of the map's own
bitmaps (forge_blocks.c, scene_pack_blocks.c), whose corners SNAP puts on
the grid so that they meet edge to edge.

An object spawned is the game's own: object_placement_data_new and
object_new, as the cheats' object_new spawns a weapon or vehicle
(cheats.c), never deleted on its own (_new_object_never_automatically_delete_bit);
moved with object_set_position, deleted with object_delete. The object
under the crosshair is the nearest whose bounding sphere the camera's line
meets in front of the structure (a weapon on the ground has no collision
model a collision vector would meet). The budget (forge_placed, the layout)
counts the objects this session placed and has not deleted - one gone in
play (picked up, destroyed) is still the layout's, back with the next round
- at most FORGE_MAXIMUM_OBJECTS, and the characters among them, dead or
alive, at most FORGE_MAXIMUM_AI: past either the browser's choice is
refused, said on the screen. Its table is a fixed array, each index checked.
The HUD counts the characters alive, the dead apart ("AI 5/32, 3 back next
round"): a dead one keeps its place (the next round brings it back, and
must have room) until the Forge menu's CLEAR DEAD AI takes the dead out of
the layout - the refusal says so - or CLEAR ALL AI (asked first) every
character, alive or dead (forge_ai_clear: a dead Flood body vanishes, so
nothing is left to aim at and delete). A Flood combat form feigning death
is not dead (it gets back up as the same unit), and what the game spawns
(a carrier's infection forms) is never the layout's nor counted.

A placed character has an AI team and its AI on or off (the properties
window; forge_ai_apply: the engine's unit and actor teams, actor_braindead),
and the Forge menu's ALL AI sets every one (its row ON, OFF or MIXED: off
from ON or MIXED, on from OFF); the setting stays for the characters placed
after it and, in a Forge game, for those the game spawns (a carrier's
infection forms). A character with its AI off is held as it stands every
frame (forge_ai_hold): braindead again if anything woke it, facing where
the editor turned it. A character is given its variant's weapon, grenades,
vitality and camouflage (actor_customize_unit) and a free actor, or a swarm
of its own for a swarm's (a Flood infection form). In a Forge game the
humans are the local player's allies, as every campaign script makes them
(the Marine AI pack's marines fight for the player). NEW ROUND (the Forge menu,
forge_new_round_start) resets the map as the engine's map_reset does; the
layout, each object at its placed spot (forge_placed's home: where the
editor put it, not where play took it), is taken as the game's state goes
(forge_map_resetting) and placed again once the player has spawned.

Threads: forge_update runs on the main thread after the cameras update and
before the tick thread starts (main.c), so no object is touched while the
tick runs; the browser's choice (a widget's event, which may run beside the
tick) is only noted, and spawned there. forge_draw reads only what
forge_update wrote (the HUD's lines), never an object.

The console's commands (forge_command; the tests use them through
HALO_TEST_COMMANDS):
  forge [on|off]           the monitor, toggled or set (any local game)
  forge list [category]    the palette (each category's count; a category's
                           items, with their tags)
  forge spawn <name>       the first palette tag with name in its readable
                           name or its tag's, spawned in front of the
                           camera and held (the budget as the browser's)
  forge fill <count> <name>  that many spawned and let go on a grid in front
                           of the camera, while the budget allows
  forge grab [#id] | drop  the object under the crosshair (or the layout's
                           of that id) grabbed, the held one dropped
  forge turn <degrees>     the held (or aimed at) object turned about up
  forge property yaw|pitch|roll [-]  the held object's exact angle stepped
                           (back with -), as its properties' rows; "property
                           resetrot" its angles none
  forge distance <wu>      the held object that far from the camera
  forge snap [on|off]      the Forge menu's SNAP (the grid), flipped or set
  forge blocks ray|projectiles  (tests) a projectile's line along the view
                           and what it meets; the projectiles in the world
                           (forge_blocks.c)
  forge delete             the held (or aimed at) object deleted
  forge camera x y z yaw pitch   the monitor's camera put there (degrees)
  forge press <buttons> [frames]  the monitor's buttons pressed as a
                           controller would (a b x y black white lt rt ls rs,
                           and combo: the monitor and the Spartan's, joined
                           by +), for frames frames (2 by default)
  forge stick lx ly rx ry [frames]  the sticks held (-1 to 1)
  forge look yaw pitch     the mouse's look (radians, positive left and up)
                           once, as the keyboard's port gives it
  forge where              the monitor's state in the log
  forge budget             the budget in the log (the AI alive and dead,
                           the HUD's line)
  forge deps <name> [> file]  the tags the first object tag with name in
                           its name needs, every one it references and
                           theirs, with their sizes (the cross-map probe:
                           what another map would have to load to spawn
                           it); with a file, their names written there too
  forge tags <file>        every tag of the loaded map, group and name,
                           written to file
  forge aiteam <team>      the held (or aimed at) character's AI team
                           (default player human covenant flood sentinel)
  forge brain on|off       the held (or aimed at) character's AI on or off
  forge allai [on|off]     every placed character's AI (flipped as the
                           Forge menu's AI row, or set)
  forge clearai dead|all   the layout's dead characters, or all of them,
                           cleared (the Forge menu's CLEAR DEAD AI, CLEAR
                           ALL AI)
  forge ai                 each placed character in the log: where, its
                           team, its brain, alive or dead, its aim and
                           facing, its actor's mode, its weapon, body and
                           shield; the game's own characters counted; the
                           row's state; the shots characters have fired
                           since the first "forge ai"
  forge objects            each object of the layout in the log: where it
                           is, its placed spot, alive, destroyed or gone
  forge round              a new round, as the Forge menu's NEW ROUND
  forge destroy [#id]      (tests) the held or aimed at object (or the
                           layout's of that id) destroyed
                           by the engine's damage (a vehicle's wreck, a
                           character's death), as play would
  forge kill [#id]         (tests) the held or aimed at unit (or the
                           layout's of that id) killed as in play: a
                           vehicle's wreck, a character's body stay
  forge shove [#id] i j k  (tests) the held or aimed at object (or the
                           layout's of that id) given that velocity, as a
                           blast would
Each logs what it did ("forge: ...").
*/

#include "cseries.h"
#include "real_math.h"
#include "ai/ai.h"
#include "ai/ai_script.h"
#include "ai/actor_definitions.h"
#include "ai/actors.h"
#include "cache/cache_files.h"
#include "camera/observer.h"
#include "devices/device_controls.h"
#include "devices/device_definitions.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/game_engine.h"
#include "game/player_control.h"
#include "game/players.h"
#include "input/input.h"
#include "interface/hud_messaging.h"
#include "items/equipment_definitions.h"
#include "items/weapon_definitions.h"
#include "main/main.h"
#include "objects/damage.h"
#include "objects/objects.h"
#include "objects/object_types.h"
#include "physics/collisions.h"
#include "cache/physical_memory_map.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "units/biped_definitions.h"
#include "units/unit_control_data.h"
#include "units/units.h"
#include "units/vehicle_definitions.h"
#include "units/unit_definitions.h"

#include "forge.h"
#include "forge_blocks.h"
#include "forge_files.h"
#include "halo_menus.h"
#include "scene_pack.h"
#include "scene_packs.h"
#include "tag_schema.h"
#include "../src/lang.h"
#include "../src/forge_store.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);
/* xinput_sdl.c's: the mouse's (the Vita's gyro's) look since the last call,
and the keyboard's keys of the monitor's own (forge_keys) */
int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch);
unsigned long halo_linux_forge_keys(float *wheel_notches);
/* menu_tags.c's: a screen of the in-game set (its pictures read), or NONE */
long menu_tags_ingame_screen(char const *name);
/* ui_widget.c's (port) */
struct widget_instance *ui_widget_port_active_screen(short local_player_index);
struct widget_instance *ui_widget_load_by_name_or_tag(char const *name, long tag_index,
	struct widget_instance *parent, short local_player_index, long invoking_widget_tag,
	long focused_child_parent_widget_tag, short focused_child_index);
long hud_get_font_index(void);
void ui_widgets_close_all(void);
unsigned long system_milliseconds(void);
/* game_engine.c's */
struct game_variant *build_game_variant_slayer(struct game_variant *variant);
char *strtok_r(char *text, char const *separators, char **saved);

/* ---------- constants */

enum
{
	/* the palette's most tags (a campaign map has some 300 of these
	groups' tags) */
	MAXIMUM_FORGE_PALETTE_TAGS = 512,
	/* the deps probe's most tags */
	MAXIMUM_FORGE_DEPENDENCY_TAGS = 4096,
	/* the movement speeds (White, D-pad right) */
	_forge_speed_slow = 0,
	_forge_speed_normal,
	_forge_speed_fast,
	NUMBER_OF_FORGE_SPEEDS,
	/* the keyboard's keys of the monitor's own (halo_linux_forge_keys) */
	FORGE_KEY_SHIFT = 1 << 0,
	FORGE_KEY_DELETE = 1 << 1,
	FORGE_KEY_TAB = 1 << 2,
	FORGE_KEY_WHEEL = 1 << 3,
	/* a notice's time on the screen */
	FORGE_NOTICE_MILLISECONDS = 3000,
	/* the undo history's steps */
	FORGE_UNDO_DEPTH = 32,
	/* the HUD's lines (UTF-8) */
	FORGE_LINE_SIZE = 128,
	/* how long an object let go, moved or placed is followed to where it
	comes to rest (its placed spot, forge_placed's home): it falls, it
	settles (a character with its brain on: only its fall, not where it
	walks off to) */
	FORGE_SETTLE_MILLISECONDS = 3000,
};

/* (the scenery group's tag: no header of the game's names it) */
#define FORGE_SCENERY_DEFINITION_TAG 'scen'
#define FORGE_FONT_TAG 'font'
#define FORGE_UI_WIDGET_TAG 'DeLa'

/* (world units: one is ten feet, about 3 m) */
#define FORGE_CAMERA_THRUST 4.f
#define FORGE_CAMERA_LOOK_RATE 2.5f
#define FORGE_PICK_RANGE 60.f
#define FORGE_PUSH_RATE 3.f
#define FORGE_WHEEL_PUSH 0.5f
#define FORGE_MOUSE_TURN 1.f
#define FORGE_MINIMUM_HOLD_DISTANCE 0.5f
#define FORGE_MAXIMUM_HOLD_DISTANCE 40.f
#define FORGE_STICK_DEAD_ZONE 0.15f
/* a placed object's respawn time unless its properties say (Halo 3's
default) */
#define FORGE_DEFAULT_RESPAWN_SECONDS 30
/* how far outside the structure's world bounds a map variant's object may
be (a vehicle parked at the edge) */
#define FORGE_WORLD_BOUNDS_MARGIN 10.f
/* snapping: the grid along the ground (world units, about 75 cm) and the
turn's step (15 degrees) */
#define FORGE_SNAP_GRID 0.25f
#define FORGE_SNAP_ANGLE 0.2617994f
/* rotate mode (L held while holding an object): a stick's turn (radians a
second, full over), a flick's (snapping: past the first, back under the
second to flick again), the mouse's look that makes a snapped step, the
exact angles' step with no snapping (degrees) */
#define FORGE_ROTATE_RATE 2.f
#define FORGE_ROTATE_FLICK 0.6f
#define FORGE_ROTATE_REARM 0.3f
#define FORGE_ROTATE_MOUSE_STEP 0.1f
#define FORGE_ANGLE_STEP 15

/* the in-game set's screens (halo3/forge.xml) */
#define FORGE_BROWSER_SCREEN "h3/forge/browser/screen"
#define FORGE_MENU_SCREEN "h3/forge/pause/screen"
#define FORGE_PROPERTIES_SCREEN "h3/forge/properties/screen"

static real const forge_speeds[NUMBER_OF_FORGE_SPEEDS] = { 0.6f, 2.f, 5.f };
/* rotate mode's snapping (D-pad right in it), degrees: 0 off */
static short const forge_rotate_snaps[] = { 0, 15, 45, 90 };
/* rotate mode's axes, the exact angles' order */
enum
{
	_forge_axis_yaw,
	_forge_axis_pitch,
	_forge_axis_roll,
	NUMBER_OF_FORGE_AXES
};
static char const *const forge_speed_names[NUMBER_OF_FORGE_SPEEDS] =
{
	N_("Movement Speed: Slow"), N_("Movement Speed: Normal"), N_("Movement Speed: Fast"),
};

/* the monitor's buttons: the game's (input.h) */
enum
{
	_forge_button_fly_up = _gamepad_analog_button_a,
	_forge_button_duplicate = _gamepad_analog_button_b,
	_forge_button_browser = _gamepad_analog_button_x,
	_forge_button_delete = _gamepad_analog_button_y,
	_forge_button_straighten = _gamepad_analog_button_black,
	_forge_button_speed = _gamepad_analog_button_white,
	_forge_button_alternate = _gamepad_analog_button_left_trigger,
	_forge_button_grab = _gamepad_analog_button_right_trigger,
	_forge_button_fly_down = _gamepad_binary_button_left_thumb,
};

/* the browser's categories' names (forge.h's order) */
static char const *const forge_category_names[NUMBER_OF_FORGE_CATEGORIES] =
{
	N_("VEHICLES"), N_("WEAPONS"), N_("EQUIPMENT"), N_("SCENERY"), N_("AI"), N_("BLOCKS"),
};
/* (the log's) */
static char const *const forge_category_log_names[NUMBER_OF_FORGE_CATEGORIES] =
{
	"vehicles", "weapons", "equipment", "scenery", "characters", "blocks",
};

/* the lobby's maps: the Xbox's multiplayer maps, in the order of ui.map's
pictures of them (ui\shell\bitmaps\mp_map_grafix, as
ui_widget_game_data_input_functions.c picks a frame) and its descriptions */
static struct
{
	char const *path;
	char const *name;
} const forge_maps[] =
{
	{ "levels\\test\\beavercreek\\beavercreek", N_("Battle Creek") },
	{ "levels\\test\\sidewinder\\sidewinder", N_("Sidewinder") },
	{ "levels\\test\\damnation\\damnation", N_("Damnation") },
	{ "levels\\test\\ratrace\\ratrace", N_("Rat Race") },
	{ "levels\\test\\prisoner\\prisoner", N_("Prisoner") },
	{ "levels\\test\\hangemhigh\\hangemhigh", N_("Hang 'Em High") },
	{ "levels\\test\\chillout\\chillout", N_("Chill Out") },
	{ "levels\\test\\carousel\\carousel", N_("Derelict") },
	{ "levels\\test\\boardingaction\\boardingaction", N_("Boarding Action") },
	{ "levels\\test\\bloodgulch\\bloodgulch", N_("Blood Gulch") },
	{ "levels\\test\\wizard\\wizard", N_("Wizard") },
	{ "levels\\test\\putput\\putput", N_("Chiron TL-34") },
	{ "levels\\test\\longest\\longest", N_("Longest") },
};

/* the scene packs Forge offers (scene_packs.h: app0:packs/<name>.txt, built
on the player's machine from the player's own campaign maps), in the order
they are loaded: when the tag cache cannot take them all, the first ones
are kept. The vehicles' are on unless turned off in the lobby; the AI is
asked for. (The multiplayer maps' own vehicles - the Warthog, the Ghost,
the Scorpion - are in every Xbox multiplayer map's tags already: no pack
brings them) */
static struct
{
	char const *name;
	char const *label;
	char const *description;
	boolean on_by_default;
} const forge_packs[FORGE_PACK_COUNT] =
{
	{ "covenant_vehicles", N_("Covenant vehicles"), N_("The Banshee, the Wraith and the Shade.\nBuilt from your Assault on the Control\nRoom or Two Betrayals map."), TRUE },
	{ "dropships", N_("Dropships"), N_("The Pelican, which you fly with ten\nriders, and the Covenant dropship. Built\nfrom your The Silent Cartographer or\nHalo map."), TRUE },
	{ "covenant_ai", N_("Covenant AI"), N_("Grunts, jackals, elites (stealth and\nsword too) and hunters, as in the\ncampaign. Built from your Assault on the\nControl Room map."), FALSE },
	{ "flood_ai", N_("Flood AI"), N_("Infection forms, combat forms and\ncarriers, on the Flood's team. Built from\nyour The Library map."), FALSE },
	{ "sentinel_ai", N_("Sentinel AI"), N_("Sentinels, on their own team: they hunt\nthe Flood. Built from your The Library\nmap."), FALSE },
	{ "marine_ai", N_("Marine AI"), N_("Marines with assault and sniper rifles,\non your side as in the campaign. Built\nfrom your Halo map."), FALSE },
};

/* our own names for the multiplayer maps' objects (and the campaign's
common ones), by the last part of the tag's name ("_mp" taken off); an
ammunition's ("<weapon> ammo") is its weapon's, "%s Ammo" */
static struct
{
	char const *tag;
	char const *name;
} const forge_names[] =
{
	/* vehicles */
	{ "warthog", N_("Warthog") },
	{ "rwarthog", N_("Rocket Warthog") },
	{ "scorpion", N_("Scorpion") },
	{ "ghost", N_("Ghost") },
	{ "banshee", N_("Banshee") },
	{ "c gun turret", N_("Shade") },
	{ "wraith", N_("Wraith") },
	{ "pelican", N_("Pelican") },
	{ "c_dropship", N_("Covenant Dropship") },
	/* weapons */
	{ "assault rifle", N_("Assault Rifle") },
	{ "pistol", N_("Pistol") },
	{ "shotgun", N_("Shotgun") },
	{ "sniper rifle", N_("Sniper Rifle") },
	{ "rocket launcher", N_("Rocket Launcher") },
	{ "flamethrower", N_("Flamethrower") },
	{ "plasma pistol", N_("Plasma Pistol") },
	{ "plasma rifle", N_("Plasma Rifle") },
	{ "needler", N_("Needler") },
	{ "plasma_cannon", N_("Fuel Rod Gun") },
	{ "gravity rifle", N_("Gravity Rifle") },
	{ "energy sword", N_("Energy Sword") },
	{ "frag grenade", N_("Frag Grenade") },
	{ "plasma grenade", N_("Plasma Grenade") },
	/* equipment */
	{ "active camouflage", N_("Active Camouflage") },
	{ "over shield", N_("Overshield") },
	{ "health pack", N_("Health Pack") },
	{ "double speed", N_("Double Speed") },
	{ "full-spectrum vision", N_("Full-Spectrum Vision") },
	/* scenery */
	{ "flag_base", N_("Flag Base") },
	{ "floor_arrow", N_("Floor Arrow") },
	{ "teleporter", N_("Teleporter") },
	{ "teleporter_base", N_("Teleporter Base") },
	{ "small blue beacon", N_("Small Blue Beacon") },
	{ "small red beacon", N_("Small Red Beacon") },
	{ "landing beacon", N_("Landing Beacon") },
	{ "blue landing beacon", N_("Blue Landing Beacon") },
	{ "hilltop", N_("Hill Marker") },
	{ "c_storage", N_("Covenant Crate") },
	{ "c_storage_large", N_("Large Covenant Crate") },
	{ "c_field_generator", N_("Field Generator") },
	{ "c_uplink", N_("Covenant Uplink") },
	{ "h_barricade_large", N_("Large Barricade") },
	{ "h_barricade_large_gap", N_("Large Barricade, Gap") },
	{ "h_barricade_small", N_("Small Barricade") },
	{ "h_barricade_small_visor", N_("Small Barricade, Visor") },
	{ "glowingdrip", N_("Glowing Drip") },
	{ "boulder", N_("Boulder") },
	{ "boulder_crouch", N_("Low Boulder") },
	{ "boulder_doublewide", N_("Wide Boulder") },
	{ "boulder_large_grey", N_("Large Grey Boulder") },
	{ "a50_rock_large", N_("Large Rock") },
	{ "snowrock", N_("Snow Rocks") },
	{ "snowrocksmall", N_("Small Snow Rocks") },
	{ "shrub_large", N_("Large Shrub") },
	{ "plant_broadleaf_short", N_("Short Broadleaf") },
	{ "plant_broadleaf_tall", N_("Tall Broadleaf") },
	{ "plant fern", N_("Fern") },
	{ "tree_desert_dead", N_("Dead Tree") },
	{ "tree_desert_whitebark", N_("Whitebark Tree") },
	{ "tree_leafy", N_("Leafy Tree") },
	{ "tree_leafy_medium", N_("Medium Leafy Tree") },
	{ "tree_leafy_sapling", N_("Sapling") },
	{ "tree_leafy_fallentrunk", N_("Fallen Trunk") },
	{ "tree_leafy_stump_crouch", N_("Tree Stump") },
	{ "tree_pine", N_("Pine Tree") },
	{ "tree_pine_snow", N_("Snowy Pine") },
	{ "tree_pine_snowsmall", N_("Small Snowy Pine") },
	{ "doorblinker", N_("Door Light") },
	/* characters (the actor variants: the rest are their names' words) */
	{ "flood_infection", N_("Flood Infection Form") },
	{ "floodcarrier", N_("Flood Carrier Form") },
	{ "floodcombat elite plasma rifle", N_("Flood Elite Plasma Rifle") },
	{ "floodcombat elite shotgun", N_("Flood Elite Shotgun") },
	{ "floodcombat_human assault rifle", N_("Flood Human Assault Rifle") },
	{ "floodcombat_human shotgun", N_("Flood Human Shotgun") },
	{ "marine_armored assault rifle", N_("Armored Marine Assault Rifle") },
	{ "marine_armored assault rifle major", N_("Armored Marine Assault Rifle Major") },
	{ "marine_armored sniper rifle", N_("Armored Marine Sniper Rifle") },
	{ "marine_armored sniper rifle major", N_("Armored Marine Sniper Rifle Major") },
	{ "metal_small", N_("Small Metal Block") },
	{ "metal_tall", N_("Tall Metal Block") },
	{ "metal_wide", N_("Wide Metal Block") },
	{ "door_blast_collision", N_("Blast Door") },
	{ "h oxy tank", N_("Oxygen Tank") },
	{ "alert_light", N_("Alert Light") },
	{ "wiz_alert_light", N_("Alert Light") },
};

/* ---------- structures */

struct forge_pad
{
	boolean down[NUMBER_OF_GAMEPAD_BUTTONS];
	/* -1 to 1, the dead zone taken off */
	real sticks[NUMBER_OF_GAMEPAD_STICKS][2];
	/* the mouse's (the gyro's) look this frame, radians */
	real look_yaw;
	real look_pitch;
	/* the keyboard's own keys (FORGE_KEY_*), the wheel's notches */
	unsigned long keys;
	real wheel;
	/* the monitor and the Spartan's combo held (Back + the right stick's
	click, xinput_sdl.c forge_combo_filter), or Tab */
	boolean toggle;
};

/* a tag the browser offers */
struct forge_palette_entry
{
	long tag_index;
	short category;
	/* a character's side (FORGE_AI_GROUP_*: the browser's AI category
	lists them side by side), else 0 */
	short group;
	char name[FORGE_NAME_SIZE];
};

/* the sides the browser's AI category groups its characters by, in its
order: by the unit's default team (the Covenant's, the Flood's, the
Sentinels', the humans' and the player's), the rest after */
enum
{
	FORGE_AI_GROUP_COVENANT,
	FORGE_AI_GROUP_FLOOD,
	FORGE_AI_GROUP_SENTINELS,
	FORGE_AI_GROUP_HUMANS,
	FORGE_AI_GROUP_OTHER,
	NUMBER_OF_FORGE_AI_GROUPS
};
static char const *const forge_ai_group_names[NUMBER_OF_FORGE_AI_GROUPS] =
{
	N_("Covenant"), N_("Flood"), N_("Sentinels"), N_("Humans"), "",
};
static char const *const forge_ai_group_log_names[NUMBER_OF_FORGE_AI_GROUPS] =
{
	"covenant", "flood", "sentinels", "humans", "other",
};

/* an object's place and facing */
struct forge_transform
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
};

/* an object this session placed: the layout (the budget's, what a map
variant saves and what a new round places again). An object gone in play
(equipment picked up, a grenade thrown) keeps its entry, its object NONE:
it is still the layout's and comes back with the next round; only the
editor's delete (or undo) takes an entry out */
struct forge_placed
{
	long object_index;
	/* the palette tag it was spawned from (an actor variant for a
	character) */
	long tag_index;
	boolean ai;
	/* its properties (a map variant's record's): team (NONE neutral),
	respawn time, FORGE_RECORD_* flags (FORGE_RECORD_AI_OFF: a
	character's brain off), a character's AI team (FORGE_AI_TEAM_*) */
	signed char team;
	unsigned char flags;
	unsigned short respawn_seconds;
	unsigned char ai_team;
	/* its own number this session (the undo history names it by this) */
	long id;
	/* where the editor put it (its placed spot: saved, and where a new
	round puts it), followed while it is held and for a moment after it
	is let go or moved (it falls, it settles: settle_until), never by play
	(a Warthog driven, an elite walking) */
	struct forge_transform home;
	boolean has_home;
	unsigned long settle_until;
};

/* a step of the undo history */
enum
{
	_forge_undo_place,
	_forge_undo_delete,
	_forge_undo_move,
	NUMBER_OF_FORGE_UNDO_KINDS
};

struct forge_undo
{
	short kind;
	/* the object: a placed one's id, else (NONE) a map object's index */
	long id;
	long object_index;
	/* what brings it back: its tag (an actor variant for a character),
	its properties */
	long tag_index;
	signed char team;
	unsigned char flags;
	unsigned short respawn_seconds;
	unsigned char ai_team;
	/* where it was before the step, and after */
	struct forge_transform from;
	struct forge_transform to;
};

typedef char verify_forge_file_records[FORGE_FILE_MAXIMUM_RECORDS == FORGE_MAXIMUM_OBJECTS ? 1 : -1];

/* ---------- globals */

/* (vita_pad.c) set while the monitor flies: D-pad down is fly down while
held, never the crouch toggle's latch */
int forge_monitor_flying = 0;
/* (objects.c) set while forge_spawn makes the object chosen: the game
type's weapon, vehicle and equipment sets do not put another in its place
(a multiplayer game makes a flamethrower a rocket launcher) */
int forge_placing_exact = 0;
/* (xinput_sdl.c forge_combo_filter) set while a Forge game is played or the
monitor flies: Back + the right stick's click together are then the
monitor and the Spartan's combo, kept from the game; set there while it is
held */
int forge_game_playing = 0;
int forge_combo_held = 0;

static struct
{
	/* the session: a Forge game asked for (the lobby), then played */
	boolean game_requested;
	char game_map[256];
	boolean game_active;
	/* the monitor */
	boolean active;
	short local_player_index;
	/* the player's unit while it waits, and what it was before */
	long unit_index;
	boolean unit_could_take_damage;
	/* the object held, how far from the camera, and its facing as held */
	long held_object_index;
	real held_distance;
	real_vector3d held_forward;
	real_vector3d held_up;
	short speed;
	/* the palette, category by category, each by name */
	struct forge_palette_entry palette[MAXIMUM_FORGE_PALETTE_TAGS];
	short palette_count;
	short palette_first[NUMBER_OF_FORGE_CATEGORIES];
	short palette_counts[NUMBER_OF_FORGE_CATEGORIES];
	boolean palette_built;
	char palette_language[16];
	/* (the console's forge spawn and pick) the palette's current one */
	short palette_index;
	/* the budget */
	struct forge_placed placed[FORGE_MAXIMUM_OBJECTS];
	short placed_count;
	/* the characters of the layout, and the dead among them (they hold
	their places in the budget: they come back with the next round) */
	short ai_count;
	short ai_dead_count;
	/* the buttons down last frame */
	boolean was_down[NUMBER_OF_GAMEPAD_BUTTONS];
	boolean toggle_was_down;
	/* a menu was over the game at the last forge_update */
	boolean menu_was_open;
	unsigned long keys_were_down;
	/* (forge press, forge stick) the buttons and sticks the tests hold */
	boolean test_down[NUMBER_OF_GAMEPAD_BUTTONS];
	boolean test_toggle;
	short test_buttons_frames;
	real test_sticks[NUMBER_OF_GAMEPAD_STICKS][2];
	short test_sticks_frames;
	/* (forge look) the mouse's look the tests give, radians, taken once */
	real test_look_yaw;
	real test_look_pitch;
	/* asked from the menus (their events may run beside the tick): a
	palette entry to spawn (browser_choice, once browser_chosen), monitor
	and Spartan flipped */
	short browser_choice;
	volatile boolean browser_chosen;
	volatile boolean toggle_requested;
	volatile boolean close_requested;
	volatile boolean quit_requested;
	/* the mouse's turn of the object held (forge_camera_input's, for
	forge_update): across, and up and down */
	real pending_mouse_turn;
	real pending_mouse_pitch;
	/* rotate mode: on this frame (L held while holding an object), its
	snapping (forge_rotate_snaps' index), and, snapping, each axis's stick
	flicked (let back to flick again) and the mouse's look not yet a
	step */
	boolean rotating;
	short rotate_snap;
	boolean rotate_flicked[NUMBER_OF_FORGE_AXES];
	real rotate_mouse[NUMBER_OF_FORGE_AXES];
	/* what the monitor did since it started, for the log */
	long spawned_count;
	long deleted_count;
	/* the HUD's lines (forge_draw), written by forge_update */
	char hud_title[FORGE_LINE_SIZE];
	char hud_budget[FORGE_LINE_SIZE];
	char hud_label[FORGE_LINE_SIZE];
	boolean hud_label_held;
	/* (holding an object) its angles and rotate mode's snapping */
	char hud_rotate[FORGE_LINE_SIZE];
	char notice[FORGE_LINE_SIZE];
	unsigned long notice_until;
	/* (a menu that pauses the game is over it: no HUD) */
	boolean hud_hidden;
	/* the in-game set's fonts for the HUD (NONE: the HUD's own): its small
	one for the lines, its item one for the label, its title one for the
	crosshair */
	long hud_font_index;
	long hud_label_font_index;
	long hud_crosshair_font_index;
	boolean hud_font_looked_up;
	/* the map variant the session plays (its title, its file: a save under
	its title replaces it), one read for the session to apply as it starts
	(forge_variant_layout), a save asked for by the Forge menu (its title)
	and what it came to (said there) */
	char variant_title[FORGE_FILE_TITLE_SIZE];
	char variant_file[FORGE_FILE_NAME_SIZE];
	boolean variant_pending;
	volatile boolean save_requested;
	char save_title[FORGE_FILE_TITLE_SIZE];
	char save_author[FORGE_FILE_AUTHOR_SIZE];
	char save_status[FORGE_STORE_WHY_SIZE];
	/* the undo history: undo_count steps, the first undo_cursor of them
	done (undo takes the one before the cursor, redo the one at it); the
	next placed id; where the object held was when it was grabbed, and
	whether it was placed by this hold (its drop is a placing, not a
	move) */
	struct forge_undo undo[FORGE_UNDO_DEPTH];
	short undo_count;
	short undo_cursor;
	long next_id;
	struct forge_transform held_from;
	boolean held_spawned;
	/* snapping on */
	boolean snap;
	/* asked from the menus (done at the next forge_update): undo (-1) or
	redo (1), snapping flipped, a property stepped */
	volatile short undo_requested;
	volatile boolean snap_requested;
	volatile boolean property_requested;
	short property_request;
	short property_step;
	/* what the last thing asked from the Forge menu came to (said under
	it) */
	char menu_status[FORGE_STORE_WHY_SIZE];
	/* the packs: the lobby's choice (a bit each, forge_packs' order; set
	from their defaults once), the session's (the lobby's and its map
	variant's), and what came of them as its map loaded */
	boolean packs_chosen;
	unsigned long packs_enabled;
	unsigned long packs_session;
	char packs_report[FORGE_LINE_SIZE];
	/* a new round (the Forge menu's NEW ROUND, Halo 3's Restart Round):
	asked (round_requested, done at the next forge_update with no menu
	over the game), the map reset by the engine (main_reset_map: the map's
	own objects, the players, the scores start over); the layout taken as
	the map resets (forge_map_resetting: round_layout) and placed again
	once the player has spawned (round_pending), the monitor again where it
	was if it flew (round_monitor, round_camera) */
	volatile boolean round_requested;
	boolean round_resetting;
	boolean round_pending;
	boolean round_monitor;
	struct forge_transform round_camera;
	long round_count;
	/* (the Forge menu's AI row) every placed character's brain flipped:
	all off if any is on, else all on */
	volatile boolean ai_all_requested;
	/* (the Forge menu's CLEAR DEAD AI and CLEAR ALL AI, the settings
	panel's) the layout's dead characters, or all of them, taken out:
	_forge_clear_ai_* */
	volatile short clear_ai_requested;
	/* the ALL AI setting (the last one chosen, or read with a map
	variant): a character placed gets its AI off while it is set; the
	characters the game spawns in a Forge game follow it */
	boolean ai_all_off;
	/* (tests) the projectiles the characters fired, counted as they are
	seen (forge_ai_shots_update, once the console's "forge ai" has asked):
	their objects' last ones, to count each once */
	boolean ai_shots_counted;
	long ai_shots;
	long ai_shot_seen[128];
	short ai_shot_cursor;
} forge_globals = { FALSE, "", FALSE, FALSE, 0, NONE, FALSE, NONE, 0.f, { 0.f }, { 0.f }, _forge_speed_normal };

/* ---------- private code */

static char const *forge_tag_leaf(
	long tag_index)
{
	char const *name = tag_get_name(tag_index);
	char const *slash = name ? strrchr(name, '\\') : NULL;

	return slash ? slash + 1 : (name ? name : "?");
}

/* our own name of a tag's last part (its "_mp" off), translated; NULL if
it has none */
static char const *forge_names_find(
	char const *leaf)
{
	short index;

	for (index = 0; index < (short)NUMBEROF(forge_names); index++)
	{
		if (!strcmp(forge_names[index].tag, leaf))
			return T(forge_names[index].name);
	}
	return NULL;
}

/* a tag's last part as words: a level's prefix ("a50_") off, "_" a space,
each word's first letter a capital */
static void forge_name_words(
	char const *leaf,
	char *name,
	size_t size)
{
	size_t length = 0;
	boolean word_start = TRUE;

	if (leaf[0] >= 'a' && leaf[0] <= 'd' && leaf[1] >= '0' && leaf[1] <= '9' && leaf[2] >= '0' &&
		leaf[2] <= '9' && leaf[3] == '_' && leaf[4])
	{
		leaf += 4;
	}
	for (; *leaf && length + 1 < size; leaf++)
	{
		char character = *leaf == '_' ? ' ' : *leaf;

		if (word_start && character >= 'a' && character <= 'z')
			character = (char)(character - 'a' + 'A');
		word_start = character == ' ';
		name[length++] = character;
	}
	name[length] = 0;
}

/* a tag's readable name (forge_names, else its last part as words) */
static void forge_tag_readable_name(
	long tag_index,
	char *name,
	size_t size)
{
	char leaf[FORGE_NAME_SIZE];
	char const *found;
	size_t length;

	/* (a building block: forge_blocks.c's names) */
	if (forge_blocks_of_tag(tag_index) != NONE)
	{
		forge_blocks_name(forge_blocks_of_tag(tag_index), name, (int)size);
		return;
	}
	csstrncpy(leaf, forge_tag_leaf(tag_index), sizeof(leaf) - 1);
	leaf[sizeof(leaf) - 1] = 0;
	length = strlen(leaf);
	if (length > 3 && !strcmp(leaf + length - 3, "_mp"))
		leaf[length -= 3] = 0;
	/* (an ammunition: its weapon's name) */
	if (length > 5 && !strcmp(leaf + length - 5, " ammo"))
	{
		char weapon[FORGE_NAME_SIZE];

		leaf[length - 5] = 0;
		found = forge_names_find(leaf);
		if (found)
			snprintf(weapon, sizeof(weapon), "%s", found);
		else
			forge_name_words(leaf, weapon, sizeof(weapon));
		snprintf(name, size, T("%s Ammo"), weapon);
		return;
	}
	found = forge_names_find(leaf);
	if (found)
		snprintf(name, size, "%s", found);
	else
		forge_name_words(leaf, name, size);
}

/* the browser's category of a tag of the loaded map, NONE if it offers
none of it: a vehicle's gun, the flag and the ball (the game's), anything
of a cinematic's, a character that names no unit (a vehicle's crew) */
static short forge_tag_category(
	long tag_index)
{
	unsigned long group_tag = tag_get_group_tag(tag_index);
	char const *name = tag_get_name(tag_index);

	if (!name || strstr(name, "cinematic"))
		return NONE;
	switch (group_tag)
	{
	case VEHICLE_DEFINITION_TAG:
		return _forge_category_vehicles;
	case WEAPON_DEFINITION_TAG:
		if (!strncmp(name, "vehicles\\", 9) || !strncmp(name, "weapons\\flag\\", 13) ||
			!strncmp(name, "weapons\\ball\\", 13))
		{
			return NONE;
		}
		/* (nor a character's own - a sentinel's beam - nor a pack's with
		no first-person model, which only its character holds: the elite
		commander's energy sword, the hunter's fuel rod; a map's own is
		offered as before) */
		if (!strncmp(name, "characters\\", 11) ||
			(weapon_definition_get(tag_index)->weapon.interface_definition.first_person_model.index == NONE &&
				scene_packs_owner(tag_index)))
		{
			return NONE;
		}
		return _forge_category_weapons;
	case EQUIPMENT_DEFINITION_TAG:
		return strstr(name, "grenade") ? _forge_category_weapons : _forge_category_equipment;
	case FORGE_SCENERY_DEFINITION_TAG:
		/* (the building blocks: forge_blocks.c) */
		if (forge_blocks_of_tag(tag_index) != NONE)
			return _forge_category_blocks;
		return _forge_category_scenery;
	case MACHINE_DEFINITION_TAG:
	case CONTROL_DEFINITION_TAG:
		return _forge_category_scenery;
	case ACTOR_VARIANT_DEFINITION_TAG:
		/* (not a vehicle's crew: a dropship's gunner, a turret's) */
		if (!strncmp(name, "vehicles\\", 9))
			return NONE;
		return actor_variant_definition_get(tag_index)->unit_reference.index != NONE ?
			_forge_category_characters : NONE;
	}
	return NONE;
}

/* a character's side in the browser (FORGE_AI_GROUP_*), by its unit's
default team */
static short forge_ai_group_of_variant(
	long tag_index)
{
	long unit_definition_index = actor_variant_definition_get(tag_index)->unit_reference.index;

	if (unit_definition_index == NONE)
		return FORGE_AI_GROUP_OTHER;
	switch (unit_definition_get(unit_definition_index)->unit.default_team)
	{
	case _game_team_covenant:
		return FORGE_AI_GROUP_COVENANT;
	case _game_team_flood:
		return FORGE_AI_GROUP_FLOOD;
	case _game_team_sentinel:
		return FORGE_AI_GROUP_SENTINELS;
	case _game_team_human:
	case _game_team_player:
		return FORGE_AI_GROUP_HUMANS;
	}
	return FORGE_AI_GROUP_OTHER;
}

static int forge_palette_compare(
	void const *a,
	void const *b)
{
	struct forge_palette_entry const *left = a;
	struct forge_palette_entry const *right = b;

	if (left->category != right->category)
		return left->category - right->category;
	if (left->group != right->group)
		return left->group - right->group;
	return strcmp(left->name, right->name);
}

/* the palette: every tag the browser offers of the loaded map, category by
category, each by its readable name (the same name twice numbered) */
static void forge_palette_build(
	void)
{
	static unsigned long const groups[] =
	{
		VEHICLE_DEFINITION_TAG, WEAPON_DEFINITION_TAG, EQUIPMENT_DEFINITION_TAG, FORGE_SCENERY_DEFINITION_TAG,
		MACHINE_DEFINITION_TAG, CONTROL_DEFINITION_TAG, ACTOR_VARIANT_DEFINITION_TAG,
	};
	short group_index;
	short index;
	short category;

	forge_globals.palette_count = 0;
	for (group_index = 0; group_index < (short)NUMBEROF(groups); group_index++)
	{
		struct tag_iterator iterator;
		long tag_index;

		tag_iterator_new(&iterator, groups[group_index]);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE &&
			forge_globals.palette_count < MAXIMUM_FORGE_PALETTE_TAGS)
		{
			struct forge_palette_entry *entry;

			/* (an iterator of a group walks its children's tags too:
			only the group's own) */
			if (tag_get_group_tag(tag_index) != groups[group_index] || (category = forge_tag_category(tag_index)) == NONE)
				continue;
			/* (a pack's copy of a tag the map has, or an earlier pack: the
			first one's, the map's own when it has it, so that each object
			is offered once) */
			if (tag_get_name(tag_index) && tag_loaded((long)groups[group_index], tag_get_name(tag_index)) != tag_index)
				continue;
			entry = &forge_globals.palette[forge_globals.palette_count++];
			entry->tag_index = tag_index;
			entry->category = category;
			/* (a character's side; the blocks in their set's order) */
			entry->group = category == _forge_category_characters ? forge_ai_group_of_variant(tag_index) :
				category == _forge_category_blocks ? forge_blocks_of_tag(tag_index) : 0;
			forge_tag_readable_name(tag_index, entry->name, sizeof(entry->name));
		}
	}
	qsort(forge_globals.palette, (size_t)forge_globals.palette_count, sizeof(forge_globals.palette[0]),
		forge_palette_compare);
	for (category = 0; category < NUMBER_OF_FORGE_CATEGORIES; category++)
	{
		forge_globals.palette_first[category] = 0;
		forge_globals.palette_counts[category] = 0;
	}
	for (index = forge_globals.palette_count - 1; index >= 0; index--)
	{
		category = forge_globals.palette[index].category;
		forge_globals.palette_first[category] = index;
		forge_globals.palette_counts[category]++;
	}
	/* (one name several times in a category: the second "<name> 2", the
	third "<name> 3"...) */
	{
		short first = 0;
		short repeat = 1;

		for (index = 1; index < forge_globals.palette_count; index++)
		{
			struct forge_palette_entry *entry = &forge_globals.palette[index];
			size_t length = strlen(forge_globals.palette[first].name);

			if (entry->category == forge_globals.palette[first].category &&
				!strncmp(entry->name, forge_globals.palette[first].name, length) && !entry->name[length])
			{
				repeat++;
				if (length + 6 < sizeof(entry->name))
					snprintf(entry->name + length, sizeof(entry->name) - length, " %d", repeat);
			}
			else
			{
				first = index;
				repeat = 1;
			}
		}
	}
	if (forge_globals.palette_index >= forge_globals.palette_count)
		forge_globals.palette_index = 0;
	forge_globals.palette_built = TRUE;
	snprintf(forge_globals.palette_language, sizeof(forge_globals.palette_language), "%s", lang_current());

	return;
}

/* the palette, built for the loaded map in the language chosen */
static void forge_palette_ready(
	void)
{
	if (!forge_globals.palette_built || strcmp(forge_globals.palette_language, lang_current()))
		forge_palette_build();
}

/* the palette entry of a tag, NONE if none */
static short forge_palette_of_tag(
	long tag_index)
{
	short index;

	for (index = 0; index < forge_globals.palette_count; index++)
	{
		if (forge_globals.palette[index].tag_index == tag_index)
			return index;
	}
	return NONE;
}

/* the palette entry named name (its readable name, any case), else the
first with name in its readable name or its tag's, or NONE */
static short forge_palette_find(
	char const *name)
{
	short index;

	for (index = 0; index < forge_globals.palette_count; index++)
	{
		if (!_stricmp(forge_globals.palette[index].name, name) ||
			!strcmp(forge_tag_leaf(forge_globals.palette[index].tag_index), name))
		{
			return index;
		}
	}
	for (index = 0; index < forge_globals.palette_count; index++)
	{
		char const *tag_name = tag_get_name(forge_globals.palette[index].tag_index);

		if ((tag_name && strstr(tag_name, name)) || strstr(forge_globals.palette[index].name, name))
			return index;
	}
	return NONE;
}

/* an object's readable name: its palette entry's (a character's, by the
budget's record), else its tag's */
static void forge_object_name(
	long object_index,
	char *name,
	size_t size)
{
	struct object_datum *object = object_try_and_get(object_index);
	short index;

	if (!object)
	{
		snprintf(name, size, "?");
		return;
	}
	for (index = 0; index < forge_globals.placed_count; index++)
	{
		if (forge_globals.placed[index].object_index == object_index)
		{
			short entry = forge_palette_of_tag(forge_globals.placed[index].tag_index);

			if (entry != NONE)
			{
				snprintf(name, size, "%s", forge_globals.palette[entry].name);
				return;
			}
		}
	}
	index = forge_palette_of_tag(object->definition_index);
	if (index != NONE)
		snprintf(name, size, "%s", forge_globals.palette[index].name);
	else
		forge_tag_readable_name(object->definition_index, name, size);
}

/* a line on the monitor's HUD for a while, and in the log */
static void forge_notice(
	char const *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(forge_globals.notice, sizeof(forge_globals.notice), format, arguments);
	va_end(arguments);
	forge_globals.notice_until = system_milliseconds() + FORGE_NOTICE_MILLISECONDS;
	platform_log("forge: %s", forge_globals.notice);
}

/* a notice that is also the Forge menu's status line */
static void forge_menu_notice(
	char const *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(forge_globals.menu_status, sizeof(forge_globals.menu_status), format, arguments);
	va_end(arguments);
	forge_notice("%s", forge_globals.menu_status);
}

/* ---- the budget */

/* whether an object is there and not being deleted (object_delete only
marks it: it goes at the next update of the objects, which a paused game -
the Forge menu over it - does not run) */
static boolean forge_object_alive(
	long object_index)
{
	return object_try_and_get(object_index) &&
		!TEST_FLAG(object_header_get(object_index)->flags, _object_header_being_deleted_bit);
}

static void forge_transform_get(long object_index, struct forge_transform *transform);

/* whether a placed character is dead: its body gone (a Flood infection
form pops, a carrier bursts, a combat form's body may be taken) or lying
dead - not a Flood combat form feigning death, which gets back up as the
same unit (units.c unit_port_resurrect) */
static boolean forge_placed_dead(
	struct forge_placed const *placed)
{
	struct unit_datum *unit;

	if (!placed->ai)
		return FALSE;
	if (!forge_object_alive(placed->object_index))
		return TRUE;
	unit = (struct unit_datum *)object_get(placed->object_index);
	if (!TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return FALSE;
	return !(FLAG(unit->object.type) & _object_mask_unit) || unit->unit.feign_death_timer <= 0;
}

/* the layout's objects gone in play (equipment picked up, a grenade thrown)
kept, their object NONE (they come back with the next round); the
characters counted, and the dead among them (they come back too, and keep
their places in the budget until CLEAR DEAD AI takes them out). Only the
layout's: what the game spawns (a carrier's infection forms, the Flood's
own) is never counted */
static void forge_placed_update(
	void)
{
	short index;

	forge_globals.ai_count = 0;
	forge_globals.ai_dead_count = 0;
	forge_globals.placed_count = (short)PIN(forge_globals.placed_count, 0, FORGE_MAXIMUM_OBJECTS);
	for (index = 0; index < forge_globals.placed_count; index++)
	{
		struct forge_placed *placed = &forge_globals.placed[index];

		if (placed->object_index != NONE && !forge_object_alive(placed->object_index))
			placed->object_index = NONE;
		if (placed->ai)
		{
			forge_globals.ai_count++;
			if (forge_placed_dead(placed))
				forge_globals.ai_dead_count++;
		}
	}
}

/* an entry of the layout taken out (the editor's delete, an undone
placing): the rest kept in their order */
static void forge_placed_remove(
	short index)
{
	if (index < 0 || index >= forge_globals.placed_count || index >= FORGE_MAXIMUM_OBJECTS)
		return;
	csmemmove(&forge_globals.placed[index], &forge_globals.placed[index + 1],
		sizeof(forge_globals.placed[0]) * (size_t)(forge_globals.placed_count - index - 1));
	forge_globals.placed_count--;
	forge_placed_update();
}

/* the entry of an object (NULL for NONE, or one not placed) */
static struct forge_placed *forge_placed_of_object(
	long object_index)
{
	short index;

	if (object_index == NONE)
		return NULL;
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		if (forge_globals.placed[index].object_index == object_index)
			return &forge_globals.placed[index];
	}
	return NULL;
}

/* the editor took an object out of the layout: its entry goes (not kept
as gone in play) */
static void forge_placed_forget_object(
	long object_index)
{
	struct forge_placed *placed = forge_placed_of_object(object_index);

	if (placed)
		forge_placed_remove((short)(placed - forge_globals.placed));
}

static void forge_placed_forget_id(
	long id)
{
	short index;

	for (index = 0; id != NONE && index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		if (forge_globals.placed[index].id == id)
		{
			forge_placed_remove(index);
			return;
		}
	}
}

/* an object placed, let go or moved by the editor: its placed spot follows
it while it comes to rest */
static void forge_settle(
	long object_index)
{
	struct forge_placed *placed = forge_placed_of_object(object_index);

	if (!placed)
		return;
	if (forge_object_alive(object_index))
	{
		forge_transform_get(object_index, &placed->home);
		placed->has_home = TRUE;
	}
	placed->settle_until = system_milliseconds() + FORGE_SETTLE_MILLISECONDS;
}

/* (each frame) the placed spots of the objects held or settling followed:
not while something else carries them (a weapon in a hand, a rider in a
seat), not once dead or destroyed */
static void forge_homes_update(
	void)
{
	unsigned long now = system_milliseconds();
	short index;

	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed *placed = &forge_globals.placed[index];
		struct object_datum *object;

		if (!forge_object_alive(placed->object_index))
			continue;
		object = object_get(placed->object_index);
		if (placed->object_index != forge_globals.held_object_index &&
			((long)(placed->settle_until - now) <= 0 || object->object.parent_object_index != NONE ||
				TEST_FLAG(object->object.damage_flags, _object_dead_bit)))
		{
			continue;
		}
		/* (a character with its brain on, let go: only its fall followed,
		not where it walks off to) */
		if (placed->ai && placed->has_home && placed->object_index != forge_globals.held_object_index &&
			!(placed->flags & FORGE_RECORD_AI_OFF))
		{
			placed->home.position.z = object->object.position.z;
			continue;
		}
		forge_transform_get(placed->object_index, &placed->home);
		placed->has_home = TRUE;
	}
}

static void forge_placed_add(
	long object_index,
	long tag_index,
	boolean ai)
{
	struct forge_placed *placed;

	if (forge_globals.placed_count < 0 || forge_globals.placed_count >= FORGE_MAXIMUM_OBJECTS)
		return;
	placed = &forge_globals.placed[forge_globals.placed_count];
	csmemset(placed, 0, sizeof(*placed));
	placed->object_index = object_index;
	placed->tag_index = tag_index;
	placed->ai = ai;
	placed->team = NONE;
	placed->flags = FORGE_RECORD_PLACE_AT_START;
	placed->respawn_seconds = FORGE_DEFAULT_RESPAWN_SECONDS;
	placed->ai_team = FORGE_AI_TEAM_DEFAULT;
	placed->id = ++forge_globals.next_id;
	forge_globals.placed_count++;
	if (ai)
		forge_globals.ai_count++;
	forge_settle(object_index);
}

/* ---- a character's AI (forge_placed's ai_team, FORGE_RECORD_AI_OFF), by
the engine's own switches: its unit's and its actor's team, as the scripts'
ai_set_team gives them (ai_update_team_status then has every actor see
friend and foe again), and ai_braindead's actor_braindead (a braindead
actor stands frozen as placed: it thinks, moves and fights not, as a
cinematic's characters do) */

/* the properties window's words for the AI teams, and the log's */
static char const *const forge_ai_team_names[NUMBER_OF_FORGE_AI_TEAMS] =
{
	N_("DEFAULT"), N_("PLAYER"), N_("HUMAN"), N_("COVENANT"), N_("FLOOD"), N_("SENTINEL"),
};
static char const *const forge_ai_team_log_names[NUMBER_OF_FORGE_AI_TEAMS] =
{
	"default", "player", "human", "covenant", "flood", "sentinel",
};

/* the engine's team (game.h _game_team_*) an AI team is for a unit: the
player's own (in a free for all game every player has a team of his own:
the local player's), an AI team, or the unit's own default */
static short forge_ai_team_index(
	short ai_team,
	long unit_index)
{
	struct object_datum *object;

	switch (ai_team)
	{
	case FORGE_AI_TEAM_PLAYER:
	{
		long player_index = local_player_get_player_index(0);

		return player_index != NONE ? (short)player_get(player_index)->team_index : (short)_game_team_player;
	}
	case FORGE_AI_TEAM_HUMAN:
		return _game_team_human;
	case FORGE_AI_TEAM_COVENANT:
		return _game_team_covenant;
	case FORGE_AI_TEAM_FLOOD:
		return _game_team_flood;
	case FORGE_AI_TEAM_SENTINEL:
		return _game_team_sentinel;
	}
	/* (its unit's own; one of no AI team - none, or the default team, a
	free for all player's - fights everyone but its own kind: an unused
	team of the engine's) */
	object = object_try_and_get(unit_index);
	if (object)
	{
		short team = unit_definition_get(object->definition_index)->unit.default_team;

		if (team > _game_team_default && team < NUMBER_OF_SOLO_CAMPAIGN_TEAMS)
			return team;
	}
	return _game_team_unused6;
}

/* (actors.c's _actor_mode_braindead: an actor's state.mode while its AI
is off, ai_braindead's) */
#define FORGE_ACTOR_MODE_BRAINDEAD 0
/* (actors.c's actor_default_state_alert and _none) */
#define FORGE_ACTOR_DEFAULT_STATE_NONE 0
#define FORGE_ACTOR_DEFAULT_STATE_ALERT 2

/* a unit's actor: its own, or its swarm's (a Flood infection form's), or
NONE */
static long forge_ai_actor(
	long unit_index)
{
	struct object_datum *object = object_try_and_get(unit_index);
	struct unit_datum *unit;

	if (!object || !(FLAG(object->object.type) & _object_mask_unit))
		return NONE;
	unit = (struct unit_datum *)object;
	return unit->unit.actor_index != NONE ? unit->unit.actor_index : unit->unit.swarm_actor_index;
}

/* a placed character's AI settings given to its unit and actor (a dead
one has none to give) */
static void forge_ai_apply(
	struct forge_placed const *placed)
{
	struct unit_datum *unit;
	long actor_index;
	short team;

	if (!placed || !placed->ai || !forge_object_alive(placed->object_index))
		return;
	unit = unit_get(placed->object_index);
	if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return;
	team = forge_ai_team_index(placed->ai_team, placed->object_index);
	unit->object.owner_team_index = team;
	actor_index = unit->unit.actor_index;
	if (actor_index != NONE)
	{
		actor_get(actor_index)->meta.team_index = team;
		actor_braindead(actor_index, (placed->flags & FORGE_RECORD_AI_OFF) != 0);
	}
	ai_update_team_status();
}

/* (the Forge menu's AI row) the layout's characters' AI: all on, all off,
or mixed (none placed: the ALL AI setting) */
static short forge_ai_state(
	void)
{
	short index;
	short on = 0;
	short off = 0;

	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		if (!forge_globals.placed[index].ai)
			continue;
		if (forge_globals.placed[index].flags & FORGE_RECORD_AI_OFF)
			off++;
		else
			on++;
	}
	if (on && off)
		return _forge_ai_all_mixed;
	if (on)
		return _forge_ai_all_on;
	if (off)
		return _forge_ai_all_off;
	return forge_globals.ai_all_off ? _forge_ai_all_off : _forge_ai_all_on;
}

/* a character the game has (a unit with an actor, or a swarm's) not of
the layout, in a Forge game, brought to the ALL AI setting: one a carrier
spawned, a body the Flood took (on a campaign map with the monitor, the
map's own characters are the map's: left alone) */
static void forge_ai_unplaced_update(
	boolean all_set)
{
	struct object_iterator iterator;
	struct unit_datum *unit;

	if (!forge_globals.game_active || (!all_set && !forge_globals.ai_all_off))
		return;
	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((unit = (struct unit_datum *)object_iterator_next(&iterator)) != NULL)
	{
		long actor_index = unit->unit.actor_index != NONE ? unit->unit.actor_index : unit->unit.swarm_actor_index;
		struct actor_datum *actor;

		if (actor_index == NONE || TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ||
			unit->unit.player_index != NONE || forge_placed_of_object(iterator.index))
		{
			continue;
		}
		actor = actor_get(actor_index);
		if (forge_globals.ai_all_off && actor->state.mode != FORGE_ACTOR_MODE_BRAINDEAD)
			actor_braindead(actor_index, TRUE);
		else if (all_set && !forge_globals.ai_all_off && actor->state.mode == FORGE_ACTOR_MODE_BRAINDEAD)
			actor_braindead(actor_index, FALSE);
	}
}

/* every frame: a placed character with its AI off kept as it stands (its
actor braindead again if anything woke it), facing where the editor
turned it - a braindead unit keeps the facing and aim it had when its AI
went off and would turn back to them - and the game's own characters at
the ALL AI setting */
static void forge_ai_hold(
	void)
{
	short index;

	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed const *placed = &forge_globals.placed[index];
		struct unit_datum *unit;
		long actor_index;
		real_vector3d forward;

		if (!placed->ai || !(placed->flags & FORGE_RECORD_AI_OFF) || !forge_object_alive(placed->object_index))
			continue;
		unit = unit_get(placed->object_index);
		actor_index = forge_ai_actor(placed->object_index);
		if (actor_index == NONE || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			continue;
		if (actor_get(actor_index)->state.mode != FORGE_ACTOR_MODE_BRAINDEAD)
			actor_braindead(actor_index, TRUE);
		forward = unit->object.forward;
		forward.k = 0.f;
		if (normalize3d(&forward) < 0.0001f)
			continue;
		if (dot_product3d(&forward, &unit->unit.desired_facing_vector) < 0.9999f)
		{
			struct unit_control_data control;

			csmemset(&control, 0, sizeof(control));
			control.animation_state = _unit_animation_state_alert;
			control.throttle = *global_zero_vector3d;
			control.aiming_speed = 1;
			control.weapon_index = NONE;
			control.grenade_index = NONE;
			control.zoom_level = NONE;
			control.facing_vector = forward;
			control.aiming_vector = forward;
			control.looking_vector = forward;
			unit_control(placed->object_index, &control);
		}
	}
	forge_ai_unplaced_update(FALSE);
	/* (a Forge game: the humans the local player's allies, as every
	campaign map's script makes them (ai_allegiance player human) - the
	Marine AI pack's marines fight for the player - again after a new
	round, which starts the allegiances over) */
	if (forge_globals.game_active)
	{
		long player_index = local_player_get_player_index(0);
		short team = player_index != NONE ? (short)player_get(player_index)->team_index : NONE;

		if (team >= 0 && team < NUMBER_OF_SOLO_CAMPAIGN_TEAMS && team != _game_team_human &&
			game_team_is_enemy(team, _game_team_human))
		{
			ai_scripting_allegiance(team, _game_team_human);
			ai_update_team_status();
			platform_log("forge: the humans are the player's allies (team %d)", team);
		}
	}
}

/* every placed character's AI set (on or off), the setting kept for the
ones placed after it, said */
static void forge_ai_all_set(
	boolean on)
{
	short index;
	short count = 0;

	forge_globals.ai_all_off = !on;
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed *placed = &forge_globals.placed[index];

		if (!placed->ai)
			continue;
		if (on)
			placed->flags &= (unsigned char)~FORGE_RECORD_AI_OFF;
		else
			placed->flags |= FORGE_RECORD_AI_OFF;
		forge_ai_apply(placed);
		count++;
	}
	forge_ai_unplaced_update(TRUE);
	platform_log("forge: all AI %s (%d characters)", on ? "on" : "off", count);
	forge_menu_notice("%s", on ? T("AI: On") : T("AI: Off"));
}

/* (CLEAR DEAD AI, CLEAR ALL AI) the layout's dead characters, or every
one of them alive or dead, taken out of the layout and the world: they do
not come back with the next round, a map variant saved after has them not,
and the steps of the undo history that name them go (nothing brings a
cleared character back). A Flood combat form feigning death is not dead.
What the game spawned (a carrier's infection forms) is the game's, left */
static void forge_ai_clear(
	boolean all)
{
	short index;
	short cleared = 0;
	long cleared_ids[FORGE_MAXIMUM_OBJECTS];

	forge_placed_update();
	for (index = (short)(forge_globals.placed_count - 1); index >= 0; index--)
	{
		struct forge_placed *placed = &forge_globals.placed[index];

		if (!placed->ai || (!all && !forge_placed_dead(placed)))
			continue;
		if (placed->object_index != NONE && placed->object_index == forge_globals.held_object_index)
		{
			forge_globals.held_object_index = NONE;
			forge_globals.held_spawned = FALSE;
		}
		if (forge_object_alive(placed->object_index))
			object_delete(placed->object_index);
		cleared_ids[cleared++] = placed->id;
		forge_placed_remove(index);
	}
	/* (the history's steps of the cleared: gone, the cursor kept on the
	same step) */
	if (cleared > 0)
	{
		short kept = 0;
		short cursor = forge_globals.undo_cursor;

		for (index = 0; index < forge_globals.undo_count; index++)
		{
			short id;

			for (id = 0; id < cleared && forge_globals.undo[index].id != cleared_ids[id]; id++)
				;
			if (forge_globals.undo[index].id != NONE && id < cleared)
			{
				if (index < forge_globals.undo_cursor)
					cursor--;
				continue;
			}
			forge_globals.undo[kept++] = forge_globals.undo[index];
		}
		forge_globals.undo_count = kept;
		forge_globals.undo_cursor = (short)PIN(cursor, 0, kept);
	}
	forge_placed_update();
	platform_log("forge: cleared %d %s AI; budget %d/%d objects, %d/%d AI (%d dead)", cleared, all ? "placed" : "dead",
		forge_globals.placed_count, FORGE_MAXIMUM_OBJECTS, forge_globals.ai_count, FORGE_MAXIMUM_AI,
		forge_globals.ai_dead_count);
	if (!cleared)
		forge_menu_notice("%s", all ? T("No AI to clear") : T("No dead AI to clear"));
	else if (all)
		forge_menu_notice(T("Cleared all AI: %d characters"), cleared);
	else
		forge_menu_notice(T("Cleared %d dead AI"), cleared);
}

#ifndef HALO_RELEASE
/* (tests) the projectiles in the world fired by a character (a unit with
an actor), each counted once as it is first seen */
static void forge_ai_shots_update(
	void)
{
	struct object_iterator iterator;
	struct object_datum *object;

	object_iterator_new(&iterator, _object_mask_projectile, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		struct unit_datum *owner;
		short seen;

		if (object->object.owner_object_index == NONE)
			continue;
		owner = (struct unit_datum *)object_try_and_get(object->object.owner_object_index);
		if (!owner || !(FLAG(owner->object.type) & _object_mask_unit) ||
			(owner->unit.actor_index == NONE && owner->unit.swarm_actor_index == NONE))
		{
			continue;
		}
		for (seen = 0; seen < (short)NUMBEROF(forge_globals.ai_shot_seen); seen++)
		{
			if (forge_globals.ai_shot_seen[seen] == iterator.index)
				break;
		}
		if (seen < (short)NUMBEROF(forge_globals.ai_shot_seen))
			continue;
		forge_globals.ai_shot_seen[forge_globals.ai_shot_cursor] = iterator.index;
		forge_globals.ai_shot_cursor = (short)((forge_globals.ai_shot_cursor + 1) % NUMBEROF(forge_globals.ai_shot_seen));
		forge_globals.ai_shots++;
	}
}
#endif

static short forge_placed_of_tag(
	long tag_index)
{
	short index;
	short count = 0;

	for (index = 0; index < forge_globals.placed_count; index++)
	{
		if (forge_globals.placed[index].tag_index == tag_index && forge_object_alive(forge_globals.placed[index].object_index))
			count++;
	}
	return count;
}

/* ---- the controller */

static void forge_pad_get(
	struct forge_pad *pad)
{
	long player_index = local_player_get_player_index(forge_globals.local_player_index);
	short button;
	short stick;

	csmemset(pad, 0, sizeof(*pad));
	if (player_index != NONE)
	{
		short gamepad_index = player_get(player_index)->local_player_index;

		if (gamepad_index != NONE && input_has_gamepad(gamepad_index))
		{
			struct gamepad_state const *gamepad = input_get_gamepad_state(gamepad_index);
			float yaw, pitch;

			for (button = 0; button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
				pad->down[button] = gamepad->buttons[button] != 0;
			for (stick = 0; stick < NUMBER_OF_GAMEPAD_STICKS; stick++)
			{
				pad->sticks[stick][0] = (real)gamepad->sticks[stick].x / 32767.f;
				pad->sticks[stick][1] = (real)gamepad->sticks[stick].y / 32767.f;
			}
			/* (the mouse on the keyboard's port, the gyro on the Vita's) */
			if (halo_linux_mouse_look(gamepad_index, &yaw, &pitch))
			{
				pad->look_yaw = yaw;
				pad->look_pitch = pitch;
			}
			if (gamepad_index == 0)
			{
				float wheel = 0.f;

				pad->keys = halo_linux_forge_keys(&wheel);
				pad->wheel = wheel;
			}
		}
	}
	/* (the wheel holds Y a moment to switch weapons in play, Tab is Y too:
	here they push and pull, and flip to the Spartan, never delete) */
	if (pad->keys & (FORGE_KEY_WHEEL | FORGE_KEY_TAB))
		pad->down[_forge_button_delete] = FALSE;
	if (pad->keys & FORGE_KEY_DELETE)
		pad->down[_forge_button_delete] = TRUE;
	pad->toggle = forge_combo_held || (pad->keys & FORGE_KEY_TAB) != 0;
	/* (forge press, forge stick) */
	if (forge_globals.test_buttons_frames > 0)
	{
		for (button = 0; button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
			pad->down[button] |= forge_globals.test_down[button];
		pad->toggle |= forge_globals.test_toggle;
	}
	if (forge_globals.test_sticks_frames > 0)
	{
		for (stick = 0; stick < NUMBER_OF_GAMEPAD_STICKS; stick++)
		{
			pad->sticks[stick][0] = forge_globals.test_sticks[stick][0];
			pad->sticks[stick][1] = forge_globals.test_sticks[stick][1];
		}
	}
	for (stick = 0; stick < NUMBER_OF_GAMEPAD_STICKS; stick++)
	{
		short axis;

		for (axis = 0; axis < 2; axis++)
		{
			real value = pad->sticks[stick][axis];

			if (value > -FORGE_STICK_DEAD_ZONE && value < FORGE_STICK_DEAD_ZONE)
				value = 0.f;
			pad->sticks[stick][axis] = PIN(value, -1.f, 1.f);
		}
	}

	return;
}

/* ---- the monitor's world */

/* the camera's place and direction (the last frame's) */
static boolean forge_camera_get(
	real_point3d *position,
	real_vector3d *forward)
{
	struct observer_result const *camera = observer_get_camera(forge_globals.local_player_index);

	if (!camera)
		return FALSE;
	*position = camera->position;
	*forward = camera->forward;
	return TRUE;
}

/* whether object_index is the player's unit or carried by it */
static boolean forge_object_is_players(
	long object_index)
{
	long player_index = local_player_get_player_index(forge_globals.local_player_index);
	long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;

	return (forge_globals.unit_index != NONE && object_get_ultimate_parent(object_index) == forge_globals.unit_index) ||
		(unit_index != NONE && object_get_ultimate_parent(object_index) == unit_index);
}

/* how far along the camera's line the structure is (FORGE_PICK_RANGE if
nothing is that near) */
static real forge_structure_distance(
	real_point3d const *position,
	real_vector3d const *forward)
{
	struct collision_result collision;
	real_vector3d vector;

	vector.i = forward->i * FORGE_PICK_RANGE;
	vector.j = forward->j * FORGE_PICK_RANGE;
	vector.k = forward->k * FORGE_PICK_RANGE;
	if (collision_test_vector(
			FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
			position,
			&vector,
			NONE,
			&collision))
	{
		return collision.t * FORGE_PICK_RANGE;
	}
	return FORGE_PICK_RANGE;
}

/* the object under the crosshair: the first the camera's line meets - by
its collision model (a vehicle, a biped, scenery, a machine), or, for a
weapon or equipment on the ground, which has none a line would meet, by its
bounding sphere - before the structure; not the player's, nothing attached
to another, not the one held; NONE if none */
static long forge_object_under_crosshair(
	void)
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d vector;
	struct collision_result collision;
	real nearest = FORGE_PICK_RANGE;
	long nearest_index = NONE;
	long player_index = local_player_get_player_index(forge_globals.local_player_index);
	long ignored = forge_globals.unit_index != NONE ? forge_globals.unit_index :
		player_index != NONE ? player_get(player_index)->unit_index : NONE;
	struct object_iterator iterator;
	struct object_datum *object;

	if (!forge_camera_get(&position, &forward))
		return NONE;
	vector.i = forward.i * FORGE_PICK_RANGE;
	vector.j = forward.j * FORGE_PICK_RANGE;
	vector.k = forward.k * FORGE_PICK_RANGE;
	if (collision_test_vector(
			FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
			FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_bipeds_bit) |
			FLAG(_collision_test_objects_vehicles_bit) | FLAG(_collision_test_objects_scenery_bit) |
			FLAG(_collision_test_objects_machines_bit) | FLAG(_collision_test_objects_controls_bit),
			&position,
			&vector,
			ignored,
			&collision))
	{
		nearest = collision.t * FORGE_PICK_RANGE;
		if (collision.type == _collision_result_object && object_try_and_get(collision.object_index))
		{
			long hit = object_get_ultimate_parent(collision.object_index);

			if (!forge_object_is_players(hit) && hit != forge_globals.held_object_index)
				nearest_index = hit;
		}
	}
	/* (the items, nearer than that) */
	object_iterator_new(&iterator, _object_mask_weapon | _object_mask_equipment, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		real_vector3d to_center;
		real along;
		real across_squared;
		real radius = MAX(object->object.bounding_sphere_radius, 0.2f);
		real entry;

		if (object->object.parent_object_index != NONE || forge_object_is_players(iterator.index) ||
			iterator.index == forge_globals.held_object_index)
		{
			continue;
		}
		to_center.i = object->object.bounding_sphere_center.x - position.x;
		to_center.j = object->object.bounding_sphere_center.y - position.y;
		to_center.k = object->object.bounding_sphere_center.z - position.z;
		along = dot_product3d(&to_center, &forward);
		across_squared = dot_product3d(&to_center, &to_center) - along * along;
		if (along < 0.f || across_squared > radius * radius)
			continue;
		entry = along - square_root(radius * radius - across_squared);
		if (entry < nearest)
		{
			nearest = entry;
			nearest_index = iterator.index;
		}
	}

	return nearest_index;
}

static void forge_undo_record(short kind, long object_index, struct forge_transform const *from);
static void forge_snapped(real_point3d *position, real_vector3d *forward, real_vector3d *up);

/* the held object where the camera holds it */
static void forge_held_object_place(
	void)
{
	struct object_datum *object = object_try_and_get(forge_globals.held_object_index);
	real_point3d position;
	real_vector3d forward;

	if (!object)
	{
		forge_globals.held_object_index = NONE;
		return;
	}
	if (!forge_camera_get(&position, &forward))
		return;
	position.x += forward.i * forge_globals.held_distance;
	position.y += forward.j * forge_globals.held_distance;
	position.z += forward.k * forge_globals.held_distance;
	{
		real_vector3d facing = forge_globals.held_forward;
		real_vector3d up = forge_globals.held_up;

		forge_snapped(&position, &facing, &up);
		object_set_position(forge_globals.held_object_index, &position, &facing, &up);
	}
	/* (it hangs where it is held: what the physics gave it this tick
	goes) */
	object->object.translational_velocity = *global_zero_vector3d;
	object->object.angular_velocity = *global_zero_vector3d;

	return;
}

static void forge_grab(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	real_point3d position;
	real_vector3d forward;
	char name[FORGE_NAME_SIZE];

	if (!object || !forge_camera_get(&position, &forward))
		return;
	forge_globals.held_object_index = object_index;
	forge_globals.held_forward = object->object.forward;
	forge_globals.held_up = object->object.up;
	{
		real_vector3d to_object;

		to_object.i = object->object.position.x - position.x;
		to_object.j = object->object.position.y - position.y;
		to_object.k = object->object.position.z - position.z;
		forge_globals.held_distance = PIN(
			dot_product3d(&to_object, &forward),
			FORGE_MINIMUM_HOLD_DISTANCE,
			FORGE_MAXIMUM_HOLD_DISTANCE);
	}
	forge_transform_get(object_index, &forge_globals.held_from);
	forge_globals.held_spawned = FALSE;
	forge_object_name(object_index, name, sizeof(name));
	platform_log("forge: grabbed %s (%s)", name, forge_tag_leaf(object->definition_index));

	return;
}

static void forge_drop(
	void)
{
	struct object_datum *object = object_try_and_get(forge_globals.held_object_index);

	if (object)
	{
		char name[FORGE_NAME_SIZE];

		/* (snapped, its facing is the one shown) */
		if (forge_globals.snap)
		{
			real_point3d position = object->object.position;

			forge_snapped(&position, &forge_globals.held_forward, &forge_globals.held_up);
		}
		/* (a placing, or a move, in the undo history) */
		if (forge_globals.held_spawned)
			forge_undo_record(_forge_undo_place, forge_globals.held_object_index, NULL);
		else
			forge_undo_record(_forge_undo_move, forge_globals.held_object_index, &forge_globals.held_from);
		/* (it falls from where it was let go: its placed spot where it
		comes to rest) */
		SET_FLAG(object->object.flags, _object_at_rest_bit, FALSE);
		forge_settle(forge_globals.held_object_index);
		forge_object_name(forge_globals.held_object_index, name, sizeof(name));
		platform_log("forge: dropped %s (%s) at %.2f %.2f %.2f facing %.2f %.2f %.2f", name,
			forge_tag_leaf(object->definition_index), object->object.position.x, object->object.position.y,
			object->object.position.z, object->object.forward.i, object->object.forward.j, object->object.forward.k);
	}
	forge_globals.held_object_index = NONE;
	forge_globals.held_spawned = FALSE;

	return;
}

/* the held object, or the one under the crosshair */
static long forge_target_object(
	void)
{
	if (object_try_and_get(forge_globals.held_object_index))
		return forge_globals.held_object_index;
	return forge_object_under_crosshair();
}

/* an object's facing set: the one held keeps it as it is held */
static void forge_set_facing(
	long object_index,
	real_vector3d const *forward,
	real_vector3d const *up)
{
	struct object_datum *object = object_try_and_get(object_index);

	if (!object)
		return;
	if (object_index == forge_globals.held_object_index)
	{
		forge_globals.held_forward = *forward;
		forge_globals.held_up = *up;
	}
	else
	{
		struct forge_transform from;

		forge_transform_get(object_index, &from);
		object_set_position(object_index, &object->object.position, forward, up);
		SET_FLAG(object->object.flags, _object_at_rest_bit, FALSE);
		forge_undo_record(_forge_undo_move, object_index, &from);
		forge_settle(object_index);
	}
}

static void forge_turn(
	long object_index,
	real angle)
{
	struct object_datum *object = object_try_and_get(object_index);
	real_vector3d forward;
	real_vector3d up;
	real_vector3d axis = *global_up3d;

	if (!object)
		return;
	forward = object_index == forge_globals.held_object_index ? forge_globals.held_forward : object->object.forward;
	up = object_index == forge_globals.held_object_index ? forge_globals.held_up : object->object.up;
	rotate_vector_about_axis(&forward, &axis, sine(angle), cosine(angle));
	rotate_vector_about_axis(&up, &axis, sine(angle), cosine(angle));
	forge_set_facing(object_index, &forward, &up);

	return;
}

/* ---- rotate mode (L held while holding an object): the object turned
about the camera's view - yaw about the world's up, pitch about the
camera's left along the ground, roll about its forward along the ground -
smoothly, or in steps of the snapping D-pad right picks (Off, 15, 45, 90
degrees): one step a flick of the stick */

static void forge_angles_from_facing(real_vector3d const *forward, real_vector3d const *up, float angles[3]);
static void forge_facing_from_angles(float const angles[3], real_vector3d *forward, real_vector3d *up);
static real forge_round(real value);

static real forge_absolute(
	real value)
{
	return value < 0.f ? -value : value;
}

/* the axis an axis's turn is about: the world's up (a positive turn yaws
the object left, counterclockwise seen from above); the camera's left
along the ground (it pitches the object's top away from the camera, the
way the stick is pushed); the camera's forward along the ground (it rolls
the top to the right). Snapping, the camera's nearest of the world's own
axes, so that the steps keep the object square with the map */
static void forge_rotate_axis(
	short axis,
	real_vector3d *vector)
{
	real_point3d position;
	real_vector3d forward;

	if (axis == _forge_axis_yaw)
	{
		*vector = *global_up3d;
		return;
	}
	if (!forge_camera_get(&position, &forward))
		forward = *global_forward3d;
	forward.k = 0.f;
	if (normalize3d(&forward) < 0.0001f)
		forward = *global_forward3d;
	if (forge_rotate_snaps[forge_globals.rotate_snap] > 0)
	{
		if (forge_absolute(forward.i) >= forge_absolute(forward.j))
		{
			forward.i = forward.i < 0.f ? -1.f : 1.f;
			forward.j = 0.f;
		}
		else
		{
			forward.j = forward.j < 0.f ? -1.f : 1.f;
			forward.i = 0.f;
		}
	}
	if (axis == _forge_axis_pitch)
	{
		vector->i = -forward.j;
		vector->j = forward.i;
		vector->k = 0.f;
	}
	else
	{
		*vector = forward;
	}
}

/* the object held turned by angle (radians) about one of rotate mode's
axes, its facing kept square (forward and up at right angles, each of
length one) */
static void forge_rotate_held(
	short axis_kind,
	real angle)
{
	real_vector3d axis;
	real_vector3d forward = forge_globals.held_forward;
	real_vector3d up = forge_globals.held_up;
	real along;

	if (angle == 0.f)
		return;
	forge_rotate_axis(axis_kind, &axis);
	rotate_vector_about_axis(&forward, &axis, sine(angle), cosine(angle));
	rotate_vector_about_axis(&up, &axis, sine(angle), cosine(angle));
	if (normalize3d(&forward) < 0.0001f)
		return;
	along = dot_product3d(&up, &forward);
	up.i -= forward.i * along;
	up.j -= forward.j * along;
	up.k -= forward.k * along;
	if (normalize3d(&up) < 0.0001f)
		return;
	forge_globals.held_forward = forward;
	forge_globals.held_up = up;
}

/* (each frame of rotate mode) the right stick, or the mouse, turns the
object held: across yaws it, up and down pitch it; with R held too, across
rolls it. Snapping, a flick (past FORGE_ROTATE_FLICK, let back under
FORGE_ROTATE_REARM) or FORGE_ROTATE_MOUSE_STEP of the mouse turns it one
step. The left stick pushes it away (up) and pulls it nearer (down): the
camera stays while it turns */
static void forge_rotate_update(
	struct forge_pad const *pad,
	real seconds)
{
	boolean roll = pad->down[_forge_button_grab];
	real step = DEGREES_TO_RADIANS((real)forge_rotate_snaps[forge_globals.rotate_snap]);
	real stick[NUMBER_OF_FORGE_AXES];
	real mouse[NUMBER_OF_FORGE_AXES];
	short axis;

	stick[_forge_axis_yaw] = roll ? 0.f : -pad->sticks[_gamepad_stick_right][0];
	stick[_forge_axis_pitch] = roll ? 0.f : pad->sticks[_gamepad_stick_right][1];
	stick[_forge_axis_roll] = roll ? pad->sticks[_gamepad_stick_right][0] : 0.f;
	mouse[_forge_axis_yaw] = roll ? 0.f : forge_globals.pending_mouse_turn;
	mouse[_forge_axis_pitch] = roll ? 0.f : forge_globals.pending_mouse_pitch;
	mouse[_forge_axis_roll] = roll ? -forge_globals.pending_mouse_turn : 0.f;
	for (axis = 0; axis < NUMBER_OF_FORGE_AXES; axis++)
	{
		if (step <= 0.f)
		{
			forge_rotate_held(axis, stick[axis] * FORGE_ROTATE_RATE * seconds + mouse[axis] * FORGE_MOUSE_TURN);
			continue;
		}
		if (forge_globals.rotate_flicked[axis])
		{
			if (forge_absolute(stick[axis]) < FORGE_ROTATE_REARM)
				forge_globals.rotate_flicked[axis] = FALSE;
		}
		else if (forge_absolute(stick[axis]) > FORGE_ROTATE_FLICK)
		{
			forge_globals.rotate_flicked[axis] = TRUE;
			forge_rotate_held(axis, stick[axis] > 0.f ? step : -step);
		}
		forge_globals.rotate_mouse[axis] += mouse[axis];
		while (forge_globals.rotate_mouse[axis] >= FORGE_ROTATE_MOUSE_STEP)
		{
			forge_globals.rotate_mouse[axis] -= FORGE_ROTATE_MOUSE_STEP;
			forge_rotate_held(axis, step);
		}
		while (forge_globals.rotate_mouse[axis] <= -FORGE_ROTATE_MOUSE_STEP)
		{
			forge_globals.rotate_mouse[axis] += FORGE_ROTATE_MOUSE_STEP;
			forge_rotate_held(axis, -step);
		}
	}
	forge_globals.held_distance += pad->sticks[_gamepad_stick_left][1] * FORGE_PUSH_RATE * seconds;
}

/* rotate mode's snapping in words: "Off", "45°" */
static void forge_rotate_snap_text(
	char *text,
	size_t size)
{
	short degrees = forge_rotate_snaps[forge_globals.rotate_snap];

	if (degrees > 0)
		snprintf(text, size, "%d\xc2\xb0", degrees);
	else
		snprintf(text, size, "%s", T("Off"));
}

/* an object upright, facing along the ground as it faces */
static void forge_straighten(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	real_vector3d forward;
	char name[FORGE_NAME_SIZE];

	if (!object)
		return;
	forward = object_index == forge_globals.held_object_index ? forge_globals.held_forward : object->object.forward;
	forward.k = 0.f;
	if (normalize3d(&forward) < 0.0001f)
		forward = *global_forward3d;
	forge_set_facing(object_index, &forward, global_up3d);
	forge_object_name(object_index, name, sizeof(name));
	forge_notice(T("%s set upright"), name);

	return;
}

static void forge_delete(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	char name[FORGE_NAME_SIZE];

	if (!object || forge_object_is_players(object_index))
		return;
	forge_object_name(object_index, name, sizeof(name));
	platform_log("forge: deleted %s (%s)", name, forge_tag_leaf(object->definition_index));
	forge_notice(T("Deleted %s"), name);
	forge_undo_record(_forge_undo_delete, object_index, NULL);
	if (object_index == forge_globals.held_object_index)
		forge_globals.held_object_index = NONE;
	/* (out of the layout: not kept as gone in play) */
	forge_placed_forget_object(object_index);
	object_delete(object_index);
	forge_globals.deleted_count++;
	forge_placed_update();

	return;
}

/* why forge_place placed nothing */
enum
{
	_forge_place_ok,
	_forge_place_budget,
	_forge_place_ai_budget,
	_forge_place_refused,
};

/* a palette tag placed at position, facing forward with up up, within the
budget (an actor variant: its unit, given a free actor); the object, or
NONE with why in *refusal (the player told unless quiet) */
static long forge_place(
	long tag_index,
	real_point3d const *position,
	real_vector3d const *forward,
	real_vector3d const *up,
	boolean quiet,
	short *refusal)
{
	struct object_placement_data placement;
	long definition_index = tag_index;
	long actor_variant_index = NONE;
	long object_index;
	short entry = forge_palette_of_tag(tag_index);
	char name[FORGE_NAME_SIZE];

	*refusal = _forge_place_ok;
	if (entry != NONE)
		snprintf(name, sizeof(name), "%s", forge_globals.palette[entry].name);
	else
		forge_tag_readable_name(tag_index, name, sizeof(name));
	/* (the budget) */
	forge_placed_update();
	if (forge_globals.placed_count >= FORGE_MAXIMUM_OBJECTS)
	{
		*refusal = _forge_place_budget;
		if (!quiet)
			forge_notice(T("Budget full: %d objects. Delete one to place another."), FORGE_MAXIMUM_OBJECTS);
		return NONE;
	}
	/* (a character: its actor variant's unit, then a free actor of that
	variant for it) */
	if (tag_get_group_tag(tag_index) == ACTOR_VARIANT_DEFINITION_TAG)
	{
		/* (the dead keep their places: they come back with the next
		round, which must have room for them; CLEAR DEAD AI frees them) */
		if (forge_globals.ai_count >= FORGE_MAXIMUM_AI)
		{
			*refusal = _forge_place_ai_budget;
			if (!quiet && forge_globals.ai_dead_count > 0)
				forge_notice(T("AI budget full: %d characters, %d of them dead. Clear dead AI to place more."),
					FORGE_MAXIMUM_AI, forge_globals.ai_dead_count);
			else if (!quiet)
				forge_notice(T("AI budget full: %d characters."), FORGE_MAXIMUM_AI);
			return NONE;
		}
		actor_variant_index = tag_index;
		definition_index = actor_variant_definition_get(tag_index)->unit_reference.index;
		if (definition_index == NONE)
		{
			*refusal = _forge_place_refused;
			if (!quiet)
				forge_notice(T("%s cannot be placed here"), name);
			return NONE;
		}
	}
	object_placement_data_new(&placement, definition_index, NONE);
	SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
	placement.position = *position;
	placement.forward = *forward;
	placement.up = *up;
	forge_placing_exact = 1;
	object_index = object_new(&placement);
	forge_placing_exact = 0;
	if (object_index == NONE)
	{
		*refusal = _forge_place_refused;
		if (!quiet)
			forge_notice(T("%s cannot be placed here"), name);
		return NONE;
	}
	if (actor_variant_index != NONE)
	{
		long actor_definition_index = actor_variant_definition_get(actor_variant_index)->actor_reference.index;

		/* (its variant's weapon, grenades, vitality, colours and
		camouflage, as a map's own characters are given them as they are
		placed (actor_place): an elite major needler with its needler, a
		stealth elite cloaked, a Flood combat form with its shotgun) */
		if (actor_definition_index != NONE)
			actor_customize_unit(actor_variant_index, object_index);
		/* (a swarm's - a Flood infection form - which ai_attach_free will
		not take: a swarm of its own, of no encounter, as a carrier's
		infection forms are made a swarm (actors_spawn_from_unit); a squad
		given so that it never joins another placed swarm, whose AI
		settings are its own) */
		if (actor_definition_index != NONE &&
			TEST_FLAG(actor_definition_get(actor_definition_index)->flags, _actor_definition_swarm_actor_bit))
		{
			actor_create_for_unit(TRUE, object_index, actor_variant_index, NONE, 0, FALSE, NONE, FALSE,
				FORGE_ACTOR_DEFAULT_STATE_ALERT, FORGE_ACTOR_DEFAULT_STATE_NONE, NONE, 0);
			platform_log("forge: %s given a swarm of its own (%lx)", forge_tag_leaf(definition_index),
				(unsigned long)unit_get(object_index)->unit.swarm_actor_index);
		}
		else
		{
			ai_scripting_attach_free(object_index, actor_variant_index);
			platform_log("forge: %s given a free actor (%lx)", forge_tag_leaf(definition_index),
				(unsigned long)unit_get(object_index)->unit.actor_index);
		}
	}
	forge_placed_add(object_index, tag_index, actor_variant_index != NONE);
	/* (a character's team, its default, at once: a multiplayer game's
	units are given none by the engine; its AI off while ALL AI is) */
	if (actor_variant_index != NONE)
	{
		struct forge_placed *placed = forge_placed_of_object(object_index);

		if (placed && forge_globals.ai_all_off)
			placed->flags |= FORGE_RECORD_AI_OFF;
		forge_ai_apply(placed);
	}
	forge_globals.spawned_count++;
	if (!quiet)
	{
		platform_log("forge: spawned %s (%s, %s) at %.2f %.2f %.2f facing %.2f %.2f %.2f; budget %d/%d objects, %d/%d AI",
			name, forge_tag_leaf(tag_index), entry != NONE ? forge_category_log_names[forge_globals.palette[entry].category] : "?",
			placement.position.x, placement.position.y, placement.position.z, placement.forward.i, placement.forward.j,
			placement.forward.k, forge_globals.placed_count, FORGE_MAXIMUM_OBJECTS, forge_globals.ai_count, FORGE_MAXIMUM_AI);
		forge_notice(T("Placed %s"), name);
	}

	return object_index;
}

/* a palette tag spawned in front of the camera (before the structure),
and held, within the budget; the object, or NONE */
static long forge_spawn(
	long tag_index)
{
	real_point3d position;
	real_vector3d forward;
	real_point3d point;
	real_vector3d facing;
	long object_index;
	real distance;
	short refusal;

	if (!forge_camera_get(&position, &forward))
		return NONE;
	distance = MIN(4.f, forge_structure_distance(&position, &forward) - 1.f);
	distance = MAX(distance, FORGE_MINIMUM_HOLD_DISTANCE);
	point.x = position.x + forward.i * distance;
	point.y = position.y + forward.j * distance;
	point.z = position.z + forward.k * distance;
	/* (facing the camera, upright) */
	facing.i = -forward.i;
	facing.j = -forward.j;
	facing.k = 0.f;
	if (normalize3d(&facing) < 0.0001f)
		facing = *global_forward3d;
	object_index = forge_place(tag_index, &point, &facing, global_up3d, FALSE, &refusal);
	if (object_index == NONE)
		return NONE;
	forge_globals.held_object_index = object_index;
	forge_globals.held_distance = distance;
	forge_globals.held_forward = facing;
	forge_globals.held_up = *global_up3d;
	forge_globals.held_spawned = TRUE;

	return object_index;
}

/* a copy of the object held or under the crosshair, held where it was,
facing as it faced (the original left there) */
static void forge_duplicate(
	void)
{
	long object_index = forge_target_object();
	struct object_datum *object = object_try_and_get(object_index);
	long tag_index = NONE;
	real_vector3d forward, up;
	real distance = forge_globals.held_distance;
	boolean held = object_index == forge_globals.held_object_index;
	struct forge_placed original;
	boolean has_original = FALSE;
	short index;

	if (!object)
		return;
	for (index = 0; index < forge_globals.placed_count; index++)
	{
		if (forge_globals.placed[index].object_index == object_index)
		{
			tag_index = forge_globals.placed[index].tag_index;
			original = forge_globals.placed[index];
			has_original = TRUE;
		}
	}
	if (tag_index == NONE && forge_palette_of_tag(object->definition_index) != NONE)
		tag_index = object->definition_index;
	if (tag_index == NONE)
	{
		char name[FORGE_NAME_SIZE];

		forge_object_name(object_index, name, sizeof(name));
		forge_notice(T("%s cannot be copied"), name);
		return;
	}
	forward = held ? forge_globals.held_forward : object->object.forward;
	up = held ? forge_globals.held_up : object->object.up;
	if (held)
		forge_drop();
	if (forge_spawn(tag_index) != NONE)
	{
		struct forge_placed *copy = forge_placed_of_object(forge_globals.held_object_index);

		forge_globals.held_forward = forward;
		forge_globals.held_up = up;
		if (held)
			forge_globals.held_distance = distance;
		/* (the copy keeps the original's properties - team, respawn, place at
		start, a character's AI team and AI on or off - not the defaults: the
		owner's AI teams were reset by every copy) */
		if (copy && has_original)
		{
			copy->team = original.team;
			copy->flags = original.flags;
			copy->respawn_seconds = original.respawn_seconds;
			copy->ai_team = original.ai_team;
			forge_ai_apply(copy);
		}
	}
}

/* the player's unit and what it carries shown or hidden */
static void forge_unit_set_visible(
	long unit_index,
	boolean visible)
{
	struct object_datum *unit = object_try_and_get(unit_index);
	long child_index;

	if (!unit)
		return;
	object_set_visibility(unit_index, visible);
	for (child_index = unit->object.first_child_object_index; child_index != NONE;)
	{
		struct object_datum *child = object_try_and_get(child_index);

		if (!child)
			break;
		object_set_visibility(child_index, visible);
		child_index = child->object.next_object_index;
	}

	return;
}

/* the player's unit as the monitor leaves it: hidden, unhurt (whatever it
was is kept for the Spartan) */
static void forge_unit_hide(
	long unit_index)
{
	struct object_datum *unit = object_try_and_get(unit_index);

	forge_globals.unit_index = unit_index;
	if (!unit)
		return;
	forge_globals.unit_could_take_damage = !TEST_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit);
	SET_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit, TRUE);
	forge_unit_set_visible(unit_index, FALSE);
}

static void forge_hud_title_update(
	void);

static boolean forge_begin(
	void)
{
	long player_index;
	real_point3d position;
	real_vector3d forward;

	if (forge_globals.active)
		return TRUE;
	if (game_connection() != _game_connection_local)
	{
		platform_log("forge: only in a local game");
		return FALSE;
	}
	forge_globals.local_player_index = 0;
	player_index = local_player_get_player_index(forge_globals.local_player_index);
	if (player_index == NONE || !forge_camera_get(&position, &forward))
	{
		platform_log("forge: no local player");
		return FALSE;
	}
	forge_unit_hide(player_get(player_index)->unit_index);
	/* (D-pad up zooms as the Spartan: unzoomed for the monitor) */
	players_unzoom_all();
	forge_palette_ready();
	forge_globals.held_object_index = NONE;
	director_forge_camera(forge_globals.local_player_index, TRUE, &position, &forward);
	forge_globals.active = TRUE;
	forge_monitor_flying = 1;
	forge_hud_title_update();
	platform_log("forge: monitor mode at %.2f %.2f %.2f (%d objects in the palette)", position.x, position.y, position.z,
		forge_globals.palette_count);

	return TRUE;
}

static void forge_end(
	void)
{
	struct object_datum *unit;
	real_point3d position;
	real_vector3d forward;

	if (!forge_globals.active)
		return;
	if (object_try_and_get(forge_globals.held_object_index))
		forge_drop();
	unit = object_try_and_get(forge_globals.unit_index);
	if (unit)
	{
		forge_unit_set_visible(forge_globals.unit_index, TRUE);
		if (forge_globals.unit_could_take_damage)
			SET_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit, FALSE);
		/* (the player stands where the monitor was, as Halo 3's Spartan
		does; not one in a vehicle's seat) */
		if (unit->object.parent_object_index == NONE && forge_camera_get(&position, &forward))
		{
			object_set_position(forge_globals.unit_index, &position, NULL, NULL);
			unit->object.translational_velocity = *global_zero_vector3d;
		}
	}
	director_forge_camera(forge_globals.local_player_index, FALSE, NULL, NULL);
	forge_globals.active = FALSE;
	forge_monitor_flying = 0;
	if (unit)
	{
		platform_log("forge: player mode at %.2f %.2f %.2f, %s (%ld spawned, %ld deleted)", unit->object.position.x,
			unit->object.position.y, unit->object.position.z,
			TEST_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit) ? "unhurt" : "can be hurt",
			forge_globals.spawned_count, forge_globals.deleted_count);
	}
	else
	{
		platform_log("forge: player mode, waiting to respawn (%ld spawned, %ld deleted)", forge_globals.spawned_count,
			forge_globals.deleted_count);
	}
	forge_globals.unit_index = NONE;

	return;
}

/* the monitor's HUD's first line: Forge and the map's name */
static void forge_hud_title_update(
	void)
{
	char const *scenario = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
	char const *name = NULL;
	short map_index;
	char words[FORGE_NAME_SIZE];

	for (map_index = 0; scenario && map_index < forge_map_count(); map_index++)
	{
		if (!_stricmp(scenario, forge_maps[map_index].path))
			name = T(forge_maps[map_index].name);
	}
	if (!name)
	{
		char const *slash = scenario ? strrchr(scenario, '\\') : NULL;

		forge_name_words(slash ? slash + 1 : scenario ? scenario : "", words, sizeof(words));
		name = words;
	}
	snprintf(forge_globals.hud_title, sizeof(forge_globals.hud_title), "%s \xc2\xb7 %s", T("FORGE"), name);
}

/* the monitor's HUD's other lines: the budget, and the object held or
under the crosshair */
static void forge_hud_update(
	void)
{
	long object_index;

	/* (the AI alive; the dead apart, back with the next round) */
	if (forge_globals.ai_dead_count > 0)
		snprintf(forge_globals.hud_budget, sizeof(forge_globals.hud_budget), T("Objects %d/%d, AI %d/%d, %d back next round"),
			forge_globals.placed_count, FORGE_MAXIMUM_OBJECTS, forge_globals.ai_count - forge_globals.ai_dead_count,
			FORGE_MAXIMUM_AI, forge_globals.ai_dead_count);
	else
		snprintf(forge_globals.hud_budget, sizeof(forge_globals.hud_budget), T("Objects %d/%d, AI %d/%d"),
			forge_globals.placed_count, FORGE_MAXIMUM_OBJECTS, forge_globals.ai_count, FORGE_MAXIMUM_AI);
	forge_globals.hud_label[0] = 0;
	forge_globals.hud_label_held = object_try_and_get(forge_globals.held_object_index) != NULL;
	object_index = forge_globals.hud_label_held ? forge_globals.held_object_index : forge_object_under_crosshair();
	if (object_index != NONE)
		forge_object_name(object_index, forge_globals.hud_label, sizeof(forge_globals.hud_label));
	/* (holding one: rotate mode's snapping; rotating, its angles too) */
	forge_globals.hud_rotate[0] = 0;
	if (forge_globals.hud_label_held)
	{
		char snap[16];
		char snapping[64];

		forge_rotate_snap_text(snap, sizeof(snap));
		snprintf(snapping, sizeof(snapping), T("Rotation snap: %s"), snap);
		if (forge_globals.rotating)
		{
			float angles[3];

			forge_angles_from_facing(&forge_globals.held_forward, &forge_globals.held_up, angles);
			snprintf(forge_globals.hud_rotate, sizeof(forge_globals.hud_rotate), T("Yaw %d, pitch %d, roll %d. %s"),
				(int)forge_round(RADIANS_TO_DEGREES(angles[_forge_axis_yaw])),
				(int)forge_round(RADIANS_TO_DEGREES(angles[_forge_axis_pitch])),
				(int)forge_round(RADIANS_TO_DEGREES(angles[_forge_axis_roll])), snapping);
		}
		else
		{
			snprintf(forge_globals.hud_rotate, sizeof(forge_globals.hud_rotate), "%s", snapping);
		}
	}
}

/* whether a screen of the menus is open for the monitor's player (the
browser, the Forge menu): the monitor's buttons are the menu's then */
static boolean forge_menu_open(
	void)
{
	return ui_widget_port_active_screen(forge_globals.local_player_index) != NULL;
}

/* the object browser opened over the game (it does not pause it, as Halo
3's does not) */
static void forge_browser_open(
	void)
{
	long screen = menu_tags_ingame_screen(FORGE_BROWSER_SCREEN);

	if (screen == NONE ||
		!ui_widget_load_by_name_or_tag(NULL, screen, NULL, forge_globals.local_player_index, NONE, NONE, NONE))
	{
		forge_notice("%s", T("The object browser needs the Halo PC files (the Halo 3 menus)"));
		return;
	}
	platform_log("forge: the object browser opened");
}

/* (Start in a Forge game) the Forge menu: a multiplayer map has no pause
screen of a game played alone (ui_widget.c ui_check_for_pause_game opens
none), so it is opened here; where the map has one, menu_tags.c opens this
in its place, and the menu is open by now */
static void forge_menu_open_pause(
	void)
{
	long screen = menu_tags_ingame_screen(FORGE_MENU_SCREEN);

	if (screen == NONE ||
		!ui_widget_load_by_name_or_tag(NULL, screen, NULL, forge_globals.local_player_index, NONE, NONE, NONE))
	{
		return;
	}
	platform_log("forge: the Forge menu opened");
}

/* ---- undo and redo: a bounded history of what the monitor did (placed,
deleted, moved or turned an object), FORGE_UNDO_DEPTH steps. An object is
named by its placed id (forge_placed: the same id again when an undo
brings it back) or, for one of the map's own, by its object index. What
cannot be undone any more (the object destroyed, the budget full) is said
and skipped, never guessed at */

static void forge_transform_get(
	long object_index,
	struct forge_transform *transform)
{
	struct object_datum *object = object_get(object_index);

	transform->position = object->object.position;
	if (object_index == forge_globals.held_object_index)
	{
		transform->forward = forge_globals.held_forward;
		transform->up = forge_globals.held_up;
	}
	else
	{
		transform->forward = object->object.forward;
		transform->up = object->object.up;
	}
}

static boolean forge_transforms_equal(
	struct forge_transform const *a,
	struct forge_transform const *b)
{
	return distance_squared3d(&a->position, &b->position) < 0.0001f &&
		dot_product3d(&a->forward, &b->forward) > 0.99999f && dot_product3d(&a->up, &b->up) > 0.99999f;
}

/* the object a history step names, NONE if it is gone */
static long forge_undo_object(
	struct forge_undo const *entry)
{
	short index;

	if (entry->id == NONE)
		return forge_object_alive(entry->object_index) ? entry->object_index : NONE;
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		if (forge_globals.placed[index].id == entry->id)
			return forge_object_alive(forge_globals.placed[index].object_index) ? forge_globals.placed[index].object_index : NONE;
	}
	return NONE;
}

/* a step of an object filled: what names it, its tag, its properties */
static boolean forge_undo_fill(
	struct forge_undo *entry,
	short kind,
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	struct forge_placed *placed = forge_placed_of_object(object_index);

	if (!object)
		return FALSE;
	csmemset(entry, 0, sizeof(*entry));
	entry->kind = kind;
	entry->id = placed ? placed->id : NONE;
	entry->object_index = object_index;
	entry->tag_index = placed ? placed->tag_index : object->definition_index;
	entry->team = placed ? placed->team : NONE;
	entry->flags = placed ? placed->flags : FORGE_RECORD_PLACE_AT_START;
	entry->respawn_seconds = placed ? placed->respawn_seconds : FORGE_DEFAULT_RESPAWN_SECONDS;
	entry->ai_team = placed ? placed->ai_team : FORGE_AI_TEAM_DEFAULT;
	forge_transform_get(object_index, &entry->from);
	entry->to = entry->from;
	return TRUE;
}

static void forge_undo_push(
	struct forge_undo const *entry)
{
	/* (a new step: what could be redone is gone; the oldest goes when full) */
	forge_globals.undo_count = forge_globals.undo_cursor;
	if (forge_globals.undo_count >= FORGE_UNDO_DEPTH)
	{
		csmemmove(&forge_globals.undo[0], &forge_globals.undo[1], sizeof(forge_globals.undo[0]) * (FORGE_UNDO_DEPTH - 1));
		forge_globals.undo_count = FORGE_UNDO_DEPTH - 1;
	}
	forge_globals.undo[forge_globals.undo_count++] = *entry;
	forge_globals.undo_cursor = forge_globals.undo_count;
}

static void forge_undo_record(
	short kind,
	long object_index,
	struct forge_transform const *from)
{
	struct forge_undo entry;

	if (!forge_undo_fill(&entry, kind, object_index))
		return;
	if (from)
		entry.from = *from;
	if (kind == _forge_undo_move && forge_transforms_equal(&entry.from, &entry.to))
		return;
	forge_undo_push(&entry);
}

/* a step's words: "Place Warthog" */
static void forge_undo_describe(
	struct forge_undo const *entry,
	char *text,
	size_t size)
{
	static char const *const kinds[NUMBER_OF_FORGE_UNDO_KINDS] = { N_("Place %s"), N_("Delete %s"), N_("Move %s") };
	char name[FORGE_NAME_SIZE];
	short palette = forge_palette_of_tag(entry->tag_index);

	if (palette != NONE)
		snprintf(name, sizeof(name), "%s", forge_globals.palette[palette].name);
	else
		forge_tag_readable_name(entry->tag_index, name, sizeof(name));
	snprintf(text, size, T(kinds[PIN(entry->kind, 0, NUMBER_OF_FORGE_UNDO_KINDS - 1)]), name);
}

/* a step's object brought back at a place: under its id again (a map
object's steps given the new one's id); FALSE when it cannot be */
static boolean forge_undo_bring_back(
	struct forge_undo *entry,
	struct forge_transform const *transform)
{
	long object_index;
	short refusal;
	struct forge_placed *placed;

	/* (an entry of its id gone in play is this object: it comes back as
	itself, not twice) */
	forge_placed_forget_id(entry->id);
	object_index = forge_place(entry->tag_index, &transform->position, &transform->forward, &transform->up, TRUE, &refusal);
	if (object_index == NONE)
		return FALSE;
	placed = forge_placed_of_object(object_index);
	if (placed)
	{
		placed->team = entry->team;
		placed->flags = entry->flags;
		placed->respawn_seconds = entry->respawn_seconds;
		placed->ai_team = entry->ai_team;
		forge_ai_apply(placed);
		if (entry->id != NONE)
		{
			placed->id = entry->id;
		}
		else
		{
			short index;
			long old_object_index = entry->object_index;

			/* (a map object back as a placed one: every step of it named
			by its new id) */
			for (index = 0; index < forge_globals.undo_count; index++)
			{
				if (forge_globals.undo[index].id == NONE && forge_globals.undo[index].object_index == old_object_index)
					forge_globals.undo[index].id = placed->id;
			}
		}
	}
	return TRUE;
}

static void forge_undo_set_transform(
	long object_index,
	struct forge_transform const *transform)
{
	struct object_datum *object = object_get(object_index);

	object_set_position(object_index, &transform->position, &transform->forward, &transform->up);
	object->object.translational_velocity = *global_zero_vector3d;
	object->object.angular_velocity = *global_zero_vector3d;
	SET_FLAG(object->object.flags, _object_at_rest_bit, FALSE);
	forge_settle(object_index);
}

/* the last step undone (redo FALSE), or the next one redone */
static void forge_undo_step(
	boolean redo)
{
	struct forge_undo *entry;
	long object_index;
	char words[FORGE_LINE_SIZE];
	boolean done = TRUE;

	if (redo ? forge_globals.undo_cursor >= forge_globals.undo_count : forge_globals.undo_cursor <= 0)
	{
		forge_menu_notice("%s", redo ? T("Nothing to redo") : T("Nothing to undo"));
		return;
	}
	if (object_try_and_get(forge_globals.held_object_index))
		forge_drop();
	entry = &forge_globals.undo[redo ? forge_globals.undo_cursor : forge_globals.undo_cursor - 1];
	forge_undo_describe(entry, words, sizeof(words));
	object_index = forge_undo_object(entry);
	switch (entry->kind)
	{
	case _forge_undo_place:
	case _forge_undo_delete:
		/* (placing redone or deleting undone: the object back; placing
		undone or deleting redone: the object gone) */
		if ((entry->kind == _forge_undo_place) == redo)
		{
			if (object_index != NONE)
				break;
			if (!forge_undo_bring_back(entry, entry->kind == _forge_undo_place ? &entry->to : &entry->from))
			{
				forge_menu_notice(redo ? T("Can't redo %s: the budget is full") : T("Can't undo %s: the budget is full"), words);
				return;
			}
		}
		else
		{
			/* (out of the layout, gone in play or not) */
			if (object_index != NONE)
			{
				if (object_index == forge_globals.held_object_index)
					forge_globals.held_object_index = NONE;
				forge_placed_forget_object(object_index);
				object_delete(object_index);
			}
			forge_placed_forget_id(entry->id);
			forge_placed_update();
		}
		break;
	case _forge_undo_move:
		if (object_index == NONE)
			done = FALSE;
		else
			forge_undo_set_transform(object_index, redo ? &entry->to : &entry->from);
		break;
	}
	forge_globals.undo_cursor += redo ? 1 : -1;
	if (!done)
		forge_menu_notice(redo ? T("Can't redo %s: it is gone") : T("Can't undo %s: it is gone"), words);
	else
		forge_menu_notice(redo ? T("Redone: %s") : T("Undone: %s"), words);
	platform_log("forge: %s %s (%d of %d steps)", redo ? "redid" : "undid", words, forge_globals.undo_cursor,
		forge_globals.undo_count);
}

/* ---- snapping (the Forge menu's SNAP): the object held on a grid of
FORGE_SNAP_GRID along the ground, and turned in steps of FORGE_SNAP_ANGLE */

/* the nearest whole number (within the world's +-5000 over the grid) */
static real forge_round(
	real value)
{
	return value >= 0.f ? (real)(long)(value + 0.5f) : -(real)(long)(-value + 0.5f);
}

/* the place and facing an object held is shown at */
static void forge_snapped(
	real_point3d *position,
	real_vector3d *forward,
	real_vector3d *up)
{
	real yaw;
	real snapped;
	real turn;
	real_vector3d axis = *global_up3d;

	if (!forge_globals.snap)
		return;
	position->x = forge_round(position->x / FORGE_SNAP_GRID) * FORGE_SNAP_GRID;
	position->y = forge_round(position->y / FORGE_SNAP_GRID) * FORGE_SNAP_GRID;
	yaw = arctangent(forward->j, forward->i);
	snapped = forge_round(yaw / FORGE_SNAP_ANGLE) * FORGE_SNAP_ANGLE;
	turn = snapped - yaw;
	rotate_vector_about_axis(forward, &axis, sine(turn), cosine(turn));
	rotate_vector_about_axis(up, &axis, sine(turn), cosine(turn));
	/* (a building block: its corner on the grid, up and down too, so that
	blocks meet edge to edge: forge_blocks.c) */
	forge_blocks_snap(forge_globals.held_object_index, FORGE_SNAP_GRID, position, forward, up);
}

/* ---- object properties (Square while holding an object: halo3/forge.xml's
properties window): its team, respawn time and whether it is placed at
the start, kept in its placed entry and saved in the map variant; its
exact angles (yaw, pitch, roll, forge_angles_from_facing's: the map
variant keeps all three) and their reset */

static short const forge_respawn_choices[] = { 0, 10, 20, 30, 45, 60, 90, 120, 180 };

/* the placed entry of the object held, NULL for none or one of the map's */
static struct forge_placed *forge_properties_target(
	void)
{
	return object_try_and_get(forge_globals.held_object_index) ? forge_placed_of_object(forge_globals.held_object_index) :
		NULL;
}

static void forge_property_apply(
	short property,
	short step)
{
	struct forge_placed *placed = forge_properties_target();
	short index;

	if (!placed)
		return;
	switch (property)
	{
	case _forge_property_team:
		/* (neutral, red, blue) */
		placed->team = (signed char)(((placed->team + 1 + step + 3) % 3) - 1);
		break;
	case _forge_property_respawn:
		for (index = 0; index < (short)NUMBEROF(forge_respawn_choices) - 1; index++)
		{
			if (forge_respawn_choices[index] >= placed->respawn_seconds)
				break;
		}
		index = (short)((index + step + (short)NUMBEROF(forge_respawn_choices)) % (short)NUMBEROF(forge_respawn_choices));
		placed->respawn_seconds = (unsigned short)forge_respawn_choices[index];
		break;
	case _forge_property_place_at_start:
		placed->flags ^= FORGE_RECORD_PLACE_AT_START;
		break;
	case _forge_property_yaw:
	case _forge_property_pitch:
	case _forge_property_roll:
	case _forge_property_reset_rotation:
	{
		/* (the exact angles: to the next whole step either way - rotate
		mode's snapping, else FORGE_ANGLE_STEP -, yaw and roll round the
		circle, pitch from straight down to straight up; or all none) */
		float angles[3];
		real stepping = (real)(forge_rotate_snaps[forge_globals.rotate_snap] > 0 ?
			forge_rotate_snaps[forge_globals.rotate_snap] : FORGE_ANGLE_STEP);
		real_vector3d forward, up;

		forge_angles_from_facing(&forge_globals.held_forward, &forge_globals.held_up, angles);
		if (property == _forge_property_reset_rotation)
		{
			angles[0] = angles[1] = angles[2] = 0.f;
		}
		else
		{
			short axis = (short)(property - _forge_property_yaw);
			real degrees = RADIANS_TO_DEGREES(angles[axis]) / stepping;
			real whole = forge_round(degrees);

			/* (from between two steps, the next one; from a step, the one
			after) */
			if (step > 0)
				degrees = (degrees - whole < -0.01f ? whole : whole + 1.f) * stepping;
			else
				degrees = (degrees - whole > 0.01f ? whole : whole - 1.f) * stepping;
			if (axis == _forge_axis_pitch)
				degrees = PIN(degrees, -90.f, 90.f);
			else if (degrees > 180.f)
				degrees -= 360.f;
			else if (degrees <= -180.f)
				degrees += 360.f;
			angles[axis] = DEGREES_TO_RADIANS(degrees);
		}
		forge_facing_from_angles(angles, &forward, &up);
		forge_set_facing(forge_globals.held_object_index, &forward, &up);
		break;
	}
	case _forge_property_ai_team:
		if (!placed->ai)
			return;
		placed->ai_team = (unsigned char)((placed->ai_team + step + NUMBER_OF_FORGE_AI_TEAMS) % NUMBER_OF_FORGE_AI_TEAMS);
		forge_ai_apply(placed);
		break;
	case _forge_property_ai_brain:
		if (!placed->ai)
			return;
		placed->flags ^= FORGE_RECORD_AI_OFF;
		forge_ai_apply(placed);
		break;
	}
	{
		float angles[3];
		char rotation[96];

		forge_angles_from_facing(&forge_globals.held_forward, &forge_globals.held_up, angles);
		snprintf(rotation, sizeof(rotation), "yaw %.1f, pitch %.1f, roll %.1f, facing %.3f %.3f %.3f up %.3f %.3f %.3f",
			RADIANS_TO_DEGREES(angles[0]), RADIANS_TO_DEGREES(angles[1]), RADIANS_TO_DEGREES(angles[2]),
			forge_globals.held_forward.i, forge_globals.held_forward.j, forge_globals.held_forward.k, forge_globals.held_up.i,
			forge_globals.held_up.j, forge_globals.held_up.k);
		if (placed->ai)
		{
			platform_log("forge: properties: team %d, respawn %d s, %splaced at start, AI team %s (%d), AI %s, %s", placed->team,
				placed->respawn_seconds, (placed->flags & FORGE_RECORD_PLACE_AT_START) ? "" : "not ",
				forge_ai_team_log_names[placed->ai_team % NUMBER_OF_FORGE_AI_TEAMS],
				forge_ai_team_index(placed->ai_team, placed->object_index), (placed->flags & FORGE_RECORD_AI_OFF) ? "off" : "on",
				rotation);
		}
		else
		{
			platform_log("forge: properties: team %d, respawn %d s, %splaced at start, %s", placed->team, placed->respawn_seconds,
				(placed->flags & FORGE_RECORD_PLACE_AT_START) ? "" : "not ", rotation);
		}
	}
}

static void forge_properties_open(
	void)
{
	long screen;

	if (!forge_properties_target())
	{
		forge_notice("%s", T("Only objects you placed have properties"));
		return;
	}
	screen = menu_tags_ingame_screen(FORGE_PROPERTIES_SCREEN);
	if (screen == NONE ||
		!ui_widget_load_by_name_or_tag(NULL, screen, NULL, forge_globals.local_player_index, NONE, NONE, NONE))
	{
		return;
	}
	platform_log("forge: the object's properties opened");
}

/* ---- the scene packs (scene_packs.h): the lobby's choice, built in the
background as it is open; loaded as a Forge game's map loads, before its
objects (forge_map_loaded), in forge_packs' order while the tag cache has
room, a pack whose objects the map has already left out */

static short forge_pack_of_name(
	char const *name)
{
	short pack;

	for (pack = 0; pack < FORGE_PACK_COUNT; pack++)
	{
		if (!strcmp(forge_packs[pack].name, name))
			return pack;
	}
	return NONE;
}

static void forge_packs_defaults(
	void)
{
	short pack;

	if (forge_globals.packs_chosen)
		return;
	forge_globals.packs_chosen = TRUE;
	forge_globals.packs_enabled = 0;
	for (pack = 0; pack < FORGE_PACK_COUNT; pack++)
	{
		if (forge_packs[pack].on_by_default)
			forge_globals.packs_enabled |= 1UL << pack;
	}
#ifndef HALO_RELEASE
	/* (debug: the tests' choice, the packs' names with commas between, or
	"none") */
	{
		char const *setting = getenv("HALO_TEST_FORGE_PACKS");

		if (setting && *setting)
		{
			forge_globals.packs_enabled = 0;
			for (pack = 0; pack < FORGE_PACK_COUNT; pack++)
			{
				if (strstr(setting, forge_packs[pack].name))
					forge_globals.packs_enabled |= 1UL << pack;
			}
		}
	}
#endif
}

/* (the Forge game's map loaded) the session's packs loaded, what came of
each said in the log and kept for the HUD */
static void forge_packs_load(
	void)
{
	short pack;
	int length = 0;
	char loaded[FORGE_LINE_SIZE];
	int loaded_length = 0;

	forge_globals.packs_report[0] = 0;
	loaded[0] = 0;
	/* (the building blocks first, of the map itself: forge_blocks.c) */
	forge_blocks_load(forge_globals.game_map);
	for (pack = 0; pack < FORGE_PACK_COUNT; pack++)
	{
		char why[SCENE_PACK_WHY_SIZE];
		long progress;

		if (!(forge_globals.packs_session & (1UL << pack)))
			continue;
		if (scene_packs_recipe_covered(forge_packs[pack].name))
		{
			platform_log("forge: packs: %s: the map has its objects already", forge_packs[pack].name);
			continue;
		}
		/* (a pack whose source map the player has not - the Custom Edition
		map of the flyable Pelican - is not offered: the lobby says why, the
		game says nothing) */
		if (!scene_packs_recipe_available(forge_packs[pack].name, why, sizeof(why)))
		{
			platform_log("forge: packs: %s: unavailable: %s", forge_packs[pack].name, why);
			continue;
		}
		if (scene_packs_load_now(forge_packs[pack].name))
		{
			platform_log("forge: packs: %s loaded (%lu KB of the tag cache held by packs now)", forge_packs[pack].name,
				scene_packs_reserved_bytes() / 1024);
			if (loaded_length < (int)sizeof(loaded))
				loaded_length += snprintf(loaded + loaded_length, sizeof(loaded) - (size_t)loaded_length, "%s%s",
					loaded_length ? ", " : "", T(forge_packs[pack].label));
			continue;
		}
		scene_packs_status(forge_packs[pack].name, &progress, why, sizeof(why));
		platform_log("forge: packs: %s left out: %s", forge_packs[pack].name, why[0] ? why : "it could not be loaded");
		if (length < (int)sizeof(forge_globals.packs_report))
			length += snprintf(forge_globals.packs_report + length, sizeof(forge_globals.packs_report) - (size_t)length,
				"%s%s", length ? ", " : "", T(forge_packs[pack].label));
	}
	platform_log("forge: packs: loaded: %s; left out: %s; %lu KB of the tag cache held by packs", loaded[0] ? loaded : "none",
		forge_globals.packs_report[0] ? forge_globals.packs_report : "none", scene_packs_reserved_bytes() / 1024);
	forge_globals.palette_built = FALSE;
}

/* ---- map variants (forge_files.h: the file; forge_store.h: the memory
card) */

/* the map variant read for the session to apply as it starts, or the one
being saved (one at a time: the start's is applied before any save) */
static struct forge_file_layout forge_variant_layout;
/* a new round's layout: the session's, taken as the map resets
(forge_map_resetting), placed again once the player has spawned */
static struct forge_file_layout forge_round_layout;

/* a scenario's short name (levels\test\bloodgulch\bloodgulch: bloodgulch),
FALSE if it is not one a map variant may name */
static boolean forge_map_short_name(
	char const *scenario,
	char *name,
	int size)
{
	char const *slash = scenario ? strrchr(scenario, '\\') : NULL;
	int index;

	snprintf(name, (size_t)size, "%s", slash ? slash + 1 : scenario ? scenario : "");
	for (index = 0; name[index]; index++)
	{
		if (name[index] >= 'A' && name[index] <= 'Z')
			name[index] = (char)(name[index] - 'A' + 'a');
	}
	return forge_file_map_name_valid(name);
}

/* a facing (forward, up) as yaw, pitch and roll, and back: yaw about up
from +x, pitch up from the ground, roll about forward (the up of no roll,
upright as yaw and pitch leave it, turned toward the left) */
static void forge_angles_from_facing(
	real_vector3d const *forward,
	real_vector3d const *up,
	float angles[3])
{
	real_vector3d left;
	real_vector3d upright;
	real yaw = arctangent(forward->j, forward->i);
	real pitch = arcsine(PIN(forward->k, -1.f, 1.f));

	left.i = -sine(yaw);
	left.j = cosine(yaw);
	left.k = 0.f;
	cross_product3d(forward, &left, &upright);
	angles[0] = forge_file_angle_reduce(yaw);
	angles[1] = forge_file_angle_reduce(pitch);
	angles[2] = forge_file_angle_reduce(arctangent(-dot_product3d(up, &left), dot_product3d(up, &upright)));
}

static void forge_facing_from_angles(
	float const angles[3],
	real_vector3d *forward,
	real_vector3d *up)
{
	real_vector3d left;
	real_vector3d upright;
	real cosine_pitch = cosine(angles[1]);

	forward->i = cosine_pitch * cosine(angles[0]);
	forward->j = cosine_pitch * sine(angles[0]);
	forward->k = sine(angles[1]);
	left.i = -sine(angles[0]);
	left.j = cosine(angles[0]);
	left.k = 0.f;
	cross_product3d(forward, &left, &upright);
	up->i = upright.i * cosine(angles[2]) - left.i * sine(angles[2]);
	up->j = upright.j * cosine(angles[2]) - left.j * sine(angles[2]);
	up->k = upright.k * cosine(angles[2]) - left.k * sine(angles[2]);
	if (normalize3d(forward) < 0.0001f)
		*forward = *global_forward3d;
	if (normalize3d(up) < 0.0001f)
		*up = *global_up3d;
}

/* the session's placed objects as a map variant (its paths, its records:
place, facing and properties); FALSE with why if the map is not one a
variant may be of. The map's own objects are not in it, as the budget
does not count them */
static boolean forge_variant_capture(
	struct forge_file_layout *layout,
	char const *title,
	char const *author,
	char *why,
	int why_size)
{
	short index;
	short skipped = 0;

	csmemset(layout, 0, sizeof(*layout));
	if (global_scenario_index == NONE ||
		!forge_map_short_name(tag_get_name(global_scenario_index), layout->map_name, sizeof(layout->map_name)))
	{
		snprintf(why, (size_t)why_size, "%s", T("Map variants can't be saved on this map."));
		return FALSE;
	}
	layout->map_hash = (unsigned int)cache_files_get_checksum();
	forge_placed_update();
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed const *placed = &forge_globals.placed[index];
		struct forge_transform home;
		struct forge_file_record *record;
		unsigned long group_tag = tag_get_group_tag(placed->tag_index);
		char const *name = tag_get_name(placed->tag_index);
		short path;
		short axis;

		/* (where the editor put it: its placed spot, not where play took
		it; one gone in play is still the layout's) */
		if (placed->has_home)
			home = placed->home;
		else if (forge_object_alive(placed->object_index))
			forge_transform_get(placed->object_index, &home);
		else
			continue;
		if (!name || !forge_file_group_allowed((unsigned int)group_tag) || !forge_file_path_valid(name, (int)strlen(name)) ||
			layout->record_count >= FORGE_FILE_MAXIMUM_RECORDS)
		{
			skipped++;
			continue;
		}
		for (path = 0; path < layout->path_count; path++)
		{
			if (layout->paths[path].group_tag == (unsigned int)group_tag && !strcmp(layout->paths[path].path, name))
				break;
		}
		if (path == layout->path_count)
		{
			if (layout->path_count >= FORGE_FILE_MAXIMUM_PATHS)
			{
				skipped++;
				continue;
			}
			layout->paths[path].group_tag = (unsigned int)group_tag;
			snprintf(layout->paths[path].path, sizeof(layout->paths[path].path), "%s", name);
			layout->path_count++;
		}
		record = &layout->records[layout->record_count];
		csmemset(record, 0, sizeof(*record));
		record->path_index = (unsigned short)path;
		record->kind = _forge_record_object;
		record->team = placed->team;
		record->respawn_seconds = (unsigned short)MIN(placed->respawn_seconds, FORGE_FILE_MAXIMUM_RESPAWN_SECONDS);
		record->count = 1;
		record->flags = (unsigned char)(placed->flags & FORGE_RECORD_FLAGS_KNOWN);
		/* (a character's AI settings: version 2 when any is set) */
		if (placed->ai && group_tag == ACTOR_VARIANT_DEFINITION_TAG)
			record->ai_team = (unsigned char)(placed->ai_team % NUMBER_OF_FORGE_AI_TEAMS);
		else
			record->flags &= (unsigned char)~FORGE_RECORD_AI_OFF;
		record->position[0] = home.position.x;
		record->position[1] = home.position.y;
		record->position[2] = home.position.z;
		forge_angles_from_facing(&home.forward, &home.up, record->angles);
		for (axis = 0; axis < 3; axis++)
		{
			if (!(record->position[axis] > -FORGE_FILE_MAXIMUM_COORDINATE + 1.f &&
				record->position[axis] < FORGE_FILE_MAXIMUM_COORDINATE - 1.f))
			{
				break;
			}
		}
		if (axis < 3)
		{
			skipped++;
			continue;
		}
		layout->record_count++;
	}
	/* (the packs the records' tags are of: named, by recipe and hash, so
	that loading the variant brings them) */
	for (index = 0; index < layout->record_count; index++)
	{
		long tag_index = tag_loaded((long)layout->paths[layout->records[index].path_index].group_tag,
			layout->paths[layout->records[index].path_index].path);
		char const *owner;
		short pack;

		if (tag_index == NONE || (owner = scene_packs_owner(tag_index)) == NULL)
			continue;
		for (pack = 0; pack < layout->pack_count; pack++)
		{
			if (!strcmp(layout->packs[pack].name, owner))
				break;
		}
		if (pack == layout->pack_count && pack < FORGE_FILE_MAXIMUM_PACKS && forge_pack_of_name(owner) != NONE)
		{
			unsigned long long hash = scene_packs_recipe_hash(owner);

			layout->packs[pack].recipe[0] = (unsigned int)(hash & 0xFFFFFFFFUL);
			layout->packs[pack].recipe[1] = (unsigned int)(hash >> 32);
			snprintf(layout->packs[pack].name, sizeof(layout->packs[pack].name), "%s", owner);
			layout->pack_count++;
		}
	}
	if (skipped)
		platform_log("forge: %d placed objects left out of the map variant", skipped);
	snprintf(layout->title, sizeof(layout->title), "%s", title);
	if (author && forge_file_text_valid(author, (int)sizeof(layout->author)))
		snprintf(layout->author, sizeof(layout->author), "%s", author);
	forge_store_date_now(layout->date, sizeof(layout->date));
	return TRUE;
}

/* (forge_update) the save the Forge menu asked for: the session's objects
written as a map variant, under the title typed (the session's own
variant's file when it has that title), and what came of it said */
static void forge_variant_save(
	void)
{
	char why[FORGE_STORE_WHY_SIZE];
	char saved[FORGE_FILE_NAME_SIZE];
	char title[FORGE_FILE_TITLE_SIZE];
	char author[FORGE_FILE_AUTHOR_SIZE];
	char const *file_name = NULL;
	boolean done = FALSE;

	forge_globals.save_requested = FALSE;
	snprintf(title, sizeof(title), "%s", forge_globals.save_title);
	snprintf(author, sizeof(author), "%s", forge_globals.save_author);
	why[0] = 0;
	if (!title[0] || !forge_file_text_valid(title, (int)sizeof(title)))
	{
		snprintf(forge_globals.save_status, sizeof(forge_globals.save_status), "%s", T("That name can't be used."));
	}
	else if (!forge_variant_capture(&forge_variant_layout, title, author, why, sizeof(why)))
	{
		snprintf(forge_globals.save_status, sizeof(forge_globals.save_status), "%s", why);
	}
	else
	{
		if (forge_globals.variant_file[0] && !strcmp(title, forge_globals.variant_title))
			file_name = forge_globals.variant_file;
		if (forge_store_save(&forge_variant_layout, file_name, saved, sizeof(saved), why, sizeof(why)))
		{
			snprintf(forge_globals.variant_title, sizeof(forge_globals.variant_title), "%s", title);
			snprintf(forge_globals.variant_file, sizeof(forge_globals.variant_file), "%s", saved);
			if (forge_variant_layout.record_count == 1)
				snprintf(forge_globals.save_status, sizeof(forge_globals.save_status), T("Saved \"%s\": 1 object."), title);
			else
				snprintf(forge_globals.save_status, sizeof(forge_globals.save_status), T("Saved \"%s\": %d objects."), title,
					forge_variant_layout.record_count);
			platform_log("forge: map variant \"%s\" saved as %s/%s, %d objects", title, forge_variant_layout.map_name, saved,
				forge_variant_layout.record_count);
			done = TRUE;
		}
		else
		{
			snprintf(forge_globals.save_status, sizeof(forge_globals.save_status), "%s", why);
		}
	}
	if (!done)
		platform_log("forge: the map variant was not saved: %s", forge_globals.save_status);
	forge_menu_notice("%s", forge_globals.save_status);
}

/* (the session's start) the map variant read for it placed: each record's
tag found by its path among the palette's (the browser's tags: nothing
else is ever placed), its place inside the structure's world, within the
budget. A record whose tag the map does not have, or that cannot be
placed, is skipped and counted, never the variant refused (it passed the
validator whole before it was read). (A new round's layout, round, is
placed the same way, said as a new round) */
static void forge_variant_apply(
	struct forge_file_layout const *layout,
	boolean round)
{
	long tags[FORGE_FILE_MAXIMUM_PATHS];
	short placed = 0;
	short missing = 0;
	short outside = 0;
	short over_budget = 0;
	short refused = 0;
	short other_kinds = 0;
	short skipped;
	short index;
	struct structure_bsp *structure = global_structure_bsp_get();

	forge_palette_ready();
	for (index = 0; index < layout->path_count && index < FORGE_FILE_MAXIMUM_PATHS; index++)
	{
		short entry;

		tags[index] = NONE;
		for (entry = 0; entry < forge_globals.palette_count; entry++)
		{
			long tag_index = forge_globals.palette[entry].tag_index;
			char const *name = tag_get_name(tag_index);

			if ((unsigned int)tag_get_group_tag(tag_index) == layout->paths[index].group_tag && name &&
				!_stricmp(name, layout->paths[index].path))
			{
				tags[index] = tag_index;
				break;
			}
		}
		if (tags[index] == NONE)
			platform_log("forge: the map variant's %s is not on this map: its objects are skipped", layout->paths[index].path);
	}
	for (index = 0; index < layout->record_count && index < FORGE_FILE_MAXIMUM_RECORDS; index++)
	{
		struct forge_file_record const *record = &layout->records[index];
		real_point3d position;
		real_vector3d forward;
		real_vector3d up;
		long object_index;
		short refusal;

		if (record->kind != _forge_record_object)
		{
			other_kinds++;
			continue;
		}
		if (record->path_index >= layout->path_count || tags[record->path_index] == NONE)
		{
			missing++;
			continue;
		}
		position.x = record->position[0];
		position.y = record->position[1];
		position.z = record->position[2];
		if (structure && (position.x < structure->world_bounds.x0 - FORGE_WORLD_BOUNDS_MARGIN ||
			position.x > structure->world_bounds.x1 + FORGE_WORLD_BOUNDS_MARGIN ||
			position.y < structure->world_bounds.y0 - FORGE_WORLD_BOUNDS_MARGIN ||
			position.y > structure->world_bounds.y1 + FORGE_WORLD_BOUNDS_MARGIN ||
			position.z < structure->world_bounds.z0 - FORGE_WORLD_BOUNDS_MARGIN ||
			position.z > structure->world_bounds.z1 + FORGE_WORLD_BOUNDS_MARGIN))
		{
			outside++;
			continue;
		}
		forge_facing_from_angles(record->angles, &forward, &up);
		object_index = forge_place(tags[record->path_index], &position, &forward, &up, TRUE, &refusal);
		if (object_index == NONE)
		{
			if (refusal == _forge_place_budget || refusal == _forge_place_ai_budget)
				over_budget++;
			else
				refused++;
			continue;
		}
		if (forge_globals.placed_count > 0)
		{
			struct forge_placed *entry = &forge_globals.placed[forge_globals.placed_count - 1];

			entry->team = record->team;
			entry->flags = record->flags;
			entry->respawn_seconds = record->respawn_seconds;
			entry->ai_team = record->ai_team;
			if (!entry->ai)
			{
				entry->ai_team = FORGE_AI_TEAM_DEFAULT;
				entry->flags &= (unsigned char)~FORGE_RECORD_AI_OFF;
			}
			forge_ai_apply(entry);
		}
		placed++;
	}
	skipped = (short)(missing + outside + over_budget + refused + other_kinds);
	/* (a map variant read: ALL AI as its characters have it - off when
	every one is off - for the ones placed after; a new round keeps the
	setting) */
	if (!round)
		forge_globals.ai_all_off = forge_ai_state() == _forge_ai_all_off;
	if (round)
	{
		platform_log("forge: new round %ld: %d placed again, %d skipped (%d missing tags, %d outside the map, %d past the "
			"budget, %d refused)", forge_globals.round_count, placed, skipped, missing, outside, over_budget, refused);
		forge_notice(T("New round: %d objects placed"), placed);
		return;
	}
	if (layout->map_hash != (unsigned int)cache_files_get_checksum())
		platform_log("forge: the map variant was made with another copy of this map (%08x, this one %08lx)", layout->map_hash,
			(unsigned long)cache_files_get_checksum());
	platform_log("forge: map variant \"%s\" applied: %d placed, %d skipped (%d missing tags, %d outside the map, %d past the "
		"budget, %d refused, %d not objects)", layout->title, placed, skipped, missing, outside, over_budget, refused,
		other_kinds);
	if (skipped)
		forge_notice(T("Map variant %s: %d placed, %d skipped"), layout->title, placed, skipped);
	else
		forge_notice(T("Map variant %s: %d placed"), layout->title, placed);
}

/* the session: the Forge game asked for, once its map is the one loaded
and its player has spawned, starts with the monitor; another map's state
is gone */
static void forge_session_update(
	void)
{
	long player_index;

	if (!forge_globals.game_active)
		return;
	if (game_connection() != _game_connection_local)
	{
		forge_globals.game_active = FALSE;
		forge_globals.game_requested = FALSE;
		return;
	}
	if (!forge_globals.hud_font_looked_up)
	{
		/* (the in-game set's, halo3/pause.xml's h3/font_small: menu_tags.c
		names ours pc\...) */
		forge_globals.hud_font_index = tag_loaded(FORGE_FONT_TAG, "pc\\h3\\font_small");
		forge_globals.hud_label_font_index = tag_loaded(FORGE_FONT_TAG, "pc\\h3\\font_item");
		forge_globals.hud_crosshair_font_index = tag_loaded(FORGE_FONT_TAG, "pc\\h3\\font_title");
		forge_globals.hud_font_looked_up = TRUE;
	}
	if (forge_globals.game_requested)
	{
		player_index = local_player_get_player_index(0);
		if (player_index != NONE && player_get(player_index)->unit_index != NONE && forge_begin())
		{
			forge_globals.game_requested = FALSE;
			platform_log("forge: the Forge game of %s starts", forge_globals.game_map);
			if (forge_globals.packs_report[0])
				forge_notice(T("Left out (no room): %s"), forge_globals.packs_report);
			if (forge_globals.variant_pending)
			{
				forge_globals.variant_pending = FALSE;
				forge_variant_apply(&forge_variant_layout, FALSE);
			}
		}
	}
	/* (a new round: once the player has spawned again, the monitor where
	it flew, if it did, and the layout placed again) */
	if (forge_globals.round_pending && !forge_globals.game_requested)
	{
		player_index = local_player_get_player_index(0);
		if (player_index != NONE && player_get(player_index)->unit_index != NONE)
		{
			forge_globals.round_pending = FALSE;
			if (forge_globals.round_monitor && forge_begin())
			{
				director_forge_camera(forge_globals.local_player_index, TRUE, &forge_globals.round_camera.position,
					&forge_globals.round_camera.forward);
			}
			forge_variant_apply(&forge_round_layout, TRUE);
		}
	}
}

/* (forge_update, the Forge menu's NEW ROUND, no menu over the game) the
map reset by the engine, as its own map_reset does it: forge_map_resetting
takes the layout as the game's state goes, forge_map_reset and the session
place it again */
static void forge_new_round_start(
	void)
{
	real_point3d position;
	real_vector3d forward;

	if (object_try_and_get(forge_globals.held_object_index))
		forge_drop();
	forge_globals.round_monitor = forge_globals.active;
	if (forge_globals.active && forge_camera_get(&position, &forward))
	{
		forge_globals.round_camera.position = position;
		forge_globals.round_camera.forward = forward;
	}
	else
	{
		forge_globals.round_monitor = FALSE;
	}
	forge_globals.round_resetting = TRUE;
	platform_log("forge: new round asked: the map resets, the layout's %d objects placed again (as the %s)",
		forge_globals.placed_count, forge_globals.round_monitor ? "monitor" : "Spartan");
	main_reset_map();
}

/* ---------- the cross-map probe (forge deps) */

static struct
{
	long tags[MAXIMUM_FORGE_DEPENDENCY_TAGS];
	/* each tag's own bytes in the tag cache and in the map's file */
	unsigned long tag_sizes[MAXIMUM_FORGE_DEPENDENCY_TAGS];
	unsigned long file_sizes[MAXIMUM_FORGE_DEPENDENCY_TAGS];
	long count;
	/* the tag being walked (its slot) */
	long current;
	/* bytes in the tag cache (roots, blocks' elements, data) and in the
	map's file (bitmaps' pixels, sounds' samples) */
	unsigned long tag_bytes;
	unsigned long file_bytes;
	unsigned long group_tag_bytes[64];
	unsigned long group_file_bytes[64];
	unsigned long group_tags[64];
	short group_counts[64];
	short group_count;
} forge_dependencies;

static struct tag_schema_group const *forge_schema_group_get(
	unsigned long group_tag)
{
	struct tag_schema_group const *const *list;

	for (list = tag_schema_group_lists; *list; list++)
	{
		struct tag_schema_group const *group;

		for (group = *list; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group;
		}
	}
	return NULL;
}

static short forge_dependency_group(
	unsigned long group_tag)
{
	short index;

	for (index = 0; index < forge_dependencies.group_count; index++)
	{
		if (forge_dependencies.group_tags[index] == group_tag)
			return index;
	}
	if (forge_dependencies.group_count >= (short)NUMBEROF(forge_dependencies.group_tags))
		return NUMBEROF(forge_dependencies.group_tags) - 1;
	forge_dependencies.group_tags[forge_dependencies.group_count] = group_tag;
	forge_dependencies.group_tag_bytes[forge_dependencies.group_count] = 0;
	forge_dependencies.group_file_bytes[forge_dependencies.group_count] = 0;
	forge_dependencies.group_counts[forge_dependencies.group_count] = 0;
	return forge_dependencies.group_count++;
}

static void forge_dependency_add(
	long tag_index);

/* bytes of the tag being walked, of its group (group) */
static void forge_dependency_count(
	short group,
	unsigned long tag_bytes,
	unsigned long file_bytes)
{
	forge_dependencies.group_tag_bytes[group] += tag_bytes;
	forge_dependencies.group_file_bytes[group] += file_bytes;
	forge_dependencies.tag_sizes[forge_dependencies.current] += tag_bytes;
	forge_dependencies.file_sizes[forge_dependencies.current] += file_bytes;

	return;
}

static void forge_dependency_walk(
	byte *base,
	struct tag_schema_definition const *definition,
	short group,
	short depth)
{
	struct tag_schema_field const *field;

	if (depth > 32 || !base)
		return;
	for (field = definition->fields; field->type != _tag_schema_terminator; field++)
	{
		short index;

		for (index = 0; index < field->count; index++)
		{
			byte *address = base + field->offset + index * field->size;

			switch (field->type)
			{
			case _tag_schema_struct:
				forge_dependency_walk(address, field->definition, group, depth + 1);
				break;

			case _tag_schema_block:
			{
				struct tag_block *block = (struct tag_block *)address;
				struct tag_schema_definition const *element = field->definition;
				long element_index;

				if (block->count <= 0 || !block->address)
					break;
				forge_dependency_count(group, (unsigned long)block->count * element->size, 0);
				for (element_index = 0; element_index < block->count; element_index++)
					forge_dependency_walk((byte *)block->address + element_index * element->size, element, group, depth + 1);
				break;
			}

			case _tag_schema_data:
				if (((struct tag_data *)address)->size > 0)
					forge_dependency_count(group, (unsigned long)((struct tag_data *)address)->size, 0);
				break;

			case _tag_schema_file_data:
				if (((struct tag_data *)address)->size > 0)
					forge_dependency_count(group, 0, (unsigned long)((struct tag_data *)address)->size);
				break;

			case _tag_schema_reference:
				forge_dependency_add(((struct tag_reference *)address)->index);
				break;

			case _tag_schema_tag_index:
				forge_dependency_add(*(long *)address);
				break;
			}
		}
	}

	return;
}

static void forge_dependency_add(
	long tag_index)
{
	long index;
	unsigned long group_tag;
	struct tag_schema_group const *schema;
	short group;
	long previous;

	if (tag_index == NONE)
		return;
	for (index = 0; index < forge_dependencies.count; index++)
	{
		if (forge_dependencies.tags[index] == tag_index)
			return;
	}
	if (forge_dependencies.count >= MAXIMUM_FORGE_DEPENDENCY_TAGS)
		return;
	group_tag = tag_get_group_tag(tag_index);
	if (!group_tag || group_tag == NONE)
		return;
	group = forge_dependency_group(group_tag);
	forge_dependencies.group_counts[group]++;
	schema = forge_schema_group_get(group_tag);
	previous = forge_dependencies.current;
	forge_dependencies.current = forge_dependencies.count;
	forge_dependencies.tags[forge_dependencies.count++] = tag_index;
	if (schema)
	{
		forge_dependency_count(group, (unsigned long)schema->definition->size, 0);
		forge_dependency_walk(tag_get(group_tag, tag_index), schema->definition, group, 0);
	}
	forge_dependencies.current = previous;

	return;
}

static char *forge_group_text(
	unsigned long group_tag,
	char text[5])
{
	text[0] = (char)(group_tag >> 24);
	text[1] = (char)(group_tag >> 16);
	text[2] = (char)(group_tag >> 8);
	text[3] = (char)group_tag;
	text[4] = 0;
	return text;
}

static void forge_dependencies_log(
	long tag_index,
	char const *file_name)
{
	short group;
	long index;
	unsigned long tag_bytes = 0;
	unsigned long file_bytes = 0;
	char text[5];
	FILE *file = NULL;

	csmemset(&forge_dependencies, 0, sizeof(forge_dependencies));
	forge_dependency_add(tag_index);
	for (group = 0; group < forge_dependencies.group_count; group++)
	{
		tag_bytes += forge_dependencies.group_tag_bytes[group];
		file_bytes += forge_dependencies.group_file_bytes[group];
		platform_log("forge: deps %s: %d tags, %lu KB of tags, %lu KB in the map file",
			forge_group_text(forge_dependencies.group_tags[group], text), forge_dependencies.group_counts[group],
			forge_dependencies.group_tag_bytes[group] / 1024, forge_dependencies.group_file_bytes[group] / 1024);
	}
	platform_log("forge: deps of %s: %ld tags, %lu KB of tags (the tag cache), %lu KB of bitmaps' pixels and sounds' samples",
		tag_get_name(tag_index), forge_dependencies.count, tag_bytes / 1024, file_bytes / 1024);
	if (file_name && *file_name && (file = fopen(file_name, "w")) != NULL)
	{
		for (index = 0; index < forge_dependencies.count; index++)
		{
			fprintf(file, "%s %s\t%lu %lu\n", forge_group_text(tag_get_group_tag(forge_dependencies.tags[index]), text),
				tag_get_name(forge_dependencies.tags[index]), forge_dependencies.tag_sizes[index],
				forge_dependencies.file_sizes[index]);
		}
		fclose(file);
		platform_log("forge: deps' names written to %s", file_name);
	}

	return;
}

static void forge_tags_write(
	char const *file_name)
{
	struct tag_iterator iterator;
	long tag_index;
	long count = 0;
	char text[5];
	FILE *file = fopen(file_name, "w");

	if (!file)
	{
		platform_log("forge: %s could not be written", file_name);
		return;
	}
	tag_iterator_new(&iterator, NONE);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		fprintf(file, "%s %s\n", forge_group_text(tag_get_group_tag(tag_index), text), tag_get_name(tag_index));
		count++;
	}
	fclose(file);
	platform_log("forge: %ld tags written to %s", count, file_name);
	/* (the tag cache's room: the tags, then the largest structure bsp
	loaded after them) */
	{
		long cache_file_tag_data_size(void);
		struct tag_block const *references = &global_scenario_get()->structure_bsp_references;
		long largest_bsp_size = 0;
		long reference_index;

		for (reference_index = 0; reference_index < references->count; reference_index++)
		{
			struct scenario_structure_bsp_reference const *reference =
				TAG_BLOCK_GET_ELEMENT(references, reference_index, struct scenario_structure_bsp_reference);

			largest_bsp_size = MAX(largest_bsp_size, reference->file_size);
		}
		platform_log("forge: the tag cache: %ld KB of tags, %ld KB the largest of %ld structure bsps, %ld KB free of %ld KB",
			cache_file_tag_data_size() / 1024, largest_bsp_size / 1024, (long)references->count,
			((long)TAG_CACHE_SIZE - cache_file_tag_data_size() - largest_bsp_size) / 1024, (long)TAG_CACHE_SIZE / 1024);
	}

	return;
}

/* ---------- public code */

short forge_map_count(
	void)
{
	return (short)NUMBEROF(forge_maps);
}

char const *forge_map_path(
	short map_index)
{
	return map_index >= 0 && map_index < forge_map_count() ? forge_maps[map_index].path : NULL;
}

char const *forge_map_name(
	short map_index)
{
	return map_index >= 0 && map_index < forge_map_count() ? forge_maps[map_index].name : "";
}

boolean forge_map_available(
	short map_index)
{
	char path[512];
	char const *slash;
	FILE *file;

	if (map_index < 0 || map_index >= forge_map_count())
		return FALSE;
	slash = strrchr(forge_maps[map_index].path, '\\');
	snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), slash ? slash + 1 : forge_maps[map_index].path);
	file = fopen(path, "rb");
	if (file)
		fclose(file);
	return file != NULL;
}

boolean forge_game_start(
	short map_index,
	char const *variant_file,
	char *why,
	int why_size)
{
	struct game_variant variant;
	static wchar_t const name[] = L"Forge";
	char map_name[FORGE_FILE_MAP_NAME_SIZE];

	if (map_index < 0 || map_index >= forge_map_count())
		return FALSE;
	/* (the map variant chosen, read and checked whole before the game is
	asked for: one that cannot be read starts nothing) */
	forge_globals.variant_pending = FALSE;
	forge_globals.variant_title[0] = 0;
	forge_globals.variant_file[0] = 0;
	forge_packs_defaults();
	forge_globals.packs_session = forge_globals.packs_enabled;
	if (variant_file && variant_file[0])
	{
		if (!forge_map_short_name(forge_maps[map_index].path, map_name, sizeof(map_name)) ||
			!forge_store_load(map_name, variant_file, &forge_variant_layout, why, why_size))
		{
			platform_log("forge: the map variant %s of %s was not read: %s", variant_file, forge_maps[map_index].path, why);
			return FALSE;
		}
		snprintf(forge_globals.variant_title, sizeof(forge_globals.variant_title), "%s", forge_variant_layout.title);
		snprintf(forge_globals.variant_file, sizeof(forge_globals.variant_file), "%s", variant_file);
		forge_globals.variant_pending = TRUE;
		/* (the packs it names, ours only: brought with the lobby's; one of
		another recipe's version is brought as it is now, and its records
		skipped if their tags are not in it) */
		{
			short index;

			for (index = 0; index < forge_variant_layout.pack_count; index++)
			{
				short pack = forge_pack_of_name(forge_variant_layout.packs[index].name);
				unsigned long long hash;

				if (pack == NONE)
				{
					platform_log("forge: the map variant names a pack Forge does not offer (%s): its objects are skipped",
						forge_variant_layout.packs[index].name);
					continue;
				}
				forge_globals.packs_session |= 1UL << pack;
				hash = scene_packs_recipe_hash(forge_packs[pack].name);
				if ((unsigned int)(hash & 0xFFFFFFFFUL) != forge_variant_layout.packs[index].recipe[0] ||
					(unsigned int)(hash >> 32) != forge_variant_layout.packs[index].recipe[1])
				{
					platform_log("forge: the map variant's %s pack was another version of it", forge_packs[pack].name);
				}
			}
		}
		platform_log("forge: map variant \"%s\" (%s, %d objects) for the Forge game", forge_variant_layout.title, variant_file,
			forge_variant_layout.record_count);
	}
	/* (free for all slayer: the map's own vehicles and weapons, players
	respawning at once; a score no one alone reaches - a kill of the
	player's own is the only score, AI's kills are not a player's - and no
	time limit, game_variant_options' default) */
	build_game_variant_slayer(&variant);
	csmemset(variant.human_readable_game_description, 0, sizeof(variant.human_readable_game_description));
	csmemcpy(variant.human_readable_game_description, name, sizeof(name));
	variant.universal_variant.score_to_win = 999;
	variant.universal_variant.respawn_time = 0;
	variant.universal_variant.suicide_penalty = 0;
	game_connection_set(_game_connection_local);
	game_set_game_variant(&variant);
	main_set_map_name((char *)forge_maps[map_index].path);
	/* (no saved game read or written: the campaign's is left alone) */
	main_disallow_persistent_storage();
	csstrncpy(forge_globals.game_map, forge_maps[map_index].path, sizeof(forge_globals.game_map) - 1);
	forge_globals.game_map[sizeof(forge_globals.game_map) - 1] = 0;
	forge_globals.game_requested = TRUE;
	platform_log("forge: a Forge game of %s asked for", forge_globals.game_map);
	/* (as "main menu switch to solo game": the map loads after the main
	menu's fade) */
	main_menu_switch_to_single_player();
	return TRUE;
}

/* (the settings panel's Forge page, vita_settings.c: another ABI, plain
types only) the lobby's maps */
int forge_panel_map_count(
	void)
{
	return forge_map_count();
}

char const *forge_panel_map_name(
	int map_index)
{
	return forge_map_name((short)map_index);
}

char const *forge_panel_map_short(
	int map_index)
{
	return forge_map_short((short)map_index);
}

int forge_panel_map_available(
	int map_index)
{
	return map_index >= 0 && map_index < forge_map_count() && forge_map_available((short)map_index) ? 1 : 0;
}

char const *forge_map_short(
	short map_index)
{
	static char names[NUMBEROF(forge_maps)][FORGE_FILE_MAP_NAME_SIZE];

	if (map_index < 0 || map_index >= forge_map_count())
		return "";
	if (!names[map_index][0])
		forge_map_short_name(forge_maps[map_index].path, names[map_index], sizeof(names[map_index]));
	return names[map_index];
}

short forge_map_of_short(
	char const *map_name)
{
	short map_index;

	for (map_index = 0; map_name && map_index < forge_map_count(); map_index++)
	{
		if (!strcmp(forge_map_short(map_index), map_name))
			return map_index;
	}
	return NONE;
}

void forge_save_request(
	char const *title,
	char const *author)
{
	snprintf(forge_globals.save_title, sizeof(forge_globals.save_title), "%s", title ? title : "");
	snprintf(forge_globals.save_author, sizeof(forge_globals.save_author), "%s", author ? author : "");
	forge_globals.save_status[0] = 0;
	forge_globals.save_requested = TRUE;
}

char const *forge_variant_title(
	void)
{
	return forge_globals.variant_title;
}

short forge_pack_count(
	void)
{
	return FORGE_PACK_COUNT;
}

char const *forge_pack_label(
	short pack)
{
	return pack >= 0 && pack < FORGE_PACK_COUNT ? T(forge_packs[pack].label) : "";
}

char const *forge_pack_description(
	short pack)
{
	return pack >= 0 && pack < FORGE_PACK_COUNT ? T(forge_packs[pack].description) : "";
}

boolean forge_pack_enabled(
	short pack)
{
	forge_packs_defaults();
	return pack >= 0 && pack < FORGE_PACK_COUNT && (forge_globals.packs_enabled & (1UL << pack)) != 0;
}

void forge_pack_set(
	short pack,
	boolean enabled)
{
	forge_packs_defaults();
	if (pack < 0 || pack >= FORGE_PACK_COUNT)
		return;
	if (enabled)
		forge_globals.packs_enabled |= 1UL << pack;
	else
		forge_globals.packs_enabled &= ~(1UL << pack);
	platform_log("forge: packs: %s %s", forge_packs[pack].name, enabled ? "on" : "off");
}

short forge_pack_status(
	short pack,
	long *progress,
	char *why,
	int why_size)
{
	if (pack < 0 || pack >= FORGE_PACK_COUNT)
		return _scene_pack_state_none;
	return scene_packs_status(forge_packs[pack].name, progress, why, why_size);
}

void forge_packs_prebuild(
	void)
{
	short pack;

	forge_packs_defaults();
	for (pack = 0; pack < FORGE_PACK_COUNT; pack++)
	{
		if ((forge_globals.packs_enabled & (1UL << pack)) &&
			scene_packs_status(forge_packs[pack].name, NULL, NULL, 0) == _scene_pack_state_none)
		{
			platform_log("forge: packs: %s wanted for the lobby: %s", forge_packs[pack].name,
				scene_packs_state_name(scene_packs_prebuild(forge_packs[pack].name)));
		}
	}
}

boolean forge_packs_building(
	char *label,
	int size,
	long *progress)
{
	short pack;

	for (pack = 0; pack < FORGE_PACK_COUNT; pack++)
	{
		long done = 0;

		if ((forge_globals.packs_enabled & (1UL << pack)) &&
			scene_packs_status(forge_packs[pack].name, &done, NULL, 0) == _scene_pack_state_building)
		{
			if (label && size > 0)
				snprintf(label, (size_t)size, "%s", T(forge_packs[pack].label));
			if (progress)
				*progress = done;
			return TRUE;
		}
	}
	return FALSE;
}

void forge_undo_request(
	boolean redo)
{
	forge_globals.undo_requested = redo ? 1 : -1;
}

void forge_undo_available(
	short *undo,
	short *redo)
{
	*undo = forge_globals.undo_cursor;
	*redo = (short)(forge_globals.undo_count - forge_globals.undo_cursor);
}

void forge_snap_toggle_request(
	void)
{
	forge_globals.snap_requested = TRUE;
}

boolean forge_snap_enabled(
	void)
{
	return forge_globals.snap;
}

void forge_property_request(
	short property,
	short step)
{
	if (property < 0 || property >= NUMBER_OF_FORGE_PROPERTIES)
		return;
	forge_globals.property_request = property;
	forge_globals.property_step = step < 0 ? -1 : 1;
	forge_globals.property_requested = TRUE;
}

boolean forge_properties_get(
	struct forge_properties *properties)
{
	struct forge_placed *placed = forge_properties_target();

	csmemset(properties, 0, sizeof(*properties));
	if (!placed)
		return FALSE;
	forge_object_name(placed->object_index, properties->name, sizeof(properties->name));
	properties->team = placed->team;
	properties->respawn_seconds = placed->respawn_seconds;
	properties->place_at_start = (placed->flags & FORGE_RECORD_PLACE_AT_START) != 0;
	{
		float angles[3];

		forge_angles_from_facing(&forge_globals.held_forward, &forge_globals.held_up, angles);
		properties->yaw = (short)forge_round(RADIANS_TO_DEGREES(angles[0]));
		properties->pitch = (short)forge_round(RADIANS_TO_DEGREES(angles[1]));
		properties->roll = (short)forge_round(RADIANS_TO_DEGREES(angles[2]));
	}
	properties->ai = placed->ai;
	properties->ai_team = placed->ai_team;
	properties->ai_on = !(placed->flags & FORGE_RECORD_AI_OFF);
	if (placed->ai_team == FORGE_AI_TEAM_DEFAULT && forge_object_alive(placed->object_index))
	{
		/* ("DEFAULT (COVENANT)": the character's own team, named) */
		short team = forge_ai_team_index(FORGE_AI_TEAM_DEFAULT, placed->object_index);
		short named = team == _game_team_human ? FORGE_AI_TEAM_HUMAN : team == _game_team_covenant ? FORGE_AI_TEAM_COVENANT :
			team == _game_team_flood ? FORGE_AI_TEAM_FLOOD : team == _game_team_sentinel ? FORGE_AI_TEAM_SENTINEL :
			team == _game_team_player ? FORGE_AI_TEAM_PLAYER : NONE;

		if (named != NONE)
			snprintf(properties->ai_team_name, sizeof(properties->ai_team_name), "%s (%s)", T(forge_ai_team_names[0]),
				T(forge_ai_team_names[named]));
		else
			snprintf(properties->ai_team_name, sizeof(properties->ai_team_name), "%s", T(forge_ai_team_names[0]));
	}
	else
	{
		snprintf(properties->ai_team_name, sizeof(properties->ai_team_name), "%s",
			T(forge_ai_team_names[placed->ai_team % NUMBER_OF_FORGE_AI_TEAMS]));
	}
	return TRUE;
}

void forge_menu_status(
	char *status,
	int size)
{
	snprintf(status, (size_t)size, "%s", forge_globals.menu_status);
}

void forge_new_round_request(
	void)
{
	forge_globals.round_requested = TRUE;
}

void forge_ai_all_request(
	void)
{
	forge_globals.ai_all_requested = TRUE;
}

short forge_ai_all_state(
	void)
{
	return forge_ai_state();
}

void forge_clear_ai_request(
	boolean all)
{
	forge_globals.clear_ai_requested = all ? _forge_clear_ai_all : _forge_clear_ai_dead;
}

int forge_panel_ai_counts(
	int *alive,
	int *dead)
{
	if (alive)
		*alive = forge_globals.ai_count - forge_globals.ai_dead_count;
	if (dead)
		*dead = forge_globals.ai_dead_count;
	return forge_globals.game_active && !forge_globals.game_requested;
}

void forge_panel_clear_ai(
	int all)
{
	forge_clear_ai_request(all != 0);
}

void forge_map_resetting(
	void)
{
	char why[FORGE_STORE_WHY_SIZE];
	short characters = 0;
	short index;

	if (!forge_globals.game_active || forge_globals.game_requested)
	{
		forge_globals.round_resetting = FALSE;
		return;
	}
	/* (a reset not asked by NEW ROUND - the console's map_reset - keeps the
	layout too, the monitor where it was) */
	if (!forge_globals.round_resetting)
	{
		real_point3d position;
		real_vector3d forward;

		forge_globals.round_monitor = forge_globals.active && forge_camera_get(&position, &forward);
		if (forge_globals.round_monitor)
		{
			forge_globals.round_camera.position = position;
			forge_globals.round_camera.forward = forward;
		}
	}
	forge_placed_update();
	forge_homes_update();
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
		characters += forge_globals.placed[index].ai ? 1 : 0;
	if (!forge_variant_capture(&forge_round_layout, "New Round", NULL, why, sizeof(why)))
	{
		platform_log("forge: new round: the layout was not taken: %s", why);
		csmemset(&forge_round_layout, 0, sizeof(forge_round_layout));
	}
	forge_globals.round_count++;
	forge_globals.round_pending = TRUE;
	platform_log("forge: new round %ld: the layout taken: %d objects (%d characters)", forge_globals.round_count,
		forge_round_layout.record_count, characters);
}

void forge_map_reset(
	void)
{
	/* (every object is gone, its indices with it: the layout waits in
	forge_round_layout; the undo history's steps name objects no more) */
	forge_globals.placed_count = 0;
	forge_globals.ai_count = 0;
	forge_globals.ai_dead_count = 0;
	forge_globals.held_object_index = NONE;
	forge_globals.held_spawned = FALSE;
	forge_globals.undo_count = 0;
	forge_globals.undo_cursor = 0;
	forge_globals.undo_requested = 0;
	forge_globals.property_requested = FALSE;
	forge_globals.browser_chosen = FALSE;
	forge_globals.toggle_requested = FALSE;
	forge_globals.close_requested = FALSE;
	forge_globals.ai_all_requested = FALSE;
	forge_globals.clear_ai_requested = _forge_clear_ai_none;
	forge_globals.round_requested = FALSE;
	forge_globals.round_resetting = FALSE;
	if (forge_globals.active)
	{
		/* (the director starts over with the map) */
		forge_globals.active = FALSE;
		forge_monitor_flying = 0;
	}
	forge_globals.unit_index = NONE;
	if (forge_globals.game_active)
		platform_log("forge: the map was reset: the layout is placed again once the player has spawned");
}

void forge_map_loaded(
	char const *scenario_name)
{
	/* (every object of the last map is gone, its indices with them) */
	forge_globals.placed_count = 0;
	forge_globals.ai_count = 0;
	forge_globals.ai_dead_count = 0;
	forge_globals.held_object_index = NONE;
	forge_globals.held_spawned = FALSE;
	forge_globals.undo_count = 0;
	forge_globals.undo_cursor = 0;
	forge_globals.undo_requested = 0;
	forge_globals.snap_requested = FALSE;
	forge_globals.property_requested = FALSE;
	forge_globals.menu_status[0] = 0;
	forge_globals.save_status[0] = 0;
	forge_globals.palette_built = FALSE;
	forge_globals.hud_font_looked_up = FALSE;
	forge_globals.hud_font_index = NONE;
	forge_globals.hud_label_font_index = NONE;
	forge_globals.hud_crosshair_font_index = NONE;
	forge_globals.browser_chosen = FALSE;
	forge_globals.toggle_requested = FALSE;
	forge_globals.close_requested = FALSE;
	forge_globals.quit_requested = FALSE;
	forge_globals.spawned_count = 0;
	forge_globals.deleted_count = 0;
	forge_globals.notice[0] = 0;
	forge_globals.round_requested = FALSE;
	forge_globals.round_resetting = FALSE;
	forge_globals.round_pending = FALSE;
	forge_globals.ai_all_requested = FALSE;
	forge_globals.clear_ai_requested = _forge_clear_ai_none;
	forge_globals.ai_all_off = FALSE;
	if (forge_globals.active)
	{
		/* (the director starts over with the map) */
		forge_globals.active = FALSE;
		forge_monitor_flying = 0;
		forge_globals.unit_index = NONE;
	}
	forge_globals.game_active = forge_globals.game_requested && scenario_name &&
		!_stricmp(scenario_name, forge_globals.game_map);
	forge_game_playing = forge_globals.game_active;
	/* (the session's packs, before the map's objects are placed) */
	if (forge_globals.game_active)
		forge_packs_load();
	if (!forge_globals.game_active)
		forge_globals.game_requested = FALSE;
	else
		platform_log("forge: %s loaded for a Forge game", scenario_name);
}

boolean forge_game_active(
	void)
{
	return forge_globals.game_active;
}

boolean forge_monitor_active(
	void)
{
	return forge_globals.active;
}

void forge_toggle_request(
	void)
{
	forge_globals.toggle_requested = TRUE;
}

void forge_menus_close_request(
	void)
{
	forge_globals.close_requested = TRUE;
}

void forge_quit_request(
	void)
{
	forge_globals.quit_requested = TRUE;
}

char const *forge_category_name(
	short category)
{
	return category >= 0 && category < NUMBER_OF_FORGE_CATEGORIES ? T(forge_category_names[category]) : "";
}

short forge_category_item_count(
	short category)
{
	if (category < 0 || category >= NUMBER_OF_FORGE_CATEGORIES)
		return 0;
	forge_palette_ready();
	return forge_globals.palette_counts[category];
}

boolean forge_category_item(
	short category,
	short item_index,
	struct forge_item *item)
{
	struct forge_palette_entry const *entry;

	if (item_index < 0 || item_index >= forge_category_item_count(category))
		return FALSE;
	entry = &forge_globals.palette[forge_globals.palette_first[category] + item_index];
	csmemcpy(item->name, entry->name, sizeof(item->name));
	item->placed = forge_placed_of_tag(entry->tag_index);
	snprintf(item->group, sizeof(item->group), "%s", entry->category == _forge_category_characters &&
		entry->group < FORGE_AI_GROUP_OTHER ? T(forge_ai_group_names[entry->group]) : "");
	return TRUE;
}

void forge_browser_choose(
	short category,
	short item_index)
{
	if (item_index < 0 || item_index >= forge_category_item_count(category))
		return;
	forge_globals.browser_choice = (short)(forge_globals.palette_first[category] + item_index);
	forge_globals.browser_chosen = TRUE;
}

void forge_budget_get(
	short *objects,
	short *ai,
	short *dead_ai)
{
	*objects = forge_globals.placed_count;
	*ai = (short)(forge_globals.ai_count - forge_globals.ai_dead_count);
	if (dead_ai)
		*dead_ai = forge_globals.ai_dead_count;
}

boolean forge_camera_input(
	short local_player_index,
	real seconds,
	struct forge_camera_input *input)
{
	struct forge_pad pad;
	real speed;
	boolean turning_object;

	if (!forge_globals.active || local_player_index != forge_globals.local_player_index)
		return FALSE;
	csmemset(input, 0, sizeof(*input));
	/* (a menu open over the game has the buttons: the camera stays) */
	if (forge_menu_open())
		return TRUE;
	forge_pad_get(&pad);
	/* (forge look: as the mouse's look, once) */
	pad.look_yaw += forge_globals.test_look_yaw;
	pad.look_pitch += forge_globals.test_look_pitch;
	forge_globals.test_look_yaw = 0.f;
	forge_globals.test_look_pitch = 0.f;
	turning_object = pad.down[_forge_button_alternate] && object_try_and_get(forge_globals.held_object_index);
	speed = forge_speeds[PIN(forge_globals.speed, 0, NUMBER_OF_FORGE_SPEEDS - 1)] * seconds;
	if ((pad.down[_forge_button_alternate] && !turning_object) || (pad.keys & FORGE_KEY_SHIFT))
		speed *= FORGE_CAMERA_THRUST;
	/* (rotate mode: the left stick pushes and pulls the object, the right
	one turns it - forge_rotate_update -; the camera stays) */
	if (!turning_object)
	{
		input->translation.i = pad.sticks[_gamepad_stick_left][1] * speed;
		input->translation.j = -pad.sticks[_gamepad_stick_left][0] * speed;
	}
	input->translation.k = ((pad.down[_forge_button_fly_up] ? 1.f : 0.f) - (pad.down[_forge_button_fly_down] ? 1.f : 0.f)) * speed;
	if (!turning_object)
	{
		input->facing_delta.yaw = -pad.sticks[_gamepad_stick_right][0] * FORGE_CAMERA_LOOK_RATE * seconds + pad.look_yaw;
		input->facing_delta.pitch = pad.sticks[_gamepad_stick_right][1] * FORGE_CAMERA_LOOK_RATE * seconds * 0.6f +
			pad.look_pitch;
	}
	else
	{
		/* (the mouse, or the gyro, turns the object: its own update takes
		it) */
		forge_globals.pending_mouse_turn += pad.look_yaw;
		forge_globals.pending_mouse_pitch += pad.look_pitch;
	}

	return TRUE;
}

void forge_update(
	void)
{
	struct forge_pad pad;
	boolean pressed[NUMBER_OF_GAMEPAD_BUTTONS];
	boolean toggle_pressed;
	short button;
	real seconds = 1.f / 30.f;
	boolean menu_open;
	long player_index;

	if (forge_globals.test_buttons_frames > 0)
		forge_globals.test_buttons_frames--;
	if (forge_globals.test_sticks_frames > 0)
		forge_globals.test_sticks_frames--;
	forge_session_update();
	forge_game_playing = forge_globals.active || forge_globals.game_active;
	if (forge_globals.save_requested)
	{
		if (forge_game_playing && !forge_globals.game_requested)
			forge_variant_save();
		else
			forge_globals.save_requested = FALSE;
	}
	/* (the Forge menu's and the properties window's) */
	if (forge_globals.undo_requested)
	{
		short step = forge_globals.undo_requested;

		forge_globals.undo_requested = 0;
		if (forge_globals.active)
			forge_undo_step(step > 0);
	}
	if (forge_globals.snap_requested)
	{
		forge_globals.snap_requested = FALSE;
		forge_globals.snap = !forge_globals.snap;
		forge_menu_notice("%s", forge_globals.snap ? T("Snap: On") : T("Snap: Off"));
	}
	if (forge_globals.property_requested)
	{
		forge_globals.property_requested = FALSE;
		forge_property_apply(forge_globals.property_request, forge_globals.property_step);
	}
	if (forge_game_playing)
		forge_ai_hold();
#ifndef HALO_RELEASE
	if (forge_globals.game_active && forge_globals.ai_shots_counted)
		forge_ai_shots_update();
#endif
	if (forge_globals.ai_all_requested)
	{
		forge_globals.ai_all_requested = FALSE;
		forge_ai_all_set(forge_ai_state() == _forge_ai_all_off);
	}
	if (forge_globals.clear_ai_requested)
	{
		short clear = forge_globals.clear_ai_requested;

		forge_globals.clear_ai_requested = _forge_clear_ai_none;
		if (forge_globals.game_active && !forge_globals.game_requested)
			forge_ai_clear(clear == _forge_clear_ai_all);
	}
	/* (NEW ROUND: once the Forge menu has gone and the game runs again -
	the engine resets a map only then) */
	if (forge_globals.round_requested)
	{
		if (!forge_globals.game_active || forge_globals.game_requested || forge_globals.round_pending ||
			forge_globals.round_resetting)
		{
			forge_globals.round_requested = FALSE;
		}
		else if (!forge_menu_open() && !game_time_get_paused())
		{
			forge_globals.round_requested = FALSE;
			forge_new_round_start();
		}
	}
	if (!forge_game_playing)
		return;
	player_index = local_player_get_player_index(forge_globals.local_player_index);
	if (player_index == NONE || game_connection() != _game_connection_local)
	{
		/* (no player, or not a local game: the monitor's state is gone; the
		player's own camera is the director's again) */
		if (forge_globals.active)
		{
			forge_globals.active = FALSE;
			forge_monitor_flying = 0;
			forge_globals.held_object_index = NONE;
			forge_globals.unit_index = NONE;
			if (player_index != NONE)
				director_forge_camera(forge_globals.local_player_index, FALSE, NULL, NULL);
			platform_log("forge: the monitor is gone (a new map, or no player)");
		}
		return;
	}

	forge_pad_get(&pad);
	for (button = 0; button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
	{
		pressed[button] = pad.down[button] && !forge_globals.was_down[button];
		forge_globals.was_down[button] = pad.down[button];
	}
	toggle_pressed = pad.toggle && !forge_globals.toggle_was_down;
	forge_globals.toggle_was_down = pad.toggle;
	menu_open = forge_menu_open();
	/* (Leave asked: once its A is let go, the main menu - a button still
	held would press the main menu's first item as it opens) */
	if (forge_globals.quit_requested)
	{
		if (!pad.down[_gamepad_analog_button_a] && !pad.down[_gamepad_binary_button_start])
		{
			forge_globals.quit_requested = FALSE;
			platform_log("forge: leaving to the main menu");
			main_goto_main_menu();
		}
		return;
	}
	/* (the browser or the properties closed: the choice made, or B or
	Square) */
	if (forge_globals.close_requested || (forge_globals.browser_chosen && menu_open))
	{
		forge_globals.close_requested = FALSE;
		if (menu_open)
		{
			ui_widgets_close_all();
			menu_open = FALSE;
		}
	}
	/* (a menu over the game closed since the last frame, by whatever way:
	the press that closed it is not the monitor's too - B would copy the
	object held) */
	if (forge_globals.menu_was_open && !menu_open)
	{
		for (button = 0; button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
			pressed[button] = FALSE;
		toggle_pressed = FALSE;
	}
	forge_globals.menu_was_open = menu_open;
	forge_placed_update();
	forge_homes_update();
	if (forge_globals.game_active && pressed[_gamepad_binary_button_start] && !menu_open && !forge_globals.game_requested)
	{
		forge_menu_open_pause();
		menu_open = forge_menu_open();
	}
	forge_globals.hud_hidden = menu_open && game_time_get_paused();

	/* (the Spartan: D-pad up, or the Forge menu's choice, is the monitor) */
	if (!forge_globals.active)
	{
		forge_globals.browser_chosen = FALSE;
		if ((toggle_pressed && !menu_open) || forge_globals.toggle_requested)
		{
			forge_globals.toggle_requested = FALSE;
			forge_begin();
		}
		return;
	}

	/* (the player respawned while the monitor flew: the new unit waits
	hidden as the last did) */
	if (player_get(player_index)->unit_index != forge_globals.unit_index)
	{
		if (object_try_and_get(forge_globals.unit_index))
		{
			forge_unit_set_visible(forge_globals.unit_index, TRUE);
			if (forge_globals.unit_could_take_damage)
				SET_FLAG(object_get(forge_globals.unit_index)->object.damage_flags, _object_cannot_take_damage_bit, FALSE);
		}
		forge_unit_hide(player_get(player_index)->unit_index);
	}

	if (forge_globals.toggle_requested || (toggle_pressed && !menu_open))
	{
		forge_globals.toggle_requested = FALSE;
		forge_end();
		return;
	}
	/* (the browser's choice, spawned here, held) */
	if (forge_globals.browser_chosen)
	{
		short choice = forge_globals.browser_choice;

		forge_globals.browser_chosen = FALSE;
		if (choice >= 0 && choice < forge_globals.palette_count)
		{
			if (object_try_and_get(forge_globals.held_object_index))
				forge_drop();
			forge_spawn(forge_globals.palette[choice].tag_index);
		}
	}
	if (!menu_open)
	{
		/* (Square: holding an object, its properties; else the browser) */
		if (pressed[_forge_button_browser])
		{
			if (object_try_and_get(forge_globals.held_object_index))
				forge_properties_open();
			else
				forge_browser_open();
		}
		boolean rotating = pad.down[_forge_button_alternate] && object_try_and_get(forge_globals.held_object_index);

		/* (D-pad right: in rotate mode its snapping, else the movement
		speed) */
		if (pressed[_forge_button_speed] && rotating)
		{
			char snap[16];

			forge_globals.rotate_snap = (short)((forge_globals.rotate_snap + 1) % (short)NUMBEROF(forge_rotate_snaps));
			forge_rotate_snap_text(snap, sizeof(snap));
			forge_notice(T("Rotation snap: %s"), snap);
		}
		else if (pressed[_forge_button_speed])
		{
			forge_globals.speed = (short)((forge_globals.speed + 1) % NUMBER_OF_FORGE_SPEEDS);
			forge_notice("%s", T(forge_speed_names[forge_globals.speed]));
		}
		if (pressed[_forge_button_straighten])
			forge_straighten(forge_target_object());
		if (pressed[_forge_button_duplicate])
			forge_duplicate();
		/* (R in rotate mode: roll, never a drop) */
		if (pressed[_forge_button_grab] && !rotating)
		{
			if (object_try_and_get(forge_globals.held_object_index))
			{
				forge_drop();
			}
			else
			{
				long object_index = forge_object_under_crosshair();

				if (object_index != NONE)
					forge_grab(object_index);
			}
		}
		if (pressed[_forge_button_delete])
			forge_delete(forge_target_object());
		rotating = rotating && object_try_and_get(forge_globals.held_object_index);
		if (rotating)
		{
			forge_rotate_update(&pad, seconds);
		}
		else
		{
			short axis;

			for (axis = 0; axis < NUMBER_OF_FORGE_AXES; axis++)
			{
				forge_globals.rotate_flicked[axis] = FALSE;
				forge_globals.rotate_mouse[axis] = 0.f;
			}
		}
		forge_globals.rotating = rotating;
		if (object_try_and_get(forge_globals.held_object_index))
		{
			forge_globals.held_distance = PIN(forge_globals.held_distance + pad.wheel * FORGE_WHEEL_PUSH,
				FORGE_MINIMUM_HOLD_DISTANCE, FORGE_MAXIMUM_HOLD_DISTANCE);
		}
	}
	else
	{
		forge_globals.rotating = FALSE;
	}
	forge_globals.pending_mouse_turn = 0.f;
	forge_globals.pending_mouse_pitch = 0.f;
	if (object_try_and_get(forge_globals.held_object_index))
		forge_held_object_place();
	forge_hud_update();

	return;
}

/* a line of the monitor's HUD in a font, at bounds (justification: 0 left,
2 centred), in a colour */
static void forge_draw_line(
	long font_index,
	rectangle2d const *bounds,
	short justification,
	real red,
	real green,
	real blue,
	char const *line)
{
	real_argb_color color;
	unsigned short text[FORGE_LINE_SIZE];

	color.alpha = 1.f;
	color.red = red;
	color.green = green;
	color.blue = blue;
	draw_string_set_draw_mode(font_index, NONE, justification, 0, &color);
	halo_menus_utf16(line, text, NUMBEROF(text));
	rasterizer_draw_unicode_string(bounds, NULL, NULL, 0, (wchar_t const *)text);
}

static short forge_font_height(
	long font_index)
{
	struct font_header *font = font_definition_get(font_index);

	return (short)(font->ascending_height + font->descending_height);
}

void forge_draw(
	short local_player_index)
{
	long font_index;
	long label_font_index;
	long crosshair_font_index;
	rectangle2d bounds;
	rectangle2d window;
	short height;
	short label_height;
	short crosshair_height;

	if (!forge_globals.active || local_player_index != forge_globals.local_player_index || forge_globals.hud_hidden)
		return;
	/* (the Halo 3 menus' fonts when the map has them, else the HUD's) */
	font_index = forge_globals.hud_font_looked_up ? forge_globals.hud_font_index : NONE;
	label_font_index = forge_globals.hud_font_looked_up ? forge_globals.hud_label_font_index : NONE;
	if (font_index == NONE)
		font_index = hud_get_font_index();
	if (font_index == NONE)
		return;
	crosshair_font_index = forge_globals.hud_font_looked_up ? forge_globals.hud_crosshair_font_index : NONE;
	if (label_font_index == NONE)
		label_font_index = font_index;
	if (crosshair_font_index == NONE)
		crosshair_font_index = label_font_index;
	height = forge_font_height(font_index);
	label_height = forge_font_height(label_font_index);
	crosshair_height = forge_font_height(crosshair_font_index);
	window = render.camera.window_bounds;

	/* the map and the budget, top left */
	bounds = window;
	bounds.x0 = (short)(window.x0 + 24);
	bounds.y0 = (short)(window.y0 + 20);
	bounds.y1 = (short)(bounds.y0 + height);
	forge_draw_line(font_index, &bounds, 0, 0.93f, 0.95f, 0.97f, forge_globals.hud_title);
	bounds.y0 = bounds.y1;
	bounds.y1 = (short)(bounds.y0 + height);
	forge_draw_line(font_index, &bounds, 0, 0.62f, 0.71f, 0.80f, forge_globals.hud_budget);
	/* the notice, a while, at the left below the middle (where Halo
	Online's Forge says "Movement Speed: Fast") */
	if (forge_globals.notice[0] && system_milliseconds() < forge_globals.notice_until)
	{
		bounds.y0 = (short)(window.y0 + (window.y1 - window.y0) * 62 / 100);
		bounds.y1 = (short)(bounds.y0 + height);
		forge_draw_line(font_index, &bounds, 0, 0.62f, 0.82f, 1.f, forge_globals.notice);
	}

	/* the crosshair, and under it what it is on (ice blue) or what is
	held (orange) */
	bounds = window;
	bounds.y0 = (short)((window.y0 + window.y1) / 2 - crosshair_height / 2);
	bounds.y1 = (short)(bounds.y0 + crosshair_height);
	if (forge_globals.hud_label_held)
		forge_draw_line(crosshair_font_index, &bounds, 2, 0.95f, 0.62f, 0.30f, "[ + ]");
	else if (forge_globals.hud_label[0])
		forge_draw_line(crosshair_font_index, &bounds, 2, 0.47f, 0.84f, 1.f, "[ + ]");
	else
		forge_draw_line(crosshair_font_index, &bounds, 2, 0.85f, 0.9f, 0.95f, "( + )");
	if (forge_globals.hud_label[0])
	{
		bounds.y0 = (short)(bounds.y1 + 2);
		bounds.y1 = (short)(bounds.y0 + label_height);
		if (forge_globals.hud_label_held)
			forge_draw_line(label_font_index, &bounds, 2, 0.95f, 0.62f, 0.30f, forge_globals.hud_label);
		else
			forge_draw_line(label_font_index, &bounds, 2, 0.47f, 0.84f, 1.f, forge_globals.hud_label);
	}
	/* (holding one: its angles while rotating, the snapping) */
	if (forge_globals.hud_rotate[0])
	{
		bounds.y0 = (short)(bounds.y1 + 2);
		bounds.y1 = (short)(bounds.y0 + height);
		forge_draw_line(font_index, &bounds, 2, forge_globals.rotating ? 0.95f : 0.62f,
			forge_globals.rotating ? 0.78f : 0.71f, forge_globals.rotating ? 0.55f : 0.80f, forge_globals.hud_rotate);
	}
}

static boolean forge_word(
	char const **text,
	char *word,
	size_t size)
{
	size_t length = 0;
	char const *character = *text;

	while (*character == ' ' || *character == '\t')
		character++;
	while (*character && *character != ' ' && *character != '\t' && *character != ')')
	{
		if (length + 1 < size)
			word[length++] = *character;
		character++;
	}
	word[length] = 0;
	*text = character;
	return length > 0;
}

/* the rest of the text, spaces and a closing parenthesis trimmed */
static void forge_rest(
	char const *text,
	char *rest,
	size_t size)
{
	size_t length;

	while (*text == ' ' || *text == '\t' || *text == '"')
		text++;
	csstrncpy(rest, text, size - 1);
	rest[size - 1] = 0;
	length = strlen(rest);
	while (length > 0 && (rest[length - 1] == ' ' || rest[length - 1] == ')' || rest[length - 1] == '"' ||
		rest[length - 1] == '\r' || rest[length - 1] == '\n'))
	{
		rest[--length] = 0;
	}

	return;
}

static short forge_button_from_name(
	char const *name)
{
	static struct
	{
		char const *name;
		short button;
	} const names[] =
	{
		{ "a", _gamepad_analog_button_a }, { "b", _gamepad_analog_button_b },
		{ "x", _gamepad_analog_button_x }, { "y", _gamepad_analog_button_y },
		{ "black", _gamepad_analog_button_black }, { "white", _gamepad_analog_button_white },
		{ "lt", _gamepad_analog_button_left_trigger }, { "rt", _gamepad_analog_button_right_trigger },
		{ "ls", _gamepad_binary_button_left_thumb }, { "rs", _gamepad_binary_button_right_thumb },
	};
	short index;

	for (index = 0; index < (short)NUMBEROF(names); index++)
	{
		if (!strcmp(names[index].name, name))
			return names[index].button;
	}
	return NONE;
}

boolean forge_command(
	char const *arguments)
{
	char word[32];
	char rest[160];
	char const *text = arguments;

	if (!forge_word(&text, word, sizeof(word)))
	{
		if (forge_globals.active)
			forge_end();
		else
			forge_begin();
		return TRUE;
	}
	forge_rest(text, rest, sizeof(rest));

	if (!strcmp(word, "on"))
		return forge_begin();
	if (!strcmp(word, "off"))
	{
		forge_end();
		return TRUE;
	}
	/* (the probes need no monitor: any map, any game) */
	if (!strcmp(word, "deps"))
	{
		char name[160];
		char const *file_name = "";
		char *arrow;
		struct tag_iterator iterator;
		long tag_index;

		/* ("deps <name> > <file>") */
		csstrncpy(name, rest, sizeof(name) - 1);
		name[sizeof(name) - 1] = 0;
		if ((arrow = strstr(name, " > ")) != NULL)
		{
			*arrow = 0;
			file_name = arrow + 3;
		}
		tag_iterator_new(&iterator, NONE);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE)
		{
			char const *tag_name = tag_get_name(tag_index);

			if (tag_name && strstr(tag_name, name) && forge_tag_category(tag_index) != NONE)
			{
				forge_dependencies_log(tag_index, file_name);
				return TRUE;
			}
		}
		platform_log("forge: no object tag named %s", name);
		return FALSE;
	}
	if (!strcmp(word, "tags"))
	{
		forge_tags_write(rest);
		return TRUE;
	}
	if (!strcmp(word, "list"))
	{
		short index;
		short category;

		forge_palette_build();
		for (category = 0; category < NUMBER_OF_FORGE_CATEGORIES; category++)
		{
			platform_log("forge: palette %s: %d", forge_category_log_names[category],
				forge_globals.palette_counts[category]);
			if (strcmp(rest, forge_category_log_names[category]))
				continue;
			for (index = 0; index < forge_globals.palette_counts[category]; index++)
			{
				struct forge_palette_entry const *entry =
					&forge_globals.palette[forge_globals.palette_first[category] + index];

				platform_log("forge: %s %d: %s (%s)%s%s", rest, index, entry->name, tag_get_name(entry->tag_index),
					entry->category == _forge_category_characters ? ", " : "",
					entry->category == _forge_category_characters ? forge_ai_group_log_names[entry->group] : "");
			}
		}
		return TRUE;
	}
	if (!strcmp(word, "budget"))
	{
		forge_placed_update();
		platform_log("forge: budget %d/%d objects, %d/%d AI (%d alive, %d dead); HUD: %s", forge_globals.placed_count,
			FORGE_MAXIMUM_OBJECTS, forge_globals.ai_count, FORGE_MAXIMUM_AI, forge_globals.ai_count - forge_globals.ai_dead_count,
			forge_globals.ai_dead_count, forge_globals.hud_budget);
		return TRUE;
	}
	if (!strcmp(word, "clearai"))
	{
		/* ("clearai dead|all": as the Forge menu's CLEAR DEAD AI and CLEAR
		ALL AI) */
		if (strcmp(rest, "dead") && strcmp(rest, "all"))
			return FALSE;
		forge_ai_clear(!strcmp(rest, "all"));
		return TRUE;
	}
	if (!strcmp(word, "press") || !strcmp(word, "stick"))
	{
		/* (the tests' controller: works with or without the monitor, so a
		press can start it) */
		if (!strcmp(word, "press"))
		{
			char names[64];
			char *name;
			char *end;
			int frames = 2;

			names[0] = 0;
			sscanf(rest, "%63s %d", names, &frames);
			csmemset(forge_globals.test_down, 0, sizeof(forge_globals.test_down));
			forge_globals.test_toggle = FALSE;
			for (name = strtok_r(names, "+", &end); name; name = strtok_r(NULL, "+", &end))
			{
				short button = forge_button_from_name(name);

				if (button != NONE)
					forge_globals.test_down[button] = TRUE;
				/* ("combo": the monitor and the Spartan's) */
				if (!strcmp(name, "combo"))
					forge_globals.test_toggle = TRUE;
			}
			forge_globals.test_buttons_frames = (short)PIN(frames, 1, 3000);
		}
		else
		{
			float lx = 0.f, ly = 0.f, rx = 0.f, ry = 0.f;
			int frames = 30;

			if (sscanf(rest, "%f %f %f %f %d", &lx, &ly, &rx, &ry, &frames) < 4)
				return FALSE;
			forge_globals.test_sticks[_gamepad_stick_left][0] = lx;
			forge_globals.test_sticks[_gamepad_stick_left][1] = ly;
			forge_globals.test_sticks[_gamepad_stick_right][0] = rx;
			forge_globals.test_sticks[_gamepad_stick_right][1] = ry;
			forge_globals.test_sticks_frames = (short)PIN(frames, 1, 3000);
		}
		return TRUE;
	}
	if (!strcmp(word, "look"))
	{
		/* ("look <yaw> <pitch>": the mouse's look, radians - positive yaw
		left, positive pitch up -, as the keyboard's port gives it, once) */
		float yaw = 0.f, pitch = 0.f;

		if (sscanf(rest, "%f %f", &yaw, &pitch) < 2)
			return FALSE;
		forge_globals.test_look_yaw += yaw;
		forge_globals.test_look_pitch += pitch;
		return TRUE;
	}

	/* (the layout's state: with or without the monitor) */
	if (!strcmp(word, "ai") || !strcmp(word, "objects"))
	{
		short index;
		boolean characters = !strcmp(word, "ai");

		forge_globals.ai_shots_counted = TRUE;

		forge_placed_update();
		for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
		{
			struct forge_placed const *placed = &forge_globals.placed[index];
			struct object_datum *object = forge_object_alive(placed->object_index) ? object_get(placed->object_index) : NULL;
			short entry = forge_palette_of_tag(placed->tag_index);
			char const *name = entry != NONE ? forge_globals.palette[entry].name : forge_tag_leaf(placed->tag_index);
			char const *state = !object ? "gone" : TEST_FLAG(object->object.damage_flags, _object_dead_bit) ?
				(!placed->ai ? "destroyed" : forge_placed_dead(placed) ? "dead" : "down") : "alive";

			if (characters && !placed->ai)
				continue;
			if (characters)
			{
				real_vector3d aim = { 0.f, 0.f, 0.f };
				real_vector3d facing = { 0.f, 0.f, 0.f };
				long actor_index = forge_ai_actor(placed->object_index);
				struct actor_datum const *actor = actor_index != NONE ? actor_get(actor_index) : NULL;

				char const *weapon = "none";

				if (object)
				{
					struct unit_datum *unit = unit_get(placed->object_index);
					short slot = unit->unit.current_weapon_index;
					struct object_datum *held = slot >= 0 && slot < MAXIMUM_WEAPONS_PER_UNIT ?
						object_try_and_get(unit->unit.weapon_object_indices[slot]) : NULL;

					unit_get_aiming_vector(placed->object_index, &aim);
					unit_get_facing_vector(placed->object_index, &facing);
					if (held)
						weapon = forge_tag_leaf(held->definition_index);
				}
				platform_log("forge: ai %d: %s (id %ld) %s at %.2f %.2f %.2f, AI team %s, engine team %d, AI %s, actor %s, "
					"aim %.3f %.3f %.3f, facing %.3f %.3f %.3f, mode %d action %d, weapon %s, body %.2f, shield %.2f",
					index, name, placed->id, state, object ? object->object.position.x : 0.f,
					object ? object->object.position.y : 0.f, object ? object->object.position.z : 0.f,
					forge_ai_team_log_names[placed->ai_team % NUMBER_OF_FORGE_AI_TEAMS],
					object ? object->object.owner_team_index : NONE, (placed->flags & FORGE_RECORD_AI_OFF) ? "off" : "on",
					actor ? "yes" : "none", aim.i, aim.j, aim.k, facing.i, facing.j, facing.k, actor ? actor->state.mode : NONE,
					actor ? actor->state.action : NONE, weapon, object ? object->object.body_vitality : 0.f,
					object ? object->object.shield_vitality : 0.f);
			}
			else
			{
				platform_log("forge: object %d: %s (id %ld) %s at %.2f %.2f %.2f, placed at %.2f %.2f %.2f facing %.3f %.3f %.3f "
					"up %.3f %.3f %.3f", index, name, placed->id, state, object ? object->object.position.x : 0.f,
					object ? object->object.position.y : 0.f, object ? object->object.position.z : 0.f, placed->home.position.x,
					placed->home.position.y, placed->home.position.z, placed->home.forward.i, placed->home.forward.j,
					placed->home.forward.k, placed->home.up.i, placed->home.up.j, placed->home.up.k);
			}
		}
		if (characters)
		{
			struct object_iterator iterator;
			struct unit_datum *unit;
			short others = 0;
			short others_off = 0;

			/* (the game's own: a carrier's infection forms) */
			object_iterator_new(&iterator, _object_mask_unit, 0);
			while ((unit = (struct unit_datum *)object_iterator_next(&iterator)) != NULL)
			{
				long actor_index = unit->unit.actor_index != NONE ? unit->unit.actor_index : unit->unit.swarm_actor_index;

				if (actor_index == NONE || TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ||
					forge_placed_of_object(iterator.index))
				{
					continue;
				}
				others++;
				others_off += actor_get(actor_index)->state.mode == FORGE_ACTOR_MODE_BRAINDEAD ? 1 : 0;
			}
			platform_log("forge: ai: %d characters not of the layout alive (%d with their AI off); ALL AI %s", others,
				others_off, forge_ai_state() == _forge_ai_all_on ? "on" : forge_ai_state() == _forge_ai_all_off ? "off" : "mixed");
		}
		platform_log("forge: %s listed: %d objects, %d characters in the layout (%d dead); %ld shots by characters so far",
			word, forge_globals.placed_count, forge_globals.ai_count, forge_globals.ai_dead_count, forge_globals.ai_shots);
		return TRUE;
	}
	if (!strcmp(word, "round"))
	{
		forge_new_round_request();
		return TRUE;
	}
	if (!strcmp(word, "allai"))
	{
		forge_ai_all_set(!strcmp(rest, "on") ? TRUE : !strcmp(rest, "off") ? FALSE : forge_ai_state() == _forge_ai_all_off);
		return TRUE;
	}
	/* (tests, the monitor's or the Spartan's: forge_blocks.c) */
	if (!strcmp(word, "blocks"))
		return forge_blocks_command(rest);
	if (!forge_globals.active && strcmp(word, "where"))
	{
		platform_log("forge: %s: not in monitor mode (\"forge on\")", word);
		return FALSE;
	}
	if (!strcmp(word, "aiteam") || !strcmp(word, "brain"))
	{
		struct forge_placed *placed = forge_placed_of_object(forge_target_object());
		short team;

		if (!placed || !placed->ai)
		{
			platform_log("forge: %s: no character of the layout held or aimed at", word);
			return FALSE;
		}
		if (!strcmp(word, "brain"))
		{
			if (!strcmp(rest, "off"))
				placed->flags |= FORGE_RECORD_AI_OFF;
			else
				placed->flags &= (unsigned char)~FORGE_RECORD_AI_OFF;
		}
		else
		{
			for (team = 0; team < NUMBER_OF_FORGE_AI_TEAMS && strcmp(rest, forge_ai_team_log_names[team]); team++)
				;
			if (team >= NUMBER_OF_FORGE_AI_TEAMS)
				return FALSE;
			placed->ai_team = (unsigned char)team;
		}
		forge_ai_apply(placed);
		platform_log("forge: %s: AI team %s (engine team %d), AI %s", forge_tag_leaf(placed->tag_index),
			forge_ai_team_log_names[placed->ai_team % NUMBER_OF_FORGE_AI_TEAMS],
			forge_ai_team_index(placed->ai_team, placed->object_index), (placed->flags & FORGE_RECORD_AI_OFF) ? "off" : "on");
		return TRUE;
	}
	if (!strcmp(word, "destroy") || !strcmp(word, "shove") || !strcmp(word, "kill"))
	{
		long object_index = forge_target_object();
		struct object_datum *object;

		/* ("#<id>": the layout's object of that id instead) */
		if (rest[0] == '#')
		{
			long id = atol(rest + 1);
			short index;
			char *space = strchr(rest, ' ');

			object_index = NONE;
			for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
			{
				if (forge_globals.placed[index].id == id)
					object_index = forge_globals.placed[index].object_index;
			}
			memmove(rest, space ? space + 1 : rest + strlen(rest), strlen(space ? space + 1 : rest + strlen(rest)) + 1);
		}
		object = object_try_and_get(object_index);

		if (!object)
			return FALSE;
		if (object_index == forge_globals.held_object_index)
			forge_drop();
		if (!strcmp(word, "destroy"))
		{
			platform_log("forge: destroyed %s", forge_tag_leaf(object->definition_index));
			/* (a unit as the engine destroys one: a carrier's infection
			forms spawned) */
			if (FLAG(object->object.type) & _object_mask_unit)
				unit_destroy(object_index);
			else
				object_destroy(object_index);
		}
		else if (!strcmp(word, "kill"))
		{
			/* (a unit dies as in play: a vehicle's wreck, a character's
			body stay) */
			if (!(FLAG(object->object.type) & _object_mask_unit))
				return FALSE;
			platform_log("forge: killed %s", forge_tag_leaf(object->definition_index));
			unit_kill(object_index);
		}
		else
		{
			float i = 0.f, j = 0.f, k = 0.f;

			if (sscanf(rest, "%f %f %f", &i, &j, &k) != 3)
				return FALSE;
			object_activate(object_index);
			object->object.translational_velocity.i = i;
			object->object.translational_velocity.j = j;
			object->object.translational_velocity.k = k;
			SET_FLAG(object->object.flags, _object_at_rest_bit, FALSE);
			platform_log("forge: shoved %s", forge_tag_leaf(object->definition_index));
		}
		return TRUE;
	}
	if (!strcmp(word, "spawn") || !strcmp(word, "pick"))
	{
		short index;

		forge_palette_ready();
		index = forge_palette_find(rest);
		if (index == NONE)
		{
			platform_log("forge: no palette tag named %s", rest);
			return FALSE;
		}
		forge_globals.palette_index = index;
		if (!strcmp(word, "pick"))
			return TRUE;
		if (object_try_and_get(forge_globals.held_object_index))
			forge_drop();
		return forge_spawn(forge_globals.palette[index].tag_index) != NONE;
	}
	if (!strcmp(word, "seats"))
	{
		/* ("seats <name>": a vehicle of the palette's seats: label, driver
		or gunner, a built-in actor) */
		short index;

		forge_palette_ready();
		index = forge_palette_find(rest);
		if (index == NONE || tag_get_group_tag(forge_globals.palette[index].tag_index) != VEHICLE_DEFINITION_TAG)
		{
			platform_log("forge: seats: no vehicle named %s", rest);
			return FALSE;
		}
		{
			struct unit_definition *unit = tag_get(UNIT_DEFINITION_TAG, forge_globals.palette[index].tag_index);
			long seat;

			platform_log("forge: seats of %s: %ld", forge_globals.palette[index].name, (long)unit->unit.seats.count);
			for (seat = 0; seat < unit->unit.seats.count; seat++)
			{
				struct unit_seat *element = TAG_BLOCK_GET_ELEMENT(&unit->unit.seats, seat, struct unit_seat);

				platform_log("forge: seat %ld: %s%s%s%s%s", seat, element->label,
					TEST_FLAG(element->flags, _unit_seat_driver_bit) ? ", driver" : "",
					TEST_FLAG(element->flags, _unit_seat_gunner_bit) ? ", gunner" : "",
					element->built_in_actor_reference.index != NONE ? ", built-in actor " : "",
					element->built_in_actor_reference.index != NONE ? tag_get_name(element->built_in_actor_reference.index) : "");
			}
		}
		return TRUE;
	}
	if (!strcmp(word, "undo") || !strcmp(word, "redo"))
	{
		forge_undo_step(!strcmp(word, "redo"));
		return TRUE;
	}
	if (!strcmp(word, "snap"))
	{
		forge_globals.snap = strcmp(rest, "off") != 0;
		platform_log("forge: snapping %s", forge_globals.snap ? "on" : "off");
		return TRUE;
	}
	if (!strcmp(word, "property"))
	{
		/* ("property team|respawn|start": the held object's stepped) */
		short property = !strcmp(rest, "team") ? _forge_property_team : !strcmp(rest, "respawn") ? _forge_property_respawn :
			!strcmp(rest, "start") ? _forge_property_place_at_start : NONE;

		short step = 1;

		/* ("property yaw|pitch|roll [-]": an exact angle stepped, back with
		"-"; "property resetrot") */
		if (!strncmp(rest, "aiteam", 6))
			property = _forge_property_ai_team;
		else if (!strncmp(rest, "brain", 5))
			property = _forge_property_ai_brain;
		else if (!strncmp(rest, "yaw", 3))
			property = _forge_property_yaw;
		else if (!strncmp(rest, "pitch", 5))
			property = _forge_property_pitch;
		else if (!strncmp(rest, "roll", 4))
			property = _forge_property_roll;
		else if (!strcmp(rest, "resetrot"))
			property = _forge_property_reset_rotation;
		if (strchr(rest, '-'))
			step = -1;
		if (property == NONE || !forge_properties_target())
			return FALSE;
		forge_property_apply(property, step);
		return TRUE;
	}
	if (!strcmp(word, "save"))
	{
		/* ("save <title>": as the Forge menu's SAVE MAP VARIANT) */
		forge_save_request(rest, "");
		return TRUE;
	}
	if (!strcmp(word, "fill"))
	{
		/* ("fill <count> <name>": that many spawned and let go on a grid
		in front of the camera, as many as the budget allows: its tests) */
		int count = 0;
		int offset = 0;
		int placed = 0;
		short index;
		real_point3d position;
		real_vector3d forward, left;

		if (sscanf(rest, "%d %n", &count, &offset) < 1 || offset <= 0)
			return FALSE;
		forge_palette_ready();
		index = forge_palette_find(rest + offset);
		if (index == NONE || !forge_camera_get(&position, &forward))
		{
			platform_log("forge: no palette tag named %s", rest + offset);
			return FALSE;
		}
		left.i = -forward.j;
		left.j = forward.i;
		left.k = 0.f;
		if (normalize3d(&left) < 0.0001f)
			left = *global_left3d;
		for (placed = 0; placed < PIN(count, 0, 1000); placed++)
		{
			long object_index;
			real_point3d point;
			real across = (real)(placed % 16 - 8) * 1.2f;
			real along = 4.f + (real)(placed / 16) * 1.2f;

			if (object_try_and_get(forge_globals.held_object_index))
				forge_drop();
			object_index = forge_spawn(forge_globals.palette[index].tag_index);
			if (object_index == NONE)
				break;
			point.x = position.x + forward.i * along + left.i * across;
			point.y = position.y + forward.j * along + left.j * across;
			point.z = position.z;
			object_set_position(object_index, &point, NULL, NULL);
			forge_drop();
		}
		platform_log("forge: filled %d of %d %s", placed, count, forge_globals.palette[index].name);
		return TRUE;
	}
	if (!strcmp(word, "grab"))
	{
		long object_index = forge_object_under_crosshair();

		/* ("#<id>": the layout's object of that id instead) */
		if (rest[0] == '#')
		{
			long id = atol(rest + 1);
			short index;

			object_index = NONE;
			for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
			{
				if (forge_globals.placed[index].id == id && forge_object_alive(forge_globals.placed[index].object_index))
					object_index = forge_globals.placed[index].object_index;
			}
		}
		if (object_index == NONE)
		{
			platform_log("forge: nothing under the crosshair");
			return FALSE;
		}
		forge_grab(object_index);
		return TRUE;
	}
	if (!strcmp(word, "drop"))
	{
		forge_drop();
		return TRUE;
	}
	if (!strcmp(word, "turn"))
	{
		long object_index = forge_target_object();

		if (object_index == NONE)
			return FALSE;
		forge_turn(object_index, DEGREES_TO_RADIANS((real)atof(rest)));
		platform_log("forge: turned %s by %s degrees", forge_tag_leaf(object_get(object_index)->definition_index), rest);
		return TRUE;
	}
	if (!strcmp(word, "distance"))
	{
		forge_globals.held_distance = PIN((real)atof(rest), FORGE_MINIMUM_HOLD_DISTANCE, FORGE_MAXIMUM_HOLD_DISTANCE);
		return TRUE;
	}
	if (!strcmp(word, "snap"))
	{
		/* (the Forge menu's SNAP, flipped or set) */
		forge_globals.snap = !strcmp(rest, "on") ? TRUE : !strcmp(rest, "off") ? FALSE : !forge_globals.snap;
		platform_log("forge: snapping %s", forge_globals.snap ? "on" : "off");
		return TRUE;
	}
	if (!strcmp(word, "delete"))
	{
		long object_index = forge_target_object();

		if (object_index == NONE)
		{
			platform_log("forge: nothing to delete");
			return FALSE;
		}
		forge_delete(object_index);
		return TRUE;
	}
	if (!strcmp(word, "camera"))
	{
		float x, y, z, yaw, pitch;

		if (sscanf(rest, "%f %f %f %f %f", &x, &y, &z, &yaw, &pitch) == 5)
		{
			real_point3d position = { x, y, z };
			real_euler_angles2d angles = { DEGREES_TO_RADIANS(yaw), DEGREES_TO_RADIANS(pitch) };
			real_vector3d forward;

			vector3d_from_euler_angles2d(&forward, &angles);
			director_forge_camera(forge_globals.local_player_index, TRUE, &position, &forward);
			return TRUE;
		}
		return FALSE;
	}
	if (!strcmp(word, "where"))
	{
		real_point3d position;
		real_vector3d forward;
		struct object_datum *held = object_try_and_get(forge_globals.held_object_index);
		long aimed = forge_object_under_crosshair();
		long player_index = local_player_get_player_index(forge_globals.local_player_index);
		long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;

		forge_camera_get(&position, &forward);
		platform_log("forge: camera at %.2f %.2f %.2f looking %.2f %.2f %.2f; holding %s at %.2f; aiming at %s; "
			"%ld spawned, %ld deleted; budget %d/%d objects, %d/%d AI",
			position.x, position.y, position.z, forward.i, forward.j, forward.k,
			held ? forge_tag_leaf(held->definition_index) : "nothing", forge_globals.held_distance,
			aimed != NONE ? forge_tag_leaf(object_get(aimed)->definition_index) : "nothing",
			forge_globals.spawned_count, forge_globals.deleted_count, forge_globals.placed_count, FORGE_MAXIMUM_OBJECTS,
			forge_globals.ai_count, FORGE_MAXIMUM_AI);
		if (object_try_and_get(unit_index))
		{
			struct object_datum *unit = object_get(unit_index);

			platform_log("forge: the player's unit at %.2f %.2f %.2f, %s, %s, team %d, body %.2f, shield %.2f",
				unit->object.position.x, unit->object.position.y, unit->object.position.z,
				TEST_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit) ? "unhurt" : "can be hurt",
				TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ? "dead" : "alive", unit->object.owner_team_index,
				unit->object.body_vitality, unit->object.shield_vitality);
		}
		/* (an AI character finds its way on the structure's pathfinding
		data, which a multiplayer map may lack) */
		if (global_structure_bsp_get())
		{
			platform_log("forge: the structure has %ld pathfinding surfaces",
				(long)global_structure_bsp_get()->pathfinding_surfaces.count);
		}
		if (held)
		{
			float angles[3];

			forge_angles_from_facing(&forge_globals.held_forward, &forge_globals.held_up, angles);
			platform_log("forge: held %s at %.2f %.2f %.2f facing %.2f %.2f %.2f; held facing %.3f %.3f %.3f up %.3f %.3f %.3f "
				"(yaw %.1f pitch %.1f roll %.1f), rotation snap %d, %s",
				forge_tag_leaf(held->definition_index), held->object.position.x, held->object.position.y, held->object.position.z,
				held->object.forward.i, held->object.forward.j, held->object.forward.k, forge_globals.held_forward.i,
				forge_globals.held_forward.j, forge_globals.held_forward.k, forge_globals.held_up.i, forge_globals.held_up.j,
				forge_globals.held_up.k, RADIANS_TO_DEGREES(angles[0]), RADIANS_TO_DEGREES(angles[1]),
				RADIANS_TO_DEGREES(angles[2]), forge_rotate_snaps[forge_globals.rotate_snap],
				forge_globals.rotating ? "rotating" : "not rotating");
		}
		return TRUE;
	}

	platform_log("forge: unknown command %s", word);
	return FALSE;
}
