/*
OBJECT_MESH.C

An object's drawn surface (object_mesh.h), which first_person_legs.c finds
the waist's rim in: the triangles of its model's most detailed geometry, in
the permutation of each region the object shows, read from the vertex and
index buffers the game draws them from, with each vertex's nodes and
weight. Opaque parts only (no glass, no effects), and none the game doesn't
draw (stripped parts).

From OpenCE (OpenCommunityEdition/OpenCE, d36c15c3 "Your own legs in first
person, seen looking down" and f735ca15 "First-person legs: one mesh kept,
its waist forgotten on a new map", #243). Here:
- The one mesh kept takes the memory its model needs, counted before it is
  read (the cyborg's: about 60 KB), not OpenCE's 400 KB for any model; a
  bigger model later grows it, up to OBJECT_MESH_MAXIMUM_VERTICES and
  _TRIANGLES. The Vita's C heap is small.
- Custom Edition models of more nodes than the renderer skins at once have
  vertices that name their part's own nodes (custom_edition_geometry.c, the
  part palettes): made the model's nodes again here, as the renderer does.
- The buffers are this build's (d3d8_resources.c, on the Vita too): a lock
  is a pointer to the vertices and indices as the cache file had them.
- Its memory is the host's (realloc in parentheses, as render_interpolation.c
  takes its own): the game's debug allocator draws on the local random seed.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "models/model_definitions.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/rasterizer_model_types.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"

#include "object_mesh.h"
#include "custom_edition_cache.h"

#include <xtl.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	OBJECT_MESH_DETAIL_LEVELS = 5, /* (models.c's NUMBER_OF_DETAIL_LEVELS_PER_MODEL) */
};

enum
{
	_object_mesh_part_stripped_bit = 0,
	_object_mesh_part_local_nodes_bit,
};

/* ---------- structures */

/* this build's model geometry and part (models.c's; no header declares
them) */
struct object_mesh_model_geometry
{
	byte reserved[0x24];
	struct tag_block parts;
};

struct object_mesh_model_geometry_part
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

typedef char verify_object_mesh_model_geometry_part_size[sizeof(struct object_mesh_model_geometry_part) == 0x68 ? 1 : -1];

struct object_mesh_model_shader_reference
{
	struct tag_reference shader;
	short permutation_index;
	word pad;
	long unused[3];
};

/* ---------- globals */

/* the mesh kept (its vertices and triangles NULL until one is asked for) */
static struct object_mesh object_mesh_kept = { NONE };
/* what its memory holds */
static long object_mesh_vertex_capacity;
static long object_mesh_triangle_capacity;

/* ---------- private code */

/* (a key for the model and the permutations it shows) */
static long object_mesh_key(
	long model_index,
	byte const *permutations)
{
	unsigned long key = (unsigned long)model_index * 2654435761ul;
	short index;

	for (index = 0; index < MAXIMUM_REGIONS_PER_OBJECT; index++)
		key = (key ^ permutations[index]) * 16777619ul;
	key &= 0x7fffffff;
	return key == (unsigned long)NONE ? 0 : (long)key;
}

/* whether a part is read (drawn, opaque, its buffers of a kind read here),
and how many vertices and triangles (at most) it has */
static boolean object_mesh_part_read(
	struct model const *model,
	struct object_mesh_model_geometry_part const *part,
	long *vertex_count,
	long *triangle_count)
{
	struct vertex_buffer const *vertex_buffer = &part->vertex_buffer;
	struct triangle_buffer const *triangle_buffer = &part->triangle_buffer;

	/* (only what the game draws, opaque) */
	if (TEST_FLAG(part->flags, _object_mesh_part_stripped_bit))
		return FALSE;
	if (VALID_INDEX(part->shader_index, model->shaders.count))
	{
		struct object_mesh_model_shader_reference const *reference = TAG_BLOCK_GET_ELEMENT(
			&model->shaders, part->shader_index, struct object_mesh_model_shader_reference);
		struct shader *shader = reference->shader.index != NONE && reference->shader.index != 0 ?
			shader_definition_get(reference->shader.index) : NULL;

		if (!shader || !shader_type_is_valid_for_model(shader->base.type) || shader_type_is_transparent(shader->base.type))
			return FALSE;
	}
	else
		return FALSE;
	if (!vertex_buffer->hardware_format || vertex_buffer->count <= 0 || !triangle_buffer->hardware_format ||
		triangle_buffer->count <= 0)
	{
		return FALSE;
	}
	if (vertex_buffer->type != _rasterizer_vertex_type_model_compressed &&
		vertex_buffer->type != _rasterizer_vertex_type_model_uncompressed)
	{
		return FALSE;
	}
	if (triangle_buffer->type == 1)
		*triangle_count = triangle_buffer->count; /* a strip: count + 2 indices */
	else if (triangle_buffer->type == 0)
		*triangle_count = triangle_buffer->count; /* a list: count * 3 */
	else
		return FALSE;
	*vertex_count = vertex_buffer->count;
	return TRUE;
}

