/*
RENDER_SPRITE.C

symbols in this file:
0017C760 0130:
	_code_0017c760 (0000)
0017C890 0070:
	_code_0017c890 (0000)
0017C900 0090:
	_build_sprite_prepare_for_window (0000)
0017C990 0180:
	_code_0017c990 (0000)
0017CB10 00a0:
	_build_sprites_begin (0000)
0017CBB0 0130:
	_build_sprites_end (0000)
0017CCE0 01e0:
	_code_0017cce0 (0000)
0017CEC0 0070:
	_build_sprite_compute_vertex_fade (0000)
0017CF30 0610:
	_build_sprite (0000)
0017D540 0280:
	_build_sprite_rotational (0000)
002A00F4 0004:
	_one_over_full_circle (0000)
002A00F8 001b:
	??_C@_0BL@MGMOMNLP@perpendicular?5to?5direction?$AA@ (0000)
002A0114 0016:
	??_C@_0BG@KINAGGNG@parallel?5to?5direction?$AA@ (0000)
002A012C 000e:
	??_C@_0O@GDBPHPAN@screen?5facing?$AA@ (0000)
002A013C 0019:
	??_C@_0BJ@NOODAMK@?$CBuntransformed_direction?$AA@ (0000)
002A0158 0016:
	??_C@_0BG@PMLDPFAJ@transformed_direction?$AA@ (0000)
002A0170 0013:
	??_C@_0BD@IDBJDPKL@transformed_origin?$AA@ (0000)
002A0184 0015:
	??_C@_0BF@ILAHHFNF@untransformed_origin?$AA@ (0000)
002A019C 0026:
	??_C@_0CG@EFAPFCGE@c?3?2halo?2SOURCE?2render?2render_spr@ (0000)
002A01C4 0022:
	??_C@_0CC@MKHFOGBB@?5?5?5coverage?3?5?$CF?41f?5big?5sprites?3?5?$CF@ (0000)
002A01E8 0031:
	??_C@_0DB@IJJJGION@build_sprite?5failed?5to?5allocate?5@ (0000)
002A021C 0010:
	??_C@_0BA@BMIFFLPI@group?9?$DOvertices?$AA@ (0000)
002A022C 003d:
	??_C@_0DN@EKFLFNFF@a?5build_sprites_begin?5call?5can?5a@ (0000)
002A026C 002c:
	??_C@_0CM@LNKKNFPA@?$CBTEST_FLAG?$CIflags?0?5_build_sprites@ (0000)
002A0298 0041:
	??_C@_0EB@NNMLIIBJ@?$CD?$CD?$CD?5ERROR?5sprites?5rendered?5with?5@ (0000)
002A02DC 0031:
	??_C@_0DB@ILMICBDJ@TEST_FLAG?$CIdata?9?$DOflags?0?5_build_sp@ (0000)
002A0310 003b:
	??_C@_0DL@OIGDNGND@build_sprite?5only?5supports?5norma@ (0000)
002A034C 0024:
	??_C@_0CE@EJDAIAFL@build_sprite?5sprite?5count?5exceed@ (0000)
002A0370 004c:
	??_C@_0EM@DGAJPPIN@the?5bitmap?5group?5?$CFs?5sequence?5?$CFd?5@ (0000)
002A03C0 0068:
	??_C@_0GI@GFCEGJMK@mode?$DN?$DN_build_sprite_normal?5?$HM?$HM?5?$CIu@ (0000)
002A0428 0004:
	__real@3ecf817a (0000)
002A042C 001f:
	??_C@_0BP@HLIODOFJ@untransformed_axis_of_rotation?$AA@ (0000)
0030E778 0018:
	_data_0030e778 (0000)
	_global_sprite_render_orientations_enum (000c)
004C0518 0001:
	_bss_004c0518 (0000)
*/

/* ---------- headers */

#include "cseries/cseries.h"
#include "bitmaps/bitmap_group.h"
#include "cseries/errors.h"
#include "interface/hud_draw.h"
#include "bitmaps/bitmap_color_conversion.h"
#include "math/real_math.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "render/render_cameras_internal.h"
#include "render/render_debug.h"
#include "render/render_sprite.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"

/* ---------- constants */

enum build_sprites_internal_flags
{
	_build_sprites_valid_bit = NUMBER_OF_BUILD_SPRITES_FLAGS,
};

enum
{
	MAXIMUM_BUILD_SPRITE_GROUPS = 8,
};

enum
{
	_shader_effect_uses_nonlinear_tint_bit = 1,
};

enum
{
	_rasterizer_geometry_no_sort_bit = 0,
	_rasterizer_geometry_no_queue_bit,
	_rasterizer_geometry_no_fog_bit,
	_rasterizer_geometry_no_zbuffer_bit,
	_rasterizer_geometry_sky_bit,
	_rasterizer_geometry_viewspace_bit,
	_rasterizer_geometry_atmospheric_fog_but_no_planar_fog_bit,
	_rasterizer_geometry_first_person_bit,
	_rasterizer_geometry_parts_define_local_nodes_bit,
	NUMBER_OF_RASTERIZER_GEOMETRY_FLAGS
};

/* ---------- macros */

/* ---------- structures */

struct build_sprite_vertex
{
	real_point3d point;
	real_point2d texture_coordinates;
	pixel32 color;
};

typedef char build_sprite_vertex_size_assert[
	sizeof(struct build_sprite_vertex) == 0x18 ? 1 : -1];

struct build_sprite_globals_data
{
	boolean initialized;
	boolean debug_flag;
	word pad02;
	real screen_coverage;
	short big_sprite_count;
	word pad0A;
	real screen_area_scale;
	real_vector3d viewer_space_world_up;
	real_vector3d viewer_space_world_forward;
};

typedef char build_sprite_globals_data_size_assert[
	sizeof(struct build_sprite_globals_data) == 0x28 ? 1 : -1];

/* ---------- prototypes */

void *_texture_cache_bitmap_get_hardware_format(
	struct bitmap_data *bitmap,
	boolean block,
	boolean load);

