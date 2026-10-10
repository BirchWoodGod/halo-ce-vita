/*
CUSTOM_EDITION_MODS.C

(port) Halo: The Master Chief Collection campaign mods: the maps of the
mods folder's mods, their names, titles, order and pictures
(custom_edition_mods.h).

Each folder of the mods folder is a mod (16 at most), named by its folder:
its key is the folder's name in lower case, its letters, digits and '-'
only, at most 12 of them (the Workshop's 2915693904 as it is; "Ruby's
Rebalanced" as rubysrebalan). Its maps are its maps\*.map files that are
Halo: CE Anniversary caches of solo scenarios (custom_edition_cache.c
looks at each one's header and tag index), named <key>_<map> (ruby_a10),
at most 25 characters: a map of a longer name, or of a name a file of the
maps folder already has (which the cache file loader would find first), is
left out, and logged. Its CampaignInfo.json orders them and titles the
mod, and each level's campaign file (the ones CampaignInfo.json lists, or
without one every campaign\*.json) titles and describes the map its
ScenarioFile names (mod_campaign_files.c) and names its picture, a PNG of
the mod's folder (its thumbnail, else its large image: png_files.c), which
a Windows bitmap <map>.bmp beside the map takes the place of. A map its campaign does not
list comes after those it does, in the stock levels' order and then the
order of their names.

Everything read here is the mod's, untrusted: names are taken only of the
characters above, a campaign file only of the mod's own campaign folder
and of a name without a path, and each file only to
MOD_CAMPAIGN_FILE_MAXIMUM_BYTES. Nothing in a mod is run.
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "cseries_windows.h"
#include "tag_files/files.h"
#include "cache/cache_files.h"
#include "cache_file_formats.h"
#include "custom_edition_cache.h"
#include "custom_edition_mods.h"
#include "mod_campaign_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

#define MOD_KEY_LENGTH 12
/* a mod's folder name, and a map's file name without ".map", kept */
#define MOD_FOLDER_BYTES 48
#define MAP_STEM_BYTES 24
#define PATH_BYTES 256
/* the campaign files of a mod without a CampaignInfo.json looked at */
#define MAXIMUM_LOOSE_LEVEL_FILES 32
/* a map its campaign does not list: after those it does */
#define UNLISTED_ORDER 0x4000

/* ---------- structures */

struct custom_edition_mod
{
	char folder[MOD_FOLDER_BYTES];
	char key[MOD_KEY_LENGTH + 1];
	char title[CUSTOM_EDITION_MODS_TITLE_BYTES];
	boolean has_campaign_info;
};

struct custom_edition_mod_map
{
	char name[CUSTOM_EDITION_MODS_MAXIMUM_NAME_LENGTH + 1];
	char level_name[CUSTOM_EDITION_MODS_MAXIMUM_NAME_LENGTH + 8];
	char stem[MAP_STEM_BYTES];
	short mod_index;
	/* its place in its campaign (CampaignMaps), else UNLISTED_ORDER and
	then its stock level's, or after them */
	short order;
	char title[CUSTOM_EDITION_MODS_TITLE_BYTES];
	char description[CUSTOM_EDITION_MODS_DESCRIPTION_BYTES];
	boolean has_picture;
	char picture_path[PATH_BYTES];
};

struct custom_edition_mods_globals
{
	boolean looked_for;
	short mod_count;
	struct custom_edition_mod mods[CUSTOM_EDITION_MODS_MAXIMUM];
	short map_count;
	struct custom_edition_mod_map maps[CUSTOM_EDITION_MODS_MAXIMUM_MAPS];
};

/* ---------- globals */

static struct custom_edition_mods_globals custom_edition_mods_globals;

/* the stock campaign's levels, in their order */
static char const *const stock_level_names[] =
{
	"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
};

/* ---------- private code */

