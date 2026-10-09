/*
OBJECT_BOUNDS_CACHE.H

(port) The objects' bounding spheres packed by absolute index, for the
collision queries' object walks: object_bounds_cache.c
*/

#ifndef __OBJECT_BOUNDS_CACHE_H
#define __OBJECT_BOUNDS_CACHE_H
#pragma once

#include "render_epoch.h"

/* ---------- constants */

enum
{
	/* the absolute indices covered (the retail limit of objects; a map with
	more has its later ones read as before) */
	OBJECT_BOUNDS_CACHE_ENTRIES = 2048,
};

/* ---------- structures */

struct object_bounds
{
	long object_index;
	unsigned long generation;
	real_point3d center;
	real radius;
};

/* ---------- globals */

extern struct object_bounds object_bounds_cache[OBJECT_BOUNDS_CACHE_ENTRIES];

/* ---------- prototypes */

/* object_compute_node_matrices, as it sets the object's bounding sphere */
void object_bounds_cache_update(long object_index, real_point3d const *center, real radius);
/* an object made in the datum (objects.c) */
void object_bounds_cache_forget(long object_index);

/* ---------- inline code */

/* whether the walks may read the copy: on the tick's thread (or the only one) */
static __inline__ boolean object_bounds_cache_usable(
	void)
{
	return !halo_epoch_threaded || halo_epoch_on_mutator_inline();
}

/* the object's bounding sphere as last computed, or NULL when it is not known */
static __inline__ struct object_bounds const *object_bounds_cache_get(
	long object_index)
{
	unsigned long absolute_index = (unsigned long)(object_index & 0xFFFF);
	struct object_bounds const *bounds;

	if (absolute_index >= OBJECT_BOUNDS_CACHE_ENTRIES)
	{
		return NULL;
	}
	bounds = &object_bounds_cache[absolute_index];
	if (bounds->object_index != object_index || bounds->generation != halo_map_generation)
	{
		return NULL;
	}

	return bounds;
}

#endif
