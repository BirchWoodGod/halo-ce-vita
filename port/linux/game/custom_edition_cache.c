/*
CUSTOM_EDITION_CACHE.C

Halo Custom Edition and OpenSauce caches in the native builds' cache file
loader (custom_edition_cache.h). The loader runs only Xbox caches of this
build; without this unit it rejects a Custom Edition cache as "an old
version" and never looks for a ".yelo" file at all.

With HALO_CUSTOM_EDITION set (the platform then reserves the Custom Edition
tag cache, port/linux/src/xbox_memory.c), a Custom Edition map is loaded
instead: it is read in place rather than copied to the cache partition, and
its tags go to 0x40440000 through cache_file_formats.c, which also converts
what only needs their bytes changed. custom_edition_bitmaps.c and
custom_edition_geometry.c convert the rest with the game's own functions.
Every read the game makes of the map (structure BSPs, bitmap pixels, sound
samples) is served from the map, bitmaps.map or sounds.map according to
where its offset falls in their combined offset space.

A process that cannot have its tag cache at 0x40440000 (the Vita, whose
user memory starts higher) loads the tags into memory of its own instead:
cache_file_formats.c works on any copy of the tag cache, and once it and
the game's own conversions are done, the tags' pointers are moved to where
the copy is (port/linux/src/tag_relocate.c, with this build's tag layouts,
which the converted tags now have), as is each structure BSP when it is
read.
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "tag_files/files.h"
#include "tag_files/tag_files.h"
#include "cache/cache_files.h"
#include "scenario/scenario_definitions.h"
#include "cache_file_formats.h"
#include "custom_edition_cache.h"
#include "map_share_protocol.h"
#ifdef HALO_RELOCATABLE_TAG_CACHE
#include "tag_relocate.h"
#endif

#include "memory/zlib/zlib.h"

#include <stdlib.h>

/* ---------- constants */

#define MAP_FILE_EXTENSION ".map"
#define OPENSAUCE_MAP_FILE_EXTENSION ".yelo"
/* as the cache file loader's own map paths */
#define MAP_PATH_SIZE 256

/* where bitmaps.map and sounds.map start in the combined offset space; the
largest map is 0x24000000 bytes long (cache_file_formats.h) */
#define COMBINED_BITMAPS_OFFSET 0x40000000UL
#define COMBINED_SOUNDS_OFFSET 0x60000000UL
#define COMBINED_OFFSET_LIMIT 0x80000000UL

/* The renderer write-protects the memory it has made textures of and learns
of changes to it from the faults writes take (port/linux/src/memory_watch.c);
the kernel fails a read into such memory instead of faulting. Reads for the
game therefore land here first and are copied, as the platform's own file
layer does (port/linux/src/xbox_files.c, read_at). */
#define READ_STAGING_BYTES 0x10000

/* the platform's write tracking (port/linux/src/platform.h): told of every
write into guest memory the game's own code does not make, which the Vita,
without page protection, sees in no other way - its renderer keeps converted
textures until their pages are written */
void memory_watch_prepare_write(void *address, unsigned long size);

/* ---------- structures */

struct custom_edition_file
{
	FILE *stream;
	struct cache_file_source source;
};

struct custom_edition_cache_globals
{
	boolean tags_loaded;
	/* the tag cache and the bytes of it the loaded tags use */
	uint8_t *tag_cache;
	uint32_t loaded_bytes;
	struct custom_edition_file map;
	struct custom_edition_file resource_files[NUMBER_OF_RESOURCE_MAP_TYPES];
	struct resource_map resource_map_storage[NUMBER_OF_RESOURCE_MAP_TYPES];
	struct resource_map *resource_maps[NUMBER_OF_RESOURCE_MAP_TYPES];
	/* the port reads from the tick thread (sounds, the textures it
	predicts) and the render thread (textures) at once: the streams and the
	staging buffer are taken under this */
	volatile int read_lock;
	byte read_staging[READ_STAGING_BYTES];
};

/* ---------- globals */

static struct custom_edition_cache_globals custom_edition_cache_globals;

/* ---------- private code */

static int custom_edition_file_read(
	void *context,
	uint32_t offset,
	uint32_t size,
	void *buffer)
{
	FILE *stream = context;

	return fseek(stream, (long)offset, SEEK_SET) == 0 && fread(buffer, 1, size, stream) == size;
}

