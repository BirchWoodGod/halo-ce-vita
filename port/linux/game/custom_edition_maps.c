/*
CUSTOM_EDITION_MAPS.C

The Halo Custom Edition maps in the menus (custom_edition_maps.h).

The maps are the Custom Edition caches of multiplayer scenarios in the maps
folder, OpenSauce's ".yelo" maps among them (custom_edition_cache_multiplayer),
looked for whenever the level list opens. A map is offered under its file's
name, as the level levels\test\<name>\<name> as the Xbox levels are named:
the cache file loader finds a map by the last part of its level name. The
game engine keeps a level name in 64 characters, so a map whose name is
longer than 25 characters is left out, and so is a map named as one of the
Xbox levels, which that level already offers.

The Custom Edition caches of solo scenarios, the campaign maps
(custom_edition_cache_campaign), are the campaign's level list's, after its
ten levels (ui_widget_event_handler_functions.c; OpenCE's CUSTOM
SINGLEPLAYER, ce41b41d), looked for whenever that list opens: played alone
as the campaign's levels are, at the difficulty chosen next, or hosted as
network co-op (Y on the difficulty). Their level name is
custom_maps\<name> (custom_edition_cache.h), which no campaign level has.
They come after the multiplayer maps in the maps array, with display
indices of their own.

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
#include "text/unicode.h"
#include "../src/lang.h"

#include <stdio.h>
#include <stdlib.h>

/* main.c's */
short main_get_solo_level_from_name(char const *name);

/* ---------- constants */

#define MAXIMUM_CUSTOM_EDITION_MAPS CUSTOM_EDITION_MAPS_MAXIMUM
/* room for the level list's Xbox levels
(ui_widget_event_handler_functions.c offers 13) */
#define MAXIMUM_XBOX_LEVELS 16

/* levels\test\<name>\<name> in the 63 characters the game engine's stage
keeps of a level name (game_engine.c, struct game_engine_stage) */
#define LEVEL_NAME_FORMAT "levels\\test\\%s\\%s"
#define MAXIMUM_MAP_NAME_LENGTH 25

/* The maps' display indices: beyond every string and frame of the menus'
tags (the level names are 15 strings, the level pictures 14 frames); the
campaign maps' after the multiplayer maps' (OpenCE's numbers). */
#define FIRST_DISPLAY_INDEX 0x4000
#define FIRST_CAMPAIGN_DISPLAY_INDEX 0x6000

/* the level pictures, their shape (the level list's are 140 by 114 of the
menus' 640 by 480, the lobby's 139 by 113) and their frame of an unknown
level (ui_widget_game_data_input_functions.c) */
#define LEVEL_PICTURES_TAG_NAME "ui\\shell\\bitmaps\\mp_map_grafix"
#define LEVEL_PICTURE_SHAPE_WIDTH 140
#define LEVEL_PICTURE_SHAPE_HEIGHT 114
#define UNKNOWN_LEVEL_FRAME 13
/* the campaign's level pictures (the level list's), and their frame of a
level not reached yet (ui_widget_game_data_input_functions.c) */
#define CAMPAIGN_LEVEL_PICTURES_TAG_NAME "ui\\shell\\bitmaps\\sp_levels"
#define UNKNOWN_CAMPAIGN_LEVEL_FRAME 10

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
	/* a Custom Edition campaign map (a solo scenario) */
	boolean campaign;
};

struct custom_edition_maps_globals
{
	boolean looked_for;
	/* the maps: the multiplayer ones, then the campaign ones */
	short map_count;
	short multiplayer_count;
	short campaign_count;
	struct custom_edition_map maps[MAXIMUM_CUSTOM_EDITION_MAPS];
	/* the latest level list: its Xbox levels, then the maps' level names */
	short xbox_level_count;
	char *levels[MAXIMUM_XBOX_LEVELS + MAXIMUM_CUSTOM_EDITION_MAPS];
};

