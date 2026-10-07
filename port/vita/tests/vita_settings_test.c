/*
VITA_SETTINGS_TEST.C

A desktop test of the settings panel (port/vita/host/vita_settings.c,
included whole), with the calls it makes into internet play (p2p.h), ad hoc
play (vita_net.c) and the renderer (vgxm_menu_set) recorded instead: the
panel opened with SELECT+START, its four tabs (and Dev) switched with L and
R, no page longer than a screen (ten rows on a tab at most, the values in
a column, a help line and a line of the panel's buttons), the pages a row
opens and circle going back; the Multiplayer tab and its Play page (the
rows each Connection has, Host a game's steps, the lobby name and password
typed on the system's keyboard and kept as printable ASCII, Max players,
Visibility at once, Host co-op campaign asking the game for its Campaign
screen, a code typed with the D-pad and joined, the public lobby listed and
joined, Games on this network, an ad hoc group joined first, the game
seeing no buttons while a system dialog is up; 1.0.3's co-op choice loading
as Off), its Modded
maps page (a folder of fake maps: listed, turned off and on, deleted after
a confirmation, the map in play kept; Map downloads' three choices), the
Dev tab behind its switch (switches saved only while on, the timing
variables, Save report's folder),
Controls' pages (Button layout and Touch zones in Xbox terms, Gyro
settings with the gyroscope's line, saved and read back), Button icons
(PlayStation at once, the panel then naming the Xbox buttons by what they
do and the guide the Vita's buttons, a remap followed by the game's icon,
kept by Reset controls), every settings
variable of 1.0.3 still a row, and a settings.txt of 1.0 loading. It runs
in a folder of its own, with ux0:data/haloce-vita made there.

Run port/vita/tests/run_vita_settings_test.sh.
*/

#include "../host/vita_settings.c"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

volatile unsigned long halo_settings_generation;
/* (the game's side of Host a game and Join a game: system_link_shortcut.c) */
volatile int halo_system_link_request;
volatile int halo_system_link_answer;
volatile int halo_multiplayer_status[SYSTEM_LINK_STATUS_COUNT];
int (*halo_test_setting_hook)(const char *variable, const char *value);
int halo_screen_restart_needed(void) { return 0; }
/* (the map in play: never deleted) */
int halo_cache_map_in_use(const char *name) { return !strcmp(name, "inplay"); }
static char log_text[16384];
static int overlay_level = -1;

/* (vita_input.c's) */
void vita_gyro_status(char *text, int size) { snprintf(text, (size_t)size, "Gyro: yaw -999 pitch -999 roll -999 lay still"); }

int vita_host_thread_start(const char *name, void (*function)(void *), void *argument, int core)
{
	(void)name;
	(void)core;
	function(argument);
	return 0;
}

int sceRtcGetCurrentClockLocalTime(SceDateTime *time)
{
	time->year = 2026;
	time->month = 10;
	time->day = 6;
	time->hour = 15;
	time->minute = 30;
	time->second = 12;
	return 0;
}

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

/* ---------- what the panel calls, recorded */

static unsigned long long clock_us = 1000000;
static char menu[2048];
static int menu_selected, menu_visible;
static char joined_code[32];
static int joined_from_lobby;
static int lobby_public = -1, browsing, adhoc_connects, adhoc_mode = -1, adhoc_room = -1, adhoc_state_value;
static char lobby_name[64];
#ifdef PLAY_UPSTREAM_BROWSER
static char lobby_password[64], joined_password[64];
#endif
static int ime_opens, ime_open_now, ime_result, ime_maximum, ime_password;
static char ime_title[64], ime_initial[64], ime_typed[128];
static int hosting;

SceUInt64 sceKernelGetProcessTimeWide(void) { return clock_us; }

int sceAppUtilSystemParamGetString(unsigned int paramId, SceChar8 *buf, SceSize bufSize)
{
	(void)paramId;
	snprintf((char *)buf, bufSize, "vitauser");
	return 0;
}

void vgxm_menu_set(const char *text, int selected)
{
	menu_visible = text != NULL;
	snprintf(menu, sizeof(menu), "%s", text ? text : "");
	menu_selected = selected;
}

void vgxm_overlay_enable(int enabled) { overlay_level = enabled; }
void vgxm_upscale_filter_set(int filter) { (void)filter; }
void vita_host_log(const char *line)
{
	size_t used = strlen(log_text);

	snprintf(log_text + used, sizeof(log_text) - used, "%s\n", line);
}

int p2p_join_code(const char *code)
{
	snprintf(joined_code, sizeof(joined_code), "%s", code);
	return 1;
}

#ifndef PLAY_UPSTREAM_BROWSER
/* (a public game's: by its code, for the host it is listed under) */
static char joined_host[16];

int p2p_join_lobby_entry(const struct p2p_lobby_entry *entry)
{
	snprintf(joined_host, sizeof(joined_host), "%s", entry->host);
	joined_from_lobby = 1;
	return p2p_join_code(entry->code);
}
#endif

int p2p_hosting_code(char *code, int size)
{
	if (!hosting)
		return 0;
	snprintf(code, (size_t)size, "QX7K-M2PA");
	return 1;
}

void p2p_lobby_set_public(int listed) { lobby_public = listed; }
void p2p_lobby_browse(int on) { browsing = on; }
void p2p_lobby_set_name(const char *name) { snprintf(lobby_name, sizeof(lobby_name), "%s", name ? name : ""); }

#ifdef PLAY_UPSTREAM_BROWSER
/* (OpenCE's server browser, ported: entries by id, with a password) */
void p2p_lobby_set_password(const char *password)
{
	snprintf(lobby_password, sizeof(lobby_password), "%s", password ? password : "");
}

int p2p_lobby_join(const char *id, const char *password)
{
	snprintf(joined_code, sizeof(joined_code), "%s", id);
	snprintf(joined_password, sizeof(joined_password), "%s", password ? password : "");
	joined_from_lobby = 1;
	return 1;
}

int p2p_lobby_join_state(void) { return P2P_LOBBY_JOIN_IDLE; }

int p2p_lobby_entry(int index, struct p2p_lobby_entry *entry)
{
	if (!browsing || index >= 3)
		return 0;
	memset(entry, 0, sizeof(*entry));
	snprintf(entry->id, sizeof(entry->id), "%s", index == 0 ? "0wn" : index == 1 ? "a1b2" : "c3d4");
	snprintf(entry->name, sizeof(entry->name), "%s", index == 0 ? "this vita" : index == 1 ? "desktop host" : "locked game");
	snprintf(entry->map, sizeof(entry->map), "%s", index == 2 ? "Wizard" : "Blood Gulch");
	snprintf(entry->rules, sizeof(entry->rules), "%s", "Slayer to 50 on Blood Gulch");
	snprintf(entry->players_line, sizeof(entry->players_line), "%s", "2 of 16: Alpha, Bravo");
	entry->players = 2;
	entry->maximum = 16;
	entry->compatible = 1;
	entry->own = index == 0;
	entry->locked = index == 2;
	return 1;
}
#else
int p2p_lobby_entry(int index, struct p2p_lobby_entry *entry)
{
	static const struct p2p_lobby_entry entries[] = {
		{ "OWNN-GAME", "this vita", 1, 128, 1, 1, "0200000000aa" },
		{ "HJ4T-9WXZ", "desktop host", 2, 16, 1, 0, "0211223344bb" },
	};

	if (!browsing || index >= 2)
		return 0;
	*entry = entries[index];
	return 1;
}
#endif

/* (vita_ime.c's keyboard: opened, then closed with ime_result and
ime_typed once a test says so) */
int vita_ime_open(const char *title, const char *text, int maximum_length, int password)
{
	ime_opens++;
	ime_open_now = 1;
	ime_result = 0;
	snprintf(ime_title, sizeof(ime_title), "%s", title);
	snprintf(ime_initial, sizeof(ime_initial), "%s", text);
	ime_maximum = maximum_length;
	ime_password = password;
	return 0;
}

int vita_ime_poll(char *text, int size)
{
	if (!ime_open_now)
		return -1;
	if (!ime_result)
		return 0;
	ime_open_now = 0;
	if (ime_result > 0)
		snprintf(text, (size_t)size, "%s", ime_typed);
	return ime_result;
}

int p2p_status(char *text, int size)
{
	snprintf(text, (size_t)size, "ready");
	return 1;
}

int p2p_adhoc_status(char *text, int size)
{
	snprintf(text, (size_t)size, "in the group, 1 other machine");
	return 1;
}

int vita_adhoc_connect(int mode, int room)
{
	adhoc_connects++;
	adhoc_mode = mode;
	adhoc_room = room;
	adhoc_state_value = 1;
	return 0;
}

void vita_adhoc_leave(void) { adhoc_state_value = 0; }

int vita_adhoc_state(char *text, int size)
{
	if (text && size > 0)
		snprintf(text, (size_t)size, "ad hoc: state %d", adhoc_state_value);
	return adhoc_state_value;
}

/* ---------- the pad */

/* one frame of the pad with these buttons held, 16 ms on */
static int frame(unsigned long buttons)
{
	struct vita_host_pad pad;

	memset(&pad, 0, sizeof(pad));
	pad.buttons = buttons;
	clock_us += 16667;
	return vita_settings_input(&pad);
}

/* a press and its release */
static void press(unsigned long button)
{
	frame(button);
	frame(0);
}

static void open_panel(void)
{
	int index;

	for (index = 0; index < 60; index++)
		frame(VITA_BUTTON_SELECT | VITA_BUTTON_START);
	frame(0);
}

/* the menu's line (0: the title) */
static const char *menu_line(int wanted, char *line, int size)
{
	const char *start = menu;
	int index;

	for (index = 0; index < wanted && start; index++)
	{
		start = strchr(start, '\n');
		if (start)
			start++;
	}
	if (!start)
		return "";
	snprintf(line, (size_t)size, "%.*s", (int)strcspn(start, "\n"), start);
	return line;
}

/* the file's text ("" if there is none) */
static const char *file_text(const char *path)
{
	static char text[4096];
	FILE *file = fopen(path, "r");
	size_t size = 0;

	if (file)
	{
		size = fread(text, 1, sizeof(text) - 1, file);
		fclose(file);
	}
	text[size] = 0;
	return text;
}

static void write_file(const char *path, const void *data, size_t size)
{
	FILE *file = fopen(path, "wb");

	fwrite(data, 1, size, file);
	fclose(file);
}

/* a fake map: the cache header's signature and version, then zeros */
static void write_map(const char *path, unsigned version, size_t size)
{
	static unsigned char bytes[300000];

	memset(bytes, 0, sizeof(bytes));
	memcpy(bytes, "daeh", 4);
	bytes[4] = version & 0xFF;
	bytes[5] = (version >> 8) & 0xFF;
	write_file(path, bytes, size < sizeof(bytes) ? size : sizeof(bytes));
}

/* L or R until the tab named is shown */
static int to_tab(const char *name)
{
	char marked[32];
	int index;

	/* (from a page a row opened: back to its tab first) */
	if (strstr(menu, "\n\x03"))
		press(VITA_BUTTON_CIRCLE);
	snprintf(marked, sizeof(marked), "*%s", name);
	for (index = 0; index < 8 && !strstr(menu, marked); index++)
		press(VITA_BUTTON_R);
	return strstr(menu, marked) != NULL && !strstr(menu, "\n\x03");
}

/* down until the selected line starts with `label` (a row's label is the
line up to its '\x02') */
static int to_line(const char *label)
{
	char line[128];
	int index;

	for (index = 0; index < 40; index++)
	{
		if (!strncmp(menu_line(menu_selected, line, sizeof(line)), label, strlen(label)))
			return 1;
		press(VITA_BUTTON_DOWN);
	}
	return 0;
}

/* ---------- the page's shape */

/* the menu's lines: how many (the zones' diagram line not counted), the
longest row label, and the diagram's marks ("" without one) */
static int menu_lines(int *longest, char *diagram, int size)
{
	const char *at = menu;
	int count = 0;

	*longest = 0;
	diagram[0] = 0;
	while (*at)
	{
		int length = (int)strcspn(at, "\n");

		if (at[0] == '\x01')
			snprintf(diagram, (size_t)size, "%.*s", length - 1, at + 1);
		else
		{
			int label = (int)strcspn(at, "\x02\n");

			/* (the tab bar is drawn apart, not as a row of text) */
			if (at[0] != '\t')
				*longest = label > *longest ? label : *longest;
			count++;
		}
		at += length + (at[length] == '\n');
	}
	return count;
}