static boolean custom_edition_file_open(
	struct custom_edition_file *file,
	char const *path)
{
	long size;

	file->stream = fopen(path, "rb");
	if (!file->stream)
	{
		return FALSE;
	}
	if (fseek(file->stream, 0, SEEK_END) != 0 || (size = ftell(file->stream)) < 0)
	{
		fclose(file->stream);
		file->stream = NULL;
		return FALSE;
	}
	file->source.context = file->stream;
	file->source.read = custom_edition_file_read;
	file->source.size = (uint32_t)size;

	return TRUE;
}

static void custom_edition_file_close(
	struct custom_edition_file *file)
{
	if (file->stream)
	{
		fclose(file->stream);
		file->stream = NULL;
	}

	return;
}

static boolean file_path_exists(
	char const *path)
{
	struct file_reference reference;

	return file_exists(file_reference_create_from_path(&reference, path, FALSE));
}

/* the file that holds the map `map_name` names: <maps>\<name>.map, or the
OpenSauce <maps>\<name>.yelo when there is no .map */
static boolean custom_edition_map_path(
	char const *map_name,
	char *path)
{
	char const *directory = cache_files_map_directory();
	char const *name = tag_name_strip_path(map_name);

	if (strlen(directory) + strlen(name) + strlen(OPENSAUCE_MAP_FILE_EXTENSION) >= MAP_PATH_SIZE)
	{
		return FALSE;
	}
	sprintf(path, "%s%s%s", directory, name, MAP_FILE_EXTENSION);
	if (!file_path_exists(path))
	{
		sprintf(path, "%s%s%s", directory, name, OPENSAUCE_MAP_FILE_EXTENSION);
	}

	return file_path_exists(path);
}

/* A cache's resource map of `type`: <maps>\bitmaps.map and so on, or for an
OpenSauce cache built with a mod set, <maps>\data_files\<mod>-bitmaps.map
(the mod set file name is an assumption: docs/custom_edition_caches.md). */
static boolean custom_edition_resource_map_path(
	struct cache_file_identity const *identity,
	enum resource_map_type type,
	char *path)
{
	char const *directory = cache_files_map_directory();
	char const *type_name = resource_map_type_describe(type);

	if (identity->has_opensauce_header &&
		TEST_FLAG(identity->opensauce.flags, _opensauce_cache_uses_mod_data_files_bit))
	{
		if (strlen(directory) + strlen(identity->opensauce.mod_name) + strlen(type_name) + 32 >= MAP_PATH_SIZE)
		{
			return FALSE;
		}
		sprintf(path, "%sdata_files\\%s-%s%s", directory, identity->opensauce.mod_name, type_name, MAP_FILE_EXTENSION);
	}
	else
	{
		sprintf(path, "%s%s%s", directory, type_name, MAP_FILE_EXTENSION);
	}

	return TRUE;
}

static void custom_edition_cache_files_close(
	void)
{
	struct custom_edition_cache_globals *globals = &custom_edition_cache_globals;
	short type;

	for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
	{
		if (globals->resource_maps[type])
		{
			resource_map_close(globals->resource_maps[type]);
			globals->resource_maps[type] = NULL;
		}
		custom_edition_file_close(&globals->resource_files[type]);
	}
	custom_edition_file_close(&globals->map);

	return;
}

/* Converts the loaded map's models for this build from its model data,
which custom_edition_models_convert reads a part at a time. */
static boolean custom_edition_cache_models_convert(
	uint8_t *tag_cache,
	struct custom_edition_load_report const *report)
{
	return custom_edition_models_convert(
		tag_cache,
		report->tag_data_bytes + report->resource_tag_bytes,
		report);
}

/* Makes the tags custom_edition_cache_load loaded into `tag_cache` this
build's: their resource offsets combined, their bytes converted, their
bitmaps checked, their models converted. */
static boolean custom_edition_cache_tags_convert(
	uint8_t *tag_cache,
	uint32_t tag_cache_bytes,
	struct custom_edition_load_report const *report)
{
	uint32_t loaded_bytes = report->tag_data_bytes + report->resource_tag_bytes;
	struct custom_edition_conversion_report conversion;
	enum cache_file_status status;

