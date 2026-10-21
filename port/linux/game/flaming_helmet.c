/*
FLAMING_HELMET.C

The tester's flaming helmet drawn: a blue flame rising from the head of each
player whose flame this machine checked (port/linux/src/p2p_flame.inc: the
owner's signed tester list and the wearer's own proof; nothing of the
host's word), and of this machine's own first player while it wears it
(player.flaming_helmet, listed). Never in the wearer's own first person
view (the particles are the local player's, not drawn first person: render
particles), in third person (a vehicle, dead), for everyone else, and on
CUSTOMIZATION's Spartan (flaming_helmet_preview).

No art of the project's: the stock multiplayer maps' own Covenant smoke
particle (FLAME_PARTICLE, in every one of them; grey, made to be tinted:
their fire's is orange and cannot be made blue), tinted blue,
emitted from one point a little behind and above the head marker ("head",
else the object's centre raised), billowing up behind and above it: six
particles a tick (three of the Covenant smoke, blue pulled toward violet,
large and slow; three of its white smoke tinted lavender, smaller and
rising fast), 180 a second a wearer, alive for the tags' own short life,
under the particle budget like any effect's; moving with the unit as they
leave it. A
map without it draws nothing. Each machine draws its own (no network
traffic, nothing deterministic: the local random seed).

(debug, not in release builds) HALO_TEST_FLAME_LOG=1 logs once a second
whose flame is drawn, for the tests.
*/

#include "cseries.h"
#include "real_math.h"
#include "objects/objects.h"
#include "units/units.h"
#include "game/players.h"
#include "effects/particles.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "render_epoch.h"

#include "../src/p2p_friends.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);
unsigned long system_milliseconds(void);

#define FLAME_PARTICLE "effects\\particles\\energy\\smoke c generic"
#define FLAME_TONGUE_PARTICLE "effects\\particles\\energy\\smoke white c generic"
#define FLAME_PARTICLE_FALLBACK "effects\\particles\\energy\\flash c generic"
/* the flame's particles a tick, their size, how they rise (world units a
second), how far from the head they start */
#define FLAME_PARTICLES_PER_TICK 3
#define FLAME_RADIUS_LOWER 0.09f
#define FLAME_RADIUS_UPPER 0.15f
#define FLAME_RISE_LOWER 0.08f
#define FLAME_RISE_UPPER 0.8f
#define FLAME_SPREAD 0.1f
#define FLAME_TONGUE_RISE_LOWER 1.2f
#define FLAME_TONGUE_RISE_UPPER 2.0f
/* above the head marker (its origin sits inside the helmet) */
#define FLAME_HEIGHT (-0.01f)
/* behind the head (world units against its facing): the face stays clear */
#define FLAME_BEHIND 0.07f

static struct
{
	unsigned long map_generation;
	long particle;
	/* (the tongues': the white smoke, tinted lavender) */
	long tongue_particle;
	boolean looked_up;
	unsigned long logged_time;
	/* (the preview's unit: CUSTOMIZATION's Spartan, flaming_helmet_preview) */
	long preview_object;
	/* (this second's answers: this machine wears it; each machine's checked,
	-1 not asked yet) */
	int refresh_ticks;
	boolean wearing;
	signed char verified[256];
} flaming_helmet = { 0, NONE, NONE, FALSE, 0, NONE };

/* the colour: the body's Covenant smoke (blue of its own) pulled toward
violet, the tongues' white smoke lavender: blue at the core, blue-violet to
lavender at the tips (the bitmaps' colours multiplied by these) */
static void flame_color(real_argb_color *color, unsigned long *seed, boolean tongue)
{
	real mix = real_seed_random_range(seed, 0.0f, 1.0f);

	color->alpha = 1.0f;
	/* (the body blue-violet, a brighter blue core now and then; the tongues
	lavender at the tips) */
	color->rgb.red = tongue ? 0.5f + 0.15f * mix : 0.7f + 0.25f * mix;
	color->rgb.green = tongue ? 0.38f + 0.12f * mix : 0.3f + 0.3f * mix * mix;
	color->rgb.blue = 1.0f;
}