/* the rows of the page shown (lines with a value) */
static int menu_rows(void)
{
	const char *at = menu;
	int rows = 0;

	while ((at = strchr(at, '\x02')) != NULL)
	{
		rows++;
		at++;
	}
	return rows;
}

/* every line fits: a row's label 22 characters at most and its value 24
(the value column starts 23 characters in, vita_gxm.c menu_build), the
help and the panel's buttons 70 (drawn smaller), any other line 46 */
static int menu_fits(void)
{
	const char *at = menu;

	while (*at)
	{
		int length = (int)strcspn(at, "\n");
		int label = (int)strcspn(at, "\x02\n");
		int fits;

		if (at[0] == '\t' || at[0] == '\x01')
			fits = 1;
		else if (at[0] == '\x05' || at[0] == '\x06')
			fits = length - 1 <= 70;
		else if (label < length)
			fits = label <= 22 && length - label - 1 <= 24;
		else
			fits = length - (at[0] == '\x03' || at[0] == '\x04' || at[0] == '!') <= 46;
		if (!fits)
		{
			printf("too long: %.*s\n", length, at);
			return 0;
		}
		at += length + (at[length] == '\n');
	}
	return 1;
}

/* the line that starts with `prefix` ("" if none) */
static const char *menu_find(const char *prefix, char *line, int size)
{
	const char *at = menu;

	while (*at)
	{
		int length = (int)strcspn(at, "\n");

		if (!strncmp(at, prefix, strlen(prefix)))
		{
			snprintf(line, (size_t)size, "%.*s", length, at);
			return line;
		}
		at += length + (at[length] == '\n');
	}
	line[0] = 0;
	return line;
}

/* a row opening a page: down to it, then cross */
static int open_page(const char *label)
{
	char title[64];

	if (!to_line(label))
		return 0;
	press(VITA_BUTTON_CROSS);
	snprintf(title, sizeof(title), "\n\x03");
	return strstr(menu, title) != NULL;
}

/* ---------- the Controls tab and its pages */

/* one frame with these buttons held and these touch zones */
static int frame_touch(unsigned long buttons, unsigned long touch)
{
	struct vita_host_pad pad;

	memset(&pad, 0, sizeof(pad));
	pad.buttons = buttons;
	pad.touch = touch;
	clock_us += 16667;
	return vita_settings_input(&pad);
}

static void test_controls_tab(void)
{
	struct vita_controls_config config;
	char diagram[16], line[128];
	int longest, count, index;

	/* (a 1.0 settings.txt was loaded last: no touch or button lines in it) */
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "off") && !strcmp(getenv("HALO_TOUCH_TOP_RIGHT"), "off") &&
		!strcmp(getenv("HALO_XBOX_A"), "cross") && !strcmp(getenv("HALO_XBOX_WHITE"), "right") &&
		!strcmp(getenv("HALO_XBOX_BACK"), "select"), "a 1.0 settings.txt: touch zones Off, Xbox buttons as shipped");
	open_panel();
	to_tab("Controls");
	count = menu_lines(&longest, diagram, sizeof(diagram));
	printf("%s\n--\n", menu);
	check(!strncmp(menu_line(1, line, sizeof(line)), "Look sensitivity\x02", 17) &&
		!strncmp(menu_line(2, line, sizeof(line)), "Invert look\x02", 12) &&
		!strncmp(menu_line(3, line, sizeof(line)), "Crouch\x02", 7) &&
		!strncmp(menu_line(4, line, sizeof(line)), "Gyro aiming\x02", 12) &&
		!strncmp(menu_line(5, line, sizeof(line)), "Rear touch guard\x02", 17) &&
		!strcmp(menu_line(6, line, sizeof(line)), "Button layout\x02  As shipped  >") &&
		!strcmp(menu_line(7, line, sizeof(line)), "Touch zones\x02  Off  >") &&
		!strcmp(menu_line(8, line, sizeof(line)), "Gyro settings\x02  >") &&
		!strcmp(menu_line(9, line, sizeof(line)), "Advanced\x02  >"),
		"Controls: look, invert, crouch, gyro aiming, rear guard; then Button layout, Touch zones, Gyro settings, Advanced");
	check(!strstr(menu, "\nTouch top left\x02") && !strstr(menu, "\nA\x02") && !strstr(menu, "\nGyro sensitivity\x02") &&
		!strstr(menu, "\nStick deadzone\x02") && !strstr(menu, "\nShow dev settings\x02"),
		"Controls: the zones, the buttons, the gyro's details and the rare rows on their pages");
	check(menu_rows() == 9 && count == 12 && menu_fits(), "Controls: 9 rows (and the tab bar, help, buttons), each fits");
	check(strstr(menu, "\nRear touch guard\x02< Normal >") && !strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "normal"),
		"Rear touch guard: Normal as shipped (a 1.0 settings.txt has none)");
	check(!diagram[0], "Controls: no zone diagram");
	to_line("Look sensitivity");
	check(strstr(menu, "\n\x06L/R: tabs   Left/right: change   Circle: close") != NULL,
		"the panel's buttons on one line: a value row's, on a tab");

	/* Touch zones: its page, the zones drawn beside */
	check(open_page("Touch zones") && strstr(menu, "\n\x03" "Controls > Touch zones\n"), "Touch zones opens its page");
	count = menu_lines(&longest, diagram, sizeof(diagram));
	printf("%s\n--\n", menu);
	check(strstr(menu, "\nTouch top left\x02") && strstr(menu, "\nTouch top right\x02") &&
		strstr(menu, "\nTouch left edge\x02") && strstr(menu, "\nTouch right edge\x02") &&
		strstr(menu, "\nRear touch left\x02") && strstr(menu, "\nRear touch right\x02") && menu_rows() == 6 &&
		menu_fits(), "Touch zones: the six zones");
	check(!strcmp(diagram, "s----- aaaaaa"), "Touch top left chosen: the diagram marks its zone (Off)");
	check(strstr(menu, "\n\x06L/R: tabs   Left/right: change   Circle: back") != NULL, "a page: circle goes back");
	to_line("Rear touch left");
	menu_lines(&longest, diagram, sizeof(diagram));
	check(!strcmp(diagram, "----s- aaaaaa"), "Rear touch left chosen: the diagram marks its zone (Off)");
	check(frame_touch(0, 1UL << VITA_ZONE_REAR_LEFT) == 1, "panel open: a touch zone held does not reach the game");
	press(VITA_BUTTON_RIGHT);
	menu_lines(&longest, diagram, sizeof(diagram));
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "a") && strstr(menu, "< A >") &&
		strstr(file_text(SETTINGS_FILE), "HALO_TOUCH_REAR_LEFT=a\n") && !strcmp(diagram, "----S- aaaaba"),
		"Rear touch left: A, in the environment and settings.txt; the diagram shows it set");
	for (index = 0; index < 4; index++)
		press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "y"), "Rear touch left: Y");
	to_line("Touch top right");
	for (index = 0; index < 20; index++)
		press(VITA_BUTTON_RIGHT);
	menu_lines(&longest, diagram, sizeof(diagram));
	check(!strcmp(getenv("HALO_TOUCH_TOP_RIGHT"), "back") && strstr(menu, "< Back  ") && !strcmp(diagram, "-S--A- alaaea"),
		"Touch top right: Back, the last choice; the rear zone still marked set");
	press(VITA_BUTTON_CIRCLE);
	check(menu_visible && strstr(menu, "*Controls") && !strstr(menu, "\n\x03") &&
		!strncmp(menu_line(menu_selected, line, sizeof(line)), "Touch zones\x02  2 on  >", 21),
		"circle: back to Controls, on Touch zones, which says two are on");

	/* the rear guard, on the tab */
	to_line("Rear touch guard");
	menu_lines(&longest, diagram, sizeof(diagram));
	check(!diagram[0] && strstr(menu, "Rear pad: its edges (the grip) ignored"), "Rear touch guard chosen: its help, no zone diagram");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "strong") && strstr(menu, "< Strong") &&
		strstr(file_text(SETTINGS_FILE), "HALO_TOUCH_REAR_GUARD=strong\n"), "Rear touch guard: Strong, in the environment and settings.txt");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "strong"), "Rear touch guard: Strong is the last");
	for (index = 0; index < 3; index++)
		press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "off") && strstr(menu, " Off >"), "Rear touch guard: Off, the first");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "light") && vita_rear_guard_named(getenv("HALO_TOUCH_REAR_GUARD")) ==
		VITA_REAR_GUARD_LIGHT, "Rear touch guard: Light, as the touch reader takes it");

	/* Button layout: the Xbox buttons by name */
	check(open_page("Button layout") && strstr(menu, "\n\x03" "Controls > Button layout\n"), "Button layout opens its page");
	printf("%s\n--\n", menu);
	check(strstr(menu, "\nA\x02") && strstr(menu, "\nB\x02") && strstr(menu, "\nX\x02") && strstr(menu, "\nY\x02") &&
		strstr(menu, "\nBlack\x02") && strstr(menu, "\nWhite\x02") && strstr(menu, "\nLeft trigger\x02") &&
		strstr(menu, "\nRight trigger\x02") && strstr(menu, "\nLeft stick click\x02") &&
		strstr(menu, "\nRight stick click\x02") && strstr(menu, "\nBack\x02") && menu_rows() == 12 &&
		!strstr(menu, " button") && menu_fits(), "Button layout: the eleven Xbox buttons by name, each fits");
	check(!strcmp(menu_line(2, line, sizeof(line)), "Button icons\x02  Xbox >") &&
		vita_button_icons_playstation() == 0, "Button layout: Button icons first, Xbox as shipped");
	to_line("White");
	menu_lines(&longest, diagram, sizeof(diagram));
	check(!diagram[0], "a button's row: no zone diagram");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_XBOX_WHITE"), "left") && strstr(menu, "< D-pad left >") && strstr(menu, "White: flashlight"),
		"White: D-pad left; the help line says what Halo does with it");
	vita_controls_config_load(&config);
	check(config.zone_xbox[VITA_ZONE_REAR_LEFT] == VITA_XBOX_Y && config.zone_xbox[VITA_ZONE_TOP_RIGHT] == VITA_XBOX_BACK &&
		config.xbox_button[VITA_XBOX_WHITE] == VITA_BUTTON_LEFT, "the pad reads the panel's choices");
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "\nButton layout\x02  Custom  >") != NULL, "back on Controls: Button layout says Custom");
	press(VITA_BUTTON_CIRCLE);
	check(!menu_visible && frame_touch(0, 1UL << VITA_ZONE_REAR_LEFT) == 0,
		"circle on a tab closes the panel: the touch zones reach the game");

	/* the next start: settings.txt brings them back */
	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].variable && (!strncmp(settings[index].variable, "HALO_TOUCH_", 11) ||
			!strncmp(settings[index].variable, "HALO_XBOX_", 10)))
		{
			settings[index].choice = 0;
			unsetenv(settings[index].variable);
		}
	vita_settings_load();
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "y") && !strcmp(getenv("HALO_TOUCH_TOP_RIGHT"), "back") &&
		!strcmp(getenv("HALO_TOUCH_LEFT_EDGE"), "off") && !strcmp(getenv("HALO_XBOX_WHITE"), "left") &&
		!strcmp(getenv("HALO_XBOX_RIGHT_TRIGGER"), "r") && !strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "light"),
		"settings.txt round trip: zones, the rear guard and buttons back at start-up");

	/* Advanced: the deadzone, Reset controls (Show dev settings kept) */
	open_panel();
	check(strstr(menu, "*Controls") && !strstr(menu, "\n\x03"), "the panel opens again on the tab's own page");
	check(open_page("Advanced") && strstr(menu, "\n\x03" "Controls > Advanced\n") &&
		!strncmp(menu_line(2, line, sizeof(line)), "Stick deadzone\x02", 15) &&
		!strcmp(menu_line(3, line, sizeof(line)), "Reset controls\x02  >") &&
		!strncmp(menu_line(4, line, sizeof(line)), "Show dev settings\x02", 18) && menu_rows() == 3,
		"Controls, Advanced: Stick deadzone, Reset controls, Show dev settings");
	to_line("Reset controls");
	check(strstr(menu, "\n\x06L/R: tabs   Cross: select   Circle: back") != NULL, "an action row's buttons: cross selects");
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "off") && !strcmp(getenv("HALO_TOUCH_TOP_RIGHT"), "off") &&
		!strcmp(getenv("HALO_XBOX_WHITE"), "right") && !strcmp(getenv("XV_LOOK_SENS"), "100") &&
		!strcmp(getenv("HALO_CROUCH_TOGGLE"), "1") && !strcmp(getenv("XV_INVERT_Y"), "0") &&
		!strcmp(getenv("XV_DEADZONE"), "0") &&
		strstr(file_text(SETTINGS_FILE), "HALO_TOUCH_REAR_LEFT=off\n") &&
		strstr(file_text(SETTINGS_FILE), "XV_LOOK_SENS=100\n") && strstr(menu, "\n\x05" "Controls as shipped") &&
		!strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "normal") &&
		strstr(file_text(SETTINGS_FILE), "HALO_TOUCH_REAR_GUARD=normal\n"),
		"Reset controls: zones Off, the rear guard Normal, buttons, look, crouch, deadzone as shipped, saved");
	check(!strcmp(getenv("HALO_DEV_SETTINGS"), "1") && strstr(menu, "|Dev"), "Reset controls keeps Show dev settings");
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "\nButton layout\x02  As shipped  >") && strstr(menu, "\nTouch zones\x02  Off  >"),
		"back on Controls: buttons as shipped, touch zones Off");
	press(VITA_BUTTON_CIRCLE);
}

