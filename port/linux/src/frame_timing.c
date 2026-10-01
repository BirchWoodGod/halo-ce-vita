/* frame_timing.c

HALO_FRAME_TIMING=N: every N frames, print to stderr where main_loop's time
went (game ticks, render, throttle and present, the rest), with the game
ticks run in those frames. A measurement aid for the Vita port spike. */

#include <stdio.h>
#include <stdlib.h>

/* (platform.h's; stderr on Linux, the log on the Vita) */
void platform_log(const char *format, ...);
#include <time.h>

#include "frame_timing.h"

static int frame_timing_every = -1;
static unsigned long long frame_timing_mark[_frame_timing_event_count];
static unsigned long long frame_timing_sum[4], frame_timing_max_frame;
static unsigned long frame_timing_frames, frame_timing_ticks_start, frame_timing_ticks_last;

static unsigned long long frame_timing_now(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long long)now.tv_sec * 1000000000ull + (unsigned long long)now.tv_nsec;
}

/* rolling averages of the last frames, for an on-screen overlay */
#define RECENT_FRAMES 30
static unsigned long long recent_frame[RECENT_FRAMES], recent_tick[RECENT_FRAMES], recent_render[RECENT_FRAMES];
static unsigned long recent_index, recent_count;

void halo_frame_timing_recent(float *frame_ms, float *tick_ms, float *render_ms)
{
	unsigned long long frame = 0, tick = 0, render = 0;
	unsigned long index, count = recent_count < RECENT_FRAMES ? recent_count : RECENT_FRAMES;

	for (index = 0; index < count; index++)
	{
		frame += recent_frame[index];
		tick += recent_tick[index];
		render += recent_render[index];
	}
	*frame_ms = count ? (float)frame / 1e6f / (float)count : 0.0f;
	*tick_ms = count ? (float)tick / 1e6f / (float)count : 0.0f;
	*render_ms = count ? (float)render / 1e6f / (float)count : 0.0f;
}

/* the tick on its own thread (tick_thread.c): its duration this frame,
overlapping the render, so it is reported but not part of the frame's sum */
static unsigned long long threaded_tick, threaded_tick_sum;

void halo_frame_timing_tick_threaded(unsigned long long tick_us)
{
	threaded_tick = tick_us;
}

void halo_frame_timing(int event, unsigned long game_ticks)
{
	unsigned long long frame;

	if (frame_timing_every < 0)
	{
		const char *value = getenv("HALO_FRAME_TIMING");
		frame_timing_every = value ? atoi(value) : 0;
		frame_timing_ticks_start = game_ticks;
	}
	if (event == _frame_timing_frame_end && frame_timing_mark[_frame_timing_frame_start])
	{
		/* (the rolling averages run whether or not the report does) */
		unsigned long long now = frame_timing_now();

		recent_frame[recent_index] = now - frame_timing_mark[_frame_timing_frame_start];
		recent_tick[recent_index] = threaded_tick ? threaded_tick : frame_timing_mark[_frame_timing_tick_end] ?
			frame_timing_mark[_frame_timing_tick_end] - frame_timing_mark[_frame_timing_tick_start] : 0;
		recent_render[recent_index] = frame_timing_mark[_frame_timing_render_end] ?
			frame_timing_mark[_frame_timing_render_end] - frame_timing_mark[_frame_timing_render_start] : 0;
		recent_index = (recent_index + 1) % RECENT_FRAMES;
		recent_count++;
	}
	if (!frame_timing_every)
	{
		if (event == _frame_timing_frame_start)
		{
			int index;
			for (index = 0; index < _frame_timing_event_count; index++)
				frame_timing_mark[index] = 0;
		}
		if (event < _frame_timing_event_count)
			frame_timing_mark[event] = frame_timing_now();
		return;
	}
	if (event == _frame_timing_frame_start)
	{
		int index;
		for (index = 0; index < _frame_timing_event_count; index++)
			frame_timing_mark[index] = 0;
	}
	frame_timing_mark[event] = frame_timing_now();
	if (event != _frame_timing_frame_end || !frame_timing_mark[_frame_timing_frame_start])
		return;
	frame = frame_timing_mark[_frame_timing_frame_end] - frame_timing_mark[_frame_timing_frame_start];
	if (threaded_tick)
	{
		frame_timing_sum[0] += threaded_tick;
		threaded_tick_sum += threaded_tick;
		threaded_tick = 0;
	}
	else if (frame_timing_mark[_frame_timing_tick_end])
		frame_timing_sum[0] += frame_timing_mark[_frame_timing_tick_end] - frame_timing_mark[_frame_timing_tick_start];
	if (frame_timing_mark[_frame_timing_render_end])
		frame_timing_sum[1] += frame_timing_mark[_frame_timing_render_end] - frame_timing_mark[_frame_timing_render_start];
	if (frame_timing_mark[_frame_timing_present_end])
		frame_timing_sum[2] += frame_timing_mark[_frame_timing_present_end] - frame_timing_mark[_frame_timing_present_start];
	frame_timing_sum[3] += frame;
	if (frame > frame_timing_max_frame)
		frame_timing_max_frame = frame;
	if (game_ticks < frame_timing_ticks_start)
		frame_timing_ticks_start = game_ticks; /* a new map */
	frame_timing_ticks_last = game_ticks;
	if (++frame_timing_frames >= (unsigned long)frame_timing_every)
	{
		double frames = (double)frame_timing_frames;
		unsigned long ticks = frame_timing_ticks_last - frame_timing_ticks_start;
		double total_ms = (double)frame_timing_sum[3] / 1e6;

		platform_log("frame-timing: game tick %lu frames %lu ticks %lu | frame %.2f ms (max %.2f) | ticks %.2f ms/frame %.2f ms/tick | render %.2f | throttle+present %.2f | other %.2f | %.1f fps %.1f ticks/s",
			game_ticks, frame_timing_frames, ticks, total_ms / frames, (double)frame_timing_max_frame / 1e6,
			(double)frame_timing_sum[0] / 1e6 / frames, ticks ? (double)frame_timing_sum[0] / 1e6 / (double)ticks : 0.0,
			(double)frame_timing_sum[1] / 1e6 / frames, (double)frame_timing_sum[2] / 1e6 / frames,
			(double)(frame_timing_sum[3] + threaded_tick_sum - frame_timing_sum[0] - frame_timing_sum[1] - frame_timing_sum[2]) / 1e6 / frames,
			frames * 1000.0 / total_ms, (double)ticks * 1000.0 / total_ms);
		frame_timing_frames = 0;
		frame_timing_ticks_start = frame_timing_ticks_last;
		frame_timing_max_frame = 0;
		frame_timing_sum[0] = frame_timing_sum[1] = frame_timing_sum[2] = frame_timing_sum[3] = 0;
		threaded_tick_sum = 0;
	}
}
