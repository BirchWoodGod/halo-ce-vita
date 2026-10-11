/*
NET_FUZZ_VARIANT_OPTIONS.C

A fuzz target for the game settings record a host sends
(port/linux/game/network_game_record.c): on the crossplay wire OpenCE's
record, with the gametype's PC options (struct game_variant_options) and
co-op's switches, which must be refused unless every value is one this game
can play by; on the legacy Vita wire the betas' record, which gets the
variant's default options.

An input is a kind byte, then the bytes:
  0  a crossplay record: the bytes (cut or padded with zeros to the record's
     size), decoded
  1  a legacy record, the same
  2  a crossplay record of the input's own size (any size must be refused
     but the record's)
  3  the PC options alone (struct game_variant_options), checked
A record that decodes must have options and switches in range (checked here
on their own, field by field, as the game reads them), and encode again to
the bytes it came from (crossplay), or to the betas' record (legacy).

Built as the game's code is (its flags, from build.ninja; DEBUG) by
run_net_fuzz_test.sh, with network_game_record.c.
*/

#include "cseries.h"
#include "game/game_engine.h"
#include "networking/network_game_manager.h"

#include <stdint.h>

#include "network_game_record.h"
#include "net_fuzz_game.h"

/* ---------- what the game gives the code under test */

int config_boolean(const char *name)
{
	(void)name;
	return 0;
}

/* ---------- the target */

/* the options as the game reads them: every value an index or a time it can
use (game_engine.c, units.c, bipeds.c, motion_sensor.c) */
static int fuzz_options_in_range(struct game_variant_options const *options)
{
	short side, type;

	if (options->time_limit < 0 || options->vehicle_respawn_time < 0 || options->friendly_fire_penalty < 0 ||
		(long)options->friendly_fire_penalty * TICKS_PER_SECOND > SHORT_MAX)
	{
		return 0;
	}
	if (options->friendly_fire < 0 || options->friendly_fire >= NUMBER_OF_FRIENDLY_FIRE_MODES ||
		options->radar_players >= NUMBER_OF_RADAR_PLAYERS || options->loadout >= NUMBER_OF_LOADOUTS ||
		options->primary_weapon >= NUMBER_OF_LOADOUT_WEAPONS || options->secondary_weapon >= NUMBER_OF_LOADOUT_WEAPONS ||
		options->auto_team_balance > 1 || options->no_map_weapons > 1)
	{
		return 0;
	}
	for (side = 0; side < 2; side++)
	{
		byte set = options->vehicle_set[side];

		/* (default, none, a set of one type each, PC, custom) */
		if (!(set < 2 + NUMBER_OF_VARIANT_VEHICLES || set == VARIANT_VEHICLE_SET_PC || set == VARIANT_VEHICLE_SET_CUSTOM))
			return 0;
		for (type = 0; type < NUMBER_OF_VARIANT_VEHICLES; type++)
		{
			if (options->vehicle_counts[side][type] > MAXIMUM_VARIANT_VEHICLE_COUNT)
				return 0;
		}
	}
	return 1;
}

