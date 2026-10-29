/*
BUTTON_ICONS_TEST.C

A desktop test of the desktop build's button icons (port/linux/src/
button_glyphs.c, included whole; port/linux/include/halo_button_icons.h):
the key or mouse button each of the game's buttons shows with the
keyboard's icons, in the menus (their fixed keys, checked against
xinput_sdl.c's keyboard_menus by run_button_icons_test.sh) and in play (the
player's bindings, halo_key_bindings.h, at the MCC defaults and rebound,
through the profile's button layout);
display.button_icons and HALO_BUTTON_ICONS (auto, keyboard, xbox,
playstation, read each time), auto following the device used last (the
keyboard before any, an Xbox pad's, a PlayStation pad's); the glyph
halo_button_glyph hands hud_draw.c for each; and the keys' list (one square
cell each, the wider keycaps fitting the atlas's rows).

Run port/vita/tests/run_button_icons_test.sh.
*/

#define HALO_LINUX 1

#include "../../linux/src/button_glyphs.c"
#include "../../linux/src/key_bindings.c"

#include <stdio.h>
#include <strings.h>

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

/* (button_glyphs.c's: config.toml's value and the device used last, the
test's) */
static const char *test_configured = "auto";
static int test_device = _halo_input_device_keyboard;

const char *config_string(const char *name)
{
	return !strcmp(name, "display.button_icons") ? test_configured : "";
}

int platform_input_device_last(void)
{
	return test_device;
}

/* (xinput_sdl.c's: the bindings in force, the test's; SDL's names of a
few keys, by scancode) */
static struct halo_bindings test_bindings;

static int test_scancode_from_name(const char *name)
{
	static const struct
	{
		int scancode;
		const char *name;
	} names[] =
	{
		{ 4, "A" }, { 6, "C" }, { 7, "D" }, { 8, "E" }, { 9, "F" }, { 10, "G" }, { 11, "H" }, { 20, "Q" }, { 21, "R" },
		{ 22, "S" }, { 26, "W" }, { 29, "Z" }, { 42, "Backspace" }, { 43, "Tab" }, { 44, "Space" }, { 224, "Left Ctrl" },
		{ 225, "Left Shift" }, { 25, "V" }, { 52, "'" },
	};
	size_t index;

	for (index = 0; index < sizeof(names) / sizeof(names[0]); index++)
	{
		if (!strcasecmp(names[index].name, name))
			return names[index].scancode;
	}
	return 0;
}

const struct halo_bindings *halo_key_bindings(void)
{
	return &test_bindings;
}

/* (input_abstraction.c's: the profile's button layout, the Xbox's
default, or Green Thumb's - jump on B, melee on A, ...) */
static int test_green_thumb;

short input_abstraction_port_control_button(short controller_index, short control_index)
{
	static const short standard[12] = { 0, 4, 2, 3, 1, 5, 6, 7, 12, 13, 14, 15 };
	static const short green_thumb[12] = { 1, 4, 2, 3, 0, 5, 6, 7, 12, 13, 14, 15 };

	(void)controller_index;
	return control_index >= 0 && control_index < 12 ? (test_green_thumb ? green_thumb : standard)[control_index] : -1;
}

/* the game's buttons (source/input/input.h) */
enum
{
	A, B, X, Y, BLACK, WHITE, LT, RT, UP, DOWN, LEFT, RIGHT, START, BACK, LS, RS, LEFT_STICK, RIGHT_STICK
};