	custom_edition_cache_combine_resource_offsets(
		tag_cache,
		loaded_bytes,
		COMBINED_BITMAPS_OFFSET,
		COMBINED_SOUNDS_OFFSET);
	status = custom_edition_cache_convert(tag_cache, loaded_bytes, &conversion);
	if (status != _cache_file_status_ok)
	{
		error(
			_error_silent,
			"custom edition: cannot convert '%s': %s",
			custom_edition_cache_tag_name(tag_cache, loaded_bytes, conversion.problem_tag_index),
			cache_file_status_describe(status));
		return FALSE;
	}
	error(
		_error_silent,
		"custom edition: %ld shaders renumbered, %ld transparent chicago extended shaders made transparent chicago shaders, %ld bitmaps prepared%s",
		(long)conversion.shaders_retyped,
		(long)conversion.chicago_extended_shaders,
		(long)conversion.bitmaps_prepared,
		conversion.script_nodes_reduced ? ", OpenSauce's script nodes made this build's number" : "");
	if (conversion.animation_overlays_disabled)
	{
		error(
			_error_silent,
			"custom edition: %ld animation overlays named animations their graphs do not have and were disabled",
			(long)conversion.animation_overlays_disabled);
	}
	if (conversion.sounds_undecodable)
	{
		error(
			_error_silent,
			"custom edition: %ld sounds use a compression this build cannot decode (Custom Edition's Ogg Vorbis) and will not play",
			(long)conversion.sounds_undecodable);
	}
	if (conversion.hud_placements_rescaled)
	{
		error(
			_error_silent,
			"custom edition: %ld HUD elements drawn from Halo PC's double resolution bitmaps were given half their scale",
			(long)conversion.hud_placements_rescaled);
	}
	if (conversion.score_hint_converted)
	{
		error(_error_silent, "custom edition: the multiplayer score hint names the BACK button where Halo PC names a key");
	}

#ifdef HALO_RELOCATABLE_TAG_CACHE
	/* The tags, in this build's layouts now but for the models (whose
	Custom Edition layout the walk knows too), moved to where they are
	before the game's own code below reads them through their pointers */
	if ((uint32_t)(unsigned long)tag_cache != custom_edition_cache_linked_address())
	{
		if (!halo_tag_relocate_linked_tags(tag_cache, loaded_bytes, custom_edition_cache_linked_address(), tag_cache_bytes))
		{
			error(_error_silent, "custom edition: out of memory moving the tags");
			return FALSE;
		}
		custom_edition_cache_tags_moved((uint32_t)(unsigned long)tag_cache);
		error(
			_error_silent,
			"custom edition: tags moved from 0x%08lX to %p",
			(unsigned long)custom_edition_cache_linked_address(),
			tag_cache);
	}
#else
	(void)tag_cache_bytes;
#endif

	return custom_edition_bitmaps_verify(tag_cache, loaded_bytes) &&
		custom_edition_reordered_bitmaps_find(tag_cache, loaded_bytes) &&
		custom_edition_scripts_convert(tag_cache, loaded_bytes) &&
		custom_edition_cache_models_convert(tag_cache, report);
}

/* Whether Custom Edition maps may run (game.custom_edition) and the map
`map_name` names is a Custom Edition cache, whose header is then described
in `identity`. */
static boolean custom_edition_cache_identify(
	char const *map_name,
	struct cache_file_identity *identity)
{
	char path[MAP_PATH_SIZE];
	struct custom_edition_file file;
	boolean identified = FALSE;

	if (!halo_custom_edition_enabled() ||
		!custom_edition_map_path(map_name, path) ||
		!custom_edition_file_open(&file, path))
	{
		return FALSE;
	}
	if (cache_file_identify(&file.source, identity) == _cache_file_status_ok &&
		identity->format == _cache_file_format_custom_edition_cache)
	{
		identified = TRUE;
	}
	custom_edition_file_close(&file);

	return identified;
}

static void custom_edition_cache_report_log(
	struct custom_edition_load_report const *report)
{
	error(
		_error_silent,
		"custom edition: %ld tags, 0x%lX bytes of tag data and 0x%lX of tags from resource maps (bitmaps %ld, sounds %ld, loc %ld)",
		(long)report->tag_count,
		(unsigned long)report->tag_data_bytes,
		(unsigned long)report->resource_tag_bytes,
		(long)report->resource_tag_counts[_resource_map_bitmaps],
		(long)report->resource_tag_counts[_resource_map_sounds],
		(long)report->resource_tag_counts[_resource_map_locale]);
	error(
		_error_silent,
		"custom edition: %ld structure BSPs (%ld materials), %ld bitmap and %ld sound ranges checked, %ld pointers relocated, checksum %s",
		(long)report->structure_bsp_count,
		(long)report->structure_bsp_materials_checked,
		(long)report->bitmap_data_ranges_checked,
		(long)report->sound_sample_ranges_checked,
		(long)report->relocated_pointer_count,
		TEST_FLAG(report->warnings, _custom_edition_warning_checksum_mismatch_bit) ? "mismatched" : "matched");

