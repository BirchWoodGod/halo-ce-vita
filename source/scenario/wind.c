/*
WIND.C

symbols in this file:
0017F740 0010:
	_wind_dispose_from_old_map (0000)
0017F750 00e0:
	_wind_variance_get (0000)
0017F830 0260:
	_wind_update (0000)
0017FA90 0140:
	_scenario_get_current_from_weather_palette (0000)
0017FBD0 0180:
	_wind_variance_initialize (0000)
0017FD50 0050:
	_wind_initialize_for_new_map (0000)
0017FDA0 0120:
	_scenario_get_current (0000)
0017FEC0 0020:
	_scenario_get_wind (0000)
0017FEE0 0020:
	_scenario_get_water_current (0000)
002A07CC 0004:
	__real@4b000000 (0000)
002A07D0 0004:
	__real@bc23d70a (0000)
002A07D4 0019:
	??_C@_0BJ@MOOMLFGA@wind_globals?4initialized?$AA@ (0000)
002A07F0 001f:
	??_C@_0BP@GCGLBDOK@c?3?2halo?2SOURCE?2scenario?2wind?4c?$AA@ (0000)
002A0810 001a:
	??_C@_0BK@OAAHBDPL@?$CBwind_globals?4initialized?$AA@ (0000)
0030E7FC 0004:
	_global_environment_index (0000)
*/

/* ---------- headers */

#define uniform_cubic_spline wind_uniform_cubic_spline_inline
#define uniform_cubic_spline_vector3d wind_uniform_cubic_spline_vector3d_inline
#include "scenario/wind.h"
#undef uniform_cubic_spline_vector3d
#undef uniform_cubic_spline

#include "cseries/cseries.h"
#include "objects/objects.h"
#include "scenario/fog_definitions.h"
#include "scenario/scenario.h"
#include "scenario/wind_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_groups.h"
#ifdef HALO_LINUX
#include <stdlib.h>
#include "structures/structures.h"
#include "render_epoch.h"
#include "point_leaf_cache.h"
#endif

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct wind_state
{
	boolean valid;
	byte pad[3];
	real velocity_variance;
	real_euler_angles2d angular_variance;
	real velocity;
	real_vector3d velocity3d;
};

typedef char wind_state_size_assert[
	sizeof(struct wind_state) == 0x20 ? 1 : -1];

struct wind_globals
{
	boolean initialized;
	byte pad[3];
	real_vector3d variance[3][64];
	short count;
	word pad2;
	struct wind_state states[32];
	long time;
};

typedef char wind_globals_size_assert[
	sizeof(struct wind_globals) == 0xD0C ? 1 : -1];

struct structure_weather_palette_entry
{
	char name[32];
	struct tag_reference particle_system;
	word pad30;
	short runtime_particle_system_global_function_index;
	char particle_system_global_function_name[32];
	long particle_system_unused[11];
	struct tag_reference wind;
	real_vector3d wind_direction;
	real wind_magnitude;
	word padA0;
	short wind_global_function_index;
	char wind_global_function_name[32];
	long wind_unused[11];
};

typedef char structure_weather_palette_entry_size_assert[
	sizeof(struct structure_weather_palette_entry) == 0xF0 ? 1 : -1];
typedef char structure_weather_palette_entry_wind_offset_assert[
	offsetof(struct structure_weather_palette_entry, wind) == 0x80 ? 1 : -1];
typedef char structure_weather_palette_entry_direction_offset_assert[
	offsetof(struct structure_weather_palette_entry, wind_direction) == 0x90 ? 1 : -1];

/* ---------- prototypes */

static void wind_variance_get(
	real_point3d const *position,
	real_vector3d *wind,
	real local_variation_rate,
	real maximum_magnitude);
void wind_variance_initialize(
	void);
void uniform_cubic_spline_vector3d(
	real_vector3d *result,
	real_vector3d const *f0,
	real_vector3d const *f1,
	real_vector3d const *f2,
	real_vector3d const *f3,
	real t0,
	real h,
	real t);

