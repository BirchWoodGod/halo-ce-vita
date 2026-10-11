/*
CLUSTER_OBJECT_TYPES.H

(port) A cluster's collideable objects with each one's type, for the
collision tests' object walks: cluster_object_types.c
*/

#ifndef __CLUSTER_OBJECT_TYPES_H
#define __CLUSTER_OBJECT_TYPES_H
#pragma once

/* ---------- constants */

enum
{
	/* the tables kept (by cluster index) */
	CLUSTER_OBJECT_TYPE_TABLES = 32,
	/* the most objects of a table (a longer list is walked as before) */
	CLUSTER_OBJECT_TYPE_TABLE_LENGTH = 256,
	/* in a type: the object is a child (_object_header_child_bit) */
	CLUSTER_OBJECT_TYPE_CHILD = 0x80
};

/* ---------- structures */

struct cluster_object_type_table
{
	long count;
	short cluster_index;
	unsigned long changes;
	unsigned long map_generation;
	long object_indices[CLUSTER_OBJECT_TYPE_TABLE_LENGTH];
	/* each object's header type, CLUSTER_OBJECT_TYPE_CHILD added for a child */
	byte types[CLUSTER_OBJECT_TYPE_TABLE_LENGTH];
};

/* ---------- prototypes */

/* the cluster's collideable objects (cluster_get_collideable_objects) and
their types, as the walk would read them from their headers now; NULL (walk
as before) off the tick's thread, for a list longer than a table, or while
another walk holds a table. A table returned is held until
cluster_object_types_release */
struct cluster_object_type_table const *cluster_object_types_get(short cluster_index);
void cluster_object_types_release(void);

#endif
