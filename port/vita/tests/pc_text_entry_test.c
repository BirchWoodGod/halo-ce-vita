/*
PC_TEXT_ENTRY_TEST.C

The PC build's text box (port/linux/src/pc_text_entry.c) on its own, built
as a test build and as a release build (run_pc_text_entry_test.sh): a
request opens the box (never cancelled for want of a test's answers), the
keyboard's UTF-8 kept as printable ASCII, the editing keys, the most it
takes, Enter and Escape (a held key's repeats neither), the on-screen
keyboard driven by a gamepad alone (a name typed with SHIFT, B, Start; Y
cancels), the game's controller kept empty while it is open and until the
buttons that closed it are let go.
*/

#include "../../linux/src/pc_text_entry.h"
#include "../../linux/src/system_link_shortcut.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

volatile int halo_text_input_state;
char halo_text_input_title[HALO_TEXT_INPUT_SIZE];
char halo_text_input_text[HALO_TEXT_INPUT_SIZE];
int halo_text_input_maximum;
int halo_text_input_password;

static char last_log[512];
static int failures, checks;

void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(last_log, sizeof(last_log), format, arguments);
	va_end(arguments);
}

static void check(int ok, const char *what)
{
	checks++;
	if (!ok)
	{
		failures++;
		printf("FAIL: %s\n", what);
	}
}

static unsigned long now = 1000;

/* the game asks, as menu_functions.c typing_begin does */
static void ask(const char *title, const char *text, int maximum, int password)
{
	snprintf(halo_text_input_title, sizeof(halo_text_input_title), "%s", title);
	snprintf(halo_text_input_text, sizeof(halo_text_input_text), "%s", text);
	halo_text_input_maximum = maximum;
	halo_text_input_password = password;
	pc_text_entry_request();
}

/* a controller read with these buttons held */
static void pad(unsigned int buttons)
{
	now += 33;
	pc_text_entry_frame(now, 1);
	pc_text_entry_pad(buttons, 0, 0, now);
}

/* a button pressed and let go */
static void press(unsigned int button)
{
	pad(button);
	pad(0);
}

static void test_filter(void)
{
	char out[64];

	check(pc_text_entry_filter(out, sizeof(out), "Rookie") == 6 && !strcmp(out, "Rookie"), "ASCII kept");
	check(pc_text_entry_filter(out, sizeof(out), "Jos\xC3\xA9 \xE2\x82\xAC!") == 5 && !strcmp(out, "Jos !"),
		"an accented letter and the euro sign left out whole");
	check(pc_text_entry_filter(out, sizeof(out), "a\xF0\x9F\x98\x80" "b") == 2 && !strcmp(out, "ab"), "an emoji left out whole");
	check(pc_text_entry_filter(out, sizeof(out), "a\tb\nc\x7F") == 3 && !strcmp(out, "abc"), "control characters left out");
	check(pc_text_entry_filter(out, sizeof(out), "x\xC3") == 1 && !strcmp(out, "x"), "a sequence cut short left out");
	check(pc_text_entry_filter(out, 4, "abcdef") == 3 && !strcmp(out, "abc"), "no more than the room");
}

static void test_keyboard(void)
{
	struct pc_text_entry_view view;

	ask("New profile's name", "", 11, 0);
	check(halo_text_input_state == HALO_TEXT_INPUT_REQUESTED, "asked: REQUESTED, not cancelled (a release build too)");
	check(!pc_text_entry_active(), "not open before the game's next controller read");
	check(pc_text_entry_frame(now, 0) == 1 && pc_text_entry_active() && halo_text_input_state == HALO_TEXT_INPUT_OPEN,
		"the next read opens it: OPEN");
	pc_text_entry_view(&view);
	check(view.open && !view.grid && view.maximum == 11 && !strcmp(view.title, "New profile's name"),
		"opened on the title and length asked, no on-screen keyboard after the keyboard");
	check(pc_text_entry_holds_pad(0), "the game's controller empty while it is open");
	pc_text_entry_type("Ro\xC3\xA9");
	pc_text_entry_type("kie");
	pc_text_entry_view(&view);
	check(!strcmp(view.shown, "Rokie") && view.caret == 5, "typed, the accented letter left out");
	pc_text_entry_key(PC_TEXT_KEY_LEFT, 0);
	pc_text_entry_key(PC_TEXT_KEY_LEFT, 0);
	pc_text_entry_key(PC_TEXT_KEY_LEFT, 0);
	pc_text_entry_type("o");
	pc_text_entry_view(&view);
	check(!strcmp(view.shown, "Rookie") && view.caret == 3, "Left moves the caret, typing goes in there");
	pc_text_entry_key(PC_TEXT_KEY_HOME, 0);
	pc_text_entry_key(PC_TEXT_KEY_DELETE, 0);
	pc_text_entry_type("r");
	pc_text_entry_key(PC_TEXT_KEY_END, 0);
	pc_text_entry_key(PC_TEXT_KEY_BACKSPACE, 0);
	pc_text_entry_type("E");
	pc_text_entry_view(&view);
	check(!strcmp(view.shown, "rookiE") && view.caret == 6, "Home, Delete, End, Backspace");
	pc_text_entry_type("XXXXXXXXXX");
	pc_text_entry_view(&view);
	check(view.length == 11 && !strcmp(view.shown, "rookiEXXXXX") && view.refused_ms != 0,
		"no more than 11 characters, the rest refused (the count flashes)");
	pc_text_entry_key(PC_TEXT_KEY_ACCEPT, 1);
	check(pc_text_entry_active(), "a held Enter's repeat does not accept");
	pc_text_entry_key(PC_TEXT_KEY_ACCEPT, 0);
	check(!pc_text_entry_active() && halo_text_input_state == HALO_TEXT_INPUT_DONE &&
		!strcmp(halo_text_input_text, "rookiEXXXXX"), "Enter: DONE, the text given to the game");
	check(!pc_text_entry_holds_pad(0), "closed by the keyboard: the controller the game's again");

	ask("Password", "", 16, 1);
	pc_text_entry_frame(now, 0);
	pc_text_entry_type("secret");
	pc_text_entry_view(&view);
	check(!strcmp(view.shown, "******") && view.password, "a password shown as stars");
	pc_text_entry_key(PC_TEXT_KEY_ACCEPT, 0);
	check(!strcmp(halo_text_input_text, "secret") && !strstr(last_log, "secret"), "a password given, never logged");

	ask("Lobby name", "Halo", 15, 0);
	pc_text_entry_frame(now, 0);
	pc_text_entry_view(&view);
	check(!strcmp(view.shown, "Halo") && view.caret == 4, "opened on the text asked, the caret after it");
	pc_text_entry_key(PC_TEXT_KEY_CANCEL, 1);
	check(pc_text_entry_active(), "a held Escape's repeat does not cancel");
	pc_text_entry_key(PC_TEXT_KEY_CANCEL, 0);
	check(!pc_text_entry_active() && halo_text_input_state == HALO_TEXT_INPUT_CANCELLED, "Escape: CANCELLED");
}

