/*
RENDER_OBJECTS.C

symbols in this file:
0017A740 00a0:
	_object_get_render_bounding_sphere (0000)
0017A7E0 0040:
	_render_objects_initialize (0000)
0017A820 0010:
	_render_objects_initialize_for_new_map (0000)
0017A830 0020:
	_render_objects_dispose_from_old_map (0000)
0017A850 0010:
	_render_objects_dispose (0000)
0017A860 0070:
	_code_0017a860 (0000)
0017A8D0 00b0:
	_code_0017a8d0 (0000)
0017A980 0060:
	_code_0017a980 (0000)
0017A9E0 0480:
	_code_0017a9e0 (0000)
0017AE60 00a0:
	_code_0017ae60 (0000)
0017AF00 00d0:
	_code_0017af00 (0000)
0017AFD0 00b0:
	_code_0017afd0 (0000)
0017B080 0100:
	_code_0017b080 (0000)
0017B180 0060:
	_code_0017b180 (0000)
0017B1E0 02d0:
	_code_0017b1e0 (0000)
0017B4B0 0320:
	_code_0017b4b0 (0000)
0017B7D0 0130:
	_code_0017b7d0 (0000)
0017B900 0050:
	_object_get_cached_render_lighting (0000)
0017B950 0270:
	_code_0017b950 (0000)
0017BBC0 0030:
	_code_0017bbc0 (0000)
0017BBF0 00c0:
	_render_objects (0000)
0017BCB0 0070:
	_render_object_shadows (0000)
0029FEB0 0016:
	??_C@_0BG@FKOKPMPP@render_object_shadows?$AA@ (0000)
0029FEC8 000f:
	??_C@_0P@LMDLJPAO@render_objects?$AA@ (0000)
0029FED8 001c:
	??_C@_0BM@HFAFBIOC@cached_object_render_states?$AA@ (0000)
0029FEF4 0027:
	??_C@_0CH@NBGGJMPB@c?3?2halo?2SOURCE?2render?2render_obj@ (0000)
0029FF1C 001c:
	??_C@_0BM@GFBNGBOK@cached?5object?5render?5states?$AA@ (0000)
0029FF38 0023:
	??_C@_0CD@FKDHABCL@MAXIMUM_RENDERED_OBJECTS?5exceede@ (0000)
0029FF5C 000c:
	??_C@_0M@EIIDIBKF@inactive?5?$CFs?$AA@ (0000)
0029FF68 0022:
	??_C@_0CC@IDEANNOJ@?$CD?$CD?$CD?5ERROR?5invalid?5modifier?5shade@ (0000)
0029FF8C 000f:
	??_C@_0P@POPICHFJ@data?9?$DOlighting?$AA@ (0000)
0029FF9C 0014:
	??_C@_0BE@MCONOJLA@parent_model_effect?$AA@ (0000)
0029FFB0 0091:
	??_C@_0JB@OHAJFPFN@state?9?$DOlighting?4point_light_indi@ (0000)
002A0044 002f:
	??_C@_0CP@MCHIMIAE@state?9?$DOdesired_lighting?4distant_@ (0000)
002A0074 0004:
	__real@4111745c (0000)
002A0078 0004:
	__real@3e428f5c (0000)
0030D588 0bf8:
	_render_shadows (0000)
004C0078 047e:
	_bss_004c0078 (0000)
	_debug_inactive_objects (047c)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "cseries/profile.h"
#include "memory/data.h"
#include "math/real_math.h"
#include "bitmaps/bitmaps.h"
#include "render/render.h"
#include "render/render_objects.h"
#include "render/render_cameras_internal.h"
#include "render/render_debug.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "objects/object_lights.h"
#include "objects/object_lights_rendering.h"
#include "objects/widgets/widgets.h"
#include "units/units.h"
#include "models/models.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"
#include "game/players.h"
#include "interface/first_person_weapons.h"
#include "camera/director.h"
#include "cutscene/cinematics.h"
#include "tag_files/tag_files.h"
#include "saved games/game_state.h"
#ifdef HALO_LINUX
#include "render_epoch.h"
#endif

/* ---------- constants */

enum
{
#ifdef HALO_LINUX
	/* the native builds' larger render state cache (halo_port_capacity.h) */
	MAXIMUM_CACHED_OBJECT_RENDER_STATES = HALO_PORT_MAXIMUM_CACHED_OBJECT_RENDER_STATES,
#else
	MAXIMUM_CACHED_OBJECT_RENDER_STATES = 256,
#endif
	NUMBER_OF_SHADOW_VOLUME_PLANES = 6,
	MAXIMUM_OBJECT_RENDER_STATE_AGE = 1000,
	OBJECT_RENDER_STATE_LARGE_INTERVAL = 3,
	OBJECT_RENDER_STATE_SMALL_INTERVAL = 10,
};

enum
{
	_render_model_effect_type_none = 0,
	_render_model_effect_type_active_camouflage,
	_render_model_effect_type_modifier,
};

enum
{
	_render_model_shadow_bit = 1,
	_render_model_no_planar_fog_bit,
};

enum
{
	_render_planar_fog_mode_normal = 1,
};

#define OBJECT_RENDER_STATE_LARGE_PIXELS 400.f
#define OBJECT_RENDER_STATE_SMALL_PIXELS 100.f

#define OBJECT_SHADOW_MINIMUM_PIXELS 30.f
#define OBJECT_SHADOW_MINIMUM_DARKNESS 0.19f

#define OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA 0.03f
#define OBJECT_LIGHTING_MAXIMUM_SHADOW_VECTOR_DELTA 0.012f

/* ---------- macros */

#define object_render_state_get(index) \
	((struct object_render_state *)datum_get(cached_object_render_states, (index)))

/* ---------- structures */

struct render_model_effect
{
	short type;
	word pad;
	real intensity;
	real parameter;
	long source_object_index;
	real_point3d source_object_centroid;
	struct shader const *modifier_shader;
	struct render_animation modifier_animation;
};

struct object_render_data
{
	long object_index;
	struct render_lighting *lighting;
	boolean shadow;
	boolean no_planar_fog;
	word pad;
	real_matrix4x3 shadow_matrix;
	real shadow_bounding_radius;
	long unused44;
};

struct object_render_state
{
	struct datum_header header;
	long object_index;
	long refresh_frame_index;
	long render_scene_index;
	long render_frame_index;
	struct render_lighting lighting;
	struct render_lighting desired_lighting;
	real level_of_detail_pixels;
};

#ifdef HALO_LINUX
/* (port) per render state (by its slot): the scene its dynamic lights were
last searched in (HALO_LIGHTING_REFRESH_DIVISOR) and the lights found, as
light datum indices; render-side, never saved. The lights are put into each
scene's rasterizer light indices every scene (object_render_state_refresh):
those are numbered afresh every scene, in the order the lights are
submitted, and an index kept from an earlier scene named whichever light
was submitted there now, or none - the flashlight lit an object one frame
and another light (or none) the next. */
#define DYNAMIC_SCENE_SLOTS 4096
static struct
{
	long scene_index;
	short light_count;
	long light_indices[MAXIMUM_RENDERED_POINT_LIGHTS];
} dynamic_lights[DYNAMIC_SCENE_SLOTS];
#endif

struct render_object_globals
{
	short rendered_object_count;
	word pad;
	long rendered_object_indices[MAXIMUM_RENDERED_OBJECTS];
};

