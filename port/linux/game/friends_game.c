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

- The game's players' options (the Halo 3 PLAYERS screen,
  menus/halo3/players.xml): Triangle opens it in the game's lobby (the
  waiting screen) and on the carnage report, and with the scoreboard up
  (Back held) in a game; and each second the game this machine plays is
  told to friends (its machine, whether it hosts, its machines' addresses:
  p2p_friends_set_game), whose cards the players' Add friend and Block ask
  for.

- The party (port/linux/src/p2p_party.h): a party invite asked as a
  friend's invite is (at the menus, Accept or Decline: JOIN PARTY or NO);
  the leader's follow joined by itself at the menus (as an invite accepted
  joins: the System Link screen next), after a look at the map (missing with
  map downloads "never": not joined, the party told); in another game it
  waits for the menus.

(debug, not in release builds) HALO_TEST_FRIENDS_ANSWER=accept or ignore
answers the invite's question at once, for the automated tests (a run
nobody watches answers every question no); HALO_TEST_FRIENDS_PLAYERS=add@S
opens the PLAYERS screen S seconds into a network game and adds the first
other machine's player, as its options' ADD FRIEND does.
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
#include "tag_files/tag_groups.h"
#include "input/input.h"
#include "networking/network_server_manager.h"
#include "halo_port_limits.h"

#include "../src/p2p_friends.h"
#include "../src/p2p_party.h"
#include "../src/system_link_shortcut.h"
#include "../src/lang.h"
#include "custom_edition_maps.h"
#include "custom_edition_cache.h"
#include "forge.h"
#include "../src/port_config.h"
#include "network_wire.h"

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
/* menu_functions.c's: the first other machine's player added (the tests') */
int menu_functions_players_add_first(void);
/* menu_functions.c's: the PLAYERS screen opened (1: it did), and Triangle
read for it */
int menu_functions_players_open(boolean main_menu_loaded);
void menu_functions_players_button(boolean main_menu_loaded);

#define FRIENDS_ACTIVITY_INTERVAL 1000