static void build_sprite_transform_origin_and_direction(
	struct build_sprite_data const *data,
	unsigned long flags,
	real_point3d const *untransformed_origin,
	real_vector3d const *untransformed_direction,
	real_point3d *transformed_origin,
	real_vector3d *transformed_direction);
static void build_sprite_compute_scale(
	struct build_sprite_data const *data,
	short mode,
	unsigned long flags,
	real_point3d const *origin,
	struct bitmap_data const *bitmap,
	real *scale);
static short build_sprite_get_group(
	struct build_sprite_data *data,
	struct bitmap_data *bitmap);
static void build_sprite_compute_basis(
	struct build_sprite_data const *data,
	short mode,
	unsigned long flags,
	real_point3d const *origin,
	real_vector3d const *transformed_direction,
	real_matrix4x3 *basis);

/* ---------- globals */

extern boolean debug_sprites;
extern struct build_sprite_globals_data build_sprite_globals;

static char *sprite_render_orientation_names[NUMBER_OF_BUILD_SPRITE_ORIENTATIONS] =
{
	"screen facing",
	"parallel to direction",
	"perpendicular to direction",
};

struct tag_enum_definition global_sprite_render_orientations_enum =
{
	NUMBER_OF_BUILD_SPRITE_ORIENTATIONS,
	sprite_render_orientation_names,
	NULL,
};

real const one_over_full_circle = 1.f / (2.f*_pi);

#ifdef HALO_LINUX
/* (port) Trimmed sprites: a particle's quad cut to the part of its texture
that can colour anything (port/vita/platform/vita_textures.c: the texels
that are not zero in the channels the shader's blend reads, with the
filter's reach at every mip level), as a rectangle or that rectangle with
its corners cut along the diagonals (an octagon, drawn as up to three
quads). The rest of the quad sampled zero alpha (alpha blend) or zero
colour (add, subtract, max) everywhere: those pixels left the framebuffer
as it was, and the GPU shades fewer of them. The smoke and flame sprites of
a firefight are most of the effects' fill (triage/fx-status.md). Only
sprites whose cut quad shades its pixels as the whole quad did: the fog the
vertex program gives the corners linear across the sprite (planar fog is
not: sprite_trim_fog_linear), at least 32 pixels across, not point
sampled. The rest differ from the whole quad's only by the interpolation's
rounding (Vita3K at fixed ticks: under 0.2% of a frame's pixels, nearly all
by 1 of 255; triage/fx2-status.md). HALO_SPRITE_TRIM=0 off, 1 rectangles,
2 octagons (the default); HALO_SPRITE_TRIM_MIN_PIXELS=n the smallest.
Only the Vita's texture translator (vita_textures.c, also the gxm-null
harness's) finds the texels; the OpenGL builds' (xbox_textures.c) answers
that it has none, and their sprites are drawn whole. */
#include <stdlib.h>
struct vita_sprite_texels { int empty; float x0, y0, x1, y1, sum0, sum1, difference0, difference1; };
extern int vita_sprite_texel_bounds_available(void);
extern int vita_sprite_texel_bounds(const void *resource, const float bounds[4], int channels,
	struct vita_sprite_texels *texels, float *width, float *height);

static int sprite_trim_mode = -1;
static real sprite_trim_minimum_pixels;
/* (HALO_EFFECT_STATS) the effects' quad area kept, weighted by screen coverage */
double sprite_trim_area_full, sprite_trim_area_kept;
unsigned long sprite_trim_sprites, sprite_trim_trimmed, sprite_trim_empty, sprite_trim_fogged, sprite_trim_small;

static int sprite_trim_enabled(void)
{
	if (sprite_trim_mode < 0)
	{
		const char *setting = getenv("HALO_SPRITE_TRIM");

		sprite_trim_mode = setting ? atoi(setting) : 2;
		setting = getenv("HALO_SPRITE_TRIM_MIN_PIXELS");
		sprite_trim_minimum_pixels = setting ? (real)atof(setting) : 32.f;
		if (!vita_sprite_texel_bounds_available())
			sprite_trim_mode = 0;
	}
	return sprite_trim_mode;
}

/* the channels whose zero leaves the framebuffer as it was under the
effect shader's blend function (rasterizer_set_framebuffer_blend_function;
the effect combiners multiply the texel by the tint, the fade and the fog,
rasterizer_xbox_transparent_geometry.c): 1 alpha, 2 colour, 3 both, 0 none */
static int sprite_trim_channels(struct shader_effect_definition const *shader)
{
	/* (only an effect shader is drawn with the effect combiners: type 1,
	rasterizer_xbox_transparent_geometry.c) */
	if (!shader || shader->shader.base.type != 1)
		return 0;
	/* point sampled (primary_map_flags bit 0): a pixel next to a texel
	boundary would flip to the other texel on the cut quad's slightly
	different interpolation */
	if (TEST_FLAG(shader->primary_map_flags, 0))
		return 0;
	switch (shader->framebuffer_blend_function)
	{
	case 0: return 1; /* alpha blend: src*a + dst*(1 - a) */
	case 3: /* add: src + dst */
	case 4: /* subtract: dst - src */
	case 6: return 2; /* component max */
	case 7: return 3; /* alpha multiply add: src + dst*(1 - a) */
	default: return 0; /* multiply, double multiply, component min: zero is not neutral */
	}
}

/* a z-sprite (the secondary map anchored to the sprite: a soft particle)
takes its depth texture's coordinates from a stream of fixed corner values,
four a quad (rasterizer_xbox_transparent_geometry.c's texcoord_stream). This
port's pixel programs leave the depth replace out (nv2a_psh_cg.c dot_zw), so
those coordinates colour nothing; a z-sprite is cut to a rectangle only, one
quad a sprite as before, so the stream's quads stay the sprites' */
static boolean sprite_trim_zsprite(struct shader_effect_definition const *shader)
{
	return shader && shader->secondary_map.index != NONE && shader->secondary_map_anchor == 2;
}