/* ---------- globals */

extern struct wind_globals wind_globals;
long global_environment_index = NONE;

/* ---------- public code */

void wind_dispose_from_old_map(
	void)
{
	wind_globals.initialized = FALSE;

	return;
}

static void wind_variance_get(
	real_point3d const *position,
	real_vector3d *wind,
	real local_variation_rate,
	real maximum_magnitude)
{
	real const axis_scale[3] = { 0.1f, 0.2f, 0.07f };
	real magnitude = maximum_magnitude * (1.f / 3.f);
	long axis_index;
	long axes_remaining = 3;

	*wind = *global_zero_vector3d;
	axis_index = 0;
	do
	{
		union
		{
			real value;
			long bits;
		} sample_key;
		long sample_index;

		sample_key.value =
			((axis_scale[axis_index] * wind_globals.time * local_variation_rate)
				+ position->n[axis_index]) * 8.f;
		sample_key.bits &= 0x7FFFFFFF;
		sample_key.value += 8388608.f;
		sample_index = (byte)sample_key.bits;

		sample_index &= 0x3F;
		wind->i += wind_globals.variance[axis_index][(short)sample_index].i;
		wind->j += wind_globals.variance[axis_index][(short)sample_index].j;
		wind->k += wind_globals.variance[axis_index][(short)sample_index].k;
		axis_index++;
	}
	while (--axes_remaining);

	wind->i *= magnitude;
	wind->j *= magnitude;
	wind->k *= magnitude;

	return;
}

void wind_update(
	void)
{
	short weather_palette_index;
	short weather_palette_count;
	struct structure_bsp *structure_bsp = global_structure_bsp_get();
	struct tag_block *weather_palette_block;

	match_assert(
		"c:\\halo\\SOURCE\\scenario\\wind.c",
		89,
		wind_globals.initialized);
	wind_globals.time++;
	weather_palette_index = 0;
	weather_palette_block = &structure_bsp->weather_palette;
	/* port (from OpenCE, MrBruh's "Harden map and network input"): no more
	entries than there are wind states (a map's count) */
	weather_palette_count = (short)MIN(weather_palette_block->count, (long)NUMBEROF(wind_globals.states));

	for (;
		weather_palette_index < weather_palette_count;
		weather_palette_index++)
	{
		struct structure_weather_palette_entry *weather_palette =
			TAG_BLOCK_GET_ELEMENT(
				weather_palette_block,
				weather_palette_index,
				struct structure_weather_palette_entry);
		struct wind_state *state = &wind_globals.states[weather_palette_index];

		if (weather_palette->wind.index != NONE)
		{
			struct wind_definition *definition =
				wind_definition_get(weather_palette->wind.index);
			real_euler_angles2d direction;
			real_vector3d *velocity;
			real scale;

			state->velocity_variance += seed_random_range(
					get_global_local_random_seed_address(),
					0,
					2) ? 0.01f : -0.01f;
			state->velocity_variance =
				PIN(state->velocity_variance, 0.f, 1.f);

			state->angular_variance.pitch += seed_random_range(
					get_global_local_random_seed_address(),
					0,
					2) ? 0.01f : -0.01f;
			state->angular_variance.pitch =
				PIN(state->angular_variance.pitch, -1.f, 1.f);

			state->angular_variance.yaw += seed_random_range(
					get_global_local_random_seed_address(),
					0,
					2) ? 0.01f : -0.01f;
			state->angular_variance.yaw =
				PIN(state->angular_variance.yaw, -1.f, 1.f);

			state->velocity =
				(definition->velocity_upper_bound - definition->velocity_lower_bound)
					* state->velocity_variance
				+ definition->velocity_lower_bound;

			euler_angles2d_from_vector3d(
				&direction,
				&weather_palette->wind_direction);
			direction.pitch += definition->variation_area.pitch
				* state->angular_variance.pitch * 0.5f;
			direction.yaw += definition->variation_area.yaw
				* state->angular_variance.yaw * 0.5f;

			velocity = &state->velocity3d;
			vector3d_from_euler_angles2d(velocity, &direction);
			scale = weather_palette->wind_magnitude * state->velocity;
			velocity->i *= scale;
			velocity->j *= scale;
			velocity->k *= scale;
			state->valid = TRUE;
		}
		else
		{
			state->valid = FALSE;
		}
	}

	wind_globals.count = weather_palette_count;

	return;
}