static struct
{
	unsigned long activity_time;
	/* the invite asked about, by its id ("" none) */
	char asking[P2P_FRIEND_ID_SIZE];
	char asking_name[P2P_FRIEND_NAME_SIZE];
	/* (the tests' steps) the seconds since in a network game, and whether
	the player was added */
	unsigned long in_game_since;
	boolean test_added;
	/* the party invite asked about, by its id ("" none) */
	char party_asking[P2P_FRIEND_ID_SIZE];
	char party_asking_name[P2P_FRIEND_NAME_SIZE];
	/* the follow last looked at (its serial) and what it came to */
	unsigned long follow_serial;
	int follow_result;
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

/* ---------- the tester's flaming helmet (port/linux/src/p2p_flame.inc) */

/* (a network player's name: players.h's 12 wide characters) */
#define FLAME_PLAYER_NAME_LENGTH 12

boolean friends_game_flame_claim(short local_player_index)
{
	/* (its first player's alone; never on the crossplay wire, whose PC
	builds are not this project's) */
	return local_player_index == 0 && !network_wire_crossplay() && p2p_flame_wearing();
}

/* the game's record told to the flame's checks: each machine's claim and
its claimant's name, and this machine's own player's name there */
static void friends_game_flame_players(struct network_game_client *client)
{
	static int claims[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	static int present[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	static unsigned short names[HALO_PORT_MAXIMUM_NETWORK_MACHINES][FLAME_PLAYER_NAME_LENGTH];
	unsigned short own_name[FLAME_PLAYER_NAME_LENGTH];
	struct network_game *game = network_game_client_get_game(client);
	int own = network_game_client_get_machine_index(client), own_found = 0;
	long index;

	typedef char name_length_check[FLAME_PLAYER_NAME_LENGTH == 12 ? 1 : -1];
	(void)sizeof(name_length_check);
	memset(claims, 0, sizeof(claims));
	memset(present, 0, sizeof(present));
	memset(names, 0, sizeof(names));
	memset(own_name, 0, sizeof(own_name));
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS && game; index++)
	{
		struct network_player *player = &game->players[index];
		int machine = player->machine_index, unit;

		if (!network_player_is_valid(player) || machine < 0 || machine >= HALO_PORT_MAXIMUM_NETWORK_MACHINES)
			continue;
		present[machine] = 1;
		if (machine == own && (!own_found || player->icon_index == NETWORK_PLAYER_FLAME_CLAIM))
		{
			own_found = 1;
			for (unit = 0; unit < FLAME_PLAYER_NAME_LENGTH && player->name[unit]; unit++)
				own_name[unit] = (unsigned short)player->name[unit];
			for (; unit < FLAME_PLAYER_NAME_LENGTH; unit++)
				own_name[unit] = 0;
		}
		if (player->icon_index != NETWORK_PLAYER_FLAME_CLAIM || claims[machine])
			continue;
		claims[machine] = 1;
		for (unit = 0; unit < FLAME_PLAYER_NAME_LENGTH && player->name[unit]; unit++)
			names[machine][unit] = (unsigned short)player->name[unit];
	}
	p2p_flame_set_players(present, claims, &names[0][0], HALO_PORT_MAXIMUM_NETWORK_MACHINES, own_name);
}

/* the game this machine plays, told to friends: its machine, whether it
hosts, and (a host's) each machine's address */
static void friends_game_network(void)
{
	struct network_game_client *client = global_network_game_client_get();
	struct network_game_server *server = global_network_game_server_get();
	static unsigned long addresses[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	int index;

	if (!client || network_game_is_splitscreen_local())
	{
		p2p_friends_set_game(-1, 0, NULL, 0);
		friends_game.in_game_since = 0;
		return;
	}
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; index++)
		addresses[index] = server ? network_game_server_machine_address(index) : 0;
	p2p_friends_set_game(network_game_client_get_machine_index(client), server != NULL, addresses,
		HALO_PORT_MAXIMUM_NETWORK_MACHINES);
	friends_game_flame_players(client);
	if (!friends_game.in_game_since)
		friends_game.in_game_since = system_milliseconds() ? system_milliseconds() : 1;
}

/* (debug) HALO_TEST_FRIENDS_PLAYERS=add@S */
static void friends_game_players_test(boolean main_menu_loaded)
{
#ifndef HALO_RELEASE
	char const *setting = getenv("HALO_TEST_FRIENDS_PLAYERS");
	unsigned long seconds;

	if (!setting || strncmp(setting, "add@", 4) || friends_game.test_added || !friends_game.in_game_since)
		return;
	seconds = (unsigned long)atol(setting + 4);
	if (system_milliseconds() - friends_game.in_game_since < seconds * 1000UL)
		return;
	friends_game.test_added = TRUE;
	platform_log("friends: test: the PLAYERS screen %s", menu_functions_players_open(main_menu_loaded) ? "opened" :
		"did not open");
	if (!menu_functions_players_add_first())
		platform_log("friends: test: no other machine's player to add");
#else
	(void)main_menu_loaded;
#endif
}

/* the answer the tests give a party invite's question: 1 accept, 0 no, -1
ask the player */
static int friends_game_party_test_answer(void)
{
#ifndef HALO_RELEASE
	char const *setting = getenv("HALO_TEST_PARTY_ANSWER");

	if (setting && !strcmp(setting, "accept"))
		return 1;
	if (setting && !strcmp(setting, "decline"))
		return 0;
#endif
	return -1;
}

static void friends_game_party_answer(int accept)
{
	int outcome = p2p_party_answer(friends_game.party_asking, accept);

	platform_log("party: %s's party invite %s: %s", friends_game.party_asking_name, accept ? "accepted" : "declined",
		p2p_party_outcome_text(outcome));
	friends_game.party_asking[0] = 0;
}

/* a party invite: the question at the menus, as a friend's invite's */
static void friends_game_party_invites(boolean main_menu_loaded)
{
	int state = __atomic_load_n(&halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE], __ATOMIC_ACQUIRE);
	struct p2p_party_invite invite;

	if (friends_game.party_asking[0])
	{
		int answer = platform_question_answer();

		if (!main_menu_loaded || state != SYSTEM_LINK_STATE_MENUS)
		{
			platform_ask_question(NULL, NULL);
			friends_game.party_asking[0] = 0;
			return;
		}
		if (answer >= 0)
		{
			platform_ask_question(NULL, NULL);
			friends_game_party_answer(answer == 1);
		}
		return;
	}
	if (friends_game.asking[0] || !main_menu_loaded || state != SYSTEM_LINK_STATE_MENUS || platform_question_asking())
		return;
	if (!p2p_party_take_invite(&invite))
		return;
	snprintf(friends_game.party_asking, sizeof(friends_game.party_asking), "%s", invite.id);
	snprintf(friends_game.party_asking_name, sizeof(friends_game.party_asking_name), "%s", invite.name);
	if (friends_game_party_test_answer() >= 0)
	{
		platform_log("party: %s invited you to their party (the test answers)", invite.name);
		friends_game_party_answer(friends_game_party_test_answer());
		return;
	}
	{
		char text[256];

		snprintf(text, sizeof(text), T("%s invited you to their party (%d of %d).\n\nJoin the party? You'll follow "
			"them from game to game."), invite.name, invite.size, P2P_PARTY_MAXIMUM);
		platform_ask_question(T("PARTY INVITE"), text);
	}
}

/* the leader's follow: joined at the menus (out of any game), the map looked
at first; in a game, it waits */
static void friends_game_party_follow(boolean main_menu_loaded)
{
	struct network_game_client *client = global_network_game_client_get();
	int state = __atomic_load_n(&halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE], __ATOMIC_ACQUIRE);
	struct p2p_party_follow follow;
	int result;