struct rasterizer_debug_options
{
	byte unused00[0xE];
	boolean draw_first_person_weapon_first;
};

typedef char render_model_effect_size_assert[
	sizeof(struct render_model_effect) == 0x28 ? 1 : -1];
typedef char object_render_data_size_assert[
	sizeof(struct object_render_data) == 0x48 ? 1 : -1];
typedef char object_render_state_size_assert[
	sizeof(struct object_render_state) == 0x100 ? 1 : -1];
#ifdef HALO_LINUX
/* the native builds render up to MAXIMUM_RENDERED_OBJECTS (objects.h) */
typedef char render_object_globals_size_assert[
	sizeof(struct render_object_globals) == 4 + MAXIMUM_RENDERED_OBJECTS * sizeof(long) ? 1 : -1];
#else
typedef char render_object_globals_size_assert[
	sizeof(struct render_object_globals) == 0x404 ? 1 : -1];
#endif

/* ---------- prototypes */

static boolean object_is_first_person_camera(
	long object_index);
static void find_rendered_objects(
	void);
#ifdef HALO_LINUX
static void flicker_check_found(void);
#endif
static real object_get_level_of_detail_pixels(
	long object_index);
static void render_object_list(
	struct object_render_data *data,
	struct render_model_effect const *parent_model_effect,
	long object_index);
static void interpolate_real_rgb_color(
	union real_rgb_color *color,
	union real_rgb_color const *desired_color,
	real maximum_delta);
static void interpolate_real_argb_color(
	union real_argb_color *color,
	union real_argb_color const *desired_color,
	real maximum_delta);
static void interpolate_normal(
	real_vector3d *normal,
	real_vector3d const *desired_normal,
	real maximum_delta);
static boolean render_object_shadow_begin(
	struct object_render_data *data,
	real level_of_detail);
static void shadow_volume_plane_pair(
	real_vector3d const *normal,
	real_point3d const *point,
	real_plane3d *plane,
	real_plane3d *opposite_plane);
static void render_object_shadow_end(
	struct object_render_data *data);
static void object_render_state_refresh(
	long render_state_index,
	long object_index,
	real level_of_detail_pixels,
	boolean rebuild);
static long object_get_cached_render_state(
	long object_index,
	real level_of_detail_pixels);
static void render_object(
	struct object_render_data *data);
static void process_rendered_objects(
	struct object_render_data *data);

boolean scripted_camera_object_is_first_person_camera(
	long object_index);
short structure_visibility_find_objects(
	long *object_indices,
	short maximum_object_count,
	long (*get_first_object_function)(long *reference_index, short cluster_index),
	long (*get_next_object_function)(long *reference_index),
	void (*get_bounding_sphere_function)(long object_index, real_point3d *center, real *radius),
	boolean (*unmarked_function)(long object_index),
	boolean (*mark_function)(long object_index));
void rasterizer_models_begin(
	boolean sky);
void rasterizer_models_end(
	void);
void rasterizer_environment_shadows_begin(
	void);
void rasterizer_environment_shadows_end(
	void);
boolean rasterizer_environment_shadow_begin(
	long object_index,
	real_matrix4x3 const *shadow_matrix,
	union real_rgb_color const *shadow_color,
	real object_bounding_radius,
	real *shadow_bounding_radius);
void rasterizer_environment_shadow_end(
	void);
boolean editor_preprocess_rendered_object(
	long object_index,
	struct render_lighting const *lighting);

extern struct rasterizer_debug_options rasterizer_debug_options;
extern boolean debug_objects;
extern short debug_rasterizer_light_count;

/* ---------- globals */

boolean render_shadows = TRUE;

static struct profile_section render_objects_section =
	{ "render_objects", NONE, TRUE };
static struct profile_section render_object_shadows_section =
	{ "render_object_shadows", NONE, TRUE };

struct data_array *cached_object_render_states;

static struct render_object_globals render_object_globals = { 0 };

boolean debug_inactive_objects = FALSE;

static boolean reported_rendered_object_overflow = FALSE;

/* ---------- public code */

void render_objects_initialize(
	void)
{
	cached_object_render_states = game_state_data_new(
		"cached object render states",
		MAXIMUM_CACHED_OBJECT_RENDER_STATES,
		sizeof(struct object_render_state));
	match_assert("c:\\halo\\SOURCE\\render\\render_objects.c", 125, cached_object_render_states);
#ifdef HALO_LINUX
	/* (the render's alone: the tick only clears an object's index into it) */
	halo_epoch_reader_owned(cached_object_render_states);
#endif

	return;
}

void render_objects_initialize_for_new_map(
	void)
{
	data_make_valid(cached_object_render_states);

	return;
}

void render_objects_dispose_from_old_map(
	void)
{
	if (cached_object_render_states && cached_object_render_states->valid)
	{
		data_make_invalid(cached_object_render_states);
	}

	return;
}

void render_objects_dispose(
	void)
{
	cached_object_render_states = NULL;

	return;
}

struct render_lighting *object_get_cached_render_lighting(
	long object_index,
	real level_of_detail_pixels)
{
	static struct render_lighting lighting_storage;
	long render_state_index = object_get_cached_render_state(
		object_index,
		level_of_detail_pixels);

	if (render_state_index != NONE)
	{
		return &object_render_state_get(render_state_index)->lighting;
	}

	lights_prepare_for_object_static(object_index, &lighting_storage);
	lights_prepare_for_object_dynamic(object_index, &lighting_storage);

	return &lighting_storage;
}

#ifdef HALO_LINUX
/* HALO_RENDER_PROFILE=1: inside render_objects (render.c times the phases),
in the frames fine_profile.h picks (HALO_PROFILE_SAMPLE) */
#include <stdlib.h>
#include "fine_profile.h"
void platform_log(const char *format, ...);
void halo_render_model_profile_report(unsigned long frames);
enum
{
	_objects_profile_find,
	_objects_profile_first_person,
	_objects_profile_objects,
	_objects_profile_refresh,
	_objects_profile_models,
	_objects_profile_shadow_models,
	_objects_profile_lighting,
	_objects_profile_widgets,
	_objects_profile_shadow_pass,
	NUMBER_OF_OBJECTS_PROFILE_SLOTS
};
static unsigned long long objects_profile_us[NUMBER_OF_OBJECTS_PROFILE_SLOTS];
static unsigned long objects_profile_frames, objects_profile_objects, objects_profile_models, objects_profile_shadow_models;
static int objects_profile_enabled = -1;
#define OBJECTS_PROFILE_ON() (objects_profile_enabled > 0 && halo_fine_render_on)
#define OBJECTS_PROFILE_ADD(slot, before) do { if (OBJECTS_PROFILE_ON()) objects_profile_us[slot] += halo_fine_render_now() - (before); } while (0)
static unsigned long long objects_profile_now(void) { return OBJECTS_PROFILE_ON() ? halo_fine_render_now() : 0; }
static unsigned long long objects_refresh_before;
#define OBJECTS_REFRESH_BEGIN() (objects_refresh_before = objects_profile_now())
#define OBJECTS_REFRESH_END() OBJECTS_PROFILE_ADD(_objects_profile_refresh, objects_refresh_before)
#else
#define OBJECTS_REFRESH_BEGIN() ((void)0)
#define OBJECTS_REFRESH_END() ((void)0)
#endif

