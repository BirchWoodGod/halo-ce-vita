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

Called from the main loop with the test commands (main.c).
*/

#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "hs/hs.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"

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
