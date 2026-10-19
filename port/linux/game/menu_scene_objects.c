/*
MENU_SCENE_OBJECTS.C

The scenes behind the Halo 3 menus' lobbies, as Halo 3's lobbies have
theirs: a screen of the Halo 3 set with a scene= (its camera, menu_functions.c's
h3 scenes) may say what stands in front of that camera, one entry after
another,

	scene_object="<pack>[:<tag>] [<distance> <right> <up> <facing>] [<option> ...] [; ...]"

each entry an object of a scene pack (scene_packs.c builds and loads the
pack from the player's own maps, app0:packs/<pack>.txt): its recipe's tag
whose name ends in <tag> (its last word, spaces written '_', any case:
menu_campaign:warthog, menu_multiplayer:cyborg_mp), or its first; a biped, a
vehicle, a weapon, a piece of equipment or the flag. It stands in the
camera's frame - distance along its view, right of it, up from there (world
units) - turned facing degrees from facing the camera (positive: its front
to the picture's left). The options, each <name>=<value>:

	pitch=<degrees>    its nose up (a Warthog mid-jump), roll=<degrees> its
	                   left side down (banked to its left; negative: to its
	                   right)
	color=<0..17>      in that multiplayer colour (0: white), or
	                   color=profile, the first local player's; without it,
	                   the object's own colours
	weapon=[<pack>:]<tag>  that weapon in its hands (the entry's pack when
	                   no pack is named); weapon=none, none
	anim=<name>        that animation of its own, named after its seat and
	                   weapon as the game names them ("airborne": stand
	                   missile airborne with a rocket launcher, "idle"), or
	                   whole, '_' for each space (crouch_rifle_idle);
	                   stance=crouch names the crouching seat's
	frame=<n>          that frame of it held (a still); without it, looped
	in=<entry> seat=<label>  seated in that entry's vehicle (1 is the first)
	                   as the scripts' unit_enter_vehicle seats a unit: the
	                   seat of that label, or whose label ends in -<label>
	                   (driver, gunner, passenger: W-driver, W-gunner,
	                   W-passenger); the place is its seat's, none is given
	look=<entry>       turned to that entry, its head (and a flying biped's
	                   whole body) looking at it
	suspension=<0..1>  a vehicle's wheels: 0 (the default) hanging all the
	                   way down, as in the air; 1 pushed all the way up

A vehicle is held still, frozen as it was placed: none of its physics run
(menu_scene_objects_frozen, vehicles.c), so nothing of it touches anything
- no dust, no skid, no crash, no sound -, and its suspension holds where the
scene says. A scene is all or nothing: its objects are made together once
every pack it names is loaded, and none is made when one of them cannot be
(no source map in the maps folder, a build or a load that failed, a tag the
pack does not have). The pickers opened from a lobby keep its scene; the main
menu (scene="ring") and leaving the main menu let it go.

It does nothing at all, and nothing breaks, when it cannot: Menu style Halo
CE, HALO_MENU_SCENES=0 (no scene is asked for), HALO_SCENE_PACKS=0, a missing
map: there is simply no scene.

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

How they look: upright in the picture (placed and stood in the camera's own
frame, its pitch with it: a camera pitched up no longer looks at them from
their feet), a biped's weapon held low in front and its head turned to the
camera unless the scene poses it (the aim and look held each frame: no
player or AI aims it), and lit by a light of their own
(menu_scene_objects_lighting, asked by object_lights.c, for what they hold
and who sits in them too: ui.map's space has no lightmap under them, so they
took its structure's dim default) - a key light from the camera's upper
left, a cool rim from behind, a soft ambient.
*/

/* ---------- headers */

#include "cseries.h"
#include "real_math.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "units/biped_definitions.h"
#include "items/weapon_definitions.h"
#include "models/model_animation_definitions.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "memory/data.h"
#include "render/render.h"

#include "halo_menus.h"
#include "scene_packs.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);
int halo_main_menu_idle(void);
void placement_data_set_change_color(struct object_placement_data *placement_data, real_rgb_color const *change_color);
unsigned long scene_packs_map_epoch(void);
void unit_scripting_enter_vehicle(long unit_index, long vehicle_index, char const *seat_name);
extern struct data_array *effect_data;
extern struct data_array *particle_data;

/* ---------- constants */

enum
{
	/* a scene's entries (the campaign lobby's: a Warthog and three in it) */
	MAXIMUM_MENU_SCENE_OBJECTS = 6,
	/* the preview's character: after the screen's */
	MENU_SCENE_PREVIEW = MAXIMUM_MENU_SCENE_OBJECTS,
	NUMBER_OF_MENU_SCENE_SLOTS,
	/* Halo CE's multiplayer colours (player_profile.c) */
	NUMBER_OF_MENU_SCENE_COLORS = 18,
	/* color=: the object's own colours, the profile's */
	MENU_SCENE_COLOR_OWN = -1,
	MENU_SCENE_COLOR_PROFILE = -2,
	/* frames an animation's name is looked for (its weapon readied first)
	before it is given up */
	MENU_SCENE_ANIMATION_TRIES = 30,
	MENU_SCENE_NAME_SIZE = 48,
	MENU_SCENE_TEXT_SIZE = 1024,
};

#define MENU_SCENE_PREVIEW_PACK "menu_spartan"
/* (a model chosen rests this long before its pack is wanted) */
#define MENU_SCENE_MODEL_REST_US 300000ull
/* (a scene whose packs are loaded waits for their textures, on the cache
file thread, at most this long before it is shown: none of them drawn as
a stand-in, no frame waiting for one) */
#define MENU_SCENE_TEXTURES_WAIT_US 3000000ull
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
	/* the recipe's tag (its name's last word), "" for its first */
	char tag[MENU_SCENE_NAME_SIZE];
	real distance;
	real right;
	real up;
	real facing;
	real pitch;
	real roll;
	/* MENU_SCENE_COLOR_OWN, _PROFILE, or a multiplayer colour */
	short color_index;
	/* the weapon: "" (none), its pack and tag */
	char weapon_pack[32];
	char weapon_tag[MENU_SCENE_NAME_SIZE];
	/* the animation ("" none), the crouching seat's, the frame held (NONE:
	looped) */
	char animation[MENU_SCENE_NAME_SIZE];
	boolean crouch;
	short frame;
	/* seated in that entry's vehicle (NONE: placed), in the seat so named */
	short vehicle_entry;
	char seat[32];
	/* looking at that entry (NONE: the camera) */
	short look_entry;
	real suspension;
	struct menu_scene_camera camera;

	/* (the update's) what was made, the map it was made on, its colour */
	long object_index;
	long weapon_index;
	unsigned long epoch;
	short made_color_index;
	boolean refused;
	/* the animation found and started (or given up), the tries */
	boolean animation_started;
	boolean animation_given_up;
	short animation_tries;
	short animation_index;
	/* (the update's) its light, from its camera (object_lights.c reads it) */
	struct render_lighting lighting;
	/* (the preview's) how much further away it stands than placed: a
	taller character than the menu Spartan (a player model's) as far as
	keeps it whole in the window */
	real scale;
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
	/* the scene's description, as last asked for (the same asked again:
	kept) */
	char text[MENU_SCENE_TEXT_SIZE];
	/* the scene could not be made (a pack failed): not tried again until
	another is asked for */
	boolean refused;
	/* another scene asked for: the objects standing let go by the update
	first */
	boolean replaced;
	/* the scene's objects stand now */
	boolean made;
	unsigned long long made_at;
	/* effects and particles seen at most while they stood (no dust: none) */
	short most_effects;
	short most_particles;
	/* objects made now (object_lights.c's quick way out) */
	short made_count;
	/* objects let go, still there until the objects' next update deletes
	them (object_delete marks them): still frozen meanwhile, the updates
	left */
	long released[NUMBER_OF_MENU_SCENE_SLOTS * 2];
	short released_updates[NUMBER_OF_MENU_SCENE_SLOTS * 2];
	/* the preview's pack: menu_spartan, or a player model's
	(menu_scene_spartan_model) */
	char preview_pack[32];
	/* the pack asked for since (menu_scene_spartan_model), taken once it
	has rested MENU_SCENE_MODEL_REST_US: stepping through models loads none
	of those passed */
	char pending_pack[32];
	unsigned long long pending_since;
	/* (the scene's, the preview's) since when their packs are loaded and
	their textures awaited (0: not yet), before they are shown */
	unsigned long long scene_textures_since;
	unsigned long long preview_textures_since;
} menu_scene_objects;

