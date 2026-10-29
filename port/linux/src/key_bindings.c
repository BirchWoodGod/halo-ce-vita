/*
KEY_BINDINGS.C

The keyboard and mouse's bindings (port/linux/include/halo_key_bindings.h):
the actions, their Halo MCC defaults, the names of inputs, a binding's text
read and written, conflicts, and the migration of an earlier version's
config.toml. Plain C: the keys' names are SDL's, handed in
(halo_bind_set_key_names), so this is tested without SDL
(port/vita/tests/key_bindings_test.c).

The defaults are Halo: The Master Chief Collection's for Halo: CE on PC, as
the owner asked ("like MCC"): the mouse fires (left) and zooms (right), G
throws a grenade, F is melee, E the action (enter, pick up, hold to swap)
and R reload - apart, as on PC, where the Xbox's X does both -, Q the
flashlight, Tab and the wheel switch weapons, H (or the middle button)
switches grenades, Ctrl crouches, Space jumps, Backspace shows the scores;
Escape pauses (fixed, as the menus'
back).
*/

#include "../include/halo_key_bindings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- the actions */

static const struct
{
	const char *key;
	const char *label;
	const char *default_text;
} actions[HALO_BIND_ACTION_COUNT] =
{
	{ "move_forward", "MOVE FORWARD", "W" },
	{ "move_backward", "MOVE BACKWARD", "S" },
	{ "strafe_left", "STRAFE LEFT", "A" },
	{ "strafe_right", "STRAFE RIGHT", "D" },
	{ "jump", "JUMP", "Space" },
	{ "crouch", "CROUCH", "Left Ctrl, C" },
	{ "fire", "FIRE", "Mouse Left" },
	{ "zoom", "ZOOM", "Mouse Right, Z" },
	{ "throw_grenade", "THROW GRENADE", "G" },
	{ "melee", "MELEE", "F, Mouse 4" },
	{ "reload", "RELOAD", "R" },
	{ "switch_weapon", "SWITCH WEAPON", "Tab, Wheel" },
	{ "switch_grenade", "SWITCH GRENADE", "H, Mouse Middle" },
	{ "action", "ACTION", "E" },
	{ "flashlight", "FLASHLIGHT", "Q" },
	{ "scoreboard", "SCOREBOARD", "Backspace" },
};

/* the defaults of earlier versions, each action's (NULL: none): the
desktop build's fixed layout before rebinding (xinput_sdl.c's
keyboard_gamepad up to 1.1.1-insider.24), as its keys pressed each action.
A config.toml that has one of these was never the player's choice */
static const char *const old_defaults[][HALO_BIND_ACTION_COUNT] =
{
	{
		"W", "S", "A", "D", "Space, Return", "Left Ctrl, C", "Mouse Left", "Z, Mouse Middle",
		"Mouse Right, G", "F, Backspace, Mouse 4", "E, R", "Tab, Wheel", "X", "E, R", "Q", "F1",
	},
};

const char *halo_bind_action_key(int action)
{
	return action >= 0 && action < HALO_BIND_ACTION_COUNT ? actions[action].key : NULL;
}

const char *halo_bind_action_label(int action)
{
	return action >= 0 && action < HALO_BIND_ACTION_COUNT ? actions[action].label : NULL;
}

const char *halo_bind_action_default(int action)
{
	return action >= 0 && action < HALO_BIND_ACTION_COUNT ? actions[action].default_text : NULL;
}

int halo_bind_action_of_key(const char *key)
{
	int action;

	for (action = 0; key && action < HALO_BIND_ACTION_COUNT; action++)
	{
		if (!strcmp(actions[action].key, key))
			return action;
	}
	return -1;
}

/* ---------- names */

static int (*key_from_name)(const char *name);
static const char *(*key_name)(int scancode);

void halo_bind_set_key_names(int (*scancode_from_name)(const char *name), const char *(*scancode_name)(int scancode))
{
	key_from_name = scancode_from_name;
	key_name = scancode_name;
}

