/*
XINPUT_SDL.C

Xbox controllers and the debug keyboard for the Linux build.

Port 0 is always connected: it is the keyboard and mouse, merged with the
first SDL gamepad when one is present. Further SDL gamepads take ports 1-3.

Keyboard and mouse (port 0):
	W A S D          left stick          arrows           D-pad
	mouse            aim (see halo_linux_mouse_look)
	left button      right trigger       right button, G  left trigger
	space, enter     A                   F, backspace, X1 B
	                 (in the menus Space is Start and Escape is B, back)
	E, R             X                   tab, wheel       Y
	Q                white               X                black
	left ctrl, C     left stick click    Z, middle button right stick click
	escape           start               F1               back
	F12              release or recapture the mouse

In the menus the mouse is free and drives a pointer instead
(port/linux/include/halo_ui_pointer.h, source/interface/ui_widget.c): its
motion, buttons and wheel do not reach the controller then.

Mouse aim does not go through the right stick: the game's look code asks
halo_linux_mouse_look for the motion since its last call and adds it to the
stick's facing change, so aiming is direct rather than rate based.

The game's debug keyboard exists only for the console. Backquote (which
opens it) always reaches the keystroke queue, everything else only while
the console is open, since the game also polls a few keys directly (escape
returns to the main menu). While the console is open the keyboard does not
drive the controller.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"
#include "pc_text_entry.h"
#include "../game/game_chat.h"
#ifndef HALO_NOT_DESKTOP
#include "../include/halo_button_icons.h"
#endif

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_COUNT 4
#define VK_OEM_3_BACKQUOTE 0xc0

/* ---------- game hooks */

/* main/console.c */
extern unsigned char console_is_active(void);

/* ---------- device tables */

XPP_DEVICE_TYPE XDEVICE_TYPE_GAMEPAD_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_MEMORY_UNIT_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_DEBUG_KEYBOARD_TABLE;

struct controller
{
	BOOL open;
	DWORD packet_number;
	XINPUT_GAMEPAD previous;
};

static struct controller controllers[PORT_COUNT];
static struct controller keyboard_device;
static DWORD reported_gamepads = 0;
static BOOL reported_keyboard = FALSE;

/* ---------- mouse */

static pthread_mutex_t mouse_lock = PTHREAD_MUTEX_INITIALIZER;
static float mouse_pending_x, mouse_pending_y;
static unsigned long mouse_polls_unconsumed = 0;
static float mouse_wheel_accumulated = 0.0f;
/* (Forge's monitor: halo_linux_forge_keys) the wheel's notches it has not
taken, and its keys held at the last poll */
static float forge_wheel_pending = 0.0f;
static unsigned long forge_keys_held = 0;
/* the wheel's switch (wheel_update): when the wheel last moved, until when
Y is held, and whether a scroll is under way */
static Uint64 wheel_moved_ms = 0;
static Uint64 wheel_press_until_ms = 0;
static BOOL wheel_scrolling = FALSE;

static float mouse_sensitivity(void)
{
	static float sensitivity = -1.0f;

	if (sensitivity < 0.0f)
	{
		sensitivity = (float)config_real("input.mouse_sensitivity");
		if (sensitivity <= 0.0f)
			sensitivity = 1.0f;
	}
	return sensitivity;
}

#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
/* (tests) HALO_TEST_GYRO=on|zoomed|hold (off or unset: none): the desktop
harness's mouse look stands for the Vita's gyro in that mode, with the
Vita's Gyro aim assist setting (HALO_GYRO_ASSIST, default 1), both read as
they are now (@set): halo_linux_look_assist, halo_linux_aim_assist_off.
"@aim gyro <yaw> <pitch>" (main.c) turns it once, in radians (positive
left and up), as the Vita's gyro hands the game a frame's turn, and
"@aim pad lx ly rx ry rt frames" holds the sticks (-1 to 1) and the right
trigger (0 or 1) for that many pad reads (test_input_gamepad) */
static float test_gyro_pending_yaw, test_gyro_pending_pitch;
static struct
{
	float sticks[4];
	int trigger;
	int reads;
} test_pad_hold;

void test_input_hold_pad(float lx, float ly, float rx, float ry, int trigger, int reads)
{
	test_pad_hold.sticks[0] = lx;
	test_pad_hold.sticks[1] = ly;
	test_pad_hold.sticks[2] = rx;
	test_pad_hold.sticks[3] = ry;
	test_pad_hold.trigger = trigger;
	test_pad_hold.reads = reads;
}

void test_input_gyro_turn(float yaw, float pitch)
{
	test_gyro_pending_yaw += yaw;
	test_gyro_pending_pitch += pitch;
}

static int test_gyro_on(void)
{
	const char *mode = getenv("HALO_TEST_GYRO");

	return mode && *mode && strcmp(mode, "off") != 0 && strcmp(mode, "0") != 0;
}

static int test_gyro_assist(void)
{
	const char *assist = getenv("HALO_GYRO_ASSIST");

	return assist ? atoi(assist) != 0 : 1;
}
#endif

/* radians of yaw and pitch for the mouse motion since the last call; the
game adds these to the facing change of the player on gamepad 0 */
int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch)
{
	/* radians per pixel of relative motion at sensitivity 1 */
	const float scale = 0.0022f;
	static int invert = -1;
	float x, y;

	*yaw = 0.0f;
	*pitch = 0.0f;
#ifdef HALO_VITA
	/* the Vita has no mouse: its gyroscope aims the same way
	(port/vita/platform/vita_pad.c) */
	{
		extern int vita_pad_gyro_look(short gamepad_index, float *yaw, float *pitch);

		(void)scale;
		(void)invert;
		(void)x;
		(void)y;
		return vita_pad_gyro_look(gamepad_index, yaw, pitch);
	}
#endif
	if (gamepad_index != 0)
		return FALSE;
	if (invert < 0)
		invert = config_boolean("input.invert_mouse");
	pthread_mutex_lock(&mouse_lock);
	x = mouse_pending_x;
	y = mouse_pending_y;
	mouse_pending_x = 0.0f;
	mouse_pending_y = 0.0f;
	mouse_polls_unconsumed = 0;
	pthread_mutex_unlock(&mouse_lock);
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
	/* (tests, @aim gyro: the harness's stand-in for the Vita's gyro) */
	if (test_gyro_pending_yaw != 0.0f || test_gyro_pending_pitch != 0.0f)
	{
		*yaw = test_gyro_pending_yaw;
		*pitch = test_gyro_pending_pitch;
		test_gyro_pending_yaw = test_gyro_pending_pitch = 0.0f;
		return TRUE;
	}
#endif
	if (x == 0.0f && y == 0.0f)
		return FALSE;
	*yaw = -x * scale * mouse_sensitivity();
	*pitch = (invert ? y : -y) * scale * mouse_sensitivity();
	return TRUE;
}

