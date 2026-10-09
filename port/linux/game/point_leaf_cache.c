/*
POINT_LEAF_CACHE.C

(port) Where a particle is in the structure's bsp, without walking the bsp
again when it has hardly moved.

The point physics (point_physics.c) moves each particle and contrail point
once a frame by a collision vector test, and one with neither the structure
nor water to hit (most of a fight's particles) only looks up the leaf its end
point is in: a walk of the collision bsp from its root (bsp3d_test_point,
some 15-30 planes deep at the beach). In b30's fight that is ~440 walks a
tick, ~0.29 M of the Vita's cycles a tick (callgrind's Cortex-A9 model).

A walk that ends in a leaf tested the point against each plane on its way,
and the leaf's cell is the space where every one of those tests comes out as
it did. The walk (bsp3d_test_point_nearest_planes) also keeps the three planes
it came nearest to (a particle near the ground slides along it) and the
fourth nearest distance, which no other plane on the way is nearer than: a
point on the same side of each of the three, and within that distance of the
walked point, is in the same cell. Less the error of the arithmetic (planes
and points are floats; a distance's rounding is bounded by a few units in the
last place of the point's coordinates and the planes' offsets, here allowed
for sixteen times over), such a point takes the same turn at every plane of
bsp3d_test_point's walk: it is answered with the walked leaf, the walk's own
answer.

A cell is a fact about the bsp alone, whoever's walk found it: one per
particle and contrail point, by its datum's absolute index (the callers name
it), each the last walked for it (a particle's first from its creation's
walk, particle_new); all forgotten when the structure's bsp changes
(halo_structure_bsp_generation, bumped as scenario.c sets the bsp). Only the
tick's thread uses them (the point physics runs there, in game_frame); another
thread walks as before. Kept out of the game state.
HALO_POINT_LEAF_CACHE=0 walks always; HALO_POINT_LEAF_CACHE_VERIFY=1 walks as
well and logs any difference.
*/

/* ---------- headers */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cseries.h"
#include "math/real_math.h"
#include "physics/bsp3d.h"
#include "scenario/scenario.h"
#include "point_leaf_cache.h"

void platform_log(const char *format, ...);

/* ---------- constants */

/* the rounding allowed for, per unit of the coordinates and offsets that go
into a distance (16 times the bound of a four-term float sum's error) */
#define POINT_LEAF_ERROR_SCALE (1.0f / 262144.0f)
/* and for the comparisons of squared lengths */
#define POINT_LEAF_COMPARE_SCALE (1.0f - 1.0f / 65536.0f)

/* ---------- structures */

typedef char point_leaf_cell_planes_check[
	NUMBEROF(((struct point_leaf_cell *)0)->signed_plane_indices) == BSP3D_POINT_NEAREST_PLANES ? 1 : -1];

/* ---------- globals */

unsigned long halo_structure_bsp_generation = 1;
unsigned long point_leaf_cache_points, point_leaf_cache_point_hits;

static struct point_leaf_cell point_leaf_particle_cells[POINT_LEAF_PARTICLE_CELLS];
static struct point_leaf_cell point_leaf_contrail_point_cells[POINT_LEAF_CONTRAIL_POINT_CELLS];

static struct
{
	unsigned long generation;
	struct bsp3d const *bsp;
	/* the largest plane offset (+1) and normal length (at least 1) */
	real offset;
	real normal;
	boolean usable;
} point_leaf_bsp;

static int point_leaf_enabled = -1;
static int point_leaf_verify;
static unsigned long point_leaf_mismatches;

/* ---------- private code */

static boolean point_leaf_cache_ready(
	void)
{
	struct bsp3d const *bsp;

	if (point_leaf_enabled < 0)
	{
		char const *setting = getenv("HALO_POINT_LEAF_CACHE");
		char const *verify = getenv("HALO_POINT_LEAF_CACHE_VERIFY");

		point_leaf_enabled = !setting || atoi(setting) != 0;
		point_leaf_verify = verify && atoi(verify) != 0;
	}
	if (!point_leaf_enabled)
		return FALSE;
	bsp = global_bsp3d_get();
	if (!bsp)
		return FALSE;
	if (point_leaf_bsp.generation != halo_structure_bsp_generation || point_leaf_bsp.bsp != bsp)
	{
		real offset = 0.0f;
		real normal_squared = 1.0f;
		long plane_index;

		for (plane_index = 0; plane_index < bsp->planes.count; plane_index++)
		{
			real_plane3d const *plane = TAG_BLOCK_GET_ELEMENT(&bsp->planes, plane_index, real_plane3d);
			real length_squared = plane->n.i*plane->n.i + plane->n.j*plane->n.j + plane->n.k*plane->n.k;

			if (!(fabsf(plane->d) <= offset))
				offset = fabsf(plane->d);
			if (!(length_squared <= normal_squared))
				normal_squared = length_squared;
		}
		point_leaf_bsp.offset = offset + 1.0f;
		point_leaf_bsp.normal = sqrtf(normal_squared) * (1.0f + 1.0f / 65536.0f);
		/* (a plane not finite: nothing is ever known) */
		point_leaf_bsp.usable = point_leaf_bsp.offset < 1.0e30f && point_leaf_bsp.normal < 1.0e30f;
		memset(point_leaf_particle_cells, 0, sizeof(point_leaf_particle_cells));
		memset(point_leaf_contrail_point_cells, 0, sizeof(point_leaf_contrail_point_cells));
		point_leaf_bsp.bsp = bsp;
		point_leaf_bsp.generation = halo_structure_bsp_generation;
	}

	return point_leaf_bsp.usable;
}

