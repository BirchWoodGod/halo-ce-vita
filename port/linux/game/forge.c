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
  fast) {or Shift}; holding an object, it turns the right stick {the mouse}
  to the object instead: across, it turns the object about the up axis, up
  and down, it pulls the object nearer or pushes it away {the wheel};
- right trigger [R] {left mouse button}: grab the object under the
  crosshair, and drop the one held (it falls where it is let go, as the
  game's physics have it);
- Y [Triangle] {Delete}: delete the object held, or the one under the
  crosshair;
- X [Square] {E}: the object browser (halo3/forge.xml): the map's objects
  by category, the one chosen spawned in the monitor's hand;
- B [Circle] {F}: a copy of the object held or under the crosshair, held;
- White [D-pad right] {Q}: the movement speed (slow, normal, fast);
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

An object spawned is the game's own: object_placement_data_new and
object_new, as the cheats' object_new spawns a weapon or vehicle
(cheats.c), never deleted on its own (_new_object_never_automatically_delete_bit);
moved with object_set_position, deleted with object_delete. The object
under the crosshair is the nearest whose bounding sphere the camera's line
meets in front of the structure (a weapon on the ground has no collision
model a collision vector would meet). The budget (forge_placed) counts the
objects this session placed that are still there, at most
FORGE_MAXIMUM_OBJECTS, and the characters among them still alive, at most
FORGE_MAXIMUM_AI: past either the browser's choice is refused, said on the
screen. Its table is a fixed array, each index checked.

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
  forge grab | drop        the object under the crosshair grabbed, the held
                           one dropped
  forge turn <degrees>     the held (or aimed at) object turned about up
  forge distance <wu>      the held object that far from the camera
  forge delete             the held (or aimed at) object deleted
  forge camera x y z yaw pitch   the monitor's camera put there (degrees)
  forge press <buttons> [frames]  the monitor's buttons pressed as a
                           controller would (a b x y black white lt rt ls rs,
                           and combo: the monitor and the Spartan's, joined
                           by +), for frames frames (2 by default)
  forge stick lx ly rx ry [frames]  the sticks held (-1 to 1)
  forge where              the monitor's state in the log
  forge budget             the budget in the log
  forge deps <name> [> file]  the tags the first object tag with name in
                           its name needs, every one it references and
                           theirs, with their sizes (the cross-map probe:
                           what another map would have to load to spawn
                           it); with a file, their names written there too
  forge tags <file>        every tag of the loaded map, group and name,
                           written to file
Each logs what it did ("forge: ...").
*/

#include "cseries.h"
#include "real_math.h"
#include "ai/ai_script.h"
#include "ai/actor_definitions.h"
#include "cache/cache_files.h"
#include "camera/observer.h"
#include "devices/device_controls.h"
#include "devices/device_definitions.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/player_control.h"
#include "game/players.h"
#include "input/input.h"
#include "interface/hud_messaging.h"
#include "items/equipment_definitions.h"
#include "items/weapon_definitions.h"
#include "main/main.h"
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
#include "units/units.h"
#include "units/vehicle_definitions.h"

#include "forge.h"
#include "halo_menus.h"
#include "tag_schema.h"
#include "../src/lang.h"

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
	/* the HUD's lines (UTF-8) */
	FORGE_LINE_SIZE = 128,
};

/* (the scenery group's tag: no header of the game's names it) */
#define FORGE_SCENERY_DEFINITION_TAG 'scen'
#define FORGE_FONT_TAG 'font'
#define FORGE_UI_WIDGET_TAG 'DeLa'

/* (world units: one is ten feet, about 3 m) */
#define FORGE_CAMERA_THRUST 4.f
#define FORGE_CAMERA_LOOK_RATE 2.5f
#define FORGE_PICK_RANGE 60.f
#define FORGE_TURN_RATE 2.f
#define FORGE_PUSH_RATE 3.f
#define FORGE_WHEEL_PUSH 0.5f
#define FORGE_MOUSE_TURN 1.f
#define FORGE_MINIMUM_HOLD_DISTANCE 0.5f
#define FORGE_MAXIMUM_HOLD_DISTANCE 40.f
#define FORGE_STICK_DEAD_ZONE 0.15f