/* the polygon (UV, up to 8 points, in order) of the sprite rectangle that
is drawn; 0 points when nothing is */
static short sprite_trim_polygon(
	struct bitmap_group_sprite const *sprite,
	struct vita_sprite_texels const *texels,
	real width,
	real height,
	boolean octagon,
	real_point2d points[8])
{
	real_point2d buffer[2][12];
	short count = 4, plane;
	real u0 = MIN(sprite->bounds.x0, sprite->bounds.x1), u1 = MAX(sprite->bounds.x0, sprite->bounds.x1);
	real v0 = MIN(sprite->bounds.y0, sprite->bounds.y1), v1 = MAX(sprite->bounds.y0, sprite->bounds.y1);
	real x0 = MAX(texels->x0, u0*width), x1 = MIN(texels->x1, u1*width);
	real y0 = MAX(texels->y0, v0*height), y1 = MIN(texels->y1, v1*height);
	real_point2d *in = buffer[0], *out = buffer[1];
	short index;

	if (texels->empty || x0 >= x1 || y0 >= y1)
		return 0;
	/* the original's vertex order: (x0, y1) (x1, y1) (x1, y0) (x0, y0) */
	in[0].x = x0; in[0].y = y1;
	in[1].x = x1; in[1].y = y1;
	in[2].x = x1; in[2].y = y0;
	in[3].x = x0; in[3].y = y0;
	for (plane = 0; octagon && plane < 4; plane++)
	{
		/* a*x + b*y >= c */
		real a = plane < 2 ? 1.f : 1.f, b = plane < 2 ? 1.f : -1.f, c, sign = (plane & 1) ? -1.f : 1.f;
		short out_count = 0;

		c = plane == 0 ? texels->sum0 : plane == 1 ? texels->sum1 : plane == 2 ? texels->difference0 : texels->difference1;
		for (index = 0; index < count; index++)
		{
			real_point2d const *p = &in[index], *q = &in[(index + 1) % count];
			real dp = sign*(a*p->x + b*p->y - c), dq = sign*(a*q->x + b*q->y - c);

			if (dp >= 0.f)
				out[out_count++] = *p;
			if ((dp >= 0.f) != (dq >= 0.f))
			{
				real t = dp/(dp - dq);

				out[out_count].x = p->x + (q->x - p->x)*t;
				out[out_count].y = p->y + (q->y - p->y)*t;
				out_count++;
			}
		}
		count = out_count;
		{
			real_point2d *swap = in; in = out; out = swap;
		}
		if (count < 3)
			return 0;
	}
	if (count > 8)
		count = 8;
	for (index = 0; index < count; index++)
	{
		points[index].x = in[index].x/width;
		points[index].y = in[index].y/height;
	}
	return count;
}

extern struct rasterizer_window_begin_parameters global_window_parameters;

/* the fog the effect vertex shader gives a sprite's corners (the
rasterizer's fog constants, rasterizer_xbox.c): the atmospheric term is
linear in the view depth, clamped to [0, 1]; the planar terms (depth below
the fog plane, view depth over the planar distance) are clamped and
squared. A cut quad interpolates the fog of its own corners, so it shades
as the whole quad did only where those terms are linear across the sprite:
TRUE when each is constant or (the atmospheric one) unclamped at all four
corners. A screen-facing sprite has one view depth; the planar fog's plane
depth still varies across it */
static boolean sprite_trim_fog_linear(
	struct build_sprite_vertex const *corners)
{
	struct render_fog const *fog = &global_window_parameters.fog;
	struct render_camera const *camera = &global_window_parameters.camera;
	real view_distance = dot_product3d((real_vector3d const *)&camera->position, &camera->forward);
	real atmospheric_range = fog->atmospheric_maximum_distance - fog->atmospheric_minimum_distance;
	short below[3] = { 0, 0, 0 }, above[3] = { 0, 0, 0 }, corner;

	for (corner = 0; corner < NUMBER_OF_VERTICES_PER_QUADRILATERAL; corner++)
	{
		real_point3d world;
		real depth, term[3];
		short index;

		matrix4x3_transform_point(&global_window_parameters.frustum.view_to_world, &corners[corner].point, &world);
		depth = dot_product3d((real_vector3d const *)&world, &camera->forward) - view_distance;
		term[0] = atmospheric_range > 0.f ? (depth - fog->atmospheric_minimum_distance)/atmospheric_range : 0.f;
		term[1] = fog->planar_maximum_depth > 0.f ?
			(fog->plane.d - dot_product3d((real_vector3d const *)&world, &fog->plane.n))/fog->planar_maximum_depth : 0.f;
		term[2] = fog->planar_maximum_distance > 0.f ? depth/fog->planar_maximum_distance : 0.f;
		for (index = 0; index < 3; index++)
		{
			if (term[index] <= 0.f)
				below[index]++;
			if (term[index] >= 1.f)
				above[index]++;
		}
	}
	/* atmospheric: no corner clamped, or all on one side */
	if (fog->atmospheric_maximum_density > 0.f &&
		(below[0] || above[0]) && below[0] != NUMBER_OF_VERTICES_PER_QUADRILATERAL &&
		above[0] != NUMBER_OF_VERTICES_PER_QUADRILATERAL)
		return FALSE;
	/* planar (squared): every corner clamped to the same side */
	if (fog->planar_maximum_density > 0.f &&
		((below[1] != NUMBER_OF_VERTICES_PER_QUADRILATERAL && above[1] != NUMBER_OF_VERTICES_PER_QUADRILATERAL) ||
		(below[2] != NUMBER_OF_VERTICES_PER_QUADRILATERAL && above[2] != NUMBER_OF_VERTICES_PER_QUADRILATERAL)))
		return FALSE;
	return TRUE;
}

static real sprite_trim_polygon_area(real_point2d const *points, short count)
{
	real area = 0.f;
	short index;

	for (index = 0; index < count; index++)
		area += points[index].x*points[(index + 1) % count].y - points[(index + 1) % count].x*points[index].y;
	return area < 0.f ? -area/2.f : area/2.f;
}
#endif

/* ---------- public code */

