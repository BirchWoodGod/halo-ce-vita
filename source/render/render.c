/*
RENDER.C

symbols in this file:
001743B0 0020:
	_render_effects (0000)
001743D0 0010:
	_render_initialize (0000)
001743E0 0010:
	_render_initialize_for_new_map (0000)
001743F0 0010:
	_render_dispose_from_old_map (0000)
00174400 0010:
	_render_dispose (0000)
00174410 0110:
	_render_nonplayer_frame (0000)
00174520 00f0:
	_render_frame_pregame (0000)
00174610 0020:
	_render_frame_present (0000)
00174630 0070:
	_render_location_visible (0000)
001746A0 0050:
	_rendered_cluster_get (0000)
001746F0 03f0:
	_render_window (0000)
00174AE0 03f0:
	_render_player_frame (0000)
00174ED0 00f0:
	_render_frame (0000)
0029F44C 001f:
	??_C@_0BP@JNDFGHKA@c?3?2halo?2SOURCE?2render?2render?4c?$AA@ (0000)
0029F470 0061:
	??_C@_0GB@NHJDHKGO@location?9?$DOcluster_index?$DO?$DN0?5?$CG?$CG?5lo@ (0000)
0029F4D8 0052:
	??_C@_0FC@DLFICDJP@rendered_cluster_index?$DO?$DN0?5?$CG?$CG?5ren@ (0000)
0029F530 0052:
	??_C@_0FC@NBMIPGBN@window?9?$DOrender_camera?4viewport_b@ (0000)
0029F588 0051:
	??_C@_0FB@IDPOBHAJ@window?9?$DOrender_camera?4viewport_b@ (0000)
0029F5DC 002c:
	??_C@_0CM@LKHLGDKF@window?9?$DOrender_camera?4viewport_b@ (0000)
0029F608 002c:
	??_C@_0CM@HBCHLAAA@window?9?$DOrender_camera?4viewport_b@ (0000)
0029F638 006d:
	??_C@_0GN@PKELCNNH@?$CBmemcmp?$CI?$CGwindow?9?$DOrender_camera?4w@ (0000)
0029F6A8 0071:
	??_C@_0HB@BCLIBPGM@?$CBmemcmp?$CI?$CGwindow?9?$DOrender_camera?4v@ (0000)
0029F720 0048:
	??_C@_0EI@NGOLLMDP@?$CD?$CD?$CD?5ERROR?5something?5is?5wrong?5wit@ (0000)
0030D4D2 0004:
	_render_contrails_enabled (0000)
	_render_particles_enabled (0001)
	_render_particle_systems_enabled (0002)
	_render_weather_particle_systems_enabled (0003)
004B8B22 0001:
	_render_invalid_fog_warning_displayed (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "render.h"
#include "render_cameras_internal.h"
#include "render_particles.h"
#include "objects.h"
#include "scenario.h"
#include "structure_bsp_definitions.h"
#include "structures/structure_visibility.h"
#include "rasterizer.h"
#include "rasterizer/rasterizer_lights.h"
#include "profile.h"
#include "progress_bar.h"
#include "ui_widget.h"
#include "bink_playback.h"
#include "game.h"
#include "game_engine.h"
#include "interface/first_person_weapons.h"
#include "editor_stubs.h"
#include "render_debug.h"
#include "objects/object_lights_rendering.h"
#include "effects/particle_systems.h"
#include "effects/weather_particle_systems.h"

/* ---------- constants */

enum
{
	_render_target_primary = 0,
	_render_target_secondary,
};

enum
{
	_decal_layer_primary = 0,
	_decal_layer_secondary,
	_decal_layer_light,
	_decal_layer_alpha_tested,
	_decal_layer_water,
};

enum
{
	_render_planar_fog_mode_fully_fogged = 2,
};

/* ---------- macros */

#ifdef HALO_LINUX
#define RASTERIZER_TARGET_RENDER_PRIMARY_WIDTH halo_screen_width()
#else
#define RASTERIZER_TARGET_RENDER_PRIMARY_WIDTH 640
#endif
#define RASTERIZER_TARGET_RENDER_PRIMARY_HEIGHT 480

/* ---------- structures */

/* ---------- prototypes */

static void render_nonplayer_frame(
	const struct render_window *window,
	long window_type);