/* a part's triangles and vertices onto the mesh; FALSE when it can't be read
(the mesh is then not used) */
static boolean object_mesh_add_part(
	struct object_mesh *mesh,
	struct model const *model,
	struct object_mesh_model_geometry_part const *part)
{
	struct vertex_buffer const *vertex_buffer = &part->vertex_buffer;
	struct triangle_buffer const *triangle_buffer = &part->triangle_buffer;
	byte *vertices = NULL;
	BYTE *indices = NULL;
	byte const *palette = NULL;
	short palette_count = 0;
	long stride;
	long first_vertex = mesh->vertex_count;
	long index_count;
	long part_vertices;
	long part_triangles;
	long index;

	if (!object_mesh_part_read(model, part, &part_vertices, &part_triangles))
		return TRUE;
	stride = vertex_buffer->type == _rasterizer_vertex_type_model_compressed ?
		sizeof(struct model_vertex_compressed) :
		sizeof(struct model_vertex_uncompressed);
	index_count = triangle_buffer->type == 1 ? triangle_buffer->count + 2 : triangle_buffer->count * 3;
	if (mesh->vertex_count + vertex_buffer->count > object_mesh_vertex_capacity)
		return FALSE;
	/* (a Custom Edition model of many nodes: the part's vertices name its
	own nodes, by their place in its palette) */
	if (model->nodes.count >= custom_edition_part_palette_nodes())
		palette_count = custom_edition_part_palette(vertex_buffer, &palette);

	IDirect3DVertexBuffer8_Lock((IDirect3DVertexBuffer8 *)vertex_buffer->hardware_format, 0, 0, &vertices, D3DLOCK_READONLY);
	IDirect3DIndexBuffer8_Lock((IDirect3DIndexBuffer8 *)triangle_buffer->hardware_format, 0, 0, &indices, D3DLOCK_READONLY);
	if (!vertices || !indices)
	{
		if (vertices)
			IDirect3DVertexBuffer8_Unlock((IDirect3DVertexBuffer8 *)vertex_buffer->hardware_format);
		if (indices)
			IDirect3DIndexBuffer8_Unlock((IDirect3DIndexBuffer8 *)triangle_buffer->hardware_format);
		return TRUE;
	}

	/* its vertices, with their nodes */
	for (index = 0; index < vertex_buffer->count; index++)
	{
		struct object_mesh_vertex *out = &mesh->vertices[mesh->vertex_count + index];
		long node0;
		long node1;
		real weight;

		if (vertex_buffer->type == _rasterizer_vertex_type_model_compressed)
		{
			struct model_vertex_compressed const *in = (struct model_vertex_compressed const *)(vertices + index * stride);

			out->position = in->position;
			node0 = in->nodes[0] / 3;
			node1 = in->nodes[1] / 3;
			weight = (real)in->node_weight * (1.f / 32767.f);
		}
		else
		{
			struct model_vertex_uncompressed const *in = (struct model_vertex_uncompressed const *)(vertices + index * stride);

			out->position = in->position;
			node0 = in->nodes[0];
			node1 = in->nodes[1];
			weight = in->node_weights[0];
		}
		if (palette_count > 0)
		{
			node0 = VALID_INDEX(node0, palette_count) ? palette[node0] : 0;
			node1 = VALID_INDEX(node1, palette_count) ? palette[node1] : node0;
		}
		/* (a part with its own node map, which this build's part doesn't
		carry: held to the node its middle is on; a tree's trunk, a rock) */
		if (TEST_FLAG(part->flags, _object_mesh_part_local_nodes_bit))
		{
			node0 = node1 = VALID_INDEX(part->centroid_primary_node_index, mesh->node_count) ?
				part->centroid_primary_node_index : 0;
			weight = 1.f;
		}
		if (!VALID_INDEX(node0, mesh->node_count))
			node0 = 0;
		if (!VALID_INDEX(node1, mesh->node_count))
		{
			node1 = node0;
			weight = 1.f;
		}
		if (!valid_real(out->position.x) || !valid_real(out->position.y) || !valid_real(out->position.z))
			out->position.x = out->position.y = out->position.z = 0.f;
		out->nodes[0] = (byte)node0;
		out->nodes[1] = (byte)node1;
		out->weight = (short)(PIN(valid_real(weight) ? weight : 1.f, 0.f, 1.f) * 32767.f);
	}
	mesh->vertex_count += vertex_buffer->count;

