/*
VITA_SETTINGS_TEST.C

A desktop test of the settings panel (port/vita/host/vita_settings.c,
included whole), with the calls it makes into internet play (p2p.h), ad hoc
play (vita_net.c) and the renderer (vgxm_menu_set) recorded instead: the
panel opened with SELECT+START, the tabs switched with L and R, the
Multiplayer tab (a code typed with the D-pad and joined, the public lobby
listed and joined, Online games switched to Public at once, an ad hoc group
joined, the game seeing no buttons while the system's dialog is up), the
Modded maps tab (a folder of fake maps: listed, turned off and on, deleted
after a confirmation, the map in play kept), the Dev tab behind its switch
(switches saved only while on, the timing variables, Save report's folder),
the Gyro tab (its rows, the gyroscope's line, saved and read back), and a
settings.txt of 1.0 loading. It runs in a folder of its own, with
ux0:data/haloce-vita made there.

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
static int lobby_public = -1, browsing, adhoc_connects, adhoc_mode = -1, adhoc_room = -1, adhoc_state_value;
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

int p2p_hosting_code(char *code, int size)
{
	if (!hosting)
		return 0;
	snprintf(code, (size_t)size, "QX7K-M2PA");
	return 1;
}

void p2p_lobby_set_public(int listed) { lobby_public = listed; }
void p2p_lobby_browse(int on) { browsing = on; }

int p2p_lobby_entry(int index, struct p2p_lobby_entry *entry)
{
	static const struct p2p_lobby_entry entries[] = {
		{ "OWNN-GAME", "this vita", 1, 128, 1, 1 },
		{ "HJ4T-9WXZ", "desktop host", 2, 128, 1, 0 },
	};

	if (!browsing || index >= 2)
		return 0;
	*entry = entries[index];
	return 1;
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

	snprintf(marked, sizeof(marked), "*%s", name);
	for (index = 0; index < 8 && !strstr(menu, marked); index++)
		press(VITA_BUTTON_R);
	return strstr(menu, marked) != NULL;
}

/* down until the selected line starts with `label` */
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

/* ---------- the Controls tab: touch zones, buttons, Reset controls */

/* the menu's lines: how many (the zones' diagram line not counted), the
longest, and the diagram's marks ("" without one) */
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
			/* (the tab bar is drawn apart, not as a row of text) */
			if (at[0] != '\t')
				*longest = length > *longest ? length : *longest;
			count++;
		}
		at += length + (at[length] == '\n');
	}
	return count;
}

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
	char diagram[16];
	int longest, count, index;

	/* (a 1.0 settings.txt was loaded last: no touch or button lines in it) */
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "off") && !strcmp(getenv("HALO_TOUCH_TOP_RIGHT"), "off") &&
		!strcmp(getenv("HALO_XBOX_A"), "cross") && !strcmp(getenv("HALO_XBOX_WHITE"), "right") &&
		!strcmp(getenv("HALO_XBOX_BACK"), "select"), "a 1.0 settings.txt: touch zones Off, Xbox buttons as shipped");
	open_panel();
	to_tab("Controls");
	count = menu_lines(&longest, diagram, sizeof(diagram));
	printf("%s\n--\n", menu);
	check(strstr(menu, "Touch top left") && strstr(menu, "Touch right edge") && strstr(menu, "Rear touch left") &&
		strstr(menu, "Rear touch right") && strstr(menu, "\nA ") && strstr(menu, "\nBlack ") &&
		strstr(menu, "\nLeft trigger ") && strstr(menu, "\nRight stick click ") && strstr(menu, "\nBack ") &&
		strstr(menu, "Reset controls >") && strstr(menu, "Show dev settings") && !strstr(menu, " button"),
		"Controls: the touch zones, the Xbox buttons by name, Reset controls");
	check(count <= 26 && longest <= 46, "Controls: 26 lines at most, each 46 characters at most");
	check(strstr(menu, "\nRear touch guard ") && strstr(menu, "< Normal >") && !strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "normal"),
		"Rear touch guard: Normal as shipped (a 1.0 settings.txt has none)");
	check(!diagram[0], "Look sensitivity chosen: no zone diagram");
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
	check(frame_touch(0, 1UL << VITA_ZONE_REAR_LEFT) == 0, "panel closed: the touch zones reach the game");

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

	/* Reset controls: this tab as shipped, Show dev settings kept */
	open_panel();
	to_tab("Controls");
	to_line("Reset controls");
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "off") && !strcmp(getenv("HALO_TOUCH_TOP_RIGHT"), "off") &&
		!strcmp(getenv("HALO_XBOX_WHITE"), "right") && !strcmp(getenv("XV_LOOK_SENS"), "100") &&
		!strcmp(getenv("HALO_CROUCH_TOGGLE"), "1") && !strcmp(getenv("XV_INVERT_Y"), "0") &&
		strstr(file_text(SETTINGS_FILE), "HALO_TOUCH_REAR_LEFT=off\n") &&
		strstr(file_text(SETTINGS_FILE), "XV_LOOK_SENS=100\n") && strstr(menu, "Controls as shipped") &&
		!strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "normal") &&
		strstr(file_text(SETTINGS_FILE), "HALO_TOUCH_REAR_GUARD=normal\n"),
		"Reset controls: zones Off, the rear guard Normal, buttons, look and crouch as shipped, saved");
	check(!strcmp(getenv("HALO_DEV_SETTINGS"), "1") && strstr(menu, "|Dev"), "Reset controls keeps Show dev settings");
	press(VITA_BUTTON_CIRCLE);
}

