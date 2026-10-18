/*
SCENE_PACK_CE.C

Scene packs built from Halo Custom Edition maps (scene_pack_ce.h, which says
what it does; the pack itself and the builder: scene_pack.c).

A Custom Edition map is anyone's file from the internet, as are the
resource maps beside it: it is read only through the checks of the game's
own Custom Edition loader, in the order the game takes them
(custom_edition_cache.c, custom_edition_cache_tags_load_private), on the
builder's thread rather than the game's:
- its identity (cache_file_identify: every header field against the file)
  and the recipe's (its size and header checksum);
- its tags and the resource maps' (custom_edition_cache_load_tags: the tag
  index, every instance, name and address, the scenario's structure BSPs'
  references, every bitmap's pixels and sound's samples in their file,
  every model part's geometry in the model data), into the tag cache's free
  room or the heap, never more than there is;
- their bytes made this build's (custom_edition_cache_combine_resource_
  offsets, custom_edition_cache_convert), moved where they are
  (halo_tag_relocate_linked_tags_isolated) and checked against their
  groups' schemas (tag_validate_custom_edition_tags_isolated), the game's
  own walk and check states kept as they were;
then, as the builder takes each tag of the pack, what Custom Edition holds
otherwise is made what an Xbox map holds, with the game's own checks again
(custom_edition_geometry.c's of each model part, custom_edition_bitmaps.c's
of each bitmap), and the pack the builder writes is checked as any pack is
when it is loaded (scene_pack_load): its tags moved and checked with no
correction allowed.

What Custom Edition holds otherwise, and what the pack holds instead:
- A gbxmodel's parts (0x84 bytes) hold no vertices: their uncompressed
  vertices and strips lie in the map's model data, and a model whose parts
  have local nodes names its nodes through each part's table. The parts are
  repacked as this build's (0x68 bytes) where they are, their vertices'
  nodes made the model's, their vertices compressed as the game compresses
  them (rasterizer_geometry_compress_vertices, here without the game) into
  buffers of the pack, their strips copied: the model becomes a 'mode' tag.
  A model of more nodes than the renderer skins at once (44 or more: the
  game's Custom Edition maps draw such a model a part's nodes at a time,
  which a pack's models are not) is refused.
- A bitmap's pixels are laid out as the tag stores them (levels one after
  another, a cube map's six faces in each, unswizzled): they are laid out
  as the Xbox's (rasterizer_xbox_bitmap_rebuild_hardware_format, here
  without the game: every level swizzled, cube maps face by face, linear
  rows padded to 64 bytes, the whole padded to 128). A large 2D texture is
  written from its second level, as the game draws a Custom Edition map's
  (custom_edition_bitmaps_reduce, 128 KB). A model shader's multipurpose
  map and a HUD meter that Halo PC keeps in other channels than this build
  reads them from (custom_edition_bitmaps.c) have their channels put where
  this build reads them: decompressed (DXT) to 32-bit colour first, since
  the channels move between DXT's colour and alpha.
- A sound permutation may be Ogg Vorbis, 16-bit PCM, or (a 44 kHz mono
  sound) taken at half its rate: it is made Xbox ADPCM, as the game's sound
  cache makes it as it plays a Custom Edition map's (ogg_sound.c,
  xbox_adpcm_encoder.c), at most 1 MB of it (a longer one is cut off: the
  game splits such sounds as the map loads, which a pack does not).
*/

/* ---------- headers */

#include "cseries.h"
#include "tag_files/tag_groups.h"
#include "bitmaps/bitmap_group.h"
#include "sound/sound_definitions.h"
#include "shaders/shader_definitions.h"
#include "interface/unit_hud_interface_definition.h"

#include "cache_file_formats.h"
#include "scene_pack.h"
#include "scene_pack_ce.h"
#include "tag_schema.h"
#include "tag_relocate.h"
#include "../src/ogg_sound.h"
#include "../src/xbox_adpcm_encoder.h"
#include "../../third_party/zlib/zlib_prefixed.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	/* the combined offset space the converted tags' pixels and samples
	count in (custom_edition_cache.c): the map, bitmaps.map, sounds.map */
	COMBINED_BITMAPS_OFFSET = 0x40000000,
	COMBINED_SOUNDS_OFFSET = 0x60000000,

	/* the room the tags' load needs past the tag data: the resource maps'
	tags (half a megabyte in the maps examined), the check's bits for each
	byte (an eighth) and its working memory */
	TAGS_ROOM_EXTRA_BYTES = 4 * 1024 * 1024,

	/* a gbxmodel's (custom_edition_geometry.c) and this build's model
	parts */
	GBXMODEL_PART_SIZE = 0x84,
	MODEL_PART_SIZE = 0x68,
	MODEL_FLAGS_LOCAL_NODES_BIT = 1,
	MODEL_PART_FLAGS_LOCAL_NODES_BIT = 1,
	MODEL_NODES_OFFSET = 0xB8,
	MODEL_GEOMETRIES_OFFSET = 0xD0,
	MODEL_SHADERS_OFFSET = 0xDC,
	MODEL_ROOT_SIZE = 0xE8,
	MODEL_GEOMETRY_SIZE = 0x30,
	MODEL_GEOMETRY_PARTS_OFFSET = 0x24,
	MAXIMUM_PART_LOCAL_NODES = 22,
	/* the most nodes the renderer skins at once (rasterizer.h's
	RASTERIZER_MAXIMUM_NODES_PER_MODEL, less one) */
	MAXIMUM_SKINNED_NODES = 43,
	UNCOMPRESSED_MODEL_VERTEX_SIZE = 68,
	COMPRESSED_MODEL_VERTEX_SIZE = 32,
	RASTERIZER_VERTEX_TYPE_MODEL_COMPRESSED = 5,
	TRIANGLE_BUFFER_TYPE_STRIP = 1,
	MAXIMUM_PART_VERTICES = 0xFFFF,

	/* bitmaps (bitmaps.c) */
	BITMAP_TYPE_2D = 0,
	BITMAP_TYPE_3D = 1,
	BITMAP_TYPE_CUBE_MAP = 2,
	BITMAP_FORMAT_A8R8G8B8 = 11,
	BITMAP_FORMAT_DXT1 = 14,
	BITMAP_FORMAT_DXT3 = 15,
	BITMAP_FORMAT_DXT5 = 16,
	BITMAP_FORMAT_P8_BUMP = 17,
	NUMBER_OF_BITMAP_FORMATS = 18,
	BITMAP_FLAG_POWER_OF_TWO = 1 << 0,
	BITMAP_FLAG_COMPRESSED = 1 << 1,
	BITMAP_FLAG_PALETTIZED = 1 << 2,
	BITMAP_FLAG_SWIZZLED = 1 << 3,
	BITMAP_FLAG_LINEAR = 1 << 4,
	/* (the texture cache loads only a bitmap whose flags say it is cached,
	as a cache file's tool sets: xbox_texture_cache.c) */
	BITMAP_FLAG_CACHED = 1 << 7,
	BITMAP_GROUP_TYPE_2D_TEXTURES = 0,
	MAXIMUM_BITMAP_DIMENSION = 4096,
	MAXIMUM_BITMAP_BYTES = 24 * 1024 * 1024,
	REDUCED_TEXTURE_LEVEL_BYTES = 128 * 1024,

	/* sounds (cache_file_formats.c's, sound_definitions.h) */
	SOUND_COMPRESSION_NONE = 0,
	SOUND_COMPRESSION_XBOX_ADPCM = 1,
	SOUND_COMPRESSION_OGG_VORBIS = 3,
	SOUND_ENCODING_STEREO = 1,
	SOUND_SAMPLE_RATE_44KHZ = 1,
	PCM_READ_BYTES = 16 * 1024,

	/* model shaders' flag of MCC's tools, and HUD meters' (custom_edition_
	bitmaps.c) */
	SHADER_MODEL_XBOX_MULTIPURPOSE_BIT = 6,
	HUD_METER_USE_XBOX_SHADING_BIT = 5,

	/* channel orders (cache_file_formats.h's enum custom_edition_channel_order) */
	CHANNELS_UNKNOWN = 0xFF,
	CHANNELS_XBOX = 0,
	CHANNELS_MULTIPURPOSE = 1,
	CHANNELS_HUD_METER = 2,
	/* (drawn in one order and otherwise too: kept as they are) */
	CHANNELS_KEPT = 0xFE,
};

/* ---------- structures */

struct ce_file
{
	FILE *file;
	unsigned long size;
	struct cache_file_source source;
	/* (the map's own: scene_pack_ce_read_hook sees its reads) */
	boolean hooked;
};

/* a model part made this build's (scene_pack_ce_model_convert): where it
is, and what scene_pack_ce_part_buffers reads of the model data for it */
struct ce_part
{
	unsigned long address;
	unsigned long vertex_offset;
	long vertex_count;
	unsigned long strip_offset;
	long strip_index_count;
	short model_node_count;
	byte local_node_count;
	byte local_nodes[MAXIMUM_PART_LOCAL_NODES];
};

struct scene_pack_ce
{
	struct ce_file map;
	struct ce_file resources[NUMBER_OF_RESOURCE_MAP_TYPES];
	char name[SCENE_PACK_NAME_SIZE];
	unsigned char *tags;
	unsigned long tags_room;
	boolean tags_owned;
	unsigned long loaded_bytes;
	long tag_count;
	unsigned long model_data_offset;
	unsigned long model_index_data_offset;
	unsigned long model_data_bytes;
	struct ce_part *parts;
	long part_count;
	long part_capacity;
	/* each tag's channel order, by its index (scene_pack_ce_channels) */
	unsigned char *channels;
	/* names made for tags of none (protected maps') */
	char *names;
	unsigned long names_size;
	unsigned long names_capacity;
	/* the Ogg Vorbis decoder's working memory (made when first wanted), and
	where in it the decoder's 8-byte-aligned part starts */
	void *working;
	void *working_aligned;
	/* what was made (logged) */
	long bitmaps_reordered;
	long bitmaps_reduced;
	long permutations_transcoded;
	long permutations_truncated;
};

/* ---------- globals */

void (*scene_pack_ce_read_hook)(unsigned long offset, void *buffer, unsigned long size) = NULL;

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

static void write_u16(void *address, unsigned short value)
{
	unsigned char *bytes = address;

	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
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

static void why_set(char *why, long why_size, char const *format, char const *detail)
{
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, format, detail ? detail : "");
}

/* zeroed memory of the game's heap (cseries.h's malloc: never calloc,
which is the C library's) */
static void *zeroed(unsigned long size)
{
	void *memory = malloc(size ? size : 1);

	if (memory)
		memset(memory, 0, size);
	return memory;
}

/* whether name starts with prefix, case aside */
static boolean name_starts(char const *name, char const *prefix)
{
	for (; *prefix; name++, prefix++)
	{
		int a = *name >= 'A' && *name <= 'Z' ? *name - 'A' + 'a' : *name;
		int b = *prefix >= 'A' && *prefix <= 'Z' ? *prefix - 'A' + 'a' : *prefix;

		if (!*name || a != b)
			return FALSE;
	}
	return TRUE;
}

