/*
FORGE_TOUCH_TEST.C

What the fingers did in Forge's monitor (port/linux/game/forge_touch.c): taps
and drags told apart, a finger in a touch zone never counting, a second
finger's turn, a resting third. What the gestures do to objects is the game
run's: run_forge_test.sh touch.
*/

#include "forge_touch.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;

static void check(int condition, char const *what)
{
	checks++;
	if (!condition)
	{
		failures++;
		printf("FAIL: %s\n", what);
	}
}

static struct forge_touch_gesture gesture;
static struct forge_touch_events events;
static unsigned long now;

static void frame(int count, struct forge_touch_finger const *fingers)
{
	now += 33;
	forge_touch_gesture_update(&gesture, fingers, count, now, &events);
}

static void finger(struct forge_touch_finger *out, int id, int x, int y, int reserved)
{
	out->id = id;
	out->x = (short)x;
	out->y = (short)y;
	out->reserved = reserved;
}

static void nothing(void)
{
	static struct forge_touch_events none;

	frame(0, NULL);
	check(!memcmp(&events, &none, sizeof(none)), "nothing down: nothing happens");
}

int main(void)
{
	struct forge_touch_finger f[3];
	int step;

	nothing();
	/* a tap */
	finger(&f[0], 1, 300, 200, 0);
	frame(1, f);
	check(!events.tap && !events.drag_began, "down: nothing yet");
	f[0].x += 10;
	frame(1, f);
	check(!events.drag_began, "within the slop: no drag");
	frame(0, NULL);
	check(events.tap && events.tap_x == 300 && events.tap_y == 200 && !events.drag_ended, "lifted soon: a tap, where it came down");
	nothing();
	/* a press too long is no tap */
	frame(1, f);
	now += FORGE_TOUCH_TAP_MILLISECONDS + 1;
	frame(1, f);
	frame(0, NULL);
	check(!events.tap, "held too long: no tap");
	/* a drag */
	finger(&f[0], 2, 100, 100, 0);
	frame(1, f);
	f[0].x = 130;
	frame(1, f);
	check(events.drag_began && events.began_x == 100 && events.began_y == 100, "past the slop: a drag, from where it came down");
	check(events.dragging && events.move_x == 30 && events.move_y == 0, "its move starts at once");
	f[0].x = 140;
	f[0].y = 90;
	frame(1, f);
	check(events.dragging && !events.drag_began && events.move_x == 10 && events.move_y == -10, "its move each frame");
	f[0].x = 100;
	f[0].y = 100;
	frame(1, f);
	frame(0, NULL);
	check(events.drag_ended && !events.tap, "a drag brought back and lifted: no tap");
	nothing();
	/* a finger in a touch zone */
	finger(&f[0], 3, 40, 40, 1);
	frame(1, f);
	f[0].reserved = 0;
	f[0].x = 300;
	f[0].y = 300;
	frame(1, f);
	check(!events.drag_began && !events.dragging, "come down in a zone: it never counts, slid off or not");
	frame(0, NULL);
	check(!events.tap, "lifted: no tap");
	/* its place taken by another finger, that does count */
	finger(&f[0], 4, 300, 200, 1);
	finger(&f[1], 5, 200, 200, 0);
	frame(2, f);
	f[1].x = 260;
	frame(2, f);
	check(events.drag_began && events.move_x == 60, "a finger resting in a zone leaves the next as the first");
	frame(0, NULL);
	nothing();
	/* two fingers: the second's move, while the first drags */
	finger(&f[0], 6, 100, 100, 0);
	frame(1, f);
	f[0].x = 150;
	frame(1, f);
	finger(&f[1], 7, 400, 300, 0);
	frame(2, f);
	check(events.dragging && !events.second_x, "the second finger down: no move yet");
	f[1].x = 420;
	f[1].y = 290;
	frame(2, f);
	check(events.second_x == 20 && events.second_y == -10 && events.move_x == 0, "the second finger's move");
	/* a third counts for nothing */
	finger(&f[2], 8, 500, 300, 0);
	frame(3, f);
	f[2].x = 560;
	frame(3, f);
	check(!events.second_x && !events.move_x, "a third finger counts for nothing");
	/* the second lifted, then down again: the drag goes on */
	frame(1, f);
	check(events.dragging && !events.drag_ended, "the second lifted: the first still drags");
	frame(2, f);
	check(events.dragging && !events.second_x, "a second finger again: no jump");
	/* the first lifted with the second down: it ends, the second counts for nothing until lifted */
	frame(1, &f[1]);
	check(events.drag_ended, "the first lifted: the drag ends");
	f[1].x += 80;
	frame(1, &f[1]);
	check(!events.drag_began && !events.dragging, "the second left down starts nothing");
	frame(0, NULL);
	check(!events.tap, "and is no tap");
	/* two fingers down at once are no tap */
	finger(&f[0], 9, 100, 100, 0);
	finger(&f[1], 10, 200, 100, 0);
	frame(2, f);
	frame(0, NULL);
	check(!events.tap, "two fingers together: no tap");
	nothing();
	/* more fingers than the slots */
	{
		struct forge_touch_finger many[12];

		for (step = 0; step < 12; step++)
			finger(&many[step], 20 + step, 100 + step, 100, 0);
		frame(12, many);
		frame(12, many);
		frame(0, NULL);
		nothing();
	}
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures != 0;
}