static char lower_case(
	char character)
{
	return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

/* a folder or file name of the mods folder this unit takes: printable
ASCII, none of what a path or a file system takes for something else, and
no "..", at most `bytes` - 1 characters */
static boolean mod_file_name_valid(
	char const *name,
	long bytes)
{
	long length;

	if (!name[0] || name[0] == '.')
	{
		return FALSE;
	}
	for (length = 0; name[length]; length++)
	{
		if (length >= bytes - 1 ||
			name[length] < ' ' || name[length] > '~' ||
			strchr("/\\:*?\"<>|%,", name[length]) ||
			(name[length] == '.' && name[length + 1] == '.'))
		{
			return FALSE;
		}
	}

	return TRUE;
}

/* a map's file name without ".map", as its name's part: letters (in lower
case), digits, '_' and '-' only */
static boolean map_stem_valid(
	char const *stem)
{
	long length;

	for (length = 0; stem[length]; length++)
	{
		char character = stem[length];

		if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
			character == '_' || character == '-'))
		{
			return FALSE;
		}
	}

	return length > 0;
}

static short stock_level_index(
	char const *stem)
{
	short index;

	for (index = 0; index < NUMBEROF(stock_level_names); index++)
	{
		if (!csstrcmp(stem, stock_level_names[index]))
		{
			return index;
		}
	}

	return NONE;
}

/* Reads the file at `path` (at most MOD_CAMPAIGN_FILE_MAXIMUM_BYTES of it)
into `text`, returning its size, or 0. */
static uint32_t mod_text_file_read(
	char const *path,
	uint8_t *text)
{
	FILE *stream = fopen(path, "rb");
	size_t size;

	if (!stream)
	{
		return 0;
	}
	/* (one more byte than is read: a larger file is refused) */
	size = fread(text, 1, MOD_CAMPAIGN_FILE_MAXIMUM_BYTES + 1, stream);
	fclose(stream);

	return size <= MOD_CAMPAIGN_FILE_MAXIMUM_BYTES ? (uint32_t)size : 0;
}

/* (port) the file of a level's picture, `name` as its campaign file gives
it ("assets\1 small.png": a PNG in a folder of the mod's or in its own),
into `path`: FALSE for a name of any other kind, or of no file */
static boolean mod_picture_path(
	struct custom_edition_mod const *mod,
	char const *name,
	char *path,
	long path_size)
{
	char parts[2][MOD_CAMPAIGN_FILE_NAME_BYTES];
	short part_count = 0;
	long length = 0;
	struct file_reference reference;

	if (!name[0])
	{
		return FALSE;
	}
	/* (at most a folder and a file, each named as mod_file_name_valid takes) */
	for (;;)
	{
		char character = *name++;

		if (character == '\\' || character == '/' || !character)
		{
			if (part_count >= NUMBEROF(parts) || !length)
			{
				return FALSE;
			}
			parts[part_count][length] = 0;
			if (!mod_file_name_valid(parts[part_count], MOD_CAMPAIGN_FILE_NAME_BYTES))
			{
				return FALSE;
			}
			part_count++;
			length = 0;
			if (!character)
			{
				break;
			}
			continue;
		}
		if (part_count >= NUMBEROF(parts) || length >= MOD_CAMPAIGN_FILE_NAME_BYTES - 1)
		{
			return FALSE;
		}
		parts[part_count][length++] = character;
	}
	length = (long)strlen(parts[part_count - 1]);
	if (length < 5 || csstrcasecmp(parts[part_count - 1] + length - 4, ".png"))
	{
		return FALSE;
	}
	if ((part_count == 1 ?
		snprintf(path, (size_t)path_size, "%s%s\\%s", CUSTOM_EDITION_MODS_DIRECTORY, mod->folder, parts[0]) :
		snprintf(path, (size_t)path_size, "%s%s\\%s\\%s", CUSTOM_EDITION_MODS_DIRECTORY, mod->folder, parts[0], parts[1])) >= path_size)
	{
		return FALSE;
	}

	return file_exists(file_reference_create_from_path(&reference, path, FALSE));
}

/* the mod's map of the file name `stem` (any case), or NULL */
static struct custom_edition_mod_map *mod_map_find(
	short mod_index,
	char const *stem)
{
	struct custom_edition_mods_globals *globals = &custom_edition_mods_globals;
	short map_index;

	for (map_index = 0; map_index < globals->map_count; map_index++)
	{
		if (globals->maps[map_index].mod_index == mod_index && !csstrcasecmp(globals->maps[map_index].stem, stem))
		{
			return &globals->maps[map_index];
		}
	}

	return NULL;
}

