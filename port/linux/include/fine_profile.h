/* fine_profile.h

HALO_PROFILE_SAMPLE=N: the fine timers of HALO_RENDER_PROFILE (inside the
objects phase: objects-profile, render_model split, model part split) and
of HALO_TICK_PROFILE=2/3 (objects-update, tick-detail) run in one frame
(one tick) in N, and their reports average over the frames (ticks) timed.
Each timer reads the clock, a 0.7 us system call on the Vita: timing every
draw and every object added 2-3 ms to the objects phase and to the tick it
measured. With N > 1 the other frames run untimed, the coarse phases
(render-profile, tick-profile) stay near their true cost, and each fine
report says how many clock reads a timed frame made and what they cost
(measured at start), so the reader can take that off. N = 1 (the default)
times every frame, as before. */

#ifndef __HALO_FINE_PROFILE_H
#define __HALO_FINE_PROFILE_H

/* this frame's fine render timers run (the main thread's), this tick's
fine tick timers run (the tick thread's) */
extern unsigned char halo_fine_render_on;
extern unsigned char halo_fine_tick_on;

/* the main thread, once a frame before the render: picks the frame */
void halo_fine_render_frame(void);
/* the tick thread, once a tick before the tick: picks the tick */
void halo_fine_tick_begin(void);

/* a clock read for a fine timer (counted for the overhead estimate) */
unsigned long long halo_fine_render_now(void);
unsigned long long halo_fine_tick_now(void);

/* a report's place in the counts: halo_fine_*_since() gives the frames
(ticks) timed since the report's last call and its note - "1 in N timed,
R clock reads a timed frame ~X ms of it" - and moves the mark on (each
report keeps its own mark, so reports on different counters agree) */
struct halo_fine_mark
{
	unsigned long timed, reads;
	char note[160];
};
unsigned long halo_fine_render_since(struct halo_fine_mark *mark);
unsigned long halo_fine_tick_since(struct halo_fine_mark *mark);

#endif