void build_sprites_begin(
	struct build_sprite_data *data,
	short maximum_sprite_count,
	long bitmap_group_index,
	struct shader_effect_definition const *shader,
	unsigned long flags)
{
	match_assert("c:\\halo\\SOURCE\\render\\render_sprite.c", 335, shader);
	match_assert(
		"c:\\halo\\SOURCE\\render\\render_sprite.c",
		336,
		!TEST_FLAG(flags, _build_sprites_valid_bit));

	data->bitmap_group_index = bitmap_group_index;
	data->flags = flags;
	data->shader = shader;
	data->group_count = 0;
	data->sprite_count = 0;
	data->maximum_sprite_count = maximum_sprite_count;
	data->centroid = *global_origin3d;
#ifdef HALO_LINUX
	/* (port) the groups whose vertices have room for trimmed sprites'
	octagons (build_sprite_get_group) */
	data->pad22 = 0;
#endif
	SET_FLAG(data->flags, _build_sprites_valid_bit, TRUE);
	return;
}

void build_sprites_end(
	struct build_sprite_data *data)
{
	real one_over_sprite_count = 1.f / data->sprite_count;
	short group_index;

	match_assert(
		"c:\\halo\\SOURCE\\render\\render_sprite.c",
		358,
		TEST_FLAG(data->flags, _build_sprites_valid_bit));

	data->centroid.x *= one_over_sprite_count;
	data->centroid.y *= one_over_sprite_count;
	data->centroid.z *= one_over_sprite_count;
	matrix4x3_transform_point(&render.frustum.view_to_world, &data->centroid, &data->centroid);

	for (group_index = 0; group_index < data->group_count; group_index++)
	{
		struct build_sprite_group *group = &data->groups[group_index];

		if (group->sprite_count)
		{
			rasterizer_dynamic_vertices_unlock(group->vertex_buffer_index);
			if (TEST_FLAG(data->flags, _build_sprites_screen_space_bit))
			{
				match_vassert(
					"c:\\halo\\SOURCE\\render\\render_sprite.c",
					377,
					FALSE,
					"### ERROR sprites rendered with screen geometry -- tell Bernie!!");
				rasterizer_dynamic_screen_geometry_draw(
					0,
					-NUMBER_OF_VERTICES_PER_QUADRILATERAL,
					group->vertex_buffer_index,
					2 * group->sprite_count);
			}
			else
			{
				unsigned long geometry_flags = FLAG(_rasterizer_geometry_viewspace_bit);

				SET_FLAG(
					geometry_flags,
					_rasterizer_geometry_first_person_bit,
					TEST_FLAG(data->flags, _build_sprites_first_person_bit));
				rasterizer_dynamic_unlit_geometry_draw(
					(struct shader const *)data->shader,
					group->bitmap,
					NULL,
					-NUMBER_OF_VERTICES_PER_QUADRILATERAL,
					group->vertex_buffer_index,
#ifdef HALO_LINUX
					/* (port) and the trimmed sprites' extra quads */
					2 * (group->sprite_count + group->pad0A),
#else
					2 * group->sprite_count,
#endif
					&data->centroid,
					geometry_flags);
			}
			rasterizer_dynamic_vertices_delete(group->vertex_buffer_index);
		}
	}

	SET_FLAG(data->flags, _build_sprites_valid_bit, FALSE);
	return;
}

void build_sprite_prepare_for_window(void)
{
	char string[512];

	if (debug_sprites)
	{
		sprintf(
			string,
			"   coverage: %.1f big sprites: %d",
			build_sprite_globals.screen_coverage,
			build_sprite_globals.big_sprite_count);
		render_debug_string(FALSE, string);
	}

	build_sprite_globals.screen_coverage = 0.f;
	build_sprite_globals.big_sprite_count = 0;
	matrix4x3_transform_normal(
		&render.frustum.world_to_view,
		global_up3d,
		&build_sprite_globals.viewer_space_world_up);
	matrix4x3_transform_normal(
		&render.frustum.world_to_view,
		global_left3d,
		&build_sprite_globals.viewer_space_world_forward);
	return;
}

real build_sprite_compute_vertex_fade(
	short fade_mode,
	real_point3d const *viewer_space_point,
	real_vector3d const *viewer_space_normal)
{
	real fade = 1.f;

	if (fade_mode)
	{
		fade = fabs(dot_product3d(viewer_space_normal, (real_vector3d const *)viewer_space_point) /
			magnitude3d((real_vector3d const *)viewer_space_point));

		if (fade_mode == 2)
			fade = 1.f - fade;
	}

	return fade;
}

