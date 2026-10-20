/*
SYSTEM_LINK_SHORTCUT.C

The game's side of the Vita settings panel's Play page: hosting, joining
and co-op (port/vita/host/vita_settings.c; system_link_shortcut.h holds
what the two share), called from the main loop every frame (main.c):

- What the game is doing, for the panel's Multiplayer tab: this machine's
  system link address; in the menus,
  looking for games (and how many the System Link list shows), joining,
  hosting or in another's lobby (and the machines and players there), or
  playing (a network game or not).
- The Play page's Host co-op campaign opens the Campaign screen the same
  way (main menu, Campaign: its profiles, then the levels and the
  difficulty, where Y hosts co-op, coop_menu.c), with the same checks.
  With Menu style Halo 3 the profiles lead to the Halo 3 campaign lobby,
  which opens with its NETWORK on co-op (menu_functions.c), START GAME
  hosting it.
- A request from the panel opens the game's System Link screen as the main
  menu's Multiplayer, then System Link, open it - with that history behind
  it, so B goes back the same way - at its first step: press A to join
  (on the Vita, player one comes in joined already), pick a profile, then
  the list of games found (SYSTEM LINK GAMES), where A joins a game and Y
  creates one. Only at the menus and outside a lobby
  (a lobby left so would end its game for the others); not without a
  network (the game's own check, as System Link makes it); and only on the
  maps of a build that plays with others (as the menus' own check,
  ui_widget.c ui_widget_launch_widget, which shows the player why).

- The panel's Forge page (Forge for the Halo CE menus too) asks for a
  Forge game: started at the main menu as the Forge lobby's START FORGE
  starts it (forge.c forge_game_start), with the map variant chosen; not
  in a level or a lobby. What stops it (a damaged variant) is said in a
  message over the game.

- The connection (network_connection.c: Same Wi-Fi, Ad hoc or Online)
  switched live when it is chosen, at the menus with no network game.
- Ad hoc play's group (halo_adhoc_request, halo_adhoc_group:
  system_link_shortcut.h): the Vita's host answers its requests; elsewhere
  this file does (an emulated group is joined from the start, and a join is
  logged and answered as the system's dialog would answer it in the tests).

halo.log says each request and its answer, and each change of the state.
(Debug) HALO_SYSTEM_LINK_TEST=host, join or campaign makes the request
itself, as the panel would, 3 seconds after the main menu first loads: the automated
runs (no panel there; triage/g110/run/run.sh with MAP=-) open the screen
so; forge:<map index>[:<map variant file>] asks for Forge as the panel's
Forge page does (run_forge_test.sh panel).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "bungie_net/network/transport.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "interface/ui_widget.h"
#include "networking/network_client_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "tag_files/tag_groups.h"
#include "interface/ui_widget_definitions.h"

#include "../src/system_link_shortcut.h"
#include "forge.h"
#include "../src/lang.h"
#include "../src/port_config.h"
#include "network_connection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
void platform_show_message(char const *title, char const *message);
struct widget_instance *ui_widget_port_active_screen(short local_player_index);

volatile int halo_system_link_request;
volatile int halo_system_link_answer;
volatile int halo_multiplayer_status[SYSTEM_LINK_STATUS_COUNT];
/* (OpenCE's multiplayer screens' typing: menu_functions.c, the Vita's
host) */
volatile int halo_pc_menus_state;
volatile int halo_text_input_state;
volatile int halo_settings_panel_request;
volatile int halo_adhoc_request;
volatile int halo_adhoc_group = HALO_ADHOC_GROUP_OFF;
char halo_text_input_title[HALO_TEXT_INPUT_SIZE];
char halo_text_input_text[HALO_TEXT_INPUT_SIZE];
int halo_text_input_maximum;
int halo_text_input_password;
/* (SYSTEM_LINK_REQUEST_FORGE) the map (forge.c's index) and map variant's
file ("" none) of the Forge game the panel asks for */
volatile int halo_forge_request_map = NONE;
char halo_forge_request_variant[HALO_FORGE_VARIANT_SIZE];
/* ... on the network: hosted, others joining (0: a local game) */
volatile int halo_forge_request_network;
volatile int halo_forge_net_hosting;

/* (network_client_manager.c's client states, in its order) */
enum
{
	_client_searching,
	_client_joining,
	_client_pregame,
	_client_ingame,
	_client_postgame,
};

