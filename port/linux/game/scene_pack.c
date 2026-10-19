/*
SCENE_PACK.C

Scene packs (scene_pack.h, which says what one is and what its file holds):
the recipe, the builder, and the checks and moves a pack's tags go through
before anything reads them. No game state: scene_packs.c puts a pack into
the running game, tools/scene_pack_test.c tests this alone.

The builder reads its source map (an Xbox map, inflated as it is read; or
already inflated, or the game's decompressed copy of it in the cache
partition, scene_pack_copy_source: then read in place, seeking to what it
wants) twice: once to the end of its tag data, which it walks there, then
from the start again for the bitmaps' pixels and sounds' samples the pack's
tags name, in the file's order - one read for several recipes' packs at
once (scene_pack_build_many: the tag data once, the union of their pieces
once). From the tag data:
- the recipe's tags, then every tag they need (every reference the tag
  layouts know, tag_relocate.c's, and every tag index the tag schemas know,
  tag_schema*.c), but for the groups and tags the recipe strips, whose
  references name none;
- each tag copied (its root, the size of its group's schema, then each
  block and data the layouts say it has), every pointer the layouts know
  made a pointer to where its bytes are in the pack (none if they are not
  in the tags: a pointer the walk below would follow out of the pack is
  never written), every reference made one to the pack's tag;
- the models' parts' vertex and index buffers (their headers and bytes,
  which a map keeps beside the tags) copied with them;
- the tag indices the schemas know, the bitmaps' pixels and the sounds'
  samples made the pack's;
- (keep lines) an animation graph's animations the recipe does not keep
  copied without frames (build_animations_plan, build_animations_patch).
Anything of the source map that does not fit where it says is the map's
damage: the build stops, nothing is written. What it writes is checked as
any pack is when it is loaded.

The checks of a pack (scene_pack_open, scene_pack_load) are a map's: its
header bounded, its sections in the file and their CRCs, then its tags
moved with tag_relocate.c's walk (halo_tag_relocate_pack) and checked by
tag_validate.c's (tag_validate_pack_tags), which must find nothing to
correct.
*/

/* ---------- headers */

#include "cseries.h"
#include "tag_files/tag_groups.h"
#include "bitmaps/bitmap_group.h"
#include "sound/sound_definitions.h"
#include "models/model_animation_definitions.h"

#include "scene_pack.h"
#include "scene_pack_ce.h"
#include "tag_schema.h"
#include "tag_relocate.h"
#include "../../third_party/zlib/zlib_prefixed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);

/* ---------- constants */

enum
{
	/* an Xbox map's header (cache_files.c) */
	CACHE_FILE_HEADER_SIZE = 0x800,
	CACHE_FILE_HEADER_SIGNATURE = 'head',
	CACHE_FILE_FOOTER_SIGNATURE = 'foot',
	CACHE_FILE_XBOX_VERSION = 5,
	CACHE_FILE_TAG_HEADER_SIGNATURE = 'tags',
	XBOX_TAG_CACHE_SIZE = 0x01600000,

	/* a map's tag header and instances (cache_files.c) */
	TAG_HEADER_SIZE = 0x24,
	TAG_INSTANCE_SIZE = 0x20,
	/* a D3D buffer header: Common, Data, Lock */
	BUFFER_HEADER_SIZE = 12,

	/* a model geometry part (models.c) and its buffers' fields */
	MODEL_PART_SIZE = 0x68,
	MODEL_PART_TRIANGLE_TYPE = 0x44,
	MODEL_PART_TRIANGLE_COUNT = 0x48,
	MODEL_PART_TRIANGLE_BASE = 0x4C,
	MODEL_PART_TRIANGLE_HARDWARE = 0x50,
	MODEL_PART_VERTEX_TYPE = 0x54,
	MODEL_PART_VERTEX_COUNT = 0x58,
	MODEL_PART_VERTEX_BASE = 0x60,
	MODEL_PART_VERTEX_HARDWARE = 0x64,
	NUMBER_OF_MODEL_VERTEX_TYPES = 12,
	MAXIMUM_MODEL_PART_VERTICES = 0xFFFF,
	MAXIMUM_MODEL_PART_TRIANGLES = 0xFFFF,

	/* a tag's walk: how deep, and how many elements in all (a damaged map
	whose blocks point at each other cannot make it take longer) */
	MAXIMUM_WALK_DEPTH = 32,
	MAXIMUM_WALK_ELEMENTS = 4 * 1024 * 1024,

	/* the pack's tags' salts before they are the game's (scene_pack_rebase) */
	PACK_FIRST_SALT = 0x1000,

	/* the reads' and writes' bounce (and the inflate's input) */
	BOUNCE_SIZE = 64 * 1024,

	/* the header's fields */
	HEADER_SIGNATURE = 0,
	HEADER_VERSION = 8,
	HEADER_HEADER_SIZE = 12,
	HEADER_FILE_SIZE = 16,
	HEADER_TAGS_OFFSET = 20,
	HEADER_TAGS_SIZE = 24,
	HEADER_DATA_OFFSET = 28,
	HEADER_DATA_SIZE = 32,
	HEADER_TAG_COUNT = 36,
	HEADER_ROOT_COUNT = 40,
	HEADER_TAGS_CRC = 44,
	HEADER_DATA_CRC = 48,
	HEADER_BUILDER_VERSION = 52,
	HEADER_RECIPE_HASH = 56,
	HEADER_SOURCE_HASH = 64,
	HEADER_SOURCE_MAP = 72,
	HEADER_HEADER_CRC = 124,

	/* bitmaps (tag_schema_render.c's) */
	BITMAP_TYPE_CUBE_MAP = 2,
	BITMAP_COMPRESSED_BIT = 1,
	NUMBER_OF_BITMAP_FORMATS = 18,
	MAXIMUM_BITMAP_PIXELS = 16 * 1024 * 1024,
};

static char const scene_pack_signature[8] = { 'H', 'C', 'E', 'V', 'P', 'A', 'C', 'K' };

/* rasterizer_geometry.c's vertex sizes, by vertex type (tag_schema_models.c) */
static short const model_vertex_type_sizes[NUMBER_OF_MODEL_VERTEX_TYPES] =
{
	56, 32, 20, 8, 68, 32, 24, 36, 20, 16, 16, 8
};

/* bits a pixel, by bitmap format (tag_schema_render.c) */
static char const bitmap_format_bits_per_pixel[NUMBER_OF_BITMAP_FORMATS] =
{
	8, 8, 8, 16, 0, 0, 16, 0, 16, 16, 32, 32, 0, 0, 4, 8, 8, 8
};

/* ---------- structures */

/* a tag of the pack, as it is built */
struct build_tag
{
	long source_index;
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	unsigned long source_base;
	unsigned long root_size;
	char const *name;
	unsigned long name_offset;
	unsigned long base_offset;
};

/* bytes of the source's tag data copied into the pack (a root, a block's
elements, a data) */
struct build_extent
{
	unsigned long source;
	unsigned long size;
	unsigned long offset;
};

/* a model part's vertex or index buffer */
struct build_buffer
{
	unsigned long source_header;
	unsigned long source_data;
	unsigned long size;
	boolean index;
	unsigned long header_offset;
	unsigned long data_offset;
	/* (a Custom Edition model's: its bytes made by the builder,
	scene_pack_ce_part_buffers, not the source's) */
	unsigned char *made;
};

/* (a Custom Edition source's) a bitmap's pixels or a sound permutation's
samples, made this build's as they are written (scene_pack_ce.c): the
bitmap's or the permutation's place in the pack's tags, and its tag (the
source's index: a bitmap's channel order) */
struct build_item
{
	unsigned long location;
	boolean bitmap;
	long source_index;
	/* (a permutation's) its sound's place in the pack's tags */
	unsigned long sound_location;
};

/* a pointer of the pack to a buffer's header or bytes, made once they have
their places */
struct build_buffer_fixup
{
	unsigned long location;
	long buffer;
	boolean data;
};

/* bytes of the source's file (pixels, samples) the pack holds */
struct build_range
{
	unsigned long source;
	unsigned long size;
	/* (once placed) where in the pack's data */
	unsigned long offset;
	long order;
};

/* a file offset in the pack's tags made one of the pack's data: the range's
place plus delta */
struct build_range_fixup
{
	unsigned long location;
	long range;
	long delta;
};

/* the source map, read in order (inflated, if it is compressed) */
struct source_reader
{
	FILE *file;
	boolean compressed;
	z_stream stream;
	boolean stream_open;
	boolean ended;
	unsigned char *input;
	unsigned char *bounce;
	unsigned long position;
	unsigned long file_length;
	unsigned long long read_bytes;
	int volatile const *cancel;
	/* (the build's progress: the read's half, 0 or 50, and where it goes;
	read in place, the bytes the half reads and has read) */
	long progress_base;
	long volatile *progress;
	unsigned long long phase_total;
	unsigned long long phase_read;
	/* (read from the map's decompressed copy: scene_pack_copy_source) its
	token, and the map's header it must still have */
	boolean using_copy;
	unsigned long copy_token;
	unsigned char map_header[CACHE_FILE_HEADER_SIZE];
};

struct build_state
{
	struct scene_pack_recipe const *recipe;
	struct scene_pack_build_result *result;

	/* the source's tag data (as the Xbox has it, at its tag cache's
	address; a Custom Edition map's moved to where it is) and its table */
	unsigned char *tags;
	unsigned long tags_size;
	unsigned long source_address;
	/* (a Custom Edition map's: scene_pack_ce.c) */
	struct scene_pack_ce *custom_edition;
	struct build_item *items;
	long item_count;
	long item_capacity;
	unsigned long instances;
	long source_count;
	unsigned long vertex_buffers;
	long vertex_buffer_count;
	unsigned long index_buffers;
	long index_buffer_count;
	/* each source tag's place in the pack (NONE: not in it) */
	long *pack_index;

	struct build_tag *pack_tags;
	long pack_tag_count;

	unsigned char *blob;
	unsigned long blob_size;
	unsigned long blob_capacity;

	struct build_extent *extents;
	long extent_count;
	long extent_capacity;
	boolean extents_sorted;

	struct build_buffer *buffers;
	long buffer_count;
	long buffer_capacity;
	struct build_buffer_fixup *buffer_fixups;
	long buffer_fixup_count;
	long buffer_fixup_capacity;

	struct build_range *ranges;
	long range_count;
	long range_capacity;
	struct build_range_fixup *range_fixups;
	long range_fixup_count;
	long range_fixup_capacity;

	unsigned short model_part_layout;
	long walked;
	boolean failed;
	int volatile const *cancel;

	/* (keep lines) the animations a pack keeps no frames of: each one's
	element, in the source; their data fields not copied
	(build_animations_plan, build_animations_patch) */
	struct build_reduced_animation
	{
		unsigned long animation;
	} *reduced;
	long reduced_count;
	long reduced_capacity;
	long reduced_bytes;
};

/* ---------- globals */

void (*scene_pack_io_hook)(unsigned long bytes, boolean source) = NULL;
struct scene_pack_copy_source const *scene_pack_copy_source = NULL;

/* ---------- prototypes */

static boolean layout_copy(struct build_state *state, unsigned short layout, unsigned long source,
	unsigned long offset, short depth);

/* ---------- private code */

