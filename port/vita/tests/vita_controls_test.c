/*
VITA_CONTROLS_TEST.C

A desktop test of the controls' mapping layer (port/vita/host/vita_controls.c,
included whole): where the touch zones are, a rear finger counting only once
held VITA_TOUCH_REAR_HOLD_US and a front one at once, a finger that starts
outside every zone never counting, several fingers each on their own, the
default layout (Xita's, as vita_pad.c had it), the touch zones' Xbox
buttons in play and not in the menus, the crouch toggle (the left stick's
click) from a button and a zone, the Xbox buttons moved to other Vita
buttons, and the settings read from the environment; gyro aiming: rates
to angles, the deadzone, smoothing of tremor, the bias learnt only while
the Vita lies still, the yaw/roll and invert choices, the sensitivity, and
when the game gets the angles (on, zoomed, while holding, never in the
menus, dropped when not taken).

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

/* ---------- gyro aiming */

#define DEG 0.017453293f

static int near(float value, float wanted, float tolerance)
{
	return value > wanted - tolerance && value < wanted + tolerance;
}

/* `seconds` of samples every 5 ms at these rates (degrees a second) and
this accelerometer (g), noise of +-noise degrees a second alternating; the
angle turned (degrees) about each axis */
static void feed(struct vita_gyro_filter *filter, float x, float y, float z, const float accel[3], float noise,
	float seconds, int calibrate, float angle_degrees[3])
{
	float angle[3] = { 0.0f, 0.0f, 0.0f };
	int samples = (int)(seconds / 0.005f + 0.5f), index;

	for (index = 0; index < samples; index++)
	{
		float wobble = (index & 1 ? noise : -noise) * DEG;
		float rate[3];

		rate[0] = x * DEG + wobble;
		rate[1] = y * DEG - wobble;
		rate[2] = z * DEG + wobble;
		vita_gyro_filter_sample(filter, rate, accel, 0.005f, calibrate, angle);
	}
	for (index = 0; index < 3; index++)
		angle_degrees[index] = angle[index] / DEG;
}

static void gyro_environment(const char *mode, const char *button, const char *sensitivity, const char *invert,
	const char *turn)
{
	const char *names[] = { "HALO_GYRO", "HALO_GYRO_BUTTON", "HALO_GYRO_SENS", "HALO_GYRO_INVERT_Y", "HALO_GYRO_TURN" };
	const char *values[] = { mode, button, sensitivity, invert, turn };
	int index;

	for (index = 0; index < 5; index++)
		if (values[index])
			setenv(names[index], values[index], 1);
		else
			unsetenv(names[index]);
}

