/*
MODELS.C

symbols in this file:
00112DB0 0430:
	_render_model_parts (0000)
001131E0 0110:
	_model_interpolate_node_orientations (0000)
001132F0 0090:
	_model_get_node_orientations (0000)
00113380 0140:
	_model_get_node_matrices (0000)
001134C0 0110:
	_model_node_matrices_from_orientations (0000)
001135D0 00a0:
	_model_find_marker (0000)
00113670 0030:
	_model_get_default_inverse_matrix (0000)
001136A0 0070:
	_model_find_node (0000)
00113710 0010:
	_model_geometry_part_build_tangent_matrices (0000)
00113720 0860:
	_render_model (0000)
00113F80 01d0:
	_model_get_marker_by_name (0000)
00114150 0070:
	_model_build_tangent_matrices (0000)
0027F98C 000d:
	??_C@_0N@PBHIAIOK@render_model?$AA@ (0000)
0027F9A0 0061:
	??_C@_0GB@CAHPCM@part?9?$DOcentroid_secondary_node_in@ (0000)
0027FA08 005d:
	??_C@_0FN@MLJHGCND@part?9?$DOcentroid_primary_node_inde@ (0000)
0027FA68 002c:
	??_C@_0CM@MBIKHGBC@?$CBTEST_FLAG?$CIflags?0?5_render_model_@ (0000)
0027FA94 001f:
	??_C@_0BP@PJEILNMK@c?3?2halo?2SOURCE?2models?2models?4c?$AA@ (0000)
0027FAB4 001e:
	??_C@_0BO@FFDGBNA@node?9?$DOparent_node_index?$CB?$DNNONE?$AA@ (0000)
0027FAD8 0064:
	??_C@_0GE@EAOMEKOC@?$CIactual_detail_level_index?5?$DO?$DN?50?$CJ@ (0000)
0027FB3C 001e:
	??_C@_0BO@INPHBEBB@actual_detail_level_index?5?$DO?50?$AA@ (0000)
0027FB60 0060:
	??_C@_0GA@CEBIGMGH@geometry_detail_level_index?$DO?$DN0?5?$CG@ (0000)
0027FBC0 0009:
	??_C@_08OEDKDIOB@lighting?$AA@ (0000)
0027FBD0 0073:
	??_C@_0HD@NKPBNIND@object_marker?9?$DOnode_index?$DO?$DN0?5?$CG?$CG?5@ (0000)
0027FC44 0008:
	??_C@_07LGFKJAB@markers?$AA@ (0000)
0027FC4C 000e:
	??_C@_0O@BKFGBDDJ@node_matrices?$AA@ (0000)
0030A390 05f8:
	_render_model_section (0000)
00456650 0088:
	_default_function_values (0000)
	_default_render_model_change_colors (0000)
	_default_render_model_effect (0000)
	_default_render_model_region_permutation_indices (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "cseries/profile.h"
#include "models.h"

#include "model_definitions.h"

#include "game/game.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer_geometry.h"
#include "render/render.h"
#include "render/render_debug.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"

/* ---------- constants */

enum
{
	NUMBER_OF_DETAIL_LEVELS_PER_MODEL = 5,
	CORTANA_MODEL_NODE_LIST_CHECKSUM = 124371095,
};

enum
{
	_scenario_cortana_hack_bit = 0,
	_scenario_demo_ui_bit,
	NUMBER_OF_SCENARIO_FLAGS
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

enum
{
	_render_model_immediate_bit = 0,
	_render_model_shadow_bit,
	_render_model_no_planar_fog_bit,
	_render_model_first_person_bit,
	NUMBER_OF_RENDER_MODEL_FLAGS
};

enum
{
	_render_model_pass_solid = 0,
	_render_model_pass_decal,
	_render_model_pass_transparent,
	NUMBER_OF_RENDER_MODEL_PASSES
};

enum
{
	_model_geometry_part_stripped_bit = 0,
	_model_geometry_part_local_nodes_bit,
	NUMBER_OF_MODEL_GEOMETRY_PART_FLAGS
};

enum
{
	_shader_type_screen = 0,
	_shader_type_effect,
	_shader_type_decal,
	_shader_type_environment,
	_shader_type_model,
	_shader_type_transparent_generic,
	_shader_type_transparent_chicago,
	_shader_type_transparent_water,
	_shader_type_transparent_glass,
	_shader_type_transparent_meter,
	_shader_type_transparent_plasma,
	NUMBER_OF_SHADER_TYPES
};

enum
{
	_shader_model_detail_after_reflection_bit = 0,
	_shader_model_two_sided_bit,
	_shader_model_not_alpha_tested_bit,
	_shader_model_alpha_blended_decal_bit,
	_shader_model_true_atmospheric_fog_bit,
	_shader_model_nocull_two_sided_bit,
	NUMBER_OF_SHADER_MODEL_FLAGS
};

/* ---------- macros */

/* ---------- structures */

struct model_shader_reference
{
	struct tag_reference shader;
	short permutation_index;
	word pad;
	long unused[3];
};

struct model_geometry
{
	byte reserved[0x24];
	struct tag_block parts;
};

struct model_geometry_part
{
	unsigned long flags;
	short shader_index;
	char previous_part_index;
	char next_part_index;
	short centroid_primary_node_index;
	short centroid_secondary_node_index;
	real centroid_primary_node_weight;
	real centroid_secondary_node_weight;
	real_point3d centroid;
	struct tag_block uncompressed_vertices;
	struct tag_block compressed_vertices;
	struct tag_block triangles;
	struct triangle_buffer triangle_buffer;
	struct vertex_buffer vertex_buffer;
};

typedef char verify_model_shader_reference_size[sizeof(struct model_shader_reference) == 0x20 ? 1 : -1];
typedef char verify_model_geometry_part_size[sizeof(struct model_geometry_part) == 0x68 ? 1 : -1];

struct shader_model_definition
{
	struct shader shader;
	word flags;
	short type;
	byte reserved_before_translucency[0xC];
	real translucency;
};

struct rasterizer_model_skinning
{
	real_matrix4x3 const *node_matrices;
	short node_matrix_count;
	word pad;
};

struct render_sort_filth
{
	short *previous_group_presorted_index_reference;
	short *next_group_presorted_index_reference;
	short group_index;
	short next_part_index;
	short part_index;
	word pad;
};

struct render_model_effect
{
	short type;
	word pad;
	real intensity;
	byte reserved[0x20];
};

struct rasterizer_model_begin_parameters
{
	unsigned long geometry_flags;
	long unique_identifier;
	struct rasterizer_model_skinning skinning;
	struct render_lighting lighting;
	struct render_animation animation;
	struct render_model_effect effect;
	real_point3d centroid;
	real radius;
	real_vector2d base_map_scale;
};

typedef char verify_render_model_effect_size[sizeof(struct render_model_effect) == 0x28 ? 1 : -1];
typedef char verify_rasterizer_model_begin_parameters_size[sizeof(struct rasterizer_model_begin_parameters) == 0xCC ? 1 : -1];

struct rasterizer_debug_options
{
	byte reserved[8];
	short debug_model_lod;
	byte trailing[0x5E];
};

typedef char verify_rasterizer_debug_options_size[sizeof(struct rasterizer_debug_options) == 0x68 ? 1 : -1];

/* ---------- prototypes */

#include "rasterizer/rasterizer_models.h"

#ifdef HALO_LINUX
/* (port) HALO_RENDER_PROFILE=1: render_model's time split - the node
matrices, everything up to rasterizer_model_begin (lighting and the
parameters), and the parts (the draws), the parts by kind (the opaque and
decal draws, the transparent submissions, the shadow draws; "loop" the
walk over the regions and parts around them) - in the frames
fine_profile.h picks, reported by render_objects.c */
#include "fine_profile.h"
void platform_log(const char *format, ...);
static int render_model_profile_on = -1;
enum
{
	_render_model_profile_matrices,
	_render_model_profile_setup,
	_render_model_profile_parts,
	_render_model_profile_draws,
	_render_model_profile_transparent,
	_render_model_profile_shadow,
	NUMBER_OF_RENDER_MODEL_PROFILE_SLOTS
};
static unsigned long long render_model_profile_us[NUMBER_OF_RENDER_MODEL_PROFILE_SLOTS];
static unsigned long render_model_profile_calls, render_model_profile_parts[3];
#define RENDER_MODEL_PROFILE_ON() (render_model_profile_on > 0 && halo_fine_render_on)
#define RENDER_MODEL_NOW() (RENDER_MODEL_PROFILE_ON() ? halo_fine_render_now() : 0)
#define RENDER_MODEL_ADD(slot, from) do { if (RENDER_MODEL_PROFILE_ON()) { unsigned long long now_ = halo_fine_render_now(); render_model_profile_us[slot] += now_ - (from); (from) = now_; } } while (0)
void halo_model_part_profile_report(unsigned long frames);
void halo_render_model_profile_report(unsigned long frames)
{
	if (render_model_profile_on > 0 && frames)
	{
		double per = 1.0 / (frames * 1000.0);
		long long loop = (long long)render_model_profile_us[_render_model_profile_parts] -
			(long long)render_model_profile_us[_render_model_profile_draws] -
			(long long)render_model_profile_us[_render_model_profile_transparent] -
			(long long)render_model_profile_us[_render_model_profile_shadow];

		platform_log("render_model split (ms/frame over %.1f models/frame): matrices %.2f setup+lighting %.2f parts %.2f "
			"(draws %.2f over %.1f, transparent %.2f over %.1f, shadow %.2f over %.1f, loop %.2f)",
			(double)render_model_profile_calls / frames, render_model_profile_us[_render_model_profile_matrices] * per,
			render_model_profile_us[_render_model_profile_setup] * per, render_model_profile_us[_render_model_profile_parts] * per,
			render_model_profile_us[_render_model_profile_draws] * per, (double)render_model_profile_parts[0] / frames,
			render_model_profile_us[_render_model_profile_transparent] * per, (double)render_model_profile_parts[1] / frames,
			render_model_profile_us[_render_model_profile_shadow] * per, (double)render_model_profile_parts[2] / frames,
			loop * per);
		memset(render_model_profile_us, 0, sizeof(render_model_profile_us));
		memset(render_model_profile_parts, 0, sizeof(render_model_profile_parts));
		render_model_profile_calls = 0;
		halo_model_part_profile_report(frames);
	}
}
#define RENDER_MODEL_PART_BEGIN() unsigned long long part_profile_from = RENDER_MODEL_NOW()
#define RENDER_MODEL_PART_END(kind) do { if (RENDER_MODEL_PROFILE_ON()) { \
	render_model_profile_us[_render_model_profile_draws + (kind)] += halo_fine_render_now() - part_profile_from; \
	render_model_profile_parts[kind]++; } } while (0)