void render_objects(
	void)
{
	struct object_render_data data;
	boolean first_person_pass = FALSE;
#ifdef HALO_LINUX
	unsigned long long before;

	if (objects_profile_enabled < 0) { const char *e = getenv("HALO_RENDER_PROFILE"); objects_profile_enabled = e && atoi(e) != 0; }
#endif

	profile_enter(render_objects_section);

	rasterizer_models_begin(FALSE);
#ifdef HALO_LINUX
	before = objects_profile_now();
	find_rendered_objects();
	flicker_check_found();
	OBJECTS_PROFILE_ADD(_objects_profile_find, before);
	if (OBJECTS_PROFILE_ON())
		objects_profile_objects += render_object_globals.rendered_object_count;
#else
	find_rendered_objects();
#endif

	data.shadow = FALSE;

	do
	{
		if (first_person_pass != rasterizer_debug_options.draw_first_person_weapon_first)
		{
#ifdef HALO_LINUX
			before = objects_profile_now();
			first_person_weapon_draw();
			OBJECTS_PROFILE_ADD(_objects_profile_first_person, before);
#else
			first_person_weapon_draw();
#endif
		}
		else
		{
#ifdef HALO_LINUX
			before = objects_profile_now();
			process_rendered_objects(&data);
			OBJECTS_PROFILE_ADD(_objects_profile_objects, before);
#else
			process_rendered_objects(&data);
#endif
		}

		first_person_pass = !first_person_pass;
	}
	while (first_person_pass);

	rasterizer_models_end();
#ifdef HALO_LINUX
	if (objects_profile_enabled > 0 && ++objects_profile_frames % 300 == 0)
	{
		/* (per timed frame; "walk" is the objects pass less its models,
		lighting and widgets: the object list, LOD and effects) */
		static struct halo_fine_mark mark;
		unsigned long timed = halo_fine_render_since(&mark);
		double per = timed ? 1.0 / (timed * 1000.0) : 0.0;
		long long walk = (long long)objects_profile_us[_objects_profile_objects] - (long long)objects_profile_us[_objects_profile_models] -
			(long long)objects_profile_us[_objects_profile_lighting] - (long long)objects_profile_us[_objects_profile_widgets];

		platform_log("objects-profile (ms/frame): find %.2f first_person %.2f objects %.2f (models %.2f, lighting %.2f, widgets %.2f, walk %.2f; light refresh in both passes %.2f) | shadow pass %.2f (models %.2f) | %.1f objects, %.1f models, %.1f shadow models a frame | %s",
			objects_profile_us[_objects_profile_find] * per, objects_profile_us[_objects_profile_first_person] * per,
			objects_profile_us[_objects_profile_objects] * per, objects_profile_us[_objects_profile_models] * per,
			objects_profile_us[_objects_profile_lighting] * per, objects_profile_us[_objects_profile_widgets] * per, walk * per,
			objects_profile_us[_objects_profile_refresh] * per,
			objects_profile_us[_objects_profile_shadow_pass] * per, objects_profile_us[_objects_profile_shadow_models] * per,
			timed ? (double)objects_profile_objects / timed : 0.0, timed ? (double)objects_profile_models / timed : 0.0,
			timed ? (double)objects_profile_shadow_models / timed : 0.0, mark.note);
		memset(objects_profile_us, 0, sizeof(objects_profile_us));
		halo_render_model_profile_report(timed);
		objects_profile_objects = objects_profile_models = objects_profile_shadow_models = 0;
	}
#endif

	profile_exit(render_objects_section);

	return;
}

void render_object_shadows(
	void)
{
	profile_enter(render_object_shadows_section);

	if (render_shadows)
	{
		struct object_render_data data;

		rasterizer_environment_shadows_begin();

		data.shadow = TRUE;
#ifdef HALO_LINUX
		{
			unsigned long long shadow_before = objects_profile_now();

			process_rendered_objects(&data);
			OBJECTS_PROFILE_ADD(_objects_profile_shadow_pass, shadow_before);
		}
#else
		process_rendered_objects(&data);
#endif

		rasterizer_environment_shadows_end();
	}

	profile_exit(render_object_shadows_section);

	return;
}

/* ---------- private code */

static boolean object_is_first_person_camera(
	long object_index)
{
	long unit_index = (local_player_get_player_index(render.local_player_index) == NONE)
		? NONE
		: player_get(local_player_get_player_index(render.local_player_index))->unit_index;

	return (unit_index == object_index &&
		director_get_perspective(render.local_player_index) ==
			_director_perspective_first_person) ||
		scripted_camera_object_is_first_person_camera(object_index);
}

#ifdef HALO_LINUX
/* (port) with the tick on its thread, the clusters' object lists as the
finished tick left them rather than the running tick's, which has every
moving object out of its clusters for a moment each tick
(render_interpolation.c, "the threaded tick's cluster lists") */
static long render_cluster_get_first_collideable_object(long *iterator, short cluster_index)
{
	return render_tick_cluster_list_first(_tick_cluster_list_collideable, iterator, cluster_index);
}

static long render_cluster_get_next_collideable_object(long *iterator)
{
	return render_tick_cluster_list_next(_tick_cluster_list_collideable, iterator);
}

static long render_cluster_get_first_noncollideable_object(long *iterator, short cluster_index)
{
	return render_tick_cluster_list_first(_tick_cluster_list_noncollideable, iterator, cluster_index);
}

static long render_cluster_get_next_noncollideable_object(long *iterator)
{
	return render_tick_cluster_list_next(_tick_cluster_list_noncollideable, iterator);
}
#endif

static void find_rendered_objects(
	void)
{
	long (*get_first_collideable)(long *, short) = cluster_get_first_collideable_object;
	long (*get_next_collideable)(long *) = cluster_get_next_collideable_object;
	long (*get_first_noncollideable)(long *, short) = cluster_get_first_noncollideable_object;
	long (*get_next_noncollideable)(long *) = cluster_get_next_noncollideable_object;

#ifdef HALO_LINUX
	if (render_tick_cluster_lists_active(_tick_cluster_list_collideable) &&
		render_tick_cluster_lists_active(_tick_cluster_list_noncollideable))
	{
		get_first_collideable = render_cluster_get_first_collideable_object;
		get_next_collideable = render_cluster_get_next_collideable_object;
		get_first_noncollideable = render_cluster_get_first_noncollideable_object;
		get_next_noncollideable = render_cluster_get_next_noncollideable_object;
	}
#endif
	object_marker_begin();

	render_object_globals.rendered_object_count = structure_visibility_find_objects(
		render_object_globals.rendered_object_indices,
		MAXIMUM_RENDERED_OBJECTS,
		get_first_collideable,
		get_next_collideable,
		object_get_render_bounding_sphere,
		object_unmarked_function,
		object_mark_function);

	render_object_globals.rendered_object_count += structure_visibility_find_objects(
		&render_object_globals.rendered_object_indices[
			render_object_globals.rendered_object_count],
		MAXIMUM_RENDERED_OBJECTS - render_object_globals.rendered_object_count,
		get_first_noncollideable,
		get_next_noncollideable,
		object_get_render_bounding_sphere,
		object_unmarked_function,
		object_mark_function);

	object_marker_end();

	if (render_object_globals.rendered_object_count == MAXIMUM_RENDERED_OBJECTS &&
		!reported_rendered_object_overflow)
	{
		error(_error_silent, "MAXIMUM_RENDERED_OBJECTS exceeded.");
		reported_rendered_object_overflow = TRUE;
	}

	return;
}

