/*
VITA_SETTINGS.C

The settings panel: hold SELECT and START together for a moment, in play
or in the menus, and a panel over the game lists the Vita's quality and
control settings. Up and down choose one, left and right change it, circle
(or SELECT+START again) closes the panel; the game sees no buttons while it
is open. Each setting is one of the environment variables the port already
reads (HALO_MODEL_LOD_SCALE...), kept in ux0:data/haloce-vita/settings.txt
and set before the game starts; a change bumps halo_settings_generation,
and the readers that can take a new value mid-game read theirs again (the
render resolution sizes the screen's targets once: it waits for a restart).

The release's defaults, the ones measured best on the Vita, are set here
too, under whatever env.txt and settings.txt say.

Multiplayer is a page of its own (the last line opens it), with three ways
to play beyond the Wi-Fi network's system link, each handing off to the
game's own System Link screen:

- Online (internet play, port/linux/src/p2p.c): hosting a System Link game
  shows its short code here (ABCD-EFGH) for others to type in; "Online
  games: Public" also lists it in the public lobby. "Join with a code"
  types one in with the D-pad; "Browse public games" lists the lobby. The
  host's game then shows under System Link.
- Ad hoc (p2p_adhoc.c, vita_net.c): "Join ad hoc group" opens the system's
  ad hoc dialog for the room's group; the Vitas in it then see each
  other's games under System Link.
- The network ("Network": Wi-Fi, Online, Ad hoc) applies after a restart:
  internet play starts with the game's networking. Wi-Fi, the default, is
  the system link that works on hardware; the other two are not yet
  verified there.
*/

#include <psp2/apputil.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/system_param.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p2p.h"
#include "vita_gxm.h"
#include "vita_host.h"

#define SETTINGS_FILE "ux0:data/haloce-vita/settings.txt"
#define MAXIMUM_CHOICES 6
/* a code's characters as typed (p2p.h shows them ABCD-EFGH) */
#define P2P_CODE_LENGTH_TYPED 8

/* read again by the readers that take a change mid-game (port_config.c) */
extern volatile unsigned long halo_settings_generation;

enum
{
	PAGE_SETTINGS,
	PAGE_MULTIPLAYER,
};

enum
{
	/* a line whose value left and right change (the default) */
	KIND_CHOICE,
	/* a line that does something when chosen (cross, or right) */
	KIND_ACTION,
};

enum
{
	ACTION_NONE,
	ACTION_MULTIPLAYER,
	ACTION_BACK,
	ACTION_JOIN_CODE,
	ACTION_BROWSE,
	ACTION_ADHOC_JOIN,
	ACTION_ADHOC_LEAVE,
};

struct setting
{
	const char *label;
	const char *variable;
	int restart;
	int count;
	const char *values[MAXIMUM_CHOICES];
	const char *names[MAXIMUM_CHOICES];
	const char *help;
	int choice;
	int page;
	int kind;
	int action;
};