void build_sprite(
	struct build_sprite_data *data,
	short mode,
	short sequence_index,
	short sprite_index,
	real_point3d const *untransformed_origin,
	real_vector3d const *untransformed_direction,
	real rotation,
	real scale,
	real_argb_color const *color,
	real fade,
	unsigned long flags)
{
	struct bitmap_group *bitmap_group = bitmap_group_get(data->bitmap_group_index);

	match_assert("c:\\halo\\SOURCE\\render\\render_sprite.c", 418, untransformed_origin);
	match_assert(
		"c:\\halo\\SOURCE\\render\\render_sprite.c",
		419,
		mode==_build_sprite_normal ||
			(untransformed_direction && magnitude_squared3d(untransformed_direction)));

	if (!color)
		color = global_real_argb_white;

	if (data->sprite_count<data->maximum_sprite_count)
	{
		if (sequence_index>=0 && sequence_index<bitmap_group->sequences.count)
		{
		struct bitmap_group_sequence *sequence = TAG_BLOCK_GET_ELEMENT(
			&bitmap_group->sequences,
			sequence_index,
			struct bitmap_group_sequence);

		if (sequence->first_bitmap_index!=NONE &&
			sprite_index>=0 && sprite_index<sequence->sprites.count)
		{
			struct bitmap_group_sprite *sprite = TAG_BLOCK_GET_ELEMENT(
				&sequence->sprites,
				sprite_index,
				struct bitmap_group_sprite);
			struct bitmap_data *bitmap;
			short group_index;

			match_vassert(
				"c:\\halo\\SOURCE\\render\\render_sprite.c",
				435,
				sprite->bitmap_index!=NONE,
				csprintf(
					temporary,
					"the bitmap group %s sequence %d sprite %d references bitmap -1 (tell matt).",
					tag_get_name(data->bitmap_group_index),
					sequence_index,
					sprite_index));

			bitmap = TAG_BLOCK_GET_ELEMENT(
				&bitmap_group->bitmaps,
				sprite->bitmap_index,
				struct bitmap_data);
			group_index = build_sprite_get_group(data, bitmap);
			if (group_index!=NONE)
			{
				struct build_sprite_group *group = &data->groups[group_index];

				if (group->sprite_count<data->maximum_sprite_count)
				{
#ifdef HALO_LINUX
					/* (port) after the trimmed sprites' extra quads */
					short vertex_index = NUMBER_OF_VERTICES_PER_QUADRILATERAL*(group->sprite_count + group->pad0A);
					short first_vertex_index = vertex_index;
					word extra_quads_before = group->pad0A;
					real trim_fraction = 1.f;
#else
					short vertex_index = NUMBER_OF_VERTICES_PER_QUADRILATERAL*group->sprite_count;
#endif
					real rotation_sine = 0.f;
					real rotation_cosine = 1.f;
					real_rectangle3d bounds = *global_null_rectangle3d;
					real_point3d transformed_origin;
					real_vector3d transformed_direction;
					real_matrix4x3 basis;
					pixel32 pixel;
					real alpha;
					short vertex;

					if (rotation!=0.f)
					{
#ifdef HALO_LINUX
						sine_cosine(rotation, &rotation_sine, &rotation_cosine);
#else
						rotation_sine = sine(rotation);
						rotation_cosine = cosine(rotation);
#endif
					}

					build_sprite_transform_origin_and_direction(
						data,
						flags,
						untransformed_origin,
						untransformed_direction,
						&transformed_origin,
						&transformed_direction);
					build_sprite_compute_basis(
						data,
						mode,
						flags,
						&transformed_origin,
						&transformed_direction,
						&basis);
					build_sprite_compute_scale(data, mode, flags, &transformed_origin, bitmap, &scale);

					if (data->shader &&
						data->shader->framebuffer_fade_mode &&
						mode!=_build_sprite_normal)
					{
						cross_product3d(&basis.forward, &basis.left, &basis.up);
						fade *= build_sprite_compute_vertex_fade(
							data->shader->framebuffer_fade_mode,
							&transformed_origin,
							&basis.up);
					}

					pixel = real_argb_color_to_pixel32(color);
					if (data->shader &&
						data->shader->framebuffer_blend_function &&
						!TEST_FLAG(
							data->shader->flags,
							_shader_effect_uses_nonlinear_tint_bit))
					{
						alpha = fade*255.f;
					}
					else
					{
						alpha = (real)(pixel>>24)*fade;
					}
					pixel = (pixel & 0x00ffffff) | ((unsigned long)(byte)alpha<<24);

					for (vertex = 0; vertex<NUMBER_OF_VERTICES_PER_QUADRILATERAL; vertex++)
					{
						real u = (((vertex>>1)^vertex)&1)
							? sprite->bounds.x1
							: sprite->bounds.x0;
						real v = (vertex&2) ? sprite->bounds.y0 : sprite->bounds.y1;
						real offset_x = u - (sprite->bounds.x0 + sprite->registration_point.x);
						real offset_y = (sprite->registration_point.y + sprite->bounds.y0) - v;
						real x = offset_x*rotation_cosine - offset_y*rotation_sine;
						real y = offset_y*rotation_cosine + offset_x*rotation_sine;

						if (TEST_FLAG(flags, _build_sprite_u_mirror_bit))
							x = -x;
						if (TEST_FLAG(flags, _build_sprite_v_mirror_bit))
							y = -y;

						if (TEST_FLAG(data->flags, _build_sprites_screen_space_bit))
						{
							struct dynamic_screen_vertex *screen_vertex =
								&((struct dynamic_screen_vertex *)group->vertices)[vertex_index];

							screen_vertex->position.x = x*scale + transformed_origin.x;
							screen_vertex->position.y = y*scale + transformed_origin.y;
							screen_vertex->texture_coordinates.x = u;
							screen_vertex->texture_coordinates.y = v;
							screen_vertex->color = pixel;
						}
						else
						{
							struct build_sprite_vertex *sprite_vertex =
								&((struct build_sprite_vertex *)group->vertices)[vertex_index];
							real_point3d point;

							point.x = (basis.forward.i*x + basis.left.i*y)*scale +
								transformed_origin.x;
							point.y = (basis.forward.j*x + basis.left.j*y)*scale +
								transformed_origin.y;
							point.z = (basis.forward.k*x + basis.left.k*y)*scale +
								transformed_origin.z;
							if (point.x<bounds.x0)
								bounds.x0 = point.x;
							if (point.x>bounds.x1)
								bounds.x1 = point.x;
							if (point.y<bounds.y0)
								bounds.y0 = point.y;
							if (point.y>bounds.y1)
								bounds.y1 = point.y;
							if (point.z<bounds.z0)
								bounds.z0 = point.z;
							if (point.z>bounds.z1)
								bounds.z1 = point.z;
							sprite_vertex->point = point;
							sprite_vertex->texture_coordinates.x = u;
							sprite_vertex->texture_coordinates.y = v;
							sprite_vertex->color = pixel;
						}
						vertex_index++;
					}

#ifdef HALO_LINUX
					if (!TEST_FLAG(data->flags, _build_sprites_screen_space_bit) && sprite_trim_enabled())
					{
						int channels = sprite_trim_channels(data->shader);
						void *resource = channels ? _texture_cache_bitmap_get_hardware_format(bitmap, FALSE, FALSE) : NULL;
						struct vita_sprite_texels texels;
						float texture_width, texture_height;
						float rectangle[4];
						real_point2d points[8];
						short point_count = -1;

						rectangle[0] = sprite->bounds.x0;
						rectangle[1] = sprite->bounds.y0;
						rectangle[2] = sprite->bounds.x1;
						rectangle[3] = sprite->bounds.y1;
						/* (only where the cut quad's corners get the fog the
						whole quad interpolates to there) */
						if (resource && !sprite_trim_fog_linear(
							&((struct build_sprite_vertex *)group->vertices)[first_vertex_index]))
						{
							resource = NULL;
							sprite_trim_fogged++;
						}
						if (resource)
						{
							/* (only sprites at least HALO_SPRITE_TRIM_MIN_PIXELS
							across on screen, default 32, in front of the near
							plane: a small sprite's cut corners land on the
							GPU's sub-pixel grid apart from where its texels
							map, and its edge pixels sample a little elsewhere;
							small sprites are little of the fill) */
							struct build_sprite_vertex const *corners =
								&((struct build_sprite_vertex *)group->vertices)[first_vertex_index];
							real x0 = 1.0e30f, x1 = -1.0e30f, y0 = 1.0e30f, y1 = -1.0e30f;
							short corner;

							for (corner = 0; corner < NUMBER_OF_VERTICES_PER_QUADRILATERAL; corner++)
							{
								real depth = -corners[corner].point.z;
								real x, y;

								if (depth <= render.frustum.z_near)
								{
									x0 = y0 = 0.f;
									x1 = y1 = -1.f;
									break;
								}
								x = corners[corner].point.x*render.frustum.projection_world_to_screen.i/depth;
								y = corners[corner].point.y*render.frustum.projection_world_to_screen.j/depth;
								x0 = MIN(x0, x); x1 = MAX(x1, x);
								y0 = MIN(y0, y); y1 = MAX(y1, y);
							}
							if (x1 - x0 < sprite_trim_minimum_pixels || y1 - y0 < sprite_trim_minimum_pixels)
							{
								resource = NULL;
								sprite_trim_small++;
							}
						}
						if (resource &&
							vita_sprite_texel_bounds(resource, rectangle, channels, &texels, &texture_width, &texture_height))
						{
							boolean octagon = sprite_trim_mode >= 2 && TEST_FLAG(data->pad22, group_index);

							point_count = sprite_trim_polygon(sprite, &texels, texture_width, texture_height, octagon, points);
						}
						sprite_trim_sprites++;
						if (point_count >= 0)
						{
							struct build_sprite_vertex *sprite_vertices =
								&((struct build_sprite_vertex *)group->vertices)[first_vertex_index];
							real full_area = (real)fabs((sprite->bounds.x1 - sprite->bounds.x0)*(sprite->bounds.y1 - sprite->bounds.y0));
							short quad_count = point_count >= 4 ? (point_count - 1)/2 : 1;
							short quad, corner;

							sprite_trim_trimmed++;
							if (!point_count)
							{
								/* nothing can show: four copies of one point */
								sprite_trim_empty++;
								trim_fraction = 0.f;
								for (corner = 1; corner < NUMBER_OF_VERTICES_PER_QUADRILATERAL; corner++)
									sprite_vertices[corner] = sprite_vertices[0];
							}
							else
							{
								struct build_sprite_vertex first_vertex = sprite_vertices[0];

								trim_fraction = full_area > 0.f ? sprite_trim_polygon_area(points, point_count)/full_area : 1.f;
								for (quad = 0; quad < quad_count; quad++)
								{
									for (corner = 0; corner < NUMBER_OF_VERTICES_PER_QUADRILATERAL; corner++)
									{
										/* a fan from point 0: (0 1 2 3) (0 3 4 5) (0 5 6 7) */
										short point_index = corner == 0 ? 0 : MIN(2*quad + corner, point_count - 1);
										struct build_sprite_vertex *sprite_vertex =
											&sprite_vertices[quad*NUMBER_OF_VERTICES_PER_QUADRILATERAL + corner];
										real u = points[point_index].x;
										real v = points[point_index].y;
										real offset_x = u - (sprite->bounds.x0 + sprite->registration_point.x);
										real offset_y = (sprite->registration_point.y + sprite->bounds.y0) - v;
										real x = offset_x*rotation_cosine - offset_y*rotation_sine;
										real y = offset_y*rotation_cosine + offset_x*rotation_sine;

										if (TEST_FLAG(flags, _build_sprite_u_mirror_bit))
											x = -x;
										if (TEST_FLAG(flags, _build_sprite_v_mirror_bit))
											y = -y;
										sprite_vertex->point.x = (basis.forward.i*x + basis.left.i*y)*scale +
											transformed_origin.x;
										sprite_vertex->point.y = (basis.forward.j*x + basis.left.j*y)*scale +
											transformed_origin.y;
										sprite_vertex->point.z = (basis.forward.k*x + basis.left.k*y)*scale +
											transformed_origin.z;
										sprite_vertex->texture_coordinates.x = u;
										sprite_vertex->texture_coordinates.y = v;
										sprite_vertex->color = first_vertex.color;
									}
								}
								group->pad0A += quad_count - 1;
								vertex_index = first_vertex_index + quad_count*NUMBER_OF_VERTICES_PER_QUADRILATERAL;
							}
						}
					}
#endif
					data->centroid.x += transformed_origin.x;
					data->centroid.y += transformed_origin.y;
					data->centroid.z += transformed_origin.z;
					group->sprite_count++;
					data->sprite_count++;

					if (!TEST_FLAG(data->flags, _build_sprites_screen_space_bit))
					{
						real coverage = render_frustum_cube_view_fraction(&render.frustum, &bounds);

						build_sprite_globals.screen_coverage += coverage;
#ifdef HALO_LINUX
						sprite_trim_area_full += coverage;
						sprite_trim_area_kept += coverage*trim_fraction;
#endif
						if (coverage>0.5f && build_sprite_globals.big_sprite_count++>10)
						{
							group->sprite_count--;
							data->sprite_count--;
#ifdef HALO_LINUX
							group->pad0A = extra_quads_before;
							vertex_index = first_vertex_index + NUMBER_OF_VERTICES_PER_QUADRILATERAL;
#endif
						}

						if (debug_sprites)
						{
							struct build_sprite_vertex *vertices =
								(struct build_sprite_vertex *)group->vertices;
							real_point3d point0;
							real_point3d point1;
							real_point3d point2;
							real_point3d point3;

							matrix4x3_transform_point(
								&render.frustum.view_to_world,
								&vertices[vertex_index - 4].point,
								&point0);
							matrix4x3_transform_point(
								&render.frustum.view_to_world,
								&vertices[vertex_index - 3].point,
								&point1);
							matrix4x3_transform_point(
								&render.frustum.view_to_world,
								&vertices[vertex_index - 2].point,
								&point2);
							matrix4x3_transform_point(
								&render.frustum.view_to_world,
								&vertices[vertex_index - 1].point,
								&point3);
							rasterizer_debug_line(&point0, &point1, global_real_argb_white);
							rasterizer_debug_line(&point0, &point2, global_real_argb_white);
							rasterizer_debug_line(&point2, &point3, global_real_argb_white);
							rasterizer_debug_line(&point3, &point1, global_real_argb_white);
						}
					}
				}
			}
		}
		}
	}
	else
	{
		error(_error_silent, "build_sprite sprite count exceeded.");
	}
	return;
}

