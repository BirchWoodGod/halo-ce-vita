/*
VITA_CONTROLS.C

The mapping layer between the Vita's controls and the Xbox controller
(port/vita/include/vita_controls.h): touch zones, the Xbox button each zone
presses and the Vita button of each Xbox button (the settings panel's
Controls tab), and the Vita button each of the game's button icons
shows with the panel's Button icons PlayStation (vita_button_glyph).
vita_input.c feeds the touch panels in, vita_pad.c (the platform layer)
asks for the controller's state, vita_settings.c shows the rows.

The zones, in the screen's 960 x 544 pixels:
	front, top left     0..160 x 0..120     (over the weapon and ammo HUD)
	front, top right    800..960 x 0..120   (over the shield meter)
	front, left edge    0..120 x 190..400   (beside the D-pad, above the
	                                         motion tracker: no HUD there)
	front, right edge   840..960 x 190..400 (beside the face buttons)
	rear, left half     0..440 x all
	rear, right half    520..960 x all      (a strip between the two counts
	                                         for neither)
The bottom corners are left alone: the motion tracker is bottom left.

The rear pad is where the hands holding the Vita rest, so its fingers go
through a guard (the panel's Rear touch guard): one that comes down in the
pad's outer border never counts, and one further in only once held a
while. On hardware the hands resting on the pad pressed a rear zone's
button (Black) with the old 0.1 s and no border.
	guard     border   hold
	Off       none     0.1 s
	Light     48 px    0.15 s
	Normal    96 px    0.25 s   (the default)
	Strong    144 px   0.4 s
*/

#include "vita_controls.h"
#include "vita_host.h"

#include <stdlib.h>
#include <string.h>

const struct vita_touch_zone vita_touch_zones[VITA_ZONE_COUNT] = {
	{ VITA_TOUCH_FRONT, 0, 0, 160, 120, "tl" },
	{ VITA_TOUCH_FRONT, 800, 0, 960, 120, "tr" },
	{ VITA_TOUCH_FRONT, 0, 190, 120, 400, "el" },
	{ VITA_TOUCH_FRONT, 840, 190, 960, 400, "er" },
	{ VITA_TOUCH_REAR, 0, 0, 440, 544, "rl" },
	{ VITA_TOUCH_REAR, 520, 0, 960, 544, "rr" },
};

const struct vita_rear_guard vita_rear_guards[VITA_REAR_GUARD_COUNT] = {
	{ 0, 100 },
	{ 48, 150 },
	{ 96, 250 },
	{ 144, 400 },
};

static const char *const rear_guard_values[VITA_REAR_GUARD_COUNT] = { VITA_REAR_GUARD_VALUES };

int vita_rear_guard_named(const char *value)
{
	int guard;

	for (guard = 0; value && guard < VITA_REAR_GUARD_COUNT; guard++)
		if (strcmp(value, rear_guard_values[guard]) == 0)
			return guard;
	return VITA_REAR_GUARD_DEFAULT;
}

const char *const vita_touch_variables[VITA_ZONE_COUNT] = {
	"HALO_TOUCH_TOP_LEFT", "HALO_TOUCH_TOP_RIGHT", "HALO_TOUCH_LEFT_EDGE", "HALO_TOUCH_RIGHT_EDGE",
	"HALO_TOUCH_REAR_LEFT", "HALO_TOUCH_REAR_RIGHT",
};

const char *const vita_xbox_variables[VITA_XBOX_COUNT] = {
	NULL, "HALO_XBOX_A", "HALO_XBOX_B", "HALO_XBOX_X", "HALO_XBOX_Y", "HALO_XBOX_BLACK", "HALO_XBOX_WHITE",
	"HALO_XBOX_LEFT_TRIGGER", "HALO_XBOX_RIGHT_TRIGGER", "HALO_XBOX_LEFT_STICK", "HALO_XBOX_RIGHT_STICK",
	"HALO_XBOX_BACK",
};