/* ---------- the Gyro tab */

static void test_gyro_tab(void)
{
	struct vita_gyro_config config;
	char diagram[16], line[128];
	int longest, count, index;

	/* (a 1.0 settings.txt was loaded: no gyro lines in it) */
	check(!strcmp(getenv("HALO_GYRO"), "off") && !strcmp(getenv("HALO_GYRO_BUTTON"), "l") &&
		!strcmp(getenv("HALO_GYRO_SENS"), "150") && !strcmp(getenv("HALO_GYRO_INVERT_Y"), "0") &&
		!strcmp(getenv("HALO_GYRO_TURN"), "yaw"), "a 1.0 settings.txt: gyro Off, button L, 1.5x, normal, yaw");
	open_panel();
	check(to_tab("Gyro"), "the Gyro tab");
	count = menu_lines(&longest, diagram, sizeof(diagram));
	printf("%s\n--\n", menu);
	check(strstr(menu_line(1, line, sizeof(line)), "Gyro aiming") && strstr(line, "Off") &&
		strstr(menu_line(2, line, sizeof(line)), "Gyro button") && strstr(line, "< L >") &&
		strstr(menu_line(3, line, sizeof(line)), "Gyro sensitivity") && strstr(line, "< 1.5x >") &&
		strstr(menu_line(4, line, sizeof(line)), "Gyro vertical") && strstr(line, "Normal") &&
		strstr(menu_line(5, line, sizeof(line)), "Gyro turning") && strstr(line, "Turn (yaw)") &&
		strstr(menu_line(6, line, sizeof(line)), "Gyro: yaw -999 pitch -999 roll -999"),
		"Gyro: aiming, button, sensitivity, vertical, turning, the gyroscope's line");
	check(count == 8 && longest <= 46 && !diagram[0], "Gyro: 8 lines (tab bar, 5 rows, the line, help), 46 characters at most");
	check(strstr(menu, "Turn the Vita to aim") != NULL, "Gyro aiming's help line");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GYRO"), "on") && strstr(file_text(SETTINGS_FILE), "HALO_GYRO=on\n"),
		"Gyro aiming On: in the environment and settings.txt");
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GYRO"), "hold") && strstr(menu, "< While holding  "), "Gyro aiming: While holding, the last");
	for (index = 0; index < 5; index++)
		press(VITA_BUTTON_DOWN);
	check(!strncmp(menu_line(menu_selected, line, sizeof(line)), "Gyro aiming", 11),
		"down skips the gyroscope's line (five downs: back to the first row)");
	press(VITA_BUTTON_DOWN);
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
		"the pad reads the Gyro tab's choices");
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

	/* Reset controls is the Controls tab's: the gyro's rows stay */
	open_panel();
	to_tab("Controls");
	to_line("Reset controls");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(getenv("HALO_GYRO"), "hold"), "Reset controls leaves the Gyro tab");
	press(VITA_BUTTON_CIRCLE);
}

/* ---------- the Multiplayer tab: Host a game, Join a game */

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

/* every line of the menu 46 characters at most (the tab bar aside) */
static int menu_fits(void)
{
	const char *at = menu;

	while (*at)
	{
		int length = (int)strcspn(at, "\n");

		if (at[0] != '\t' && length > 46)
		{
			printf("too long: %.*s\n", length, at);
			return 0;
		}
		at += length + (at[length] == '\n');
	}
	return 1;
}