void build_sprite_rotational(
	struct build_sprite_data *data,
	unsigned long flags,
	short sequence_index,
	short sprite_index,
	real_point3d const *untransformed_origin,
	real_vector3d const *untransformed_axis_of_rotation,
	real rotation,
	real scale,
	real_argb_color const *color,
	real fade)
{
	real_point3d transformed_origin;
	real_vector3d transformed_axis_of_rotation;
	real const quarter_circle = _pi/2;
	real fraction;
	real angle;
	real sprite_rotation;

	match_assert("c:\\halo\\SOURCE\\render\\render_sprite.c", 646, data);
	match_assert("c:\\halo\\SOURCE\\render\\render_sprite.c", 647, untransformed_origin);
	match_assert(
		"c:\\halo\\SOURCE\\render\\render_sprite.c",
		648,
		untransformed_axis_of_rotation);

	if (!color)
		color = global_real_argb_white;

	build_sprite_transform_origin_and_direction(
		data,
		flags & FLAG(_build_sprite_rotational_viewer_space_bit),
		untransformed_origin,
		untransformed_axis_of_rotation,
		&transformed_origin,
		&transformed_axis_of_rotation);

	angle = angle_between_vectors3d(
		(real_vector3d const *)&transformed_origin,
		&transformed_axis_of_rotation) - quarter_circle;
	fraction = angle*angle/(quarter_circle*quarter_circle);
	fraction = PIN(fraction, 0.f, 1.f);

	if (fraction>0.05f)
	{
		struct bitmap_group_sequence *sequence = TAG_BLOCK_GET_ELEMENT(
			&bitmap_group_get(data->bitmap_group_index)->sequences,
			sequence_index+1,
			struct bitmap_group_sequence);
		short sprite_count = (short)sequence->sprites.count;
		unsigned long edge_flags = FLAG(_build_sprite_viewer_space_bit);
		short edge_sprite_index;

		if (TEST_FLAG(flags, _build_sprite_rotational_sideways_rotation_animates_bit))
		{
			edge_sprite_index = (short)(fmod(
				sprite_count*one_over_full_circle*rotation + 0.5f,
				(real)sprite_count) + sprite_index);
			sprite_rotation = 0.f;
			if (angle<0.f)
				edge_sprite_index = sprite_count-sprite_index;
		}
		else
		{
			edge_sprite_index = sprite_index;
			sprite_rotation = rotation;
			if (angle<0.f)
				SET_FLAG(edge_flags, _build_sprite_u_mirror_bit, TRUE);
		}

		build_sprite(
			data,
			_build_sprite_normal,
			sequence_index+1,
			edge_sprite_index,
			&transformed_origin,
			NULL,
			sprite_rotation,
			scale,
			color,
			fraction*fade,
			edge_flags);
	}

	fraction = 1.f-fraction;
	if (fraction>0.05f)
	{
		struct bitmap_group_sequence *sequence = TAG_BLOCK_GET_ELEMENT(
			&bitmap_group_get(data->bitmap_group_index)->sequences,
			sequence_index,
			struct bitmap_group_sequence);
		short sprite_count = (short)sequence->sprites.count;

		build_sprite(
			data,
			_build_sprite_normal,
			sequence_index,
			(short)fmod(
				sprite_count*one_over_full_circle*rotation + 0.5f,
				(real)sprite_count),
			&transformed_origin,
			NULL,
			arctangent(
				transformed_axis_of_rotation.j,
				transformed_axis_of_rotation.i),
			scale,
			color,
			fraction*fade,
			FLAG(_build_sprite_viewer_space_bit));
	}
	return;
}