static void test_gyro_filter(void)
{
	static const float flat[3] = { 0.0f, 0.0f, 1.0f };
	struct vita_gyro_filter filter;
	float angle[3];

	memset(&filter, 0, sizeof(filter));
	feed(&filter, 0.0f, 30.0f, 0.0f, flat, 0.0f, 0.5f, 0, angle);
	check(near(angle[1], 15.0f - 0.375f, 0.1f) && near(angle[0], 0.0f, 0.001f) && near(angle[2], 0.0f, 0.001f),
		"gyro: 30 deg/s for 0.5 s about y turns 15 degrees (less the 0.75 deg/s deadzone), nothing else");
	memset(&filter, 0, sizeof(filter));
	feed(&filter, -90.0f, 0.0f, 0.0f, flat, 0.0f, 0.2f, 0, angle);
	check(near(angle[0], -18.0f + 0.15f, 0.1f), "gyro: -90 deg/s for 0.2 s about x: -18 degrees (fast: not smoothed)");
	memset(&filter, 0, sizeof(filter));
	feed(&filter, 0.4f, 0.5f, 0.0f, flat, 0.0f, 2.0f, 0, angle);
	check(angle[0] == 0.0f && angle[1] == 0.0f, "gyro: under the deadzone (0.64 deg/s) nothing turns");
	memset(&filter, 0, sizeof(filter));
	feed(&filter, 0.0f, 4.0f, 0.0f, flat, 0.0f, 2.0f, 0, angle);
	check(near(angle[1], (4.0f - 0.75f) * 2.0f, 0.3f), "gyro: a slow deliberate 4 deg/s turn still turns (smoothed)");
	memset(&filter, 0, sizeof(filter));
	feed(&filter, 0.0f, 0.0f, 0.0f, flat, 3.0f, 1.0f, 0, angle);
	check(near(angle[0], 0.0f, 0.02f) && near(angle[1], 0.0f, 0.02f) &&
		filter.rate[0] * filter.rate[0] < (0.5f * DEG) * (0.5f * DEG),
		"gyro: a +-3 deg/s tremor at 100 Hz is smoothed away (no aim moves)");
	memset(&filter, 0, sizeof(filter));
	{
		float angle_single[3] = { 0.0f, 0.0f, 0.0f };
		float rate[3] = { 0.0f, 50.0f * DEG, 0.0f };

		vita_gyro_filter_sample(&filter, rate, flat, 0.2f, 0, angle_single);
		check(angle_single[1] == 0.0f, "gyro: a sample after a gap of 0.2 s integrates nothing");
	}

	/* the bias: learnt only while still */
	memset(&filter, 0, sizeof(filter));
	feed(&filter, 2.0f, -1.0f, 0.5f, flat, 0.0f, 3.0f, 0, angle);
	check(angle[0] > 3.0f && angle[1] < -0.5f, "gyro: a 2 deg/s bias, not learnt (the pad script's samples), drifts");
	memset(&filter, 0, sizeof(filter));
	feed(&filter, 2.0f, -1.0f, 0.5f, flat, 0.3f, 1.2f, 1, angle);
	check(filter.still_count == 1 && near(filter.bias[0], 2.0f * DEG, 0.05f * DEG) &&
		near(filter.bias[1], -1.0f * DEG, 0.05f * DEG), "gyro: a second lying still (noise 0.3 deg/s) learns the bias");
	feed(&filter, 2.0f, -1.0f, 0.5f, flat, 0.0f, 3.0f, 1, angle);
	check(near(angle[0], 0.0f, 0.05f) && near(angle[1], 0.0f, 0.05f) && near(angle[2], 0.0f, 0.05f),
		"gyro: once learnt the bias no longer moves the aim");
	feed(&filter, 2.0f, 29.0f, 0.5f, flat, 0.0f, 0.5f, 1, angle);
	check(near(angle[1], 15.0f - 0.375f, 0.2f) && near(angle[0], 0.0f, 0.05f),
		"gyro: a turn after the bias is learnt: 30 deg/s turns 15 degrees in 0.5 s");
	{
		/* a slow turn: the accelerometer sees gravity move - not still */
		struct vita_gyro_filter turning;
		float angle_turning[3] = { 0.0f, 0.0f, 0.0f };
		int index;

		memset(&turning, 0, sizeof(turning));
		for (index = 0; index < 400; index++)
		{
			float tilt = index * 0.005f * 4.0f * DEG;
			float accel[3] = { 0.0f, tilt, 1.0f };
			float rate[3] = { 4.0f * DEG, 0.0f, 0.0f };

			vita_gyro_filter_sample(&turning, rate, accel, 0.005f, 1, angle_turning);
		}
		check(turning.still_count == 0 && angle_turning[0] > 5.0f * DEG,
			"gyro: a slow 4 deg/s tilt for 2 s (gravity moves) is not taken for the bias");
	}
	memset(&filter, 0, sizeof(filter));
	feed(&filter, 0.0f, 0.0f, 10.0f, flat, 0.0f, 3.0f, 1, angle);
	check(filter.still_count == 0 && angle[2] > 25.0f,
		"gyro: a steady 10 deg/s turn about gravity (yaw on a table) is not taken for the bias");
	memset(&filter, 0, sizeof(filter));
	feed(&filter, 0.0f, 0.0f, 0.0f, flat, 3.0f, 3.0f, 1, angle);
	check(filter.still_count == 0, "gyro: held in the hands (a 3 deg/s tremor): no bias learnt");
	{
		static const float shaking[2][3] = { { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.05f, 1.0f } };
		float angle_shake[3] = { 0.0f, 0.0f, 0.0f };
		float rate[3] = { 1.0f * DEG, 0.0f, 0.0f };
		int index;

		memset(&filter, 0, sizeof(filter));
		for (index = 0; index < 600; index++)
			vita_gyro_filter_sample(&filter, rate, shaking[index & 1], 0.005f, 1, angle_shake);
		check(filter.still_count == 0, "gyro: the accelerometer moving 0.05 g: not still");
	}
}