#else
#define RENDER_MODEL_PART_BEGIN() ((void)0)
#define RENDER_MODEL_PART_END(kind) ((void)0)
#endif

static void render_model_parts(
	struct model const *model,
	char const *region_permutation_indices,
	struct rasterizer_model_skinning const *skinning,
	long object_index,
	short geometry_detail_level_index,
	short forced_shader_permutation_index,
	long flags);
static void model_data_error(
	struct model const *model,
	char const *problem);

/* ---------- globals */

extern struct rasterizer_debug_options rasterizer_debug_options;
extern boolean rasterizer_model_cortana_hack;

extern boolean render_model_nodes;
extern boolean render_model_markers;
extern boolean render_model_vertex_counts;
extern boolean render_model_index_counts;
extern boolean render_model_no_geometry;

static real default_function_values[MAXIMUM_FUNCTION_VALUES_PER_MODEL] = { 0 };
static real_rgb_color default_render_model_change_colors[MAXIMUM_CHANGE_COLORS_PER_MODEL] = { 0 };
static struct render_model_effect default_render_model_effect = { 0 };
static char default_render_model_region_permutation_indices[MAXIMUM_REGIONS_PER_MODEL] = { 0 };

struct profile_section render_model_section = { "render_model", NONE, TRUE };

/* ---------- private code */

#ifdef HALO_LINUX
/* (port) a part's work in a pass: what render_model_parts' walk did for
each part in each pass (unchanged) */
static void render_model_part_in_pass(
	short pass,
	struct model const *model,
	struct model_geometry_part const *part,
	short part_index,
	struct model_shader_reference const *shader_reference,
	struct shader *shader,
	struct rasterizer_model_skinning const *skinning,
	long object_index,
	short forced_shader_permutation_index,
	long flags,
	struct render_sort_filth *sort_filth,
	short *sort_filth_count_reference)
{
	boolean immediate = TEST_FLAG(flags, _render_model_immediate_bit);
	short sort_filth_count = *sort_filth_count_reference;
	real_point3d centroid;

	if (shader_type_is_valid_for_model(shader->base.type) &&
		!TEST_FLAG(part->flags, _model_geometry_part_stripped_bit))
	{
		if (shader_type_is_transparent(shader->base.type))
		{
			if (pass==_render_model_pass_transparent)
			{
				match_assert("c:\\halo\\SOURCE\\models\\models.c", 442, !TEST_FLAG(flags, _render_model_shadow_bit));
				match_assert(
					"c:\\halo\\SOURCE\\models\\models.c",
					445,
					part->centroid_primary_node_index>=0 && part->centroid_primary_node_index<model->nodes.count);
				match_assert(
					"c:\\halo\\SOURCE\\models\\models.c",
					446,
					part->centroid_secondary_node_index>=0 && part->centroid_secondary_node_index<model->nodes.count);

				RENDER_MODEL_PART_BEGIN();

				matrix4x3_transform_point(
					&skinning->node_matrices[part->centroid_primary_node_index],
					&part->centroid,
					&centroid);
				rasterizer_model_transparent_geometry_submit(
					shader,
					forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
					&part->triangle_buffer,
					NONE,
					part->triangle_buffer.count,
					&part->vertex_buffer,
					NONE,
					&centroid,
					&sort_filth[sort_filth_count]);
				RENDER_MODEL_PART_END(1);

				if (sort_filth_count<MAXIMUM_PARTS_PER_MODEL_GEOMETRY &&
					sort_filth[sort_filth_count].group_index!=NONE &&
					!immediate &&
					(part->next_part_index>0 || part->previous_part_index>0))
				{
					sort_filth[sort_filth_count].part_index = part_index;
					sort_filth[sort_filth_count].next_part_index = part->next_part_index;
					sort_filth_count++;
				}
			}
		}
		else if (shader->base.type==_shader_type_model &&
			TEST_FLAG(((struct shader_model_definition *)shader_get_and_verify_type(shader, _shader_type_model))->flags, _shader_model_alpha_blended_decal_bit))
		{
			if (pass==_render_model_pass_decal)
			{
				RENDER_MODEL_PART_BEGIN();

				match_assert("c:\\halo\\SOURCE\\models\\models.c", 491, !TEST_FLAG(flags, _render_model_shadow_bit));

				rasterizer_model_draw(
					shader,
					forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
					&part->triangle_buffer,
					NONE,
					part->triangle_buffer.count,
					&part->vertex_buffer,
					NONE);
				RENDER_MODEL_PART_END(0);
			}
		}
		else if (pass==_render_model_pass_solid)
		{
			RENDER_MODEL_PART_BEGIN();

			if (TEST_FLAG(flags, _render_model_shadow_bit))
			{
				rasterizer_environment_shadow_model_draw(
					shader,
					forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
					&part->triangle_buffer,
					&part->vertex_buffer);
				RENDER_MODEL_PART_END(2);
			}
			else
			{
				rasterizer_model_draw(
					shader,
					forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
					&part->triangle_buffer,
					NONE,
					part->triangle_buffer.count,
					&part->vertex_buffer,
					NONE);
				rasterizer_debug_model_vertices(object_index, skinning, part);
				RENDER_MODEL_PART_END(0);
			}
		}
	}

	*sort_filth_count_reference = sort_filth_count;

	return;
}

