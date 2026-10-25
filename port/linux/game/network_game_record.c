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
#include "network_wire.h"
#include "halo_port_limits.h"

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
	return network_wire_crossplay() ? _network_game_record_crossplay : _network_game_record_legacy;
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

/* ---------- OpenCE's network versions 33 to 38 (network_wire.h)

As ChupathingyCE reads them (2d2ad1d8, GPL: port/linux/game/network_rules.h
and .c, "network versions 33 to 38"): from 35 the gametype's options are
0x2C bytes, not 0x1C - the 16 further bytes before the record's last 4, its
local data -, and say which of OpenCE's own game types the game plays (the
mode, on the Slayer engine: SWAT on any, Infection with teams, Gun Game free
for all) and whether it is Hardcore; from 33 the co-op byte's bit 1 is a
switch of the host's alone. This build plays none of OpenCE's game types:
a record that plays one is refused, its reason the game type's name. */

enum
{
	FURTHER_OPTIONS_SIZE = 16,
	/* (the further options' bytes: network_rules.h's NETWORK_RULES_OPTION_*) */
	FURTHER_OPTION_MODE = 0,
	FURTHER_OPTION_HARDCORE = 1,
	/* (bytes 10 to 15 reserved, 0) */
	FURTHER_OPTION_RESERVED = 10,
	/* (the modes: network_rules.h's NETWORK_RULES_MODE_*) */
	FURTHER_MODE_NONE = 0,
	FURTHER_MODE_SWAT,
	FURTHER_MODE_INFECTION,
	FURTHER_MODE_GUN_GAME,
	NUMBER_OF_FURTHER_MODES,
	/* (the co-op byte's bit 1, version 33: the players' hits spare their AI
	allies, ChupathingyCE's _network_game_cooperative_no_ally_damage_bit) */
	COOPERATIVE_NO_ALLY_DAMAGE_BIT = 1,
};

long network_game_record_wire_size(
	short format,
	unsigned int version)
{
	long size = network_game_record_size(format);

	return size && format == _network_game_record_crossplay && version >= 35 ? size + FURTHER_OPTIONS_SIZE : size;
}

/* the game type of OpenCE's that a record's further options play, by
ChupathingyCE's network_rules_extended_options_mode (the mode counted on the
Slayer engine alone, Infection with teams alone, Gun Game free for all
alone; Hardcore on any engine): its name, or NULL for none */
static char const *further_options_game_type(
	byte const *further,
	struct network_game const *game)
{
	int mode = further[FURTHER_OPTION_MODE];
	boolean slayer = game->variant.game_engine_index == game_engine_slayer;
	boolean teams = game->variant.universal_variant.teams != 0;

	if (!slayer || (mode == FURTHER_MODE_INFECTION && !teams) || (mode == FURTHER_MODE_GUN_GAME && teams))
		mode = FURTHER_MODE_NONE;
	switch (mode)
	{
	case FURTHER_MODE_SWAT: return "OpenCE's SWAT game type, which this build does not play";
	case FURTHER_MODE_INFECTION: return "OpenCE's Infection game type, which this build does not play";
	case FURTHER_MODE_GUN_GAME: return "OpenCE's Gun Game game type, which this build does not play";
	}
	if (further[FURTHER_OPTION_HARDCORE])
		return "OpenCE's Hardcore game type, which this build does not play";
	return NULL;
}

boolean network_game_record_decode(
	short format,
	void const *buffer,
	long size,
	struct network_game *game,
	char const **why)
{
	return network_game_record_decode_version(format, HALO_PORT_CROSSPLAY_NETWORK_VERSION, buffer, size, game, why);
}

boolean network_game_record_decode_version(
	short format,
	unsigned int version,
	void const *buffer,
	long size,
	struct network_game *game,
	char const **why)
{
	byte const *bytes = (byte const *)buffer;
	char const *reason = NULL;

	if (!VALID_INDEX(format, NUMBER_OF_NETWORK_GAME_RECORD_FORMATS) || size != network_game_record_wire_size(format, version))
	{
		reason = "its size is not this wire's";
	}
	else if (format == _network_game_record_crossplay)
	{
		byte cooperative_known = (byte)(FLAG(NUMBER_OF_NETWORK_GAME_COOPERATIVE_FLAGS) - 1);
		byte const *further = NULL;
		short index;

		if (size == network_game_record_size(format))
			csmemcpy(game, bytes, sizeof(*game));
		else
		{
			/* (35 on: the record to its local data, then the further options,
			then the local data) */
			further = bytes + HALO_PORT_NETWORK_GAME_LOCAL_DATA_OFFSET;
			csmemcpy(game, bytes, HALO_PORT_NETWORK_GAME_LOCAL_DATA_OFFSET);
			csmemcpy(&game->local_data, further + FURTHER_OPTIONS_SIZE, sizeof(game->local_data));
		}
		/* (33 on: the host's own switch, known, and not kept: this build's
		code reads none of it) */
		if (version >= 33)
			cooperative_known |= (byte)FLAG(COOPERATIVE_NO_ALLY_DAMAGE_BIT);
		if (game->cooperative_flags & ~cooperative_known)
			reason = "co-op switches this game does not know";
		else if (!game_variant_options_valid(&game->variant_options))
			reason = "gametype options out of range";
		else if (further && (further[FURTHER_OPTION_MODE] >= NUMBER_OF_FURTHER_MODES || further[FURTHER_OPTION_HARDCORE] > 1))
			reason = "OpenCE's gametype options out of range";
		else if (further)
			reason = further_options_game_type(further, game);
		for (index = FURTHER_OPTION_RESERVED; !reason && further && index < FURTHER_OPTIONS_SIZE; index++)
		{
			if (further[index])
				reason = "OpenCE's reserved gametype options not zero";
		}
		game->cooperative_flags &= (byte)(FLAG(NUMBER_OF_NETWORK_GAME_COOPERATIVE_FLAGS) - 1);
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