static void test_gamepad(void)
{
	struct pc_text_entry_view view;
	int index;

	/* (A held as it opens - the press that asked for it - types nothing) */
	ask("New profile's name", "", 11, 0);
	pad(PC_TEXT_PAD_A);
	pad(PC_TEXT_PAD_A);
	pc_text_entry_view(&view);
	check(view.open && view.grid && view.length == 0, "the gamepad: the grid shown, the A held from the menu types nothing");
	pad(0);
	check(view.row == 1 && view.column == 0 && pc_text_entry_grid_character(0, 0, 1, 0) == 'q', "the grid starts on q");
	/* (SHIFT, R; SHIFT off, o o; k; i; e: "Rookie"; an extra q and B) */
	press(PC_TEXT_PAD_WHITE);
	for (index = 0; index < 3; index++)
		press(PC_TEXT_PAD_RIGHT);
	press(PC_TEXT_PAD_A);
	press(PC_TEXT_PAD_WHITE);
	for (index = 0; index < 5; index++)
		press(PC_TEXT_PAD_RIGHT);
	press(PC_TEXT_PAD_A);
	press(PC_TEXT_PAD_A);
	press(PC_TEXT_PAD_DOWN);
	press(PC_TEXT_PAD_LEFT);
	press(PC_TEXT_PAD_A);
	press(PC_TEXT_PAD_UP);
	press(PC_TEXT_PAD_A);
	for (index = 0; index < 5; index++)
		press(PC_TEXT_PAD_LEFT);
	press(PC_TEXT_PAD_A);
	press(PC_TEXT_PAD_A);
	press(PC_TEXT_PAD_B);
	pc_text_entry_view(&view);
	check(!strcmp(view.shown, "Rookie"), "typed on the grid with SHIFT, B deleting");
	/* (the action row, its keys two columns wide: down to #+=, right to
	SPACE, then DELETE) */
	press(PC_TEXT_PAD_DOWN);
	press(PC_TEXT_PAD_DOWN);
	press(PC_TEXT_PAD_DOWN);
	pc_text_entry_view(&view);
	check(view.row == 4 && view.column == 2, "down to the action row: #+=");
	press(PC_TEXT_PAD_RIGHT);
	press(PC_TEXT_PAD_A);
	pc_text_entry_view(&view);
	check(!strcmp(view.shown, "Rookie "), "SPACE");
	press(PC_TEXT_PAD_RIGHT);
	press(PC_TEXT_PAD_A);
	pc_text_entry_view(&view);
	check(!strcmp(view.shown, "Rookie") && view.column == 6, "DELETE");
	/* (held right: repeats after a while, round the row) */
	now += 33;
	pc_text_entry_pad(PC_TEXT_PAD_RIGHT, 0, 0, now);
	for (index = 0; index < 20; index++)
	{
		now += 33;
		pc_text_entry_pad(PC_TEXT_PAD_RIGHT, 0, 0, now);
	}
	pc_text_entry_view(&view);
	check(view.column != 8 && view.column != 6, "a held direction repeats");
	pad(0);
	/* (the left stick moves as the D-pad) */
	now += 33;
	pc_text_entry_pad(0, 0, 30000, now);
	pc_text_entry_view(&view);
	check(view.row == 3, "the left stick moves");
	pad(0);
	pad(PC_TEXT_PAD_START);
	check(!pc_text_entry_active() && halo_text_input_state == HALO_TEXT_INPUT_DONE && !strcmp(halo_text_input_text, "Rookie"),
		"Start: DONE, the name given");
	check(pc_text_entry_holds_pad(PC_TEXT_PAD_START), "Start still held: kept from the game");
	check(!pc_text_entry_holds_pad(0), "let go: the controller the game's again");

	ask("Friend code", "", 14, 0);
	pad(0);
	press(PC_TEXT_PAD_A);
	press(PC_TEXT_PAD_Y);
	check(!pc_text_entry_active() && halo_text_input_state == HALO_TEXT_INPUT_CANCELLED, "Y: CANCELLED");
}

int main(void)
{
	test_filter();
	test_keyboard();
	test_gamepad();
#ifdef HALO_RELEASE
	printf("(a release build)\n");
#endif
	printf("%d of %d checks passed\n", checks - failures, checks);
	return failures ? 1 : 0;
}
