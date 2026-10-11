/*
NETWORK_ACTOR_TABLE.H

A client's table of the host's AI units it drives (network_actors.c), kept
apart so that its fuzz target builds it alone (port/vita/tests/
net_fuzz_actor_table.c). Each entry is a unit's object index, from the host:
any 32-bit value. Entries are found by the object's absolute index (its low
16 bits) through a slot map, at once among the thousand a PC co-op host
drives on the crossplay wire (OpenCE's 1024 actors and 32, its network
version 13; docs/crossplay.md 6.2). An index past the objects a map holds
is never an entry. The caller keeps each entry's own data in an array of
its own, by the same entry index, and moves it as remove says.
*/

#ifndef __NETWORK_ACTOR_TABLE_H
#define __NETWORK_ACTOR_TABLE_H

/* ---------- constants */

/* the most entries a table holds (a crossplay client's: OpenCE's actors
and the units its cutscenes drive), and the objects a map holds, whose
absolute indices the slot map covers (halo_port_capacity.h's
HALO_PORT_MAXIMUM_OBJECTS_PER_MAP) */
#define NETWORK_ACTOR_TABLE_CAPACITY (1024 + 32)
#define NETWORK_ACTOR_TABLE_OBJECTS 8192

/* ---------- structures */

struct network_actor_table
{
	/* the entries' units (their object indices) */
	long unit_indices[NETWORK_ACTOR_TABLE_CAPACITY];
	short count;
	/* by absolute object index: its entry plus one, 0 for none */
	short slots[NETWORK_ACTOR_TABLE_OBJECTS];
};

/* ---------- prototypes/NETWORK_ACTOR_TABLE.C */

/* no entries */
void network_actor_table_clear(
	struct network_actor_table *table);

/* the unit's entry, or -1 */
short network_actor_table_find(
	struct network_actor_table const *table,
	long unit_index);

/* the unit's entry, made if it has none: -1 when its index is past the
objects a map holds, or when the table already has limit entries (limit
pinned to the capacity). A new entry's previous unit is in *previous: the
unit whose entry it was (an object gone from that index, its entry taken
over), else -1 (a new entry at the end); a found one's is the unit itself */
short network_actor_table_add(
	struct network_actor_table *table,
	long unit_index,
	short limit,
	long *previous);

/* the entry removed: the last entry takes its place, and its index is
returned (the caller moves that entry's data to index), or -1 when the
removed entry was the last (or the index none) */
short network_actor_table_remove(
	struct network_actor_table *table,
	short index);

#endif /* __NETWORK_ACTOR_TABLE_H */
