/*
BSP3D.H

header included in hcex build.
*/

#ifndef __BSP3D_H
#define __BSP3D_H
#pragma once


/* ---------- headers */

#include "tag_files/tag_groups.h"
#include "math/real_math.h"

/* ---------- constants */

enum
{
	/* port: how many nodes deep a walk of a bsp3d (from the map) goes
	before it stops: the engine's MAXIMUM_BSP3D_DEPTH (the plane stacks of
	collision_bsp.c, the path render_debug_bsp prints), where the retail
	bsps are at most 66 deep. A cycle of nodes would never end */
	MAXIMUM_BSP3D_TRAVERSAL_DEPTH = 128,
};

/* ---------- macros */

/* ---------- structures */

struct bsp3d
{
	struct tag_block nodes;
	struct tag_block planes;
};

struct bsp3d_node
{
	long plane_designator;
	long children[2];
};

typedef char bsp3d_node_size_check[
	sizeof(struct bsp3d_node) == 0xC ? 1 : -1];

/* ---------- prototypes/BSP3D.C */

long bsp3d_test_point(struct bsp3d const *bsp, long node_index, union real_point3d const *point);
#ifdef HALO_LINUX
/* (port) the planes a point came nearest to in its walk
(bsp3d_test_point_nearest_planes): each plane's index, its complement (~)
where the point was on its back */
enum
{
	BSP3D_POINT_NEAREST_PLANES = 3
};

struct bsp3d_point_planes
{
	short count;
	/* the least distance to the other planes tested */
	real rest;
	long signed_indices[BSP3D_POINT_NEAREST_PLANES];
};

long bsp3d_test_point_nearest_planes(struct bsp3d const *bsp, long node_index, union real_point3d const *point,
	struct bsp3d_point_planes *planes);
#endif

/* ---------- globals */

/* ---------- public code */

__inline real_plane3d *bsp3d_get_plane_from_designator(
	struct bsp3d const *bsp,
	long plane_designator,
	real_plane3d *result)
{
	real_plane3d* plane = TAG_BLOCK_GET_ELEMENT(&bsp->planes, plane_designator & LONG_MAX, real_plane3d);

	if (plane_designator & LONG_MIN)
	{
		plane3d_negate(plane, result);
	}
	else
	{
		*result = *plane;
	}

	return result;
}

#endif // __BSP3D_H
