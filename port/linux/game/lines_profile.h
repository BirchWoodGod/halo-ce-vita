/*
LINES_PROFILE.H

(port, HALO_TICK_PROFILE) The game frame's particles and sounds, and the
sound obstruction rays and point physics lookups in them, and the tick's AI
lines of sight: lines_profile.c
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