static struct setting settings[] = {
	{ "Performance overlay", "XV_FPS", 0, 2, { "0", "1" }, { "Off", "On" },
		"Frames per second and frame times, top right", 0 },
	{ "FPS counter", "HALO_FRAMERATE_COUNTER", 0, 2, { "0", "1" }, { "Off", "On" },
		"The game's frame counter, bottom right", 0 },
	{ "Frame limit", "HALO_FRAME_CAP", 0, 3, { "30", "60", "0" }, { "30 FPS", "60 FPS", "Off" },
		"The most frames shown a second", 0 },
	{ "Render resolution", "HALO_RENDER_SCALE", 1, 5, { "1", "0.875", "0.75", "0.625", "0.5" },
		{ "100%", "88%", "75%", "63%", "50%" }, "Lower is faster and softer (after a restart)", 2 },
	{ "Model detail", "HALO_MODEL_LOD_SCALE", 0, 4, { "1", "0.75", "0.5", "0.35" },
		{ "High", "Medium", "Low", "Lowest" }, "Level of detail of characters and vehicles", 2 },
	{ "Hide distant objects", "HALO_MIN_OBJECT_PIXELS", 0, 4, { "0", "4", "8", "12" },
		{ "Off", "Tiny", "Small", "Medium" }, "Skip objects this small on screen", 2 },
	{ "Scenery updates", "HALO_SCENERY_UPDATE_DIVISOR", 0, 3, { "1", "2", "4" },
		{ "Every tick", "Half", "Quarter" }, "How often static props are updated", 2 },
	{ "Object lighting", "HALO_LIGHTING_REFRESH_DIVISOR", 0, 3, { "1", "2", "3" },
		{ "Full", "Half", "Third" }, "How often object lighting is recomputed", 2 },
	{ "Sound voices", "HALO_SOUND_CHANNELS", 1, 4, { "16", "24", "32", "0" },
		{ "16", "24", "32", "Original" }, "Fewer is faster; the AI then differs (after a restart)", 3 },
	{ "Sound occlusion", "HALO_SOUND_OBSTRUCTION_TICKS", 0, 3, { "1", "3", "6" },
		{ "Every tick", "Every 3rd", "Every 6th" }, "How often muffling behind walls is rechecked", 1 },
	{ "Look sensitivity", "XV_LOOK_SENS", 0, 6, { "50", "75", "100", "125", "150", "200" },
		{ "50%", "75%", "100%", "125%", "150%", "200%" }, "Right stick turning speed", 2 },
	{ "Crouch", "HALO_CROUCH_TOGGLE", 0, 2, { "1", "0" }, { "Toggle", "Hold" },
		"D-pad down: a press crouches, the next stands (Toggle)", 0 },
	{ "Invert look", "XV_INVERT_Y", 0, 2, { "0", "1" }, { "No", "Yes" }, "Reverse the right stick's up and down", 0 },
	{ "Stick deadzone", "XV_DEADZONE", 0, 4, { "0", "5", "10", "15" }, { "Off", "5%", "10%", "15%" },
		"Raise if the sticks drift", 0 },
	{ "Multiplayer", NULL, 0, 0, { NULL }, { NULL }, "Wi-Fi, online and ad hoc play", 0, PAGE_SETTINGS, KIND_ACTION,
		ACTION_MULTIPLAYER },

	{ "Network", "HALO_VITA_NETWORK", 1, 3, { "wifi", "online", "adhoc" }, { "Wi-Fi", "Online", "Ad hoc" },
		"This network / the internet / Vitas nearby", 0, PAGE_MULTIPLAYER },
	{ "Online games", "HALO_NET_LOBBY_PUBLIC", 0, 2, { "false", "true" }, { "Private", "Public" },
		"Private: join by code. Public: listed for all", 0, PAGE_MULTIPLAYER },
	{ "Join with a code", NULL, 0, 0, { NULL }, { NULL }, "Type the code another player's game shows", 0,
		PAGE_MULTIPLAYER, KIND_ACTION, ACTION_JOIN_CODE },
	{ "Browse public games", NULL, 0, 0, { NULL }, { NULL }, "The games listed in the public lobby", 0,
		PAGE_MULTIPLAYER, KIND_ACTION, ACTION_BROWSE },
	{ "Ad hoc room", "HALO_ADHOC_ROOM", 0, 4, { "1", "2", "3", "4" }, { "1", "2", "3", "4" },
		"Vitas in the same room play together", 0, PAGE_MULTIPLAYER },
	{ "Ad hoc dialog", "HALO_ADHOC_DIALOG_MODE", 0, 3, { "0", "1", "2" }, { "Connect", "Create", "Join" },
		"How the system dialog joins: try Connect first", 0, PAGE_MULTIPLAYER },
	{ "Join ad hoc group", NULL, 0, 0, { NULL }, { NULL }, "Opens the system's ad hoc dialog", 0, PAGE_MULTIPLAYER,
		KIND_ACTION, ACTION_ADHOC_JOIN },
	{ "Leave ad hoc group", NULL, 0, 0, { NULL }, { NULL }, "Back to no group", 0, PAGE_MULTIPLAYER, KIND_ACTION,
		ACTION_ADHOC_LEAVE },
	{ "Back", NULL, 0, 0, { NULL }, { NULL }, "To the settings", 0, PAGE_MULTIPLAYER, KIND_ACTION, ACTION_BACK },
};

