/*
BUTTON_GLYPHS.C

The Linux harness's PlayStation button icons, so that its tests can draw
them as the Vita does (source/interface/hud_draw.c, HALO_BUTTON_GLYPHS):
HALO_BUTTON_ICONS=playstation (or the default, VITA_BUTTON_ICONS_DEFAULT in
port/vita/include/vita_controls.h, when unset) shows each of the game's
button icons as the Vita button its Xbox button is on in the Vita's shipped
layout - the menus' fixed one (A Cross, B Circle, X Square, Y Triangle, the
triggers L and R, the D-pad, Start, Back Select) or play's (the Button
layout as shipped: Black and White on the D-pad's left and right, the
sticks' clicks on its down and up). The Vita's own is port/vita/platform/
vita_pad.c's, which follows the player's Button layout and touch zones.
*/

#if defined(HALO_LINUX) && defined(__linux__) && !defined(HALO_ANDROID) && !defined(HALO_VITA)

#include "../../vita/include/vita_controls.h"

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

static int icons_playstation(void)
{
	const char *value = getenv("HALO_BUTTON_ICONS");

	if (value && strcmp(value, "playstation") == 0)
		return 1;
	if (value && strcmp(value, "xbox") == 0)
		return 0;
	return VITA_BUTTON_ICONS_DEFAULT == 1;
}

/* (hud_draw.c) the glyph of the Vita button an Xbox button is on, in the
menus or in play: VITA_GLYPH_NONE while the icons are the Xbox's (read
each time: HALO_TEST_COMMANDS' "@set HALO_BUTTON_ICONS ..." is live) */
int halo_button_glyph(short gamepad_button, int menus)
{
	if (!icons_playstation() || gamepad_button < 0 || gamepad_button >= 16)
		return VITA_GLYPH_NONE;
#ifndef HALO_RELEASE
	{
		/* (test) HALO_TEST_BUTTON_GLYPHS="g,g,...": the glyphs (VITA_GLYPH_*,
		by number) of the game's buttons 0, 1, 2 ... in place of either
		layout's, so a screen's few icons can show the others (L, START,
		the D-pad ...) */
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

			return glyph > VITA_GLYPH_NONE && glyph < VITA_GLYPH_COUNT ? glyph : VITA_GLYPH_NONE;
		}
	}
#endif
	return menus ? menu_glyphs[gamepad_button] : play_glyphs[gamepad_button];
}

#else
/* (another platform: the Vita's are vita_pad.c's; none elsewhere) */
typedef int button_glyphs_none;
#endif
