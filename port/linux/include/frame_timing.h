/* frame_timing.h: HALO_FRAME_TIMING main loop timing (port/linux/src/frame_timing.c) */
#ifndef HALO_FRAME_TIMING_H
#define HALO_FRAME_TIMING_H

enum
{
	_frame_timing_frame_start,
	_frame_timing_tick_start,
	_frame_timing_tick_end,
	_frame_timing_render_start,
	_frame_timing_render_end,
	_frame_timing_present_start,
	_frame_timing_present_end,
	_frame_timing_frame_end,
	_frame_timing_event_count
};

void halo_frame_timing(int event, unsigned long game_ticks);
/* the threaded tick's own duration this frame (tick_thread.c) */
void halo_frame_timing_tick_threaded(unsigned long long tick_us);
/* the frame was presented; drawn between ticks (frame interpolation: from
the same tick as the frame before); with frame interpolation on, and two
frames a tick (main.c) */
void halo_frame_timing_presented(int presented, int between_ticks, int interpolation, int two_a_tick);
/* rolling averages of the last 30 frames */
void halo_frame_timing_recent(float *frame_ms, float *tick_ms, float *render_ms);

#endif