static void render_window(
	short local_player_index,
	const struct render_camera *camera,
	const struct render_frustum *frustum,
	const struct render_camera *rasterizer_camera,
	const struct render_frustum *rasterizer_frustum,
	short rasterizer_target,
	boolean has_mirror);
static void render_player_frame(
	struct render_window *window,
	const point2d *screenshot_combined_index);

void structure_get_planar_fog(
	short cluster_index,
	struct render_fog *fog);
void player_effect_get_screen_flash(
	short local_player_index,
	struct render_screen_flash *screen_flash);
void build_sprite_prepare_for_window(
	void);
void render_sky(
	void);
void render_objects(
	void);
void render_object_shadows(
	void);
void rasterizer_transparent_geometry_stop(
	void);
void interface_draw_fullscreen_overlays(
	void);
void rasterizer_debug_draw(
	void);
void render_debug(
	void);
void progress_bar_eachframe(
	void);
short main_get_window_count(
	void);

/* ---------- globals */

struct render_globals render;

static boolean render_invalid_fog_warning_displayed;

extern short global_screenshot_count;
extern short global_screenshot_size;

boolean render_contrails_enabled = TRUE;
boolean render_particles_enabled = TRUE;
boolean render_particle_systems_enabled = TRUE;
boolean render_weather_particle_systems_enabled = TRUE;

/* ---------- public code */

void render_effects(
	boolean enable)
{
	render_weather_particle_systems_enabled = enable;
	render_particle_systems_enabled = enable;
	render_particles_enabled = enable;
	render_contrails_enabled = enable;
}

void render_initialize(
	void)
{
	render_objects_initialize();
}

void render_initialize_for_new_map(
	void)
{
	render_objects_initialize_for_new_map();
}

void render_dispose_from_old_map(
	void)
{
	render_objects_dispose_from_old_map();
}

void render_dispose(
	void)
{
	render_objects_dispose();
}

static void render_nonplayer_frame(
	const struct render_window *window,
	long window_type)
{
	struct rasterizer_window_begin_parameters parameters;

	profile_render_window_start(FALSE);
	memset(&parameters, 0, sizeof(parameters));

	render.camera = window->render_camera;
	render_camera_build_frustum(&render.camera, NULL, &render.frustum, TRUE);

	parameters.camera = window->rasterizer_camera;
	render_camera_build_frustum(&parameters.camera, NULL, &parameters.frustum, TRUE);

	parameters.rasterizer_target = _render_target_primary;
	parameters.suppress_clear = window_type == 0;
	parameters.window_index = NONE;
	rasterizer_window_begin(&parameters);

	switch (window_type)
	{
	case 0:
		/* letterbox bars and split screen dividers: laid out from the
		viewport, so not centered like the menus on a wide screen */
		interface_draw_fullscreen_overlays();
		rasterizer_debug_draw();
		break;

	case 1:
#ifdef HALO_LINUX
		halo_screen_ui_offset(TRUE);
		game_engine_nonplayer_post_rasterize();
		halo_screen_ui_offset(FALSE);
#else
		game_engine_nonplayer_post_rasterize();
#endif
		break;

	default:
		match_assert("c:\\halo\\SOURCE\\render\\render.c", 287, !"unreachable");
		break;
	}

	rasterizer_window_end();
	profile_render_window_end();

	return;
}

void render_frame_pregame(
	struct render_window const *window,
	struct bitmap_data *bitmap)
{
	struct rasterizer_frame_begin_parameters parameters;
	struct rasterizer_window_begin_parameters rasterizer_parameters;

	render.frame_index++;

	rasterizer_frame_begin(&parameters);
	rasterizer_windows_begin();
	profile_render_window_start(FALSE);

	memset(&rasterizer_parameters, 0, sizeof(rasterizer_parameters));

	render.camera = window->render_camera;
	render_camera_build_frustum(&render.camera, NULL, &render.frustum, TRUE);

	rasterizer_parameters.camera = window->rasterizer_camera;
	render_camera_build_frustum(&rasterizer_parameters.camera, NULL, &rasterizer_parameters.frustum, TRUE);

	rasterizer_parameters.rasterizer_target = 0;
	rasterizer_window_begin(&rasterizer_parameters);

#ifdef HALO_LINUX
	halo_screen_ui_offset(TRUE);
	render_ui_widgets(0, &window->rasterizer_camera.viewport_bounds);
	halo_screen_ui_offset(FALSE);
#else
	render_ui_widgets(0, &window->rasterizer_camera.viewport_bounds);
#endif
	bink_playback_render();

