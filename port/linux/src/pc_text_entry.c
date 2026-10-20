/*
PC_TEXT_ENTRY.C

The PC build's text box (pc_text_entry.h): its state and editing. The
keyboard's text and keys come from sdl_platform.c's event loop, the
gamepads' buttons from xinput_sdl.c, both on the game's thread; the
picture is pc_text_entry_overlay.c's (the render's), so the state is
behind a lock.

The text is what the game keeps of any typed line (menu_functions.c
sanitize: printable ASCII, the characters its fonts and lists show): the
keyboard's UTF-8 is read whole, character by character, and a character
past ASCII (an accented letter, an emoji) is left out, never cut into
bytes. At most halo_text_input_maximum characters (a profile's name 11, a
code 9); a character past that is refused and the count flashes.

The on-screen keyboard (for a gamepad alone: a Steam Deck's, a TV's) is a
grid of four rows of ten characters - the digits, then three rows of
letters in a keyboard's order with some punctuation, capitals with SHIFT;
a page of symbols with #+= - and a row of actions: SHIFT, #+= (abc), SPACE,
DELETE and DONE. The D-pad or the left stick moves (held: repeats), A types
the key, B deletes, X a space, Y cancels, Start accepts, the shoulders
SHIFT and #+=, the triggers move the caret. It shows once a gamepad is used
(or was the device used last as the box opened); the keyboard types all
the same.

None of it on the Vita (its host opens the system's keyboard: vita_ime.c).
*/

#ifndef HALO_VITA

#include "pc_text_entry.h"
#include "system_link_shortcut.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);

/* a held direction repeats after this long, then this often */
#define REPEAT_DELAY_MS 400
#define REPEAT_RATE_MS 90
/* the left stick pushed this far is a direction */
#define STICK_THRESHOLD 16000

static pthread_mutex_t entry_lock = PTHREAD_MUTEX_INITIALIZER;

static struct
{
	int open;
	char title[PC_TEXT_TEXT_SIZE];
	char text[PC_TEXT_TEXT_SIZE];
	int length;
	int caret;
	int maximum;
	int password;
	int grid;
	int row;
	int column;
	int shift;
	int symbols;
	unsigned long now_ms;
	unsigned long opened_ms;
	unsigned long refused_ms;
	/* the gamepads: the buttons at the last read (the first read after
	the box opened only learns them: a button held as it opened types
	nothing), the direction repeating and when next */
	unsigned int pad_previous;
	int pad_fresh;
	unsigned long repeat_ms;
	/* (closed) the buttons that closed it are kept from the game until
	they are let go */
	int pad_hold;
} entry;

/* the grid's characters: the letters' page, its capitals, the symbols' */
static const char *const grid_letters[PC_TEXT_GRID_CHARACTER_ROWS] =
{
	"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.-",
};
static const char *const grid_capitals[PC_TEXT_GRID_CHARACTER_ROWS] =
{
	"!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL\"", "ZXCVBNM<>_",
};
static const char *const grid_symbols[PC_TEXT_GRID_CHARACTER_ROWS] =
{
	"1234567890", "!@#$%^&*()", "-_=+[]{};:", "'\"`~\\|/?<>",
};

/* ---------- the text */

int pc_text_entry_filter(char *out, int size, const char *utf8)
{
	const unsigned char *text = (const unsigned char *)(utf8 ? utf8 : "");
	int length = 0;

	if (size <= 0)
		return 0;
	while (*text)
	{
		int extra, index;

		if (*text < 0x80)
		{
			if (*text >= 0x20 && *text <= 0x7E && length < size - 1)
				out[length++] = (char)*text;
			text++;
			continue;
		}
		/* (a character of several bytes: skipped whole; a byte that
		starts none, alone) */
		extra = (*text & 0xE0) == 0xC0 ? 1 : (*text & 0xF0) == 0xE0 ? 2 : (*text & 0xF8) == 0xF0 ? 3 : 0;
		text++;
		for (index = 0; index < extra && (*text & 0xC0) == 0x80; index++)
			text++;
	}
	out[length] = 0;
	return length;
}

