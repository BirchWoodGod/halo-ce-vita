/*
STALE_PLAYER_TEST.C

A player who quit, whose slot another player takes (network_game_spawn_player,
source/networking/network_game_manager.c): the objects forget his handle
(network_game_player_forget), the units he played, the objects he owns (a
grenade still flying) and the damage he did to units. And damage that names
a handle no player has now (an older checkpoint's, or one that outlived the
forgetting) is neither recorded nor read (unit_record_damage,
source/units/units.c): the Xbox game's player_get on it ended the process
(a release build's datum_get, NULL), as a co-op player quit with a grenade
in the air that then hit an AI. Built as the game's code is (build.ninja's
flags for each file, DEBUG: its assertions fatal, here an abort) with ASan
and UBSan by run_stale_player_test.sh, linked with the real players.c,
units.c and data.c; the objects are a table of the test's own.
*/

#include "../../../source/networking/network_game_manager.c"
#include "net_fuzz_game.h"

#include <stdio.h>

/* (units.c's own, declared in no header) */
void unit_record_damage(long unit_index, real damage_amount, short damage_type, boolean notify_ai,
	long attacker_player_index, short attacker_team, long killing_object_index);

/* ---------- what the code under test calls from the rest of the game */

wchar_t *ustrncpy(wchar_t *destination, wchar_t const *source, unsigned long count)
{
	unsigned long index;

	for (index = 0; index < count && source[index]; index++)
		destination[index] = source[index];
	for (; index < count; index++)
		destination[index] = 0;
	return destination;
}
void platform_log(const char *format, ...) { (void)format; }
void halo_epoch_datum_created(struct data_array *data, long index) { (void)data; (void)index; }
void halo_epoch_check_reader_mutation(const struct data_array *data, long index, const char *what)
{
	(void)data; (void)index; (void)what;
}
int halo_epoch_datum_delete(struct data_array *data, long index) { (void)data; (void)index; return 0; }
int halo_epoch_datum_hidden_from_get(const struct data_array *data, long index) { (void)data; (void)index; return 0; }
int halo_epoch_datum_hidden_from_caller(const struct data_array *data, long index) { (void)data; (void)index; return 0; }

boolean network_game_player_is_local(struct network_player *player) { (void)player; return FALSE; }
long game_time_get(void) { return 100; }
boolean game_team_is_enemy(short team, short other_team) { return team != other_team; }

static int ai_killing_sprees;
static long ai_killing_spree_unit;

boolean ai_handle_killing_spree(long unit_index, short count)
{
	(void)count;
	ai_killing_sprees++;
	ai_killing_spree_unit = unit_index;
	return FALSE;
}

/* the objects: units (the quitter's, an AI) and a grenade, at absolute
indices with identifiers of their own */
enum
{
	_test_quitter_unit,
	_test_ai_unit,
	_test_grenade,
	_test_other_unit,
	NUMBER_OF_TEST_OBJECTS
};

static struct unit_datum test_objects[NUMBER_OF_TEST_OBJECTS];
static boolean test_object_exists[NUMBER_OF_TEST_OBJECTS];

#define TEST_OBJECT_INDEX(absolute_index) DATUM_INDEX_NEW(absolute_index, 0x1000 + (absolute_index))

static void *test_object_get(long object_index, unsigned long valid_type_flags)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

	if (object_index == NONE || absolute_index >= NUMBER_OF_TEST_OBJECTS || !test_object_exists[absolute_index] ||
		object_index != TEST_OBJECT_INDEX(absolute_index) ||
		!TEST_FLAG(valid_type_flags, test_objects[absolute_index].object.type))
	{
		return NULL;
	}
	return &test_objects[absolute_index];
}

void *object_try_and_get_and_verify_type(long object_index, unsigned long valid_type_flags)
{
	return test_object_get(object_index, valid_type_flags);
}

void *object_get_and_verify_type(long object_index, unsigned long valid_type_flags)
{
	void *object = test_object_get(object_index, valid_type_flags);

	if (!object)
	{
		__builtin_printf("FAIL object_get of %08lx, no such object\n", (unsigned long)object_index);
		__builtin_abort();
	}
	return object;
}

void object_iterator_new(struct object_iterator *iterator, unsigned long type_flags, byte flags)
{
	iterator->type_flags = type_flags;
	iterator->flags = flags;
	iterator->absolute_index = 0;
	iterator->index = NONE;
}

void *object_iterator_next(struct object_iterator *iterator)
{
	while (iterator->absolute_index < NUMBER_OF_TEST_OBJECTS)
	{
		short absolute_index = iterator->absolute_index++;

		if (test_object_exists[absolute_index] &&
			TEST_FLAG(iterator->type_flags, test_objects[absolute_index].object.type))
		{
			iterator->index = TEST_OBJECT_INDEX(absolute_index);
			return &test_objects[absolute_index];
		}
	}
	iterator->index = NONE;
	return NULL;
}

/* ---------- the test */

static int failures;

static void check(int passed, char const *what)
{
	__builtin_printf("%s %s\n", passed ? "PASS" : "FAIL", what);
	if (!passed)
		failures++;
}

static void test_object_new(short absolute_index, short type, short team, long owner_player_index)
{
	struct unit_datum *unit = &test_objects[absolute_index];
	short attacker_index;

	memset(unit, 0, sizeof(*unit));
	unit->object.type = type;
	unit->object.owner_team_index = team;
	unit->object.owner_player_index = owner_player_index;
	unit->unit.player_index = NONE;
	unit->unit.driver_object_index = NONE;
	unit->unit.gunner_object_index = NONE;
	unit->unit.killing_spree_last_time = NONE;
	for (attacker_index = 0; attacker_index < MAXIMUM_ATTACKERS_PER_UNIT; attacker_index++)
	{
		unit->unit.attackers[attacker_index].game_time_stamp = (unsigned long)NONE;
		unit->unit.attackers[attacker_index].object_index = NONE;
		unit->unit.attackers[attacker_index].player_index = NONE;
	}
	test_object_exists[absolute_index] = TRUE;
}