static char const system_link_main_menu[] = "ui\\shell\\main_menu\\main_menu";
static char const system_link_type_select[] =
	"ui\\shell\\main_menu\\multiplayer_type_select\\multiplayer_type_select_screen";
static char const system_link_profiles[] =
	"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\4way_profile_select\\4way_start2join_screen";
static char const system_link_campaign[] =
	"ui\\shell\\main_menu\\player_profiles_select\\solo_game_player_profile_select_screen";

static void system_link_shortcut_status(
	boolean main_menu_loaded)
{
	struct network_game_client *client = global_network_game_client_get();
	struct network_game_server *server = global_network_game_server_get();
	/* (split screen: a server that takes no other machines) */
	boolean network_game = client && !network_game_is_splitscreen_local();
	int state = SYSTEM_LINK_STATE_MENUS, machines = 0, players = 0, games = 0, maximum = 0;
	XNADDR address;

	if (network_game)
	{
		struct network_game *game = network_game_client_get_game(client);

		machines = game->machine_count;
		players = game->player_count;
		maximum = game->maximum_players;
	}
	if (!main_menu_loaded)
	{
		state = network_game && game_connection() != _game_connection_local ? SYSTEM_LINK_STATE_IN_GAME :
			SYSTEM_LINK_STATE_PLAYING;
	}
	/* (hosting from the game's creation on: its map and game type are
	chosen with the server already up) */
	else if (network_game && server)
		state = SYSTEM_LINK_STATE_HOSTING;
	else if (network_game)
	{
		switch (network_game_client_get_state(client, NULL))
		{
		case _client_searching:
			state = SYSTEM_LINK_STATE_SEARCHING;
			games = network_game_client_listed_game_count();
			machines = players = maximum = 0;
			break;
		case _client_joining:
			state = SYSTEM_LINK_STATE_JOINING;
			break;
		default:
			state = SYSTEM_LINK_STATE_LOBBY;
			break;
		}
	}
	if (state != halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE])
	{
		static char const *const names[] = { "starting", "playing", "menus", "looking for games", "joining", "hosting",
			"in another's lobby", "in a network game" };

		platform_log("system link: %s", names[state]);
	}
	halo_multiplayer_status[SYSTEM_LINK_STATUS_MACHINES] = machines;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_PLAYERS] = players;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_GAMES] = games;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_MAXIMUM] = maximum;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_HOST] = server != NULL && network_game;
	/* (xnet.c keeps it half a second: no request to the system each frame) */
	halo_multiplayer_status[SYSTEM_LINK_STATUS_ADDRESS] =
		XNetGetTitleXnAddr(&address) != XNET_GET_XNADDR_PENDING ? (int)address.ina.s_addr : 0;
	__atomic_store_n(&halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE], state, __ATOMIC_RELEASE);
}

/* the System Link screen, its history behind it */
boolean system_link_shortcut_open(
	void)
{
	long main_menu = tag_loaded(UI_WIDGET_DEFINITION_TAG, system_link_main_menu);
	long type_select = tag_loaded(UI_WIDGET_DEFINITION_TAG, system_link_type_select);
	long profiles = tag_loaded(UI_WIDGET_DEFINITION_TAG, system_link_profiles);

	if (main_menu == NONE || type_select == NONE || profiles == NONE)
		return FALSE;
	ui_widgets_close_all();
	return ui_widget_load_by_name_or_tag(NULL, main_menu, NULL, NONE, NONE, NONE, NONE) &&
		ui_widget_load_by_name_or_tag(NULL, type_select, NULL, NONE, main_menu, NONE, NONE) &&
		ui_widget_load_by_name_or_tag(NULL, profiles, NULL, NONE, type_select, NONE, NONE);
}

void menu_functions_h3_campaign_coop(void);

/* the Campaign screen (main menu, Campaign: the profiles, then the levels
and the difficulty, where Y hosts co-op; or the Halo 3 campaign lobby,
its NETWORK on co-op), the main menu behind it */
static boolean system_link_shortcut_open_campaign(
	void)
{
	long main_menu = tag_loaded(UI_WIDGET_DEFINITION_TAG, system_link_main_menu);
	long campaign = tag_loaded(UI_WIDGET_DEFINITION_TAG, system_link_campaign);

	if (main_menu == NONE || campaign == NONE)
		return FALSE;
	menu_functions_h3_campaign_coop();
	ui_widgets_close_all();
	return ui_widget_load_by_name_or_tag(NULL, main_menu, NULL, NONE, NONE, NONE, NONE) &&
		ui_widget_load_by_name_or_tag(NULL, campaign, NULL, NONE, main_menu, NONE, NONE);
}