/* under the lock: printable ASCII put in at the caret, as much as fits */
static void insert(const char *ascii)
{
	for (; *ascii; ascii++)
	{
		if (entry.length >= entry.maximum)
		{
			entry.refused_ms = entry.now_ms ? entry.now_ms : 1;
			return;
		}
		memmove(entry.text + entry.caret + 1, entry.text + entry.caret, (size_t)(entry.length - entry.caret + 1));
		entry.text[entry.caret++] = *ascii;
		entry.length++;
	}
}

static void erase_before(void)
{
	if (entry.caret <= 0)
		return;
	memmove(entry.text + entry.caret - 1, entry.text + entry.caret, (size_t)(entry.length - entry.caret + 1));
	entry.caret--;
	entry.length--;
}

static void erase_after(void)
{
	if (entry.caret >= entry.length)
		return;
	memmove(entry.text + entry.caret, entry.text + entry.caret + 1, (size_t)(entry.length - entry.caret));
	entry.length--;
}

/* under the lock: the box closed, the game answered */
static void close_box(int accepted)
{
	entry.open = 0;
	entry.pad_hold = 1;
	if (accepted)
	{
		snprintf(halo_text_input_text, sizeof(halo_text_input_text), "%s", entry.text);
		if (entry.password)
			platform_log("text entry: \"%s\": done (a password of %d characters)", entry.title, entry.length);
		else
			platform_log("text entry: \"%s\": done: \"%s\"", entry.title, entry.text);
	}
	else
	{
		platform_log("text entry: \"%s\": cancelled", entry.title);
	}
	__atomic_store_n(&halo_text_input_state, accepted ? HALO_TEXT_INPUT_DONE : HALO_TEXT_INPUT_CANCELLED,
		__ATOMIC_RELEASE);
}

/* ---------- the game's side */

void pc_text_entry_request(void)
{
	__atomic_store_n(&halo_text_input_state, HALO_TEXT_INPUT_REQUESTED, __ATOMIC_RELEASE);
}

int pc_text_entry_frame(unsigned long now_ms, int gamepad_used_last)
{
	int opened = 0;

	if (__atomic_load_n(&halo_text_input_state, __ATOMIC_ACQUIRE) != HALO_TEXT_INPUT_REQUESTED)
	{
		pthread_mutex_lock(&entry_lock);
		entry.now_ms = now_ms;
		pthread_mutex_unlock(&entry_lock);
		return 0;
	}
	pthread_mutex_lock(&entry_lock);
	halo_text_input_title[HALO_TEXT_INPUT_SIZE - 1] = 0;
	halo_text_input_text[HALO_TEXT_INPUT_SIZE - 1] = 0;
	snprintf(entry.title, sizeof(entry.title), "%s", halo_text_input_title);
	entry.maximum = halo_text_input_maximum > 0 && halo_text_input_maximum < PC_TEXT_TEXT_SIZE ? halo_text_input_maximum :
		PC_TEXT_TEXT_SIZE - 1;
	entry.length = pc_text_entry_filter(entry.text, entry.maximum + 1, halo_text_input_text);
	entry.caret = entry.length;
	entry.password = halo_text_input_password != 0;
	entry.grid = gamepad_used_last != 0;
	entry.row = 1;
	entry.column = 0;
	entry.shift = 0;
	entry.symbols = 0;
	entry.now_ms = now_ms;
	entry.opened_ms = now_ms ? now_ms : 1;
	entry.refused_ms = 0;
	entry.pad_fresh = 1;
	entry.pad_hold = 0;
	entry.open = 1;
	opened = 1;
	platform_log("text entry: \"%s\" opened (up to %d characters%s%s)", entry.title, entry.maximum,
		entry.password ? ", a password" : "", entry.grid ? ", the on-screen keyboard shown" : "");
	__atomic_store_n(&halo_text_input_state, HALO_TEXT_INPUT_OPEN, __ATOMIC_RELEASE);
	pthread_mutex_unlock(&entry_lock);
	return opened;
}

int pc_text_entry_active(void)
{
	int open;

	pthread_mutex_lock(&entry_lock);
	open = entry.open;
	pthread_mutex_unlock(&entry_lock);
	return open;
}

