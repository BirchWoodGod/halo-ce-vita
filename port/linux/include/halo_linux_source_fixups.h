/*
HALO_LINUX_SOURCE_FIXUPS.H

Game-only workarounds for source that MSVC accepts but clang rejects, where
editing the source itself would change the byte-matched MSVC output (see
port/linux/README.md for how each was checked).
*/

#ifndef __HALO_LINUX_SOURCE_FIXUPS_H
#define __HALO_LINUX_SOURCE_FIXUPS_H

/* rasterizer.h declares rasterizer_debug_drawing_begin(boolean opaque) while
rasterizer_xbox_debug.h declares a second `long zbias` parameter, and
rasterizer_debug.c includes both and passes two arguments. MSVC tolerates
the mismatch; the definition ignores zbias. Adding the parameter to
rasterizer.h perturbs MSVC's register allocation elsewhere, so instead every
declaration and call collapses to the one-parameter form here. */
#define rasterizer_debug_drawing_begin(opaque, ...) (rasterizer_debug_drawing_begin)(opaque)

/* frames between the 30 Hz ticks (port/linux/game/render_interpolation.c);
the platform layer reads the display.interpolation setting */
struct observer_result;
struct render_camera;
struct real_matrix4x3;
int halo_interpolation_enabled(void);
float game_time_get_tick_fraction(void);
/* the fraction for the state the render draws (the finished update's, with the tick on its thread) */
float halo_render_tick_fraction_get(void);
/* the first-person weapon blended between ticks without the rest (HALO_INTERPOLATE_FIRST_PERSON) */
int halo_first_person_interpolation_enabled(void);
void render_interpolation_tick(void);
void render_interpolation_reset(void);
void render_interpolation_frame_begin(void);
void render_interpolation_frame_end(void);
float render_interpolation_fraction(void);
struct real_matrix4x3 *render_interpolation_object_node_matrices(long object_index);
/* with the tick on its thread, the bounding sphere of the pose drawn
(render_interpolation.c, "the threaded tick's poses"); FALSE when the
object is drawn live */
union real_point3d;
unsigned char render_tick_pose_bounding_sphere(long object_index, union real_point3d *center, float *radius);
/* ... and the clusters' object lists as that tick left them, which the
render walks instead of the running tick's (render_interpolation.c, "the
threaded tick's cluster lists") */
enum { _tick_cluster_list_collideable, _tick_cluster_list_noncollideable, _tick_cluster_list_light, _tick_cluster_list_count };
unsigned char render_tick_cluster_lists_active(int which);
long render_tick_cluster_list_first(int which, long *iterator, short cluster_index);
long render_tick_cluster_list_next(int which, long *iterator);
/* the clusters an object (its ultimate parent) was in when that tick was
captured; 0 when the frame walks the live lists or the object is not there */
unsigned char render_tick_object_clusters(long object_index, short const **clusters, short *count);
struct observer_result const *render_interpolation_camera(short local_player_index,
	struct observer_result const *observer);
void render_interpolation_first_person(short local_player_index, struct real_matrix4x3 *node_matrices,
	short node_count, struct render_camera const *camera);
float render_interpolation_game_time_sec(long ticks);

/* the width of the screen the game draws, 480 lines tall: the device's or
the display's shape, or 640 (port/linux/src/d3d8_gl.c) */
long halo_screen_width(void);
/* takes up a new width between frames (F11); returns the width */
long halo_screen_commit(void);
/* the Custom Edition tag cache window, or NULL unless HALO_CUSTOM_EDITION
reserved it (port/linux/src/xbox_memory.c) */
void *halo_custom_edition_tag_cache(void);
/* whether Custom Edition maps may run, and a map's tag cache when the
window above is not there (port/linux/src/xbox_memory.c) */
int halo_custom_edition_enabled(void);
void *halo_custom_edition_tag_cache_acquire(unsigned long bytes);
void halo_custom_edition_tag_cache_release(void);
/* zeroed memory blocks of their own for a Custom Edition map's converted
geometry and its conversion (port/linux/src/xbox_memory.c); NULL when there
is no room */
void *halo_custom_edition_memory_alloc(unsigned long bytes);
void halo_custom_edition_memory_free(void *address);
/* the C heap's bytes in use and size (0: none fixed) */
void platform_heap_usage(unsigned long *in_use, unsigned long *capacity);
/* the contiguous window's bytes in blocks and free (where blocks are laid out) */
void platform_contiguous_usage(unsigned long *used, unsigned long *free_bytes);
/* where Halo PC keeps the channels of the pixels a Custom Edition bitmap
just arrived at (an enum custom_edition_channel_order,
port/linux/game/cache_file_formats.h), which the renderer then samples in
this build's order; forgotten together when the map goes
(port/linux/src/xbox_textures.c) */
void halo_custom_edition_texels_channels(const void *texels, unsigned char channel_order);
void halo_custom_edition_texels_forget(void);
/* a vertex buffer (its Direct3D header) the game made once and will not
rewrite until it says otherwise: the Vita's device reads it in place
(port/vita/platform/d3d8_gxm.c) */
void halo_d3d_buffer_in_place(const void *buffer, int in_place);
/* whether a Halo Custom Edition map's multiplayer vehicles are chosen by
their placements' spawn flags, as in retail Halo, and whether a vehicle
placement is placed in the running game
(port/linux/game/custom_edition_objects.c) */
struct scenario_object_datum;
unsigned char custom_edition_vehicles_by_placement(void);
/* when the last load of the Custom Edition map `map_name` names failed: the
player is told why, and the caller goes back to the menu
(port/linux/game/custom_edition_cache.c) */
unsigned char custom_edition_cache_load_failure_show(char const *map_name);
unsigned char custom_edition_vehicle_placement_allowed(struct scenario_object_datum const *placement);

/* while TRUE, drawing shifts right to center 640-column layouts */
void halo_screen_ui_offset(unsigned char centered);
/* the open movie's display shape (width / height) when its file gives one
apart from its size in pixels, 0 otherwise (port/vita/platform/bink_vita.c;
port/linux/src/bink_null.c: 0) */
float halo_movie_display_aspect(void);
/* the mouse in the menus (source/interface/ui_widget.c) */
#include "halo_ui_pointer.h"

#endif