static boolean menu_scene_objects_initialized = FALSE;

/* ---------- private code */

static void object_slot_reset(struct menu_scene_object *object)
{
	object->object_index = NONE;
	object->weapon_index = NONE;
	object->color_index = MENU_SCENE_COLOR_OWN;
	object->frame = NONE;
	object->vehicle_entry = NONE;
	object->look_entry = NONE;
	object->animation_index = NONE;
}

static void initialize(void)
{
	short index;

	if (menu_scene_objects_initialized)
		return;
	for (index = 0; index < NUMBER_OF_MENU_SCENE_SLOTS; index++)
		object_slot_reset(&menu_scene_objects.slots[index]);
	menu_scene_objects_initialized = TRUE;
}

static boolean real_in_range(float value, float bound)
{
	return value > -bound && value < bound;
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
		if (!real_in_range(values[index], 1000.f))
			return FALSE;
	}
	object->distance = values[0];
	object->right = values[1];
	object->up = values[2];
	object->facing = values[3];
	return TRUE;
}

/* a word of letters, digits and "_-." (a pack's, a tag's or a seat's name) */
static boolean name_word(char const *word, long size)
{
	long length = (long)strlen(word);

	return length > 0 && length < size && strspn(word, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") ==
		(size_t)length;
}

/* "<pack>[:<tag>]" */
static boolean pack_and_tag_parse(char const *text, char *pack, long pack_size, char *tag, long tag_size)
{
	char const *colon = strchr(text, ':');
	long pack_length = colon ? (long)(colon - text) : (long)strlen(text);

	if (pack_length <= 0 || pack_length >= pack_size)
		return FALSE;
	memcpy(pack, text, (size_t)pack_length);
	pack[pack_length] = 0;
	snprintf(tag, (size_t)tag_size, "%s", colon ? colon + 1 : "");
	return name_word(pack, pack_size) && (!colon || name_word(tag, tag_size));
}

/* one "<name>=<value>" option of an entry */
static boolean option_parse(char const *word, struct menu_scene_object *object)
{
	char const *equals = strchr(word, '=');
	char name[16];
	char const *value;
	long length;
	float number;
	int used = 0;

	if (!equals || equals == word || equals - word >= (long)sizeof(name))
		return FALSE;
	length = (long)(equals - word);
	memcpy(name, word, (size_t)length);
	name[length] = 0;
	value = equals + 1;
	if (!*value)
		return FALSE;
	if (!strcmp(name, "pitch") || !strcmp(name, "roll") || !strcmp(name, "suspension"))
	{
		if (sscanf(value, "%f%n", &number, &used) != 1 || value[used] || !real_in_range(number, 360.f))
			return FALSE;
		if (!strcmp(name, "pitch"))
			object->pitch = number;
		else if (!strcmp(name, "roll"))
			object->roll = number;
		else if (number >= 0.f && number <= 1.f)
			object->suspension = number;
		else
			return FALSE;
		return TRUE;
	}
	if (!strcmp(name, "color"))
	{
		if (!strcmp(value, "profile"))
			object->color_index = MENU_SCENE_COLOR_PROFILE;
		else if (sscanf(value, "%f%n", &number, &used) == 1 && !value[used] && number >= 0.f &&
			number < NUMBER_OF_MENU_SCENE_COLORS && number == (float)(int)number)
		{
			object->color_index = (short)number;
		}
		else
			return FALSE;
		return TRUE;
	}
	if (!strcmp(name, "frame") || !strcmp(name, "in") || !strcmp(name, "look"))
	{
		int integer;

		if (sscanf(value, "%d%n", &integer, &used) != 1 || value[used])
			return FALSE;
		if (!strcmp(name, "frame") && integer >= 0 && integer < 10000)
			object->frame = (short)integer;
		else if (strcmp(name, "frame") && integer >= 1 && integer <= MAXIMUM_MENU_SCENE_OBJECTS)
			*(!strcmp(name, "in") ? &object->vehicle_entry : &object->look_entry) = (short)(integer - 1);
		else
			return FALSE;
		return TRUE;
	}
	if (!strcmp(name, "weapon"))
	{
		if (!strcmp(value, "none"))
		{
			object->weapon_pack[0] = object->weapon_tag[0] = 0;
			return TRUE;
		}
		if (strchr(value, ':'))
			return pack_and_tag_parse(value, object->weapon_pack, sizeof(object->weapon_pack), object->weapon_tag,
				sizeof(object->weapon_tag)) && object->weapon_tag[0];
		/* (the entry's own pack) */
		snprintf(object->weapon_pack, sizeof(object->weapon_pack), "%s", object->pack);
		snprintf(object->weapon_tag, sizeof(object->weapon_tag), "%s", value);
		return name_word(value, sizeof(object->weapon_tag));
	}
	if (!strcmp(name, "anim"))
	{
		snprintf(object->animation, sizeof(object->animation), "%s", value);
		return name_word(value, sizeof(object->animation));
	}
	if (!strcmp(name, "stance"))
	{
		if (strcmp(value, "crouch") && strcmp(value, "stand"))
			return FALSE;
		object->crouch = !strcmp(value, "crouch");
		return TRUE;
	}
	if (!strcmp(name, "seat"))
	{
		snprintf(object->seat, sizeof(object->seat), "%s", value);
		return name_word(value, sizeof(object->seat));
	}
	return FALSE;
}

/* one entry, "<pack>[:<tag>] [<distance> <right> <up> <facing>] [<option>
...]": FALSE if it is not one */
static boolean entry_parse(char *text, struct menu_scene_object *object, short entry)
{
	char *word, *end;
	short placement_count = 0;
	float placement[4] = { 0.f, 0.f, 0.f, 0.f };

	object->distance = object->right = object->up = object->facing = 0.f;
	object->pitch = object->roll = 0.f;
	object->suspension = 0.f;
	object->color_index = MENU_SCENE_COLOR_OWN;
	object->weapon_pack[0] = object->weapon_tag[0] = 0;
	object->animation[0] = 0;
	object->crouch = FALSE;
	object->frame = NONE;
	object->vehicle_entry = NONE;
	object->seat[0] = 0;
	object->look_entry = NONE;
	word = strtok_r(text, " \t\r\n", &end);
	if (!word || !pack_and_tag_parse(word, object->pack, sizeof(object->pack), object->tag, sizeof(object->tag)))
		return FALSE;
	while ((word = strtok_r(NULL, " \t\r\n", &end)) != NULL)
	{
		float number;
		int used = 0;

		if (!strchr(word, '=') && placement_count < 4 && sscanf(word, "%f%n", &number, &used) == 1 && !word[used])
		{
			if (!real_in_range(number, 1000.f))
				return FALSE;
			placement[placement_count++] = number;
			continue;
		}
		if (!option_parse(word, object))
			return FALSE;
	}
	/* (a place given whole, or none for a seated one) */
	if (placement_count == 4)
	{
		object->distance = placement[0];
		object->right = placement[1];
		object->up = placement[2];
		object->facing = placement[3];
	}
	else if (placement_count || object->vehicle_entry == NONE)
		return FALSE;
	/* (a seat with its vehicle, an earlier entry; nothing looks at itself) */
	if ((object->vehicle_entry != NONE) != (object->seat[0] != 0) ||
		(object->vehicle_entry != NONE && object->vehicle_entry >= entry) || object->look_entry == entry)
	{
		return FALSE;
	}
	return TRUE;
}

/* the multiplayer colour index the object is drawn in, NONE for its own
colours */
static short color_index_of(struct menu_scene_object const *object)
{
	long profile_index;
	struct player_profile profile;

	if (object->color_index >= 0 && object->color_index < NUMBER_OF_MENU_SCENE_COLORS)
		return object->color_index;
	if (object->color_index != MENU_SCENE_COLOR_PROFILE)
		return NONE;
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

/* where the entry stands in its camera's frame - distance along the view,
right of it, up in the picture - (with the preview's scale) */
static void entry_position(struct menu_scene_object const *object, real_point3d *position)
{
	struct menu_scene_frame frame;
	real scale = object->scale > 1.f ? object->scale : 1.f;

	camera_frame(&object->camera, &frame);
	position->x = object->camera.position.x + (frame.view.i * object->distance + frame.right.i * object->right +
		frame.up.i * object->up) * scale;
	position->y = object->camera.position.y + (frame.view.j * object->distance + frame.right.j * object->right +
		frame.up.j * object->up) * scale;
	position->z = object->camera.position.z + (frame.view.k * object->distance + frame.right.k * object->right +
		frame.up.k * object->up) * scale;
}

/* where the object stands, and its facing: toward the camera (or the entry
it looks at, in the picture's plane), turned by facing about the picture's
up, then its nose pitched up and its left side rolled down - so that it
stands upright in the picture whatever the camera's pitch (a flying biped
keeps its forward's pitch; its up follows it: biped_snap_facing) */
static void object_place(struct menu_scene_object const *object, real_point3d *position, real_vector3d *forward,
	real_vector3d *up)
{
	struct menu_scene_frame frame;
	real facing = object->facing * MENU_SCENE_DEGREES;
	real cosine = (real)cos(facing), sine = (real)sin(facing);
	real_vector3d base, right;

	camera_frame(&object->camera, &frame);
	entry_position(object, position);
	/* (back toward the camera; or toward what it looks at, level in the
	picture) */
	base.i = -frame.view.i;
	base.j = -frame.view.j;
	base.k = -frame.view.k;
	if (object->look_entry != NONE)
	{
		real_point3d target;
		real_vector3d toward;
		real along;

		entry_position(&menu_scene_objects.slots[object->look_entry], &target);
		vector_from_points3d(position, &target, &toward);
		along = dot_product3d(&toward, &frame.up);
		toward.i -= frame.up.i * along;
		toward.j -= frame.up.j * along;
		toward.k -= frame.up.k * along;
		if (normalize3d(&toward) > 0.0001f)
			base = toward;
	}
	/* (turned about the picture's up: positive facing turns its front to
	the picture's left, as before) */
	cross_product3d(&base, &frame.up, &right);
	forward->i = base.i * cosine - right.i * sine;
	forward->j = base.j * cosine - right.j * sine;
	forward->k = base.k * cosine - right.k * sine;
	*up = frame.up;
	if (object->pitch != 0.f)
	{
		real angle = object->pitch * MENU_SCENE_DEGREES;
		real c = (real)cos(angle), s = (real)sin(angle);
		real_vector3d f = *forward, u = *up;

		forward->i = f.i * c + u.i * s;
		forward->j = f.j * c + u.j * s;
		forward->k = f.k * c + u.k * s;
		up->i = u.i * c - f.i * s;
		up->j = u.j * c - f.j * s;
		up->k = u.k * c - f.k * s;
	}
	if (object->roll != 0.f)
	{
		real angle = object->roll * MENU_SCENE_DEGREES;
		real c = (real)cos(angle), s = (real)sin(angle);
		real_vector3d u = *up;

		/* (its right: forward x up; its up leans to its left, its left side
		down) */
		cross_product3d(forward, &u, &right);
		up->i = u.i * c - right.i * s;
		up->j = u.j * c - right.j * s;
		up->k = u.k * c - right.k * s;
	}
	normalize3d(forward);
	normalize3d(up);
}

/* the unit's aim and look held: a placed biped's weapon low in front of it
and its head to the camera (or to the entry it looks at); a seated one's
straight ahead of its vehicle */
static void object_pose(struct menu_scene_object const *object, struct unit_datum *unit, real_vector3d const *forward,
	real_vector3d const *up)
{
	real_vector3d aim, look;
	real cosine = (real)cos(MENU_SCENE_AIM_DOWN), sine = (real)sin(MENU_SCENE_AIM_DOWN);
	struct menu_scene_frame frame;

	camera_frame(&object->camera, &frame);
	if (object->vehicle_entry != NONE)
	{
		aim = *forward;
		look = *forward;
	}
	else
	{
		aim.i = forward->i * cosine - up->i * sine;
		aim.j = forward->j * cosine - up->j * sine;
		aim.k = forward->k * cosine - up->k * sine;
		look.i = -frame.view.i;
		look.j = -frame.view.j;
		look.k = -frame.view.k;
	}
	if (object->look_entry != NONE)
	{
		struct object_datum *target = object_try_and_get(menu_scene_objects.slots[object->look_entry].object_index);

		if (target)
		{
			vector_from_points3d(&unit->object.position, &target->object.position, &look);
			if (normalize3d(&look) <= 0.0001f)
				look = *forward;
			aim = look;
		}
	}
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

	real ambient = 1.f, key = 1.f, rim = 1.f, reflection = 0.5f;

#ifndef HALO_RELEASE
	/* (debug: HALO_MENU_SCENE_LIGHT="<ambient> <key> <rim>", each a scale) */
	{
		char const *setting = getenv("HALO_MENU_SCENE_LIGHT");

		if (setting)
			sscanf(setting, "%f %f %f %f", &ambient, &key, &rim, &reflection);
	}
#endif
	camera_frame(&object->camera, &frame);
	memset(lighting, 0, sizeof(*lighting));
	lighting->ambient_color.red = 0.58f * ambient;
	lighting->ambient_color.green = 0.57f * ambient;
	lighting->ambient_color.blue = 0.55f * ambient;
	lighting->distant_light_count = 2;
	light_set(&lighting->distant_lights[0], 1.70f * key, 1.60f * key, 1.45f * key, &frame, 0.55f, 0.45f, -0.70f);
	light_set(&lighting->distant_lights[1], 0.75f * rim, 0.85f * rim, 1.15f * rim, &frame, -0.70f, -0.45f, -0.25f);
	lighting->point_light_count = 0;
	lighting->reflection_tint_color.alpha = reflection;
	lighting->reflection_tint_color.red = 1.f;
	lighting->reflection_tint_color.green = 1.f;
	lighting->reflection_tint_color.blue = 1.f;
	lighting->shadow_vector.i = -frame.up.i;
	lighting->shadow_vector.j = -frame.up.j;
	lighting->shadow_vector.k = -frame.up.k;
	lighting->shadow_color.red = lighting->shadow_color.green = lighting->shadow_color.blue = 1.f;
}

/* the object made by this slot, if it is still there (this map's) */
static boolean slot_object_valid(struct menu_scene_object const *object)
{
	return object->object_index != NONE && object->epoch == scene_packs_map_epoch() &&
		object_try_and_get(object->object_index) != NULL;
}

static void object_forget(struct menu_scene_object *object, boolean delete_it)
{
	if (delete_it && slot_object_valid(object))
	{
		struct unit_datum *unit = unit_try_and_get(object->object_index);
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
		object_delete(object->object_index);
		/* (deleted by the objects' next update: until then a frozen
		vehicle's physics stay off - ui.map's globals have none of what
		they read) */
		{
			short index;

			for (index = 0; index < (short)NUMBEROF(menu_scene_objects.released); index++)
			{
				if (menu_scene_objects.released_updates[index] <= 0)
				{
					menu_scene_objects.released[index] = object->object_index;
					menu_scene_objects.released_updates[index] = 4;
					break;
				}
			}
		}
	}
	object->object_index = NONE;
	object->weapon_index = NONE;
	object->refused = FALSE;
	object->animation_started = FALSE;
	object->animation_given_up = FALSE;
	object->animation_tries = 0;
	object->animation_index = NONE;
}

/* a loaded pack's recipe tag whose name's last word is tag ('_' for a
space, any case; "" its first): the game's tag index, or NONE */
static long pack_tag_find(char const *pack, char const *tag)
{
	long roots[16];
	short count = scene_packs_roots(pack, roots, NUMBEROF(roots));
	short index;

	for (index = 0; index < count; index++)
	{
		char const *name = roots[index] != NONE ? tag_get_name(roots[index]) : NULL;
		char const *last, *a, *b;

		if (!name)
			continue;
		if (!*tag)
			return roots[index];
		last = strrchr(name, '\\');
		last = last ? last + 1 : name;
		for (a = last, b = tag; *a && *b; a++, b++)
		{
			if (tolower((unsigned char)*a) != tolower((unsigned char)*b) && !(*a == ' ' && *b == '_'))
				break;
		}
		if (!*a && !*b)
			return roots[index];
	}
	return NONE;
}

/* the vehicle's seat a scene names: of that label, or whose label ends in
"-<label>" (driver: W-driver), its label (NULL for none) */
static char const *seat_label_find(long vehicle_index, char const *wanted)
{
	struct unit_datum *vehicle = unit_try_and_get(vehicle_index);
	struct unit_definition *definition = vehicle ? unit_definition_get(vehicle->definition_index) : NULL;
	long wanted_length = (long)strlen(wanted);
	short index;

	if (!definition)
		return NULL;
	for (index = 0; index < definition->unit.seats.count; index++)
	{
		struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, index, struct unit_seat);

		if (!_stricmp(seat->label, wanted))
			return seat->label;
	}
	for (index = 0; index < definition->unit.seats.count; index++)
	{
		struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, index, struct unit_seat);
		long length = (long)strnlen(seat->label, sizeof(seat->label));

		if (length > wanted_length && seat->label[length - wanted_length - 1] == '-' &&
			!_stricmp(seat->label + length - wanted_length, wanted))
		{
			return seat->label;
		}
	}
	return NULL;
}

