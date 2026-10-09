/*
LINES_PROFILE.C

(port, HALO_TICK_PROFILE) Every 300 game frames, "lines-profile (ms/frame)":
the game frame's steps (game.c: the particles, contrails, particle systems,
widgets and looping sounds, on the tick's thread when it has one), the sound
obstruction rays' time and counts (game_sound.c: calls, those answered by the
same tick's ray, those reused by HALO_SOUND_OBSTRUCTION_TICKS, rays cast and
how many met something), the point physics' leaf lookups, with how many
were answered from a known cell (point_leaf_cache.c), and the AI's lines of
sight (ai.c: ai_test_line_of_sight, run by the tick between two game frames;
its calls, the rays they cast, the share of those that met an object or the
structure, and the extra lines of the expanded modes), with the collision
tests' cluster walks: how many a frame, and how many found their cluster's
table of objects and types (cluster_object_types.c), and the AI's firing
position selections (actor_firing_position.c: actor_select_firing_position,
run by the tick when an actor's timeslice comes up or it begins to flee or
guard; a frame, with the positions they considered and the lines of sight
they tested, and the longest one: one selection is a spike in its tick),
and the AI's path searches (path.c: path_state_find, inside those selections
and in path refreshes; a frame, the nodes each made and the longest) and path
builds (path_state_build_path: smoothing and obstacle avoidance), and the
AI's perception (actor_perception.c: actor_perception_update, each actor's
walk of its props with their position and status refreshes, and its
timeslice refresh of the units nearby).
*/

/* ---------- headers */

#include <stdlib.h>

#include "cseries.h"
#include "math/real_math.h"
#include "lines_profile.h"
#include "point_leaf_cache.h"

unsigned long long vita_host_time_us(void) __attribute__((weak));
void platform_log(const char *format, ...);
/* (collisions.c) the obstruction rays' structure tests answered as before, and not */
extern unsigned long obstruction_memo_hits, obstruction_memo_misses;
/* (cluster_object_types.c) the collision walks' cluster tables found, and made */
extern unsigned long cluster_object_types_found, cluster_object_types_made;

/* ---------- globals */

struct halo_lines_stats halo_lines_stats;

static int lines_enabled = -1;
static unsigned long long lines_step_us[NUMBER_OF_LINES_STEPS];
static unsigned long lines_frames;

/* ---------- public code */

int halo_lines_profile_enabled(void)
{
	if (lines_enabled < 0)
	{
		char const *setting = getenv("HALO_TICK_PROFILE");

		lines_enabled = setting && atoi(setting) != 0 && vita_host_time_us;
	}
	return lines_enabled;
}

unsigned long long halo_lines_now(void)
{
	return halo_lines_profile_enabled() ? vita_host_time_us() : 0;
}

void halo_lines_profile_step(int step, unsigned long long started)
{
	if (started)
		lines_step_us[step] += vita_host_time_us() - started;
}

