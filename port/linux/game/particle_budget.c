/*
PARTICLE_BUDGET.C

(port) HALO_PARTICLE_BUDGET (full, reduced, low; or 0, 1, 2), else
config.toml's graphics.particles: the settings panel's Particles row, Reduced
in all three of its profiles. Particles are a large share of the tick in a big
fight (a 15-bot Chill Out on the Pi: ~1000 live, the local game's 1024 most
of the time, ~2.8 us each a frame, the Vita some six times that), most of
them the assault rifle's sparks (20-40 at every bullet's impact on metal or
armour, 1.1 s each), and half of all of them deleted within half a second
unseen: out of view, or too small at their distance to be drawn
(render_particles.c draws a particle only over its definition's lod_cutoff
pixels; particles.c deletes one undrawn for 15 ticks).

Reduced and Low thin out what an effect's event makes (effects.c,
effect_generate_particles), as the Xbox game already did for split screen
(effects.c: an entry's count over 6 shared between the local players;
particles.c: a life span over 0.7 s likewise):
- an entry's count over a few is cut (Reduced: 4 and a third of the rest;
  Low: 2 and a sixth), so a single flash and a puff or two stay;
- a life span over 0.7 s is cut likewise (Reduced: half of the rest, Low:
  a quarter): sparks, smoke, dust and casings go sooner, a flash as before;
- with more than a few hundred particles live, the counts are cut further in
  proportion (pressure), and over twice that a thinned particle is not made;
- a thinned particle too small to be drawn at its distance from every view
  drawn lately is not made (Reduced: under half the size that would be drawn,
  Low: under that size, or well behind the camera).
Left whole: particles attached to an object (muzzle flashes), those of the
first-person weapon and first-person only entries, every effect while a
cinematic plays, effects made by scripts (hs effect_new, network co-op's
copies of them) and effects on scenery, devices and the like (a map's fires,
steam and waterfalls). Full changes nothing. Not exact: fewer particles draw
fewer local random numbers, which nothing in the game state depends on.
*/

/* ---------- headers */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cseries/cseries.h"
#include "effects/particles.h"
#include "math/real_math.h"
#include "render/render.h"
#include "game/game_engine.h"
#include "particle_budget.h"

const char *config_string(const char *name);

/* ---------- constants */

enum
{
	/* the local players whose views are noted */
	MAXIMUM_PARTICLE_BUDGET_VIEWS = 4,
	/* a view noted longer ago than this many frames is not used */
	PARTICLE_BUDGET_VIEW_FRAMES = 30,
};

/* the shared life span the Xbox's split screen keeps (particles.c) */
#define PARTICLE_BUDGET_SHARED_LIFESPAN 0.7f

/* ---------- structures */

struct particle_budget_parameters
{
	/* an entry's count kept whole, and the share of the rest made */
	short count_kept;
	short count_divisor;
	/* the share of a life span over the shared one */
	real lifespan_divisor;
	/* live particles from which the counts are cut in proportion, and at
	which a thinned particle is not made */
	short pressure_count;
	short maximum_count;
	/* a particle under this share of lod_cutoff pixels from every view is
	not made */
	real pixels_fraction;
	/* not made either when farther than this behind every view (cosine of
	the angle from the camera's forward, and the distance); 0: not used */
	real behind_cosine;
	real behind_distance;
};

/* ---------- globals */

boolean particle_budget_applies = FALSE;

static struct particle_budget_parameters const particle_budget_parameters[NUMBER_OF_PARTICLE_BUDGET_LEVELS] =
{
	{ 0, 1, 1.0f, 0, 0, 0.0f, 0.0f, 0.0f },
	{ 4, 3, 2.0f, 384, 768, 0.5f, 0.0f, 0.0f },
	{ 2, 6, 4.0f, 192, 384, 1.0f, -0.5f, 4.0f },
};

static struct
{
	long frame_index;
	real_point3d position;
	real_vector3d forward;
	real pixels_per_unit;
} particle_budget_views[MAXIMUM_PARTICLE_BUDGET_VIEWS];
static boolean particle_budget_views_noted = FALSE;

/* ---------- private prototypes */

static struct particle_budget_parameters const *particle_budget_get_parameters(void);

/* ---------- public code */

short particle_budget_level(
	void)
{
	extern volatile unsigned long halo_settings_generation;
	static unsigned long settings_seen;
	static short level = NONE;

	if (level == NONE || settings_seen != halo_settings_generation)
	{
		char const *setting = getenv("HALO_PARTICLE_BUDGET");

		settings_seen = halo_settings_generation;
		if (!setting || !*setting)
			setting = config_string("graphics.particles");
		if (!strcasecmp(setting, "reduced") || !strcmp(setting, "1"))
			level = _particle_budget_reduced;
		else if (!strcasecmp(setting, "low") || !strcmp(setting, "2"))
			level = _particle_budget_low;
		else
			level = _particle_budget_full;
	}
	return level;
}