/* (debug) HALO_SYSTEM_LINK_TEST: the panel's request, once, 3 s into the
main menu */
static void system_link_shortcut_test(
	boolean main_menu_loaded)
{
	static boolean done;
	static unsigned long since;
	char const *test;

	if (done)
		return;
	test = getenv("HALO_SYSTEM_LINK_TEST");
	if (!test || !*test)
	{
		done = TRUE;
		return;
	}
	if (!main_menu_loaded)
	{
		since = 0;
		return;
	}
	if (!since)
		since = system_milliseconds() | 1;
	if (system_milliseconds() - since < 3000)
		return;
	done = TRUE;
	platform_log("system link: HALO_SYSTEM_LINK_TEST=%s", test);
	if (!strncmp(test, "forge:", 6) || !strncmp(test, "forgenet:", 9))
	{
		/* ("forgenet:<map>[:<variant>]": the network Forge game) */
		boolean network = !strncmp(test, "forgenet:", 9);
		char const *map = test + (network ? 9 : 6);
		char const *file = strchr(map, ':');

		halo_forge_request_network = network;
		halo_forge_request_map = atoi(map);
		snprintf(halo_forge_request_variant, sizeof(halo_forge_request_variant), "%s", file ? file + 1 : "");
		__atomic_store_n(&halo_system_link_request, SYSTEM_LINK_REQUEST_FORGE, __ATOMIC_RELEASE);
		return;
	}
	__atomic_store_n(&halo_system_link_request, !strcmp(test, "join") ? SYSTEM_LINK_REQUEST_JOIN :
		!strcmp(test, "campaign") ? SYSTEM_LINK_REQUEST_CAMPAIGN : SYSTEM_LINK_REQUEST_HOST, __ATOMIC_RELEASE);
}

/* (no Vita host: the game answers itself) the ad hoc group's state - an
emulated group (HALO_NET_ADHOC_EMULATE, posix_net.c) is in it from the
start, as a Vita is once its system's dialog has joined one - and the Halo 3
menus' requests: (test builds) HALO_ADHOC_TEST=join or fail says what the
system's dialog would have done, a second and a half later; without it
there is no dialog to open, and the join fails. Leaving the group forgets
the test's answer */
static void system_link_shortcut_adhoc(
	void)
{
#ifndef HALO_VITA
	extern int posix_adhoc_ready(unsigned char *address);
	/* (the test's answer, once given; when the request was taken) */
	static int answered = HALO_ADHOC_GROUP_NONE;
	static unsigned long asked;
	int request = __atomic_load_n(&halo_adhoc_request, __ATOMIC_ACQUIRE);
	unsigned char address[6];
	int group;
	char const *test = NULL;

#ifndef HALO_RELEASE
	test = getenv("HALO_ADHOC_TEST");
#endif
	if (!config_boolean("network.adhoc"))
		group = HALO_ADHOC_GROUP_OFF;
	else if (posix_adhoc_ready(address))
		group = HALO_ADHOC_GROUP_IN;
	else
		group = answered;
	if (request == HALO_ADHOC_REQUEST_JOIN)
	{
		if (group == HALO_ADHOC_GROUP_IN || group == HALO_ADHOC_GROUP_OFF)
		{
			platform_log("ad hoc: a join asked: %s", group == HALO_ADHOC_GROUP_IN ? "in the group already" :
				"the connection is not Ad hoc");
			__atomic_store_n(&halo_adhoc_request, HALO_ADHOC_REQUEST_NONE, __ATOMIC_RELEASE);
		}
		else if (test && (!strcmp(test, "join") || !strcmp(test, "fail")))
		{
			group = HALO_ADHOC_GROUP_JOINING;
			if (!asked)
			{
				asked = system_milliseconds() | 1;
				platform_log("ad hoc: the system's dialog would join the room's group here (HALO_ADHOC_TEST=%s)", test);
			}
			else if (system_milliseconds() - asked >= 1500)
			{
				answered = group = !strcmp(test, "join") ? HALO_ADHOC_GROUP_IN : HALO_ADHOC_GROUP_FAILED;
				asked = 0;
				platform_log("ad hoc: the system's dialog %s (HALO_ADHOC_TEST)", group == HALO_ADHOC_GROUP_IN ?
					"joined the group" : "did not join a group");
				__atomic_store_n(&halo_adhoc_request, HALO_ADHOC_REQUEST_NONE, __ATOMIC_RELEASE);
			}
		}
		else
		{
			answered = group = HALO_ADHOC_GROUP_FAILED;
			platform_log("ad hoc: no system dialog on this build (an emulated group: HALO_NET_ADHOC_EMULATE)");
			__atomic_store_n(&halo_adhoc_request, HALO_ADHOC_REQUEST_NONE, __ATOMIC_RELEASE);
		}
	}
	else if (request == HALO_ADHOC_REQUEST_LEAVE)
	{
		/* (an emulated group is the setting's: nothing to leave) */
		answered = HALO_ADHOC_GROUP_NONE;
		platform_log("ad hoc: the group left");
		__atomic_store_n(&halo_adhoc_request, HALO_ADHOC_REQUEST_NONE, __ATOMIC_RELEASE);
	}
	__atomic_store_n(&halo_adhoc_group, group, __ATOMIC_RELEASE);
#endif
}