#define SETTING_COUNT ((int)(sizeof(settings) / sizeof(settings[0])))

/* the release's fixed defaults (not in the panel) */
static const char *const fixed_defaults[][2] = {
	{ "HALO_TICK_THREAD", "1" },
	{ "HALO_INTERPOLATION", "false" },
	{ "HALO_NO_VSYNC", "1" },
	{ "HALO_STATIC_SCENERY", "1" },
	/* (the netcode is the platform's default, distributed: lockstep had
	been the Vita's because a match's client could not reach its own host -
	that was the Vita refusing a sendto on a connected datagram socket,
	vita_net.c; with it fixed the distributed netcode plays, with less
	waiting each frame. HALO_NETCODE=lockstep in env.txt plays the Xbox's) */
};

static int panel_open, selected;
static unsigned long long both_since, last_move, last_shown;
static unsigned long previous_buttons;
static int restart_pending;

/* the page shown, and the screens a multiplayer action opens */
static int page;
enum
{
	SCREEN_LIST,
	SCREEN_CODE,
	SCREEN_BROWSE,
};
static int screen;
/* the code being typed (eight characters of P2P_CODE_ALPHABET) and the
character the cursor is on */
static char code_typed[P2P_CODE_LENGTH_TYPED + 1] = "AAAAAAAA";
static int code_cursor;
/* the public lobby's entries as last shown, and the one chosen */
#define BROWSE_LINES 8
static struct p2p_lobby_entry browse_entries[BROWSE_LINES];
static int browse_count, browse_selected;
/* a line about the last action (a code looked up ...), shown until the
status has more to say */
static char notice[64];
static unsigned long long notice_until;
/* the network the game started with (HALO_VITA_NETWORK at load): what
internet or ad hoc play can do this session */
static char running_network[8] = "wifi";

static unsigned long long now_us(void)
{
	return sceKernelGetProcessTimeWide();
}

/* a line on the last action, shown in the status line for a few seconds */
static void set_notice(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(notice, sizeof(notice), format, arguments);
	va_end(arguments);
	notice_until = now_us() + 4000000ULL;
}

static int find_choice(const struct setting *setting, const char *value)
{
	int index;

	for (index = 0; index < setting->count; index++)
		if (strcmp(setting->values[index], value) == 0)
			return index;
	/* (a value not on the list, from env.txt: the nearest by number) */
	{
		double wanted = atof(value), best_distance = 1e30;
		int best = setting->choice;

		for (index = 0; index < setting->count; index++)
		{
			double distance = atof(setting->values[index]) - wanted;

			if (distance < 0)
				distance = -distance;
			if (distance < best_distance)
			{
				best_distance = distance;
				best = index;
			}
		}
		return best;
	}
}

static struct setting *setting_of(const char *variable)
{
	int index;

	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].variable && strcmp(settings[index].variable, variable) == 0)
			return &settings[index];
	return NULL;
}

static void save(void)
{
	FILE *file = fopen(SETTINGS_FILE, "w");
	int index;

	if (!file)
		return;
	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].kind == KIND_CHOICE)
			fprintf(file, "%s=%s\n", settings[index].variable, settings[index].values[settings[index].choice]);
	fclose(file);
}

/* the chosen network as the platform layer's settings (port_config.c):
internet play on, or ad hoc play, which needs no internet. UPnP (asking
the router to forward internet play's port: posix_upnp.c, built on
newlib's sockets) stays off until it is seen working on a Vita;
HALO_NET_ALLOW_UPNP=true in env.txt turns it on */
static void apply_network(void)
{
	const char *network = getenv("HALO_VITA_NETWORK");

	snprintf(running_network, sizeof(running_network), "%s", network ? network : "wifi");
	if (!strcmp(running_network, "online"))
	{
		setenv("HALO_NET_ONLINE", "true", 1);
		setenv("HALO_NET_ALLOW_UPNP", "false", 0);
	}
	else if (!strcmp(running_network, "adhoc"))
	{
		setenv("HALO_NET_ADHOC", "true", 1);
	}
	{
		/* the public lobby shows the Vita's user name for its games */
		char name[SCE_SYSTEM_PARAM_USERNAME_MAXSIZE + 1];

		memset(name, 0, sizeof(name));
		if (sceAppUtilSystemParamGetString(SCE_SYSTEM_PARAM_ID_USERNAME, (SceChar8 *)name, sizeof(name) - 1) >= 0 &&
			name[0])
			setenv("HALO_NET_LOBBY_NAME", name, 0);
	}
	{
		char line[96];

		snprintf(line, sizeof(line), "vita: network %s (settings panel, Multiplayer)", running_network);
		vita_host_log(line);
	}
}