static unsigned long read_u32(void const *address)
{
	unsigned char const *bytes = address;

	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

static short read_s16(void const *address)
{
	unsigned char const *bytes = address;

	return (short)(bytes[0] | (bytes[1] << 8));
}

static void write_u32(void *address, unsigned long value)
{
	unsigned char *bytes = address;

	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

static unsigned long long read_u64(void const *address)
{
	unsigned char const *bytes = address;

	return (unsigned long long)read_u32(bytes) | ((unsigned long long)read_u32(bytes + 4) << 32);
}

static void write_u64(void *address, unsigned long long value)
{
	write_u32(address, (unsigned long)value);
	write_u32((unsigned char *)address + 4, (unsigned long)(value >> 32));
}

static unsigned long long fnv_bytes(unsigned long long hash, void const *data, unsigned long size)
{
	unsigned char const *bytes = data;
	unsigned long index;

	for (index = 0; index < size; index++)
	{
		hash ^= bytes[index];
		hash *= 0x100000001B3ULL;
	}
	return hash;
}

#define FNV_BASIS 0xCBF29CE484222325ULL

static unsigned long crc_bytes(unsigned long crc, void const *data, unsigned long size)
{
	return (unsigned long)crc32((uLong)crc, (Bytef const *)data, (uInt)size);
}

/* a read or write of a build's or a load's done (its pauses and pace:
scene_pack_io_hook) */
static void io_done(unsigned long bytes)
{
	if (scene_pack_io_hook)
		scene_pack_io_hook(bytes, FALSE);
}

/* (the same, of the source map) */
static void io_done_source(unsigned long bytes)
{
	if (scene_pack_io_hook)
		scene_pack_io_hook(bytes, TRUE);
}

static void why_set(char *why, long why_size, char const *format, char const *detail)
{
	if (why && why_size > 0)
		snprintf(why, why_size, format, detail ? detail : "");
}

static voidpf zlib_allocate(voidpf opaque, uInt items, uInt size)
{
	(void)opaque;
	if (size && items > 0x7FFFFFFFUL / size)
		return NULL;
	return malloc(items * size ? items * size : 1);
}

static void zlib_free(voidpf opaque, voidpf address)
{
	(void)opaque;
	if (address)
		free(address);
}

static int lower(int character)
{
	return character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character;
}

static boolean names_match(char const *a, char const *b)
{
	while (*a && *b && lower((unsigned char)*a) == lower((unsigned char)*b))
		a++, b++;
	return !*a && !*b;
}

/* a pack's, a map's name: letters, digits, '_' and '-' */
static boolean name_valid(char const *name)
{
	long length = 0;

	for (; name[length]; length++)
	{
		char character = name[length];

		if (length >= SCENE_PACK_NAME_SIZE - 1 ||
			!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
				(character >= '0' && character <= '9') || character == '_' || character == '-'))
		{
			return FALSE;
		}
	}
	return length > 0;
}

char *scene_pack_group_text(
	unsigned long group_tag,
	char text[5])
{
	short index;

	for (index = 0; index < 4; index++)
	{
		char character = (char)(group_tag >> (24 - 8 * index));

		text[index] = character >= 0x20 && character < 0x7F ? character : '?';
	}
	text[4] = 0;
	return text;
}

static struct tag_schema_group const *schema_group_get(
	unsigned long group_tag)
{
	struct tag_schema_group const *const *list;

	for (list = tag_schema_group_lists; *list; list++)
	{
		struct tag_schema_group const *group;

		for (group = *list; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group;
		}
	}
	return NULL;
}

/* ---------- the recipe */

/* the next line of text (from *cursor to end), its spaces trimmed */
static boolean recipe_line(char const **cursor, char const *end, char *line, long line_size)
{
	char const *start = *cursor;
	char const *stop;
	long length;

	if (start >= end)
		return FALSE;
	stop = start;
	while (stop < end && *stop != '\n')
		stop++;
	*cursor = stop < end ? stop + 1 : end;
	while (start < stop && (*start == ' ' || *start == '\t' || *start == '\r'))
		start++;
	while (stop > start && (stop[-1] == ' ' || stop[-1] == '\t' || stop[-1] == '\r'))
		stop--;
	length = (long)(stop - start);
	if (length >= line_size)
		length = -1;
	if (length >= 0)
	{
		memcpy(line, start, length);
		line[length] = 0;
	}
	else
	{
		line[0] = 1;
		line[1] = 0;
	}
	return TRUE;
}

/* a group's four characters (printable) */
static boolean recipe_group(char const *text, unsigned long *group_tag)
{
	short index;

	*group_tag = 0;
	for (index = 0; index < 4; index++)
	{
		if (text[index] < 0x20 || text[index] >= 0x7F)
			return FALSE;
		*group_tag = (*group_tag << 8) | (unsigned char)text[index];
	}
	return text[4] == 0 || text[4] == ' ' || text[4] == '\t';
}

/* "<group> <path>": a path of printable characters, its case as given */
static boolean recipe_tag(char const *text, struct scene_pack_recipe_tag *tag, boolean path_needed)
{
	char const *path;
	long length;

	if (!recipe_group(text, &tag->group_tag))
		return FALSE;
	path = text + 4;
	while (*path == ' ' || *path == '\t')
		path++;
	length = (long)strlen(path);
	if ((path_needed && !length) || length >= SCENE_PACK_TAG_PATH_SIZE)
		return FALSE;
	for (; *path; path++)
	{
		if ((unsigned char)*path < 0x20 || (unsigned char)*path >= 0x7F)
			return FALSE;
	}
	memcpy(tag->path, text + (path - text) - length, length);
	tag->path[length] = 0;
	return TRUE;
}

boolean scene_pack_recipe_parse(
	char const *text,
	long length,
	struct scene_pack_recipe *recipe,
	char *why,
	long why_size)
{
	char const *cursor = text;
	char const *end;
	char line[SCENE_PACK_TAG_PATH_SIZE + 16];
	long line_number = 0;
	unsigned long long hash = FNV_BASIS;
	char version[16];
	short index;

	memset(recipe, 0, sizeof(*recipe));
	if (!text || length < 0 || length > SCENE_PACK_MAXIMUM_RECIPE_BYTES)
	{
		why_set(why, why_size, "the recipe is larger than a recipe may be%s", NULL);
		return FALSE;
	}
	end = text + length;
	while (recipe_line(&cursor, end, line, sizeof(line)))
	{
		char word[8];
		char const *rest;
		long word_length;

		line_number++;
		if (line[0] == 1)
		{
			snprintf(why, why_size, "the recipe's line %ld is too long", line_number);
			return FALSE;
		}
		if (!line[0] || line[0] == '#')
			continue;
		word_length = (long)strcspn(line, " \t");
		if (word_length >= (long)sizeof(word))
			word_length = sizeof(word) - 1;
		memcpy(word, line, word_length);
		word[word_length] = 0;
		rest = line + strcspn(line, " \t");
		while (*rest == ' ' || *rest == '\t')
			rest++;
		if (!strcmp(word, "pack") && !recipe->name[0] && name_valid(rest))
		{
			snprintf(recipe->name, sizeof(recipe->name), "%s", rest);
		}
		else if (!strcmp(word, "map") && *rest)
		{
			while (*rest)
			{
				long name_length = (long)strcspn(rest, " \t");
				char name[SCENE_PACK_NAME_SIZE];

				if (recipe->map_count >= SCENE_PACK_MAXIMUM_MAPS || name_length >= SCENE_PACK_NAME_SIZE)
					break;
				memcpy(name, rest, name_length);
				name[name_length] = 0;
				if (!name_valid(name))
					break;
				memcpy(recipe->maps[recipe->map_count++], name, name_length + 1);
				rest += name_length;
				while (*rest == ' ' || *rest == '\t')
					rest++;
			}
			if (*rest)
			{
				snprintf(why, why_size, "the recipe's line %ld names a map that cannot be one", line_number);
				return FALSE;
			}
		}
		else if (!strcmp(word, "cemap") && *rest)
		{
			struct scene_pack_recipe_custom_edition_map *map;
			char name[SCENE_PACK_NAME_SIZE + 1];
			char size_text[16];
			char checksum_text[16];
			char extra[2];
			char *end;
			unsigned long long size;

			if (recipe->custom_edition_map_count >= SCENE_PACK_MAXIMUM_MAPS ||
				sscanf(rest, "%32s %15s %15s %1s", name, size_text, checksum_text, extra) != 3 ||
				strlen(name) >= SCENE_PACK_NAME_SIZE || !name_valid(name) ||
				strspn(size_text, "0123456789") != strlen(size_text) || strlen(size_text) > 10 ||
				strlen(checksum_text) != 8 || strspn(checksum_text, "0123456789abcdefABCDEF") != 8)
			{
				snprintf(why, why_size, "the recipe's line %ld names a Custom Edition map that cannot be one", line_number);
				return FALSE;
			}
			for (size = 0, end = size_text; *end; end++)
				size = size * 10 + (unsigned long long)(*end - '0');
			if (!size || size > 0x7FFFFFFFULL)
			{
				snprintf(why, why_size, "the recipe's line %ld names a Custom Edition map that cannot be one", line_number);
				return FALSE;
			}
			map = &recipe->custom_edition_maps[recipe->custom_edition_map_count++];
			memcpy(map->name, name, strlen(name) + 1);
			map->file_size = (unsigned long)size;
			map->checksum = (unsigned long)strtoul(checksum_text, &end, 16);
		}
		else if (!strcmp(word, "unit") && !strcmp(rest, "multiplayer") && recipe->root_count < SCENE_PACK_MAXIMUM_ROOTS)
		{
			recipe->roots[recipe->root_count].group_tag = 'unit';
			recipe->roots[recipe->root_count].path[0] = 0;
			recipe->root_count++;
		}
		else if (!strcmp(word, "tag") && recipe->root_count < SCENE_PACK_MAXIMUM_ROOTS &&
			recipe_tag(rest, &recipe->roots[recipe->root_count], TRUE))
		{
			recipe->root_count++;
		}
		else if (!strcmp(word, "strip") && strlen(rest) == 4 &&
			recipe->strip_group_count < SCENE_PACK_MAXIMUM_STRIP_GROUPS &&
			recipe_group(rest, &recipe->strip_groups[recipe->strip_group_count]))
		{
			recipe->strip_group_count++;
		}
		else if (!strcmp(word, "strip") && strlen(rest) > 5 && recipe->strip_tag_count < SCENE_PACK_MAXIMUM_STRIP_TAGS &&
			recipe_tag(rest, &recipe->strip_tags[recipe->strip_tag_count], TRUE))
		{
			recipe->strip_tag_count++;
		}
		else if (!strcmp(word, "keep") && recipe->keep_count < SCENE_PACK_MAXIMUM_KEEPS && strchr(rest, '|') &&
			strchr(rest, '|') > rest && strlen(strchr(rest, '|') + 1) > 0 &&
			strchr(rest, '|') - rest < SCENE_PACK_TAG_PATH_SIZE && strlen(strchr(rest, '|') + 1) < SCENE_PACK_ANIMATION_NAME_SIZE)
		{
			struct scene_pack_recipe_keep *keep = &recipe->keeps[recipe->keep_count++];
			char const *bar = strchr(rest, '|');

			memcpy(keep->graph, rest, (size_t)(bar - rest));
			keep->graph[bar - rest] = 0;
			snprintf(keep->animation, sizeof(keep->animation), "%s", bar + 1);
		}
		else
		{
			snprintf(why, why_size, "the recipe's line %ld is not one a recipe has", line_number);
			return FALSE;
		}
	}
	if (!recipe->name[0] || !(recipe->map_count || recipe->custom_edition_map_count) || !recipe->root_count)
	{
		why_set(why, why_size, "the recipe has no pack name, map or tag%s", NULL);
		return FALSE;
	}
	if (recipe->map_count && recipe->custom_edition_map_count)
	{
		why_set(why, why_size, "the recipe names both Xbox and Custom Edition maps%s", NULL);
		return FALSE;
	}

	/* (what it says, in a form of its own: its case where the game's names
	have none) */
	hash = fnv_bytes(hash, recipe->name, strlen(recipe->name) + 1);
	for (index = 0; index < recipe->map_count; index++)
	{
		char const *name = recipe->maps[index];

		for (; *name; name++)
		{
			unsigned char character = (unsigned char)lower((unsigned char)*name);

			hash = fnv_bytes(hash, &character, 1);
		}
		hash = fnv_bytes(hash, "", 1);
	}
	for (index = 0; index < recipe->root_count + recipe->strip_tag_count; index++)
	{
		struct scene_pack_recipe_tag const *tag = index < recipe->root_count ? &recipe->roots[index] :
			&recipe->strip_tags[index - recipe->root_count];
		char const *path = tag->path;
		unsigned char group[4];

		hash = fnv_bytes(hash, index < recipe->root_count ? "t" : "s", 1);
		write_u32(group, tag->group_tag);
		hash = fnv_bytes(hash, group, 4);
		for (; *path; path++)
		{
			unsigned char character = (unsigned char)lower((unsigned char)*path);

			hash = fnv_bytes(hash, &character, 1);
		}
		hash = fnv_bytes(hash, "", 1);
	}
	for (index = 0; index < recipe->strip_group_count; index++)
	{
		unsigned char group[4];

		write_u32(group, recipe->strip_groups[index]);
		hash = fnv_bytes(hash, "g", 1);
		hash = fnv_bytes(hash, group, 4);
	}
	for (index = 0; index < recipe->custom_edition_map_count; index++)
	{
		struct scene_pack_recipe_custom_edition_map const *map = &recipe->custom_edition_maps[index];
		char const *name = map->name;
		unsigned char identity[8];

		hash = fnv_bytes(hash, "c", 1);
		for (; *name; name++)
		{
			unsigned char character = (unsigned char)lower((unsigned char)*name);

			hash = fnv_bytes(hash, &character, 1);
		}
		write_u32(identity, map->file_size);
		write_u32(identity + 4, map->checksum);
		hash = fnv_bytes(hash, identity, sizeof(identity));
	}
	for (index = 0; index < recipe->keep_count; index++)
	{
		char const *text;

		hash = fnv_bytes(hash, "k", 1);
		for (text = recipe->keeps[index].graph; *text; text++)
		{
			unsigned char character = (unsigned char)lower((unsigned char)*text);

			hash = fnv_bytes(hash, &character, 1);
		}
		hash = fnv_bytes(hash, "|", 1);
		hash = fnv_bytes(hash, recipe->keeps[index].animation, strlen(recipe->keeps[index].animation) + 1);
	}
	snprintf(version, sizeof(version), "v%d.%d%s", SCENE_PACK_VERSION, SCENE_PACK_BUILDER_VERSION,
		recipe->custom_edition_map_count ? scene_pack_custom_edition_builder_version() : "");
	recipe->hash = fnv_bytes(hash, version, strlen(version));
	return TRUE;
}

unsigned long scene_pack_recipe_size(
	void)
{
	return sizeof(struct scene_pack_recipe);
}

char const *scene_pack_recipe_custom_edition_map(
	struct scene_pack_recipe const *recipe,
	short index)
{
	return index >= 0 && index < recipe->custom_edition_map_count ? recipe->custom_edition_maps[index].name : NULL;
}

boolean scene_pack_recipe_root_is_multiplayer_unit(
	struct scene_pack_recipe_tag const *tag)
{
	return tag->group_tag == 'unit' && !tag->path[0];
}

/* ---------- the source map */

/* an Xbox map's header: its signatures, version, sizes */
static boolean source_header_valid(unsigned char const *header, unsigned long file_size)
{
	unsigned long file_length = read_u32(header + 8);
	unsigned long tag_data_offset = read_u32(header + 16);
	unsigned long tag_data_size = read_u32(header + 20);

	return read_u32(header) == CACHE_FILE_HEADER_SIGNATURE && read_u32(header + 0x7FC) == CACHE_FILE_FOOTER_SIGNATURE &&
		read_u32(header + 4) == CACHE_FILE_XBOX_VERSION &&
		file_length >= CACHE_FILE_HEADER_SIZE && file_length <= 0x7FFFFFFFUL &&
		tag_data_offset >= CACHE_FILE_HEADER_SIZE && tag_data_size >= TAG_HEADER_SIZE &&
		tag_data_size <= XBOX_TAG_CACHE_SIZE && tag_data_offset <= file_length &&
		tag_data_size <= file_length - tag_data_offset && file_size >= CACHE_FILE_HEADER_SIZE;
}

static void map_path(char *path, long path_size, char const *maps_directory, char const *map)
{
	long length = (long)strlen(maps_directory);

	snprintf(path, path_size, "%s%s%s.map", maps_directory,
		length && maps_directory[length - 1] != '/' && maps_directory[length - 1] != '\\' ? "/" : "", map);
}

/* the file's size, or NONE */
static long file_size_get(FILE *file)
{
	long size;

	if (fseek(file, 0, SEEK_END))
		return NONE;
	size = ftell(file);
	if (fseek(file, 0, SEEK_SET))
		return NONE;
	return size;
}

/* the map of that name in maps_directory, an Xbox map: its header (if not
NULL), its file's size, its identity as packs record it (FALSE if it is not
there or not one) */
static boolean map_identity(char const *maps_directory, char const *map, unsigned char *header_out, long *size_out,
	unsigned long long *source_hash)
{
	char path[512];
	unsigned char header[CACHE_FILE_HEADER_SIZE];
	FILE *file;
	long size;
	boolean found;

	map_path(path, sizeof(path), maps_directory, map);
	file = fopen(path, "rb");
	if (!file)
		return FALSE;
	size = file_size_get(file);
	found = size >= CACHE_FILE_HEADER_SIZE && fread(header, 1, sizeof(header), file) == sizeof(header) &&
		source_header_valid(header, (unsigned long)size);
	fclose(file);
	if (!found)
		return FALSE;
	if (header_out)
		memcpy(header_out, header, sizeof(header));
	if (size_out)
		*size_out = size;
	if (source_hash)
	{
		unsigned char size_bytes[4];

		write_u32(size_bytes, (unsigned long)size);
		*source_hash = fnv_bytes(fnv_bytes(FNV_BASIS, header, sizeof(header)), size_bytes, 4);
	}
	return TRUE;
}

static FILE *copy_open(char const *map, unsigned char const *header, unsigned long *token);

boolean scene_pack_source_check(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char *map_name,
	long map_name_size,
	unsigned long long *source_hash,
	char *why,
	long why_size)
{
	if (recipe->custom_edition_map_count)
		return scene_pack_ce_source_check(recipe, maps_directory, map_name, map_name_size, source_hash, why, why_size);
	if (scene_pack_source_find(recipe, maps_directory, map_name, map_name_size, source_hash))
		return TRUE;
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "none of its maps (%s...) is in the maps folder", recipe->maps[0]);
	return FALSE;
}

boolean scene_pack_source_find(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char *map_name,
	long map_name_size,
	unsigned long long *source_hash)
{
	short index;

	if (recipe->custom_edition_map_count)
		return scene_pack_ce_source_check(recipe, maps_directory, map_name, map_name_size, source_hash, NULL, 0);

	for (index = 0; index < recipe->map_count; index++)
	{
		if (!map_identity(maps_directory, recipe->maps[index], NULL, NULL, source_hash))
			continue;
		if (map_name)
			snprintf(map_name, map_name_size, "%s", recipe->maps[index]);
		return TRUE;
	}
	return FALSE;
}

boolean scene_pack_source_identity(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char const *map,
	unsigned long long *source_hash)
{
	short index;

	if (!map || recipe->custom_edition_map_count)
		return FALSE;
	for (index = 0; index < recipe->map_count; index++)
	{
		if (names_match(recipe->maps[index], map))
			return map_identity(maps_directory, recipe->maps[index], NULL, NULL, source_hash);
	}
	return FALSE;
}

/* the map's decompressed copy (scene_pack_copy_source), opened, if it is
that map's: its header header (the maps folder's map's) byte for byte, a
checksum in it, and room in it for the map's whole length. NULL if not */
static FILE *copy_open(char const *map, unsigned char const *header, unsigned long *token)
{
	unsigned char copy_header[CACHE_FILE_HEADER_SIZE];
	unsigned long checksum = read_u32(header + 0x64);
	FILE *file;
	long size;

	if (!scene_pack_copy_source || !scene_pack_copy_source->open || checksum == 0 || checksum == 0xFFFFFFFFUL)
		return NULL;
	file = scene_pack_copy_source->open(map, token);
	if (!file)
		return NULL;
	size = file_size_get(file);
	if (size < 0 || (unsigned long)size < read_u32(header + 8) ||
		fread(copy_header, 1, sizeof(copy_header), file) != sizeof(copy_header) ||
		memcmp(copy_header, header, sizeof(copy_header)))
	{
		fclose(file);
		return NULL;
	}
	return file;
}

boolean scene_pack_source_seekable(
	char const *maps_directory,
	char const *map)
{
	unsigned char header[CACHE_FILE_HEADER_SIZE];
	long size = 0;
	unsigned long token = 0;
	FILE *copy;

	if (!map_identity(maps_directory, map, header, &size, NULL))
		return FALSE;
	if ((unsigned long)size == read_u32(header + 8))
		return TRUE;
	copy = copy_open(map, header, &token);
	if (copy)
		fclose(copy);
	return copy != NULL;
}

static boolean source_cancelled(struct source_reader *reader)
{
	return reader->cancel && *reader->cancel;
}

static void source_close(struct source_reader *reader)
{
	if (reader->stream_open)
		inflateEnd(&reader->stream);
	if (reader->file)
		fclose(reader->file);
	if (reader->input)
		free(reader->input);
	if (reader->bounce)
		free(reader->bounce);
	memset(reader, 0, sizeof(*reader));
}

/* from the start of the map's data again (after its header) */
static boolean source_rewind(struct source_reader *reader)
{
	if (fseek(reader->file, CACHE_FILE_HEADER_SIZE, SEEK_SET))
		return FALSE;
	reader->position = CACHE_FILE_HEADER_SIZE;
	reader->ended = FALSE;
	if (reader->compressed)
	{
		if (reader->stream_open)
			inflateEnd(&reader->stream);
		memset(&reader->stream, 0, sizeof(reader->stream));
		reader->stream.zalloc = zlib_allocate;
		reader->stream.zfree = zlib_free;
		reader->stream_open = inflateInit(&reader->stream) == Z_OK;
		return reader->stream_open;
	}
	return TRUE;
}