#ifdef HALO_LINUX
/* (debug) HALO_FLICKER_LOG=1: an object the render found in the frames on
either side of one but not in it, or drew (render_model) on either side but
not in it, is logged with what the frame in between saw of it: whether it
still existed, its map connection and clusters, whether its clusters' lists
held it, its sphere against the view - to tell a one-frame flicker's cause */
static int flicker_log_enabled = -1;
static unsigned long flicker_frame;
static unsigned long *flicker_found_frame, *flicker_drawn_frame;
static short *flicker_identifier;
static char (*flicker_why)[200];
static char (*flicker_why_drawn)[200];

static boolean flicker_log_wanted(void)
{
	if (flicker_log_enabled < 0)
	{
		const char *setting = getenv("HALO_FLICKER_LOG");

		flicker_log_enabled = setting && atoi(setting) != 0;
		if (flicker_log_enabled)
		{
			flicker_found_frame = calloc(MAXIMUM_OBJECTS_PER_MAP, sizeof(*flicker_found_frame));
			flicker_drawn_frame = calloc(MAXIMUM_OBJECTS_PER_MAP, sizeof(*flicker_drawn_frame));
			flicker_identifier = calloc(MAXIMUM_OBJECTS_PER_MAP, sizeof(*flicker_identifier));
			flicker_why = calloc(MAXIMUM_OBJECTS_PER_MAP, sizeof(*flicker_why));
			flicker_why_drawn = calloc(MAXIMUM_OBJECTS_PER_MAP, sizeof(*flicker_why_drawn));
			if (!flicker_found_frame || !flicker_drawn_frame || !flicker_identifier || !flicker_why || !flicker_why_drawn)
				flicker_log_enabled = 0;
		}
	}
	return flicker_log_enabled > 0 && render.window_index == 0 && !render.camera.mirrored;
}

static void flicker_describe(long object_index, char *why, size_t size)
{
	struct object_datum *object = object_try_and_get(object_index);
	struct object_header_datum *header = object_header_try_and_get(object_index);
	int listed = 0, rendered_clusters = 0, clusters = 0;
	long reference;
	short cluster_index;
	struct cluster_partition *partition;
	boolean visible;

	if (!object || !header)
	{
		snprintf(why, size, "gone");
		return;
	}
	partition = TEST_FLAG(object->object.flags, _object_has_collision_model_bit) ?
		&collideable_object_cluster_partition : &noncollideable_object_cluster_partition;
	for (cluster_index = (short)cluster_partition_get_first_cluster(partition, &reference, object->object.first_cluster_reference_index);
		cluster_index != NONE;
		cluster_index = (short)cluster_partition_get_next_cluster(partition, &reference))
	{
		long rendered_index;

		clusters++;
		for (rendered_index = 0; rendered_index < render.rendered_cluster_count; rendered_index++)
		{
			if (rendered_cluster_get(rendered_index)->cluster_index == cluster_index)
			{
				long iterator, datum;

				rendered_clusters++;
				for (datum = cluster_partition_get_first_datum(partition, &iterator, cluster_index); datum != NONE;
					datum = cluster_partition_get_next_datum(partition, &iterator))
				{
					if (datum == object_index)
						listed++;
				}
			}
		}
	}
	visible = render_frustum_sphere_visible(&render.frustum, &object->object.bounding_sphere_center,
		object->object.bounding_sphere_radius);
	snprintf(why, size, "type %d parent %08lx flags %08lx header %02x connected %d clusters %d rendered %d listed %d visible %d invisible %d",
		object->object.type, (unsigned long)object->object.parent_object_index, (unsigned long)object->object.flags,
		header->flags, TEST_FLAG(object->object.flags, _object_connected_to_map_bit) ? 1 : 0,
		clusters, rendered_clusters, listed, visible ? 1 : 0, TEST_FLAG(object->object.flags, _object_invisible_bit) ? 1 : 0);
}

/* after find_rendered_objects: the found list against the last two frames */
static void flicker_check_found(void)
{
	short index;
	struct object_iterator iterator;
	struct object_datum *object;

	if (!flicker_log_wanted())
		return;
	flicker_frame++;
	for (index = 0; index < render_object_globals.rendered_object_count; index++)
	{
		long object_index = render_object_globals.rendered_object_indices[index];
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

		if (absolute < 0 || absolute >= MAXIMUM_OBJECTS_PER_MAP)
			continue;
		if (flicker_identifier[absolute] == DATUM_INDEX_TO_IDENTIFIER(object_index) &&
			flicker_found_frame[absolute] == flicker_frame - 2)
		{
			object = object_try_and_get(object_index);
			platform_log("flicker: frame %lu: object %08lx (%s) not found in the frame before: %s",
				flicker_frame, (unsigned long)object_index,
				object ? tag_name_strip_path(tag_get_name(object->definition_index)) : "?", flicker_why[absolute]);
		}
		flicker_identifier[absolute] = (short)DATUM_INDEX_TO_IDENTIFIER(object_index);
		flicker_found_frame[absolute] = flicker_frame;
	}
	/* (found last frame, not now: what this frame sees of it) */
	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.index);

		if (absolute < MAXIMUM_OBJECTS_PER_MAP && flicker_found_frame[absolute] == flicker_frame - 1 &&
			flicker_identifier[absolute] == DATUM_INDEX_TO_IDENTIFIER(iterator.index))
		{
			flicker_describe(iterator.index, flicker_why[absolute], sizeof(flicker_why[absolute]));
		}
	}
}

/* render_object_list: whether render_model drew the object, and why not */
static void flicker_note_drawn(long object_index, boolean drawn, real pixels, real minimum)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

	if (!flicker_log_wanted() || absolute < 0 || absolute >= MAXIMUM_OBJECTS_PER_MAP)
		return;
	if (drawn)
	{
		if (flicker_drawn_frame[absolute] == flicker_frame - 2 && flicker_identifier[absolute] == DATUM_INDEX_TO_IDENTIFIER(object_index))
		{
			struct object_datum *object = object_try_and_get(object_index);

			platform_log("flicker: frame %lu: object %08lx (%s) found but not drawn in the frame before: %s",
				flicker_frame, (unsigned long)object_index,
				object ? tag_name_strip_path(tag_get_name(object->definition_index)) : "?", flicker_why_drawn[absolute]);
		}
		flicker_drawn_frame[absolute] = flicker_frame;
	}
	else
		snprintf(flicker_why_drawn[absolute], sizeof(flicker_why_drawn[absolute]), "%.1f pixels, minimum %.1f", pixels, minimum);
}

/* (port) an object's bounding sphere where it is drawn: with the tick on its
thread, as its pose has it (port/linux/game/render_interpolation.c), so the
detail level, the shadow and the centroid follow the drawn object rather
than the tick running alongside */
static void render_object_get_bounding_sphere(
	long object_index,
	real_point3d *center,
	real *radius)
{
	if (!render_tick_pose_bounding_sphere(object_index, center, radius))
	{
		object_get_bounding_sphere(object_index, center, radius);
	}
}
#endif

static real object_get_level_of_detail_pixels(
	long object_index)
{
	real_point3d center;
	real radius;

	if (cinematic_in_progress() &&
		TEST_FLAG((object_try_and_get(object_index) ? object_try_and_get(object_index)->object.flags : 0), _object_movie_star_bit))
	{
		return REAL_MAX;
	}

#ifdef HALO_LINUX
	render_object_get_bounding_sphere(object_index, &center, &radius);
#else
	object_get_bounding_sphere(object_index, &center, &radius);
#endif

	return render_frustum_sphere_diameter_in_pixels(&render.frustum, &center, radius);
}