void vita_settings_load(void)
{
	FILE *file = fopen(SETTINGS_FILE, "r");
	char line[256];
	int index;

	/* env.txt (already read) gives a starting choice; settings.txt, the
	panel's own file, has the last word */
	for (index = 0; index < SETTING_COUNT; index++)
	{
		const char *value = settings[index].variable ? getenv(settings[index].variable) : NULL;

		if (value && settings[index].kind == KIND_CHOICE)
			settings[index].choice = find_choice(&settings[index], value);
	}
	if (file)
	{
		while (fgets(line, sizeof(line), file))
		{
			char *equals = strchr(line, '=');
			char *end = line + strlen(line);
			struct setting *setting;

			while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '))
				*--end = 0;
			if (!equals)
				continue;
			*equals = 0;
			setting = setting_of(line);
			if (setting && setting->kind == KIND_CHOICE)
				setting->choice = find_choice(setting, equals + 1);
		}
		fclose(file);
	}
	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].kind == KIND_CHOICE)
			setenv(settings[index].variable, settings[index].values[settings[index].choice], 1);
	for (index = 0; index < (int)(sizeof(fixed_defaults) / sizeof(fixed_defaults[0])); index++)
		setenv(fixed_defaults[index][0], fixed_defaults[index][1], 0);
	apply_network();
}

static int choice_of(const char *variable)
{
	struct setting const *setting = setting_of(variable);

	return setting ? setting->choice : 0;
}

/* one short line on the network this session plays on */
static void status_line(char *text, int size)
{
	char code[P2P_CODE_SIZE];
	char detail[96];

	if (notice[0] && now_us() < notice_until)
		snprintf(text, (size_t)size, "%s", notice);
	else if (!strcmp(running_network, "online"))
	{
		if (p2p_hosting_code(code, sizeof(code)))
			snprintf(text, (size_t)size, "Your code: %s%s", code, choice_of("HALO_NET_LOBBY_PUBLIC") ? " (public)" : "");
		else
		{
			p2p_status(detail, sizeof(detail));
			snprintf(text, (size_t)size, "Online: %.26s", detail);
		}
	}
	else if (!strcmp(running_network, "adhoc"))
	{
		int state = vita_adhoc_state(detail, sizeof(detail));

		if (state == 2)
		{
			char group[64];

			p2p_adhoc_status(group, sizeof(group));
			snprintf(text, (size_t)size, "Ad hoc: %.27s", group);
		}
		else
		{
			snprintf(text, (size_t)size, "%s", state == 1 ? "Ad hoc: joining..." : state == -1 ? "Ad hoc: did not join" :
				"Ad hoc: not in a group");
		}
	}
	else
	{
		snprintf(text, (size_t)size, "Wi-Fi: system link on this network");
	}
}

/* the longer line under it: what the selected line does, or why the
last action did not work */
static void help_line(char *text, int size, const struct setting *setting)
{
	char detail[96];

	if (restart_pending)
		snprintf(text, (size_t)size, "Restart the game for this change. O: close");
	else if (page == PAGE_MULTIPLAYER && !strcmp(running_network, "online") && (setting->action == ACTION_JOIN_CODE ||
		setting->action == ACTION_BROWSE || setting->action == ACTION_NONE) && p2p_status(detail, sizeof(detail)))
		snprintf(text, (size_t)size, "%.46s", detail);
	else if (page == PAGE_MULTIPLAYER && !strcmp(running_network, "adhoc") &&
		(setting->action == ACTION_ADHOC_JOIN || setting->action == ACTION_ADHOC_LEAVE))
	{
		vita_adhoc_state(detail, sizeof(detail));
		snprintf(text, (size_t)size, "%.46s", detail);
	}
	else
		snprintf(text, (size_t)size, "%s", setting->help);
}