static byte fuzz_record[HALO_PORT_NETWORK_GAME_SIZE];
static byte fuzz_again[HALO_PORT_NETWORK_GAME_SIZE];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct network_game *game;
	long record_size;
	short format;
	byte kind;
	char const *why;

	if (size < 1)
		return 0;
	kind = data[0];
	data++;
	size--;
	/* (in an allocation of its own, as large as the record: a write past
	it is caught) */
	game = malloc(sizeof(*game));
	memset(game, 0xA5, sizeof(*game));
	switch (kind % 4)
	{
	case 0:
	case 1:
		format = kind % 4 == 0 ? _network_game_record_crossplay : _network_game_record_legacy;
		record_size = network_game_record_size(format);
		memset(fuzz_record, 0, sizeof(fuzz_record));
		memcpy(fuzz_record, data, size < (size_t)record_size ? size : (size_t)record_size);
		why = NULL;
		if (network_game_record_decode(format, fuzz_record, record_size, game, &why))
		{
			if (why || !fuzz_options_in_range(&game->variant_options) ||
				!network_game_cooperative_flags_valid(game->cooperative_flags))
			{
				abort();
			}
			memset(fuzz_again, 0xEE, sizeof(fuzz_again));
			if (!network_game_record_encode(format, game, fuzz_again, sizeof(fuzz_again)))
				abort();
			if (format == _network_game_record_crossplay)
			{
				if (memcmp(fuzz_again, fuzz_record, (size_t)record_size))
					abort();
			}
			else
			{
				struct game_variant_options defaults;

				/* (the betas' record again, but the padding byte, which is
				sent as 0; the variant's default options, no switches) */
				fuzz_record[offsetof(struct network_game, cooperative_flags)] = 0;
				game_variant_options_default(&game->variant, &defaults);
				if (memcmp(fuzz_again, fuzz_record, (size_t)record_size) || game->cooperative_flags != 0 ||
					memcmp(&defaults, &game->variant_options, sizeof(defaults)))
				{
					abort();
				}
			}
		}
		else if (!why)
		{
			abort();
		}
		break;
	case 2:
		if ((long)size != network_game_record_size(_network_game_record_crossplay) &&
			network_game_record_decode(_network_game_record_crossplay, data, (long)size, game, NULL))
		{
			abort();
		}
		break;
	case 3:
	{
		struct game_variant_options options;

		memset(&options, 0, sizeof(options));
		memcpy(&options, data, size < sizeof(options) ? size : sizeof(options));
		if (game_variant_options_valid(&options) != fuzz_options_in_range(&options))
			abort();
		break;
	}
	}
	free(game);

	return 0;
}

/* ---------- the target's checks */

void net_fuzz_write_seed(char const *folder, char const *name, void const *data, unsigned long size);

static int fuzz_failures = 0;

static void fuzz_check(char const *name, int passed)
{
	if (!passed)
	{
		__builtin_printf("FAIL variant options: %s\n", name);
		fuzz_failures++;
	}
}

