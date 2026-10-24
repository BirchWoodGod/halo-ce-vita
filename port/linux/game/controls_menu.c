/*
CONTROLS_MENU.C

The menus' KEYBOARD & MOUSE page (controls_menu.h): the PC builds' keyboard
and mouse bindings (port/linux/include/halo_key_bindings.h) and the mouse's
settings, Halo MCC's way: a list of the actions with their two keys, one of
them chosen (left and right), A (or a click) asking for the key or mouse
button to bind there, X clearing it, Y putting every key back at the MCC
defaults; the mouse's sensitivity, invert and raw input. Each change is in
force and written to config.toml at once.

One page, three looks: Menu style Halo CE's (menus/classic/keys.xml, the
Xbox's profile screens' own widgets), Halo 3's (halo3/keys.xml) and
Online's (online/keys.xml); each names its widgets as these functions find
them (the leaf of the name):
	op_group                SHOW: the actions shown (MOVEMENT, WEAPONS,
	                        ACTIONS, MOUSE); its value and arrows
	op_row1 ... op_row7     a row each: label, key1, key2 (the mouse's
	                        rows: their value in key1)
	op_reset                RESET TO MCC DEFAULTS
	keys_help, keys_status  the focused row's help, and what was done or
	                        what is asked for
The entries to it (the Halo 3 CUSTOMIZATION's row, the Halo CE profile's
row, the Online CUSTOMIZATION's CONTROLS tab's) run "port keys entry",
which hides them where the page means nothing (the Vita; Android).

While a key is asked for, the keyboard and mouse press nothing else
(platform_binding_capture_*: sdl_platform.c): the key or click is the
binding's alone; Escape cancels and Delete clears. A key another action has
is swapped (that action takes the slot's old key) and said: nothing is
left bound twice. The design follows ChupathingyCE's Controls Setup
(GPL-3.0, port/linux/game/controls_screen.c and its controls_setup.xml).
*/

#include "cseries.h"
#include "interface/ui_widget.h"
#include "halo_menus.h"
#include "controls_menu.h"
#include "../src/lang.h"
#include "../src/port_config.h"
#include "halo_key_bindings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the Vita and Android: no keyboard and mouse controls, no page; the
entries to it hidden) */
#if defined(HALO_VITA) || defined(HALO_ANDROID)
#define CONTROLS_MENU_NONE 1
#endif

void platform_log(char const *format, ...);
void ui_widget_port_give_focus(struct widget_instance *list, struct widget_instance *child);

/* ---------- the widgets, as ui_widget.c has them (menu_functions.c's) */

enum
{
	_controls_widget_type_container,
	_controls_widget_type_text_box,
	_controls_widget_type_spinner_list,
	_controls_widget_type_column_list,
};

struct controls_widget_animation
{
	short current_frame_index;
	short first_frame_index;
	short last_frame_index;
	short number_of_sprite_frames;
};

struct widget_instance
{
	long definition_tag_index;
	char const *name;
	short local_player_index;
	short horizontal_offset;
	short vertical_offset;
	short type;
	boolean visible;
	boolean render_regardless_of_controller_index;
	boolean disabled;
	boolean pause_game_time;
	boolean delete_recursion_lock;
	boolean widget_is_error_dialog;
	boolean close_if_local_player_controller_present;
	byte pad17;
	long creation_time;
	unsigned long milliseconds_to_auto_close;
	unsigned long auto_close_fade_time;
	real alpha_modifier;
	struct widget_instance *previous;
	struct widget_instance *next;
	struct widget_instance *parent;
	struct widget_instance *child;
	struct widget_instance *focused_child;
	union
	{
		struct
		{
			wchar_t *text;
			short string_list_index;
		} text_box;
		struct
		{
			short selected_index;
			short last_list_tab_direction;
			void *list_items;
			word number_of_items;
			struct widget_instance *extended_description;
			wchar_t *item_text;
		} list;
	} parameters;
	struct controls_widget_animation animation;
};

typedef char verify_controls_widget_instance_size[sizeof(struct widget_instance) == 0x58 ? 1 : -1];

/* ---------- constants */