/* the lines of the page shown: their settings' indices */
static int page_lines(int *lines)
{
	int count = 0, index;

	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].page == page)
			lines[count++] = index;
	return count;
}

static void show_list(void)
{
	char text[2048];
	int lines[SETTING_COUNT];
	int count = page_lines(lines), length, index;
	char status[64], help[96];

	if (selected >= count)
		selected = count - 1;
	length = snprintf(text, sizeof(text), "%s", page == PAGE_MULTIPLAYER ? "MULTIPLAYER" : "SETTINGS");
	for (index = 0; index < count && length < (int)sizeof(text); index++)
	{
		const struct setting *setting = &settings[lines[index]];

		if (setting->kind == KIND_ACTION)
			length += snprintf(text + length, sizeof(text) - length, "\n%s%s", setting->label,
				setting->action == ACTION_BACK ? "" : " >");
		else
			length += snprintf(text + length, sizeof(text) - length, "\n%-22s%c %s %c", setting->label,
				setting->choice > 0 ? '<' : ' ', setting->names[setting->choice],
				setting->choice < setting->count - 1 ? '>' : ' ');
	}
	if (page == PAGE_MULTIPLAYER && length < (int)sizeof(text))
	{
		status_line(status, sizeof(status));
		length += snprintf(text + length, sizeof(text) - length, "\n%s", status);
	}
	help_line(help, sizeof(help), &settings[lines[selected]]);
	if (length < (int)sizeof(text))
		snprintf(text + length, sizeof(text) - length, "\n%s", help);
	vgxm_menu_set(text, selected + 1);
}

static void show_code(void)
{
	char text[512], letters[32], cursor[32];
	int index, column = 0;

	/* "A B C D - E F G H", and a caret under the cursor's */
	for (index = 0; index < P2P_CODE_LENGTH_TYPED; index++)
	{
		if (index == 4)
		{
			letters[column] = '-';
			cursor[column++] = ' ';
			letters[column] = ' ';
			cursor[column++] = ' ';
		}
		letters[column] = code_typed[index];
		cursor[column++] = index == code_cursor ? '^' : ' ';
		letters[column] = ' ';
		cursor[column++] = ' ';
	}
	letters[column] = cursor[column] = 0;
	snprintf(text, sizeof(text), "JOIN WITH A CODE\n\n    %s\n    %s\n%s\nUp/down letter  L/R move  X join  O back",
		letters, cursor, strcmp(running_network, "online") ? "Network must be Online (restart)" : "");
	vgxm_menu_set(text, 2);
}

static void show_browse(void)
{
	char text[2048];
	int length, index;
	char detail[64];

	length = snprintf(text, sizeof(text), "PUBLIC GAMES");
	if (strcmp(running_network, "online"))
		length += snprintf(text + length, sizeof(text) - length, "\nNetwork must be Online (restart)");
	else if (!browse_count)
	{
		p2p_status(detail, sizeof(detail));
		length += snprintf(text + length, sizeof(text) - length, "\nLooking for games (%.12s)", detail);
	}
	for (index = 0; index < browse_count && length < (int)sizeof(text); index++)
		length += snprintf(text + length, sizeof(text) - length, "\n%-15.15s %3d %s%s", browse_entries[index].name,
			browse_entries[index].players, browse_entries[index].code, browse_entries[index].compatible ? "" : " (old)");
	if (length < (int)sizeof(text))
		snprintf(text + length, sizeof(text) - length, "\nName, machines, code. X: join. O: back");
	vgxm_menu_set(text, browse_count ? browse_selected + 1 : 0);
}

static void show(void)
{
	last_shown = now_us();
	if (screen == SCREEN_CODE)
		show_code();
	else if (screen == SCREEN_BROWSE)
		show_browse();
	else
		show_list();
}

