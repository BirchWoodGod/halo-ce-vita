/*
VITA_CONTROLS_TEST.C

A desktop test of the controls' mapping layer (port/vita/host/vita_controls.c,
included whole): where the touch zones are, a rear finger counting only once
held VITA_TOUCH_REAR_HOLD_US and a front one at once, a finger that starts
outside every zone never counting, several fingers each on their own, the
default layout (Xita's, as vita_pad.c had it), the touch zones' Xbox
buttons in play and not in the menus, the crouch toggle (the left stick's
click) from a button and a zone, the Xbox buttons moved to other Vita
buttons, and the settings read from the environment.

Run port/vita/tests/run_vita_controls_test.sh.
*/

#include "../host/vita_controls.c"

#include <stdio.h>

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

static struct vita_touch_contact contact(int panel, int id, int x, int y)
{
	struct vita_touch_contact result;

	result.panel = panel;
	result.id = id;
	result.x = x;
	result.y = y;
	return result;
}

#define ZONE(zone) (1UL << (zone))

static void test_zones(void)
{
	check(vita_touch_zone_at(VITA_TOUCH_FRONT, 10, 10) == VITA_ZONE_TOP_LEFT, "front top left corner");
	check(vita_touch_zone_at(VITA_TOUCH_FRONT, 159, 119) == VITA_ZONE_TOP_LEFT, "front top left: its last pixel");
	check(vita_touch_zone_at(VITA_TOUCH_FRONT, 160, 10) == -1 && vita_touch_zone_at(VITA_TOUCH_FRONT, 10, 120) == -1,
		"front top left: right and bottom edges excluded");
	check(vita_touch_zone_at(VITA_TOUCH_FRONT, 950, 5) == VITA_ZONE_TOP_RIGHT, "front top right corner");
	check(vita_touch_zone_at(VITA_TOUCH_FRONT, 20, 300) == VITA_ZONE_LEFT_EDGE, "front left edge");
	check(vita_touch_zone_at(VITA_TOUCH_FRONT, 940, 300) == VITA_ZONE_RIGHT_EDGE, "front right edge");
	check(vita_touch_zone_at(VITA_TOUCH_FRONT, 480, 272) == -1, "front centre: no zone (the game's view)");
	check(vita_touch_zone_at(VITA_TOUCH_FRONT, 40, 500) == -1, "front bottom left: no zone (the motion tracker)");
	check(vita_touch_zone_at(VITA_TOUCH_FRONT, 900, 500) == -1, "front bottom right: no zone");
	check(vita_touch_zone_at(VITA_TOUCH_FRONT, 20, 150) == -1, "front, between the top corner and the edge: none");
	check(vita_touch_zone_at(VITA_TOUCH_REAR, 100, 100) == VITA_ZONE_REAR_LEFT, "rear left half");
	check(vita_touch_zone_at(VITA_TOUCH_REAR, 900, 500) == VITA_ZONE_REAR_RIGHT, "rear right half");
	check(vita_touch_zone_at(VITA_TOUCH_REAR, 480, 272) == -1, "rear middle strip: neither half");
	check(vita_touch_zone_at(VITA_TOUCH_REAR, 10, 10) != VITA_ZONE_TOP_LEFT, "a rear finger is never a front zone");
	{
		int zone, overlap = 0, x, y;

		for (y = 0; y < VITA_TOUCH_HEIGHT; y += 4)
			for (x = 0; x < VITA_TOUCH_WIDTH; x += 4)
			{
				int hits[2] = { 0, 0 }, panel;

				for (panel = 0; panel < 2; panel++)
					for (zone = 0; zone < VITA_ZONE_COUNT; zone++)
					{
						const struct vita_touch_zone *rectangle = &vita_touch_zones[zone];

						hits[panel] += rectangle->panel == panel && x >= rectangle->left && x < rectangle->right &&
							y >= rectangle->top && y < rectangle->bottom;
					}
				overlap |= hits[0] > 1 || hits[1] > 1;
			}
		check(!overlap, "no two zones of a panel overlap");
	}
}