/* (port) render_model_parts walked the regions and parts once per pass
(solid, decal, transparent), looking every part's permutation, geometry,
shader reference and shader up three times to act on it in one of them.
The walk is made once: the solid parts are drawn as it finds them, as the
solid pass did, and the decal and transparent parts are kept, in the order
found, for their passes after it. Each part gets the same work in the same
order as before. A model with more than RENDER_MODEL_KEPT_PARTS decal or
transparent parts has those passes walk the regions as before. */
#define RENDER_MODEL_KEPT_PARTS 64
struct render_model_kept_part
{
	struct model_geometry_part const *part;
	struct model_shader_reference const *shader_reference;
	struct shader *shader;
	short part_index;
};
#endif

static void render_model_parts(
	struct model const *model,
	char const *region_permutation_indices,
	struct rasterizer_model_skinning const *skinning,
	long object_index,
	short geometry_detail_level_index,
	short forced_shader_permutation_index,
	long flags)
{
#ifndef HALO_LINUX
	boolean immediate = TEST_FLAG(flags, _render_model_immediate_bit);
#endif
	short last_pass = TEST_FLAG(flags, _render_model_shadow_bit) ? _render_model_pass_solid : _render_model_pass_transparent;
	struct render_sort_filth sort_filth[MAXIMUM_PARTS_PER_MODEL_GEOMETRY];
#ifdef HALO_LINUX
	struct render_model_kept_part decal_parts[RENDER_MODEL_KEPT_PARTS], transparent_parts[RENDER_MODEL_KEPT_PARTS];
	short decal_count = 0, transparent_count = 0;
	boolean kept_overflow = FALSE;
#else
	real_point3d centroid;
#endif
	short pass;

	for (pass = _render_model_pass_solid; pass<=last_pass; pass++)
	{
		short sort_filth_count = 0;
		short region_index;
		short i, j;
		/* port: no more regions than a model has room for (a map's count; the
		permutations passed in are that long at most) */
		short region_count = (short)MIN(model->regions.count, MAXIMUM_REGIONS_PER_MODEL);

#ifdef HALO_LINUX
		if (pass != _render_model_pass_solid && !kept_overflow)
		{
			/* (the decal or transparent parts the solid pass's walk kept) */
			struct render_model_kept_part const *kept = pass == _render_model_pass_decal ? decal_parts : transparent_parts;
			short kept_count = pass == _render_model_pass_decal ? decal_count : transparent_count;
			short kept_index;

			for (kept_index = 0; kept_index < kept_count; kept_index++)
			{
				render_model_part_in_pass(pass, model, kept[kept_index].part, kept[kept_index].part_index,
					kept[kept_index].shader_reference, kept[kept_index].shader, skinning, object_index,
					forced_shader_permutation_index, flags, sort_filth, &sort_filth_count);
			}
		}
		else
#endif
		for (region_index = 0; region_index<region_count; region_index++)
		{
			struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);
			char permutation_index = region_permutation_indices[region_index];

#ifdef HALO_LINUX
			/* (port) the object's permutation bytes are the tick's, and one
			read mid-change is skipped this frame (render_epoch.h) */
			if (permutation_index >= 0 && permutation_index < region->permutations.count)
#else
			if (permutation_index!=NONE)
#endif
			{
				struct model_region_permutation *permutation;
				short geometry_index;

				/* port: a permutation the region has, and a geometry the model
				has (a map's indices); a bad one draws nothing */
				if (!VALID_INDEX(permutation_index, region->permutations.count))
				{
					model_data_error(model, "region permutation");
					continue;
				}
				permutation = TAG_BLOCK_GET_ELEMENT(
					&region->permutations,
					permutation_index,
					struct model_region_permutation);
				geometry_index = permutation->geometry_indices[geometry_detail_level_index];
				if (geometry_index!=NONE && !VALID_INDEX(geometry_index, model->geometries.count))
				{
					model_data_error(model, "geometry");
					continue;
				}

				if (!render_model_no_geometry && geometry_index!=NONE)
				{
					struct model_geometry *geometry = TAG_BLOCK_GET_ELEMENT(&model->geometries, geometry_index, struct model_geometry);
					short part_index;

					for (part_index = 0; part_index<geometry->parts.count; part_index++)
					{
						struct model_geometry_part *part = TAG_BLOCK_GET_ELEMENT(&geometry->parts, part_index, struct model_geometry_part);
						struct model_shader_reference *shader_reference;
						struct shader *shader;

						/* port: a shader the model has (a map's index); a part
						without one isn't drawn */
						if (!VALID_INDEX(part->shader_index, model->shaders.count))
						{
							model_data_error(model, "shader");
							continue;
						}
						shader_reference = TAG_BLOCK_GET_ELEMENT(
							&model->shaders,
							part->shader_index,
							struct model_shader_reference);
						shader = shader_definition_get(shader_reference->shader.index);

#ifdef HALO_LINUX
						if (pass == _render_model_pass_solid && shader_type_is_valid_for_model(shader->base.type) &&
							!TEST_FLAG(part->flags, _model_geometry_part_stripped_bit))
						{
							/* (as the solid pass's walk: solid parts drawn now, the
							others kept for their passes) */
							struct render_model_kept_part *kept = NULL;

							if (shader_type_is_transparent(shader->base.type))
							{
								if (transparent_count < RENDER_MODEL_KEPT_PARTS)
									kept = &transparent_parts[transparent_count++];
								else
									kept_overflow = TRUE;
							}
							else if (shader->base.type==_shader_type_model &&
								TEST_FLAG(((struct shader_model_definition *)shader_get_and_verify_type(shader, _shader_type_model))->flags, _shader_model_alpha_blended_decal_bit))
							{
								if (decal_count < RENDER_MODEL_KEPT_PARTS)
									kept = &decal_parts[decal_count++];
								else
									kept_overflow = TRUE;
							}
							else
							{
								render_model_part_in_pass(pass, model, part, part_index, shader_reference, shader, skinning,
									object_index, forced_shader_permutation_index, flags, sort_filth, &sort_filth_count);
							}
							if (kept)
							{
								kept->part = part;
								kept->shader_reference = shader_reference;
								kept->shader = shader;
								kept->part_index = part_index;
							}
						}
						else if (pass != _render_model_pass_solid)
						{
							render_model_part_in_pass(pass, model, part, part_index, shader_reference, shader, skinning,
								object_index, forced_shader_permutation_index, flags, sort_filth, &sort_filth_count);
						}
#else
						if (shader_type_is_valid_for_model(shader->base.type) &&
							!TEST_FLAG(part->flags, _model_geometry_part_stripped_bit))
						{
							if (shader_type_is_transparent(shader->base.type))
							{
								if (pass==_render_model_pass_transparent)
								{
									/* port: the root's matrix for a node the model
									doesn't have (a map's index) */
									short centroid_node_index = VALID_INDEX(part->centroid_primary_node_index, skinning->node_matrix_count) ?
										part->centroid_primary_node_index :
										0;

									match_assert("c:\\halo\\SOURCE\\models\\models.c", 442, !TEST_FLAG(flags, _render_model_shadow_bit));
									match_assert(
										"c:\\halo\\SOURCE\\models\\models.c",
										445,
										part->centroid_primary_node_index>=0 && part->centroid_primary_node_index<model->nodes.count);
									match_assert(
										"c:\\halo\\SOURCE\\models\\models.c",
										446,
										part->centroid_secondary_node_index>=0 && part->centroid_secondary_node_index<model->nodes.count);

									RENDER_MODEL_PART_BEGIN();

									matrix4x3_transform_point(
										&skinning->node_matrices[centroid_node_index],
										&part->centroid,
										&centroid);
									rasterizer_model_transparent_geometry_submit(
										shader,
										forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
										&part->triangle_buffer,
										NONE,
										part->triangle_buffer.count,
										&part->vertex_buffer,
										NONE,
										&centroid,
										&sort_filth[sort_filth_count]);
									RENDER_MODEL_PART_END(1);

									if (sort_filth_count<MAXIMUM_PARTS_PER_MODEL_GEOMETRY &&
										sort_filth[sort_filth_count].group_index!=NONE &&
										!immediate &&
										(part->next_part_index>0 || part->previous_part_index>0))
									{
										sort_filth[sort_filth_count].part_index = part_index;
										sort_filth[sort_filth_count].next_part_index = part->next_part_index;
										sort_filth_count++;
									}
								}
							}
							else if (shader->base.type==_shader_type_model &&
								TEST_FLAG(((struct shader_model_definition *)shader_get_and_verify_type(shader, _shader_type_model))->flags, _shader_model_alpha_blended_decal_bit))
							{
								if (pass==_render_model_pass_decal)
								{
									RENDER_MODEL_PART_BEGIN();

									match_assert("c:\\halo\\SOURCE\\models\\models.c", 491, !TEST_FLAG(flags, _render_model_shadow_bit));

									rasterizer_model_draw(
										shader,
										forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
										&part->triangle_buffer,
										NONE,
										part->triangle_buffer.count,
										&part->vertex_buffer,
										NONE);
									RENDER_MODEL_PART_END(0);
								}
							}
							else if (pass==_render_model_pass_solid)
							{
								RENDER_MODEL_PART_BEGIN();

								if (TEST_FLAG(flags, _render_model_shadow_bit))
								{
									rasterizer_environment_shadow_model_draw(
										shader,
										forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
										&part->triangle_buffer,
										&part->vertex_buffer);
									RENDER_MODEL_PART_END(2);
								}
								else
								{
									rasterizer_model_draw(
										shader,
										forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
										&part->triangle_buffer,
										NONE,
										part->triangle_buffer.count,
										&part->vertex_buffer,
										NONE);
									rasterizer_debug_model_vertices(object_index, skinning, part);
									RENDER_MODEL_PART_END(0);
								}
							}
						}
#endif
					}
				}
			}
		}

		for (i = 0; i<sort_filth_count; i++)
		{
			for (j = 0; j<sort_filth_count; j++)
			{
				if (sort_filth[i].next_part_index==sort_filth[j].part_index && sort_filth[i].next_part_index>0)
				{
					*sort_filth[i].next_group_presorted_index_reference = sort_filth[j].group_index;
					*sort_filth[j].previous_group_presorted_index_reference = sort_filth[i].group_index;
					break;
				}
			}
		}
	}

	return;
}

