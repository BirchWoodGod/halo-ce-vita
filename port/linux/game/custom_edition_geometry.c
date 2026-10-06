/*
CUSTOM_EDITION_GEOMETRY.C

The model and structure BSP geometry of Halo Custom Edition maps, made into
the geometry this build draws (custom_edition_cache.h).

Custom Edition keeps geometry as Halo PC draws it. A model's parts hold no
vertices: their strips and uncompressed vertices lie in the map's model data,
which no tag holds, and when a model's parts have local nodes, each vertex
names its nodes through a table of its part's. A structure BSP's materials
hold uncompressed environment and lightmap vertices. This build draws
compressed vertices only, from Direct3D buffers that Xbox caches carry ready
made, so every part and material has its vertices compressed by the game's
own rasterizer_geometry_compress_vertices and its buffers made by the game's
own rasterizer_vertex_buffer_new and rasterizer_triangle_buffer_new. The
strips need no change. Against the Xbox maps of build 2276, whose models have
this build's layout, the models the two versions of Blood Gulch share have
the same strips and positions, and their compressed texture coordinates,
node indices and node weights are what the compressor makes of the Custom
Edition vertices (docs/custom_edition_caches.md).
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "tag_files/tag_groups.h"
#include "models/model_definitions.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/rasterizer_model_types.h"
#include "structures/structure_bsp_definitions.h"
#include "cache_file_formats.h"
#include "custom_edition_cache.h"

#include <math.h>
#include <stdlib.h>

/* ---------- constants */

enum
{
	GBXMODEL_GROUP_TAG = 'mod2',
};

enum
{
	/* a gbxmodel's parts have node tables of their own (OpenSauce
	model_definitions.hpp, gbxmodel_definition::parts_have_local_nodes) */
	_gbxmodel_parts_have_local_nodes_bit = 1,
};

enum
{
	/* the part's vertices name nodes through its node table: set on exactly
	the parts that have one in every map examined (models.c) */
	_model_geometry_part_local_nodes_bit = 1,
};

/* rasterizer_geometry_compress_vertices clamps normals and the like to
[-1, 1] and asserts the packed vector is within 0.01 of the original: a
component this far past 1 stays within that after the packing's rounding
(every vector of the maps examined is within 1.0001) */
#define MAXIMUM_COMPRESSIBLE_COMPONENT 1.005f

/* ---------- structures */

/* this build's model geometry and part, as models.c and rasterizer.c each
define them for themselves (no header declares them) */
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

/* A model part as Custom Edition caches hold it (OpenSauce
model_definitions.hpp, gbxmodel_geometry_part). Where this build keeps its
buffers it keeps the kind, length and place of its strip and vertices in the
model data (cache_file_formats.c checked they lie within it), and after them
the table its vertices' node indices go through when the model's parts have
local nodes. */
struct custom_edition_model_part
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
	short strip_type;
	word pad1;
	long strip_triangle_count;
	unsigned long strip_offset;
	unsigned long unused1;
	short vertex_type;
	word pad2;
	long vertex_count;
	unsigned long unused2[2];
	unsigned long vertex_offset;
	byte pad3[3];
	byte local_node_count;
	byte local_node_indices[MAXIMUM_NODES_PER_MODEL_GEOMETRY_PART];
	word pad4;
};

typedef char verify_model_geometry_size[
	sizeof(struct model_geometry) == 0x30 ? 1 : -1];
typedef char verify_model_geometry_part_size[
	sizeof(struct model_geometry_part) == 0x68 ? 1 : -1];
typedef char verify_custom_edition_model_part_size[
	sizeof(struct custom_edition_model_part) == 0x84 ? 1 : -1];
typedef char verify_custom_edition_model_part_vertex_offset[
	offsetof(struct custom_edition_model_part, vertex_offset) == 0x64 ? 1 : -1];
typedef char verify_structure_material_size[
	sizeof(struct structure_material) == 0x100 ? 1 : -1];

/* what the models of a map need, counted before any is converted */
struct model_geometry_totals
{
	long part_count;
	long vertex_count;
	long strip_index_count;
	long largest_part_vertex_count;
	long largest_part_strip_index_count;
};