/* (source/game/player_control.c) whether the stick's aim assist, its
friction and adhesion, acts on halo_linux_mouse_look's turns too: the
Vita's gyro when its Gyro aim assist setting is on; never a desktop mouse,
which aims unassisted as on Halo PC (the harness's HALO_TEST_GYRO aside) */
int halo_linux_look_assist(short gamepad_index)
{
#ifdef HALO_VITA
	extern int vita_pad_gyro_assist(short gamepad_index);

	return vita_pad_gyro_assist(gamepad_index);
#elif !defined(HALO_RELEASE)
	return gamepad_index == 0 && test_gyro_on() && test_gyro_assist();
#else
	(void)gamepad_index;
	return FALSE;
#endif
}

/* (source/game/player_control.c, source/game/aim_assist.c) whether the
player on that gamepad has no aim assist at all, neither on the stick's
look nor on its shots (the autoaim): the Vita's gyro on (any mode but off)
with its Gyro aim assist setting off. Only this machine's own players ask,
for their own aim: a desktop never (the harness's HALO_TEST_GYRO aside) */
int halo_linux_aim_assist_off(short gamepad_index)
{
#ifdef HALO_VITA
	extern int vita_pad_gyro_unassisted(short gamepad_index);

	return vita_pad_gyro_unassisted(gamepad_index);
#elif !defined(HALO_RELEASE)
	return gamepad_index == 0 && test_gyro_on() && !test_gyro_assist();
#else
	(void)gamepad_index;
	return FALSE;
#endif
}

/* collects the motion the game has not asked for yet; motion that nobody
consumes for a few polls (menus, cutscenes) is dropped so it cannot jerk
the view later */
static void mouse_poll(const struct platform_input_state *input)
{
	pthread_mutex_lock(&mouse_lock);
	if (++mouse_polls_unconsumed > 4)
	{
		mouse_pending_x = 0.0f;
		mouse_pending_y = 0.0f;
	}
	if (!input->mouse_released)
	{
		mouse_pending_x += input->mouse_dx;
		mouse_pending_y += input->mouse_dy;
		mouse_wheel_accumulated += input->mouse_wheel;
		forge_wheel_pending += input->mouse_wheel;
		if (input->mouse_wheel != 0.0f)
			wheel_moved_ms = SDL_GetTicks();
	}
	pthread_mutex_unlock(&mouse_lock);
}

/* ---------- keyboard and mouse as a controller */

static BYTE analog(BOOL down)
{
	return down ? 0xff : 0x00;
}

#ifndef HALO_NOT_DESKTOP
/* source/interface/ui_widget.c: whether a menu is up (the pause menu too) */
int ui_widget_menus_active(void);
#endif

/* whether a key held now was pressed while a menu was up: its part is the
one it had when it went down, kept until it is let go (*latched: -1 up),
so Escape that opens the pause menu is not also its B, nor Escape that
closes it a melee */
static BOOL key_pressed_in_menus(BOOL down, BOOL menus, int *latched)
{
	if (!down)
	{
		*latched = -1;
		return FALSE;
	}
	if (*latched < 0)
		*latched = menus != FALSE;
	return *latched != 0;
}

static void keyboard_gamepad(const struct platform_input_state *input, XINPUT_GAMEPAD *pad)
{
	const unsigned char *k = input->keys;
	BOOL mouse = !input->mouse_released;
	const unsigned char *m = input->mouse_buttons;
	int x = 0, y = 0;
	/* Escape is Start in play (the pause menu) and B in the menus (back, as
	Backspace: the owner, Oct 20 2026); Space is A in play (jump) and Start
	in the menus, where Enter accepts. button_glyphs.c's menu_keys show them */
	static int escape_latched = -1, space_latched = -1;
#ifndef HALO_NOT_DESKTOP
	BOOL menus = ui_widget_menus_active() != 0;
#else
	BOOL menus = FALSE;
#endif
	BOOL escape_in_menus = key_pressed_in_menus(k[SDL_SCANCODE_ESCAPE], menus, &escape_latched);
	BOOL space_in_menus = key_pressed_in_menus(k[SDL_SCANCODE_SPACE], menus, &space_latched);

	if (k[SDL_SCANCODE_D]) x++;
	if (k[SDL_SCANCODE_A]) x--;
	if (k[SDL_SCANCODE_W]) y++;
	if (k[SDL_SCANCODE_S]) y--;
	if (x || y)
	{
		/* full deflection, diagonals on the unit circle */
		float length = (x && y) ? 0.70710678f : 1.0f;

		pad->sThumbLX = (SHORT)(x * 32767 * length);
		pad->sThumbLY = (SHORT)(y * 32767 * length);
	}

	if (k[SDL_SCANCODE_UP]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_UP;
	if (k[SDL_SCANCODE_DOWN]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
	if (k[SDL_SCANCODE_LEFT]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
	if (k[SDL_SCANCODE_RIGHT]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
	if ((k[SDL_SCANCODE_ESCAPE] && !escape_in_menus) || (k[SDL_SCANCODE_SPACE] && space_in_menus))
		pad->wButtons |= XINPUT_GAMEPAD_START;
	if (k[SDL_SCANCODE_F1]) pad->wButtons |= XINPUT_GAMEPAD_BACK;
	if (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_C]) pad->wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
	if (k[SDL_SCANCODE_Z] || (mouse && m[SDL_BUTTON_MIDDLE])) pad->wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;

	pad->bAnalogButtons[XINPUT_GAMEPAD_A] |= analog((k[SDL_SCANCODE_SPACE] && !space_in_menus) || k[SDL_SCANCODE_RETURN] ||
		k[SDL_SCANCODE_KP_ENTER]);
	pad->bAnalogButtons[XINPUT_GAMEPAD_B] |= analog(k[SDL_SCANCODE_F] || k[SDL_SCANCODE_BACKSPACE] ||
		(k[SDL_SCANCODE_ESCAPE] && escape_in_menus) || (mouse && m[SDL_BUTTON_X1]));
#ifdef HALO_ANDROID
	/* the system back key (gesture or button) backs out of menus */
	pad->bAnalogButtons[XINPUT_GAMEPAD_B] |= analog(k[SDL_SCANCODE_AC_BACK]);
#endif
	pad->bAnalogButtons[XINPUT_GAMEPAD_X] |= analog(k[SDL_SCANCODE_E] || k[SDL_SCANCODE_R]);
	pad->bAnalogButtons[XINPUT_GAMEPAD_Y] |= analog(k[SDL_SCANCODE_TAB] || SDL_GetTicks() < wheel_press_until_ms);
	pad->bAnalogButtons[XINPUT_GAMEPAD_WHITE] |= analog(k[SDL_SCANCODE_Q]);
	pad->bAnalogButtons[XINPUT_GAMEPAD_BLACK] |= analog(k[SDL_SCANCODE_X]);
	pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] |= analog(k[SDL_SCANCODE_G] || (mouse && m[SDL_BUTTON_RIGHT]));
	pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] |= analog(mouse && m[SDL_BUTTON_LEFT]);
	/* (Forge's monitor's own: halo_linux_forge_keys) */
	forge_keys_held = (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT] ? 1UL << 0 : 0) |
		(k[SDL_SCANCODE_DELETE] ? 1UL << 1 : 0) | (k[SDL_SCANCODE_TAB] ? 1UL << 2 : 0) |
		(wheel_scrolling || SDL_GetTicks() < wheel_press_until_ms ? 1UL << 3 : 0);
}

