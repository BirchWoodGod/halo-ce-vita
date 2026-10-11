/*
NETWORK_GAME_RECORD.C

The game settings record on the legacy Vita wire and on the crossplay wire
(network_game_record.h), and the gametype's PC options' defaults and checks
(game_engine.h's struct game_variant_options, from OpenCE c9ee319a and
6b1d81c7, CC0).
*/

#include "cseries.h"
#include "game/game_engine.h"
#include "networking/network_game_manager.h"

#include "network_game_record.h"

/* the platform layer's (port/linux/src/port_config.c) */
int config_boolean(const char *name);

/* ---------- the layouts */

typedef char network_game_record_options_offset_assert[
	offsetof(struct network_game, variant_options) == HALO_PORT_NETWORK_GAME_VARIANT_OPTIONS_OFFSET ? 1 : -1];
typedef char network_game_record_options_size_assert[
	sizeof(struct game_variant_options) == HALO_PORT_NETWORK_GAME_VARIANT_OPTIONS_SIZE ? 1 : -1];
typedef char network_game_record_local_data_assert[
	offsetof(struct network_game, local_data) == HALO_PORT_NETWORK_GAME_LOCAL_DATA_OFFSET ? 1 : -1];
typedef char network_game_record_size_assert[
	sizeof(struct network_game) == HALO_PORT_NETWORK_GAME_SIZE ? 1 : -1];
typedef char network_game_record_cooperative_flags_assert[
	offsetof(struct network_game, cooperative_flags) == 0x10C ? 1 : -1];
/* (the legacy record: the same to the options, then the local data) */
typedef char network_game_record_legacy_assert[
	HALO_PORT_NETWORK_GAME_LEGACY_LOCAL_DATA_OFFSET == HALO_PORT_NETWORK_GAME_VARIANT_OPTIONS_OFFSET &&
	HALO_PORT_NETWORK_GAME_LEGACY_SIZE ==
		HALO_PORT_NETWORK_GAME_LEGACY_LOCAL_DATA_OFFSET + sizeof(struct network_game_local_data) ? 1 : -1];

/* ---------- the PC options */

enum
{
	/* (game_engine.c's enum game_engine_vehicles: default, none, then the
	warthogs' set, the first of a type alone) */
	VARIANT_VEHICLE_SET_FIRST_TYPE = 2
};

void game_variant_options_default(
	struct game_variant const *variant,
	struct game_variant_options *options)
{
	/* (OpenCE's game_variant_options_default, c9ee319a) */
	csmemset(options, 0, sizeof(*options));
	options->friendly_fire = _friendly_fire_on;
	options->loadout = _loadout_category;
	options->primary_weapon = _loadout_weapon_assault_rifle;
	options->secondary_weapon = _loadout_weapon_pistol;
	options->radar_players = variant &&
		!TEST_FLAG(variant->universal_variant.flags, _game_variant_draw_object_in_motion_sensor_bit) ?
		_radar_players_none : _radar_players_all;
	/* (port: the variant's own set, pinned as game_engine_variant_cleanup
	pins it: the Xbox's five, never a set of the PC's) */
	options->vehicle_set[0] = options->vehicle_set[1] = (byte)(variant ?
		PIN(variant->universal_variant.vehicle_set, 0, VARIANT_VEHICLE_SET_FIRST_TYPE + _variant_vehicle_scorpion) : 0);

	return;
}

/* a team's vehicle set: one of the variant's sets (default, none, warthogs,
ghosts, scorpions), one type alone (rocket warthogs, banshees, gun turrets:
the sets past the Xbox's, as OpenCE's menus offer them), the map's own
placements (PC, OpenCE's network version 24) or the counts (custom) */
static boolean game_variant_vehicle_set_valid(
	byte set)
{
	return set < VARIANT_VEHICLE_SET_FIRST_TYPE + NUMBER_OF_VARIANT_VEHICLES ||
		set == VARIANT_VEHICLE_SET_PC ||
		set == VARIANT_VEHICLE_SET_CUSTOM;
}

boolean game_variant_options_valid(
	struct game_variant_options const *options)
{
	short side;
	short type;