/* The map's model data, read a part at a time rather than whole (Extinction's
is 21 MB, which the Vita's C heap never has): its vertices and strips each
lie in model order, one region after the other, so each is read ahead into a
buffer of its own (custom_edition_cache_model_data_read) */
#define MODEL_DATA_READ_AHEAD_BYTES 0x80000

struct model_data_stream
{
	byte *buffer;
	unsigned long start;
	unsigned long length;
};

struct model_data_reader
{
	struct custom_edition_load_report const *report;
	/* room for a part's compressed vertices */
	struct model_vertex_compressed *compressed;
	struct model_data_stream vertices;
	struct model_data_stream strips;
};

struct custom_edition_geometry_globals
{
	/* the model parts that have buffers (each holding a copy of its
	compressed vertices and strip) */
	struct model_geometry_part **model_parts;
	long model_part_count;

	/* the structure BSP whose materials have buffers, and the compressed
	vertices those were made from */
	struct structure_bsp *structure_bsp;
	byte *structure_bsp_vertices;
};

/* ---------- globals */

static struct custom_edition_geometry_globals custom_edition_geometry_globals;

/* ---------- private code */

static boolean vector_compressible(
	real_vector3d const *vector)
{
	/* a NaN fails every comparison */
	return fabs(vector->i) <= MAXIMUM_COMPRESSIBLE_COMPONENT &&
		fabs(vector->j) <= MAXIMUM_COMPRESSIBLE_COMPONENT &&
		fabs(vector->k) <= MAXIMUM_COMPRESSIBLE_COMPONENT;
}

/* Whether this build can draw the part `part` of `model` (`part_count`
parts in its geometry) from `vertices` and `strip`: every index its fields,
strip and vertices hold names what exists, and every vector compresses.
Node indices are checked as the model data holds them, local to the part
when the model's parts have local nodes. */
static boolean custom_edition_model_part_verify(
	struct model const *model,
	struct custom_edition_model_part const *part,
	long part_count,
	struct model_vertex_uncompressed const *vertices,
	word const *strip)
{
	boolean local_nodes = TEST_FLAG(model->flags, _gbxmodel_parts_have_local_nodes_bit);
	long node_limit = local_nodes ? part->local_node_count : model->nodes.count;
	long strip_index;
	long vertex_index;
	long node_index;

	if (part->shader_index < 0 || part->shader_index >= model->shaders.count ||
		part->centroid_primary_node_index < 0 || part->centroid_primary_node_index >= model->nodes.count ||
		part->centroid_secondary_node_index < 0 || part->centroid_secondary_node_index >= model->nodes.count ||
		part->previous_part_index < NONE || part->previous_part_index >= part_count ||
		part->next_part_index < NONE || part->next_part_index >= part_count ||
		(local_nodes && part->local_node_count == 0))
	{
		return FALSE;
	}
	if (local_nodes)
	{
		for (node_index = 0; node_index < part->local_node_count; node_index++)
		{
			if (part->local_node_indices[node_index] >= model->nodes.count)
			{
				return FALSE;
			}
		}
	}
	for (strip_index = 0; strip_index < part->strip_triangle_count + 2; strip_index++)
	{
		if (strip[strip_index] >= part->vertex_count)
		{
			return FALSE;
		}
	}
	for (vertex_index = 0; vertex_index < part->vertex_count; vertex_index++)
	{
		struct model_vertex_uncompressed const *vertex = &vertices[vertex_index];

		if (vertex->nodes[0] < 0 || vertex->nodes[0] >= node_limit ||
			vertex->nodes[1] < 0 || vertex->nodes[1] >= node_limit ||
			!vector_compressible(&vertex->normal) ||
			!vector_compressible(&vertex->binormal) ||
			!vector_compressible(&vertex->tangent))
		{
			return FALSE;
		}
	}

	return TRUE;
}

/* Whether this build can draw `model`: its renderer skins at most
RASTERIZER_MAXIMUM_NODES_PER_MODEL - 1 nodes (each part is checked against
its vertices and strip as it is converted, custom_edition_model_convert).
Adds what its parts need to `totals`. */
static boolean custom_edition_model_count(
	struct model const *model,
	char const *name,
	struct model_geometry_totals *totals)
{
	long geometry_index;