/* (port/linux/game/forge.c, Forge's monitor) the keyboard's keys the
controller's layout above has no room for, held at the last poll: Shift
(thrust: bit 0), Delete (delete: bit 1), Tab (the monitor and the Spartan:
bit 2, as well as Y), and whether the wheel is scrolling (bit 3: its Y is
not a delete there); and the wheel's notches since the last call (push and
pull). None on the Vita, whose port 0 is its own controls */
unsigned long halo_linux_forge_keys(float *wheel_notches)
{
	unsigned long keys;

	pthread_mutex_lock(&mouse_lock);
	*wheel_notches = forge_wheel_pending;
	forge_wheel_pending = 0.0f;
	pthread_mutex_unlock(&mouse_lock);
#ifdef HALO_VITA
	*wheel_notches = 0.0f;
	keys = 0;
#else
	keys = console_is_active() ? 0 : forge_keys_held;
#endif
	return keys;
}

/* A scroll of the wheel switches weapons once: it holds Y for WHEEL_PRESS_MS
once the wheel has turned a notch, and the scroll lasts until the wheel has
been still for WHEEL_SCROLL_GAP_MS. One notch often arrives as several events
over a few tens of milliseconds (high-resolution and smooth-scrolling
wheels), and one flick turns several notches; switching for each would bring
the same weapon straight back. Timed in milliseconds, not polls: polls come
once a frame, at the display's refresh rate. */
#define WHEEL_PRESS_MS 50
#define WHEEL_SCROLL_GAP_MS 200

/* debug.test_input "bot:<seed>": a scripted player for the automated
network tests (port/linux/game/network_test.c), different for each seed:
it walks and strafes in circles, turns, fires every few seconds, jumps now
and then and throws a grenade every seven seconds */
static int test_input_holding_action;
static Uint64 test_input_holding_action_since;

/* the automated tests (port/linux/game/network_test.c): the scripted player
stands still, holding the action button (X: picking up, swapping weapons)
after a second */
void test_input_hold_action(int hold)
{
	if (hold && !test_input_holding_action)
		test_input_holding_action_since = SDL_GetTicks();
	test_input_holding_action = hold;
}

/* the automated tests (network_test.c's HALO_NETWORK_TEST_PLAYER_DRIVES):
the scripted player lets go of everything while held (its vehicle stops,
for the offline bots to get in beside it) */
static int test_input_holding_still;

void test_input_hold_still(int hold)
{
	test_input_holding_still = hold;
}

/* the automated tests (network_test.c's HALO_NETWORK_TEST_AIM): the
scripted player holds the trigger and leaves the right stick alone (the
test turns the player to its target), walking as before */
static int test_input_holding_fire;

void test_input_hold_fire(int hold)
{
	test_input_holding_fire = hold;
}

/* the automated tests (network_test.c's debug.network_test_scores): the
player holds Back, the scoreboard, as the scripted player plays on */
static int test_input_holding_back;

void test_input_hold_back(int hold)
{
	test_input_holding_back = hold;
}

/* the automated tests (bots_playlist.c's HALO_TEST_BOT_PLAYLIST_CANCEL): the
player holds B, as one stopping the bots' playlist in its countdown does */
static int test_input_holding_b;

void test_input_hold_b(int hold)
{
	test_input_holding_b = hold;
}

/* (debug) HALO_TEST_PAD="steps": presses for automated menu tests on Linux
(no window to type into: the harness), as HALO_PAD_FILE gives the Vita's
(port/vita/host/vita_input.c): steps "name:hold_ms:pause_ms" separated by
spaces (hold 150 and pause 1500 by default), pressed one after another
from when the main menu has been up for a second (test_input_main_menu,
main.c). Names in the Xbox's terms: a b x y black white lt rt up down left
right start back ls rs, the sticks pushed all the way lup ldown lleft
lright rup rdown rleft rright, several at once joined by "+"; "wait" presses
none, "level" none until a level (not the main menu) has run three
seconds' frames, its hold and pause counted from then (a map's load takes
as long as it takes), "carnage" none until a multiplayer game's Halo 3
carnage report is up (port/linux/game/carnage_report.c: a game's end comes
when it comes), "until/TEXT" none until the screen open is one whose tag's
name has TEXT in it (a message box: "until/dlg\error"), and "@name" none,
the next frame saved as name.bmp
(HALO_SCREENSHOT_DIR, as HALO_TEST_COMMANDS' @shot). halo.log says each
step as it is pressed. */
static struct
{
	int parsed;
	int count;
	int index;
	Uint64 started;
	/* (the "until/" step whose screen was open: going on) */
	int until_index;
	struct
	{
		char name[48];
		int buttons;
		WORD digital;
		/* (the sticks pushed all the way: TEST_PAD_STICK_*) */
		int sticks;
		unsigned int hold_ms;
		unsigned int pause_ms;
	} steps[96];
} test_pad;
enum
{
	TEST_PAD_LEFT_UP = 1 << 0, TEST_PAD_LEFT_DOWN = 1 << 1, TEST_PAD_LEFT_LEFT = 1 << 2, TEST_PAD_LEFT_RIGHT = 1 << 3,
	TEST_PAD_RIGHT_UP = 1 << 4, TEST_PAD_RIGHT_DOWN = 1 << 5, TEST_PAD_RIGHT_LEFT = 1 << 6, TEST_PAD_RIGHT_RIGHT = 1 << 7,
};
static volatile int test_pad_menu_ready;
/* (the main menu is up: the scripted player leaves the menus to the steps) */
static volatile int test_pad_at_menu;
/* (the "level" step) the frames a level has run since the main menu */
static volatile int test_pad_level_frames;