/* ---------- gyro aiming: its row on Controls, its page */

static void test_gyro_page(void)
{
	struct vita_gyro_config config;
	char diagram[16], line[128];
	int longest, count, index;

	/* (a 1.0 settings.txt was loaded: no gyro lines in it) */
	check(!strcmp(getenv("HALO_GYRO"), "off") && !strcmp(getenv("HALO_GYRO_BUTTON"), "l") &&
		!strcmp(getenv("HALO_GYRO_SENS"), "150") && !strcmp(getenv("HALO_GYRO_INVERT_Y"), "0") &&
		!strcmp(getenv("HALO_GYRO_TURN"), "yaw"), "a 1.0 settings.txt: gyro Off, button L, 1.5x, normal, yaw");
	open_panel();
	to_tab("Controls");
	to_line("Gyro aiming");
	check(strstr(menu, "Turn the Vita to aim") != NULL, "Gyro aiming's help line");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GYRO"), "on") && strstr(file_text(SETTINGS_FILE), "HALO_GYRO=on\n"),
		"Gyro aiming On: in the environment and settings.txt");
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GYRO"), "hold") && strstr(menu, "< While holding  "), "Gyro aiming: While holding, the last");
	check(open_page("Gyro settings"), "Gyro settings opens its page");
	count = menu_lines(&longest, diagram, sizeof(diagram));
	printf("%s\n--\n", menu);
	check(!strcmp(menu_line(1, line, sizeof(line)), "\x03" "Controls > Gyro settings") &&
		strstr(menu_line(2, line, sizeof(line)), "Gyro button\x02") && strstr(line, "< L >") &&
		strstr(menu_line(3, line, sizeof(line)), "Gyro sensitivity\x02") && strstr(line, "< 1.5x >") &&
		strstr(menu_line(4, line, sizeof(line)), "Gyro vertical\x02") && strstr(line, "Normal") &&
		strstr(menu_line(5, line, sizeof(line)), "Gyro turning\x02") && strstr(line, "Turn (yaw)") &&
		!strncmp(menu_line(6, line, sizeof(line)), "\x04Gyro: yaw -999 pitch -999 roll -999", 36),
		"Gyro settings: button, sensitivity, vertical, turning, the gyroscope's line");
	check(count == 9 && menu_fits() && !diagram[0],
		"Gyro settings: 9 lines (tab bar, title, 4 rows, the line, help, buttons), each fits");
	for (index = 0; index < 4; index++)
		press(VITA_BUTTON_DOWN);
	check(!strncmp(menu_line(menu_selected, line, sizeof(line)), "Gyro button", 11),
		"down skips the gyroscope's line (four downs: back to the first row)");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GYRO_BUTTON"), "r") && strstr(menu, "nothing else in play"), "Gyro button: R");
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GYRO_SENS"), "250") && strstr(menu, "< 2.5x >"), "Gyro sensitivity: 2.5x");
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GYRO_INVERT_Y"), "1") && !strcmp(getenv("HALO_GYRO_TURN"), "roll"),
		"Gyro vertical Inverted, turning Tilt (roll)");
	vita_gyro_config_load(&config);
	check(config.mode == VITA_GYRO_HOLD && config.button == VITA_BUTTON_R && config.sensitivity > 2.49f &&
		config.sensitivity < 2.51f && config.invert_y && config.turn == VITA_GYRO_TURN_ROLL,
		"the pad reads the gyro's choices");
	press(VITA_BUTTON_CIRCLE);
	press(VITA_BUTTON_CIRCLE);

	/* the next start: settings.txt brings them back */
	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].variable && !strncmp(settings[index].variable, "HALO_GYRO", 9))
		{
			settings[index].choice = 0;
			unsetenv(settings[index].variable);
		}
	vita_settings_load();
	check(!strcmp(getenv("HALO_GYRO"), "hold") && !strcmp(getenv("HALO_GYRO_BUTTON"), "r") &&
		!strcmp(getenv("HALO_GYRO_SENS"), "250") && !strcmp(getenv("HALO_GYRO_INVERT_Y"), "1") &&
		!strcmp(getenv("HALO_GYRO_TURN"), "roll"), "settings.txt round trip: the gyro's rows back at start-up");
	check(strstr(file_text(SETTINGS_FILE), "HALO_XBOX_A=") && strstr(file_text(SETTINGS_FILE), "XV_LOOK_SENS="),
		"the other rows are still saved beside them");

	/* Reset controls leaves the gyro's rows (as when they had a tab) */
	open_panel();
	to_tab("Controls");
	open_page("Advanced");
	to_line("Reset controls");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(getenv("HALO_GYRO"), "hold") && !strcmp(getenv("HALO_GYRO_SENS"), "250"),
		"Reset controls leaves gyro aiming and its settings");
	press(VITA_BUTTON_CIRCLE);
	press(VITA_BUTTON_CIRCLE);
}

/* ---------- the Multiplayer tab and its Play page */

/* the most players the game says its network game takes (0: none yet) */
static int status_maximum;

/* what the game's main loop says (system_link_shortcut.c), as it would */
static void game_status(int state, int machines, int games, int host)
{
	static const unsigned char address[4] = { 192, 168, 1, 23 };
	int value;

	memcpy(&value, address, sizeof(value));
	halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE] = state;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_MACHINES] = machines;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_PLAYERS] = machines;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_GAMES] = games;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_HOST] = host;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_MAXIMUM] = status_maximum;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_ADDRESS] = state == SYSTEM_LINK_STATE_STARTING ? 0 : value;
	/* (the panel redraws twice a second) */
	clock_us += 600000;
	frame(0);
}

/* the game's main loop answering the panel's request */
static void game_answers(int answer)
{
	halo_system_link_answer = answer;
	halo_system_link_request = SYSTEM_LINK_REQUEST_NONE;
	frame(0);
}

/* the keyboard closed: with this text typed (NULL: cancelled) */
static void keyboard_closes(const char *typed)
{
	snprintf(ime_typed, sizeof(ime_typed), "%s", typed ? typed : "");
	ime_result = typed ? 1 : -1;
	frame(0);
}

/* the Play page's rows, in order, by their labels ("" between rows of a
line each); its lines that are not rows ('\x04') counted in info */
static int play_rows(char *labels, int size, int *info)
{
	const char *at = menu;
	int rows = 0, used = 0;

	labels[0] = 0;
	*info = 0;
	while (*at)
	{
		int length = (int)strcspn(at, "\n");
		int label = (int)strcspn(at, "\x02\n");

		if (label < length && used < size)
		{
			used += snprintf(labels + used, (size_t)(size - used), "%s%.*s", rows ? "|" : "", label, at);
			rows++;
		}
		else if (at[0] == '\x04')
			(*info)++;
		at += length + (at[length] == '\n');
	}
	return rows;
}

