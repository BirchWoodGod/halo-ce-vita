/*
BUTTON_GLYPHS.C

The desktop build's button icons (port/linux/include/halo_button_icons.h),
which source/interface/hud_draw.c draws in the place of the game's Xbox
icons (HALO_BUTTON_GLYPHS): display.button_icons (HALO_BUTTON_ICONS) is
"auto" (the default: the keyboard and mouse's keys while they were used
last, a gamepad's buttons once it is - an Xbox pad's or, per SDL's gamepad
type, a PlayStation's), "keyboard", "xbox" or "playstation".

- The keyboard's: each button's key or mouse button in xinput_sdl.c's
  layout (keyboard_gamepad), the menus' (the mouse being the pointer there)
  or play's.
- The PlayStation's: the Vita button its Xbox button is on in the Vita's
  shipped layout - the menus' fixed one (A Cross, B Circle, X Square, Y
  Triangle, the triggers L and R, the D-pad, Start, Back Select) or play's
  (the Button layout as shipped: Black and White on the D-pad's left and
  right, the sticks' clicks on its down and up), as the harness's tests
  draw them. The Vita's own is port/vita/platform/vita_pad.c's, which
  follows the player's Button layout and touch zones.
- The Xbox's: the game's own icons (VITA_GLYPH_NONE).
*/

#if defined(HALO_LINUX) && defined(__linux__) && !defined(HALO_ANDROID) && !defined(HALO_VITA)

#include "../../vita/include/vita_controls.h"
#include "../include/halo_button_icons.h"
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

/* the key shown for each button (and the sticks, 16 and 17), the first of
those xinput_sdl.c keyboard_gamepad puts on it: in play the mouse's
buttons fire, throw and zoom; in the menus they are the pointer's, so the
keys (Enter accepts, Backspace backs out, G the left trigger; the right
trigger has none). Change these with keyboard_gamepad and README.md's
Controls table */
static const signed char play_keys[18] = {
	HALO_KEY_SPACE, HALO_KEY_F, HALO_KEY_E, HALO_KEY_TAB, HALO_KEY_X, HALO_KEY_Q,
	HALO_KEY_RMB, HALO_KEY_LMB, HALO_KEY_UP, HALO_KEY_DOWN, HALO_KEY_LEFT, HALO_KEY_RIGHT, HALO_KEY_ESC,
	HALO_KEY_F1, HALO_KEY_CTRL, HALO_KEY_MMB, HALO_KEY_WASD, HALO_KEY_MOUSE,
};
static const signed char menu_keys[18] = {
	HALO_KEY_ENTER, HALO_KEY_BACKSPACE, HALO_KEY_E, HALO_KEY_TAB, HALO_KEY_X, HALO_KEY_Q,
	HALO_KEY_G, -1, HALO_KEY_UP, HALO_KEY_DOWN, HALO_KEY_LEFT, HALO_KEY_RIGHT, HALO_KEY_ESC,
	HALO_KEY_F1, HALO_KEY_CTRL, HALO_KEY_Z, HALO_KEY_WASD, -1,
};

int halo_keyboard_button_key(int gamepad_button, int menus)
{
	if (gamepad_button < 0 || gamepad_button >= 18)
		return -1;
	return menus ? menu_keys[gamepad_button] : play_keys[gamepad_button];
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