/* (read from a copy) whether it is still the map's: its header, and its
token (the game has not begun another copy into it meanwhile) */
static boolean source_copy_unchanged(struct source_reader *reader)
{
	unsigned char header[CACHE_FILE_HEADER_SIZE];

	if (!reader->using_copy)
		return TRUE;
	return !fseek(reader->file, 0, SEEK_SET) && fread(header, 1, sizeof(header), reader->file) == sizeof(header) &&
		!memcmp(header, reader->map_header, sizeof(header)) &&
		(!scene_pack_copy_source->unchanged || scene_pack_copy_source->unchanged(reader->copy_token));
}

/* the map at path (named map), its header read to header; with try_copy,
read from its decompressed copy instead when there is one of it
(copy_open) */
static boolean source_open(struct source_reader *reader, char const *path, char const *map, unsigned char *header,
	int volatile const *cancel, boolean try_copy)
{
	long size;

	memset(reader, 0, sizeof(*reader));
	reader->cancel = cancel;
	reader->file = fopen(path, "rb");
	if (!reader->file)
		return FALSE;
	size = file_size_get(reader->file);
	reader->input = malloc(BOUNCE_SIZE);
	reader->bounce = malloc(BOUNCE_SIZE);
	if (size < CACHE_FILE_HEADER_SIZE || !reader->input || !reader->bounce ||
		fread(header, 1, CACHE_FILE_HEADER_SIZE, reader->file) != CACHE_FILE_HEADER_SIZE ||
		!source_header_valid(header, (unsigned long)size))
	{
		source_close(reader);
		return FALSE;
	}
	reader->file_length = read_u32(header + 8);
	/* (a map whose file is its whole length is inflated already) */
	reader->compressed = (unsigned long)size != reader->file_length;
	if (reader->compressed && try_copy)
	{
		unsigned long token = 0;
		FILE *copy = copy_open(map, header, &token);

		if (copy)
		{
			fclose(reader->file);
			reader->file = copy;
			reader->compressed = FALSE;
			reader->using_copy = TRUE;
			reader->copy_token = token;
			memcpy(reader->map_header, header, CACHE_FILE_HEADER_SIZE);
		}
	}
	if (!source_rewind(reader))
	{
		source_close(reader);
		return FALSE;
	}
	return TRUE;
}

/* size bytes of the map's (inflated) file at offset, at or after where the
last read ended, to buffer (NULL: passed over) */
static boolean source_read(struct source_reader *reader, unsigned long offset, unsigned long size,
	unsigned char *buffer)
{
	if (offset < reader->position || offset > reader->file_length || size > reader->file_length - offset)
		return FALSE;
	if (!reader->compressed)
	{
		unsigned long done = 0;

		if (fseek(reader->file, (long)offset, SEEK_SET))
			return FALSE;
		/* (in pieces: the pauses between them, scene_pack_io_hook) */
		while (buffer && done < size)
		{
			unsigned long chunk = size - done > 4 * BOUNCE_SIZE ? 4 * BOUNCE_SIZE : size - done;

			if (source_cancelled(reader) || fread(buffer + done, 1, chunk, reader->file) != chunk)
				return FALSE;
			done += chunk;
			reader->phase_read += chunk;
			io_done_source(chunk);
			if (reader->progress && reader->phase_total)
				*reader->progress = reader->progress_base +
					(long)(MIN(reader->phase_read, reader->phase_total) * 49 / reader->phase_total);
		}
		reader->position = offset + size;
		reader->read_bytes += size;
		return TRUE;
	}
	/* (passed over to offset, then into buffer) */
	while (reader->position < offset + size)
	{
		boolean skipping = reader->position < offset;
		unsigned long want = skipping ? offset - reader->position : offset + size - reader->position;
		unsigned char *out = skipping || !buffer ? reader->bounce : buffer + (reader->position - offset);
		int result;

		if (source_cancelled(reader) || reader->ended)
			return FALSE;
		if (skipping || !buffer)
			want = want > BOUNCE_SIZE ? BOUNCE_SIZE : want;
		if (!reader->stream.avail_in)
		{
			size_t read = fread(reader->input, 1, BOUNCE_SIZE, reader->file);

			if (!read)
				return FALSE;
			io_done_source((unsigned long)read);
			reader->stream.next_in = reader->input;
			reader->stream.avail_in = (uInt)read;
		}
		reader->stream.next_out = out;
		reader->stream.avail_out = (uInt)want;
		result = inflate(&reader->stream, Z_NO_FLUSH);
		if (result == Z_STREAM_END)
			reader->ended = TRUE;
		else if (result != Z_OK && result != Z_BUF_ERROR)
			return FALSE;
		reader->position += want - reader->stream.avail_out;
		reader->read_bytes += want - reader->stream.avail_out;
		if (reader->progress && reader->file_length)
			*reader->progress = reader->progress_base +
				(long)((unsigned long long)reader->position * 49 / reader->file_length);
		if (result == Z_BUF_ERROR && reader->stream.avail_out == want && reader->stream.avail_in)
			return FALSE;
	}
	return TRUE;
}

/* ---------- the build's memory */

static boolean grow(void **array, long *capacity, long count, long element_size, long most)
{
	void *grown;
	long wanted;

	if (count < *capacity)
		return TRUE;
	if (count >= most)
		return FALSE;
	wanted = *capacity ? *capacity * 2 : 64;
	if (wanted > most)
		wanted = most;
	grown = realloc(*array, (unsigned long)wanted * element_size);
	if (!grown)
		return FALSE;
	*array = grown;
	*capacity = wanted;
	return TRUE;
}

static void build_fail(struct build_state *state, char const *format, char const *detail)
{
	if (!state->failed)
	{
		snprintf(state->result->why, sizeof(state->result->why), format, detail ? detail : "");
		state->failed = TRUE;
	}
}

/* the pack's tag data grown by size bytes (4-aligned, zeroed): their offset */
static unsigned long blob_alloc(struct build_state *state, unsigned long size)
{
	unsigned long offset = (state->blob_size + 3) & ~3UL;
	unsigned long end = offset + ((size + 3) & ~3UL);

	if (end < offset || end > SCENE_PACK_MAXIMUM_TAG_BYTES)
	{
		build_fail(state, "the pack would be larger than a pack may be%s", NULL);
		return 0;
	}
	if (end > state->blob_capacity)
	{
		unsigned long capacity = state->blob_capacity ? state->blob_capacity : 256 * 1024;
		unsigned char *grown;

		while (capacity < end)
			capacity *= 2;
		if (capacity > SCENE_PACK_MAXIMUM_TAG_BYTES)
			capacity = SCENE_PACK_MAXIMUM_TAG_BYTES;
		grown = realloc(state->blob, capacity);
		if (!grown)
		{
			build_fail(state, "there is no memory for the pack%s", NULL);
			return 0;
		}
		memset(grown + state->blob_capacity, 0, capacity - state->blob_capacity);
		state->blob = grown;
		state->blob_capacity = capacity;
	}
	state->blob_size = end;
	return offset;
}

/* the source's size bytes at the Xbox address, if they are in its tag
data: NULL otherwise */
static unsigned char *source_at(struct build_state const *state, unsigned long address, unsigned long size)
{
	unsigned long offset = address - state->source_address;

	if (address < state->source_address || offset > state->tags_size || size > state->tags_size - offset)
		return NULL;
	return state->tags + offset;
}

static unsigned long link_address(unsigned long offset)
{
	return SCENE_PACK_LINK_BASE + offset;
}

/* a source tag (by its index in the source's table): its instance */
static unsigned char *source_instance(struct build_state const *state, long index)
{
	return source_at(state, state->instances + (unsigned long)index * TAG_INSTANCE_SIZE, TAG_INSTANCE_SIZE);
}

/* the source tag a tag index names, or NONE */
static long source_tag(struct build_state const *state, long tag_index)
{
	short absolute_index = (short)tag_index;
	unsigned char *instance;

	if (tag_index == NONE || absolute_index < 0 || absolute_index >= state->source_count)
		return NONE;
	instance = source_instance(state, absolute_index);
	return instance && (long)read_u32(instance + 12) == tag_index ? absolute_index : NONE;
}

static char const *source_tag_name(struct build_state const *state, long index)
{
	unsigned char *instance = source_instance(state, index);
	unsigned long address = instance ? read_u32(instance + 16) : 0;
	unsigned char *name = source_at(state, address, 1);

	if (!name || !memchr(name, 0, state->tags_size - (unsigned long)(name - state->tags)))
		return NULL;
	return (char const *)name;
}

static boolean stripped(struct build_state const *state, unsigned long group_tag, char const *name)
{
	struct scene_pack_recipe const *recipe = state->recipe;
	short index;

	for (index = 0; index < recipe->strip_group_count; index++)
	{
		if (recipe->strip_groups[index] == group_tag)
			return TRUE;
	}
	for (index = 0; index < recipe->strip_tag_count; index++)
	{
		if (recipe->strip_tags[index].group_tag == group_tag && names_match(recipe->strip_tags[index].path, name))
			return TRUE;
	}
	return FALSE;
}

/* the source tag (by its index) in the pack: its place there, NONE if it
is stripped (or cannot be: damaged) */
static long pack_tag_add(struct build_state *state, long index)
{
	unsigned char *instance;
	unsigned long group_tag;
	struct tag_schema_group const *group;
	struct build_tag *tag;
	char const *name;

	if (index == NONE)
		return NONE;
	if (state->pack_index[index] != NONE)
		return state->pack_index[index] >= 0 ? state->pack_index[index] : NONE;
	instance = source_instance(state, index);
	name = source_tag_name(state, index);
	group_tag = instance ? read_u32(instance) : 0;
	/* (a Custom Edition map's gbxmodel made this build's model as it joins
	the pack: scene_pack_ce.c) */
	if (instance && name && state->custom_edition && group_tag == 'mod2' && !stripped(state, group_tag, name) &&
		!stripped(state, 'mode', name))
	{
		char why[SCENE_PACK_WHY_SIZE];

		if (!scene_pack_ce_model_convert(state->custom_edition, read_u32(instance + 20), index, why, sizeof(why)))
		{
			build_fail(state, "%s", why);
			return NONE;
		}
		write_u32(instance, 'mode');
		write_u32(instance + 4, (unsigned long)NONE);
		write_u32(instance + 8, (unsigned long)NONE);
		group_tag = 'mode';
	}
	if (!instance || !name || stripped(state, group_tag, name))
	{
		/* (-2: left out) */
		state->pack_index[index] = -2;
		state->result->stripped_count++;
		return NONE;
	}
	group = schema_group_get(group_tag);
	if (!group || !group->definition || halo_tag_layout_root(group_tag) == HALO_TAG_LAYOUT_UNKNOWN ||
		group_tag == 'scnr' || group_tag == 'sbsp' || group_tag == 'matg')
	{
		/* (a group a pack cannot hold: as if stripped) */
		state->pack_index[index] = -2;
		state->result->stripped_count++;
		return NONE;
	}
	if (state->pack_tag_count >= SCENE_PACK_MAXIMUM_TAGS)
	{
		build_fail(state, "the pack would hold more tags than a pack may%s", NULL);
		return NONE;
	}
	tag = &state->pack_tags[state->pack_tag_count];
	memset(tag, 0, sizeof(*tag));
	tag->source_index = index;
	tag->group_tag = group_tag;
	tag->parent_group_tags[0] = group->parent_group_tags[0];
	tag->parent_group_tags[1] = group->parent_group_tags[1];
	tag->source_base = read_u32(instance + 20);
	tag->root_size = (unsigned long)group->definition->size;
	tag->name = state->custom_edition ? scene_pack_ce_tag_name(state->custom_edition, index, name) : name;
	if (!source_at(state, tag->source_base, tag->root_size))
	{
		char text[5];

		build_fail(state, "the source map's tag of group '%s' is outside its tag data", scene_pack_group_text(group_tag, text));
		return NONE;
	}
	state->pack_index[index] = state->pack_tag_count;
	return state->pack_tag_count++;
}

/* ---------- the walks */

/* the source's bytes, or a loaded pack's (its addresses its own): NULL if
they are not there */
typedef unsigned char *(*walk_resolve_proc)(void *context, unsigned long address, unsigned long size);
/* a field of the schema's a walk meets (a reference, a tag index, a file's
data), at address */
typedef void (*walk_visit_proc)(void *context, struct tag_schema_field const *field, unsigned long address);

struct schema_walk
{
	void *context;
	walk_resolve_proc resolve;
	walk_visit_proc visit;
	long walked;
	boolean failed;
};

/* an element of a schema's definition at address, through its fields */
static void schema_walk_element(struct schema_walk *walk, struct tag_schema_definition const *definition,
	unsigned long address, short depth)
{
	struct tag_schema_field const *field;

	if (walk->failed)
		return;
	if (depth > MAXIMUM_WALK_DEPTH || ++walk->walked > MAXIMUM_WALK_ELEMENTS ||
		!walk->resolve(walk->context, address, (unsigned long)definition->size))
	{
		walk->failed = TRUE;
		return;
	}
	for (field = definition->fields; field->type != _tag_schema_terminator && !walk->failed; field++)
	{
		short index;

		for (index = 0; index < field->count && !walk->failed; index++)
		{
			unsigned long at = address + (unsigned long)field->offset + (unsigned long)index * field->size;

			switch (field->type)
			{
			case _tag_schema_struct:
				schema_walk_element(walk, field->definition, at, depth + 1);
				break;
			case _tag_schema_block:
			{
				struct tag_schema_definition const *element = field->definition;
				unsigned char *block = walk->resolve(walk->context, at, 12);
				long count = block ? (long)read_u32(block) : 0;
				unsigned long elements = block ? read_u32(block + 4) : 0;
				long element_index;

				if (!block || count < 0 || (count && (!element->size ||
					(unsigned long)count > 0x7FFFFFFFUL / (unsigned long)element->size ||
					!walk->resolve(walk->context, elements, (unsigned long)count * element->size))))
				{
					walk->failed = TRUE;
					break;
				}
				for (element_index = 0; element_index < count && !walk->failed; element_index++)
					schema_walk_element(walk, element, elements + (unsigned long)element_index * element->size, depth + 1);
				break;
			}
			case _tag_schema_reference:
			case _tag_schema_tag_index:
			case _tag_schema_file_data:
				walk->visit(walk->context, field, at);
				break;
			}
		}
	}
}

static unsigned char *build_resolve(void *context, unsigned long address, unsigned long size)
{
	return source_at(context, address, size);
}

/* (the closure) a tag index's tag needed too */
static void closure_visit(void *context, struct tag_schema_field const *field, unsigned long address)
{
	struct build_state *state = context;
	unsigned char *value = source_at(state, address, 4);

	if (field->type == _tag_schema_tag_index && value)
		pack_tag_add(state, source_tag(state, (long)read_u32(value)));
}

/* (the closure) every reference the layouts know: their tags needed too */
static void layout_references(struct build_state *state, unsigned short layout, unsigned long address, short depth)
{
	long count = halo_tag_layout_field_count(layout);
	long index;

	if (state->failed)
		return;
	if (depth > MAXIMUM_WALK_DEPTH || ++state->walked > MAXIMUM_WALK_ELEMENTS)
	{
		build_fail(state, "the source map's tags go deeper than tags do%s", NULL);
		return;
	}
	for (index = 0; index < count && !state->failed; index++)
	{
		struct halo_tag_layout_field field;
		unsigned long at;

		if (!halo_tag_layout_field(layout, index, &field))
			continue;
		at = address + field.offset;
		switch (field.kind)
		{
		case HALO_TAG_LAYOUT_FIELD_BLOCK:
		{
			unsigned char *block = source_at(state, at, 12);
			long elements = block ? (long)read_u32(block) : 0;
			unsigned long first = block ? read_u32(block + 4) : 0;
			long element;

			if (!block || elements < 0 || (elements && (!field.child_size ||
				(unsigned long)elements > SCENE_PACK_MAXIMUM_TAG_BYTES / field.child_size ||
				!source_at(state, first, (unsigned long)elements * field.child_size))))
			{
				build_fail(state, "the source map has a block outside its tag data%s", NULL);
				break;
			}
			if (field.child != HALO_TAG_LAYOUT_NONE)
			{
				for (element = 0; element < elements && !state->failed; element++)
					layout_references(state, field.child, first + (unsigned long)element * field.child_size, depth + 1);
			}
			break;
		}
		case HALO_TAG_LAYOUT_FIELD_REFERENCE:
		{
			unsigned char *reference = source_at(state, at, 16);

			if (reference)
				pack_tag_add(state, source_tag(state, (long)read_u32(reference + 12)));
			break;
		}
		case HALO_TAG_LAYOUT_FIELD_STRUCT:
			if (field.child != HALO_TAG_LAYOUT_NONE)
				layout_references(state, field.child, at, depth + 1);
			break;
		}
	}
}