/* the object made, its pack loaded (scene objects: every pack they name is) */
static boolean object_make(struct menu_scene_object *object, short slot)
{
	long definition_index = pack_tag_find(object->pack, object->tag);
	unsigned long group_tag;
	struct object_placement_data placement;
	real_rgb_color color;
	real_vector3d up;
	unsigned long long started;
	struct unit_datum *unit = NULL;
	long vehicle_index = NONE;
	char const *seat_label = NULL;
	long weapon_definition_index = NONE;

	if (definition_index == NONE)
	{
		platform_log("menus: h3 scene object %d: the pack %s has no tag %s", slot, object->pack,
			object->tag[0] ? object->tag : "at all");
		return FALSE;
	}
	group_tag = tag_get_group_tag(definition_index);
	started = vita_host_time_us();
	if (object->vehicle_entry != NONE)
	{
		struct menu_scene_object const *vehicle = &menu_scene_objects.slots[object->vehicle_entry];

		vehicle_index = slot_object_valid(vehicle) ? vehicle->object_index : NONE;
		if (vehicle_index == NONE || !unit_try_and_get(vehicle_index) || group_tag != 'bipd' ||
			(seat_label = seat_label_find(vehicle_index, object->seat)) == NULL)
		{
			platform_log("menus: h3 scene object %d: no seat %s in entry %d for %s", slot, object->seat,
				object->vehicle_entry + 1, tag_get_name(definition_index));
			return FALSE;
		}
	}
	if (object->weapon_tag[0])
	{
		weapon_definition_index = pack_tag_find(object->weapon_pack, object->weapon_tag);
		if (weapon_definition_index == NONE || tag_get_group_tag(weapon_definition_index) != 'weap')
		{
			platform_log("menus: h3 scene object %d: the pack %s has no weapon %s", slot, object->weapon_pack,
				object->weapon_tag);
			return FALSE;
		}
	}
	/* (ui.map has no ground under its cameras: the pack's copy of a biped,
	which this map alone has, made one that flies, which stands where it is
	- state idle, seat stand: no airborne pose of its own - rather than
	falling, and keeps the pitch it is given, so it stands upright in a
	pitched camera's picture) */
	if (group_tag == 'bipd')
	{
		struct biped_definition *definition = biped_definition_get(definition_index);

		SET_FLAG(definition->biped.flags, _biped_flying_bit, TRUE);
		/* (the preview: a character taller than the Spartan, whose eyes are
		0.62 up, further away, its head in the picture too) */
		object->scale = slot == MENU_SCENE_PREVIEW && definition->biped.standing_camera_height > 0.62f &&
			definition->biped.standing_camera_height < 3.f ? definition->biped.standing_camera_height / 0.62f : 1.f;
	}
	object_placement_data_new(&placement, definition_index, NONE);
	SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
	if (vehicle_index != NONE)
	{
		struct object_datum *vehicle = object_get(vehicle_index);

		placement.position = vehicle->object.position;
		placement.forward = vehicle->object.forward;
		placement.up = vehicle->object.up;
		up = vehicle->object.up;
	}
	else
	{
		object_place(object, &placement.position, &placement.forward, &up);
		placement.up = up;
	}
	object->made_color_index = color_index_of(object);
	if (object->made_color_index != NONE)
	{
		color = player_profile_get_rgb_color(object->made_color_index);
		placement_data_set_change_color(&placement, &color);
	}
	object->object_index = object_new(&placement);
	object->epoch = scene_packs_map_epoch();
	if (object->object_index == NONE)
	{
		platform_log("menus: h3 scene object %d: %s could not be made", slot, tag_get_name(definition_index));
		return FALSE;
	}
	/* (kept awake: an object sleeps outside the players' clusters, and
	ui.map has no player, so it would stand still, its weapon never readied) */
	object_set_automatic_deactivation(object->object_index, FALSE);
	object_activate(object->object_index);
	object_light(object);
	unit = unit_try_and_get(object->object_index);
	if (unit)
		object_pose(object, unit, &placement.forward, &up);
	/* (a player model's pack holds its biped alone: the menu Spartan's
	rifle in its hands, when that pack is loaded too) */
	if (slot == MENU_SCENE_PREVIEW && unit)
	{
		long roots[2] = { NONE, NONE };

		if (scene_packs_roots(object->pack, roots, 2) > 1 && roots[1] != NONE && tag_get_group_tag(roots[1]) == 'weap')
			weapon_definition_index = roots[1];
		else if (strcmp(object->pack, MENU_SCENE_PREVIEW_PACK) &&
			scene_packs_want(MENU_SCENE_PREVIEW_PACK) == _scene_pack_state_loaded &&
			scene_packs_roots(MENU_SCENE_PREVIEW_PACK, roots, 2) == 2)
		{
			weapon_definition_index = roots[1];
		}
	}
	/* (the weapon in its hands, if its own initial weapons gave it none) */
	if (unit && weapon_definition_index != NONE && unit->unit.weapon_object_indices[0] == NONE)
	{
		struct object_placement_data weapon_placement;
		long weapon_index;

		object_placement_data_new(&weapon_placement, weapon_definition_index, object->object_index);
		weapon_placement.position = placement.position;
		weapon_index = object_new(&weapon_placement);
		if (weapon_index != NONE && !unit_add_weapon_to_inventory(object->object_index, weapon_index, FALSE))
		{
			object_delete(weapon_index);
			weapon_index = NONE;
		}
		object->weapon_index = weapon_index;
		platform_log("menus: h3 scene object %d: %s %s", slot, tag_get_name(weapon_definition_index),
			weapon_index != NONE ? "in its hands" : "could not be given");
	}
	/* (seated as the scripts seat one: unit_enter_vehicle) */
	if (unit && vehicle_index != NONE)
	{
		unit_scripting_enter_vehicle(object->object_index, vehicle_index, seat_label);
		platform_log("menus: h3 scene object %d: %s %s %s", slot, tag_get_name(definition_index),
			object_get(object->object_index)->object.parent_object_index == vehicle_index ? "seated as" : "could not sit as",
			seat_label);
	}
	platform_log("menus: h3 scene object %d: %s (%s) at %.2f %.2f %.2f facing %.0f pitch %.0f, colour %d: made in %.1f ms",
		slot, tag_get_name(definition_index), object->pack, placement.position.x, placement.position.y,
		placement.position.z, object->facing, object->pitch, object->made_color_index,
		(double)(vita_host_time_us() - started) / 1000.0);
	return TRUE;
}

