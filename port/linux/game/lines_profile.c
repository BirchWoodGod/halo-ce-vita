/*
LINES_PROFILE.C

(port, HALO_TICK_PROFILE) Every 300 game frames, "lines-profile (ms/frame)":
the game frame's steps (game.c: the particles, contrails, particle systems,
widgets and looping sounds, on the tick's thread when it has one), the sound
obstruction rays' time and counts (game_sound.c: calls, those answered by the
same tick's ray, those reused by HALO_SOUND_OBSTRUCTION_TICKS, rays cast and
how many met something).
*/

/* ---------- headers */

#include <stdlib.h>

#include "cseries.h"
#include "math/real_math.h"
#include "lines_profile.h"

unsigned long long vita_host_time_us(void) __attribute__((weak));
void platform_log(const char *format, ...);

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
	double frames;
	int step;

	if (!halo_lines_profile_enabled() || ++lines_frames < 300)
		return;
	frames = (double)lines_frames;
	platform_log("lines-profile (ms/frame): particles %.3f contrails %.3f particle_systems %.3f widgets %.3f "
		"game_sound %.3f rest %.3f | obstruction rays %.3f: %.1f calls, %.1f same tick, %.1f reused, %.1f rays "
		"(%.0f%% met something) a frame",
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
		halo_lines_stats.obstruction_rays ? 100.0 * halo_lines_stats.obstruction_ray_hits / halo_lines_stats.obstruction_rays : 0.0);
	for (step = 0; step < NUMBER_OF_LINES_STEPS; step++)
		lines_step_us[step] = 0;
	halo_lines_stats.obstruction_calls = 0;
	halo_lines_stats.obstruction_same_tick = 0;
	halo_lines_stats.obstruction_reused = 0;
	halo_lines_stats.obstruction_rays = 0;
	halo_lines_stats.obstruction_ray_hits = 0;
	halo_lines_stats.obstruction_us = 0;
	lines_frames = 0;
}