static void render_object_list(
	struct object_render_data *data,
	struct render_model_effect const *parent_model_effect,
	long object_index)
{
	while (object_index != NONE)
	{
#ifdef HALO_LINUX
		/* (port) the child and sibling links are the tick's; a link to an
		object gone since ends the walk this frame (render_epoch.h) */
		struct object_datum *object = object_try_and_get(object_index);

		if (!object)
			break;
#else
		struct object_datum *object = object_get(object_index);
#endif

		if (!object_is_first_person_camera(object_index) || render.camera.mirrored)
		{
			struct render_model_effect model_effect;

			if (!data->shadow)
			{
				match_assert(
					"c:\\halo\\SOURCE\\render\\render_objects.c",
					390,
					parent_model_effect);

				model_effect = *parent_model_effect;

				/* a modifier effect is not inherited by an object's children */
				if (parent_model_effect->type == _render_model_effect_type_modifier)
				{
					model_effect.type = _render_model_effect_type_none;
					model_effect.modifier_shader = NULL;
					model_effect.modifier_animation.values = NULL;
					model_effect.modifier_animation.colors = NULL;
				}
				else
				{
					/* January copies the parent effect again on this path (a second block copy in
					   both the PC and Xbox builds) */
					model_effect = *parent_model_effect;
				}
			}

			if (!TEST_FLAG(object->object.flags, _object_invisible_bit))
			{
				struct object_definition *definition =
					object_definition_get(object->definition_index);
				real level_of_detail_pixels = object_get_level_of_detail_pixels(object_index);
#ifdef HALO_LINUX
				/* (port) the sphere of the pose drawn (render_object_get_bounding_sphere) */
				real_point3d drawn_center;
				real drawn_radius;

				render_object_get_bounding_sphere(object_index, &drawn_center, &drawn_radius);
#endif

				match_assert(
					"c:\\halo\\SOURCE\\render\\render_objects.c",
					415,
					data->lighting);

				if (!data->shadow)
				{
					if (definition->object.modifier_shader.index != NONE)
					{
						model_effect.modifier_shader = shader_definition_get(
							definition->object.modifier_shader.index);
						if (shader_type_is_valid_for_modifier(
							model_effect.modifier_shader->base.type))
						{
							model_effect.modifier_animation.colors =
								object->object.outgoing_change_colors;
							model_effect.modifier_animation.values =
								object->object.outgoing_function_values;
						}
						else
						{
							error(
								_error_silent,
								"### ERROR invalid modifier shader",
								model_effect.modifier_shader->base.type);
							model_effect.modifier_shader = NULL;
						}
					}

					if (TEST_FLAG(_object_mask_unit, object->object.type))
					{
						struct unit_datum *unit = unit_get(object_index);

						if (unit->unit.active_camouflage > 0.f)
						{
							model_effect.type =
								_render_model_effect_type_active_camouflage;
							model_effect.source_object_index = object_index;
							model_effect.source_object_centroid =
								object->object.bounding_sphere_center;
							model_effect.intensity = unit->unit.active_camouflage;
							model_effect.parameter =
								unit->unit.active_camouflage_super_amount;
						}
					}

					if (TEST_FLAG(
						definition->object.flags,
						_object_transparency_self_occludes_bit))
					{
						model_effect.type = _render_model_effect_type_modifier;
						model_effect.source_object_index = object_index;
						model_effect.source_object_centroid =
							object->object.bounding_sphere_center;
					}

					{
						struct object_header_datum *object_header =
							object_header_get(object_index);
						char name[512];
						real_point3d text_point;

						if (debug_inactive_objects &&
							object->object.parent_object_index == NONE &&
							!TEST_FLAG(object_header->flags, _object_header_active_bit) &&
							definition->object.model.index != NONE)
						{
							/* point_from_line3d(center, global_up3d, 0.2f, &text_point), expanded so this
							   object does not emit the real_math.h point_from_line3d COMDAT */
							real_point3d const *center = &object->object.bounding_sphere_center;

							text_point.x = global_up3d->i * 0.2f + center->x;
							text_point.y = global_up3d->j * 0.2f + center->y;
							text_point.z = global_up3d->k * 0.2f + center->z;

							sprintf(
								name,
								"inactive %s",
								tag_name_strip_path(tag_get_name(object->definition_index)));

							render_debug_point(
								FALSE,
								center,
								object->object.bounding_sphere_radius,
								global_real_argb_blue);
							render_debug_string_at_point(
								FALSE,
								&text_point,
								name,
								global_real_argb_blue);
						}
					}

					{
					unsigned long long model_before = objects_profile_now();
					/* (port) HALO_MIN_OBJECT_PIXELS=n (a handheld quality setting):
					an object whose bounding sphere spans fewer pixels than n is
					not drawn (its children, lights and effects still are) */
					static float minimum_pixels = -1.0f;
					static unsigned long settings_seen;
					extern volatile unsigned long halo_settings_generation;

					if (minimum_pixels < 0.0f || settings_seen != halo_settings_generation)
					{
						settings_seen = halo_settings_generation;
						const char *setting = getenv("HALO_MIN_OBJECT_PIXELS");
						minimum_pixels = setting ? (float)atof(setting) : 0.0f;
					}
					flicker_note_drawn(object_index, level_of_detail_pixels >= minimum_pixels, level_of_detail_pixels, minimum_pixels);
					if (level_of_detail_pixels >= minimum_pixels && OBJECTS_PROFILE_ON())
						objects_profile_models++;
					if (level_of_detail_pixels >= minimum_pixels)
					render_model(
						definition->object.model.index,
						level_of_detail_pixels,
						object_get_node_matrices(object_index),
						object->object.region_permutations,
						object->object.outgoing_change_colors,
						object->object.outgoing_function_values,
						data->lighting,
#ifdef HALO_LINUX
						&drawn_center,
						drawn_radius,
#else
						&object->object.bounding_sphere_center,
						object->object.bounding_sphere_radius,
#endif
						&model_effect,
						object_index,
						object->object.forced_shader_permutation_index,
						data->no_planar_fog ? FLAG(_render_model_no_planar_fog_bit) : 0);
					OBJECTS_PROFILE_ADD(_objects_profile_models, model_before);
					}

					if (debug_objects)
					{
						object_type_render_debug(object_index);
					}
				}
				else
				{
					{
					unsigned long long model_before = objects_profile_now();
					render_model(
						definition->object.model.index,
						level_of_detail_pixels * 0.3f,
						object_get_node_matrices(object_index),
						object->object.region_permutations,
						object->object.outgoing_change_colors,
						object->object.outgoing_function_values,
						data->lighting,
#ifdef HALO_LINUX
						&drawn_center,
						drawn_radius,
#else
						&object->object.bounding_sphere_center,
						object->object.bounding_sphere_radius,
#endif
						NULL,
						object_index,
						object->object.forced_shader_permutation_index,
						FLAG(_render_model_shadow_bit));
					OBJECTS_PROFILE_ADD(_objects_profile_shadow_models, model_before);
					if (OBJECTS_PROFILE_ON())
						objects_profile_shadow_models++;
					}
				}
			}

			if (!data->shadow && object->object.first_widget_index != NONE)
			{
				struct render_animation animation;

				animation.colors = object->object.outgoing_change_colors;
				animation.values = object->object.outgoing_function_values;
#ifdef HALO_LINUX
				{
					unsigned long long widgets_before = objects_profile_now();

					widgets_render(object_index, data->lighting, &animation);
					OBJECTS_PROFILE_ADD(_objects_profile_widgets, widgets_before);
				}
#else
				widgets_render(object_index, data->lighting, &animation);
#endif
			}

			if (object->object.first_child_object_index != NONE)
			{
				render_object_list(
					data,
					data->shadow ? NULL : &model_effect,
					object->object.first_child_object_index);
			}
		}

		object_index = object->object.next_object_index;
	}

	return;
}

