/*
LINES_PROFILE.H

(port, HALO_TICK_PROFILE) The game frame's particles and sounds, and the
sound obstruction rays and point physics lookups in them, and the tick's AI
lines of sight, firing position selections, path searches and perception:
lines_profile.c
*/

#ifndef __LINES_PROFILE_H
#define __LINES_PROFILE_H
#pragma once

/* ---------- constants */

enum
{
	_lines_step_particles,
	_lines_step_contrails,
	_lines_step_particle_systems,
	_lines_step_widgets,
	_lines_step_game_sound,
	_lines_step_rest,
	NUMBER_OF_LINES_STEPS
};

/* ---------- structures */

struct halo_lines_stats
{
	unsigned long obstruction_calls;
	unsigned long obstruction_same_tick;
	unsigned long obstruction_reused;
	unsigned long obstruction_rays;
	unsigned long obstruction_ray_hits;
	unsigned long long obstruction_us;
	/* ai_test_line_of_sight (ai.c): calls, their rays (those the visibility
	of the two clusters lets through), what the rays met, the extra lines of
	the expanded modes, and the time */
	unsigned long ai_los_calls;
	unsigned long ai_los_rays;
	unsigned long ai_los_object_hits;
	unsigned long ai_los_other_hits;
	unsigned long ai_los_extra_lines;
	unsigned long long ai_los_us;
	/* actor_select_firing_position (actor_firing_position.c): selections,
	the positions they considered and the lines of sight they tested, the
	time, and the longest selection */
	unsigned long firing_position_calls;
	unsigned long firing_position_considered;
	unsigned long firing_position_lines_of_sight;
	unsigned long long firing_position_us;
	unsigned long long firing_position_worst_us;
	/* path_state_find (path.c): the AI's path searches (firing position
	selections' area and target floods, their nearby tests, path refreshes,
	flight), the nodes (surfaces reached) they made, the time and the longest
	search; path_state_build_path's path smoothing and obstacle avoidance:
	builds and time. Counted on the tick's thread only (the offline bots'
	helper thread searches too) */
	unsigned long path_searches;
	unsigned long path_nodes;
	unsigned long long path_us;
	unsigned long long path_worst_us;
	unsigned long path_builds;
	unsigned long long path_build_us;
	/* actor_perception_update (actor_perception.c): the actors it ran for,
	the props they walked, the props' position refreshes and status
	refreshes (a line of sight each) in that walk, the timeslice refreshes
	(actor_perception_refresh: the nearby units looked for in the actor's
	visible clusters) and the objects those tested, and the time */
	unsigned long perception_actors;
	unsigned long perception_props;
	unsigned long perception_positions;
	unsigned long perception_statuses;
	unsigned long perception_refreshes;
	unsigned long perception_refresh_objects;
	unsigned long long perception_us;
};

/* ---------- globals */

extern struct halo_lines_stats halo_lines_stats;

/* ---------- prototypes */

int halo_lines_profile_enabled(void);
unsigned long long halo_lines_now(void);
/* game_frame: a step's time since started (halo_lines_now), and the frame's end */
void halo_lines_profile_step(int step, unsigned long long started);
void halo_lines_profile_frame(void);

#endif