static void test_hold(void)
{
	struct vita_touch_tracker tracker;
	struct vita_touch_contact fingers[4];
	unsigned long long t = 5000000;
	int started = -1;

	memset(&tracker, 0, sizeof(tracker));
	fingers[0] = contact(VITA_TOUCH_FRONT, 3, 50, 50);
	check(vita_touch_update(&tracker, fingers, 1, t, &started) == ZONE(VITA_ZONE_TOP_LEFT) && started == 1,
		"front: counts on its first frame, one finger started");
	check(vita_touch_update(&tracker, fingers, 1, t + 16000, &started) == ZONE(VITA_ZONE_TOP_LEFT) && started == 0,
		"front: still held, nothing new started");
	check(vita_touch_update(&tracker, fingers, 0, t + 32000, &started) == 0, "front: lifted, released");

	/* the rear: only after its hold time */
	t += 1000000;
	fingers[0] = contact(VITA_TOUCH_REAR, 1, 100, 300);
	check(vita_touch_update(&tracker, fingers, 1, t, NULL) == 0, "rear: not on its first frame");
	check(vita_touch_update(&tracker, fingers, 1, t + VITA_TOUCH_REAR_HOLD_US - 1, NULL) == 0,
		"rear: not 1 us before the hold time");
	check(vita_touch_update(&tracker, fingers, 1, t + VITA_TOUCH_REAR_HOLD_US, NULL) == ZONE(VITA_ZONE_REAR_LEFT),
		"rear: counts once held the hold time");
	/* (a brush: down for less than the hold time, never counts) */
	vita_touch_update(&tracker, fingers, 0, t + 200000, NULL);
	t += 1000000;
	fingers[0] = contact(VITA_TOUCH_REAR, 1, 800, 300);
	check(vita_touch_update(&tracker, fingers, 1, t, NULL) == 0 &&
		vita_touch_update(&tracker, fingers, 1, t + 50000, NULL) == 0 &&
		vita_touch_update(&tracker, fingers, 0, t + 70000, NULL) == 0 &&
		vita_touch_update(&tracker, fingers, 0, t + 200000, NULL) == 0, "rear: a 70 ms brush never counts");
	/* (the same id coming down again starts its hold time over) */
	t += 1000000;
	fingers[0] = contact(VITA_TOUCH_REAR, 1, 800, 300);
	vita_touch_update(&tracker, fingers, 1, t, NULL);
	vita_touch_update(&tracker, fingers, 0, t + 60000, NULL);
	check(vita_touch_update(&tracker, fingers, 1, t + 120000, NULL) == 0, "rear: lifted and down again: timed afresh");
	check(vita_touch_update(&tracker, fingers, 1, t + 220000, NULL) == ZONE(VITA_ZONE_REAR_RIGHT),
		"rear: then counts after its own hold time");
	vita_touch_update(&tracker, fingers, 0, t + 300000, NULL);

	/* a finger that starts outside every zone never counts, even moved in;
	one that starts in a zone counts for it while down, moved or not */
	t += 1000000;
	fingers[0] = contact(VITA_TOUCH_FRONT, 7, 480, 272);
	check(vita_touch_update(&tracker, fingers, 1, t, NULL) == 0, "front: a finger in the middle holds nothing");
	fingers[0] = contact(VITA_TOUCH_FRONT, 7, 50, 50);
	check(vita_touch_update(&tracker, fingers, 1, t + 16000, NULL) == 0,
		"front: moved into a corner after starting outside: still nothing");
	vita_touch_update(&tracker, fingers, 0, t + 32000, NULL);
	fingers[0] = contact(VITA_TOUCH_FRONT, 8, 50, 50);
	vita_touch_update(&tracker, fingers, 1, t + 48000, NULL);
	fingers[0] = contact(VITA_TOUCH_FRONT, 8, 300, 200);
	check(vita_touch_update(&tracker, fingers, 1, t + 64000, NULL) == ZONE(VITA_ZONE_TOP_LEFT),
		"front: started in a corner and slid out: still that zone");
	vita_touch_update(&tracker, fingers, 0, t + 80000, NULL);

	/* several fingers, each on its own */
	t += 1000000;
	fingers[0] = contact(VITA_TOUCH_FRONT, 1, 900, 50);
	fingers[1] = contact(VITA_TOUCH_REAR, 1, 100, 100);
	fingers[2] = contact(VITA_TOUCH_FRONT, 2, 30, 300);
	check(vita_touch_update(&tracker, fingers, 3, t, &started) == (ZONE(VITA_ZONE_TOP_RIGHT) | ZONE(VITA_ZONE_LEFT_EDGE)) &&
		started == 3, "three fingers: the two front zones at once (the same id on both panels kept apart)");
	check(vita_touch_update(&tracker, fingers, 3, t + 150000, NULL) ==
		(ZONE(VITA_ZONE_TOP_RIGHT) | ZONE(VITA_ZONE_LEFT_EDGE) | ZONE(VITA_ZONE_REAR_LEFT)), "then the rear one too");
	fingers[0] = fingers[2];
	check(vita_touch_update(&tracker, fingers, 1, t + 160000, NULL) == ZONE(VITA_ZONE_LEFT_EDGE),
		"two lifted: the one left still counts");
	fingers[1] = contact(VITA_TOUCH_FRONT, 4, 40, 320);
	check(vita_touch_update(&tracker, fingers, 2, t + 170000, NULL) == ZONE(VITA_ZONE_LEFT_EDGE),
		"two fingers on one zone: one bit");
	check(vita_touch_update(&tracker, fingers + 1, 1, t + 180000, NULL) == ZONE(VITA_ZONE_LEFT_EDGE),
		"one of the two lifted: the zone still held");
	vita_touch_update(&tracker, fingers, 0, t + 190000, NULL);
	{
		struct vita_touch_contact many[VITA_TOUCH_TRACKED + 4];
		int index;

		for (index = 0; index < VITA_TOUCH_TRACKED + 4; index++)
			many[index] = contact(VITA_TOUCH_FRONT, 20 + index, 40, 300);
		check(vita_touch_update(&tracker, many, VITA_TOUCH_TRACKED + 4, t + 200000, &started) ==
			ZONE(VITA_ZONE_LEFT_EDGE) && started == VITA_TOUCH_TRACKED, "more fingers than followed: the rest ignored");
		check(vita_touch_update(&tracker, many, 0, t + 210000, NULL) == 0, "all lifted");
	}
}