	if (model->nodes.count < 1 || model->nodes.count >= RASTERIZER_MAXIMUM_NODES_PER_MODEL)
	{
		error(
			_error_silent,
			"custom edition: the model '%s' has %ld nodes; this build draws models of 1 to %d",
			name,
			model->nodes.count,
			RASTERIZER_MAXIMUM_NODES_PER_MODEL - 1);
		return FALSE;
	}
	for (geometry_index = 0; geometry_index < model->geometries.count; geometry_index++)
	{
		struct model_geometry const *geometry = TAG_BLOCK_GET_ELEMENT(
			&model->geometries,
			geometry_index,
			struct model_geometry);
		long part_index;

		for (part_index = 0; part_index < geometry->parts.count; part_index++)
		{
			struct custom_edition_model_part const *part = TAG_BLOCK_GET_ELEMENT(
				&geometry->parts,
				part_index,
				struct custom_edition_model_part);

			totals->part_count++;
			totals->vertex_count += part->vertex_count;
			totals->strip_index_count += part->strip_triangle_count + 2;
			totals->largest_part_vertex_count = MAX(totals->largest_part_vertex_count, part->vertex_count);
			totals->largest_part_strip_index_count = MAX(
				totals->largest_part_strip_index_count,
				part->strip_triangle_count + 2);
		}
	}

	return TRUE;
}

/* `size` bytes at `offset` in the model data through `stream`'s read-ahead
buffer (cache_file_formats.c checked every part's range lies within the
model data); NULL when the map cannot be read */
static void const *model_data_stream_read(
	struct model_data_reader const *reader,
	struct model_data_stream *stream,
	unsigned long offset,
	unsigned long size,
	void *large_buffer)
{
	unsigned long model_data_bytes = reader->report->model_data_bytes;

	if (offset >= stream->start && offset + size <= stream->start + stream->length)
	{
		return stream->buffer + (offset - stream->start);
	}
	if (size > MODEL_DATA_READ_AHEAD_BYTES)
	{
		return custom_edition_cache_model_data_read(reader->report, offset, size, large_buffer) ? large_buffer : NULL;
	}
	stream->start = offset;
	stream->length = MIN(MODEL_DATA_READ_AHEAD_BYTES, model_data_bytes - offset);
	if (!custom_edition_cache_model_data_read(reader->report, stream->start, stream->length, stream->buffer))
	{
		stream->length = 0;
		return NULL;
	}

	return stream->buffer + (offset - stream->start);
}

/* Makes `part` this build's part for the Custom Edition part `source`,
compressing its vertices (by way of `scratch`, room for all of them) to
`vertices` and copying its strip to `strip`, and gives it buffers. */
static boolean custom_edition_model_part_convert(
	struct model_geometry_part *part,
	struct custom_edition_model_part const *source,
	boolean local_nodes,
	struct model_vertex_uncompressed const *source_vertices,
	word const *source_strip,
	struct model_vertex_uncompressed *scratch,
	struct model_vertex_compressed *vertices,
	word *strip)
{
	long vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_model_compressed);
	long uncompressed_vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_model_uncompressed);
	long strip_index_count = source->strip_triangle_count + 2;

	/* the renderer skins with the model's nodes */
	if (scratch != source_vertices)
	{
		csmemcpy(scratch, source_vertices, source->vertex_count * uncompressed_vertex_size);
	}
	if (local_nodes)
	{
		long vertex_index;

		for (vertex_index = 0; vertex_index < source->vertex_count; vertex_index++)
		{
			short *nodes = scratch[vertex_index].nodes;

			nodes[0] = source->local_node_indices[nodes[0]];
			nodes[1] = source->local_node_indices[nodes[1]];
		}
	}
	rasterizer_geometry_compress_vertices(
		_rasterizer_vertex_type_model_uncompressed,
		source->vertex_count,
		vertices,
		source->vertex_count * vertex_size,
		scratch,
		source->vertex_count * uncompressed_vertex_size);
	if (strip != source_strip)
	{
		csmemcpy(strip, source_strip, strip_index_count * sizeof(*strip));
	}

	part->flags = source->flags & ~FLAG(_model_geometry_part_local_nodes_bit);
	part->shader_index = source->shader_index;
	part->previous_part_index = source->previous_part_index;
	part->next_part_index = source->next_part_index;
	part->centroid_primary_node_index = source->centroid_primary_node_index;
	part->centroid_secondary_node_index = source->centroid_secondary_node_index;
	part->centroid_primary_node_weight = source->centroid_primary_node_weight;
	part->centroid_secondary_node_weight = source->centroid_secondary_node_weight;
	part->centroid = source->centroid;
	/* empty, as in Xbox caches */
	part->uncompressed_vertices = source->uncompressed_vertices;
	part->compressed_vertices = source->compressed_vertices;
	part->triangles = source->triangles;
	csmemset(&part->triangle_buffer, 0, sizeof(part->triangle_buffer));
	csmemset(&part->vertex_buffer, 0, sizeof(part->vertex_buffer));

	if (!rasterizer_vertex_buffer_new(
		&part->vertex_buffer,
		_rasterizer_vertex_type_model_compressed,
		source->vertex_count,
		vertices,
		source->vertex_count * vertex_size))
	{
		return FALSE;
	}
	/* (made once: read in place, as an Xbox cache's model vertices are) */
	halo_d3d_buffer_in_place(part->vertex_buffer.hardware_format, TRUE);

	return rasterizer_triangle_buffer_new(
			&part->triangle_buffer,
			_triangle_buffer_type_precompiled_strip,
			source->strip_triangle_count,
			strip);
}

