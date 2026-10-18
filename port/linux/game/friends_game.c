/*
FRIENDS_GAME.C

The game's side of friends (port/linux/src/p2p_friends.h), from the main
loop (system_link_shortcut.c) every frame:

- What this machine is doing, for its presence, once a second: at the
  menus (in a network game's lobby: its game), a multiplayer game (its game
  type and map), co-op (its level and difficulty), the campaign alone, or
  Forge; and the player's name (the first profile's).
- A friend's invite: at the main menu, out of any lobby, the game's own
  question (on the Vita the settings panel's, in the Halo 3 menus' look)
  asks Accept or Ignore; Accept joins the invite's game as JOIN WITH CODE
  joins a code (the System Link screen next, where the host's game is joined
  once listed). An invite that comes in a level or a lobby waits for the
  menus (they last five minutes); the lists show it meanwhile.

(debug, not in release builds) HALO_TEST_FRIENDS_ANSWER=accept or ignore
answers the invite's question at once, for the automated tests (a run
nobody watches answers every question no).
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/player_ui.h"
#include "main/main.h"
#include "networking/network_client_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "saved games/player_profile.h"
#include "tag_files/tag_files.h"

#include "../src/p2p_friends.h"
#include "../src/system_link_shortcut.h"
#include "../src/lang.h"
#include "custom_edition_maps.h"
#include "forge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);
void platform_ask_question(char const *title, char const *text);
int platform_question_answer(void);
int platform_question_asking(void);
/* menu_functions.c's: the System Link screen opened at the next frame, as
JOIN WITH CODE opens it */
void menu_functions_open_system_link(void);

#define FRIENDS_ACTIVITY_INTERVAL 1000

static struct
{
	unsigned long activity_time;
	/* the invite asked about, by its id ("" none) */
	char asking[P2P_FRIEND_ID_SIZE];
	char asking_name[P2P_FRIEND_NAME_SIZE];
} friends_game;

/* wide text as ASCII, a Latin letter with a mark its plain letter ('?' for
the rest), as the server browser's listing has it */
static void friends_ascii(char *text, int size, wchar_t const *wide, int length)
{
	int index;

	for (index = 0; index < size - 1 && index < length && wide[index]; index++)
		text[index] = player_name_character_ascii(wide[index]);
	text[index] = 0;
}

static void friends_game_activity(boolean main_menu_loaded)
{
	struct network_game_client *client = global_network_game_client_get();
	int status = __atomic_load_n(&halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE], __ATOMIC_ACQUIRE);
	/* (looking for games at the menus: still the menus) */
	boolean network_game = client && !network_game_is_splitscreen_local() &&
		!(main_menu_loaded && status == SYSTEM_LINK_STATE_SEARCHING);
	int state = P2P_FRIEND_STATE_MENUS, engine = 0, difficulty = -1, pc_map = 0, players = 0, maximum = 0;
	char map[64], gametype[32], name[24];
	long profile_index;

	map[0] = 0;
	gametype[0] = 0;
	if (network_game)
	{
		struct network_game *game = network_game_client_get_game(client);

		snprintf(map, sizeof(map), "%s", tag_name_strip_path(game->map.name));
		friends_ascii(gametype, sizeof(gametype), game->variant.human_readable_game_description,
			NUMBEROF(game->variant.human_readable_game_description));
		engine = (int)game->variant.game_engine_index;
		players = game->player_count;
		maximum = game->maximum_players;
		if (engine == 0 && custom_edition_maps_campaign_level(game->map.name))
		{
			state = P2P_FRIEND_STATE_COOP;
			difficulty = PIN(game->difficulty, 0, 3);
		}
		else
			state = P2P_FRIEND_STATE_MULTIPLAYER;
	}
	else if (!main_menu_loaded)
	{
		struct game_variant *variant = game_engine_running() ? game_engine_get_variant() : NULL;
		char const *level = variant ? main_get_multiplayer_map_name() : main_get_map_name();

		snprintf(map, sizeof(map), "%s", level ? tag_name_strip_path(level) : "");
		if (forge_game_active())
			state = P2P_FRIEND_STATE_FORGE;
		else if (variant)
		{
			state = P2P_FRIEND_STATE_MULTIPLAYER;
			engine = (int)variant->game_engine_index;
			friends_ascii(gametype, sizeof(gametype), variant->human_readable_game_description,
				NUMBEROF(variant->human_readable_game_description));
		}
		else
		{
			state = P2P_FRIEND_STATE_CAMPAIGN;
			difficulty = PIN(game_difficulty_level_get(), 0, 3);
		}
	}
	p2p_friends_set_activity(state, map, gametype, engine, difficulty, pc_map, players, maximum);
	/* (the name: the first profile's) */
	name[0] = 0;
	profile_index = player_ui_get_active_player_profile_index(0);
	if (profile_index != NONE)
	{
		struct player_profile profile;

		if (player_profile_get(profile_index, &profile))
			friends_ascii(name, sizeof(name), profile.player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH);
	}
	p2p_friends_set_name(name);
}

