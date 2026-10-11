/*
NETWORK_ACTOR_TABLE.C

A client's table of the host's AI units (network_actor_table.h).
*/

#include "network_actor_table.h"

#include <string.h>

/* the unit's absolute object index, or -1 past the objects a map holds */
static long absolute_index_of(
	long unit_index)
{
	long absolute_index = (long)((unsigned long)unit_index & 0xFFFFUL);

	return absolute_index < NETWORK_ACTOR_TABLE_OBJECTS ? absolute_index : -1;
}

/* the entry the slot map names for an absolute index (its unit's index is
there, whatever its salt), or -1 */
static short slot_entry(
	struct network_actor_table const *table,
	long absolute_index)
{
	short slot = table->slots[absolute_index];

	if (slot <= 0 || slot > table->count || absolute_index_of(table->unit_indices[slot - 1]) != absolute_index)
		return -1;
	return (short)(slot - 1);
}

void network_actor_table_clear(
	struct network_actor_table *table)
{
	table->count = 0;
	memset(table->slots, 0, sizeof(table->slots));
}

short network_actor_table_find(
	struct network_actor_table const *table,
	long unit_index)
{
	long absolute_index = absolute_index_of(unit_index);
	short index;

	if (absolute_index < 0)
		return -1;
	index = slot_entry(table, absolute_index);
	return index >= 0 && table->unit_indices[index] == unit_index ? index : -1;
}

short network_actor_table_add(
	struct network_actor_table *table,
	long unit_index,
	short limit,
	long *previous)
{
	long absolute_index = absolute_index_of(unit_index);
	short index;

	*previous = -1;
	if (absolute_index < 0)
		return -1;
	index = slot_entry(table, absolute_index);
	if (index >= 0)
	{
		/* (the unit's own, or that of the object this index held before,
		gone: taken over) */
		*previous = table->unit_indices[index];
		table->unit_indices[index] = unit_index;
		return index;
	}
	if (limit > NETWORK_ACTOR_TABLE_CAPACITY)
		limit = NETWORK_ACTOR_TABLE_CAPACITY;
	if (table->count >= limit)
		return -1;
	index = table->count++;
	table->unit_indices[index] = unit_index;
	table->slots[absolute_index] = (short)(index + 1);
	return index;
}

short network_actor_table_remove(
	struct network_actor_table *table,
	short index)
{
	long absolute_index;
	short last;

	if (index < 0 || index >= table->count)
		return -1;
	absolute_index = absolute_index_of(table->unit_indices[index]);
	if (absolute_index >= 0 && table->slots[absolute_index] == index + 1)
		table->slots[absolute_index] = 0;
	last = --table->count;
	if (index == last)
		return -1;
	table->unit_indices[index] = table->unit_indices[last];
	absolute_index = absolute_index_of(table->unit_indices[index]);
	if (absolute_index >= 0)
		table->slots[absolute_index] = (short)(index + 1);
	return last;
}