void system_link_shortcut_update(
	boolean main_menu_loaded)
{
	int request = __atomic_load_n(&halo_system_link_request, __ATOMIC_ACQUIRE);
	int answer, state;
	char build[0x20];

	system_link_shortcut_status(main_menu_loaded);
	network_connection_update(main_menu_loaded);
	system_link_shortcut_adhoc();
	/* (the host an invite, a code or the server browser joined, once
	reached: its game joined as soon as the System Link list shows it, as A
	on its row would; tried once, the host then forgotten, so that a refusal
	is told once and a later visit to the list joins nothing by itself) */
	if (main_menu_loaded)
	{
		extern int p2p_joined_host(unsigned char *identifier, int take);
		extern int ui_widget_port_join_listed_game(void const *server);
		unsigned char host[6];
		void const *game;

		if (p2p_joined_host(host, FALSE) && (game = network_game_client_listed_game_of(host)) != NULL)
		{
			extern int menu_functions_lan_join_listed(void const *game);
			int joined = ui_widget_port_join_listed_game(game);

			/* (or the Halo 3 games browser's list, when it is the screen open) */
			if (joined < 0)
				joined = menu_functions_lan_join_listed(game);

			if (joined >= 0)
			{
				p2p_joined_host(host, TRUE);
				platform_log("system link: the joined host's game %s by itself",
					joined ? "joined" : "listed but not joined (as a pick would be told)");
			}
		}
	}
	/* (OpenCE's multiplayer screens: what they open from outside the
	widgets' own events, menu_functions.c) */
	{
		extern void pc_menu_functions_update(boolean main_menu_loaded);

		pc_menu_functions_update(main_menu_loaded);
	}
	/* (friends: this machine's presence, a friend's invite asked about:
	friends_game.c) */
	{
		extern void friends_game_update(boolean main_menu_loaded);

		friends_game_update(main_menu_loaded);
	}
	/* (the settings panel's Mod manager page: its look and deletes,
	answered here, on the game's thread: mod_manager.c) */
	{
		extern void mod_manager_update(boolean main_menu_loaded);

		mod_manager_update(main_menu_loaded);
	}
	if (request == SYSTEM_LINK_REQUEST_NONE)
	{
		system_link_shortcut_test(main_menu_loaded);
		return;
	}
	state = halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE];
	/* (the panel's Forge page: the Forge game, at the main menu) */
	if (request == SYSTEM_LINK_REQUEST_FORGE)
	{
		char why[160];
		char variant[HALO_FORGE_VARIANT_SIZE];

		why[0] = 0;
		csstrncpy(variant, halo_forge_request_variant, sizeof(variant) - 1);
		variant[sizeof(variant) - 1] = 0;
		/* (at the main menu: once its screen is up, its players with it - a
		game started before has none) */
		if (main_menu_loaded && !ui_widget_port_active_screen(0))
			return;
		if (!main_menu_loaded)
		{
			answer = SYSTEM_LINK_ANSWER_IN_PLAY;
			snprintf(why, sizeof(why), "%s", T("Leave the level first: Start, then Quit"));
		}
		else if (state == SYSTEM_LINK_STATE_JOINING || state == SYSTEM_LINK_STATE_HOSTING ||
			state == SYSTEM_LINK_STATE_LOBBY)
		{
			answer = SYSTEM_LINK_ANSWER_IN_LOBBY;
			snprintf(why, sizeof(why), "%s", T("Leave the network game's lobby first"));
		}
		else if (halo_forge_request_map < 0 || halo_forge_request_map >= forge_map_count() ||
			!forge_map_available((short)halo_forge_request_map))
		{
			answer = SYSTEM_LINK_ANSWER_UNAVAILABLE;
			snprintf(why, sizeof(why), "%s", T("No multiplayer map is in the maps folder."));
		}
		/* (on the network: the profile, then the game hosted and its lobby,
		menu_functions.c; no network up: said) */
		else if (halo_forge_request_network)
		{
			extern boolean menu_functions_forge_host(short map_index, char const *variant_file, char *why, int why_size);

			if (!transport_network_available())
			{
				answer = SYSTEM_LINK_ANSWER_NO_NETWORK;
				snprintf(why, sizeof(why), "%s", T("No network: choose a connection on the Play page"));
			}
			/* (the screens opened first: the multiplayer screen's opening
			forgets a game asked to be created before it) */
			else if (system_link_shortcut_open() &&
				menu_functions_forge_host((short)halo_forge_request_map, variant, why, sizeof(why)))
			{
				answer = SYSTEM_LINK_ANSWER_OPENED;
			}
			else
				answer = SYSTEM_LINK_ANSWER_UNAVAILABLE;
		}
		else if (forge_game_start((short)halo_forge_request_map, variant, why, sizeof(why)))
			answer = SYSTEM_LINK_ANSWER_OPENED;
		else
			answer = SYSTEM_LINK_ANSWER_UNAVAILABLE;
		platform_log("system link: the settings panel's Forge: %s%s%s", answer == SYSTEM_LINK_ANSWER_OPENED ? "started" :
			"not started", why[0] ? ": " : "", why);
		if (answer != SYSTEM_LINK_ANSWER_OPENED)
			platform_show_message(T("FORGE"), why);
		halo_system_link_answer = answer;
		__atomic_store_n(&halo_system_link_request, SYSTEM_LINK_REQUEST_NONE, __ATOMIC_RELEASE);
		return;
	}
	if (!main_menu_loaded)
		answer = SYSTEM_LINK_ANSWER_IN_PLAY;
	else if (state == SYSTEM_LINK_STATE_JOINING || state == SYSTEM_LINK_STATE_HOSTING ||
		state == SYSTEM_LINK_STATE_LOBBY)
		answer = SYSTEM_LINK_ANSWER_IN_LOBBY;
	else if (!transport_network_available())
		answer = SYSTEM_LINK_ANSWER_NO_NETWORK;
	else if (!cache_files_multiplayer_region(build))
	{
		cache_files_show_multiplayer_unavailable(NULL, build);
		answer = SYSTEM_LINK_ANSWER_UNAVAILABLE;
	}
	else if (request == SYSTEM_LINK_REQUEST_CAMPAIGN)
		answer = system_link_shortcut_open_campaign() ? SYSTEM_LINK_ANSWER_OPENED : SYSTEM_LINK_ANSWER_UNAVAILABLE;
	else
		answer = system_link_shortcut_open() ? SYSTEM_LINK_ANSWER_OPENED : SYSTEM_LINK_ANSWER_UNAVAILABLE;
	platform_log("system link: the settings panel's %s: %s", request == SYSTEM_LINK_REQUEST_HOST ? "host a game" :
		request == SYSTEM_LINK_REQUEST_CAMPAIGN ? "host co-op campaign" : "join a game",
		answer == SYSTEM_LINK_ANSWER_OPENED ? request == SYSTEM_LINK_REQUEST_CAMPAIGN ? "the Campaign screen opened" :
		"the System Link screen opened" :
		answer == SYSTEM_LINK_ANSWER_IN_PLAY ? "not at the menus" : answer == SYSTEM_LINK_ANSWER_IN_LOBBY ?
		"already in a lobby" : answer == SYSTEM_LINK_ANSWER_NO_NETWORK ? "no network" : "the menus did not open");
	halo_system_link_answer = answer;
	__atomic_store_n(&halo_system_link_request, SYSTEM_LINK_REQUEST_NONE, __ATOMIC_RELEASE);
}