	{
		real progress;
		if (game_map_loading_in_progress(&progress))
		{
			progress_bar_display(progress);
		}
	}

	rasterizer_window_end();
	profile_render_window_end();
	rasterizer_windows_end();
	rasterizer_frame_end();

	return;
}

void render_frame_present(
	const point2d *screenshot_index,
	struct bitmap_data *bitmap)
{
	rasterizer_present(bitmap, screenshot_index);

	return;
}

boolean render_location_visible(
	struct location *location)
{
	match_assert("c:\\halo\\SOURCE\\render\\render.c", 584, location->cluster_index>=0 && location->cluster_index<global_structure_bsp_get()->clusters.count);
	return TEST_FLAG(render.visible_cluster_flags[location->cluster_index>>5], location->cluster_index&31);
}

struct rendered_cluster *rendered_cluster_get(
	short rendered_cluster_index)
{
	match_assert("c:\\halo\\SOURCE\\render\\render.c", 592, rendered_cluster_index>=0 && rendered_cluster_index<render.rendered_cluster_count);
	return &render.rendered_clusters[rendered_cluster_index];
}


#ifdef HALO_LINUX
/* HALO_RENDER_PROFILE=1: where render_window's time goes, every 300 frames
(as game.c's tick profile) */
#include <stdlib.h>
static int render_profile_enabled = -1;
static unsigned long long render_phase_started, render_phase_us[32];
static const char *render_phase_name[32];
static unsigned long render_profile_frames;
unsigned long long vita_host_time_us(void);
void platform_log(const char *format, ...);
static unsigned long long render_now(void) { return vita_host_time_us ? vita_host_time_us() : 0; }
#define RENDER_PHASE_BEGIN() do { if (render_profile_enabled > 0) render_phase_started = render_now(); } while (0)
#define RENDER_PHASE_END(phase, name) do { \
	if (render_profile_enabled < 0) { const char *e = getenv("HALO_RENDER_PROFILE"); render_profile_enabled = e && atoi(e) != 0; } \
	if (render_profile_enabled > 0) { render_phase_us[phase] += render_now() - render_phase_started; render_phase_name[phase] = name; } } while (0)
static void render_phase_report(void)
{
	char line[800]; int n = 0, i;
	if (render_profile_enabled <= 0 || ++render_profile_frames % 300) return;
	for (i = 0; i < 32; i++) { if (!render_phase_name[i]) continue;
		n += snprintf(line + n, sizeof(line) - n, " %s %.1f", render_phase_name[i], render_phase_us[i] / 1000.0 / 300.0); render_phase_us[i] = 0; }
	platform_log("render-profile (ms/frame):%s", line);
}
#define RENDER_PHASE_REPORT() render_phase_report()
#else
#define RENDER_PHASE_BEGIN() ((void)0)
#define RENDER_PHASE_END(phase, name) ((void)0)
#define RENDER_PHASE_REPORT() ((void)0)
#endif