static void interpolate_real_rgb_color(
	union real_rgb_color *color,
	union real_rgb_color const *desired_color,
	real maximum_delta)
{
	color->red += PIN(
		desired_color->red - color->red,
		-maximum_delta,
		maximum_delta);

	color->green += PIN(
		desired_color->green - color->green,
		-maximum_delta,
		maximum_delta);

	color->blue += PIN(
		desired_color->blue - color->blue,
		-maximum_delta,
		maximum_delta);

	return;
}

static void interpolate_real_argb_color(
	union real_argb_color *color,
	union real_argb_color const *desired_color,
	real maximum_delta)
{
	color->alpha += PIN(
		desired_color->alpha - color->alpha,
		-maximum_delta,
		maximum_delta);

	color->red += PIN(
		desired_color->red - color->red,
		-maximum_delta,
		maximum_delta);

	color->green += PIN(
		desired_color->green - color->green,
		-maximum_delta,
		maximum_delta);

	color->blue += PIN(
		desired_color->blue - color->blue,
		-maximum_delta,
		maximum_delta);

	return;
}

static void interpolate_normal(
	real_vector3d *normal,
	real_vector3d const *desired_normal,
	real maximum_delta)
{
	normal->i += PIN(
		desired_normal->i - normal->i,
		-maximum_delta,
		maximum_delta);

	normal->j += PIN(
		desired_normal->j - normal->j,
		-maximum_delta,
		maximum_delta);

	normal->k += PIN(
		desired_normal->k - normal->k,
		-maximum_delta,
		maximum_delta);

	normalize3d(normal);

	return;
}

static boolean render_object_shadow_begin(
	struct object_render_data *data,
	real level_of_detail)
{
	real_point3d center;
	real radius;
	real_vector3d shadow_right;
	union real_rgb_color shadow_color;
	struct object_datum *object;
	real shadow_intensity = level_of_detail;

#ifdef HALO_LINUX
	render_object_get_bounding_sphere(data->object_index, &center, &radius);
#else
	object_get_bounding_sphere(data->object_index, &center, &radius);
#endif

	perpendicular3d(&data->lighting->shadow_vector, &shadow_right);
	normalize3d(&shadow_right);
	matrix4x3_from_point_and_vectors(
		&data->shadow_matrix,
		&center,
		&shadow_right,
		&data->lighting->shadow_vector);

	shadow_color = data->lighting->shadow_color;

	object = object_get(data->object_index);
	if (TEST_FLAG(_object_mask_unit, object->object.type))
	{
		struct unit_datum *unit = unit_get(data->object_index);

		if (unit->unit.active_camouflage > 0.f)
		{
			shadow_intensity = (1.f - unit->unit.active_camouflage) * level_of_detail;
		}
	}

	shadow_color.red = shadow_color.red * shadow_intensity + (1.f - shadow_intensity);
	shadow_color.green = shadow_color.green * shadow_intensity + (1.f - shadow_intensity);
	shadow_color.blue = shadow_color.blue * shadow_intensity + (1.f - shadow_intensity);

	return rasterizer_environment_shadow_begin(
		data->object_index,
		&data->shadow_matrix,
		&shadow_color,
		radius,
		&data->shadow_bounding_radius);
}

static void shadow_volume_plane_pair(
	real_vector3d const *normal,
	real_point3d const *point,
	real_plane3d *plane,
	real_plane3d *opposite_plane)
{
	plane->n = *normal;
	plane->d = dot_product3d(normal, (real_vector3d const *)point);
	negate_vector3d(&plane->n, &opposite_plane->n);
	opposite_plane->d = -plane->d;

	return;
}

static void render_object_shadow_end(
	struct object_render_data *data)
{
	real_plane3d shadow_volume_planes[NUMBER_OF_SHADOW_VOLUME_PLANES];
	real_rectangle3d shadow_volume_bounds;

	shadow_volume_plane_pair(
		&data->shadow_matrix.up,
		&data->shadow_matrix.position,
		&shadow_volume_planes[0],
		&shadow_volume_planes[1]);
	shadow_volume_planes[0].d -= data->shadow_bounding_radius * 0.5f;
	shadow_volume_planes[1].d -= data->shadow_bounding_radius * 4.f;

	shadow_volume_plane_pair(
		&data->shadow_matrix.forward,
		&data->shadow_matrix.position,
		&shadow_volume_planes[2],
		&shadow_volume_planes[3]);
	shadow_volume_planes[2].d -= data->shadow_bounding_radius;
	shadow_volume_planes[3].d -= data->shadow_bounding_radius;

	shadow_volume_plane_pair(
		&data->shadow_matrix.left,
		&data->shadow_matrix.position,
		&shadow_volume_planes[4],
		&shadow_volume_planes[5]);
	shadow_volume_planes[4].d -= data->shadow_bounding_radius;
	shadow_volume_planes[5].d -= data->shadow_bounding_radius;

	shadow_volume_bounds.x1 =
		ABS(data->shadow_matrix.forward.i) + ABS(data->shadow_matrix.left.i);
	shadow_volume_bounds.x0 = -shadow_volume_bounds.x1;
	shadow_volume_bounds.y1 =
		ABS(data->shadow_matrix.forward.j) + ABS(data->shadow_matrix.left.j);
	shadow_volume_bounds.y0 = -shadow_volume_bounds.y1;
	shadow_volume_bounds.z1 =
		ABS(data->shadow_matrix.forward.k) + ABS(data->shadow_matrix.left.k);
	shadow_volume_bounds.z0 = -shadow_volume_bounds.z1;

	shadow_volume_bounds.x0 += (data->shadow_matrix.up.i <= 0.f)
		? data->shadow_matrix.up.i * 4.f
		: data->shadow_matrix.up.i * -0.5f;
	shadow_volume_bounds.x1 += (data->shadow_matrix.up.i > 0.f)
		? data->shadow_matrix.up.i * 4.f
		: data->shadow_matrix.up.i * -0.5f;
	shadow_volume_bounds.y0 += (data->shadow_matrix.up.j <= 0.f)
		? data->shadow_matrix.up.j * 4.f
		: data->shadow_matrix.up.j * -0.5f;
	shadow_volume_bounds.y1 += (data->shadow_matrix.up.j > 0.f)
		? data->shadow_matrix.up.j * 4.f
		: data->shadow_matrix.up.j * -0.5f;
	shadow_volume_bounds.z0 += data->shadow_matrix.up.k * 4.f;
	shadow_volume_bounds.z1 -= data->shadow_matrix.up.k * 0.5f;

