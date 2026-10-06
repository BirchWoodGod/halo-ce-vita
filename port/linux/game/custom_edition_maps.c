/*
CUSTOM_EDITION_MAPS.C

The Halo Custom Edition maps in the multiplayer menus
(custom_edition_maps.h).

The maps are the Custom Edition caches of multiplayer scenarios in the maps
folder, OpenSauce's ".yelo" maps among them (custom_edition_cache_multiplayer),
looked for whenever the level list opens. A map is offered under its file's
name, as the level levels\test\<name>\<name> as the Xbox levels are named:
the cache file loader finds a map by the last part of its level name. The
game engine keeps a level name in 64 characters, so a map whose name is
longer than 25 characters is left out, and so is a map named as one of the
Xbox levels, which that level already offers.

A map's picture is the Windows bitmap <name>.bmp beside it, when there is one
(bmp_files.c): the middle of it with the shape of the menus' level pictures,
in a texture of its own that is drawn over the whole picture widget. A map
without one shows the unknown level's picture. A picture is read the first
time it is drawn, and let go when the maps are looked for again. A map's
description is the text file <name>.txt beside it, when there is one, with
its lines as they are written, as the Xbox levels' descriptions are written
in lines of about 20 characters.
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "tag_files/files.h"
#include "tag_files/tag_files.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmaps_internal.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "bmp_files.h"
#include "custom_edition_cache.h"
#include "custom_edition_maps.h"

#include <stdio.h>
#include <stdlib.h>

/* ---------- constants */

#define MAXIMUM_CUSTOM_EDITION_MAPS 128
/* room for the level list's Xbox levels
(ui_widget_event_handler_functions.c offers 13) */
#define MAXIMUM_XBOX_LEVELS 16

/* levels\test\<name>\<name> in the 63 characters the game engine's stage
keeps of a level name (game_engine.c, struct game_engine_stage) */
#define LEVEL_NAME_FORMAT "levels\\test\\%s\\%s"
#define MAXIMUM_MAP_NAME_LENGTH 25

/* The maps' display indices: beyond every string and frame of the menus'
tags (the level names are 15 strings, the level pictures 14 frames). */
#define FIRST_DISPLAY_INDEX 0x4000

/* the level pictures, their shape (the level list's are 140 by 114 of the
menus' 640 by 480, the lobby's 139 by 113) and their frame of an unknown
level (ui_widget_game_data_input_functions.c) */
#define LEVEL_PICTURES_TAG_NAME "ui\\shell\\bitmaps\\mp_map_grafix"
#define LEVEL_PICTURE_SHAPE_WIDTH 140
#define LEVEL_PICTURE_SHAPE_HEIGHT 114
#define UNKNOWN_LEVEL_FRAME 13

#define PICTURE_EXTENSION ".bmp"
/* a 4K screenshot is 25 MB as a bmp file */
#define MAXIMUM_PICTURE_FILE_BYTES 0x04000000L
/* the width and height of a picture's texture: about twice the menus'
level pictures */
#define PICTURE_TEXTURE_SIZE 256

#define DESCRIPTION_EXTENSION ".txt"
/* the bytes of a description file read, and the characters of it kept: the
menus' descriptions are a few short lines */
#define MAXIMUM_DESCRIPTION_FILE_BYTES 1024
#define MAXIMUM_DESCRIPTION_LENGTH 127

/* bitmaps.c's format of 32-bit color with alpha */
enum
{
	_bitmap_format_a8r8g8b8 = 11,
};

/* ---------- structures */

struct custom_edition_map
{
	/* the name of its file, without the extension */
	char name[MAXIMUM_MAP_NAME_LENGTH + 1];
	/* saved_game_file_remember_last_used_multiplayer_map writes
	MAXIMUM_FILENAME_LENGTH+1 characters of a level name */
	char level_name[MAXIMUM_FILENAME_LENGTH + 1];
	wchar_t display_name[MAXIMUM_MAP_NAME_LENGTH + 1];
	wchar_t description[MAXIMUM_DESCRIPTION_LENGTH + 1];
	boolean picture_read;
	struct bitmap_data *picture;
	/* an Xbox cache (a modded or newly built Xbox map), not a Custom Edition
	one */
	boolean xbox_cache;
};