static void change(struct setting *setting, int step)
{
	int choice = setting->choice + step;

	if (choice < 0 || choice >= setting->count)
		return;
	setting->choice = choice;
	setenv(setting->variable, setting->values[choice], 1);
	if (setting->restart)
		restart_pending = 1;
	if (strcmp(setting->variable, "XV_FPS") == 0)
		vgxm_overlay_enable(choice != 0);
	/* (listed or not takes effect at once, also while hosting) */
	if (strcmp(setting->variable, "HALO_NET_LOBBY_PUBLIC") == 0)
		p2p_lobby_set_public(choice);
	__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
	save();
}

static void close_panel(void)
{
	if (screen == SCREEN_BROWSE)
		p2p_lobby_browse(0);
	panel_open = 0;
	screen = SCREEN_LIST;
	vgxm_menu_set(NULL, 0);
}

static void act(const struct setting *setting)
{
	switch (setting->action)
	{
	case ACTION_MULTIPLAYER:
		page = PAGE_MULTIPLAYER;
		selected = 0;
		break;
	case ACTION_BACK:
		page = PAGE_SETTINGS;
		selected = 0;
		break;
	case ACTION_JOIN_CODE:
		screen = SCREEN_CODE;
		break;
	case ACTION_BROWSE:
		screen = SCREEN_BROWSE;
		browse_count = browse_selected = 0;
		p2p_lobby_browse(1);
		break;
	case ACTION_ADHOC_JOIN:
		if (strcmp(running_network, "adhoc"))
		{
			set_notice("Network must be Ad hoc (restart)");
			break;
		}
		/* (the panel closes: the system's dialog takes the screen) */
		close_panel();
		vita_adhoc_connect(choice_of("HALO_ADHOC_DIALOG_MODE"), choice_of("HALO_ADHOC_ROOM") + 1);
		return;
	case ACTION_ADHOC_LEAVE:
		vita_adhoc_leave();
		break;
	}
}

/* the code screen's buttons */
static void code_input(unsigned long pressed, unsigned long buttons, unsigned long long now)
{
	static const char alphabet[] = P2P_CODE_ALPHABET;
	int step = 0;

	if (pressed & VITA_BUTTON_CIRCLE)
	{
		screen = SCREEN_LIST;
		return;
	}
	if (pressed & VITA_BUTTON_CROSS)
	{
		char code[P2P_CODE_SIZE];

		snprintf(code, sizeof(code), "%.4s-%.4s", code_typed, code_typed + 4);
		if (strcmp(running_network, "online"))
			set_notice("Network must be Online (restart)");
		else
		{
			p2p_join_code(code);
			set_notice("Looking up %s...", code);
		}
		screen = SCREEN_LIST;
		return;
	}
	if (pressed & VITA_BUTTON_LEFT)
		code_cursor = (code_cursor + P2P_CODE_LENGTH_TYPED - 1) % P2P_CODE_LENGTH_TYPED;
	if (pressed & VITA_BUTTON_RIGHT)
		code_cursor = (code_cursor + 1) % P2P_CODE_LENGTH_TYPED;
	if (pressed & VITA_BUTTON_UP)
		step = 1;
	else if (pressed & VITA_BUTTON_DOWN)
		step = -1;
	else if ((buttons & (VITA_BUTTON_UP | VITA_BUTTON_DOWN)) && now - last_move > 150000)
		step = (buttons & VITA_BUTTON_UP) ? 1 : -1;
	if (step)
	{
		const char *at = strchr(alphabet, code_typed[code_cursor]);
		int position = at ? (int)(at - alphabet) : 0;
		int size = (int)sizeof(alphabet) - 1;

		code_typed[code_cursor] = alphabet[(position + step + size) % size];
		last_move = now;
	}
}

