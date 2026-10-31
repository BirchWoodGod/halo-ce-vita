/*
FORGE_TOUCH.H

The Vita's front screen in Forge's monitor: what the fingers did this frame
(forge_touch.c), kept apart from the game so that its rules are tested on
their own (port/vita/tests/forge_touch_test.c). What each gesture does to an
object is forge_touch.inc's, through the pad's own grab, turn and drop.

Positions are in the menus' 640x480 (vita_touch_to_menus, the tests' "forge
touch"), so negative or past 640 in a wide screen's margins.
*/

#ifndef __FORGE_TOUCH_H
#define __FORGE_TOUCH_H

/* a finger moved this far from where it came down is a drag, as far as
the menus' pointer's tap goes (VITA_TAP_SLOP, in the menus' pixels); lifted
sooner, and within this time, a tap */
#define FORGE_TOUCH_SLOP 21
#define FORGE_TOUCH_TAP_MILLISECONDS 600

#define FORGE_TOUCH_MAXIMUM_FINGERS 8

struct forge_touch_finger
{
	int id;
	short x, y;
	/* it stands on a place kept for something else (the Vita's touch
	zones): if it comes down here it never counts, slid off or not */
	int reserved;
};

struct forge_touch_gesture
{
	/* every finger down, the first two counted */
	struct
	{
		int used, id, counted;
	} fingers[FORGE_TOUCH_MAXIMUM_FINGERS];
	int first_down, first_id, first_dragging, first_no_tap;
	short first_start_x, first_start_y, first_x, first_y;
	unsigned long first_since;
	int second_down, second_id;
	short second_x, second_y;
};

/* this frame's, all zero when nothing happened */
struct forge_touch_events
{
	int tap;
	short tap_x, tap_y;
	/* a drag began: at began_x, began_y, where the finger came down */
	int drag_began;
	short began_x, began_y;
	/* a drag is on (from the frame it began): how far it went this frame */
	int dragging;
	int move_x, move_y;
	/* a drag's finger lifted */
	int drag_ended;
	/* how far a second finger went this frame, while the first drags */
	int second_x, second_y;
};

/* the fingers on the screen this frame (count at most
FORGE_TOUCH_MAXIMUM_FINGERS; none lifts them all). A third finger, or the
second once the first has lifted, counts for nothing until it lifts */
void forge_touch_gesture_update(struct forge_touch_gesture *gesture, struct forge_touch_finger const *fingers, int count,
	unsigned long now_milliseconds, struct forge_touch_events *events);

#endif
