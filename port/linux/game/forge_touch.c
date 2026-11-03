/*
FORGE_TOUCH.C

What the fingers did in Forge's monitor: forge_touch.h. No game state here.
*/

#include "forge_touch.h"

#include <string.h>

static int forge_touch_distance(int a, int b)
{
	return a > b ? a - b : b - a;
}

void forge_touch_gesture_update(struct forge_touch_gesture *gesture, struct forge_touch_finger const *fingers, int count,
	unsigned long now_milliseconds, struct forge_touch_events *events)
{
	int index, slot;
	int first_seen = 0, second_seen = 0;

	memset(events, 0, sizeof(*events));
	if (count > FORGE_TOUCH_MAXIMUM_FINGERS)
		count = FORGE_TOUCH_MAXIMUM_FINGERS;
	/* (fingers gone: their slots free) */
	for (slot = 0; slot < FORGE_TOUCH_MAXIMUM_FINGERS; slot++)
	{
		if (!gesture->fingers[slot].used)
			continue;
		for (index = 0; index < count; index++)
			if (fingers[index].id == gesture->fingers[slot].id)
				break;
		if (index == count)
			gesture->fingers[slot].used = 0;
	}
	for (index = 0; index < count; index++)
	{
		struct forge_touch_finger const *finger = &fingers[index];
		int free_slot = -1;

		for (slot = 0; slot < FORGE_TOUCH_MAXIMUM_FINGERS; slot++)
		{
			if (gesture->fingers[slot].used && gesture->fingers[slot].id == finger->id)
				break;
			if (!gesture->fingers[slot].used && free_slot < 0)
				free_slot = slot;
		}
		if (slot == FORGE_TOUCH_MAXIMUM_FINGERS)
		{
			/* (come down: the first and the second count, wherever they go
			from here, unless on a place kept for something else) */
			if (free_slot < 0)
				continue;
			slot = free_slot;
			gesture->fingers[slot].used = 1;
			gesture->fingers[slot].id = finger->id;
			gesture->fingers[slot].counted = 0;
			if (finger->reserved)
				continue;
			if (!gesture->first_down)
			{
				gesture->fingers[slot].counted = 1;
				gesture->first_down = 1;
				gesture->first_id = finger->id;
				gesture->first_dragging = 0;
				gesture->first_no_tap = 0;
				gesture->first_start_x = gesture->first_x = finger->x;
				gesture->first_start_y = gesture->first_y = finger->y;
				gesture->first_since = now_milliseconds;
			}
			else if (!gesture->second_down)
			{
				gesture->fingers[slot].counted = 1;
				gesture->second_down = 1;
				gesture->second_id = finger->id;
				gesture->second_x = finger->x;
				gesture->second_y = finger->y;
				/* (two fingers down: no tap) */
				gesture->first_no_tap = 1;
			}
		}
		if (!gesture->fingers[slot].counted)
			continue;
		if (gesture->first_down && finger->id == gesture->first_id)
		{
			first_seen = 1;
			if (!gesture->first_dragging && (forge_touch_distance(finger->x, gesture->first_start_x) > FORGE_TOUCH_SLOP ||
				forge_touch_distance(finger->y, gesture->first_start_y) > FORGE_TOUCH_SLOP))
			{
				gesture->first_dragging = 1;
				events->drag_began = 1;
				events->began_x = gesture->first_start_x;
				events->began_y = gesture->first_start_y;
			}
			if (gesture->first_dragging)
			{
				events->dragging = 1;
				events->move_x = finger->x - gesture->first_x;
				events->move_y = finger->y - gesture->first_y;
			}
			gesture->first_x = finger->x;
			gesture->first_y = finger->y;
		}
		else if (gesture->second_down && finger->id == gesture->second_id)
		{
			second_seen = 1;
			if (gesture->first_dragging)
			{
				events->second_x = finger->x - gesture->second_x;
				events->second_y = finger->y - gesture->second_y;
			}
			gesture->second_x = finger->x;
			gesture->second_y = finger->y;
		}
	}
	if (gesture->first_down && !first_seen)
	{
		if (gesture->first_dragging)
			events->drag_ended = 1;
		else if (!gesture->first_no_tap &&
			now_milliseconds - gesture->first_since <= FORGE_TOUCH_TAP_MILLISECONDS)
		{
			events->tap = 1;
			events->tap_x = gesture->first_start_x;
			events->tap_y = gesture->first_start_y;
		}
		gesture->first_down = 0;
		gesture->first_dragging = 0;
		/* (the second stays down, for nothing, until it lifts) */
		if (gesture->second_down)
		{
			for (slot = 0; slot < FORGE_TOUCH_MAXIMUM_FINGERS; slot++)
				if (gesture->fingers[slot].used && gesture->fingers[slot].id == gesture->second_id)
					gesture->fingers[slot].counted = 0;
			gesture->second_down = 0;
			second_seen = 0;
		}
	}
	if (gesture->second_down && !second_seen)
		gesture->second_down = 0;
}