/* a model geometry part's layout (its buffers' pointers at 0x50 and 0x64),
found once among a model's: HALO_TAG_LAYOUT_NONE if none */
static unsigned short model_part_layout_find(unsigned short layout, short depth)
{
	long count = halo_tag_layout_field_count(layout);
	long index;

	if (depth > 8)
		return HALO_TAG_LAYOUT_NONE;
	for (index = 0; index < count; index++)
	{
		struct halo_tag_layout_field field;

		if (!halo_tag_layout_field(layout, index, &field) || field.child == HALO_TAG_LAYOUT_NONE ||
			(field.kind != HALO_TAG_LAYOUT_FIELD_BLOCK && field.kind != HALO_TAG_LAYOUT_FIELD_STRUCT))
		{
			continue;
		}
		if (field.kind == HALO_TAG_LAYOUT_FIELD_BLOCK && field.child_size == MODEL_PART_SIZE)
		{
			long child_count = halo_tag_layout_field_count(field.child);
			long child_index;
			short pointers = 0;

			for (child_index = 0; child_index < child_count; child_index++)
			{
				struct halo_tag_layout_field child;

				if (halo_tag_layout_field(field.child, child_index, &child) && child.kind == HALO_TAG_LAYOUT_FIELD_POINTER &&
					(child.offset == MODEL_PART_TRIANGLE_HARDWARE || child.offset == MODEL_PART_VERTEX_HARDWARE))
				{
					pointers++;
				}
			}
			if (pointers == 2)
				return field.child;
		}
		{
			unsigned short found = model_part_layout_find(field.child, depth + 1);

			if (found != HALO_TAG_LAYOUT_NONE)
				return found;
		}
	}
	return HALO_TAG_LAYOUT_NONE;
}

/* ---------- the copy */

/* the pack's copy of the source's bytes there (exactly those): its offset,
or NONE */
static long extent_exact(struct build_state const *state, unsigned long source, unsigned long size)
{
	long index;

	/* (only for blocks and data copied a second time: rare in a map) */
	for (index = state->extent_count - 1; index >= 0; index--)
	{
		if (state->extents[index].source == source && state->extents[index].size == size)
			return (long)state->extents[index].offset;
	}
	return NONE;
}

static void extent_add(struct build_state *state, unsigned long source, unsigned long size, unsigned long offset)
{
	if (!grow((void **)&state->extents, &state->extent_capacity, state->extent_count, sizeof(struct build_extent),
		4 * 1024 * 1024))
	{
		build_fail(state, "there is no memory for the pack's extents%s", NULL);
		return;
	}
	state->extents[state->extent_count].source = source;
	state->extents[state->extent_count].size = size;
	state->extents[state->extent_count].offset = offset;
	state->extent_count++;
	state->extents_sorted = FALSE;
}

static int extent_compare(void const *a, void const *b)
{
	struct build_extent const *left = a;
	struct build_extent const *right = b;

	return left->source < right->source ? -1 : left->source > right->source ? 1 : 0;
}

/* where the source's byte at address is in the pack (a field of a copied
tag): its offset, or NONE */
static long extent_map(struct build_state *state, unsigned long address, unsigned long size)
{
	long low = 0, high;

	if (!state->extents_sorted)
	{
		qsort(state->extents, state->extent_count, sizeof(struct build_extent), extent_compare);
		state->extents_sorted = TRUE;
	}
	high = state->extent_count;
	/* (the last extent starting at or before address) */
	while (low < high)
	{
		long middle = (low + high) / 2;

		if (state->extents[middle].source <= address)
			low = middle + 1;
		else
			high = middle;
	}
	for (low = low - 1; low >= 0 && low >= high - 4; low--)
	{
		struct build_extent const *extent = &state->extents[low];

		if (address >= extent->source && address - extent->source <= extent->size &&
			size <= extent->size - (address - extent->source))
		{
			return (long)(extent->offset + (address - extent->source));
		}
	}
	return NONE;
}

static long buffer_add(struct build_state *state, unsigned long header, boolean index, unsigned long size)
{
	unsigned long array = index ? state->index_buffers : state->vertex_buffers;
	long array_count = index ? state->index_buffer_count : state->vertex_buffer_count;
	unsigned char *bytes;
	unsigned long data;
	long buffer;

	if (header < array || (header - array) % BUFFER_HEADER_SIZE || (header - array) / BUFFER_HEADER_SIZE >= (unsigned long)array_count ||
		!(bytes = source_at(state, header, BUFFER_HEADER_SIZE)))
	{
		build_fail(state, "the source map has a model part's buffer not of its own%s", NULL);
		return NONE;
	}
	data = read_u32(bytes + 4);
	if (!size || !source_at(state, data, size))
	{
		build_fail(state, "the source map has a model part's vertices or indices outside its tag data%s", NULL);
		return NONE;
	}
	for (buffer = 0; buffer < state->buffer_count; buffer++)
	{
		if (state->buffers[buffer].source_header == header && state->buffers[buffer].index == index)
		{
			if (size > state->buffers[buffer].size)
				state->buffers[buffer].size = size;
			return buffer;
		}
	}
	if (!grow((void **)&state->buffers, &state->buffer_capacity, state->buffer_count, sizeof(struct build_buffer),
		SCENE_PACK_MAXIMUM_BUFFERS))
	{
		build_fail(state, "the pack would have more buffers than a pack may%s", NULL);
		return NONE;
	}
	memset(&state->buffers[state->buffer_count], 0, sizeof(struct build_buffer));
	state->buffers[state->buffer_count].source_header = header;
	state->buffers[state->buffer_count].source_data = data;
	state->buffers[state->buffer_count].size = size;
	state->buffers[state->buffer_count].index = index;
	return state->buffer_count++;
}

static void buffer_fixup(struct build_state *state, unsigned long location, long buffer, boolean data)
{
	if (buffer == NONE)
		return;
	if (!grow((void **)&state->buffer_fixups, &state->buffer_fixup_capacity, state->buffer_fixup_count,
		sizeof(struct build_buffer_fixup), 4 * SCENE_PACK_MAXIMUM_BUFFERS))
	{
		build_fail(state, "there is no memory for the pack's buffers%s", NULL);
		return;
	}
	state->buffer_fixups[state->buffer_fixup_count].location = location;
	state->buffer_fixups[state->buffer_fixup_count].buffer = buffer;
	state->buffer_fixups[state->buffer_fixup_count].data = data;
	state->buffer_fixup_count++;
}

/* a model part's buffers (the part's at source, copied to offset): copied
with it, its pointers to them made the pack's once they have places */
/* a buffer the builder made (a Custom Edition model part's): its place */
static long buffer_made(struct build_state *state, unsigned char *bytes, unsigned long size, boolean index)
{
	if (!bytes || !size || !grow((void **)&state->buffers, &state->buffer_capacity, state->buffer_count,
		sizeof(struct build_buffer), SCENE_PACK_MAXIMUM_BUFFERS))
	{
		free(bytes);
		build_fail(state, "the pack would have more buffers than a pack may%s", NULL);
		return NONE;
	}
	memset(&state->buffers[state->buffer_count], 0, sizeof(struct build_buffer));
	state->buffers[state->buffer_count].size = size;
	state->buffers[state->buffer_count].index = index;
	state->buffers[state->buffer_count].made = bytes;
	return state->buffer_count++;
}

static void model_part_buffers(struct build_state *state, unsigned long source, unsigned long offset)
{
	unsigned char *part = source_at(state, source, MODEL_PART_SIZE);

	/* (a Custom Edition model's part: its vertices compressed and strip
	copied from the map's model data, scene_pack_ce.c) */
	if (state->custom_edition && part)
	{
		unsigned char *vertices = NULL, *indices = NULL;
		unsigned long vertex_bytes = 0, index_bytes = 0;
		char why[SCENE_PACK_WHY_SIZE];
		long index_buffer, vertex_buffer;

		if (!scene_pack_ce_part_buffers(state->custom_edition, source, &vertices, &vertex_bytes, &indices, &index_bytes,
			why, sizeof(why)))
		{
			build_fail(state, "%s", why);
			return;
		}
		index_buffer = buffer_made(state, indices, index_bytes, TRUE);
		vertex_buffer = buffer_made(state, vertices, vertex_bytes, FALSE);
		buffer_fixup(state, offset + MODEL_PART_TRIANGLE_BASE, index_buffer, TRUE);
		buffer_fixup(state, offset + MODEL_PART_TRIANGLE_HARDWARE, index_buffer, FALSE);
		buffer_fixup(state, offset + MODEL_PART_VERTEX_BASE, vertex_buffer, TRUE);
		buffer_fixup(state, offset + MODEL_PART_VERTEX_HARDWARE, vertex_buffer, FALSE);
		return;
	}
	unsigned long triangles, vertices;
	short triangle_type, vertex_type;
	long triangle_count, vertex_count, index_count;

	if (!part)
		return;
	triangles = read_u32(part + MODEL_PART_TRIANGLE_HARDWARE);
	vertices = read_u32(part + MODEL_PART_VERTEX_HARDWARE);
	triangle_type = read_s16(part + MODEL_PART_TRIANGLE_TYPE);
	triangle_count = (long)read_u32(part + MODEL_PART_TRIANGLE_COUNT);
	vertex_type = read_s16(part + MODEL_PART_VERTEX_TYPE);
	vertex_count = (long)read_u32(part + MODEL_PART_VERTEX_COUNT);
	if (!triangles || !vertices)
		return;
	index_count = triangle_type == 0 ? 3 * triangle_count : triangle_type == 1 && triangle_count > 0 ? triangle_count + 2 : 0;
	if (vertex_type < 0 || vertex_type >= NUMBER_OF_MODEL_VERTEX_TYPES || vertex_count <= 0 ||
		vertex_count > MAXIMUM_MODEL_PART_VERTICES || triangle_count < 0 || triangle_count > MAXIMUM_MODEL_PART_TRIANGLES ||
		index_count <= 0)
	{
		build_fail(state, "the source map has a model part that cannot be drawn%s", NULL);
		return;
	}
	{
		long index_buffer = buffer_add(state, triangles, TRUE, (unsigned long)index_count * 2);
		long vertex_buffer = buffer_add(state, vertices, FALSE,
			(unsigned long)vertex_count * model_vertex_type_sizes[vertex_type]);

		buffer_fixup(state, offset + MODEL_PART_TRIANGLE_BASE, index_buffer, TRUE);
		buffer_fixup(state, offset + MODEL_PART_TRIANGLE_HARDWARE, index_buffer, FALSE);
		buffer_fixup(state, offset + MODEL_PART_VERTEX_BASE, vertex_buffer, TRUE);
		buffer_fixup(state, offset + MODEL_PART_VERTEX_HARDWARE, vertex_buffer, FALSE);
	}
}

/* a tag reference's or tag index's source tag index made the pack's (NONE
for a tag not in it) */
static unsigned long pack_tag_index(struct build_state const *state, unsigned long source_tag_index)
{
	long index = source_tag(state, (long)source_tag_index);
	long pack = index != NONE ? state->pack_index[index] : NONE;

	return pack >= 0 ? (unsigned long)(((PACK_FIRST_SALT + pack) << 16) | pack) : (unsigned long)NONE;
}

/* whether a data field (at, in the source) is one of an animation kept
without its frames */
static boolean reduced_data_field(struct build_state const *state, unsigned long at)
{
	static unsigned long const fields[3] =
	{
		offsetof(struct animation, frame_info), offsetof(struct animation, default_data), offsetof(struct animation, data)
	};
	long index;
	short field;

	for (index = 0; index < state->reduced_count; index++)
	{
		struct build_reduced_animation const *reduced = &state->reduced[index];

		if (at < reduced->animation || at >= reduced->animation + sizeof(struct animation))
			continue;
		for (field = 0; field < 3; field++)
		{
			if (at == reduced->animation + fields[field])
				return TRUE;
		}
		return FALSE;
	}
	return FALSE;
}

/* the struct of layout at source, copied already to offset: its blocks and
data copied, its pointers made the pack's */
static boolean layout_copy(struct build_state *state, unsigned short layout, unsigned long source,
	unsigned long offset, short depth)
{
	long count = halo_tag_layout_field_count(layout);
	long index;

	if (state->failed)
		return FALSE;
	if (depth > MAXIMUM_WALK_DEPTH || ++state->walked > MAXIMUM_WALK_ELEMENTS)
	{
		build_fail(state, "the source map's tags go deeper than tags do%s", NULL);
		return FALSE;
	}
	for (index = 0; index < count && !state->failed; index++)
	{
		struct halo_tag_layout_field field;
		unsigned long at, out;

		if (!halo_tag_layout_field(layout, index, &field))
			continue;
		at = source + field.offset;
		out = offset + field.offset;
		switch (field.kind)
		{
		case HALO_TAG_LAYOUT_FIELD_BLOCK:
		{
			unsigned char *block = source_at(state, at, 12);
			long elements = block ? (long)read_u32(block) : 0;
			unsigned long first = block ? read_u32(block + 4) : 0;
			unsigned long size = (unsigned long)elements * field.child_size;
			long existing;
			unsigned long copy;
			long element;

			write_u32(state->blob + out + 8, 0);
			if (!elements || !field.child_size)
			{
				write_u32(state->blob + out, 0);
				write_u32(state->blob + out + 4, 0);
				break;
			}
			if (!block || elements < 0 || (unsigned long)elements > SCENE_PACK_MAXIMUM_TAG_BYTES / field.child_size ||
				!source_at(state, first, size))
			{
				build_fail(state, "the source map has a block outside its tag data%s", NULL);
				break;
			}
			existing = extent_exact(state, first, size);
			if (existing != NONE)
			{
				write_u32(state->blob + out + 4, link_address((unsigned long)existing));
				break;
			}
			copy = blob_alloc(state, size);
			if (state->failed)
				break;
			memcpy(state->blob + copy, source_at(state, first, size), size);
			extent_add(state, first, size, copy);
			write_u32(state->blob + out + 4, link_address(copy));
			if (field.child != HALO_TAG_LAYOUT_NONE)
			{
				for (element = 0; element < elements && !state->failed; element++)
				{
					layout_copy(state, field.child, first + (unsigned long)element * field.child_size,
						copy + (unsigned long)element * field.child_size, depth + 1);
				}
			}
			break;
		}
		case HALO_TAG_LAYOUT_FIELD_DATA:
		{
			unsigned char *data = source_at(state, at, 20);

			/* (an animation kept without its frames: none of its bytes) */
			if (state->reduced_count && reduced_data_field(state, at))
				data = NULL;
			long size = data ? (long)read_u32(data) : 0;
			unsigned long address = data ? read_u32(data + 12) : 0;
			long existing;
			unsigned long copy;

			write_u32(state->blob + out + 12, 0);
			write_u32(state->blob + out + 16, 0);
			/* (a file's data, a bitmap's or a sound's, has no bytes here:
			the schema's walk makes its offset the pack's) */
			if (size <= 0 || !source_at(state, address, (unsigned long)size))
				break;
			existing = extent_exact(state, address, (unsigned long)size);
			if (existing != NONE)
			{
				write_u32(state->blob + out + 12, link_address((unsigned long)existing));
				break;
			}
			copy = blob_alloc(state, (unsigned long)size);
			if (state->failed)
				break;
			memcpy(state->blob + copy, source_at(state, address, (unsigned long)size), (unsigned long)size);
			extent_add(state, address, (unsigned long)size, copy);
			write_u32(state->blob + out + 12, link_address(copy));
			break;
		}
		case HALO_TAG_LAYOUT_FIELD_REFERENCE:
		{
			unsigned char *reference = source_at(state, at, 16);
			unsigned long tag_index = reference ? pack_tag_index(state, read_u32(reference + 12)) : (unsigned long)NONE;

			write_u32(state->blob + out + 12, tag_index);
			if (tag_index != (unsigned long)NONE)
			{
				struct build_tag const *tag = &state->pack_tags[(short)tag_index];

				/* (the group the reference says is its tag's: a Custom Edition
				map's gbxmodels and transparent chicago extended shaders are this
				build's models and transparent chicago shaders) */
				write_u32(state->blob + out, tag->group_tag);
				write_u32(state->blob + out + 4, link_address(tag->name_offset));
				write_u32(state->blob + out + 8, (unsigned long)strlen(tag->name));
			}
			else
			{
				write_u32(state->blob + out + 4, 0);
				write_u32(state->blob + out + 8, 0);
			}
			break;
		}
		case HALO_TAG_LAYOUT_FIELD_POINTER:
			/* (the game's at run time, but a model part's buffers: below) */
			write_u32(state->blob + out, 0);
			break;
		case HALO_TAG_LAYOUT_FIELD_STRUCT:
			if (field.child != HALO_TAG_LAYOUT_NONE)
				layout_copy(state, field.child, at, out, depth + 1);
			break;
		}
	}
	if (layout == state->model_part_layout && !state->failed)
		model_part_buffers(state, source, offset);
	return !state->failed;
}

