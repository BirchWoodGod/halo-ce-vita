/*
XINPUT_SDL.C

Xbox controllers and the debug keyboard for the Linux build.

Port 0 is always connected: it is the keyboard and mouse, merged with the
first SDL gamepad when one is present. Further SDL gamepads take ports 1-3.
On the Vita, port 0 is the Vita's own controls and ports 1-3 a PS TV's other
controllers (port/vita/platform/vita_pad.c). (debug) debug.test_controllers
connects ports 1-3 with no device behind them, for automated split screen
tests (the scripted player and HALO_TEST_PAD's steps play them).

Keyboard and mouse (port 0):
	W A S D          left stick          arrows           D-pad
	mouse            aim (see halo_linux_mouse_look)
	left button      right trigger       right button, G  left trigger
	space, enter     A                   F, backspace, X1 B
	E, R             X                   tab, wheel       Y
	Q                white               X                black
	left ctrl, C     left stick click    Z, middle button right stick click
	escape           start               F1               back
	F12              release or recapture the mouse
	V                voice chat's push to talk (as Back + left trigger)

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
#include "voice_link.h"

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
	if (x == 0.0f && y == 0.0f)
		return FALSE;
	*yaw = -x * scale * mouse_sensitivity();
	*pitch = (invert ? y : -y) * scale * mouse_sensitivity();
	return TRUE;
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

static void keyboard_gamepad(const struct platform_input_state *input, XINPUT_GAMEPAD *pad)
{
	const unsigned char *k = input->keys;
	BOOL mouse = !input->mouse_released;
	const unsigned char *m = input->mouse_buttons;
	int x = 0, y = 0;

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
	if (k[SDL_SCANCODE_ESCAPE]) pad->wButtons |= XINPUT_GAMEPAD_START;
	if (k[SDL_SCANCODE_F1]) pad->wButtons |= XINPUT_GAMEPAD_BACK;
	if (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_C]) pad->wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
	if (k[SDL_SCANCODE_Z] || (mouse && m[SDL_BUTTON_MIDDLE])) pad->wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;

	pad->bAnalogButtons[XINPUT_GAMEPAD_A] |= analog(k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RETURN] ||
		k[SDL_SCANCODE_KP_ENTER]);
	pad->bAnalogButtons[XINPUT_GAMEPAD_B] |= analog(k[SDL_SCANCODE_F] || k[SDL_SCANCODE_BACKSPACE] ||
		(mouse && m[SDL_BUTTON_X1]));
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
}

/* Voice chat's push to talk (voice_link.h): V held (the console closed), or
Back with the left trigger, which in a game with voice are voice's alone
(no scoreboard, no grenade) while both are held */
static void voice_push_to_talk(const struct platform_input_state *input, XINPUT_GAMEPAD *pad)
{
	int available = __atomic_load_n(&halo_voice_status[HALO_VOICE_STATUS_AVAILABLE], __ATOMIC_ACQUIRE);
	int combination = (pad->wButtons & XINPUT_GAMEPAD_BACK) && pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] >= 128;
	int key = !console_is_active() && input->keys[SDL_SCANCODE_V];

	if (available && combination)
	{
		pad->wButtons &= ~XINPUT_GAMEPAD_BACK;
		pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] = 0;
	}
	__atomic_store_n(&halo_voice_talk_held, available && (combination || key), __ATOMIC_RELEASE);
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

/* (debug) debug.test_controllers: the controllers the automated tests have,
1 to 4 (ports 1 on with no device behind them) */
static int test_controller_count(void)
{
	static int count;

	if (!count)
	{
		long setting = config_integer("debug.test_controllers");

		count = setting < 1 ? 1 : setting > PORT_COUNT ? PORT_COUNT : (int)setting;
	}
	return count;
}

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