/* the animation's name the scene asks for: whole ('_' for a space), or
"<seat> <weapon class> <name>" after the unit's seat (or the crouching
seat's) and the weapon it holds now. FALSE until it can be named */
static boolean animation_name(struct menu_scene_object const *object, struct unit_datum *unit, char *name, long size)
{
	struct animation_graph *graph;
	struct animation_graph_unit_seat *seat;
	struct animation_graph_weapon_class *weapon_class;
	char *cursor;

	if (strchr(object->animation, '_'))
	{
		snprintf(name, (size_t)size, "%s", object->animation);
		for (cursor = name; *cursor; cursor++)
		{
			if (*cursor == '_')
				*cursor = ' ';
		}
		return TRUE;
	}
	if (unit->object.animation.animation_graph_index == NONE)
		return FALSE;
	graph = animation_graph_definition_get(unit->object.animation.animation_graph_index);
	if (unit->unit.animation.seat_index < 0 || unit->unit.animation.seat_index >= graph->unit_seats.count)
		return FALSE;
	seat = TAG_BLOCK_GET_ELEMENT(&graph->unit_seats, unit->unit.animation.seat_index, struct animation_graph_unit_seat);
	if (unit->unit.animation.weapon_index < 0 || unit->unit.animation.weapon_index >= seat->weapon_classes.count)
		return FALSE;
	/* (its weapon readied first: the class of the one it holds) */
	if (object->weapon_index != NONE && unit->unit.current_weapon_index == NONE)
		return FALSE;
	weapon_class = TAG_BLOCK_GET_ELEMENT(&seat->weapon_classes, unit->unit.animation.weapon_index,
		struct animation_graph_weapon_class);
	snprintf(name, (size_t)size, "%s %s %s", object->crouch ? "crouch" : seat->label, weapon_class->label,
		object->animation);
	return TRUE;
}