static boolean power_of_two(long value)
{
	return value > 0 && !(value & (value - 1));
}

static short ce_floor_log2(long value)
{
	short log = -1;

	while (value > 0)
	{
		value >>= 1;
		log++;
	}
	return log;
}

/* ---------- files */

static int ce_file_read(void *context, uint32_t offset, uint32_t size, void *buffer)
{
	struct ce_file *file = context;

	if (!file->file || offset > file->size || size > file->size - offset)
		return 0;
	if (size && (fseek(file->file, (long)offset, SEEK_SET) || fread(buffer, 1, size, file->file) != size))
		return 0;
	if (file->hooked && scene_pack_ce_read_hook && size)
		scene_pack_ce_read_hook(offset, buffer, size);
	return 1;
}

static boolean ce_file_open(struct ce_file *file, char const *path)
{
	long size;

	memset(file, 0, sizeof(*file));
	file->file = fopen(path, "rb");
	if (!file->file)
		return FALSE;
	if (fseek(file->file, 0, SEEK_END) || (size = ftell(file->file)) < 0 || fseek(file->file, 0, SEEK_SET))
	{
		fclose(file->file);
		file->file = NULL;
		return FALSE;
	}
	file->size = (unsigned long)size;
	file->source.context = file;
	file->source.read = ce_file_read;
	file->source.size = (uint32_t)file->size;
	return TRUE;
}

static void ce_file_close(struct ce_file *file)
{
	if (file->file)
		fclose(file->file);
	memset(file, 0, sizeof(*file));
}

static void map_path(char *path, long path_size, char const *maps_directory, char const *name)
{
	long length = (long)strlen(maps_directory);

	snprintf(path, (size_t)path_size, "%s%s%s.map", maps_directory,
		length && maps_directory[length - 1] != '/' && maps_directory[length - 1] != '\\' ? "/" : "", name);
}

static char const *const resource_map_names[NUMBER_OF_RESOURCE_MAP_TYPES] = { NULL, "bitmaps", "sounds", "loc" };

/* the bytes of the combined offset space (the map, bitmaps.map, sounds.map)
at offset */
static boolean ce_combined_read(struct scene_pack_ce *ce, unsigned long offset, unsigned long size, void *buffer)
{
	struct ce_file *file = &ce->map;

	if (offset >= COMBINED_SOUNDS_OFFSET)
	{
		file = &ce->resources[_resource_map_sounds];
		offset -= COMBINED_SOUNDS_OFFSET;
	}
	else if (offset >= COMBINED_BITMAPS_OFFSET)
	{
		file = &ce->resources[_resource_map_bitmaps];
		offset -= COMBINED_BITMAPS_OFFSET;
	}
	return ce_file_read(file, (uint32_t)offset, (uint32_t)size, buffer) != 0;
}

/* ---------- the source map */

char const *scene_pack_custom_edition_builder_version(
	void)
{
	return "ce1";
}

boolean scene_pack_ce_source_check(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char *map_name,
	long map_name_size,
	unsigned long long *source_hash,
	char *why,
	long why_size)
{
	short index;
	boolean other_version = FALSE;
	boolean not_custom_edition = FALSE;
	char missing[96];

	missing[0] = 0;
	for (index = 0; index < recipe->custom_edition_map_count; index++)
	{
		struct scene_pack_recipe_custom_edition_map const *wanted = &recipe->custom_edition_maps[index];
		struct cache_file_identity identity;
		struct ce_file file;
		unsigned char header[CACHE_FILE_HEADER_BYTES];
		char path[512];
		unsigned long long hash;
		uint32_t used;
		short type;

		map_path(path, sizeof(path), maps_directory, wanted->name);
		if (!ce_file_open(&file, path))
			continue;
		if (cache_file_identify(&file.source, &identity) != _cache_file_status_ok ||
			identity.format != _cache_file_format_custom_edition_cache)
		{
			not_custom_edition = TRUE;
			ce_file_close(&file);
			continue;
		}
		if (identity.file_size != wanted->file_size || identity.checksum != wanted->checksum ||
			!ce_file_read(&file, 0, CACHE_FILE_HEADER_BYTES, header))
		{
			other_version = TRUE;
			ce_file_close(&file);
			continue;
		}
		/* (the resource maps it takes tags from must be there) */
		used = custom_edition_cache_resource_maps_used(&file.source, &identity);
		hash = fnv_bytes(0xCBF29CE484222325ULL, header, sizeof(header));
		{
			unsigned char size_bytes[4];

			write_u32(size_bytes, file.size);
			hash = fnv_bytes(hash, size_bytes, 4);
		}
		ce_file_close(&file);
		for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
		{
			struct ce_file resource;
			unsigned char size_bytes[4];

			map_path(path, sizeof(path), maps_directory, resource_map_names[type]);
			if (!ce_file_open(&resource, path))
			{
				if (used & (1UL << type))
				{
					snprintf(missing + strlen(missing), sizeof(missing) - strlen(missing), "%s%s.map",
						missing[0] ? ", " : "", resource_map_names[type]);
				}
				continue;
			}
			write_u32(size_bytes, resource.size);
			hash = fnv_bytes(hash, size_bytes, 4);
			ce_file_close(&resource);
		}
		if (missing[0])
		{
			if (why && why_size > 0)
				snprintf(why, (size_t)why_size, "the maps folder has no %s (Halo Custom Edition's), which %s.map needs",
					missing, wanted->name);
			return FALSE;
		}
		if (map_name)
			snprintf(map_name, (size_t)map_name_size, "%s", wanted->name);
		if (source_hash)
			*source_hash = hash;
		return TRUE;
	}
	if (why && why_size > 0)
	{
		char const *name = recipe->custom_edition_map_count ? recipe->custom_edition_maps[0].name : "";

		if (other_version)
			snprintf(why, (size_t)why_size, "your %s.map is another version of it than the one it is built from", name);
		else if (not_custom_edition)
			snprintf(why, (size_t)why_size, "%s.map is not a Halo Custom Edition map", name);
		else
			snprintf(why, (size_t)why_size, "%s.map (Halo Custom Edition) is not in the maps folder", name);
	}
	return FALSE;
}

struct scene_pack_ce *scene_pack_ce_open(
	char const *maps_directory,
	char const *map_name,
	void *scratch,
	unsigned long scratch_size,
	int volatile const *cancel,
	long volatile *progress,
	char *why,
	long why_size)
{
	struct scene_pack_ce *ce = zeroed(sizeof(*ce));
	struct resource_map maps[NUMBER_OF_RESOURCE_MAP_TYPES];
	struct resource_map *map_pointers[NUMBER_OF_RESOURCE_MAP_TYPES];
	struct cache_file_identity identity;
	struct custom_edition_load_report *report = NULL;
	struct custom_edition_conversion_report *conversion = NULL;
	struct tag_validate_file_range ranges[3];
	enum cache_file_status status;
	char path[512];
	unsigned long room;
	unsigned long anomalies;
	long corrections = 0;
	short type;

	memset(maps, 0, sizeof(maps));
	memset(map_pointers, 0, sizeof(map_pointers));
	if (!ce)
	{
		why_set(why, why_size, "there is no memory to build the pack%s", NULL);
		return NULL;
	}
	snprintf(ce->name, sizeof(ce->name), "%s", map_name);
	map_path(path, sizeof(path), maps_directory, map_name);
	if (!ce_file_open(&ce->map, path))
	{
		snprintf(why, (size_t)why_size, "%s.map cannot be read", map_name);
		goto failed;
	}
	ce->map.hooked = TRUE;
	if (cache_file_identify(&ce->map.source, &identity) != _cache_file_status_ok ||
		identity.format != _cache_file_format_custom_edition_cache || identity.file_size > COMBINED_BITMAPS_OFFSET)
	{
		snprintf(why, (size_t)why_size, "%s.map is not a Custom Edition map this game loads", map_name);
		goto failed;
	}
	/* the resource maps, as the game opens them (no OpenSauce mod set:
	the maps folder's own) */
	for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
	{
		map_path(path, sizeof(path), maps_directory, resource_map_names[type]);
		if (!ce_file_open(&ce->resources[type], path))
			continue;
		if (resource_map_open(&ce->resources[type].source, (enum resource_map_type)type, &maps[type]) != _cache_file_status_ok)
		{
			snprintf(why, (size_t)why_size, "the maps folder's %s.map is damaged", resource_map_names[type]);
			goto failed;
		}
		map_pointers[type] = &maps[type];
	}
	if ((ce->resources[_resource_map_bitmaps].file &&
		ce->resources[_resource_map_bitmaps].size > COMBINED_SOUNDS_OFFSET - COMBINED_BITMAPS_OFFSET) ||
		(ce->resources[_resource_map_sounds].file && ce->resources[_resource_map_sounds].size > 0x20000000UL))
	{
		why_set(why, why_size, "a resource map in the maps folder is too large%s", NULL);
		goto failed;
	}
	if (cancel && *cancel)
		goto stopped;

	/* its tags, in the tag cache's free room when they fit there */
	room = identity.tag_data_size + TAGS_ROOM_EXTRA_BYTES;
	if (room > TAG_VALIDATE_MAXIMUM_TAG_CACHE_SIZE)
		room = TAG_VALIDATE_MAXIMUM_TAG_CACHE_SIZE;
	if (scratch && scratch_size >= room)
	{
		ce->tags = scratch;
		ce->tags_room = scratch_size > TAG_VALIDATE_MAXIMUM_TAG_CACHE_SIZE ? TAG_VALIDATE_MAXIMUM_TAG_CACHE_SIZE : scratch_size;
		ce->tags_room &= ~15UL;
	}
	else
	{
		ce->tags = malloc(room);
		ce->tags_room = room & ~15UL;
		ce->tags_owned = TRUE;
	}
	report = zeroed(sizeof(*report));
	conversion = zeroed(sizeof(*conversion));
	if (!ce->tags || !report || !conversion)
	{
		why_set(why, why_size, "there is no memory for the map's tags%s", NULL);
		goto failed;
	}
	status = custom_edition_cache_load_tags(&ce->map.source, map_pointers, ce->tags, (uint32_t)ce->tags_room, report);
	for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
	{
		if (map_pointers[type])
			resource_map_close(&maps[type]);
		map_pointers[type] = NULL;
	}
	if (progress)
		*progress = 15;
	if (status != _cache_file_status_ok)
	{
		platform_log("scene packs: %s.map cannot be loaded: %s (tag %ld, 0x%08lX)", map_name,
			cache_file_status_describe(status), (long)report->problem_tag_index, (unsigned long)report->problem_location);
		snprintf(why, (size_t)why_size, "%s.map is damaged or not supported (%s)", map_name,
			cache_file_status_describe(status));
		goto failed;
	}
	ce->loaded_bytes = report->tag_data_bytes + report->resource_tag_bytes;
	ce->tag_count = report->tag_count;
	ce->model_data_offset = report->model_data_offset;
	ce->model_index_data_offset = report->model_index_data_offset;
	ce->model_data_bytes = report->model_data_bytes;
	if (cancel && *cancel)
		goto stopped;

	/* made this build's, moved where they are, checked */
	custom_edition_cache_combine_resource_offsets(ce->tags, (uint32_t)ce->loaded_bytes, COMBINED_BITMAPS_OFFSET,
		COMBINED_SOUNDS_OFFSET);
	status = custom_edition_cache_convert(ce->tags, (uint32_t)ce->loaded_bytes, identity.name, conversion);
	if (status != _cache_file_status_ok)
	{
		snprintf(why, (size_t)why_size, "%s.map cannot be converted (%s)", map_name, cache_file_status_describe(status));
		goto failed;
	}
	anomalies = halo_tag_relocate_linked_tags_isolated(ce->tags, ce->loaded_bytes, CUSTOM_EDITION_TAG_CACHE_ADDRESS,
		ce->tags_room);
	if (anomalies == (unsigned long)-1)
	{
		why_set(why, why_size, "there is no memory to move the map's tags%s", NULL);
		goto failed;
	}
	ranges[0].offset = 0;
	ranges[0].size = ce->map.size;
	ranges[1].offset = COMBINED_BITMAPS_OFFSET;
	ranges[1].size = ce->resources[_resource_map_bitmaps].size;
	ranges[2].offset = COMBINED_SOUNDS_OFFSET;
	ranges[2].size = ce->resources[_resource_map_sounds].size;
	if (cancel && *cancel)
		goto stopped;
	if (!tag_validate_custom_edition_tags_isolated(ce->tags, (long)ce->loaded_bytes, ce->tags_room, ranges, 3, identity.name,
		&corrections))
	{
		snprintf(why, (size_t)why_size, "%s.map failed the tag check", map_name);
		goto failed;
	}
	platform_log("scene packs: %s.map: %ld tags loaded (%lu KB, %lu KB from the resource maps), %ld shaders renumbered, "
		"%ld bitmaps prepared, %ld Ogg Vorbis sounds; the walk found %lu inconsistencies, the check made %ld corrections",
		map_name, ce->tag_count, ce->loaded_bytes / 1024, (unsigned long)report->resource_tag_bytes / 1024,
		(long)conversion->shaders_retyped, (long)conversion->bitmaps_prepared, (long)conversion->sounds_ogg_vorbis,
		anomalies, corrections);
	if (progress)
		*progress = 30;
	ce->channels = malloc((unsigned long)ce->tag_count);
	if (!ce->channels)
	{
		why_set(why, why_size, "there is no memory to build the pack%s", NULL);
		goto failed;
	}
	memset(ce->channels, CHANNELS_UNKNOWN, (unsigned long)ce->tag_count);
	free(report);
	free(conversion);
	return ce;

stopped:
	why_set(why, why_size, "it was stopped%s", NULL);
failed:
	for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
	{
		if (map_pointers[type])
			resource_map_close(&maps[type]);
	}
	if (report)
		free(report);
	if (conversion)
		free(conversion);
	scene_pack_ce_close(ce);
	return NULL;
}