static void test_multiplayer_tab(void)
{
	char line[128], labels[512];
	int info, count, longest;
	char diagram[16];

	/* (the network this session runs: Online, from the environment) */
	check(!strncmp(menu_line(1, line, sizeof(line)), "Connection*\x02", 12) && strstr(line, "< Online") &&
		!strcmp(menu_line(2, line, sizeof(line)), "Play\x02  >") &&
		!strcmp(menu_line(3, line, sizeof(line)), "Modded maps\x02  0 maps  >") &&
		!menu_line(4, line, sizeof(line))[0],
		"Multiplayer: Connection, Play, Modded maps (no ad hoc room: Online); a gap");
	check(!strstr(menu, "Host a game") && !strstr(menu, "Join a game") && !strstr(menu, "Co-op") &&
		!strstr(menu, "Online games") && !strstr(menu, "Ad hoc") && !strstr(menu, "PC maps") &&
		!strstr(menu, "Lobby name"), "Multiplayer: no Host, Join, Co-op or Online games rows (all on Play)");
	check(strstr(menu, "\n\x04This Vita: vitauser\n") && strstr(menu, "\n\x04No game yet: host one or join one\n") &&
		strstr(menu, "\n\x04Online: ready"), "before the game says: this Vita's name, no game, internet play's line");
	check(menu_rows() == 3 && menu_fits(), "Multiplayer: three rows, 46 characters a line");
	to_line("Play");
	check(strstr(menu, "\n\x05Host a game, join one, or co-op the campaign\n") != NULL, "Play's help line");
	game_status(SYSTEM_LINK_STATE_MENUS, 0, 0, 0);
	printf("%s\n--\n", menu);
	check(strstr(menu, "\n\x04This Vita: vitauser  192.168.1.23\n") != NULL, "this Vita's address, as the others reach it");

	/* the Play page, Online: every row */
	check(open_page("Play") && strstr(menu, "\n\x03Multiplayer > Play\n"), "Play opens its page");
	printf("%s\n--\n", menu);
	count = play_rows(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Host a game|  Lobby name|  Max players|  Visibility|  Password|Host co-op campaign|"
		"Join with a code|Browse public games|Games on this network") && count == 9,
		"Play, Online: Host a game (lobby name, max players, visibility, password), co-op, a code, browse, this network");
	check(strstr(menu, "\nHost a game\x02  >\n") && strstr(menu, "\n  Lobby name\x02  vitauser\n") &&
		strstr(menu, "\n  Max players\x02< 16  \n") && strstr(menu, "\n  Visibility\x02< Public  \n") &&
		strstr(menu, "\n  Password\x02  none\n") && strstr(menu, "\nGames on this network\x02  >\n"),
		"Play's values: the lobby name is the Vita's user name, 16 players, Public (OpenCE's default), no password");
	check(info == 2 && strstr(menu, "\n\x04No game yet: host one or join one\n") && strstr(menu, "\n\x04Online: ready") &&
		!strstr(menu, "This Vita"), "Play: the game's line and internet play's under the rows");
	count = menu_lines(&longest, diagram, sizeof(diagram));
	check(menu_fits() && count <= 17, "Play, Online: 46 characters a line, no longer than Button layout's page");

	/* Host a game: the steps, then cross asks the game for System Link */
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(!strncmp(menu, "HOST A GAME", 11) && strstr(menu, "1 The game's System Link screen opens") &&
		strstr(menu, "2 A to join if asked, A on a profile, A again") && strstr(menu, "3 SYSTEM LINK GAMES: Y creates a game") &&
		strstr(menu, "4 A on a map, A on a game type") && strstr(menu, "\n  As \"vitauser\", 16 players at most\n") &&
		strstr(menu, "Menus: A Cross, B Circle, X Square, Y Triangle") &&
		strstr(menu, "Cross: open System Link   Circle: back"), "Host a game: the steps, the game as others see it");
	check(menu_fits() && !strstr(menu, " button") && !strstr(menu, "Co-op"), "the steps: 46 characters a line, no co-op line");
	check(frame(VITA_BUTTON_CROSS) == 1 && halo_system_link_request == SYSTEM_LINK_REQUEST_HOST &&
		strstr(menu, "Opening System Link..."), "cross: the game is asked for its System Link screen (host)");
	/* (the cross still held when the game answers) */
	halo_system_link_answer = SYSTEM_LINK_ANSWER_OPENED;
	halo_system_link_request = SYSTEM_LINK_REQUEST_NONE;
	check(frame(VITA_BUTTON_CROSS) == 1 && !menu_visible && frame(VITA_BUTTON_CROSS) == 1,
		"the game opened it: the panel closes; the cross still held is not the game's A, that frame or after");
	check(frame(0) == 0 && frame(VITA_BUTTON_CROSS) == 0, "let go and pressed again: the game has the pad");
	check(strstr(log_text, "settings: host a game: asked the game for its System Link screen (online)") != NULL,
		"halo.log says so");
	frame(0);

	/* the hosted game's settings: the lobby name, on the system's keyboard */
	open_panel();
	check(strstr(menu, "*Multiplayer") && !strstr(menu, "\n\x03"), "the panel opens again on the tab");
	open_page("Play");
	to_line("  Lobby name");
	check(strstr(menu, "\n\x05The name others see for your game\n") &&
		strstr(menu, "\n\x06L/R: tabs   Cross: type   Circle: back"), "Lobby name: its help; cross types");
	press(VITA_BUTTON_RIGHT);
	check(ime_opens == 0, "right does not open the keyboard");
	press(VITA_BUTTON_CROSS);
	check(ime_opens == 1 && !strcmp(ime_title, "Lobby name") && !strcmp(ime_initial, "vitauser") && ime_maximum == 15 &&
		!ime_password && !menu_visible, "cross: the system's keyboard on the name (15 at most), the panel hidden");
	check(frame(VITA_BUTTON_CROSS) == 1 && frame(VITA_BUTTON_CIRCLE) == 1 && frame(0) == 1,
		"the game sees no buttons while the keyboard is up");
	keyboard_closes("  Birch's game\x01\t of the century  ");
	printf("%s\n--\n", menu);
	check(!strcmp(getenv("HALO_NET_LOBBY_NAME"), "Birch's game of") && !strcmp(lobby_name, "Birch's game of") &&
		strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_NAME=Birch's game of\n") && menu_visible &&
		strstr(menu, "\n  Lobby name\x02  Birch's game of\n") && menu_fits(),
		"the name typed: printable ASCII, trimmed, 15 at most; in the environment, the listing and settings.txt");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(ime_initial, "Birch's game of"), "the keyboard opens on the name kept");
	keyboard_closes(NULL);
	check(!strcmp(getenv("HALO_NET_LOBBY_NAME"), "Birch's game of") && menu_visible, "cancelled: the name stays");
	press(VITA_BUTTON_CROSS);
	keyboard_closes("\x01\x02   ");
	check(!strcmp(getenv("HALO_NET_LOBBY_NAME"), "vitauser") && !strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_NAME"),
		"nothing printable typed: the Vita's user name again, not saved (a new user name shows)");
	press(VITA_BUTTON_CROSS);
	keyboard_closes("Birch's game");
	check(!strcmp(getenv("HALO_NET_LOBBY_NAME"), "Birch's game"), "Lobby name: Birch's game");

	/* Max players: 2 to 16 */
	to_line("  Max players");
	check(strstr(menu, "\n\x05The most players your games take (co-op: 2)\n") != NULL, "Max players' help: co-op takes 2");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_NET_MAX_PLAYERS"), "16"), "Max players: 16 is the most");
	for (int index = 0; index < 20; index++)
		press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_NET_MAX_PLAYERS"), "2") && strstr(menu, "\n  Max players\x02  2 >\n") &&
		strstr(file_text(SETTINGS_FILE), "HALO_NET_MAX_PLAYERS=2\n"), "Max players: 2 is the fewest, saved");
	for (int index = 0; index < 6; index++)
		press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_NET_MAX_PLAYERS"), "8") && !strstr(menu, "Restart the game"), "Max players: 8, live");

	/* Visibility: Private hides the password; Public at once */
	to_line("  Visibility");
	press(VITA_BUTTON_LEFT);
	check(lobby_public == 0 && !strcmp(getenv("HALO_NET_HOST_PUBLIC"), "false") &&
		!strcmp(getenv("HALO_NET_LOBBY_PUBLIC"), "false") && !strstr(menu, "Password"),
		"Visibility Private: at once, internet play's variable too; no password row");
	press(VITA_BUTTON_RIGHT);
	check(lobby_public == 1 && !strcmp(getenv("HALO_NET_LOBBY_PUBLIC"), "true") && strstr(menu, "\n  Password\x02"),
		"Visibility Public: listed at once, the password row back");

	/* Password: hidden as typed, "set" */
	to_line("  Password");
	press(VITA_BUTTON_CROSS);
	check(ime_password && strstr(ime_title, "Password") && !ime_initial[0], "Password: the keyboard hides what is typed");
	keyboard_closes("hunter2");
	check(!strcmp(getenv("HALO_NET_LOBBY_PASSWORD"), "hunter2") && strstr(menu, "\n  Password\x02  set\n") &&
		!strstr(menu, "hunter2") && strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_PASSWORD=hunter2\n") &&
		strstr(log_text, "settings: password set (settings panel)") && !strstr(log_text, "hunter2"),
		"Password: kept and saved, shown as set (not in the panel or halo.log)");
#ifdef PLAY_UPSTREAM_BROWSER
	check(!strcmp(lobby_password, "hunter2"), "the listing has the password");
#endif
	press(VITA_BUTTON_CROSS);
	keyboard_closes("");
	check(!getenv("HALO_NET_LOBBY_PASSWORD")[0] && strstr(menu, "\n  Password\x02  none\n") &&
		!strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_PASSWORD"), "an empty password: none, not saved");

	/* the game's state, on the page */
	game_status(SYSTEM_LINK_STATE_SEARCHING, 0, 2, 0);
	check(strstr(menu, "Looking for games: 2 found") != NULL, "the System Link list, two games");
	status_maximum = 8;
	game_status(SYSTEM_LINK_STATE_HOSTING, 1, 0, 1);
	check(strstr(menu, "\n\x04Hosting: waiting for players (max 8)\n") != NULL, "hosting alone: waiting, the most players");
	game_status(SYSTEM_LINK_STATE_HOSTING, 3, 0, 1);
	check(strstr(menu, "\n\x04Hosting: 3 of 8 players in the lobby\n") && menu_fits(), "hosting: three of eight");
	status_maximum = 0;
	game_status(SYSTEM_LINK_STATE_HOSTING, 3, 0, 1);
	check(strstr(menu, "Hosting: 3 Vitas in the lobby") != NULL, "hosting, the game not saying its most: three Vitas");

	/* hosting: Games on this network says why not, and asks nothing */
	to_line("Games on this network");
	check(strstr(menu, "\n\x05The System Link list: join a game there\n") != NULL, "Games on this network's help");
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(!strncmp(menu, "JOIN A GAME", 11) && strstr(menu, "A on the host's game") &&
		strstr(menu, "\n!Already hosting: the game's lobby is open\nCircle: back"),
		"Games on this network while hosting: Join's steps, and why not now");
	check(menu_fits(), "Join's steps: 46 characters a line");
	press(VITA_BUTTON_CROSS);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_NONE, "... and cross asks nothing");
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "> Play\n") != NULL, "circle: back to the Play page");
	game_status(SYSTEM_LINK_STATE_LOBBY, 2, 0, 0);
	check(strstr(menu, "In a lobby: 2 Vitas, the host starts") != NULL, "in another's lobby");
	game_status(SYSTEM_LINK_STATE_IN_GAME, 4, 0, 1);
	check(strstr(menu, "In a game: 4 Vitas (you host)") != NULL, "in a game this Vita hosts");

	/* Host co-op campaign: the Campaign screen at once (not in a game) */
	to_line("Host co-op campaign");
	check(strstr(menu, "\n\x05" "Campaign: pick a level and difficulty, then Y: Play co-op\n") &&
		strstr(menu, "\nHost co-op campaign\x02  >\n"), "Host co-op campaign: its note, Y on the difficulty");
	press(VITA_BUTTON_CROSS);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_NONE && menu_visible &&
		strstr(menu, "\n\x05In a game: Start, then Quit, to leave it\n"), "in a game: it says why, and asks nothing");
	game_status(SYSTEM_LINK_STATE_PLAYING, 0, 0, 0);
	check(strstr(menu, "Playing: quit the level to host or join") != NULL, "playing a level");
	game_status(SYSTEM_LINK_STATE_MENUS, 0, 0, 0);
	clock_us += 5000000;
	press(VITA_BUTTON_CROSS);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_CAMPAIGN &&
		strstr(log_text, "settings: host co-op campaign: asked the game for its Campaign screen (online)"),
		"in the menus: the game is asked for its Campaign screen");
	game_answers(SYSTEM_LINK_ANSWER_UNAVAILABLE);
	check(menu_visible && strstr(menu, "\n\x05The game could not open it (see its message)\n"),
		"the game could not: the help line says so, over no screen of its own");
	press(VITA_BUTTON_CROSS);
	game_answers(SYSTEM_LINK_ANSWER_OPENED);
	check(!menu_visible && frame(0) == 0, "the Campaign screen opened: the panel closes");

	/* Join with a code: today's code screen, then Join's steps (761c59ce) */
	open_panel();
	open_page("Play");
	to_line("Join with a code");
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(!strncmp(menu, "JOIN WITH A CODE\n(the host's Play page shows it)", 47) &&
		strstr(menu, "Triangle: browse public games instead"), "Join with a code: the host's code first");
	check(menu_fits(), "the code: 46 characters a line");
	press(VITA_BUTTON_CROSS);
	check(!strncmp(menu, "JOIN A GAME\n", 12) && strstr(menu, "\nOnline: ") && halo_system_link_request == SYSTEM_LINK_REQUEST_NONE &&
		!strcmp(joined_code, "AAAA-AAAA"), "the code taken: Join's steps, with how the lookup goes, and nothing asked yet");
	check(menu_fits(), "Join's steps online: 46 characters a line");
	press(VITA_BUTTON_CROSS);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_JOIN, "Join: the game is asked (join)");
	game_answers(SYSTEM_LINK_ANSWER_IN_PLAY);
	check(menu_visible && strstr(menu, "!Leave the level first: Start, then Quit"), "the game said no: the guide says why");
	/* ... and a game that does not answer in 3 s is asked no more */
	press(VITA_BUTTON_CROSS);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_JOIN, "cross again: asked again");
	clock_us += 3100000;
	frame(0);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_NONE && strstr(menu, "!The game did not answer: try again"),
		"no answer in 3 s: the request withdrawn, the guide says so");
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "> Play\n") != NULL, "circle: the Play page");

	/* Connection: Same Wi-Fi, Ad hoc, Online (experimental) */
	to_tab("Multiplayer");
	to_line("Connection");
	check(strstr(menu, "\n\x05Internet play by code (experimental)") != NULL, "Connection Online's help line");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_VITA_NETWORK"), "online") && strstr(menu_line(menu_selected, line, sizeof(line)), "< Online  "),
		"Connection: Online is the last");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_VITA_NETWORK"), "adhoc") && strstr(menu, "Restart the game"),
		"Connection: Ad hoc asks for a restart");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_VITA_NETWORK"), "adhoc") && strstr(menu_line(menu_selected, line, sizeof(line)),
		"< Ad hoc >"), "... Same Wi-Fi and back: Online offered on either side (no dev setting needed)");
	clock_us += 5000000;
	press(VITA_BUTTON_DOWN);
	check(!strstr(menu, "Restart the game"), "the other lines' help again after a few seconds");
	press(VITA_BUTTON_UP);
	check(strstr(menu, "Restart the game") != NULL, "the Connection line still says it");
	check(!strcmp(menu_line(3, line, sizeof(line)), "Ad hoc room\x02  1 >") && menu_rows() == 4,
		"Ad hoc chosen: the room's row on the tab, under Play");
	open_page("Play");
	to_line("Host a game");
	press(VITA_BUTTON_CROSS);
	check(strstr(menu, "!Restart the game first: Connection changed") != NULL, "Host a game waits for the restart");
	press(VITA_BUTTON_CIRCLE);
	to_line("Host co-op campaign");
	press(VITA_BUTTON_CROSS);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_NONE && strstr(menu, "Restart the game first"),
		"Host co-op campaign too");

	/* the next start, in ad hoc play */
	restart_pending = 0;
	vita_settings_load();
	check(!strcmp(getenv("HALO_NET_ADHOC"), "true"), "Connection Ad hoc: ad hoc play on");
	to_tab("Multiplayer");
	clock_us += 600000;
	frame(0);
	check(strstr(menu, "\n\x04This Vita: vitauser, ad hoc room 1\n") && strstr(menu, "\n\x04" "Ad hoc: not in a group\n"),
		"ad hoc: this Vita's room, not in a group");
	to_line("Ad hoc room");
	check(strstr(menu, "\n\x05Vitas in the same room play together\n") != NULL, "Ad hoc room's help");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_ADHOC_ROOM"), "2"), "Ad hoc room 2");
	open_page("Play");
	printf("%s\n--\n", menu);
	play_rows(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Host a game|  Lobby name|  Max players|Host co-op campaign|Games on this network") &&
		menu_fits(), "Play, Ad hoc: no visibility, password, code or public games (internet play's)");
	to_line("Host a game");
	check(strstr(menu, "\n\x05" "First the system's dialog joins ad hoc room 2\n") != NULL,
		"not in the group: the help line says it is joined first");

	/* Host a game in ad hoc play, not in a group: the dialog first, then
	System Link by itself */
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(strstr(menu, "1 The system's dialog joins ad hoc room 2") && strstr(menu, "2 The game's System Link screen") &&
		menu_fits(), "Host in ad hoc play: the room's group joined first");
	press(VITA_BUTTON_CROSS);
	check(adhoc_connects == 1 && adhoc_room == 2 && adhoc_mode == 0 && !menu_visible &&
		halo_system_link_request == SYSTEM_LINK_REQUEST_NONE, "cross: the ad hoc dialog (connect, room 2), the panel closed");
	check(frame(VITA_BUTTON_CROSS) == 1 && frame(0) == 1, "the game sees no buttons while the dialog is up");
	adhoc_state_value = 2;
	frame(0);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_HOST, "in the group: System Link asked for");
	game_answers(SYSTEM_LINK_ANSWER_OPENED);
	check(!menu_visible && frame(0) == 0, "opened: nothing over the game");
	/* ... co-op the same way */
	vita_adhoc_leave();
	open_panel();
	open_page("Play");
	to_line("Host co-op campaign");
	press(VITA_BUTTON_CROSS);
	check(adhoc_connects == 2 && !menu_visible && halo_system_link_request == SYSTEM_LINK_REQUEST_NONE,
		"Host co-op campaign in ad hoc play: the dialog first");
	adhoc_state_value = 2;
	frame(0);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_CAMPAIGN, "in the group: the Campaign screen asked for");
	game_answers(SYSTEM_LINK_ANSWER_OPENED);
	/* ... and a dialog that does not join says so over the game */
	vita_adhoc_leave();
	open_panel();
	open_page("Play");
	to_line("Games on this network");
	press(VITA_BUTTON_CROSS);
	press(VITA_BUTTON_CROSS);
	adhoc_state_value = -1;
	frame(0);
	check(menu_visible && strstr(menu, "Join a game") && strstr(menu, "did not join the ad hoc group") &&
		halo_system_link_request == 0, "the dialog did not join: a message, System Link not asked");
	press(VITA_BUTTON_CROSS);
	check(!menu_visible, "cross closes the message");
	adhoc_state_value = 2;
	frame(0);

	/* Same Wi-Fi: the same five rows, no ad hoc room */
	vita_settings_set("HALO_VITA_NETWORK", "wifi");
	restart_pending = 0;
	vita_settings_load();
	open_panel();
	to_tab("Multiplayer");
	check(menu_rows() == 3 && !strstr(menu, "Ad hoc room"), "Same Wi-Fi: Connection, Play, Modded maps");
	open_page("Play");
	play_rows(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Host a game|  Lobby name|  Max players|Host co-op campaign|Games on this network") &&
		menu_fits(), "Play, Same Wi-Fi: the hosted game's name and size, co-op, the System Link list");
	press(VITA_BUTTON_CIRCLE);
	press(VITA_BUTTON_CIRCLE);
}