	/* its triangles: a strip's every one (turning, its degenerate ones left
	out), or a list's */
	{
		unsigned short const *list = (unsigned short const *)indices;

		for (index = 0; index + 2 < index_count && mesh->triangle_count < object_mesh_triangle_capacity;
			index += (triangle_buffer->type == 1) ? 1 : 3)
		{
			unsigned short a = list[index];
			unsigned short b = list[index + 1];
			unsigned short c = list[index + 2];
			unsigned short *out = mesh->triangles[mesh->triangle_count];

			if (a == b || b == c || a == c ||
				a >= vertex_buffer->count || b >= vertex_buffer->count || c >= vertex_buffer->count)
			{
				continue;
			}
			/* (a strip's odd triangles turn the other way) */
			if (triangle_buffer->type == 1 && (index & 1))
			{
				unsigned short swap = b;

				b = c;
				c = swap;
			}
			out[0] = (unsigned short)(first_vertex + a);
			out[1] = (unsigned short)(first_vertex + b);
			out[2] = (unsigned short)(first_vertex + c);
			mesh->triangle_count++;
		}
	}

	IDirect3DVertexBuffer8_Unlock((IDirect3DVertexBuffer8 *)vertex_buffer->hardware_format);
	IDirect3DIndexBuffer8_Unlock((IDirect3DIndexBuffer8 *)triangle_buffer->hardware_format);
	return TRUE;
}

/* the geometry a region's permutation is drawn with, its most detailed;
NULL: none */
static struct object_mesh_model_geometry const *object_mesh_region_geometry(
	struct model const *model,
	short region_index,
	byte const *permutations)
{
	struct model_region const *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);
	short permutation_index = permutations[region_index];
	struct model_region_permutation const *permutation;
	struct object_mesh_model_geometry const *geometry;
	short geometry_index = NONE;
	short level;

	if (!VALID_INDEX(permutation_index, region->permutations.count) || !region->permutations.address)
		return NULL;
	permutation = TAG_BLOCK_GET_ELEMENT(&region->permutations, permutation_index, struct model_region_permutation);
	for (level = OBJECT_MESH_DETAIL_LEVELS - 1; level >= 0 && !VALID_INDEX(geometry_index, model->geometries.count); level--)
		geometry_index = permutation->geometry_indices[level];
	if (!VALID_INDEX(geometry_index, model->geometries.count))
		return NULL;
	geometry = TAG_BLOCK_GET_ELEMENT(&model->geometries, geometry_index, struct object_mesh_model_geometry);
	if (geometry->parts.count <= 0 || geometry->parts.count > MAXIMUM_PARTS_PER_MODEL_GEOMETRY || !geometry->parts.address)
		return NULL;
	return geometry;
}