/* the scene's animation started, looped, or its frame held */
static void object_animate(struct menu_scene_object *object, short slot, struct unit_datum *unit)
{
	long graph_index = unit->object.animation.animation_graph_index;

	if (!object->animation[0] || object->animation_given_up || graph_index == NONE)
		return;
	if (!object->animation_started)
	{
		char name[96];
		short index = NONE;

		if (animation_name(object, unit, name, sizeof(name)))
			index = animation_graph_get_animation_by_name(graph_index, name);
		if (index == NONE)
		{
			if (++object->animation_tries >= MENU_SCENE_ANIMATION_TRIES)
			{
				object->animation_given_up = TRUE;
				platform_log("menus: h3 scene object %d: no animation %s in %s", slot, object->animation,
					tag_get_name(graph_index));
			}
			return;
		}
		if (!unit_start_user_animation(object->object_index, graph_index, name, FALSE))
		{
			object->animation_given_up = TRUE;
			platform_log("menus: h3 scene object %d: the animation %s could not be played", slot, name);
			return;
		}
		object->animation_started = TRUE;
		object->animation_index = index;
		platform_log("menus: h3 scene object %d: animation %s, %s", slot, name, object->frame == NONE ? "looped" : "held");
	}
	/* (played again from the start as it ends, or its frame held: the
	tick plays on from it, one frame a tick) */
	{
		struct animation_graph *graph = animation_graph_definition_get(graph_index);
		struct animation *animation;
		short frame_count;

		if (unit->object.animation.state.index == NONE || unit->object.animation.state.index >= graph->animations.count)
			return;
		animation = TAG_BLOCK_GET_ELEMENT(&graph->animations, unit->object.animation.state.index, struct animation);
		frame_count = animation->frame_count;
		if (unit->unit.animation.state != _unit_state_user_animation ||
			(object->frame == NONE && unit->object.animation.state.frame_index + 2 >= frame_count))
		{
			char name[96];

			if (animation_name(object, unit, name, sizeof(name)))
				unit_start_user_animation(object->object_index, graph_index, name, FALSE);
		}
		if (object->frame != NONE && unit->unit.animation.state == _unit_state_user_animation)
		{
			frame_count = TAG_BLOCK_GET_ELEMENT(&graph->animations, unit->object.animation.state.index,
				struct animation)->frame_count;
			unit->object.animation.state.frame_index = (short)MIN(object->frame, frame_count > 2 ? frame_count - 2 : 0);
		}
	}
}

/* the object held where it stands (no ground under it), posed */
static boolean object_hold(struct menu_scene_object *object, short slot)
{
	struct object_datum *datum = object_try_and_get(object->object_index);
	struct unit_datum *unit = unit_try_and_get(object->object_index);
	real_point3d position;
	real_vector3d forward, up;

	if (!datum)
	{
		object->object_index = NONE;
		return FALSE;
	}
	if (object->vehicle_entry != NONE)
	{
		/* (in its seat: where its vehicle is) */
		forward = datum->object.forward;
		up = datum->object.up;
		if (datum->object.parent_object_index != NONE && object_try_and_get(datum->object.parent_object_index))
		{
			forward = object_get(datum->object.parent_object_index)->object.forward;
			up = object_get(datum->object.parent_object_index)->object.up;
		}
	}
	else
	{
		object_place(object, &position, &forward, &up);
		/* (put back only when it has moved: a flying biped stays where it is,
		and placing an object finds its clusters again, each time) */
		if (distance_squared3d(&datum->object.position, &position) > 0.0001f ||
			dot_product3d(&datum->object.forward, &forward) < 0.9999f ||
			dot_product3d(&datum->object.up, &up) < 0.9999f)
		{
			object_set_position(object->object_index, &position, &forward, &up);
		}
		datum->object.translational_velocity.i = datum->object.translational_velocity.j =
			datum->object.translational_velocity.k = 0.f;
		datum->object.angular_velocity.i = datum->object.angular_velocity.j = datum->object.angular_velocity.k = 0.f;
	}
	if (unit)
	{
		object_pose(object, unit, &forward, &up);
		object_animate(object, slot, unit);
	}
	/* (another colour: made again in it, by the update right after this;
	an object's colours are worked out as it is made) */
	return color_index_of(object) == object->made_color_index;
}