enum
{
	KEYS_ROWS = 7,
	/* the mouse's rows (in a group's place of an action) */
	ROW_SENSITIVITY = -2,
	ROW_INVERT = -3,
	ROW_RAW = -4,
	/* the sensitivity's steps */
	SENSITIVITY_MINIMUM = 10,
	SENSITIVITY_MAXIMUM = 500,
	SENSITIVITY_STEP = 5,
};

static char const *const group_names[] = { N_("MOVEMENT"), N_("WEAPONS"), N_("ACTIONS"), N_("MOUSE") };

static signed char const groups[][KEYS_ROWS] =
{
	{ HALO_BIND_MOVE_FORWARD, HALO_BIND_MOVE_BACKWARD, HALO_BIND_STRAFE_LEFT, HALO_BIND_STRAFE_RIGHT, HALO_BIND_JUMP,
		HALO_BIND_CROUCH, HALO_BIND_BOOST },
	{ HALO_BIND_FIRE, HALO_BIND_ZOOM, HALO_BIND_THROW_GRENADE, HALO_BIND_MELEE, HALO_BIND_RELOAD, HALO_BIND_SWITCH_WEAPON,
		HALO_BIND_SWITCH_GRENADE },
	{ HALO_BIND_ACTION, HALO_BIND_FLASHLIGHT, HALO_BIND_SCOREBOARD, -1, -1, -1, -1 },
	{ ROW_SENSITIVITY, ROW_INVERT, ROW_RAW, -1, -1, -1, -1 },
};

#define GROUP_COUNT ((short)(sizeof(groups) / sizeof(groups[0])))

/* the actions' names as the page shows them (halo_bind_action_label's,
here so lang.h's word lists find them) */
static char const *const action_labels[HALO_BIND_ACTION_COUNT] =
{
	N_("MOVE FORWARD"), N_("MOVE BACKWARD"), N_("STRAFE LEFT"), N_("STRAFE RIGHT"), N_("JUMP"), N_("CROUCH"),
	N_("BOOST (HALO 2 VEHICLES)"), N_("FIRE"), N_("ZOOM"), N_("THROW GRENADE"), N_("MELEE"), N_("RELOAD"),
	N_("SWITCH WEAPON"), N_("SWITCH GRENADE"), N_("ACTION"), N_("FLASHLIGHT"), N_("SCOREBOARD"),
};

/* ---------- globals */

static struct
{
	short group;
	/* the key chosen on the focused row (0 or 1) */
	short slot;
	/* a key asked for: the action and slot */
	boolean capturing;
	short capture_action;
	short capture_slot;
	char status[256];
	/* (the log's, what it last said the page showed) */
	char said[256];
} keys_menu;

/* ---------- widgets */

static struct widget_instance *named(struct widget_instance *widget, char const *name)
{
	struct widget_instance *child;

	if (!widget)
		return NULL;
	if (widget->name && !strcmp(widget->name, name))
		return widget;
	for (child = widget->child; child; child = child->next)
	{
		struct widget_instance *found = named(child, name);

		if (found)
			return found;
	}
	return NULL;
}

static struct widget_instance *screen_of(struct widget_instance *widget)
{
	while (widget && widget->parent)
		widget = widget->parent;
	return widget;
}

/* a text box's own text (UTF-8, made the game's UTF-16) */
static void text_show(struct widget_instance *widget, char const *text)
{
	unsigned short characters[481];
	long length;

	if (!widget || widget->type != _controls_widget_type_text_box)
		return;
	length = halo_menus_utf16(text ? text : "", characters, (long)NUMBEROF(characters));
	widget->parameters.text_box.text = ui_widget_realloc(widget->parameters.text_box.text,
		(word)(length * sizeof(wchar_t)), __FILE__, __LINE__);
	if (!widget->parameters.text_box.text)
		return;
	memcpy(widget->parameters.text_box.text, characters, length * sizeof(wchar_t));
}

static void shown(struct widget_instance *widget, boolean visible)
{
	if (!widget)
		return;
	widget_instance_set_visibility_recursive(widget, visible);
	widget->disabled = !visible;
}