struct custom_edition_maps_globals
{
	boolean looked_for;
	short map_count;
	struct custom_edition_map maps[MAXIMUM_CUSTOM_EDITION_MAPS];
	/* the latest level list: its Xbox levels, then the maps' level names */
	short xbox_level_count;
	char *levels[MAXIMUM_XBOX_LEVELS + MAXIMUM_CUSTOM_EDITION_MAPS];
};

/* ---------- globals */

static struct custom_edition_maps_globals custom_edition_maps_globals;

/* the description of a map without a description file, in the manner of
the Xbox levels' */
static wchar_t const default_description[] = L"Halo Custom\r\nEdition map";
static wchar_t const default_xbox_description[] = L"Custom map";

/* the Xbox's own levels, whose copies differ by region (the PAL and NTSC
maps play together: port/linux/game/pal_tags.c), so they are never
compared and never custom maps: the multiplayer levels, and the campaign's
(network co-op plays them: port/linux/game/network_coop.c) */
static char const *const xbox_level_names[] =
{
	"beavercreek", "sidewinder", "damnation", "ratrace", "prisoner", "hangemhigh", "chillout",
	"carousel", "boardingaction", "bloodgulch", "wizard", "putput", "longest",
	"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
};

/* ---------- private code */

static boolean xbox_level_stock(
	char const *name)
{
	short index;

	for (index = 0; index < NUMBEROF(xbox_level_names); index++)
	{
		if (!csstrcasecmp(name, xbox_level_names[index]))
		{
			return TRUE;
		}
	}

	return FALSE;
}

static void custom_edition_maps_forget(
	void)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	short map_index;

	for (map_index = 0; map_index < globals->map_count; map_index++)
	{
		bitmap_delete(globals->maps[map_index].picture);
	}
	globals->map_count = 0;

	return;
}

static boolean xbox_level_named(
	char const *name)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	short level_index;

	/* (also before a level list was made: a client that joins never makes
	one) */
	if (xbox_level_stock(name))
	{
		return TRUE;
	}
	for (level_index = 0; level_index < globals->xbox_level_count; level_index++)
	{
		if (!csstrcasecmp(tag_name_strip_path(globals->levels[level_index]), name))
		{
			return TRUE;
		}
	}

	return FALSE;
}

/* Whether the player turned the map `name` off (the Vita settings panel's
Modded maps page): HALO_MAPS_DISABLED holds the names of those maps, commas
between them. An off map stays in the folder but is not in the level list. */
#define LOWER_CASE(c) ((c) >= 'A' && (c) <= 'Z' ? (c) - 'A' + 'a' : (c))

static boolean custom_edition_map_disabled(
	char const *name)
{
	char const *list = getenv("HALO_MAPS_DISABLED");

	while (list && *list)
	{
		short index = 0;

		/* (each name of the list against `name`, letters of either case) */
		while (name[index] && list[index] && list[index] != ',' &&
			LOWER_CASE(name[index]) == LOWER_CASE(list[index]))
		{
			index++;
		}
		if (!name[index] && (!list[index] || list[index] == ','))
		{
			return TRUE;
		}
		while (*list && *list != ',')
		{
			list++;
		}
		if (*list == ',')
		{
			list++;
		}
	}

	return FALSE;
}

/* the file name as the menus show it: "beavercreek_halo3" as
"Beavercreek Halo3" */
static void display_name_make(
	char const *name,
	wchar_t *display_name)
{
	boolean word_start = TRUE;
	short index;

	for (index = 0; name[index]; index++)
	{
		unsigned char character = (unsigned char)name[index];

		if (character == '_')
		{
			character = ' ';
		}
		else if (word_start && character >= 'a' && character <= 'z')
		{
			character = (unsigned char)(character - 'a' + 'A');
		}
		word_start = character == ' ';
		display_name[index] = (wchar_t)character;
	}
	display_name[index] = 0;

	return;
}