/* Xita's layout: A B X Y on Cross Circle Square Triangle, Black and White
on D-pad left and right, the triggers on L and R, the sticks' clicks
(crouch, zoom) on D-pad down and up, Back on Select */
const unsigned long vita_xbox_defaults[VITA_XBOX_COUNT] = {
	0, VITA_BUTTON_CROSS, VITA_BUTTON_CIRCLE, VITA_BUTTON_SQUARE, VITA_BUTTON_TRIANGLE, VITA_BUTTON_LEFT,
	VITA_BUTTON_RIGHT, VITA_BUTTON_L, VITA_BUTTON_R, VITA_BUTTON_DOWN, VITA_BUTTON_UP, VITA_BUTTON_SELECT,
};

static const char *const xbox_values[VITA_XBOX_COUNT] = { VITA_XBOX_VALUES };
static const char *const button_values[VITA_BUTTON_CHOICES] = { VITA_BUTTON_VALUES };
static const unsigned long button_bits[VITA_BUTTON_CHOICES] = {
	VITA_BUTTON_CROSS, VITA_BUTTON_CIRCLE, VITA_BUTTON_SQUARE, VITA_BUTTON_TRIANGLE, VITA_BUTTON_L, VITA_BUTTON_R,
	VITA_BUTTON_UP, VITA_BUTTON_DOWN, VITA_BUTTON_LEFT, VITA_BUTTON_RIGHT, VITA_BUTTON_SELECT, 0,
};

int vita_touch_zone_at(int panel, int x, int y)
{
	int zone;

	for (zone = 0; zone < VITA_ZONE_COUNT; zone++)
	{
		const struct vita_touch_zone *rectangle = &vita_touch_zones[zone];

		if (rectangle->panel == panel && x >= rectangle->left && x < rectangle->right && y >= rectangle->top &&
			y < rectangle->bottom)
			return zone;
	}
	return -1;
}

unsigned long vita_touch_update(struct vita_touch_tracker *tracker, const struct vita_touch_contact *contacts,
	int count, unsigned long long now_us, int *started)
{
	int seen[VITA_TOUCH_TRACKED];
	unsigned long held = 0;
	int index, finger, new_fingers = 0;
	const struct vita_rear_guard *guard = &vita_rear_guards[tracker->rear_guard >= 0 &&
		tracker->rear_guard < VITA_REAR_GUARD_COUNT ? tracker->rear_guard : VITA_REAR_GUARD_DEFAULT];
	unsigned long long rear_hold_us = (unsigned long long)guard->hold_ms * 1000;

	memset(seen, 0, sizeof(seen));
	for (index = 0; index < count; index++)
	{
		const struct vita_touch_contact *contact = &contacts[index];
		int free_slot = -1;

		for (finger = 0; finger < VITA_TOUCH_TRACKED; finger++)
		{
			if (!tracker->fingers[finger].used)
			{
				if (free_slot < 0)
					free_slot = finger;
				continue;
			}
			if (tracker->fingers[finger].panel == contact->panel && tracker->fingers[finger].id == contact->id)
				break;
		}
		if (finger == VITA_TOUCH_TRACKED)
		{
			/* (a finger come down: the zone it starts in is the one it
			counts for, as long as it stays down) */
			if (free_slot < 0)
				continue;
			finger = free_slot;
			tracker->fingers[finger].used = 1;
			tracker->fingers[finger].panel = contact->panel;
			tracker->fingers[finger].id = contact->id;
			tracker->fingers[finger].zone = vita_touch_zone_at(contact->panel, contact->x, contact->y);
			/* (the rear pad's border, where the hands holding the Vita
			rest: never a zone) */
			if (contact->panel == VITA_TOUCH_REAR && (contact->x < guard->edge || contact->y < guard->edge ||
				contact->x >= VITA_TOUCH_WIDTH - guard->edge || contact->y >= VITA_TOUCH_HEIGHT - guard->edge))
				tracker->fingers[finger].zone = -1;
			tracker->fingers[finger].since = now_us;
			new_fingers++;
		}
		seen[finger] = 1;
	}
	for (finger = 0; finger < VITA_TOUCH_TRACKED; finger++)
	{
		if (!tracker->fingers[finger].used)
			continue;
		if (!seen[finger])
		{
			tracker->fingers[finger].used = 0;
			continue;
		}
		if (tracker->fingers[finger].zone < 0)
			continue;
		if (tracker->fingers[finger].panel == VITA_TOUCH_REAR &&
			now_us - tracker->fingers[finger].since < rear_hold_us)
			continue;
		held |= 1UL << tracker->fingers[finger].zone;
	}
	if (started)
		*started = new_fingers;
	return held;
}