static void test_multiplayer_tab(void)
{
	char line[128];

	/* (the network this session runs: Online, from the environment) */
	check(strstr(menu_line(1, line, sizeof(line)), "Host a game >") && strstr(menu_line(2, line, sizeof(line)),
		"Join a game >") && !strncmp(menu_line(3, line, sizeof(line)), "Connection*", 11) && strstr(line, "< Online") &&
		!strncmp(menu_line(4, line, sizeof(line)), "Co-op campaign", 14) &&
		!strncmp(menu_line(5, line, sizeof(line)), "Co-op difficulty", 16),
		"Multiplayer: Host a game, Join a game, Connection, the co-op rows");
	check(!strstr(menu, "Online games") && !strstr(menu, "Join with a code") && !strstr(menu, "Browse public") &&
		!strstr(menu, "Ad hoc room") && !strstr(menu, "Ad hoc dialog"),
		"Multiplayer: internet play's rows and ad hoc's are out of the way");
	check(strstr(menu, "\nThis Vita: vitauser\n") && strstr(menu, "\nNo game yet: host one or join one\n") &&
		strstr(menu, "\nOnline: ready"), "before the game says: this Vita's name, no game, internet play's line");
	check(strstr(menu, "Start a game for other Vitas") != NULL, "Host a game's help line");
	check(menu_fits(), "Multiplayer: lines of 46 characters at most");
	game_status(SYSTEM_LINK_STATE_MENUS, 0, 0, 0);
	printf("%s\n--\n", menu);
	check(strstr(menu, "\nThis Vita: vitauser  192.168.1.23\n") != NULL, "this Vita's address, as the others reach it");

	/* Host a game: the steps, then cross asks the game for System Link */
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(!strncmp(menu, "HOST A GAME", 11) && strstr(menu, "1 The game's System Link screen opens") &&
		strstr(menu, "2 A to join if asked, A on a profile, A again") && strstr(menu, "3 SYSTEM LINK GAMES: Y creates a game") &&
		strstr(menu, "4 A on a map, A on a game type") && strstr(menu, "Menus: A Cross, B Circle, X Square, Y Triangle") &&
		strstr(menu, "Cross: open System Link   Circle: back"), "Host a game: the steps in the game's (Xbox) buttons");
	check(menu_fits() && !strstr(menu, " button"), "the steps: 46 characters a line, buttons by name");
	check(frame(VITA_BUTTON_CROSS) == 1 && halo_system_link_request == SYSTEM_LINK_REQUEST_HOST &&
		strstr(menu, "Opening System Link..."), "cross: the game is asked for its System Link screen (host)");
	/* (the cross still held when the game answers) */
	halo_system_link_answer = SYSTEM_LINK_ANSWER_OPENED;
	halo_system_link_request = SYSTEM_LINK_REQUEST_NONE;
	frame(VITA_BUTTON_CROSS);
	check(!menu_visible && frame(VITA_BUTTON_CROSS) == 1,
		"the game opened it: the panel closes; the cross still held is not the game's A");
	check(frame(0) == 0 && frame(VITA_BUTTON_CROSS) == 0, "let go and pressed again: the game has the pad");
	check(strstr(log_text, "settings: host a game: asked the game for its System Link screen (online)") != NULL,
		"halo.log says so");
	frame(0);

	/* the game's state, in the tab's lines */
	open_panel();
	game_status(SYSTEM_LINK_STATE_SEARCHING, 0, 0, 0);
	check(strstr(menu, "Looking for games: none yet") != NULL, "the System Link list, no game found");
	game_status(SYSTEM_LINK_STATE_SEARCHING, 0, 2, 0);
	check(strstr(menu, "Looking for games: 2 found") != NULL, "the System Link list, two games");
	game_status(SYSTEM_LINK_STATE_HOSTING, 1, 0, 1);
	check(strstr(menu, "Hosting: waiting for players") != NULL, "hosting alone: waiting for players");
	game_status(SYSTEM_LINK_STATE_HOSTING, 3, 0, 1);
	check(strstr(menu, "Hosting: 3 Vitas in the lobby") != NULL, "hosting: three Vitas in the lobby");

	/* hosting: Join a game says why not, and asks nothing */
	to_line("Join a game");
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(!strncmp(menu, "JOIN A GAME", 11) && strstr(menu, "A on the host's game") &&
		strstr(menu, "\n!Already hosting: the game's lobby is open\nCircle: back"),
		"Join a game while hosting: the steps, and why not now");
	check(menu_fits(), "Join's steps: 46 characters a line");
	press(VITA_BUTTON_CROSS);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_NONE, "... and cross asks nothing");
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "*Multiplayer") != NULL, "circle: back to the tab");
	game_status(SYSTEM_LINK_STATE_LOBBY, 2, 0, 0);
	check(strstr(menu, "In a lobby: 2 Vitas, the host starts") != NULL, "in another's lobby");
	game_status(SYSTEM_LINK_STATE_IN_GAME, 4, 0, 1);
	check(strstr(menu, "In a game: 4 Vitas (you host)") != NULL, "in a game this Vita hosts");
	game_status(SYSTEM_LINK_STATE_PLAYING, 0, 0, 0);
	check(strstr(menu, "Playing: quit the level to host or join") != NULL, "playing a level");

	/* the game refuses (it has the last word): the guide says why */
	game_status(SYSTEM_LINK_STATE_MENUS, 0, 0, 0);
	press(VITA_BUTTON_CROSS);
	press(VITA_BUTTON_CROSS);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_JOIN, "Join a game: the game is asked (join)");
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

	/* co-op: the level a hosted game plays together, at once (the server
	reads the environment each frame of its lobby); Host's steps say it */
	to_line("Co-op campaign");
	menu_line(menu_selected, line, sizeof(line));
	check(strstr(line, "Off") != NULL && getenv("HALO_NET_COOP_LEVEL") && !getenv("HALO_NET_COOP_LEVEL")[0],
		"Co-op campaign: Off at first");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_NET_COOP_LEVEL"), "a10") &&
		strstr(menu_line(menu_selected, line, sizeof(line)), "Pillar of Autumn") && strlen(line) <= 46,
		"Co-op campaign: right picks The Pillar of Autumn (a10)");
	press(VITA_BUTTON_DOWN);
	check(!strncmp(menu_line(menu_selected, line, sizeof(line)), "Co-op difficulty", 16) && strstr(line, "Normal") &&
		!strcmp(getenv("HALO_NET_COOP_DIFFICULTY"), "1"), "Co-op difficulty: Normal at first");
	to_line("Host a game");
	press(VITA_BUTTON_CROSS);
	check(strstr(menu, "\n  Co-op: plays Pillar of Autumn, Normal\n") && menu_fits(), "Host's steps name the co-op level");
	press(VITA_BUTTON_CIRCLE);
	to_line("Co-op campaign");
	press(VITA_BUTTON_LEFT);
	check(!getenv("HALO_NET_COOP_LEVEL")[0], "Co-op campaign: left goes back to Off");

	/* Connection: Online is not offered again without Show dev settings */
	to_line("Connection");
	check(strstr(menu, "Internet play by code, Dev tab") != NULL, "Connection Online's help line");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_VITA_NETWORK"), "adhoc") && strstr(menu, "Restart the game"),
		"Connection: Ad hoc asks for a restart");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_VITA_NETWORK"), "adhoc") && strstr(menu_line(menu_selected, line, sizeof(line)),
		"< Ad hoc  "), "... and Online is not offered again (dev settings off)");
	clock_us += 5000000;
	press(VITA_BUTTON_DOWN);
	check(!strstr(menu, "Restart the game"), "the other lines' help again after a few seconds");
	press(VITA_BUTTON_UP);
	check(strstr(menu, "Restart the game") != NULL, "the Connection line still says it");
	check(strstr(menu, "Ad hoc room") && strstr(menu, "Join ad hoc group >") && strstr(menu, "Leave ad hoc group >"),
		"Ad hoc chosen: its rows show");
	to_line("Join ad hoc group");
	press(VITA_BUTTON_CROSS);
	check(adhoc_connects == 0 && strstr(menu, "Connection must be Ad hoc (restart)"),
		"Join ad hoc group waits for the restart");
	to_line("Host a game");
	press(VITA_BUTTON_CROSS);
	check(strstr(menu, "!Restart the game first: Connection changed") != NULL, "Host a game waits for the restart too");
	press(VITA_BUTTON_CIRCLE);

	/* the next start, in ad hoc play */
	restart_pending = 0;
	vita_settings_load();
	check(!strcmp(getenv("HALO_NET_ADHOC"), "true"), "Connection Ad hoc: ad hoc play on");
	clock_us += 600000;
	frame(0);
	check(strstr(menu, "\nThis Vita: vitauser, ad hoc room 1\n") && strstr(menu, "\nAd hoc: not in a group\n"),
		"ad hoc: this Vita's room, not in a group");
	to_line("Ad hoc room");
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_CROSS);
	check(adhoc_connects == 1 && adhoc_mode == 0 && adhoc_room == 2 && !menu_visible,
		"Join ad hoc group opens the dialog (connect, room 2) and closes the panel");
	check(frame(VITA_BUTTON_CROSS) == 1 && frame(0) == 1, "the game sees no buttons while the dialog is up");
	adhoc_state_value = 2;
	check(frame(0) == 0, "and gets them again after");

	/* Host a game in ad hoc play, not in a group: the dialog first, then
	System Link by itself */
	vita_adhoc_leave();
	open_panel();
	to_line("Host a game");
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(strstr(menu, "1 The system's dialog joins ad hoc room 2") && strstr(menu, "2 The game's System Link screen") &&
		menu_fits(), "Host in ad hoc play: the room's group joined first");
	press(VITA_BUTTON_CROSS);
	check(adhoc_connects == 2 && adhoc_room == 2 && !menu_visible && halo_system_link_request == SYSTEM_LINK_REQUEST_NONE,
		"cross: the ad hoc dialog, the panel closed, System Link not asked yet");
	adhoc_state_value = 2;
	frame(0);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_HOST, "in the group: System Link asked for");
	game_answers(SYSTEM_LINK_ANSWER_OPENED);
	check(!menu_visible && frame(0) == 0, "opened: nothing over the game");
	/* ... and a dialog that does not join says so over the game */
	vita_adhoc_leave();
	open_panel();
	to_line("Join a game");
	press(VITA_BUTTON_CROSS);
	press(VITA_BUTTON_CROSS);
	adhoc_state_value = -1;
	frame(0);
	check(menu_visible && strstr(menu, "did not join the ad hoc group") && halo_system_link_request == 0,
		"the dialog did not join: a message, System Link not asked");
	press(VITA_BUTTON_CROSS);
	check(!menu_visible, "cross closes the message");
	adhoc_state_value = 2;
	frame(0);
}