/* ---------- public code */

void model_interpolate_node_orientations(
	struct model const *model,
	struct real_orientation *original_node_orientations,
	struct real_orientation *target_node_orientations,
	short frame_index,
	short frame_count)
{
	real fraction = (real)(frame_index + 1) / (real)frame_count;
	real inverse_fraction = 1.f - fraction;
	short node_index;
	/* port: no more nodes than the engine's arrays hold (a map's count) */
	short node_count = (short)MIN(model->nodes.count, MAXIMUM_NODES_PER_MODEL);

	match_assert(
		"c:\\halo\\SOURCE\\models\\models.c",
		579,
		frame_count>0);
	match_assert(
		"c:\\halo\\SOURCE\\models\\models.c",
		580,
		frame_index<frame_count);

	for (node_index = 0; node_index < node_count; node_index++)
	{
		struct real_orientation *target = &target_node_orientations[node_index];
		struct real_orientation *original = &original_node_orientations[node_index];

		target->scale = original->scale * inverse_fraction + target->scale * fraction;
		quaternions_interpolate_and_normalize(
			&original->rotation,
			&target->rotation,
			fraction,
			&target->rotation);
		target->translation.x = original->translation.x * inverse_fraction + target->translation.x * fraction;
		target->translation.y = original->translation.y * inverse_fraction + target->translation.y * fraction;
		target->translation.z = original->translation.z * inverse_fraction + target->translation.z * fraction;
	}

	return;
}

void model_get_node_orientations(
	struct model const *model,
	real_orientation *node_orientations)
{
	short node_index;
	/* port: no more nodes than the engine's arrays hold (a map's count) */
	short node_count = (short)MIN(model->nodes.count, MAXIMUM_NODES_PER_MODEL);

	for (node_index = 0; node_index<node_count; node_index++)
	{
		struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);

		node_orientations[node_index].rotation = node->default_rotation;
		node_orientations[node_index].translation = node->default_translation;
		node_orientations[node_index].scale = 1.f;
	}

	return;
}