	shadow_volume_bounds.x0 = shadow_volume_bounds.x0 * data->shadow_bounding_radius +
		data->shadow_matrix.position.x;
	shadow_volume_bounds.x1 = shadow_volume_bounds.x1 * data->shadow_bounding_radius +
		data->shadow_matrix.position.x;
	shadow_volume_bounds.y0 = shadow_volume_bounds.y0 * data->shadow_bounding_radius +
		data->shadow_matrix.position.y;
	shadow_volume_bounds.y1 = shadow_volume_bounds.y1 * data->shadow_bounding_radius +
		data->shadow_matrix.position.y;
	shadow_volume_bounds.z0 = shadow_volume_bounds.z0 * data->shadow_bounding_radius +
		data->shadow_matrix.position.z;
	shadow_volume_bounds.z1 = shadow_volume_bounds.z1 * data->shadow_bounding_radius +
		data->shadow_matrix.position.z;

	structure_render_shadow(
		&data->shadow_matrix.position,
		data->shadow_bounding_radius * 4.f,
		&shadow_volume_bounds,
		NUMBER_OF_SHADOW_VOLUME_PLANES,
		shadow_volume_planes);

	rasterizer_environment_shadow_end();

	return;
}

static void object_render_state_refresh(
	long render_state_index,
	long object_index,
	real level_of_detail_pixels,
	boolean rebuild)
{
	struct object_render_state *state = object_render_state_get(render_state_index);
	long scene_age = render.scene_index - state->render_scene_index;
	long render_age = render.frame_index - state->render_frame_index;
#ifdef HALO_LINUX
	/* The native builds draw several frames per tick
	(port/linux/game/render_interpolation.c), and a refresh moves the lighting
	a fixed step toward its target: refresh at the intervals in ticks the
	Xbox refreshed at in frames, so lighting changes as fast as it did. */
	long refresh_age = game_time_get() - state->refresh_frame_index;
#else
	long refresh_age = render.frame_index - state->refresh_frame_index;
#endif
	boolean refresh = FALSE;
#ifdef HALO_LINUX
	/* (port) HALO_LIGHTING_REFRESH_DIVISOR=n (a handheld quality setting):
	an object's lighting is refreshed every n-th time it would have been -
	the static (lightmap) part at n times the intervals, the dynamic point
	light search every n-th scene for objects below the large size - and
	moves toward its target as before; the game state is untouched */
	static long refresh_divisor = -1;
	static unsigned long settings_seen;
	extern volatile unsigned long halo_settings_generation;

	if (refresh_divisor < 0 || settings_seen != halo_settings_generation)
	{
		settings_seen = halo_settings_generation;
		const char *setting = getenv("HALO_LIGHTING_REFRESH_DIVISOR");
		refresh_divisor = setting && atoi(setting) > 1 ? atoi(setting) : 1;
	}
#endif

	if (refresh_age < 0 || scene_age < 0)
	{
		scene_age = 1;
		refresh_age = 1;
	}

	if (TEST_FLAG(object_get(object_index)->object.flags, _object_static_lighting_recompute_bit))
	{
		if (level_of_detail_pixels > OBJECT_RENDER_STATE_LARGE_PIXELS)
		{
#ifdef HALO_LINUX
			refresh = refresh_age >= refresh_divisor;
#else
			refresh = refresh_age > 0;
#endif
		}
		else if (level_of_detail_pixels > OBJECT_RENDER_STATE_SMALL_PIXELS)
		{
#ifdef HALO_LINUX
			refresh = refresh_age > OBJECT_RENDER_STATE_LARGE_INTERVAL * refresh_divisor;
#else
			refresh = refresh_age > OBJECT_RENDER_STATE_LARGE_INTERVAL;
#endif
		}
		else
		{
#ifdef HALO_LINUX
			refresh = refresh_age > OBJECT_RENDER_STATE_SMALL_INTERVAL * refresh_divisor;
#else
			refresh = refresh_age > OBJECT_RENDER_STATE_SMALL_INTERVAL;
#endif
		}
	}

	if (refresh &&
		render_age > 1 &&
		TEST_FLAG(
			object_get(object_index)->object.flags,
			_object_static_lighting_recompute_bit))
	{
		rebuild = TRUE;
	}

	if (rebuild || refresh)
	{
		state->object_index = object_index;
		lights_prepare_for_object_static(object_index, &state->desired_lighting);
		state->level_of_detail_pixels = level_of_detail_pixels;
#ifdef HALO_LINUX
		state->refresh_frame_index = game_time_get();
#else
		state->refresh_frame_index = render.frame_index;
#endif
	}

#ifdef HALO_LINUX
	{
		static int datum_cache = -1;
		long slot = (unsigned long)(render_state_index & 0xffff) % DYNAMIC_SCENE_SLOTS;

		if (datum_cache < 0)
		{
			/* (HALO_LIGHT_DATUM_CACHE=0: the rasterizer indices are kept
			between searches, as in 1.0.3 beta 2 - the A/B switch) */
			const char *setting = getenv("HALO_LIGHT_DATUM_CACHE");

			datum_cache = !setting || atoi(setting) != 0;
		}
		if (rebuild || render.scene_index - dynamic_lights[slot].scene_index >=
			(level_of_detail_pixels > OBJECT_RENDER_STATE_LARGE_PIXELS ? 1 : refresh_divisor) ||
			render.scene_index < dynamic_lights[slot].scene_index)
		{
			if (datum_cache)
				lights_find_for_object_dynamic(object_index, dynamic_lights[slot].light_indices,
					&dynamic_lights[slot].light_count);
			else
				lights_prepare_for_object_dynamic(object_index, &state->desired_lighting);
			dynamic_lights[slot].scene_index = render.scene_index;
		}
		/* (every scene, and after a static refresh: that overwrites the
		whole desired lighting, its point lights with the level's default
		none, and the object went dark until its next search) */
		if (datum_cache)
			lights_translate_for_object_dynamic(dynamic_lights[slot].light_indices,
				dynamic_lights[slot].light_count, &state->desired_lighting);
	}
#else
	if (rebuild || scene_age > 0)
	{
		lights_prepare_for_object_dynamic(object_index, &state->desired_lighting);
	}
#endif

	if (!rebuild && !refresh)
	{
		if (scene_age > 0)
		{
			state->lighting.point_light_count = state->desired_lighting.point_light_count;
			state->lighting.point_light_indices[0] =
				state->desired_lighting.point_light_indices[0];
			state->lighting.point_light_indices[1] =
				state->desired_lighting.point_light_indices[1];
		}
	}
	else if (!rebuild && object_light_interpolate)
	{
		real_vector3d velocity;
		struct render_lighting *lighting = &state->lighting;
		struct render_lighting *desired_lighting = &state->desired_lighting;

		match_assert(
			"c:\\halo\\SOURCE\\render\\render_objects.c",
			635,
			state->desired_lighting.distant_light_count==2);

		object_get_velocities(object_index, &velocity, NULL);
		if (velocity.i != 0.f ||
			velocity.j != 0.f ||
			velocity.k != 0.f ||
			object_try_and_get_and_verify_type(object_index, _object_mask_machine))
		{
			interpolate_real_rgb_color(
				&lighting->ambient_color,
				&desired_lighting->ambient_color,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
			interpolate_real_argb_color(
				&lighting->reflection_tint_color,
				&desired_lighting->reflection_tint_color,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
			interpolate_real_rgb_color(
				&lighting->distant_lights[0].color,
				&desired_lighting->distant_lights[0].color,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
			interpolate_normal(
				&lighting->distant_lights[0].direction,
				&desired_lighting->distant_lights[0].direction,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
			interpolate_real_rgb_color(
				&lighting->distant_lights[1].color,
				&desired_lighting->distant_lights[1].color,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
			interpolate_normal(
				&lighting->distant_lights[1].direction,
				&desired_lighting->distant_lights[1].direction,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
			interpolate_normal(
				&lighting->shadow_vector,
				&desired_lighting->shadow_vector,
				OBJECT_LIGHTING_MAXIMUM_SHADOW_VECTOR_DELTA);
			interpolate_real_rgb_color(
				&lighting->shadow_color,
				&desired_lighting->shadow_color,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
		}

		state->lighting.point_light_count = state->desired_lighting.point_light_count;
		state->lighting.point_light_indices[0] =
			state->desired_lighting.point_light_indices[0];
		state->lighting.point_light_indices[1] =
			state->desired_lighting.point_light_indices[1];
	}
	else
	{
		state->lighting = state->desired_lighting;
	}

	{
		short point_light_index;

		for (point_light_index = 0;
			point_light_index < state->lighting.point_light_count;
			point_light_index++)
		{
#ifdef HALO_LINUX
			/* (port) a cached index into this frame's submitted lights can
			be stale when the tick deleted the light meanwhile
			(render_epoch.h): dropped rather than asserted */
			if (state->lighting.point_light_indices[point_light_index] < 0 ||
				state->lighting.point_light_indices[point_light_index] >= debug_rasterizer_light_count)
			{
				short remaining = point_light_index + 1;

				for (; remaining < state->lighting.point_light_count; remaining++)
					state->lighting.point_light_indices[remaining - 1] = state->lighting.point_light_indices[remaining];
				state->lighting.point_light_count--;
				point_light_index--;
				continue;
			}
#endif
			match_assert(
				"c:\\halo\\SOURCE\\render\\render_objects.c",
				690,
				state->lighting.point_light_indices[point_light_index]>=0 &&
					state->lighting.point_light_indices[point_light_index]<debug_rasterizer_light_count);
		}
	}

	state->render_scene_index = render.scene_index;
	state->render_frame_index = render.frame_index;

	return;
}

static long object_get_cached_render_state(
	long object_index,
	real level_of_detail_pixels)
{
	struct object_datum *object = object_get(object_index);
	long render_state_index = NONE;

	if (object->object.cached_render_state_index != NONE &&
		object_render_state_get(object->object.cached_render_state_index)->object_index ==
			object_index)
	{
		render_state_index = object->object.cached_render_state_index;
	}

	if (render_state_index == NONE)
	{
		render_state_index = datum_new(cached_object_render_states);
		if (render_state_index == NONE)
		{
			real oldest_age = REAL_MIN;
			long index;

			for (index = data_next_index(cached_object_render_states, NONE);
				index != NONE;
				index = data_next_index(cached_object_render_states, index))
			{
				real age = (real)(render.scene_index -
					object_render_state_get(index)->render_scene_index);

				if (age < 0.f)
				{
					age = MAXIMUM_OBJECT_RENDER_STATE_AGE;
				}

				if (age > oldest_age)
				{
					oldest_age = age;
					render_state_index = index;
				}
			}
		}

		if (render_state_index != NONE)
		{
			OBJECTS_REFRESH_BEGIN();
			object_render_state_refresh(
				render_state_index,
				object_index,
				level_of_detail_pixels,
				TRUE);
			OBJECTS_REFRESH_END();
			object->object.cached_render_state_index = render_state_index;
		}
	}
	else
	{
		OBJECTS_REFRESH_BEGIN();
		object_render_state_refresh(
			render_state_index,
			object_index,
			level_of_detail_pixels,
			FALSE);
		OBJECTS_REFRESH_END();
	}

	return render_state_index;
}

static void render_object(
	struct object_render_data *data)
{
	if (data->shadow)
	{
		struct object_datum *object = object_get(data->object_index);

		if (!object_is_first_person_camera(data->object_index) &&
			!TEST_FLAG(object->object.flags, _object_shadowless_bit) &&
			(!TEST_FLAG(object->object.flags, _object_invisible_bit) ||
				object->object.first_child_object_index != NONE))
		{
			real level_of_detail_pixels;
			real shadow_darkness;

			data->lighting = object_get_cached_render_lighting(
				data->object_index,
				object_get_level_of_detail_pixels(data->object_index));

			level_of_detail_pixels = object_get_level_of_detail_pixels(data->object_index);
			shadow_darkness = 1.f - real_rgb_color_brightness(&data->lighting->shadow_color);

			if (level_of_detail_pixels > OBJECT_SHADOW_MINIMUM_PIXELS &&
				shadow_darkness > OBJECT_SHADOW_MINIMUM_DARKNESS)
			{
				real size_fraction =
					(level_of_detail_pixels - OBJECT_SHADOW_MINIMUM_PIXELS) * 0.06666667f;
				real darkness_fraction =
					(shadow_darkness - OBJECT_SHADOW_MINIMUM_DARKNESS) * 9.0909081f;

				size_fraction = PIN(size_fraction, 0.f, 1.f);
				darkness_fraction = PIN(darkness_fraction, 0.f, 1.f);

				if (render_object_shadow_begin(data, darkness_fraction * size_fraction))
				{
					render_object_list(data, NULL, data->object_index);
					render_object_shadow_end(data);
				}
			}
		}
	}
	else
	{
		struct object_datum *object = object_get(data->object_index);
		boolean needs_lighting;

		if (!TEST_FLAG(object->object.flags, _object_invisible_bit) ||
			object->object.first_child_object_index != NONE ||
			widgets_need_lighting(object->object.first_widget_index))
		{
			needs_lighting = TRUE;
		}
		else
		{
			needs_lighting = FALSE;
		}

		if (needs_lighting || object->object.first_widget_index != NONE)
		{
			struct object_definition *definition =
				object_definition_get(object->definition_index);

			if (needs_lighting)
			{
#ifdef HALO_LINUX
				unsigned long long lighting_before = objects_profile_now();
#endif
				data->lighting = object_get_cached_render_lighting(
					data->object_index,
					object_get_level_of_detail_pixels(data->object_index));
#ifdef HALO_LINUX
				OBJECTS_PROFILE_ADD(_objects_profile_lighting, lighting_before);
#endif
			}
			else
			{
				data->lighting = NULL;
			}

			if (editor_preprocess_rendered_object(data->object_index, data->lighting))
			{
				struct render_model_effect model_effect;

				model_effect.type = _render_model_effect_type_none;
				model_effect.modifier_shader = NULL;
				model_effect.modifier_animation.values = NULL;
				model_effect.modifier_animation.colors = NULL;

				data->no_planar_fog =
					render.fog.planar_mode != _render_planar_fog_mode_normal ||
					plane3d_distance_to_point(
						&render.fog.plane,
						&object->object.bounding_sphere_center) >
						definition->object.bounding_radius;

				render_object_list(data, &model_effect, data->object_index);
			}
		}
	}

	return;
}

static void process_rendered_objects(
	struct object_render_data *data)
{
	short rendered_object_index;

	for (rendered_object_index = 0;
		rendered_object_index < render_object_globals.rendered_object_count;
		rendered_object_index++)
	{
		data->object_index =
			render_object_globals.rendered_object_indices[rendered_object_index];
		render_object(data);
	}

	return;
}