/* internet play's rows on the Play page: the public games */
static void test_online_rows(void)
{
	char labels[512];
	int info;

	/* (a session started Online) */
	vita_settings_set("HALO_VITA_NETWORK", "online");
	restart_pending = 0;
	vita_settings_load();
	to_tab("Multiplayer");
	check(open_page("Play") && strstr(menu, "\n\x03Multiplayer > Play\n"), "the Play page, Online");
	play_rows(labels, sizeof(labels), &info);
	check(strstr(labels, "|  Visibility|  Password|") && strstr(labels, "|Join with a code|Browse public games|") &&
		!strstr(menu, "Ad hoc dialog"), "Online: internet play's rows back (the ad hoc dialog stays in Dev)");

	/* a code typed with the D-pad: B (up from A) on the first, 9 (down
	from A: the digits come first) on the fifth, then cross */
	to_line("Join with a code");
	press(VITA_BUTTON_CROSS);
	check(!strncmp(menu, "JOIN WITH A CODE", 16), "Join with a code opens the code screen");
	press(VITA_BUTTON_UP);
	for (int index = 0; index < 4; index++)
		press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_DOWN);
	printf("%s\n--\n", menu);
	check(strstr(menu, "B A A A - 9 A A A") != NULL, "the code screen shows the letters typed");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(joined_code, "BAAA-9AAA") && !strncmp(menu, "JOIN A GAME\n", 12), "cross joins the code typed: Join's steps");
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "Looking up BAAA-9AAA") != NULL && strstr(menu, "> Play\n"),
		"back on the page: the help line says it is looked up");

	/* the public lobby: this Vita's own game is left out */
	to_line("Browse public games");
	press(VITA_BUTTON_CROSS);
	check(browsing == 1 && !strncmp(menu, "PUBLIC GAMES", 12), "Browse public games starts browsing");
	clock_us += 600000;
	frame(0);
	printf("%s\n--\n", menu);
	check(strstr(menu, "desktop host") && !strstr(menu, "this vita") && strstr(menu, "\nCross: join   Circle: back"),
		"the lobby lists the others' games");
#ifdef PLAY_UPSTREAM_BROWSER
	check(strstr(menu, "\ndesktop host     2/16 Blood Gulch\n") && strstr(menu, "\nlocked game      2/16 Wizard         [pw]\n") &&
		strstr(menu, "\nSlayer to 50 on Blood Gulch\n2 of 16: Alpha, Bravo\n"),
		"each game: name, players of most, map, [pw]; the chosen one's rules and players");
#else
	check(strstr(menu, "\ndesktop host     2/16 HJ4T-9WXZ\n") != NULL, "each game: name, players of most, code");
#endif
	{
		const char *at = menu;
		int longest = 0;

		for (; *at; )
		{
			int length = (int)strcspn(at, "\n");

			longest = length > longest ? length : longest;
			at += length + (at[length] == '\n');
		}
		check(longest <= 46, "the public games: 46 characters a line");
	}
	press(VITA_BUTTON_CROSS);
#ifdef PLAY_UPSTREAM_BROWSER
	check(!strcmp(joined_code, "a1b2") && !joined_password[0] && browsing == 0, "cross joins the game by its id");
#else
	check(!strcmp(joined_code, "HJ4T-9WXZ") && !strcmp(joined_host, "0211223344bb") && browsing == 0,
		"cross joins the game's code, for the host it is listed under, and stops browsing");
#endif
	check(joined_from_lobby && !strncmp(menu, "JOIN A GAME\n", 12),
		"... as a public lobby's game (its map downloads warn); then Join's steps");
	press(VITA_BUTTON_CIRCLE);
#ifdef PLAY_UPSTREAM_BROWSER
	/* a locked game: its password first */
	to_line("Browse public games");
	press(VITA_BUTTON_CROSS);
	clock_us += 600000;
	frame(0);
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_CROSS);
	check(ime_password && !menu_visible, "a locked game: the keyboard for its password");
	keyboard_closes("swordfish");
	check(!strcmp(joined_code, "c3d4") && !strcmp(joined_password, "swordfish") && !strncmp(menu, "JOIN A GAME\n", 12),
		"joined with the password typed");
	press(VITA_BUTTON_CIRCLE);
#endif

	/* hosting: the code shows on the Play page and the tab */
	hosting = 1;
	clock_us += 5000000;
	frame(0);
	check(strstr(menu, "\n\x04Your code: QX7K-M2PA (public)") != NULL, "hosting online: the Play page shows the code");
	to_tab("Multiplayer");
	clock_us += 5000000;
	frame(0);
	check(strstr(menu, "\n\x04Your code: QX7K-M2PA (public)") != NULL, "... and the Multiplayer tab");
	check(menu_fits(), "Multiplayer with internet play: 46 characters a line");
	hosting = 0;
	to_tab("Dev");
	to_line("Save report");
}

/* every settings variable 1.0.3 had (its settings.txt) is still a row,
with the values it saved, but the co-op pair (gone: co-op is hosted from
the Campaign screen) and Online games (now Visibility, by OpenCE's name);
and the rows added since (Sun rays, Button icons, Map downloads, Max
players, Visibility; the Play page's two texts) */
static void test_variables_kept(void)
{
	static const char *const variables[] = {
		"HALO_PROFILE", "HALO_RENDER_SCALE", "HALO_DYNAMIC_RES_MIN", "HALO_DISPLAY_WIDTH", "HALO_UPSCALE_FILTER",
		"HALO_MODEL_LOD_SCALE", "HALO_MIN_OBJECT_PIXELS", "HALO_SCENERY_UPDATE_DIVISOR",
		"HALO_LIGHTING_REFRESH_DIVISOR", "HALO_INTERPOLATE_FIRST_PERSON", "HALO_FRAMERATE_COUNTER", "HALO_FRAME_CAP",
		"HALO_SOUND_CHANNELS", "HALO_SOUND_OBSTRUCTION_TICKS", "XV_LOOK_SENS", "XV_INVERT_Y", "XV_DEADZONE",
		"HALO_CROUCH_TOGGLE", "HALO_TOUCH_TOP_LEFT", "HALO_TOUCH_TOP_RIGHT", "HALO_TOUCH_LEFT_EDGE",
		"HALO_TOUCH_RIGHT_EDGE", "HALO_TOUCH_REAR_LEFT", "HALO_TOUCH_REAR_RIGHT", "HALO_TOUCH_REAR_GUARD",
		"HALO_XBOX_A", "HALO_XBOX_B", "HALO_XBOX_X", "HALO_XBOX_Y", "HALO_XBOX_BLACK", "HALO_XBOX_WHITE",
		"HALO_XBOX_LEFT_TRIGGER", "HALO_XBOX_RIGHT_TRIGGER", "HALO_XBOX_LEFT_STICK", "HALO_XBOX_RIGHT_STICK",
		"HALO_XBOX_BACK", "HALO_DEV_SETTINGS", "HALO_GYRO", "HALO_GYRO_BUTTON", "HALO_GYRO_SENS",
		"HALO_GYRO_INVERT_Y", "HALO_GYRO_TURN", "HALO_VITA_NETWORK",
		"HALO_ADHOC_ROOM", "HALO_CUSTOM_EDITION", PERFORMANCE_LOG, "HALO_HANG_CRASH",
		"XV_FPS", "HALO_DEBUG_CAMERA", "HALO_GXM_WCLAMP", "HALO_TARGET_CHAIN_MIN_SIZE", "HALO_FRAME_PHASE_LOCK",
		"HALO_GXM_RTT_SYNC", "HALO_ADHOC_DIALOG_MODE", "HALO_SUN_RAYS",
		"HALO_BUTTON_ICONS", "HALO_AI_PERCEPTION_LOD", "HALO_DECAL_MIN_PIXELS", "HALO_MAP_SHARE_FROM",
		"HALO_NET_MAX_PLAYERS", "HALO_NET_HOST_PUBLIC",
	};
	int index, all = 1, choices = 0;

	for (index = 0; index < (int)(sizeof(variables) / sizeof(variables[0])); index++)
	{
		const struct setting *setting = setting_named(variables[index]);

		if (!setting || setting->kind != KIND_CHOICE)
		{
			printf("missing: %s\n", variables[index]);
			all = 0;
		}
	}
	for (index = 0; index < SETTING_COUNT; index++)
		choices += settings[index].kind == KIND_CHOICE;
	check(all && choices == (int)(sizeof(variables) / sizeof(variables[0])),
		"every settings variable of 1.0.3 is still a row (but co-op's and Online games), and no other but those added since");
	check(!setting_named("HALO_NET_COOP_LEVEL") && !setting_named("HALO_NET_COOP_DIFFICULTY") &&
		!setting_named("HALO_NET_LOBBY_PUBLIC"), "no co-op rows (co-op is hosted from Campaign), no Online games row");
	check(setting_named("HALO_NET_LOBBY_NAME")->kind == KIND_TEXT && setting_named("HALO_NET_LOBBY_PASSWORD")->kind == KIND_TEXT &&
		setting_named("HALO_NET_LOBBY_NAME")->page == PAGE_PLAY, "the lobby name and password: texts on the Play page");
	check(!strcmp(setting_named("HALO_DEBUG_CAMERA")->names[0], "Off") &&
		setting_named("HALO_DEBUG_CAMERA")->page == TAB_DEV, "Debug camera stays in Dev");
	{
		/* the values each row saved in 1.0.3 */
		const struct setting *network = setting_named("HALO_VITA_NETWORK"), *gyro = setting_named("HALO_GYRO");
		const struct setting *button = setting_named("HALO_XBOX_WHITE"), *zone = setting_named("HALO_TOUCH_REAR_LEFT");

		check(!strcmp(network->values[0], "wifi") && !strcmp(network->values[1], "adhoc") &&
			!strcmp(network->values[2], "online") && !strcmp(gyro->values[3], "hold") &&
			!strcmp(button->values[9], "right") && !strcmp(zone->values[11], "back") &&
			!strcmp(setting_named("HALO_RENDER_SCALE")->values[5], "dynamic"), "the values saved are 1.0.3's");
	}
}

