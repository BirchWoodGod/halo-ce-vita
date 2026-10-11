/*
NET_FUZZ_ACTOR_TABLE.C

A fuzz target for a client's table of the host's AI units (port/linux/game/
network_actor_table.c), which a PC co-op host's actor states fill on the
crossplay wire (docs/crossplay.md 6.2): every unit index in an actor state
is a host's 32-bit value, and the table finds an entry by its low 16 bits.

An input is a list of operations of 5 bytes each: the operation (its low
2 bits: find, add, remove, add with a limit; the next bits the limit or a
choice of index), then a unit index (4 bytes, little-endian) that is also
bent toward the interesting ones (NONE, past the map's objects, an index
held with another salt). Each operation is run on the table and on a plain
list (the model: entries in order, found by walking them), and the two
must agree: the same entry found or made, the same previous unit, the same
entry moved by a remove, never more entries than the limit or capacity,
never an entry past the map's objects, and every entry's slot naming it.
Built as net_fuzz_crossplay_maps.c is, by run_net_fuzz_test.sh.
*/

#include "../../linux/game/network_actor_table.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct network_actor_table table;
static long model[NETWORK_ACTOR_TABLE_CAPACITY];
static short model_count;

static long model_absolute(
	long unit_index)
{
	long absolute_index = (long)((unsigned long)unit_index & 0xFFFFUL);

	return absolute_index < NETWORK_ACTOR_TABLE_OBJECTS ? absolute_index : -1;
}

static short model_find_absolute(
	long absolute_index)
{
	short index;

	for (index = 0; index < model_count; index++)
	{
		if (model_absolute(model[index]) == absolute_index)
			return index;
	}
	return -1;
}

static void check_invariants(
	void)
{
	short index;

	if (table.count != model_count || table.count < 0 || table.count > NETWORK_ACTOR_TABLE_CAPACITY)
		abort();
	for (index = 0; index < table.count; index++)
	{
		long absolute_index = model_absolute(table.unit_indices[index]);

		if (table.unit_indices[index] != model[index] || absolute_index < 0 ||
			table.slots[absolute_index] != index + 1 || network_actor_table_find(&table, model[index]) != index)
		{
			abort();
		}
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	size_t at;

	network_actor_table_clear(&table);
	model_count = 0;
	for (at = 0; at + 5 <= size; at += 5)
	{
		uint8_t operation = data[at];
		long unit_index = (long)((unsigned long)data[at + 1] | (unsigned long)data[at + 2] << 8 |
			(unsigned long)data[at + 3] << 16 | (unsigned long)data[at + 4] << 24);
		short found, index, limit;
		long previous;

		/* (bent toward the interesting indices) */
		switch ((operation >> 2) & 7)
		{
		case 0: unit_index = -1; break;
		case 1: unit_index = (long)((unsigned long)unit_index & 0x1FFFUL) | (long)((unsigned long)unit_index & 0x00FF0000UL); break;
		case 2: if (model_count) unit_index = model[(unsigned long)unit_index % (unsigned long)model_count] ^ 0x10000L; break;
		case 3: if (model_count) unit_index = model[(unsigned long)unit_index % (unsigned long)model_count]; break;
		default: break;
		}
		switch (operation & 3)
		{
		case 0:
			found = network_actor_table_find(&table, unit_index);
			index = model_absolute(unit_index) < 0 ? -1 : model_find_absolute(model_absolute(unit_index));
			if (index >= 0 && model[index] != unit_index)
				index = -1;
			if (found != index)
				abort();
			break;
		case 1:
		case 3:
			limit = (operation & 3) == 1 ? NETWORK_ACTOR_TABLE_CAPACITY + 7 : (short)(operation >> 5) * 37 - 3;
			found = network_actor_table_add(&table, unit_index, limit, &previous);
			if (model_absolute(unit_index) < 0)
			{
				if (found != -1 || previous != -1)
					abort();
				break;
			}
			index = model_find_absolute(model_absolute(unit_index));
			if (index >= 0)
			{
				if (found != index || previous != model[index])
					abort();
				model[index] = unit_index;
			}
			else if (model_count >= (limit < NETWORK_ACTOR_TABLE_CAPACITY ? limit : NETWORK_ACTOR_TABLE_CAPACITY))
			{
				if (found != -1 || previous != -1)
					abort();
			}
			else
			{
				if (found != model_count || previous != -1)
					abort();
				model[model_count++] = unit_index;
			}
			break;
		case 2:
			/* (any index, the table's own and past it) */
			index = (short)(((unsigned long)unit_index & 0x7FF) - 8);
			found = network_actor_table_remove(&table, index);
			if (index < 0 || index >= model_count)
			{
				if (found != -1)
					abort();
				break;
			}
			model_count--;
			if (index == model_count)
			{
				if (found != -1)
					abort();
			}
			else
			{
				if (found != model_count)
					abort();
				model[index] = model[model_count];
			}
			break;
		}
		check_invariants();
	}
	return 0;
}

/* the checks run before the kept cases: a full table, every index of a map,
salts, NONE and indices past the objects */
int net_fuzz_checks(void)
{
	long previous;
	long unit_index;
	short index;
	int failures = 0;

	network_actor_table_clear(&table);
	for (index = 0; index < NETWORK_ACTOR_TABLE_CAPACITY; index++)
	{
		if (network_actor_table_add(&table, (long)index * 7 + 0x12340000L, 30000, &previous) != index || previous != -1)
			failures++;
	}
	if (network_actor_table_add(&table, 0x55550000L | 8000, 30000, &previous) != -1)
		failures++;
	/* (another salt at a held index: taken over, even when full) */
	if (network_actor_table_add(&table, 0x77770000L | 7, 30000, &previous) != 1 || previous != 0x12340000L + 7)
		failures++;
	if (network_actor_table_find(&table, 0x12340000L + 7) != -1 || network_actor_table_find(&table, 0x77770007L) != 1)
		failures++;
	for (unit_index = -3; unit_index < 3; unit_index++)
	{
		if (unit_index < 0 && (network_actor_table_add(&table, unit_index, 30000, &previous) != -1 ||
			network_actor_table_find(&table, unit_index) != -1))
		{
			failures++;
		}
	}
	if (network_actor_table_add(&table, 0x0000FFFFL, 30000, &previous) != -1 ||
		network_actor_table_add(&table, (long)NETWORK_ACTOR_TABLE_OBJECTS, 30000, &previous) != -1 ||
		network_actor_table_find(&table, 0x7FFFFFFFL) != -1 || network_actor_table_remove(&table, -1) != -1 ||
		network_actor_table_remove(&table, NETWORK_ACTOR_TABLE_CAPACITY) != -1)
	{
		failures++;
	}
	/* (remove the first: the last moves into it, found there) */
	if (network_actor_table_remove(&table, 0) != NETWORK_ACTOR_TABLE_CAPACITY - 1 ||
		network_actor_table_find(&table, (long)(NETWORK_ACTOR_TABLE_CAPACITY - 1) * 7 + 0x12340000L) != 0 ||
		network_actor_table_find(&table, 0x12340000L) != -1)
	{
		failures++;
	}
	/* (the legacy wire's limit) */
	network_actor_table_clear(&table);
	for (index = 0; index < 300; index++)
		network_actor_table_add(&table, index, 288, &previous);
	if (table.count != 288)
		failures++;
	printf("%s actor table checks (%d failed)\n", failures ? "FAIL" : "PASS", failures);
	return failures;
}
