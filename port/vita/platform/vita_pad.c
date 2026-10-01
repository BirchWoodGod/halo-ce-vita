/*
VITA_PAD.C

The Vita's controls as the Xbox controller on port 0, in Xita's layout:
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
#include "vita_host.h"

#include <math.h>
#include <stdlib.h>

/* set while the game's menus are up (halo_ui_pointer_update, d3d8_gxm.c) */
int vita_menus_active;

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
	struct vita_host_pad pad;
	unsigned long buttons;
	WORD result = 0;

	if (deadzone < 0)
	{
		deadzone = setting("XV_DEADZONE", 0, 0, 99);
		sensitivity = setting("XV_LOOK_SENS", 100, 0, 400);
		curve = setting("XV_LOOK_CURVE", 0, 0, 2);
		invert = setting("XV_INVERT_Y", 0, 0, 1);
	}
	vita_host_pad_read(&pad);
	buttons = pad.buttons;
	if (buttons & VITA_BUTTON_START) result |= XINPUT_GAMEPAD_START;
	if (buttons & VITA_BUTTON_SELECT) result |= XINPUT_GAMEPAD_BACK;
	if (vita_menus_active)
	{
		if (buttons & VITA_BUTTON_UP) result |= XINPUT_GAMEPAD_DPAD_UP;
		if (buttons & VITA_BUTTON_DOWN) result |= XINPUT_GAMEPAD_DPAD_DOWN;
		if (buttons & VITA_BUTTON_LEFT) result |= XINPUT_GAMEPAD_DPAD_LEFT;
		if (buttons & VITA_BUTTON_RIGHT) result |= XINPUT_GAMEPAD_DPAD_RIGHT;
	}
	else
	{
		if (buttons & VITA_BUTTON_DOWN) result |= XINPUT_GAMEPAD_LEFT_THUMB;
		if (buttons & VITA_BUTTON_UP) result |= XINPUT_GAMEPAD_RIGHT_THUMB;
		gamepad->bAnalogButtons[XINPUT_GAMEPAD_BLACK] = (buttons & VITA_BUTTON_LEFT) ? 255 : 0;
		gamepad->bAnalogButtons[XINPUT_GAMEPAD_WHITE] = (buttons & VITA_BUTTON_RIGHT) ? 255 : 0;
	}
	gamepad->wButtons |= result;
	gamepad->bAnalogButtons[XINPUT_GAMEPAD_A] = (buttons & VITA_BUTTON_CROSS) ? 255 : 0;
	gamepad->bAnalogButtons[XINPUT_GAMEPAD_B] = (buttons & VITA_BUTTON_CIRCLE) ? 255 : 0;
	gamepad->bAnalogButtons[XINPUT_GAMEPAD_X] = (buttons & VITA_BUTTON_SQUARE) ? 255 : 0;
	gamepad->bAnalogButtons[XINPUT_GAMEPAD_Y] = (buttons & VITA_BUTTON_TRIANGLE) ? 255 : 0;
	gamepad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] = (buttons & VITA_BUTTON_L) ? 255 : 0;
	gamepad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = (buttons & VITA_BUTTON_R) ? 255 : 0;
	stick(pad.lx, pad.ly, deadzone, 100, 0, 0, &gamepad->sThumbLX, &gamepad->sThumbLY);
	stick(pad.rx, pad.ry, deadzone, sensitivity, curve, invert, &gamepad->sThumbRX, &gamepad->sThumbRY);
}