void model_get_node_matrices(
	struct model const *model,
	real_matrix4x3 *node_matrices,
	real_point3d const *origin,
	real_vector3d const *forward,
	real_vector3d const *up)
{
	short node_queue[MAXIMUM_NODES_PER_MODEL];
	short read_index, write_index;
	/* port: the nodes the queue and the matrices hold (a map's count) */
	short node_count = (short)MIN(model->nodes.count, MAXIMUM_NODES_PER_MODEL);

	/* port: a model without nodes has no matrices */
	if (node_count<=0)
	{
		return;
	}

	node_queue[0] = 0;
	read_index = 0;
	write_index = 1;

	while (read_index!=write_index)
	{
		short node_index = node_queue[read_index++];
		struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);
		real_matrix4x3 node_matrix;

		matrix4x3_from_point_and_quaternion(&node_matrix, &node->default_translation, &node->default_rotation);

		if (node_index == 0)
		{
			matrix4x3_from_point_and_vectors(
				&node_matrices[node_index],
				origin ? origin : global_origin3d,
				forward ? forward : global_forward3d,
				up ? up : global_up3d);
			matrix4x3_multiply(&node_matrices[node_index], &node_matrix, &node_matrices[node_index]);
		}
		else
		{
			short parent_node_index = node->parent_node_index;

			match_assert("c:\\halo\\SOURCE\\models\\models.c", 650, node->parent_node_index!=NONE);
			/* port: a parent the model doesn't have is the root (a map's index) */
			if (!VALID_INDEX(parent_node_index, node_count))
			{
				model_data_error(model, "node's parent");
				parent_node_index = 0;
			}
			matrix4x3_multiply(&node_matrices[parent_node_index], &node_matrix, &node_matrices[node_index]);
		}

		/* port: only nodes the model has, and no more than the queue holds (a
		map's links, which could go in a loop) */
		if (node->next_sibling_node_index!=NONE)
		{
			if (VALID_INDEX(node->next_sibling_node_index, node_count) && write_index<MAXIMUM_NODES_PER_MODEL)
			{
				node_queue[write_index++] = node->next_sibling_node_index;
			}
			else
			{
				model_data_error(model, "node's sibling");
			}
		}
		if (node->first_child_node_index!=NONE)
		{
			if (VALID_INDEX(node->first_child_node_index, node_count) && write_index<MAXIMUM_NODES_PER_MODEL)
			{
				node_queue[write_index++] = node->first_child_node_index;
			}
			else
			{
				model_data_error(model, "node's child");
			}
		}
	}

	return;
}

void model_node_matrices_from_orientations(
	struct model const *model,
	real_matrix4x3 *node_matrices,
	real_orientation const *node_orientations,
	real_point3d const *origin,
	real_vector3d const *forward,
	real_vector3d const *up)
{
	real_matrix4x3 root_matrix;
	/* port: the nodes the queue and the matrices hold (a map's count) */
	short node_count = (short)MIN(model->nodes.count, MAXIMUM_NODES_PER_MODEL);

	matrix4x3_from_point_and_vectors(&root_matrix, origin, forward, up);

	if (node_count>0)
	{
		short node_queue[MAXIMUM_NODES_PER_MODEL];
		short read_index = 0;
		short write_index = 1;

		node_queue[0] = 0;

		do
		{
			short node_index = node_queue[read_index++];
			struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);
			real_matrix4x3 const *parent_matrix = node_index==0 ? &root_matrix : &node_matrices[node->parent_node_index];
			real_matrix4x3 node_matrix;

			/* port: a parent the model doesn't have is the root (a map's index) */
			if (node_index!=0 && !VALID_INDEX(node->parent_node_index, node_count))
			{
				model_data_error(model, "node's parent");
				parent_matrix = &node_matrices[0];
			}

			matrix4x3_from_orientation(&node_matrix, &node_orientations[node_index]);
			matrix4x3_multiply(parent_matrix, &node_matrix, &node_matrices[node_index]);

			/* port: only nodes the model has, and no more than the queue holds
			(a map's links, which could go in a loop) */
			if (node->next_sibling_node_index!=NONE)
			{
				if (VALID_INDEX(node->next_sibling_node_index, node_count) && write_index<MAXIMUM_NODES_PER_MODEL)
				{
					node_queue[write_index++] = node->next_sibling_node_index;
				}
				else
				{
					model_data_error(model, "node's sibling");
				}
			}
			if (node->first_child_node_index!=NONE)
			{
				if (VALID_INDEX(node->first_child_node_index, node_count) && write_index<MAXIMUM_NODES_PER_MODEL)
				{
					node_queue[write_index++] = node->first_child_node_index;
				}
				else
				{
					model_data_error(model, "node's child");
				}
			}
		}
		while (read_index!=write_index);
	}

	return;
}

short model_find_marker(
	long model_index,
	char const *name)
{
	if (model_index != NONE && name && *name)
	{
		struct model *model = model_definition_get(model_index);
		short lower_bound = 0;
		short upper_bound = (short)model->markers.count - 1;

		while (lower_bound <= upper_bound)
		{
			short marker_index = (short)((lower_bound + upper_bound) / 2);
			struct model_marker *marker = TAG_BLOCK_GET_ELEMENT(
				&model->markers,
				marker_index,
				struct model_marker);
			long comparison = _stricmp(name, marker->name);

			if (comparison == 0)
			{
				return marker_index;
			}
			if (comparison < 0)
			{
				upper_bound = marker_index - 1;
			}
			else
			{
				lower_bound = marker_index + 1;
			}
		}
	}

	return NONE;
}

real_matrix4x3 *model_get_default_inverse_matrix(
	struct model *model,
	short node_index)
{
	struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);

	return &node->runtime_default_inverse_matrix;
}

short model_find_node(
	long model_index,
	char const *name)
{
	if (model_index!=NONE)
	{
		short node_index;
		struct model *model = model_definition_get(model_index);

		for (node_index = 0; node_index<model->nodes.count; node_index++)
		{
			struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);

			if (!csstrcmp(node->name, name))
			{
				return node_index;
			}
		}
	}

	return NONE;
}

short model_get_marker_by_name(
	long model_index,
	char const *name,
	byte const *region_permutations,
	short const *node_remapping_table,
	short node_count,
	real_matrix4x3 const *node_matrices,
	boolean mirrored_flag,
	struct object_marker *markers,
	short maximum_marker_count)
{
	short result = 0;
	short marker_index = model_find_marker(model_index, name);

	match_assert("c:\\halo\\SOURCE\\models\\models.c", 760, node_matrices);
	match_assert("c:\\halo\\SOURCE\\models\\models.c", 761, markers);

	if (marker_index!=NONE)
	{
		short i;

		struct model *model = model_definition_get(model_index);
		struct model_marker* marker = TAG_BLOCK_GET_ELEMENT(&model->markers, marker_index, struct model_marker);
		/* port: the nodes the matrices passed in hold (a map's counts) */
		short matrix_count = node_remapping_table ?
			(short)MIN(node_count, MAXIMUM_NODES_PER_MODEL) :
			(short)MIN(model->nodes.count, MAXIMUM_NODES_PER_MODEL);

		for (i =0; i<marker->instances.count; i++)
		{
			struct model_marker_instance* instance = TAG_BLOCK_GET_ELEMENT(&marker->instances, i, struct model_marker_instance);

			/* port: a marker on a region or node the model doesn't have is
			skipped (a map's indices) */
			if ((region_permutations && instance->region_index>=MAXIMUM_REGIONS_PER_MODEL) ||
				instance->node_index>=MIN(model->nodes.count, MAXIMUM_NODES_PER_MODEL))
			{
				model_data_error(model, "marker");
				continue;
			}

			if (!region_permutations ||
				region_permutations[instance->region_index]==instance->permutation_index)
			{
				struct object_marker *object_marker;
				short marker_node_index;

				if (result>=maximum_marker_count)
				{
					break;
				}

				marker_node_index = node_remapping_table ? node_remapping_table[instance->node_index] : instance->node_index;
				if (!VALID_INDEX(marker_node_index, matrix_count))
				{
					model_data_error(model, "marker");
					continue;
				}

				object_marker = &markers[result++];
				object_marker->node_index = marker_node_index;
				matrix4x3_from_point_and_quaternion(&object_marker->node_matrix, &instance->translation, &instance->rotation);
				match_assert(
					"c:\\halo\\SOURCE\\models\\models.c",
					785,
					object_marker->node_index>=0 && object_marker->node_index<(node_remapping_table ? node_count : model->nodes.count));
				
				matrix4x3_multiply(&node_matrices[object_marker->node_index], &object_marker->node_matrix, &object_marker->matrix);
				if (mirrored_flag)
				{
					negate_vector3d(&object_marker->matrix.left, &object_marker->matrix.left);
				}
			}
		}
	}

	return result;
}

