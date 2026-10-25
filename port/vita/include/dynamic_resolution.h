/*
DYNAMIC_RESOLUTION.H

The dynamic resolution's controller (port/vita/platform/dynamic_resolution.c):
from the GPU's time for each frame, the render scale of the next, in 32nds
of the full size (16 = 50%, 32 = 100%), between a floor and a ceiling.
Pure arithmetic, no platform calls (port/vita/tests/dynamic_resolution_test.c).

Down fast: two frames in a row over the high mark (92% of the frame budget,
33.3 ms at 30 FPS), the GPU being what ended them late (its tail after the
last submission at least a fifth of its time), and the scale drops by as
many steps as the pixel count says bring the frame to 85% of the budget (1
to 4 steps at once). Up more slowly: the next step up has to fit within 80%
of the budget (the GPU time taken as growing with the pixels, which
overestimates the growth) for 1 s, a frame that does not fit taking back
three times its time, and 5 s when that step is where the budget was last
broken (in the last 20 s); then up to 3 steps at once, as many as fit by
the smoothed GPU time. With a fight floor (under the floor), the frames at
the floor kept over the budget for half a second let the scale go under
the floor, as far as the fight floor; it rises as from any step down, and
the floor holds again once it is back there. Frames measured at
another scale than the current (the GPU runs behind) are not counted.
*/

#ifndef __HALO_DYNAMIC_RESOLUTION_H
#define __HALO_DYNAMIC_RESOLUTION_H

#define DYNRES_UNITS 32

struct dynres_controller
{
	/* the range, in 32nds, and the frame budget */
	int floor, ceiling;
	float budget_ms;
	/* the scale now, in 32nds */
	int level;
	/* frames in a row over the high mark */
	int slow_frames;
	/* time in a row the next step up fitted */
	float headroom_ms;
	/* the level the budget was last broken at (0: none lately), and how
	long ago */
	int failed_level;
	float failed_age_ms;
	/* the GPU time smoothed, and the level it was measured at */
	float smoothed_gpu_ms;
	int smoothed_level;
	/* changes made (for the log) */
	unsigned long changes;
	/* the fight floor (0: none): below the floor, at most to it, once
	the frames at the floor have kept over the budget for DYNRES_FIGHT_MS
	(dynres_controller_fight_floor); the time they have, and whether the
	scale may be under the floor now (until it is back at the floor) */
	int fight_floor;
	float fight_ms;
	int under_floor;
};

/* the fraction of the budget that is too slow, the one a step down aims
for, and the one a step up has to fit within */
#define DYNRES_HIGH 0.92f
#define DYNRES_AIM 0.85f
#define DYNRES_ROOM 0.80f
/* the most steps down, and up, at once */
#define DYNRES_MAXIMUM_DROP 4
#define DYNRES_MAXIMUM_RISE 3
/* frames over the high mark in a row before a step down */
#define DYNRES_SLOW_FRAMES 2
/* the time a step up has to fit for, and after a broken budget there */
#define DYNRES_HOLD_MS 1000.0f
#define DYNRES_HOLD_FAILED_MS 5000.0f
#define DYNRES_FAILED_MEMORY_MS 20000.0f

/* the time the frames at the floor have to keep over the budget (frames
that fit taking it back) before the scale goes under the floor, to the
fight floor */
#define DYNRES_FIGHT_MS 500.0f

/* starts at the ceiling */
void dynres_controller_init(struct dynres_controller *controller, int floor, int ceiling, float budget_ms);
/* starts at level (within the range): switched on in a game, the scale
drawn until then, from which it climbs as after a step down */
void dynres_controller_start(struct dynres_controller *controller, int floor, int ceiling, float budget_ms,
	int level);
/* the fight floor, in 32nds: 0 none, else under the floor - a fight too
heavy for the floor goes down to it, and comes back up as after any step
down */
void dynres_controller_fight_floor(struct dynres_controller *controller, int fight_floor);
/* a new range or budget, keeping the level within it */
void dynres_controller_limits(struct dynres_controller *controller, int floor, int ceiling, float budget_ms);
/* one frame the GPU finished: its GPU time and tail (vita_gxm.h
vgxm_gpu_frame), the level it was drawn at and the time since the frame
before; returns the level for the frames from now on */
int dynres_controller_frame(struct dynres_controller *controller, float gpu_ms, float tail_ms, int frame_level,
	float interval_ms);

#endif