static void render_window(
	short local_player_index,
	const struct render_camera *camera,
	const struct render_frustum *frustum,
	const struct render_camera *rasterizer_camera,
	const struct render_frustum *rasterizer_frustum,
	short rasterizer_target,
	boolean has_mirror)
{
	struct rasterizer_window_begin_parameters parameters;
	short rendered_cluster_index;

	profile_render_window_start(TRUE);
	render.scene_index++;
	memset(&parameters, 0, sizeof(parameters));

	render.local_player_index = local_player_index;
	render.camera = *camera;
	render.frustum = *frustum;
	parameters.camera = *rasterizer_camera;
	parameters.frustum = *rasterizer_frustum;
	parameters.rasterizer_target = rasterizer_target;
	parameters.has_mirror = has_mirror;
	parameters.window_index = render.window_index;
	parameters.fog = render.fog;

	RENDER_PHASE_BEGIN();
	structure_visibility_compute();
	RENDER_PHASE_END(0, "visibility");
	player_effect_get_screen_flash(local_player_index, &parameters.screen_flash);
	RENDER_PHASE_BEGIN();
	rasterizer_window_begin(&parameters);
	RENDER_PHASE_END(21, "window_begin");

	if (!bink_playback_in_progress())
	{
		build_sprite_prepare_for_window();
		RENDER_PHASE_BEGIN();
		render_sky();
		RENDER_PHASE_END(1, "sky");
		RENDER_PHASE_BEGIN();
		first_person_weapon_render_update();
		RENDER_PHASE_END(27, "fp_update");
		RENDER_PHASE_BEGIN();
		lights_preprocess_scene();
		RENDER_PHASE_END(2, "lights_preprocess");
		RENDER_PHASE_BEGIN();
		render_objects();
		RENDER_PHASE_END(3, "objects");
		RENDER_PHASE_BEGIN();
		structure_render_preprocess();
		RENDER_PHASE_END(4, "structure_preprocess");
		RENDER_PHASE_BEGIN();
		structure_render_lightmaps();
		RENDER_PHASE_END(5, "lightmaps");
		RENDER_PHASE_BEGIN();
		rasterizer_lens_flares_submit_occlusion_tests();
		RENDER_PHASE_END(28, "flare_tests");
		RENDER_PHASE_BEGIN();
#ifdef HALO_LINUX
		{
			/* (port) with HALO_NO_SMALL_TARGETS=1 the shadow draws are dropped
			by the device: the pass that builds them is skipped too */
			static int no_shadows = -1;

			if (no_shadows < 0)
			{
				const char *setting = getenv("HALO_NO_SMALL_TARGETS");
				no_shadows = setting && atoi(setting) != 0;
			}
			if (!no_shadows)
				render_object_shadows();
		}
#else
		render_object_shadows();
#endif
		RENDER_PHASE_END(6, "shadows");
		RENDER_PHASE_BEGIN();
		lights_render_diffuse();
		RENDER_PHASE_END(7, "lights_diffuse");

		rasterizer_decals_begin(_decal_layer_light);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();

		rasterizer_decals_begin(_decal_layer_alpha_tested);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();

		RENDER_PHASE_BEGIN();
		structure_render_diffuse_texture();
		RENDER_PHASE_END(8, "structure_diffuse");

		rasterizer_decals_begin(_decal_layer_primary);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();

		rasterizer_decals_begin(_decal_layer_secondary);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();

		RENDER_PHASE_BEGIN();
		lights_render_specular();
		RENDER_PHASE_END(9, "lights_specular");
		RENDER_PHASE_BEGIN();
		structure_render_specular_lightmaps();
		RENDER_PHASE_END(10, "specular_lightmaps");
		structure_render_reflection_lightmap_masks();
		structure_render_reflection_mirrors();
		RENDER_PHASE_BEGIN();
		structure_render_reflections();
		RENDER_PHASE_END(11, "reflections");
		RENDER_PHASE_BEGIN();
		structure_render_transparent_geometry();
		RENDER_PHASE_END(12, "structure_transparent");
		RENDER_PHASE_BEGIN();
		structure_render_fog();
		RENDER_PHASE_END(13, "fog");
		game_engine_post_rasterize_objects();
		RENDER_PHASE_BEGIN();
		weather_particle_systems_render();
		RENDER_PHASE_END(14, "weather");
		RENDER_PHASE_BEGIN();
		render_particles();
		RENDER_PHASE_END(15, "particles");
		RENDER_PHASE_BEGIN();
		particle_systems_render();
		RENDER_PHASE_END(16, "particle_systems");
		RENDER_PHASE_BEGIN();
		render_contrails_normal();
		RENDER_PHASE_END(17, "contrails");
		RENDER_PHASE_BEGIN();
		rasterizer_transparent_geometry_draw(TRUE);
		RENDER_PHASE_END(18, "transparent_draw");

		rasterizer_decals_begin(_decal_layer_water);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();

		RENDER_PHASE_BEGIN();
		structure_render_detail_objects();
		RENDER_PHASE_END(19, "detail_objects");
		rasterizer_transparent_geometry_draw(FALSE);
		rasterizer_transparent_geometry_stop();
		RENDER_PHASE_BEGIN();
		structure_render_fog_screen();
		RENDER_PHASE_END(29, "fog_screen");
		RENDER_PHASE_BEGIN();
		rasterizer_lens_flares_draw();
		RENDER_PHASE_END(30, "flares");
		RENDER_PHASE_BEGIN();
		interface_draw_screen();
		RENDER_PHASE_END(20, "interface");
		RENDER_PHASE_BEGIN();
		rasterizer_screen_flash();
		RENDER_PHASE_END(31, "screen_flash");
#ifdef HALO_LINUX
		RENDER_PHASE_BEGIN();
		halo_screen_ui_offset(TRUE);
		render_ui_widgets(local_player_index, &rasterizer_camera->viewport_bounds);
		halo_screen_ui_offset(FALSE);
		RENDER_PHASE_END(22, "ui_widgets");
#else
		render_ui_widgets(local_player_index, &rasterizer_camera->viewport_bounds);
#endif
	}

	bink_playback_render();
	render_camera_debug_frustum(&render.camera, &render.frustum);
	render_debug();
	editor_render();
	rasterizer_debug_draw();
	RENDER_PHASE_BEGIN();
	rasterizer_window_end();
	RENDER_PHASE_END(23, "window_end");
	profile_render_window_end();

	RENDER_PHASE_REPORT();
	return;
}

