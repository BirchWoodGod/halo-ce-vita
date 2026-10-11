/*
CLUSTER_OBJECT_TYPES.C

(port) A cluster's collideable objects with each one's type, for the
collision vector tests' object walks (collisions.c: collision_test_vector's).
A walk takes a cluster's objects and reads each one's header for its type and
whether it is a child before anything else: a line of sight (vehicles,
scenery and machines only) passes over the others by their type, and every
walk tests the rest's bounding spheres (object_bounds_cache.c) before
reading anything more. In b30's beach fight a line of sight walks ~65
objects (one or two clusters), most of them scenery passed over by their
spheres, each a header read where the walk then reads the datum of two or
three. (collision_get_features_in_sphere's walks, in objects_update where
the lists change with each object that moves, would find too few tables.)

A table is the cluster's list in its order with each object's type (and
CLUSTER_OBJECT_TYPE_CHILD for a child) read from the headers when it was
made, and is good while no list of any cluster partition has changed (a
connect, a disconnect, a partition made again or copied:
cluster_partition_changes) and the game state is the one it was made in
(halo_map_generation): an object's type is set when it is made (a new object
in a datum is another datum index, its list entries made anew), and it
becomes a child or stops being one only when object_reconnect_to_map puts it
back on the map, after it was taken off (cluster_partition_disconnect) - a
change of the lists either way. So a walk reads the same types and child
flags from a table as from the headers, and passes over the same objects in
the same order.

The tick's own (or the only thread's: the walks' use of the packed bounding
spheres, object_bounds_cache_usable); kept out of the game state.
HALO_AI_LINE_OF_SIGHT_VERIFY=1 makes each table found again a second time
from the list and the headers and compares (line_of_sight_verify.c).
*/

/* ---------- headers */

#include <string.h>

#include "cseries.h"
#include "render_epoch.h"
#include "objects/objects.h"
#include "structures/cluster_partitions.h"
#include "cluster_object_types.h"
#include "object_bounds_cache.h"
#include "line_of_sight_verify.h"

/* ---------- globals */

static struct cluster_object_type_table cluster_object_type_tables[CLUSTER_OBJECT_TYPE_TABLES];
/* a walk holds a table (another walk inside it walks as before) */
static boolean cluster_object_types_held;
/* (HALO_TICK_PROFILE: lines_profile.c) tables found and made */
unsigned long cluster_object_types_found, cluster_object_types_made;

/* ---------- private code */

/* the cluster's list and its types, read now; FALSE: none (as before) */
static boolean cluster_object_types_read(
	short cluster_index,
	struct cluster_object_type_table *table)
{
	long count = cluster_get_collideable_objects(
		cluster_index,
		table->object_indices,
		CLUSTER_OBJECT_TYPE_TABLE_LENGTH);
	long index;

	if (count == NONE)
	{
		return FALSE;
	}
	for (index = 0; index < count; index++)
	{
		struct object_header_datum const *header = object_header_get(table->object_indices[index]);

		table->types[index] = (byte)(header->type |
			(TEST_FLAG(header->flags, _object_header_child_bit) ? CLUSTER_OBJECT_TYPE_CHILD : 0));
	}
	table->count = count;

	return TRUE;
}

/* ---------- public code */

struct cluster_object_type_table const *cluster_object_types_get(
	short cluster_index)
{
	struct cluster_object_type_table *table;
	unsigned long changes;

	if (cluster_object_types_held || cluster_index < 0 || !object_bounds_cache_usable())
	{
		return NULL;
	}
	table = &cluster_object_type_tables[cluster_index % CLUSTER_OBJECT_TYPE_TABLES];
	changes = cluster_partition_changes();
	if (table->cluster_index == cluster_index &&
		table->changes == changes &&
		table->map_generation == halo_map_generation)
	{
		if (table->count == NONE)
		{
			return NULL;
		}
		cluster_object_types_found++;
		if (line_of_sight_verify_enabled())
		{
			static struct cluster_object_type_table fresh;
			boolean same = cluster_object_types_read(cluster_index, &fresh) &&
				fresh.count == table->count &&
				!memcmp(fresh.object_indices, table->object_indices, table->count * sizeof(long)) &&
				!memcmp(fresh.types, table->types, table->count);

			line_of_sight_verify_result(_line_of_sight_verify_cluster_object_types, same, same ? NULL : "a cluster's objects or their types");
		}
	}
	else
	{
		cluster_object_types_made++;
		table->cluster_index = cluster_index;
		table->changes = changes;
		table->map_generation = halo_map_generation;
		if (!cluster_object_types_read(cluster_index, table))
		{
			table->count = NONE;
			return NULL;
		}
	}
	cluster_object_types_held = TRUE;

	return table;
}

void cluster_object_types_release(
	void)
{
	cluster_object_types_held = FALSE;
}