short particle_budget_count(
	short count)
{
	struct particle_budget_parameters const *parameters = particle_budget_get_parameters();
	long result = count;

	if (parameters && count > parameters->count_kept)
	{
		long live_count = particle_data && particle_data->valid ? particle_data->actual_count : 0;

		result = parameters->count_kept +
			(count - parameters->count_kept + parameters->count_divisor - 1) / parameters->count_divisor;
		if (live_count > parameters->pressure_count && result > 1)
			result = MAX(1, result * parameters->pressure_count / live_count);
	}
	return (short)result;
}

real particle_budget_lifespan(
	real lifespan)
{
	struct particle_budget_parameters const *parameters = particle_budget_get_parameters();

	if (parameters && lifespan > PARTICLE_BUDGET_SHARED_LIFESPAN)
	{
		lifespan = (lifespan - PARTICLE_BUDGET_SHARED_LIFESPAN) / parameters->lifespan_divisor +
			PARTICLE_BUDGET_SHARED_LIFESPAN;
	}
	return lifespan;
}

long particle_budget_unrendered_ticks(
	long full)
{
	/* (Reduced and Low: a spark, a puff or a casing out of view goes after
	a sixth of a second (Low: a tenth) instead of half a second, its
	collision tests with it: in a big multiplayer fight most particles are
	made out of the client's view - other players' shots and impacts - and
	half of them used to live out that half second unseen. Multiplayer
	only: the campaign's look is left as it is) */
	static long const unrendered_ticks[NUMBER_OF_PARTICLE_BUDGET_LEVELS] = { 0, 5, 3 };
	short level = particle_budget_level();

	if (level != _particle_budget_full && VALID_INDEX(level, NUMBER_OF_PARTICLE_BUDGET_LEVELS) &&
		game_engine_running())
	{
		return MIN(full, unrendered_ticks[level]);
	}
	return full;
}

boolean particle_budget_admits(
	real_point3d const *position,
	real maximum_radius,
	real lod_cutoff)
{
	struct particle_budget_parameters const *parameters = particle_budget_get_parameters();
	boolean result = TRUE;

	if (parameters)
	{
		if (particle_data && particle_data->valid && particle_data->actual_count >= parameters->maximum_count)
		{
			result = FALSE;
		}
		else if (particle_budget_views_noted)
		{
			boolean seen = FALSE;
			boolean any_view = FALSE;
			short view_index;

			for (view_index = 0; view_index < MAXIMUM_PARTICLE_BUDGET_VIEWS && !seen; view_index++)
			{
				real_vector3d offset;
				real distance;

				if (render.frame_index - particle_budget_views[view_index].frame_index > PARTICLE_BUDGET_VIEW_FRAMES ||
					particle_budget_views[view_index].frame_index == 0)
				{
					continue;
				}
				any_view = TRUE;
				offset.i = position->x - particle_budget_views[view_index].position.x;
				offset.j = position->y - particle_budget_views[view_index].position.y;
				offset.k = position->z - particle_budget_views[view_index].position.z;
				distance = square_root(magnitude_squared3d(&offset));
				/* (the depth a particle is drawn at is at most its distance:
				its size from the distance is at most the size drawn) */
				if (particle_budget_views[view_index].pixels_per_unit * 2.0f * maximum_radius >=
					parameters->pixels_fraction * lod_cutoff * MAX(distance, 0.1f))
				{
					seen = parameters->behind_distance <= 0.0f ||
						distance <= parameters->behind_distance ||
						dot_product3d(&offset, &particle_budget_views[view_index].forward) >=
							parameters->behind_cosine * distance;
				}
			}
			result = seen || !any_view;
		}
	}
	return result;
}

void particle_budget_note_view(
	short local_player_index,
	real_point3d const *position,
	real_vector3d const *forward,
	real pixels_per_unit)
{
	if (local_player_index >= 0 && local_player_index < MAXIMUM_PARTICLE_BUDGET_VIEWS && pixels_per_unit > 0.0f)
	{
		particle_budget_views[local_player_index].position = *position;
		particle_budget_views[local_player_index].forward = *forward;
		particle_budget_views[local_player_index].pixels_per_unit = pixels_per_unit;
		particle_budget_views[local_player_index].frame_index = render.frame_index;
		particle_budget_views_noted = TRUE;
	}
}

/* ---------- private code */

/* the level's parameters, or NULL at Full */
static struct particle_budget_parameters const *particle_budget_get_parameters(
	void)
{
	short level = particle_budget_level();

	return level != _particle_budget_full ? &particle_budget_parameters[level] : NULL;
}
