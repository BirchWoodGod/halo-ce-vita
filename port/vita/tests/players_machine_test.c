/*
PLAYERS_MACHINE_TEST.C

The players a machine has (source/game/players.c's machine_to_player_table,
player_new): a network client makes the players its host's messages name,
and a host need not keep to what a machine can have. A machine with all
four of its entries taken, or one outside the table, gets no more players:
player_new refuses (NONE, nothing made) instead of the Xbox game's fatal
"failed to create a player", which ended the process (crossplay security
pass, docs/crossplay.md T9). Built as the game's code is (its flags, from
build.ninja; DEBUG: its assertions fatal, here an abort) with ASan and
UBSan by run_players_machine_test.sh.
*/

#include "../../../source/game/players.c"
#include "net_fuzz_game.h"

#include <stdio.h>

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

/* ---------- the test */

static int failures;

static void check(int passed, char const *what)
{
	__builtin_printf("%s %s\n", passed ? "PASS" : "FAIL", what);
	if (!passed)
		failures++;
}

static long count_players(void)
{
	struct data_iterator iterator;
	long count = 0;

	data_iterator_new(&iterator, player_data);
	while (data_iterator_next(&iterator))
		count++;
	return count;
}

int main(void)
{
	struct network_player network_player;
	long players[MAXIMUM_LOCAL_PLAYERS];
	long index;
	long extra;

	player_data = data_new("players", NETWORK_GAME_MAXIMUM_PLAYER_COUNT, sizeof(struct player_datum));
	data_make_valid(player_data);
	memset(machine_to_player_table, 0xFF, sizeof(machine_to_player_table));
	memset(&network_player, 0, sizeof(network_player));
	network_player.machine_index = 7;

	for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
	{
		network_player.controller_index = (char)index;
		players[index] = player_new(7, NONE, NONE, &network_player);
	}
	check(players[0] != NONE && players[1] != NONE && players[2] != NONE && players[3] != NONE,
		"a machine's four players are made");
	extra = player_new(7, NONE, NONE, &network_player);
	check(extra == NONE && count_players() == MAXIMUM_LOCAL_PLAYERS,
		"a fifth player of that machine is refused, nothing made, the process goes on");
	extra = player_new(7, ((long)player_data->next_identifier << 16) | 40, NONE, &network_player);
	check(extra == NONE && count_players() == MAXIMUM_LOCAL_PLAYERS,
		"... and one asked for at a slot of its own");
	extra = player_new(MAXIMUM_NETWORK_MACHINE_COUNT, NONE, NONE, &network_player);
	check(extra == NONE && count_players() == MAXIMUM_LOCAL_PLAYERS, "a player of a machine past the table is refused");
	extra = player_new(-2, NONE, NONE, &network_player);
	check(extra == NONE && count_players() == MAXIMUM_LOCAL_PLAYERS, "... and one of a negative machine");
	machine_remove_player(players[2]);
	extra = player_new(7, NONE, NONE, &network_player);
	check(extra != NONE && machine_get_player_list(7)[2] == extra,
		"a player that left gives its machine's entry to a new one");
	extra = player_new(8, NONE, NONE, &network_player);
	check(extra != NONE && machine_get_player_list(8)[0] == extra, "another machine's player is made");
	__builtin_printf("%s players' machines (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
