/*
MENU_SCENE_OBJECTS.C

The characters standing in the Halo 3 menus' scenes, as Halo 3's lobbies
have their Spartans: a screen of the Halo 3 set with a scene= (its camera,
menu_functions.c's h3 scenes) may say who stands in front of that camera,

	scene_object="<scene pack> <distance> <right> <up> <facing> [; ...]"

each a scene pack's first tag (a biped: scene_packs.c builds and loads the
pack from the player's multiplayer maps, app0:packs/<pack>.txt), holding the
pack's second if it is a weapon (or its own first weapon), placed in the
camera's frame - distance along its view, right of it, up from there (world
units) - and turned facing degrees from facing the camera, in the player's
multiplayer colour (the first local player's active profile's). The pickers
opened from a lobby keep its characters; the main menu (scene="ring") and
leaving the main menu let them go.

It does nothing at all, and nothing breaks, when it cannot: Menu style Halo
CE, HALO_MENU_SCENES=0 (no scene is asked for), HALO_SCENE_PACKS=0, no
multiplayer map to build the pack from, a build or load that fails, a pack
whose first tag is not a biped: there is simply no character.

The Customization screen's preview (menu_scene_spartan_show, _color,
_hide: halo_menus.h) is one more character, the menu_spartan pack's, put in
the scene open now (or at the camera it names) and recoloured live.

Threads: the screens' events (menu_functions.c, which may run beside the
tick) and the preview's calls only say what is wanted;
menu_scene_objects_update (main.c, as forge_update: after the cameras,
before the tick starts) makes, holds and deletes the objects. Objects are
made with object_new as the cheats' and Forge's are, never deleted on their
own (_new_object_never_automatically_delete_bit), and held where they stand
each frame (ui.map has no ground under the cameras: they would fall).

How they look, as Halo 3's lobby Spartans stand: upright in the picture
(placed and stood in the camera's own frame, its pitch with it: a camera
pitched up no longer looks at them from their feet), their weapon held low
in front and their head turned to the camera (the idle's aim and look held
each frame: no player or AI aims it), and lit by a light of their own
(menu_scene_objects_lighting, asked by object_lights.c: ui.map's space has
no lightmap under them, so they took its structure's dim default) - a key
light from the camera's upper left, a cool rim from behind, a soft ambient.
*/

/* ---------- headers */

#include "cseries.h"
#include "real_math.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "units/units.h"
#include "units/biped_definitions.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "render/render.h"

#include "halo_menus.h"
#include "scene_packs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);
int halo_main_menu_idle(void);
void placement_data_set_change_color(struct object_placement_data *placement_data, real_rgb_color const *change_color);
unsigned long scene_packs_map_epoch(void);

/* ---------- constants */

enum
{
	MAXIMUM_MENU_SCENE_OBJECTS = 4,
	/* the preview's character: after the screen's */
	MENU_SCENE_PREVIEW = MAXIMUM_MENU_SCENE_OBJECTS,
	NUMBER_OF_MENU_SCENE_SLOTS,
	/* Halo CE's multiplayer colours (player_profile.c) */
	NUMBER_OF_MENU_SCENE_COLORS = 18,
};

#define MENU_SCENE_PREVIEW_PACK "menu_spartan"
#define MENU_SCENE_DEGREES ((real)(3.14159265358979 / 180.0))
/* the weapon held this far below the body's forward, and the head turned
to the camera (a calm stance rather than the idle's aim straight ahead) */
#define MENU_SCENE_AIM_DOWN (30.f * MENU_SCENE_DEGREES)

/* ---------- structures */

struct menu_scene_camera
{
	real_point3d position;
	real yaw;
	real pitch;
};

struct menu_scene_object
{
	boolean wanted;
	char pack[32];
	real distance;
	real right;
	real up;
	real facing;
	/* NONE: the profile's */
	short color_index;
	struct menu_scene_camera camera;

	/* (the update's) the unit made, the map it was made on, its colour */
	long unit_index;
	unsigned long epoch;
	short made_color_index;
	boolean refused;
	/* (the update's) its light, from its camera (object_lights.c reads it) */
	struct render_lighting lighting;
};