/* ---------- Button icons: the game's button icons, and the panel's terms */

static void test_button_icons(void)
{
	struct vita_controls_config config;
	char diagram[16], line[128];
	unsigned long generation;
	int longest;

	open_panel();
	to_tab("Controls");
	check(open_page("Button layout") && to_line("Button icons") &&
		strstr(menu, "\n\x05Halo's prompts: Xbox icons, or the Vita's\n"), "Button icons: its row and help");
	generation = halo_settings_generation;
	press(VITA_BUTTON_RIGHT);
	printf("%s\n--\n", menu);
	check(!strcmp(getenv("HALO_BUTTON_ICONS"), "playstation") && vita_button_icons_playstation() &&
		halo_settings_generation != generation && strstr(file_text(SETTINGS_FILE), "HALO_BUTTON_ICONS=playstation\n") &&
		strstr(menu, "\nButton icons\x02< PlayStation  "),
		"Button icons PlayStation: in the environment and settings.txt at once, read again (no restart)");
	check(!strstr(menu, "Button icons*"), "Button icons: live, no *");
	/* (the panel keeps the Xbox controller's names with either: the icons
	change in the game only) */
	check(strstr(menu, "\nA\x02  Cross >") && strstr(menu, "\nBlack\x02") && !strstr(menu, "\nJump\x02") &&
		menu_rows() == 12 && menu_fits(), "PlayStation: the Button layout's rows keep the Xbox's names, each fits");
	/* a remap: the game's icon follows (vita_controls.c) */
	to_line("X");
	press(VITA_BUTTON_RIGHT);
	vita_controls_config_load(&config);
	check(!strcmp(getenv("HALO_XBOX_X"), "triangle") && strstr(menu, "\nX\x02< Triangle >") &&
		vita_button_glyph(&config, 2, 0) == VITA_GLYPH_TRIANGLE && vita_button_glyph(&config, 2, 1) == VITA_GLYPH_SQUARE,
		"X on Triangle: the game's X icon shows Triangle in play (Square in the menus)");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "\nButton layout\x02  As shipped  >") != NULL,
		"back on Controls: Button icons is not the layout (As shipped)");
	check(strstr(menu, "\nCrouch\x02") != NULL, "Controls: Crouch keeps its label");

	/* the touch zones: the Xbox's names too */
	check(open_page("Touch zones") && to_line("Rear touch left"), "Touch zones opens");
	press(VITA_BUTTON_RIGHT);
	menu_lines(&longest, diagram, sizeof(diagram));
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "a") && strstr(menu, "\nRear touch left\x02< A >") && menu_fits(),
		"PlayStation: a zone's choice keeps the Xbox's name");
	vita_controls_config_load(&config);
	check(vita_button_glyph(&config, 0, 0) == VITA_GLYPH_CROSS, "A on Cross and a zone: Cross shows");
	press(VITA_BUTTON_LEFT);
	to_tab("Controls");

	/* the guide's steps: the menus' Xbox buttons, and which Vita button each is */
	to_tab("Multiplayer");
	open_page("Play");
	to_line("Host a game");
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(strstr(menu, "SYSTEM LINK GAMES: Y creates a game") && strstr(menu, "Menus: A Cross") && menu_fits(),
		"PlayStation: Host a game's steps keep the Xbox's buttons");
	press(VITA_BUTTON_CIRCLE);
	press(VITA_BUTTON_CIRCLE);

	/* Reset controls keeps it; Xbox again puts the Xbox's terms back */
	to_tab("Controls");
	check(open_page("Advanced") && to_line("Reset controls"), "Advanced: Reset controls");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(getenv("HALO_BUTTON_ICONS"), "playstation"), "Reset controls keeps the Button icons");
	to_tab("Controls");
	check(open_page("Button layout") && to_line("Button icons"), "Button layout again");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_BUTTON_ICONS"), "xbox") && !vita_button_icons_playstation() &&
		strstr(menu, "\nA\x02  Cross >") && strstr(menu, "\nBlack\x02") && !strstr(menu, "\nJump\x02") &&
		strstr(file_text(SETTINGS_FILE), "HALO_BUTTON_ICONS=xbox\n"), "Xbox again: the Xbox's names");
	(void)line;
	press(VITA_BUTTON_CIRCLE);
	press(VITA_BUTTON_CIRCLE);
}

