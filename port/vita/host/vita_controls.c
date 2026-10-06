/*
VITA_CONTROLS.C

The mapping layer between the Vita's controls and the Xbox controller
(port/vita/include/vita_controls.h): touch zones, the actions a zone or a
button is set to in the settings panel, and the Xbox buttons those press.
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

const char *const vita_touch_variables[VITA_ZONE_COUNT] = {
	"HALO_TOUCH_TOP_LEFT", "HALO_TOUCH_TOP_RIGHT", "HALO_TOUCH_LEFT_EDGE", "HALO_TOUCH_RIGHT_EDGE",
	"HALO_TOUCH_REAR_LEFT", "HALO_TOUCH_REAR_RIGHT",
};

const char *const vita_button_variables[VITA_ACTION_COUNT] = {
	NULL, "HALO_BUTTON_MELEE", "HALO_BUTTON_GRENADE", "HALO_BUTTON_FLASHLIGHT", "HALO_BUTTON_ZOOM",
	"HALO_BUTTON_CROUCH", "HALO_BUTTON_RELOAD", "HALO_BUTTON_WEAPON", "HALO_BUTTON_GRENADE_SWITCH",
	"HALO_BUTTON_JUMP", NULL, "HALO_BUTTON_FIRE",
};

const unsigned long vita_button_defaults[VITA_ACTION_COUNT] = {
	0, VITA_BUTTON_CIRCLE, VITA_BUTTON_L, VITA_BUTTON_RIGHT, VITA_BUTTON_UP, VITA_BUTTON_DOWN, VITA_BUTTON_SQUARE,
	VITA_BUTTON_TRIANGLE, VITA_BUTTON_LEFT, VITA_BUTTON_CROSS, VITA_BUTTON_SELECT, VITA_BUTTON_R,
};

static const char *const action_values[VITA_ACTION_COUNT] = { VITA_ACTION_VALUES };
static const char *const button_values[VITA_BUTTON_CHOICES] = { VITA_BUTTON_VALUES };
static const unsigned long button_bits[VITA_BUTTON_CHOICES] = {
	VITA_BUTTON_CROSS, VITA_BUTTON_CIRCLE, VITA_BUTTON_SQUARE, VITA_BUTTON_TRIANGLE, VITA_BUTTON_L, VITA_BUTTON_R,
	VITA_BUTTON_UP, VITA_BUTTON_DOWN, VITA_BUTTON_LEFT, VITA_BUTTON_RIGHT, 0,
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
			now_us - tracker->fingers[finger].since < VITA_TOUCH_REAR_HOLD_US)
			continue;
		held |= 1UL << tracker->fingers[finger].zone;
	}
	if (started)
		*started = new_fingers;
	return held;
}

int vita_action_named(const char *value)
{
	int action;

	for (action = 0; value && action < VITA_ACTION_COUNT; action++)
		if (strcmp(value, action_values[action]) == 0)
			return action;
	return VITA_ACTION_OFF;
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
		config->zone_action[index] = vita_action_named(getenv(vita_touch_variables[index]));
	for (index = 0; index < VITA_ACTION_COUNT; index++)
		config->action_button[index] = vita_button_variables[index] ?
			vita_button_named(getenv(vita_button_variables[index]), vita_button_defaults[index]) :
			vita_button_defaults[index];
	/* (on unless set to 0, as vita_pad.c read it) */
	config->crouch_toggle = toggle ? atoi(toggle) != 0 : 1;
}

void vita_controls_map(const struct vita_controls_config *config, struct vita_controls_state *state,
	unsigned long buttons, unsigned long touch, int menus, struct vita_controls_output *output)
{
	int held[VITA_ACTION_COUNT];
	int action, zone;

	memset(output, 0, sizeof(*output));
	if (buttons & VITA_BUTTON_START)
		output->digital |= VITA_PAD_START;
	if (menus)
	{
		/* the menus' fixed layout: D-pad, A B X Y, the triggers; the
		crouch toggle starts over standing */
		state->crouched = 0;
		state->crouch_was_down = (buttons & config->action_button[VITA_ACTION_CROUCH]) != 0;
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
	/* in play: each action held by its button or by a zone set to it */
	for (action = 0; action < VITA_ACTION_COUNT; action++)
		held[action] = config->action_button[action] && (buttons & config->action_button[action]) != 0;
	for (zone = 0; zone < VITA_ZONE_COUNT; zone++)
		if ((touch & (1UL << zone)) && config->zone_action[zone] > VITA_ACTION_OFF &&
			config->zone_action[zone] < VITA_ACTION_COUNT)
			held[config->zone_action[zone]] = 1;
	/* crouch is the left stick's click, held, on the Xbox; Toggle (the
	panel's Crouch) makes a press crouch and the next one stand */
	if (config->crouch_toggle)
	{
		if (held[VITA_ACTION_CROUCH] && !state->crouch_was_down)
			state->crouched = !state->crouched;
		if (state->crouched)
			output->digital |= VITA_PAD_LEFT_THUMB;
	}
	else if (held[VITA_ACTION_CROUCH])
		output->digital |= VITA_PAD_LEFT_THUMB;
	state->crouch_was_down = held[VITA_ACTION_CROUCH];
	if (held[VITA_ACTION_ZOOM]) output->digital |= VITA_PAD_RIGHT_THUMB;
	if (held[VITA_ACTION_SCOREBOARD]) output->digital |= VITA_PAD_BACK;
	output->analog[0] = held[VITA_ACTION_JUMP] ? 255 : 0;
	output->analog[1] = held[VITA_ACTION_MELEE] ? 255 : 0;
	output->analog[2] = held[VITA_ACTION_RELOAD] ? 255 : 0;
	output->analog[3] = held[VITA_ACTION_SWITCH_WEAPON] ? 255 : 0;
	output->analog[4] = held[VITA_ACTION_SWITCH_GRENADE] ? 255 : 0;
	output->analog[5] = held[VITA_ACTION_FLASHLIGHT] ? 255 : 0;
	output->analog[6] = held[VITA_ACTION_GRENADE] ? 255 : 0;
	output->analog[7] = held[VITA_ACTION_FIRE] ? 255 : 0;
}