/* ---------- the pixels and samples */

/* (a Custom Edition source's) a bitmap's or permutation's data, made as it
is written */
static void item_add(struct build_state *state, unsigned long location, boolean bitmap, long source_index,
	unsigned long sound_location)
{
	if (!grow((void **)&state->items, &state->item_capacity, state->item_count, sizeof(struct build_item), 256 * 1024))
	{
		build_fail(state, "there is no memory for the pack's pixels and samples%s", NULL);
		return;
	}
	state->items[state->item_count].location = location;
	state->items[state->item_count].bitmap = bitmap;
	state->items[state->item_count].source_index = source_index;
	state->items[state->item_count].sound_location = sound_location;
	state->item_count++;
}

static long range_add(struct build_state *state, unsigned long source, unsigned long size, unsigned long file_length)
{
	long index;

	if (!size || source > file_length || size > file_length - source || size > SCENE_PACK_MAXIMUM_DATA_BYTES)
	{
		build_fail(state, "the source map has pixels or samples outside its file%s", NULL);
		return NONE;
	}
	for (index = state->range_count - 1; index >= 0 && index >= state->range_count - 64; index--)
	{
		if (state->ranges[index].source == source && state->ranges[index].size == size)
			return index;
	}
	if (!grow((void **)&state->ranges, &state->range_capacity, state->range_count, sizeof(struct build_range),
		64 * 1024))
	{
		build_fail(state, "the pack would hold more pixels and samples than a pack may%s", NULL);
		return NONE;
	}
	memset(&state->ranges[state->range_count], 0, sizeof(struct build_range));
	state->ranges[state->range_count].source = source;
	state->ranges[state->range_count].size = size;
	return state->range_count++;
}

static void range_fixup(struct build_state *state, unsigned long location, long range, long delta)
{
	if (range == NONE)
		return;
	if (!grow((void **)&state->range_fixups, &state->range_fixup_capacity, state->range_fixup_count,
		sizeof(struct build_range_fixup), 256 * 1024))
	{
		build_fail(state, "there is no memory for the pack's pixels and samples%s", NULL);
		return;
	}
	state->range_fixups[state->range_fixup_count].location = location;
	state->range_fixups[state->range_fixup_count].range = range;
	state->range_fixups[state->range_fixup_count].delta = delta;
	state->range_fixup_count++;
}

struct fixup_walk
{
	struct build_state *state;
	unsigned long file_length;
	/* (a sound tag's) its root's place in the pack */
	unsigned long sound_location;
};

static unsigned char *fixup_resolve(void *context, unsigned long address, unsigned long size)
{
	return source_at(((struct fixup_walk *)context)->state, address, size);
}

/* (the schema's walk of a copied tag, in the source) its tag indices made
the pack's, its file data (sounds' samples) the pack's data */
static void fixup_visit(void *context, struct tag_schema_field const *field, unsigned long address)
{
	struct fixup_walk *walk = context;
	struct build_state *state = walk->state;
	long out;

	if (field->type == _tag_schema_reference)
		return;
	out = extent_map(state, address, field->type == _tag_schema_tag_index ? 4 : 20);
	if (out == NONE)
	{
		build_fail(state, "the source map has a tag field the copy missed%s", NULL);
		return;
	}
	if (field->type == _tag_schema_tag_index)
	{
		write_u32(state->blob + out, pack_tag_index(state, read_u32(source_at(state, address, 4))));
	}
	else
	{
		unsigned char *data = source_at(state, address, 20);
		long size = (long)read_u32(data);

		write_u32(state->blob + out + 12, 0);
		/* (a Custom Edition map's: the permutation's samples made Xbox
		ADPCM as they are written, scene_pack_ce.c; the only file data the
		schemas know is a sound permutation's samples, at 0x40) */
		if (state->custom_edition)
		{
			if (size > 0 && (unsigned long)out >= offsetof(struct sound_permutation, samples))
				item_add(state, (unsigned long)out - offsetof(struct sound_permutation, samples), FALSE, NONE,
					walk->sound_location);
			else
				write_u32(state->blob + out + 8, 0);
			return;
		}
		if (size > 0)
			range_fixup(state, (unsigned long)out + 8, range_add(state, read_u32(data + 8), (unsigned long)size,
				walk->file_length), 0);
		else
			write_u32(state->blob + out + 8, 0);
	}
}

/* the bytes of pixels the game reads for a bitmap (tag_schema_render.c),
or 0 when it cannot say */
static unsigned long bitmap_read_size(struct bitmap_data const *bitmap)
{
	boolean compressed = (bitmap->flags & (1 << BITMAP_COMPRESSED_BIT)) != 0;
	long bits_per_pixel;
	unsigned long pixels = 0;
	short mipmap;

	if (bitmap->format < 0 || bitmap->format >= NUMBER_OF_BITMAP_FORMATS || bitmap->mipmap_count < 0 ||
		bitmap->mipmap_count > 13 || bitmap->width < 1 || bitmap->height < 1 || bitmap->depth < 1 ||
		bitmap->width > 4096 || bitmap->height > 4096 || bitmap->depth > 4096)
	{
		return 0;
	}
	bits_per_pixel = bitmap_format_bits_per_pixel[bitmap->format];
	for (mipmap = 0; mipmap <= bitmap->mipmap_count; mipmap++)
	{
		unsigned long width = MAX(bitmap->width >> mipmap, 1);
		unsigned long height = MAX(bitmap->height >> mipmap, 1);
		unsigned long depth = MAX(bitmap->depth >> mipmap, 1);

		if (compressed)
		{
			width += (-width) & 3;
			height += (-height) & 3;
		}
		pixels += width * height * depth * (bitmap->type == BITMAP_TYPE_CUBE_MAP ? 6 : 1);
		if (pixels > MAXIMUM_BITMAP_PIXELS)
			return 0;
	}
	return pixels * bits_per_pixel / 8;
}

/* a bitmap tag's pixels the pack's (the group's offset none, each
bitmap's its own, as in the map) */
static void bitmap_fixups(struct build_state *state, struct build_tag const *tag, unsigned long file_length)
{
	unsigned char *source = source_at(state, tag->source_base, sizeof(struct bitmap_group));
	struct bitmap_group group;
	long index;

	if (!source)
		return;
	memcpy(&group, source, sizeof(group));
	write_u32(state->blob + tag->base_offset + offsetof(struct bitmap_group, pixel_data) + 8, 0);
	for (index = 0; index < group.bitmaps.count && !state->failed; index++)
	{
		unsigned long address = (unsigned long)group.bitmaps.address + (unsigned long)index * sizeof(struct bitmap_data);
		unsigned char *bytes = source_at(state, address, sizeof(struct bitmap_data));
		long out = extent_map(state, address, sizeof(struct bitmap_data));
		struct bitmap_data bitmap;
		unsigned long size;

		if (!bytes || out == NONE)
		{
			build_fail(state, "the source map has a bitmap the copy missed%s", NULL);
			return;
		}
		/* (a Custom Edition map's: the pixels laid out as the Xbox's as they
		are written, scene_pack_ce.c) */
		if (state->custom_edition)
		{
			item_add(state, (unsigned long)out, TRUE, tag->source_index, 0);
			continue;
		}
		memcpy(&bitmap, bytes, sizeof(bitmap));
		size = bitmap_read_size(&bitmap);
		if ((unsigned long)bitmap.pixels_size > size)
			size = (unsigned long)bitmap.pixels_size;
		/* (a cache's bitmap's offset is the file's own: the group's is what
		the tools left, which the texture cache adds only to a tag file's
		bitmaps, texture_cache_bitmap_new, never a cache's) */
		range_fixup(state, (unsigned long)out + offsetof(struct bitmap_data, pixels_offset),
			range_add(state, (unsigned long)bitmap.pixels_offset, size, file_length), 0);
	}
}

/* a sound tag's permutations' own tag indices (the sound cache's) the
pack's */
static void sound_fixups(struct build_state *state, struct build_tag const *tag, long pack_index)
{
	unsigned char *source = source_at(state, tag->source_base, sizeof(struct sound_definition));
	struct sound_definition definition;
	unsigned long own = (unsigned long)(((PACK_FIRST_SALT + pack_index) << 16) | pack_index);
	long range_index;

	if (!source)
		return;
	memcpy(&definition, source, sizeof(definition));
	for (range_index = 0; range_index < definition.pitch_ranges.count && !state->failed; range_index++)
	{
		unsigned long range_address = (unsigned long)definition.pitch_ranges.address +
			(unsigned long)range_index * sizeof(struct sound_pitch_range);
		unsigned char *range_bytes = source_at(state, range_address, sizeof(struct sound_pitch_range));
		struct sound_pitch_range range;
		long permutation;

		if (!range_bytes)
			break;
		memcpy(&range, range_bytes, sizeof(range));
		for (permutation = 0; permutation < range.permutations.count; permutation++)
		{
			unsigned long address = (unsigned long)range.permutations.address +
				(unsigned long)permutation * sizeof(struct sound_permutation);
			long out = extent_map(state, address, sizeof(struct sound_permutation));

			if (out == NONE)
			{
				build_fail(state, "the source map has a sound the copy missed%s", NULL);
				return;
			}
			write_u32(state->blob + out + offsetof(struct sound_permutation, unknown2), own);
			write_u32(state->blob + out + offsetof(struct sound_permutation, unknown3), own);
		}
	}
}

static int range_order_compare(void const *a, void const *b)
{
	struct build_range const *const *left = a;
	struct build_range const *const *right = b;

	if ((*left)->source != (*right)->source)
		return (*left)->source < (*right)->source ? -1 : 1;
	return (*left)->size < (*right)->size ? 1 : (*left)->size > (*right)->size ? -1 : 0;
}

/* the ranges given places in the pack's data, in the source's order, those
that overlap sharing their bytes: the pieces to copy (source, size,
offset, as ranges) returned, and their count */
static struct build_range *ranges_place(struct build_state *state, long *piece_count, unsigned long *data_size)
{
	struct build_range **order;
	struct build_range *pieces;
	long count = 0;
	long index;

	*piece_count = 0;
	*data_size = 0;
	order = malloc(sizeof(*order) * (state->range_count ? state->range_count : 1));
	pieces = malloc(sizeof(*pieces) * (state->range_count ? state->range_count : 1));
	if (!order || !pieces)
	{
		if (order)
			free(order);
		if (pieces)
			free(pieces);
		build_fail(state, "there is no memory for the pack's pixels and samples%s", NULL);
		return NULL;
	}
	for (index = 0; index < state->range_count; index++)
		order[index] = &state->ranges[index];
	qsort(order, state->range_count, sizeof(*order), range_order_compare);
	for (index = 0; index < state->range_count; index++)
	{
		struct build_range *range = order[index];
		struct build_range *piece = count ? &pieces[count - 1] : NULL;

		if (piece && range->source < piece->source + piece->size)
		{
			unsigned long end = range->source + range->size;

			if (end > piece->source + piece->size)
			{
				*data_size += end - (piece->source + piece->size);
				piece->size = end - piece->source;
			}
			range->offset = piece->offset + (range->source - piece->source);
		}
		else
		{
			piece = &pieces[count++];
			piece->source = range->source;
			piece->size = range->size;
			piece->offset = *data_size;
			range->offset = *data_size;
			*data_size += range->size;
		}
	}
	free(order);
	if (*data_size > SCENE_PACK_MAXIMUM_DATA_BYTES)
	{
		free(pieces);
		build_fail(state, "the pack would hold more pixels and samples than a pack may%s", NULL);
		return NULL;
	}
	*piece_count = count;
	return pieces;
}

/* ---------- the build */

static boolean source_table_read(struct build_state *state)
{
	unsigned char *header = state->tags;

	/* (a Custom Edition map's tag index: no buffers, its signature at 0x24;
	its loader checked it, scene_pack_ce.c) */
	if (state->custom_edition)
	{
		state->instances = read_u32(header);
		state->source_count = (long)read_u32(header + 12);
		state->vertex_buffer_count = state->index_buffer_count = 0;
		state->vertex_buffers = state->index_buffers = 0;
		return state->tags_size >= 0x28 && read_u32(header + 0x24) == CACHE_FILE_TAG_HEADER_SIGNATURE &&
			state->source_count > 0 && state->source_count <= 0x7FFF &&
			source_at(state, state->instances, (unsigned long)state->source_count * TAG_INSTANCE_SIZE);
	}
	state->instances = read_u32(header);
	state->source_count = (long)read_u32(header + 12);
	state->vertex_buffer_count = (long)read_u32(header + 16);
	state->vertex_buffers = read_u32(header + 20);
	state->index_buffer_count = (long)read_u32(header + 24);
	state->index_buffers = read_u32(header + 28);
	return read_u32(header + 32) == CACHE_FILE_TAG_HEADER_SIGNATURE &&
		state->source_count > 0 && state->source_count <= 0x7FFF &&
		source_at(state, state->instances, (unsigned long)state->source_count * TAG_INSTANCE_SIZE) &&
		state->vertex_buffer_count >= 0 && state->index_buffer_count >= 0 &&
		state->vertex_buffer_count <= 0x100000 && state->index_buffer_count <= 0x100000 &&
		(!state->vertex_buffer_count ||
			source_at(state, state->vertex_buffers, (unsigned long)state->vertex_buffer_count * BUFFER_HEADER_SIZE)) &&
		(!state->index_buffer_count ||
			source_at(state, state->index_buffers, (unsigned long)state->index_buffer_count * BUFFER_HEADER_SIZE));
}

/* the source's globals' multiplayer unit (unit multiplayer), or NONE */
static long source_multiplayer_unit(struct build_state const *state)
{
	long index;

	for (index = 0; index < state->source_count; index++)
	{
		unsigned char *instance = source_instance(state, index);

		if (instance && read_u32(instance) == 'matg')
		{
			/* (game_globals.h: the multiplayer information block at 0x164, its
			first element's unit reference at 0x10, the reference's tag index at
			0x0C of it) */
			unsigned char *globals = source_at(state, read_u32(instance + 20), 0x170);
			unsigned char *information = globals && (long)read_u32(globals + 0x164) > 0 ?
				source_at(state, read_u32(globals + 0x168), 0xA0) : NULL;
			long unit = information ? source_tag(state, (long)read_u32(information + 0x10 + 12)) : NONE;
			unsigned char *unit_instance = unit != NONE ? source_instance(state, unit) : NULL;

			/* (a unit: a biped, or a vehicle, which the game spawns as one) */
			if (unit_instance && (read_u32(unit_instance) == 'bipd' || read_u32(unit_instance + 4) == 'unit' ||
				read_u32(unit_instance + 8) == 'unit'))
			{
				return unit;
			}
			return NONE;
		}
	}
	return NONE;
}

static long source_find(struct build_state const *state, struct scene_pack_recipe_tag const *tag)
{
	long index;

	if (scene_pack_recipe_root_is_multiplayer_unit(tag))
		return source_multiplayer_unit(state);

	for (index = 0; index < state->source_count; index++)
	{
		unsigned char *instance = source_instance(state, index);
		char const *name = source_tag_name(state, index);

		if (instance && name && read_u32(instance) == tag->group_tag && names_match(name, tag->path))
			return index;
	}
	return NONE;
}

static void build_state_free(struct build_state *state, void *scratch)
{
	if (state->tags && state->tags != scratch)
		free(state->tags);
	if (state->pack_index)
		free(state->pack_index);
	if (state->pack_tags)
		free(state->pack_tags);
	if (state->blob)
		free(state->blob);
	if (state->extents)
		free(state->extents);
	if (state->buffers)
	{
		long index;

		for (index = 0; index < state->buffer_count; index++)
		{
			if (state->buffers[index].made)
				free(state->buffers[index].made);
		}
		free(state->buffers);
	}
	if (state->items)
		free(state->items);
	if (state->buffer_fixups)
		free(state->buffer_fixups);
	if (state->ranges)
		free(state->ranges);
	if (state->range_fixups)
		free(state->range_fixups);
	if (state->reduced)
		free(state->reduced);
}