void scene_pack_ce_close(
	struct scene_pack_ce *ce)
{
	short type;

	if (!ce)
		return;
	ce_file_close(&ce->map);
	for (type = 0; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
		ce_file_close(&ce->resources[type]);
	if (ce->tags_owned && ce->tags)
		free(ce->tags);
	if (ce->parts)
		free(ce->parts);
	if (ce->channels)
		free(ce->channels);
	/* (the names' blocks, each naming the one before it) */
	while (ce->names)
	{
		char *block = ce->names;

		ce->names = *(char **)block;
		free(block);
	}
	if (ce->working)
		free(ce->working);
	free(ce);
}

unsigned char *scene_pack_ce_tags(
	struct scene_pack_ce *ce,
	unsigned long *size)
{
	*size = ce->loaded_bytes;
	return ce->tags;
}

/* the converted tags' bytes at a (moved) address, if they are loaded */
static unsigned char *ce_at(struct scene_pack_ce const *ce, unsigned long address, unsigned long size)
{
	unsigned long base = (unsigned long)ce->tags;
	unsigned long offset = address - base;

	if (address < base || offset > ce->loaded_bytes || size > ce->loaded_bytes - offset)
		return NULL;
	return ce->tags + offset;
}

/* a tag's instance, by index */
static unsigned char *ce_instance(struct scene_pack_ce const *ce, long index)
{
	unsigned char *header = ce->tags;

	if (index < 0 || index >= ce->tag_count)
		return NULL;
	return ce_at(ce, read_u32(header) + (unsigned long)index * 32, 32);
}

/* a tag handle's index, if it names one of the map's tags */
static long ce_tag(struct scene_pack_ce const *ce, unsigned long handle)
{
	long index = (long)(handle & 0xFFFF);
	unsigned char *instance;

	if (handle == (unsigned long)NONE || index >= ce->tag_count)
		return NONE;
	instance = ce_instance(ce, index);
	return instance && read_u32(instance + 12) == handle ? index : NONE;
}

char const *scene_pack_ce_tag_name(
	struct scene_pack_ce *ce,
	long source_index,
	char const *name)
{
	unsigned char *instance = ce_instance(ce, source_index);
	char made[64];
	char group[5];
	unsigned long length;
	short index;

	if (name && name[0] && strcmp(name, "<protected>"))
		return name;
	/* (protected\<map>\<group> <index>: a name of its own in the pack) */
	for (index = 0; index < 4; index++)
	{
		char character = instance ? (char)(read_u32(instance) >> (24 - 8 * index)) : '?';

		group[index] = character >= 'a' && character <= 'z' ? character : character >= '0' && character <= '9' ? character : '_';
	}
	group[4] = 0;
	snprintf(made, sizeof(made), "protected\\%s\\%s %ld", ce->name, group, source_index);
	length = (unsigned long)strlen(made) + 1;
	if (ce->names_size + length > ce->names_capacity)
	{
		/* (the names are pointed at as the pack is built: never moved, so
		a new block for each that does not fit, kept until the source closes) */
		unsigned long capacity = 64 * 1024;
		char *block = malloc(capacity + sizeof(char *));

		if (!block)
			return name ? name : "";
		*(char **)block = ce->names;
		ce->names = block;
		ce->names_size = sizeof(char *);
		ce->names_capacity = capacity + sizeof(char *);
	}
	memcpy(ce->names + ce->names_size, made, length);
	ce->names_size += length;
	return ce->names + ce->names_size - length;
}

/* ---------- models */

boolean scene_pack_ce_model_convert(
	struct scene_pack_ce *ce,
	unsigned long model_address,
	long source_index,
	char *why,
	long why_size)
{
	unsigned char *model = ce_at(ce, model_address, MODEL_ROOT_SIZE);
	long node_count, shader_count, geometry_count, geometry_index;
	boolean local_nodes;
	unsigned long geometries;

	if (!model)
	{
		why_set(why, why_size, "a model of the map is outside its tags%s", NULL);
		return FALSE;
	}
	node_count = (long)read_u32(model + MODEL_NODES_OFFSET);
	shader_count = (long)read_u32(model + MODEL_SHADERS_OFFSET);
	geometry_count = (long)read_u32(model + MODEL_GEOMETRIES_OFFSET);
	geometries = read_u32(model + MODEL_GEOMETRIES_OFFSET + 4);
	local_nodes = (read_u32(model) & (1UL << MODEL_FLAGS_LOCAL_NODES_BIT)) != 0;
	if (node_count < 1 || node_count > MAXIMUM_SKINNED_NODES)
	{
		if (why && why_size > 0)
			snprintf(why, (size_t)why_size, "a model of the map has %ld nodes: a pack's are drawn with at most %d",
				node_count, MAXIMUM_SKINNED_NODES);
		return FALSE;
	}
	if (geometry_count < 0 || (geometry_count && !ce_at(ce, geometries, (unsigned long)geometry_count * MODEL_GEOMETRY_SIZE)))
	{
		why_set(why, why_size, "a model of the map has its geometries outside its tags%s", NULL);
		return FALSE;
	}
	for (geometry_index = 0; geometry_index < geometry_count; geometry_index++)
	{
		unsigned char *geometry = ce_at(ce, geometries + (unsigned long)geometry_index * MODEL_GEOMETRY_SIZE,
			MODEL_GEOMETRY_SIZE);
		long part_count = (long)read_u32(geometry + MODEL_GEOMETRY_PARTS_OFFSET);
		unsigned long parts = read_u32(geometry + MODEL_GEOMETRY_PARTS_OFFSET + 4);
		long part_index;

		if (part_count < 0 || (part_count && !ce_at(ce, parts, (unsigned long)part_count * GBXMODEL_PART_SIZE)))
		{
			why_set(why, why_size, "a model of the map has its parts outside its tags%s", NULL);
			return FALSE;
		}
		/* (each part read before this build's, smaller, is written where it
		was: never over a part not yet read) */
		for (part_index = 0; part_index < part_count; part_index++)
		{
			unsigned char source[GBXMODEL_PART_SIZE];
			unsigned char *part = ce_at(ce, parts + (unsigned long)part_index * MODEL_PART_SIZE, MODEL_PART_SIZE);
			struct ce_part *record;
			short shader_index;
			long strip_triangle_count, vertex_count;
			short primary, secondary;
			char previous, next;
			byte local_count;
			short node;

			memcpy(source, ce_at(ce, parts + (unsigned long)part_index * GBXMODEL_PART_SIZE, GBXMODEL_PART_SIZE),
				GBXMODEL_PART_SIZE);
			shader_index = read_s16(source + 4);
			previous = (char)source[6];
			next = (char)source[7];
			primary = read_s16(source + 8);
			secondary = read_s16(source + 10);
			strip_triangle_count = (long)read_u32(source + 0x48);
			vertex_count = (long)read_u32(source + 0x58);
			local_count = source[0x6B];
			/* (custom_edition_geometry.c's custom_edition_model_part_verify,
			before the vertices: the part's indices) */
			if (local_nodes)
			{
				if (!local_count || local_count > MAXIMUM_PART_LOCAL_NODES)
				{
					why_set(why, why_size, "a model part of the map names its local nodes wrongly%s", NULL);
					return FALSE;
				}
				for (node = 0; node < local_count; node++)
				{
					if (source[0x6C + node] >= node_count)
					{
						why_set(why, why_size, "a model part of the map names a node its model has not%s", NULL);
						return FALSE;
					}
				}
				/* (a part's centroid nodes among its own: made the model's) */
				if (primary >= 0 && primary < local_count)
					primary = source[0x6C + primary];
				if (secondary >= 0 && secondary < local_count)
					secondary = source[0x6C + secondary];
			}
			if (shader_index < 0 || shader_index >= shader_count || primary < 0 || primary >= node_count ||
				secondary < 0 || secondary >= node_count || previous < NONE || previous >= part_count ||
				next < NONE || next >= part_count || strip_triangle_count < 1 || strip_triangle_count > MAXIMUM_PART_VERTICES ||
				vertex_count < 1 || vertex_count > MAXIMUM_PART_VERTICES || read_s16(source + 0x44) != TRIANGLE_BUFFER_TYPE_STRIP)
			{
				why_set(why, why_size, "a model part of the map names a shader, node or part its model has not%s", NULL);
				return FALSE;
			}
			if (ce->part_count == ce->part_capacity)
			{
				long capacity = ce->part_capacity ? ce->part_capacity * 2 : 64;
				struct ce_part *grown = capacity <= 65536 ? realloc(ce->parts, sizeof(*grown) * (unsigned long)capacity) : NULL;

				if (!grown)
				{
					why_set(why, why_size, "there is no memory for the models' parts%s", NULL);
					return FALSE;
				}
				ce->parts = grown;
				ce->part_capacity = capacity;
			}
			record = &ce->parts[ce->part_count++];
			memset(record, 0, sizeof(*record));
			record->address = parts + (unsigned long)part_index * MODEL_PART_SIZE;
			record->vertex_offset = read_u32(source + 0x64);
			record->vertex_count = vertex_count;
			record->strip_offset = read_u32(source + 0x4C);
			record->strip_index_count = strip_triangle_count + 2;
			record->model_node_count = (short)node_count;
			record->local_node_count = local_nodes ? local_count : 0;
			if (local_nodes)
				memcpy(record->local_nodes, source + 0x6C, local_count);

			/* this build's part: the fields both have, its blocks none (as an
			Xbox map's), its buffers' kinds and counts (their places the
			builder's) */
			memset(part, 0, MODEL_PART_SIZE);
			write_u32(part, read_u32(source) & ~(1UL << MODEL_PART_FLAGS_LOCAL_NODES_BIT));
			write_u16(part + 4, (unsigned short)shader_index);
			part[6] = (unsigned char)previous;
			part[7] = (unsigned char)next;
			write_u16(part + 8, (unsigned short)primary);
			write_u16(part + 10, (unsigned short)secondary);
			/* (the centroid's node weights and point) */
			memcpy(part + 12, source + 12, 20);
			write_u16(part + 0x44, TRIANGLE_BUFFER_TYPE_STRIP);
			write_u32(part + 0x48, (unsigned long)strip_triangle_count);
			write_u16(part + 0x54, RASTERIZER_VERTEX_TYPE_MODEL_COMPRESSED);
			write_u32(part + 0x58, (unsigned long)vertex_count);
		}
	}
	/* (the parts' nodes are the model's now) */
	write_u32(model, read_u32(model) & ~(1UL << MODEL_FLAGS_LOCAL_NODES_BIT));
	(void)source_index;
	return TRUE;
}

static unsigned long compress_vector(float const *vector)
{
	/* (rasterizer_geometry.c's compress_real_vector3d_to_int32_clamp) */
	float i = vector[0] < -1.f ? -1.f : vector[0] > 1.f ? 1.f : vector[0];
	float j = vector[1] < -1.f ? -1.f : vector[1] > 1.f ? 1.f : vector[1];
	float k = vector[2] < -1.f ? -1.f : vector[2] > 1.f ? 1.f : vector[2];
	long ci = (long)floor((double)i * 1023.5f) & 0x7FF;
	long cj = (long)floor((double)j * 1023.5f) & 0x7FF;
	long ck = (long)floor((double)k * 511.5f) & 0x3FF;

	return (unsigned long)((((ck << 11) | cj) << 11) | ci);
}

static short compress_real(float value)
{
	/* (compress_real_to_int16_clamp) */
	float clamped = value < -1.f ? -1.f : value > 1.f ? 1.f : value;

	return (short)(long)floor((double)clamped * 32767.5f);
}

/* a vector the compressor takes (custom_edition_geometry.c's
vector_compressible: within 1.005, not a NaN) */
static boolean vector_compressible(float const *vector)
{
	return fabs((double)vector[0]) <= 1.005f && fabs((double)vector[1]) <= 1.005f && fabs((double)vector[2]) <= 1.005f;
}

boolean scene_pack_ce_part_buffers(
	struct scene_pack_ce *ce,
	unsigned long part_address,
	unsigned char **vertices,
	unsigned long *vertex_bytes,
	unsigned char **indices,
	unsigned long *index_bytes,
	char *why,
	long why_size)
{
	struct ce_part const *part = NULL;
	unsigned char *source = NULL;
	unsigned char *compressed = NULL;
	unsigned char *strip = NULL;
	unsigned long source_bytes, strip_bytes;
	long index;

	*vertices = *indices = NULL;
	*vertex_bytes = *index_bytes = 0;
	for (index = ce->part_count - 1; index >= 0; index--)
	{
		if (ce->parts[index].address == part_address)
		{
			part = &ce->parts[index];
			break;
		}
	}
	if (!part)
	{
		why_set(why, why_size, "a model part of the map was not made this game's%s", NULL);
		return FALSE;
	}
	source_bytes = (unsigned long)part->vertex_count * UNCOMPRESSED_MODEL_VERTEX_SIZE;
	strip_bytes = (unsigned long)part->strip_index_count * 2;
	/* (cache_file_formats.c checked both lie within the model data; again) */
	if (part->vertex_offset > ce->model_data_bytes || source_bytes > ce->model_data_bytes - part->vertex_offset ||
		ce->model_index_data_offset > ce->model_data_bytes ||
		part->strip_offset > ce->model_data_bytes - ce->model_index_data_offset ||
		strip_bytes > ce->model_data_bytes - ce->model_index_data_offset - part->strip_offset)
	{
		why_set(why, why_size, "a model part's vertices or strip are outside the map's model data%s", NULL);
		return FALSE;
	}
	source = malloc(source_bytes);
	compressed = malloc((unsigned long)part->vertex_count * COMPRESSED_MODEL_VERTEX_SIZE);
	strip = malloc(strip_bytes);
	if (!source || !compressed || !strip)
	{
		why_set(why, why_size, "there is no memory for a model part%s", NULL);
		goto failed;
	}
	if (!ce_file_read(&ce->map, (uint32_t)(ce->model_data_offset + part->vertex_offset), (uint32_t)source_bytes, source) ||
		!ce_file_read(&ce->map, (uint32_t)(ce->model_data_offset + ce->model_index_data_offset + part->strip_offset),
			(uint32_t)strip_bytes, strip))
	{
		why_set(why, why_size, "the map's model data cannot be read%s", NULL);
		goto failed;
	}
	for (index = 0; index < part->strip_index_count; index++)
	{
		if ((long)(strip[index * 2] | (strip[index * 2 + 1] << 8)) >= part->vertex_count)
		{
			why_set(why, why_size, "a model part's strip names a vertex it has not%s", NULL);
			goto failed;
		}
	}
	/* (a vertex: position, normal, binormal, tangent (3 floats each), its
	texture coordinates (2), its nodes (2 shorts), their weights (2 floats)) */
	for (index = 0; index < part->vertex_count; index++)
	{
		unsigned char const *vertex = source + index * UNCOMPRESSED_MODEL_VERTEX_SIZE;
		unsigned char *out = compressed + index * COMPRESSED_MODEL_VERTEX_SIZE;
		float values[14];
		short nodes[2];
		long limit = part->local_node_count ? part->local_node_count : part->model_node_count;
		short slot;

		memcpy(values, vertex, sizeof(values));
		nodes[0] = read_s16(vertex + 56);
		nodes[1] = read_s16(vertex + 58);
		for (slot = 0; slot < 2; slot++)
		{
			if (nodes[slot] < 0 || nodes[slot] >= limit)
			{
				why_set(why, why_size, "a model part's vertex names a node it has not%s", NULL);
				goto failed;
			}
			if (part->local_node_count)
				nodes[slot] = part->local_nodes[nodes[slot]];
		}
		if (!vector_compressible(values + 3) || !vector_compressible(values + 6) || !vector_compressible(values + 9) ||
			!(values[0] == values[0] && values[1] == values[1] && values[2] == values[2]))
		{
			why_set(why, why_size, "a model part has a vertex this game cannot compress%s", NULL);
			goto failed;
		}
		/* (rasterizer_geometry_compress_vertices: position, the three
		vectors packed, the texture coordinates as shorts, the nodes times
		three, the first weight as a short) */
		memcpy(out, vertex, 12);
		write_u32(out + 12, compress_vector(values + 3));
		write_u32(out + 16, compress_vector(values + 6));
		write_u32(out + 20, compress_vector(values + 9));
		write_u16(out + 24, (unsigned short)compress_real(values[12]));
		write_u16(out + 26, (unsigned short)compress_real(values[13]));
		out[28] = (unsigned char)(nodes[0] * 3);
		out[29] = (unsigned char)(nodes[1] * 3);
		{
			float weight;

			memcpy(&weight, vertex + 60, 4);
			write_u16(out + 30, (unsigned short)compress_real(weight));
		}
	}
	free(source);
	*vertices = compressed;
	*vertex_bytes = (unsigned long)part->vertex_count * COMPRESSED_MODEL_VERTEX_SIZE;
	*indices = strip;
	*index_bytes = strip_bytes;
	return TRUE;

failed:
	if (source)
		free(source);
	if (compressed)
		free(compressed);
	if (strip)
		free(strip);
	return FALSE;
}

/* ---------- channel orders */

/* a model shader to its reflection cube map (custom_edition_bitmaps.c's
struct shader_model_maps): the bitmaps it draws with */
enum
{
	SHADER_MODEL_FLAGS_OFFSET = 0x28,
	SHADER_MODEL_BASE_MAP_INDEX_OFFSET = 0xB0,
	SHADER_MODEL_MULTIPURPOSE_MAP_INDEX_OFFSET = 0xC8,
	SHADER_MODEL_DETAIL_MAP_INDEX_OFFSET = 0xE8,
	SHADER_MODEL_REFLECTION_CUBE_MAP_INDEX_OFFSET = 0x164 + 12,
	SHADER_MODEL_SIZE = 0x164 + 16,
	/* a weapon HUD interface's statics and meters, their elements' size and
	where their element definitions are (custom_edition_bitmaps.c) */
	WEAPON_HUD_STATICS_OFFSET = 0x60,
	WEAPON_HUD_METERS_OFFSET = 0x6C,
	WEAPON_HUD_ELEMENT_SIZE = 0xB4,
	WEAPON_HUD_ELEMENT_DEFINITION_OFFSET = 0x24,
};

/* the bitmap handle names noted as drawn in order (CHANNELS_XBOX: as a
texture of this build's channels) */
static void channel_note(struct scene_pack_ce *ce, unsigned long handle, unsigned char order)
{
	long index = ce_tag(ce, handle);
	unsigned char *instance = index != NONE ? ce_instance(ce, index) : NULL;

	if (!instance || read_u32(instance) != BITMAP_GROUP_TAG)
		return;
	if (ce->channels[index] == CHANNELS_UNKNOWN)
		ce->channels[index] = order;
	else if (ce->channels[index] != order)
		ce->channels[index] = CHANNELS_KEPT;
}

static void meter_note(struct scene_pack_ce *ce, struct meter_hud_element_definition const *meter)
{
	channel_note(ce, (unsigned long)meter->meter_bitmap.index,
		(meter->meter_flags & (1 << HUD_METER_USE_XBOX_SHADING_BIT)) ? CHANNELS_XBOX : CHANNELS_HUD_METER);
}

void scene_pack_ce_channels(
	struct scene_pack_ce *ce,
	unsigned long const *groups,
	unsigned long const *roots,
	long count)
{
	long index;

	for (index = 0; index < count; index++)
	{
		if (groups[index] == 'soso')
		{
			unsigned char *shader = ce_at(ce, roots[index], SHADER_MODEL_SIZE);

			if (!shader)
				continue;
			channel_note(ce, read_u32(shader + SHADER_MODEL_MULTIPURPOSE_MAP_INDEX_OFFSET),
				(read_s16(shader + SHADER_MODEL_FLAGS_OFFSET) & (1 << SHADER_MODEL_XBOX_MULTIPURPOSE_BIT)) ?
					CHANNELS_XBOX : CHANNELS_MULTIPURPOSE);
			channel_note(ce, read_u32(shader + SHADER_MODEL_BASE_MAP_INDEX_OFFSET), CHANNELS_XBOX);
			channel_note(ce, read_u32(shader + SHADER_MODEL_DETAIL_MAP_INDEX_OFFSET), CHANNELS_XBOX);
			channel_note(ce, read_u32(shader + SHADER_MODEL_REFLECTION_CUBE_MAP_INDEX_OFFSET), CHANNELS_XBOX);
		}
		else if (groups[index] == UNIT_HUD_INTERFACE_DEFINITION_TAG)
		{
			struct unit_hud_interface_definition *hud = (struct unit_hud_interface_definition *)
				ce_at(ce, roots[index], sizeof(struct unit_hud_interface_definition));
			long element;

			if (!hud)
				continue;
			meter_note(ce, &hud->shield_meter.meter);
			meter_note(ce, &hud->health_meter.meter);
			channel_note(ce, (unsigned long)hud->background.interface_bitmap.index, CHANNELS_XBOX);
			channel_note(ce, (unsigned long)hud->shield_meter.background.interface_bitmap.index, CHANNELS_XBOX);
			channel_note(ce, (unsigned long)hud->health_meter.background.interface_bitmap.index, CHANNELS_XBOX);
			channel_note(ce, (unsigned long)hud->motion_sensor.background.interface_bitmap.index, CHANNELS_XBOX);
			channel_note(ce, (unsigned long)hud->motion_sensor.foreground.interface_bitmap.index, CHANNELS_XBOX);
			for (element = 0; element < hud->auxilary_meters.count; element++)
			{
				struct auxilary_meter_definition *meter = (struct auxilary_meter_definition *)ce_at(ce,
					(unsigned long)hud->auxilary_meters.address + (unsigned long)element * sizeof(*meter), sizeof(*meter));

				if (!meter)
					break;
				meter_note(ce, &meter->panel.meter);
				channel_note(ce, (unsigned long)meter->panel.background.interface_bitmap.index, CHANNELS_XBOX);
			}
			for (element = 0; element < hud->auxilary_panel.auxilary_overlays.count; element++)
			{
				struct auxilary_overlay_definition *overlay = (struct auxilary_overlay_definition *)ce_at(ce,
					(unsigned long)hud->auxilary_panel.auxilary_overlays.address + (unsigned long)element * sizeof(*overlay),
					sizeof(*overlay));

				if (!overlay)
					break;
				channel_note(ce, (unsigned long)overlay->static_element.interface_bitmap.index, CHANNELS_XBOX);
			}
		}
		else if (groups[index] == 'wphi')
		{
			unsigned char *hud = ce_at(ce, roots[index], WEAPON_HUD_METERS_OFFSET + 12);
			short which;

			if (!hud)
				continue;
			for (which = 0; which < 2; which++)
			{
				unsigned char *block = hud + (which ? WEAPON_HUD_METERS_OFFSET : WEAPON_HUD_STATICS_OFFSET);
				long element_count = (long)read_u32(block);
				long element;

				for (element = 0; element < element_count; element++)
				{
					unsigned char *definition = ce_at(ce, read_u32(block + 4) + (unsigned long)element * WEAPON_HUD_ELEMENT_SIZE,
						WEAPON_HUD_ELEMENT_SIZE);

					if (!definition)
						break;
					if (which)
						meter_note(ce, (struct meter_hud_element_definition const *)(definition + WEAPON_HUD_ELEMENT_DEFINITION_OFFSET));
					else
						channel_note(ce, (unsigned long)((struct static_hud_element_definition const *)
							(definition + WEAPON_HUD_ELEMENT_DEFINITION_OFFSET))->interface_bitmap.index, CHANNELS_XBOX);
				}
			}
		}
	}
}

/* ---------- bitmaps */

/* bits a pixel, by format (tag_schema_render.c) */
static char const bitmap_bits_per_pixel[NUMBER_OF_BITMAP_FORMATS] =
{
	8, 8, 8, 16, 0, 0, 16, 0, 16, 16, 32, 32, 0, 0, 4, 8, 8, 8
};

/* a level's width, height and depth as the game takes them (bitmaps.c:
compressed levels padded to whole blocks) */
static void level_dimensions(struct bitmap_data const *bitmap, short level, long *width, long *height, long *depth)
{
	*width = MAX(bitmap->width >> level, 1);
	*height = MAX(bitmap->height >> level, 1);
	*depth = MAX(bitmap->depth >> level, 1);
	if (bitmap->flags & BITMAP_FLAG_COMPRESSED)
	{
		*width += (-*width) & 3;
		*height += (-*height) & 3;
	}
}

/* a level's bytes (all six faces of a cube map's: bitmap_mipmap_get_pixel_
data_size) */
static unsigned long level_bytes(struct bitmap_data const *bitmap, short level)
{
	long width, height, depth;

	level_dimensions(bitmap, level, &width, &height, &depth);
	return (unsigned long)(width * height * depth * (bitmap->type == BITMAP_TYPE_CUBE_MAP ? 6 : 1)) *
		(unsigned long)bitmap_bits_per_pixel[bitmap->format] / 8;
}

/* where a level is in Halo PC's layout (bitmap_2d/3d/cube_map_address:
levels one after another, compressed levels at least 4 by 4) */
static unsigned long pc_level_offset(struct bitmap_data const *bitmap, short level)
{
	long width = bitmap->width, height = bitmap->height, depth = bitmap->depth;
	long minimum = (bitmap->flags & BITMAP_FLAG_COMPRESSED) ? 4 : 1;
	unsigned long pixels = 0;
	short index;

	for (index = 0; index < level; index++)
	{
		if (bitmap->type == BITMAP_TYPE_CUBE_MAP)
			pixels += (unsigned long)(width * width * 6);
		else if (bitmap->type == BITMAP_TYPE_3D)
			pixels += (unsigned long)(width * height * depth);
		else
			pixels += (unsigned long)(width * height);
		width = MAX(minimum, width >> 1);
		height = MAX(minimum, height >> 1);
		depth = MAX(1, depth >> 1);
	}
	return pixels * (unsigned long)bitmap_bits_per_pixel[bitmap->format] / 8;
}

/* the Xbox's levels of it (rasterizer_xbox_bitmap_get_max_mipmap_count) */
static short xbox_maximum_level(struct bitmap_data const *bitmap)
{
	if (!(bitmap->flags & BITMAP_FLAG_POWER_OF_TWO) || (bitmap->flags & BITMAP_FLAG_LINEAR))
		return 0;
	if (bitmap->flags & BITMAP_FLAG_COMPRESSED)
		return (short)MIN(bitmap->mipmap_count, ce_floor_log2(MAX(bitmap->width / 4, MAX(bitmap->height / 4, bitmap->depth))));
	return (short)MIN(bitmap->mipmap_count, ce_floor_log2(MAX(bitmap->width, MAX(bitmap->height, bitmap->depth))));
}

/* its pixels' bytes as the Xbox lays them out (rasterizer_xbox_bitmap_get_
pixel_data_size) */
static unsigned long xbox_bytes(struct bitmap_data const *bitmap)
{
	short maximum = xbox_maximum_level(bitmap);
	unsigned long offset = 0;
	short level;

	for (level = 0; level <= maximum; level++)
	{
		unsigned long size = level_bytes(bitmap, level);

		if (bitmap->flags & BITMAP_FLAG_LINEAR)
		{
			long width, height, depth;
			unsigned long pitch;

			level_dimensions(bitmap, level, &width, &height, &depth);
			pitch = (unsigned long)width * (unsigned long)bitmap_bits_per_pixel[bitmap->format] / 8;
			size += (unsigned long)height * ((-(long)pitch) & 63);
		}
		if (bitmap->type == BITMAP_TYPE_CUBE_MAP)
			size /= 6;
		offset += size;
	}
	offset += (-(long)offset) & 127;
	if (bitmap->type == BITMAP_TYPE_CUBE_MAP)
		offset *= 6;
	return offset;
}

/* the Xbox's swizzle (compute_swizzle_masks and the swizzles of
rasterizer_swizzle.c): each coordinate's bits interleaved, x's first, then
y's, then z's, while each is below its dimension */
static void swizzle_level(unsigned char *destination, unsigned char const *source, long width, long height, long depth,
	long bytes_per_pixel)
{
	unsigned long x_mask = 0, y_mask = 0, z_mask = 0, bit = 1, size = 1;
	unsigned long x_swizzle, y_swizzle, z_swizzle = 0;
	unsigned long source_index = 0;
	boolean advanced;
	long x, y, z;

	do
	{
		advanced = FALSE;
		if (size < (unsigned long)width)
		{
			x_mask |= bit;
			bit <<= 1;
			advanced = TRUE;
		}
		if (size < (unsigned long)height)
		{
			y_mask |= bit;
			bit <<= 1;
			advanced = TRUE;
		}
		if (size < (unsigned long)depth)
		{
			z_mask |= bit;
			bit <<= 1;
			advanced = TRUE;
		}
		size <<= 1;
	}
	while (advanced);
	for (z = 0; z < depth; z++)
	{
		y_swizzle = 0;
		for (y = 0; y < height; y++)
		{
			x_swizzle = 0;
			for (x = 0; x < width; x++)
			{
				memcpy(destination + (z_swizzle | y_swizzle | x_swizzle) * (unsigned long)bytes_per_pixel,
					source + source_index * (unsigned long)bytes_per_pixel, (size_t)bytes_per_pixel);
				source_index++;
				x_swizzle = (x_swizzle - x_mask) & x_mask;
			}
			y_swizzle = (y_swizzle - y_mask) & y_mask;
		}
		z_swizzle = (z_swizzle - z_mask) & z_mask;
	}
}

/* (the drawable bitmap's checks of custom_edition_bitmaps.c: a format with
a hardware texture, a compressed flag as the format says, power-of-two
dimensions or a linear 2D bitmap, square cube maps, its levels' pixels all
there) */
static boolean bitmap_drawable(struct bitmap_data const *bitmap, unsigned long pc_bytes)
{
	boolean linear = (bitmap->flags & BITMAP_FLAG_LINEAR) != 0;
	boolean compressed_format = bitmap->format >= BITMAP_FORMAT_DXT1 && bitmap->format <= BITMAP_FORMAT_DXT5;

	if (bitmap->format < 0 || bitmap->format >= NUMBER_OF_BITMAP_FORMATS || !bitmap_bits_per_pixel[bitmap->format] ||
		bitmap->type < BITMAP_TYPE_2D || bitmap->type > BITMAP_TYPE_CUBE_MAP ||
		bitmap->width < 1 || bitmap->height < 1 || bitmap->depth < 1 || bitmap->width > MAXIMUM_BITMAP_DIMENSION ||
		bitmap->height > MAXIMUM_BITMAP_DIMENSION || bitmap->depth > MAXIMUM_BITMAP_DIMENSION ||
		bitmap->mipmap_count < 0 || bitmap->mipmap_count > ce_floor_log2(MAX(bitmap->width, MAX(bitmap->height, bitmap->depth))))
	{
		return FALSE;
	}
	if (linear && (compressed_format || bitmap->format == BITMAP_FORMAT_P8_BUMP))
		return FALSE;
	return ((bitmap->flags & BITMAP_FLAG_COMPRESSED) != 0) == compressed_format &&
		(linear ? bitmap->type == BITMAP_TYPE_2D :
			power_of_two(bitmap->width) && power_of_two(bitmap->height) && power_of_two(bitmap->depth)) &&
		(bitmap->type != BITMAP_TYPE_CUBE_MAP || bitmap->width == bitmap->height) &&
		(unsigned long)bitmap->pixels_size >= pc_bytes;
}

/* the pixels of every level of it (bitmap_get_pixel_data_size, as Halo PC
lays them out: every level, compressed ones at least a block) */
static unsigned long pc_bytes_of(struct bitmap_data const *bitmap)
{
	return pc_level_offset(bitmap, (short)(bitmap->mipmap_count + 1));
}

static unsigned short rgb565(unsigned char const *bytes)
{
	return (unsigned short)(bytes[0] | (bytes[1] << 8));
}

static void color565(unsigned short value, unsigned char *bgra)
{
	unsigned char red = (unsigned char)((value >> 11) & 31);
	unsigned char green = (unsigned char)((value >> 5) & 63);
	unsigned char blue = (unsigned char)(value & 31);

	bgra[0] = (unsigned char)((blue << 3) | (blue >> 2));
	bgra[1] = (unsigned char)((green << 2) | (green >> 4));
	bgra[2] = (unsigned char)((red << 3) | (red >> 2));
	bgra[3] = 255;
}

/* a DXT block's 16 pixels (BGRA, row by row) */
static void dxt_block_decode(unsigned char const *block, short format, unsigned char pixels[16][4])
{
	unsigned char const *color_block = format == BITMAP_FORMAT_DXT1 ? block : block + 8;
	unsigned short c0 = rgb565(color_block), c1 = rgb565(color_block + 2);
	unsigned char palette[4][4];
	unsigned long bits = read_u32(color_block + 4);
	short index;

	color565(c0, palette[0]);
	color565(c1, palette[1]);
	if (c0 > c1 || format != BITMAP_FORMAT_DXT1)
	{
		for (index = 0; index < 3; index++)
		{
			palette[2][index] = (unsigned char)((2 * palette[0][index] + palette[1][index]) / 3);
			palette[3][index] = (unsigned char)((palette[0][index] + 2 * palette[1][index]) / 3);
		}
		palette[2][3] = palette[3][3] = 255;
	}
	else
	{
		for (index = 0; index < 3; index++)
			palette[2][index] = (unsigned char)((palette[0][index] + palette[1][index]) / 2);
		palette[2][3] = 255;
		palette[3][0] = palette[3][1] = palette[3][2] = palette[3][3] = 0;
	}
	for (index = 0; index < 16; index++)
		memcpy(pixels[index], palette[(bits >> (2 * index)) & 3], 4);
	if (format == BITMAP_FORMAT_DXT3)
	{
		for (index = 0; index < 16; index++)
		{
			unsigned char nibble = (unsigned char)((block[index / 2] >> (4 * (index & 1))) & 15);

			pixels[index][3] = (unsigned char)(nibble * 17);
		}
	}
	else if (format == BITMAP_FORMAT_DXT5)
	{
		unsigned char a0 = block[0], a1 = block[1];
		unsigned char alphas[8];
		unsigned long long alpha_bits = 0;

		for (index = 0; index < 6; index++)
			alpha_bits |= (unsigned long long)block[2 + index] << (8 * index);
		alphas[0] = a0;
		alphas[1] = a1;
		if (a0 > a1)
		{
			for (index = 1; index < 7; index++)
				alphas[index + 1] = (unsigned char)(((7 - index) * a0 + index * a1) / 7);
		}
		else
		{
			for (index = 1; index < 5; index++)
				alphas[index + 1] = (unsigned char)(((5 - index) * a0 + index * a1) / 5);
			alphas[6] = 0;
			alphas[7] = 255;
		}
		for (index = 0; index < 16; index++)
			pixels[index][3] = alphas[(alpha_bits >> (3 * index)) & 7];
	}
}

/* a pixel of an uncompressed format as BGRA (bitmap_format_to_a8r8g8b8's
formats); FALSE for one that cannot be */
static boolean pixel_decode(short format, unsigned char const *source, unsigned char *bgra)
{
	unsigned short value;

	switch (format)
	{
	case 0: /* a8 */
		bgra[0] = bgra[1] = bgra[2] = 255;
		bgra[3] = source[0];
		return TRUE;
	case 1: /* y8 */
		bgra[0] = bgra[1] = bgra[2] = source[0];
		bgra[3] = 255;
		return TRUE;
	case 2: /* ay8 */
		bgra[0] = bgra[1] = bgra[2] = bgra[3] = source[0];
		return TRUE;
	case 3: /* a8y8 */
		bgra[0] = bgra[1] = bgra[2] = source[0];
		bgra[3] = source[1];
		return TRUE;
	case 6: /* r5g6b5 */
		color565(rgb565(source), bgra);
		return TRUE;
	case 8: /* a1r5g5b5 */
		value = rgb565(source);
		bgra[0] = (unsigned char)(((value & 31) << 3) | ((value & 31) >> 2));
		bgra[1] = (unsigned char)((((value >> 5) & 31) << 3) | (((value >> 5) & 31) >> 2));
		bgra[2] = (unsigned char)((((value >> 10) & 31) << 3) | (((value >> 10) & 31) >> 2));
		bgra[3] = (value & 0x8000) ? 255 : 0;
		return TRUE;
	case 9: /* a4r4g4b4 */
		value = rgb565(source);
		bgra[0] = (unsigned char)((value & 15) * 17);
		bgra[1] = (unsigned char)(((value >> 4) & 15) * 17);
		bgra[2] = (unsigned char)(((value >> 8) & 15) * 17);
		bgra[3] = (unsigned char)(((value >> 12) & 15) * 17);
		return TRUE;
	case 10: /* x8r8g8b8 */
		memcpy(bgra, source, 3);
		bgra[3] = 255;
		return TRUE;
	case 11: /* a8r8g8b8 */
		memcpy(bgra, source, 4);
		return TRUE;
	}
	return FALSE;
}

/* a face (or a 3D level) of width by height (by depth) in format, decoded
to BGRA: FALSE for a format that cannot be */
static boolean face_decode(short format, unsigned char const *source, long width, long height, long depth,
	long visible_width, long visible_height, unsigned char *bgra)
{
	long x, y, z;

	if (format >= BITMAP_FORMAT_DXT1 && format <= BITMAP_FORMAT_DXT5)
	{
		long block_bytes = format == BITMAP_FORMAT_DXT1 ? 8 : 16;
		long blocks_across = width / 4;
		long slice_bytes = blocks_across * (height / 4) * block_bytes;

		/* (a compressed level is whole blocks; its visible part is what
		the uncompressed level keeps; a 3D level slice by slice) */
		for (z = 0; z < depth; z++)
		{
			for (y = 0; y < height; y += 4)
			{
				for (x = 0; x < width; x += 4)
				{
					unsigned char pixels[16][4];
					short index;

					dxt_block_decode(source + z * slice_bytes + ((y / 4) * blocks_across + x / 4) * block_bytes, format, pixels);
					for (index = 0; index < 16; index++)
					{
						long px = x + (index & 3), py = y + (index >> 2);

						if (px < visible_width && py < visible_height)
							memcpy(bgra + ((z * visible_height + py) * visible_width + px) * 4, pixels[index], 4);
					}
				}
			}
		}
		return TRUE;
	}
	for (z = 0; z < depth; z++)
	{
		for (y = 0; y < height; y++)
		{
			for (x = 0; x < width; x++)
			{
				long index = (z * height + y) * width + x;

				if (!pixel_decode(format, source + index * (bitmap_bits_per_pixel[format] / 8), bgra + index * 4))
					return FALSE;
			}
		}
	}
	return TRUE;
}

/* the channels of BGRA pixels put where this build reads them
(xbox_textures.c's custom_edition_channel_sources: each channel of the
output, red, green, blue, alpha, from that channel of Halo PC's) */
static void channels_reorder(unsigned char *bgra, unsigned long count, unsigned char order)
{
	static unsigned char const sources[3][4] = { { 0, 1, 2, 3 }, { 2, 1, 3, 0 }, { 3, 3, 3, 0 } };
	unsigned long index;

	for (index = 0; index < count; index++)
	{
		unsigned char *pixel = bgra + index * 4;
		/* (red, green, blue, alpha of BGRA bytes) */
		unsigned char rgba[4] = { pixel[2], pixel[1], pixel[0], pixel[3] };

		pixel[2] = rgba[sources[order][0]];
		pixel[1] = rgba[sources[order][1]];
		pixel[0] = rgba[sources[order][2]];
		pixel[3] = rgba[sources[order][3]];
	}
}

boolean scene_pack_ce_bitmap_write(
	struct scene_pack_ce *ce,
	void *bitmap_bytes,
	long source_index,
	void const *group_root,
	FILE *data,
	unsigned long *data_size,
	unsigned long *data_crc,
	char *why,
	long why_size)
{
	struct bitmap_data bitmap;
	struct bitmap_group group;
	unsigned char *instance = ce_instance(ce, source_index);
	char const *name = instance ? (char const *)ce_at(ce, read_u32(instance + 16), 1) : NULL;
	unsigned char order = source_index >= 0 && source_index < ce->tag_count ? ce->channels[source_index] : CHANNELS_UNKNOWN;
	unsigned char *pc = NULL;
	unsigned char *laid = NULL;
	unsigned long pc_bytes, size, offset;
	short faces, face, level, maximum;
	boolean reorder;

	memcpy(&bitmap, bitmap_bytes, sizeof(bitmap));
	memcpy(&group, group_root, sizeof(group));
	pc_bytes = bitmap_drawable(&bitmap, 0) ? pc_bytes_of(&bitmap) : 0;
	if (!pc_bytes || !bitmap_drawable(&bitmap, pc_bytes) || pc_bytes > MAXIMUM_BITMAP_BYTES)
	{
		if (why && why_size > 0)
			snprintf(why, (size_t)why_size, "a bitmap of %s (type %d, format %d, %dx%dx%d) cannot be drawn by this game",
				name ? name : "the map", bitmap.type, bitmap.format, bitmap.width, bitmap.height, bitmap.depth);
		return FALSE;
	}
	/* (a large 2D texture from its second level, as the game draws a Custom
	Edition map's: custom_edition_bitmaps_reduce) */
	bitmap.levels_dropped = 0;
	if (group.type == BITMAP_GROUP_TYPE_2D_TEXTURES && name && !name_starts(name, "ui\\") &&
		!name_starts(name, "interface\\") && bitmap.type == BITMAP_TYPE_2D && !(bitmap.flags & BITMAP_FLAG_LINEAR) &&
		bitmap.mipmap_count >= 1 && bitmap.width >= 8 && bitmap.height >= 8 &&
		level_bytes(&bitmap, 0) >= REDUCED_TEXTURE_LEVEL_BYTES)
	{
		unsigned long first = pc_level_offset(&bitmap, 1);

		bitmap.pixels_offset += (long)first;
		bitmap.pixels_size -= (long)first;
		bitmap.width /= 2;
		bitmap.height /= 2;
		bitmap.mipmap_count--;
		bitmap.levels_dropped = 1;
		pc_bytes = pc_bytes_of(&bitmap);
		ce->bitmaps_reduced++;
	}
	pc = malloc(pc_bytes);
	if (!pc)
	{
		why_set(why, why_size, "there is no memory for a bitmap's pixels%s", NULL);
		return FALSE;
	}
	if (!ce_combined_read(ce, (unsigned long)bitmap.pixels_offset, pc_bytes, pc))
	{
		free(pc);
		why_set(why, why_size, "a bitmap's pixels cannot be read%s", NULL);
		return FALSE;
	}
	faces = bitmap.type == BITMAP_TYPE_CUBE_MAP ? 6 : 1;

	/* (the channels Halo PC keeps elsewhere put where this build reads
	them: decoded to 32-bit colour, which keeps every channel) */
	reorder = (order == CHANNELS_MULTIPURPOSE || order == CHANNELS_HUD_METER) &&
		((bitmap.format >= BITMAP_FORMAT_DXT1 && bitmap.format <= BITMAP_FORMAT_DXT5) ||
		(bitmap.format >= 0 && bitmap.format <= 3) || bitmap.format == 6 || (bitmap.format >= 8 && bitmap.format <= 11));
	if (reorder)
	{
		struct bitmap_data decoded = bitmap;
		unsigned long decoded_bytes;
		unsigned char *bgra;

		decoded.format = BITMAP_FORMAT_A8R8G8B8;
		decoded.flags = (unsigned short)(decoded.flags & ~BITMAP_FLAG_COMPRESSED);
		decoded_bytes = pc_bytes_of(&decoded);
		bgra = decoded_bytes <= MAXIMUM_BITMAP_BYTES ? malloc(decoded_bytes) : NULL;
		if (!bgra)
		{
			free(pc);
			why_set(why, why_size, "there is no memory for a bitmap's pixels%s", NULL);
			return FALSE;
		}
		for (level = 0; level <= bitmap.mipmap_count; level++)
		{
			long width, height, depth, visible_width, visible_height, visible_depth;
			unsigned long source_face = level_bytes(&bitmap, level) / (unsigned long)faces;
			unsigned long decoded_face = level_bytes(&decoded, level) / (unsigned long)faces;

			level_dimensions(&bitmap, level, &width, &height, &depth);
			level_dimensions(&decoded, level, &visible_width, &visible_height, &visible_depth);
			for (face = 0; face < faces; face++)
			{
				unsigned char *out = bgra + pc_level_offset(&decoded, level) + (unsigned long)face * decoded_face;

				face_decode(bitmap.format, pc + pc_level_offset(&bitmap, level) + (unsigned long)face * source_face,
					width, height, depth, visible_width, visible_height, out);
				channels_reorder(out, decoded_face / 4, order);
			}
		}
		free(pc);
		pc = bgra;
		pc_bytes = decoded_bytes;
		bitmap = decoded;
		ce->bitmaps_reordered++;
	}

	/* laid out as the Xbox's (rasterizer_xbox_bitmap_rebuild_hardware_format):
	face by face, each level swizzled (uncompressed, not linear) or as it
	is, linear rows padded to 64 bytes, each face padded to 128 */
	if (!(bitmap.flags & BITMAP_FLAG_COMPRESSED) && !(bitmap.flags & BITMAP_FLAG_LINEAR))
		bitmap.flags |= BITMAP_FLAG_POWER_OF_TWO;
	size = xbox_bytes(&bitmap);
	laid = size <= MAXIMUM_BITMAP_BYTES ? zeroed(size) : NULL;
	if (!laid)
	{
		free(pc);
		why_set(why, why_size, "there is no memory for a bitmap's pixels%s", NULL);
		return FALSE;
	}
	maximum = xbox_maximum_level(&bitmap);
	offset = 0;
	for (face = 0; face < faces; face++)
	{
		/* (face_mapping_inverse_table: the Xbox's second and third faces
		are Halo PC's third and second) */
		static short const face_mapping[6] = { 0, 2, 1, 3, 4, 5 };

		for (level = 0; level <= maximum; level++)
		{
			unsigned long face_bytes = level_bytes(&bitmap, level) / (unsigned long)faces;
			unsigned char const *source = pc + pc_level_offset(&bitmap, level) + (unsigned long)face_mapping[face] * face_bytes;
			long width, height, depth;

			level_dimensions(&bitmap, level, &width, &height, &depth);
			if (bitmap.flags & BITMAP_FLAG_LINEAR)
			{
				unsigned long pitch = (unsigned long)width * (unsigned long)bitmap_bits_per_pixel[bitmap.format] / 8;
				unsigned long padding = (unsigned long)((-(long)pitch) & 63);
				long row;

				for (row = 0; row < height; row++)
				{
					memcpy(laid + offset, source + (unsigned long)row * pitch, pitch);
					offset += pitch + padding;
				}
			}
			else if (!(bitmap.flags & BITMAP_FLAG_COMPRESSED) && bitmap_bits_per_pixel[bitmap.format] >= 8)
			{
				swizzle_level(laid + offset, source, width, height, bitmap.type == BITMAP_TYPE_3D ? depth : 1,
					bitmap_bits_per_pixel[bitmap.format] / 8);
				offset += face_bytes;
			}
			else
			{
				memcpy(laid + offset, source, face_bytes);
				offset += face_bytes;
			}
		}
		offset += (unsigned long)((-(long)offset) & 127);
	}
	free(pc);
	if (offset != size)
	{
		free(laid);
		why_set(why, why_size, "a bitmap's pixels cannot be laid out as this game's%s", NULL);
		return FALSE;
	}
	if (fwrite(laid, 1, size, data) != size)
	{
		free(laid);
		why_set(why, why_size, "the pack cannot be written%s", NULL);
		return FALSE;
	}
	*data_crc = (unsigned long)crc32((uLong)*data_crc, (Bytef const *)laid, (uInt)size);
	free(laid);
	if (!(bitmap.flags & BITMAP_FLAG_COMPRESSED) && !(bitmap.flags & BITMAP_FLAG_LINEAR))
		bitmap.flags |= BITMAP_FLAG_SWIZZLED;
	bitmap.flags |= BITMAP_FLAG_CACHED;
	bitmap.pixels_offset = (long)*data_size;
	bitmap.pixels_size = (long)size;
	*data_size += size;
	memcpy(bitmap_bytes, &bitmap, sizeof(bitmap));
	return TRUE;
}

/* ---------- sounds */

struct pcm_reader
{
	struct scene_pack_ce *ce;
	unsigned long offset;
	unsigned long size;
};

static int ogg_read(void *context, uint32_t offset, uint32_t size, void *buffer)
{
	struct pcm_reader *reader = context;

	if (offset > reader->size || size > reader->size - offset)
		return 0;
	return ce_combined_read(reader->ce, reader->offset + offset, size, buffer);
}

boolean scene_pack_ce_permutation_write(
	struct scene_pack_ce *ce,
	void *permutation_bytes,
	void const *sound_bytes,
	FILE *data,
	unsigned long *data_size,
	unsigned long *data_crc,
	char *why,
	long why_size)
{
	struct sound_permutation permutation;
	struct sound_definition sound;
	int channels;
	long rate;
	unsigned long output_bytes;
	unsigned char *output = NULL;
	unsigned long written = 0;
	short compression;
	struct pcm_reader reader;

	memcpy(&permutation, permutation_bytes, sizeof(permutation));
	memcpy(&sound, sound_bytes, sizeof(sound));
	channels = sound.encoding == SOUND_ENCODING_STEREO ? 2 : 1;
	rate = sound.sample_rate == SOUND_SAMPLE_RATE_44KHZ ? 44100 : 22050;
	compression = permutation.compression;
	reader.ce = ce;
	reader.offset = (unsigned long)permutation.samples.file_offset;
	reader.size = permutation.samples.size > 0 ? (unsigned long)permutation.samples.size : 0;
	if (!reader.size)
	{
		permutation.samples.size = 0;
		permutation.samples.file_offset = 0;
		memcpy(permutation_bytes, &permutation, sizeof(permutation));
		return TRUE;
	}
	if (compression == SOUND_COMPRESSION_XBOX_ADPCM)
	{
		/* (as it is: Xbox ADPCM of whole blocks, cache_file_formats.c's
		sound_prepare) */
		output_bytes = reader.size;
		if (output_bytes > 0x400000UL)
			output_bytes = 0;
		output = output_bytes ? malloc(output_bytes) : NULL;
		if (output_bytes && (!output || !ce_combined_read(ce, reader.offset, output_bytes, output)))
		{
			if (output)
				free(output);
			why_set(why, why_size, "a sound's samples cannot be read%s", NULL);
			return FALSE;
		}
		written = output_bytes;
	}
	else
	{
		struct xbox_adpcm_encoder encoder;
		boolean halved = (compression & CUSTOM_EDITION_PERMUTATION_HALVED) != 0;

		output_bytes = permutation.sample_buffer_size;
		if (compression == SOUND_COMPRESSION_OGG_VORBIS && !output_bytes)
		{
			/* (a length the loader could not give: measured from the
			stream's first and last pages) */
			unsigned char head[4096], tail[65536];
			unsigned long head_bytes = reader.size < sizeof(head) ? reader.size : sizeof(head);
			unsigned long tail_bytes = reader.size < sizeof(tail) ? reader.size : sizeof(tail);
			struct ogg_sound_stream_info info;

			if (ogg_read(&reader, 0, (uint32_t)head_bytes, head) &&
				ogg_read(&reader, (uint32_t)(reader.size - tail_bytes), (uint32_t)tail_bytes, tail) &&
				ogg_sound_measure(head, (uint32_t)head_bytes, tail, (uint32_t)tail_bytes, &info))
			{
				output_bytes = ogg_sound_adpcm_bytes(ogg_sound_output_frames(info.frames, info.rate, rate), channels);
			}
		}
		if (output_bytes > OGG_SOUND_MAXIMUM_ADPCM_BYTES)
		{
			output_bytes = OGG_SOUND_MAXIMUM_ADPCM_BYTES / (OGG_SOUND_ADPCM_BLOCK_BYTES * (unsigned long)channels) *
				(OGG_SOUND_ADPCM_BLOCK_BYTES * (unsigned long)channels);
			ce->permutations_truncated++;
		}
		output = output_bytes ? malloc(output_bytes) : NULL;
		if (output_bytes && !output)
		{
			why_set(why, why_size, "there is no memory for a sound%s", NULL);
			return FALSE;
		}
		if (output_bytes && compression == SOUND_COMPRESSION_OGG_VORBIS)
		{
			struct ogg_sound_request request;
			struct ogg_sound_result result;

			if (!ce->working)
			{
				ce->working = malloc(OGG_SOUND_WORKING_BYTES + 8);
				ce->working_aligned = ce->working ?
					(void *)(((unsigned long)ce->working + 7) & ~7UL) : NULL;
			}
			memset(&request, 0, sizeof(request));
			request.read = ogg_read;
			request.read_context = &reader;
			request.input_bytes = (uint32_t)reader.size;
			request.output = output;
			request.output_bytes = (uint32_t)output_bytes;
			request.channels = channels;
			request.rate = rate;
			request.working = ce->working_aligned;
			request.working_bytes = OGG_SOUND_WORKING_BYTES;
			memset(&result, 0, sizeof(result));
			if (!ce->working || ogg_sound_transcode(&request, &result) != _ogg_sound_ok)
			{
				/* (a stream this game cannot decode plays as silence, as the
				game's sound cache leaves it) */
				platform_log("scene packs: an Ogg Vorbis permutation of %s.map cannot be decoded (%s): silent",
					ce->name, ogg_sound_status_describe(result.status));
				memset(output, 0, output_bytes);
			}
			written = output_bytes;
		}
		else if (output_bytes && (compression == SOUND_COMPRESSION_NONE || halved))
		{
			struct xbox_adpcm_rate rate_state;
			unsigned char piece[PCM_READ_BYTES];
			boolean adpcm = halved && (compression & 0xFF) == SOUND_COMPRESSION_XBOX_ADPCM;
			unsigned long unit = adpcm ? (unsigned long)OGG_SOUND_ADPCM_BLOCK_BYTES * (unsigned long)channels :
				2UL * (unsigned long)channels;
			unsigned long piece_bytes = sizeof(piece) / unit * unit;
			unsigned long offset = 0;

			if (!xbox_adpcm_encoder_begin(&encoder, output, (uint32_t)output_bytes, channels) ||
				!xbox_adpcm_rate_begin(&rate_state, &encoder, halved ? 2 * rate : rate, rate))
			{
				free(output);
				why_set(why, why_size, "a sound of the map cannot be encoded%s", NULL);
				return FALSE;
			}
			while (offset + unit <= reader.size && !encoder.full)
			{
				unsigned long size = reader.size - offset < piece_bytes ? (reader.size - offset) / unit * unit : piece_bytes;
				unsigned long at;

				if (!ogg_read(&reader, (uint32_t)offset, (uint32_t)size, piece))
				{
					free(output);
					why_set(why, why_size, "a sound's samples cannot be read%s", NULL);
					return FALSE;
				}
				for (at = 0; at + unit <= size && !encoder.full; at += unit)
				{
					int values[XBOX_ADPCM_MAXIMUM_CHANNELS];
					int channel;

					if (adpcm)
					{
						short frames[XBOX_ADPCM_BLOCK_SAMPLES * XBOX_ADPCM_MAXIMUM_CHANNELS];
						int frame;

						xbox_adpcm_block_decode(piece + at, channels, frames);
						for (frame = 0; frame < XBOX_ADPCM_BLOCK_SAMPLES; frame++)
						{
							for (channel = 0; channel < channels; channel++)
								values[channel] = frames[frame * channels + channel];
							xbox_adpcm_rate_frame(&rate_state, values);
						}
					}
					else
					{
						for (channel = 0; channel < channels; channel++)
							values[channel] = (int16_t)(uint16_t)(piece[at + 2 * channel] | piece[at + 2 * channel + 1] << 8);
						xbox_adpcm_rate_frame(&rate_state, values);
					}
				}
				offset += size;
			}
			xbox_adpcm_rate_finish(&rate_state);
			xbox_adpcm_encoder_finish(&encoder);
			written = output_bytes;
		}
		else
		{
			/* (a compression the loader left none of: none played) */
			written = 0;
		}
		ce->permutations_transcoded++;
	}
	if (written && written < (unsigned long)OGG_SOUND_ADPCM_BLOCK_BYTES * (unsigned long)channels)
		written = 0;
	if (written && fwrite(output, 1, written, data) != written)
	{
		free(output);
		why_set(why, why_size, "the pack cannot be written%s", NULL);
		return FALSE;
	}
	if (written)
		*data_crc = (unsigned long)crc32((uLong)*data_crc, (Bytef const *)output, (uInt)written);
	if (output)
		free(output);
	permutation.compression = SOUND_COMPRESSION_XBOX_ADPCM;
	permutation.sample_buffer_size = 0;
	permutation.samples.size = (long)written;
	permutation.samples.file_offset = written ? (long)*data_size : 0;
	*data_size += written;
	memcpy(permutation_bytes, &permutation, sizeof(permutation));
	return TRUE;
}

/* ---------- a map's multiplayer unit (the player models' scan) */

/* the bytes of a CE map's tag data at a linked address, read from its file:
FALSE if they are not all within its tag data */
static boolean unit_scan_read(struct ce_file *file, struct cache_file_identity const *identity, unsigned long address,
	unsigned long size, void *buffer)
{
	unsigned long offset = address - CUSTOM_EDITION_TAG_CACHE_ADDRESS;

	if (address < CUSTOM_EDITION_TAG_CACHE_ADDRESS || offset > identity->tag_data_size ||
		size > identity->tag_data_size - offset)
	{
		return FALSE;
	}
	return ce_file_read(file, (uint32_t)(identity->tag_data_offset + offset), (uint32_t)size, buffer) != 0;
}

/* a tag's name (at most size - 1 characters, printable), read from the tag
data: FALSE if it is not there */
static boolean unit_scan_name(struct ce_file *file, struct cache_file_identity const *identity, unsigned long address,
	char *name, long size)
{
	long length;

	if (size < 2)
		return FALSE;
	memset(name, 0, (size_t)size);
	/* (the name may end before size: read what lies in the tag data) */
	for (length = size - 1; length > 0 && !unit_scan_read(file, identity, address, (unsigned long)length, name); length /= 2)
		;
	if (length <= 0)
		return FALSE;
	name[length] = 0;
	for (length = 0; name[length]; length++)
	{
		if ((unsigned char)name[length] < 0x20 || (unsigned char)name[length] >= 0x7F)
			return FALSE;
	}
	return length > 0;
}

boolean scene_pack_ce_multiplayer_unit(
	char const *maps_directory,
	char const *map_name,
	struct scene_pack_ce_unit *unit,
	char *why,
	long why_size)
{
	struct ce_file file;
	struct cache_file_identity identity;
	unsigned char header[0x28];
	unsigned char *instances = NULL;
	unsigned char globals[0x170];
	unsigned char information[0xA0];
	unsigned char biped[0x38];
	unsigned char model[MODEL_ROOT_SIZE];
	char path[512];
	long count, index, unit_index, model_index;
	boolean found = FALSE;

	memset(unit, 0, sizeof(*unit));
	map_path(path, sizeof(path), maps_directory, map_name);
	if (!ce_file_open(&file, path))
	{
		why_set(why, why_size, "it cannot be read%s", NULL);
		return FALSE;
	}
	if (cache_file_identify(&file.source, &identity) != _cache_file_status_ok ||
		identity.format != _cache_file_format_custom_edition_cache)
	{
		ce_file_close(&file);
		why_set(why, why_size, "it is not a Halo Custom Edition map%s", NULL);
		return FALSE;
	}
	unit->file_size = identity.file_size;
	unit->checksum = identity.checksum;
	unit->scenario_type = identity.scenario_type;
	/* (the tag index, then the instances: bounded, every address within
	the tag data) */
	if (identity.tag_data_size < sizeof(header) ||
		!ce_file_read(&file, identity.tag_data_offset, sizeof(header), header) ||
		read_u32(header + 0x24) != 'tags' ||
		(count = (long)read_u32(header + 12)) <= 0 || count > 0x8000 ||
		!(instances = malloc((unsigned long)count * 32)) ||
		!unit_scan_read(&file, &identity, read_u32(header), (unsigned long)count * 32, instances))
	{
		if (instances)
			free(instances);
		ce_file_close(&file);
		why_set(why, why_size, "its tag index is damaged%s", NULL);
		return FALSE;
	}
	for (index = 0; index < count && !found; index++)
	{
		unsigned char *instance = instances + index * 32;
		long information_count;

		if (read_u32(instance) != 'matg' || read_u32(instance + 24))
			continue;
		/* (the globals' multiplayer information, its unit reference) */
		if (!unit_scan_read(&file, &identity, read_u32(instance + 20), sizeof(globals), globals) ||
			(information_count = (long)read_u32(globals + 0x164)) < 1 ||
			!unit_scan_read(&file, &identity, read_u32(globals + 0x168), sizeof(information), information))
		{
			break;
		}
		unit_index = (long)(read_u32(information + 0x10 + 12) & 0xFFFF);
		if (read_u32(information + 0x10 + 12) == (unsigned long)NONE || unit_index >= count ||
			read_u32(instances + unit_index * 32 + 12) != read_u32(information + 0x10 + 12))
		{
			break;
		}
		instance = instances + unit_index * 32;
		unit->group_tag = read_u32(instance);
		unit->tag_index = unit_index;
		if (read_u32(instance + 24) || !unit_scan_name(&file, &identity, read_u32(instance + 16), unit->name, sizeof(unit->name)))
			break;
		/* (its model, and the model's nodes) */
		if (unit->group_tag == 'bipd' && unit_scan_read(&file, &identity, read_u32(instance + 20), sizeof(biped), biped))
		{
			model_index = (long)(read_u32(biped + 0x34) & 0xFFFF);
			if (read_u32(biped + 0x34) != (unsigned long)NONE && model_index < count &&
				read_u32(instances + model_index * 32 + 12) == read_u32(biped + 0x34))
			{
				unsigned char *model_instance = instances + model_index * 32;

				unit_scan_name(&file, &identity, read_u32(model_instance + 16), unit->model_name, sizeof(unit->model_name));
				if (unit_scan_read(&file, &identity, read_u32(model_instance + 20), sizeof(model), model))
					unit->model_nodes = (long)read_u32(model + MODEL_NODES_OFFSET);
			}
		}
		found = TRUE;
	}
	free(instances);
	ce_file_close(&file);
	if (!found)
		why_set(why, why_size, "its globals name no multiplayer unit%s", NULL);
	return found;
}