/* the key's cell, NULL for none */
static __inline__ struct point_leaf_cell *point_leaf_cell_get(
	long key)
{
	unsigned long index = (unsigned long)(key & 0xFFFF);

	switch (key >> 16)
	{
	case _point_leaf_particle:
		return index < POINT_LEAF_PARTICLE_CELLS ? &point_leaf_particle_cells[index] : NULL;
	case _point_leaf_contrail_point:
		return index < POINT_LEAF_CONTRAIL_POINT_CELLS ? &point_leaf_contrail_point_cells[index] : NULL;
	}

	return NULL;
}

/* the error a distance computed at the point may have, with the bsp's
largest offset (and an extra length's worth: a segment's) */
static __inline__ real point_leaf_error(
	real_point3d const *point,
	real extra)
{
	return POINT_LEAF_ERROR_SCALE * point_leaf_bsp.normal *
		(fabsf(point->x) + fabsf(point->y) + fabsf(point->z) + point_leaf_bsp.offset + extra);
}

/* whether the point is on the nearest planes' sides and within the reach of
the center, by its error and the extra */
static __inline__ boolean point_leaf_inside(
	struct point_leaf_cell const *cell,
	real_point3d const *point,
	real extra)
{
	struct bsp3d const *bsp = point_leaf_bsp.bsp;
	real error = point_leaf_error(point, extra);
	real room = (cell->reach - error) * POINT_LEAF_COMPARE_SCALE / point_leaf_bsp.normal;
	real dx = point->x - cell->center.x;
	real dy = point->y - cell->center.y;
	real dz = point->z - cell->center.z;
	short plane_index;

	if (!(room > 0.0f && dx*dx + dy*dy + dz*dz < room*room))
		return FALSE;
	for (plane_index = 0; plane_index < BSP3D_POINT_NEAREST_PLANES; plane_index++)
	{
		long signed_index = cell->signed_plane_indices[plane_index];
		real distance = plane3d_distance_to_point(
			TAG_BLOCK_GET_ELEMENT(&bsp->planes, signed_index < 0 ? ~signed_index : signed_index, real_plane3d),
			point);

		if (signed_index < 0 ? !(distance < -error) : !(distance > error))
			return FALSE;
	}

	return TRUE;
}

static long point_leaf_walk(
	struct point_leaf_cell *cell,
	real_point3d const *point)
{
	struct bsp3d_point_planes planes;
	long leaf_index = bsp3d_test_point_nearest_planes(point_leaf_bsp.bsp, 0, point, &planes);

	cell->center = *point;
	cell->leaf_index = leaf_index;
	cell->reach = leaf_index == NONE || planes.count < BSP3D_POINT_NEAREST_PLANES ?
		0.0f : planes.rest - point_leaf_error(point, 0.0f);
	if (!(cell->reach > 0.0f))
		cell->reach = 0.0f;
	cell->signed_plane_indices[0] = planes.signed_indices[0];
	cell->signed_plane_indices[1] = planes.signed_indices[1];
	cell->signed_plane_indices[2] = planes.signed_indices[2];

	return leaf_index;
}

static void point_leaf_mismatch(
	char const *what,
	real_point3d const *point,
	long cached,
	long walked)
{
	if (point_leaf_mismatches++ < 20)
		platform_log("point-leaf-cache: %s mismatch at %.9g %.9g %.9g: cached %ld, walked %ld",
			what, point->x, point->y, point->z, cached, walked);
}

/* ---------- public code */

long point_leaf_cache_leaf_from_point(
	long key,
	real_point3d const *point)
{
	struct point_leaf_cell *cell;

	if (!point_leaf_cache_usable() || !point_leaf_cache_ready() || !(cell = point_leaf_cell_get(key)))
		return bsp3d_test_point(global_bsp3d_get(), 0, point);
	point_leaf_cache_points++;
	/* (the particles move in the order of their indices: the next one's cell
	read in the meantime, a line the frame has not touched yet) */
	__builtin_prefetch(cell + 1);
	if (cell->reach > 0.0f && point_leaf_inside(cell, point, 0.0f))
	{
		point_leaf_cache_point_hits++;
		if (point_leaf_verify)
		{
			long walked = bsp3d_test_point(global_bsp3d_get(), 0, point);

			if (walked != cell->leaf_index)
				point_leaf_mismatch("point", point, cell->leaf_index, walked);
		}
		return cell->leaf_index;
	}

	return point_leaf_walk(cell, point);
}

long point_leaf_cache_seed(
	real_point3d const *point,
	struct point_leaf_cell *seed)
{
	if (!point_leaf_cache_usable() || !point_leaf_cache_ready())
	{
		seed->reach = 0.0f;
		return bsp3d_test_point(global_bsp3d_get(), 0, point);
	}

	return point_leaf_walk(seed, point);
}

void point_leaf_cache_seed_store(
	long key,
	struct point_leaf_cell const *seed)
{
	struct point_leaf_cell *cell;

	if (seed->reach > 0.0f &&
		point_leaf_cache_usable() &&
		point_leaf_cache_ready() &&
		(cell = point_leaf_cell_get(key)) != NULL)
	{
		*cell = *seed;
	}

	return;
}
