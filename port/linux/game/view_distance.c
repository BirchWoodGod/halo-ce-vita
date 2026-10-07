/* view_distance.c

(port) Where this machine's players look from, for work whose only result
is a picture that would be too small to see from there: decals far from
every local view (decals.c, HALO_DECAL_MIN_PIXELS).

The views are taken on the main thread once its cameras are updated for the
frame (observer_update), right before the tick starts (main.c), so the tick
reads a copy the render does not change under it. Only what this machine
draws depends on them, never the simulation: each machine makes its own
decals (the local random seed), and they are not sent over the network. */

#include "cseries.h"
#include "camera/observer.h"
#include "game/players.h"

#include "view_distance.h"

static real_point3d view_points[MAXIMUM_LOCAL_PLAYERS];
static short view_count;

void halo_view_points_capture(
	void)
{
	short local_player_index;
	short count = 0;

	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		struct observer_result const *camera;

		if (local_player_get_player_index(local_player_index) == NONE)
			continue;
		camera = observer_get_camera(local_player_index);
		if (camera)
			view_points[count++] = camera->position;
	}
	view_count = count;
}

/* the squared distance to the nearest local view; 0 with none (as if
everything were near) */
float halo_view_distance_squared(
	union real_point3d const *point)
{
	real nearest = REAL_MAX;
	short index;

	if (!view_count)
		return 0.0f;
	for (index = 0; index < view_count; index++)
	{
		real dx = point->x - view_points[index].x;
		real dy = point->y - view_points[index].y;
		real dz = point->z - view_points[index].z;
		real distance_squared = dx * dx + dy * dy + dz * dz;

		if (distance_squared < nearest)
			nearest = distance_squared;
	}
	return nearest;
}