void model_build_tangent_matrices(
	struct model *model)
{
	short geometry_index;

	for (geometry_index = 0; geometry_index < model->geometries.count; geometry_index++)
	{
		struct model_geometry *geometry = TAG_BLOCK_GET_ELEMENT(
			&model->geometries,
			geometry_index,
			struct model_geometry);
		short part_index;

		for (part_index = 0; part_index < geometry->parts.count; part_index++)
		{
			struct model_geometry_part *part = TAG_BLOCK_GET_ELEMENT(
				&geometry->parts,
				part_index,
				struct model_geometry_part);
		}
	}

	return;
}

void model_geometry_part_build_tangent_matrices(
	struct model_geometry_part *part)
{
	return;
}

#ifdef HALO_LINUX
static short model_geometry_transparent_part_count(
	struct model const *model,
	short geometry_index)
{
	struct model_geometry *geometry;
	short part_index, count = 0;

	if (geometry_index < 0 || geometry_index >= model->geometries.count)
		return 0;
	geometry = TAG_BLOCK_GET_ELEMENT(&model->geometries, geometry_index, struct model_geometry);
	for (part_index = 0; part_index < geometry->parts.count; part_index++)
	{
		struct model_geometry_part *part = TAG_BLOCK_GET_ELEMENT(&geometry->parts, part_index, struct model_geometry_part);
		struct model_shader_reference *shader_reference;
		struct shader *shader;

		if (part->shader_index < 0 || part->shader_index >= model->shaders.count ||
			TEST_FLAG(part->flags, _model_geometry_part_stripped_bit))
			continue;
		shader_reference = TAG_BLOCK_GET_ELEMENT(&model->shaders, part->shader_index, struct model_shader_reference);
		if (shader_reference->shader.index == NONE)
			continue;
		shader = shader_definition_get(shader_reference->shader.index);
		if (shader_type_is_valid_for_model(shader->base.type) && shader_type_is_transparent(shader->base.type))
			count++;
	}
	return count;
}

/* (port) the quality settings (HALO_MODEL_LOD_SCALE, HALO_MIN_OBJECT_PIXELS)
keep a model's transparent parts: see models.h. Worked out once per model
tag (each geometry's count of transparent parts, four bits each for the
first 64) and kept in a small table (the render thread's alone) */
#define MODEL_TRANSPARENCY_COUNTED_GEOMETRIES 64

struct model_transparency
{
	long model_index;
	struct model const *model;
	unsigned long flags;
	unsigned char counts[MODEL_TRANSPARENCY_COUNTED_GEOMETRIES / 2];
};

static short model_transparent_part_count(
	struct model_transparency const *entry,
	short geometry_index);

static struct model_transparency const *model_transparency_get(
	long model_index)
{
	static struct model_transparency cache[256];
	struct model_transparency *entry;
	struct model const *model = model_definition_get(model_index);
	short geometry_index, region_index;
	boolean detail_levels = FALSE;
	short level;

	entry = &cache[(unsigned int)model_index & (NUMBEROF(cache) - 1)];
	if (entry->model_index == model_index && entry->model == model)
		return entry;
	memset(entry, 0, sizeof(*entry));
	for (geometry_index = 0;
		geometry_index < model->geometries.count && geometry_index < MODEL_TRANSPARENCY_COUNTED_GEOMETRIES;
		geometry_index++)
	{
		short count = model_geometry_transparent_part_count(model, geometry_index);

		entry->counts[geometry_index / 2] |= (unsigned char)(MIN(count, 15) << (4 * (geometry_index & 1)));
	}
	entry->model_index = model_index;
	entry->model = model;
	for (level = 0; level < NUMBER_OF_DETAIL_LEVELS_PER_MODEL; level++)
		if (model->detail_cutoff_pixels[level] > 0.0f)
			detail_levels = TRUE;
	for (region_index = 0; region_index < model->regions.count; region_index++)
	{
		struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);
		short permutation_index;

		for (permutation_index = 0; permutation_index < region->permutations.count; permutation_index++)
		{
			struct model_region_permutation *permutation = TAG_BLOCK_GET_ELEMENT(
				&region->permutations,
				permutation_index,
				struct model_region_permutation);
			short highest = model_transparent_part_count(entry, permutation->geometry_indices[NUMBER_OF_DETAIL_LEVELS_PER_MODEL - 1]);

			for (level = 0; level < NUMBER_OF_DETAIL_LEVELS_PER_MODEL; level++)
			{
				short count = model_transparent_part_count(entry, permutation->geometry_indices[level]);

				if (count)
					entry->flags |= FLAG(_model_transparency_any_bit);
				if (count < highest)
					entry->flags |= FLAG(_model_transparency_lost_bit);
			}
		}
	}
	if (TEST_FLAG(entry->flags, _model_transparency_any_bit) && !detail_levels)
		entry->flags |= FLAG(_model_transparency_every_distance_bit);
	return entry;
}

static short model_transparent_part_count(
	struct model_transparency const *entry,
	short geometry_index)
{
	if (geometry_index < 0)
		return 0;
	if (geometry_index < MODEL_TRANSPARENCY_COUNTED_GEOMETRIES)
		return (entry->counts[geometry_index / 2] >> (4 * (geometry_index & 1))) & 15;
	return MIN(model_geometry_transparent_part_count(entry->model, geometry_index), 15);
}

unsigned long model_transparency_flags(
	long model_index)
{
	return model_index == NONE ? 0 : model_transparency_get(model_index)->flags;
}

/* (port) HALO_MODEL_LOD_SCALE: the lowered detail level raised again (to at
most the level the game would have chosen) until no region of the drawn
permutations has fewer transparent parts than at the game's level (a visor,
a canopy, a shield stays where the Xbox draws it) */
static short model_detail_level_keeping_transparency(
	long model_index,
	struct model const *model,
	char const *region_permutation_indices,
	short level,
	short game_level)
{
	struct model_transparency const *entry = model_transparency_get(model_index);

	for (; level < game_level; level++)
	{
		short region_index;
		boolean lost = FALSE;

		for (region_index = 0; region_index < model->regions.count && !lost; region_index++)
		{
			struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);
			char permutation_index = region_permutation_indices[region_index];
			struct model_region_permutation *permutation;

			if (permutation_index < 0 || permutation_index >= region->permutations.count)
				continue;
			permutation = TAG_BLOCK_GET_ELEMENT(&region->permutations, permutation_index, struct model_region_permutation);
			lost = model_transparent_part_count(entry, permutation->geometry_indices[level]) <
				model_transparent_part_count(entry, permutation->geometry_indices[game_level]);
		}
		if (!lost)
			break;
	}
	return level;
}
#endif

/* port: a map's model with an index past what it has */
static void model_data_error(
	struct model const *model,
	char const *problem)
{
	if (model_data_report_once(model))
	{
		error(
			_error_silent,
			"### ERROR a model (%ld nodes) has a bad %s index; it is skipped",
			model->nodes.count,
			problem);
	}

	return;
}