int vita_xbox_named(const char *value)
{
	int xbox;

	for (xbox = 0; value && xbox < VITA_XBOX_COUNT; xbox++)
		if (strcmp(value, xbox_values[xbox]) == 0)
			return xbox;
	return VITA_XBOX_OFF;
}

unsigned long vita_button_named(const char *value, unsigned long fallback)
{
	int index;

	for (index = 0; value && index < VITA_BUTTON_CHOICES; index++)
		if (strcmp(value, button_values[index]) == 0)
			return button_bits[index];
	return fallback;
}

void vita_controls_config_load(struct vita_controls_config *config)
{
	const char *toggle = getenv("HALO_CROUCH_TOGGLE");
	int index;

	for (index = 0; index < VITA_ZONE_COUNT; index++)
		config->zone_xbox[index] = vita_xbox_named(getenv(vita_touch_variables[index]));
	for (index = 0; index < VITA_XBOX_COUNT; index++)
		config->xbox_button[index] = vita_xbox_variables[index] ?
			vita_button_named(getenv(vita_xbox_variables[index]), vita_xbox_defaults[index]) :
			vita_xbox_defaults[index];
	/* (on unless set to 0, as vita_pad.c read it) */
	config->crouch_toggle = toggle ? atoi(toggle) != 0 : 1;
}