	if (!p2p_party_follow_pending(&follow))
		return;
	if (!main_menu_loaded || state != SYSTEM_LINK_STATE_MENUS || (client && !network_game_is_splitscreen_local()) ||
		friends_game.asking[0] || friends_game.party_asking[0])
	{
		/* (in a game, or its lobby: followed once back at the menus) */
		if (friends_game.follow_serial != follow.serial || friends_game.follow_result != P2P_PARTY_FOLLOW_WAITING)
		{
			friends_game.follow_serial = follow.serial;
			friends_game.follow_result = P2P_PARTY_FOLLOW_WAITING;
			platform_log("party: %s went to a game (%s); following once this machine is back at the menus",
				follow.leader, follow.activity);
			p2p_party_follow_result(follow.serial, P2P_PARTY_FOLLOW_WAITING);
		}
		return;
	}
	friends_game.follow_serial = follow.serial;
	/* (a map missing here, with map downloads never: not joined) */
	if (follow.map[0] && !custom_edition_cache_map_identity(follow.map) &&
		!strcmp(config_string("network.map_downloads"), "never"))
	{
		friends_game.follow_result = P2P_PARTY_FOLLOW_NO_MAP;
		platform_log("party: not following %s: the map %s is missing and map downloads are off", follow.leader, follow.map);
		p2p_party_follow_result(follow.serial, P2P_PARTY_FOLLOW_NO_MAP);
		return;
	}
	result = p2p_party_follow_join(follow.serial);
	friends_game.follow_result = result;
	platform_log("party: following %s: %s", follow.leader, p2p_party_follow_text(result));
	if (result == P2P_PARTY_FOLLOW_JOINING)
		menu_functions_open_system_link();
}

void friends_game_update(boolean main_menu_loaded)
{
	unsigned long now = system_milliseconds();

	if (!friends_game.activity_time || now - friends_game.activity_time >= FRIENDS_ACTIVITY_INTERVAL)
	{
		friends_game.activity_time = now ? now : 1;
		friends_game_activity(main_menu_loaded);
		friends_game_network();
	}
	/* (the flaming helmet's proofs: their checks' work and their packets,
	each frame: flame_net.c) */
	{
		void flame_net_update(void);

		flame_net_update();
	}
	friends_game_invites(main_menu_loaded);
	friends_game_party_invites(main_menu_loaded);
	/* (in a party: the lobbies' NETWORK ONLINE, a party plays online:
	menu_functions.c) */
	if (main_menu_loaded)
	{
		extern void menu_functions_party_online(void);

		menu_functions_party_online();
	}
	friends_game_party_follow(main_menu_loaded);
	menu_functions_players_button(main_menu_loaded);
	friends_game_players_test(main_menu_loaded);
}