int main(void)
{
	char line[128];
	int index;

	/* (the Vita's folders, in this test's own) */
	mkdir("ux0:data", 0777);
	mkdir("ux0:data/haloce-vita", 0777);
	mkdir("ux0:data/haloce-vita/maps", 0777);
	setenv("HALO_MAPS_ROOT", "ux0:data/haloce-vita/maps", 1);

	test_variables_kept();
	setenv("HALO_VITA_NETWORK", "online", 1);
	vita_settings_load();
	check(!strcmp(getenv("HALO_NET_ONLINE"), "true") && !strcmp(getenv("HALO_NET_ALLOW_UPNP"), "false"),
		"Network Online: internet play on, without UPnP");
	check(!strcmp(getenv("HALO_NET_LOBBY_NAME"), "vitauser"), "the lobby name is the Vita's user name");
	check(getenv("HALO_NET_PLAYER_NAME") && !strcmp(getenv("HALO_NET_PLAYER_NAME"), "vitauser"),
		"a default profile's player name is the Vita's user name");
	check(!strstr(log_text, "TEST MODE"), "no dev switch on: halo.log does not say test mode");

	check(!frame(0), "closed: the game gets the buttons");
	open_panel();
	printf("%s\n--\n", menu);
	check(menu_visible && menu[0] == '\t' && strstr(menu, "\t*Graphics|Controls|Audio|Multiplayer\n"),
		"SELECT+START opens the Graphics tab; four tabs (Dev hidden)");
	check(!strncmp(menu_line(1, line, sizeof(line)), "Profile\x02", 8) &&
		!strncmp(menu_line(2, line, sizeof(line)), "Render resolution\x02", 18) &&
		!strncmp(menu_line(3, line, sizeof(line)), "Aspect ratio\x02", 13) &&
		!strncmp(menu_line(4, line, sizeof(line)), "Upscale filter\x02", 15) &&
		!strncmp(menu_line(5, line, sizeof(line)), "Frame limit\x02", 12) &&
		!strncmp(menu_line(6, line, sizeof(line)), "FPS counter\x02", 12) &&
		!strncmp(menu_line(7, line, sizeof(line)), "Smooth weapon motion\x02", 21) &&
		!strcmp(menu_line(8, line, sizeof(line)), "Advanced\x02  >") &&
		!strncmp(menu_line(9, line, sizeof(line)), "\x05Sets resolution", 16) &&
		!strncmp(menu_line(10, line, sizeof(line)), "\x06L/R: tabs", 10),
		"Graphics: Profile, Render resolution, Aspect, Upscale, Frame limit, FPS counter, weapon motion, Advanced; help, buttons");
	check(menu_fits() && menu_rows() <= 10, "Graphics: ten rows at most, each fits");
	check(!strstr(menu, "Dynamic minimum") && !strstr(menu, "Model detail"),
		"Graphics: Dynamic minimum hidden (not Dynamic), the detail rows under Advanced");
	check(strstr(menu, "\nProfile\x02< Balanced >") && strstr(menu, "\nRender resolution\x02< 75% >") &&
		strstr(menu, "\nAspect ratio\x02  16:9 >"), "values in a column: '< ' or two spaces before each");
	check(!strstr(menu, "Render resolution*") && !strstr(menu, "Aspect ratio*"), "resolution and aspect: live, no *");
	{
		/* Render resolution's Dynamic: saved as a word, the profile Custom,
		back to a number, and the minimum's row */
		unsigned long generation = halo_settings_generation;

		vita_settings_set("HALO_RENDER_SCALE", "dynamic");
		check(!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && halo_settings_generation != generation &&
			strstr(file_text(SETTINGS_FILE), "HALO_RENDER_SCALE=dynamic\n") &&
			!strcmp(settings[0].names[settings[0].choice], "Custom"),
			"Render resolution Dynamic: in the environment and settings.txt at once, the profile Custom");
		press(VITA_BUTTON_DOWN);
		press(VITA_BUTTON_UP);
		check(!strncmp(menu_line(3, line, sizeof(line)), "Dynamic minimum\x02", 16) && !strstr(line, "*"),
			"Dynamic: the Dynamic minimum row shows, under it (live, no *)");
		check(strstr(file_text(SETTINGS_FILE), "HALO_DYNAMIC_RES_MIN=0.5\n") != NULL, "Dynamic minimum: 50% by default, saved");
		vita_settings_set("HALO_DYNAMIC_RES_MIN", "0.75");
		check(!strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.75") &&
			strstr(file_text(SETTINGS_FILE), "HALO_DYNAMIC_RES_MIN=0.75\n"), "Dynamic minimum 75%");
		vita_settings_set("HALO_RENDER_SCALE", "0.3");
		check(!strcmp(getenv("HALO_RENDER_SCALE"), "0.5"), "a scale off the list goes to the nearest number, not Dynamic");
		vita_settings_set("HALO_RENDER_SCALE", "0.75");
		check(!strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.75") &&
			strstr(file_text(SETTINGS_FILE), "HALO_DYNAMIC_RES_MIN=0.75\n"), "not Dynamic: the minimum kept (hidden)");
		vita_settings_set("HALO_DYNAMIC_RES_MIN", "0.5");
		check(!strcmp(settings[0].names[settings[0].choice], "Balanced"), "back to 75%: Balanced again");
	}
	/* Graphics' Advanced: the detail rows the profile sets */
	check(open_page("Advanced") && strstr(menu, "\n\x03Graphics > Advanced\n") &&
		!strncmp(menu_line(2, line, sizeof(line)), "Model detail\x02", 13) &&
		!strncmp(menu_line(3, line, sizeof(line)), "Hide distant objects\x02", 21) &&
		!strncmp(menu_line(4, line, sizeof(line)), "Scenery updates\x02", 16) &&
		!strncmp(menu_line(5, line, sizeof(line)), "Object lighting\x02", 16) &&
		!strncmp(menu_line(6, line, sizeof(line)), "Sun rays\x02", 9) &&
		!strncmp(menu_line(7, line, sizeof(line)), "Distant AI\x02", 11) &&
		!strncmp(menu_line(8, line, sizeof(line)), "Tiny decals\x02", 12) && menu_rows() == 7 && menu_fits(),
		"Graphics, Advanced: model detail, distant objects, scenery, lighting, sun rays, distant AI, tiny decals");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_MODEL_LOD_SCALE"), "0.75") && !strcmp(settings[0].names[settings[0].choice], "Custom"),
		"a detail row changed on its page: the profile Custom");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(settings[0].names[settings[0].choice], "Balanced"), "... and Balanced again");
	press(VITA_BUTTON_CIRCLE);
	check(menu_visible && strstr(menu, "\t*Graphics") && !strstr(menu, "\n\x03") &&
		!strncmp(menu_line(menu_selected, line, sizeof(line)), "Advanced\x02", 9),
		"circle: back to Graphics, on Advanced");
	press(VITA_BUTTON_L);
	check(strstr(menu, "*Multiplayer") != NULL, "L from the first tab wraps to the last shown");
	press(VITA_BUTTON_R);
	press(VITA_BUTTON_R);
	check(strstr(menu, "*Controls") && strstr(menu, "Look sensitivity"), "R: Controls");
	press(VITA_BUTTON_R);
	check(strstr(menu, "*Audio") && strstr(menu, "\nSound voices*\x02") && strstr(menu, "\nSound occlusion\x02") &&
		menu_rows() == 2 && menu_fits(), "R: Audio");
	press(VITA_BUTTON_R);
	check(!strncmp(menu, "\tGraphics|Controls|Audio|*Multiplayer\n", 38), "R: Multiplayer");
	printf("%s\n--\n", menu);

	test_multiplayer_tab();

	/* ---------- Multiplayer's Modded maps page */
	write_map("ux0:data/haloce-vita/maps/mygulch.map", 5, 250000);
	write_map("ux0:data/haloce-vita/maps/inplay.map", 5, 2000);
	write_map("ux0:data/haloce-vita/maps/cemap.map", 609, 3 * 1024 * 1024 / 16);
	write_map("ux0:data/haloce-vita/maps/bloodgulch.map", 5, 1000);
	write_map("ux0:data/haloce-vita/maps/bitmaps.map", 1, 1000);
	write_file("ux0:data/haloce-vita/maps/cemap.bmp", "BM", 2);
	write_file("ux0:data/haloce-vita/maps/notes.txt", "x", 1);
	open_panel();
	check(to_tab("Multiplayer") && strstr(menu, "\nModded maps\x02  3 maps  >"), "Multiplayer: Modded maps says how many");
	check(open_page("Modded maps") && strstr(menu, "\n\x03Multiplayer > Modded maps\n"), "the Modded maps page");
	printf("%s\n--\n", menu);
	check(!strncmp(menu_line(2, line, sizeof(line)), "PC maps\x02", 8) &&
		!strcmp(menu_line(3, line, sizeof(line)), "Map downloads\x02  Ask >") && !menu_line(4, line, sizeof(line))[0] &&
		!strcmp(menu_line(5, line, sizeof(line)), "!Missing: sounds.map loc.map"),
		"PC maps switch, Map downloads (Ask), a gap; a CE map without sounds.map/loc.map: the warning");
	check(!strncmp(menu_line(6, line, sizeof(line)), "cemap\x02", 6) && !strncmp(menu_line(7, line, sizeof(line)), "inplay\x02", 7) &&
		!strncmp(menu_line(8, line, sizeof(line)), "mygulch\x02", 8) && menu_line(9, line, sizeof(line))[0] == '\x05',
		"the custom maps listed in order, not the Xbox's or CE resource maps");
	/* Map downloads: Ask (the default: public games warn), Not public games,
	Never; each with its own help, saved and in the environment the game
	reads (map_share.c) */
	check(!strcmp(getenv("HALO_MAP_SHARE_FROM"), "ask") &&
		strstr(file_text(SETTINGS_FILE), "HALO_MAP_SHARE_FROM=ask\n"), "Map downloads: Ask by default, saved");
	to_line("Map downloads");
	check(strstr(menu, "\n\x05" "Asks before a host's map comes; public games warn\n") && menu_fits(),
		"Ask's help: public games warn");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_MAP_SHARE_FROM"), "private") &&
		strstr(file_text(SETTINGS_FILE), "HALO_MAP_SHARE_FROM=private\n") &&
		strstr(menu, "\nMap downloads\x02< Not public games >\n") &&
		strstr(menu, "\n\x05" "Asks, but not in games from the public lobby\n") && menu_fits(),
		"right: Not public games, saved, its help");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_MAP_SHARE_FROM"), "never") &&
		strstr(file_text(SETTINGS_FILE), "HALO_MAP_SHARE_FROM=never\n") &&
		strstr(menu, "\nMap downloads\x02< Never  \n") &&
		strstr(menu, "\n\x05" "No downloads: copy the host's map in yourself\n"), "right: Never, saved, its help");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_MAP_SHARE_FROM"), "ask"), "back to Ask");
	check(strstr(menu, "\nmygulch\x02  On    245 KB  Xbox\n") != NULL, "mygulch: On, size, Xbox");
	check(strstr(menu, "\ncemap\x02  On ") && strstr(menu, "  CE\n"), "cemap: Custom Edition");
	check(menu_fits(), "Modded maps: each line fits");
	to_line("mygulch");
	check(strstr(menu, "\n\x05Xbox map: on or off, or delete it") &&
		strstr(menu, "\n\x06L/R: tabs   Left/right: on/off   Square: delete   Circle: back"), "a map's help and buttons");
	press(VITA_BUTTON_LEFT);
	check(strstr(menu_line(menu_selected, line, sizeof(line)), "Off") && !strcmp(getenv("HALO_MAPS_DISABLED"), "mygulch") &&
		strstr(file_text(SETTINGS_FILE), "HALO_MAPS_DISABLED=mygulch\n"), "left: mygulch off, in the environment and settings.txt");
	press(VITA_BUTTON_UP);
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_MAPS_DISABLED"), "inplay,mygulch") || !strcmp(getenv("HALO_MAPS_DISABLED"), "mygulch,inplay"),
		"two maps off");
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_CROSS);
	check(!getenv("HALO_MAPS_DISABLED") || !getenv("HALO_MAPS_DISABLED")[0], "right / cross: both on again");
	check(!strstr(file_text(SETTINGS_FILE), "HALO_MAPS_DISABLED"), "none off: no line in settings.txt");
	press(VITA_BUTTON_LEFT);
	/* delete: the map in play is kept */
	to_line("inplay");
	press(VITA_BUTTON_SQUARE);
	check(!strncmp(menu, "DELETE MAP", 10) && strstr(menu, "inplay.map"), "square asks before deleting");
	press(VITA_BUTTON_CROSS);
	check(strstr(menu, "inplay is in play") && file_exists("ux0:data/haloce-vita/maps/inplay.map"), "the map in play is kept");
	to_line("cemap");
	press(VITA_BUTTON_SQUARE);
	press(VITA_BUTTON_CIRCLE);
	check(file_exists("ux0:data/haloce-vita/maps/cemap.map") && strstr(menu, "> Modded maps\n"), "circle keeps it");
	press(VITA_BUTTON_SQUARE);
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(!file_exists("ux0:data/haloce-vita/maps/cemap.map") && !file_exists("ux0:data/haloce-vita/maps/cemap.bmp") &&
		strstr(menu, "Deleted cemap.map") && !strstr(menu, "!Missing"), "cross deletes it (and its picture); no CE map, no warning");
	check(strstr(log_text, "settings: map cemap.map deleted") != NULL, "halo.log says what was deleted");
	press(VITA_BUTTON_UP);
	while (strncmp(menu_line(menu_selected, line, sizeof(line)), "PC maps", 7))
		press(VITA_BUTTON_UP);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_CUSTOM_EDITION"), "1") && strstr(menu, "!Missing: sounds.map loc.map"),
		"PC maps On: the warning again");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "\nModded maps\x02  2 maps  >") != NULL, "back on Multiplayer: two maps now");

	/* ---------- Dev, behind its switch */
	press(VITA_BUTTON_R);
	check(strstr(menu, "*Graphics") != NULL, "R from Multiplayer: Graphics (Dev hidden)");
	to_tab("Controls");
	open_page("Advanced");
	to_line("Show dev settings");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\t*Graphics|*Controls") == NULL && strstr(menu, "|Multiplayer|Dev\n") != NULL,
		"Show dev settings (Controls, Advanced): the Dev tab");
	to_tab("Dev");
	printf("%s\n--\n", menu);
	check(!strncmp(menu_line(1, line, sizeof(line)), "Performance logging*\x02", 21) &&
		!strncmp(menu_line(2, line, sizeof(line)), "Crash dump on hang\x02", 19) &&
		!strncmp(menu_line(3, line, sizeof(line)), "FPS overlay\x02", 12) &&
		!strncmp(menu_line(4, line, sizeof(line)), "Debug camera\x02", 13) &&
		!strncmp(menu_line(5, line, sizeof(line)), "Ad hoc dialog\x02", 14) &&
		!strcmp(menu_line(6, line, sizeof(line)), "Save report\x02  >") &&
		!strcmp(menu_line(7, line, sizeof(line)), "A/B switches\x02  >") && menu_rows() == 7 && menu_fits(),
		"Dev: the switches, Debug camera, Ad hoc dialog, Save report, then A/B switches");
	check(!strstr(file_text(SETTINGS_FILE), "HALO_GXM_RTT_SYNC") && !strstr(file_text(SETTINGS_FILE), "XV_FPS"),
		"dev switches off: not in settings.txt");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_FRAME_TIMING"), "300") && !strcmp(getenv("HALO_RENDER_PROFILE"), "1") &&
		!strcmp(getenv("HALO_TICK_PROFILE"), "1") && strstr(file_text(SETTINGS_FILE), "HALO_PERF_LOG=1\n") &&
		strstr(menu, "\n\x05Restart the game"), "Performance logging: the three timing variables, saved, a restart");
	clock_us += 5000000;
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_HANG_CRASH"), "1") && !strstr(menu, "Restart the game"), "Crash dump on hang: live");
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	check(overlay_level == 2 && !strcmp(getenv("XV_FPS"), "2"), "FPS overlay: FPS only, live");
	press(VITA_BUTTON_RIGHT);
	check(overlay_level == 1 && strstr(file_text(SETTINGS_FILE), "XV_FPS=1\n"), "FPS overlay: Full");
	check(open_page("A/B switches") && strstr(menu, "\n\x03" "Dev > A/B switches\n") &&
		strstr(menu, "\nGPU W clamp*\x02") && strstr(menu, "\nTarget mip minimum*\x02") &&
		strstr(menu, "\nFrame phase lock*\x02") && strstr(menu, "\nRender target sync*\x02") && menu_rows() == 4 &&
		menu_fits(), "Dev, A/B switches: the start-up ones, marked *");
	to_line("Render target sync");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GXM_RTT_SYNC"), "0") && strstr(file_text(SETTINGS_FILE), "HALO_GXM_RTT_SYNC=0\n"),
		"Render target sync Off: 0, saved");
	press(VITA_BUTTON_LEFT);
	check(!getenv("HALO_GXM_RTT_SYNC") && !strstr(file_text(SETTINGS_FILE), "HALO_GXM_RTT_SYNC"),
		"back On: unset (the default), not saved");
	press(VITA_BUTTON_RIGHT);
	restart_pending = 0;
	press(VITA_BUTTON_CIRCLE);
	/* Save report */
	write_file("ux0:data/haloce-vita/halo.log", "log", 3);
	write_file("ux0:data/haloce-vita/halo-prev.log", "prev", 4);
	write_file("ux0:data/haloce-vita/env.txt", "HALO_NO_AUDIO=1\n", 16);
	write_map("ux0:data/psp2core-1-old.psp2dmp", 0, 100);
	write_map("ux0:data/psp2core-2-new.psp2dmp", 0, 200);
	{
		struct utimbuf old_time = { 1000, 1000 };

		utime("ux0:data/psp2core-1-old.psp2dmp", &old_time);
	}
	to_line("Save report");
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(strstr(menu, "Saved: ux0:data/haloce-vita/report-20261006-153012") != NULL, "Save report shows the folder");
	check(file_exists("ux0:data/haloce-vita/report-20261006-153012/halo.log") &&
		file_exists("ux0:data/haloce-vita/report-20261006-153012/halo-prev.log") &&
		file_exists("ux0:data/haloce-vita/report-20261006-153012/settings.txt") &&
		file_exists("ux0:data/haloce-vita/report-20261006-153012/env.txt") &&
		file_exists("ux0:data/haloce-vita/report-20261006-153012/psp2core-2-new.psp2dmp") &&
		!file_exists("ux0:data/haloce-vita/report-20261006-153012/psp2core-1-old.psp2dmp"),
		"the report: the logs, settings, env.txt and the newest dump");
	check(menu_fits(), "Dev: each line fits (the report's folder too)");
	test_online_rows();
	press(VITA_BUTTON_CIRCLE);
	check(!menu_visible, "circle closes the panel");

	/* the next start: the dev switches on say test mode in halo.log */
	log_text[0] = 0;
	unsetenv("HALO_GXM_RTT_SYNC");
	unsetenv("XV_FPS");
	vita_settings_load();
	printf("%s--\n", log_text);
	check(strstr(log_text, "settings: TEST MODE, dev switches on: HALO_PERF_LOG=1 (HALO_FRAME_TIMING=300") &&
		strstr(log_text, "HALO_GXM_RTT_SYNC=0") && strstr(log_text, "XV_FPS=1"), "test mode in halo.log");
	check(!strcmp(getenv("HALO_GXM_RTT_SYNC"), "0"), "settings.txt's dev switch applied at start-up");

	/* a dev switch env.txt sets and settings.txt does not: env.txt's
	value stands */
	unlink(SETTINGS_FILE);
	settings[0].choice = 1;
	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].dev)
			settings[index].choice = 0;
	setenv("HALO_FRAME_PHASE_LOCK", "0", 1);
	setenv("HALO_TARGET_CHAIN_MIN_SIZE", "12", 1);
	unsetenv("HALO_GXM_RTT_SYNC");
	unsetenv("HALO_FRAME_TIMING");
	unsetenv("HALO_RENDER_PROFILE");
	unsetenv("HALO_TICK_PROFILE");
	vita_settings_load();
	check(!strcmp(getenv("HALO_TARGET_CHAIN_MIN_SIZE"), "12") && !strcmp(getenv("HALO_FRAME_PHASE_LOCK"), "0") &&
		choice_of("HALO_FRAME_PHASE_LOCK") == 1, "env.txt's dev values stand (shown in the panel)");

	/* settings.txt of 1.0 loads */
	{
		static const char old[] = "HALO_PROFILE=custom\nXV_FPS=1\nHALO_FRAMERATE_COUNTER=1\nHALO_FRAME_CAP=60\n"
			"HALO_RENDER_SCALE=0.5\nHALO_DISPLAY_WIDTH=640\nHALO_UPSCALE_FILTER=1\nHALO_MODEL_LOD_SCALE=1\n"
			"HALO_MIN_OBJECT_PIXELS=0\nHALO_SCENERY_UPDATE_DIVISOR=1\nHALO_INTERPOLATE_FIRST_PERSON=0\n"
			"HALO_LIGHTING_REFRESH_DIVISOR=1\nHALO_SOUND_CHANNELS=24\nHALO_SOUND_OBSTRUCTION_TICKS=6\n"
			"XV_LOOK_SENS=150\nHALO_CROUCH_TOGGLE=0\nXV_INVERT_Y=1\nXV_DEADZONE=10\n";

		write_file(SETTINGS_FILE, old, sizeof(old) - 1);
		unsetenv("XV_FPS");
		vita_settings_load();
		check(!strcmp(getenv("XV_FPS"), "1") && !strcmp(getenv("HALO_RENDER_SCALE"), "0.5") &&
			!strcmp(getenv("HALO_DISPLAY_WIDTH"), "640") && !strcmp(getenv("XV_LOOK_SENS"), "150") &&
			!strcmp(getenv("XV_INVERT_Y"), "1") && !strcmp(getenv("HALO_SOUND_CHANNELS"), "24") &&
			!strcmp(getenv("HALO_CROUCH_TOGGLE"), "0") && !strcmp(getenv("HALO_FRAME_CAP"), "60") &&
			!strcmp(settings[0].names[settings[0].choice], "Custom"), "a 1.0 settings.txt loads, row for row");
		check(!strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.5") && choice_of("HALO_DYNAMIC_RES_MIN") == 0,
			"a 1.0 settings.txt: Dynamic minimum at its default");
	}

	/* 1.0.3's co-op choice (every game hosted became that level, the
	owner's trap) loads as Off, and is gone from settings.txt at the next
	save; its Online games row loads as Visibility */
	{
		static const char coop[] = "HALO_NET_COOP_LEVEL=a10\nHALO_NET_COOP_DIFFICULTY=2\nHALO_NET_LOBBY_PUBLIC=false\n"
			"HALO_VITA_NETWORK=online\n";

		write_file(SETTINGS_FILE, coop, sizeof(coop) - 1);
		unsetenv("HALO_NET_COOP_LEVEL");
		unsetenv("HALO_NET_COOP_DIFFICULTY");
		unsetenv("HALO_NET_HOST_PUBLIC");
		unsetenv("HALO_NET_LOBBY_PUBLIC");
		log_text[0] = 0;
		vita_settings_load();
		check(!getenv("HALO_NET_COOP_LEVEL") && !getenv("HALO_NET_COOP_DIFFICULTY") &&
			strstr(log_text, "settings: the old co-op setting (a10) is off: co-op is hosted from Campaign"),
			"a 1.0.3 settings.txt with a co-op level: Off (not set), halo.log says where co-op went");
		check(!strcmp(getenv("HALO_NET_HOST_PUBLIC"), "false") && !strcmp(getenv("HALO_NET_LOBBY_PUBLIC"), "false") &&
			choice_of("HALO_NET_HOST_PUBLIC") == 0, "1.0.3's Online games Private: Visibility Private");
		vita_settings_set("HALO_FRAME_CAP", "30");
		check(!strstr(file_text(SETTINGS_FILE), "HALO_NET_COOP") && !strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_PUBLIC") &&
			strstr(file_text(SETTINGS_FILE), "HALO_NET_HOST_PUBLIC=false\n"),
			"the next save: no co-op lines, Visibility under its own name");
		/* (env.txt's co-op level stands: the automated co-op tests) */
		setenv("HALO_NET_COOP_LEVEL", "a30", 1);
		vita_settings_load();
		check(!strcmp(getenv("HALO_NET_COOP_LEVEL"), "a30") && !strstr(file_text(SETTINGS_FILE), "HALO_NET_COOP"),
			"env.txt's co-op level is left alone (tests), and never saved");
		unsetenv("HALO_NET_COOP_LEVEL");
	}

	/* the Play page's texts: a lobby name typed comes back; none typed is
	the Vita's user name, not saved */
	{
		static const char texts[] = "HALO_NET_LOBBY_NAME=Couch\x7f co-op!\nHALO_NET_LOBBY_PASSWORD=pw 1\n";

		write_file(SETTINGS_FILE, texts, sizeof(texts) - 1);
		unsetenv("HALO_NET_LOBBY_NAME");
		unsetenv("HALO_NET_LOBBY_PASSWORD");
		vita_settings_load();
		check(!strcmp(getenv("HALO_NET_LOBBY_NAME"), "Couch co-op!") && !strcmp(getenv("HALO_NET_LOBBY_PASSWORD"), "pw 1"),
			"settings.txt's lobby name (printable characters only) and password at start-up");
		unlink(SETTINGS_FILE);
		unsetenv("HALO_NET_LOBBY_NAME");
		unsetenv("HALO_NET_LOBBY_PASSWORD");
		vita_settings_load();
		vita_settings_set("HALO_FRAME_CAP", "30");
		check(!strcmp(getenv("HALO_NET_LOBBY_NAME"), "vitauser") && !getenv("HALO_NET_LOBBY_PASSWORD")[0] &&
			!strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_NAME") && !strstr(file_text(SETTINGS_FILE), "PASSWORD"),
			"none saved: the lobby name is the Vita's user name (not saved), no password");
		vita_settings_set("HALO_NET_LOBBY_NAME", "Test_Lobby_With_A_Long_Name");
		check(!strcmp(getenv("HALO_NET_LOBBY_NAME"), "Test_Lobby_With") &&
			strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_NAME=Test_Lobby_With\n"),
			"@set HALO_NET_LOBBY_NAME (the harness's): cut to 15, saved");
		vita_settings_set("HALO_NET_LOBBY_NAME", "");
	}

	/* a settings.txt saved with the Performance profile before Sun rays
	was a row: still Performance, the sun rays off as the profile has them */
	{
		static const char performance[] = "HALO_PROFILE=performance\nHALO_RENDER_SCALE=0.5\nHALO_MODEL_LOD_SCALE=0.5\n"
			"HALO_MIN_OBJECT_PIXELS=8\nHALO_SCENERY_UPDATE_DIVISOR=4\nHALO_LIGHTING_REFRESH_DIVISOR=3\n"
			"HALO_SOUND_OBSTRUCTION_TICKS=6\n";
		static const char quality[] = "HALO_PROFILE=quality\nHALO_RENDER_SCALE=1\nHALO_MODEL_LOD_SCALE=1\n"
			"HALO_MIN_OBJECT_PIXELS=0\nHALO_SCENERY_UPDATE_DIVISOR=1\nHALO_LIGHTING_REFRESH_DIVISOR=1\n"
			"HALO_SOUND_OBSTRUCTION_TICKS=1\n";

		write_file(SETTINGS_FILE, performance, sizeof(performance) - 1);
		unsetenv("HALO_SUN_RAYS");
		vita_settings_load();
		check(!strcmp(getenv("HALO_SUN_RAYS"), "0") && !strcmp(settings[0].names[settings[0].choice], "Performance"),
			"a Performance settings.txt from before Sun rays: Performance, sun rays off");
		write_file(SETTINGS_FILE, quality, sizeof(quality) - 1);
		unsetenv("HALO_SUN_RAYS");
		vita_settings_load();
		check(!strcmp(getenv("HALO_SUN_RAYS"), "1") && !strcmp(settings[0].names[settings[0].choice], "Quality"),
			"a Quality settings.txt from before Sun rays: Quality, sun rays on");
	}

	/* a settings.txt with Dynamic loads */
	{
		static const char dynamic[] = "HALO_RENDER_SCALE=dynamic\nHALO_DYNAMIC_RES_MIN=0.625\nHALO_MODEL_LOD_SCALE=0.5\n";

		write_file(SETTINGS_FILE, dynamic, sizeof(dynamic) - 1);
		vita_settings_load();
		check(!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.625") &&
			!strcmp(settings[1].names[settings[1].choice], "Dynamic"), "a settings.txt with Dynamic loads");
	}

	/* PR #7's message overlay (a join refused): wrapped to the menu's width,
	not dismissed by the press that was down when it opened, and taking the
	pad until a new Cross or Circle (tools/test_vita_message.py's checks) */
	{
		static const char text[] = "You are on version 5. The host is on version 9. Please install the updated build.";
		const char *at;
		int longest = 0;

		vita_settings_message("Join rejected", text);
		check(frame(VITA_BUTTON_CROSS) == 1 && menu_visible && strstr(menu, "Join rejected"),
			"message: shown, and the press that was down does not reach the game");
		for (at = menu; *at; )
		{
			int length = (int)strcspn(at, "\n");

			longest = length > longest ? length : longest;
			at += length + (at[length] == '\n');
		}
		check(longest <= 46, "message: lines of 46 characters at most");
		check(frame(VITA_BUTTON_CROSS) == 1 && menu_visible, "message: a held Cross does not close it");
		check(frame(0) == 1 && frame(VITA_BUTTON_CIRCLE) == 1 && !menu_visible, "message: a new Circle closes it");
		check(frame(0) == 0, "message: then the game has the pad again");
	}

	test_controls_tab();
	test_gyro_page();
	test_button_icons();
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures ? 1 : 0;
}