void vita_controls_map(const struct vita_controls_config *config, struct vita_controls_state *state,
	unsigned long buttons, unsigned long touch, int menus, struct vita_controls_output *output)
{
	int held[VITA_XBOX_COUNT];
	int xbox, zone;

	memset(output, 0, sizeof(*output));
	/* a DualShock's L2 and R2 (a PS TV's) are the Vita's L and R as well
	as its L1 and R1 */
	if (buttons & VITA_BUTTON_L2)
		buttons |= VITA_BUTTON_L;
	if (buttons & VITA_BUTTON_R2)
		buttons |= VITA_BUTTON_R;
	if (buttons & VITA_BUTTON_START)
		output->digital |= VITA_PAD_START;
	if (menus)
	{
		/* the menus' fixed layout: D-pad, A B X Y, the triggers; the
		crouch toggle starts over standing */
		state->crouched = 0;
		state->crouch_was_down = (buttons & config->xbox_button[VITA_XBOX_LEFT_STICK]) != 0;
		if (buttons & VITA_BUTTON_SELECT) output->digital |= VITA_PAD_BACK;
		if (buttons & VITA_BUTTON_UP) output->digital |= VITA_PAD_DPAD_UP;
		if (buttons & VITA_BUTTON_DOWN) output->digital |= VITA_PAD_DPAD_DOWN;
		if (buttons & VITA_BUTTON_LEFT) output->digital |= VITA_PAD_DPAD_LEFT;
		if (buttons & VITA_BUTTON_RIGHT) output->digital |= VITA_PAD_DPAD_RIGHT;
		output->analog[0] = (buttons & VITA_BUTTON_CROSS) ? 255 : 0;
		output->analog[1] = (buttons & VITA_BUTTON_CIRCLE) ? 255 : 0;
		output->analog[2] = (buttons & VITA_BUTTON_SQUARE) ? 255 : 0;
		output->analog[3] = (buttons & VITA_BUTTON_TRIANGLE) ? 255 : 0;
		output->analog[6] = (buttons & VITA_BUTTON_L) ? 255 : 0;
		output->analog[7] = (buttons & VITA_BUTTON_R) ? 255 : 0;
		return;
	}
	/* in play: each Xbox button held by its Vita button or by a zone set
	to it */
	for (xbox = 0; xbox < VITA_XBOX_COUNT; xbox++)
		held[xbox] = config->xbox_button[xbox] && (buttons & config->xbox_button[xbox]) != 0;
	for (zone = 0; zone < VITA_ZONE_COUNT; zone++)
		if ((touch & (1UL << zone)) && config->zone_xbox[zone] > VITA_XBOX_OFF &&
			config->zone_xbox[zone] < VITA_XBOX_COUNT)
			held[config->zone_xbox[zone]] = 1;
	/* and its sticks' clicks are the Xbox's */
	if (buttons & VITA_BUTTON_L3)
		held[VITA_XBOX_LEFT_STICK] = 1;
	if (buttons & VITA_BUTTON_R3)
		held[VITA_XBOX_RIGHT_STICK] = 1;
	/* the left stick's click is crouch, held, on the Xbox; Toggle (the
	panel's Crouch) makes a press crouch and the next one stand */
	if (config->crouch_toggle)
	{
		if (held[VITA_XBOX_LEFT_STICK] && !state->crouch_was_down)
			state->crouched = !state->crouched;
		if (state->crouched)
			output->digital |= VITA_PAD_LEFT_THUMB;
	}
	else if (held[VITA_XBOX_LEFT_STICK])
		output->digital |= VITA_PAD_LEFT_THUMB;
	state->crouch_was_down = held[VITA_XBOX_LEFT_STICK];
	if (held[VITA_XBOX_RIGHT_STICK]) output->digital |= VITA_PAD_RIGHT_THUMB;
	if (held[VITA_XBOX_BACK]) output->digital |= VITA_PAD_BACK;
	/* (A to the right trigger: the analog buttons in order) */
	for (xbox = VITA_XBOX_A; xbox <= VITA_XBOX_RIGHT_TRIGGER; xbox++)
		output->analog[xbox - VITA_XBOX_A] = held[xbox] ? 255 : 0;
}

/* ---------- a PS TV's controllers */

unsigned long vita_controls_extra_connected(const unsigned char types[VITA_CONTROLLER_PORTS])
{
	unsigned long connected = 0;
	int controller;

	for (controller = 1; controller <= VITA_EXTRA_CONTROLLERS; controller++)
		if (types[VITA_EXTRA_PORT(controller)] != 0)
			connected |= 1UL << controller;
	return connected;
}

unsigned long vita_controls_ext2_buttons(unsigned long buttons)
{
	/* (the Vita's buttons that keep their bits: Select, L3, R3, Start, the
	D-pad, the face buttons) */
	unsigned long result = buttons & (VITA_BUTTON_SELECT | VITA_BUTTON_L3 | VITA_BUTTON_R3 | VITA_BUTTON_START |
		VITA_BUTTON_UP | VITA_BUTTON_RIGHT | VITA_BUTTON_DOWN | VITA_BUTTON_LEFT | VITA_BUTTON_TRIANGLE |
		VITA_BUTTON_CIRCLE | VITA_BUTTON_CROSS | VITA_BUTTON_SQUARE);

	if (buttons & VITA_EXT2_L2) result |= VITA_BUTTON_L2;
	if (buttons & VITA_EXT2_R2) result |= VITA_BUTTON_R2;
	if (buttons & VITA_EXT2_L1) result |= VITA_BUTTON_L;
	if (buttons & VITA_EXT2_R1) result |= VITA_BUTTON_R;
	return result;
}

/* ---------- button icons */

int vita_button_icons_playstation(void)
{
	const char *value = getenv("HALO_BUTTON_ICONS");

	return value && strcmp(value, "playstation") == 0;
}