static long flame_particle(void)
{
	if (!flaming_helmet.looked_up || flaming_helmet.map_generation != halo_map_generation)
	{
		flaming_helmet.looked_up = TRUE;
		flaming_helmet.map_generation = halo_map_generation;
		flaming_helmet.particle = tag_loaded(PARTICLE_TAG, FLAME_PARTICLE);
#ifndef HALO_RELEASE
		/* (debug: another particle tried, HALO_TEST_FLAME_PARTICLE) */
		if (getenv("HALO_TEST_FLAME_PARTICLE"))
			flaming_helmet.particle = tag_loaded(PARTICLE_TAG, getenv("HALO_TEST_FLAME_PARTICLE"));
#endif
		/* (the menus' map has none: the Covenant flash, also made to be tinted) */
		if (flaming_helmet.particle == NONE)
			flaming_helmet.particle = tag_loaded(PARTICLE_TAG, FLAME_PARTICLE_FALLBACK);
		flaming_helmet.tongue_particle = tag_loaded(PARTICLE_TAG, FLAME_TONGUE_PARTICLE);
		if (flaming_helmet.tongue_particle == NONE)
			flaming_helmet.tongue_particle = flaming_helmet.particle;
		platform_log("flame: the flame's particle %s in this map", flaming_helmet.particle == NONE ? "not" : "found");
	}
	return flaming_helmet.particle;
}

/* where the flame starts: the head marker's position, else the object's
centre raised */
static boolean flame_head(long object_index, real_point3d *position)
{
	struct object_marker marker;
	struct object_datum *object;

	if (object_get_marker_by_name(object_index, "head", &marker, 1))
	{
		*position = marker.matrix.position;
		position->z += FLAME_HEIGHT;
		return TRUE;
	}
	object = object_get(object_index);
	if (!object)
		return FALSE;
	*position = object->object.bounding_sphere_center;
	position->z += object->object.bounding_sphere_radius * 0.75f;
	return TRUE;
}

/* the flame on an object, for a tick: local_player_index the wearer's own
(its first person view does not draw it), NONE another's */
static void flame_emit(long object_index, short local_player_index)
{
	unsigned long *seed = get_global_local_random_seed_address();
	real_vector3d velocity, angular;
	real_point3d head;
	long particle = flame_particle();
	int index;

	if (particle == NONE || !flame_head(object_index, &head))
		return;
	/* (one emitter, a little behind the head: the flames billow up behind
	and above it, wider than the helmet, the visor clear) */
	{
		struct object_datum *object = object_get(object_index);

		head.x -= object->object.forward.i * FLAME_BEHIND;
		head.y -= object->object.forward.j * FLAME_BEHIND;
	}
	object_get_velocities(object_index, &velocity, &angular);
	for (index = 0; index < 2 * FLAME_PARTICLES_PER_TICK; index++)
	{
		struct new_particle_data data;
		/* (half of them tongues: smaller, paler, rising faster) */
		boolean tongue = index >= FLAME_PARTICLES_PER_TICK;

		memset(&data, 0, sizeof(data));
		data.definition_index = tongue ? flaming_helmet.tongue_particle : particle;
		data.object_index = NONE;
		data.node_index = NONE;
		data.local_player_index = local_player_index;
		data.attached_to_local_player = FALSE;
		data.dont_draw_first_person = local_player_index != NONE;
		data.dont_draw_third_person = FALSE;
		data.position.x = head.x + real_seed_random_range(seed, -FLAME_SPREAD, FLAME_SPREAD);
		data.position.y = head.y + real_seed_random_range(seed, -FLAME_SPREAD, FLAME_SPREAD);
		data.position.z = head.z + real_seed_random_range(seed, 0.0f, FLAME_SPREAD * 0.6f);
		data.direction.i = 0.0f;
		data.direction.j = 0.0f;
		data.direction.k = 1.0f;
		/* (the unit's own motion, a tick's worth in world units a tick, as
		effects.c gives its particles, and the rise) */
		data.velocity.i = velocity.i * 30.0f;
		data.velocity.j = velocity.j * 30.0f;
		data.velocity.k = velocity.k * 30.0f + (tongue ? real_seed_random_range(seed, FLAME_TONGUE_RISE_LOWER,
			FLAME_TONGUE_RISE_UPPER) : real_seed_random_range(seed, FLAME_RISE_LOWER, FLAME_RISE_UPPER));
		data.initial_impulse = *global_zero_vector3d;
		data.rotation = real_seed_random_range(seed, 0.0f, 2.0f * _pi);
		data.angular_velocity = real_seed_random_range(seed, -3.0f, 3.0f);
		data.radius = tongue ? real_seed_random_range(seed, FLAME_RADIUS_LOWER * 0.7f, FLAME_RADIUS_LOWER) :
			real_seed_random_range(seed, FLAME_RADIUS_LOWER, FLAME_RADIUS_UPPER);
		flame_color(&data.color, seed, tongue);
		particle_new(&data);
	}
}