/* the row (1 to KEYS_ROWS) a row widget is, 0 the group's, KEYS_ROWS + 1
the reset's, -1 none */
static short row_of(struct widget_instance *widget)
{
	for (; widget; widget = widget->parent)
	{
		if (!widget->name)
			continue;
		if (!strcmp(widget->name, "op_group"))
			return 0;
		if (!strcmp(widget->name, "op_reset"))
			return KEYS_ROWS + 1;
		if (!strncmp(widget->name, "op_row", 6) && widget->name[6] >= '1' && widget->name[6] <= '0' + KEYS_ROWS &&
			!widget->name[7])
		{
			return (short)(widget->name[6] - '0');
		}
	}
	return -1;
}

/* the action (or the mouse's ROW_*) of a row 1 to KEYS_ROWS, -1 none */
static short row_action(short row)
{
	return row >= 1 && row <= KEYS_ROWS ? groups[keys_menu.group][row - 1] : -1;
}

#ifndef CONTROLS_MENU_NONE
/* ---------- what the page shows */

/* an input as the page shows it ("G", "LEFT MOUSE"), "-" for none */
static void input_shown(int input, char *text, size_t size)
{
	if (input == HALO_BIND_NONE)
		snprintf(text, size, "-");
	else
		halo_bind_input_shown(input, text, size);
}

static void mouse_value(short row, char *text, size_t size)
{
	switch (row)
	{
	case ROW_SENSITIVITY:
		snprintf(text, size, "%.2f", config_real("input.mouse_sensitivity"));
		break;
	case ROW_INVERT:
		snprintf(text, size, "%s", config_boolean("input.invert_mouse") ? T("YES") : T("NO"));
		break;
	case ROW_RAW:
		snprintf(text, size, "%s", config_boolean("input.raw_mouse") ? T("ON") : T("OFF"));
		break;
	default:
		text[0] = 0;
	}
}

static char const *mouse_label(short row)
{
	switch (row)
	{
	case ROW_SENSITIVITY: return T("MOUSE SENSITIVITY");
	case ROW_INVERT: return T("INVERT MOUSE");
	case ROW_RAW: return T("RAW INPUT");
	}
	return "";
}

static void help_of(short row, char *text, size_t size)
{
	short action = row_action(row);
	char const *help;

	if (row == 0)
		help = T("Left and right show the movement, weapon or\nother actions' keys, or the mouse's settings.");
	else if (row == KEYS_ROWS + 1)
		help = T("Every key back at Halo MCC's PC defaults.\nThe mouse's settings stay as they are.");
	else if (action == ROW_SENSITIVITY)
		help = T("How far the view turns for the mouse's\nmovement. Left and right change it.");
	else if (action == ROW_INVERT)
		help = T("Moving the mouse forward looks down.");
	else if (action == ROW_RAW)
		help = T("ON: the mouse's own movement. OFF: with the\nsystem's pointer speed and acceleration.");
	else if (action == HALO_BIND_BOOST)
		help = T("Held: Halo 2's vehicles (the Ghost) boost.\nA binds a key, left and right pick which.");
	else if (action == HALO_BIND_ACTION)
		help = T("Get in, pick up (held: swap weapons), use.\nIt never reloads: RELOAD does.");
	else
		help = T("A binds a key or mouse button. Left and right\nchoose the first or second key; X clears it.");
	snprintf(text, size, "%s", help);
}

/* ---------- changes */

static void capture_begin(short action, short slot)
{
	keys_menu.capturing = TRUE;
	keys_menu.capture_action = action;
	keys_menu.capture_slot = slot;
	snprintf(keys_menu.status, sizeof(keys_menu.status), T("Press a key or mouse button for %s. Escape cancels,\nDelete clears it."),
		T(action_labels[action]));
	platform_binding_capture_begin();
}

static void capture_cancel(void)
{
	if (!keys_menu.capturing)
		return;
	keys_menu.capturing = FALSE;
	platform_binding_capture_end();
}