static int glyph_of_button(unsigned long button)
{
	static const unsigned long bits[] = {
		VITA_BUTTON_CROSS, VITA_BUTTON_CIRCLE, VITA_BUTTON_SQUARE, VITA_BUTTON_TRIANGLE, VITA_BUTTON_L, VITA_BUTTON_R,
		VITA_BUTTON_UP, VITA_BUTTON_DOWN, VITA_BUTTON_LEFT, VITA_BUTTON_RIGHT, VITA_BUTTON_START, VITA_BUTTON_SELECT,
	};
	int index;

	for (index = 0; index < (int)(sizeof(bits) / sizeof(bits[0])); index++)
		if (button & bits[index])
			return VITA_GLYPH_CROSS + index;
	return VITA_GLYPH_NONE;
}

int vita_button_glyph(const struct vita_controls_config *config, int gamepad_button, int menus)
{
	/* the game's numbering: the analog buttons (A to the right trigger),
	then the D-pad, Start, Back and the sticks' clicks */
	static const int xbox_of_gamepad[16] = {
		VITA_XBOX_A, VITA_XBOX_B, VITA_XBOX_X, VITA_XBOX_Y, VITA_XBOX_BLACK, VITA_XBOX_WHITE, VITA_XBOX_LEFT_TRIGGER,
		VITA_XBOX_RIGHT_TRIGGER, VITA_XBOX_OFF, VITA_XBOX_OFF, VITA_XBOX_OFF, VITA_XBOX_OFF, VITA_XBOX_OFF,
		VITA_XBOX_BACK, VITA_XBOX_LEFT_STICK, VITA_XBOX_RIGHT_STICK,
	};
	/* the menus' fixed layout (vita_controls_map) */
	static const int menu_glyphs[16] = {
		VITA_GLYPH_CROSS, VITA_GLYPH_CIRCLE, VITA_GLYPH_SQUARE, VITA_GLYPH_TRIANGLE, VITA_GLYPH_NONE, VITA_GLYPH_NONE,
		VITA_GLYPH_L, VITA_GLYPH_R, VITA_GLYPH_UP, VITA_GLYPH_DOWN, VITA_GLYPH_LEFT, VITA_GLYPH_RIGHT, VITA_GLYPH_START,
		VITA_GLYPH_SELECT, VITA_GLYPH_NONE, VITA_GLYPH_NONE,
	};
	int xbox, zone, glyph;

	if (gamepad_button < 0 || gamepad_button >= 16)
		return VITA_GLYPH_NONE;
	if (menus)
		return menu_glyphs[gamepad_button];
	/* (Start stays on Start) */
	if (gamepad_button == 12)
		return VITA_GLYPH_START;
	xbox = xbox_of_gamepad[gamepad_button];
	if (xbox == VITA_XBOX_OFF)
		return VITA_GLYPH_NONE;
	glyph = glyph_of_button(config->xbox_button[xbox]);
	if (glyph != VITA_GLYPH_NONE)
		return glyph;
	for (zone = 0; zone < VITA_ZONE_COUNT; zone++)
		if (config->zone_xbox[zone] == xbox)
			return VITA_GLYPH_TOUCH_TOP_LEFT + zone;
	return VITA_GLYPH_NONE;
}

/* ---------- gyro aiming */

static const char *const gyro_mode_values[VITA_GYRO_MODES] = { "off", "on", "zoomed", "hold" };