/* main.c, every frame: whether the main menu is up */
void test_input_main_menu(int loaded)
{
	static Uint64 since;

	test_pad_at_menu = loaded;
	if (!loaded)
	{
		since = 0;
		if (test_pad_level_frames < 1000000)
			test_pad_level_frames++;
		return;
	}
	test_pad_level_frames = 0;
	if (!since)
		since = SDL_GetTicks();
	if (SDL_GetTicks() - since >= 1000)
		test_pad_menu_ready = 1;
}

static void test_pad_parse(void)
{
	static const struct
	{
		const char *name;
		int analog;
		WORD digital;
		int sticks;
	} names[] =
	{
		{ "a", XINPUT_GAMEPAD_A, 0 }, { "b", XINPUT_GAMEPAD_B, 0 }, { "x", XINPUT_GAMEPAD_X, 0 },
		{ "y", XINPUT_GAMEPAD_Y, 0 }, { "black", XINPUT_GAMEPAD_BLACK, 0 }, { "white", XINPUT_GAMEPAD_WHITE, 0 },
		{ "lt", XINPUT_GAMEPAD_LEFT_TRIGGER, 0 }, { "rt", XINPUT_GAMEPAD_RIGHT_TRIGGER, 0 },
		{ "up", -1, XINPUT_GAMEPAD_DPAD_UP }, { "down", -1, XINPUT_GAMEPAD_DPAD_DOWN },
		{ "left", -1, XINPUT_GAMEPAD_DPAD_LEFT }, { "right", -1, XINPUT_GAMEPAD_DPAD_RIGHT },
		{ "start", -1, XINPUT_GAMEPAD_START }, { "back", -1, XINPUT_GAMEPAD_BACK },
		{ "ls", -1, XINPUT_GAMEPAD_LEFT_THUMB }, { "rs", -1, XINPUT_GAMEPAD_RIGHT_THUMB },
		{ "wait", -1, 0 }, { "level", -1, 0 }, { "carnage", -1, 0 },
		{ "lup", -1, 0, TEST_PAD_LEFT_UP }, { "ldown", -1, 0, TEST_PAD_LEFT_DOWN },
		{ "lleft", -1, 0, TEST_PAD_LEFT_LEFT }, { "lright", -1, 0, TEST_PAD_LEFT_RIGHT },
		{ "rup", -1, 0, TEST_PAD_RIGHT_UP }, { "rdown", -1, 0, TEST_PAD_RIGHT_DOWN },
		{ "rleft", -1, 0, TEST_PAD_RIGHT_LEFT }, { "rright", -1, 0, TEST_PAD_RIGHT_RIGHT },
	};
#ifndef HALO_RELEASE
	const char *setting = getenv("HALO_TEST_PAD");
#else
	const char *setting = NULL; /* (the tests' presses, never in release builds) */
#endif
	char text[2048];
	char *token, *token_end;

	test_pad.parsed = 1;
	test_pad.until_index = -1;
	if (!setting || !*setting)
		return;
	snprintf(text, sizeof(text), "%s", setting);
	for (token = strtok_r(text, " \t\r\n", &token_end); token && test_pad.count < (int)(sizeof(test_pad.steps) /
		sizeof(test_pad.steps[0])); token = strtok_r(NULL, " \t\r\n", &token_end))
	{
		char *hold = strchr(token, ':');
		char *pause = hold ? strchr(hold + 1, ':') : NULL;
		char *key, *key_end;
		int step = test_pad.count++;

		memset(&test_pad.steps[step], 0, sizeof(test_pad.steps[step]));
		test_pad.steps[step].hold_ms = 150;
		test_pad.steps[step].pause_ms = 1500;
		if (hold)
		{
			*hold = 0;
			test_pad.steps[step].hold_ms = (unsigned int)atoi(hold + 1);
		}
		if (pause)
		{
			*pause = 0;
			test_pad.steps[step].pause_ms = (unsigned int)atoi(pause + 1);
		}
		snprintf(test_pad.steps[step].name, sizeof(test_pad.steps[step].name), "%s", token);
		for (key = strtok_r(token, "+", &key_end); key; key = strtok_r(NULL, "+", &key_end))
		{
			int index;

			for (index = 0; index < (int)(sizeof(names) / sizeof(names[0])); index++)
			{
				if (!strcmp(key, names[index].name))
				{
					if (names[index].analog >= 0)
						test_pad.steps[step].buttons |= 1 << names[index].analog;
					test_pad.steps[step].digital |= names[index].digital;
					test_pad.steps[step].sticks |= names[index].sticks;
				}
			}
		}
	}
}

/* a step begun: said in the log; "@name" has the next frame saved as
name.bmp in HALO_SCREENSHOT_DIR (main.c's @shot) */
static void test_pad_step_begin(void)
{
#ifndef HALO_VITA
	extern char halo_screenshot_name[64];
#endif
	const char *name = test_pad.steps[test_pad.index].name;

	platform_log("test pad: %s", name);
#ifndef HALO_VITA
	if (name[0] == '@' && name[1])
		snprintf(halo_screenshot_name, 64, "%s", name + 1);
#endif
#ifndef HALO_NOT_DESKTOP
	/* ("kb": a key pressed, as far as the button icons on auto go: the
	keyboard's again after the pad's presses) */
	if (!strcmp(name, "kb"))
		platform_input_device_used(_halo_input_device_keyboard);
#endif
}

