/*
VITA_PAD.C

The Vita's controls as the Xbox controller on port 0 (and a PS TV's other
controllers on ports 1 to 3: split screen, below), by default in Xita's
layout (the settings panel's Controls tab puts each Xbox button on another
Vita button and gives the touch zones Xbox buttons: vita_controls.c):
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

Gyro aiming (the panel's Controls tab, vita_controls.h): what the Vita turned,
in play, waits here for the game's look code, which takes it once a frame
through vita_pad_gyro_look (port/linux/src/xinput_sdl.c halo_linux_mouse_look)
and adds it to the facing change as mouse aim is added, divided by the
zoom. In hold mode the gyro button only aims, in play.

The game's button icons (the panel's Button icons): vita_pad_button_glyph
tells the HUD (source/interface/hud_draw.c) the Vita button an Xbox button
is on now, the menus' or play's, with the settings read here.
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

/* the zoom level of a local player, NONE (-1) unzoomed (source/game/player_control.c) */
extern short player_control_get_zoom_level(short local_player_index);

/* the button icons' settings (the panel's Button icons, vita_controls.h):
whether they are PlayStation's, and the layout they follow, copied where
the settings are read (the pad's reader, which the panel's changes reach
first) for the HUD that draws them (vita_pad_button_glyph) */
static volatile int icons_playstation;
static struct vita_controls_config icons_controls;

/* the gyro's settings, and what it turned that the game has not taken */
static struct vita_gyro_config gyro_config;
static struct vita_gyro_state gyro_state;

/* (the game's look code, once a frame while the player can look) the
gyro's yaw and pitch since the last call, in radians; 0 if none */
int vita_pad_gyro_look(short gamepad_index, float *yaw, float *pitch)
{
	/* (debug) HALO_GYRO_LOG=1: what the game took, in degrees, a line
	every 30 frames while it turns */
	static int log_wanted = -1, logged_frames;
	static float logged_yaw, logged_pitch;
	int result;

	*yaw = *pitch = 0.0f;
	if (gamepad_index != 0)
		return 0;
	result = vita_gyro_take(&gyro_state, &gyro_config, player_control_get_zoom_level(0) >= 0, yaw, pitch);
	if (log_wanted < 0)
		log_wanted = getenv("HALO_GYRO_LOG") && atoi(getenv("HALO_GYRO_LOG"));
	if (log_wanted)
	{
		logged_yaw += *yaw * 57.29578f;
		logged_pitch += *pitch * 57.29578f;
		if (++logged_frames >= 30)
		{
			if (logged_yaw != 0.0f || logged_pitch != 0.0f)
				platform_log("gyro: the game took yaw %+.2f pitch %+.2f degrees (mode %d, %.2fx, zoom %d)",
					logged_yaw, logged_pitch, gyro_config.mode, gyro_config.sensitivity,
					player_control_get_zoom_level(0));
			logged_yaw = logged_pitch = 0.0f;
			logged_frames = 0;
		}
	}
	return result;
}

/* (the game's button icons, source/interface/hud_draw.c) the glyph of
the Vita button an Xbox button (the game's numbering, 0 to 15) is on now,
in the menus or in play: VITA_GLYPH_NONE while the icons are the Xbox's or
the button is on none */
int vita_pad_button_glyph(short gamepad_button)
{
	if (!icons_playstation)
		return VITA_GLYPH_NONE;
	return vita_button_glyph(&icons_controls, gamepad_button, vita_menus_active);
}

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

/* the sticks' settings, the zones' and the Xbox buttons' (the panel's
Controls tab), every controller's */
static int deadzone = -1, sensitivity, curve, invert;
static struct vita_controls_config controls;

/* (read again after a change in the settings panel) */
static void settings_read(void)
{
	static unsigned long settings_seen;

	if (deadzone >= 0 && settings_seen == halo_settings_generation)
		return;
	settings_seen = halo_settings_generation;
	deadzone = setting("XV_DEADZONE", 0, 0, 99);
	sensitivity = setting("XV_LOOK_SENS", 100, 0, 400);
	curve = setting("XV_LOOK_CURVE", 0, 0, 2);
	invert = setting("XV_INVERT_Y", 0, 0, 1);
	vita_controls_config_load(&controls);
	vita_gyro_config_load(&gyro_config);
	icons_controls = controls;
	icons_playstation = vita_button_icons_playstation();
}

