/*
FORGE.C

Forge's sandbox: a SPIKE, to prove the core loop of Forge in a loaded level
before the design (docs/superpowers/specs/2026-10-18-forge-design.md) is
built for real. Nothing here is saved, sent over the network or shown in a
menu; it changes nothing until the console's "forge" command (or
HALO_TEST_COMMANDS) asks for it, and only in a local game.

The monitor: "forge" (or "forge on") turns the first local player into
Forge's monitor. Its unit stays where it stood, hidden and unhurt, its
controls no longer read; the director flies the flying camera (the beta's
debug camera, flying_camera.c) from where the player's eyes were, by the
monitor's controls (forge_camera_input), and "forge off" (or Right stick
click, the Vita's D-pad up) puts the player back, at the camera, as Halo
3's monitor drops its Spartan where it hovers.

The monitor's controls, in the Xbox's terms (the Vita's in brackets, its
default layout: port/vita/README.md):
- left stick: fly along the ground (forward, back, sideways), right
  stick: look;
- A [Cross]: fly up, left stick click [D-pad down]: fly down;
- left trigger [L] held: thrust (four times as fast); holding an object, it
  turns the right stick to the object instead: across, it turns the object
  about the up axis, up and down, it pulls the object nearer or pushes it
  away;
- right trigger [R]: grab the object under the crosshair, and drop the one
  held (it falls where it is let go, as the game's physics have it);
- Y [Triangle]: delete the object held, or the one under the crosshair;
- X [Square]: spawn the palette's current object, held at once;
- Black, White [D-pad left, right]: the palette's previous, next object;
- right stick click [D-pad up]: leave the monitor.

The palette is every tag in the loaded map of the groups the object browser
will have (forge_palette_groups): vehicles, weapons, equipment, scenery,
machines and controls, bipeds, and actor variants - an actor variant spawns
its unit and gives it a free actor of that variant (ai_attach_free), so an
AI character fights as the map's own do. Only the loaded map's: other
maps' objects are the design's subject, not the spike's.

An object spawned is the game's own: object_placement_data_new and
object_new, as the cheats' object_new spawns a weapon or vehicle
(cheats.c), never deleted on its own (_new_object_never_automatically_delete_bit);
moved with object_set_position, deleted with object_delete. The object
under the crosshair is the nearest whose bounding sphere the camera's line
meets in front of the structure (a weapon on the ground has no collision
model a collision vector would meet).

The console's commands (forge_command; the tests use them through
HALO_TEST_COMMANDS):
  forge [on|off]           the monitor, toggled or set
  forge list [group]       the palette (each group's count; a group's tags)
  forge spawn <name>       the first palette tag with name in its name,
                           spawned in front of the camera and held
  forge pick <name>        that tag as the palette's current one
  forge grab | drop        the object under the crosshair grabbed, the held
                           one dropped
  forge turn <degrees>     the held (or aimed at) object turned about up
  forge distance <wu>      the held object that far from the camera
  forge delete             the held (or aimed at) object deleted
  forge camera x y z yaw pitch   the monitor's camera put there (degrees)
  forge press <buttons> [frames]  the monitor's buttons pressed as a
                           controller would (a b x y black white lt rt ls rs,
                           joined by +), for frames frames (2 by default)
  forge stick lx ly rx ry [frames]  the sticks held (-1 to 1)
  forge where              the monitor's state in the log
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
#include "game/players.h"
#include "input/input.h"
#include "interface/hud_messaging.h"
#include "items/equipment_definitions.h"
#include "items/weapon_definitions.h"
#include "objects/objects.h"
#include "objects/object_types.h"
#include "physics/collisions.h"
#include "cache/physical_memory_map.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "units/biped_definitions.h"
#include "units/units.h"
#include "units/vehicle_definitions.h"

#include "forge.h"
#include "tag_schema.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);

/* ---------- constants */