/* the inputs SDL has no name for, or one a list split at commas cannot
hold (SDL's "," and "Keypad ,") */
static const struct
{
	const char *name;
	int input;
} named_inputs[] =
{
	{ "Mouse Left", HALO_BIND_MOUSE + 1 },
	{ "Mouse Middle", HALO_BIND_MOUSE + 2 },
	{ "Mouse Right", HALO_BIND_MOUSE + 3 },
	{ "Mouse 4", HALO_BIND_MOUSE + 4 },
	{ "Mouse 5", HALO_BIND_MOUSE + 5 },
	{ "Wheel", HALO_BIND_WHEEL },
	{ "Wheel Up", HALO_BIND_WHEEL_UP },
	{ "Wheel Down", HALO_BIND_WHEEL_DOWN },
	/* (SDL's scancodes 54 and 133) */
	{ "Comma", 54 },
	{ "Keypad Comma", 133 },
};

static int same_name(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
	}
	return *a == *b;
}

void halo_bind_input_name(int input, char *name, size_t size)
{
	size_t index;
	const char *sdl;

	if (!size)
		return;
	name[0] = 0;
	for (index = 0; index < sizeof(named_inputs) / sizeof(named_inputs[0]); index++)
	{
		if (named_inputs[index].input == input)
		{
			snprintf(name, size, "%s", named_inputs[index].name);
			return;
		}
	}
	if (input >= HALO_BIND_MOUSE && input < HALO_BIND_MOUSE + HALO_BIND_MOUSE_BUTTONS)
		snprintf(name, size, "Mouse %d", input - HALO_BIND_MOUSE);
	else if (input > 0 && input < HALO_BIND_SCANCODES && key_name && (sdl = key_name(input)) != NULL && *sdl &&
		!strchr(sdl, ','))
	{
		snprintf(name, size, "%s", sdl);
	}
}

int halo_bind_input_from_name(const char *name)
{
	size_t index;

	if (!name || !*name)
		return HALO_BIND_NONE;
	for (index = 0; index < sizeof(named_inputs) / sizeof(named_inputs[0]); index++)
	{
		if (same_name(name, named_inputs[index].name))
			return named_inputs[index].input;
	}
	if ((name[0] == 'M' || name[0] == 'm') && same_name(name, "Mouse 6") + same_name(name, "Mouse 7") +
		same_name(name, "Mouse 8") > 0)
	{
		return HALO_BIND_MOUSE + (name[6] - '0');
	}
	if (key_from_name)
	{
		int scancode = key_from_name(name);

		if (scancode > 0 && scancode < HALO_BIND_SCANCODES)
			return scancode;
	}
	return HALO_BIND_NONE;
}

static int input_valid(int input)
{
	return (input > 0 && input < HALO_BIND_SCANCODES) ||
		(input > HALO_BIND_MOUSE && input < HALO_BIND_MOUSE + HALO_BIND_MOUSE_BUTTONS) ||
		input == HALO_BIND_WHEEL_UP || input == HALO_BIND_WHEEL_DOWN || input == HALO_BIND_WHEEL;
}

/* ---------- a binding's text */

int halo_bind_parse(const char *text, int *inputs, int count, char *unknown, size_t unknown_size)
{
	int read = 0, index;

	for (index = 0; index < count; index++)
		inputs[index] = HALO_BIND_NONE;
	if (unknown && unknown_size)
		unknown[0] = 0;
	while (text && *text)
	{
		char name[64];
		size_t length;
		int input;

		while (*text == ' ' || *text == '\t' || *text == ',')
			text++;
		length = strcspn(text, ",");
		if (!length)
			break;
		snprintf(name, sizeof(name), "%.*s", (int)(length < sizeof(name) - 1 ? length : sizeof(name) - 1), text);
		text += length;
		while (*name && (name[strlen(name) - 1] == ' ' || name[strlen(name) - 1] == '\t'))
			name[strlen(name) - 1] = 0;
		input = halo_bind_input_from_name(name);
		if (input == HALO_BIND_NONE)
		{
			if (unknown && unknown_size && !unknown[0])
				snprintf(unknown, unknown_size, "%s", name);
			continue;
		}
		/* (a key named twice counts once) */
		for (index = 0; index < read && inputs[index] != input; index++)
			;
		if (index < read || read >= count)
			continue;
		inputs[read++] = input;
	}
	return read;
}