/* ---------- private code */

static void build_sprite_transform_origin_and_direction(
	struct build_sprite_data const *data,
	unsigned long flags,
	real_point3d const *untransformed_origin,
	real_vector3d const *untransformed_direction,
	real_point3d *transformed_origin,
	real_vector3d *transformed_direction)
{
	match_assert("c:\\halo\\SOURCE\\render\\render_sprite.c", 74, data);
	match_assert("c:\\halo\\SOURCE\\render\\render_sprite.c", 75, untransformed_origin);
	match_assert("c:\\halo\\SOURCE\\render\\render_sprite.c", 76, transformed_origin);
	match_assert("c:\\halo\\SOURCE\\render\\render_sprite.c", 77, transformed_direction);

	if (TEST_FLAG(data->flags, _build_sprites_screen_space_bit))
	{
		real_vector3d unused_view_vector;

		render_camera_screen_to_view(
			&render.camera,
			&render.frustum,
			(real_point2d const *)untransformed_origin,
			&unused_view_vector);
		match_assert("c:\\halo\\SOURCE\\render\\render_sprite.c", 84, !untransformed_direction);
	}
	else if (TEST_FLAG(flags, _build_sprite_viewer_space_bit))
	{
		*transformed_origin = *untransformed_origin;
		if (untransformed_direction)
			*transformed_direction = *untransformed_direction;
	}
	else
	{
		matrix4x3_transform_point(
			&render.frustum.world_to_view,
			untransformed_origin,
			transformed_origin);
		if (untransformed_direction)
			matrix4x3_transform_normal(
				&render.frustum.world_to_view,
				untransformed_direction,
				transformed_direction);
	}
	return;
}