void scenario_get_current_from_weather_palette(
	real_point3d const *position,
	real_vector3d *current,
	long flags,
	short weather_palette_index)
{
	struct wind_state *state;

	if (VALID_INDEX(weather_palette_index, wind_globals.count))
	{
		state = &wind_globals.states[weather_palette_index];
		if (state->valid)
		{
			struct structure_weather_palette_entry *weather_palette =
				TAG_BLOCK_GET_ELEMENT(
					&global_structure_bsp_get()->weather_palette,
					weather_palette_index,
					struct structure_weather_palette_entry);
			struct wind_definition *definition =
				wind_definition_get(weather_palette->wind.index);
			real local_variation_weight =
				TEST_FLAG(flags, _scenario_current_simple_bit)
					? 0.f
					: definition->local_variation_weight;
			real_vector3d variance;

			wind_variance_get(
				position,
				&variance,
				definition->local_variation_rate,
				definition->local_variation_weight * state->velocity);

			current->i = state->velocity3d.i * (1.f - local_variation_weight)
				+ variance.i;
			current->j = state->velocity3d.j * (1.f - local_variation_weight)
				+ variance.j;
			current->k = state->velocity3d.k * (1.f - local_variation_weight)
				+ variance.k;

			if (TEST_FLAG(flags, _scenario_current_damped_bit))
			{
				current->i *= 1.f - definition->damping;
				current->j *= 1.f - definition->damping;
				current->k *= 1.f - definition->damping;
			}

			return;
		}

		*current = *global_zero_vector3d;

		return;
	}

	*current = *global_zero_vector3d;

	return;
}

void wind_initialize_for_new_map(
	void)
{
	global_structure_bsp_get();
	match_assert(
		"c:\\halo\\SOURCE\\scenario\\wind.c",
		65,
		!wind_globals.initialized);
	memset(&wind_globals, 0, sizeof(wind_globals));
	wind_globals.initialized = TRUE;
	wind_variance_initialize();

	return;
}

#ifdef HALO_LINUX
/* (port) what scenario_get_current reads of the structure bsp's tags for a
location, by cluster, read once for each bsp: the point physics asks for each
particle's current every frame (~600 times a tick in b30's fight, ~0.26 M of
the Vita's cycles a tick in callgrind's Cortex-A9 model), each call a dozen
tag reads and two or three tag lookups whose answers never change. A cluster's
weather palette entry and its fog: none, a fog region, or a fog plane (its
plane, the offset its fog adds, and the region a point behind it is in, as
scenario_get_fog_region_index finds them); a region's weather palette entry
and whether its fog is water, when it has both. The same values, compared and
chosen as scenario_get_current and scenario_get_fog_region_index do; the
tick's thread only (the point physics runs there), others reading the tags as
before; built again when the bsp changes (halo_structure_bsp_generation) */
enum
{
	_current_fog_none,
	_current_fog_region,
	_current_fog_plane
};

static struct
{
	unsigned long generation;
	boolean usable;
	short cluster_count;
	struct
	{
		short weather_palette_index;
		short fog_kind;
		/* the region (a plane's for a point behind it), valid */
		short fog_region_index;
		real_plane3d const *plane;
		real plane_distance;
	} clusters[MAXIMUM_CLUSTERS_PER_STRUCTURE];
	struct
	{
		/* (its palette entry valid, its weather palette entry and fog not NONE) */
		boolean has_weather;
		boolean water;
		short weather_palette_index;
	} regions[MAXIMUM_FOG_REGIONS_PER_STRUCTURE];
} current_clusters;

