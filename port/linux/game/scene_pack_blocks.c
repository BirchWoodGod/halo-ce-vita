/*
SCENE_PACK_BLOCKS.C

Forge's building blocks, made by the game (scene_pack_blocks.h says what
they are and how the builder takes them). Three tags a block - its
scenery, its model, its collision model - and two shaders for them all, laid
out as an Xbox map's tags at SCENE_PACK_BLOCKS_ADDRESS:
- the shapes: boxes (blocks, walls, platforms, bridges, columns), ramps (a
  box cut along its diagonal, rising toward +x) and a corner (an L of two
  walls), each a few convex pieces whose faces are its surfaces, every size a
  multiple of a quarter of a world unit (Forge's grid: the blocks tile edge to
  edge), the origin at the middle;
- the model: one node, one region, one geometry of one part (one draw), its
  vertices compressed as rasterizer_geometry_compress_vertices compresses
  them (the faces flat, their texture coordinates world units over the
  texture's size, so that blocks side by side continue one another) and its
  triangles one strip;
- the collision model: one node, one region, one material (thick metal: a
  metal's bullet holes and sounds), one bsp: a convex piece's faces a chain
  of nodes (each face's plane, in front of it a leaf that names its surface,
  behind the last one solid), two pieces split first by the plane between
  them (the leaves on each side naming the other piece's face on that plane
  where it shows), the surfaces' edges a ring each, shared with the next
  surface (collision_bsp.c walks them; the features of collision_features.c
  take the convex ones), as the tool's bsps are;
- the shaders: model shaders of our own (not alpha tested, no reflection)
  whose base map and detail map are those of the source map's environment
  shader that reads most like a wall (metal, panels), and of the one that
  reads most like a floor (plates, floors), or the same one.
Every read of the source is bounded (source_at); nothing made depends on
anything but the source's tags and these tables.
*/

/* ---------- headers */

#include "cseries.h"
#include "tag_files/tag_groups.h"
#include "bitmaps/bitmap_group.h"
#include "models/model_definitions.h"
#include "objects/object_definitions.h"
#include "objects/object_types.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/collision_model_definitions.h"

#include "scene_pack_blocks.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	TAG_INSTANCE_SIZE = 0x20,

	/* a made tag's root: room past the group's own (the walks of the
	layouts go no further than a group's root) */
	ROOT_PADDING = 0x100,

	/* the made tags: three a block, then the two shaders */
	TAGS_PER_BLOCK = 3,
	NUMBER_OF_MATERIALS = 2,

	/* a shape's most */
	MAXIMUM_MESH_VERTICES = 32,
	MAXIMUM_MESH_SURFACES = 16,
	MAXIMUM_SURFACE_POINTS = 8,
	MAXIMUM_MESH_EDGES = 64,
	MAXIMUM_MESH_PIECES = 2,
	MAXIMUM_LEAF_REFERENCES = 4,

	/* the source's environment shaders (tag_schema_render.c's
	shader_environment_definition: its base map at 0x88, its detail map
	function at 0xB0, the primary detail map's scale and reference after) */
	SHADER_ENVIRONMENT_SIZE = 0x344,
	SHADER_ENVIRONMENT_BASE_MAP = 0x88,
	SHADER_ENVIRONMENT_DETAIL_FUNCTION = 0xB0,
	SHADER_ENVIRONMENT_PRIMARY_DETAIL_SCALE = 0xB4,
	SHADER_ENVIRONMENT_PRIMARY_DETAIL_MAP = 0xB8,
	SHADER_ENVIRONMENT_SECONDARY_DETAIL_SCALE = 0xC8,
	SHADER_ENVIRONMENT_SECONDARY_DETAIL_MAP = 0xCC,
	SHADER_ENVIRONMENT_MICRO_DETAIL_FUNCTION = 0xF4,
	SHADER_ENVIRONMENT_MICRO_DETAIL_SCALE = 0xF8,
	SHADER_ENVIRONMENT_MICRO_DETAIL_MAP = 0xFC,
	/* and model shaders (tag_schema_render.c's shader_model_definition) */
	SHADER_MODEL_SIZE = 0x1B8,
	SHADER_MATERIAL_TYPE = 0x22,
	SHADER_TYPE = 0x24,
	SHADER_MODEL_FLAGS = 0x28,
	SHADER_MODEL_SELF_ILLUMINATION_PERIOD = 0x74,
	SHADER_MODEL_MAP_U_SCALE = 0x9C,
	SHADER_MODEL_MAP_V_SCALE = 0xA0,
	SHADER_MODEL_BASE_MAP = 0xA4,
	SHADER_MODEL_MULTIPURPOSE_MAP = 0xBC,
	SHADER_MODEL_DETAIL_FUNCTION = 0xD4,
	SHADER_MODEL_DETAIL_MAP_SCALE = 0xD8,
	SHADER_MODEL_DETAIL_MAP = 0xDC,
	SHADER_MODEL_DETAIL_MAP_V_SCALE = 0xEC,
	SHADER_MODEL_REFLECTION_CUBE_MAP = 0x164,
	/* (shaders.c) */
	SHADER_TYPE_MODEL = 4,
	/* (rasterizer_xbox_models.c's shader model flags) */
	SHADER_MODEL_NOT_ALPHA_TESTED_BIT = 2,
	/* (game_globals.h) */
	MATERIAL_THICK_METAL = 7,
	/* what a shader of the map's own level scores more */
	MATERIAL_OWN_MAP_SCORE = 10,
	/* (bitmaps.c's: a group of 2D textures, a 2D texture) */
	BITMAP_GROUP_TYPE_2D_TEXTURES = 0,
	BITMAP_TYPE_2D = 0,

	/* a model part (models.c's model_geometry_part) */
	MODEL_GEOMETRY_SIZE = 0x30,
	MODEL_GEOMETRY_PARTS = 0x24,
	MODEL_PART_SIZE = 0x68,
	MODEL_PART_SHADER_INDEX = 0x04,
	MODEL_PART_PREVIOUS = 0x06,
	MODEL_PART_NEXT = 0x07,
	MODEL_PART_CENTROID_PRIMARY_WEIGHT = 0x0C,
	MODEL_PART_TRIANGLE_TYPE = 0x44,
	MODEL_PART_TRIANGLE_COUNT = 0x48,
	MODEL_PART_VERTEX_TYPE = 0x54,
	MODEL_PART_VERTEX_COUNT = 0x58,
	/* (rasterizer_geometry.h: a precompiled strip; compressed model
	vertices, 32 bytes each) */
	TRIANGLE_BUFFER_STRIP = 1,
	VERTEX_TYPE_MODEL_COMPRESSED = 5,
	COMPRESSED_MODEL_VERTEX_SIZE = 32,
	MODEL_SHADER_REFERENCE_SIZE = 0x20,

	/* damage.c's damage region and its permutation (tag_schema_collision.c) */
	DAMAGE_REGION_SIZE = 0x54,
	DAMAGE_REGION_DESTROYED_EFFECT = 0x30,
	DAMAGE_REGION_PERMUTATIONS = 0x40,
	DAMAGE_PERMUTATION_SIZE = 0x20,
	COLLISION_LEAF_SIZE = 8,
	BSP2D_REFERENCE_SIZE = 8,
	MATERIAL_SIZE = 0x48,
};

/* (a ramp's slope, its normal's x and z: rise over run 1 and 1/4) */
#define SLOPE_HALF_SQRT2 0.707106769f
#define SLOPE_QUARTER_X 0.242535621f
#define SLOPE_QUARTER_Z 0.970142484f
/* a corner's walls' thickness */
#define CORNER_THICKNESS 0.25f
/* the world units a texture covers, walls' and floors' */
#define TEXTURE_SIZE 1.0f

/* which way round a triangle faces the camera when drawn: the strip's
triangles are counter-clockwise seen from outside (as the collision surfaces'
rings are) unless this says otherwise */
#define RENDER_CLOCKWISE TRUE

enum
{
	_material_walls = 0,
	_material_floors,
};

/* ---------- structures */

struct mesh_surface
{
	short count;
	short points[MAXIMUM_SURFACE_POINTS];
	real n[3];
	real d;
	short piece;
	/* (its ring's first edge) */
	short first_edge;
	/* (a surface on the plane between two pieces: named by the other
	side's leaves, not a node of its piece's chain) */
	boolean separator;
};

struct mesh_edge
{
	short vertices[2];
	short surfaces[2];
	short next[2];
};

struct mesh
{
	short vertex_count;
	real vertices[MAXIMUM_MESH_VERTICES][3];
	short surface_count;
	struct mesh_surface surfaces[MAXIMUM_MESH_SURFACES];
	short edge_count;
	struct mesh_edge edges[MAXIMUM_MESH_EDGES];
	/* (two pieces: the second in front of the plane, the first behind) */
	short piece_count;
	real separator_n[3];
	real separator_d;
	boolean failed;
};

struct made_part
{
	unsigned long address;
	unsigned char *vertices;
	unsigned long vertex_bytes;
	unsigned char *indices;
	unsigned long index_bytes;
};

struct scene_pack_blocks
{
	unsigned char *bytes;
	unsigned long size;
	unsigned long capacity;
	boolean failed;
	long source_count;
	long tag_count;
	unsigned long instances;
	struct made_part parts[SCENE_PACK_MAXIMUM_BLOCKS];
	short part_count;
	char material_names[NUMBER_OF_MATERIALS][256];
};

/* a bitmap chosen for a material: the source's tags (NONE: none) */
struct material_choice
{
	long shader;
	long base_map;
	long detail_map;
	short detail_function;
	real detail_scale;
	long score;
};