/* the key asked for, once pressed: bound (swapped with the action that had
it), cleared or nothing */
static void capture_update(void)
{
	struct halo_bindings bindings;
	short action = keys_menu.capture_action, slot = keys_menu.capture_slot;
	int input = HALO_BIND_NONE, result, displaced = -1;
	char name[64], other_name[64];

	if (!keys_menu.capturing || !(result = platform_binding_capture_poll(&input)))
		return;
	keys_menu.capturing = FALSE;
	bindings = *halo_key_bindings();
	if (result == 2)
	{
		snprintf(keys_menu.status, sizeof(keys_menu.status), "%s", T("Nothing changed."));
		return;
	}
	if (result == 3)
	{
		halo_bind_clear(&bindings, action, slot);
		halo_key_bindings_write(&bindings);
		snprintf(keys_menu.status, sizeof(keys_menu.status), T("%s: cleared."), T(action_labels[action]));
		return;
	}
	if (!halo_bind_assign(&bindings, action, slot, input, &displaced))
	{
		snprintf(keys_menu.status, sizeof(keys_menu.status), "%s", T("That can't be bound."));
		return;
	}
	halo_key_bindings_write(&bindings);
	input_shown(input, name, sizeof(name));
	if (displaced >= 0)
	{
		/* (the conflict said: what the other action has now) */
		int other_input = bindings.inputs[displaced][0];

		if (other_input == HALO_BIND_NONE)
		{
			snprintf(keys_menu.status, sizeof(keys_menu.status), T("%s was %s's: %s has no key now."), name,
				T(action_labels[displaced]), T(action_labels[displaced]));
		}
		else
		{
			char shown_keys[96], second[64];

			input_shown(other_input, other_name, sizeof(other_name));
			snprintf(shown_keys, sizeof(shown_keys), "%s", other_name);
			if (bindings.inputs[displaced][1] != HALO_BIND_NONE)
			{
				input_shown(bindings.inputs[displaced][1], second, sizeof(second));
				snprintf(shown_keys, sizeof(shown_keys), "%s, %s", other_name, second);
			}
			snprintf(keys_menu.status, sizeof(keys_menu.status), T("%s was %s's: it has %s now."), name,
				T(action_labels[displaced]), shown_keys);
		}
	}
	else if (input == 40 || input == 58 || (input >= 79 && input <= 82))
	{
		/* (Enter, F1 and the arrows have parts of their own in play) */
		snprintf(keys_menu.status, sizeof(keys_menu.status), T("%s: %s (in play it no longer does its own part)."),
			T(action_labels[action]), name);
	}
	else
		snprintf(keys_menu.status, sizeof(keys_menu.status), "%s: %s", T(action_labels[action]), name);
}

static void mouse_step(short row, int step)
{
	switch (row)
	{
	case ROW_SENSITIVITY:
	{
		long value = (long)(config_real("input.mouse_sensitivity") * 100.0 + 0.5);

		value = (value / SENSITIVITY_STEP + step) * SENSITIVITY_STEP;
		if (value < SENSITIVITY_MINIMUM)
			value = SENSITIVITY_MINIMUM;
		if (value > SENSITIVITY_MAXIMUM)
			value = SENSITIVITY_MAXIMUM;
		config_write_real("input.mouse_sensitivity", value / 100.0);
		break;
	}
	case ROW_INVERT:
		config_write_boolean("input.invert_mouse", !config_boolean("input.invert_mouse"));
		break;
	case ROW_RAW:
		config_write_boolean("input.raw_mouse", !config_boolean("input.raw_mouse"));
		break;
	default:
		return;
	}
	{
		char value[32];

		mouse_value(row, value, sizeof(value));
		snprintf(keys_menu.status, sizeof(keys_menu.status), "%s: %s", mouse_label(row), value);
		platform_log("controls: %s %s", mouse_label(row), value);
	}
}

static void side(struct widget_instance *widget, int step)
{
	short row = row_of(widget), action;

	if (keys_menu.capturing)
		return;
	if (row == 0)
	{
		keys_menu.group = (short)((keys_menu.group + step + GROUP_COUNT) % GROUP_COUNT);
		keys_menu.status[0] = 0;
		return;
	}
	action = row_action(row);
	if (action <= ROW_SENSITIVITY)
		mouse_step(action, step);
	else if (action >= 0)
		keys_menu.slot = (short)(1 - keys_menu.slot);
}

static void reset(void)
{
	capture_cancel();
	halo_key_bindings_reset();
	snprintf(keys_menu.status, sizeof(keys_menu.status), "%s", T("Every key is back at Halo MCC's defaults."));
}

/* ---------- the update */

