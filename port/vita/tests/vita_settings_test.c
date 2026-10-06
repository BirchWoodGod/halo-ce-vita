/*
VITA_SETTINGS_TEST.C

A desktop test of the settings panel's multiplayer page
(port/vita/host/vita_settings.c, included whole), with the calls it makes
into internet play (p2p.h), ad hoc play (vita_net.c) and the renderer
(vgxm_menu_set) recorded instead: the panel opened with SELECT+START, the
Multiplayer page, a code typed with the D-pad and joined, the public lobby
listed and joined, Online games switched to Public at once, an ad hoc group
joined, and the game seeing no buttons while the system's dialog is up.

Run port/vita/tests/run_vita_settings_test.sh.
*/

#include "../host/vita_settings.c"

#include <stdio.h>

volatile unsigned long halo_settings_generation;

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

void vgxm_overlay_enable(int enabled) { (void)enabled; }
void vita_host_log(const char *line) { (void)line; }

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

int main(void)
{
	char line[128];
	int index;

	setenv("HALO_VITA_NETWORK", "online", 1);
	vita_settings_load();
	check(!strcmp(getenv("HALO_NET_ONLINE"), "true") && !strcmp(getenv("HALO_NET_ALLOW_UPNP"), "false"),
		"Network Online: internet play on, without UPnP");
	check(!strcmp(getenv("HALO_NET_LOBBY_NAME"), "vitauser"), "the lobby name is the Vita's user name");

	check(!frame(0), "closed: the game gets the buttons");
	open_panel();
	check(menu_visible && !strncmp(menu, "SETTINGS", 8), "SELECT+START opens the settings");
	/* (Multiplayer is the last line: up from the first wraps to it) */
	press(VITA_BUTTON_UP);
	check(strstr(menu_line(menu_selected, line, sizeof(line)), "Multiplayer >") != NULL, "the last line is Multiplayer");
	press(VITA_BUTTON_CROSS);
	check(!strncmp(menu, "MULTIPLAYER", 11), "cross opens the Multiplayer page");
	printf("%s\n--\n", menu);

	/* Online games: Public, at once */
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	check(lobby_public == 1 && strstr(menu_line(2, line, sizeof(line)), "Public"), "Online games: Public takes effect at once");

	/* a code typed with the D-pad: B (up from A) on the first, 9 (down
	from A: the digits come first) on the fifth, then cross */
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_CROSS);
	check(!strncmp(menu, "JOIN WITH A CODE", 16), "Join with a code opens the code screen");
	press(VITA_BUTTON_UP);
	for (index = 0; index < 4; index++)
		press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_DOWN);
	printf("%s\n--\n", menu);
	check(strstr(menu, "B A A A - 9 A A A") != NULL, "the code screen shows the letters typed");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(joined_code, "BAAA-9AAA") && !strncmp(menu, "MULTIPLAYER", 11), "cross joins the code typed");
	check(strstr(menu, "Looking up BAAA-9AAA") != NULL, "the status line says it is looked up");

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

	/* hosting: the code shows */
	hosting = 1;
	clock_us += 5000000;
	frame(0);
	check(strstr(menu, "Your code: QX7K-M2PA (public)") != NULL, "hosting, the status line shows the code");

	/* ad hoc needs the network chosen first */
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_CROSS);
	check(adhoc_connects == 0 && strstr(menu, "Network must be Ad hoc"), "Join ad hoc group asks for the Ad hoc network");

	/* Network: a restart */
	while (strncmp(menu_line(menu_selected, line, sizeof(line)), "Network", 7))
		press(VITA_BUTTON_UP);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_VITA_NETWORK"), "adhoc") && strstr(menu, "Restart the game"), "Network: Ad hoc asks for a restart");

	/* the next start, in ad hoc play */
	restart_pending = 0;
	vita_settings_load();
	check(!strcmp(getenv("HALO_NET_ADHOC"), "true"), "Network Ad hoc: ad hoc play on");
	while (strncmp(menu_line(menu_selected, line, sizeof(line)), "Ad hoc room", 11))
		press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_CROSS);
	check(adhoc_connects == 1 && adhoc_mode == 0 && adhoc_room == 2 && !menu_visible,
		"Join ad hoc group opens the dialog (connect, room 2) and closes the panel");
	check(frame(VITA_BUTTON_CROSS) == 1 && frame(0) == 1, "the game sees no buttons while the dialog is up");
	adhoc_state_value = 2;
	check(frame(0) == 0, "and gets them again after");

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

	printf("-- %d of %d checks failed\n", failures, checks);
	return failures ? 1 : 0;
}
