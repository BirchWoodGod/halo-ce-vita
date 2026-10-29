/*
NETWORK_CROSSPLAY_GUARD.C

The game client's side of the crossplay guardrails
(network_crossplay_guard.h; the rules: port/linux/src/crossplay_guard.h).
Everything here comes from a PC machine and is taken as untrusted: names
ended and kept to printable ASCII here, counts as they are (the record's
already checked against this build's limits), the engine as any value.
*/

#include "cseries.h"
#include "interface/ui_widget.h"
#include "networking/network_messages.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "network_wire.h"
#include "network_crossplay_guard.h"
#include "../src/crossplay_guard.h"
#include "../src/lang.h"

#include <stdio.h>
#include <string.h>

/* the platform layer's and the game's */
void platform_log(char const *format, ...);
void platform_show_message(char const *title, char const *message);
void platform_ask_question(char const *title, char const *text);
int platform_question_answer(void);
unsigned long system_milliseconds(void);
short main_get_solo_level_from_name(char const *name);

enum
{
	/* (an advertisement's and a record's game name: 16 wide characters) */
	ADVERTISED_NAME_LENGTH = 16,
	GAME_NAME_SIZE = 17,
	MAP_NAME_SIZE = 0x80,
};

/* the game the client is in (its record's): asked about, said yes */
static struct
{
	void const *client;
	char name[GAME_NAME_SIZE];
	boolean asking;
	boolean accepted;
	/* the controllers told they cannot play split screen (a bit each) */
	unsigned long split_told;
	/* the map of the last record taken (a record of the same map with no
	engine, after the first, is a host between games, not co-op) */
	char record_map[MAP_NAME_SIZE];
	boolean record_seen;
	/* the automatic joins told of a "no" */
	boolean declined_told;
	/* what it was asked as (CROSSPLAY_GUARD_LARGE or _COOP) */
	int verdict;
} network_crossplay_guard;

/* a game's name (a host's wide text, any 16-bit units) as printable ASCII,
ended */
static void name_ascii(
	wchar_t const *wide,
	long count,
	char *ascii,
	long size)
{
	long index, length = 0;

	for (index = 0; wide && index < count && wide[index] && length < size - 1; index++)
	{
		if (wide[index] >= 0x20 && wide[index] < 0x7F)
			ascii[length++] = (char)wide[index];
	}
	ascii[length] = 0;
}

/* whether a game on this map with this engine is co-op: one of the
campaign's levels whatever it says, or no game engine (the campaign's, as
OpenCE lists and hosts co-op) but on one of the Xbox's multiplayer levels
(a PC host between games, or a network test's, sends no engine a moment) */
boolean network_crossplay_guard_coop_game(
	char const *map_name,
	long map_name_size,
	long engine_type)
{
	char map[MAP_NAME_SIZE];
	long length = map_name_size < (long)sizeof(map) ? map_name_size : (long)sizeof(map) - 1;

	if (!map_name || length <= 0)
		return engine_type == 0;
	memcpy(map, map_name, (size_t)length);
	map[length] = 0;
	if (main_get_solo_level_from_name(map) != NONE)
		return TRUE;
	return engine_type == 0 && !crossplay_guard_stock_multiplayer_map(map, (int)sizeof(map));
}

boolean network_crossplay_guard_advertisement(
	wchar_t const *game_name,
	char const *map_name,
	long map_name_size,
	short engine_type,
	long players,
	long maximum,
	boolean tell)
{
	char name[GAME_NAME_SIZE];
	int verdict;

	(void)tell;
	if (!network_wire_crossplay() || network_wire_host_vita())
		return TRUE;
	name_ascii(game_name, ADVERTISED_NAME_LENGTH, name, sizeof(name));
	verdict = crossplay_guard_judge(network_crossplay_guard_coop_game(map_name, map_name_size, engine_type), (int)players,
		(int)maximum);
	switch (verdict)
	{
	case CROSSPLAY_GUARD_COOP:
	case CROSSPLAY_GUARD_LARGE:
		network_event("joining the %s PC game '%s' (%ld of %ld players, experimental): %s",
			verdict == CROSSPLAY_GUARD_COOP ? "co-op" : "large", name, players, maximum,
			crossplay_guard_answered(name, system_milliseconds()) == 1 ? "the player said yes to it" :
			"the player is asked in its lobby");
		break;
	}
	return TRUE;
}

boolean network_crossplay_guard_declined(
	wchar_t const *game_name,
	boolean tell)
{
	char name[GAME_NAME_SIZE];

	if (!network_wire_crossplay() || network_wire_host_vita())
		return FALSE;
	name_ascii(game_name, ADVERTISED_NAME_LENGTH, name, sizeof(name));
	if (crossplay_guard_answered(name, system_milliseconds()) != 0)
		return FALSE;
	if (tell && !network_crossplay_guard.declined_told)
	{
		network_crossplay_guard.declined_told = TRUE;
		network_event("not joining the PC game '%s' again: the player said no to it", name);
	}
	return TRUE;
}

/* what the client's game was asked as ("large" or "co-op"), for logs */
static char const *asked_kind(
	void)
{
	return network_crossplay_guard.verdict == CROSSPLAY_GUARD_COOP ? "co-op" : "large";
}

