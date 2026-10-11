/*
WIRE_CONTRACT_RECORD.C

The wire contract test's game settings record probe (tools/wire_contract.py).

Built against each tree's network_game_manager.h with the game's flags. The
script generates wire_contract_fill() for each tree (WIRE_CONTRACT_FILL_HEADER)
from the record's layout as clang lays it out: every field the same bytes
in every tree (a pattern seeded by the field's name), the gametype's PC
options valid values, co-op's switches 1 (the legacy tree's padding 0).

  another tree's (OpenCE's, the legacy Vita wire's tree): prints the record
  as that tree sends it, its bytes as they are in memory, in hex.

  this tree's (WIRE_CONTRACT_OURS, with port/linux/game/network_game_record.c):
  wire_contract_record OPENCE_HEX LEGACY_HEX checks, a line each
  ("CHECK <name> PASS|FAIL <detail>"):
    crossplay_encode  the record as the crossplay wire sends it is OpenCE's
    legacy_encode     the record as the legacy wire sends it is the betas'
    crossplay_decode  OpenCE's record taken, field for field, and sent again
                      the same
    legacy_decode     the betas' record taken (no co-op switches, the
                      variant's default PC options) and sent again the same
    refuse_*          OpenCE's record with one value out of range refused
*/

#include "cseries.h"
#include "game/game_engine.h"
#include "networking/network_game_manager.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* a field's bytes: the same pattern in every tree, from its name */
static void wire_contract_pattern(void *field, unsigned long size, unsigned seed)
{
	unsigned char *bytes = (unsigned char *)field;
	unsigned long index;

	for (index = 0; index < size; index++)
		bytes[index] = (unsigned char)(seed * 31u + index * 7u + (index >> 8));
}

#include WIRE_CONTRACT_FILL_HEADER

#ifndef WIRE_CONTRACT_OURS

int main(void)
{
	static struct network_game game;
	unsigned long index;

	wire_contract_fill(&game);
	for (index = 0; index < sizeof(game); index++)
		printf("%02x", ((unsigned char *)&game)[index]);
	printf("\n");
	return 0;
}

#else

#include "network_game_record.h"
#include "net_fuzz_game.h"

int config_boolean(const char *name)
{
	(void)name;
	return 0;
}

static long read_hex(char const *path, unsigned char *bytes, long maximum)
{
	FILE *file = fopen(path, "r");
	long count = 0;
	unsigned value;

	if (!file)
		return -1;
	while (count < maximum && fscanf(file, "%2x", &value) == 1)
		bytes[count++] = (unsigned char)value;
	fclose(file);
	return count;
}

static int failures = 0;

static void check(char const *name, int passed, char const *detail)
{
	printf("CHECK %s %s %s\n", name, passed ? "PASS" : "FAIL", detail ? detail : "");
	if (!passed)
		failures++;
}

static void first_difference(unsigned char const *a, unsigned char const *b, long size, char *text, unsigned long text_size)
{
	long index;

	for (index = 0; index < size; index++)
	{
		if (a[index] != b[index])
		{
			snprintf(text, text_size, "first difference at byte 0x%lX: %02x, expected %02x", index, a[index], b[index]);
			return;
		}
	}
	snprintf(text, text_size, "same");
}

/* OpenCE's record with one change, which decoding must refuse */
#define REFUSE(name, statement) \
	do \
	{ \
		struct network_game *g = &mutated; \
		char const *why = NULL; \
		memcpy(&mutated, opence, sizeof(mutated)); \
		statement; \
		memcpy(buffer, &mutated, sizeof(mutated)); \
		check("refuse_" name, !network_game_record_decode(_network_game_record_crossplay, buffer, \
			sizeof(mutated), &decoded, &why), why); \
	} while (0)