/* a pad's buttons, touch zones and sticks as the Xbox controller's: crouch
is the left stick's click, which the Vita does not have; HALO_CROUCH_TOGGLE
(the panel's Crouch, on by default) makes a press crouch and the next one
stand (vita_controls.c) */
static void pad_gamepad(const struct vita_host_pad *pad, struct vita_controls_state *state, XINPUT_GAMEPAD *gamepad)
{
	struct vita_controls_output output;
	int index;

	vita_controls_map(&controls, state, pad->buttons, pad->touch, vita_menus_active, &output);
	gamepad->wButtons |= (WORD)output.digital;
	for (index = 0; index < 8; index++)
		gamepad->bAnalogButtons[index] = output.analog[index];
	stick(pad->lx, pad->ly, deadzone, 100, 0, 0, &gamepad->sThumbLX, &gamepad->sThumbLY);
	stick(pad->rx, pad->ry, deadzone, sensitivity, curve, invert, &gamepad->sThumbRX, &gamepad->sThumbRY);
}

/* whether the settings panel (or the system's keyboard or dialog) had the
buttons at controller 0's last read: the other controllers wait too */
static volatile int settings_have_input;

void vita_pad_state(XINPUT_GAMEPAD *gamepad)
{
	/* the crouch toggle's state */
	static struct vita_controls_state controls_state;
	struct vita_host_pad pad;

	settings_read();
	vita_host_pad_read(&pad);
	/* the settings panel has the buttons (and the touch zones) while it is
	open; the gyro waits */
	settings_have_input = vita_settings_input(&pad);
	if (settings_have_input)
	{
		vita_gyro_accumulate(&gyro_state, &gyro_config, pad.gyro, 0);
		gamepad->sThumbLX = gamepad->sThumbLY = gamepad->sThumbRX = gamepad->sThumbRY = 0;
		return;
	}
	/* (game chat's Back + Y and Back + D-pad: vita_settings.c) */
	pad.buttons = vita_settings_game_buttons(pad.buttons, vita_menus_active);
	/* the gyro, in play: in hold mode its button only aims */
	vita_gyro_accumulate(&gyro_state, &gyro_config, pad.gyro,
		vita_gyro_active(&gyro_config, pad.buttons, vita_menus_active));
	if (gyro_config.mode == VITA_GYRO_HOLD && !vita_menus_active)
		pad.buttons &= ~gyro_config.button;
	pad_gamepad(&pad, &controls_state, gamepad);
}

/* ---------- a PS TV's other controllers (split screen)

The game's controllers 1 to 3 are a PS TV's other DualShocks (sceCtrl ports
2 to 4: vita_controls.h), with controller 0's layout and settings (the
panel's Controls tab; their L2 and R2 are L and R too, L3 and R3 the
sticks' clicks), no touch zones and no gyro. The settings panel opens only
from controller 0, and while it is open the others are at rest too. The
Vita has none: its split screen stays one player. */

unsigned long vita_pad_extra_connected(void)
{
	return vita_host_pad_extra_connected();
}

void vita_pad_extra_state(int controller, XINPUT_GAMEPAD *gamepad)
{
	static struct vita_controls_state controls_states[1 + VITA_EXTRA_CONTROLLERS];
	struct vita_host_pad pad;

	if (controller < 1 || controller > VITA_EXTRA_CONTROLLERS || !vita_host_pad_extra_read(controller, &pad) ||
		settings_have_input)
	{
		return;
	}
	settings_read();
	pad.touch = 0;
	pad_gamepad(&pad, &controls_states[controller], gamepad);
}

/* the game's rumble (the Xbox's left motor the large one, the right the
small): a PS TV's DualShocks, controller 0's too; the Vita has no motor */
void vita_pad_rumble(int controller, unsigned short left, unsigned short right)
{
	vita_host_pad_rumble(controller, left >> 8, right >> 8);
}