static void test_keys(void)
{
	/* (port/linux/README.md's Controls, the first key of each) */
	static const struct
	{
		int button;
		int play;
		int menus;
		const char *what;
	} expected[] =
	{
		{ A, HALO_KEY_SPACE, HALO_KEY_ENTER, "A: Space in play (jump), Enter in the menus" },
		{ B, HALO_KEY_F, HALO_KEY_ESC, "B: F in play (melee), Escape in the menus (back; Backspace too)" },
		{ X, HALO_KEY_E, HALO_KEY_E, "X: E (the action; R reloads, apart)" },
		{ Y, HALO_KEY_TAB, HALO_KEY_TAB, "Y: Tab (switch weapons)" },
		{ BLACK, HALO_KEY_H, HALO_KEY_X, "Black: H in play (switch grenades), X in the menus" },
		{ WHITE, HALO_KEY_Q, HALO_KEY_Q, "White: Q (flashlight)" },
		{ LT, HALO_KEY_G, HALO_KEY_G, "left trigger: G (throw a grenade, as MCC)" },
		{ RT, HALO_KEY_LMB, -1, "right trigger: left mouse button in play, none in the menus" },
		{ UP, HALO_KEY_UP, HALO_KEY_UP, "D-pad up: the up arrow" },
		{ DOWN, HALO_KEY_DOWN, HALO_KEY_DOWN, "D-pad down: the down arrow" },
		{ LEFT, HALO_KEY_LEFT, HALO_KEY_LEFT, "D-pad left: the left arrow" },
		{ RIGHT, HALO_KEY_RIGHT, HALO_KEY_RIGHT, "D-pad right: the right arrow" },
		{ START, HALO_KEY_ESC, HALO_KEY_SPACE, "Start: Escape in play (the pause menu), Space in the menus" },
		{ BACK, HALO_KEY_BACKSPACE, HALO_KEY_F1, "Back: Backspace in play (the scores), F1 in the menus" },
		{ LS, HALO_KEY_CTRL, HALO_KEY_CTRL, "left stick click: Ctrl (crouch)" },
		{ RS, HALO_KEY_RMB, HALO_KEY_Z, "right stick click: the right button in play (zoom, as MCC), Z in the menus" },
		{ LEFT_STICK, HALO_KEY_WASD, HALO_KEY_WASD, "the left stick: W A S D" },
		{ RIGHT_STICK, HALO_KEY_MOUSE, -1, "the right stick: the mouse in play, none in the menus" },
	};
	size_t index;

	for (index = 0; index < sizeof(expected) / sizeof(expected[0]); index++)
	{
		check(halo_keyboard_button_key(expected[index].button, 0) == expected[index].play &&
			halo_keyboard_button_key(expected[index].button, 1) == expected[index].menus, expected[index].what);
	}
	check(halo_keyboard_button_key(-1, 0) == -1 && halo_keyboard_button_key(18, 1) == -1, "no key past the buttons");

	/* rebound: the grenade on V, melee on a key with no keycap first */
	halo_bind_assign(&test_bindings, HALO_BIND_THROW_GRENADE, 0, 25, NULL);
	check(halo_keyboard_button_key(LT, 0) == HALO_KEY_V && halo_keyboard_button_key(LT, 1) == HALO_KEY_G,
		"rebound: the left trigger shows V in play, the menus' G still");
	halo_bind_assign(&test_bindings, HALO_BIND_MELEE, 0, 52, NULL);
	check(halo_keyboard_button_key(B, 0) == HALO_KEY_MOUSE4, "rebound: a key with no keycap skipped for the next one's");
	halo_bind_clear(&test_bindings, HALO_BIND_MELEE, 1);
	check(halo_keyboard_button_key(B, 0) == -1, "rebound: none with a keycap: the Xbox's icon");
	/* the profile's button layout moves the buttons, not the keys */
	halo_bind_defaults(&test_bindings);
	test_green_thumb = 1;
	check(halo_keyboard_button_key(B, 0) == HALO_KEY_SPACE && halo_keyboard_button_key(A, 0) == HALO_KEY_F,
		"Green Thumb: B is jump's Space, A melee's F");
	test_green_thumb = 0;
}

static void test_shown(void)
{
	unsetenv("HALO_BUTTON_ICONS");
	test_device = _halo_input_device_keyboard;
	check(halo_button_icons_shown() == _halo_button_icons_keyboard, "auto, before any input: the keyboard's");
	test_device = _halo_input_device_xbox_gamepad;
	check(halo_button_icons_shown() == _halo_button_icons_xbox, "auto, an Xbox pad used last: the Xbox's");
	test_device = _halo_input_device_playstation_gamepad;
	check(halo_button_icons_shown() == _halo_button_icons_playstation, "auto, a PlayStation pad used last: the PlayStation's");
	test_device = _halo_input_device_keyboard;
	check(halo_button_icons_shown() == _halo_button_icons_keyboard, "auto, a key pressed after the pad: the keyboard's again");

	setenv("HALO_BUTTON_ICONS", "xbox", 1);
	check(halo_button_icons_shown() == _halo_button_icons_xbox, "xbox: the Xbox's while the keyboard is used");
	setenv("HALO_BUTTON_ICONS", "playstation", 1);
	check(halo_button_icons_shown() == _halo_button_icons_playstation, "playstation: the PlayStation's");
	setenv("HALO_BUTTON_ICONS", "keyboard", 1);
	test_device = _halo_input_device_xbox_gamepad;
	check(halo_button_icons_shown() == _halo_button_icons_keyboard, "keyboard: the keyboard's while a pad is used");
	setenv("HALO_BUTTON_ICONS", "auto", 1);
	check(halo_button_icons_shown() == _halo_button_icons_xbox, "auto by the variable: the device's");
	setenv("HALO_BUTTON_ICONS", "nonsense", 1);
	check(halo_button_icons_shown() == _halo_button_icons_xbox, "an unknown value: auto");
	unsetenv("HALO_BUTTON_ICONS");
}

