/*
CUSTOM_EDITION_OBJECTS.C

The multiplayer vehicles of Halo Custom Edition maps (custom_edition_cache.h).

This build chooses a multiplayer game's vehicles by type: only the first
three of the globals' multiplayer vehicles are ever created, and of those
the ones the game variant's vehicle set names (game_engine_remap_vehicle).
Retail Halo, whose rules Custom Edition maps are made for, chooses them by
placement: a vehicle placement's multiplayer spawn flags name the game
types it is placed in by default, and a script may create any vehicle.
Custom Edition maps depend on that. hugeass.map places one jet by default
only in oddball, and its scripts recreate that jet while it exists and turn
the map to night when a biped under it dies; placed in a slayer game, the
jet is recreated on the biped and the map turns to night at once. So for a
Custom Edition map the native builds place the vehicles whose spawn flags
name the game type by default, and let a script create any vehicle, unless
the variant has no vehicles. Race has no spawn flag, and keeps this build's
rule.

(port) PC map vehicles (game.pc_map_vehicles, HALO_PC_MAP_VEHICLES), for a
game of this machine's alone (split screen, offline, with or without bots:
network_game_is_splitscreen_local). Of the placements so chosen, an Xbox
variant's default vehicle set keeps the Warthogs, Ghosts and Scorpions
(game_engine_vehicle_placement_allowed; Slayer's own set is Warthogs
alone), so Halo PC's maps lose their
Banshees, Rocket Warthogs and gun turrets: Death Island, Gephyrophobia and
Infinity place Banshees by default, Blood Gulch two only "allowed" (Halo
PC's game variants turn such ones on). "xbox" (the default) keeps that;
"pc" places every type a placement's default flag names, as Halo PC's
default set does; "all" also places those the map only allows in the game
type (Blood Gulch's Banshees, and its every Ghost and Scorpion). Every
machine of a network game places a map's vehicles itself, and no wire
carries this choice, so a network game's vehicles (hosted, joined, or the
dedicated server's) are never changed by it. "pc" and "all" take the place
of the Xbox variant's vehicle set, as a PC host's set does; a variant with
no vehicles keeps none.
*/

/* ---------- headers */

#include "cseries.h"
#include "math/real_math.h"
#include "tag_files/tag_groups.h"
#include "game/game_engine.h"
#include "scenario/scenario_definitions.h"
#include "networking/network_game_globals.h"
#include "cache_file_formats.h"
#include "custom_edition_cache.h"
#include "../src/port_config.h"

#include <stdlib.h>
#include <string.h>

/* ---------- constants */

/* the game types, as game_engine_list.c orders their engines */
enum
{
	_game_engine_ctf = 1,
	_game_engine_slayer,
	_game_engine_oddball,
	_game_engine_king,
	_game_engine_race,
};

/* a variant's vehicle set that places no vehicle (game_engine_remap_vehicle) */
#define VEHICLE_SET_NONE 1

/* the game types a vehicle placement is placed in by default; those it is
allowed in are the same bits eight higher (Invader's
ScenarioVehicleMultiplayerSpawnFlags) */
enum
{
	_multiplayer_spawn_slayer_default_bit = 0,
	_multiplayer_spawn_ctf_default_bit,
	_multiplayer_spawn_king_default_bit,
	_multiplayer_spawn_oddball_default_bit,
	MULTIPLAYER_SPAWN_ALLOWED_SHIFT = 8
};

/* the PC map vehicles (the description above) */
enum
{
	_pc_map_vehicles_xbox,
	_pc_map_vehicles_pc,
	_pc_map_vehicles_all
};

/* ---------- structures */

/* A vehicle placement: a scenario object, a unit's fields, and then the
vehicle's own, of which the multiplayer team and spawn flags come first.
OpenSauce (scenario_object_definitions_structures.hpp, s_scenario_vehicle)
leaves those 0x20 bytes unnamed; in hugeass.map the byte at 0x58 is 1 on
exactly the blue team's vehicles, and the word at 0x5A is 0x0303, 0x0404,
0x0808, 0x0F0F or 0 by vehicle, the game types' default and allowed bits
(docs/custom_edition_caches.md). */
struct custom_edition_vehicle_placement
{
	struct scenario_object_datum object;
	byte unit[0x30];
	char multiplayer_team_index;
	byte pad59;
	word multiplayer_spawn_flags;
	byte unused[0x1C];
};

typedef char verify_custom_edition_vehicle_placement_spawn_flags_offset[
	offsetof(struct custom_edition_vehicle_placement, multiplayer_spawn_flags) == 0x5A ? 1 : -1];
typedef char verify_custom_edition_vehicle_placement_size[
	sizeof(struct custom_edition_vehicle_placement) == 0x78 ? 1 : -1];

/* ---------- private code */

/* the spawn flag that places a vehicle by default in the running game type,
or NONE */
static short default_spawn_flag_bit(
	void)
{
	switch (game_engine_get_variant()->game_engine_index)
	{
	case _game_engine_slayer:
		return _multiplayer_spawn_slayer_default_bit;
	case _game_engine_ctf:
		return _multiplayer_spawn_ctf_default_bit;
	case _game_engine_king:
		return _multiplayer_spawn_king_default_bit;
	case _game_engine_oddball:
		return _multiplayer_spawn_oddball_default_bit;
	default:
		return NONE;
	}
}

/* the PC map vehicles of the running game: the Xbox's in any game but a
Custom Edition map's of this machine's alone */
static short pc_map_vehicles(
	void)
{
	static short logged = NONE;
	char const *value;
	short choice = _pc_map_vehicles_xbox;

	if (!custom_edition_vehicles_by_placement() || !network_game_is_splitscreen_local())
		return _pc_map_vehicles_xbox;
	value = getenv("HALO_PC_MAP_VEHICLES");
	if (!value || !value[0])
		value = config_string("game.pc_map_vehicles");
	if (value && !strcmp(value, "pc"))
		choice = _pc_map_vehicles_pc;
	else if (value && !strcmp(value, "all"))
		choice = _pc_map_vehicles_all;
	if (choice != logged)
	{
		void platform_log(char const *format, ...);

		logged = choice;
		platform_log("custom edition: PC map vehicles in this local game: %s",
			choice == _pc_map_vehicles_all ? "all the map allows" : choice == _pc_map_vehicles_pc ? "Halo PC's" : "the Xbox's");
	}
	return choice;
}

/* ---------- public code */

boolean custom_edition_vehicles_by_placement(
	void)
{
	return custom_edition_cache_tags_loaded() &&
		game_engine_running() &&
		game_engine_get_variant()->universal_variant.vehicle_set != VEHICLE_SET_NONE &&
		default_spawn_flag_bit() != NONE;
}

boolean custom_edition_vehicle_placement_allowed(
	struct scenario_object_datum const *placement)
{
	struct custom_edition_vehicle_placement const *vehicle = (struct custom_edition_vehicle_placement const *)placement;

	if (!custom_edition_vehicles_by_placement())
		return TRUE;
	return TEST_FLAG(vehicle->multiplayer_spawn_flags, default_spawn_flag_bit()) ||
		(pc_map_vehicles() == _pc_map_vehicles_all &&
			TEST_FLAG(vehicle->multiplayer_spawn_flags, default_spawn_flag_bit() + MULTIPLAYER_SPAWN_ALLOWED_SHIFT));
}

boolean custom_edition_vehicles_every_type(
	void)
{
	return pc_map_vehicles() != _pc_map_vehicles_xbox;
}