/* Converts every part of `model` (named `name`), repacking each geometry's
parts from Custom Edition's size to this build's in place, and records them
in the globals; each part's vertices and strip are read from the model data
to `scratch` and `strip_scratch` and checked first, and its vertices
compressed to the reader's room for them before its buffers are made. */
static boolean custom_edition_model_convert(
	struct model *model,
	char const *name,
	struct model_data_reader *reader,
	struct model_vertex_uncompressed *scratch,
	word *strip_scratch)
{
	struct custom_edition_geometry_globals *globals = &custom_edition_geometry_globals;
	boolean local_nodes = TEST_FLAG(model->flags, _gbxmodel_parts_have_local_nodes_bit);
	long uncompressed_vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_model_uncompressed);
	long geometry_index;

	for (geometry_index = 0; geometry_index < model->geometries.count; geometry_index++)
	{
		struct model_geometry *geometry = TAG_BLOCK_GET_ELEMENT(
			&model->geometries,
			geometry_index,
			struct model_geometry);
		long part_index;

		/* a part is never written over a Custom Edition part not yet read:
		this build's parts are smaller, and each is read first */
		for (part_index = 0; part_index < geometry->parts.count; part_index++)
		{
			struct custom_edition_model_part source = *TAG_BLOCK_GET_ELEMENT(
				&geometry->parts,
				part_index,
				struct custom_edition_model_part);
			struct model_geometry_part *part = TAG_BLOCK_GET_ELEMENT(
				&geometry->parts,
				part_index,
				struct model_geometry_part);
			unsigned long vertex_bytes = (unsigned long)source.vertex_count * uncompressed_vertex_size;
			unsigned long strip_bytes = (unsigned long)(source.strip_triangle_count + 2) * sizeof(word);
			void const *source_vertices = model_data_stream_read(
				reader,
				&reader->vertices,
				source.vertex_offset,
				vertex_bytes,
				scratch);
			void const *source_strip = source_vertices ? model_data_stream_read(
				reader,
				&reader->strips,
				reader->report->model_index_data_offset + source.strip_offset,
				strip_bytes,
				strip_scratch) : NULL;

			if (!source_vertices || !source_strip)
			{
				error(_error_silent, "custom edition: cannot read the model data of '%s'", name);
				return FALSE;
			}
			/* (the convert below works in `scratch`; from the read-ahead
			buffers the vertices and strip are copied there) */
			if (source_vertices != scratch)
			{
				csmemcpy(scratch, source_vertices, vertex_bytes);
			}
			if (source_strip != strip_scratch)
			{
				csmemcpy(strip_scratch, source_strip, strip_bytes);
			}
			if (!custom_edition_model_part_verify(model, &source, geometry->parts.count, scratch, strip_scratch))
			{
				error(
					_error_silent,
					"custom edition: part %ld of geometry %ld of the model '%s' names a node, vertex, shader or part that does not exist, or has a vector this build cannot compress",
					part_index,
					geometry_index,
					name);
				return FALSE;
			}

			globals->model_parts[globals->model_part_count++] = part;
			if (!custom_edition_model_part_convert(
				part,
				&source,
				local_nodes,
				scratch,
				strip_scratch,
				scratch,
				reader->compressed,
				strip_scratch))
			{
				return FALSE;
			}
		}
	}
	/* the parts' node indices are now the model's */
	model->flags &= ~FLAG(_gbxmodel_parts_have_local_nodes_bit);

	return TRUE;
}