/* internet play's rows, in the Dev tab (it is shown) */
static void test_online_rows(void)
{
	char line[128];

	/* (a session started Online) */
	vita_settings_set("HALO_VITA_NETWORK", "online");
	restart_pending = 0;
	vita_settings_load();
	to_tab("Multiplayer");
	to_line("Connection");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_VITA_NETWORK"), "online"), "Show dev settings on: Connection offers Online");
	restart_pending = 0;
	to_tab("Dev");
	printf("%s\n--\n", menu);
	check(strstr(menu, "Online games") && strstr(menu, "Join with a code >") && strstr(menu, "Browse public games >") &&
		strstr(menu, "Ad hoc dialog"), "Dev: internet play's rows and the ad hoc dialog's");

	/* Online games: Public, at once */
	to_line("Online games");
	press(VITA_BUTTON_RIGHT);
	check(lobby_public == 1 && strstr(menu_line(menu_selected, line, sizeof(line)), "Public"),
		"Online games: Public takes effect at once");

	/* a code typed with the D-pad: B (up from A) on the first, 9 (down
	from A: the digits come first) on the fifth, then cross */
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_CROSS);
	check(!strncmp(menu, "JOIN WITH A CODE", 16), "Join with a code opens the code screen");
	press(VITA_BUTTON_UP);
	for (int index = 0; index < 4; index++)
		press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_DOWN);
	printf("%s\n--\n", menu);
	check(strstr(menu, "B A A A - 9 A A A") != NULL, "the code screen shows the letters typed");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(joined_code, "BAAA-9AAA") && strstr(menu, "*Dev"), "cross joins the code typed");
	check(strstr(menu, "Looking up BAAA-9AAA") != NULL, "the help line says it is looked up");

	/* the public lobby: this Vita's own game is left out */
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_CROSS);
	check(browsing == 1 && !strncmp(menu, "PUBLIC GAMES", 12), "Browse public games starts browsing");
	clock_us += 600000;
	frame(0);
	printf("%s\n--\n", menu);
	check(strstr(menu, "desktop host") && !strstr(menu, "this vita"), "the lobby lists the others' games");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(joined_code, "HJ4T-9WXZ") && browsing == 0, "cross joins the game's code and stops browsing");

	/* hosting: the code shows in the Multiplayer tab */
	hosting = 1;
	to_tab("Multiplayer");
	clock_us += 5000000;
	frame(0);
	check(strstr(menu, "Your code: QX7K-M2PA (public)") != NULL, "hosting online: the Multiplayer tab shows the code");
	check(menu_fits(), "Multiplayer with internet play: 46 characters a line");
	hosting = 0;
	to_tab("Dev");
	to_line("Save report");
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
	check(menu_visible && menu[0] == '\t' && strstr(menu, "*Graphics|Audio|Controls|Gyro|Multiplayer|Modded maps\n"),
		"SELECT+START opens the Graphics tab; six tabs (Dev hidden)");
	check(strstr(menu_line(1, line, sizeof(line)), "Profile") && strstr(menu_line(3, line, sizeof(line)), "Dynamic minimum") &&
		strstr(menu_line(11, line, sizeof(line)), "FPS counter") && strstr(menu_line(12, line, sizeof(line)), "Frame limit"),
		"Graphics: Profile, Render resolution, Dynamic minimum ... FPS counter, Frame limit");
	for (index = 1; index < 13; index++)
		check(strlen(menu_line(index, line, sizeof(line))) <= 46, "a Graphics line fits (46 characters)");
	check(!strstr(menu, "Render resolution*") && !strstr(menu, "Aspect ratio*") && !strstr(menu, "Dynamic minimum*"),
		"resolution, dynamic minimum and aspect: live, no *");
	{
		/* Render resolution's Dynamic: saved as a word, the profile Custom,
		back to a number, and the minimum's row */
		unsigned long generation = halo_settings_generation;

		vita_settings_set("HALO_RENDER_SCALE", "dynamic");
		check(!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && halo_settings_generation != generation &&
			strstr(file_text(SETTINGS_FILE), "HALO_RENDER_SCALE=dynamic\n") &&
			!strcmp(settings[0].names[settings[0].choice], "Custom"),
			"Render resolution Dynamic: in the environment and settings.txt at once, the profile Custom");
		check(strstr(file_text(SETTINGS_FILE), "HALO_DYNAMIC_RES_MIN=0.5\n") != NULL, "Dynamic minimum: 50% by default, saved");
		vita_settings_set("HALO_DYNAMIC_RES_MIN", "0.75");
		check(!strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.75") &&
			strstr(file_text(SETTINGS_FILE), "HALO_DYNAMIC_RES_MIN=0.75\n"), "Dynamic minimum 75%");
		vita_settings_set("HALO_RENDER_SCALE", "0.3");
		check(!strcmp(getenv("HALO_RENDER_SCALE"), "0.5"), "a scale off the list goes to the nearest number, not Dynamic");
		vita_settings_set("HALO_RENDER_SCALE", "0.75");
		vita_settings_set("HALO_DYNAMIC_RES_MIN", "0.5");
		check(!strcmp(settings[0].names[settings[0].choice], "Balanced"), "back to 75%: Balanced again");
	}
	press(VITA_BUTTON_L);
	check(strstr(menu, "*Modded maps") != NULL, "L from the first tab wraps to the last shown");
	press(VITA_BUTTON_R);
	press(VITA_BUTTON_R);
	check(strstr(menu, "*Audio") && strstr(menu, "Sound voices*") && strstr(menu, "Sound occlusion"), "R: Audio");
	press(VITA_BUTTON_R);
	check(strstr(menu, "*Controls") && strstr(menu, "Look sensitivity") && strstr(menu, "Show dev settings"),
		"R: Controls, with Show dev settings");
	press(VITA_BUTTON_R);
	check(!strncmp(menu, "\tGraphics|Audio|Controls|*Gyro|", 31) && strstr(menu, "Gyro aiming"), "R: Gyro");
	press(VITA_BUTTON_R);
	check(!strncmp(menu, "\tGraphics|Audio|Controls|Gyro|*Multiplayer", 42), "R: Multiplayer");
	printf("%s\n--\n", menu);

	test_multiplayer_tab();

	/* ---------- Modded maps */
	write_map("ux0:data/haloce-vita/maps/mygulch.map", 5, 250000);
	write_map("ux0:data/haloce-vita/maps/inplay.map", 5, 2000);
	write_map("ux0:data/haloce-vita/maps/cemap.map", 609, 3 * 1024 * 1024 / 16);
	write_map("ux0:data/haloce-vita/maps/bloodgulch.map", 5, 1000);
	write_map("ux0:data/haloce-vita/maps/bitmaps.map", 1, 1000);
	write_file("ux0:data/haloce-vita/maps/cemap.bmp", "BM", 2);
	write_file("ux0:data/haloce-vita/maps/notes.txt", "x", 1);
	open_panel();
	check(to_tab("Modded maps"), "the Modded maps tab");
	printf("%s\n--\n", menu);
	check(strstr(menu, "PC maps") && strstr(menu, "!Missing: sounds.map loc.map"),
		"PC maps switch; a CE map without sounds.map/loc.map: the warning");
	check(!strncmp(menu_line(3, line, sizeof(line)), "cemap ", 6) && !strncmp(menu_line(4, line, sizeof(line)), "inplay ", 7) &&
		!strncmp(menu_line(5, line, sizeof(line)), "mygulch ", 8) && menu_line(7, line, sizeof(line))[0] == 0,
		"the custom maps listed in order, not the Xbox's or CE resource maps");
	check(strstr(menu, "mygulch                245 KB  Xbox  On") != NULL, "mygulch: size, Xbox, On");
	check(strstr(menu, " CE    On") != NULL, "cemap: Custom Edition");
	to_line("mygulch");
	check(strstr(menu, "Xbox map: left/right on or off") != NULL, "a map's help line");
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
	check(file_exists("ux0:data/haloce-vita/maps/cemap.map") && strstr(menu, "*Modded maps"), "circle keeps it");
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

	/* ---------- Dev, behind its switch */
	press(VITA_BUTTON_R);
	check(strstr(menu, "*Graphics") != NULL, "R from Modded maps: Graphics (Dev hidden)");
	to_tab("Controls");
	to_line("Show dev settings");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "|Modded maps|Dev\n") != NULL, "Show dev settings: the Dev tab");
	to_tab("Dev");
	printf("%s\n--\n", menu);
	check(strstr(menu, "Performance logging*") && strstr(menu, "Crash dump on hang") && strstr(menu, "FPS overlay") &&
		strstr(menu, "GPU W clamp*") && strstr(menu, "Target mip minimum*") && strstr(menu, "Frame phase lock*") &&
		strstr(menu, "Render target sync*") && strstr(menu, "Save report >"), "Dev: the switches, start-up ones marked *");
	check(!strstr(file_text(SETTINGS_FILE), "HALO_GXM_RTT_SYNC") && !strstr(file_text(SETTINGS_FILE), "XV_FPS"),
		"dev switches off: not in settings.txt");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_FRAME_TIMING"), "300") && !strcmp(getenv("HALO_RENDER_PROFILE"), "1") &&
		!strcmp(getenv("HALO_TICK_PROFILE"), "1") && strstr(file_text(SETTINGS_FILE), "HALO_PERF_LOG=1\n") &&
		strstr(menu, "Restart the game"), "Performance logging: the three timing variables, saved, a restart");
	clock_us += 5000000;
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_HANG_CRASH"), "1") && !strstr(menu, "Restart the game"), "Crash dump on hang: live");
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	check(overlay_level == 2 && !strcmp(getenv("XV_FPS"), "2"), "FPS overlay: FPS only, live");
	press(VITA_BUTTON_RIGHT);
	check(overlay_level == 1 && strstr(file_text(SETTINGS_FILE), "XV_FPS=1\n"), "FPS overlay: Full");
	to_line("Render target sync");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GXM_RTT_SYNC"), "0") && strstr(file_text(SETTINGS_FILE), "HALO_GXM_RTT_SYNC=0\n"),
		"Render target sync Off: 0, saved");
	press(VITA_BUTTON_LEFT);
	check(!getenv("HALO_GXM_RTT_SYNC") && !strstr(file_text(SETTINGS_FILE), "HALO_GXM_RTT_SYNC"),
		"back On: unset (the default), not saved");
	press(VITA_BUTTON_RIGHT);
	restart_pending = 0;
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
	for (index = 1; index < 12; index++)
		check(strlen(menu_line(index, line, sizeof(line))) <= 64, "a Dev line fits");
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
	test_gyro_tab();
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures ? 1 : 0;
}
