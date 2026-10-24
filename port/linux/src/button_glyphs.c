/*
BUTTON_GLYPHS.C

The desktop build's button icons (port/linux/include/halo_button_icons.h),
which source/interface/hud_draw.c draws in the place of the game's Xbox
icons (HALO_BUTTON_GLYPHS): display.button_icons (HALO_BUTTON_ICONS) is
"auto" (the default: the keyboard and mouse's keys while they were used
last, a gamepad's buttons once it is - an Xbox pad's or, per SDL's gamepad
type, a PlayStation's), "keyboard", "xbox" or "playstation".

- The keyboard's: each button's key or mouse button, the menus' fixed one
  (xinput_sdl.c keyboard_menus; the mouse being the pointer there) or, in
  play, the player's binding of the action the button is (play_key).
- The PlayStation's: the Vita button its Xbox button is on in the Vita's
  shipped layout - the menus' fixed one (A Cross, B Circle, X Square, Y
  Triangle, the triggers L and R, the D-pad, Start, Back Select) or play's
  (the Button layout as shipped: Black and White on the D-pad's left and
  right, the sticks' clicks on its down and up), as the harness's tests
  draw them. The Vita's own is port/vita/platform/vita_pad.c's, which
  follows the player's Button layout and touch zones.
- The Xbox's: the game's own icons (VITA_GLYPH_NONE).
*/

#if defined(HALO_LINUX) && (defined(__linux__) || defined(_WIN32)) && !defined(HALO_ANDROID) && !defined(HALO_VITA)

#include "../../vita/include/vita_controls.h"
#include "../include/halo_button_icons.h"
#include "../include/halo_key_bindings.h"
#include "port_config.h"

#include <stdlib.h>
#include <string.h>

int halo_button_glyph(short gamepad_button, int menus);

/* (the game's numbering: A B X Y Black White, the left and right
triggers, the D-pad's up down left right, Start, Back, the sticks' clicks;
vita_controls.c vita_button_glyph's with the shipped layout) */
static const signed char menu_glyphs[16] = {
	VITA_GLYPH_CROSS, VITA_GLYPH_CIRCLE, VITA_GLYPH_SQUARE, VITA_GLYPH_TRIANGLE, VITA_GLYPH_NONE, VITA_GLYPH_NONE,
	VITA_GLYPH_L, VITA_GLYPH_R, VITA_GLYPH_UP, VITA_GLYPH_DOWN, VITA_GLYPH_LEFT, VITA_GLYPH_RIGHT, VITA_GLYPH_START,
	VITA_GLYPH_SELECT, VITA_GLYPH_NONE, VITA_GLYPH_NONE,
};
static const signed char play_glyphs[16] = {
	VITA_GLYPH_CROSS, VITA_GLYPH_CIRCLE, VITA_GLYPH_SQUARE, VITA_GLYPH_TRIANGLE, VITA_GLYPH_LEFT, VITA_GLYPH_RIGHT,
	VITA_GLYPH_L, VITA_GLYPH_R, VITA_GLYPH_NONE, VITA_GLYPH_NONE, VITA_GLYPH_NONE, VITA_GLYPH_NONE, VITA_GLYPH_START,
	VITA_GLYPH_SELECT, VITA_GLYPH_DOWN, VITA_GLYPH_UP,
};

/* the key shown for each button (and the sticks, 16 and 17). In the menus
xinput_sdl.c's fixed keys (the mouse being the pointer there: Enter accepts,
Escape backs out - Backspace too -, Space is Start, G the left trigger; the
right trigger has none). In play the player's bindings
(halo_key_bindings.h): the button's game control (the profile's button
layout: input_abstraction_port_control_button) is an action's, whose first
key with a keycap is shown (none: the Xbox's icon); Start is Escape, Back the
scoreboard's key, the D-pad the arrows, the sticks W A S D and the mouse.
Change these with keyboard_menus and README.md's Controls table */
static const signed char menu_keys[18] = {
	HALO_KEY_ENTER, HALO_KEY_ESC, HALO_KEY_E, HALO_KEY_TAB, HALO_KEY_X, HALO_KEY_Q,
	HALO_KEY_G, -1, HALO_KEY_UP, HALO_KEY_DOWN, HALO_KEY_LEFT, HALO_KEY_RIGHT, HALO_KEY_SPACE,
	HALO_KEY_F1, HALO_KEY_CTRL, HALO_KEY_Z, HALO_KEY_WASD, -1,
};