/* the in-game set's screens (halo3/forge.xml) */
#define FORGE_BROWSER_SCREEN "h3/forge/browser/screen"
#define FORGE_MENU_SCREEN "h3/forge/pause/screen"

static real const forge_speeds[NUMBER_OF_FORGE_SPEEDS] = { 0.6f, 2.f, 5.f };
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
	N_("VEHICLES"), N_("WEAPONS"), N_("EQUIPMENT"), N_("SCENERY"), N_("CHARACTERS"),
};
/* (the log's) */
static char const *const forge_category_log_names[NUMBER_OF_FORGE_CATEGORIES] =
{
	"vehicles", "weapons", "equipment", "scenery", "characters",
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
	char name[FORGE_NAME_SIZE];
};

/* an object this session placed (the budget's) */
struct forge_placed
{
	long object_index;
	/* the palette tag it was spawned from (an actor variant for a
	character) */
	long tag_index;
	boolean ai;
};

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
	short ai_count;
	/* the buttons down last frame */
	boolean was_down[NUMBER_OF_GAMEPAD_BUTTONS];
	boolean toggle_was_down;
	unsigned long keys_were_down;
	/* (forge press, forge stick) the buttons and sticks the tests hold */
	boolean test_down[NUMBER_OF_GAMEPAD_BUTTONS];
	boolean test_toggle;
	short test_buttons_frames;
	real test_sticks[NUMBER_OF_GAMEPAD_STICKS][2];
	short test_sticks_frames;
	/* asked from the menus (their events may run beside the tick): a
	palette entry to spawn (browser_choice, once browser_chosen), monitor
	and Spartan flipped */
	short browser_choice;
	volatile boolean browser_chosen;
	volatile boolean toggle_requested;
	volatile boolean close_requested;
	volatile boolean quit_requested;
	/* the mouse's turn of the object held (forge_camera_input's, for
	forge_update) */
	real pending_mouse_turn;
	/* what the monitor did since it started, for the log */
	long spawned_count;
	long deleted_count;
	/* the HUD's lines (forge_draw), written by forge_update */
	char hud_title[FORGE_LINE_SIZE];
	char hud_budget[FORGE_LINE_SIZE];
	char hud_label[FORGE_LINE_SIZE];
	boolean hud_label_held;
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
		return _forge_category_weapons;
	case EQUIPMENT_DEFINITION_TAG:
		return strstr(name, "grenade") ? _forge_category_weapons : _forge_category_equipment;
	case FORGE_SCENERY_DEFINITION_TAG:
	case MACHINE_DEFINITION_TAG:
	case CONTROL_DEFINITION_TAG:
		return _forge_category_scenery;
	case ACTOR_VARIANT_DEFINITION_TAG:
		return actor_variant_definition_get(tag_index)->unit_reference.index != NONE ?
			_forge_category_characters : NONE;
	}
	return NONE;
}

static int forge_palette_compare(
	void const *a,
	void const *b)
{
	struct forge_palette_entry const *left = a;
	struct forge_palette_entry const *right = b;

	if (left->category != right->category)
		return left->category - right->category;
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
			entry = &forge_globals.palette[forge_globals.palette_count++];
			entry->tag_index = tag_index;
			entry->category = category;
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

/* ---- the budget */

/* the objects placed that are gone (deleted, destroyed, a new map) let go,
the characters alive counted */
static void forge_placed_update(
	void)
{
	short index;
	short kept = 0;

	forge_globals.ai_count = 0;
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed placed = forge_globals.placed[index];
		struct object_datum *object = object_try_and_get(placed.object_index);

		if (!object)
			continue;
		if (placed.ai && !TEST_FLAG(object->object.damage_flags, _object_dead_bit))
			forge_globals.ai_count++;
		forge_globals.placed[kept++] = placed;
	}
	forge_globals.placed_count = kept;
}