/* Reads the level campaign file `file_name` (a name of the mod's campaign
folder, as CampaignInfo.json lists it: "campaign/a10.json") and gives the
map its ScenarioFile names its title and description, and `order` when it
is not NONE. */
static void mod_level_file_read(
	short mod_index,
	char const *file_name,
	short order,
	uint8_t *text)
{
	struct custom_edition_mod const *mod = &custom_edition_mods_globals.mods[mod_index];
	struct mod_campaign_level level;
	struct custom_edition_mod_map *map;
	char path[PATH_BYTES];
	char const *name = file_name;
	uint32_t size;
	enum mod_campaign_file_status status;

	/* (only a file of the mod's campaign folder, named without a path) */
	if (!_strnicmp(name, "campaign/", 9) || !_strnicmp(name, "campaign\\", 9))
	{
		name += 9;
	}
	if (!mod_file_name_valid(name, MOD_CAMPAIGN_FILE_NAME_BYTES) ||
		snprintf(path, sizeof(path), "%s%s\\campaign\\%s", CUSTOM_EDITION_MODS_DIRECTORY, mod->folder, name) >= (int)sizeof(path))
	{
		error(_error_silent, "mods: %s: the campaign file '%s' is not one of its campaign folder", mod->folder, file_name);
		return;
	}
	size = mod_text_file_read(path, text);
	status = mod_campaign_level_read(text, size, &level);
	if (status != _mod_campaign_file_status_ok)
	{
		error(_error_silent, "mods: %s: the campaign file '%s' is not read: %s", mod->folder, name,
			size ? mod_campaign_file_status_describe(status) : "missing or too large");
		return;
	}
	map = level.scenario_file[0] ? mod_map_find(mod_index, level.scenario_file) : NULL;
	if (!map)
	{
		return;
	}
	csstrncpy(map->title, level.title, sizeof(map->title) - 1);
	map->title[sizeof(map->title) - 1] = 0;
	csstrncpy(map->description, level.description, sizeof(map->description) - 1);
	map->description[sizeof(map->description) - 1] = 0;
	if (order != NONE)
	{
		map->order = order;
	}
	/* (its picture: a bitmap beside the map, else the campaign file's) */
	if (!map->has_picture)
	{
		map->has_picture =
			mod_picture_path(mod, level.thumbnail, map->picture_path, sizeof(map->picture_path)) ||
			mod_picture_path(mod, level.large_image, map->picture_path, sizeof(map->picture_path));
	}

	return;
}

/* the campaign files of the mod `mod_index`: its CampaignInfo.json, else
every campaign\*.json */
static void mod_campaign_read(
	short mod_index)
{
	struct custom_edition_mod *mod = &custom_edition_mods_globals.mods[mod_index];
	static uint8_t text[MOD_CAMPAIGN_FILE_MAXIMUM_BYTES + 1];
	static struct mod_campaign_info info;
	char path[PATH_BYTES];
	uint32_t size;

	snprintf(path, sizeof(path), "%s%s\\CampaignInfo.json", CUSTOM_EDITION_MODS_DIRECTORY, mod->folder);
	size = mod_text_file_read(path, text);
	if (size && mod_campaign_info_read(text, size, &info) == _mod_campaign_file_status_ok)
	{
		short level_index;

		mod->has_campaign_info = TRUE;
		csstrncpy(mod->title, info.title, sizeof(mod->title) - 1);
		mod->title[sizeof(mod->title) - 1] = 0;
		for (level_index = 0; level_index < info.level_count; level_index++)
		{
			mod_level_file_read(mod_index, info.levels[level_index], level_index, text);
		}
	}
	else
	{
		char names[MAXIMUM_LOOSE_LEVEL_FILES][MOD_CAMPAIGN_FILE_NAME_BYTES];
		short name_count = 0;
		short name_index;
		WIN32_FIND_DATAA data;
		HANDLE find;

		if (size)
		{
			error(_error_silent, "mods: %s: its CampaignInfo.json is not read", mod->folder);
		}
		snprintf(path, sizeof(path), "%s%s\\campaign\\*.json", CUSTOM_EDITION_MODS_DIRECTORY, mod->folder);
		find = FindFirstFileA(path, &data);
		if (find != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
					mod_file_name_valid(data.cFileName, MOD_CAMPAIGN_FILE_NAME_BYTES) &&
					name_count < MAXIMUM_LOOSE_LEVEL_FILES)
				{
					csstrcpy(names[name_count++], data.cFileName);
				}
			} while (FindNextFileA(find, &data));
			CloseHandle(find);
		}
		for (name_index = 0; name_index < name_count; name_index++)
		{
			mod_level_file_read(mod_index, names[name_index], NONE, text);
		}
	}

	return;
}

