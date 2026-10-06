/* fine_profile.c

HALO_PROFILE_SAMPLE=N: which frames and ticks the fine timers run in
(fine_profile.h). One counter per thread: the main thread picks frames,
the tick thread ticks; neither touches the other's. */

#include <stdio.h>
#include <stdlib.h>

#include "fine_profile.h"

unsigned long long vita_host_time_us(void) __attribute__((weak));

unsigned char halo_fine_render_on;
unsigned char halo_fine_tick_on;

static int sample_every = -1;
/* us per clock read, measured once by each thread (the main thread's for
the render notes, the tick thread's for the tick's) */
static double render_clock_us = -1.0, tick_clock_us = -1.0;

static unsigned long render_frame_counter, render_frames_timed, render_reads;
static unsigned long tick_counter, ticks_timed, tick_reads;

static void fine_setup(double *clock_us)
{
	if (sample_every < 0)
	{
		const char *setting = getenv("HALO_PROFILE_SAMPLE");

		/* (both threads may get here first: they store the same value) */
		sample_every = setting && atoi(setting) > 1 ? atoi(setting) : 1;
	}
	if (*clock_us < 0.0)
	{
		/* (256 reads between two: the per-read cost the reports quote) */
		unsigned long long before = vita_host_time_us(), after;
		volatile unsigned long long sink = 0;
		int index;

		for (index = 0; index < 256; index++)
			sink += vita_host_time_us();
		after = vita_host_time_us();
		*clock_us = (double)(after - before) / 256.0;
		(void)sink;
	}
}

void halo_fine_render_frame(void)
{
	static int render_profile_enabled = -1;

	if (render_profile_enabled < 0)
	{
		const char *setting = getenv("HALO_RENDER_PROFILE");

		render_profile_enabled = setting ? atoi(setting) : 0;
	}
	if (render_profile_enabled <= 0 || !vita_host_time_us)
	{
		halo_fine_render_on = 0;
		return;
	}
	fine_setup(&render_clock_us);
	halo_fine_render_on = (render_frame_counter++ % (unsigned long)sample_every) == 0;
	if (halo_fine_render_on)
		render_frames_timed++;
}

void halo_fine_tick_begin(void)
{
	static int tick_profile_level = -1;

	if (tick_profile_level < 0)
	{
		const char *setting = getenv("HALO_TICK_PROFILE");

		tick_profile_level = setting ? atoi(setting) : 0;
	}
	if (tick_profile_level < 2 || !vita_host_time_us)
	{
		halo_fine_tick_on = 0;
		return;
	}
	fine_setup(&tick_clock_us);
	halo_fine_tick_on = (tick_counter++ % (unsigned long)sample_every) == 0;
	if (halo_fine_tick_on)
		ticks_timed++;
}

unsigned long long halo_fine_render_now(void)
{
	render_reads++;
	return vita_host_time_us();
}

unsigned long long halo_fine_tick_now(void)
{
	tick_reads++;
	return vita_host_time_us();
}

static unsigned long fine_since(struct halo_fine_mark *mark, unsigned long timed_now, unsigned long reads_now, double clock_us,
	const char *unit)
{
	unsigned long timed = timed_now - mark->timed;
	double reads = timed ? (double)(reads_now - mark->reads) / timed : 0.0;

	snprintf(mark->note, sizeof(mark->note), "1 in %d %ss timed, %.0f clock reads a timed %s ~%.2f ms of it",
		sample_every < 1 ? 1 : sample_every, unit, reads, unit, reads * (clock_us > 0.0 ? clock_us : 0.0) / 1000.0);
	mark->timed = timed_now;
	mark->reads = reads_now;
	return timed;
}

unsigned long halo_fine_render_since(struct halo_fine_mark *mark)
{
	return fine_since(mark, render_frames_timed, render_reads, render_clock_us, "frame");
}

unsigned long halo_fine_tick_since(struct halo_fine_mark *mark)
{
	return fine_since(mark, ticks_timed, tick_reads, tick_clock_us, "tick");
}