static void test_pad_gamepad(XINPUT_GAMEPAD *pad)
{
	Uint64 now;

	if (!test_pad.parsed)
		test_pad_parse();
	if (test_pad.index >= test_pad.count || !test_pad_menu_ready)
		return;
	now = SDL_GetTicks();
	if (!test_pad.started)
	{
		test_pad.started = now;
		test_pad_step_begin();
	}
	while (test_pad.index < test_pad.count)
	{
		Uint64 elapsed;
		int analog;

		/* ("until/TEXT": until the screen open is one whose tag's name has
		TEXT in it, a message box's "dlg\question"; its hold and pause
		counted from then) */
		if (!strncmp(test_pad.steps[test_pad.index].name, "until/", 6) && test_pad.until_index != test_pad.index)
		{
			char const *ui_widget_port_active_screen_name(void);

			if (!strstr(ui_widget_port_active_screen_name(), test_pad.steps[test_pad.index].name + 6))
			{
				test_pad.started = now;
				return;
			}
			/* (met: the step goes on, whatever opens meanwhile) */
			test_pad.until_index = test_pad.index;
		}
		/* ("level": until a level has run 90 frames) */
		if (!strcmp(test_pad.steps[test_pad.index].name, "level") ||
			!strncmp(test_pad.steps[test_pad.index].name, "level:", 6))
		{
			if (test_pad_level_frames < 90)
			{
				test_pad.started = now;
				return;
			}
		}
		/* ("carnage": until the carnage report is up) */
		if (!strcmp(test_pad.steps[test_pad.index].name, "carnage"))
		{
			extern unsigned char carnage_report_replaces_postgame(void);

			if (!carnage_report_replaces_postgame())
			{
				test_pad.started = now;
				return;
			}
		}
		elapsed = now - test_pad.started;

		if (elapsed < test_pad.steps[test_pad.index].hold_ms)
		{
			for (analog = 0; analog < 8; analog++)
			{
				if (test_pad.steps[test_pad.index].buttons & (1 << analog))
					pad->bAnalogButtons[analog] = 255;
			}
			pad->wButtons |= test_pad.steps[test_pad.index].digital;
			{
				int sticks = test_pad.steps[test_pad.index].sticks;

				if (sticks & (TEST_PAD_LEFT_UP | TEST_PAD_LEFT_DOWN))
					pad->sThumbLY = (sticks & TEST_PAD_LEFT_UP) ? 32767 : -32767;
				if (sticks & (TEST_PAD_LEFT_LEFT | TEST_PAD_LEFT_RIGHT))
					pad->sThumbLX = (sticks & TEST_PAD_LEFT_RIGHT) ? 32767 : -32767;
				if (sticks & (TEST_PAD_RIGHT_UP | TEST_PAD_RIGHT_DOWN))
					pad->sThumbRY = (sticks & TEST_PAD_RIGHT_UP) ? 32767 : -32767;
				if (sticks & (TEST_PAD_RIGHT_LEFT | TEST_PAD_RIGHT_RIGHT))
					pad->sThumbRX = (sticks & TEST_PAD_RIGHT_RIGHT) ? 32767 : -32767;
			}
			return;
		}
		if (elapsed < test_pad.steps[test_pad.index].hold_ms + test_pad.steps[test_pad.index].pause_ms)
			return;
		test_pad.started += test_pad.steps[test_pad.index].hold_ms + test_pad.steps[test_pad.index].pause_ms;
		test_pad.index++;
		if (test_pad.index < test_pad.count)
			test_pad_step_begin();
		else
			platform_log("test pad: done");
	}
}

static void test_input_gamepad(XINPUT_GAMEPAD *pad)
{
	static int checked;
	static int seed = -1;
	/* "bot:<seed>:look": it also looks up and down and presses X (action)
	now and then: on Easy and Normal the campaign's first level asks for a
	look all around and then for the action button before the player leaves
	the cryo tube (player_action_test_action), and until a teammate is out a
	co-op partner only watches (players_coop_room_to_spawn) */
	static int look;
	double t;

	if (!checked)
	{
		const char *setting = config_string("debug.test_input");

		checked = 1;
		if (!strncmp(setting, "bot:", 4))
			seed = atoi(setting + 4);
		else if (!strcmp(setting, "bot"))
			seed = 0;
		look = strstr(setting, ":look") != NULL;
	}
	if (test_input_holding_back)
		pad->wButtons |= XINPUT_GAMEPAD_BACK;
	if (test_input_holding_b)
		pad->bAnalogButtons[XINPUT_GAMEPAD_B] = 255;
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
	/* (@aim pad) */
	if (test_pad_hold.reads > 0)
	{
		test_pad_hold.reads--;
		pad->sThumbLX = (SHORT)(test_pad_hold.sticks[0] * 32767.0f);
		pad->sThumbLY = (SHORT)(test_pad_hold.sticks[1] * 32767.0f);
		pad->sThumbRX = (SHORT)(test_pad_hold.sticks[2] * 32767.0f);
		pad->sThumbRY = (SHORT)(test_pad_hold.sticks[3] * 32767.0f);
		if (test_pad_hold.trigger)
			pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255;
	}
#endif
	if (seed < 0)
		return;
	/* (HALO_TEST_PAD presses the menus: the scripted player plays only in
	a level) */
	if (test_pad_at_menu && test_pad.count > 0)
		return;
	/* (nor while the Halo 3 scoreboard is open: the steps press it, the
	scripted player's stick and jumps would move its cursor and choose) */
	{
		extern unsigned char menu_functions_scoreboard_is_open(void);

		if (test_pad.count > 0 && menu_functions_scoreboard_is_open())
			return;
	}
	if (test_input_holding_still)
		return;
	if (test_input_holding_action)
	{
		/* (standing still, the button held from a second on) */
		if (SDL_GetTicks() - test_input_holding_action_since >= 1000)
			pad->bAnalogButtons[XINPUT_GAMEPAD_X] = 255;
		return;
	}
	t = (double)SDL_GetTicks() / 1000.0 + seed * 1.7;
#ifndef HALO_VITA
	{
		/* (debug) HALO_FIXED_TICK: on the game's clock, so the scripted
		player does the same in every run (tick_hash.c) */
		const char *fixed = getenv("HALO_FIXED_TICK");
		extern volatile unsigned long halo_ticks_simulated;

		if (fixed && atoi(fixed))
			t = (double)halo_ticks_simulated / 30.0 + seed * 1.7;
	}
#endif
	pad->sThumbLY = (SHORT)(sin(t * 0.9) * 32000.0);
	pad->sThumbLX = (SHORT)(cos(t * 0.6 + seed) * 20000.0);
	pad->sThumbRX = (SHORT)(sin(t * 0.4) * 14000.0);
	if (look)
	{
		pad->sThumbRY = (SHORT)(sin(t * 0.7 + seed) * 20000.0);
		if (fmod(t, 4.0) >= 2.0 && fmod(t, 4.0) < 2.2)
			pad->bAnalogButtons[XINPUT_GAMEPAD_X] = 255;
	}
	if (fmod(t, 3.0) < 0.3)
		pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255;
	if (test_input_holding_fire)
	{
		pad->sThumbRX = 0;
		pad->sThumbRY = 0;
		pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255;
	}
	if (fmod(t, 5.0) < 0.1)
		pad->bAnalogButtons[XINPUT_GAMEPAD_A] = 255;
	if (fmod(t, 7.0) < 0.2)
		pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] = 255;
}

static void wheel_update(void)
{
	Uint64 now = SDL_GetTicks();

	pthread_mutex_lock(&mouse_lock);
	if (!wheel_scrolling)
	{
		if (fabsf(mouse_wheel_accumulated) >= 1.0f)
		{
			wheel_scrolling = TRUE;
			wheel_press_until_ms = now + WHEEL_PRESS_MS;
		}
	}
	else if (now >= wheel_press_until_ms && now - wheel_moved_ms >= WHEEL_SCROLL_GAP_MS)
	{
		wheel_scrolling = FALSE;
		mouse_wheel_accumulated = 0.0f;
	}
	pthread_mutex_unlock(&mouse_lock);
}