/* What each map file of the maps folder was found to be, by its name and
the time it was last written, and the text of a description file by the
same, so that looking for the maps again - each time the level list opens -
opens none of the files it has seen (custom_edition_maps_look_for): each
was opened, its header read and closed, and its <name>.txt looked for, ~180
file calls and 0.5-0.7 s of the game thread on the Vita at each opening of
the level list (Oct 8 and 9). A file written since, or the setting changed,
is looked at again; a map share download forgets its map's
(custom_edition_maps_file_forget). */
#define MAXIMUM_REMEMBERED_MAP_FILES 192
/* the folder's files a look takes in (the maps and their descriptions) */
#define MAXIMUM_LISTED_FILES 512
#define LISTED_NAME_BYTES 64

struct remembered_map_file
{
	/* (its name and extension, as lower case, hashed: 0 is none) */
	unsigned long hash;
	struct file_last_modification_date date;
	boolean custom_edition_enabled;
	boolean xbox_cache;
	boolean multiplayer;
	boolean campaign;
	/* its description file's text, as read when it was last written */
	boolean description_known;
	struct file_last_modification_date description_date;
	wchar_t description[MAXIMUM_DESCRIPTION_LENGTH + 1];
};

struct listed_file
{
	char name[LISTED_NAME_BYTES];
	char extension[8];
	struct file_last_modification_date date;
};

/* ---------- globals */

static struct custom_edition_maps_globals custom_edition_maps_globals;
static struct remembered_map_file remembered_map_files[MAXIMUM_REMEMBERED_MAP_FILES];
static short next_remembered_map_file;
static struct listed_file listed_files[MAXIMUM_LISTED_FILES];


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

/* the Xbox levels' titles, as the menus name them (p2p_lobby.c's too), in
xbox_level_names' order */
static char const *const xbox_level_titles[] =
{
	"Battle Creek", "Sidewinder", "Damnation", "Rat Race", "Prisoner", "Hang 'Em High", "Chill Out",
	"Derelict", "Boarding Action", "Blood Gulch", "Wizard", "Chiron TL-34", "Longest",
	"The Pillar of Autumn", "Halo", "The Truth and Reconciliation", "The Silent Cartographer",
	"Assault on the Control Room", "343 Guilty Spark", "The Library", "Two Betrayals", "Keyes", "The Maw",
};

typedef char verify_xbox_level_titles[NUMBEROF(xbox_level_titles) == NUMBEROF(xbox_level_names) ? 1 : -1];

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
	globals->multiplayer_count = 0;
	globals->campaign_count = 0;

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
	struct custom_edition_map *map,
	struct remembered_map_file *remembered,
	boolean description_listed,
	struct file_last_modification_date const *description_date)
{
	char path[MAXIMUM_FILENAME_LENGTH + 1];
	byte text[MAXIMUM_DESCRIPTION_FILE_BYTES];
	FILE *stream;
	long text_size = 0;
	long text_index = 0;
	short length = 0;

	/* (the folder's listing said whether there is one, and when it was
	written: none is not looked for, one read before is not read again) */
	if (description_listed && !description_date)
	{
		stream = NULL;
	}
	else if (remembered && description_date && remembered->description_known &&
		!csmemcmp(&remembered->description_date, description_date, sizeof(*description_date)))
	{
		csmemcpy(map->description, remembered->description, sizeof(map->description));
		length = (short)ustrlen(map->description);
		goto described;
	}
	else
	{
		csprintf(path, "%s%s%s", cache_files_map_directory(), map->name, DESCRIPTION_EXTENSION);
		stream = fopen(path, "rb");
	}
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
	if (remembered && description_date)
	{
		csmemcpy(remembered->description, map->description, sizeof(remembered->description));
		remembered->description_date = *description_date;
		remembered->description_known = TRUE;
	}

described:
	/* (none: the default, in the manner of the Xbox levels', in the
	player's language, lang.h) */
	if (!length && map->xbox_cache)
	{
		ustrncpy(map->description, TW(L"Custom map"), MAXIMUM_DESCRIPTION_LENGTH);
	}
	else if (!length)
	{
		ustrncpy(map->description, TW(L"Halo Custom\r\nEdition map"), MAXIMUM_DESCRIPTION_LENGTH);
	}
	map->description[MAXIMUM_DESCRIPTION_LENGTH] = 0;