void render_model(
	long model_index,
	real level_of_detail_pixels,
	real_matrix4x3 const *node_matrices,
	char const *region_permutation_indices,
	real_rgb_color const *change_colors,
	real const *function_values,
	struct render_lighting const *lighting,
	real_point3d const *centroid,
	real radius,
	struct render_model_effect const *model_effect,
	long unique_identifier,
	short forced_shader_permutation_index,
	unsigned long flags)
{
	struct model *model = model_definition_get(model_index);
#ifdef HALO_LINUX
	unsigned long long profile_from;

	if (render_model_profile_on < 0) { const char *e = getenv("HALO_RENDER_PROFILE"); render_model_profile_on = e && atoi(e) != 0; }
	profile_from = RENDER_MODEL_NOW();
	if (RENDER_MODEL_PROFILE_ON())
		render_model_profile_calls++;
#endif

	profile_enter(render_model_section);

	match_assert("c:\\halo\\SOURCE\\models\\models.c", 82, lighting);

	if (model->node_list_checksum==CORTANA_MODEL_NODE_LIST_CHECKSUM &&
		TEST_FLAG(global_scenario_get()->flags, _scenario_cortana_hack_bit))
	{
		rasterizer_model_cortana_hack = TRUE;
	}
	else
	{
		rasterizer_model_cortana_hack = FALSE;
	}

	if (level_of_detail_pixels>=model->detail_cutoff_pixels[0] || TEST_FLAG(flags, _render_model_shadow_bit))
	{
		real_matrix4x3 relative_node_matrices[MAXIMUM_NODES_PER_MODEL];
		struct rasterizer_model_begin_parameters model_parameters;
		short geometry_detail_level_index;
		short node_index;
		/* port: no more nodes than relative_node_matrices holds (a map's count) */
		short node_count = (short)MIN(model->nodes.count, MAXIMUM_NODES_PER_MODEL);

		if (!region_permutation_indices)
		{
			region_permutation_indices = default_render_model_region_permutation_indices;
		}
		if (!model_effect)
		{
			model_effect = &default_render_model_effect;
		}
		if (!change_colors)
		{
			change_colors = default_render_model_change_colors;
		}
		if (!function_values)
		{
			function_values = default_function_values;
		}
		if (!centroid)
		{
			centroid = &node_matrices->position;
		}

		if (node_matrices)
		{
			for (node_index = 0; node_index<node_count; node_index++)
			{
				struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);

				matrix4x3_multiply(
					&node_matrices[node_index],
					&node->runtime_default_inverse_matrix,
					&relative_node_matrices[node_index]);
			}
		}
		else
		{
			for (node_index = 0; node_index<node_count; node_index++)
			{
				relative_node_matrices[node_index] = render.frustum.world_to_view;
			}
		}
#ifdef HALO_LINUX
		RENDER_MODEL_ADD(_render_model_profile_matrices, profile_from);
#endif

#ifdef HALO_LINUX
		real game_level_of_detail_pixels = -1.0f;

		{
			/* (port) HALO_MODEL_LOD_SCALE=<real>: the detail level is chosen as
			if the model covered that fraction of its pixels (a handheld quality
			setting: 0.5 picks a level or so lower; the game state is untouched) */
			static float lod_scale = -1.0f;
			static unsigned long settings_seen;
			extern volatile unsigned long halo_settings_generation;

			if (lod_scale < 0.0f || settings_seen != halo_settings_generation)
			{
				settings_seen = halo_settings_generation;
				const char *setting = getenv("HALO_MODEL_LOD_SCALE");
				lod_scale = setting && atof(setting) > 0.0 ? (float)atof(setting) : 1.0f;
			}
			if (lod_scale != 1.0f)
			{
				game_level_of_detail_pixels = level_of_detail_pixels;
				level_of_detail_pixels *= lod_scale;
			}
		}
#endif
		geometry_detail_level_index = NUMBER_OF_DETAIL_LEVELS_PER_MODEL-1;
		while (geometry_detail_level_index>0 &&
			level_of_detail_pixels<model->detail_cutoff_pixels[geometry_detail_level_index])
		{
			geometry_detail_level_index--;
		}
#ifdef HALO_LINUX
		if (game_level_of_detail_pixels >= 0.0f &&
			geometry_detail_level_index < NUMBER_OF_DETAIL_LEVELS_PER_MODEL-1 &&
			TEST_FLAG(model_transparency_flags(model_index), _model_transparency_lost_bit))
		{
			short game_level = NUMBER_OF_DETAIL_LEVELS_PER_MODEL-1;

			while (game_level>0 && game_level_of_detail_pixels<model->detail_cutoff_pixels[game_level])
			{
				game_level--;
			}
			geometry_detail_level_index = model_detail_level_keeping_transparency(model_index, model,
				region_permutation_indices, geometry_detail_level_index, game_level);
		}
#endif
		if (rasterizer_debug_options.debug_model_lod!=NONE)
		{
			geometry_detail_level_index = PIN(rasterizer_debug_options.debug_model_lod, 0, NUMBER_OF_DETAIL_LEVELS_PER_MODEL-1);
		}
		match_assert(
			"c:\\halo\\SOURCE\\models\\models.c",
			169,
			geometry_detail_level_index>=0 && geometry_detail_level_index<NUMBER_OF_DETAIL_LEVELS_PER_MODEL);

		if (!TEST_FLAG(flags, _render_model_shadow_bit))
		{
			if (render_model_nodes)
			{
				for (node_index = 0; node_index<node_count; node_index++)
				{
					struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);

					/* port: and the parent is one the model has (a map's index) */
					if (VALID_INDEX(node->parent_node_index, node_count))
					{
						render_debug_line(
							TRUE,
							&node_matrices[node_index].position,
							&node_matrices[node->parent_node_index].position,
							global_real_argb_white);
					}
					render_debug_matrix(TRUE, &node_matrices[node_index], 0.05f);
				}
			}

			if (render_model_markers)
			{
				short marker_index;

				for (marker_index = 0; marker_index<model->markers.count; marker_index++)
				{
					struct model_marker *marker = TAG_BLOCK_GET_ELEMENT(&model->markers, marker_index, struct model_marker);
					short instance_index;

					for (instance_index = 0; instance_index<marker->instances.count; instance_index++)
					{
						struct model_marker_instance *instance = TAG_BLOCK_GET_ELEMENT(
							&marker->instances,
							instance_index,
							struct model_marker_instance);

						/* port: and the region and node are ones the model has room
						for (a map's indices) */
						if (instance->region_index<MAXIMUM_REGIONS_PER_MODEL &&
							instance->node_index<node_count &&
							region_permutation_indices[instance->region_index]==instance->permutation_index)
						{
							real_matrix4x3 marker_matrix;

							matrix4x3_from_point_and_quaternion(&marker_matrix, &instance->translation, &instance->rotation);
							matrix4x3_multiply(&node_matrices[instance->node_index], &marker_matrix, &marker_matrix);
							render_debug_matrix(FALSE, &marker_matrix, 0.05f);
							render_debug_string_at_point(FALSE, &marker_matrix.position, marker->name, global_real_argb_white);
						}
					}
				}
			}

			if (render_model_vertex_counts || render_model_index_counts)
			{
				short maximum_actual_detail_level_index = geometry_detail_level_index;
				boolean has_unstripped_parts = FALSE;
				short vertex_count = 0;
				short index_count = 0;
				short region_index;
				real distance;

				/* port: the regions and permutations render_model_parts draws (a
				map's counts and indices) */
				for (region_index = 0; region_index<MIN(model->regions.count, MAXIMUM_REGIONS_PER_MODEL); region_index++)
				{
					struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);
					char permutation_index = region_permutation_indices[region_index];

					if (VALID_INDEX(permutation_index, region->permutations.count))
					{
						struct model_region_permutation *permutation = TAG_BLOCK_GET_ELEMENT(
							&region->permutations,
							permutation_index,
							struct model_region_permutation);
						short actual_detail_level_index;
						short geometry_index;

						for (actual_detail_level_index = geometry_detail_level_index+1;
							actual_detail_level_index<NUMBER_OF_DETAIL_LEVELS_PER_MODEL;
							actual_detail_level_index++)
						{
							if (permutation->geometry_indices[actual_detail_level_index]!=
								permutation->geometry_indices[geometry_detail_level_index])
							{
								break;
							}
						}
						match_assert("c:\\halo\\SOURCE\\models\\models.c", 247, actual_detail_level_index > 0);
						actual_detail_level_index--;
						match_assert(
							"c:\\halo\\SOURCE\\models\\models.c",
							249,
							(actual_detail_level_index >= 0) && (actual_detail_level_index < NUMBER_OF_DETAIL_LEVELS_PER_MODEL));
						maximum_actual_detail_level_index = MAX(maximum_actual_detail_level_index, actual_detail_level_index);

						geometry_index = permutation->geometry_indices[geometry_detail_level_index];
						if (VALID_INDEX(geometry_index, model->geometries.count))
						{
							struct model_geometry *geometry = TAG_BLOCK_GET_ELEMENT(&model->geometries, geometry_index, struct model_geometry);
							short part_index;

							for (part_index = 0; part_index<geometry->parts.count; part_index++)
							{
								struct model_geometry_part *part = TAG_BLOCK_GET_ELEMENT(&geometry->parts, part_index, struct model_geometry_part);

								vertex_count+= part->vertex_buffer.count;
								switch (part->triangle_buffer.type)
								{
								case _triangle_buffer_type_triangles:
									index_count+= 3*part->triangle_buffer.count;
									has_unstripped_parts = TRUE;
									break;
								case _triangle_buffer_type_precompiled_strip:
									index_count+= part->triangle_buffer.count+2;
									break;
								default:
									match_assert("c:\\halo\\SOURCE\\models\\models.c", 276, !"unreachable");
								}
							}
						}
					}
				}

				distance = fabs(
					render.frustum.world_to_view.forward.k*centroid->x +
					render.frustum.world_to_view.left.k*centroid->y +
					render.frustum.world_to_view.up.k*centroid->z +
					render.frustum.world_to_view.position.z);
				level_of_detail_pixels = distance/render.frustum.projection_world_to_screen.j*level_of_detail_pixels*0.5f;
				if (level_of_detail_pixels>0.0001f)
				{
					real_argb_color const *detail_level_colors[NUMBER_OF_DETAIL_LEVELS_PER_MODEL];
					real_argb_color const *color;
					char string[256];
					real_point3d point;

					detail_level_colors[0] = global_real_argb_blue;
					detail_level_colors[1] = global_real_argb_green;
					detail_level_colors[2] = global_real_argb_yellow;
					detail_level_colors[3] = global_real_argb_orange;
					detail_level_colors[4] = global_real_argb_red;
					color = detail_level_colors[maximum_actual_detail_level_index];
					if (has_unstripped_parts && (game_time_get()+model_index)%30<15)
					{
						color = global_real_argb_white;
					}

					csstrcpy(string, "");
					if (render_model_vertex_counts)
					{
						_snprintf(string+csstrlen(string), sizeof(string)-csstrlen(string), "%d", vertex_count);
					}
					if (render_model_vertex_counts && render_model_index_counts)
					{
						_snprintf(string+csstrlen(string), sizeof(string)-csstrlen(string), "/");
					}
					if (render_model_index_counts)
					{
						_snprintf(string+csstrlen(string), sizeof(string)-csstrlen(string), "%d", index_count);
					}

					set_real_point3d(&point, centroid->x, centroid->y, centroid->z+level_of_detail_pixels);
					render_debug_string_at_point(FALSE, &point, string, color);
				}
			}
		}

		model_parameters.unique_identifier = unique_identifier;
		model_parameters.lighting = *lighting;
		model_parameters.centroid = *centroid;
		model_parameters.radius = radius;
		model_parameters.effect = *model_effect;
		model_parameters.animation.colors = change_colors;
		model_parameters.animation.values = function_values;
		model_parameters.skinning.node_matrices = relative_node_matrices;
		model_parameters.skinning.node_matrix_count = node_count;
		model_parameters.geometry_flags = 0;
		model_parameters.base_map_scale = model->base_map_scale;

		if (TEST_FLAG(flags, _render_model_immediate_bit))
		{
			model_parameters.geometry_flags = FLAG(_rasterizer_geometry_no_sort_bit) |
				FLAG(_rasterizer_geometry_no_queue_bit) |
				FLAG(_rasterizer_geometry_no_fog_bit) |
				FLAG(_rasterizer_geometry_no_zbuffer_bit) |
				FLAG(_rasterizer_geometry_sky_bit);
		}
		SET_FLAG(model_parameters.geometry_flags, _rasterizer_geometry_atmospheric_fog_but_no_planar_fog_bit, TEST_FLAG(flags, _render_model_no_planar_fog_bit));
		SET_FLAG(model_parameters.geometry_flags, _rasterizer_geometry_first_person_bit, TEST_FLAG(flags, _render_model_first_person_bit));

		if (TEST_FLAG(flags, _render_model_shadow_bit))
		{
			rasterizer_environment_shadow_model_begin(&model_parameters);
		}
		else
		{
			rasterizer_model_begin(&model_parameters, FALSE);
		}
