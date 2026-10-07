/*
DYNAMIC_RESOLUTION.C

The dynamic resolution's controller: the render scale for the next frames
from the GPU's time for the last (dynamic_resolution.h). d3d8_gxm.c feeds it
on the render worker, after each present, and draws the screen-sized
targets at its scale (vgxm_render_rect_set).
*/

#include "dynamic_resolution.h"

static int dynres_clamp(int level, int floor, int ceiling)
{
	return level < floor ? floor : level > ceiling ? ceiling : level;
}

void dynres_controller_limits(struct dynres_controller *controller, int floor, int ceiling, float budget_ms)
{
	if (ceiling > DYNRES_UNITS)
		ceiling = DYNRES_UNITS;
	if (ceiling < 1)
		ceiling = 1;
	if (floor > ceiling)
		floor = ceiling;
	if (floor < 1)
		floor = 1;
	controller->floor = floor;
	controller->ceiling = ceiling;
	controller->budget_ms = budget_ms > 1.0f ? budget_ms : 1000.0f / 30.0f;
	controller->level = dynres_clamp(controller->level, floor, ceiling);
}

void dynres_controller_init(struct dynres_controller *controller, int floor, int ceiling, float budget_ms)
{
	controller->level = DYNRES_UNITS;
	controller->slow_frames = 0;
	controller->headroom_ms = 0.0f;
	controller->failed_level = 0;
	controller->failed_age_ms = 0.0f;
	controller->changes = 0;
	controller->smoothed_gpu_ms = 0.0f;
	controller->smoothed_level = 0;
	dynres_controller_limits(controller, floor, ceiling, budget_ms);
}

int dynres_controller_frame(struct dynres_controller *controller, float gpu_ms, float tail_ms, int frame_level,
	float interval_ms)
{
	int level = controller->level;
	float budget = controller->budget_ms;

	/* (a hitch's interval counts as a frame's, not as seconds of room) */
	if (interval_ms < 0.0f)
		interval_ms = 0.0f;
	if (interval_ms > 100.0f)
		interval_ms = 100.0f;
	if (controller->failed_level)
	{
		controller->failed_age_ms += interval_ms;
		if (controller->failed_age_ms > DYNRES_FAILED_MEMORY_MS)
			controller->failed_level = 0;
	}
	/* (drawn at another scale: the GPU runs a frame or two behind) */
	if (frame_level != level || gpu_ms <= 0.0f)
		return level;
	/* (the GPU time smoothed over about a second, for the size of a step up;
	a frame of a new level starts it again) */
	if (controller->smoothed_level != level || controller->smoothed_gpu_ms <= 0.0f)
	{
		controller->smoothed_level = level;
		controller->smoothed_gpu_ms = gpu_ms;
	}
	else
		controller->smoothed_gpu_ms += (gpu_ms - controller->smoothed_gpu_ms) * 0.05f;
	if (gpu_ms > budget * DYNRES_HIGH && tail_ms >= gpu_ms * 0.2f)
		controller->slow_frames++;
	else
		controller->slow_frames = 0;
	if (controller->slow_frames >= DYNRES_SLOW_FRAMES)
	{
		controller->slow_frames = 0;
		controller->headroom_ms = 0.0f;
		if (level > controller->floor)
		{
			/* as many steps as the pixels say (the GPU time taken as
			theirs), 1 to DYNRES_MAXIMUM_DROP */
			int next = level - 1;

			while (next > controller->floor && level - next < DYNRES_MAXIMUM_DROP &&
				gpu_ms * (float)next * (float)next / ((float)level * (float)level) > budget * DYNRES_AIM)
				next--;
			controller->failed_level = level;
			controller->failed_age_ms = 0.0f;
			controller->level = next;
			controller->changes++;
		}
		return controller->level;
	}
	/* Up: the step above has to fit within the room for a while. A frame
	that does not (a spike, an explosion) takes back three times its own
	time rather than starting the wait again: on the Vita a spike came every
	second or so in a fight, and a scale that went down climbed one step a
	minute. With room for more, up to DYNRES_MAXIMUM_RISE steps at once, as
	the pixels say (from the smoothed GPU time), never onto the step the
	budget was last broken at before its longer hold. */
	if (level < controller->ceiling &&
		gpu_ms * (float)(level + 1) * (float)(level + 1) / ((float)level * (float)level) < budget * DYNRES_ROOM)
	{
		controller->headroom_ms += interval_ms;
	}
	else
	{
		controller->headroom_ms -= 3.0f * interval_ms;
		if (controller->headroom_ms < 0.0f)
			controller->headroom_ms = 0.0f;
	}
	if (level < controller->ceiling)
	{
		int failed_near = controller->failed_level && level + 1 >= controller->failed_level;
		float hold = failed_near ? DYNRES_HOLD_FAILED_MS : DYNRES_HOLD_MS;

		if (controller->headroom_ms >= hold)
		{
			float smoothed = controller->smoothed_gpu_ms > 0.0f ? controller->smoothed_gpu_ms : gpu_ms;
			int next = level + 1;

			while (next < controller->ceiling && next + 1 - level <= DYNRES_MAXIMUM_RISE &&
				(!controller->failed_level || next + 1 < controller->failed_level) &&
				smoothed * (float)(next + 1) * (float)(next + 1) / ((float)level * (float)level) < budget * DYNRES_ROOM)
				next++;
			controller->headroom_ms = 0.0f;
			controller->level = next;
			controller->changes++;
		}
	}
	return controller->level;
}