void halo_bind_format(const int *inputs, int count, char *text, size_t size)
{
	size_t length = 0;
	int index;

	if (!size)
		return;
	text[0] = 0;
	for (index = 0; index < count; index++)
	{
		char name[64];

		if (inputs[index] == HALO_BIND_NONE)
			continue;
		halo_bind_input_name(inputs[index], name, sizeof(name));
		if (!name[0])
			continue;
		length += (size_t)snprintf(text + length, length < size ? size - length : 0, "%s%s", length ? ", " : "", name);
		if (length >= size)
		{
			text[size - 1] = 0;
			return;
		}
	}
}

/* ---------- bindings */

void halo_bind_defaults(struct halo_bindings *bindings)
{
	int action;

	for (action = 0; action < HALO_BIND_ACTION_COUNT; action++)
		halo_bind_parse(actions[action].default_text, bindings->inputs[action], HALO_BIND_SLOTS, NULL, 0);
}

int halo_bind_owner(const struct halo_bindings *bindings, int input, int except_action, int *slot)
{
	int action, index;

	if (input == HALO_BIND_NONE)
		return -1;
	for (action = 0; action < HALO_BIND_ACTION_COUNT; action++)
	{
		if (action == except_action)
			continue;
		for (index = 0; index < HALO_BIND_SLOTS; index++)
		{
			if (bindings->inputs[action][index] == input)
			{
				if (slot)
					*slot = index;
				return action;
			}
		}
	}
	return -1;
}

/* the action's inputs packed to the front (none at the end) */
static void pack(struct halo_bindings *bindings, int action)
{
	int from, to = 0;

	for (from = 0; from < HALO_BIND_SLOTS; from++)
	{
		if (bindings->inputs[action][from] != HALO_BIND_NONE)
			bindings->inputs[action][to++] = bindings->inputs[action][from];
	}
	while (to < HALO_BIND_SLOTS)
		bindings->inputs[action][to++] = HALO_BIND_NONE;
}

int halo_bind_assign(struct halo_bindings *bindings, int action, int slot, int input, int *displaced)
{
	int old, other, other_slot = 0, index;

	if (displaced)
		*displaced = -1;
	if (action < 0 || action >= HALO_BIND_ACTION_COUNT || slot < 0 || slot >= HALO_BIND_SLOTS || !input_valid(input))
		return 0;
	old = bindings->inputs[action][slot];
	if (old == input)
		return 1;
	/* (already this action's, in another slot: the two swapped) */
	for (index = 0; index < HALO_BIND_SLOTS; index++)
	{
		if (index != slot && bindings->inputs[action][index] == input)
		{
			bindings->inputs[action][index] = old;
			bindings->inputs[action][slot] = input;
			pack(bindings, action);
			return 1;
		}
	}
	other = halo_bind_owner(bindings, input, action, &other_slot);
	if (other >= 0)
	{
		/* (the other action takes the old input, unless it has that one
		already: then its slot just empties) */
		for (index = 0; index < HALO_BIND_SLOTS && bindings->inputs[other][index] != old; index++)
			;
		bindings->inputs[other][other_slot] = old != HALO_BIND_NONE && index == HALO_BIND_SLOTS ? old : HALO_BIND_NONE;
		pack(bindings, other);
		if (displaced)
			*displaced = other;
	}
	bindings->inputs[action][slot] = input;
	pack(bindings, action);
	return 1;
}

void halo_bind_clear(struct halo_bindings *bindings, int action, int slot)
{
	if (action < 0 || action >= HALO_BIND_ACTION_COUNT || slot < 0 || slot >= HALO_BIND_SLOTS)
		return;
	bindings->inputs[action][slot] = HALO_BIND_NONE;
	pack(bindings, action);
}

int halo_bind_first_conflict(const struct halo_bindings *bindings, int *first, int *second)
{
	int action, slot;

	for (action = 0; action < HALO_BIND_ACTION_COUNT; action++)
	{
		for (slot = 0; slot < HALO_BIND_SLOTS; slot++)
		{
			int input = bindings->inputs[action][slot];
			int other = input == HALO_BIND_NONE ? -1 : halo_bind_owner(bindings, input, action, NULL);

			if (other > action)
			{
				if (first)
					*first = action;
				if (second)
					*second = other;
				return input;
			}
		}
	}
	return -1;
}