/* Reads the description of `map` from the text file <name>.txt beside it:
its lines, ended as the menus' strings end theirs, with tabs as spaces, each
character beyond printable ASCII as a '?', and at most
MAXIMUM_DESCRIPTION_LENGTH characters. A map without one, or with an empty
one, has the default description. */
static void custom_edition_map_description_read(
	struct custom_edition_map *map)
{
	char path[MAXIMUM_FILENAME_LENGTH + 1];
	byte text[MAXIMUM_DESCRIPTION_FILE_BYTES];
	FILE *stream;
	long text_size = 0;
	long text_index = 0;
	short length = 0;

	csprintf(path, "%s%s%s", cache_files_map_directory(), map->name, DESCRIPTION_EXTENSION);
	stream = fopen(path, "rb");
	if (stream)
	{
		text_size = (long)fread(text, 1, sizeof(text), stream);
		fclose(stream);
	}
	/* the byte order mark of UTF-8 text */
	if (text_size >= 3 && text[0] == 0xEF && text[1] == 0xBB && text[2] == 0xBF)
	{
		text_index = 3;
	}
	for (; text_index < text_size && length < MAXIMUM_DESCRIPTION_LENGTH; text_index++)
	{
		byte character = text[text_index];

		if (character == '\n')
		{
			if (length + 2 > MAXIMUM_DESCRIPTION_LENGTH)
			{
				break;
			}
			map->description[length++] = '\r';
			map->description[length++] = '\n';
		}
		else if (character == '\t')
		{
			map->description[length++] = ' ';
		}
		else if (character >= ' ' && character < 0x7F)
		{
			map->description[length++] = character;
		}
		else if (character >= 0xC0)
		{
			/* the first byte of a character UTF-8 writes in several */
			map->description[length++] = '?';
		}
	}
	while (length > 0 &&
		(map->description[length - 1] == ' ' ||
			map->description[length - 1] == '\r' ||
			map->description[length - 1] == '\n'))
	{
		length--;
	}
	map->description[length] = 0;

	if (!length && map->xbox_cache)
	{
		csmemcpy(map->description, default_xbox_description, sizeof(default_xbox_description));
	}
	else if (!length)
	{
		csmemcpy(map->description, default_description, sizeof(default_description));
	}

	return;
}

/* Adds the map the file `name`.`extension` of the maps folder holds, when it
is a Custom Edition multiplayer map not added yet (as a .map and a .yelo of
one name are, which the loader reads the .map of). */
static void custom_edition_map_add(
	char const *name,
	char const *extension)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	struct custom_edition_map *map;
	short map_index;
	boolean xbox_cache;

	if (csstrcasecmp(extension, "map") && csstrcasecmp(extension, "yelo"))
	{
		return;
	}
	for (map_index = 0; map_index < globals->map_count; map_index++)
	{
		if (!csstrcasecmp(globals->maps[map_index].name, name))
		{
			return;
		}
	}
	if (xbox_level_named(name))
	{
		return;
	}
	if (custom_edition_map_disabled(name))
	{
		error(_error_silent, "custom edition: the map '%s' is turned off (HALO_MAPS_DISABLED)", name);
		return;
	}
	/* a modded or newly built Xbox map plays as the Xbox levels do; a
	Custom Edition one only with the setting on */
	xbox_cache = !csstrcasecmp(extension, "map") && custom_edition_cache_xbox_multiplayer(name);
	if (!xbox_cache && !(halo_custom_edition_enabled() && custom_edition_cache_multiplayer(name)))
	{
		return;
	}
	if (csstrlen(name) > MAXIMUM_MAP_NAME_LENGTH)
	{
		error(
			_error_silent,
			"custom edition: the map '%s' is not in the level list: its name is longer than %d characters",
			name,
			MAXIMUM_MAP_NAME_LENGTH);
		return;
	}
	if (globals->map_count == MAXIMUM_CUSTOM_EDITION_MAPS)
	{
		error(
			_error_silent,
			"custom edition: the map '%s' is not in the level list, which holds %d of them",
			name,
			MAXIMUM_CUSTOM_EDITION_MAPS);
		return;
	}

	map = &globals->maps[globals->map_count++];
	csmemset(map, 0, sizeof(*map));
	map->xbox_cache = xbox_cache;
	csstrcpy(map->name, name);
	csprintf(map->level_name, LEVEL_NAME_FORMAT, name, name);
	display_name_make(name, map->display_name);
	custom_edition_map_description_read(map);

	return;
}