void halo_lines_profile_frame(void)
{
	static unsigned long points_seen, point_hits_seen, memo_hits_seen, memo_misses_seen, tables_found_seen, tables_made_seen;
	unsigned long points, point_hits, memo_hits, memo_misses, tables_found, tables_made;
	double frames;
	int step;

	if (!halo_lines_profile_enabled() || ++lines_frames < 300)
		return;
	frames = (double)lines_frames;
	points = point_leaf_cache_points - points_seen;
	point_hits = point_leaf_cache_point_hits - point_hits_seen;
	memo_hits = obstruction_memo_hits - memo_hits_seen;
	memo_misses = obstruction_memo_misses - memo_misses_seen;
	tables_found = cluster_object_types_found - tables_found_seen;
	tables_made = cluster_object_types_made - tables_made_seen;
	platform_log("lines-profile (ms/frame): particles %.3f contrails %.3f particle_systems %.3f widgets %.3f "
		"game_sound %.3f rest %.3f | obstruction rays %.3f: %.1f calls, %.1f same tick, %.1f reused, %.1f rays "
		"(%.0f%% met something, %.0f%% of their structure tests as before) a frame | point physics: %.1f leaf lookups a frame, %.0f%% from a known cell"
		" | ai line of sight %.3f: %.1f calls, %.1f rays (%.0f%% met an object, %.0f%% the structure), %.1f extra lines a frame"
		" | cluster walks %.1f a frame, %.0f%% from a kept table"
		" | ai firing positions %.3f: %.2f selections a frame (%.1f positions, %.1f lines of sight a selection), longest %.3f ms"
		" | ai path searches %.3f: %.2f a frame (%.1f nodes a search), longest %.3f ms; path builds %.3f: %.2f a frame"
		" | ai perception %.3f: %.1f actors, %.1f props, %.1f position and %.1f status refreshes, %.2f timeslice refreshes (%.0f objects each) a frame",
		lines_step_us[_lines_step_particles] / 1000.0 / frames,
		lines_step_us[_lines_step_contrails] / 1000.0 / frames,
		lines_step_us[_lines_step_particle_systems] / 1000.0 / frames,
		lines_step_us[_lines_step_widgets] / 1000.0 / frames,
		lines_step_us[_lines_step_game_sound] / 1000.0 / frames,
		lines_step_us[_lines_step_rest] / 1000.0 / frames,
		halo_lines_stats.obstruction_us / 1000.0 / frames,
		halo_lines_stats.obstruction_calls / frames,
		halo_lines_stats.obstruction_same_tick / frames,
		halo_lines_stats.obstruction_reused / frames,
		halo_lines_stats.obstruction_rays / frames,
		halo_lines_stats.obstruction_rays ? 100.0 * halo_lines_stats.obstruction_ray_hits / halo_lines_stats.obstruction_rays : 0.0,
		memo_hits + memo_misses ? 100.0 * memo_hits / (memo_hits + memo_misses) : 0.0,
		points / frames, points ? 100.0 * point_hits / points : 0.0,
		halo_lines_stats.ai_los_us / 1000.0 / frames,
		halo_lines_stats.ai_los_calls / frames,
		halo_lines_stats.ai_los_rays / frames,
		halo_lines_stats.ai_los_rays ? 100.0 * halo_lines_stats.ai_los_object_hits / halo_lines_stats.ai_los_rays : 0.0,
		halo_lines_stats.ai_los_rays ? 100.0 * halo_lines_stats.ai_los_other_hits / halo_lines_stats.ai_los_rays : 0.0,
		halo_lines_stats.ai_los_extra_lines / frames,
		(tables_found + tables_made) / frames,
		tables_found + tables_made ? 100.0 * tables_found / (tables_found + tables_made) : 0.0,
		halo_lines_stats.firing_position_us / 1000.0 / frames,
		halo_lines_stats.firing_position_calls / frames,
		halo_lines_stats.firing_position_calls ? (double)halo_lines_stats.firing_position_considered / halo_lines_stats.firing_position_calls : 0.0,
		halo_lines_stats.firing_position_calls ? (double)halo_lines_stats.firing_position_lines_of_sight / halo_lines_stats.firing_position_calls : 0.0,
		halo_lines_stats.firing_position_worst_us / 1000.0,
		halo_lines_stats.path_us / 1000.0 / frames,
		halo_lines_stats.path_searches / frames,
		halo_lines_stats.path_searches ? (double)halo_lines_stats.path_nodes / halo_lines_stats.path_searches : 0.0,
		halo_lines_stats.path_worst_us / 1000.0,
		halo_lines_stats.path_build_us / 1000.0 / frames,
		halo_lines_stats.path_builds / frames,
		halo_lines_stats.perception_us / 1000.0 / frames,
		halo_lines_stats.perception_actors / frames,
		halo_lines_stats.perception_props / frames,
		halo_lines_stats.perception_positions / frames,
		halo_lines_stats.perception_statuses / frames,
		halo_lines_stats.perception_refreshes / frames,
		halo_lines_stats.perception_refreshes ? (double)halo_lines_stats.perception_refresh_objects / halo_lines_stats.perception_refreshes : 0.0);
	for (step = 0; step < NUMBER_OF_LINES_STEPS; step++)
		lines_step_us[step] = 0;
	halo_lines_stats.obstruction_calls = 0;
	halo_lines_stats.obstruction_same_tick = 0;
	halo_lines_stats.obstruction_reused = 0;
	halo_lines_stats.obstruction_rays = 0;
	halo_lines_stats.obstruction_ray_hits = 0;
	halo_lines_stats.obstruction_us = 0;
	halo_lines_stats.ai_los_calls = 0;
	halo_lines_stats.ai_los_rays = 0;
	halo_lines_stats.ai_los_object_hits = 0;
	halo_lines_stats.ai_los_other_hits = 0;
	halo_lines_stats.ai_los_extra_lines = 0;
	halo_lines_stats.ai_los_us = 0;
	halo_lines_stats.firing_position_calls = 0;
	halo_lines_stats.firing_position_considered = 0;
	halo_lines_stats.firing_position_lines_of_sight = 0;
	halo_lines_stats.firing_position_us = 0;
	halo_lines_stats.firing_position_worst_us = 0;
	halo_lines_stats.path_searches = 0;
	halo_lines_stats.path_nodes = 0;
	halo_lines_stats.path_us = 0;
	halo_lines_stats.path_worst_us = 0;
	halo_lines_stats.path_builds = 0;
	halo_lines_stats.path_build_us = 0;
	halo_lines_stats.perception_actors = 0;
	halo_lines_stats.perception_props = 0;
	halo_lines_stats.perception_positions = 0;
	halo_lines_stats.perception_statuses = 0;
	halo_lines_stats.perception_refreshes = 0;
	halo_lines_stats.perception_refresh_objects = 0;
	halo_lines_stats.perception_us = 0;
	points_seen = point_leaf_cache_points;
	point_hits_seen = point_leaf_cache_point_hits;
	memo_hits_seen = obstruction_memo_hits;
	memo_misses_seen = obstruction_memo_misses;
	tables_found_seen = cluster_object_types_found;
	tables_made_seen = cluster_object_types_made;
	lines_frames = 0;
}