int pc_text_entry_holds_pad(unsigned int pad_buttons)
{
	int hold;

	pthread_mutex_lock(&entry_lock);
	if (!entry.open && entry.pad_hold && !pad_buttons)
		entry.pad_hold = 0;
	hold = entry.open || entry.pad_hold;
	pthread_mutex_unlock(&entry_lock);
	return hold;
}

/* ---------- the keyboard */

void pc_text_entry_type(const char *utf8)
{
	char ascii[PC_TEXT_TEXT_SIZE * 4];

	pc_text_entry_filter(ascii, (int)sizeof(ascii), utf8);
	pthread_mutex_lock(&entry_lock);
	if (entry.open)
		insert(ascii);
	pthread_mutex_unlock(&entry_lock);
}

void pc_text_entry_key(int key, int repeat)
{
	pthread_mutex_lock(&entry_lock);
	if (!entry.open)
	{
		pthread_mutex_unlock(&entry_lock);
		return;
	}
	switch (key)
	{
	case PC_TEXT_KEY_BACKSPACE:
		erase_before();
		break;
	case PC_TEXT_KEY_DELETE:
		erase_after();
		break;
	case PC_TEXT_KEY_LEFT:
		if (entry.caret > 0)
			entry.caret--;
		break;
	case PC_TEXT_KEY_RIGHT:
		if (entry.caret < entry.length)
			entry.caret++;
		break;
	case PC_TEXT_KEY_HOME:
		entry.caret = 0;
		break;
	case PC_TEXT_KEY_END:
		entry.caret = entry.length;
		break;
	case PC_TEXT_KEY_ACCEPT:
	case PC_TEXT_KEY_CANCEL:
		/* (a held key's repeats neither: the Enter held from the menu
		that asked must be pressed again) */
		if (!repeat)
			close_box(key == PC_TEXT_KEY_ACCEPT);
		break;
	default:
		break;
	}
	pthread_mutex_unlock(&entry_lock);
}

/* ---------- the gamepads and the grid */

char pc_text_entry_grid_character(int symbols, int shift, int row, int column)
{
	const char *const *rows = symbols ? grid_symbols : shift ? grid_capitals : grid_letters;

	if (row < 0 || row >= PC_TEXT_GRID_CHARACTER_ROWS || column < 0 || column >= PC_TEXT_GRID_COLUMNS)
		return 0;
	return rows[row][column];
}

/* under the lock: the cursor moved a step (the action row's keys two
columns wide), round at the edges */
static void grid_move(unsigned int direction)
{
	int step = entry.row == PC_TEXT_GRID_CHARACTER_ROWS ? 2 : 1;

	if (direction & PC_TEXT_PAD_UP)
		entry.row = (entry.row + PC_TEXT_GRID_ROWS - 1) % PC_TEXT_GRID_ROWS;
	else if (direction & PC_TEXT_PAD_DOWN)
		entry.row = (entry.row + 1) % PC_TEXT_GRID_ROWS;
	else if (direction & PC_TEXT_PAD_LEFT)
		entry.column = (entry.column + PC_TEXT_GRID_COLUMNS - step) % PC_TEXT_GRID_COLUMNS;
	else if (direction & PC_TEXT_PAD_RIGHT)
		entry.column = (entry.column + step) % PC_TEXT_GRID_COLUMNS;
	if (entry.row == PC_TEXT_GRID_CHARACTER_ROWS)
		entry.column &= ~1;
}

/* under the lock: A on the key chosen */
static void grid_press(void)
{
	char text[2] = { 0, 0 };

	if (entry.row < PC_TEXT_GRID_CHARACTER_ROWS)
	{
		text[0] = pc_text_entry_grid_character(entry.symbols, entry.shift, entry.row, entry.column);
		insert(text);
		return;
	}
	switch (entry.column / 2)
	{
	case PC_TEXT_ACTION_SHIFT:
		entry.shift = !entry.shift;
		entry.symbols = 0;
		break;
	case PC_TEXT_ACTION_SYMBOLS:
		entry.symbols = !entry.symbols;
		break;
	case PC_TEXT_ACTION_SPACE:
		insert(" ");
		break;
	case PC_TEXT_ACTION_DELETE:
		erase_before();
		break;
	case PC_TEXT_ACTION_DONE:
		close_box(1);
		break;
	default:
		break;
	}
}