/* the public lobby screen's buttons */
static void browse_input(unsigned long pressed)
{
	if (pressed & VITA_BUTTON_CIRCLE)
	{
		p2p_lobby_browse(0);
		screen = SCREEN_LIST;
		return;
	}
	if ((pressed & VITA_BUTTON_UP) && browse_selected > 0)
		browse_selected--;
	if ((pressed & VITA_BUTTON_DOWN) && browse_selected < browse_count - 1)
		browse_selected++;
	if ((pressed & VITA_BUTTON_CROSS) && browse_selected < browse_count)
	{
		p2p_join_code(browse_entries[browse_selected].code);
		set_notice("Joining %.20s...", browse_entries[browse_selected].name);
		p2p_lobby_browse(0);
		screen = SCREEN_LIST;
	}
}

/* the public lobby's entries again (not this machine's own game) */
static void browse_refresh(void)
{
	struct p2p_lobby_entry entry;
	int index;

	browse_count = 0;
	for (index = 0; browse_count < BROWSE_LINES && p2p_lobby_entry(index, &entry); index++)
		if (!entry.own)
			browse_entries[browse_count++] = entry;
	if (browse_selected >= browse_count)
		browse_selected = browse_count ? browse_count - 1 : 0;
}

int vita_settings_input(const struct vita_host_pad *pad)
{
	unsigned long buttons = pad->buttons;
	unsigned long pressed = buttons & ~previous_buttons;
	int both = (buttons & VITA_BUTTON_SELECT) && (buttons & VITA_BUTTON_START);
	unsigned long long now = now_us();

	previous_buttons = buttons;
	/* (the system's ad hoc dialog reads the pad itself: the game must not
	act on the same presses) */
	if (vita_adhoc_state(NULL, 0) == 1)
		return 1;
	if (both)
	{
		if (!both_since)
			both_since = now;
		/* held for 0.8 s: the panel opens or closes, once per hold */
		if (both_since != 1 && now - both_since > 800000)
		{
			if (panel_open)
				close_panel();
			else
			{
				panel_open = 1;
				show();
			}
			both_since = 1;
		}
		/* (the pair never reaches the game while held: no pause menu) */
		return 1;
	}
	both_since = 0;
	if (!panel_open)
		return 0;
	if (screen == SCREEN_CODE)
	{
		code_input(pressed, buttons, now);
		show();
		return 1;
	}
	if (screen == SCREEN_BROWSE)
	{
		browse_input(pressed);
		/* (the list as it fills, twice a second) */
		if (screen == SCREEN_BROWSE && now - last_shown > 500000)
			browse_refresh();
		if (pressed || now - last_shown > 500000)
			show();
		return 1;
	}
	if (pressed & VITA_BUTTON_CIRCLE)
	{
		/* (the multiplayer page goes back to the settings first) */
		if (page == PAGE_MULTIPLAYER)
		{
			page = PAGE_SETTINGS;
			selected = 0;
			show();
		}
		else
			close_panel();
		return 1;
	}
	{
		/* up and down step, and repeat while held */
		int lines[SETTING_COUNT];
		int count = page_lines(lines);
		int step = 0;

		if (pressed & VITA_BUTTON_UP)
			step = -1;
		else if (pressed & VITA_BUTTON_DOWN)
			step = 1;
		else if ((buttons & (VITA_BUTTON_UP | VITA_BUTTON_DOWN)) && now - last_move > 250000)
			step = (buttons & VITA_BUTTON_UP) ? -1 : 1;
		if (step)
		{
			selected = (selected + step + count) % count;
			last_move = now;
		}
		if (selected >= count)
			selected = count - 1;
		if (pressed & (VITA_BUTTON_LEFT | VITA_BUTTON_RIGHT | VITA_BUTTON_CROSS))
		{
			struct setting *setting = &settings[lines[selected]];

			if (setting->kind == KIND_ACTION)
			{
				if (pressed & (VITA_BUTTON_RIGHT | VITA_BUTTON_CROSS))
				{
					notice[0] = 0;
					act(setting);
					if (!panel_open)
						return 1;
				}
			}
			else
				change(setting, (pressed & VITA_BUTTON_LEFT) ? -1 : 1);
		}
	}
	/* (redrawn on a press, and every half second for the status line: a
	code appears when the game starts hosting) */
	if (pressed || now - last_shown > 500000)
		show();
	return 1;
}