/* ---------- globals */

static struct scene_pack_block_info const scene_pack_blocks[] =
{
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "block_small", { 0.5f, 0.5f, 0.5f }, _scene_pack_block_box },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "block", { 1.0f, 1.0f, 1.0f }, _scene_pack_block_box },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "block_large", { 2.0f, 2.0f, 2.0f }, _scene_pack_block_box },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "wall_small", { 1.0f, 0.25f, 1.0f }, _scene_pack_block_box },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "wall", { 2.0f, 0.25f, 1.0f }, _scene_pack_block_box },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "wall_large", { 4.0f, 0.25f, 2.0f }, _scene_pack_block_box },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "platform_small", { 1.0f, 1.0f, 0.25f }, _scene_pack_block_box },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "platform", { 2.0f, 2.0f, 0.25f }, _scene_pack_block_box },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "platform_large", { 4.0f, 4.0f, 0.25f }, _scene_pack_block_box },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "bridge", { 4.0f, 1.0f, 0.25f }, _scene_pack_block_box },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "bridge_long", { 8.0f, 1.0f, 0.25f }, _scene_pack_block_box },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "column", { 0.5f, 0.5f, 2.0f }, _scene_pack_block_box },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "column_tall", { 0.5f, 0.5f, 4.0f }, _scene_pack_block_box },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "ramp_small", { 1.0f, 1.0f, 1.0f }, _scene_pack_block_ramp },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "ramp", { 2.0f, 2.0f, 2.0f }, _scene_pack_block_ramp },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "ramp_shallow", { 4.0f, 2.0f, 1.0f }, _scene_pack_block_ramp },
	{ SCENE_PACK_BLOCKS_PATH_PREFIX "corner", { 1.0f, 1.0f, 1.0f }, _scene_pack_block_corner },
};

#define NUMBER_OF_BLOCKS ((short)NUMBEROF(scene_pack_blocks))
typedef char verify_number_of_blocks[NUMBEROF(scene_pack_blocks) <= SCENE_PACK_MAXIMUM_BLOCKS ? 1 : -1];

static char const *const material_shader_names[NUMBER_OF_MATERIALS] =
{
	SCENE_PACK_BLOCKS_PATH_PREFIX "shaders\\walls",
	SCENE_PACK_BLOCKS_PATH_PREFIX "shaders\\floors",
};

/* what a source shader's name says (lower case): its score as a wall's,
as a floor's */
static struct
{
	char const *word;
	short walls;
	short floors;
} const material_words[] =
{
	{ "metal", 6, 6 },
	{ "panel", 4, 1 },
	{ "plate", 3, 4 },
	{ "floor", 1, 4 },
	{ "flat", 1, 4 },
	{ "generic", 2, 2 },
	{ "clean", 2, 2 },
	{ "concrete", 3, 3 },
	{ "wall", 2, 0 },
	{ "tile", 2, 3 },
	{ "strip", -3, -3 },
	{ "trim", -3, -3 },
	{ "pipe", -6, -6 },
	{ "grate", -3, -1 },
	{ "vent", -4, -4 },
	{ "decor", -2, -2 },
	{ "glass", -20, -20 },
	{ "light", -20, -20 },
	{ "decal", -20, -20 },
	{ "water", -20, -20 },
	{ "lava", -20, -20 },
	{ "sky", -20, -20 },
	{ "energy", -20, -20 },
	{ "holo", -20, -20 },
	{ "screen", -20, -20 },
	{ "glow", -20, -20 },
	{ "shield", -20, -20 },
	{ "tree", -20, -20 },
	{ "leaf", -20, -20 },
	{ "grass", -20, -20 },
	{ "snow", -20, -20 },
	{ "ice", -20, -20 },
	{ "cliff", -20, -20 },
	{ "rock", -20, -20 },
	{ "boulder", -20, -20 },
	{ "ground", -20, -20 },
	{ "dirt", -20, -20 },
	{ "sand", -20, -20 },
	{ "mud", -20, -20 },
	{ "moss", -20, -20 },
	{ "bark", -20, -20 },
	{ "plant", -20, -20 },
	{ "teleporter", -10, -10 },
	{ "flag", -10, -10 },
};

/* ---------- byte order (little-endian, as the maps and the packs) */

static unsigned long read_u32(unsigned char const *address)
{
	return (unsigned long)address[0] | ((unsigned long)address[1] << 8) | ((unsigned long)address[2] << 16) |
		((unsigned long)address[3] << 24);
}

static short read_s16(unsigned char const *address)
{
	return (short)(address[0] | (address[1] << 8));
}