	return;
}

/* Adds the map the file `name`.`extension` of the maps folder holds, when it
is a Custom Edition multiplayer or campaign map not added yet (as a .map and
a .yelo of one name are, which the loader reads the .map of). */
static unsigned long map_file_hash(
	char const *name,
	char const *extension)
{
	/* (FNV-1a of "name.extension" in lower case) */
	unsigned long hash = 2166136261UL;
	char const *part;
	short part_index;

	for (part_index = 0; part_index < 3; part_index++)
	{
		part = part_index == 0 ? name : part_index == 1 ? "." : extension;
		for (; *part; part++)
		{
			hash = (hash ^ (unsigned char)LOWER_CASE(*part)) * 16777619UL;
		}
	}

	return hash ? hash : 1;
}

static struct remembered_map_file *remembered_map_file_find(
	unsigned long hash)
{
	short index;

	for (index = 0; index < MAXIMUM_REMEMBERED_MAP_FILES; index++)
	{
		if (remembered_map_files[index].hash == hash)
		{
			return &remembered_map_files[index];
		}
	}

	return NULL;
}

/* Adds the map the file `name`.`extension` of the maps folder holds, when it
is a Custom Edition multiplayer or campaign map not added yet (as a .map and
a .yelo of one name are, which the loader reads the .map of). With `date`,
when the file was last written, what it was found to be before is taken
rather than looked at again (remembered_map_files); with `listed`, whether
it has a description file is known: when it was last written, or NULL for
none. */
static void custom_edition_map_add(
	char const *name,
	char const *extension,
	struct file_last_modification_date const *date,
	boolean description_listed,
	struct file_last_modification_date const *description_date)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	struct custom_edition_map *map;
	struct remembered_map_file *remembered = NULL;
	boolean custom_edition_enabled = halo_custom_edition_enabled();
	short map_index;
	boolean xbox_cache;
	boolean multiplayer;
	boolean campaign = FALSE;

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
	if (date)
	{
		remembered = remembered_map_file_find(map_file_hash(name, extension));
		if (remembered &&
			(csmemcmp(&remembered->date, date, sizeof(*date)) ||
				remembered->custom_edition_enabled != custom_edition_enabled))
		{
			/* (written since, or the setting changed: looked at again) */
			csmemset(remembered, 0, sizeof(*remembered));
			remembered = NULL;
		}
	}
	if (remembered)
	{
		xbox_cache = remembered->xbox_cache;
		multiplayer = remembered->multiplayer;
		campaign = remembered->campaign;
	}
	else
	{
		/* a modded or newly built Xbox map plays as the Xbox levels do; a
		Custom Edition one only with the setting on */
		xbox_cache = !csstrcasecmp(extension, "map") && custom_edition_cache_xbox_multiplayer(name);
		multiplayer = !xbox_cache && custom_edition_enabled && custom_edition_cache_multiplayer(name);
		/* (a campaign map: the campaign's level list's) */
		campaign = !xbox_cache && !multiplayer && custom_edition_enabled && custom_edition_cache_campaign(name);
		if (date)
		{
			remembered = &remembered_map_files[next_remembered_map_file];
			next_remembered_map_file = (short)((next_remembered_map_file + 1) % MAXIMUM_REMEMBERED_MAP_FILES);
			csmemset(remembered, 0, sizeof(*remembered));
			remembered->hash = map_file_hash(name, extension);
			remembered->date = *date;
			remembered->custom_edition_enabled = custom_edition_enabled;
			remembered->xbox_cache = xbox_cache;
			remembered->multiplayer = multiplayer;
			remembered->campaign = campaign;
		}
	}
	if (!xbox_cache && !multiplayer && !campaign)
	{
		return;
	}
	if (csstrlen(name) > MAXIMUM_MAP_NAME_LENGTH)
	{
		error(
			_error_silent,
			"custom edition: the map '%s' is not in a level list: its name is longer than %d characters",
			name,
			MAXIMUM_MAP_NAME_LENGTH);
		return;
	}
	if (globals->map_count == MAXIMUM_CUSTOM_EDITION_MAPS)
	{
		error(
			_error_silent,
			"custom edition: the map '%s' is not in a level list, which hold %d of them",
			name,
			MAXIMUM_CUSTOM_EDITION_MAPS);
		return;
	}

	map = &globals->maps[globals->map_count++];
	csmemset(map, 0, sizeof(*map));
	map->xbox_cache = xbox_cache;
	map->campaign = campaign;
	csstrcpy(map->name, name);
	if (campaign)
	{
		csprintf(map->level_name, "%s%s", CUSTOM_EDITION_LEVEL_NAME_PREFIX, name);
	}
	else
	{
		csprintf(map->level_name, LEVEL_NAME_FORMAT, name, name);
	}
	display_name_make(name, map->display_name);
	custom_edition_map_description_read(map, remembered, description_listed, description_date);

	return;
}