/* ---------- SDL gamepads */

/* the SDL gamepads in connection order, at most one per port */
static int sdl_gamepads(SDL_Gamepad *gamepads[PORT_COUNT])
{
	SDL_JoystickID *ids;
	int count = 0, index, found = 0;

	memset(gamepads, 0, sizeof(SDL_Gamepad *) * PORT_COUNT);
	ids = SDL_GetGamepads(&count);
	if (!ids)
		return 0;
#ifdef HALO_ANDROID
	{
		/* Android can list input devices with a few gamepad buttons (the
		emulator's keyboard, some phones' key devices) as generic gamepads:
		recognised controllers take the first ports */
		int pass;

		for (pass = 0; pass < 2; pass++)
		{
			for (index = 0; index < count && found < PORT_COUNT; index++)
			{
				SDL_Gamepad *gamepad = SDL_GetGamepadFromID(ids[index]);
				SDL_GamepadType type;
				BOOL recognised;

				if (!gamepad)
					continue;
				type = SDL_GetGamepadType(gamepad);
				recognised = type != SDL_GAMEPAD_TYPE_UNKNOWN && type != SDL_GAMEPAD_TYPE_STANDARD;
				if (recognised == (pass == 0))
					gamepads[found++] = gamepad;
			}
		}
	}
#else
	for (index = 0; index < count && found < PORT_COUNT; index++)
	{
		SDL_Gamepad *gamepad = SDL_GetGamepadFromID(ids[index]);

		if (gamepad)
			gamepads[found++] = gamepad;
	}
#endif
	SDL_free(ids);
	return found;
}

static SHORT stick(Sint16 value, BOOL flip)
{
	int result = flip ? -(int)value - 1 : value;

	if (result < -32768) result = -32768;
	if (result > 32767) result = 32767;
	return (SHORT)result;
}

static void merge_button(XINPUT_GAMEPAD *pad, int analog_index, BOOL down)
{
	if (down)
		pad->bAnalogButtons[analog_index] = 0xff;
}

static void sdl_gamepad_state(SDL_Gamepad *gamepad, XINPUT_GAMEPAD *pad)
{
	static const struct
	{
		SDL_GamepadButton button;
		WORD mask;
	} digital[] =
	{
		{ SDL_GAMEPAD_BUTTON_DPAD_UP, XINPUT_GAMEPAD_DPAD_UP },
		{ SDL_GAMEPAD_BUTTON_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN },
		{ SDL_GAMEPAD_BUTTON_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT },
		{ SDL_GAMEPAD_BUTTON_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT },
		{ SDL_GAMEPAD_BUTTON_START, XINPUT_GAMEPAD_START },
		{ SDL_GAMEPAD_BUTTON_BACK, XINPUT_GAMEPAD_BACK },
		{ SDL_GAMEPAD_BUTTON_LEFT_STICK, XINPUT_GAMEPAD_LEFT_THUMB },
		{ SDL_GAMEPAD_BUTTON_RIGHT_STICK, XINPUT_GAMEPAD_RIGHT_THUMB },
	};
	int index;
	int left_trigger, right_trigger;
	SHORT value;

	for (index = 0; index < (int)(sizeof(digital) / sizeof(digital[0])); index++)
	{
		if (SDL_GetGamepadButton(gamepad, digital[index].button))
			pad->wButtons |= digital[index].mask;
	}
	merge_button(pad, XINPUT_GAMEPAD_A, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH));
	merge_button(pad, XINPUT_GAMEPAD_B, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST));
	merge_button(pad, XINPUT_GAMEPAD_X, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST));
	merge_button(pad, XINPUT_GAMEPAD_Y, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH));
	/* the Duke's white and black buttons sit where later pads have shoulders */
	merge_button(pad, XINPUT_GAMEPAD_WHITE, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
	merge_button(pad, XINPUT_GAMEPAD_BLACK, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));

	left_trigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) * 255 / 32767;
	right_trigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) * 255 / 32767;
	if (left_trigger > pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER])
		pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] = (BYTE)left_trigger;
	if (right_trigger > pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER])
		pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = (BYTE)right_trigger;

	/* a stick only overrides the keyboard when it is pushed further */
	value = stick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX), FALSE);
	if (abs(value) > abs(pad->sThumbLX)) pad->sThumbLX = value;
	value = stick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY), TRUE);
	if (abs(value) > abs(pad->sThumbLY)) pad->sThumbLY = value;
	value = stick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX), FALSE);
	if (abs(value) > abs(pad->sThumbRX)) pad->sThumbRX = value;
	value = stick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY), TRUE);
	if (abs(value) > abs(pad->sThumbRY)) pad->sThumbRY = value;
}

/* ---------- Forge's monitor and Spartan combo */

/* (port/linux/game/forge.c) set while a Forge game is played, and set here
while its combo is held */
extern int forge_game_playing;
extern int forge_combo_held;

/* how long Back alone is kept from the game in a Forge game, in case the
combo's other button follows (the scoreboard opens that much later) */
#define FORGE_COMBO_BACK_GRACE_MS 200

