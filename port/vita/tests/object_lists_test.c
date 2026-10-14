/*
OBJECT_LISTS_TEST.C

The scripts' object lists (source/hs/object_lists.c): each (players),
(vehicle_riders ...) and the like makes one, and a tick's lists are freed
only after its scripts have run (object_list_gc). A heavily scripted map
takes them all: object_list_new then finds none (NONE), and object_list_add
on it, from hs_players and the other makers, halted the game ("object list
header index #65535 is unused or changed"; a release build's datum_get,
NULL). A list that cannot be made is an empty one. Built as the game's code
is (its flags, from build.ninja; DEBUG: its assertions fatal, here an
abort) with ASan and UBSan by run_object_lists_test.sh.
*/

#include "../../../source/hs/object_lists.c"
#include "net_fuzz_game.h"

#include <stdio.h>

/* ---------- what the code under test calls from the rest of the game */

void platform_log(const char *format, ...) { (void)format; }
void halo_epoch_datum_created(struct data_array *data, long index) { (void)data; (void)index; }
void halo_epoch_datum_ready(struct data_array *data, long index) { (void)data; (void)index; }
void halo_epoch_check_reader_mutation(const struct data_array *data, long index, const char *what)
{
	(void)data; (void)index; (void)what;
}
int halo_epoch_datum_delete(struct data_array *data, long index) { (void)data; (void)index; return 0; }
int halo_epoch_datum_hidden_from_get(const struct data_array *data, long index) { (void)data; (void)index; return 0; }
int halo_epoch_datum_hidden_from_caller(const struct data_array *data, long index) { (void)data; (void)index; return 0; }

int halo_linux_sprintf(char *buffer, const char *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vsnprintf(buffer, 256, format, arguments);
	va_end(arguments);
	return result;
}
int halo_local_limits_active(void) { return 0; }

struct data_array *game_state_data_new(const char *name, short maximum_count, short size)
{
	return data_new(name, maximum_count, size);
}

/* ---------- the test */

static int failures;

static void check(int passed, char const *what)
{
	__builtin_printf("%s %s\n", passed ? "PASS" : "FAIL", what);
	if (!passed)
		failures++;
}

/* hs_object_to_object_list's and hs_players' way: a list made, an object added */
static long list_of(long object_index)
{
	long list_index = object_list_new();

	object_list_add(list_index, object_index);
	return list_index;
}

int main(void)
{
	static long lists[MAXIMUM_OBJECT_LISTS_PER_MAP];
	long reference_index = NONE;
	long index;
	long extra;
	boolean all_made = TRUE;

	object_lists_initialize();
	object_lists_initialize_for_new_map();

	for (index = 0; index < MAXIMUM_OBJECT_LISTS_PER_MAP; index++)
	{
		lists[index] = list_of(0x10000 + index);
		if (lists[index] == NONE || object_list_count(lists[index]) != 1)
			all_made = FALSE;
	}
	check(all_made, "a map's lists are made, an object in each");

	extra = list_of(0x20000);
	check(extra == NONE, "a list past the last is none");
	check(object_list_count(extra) == 0 && object_list_get_first(extra, &reference_index) == NONE,
		"... an empty one to the scripts, and the game goes on");

	/* the tick's lists freed (none referenced by a global): lists again */
	object_list_add_reference(lists[0]);
	object_list_gc();
	extra = list_of(0x30000);
	check(extra != NONE && object_list_count(extra) == 1 &&
		object_list_get_first(extra, &reference_index) == 0x30000,
		"after the tick's lists are freed, a list is made again");
	check(object_list_count(lists[0]) == 1, "... and the one a global holds is kept");

	__builtin_printf("%s object lists (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