	return;
}

/* The last Custom Edition map loaded, and why it could not be, for the
player (custom_edition_cache_load_failure_show): a map that cannot be loaded
for want of memory, a resource map or anything else takes the player back
to the menu with this, rather than stopping the game. */
static struct
{
	char map_name[MAP_PATH_SIZE];
	char reason[160];
	boolean failed;
} custom_edition_load_failure;

static void custom_edition_cache_load_failure_begin(
	char const *map_name)
{
	csstrncpy(custom_edition_load_failure.map_name, tag_name_strip_path(map_name), MAP_PATH_SIZE - 1);
	custom_edition_load_failure.map_name[MAP_PATH_SIZE - 1] = 0;
	custom_edition_load_failure.reason[0] = 0;
	custom_edition_load_failure.failed = FALSE;

	return;
}

/* the player's reason for a load's failure (the first one given stands: the
most precise, a generic one following it) */
static void custom_edition_cache_load_failure_reason(
	char const *reason)
{
	custom_edition_load_failure.failed = TRUE;
	if (!custom_edition_load_failure.reason[0])
	{
		csstrncpy(custom_edition_load_failure.reason, reason, sizeof(custom_edition_load_failure.reason) - 1);
		custom_edition_load_failure.reason[sizeof(custom_edition_load_failure.reason) - 1] = 0;
	}

	return;
}

/* the reason for a failed custom_edition_cache_load, by its status */
static char const *custom_edition_cache_load_status_reason(
	enum cache_file_status status,
	struct custom_edition_load_report const *report)
{
	switch (status)
	{
	case _cache_file_status_out_of_memory:
		return "there is not enough memory for it";
	case _cache_file_status_read_failed:
		return "its file could not be read";
	case _cache_file_status_missing_resource_map:
	case _cache_file_status_missing_resource_item:
		return "it needs Halo Custom Edition's bitmaps.map, sounds.map and loc.map in the maps folder";
	case _cache_file_status_bad_scenario_tag:
		(void)report;
		return "it has no scenario this port can find (a protected or damaged map)";
	default:
		return cache_file_status_describe(status);
	}
}

/* the C heap (on the Vita newlib's fixed 48 MB, which the system's
libraries share; 0: no fixed size) and the memory window, for a map that
runs out of either */
static void custom_edition_cache_heap_log(
	char const *when)
{
	unsigned long in_use;
	unsigned long capacity;
	unsigned long window_used;
	unsigned long window_free;

	platform_heap_usage(&in_use, &capacity);
	platform_contiguous_usage(&window_used, &window_free);
	error(
		_error_silent,
		"custom edition: %s: C heap %lu KB in use (of %lu KB), memory window %lu KB in use, %lu KB free",
		when,
		in_use / 1024,
		capacity / 1024,
		window_used / 1024,
		window_free / 1024);

	return;
}

/* ---------- public code */

boolean custom_edition_cache_refuse(
	void const *header,
	char const *build,
	char const *scenario_name,
	boolean fatal)
{
	int has_opensauce_header;

	if (cache_file_header_format(header, &has_opensauce_header) != _cache_file_format_custom_edition_cache)
	{
		return FALSE;
	}

	/* temporary holds 256 characters: bound the name and build strings */
	csprintf(
		temporary,
		"'%.96s' is a Halo Custom Edition cache%s (build %.31s): this build recognizes it but cannot run it (docs/custom_edition_caches.md)",
		scenario_name,
		has_opensauce_header ? " with an OpenSauce header" : "",
		build);
	error(_error_silent, "%s", temporary);
	if (fatal)
	{
		vassert(FALSE, temporary);
	}

	return TRUE;
}