enum
{
	/* the palette's most tags (a campaign map has some 300 of these
	groups' tags) */
	MAXIMUM_FORGE_PALETTE_TAGS = 1024,
	/* the objects the monitor walks the line through for the one under
	the crosshair */
	FORGE_PICK_MASK =
		_object_mask_biped |
		_object_mask_vehicle |
		_object_mask_weapon |
		_object_mask_equipment |
		_object_mask_scenery |
		_object_mask_device,
	/* the deps probe's most tags */
	MAXIMUM_FORGE_DEPENDENCY_TAGS = 4096,
};

/* (the scenery group's tag: no header of the game's names it) */
#define FORGE_SCENERY_DEFINITION_TAG 'scen'

/* (world units: one is ten feet, about 3 m) */
#define FORGE_CAMERA_SPEED 2.f
#define FORGE_CAMERA_THRUST 4.f
#define FORGE_CAMERA_LOOK_RATE 2.5f
#define FORGE_PICK_RANGE 60.f
#define FORGE_TURN_RATE 2.f
#define FORGE_PUSH_RATE 3.f
#define FORGE_MINIMUM_HOLD_DISTANCE 0.5f
#define FORGE_MAXIMUM_HOLD_DISTANCE 40.f
#define FORGE_STICK_DEAD_ZONE 0.15f

/* the monitor's buttons: the game's (input.h) */
enum
{
	_forge_button_fly_up = _gamepad_analog_button_a,
	_forge_button_delete = _gamepad_analog_button_y,
	_forge_button_spawn = _gamepad_analog_button_x,
	_forge_button_previous = _gamepad_analog_button_black,
	_forge_button_next = _gamepad_analog_button_white,
	_forge_button_alternate = _gamepad_analog_button_left_trigger,
	_forge_button_grab = _gamepad_analog_button_right_trigger,
	_forge_button_fly_down = _gamepad_binary_button_left_thumb,
	_forge_button_leave = _gamepad_binary_button_right_thumb,
};

/* the groups of the palette, in the object browser's order */
static struct
{
	unsigned long group_tag;
	char const *name;
} const forge_palette_groups[] =
{
	{ VEHICLE_DEFINITION_TAG, "vehicles" },
	{ WEAPON_DEFINITION_TAG, "weapons" },
	{ EQUIPMENT_DEFINITION_TAG, "equipment" },
	{ ACTOR_VARIANT_DEFINITION_TAG, "characters" },
	{ BIPED_DEFINITION_TAG, "bipeds" },
	{ FORGE_SCENERY_DEFINITION_TAG, "scenery" },
	{ MACHINE_DEFINITION_TAG, "machines" },
	{ CONTROL_DEFINITION_TAG, "controls" },
};

/* ---------- structures */

struct forge_pad
{
	boolean down[NUMBER_OF_GAMEPAD_BUTTONS];
	/* -1 to 1, the dead zone taken off */
	real sticks[NUMBER_OF_GAMEPAD_STICKS][2];
};

/* ---------- globals */

static struct
{
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
	/* the palette */
	long palette[MAXIMUM_FORGE_PALETTE_TAGS];
	short palette_count;
	short palette_index;
	/* the buttons down last frame */
	boolean was_down[NUMBER_OF_GAMEPAD_BUTTONS];
	/* (forge press, forge stick) the buttons and sticks the tests hold */
	boolean test_down[NUMBER_OF_GAMEPAD_BUTTONS];
	short test_buttons_frames;
	real test_sticks[NUMBER_OF_GAMEPAD_STICKS][2];
	short test_sticks_frames;
	/* what the monitor did since it started, for the log */
	long spawned_count;
	long deleted_count;
} forge_globals = { FALSE, 0, NONE, FALSE, NONE };

/* ---------- private code */

static char const *forge_tag_short_name(
	long tag_index)
{
	char const *name = tag_get_name(tag_index);
	char const *slash = name ? strrchr(name, '\\') : NULL;

	return slash ? slash + 1 : (name ? name : "?");
}

static void forge_hud_message(
	char const *format,
	...)
{
	char text[96];
	wchar_t message[96];
	va_list arguments;
	short index;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	for (index = 0; text[index] && index < NUMBEROF(message) - 1; index++)
		message[index] = (wchar_t)(unsigned char)text[index];
	message[index] = 0;
	hud_print_message(forge_globals.local_player_index, message);
	platform_log("forge: %s", text);
}