static void forge_placed_add(
	long object_index,
	long tag_index,
	boolean ai)
{
	if (forge_globals.placed_count < 0 || forge_globals.placed_count >= FORGE_MAXIMUM_OBJECTS)
		return;
	forge_globals.placed[forge_globals.placed_count].object_index = object_index;
	forge_globals.placed[forge_globals.placed_count].tag_index = tag_index;
	forge_globals.placed[forge_globals.placed_count].ai = ai;
	forge_globals.placed_count++;
	if (ai)
		forge_globals.ai_count++;
}

static short forge_placed_of_tag(
	long tag_index)
{
	short index;
	short count = 0;

	for (index = 0; index < forge_globals.placed_count; index++)
	{
		if (forge_globals.placed[index].tag_index == tag_index && object_try_and_get(forge_globals.placed[index].object_index))
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
	object_set_position(
		forge_globals.held_object_index,
		&position,
		&forge_globals.held_forward,
		&forge_globals.held_up);
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

		/* (it falls from where it was let go) */
		SET_FLAG(object->object.flags, _object_at_rest_bit, FALSE);
		forge_object_name(forge_globals.held_object_index, name, sizeof(name));
		platform_log("forge: dropped %s (%s) at %.2f %.2f %.2f facing %.2f %.2f %.2f", name,
			forge_tag_leaf(object->definition_index), object->object.position.x, object->object.position.y,
			object->object.position.z, object->object.forward.i, object->object.forward.j, object->object.forward.k);
	}
	forge_globals.held_object_index = NONE;

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
		object_set_position(object_index, &object->object.position, forward, up);
		SET_FLAG(object->object.flags, _object_at_rest_bit, FALSE);
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
	if (object_index == forge_globals.held_object_index)
		forge_globals.held_object_index = NONE;
	object_delete(object_index);
	forge_globals.deleted_count++;
	forge_placed_update();

	return;
}