void opensauce_cache_path_find(
	char *path,
	long path_size)
{
	long stem_length = (long)strlen(path) - (long)strlen(MAP_FILE_EXTENSION);
	struct file_reference reference;

	/* OpenSauce looks for the .map first, then the .yelo
	(cache_files_yelo.cpp, c_map_file_finder::SearchPath) */
	if (stem_length < 0 ||
		strcmp(path + stem_length, MAP_FILE_EXTENSION) ||
		stem_length + (long)strlen(OPENSAUCE_MAP_FILE_EXTENSION) >= path_size ||
		file_exists(file_reference_create_from_path(&reference, path, FALSE)))
	{
		return;
	}

	strcpy(path + stem_length, OPENSAUCE_MAP_FILE_EXTENSION);
	if (!file_exists(file_reference_create_from_path(&reference, path, FALSE)))
	{
		strcpy(path + stem_length, MAP_FILE_EXTENSION);
	}

	return;
}

boolean custom_edition_cache_playable(
	char const *map_name)
{
	struct cache_file_identity identity;

	return custom_edition_cache_identify(map_name, &identity);
}

boolean custom_edition_cache_xbox_multiplayer(
	char const *map_name)
{
	/* the cache partition's room for a multiplayer map
	(cache_files_windows.c, MULTIPLAYER_CACHE_FILE_MAXIMUM_SIZE), which its
	decompressed length must stay below */
	enum { MULTIPLAYER_CACHE_FILE_MAXIMUM_SIZE = 0x02F00000 };
	char const *name = tag_name_strip_path(map_name);
	char path[MAP_PATH_SIZE];
	struct custom_edition_file file;
	struct cache_file_identity identity;
	boolean multiplayer = FALSE;

	if (strlen(cache_files_map_directory()) + strlen(name) + strlen(MAP_FILE_EXTENSION) >= MAP_PATH_SIZE)
	{
		return FALSE;
	}
	sprintf(path, "%s%s%s", cache_files_map_directory(), name, MAP_FILE_EXTENSION);
	if (!custom_edition_file_open(&file, path))
	{
		return FALSE;
	}
	if (cache_file_identify(&file.source, &identity) == _cache_file_status_ok &&
		identity.format == _cache_file_format_xbox_cache &&
		identity.scenario_type == _scenario_type_multiplayer)
	{
		/* the cache partition knows a copied map by the name in its header
		(cache_files_windows.c, cached_map_files_find_map) */
		if (csstrcasecmp(identity.name, name))
		{
			error(_error_silent, "custom maps: '%s' is named '%s' inside: rename the file to %s.map", path, identity.name, identity.name);
		}
		else if (identity.file_length >= MULTIPLAYER_CACHE_FILE_MAXIMUM_SIZE)
		{
			error(
				_error_silent,
				"custom maps: '%s' is 0x%lX bytes decompressed, more than the Xbox's multiplayer maps may be (0x%X)",
				path,
				(unsigned long)identity.file_length,
				MULTIPLAYER_CACHE_FILE_MAXIMUM_SIZE);
		}
		else
		{
			multiplayer = TRUE;
		}
	}
	custom_edition_file_close(&file);

	return multiplayer;
}

/* the identities custom_edition_cache_map_identity worked out, by file
name and size: the CRC of a map without a checksum reads the whole file */
enum { REMEMBERED_IDENTITIES = 8 };
static struct
{
	char name[CACHE_FILE_STRING_BYTES];
	uint32_t size;
	unsigned long identity;
} remembered_identities[REMEMBERED_IDENTITIES];

boolean custom_edition_cache_map_file_path(
	char const *map_name,
	char *path,
	long path_size)
{
	char found[MAP_PATH_SIZE];

	if (!custom_edition_map_path(map_name, found) || (long)strlen(found) >= path_size)
	{
		return FALSE;
	}
	strcpy(path, found);

	return TRUE;
}

void custom_edition_cache_map_identity_forget(
	char const *map_name)
{
	char const *name = tag_name_strip_path(map_name);
	short index;

	for (index = 0; index < REMEMBERED_IDENTITIES; index++)
	{
		if (!csstrcasecmp(remembered_identities[index].name, name))
		{
			remembered_identities[index].identity = 0;
			remembered_identities[index].name[0] = 0;
		}
	}

	return;
}