static short forge_palette_group_index(
	unsigned long group_tag)
{
	short index;

	for (index = 0; index < (short)NUMBEROF(forge_palette_groups); index++)
	{
		if (forge_palette_groups[index].group_tag == group_tag)
			return index;
	}
	return NONE;
}

/* the palette: every tag of its groups in the loaded map, group by group */
static void forge_palette_build(
	void)
{
	short group_index;

	forge_globals.palette_count = 0;
	for (group_index = 0; group_index < (short)NUMBEROF(forge_palette_groups); group_index++)
	{
		struct tag_iterator iterator;
		long tag_index;

		tag_iterator_new(&iterator, forge_palette_groups[group_index].group_tag);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE &&
			forge_globals.palette_count < MAXIMUM_FORGE_PALETTE_TAGS)
		{
			/* (an iterator of a group walks its children's tags too:
			only the group's own) */
			if (tag_get_group_tag(tag_index) == forge_palette_groups[group_index].group_tag)
				forge_globals.palette[forge_globals.palette_count++] = tag_index;
		}
	}
	if (forge_globals.palette_index >= forge_globals.palette_count)
		forge_globals.palette_index = 0;

	return;
}

/* the palette tag named name (its last part), else the first with name in
its name, or NONE */
static short forge_palette_find(
	char const *name)
{
	short index;

	for (index = 0; index < forge_globals.palette_count; index++)
	{
		if (!strcmp(forge_tag_short_name(forge_globals.palette[index]), name))
			return index;
	}
	for (index = 0; index < forge_globals.palette_count; index++)
	{
		char const *tag_name = tag_get_name(forge_globals.palette[index]);

		if (tag_name && strstr(tag_name, name))
			return index;
	}
	return NONE;
}