#ifdef HALO_LINUX
		RENDER_MODEL_ADD(_render_model_profile_setup, profile_from);
#endif
		render_model_parts(
			model,
			region_permutation_indices,
			&model_parameters.skinning,
			unique_identifier,
			geometry_detail_level_index,
			forced_shader_permutation_index,
			flags);
#ifdef HALO_LINUX
		RENDER_MODEL_ADD(_render_model_profile_parts, profile_from);
#endif
		if (TEST_FLAG(flags, _render_model_shadow_bit))
		{
			rasterizer_environment_shadow_model_end();
		}
		else
		{
			rasterizer_model_end();
		}
	}

	rasterizer_model_cortana_hack = FALSE;

	profile_exit(render_model_section);

	return;
}

/* port: TRUE the first time a map's model, animation or other tag data is
found bad. The checks run every frame; the report goes out once. */
boolean model_data_report_once(
	void const *data)
{
	static void const *reported_data[32];
	static long next_reported_index = 0;
	long reported_index;

	for (reported_index = 0; reported_index<(long)NUMBEROF(reported_data); reported_index++)
	{
		if (reported_data[reported_index]==data)
		{
			return FALSE;
		}
	}
	reported_data[next_reported_index] = data;
	next_reported_index = (next_reported_index+1)%(long)NUMBEROF(reported_data);

	return TRUE;
}