/* a camera's frame: its view, its right (level) and its up (pitched with
the view) */
struct menu_scene_frame
{
	real_vector3d view;
	real_vector3d right;
	real_vector3d up;
};

/* ---------- globals */

static struct
{
	struct menu_scene_object slots[NUMBER_OF_MENU_SCENE_SLOTS];
	/* the scene open now's camera (menu_scene_objects_want), for the
	preview's default */
	boolean has_camera;
	struct menu_scene_camera camera;
	char screen[64];
	/* objects made now (object_lights.c's quick way out) */
	short made_count;
} menu_scene_objects;

static boolean menu_scene_objects_initialized = FALSE;

/* ---------- private code */

static void initialize(void)
{
	short index;

	if (menu_scene_objects_initialized)
		return;
	for (index = 0; index < NUMBER_OF_MENU_SCENE_SLOTS; index++)
	{
		menu_scene_objects.slots[index].unit_index = NONE;
		menu_scene_objects.slots[index].color_index = NONE;
	}
	menu_scene_objects_initialized = TRUE;
}

/* "<distance> <right> <up> <facing>" in range */
static boolean placement_parse(char const *text, struct menu_scene_object *object)
{
	float values[4];
	short index;

	if (sscanf(text, "%f %f %f %f", &values[0], &values[1], &values[2], &values[3]) != 4)
		return FALSE;
	for (index = 0; index < 4; index++)
	{
		if (!(values[index] > -1000.f && values[index] < 1000.f))
			return FALSE;
	}
	object->distance = values[0];
	object->right = values[1];
	object->up = values[2];
	object->facing = values[3];
	return TRUE;
}

/* the multiplayer colour index the object is drawn in */
static short color_index_of(struct menu_scene_object const *object)
{
	long profile_index;
	struct player_profile profile;

	if (object->color_index >= 0 && object->color_index < NUMBER_OF_MENU_SCENE_COLORS)
		return object->color_index;
	profile_index = player_ui_get_active_player_profile_index(0);
	if (profile_index != NONE && player_profile_get(profile_index, &profile) && profile.primary_color_index >= 0 &&
		profile.primary_color_index < NUMBER_OF_MENU_SCENE_COLORS)
	{
		return profile.primary_color_index;
	}
	/* (no profile yet: Halo's own default, white) */
	return 0;
}

static void camera_frame(struct menu_scene_camera const *camera, struct menu_scene_frame *frame)
{
	real yaw = camera->yaw, pitch = camera->pitch;

	frame->view.i = (real)(cos(pitch) * cos(yaw));
	frame->view.j = (real)(cos(pitch) * sin(yaw));
	frame->view.k = (real)sin(pitch);
	frame->right.i = (real)sin(yaw);
	frame->right.j = (real)-cos(yaw);
	frame->right.k = 0.f;
	/* (right x view: up in the picture) */
	frame->up.i = (real)(-sin(pitch) * cos(yaw));
	frame->up.j = (real)(-sin(pitch) * sin(yaw));
	frame->up.k = (real)cos(pitch);
}

/* where the object stands, in its camera's frame - distance along the
view, right of it, up in the picture -, and its facing: toward the camera,
turned by facing about the picture's up, so that it stands upright in the
picture whatever the camera's pitch (a flying biped keeps its forward's
pitch; its up follows it: biped_snap_facing) */
static void object_place(struct menu_scene_object const *object, real_point3d *position, real_vector3d *forward,
	real_vector3d *up)
{
	struct menu_scene_frame frame;
	real facing = object->facing * MENU_SCENE_DEGREES;
	real cosine = (real)cos(facing), sine = (real)sin(facing);

	camera_frame(&object->camera, &frame);
	position->x = object->camera.position.x + frame.view.i * object->distance + frame.right.i * object->right +
		frame.up.i * object->up;
	position->y = object->camera.position.y + frame.view.j * object->distance + frame.right.j * object->right +
		frame.up.j * object->up;
	position->z = object->camera.position.z + frame.view.k * object->distance + frame.right.k * object->right +
		frame.up.k * object->up;
	/* (back toward the camera, turned: positive facing turns its front to
	the picture's left, as before) */
	forward->i = -frame.view.i * cosine + frame.right.i * sine;
	forward->j = -frame.view.j * cosine + frame.right.j * sine;
	forward->k = -frame.view.k * cosine + frame.right.k * sine;
	*up = frame.up;
}