/* (keep lines) each animation graph of the pack's that the recipe keeps
animations of: every animation it does not keep is kept without its frames
(build_animations_patch), so that its bytes are not in the pack */
static void build_animations_plan(struct build_state *state)
{
	struct scene_pack_recipe const *recipe = state->recipe;
	long index;

	for (index = 0; index < state->pack_tag_count && !state->failed; index++)
	{
		struct build_tag const *tag = &state->pack_tags[index];
		unsigned char *source = tag->group_tag == 'antr' ? source_at(state, tag->source_base, sizeof(struct animation_graph)) :
			NULL;
		struct animation_graph graph;
		long animation, keep;
		boolean any = FALSE;

		if (!source)
			continue;
		for (keep = 0; keep < recipe->keep_count && !any; keep++)
			any = names_match(recipe->keeps[keep].graph, tag->name);
		if (!any)
			continue;
		memcpy(&graph, source, sizeof(graph));
		if (graph.animations.count <= 0 || graph.animations.count > 0x7FFF ||
			!source_at(state, (unsigned long)graph.animations.address,
				(unsigned long)graph.animations.count * sizeof(struct animation)))
		{
			continue;
		}
		for (animation = 0; animation < graph.animations.count && !state->failed; animation++)
		{
			unsigned long address = (unsigned long)graph.animations.address + (unsigned long)animation * sizeof(struct animation);
			struct animation const *element = (struct animation const *)source_at(state, address, sizeof(struct animation));
			char name[sizeof(element->name) + 1];
			boolean kept = FALSE;

			memcpy(name, element->name, sizeof(element->name));
			name[sizeof(element->name)] = 0;
			for (keep = 0; keep < recipe->keep_count && !kept; keep++)
				kept = names_match(recipe->keeps[keep].graph, tag->name) && names_match(name, recipe->keeps[keep].animation);
			if (kept)
				continue;
			if (!grow((void **)&state->reduced, &state->reduced_capacity, state->reduced_count,
				sizeof(struct build_reduced_animation), 64 * 1024))
			{
				build_fail(state, "there is no memory to build the pack%s", NULL);
				break;
			}
			state->reduced[state->reduced_count++].animation = address;
			state->reduced_bytes += element->frame_info.size + element->default_data.size + element->data.size;
		}
	}
}

/* (keep lines) the copies of the animations kept without their frames
made ones that move no node, as the tag check makes an animation whose
frames it refuses: no nodes, no frame data, defaults or frame info, not
compressed - its frame count, frame indices, name, type and next animation
its own (a scene never plays it; played, it would hold the rest pose) */
static void build_animations_patch(struct build_state *state)
{
	long index;

	for (index = 0; index < state->reduced_count && !state->failed; index++)
	{
		long out = extent_map(state, state->reduced[index].animation, sizeof(struct animation));
		struct animation *copy;

		if (out == NONE)
		{
			build_fail(state, "the source map has an animation the copy missed%s", NULL);
			return;
		}
		copy = (struct animation *)(state->blob + out);
		copy->node_count = 0;
		copy->frame_size = 0;
		copy->frame_info_type = 0;
		copy->flags &= ~FLAG(_animation_compressed_bit);
		memset(copy->nodes_with_translation_flags, 0, sizeof(copy->nodes_with_translation_flags));
		memset(copy->nodes_with_rotation_flags, 0, sizeof(copy->nodes_with_rotation_flags));
		memset(copy->nodes_with_scale_flags, 0, sizeof(copy->nodes_with_scale_flags));
		copy->compressed_data_offset = 0;
		memset(&copy->frame_info, 0, sizeof(copy->frame_info));
		memset(&copy->default_data, 0, sizeof(copy->default_data));
		memset(&copy->data, 0, sizeof(copy->data));
	}
}

/* the pack's tag data put together from the source's (state->tags): its
tags, their copies, buffers, header and table */
static void build_tags(struct build_state *state, unsigned long file_length)
{
	struct scene_pack_recipe const *recipe = state->recipe;
	unsigned long instances_offset, vertex_headers, index_headers;
	long vertex_count = 0, index_count = 0;
	long index;

	state->model_part_layout = model_part_layout_find(halo_tag_layout_root('mode'), 0);
	/* the recipe's tags, then all they need */
	for (index = 0; index < recipe->root_count && !state->failed; index++)
	{
		long source = source_find(state, &recipe->roots[index]);
		char text[5];

		if (source == NONE || pack_tag_add(state, source) != index)
		{
			if (state->failed)
				break;
			if (scene_pack_recipe_root_is_multiplayer_unit(&recipe->roots[index]))
				snprintf(state->result->why, sizeof(state->result->why), "the map has no multiplayer unit it can take");
			else
				snprintf(state->result->why, sizeof(state->result->why), "the map has no %s %s it can take",
					scene_pack_group_text(recipe->roots[index].group_tag, text), recipe->roots[index].path);
			state->failed = TRUE;
		}
	}
	for (index = 0; index < state->pack_tag_count && !state->failed; index++)
	{
		struct build_tag const *tag = &state->pack_tags[index];
		struct tag_schema_group const *group = schema_group_get(tag->group_tag);
		unsigned short layout = halo_tag_layout_root(tag->group_tag);
		struct schema_walk walk;

		if (layout != HALO_TAG_LAYOUT_NONE)
			layout_references(state, layout, tag->source_base, 0);
		memset(&walk, 0, sizeof(walk));
		walk.context = state;
		walk.resolve = build_resolve;
		walk.visit = closure_visit;
		schema_walk_element(&walk, group->definition, tag->source_base, 0);
		if (walk.failed)
			build_fail(state, "the source map has a tag the schema cannot walk%s", NULL);
	}
	if (state->failed)
		return;

	/* the header, table and names */
	blob_alloc(state, TAG_HEADER_SIZE);
	instances_offset = blob_alloc(state, (unsigned long)state->pack_tag_count * TAG_INSTANCE_SIZE);
	for (index = 0; index < state->pack_tag_count && !state->failed; index++)
	{
		struct build_tag *tag = &state->pack_tags[index];
		unsigned long length = (unsigned long)strlen(tag->name) + 1;

		tag->name_offset = blob_alloc(state, length);
		if (!state->failed)
			memcpy(state->blob + tag->name_offset, tag->name, length);
	}
	/* (keep lines) the animations to take kept ones' frames */
	if (recipe->keep_count)
		build_animations_plan(state);
	/* the tags */
	for (index = 0; index < state->pack_tag_count && !state->failed; index++)
	{
		struct build_tag *tag = &state->pack_tags[index];
		unsigned short layout = halo_tag_layout_root(tag->group_tag);

		tag->base_offset = blob_alloc(state, tag->root_size);
		if (state->failed)
			break;
		memcpy(state->blob + tag->base_offset, source_at(state, tag->source_base, tag->root_size), tag->root_size);
		extent_add(state, tag->source_base, tag->root_size, tag->base_offset);
		if (layout != HALO_TAG_LAYOUT_NONE)
			layout_copy(state, layout, tag->source_base, tag->base_offset, 0);
	}
	if (state->reduced_count && !state->failed)
		build_animations_patch(state);
	/* their tag indices, pixels and samples */
	for (index = 0; index < state->pack_tag_count && !state->failed; index++)
	{
		struct build_tag const *tag = &state->pack_tags[index];
		struct schema_walk walk;
		struct fixup_walk context;

		context.state = state;
		context.file_length = file_length;
		context.sound_location = tag->group_tag == 'snd!' ? tag->base_offset : 0;
		memset(&walk, 0, sizeof(walk));
		walk.context = &context;
		walk.resolve = fixup_resolve;
		walk.visit = fixup_visit;
		schema_walk_element(&walk, schema_group_get(tag->group_tag)->definition, tag->source_base, 0);
		if (walk.failed)
			build_fail(state, "the source map has a tag the schema cannot walk%s", NULL);
		if (tag->group_tag == BITMAP_GROUP_TAG)
			bitmap_fixups(state, tag, file_length);
		else if (tag->group_tag == 'snd!')
			sound_fixups(state, tag, index);
	}
	if (state->failed)
		return;

	/* the buffers: headers, then vertices and indices */
	for (index = 0; index < state->buffer_count; index++)
	{
		if (state->buffers[index].index)
			index_count++;
		else
			vertex_count++;
	}
	vertex_headers = blob_alloc(state, (unsigned long)vertex_count * BUFFER_HEADER_SIZE);
	index_headers = blob_alloc(state, (unsigned long)index_count * BUFFER_HEADER_SIZE);
	vertex_count = index_count = 0;
	for (index = 0; index < state->buffer_count && !state->failed; index++)
	{
		struct build_buffer *buffer = &state->buffers[index];

		buffer->header_offset = buffer->index ? index_headers + (unsigned long)index_count++ * BUFFER_HEADER_SIZE :
			vertex_headers + (unsigned long)vertex_count++ * BUFFER_HEADER_SIZE;
		buffer->data_offset = blob_alloc(state, buffer->size);
		if (state->failed)
			break;
		memcpy(state->blob + buffer->data_offset, buffer->made ? buffer->made :
			source_at(state, buffer->source_data, buffer->size), buffer->size);
		write_u32(state->blob + buffer->header_offset + 4, link_address(buffer->data_offset));
	}
	for (index = 0; index < state->buffer_fixup_count && !state->failed; index++)
	{
		struct build_buffer_fixup const *fixup = &state->buffer_fixups[index];
		struct build_buffer const *buffer = &state->buffers[fixup->buffer];

		write_u32(state->blob + fixup->location, link_address(fixup->data ? buffer->data_offset : buffer->header_offset));
	}
	if (state->failed)
		return;

	/* the header and table */
	write_u32(state->blob + 0, link_address(instances_offset));
	write_u32(state->blob + 4, (unsigned long)NONE);
	write_u32(state->blob + 8, (unsigned long)recipe->hash);
	write_u32(state->blob + 12, (unsigned long)state->pack_tag_count);
	write_u32(state->blob + 16, (unsigned long)vertex_count);
	write_u32(state->blob + 20, vertex_count ? link_address(vertex_headers) : 0);
	write_u32(state->blob + 24, (unsigned long)index_count);
	write_u32(state->blob + 28, index_count ? link_address(index_headers) : 0);
	write_u32(state->blob + 32, CACHE_FILE_TAG_HEADER_SIGNATURE);
	for (index = 0; index < state->pack_tag_count; index++)
	{
		struct build_tag const *tag = &state->pack_tags[index];
		unsigned char *instance = state->blob + instances_offset + (unsigned long)index * TAG_INSTANCE_SIZE;

		write_u32(instance + 0, tag->group_tag);
		write_u32(instance + 4, tag->parent_group_tags[0]);
		write_u32(instance + 8, tag->parent_group_tags[1]);
		write_u32(instance + 12, (unsigned long)(((PACK_FIRST_SALT + index) << 16) | index));
		write_u32(instance + 16, link_address(tag->name_offset));
		write_u32(instance + 20, link_address(tag->base_offset));
		write_u32(instance + 24, 0);
		write_u32(instance + 28, 0);
	}
	state->result->buffer_count = state->buffer_count;
}

static void header_write(unsigned char *header, struct build_state const *state, unsigned long tags_crc,
	unsigned long data_size, unsigned long data_crc, unsigned long long source_hash, char const *map)
{
	memset(header, 0, SCENE_PACK_HEADER_SIZE);
	memcpy(header + HEADER_SIGNATURE, scene_pack_signature, sizeof(scene_pack_signature));
	write_u32(header + HEADER_VERSION, SCENE_PACK_VERSION);
	write_u32(header + HEADER_HEADER_SIZE, SCENE_PACK_HEADER_SIZE);
	write_u32(header + HEADER_FILE_SIZE, SCENE_PACK_HEADER_SIZE + state->blob_size + data_size);
	write_u32(header + HEADER_TAGS_OFFSET, SCENE_PACK_HEADER_SIZE);
	write_u32(header + HEADER_TAGS_SIZE, state->blob_size);
	write_u32(header + HEADER_DATA_OFFSET, SCENE_PACK_HEADER_SIZE + state->blob_size);
	write_u32(header + HEADER_DATA_SIZE, data_size);
	write_u32(header + HEADER_TAG_COUNT, (unsigned long)state->pack_tag_count);
	write_u32(header + HEADER_ROOT_COUNT, (unsigned long)state->recipe->root_count);
	write_u32(header + HEADER_TAGS_CRC, tags_crc);
	write_u32(header + HEADER_DATA_CRC, data_crc);
	write_u32(header + HEADER_BUILDER_VERSION, SCENE_PACK_BUILDER_VERSION);
	write_u64(header + HEADER_RECIPE_HASH, state->recipe->hash);
	write_u64(header + HEADER_SOURCE_HASH, source_hash);
	snprintf((char *)header + HEADER_SOURCE_MAP, SCENE_PACK_NAME_SIZE, "%s", map);
	write_u32(header + HEADER_HEADER_CRC, crc_bytes(0, header, SCENE_PACK_HEADER_SIZE));
}

/* (a Custom Edition source's) the pack's tags of the groups that say which
bitmaps Halo PC keeps in other channels, given to scene_pack_ce.c */
static boolean build_channels(struct build_state *state)
{
	unsigned long *groups = malloc(sizeof(unsigned long) * (unsigned long)(state->pack_tag_count + 1));
	unsigned long *roots = malloc(sizeof(unsigned long) * (unsigned long)(state->pack_tag_count + 1));
	long count = 0;
	long index;

	if (!groups || !roots)
	{
		if (groups)
			free(groups);
		if (roots)
			free(roots);
		build_fail(state, "there is no memory to build the pack%s", NULL);
		return FALSE;
	}
	for (index = 0; index < state->pack_tag_count; index++)
	{
		unsigned long group_tag = state->pack_tags[index].group_tag;

		if (group_tag == 'soso' || group_tag == 'unhi' || group_tag == 'wphi')
		{
			groups[count] = group_tag;
			roots[count] = state->pack_tags[index].source_base;
			count++;
		}
	}
	scene_pack_ce_channels(state->custom_edition, groups, roots, count);
	free(groups);
	free(roots);
	return TRUE;
}

/* (a Custom Edition source's) its bitmaps' pixels and permutations' samples
made this build's and written to data, their fields in the pack's tags made
what was written: the data's size and CRC */
static void build_items(struct build_state *state, FILE *data, unsigned long *data_size, unsigned long *data_crc)
{
	long index;

	for (index = 0; index < state->item_count && !state->failed; index++)
	{
		struct build_item const *item = &state->items[index];
		char why[SCENE_PACK_WHY_SIZE];
		boolean written;

		if (state->cancel && *state->cancel)
		{
			build_fail(state, "it was stopped%s", NULL);
			break;
		}
		if (item->bitmap)
		{
			struct build_tag const *tag = NULL;
			long pack = item->source_index >= 0 && item->source_index < 0x8000 ? state->pack_index[item->source_index] : NONE;

			if (pack >= 0)
				tag = &state->pack_tags[pack];
			written = tag && scene_pack_ce_bitmap_write(state->custom_edition, state->blob + item->location,
				item->source_index, state->blob + tag->base_offset, data, data_size, data_crc, why, sizeof(why));
			if (!tag)
				snprintf(why, sizeof(why), "a bitmap of the pack is not its own");
		}
		else
		{
			written = scene_pack_ce_permutation_write(state->custom_edition, state->blob + item->location,
				state->blob + item->sound_location, data, data_size, data_crc, why, sizeof(why));
		}
		if (!written)
		{
			build_fail(state, "%s", why);
			break;
		}
		if (*data_size > SCENE_PACK_MAXIMUM_DATA_BYTES)
		{
			build_fail(state, "the pack would hold more pixels and samples than a pack may%s", NULL);
			break;
		}
		state->result->progress = 50 + (long)((unsigned long long)(index + 1) * 45 / (unsigned long long)state->item_count);
	}
}

/* the offsets of the pack's bitmaps' pixels and permutations' samples, from
the data's start, made the pack's file offsets */
static void build_items_place(struct build_state *state)
{
	long index;

	for (index = 0; index < state->item_count; index++)
	{
		struct build_item const *item = &state->items[index];
		unsigned long at = item->location + (item->bitmap ? offsetof(struct bitmap_data, pixels_offset) :
			offsetof(struct sound_permutation, samples) + offsetof(struct tag_data, file_offset));
		unsigned long size_at = item->location + (item->bitmap ? offsetof(struct bitmap_data, pixels_size) :
			offsetof(struct sound_permutation, samples));

		if (read_u32(state->blob + size_at))
			write_u32(state->blob + at, SCENE_PACK_FILE_BASE + read_u32(state->blob + at));
		else
			write_u32(state->blob + at, 0);
	}
}