static void render_player_frame(
	struct render_window *window,
	const point2d *screenshot_combined_index)
{
	struct render_camera *camera;
	boolean has_mirror;
	real_rectangle2d frustum_bounds;
	struct render_frustum frustum;
	struct render_frustum rasterizer_frustum;
	struct render_mirror mirror;

	camera = &window->render_camera;
	has_mirror = FALSE;

	RENDER_PHASE_BEGIN();
	structure_visibility_find_camera(camera);
	render.fog.runtime_flags = 0;
	scenario_get_atmospheric_fog(
		window->local_player_index,
		(word)render.visible_sky_index,
		&camera->position,
		&render.fog);
	structure_get_planar_fog((short)render.cluster_index, &render.fog);

	if (render.fog.atmospheric_maximum_distance != 0.0f &&
		render.visible_sky_index == NONE &&
		render.fog.planar_maximum_distance > render.fog.atmospheric_maximum_distance)
	{
		render.fog.planar_maximum_distance = render.fog.atmospheric_maximum_distance;
	}

	if (render.fog.atmospheric_maximum_density == 1.0f &&
		render.fog.atmospheric_maximum_distance != 0.0f)
	{
		window->render_camera.z_far = MIN(
			window->render_camera.z_far,
			render.fog.atmospheric_maximum_distance);
	}

	if (render.fog.planar_mode == _render_planar_fog_mode_fully_fogged &&
		render.fog.planar_maximum_distance != 0.0f)
	{
		window->render_camera.z_far = MIN(
			window->render_camera.z_far,
			render.fog.planar_maximum_distance);
	}

	if (window->render_camera.z_far <= window->render_camera.z_near)
	{
		if (!render_invalid_fog_warning_displayed)
		{
			error(2, "### ERROR something is wrong with the fog in the sky tag or the fog tag");
			render_invalid_fog_warning_displayed = TRUE;
		}

		window->render_camera.z_far = window->render_camera.z_near + 0.01f;
	}

	match_assert(
		"c:\\halo\\SOURCE\\render\\render.c",
		187,
		!memcmp(&window->render_camera.viewport_bounds,
			&window->rasterizer_camera.viewport_bounds,
			sizeof(rectangle2d)));
	match_assert(
		"c:\\halo\\SOURCE\\render\\render.c",
		188,
		!memcmp(&window->render_camera.window_bounds,
			&window->rasterizer_camera.window_bounds,
			sizeof(rectangle2d)));

	render_camera_build_frustum_bounds(camera, &frustum_bounds);

	if (screenshot_combined_index != NULL)
	{
		long tile_count;

		tile_count = global_screenshot_count * global_screenshot_size;
		if (tile_count > 0)
		{
			real tile_width;
			real tile_height;
			real_rectangle2d adjusted_bounds;

			tile_width = (frustum_bounds.x1 - frustum_bounds.x0) / (real)tile_count;
			tile_height = (frustum_bounds.y1 - frustum_bounds.y0) / (real)tile_count;
			adjusted_bounds.x0 =
				(real)screenshot_combined_index->x * tile_width + frustum_bounds.x0;
			adjusted_bounds.y0 =
				(real)(tile_count - screenshot_combined_index->y - 1) * tile_height + frustum_bounds.y0;
			adjusted_bounds.x1 = adjusted_bounds.x0 + tile_width;
			adjusted_bounds.y1 = adjusted_bounds.y0 + tile_height;
			frustum_bounds = adjusted_bounds;
		}
	}

	render_camera_build_frustum(camera, &frustum_bounds, &frustum, TRUE);
	render_camera_build_frustum(
		&window->rasterizer_camera,
		&frustum_bounds,
		&rasterizer_frustum,
		TRUE);

	RENDER_PHASE_END(25, "player_frame_pre");
	if (main_get_window_count() == 1)
	{
		if (structure_visibility_find_mirror(camera, &frustum, &mirror))
		{
			short saved_cluster_index;
			struct render_camera mirror_camera;
			struct render_frustum mirror_frustum;

			saved_cluster_index = (short)render.cluster_index;
			match_assert("c:\\halo\\SOURCE\\render\\render.c", 225, window->render_camera.viewport_bounds.x0==0);
			match_assert("c:\\halo\\SOURCE\\render\\render.c", 226, window->render_camera.viewport_bounds.y0==0);
			match_assert("c:\\halo\\SOURCE\\render\\render.c", 227, window->render_camera.viewport_bounds.x1==RASTERIZER_TARGET_RENDER_PRIMARY_WIDTH);
			match_assert("c:\\halo\\SOURCE\\render\\render.c", 228, window->render_camera.viewport_bounds.y1==RASTERIZER_TARGET_RENDER_PRIMARY_HEIGHT);

			render_camera_mirror(camera, &mirror, &mirror_camera);
			render_camera_build_frustum(
				&mirror_camera,
				&frustum_bounds,
				&mirror_frustum,
				TRUE);

			rasterizer_profile_enable(FALSE);
			render.cluster_index = mirror.cluster_index;
			render_window(
				NONE,
				&mirror_camera,
				&mirror_frustum,
				&mirror_camera,
				&mirror_frustum,
				_render_target_secondary,
				FALSE);
			render.cluster_index = saved_cluster_index;
			rasterizer_profile_enable(TRUE);
			has_mirror = TRUE;
		}
	}

	render_window(
		window->local_player_index,
		camera,
		&frustum,
		&window->rasterizer_camera,
		&rasterizer_frustum,
		_render_target_primary,
		has_mirror);

	return;
}

