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
	if (level < controller->ceiling &&
		gpu_ms * (float)(level + 1) * (float)(level + 1) / ((float)level * (float)level) < budget * DYNRES_ROOM)
	{
		float hold = controller->failed_level && level + 1 >= controller->failed_level ? DYNRES_HOLD_FAILED_MS :
			DYNRES_HOLD_MS;

		controller->headroom_ms += interval_ms;
		if (controller->headroom_ms >= hold)
		{
			controller->headroom_ms = 0.0f;
			controller->level = level + 1;
			controller->changes++;
		}
	}
	else
		controller->headroom_ms = 0.0f;
	return controller->level;
}