/* (debug) HALO_TEST_PAD="steps": presses for automated menu tests on Linux
(no window to type into: the harness), as HALO_PAD_FILE gives the Vita's
(port/vita/host/vita_input.c): steps "name:hold_ms:pause_ms" separated by
spaces (hold 150 and pause 1500 by default), pressed one after another
from when the main menu has been up for a second (test_input_main_menu,
main.c). Names in the Xbox's terms: a b x y black white lt rt up down left
right start back ls rs, several at once joined by "+"; "wait" presses
none; "2." to "4." before a step press it on controller 2 to 4 (the test
controllers, debug.test_controllers) instead of controller 1; "unplug" and
"plug" on one of them disconnect it and connect it again as the step
begins (a PS TV's DualShock switched off mid-game, and back). halo.log says
each step as it is pressed. */
static struct
{
	int parsed;
	int count;
	int index;
	Uint64 started;
	struct
	{
		char name[24];
		/* the port pressing it */
		int port;
		int buttons;
		WORD digital;
		unsigned int hold_ms;
		unsigned int pause_ms;
	} steps[96];
} test_pad;
static volatile int test_pad_menu_ready;
/* the test controllers a step unplugged (a bit per port) */
static volatile DWORD test_pad_unplugged;
/* (the main menu is up: the scripted player leaves the menus to the steps) */
static volatile int test_pad_at_menu;

/* main.c, every frame: whether the main menu is up */
void test_input_main_menu(int loaded)
{
	static Uint64 since;

	test_pad_at_menu = loaded;
	if (!loaded)
	{
		since = 0;
		return;
	}
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
	} names[] =
	{
		{ "a", XINPUT_GAMEPAD_A, 0 }, { "b", XINPUT_GAMEPAD_B, 0 }, { "x", XINPUT_GAMEPAD_X, 0 },
		{ "y", XINPUT_GAMEPAD_Y, 0 }, { "black", XINPUT_GAMEPAD_BLACK, 0 }, { "white", XINPUT_GAMEPAD_WHITE, 0 },
		{ "lt", XINPUT_GAMEPAD_LEFT_TRIGGER, 0 }, { "rt", XINPUT_GAMEPAD_RIGHT_TRIGGER, 0 },
		{ "up", -1, XINPUT_GAMEPAD_DPAD_UP }, { "down", -1, XINPUT_GAMEPAD_DPAD_DOWN },
		{ "left", -1, XINPUT_GAMEPAD_DPAD_LEFT }, { "right", -1, XINPUT_GAMEPAD_DPAD_RIGHT },
		{ "start", -1, XINPUT_GAMEPAD_START }, { "back", -1, XINPUT_GAMEPAD_BACK },
		{ "ls", -1, XINPUT_GAMEPAD_LEFT_THUMB }, { "rs", -1, XINPUT_GAMEPAD_RIGHT_THUMB },
		{ "wait", -1, 0 },
	};
	const char *setting = getenv("HALO_TEST_PAD");
	char text[2048];
	char *token, *token_end;

	test_pad.parsed = 1;
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
		if (token[0] >= '2' && token[0] <= '0' + PORT_COUNT && token[1] == '.')
		{
			test_pad.steps[step].port = token[0] - '1';
			token += 2;
		}
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
				}
			}
		}
	}
}

/* a step as it begins: logged, and a test controller's unplug or plug */
static void test_pad_step_begin(void)
{
	const char *name = test_pad.steps[test_pad.index].name;
	int port = test_pad.steps[test_pad.index].port;
	const char *action = port ? name + 2 : name;

	platform_log("test pad: %s", name);
	if (port && !strncmp(action, "unplug", 6))
		test_pad_unplugged |= 1UL << port;
	else if (port && !strncmp(action, "plug", 4))
		test_pad_unplugged &= ~(1UL << port);
}