/* the unit's aim and look held: the weapon low in front of it, the head
to the camera */
static void object_pose(struct menu_scene_object const *object, struct unit_datum *unit, real_vector3d const *forward,
	real_vector3d const *up)
{
	real_vector3d aim, look;
	real cosine = (real)cos(MENU_SCENE_AIM_DOWN), sine = (real)sin(MENU_SCENE_AIM_DOWN);
	struct menu_scene_frame frame;

	aim.i = forward->i * cosine - up->i * sine;
	aim.j = forward->j * cosine - up->j * sine;
	aim.k = forward->k * cosine - up->k * sine;
	camera_frame(&object->camera, &frame);
	look.i = -frame.view.i;
	look.j = -frame.view.j;
	look.k = -frame.view.k;
	unit->unit.desired_facing_vector = *forward;
	unit->unit.desired_aiming_vector = aim;
	unit->unit.aiming_vector = aim;
	unit->unit.desired_looking_vector = look;
	unit->unit.looking_vector = look;
	unit->unit.aiming_velocity.i = unit->unit.aiming_velocity.j = unit->unit.aiming_velocity.k = 0.f;
	unit->unit.looking_velocity.i = unit->unit.looking_velocity.j = unit->unit.looking_velocity.k = 0.f;
}

static void light_set(struct render_distant_light *light, real red, real green, real blue,
	struct menu_scene_frame const *frame, real view, real right, real up)
{
	light->color.red = red;
	light->color.green = green;
	light->color.blue = blue;
	/* (the way the light goes, in the camera's frame) */
	light->direction.i = frame->view.i * view + frame->right.i * right + frame->up.i * up;
	light->direction.j = frame->view.j * view + frame->right.j * right + frame->up.j * up;
	light->direction.k = frame->view.k * view + frame->right.k * right + frame->up.k * up;
	normalize3d(&light->direction);
}

/* the object's own light: a warm key from the camera's upper left, a cool
rim from behind on the right, a soft ambient, no shadow (there is nothing
under it to fall on) */
static void object_light(struct menu_scene_object *object)
{
	struct render_lighting *lighting = &object->lighting;
	struct menu_scene_frame frame;

	camera_frame(&object->camera, &frame);
	memset(lighting, 0, sizeof(*lighting));
	lighting->ambient_color.red = 0.36f;
	lighting->ambient_color.green = 0.38f;
	lighting->ambient_color.blue = 0.44f;
	lighting->distant_light_count = 2;
	light_set(&lighting->distant_lights[0], 1.25f, 1.20f, 1.12f, &frame, 0.55f, 0.45f, -0.70f);
	light_set(&lighting->distant_lights[1], 0.50f, 0.62f, 0.90f, &frame, -0.70f, -0.45f, -0.25f);
	lighting->point_light_count = 0;
	lighting->reflection_tint_color.alpha = 0.5f;
	lighting->reflection_tint_color.red = 1.f;
	lighting->reflection_tint_color.green = 1.f;
	lighting->reflection_tint_color.blue = 1.f;
	lighting->shadow_vector.i = -frame.up.i;
	lighting->shadow_vector.j = -frame.up.j;
	lighting->shadow_vector.k = -frame.up.k;
	lighting->shadow_color.red = lighting->shadow_color.green = lighting->shadow_color.blue = 1.f;
}

static void object_forget(struct menu_scene_object *object, boolean delete_it)
{
	if (object->unit_index != NONE && delete_it && object->epoch == scene_packs_map_epoch() &&
		object_try_and_get(object->unit_index))
	{
		struct unit_datum *unit = unit_try_and_get(object->unit_index);
		short weapon;

		/* (its weapons with it) */
		if (unit)
		{
			for (weapon = 0; weapon < MAXIMUM_WEAPONS_PER_UNIT; weapon++)
			{
				long weapon_index = unit->unit.weapon_object_indices[weapon];

				if (weapon_index != NONE && object_try_and_get(weapon_index))
					object_delete(weapon_index);
			}
		}
		object_delete(object->unit_index);
	}
	object->unit_index = NONE;
	object->refused = FALSE;
}