static struct vita_controls_output map(const struct vita_controls_config *config, struct vita_controls_state *state,
	unsigned long buttons, unsigned long touch, int menus)
{
	struct vita_controls_output output;

	vita_controls_map(config, state, buttons, touch, menus, &output);
	return output;
}

static void clear_environment(void)
{
	int index;

	for (index = 0; index < VITA_ZONE_COUNT; index++)
		unsetenv(vita_touch_variables[index]);
	for (index = 0; index < VITA_XBOX_COUNT; index++)
		if (vita_xbox_variables[index])
			unsetenv(vita_xbox_variables[index]);
	unsetenv("HALO_CROUCH_TOGGLE");
}

static void test_mapping(void)
{
	struct vita_controls_config config;
	struct vita_controls_state state;
	struct vita_controls_output out;
	int index, zone;

	clear_environment();
	vita_controls_config_load(&config);
	memset(&state, 0, sizeof(state));
	for (index = 0, zone = 0; index < VITA_ZONE_COUNT; index++)
		zone |= config.zone_xbox[index] != VITA_XBOX_OFF;
	check(!zone && config.crouch_toggle == 1, "defaults: every zone Off, crouch a toggle");

	/* Xita's layout, in play */
	out = map(&config, &state, VITA_BUTTON_CROSS, 0, 0);
	check(out.analog[0] == 255 && !out.analog[1] && !out.digital, "play: Cross is A (jump)");
	out = map(&config, &state, VITA_BUTTON_CIRCLE | VITA_BUTTON_SQUARE | VITA_BUTTON_TRIANGLE, 0, 0);
	check(out.analog[1] == 255 && out.analog[2] == 255 && out.analog[3] == 255, "play: Circle Square Triangle: B X Y");
	out = map(&config, &state, VITA_BUTTON_L | VITA_BUTTON_R, 0, 0);
	check(out.analog[6] == 255 && out.analog[7] == 255, "play: L and R: the triggers");
	out = map(&config, &state, VITA_BUTTON_UP, 0, 0);
	check(out.digital == VITA_PAD_RIGHT_THUMB, "play: D-pad up: right stick click (zoom), no D-pad bit");
	out = map(&config, &state, VITA_BUTTON_LEFT | VITA_BUTTON_RIGHT, 0, 0);
	check(out.analog[4] == 255 && out.analog[5] == 255 && !out.digital, "play: D-pad left / right: Black / White");
	out = map(&config, &state, VITA_BUTTON_START | VITA_BUTTON_SELECT, 0, 0);
	check(out.digital == (VITA_PAD_START | VITA_PAD_BACK), "play: Start and Select: Start and Back");
	/* the crouch toggle on D-pad down */
	out = map(&config, &state, VITA_BUTTON_DOWN, 0, 0);
	check(out.digital == VITA_PAD_LEFT_THUMB, "play: D-pad down crouches");
	out = map(&config, &state, 0, 0, 0);
	check(out.digital == VITA_PAD_LEFT_THUMB, "play: released: still crouched (toggle)");
	map(&config, &state, VITA_BUTTON_DOWN, 0, 0);
	out = map(&config, &state, 0, 0, 0);
	check(!out.digital, "play: pressed again: standing");

	/* the menus: the fixed layout, touch ignored */
	config.zone_xbox[VITA_ZONE_TOP_LEFT] = VITA_XBOX_A;
	out = map(&config, &state, VITA_BUTTON_UP | VITA_BUTTON_DOWN | VITA_BUTTON_CROSS, 0, 1);
	check(out.digital == (VITA_PAD_DPAD_UP | VITA_PAD_DPAD_DOWN) && out.analog[0] == 255, "menus: D-pad and A");
	out = map(&config, &state, 0, ZONE(VITA_ZONE_TOP_LEFT), 1);
	check(!out.digital && !out.analog[0], "menus: a touch zone does nothing");
	out = map(&config, &state, VITA_BUTTON_LEFT, 0, 1);
	check(out.digital == VITA_PAD_DPAD_LEFT && !out.analog[4], "menus: D-pad left is the D-pad, not Black");

	/* each Xbox button from a zone, in play */
	{
		static const struct
		{
			int xbox;
			unsigned long digital;
			int analog;
			const char *what;
		} expected[] = {
			{ VITA_XBOX_B, 0, 1, "zone B" },
			{ VITA_XBOX_LEFT_TRIGGER, 0, 6, "zone Left trigger" },
			{ VITA_XBOX_WHITE, 0, 5, "zone White" },
			{ VITA_XBOX_RIGHT_STICK, VITA_PAD_RIGHT_THUMB, -1, "zone Right stick: its click" },
			{ VITA_XBOX_X, 0, 2, "zone X" },
			{ VITA_XBOX_Y, 0, 3, "zone Y" },
			{ VITA_XBOX_BLACK, 0, 4, "zone Black" },
			{ VITA_XBOX_A, 0, 0, "zone A" },
			{ VITA_XBOX_BACK, VITA_PAD_BACK, -1, "zone Back" },
			{ VITA_XBOX_RIGHT_TRIGGER, 0, 7, "zone Right trigger" },
		};

		for (index = 0; index < (int)(sizeof(expected) / sizeof(expected[0])); index++)
		{
			int analog, others = 0;

			config.zone_xbox[VITA_ZONE_REAR_RIGHT] = expected[index].xbox;
			out = map(&config, &state, 0, ZONE(VITA_ZONE_REAR_RIGHT), 0);
			for (analog = 0; analog < 8; analog++)
				others |= analog != expected[index].analog && out.analog[analog];
			check(out.digital == expected[index].digital && !others &&
				(expected[index].analog < 0 || out.analog[expected[index].analog] == 255), expected[index].what);
		}
		config.zone_xbox[VITA_ZONE_REAR_RIGHT] = VITA_XBOX_OFF;
		out = map(&config, &state, 0, ZONE(VITA_ZONE_REAR_RIGHT), 0);
		check(!out.digital && !out.analog[0], "zone Off: nothing");
	}
	/* a zone and a button together, and a zone's crouch with the toggle
	and held */
	config.zone_xbox[VITA_ZONE_TOP_RIGHT] = VITA_XBOX_WHITE;
	out = map(&config, &state, VITA_BUTTON_R, ZONE(VITA_ZONE_TOP_RIGHT), 0);
	check(out.analog[5] == 255 && out.analog[7] == 255, "zone with a button: both (OR)");
	config.zone_xbox[VITA_ZONE_LEFT_EDGE] = VITA_XBOX_LEFT_STICK;
	memset(&state, 0, sizeof(state));
	out = map(&config, &state, 0, ZONE(VITA_ZONE_LEFT_EDGE), 0);
	check(out.digital == VITA_PAD_LEFT_THUMB, "zone Crouch (toggle): a touch crouches");
	out = map(&config, &state, 0, ZONE(VITA_ZONE_LEFT_EDGE), 0);
	check(out.digital == VITA_PAD_LEFT_THUMB, "zone Crouch (toggle): held, still crouched");
	out = map(&config, &state, 0, 0, 0);
	check(out.digital == VITA_PAD_LEFT_THUMB, "zone Crouch (toggle): lifted, still crouched");
	out = map(&config, &state, VITA_BUTTON_DOWN, ZONE(VITA_ZONE_LEFT_EDGE), 0);
	check(!out.digital, "zone Crouch (toggle): touch and D-pad down together are one press: standing");
	map(&config, &state, 0, 0, 0);
	config.crouch_toggle = 0;
	out = map(&config, &state, 0, ZONE(VITA_ZONE_LEFT_EDGE), 0);
	check(out.digital == VITA_PAD_LEFT_THUMB, "zone Crouch (hold): crouched while touched");
	out = map(&config, &state, 0, 0, 0);
	check(!out.digital, "zone Crouch (hold): standing when lifted");

	/* buttons moved */
	setenv("HALO_XBOX_A", "circle", 1);
	setenv("HALO_XBOX_B", "none", 1);
	setenv("HALO_XBOX_WHITE", "triangle", 1);
	setenv("HALO_XBOX_Y", "nonsense", 1);
	setenv("HALO_TOUCH_REAR_LEFT", "b", 1);
	setenv("HALO_XBOX_BACK", "l", 1);
	setenv("HALO_TOUCH_TOP_LEFT", "nonsense", 1);
	setenv("HALO_CROUCH_TOGGLE", "0", 1);
	vita_controls_config_load(&config);
	check(config.xbox_button[VITA_XBOX_A] == VITA_BUTTON_CIRCLE && !config.xbox_button[VITA_XBOX_B] &&
		config.xbox_button[VITA_XBOX_WHITE] == VITA_BUTTON_TRIANGLE &&
		config.xbox_button[VITA_XBOX_Y] == VITA_BUTTON_TRIANGLE,
		"settings: Xbox buttons moved; an unknown value keeps the shipped button");
	check(config.zone_xbox[VITA_ZONE_REAR_LEFT] == VITA_XBOX_B &&
		config.zone_xbox[VITA_ZONE_TOP_LEFT] == VITA_XBOX_OFF && config.crouch_toggle == 0,
		"settings: a zone's Xbox button; an unknown value is Off; crouch held");
	memset(&state, 0, sizeof(state));
	out = map(&config, &state, VITA_BUTTON_CIRCLE, 0, 0);
	check(out.analog[0] == 255 && !out.analog[1], "moved: Circle is A, B on no button");
	out = map(&config, &state, VITA_BUTTON_CROSS, 0, 0);
	check(!out.analog[0], "moved: Cross does nothing in play");
	out = map(&config, &state, VITA_BUTTON_TRIANGLE, 0, 0);
	check(out.analog[3] == 255 && out.analog[5] == 255, "moved: a Vita button on two Xbox buttons presses both");
	out = map(&config, &state, 0, ZONE(VITA_ZONE_REAR_LEFT), 0);
	check(out.analog[1] == 255, "moved: B from its zone");
	out = map(&config, &state, VITA_BUTTON_L, 0, 0);
	check(out.digital == VITA_PAD_BACK && out.analog[6] == 255, "moved: Back on L too (with the left trigger)");
	out = map(&config, &state, VITA_BUTTON_SELECT, 0, 0);
	check(!out.digital, "moved: Select no longer Back in play");
	out = map(&config, &state, VITA_BUTTON_SELECT, 0, 1);
	check(out.digital == VITA_PAD_BACK, "menus: Select stays Back");
	out = map(&config, &state, VITA_BUTTON_CROSS | VITA_BUTTON_CIRCLE, 0, 1);
	check(out.analog[0] == 255 && out.analog[1] == 255, "menus keep Cross A and Circle B whatever the buttons");
	clear_environment();
	vita_controls_config_load(&config);
	check(config.xbox_button[VITA_XBOX_A] == VITA_BUTTON_CROSS &&
		config.xbox_button[VITA_XBOX_BACK] == VITA_BUTTON_SELECT, "settings unset: shipped buttons");
	check(vita_xbox_named("black") == VITA_XBOX_BLACK && vita_xbox_named("rs") == VITA_XBOX_RIGHT_STICK &&
		vita_xbox_named(NULL) == 0, "Xbox button names");
	check(vita_button_named("right", 0) == VITA_BUTTON_RIGHT && vita_button_named("select", 0) == VITA_BUTTON_SELECT &&
		vita_button_named("none", 7) == 0 &&
		vita_button_named("x", 7) == 7, "button names");
}

int main(void)
{
	test_zones();
	test_hold();
	test_mapping();
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures ? 1 : 0;
}