/* a palette tag spawned in front of the camera (before the structure),
and held, within the budget; the object, or NONE */
static long forge_spawn(
	long tag_index)
{
	struct object_placement_data placement;
	real_point3d position;
	real_vector3d forward;
	long definition_index = tag_index;
	long actor_variant_index = NONE;
	long object_index;
	real distance;
	short entry = forge_palette_of_tag(tag_index);
	char name[FORGE_NAME_SIZE];

	if (!forge_camera_get(&position, &forward))
		return NONE;
	if (entry != NONE)
		snprintf(name, sizeof(name), "%s", forge_globals.palette[entry].name);
	else
		forge_tag_readable_name(tag_index, name, sizeof(name));
	/* (the budget) */
	forge_placed_update();
	if (forge_globals.placed_count >= FORGE_MAXIMUM_OBJECTS)
	{
		forge_notice(T("Budget full: %d objects. Delete one to place another."), FORGE_MAXIMUM_OBJECTS);
		return NONE;
	}
	/* (a character: its actor variant's unit, then a free actor of that
	variant for it) */
	if (tag_get_group_tag(tag_index) == ACTOR_VARIANT_DEFINITION_TAG)
	{
		if (forge_globals.ai_count >= FORGE_MAXIMUM_AI)
		{
			forge_notice(T("AI budget full: %d characters."), FORGE_MAXIMUM_AI);
			return NONE;
		}
		actor_variant_index = tag_index;
		definition_index = actor_variant_definition_get(tag_index)->unit_reference.index;
		if (definition_index == NONE)
		{
			forge_notice(T("%s cannot be placed here"), name);
			return NONE;
		}
	}
	distance = MIN(4.f, forge_structure_distance(&position, &forward) - 1.f);
	distance = MAX(distance, FORGE_MINIMUM_HOLD_DISTANCE);
	object_placement_data_new(&placement, definition_index, NONE);
	SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
	placement.position.x = position.x + forward.i * distance;
	placement.position.y = position.y + forward.j * distance;
	placement.position.z = position.z + forward.k * distance;
	/* (facing the camera, upright) */
	placement.forward.i = -forward.i;
	placement.forward.j = -forward.j;
	placement.forward.k = 0.f;
	if (normalize3d(&placement.forward) < 0.0001f)
		placement.forward = *global_forward3d;
	placement.up = *global_up3d;
	forge_placing_exact = 1;
	object_index = object_new(&placement);
	forge_placing_exact = 0;
	if (object_index == NONE)
	{
		forge_notice(T("%s cannot be placed here"), name);
		return NONE;
	}
	if (actor_variant_index != NONE)
	{
		ai_scripting_attach_free(object_index, actor_variant_index);
		platform_log("forge: %s given a free actor (%lx)", forge_tag_leaf(definition_index),
			(unsigned long)unit_get(object_index)->unit.actor_index);
	}
	forge_placed_add(object_index, tag_index, actor_variant_index != NONE);
	forge_globals.spawned_count++;
	platform_log("forge: spawned %s (%s, %s) at %.2f %.2f %.2f facing %.2f %.2f %.2f; budget %d/%d objects, %d/%d AI",
		name, forge_tag_leaf(tag_index), entry != NONE ? forge_category_log_names[forge_globals.palette[entry].category] : "?",
		placement.position.x, placement.position.y, placement.position.z, placement.forward.i, placement.forward.j,
		placement.forward.k, forge_globals.placed_count, FORGE_MAXIMUM_OBJECTS, forge_globals.ai_count, FORGE_MAXIMUM_AI);
	forge_notice(T("Placed %s"), name);
	forge_globals.held_object_index = object_index;
	forge_globals.held_distance = distance;
	forge_globals.held_forward = placement.forward;
	forge_globals.held_up = placement.up;

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
	short index;

	if (!object)
		return;
	for (index = 0; index < forge_globals.placed_count; index++)
	{
		if (forge_globals.placed[index].object_index == object_index)
			tag_index = forge_globals.placed[index].tag_index;
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
		forge_globals.held_forward = forward;
		forge_globals.held_up = up;
		if (held)
			forge_globals.held_distance = distance;
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

	snprintf(forge_globals.hud_budget, sizeof(forge_globals.hud_budget), T("Objects %d/%d, AI %d/%d"),
		forge_globals.placed_count, FORGE_MAXIMUM_OBJECTS, forge_globals.ai_count, FORGE_MAXIMUM_AI);
	forge_globals.hud_label[0] = 0;
	forge_globals.hud_label_held = object_try_and_get(forge_globals.held_object_index) != NULL;
	object_index = forge_globals.hud_label_held ? forge_globals.held_object_index : forge_object_under_crosshair();
	if (object_index != NONE)
		forge_object_name(object_index, forge_globals.hud_label, sizeof(forge_globals.hud_label));
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
		}
	}
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

void forge_game_start(
	short map_index)
{
	struct game_variant variant;
	static wchar_t const name[] = L"Forge";

	if (map_index < 0 || map_index >= forge_map_count())
		return;
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
}

