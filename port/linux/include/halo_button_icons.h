/*
HALO_BUTTON_ICONS.H

The desktop build's button icons (display.button_icons, HALO_BUTTON_ICONS):
the keyboard and mouse's keys, the Xbox controller's or the PlayStation's,
chosen by the setting, or, on auto (the default), by the device the player
last used (Halo PC's way): a key or a mouse button pressed shows the
keyboard's, a gamepad's button or stick its own (a PlayStation pad's, per
SDL's gamepad type, the PlayStation's).

A glyph (hud_draw.c's, source/interface/hud_draw.h) is a VITA_GLYPH_*
(port/vita/include/vita_controls.h: the PlayStation's), or one of these:
HALO_GLYPH_KEY_FIRST + a halo_key (a keycap or a mouse button, drawn from
the menus folder's halo3/key_atlas.png) or HALO_GLYPH_XBOX_FIRST + one of
the game's buttons (the Xbox controller's own button, drawn from
halo3/xbox_atlas.png: the Halo 3 menus' hints, whose pictures are the
PlayStation's). Both atlases are drawn by tools/halo3_menu_art.py, which
reads the keys' labels and sizes from HALO_KEY_GLYPH_LIST below: our own
art, no game's or icon pack's. None of it is the Vita's (HALO_VITA), which
keeps its own icons (vita_pad.c).

Which key each of the game's buttons is on is port/linux/src/xinput_sdl.c's
fixed layout (keyboard_gamepad; port/linux/README.md "Controls"): the
desktop build has no remapping. In the menus the mouse drives the pointer
(its buttons never reach the controller), so a button shows its key there.
*/

#ifndef HALO_BUTTON_ICONS_H
#define HALO_BUTTON_ICONS_H

enum
{
	/* (after the VITA_GLYPH_*s, below a short's reach) */
	HALO_GLYPH_KEY_FIRST = 64,
	HALO_GLYPH_XBOX_FIRST = 128,
	/* (the Xbox's glyphs: the game's sixteen buttons in its order, then
	the D-pad's left and right as one, the browsers' filter) */
	HALO_GLYPH_XBOX_DPAD_LEFT_RIGHT = 16,
	HALO_GLYPH_XBOX_COUNT = 17,
};

/* K(name, compact, wide, wide_width): a key's glyph in a square keycap
(`compact`: its label, or "@word" for a drawn symbol - an arrow, the mouse)
and, when `wide` is not "", in a wider keycap with that label, `wide_width`
pixels across in the atlas's 32-pixel-high cells (at most 64): the wider is
drawn where the icon is in a line of text (the HUD's messages, the Halo CE
menus' strings), the square in a button hint's fixed box. The order is the
atlas's (tools/halo3_menu_art.py reads this list) */
#define HALO_KEY_GLYPH_LIST(K) \
	K(SPACE, "@space", "SPACE", 60) \
	K(ENTER, "@enter", "ENTER", 58) \
	K(ESC, "ESC", "ESC", 44) \
	K(TAB, "TAB", "TAB", 44) \
	K(BACKSPACE, "@backspace", "BKSP", 52) \
	K(DELETE, "DEL", "", 0) \
	K(CTRL, "CTRL", "CTRL", 50) \
	K(SHIFT, "@shift", "SHIFT", 56) \
	K(F1, "F1", "", 0) \
	K(UP, "@up", "", 0) \
	K(DOWN, "@down", "", 0) \
	K(LEFT, "@left", "", 0) \
	K(RIGHT, "@right", "", 0) \
	K(LEFT_RIGHT, "@leftright", "", 0) \
	K(E, "E", "", 0) \
	K(F, "F", "", 0) \
	K(R, "R", "", 0) \
	K(Q, "Q", "", 0) \
	K(X, "X", "", 0) \
	K(G, "G", "", 0) \
	K(C, "C", "", 0) \
	K(Z, "Z", "", 0) \
	K(W, "W", "", 0) \
	K(A, "A", "", 0) \
	K(S, "S", "", 0) \
	K(D, "D", "", 0) \
	K(WASD, "@wasd", "WASD", 56) \
	K(LMB, "@lmb", "", 0) \
	K(RMB, "@rmb", "", 0) \
	K(MMB, "@mmb", "", 0) \
	K(WHEEL, "@wheel", "", 0) \
	K(MOUSE4, "@mouse4", "", 0) \
	K(MOUSE, "@mouse", "", 0)

/* the wider keycaps' labels in Spanish (lang_current "es"), after the
English ones in the atlas: K(name, wide, wide_width) */
#define HALO_KEY_GLYPH_LIST_ES(K) \
	K(SPACE, "ESPACIO", 64) \
	K(ENTER, "INTRO", 54) \
	K(SHIFT, "MAYÚS", 60)

#define HALO_KEY_ENUM(name, compact, wide, wide_width) HALO_KEY_##name,
enum halo_key
{
	HALO_KEY_GLYPH_LIST(HALO_KEY_ENUM)
	HALO_KEY_COUNT
};
#undef HALO_KEY_ENUM

/* the atlas (halo3/key_atlas.png, 256x256): the square keycaps in 32x32
cells, eight to a row, in the list's order; from HALO_KEY_ATLAS_WIDE_ROW's
row the wider ones in 64x32 cells, four to a row, left-aligned in theirs,
the English then the Spanish, each list's in its order. The Xbox's
(halo3/xbox_atlas.png, 256x128): 32x32 cells, eight to a row, in the
game's buttons' order, then the D-pad's left and right */
enum
{
	HALO_KEY_ATLAS_SIZE = 256,
	HALO_KEY_ATLAS_CELL = 32,
	HALO_KEY_ATLAS_WIDE_ROW = 5,
	HALO_XBOX_ATLAS_WIDTH = 256,
	HALO_XBOX_ATLAS_HEIGHT = 128,
};

/* the icons shown: the setting's, auto resolved by the device used last */
enum halo_button_icons
{
	_halo_button_icons_xbox,
	_halo_button_icons_playstation,
	_halo_button_icons_keyboard,
};

/* the device used last (sdl_platform.c platform_input_device_last) */
enum halo_input_device
{
	_halo_input_device_keyboard,
	_halo_input_device_xbox_gamepad,
	_halo_input_device_playstation_gamepad,
};

#if defined(HALO_LINUX) && !defined(HALO_VITA) && !defined(HALO_ANDROID)
/* (button_glyphs.c) the icons shown now: display.button_icons (its
environment variable read each time: "@set HALO_BUTTON_ICONS ..." is
live), "auto" by platform_input_device_last */
int halo_button_icons_shown(void);
/* (button_glyphs.c) the key or mouse button (a halo_key) the keyboard and
mouse press one of the game's buttons with (0 A ... 15 the right stick's
click; 16 and 17 the left and right sticks themselves), in the menus or in
play; -1 for none (the right trigger in the menus: the left button is the
pointer's there) */
int halo_keyboard_button_key(int gamepad_button, int menus);
/* (sdl_platform.c) the device the player used last, and the input that
says so (xinput_sdl.c: the tests' scripted pad is a gamepad's) */
int platform_input_device_last(void);
void platform_input_device_used(int device);
#endif

#endif