static void build_sprite_compute_scale(
	struct build_sprite_data const *data,
	short mode,
	unsigned long flags,
	real_point3d const *origin,
	struct bitmap_data const *bitmap,
	real *scale)
{
	if (TEST_FLAG(data->flags, _build_sprites_screen_space_bit))
	{
		if (*scale == 0.f)
			*scale = 1.f;
	}
	else if (mode == _build_sprite_normal && *scale == 0.f)
	{
		*scale = -(origin->z / render.frustum.projection_world_to_screen.i);
	}

	*scale = bitmap->width * *scale;
	return;
}

static short build_sprite_get_group(
	struct build_sprite_data *data,
	struct bitmap_data *bitmap)
{
	/* Name, type and function scope from the 2003 PC demo PDB and the HCEX PDB (static local
	   unsigned char warned). Neither PDB records the block: placing it at the top of the function
	   is unattested. January corroborates: its one-byte .bss is referenced only here. */
	static boolean warned;
	short group_index;

	for (group_index = 0; group_index < data->group_count; group_index++)
	{
		if (data->groups[group_index].bitmap == bitmap)
			break;
	}

	match_vassert(
		"c:\\halo\\SOURCE\\render\\render_sprite.c",
		275,
		group_index < data->group_count ||
			data->group_count < MAXIMUM_BUILD_SPRITE_GROUPS,
		csprintf(
			temporary,
			"a build_sprites_begin call can accomodate at most %d bitmaps",
			MAXIMUM_BUILD_SPRITE_GROUPS));

	if (group_index < data->group_count ||
		data->group_count < MAXIMUM_BUILD_SPRITE_GROUPS)
	{
		if (group_index >= data->group_count)
		{
			struct build_sprite_group *group = &data->groups[group_index];

			data->group_count++;
			group->bitmap = bitmap;
			if (_texture_cache_bitmap_get_hardware_format(bitmap, FALSE, TRUE))
			{
				rasterizer_globals.current_lock_operation = _rasterizer_lock_sprite;
#ifdef HALO_LINUX
				/* (port) room for every sprite as an octagon's three quads
				when sprites are trimmed (above); one quad each if that much
				is not free */
				group->vertex_buffer_index = NONE;
				group->pad0A = 0;
				if (!TEST_FLAG(data->flags, _build_sprites_screen_space_bit) && sprite_trim_enabled() >= 2 &&
					sprite_trim_channels(data->shader) && !sprite_trim_zsprite(data->shader) &&
					data->maximum_sprite_count <= 2700 &&
					rasterizer_dynamic_vertices_available(6) >=
						3*NUMBER_OF_VERTICES_PER_QUADRILATERAL*(long)data->maximum_sprite_count + 4096)
				{
					group->vertex_buffer_index = rasterizer_dynamic_vertices_new(
						6, 3*NUMBER_OF_VERTICES_PER_QUADRILATERAL*data->maximum_sprite_count);
					if (group->vertex_buffer_index != NONE)
						SET_FLAG(data->pad22, group_index, TRUE);
				}
				if (group->vertex_buffer_index == NONE)
#endif
				group->vertex_buffer_index = rasterizer_dynamic_vertices_new(
					TEST_FLAG(data->flags, _build_sprites_screen_space_bit) ? 8 : 6,
					NUMBER_OF_VERTICES_PER_QUADRILATERAL*data->maximum_sprite_count);
				if (group->vertex_buffer_index != NONE)
				{
					group->vertices = rasterizer_dynamic_vertices_lock(
						group->vertex_buffer_index);
					match_assert(
						"c:\\halo\\SOURCE\\render\\render_sprite.c",
						294,
						group->vertices);
				}
				else
				{
					if (!warned)
					{
						error(
							_error_silent,
							"build_sprite failed to allocate dynamic vertices");
						warned = TRUE;
					}
					group->vertices = NULL;
				}
				rasterizer_globals.current_lock_operation = _rasterizer_lock_none;
			}
			else
			{
				group->vertices = NULL;
			}
			group->sprite_count = 0;
		}
	}
	else
	{
		group_index = NONE;
	}

	if (group_index != NONE && !data->groups[group_index].vertices)
	{
		group_index = NONE;
	}

	return group_index;
}

static void build_sprite_compute_basis(
	struct build_sprite_data const *data,
	short mode,
	unsigned long flags,
	real_point3d const *origin,
	real_vector3d const *transformed_direction,
	real_matrix4x3 *basis)
{
	match_assert("c:\\halo\\SOURCE\\render\\render_sprite.c", 116, data);
	match_assert("c:\\halo\\SOURCE\\render\\render_sprite.c", 117, origin);
	match_assert("c:\\halo\\SOURCE\\render\\render_sprite.c", 118, basis);

	if (TEST_FLAG(data->flags, _build_sprites_screen_space_bit))
	{
		match_vassert(
			"c:\\halo\\SOURCE\\render\\render_sprite.c",
			122,
			mode == _build_sprite_normal,
			"build_sprite only supports normal sprites in screen space.");
	}
	else if (mode == _build_sprite_normal)
	{
		basis->forward.i = 1.f;
		basis->forward.j = 0.f;
		basis->forward.k = 0.f;
		basis->left.i = 0.f;
		basis->left.j = 1.f;
		basis->left.k = 0.f;
	}
	else if (mode == _build_sprite_parallel)
	{
		basis->forward = *transformed_direction;
		normalize3d(&basis->forward);
		cross_product3d((real_vector3d const *)origin, &basis->forward, &basis->left);
		normalize3d(&basis->left);
	}
	else if (mode == _build_sprite_perpendicular)
	{
		real_vector3d const *reference = &build_sprite_globals.viewer_space_world_up;
		real dot = dot_product3d(transformed_direction, reference);

		if (dot*dot > magnitude_squared3d(transformed_direction)*0.99f)
			reference = &build_sprite_globals.viewer_space_world_forward;

		cross_product3d(transformed_direction, reference, &basis->forward);
		normalize3d(&basis->forward);
		basis->left = basis->forward;
		basis->up = *transformed_direction;
		normalize3d(&basis->up);
		rotate_vector_about_axis(&basis->left, &basis->up, -1.f, 0.f);
	}
	else
	{
		match_vassert("c:\\halo\\SOURCE\\render\\render_sprite.c", 172, FALSE, NULL);
	}
	return;
}