static boolean attackers_name(struct unit_datum const *unit, long player_index)
{
	short attacker_index;

	for (attacker_index = 0; attacker_index < MAXIMUM_ATTACKERS_PER_UNIT; attacker_index++)
	{
		if (unit->unit.attackers[attacker_index].game_time_stamp != (unsigned long)NONE &&
			unit->unit.attackers[attacker_index].player_index == player_index)
		{
			return TRUE;
		}
	}
	return FALSE;
}

int main(void)
{
	struct network_player quitter_network_player;
	struct network_player new_network_player;
	struct player_datum *quitter;
	long machine_index;
	long quitter_index;
	long new_player_index;
	enum { slot = 3, player_team = 0, ai_team = 3 };

	player_data = data_new("players", NETWORK_GAME_PLAYER_SLOTS, sizeof(struct player_datum));
	data_make_valid(player_data);
	for (machine_index = 0; machine_index < MAXIMUM_NETWORK_MACHINE_COUNT; machine_index++)
		memset(machine_get_player_list(machine_index), 0xFF, sizeof(long) * MAXIMUM_LOCAL_PLAYERS);

	/* a co-op player at his slot, who hit an AI, threw a grenade and quit;
	his unit is gone */
	memset(&quitter_network_player, 0, sizeof(quitter_network_player));
	quitter_network_player.machine_index = 1;
	quitter_network_player.player_list_index = slot;
	check(network_game_spawn_player(&quitter_network_player) && quitter_network_player.player_list_index == slot,
		"the first player is made at his slot");
	quitter_index = ((long)(word)((struct datum_header *)((byte *)player_data->data +
		player_data->size * slot))->identifier << 16) | slot;
	quitter = player_try_and_get(quitter_index);
	check(quitter != NULL, "... and found by his handle");
	if (!quitter)
		return 1;
	quitter->quit_out_of_game = TRUE;
	quitter->unit_index = NONE;
	test_object_new(_test_quitter_unit, _object_type_biped, player_team, NONE);
	test_objects[_test_quitter_unit].unit.player_index = quitter_index;
	test_object_new(_test_ai_unit, _object_type_biped, ai_team, NONE);
	test_objects[_test_ai_unit].unit.attackers[0].game_time_stamp = 50;
	test_objects[_test_ai_unit].unit.attackers[0].damage_inflicted = 0.25f;
	test_objects[_test_ai_unit].unit.attackers[0].player_index = quitter_index;
	test_objects[_test_ai_unit].unit.attackers[0].object_index = NONE;
	test_object_new(_test_grenade, _object_type_projectile, player_team, quitter_index);
	test_object_new(_test_other_unit, _object_type_vehicle, player_team, NONE);

	/* another machine's player takes the slot */
	memset(&new_network_player, 0, sizeof(new_network_player));
	new_network_player.machine_index = 2;
	new_network_player.player_list_index = slot;
	check(network_game_spawn_player(&new_network_player) && new_network_player.player_list_index == slot,
		"another machine's player takes the slot of the one who quit");
	new_player_index = ((long)(word)((struct datum_header *)((byte *)player_data->data +
		player_data->size * slot))->identifier << 16) | slot;
	check(new_player_index != quitter_index && !player_try_and_get(quitter_index) &&
		player_try_and_get(new_player_index), "... with a handle of his own, the old one no player's");
	check(test_objects[_test_grenade].object.owner_player_index == NONE,
		"the grenade the quitter threw forgets him (team kept)");
	check(test_objects[_test_grenade].object.owner_team_index == player_team, "... and keeps its team");
	check(test_objects[_test_quitter_unit].unit.player_index == NONE, "the unit he played forgets him");
	check(!attackers_name(&test_objects[_test_ai_unit], quitter_index) &&
		test_objects[_test_ai_unit].unit.attackers[0].damage_inflicted == 0.25f,
		"the AI he hit forgets him, the damage stays");

	/* the grenade lands on the AI with the damage it was thrown with: the
	stale handle (unit_record_damage's player_get, a crash) */
	ai_killing_sprees = 0;
	unit_record_damage(TEST_OBJECT_INDEX(_test_ai_unit), 1.0f, 0, TRUE, quitter_index, player_team,
		TEST_OBJECT_INDEX(_test_grenade));
	check(!attackers_name(&test_objects[_test_ai_unit], quitter_index) &&
		!attackers_name(&test_objects[_test_ai_unit], new_player_index),
		"a stale handle's damage is recorded with no player, not the new one's");
	check(ai_killing_sprees == 0, "... and no unit is credited (the grenade is no unit)");

	/* a handle at a slot nobody holds (a datum deleted, not reused) */
	player_delete(new_player_index);
	unit_record_damage(TEST_OBJECT_INDEX(_test_ai_unit), 1.0f, 0, TRUE, new_player_index, player_team,
		TEST_OBJECT_INDEX(_test_other_unit));
	check(!attackers_name(&test_objects[_test_ai_unit], new_player_index) && ai_killing_sprees == 1 &&
		ai_killing_spree_unit == TEST_OBJECT_INDEX(_test_other_unit),
		"a deleted player's handle: the damage goes to the object that did it");

	__builtin_printf("%s stale player handles (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