int main(int argc, char **argv)
{
	static struct network_game game, decoded, mutated;
	static unsigned char opence[HALO_PORT_NETWORK_GAME_SIZE + 16];
	static unsigned char legacy[HALO_PORT_NETWORK_GAME_SIZE + 16];
	static unsigned char buffer[HALO_PORT_NETWORK_GAME_SIZE + 16];
	long opence_size, legacy_size;
	char text[160];
	char const *why;

	if (argc < 3)
	{
		fprintf(stderr, "usage: %s OPENCE_HEX LEGACY_HEX\n", argv[0]);
		return 2;
	}
	opence_size = read_hex(argv[1], opence, sizeof(opence));
	legacy_size = read_hex(argv[2], legacy, sizeof(legacy));
	wire_contract_fill(&game);
	printf("record sizes: ours %ld (crossplay %ld, legacy %ld), OpenCE %ld, legacy tree %ld\n",
		(long)sizeof(game), network_game_record_size(_network_game_record_crossplay),
		network_game_record_size(_network_game_record_legacy), opence_size, legacy_size);

	/* the crossplay wire: OpenCE's bytes */
	memset(buffer, 0xEE, sizeof(buffer));
	if (network_game_record_encode(_network_game_record_crossplay, &game, buffer, sizeof(buffer)) &&
		opence_size == network_game_record_size(_network_game_record_crossplay))
	{
		first_difference(buffer, opence, opence_size, text, sizeof(text));
		check("crossplay_encode", !memcmp(buffer, opence, (size_t)opence_size), text);
	}
	else
	{
		snprintf(text, sizeof(text), "sizes differ: ours %ld, OpenCE's %ld",
			network_game_record_size(_network_game_record_crossplay), opence_size);
		check("crossplay_encode", 0, text);
	}

	/* the legacy wire: the betas' bytes */
	memset(buffer, 0xEE, sizeof(buffer));
	if (network_game_record_encode(_network_game_record_legacy, &game, buffer, sizeof(buffer)) &&
		legacy_size == network_game_record_size(_network_game_record_legacy))
	{
		first_difference(buffer, legacy, legacy_size, text, sizeof(text));
		check("legacy_encode", !memcmp(buffer, legacy, (size_t)legacy_size), text);
	}
	else
	{
		snprintf(text, sizeof(text), "sizes differ: ours %ld, the legacy tree's %ld",
			network_game_record_size(_network_game_record_legacy), legacy_size);
		check("legacy_encode", 0, text);
	}

	/* OpenCE's record taken as it is */
	memset(&decoded, 0xEE, sizeof(decoded));
	why = NULL;
	if (opence_size == (long)sizeof(decoded) &&
		network_game_record_decode(_network_game_record_crossplay, opence, opence_size, &decoded, &why))
	{
		memset(buffer, 0xEE, sizeof(buffer));
		network_game_record_encode(_network_game_record_crossplay, &decoded, buffer, sizeof(buffer));
		first_difference(buffer, opence, opence_size, text, sizeof(text));
		check("crossplay_decode", !memcmp(&decoded, opence, sizeof(decoded)) && !memcmp(buffer, opence, (size_t)opence_size),
			text);
	}
	else
	{
		check("crossplay_decode", 0, why ? why : "not decoded");
	}

	/* the betas' record */
	memset(&decoded, 0xEE, sizeof(decoded));
	why = NULL;
	if (network_game_record_decode(_network_game_record_legacy, legacy, legacy_size, &decoded, &why))
	{
		struct game_variant_options defaults;

		game_variant_options_default(&decoded.variant, &defaults);
		memset(buffer, 0xEE, sizeof(buffer));
		network_game_record_encode(_network_game_record_legacy, &decoded, buffer, sizeof(buffer));
		first_difference(buffer, legacy, legacy_size, text, sizeof(text));
		check("legacy_decode", !memcmp(buffer, legacy, (size_t)legacy_size) && decoded.cooperative_flags == 0 &&
			!memcmp(&decoded.variant_options, &defaults, sizeof(defaults)), text);
	}
	else
	{
		check("legacy_decode", 0, why ? why : "not decoded");
	}

	/* a record of another size on either wire */
	check("refuse_size_crossplay", !network_game_record_decode(_network_game_record_crossplay, legacy,
		network_game_record_size(_network_game_record_legacy), &decoded, NULL), NULL);
	check("refuse_size_legacy", !network_game_record_decode(_network_game_record_legacy, opence,
		network_game_record_size(_network_game_record_crossplay), &decoded, NULL), NULL);

	/* every PC option, and co-op's switches, out of range */
	if (opence_size == (long)sizeof(mutated))
	{
		REFUSE("time_limit", g->variant_options.time_limit = -1);
		REFUSE("friendly_fire", g->variant_options.friendly_fire = NUMBER_OF_FRIENDLY_FIRE_MODES);
		REFUSE("friendly_fire_negative", g->variant_options.friendly_fire = -1);
		REFUSE("friendly_fire_penalty", g->variant_options.friendly_fire_penalty = 2000);
		REFUSE("vehicle_respawn_time", g->variant_options.vehicle_respawn_time = -30);
		REFUSE("auto_team_balance", g->variant_options.auto_team_balance = 2);
		REFUSE("radar_players", g->variant_options.radar_players = NUMBER_OF_RADAR_PLAYERS);
		REFUSE("vehicle_set_red", g->variant_options.vehicle_set[0] = 8);
		REFUSE("vehicle_set_blue", g->variant_options.vehicle_set[1] = 0xFD);
		REFUSE("vehicle_counts", g->variant_options.vehicle_counts[1][NUMBER_OF_VARIANT_VEHICLES - 1] =
			MAXIMUM_VARIANT_VEHICLE_COUNT + 1);
		REFUSE("loadout", g->variant_options.loadout = NUMBER_OF_LOADOUTS);
		REFUSE("primary_weapon", g->variant_options.primary_weapon = NUMBER_OF_LOADOUT_WEAPONS);
		REFUSE("secondary_weapon", g->variant_options.secondary_weapon = 0xFF);
		REFUSE("no_map_weapons", g->variant_options.no_map_weapons = 0x80);
		REFUSE("cooperative_flags", g->cooperative_flags = 0x02);
	}

	printf("%s record checks\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}

#endif