/* the player's answer about the large or co-op game the client is in:
FALSE (no) leaves it */
static boolean large_answered(
	int yes)
{
	network_crossplay_guard.asking = FALSE;
	crossplay_guard_answer(network_crossplay_guard.name, yes, system_milliseconds());
	if (!yes)
	{
		network_event("leaving the %s PC game '%s': the player said no", asked_kind(), network_crossplay_guard.name);
		return FALSE;
	}
	network_crossplay_guard.accepted = TRUE;
	network_event("playing the %s PC game '%s' (experimental): the player said join anyway", asked_kind(),
		network_crossplay_guard.name);
	return TRUE;
}

boolean network_crossplay_guard_record(
	void const *client,
	struct network_game const *game)
{
	char name[GAME_NAME_SIZE];
	boolean coop;
	int verdict;

	/* (this machine's own server's game: split screen, bots, or a game it
	hosts; a Vita's game, whose host takes the Vitas' 16 and runs a Vita's
	co-op: never large, never a PC's co-op) */
	if (!network_wire_crossplay() || network_wire_host_vita() || !game || global_network_game_server_get())
		return TRUE;
	if (network_crossplay_guard.client != client)
	{
		network_crossplay_guard_reset();
		network_crossplay_guard.client = client;
	}
	name_ascii(game->name, NUMBEROF(game->name), name, sizeof(name));
	{
		char map[MAP_NAME_SIZE];

		csmemcpy(map, game->map.name, sizeof(map) - 1);
		map[sizeof(map) - 1] = 0;
		coop = network_crossplay_guard_coop_game(map, sizeof(map), game->variant.game_engine_index);
		/* (no engine on a map already played here: a PC host between games,
		ChupathingyCE's; a campaign level stays co-op) */
		if (coop && network_crossplay_guard.record_seen && !csstrcmp(map, network_crossplay_guard.record_map) &&
			main_get_solo_level_from_name(map) == NONE)
		{
			coop = FALSE;
		}
		csmemcpy(network_crossplay_guard.record_map, map, sizeof(map));
		network_crossplay_guard.record_seen = TRUE;
	}
	verdict = crossplay_guard_judge(coop, game->player_count, game->maximum_players);
	switch (verdict)
	{
	case CROSSPLAY_GUARD_COOP:
	case CROSSPLAY_GUARD_LARGE:
	{
		int test_answer;

		/* (asked once a game, as what it was first: a large game that turns
		out co-op, or one whose players grow, is not asked again) */
		if (network_crossplay_guard.accepted || network_crossplay_guard.asking)
			return TRUE;
		network_crossplay_guard.verdict = verdict;
		csstrncpy(network_crossplay_guard.name, name, sizeof(network_crossplay_guard.name) - 1);
		network_crossplay_guard.name[sizeof(network_crossplay_guard.name) - 1] = 0;
		/* (asked in the browser: once is enough) */
		if (crossplay_guard_answered(name, system_milliseconds()) == 1)
		{
			network_crossplay_guard.accepted = TRUE;
			network_event("playing the %s PC game '%s' (%d of %d players, experimental): the player said yes in the "
				"browser", asked_kind(), name, (int)game->player_count, (int)game->maximum_players);
			return TRUE;
		}
		platform_log("crossplay: asking about the %s PC game %s (%d of %d players)", asked_kind(), name,
			(int)game->player_count, (int)game->maximum_players);
		test_answer = crossplay_guard_test_answer();
		if (test_answer >= 0)
			return large_answered(test_answer);
		network_crossplay_guard.asking = TRUE;
		platform_ask_question(crossplay_guard_title(verdict), crossplay_guard_question(verdict, game->player_count));
		return TRUE;
	}
	}
	return TRUE;
}

boolean network_crossplay_guard_update(
	void const *client)
{
	int answer;

	if (!network_crossplay_guard.asking)
		return TRUE;
	/* (another client: the game asked about is gone) */
	if (network_crossplay_guard.client != client)
	{
		network_crossplay_guard_reset();
		return TRUE;
	}
	answer = platform_question_answer();
	if (answer < 0)
		return TRUE;
	return large_answered(answer);
}

void network_crossplay_guard_reset(
	void)
{
	if (network_crossplay_guard.asking)
		platform_ask_question(NULL, NULL);
	csmemset(&network_crossplay_guard, 0, sizeof(network_crossplay_guard));
}

boolean network_crossplay_guard_local_player(
	struct network_game const *game,
	short machine_index,
	short local_player_index,
	boolean const *requested,
	short requested_count)
{
	long index, others = 0;

	if (!network_wire_crossplay() || !game || global_network_game_server_get())
		return TRUE;
	/* (this machine's players in the game, and those asked for, but this
	controller's) */
	for (index = 0; index < (long)NUMBEROF(game->players); index++)
	{
		struct network_player const *player = &game->players[index];

		if (network_player_is_valid((struct network_player *)player) && player->machine_index == machine_index &&
			player->controller_index != local_player_index && player->controller_index >= 0)
		{
			others++;
		}
	}
	for (index = 0; requested && index < requested_count; index++)
	{
		if (index != local_player_index && requested[index])
			others++;
	}
	if (others < CROSSPLAY_GUARD_LOCAL_PLAYERS)
		return TRUE;
	if (local_player_index >= 0 && local_player_index < 32 &&
		!(network_crossplay_guard.split_told & (1UL << local_player_index)))
	{
		network_crossplay_guard.split_told |= 1UL << local_player_index;
		platform_log("crossplay: not adding local player %d: one player per Vita in PC games (split screen not measured yet)",
			(int)local_player_index);
		display_error_text_deferred(
			TW(L"Crossplay games take\r\none player per Vita\r\nfor now: no split\r\nscreen in PC games."),
			local_player_index);
	}
	return FALSE;
}
