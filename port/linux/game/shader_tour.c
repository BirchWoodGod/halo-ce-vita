/*
SHADER_TOUR.C

(port, debug) HALO_SHADER_TOUR=<seconds>: a tour of the level for collecting
the GPU programs it needs (HALO_SHADER_COLLECT, tools/vita_shader_pack.py)
without anyone playing. Every <seconds> of game time the player is
teleported to the next of the scenario's cutscene flags that lies in the
loaded structure (the scripts' own marks, spread through the level), and
once a structure's flags are done the next structure is switched to. The
player is deathless; the level's scripts run as they would, so encounters
and cinematics start as the player turns up near them. Logs each stop and
"shader tour: done" at the end.

A multiplayer map has no cutscene flags: its tour goes round the player
starting locations instead (spread over the map for the game types'
spawns), facing each one's way and then the opposite way, round and round
until the program exits. It is how the harness walks a Custom Edition map
through its textures and objects as a player running about would (the
texture caches, object counts over time).

Called from the main loop with the test commands (main.c).
*/

#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "hs/hs.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"

#include <math.h>
#include <stdlib.h>

void platform_log(char const *format, ...);
void hs_object_teleport(long object_index, short cutscene_flag_index);
boolean hs_compile_and_evaluate(char const *source);

struct shader_tour_flag
{
	long runtime_unused;
	char name[TAG_STRING_LENGTH];
	real_point3d position;
	real_euler_angles2d facing;
	byte unused[0x24];
};

void halo_shader_tour_update(
	void)
{
	static long interval = -1;
	static long next_tick, stop;
	static boolean done;
	struct scenario *scenario;
	long flag_count, bsp_count;

	if (interval < 0)
	{
		const char *setting = getenv("HALO_SHADER_TOUR");

		interval = setting ? atol(setting) * 30 : 0;
		next_tick = interval;
	}
	if (interval <= 0 || done || !game_in_progress() || game_time_get() < next_tick)
		return;
	next_tick = game_time_get() + interval;
	scenario = global_scenario_get();
	flag_count = scenario->cutscene_flags.count;
	bsp_count = scenario->structure_bsp_references.count;
	if (stop == 0)
		hs_compile_and_evaluate("(set cheat_deathless_player true)");
	if (flag_count == 0 && scenario->players.count > 0)
	{
		/* (multiplayer: the starting locations, both ways, endlessly) */
		long location_index = (stop / 2) % scenario->players.count;
		struct player_starting_location *location = TAG_BLOCK_GET_ELEMENT(&scenario->players, location_index,
			struct player_starting_location);
		real facing = location->facing + ((stop & 1) ? 3.14159265f : 0.0f);
		real_vector3d forward;
		struct data_iterator iterator;
		struct player_datum *player;

		forward.i = (real)cos(facing);
		forward.j = (real)sin(facing);
		forward.k = 0.0f;
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->unit_index != NONE)
			{
				if (!(stop & 1))
				{
					platform_log("shader tour: starting location %ld/%ld (%.1f %.1f %.1f)", location_index,
						(long)scenario->players.count, location->position.x, location->position.y, location->position.z);
					player_teleport(iterator.datum_index, NONE, &location->position);
				}
				if (player->local_player_index != NONE)
					player_control_set_facing(player->local_player_index, &forward);
				break;
			}
		}
		stop++;
		return;
	}
	if (flag_count == 0)
	{
		/* (no flags and no starting locations: the main menu's scenario;
		a multiplayer map may follow) */
		return;
	}
	while (stop < flag_count * bsp_count)
	{
		short bsp = (short)(stop / flag_count);
		short flag_index = (short)(stop % flag_count);
		struct shader_tour_flag *flag;
		struct location location;
		struct data_iterator iterator;
		struct player_datum *player;

		if (bsp != global_structure_bsp_index_get())
		{
			char command[32];

			sprintf(command, "(switch_bsp %d)", bsp);
			platform_log("shader tour: structure %d", bsp);
			hs_compile_and_evaluate(command);
			if (bsp != global_structure_bsp_index_get())
				stop = (long)(bsp + 1) * flag_count;
			return;
		}
		stop++;
		flag = TAG_BLOCK_GET_ELEMENT(&scenario->cutscene_flags, flag_index, struct shader_tour_flag);
		scenario_location_from_point(&location, &flag->position);
		if (location.cluster_index == NONE)
			continue;
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->unit_index != NONE)
			{
				platform_log("shader tour: structure %d flag %d/%ld %s", bsp, flag_index, flag_count, flag->name);
				hs_object_teleport(player->unit_index, flag_index);
				break;
			}
		}
		return;
	}
	done = TRUE;
	platform_log("shader tour: done");
}