/* Adds the mod of the mods folder's folder `folder`, and its maps. */
static void mod_add(
	char const *folder)
{
	struct custom_edition_mods_globals *globals = &custom_edition_mods_globals;
	struct custom_edition_mod *mod;
	char path[PATH_BYTES];
	WIN32_FIND_DATAA data;
	HANDLE find;
	short mod_index;
	short length = 0;
	short index;

	if (globals->mod_count >= CUSTOM_EDITION_MODS_MAXIMUM)
	{
		error(_error_silent, "mods: '%s' is not looked at: the mods folder's first %d are", folder, CUSTOM_EDITION_MODS_MAXIMUM);
		return;
	}
	mod_index = globals->mod_count;
	mod = &globals->mods[mod_index];
	csmemset(mod, 0, sizeof(*mod));
	csstrcpy(mod->folder, folder);
	for (index = 0; folder[index] && length < MOD_KEY_LENGTH; index++)
	{
		char character = lower_case(folder[index]);

		if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-')
		{
			mod->key[length++] = character;
		}
	}
	mod->key[length] = 0;
	if (!length)
	{
		error(_error_silent, "mods: '%s' is not looked at: its name has no letter or digit", folder);
		return;
	}
	for (index = 0; index < mod_index; index++)
	{
		if (!csstrcmp(globals->mods[index].key, mod->key))
		{
			error(_error_silent, "mods: '%s' is not looked at: '%s' is named alike (%s)", folder, globals->mods[index].folder,
				mod->key);
			return;
		}
	}
	globals->mod_count++;

	snprintf(path, sizeof(path), "%s%s\\maps\\*.map", CUSTOM_EDITION_MODS_DIRECTORY, folder);
	find = FindFirstFileA(path, &data);
	if (find == INVALID_HANDLE_VALUE)
	{
		error(_error_silent, "mods: '%s' has no maps folder of maps", folder);
		return;
	}
	do
	{
		struct custom_edition_mod_map *map;
		char stem[MAP_STEM_BYTES];
		char map_path[PATH_BYTES];
		char maps_folder_path[PATH_BYTES];
		long stem_length = (long)strlen(data.cFileName) - 4;
		struct file_reference reference;

		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
			stem_length <= 0 || stem_length >= MAP_STEM_BYTES ||
			csstrcasecmp(data.cFileName + stem_length, ".map"))
		{
			continue;
		}
		for (index = 0; index < stem_length; index++)
		{
			stem[index] = lower_case(data.cFileName[index]);
		}
		stem[stem_length] = 0;
		if (!map_stem_valid(stem) ||
			(long)strlen(mod->key) + 1 + stem_length > CUSTOM_EDITION_MODS_MAXIMUM_NAME_LENGTH)
		{
			error(_error_silent, "mods: %s\\maps\\%s is left out: its name is not letters, digits, '_' and '-', or %s_%s is "
				"longer than %d characters", folder, data.cFileName, mod->key, stem, CUSTOM_EDITION_MODS_MAXIMUM_NAME_LENGTH);
			continue;
		}
		if (globals->map_count >= CUSTOM_EDITION_MODS_MAXIMUM_MAPS)
		{
			error(_error_silent, "mods: %s\\maps\\%s is left out: the first %d maps are listed", folder, data.cFileName,
				CUSTOM_EDITION_MODS_MAXIMUM_MAPS);
			break;
		}
		map = &globals->maps[globals->map_count];
		csmemset(map, 0, sizeof(*map));
		csprintf(map->name, "%s_%s", mod->key, stem);
		csprintf(map->level_name, "%s%s", CUSTOM_EDITION_MODS_LEVEL_NAME_PREFIX, map->name);
		csstrcpy(map->stem, stem);
		map->mod_index = mod_index;
		map->order = (short)(UNLISTED_ORDER + (stock_level_index(stem) != NONE ? stock_level_index(stem) : NUMBEROF(stock_level_names)));
		/* (a file of the maps folder of the name, which the cache file
		loader finds first) */
		snprintf(maps_folder_path, sizeof(maps_folder_path), "%s%s.map", cache_files_map_directory(), map->name);
		if (file_exists(file_reference_create_from_path(&reference, maps_folder_path, FALSE)))
		{
			error(_error_silent, "mods: %s\\maps\\%s is left out: the maps folder has a %s.map", folder, data.cFileName, map->name);
			continue;
		}
		snprintf(map_path, sizeof(map_path), "%s%s\\maps\\%s", CUSTOM_EDITION_MODS_DIRECTORY, folder, data.cFileName);
		if (!custom_edition_cache_mod_map_identify(map_path))
		{
			continue;
		}
		snprintf(map->picture_path, sizeof(map->picture_path), "%s%s\\maps\\%.*s.bmp", CUSTOM_EDITION_MODS_DIRECTORY, folder,
			(int)stem_length, data.cFileName);
		map->has_picture = file_exists(file_reference_create_from_path(&reference, map->picture_path, FALSE));
		globals->map_count++;
	} while (FindNextFileA(find, &data));
	CloseHandle(find);

	mod_campaign_read(mod_index);

	return;
}