static void row_update(struct widget_instance *item, short row, boolean focused)
{
	short action = row_action(row);
	struct widget_instance *found;
	char first[64], second[64];
	const struct halo_bindings *bindings = halo_key_bindings();

	if (!item)
		return;
	shown(item, action != -1);
	if (action == -1)
		return;
	if (action <= ROW_SENSITIVITY)
	{
		text_show(named(item, "label"), mouse_label(action));
		mouse_value(action, first, sizeof(first));
		if (focused)
		{
			char arrows[80];

			snprintf(arrows, sizeof(arrows), "< %s >", first);
			snprintf(first, sizeof(first), "%s", arrows);
		}
		text_show(named(item, "key1"), first);
		text_show(named(item, "key2"), "");
		return;
	}
	text_show(named(item, "label"), T(action_labels[action]));
	input_shown(bindings->inputs[action][0], first, sizeof(first));
	input_shown(bindings->inputs[action][1], second, sizeof(second));
	if (keys_menu.capturing && keys_menu.capture_action == action)
		snprintf(keys_menu.capture_slot ? second : first, 64, "%s", T("PRESS A KEY"));
	else if (focused)
	{
		char *chosen = keys_menu.slot ? second : first;
		char marked[80];

		snprintf(marked, sizeof(marked), "> %s <", chosen);
		snprintf(chosen, 64, "%s", marked);
	}
	if ((found = named(item, "key1")) != NULL)
		text_show(found, first);
	if ((found = named(item, "key2")) != NULL)
		text_show(found, second);
}

static void keys_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *focused = list->focused_child;
	short focused_row = row_of(focused), row;
	struct widget_instance *found;
	char text[256];

	capture_update();
	/* (a row hidden by the group's change: the focus to SHOW) */
	if (focused && focused->disabled && (found = named(list, "op_group")) != NULL)
	{
		ui_widget_port_give_focus(list, found);
		focused_row = 0;
	}
	if ((found = named(list, "op_group")) != NULL)
	{
		struct widget_instance *value = named(found, "value");

		text_show(value, T(group_names[keys_menu.group]));
		if (found->animation.last_frame_index >= 1)
			found->animation.current_frame_index = focused_row == 0;
	}
	for (row = 1; row <= KEYS_ROWS; row++)
	{
		char name[16];
		struct widget_instance *item;

		snprintf(name, sizeof(name), "op_row%d", row);
		item = named(list, name);
		row_update(item, row, focused_row == row);
		if (item && item->animation.last_frame_index >= 1)
			item->animation.current_frame_index = focused_row == row;
	}
	if ((found = named(list, "op_reset")) != NULL && found->animation.last_frame_index >= 1)
		found->animation.current_frame_index = focused_row == KEYS_ROWS + 1;
	help_of(focused_row, text, sizeof(text));
	text_show(named(screen, "keys_help"), text);
	text_show(named(screen, "keys_status"), keys_menu.status);
	snprintf(text, sizeof(text), "group %s, row %d, slot %d%s; %s", group_names[keys_menu.group], focused_row,
		keys_menu.slot, keys_menu.capturing ? ", asking" : "", keys_menu.status);
	if (strcmp(text, keys_menu.said))
	{
		snprintf(keys_menu.said, sizeof(keys_menu.said), "%s", text);
		platform_log("menus: keys: %s", text);
	}
}

#endif

/* (port keys entry, on an entry's row) hidden where the page means
nothing, the rows after it moved up into its place */
static void entry_update(struct widget_instance *entry)
{
	static boolean said;
	struct widget_instance *after;
	short height = 0;

	if (controls_menu_available() || !entry || entry->disabled)
		return;
	shown(entry, FALSE);
	/* (its place: its offset from the row before it, as laid out; the rows'
	offsets are each its own, not one after the other: the Halo CE
	profile's rows extend the map's, each at its own place) */
	if (entry->previous)
		height = (short)(entry->vertical_offset - entry->previous->vertical_offset);
	else if (entry->next)
		height = (short)(entry->next->vertical_offset - entry->vertical_offset);
	for (after = entry->next; after && height > 0; after = after->next)
		after->vertical_offset = (short)(after->vertical_offset - height);
	/* (and the Halo CE profile's line over SAVE CHANGES, the screen's) */
	if (height > 0 && (after = named(screen_of(entry), "blueline")) != NULL)
		after->vertical_offset = (short)(after->vertical_offset - height);
	if (!said)
	{
		said = TRUE;
		platform_log("menus: keys: the KEYBOARD & MOUSE entry hidden (no keyboard and mouse controls on this build)");
	}
}