static boolean flame_verified(int machine)
{
	if (machine < 0 || machine >= (int)sizeof(flaming_helmet.verified))
		return FALSE;
	if (flaming_helmet.verified[machine] < 0)
		flaming_helmet.verified[machine] = (signed char)(p2p_flame_verified(machine) != 0);
	return flaming_helmet.verified[machine] != 0;
}

/* (game_time.c, each tick after the game's) every flame shown */
void flaming_helmet_tick(void)
{
#ifndef HALO_DEDICATED_SERVER
	boolean wearing;
	int drawn = 0;
	long player_index;
	boolean log_now = FALSE;

#ifndef HALO_RELEASE
	{
		static int test_log = -1;
		unsigned long now = system_milliseconds();

		if (test_log < 0)
			test_log = getenv("HALO_TEST_FLAME_LOG") && getenv("HALO_TEST_FLAME_LOG")[0] == '1';
		if (test_log && now - flaming_helmet.logged_time >= 1000)
		{
			flaming_helmet.logged_time = now;
			log_now = TRUE;
		}
	}
#endif
	if (!player_data)
		return;
	/* (what the checks say, asked once a second at most: their lock is the
	friends', which internet play's thread holds while it checks signatures) */
	if (flaming_helmet.refresh_ticks-- <= 0)
	{
		flaming_helmet.refresh_ticks = 30;
		flaming_helmet.wearing = (boolean)p2p_flame_wearing();
		memset(flaming_helmet.verified, -1, sizeof(flaming_helmet.verified));
	}
	wearing = flaming_helmet.wearing;
	for (player_index = data_next_index(player_data, NONE); player_index != NONE;
		player_index = data_next_index(player_data, player_index))
	{
		struct player_datum *player = player_get(player_index);
		struct network_player *network = &player->network_player_data;
		short local_player_index = NONE;

		if (player->unit_index == NONE)
			continue;
		if (player->local_player_index == 0)
		{
			if (!wearing)
				continue;
			local_player_index = 0;
		}
		else if (player->local_player_index != NONE || network->icon_index != NETWORK_PLAYER_FLAME_CLAIM ||
			!flame_verified(network->machine_index))
		{
			continue;
		}
		flame_emit(player->unit_index, local_player_index);
		drawn++;
		if (log_now)
			platform_log("flame: drawn on player %ld (machine %d%s)", player_index & 0xFFFF,
				(int)network->machine_index, local_player_index != NONE ? ", this machine's own" : "");
	}
	if (flaming_helmet.preview_object != NONE)
	{
		if (object_try_and_get_and_verify_type(flaming_helmet.preview_object, _object_mask_all))
			flame_emit(flaming_helmet.preview_object, NONE);
		else
			flaming_helmet.preview_object = NONE;
	}
	(void)drawn;
#endif
}

/* (main.c's test command @flamecam, not in release builds' tests alone:
harmless) the camera that looks at the first local player's head from a
distance and a side (degrees from its facing): "x y z yaw pitch" for
main_test_camera, TRUE; FALSE with no such player */
boolean flaming_helmet_test_camera(char *line, int size, float distance, float angle)
{
	long player_index;

	if (!player_data)
		return FALSE;
	for (player_index = data_next_index(player_data, NONE); player_index != NONE;
		player_index = data_next_index(player_data, player_index))
	{
		struct player_datum *player = player_get(player_index);
		struct object_datum *object;
		real_point3d head;
		real facing, yaw, cosine_value, sine_value;

		if (player->local_player_index != 0 || player->unit_index == NONE || !flame_head(player->unit_index, &head))
			continue;
		object = object_get(player->unit_index);
		facing = atan2f(object->object.forward.j, object->object.forward.i);
		yaw = facing + angle * _pi / 180.0f;
		cosine_value = cosf(yaw);
		sine_value = sinf(yaw);
		/* (the camera out along yaw, a little above the head, looking back
		at it and slightly down) */
		snprintf(line, (size_t)size, "%f %f %f %f %f", head.x + distance * cosine_value,
			head.y + distance * sine_value, head.z + 0.12f, (yaw + _pi) * 180.0f / _pi, -6.0f);
		return TRUE;
	}
	return FALSE;
}

/* (menu_scene.c) CUSTOMIZATION's Spartan, while the preview shows it and
this machine wears the flame: its object (NONE, none) */
void flaming_helmet_preview(long object_index)
{
	flaming_helmet.preview_object = object_index;
}