static int custom_edition_map_compare(
	void const *first,
	void const *second)
{
	return (int)csstrcasecmp(
		((struct custom_edition_map const *)first)->name,
		((struct custom_edition_map const *)second)->name);
}

static void custom_edition_maps_look_for(
	void)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	struct file_reference directory;
	struct file_reference file;
	char name[MAXIMUM_FILENAME_LENGTH + 1];
	char extension[MAXIMUM_FILENAME_LENGTH + 1];

	custom_edition_maps_forget();
	globals->looked_for = TRUE;

	file_reference_create_from_path(&directory, cache_files_map_directory(), TRUE);
	find_files_start(0, &directory);
	while (find_files_next(&file, NULL))
	{
		file_reference_get_name(&file, FLAG(_name_filename_bit), name);
		file_reference_get_name(&file, FLAG(_name_extension_bit), extension);
		custom_edition_map_add(name, extension);
	}
	qsort(globals->maps, globals->map_count, sizeof(globals->maps[0]), custom_edition_map_compare);
	error(_error_silent, "custom edition: %d multiplayer maps for the level list", globals->map_count);

	return;
}

/* The picture of `map` as a texture, or NULL when it has none that can be
shown (which is logged). */
static struct bitmap_data *custom_edition_map_picture_read(
	struct custom_edition_map const *map)
{
	char path[MAXIMUM_FILENAME_LENGTH + 1];
	struct bmp_file_picture picture;
	enum bmp_file_status status;
	struct bitmap_data *bitmap = NULL;
	uint8_t *file = NULL;
	FILE *stream;
	long size = 0;

	csprintf(path, "%s%s%s", cache_files_map_directory(), map->name, PICTURE_EXTENSION);
	stream = fopen(path, "rb");
	if (!stream)
	{
		return NULL;
	}
	if (fseek(stream, 0, SEEK_END) == 0 &&
		(size = ftell(stream)) >= 0 &&
		size <= MAXIMUM_PICTURE_FILE_BYTES &&
		fseek(stream, 0, SEEK_SET) == 0)
	{
		/* one more byte, so that an empty file is not a failed allocation */
		file = malloc((size_t)size + 1);
		if (file && fread(file, 1, (size_t)size, stream) != (size_t)size)
		{
			free(file);
			file = NULL;
		}
	}
	fclose(stream);
	if (!file)
	{
		error(_error_silent, "custom edition: the picture '%s' could not be read", path);
		return NULL;
	}

	status = bmp_file_open(file, (uint32_t)size, &picture);
	if (status == _bmp_file_status_ok)
	{
		bitmap = bitmap_2d_new(PICTURE_TEXTURE_SIZE, PICTURE_TEXTURE_SIZE, 0, _bitmap_format_a8r8g8b8);
		if (bitmap && bitmap->base_address)
		{
			bmp_file_fit(
				file,
				&picture,
				LEVEL_PICTURE_SHAPE_WIDTH,
				LEVEL_PICTURE_SHAPE_HEIGHT,
				PICTURE_TEXTURE_SIZE,
				PICTURE_TEXTURE_SIZE,
				bitmap->base_address);
			bitmap_rebuild(bitmap);
		}
		if (bitmap && !bitmap->hardware_format)
		{
			bitmap_delete(bitmap);
			bitmap = NULL;
		}
	}
	else
	{
		error(_error_silent, "custom edition: the picture '%s' cannot be shown: %s", path, bmp_file_status_describe(status));
	}
	free(file);

	return bitmap;
}

/* the map shown with `display_index`, or NULL */
static struct custom_edition_map *custom_edition_map_get(
	short display_index)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	short map_index = display_index - FIRST_DISPLAY_INDEX;

	return map_index >= 0 && map_index < globals->map_count ? &globals->maps[map_index] : NULL;
}