/* the multiplayer maps first, then the campaign maps, each in the order of
their names */
static int custom_edition_map_compare(
	void const *first,
	void const *second)
{
	struct custom_edition_map const *first_map = first;
	struct custom_edition_map const *second_map = second;

	if (first_map->campaign != second_map->campaign)
	{
		return first_map->campaign ? 1 : -1;
	}

	return (int)csstrcasecmp(first_map->name, second_map->name);
}

static void custom_edition_maps_look_for(
	void)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	struct file_reference directory;
	struct file_reference file;
	struct file_last_modification_date date;
	char name[MAXIMUM_FILENAME_LENGTH + 1];
	char extension[MAXIMUM_FILENAME_LENGTH + 1];
	short listed_count = 0;
	short index;

	custom_edition_maps_forget();
	globals->looked_for = TRUE;

	file_reference_create_from_path(&directory, cache_files_map_directory(), TRUE);
	find_files_start(0, &directory);
	/* the maps and their description files listed first (a map's own is
	then known to be there or not), each with when it was last written; a
	name too long to list is looked at as it is found */
	while (find_files_next(&file, &date))
	{
		file_reference_get_name(&file, FLAG(_name_filename_bit), name);
		file_reference_get_name(&file, FLAG(_name_extension_bit), extension);
		if (csstrcasecmp(extension, "map") && csstrcasecmp(extension, "yelo") && csstrcasecmp(extension, "txt"))
		{
			continue;
		}
		if (csstrlen(name) < LISTED_NAME_BYTES && csstrlen(extension) < (long)sizeof(listed_files[0].extension) &&
			listed_count < MAXIMUM_LISTED_FILES)
		{
			csstrcpy(listed_files[listed_count].name, name);
			csstrcpy(listed_files[listed_count].extension, extension);
			listed_files[listed_count].date = date;
			listed_count++;
		}
		else if (csstrcasecmp(extension, "txt"))
		{
			custom_edition_map_add(name, extension, NULL, FALSE, NULL);
		}
	}
	for (index = 0; index < listed_count; index++)
	{
		struct file_last_modification_date const *description_date = NULL;
		boolean description_listed = TRUE;
		short other;

		if (!csstrcasecmp(listed_files[index].extension, "txt"))
		{
			continue;
		}
		for (other = 0; other < listed_count; other++)
		{
			if (!csstrcasecmp(listed_files[other].extension, "txt") &&
				!csstrcasecmp(listed_files[other].name, listed_files[index].name))
			{
				/* (one named in another case than the map: read as
				before, by the map's name) */
				if (csstrcmp(listed_files[other].name, listed_files[index].name) ||
					csstrcmp(listed_files[other].extension, "txt"))
				{
					description_listed = FALSE;
				}
				description_date = &listed_files[other].date;
				break;
			}
		}
		custom_edition_map_add(
			listed_files[index].name,
			listed_files[index].extension,
			&listed_files[index].date,
			description_listed,
			description_listed ? description_date : NULL);
	}
	qsort(globals->maps, globals->map_count, sizeof(globals->maps[0]), custom_edition_map_compare);
	for (globals->multiplayer_count = 0;
		globals->multiplayer_count < globals->map_count && !globals->maps[globals->multiplayer_count].campaign;
		globals->multiplayer_count++)
	{
	}
	globals->campaign_count = globals->map_count - globals->multiplayer_count;
	error(
		_error_silent,
		"custom edition: %d multiplayer maps for the level list, %d campaign maps for the campaign's",
		globals->multiplayer_count,
		globals->campaign_count);

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
	short campaign_index = display_index - FIRST_CAMPAIGN_DISPLAY_INDEX;

	if (campaign_index >= 0 && campaign_index < globals->campaign_count)
	{
		return &globals->maps[globals->multiplayer_count + campaign_index];
	}

	return map_index >= 0 && map_index < globals->multiplayer_count ? &globals->maps[map_index] : NULL;
}