/* the answer the tests give the invite's question: 1 accept, 0 ignore, -1
ask the player */
static int friends_game_test_answer(void)
{
#ifndef HALO_RELEASE
	char const *setting = getenv("HALO_TEST_FRIENDS_ANSWER");

	if (setting && !strcmp(setting, "accept"))
		return 1;
	if (setting && !strcmp(setting, "ignore"))
		return 0;
#endif
	return -1;
}

static void friends_game_answer(int accept)
{
	int outcome = p2p_friends_act(friends_game.asking, P2P_FRIEND_KIND_INVITE, accept ? P2P_FRIENDS_INVITE_ACCEPT :
		P2P_FRIENDS_INVITE_IGNORE);

	platform_log("friends: %s's invite %s: %s", friends_game.asking_name, accept ? "accepted" : "ignored",
		p2p_friends_outcome_text(outcome));
	if (accept && outcome == P2P_FRIENDS_DONE)
		menu_functions_open_system_link();
	friends_game.asking[0] = 0;
}

static void friends_game_invites(boolean main_menu_loaded)
{
	int state = __atomic_load_n(&halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE], __ATOMIC_ACQUIRE);
	struct p2p_friend_entry invite;

	if (friends_game.asking[0])
	{
		int answer = platform_question_answer();

		/* (left for a level or a lobby meanwhile: the question withdrawn, the
		invite kept in the lists) */
		if (!main_menu_loaded || state != SYSTEM_LINK_STATE_MENUS)
		{
			platform_ask_question(NULL, NULL);
			friends_game.asking[0] = 0;
			return;
		}
		if (answer >= 0)
		{
			platform_ask_question(NULL, NULL);
			friends_game_answer(answer == 1);
		}
		return;
	}
	if (!main_menu_loaded || state != SYSTEM_LINK_STATE_MENUS || platform_question_asking())
		return;
	if (!p2p_friends_take_invite(&invite))
		return;
	snprintf(friends_game.asking, sizeof(friends_game.asking), "%s", invite.id);
	snprintf(friends_game.asking_name, sizeof(friends_game.asking_name), "%s", invite.name);
	if (friends_game_test_answer() >= 0)
	{
		platform_log("friends: %s invited you: %s (the test answers)", invite.name, invite.activity);
		friends_game_answer(friends_game_test_answer());
		return;
	}
	{
		char text[256];

		snprintf(text, sizeof(text), T("%s invited you to their game:\n%s\n\nAccept the invite and join now?"),
			invite.name, invite.activity);
		platform_ask_question(T("FRIEND INVITE"), text);
	}
}

void friends_game_update(boolean main_menu_loaded)
{
	unsigned long now = system_milliseconds();

	if (!friends_game.activity_time || now - friends_game.activity_time >= FRIENDS_ACTIVITY_INTERVAL)
	{
		friends_game.activity_time = now ? now : 1;
		friends_game_activity(main_menu_loaded);
	}
	friends_game_invites(main_menu_loaded);
}
