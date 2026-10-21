/*
FLAMING_HELMET.C

The tester's flaming helmet drawn: a blue flame rising from the head of each
player whose flame this machine checked (port/linux/src/p2p_flame.inc: the
owner's signed tester list and the wearer's own proof; nothing of the
host's word), and of this machine's own first player while it wears it
(player.flaming_helmet, listed). Never in the wearer's own first person
view (the particles are the local player's, not drawn first person: render
particles), in third person (a vehicle, dead), for everyone else, and on
CUSTOMIZATION's Spartan (flaming_helmet_preview). In every game: online,
System Link, ad hoc, co-op, and offline (split screen, bots, Forge, the
campaign), where a listed tester's own needs no proof (their own screen);
in split screen player 1's alone (the identity is the machine's, not a
profile's). Cutscenes: drawn on the player's biped when the cutscene shows
it; never on one a script hid or took off the map (a stand-in in its place).

No art of the project's: the stock multiplayer maps' own Covenant smoke
particle (FLAME_PARTICLE, in every one of them; grey, made to be tinted:
their fire's is orange and cannot be made blue), tinted blue,
emitted from one point in the head marker's own frame ("head": it turns
as the head turns and aims; else the object's centre raised): its base on
the helmet's top, over its middle a little toward the visor, rising up: six
particles a tick (three of the Covenant smoke, blue pulled toward violet,
large and slow; three of its white smoke tinted lavender, smaller and
rising fast), 180 a second a wearer, alive for the tags' own short life,
under the particle budget like any effect's; moving with the unit as they
leave it. A
map without it draws nothing. CUSTOMIZATION's preview (the main menu,
ui.map, the scene pack's Spartan): no white smoke there, so the tongues are
the grey smoke still, tinted lavender, smaller, one a tick; and no player, so
its particles skip the players' potentially visible set
(particles_new_ignore_players_pvs: every one was refused as unseen and the
preview showed no flame at all). Each machine draws its own (no network
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
#define FLAME_TONGUE_PARTICLE_FALLBACK "effects\\particles\\air\\smoke h generic still"
/* the flame's particles a tick, their size, how they rise (world units a
second), how far from the head they start */
#define FLAME_PARTICLES_PER_TICK 3
#define FLAME_RADIUS_LOWER 0.07f
#define FLAME_RADIUS_UPPER 0.12f
#define FLAME_RISE_LOWER 0.08f
#define FLAME_RISE_UPPER 0.8f
#define FLAME_SPREAD 0.07f
#define FLAME_TONGUE_RISE_LOWER 1.2f
#define FLAME_TONGUE_RISE_UPPER 2.0f
/* above the head marker (its origin sits inside the helmet) */
/* the emitter in the head marker's frame (world units): up to the helmet's
top, forward toward the visor (over the helmet, not in front of the visor),
and its spread front to back */
#define FLAME_HEIGHT 0.0f
#define FLAME_FORWARD 0.03f
#define FLAME_SPREAD_FORWARD 0.035f

static struct
{
	unsigned long map_generation;
	long particle;
	/* (the tongues': the white smoke, tinted lavender) */
	long tongue_particle;
	real tongue_radius_scale;
	int tongues_per_tick;
	boolean looked_up;
	unsigned long logged_time;
	/* (the preview's unit: CUSTOMIZATION's Spartan, flaming_helmet_preview) */
	long preview_object;
	/* (this second's answers: this machine wears it; each machine's checked,
	-1 not asked yet) */
	int refresh_ticks;
	boolean wearing;
	signed char verified[256];
} flaming_helmet = { 0, NONE, NONE, 1.0f, FLAME_PARTICLES_PER_TICK, FALSE, 0, NONE };

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
		/* (a map without it: the Covenant flash, also made to be tinted) */
		if (flaming_helmet.particle == NONE)
			flaming_helmet.particle = tag_loaded(PARTICLE_TAG, FLAME_PARTICLE_FALLBACK);
		flaming_helmet.tongue_particle = tag_loaded(PARTICLE_TAG, FLAME_TONGUE_PARTICLE);
		flaming_helmet.tongue_radius_scale = 1.0f;
		flaming_helmet.tongues_per_tick = FLAME_PARTICLES_PER_TICK;
		/* (ui.map's CUSTOMIZATION preview: no white smoke; the plain grey
		smoke still, tinted lavender, smaller, gives the tips their violet -
		the Covenant smoke tinted so stays blue -; one a tick, as it lives
		far longer: about 160 particles in all, not 400) */
		if (flaming_helmet.tongue_particle == NONE)
		{
			flaming_helmet.tongue_particle = tag_loaded(PARTICLE_TAG, FLAME_TONGUE_PARTICLE_FALLBACK);
			flaming_helmet.tongue_radius_scale = 0.65f;
			flaming_helmet.tongues_per_tick = 1;
		}
		if (flaming_helmet.tongue_particle == NONE)
		{
			flaming_helmet.tongue_particle = flaming_helmet.particle;
			flaming_helmet.tongue_radius_scale = 1.0f;
			flaming_helmet.tongues_per_tick = FLAME_PARTICLES_PER_TICK;
		}
		platform_log("flame: the flame's particle %s in this map", flaming_helmet.particle == NONE ? "not" : "found");
	}
	return flaming_helmet.particle;
}