/* the scene's objects let go, the last made first (who sits in a vehicle
before it) */
static void scene_forget(boolean delete_them)
{
	short index;

	for (index = MAXIMUM_MENU_SCENE_OBJECTS - 1; index >= 0; index--)
	{
		struct menu_scene_object *object = &menu_scene_objects.slots[index];

		if (object->object_index != NONE && delete_them)
			platform_log("menus: h3 scene object %d: deleted", index);
		object_forget(object, delete_them);
	}
	if (menu_scene_objects.made)
	{
		platform_log("menus: h3 scene objects: let go after %.1f s (at most %d effects and %d particles meanwhile)",
			(double)(vita_host_time_us() - menu_scene_objects.made_at) / 1000000.0, menu_scene_objects.most_effects,
			menu_scene_objects.most_particles);
	}
	menu_scene_objects.made = FALSE;
}

/* whether the packs the scene (or the preview: preview TRUE) names have
their textures in, or have waited long enough (since: when the wait began,
0 before) */
static boolean packs_textures_wait(unsigned long long *since, boolean preview)
{
	unsigned long long now = vita_host_time_us();
	boolean ready = TRUE;
	short index;

	if (!*since)
		*since = now;
	for (index = preview ? MENU_SCENE_PREVIEW : 0; index < (preview ? MENU_SCENE_PREVIEW + 1 : MAXIMUM_MENU_SCENE_OBJECTS);
		index++)
	{
		struct menu_scene_object const *object = &menu_scene_objects.slots[index];

		if (!object->wanted || !object->pack[0])
			continue;
		if (!scene_packs_textures_ready(object->pack))
			ready = FALSE;
		if (object->weapon_pack[0] && !scene_packs_textures_ready(object->weapon_pack))
			ready = FALSE;
	}
	if (ready || now - *since >= MENU_SCENE_TEXTURES_WAIT_US)
	{
		platform_log("menus: h3 scene %s: %s textures %s after %.1f ms", preview ? "object 6" : "objects",
			preview ? menu_scene_objects.slots[MENU_SCENE_PREVIEW].pack : menu_scene_objects.screen,
			ready ? "in" : "still loading", (double)(now - *since) / 1000.0);
		*since = 0;
		return TRUE;
	}
	return FALSE;
}