/* the memory for a mesh of the model's vertices and triangles, counted
first (bounded by OBJECT_MESH_MAXIMUM_...); FALSE: none */
static boolean object_mesh_reserve(
	struct object_mesh *mesh,
	struct model const *model,
	byte const *permutations)
{
	long vertex_total = 0;
	long triangle_total = 0;
	short region_index;

	for (region_index = 0; region_index < MIN(model->regions.count, MAXIMUM_REGIONS_PER_OBJECT); region_index++)
	{
		struct object_mesh_model_geometry const *geometry = object_mesh_region_geometry(model, region_index, permutations);
		long part_index;

		for (part_index = 0; geometry && part_index < geometry->parts.count; part_index++)
		{
			long vertices;
			long triangles;

			if (object_mesh_part_read(model, TAG_BLOCK_GET_ELEMENT(&geometry->parts, part_index,
				struct object_mesh_model_geometry_part), &vertices, &triangles))
			{
				vertex_total += vertices;
				triangle_total += triangles;
			}
		}
	}
	vertex_total = MIN(vertex_total, OBJECT_MESH_MAXIMUM_VERTICES);
	triangle_total = MIN(triangle_total, OBJECT_MESH_MAXIMUM_TRIANGLES);
	if (vertex_total <= 0 || triangle_total <= 0)
		return FALSE;
	if (vertex_total > object_mesh_vertex_capacity)
	{
		/* (the host's allocator, not the game's debug_realloc: that fills
		new memory from the local random seed, which effects and particle
		systems draw from, and the drawing would change them - the game
		state of a run with the legs on parted from one with them off) */
		void *grown = (realloc)(mesh->vertices, sizeof(*mesh->vertices) * vertex_total);

		if (!grown)
			return FALSE;
		mesh->vertices = grown;
		object_mesh_vertex_capacity = vertex_total;
	}
	if (triangle_total > object_mesh_triangle_capacity)
	{
		void *grown = (realloc)(mesh->triangles, sizeof(*mesh->triangles) * triangle_total);

		if (!grown)
			return FALSE;
		mesh->triangles = grown;
		object_mesh_triangle_capacity = triangle_total;
	}
	return TRUE;
}

static boolean object_mesh_build(
	struct object_mesh *mesh,
	long model_index,
	byte const *permutations)
{
	struct model *model = model_definition_get(model_index);
	short region_index;
	short node_index;

	mesh->model_index = model_index;
	mesh->vertex_count = 0;
	mesh->triangle_count = 0;
	if (!model || !model->nodes.address || model->nodes.count <= 0 || !model->regions.address ||
		model->geometries.count <= 0 || !model->geometries.address)
	{
		return FALSE;
	}
	if (!object_mesh_reserve(mesh, model, permutations))
		return FALSE;
	mesh->node_count = (short)MIN(model->nodes.count, OBJECT_MESH_MAXIMUM_NODES);
	for (node_index = 0; node_index < mesh->node_count; node_index++)
	{
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);

		mesh->inverse[node_index] = node->runtime_default_inverse_matrix;
	}
	for (region_index = 0; region_index < MIN(model->regions.count, MAXIMUM_REGIONS_PER_OBJECT); region_index++)
	{
		struct object_mesh_model_geometry const *geometry = object_mesh_region_geometry(model, region_index, permutations);
		long part_index;

		for (part_index = 0; geometry && part_index < geometry->parts.count; part_index++)
		{
			struct object_mesh_model_geometry_part const *part = TAG_BLOCK_GET_ELEMENT(
				&geometry->parts, part_index, struct object_mesh_model_geometry_part);

			/* (full: what it has) */
			if (!object_mesh_add_part(mesh, model, part))
				return mesh->triangle_count > 0;
		}
	}
	return mesh->triangle_count > 0;
}

/* ---------- public code */

void object_mesh_reset(
	void)
{
	object_mesh_kept.key = NONE;
}

long object_mesh_memory(
	void)
{
	return object_mesh_vertex_capacity * (long)sizeof(struct object_mesh_vertex) +
		object_mesh_triangle_capacity * (long)sizeof(unsigned short[3]);
}

struct object_mesh const *object_mesh_get(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	struct object_definition *definition;
	struct object_mesh *mesh = &object_mesh_kept;
	long model_index;
	long key;

	if (!object)
		return NULL;
	definition = object_definition_get(object->definition_index);
	model_index = definition ? definition->object.model.index : NONE;
	if (model_index == NONE)
		return NULL;
	key = object_mesh_key(model_index, object->object.region_permutations);
	if (mesh->key == key && mesh->model_index == model_index)
		return mesh->triangle_count > 0 ? mesh : NULL;
	mesh->key = key;
	/* (remembered when it has none, too) */
	if (!object_mesh_build(mesh, model_index, object->object.region_permutations))
		mesh->triangle_count = 0;
	return mesh->triangle_count > 0 ? mesh : NULL;
}