static void structure_bsp_buffers_release(
	struct structure_bsp *structure_bsp)
{
	long lightmap_index;

	for (lightmap_index = 0; lightmap_index < structure_bsp->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(
			&structure_bsp->lightmaps,
			lightmap_index,
			struct structure_lightmap);
		long material_index;

		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material *material = TAG_BLOCK_GET_ELEMENT(
				&lightmap->materials,
				material_index,
				struct structure_material);

			halo_d3d_buffer_in_place(material->vertices.hardware_format, FALSE);
			halo_d3d_buffer_in_place(material->lightmap_vertices.hardware_format, FALSE);
			rasterizer_vertex_buffer_delete(&material->vertices);
			rasterizer_vertex_buffer_delete(&material->lightmap_vertices);
		}
	}

	return;
}

/* Gives `material` compressed vertices at `vertices` and buffers made from
them. Its uncompressed vertices (cache_file_formats.c checked their size and
place) stay where they are. */
static boolean structure_material_convert(
	struct structure_material *material,
	byte *vertices)
{
	long vertex_count = material->vertices.count;
	long lightmap_vertex_count = material->lightmap_vertices.count;
	long vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_compressed);
	long lightmap_vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_lightmap_compressed);
	byte *uncompressed_vertices = material->uncompressed_vertex_data.address;
	byte *lightmap_vertices = vertices + vertex_count * vertex_size;
	boolean success = TRUE;

	/* the lightmap vertices follow the environment vertices, as object
	lights and the structure's point queries expect of the compressed ones */
	if (vertex_count)
	{
		rasterizer_geometry_compress_vertices(
			_rasterizer_vertex_type_environment_uncompressed,
			vertex_count,
			vertices,
			vertex_count * vertex_size,
			uncompressed_vertices,
			vertex_count * rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_uncompressed));
		success = rasterizer_vertex_buffer_new(
			&material->vertices,
			_rasterizer_vertex_type_environment_compressed,
			vertex_count,
			vertices,
			vertex_count * vertex_size);
		if (success)
		{
			halo_d3d_buffer_in_place(material->vertices.hardware_format, TRUE);
		}
	}
	if (success && lightmap_vertex_count)
	{
		rasterizer_geometry_compress_vertices(
			_rasterizer_vertex_type_environment_lightmap_uncompressed,
			lightmap_vertex_count,
			lightmap_vertices,
			lightmap_vertex_count * lightmap_vertex_size,
			uncompressed_vertices + vertex_count * rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_uncompressed),
			lightmap_vertex_count * rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_lightmap_uncompressed));
		success = rasterizer_vertex_buffer_new(
			&material->lightmap_vertices,
			_rasterizer_vertex_type_environment_lightmap_compressed,
			lightmap_vertex_count,
			lightmap_vertices,
			lightmap_vertex_count * lightmap_vertex_size);
		if (success)
		{
			halo_d3d_buffer_in_place(material->lightmap_vertices.hardware_format, TRUE);
		}
	}
	material->compressed_vertex_data.size = vertex_count * vertex_size + lightmap_vertex_count * lightmap_vertex_size;
	material->compressed_vertex_data.address = vertices;

	return success;
}

/* ---------- public code */