/* the scene made: every pack it names loaded (FALSE: not yet, or never,
refused) */
static void scene_make(void)
{
	short index;
	short state = _scene_pack_state_loaded;
	unsigned long long started;

	for (index = 0; index < MAXIMUM_MENU_SCENE_OBJECTS; index++)
	{
		struct menu_scene_object *object = &menu_scene_objects.slots[index];
		short pack_state, weapon_state;

		if (!object->wanted)
			continue;
		pack_state = scene_packs_want(object->pack);
		weapon_state = object->weapon_tag[0] ? scene_packs_want(object->weapon_pack) : _scene_pack_state_loaded;
		if (pack_state == _scene_pack_state_failed || pack_state == _scene_pack_state_none ||
			weapon_state == _scene_pack_state_failed || weapon_state == _scene_pack_state_none)
		{
			char const *pack = (pack_state == _scene_pack_state_failed || pack_state == _scene_pack_state_none) ?
				object->pack : object->weapon_pack;

			menu_scene_objects.refused = TRUE;
			platform_log("menus: h3 scene objects: %s: the pack %s is %s: no scene", menu_scene_objects.screen, pack,
				scene_packs_state_name(pack == object->pack ? pack_state : weapon_state));
			return;
		}
		if (pack_state != _scene_pack_state_loaded || weapon_state != _scene_pack_state_loaded)
			state = pack_state != _scene_pack_state_loaded ? pack_state : weapon_state;
	}
	if (state != _scene_pack_state_loaded)
	{
		menu_scene_objects.scene_textures_since = 0;
		return;
	}
	/* (their textures first: read meanwhile, none waited for) */
	if (!packs_textures_wait(&menu_scene_objects.scene_textures_since, FALSE))
		return;
	/* (all at once, the vehicles before who sits in them: the entries'
	order) */
	started = vita_host_time_us();
	for (index = 0; index < MAXIMUM_MENU_SCENE_OBJECTS; index++)
	{
		struct menu_scene_object *object = &menu_scene_objects.slots[index];

		if (object->wanted && !object_make(object, index))
		{
			menu_scene_objects.refused = TRUE;
			scene_forget(TRUE);
			platform_log("menus: h3 scene objects: %s: no scene", menu_scene_objects.screen);
			return;
		}
	}
	menu_scene_objects.made = TRUE;
	menu_scene_objects.made_at = vita_host_time_us();
	menu_scene_objects.most_effects = 0;
	menu_scene_objects.most_particles = 0;
	platform_log("menus: h3 scene objects: %s: made in %.1f ms", menu_scene_objects.screen,
		(double)(menu_scene_objects.made_at - started) / 1000.0);
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
			if (!real_in_range(values[index], 5000.f))
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

/* a pack the preview let go (a player model's): released, unless it is the
menu Spartan's or a scene's (scene_packs_release: once its object is gone,
its tag cache room given back - the next model's has room) */
static void preview_pack_release(char const *pack)
{
	short index;

	if (!pack || !*pack || !strcmp(pack, MENU_SCENE_PREVIEW_PACK))
		return;
	for (index = 0; index < MAXIMUM_MENU_SCENE_OBJECTS; index++)
	{
		struct menu_scene_object const *object = &menu_scene_objects.slots[index];

		if (object->wanted && (!strcmp(object->pack, pack) || !strcmp(object->weapon_pack, pack)))
			return;
	}
	scene_packs_release(pack);
	platform_log("menus: h3 scene object %d: the pack %s let go", MENU_SCENE_PREVIEW, pack);
}

/* the model asked for taken, once it has rested: the character standing
there let go (and its pack), the other made once its pack is loaded */
static void preview_model_take(boolean at_once)
{
	struct menu_scene_object *object = &menu_scene_objects.slots[MENU_SCENE_PREVIEW];
	char const *wanted = menu_scene_objects.pending_pack[0] ? menu_scene_objects.pending_pack : MENU_SCENE_PREVIEW_PACK;

	if (!strcmp(menu_scene_objects.preview_pack, wanted) && !strcmp(object->pack, wanted))
		return;
	if (!at_once && vita_host_time_us() - menu_scene_objects.pending_since < MENU_SCENE_MODEL_REST_US)
		return;
	snprintf(menu_scene_objects.preview_pack, sizeof(menu_scene_objects.preview_pack), "%s", wanted);
	if (strcmp(object->pack, wanted))
	{
		char previous[32];

		snprintf(previous, sizeof(previous), "%s", object->pack);
		snprintf(object->pack, sizeof(object->pack), "%s", wanted);
		object->refused = FALSE;
		if (object->object_index != NONE)
		{
			platform_log("menus: h3 scene object %d: deleted (the model changed)", MENU_SCENE_PREVIEW);
			object_forget(object, TRUE);
		}
		preview_pack_release(previous);
	}
}

/* the preview's character: made (once its pack is), held, recoloured */
static void preview_update(void)
{
	struct menu_scene_object *object = &menu_scene_objects.slots[MENU_SCENE_PREVIEW];

	/* (the model asked for, once it has rested; hidden, at once) */
	preview_model_take(!object->wanted);

	if (!object->wanted || !halo_main_menu_idle() || !halo_menu_style_halo3())
	{
		if (object->object_index != NONE)
			platform_log("menus: h3 scene object %d: deleted", MENU_SCENE_PREVIEW);
		object_forget(object, TRUE);
		if (!halo_main_menu_idle())
			object->wanted = FALSE;
		return;
	}
	if (object->object_index != NONE && !object_hold(object, MENU_SCENE_PREVIEW))
	{
		object_forget(object, TRUE);
		platform_log("menus: h3 scene object: recoloured %d", color_index_of(object));
	}
	if (object->object_index == NONE && !object->refused)
	{
		short state = scene_packs_want(object->pack);
		long roots[1] = { NONE };

		if (state == _scene_pack_state_failed || state == _scene_pack_state_none)
		{
			object->refused = TRUE;
			platform_log("menus: h3 scene object %d: the pack %s is %s: no character", MENU_SCENE_PREVIEW, object->pack,
				scene_packs_state_name(state));
		}
		else if (state == _scene_pack_state_loaded && packs_textures_wait(&menu_scene_objects.preview_textures_since, TRUE))
		{
			if (!scene_packs_roots(object->pack, roots, 1) || roots[0] == NONE || tag_get_group_tag(roots[0]) != 'bipd')
			{
				object->refused = TRUE;
				platform_log("menus: h3 scene object %d: the pack %s's first tag is not a biped: no character",
					MENU_SCENE_PREVIEW, object->pack);
			}
			else if (!object_make(object, MENU_SCENE_PREVIEW))
			{
				object_forget(object, TRUE);
				object->refused = TRUE;
			}
		}
	}
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
	struct menu_scene_camera camera;
	struct menu_scene_object parsed[MAXIMUM_MENU_SCENE_OBJECTS];
	char text[MENU_SCENE_TEXT_SIZE];
	char *cursor, *end;
	short count = 0;
	short index;
	boolean same;

	initialize();
	camera.position.x = x;
	camera.position.y = y;
	camera.position.z = z;
	camera.yaw = yaw;
	camera.pitch = pitch;
	menu_scene_objects.has_camera = has_camera != 0;
	if (has_camera)
		menu_scene_objects.camera = camera;
	snprintf(menu_scene_objects.screen, sizeof(menu_scene_objects.screen), "%s", screen ? screen : "");
	/* (the same scene asked for again in front of the same camera: kept as
	it stands) */
	same = has_camera && objects && !strcmp(objects, menu_scene_objects.text) &&
		menu_scene_objects.slots[0].camera.position.x == camera.position.x &&
		menu_scene_objects.slots[0].camera.position.y == camera.position.y &&
		menu_scene_objects.slots[0].camera.position.z == camera.position.z &&
		menu_scene_objects.slots[0].camera.yaw == camera.yaw && menu_scene_objects.slots[0].camera.pitch == camera.pitch;
	if (same)
	{
		for (index = 0; index < MAXIMUM_MENU_SCENE_OBJECTS; index++)
			menu_scene_objects.slots[index].wanted = menu_scene_objects.slots[index].pack[0] != 0;
		return;
	}
	for (index = 0; index < MAXIMUM_MENU_SCENE_OBJECTS; index++)
		menu_scene_objects.slots[index].wanted = FALSE;
	snprintf(menu_scene_objects.text, sizeof(menu_scene_objects.text), "%s", objects && has_camera ? objects : "");
	menu_scene_objects.refused = FALSE;
	if (!objects || !has_camera)
		return;
	if (strlen(objects) >= sizeof(text))
	{
		platform_log("menus: h3 scene objects: %s: scene_object= is too long: no scene", menu_scene_objects.screen);
		return;
	}
	snprintf(text, sizeof(text), "%s", objects);
	for (cursor = text; cursor && *cursor; cursor = end)
	{
		end = strchr(cursor, ';');
		if (end)
			*end++ = 0;
		if (strspn(cursor, " \t\r\n") == strlen(cursor))
			continue;
		memset(&parsed[count], 0, sizeof(parsed[count]));
		if (count >= MAXIMUM_MENU_SCENE_OBJECTS || !entry_parse(cursor, &parsed[count], count))
		{
			platform_log("menus: h3 scene objects: %s: entry %d is not \"<pack>[:<tag>] <distance> <right> <up> "
				"<facing> [<option>=<value> ...]\" (or there are more than %d): no scene", menu_scene_objects.screen,
				count + 1, MAXIMUM_MENU_SCENE_OBJECTS);
			return;
		}
		count++;
	}
	/* (what it looks at is one of its entries) */
	for (index = 0; index < count; index++)
	{
		if (parsed[index].look_entry != NONE && parsed[index].look_entry >= count)
		{
			platform_log("menus: h3 scene objects: %s: entry %d looks at no entry: no scene", menu_scene_objects.screen,
				index + 1);
			return;
		}
	}
	/* (another scene: the one standing let go, by the update) */
	for (index = 0; index < count; index++)
	{
		struct menu_scene_object *object = &menu_scene_objects.slots[index];

		snprintf(object->pack, sizeof(object->pack), "%s", parsed[index].pack);
		snprintf(object->tag, sizeof(object->tag), "%s", parsed[index].tag);
		object->distance = parsed[index].distance;
		object->right = parsed[index].right;
		object->up = parsed[index].up;
		object->facing = parsed[index].facing;
		object->pitch = parsed[index].pitch;
		object->roll = parsed[index].roll;
		object->color_index = parsed[index].color_index;
		snprintf(object->weapon_pack, sizeof(object->weapon_pack), "%s", parsed[index].weapon_pack);
		snprintf(object->weapon_tag, sizeof(object->weapon_tag), "%s", parsed[index].weapon_tag);
		snprintf(object->animation, sizeof(object->animation), "%s", parsed[index].animation);
		object->crouch = parsed[index].crouch;
		object->frame = parsed[index].frame;
		object->vehicle_entry = parsed[index].vehicle_entry;
		snprintf(object->seat, sizeof(object->seat), "%s", parsed[index].seat);
		object->look_entry = parsed[index].look_entry;
		object->suspension = parsed[index].suspension;
		object->camera = camera;
		object->scale = 1.f;
		object->wanted = TRUE;
	}
	for (index = count; index < MAXIMUM_MENU_SCENE_OBJECTS; index++)
		menu_scene_objects.slots[index].pack[0] = 0;
	/* (made again from the start, another description or another camera:
	the update lets the objects standing go first) */
	menu_scene_objects.replaced = TRUE;
	menu_scene_objects.scene_textures_since = 0;
}

static void menu_scene_objects_update_internal(void);

void menu_scene_objects_update(
	void)
{
	unsigned long long started = vita_host_time_us();
	unsigned long long spent;

	menu_scene_objects_update_internal();
	spent = vita_host_time_us() - started;
	/* (the game thread's time making, holding and letting go the scenes'
	objects, logged once it is a frame's hitch) */
	if (spent >= 16000ull)
		platform_log("scene packs: game thread: the menus' scene objects took %.1f ms", (double)spent / 1000.0);
}

static void menu_scene_objects_update_internal(
	void)
{
	short index;
	boolean wanted = FALSE;
	boolean stale = FALSE;

	initialize();
	for (index = 0; index < (short)NUMBEROF(menu_scene_objects.released); index++)
	{
		if (menu_scene_objects.released_updates[index] > 0)
			menu_scene_objects.released_updates[index]--;
	}
	for (index = 0; index < NUMBER_OF_MENU_SCENE_SLOTS; index++)
	{
		struct menu_scene_object *object = &menu_scene_objects.slots[index];

		/* (another map: its objects went with the last one) */
		if (object->object_index != NONE && object->epoch != scene_packs_map_epoch())
		{
			object->object_index = NONE;
			object->weapon_index = NONE;
			stale = index < MAXIMUM_MENU_SCENE_OBJECTS;
		}
		if (index < MAXIMUM_MENU_SCENE_OBJECTS)
			wanted |= object->wanted;
	}
	/* (another scene asked for: the one standing let go first) */
	if (stale || menu_scene_objects.replaced)
		scene_forget(TRUE);
	menu_scene_objects.replaced = FALSE;
	if (!wanted || !halo_main_menu_idle() || !halo_menu_style_halo3())
	{
		scene_forget(TRUE);
		if (!halo_main_menu_idle())
		{
			for (index = 0; index < MAXIMUM_MENU_SCENE_OBJECTS; index++)
				menu_scene_objects.slots[index].wanted = FALSE;
			menu_scene_objects.text[0] = 0;
		}
	}
	else if (menu_scene_objects.made)
	{
		boolean kept = TRUE;

		/* (held; or, one of them gone or in another colour, made again
		whole, at once: no frame without it) */
		for (index = 0; index < MAXIMUM_MENU_SCENE_OBJECTS; index++)
		{
			struct menu_scene_object *object = &menu_scene_objects.slots[index];

			if (object->wanted && (!slot_object_valid(object) || !object_hold(object, index)))
				kept = FALSE;
		}
		if (!kept)
		{
			platform_log("menus: h3 scene objects: %s: made again (an object gone or recoloured)",
				menu_scene_objects.screen);
			scene_forget(TRUE);
			scene_make();
		}
		else
		{
			if (effect_data && effect_data->actual_count > menu_scene_objects.most_effects)
				menu_scene_objects.most_effects = effect_data->actual_count;
			if (particle_data && particle_data->actual_count > menu_scene_objects.most_particles)
				menu_scene_objects.most_particles = particle_data->actual_count;
		}
	}
	else if (!menu_scene_objects.refused)
		scene_make();
	preview_update();
	/* (object_lights.c's quick way out: none made, none asked about) */
	menu_scene_objects.made_count = 0;
	for (index = 0; index < NUMBER_OF_MENU_SCENE_SLOTS; index++)
	{
		if (menu_scene_objects.slots[index].object_index != NONE)
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
	snprintf(object->pack, sizeof(object->pack), "%s", menu_scene_objects.preview_pack[0] ?
		menu_scene_objects.preview_pack : MENU_SCENE_PREVIEW_PACK);
	object->tag[0] = 0;
	object->pitch = object->roll = 0.f;
	object->weapon_pack[0] = object->weapon_tag[0] = 0;
	object->animation[0] = 0;
	object->vehicle_entry = object->look_entry = NONE;
	object->color_index = color_index >= 0 && color_index < NUMBER_OF_MENU_SCENE_COLORS ? color_index :
		MENU_SCENE_COLOR_PROFILE;
	object->refused = FALSE;
	object->wanted = TRUE;
	return TRUE;
}

void menu_scene_spartan_model(
	char const *pack)
{
	struct menu_scene_object *object;
	char const *wanted = pack && *pack ? pack : MENU_SCENE_PREVIEW_PACK;

	initialize();
	object = &menu_scene_objects.slots[MENU_SCENE_PREVIEW];
	/* (taken by the update once it has rested: preview_model_take; the
	first, nothing standing yet and none asked for before, at once) */
	if (strcmp(menu_scene_objects.pending_pack, wanted))
	{
		snprintf(menu_scene_objects.pending_pack, sizeof(menu_scene_objects.pending_pack), "%s", wanted);
		menu_scene_objects.pending_since = vita_host_time_us();
	}
	if (!menu_scene_objects.preview_pack[0] && !object->pack[0])
	{
		snprintf(menu_scene_objects.preview_pack, sizeof(menu_scene_objects.preview_pack), "%s", wanted);
		snprintf(object->pack, sizeof(object->pack), "%s", wanted);
	}
}

short menu_scene_spartan_state(
	long *progress,
	char *why,
	long why_size)
{
	struct menu_scene_object const *object;

	initialize();
	object = &menu_scene_objects.slots[MENU_SCENE_PREVIEW];
	/* (the model asked for: not wanted yet while it rests) */
	return scene_packs_status(menu_scene_objects.pending_pack[0] ? menu_scene_objects.pending_pack :
		object->pack[0] ? object->pack : MENU_SCENE_PREVIEW_PACK, progress, why, why_size);
}

void menu_scene_spartan_color(
	short color_index)
{
	initialize();
	menu_scene_objects.slots[MENU_SCENE_PREVIEW].color_index =
		color_index >= 0 && color_index < NUMBER_OF_MENU_SCENE_COLORS ? color_index : MENU_SCENE_COLOR_PROFILE;
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

	return menu_scene_objects_initialized && object->wanted && object->object_index != NONE;
}

/* the slot that made this object (NONE: none) */
static short slot_of(long object_index)
{
	short index;

	if (!menu_scene_objects_initialized || menu_scene_objects.made_count <= 0 || object_index == NONE)
		return NONE;
	for (index = 0; index < NUMBER_OF_MENU_SCENE_SLOTS; index++)
	{
		struct menu_scene_object const *object = &menu_scene_objects.slots[index];

		if (object->object_index == object_index && object->epoch == scene_packs_map_epoch())
			return index;
	}
	return NONE;
}

boolean menu_scene_objects_posed(
	long unit_index)
{
	return slot_of(unit_index) != NONE;
}

boolean menu_scene_objects_frozen(
	long object_index,
	real *suspension)
{
	short slot = slot_of(object_index);
	short index;

	if (slot == NONE)
	{
		/* (let go, not yet deleted) */
		for (index = 0; menu_scene_objects_initialized && index < (short)NUMBEROF(menu_scene_objects.released); index++)
		{
			if (menu_scene_objects.released_updates[index] > 0 && menu_scene_objects.released[index] == object_index)
			{
				if (suspension)
					*suspension = 0.f;
				return TRUE;
			}
		}
		return FALSE;
	}
	if (suspension)
		*suspension = menu_scene_objects.slots[slot].suspension;
	return TRUE;
}

boolean menu_scene_objects_lighting(
	long object_index,
	struct render_lighting *lighting)
{
	short depth;

	if (!menu_scene_objects_initialized || menu_scene_objects.made_count <= 0 || object_index == NONE)
		return FALSE;
	/* (the object, what it holds, who sits in it and what they hold: up its
	parents) */
	for (depth = 0; depth < 4 && object_index != NONE; depth++)
	{
		short slot = slot_of(object_index);
		struct object_datum *datum;

		if (slot != NONE)
		{
			*lighting = menu_scene_objects.slots[slot].lighting;
			return TRUE;
		}
		datum = object_try_and_get(object_index);
		object_index = datum ? datum->object.parent_object_index : NONE;
	}
	return FALSE;
}