/* each mod's maps in their order, the mods in the order of their folders'
names */
static int mod_map_compare(
	void const *first,
	void const *second)
{
	struct custom_edition_mod_map const *first_map = first;
	struct custom_edition_mod_map const *second_map = second;

	if (first_map->mod_index != second_map->mod_index)
	{
		return first_map->mod_index - second_map->mod_index;
	}
	if (first_map->order != second_map->order)
	{
		return first_map->order - second_map->order;
	}

	return (int)csstrcmp(first_map->stem, second_map->stem);
}

static int mod_folder_compare(
	void const *first,
	void const *second)
{
	return (int)csstrcasecmp(first, second);
}

/* the map named `name` (or `name`'s last part), or NULL; the mods looked for
when they have not been */
static struct custom_edition_mod_map const *mod_map_named(
	char const *name)
{
	struct custom_edition_mods_globals *globals = &custom_edition_mods_globals;
	short map_index;

	if (!name)
	{
		return NULL;
	}
	name = tag_name_strip_path(name);
	if (!globals->looked_for)
	{
		custom_edition_mods_look_for();
	}
	for (map_index = 0; map_index < globals->map_count; map_index++)
	{
		if (!csstrcasecmp(globals->maps[map_index].name, name))
		{
			return &globals->maps[map_index];
		}
	}

	return NULL;
}

/* ---------- public code */

short custom_edition_mods_look_for(
	void)
{
	struct custom_edition_mods_globals *globals = &custom_edition_mods_globals;
	char folders[CUSTOM_EDITION_MODS_MAXIMUM][MOD_FOLDER_BYTES];
	short folder_count = 0;
	short folder_index;
	WIN32_FIND_DATAA data;
	HANDLE find;

	globals->looked_for = TRUE;
	globals->mod_count = 0;
	globals->map_count = 0;
	find = FindFirstFileA(CUSTOM_EDITION_MODS_DIRECTORY "*", &data);
	if (find == INVALID_HANDLE_VALUE)
	{
		return 0;
	}
	do
	{
		if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !csstrcmp(data.cFileName, ".") ||
			!csstrcmp(data.cFileName, ".."))
		{
			continue;
		}
		if (!mod_file_name_valid(data.cFileName, MOD_FOLDER_BYTES))
		{
			error(_error_silent, "mods: a folder of the mods folder is not looked at: its name is not one this build takes");
			continue;
		}
		if (folder_count >= CUSTOM_EDITION_MODS_MAXIMUM)
		{
			error(_error_silent, "mods: '%s' is not looked at: the mods folder's first %d are", data.cFileName,
				CUSTOM_EDITION_MODS_MAXIMUM);
			continue;
		}
		csstrcpy(folders[folder_count++], data.cFileName);
	} while (FindNextFileA(find, &data));
	CloseHandle(find);

	qsort(folders, (size_t)folder_count, sizeof(folders[0]), mod_folder_compare);
	for (folder_index = 0; folder_index < folder_count; folder_index++)
	{
		mod_add(folders[folder_index]);
	}
	qsort(globals->maps, (size_t)globals->map_count, sizeof(globals->maps[0]), mod_map_compare);
	error(_error_silent, "mods: %d maps of %d mods for the campaign's level list", globals->map_count, globals->mod_count);

	return globals->map_count;
}