/* source/input/input_abstraction.c */
short input_abstraction_port_control_button(short controller_index, short control_index);

/* the keycap (a halo_key) an input is drawn with, -1 for one with none:
SDL's scancodes (the USB HID usage numbers) and the mouse's */
int halo_key_of_input(int input)
{
	static const struct
	{
		short input;
		signed char key;
	} keys[] =
	{
		{ 44, HALO_KEY_SPACE }, { 40, HALO_KEY_ENTER }, { 88, HALO_KEY_ENTER }, { 41, HALO_KEY_ESC },
		{ 43, HALO_KEY_TAB }, { 42, HALO_KEY_BACKSPACE }, { 76, HALO_KEY_DELETE }, { 224, HALO_KEY_CTRL },
		{ 228, HALO_KEY_CTRL }, { 225, HALO_KEY_SHIFT }, { 229, HALO_KEY_SHIFT }, { 58, HALO_KEY_F1 },
		{ 82, HALO_KEY_UP }, { 81, HALO_KEY_DOWN }, { 80, HALO_KEY_LEFT }, { 79, HALO_KEY_RIGHT },
		{ 8, HALO_KEY_E }, { 9, HALO_KEY_F }, { 21, HALO_KEY_R }, { 20, HALO_KEY_Q }, { 27, HALO_KEY_X },
		{ 10, HALO_KEY_G }, { 6, HALO_KEY_C }, { 29, HALO_KEY_Z }, { 26, HALO_KEY_W }, { 4, HALO_KEY_A },
		{ 22, HALO_KEY_S }, { 7, HALO_KEY_D }, { 11, HALO_KEY_H }, { 25, HALO_KEY_V }, { 23, HALO_KEY_T },
		{ 5, HALO_KEY_B }, { 30, HALO_KEY_N1 }, { 31, HALO_KEY_N2 }, { 32, HALO_KEY_N3 },
		{ HALO_BIND_MOUSE + 1, HALO_KEY_LMB }, { HALO_BIND_MOUSE + 2, HALO_KEY_MMB }, { HALO_BIND_MOUSE + 3, HALO_KEY_RMB },
		{ HALO_BIND_MOUSE + 4, HALO_KEY_MOUSE4 }, { HALO_BIND_WHEEL, HALO_KEY_WHEEL }, { HALO_BIND_WHEEL_UP, HALO_KEY_WHEEL },
		{ HALO_BIND_WHEEL_DOWN, HALO_KEY_WHEEL },
	};
	size_t index;

	for (index = 0; index < sizeof(keys) / sizeof(keys[0]); index++)
	{
		if (keys[index].input == input)
			return keys[index].key;
	}
	return -1;
}

/* an action's first input with a keycap, -1 for none */
static int action_key(int action)
{
	const struct halo_bindings *bindings = halo_key_bindings();
	int slot;

	for (slot = 0; slot < HALO_BIND_SLOTS; slot++)
	{
		int key = halo_key_of_input(bindings->inputs[action][slot]);

		if (key >= 0)
			return key;
	}
	return -1;
}

/* the game's controls' actions (input_abstraction.c's _game_control_*:
jump, switch grenades, action, switch weapons, melee, flashlight, grenade,
fire, start, back, crouch, zoom) */
static const signed char control_actions[12] = {
	HALO_BIND_JUMP, HALO_BIND_SWITCH_GRENADE, HALO_BIND_ACTION, HALO_BIND_SWITCH_WEAPON, HALO_BIND_MELEE,
	HALO_BIND_FLASHLIGHT, HALO_BIND_THROW_GRENADE, HALO_BIND_FIRE, -1, -1, HALO_BIND_CROUCH, HALO_BIND_ZOOM,
};