boolean custom_edition_models_convert(
	byte *tag_cache,
	unsigned long loaded_bytes,
	struct custom_edition_load_report const *report)
{
	struct custom_edition_geometry_globals *globals = &custom_edition_geometry_globals;
	struct model_geometry_totals totals = { 0, 0, 0, 0, 0 };
	long vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_model_compressed);
	struct model_data_reader reader;
	struct model_vertex_uncompressed *scratch;
	word *strip_scratch;
	byte *working;
	unsigned long scratch_bytes;
	unsigned long strip_scratch_bytes;
	unsigned long compressed_bytes;
	unsigned long working_bytes;
	struct model *model;
	int32_t tag_index = NONE;
	boolean success = TRUE;

	assert(!globals->model_parts);
	while ((model = custom_edition_cache_tag_next(tag_cache, loaded_bytes, GBXMODEL_GROUP_TAG, sizeof(*model), &tag_index)) != NULL)
	{
		if (!custom_edition_model_count(model, custom_edition_cache_tag_name(tag_cache, loaded_bytes, tag_index), &totals))
		{
			return FALSE;
		}
	}

	/* A part at a time: its vertices and strip read from the model data,
	checked, compressed and given buffers, which hold copies of their own
	(rasterizer_vertex_buffer_new and rasterizer_triangle_buffer_new: in the
	window and the C heap) - so the map's model data (21 MB of Extinction's)
	and its compressed geometry (10 MB) are never held whole. The working
	memory, in the window, for as long as this takes. */
	scratch_bytes = (totals.largest_part_vertex_count * sizeof(*scratch) + 15) & ~15UL;
	strip_scratch_bytes = (totals.largest_part_strip_index_count * sizeof(*strip_scratch) + 15) & ~15UL;
	compressed_bytes = (totals.largest_part_vertex_count * vertex_size + 15) & ~15UL;
	working_bytes = scratch_bytes + strip_scratch_bytes + compressed_bytes + 2 * MODEL_DATA_READ_AHEAD_BYTES;
	globals->model_parts = malloc((totals.part_count + 1) * sizeof(*globals->model_parts));
	working = halo_custom_edition_geometry_alloc(working_bytes);
	if (!globals->model_parts || !working)
	{
		error(
			_error_silent,
			"custom edition: out of memory for converting the geometry of %ld model parts (%lu KB)",
			totals.part_count,
			working_bytes / 1024);
		halo_custom_edition_geometry_free(working);
		custom_edition_cache_load_failure_note("there is not enough memory for its models");
		return FALSE;
	}
	scratch = (struct model_vertex_uncompressed *)working;
	strip_scratch = (word *)(working + scratch_bytes);
	csmemset(&reader, 0, sizeof(reader));
	reader.report = report;
	reader.compressed = (struct model_vertex_compressed *)(working + scratch_bytes + strip_scratch_bytes);
	reader.vertices.buffer = working + scratch_bytes + strip_scratch_bytes + compressed_bytes;
	reader.strips.buffer = reader.vertices.buffer + MODEL_DATA_READ_AHEAD_BYTES;

	tag_index = NONE;
	while (success &&
		(model = custom_edition_cache_tag_next(tag_cache, loaded_bytes, GBXMODEL_GROUP_TAG, sizeof(*model), &tag_index)) != NULL)
	{
		char const *name = custom_edition_cache_tag_name(tag_cache, loaded_bytes, tag_index);

		success = custom_edition_model_convert(model, name, &reader, scratch, strip_scratch);
		if (!success)
		{
			error(_error_silent, "custom edition: cannot make the buffers of the model '%s'", name);
			custom_edition_cache_load_failure_note("there is not enough memory for its models");
		}
	}
	halo_custom_edition_geometry_free(working);
	if (success)
	{
		custom_edition_cache_tags_regroup(tag_cache, loaded_bytes, GBXMODEL_GROUP_TAG, MODELS_GROUP_TAG);
		error(
			_error_silent,
			"custom edition: %ld model parts converted (%ld vertices compressed, %lu KB in their buffers)",
			totals.part_count,
			totals.vertex_count,
			(totals.vertex_count * vertex_size + totals.strip_index_count * sizeof(word)) / 1024);
	}

	return success;
}