/* In a Forge game the monitor and the Spartan are flipped by Back and the
right stick's click pressed together (the Vita's Select + D-pad up in the
default layout, mirroring Select + D-pad down for the settings panel; the
keyboard's F1 + Z). The game never sees the combo: while it is held, and
until both buttons are let go, neither reaches it, so it neither zooms nor
shows the scoreboard; forge.c reads forge_combo_held instead. Back alone
reaches the game once held FORGE_COMBO_BACK_GRACE_MS without the click (the
scoreboard, held, as in any game); the click alone is the Spartan's zoom. */
static void forge_combo_filter(XINPUT_GAMEPAD *pad)
{
	static Uint64 back_since;
	static int held;
	BOOL back = (pad->wButtons & XINPUT_GAMEPAD_BACK) != 0;
	BOOL click = (pad->wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;

	if (!forge_game_playing)
	{
		back_since = 0;
		held = 0;
		forge_combo_held = 0;
		return;
	}
	if (held || (back && click))
	{
		if (!held)
			platform_log("forge: the monitor and Spartan combo (Back + the right stick's click), kept from the game");
		held = back || click;
		forge_combo_held = held && back && click;
		pad->wButtons &= (WORD)~(XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_RIGHT_THUMB);
		back_since = 0;
		return;
	}
	forge_combo_held = 0;
	if (!back)
	{
		back_since = 0;
		return;
	}
	if (!back_since)
		back_since = SDL_GetTicks();
	if (SDL_GetTicks() - back_since < FORGE_COMBO_BACK_GRACE_MS)
		pad->wButtons &= (WORD)~XINPUT_GAMEPAD_BACK;
}

/* ---------- the in-game chat's controls (port/linux/game/game_chat.c) */

#ifndef HALO_VITA
/* (port 0's read) Back + D-pad left (F1 + Left on the keyboard) opens the
chat's quick chat wheel in play; while it is open the D-pad, A, B, Y and
the shoulders and triggers are the chat's, the game getting none of the
buttons (the sticks stay its own). The Vita's own buttons go to the chat
in vita_pad.c */
static void chat_pad_filter(XINPUT_GAMEPAD *pad)
{
	unsigned int buttons = 0;

	if (pc_text_entry_active())
		return;
	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP) buttons |= GAME_CHAT_PAD_UP;
	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN) buttons |= GAME_CHAT_PAD_DOWN;
	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) buttons |= GAME_CHAT_PAD_LEFT;
	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) buttons |= GAME_CHAT_PAD_RIGHT;
	if ((pad->wButtons & XINPUT_GAMEPAD_BACK) && (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT))
		buttons |= GAME_CHAT_PAD_COMBO;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_A] >= 128) buttons |= GAME_CHAT_PAD_ACCEPT;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_B] >= 128) buttons |= GAME_CHAT_PAD_BACK;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_Y] >= 128) buttons |= GAME_CHAT_PAD_TYPE;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_WHITE] >= 128 || pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] >= 128)
		buttons |= GAME_CHAT_PAD_PREVIOUS;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_BLACK] >= 128 || pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] >= 128)
		buttons |= GAME_CHAT_PAD_NEXT;
	if (game_chat_pad(buttons, (unsigned long)SDL_GetTicks()))
	{
		pad->wButtons = 0;
		memset(pad->bAnalogButtons, 0, sizeof(pad->bAnalogButtons));
	}
}
#endif

/* merges one controller's state into another's: buttons held on either,
each analog button and stick as far as either pushes it */
static void merge_gamepad(XINPUT_GAMEPAD *into, const XINPUT_GAMEPAD *from)
{
	int index;

	into->wButtons |= from->wButtons;
	for (index = 0; index < 8; index++)
	{
		if (from->bAnalogButtons[index] > into->bAnalogButtons[index])
			into->bAnalogButtons[index] = from->bAnalogButtons[index];
	}
	if (abs(from->sThumbLX) > abs(into->sThumbLX)) into->sThumbLX = from->sThumbLX;
	if (abs(from->sThumbLY) > abs(into->sThumbLY)) into->sThumbLY = from->sThumbLY;
	if (abs(from->sThumbRX) > abs(into->sThumbRX)) into->sThumbRX = from->sThumbRX;
	if (abs(from->sThumbRY) > abs(into->sThumbRY)) into->sThumbRY = from->sThumbRY;
}

/* ---------- the text box (pc_text_entry.h) */

#ifndef HALO_VITA
/* a controller's buttons as the text box's (PC_TEXT_PAD_*), an analog
button pressed past half way */
static unsigned int text_entry_buttons(const XINPUT_GAMEPAD *pad)
{
	static const struct
	{
		WORD digital;
		unsigned int button;
	} digital[] =
	{
		{ XINPUT_GAMEPAD_DPAD_UP, PC_TEXT_PAD_UP }, { XINPUT_GAMEPAD_DPAD_DOWN, PC_TEXT_PAD_DOWN },
		{ XINPUT_GAMEPAD_DPAD_LEFT, PC_TEXT_PAD_LEFT }, { XINPUT_GAMEPAD_DPAD_RIGHT, PC_TEXT_PAD_RIGHT },
		{ XINPUT_GAMEPAD_START, PC_TEXT_PAD_START }, { XINPUT_GAMEPAD_BACK, PC_TEXT_PAD_BACK },
		{ XINPUT_GAMEPAD_LEFT_THUMB, PC_TEXT_PAD_STICK_CLICKS }, { XINPUT_GAMEPAD_RIGHT_THUMB, PC_TEXT_PAD_STICK_CLICKS },
	};
	static const unsigned int analog[8] =
	{
		PC_TEXT_PAD_A, PC_TEXT_PAD_B, PC_TEXT_PAD_X, PC_TEXT_PAD_Y, PC_TEXT_PAD_BLACK, PC_TEXT_PAD_WHITE,
		PC_TEXT_PAD_LEFT_TRIGGER, PC_TEXT_PAD_RIGHT_TRIGGER,
	};
	unsigned int buttons = 0;
	int index;

	for (index = 0; index < (int)(sizeof(digital) / sizeof(digital[0])); index++)
	{
		if (pad->wButtons & digital[index].digital)
			buttons |= digital[index].button;
	}
	for (index = 0; index < 8; index++)
	{
		if (pad->bAnalogButtons[index] >= 128)
			buttons |= analog[index];
	}
	return buttons;
}

/* (port 0's read) the text box asked for opened; while it is open the
gamepads (pads: theirs alone, not the keyboard's) drive its on-screen
keyboard and the game gets an empty controller, and after it closes, until
everything that closed it is let go */
static void text_entry_controller(XINPUT_GAMEPAD *game, const XINPUT_GAMEPAD *pads)
{
	Uint64 now = SDL_GetTicks();
	int gamepad_last = 0;

#ifndef HALO_NOT_DESKTOP
	gamepad_last = platform_input_device_last() != _halo_input_device_keyboard;
#endif
	pc_text_entry_frame((unsigned long)now, gamepad_last);
	pc_text_entry_pad(text_entry_buttons(pads), pads->sThumbLX, pads->sThumbLY, (unsigned long)now);
	if (pc_text_entry_holds_pad(text_entry_buttons(game)))
		memset(game, 0, sizeof(*game));
}
#endif

/* ---------- XAPI */

VOID WINAPI XInitDevices(DWORD preallocation_type_count, PXDEVICE_PREALLOC_TYPE preallocation_types)
{
	(void)preallocation_type_count;
	(void)preallocation_types;
	platform_sdl_initialize();
}

static DWORD connected_gamepads(void)
{
	SDL_Gamepad *gamepads[PORT_COUNT];
	int count = sdl_gamepads(gamepads);
	DWORD mask = XDEVICE_PORT0_MASK;
	int port;

	/* the first pad shares port 0 with the keyboard */
	for (port = 1; port < count; port++)
		mask |= 1UL << port;
	return mask;
}