void vita_gyro_config_load(struct vita_gyro_config *config)
{
	const char *mode = getenv("HALO_GYRO");
	const char *sensitivity = getenv("HALO_GYRO_SENS");
	const char *invert = getenv("HALO_GYRO_INVERT_Y");
	const char *turn = getenv("HALO_GYRO_TURN");
	int index, percent;

	config->mode = VITA_GYRO_OFF;
	for (index = 0; mode && index < VITA_GYRO_MODES; index++)
		if (strcmp(mode, gyro_mode_values[index]) == 0)
			config->mode = index;
	config->button = vita_button_named(getenv("HALO_GYRO_BUTTON"), VITA_BUTTON_L);
	percent = sensitivity ? atoi(sensitivity) : VITA_GYRO_DEFAULT_SENS;
	if (percent <= 0 || percent > 1000)
		percent = VITA_GYRO_DEFAULT_SENS;
	config->sensitivity = percent / 100.0f;
	config->invert_y = invert ? atoi(invert) != 0 : 0;
	config->turn = turn && strcmp(turn, "roll") == 0 ? VITA_GYRO_TURN_ROLL : VITA_GYRO_TURN_YAW;
}

static float gyro_absolute(float value)
{
	return value < 0.0f ? -value : value;
}

/* (no libm: the desktop test builds this alone) */
static float gyro_square_root(float value)
{
	float guess = value > 1.0f ? value : 1.0f;
	int step;

	if (value <= 0.0f)
		return 0.0f;
	for (step = 0; step < 20; step++)
		guess = 0.5f * (guess + value / guess);
	return guess;
}

static void gyro_window_start(struct vita_gyro_filter *filter, const float rate[3], const float accel[3], float dt)
{
	int axis;

	filter->window_time = dt;
	for (axis = 0; axis < 3; axis++)
	{
		filter->window_sum[axis] = rate[axis] * dt;
		filter->window_low[axis] = filter->window_high[axis] = rate[axis];
		filter->accel_low[axis] = filter->accel_high[axis] = accel ? accel[axis] : 0.0f;
	}
}

/* the bias: learnt from each second the Vita lies still (the first one
taken as it is, later ones halfway) */
static void gyro_calibrate(struct vita_gyro_filter *filter, const float rate[3], const float accel[3], float dt)
{
	int axis, still = 1;

	if (filter->window_time <= 0.0f)
	{
		gyro_window_start(filter, rate, accel, dt);
		return;
	}
	for (axis = 0; axis < 3; axis++)
	{
		float low = rate[axis] < filter->window_low[axis] ? rate[axis] : filter->window_low[axis];
		float high = rate[axis] > filter->window_high[axis] ? rate[axis] : filter->window_high[axis];
		float accel_value = accel ? accel[axis] : 0.0f;
		float accel_low = accel_value < filter->accel_low[axis] ? accel_value : filter->accel_low[axis];
		float accel_high = accel_value > filter->accel_high[axis] ? accel_value : filter->accel_high[axis];

		if (high - low > VITA_GYRO_STILL_SPREAD || accel_high - accel_low > VITA_GYRO_STILL_ACCEL ||
			gyro_absolute(rate[axis]) > VITA_GYRO_STILL_MEAN)
			still = 0;
		filter->window_low[axis] = low;
		filter->window_high[axis] = high;
		filter->accel_low[axis] = accel_low;
		filter->accel_high[axis] = accel_high;
	}
	if (!still)
	{
		/* (moved: a new window from this sample) */
		gyro_window_start(filter, rate, accel, dt);
		return;
	}
	filter->window_time += dt;
	for (axis = 0; axis < 3; axis++)
		filter->window_sum[axis] += rate[axis] * dt;
	if (filter->window_time >= VITA_GYRO_STILL_TIME)
	{
		for (axis = 0; axis < 3; axis++)
		{
			float mean = filter->window_sum[axis] / filter->window_time;

			if (gyro_absolute(mean) > VITA_GYRO_STILL_MEAN)
				break;
		}
		if (axis == 3)
		{
			for (axis = 0; axis < 3; axis++)
			{
				float mean = filter->window_sum[axis] / filter->window_time;

				filter->bias[axis] = filter->still_count ? 0.5f * (filter->bias[axis] + mean) : mean;
			}
			filter->still_count++;
		}
		filter->window_time = 0.0f;
	}
}