/* the object made, if its pack is loaded (it is wanted, and built, until
then) */
static void object_make(struct menu_scene_object *object, short slot)
{
	short state = scene_packs_want(object->pack);
	long roots[2] = { NONE, NONE };
	short root_count;
	struct object_placement_data placement;
	real_rgb_color color;
	real_vector3d up;
	unsigned long long started;
	struct unit_datum *unit;

	if (state != _scene_pack_state_loaded)
	{
		if (state == _scene_pack_state_failed || state == _scene_pack_state_none)
		{
			object->refused = TRUE;
			platform_log("menus: h3 scene object %d: the pack %s is %s: no character", slot, object->pack,
				scene_packs_state_name(state));
		}
		return;
	}
	root_count = scene_packs_roots(object->pack, roots, 2);
	if (!root_count || roots[0] == NONE || tag_get_group_tag(roots[0]) != 'bipd')
	{
		object->refused = TRUE;
		platform_log("menus: h3 scene object %d: the pack %s's first tag is not a biped: no character", slot,
			object->pack);
		return;
	}
	started = vita_host_time_us();
	/* (ui.map has no ground under its cameras: the pack's copy of the biped,
	which this map alone has, made one that flies, which stands where it is
	in its idle - state idle, seat stand: no airborne pose - rather than
	falling, and keeps the pitch it is given, so it stands upright in a
	pitched camera's picture) */
	{
		struct biped_definition *definition = biped_definition_get(roots[0]);

		SET_FLAG(definition->biped.flags, _biped_flying_bit, TRUE);
	}
	object_placement_data_new(&placement, roots[0], NONE);
	SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
	object_place(object, &placement.position, &placement.forward, &up);
	placement.up = up;
	object->made_color_index = color_index_of(object);
	color = player_profile_get_rgb_color(object->made_color_index);
	placement_data_set_change_color(&placement, &color);
	object->unit_index = object_new(&placement);
	object->epoch = scene_packs_map_epoch();
	if (object->unit_index == NONE || !(unit = unit_try_and_get(object->unit_index)))
	{
		object->unit_index = NONE;
		object->refused = TRUE;
		platform_log("menus: h3 scene object %d: %s could not be made", slot, tag_get_name(roots[0]));
		return;
	}
	/* (kept awake: an object sleeps outside the players' clusters, and
ui.map has no player, so it would stand still, its weapon never readied) */
	object_set_automatic_deactivation(object->unit_index, FALSE);
	object_activate(object->unit_index);
	object_light(object);
	object_pose(object, unit, &placement.forward, &up);
	/* (the pack's weapon in its hands, if its own initial weapons gave it
	none) */
	if (root_count > 1 && roots[1] != NONE && tag_get_group_tag(roots[1]) == 'weap' &&
		unit->unit.weapon_object_indices[0] == NONE)
	{
		struct object_placement_data weapon_placement;
		long weapon_index;

		object_placement_data_new(&weapon_placement, roots[1], object->unit_index);
		weapon_placement.position = placement.position;
		weapon_index = object_new(&weapon_placement);
		if (weapon_index != NONE && !unit_add_weapon_to_inventory(object->unit_index, weapon_index, FALSE))
		{
			object_delete(weapon_index);
			weapon_index = NONE;
		}
		platform_log("menus: h3 scene object %d: %s %s", slot, tag_get_name(roots[1]),
			weapon_index != NONE ? "in its hands" : "could not be given");
	}
	platform_log("menus: h3 scene object %d: %s (%s) at %.2f %.2f %.2f facing %.0f, colour %d: made in %.1f ms", slot,
		tag_get_name(roots[0]), object->pack, placement.position.x, placement.position.y, placement.position.z,
		object->facing, object->made_color_index, (double)(vita_host_time_us() - started) / 1000.0);
}

