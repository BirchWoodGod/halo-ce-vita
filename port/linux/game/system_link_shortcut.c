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

halo.log says each request and its answer, and each change of the state.
(Debug) HALO_SYSTEM_LINK_TEST=host, join or campaign makes the request
itself, as the panel would, 3 seconds after the main menu first loads: the automated
runs (no panel there; triage/g110/run/run.sh with MAP=-) open the screen
so.
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

#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);

volatile int halo_system_link_request;
volatile int halo_system_link_answer;
volatile int halo_multiplayer_status[SYSTEM_LINK_STATUS_COUNT];
/* (OpenCE's multiplayer screens' typing: menu_functions.c, the Vita's
host) */
volatile int halo_pc_menus_state;
volatile int halo_pc_menus_reason;
char halo_pc_menus_folder[128];
volatile int halo_text_input_state;
char halo_text_input_title[HALO_TEXT_INPUT_SIZE];
char halo_text_input_text[HALO_TEXT_INPUT_SIZE];
int halo_text_input_maximum;
int halo_text_input_password;

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

/* the Campaign screen (main menu, Campaign: the profiles, then the levels
and the difficulty, where Y hosts co-op), the main menu behind it */
static boolean system_link_shortcut_open_campaign(
	void)
{
	long main_menu = tag_loaded(UI_WIDGET_DEFINITION_TAG, system_link_main_menu);
	long campaign = tag_loaded(UI_WIDGET_DEFINITION_TAG, system_link_campaign);

	if (main_menu == NONE || campaign == NONE)
		return FALSE;
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
	__atomic_store_n(&halo_system_link_request, !strcmp(test, "join") ? SYSTEM_LINK_REQUEST_JOIN :
		!strcmp(test, "campaign") ? SYSTEM_LINK_REQUEST_CAMPAIGN : SYSTEM_LINK_REQUEST_HOST, __ATOMIC_RELEASE);
}

void system_link_shortcut_update(
	boolean main_menu_loaded)
{
	int request = __atomic_load_n(&halo_system_link_request, __ATOMIC_ACQUIRE);
	int answer, state;
	char build[0x20];

	system_link_shortcut_status(main_menu_loaded);
	/* (OpenCE's multiplayer screens: what they open from outside the
	widgets' own events, menu_functions.c) */
	{
		extern void pc_menu_functions_update(boolean main_menu_loaded);

		pc_menu_functions_update(main_menu_loaded);
	}
	if (request == SYSTEM_LINK_REQUEST_NONE)
	{
		system_link_shortcut_test(main_menu_loaded);
		return;
	}
	state = halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE];
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