void custom_edition_mods_look_again(
	void)
{
	custom_edition_mods_globals.looked_for = FALSE;

	return;
}

char const *custom_edition_mods_map_name(
	short map_index)
{
	struct custom_edition_mods_globals *globals = &custom_edition_mods_globals;

	return map_index >= 0 && map_index < globals->map_count ? globals->maps[map_index].name : NULL;
}

char const *custom_edition_mods_map_title(
	short map_index)
{
	struct custom_edition_mods_globals *globals = &custom_edition_mods_globals;

	return map_index >= 0 && map_index < globals->map_count ? globals->maps[map_index].title : NULL;
}

char const *custom_edition_mods_map_description(
	short map_index)
{
	struct custom_edition_mods_globals *globals = &custom_edition_mods_globals;

	if (map_index < 0 || map_index >= globals->map_count)
	{
		return NULL;
	}

	return globals->maps[map_index].description;
}

char const *custom_edition_mods_map_mod_title(
	short map_index)
{
	struct custom_edition_mods_globals *globals = &custom_edition_mods_globals;

	return map_index >= 0 && map_index < globals->map_count ?
		globals->mods[globals->maps[map_index].mod_index].title :
		NULL;
}

char const *custom_edition_mods_map_picture_path(
	short map_index)
{
	struct custom_edition_mods_globals *globals = &custom_edition_mods_globals;

	return map_index >= 0 && map_index < globals->map_count && globals->maps[map_index].has_picture ?
		globals->maps[map_index].picture_path :
		NULL;
}

boolean custom_edition_mods_map_path(
	char const *name,
	char *path,
	long path_size)
{
	struct custom_edition_mod_map const *map = mod_map_named(name);

	if (!map ||
		snprintf(path, (size_t)path_size, "%s%s\\maps\\%s.map", CUSTOM_EDITION_MODS_DIRECTORY,
			custom_edition_mods_globals.mods[map->mod_index].folder, map->stem) >= path_size)
	{
		return FALSE;
	}

	return TRUE;
}

boolean custom_edition_mods_level_name(
	char const *level_name)
{
	long prefix_length = (long)strlen(CUSTOM_EDITION_MODS_LEVEL_NAME_PREFIX);
	char const *name;
	long length;

	if (!level_name || _strnicmp(level_name, CUSTOM_EDITION_MODS_LEVEL_NAME_PREFIX, (size_t)prefix_length))
	{
		return FALSE;
	}
	name = level_name + prefix_length;
	for (length = 0; name[length]; length++)
	{
		char character = lower_case(name[length]);

		if (length >= CUSTOM_EDITION_MODS_MAXIMUM_NAME_LENGTH ||
			!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
			character == '_' || character == '-'))
		{
			return FALSE;
		}
	}

	return length > 0;
}

char const *custom_edition_mods_next_level(
	char const *level_name)
{
	struct custom_edition_mods_globals *globals = &custom_edition_mods_globals;
	struct custom_edition_mod_map const *map;
	struct custom_edition_mod_map const *next;
	short stock_index;

	if (!custom_edition_mods_level_name(level_name) || !(map = mod_map_named(level_name)))
	{
		return NULL;
	}
	next = map + 1;
	if (next >= globals->maps + globals->map_count || next->mod_index != map->mod_index)
	{
		return NULL;
	}
	/* (the next its campaign lists; without a campaign list, the next
	stock level's map) */
	if (globals->mods[map->mod_index].has_campaign_info)
	{
		return map->order < UNLISTED_ORDER && next->order < UNLISTED_ORDER ? next->level_name : NULL;
	}
	stock_index = stock_level_index(map->stem);
	if (stock_index == NONE || stock_index + 1 >= NUMBEROF(stock_level_names) ||
		csstrcmp(next->stem, stock_level_names[stock_index + 1]))
	{
		return NULL;
	}

	return next->level_name;
}