/* the object held where it stands (no ground under it), made again if
its colour changed */
static void object_hold(struct menu_scene_object *object)
{
	struct object_datum *datum = object_try_and_get(object->unit_index);
	struct unit_datum *unit = unit_try_and_get(object->unit_index);
	real_point3d position;
	real_vector3d forward, up;
	short color_index = color_index_of(object);

	if (!datum || !unit)
	{
		object->unit_index = NONE;
		return;
	}
	object_place(object, &position, &forward, &up);
	object_set_position(object->unit_index, &position, &forward, &up);
	object_pose(object, unit, &forward, &up);
	datum->object.translational_velocity.i = datum->object.translational_velocity.j =
		datum->object.translational_velocity.k = 0.f;
	/* (another colour: made again in it, by the update right after this;
	an object's colours are worked out as it is made) */
	if (color_index != object->made_color_index)
	{
		object_forget(object, TRUE);
		platform_log("menus: h3 scene object: recoloured %d", color_index);
	}
}

/* "<point> [x y z yaw pitch]": the camera it names (its point's own place
when it gives none) */
static boolean camera_parse(char const *text, struct menu_scene_camera *camera)
{
	char name[32];
	float values[5];
	int used = 0;
	struct scenario *scenario = global_scenario_get();
	short index;

	if (!text || sscanf(text, "%31s%n", name, &used) != 1)
		return FALSE;
	if (sscanf(text + used, "%f %f %f %f %f", &values[0], &values[1], &values[2], &values[3], &values[4]) == 5)
	{
		for (index = 0; index < 5; index++)
		{
			if (!(values[index] > -5000.f && values[index] < 5000.f))
				return FALSE;
		}
		camera->position.x = values[0];
		camera->position.y = values[1];
		camera->position.z = values[2];
		camera->yaw = values[3] * MENU_SCENE_DEGREES;
		camera->pitch = values[4] * MENU_SCENE_DEGREES;
		return TRUE;
	}
	for (index = 0; scenario && index < scenario->cutscene_camera_points.count; index++)
	{
		struct scenario_cutscene_camera_point const *point = TAG_BLOCK_GET_ELEMENT(
			&scenario->cutscene_camera_points, index, struct scenario_cutscene_camera_point);

		if (!strncmp(point->name, name, sizeof(point->name)))
		{
			camera->position = point->position;
			camera->yaw = point->orientation.yaw;
			camera->pitch = point->orientation.pitch;
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------- public code */

void menu_scene_objects_want(
	char const *screen,
	char const *objects,
	int has_camera,
	float x,
	float y,
	float z,
	float yaw,
	float pitch)
{
	real_point3d camera_position;
	real_point3d const *camera = has_camera ? &camera_position : NULL;
	char text[256];
	char *cursor, *end;
	short count = 0;
	short index;

	initialize();
	camera_position.x = x;
	camera_position.y = y;
	camera_position.z = z;
	for (index = 0; index < MAXIMUM_MENU_SCENE_OBJECTS; index++)
		menu_scene_objects.slots[index].wanted = FALSE;
	menu_scene_objects.has_camera = camera != NULL;
	if (camera)
	{
		menu_scene_objects.camera.position = *camera;
		menu_scene_objects.camera.yaw = yaw;
		menu_scene_objects.camera.pitch = pitch;
	}
	snprintf(menu_scene_objects.screen, sizeof(menu_scene_objects.screen), "%s", screen ? screen : "");
	if (!objects || !camera)
		return;
	snprintf(text, sizeof(text), "%s", objects);
	for (cursor = text; cursor && *cursor && count < MAXIMUM_MENU_SCENE_OBJECTS; cursor = end)
	{
		struct menu_scene_object *object = &menu_scene_objects.slots[count];
		char pack[32];
		int used = 0;

		end = strchr(cursor, ';');
		if (end)
			*end++ = 0;
		if (sscanf(cursor, "%31s%n", pack, &used) != 1 || !placement_parse(cursor + used, object))
		{
			platform_log("menus: h3 scene object: %s: \"%s\" is not \"<pack> <distance> <right> <up> <facing>\"",
				menu_scene_objects.screen, cursor);
			continue;
		}
		/* (the same character asked for again where it stands: kept) */
		if (strcmp(object->pack, pack) || object->camera.position.x != camera->x || object->camera.position.y != camera->y ||
			object->camera.position.z != camera->z)
		{
			object->refused = FALSE;
		}
		snprintf(object->pack, sizeof(object->pack), "%s", pack);
		object->camera = menu_scene_objects.camera;
		object->color_index = NONE;
		object->wanted = TRUE;
		count++;
	}
}

void menu_scene_objects_update(
	void)
{
	short index;

	initialize();
	for (index = 0; index < NUMBER_OF_MENU_SCENE_SLOTS; index++)
	{
		struct menu_scene_object *object = &menu_scene_objects.slots[index];

		/* (another map: its objects went with the last one) */
		if (object->unit_index != NONE && object->epoch != scene_packs_map_epoch())
			object->unit_index = NONE;
		if (!object->wanted || !halo_main_menu_idle() || !halo_menu_style_halo3())
		{
			if (object->unit_index != NONE)
				platform_log("menus: h3 scene object %d: deleted", index);
			object_forget(object, TRUE);
			if (!halo_main_menu_idle())
				object->wanted = FALSE;
			continue;
		}
		/* (held; or made, or made again at once in a new colour: no frame
		without it) */
		if (object->unit_index != NONE)
			object_hold(object);
		if (object->unit_index == NONE && !object->refused)
			object_make(object, index);
	}
	/* (object_lights.c's quick way out: none made, none asked about) */
	menu_scene_objects.made_count = 0;
	for (index = 0; index < NUMBER_OF_MENU_SCENE_SLOTS; index++)
	{
		if (menu_scene_objects.slots[index].unit_index != NONE)
			menu_scene_objects.made_count++;
	}
}

int menu_scene_spartan_show(
	char const *camera,
	char const *placement,
	short color_index)
{
	struct menu_scene_object *object;

	initialize();
	object = &menu_scene_objects.slots[MENU_SCENE_PREVIEW];
	if (camera ? !camera_parse(camera, &object->camera) : !menu_scene_objects.has_camera)
		return FALSE;
	if (!camera)
		object->camera = menu_scene_objects.camera;
	if (!placement_parse(placement ? placement : "1.6 0.45 -0.35 15", object))
		return FALSE;
	snprintf(object->pack, sizeof(object->pack), "%s", MENU_SCENE_PREVIEW_PACK);
	object->color_index = color_index;
	object->refused = FALSE;
	object->wanted = TRUE;
	return TRUE;
}

void menu_scene_spartan_color(
	short color_index)
{
	initialize();
	menu_scene_objects.slots[MENU_SCENE_PREVIEW].color_index = color_index;
}

void menu_scene_spartan_hide(
	void)
{
	initialize();
	menu_scene_objects.slots[MENU_SCENE_PREVIEW].wanted = FALSE;
}

int menu_scene_spartan_shown(
	void)
{
	struct menu_scene_object const *object = &menu_scene_objects.slots[MENU_SCENE_PREVIEW];

	return menu_scene_objects_initialized && object->wanted && object->unit_index != NONE;
}

boolean menu_scene_objects_posed(
	long unit_index)
{
	short index;

	if (!menu_scene_objects_initialized || menu_scene_objects.made_count <= 0 || unit_index == NONE)
		return FALSE;
	for (index = 0; index < NUMBER_OF_MENU_SCENE_SLOTS; index++)
	{
		struct menu_scene_object const *object = &menu_scene_objects.slots[index];

		if (object->unit_index == unit_index && object->epoch == scene_packs_map_epoch())
			return TRUE;
	}
	return FALSE;
}

boolean menu_scene_objects_lighting(
	long object_index,
	struct render_lighting *lighting)
{
	struct object_datum *datum;
	long parent_index;
	short index;

	if (!menu_scene_objects_initialized || menu_scene_objects.made_count <= 0 || object_index == NONE)
		return FALSE;
	datum = object_try_and_get(object_index);
	parent_index = datum ? datum->object.parent_object_index : NONE;
	for (index = 0; index < NUMBER_OF_MENU_SCENE_SLOTS; index++)
	{
		struct menu_scene_object const *object = &menu_scene_objects.slots[index];

		/* (the character, or what it holds) */
		if (object->unit_index != NONE && object->epoch == scene_packs_map_epoch() &&
			(object->unit_index == object_index || object->unit_index == parent_index))
		{
			*lighting = object->lighting;
			return TRUE;
		}
	}
	return FALSE;
}