unsigned long custom_edition_cache_map_identity(
	char const *map_name)
{
	static short next_remembered;
	char const *name = tag_name_strip_path(map_name);
	char path[MAP_PATH_SIZE];
	struct custom_edition_file file;
	struct cache_file_identity identity;
	unsigned long result = 0;
	short index;

	if (strlen(name) >= CACHE_FILE_STRING_BYTES ||
		!custom_edition_map_path(name, path) ||
		!custom_edition_file_open(&file, path))
	{
		return 0;
	}
	for (index = 0; index < REMEMBERED_IDENTITIES; index++)
	{
		if (remembered_identities[index].identity &&
			remembered_identities[index].size == file.source.size &&
			!csstrcasecmp(remembered_identities[index].name, name))
		{
			custom_edition_file_close(&file);
			return remembered_identities[index].identity;
		}
	}
	if (cache_file_identify(&file.source, &identity) == _cache_file_status_ok &&
		(identity.format == _cache_file_format_xbox_cache || identity.format == _cache_file_format_custom_edition_cache))
	{
		uLong crc = 0;

		/* (map_share_identity: the header's checksum with the file's
		length, or for a header without one, Invader's Xbox maps, the CRC-32
		of the whole file; map sharing works out the same of a download,
		map_share_protocol.c) */
		if (!identity.checksum || identity.checksum == 0xFFFFFFFFUL)
		{
			byte buffer[READ_STAGING_BYTES];
			uint32_t offset;

			crc = crc32(0L, Z_NULL, 0);
			for (offset = 0; offset < file.source.size; offset += READ_STAGING_BYTES)
			{
				uint32_t chunk = MIN(file.source.size - offset, READ_STAGING_BYTES);

				if (!file.source.read(file.source.context, offset, chunk, buffer))
				{
					crc = 0;
					break;
				}
				crc = crc32(crc, buffer, chunk);
			}
		}
		result = map_share_identity((uint32_t)identity.checksum, file.source.size, (uint32_t)crc);
		csstrncpy(remembered_identities[next_remembered].name, name, CACHE_FILE_STRING_BYTES - 1);
		remembered_identities[next_remembered].size = file.source.size;
		remembered_identities[next_remembered].identity = result;
		next_remembered = (short)((next_remembered + 1) % REMEMBERED_IDENTITIES);
	}
	custom_edition_file_close(&file);

	return result;
}

boolean custom_edition_cache_multiplayer(
	char const *map_name)
{
	struct cache_file_identity identity;

	return custom_edition_cache_identify(map_name, &identity) &&
		identity.scenario_type == _scenario_type_multiplayer;
}