static int play_key(int gamepad_button)
{
	short control;

	switch (gamepad_button)
	{
	case 8: return HALO_KEY_UP;
	case 9: return HALO_KEY_DOWN;
	case 10: return HALO_KEY_LEFT;
	case 11: return HALO_KEY_RIGHT;
	case 12: return HALO_KEY_ESC;
	case 13:
	{
		int key = action_key(HALO_BIND_SCOREBOARD);

		return key >= 0 ? key : HALO_KEY_F1;
	}
	case 16: return HALO_KEY_WASD;
	case 17: return HALO_KEY_MOUSE;
	}
	for (control = 0; control < 12; control++)
	{
		if (control_actions[control] >= 0 && input_abstraction_port_control_button(0, control) == gamepad_button)
			return action_key(control_actions[control]);
	}
	return -1;
}

int halo_keyboard_button_key(int gamepad_button, int menus)
{
	if (gamepad_button < 0 || gamepad_button >= 18)
		return -1;
	return menus ? menu_keys[gamepad_button] : play_key(gamepad_button);
}

int halo_button_icons_shown(void)
{
	/* (the file's value, read once; the variable each time: @set) */
	static const char *configured;
	const char *value = getenv("HALO_BUTTON_ICONS");

	if (!value || !*value)
	{
		if (!configured)
			configured = config_string("display.button_icons");
		value = configured;
	}
	if (!strcmp(value, "playstation"))
		return _halo_button_icons_playstation;
	if (!strcmp(value, "xbox"))
		return _halo_button_icons_xbox;
	if (!strcmp(value, "keyboard"))
		return _halo_button_icons_keyboard;
	/* ("auto", or anything else) */
	switch (platform_input_device_last())
	{
	case _halo_input_device_playstation_gamepad:
		return _halo_button_icons_playstation;
	case _halo_input_device_xbox_gamepad:
		return _halo_button_icons_xbox;
	default:
		return _halo_button_icons_keyboard;
	}
}

/* (hud_draw.c) the glyph shown for one of the game's buttons (and, with
the keyboard's, the sticks: 16 and 17), in the menus or in play:
VITA_GLYPH_NONE for the Xbox's icon */
int halo_button_glyph(short gamepad_button, int menus)
{
	int shown = halo_button_icons_shown();

	if (gamepad_button < 0 || gamepad_button >= 18 || shown == _halo_button_icons_xbox)
		return VITA_GLYPH_NONE;
#ifndef HALO_RELEASE
	{
		/* (test) HALO_TEST_BUTTON_GLYPHS="g,g,...": the glyphs (VITA_GLYPH_*,
		HALO_GLYPH_KEY_FIRST + a halo_key ..., by number) of the game's
		buttons 0, 1, 2 ... in place of the icons', so a screen's few icons
		can show the others (L, START, the D-pad, the wider keycaps ...) */
		const char *test = getenv("HALO_TEST_BUTTON_GLYPHS");
		int index;

		for (index = 0; test && *test && index < gamepad_button; index++)
		{
			test = strchr(test, ',');
			test = test ? test + 1 : NULL;
		}
		if (test && *test && *test != ',')
		{
			int glyph = atoi(test);

			if ((glyph > VITA_GLYPH_NONE && glyph < VITA_GLYPH_COUNT) ||
				(glyph >= HALO_GLYPH_KEY_FIRST && glyph < HALO_GLYPH_KEY_FIRST + HALO_KEY_COUNT) ||
				(glyph >= HALO_GLYPH_XBOX_FIRST && glyph < HALO_GLYPH_XBOX_FIRST + HALO_GLYPH_XBOX_COUNT))
			{
				return glyph;
			}
			return VITA_GLYPH_NONE;
		}
	}
#endif
	if (shown == _halo_button_icons_keyboard)
	{
		int key = halo_keyboard_button_key(gamepad_button, menus);

		return key >= 0 ? HALO_GLYPH_KEY_FIRST + key : VITA_GLYPH_NONE;
	}
	if (gamepad_button >= 16)
		return VITA_GLYPH_NONE;
	return menus ? menu_glyphs[gamepad_button] : play_glyphs[gamepad_button];
}

#else
/* (another platform: the Vita's are vita_pad.c's; none elsewhere) */
typedef int button_glyphs_none;
#endif