static real read_real(unsigned char const *address)
{
	unsigned long bits = read_u32(address);
	real value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static void write_u32(unsigned char *address, unsigned long value)
{
	address[0] = (unsigned char)value;
	address[1] = (unsigned char)(value >> 8);
	address[2] = (unsigned char)(value >> 16);
	address[3] = (unsigned char)(value >> 24);
}

static void write_u16(unsigned char *address, unsigned short value)
{
	address[0] = (unsigned char)value;
	address[1] = (unsigned char)(value >> 8);
}

static void write_real(unsigned char *address, real value)
{
	unsigned long bits;

	memcpy(&bits, &value, sizeof(bits));
	write_u32(address, bits);
}

/* ---------- the set */

short scene_pack_blocks_count(
	void)
{
	return NUMBER_OF_BLOCKS;
}

struct scene_pack_block_info const *scene_pack_blocks_info(
	short index)
{
	return index >= 0 && index < NUMBER_OF_BLOCKS ? &scene_pack_blocks[index] : NULL;
}

static int lower(int character)
{
	return character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character;
}

short scene_pack_blocks_find(
	char const *path)
{
	short index;

	for (index = 0; path && index < NUMBER_OF_BLOCKS; index++)
	{
		char const *a = scene_pack_blocks[index].path;
		char const *b = path;

		while (*a && lower((unsigned char)*a) == lower((unsigned char)*b))
			a++, b++;
		if (!*a && !*b)
			return index;
	}
	return NONE;
}

static short block_material(
	struct scene_pack_block_info const *block)
{
	/* (what is walked on is a floor's: platforms, bridges, ramps) */
	if (block->kind == _scene_pack_block_ramp ||
		(block->kind == _scene_pack_block_box && block->size[2] < block->size[0] && block->size[2] < block->size[1]))
	{
		return _material_floors;
	}
	return _material_walls;
}

/* ---------- the source (anyone's: every read bounded) */

static unsigned char const *source_at(
	struct scene_pack_blocks_source const *source,
	unsigned long address,
	unsigned long size)
{
	unsigned long offset = address - source->address;

	if (address < source->address || offset > source->tags_size || size > source->tags_size - offset)
		return NULL;
	return source->tags + offset;
}

static unsigned char const *source_instance(
	struct scene_pack_blocks_source const *source,
	long index)
{
	if (index < 0 || index >= source->count)
		return NULL;
	return source_at(source, source->instances + (unsigned long)index * TAG_INSTANCE_SIZE, TAG_INSTANCE_SIZE);
}

/* a source tag's name (terminated within the tag data), or NULL */
static char const *source_name(
	struct scene_pack_blocks_source const *source,
	long index)
{
	unsigned char const *instance = source_instance(source, index);
	unsigned char const *name = instance ? source_at(source, read_u32(instance + 16), 1) : NULL;

	if (!name || !memchr(name, 0, source->tags_size - (unsigned long)(name - source->tags)))
		return NULL;
	return (char const *)name;
}

/* the source tag a tag index names, if it is of group: its index, or NONE */
static long source_tag(
	struct scene_pack_blocks_source const *source,
	unsigned long tag_index,
	unsigned long group_tag)
{
	long index = (short)(tag_index & 0xFFFF);
	unsigned char const *instance;

	if (tag_index == (unsigned long)NONE || index < 0 || index >= source->count)
		return NONE;
	instance = source_instance(source, index);
	if (!instance || read_u32(instance + 12) != tag_index || read_u32(instance) != group_tag)
		return NONE;
	return index;
}

/* a bitmap the blocks can draw with: a group of 2D textures, its first of
a size and format the game draws */
static boolean source_bitmap_usable(
	struct scene_pack_blocks_source const *source,
	long index)
{
	unsigned char const *instance = source_instance(source, index);
	unsigned char const *group = instance ? source_at(source, read_u32(instance + 20), sizeof(struct bitmap_group)) : NULL;
	unsigned char const *block;
	unsigned char const *bitmap;
	long count;
	short width, height;

	if (!group || read_s16(group + offsetof(struct bitmap_group, type)) != BITMAP_GROUP_TYPE_2D_TEXTURES)
		return FALSE;
	block = group + offsetof(struct bitmap_group, bitmaps);
	count = (long)read_u32(block);
	bitmap = count > 0 && count <= 0x10000 ? source_at(source, read_u32(block + 4), sizeof(struct bitmap_data)) : NULL;
	if (!bitmap || read_s16(bitmap + offsetof(struct bitmap_data, type)) != BITMAP_TYPE_2D)
		return FALSE;
	width = read_s16(bitmap + offsetof(struct bitmap_data, width));
	height = read_s16(bitmap + offsetof(struct bitmap_data, height));
	return width >= 32 && height >= 32 && width <= 2048 && height <= 2048 &&
		!(width & (width - 1)) && !(height & (height - 1));
}

/* what a name says of a shader as a material's (lower case, the words'
scores; one of the map's own level's shaders, levels\...\<map>\, is
the map's own look, its walls and floors) */
static long material_score(
	char const *name,
	char const *map,
	short material)
{
	char text[256];
	char own[64];
	long score = 0;
	short index;

	for (index = 0; name[index] && index < (short)sizeof(text) - 1; index++)
		text[index] = (char)lower((unsigned char)name[index]);
	text[index] = 0;
	if (map && *map && strlen(map) < sizeof(own) - 3)
	{
		snprintf(own, sizeof(own), "\\%s\\", map);
		for (index = 0; own[index]; index++)
			own[index] = (char)lower((unsigned char)own[index]);
		if (text[0] == 'l' && strstr(text, "levels\\") == text && strstr(text, own))
			score += MATERIAL_OWN_MAP_SCORE;
	}
	for (index = 0; index < (short)NUMBEROF(material_words); index++)
	{
		if (strstr(text, material_words[index].word))
			score += material == _material_walls ? material_words[index].walls : material_words[index].floors;
	}
	return score;
}

/* the best of the source's shaders of group (an environment shader's maps,
or a model shader's base map alone) for a material: the highest score, the
first in the source's order of those; one whose base map is avoid's
loses to any other that scores above nothing */
static void material_choose(
	struct scene_pack_blocks_source const *source,
	unsigned long group_tag,
	short material,
	long avoid,
	struct material_choice *choice)
{
	struct material_choice best;
	long index;

	best.shader = best.base_map = best.detail_map = NONE;
	best.score = 0;
	best.detail_function = 0;
	best.detail_scale = 1.0f;
	for (index = 0; index < source->count; index++)
	{
		unsigned char const *instance = source_instance(source, index);
		unsigned char const *shader;
		char const *name;
		struct material_choice candidate;
		boolean environment = group_tag == 'senv';

		if (!instance || read_u32(instance) != group_tag || !(name = source_name(source, index)))
			continue;
		shader = source_at(source, read_u32(instance + 20), environment ? SHADER_ENVIRONMENT_SIZE : SHADER_MODEL_SIZE);
		if (!shader)
			continue;
		candidate.shader = index;
		candidate.base_map = source_tag(source,
			read_u32(shader + (environment ? SHADER_ENVIRONMENT_BASE_MAP : SHADER_MODEL_BASE_MAP) + 12), 'bitm');
		if (candidate.base_map == NONE || !source_bitmap_usable(source, candidate.base_map))
			continue;
		candidate.detail_map = NONE;
		candidate.detail_function = 0;
		candidate.detail_scale = 1.0f;
		if (environment)
		{
			/* (its primary detail map, else its secondary, else its micro
			detail map: a scale that is no number, or none, or absurd, takes
			none) */
			static short const details[3][3] =
			{
				{ SHADER_ENVIRONMENT_PRIMARY_DETAIL_MAP, SHADER_ENVIRONMENT_PRIMARY_DETAIL_SCALE,
					SHADER_ENVIRONMENT_DETAIL_FUNCTION },
				{ SHADER_ENVIRONMENT_SECONDARY_DETAIL_MAP, SHADER_ENVIRONMENT_SECONDARY_DETAIL_SCALE,
					SHADER_ENVIRONMENT_DETAIL_FUNCTION },
				{ SHADER_ENVIRONMENT_MICRO_DETAIL_MAP, SHADER_ENVIRONMENT_MICRO_DETAIL_SCALE,
					SHADER_ENVIRONMENT_MICRO_DETAIL_FUNCTION },
			};
			short which;

			for (which = 0; which < 3 && candidate.detail_map == NONE; which++)
			{
				long detail = source_tag(source, read_u32(shader + details[which][0] + 12), 'bitm');
				real scale = read_real(shader + details[which][1]);
				short function = read_s16(shader + details[which][2]);

				if (detail != NONE && source_bitmap_usable(source, detail) && scale > 0.0f && scale <= 64.0f &&
					function >= 0 && function <= 2)
				{
					candidate.detail_map = detail;
					candidate.detail_function = function;
					candidate.detail_scale = scale;
				}
			}
		}
		candidate.score = 100 + material_score(name, source->map, material);
		if (avoid != NONE && candidate.base_map == avoid)
			candidate.score -= 50;
		if (candidate.score > best.score)
			best = candidate;
	}
	*choice = best;
}

/* ---------- the made tags: their bytes, at their own addresses */

static unsigned long made_alloc(
	struct scene_pack_blocks *blocks,
	unsigned long size)
{
	unsigned long offset = (blocks->size + 3) & ~3UL;
	unsigned long end = offset + ((size + 3) & ~3UL);

	if (blocks->failed || end < offset || end > 16 * 1024 * 1024)
	{
		blocks->failed = TRUE;
		return 0;
	}
	if (end > blocks->capacity)
	{
		unsigned long capacity = blocks->capacity ? blocks->capacity : 64 * 1024;
		unsigned char *grown;

		while (capacity < end)
			capacity *= 2;
		grown = realloc(blocks->bytes, capacity);
		if (!grown)
		{
			blocks->failed = TRUE;
			return 0;
		}
		memset(grown + blocks->capacity, 0, capacity - blocks->capacity);
		blocks->bytes = grown;
		blocks->capacity = capacity;
	}
	blocks->size = end;
	return SCENE_PACK_BLOCKS_ADDRESS + offset;
}

/* (the bytes at a made address; only after the allocations that may move
them) */
static unsigned char *made_at(
	struct scene_pack_blocks *blocks,
	unsigned long address)
{
	return blocks->bytes + (address - SCENE_PACK_BLOCKS_ADDRESS);
}

static unsigned long made_tag_index(
	struct scene_pack_blocks const *blocks,
	long tag)
{
	long index = blocks->source_count + tag;

	return ((unsigned long)(0xB10C + tag) << 16) | (unsigned long)index;
}

static unsigned long made_string(
	struct scene_pack_blocks *blocks,
	char const *text)
{
	unsigned long length = (unsigned long)strlen(text) + 1;
	unsigned long address = made_alloc(blocks, length);

	if (!blocks->failed)
		memcpy(made_at(blocks, address), text, length);
	return address;
}

/* a made tag (its instance, name and root): its root's address */
static unsigned long made_tag(
	struct scene_pack_blocks *blocks,
	long tag,
	unsigned long group_tag,
	unsigned long parent_group_tag,
	char const *name,
	unsigned long root_size)
{
	unsigned long name_address = made_string(blocks, name);
	unsigned long root = made_alloc(blocks, root_size + ROOT_PADDING);
	unsigned char *instance;

	if (blocks->failed)
		return 0;
	instance = made_at(blocks, blocks->instances + (unsigned long)tag * TAG_INSTANCE_SIZE);
	write_u32(instance + 0, group_tag);
	write_u32(instance + 4, parent_group_tag);
	write_u32(instance + 8, (unsigned long)NONE);
	write_u32(instance + 12, made_tag_index(blocks, tag));
	write_u32(instance + 16, name_address);
	write_u32(instance + 20, root);
	return root;
}

/* a reference at address to a made tag (NONE: to none of group) */
static void made_reference(
	struct scene_pack_blocks *blocks,
	unsigned long address,
	unsigned long group_tag,
	long tag,
	unsigned long source_index)
{
	unsigned char *reference = made_at(blocks, address);

	write_u32(reference + 0, group_tag);
	write_u32(reference + 4, 0);
	write_u32(reference + 8, 0);
	write_u32(reference + 12, tag != NONE ? made_tag_index(blocks, tag) : source_index);
}

/* a block at address of count elements of size: their address */
static unsigned long made_block(
	struct scene_pack_blocks *blocks,
	unsigned long address,
	long count,
	unsigned long size)
{
	unsigned long elements = count > 0 ? made_alloc(blocks, (unsigned long)count * size) : 0;

	if (blocks->failed)
		return 0;
	write_u32(made_at(blocks, address), (unsigned long)count);
	write_u32(made_at(blocks, address) + 4, elements);
	write_u32(made_at(blocks, address) + 8, 0);
	return elements;
}

static void made_name(
	struct scene_pack_blocks *blocks,
	unsigned long address,
	char const *name)
{
	snprintf((char *)made_at(blocks, address), TAG_STRING_LENGTH + 1, "%s", name);
}

/* ---------- the shapes */

static short mesh_vertex(
	struct mesh *mesh,
	real x,
	real y,
	real z)
{
	short index;

	for (index = 0; index < mesh->vertex_count; index++)
	{
		if (mesh->vertices[index][0] == x && mesh->vertices[index][1] == y && mesh->vertices[index][2] == z)
			return index;
	}
	if (mesh->vertex_count >= MAXIMUM_MESH_VERTICES)
	{
		mesh->failed = TRUE;
		return 0;
	}
	mesh->vertices[index][0] = x;
	mesh->vertices[index][1] = y;
	mesh->vertices[index][2] = z;
	mesh->vertex_count++;
	return index;
}

/* the angle's order of a 2D vector (a pseudo-angle: no trigonometry) */
static real pseudo_angle(
	real x,
	real y)
{
	real sum = (x < 0.0f ? -x : x) + (y < 0.0f ? -y : y);
	real p = sum > 0.0f ? y / sum : 0.0f;

	if (x < 0.0f)
		return 2.0f - p;
	return y < 0.0f ? 4.0f + p : p;
}

/* a surface of the points (a convex polygon on the plane n, d), put in
order counter-clockwise seen from n's side */
static void mesh_surface(
	struct mesh *mesh,
	short piece,
	boolean separator,
	real nx,
	real ny,
	real nz,
	real d,
	short count,
	real const points[][3])
{
	struct mesh_surface *surface;
	real center[3] = { 0.0f, 0.0f, 0.0f };
	real u[3], v[3];
	real angles[MAXIMUM_SURFACE_POINTS];
	short order[MAXIMUM_SURFACE_POINTS];
	short index, other;

	if (mesh->surface_count >= MAXIMUM_MESH_SURFACES || count < 3 || count > MAXIMUM_SURFACE_POINTS)
	{
		mesh->failed = TRUE;
		return;
	}
	surface = &mesh->surfaces[mesh->surface_count++];
	surface->count = count;
	surface->n[0] = nx;
	surface->n[1] = ny;
	surface->n[2] = nz;
	surface->d = d;
	surface->piece = piece;
	surface->separator = separator;
	for (index = 0; index < count; index++)
	{
		center[0] += points[index][0];
		center[1] += points[index][1];
		center[2] += points[index][2];
	}
	center[0] /= (real)count;
	center[1] /= (real)count;
	center[2] /= (real)count;
	/* (u across the plane, v = n x u: from u toward v is counter-clockwise
	seen from n's side) */
	if (nz > 0.9f || nz < -0.9f)
		u[0] = 1.0f, u[1] = 0.0f, u[2] = 0.0f;
	else
		u[0] = -ny, u[1] = nx, u[2] = 0.0f;
	v[0] = ny * u[2] - nz * u[1];
	v[1] = nz * u[0] - nx * u[2];
	v[2] = nx * u[1] - ny * u[0];
	for (index = 0; index < count; index++)
	{
		real dx = points[index][0] - center[0];
		real dy = points[index][1] - center[1];
		real dz = points[index][2] - center[2];

		angles[index] = pseudo_angle(dx * u[0] + dy * u[1] + dz * u[2], dx * v[0] + dy * v[1] + dz * v[2]);
		order[index] = index;
	}
	for (index = 1; index < count; index++)
	{
		for (other = index; other > 0 && angles[order[other]] < angles[order[other - 1]]; other--)
		{
			short swap = order[other];

			order[other] = order[other - 1];
			order[other - 1] = swap;
		}
	}
	for (index = 0; index < count; index++)
		surface->points[index] = mesh_vertex(mesh, points[order[index]][0], points[order[index]][1], points[order[index]][2]);
}

/* (a box of the corners, as a piece: its six faces) */
static void mesh_box(
	struct mesh *mesh,
	short piece,
	real x0,
	real x1,
	real y0,
	real y1,
	real z0,
	real z1)
{
	real face[4][3];

#define FACE(i, x, y, z) (face[i][0] = (x), face[i][1] = (y), face[i][2] = (z))
	FACE(0, x1, y0, z0); FACE(1, x1, y1, z0); FACE(2, x1, y1, z1); FACE(3, x1, y0, z1);
	mesh_surface(mesh, piece, FALSE, 1.0f, 0.0f, 0.0f, x1, 4, face);
	FACE(0, x0, y0, z0); FACE(1, x0, y1, z0); FACE(2, x0, y1, z1); FACE(3, x0, y0, z1);
	mesh_surface(mesh, piece, FALSE, -1.0f, 0.0f, 0.0f, -x0, 4, face);
	FACE(0, x0, y1, z0); FACE(1, x1, y1, z0); FACE(2, x1, y1, z1); FACE(3, x0, y1, z1);
	mesh_surface(mesh, piece, FALSE, 0.0f, 1.0f, 0.0f, y1, 4, face);
	FACE(0, x0, y0, z0); FACE(1, x1, y0, z0); FACE(2, x1, y0, z1); FACE(3, x0, y0, z1);
	mesh_surface(mesh, piece, FALSE, 0.0f, -1.0f, 0.0f, -y0, 4, face);
	FACE(0, x0, y0, z1); FACE(1, x1, y0, z1); FACE(2, x1, y1, z1); FACE(3, x0, y1, z1);
	mesh_surface(mesh, piece, FALSE, 0.0f, 0.0f, 1.0f, z1, 4, face);
	FACE(0, x0, y0, z0); FACE(1, x1, y0, z0); FACE(2, x1, y1, z0); FACE(3, x0, y1, z0);
	mesh_surface(mesh, piece, FALSE, 0.0f, 0.0f, -1.0f, -z0, 4, face);
}

/* the block's shape: its surfaces, edges and pieces */
static boolean mesh_make(
	struct scene_pack_block_info const *block,
	struct mesh *mesh)
{
	real hx = block->size[0] * 0.5f;
	real hy = block->size[1] * 0.5f;
	real hz = block->size[2] * 0.5f;
	real face[MAXIMUM_SURFACE_POINTS][3];
	short surface_index;

	memset(mesh, 0, sizeof(*mesh));
	mesh->piece_count = 1;
	switch (block->kind)
	{
	case _scene_pack_block_box:
		mesh_box(mesh, 0, -hx, hx, -hy, hy, -hz, hz);
		break;
	case _scene_pack_block_ramp:
	{
		/* (rising toward +x: its slope through the middle, d none) */
		real slope_x, slope_z;

		if (block->size[0] == block->size[2])
			slope_x = -SLOPE_HALF_SQRT2, slope_z = SLOPE_HALF_SQRT2;
		else if (block->size[0] == 4.0f * block->size[2])
			slope_x = -SLOPE_QUARTER_X, slope_z = SLOPE_QUARTER_Z;
		else
			return FALSE;
		FACE(0, -hx, -hy, -hz); FACE(1, hx, -hy, -hz); FACE(2, hx, hy, -hz); FACE(3, -hx, hy, -hz);
		mesh_surface(mesh, 0, FALSE, 0.0f, 0.0f, -1.0f, hz, 4, face);
		FACE(0, hx, -hy, -hz); FACE(1, hx, hy, -hz); FACE(2, hx, hy, hz); FACE(3, hx, -hy, hz);
		mesh_surface(mesh, 0, FALSE, 1.0f, 0.0f, 0.0f, hx, 4, face);
		FACE(0, -hx, -hy, -hz); FACE(1, -hx, hy, -hz); FACE(2, hx, hy, hz); FACE(3, hx, -hy, hz);
		mesh_surface(mesh, 0, FALSE, slope_x, 0.0f, slope_z, 0.0f, 4, face);
		FACE(0, -hx, hy, -hz); FACE(1, hx, hy, -hz); FACE(2, hx, hy, hz);
		mesh_surface(mesh, 0, FALSE, 0.0f, 1.0f, 0.0f, hy, 3, face);
		FACE(0, -hx, -hy, -hz); FACE(1, hx, -hy, -hz); FACE(2, hx, -hy, hz);
		mesh_surface(mesh, 0, FALSE, 0.0f, -1.0f, 0.0f, hy, 3, face);
		break;
	}
	case _scene_pack_block_corner:
	{
		/* (two walls of an L: piece 0 along x at -y, piece 1 along y at -x,
		in front of the plane y = ys between them; the first's face on that
		plane shows from xs on, the second's is against the first) */
		real ys = -hy + CORNER_THICKNESS;
		real xs = -hx + CORNER_THICKNESS;

		mesh->piece_count = 2;
		mesh->separator_n[0] = 0.0f;
		mesh->separator_n[1] = 1.0f;
		mesh->separator_n[2] = 0.0f;
		mesh->separator_d = ys;
		/* piece 0: its top and bottom (the point where the second meets it
		too), its -y, +x and -x faces, its +y face where it shows */
		FACE(0, -hx, -hy, hz); FACE(1, hx, -hy, hz); FACE(2, hx, ys, hz); FACE(3, xs, ys, hz); FACE(4, -hx, ys, hz);
		mesh_surface(mesh, 0, FALSE, 0.0f, 0.0f, 1.0f, hz, 5, face);
		FACE(0, -hx, -hy, -hz); FACE(1, hx, -hy, -hz); FACE(2, hx, ys, -hz); FACE(3, xs, ys, -hz); FACE(4, -hx, ys, -hz);
		mesh_surface(mesh, 0, FALSE, 0.0f, 0.0f, -1.0f, hz, 5, face);
		FACE(0, -hx, -hy, -hz); FACE(1, hx, -hy, -hz); FACE(2, hx, -hy, hz); FACE(3, -hx, -hy, hz);
		mesh_surface(mesh, 0, FALSE, 0.0f, -1.0f, 0.0f, hy, 4, face);
		FACE(0, hx, -hy, -hz); FACE(1, hx, ys, -hz); FACE(2, hx, ys, hz); FACE(3, hx, -hy, hz);
		mesh_surface(mesh, 0, FALSE, 1.0f, 0.0f, 0.0f, hx, 4, face);
		FACE(0, -hx, -hy, -hz); FACE(1, -hx, ys, -hz); FACE(2, -hx, ys, hz); FACE(3, -hx, -hy, hz);
		mesh_surface(mesh, 0, FALSE, -1.0f, 0.0f, 0.0f, hx, 4, face);
		FACE(0, xs, ys, -hz); FACE(1, hx, ys, -hz); FACE(2, hx, ys, hz); FACE(3, xs, ys, hz);
		mesh_surface(mesh, 0, TRUE, 0.0f, 1.0f, 0.0f, ys, 4, face);
		/* piece 1: its top and bottom, its +y, -x and +x faces */
		FACE(0, -hx, ys, hz); FACE(1, xs, ys, hz); FACE(2, xs, hy, hz); FACE(3, -hx, hy, hz);
		mesh_surface(mesh, 1, FALSE, 0.0f, 0.0f, 1.0f, hz, 4, face);
		FACE(0, -hx, ys, -hz); FACE(1, xs, ys, -hz); FACE(2, xs, hy, -hz); FACE(3, -hx, hy, -hz);
		mesh_surface(mesh, 1, FALSE, 0.0f, 0.0f, -1.0f, hz, 4, face);
		FACE(0, -hx, hy, -hz); FACE(1, xs, hy, -hz); FACE(2, xs, hy, hz); FACE(3, -hx, hy, hz);
		mesh_surface(mesh, 1, FALSE, 0.0f, 1.0f, 0.0f, hy, 4, face);
		FACE(0, -hx, ys, -hz); FACE(1, -hx, hy, -hz); FACE(2, -hx, hy, hz); FACE(3, -hx, ys, hz);
		mesh_surface(mesh, 1, FALSE, -1.0f, 0.0f, 0.0f, hx, 4, face);
		FACE(0, xs, ys, -hz); FACE(1, xs, hy, -hz); FACE(2, xs, hy, hz); FACE(3, xs, ys, hz);
		mesh_surface(mesh, 1, FALSE, 1.0f, 0.0f, 0.0f, xs, 4, face);
		break;
	}
	default:
		return FALSE;
	}
#undef FACE
	if (mesh->failed)
		return FALSE;

	/* the edges: each surface's ring, an edge shared with the surface that
	goes along it the other way */
	for (surface_index = 0; surface_index < mesh->surface_count; surface_index++)
	{
		struct mesh_surface *surface = &mesh->surfaces[surface_index];
		short ring[MAXIMUM_SURFACE_POINTS];
		short point;

		for (point = 0; point < surface->count; point++)
		{
			short a = surface->points[point];
			short b = surface->points[(point + 1) % surface->count];
			short edge_index;

			for (edge_index = 0; edge_index < mesh->edge_count; edge_index++)
			{
				struct mesh_edge const *edge = &mesh->edges[edge_index];

				if ((edge->vertices[0] == a && edge->vertices[1] == b) ||
					(edge->vertices[0] == b && edge->vertices[1] == a))
				{
					break;
				}
			}
			if (edge_index < mesh->edge_count)
			{
				struct mesh_edge *edge = &mesh->edges[edge_index];

				/* (the other way, once: else the shape is not closed) */
				if (edge->vertices[0] != b || edge->surfaces[1] != NONE)
					return FALSE;
				edge->surfaces[1] = surface_index;
			}
			else
			{
				struct mesh_edge *edge;

				if (mesh->edge_count >= MAXIMUM_MESH_EDGES)
					return FALSE;
				edge = &mesh->edges[mesh->edge_count++];
				edge->vertices[0] = a;
				edge->vertices[1] = b;
				edge->surfaces[0] = surface_index;
				edge->surfaces[1] = NONE;
				edge->next[0] = edge->next[1] = edge_index;
			}
			ring[point] = edge_index;
		}
		for (point = 0; point < surface->count; point++)
		{
			struct mesh_edge *edge = &mesh->edges[ring[point]];

			edge->next[edge->surfaces[1] == surface_index] = ring[(point + 1) % surface->count];
		}
		surface->first_edge = ring[0];
	}
	return TRUE;
}

/* ---------- the model */

static unsigned long compress_vector(real const *vector)
{
	/* (rasterizer_geometry.c's compress_real_vector3d_to_int32_clamp) */
	real i = vector[0] < -1.f ? -1.f : vector[0] > 1.f ? 1.f : vector[0];
	real j = vector[1] < -1.f ? -1.f : vector[1] > 1.f ? 1.f : vector[1];
	real k = vector[2] < -1.f ? -1.f : vector[2] > 1.f ? 1.f : vector[2];
	long ci = (long)floor((double)i * 1023.5f) & 0x7FF;
	long cj = (long)floor((double)j * 1023.5f) & 0x7FF;
	long ck = (long)floor((double)k * 511.5f) & 0x3FF;

	return (unsigned long)((((ck << 11) | cj) << 11) | ci);
}

static short compress_real(real value)
{
	/* (compress_real_to_int16_clamp) */
	real clamped = value < -1.f ? -1.f : value > 1.f ? 1.f : value;

	return (short)(long)floor((double)clamped * 32767.5f);
}

/* a surface's texture axes: across (right, as seen from in front) and down,
in world units over the texture's size */
static void surface_texture_axes(
	struct mesh_surface const *surface,
	real across[3],
	real down[3])
{
	real const *n = surface->n;
	real an[3];

	an[0] = n[0] < 0.0f ? -n[0] : n[0];
	an[1] = n[1] < 0.0f ? -n[1] : n[1];
	an[2] = n[2] < 0.0f ? -n[2] : n[2];
	if (an[2] >= an[0] && an[2] >= an[1])
	{
		/* (a top or a bottom, or a ramp's slope: along x and y) */
		across[0] = 1.0f, across[1] = 0.0f, across[2] = 0.0f;
		down[0] = 0.0f, down[1] = n[2] > 0.0f ? -1.0f : 1.0f, down[2] = 0.0f;
	}
	else if (an[0] >= an[1])
	{
		across[0] = 0.0f, across[1] = n[0] > 0.0f ? 1.0f : -1.0f, across[2] = 0.0f;
		down[0] = 0.0f, down[1] = 0.0f, down[2] = -1.0f;
	}
	else
	{
		across[0] = n[1] > 0.0f ? -1.0f : 1.0f, across[1] = 0.0f, across[2] = 0.0f;
		down[0] = 0.0f, down[1] = 0.0f, down[2] = -1.0f;
	}
}

/* a strip's next triangle (a, b, c counter-clockwise seen from outside),
joined to what is there by repeated indices, turned as its place in the
strip turns it */
static void strip_triangle(
	unsigned short *strip,
	long *count,
	unsigned short a,
	unsigned short b,
	unsigned short c)
{
	unsigned short swap;

	if (RENDER_CLOCKWISE)
		swap = b, b = c, c = swap;
	if (*count)
	{
		unsigned short last = strip[*count - 1];

		strip[(*count)++] = last;
		strip[(*count)++] = a;
		/* (the triangle's place odd: drawn the other way round, so given
		the other way round) */
		if ((*count) & 1)
			strip[(*count)++] = a;
	}
	strip[(*count)++] = a;
	strip[(*count)++] = b;
	strip[(*count)++] = c;
}

/* the block's part's vertices and strip (its texture coordinates over
scale, which the model's base map scale gives back) */
static boolean part_make(
	struct mesh const *mesh,
	struct made_part *part,
	real *scale,
	long *vertex_count,
	long *triangle_count)
{
	real largest = 0.0f;
	long vertices = 0, indices = 0;
	short surface_index;
	unsigned short *strip;
	unsigned char *out;

	for (surface_index = 0; surface_index < mesh->surface_count; surface_index++)
	{
		struct mesh_surface const *surface = &mesh->surfaces[surface_index];
		real across[3], down[3];
		short point;

		surface_texture_axes(surface, across, down);
		for (point = 0; point < surface->count; point++)
		{
			real const *p = mesh->vertices[surface->points[point]];
			real u = (p[0] * across[0] + p[1] * across[1] + p[2] * across[2]) / TEXTURE_SIZE;
			real v = (p[0] * down[0] + p[1] * down[1] + p[2] * down[2]) / TEXTURE_SIZE;

			u = u < 0.0f ? -u : u;
			v = v < 0.0f ? -v : v;
			largest = u > largest ? u : largest;
			largest = v > largest ? v : largest;
		}
		vertices += surface->count;
		/* (each triangle at most 3 joining indices and its own 3) */
		indices += (surface->count - 2) * 6;
	}
	for (*scale = 1.0f; *scale < largest; *scale *= 2.0f);

	/* (malloc's, not calloc's: the game's free is its debug allocator's,
	which calloc's blocks are not) */
	part->vertices = malloc((size_t)vertices * COMPRESSED_MODEL_VERTEX_SIZE);
	strip = malloc((size_t)indices * sizeof(unsigned short));
	if (!part->vertices || !strip || vertices > 0xFFFF)
	{
		if (part->vertices)
			free(part->vertices);
		if (strip)
			free(strip);
		part->vertices = NULL;
		return FALSE;
	}
	memset(part->vertices, 0, (size_t)vertices * COMPRESSED_MODEL_VERTEX_SIZE);
	out = part->vertices;
	vertices = 0;
	indices = 0;
	for (surface_index = 0; surface_index < mesh->surface_count; surface_index++)
	{
		struct mesh_surface const *surface = &mesh->surfaces[surface_index];
		real across[3], down[3], binormal[3];
		long first = vertices;
		short point;

		surface_texture_axes(surface, across, down);
		binormal[0] = -down[0], binormal[1] = -down[1], binormal[2] = -down[2];
		for (point = 0; point < surface->count; point++)
		{
			real const *p = mesh->vertices[surface->points[point]];
			real u = (p[0] * across[0] + p[1] * across[1] + p[2] * across[2]) / TEXTURE_SIZE;
			real v = (p[0] * down[0] + p[1] * down[1] + p[2] * down[2]) / TEXTURE_SIZE;

			/* (rasterizer_geometry_compress_vertices: the position, the
			normal, binormal and tangent packed, the texture coordinates as
			shorts, the nodes times three, the first node's weight) */
			write_real(out + 0, p[0]);
			write_real(out + 4, p[1]);
			write_real(out + 8, p[2]);
			write_u32(out + 12, compress_vector(surface->n));
			write_u32(out + 16, compress_vector(binormal));
			write_u32(out + 20, compress_vector(across));
			write_u16(out + 24, (unsigned short)compress_real(u / *scale));
			write_u16(out + 26, (unsigned short)compress_real(v / *scale));
			out[28] = 0;
			out[29] = 0;
			write_u16(out + 30, (unsigned short)compress_real(1.0f));
			out += COMPRESSED_MODEL_VERTEX_SIZE;
			vertices++;
		}
		for (point = 1; point + 1 < surface->count; point++)
			strip_triangle(strip, &indices, (unsigned short)first, (unsigned short)(first + point),
				(unsigned short)(first + point + 1));
	}
	part->vertex_bytes = (unsigned long)vertices * COMPRESSED_MODEL_VERTEX_SIZE;
	part->indices = (unsigned char *)strip;
	part->index_bytes = (unsigned long)indices * sizeof(unsigned short);
	/* (little-endian, as the buffers are: the strip written so) */
	{
		long index;

		for (index = 0; index < indices; index++)
		{
			unsigned short value = strip[index];

			write_u16(part->indices + index * 2, value);
		}
	}
	*vertex_count = vertices;
	*triangle_count = indices - 2;
	return TRUE;
}

/* the smallest radius in 64ths of a world unit that holds every vertex
(exact: the vertices are in quarters, the radius found by whole numbers) */
static real mesh_radius(
	struct mesh const *mesh)
{
	double most = 0.0;
	long radius;
	short index;

	for (index = 0; index < mesh->vertex_count; index++)
	{
		double x = mesh->vertices[index][0], y = mesh->vertices[index][1], z = mesh->vertices[index][2];
		double squared = x * x + y * y + z * z;

		most = squared > most ? squared : most;
	}
	for (radius = 1; (double)radius * (double)radius < most * 4096.0; radius++);
	return (real)radius / 64.0f;
}

static void made_model(
	struct scene_pack_blocks *blocks,
	long tag,
	struct scene_pack_block_info const *block,
	struct mesh const *mesh,
	long shader_tag)
{
	unsigned long root = made_tag(blocks, tag, 'mode', NONE, block->path, sizeof(struct model));
	unsigned long node, region, permutation, geometry, part, shader;
	struct made_part *made;
	real scale;
	long vertex_count, triangle_count;
	short index;

	if (blocks->failed || blocks->part_count >= SCENE_PACK_MAXIMUM_BLOCKS)
	{
		blocks->failed = TRUE;
		return;
	}
	made = &blocks->parts[blocks->part_count];
	if (!part_make(mesh, made, &scale, &vertex_count, &triangle_count))
	{
		blocks->failed = TRUE;
		return;
	}
	blocks->part_count++;
	/* (every level of detail the same geometry: none cut off) */
	write_real(made_at(blocks, root) + offsetof(struct model, base_map_scale), scale);
	write_real(made_at(blocks, root) + offsetof(struct model, base_map_scale) + 4, scale);

	node = made_block(blocks, root + offsetof(struct model, nodes), 1, sizeof(struct model_node));
	if (blocks->failed)
		return;
	made_name(blocks, node, "frame");
	write_u16(made_at(blocks, node) + offsetof(struct model_node, next_sibling_node_index), 0xFFFF);
	write_u16(made_at(blocks, node) + offsetof(struct model_node, first_child_node_index), 0xFFFF);
	write_u16(made_at(blocks, node) + offsetof(struct model_node, parent_node_index), 0xFFFF);
	write_real(made_at(blocks, node) + offsetof(struct model_node, default_rotation) + 12, 1.0f);
	write_real(made_at(blocks, node) + offsetof(struct model_node, runtime_default_inverse_matrix), 1.0f);
	write_real(made_at(blocks, node) + offsetof(struct model_node, runtime_default_inverse_matrix) + 4, 1.0f);
	write_real(made_at(blocks, node) + offsetof(struct model_node, runtime_default_inverse_matrix) + 20, 1.0f);
	write_real(made_at(blocks, node) + offsetof(struct model_node, runtime_default_inverse_matrix) + 36, 1.0f);

	region = made_block(blocks, root + offsetof(struct model, regions), 1, sizeof(struct model_region));
	if (blocks->failed)
		return;
	made_name(blocks, region, "base");
	permutation = made_block(blocks, region + offsetof(struct model_region, permutations), 1,
		sizeof(struct model_region_permutation));
	if (blocks->failed)
		return;
	made_name(blocks, permutation, "base");
	for (index = 0; index < 5; index++)
		write_u16(made_at(blocks, permutation) + offsetof(struct model_region_permutation, geometry_indices) + index * 2, 0);

	geometry = made_block(blocks, root + offsetof(struct model, geometries), 1, MODEL_GEOMETRY_SIZE);
	part = blocks->failed ? 0 : made_block(blocks, geometry + MODEL_GEOMETRY_PARTS, 1, MODEL_PART_SIZE);
	if (blocks->failed)
		return;
	made->address = part;
	write_u16(made_at(blocks, part) + MODEL_PART_SHADER_INDEX, 0);
	made_at(blocks, part)[MODEL_PART_PREVIOUS] = 0xFF;
	made_at(blocks, part)[MODEL_PART_NEXT] = 0xFF;
	write_real(made_at(blocks, part) + MODEL_PART_CENTROID_PRIMARY_WEIGHT, 1.0f);
	write_u16(made_at(blocks, part) + MODEL_PART_TRIANGLE_TYPE, TRIANGLE_BUFFER_STRIP);
	write_u32(made_at(blocks, part) + MODEL_PART_TRIANGLE_COUNT, (unsigned long)triangle_count);
	write_u16(made_at(blocks, part) + MODEL_PART_VERTEX_TYPE, VERTEX_TYPE_MODEL_COMPRESSED);
	write_u32(made_at(blocks, part) + MODEL_PART_VERTEX_COUNT, (unsigned long)vertex_count);

	shader = made_block(blocks, root + offsetof(struct model, shaders), 1, MODEL_SHADER_REFERENCE_SIZE);
	if (blocks->failed)
		return;
	made_reference(blocks, shader, 'soso', shader_tag, 0);
}

/* ---------- the collision model */

static void made_collision_model(
	struct scene_pack_blocks *blocks,
	long tag,
	struct scene_pack_block_info const *block,
	struct mesh const *mesh)
{
	unsigned long root = made_tag(blocks, tag, 'coll', NONE, block->path, sizeof(struct collision_model));
	unsigned long material, region, permutation, node, bsp;
	unsigned long planes, nodes, leaves, references, surfaces, edges, vertices;
	/* each surface's plane (the chains' and the separator's), its node */
	long surface_planes[MAXIMUM_MESH_SURFACES];
	long chain_first[MAXIMUM_MESH_PIECES];
	long plane_count = 0, node_count = 0, leaf_count = 0, reference_count = 0;
	short surface_index, piece;
	unsigned char *at;

	if (blocks->failed)
		return;
	at = made_at(blocks, root);
	/* (no effect, no vitality: nothing damages a block) */
	write_u16(at + offsetof(struct collision_model, resistance.indirect_damage_material_index), 0xFFFF);
	made_reference(blocks, root + offsetof(struct collision_model, resistance.localized_damage_effect), 'effe', NONE,
		(unsigned long)NONE);
	made_reference(blocks, root + offsetof(struct collision_model, resistance.area_damage_effect), 'effe', NONE,
		(unsigned long)NONE);
	made_reference(blocks, root + offsetof(struct collision_model, resistance.body_damaged_effect), 'effe', NONE,
		(unsigned long)NONE);
	made_reference(blocks, root + offsetof(struct collision_model, resistance.body_depleted_effect), 'effe', NONE,
		(unsigned long)NONE);
	made_reference(blocks, root + offsetof(struct collision_model, resistance.body_destroyed_effect), 'effe', NONE,
		(unsigned long)NONE);
	made_reference(blocks, root + offsetof(struct collision_model, resistance.shield_damaged_effect), 'effe', NONE,
		(unsigned long)NONE);
	made_reference(blocks, root + offsetof(struct collision_model, resistance.shield_depleted_effect), 'effe', NONE,
		(unsigned long)NONE);
	made_reference(blocks, root + offsetof(struct collision_model, resistance.shield_recharging_effect), 'effe', NONE,
		(unsigned long)NONE);
	{
		real hx = block->size[0] * 0.5f, hy = block->size[1] * 0.5f, hz = block->size[2] * 0.5f;

		at = made_at(blocks, root) + offsetof(struct collision_model, pathfinding_box);
		write_real(at + 0, -hx);
		write_real(at + 4, hx);
		write_real(at + 8, -hy);
		write_real(at + 12, hy);
		write_real(at + 16, -hz);
		write_real(at + 20, hz);
	}

	material = made_block(blocks, root + offsetof(struct collision_model, resistance.materials), 1, MATERIAL_SIZE);
	if (blocks->failed)
		return;
	made_name(blocks, material, "metal");
	write_u16(made_at(blocks, material) + offsetof(struct damage_resistance_material, material_type), MATERIAL_THICK_METAL);
	write_real(made_at(blocks, material) + offsetof(struct damage_resistance_material, shield_damage_multiplier), 1.0f);
	write_real(made_at(blocks, material) + offsetof(struct damage_resistance_material, body_damage_multiplier), 1.0f);

	region = made_block(blocks, root + offsetof(struct collision_model, resistance.regions), 1, DAMAGE_REGION_SIZE);
	if (blocks->failed)
		return;
	made_name(blocks, region, "base");
	made_reference(blocks, region + DAMAGE_REGION_DESTROYED_EFFECT, 'effe', NONE, (unsigned long)NONE);
	permutation = made_block(blocks, region + DAMAGE_REGION_PERMUTATIONS, 1, DAMAGE_PERMUTATION_SIZE);
	if (blocks->failed)
		return;
	made_name(blocks, permutation, "base");

	node = made_block(blocks, root + offsetof(struct collision_model, nodes), 1, sizeof(struct collision_node));
	if (blocks->failed)
		return;
	made_name(blocks, node, "frame");
	write_u16(made_at(blocks, node) + offsetof(struct collision_node, region_index), 0);
	write_u16(made_at(blocks, node) + offsetof(struct collision_node, parent_node_index), 0xFFFF);
	write_u16(made_at(blocks, node) + offsetof(struct collision_node, next_sibling_node_index), 0xFFFF);
	write_u16(made_at(blocks, node) + offsetof(struct collision_node, first_child_node_index), 0xFFFF);
	bsp = made_block(blocks, node + offsetof(struct collision_node, bsps), 1, sizeof(struct collision_bsp));
	if (blocks->failed)
		return;

	/* the planes: the separator's first, then each chain surface's */
	if (mesh->piece_count > 1)
		plane_count++;
	for (surface_index = 0; surface_index < mesh->surface_count; surface_index++)
		surface_planes[surface_index] = mesh->surfaces[surface_index].separator ? 0 : plane_count++;
	/* the nodes: the separator's, then each piece's chain (the front
	piece's first), a node and a leaf a chain surface */
	node_count = (mesh->piece_count > 1 ? 1 : 0);
	for (surface_index = 0; surface_index < mesh->surface_count; surface_index++)
	{
		if (!mesh->surfaces[surface_index].separator)
			node_count++, leaf_count++;
	}
	/* (each leaf names its surface; a piece's leaves, the other piece's
	surfaces on the separator facing them) */
	reference_count = leaf_count;
	for (surface_index = 0; surface_index < mesh->surface_count; surface_index++)
	{
		short other;

		if (mesh->surfaces[surface_index].separator)
			continue;
		for (other = 0; other < mesh->surface_count; other++)
		{
			if (mesh->surfaces[other].separator && mesh->surfaces[other].piece != mesh->surfaces[surface_index].piece)
				reference_count++;
		}
	}

	planes = made_block(blocks, bsp + offsetof(struct collision_bsp, bsp3d.planes), plane_count, sizeof(real_plane3d));
	nodes = made_block(blocks, bsp + offsetof(struct collision_bsp, bsp3d.nodes), node_count, sizeof(struct bsp3d_node));
	leaves = made_block(blocks, bsp + offsetof(struct collision_bsp, leaves), leaf_count, COLLISION_LEAF_SIZE);
	references = made_block(blocks, bsp + offsetof(struct collision_bsp, bsp2d_references), reference_count,
		BSP2D_REFERENCE_SIZE);
	surfaces = made_block(blocks, bsp + offsetof(struct collision_bsp, surfaces), mesh->surface_count,
		sizeof(struct collision_surface));
	edges = made_block(blocks, bsp + offsetof(struct collision_bsp, edges), mesh->edge_count, sizeof(struct collision_edge));
	vertices = made_block(blocks, bsp + offsetof(struct collision_bsp, vertices), mesh->vertex_count,
		sizeof(struct collision_vertex));
	if (blocks->failed)
		return;

	if (mesh->piece_count > 1)
	{
		at = made_at(blocks, planes);
		write_real(at + 0, mesh->separator_n[0]);
		write_real(at + 4, mesh->separator_n[1]);
		write_real(at + 8, mesh->separator_n[2]);
		write_real(at + 12, mesh->separator_d);
	}
	for (surface_index = 0; surface_index < mesh->surface_count; surface_index++)
	{
		struct mesh_surface const *surface = &mesh->surfaces[surface_index];
		long designator = surface_planes[surface_index];

		if (!surface->separator)
		{
			at = made_at(blocks, planes) + (unsigned long)designator * sizeof(real_plane3d);
			write_real(at + 0, surface->n[0]);
			write_real(at + 4, surface->n[1]);
			write_real(at + 8, surface->n[2]);
			write_real(at + 12, surface->d);
		}
		else if (surface->n[1] * mesh->separator_n[1] + surface->n[0] * mesh->separator_n[0] +
			surface->n[2] * mesh->separator_n[2] < 0.0f)
		{
			designator |= LONG_MIN;
		}
		at = made_at(blocks, surfaces) + (unsigned long)surface_index * sizeof(struct collision_surface);
		write_u32(at + offsetof(struct collision_surface, plane_designator), (unsigned long)designator);
		write_u32(at + offsetof(struct collision_surface, first_edge_index), (unsigned long)surface->first_edge);
		at[offsetof(struct collision_surface, flags)] = 0;
		at[offsetof(struct collision_surface, breakable_surface_index)] = 0;
		write_u16(at + offsetof(struct collision_surface, material_index), 0);
	}

	/* the chains, their nodes and leaves */
	{
		long next_node = mesh->piece_count > 1 ? 1 : 0;
		long next_leaf = 0, next_reference = 0;
		short order;

		for (order = 0; order < mesh->piece_count; order++)
		{
			long previous = NONE;

			/* (the front piece's chain first) */
			piece = mesh->piece_count > 1 ? (short)(1 - order) : 0;
			chain_first[piece] = NONE;
			for (surface_index = 0; surface_index < mesh->surface_count; surface_index++)
			{
				struct mesh_surface const *surface = &mesh->surfaces[surface_index];
				short other;
				long references_first = next_reference;

				if (surface->separator || surface->piece != piece)
					continue;
				at = made_at(blocks, nodes) + (unsigned long)next_node * sizeof(struct bsp3d_node);
				write_u32(at + offsetof(struct bsp3d_node, plane_designator), (unsigned long)surface_planes[surface_index]);
				write_u32(at + offsetof(struct bsp3d_node, children) + 4, (unsigned long)(LONG_MIN | next_leaf));
				write_u32(at + offsetof(struct bsp3d_node, children), (unsigned long)NONE);
				if (previous != NONE)
					write_u32(made_at(blocks, nodes) + (unsigned long)previous * sizeof(struct bsp3d_node) +
						offsetof(struct bsp3d_node, children), (unsigned long)next_node);
				else
					chain_first[piece] = next_node;
				previous = next_node++;

				/* its leaf: its surface, and the other piece's on the
				separator facing this side */
				at = made_at(blocks, references) + (unsigned long)next_reference++ * BSP2D_REFERENCE_SIZE;
				write_u32(at, (unsigned long)surface_planes[surface_index]);
				write_u32(at + 4, (unsigned long)(LONG_MIN | surface_index));
				for (other = 0; other < mesh->surface_count; other++)
				{
					struct mesh_surface const *facing = &mesh->surfaces[other];

					if (!facing->separator || facing->piece == piece)
						continue;
					at = made_at(blocks, references) + (unsigned long)next_reference++ * BSP2D_REFERENCE_SIZE;
					write_u32(at, (unsigned long)(piece == 1 ? 0 : LONG_MIN));
					write_u32(at + 4, (unsigned long)(LONG_MIN | other));
				}
				at = made_at(blocks, leaves) + (unsigned long)next_leaf++ * COLLISION_LEAF_SIZE;
				write_u16(at, 0);
				write_u16(at + 2, (unsigned short)(next_reference - references_first));
				write_u32(at + 4, (unsigned long)references_first);
			}
		}
		if (mesh->piece_count > 1)
		{
			at = made_at(blocks, nodes);
			write_u32(at + offsetof(struct bsp3d_node, plane_designator), 0);
			write_u32(at + offsetof(struct bsp3d_node, children) + 4, (unsigned long)chain_first[1]);
			write_u32(at + offsetof(struct bsp3d_node, children), (unsigned long)chain_first[0]);
		}
		if (next_reference != reference_count || next_leaf != leaf_count || next_node != node_count)
		{
			blocks->failed = TRUE;
			return;
		}
	}

	/* the edges and vertices */
	for (surface_index = 0; surface_index < mesh->edge_count; surface_index++)
	{
		struct mesh_edge const *edge = &mesh->edges[surface_index];

		at = made_at(blocks, edges) + (unsigned long)surface_index * sizeof(struct collision_edge);
		write_u32(at + offsetof(struct collision_edge, vertex_indices), (unsigned long)edge->vertices[0]);
		write_u32(at + offsetof(struct collision_edge, vertex_indices) + 4, (unsigned long)edge->vertices[1]);
		write_u32(at + offsetof(struct collision_edge, edge_indices), (unsigned long)edge->next[0]);
		write_u32(at + offsetof(struct collision_edge, edge_indices) + 4, (unsigned long)edge->next[1]);
		write_u32(at + offsetof(struct collision_edge, surface_indices), (unsigned long)(long)edge->surfaces[0]);
		write_u32(at + offsetof(struct collision_edge, surface_indices) + 4, (unsigned long)(long)edge->surfaces[1]);
	}
	for (surface_index = 0; surface_index < mesh->vertex_count; surface_index++)
	{
		short edge_index;

		at = made_at(blocks, vertices) + (unsigned long)surface_index * sizeof(struct collision_vertex);
		write_real(at + 0, mesh->vertices[surface_index][0]);
		write_real(at + 4, mesh->vertices[surface_index][1]);
		write_real(at + 8, mesh->vertices[surface_index][2]);
		for (edge_index = 0; edge_index < mesh->edge_count; edge_index++)
		{
			if (mesh->edges[edge_index].vertices[0] == surface_index || mesh->edges[edge_index].vertices[1] == surface_index)
				break;
		}
		write_u32(at + offsetof(struct collision_vertex, first_edge_index), (unsigned long)edge_index);
	}
}

/* ---------- the scenery and the shaders */

static void made_scenery(
	struct scene_pack_blocks *blocks,
	long tag,
	struct scene_pack_block_info const *block,
	struct mesh const *mesh,
	long model_tag,
	long collision_tag)
{
	unsigned long root = made_tag(blocks, tag, 'scen', 'obje', block->path, sizeof(struct object_definition));
	real radius = mesh_radius(mesh);
	unsigned char *at;

	if (blocks->failed)
		return;
	at = made_at(blocks, root);
	write_u16(at + offsetof(struct object_definition, object.type), _object_type_scenery);
	/* (no shadow: a block is the world's, and a shadow a draw more) */
	write_u16(at + offsetof(struct object_definition, object.flags), FLAG(_object_does_not_cast_shadow_bit));
	write_real(at + offsetof(struct object_definition, object.bounding_radius), radius);
	write_real(at + offsetof(struct object_definition, object.render_bounding_radius), radius);
	write_real(at + offsetof(struct object_definition, object.acceleration_scale), 1.0f);
	write_u16(at + offsetof(struct object_definition, object.icon_text_index), 0xFFFF);
	made_reference(blocks, root + offsetof(struct object_definition, object.model), 'mode', model_tag, 0);
	made_reference(blocks, root + offsetof(struct object_definition, object.animation_graph), 'antr', NONE,
		(unsigned long)NONE);
	made_reference(blocks, root + offsetof(struct object_definition, object.collision_model), 'coll', collision_tag, 0);
	made_reference(blocks, root + offsetof(struct object_definition, object.physics), 'phys', NONE, (unsigned long)NONE);
	made_reference(blocks, root + offsetof(struct object_definition, object.modifier_shader), 'shdr', NONE,
		(unsigned long)NONE);
	made_reference(blocks, root + offsetof(struct object_definition, object.creation_effect), 'effe', NONE,
		(unsigned long)NONE);
}

static void made_shader(
	struct scene_pack_blocks *blocks,
	long tag,
	short material,
	struct material_choice const *choice,
	struct scene_pack_blocks_source const *source)
{
	unsigned long root = made_tag(blocks, tag, 'soso', 'shdr', material_shader_names[material], SHADER_MODEL_SIZE);
	unsigned char *at;

	if (blocks->failed)
		return;
	at = made_at(blocks, root);
	write_u16(at + SHADER_MATERIAL_TYPE, MATERIAL_THICK_METAL);
	write_u16(at + SHADER_TYPE, SHADER_TYPE_MODEL);
	write_u16(at + SHADER_MODEL_FLAGS, FLAG(SHADER_MODEL_NOT_ALPHA_TESTED_BIT));
	write_real(at + SHADER_MODEL_SELF_ILLUMINATION_PERIOD, 1.0f);
	write_real(at + SHADER_MODEL_MAP_U_SCALE, 1.0f);
	write_real(at + SHADER_MODEL_MAP_V_SCALE, 1.0f);
	write_u16(at + SHADER_MODEL_DETAIL_FUNCTION, (unsigned short)choice->detail_function);
	write_real(at + SHADER_MODEL_DETAIL_MAP_SCALE, choice->detail_scale);
	write_real(at + SHADER_MODEL_DETAIL_MAP_V_SCALE, 1.0f);
	made_reference(blocks, root + SHADER_MODEL_BASE_MAP, 'bitm', NONE,
		read_u32(source_instance(source, choice->base_map) + 12));
	made_reference(blocks, root + SHADER_MODEL_MULTIPURPOSE_MAP, 'bitm', NONE, (unsigned long)NONE);
	made_reference(blocks, root + SHADER_MODEL_DETAIL_MAP, 'bitm', NONE, choice->detail_map != NONE ?
		read_u32(source_instance(source, choice->detail_map) + 12) : (unsigned long)NONE);
	made_reference(blocks, root + SHADER_MODEL_REFLECTION_CUBE_MAP, 'bitm', NONE, (unsigned long)NONE);
}

/* ---------- public code */

struct scene_pack_blocks *scene_pack_blocks_make(
	struct scene_pack_blocks_source const *source,
	char *why,
	long why_size)
{
	struct scene_pack_blocks *blocks;
	struct material_choice choices[NUMBER_OF_MATERIALS];
	long tag_count = NUMBER_OF_BLOCKS * TAGS_PER_BLOCK + NUMBER_OF_MATERIALS;
	short material, index;

	if (!source || !source->tags || source->count <= 0 || source->count + tag_count > 0x7FFF)
	{
		snprintf(why, (size_t)why_size, "the map has too many tags for the blocks");
		return NULL;
	}
	/* the bitmaps: the walls' first (its environment shaders', else its
	model shaders'), then the floors', another bitmap than the walls' when
	one reads well enough */
	material_choose(source, 'senv', _material_walls, NONE, &choices[_material_walls]);
	if (choices[_material_walls].shader == NONE)
		material_choose(source, 'soso', _material_walls, NONE, &choices[_material_walls]);
	if (choices[_material_walls].shader == NONE)
	{
		snprintf(why, (size_t)why_size, "the map has no bitmap the blocks can be drawn with");
		return NULL;
	}
	material_choose(source, 'senv', _material_floors, choices[_material_walls].base_map, &choices[_material_floors]);
	if (choices[_material_floors].shader == NONE)
		choices[_material_floors] = choices[_material_walls];

	blocks = malloc(sizeof(*blocks));
	if (!blocks)
	{
		snprintf(why, (size_t)why_size, "there is no memory for the blocks");
		return NULL;
	}
	memset(blocks, 0, sizeof(*blocks));
	blocks->source_count = source->count;
	blocks->tag_count = tag_count;
	for (material = 0; material < NUMBER_OF_MATERIALS; material++)
	{
		char const *name = source_name(source, choices[material].base_map);

		snprintf(blocks->material_names[material], sizeof(blocks->material_names[material]), "%s", name ? name : "?");
	}
	blocks->instances = made_alloc(blocks, (unsigned long)tag_count * TAG_INSTANCE_SIZE);
	for (index = 0; index < NUMBER_OF_BLOCKS && !blocks->failed; index++)
	{
		struct scene_pack_block_info const *block = &scene_pack_blocks[index];
		long first = (long)index * TAGS_PER_BLOCK;
		struct mesh mesh;

		if (!mesh_make(block, &mesh))
		{
			blocks->failed = TRUE;
			break;
		}
		made_scenery(blocks, first, block, &mesh, first + 1, first + 2);
		made_model(blocks, first + 1, block, &mesh, NUMBER_OF_BLOCKS * TAGS_PER_BLOCK + block_material(block));
		made_collision_model(blocks, first + 2, block, &mesh);
	}
	for (material = 0; material < NUMBER_OF_MATERIALS && !blocks->failed; material++)
		made_shader(blocks, NUMBER_OF_BLOCKS * TAGS_PER_BLOCK + material, material, &choices[material], source);
	if (blocks->failed)
	{
		scene_pack_blocks_free(blocks);
		snprintf(why, (size_t)why_size, "the blocks could not be made");
		return NULL;
	}
	return blocks;
}

void scene_pack_blocks_free(
	struct scene_pack_blocks *blocks)
{
	short index;

	if (!blocks)
		return;
	for (index = 0; index < blocks->part_count; index++)
	{
		if (blocks->parts[index].vertices)
			free(blocks->parts[index].vertices);
		if (blocks->parts[index].indices)
			free(blocks->parts[index].indices);
	}
	if (blocks->bytes)
		free(blocks->bytes);
	free(blocks);
}

long scene_pack_blocks_tag_count(
	struct scene_pack_blocks const *blocks)
{
	return blocks ? blocks->tag_count : 0;
}

unsigned char *scene_pack_blocks_instance(
	struct scene_pack_blocks const *blocks,
	long index)
{
	if (!blocks || index < 0 || index >= blocks->tag_count)
		return NULL;
	return scene_pack_blocks_at(blocks, blocks->instances + (unsigned long)index * TAG_INSTANCE_SIZE, TAG_INSTANCE_SIZE);
}

unsigned char *scene_pack_blocks_at(
	struct scene_pack_blocks const *blocks,
	unsigned long address,
	unsigned long size)
{
	unsigned long offset = address - SCENE_PACK_BLOCKS_ADDRESS;

	if (!blocks || address < SCENE_PACK_BLOCKS_ADDRESS || offset > blocks->size || size > blocks->size - offset)
		return NULL;
	return blocks->bytes + offset;
}

long scene_pack_blocks_scenery(
	struct scene_pack_blocks const *blocks,
	short block)
{
	(void)blocks;
	return block >= 0 && block < NUMBER_OF_BLOCKS ? (long)block * TAGS_PER_BLOCK : NONE;
}

boolean scene_pack_blocks_part_buffers(
	struct scene_pack_blocks const *blocks,
	unsigned long part_address,
	unsigned char **vertices,
	unsigned long *vertex_bytes,
	unsigned char **indices,
	unsigned long *index_bytes)
{
	short index;

	*vertices = *indices = NULL;
	for (index = 0; blocks && index < blocks->part_count; index++)
	{
		struct made_part const *part = &blocks->parts[index];

		if (part->address != part_address)
			continue;
		*vertices = malloc(part->vertex_bytes);
		*indices = malloc(part->index_bytes);
		if (!*vertices || !*indices)
		{
			if (*vertices)
				free(*vertices);
			if (*indices)
				free(*indices);
			*vertices = *indices = NULL;
			return FALSE;
		}
		memcpy(*vertices, part->vertices, part->vertex_bytes);
		memcpy(*indices, part->indices, part->index_bytes);
		*vertex_bytes = part->vertex_bytes;
		*index_bytes = part->index_bytes;
		return TRUE;
	}
	return FALSE;
}

void scene_pack_blocks_materials(
	struct scene_pack_blocks const *blocks,
	char const **walls,
	char const **floors)
{
	*walls = blocks ? blocks->material_names[_material_walls] : "";
	*floors = blocks ? blocks->material_names[_material_floors] : "";
}
