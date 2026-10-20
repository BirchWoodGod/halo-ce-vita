/*
PC_TEXT_ENTRY.H

The PC build's text box (pc_text_entry.c): what the Vita's system keyboard
does there (port/vita/host/vita_ime.c), for every line of text the game's
menus ask for (system_link_shortcut.h halo_text_input_*: a new profile's
name, a password, a code, a lobby's name, the party's chat...).

The game asks (halo_text_input_state REQUESTED); the next controller read
(xinput_sdl.c, the game's thread) opens the box on the asked title, text,
length and masking. While it is open:
- the keyboard types into it (SDL's text input: sdl_platform.c gives this
  file SDL_EVENT_TEXT_INPUT's text and the editing keys) and the game sees
  none of those keys, nor the mouse's clicks;
- the gamepads drive an on-screen keyboard (a grid of keys) and the game
  gets no buttons from them.
Enter or the grid's DONE answers DONE with the text, Escape or Y CANCELLED.
The box is drawn by pc_text_entry_overlay.c over everything (interface.c's
overlays). None of it on the Vita, whose host opens the system's keyboard.
*/

#ifndef PC_TEXT_ENTRY_H
#define PC_TEXT_ENTRY_H

#ifdef __cplusplus
extern "C" {
#endif

/* the grid: PC_TEXT_GRID_COLUMNS keys in each of its rows of characters,
then the row of its actions, each two columns wide */
enum
{
	PC_TEXT_GRID_COLUMNS = 10,
	PC_TEXT_GRID_CHARACTER_ROWS = 4,
	PC_TEXT_GRID_ROWS = PC_TEXT_GRID_CHARACTER_ROWS + 1,
	PC_TEXT_TEXT_SIZE = 128,
};

/* the action row's keys (column / 2) */
enum pc_text_grid_action
{
	PC_TEXT_ACTION_SHIFT,
	PC_TEXT_ACTION_SYMBOLS,
	PC_TEXT_ACTION_SPACE,
	PC_TEXT_ACTION_DELETE,
	PC_TEXT_ACTION_DONE,
	PC_TEXT_ACTION_COUNT
};

/* the editing keys of the keyboard (sdl_platform.c) */
enum pc_text_key
{
	PC_TEXT_KEY_BACKSPACE,
	PC_TEXT_KEY_DELETE,
	PC_TEXT_KEY_LEFT,
	PC_TEXT_KEY_RIGHT,
	PC_TEXT_KEY_HOME,
	PC_TEXT_KEY_END,
	PC_TEXT_KEY_ACCEPT,
	PC_TEXT_KEY_CANCEL,
};

/* a gamepad's buttons, for pc_text_entry_pad */
enum
{
	PC_TEXT_PAD_UP = 1 << 0,
	PC_TEXT_PAD_DOWN = 1 << 1,
	PC_TEXT_PAD_LEFT = 1 << 2,
	PC_TEXT_PAD_RIGHT = 1 << 3,
	PC_TEXT_PAD_A = 1 << 4,
	PC_TEXT_PAD_B = 1 << 5,
	PC_TEXT_PAD_X = 1 << 6,
	PC_TEXT_PAD_Y = 1 << 7,
	PC_TEXT_PAD_START = 1 << 8,
	PC_TEXT_PAD_BACK = 1 << 9,
	/* (the Duke's white and black: the left and right shoulders) */
	PC_TEXT_PAD_WHITE = 1 << 10,
	PC_TEXT_PAD_BLACK = 1 << 11,
	PC_TEXT_PAD_LEFT_TRIGGER = 1 << 12,
	PC_TEXT_PAD_RIGHT_TRIGGER = 1 << 13,
	PC_TEXT_PAD_STICK_CLICKS = 1 << 14,
};

/* what the box shows (pc_text_entry_overlay.c) */
struct pc_text_entry_view
{
	int open;
	/* the title (UTF-8: a translation), the text as shown (stars for a
	password), its length and the caret's place in it, the most it takes */
	char title[PC_TEXT_TEXT_SIZE];
	char shown[PC_TEXT_TEXT_SIZE];
	int length;
	int caret;
	int maximum;
	int password;
	/* the on-screen keyboard: shown (a gamepad used), the key chosen,
	capitals, the symbols' page */
	int grid;
	int row;
	int column;
	int shift;
	int symbols;
	/* when it opened, and when a character last did not fit (the count
	flashes) */
	unsigned long opened_ms;
	unsigned long refused_ms;
};

/* (the game's menus, menu_functions.c typing_begin, once halo_text_input_*
are set) the box asked for: REQUESTED */
void pc_text_entry_request(void);
/* (the game's thread, each controller read) a request taken up: the box
opened (SDL's text input started by the caller when this says so: 1 the
box opened now) */
int pc_text_entry_frame(unsigned long now_ms, int gamepad_used_last);
/* whether the box is open */
int pc_text_entry_active(void);
/* whether the game's controller must stay empty: the box open, or closed
while the buttons that closed it (pad_buttons, held now) are still down */
int pc_text_entry_holds_pad(unsigned int pad_buttons);
/* the keyboard's text (UTF-8, SDL_EVENT_TEXT_INPUT, a paste): what the
game's text keeps (printable ASCII) put in at the caret, as much as fits */
void pc_text_entry_type(const char *utf8);
/* one of the editing keys pressed (repeat: a held key's repeat, which
neither accepts nor cancels) */
void pc_text_entry_key(int key, int repeat);
/* the gamepads' buttons held now (PC_TEXT_PAD_*) and the left stick
(-32768..32767, up positive), each read */
void pc_text_entry_pad(unsigned int buttons, int stick_x, int stick_y, unsigned long now_ms);
/* what the box shows */
void pc_text_entry_view(struct pc_text_entry_view *view);
/* the grid's character at a place (0 for the action row or none) */
char pc_text_entry_grid_character(int symbols, int shift, int row, int column);

/* (pure, for the tests too) UTF-8 text as the game keeps it: each
character printable ASCII kept, every other (a letter with an accent, an
emoji, a control character, a broken sequence) left out; at most size - 1
bytes; the count kept */
int pc_text_entry_filter(char *out, int size, const char *utf8);

#ifdef __cplusplus
}
#endif

#endif