void custom_edition_models_dispose(
	void)
{
	struct custom_edition_geometry_globals *globals = &custom_edition_geometry_globals;
	long part_index;

	for (part_index = 0; part_index < globals->model_part_count; part_index++)
	{
		rasterizer_triangle_buffer_delete(&globals->model_parts[part_index]->triangle_buffer);
		halo_d3d_buffer_in_place(globals->model_parts[part_index]->vertex_buffer.hardware_format, FALSE);
		rasterizer_vertex_buffer_delete(&globals->model_parts[part_index]->vertex_buffer);
	}
	/* the game's free stops on NULL (cseries.h), and a map can fail before
	its models are converted */
	if (globals->model_parts)
	{
		free(globals->model_parts);
	}
	globals->model_parts = NULL;
	globals->model_part_count = 0;

	return;
}

boolean custom_edition_structure_bsp_load(
	struct structure_bsp *structure_bsp)
{
	struct custom_edition_geometry_globals *globals = &custom_edition_geometry_globals;
	long vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_compressed);
	long lightmap_vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_lightmap_compressed);
	unsigned long vertices_size = 0;
	unsigned long vertices_offset = 0;
	boolean success = TRUE;
	long lightmap_index;

	assert(!globals->structure_bsp);
	/* the Custom Edition buffer fields name nothing in this process: every
	material starts with none, so that a failure releases only what was
	made (cache_file_formats.c checked the counts and sizes) */
	for (lightmap_index = 0; lightmap_index < structure_bsp->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(
			&structure_bsp->lightmaps,
			lightmap_index,
			struct structure_lightmap);
		long material_index;

		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material *material = TAG_BLOCK_GET_ELEMENT(
				&lightmap->materials,
				material_index,
				struct structure_material);
			long vertex_count = material->vertices.count;
			long lightmap_vertex_count = material->lightmap_vertices.count;

			csmemset(&material->vertices, 0, sizeof(material->vertices));
			csmemset(&material->lightmap_vertices, 0, sizeof(material->lightmap_vertices));
			material->vertices.type = _rasterizer_vertex_type_environment_compressed;
			material->vertices.count = vertex_count;
			material->lightmap_vertices.type = _rasterizer_vertex_type_environment_lightmap_compressed;
			material->lightmap_vertices.count = lightmap_vertex_count;
			vertices_size += vertex_count * vertex_size + lightmap_vertex_count * lightmap_vertex_size;
		}
	}

	globals->structure_bsp = structure_bsp;
	/* (kept for the game's own reads - object lighting, point queries -
	with the buffers made from them; out of the C heap, which the Vita has
	little of) */
	globals->structure_bsp_vertices = halo_custom_edition_geometry_alloc(vertices_size);
	if (!globals->structure_bsp_vertices)
	{
		error(_error_silent, "custom edition: out of memory for 0x%lX bytes of structure BSP vertices", vertices_size);
		custom_edition_cache_load_failure_note("there is not enough memory for its level geometry");
		custom_edition_structure_bsp_unload();
		return FALSE;
	}
	for (lightmap_index = 0; success && lightmap_index < structure_bsp->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(
			&structure_bsp->lightmaps,
			lightmap_index,
			struct structure_lightmap);
		long material_index;

		for (material_index = 0; success && material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material *material = TAG_BLOCK_GET_ELEMENT(
				&lightmap->materials,
				material_index,
				struct structure_material);

			success = structure_material_convert(material, globals->structure_bsp_vertices + vertices_offset);
			vertices_offset += material->compressed_vertex_data.size;
		}
	}
	if (success)
	{
		error(
			_error_silent,
			"custom edition: the structure BSP lit by bitmap tag 0x%08lX has its vertices compressed (0x%lX bytes)",
			(unsigned long)structure_bsp->lightmap_group.index,
			vertices_size);
	}
	else
	{
		error(_error_silent, "custom edition: cannot make the buffers of the structure BSP's materials");
		custom_edition_structure_bsp_unload();
	}

	return success;
}

void custom_edition_structure_bsp_unload(
	void)
{
	struct custom_edition_geometry_globals *globals = &custom_edition_geometry_globals;

	if (globals->structure_bsp)
	{
		structure_bsp_buffers_release(globals->structure_bsp);
		halo_custom_edition_geometry_free(globals->structure_bsp_vertices);
		globals->structure_bsp = NULL;
		globals->structure_bsp_vertices = NULL;
	}

	return;
}