static boolean scene_pack_build_custom_edition(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char const *pack_path,
	void *scratch,
	unsigned long scratch_size,
	int volatile const *cancel,
	struct scene_pack_build_result *result)
{
	struct build_state state;
	unsigned char pack_header[SCENE_PACK_HEADER_SIZE];
	unsigned long long started = vita_host_time_us();
	unsigned long long source_hash = 0;
	char temporary[512];
	char data_path[512];
	unsigned long data_size = 0, data_crc = 0, tags_crc;
	FILE *data = NULL;
	FILE *file = NULL;
	long index;

	memset(result, 0, sizeof(*result));
	memset(&state, 0, sizeof(state));
	state.recipe = recipe;
	state.result = result;
	state.cancel = cancel;
	if (!scene_pack_source_check(recipe, maps_directory, result->map, sizeof(result->map), &source_hash, result->why,
		sizeof(result->why)))
	{
		return FALSE;
	}
	state.custom_edition = scene_pack_ce_open(maps_directory, result->map, scratch, scratch_size, cancel, &result->progress,
		result->why, sizeof(result->why));
	if (!state.custom_edition)
		return FALSE;
	state.tags = scene_pack_ce_tags(state.custom_edition, &state.tags_size);
	state.source_address = (unsigned long)state.tags;
	state.pack_index = malloc(sizeof(long) * 0x8000);
	state.pack_tags = malloc(sizeof(struct build_tag) * SCENE_PACK_MAXIMUM_TAGS);
	if (!state.pack_index || !state.pack_tags)
		build_fail(&state, "there is no memory to build the pack%s", NULL);
	else if (!source_table_read(&state))
		build_fail(&state, "the map's tag header is damaged%s", NULL);
	if (!state.failed)
	{
		for (index = 0; index < 0x8000; index++)
			state.pack_index[index] = NONE;
		build_tags(&state, 0);
	}
	if (!state.failed)
		build_channels(&state);
	result->progress = 50;

	/* the pixels and samples, made as they are written to a file of their
	own, then the pack: its header, its tags, that file */
	snprintf(temporary, sizeof(temporary), "%s.tmp", pack_path);
	snprintf(data_path, sizeof(data_path), "%s.data", pack_path);
	if (!state.failed)
	{
		data = fopen(data_path, "w+b");
		if (!data)
			build_fail(&state, "the pack cannot be written%s", NULL);
	}
	if (!state.failed)
		build_items(&state, data, &data_size, &data_crc);
	if (!state.failed)
	{
		build_items_place(&state);
		tags_crc = crc_bytes(0, state.blob, state.blob_size);
		file = fopen(temporary, "wb");
		header_write(pack_header, &state, tags_crc, data_size, data_crc, source_hash, result->map);
		if (!file || fwrite(pack_header, 1, sizeof(pack_header), file) != sizeof(pack_header) ||
			fwrite(state.blob, 1, state.blob_size, file) != state.blob_size || fflush(data) || fseek(data, 0, SEEK_SET))
		{
			build_fail(&state, "the pack cannot be written%s", NULL);
		}
		else
		{
			unsigned char *bounce = malloc(BOUNCE_SIZE);
			unsigned long done = 0;

			while (bounce && done < data_size && !state.failed)
			{
				unsigned long chunk = data_size - done > BOUNCE_SIZE ? BOUNCE_SIZE : data_size - done;

				if (fread(bounce, 1, chunk, data) != chunk || fwrite(bounce, 1, chunk, file) != chunk)
					build_fail(&state, "the pack cannot be written%s", NULL);
				done += chunk;
			}
			if (!bounce)
				build_fail(&state, "there is no memory to build the pack%s", NULL);
			else
				free(bounce);
		}
	}
	if (data)
	{
		fclose(data);
		remove(data_path);
	}
	if (file && fclose(file))
		build_fail(&state, "the pack cannot be written%s", NULL);
	if (!state.failed)
	{
		remove(pack_path);
		if (rename(temporary, pack_path))
			build_fail(&state, "the pack cannot be put in its place%s", NULL);
	}
	else if (file)
	{
		remove(temporary);
	}
	result->tag_count = state.pack_tag_count;
	result->tag_bytes = state.blob_size;
	result->data_bytes = data_size;
	result->file_bytes = SCENE_PACK_HEADER_SIZE + state.blob_size + data_size;
	result->buffer_count = state.buffer_count;
	result->source_bytes = 0;
	result->microseconds = vita_host_time_us() - started;
	result->progress = state.failed ? 0 : 100;
	scene_pack_ce_close(state.custom_edition);
	state.custom_edition = NULL;
	/* (its tags were the source's buffer, which closing it let go) */
	state.tags = NULL;
	build_state_free(&state, scratch);
	return !state.failed;
}

/* (a pack of a build of several, scene_pack_build_many) its build, its
pieces of the source's pixels and samples and how far they are written,
its file */
struct build_job
{
	struct build_state state;
	struct build_range *pieces;
	long piece_count;
	long piece_cursor;
	unsigned long piece_done;
	unsigned long data_size;
	unsigned long data_crc;
	unsigned long tags_crc;
	FILE *file;
	char temporary[512];
};

/* a build's memory let go, but for the source's tag data (another's too) */
static void build_state_release(struct build_state *state)
{
	unsigned char *tags = state->tags;

	state->tags = NULL;
	build_state_free(state, NULL);
	state->pack_index = NULL;
	state->pack_tags = NULL;
	state->blob = NULL;
	state->extents = NULL;
	state->buffers = NULL;
	state->items = NULL;
	state->buffer_fixups = NULL;
	state->ranges = NULL;
	state->range_fixups = NULL;
	state->reduced = NULL;
	state->tags = tags;
}

static int range_source_compare(void const *a, void const *b)
{
	struct build_range const *left = a;
	struct build_range const *right = b;

	return left->source < right->source ? -1 : left->source > right->source ? 1 : 0;
}

/* (count) packs from the map, once read: FALSE, *copy_changed TRUE, when
the map's copy read in its place changed meanwhile (built again from the
map itself) */
static boolean build_many_once(
	struct scene_pack_recipe const *const *recipes,
	short count,
	char const *map,
	char const *maps_directory,
	char const *const *pack_paths,
	void *scratch,
	unsigned long scratch_size,
	int volatile const *cancel,
	struct scene_pack_build_result *results,
	boolean try_copy,
	boolean *copy_changed)
{
	/* (malloc's: the game's free is its debug allocator's, which calloc's
	blocks are not) */
	struct build_job *jobs = malloc((size_t)count * sizeof(struct build_job));
	struct source_reader reader;
	unsigned char header[CACHE_FILE_HEADER_SIZE];
	unsigned long long started = vita_host_time_us();
	unsigned long long source_hash = 0;
	char path[512];
	unsigned char *tags = NULL;
	unsigned long tags_size = 0;
	struct build_range *unions = NULL;
	long union_count = 0;
	unsigned long long union_bytes = 0;
	boolean all = TRUE;
	char const *failure = NULL;
	short job_index;
	long index;

	*copy_changed = FALSE;
	memset(&reader, 0, sizeof(reader));
	if (jobs)
		memset(jobs, 0, (size_t)count * sizeof(struct build_job));
	for (job_index = 0; job_index < count; job_index++)
	{
		memset(&results[job_index], 0, sizeof(results[job_index]));
		snprintf(results[job_index].map, sizeof(results[job_index].map), "%s", map);
	}
	if (!jobs)
	{
		for (job_index = 0; job_index < count; job_index++)
			snprintf(results[job_index].why, sizeof(results[job_index].why), "there is no memory to build the pack");
		return FALSE;
	}
	for (job_index = 0; job_index < count; job_index++)
	{
		struct build_state *state = &jobs[job_index].state;
		unsigned long long hash = 0;

		state->recipe = recipes[job_index];
		state->result = &results[job_index];
		state->cancel = cancel;
		state->source_address = 0x803A6000UL;
		if (recipes[job_index]->custom_edition_map_count ||
			!scene_pack_source_identity(recipes[job_index], maps_directory, map, &hash))
		{
			build_fail(state, "its recipe does not name %s, or it is not in the maps folder", map);
		}
		else
		{
			source_hash = hash;
		}
	}
	map_path(path, sizeof(path), maps_directory, map);
	if (!source_open(&reader, path, map, header, cancel, try_copy))
		failure = "the map cannot be read";
	reader.progress = &results[0].progress;
	reader.progress_base = 0;

	/* the source's tag data (read to the end of it: it is the file's last),
	once, for all of them */
	if (!failure)
	{
		tags_size = read_u32(header + 20);
		tags = scratch && scratch_size >= tags_size ? scratch : malloc(tags_size);
		reader.phase_total = tags_size;
		if (!tags)
			failure = "there is no memory to build the pack";
		else if (!source_read(&reader, read_u32(header + 16), tags_size, tags))
			failure = cancel && *cancel ? "it was stopped" : "the map's tag data cannot be read";
		else if (!source_copy_unchanged(&reader))
			*copy_changed = TRUE, failure = "the map's copy changed as it was read";
	}
	for (job_index = 0; job_index < count; job_index++)
	{
		struct build_job *job = &jobs[job_index];
		struct build_state *state = &job->state;

		if (failure)
			build_fail(state, "%s", failure);
		state->tags = tags;
		state->tags_size = tags_size;
		if (state->failed)
			continue;
		/* the pack's tags, then its file begun: a header to come, its tags */
		state->pack_index = malloc(sizeof(long) * 0x8000);
		state->pack_tags = malloc(sizeof(struct build_tag) * SCENE_PACK_MAXIMUM_TAGS);
		if (!state->pack_index || !state->pack_tags)
			build_fail(state, "there is no memory to build the pack%s", NULL);
		else if (!source_table_read(state))
			build_fail(state, "the map's tag header is damaged%s", NULL);
		if (!state->failed)
		{
			for (index = 0; index < 0x8000; index++)
				state->pack_index[index] = NONE;
			build_tags(state, reader.file_length);
		}
		if (!state->failed)
			job->pieces = ranges_place(state, &job->piece_count, &job->data_size);
		if (!state->failed)
		{
			unsigned char placeholder[SCENE_PACK_HEADER_SIZE];

			for (index = 0; index < state->range_fixup_count; index++)
			{
				struct build_range_fixup const *fixup = &state->range_fixups[index];

				write_u32(state->blob + fixup->location,
					SCENE_PACK_FILE_BASE + state->ranges[fixup->range].offset + (unsigned long)fixup->delta);
			}
			job->tags_crc = crc_bytes(0, state->blob, state->blob_size);
			snprintf(job->temporary, sizeof(job->temporary), "%s.tmp", pack_paths[job_index]);
			job->file = fopen(job->temporary, "wb");
			memset(placeholder, 0, sizeof(placeholder));
			if (!job->file || fwrite(placeholder, 1, sizeof(placeholder), job->file) != sizeof(placeholder) ||
				fwrite(state->blob, 1, state->blob_size, job->file) != state->blob_size)
			{
				build_fail(state, "the pack cannot be written%s", NULL);
			}
			else
			{
				io_done(state->blob_size);
			}
		}
		/* (its working memory let go before the next one's) */
		build_state_release(state);
	}

	/* the pixels and samples, every pack's, in the source's order (the
	second read, once): the union of all their pieces read, each piece's
	bytes written to its pack as they go by */
	reader.progress_base = 50;
	reader.phase_read = 0;
	for (job_index = 0; job_index < count; job_index++)
	{
		if (!jobs[job_index].state.failed)
			union_count += jobs[job_index].piece_count;
	}
	if (union_count)
	{
		long piece_total = union_count;

		unions = malloc(sizeof(*unions) * (size_t)piece_total);
		union_count = 0;
		if (!unions)
			failure = "there is no memory for the pack's pixels and samples";
		for (job_index = 0; unions && job_index < count; job_index++)
		{
			if (jobs[job_index].state.failed)
				continue;
			memcpy(unions + union_count, jobs[job_index].pieces, sizeof(*unions) * (size_t)jobs[job_index].piece_count);
			union_count += jobs[job_index].piece_count;
		}
		if (unions)
		{
			long merged = 0;

			qsort(unions, (size_t)union_count, sizeof(*unions), range_source_compare);
			for (index = 0; index < union_count; index++)
			{
				if (merged && unions[index].source <= unions[merged - 1].source + unions[merged - 1].size)
				{
					unsigned long end = unions[index].source + unions[index].size;

					if (end > unions[merged - 1].source + unions[merged - 1].size)
						unions[merged - 1].size = end - unions[merged - 1].source;
				}
				else
				{
					unions[merged++] = unions[index];
				}
			}
			union_count = merged;
			for (index = 0; index < union_count; index++)
				union_bytes += unions[index].size;
			reader.phase_total = union_bytes;
			if (!source_rewind(&reader))
				failure = "the map cannot be read again";
		}
	}
	for (index = 0; index < union_count && !failure; index++)
	{
		unsigned long position = unions[index].source;
		unsigned long end = unions[index].source + unions[index].size;

		while (position < end && !failure)
		{
			unsigned long chunk = end - position > BOUNCE_SIZE ? BOUNCE_SIZE : end - position;

			if (!source_read(&reader, position, chunk, reader.bounce))
			{
				failure = cancel && *cancel ? "it was stopped" : "the map's pixels and samples cannot be read";
				break;
			}
			for (job_index = 0; job_index < count; job_index++)
			{
				struct build_job *job = &jobs[job_index];

				if (job->state.failed)
					continue;
				results[job_index].progress = results[0].progress;
				while (job->piece_cursor < job->piece_count)
				{
					struct build_range const *piece = &job->pieces[job->piece_cursor];
					unsigned long from = piece->source + job->piece_done;
					unsigned long to = MIN(piece->source + piece->size, position + chunk);

					if (from >= position + chunk)
						break;
					if (from < position || fwrite(reader.bounce + (from - position), 1, to - from, job->file) != to - from)
					{
						build_fail(&job->state, "the pack cannot be written%s", NULL);
						break;
					}
					io_done(to - from);
					job->data_crc = crc_bytes(job->data_crc, reader.bounce + (from - position), to - from);
					job->piece_done += to - from;
					if (job->piece_done >= piece->size)
					{
						job->piece_cursor++;
						job->piece_done = 0;
					}
				}
			}
			position += chunk;
		}
	}
	if (!failure && !source_copy_unchanged(&reader))
		*copy_changed = TRUE, failure = "the map's copy changed as it was read";

	/* each pack's header, then its file put in its place */
	for (job_index = 0; job_index < count; job_index++)
	{
		struct build_job *job = &jobs[job_index];
		struct build_state *state = &job->state;
		struct scene_pack_build_result *result = &results[job_index];

		if (failure)
			build_fail(state, "%s", failure);
		if (!state->failed && job->piece_cursor != job->piece_count)
			build_fail(state, "the map's pixels and samples cannot be read%s", NULL);
		if (!state->failed)
		{
			unsigned char pack_header[SCENE_PACK_HEADER_SIZE];

			header_write(pack_header, state, job->tags_crc, job->data_size, job->data_crc, source_hash, map);
			if (fseek(job->file, 0, SEEK_SET) || fwrite(pack_header, 1, sizeof(pack_header), job->file) != sizeof(pack_header) ||
				fflush(job->file))
			{
				build_fail(state, "the pack cannot be written%s", NULL);
			}
		}
		if (job->file && fclose(job->file))
			build_fail(state, "the pack cannot be written%s", NULL);
		if (!state->failed)
		{
			remove(pack_paths[job_index]);
			if (rename(job->temporary, pack_paths[job_index]))
				build_fail(state, "the pack cannot be put in its place%s", NULL);
		}
		else if (job->file)
		{
			remove(job->temporary);
		}
		result->tag_count = state->pack_tag_count;
		result->tag_bytes = state->blob_size;
		result->data_bytes = job->data_size;
		result->file_bytes = SCENE_PACK_HEADER_SIZE + state->blob_size + job->data_size;
		result->source_bytes = reader.read_bytes;
		result->microseconds = vita_host_time_us() - started;
		result->progress = state->failed ? 0 : 100;
		if (state->failed)
			all = FALSE;
		if (job->pieces)
			free(job->pieces);
	}
	if (unions)
		free(unions);
	if (tags && tags != scratch)
		free(tags);
	source_close(&reader);
	free(jobs);
	return all;
}

boolean scene_pack_build_many(
	struct scene_pack_recipe const *const *recipes,
	short count,
	char const *map,
	char const *maps_directory,
	char const *const *pack_paths,
	void *scratch,
	unsigned long scratch_size,
	int volatile const *cancel,
	struct scene_pack_build_result *results)
{
	boolean copy_changed = FALSE;
	boolean built;