struct cache_file_tag_header *custom_edition_cache_tags_load(
	char const *map_name,
	void *header)
{
	struct custom_edition_cache_globals *globals = &custom_edition_cache_globals;
	uint8_t *tag_cache;
	uint32_t tag_cache_bytes;
	struct custom_edition_load_report report;
	struct cache_file_identity identity;
	char path[MAP_PATH_SIZE];
	enum cache_file_status status;
	short type;

	assert(!globals->tags_loaded);
	custom_edition_cache_load_failure_begin(map_name);
	if (!custom_edition_map_path(map_name, path) || !custom_edition_file_open(&globals->map, path))
	{
		error(_error_silent, "custom edition: cannot open the map '%s'", map_name);
		custom_edition_cache_load_failure_reason("its file could not be opened");
		return NULL;
	}
	status = cache_file_identify(&globals->map.source, &identity);
	if (status != _cache_file_status_ok ||
		identity.format != _cache_file_format_custom_edition_cache ||
		identity.file_size > COMBINED_BITMAPS_OFFSET ||
		!globals->map.source.read(globals->map.source.context, 0, CACHE_FILE_HEADER_BYTES, header))
	{
		error(_error_silent, "custom edition: '%s' is not a loadable cache (%s)", path, cache_file_status_describe(status));
		custom_edition_cache_load_failure_reason(cache_file_status_describe(status));
		custom_edition_cache_files_close();
		return NULL;
	}
	error(_error_silent, "custom edition: loading '%s' (build %s%s)",
		path,
		identity.build,
		identity.has_opensauce_header ? ", OpenSauce" : "");
	custom_edition_cache_heap_log("before loading");
	/* the tag cache the map's tags are linked to the start of: at
	0x40440000 when the platform has it there, else elsewhere and moved
	below */
	tag_cache_bytes = custom_edition_tag_cache_bytes(&identity);
	tag_cache = halo_custom_edition_tag_cache_acquire(tag_cache_bytes);
	if (!tag_cache)
	{
		error(_error_silent, "custom edition: no room for the 0x%lX byte tag cache of '%s'", (unsigned long)tag_cache_bytes, path);
		custom_edition_cache_load_failure_reason("there is not enough memory for its tags");
		custom_edition_cache_files_close();
		return NULL;
	}
#ifndef HALO_RELOCATABLE_TAG_CACHE
	if ((uint32_t)(unsigned long)tag_cache != custom_edition_cache_linked_address())
	{
		error(_error_silent, "custom edition: this build cannot move the tags of '%s' off 0x40440000", path);
		custom_edition_cache_load_failure_reason("this build cannot move its tags");
		halo_custom_edition_tag_cache_release();
		custom_edition_cache_files_close();
		return NULL;
	}
#endif

	for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
	{
		char resource_path[MAP_PATH_SIZE];

		if (!custom_edition_resource_map_path(&identity, (enum resource_map_type)type, resource_path) ||
			!custom_edition_file_open(&globals->resource_files[type], resource_path))
		{
			error(_error_silent, "custom edition: no resource map '%s'", resource_path);
			continue;
		}
		status = resource_map_open(
			&globals->resource_files[type].source,
			(enum resource_map_type)type,
			&globals->resource_map_storage[type]);
		if (status != _cache_file_status_ok)
		{
			error(_error_silent, "custom edition: resource map '%s': %s", resource_path, cache_file_status_describe(status));
			continue;
		}
		globals->resource_maps[type] = &globals->resource_map_storage[type];
	}

	/* the combined offset space serves bitmaps.map and sounds.map after the
	map itself */
	if ((globals->resource_files[_resource_map_bitmaps].stream &&
		globals->resource_files[_resource_map_bitmaps].source.size > COMBINED_SOUNDS_OFFSET - COMBINED_BITMAPS_OFFSET) ||
		(globals->resource_files[_resource_map_sounds].stream &&
		globals->resource_files[_resource_map_sounds].source.size > COMBINED_OFFSET_LIMIT - COMBINED_SOUNDS_OFFSET))
	{
		error(_error_silent, "custom edition: a resource map is too large for this loader");
		custom_edition_cache_load_failure_reason("a resource map in the maps folder is too large");
		custom_edition_cache_files_close();
		return NULL;
	}

	status = custom_edition_cache_load(
		&globals->map.source,
		globals->resource_maps,
		tag_cache,
		tag_cache_bytes,
		&report);
	if (status != _cache_file_status_ok)
	{
		error(
			_error_silent,
			"custom edition: cannot load '%s': %s (tag %ld, 0x%08lX)",
			path,
			cache_file_status_describe(status),
			(long)report.problem_tag_index,
			(unsigned long)report.problem_location);
		custom_edition_cache_load_failure_reason(custom_edition_cache_load_status_reason(status, &report));
		halo_custom_edition_tag_cache_release();
		custom_edition_cache_files_close();
		return NULL;
	}
	custom_edition_cache_report_log(&report);
	if (!custom_edition_cache_tags_convert(tag_cache, tag_cache_bytes, &report))
	{
		error(_error_silent, "custom edition: cannot run '%s'", path);
		custom_edition_cache_load_failure_reason("its tags could not be converted for this port");
		custom_edition_models_dispose();
		custom_edition_bitmaps_dispose();
#ifdef HALO_RELOCATABLE_TAG_CACHE
		halo_tag_relocate_linked_release();
#endif
		custom_edition_cache_tags_moved(custom_edition_cache_linked_address());
		halo_custom_edition_tag_cache_release();
		custom_edition_cache_files_close();
		return NULL;
	}
	globals->tag_cache = tag_cache;
	globals->loaded_bytes = report.tag_data_bytes + report.resource_tag_bytes;
	globals->tags_loaded = TRUE;
	custom_edition_cache_heap_log("loaded");

	return (struct cache_file_tag_header *)tag_cache;
}

void custom_edition_cache_load_failure_note(
	char const *reason)
{
	custom_edition_cache_load_failure_reason(reason);

	return;
}

boolean custom_edition_cache_load_failure_show(
	char const *map_name)
{
	void platform_show_message(char const *title, char const *message);
	char message[320];

	if (!custom_edition_load_failure.failed ||
		csstrcasecmp(custom_edition_load_failure.map_name, tag_name_strip_path(map_name)))
	{
		return FALSE;
	}
	snprintf(
		message,
		sizeof(message),
		"The custom map %s could not be loaded: %s.",
		custom_edition_load_failure.map_name,
		custom_edition_load_failure.reason[0] ? custom_edition_load_failure.reason : "see debug.txt");
	error(_error_silent, "custom edition: %s", message);
	platform_show_message("Halo: custom map", message);
	custom_edition_load_failure.failed = FALSE;

	return TRUE;
}