/* the display index of the map at `map_index` in the maps array */
static short custom_edition_map_display_index(
	short map_index)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;

	return globals->maps[map_index].campaign ?
		FIRST_CAMPAIGN_DISPLAY_INDEX + map_index - globals->multiplayer_count :
		FIRST_DISPLAY_INDEX + map_index;
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
	for (map_index = 0; map_index < globals->multiplayer_count; map_index++)
	{
		globals->levels[globals->xbox_level_count + map_index] = globals->maps[map_index].level_name;
	}
	*level_count = globals->xbox_level_count + globals->multiplayer_count;

	return globals->levels;
}

short custom_edition_maps_level_display_index(
	short level_index)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	short map_index = level_index - globals->xbox_level_count;

	return map_index >= 0 && map_index < globals->multiplayer_count ? FIRST_DISPLAY_INDEX + map_index : level_index;
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
			return custom_edition_map_display_index(map_index);
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
	boolean campaign_pictures;

	if (*frame_index < FIRST_DISPLAY_INDEX || bitmap_tag_index == NONE)
	{
		return NULL;
	}
	/* (the multiplayer level pictures, or the campaign's: either list shows
	maps of its own kind, and the lobby a co-op game's campaign map) */
	campaign_pictures = !csstrcasecmp(tag_get_name(bitmap_tag_index), CAMPAIGN_LEVEL_PICTURES_TAG_NAME);
	if (!campaign_pictures && csstrcasecmp(tag_get_name(bitmap_tag_index), LEVEL_PICTURES_TAG_NAME))
	{
		return NULL;
	}

	map = custom_edition_map_get(*frame_index);
	*frame_index = campaign_pictures ? UNKNOWN_CAMPAIGN_LEVEL_FRAME : UNKNOWN_LEVEL_FRAME;
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
	/* (a Halo PC map named as an Xbox level, which this machine plays with
	PC maps on, custom_edition_cache.c: not the Xbox level, and said, so that
	a player with the Xbox map does not load another map than the host's;
	versions before 1.1.0-beta.2 sent 0 and ignored it) */
	if (xbox_level_stock(name))
	{
		return halo_custom_edition_enabled() && custom_edition_cache_is_custom_edition(name) ?
			custom_edition_cache_map_identity(name) : 0;
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
	/* (an Xbox level the host plays as one: its copies differ by region,
	and are never compared; this machine's file is checked by
	custom_edition_maps_stock_problem) */
	if (xbox_level_stock(name) && !host_identity)
	{
		return TRUE;
	}
	/* (a Halo PC map named as an Xbox level, the host's: this machine's
	file of that name, whatever it is, against it) */
	if (xbox_level_stock(name))
	{
		identity = custom_edition_cache_map_identity(name);
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

short custom_edition_maps_loadable(
	char const *level_name,
	char *missing,
	long missing_size)
{
	char const *name = level_name ? tag_name_strip_path(level_name) : "";
	char path[MAXIMUM_FILENAME_LENGTH + 1];

	if (missing_size > 0)
	{
		missing[0] = 0;
	}
	/* (an Xbox level's file: the Xbox map's, checked as it is precached,
	custom_edition_maps_stock_problem; or a Halo PC map of that name, which
	loads as the Custom Edition maps below do) */
	if (xbox_level_stock(name) && !custom_edition_cache_is_custom_edition(name))
	{
		return _custom_edition_maps_loadable;
	}
	if (!name[0] || !custom_edition_cache_map_file_path(name, path, sizeof(path)))
	{
		return _custom_edition_maps_not_loadable;
	}
	if (custom_edition_cache_is_custom_edition(name))
	{
		/* (the resource maps first: PC maps on would not make it load) */
		if (custom_edition_cache_missing_resource_maps(name, missing, missing_size))
		{
			error(_error_silent, "custom maps: '%s' needs resource maps not in the maps folder: %s", name, missing);
			return _custom_edition_maps_needs_resource_maps;
		}
		if (!halo_custom_edition_enabled())
		{
			error(_error_silent, "custom maps: '%s' is a Custom Edition map, and PC maps is off", name);
			return _custom_edition_maps_needs_pc_maps;
		}
		return _custom_edition_maps_loadable;
	}
	if (!custom_edition_cache_xbox_multiplayer(name))
	{
		error(_error_silent, "custom maps: '%s' is not a multiplayer map this machine can load", name);
		return _custom_edition_maps_not_loadable;
	}

	return _custom_edition_maps_loadable;
}

boolean custom_edition_maps_stock_problem(
	char const *level_name,
	unsigned long host_identity,
	char *why,
	long why_size)
{
	char const *name = level_name ? tag_name_strip_path(level_name) : "";

	why[0] = 0;
	if (!xbox_level_stock(name) || host_identity)
	{
		return FALSE;
	}
	if (!cache_files_xbox_map_problem(level_name, why, why_size))
	{
		return FALSE;
	}
	error(_error_silent, "custom maps: the host plays the Xbox level '%s', which this machine cannot load: %s", name, why);

	return TRUE;
}

char const *custom_edition_maps_level_title(
	char const *level_name)
{
	char const *name = level_name ? tag_name_strip_path(level_name) : "";
	short index;

	for (index = 0; index < NUMBEROF(xbox_level_names); index++)
	{
		if (!csstrcasecmp(name, xbox_level_names[index]))
		{
			return xbox_level_titles[index];
		}
	}

	return name;
}

void custom_edition_maps_look_again(
	void)
{
	custom_edition_maps_globals.looked_for = FALSE;

	return;
}

void custom_edition_maps_file_forget(
	char const *name)
{
	struct remembered_map_file *remembered;

	while ((remembered = remembered_map_file_find(map_file_hash(name, "map"))) != NULL)
	{
		csmemset(remembered, 0, sizeof(*remembered));
	}
	while ((remembered = remembered_map_file_find(map_file_hash(name, "yelo"))) != NULL)
	{
		csmemset(remembered, 0, sizeof(*remembered));
	}

	return;
}

short custom_edition_maps_campaigns_find(
	void)
{
	custom_edition_maps_look_for();

	return custom_edition_maps_globals.campaign_count;
}

char const *custom_edition_maps_campaign_level_name(
	short campaign_index)
{
	struct custom_edition_map *map = custom_edition_map_get(FIRST_CAMPAIGN_DISPLAY_INDEX + campaign_index);

	return campaign_index >= 0 && map ? map->level_name : NULL;
}

short custom_edition_maps_campaign_display_index(
	short campaign_index)
{
	return campaign_index >= 0 && custom_edition_map_get(FIRST_CAMPAIGN_DISPLAY_INDEX + campaign_index) ?
		FIRST_CAMPAIGN_DISPLAY_INDEX + campaign_index :
		NONE;
}

boolean custom_edition_maps_campaign_level(
	char const *level_name)
{
	return level_name && (main_get_solo_level_from_name(level_name) != NONE || custom_edition_level_name(level_name));
}

boolean custom_edition_maps_shareable(
	char const *level_name)
{
	char const *name;

	if (!level_name || !level_name[0])
	{
		return FALSE;
	}
	name = tag_name_strip_path(level_name);

	/* (turned off since the list was made: HALO_MAPS_DISABLED is the panel's
	switch, read anew) */
	return !xbox_level_stock(name) &&
		!custom_edition_map_disabled(name) &&
		custom_edition_maps_display_index(level_name) != NONE;
}