static void test_glyphs(void)
{
	unsetenv("HALO_TEST_BUTTON_GLYPHS");
	setenv("HALO_BUTTON_ICONS", "keyboard", 1);
	check(halo_button_glyph(A, 0) == HALO_GLYPH_KEY_FIRST + HALO_KEY_SPACE, "keyboard: A in play is Space's keycap");
	check(halo_button_glyph(A, 1) == HALO_GLYPH_KEY_FIRST + HALO_KEY_ENTER, "keyboard: A in the menus is Enter's");
	check(halo_button_glyph(RT, 0) == HALO_GLYPH_KEY_FIRST + HALO_KEY_LMB, "keyboard: the right trigger in play is the left mouse button");
	check(halo_button_glyph(RT, 1) == VITA_GLYPH_NONE, "keyboard: the right trigger in the menus keeps the Xbox's icon");
	check(halo_button_glyph(LEFT_STICK, 0) == HALO_GLYPH_KEY_FIRST + HALO_KEY_WASD, "keyboard: the left stick's icon is W A S D");
	setenv("HALO_BUTTON_ICONS", "xbox", 1);
	check(halo_button_glyph(A, 0) == VITA_GLYPH_NONE && halo_button_glyph(START, 1) == VITA_GLYPH_NONE,
		"xbox: the game's own icons");
	setenv("HALO_BUTTON_ICONS", "playstation", 1);
	check(halo_button_glyph(A, 1) == VITA_GLYPH_CROSS && halo_button_glyph(LT, 1) == VITA_GLYPH_L &&
		halo_button_glyph(BACK, 0) == VITA_GLYPH_SELECT, "playstation: the Vita's shipped layout, as before");
	check(halo_button_glyph(LEFT_STICK, 0) == VITA_GLYPH_NONE, "playstation: the sticks keep the game's icons");
	setenv("HALO_TEST_BUTTON_GLYPHS", ",,64", 1);
	check(halo_button_glyph(X, 0) == HALO_GLYPH_KEY_FIRST, "HALO_TEST_BUTTON_GLYPHS: a keycap forced");
	unsetenv("HALO_TEST_BUTTON_GLYPHS");
	unsetenv("HALO_BUTTON_ICONS");
	test_device = _halo_input_device_keyboard;
	check(halo_button_glyph(Y, 0) == HALO_GLYPH_KEY_FIRST + HALO_KEY_TAB, "auto (the default): the keyboard's at first");
	test_device = _halo_input_device_xbox_gamepad;
	check(halo_button_glyph(Y, 0) == VITA_GLYPH_NONE, "auto: the Xbox's once a pad is used");
}

static void test_list(void)
{
#define WIDE(name, compact, wide, wide_width) + ((wide_width) > 0)
#define WIDE_ES(name, wide, wide_width) + 1
#define WIDTH_OK(name, compact, wide, wide_width) && ((wide_width) == 0 || ((wide_width) > 32 && (wide_width) <= 64))
	int wides = 0 HALO_KEY_GLYPH_LIST(WIDE) HALO_KEY_GLYPH_LIST_ES(WIDE_ES);
	int widths = 1 HALO_KEY_GLYPH_LIST(WIDTH_OK);

	check((int)HALO_GLYPH_KEY_FIRST >= (int)VITA_GLYPH_COUNT && (int)HALO_GLYPH_KEY_FIRST + (int)HALO_KEY_COUNT <= (int)HALO_GLYPH_XBOX_FIRST,
		"the keys' glyphs after the PlayStation's, before the Xbox's");
	check((HALO_KEY_COUNT + 7) / 8 <= HALO_KEY_ATLAS_WIDE_ROW, "the square keycaps above the wider ones' rows");
	check(HALO_KEY_ATLAS_WIDE_ROW * 32 + (wides + 3) / 4 * 32 <= HALO_KEY_ATLAS_SIZE, "the wider keycaps fit the atlas");
	check(widths, "each wider keycap 33 to 64 pixels across");
	check(HALO_GLYPH_XBOX_COUNT <= (HALO_XBOX_ATLAS_WIDTH / 32) * (HALO_XBOX_ATLAS_HEIGHT / 32), "the Xbox's fit theirs");
}

int main(void)
{
	halo_bind_set_key_names(test_scancode_from_name, NULL);
	halo_bind_defaults(&test_bindings);
	test_keys();
	test_shown();
	test_glyphs();
	test_list();
	printf("%d of %d checks passed\n", checks - failures, checks);
	return failures ? 1 : 0;
}
