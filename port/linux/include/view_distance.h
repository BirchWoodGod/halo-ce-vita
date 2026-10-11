/* view_distance.h: where this machine's players look from (view_distance.c) */
#ifndef HALO_VIEW_DISTANCE_H
#define HALO_VIEW_DISTANCE_H

union real_point3d;

/* the main thread, cameras updated, before the tick starts (main.c) */
void halo_view_points_capture(void);
/* the squared distance from a point to the nearest local view (0 with none) */
float halo_view_distance_squared(union real_point3d const *point);

/* screen pixels across per world unit across at a world unit's distance:
the Vita's 544 lines over Halo's vertical field of view (about 42 degrees
at 70 horizontal, 16:9): 272 / tan(21 degrees) */
#define HALO_VIEW_PIXELS_PER_UNIT_AT_UNIT_DISTANCE 708.0f

#endif