static boolean current_clusters_ready(
	void)
{
	struct structure_bsp *structure_bsp;
	short cluster_index;
	short region_index;

	if (halo_epoch_threaded && !halo_epoch_on_mutator_inline())
	{
		return FALSE;
	}
	if (current_clusters.generation == halo_structure_bsp_generation)
	{
		return current_clusters.usable;
	}
	current_clusters.generation = halo_structure_bsp_generation;
	current_clusters.usable = FALSE;
	structure_bsp = global_structure_bsp_get();
	if (!structure_bsp ||
		structure_bsp->clusters.count > MAXIMUM_CLUSTERS_PER_STRUCTURE ||
		structure_bsp->fog_regions.count > MAXIMUM_FOG_REGIONS_PER_STRUCTURE)
	{
		return FALSE;
	}
	for (region_index = 0; region_index < structure_bsp->fog_regions.count; region_index++)
	{
		struct structure_fog_region *fog_region = TAG_BLOCK_GET_ELEMENT(
			&structure_bsp->fog_regions,
			region_index,
			struct structure_fog_region);

		current_clusters.regions[region_index].has_weather = FALSE;
		current_clusters.regions[region_index].water = FALSE;
		current_clusters.regions[region_index].weather_palette_index = fog_region->weather_palette_index;
		if (VALID_INDEX(fog_region->fog_palette_index, structure_bsp->fog_palette.count) &&
			fog_region->weather_palette_index != NONE)
		{
			struct structure_fog_palette_entry *fog_palette = TAG_BLOCK_GET_ELEMENT(
				&structure_bsp->fog_palette,
				fog_region->fog_palette_index,
				struct structure_fog_palette_entry);

			if (fog_palette->fog.index != NONE)
			{
				current_clusters.regions[region_index].has_weather = TRUE;
				current_clusters.regions[region_index].water =
					TEST_FLAG(fog_definition_get(fog_palette->fog.index)->flags, 0) ? TRUE : FALSE;
			}
		}
	}
	for (cluster_index = 0; cluster_index < structure_bsp->clusters.count; cluster_index++)
	{
		struct structure_cluster *cluster = TAG_BLOCK_GET_ELEMENT(
			&structure_bsp->clusters,
			cluster_index,
			struct structure_cluster);
		short fog_reference = cluster->fog_reference;

		current_clusters.clusters[cluster_index].weather_palette_index = cluster->weather_palette_index;
		current_clusters.clusters[cluster_index].fog_kind = _current_fog_none;
		current_clusters.clusters[cluster_index].fog_region_index = NONE;
		current_clusters.clusters[cluster_index].plane = NULL;
		current_clusters.clusters[cluster_index].plane_distance = 0.0f;
		if (fog_reference == NONE)
		{
			continue;
		}
		if (TEST_FLAG((word)fog_reference, 15))
		{
			struct structure_fog_plane *fog_plane;
			long fog_index;

			if ((fog_reference & SHORT_MAX) >= structure_bsp->fog_planes.count)
			{
				continue;
			}
			fog_plane = TAG_BLOCK_GET_ELEMENT(
				&structure_bsp->fog_planes,
				fog_reference & SHORT_MAX,
				struct structure_fog_plane);
			if (!VALID_INDEX(fog_plane->region_index, structure_bsp->fog_regions.count))
			{
				continue;
			}
			fog_index = scenario_fog_region_get_fog_index(fog_plane->region_index);
			if (fog_index != NONE)
			{
				struct fog_definition *fog = fog_definition_get(fog_index);

				if (TEST_FLAG(fog->flags, 0))
					current_clusters.clusters[cluster_index].plane_distance = fog->plane_distance;
			}
			current_clusters.clusters[cluster_index].fog_kind = _current_fog_plane;
			current_clusters.clusters[cluster_index].fog_region_index = fog_plane->region_index;
			current_clusters.clusters[cluster_index].plane = &fog_plane->plane;
		}
		else if (VALID_INDEX(fog_reference & SHORT_MAX, structure_bsp->fog_regions.count))
		{
			current_clusters.clusters[cluster_index].fog_kind = _current_fog_region;
			current_clusters.clusters[cluster_index].fog_region_index = fog_reference & SHORT_MAX;
		}
	}
	current_clusters.cluster_count = (short)structure_bsp->clusters.count;
	current_clusters.usable = TRUE;

	return TRUE;
}

