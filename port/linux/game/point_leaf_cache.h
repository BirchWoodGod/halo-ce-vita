/*
POINT_LEAF_CACHE.H

(port) The cells of the structure's bsp the particles and contrail points
were last found in, for their point physics' collision tests:
point_leaf_cache.c
*/

#ifndef __POINT_LEAF_CACHE_H
#define __POINT_LEAF_CACHE_H
#pragma once

#include "render_epoch.h"

/* ---------- constants */

/* whose cell (point_leaf_cache.c): the key is the kind (<< 16) and the
datum's absolute index; NONE none */
enum
{
	_point_leaf_particle,
	_point_leaf_contrail_point,
	NUMBER_OF_POINT_LEAF_KINDS
};

enum
{
	/* the absolute indices with a cell (the Xbox's pools of 1024 and as
	many again; later ones walk) */
	POINT_LEAF_PARTICLE_CELLS = 2048,
	POINT_LEAF_CONTRAIL_POINT_CELLS = 1024,
};

#define POINT_LEAF_KEY(kind, datum_index) (((long)(kind) << 16) | ((datum_index) & 0xFFFF))

/* ---------- structures */

/* (32 bytes: a particle's cell in one cache line) */
struct point_leaf_cell
{
	/* the walked point */
	real_point3d center;
	/* the least distance to the other planes, less the center's error; 0 or
	less: nothing known */
	real reach;
	long leaf_index;
	/* the nearest planes' indices, complemented (~) for their back sides */
	long signed_plane_indices[3];
};

/* ---------- globals */

/* bumped whenever the structure's bsp changes (scenario.c: global_bsp3d set) */
extern unsigned long halo_structure_bsp_generation;

/* (HALO_TICK_PROFILE: lines_profile.c) */
extern unsigned long point_leaf_cache_points, point_leaf_cache_point_hits;

/* ---------- prototypes */

/* bsp3d_test_point(global_bsp3d_get(), 0, point), answered from the key's
cell when the point is well inside it (a walk otherwise, which the key's cell
then remembers) */
long point_leaf_cache_leaf_from_point(long key, real_point3d const *point);

/* bsp3d_test_point(global_bsp3d_get(), 0, point), its cell kept in the seed
for a datum yet to be made (point_leaf_cache_seed_store gives it the cell) */
long point_leaf_cache_seed(real_point3d const *point, struct point_leaf_cell *seed);
void point_leaf_cache_seed_store(long key, struct point_leaf_cell const *seed);

/* scenario_location_from_point, its leaf from point_leaf_cache_leaf_from_point
and point_leaf_cache_seed (scenario.c) */
struct location;
void scenario_location_from_point_keyed(struct location *location, real_point3d const *point, long key);
void scenario_location_from_point_seed(struct location *location, real_point3d const *point,
	struct point_leaf_cell *seed);

/* ---------- inline code */

/* on the tick's thread (or the only one): other threads walk as before */
static __inline__ boolean point_leaf_cache_usable(
	void)
{
	return !halo_epoch_threaded || halo_epoch_on_mutator_inline();
}

#endif
