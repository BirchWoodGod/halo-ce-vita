/*
COOP_PROGRESS.C

Network co-op's campaign progress: a level won together is recorded in the
profile of each player of this machine (player_profile_save_level_completed_at,
the level's flag for its difficulty), as a solo or split screen win records
it, so that the Campaign screen offers the next level.

- The host knows the level was won (main.c's main_won_map_private): it
  records it at once.
- A client is told nothing but what follows: the round ends, the host sends
  the lobby (switching to pregame), and the lobby's map is the campaign's
  next level (The Maw's: The Pillar of Autumn, network_game_server_port_
  cooperative_won). A lobby on the same level (the host quit it, or the
  players died and it restarts), or on another, records nothing; nor one of
  another game (the client gone in between).

Before this, network co-op recorded nothing: the host's record read the
level from the solo map name, which a network game never sets, and the
clients made none.
*/

#include "cseries.h"
#include "main/main.h"
#include "game/game.h"
#include "networking/network_connection.h"
#include "networking/network_client_manager.h"
#include "saved games/player_profile.h"
#include "interface/player_ui.h"

#include "coop_progress.h"

void platform_log(char const *format, ...);
short main_get_solo_level_from_name(char const *map_name);

static struct
{
	/* the client's level and difficulty while it plays one, NONE else */
	short level;
	short difficulty;
	/* whose (the client of that game) */
	void const *client;
	/* the round ended: the lobby's map decides */
	boolean round_ended;
} coop_progress = { NONE, 0, NULL, FALSE };

static void record(short level, short difficulty, char const *who)
{
	short local_player_index;
	short recorded = 0;

	if (level == NONE || level < 0 || level >= NUMBER_OF_SINGLE_PLAYER_LEVELS || difficulty < 0 ||
		difficulty >= NUMBER_OF_GAME_DIFFICULTY_LEVELS)
	{
		return;
	}
	for (local_player_index = 0; local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; local_player_index++)
	{
		if (player_ui_get_active_player_profile_index(local_player_index) == NONE)
			continue;
		player_profile_save_level_completed_at(local_player_index, level, difficulty);
		recorded++;
	}
	platform_log("co-op: level %d won on difficulty %d (%s): recorded in %d profile%s", (int)level + 1, (int)difficulty,
		who, (int)recorded, recorded == 1 ? "" : "s");
}

void coop_progress_host_won(char const *map_name, short difficulty)
{
	record(map_name ? main_get_solo_level_from_name(map_name) : NONE, difficulty, "this machine hosted it");
}

void coop_progress_client_level_started(char const *map_name, short difficulty)
{
	coop_progress.level = map_name ? main_get_solo_level_from_name(map_name) : NONE;
	coop_progress.difficulty = difficulty;
	coop_progress.client = global_network_game_client_get();
	coop_progress.round_ended = FALSE;
}

void coop_progress_client_round_ended(void)
{
	coop_progress.round_ended = coop_progress.level != NONE && coop_progress.client == global_network_game_client_get();
}

void coop_progress_client_lobby_map(char const *map_name)
{
	short level;

	if (!coop_progress.round_ended)
		return;
	coop_progress.round_ended = FALSE;
	level = coop_progress.level;
	coop_progress.level = NONE;
	if (coop_progress.client != global_network_game_client_get() || !map_name)
		return;
	if (main_get_solo_level_from_name(map_name) == (level + 1) % NUMBER_OF_SINGLE_PLAYER_LEVELS)
		record(level, coop_progress.difficulty, "the host went on to the next level");
}