/* (debug) HALO_SCENARIO_CURRENT_VERIFY=1: each answer from the table read
from the tags as well, any difference logged */
static boolean current_clusters_verify(
	void)
{
	static int verify = -1;

	if (verify < 0)
	{
		char const *setting = getenv("HALO_SCENARIO_CURRENT_VERIFY");

		verify = setting && atoi(setting) != 0;
	}
	return verify;
}

static void current_clusters_mismatch(
	struct location const *location,
	short tags_weather_palette_index,
	short weather_palette_index)
{
	static unsigned long mismatches;
	void platform_log(const char *format, ...);

	if (mismatches++ < 20)
		platform_log("scenario current mismatch: cluster %d, weather palette entry %d from the tags, %d from the table",
			location->cluster_index, tags_weather_palette_index, weather_palette_index);
}
#endif

/* (the weather palette entry and water of a location, from the tags) */
static short scenario_current_weather_palette_index(
	struct location const *location,
	real_point3d const *position,
	long flags,
	boolean *water)
{
	boolean in_water = FALSE;
	short weather_palette_index = NONE;

	if (location->cluster_index != NONE)
	{
		struct structure_bsp *structure_bsp = global_structure_bsp_get();
		short fog_region_index = scenario_get_fog_region_index(
			location,
			TEST_FLAG(flags, _scenario_current_force_water_bit) ? NULL : position);
		struct structure_cluster *cluster = TAG_BLOCK_GET_ELEMENT(
			&structure_bsp->clusters,
			location->cluster_index,
			struct structure_cluster);

		weather_palette_index = cluster->weather_palette_index;
		if (fog_region_index != NONE)
		{
			struct structure_fog_region *fog_region = TAG_BLOCK_GET_ELEMENT(
				&structure_bsp->fog_regions,
				fog_region_index,
				struct structure_fog_region);

			/* port: and the palette entry is one the map has */
			if (VALID_INDEX(fog_region->fog_palette_index, structure_bsp->fog_palette.count)
				&& fog_region->weather_palette_index != NONE)
			{
				struct structure_fog_palette_entry *fog_palette =
					TAG_BLOCK_GET_ELEMENT(
						&structure_bsp->fog_palette,
						fog_region->fog_palette_index,
						struct structure_fog_palette_entry);

				if (fog_palette->fog.index != NONE)
				{
					struct fog_definition *fog =
						fog_definition_get(fog_palette->fog.index);

					if (TEST_FLAG(fog->flags, 0))
					{
						if (!TEST_FLAG(flags, _scenario_current_force_no_water_bit))
						{
							weather_palette_index = fog_region->weather_palette_index;
							in_water = TRUE;
						}
					}
					else if (!TEST_FLAG(flags, _scenario_current_force_water_bit))
					{
						weather_palette_index = fog_region->weather_palette_index;
					}
				}
			}
		}
	}

	*water = in_water;
	return weather_palette_index;
}