void vita_gyro_filter_sample(struct vita_gyro_filter *filter, const float rate[3], const float accel[3], float dt,
	int calibrate, float angle[3])
{
	float corrected[3], magnitude, smoothing, alpha;
	int axis;

	if (dt < 0.0f || dt > VITA_GYRO_MAXIMUM_GAP)
		dt = 0.0f;
	if (calibrate)
		gyro_calibrate(filter, rate, accel, dt);
	else
		filter->window_time = 0.0f;
	for (axis = 0; axis < 3; axis++)
		corrected[axis] = rate[axis] - filter->bias[axis];
	/* small motion smoothed (fully at VITA_GYRO_SMOOTH_LOW and under, not
	at all from VITA_GYRO_SMOOTH_HIGH) */
	magnitude = gyro_square_root(corrected[0] * corrected[0] + corrected[1] * corrected[1] +
		corrected[2] * corrected[2]);
	smoothing = magnitude <= VITA_GYRO_SMOOTH_LOW ? 1.0f : magnitude >= VITA_GYRO_SMOOTH_HIGH ? 0.0f :
		(VITA_GYRO_SMOOTH_HIGH - magnitude) / (VITA_GYRO_SMOOTH_HIGH - VITA_GYRO_SMOOTH_LOW);
	alpha = smoothing > 0.0f ? dt / (VITA_GYRO_SMOOTH_TIME * smoothing + dt) : 1.0f;
	if (dt <= 0.0f && smoothing > 0.0f)
		alpha = 0.0f;
	for (axis = 0; axis < 3; axis++)
		filter->smoothed[axis] += (corrected[axis] - filter->smoothed[axis]) * alpha;
	/* the deadzone, on the smoothed rate's size */
	magnitude = gyro_square_root(filter->smoothed[0] * filter->smoothed[0] +
		filter->smoothed[1] * filter->smoothed[1] + filter->smoothed[2] * filter->smoothed[2]);
	for (axis = 0; axis < 3; axis++)
	{
		filter->rate[axis] = magnitude > VITA_GYRO_DEADZONE ?
			filter->smoothed[axis] * (magnitude - VITA_GYRO_DEADZONE) / magnitude : 0.0f;
		angle[axis] += filter->rate[axis] * dt;
	}
}

int vita_gyro_active(const struct vita_gyro_config *config, unsigned long buttons, int menus)
{
	if (menus || config->mode == VITA_GYRO_OFF)
		return 0;
	if (config->mode == VITA_GYRO_HOLD)
		return config->button && (buttons & config->button) != 0;
	return 1;
}

void vita_gyro_look(const struct vita_gyro_config *config, const float angle[3], float *yaw, float *pitch)
{
	*yaw = (config->turn == VITA_GYRO_TURN_ROLL ? angle[2] : angle[1]) * config->sensitivity;
	*pitch = (config->invert_y ? -angle[0] : angle[0]) * config->sensitivity;
}

void vita_gyro_accumulate(struct vita_gyro_state *state, const struct vita_gyro_config *config,
	const float angle[3], int active)
{
	float yaw, pitch;

	if (!active || !state->active || ++state->unconsumed > VITA_GYRO_MAXIMUM_UNCONSUMED)
	{
		state->yaw = state->pitch = 0.0f;
		state->unconsumed = 0;
	}
	state->active = active;
	if (!active)
		return;
	vita_gyro_look(config, angle, &yaw, &pitch);
	state->yaw += yaw;
	state->pitch += pitch;
}

int vita_gyro_take(struct vita_gyro_state *state, const struct vita_gyro_config *config, int zoomed, float *yaw,
	float *pitch)
{
	*yaw = state->yaw;
	*pitch = state->pitch;
	state->yaw = state->pitch = 0.0f;
	state->unconsumed = 0;
	if (!state->active || (config->mode == VITA_GYRO_ZOOMED && !zoomed))
		*yaw = *pitch = 0.0f;
	return *yaw != 0.0f || *pitch != 0.0f;
}