/* ---------- public code */

static char const *const function_names[] =
{
	"port keys init", "port keys close", "port keys accept", "port keys left", "port keys right", "port keys clear",
	"port keys reset", "port keys back",
};

enum
{
	_keys_init,
	_keys_close,
	_keys_accept,
	_keys_left,
	_keys_right,
	_keys_clear,
	_keys_reset,
	_keys_back,
};

static char const *const game_data_names[] =
{
	"port keys", "port keys entry",
};

char const *controls_menu_function_name(long index)
{
	return index >= 0 && index < (long)NUMBEROF(function_names) ? function_names[index] : NULL;
}

char const *controls_menu_game_data_input_name(long index)
{
	return index >= 0 && index < (long)NUMBEROF(game_data_names) ? game_data_names[index] : NULL;
}

long controls_menu_function_index(char const *name)
{
	long index;

	for (index = 0; name && index < (long)NUMBEROF(function_names); index++)
	{
		if (!strcmp(name, function_names[index]))
			return index;
	}
	return -1;
}

long controls_menu_game_data_input_index(char const *name)
{
	long index;

	for (index = 0; name && index < (long)NUMBEROF(game_data_names); index++)
	{
		if (!strcmp(name, game_data_names[index]))
			return index;
	}
	return -1;
}

int controls_menu_available(void)
{
#if defined(HALO_VITA) || defined(HALO_ANDROID)
	return 0;
#else
#ifndef HALO_RELEASE
	/* (tests only) HALO_TEST_NO_KEYS_PAGE=1: as the Vita, the entries
	hidden (the menus' layout without them) */
	char const *test = getenv("HALO_TEST_NO_KEYS_PAGE");

	if (test && atoi(test))
		return 0;
#endif
	return 1;
#endif
}

unsigned char controls_menu_event(struct widget_instance *widget, struct event_record *event, long index,
	unsigned char *widget_deleted)
{
	short row = row_of(widget), action = row_action(row);

	(void)event;
	(void)widget_deleted;
#ifdef CONTROLS_MENU_NONE
	(void)row;
	(void)action;
	return TRUE;
#else
	switch (index)
	{
	case _keys_init:
		capture_cancel();
		keys_menu.group = 0;
		keys_menu.slot = 0;
		keys_menu.status[0] = 0;
		keys_menu.said[0] = 0;
		platform_log("menus: keys: KEYBOARD & MOUSE opened");
		return TRUE;
	case _keys_close:
		capture_cancel();
		platform_log("menus: keys: closed");
		return TRUE;
	case _keys_back:
		/* (a key being asked for: B, a controller's, cancels the asking, not
		the page) */
		if (keys_menu.capturing)
		{
			capture_cancel();
			snprintf(keys_menu.status, sizeof(keys_menu.status), "%s", T("Nothing changed."));
			return FALSE;
		}
		return TRUE;
	case _keys_accept:
		if (keys_menu.capturing)
			return TRUE;
		if (row == 0)
			side(widget, 1);
		else if (row == KEYS_ROWS + 1)
			reset();
		else if (action <= ROW_SENSITIVITY)
			mouse_step(action, 1);
		else if (action >= 0)
			capture_begin(action, keys_menu.slot);
		return TRUE;
	case _keys_left:
		side(widget, -1);
		return TRUE;
	case _keys_right:
		side(widget, 1);
		return TRUE;
	case _keys_clear:
		if (!keys_menu.capturing && action >= 0)
		{
			struct halo_bindings bindings = *halo_key_bindings();

			halo_bind_clear(&bindings, action, keys_menu.slot);
			halo_key_bindings_write(&bindings);
			snprintf(keys_menu.status, sizeof(keys_menu.status), T("%s: cleared."), T(action_labels[action]));
		}
		return TRUE;
	case _keys_reset:
		reset();
		return TRUE;
	}
	return TRUE;
#endif
}

void controls_menu_game_data(struct widget_instance *widget, long index)
{
	switch (index)
	{
	case 0:
#ifndef CONTROLS_MENU_NONE
		if (widget && widget->type == _controls_widget_type_column_list)
			keys_update(widget);
#endif
		break;
	case 1:
		entry_update(widget);
		break;
	}
}
