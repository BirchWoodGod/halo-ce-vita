/*
HALO_KEY_BINDINGS.H

The PC builds' keyboard and mouse controls (Linux and Windows; never the
Vita's, whose port 0 is its own buttons): each of the player's actions
bound to keys, mouse buttons or the wheel, as Halo: The Master Chief
Collection's PC Halo: CE binds them (the defaults), rebound in config.toml's
[controls.keyboard] (action = "Key, Key", up to HALO_BIND_SLOTS each) and in
the menus' KEYBOARD & MOUSE page (port/linux/game/controls_menu.c).

In play the actions held press the game's own controls
(port/linux/src/xinput_sdl.c): each action that is one of the game's
controls presses the controller button the profile's button layout has
that control on (input_abstraction.c game_control_to_xbox_buttons), so the
profile's layout never moves a key; movement is the left stick, RELOAD and
ACTION are told apart (player_control.c: the controller's X does both). In the menus the keys keep their fixed
layout (arrows, Enter, Escape ...), whatever is bound.

key_bindings.c is plain C (no SDL): the names of keys come from SDL through
halo_bind_set_key_names, so the parsing, the defaults, the conflicts and
the migration are unit tested on their own (port/vita/tests/
key_bindings_test.c).
*/

#ifndef HALO_KEY_BINDINGS_H
#define HALO_KEY_BINDINGS_H

#include <stddef.h>

/* the actions, in the KEYBOARD & MOUSE page's order */
enum halo_bind_action
{
	HALO_BIND_MOVE_FORWARD,
	HALO_BIND_MOVE_BACKWARD,
	HALO_BIND_STRAFE_LEFT,
	HALO_BIND_STRAFE_RIGHT,
	HALO_BIND_JUMP,
	HALO_BIND_CROUCH,
	HALO_BIND_FIRE,
	HALO_BIND_ZOOM,
	HALO_BIND_THROW_GRENADE,
	HALO_BIND_MELEE,
	HALO_BIND_RELOAD,
	HALO_BIND_SWITCH_WEAPON,
	HALO_BIND_SWITCH_GRENADE,
	HALO_BIND_ACTION,
	HALO_BIND_FLASHLIGHT,
	HALO_BIND_SCOREBOARD,
	HALO_BIND_ACTION_COUNT
};

enum
{
	/* the most keys an action is bound to (the page shows the first two) */
	HALO_BIND_SLOTS = 4,
	/* an input: an SDL scancode (below HALO_BIND_MOUSE), a mouse button
	(HALO_BIND_MOUSE + SDL_BUTTON_*), or the wheel */
	HALO_BIND_NONE = -1,
	HALO_BIND_SCANCODES = 512,
	HALO_BIND_MOUSE = 0x200,
	HALO_BIND_MOUSE_BUTTONS = 16,
	HALO_BIND_WHEEL_UP = 0x220,
	HALO_BIND_WHEEL_DOWN,
	/* either way */
	HALO_BIND_WHEEL,
	HALO_BIND_INPUT_LIMIT,
};

struct halo_bindings
{
	int inputs[HALO_BIND_ACTION_COUNT][HALO_BIND_SLOTS];
};

/* an action's config.toml key ("throw_grenade": controls.keyboard.<key>),
its name in the menus (English, "THROW GRENADE": lang.h translates it) and
its default, Halo MCC's ("G"); NULL past the actions */
const char *halo_bind_action_key(int action);
const char *halo_bind_action_label(int action);
const char *halo_bind_action_default(int action);
/* the action of a config.toml key, or -1 */
int halo_bind_action_of_key(const char *key);

/* the names of keys (xinput_sdl.c gives SDL's: SDL_GetScancodeFromName,
SDL_GetScancodeName); without them only the mouse's and the wheel's are
known */
void halo_bind_set_key_names(int (*scancode_from_name)(const char *name),
	const char *(*scancode_name)(int scancode));

/* an input's name as config.toml keeps it ("Left Ctrl", "Mouse Right",
"Wheel Up"), "" for none; and back, HALO_BIND_NONE for a name nothing has
(case does not matter) */
void halo_bind_input_name(int input, char *name, size_t size);
int halo_bind_input_from_name(const char *name);

/* a binding's text ("F, Mouse 4") read into up to count inputs (the rest
HALO_BIND_NONE; names nothing has skipped, the first in *unknown when
given): how many were read */
int halo_bind_parse(const char *text, int *inputs, int count, char *unknown, size_t unknown_size);
/* inputs as a binding's text, the none skipped ("" for none at all) */
void halo_bind_format(const int *inputs, int count, char *text, size_t size);

/* every action at its default */
void halo_bind_defaults(struct halo_bindings *bindings);
/* the action (and slot) an input is bound to, other than except_action's:
-1 for none */
int halo_bind_owner(const struct halo_bindings *bindings, int input, int except_action, int *slot);
/* the input put in the action's slot. Another action that had it takes
the slot's old input in its place (a swap: nothing is left unbound that was
bound), and *displaced is that action (-1 for none). An input already the
action's in another slot moves to this slot (the two swapped). 0 for an
input that cannot be bound (none, out of range) */
int halo_bind_assign(struct halo_bindings *bindings, int action, int slot, int input, int *displaced);
/* the action's slot emptied, the later slots moved up */
void halo_bind_clear(struct halo_bindings *bindings, int action, int slot);
/* whether an input is bound to two actions at once (config.toml written
by hand): the first such, -1 for none, its actions in first and second */
int halo_bind_first_conflict(const struct halo_bindings *bindings, int *first, int *second);

/* (config.toml as an earlier version left it) each action's text, NULL for
an action the file does not have, made the current: an action at a default
of an earlier version (the PC build's keyboard before rebinding: its fixed
layout) is the current default; one the player chose is kept; and a default
taking a key a kept binding has gives it up (the player's binding wins).
texts[action] point into buffers of HALO_BIND_TEXT_SIZE the migration
writes the new text into; returns the actions changed (a bit each) */
#define HALO_BIND_TEXT_SIZE 128
unsigned long halo_bind_migrate(char texts[HALO_BIND_ACTION_COUNT][HALO_BIND_TEXT_SIZE],
	const unsigned char present[HALO_BIND_ACTION_COUNT]);
/* (the tests) whether text is one of an earlier version's defaults of the
action */
int halo_bind_is_old_default(int action, const char *text);

/* ---------- the runtime (xinput_sdl.c) */

#if !defined(HALO_VITA) && !defined(HALO_ANDROID)
/* the bindings in force (config.toml's, read again whenever a setting is
written: config_written_generation) */
const struct halo_bindings *halo_key_bindings(void);
/* an action's bindings written to config.toml and in force at once; every
action back at its default (the page's RESET TO MCC DEFAULTS) */
int halo_key_bindings_write(const struct halo_bindings *bindings);
int halo_key_bindings_reset(void);
/* the menus' asking for a key (controls_menu.c): begun, then polled each
frame: 0 waiting, 1 an input pressed (*input), 2 cancelled (Escape),
3 cleared (Delete). While it waits the keyboard and mouse press nothing
else: the key or click is the binding's alone */
void platform_binding_capture_begin(void);
void platform_binding_capture_end(void);
int platform_binding_capture_poll(int *input);
int platform_binding_capturing(void);
/* an input as the keyboard's layout labels the key, for the menus ("Z" is
"Y" on a German keyboard), in capitals */
void halo_bind_input_shown(int input, char *shown, size_t size);
#endif

#endif