void render_frame(
	struct render_window *windoze,
	short window_count,
	const point2d *screenshot_page_index,
	const point2d *screenshot_index,
	struct bitmap_data *screenshot_bitmap,
	real time_delta_since_tick_sec)
{
	struct rasterizer_frame_begin_parameters parameters;
	short window_index;
	struct render_window *window;
	point2d screenshot_combined_index;

	render.frame_index++;
	render.time_delta_since_tick_sec = time_delta_since_tick_sec;
	memset(&parameters, 0, sizeof(parameters));
#ifdef HALO_LINUX
	/* continuous between ticks (render_interpolation.c) */
	parameters.game_time_sec = render_interpolation_game_time_sec(game_time_get());
#else
	parameters.game_time_sec = (real)game_time_get() * (1.0f / TICKS_PER_SECOND);
#endif
	RENDER_PHASE_BEGIN();
	rasterizer_frame_begin(&parameters);
	rasterizer_windows_begin();
	RENDER_PHASE_END(24, "frame_begin");

	for (window_index = 0; window_index < window_count; window_index++)
	{
		long window_type;

		window = &windoze[window_index];
		render.window_index = window_index;
		if (window->console_window)
		{
			window_type = 0;
		}
		else if (window->local_player_index != NONE)
		{
			if (screenshot_index != NULL && screenshot_page_index != NULL)
			{
				screenshot_combined_index.x =
					screenshot_page_index->x * global_screenshot_size + screenshot_index->x;
				screenshot_combined_index.y =
					screenshot_page_index->y * global_screenshot_size + screenshot_index->y;
			}

			render_player_frame(
				window,
				screenshot_index != NULL ? &screenshot_combined_index : NULL);
			continue;
		}
		else
		{
			window_type = 1;
		}

		render_nonplayer_frame(window, window_type);
	}

#ifdef HALO_LINUX
	halo_screen_ui_offset(TRUE);
	progress_bar_eachframe();
	halo_screen_ui_offset(FALSE);
#else
	progress_bar_eachframe();
#endif
	RENDER_PHASE_BEGIN();
	rasterizer_windows_end();
	rasterizer_frame_end();
	RENDER_PHASE_END(26, "frame_end");

	return;
}

/* ---------- private code */