boolean custom_edition_cache_model_data_read(
	struct custom_edition_load_report const *report,
	unsigned long offset,
	unsigned long size,
	void *buffer)
{
	struct custom_edition_file const *map = &custom_edition_cache_globals.map;

	return map->stream &&
		offset <= report->model_data_bytes &&
		size <= report->model_data_bytes - offset &&
		map->source.read(map->source.context, report->model_data_offset + (uint32_t)offset, (uint32_t)size, buffer);
}

boolean custom_edition_cache_tags_loaded(
	void)
{
	return custom_edition_cache_globals.tags_loaded;
}

void custom_edition_cache_tags_unload(
	void)
{
	/* the structure BSP goes first, as scenario_structure_bsp_unload would
	have released it */
	custom_edition_structure_bsp_unload();
	custom_edition_models_dispose();
	custom_edition_bitmaps_dispose();
	custom_edition_cache_files_close();
	custom_edition_cache_globals.tags_loaded = FALSE;
	custom_edition_cache_globals.tag_cache = NULL;
	custom_edition_cache_globals.loaded_bytes = 0;
#ifdef HALO_RELOCATABLE_TAG_CACHE
	halo_tag_relocate_linked_release();
#endif
	custom_edition_cache_tags_moved(custom_edition_cache_linked_address());
	halo_custom_edition_tag_cache_release();

	return;
}

void custom_edition_cache_structure_bsp_moved(
	void *structure_bsp,
	long size)
{
#ifdef HALO_RELOCATABLE_TAG_CACHE
	struct custom_edition_cache_globals *globals = &custom_edition_cache_globals;

	if (globals->tags_loaded &&
		(uint32_t)(unsigned long)globals->tag_cache != custom_edition_cache_linked_address())
	{
		halo_tag_relocate_linked_structure_bsp(globals->tag_cache, structure_bsp, (unsigned long)size);
	}
#endif

	return;
}

void custom_edition_cache_read(
	long tag_index,
	long offset,
	long size,
	void *buffer)
{
	struct custom_edition_cache_globals *globals = &custom_edition_cache_globals;
	struct custom_edition_file *file;
	unsigned long file_offset;
	long read_bytes;
	boolean read;

	if ((unsigned long)offset >= COMBINED_SOUNDS_OFFSET)
	{
		file = &globals->resource_files[_resource_map_sounds];
		file_offset = (unsigned long)offset - COMBINED_SOUNDS_OFFSET;
	}
	else if ((unsigned long)offset >= COMBINED_BITMAPS_OFFSET)
	{
		file = &globals->resource_files[_resource_map_bitmaps];
		file_offset = (unsigned long)offset - COMBINED_BITMAPS_OFFSET;
	}
	else
	{
		file = &globals->map;
		file_offset = (unsigned long)offset;
	}

	/* (January read its map from its main thread only; the port's tick
	and render threads both read: read_lock) */
	while (__atomic_exchange_n(&globals->read_lock, 1, __ATOMIC_ACQUIRE))
	{
		SwitchToThread();
	}
	read = file->stream && size >= 0;
	if (size > 0)
	{
		memory_watch_prepare_write(buffer, (unsigned long)size);
	}
	for (read_bytes = 0; read && read_bytes < size; read_bytes += READ_STAGING_BYTES)
	{
		long chunk_bytes = MIN(size - read_bytes, READ_STAGING_BYTES);

		read = file->source.read(file->source.context, file_offset + read_bytes, (uint32_t)chunk_bytes, globals->read_staging);
		if (read)
		{
			csmemcpy((byte *)buffer + read_bytes, globals->read_staging, chunk_bytes);
		}
	}
	if (!read)
	{
		/* the loader checked every range the tags give, so this is an I/O
		failure: the reader gets zeros rather than whatever was there */
		error(_error_silent, "custom edition: cannot read 0x%lX bytes at 0x%08lX", (unsigned long)size, (unsigned long)offset);
		if (size > 0)
		{
			csmemset(buffer, 0, size);
		}
	}
	else if (tag_index != NONE)
	{
		custom_edition_bitmap_pixels_arrived(globals->tag_cache, globals->loaded_bytes, tag_index, offset, buffer);
	}
	__atomic_store_n(&globals->read_lock, 0, __ATOMIC_RELEASE);

	return;
}