	if (count <= 0 || !map)
		return FALSE;
	built = build_many_once(recipes, count, map, maps_directory, pack_paths, scratch, scratch_size, cancel, results,
		TRUE, &copy_changed);
	/* (the map's copy changed under the build - the game began another into
	its place: once more, from the map itself) */
	if (copy_changed && !(cancel && *cancel))
	{
		platform_log("scene packs: %s's copy changed as it was read: built from the map itself", map);
		built = build_many_once(recipes, count, map, maps_directory, pack_paths, scratch, scratch_size, cancel, results,
			FALSE, &copy_changed);
	}
	return built;
}

boolean scene_pack_build(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char const *pack_path,
	void *scratch,
	unsigned long scratch_size,
	int volatile const *cancel,
	struct scene_pack_build_result *result)
{
	char map[SCENE_PACK_NAME_SIZE];

	if (recipe->custom_edition_map_count)
		return scene_pack_build_custom_edition(recipe, maps_directory, pack_path, scratch, scratch_size, cancel, result);
	if (!scene_pack_source_find(recipe, maps_directory, map, sizeof(map), NULL))
	{
		memset(result, 0, sizeof(*result));
		snprintf(result->why, sizeof(result->why), "none of its maps (%s...) is in the maps folder", recipe->maps[0]);
		return FALSE;
	}
	return scene_pack_build_many(&recipe, 1, map, maps_directory, &pack_path, scratch, scratch_size, cancel, result);
}

/* ---------- a pack's checks */

boolean scene_pack_open(
	char const *path,
	struct scene_pack_info *info,
	char *why,
	long why_size)
{
	FILE *file;
	boolean opened;

	memset(info, 0, sizeof(*info));
	file = fopen(path, "rb");
	if (!file)
	{
		why_set(why, why_size, "there is no pack%s", NULL);
		return FALSE;
	}
	opened = scene_pack_open_file(file, info, why, why_size);
	fclose(file);
	return opened;
}

boolean scene_pack_open_file(
	FILE *file,
	struct scene_pack_info *info,
	char *why,
	long why_size)
{
	unsigned char header[SCENE_PACK_HEADER_SIZE];
	unsigned char check[SCENE_PACK_HEADER_SIZE];
	long size;
	short index;

	memset(info, 0, sizeof(*info));
	size = file ? file_size_get(file) : NONE;
	if (size < SCENE_PACK_HEADER_SIZE || fread(header, 1, sizeof(header), file) != sizeof(header))
	{
		why_set(why, why_size, "it is shorter than a pack's header%s", NULL);
		return FALSE;
	}
	io_done(sizeof(header));
	memcpy(check, header, sizeof(check));
	write_u32(check + HEADER_HEADER_CRC, 0);
	if (memcmp(header, scene_pack_signature, sizeof(scene_pack_signature)) ||
		read_u32(header + HEADER_VERSION) != SCENE_PACK_VERSION ||
		read_u32(header + HEADER_HEADER_SIZE) != SCENE_PACK_HEADER_SIZE ||
		crc_bytes(0, check, sizeof(check)) != read_u32(header + HEADER_HEADER_CRC))
	{
		why_set(why, why_size, "it is not a pack of this version, or its header is damaged%s", NULL);
		return FALSE;
	}
	info->file_size = read_u32(header + HEADER_FILE_SIZE);
	info->tags_offset = read_u32(header + HEADER_TAGS_OFFSET);
	info->tags_size = read_u32(header + HEADER_TAGS_SIZE);
	info->data_offset = read_u32(header + HEADER_DATA_OFFSET);
	info->data_size = read_u32(header + HEADER_DATA_SIZE);
	info->tag_count = (long)read_u32(header + HEADER_TAG_COUNT);
	info->root_count = (long)read_u32(header + HEADER_ROOT_COUNT);
	info->tags_crc = read_u32(header + HEADER_TAGS_CRC);
	info->data_crc = read_u32(header + HEADER_DATA_CRC);
	info->builder_version = read_u32(header + HEADER_BUILDER_VERSION);
	info->recipe_hash = read_u64(header + HEADER_RECIPE_HASH);
	info->source_hash = read_u64(header + HEADER_SOURCE_HASH);
	memcpy(info->source_map, header + HEADER_SOURCE_MAP, SCENE_PACK_NAME_SIZE);
	for (index = 0; index < SCENE_PACK_NAME_SIZE && info->source_map[index]; index++)
		;
	/* (the name's bytes after it, and the header's unused ones, are none) */
	{
		short unused;

		for (unused = HEADER_SOURCE_MAP + index; unused < HEADER_HEADER_CRC; unused++)
		{
			if (header[unused])
				index = SCENE_PACK_NAME_SIZE;
		}
	}
	/* (every section where the header says, nothing past them, nothing
	larger than a pack may hold) */
	if (info->file_size != (unsigned long)size ||
		info->tags_offset != SCENE_PACK_HEADER_SIZE ||
		info->tags_size < TAG_HEADER_SIZE || info->tags_size > SCENE_PACK_MAXIMUM_TAG_BYTES || (info->tags_size & 3) ||
		info->data_offset != info->tags_offset + info->tags_size ||
		info->data_size > SCENE_PACK_MAXIMUM_DATA_BYTES ||
		info->data_size != info->file_size - info->data_offset ||
		info->tag_count < 1 || info->tag_count > SCENE_PACK_MAXIMUM_TAGS ||
		(unsigned long)info->tag_count > info->tags_size / TAG_INSTANCE_SIZE ||
		info->root_count < 0 || info->root_count > SCENE_PACK_MAXIMUM_ROOTS || info->root_count > info->tag_count ||
		index >= SCENE_PACK_NAME_SIZE || !name_valid(info->source_map))
	{
		memset(info, 0, sizeof(*info));
		why_set(why, why_size, "its header says sizes a pack cannot have%s", NULL);
		return FALSE;
	}
	return TRUE;
}

boolean scene_pack_load(
	char const *path,
	struct scene_pack_info const *info,
	void *destination,
	char *why,
	long why_size)
{
	FILE *file = fopen(path, "rb");
	boolean loaded;

	if (!file)
	{
		why_set(why, why_size, "it cannot be read%s", NULL);
		return FALSE;
	}
	loaded = scene_pack_load_file(file, info, destination, why, why_size);
	fclose(file);
	return loaded;
}

boolean scene_pack_load_file(
	FILE *file,
	struct scene_pack_info const *info,
	void *destination,
	char *why,
	long why_size)
{
	return scene_pack_load_file_checked(file, info, destination, FALSE, why, why_size);
}

boolean scene_pack_data_check(
	FILE *file,
	struct scene_pack_info const *info,
	char *why,
	long why_size)
{
	unsigned char *bounce = malloc(BOUNCE_SIZE);
	unsigned long crc = 0, done = 0;
	boolean read = file && bounce && !fseek(file, (long)info->data_offset, SEEK_SET);

	while (read && done < info->data_size)
	{
		unsigned long chunk = info->data_size - done > BOUNCE_SIZE ? BOUNCE_SIZE : info->data_size - done;

		read = fread(bounce, 1, chunk, file) == chunk;
		crc = crc_bytes(crc, bounce, chunk);
		done += chunk;
		io_done(chunk);
	}
	if (bounce)
		free(bounce);
	if (!read)
	{
		why_set(why, why_size, "it cannot be read%s", NULL);
		return FALSE;
	}
	if (crc != info->data_crc)
	{
		why_set(why, why_size, "it is damaged (its CRC)%s", NULL);
		return FALSE;
	}
	return TRUE;
}

boolean scene_pack_load_file_checked(
	FILE *file,
	struct scene_pack_info const *info,
	void *destination,
	boolean data_checked,
	char *why,
	long why_size)
{
	unsigned char *bounce;
	unsigned long crc = 0, done = 0;
	unsigned long anomalies;
	long corrections = 0;
	boolean read;
	char name[SCENE_PACK_NAME_SIZE + 8];

	if (!destination || ((unsigned long)destination & 3))
	{
		why_set(why, why_size, "there is no room for it%s", NULL);
		return FALSE;
	}
	bounce = malloc(BOUNCE_SIZE);
	read = file && bounce && !fseek(file, (long)info->tags_offset, SEEK_SET);
	/* (the tags read once, into their place, and checked there: what is
	checked is what is used) */
	while (read && done < info->tags_size)
	{
		unsigned long chunk = info->tags_size - done > 4 * BOUNCE_SIZE ? 4 * BOUNCE_SIZE : info->tags_size - done;

		read = fread((unsigned char *)destination + done, 1, chunk, file) == chunk;
		done += chunk;
		io_done(chunk);
	}
	/* (the data's CRC: unless the caller knows it good - the same file,
	unchanged since its data was checked, scene_packs.c) */
	done = 0;
	while (read && !data_checked && done < info->data_size)
	{
		unsigned long chunk = info->data_size - done > BOUNCE_SIZE ? BOUNCE_SIZE : info->data_size - done;

		read = fread(bounce, 1, chunk, file) == chunk;
		crc = crc_bytes(crc, bounce, chunk);
		done += chunk;
		io_done(chunk);
	}
	if (data_checked)
		crc = info->data_crc;
	if (bounce)
		free(bounce);
	if (!read)
	{
		why_set(why, why_size, "it cannot be read%s", NULL);
		return FALSE;
	}
	if (crc != info->data_crc || crc_bytes(0, destination, info->tags_size) != info->tags_crc)
	{
		why_set(why, why_size, "it is damaged (its CRC)%s", NULL);
		return FALSE;
	}
	if (read_u32((unsigned char *)destination + 12) != (unsigned long)info->tag_count)
	{
		why_set(why, why_size, "its tags are not as many as its header says%s", NULL);
		return FALSE;
	}
	anomalies = halo_tag_relocate_pack(destination, info->tags_size, SCENE_PACK_LINK_BASE);
	if (anomalies)
	{
		why_set(why, why_size, anomalies == (unsigned long)-1 ? "there is no memory to check it%s" :
			"its tags point outside it%s", NULL);
		return FALSE;
	}
	snprintf(name, sizeof(name), "pack %s", info->source_map);
	if (!tag_validate_pack_tags(destination, (long)info->tags_size, SCENE_PACK_FILE_BASE, info->data_size, name,
		&corrections) || corrections)
	{
		if (why && why_size > 0)
			snprintf(why, why_size, corrections ? "its tags needed %ld corrections" : "its tags failed the tag check",
				corrections);
		return FALSE;
	}
	return TRUE;
}

/* ---------- a loaded pack */

static unsigned char *loaded_resolve(void *context, unsigned long address, unsigned long size)
{
	(void)context;
	(void)size;
	return address ? (unsigned char *)address : NULL;
}

struct rebase_walk
{
	/* (its tags' first index now: 0 for a pack not given indices yet) */
	long old_first_index;
	long count;
	long first_index;
	long first_salt;
	long file_delta;
};

static unsigned long rebased(struct rebase_walk const *rebase, unsigned long tag_index)
{
	long local = (long)(short)tag_index - rebase->old_first_index;

	if (tag_index == (unsigned long)NONE || local < 0 || local >= rebase->count)
		return (unsigned long)NONE;
	return (unsigned long)((((rebase->first_salt + local) & 0x7FFF) << 16) | (rebase->first_index + local));
}

static void rebase_visit(void *context, struct tag_schema_field const *field, unsigned long address)
{
	struct rebase_walk const *rebase = context;
	unsigned char *value;

	/* (a file's data, a sound's samples, at the pack's own file offsets) */
	if (field->type == _tag_schema_file_data)
	{
		struct tag_data *data = (struct tag_data *)address;

		if (data->size > 0)
			data->file_offset += rebase->file_delta;
		return;
	}
	/* (references are the layouts' walk's, below: each once) */
	if (field->type != _tag_schema_tag_index)
		return;
	value = (unsigned char *)address;
	write_u32(value, rebased(rebase, read_u32(value)));
}

/* every reference the layouts know (the walk tag_relocate.c checked them
by) made the game's */
static void layout_rebase(struct rebase_walk const *rebase, unsigned short layout, unsigned char *address, short depth)
{
	long count = halo_tag_layout_field_count(layout);
	long index;

	if (depth > MAXIMUM_WALK_DEPTH)
		return;
	for (index = 0; index < count; index++)
	{
		struct halo_tag_layout_field field;
		unsigned char *at;

		if (!halo_tag_layout_field(layout, index, &field))
			continue;
		at = address + field.offset;
		switch (field.kind)
		{
		case HALO_TAG_LAYOUT_FIELD_BLOCK:
		{
			long elements = (long)read_u32(at);
			unsigned char *first = (unsigned char *)read_u32(at + 4);
			long element;

			if (field.child == HALO_TAG_LAYOUT_NONE || elements <= 0 || !first)
				break;
			for (element = 0; element < elements; element++)
				layout_rebase(rebase, field.child, first + (unsigned long)element * field.child_size, depth + 1);
			break;
		}
		case HALO_TAG_LAYOUT_FIELD_REFERENCE:
			write_u32(at + 12, rebased(rebase, read_u32(at + 12)));
			break;
		case HALO_TAG_LAYOUT_FIELD_STRUCT:
			if (field.child != HALO_TAG_LAYOUT_NONE)
				layout_rebase(rebase, field.child, at, depth + 1);
			break;
		}
	}
}

void scene_pack_rebase(
	void *tags,
	long first_index,
	long first_salt,
	long file_delta)
{
	scene_pack_rebase_again(tags, 0, first_index, first_salt, file_delta);
}

void scene_pack_rebase_again(
	void *tags,
	long old_first_index,
	long first_index,
	long first_salt,
	long file_delta)
{
	long count;
	unsigned char *instances = scene_pack_instances(tags, &count);
	struct rebase_walk rebase;
	long index;

	rebase.old_first_index = old_first_index;
	rebase.count = count;
	rebase.first_index = first_index;
	rebase.first_salt = first_salt;
	rebase.file_delta = file_delta;
	for (index = 0; index < count; index++)
	{
		unsigned char *instance = instances + index * TAG_INSTANCE_SIZE;
		unsigned long group_tag = read_u32(instance);
		struct tag_schema_group const *group = schema_group_get(group_tag);
		unsigned long base = read_u32(instance + 20);
		struct schema_walk walk;

		memset(&walk, 0, sizeof(walk));
		walk.context = &rebase;
		walk.resolve = loaded_resolve;
		walk.visit = rebase_visit;
		if (group && group->definition && base)
			schema_walk_element(&walk, group->definition, base, 0);
		if (base && halo_tag_layout_root(group_tag) < HALO_TAG_LAYOUT_UNKNOWN)
			layout_rebase(&rebase, halo_tag_layout_root(group_tag), (unsigned char *)base, 0);
		/* (a bitmap's pixels at the pack's own file offsets) */
		if (group_tag == BITMAP_GROUP_TAG && base)
		{
			struct bitmap_group *bitmaps = (struct bitmap_group *)base;
			long bitmap_index;

			for (bitmap_index = 0; bitmap_index < bitmaps->bitmaps.count; bitmap_index++)
				((struct bitmap_data *)bitmaps->bitmaps.address + bitmap_index)->pixels_offset += file_delta;
		}
		/* (a sound's permutations name the sound: sound_check) */
		if (group_tag == 'snd!' && base)
		{
			struct sound_definition *definition = (struct sound_definition *)base;
			unsigned long own = rebased(&rebase, read_u32(instance + 12));
			long range_index;

			for (range_index = 0; range_index < definition->pitch_ranges.count; range_index++)
			{
				struct sound_pitch_range *range = (struct sound_pitch_range *)definition->pitch_ranges.address + range_index;
				long permutation;

				for (permutation = 0; permutation < range->permutations.count; permutation++)
				{
					struct sound_permutation *sound = (struct sound_permutation *)range->permutations.address + permutation;

					sound->unknown2 = own;
					sound->unknown3 = own;
				}
			}
		}
	}
	for (index = 0; index < count; index++)
	{
		unsigned char *instance = instances + index * TAG_INSTANCE_SIZE;

		write_u32(instance + 12, rebased(&rebase, read_u32(instance + 12)));
	}
}

void *scene_pack_instances(
	void *tags,
	long *count)
{
	unsigned char *header = tags;

	*count = (long)read_u32(header + 12);
	return (void *)read_u32(header);
}

long scene_pack_find(
	void *tags,
	unsigned long group_tag,
	char const *path)
{
	long count;
	unsigned char *instances = scene_pack_instances(tags, &count);
	long index;

	for (index = 0; index < count; index++)
	{
		unsigned char *instance = instances + index * TAG_INSTANCE_SIZE;
		char const *name = (char const *)read_u32(instance + 16);

		if (read_u32(instance) == group_tag && name && names_match(name, path))
			return index;
	}
	return NONE;
}