/* ---------- public code */

char **custom_edition_maps_level_list(
	char **xbox_levels,
	short xbox_level_count,
	short *level_count)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	short level_index;
	short map_index;

	globals->xbox_level_count = MIN(xbox_level_count, MAXIMUM_XBOX_LEVELS);
	for (level_index = 0; level_index < globals->xbox_level_count; level_index++)
	{
		globals->levels[level_index] = xbox_levels[level_index];
	}
	custom_edition_maps_look_for();
	for (map_index = 0; map_index < globals->map_count; map_index++)
	{
		globals->levels[globals->xbox_level_count + map_index] = globals->maps[map_index].level_name;
	}
	*level_count = globals->xbox_level_count + globals->map_count;

	return globals->levels;
}

short custom_edition_maps_level_display_index(
	short level_index)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	short map_index = level_index - globals->xbox_level_count;

	return map_index >= 0 && map_index < globals->map_count ? FIRST_DISPLAY_INDEX + map_index : level_index;
}

short custom_edition_maps_display_index(
	char const *level_name)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	char const *name = tag_name_strip_path(level_name);
	short map_index;

	if (!globals->looked_for)
	{
		custom_edition_maps_look_for();
	}
	for (map_index = 0; map_index < globals->map_count; map_index++)
	{
		if (!csstrcasecmp(globals->maps[map_index].name, name))
		{
			return FIRST_DISPLAY_INDEX + map_index;
		}
	}

	return NONE;
}

wchar_t *custom_edition_maps_name(
	short display_index)
{
	struct custom_edition_map *map = custom_edition_map_get(display_index);

	return map ? map->display_name : NULL;
}

wchar_t *custom_edition_maps_description(
	short display_index)
{
	struct custom_edition_map *map = custom_edition_map_get(display_index);

	return map ? map->description : NULL;
}

struct bitmap_data *custom_edition_maps_picture(
	long bitmap_tag_index,
	short *frame_index)
{
	struct custom_edition_map *map;

	if (*frame_index < FIRST_DISPLAY_INDEX ||
		bitmap_tag_index == NONE ||
		csstrcasecmp(tag_get_name(bitmap_tag_index), LEVEL_PICTURES_TAG_NAME))
	{
		return NULL;
	}

	map = custom_edition_map_get(*frame_index);
	*frame_index = UNKNOWN_LEVEL_FRAME;
	if (!map)
	{
		return NULL;
	}
	if (!map->picture_read)
	{
		map->picture = custom_edition_map_picture_read(map);
		map->picture_read = TRUE;
	}

	return map->picture;
}


unsigned long custom_edition_maps_network_identity(
	char const *level_name)
{
	char const *name;

	if (!level_name || !level_name[0])
	{
		return 0;
	}
	name = tag_name_strip_path(level_name);
	if (xbox_level_stock(name))
	{
		return 0;
	}

	return custom_edition_cache_map_identity(name);
}

boolean custom_edition_maps_host_copy_matches(
	char const *level_name,
	unsigned long host_identity,
	boolean *missing)
{
	unsigned long identity = custom_edition_maps_network_identity(level_name);
	char const *name = level_name ? tag_name_strip_path(level_name) : "";

	*missing = FALSE;
	if (xbox_level_stock(name))
	{
		return TRUE;
	}
	if (!identity)
	{
		*missing = TRUE;
		error(_error_silent, "custom maps: the host plays '%s', which this machine does not have", name);
		return FALSE;
	}
	if (!host_identity)
	{
		/* a host of a build that does not say which copy it plays */
		error(_error_silent, "custom maps: the host does not say which copy of '%s' it plays; playing this machine's", name);
		return TRUE;
	}
	if (identity != host_identity)
	{
		error(
			_error_silent,
			"custom maps: this machine's '%s' (0x%08lX) is not the host's (0x%08lX)",
			name,
			identity,
			host_identity);
		return FALSE;
	}

	return TRUE;
}