	/* (minutes and seconds: none negative; a team killer's penalty in ticks
	fits the short it is kept in, game_engine_player_killed's) */
	if (options->time_limit < 0 ||
		options->vehicle_respawn_time < 0 ||
		options->friendly_fire_penalty < 0 ||
		options->friendly_fire_penalty > SHORT_MAX / TICKS_PER_SECOND)
	{
		return FALSE;
	}
	if (!VALID_INDEX(options->friendly_fire, NUMBER_OF_FRIENDLY_FIRE_MODES) ||
		options->radar_players >= NUMBER_OF_RADAR_PLAYERS ||
		options->loadout >= NUMBER_OF_LOADOUTS ||
		options->primary_weapon >= NUMBER_OF_LOADOUT_WEAPONS ||
		options->secondary_weapon >= NUMBER_OF_LOADOUT_WEAPONS ||
		(options->auto_team_balance != FALSE && options->auto_team_balance != TRUE) ||
		(options->no_map_weapons != FALSE && options->no_map_weapons != TRUE))
	{
		return FALSE;
	}
	for (side = 0; side < 2; side++)
	{
		if (!game_variant_vehicle_set_valid(options->vehicle_set[side]))
			return FALSE;
		for (type = 0; type < NUMBER_OF_VARIANT_VEHICLES; type++)
		{
			if (options->vehicle_counts[side][type] > MAXIMUM_VARIANT_VEHICLE_COUNT)
				return FALSE;
		}
	}

	return TRUE;
}

boolean network_game_cooperative_flags_valid(
	byte flags)
{
	return (flags & ~(FLAG(NUMBER_OF_NETWORK_GAME_COOPERATIVE_FLAGS) - 1)) == 0;
}

/* ---------- the wires */

short network_game_record_format_current(
	void)
{
	return config_boolean("network.crossplay") ? _network_game_record_crossplay : _network_game_record_legacy;
}

long network_game_record_size(
	short format)
{
	switch (format)
	{
	case _network_game_record_legacy: return HALO_PORT_NETWORK_GAME_LEGACY_SIZE;
	case _network_game_record_crossplay: return HALO_PORT_NETWORK_GAME_SIZE;
	}
	return 0;
}

boolean network_game_record_encode(
	short format,
	struct network_game const *game,
	void *buffer,
	long buffer_size)
{
	byte *bytes = (byte *)buffer;

	if (!VALID_INDEX(format, NUMBER_OF_NETWORK_GAME_RECORD_FORMATS) || buffer_size < network_game_record_size(format))
		return FALSE;
	if (format == _network_game_record_crossplay)
	{
		csmemcpy(bytes, game, sizeof(*game));
		return TRUE;
	}
	/* the betas' record: the Xbox's to the options, its padding byte 0, then
	the local data */
	csmemcpy(bytes, game, HALO_PORT_NETWORK_GAME_VARIANT_OPTIONS_OFFSET);
	bytes[offsetof(struct network_game, cooperative_flags)] = 0;
	csmemcpy(bytes + HALO_PORT_NETWORK_GAME_LEGACY_LOCAL_DATA_OFFSET, &game->local_data, sizeof(game->local_data));

	return TRUE;
}

boolean network_game_record_decode(
	short format,
	void const *buffer,
	long size,
	struct network_game *game,
	char const **why)
{
	byte const *bytes = (byte const *)buffer;
	char const *reason = NULL;

	if (!VALID_INDEX(format, NUMBER_OF_NETWORK_GAME_RECORD_FORMATS) || size != network_game_record_size(format))
	{
		reason = "its size is not this wire's";
	}
	else if (format == _network_game_record_crossplay)
	{
		csmemcpy(game, bytes, sizeof(*game));
		if (!network_game_cooperative_flags_valid(game->cooperative_flags))
			reason = "co-op switches this game does not know";
		else if (!game_variant_options_valid(&game->variant_options))
			reason = "gametype options out of range";
	}
	else
	{
		csmemcpy(game, bytes, HALO_PORT_NETWORK_GAME_VARIANT_OPTIONS_OFFSET);
		/* (the betas send the Xbox's padding: never a switch) */
		game->cooperative_flags = 0;
		game_variant_options_default(&game->variant, &game->variant_options);
		/* (the host's local data, which the client puts its own over:
		network_game_client_game_settings_updated) */
		csmemcpy(&game->local_data, bytes + HALO_PORT_NETWORK_GAME_LEGACY_LOCAL_DATA_OFFSET, sizeof(game->local_data));
	}
	if (why)
		*why = reason;

	return reason == NULL;
}