static char const *forge_palette_group_name(
	long tag_index)
{
	short group_index = forge_palette_group_index(tag_get_group_tag(tag_index));

	return group_index != NONE ? forge_palette_groups[group_index].name : "?";
}

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

			for (button = 0; button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
				pad->down[button] = gamepad->buttons[button] != 0;
			for (stick = 0; stick < NUMBER_OF_GAMEPAD_STICKS; stick++)
			{
				pad->sticks[stick][0] = (real)gamepad->sticks[stick].x / 32767.f;
				pad->sticks[stick][1] = (real)gamepad->sticks[stick].y / 32767.f;
			}
		}
	}
	/* (forge press, forge stick) */
	if (forge_globals.test_buttons_frames > 0)
	{
		for (button = 0; button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
			pad->down[button] |= forge_globals.test_down[button];
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
	return forge_globals.unit_index != NONE &&
		object_get_ultimate_parent(object_index) == forge_globals.unit_index;
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

/* the object under the crosshair: the nearest whose bounding sphere the
camera's line meets before the structure, not the player's, nothing
attached to another; NONE if none */
static long forge_object_under_crosshair(
	void)
{
	real_point3d position;
	real_vector3d forward;
	real structure_distance;
	real nearest = REAL_MAX;
	long nearest_index = NONE;
	struct object_iterator iterator;
	struct object_datum *object;

	if (!forge_camera_get(&position, &forward))
		return NONE;
	structure_distance = forge_structure_distance(&position, &forward);
	object_iterator_new(&iterator, FORGE_PICK_MASK, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		real_vector3d to_center;
		real along;
		real across_squared;
		real radius = MAX(object->object.bounding_sphere_radius, 0.2f);

		if (object->object.parent_object_index != NONE || forge_object_is_players(iterator.index))
			continue;
		to_center.i = object->object.bounding_sphere_center.x - position.x;
		to_center.j = object->object.bounding_sphere_center.y - position.y;
		to_center.k = object->object.bounding_sphere_center.z - position.z;
		along = dot_product3d(&to_center, &forward);
		if (along < 0.f || along - radius > structure_distance)
			continue;
		across_squared = dot_product3d(&to_center, &to_center) - along * along;
		if (across_squared > radius * radius)
			continue;
		if (along < nearest)
		{
			nearest = along;
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
	forge_hud_message("Grabbed %s", forge_tag_short_name(object->definition_index));

	return;
}

static void forge_drop(
	void)
{
	struct object_datum *object = object_try_and_get(forge_globals.held_object_index);

	if (object)
	{
		/* (it falls from where it was let go) */
		SET_FLAG(object->object.flags, _object_at_rest_bit, FALSE);
		forge_hud_message("Dropped %s at %.2f %.2f %.2f", forge_tag_short_name(object->definition_index),
			object->object.position.x, object->object.position.y, object->object.position.z);
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
	if (object_index == forge_globals.held_object_index)
	{
		forward = forge_globals.held_forward;
		up = forge_globals.held_up;
	}
	else
	{
		forward = object->object.forward;
		up = object->object.up;
	}
	rotate_vector_about_axis(&forward, &axis, sine(angle), cosine(angle));
	rotate_vector_about_axis(&up, &axis, sine(angle), cosine(angle));
	if (object_index == forge_globals.held_object_index)
	{
		forge_globals.held_forward = forward;
		forge_globals.held_up = up;
	}
	else
	{
		object_set_position(object_index, &object->object.position, &forward, &up);
	}

	return;
}

static void forge_delete(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);

	if (!object || forge_object_is_players(object_index))
		return;
	forge_hud_message("Deleted %s", forge_tag_short_name(object->definition_index));
	if (object_index == forge_globals.held_object_index)
		forge_globals.held_object_index = NONE;
	object_delete(object_index);
	forge_globals.deleted_count++;

	return;
}

/* a palette tag spawned in front of the camera (before the structure),
and held; the object, or NONE */
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

	if (!forge_camera_get(&position, &forward))
		return NONE;
	/* (a character: its actor variant's unit, then a free actor of that
	variant for it) */
	if (tag_get_group_tag(tag_index) == ACTOR_VARIANT_DEFINITION_TAG)
	{
		actor_variant_index = tag_index;
		definition_index = actor_variant_definition_get(tag_index)->unit_reference.index;
		/* (a vehicle's gunner or pilot names no unit: the map's first biped
		carries it, as a multiplayer map's cyborg) */
		if (definition_index == NONE)
		{
			struct tag_iterator iterator;

			tag_iterator_new(&iterator, BIPED_DEFINITION_TAG);
			definition_index = tag_iterator_next(&iterator);
		}
		if (definition_index == NONE)
		{
			forge_hud_message("%s has no unit", forge_tag_short_name(tag_index));
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
	object_index = object_new(&placement);
	if (object_index == NONE)
	{
		forge_hud_message("%s could not be spawned", forge_tag_short_name(tag_index));
		return NONE;
	}
	if (actor_variant_index != NONE)
	{
		ai_scripting_attach_free(object_index, actor_variant_index);
		platform_log("forge: %s given a free actor (%lx)", forge_tag_short_name(definition_index),
			(unsigned long)unit_get(object_index)->unit.actor_index);
	}
	forge_globals.spawned_count++;
	forge_hud_message("Spawned %s (%s)", forge_tag_short_name(tag_index), forge_palette_group_name(tag_index));
	forge_globals.held_object_index = object_index;
	forge_globals.held_distance = distance;
	forge_globals.held_forward = placement.forward;
	forge_globals.held_up = placement.up;

	return object_index;
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

static boolean forge_begin(
	void)
{
	long player_index;
	struct object_datum *unit;
	real_point3d position;
	real_vector3d forward;

	if (forge_globals.active)
		return TRUE;
	if (game_connection() != _game_connection_local)
	{
		platform_log("forge: only in a local game (the sandbox spike)");
		return FALSE;
	}
	forge_globals.local_player_index = 0;
	player_index = local_player_get_player_index(forge_globals.local_player_index);
	if (player_index == NONE || !forge_camera_get(&position, &forward))
	{
		platform_log("forge: no local player");
		return FALSE;
	}
	forge_globals.unit_index = player_get(player_index)->unit_index;
	unit = object_try_and_get(forge_globals.unit_index);
	if (unit)
	{
		forge_globals.unit_could_take_damage =
			!TEST_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit);
		SET_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit, TRUE);
		forge_unit_set_visible(forge_globals.unit_index, FALSE);
	}
	forge_palette_build();
	forge_globals.held_object_index = NONE;
	csmemset(forge_globals.was_down, 0, sizeof(forge_globals.was_down));
	director_forge_camera(forge_globals.local_player_index, TRUE, &position, &forward);
	forge_globals.active = TRUE;
	forge_hud_message("Monitor mode: %d objects in the palette", forge_globals.palette_count);

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
			object_set_position(forge_globals.unit_index, &position, NULL, NULL);
	}
	director_forge_camera(forge_globals.local_player_index, FALSE, NULL, NULL);
	forge_globals.active = FALSE;
	forge_globals.unit_index = NONE;
	forge_hud_message("Player mode (%ld spawned, %ld deleted)", forge_globals.spawned_count,
		forge_globals.deleted_count);

	return;
}

/* the monitor's state is gone with the map, or when the player's unit is */
static boolean forge_still_valid(
	void)
{
	long player_index = local_player_get_player_index(forge_globals.local_player_index);

	if (player_index == NONE || game_connection() != _game_connection_local)
		return FALSE;
	if (forge_globals.unit_index != NONE && player_get(player_index)->unit_index != forge_globals.unit_index)
		return FALSE;
	return TRUE;
}

static void forge_palette_step(
	short step)
{
	long tag_index;

	if (forge_globals.palette_count <= 0)
		return;
	forge_globals.palette_index =
		(short)((forge_globals.palette_index + step + forge_globals.palette_count) % forge_globals.palette_count);
	tag_index = forge_globals.palette[forge_globals.palette_index];
	forge_hud_message("%s: %s (%d of %d)", forge_palette_group_name(tag_index), forge_tag_short_name(tag_index),
		forge_globals.palette_index + 1, forge_globals.palette_count);

	return;
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
	forge_pad_get(&pad);
	csmemset(input, 0, sizeof(*input));
	turning_object = pad.down[_forge_button_alternate] && object_try_and_get(forge_globals.held_object_index);
	speed = FORGE_CAMERA_SPEED * seconds;
	if (pad.down[_forge_button_alternate] && !turning_object)
		speed *= FORGE_CAMERA_THRUST;
	input->translation.i = pad.sticks[_gamepad_stick_left][1] * speed;
	input->translation.j = -pad.sticks[_gamepad_stick_left][0] * speed;
	input->translation.k = ((pad.down[_forge_button_fly_up] ? 1.f : 0.f) - (pad.down[_forge_button_fly_down] ? 1.f : 0.f)) * speed;
	if (!turning_object)
	{
		input->facing_delta.yaw = -pad.sticks[_gamepad_stick_right][0] * FORGE_CAMERA_LOOK_RATE * seconds;
		input->facing_delta.pitch = pad.sticks[_gamepad_stick_right][1] * FORGE_CAMERA_LOOK_RATE * seconds * 0.6f;
	}

	return TRUE;
}

void forge_update(
	void)
{
	struct forge_pad pad;
	boolean pressed[NUMBER_OF_GAMEPAD_BUTTONS];
	short button;
	real seconds = 1.f / 30.f;

	if (forge_globals.test_buttons_frames > 0)
		forge_globals.test_buttons_frames--;
	if (forge_globals.test_sticks_frames > 0)
		forge_globals.test_sticks_frames--;
	if (!forge_globals.active)
		return;
	if (!forge_still_valid())
	{
		/* (a new map, a revert or a death: the monitor's state is gone; the
		player's own camera is the director's again) */
		forge_globals.active = FALSE;
		forge_globals.held_object_index = NONE;
		forge_globals.unit_index = NONE;
		if (local_player_get_player_index(forge_globals.local_player_index) != NONE)
			director_forge_camera(forge_globals.local_player_index, FALSE, NULL, NULL);
		platform_log("forge: the monitor is gone (a new map, or the player's unit)");
		return;
	}

	forge_pad_get(&pad);
	for (button = 0; button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
	{
		pressed[button] = pad.down[button] && !forge_globals.was_down[button];
		forge_globals.was_down[button] = pad.down[button];
	}

	if (pressed[_forge_button_leave])
	{
		forge_end();
		return;
	}
	if (pressed[_forge_button_previous])
		forge_palette_step(-1);
	if (pressed[_forge_button_next])
		forge_palette_step(1);
	if (pressed[_forge_button_spawn] && forge_globals.palette_count > 0)
	{
		if (object_try_and_get(forge_globals.held_object_index))
			forge_drop();
		forge_spawn(forge_globals.palette[forge_globals.palette_index]);
	}
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
			if (pad.sticks[_gamepad_stick_right][0] != 0.f)
				forge_turn(forge_globals.held_object_index, -pad.sticks[_gamepad_stick_right][0] * FORGE_TURN_RATE * seconds);
			forge_globals.held_distance = PIN(
				forge_globals.held_distance + pad.sticks[_gamepad_stick_right][1] * FORGE_PUSH_RATE * seconds,
				FORGE_MINIMUM_HOLD_DISTANCE,
				FORGE_MAXIMUM_HOLD_DISTANCE);
		}
		forge_held_object_place();
	}

	return;
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

			if (tag_name && strstr(tag_name, name) &&
				forge_palette_group_index(tag_get_group_tag(tag_index)) != NONE)
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
		short group_index;

		forge_palette_build();
		for (group_index = 0; group_index < (short)NUMBEROF(forge_palette_groups); group_index++)
		{
			short count = 0;

			for (index = 0; index < forge_globals.palette_count; index++)
			{
				if (tag_get_group_tag(forge_globals.palette[index]) != forge_palette_groups[group_index].group_tag)
					continue;
				count++;
				if (!strcmp(rest, forge_palette_groups[group_index].name))
					platform_log("forge: %s %d: %s", rest, index, tag_get_name(forge_globals.palette[index]));
			}
			platform_log("forge: palette %s: %d", forge_palette_groups[group_index].name, count);
		}
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
			for (name = strtok_r(names, "+", &end); name; name = strtok_r(NULL, "+", &end))
			{
				short button = forge_button_from_name(name);

				if (button != NONE)
					forge_globals.test_down[button] = TRUE;
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

		forge_palette_build();
		index = forge_palette_find(rest);
		if (index == NONE)
		{
			platform_log("forge: no palette tag named %s", rest);
			return FALSE;
		}
		forge_globals.palette_index = index;
		if (!strcmp(word, "pick"))
		{
			forge_palette_step(0);
			return TRUE;
		}
		if (object_try_and_get(forge_globals.held_object_index))
			forge_drop();
		return forge_spawn(forge_globals.palette[index]) != NONE;
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
		platform_log("forge: turned %s by %s degrees", forge_tag_short_name(object_get(object_index)->definition_index), rest);
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

		forge_camera_get(&position, &forward);
		platform_log("forge: camera at %.2f %.2f %.2f looking %.2f %.2f %.2f; holding %s at %.2f; aiming at %s; "
			"palette %d of %d; %ld spawned, %ld deleted",
			position.x, position.y, position.z, forward.i, forward.j, forward.k,
			held ? forge_tag_short_name(held->definition_index) : "nothing", forge_globals.held_distance,
			aimed != NONE ? forge_tag_short_name(object_get(aimed)->definition_index) : "nothing",
			forge_globals.palette_index + 1, forge_globals.palette_count,
			forge_globals.spawned_count, forge_globals.deleted_count);
		/* (an AI character finds its way on the structure's pathfinding
		data, which a multiplayer map may lack) */
		if (global_structure_bsp_get())
		{
			platform_log("forge: the structure has %ld pathfinding surfaces",
				(long)global_structure_bsp_get()->pathfinding_surfaces.count);
		}
		if (held)
		{
			platform_log("forge: held %s at %.2f %.2f %.2f facing %.2f %.2f %.2f", forge_tag_short_name(held->definition_index),
				held->object.position.x, held->object.position.y, held->object.position.z,
				held->object.forward.i, held->object.forward.j, held->object.forward.k);
		}
		return TRUE;
	}

	platform_log("forge: unknown command %s", word);
	return FALSE;
}