BOOL WINAPI XGetDeviceChanges(PXPP_DEVICE_TYPE device_type, PDWORD insertions, PDWORD removals)
{
	*insertions = 0;
	*removals = 0;
	if (device_type == XDEVICE_TYPE_GAMEPAD)
	{
		DWORD connected = connected_gamepads();

		*insertions = connected & ~reported_gamepads;
		*removals = reported_gamepads & ~connected;
		reported_gamepads = connected;
	}
	else if (device_type == XDEVICE_TYPE_DEBUG_KEYBOARD)
	{
		if (!reported_keyboard)
		{
			*insertions = 1;
			reported_keyboard = TRUE;
		}
	}
	return *insertions || *removals;
}

HANDLE WINAPI XInputOpen(PXPP_DEVICE_TYPE device_type, DWORD port, DWORD slot,
	PXINPUT_POLLING_PARAMETERS polling_parameters)
{
	(void)slot;
	(void)polling_parameters;
	if (device_type == XDEVICE_TYPE_GAMEPAD && port < PORT_COUNT)
	{
		memset(&controllers[port], 0, sizeof(controllers[port]));
		controllers[port].open = TRUE;
		return (HANDLE)&controllers[port];
	}
	if (device_type == XDEVICE_TYPE_DEBUG_KEYBOARD && port == 0)
	{
		keyboard_device.open = TRUE;
		return (HANDLE)&keyboard_device;
	}
	SetLastError(ERROR_DEVICE_NOT_CONNECTED);
	return NULL;
}

VOID WINAPI XInputClose(HANDLE device)
{
	struct controller *controller = (struct controller *)device;

	if (controller)
		controller->open = FALSE;
}

static int controller_port(HANDLE device)
{
	int port;

	for (port = 0; port < PORT_COUNT; port++)
	{
		if (device == (HANDLE)&controllers[port] && controllers[port].open)
			return port;
	}
	return -1;
}

DWORD WINAPI XInputGetState(HANDLE device, PXINPUT_STATE state)
{
	int port = controller_port(device);
	SDL_Gamepad *gamepads[PORT_COUNT];
	int count;

	memset(state, 0, sizeof(*state));
	if (port < 0)
		return ERROR_DEVICE_NOT_CONNECTED;
	platform_pump_events();
	count = sdl_gamepads(gamepads);
#ifdef HALO_VITA
	/* the Vita's own controls, in Xita's layout (port/vita/platform/vita_pad.c) */
	if (port == 0)
	{
		extern void vita_pad_state(XINPUT_GAMEPAD *gamepad);

		vita_pad_state(&state->Gamepad);
	}
	else
#endif
	if (port == 0)
	{
		struct platform_input_state input;

		platform_input_read(&input, TRUE);
		mouse_poll(&input);
		wheel_update();
		/* (the gamepads' own, apart: the text box's on-screen keyboard
		takes them alone) */
		XINPUT_GAMEPAD pads;

		memset(&pads, 0, sizeof(pads));
		if (!console_is_active())
			keyboard_gamepad(&input, &state->Gamepad);
		if (count > 0)
			sdl_gamepad_state(gamepads[0], &pads);
#ifndef HALO_NOT_DESKTOP
		{
			/* the tests' scripted pad pressing is a gamepad used (the
			button icons on auto, halo_button_icons.h): an Xbox pad's, or
			HALO_TEST_GAMEPAD_TYPE=playstation's */
			XINPUT_GAMEPAD before = pads;

			test_input_gamepad(&pads);
			test_pad_gamepad(&pads);
			if (memcmp(&before, &pads, sizeof(before)))
			{
				const char *type = getenv("HALO_TEST_GAMEPAD_TYPE");

				platform_input_device_used(type && !strcmp(type, "playstation") ?
					_halo_input_device_playstation_gamepad : _halo_input_device_xbox_gamepad);
			}
		}
#else
		test_input_gamepad(&pads);
		test_pad_gamepad(&pads);
#endif
		merge_gamepad(&state->Gamepad, &pads);
#ifndef HALO_VITA
		{
			/* (the other gamepads type in the text box too) */
			int other;

			if (pc_text_entry_active())
				for (other = 1; other < count; other++)
					sdl_gamepad_state(gamepads[other], &pads);
			text_entry_controller(&state->Gamepad, &pads);
		}
#endif
	}
	else if (port < count)
	{
		sdl_gamepad_state(gamepads[port], &state->Gamepad);
#ifndef HALO_VITA
		/* (the text box open: nothing for the game from any controller) */
		if (pc_text_entry_active())
			memset(&state->Gamepad, 0, sizeof(state->Gamepad));
#endif
	}
	if (port == 0)
		forge_combo_filter(&state->Gamepad);
#ifndef HALO_VITA
	if (port == 0)
		chat_pad_filter(&state->Gamepad);
#endif

	if (memcmp(&state->Gamepad, &controllers[port].previous, sizeof(state->Gamepad)))
	{
		controllers[port].packet_number++;
		controllers[port].previous = state->Gamepad;
	}
	state->dwPacketNumber = controllers[port].packet_number;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputSetState(HANDLE device, PXINPUT_FEEDBACK feedback)
{
	int port = controller_port(device);
	SDL_Gamepad *gamepads[PORT_COUNT];
	int count;

	if (!feedback)
		return ERROR_INVALID_PARAMETER;
	feedback->Header.dwStatus = ERROR_SUCCESS;
	if (port < 0)
		return ERROR_DEVICE_NOT_CONNECTED;
	count = sdl_gamepads(gamepads);
	if (port < count)
	{
		/* the game refreshes the motors every frame; rumble a little longer
		than that so they do not stutter */
		SDL_RumbleGamepad(gamepads[port], feedback->Rumble.wLeftMotorSpeed,
			feedback->Rumble.wRightMotorSpeed, 100);
	}
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugInitKeyboardQueue(PXINPUT_DEBUG_KEYQUEUE_PARAMETERS parameters)
{
	(void)parameters;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugGetKeystroke(PXINPUT_DEBUG_KEYSTROKE keystroke)
{
	struct platform_keystroke next;

	memset(keystroke, 0, sizeof(*keystroke));
	while (platform_next_keystroke(&next))
	{
		BOOL key_up = (next.flags & XINPUT_DEBUG_KEYSTROKE_FLAG_KEYUP) != 0;

		/* key ups always pass, so no key is left latched down */
		if (key_up || next.virtual_key == VK_OEM_3_BACKQUOTE || console_is_active())
		{
			keystroke->VirtualKey = next.virtual_key;
			keystroke->Ascii = next.ascii;
			keystroke->Flags = next.flags;
			return ERROR_SUCCESS;
		}
	}
	return ERROR_HANDLE_EOF;
}