/* the head's frame: the head marker's ("head": its position and axes,
which turn as the head turns and aims), else the object's centre raised and
its own axes */
struct flame_frame
{
	real_point3d position;
	real_vector3d forward, left, up;
};

static boolean flame_head(long object_index, struct flame_frame *frame)
{
	struct object_marker marker;
	struct object_datum *object;

	if (object_get_marker_by_name(object_index, "head", &marker, 1))
	{
		frame->position = marker.matrix.position;
		frame->forward = marker.matrix.forward;
		frame->left = marker.matrix.left;
		frame->up = marker.matrix.up;
		return TRUE;
	}
	object = object_get(object_index);
	if (!object)
		return FALSE;
	frame->position = object->object.bounding_sphere_center;
	frame->position.z += object->object.bounding_sphere_radius * 0.75f;
	frame->forward = object->object.forward;
	frame->up = object->object.up;
	frame->left.i = frame->up.j * frame->forward.k - frame->up.k * frame->forward.j;
	frame->left.j = frame->up.k * frame->forward.i - frame->up.i * frame->forward.k;
	frame->left.k = frame->up.i * frame->forward.j - frame->up.j * frame->forward.i;
	return TRUE;
}

/* a point of the head's frame: forward, left and up of its origin */
static void flame_point(struct flame_frame const *frame, real forward, real left, real up, real_point3d *point)
{
	point->x = frame->position.x + frame->forward.i * forward + frame->left.i * left + frame->up.i * up;
	point->y = frame->position.y + frame->forward.j * forward + frame->left.j * left + frame->up.j * up;
	point->z = frame->position.z + frame->forward.k * forward + frame->left.k * left + frame->up.k * up;
}

/* the flame on an object, for a tick: local_player_index the wearer's own
(its first person view does not draw it), NONE another's */
static void flame_emit(long object_index, short local_player_index)
{
	unsigned long *seed = get_global_local_random_seed_address();
	real_vector3d velocity, angular;
	struct flame_frame head;
	long particle = flame_particle();
	int index;

	struct object_datum *object = object_try_and_get(object_index);

	/* (a biped a script hid or took off the map - a cutscene's player put
	aside for a stand-in - carries none; one a cutscene shows does) */
	if (particle == NONE || !object || TEST_FLAG(object->object.flags, _object_invisible_bit) ||
		!TEST_FLAG(object->object.flags, _object_connected_to_map_bit) || !flame_head(object_index, &head))
	{
		return;
	}
	object_get_velocities(object_index, &velocity, &angular);
	for (index = 0; index < FLAME_PARTICLES_PER_TICK + flaming_helmet.tongues_per_tick; index++)
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
		/* (one emitter in the head's own frame: its base on the helmet's top,
		over its middle a little toward the visor, spread across the helmet;
		the flames then rise straight up) */
		flame_point(&head,
			FLAME_FORWARD + real_seed_random_range(seed, -FLAME_SPREAD_FORWARD, FLAME_SPREAD_FORWARD),
			real_seed_random_range(seed, -FLAME_SPREAD, FLAME_SPREAD),
			FLAME_HEIGHT + real_seed_random_range(seed, 0.0f, FLAME_SPREAD * 0.5f), &data.position);
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
		data.radius = tongue ? flaming_helmet.tongue_radius_scale *
			real_seed_random_range(seed, FLAME_RADIUS_LOWER * 0.7f, FLAME_RADIUS_LOWER) :
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
		{
			/* (the main menu has no player and so no player's potentially
			visible set, which refuses every new particle as unseen: the
			preview's camera sees its Spartan) */
			particles_new_ignore_players_pvs = TRUE;
			flame_emit(flaming_helmet.preview_object, NONE);
			particles_new_ignore_players_pvs = FALSE;
			if (log_now)
				platform_log("flame: drawn on the preview (%d particles)", particle_data ? (int)particle_data->actual_count : 0);
		}
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
		struct flame_frame frame;
		real_point3d head;
		real facing, yaw, cosine_value, sine_value;

		if (player->local_player_index != 0 || player->unit_index == NONE || !flame_head(player->unit_index, &frame))
			continue;
		head = frame.position;
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