void pc_text_entry_pad(unsigned int buttons, int stick_x, int stick_y, unsigned long now_ms)
{
	unsigned int pressed, directions;

	if (stick_y > STICK_THRESHOLD)
		buttons |= PC_TEXT_PAD_UP;
	else if (stick_y < -STICK_THRESHOLD)
		buttons |= PC_TEXT_PAD_DOWN;
	if (stick_x < -STICK_THRESHOLD)
		buttons |= PC_TEXT_PAD_LEFT;
	else if (stick_x > STICK_THRESHOLD)
		buttons |= PC_TEXT_PAD_RIGHT;
	pthread_mutex_lock(&entry_lock);
	entry.now_ms = now_ms;
	if (!entry.open)
	{
		pthread_mutex_unlock(&entry_lock);
		return;
	}
	if (entry.pad_fresh)
	{
		entry.pad_fresh = 0;
		entry.pad_previous = buttons;
		pthread_mutex_unlock(&entry_lock);
		return;
	}
	pressed = buttons & ~entry.pad_previous;
	entry.pad_previous = buttons;
	/* (a gamepad used: the grid shown, the press that shows it taken as
	well) */
	if (pressed && !entry.grid)
	{
		entry.grid = 1;
		platform_log("text entry: the on-screen keyboard shown (a gamepad used)");
	}
	directions = buttons & (PC_TEXT_PAD_UP | PC_TEXT_PAD_DOWN | PC_TEXT_PAD_LEFT | PC_TEXT_PAD_RIGHT);
	if (pressed & directions)
	{
		grid_move(pressed & directions);
		entry.repeat_ms = now_ms + REPEAT_DELAY_MS;
	}
	else if (directions && (long)(now_ms - entry.repeat_ms) >= 0)
	{
		grid_move(directions);
		entry.repeat_ms = now_ms + REPEAT_RATE_MS;
	}
	if (pressed & PC_TEXT_PAD_A)
		grid_press();
	if (entry.open && (pressed & PC_TEXT_PAD_B))
		erase_before();
	if (entry.open && (pressed & PC_TEXT_PAD_X))
		insert(" ");
	if (entry.open && (pressed & PC_TEXT_PAD_WHITE))
	{
		entry.shift = !entry.shift;
		entry.symbols = 0;
	}
	if (entry.open && (pressed & PC_TEXT_PAD_BLACK))
		entry.symbols = !entry.symbols;
	if (entry.open && (pressed & PC_TEXT_PAD_LEFT_TRIGGER) && entry.caret > 0)
		entry.caret--;
	if (entry.open && (pressed & PC_TEXT_PAD_RIGHT_TRIGGER) && entry.caret < entry.length)
		entry.caret++;
	if (entry.open && (pressed & PC_TEXT_PAD_START))
		close_box(1);
	else if (entry.open && (pressed & PC_TEXT_PAD_Y))
		close_box(0);
	pthread_mutex_unlock(&entry_lock);
}

/* ---------- the picture */

void pc_text_entry_view(struct pc_text_entry_view *view)
{
	int index;

	pthread_mutex_lock(&entry_lock);
	memset(view, 0, sizeof(*view));
	view->open = entry.open;
	if (entry.open)
	{
		snprintf(view->title, sizeof(view->title), "%s", entry.title);
		for (index = 0; index < entry.length; index++)
			view->shown[index] = entry.password ? '*' : entry.text[index];
		view->shown[entry.length] = 0;
		view->length = entry.length;
		view->caret = entry.caret;
		view->maximum = entry.maximum;
		view->password = entry.password;
		view->grid = entry.grid;
		view->row = entry.row;
		view->column = entry.column;
		view->shift = entry.shift;
		view->symbols = entry.symbols;
		view->opened_ms = entry.opened_ms;
		view->refused_ms = entry.refused_ms;
	}
	pthread_mutex_unlock(&entry_lock);
}

#else
/* (the Vita: its host opens the system's keyboard, vita_ime.c) */
typedef int pc_text_entry_none;
#endif
