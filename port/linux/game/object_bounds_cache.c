/*
OBJECT_BOUNDS_CACHE.C

(port, after OpenCE: Addison Bair's "Pass over far objects on a packed copy of
their bounding spheres" and MrBruh's "Forget the object bounds cache in the
game state load procs")

The objects' bounding spheres packed by absolute index, so the collision
queries' object walks (collisions.c: collision_get_features_in_sphere's and
collision_test_vector's) pass over the far objects of a crowded cluster
without reading each one's datum: a fight's clusters hold a few hundred
objects, every biped and vehicle mass point asks for its features each tick,
every line of sight walks the clusters the line crosses, and each object read
is a cache miss and a write (its marker stamp).

Written by the spheres' only writer (object_compute_node_matrices), with the
same values; an entry counts only for its object (the datum index, salt and
all) and for the game state it was written in (halo_map_generation, bumped
whenever the game state is replaced: a revert, a loaded game, a new map), and
an object's entry is forgotten when it is made (objects.c). The walks run
their own test on the copy: the same objects are passed over and the same
features gathered, in order. Only the tick's thread reads it (or the one
thread, without a tick thread): another thread's walks read the objects as
before. Kept out of the game state, whose layout the saved games share.
*/

/* ---------- headers */

#include "cseries.h"
#include "math/real_math.h"
#include "object_bounds_cache.h"

/* ---------- globals */

struct object_bounds object_bounds_cache[OBJECT_BOUNDS_CACHE_ENTRIES];

/* ---------- public code */

void object_bounds_cache_update(
	long object_index,
	real_point3d const *center,
	real radius)
{
	unsigned long absolute_index = (unsigned long)(object_index & 0xFFFF);

	if (absolute_index < OBJECT_BOUNDS_CACHE_ENTRIES)
	{
		struct object_bounds *bounds = &object_bounds_cache[absolute_index];

		bounds->object_index = object_index;
		bounds->generation = halo_map_generation;
		bounds->center = *center;
		bounds->radius = radius;
	}

	return;
}

void object_bounds_cache_forget(
	long object_index)
{
	unsigned long absolute_index = (unsigned long)(object_index & 0xFFFF);

	if (absolute_index < OBJECT_BOUNDS_CACHE_ENTRIES)
	{
		object_bounds_cache[absolute_index].object_index = NONE;
	}

	return;
}