int net_fuzz_checks(void)
{
	static struct network_game game, decoded;
	static byte record[HALO_PORT_NETWORK_GAME_SIZE + 1];
	struct game_variant_options options;
	char const *folder = getenv("NET_FUZZ_SEED_DIR");
	short set;
	short side, type;

	/* the defaults of every variant: in range */
	for (set = 0; set < 5; set++)
	{
		struct game_variant variant;

		memset(&variant, 0, sizeof(variant));
		variant.universal_variant.vehicle_set = set;
		variant.universal_variant.flags = set & 1 ? FLAG(_game_variant_draw_object_in_motion_sensor_bit) : 0;
		game_variant_options_default(&variant, &options);
		fuzz_check("a variant's defaults are valid", game_variant_options_valid(&options));
		fuzz_check("a variant's defaults are its vehicle set", options.vehicle_set[0] == set && options.vehicle_set[1] == set);
		fuzz_check("a variant's defaults are its motion sensor",
			options.radar_players == (set & 1 ? _radar_players_all : _radar_players_none));
	}

	/* every value OpenCE's menus give: taken (port/linux/game/menu_functions.c's) */
	game_variant_options_default(NULL, &options);
	for (set = 0; set < 2 + NUMBER_OF_VARIANT_VEHICLES; set++)
	{
		options.vehicle_set[0] = options.vehicle_set[1] = (byte)set;
		fuzz_check("each vehicle set", game_variant_options_valid(&options));
	}
	options.vehicle_set[0] = VARIANT_VEHICLE_SET_PC;
	options.vehicle_set[1] = VARIANT_VEHICLE_SET_CUSTOM;
	for (side = 0; side < 2; side++)
		for (type = 0; type < NUMBER_OF_VARIANT_VEHICLES; type++)
			options.vehicle_counts[side][type] = MAXIMUM_VARIANT_VEHICLE_COUNT;
	options.time_limit = 45;
	options.friendly_fire = _friendly_fire_explosives_only;
	options.friendly_fire_penalty = 15;
	options.vehicle_respawn_time = 300;
	options.auto_team_balance = TRUE;
	options.radar_players = _radar_players_friends;
	options.loadout = _loadout_custom;
	options.primary_weapon = _loadout_weapon_needler;
	options.secondary_weapon = _loadout_weapon_none;
	options.no_map_weapons = TRUE;
	fuzz_check("OpenCE's menus' furthest values", game_variant_options_valid(&options));

	/* a record of them on the crossplay wire: taken as it is */
	memset(&game, 0, sizeof(game));
	game.variant_options = options;
	game.cooperative_flags = FLAG(_network_game_cooperative_no_player_collisions_bit);
	fuzz_check("encode crossplay", network_game_record_encode(_network_game_record_crossplay, &game, record, sizeof(record)));
	fuzz_check("decode crossplay", network_game_record_decode(_network_game_record_crossplay, record,
		network_game_record_size(_network_game_record_crossplay), &decoded, NULL) &&
		!memcmp(&decoded, &game, sizeof(game)));
	if (folder)
	{
		byte seed[HALO_PORT_NETWORK_GAME_SIZE + 1];

		seed[0] = 0;
		memcpy(seed + 1, record, sizeof(game));
		net_fuzz_write_seed(folder, "crossplay_record", seed, sizeof(game) + 1);
		seed[0] = 3;
		memcpy(seed + 1, &options, sizeof(options));
		net_fuzz_write_seed(folder, "options", seed, sizeof(options) + 1);
	}

	/* ... on the legacy wire: 13,092 bytes, no options, no switches */
	fuzz_check("legacy size", network_game_record_size(_network_game_record_legacy) == HALO_PORT_NETWORK_GAME_LEGACY_SIZE &&
		HALO_PORT_NETWORK_GAME_LEGACY_SIZE == 13092 && HALO_PORT_NETWORK_GAME_SIZE == 13120);
	memset(record, 0x77, sizeof(record));
	fuzz_check("encode legacy", network_game_record_encode(_network_game_record_legacy, &game, record, sizeof(record)));
	fuzz_check("legacy padding byte", record[offsetof(struct network_game, cooperative_flags)] == 0);
	fuzz_check("legacy record ends where the options were", record[HALO_PORT_NETWORK_GAME_LEGACY_SIZE] == 0x77);
	fuzz_check("decode legacy", network_game_record_decode(_network_game_record_legacy, record,
		HALO_PORT_NETWORK_GAME_LEGACY_SIZE, &decoded, NULL) && decoded.cooperative_flags == 0 &&
		decoded.variant_options.vehicle_set[0] == decoded.variant.universal_variant.vehicle_set);
	if (folder)
	{
		byte seed[HALO_PORT_NETWORK_GAME_SIZE + 1];

		seed[0] = 1;
		memcpy(seed + 1, record, HALO_PORT_NETWORK_GAME_LEGACY_SIZE);
		net_fuzz_write_seed(folder, "legacy_record", seed, HALO_PORT_NETWORK_GAME_LEGACY_SIZE + 1);
	}

	/* one past each range: refused */
	{
		struct game_variant_options bad;

#define REFUSED(statement) do { bad = options; statement; fuzz_check("refused: " #statement, !game_variant_options_valid(&bad)); } while (0)
		REFUSED(bad.time_limit = -1);
		REFUSED(bad.friendly_fire = NUMBER_OF_FRIENDLY_FIRE_MODES);
		REFUSED(bad.friendly_fire_penalty = SHORT_MAX / TICKS_PER_SECOND + 1);
		REFUSED(bad.vehicle_respawn_time = -1);
		REFUSED(bad.auto_team_balance = 2);
		REFUSED(bad.radar_players = NUMBER_OF_RADAR_PLAYERS);
		REFUSED(bad.vehicle_set[0] = 2 + NUMBER_OF_VARIANT_VEHICLES);
		REFUSED(bad.vehicle_set[1] = VARIANT_VEHICLE_SET_PC - 1);
		REFUSED(bad.vehicle_counts[0][0] = MAXIMUM_VARIANT_VEHICLE_COUNT + 1);
		REFUSED(bad.loadout = NUMBER_OF_LOADOUTS);
		REFUSED(bad.primary_weapon = NUMBER_OF_LOADOUT_WEAPONS);
		REFUSED(bad.secondary_weapon = NUMBER_OF_LOADOUT_WEAPONS);
		REFUSED(bad.no_map_weapons = 2);
#undef REFUSED
	}
	fuzz_check("unknown co-op switch refused", !network_game_cooperative_flags_valid(0x02));

	__builtin_printf("%s variant options: the record's two wires, defaults and ranges\n",
		fuzz_failures ? "FAIL" : "PASS");
	return fuzz_failures;
}