static void test_gyro_aim(void)
{
	struct vita_gyro_config config;
	struct vita_gyro_state state;
	float angle[3] = { 0.1f, 0.2f, 0.3f };
	float yaw, pitch;
	int index;

	gyro_environment(NULL, NULL, NULL, NULL, NULL);
	vita_gyro_config_load(&config);
	check(config.mode == VITA_GYRO_OFF && config.button == VITA_BUTTON_L && near(config.sensitivity, 1.5f, 0.001f) &&
		!config.invert_y && config.turn == VITA_GYRO_TURN_YAW,
		"gyro settings unset: Off, button L, 1.5x, not inverted, yaw");
	gyro_environment("hold", "r", "250", "1", "roll");
	vita_gyro_config_load(&config);
	check(config.mode == VITA_GYRO_HOLD && config.button == VITA_BUTTON_R && near(config.sensitivity, 2.5f, 0.001f) &&
		config.invert_y && config.turn == VITA_GYRO_TURN_ROLL, "gyro settings: hold R, 2.5x, inverted, roll");
	gyro_environment("sideways", "x", "-3", "0", "pitch");
	vita_gyro_config_load(&config);
	check(config.mode == VITA_GYRO_OFF && config.button == VITA_BUTTON_L && near(config.sensitivity, 1.5f, 0.001f) &&
		config.turn == VITA_GYRO_TURN_YAW, "gyro settings: unknown values are the defaults");
	gyro_environment(NULL, NULL, NULL, NULL, NULL);

	/* the angles as the game's yaw and pitch */
	config.mode = VITA_GYRO_ON;
	config.sensitivity = 1.0f;
	config.invert_y = 0;
	config.turn = VITA_GYRO_TURN_YAW;
	vita_gyro_look(&config, angle, &yaw, &pitch);
	check(near(yaw, 0.2f, 1e-6f) && near(pitch, 0.1f, 1e-6f), "gyro look: yaw is the y angle (left +), pitch x (up +)");
	config.turn = VITA_GYRO_TURN_ROLL;
	config.invert_y = 1;
	config.sensitivity = 2.0f;
	vita_gyro_look(&config, angle, &yaw, &pitch);
	check(near(yaw, 0.6f, 1e-6f) && near(pitch, -0.2f, 1e-6f), "gyro look: roll turns (z), inverted, 2x");

	/* when */
	config.turn = VITA_GYRO_TURN_YAW;
	config.invert_y = 0;
	config.sensitivity = 1.0f;
	config.button = VITA_BUTTON_L;
	config.mode = VITA_GYRO_OFF;
	check(!vita_gyro_active(&config, VITA_BUTTON_L, 0), "gyro Off: never");
	config.mode = VITA_GYRO_ON;
	check(vita_gyro_active(&config, 0, 0) && !vita_gyro_active(&config, 0, 1), "gyro On: in play, not in the menus");
	config.mode = VITA_GYRO_ZOOMED;
	check(vita_gyro_active(&config, 0, 0), "gyro While zoomed: active (the zoom is asked when the game takes it)");
	config.mode = VITA_GYRO_HOLD;
	check(vita_gyro_active(&config, VITA_BUTTON_L | VITA_BUTTON_CROSS, 0) && !vita_gyro_active(&config, VITA_BUTTON_R, 0) &&
		!vita_gyro_active(&config, VITA_BUTTON_L, 1), "gyro While holding: only with its button, not in the menus");
	config.button = 0;
	check(!vita_gyro_active(&config, 0xFFFF, 0), "gyro While holding with no button: never");

	/* accumulated, taken */
	config.mode = VITA_GYRO_ON;
	memset(&state, 0, sizeof(state));
	vita_gyro_accumulate(&state, &config, angle, 1);
	vita_gyro_accumulate(&state, &config, angle, 1);
	check(vita_gyro_take(&state, &config, 0, &yaw, &pitch) && near(yaw, 0.4f, 1e-6f) && near(pitch, 0.2f, 1e-6f),
		"gyro: two reads add up; the game takes both");
	check(!vita_gyro_take(&state, &config, 0, &yaw, &pitch) && yaw == 0.0f, "gyro: taken once only");
	vita_gyro_accumulate(&state, &config, angle, 0);
	check(!vita_gyro_take(&state, &config, 0, &yaw, &pitch), "gyro: inactive (menus, panel) gives nothing");
	vita_gyro_accumulate(&state, &config, angle, 1);
	vita_gyro_accumulate(&state, &config, angle, 0);
	vita_gyro_accumulate(&state, &config, angle, 1);
	check(vita_gyro_take(&state, &config, 0, &yaw, &pitch) && near(yaw, 0.2f, 1e-6f),
		"gyro: becoming active starts afresh (nothing from before)");
	for (index = 0; index < VITA_GYRO_MAXIMUM_UNCONSUMED + 2; index++)
		vita_gyro_accumulate(&state, &config, angle, 1);
	check(vita_gyro_take(&state, &config, 0, &yaw, &pitch) && yaw < 0.2f * VITA_GYRO_MAXIMUM_UNCONSUMED + 0.01f,
		"gyro: not taken for a few frames (a cinematic): what turned meanwhile is dropped");
	config.mode = VITA_GYRO_ZOOMED;
	vita_gyro_accumulate(&state, &config, angle, 1);
	check(!vita_gyro_take(&state, &config, 0, &yaw, &pitch), "gyro While zoomed, not zoomed: nothing");
	vita_gyro_accumulate(&state, &config, angle, 1);
	check(vita_gyro_take(&state, &config, 1, &yaw, &pitch) && near(yaw, 0.2f, 1e-6f), "gyro While zoomed, zoomed: aims");
	config.mode = VITA_GYRO_HOLD;
	vita_gyro_accumulate(&state, &config, angle, vita_gyro_active(&config, VITA_BUTTON_L, 0));
	vita_gyro_accumulate(&state, &config, angle, vita_gyro_active(&config, 0, 0));
	check(!vita_gyro_take(&state, &config, 0, &yaw, &pitch), "gyro While holding: let go before the game took it: nothing");
	{
		/* a whole path: 60 deg/s about y for 0.5 s through the filter, at
		1.5x, read in 30 frames: 45 degrees of yaw, less the deadzone */
		struct vita_gyro_filter filter;
		float total = 0.0f;
		int frame, sample;

		memset(&filter, 0, sizeof(filter));
		memset(&state, 0, sizeof(state));
		config.mode = VITA_GYRO_ON;
		config.sensitivity = 1.5f;
		for (frame = 0; frame < 30; frame++)
		{
			float frame_angle[3] = { 0.0f, 0.0f, 0.0f };
			float rate[3] = { 0.0f, 60.0f * DEG, 0.0f };

			for (sample = 0; sample < 4; sample++)
				vita_gyro_filter_sample(&filter, rate, NULL, 0.5f / 120.0f, 1, frame_angle);
			vita_gyro_accumulate(&state, &config, frame_angle, 1);
			if (vita_gyro_take(&state, &config, 0, &yaw, &pitch))
				total += yaw;
		}
		check(near(total / DEG, 1.5f * (30.0f - 0.375f), 0.3f) && near(pitch, 0.0f, 1e-6f),
			"gyro: 60 deg/s for 0.5 s at 1.5x: 44.4 degrees of yaw, frame by frame");
	}
}

int main(void)
{
	test_zones();
	test_hold();
	test_mapping();
	test_gyro_filter();
	test_gyro_aim();
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures ? 1 : 0;
}