/* ---------- migration */

/* text with each name trimmed and in small letters, ", " between them */
static void normalized(const char *text, char *out, size_t size)
{
	size_t length = 0;

	out[0] = 0;
	while (text && *text && length + 1 < size)
	{
		size_t name_length;

		while (*text == ' ' || *text == '\t' || *text == ',')
			text++;
		name_length = strcspn(text, ",");
		while (name_length && (text[name_length - 1] == ' ' || text[name_length - 1] == '\t'))
			name_length--;
		if (!name_length)
			break;
		if (length && length + 2 < size)
		{
			out[length++] = ',';
			out[length++] = ' ';
		}
		for (; name_length && length + 1 < size; name_length--)
			out[length++] = (char)tolower((unsigned char)*text++);
		out[length] = 0;
		text += strcspn(text, ",");
	}
}

static int same_text(const char *a, const char *b)
{
	char first[HALO_BIND_TEXT_SIZE * 2], second[HALO_BIND_TEXT_SIZE * 2];

	normalized(a, first, sizeof(first));
	normalized(b, second, sizeof(second));
	return !strcmp(first, second);
}

int halo_bind_is_old_default(int action, const char *text)
{
	size_t version;

	if (action < 0 || action >= HALO_BIND_ACTION_COUNT || !text)
		return 0;
	for (version = 0; version < sizeof(old_defaults) / sizeof(old_defaults[0]); version++)
	{
		if (old_defaults[version][action] && same_text(old_defaults[version][action], text))
			return 1;
	}
	return 0;
}

/* the names of a text, one at a time (*cursor moves on); 0 at its end */
static int next_name(const char **cursor, char *name, size_t size)
{
	const char *text = *cursor;
	size_t length;

	while (*text == ' ' || *text == '\t' || *text == ',')
		text++;
	length = strcspn(text, ",");
	*cursor = text + length;
	while (length && (text[length - 1] == ' ' || text[length - 1] == '\t'))
		length--;
	if (!length)
		return 0;
	snprintf(name, size, "%.*s", (int)(length < size - 1 ? length : size - 1), text);
	return 1;
}

/* whether a name is in a text's names (case aside) */
static int text_has_name(const char *text, const char *name)
{
	char other[64];

	while (next_name(&text, other, sizeof(other)))
	{
		if (same_name(other, name))
			return 1;
	}
	return 0;
}

unsigned long halo_bind_migrate(char texts[HALO_BIND_ACTION_COUNT][HALO_BIND_TEXT_SIZE],
	const unsigned char present[HALO_BIND_ACTION_COUNT])
{
	unsigned char kept[HALO_BIND_ACTION_COUNT];
	unsigned long changed = 0;
	int action, other;

	/* the player's own: there, and neither an earlier default nor this
	version's */
	for (action = 0; action < HALO_BIND_ACTION_COUNT; action++)
	{
		kept[action] = present[action] && !halo_bind_is_old_default(action, texts[action]) &&
			!same_text(texts[action], actions[action].default_text);
	}
	for (action = 0; action < HALO_BIND_ACTION_COUNT; action++)
	{
		char text[HALO_BIND_TEXT_SIZE], name[64];
		const char *cursor = actions[action].default_text;
		size_t length = 0;

		if (kept[action])
			continue;
		/* this version's default, less the keys a kept binding has */
		text[0] = 0;
		while (next_name(&cursor, name, sizeof(name)))
		{
			for (other = 0; other < HALO_BIND_ACTION_COUNT; other++)
			{
				if (kept[other] && text_has_name(texts[other], name))
					break;
			}
			if (other < HALO_BIND_ACTION_COUNT)
				continue;
			length += (size_t)snprintf(text + length, length < sizeof(text) ? sizeof(text) - length : 0, "%s%s",
				length ? ", " : "", name);
		}
		if (!present[action] || strcmp(text, texts[action]))
		{
			snprintf(texts[action], HALO_BIND_TEXT_SIZE, "%s", text);
			changed |= 1UL << action;
		}
	}
	return changed;
}
