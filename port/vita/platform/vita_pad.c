/*
VITA_PAD.C

The Vita's controls as the Xbox controller on port 0, by default in Xita's
layout (the settings panel's Controls tab moves an action to another button
and gives the touch zones actions: vita_controls.c):
	Cross, Circle, Square, Triangle   A, B, X, Y
	L, R                              left trigger (grenade), right trigger (fire)
	Start, Select                     Start, Back
	sticks                            sticks
	D-pad                             the D-pad in menus; in play, which never
	                                  reads it: down left stick click (crouch),
	                                  up right stick click (zoom), left Black
	                                  (switch grenades), right White (flashlight)
The look stick takes Xita's settings: XV_LOOK_SENS (percent), XV_LOOK_CURVE
(2: squared), XV_INVERT_Y, and XV_DEADZONE (percent, both sticks).
*/

#include "platform.h"
#include "vita_controls.h"
#include "vita_host.h"

#include <math.h>
#include <stdlib.h>

/* set while the game's menus are up (halo_ui_pointer_update, d3d8_gxm.c) */
int vita_menus_active;

/* bumped by the settings panel (port_config.c) */
extern volatile unsigned long halo_settings_generation;

static int setting(const char *name, int fallback, int low, int high)
{
	const char *value = getenv(name);
	int result = value ? atoi(value) : fallback;

	return result < low ? low : result > high ? high : result;
}

static short axis(int value)
{
	return (short)(value > 32767 ? 32767 : value < -32767 ? -32767 : value);
}

static void stick(unsigned char raw_x, unsigned char raw_y, int deadzone, int sensitivity, int curve, int invert,
	SHORT *out_x, SHORT *out_y)
{
	int x = axis(((int)raw_x - 128) * 256);
	int y = axis(-(((int)raw_y - 128) * 256));

	if (deadzone || curve == 2 || sensitivity != 100)
	{
		float fx = (float)x, fy = (float)y;
		float radius = sqrtf(fx * fx + fy * fy);
		float dead = 32767.0f * deadzone / 100.0f;

		if (radius <= dead)
		{
			x = y = 0;
		}
		else
		{
			float magnitude = fminf(radius / 32767.0f, 1.0f);
			float scale = 1.0f;

			if (deadzone)
			{
				magnitude = (magnitude - deadzone / 100.0f) / (1.0f - deadzone / 100.0f);
				scale = magnitude * 32767.0f / radius;
			}
			if (curve == 2)
				scale *= magnitude;
			scale *= sensitivity / 100.0f;
			x = axis((int)(fx * scale));
			y = axis((int)(fy * scale));
		}
	}
	*out_x = (SHORT)x;
	*out_y = (SHORT)(invert ? -y : y);
}

void vita_pad_state(XINPUT_GAMEPAD *gamepad)
{
	static int deadzone = -1, sensitivity, curve, invert;
	/* the zones' and buttons' actions (the panel's Controls tab), and the
	crouch toggle's state */
	static struct vita_controls_config controls;
	static struct vita_controls_state controls_state;
	static unsigned long settings_seen;
	struct vita_host_pad pad;
	struct vita_controls_output output;
	int index;

	/* (read again after a change in the settings panel) */
	if (deadzone < 0 || settings_seen != halo_settings_generation)
	{
		settings_seen = halo_settings_generation;
		deadzone = setting("XV_DEADZONE", 0, 0, 99);
		sensitivity = setting("XV_LOOK_SENS", 100, 0, 400);
		curve = setting("XV_LOOK_CURVE", 0, 0, 2);
		invert = setting("XV_INVERT_Y", 0, 0, 1);
		vita_controls_config_load(&controls);
	}
	vita_host_pad_read(&pad);
	/* the settings panel has the buttons (and the touch zones) while it is
	open */
	if (vita_settings_input(&pad))
	{
		gamepad->sThumbLX = gamepad->sThumbLY = gamepad->sThumbRX = gamepad->sThumbRY = 0;
		return;
	}
	/* the buttons and the touch zones held as the Xbox's buttons
	(vita_controls.c): crouch is the left stick's click, which the Vita
	does not have; HALO_CROUCH_TOGGLE (the panel's Crouch, on by default)
	makes a press crouch and the next one stand */
	vita_controls_map(&controls, &controls_state, pad.buttons, pad.touch, vita_menus_active, &output);
	gamepad->wButtons |= (WORD)output.digital;
	for (index = 0; index < 8; index++)
		gamepad->bAnalogButtons[index] = output.analog[index];
	stick(pad.lx, pad.ly, deadzone, 100, 0, 0, &gamepad->sThumbLX, &gamepad->sThumbLY);
	stick(pad.rx, pad.ry, deadzone, sensitivity, curve, invert, &gamepad->sThumbRX, &gamepad->sThumbRY);
}