boolean scenario_get_current(
	struct location const *location,
	real_point3d const *position,
	real_vector3d *current,
	long flags)
{
	boolean in_water = FALSE;
	short weather_palette_index = NONE;

#ifdef HALO_LINUX
	if (location->cluster_index != NONE &&
		location->cluster_index >= 0 &&
		current_clusters_ready() &&
		location->cluster_index < current_clusters.cluster_count)
	{
		short fog_region_index = NONE;
		short cluster_index = location->cluster_index;

		weather_palette_index = current_clusters.clusters[cluster_index].weather_palette_index;
		switch (current_clusters.clusters[cluster_index].fog_kind)
		{
		case _current_fog_region:
			fog_region_index = current_clusters.clusters[cluster_index].fog_region_index;
			break;
		case _current_fog_plane:
			/* (scenario_get_fog_region_index: no position with the water forced) */
			if (TEST_FLAG(flags, _scenario_current_force_water_bit) ||
				!position ||
				plane3d_distance_to_point(current_clusters.clusters[cluster_index].plane, position) +
					current_clusters.clusters[cluster_index].plane_distance < 0.0f)
			{
				fog_region_index = current_clusters.clusters[cluster_index].fog_region_index;
			}
			break;
		}
		if (fog_region_index != NONE && current_clusters.regions[fog_region_index].has_weather)
		{
			if (current_clusters.regions[fog_region_index].water)
			{
				if (!TEST_FLAG(flags, _scenario_current_force_no_water_bit))
				{
					weather_palette_index = current_clusters.regions[fog_region_index].weather_palette_index;
					in_water = TRUE;
				}
			}
			else if (!TEST_FLAG(flags, _scenario_current_force_water_bit))
			{
				weather_palette_index = current_clusters.regions[fog_region_index].weather_palette_index;
			}
		}
		if (current_clusters_verify())
		{
			boolean water;
			short tags_weather_palette_index = scenario_current_weather_palette_index(location, position, flags, &water);

			if (tags_weather_palette_index != weather_palette_index || water != in_water)
				current_clusters_mismatch(location, tags_weather_palette_index, weather_palette_index);
		}
	}
	else
#endif
	{
		weather_palette_index = scenario_current_weather_palette_index(location, position, flags, &in_water);
	}

	scenario_get_current_from_weather_palette(
		position,
		current,
		flags,
		weather_palette_index);

	return in_water;
}

void scenario_get_wind(
	struct location const *location,
	real_point3d const *position,
	real_vector3d *current,
	long flags)
{
	scenario_get_current(
		location,
		position,
		current,
		flags | FLAG(_scenario_current_force_no_water_bit));

	return;
}

void scenario_get_water_current(
	struct location const *location,
	real_point3d const *position,
	real_vector3d *current,
	long flags)
{
	scenario_get_current(
		location,
		position,
		current,
		flags | FLAG(_scenario_current_force_water_bit));

	return;
}

/* ---------- private code */

void wind_variance_initialize(
	void)
{
	short control_point_index;
	short sample_index;
	short axis_index;

	for (control_point_index = 0; control_point_index < 8; control_point_index++)
	{
		for (axis_index = 0; axis_index < 3; axis_index++)
		{
			seed_random_direction3d(
				get_global_random_seed_address(),
				&wind_globals.variance[axis_index][control_point_index * 8]);
		}
	}

	for (control_point_index = 0; control_point_index < 8; control_point_index++)
	{
		for (sample_index = 1; sample_index < 8; sample_index++)
		{
			for (axis_index = 0; axis_index < 3; axis_index++)
			{
				word control_point_indices[4];

				control_point_indices[0] = (control_point_index - 1) & 7;
				control_point_indices[1] = control_point_index;
				control_point_indices[2] = (control_point_index + 1) & 7;
				control_point_indices[3] = (control_point_index + 2) & 7;

				uniform_cubic_spline_vector3d(
					&wind_globals.variance[axis_index][control_point_index * 8 + sample_index],
					&wind_globals.variance[axis_index][control_point_indices[0] * 8],
					&wind_globals.variance[axis_index][control_point_indices[1] * 8],
					&wind_globals.variance[axis_index][control_point_indices[2] * 8],
					&wind_globals.variance[axis_index][control_point_indices[3] * 8],
					(real)(control_point_indices[1] - 1),
					1.f,
					(real)sample_index * 0.125f + (real)control_point_index);
			}
		}
	}

	return;
}