void forge_map_loaded(
	char const *scenario_name)
{
	/* (every object of the last map is gone, its indices with them) */
	forge_globals.placed_count = 0;
	forge_globals.ai_count = 0;
	forge_globals.held_object_index = NONE;
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
	short *ai)
{
	*objects = forge_globals.placed_count;
	*ai = forge_globals.ai_count;
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
	turning_object = pad.down[_forge_button_alternate] && object_try_and_get(forge_globals.held_object_index);
	speed = forge_speeds[PIN(forge_globals.speed, 0, NUMBER_OF_FORGE_SPEEDS - 1)] * seconds;
	if ((pad.down[_forge_button_alternate] && !turning_object) || (pad.keys & FORGE_KEY_SHIFT))
		speed *= FORGE_CAMERA_THRUST;
	input->translation.i = pad.sticks[_gamepad_stick_left][1] * speed;
	input->translation.j = -pad.sticks[_gamepad_stick_left][0] * speed;
	input->translation.k = ((pad.down[_forge_button_fly_up] ? 1.f : 0.f) - (pad.down[_forge_button_fly_down] ? 1.f : 0.f)) * speed;
	if (!turning_object)
	{
		input->facing_delta.yaw = -pad.sticks[_gamepad_stick_right][0] * FORGE_CAMERA_LOOK_RATE * seconds + pad.look_yaw;
		input->facing_delta.pitch = pad.sticks[_gamepad_stick_right][1] * FORGE_CAMERA_LOOK_RATE * seconds * 0.6f +
			pad.look_pitch;
	}
	else
	{
		/* (the mouse turns the object: its own update takes it) */
		forge_globals.pending_mouse_turn += pad.look_yaw;
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
	/* (the browser closed: its choice made, or B or Square) */
	if (forge_globals.close_requested || (forge_globals.browser_chosen && menu_open))
	{
		forge_globals.close_requested = FALSE;
		if (menu_open)
		{
			ui_widgets_close_all();
			menu_open = FALSE;
		}
	}
	forge_placed_update();
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
		if (pressed[_forge_button_browser])
			forge_browser_open();
		if (pressed[_forge_button_speed])
		{
			forge_globals.speed = (short)((forge_globals.speed + 1) % NUMBER_OF_FORGE_SPEEDS);
			forge_notice("%s", T(forge_speed_names[forge_globals.speed]));
		}
		if (pressed[_forge_button_straighten])
			forge_straighten(forge_target_object());
		if (pressed[_forge_button_duplicate])
			forge_duplicate();
		if (pressed[_forge_button_grab])
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
		if (object_try_and_get(forge_globals.held_object_index))
		{
			if (pad.down[_forge_button_alternate])
			{
				real turn = -pad.sticks[_gamepad_stick_right][0] * FORGE_TURN_RATE * seconds +
					forge_globals.pending_mouse_turn * FORGE_MOUSE_TURN;

				if (turn != 0.f)
					forge_turn(forge_globals.held_object_index, turn);
				forge_globals.held_distance += pad.sticks[_gamepad_stick_right][1] * FORGE_PUSH_RATE * seconds;
			}
			forge_globals.held_distance = PIN(forge_globals.held_distance + pad.wheel * FORGE_WHEEL_PUSH,
				FORGE_MINIMUM_HOLD_DISTANCE, FORGE_MAXIMUM_HOLD_DISTANCE);
		}
	}
	forge_globals.pending_mouse_turn = 0.f;
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

				platform_log("forge: %s %d: %s (%s)", rest, index, entry->name, tag_get_name(entry->tag_index));
			}
		}
		return TRUE;
	}
	if (!strcmp(word, "budget"))
	{
		forge_placed_update();
		platform_log("forge: budget %d/%d objects, %d/%d AI", forge_globals.placed_count, FORGE_MAXIMUM_OBJECTS,
			forge_globals.ai_count, FORGE_MAXIMUM_AI);
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

	if (!forge_globals.active)
	{
		platform_log("forge: %s: not in monitor mode (\"forge on\")", word);
		return FALSE;
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

			platform_log("forge: the player's unit at %.2f %.2f %.2f, %s, %s", unit->object.position.x,
				unit->object.position.y, unit->object.position.z,
				TEST_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit) ? "unhurt" : "can be hurt",
				TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ? "dead" : "alive");
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
			platform_log("forge: held %s at %.2f %.2f %.2f facing %.2f %.2f %.2f", forge_tag_leaf(held->definition_index),
				held->object.position.x, held->object.position.y, held->object.position.z,
				held->object.forward.i, held->object.forward.j, held->object.forward.k);
		}
		return TRUE;
	}

	platform_log("forge: unknown command %s", word);
	return FALSE;
}