static void test_pad_gamepad(XINPUT_GAMEPAD *pad, int port)
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
		Uint64 elapsed = now - test_pad.started;
		int analog;

		if (elapsed < test_pad.steps[test_pad.index].hold_ms)
		{
			if (test_pad.steps[test_pad.index].port != port)
				return;
			for (analog = 0; analog < 8; analog++)
			{
				if (test_pad.steps[test_pad.index].buttons & (1 << analog))
					pad->bAnalogButtons[analog] = 255;
			}
			pad->wButtons |= test_pad.steps[test_pad.index].digital;
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

static void test_input_gamepad(XINPUT_GAMEPAD *pad, int port)
{
	static int checked;
	static int seed = -1;
	/* "bot:<seed>:look": it also looks up and down and presses X (action)
	now and then: on Easy and Normal the campaign's first level asks for a
	look all around and then for the action button before the player leaves
	the cryo tube (player_action_test_action), and until a teammate is out a
	co-op partner only watches (players_coop_room_to_spawn) */
	static int look;
	int player_seed;
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
	if (seed < 0)
		return;
	/* (HALO_TEST_PAD presses the menus: the scripted player plays only in
	a level) */
	if (test_pad_at_menu && test_pad.count > 0)
		return;
	if (test_input_holding_action && port == 0)
	{
		/* (standing still, the button held from a second on) */
		if (SDL_GetTicks() - test_input_holding_action_since >= 1000)
			pad->bAnalogButtons[XINPUT_GAMEPAD_X] = 255;
		return;
	}
	/* (the test controllers' players: seeds of their own) */
	player_seed = seed + port * 3;
	t = (double)SDL_GetTicks() / 1000.0 + player_seed * 1.7;
#ifndef HALO_VITA
	{
		/* (debug) HALO_FIXED_TICK: on the game's clock, so the scripted
		player does the same in every run (tick_hash.c) */
		const char *fixed = getenv("HALO_FIXED_TICK");
		extern volatile unsigned long halo_ticks_simulated;

		if (fixed && atoi(fixed))
			t = (double)halo_ticks_simulated / 30.0 + player_seed * 1.7;
	}
#endif
	pad->sThumbLY = (SHORT)(sin(t * 0.9) * 32000.0);
	pad->sThumbLX = (SHORT)(cos(t * 0.6 + player_seed) * 20000.0);
	pad->sThumbRX = (SHORT)(sin(t * 0.4) * 14000.0);
	if (look)
	{
		pad->sThumbRY = (SHORT)(sin(t * 0.7 + player_seed) * 20000.0);
		if (fmod(t, 4.0) >= 2.0 && fmod(t, 4.0) < 2.2)
			pad->bAnalogButtons[XINPUT_GAMEPAD_X] = 255;
	}
	if (fmod(t, 3.0) < 0.3)
		pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255;
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

#ifdef HALO_VITA
	/* (the Vita's own controls are port 0: its SDL joystick is not another
	controller) a PS TV's other controllers (port/vita/platform/vita_pad.c) */
	{
		extern unsigned long vita_pad_extra_connected(void);

		(void)count;
		mask |= vita_pad_extra_connected() & 0x0EUL;
	}
#else
	/* the first pad shares port 0 with the keyboard */
	for (port = 1; port < count; port++)
		mask |= 1UL << port;
#endif
	/* (debug) the automated tests' controllers, but those a step unplugged */
	for (port = 1; port < test_controller_count(); port++)
	{
		if (!(test_pad_unplugged & (1UL << port)))
			mask |= 1UL << port;
	}
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
		if (!console_is_active())
			keyboard_gamepad(&input, &state->Gamepad);
		if (count > 0)
			sdl_gamepad_state(gamepads[0], &state->Gamepad);
		test_input_gamepad(&state->Gamepad, 0);
		test_pad_gamepad(&state->Gamepad, 0);
		voice_push_to_talk(&input, &state->Gamepad);
	}
	else if (port < test_controller_count())
	{
		/* (debug) a test controller: the scripted player's and the steps'
		(none while unplugged; port 0's poll moves the steps on) */
		if (test_pad_unplugged & (1UL << port))
			return ERROR_DEVICE_NOT_CONNECTED;
		test_input_gamepad(&state->Gamepad, port);
		test_pad_gamepad(&state->Gamepad, port);
	}
#ifdef HALO_VITA
	else
	{
		/* a PS TV's other controllers (port/vita/platform/vita_pad.c) */
		extern void vita_pad_extra_state(int controller, XINPUT_GAMEPAD *gamepad);

		vita_pad_extra_state(port, &state->Gamepad);
	}
#else
	else if (port < count)
	{
		sdl_gamepad_state(gamepads[port], &state->Gamepad);
	}
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
#ifdef HALO_VITA
	/* a PS TV's DualShocks (port/vita/platform/vita_pad.c) */
	{
		extern void vita_pad_rumble(int controller, unsigned short left, unsigned short right);

		(void)gamepads;
		(void)count;
		vita_pad_rumble(port, feedback->Rumble.wLeftMotorSpeed, feedback->Rumble.wRightMotorSpeed);
		return ERROR_SUCCESS;
	}
#endif
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
