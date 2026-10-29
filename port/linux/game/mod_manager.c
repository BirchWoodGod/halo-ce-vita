/*
MOD_MANAGER.C

(port) The mod manager (mod_manager.h).

What is listed:
- Each .map and .yelo file of the maps folder that is not one of the Xbox's
  own levels (map_share_name_is_stock) nor a resource map (bitmaps, sounds,
  loc, ui). What it is comes from the cache file loader's own check of its
  header and tag index (custom_edition_cache_file_check, cache_file_identify):
  an Xbox cache of a multiplayer scenario, which must also be one the cache
  partition takes (custom_edition_cache_xbox_multiplayer: not too large, and
  named inside as its file is when that is an Xbox level's name; a renamed
  copy otherwise); a Custom Edition cache of a multiplayer or a
  solo scenario (a campaign map, played alone or as co-op), which needs PC
  maps on and the resource maps it takes tags from
  (custom_edition_cache_missing_resource_maps); anything else fails, with
  the loader's words for why.
- Each folder of the mods folder, a Master Chief Collection mod, by the mods
  folder's own survey (custom_edition_mods_survey): its maps that are Halo:
  CE Anniversary campaign maps, its title, the first problem found. A mod
  plays in the campaign, alone and offline, with PC maps and MCC mods on.

The names come from the folders, untrusted: a map's file name is used for
a path only when it is one this unit takes (mod_manager_file_name_safe:
printable ASCII, none of what a path or a file system takes for something
else, no "..", no device name), a mod's only when the mods folder takes it
(custom_edition_mods_folder_name_valid); any other is listed, made
printable, and never opened, turned off or deleted. A delete is only ever of
a file of the maps folder or a folder of the mods folder, and within that
folder of names that are not paths, never through a link (the link alone is
deleted: posix_is_link), at most MAXIMUM_DELETED_FILES files.

Turning off is the settings panel's list: HALO_MAPS_DISABLED (the maps
folder's maps, by their file's name; custom_edition_maps.c leaves them out
of the level lists, the bots' playlist and map sharing) and
HALO_MODS_DISABLED (the mods, by their key; custom_edition_mods.c leaves
them out of the campaign's list), set as the panel sets a row
(halo_test_setting_hook: vita_settings_set keeps them in settings.txt).

Where a map came from: map sharing writes the maps it installs into
MOD_MANAGER_RECEIVED_FILE (mod_manager_map_received), a line each; others
were copied in by hand.
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "cseries_windows.h"
#include "cache/cache_files.h"
#include "cache_file_formats.h"
#include "custom_edition_cache.h"
#include "custom_edition_maps.h"
#include "custom_edition_mods.h"
#include "map_share_protocol.h"
#include "mod_manager.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/files.h"
#include "../src/lang.h"
#include "../src/posix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the platform layer's */
void platform_log(char const *format, ...);
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);
int setenv(const char *name, const char *value, int overwrite);
int halo_custom_edition_enabled(void);
int halo_mcc_mods_enabled(void);
int halo_cache_map_in_use(char const *name);
boolean cache_files_forget_cached_map(char const *map_name);
extern int (*halo_test_setting_hook)(const char *variable, const char *value);
extern volatile unsigned long halo_settings_generation;

/* ---------- constants */

#define PATH_BYTES 320
#define LIST_BYTES 1024
#define RECEIVED_FILE_BYTES 4096
/* a map's file name taken: as long as a mod's folder's (47 characters) */
#define MAXIMUM_FILE_NAME_LENGTH (HALO_MODS_NAME_BYTES - 1)
/* the level lists take a map of a name no longer than this
(custom_edition_maps.c) */
#define MAXIMUM_LISTED_NAME_LENGTH 25
/* a mod's folder: how deep it is gone through, and how many files at most
are counted or deleted */
#define MAXIMUM_FOLDER_DEPTH 8
#define MAXIMUM_DELETED_FILES 4096
/* the names a folder's listing hands over at a time to be deleted */
#define DELETE_BATCH 32

/* ---------- globals */

volatile int halo_mods_request;
volatile int halo_mods_answer;
volatile int halo_mods_target_kind;
char halo_mods_target[HALO_MODS_NAME_BYTES];
struct halo_mods_inventory halo_mods_inventory;

/* the game's own look (the Halo 3 screen's), apart from the one the panel
reads */
static struct halo_mods_inventory mod_manager_inventory;

/* names never listed: Custom Edition's resource maps and the user interface
map (map_share_protocol.c's reserved names) */
static char const *const reserved_names[] = { "bitmaps", "sounds", "loc", "ui" };

/* names Windows keeps for devices (map_share_protocol.c's) */
static char const *const device_names[] =
{
	"con", "prn", "aux", "nul",
	"com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
	"lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
};

/* ---------- private code */

static boolean name_in(
	char const *name,
	char const *const *names,
	long count)
{
	long index;

	for (index = 0; index < count; index++)
	{
		if (!csstrcasecmp(name, names[index]))
		{
			return TRUE;
		}
	}

	return FALSE;
}

/* Whether `name` (a file's name without its extension) is one this unit uses
in a path: printable ASCII, none of /\:*?"<>|%, (',' is the lists'
separator), no "..", not starting with a dot or a space nor ending with a
dot or a space, no device name before its first dot, at most
MAXIMUM_FILE_NAME_LENGTH characters. As map_share_name_valid, longer. */
static boolean mod_manager_file_name_safe(
	char const *name)
{
	char stem[HALO_MODS_NAME_BYTES];
	long length;

	if (!name || !name[0] || name[0] == '.' || name[0] == ' ')
	{
		return FALSE;
	}
	for (length = 0; name[length]; length++)
	{
		char character = name[length];

		if (length >= MAXIMUM_FILE_NAME_LENGTH || character < ' ' || character > '~' ||
			strchr("/\\:*?\"<>|%,", character) || (character == '.' && name[length + 1] == '.'))
		{
			return FALSE;
		}
	}
	if (name[length - 1] == '.' || name[length - 1] == ' ')
	{
		return FALSE;
	}
	for (length = 0; name[length] && name[length] != '.'; length++)
	{
		stem[length] = name[length];
	}
	stem[length] = 0;

	return !name_in(stem, device_names, NUMBEROF(device_names));
}

/* `text` as the player sees a name: printable ASCII, others as '_' */
static void printable_copy(
	char *out,
	long size,
	char const *text)
{
	long length;

	for (length = 0; text[length] && length < size - 1; length++)
	{
		out[length] = text[length] >= ' ' && text[length] <= '~' ? text[length] : '_';
	}
	out[length] = 0;

	return;
}

/* whether the comma list `list` names `name` (either case) */
static boolean list_names(
	char const *list,
	char const *name)
{
	long length = (long)strlen(name);

	while (length && list && *list)
	{
		long entry = (long)strcspn(list, ",");

		if (entry == length && !_strnicmp(list, name, (size_t)length))
		{
			return TRUE;
		}
		list += entry;
		if (*list == ',')
		{
			list++;
		}
	}

	return FALSE;
}

/* `list` again, with `name` (once, at the end) or without it, into `out` */
static void list_change(
	char *out,
	long size,
	char const *list,
	char const *name,
	boolean named)
{
	long length = 0;
	long name_length = (long)strlen(name);

	out[0] = 0;
	while (list && *list)
	{
		long entry = (long)strcspn(list, ",");

		if (entry && !(entry == name_length && !_strnicmp(list, name, (size_t)name_length)) &&
			length + entry + 2 < size)
		{
			length += snprintf(out + length, (size_t)(size - length), "%s%.*s", length ? "," : "", (int)entry, list);
		}
		list += entry;
		if (*list == ',')
		{
			list++;
		}
	}
	if (named && length + name_length + 2 < size)
	{
		snprintf(out + length, (size_t)(size - length), "%s%s", length ? "," : "", name);
	}

	return;
}

/* a list set as the settings panel sets a row (kept in settings.txt on the
Vita), else in the environment alone */
static void list_set(
	char const *variable,
	char const *value)
{
	if (!(halo_test_setting_hook && halo_test_setting_hook(variable, value)))
	{
		setenv(variable, value, 1);
		__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
	}

	return;
}

/* whether the file `path` (the game's d:\ path) is a link, not followed */
static boolean mod_manager_link(
	char const *path)
{
	char host_path[1024];

	platform_translate_path(path, host_path, sizeof(host_path));

	return posix_is_link(host_path) != 0;
}

/* the received list's text (at most RECEIVED_FILE_BYTES - 1 of it) */
static long received_read(
	char *text)
{
	FILE *stream = fopen(MOD_MANAGER_RECEIVED_FILE, "rb");
	long size = 0;

	if (stream)
	{
		size = (long)fread(text, 1, RECEIVED_FILE_BYTES - 1, stream);
		fclose(stream);
	}
	text[size] = 0;

	return size;
}

/* whether map sharing installed the map file `file` (name and extension) */
static boolean received_names(
	char const *text,
	char const *file)
{
	long length = (long)strlen(file);

	while (*text)
	{
		long line = (long)strcspn(text, "\r\n");

		if (line == length && !_strnicmp(text, file, (size_t)length))
		{
			return TRUE;
		}
		text += line;
		while (*text == '\r' || *text == '\n')
		{
			text++;
		}
	}

	return FALSE;
}

/* the received list written again, with `file` or without it */
static void received_change(
	char const *file,
	boolean named)
{
	char text[RECEIVED_FILE_BYTES];
	char kept[RECEIVED_FILE_BYTES];
	char const *at = text;
	long length = 0;
	long file_length = (long)strlen(file);
	FILE *stream;

	received_read(text);
	if (received_names(text, file) == named)
	{
		return;
	}
	kept[0] = 0;
	while (*at)
	{
		long line = (long)strcspn(at, "\r\n");
		char name[HALO_MODS_NAME_BYTES];

		/* (only lines that are names this unit takes) */
		if (line > 0 && line < (long)sizeof(name) &&
			!(line == file_length && !_strnicmp(at, file, (size_t)file_length)))
		{
			csmemcpy(name, at, line);
			name[line] = 0;
			if (mod_manager_file_name_safe(name) && length + line + 2 < (long)sizeof(kept))
			{
				length += snprintf(kept + length, sizeof(kept) - (size_t)length, "%s\n", name);
			}
		}
		at += line;
		while (*at == '\r' || *at == '\n')
		{
			at++;
		}
	}
	if (named && length + file_length + 2 < (long)sizeof(kept))
	{
		length += snprintf(kept + length, sizeof(kept) - (size_t)length, "%s\n", file);
	}
	stream = fopen(MOD_MANAGER_RECEIVED_FILE, "wb");
	if (stream)
	{
		fwrite(kept, 1, (size_t)length, stream);
		fclose(stream);
	}

	return;
}

static int kilobytes_of(
	unsigned long low,
	unsigned long high)
{
	unsigned long long bytes = ((unsigned long long)high << 32) | low;
	unsigned long long kilobytes = (bytes + 1023) >> 10;

	return kilobytes > 0x7FFFFFFFULL ? 0x7FFFFFFF : (int)kilobytes;
}

/* the size of the folder `path` and everything in it, in KB, links not
followed, MAXIMUM_FOLDER_DEPTH deep at most; `*budget` files at most */
static int folder_kilobytes(
	char const *path,
	short depth,
	long *budget)
{
	char pattern[PATH_BYTES];
	WIN32_FIND_DATAA data;
	HANDLE find;
	int kilobytes = 0;

	if (depth > MAXIMUM_FOLDER_DEPTH ||
		snprintf(pattern, sizeof(pattern), "%s\\*", path) >= (int)sizeof(pattern))
	{
		return 0;
	}
	find = FindFirstFileA(pattern, &data);
	if (find == INVALID_HANDLE_VALUE)
	{
		return 0;
	}
	do
	{
		char child[PATH_BYTES];

		if (!csstrcmp(data.cFileName, ".") || !csstrcmp(data.cFileName, "..") ||
			strchr(data.cFileName, '\\') || strchr(data.cFileName, '/') || strchr(data.cFileName, ':') ||
			--*budget < 0 ||
			snprintf(child, sizeof(child), "%s\\%s", path, data.cFileName) >= (int)sizeof(child))
		{
			continue;
		}
		if (mod_manager_link(child))
		{
			continue;
		}
		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			kilobytes += folder_kilobytes(child, (short)(depth + 1), budget);
		}
		else
		{
			kilobytes += kilobytes_of(data.nFileSizeLow, data.nFileSizeHigh);
		}
	} while (FindNextFileA(find, &data) && kilobytes < 0x7FFFFFFF / 2);
	CloseHandle(find);

	return kilobytes;
}

/* Deletes the folder `path` and everything in it: a link (to a file or a
folder) is deleted itself, never followed; a name that is a path is never
joined to one (the delete then fails, the folder kept); MAXIMUM_FOLDER_DEPTH
deep at most, `*budget` files at most. The names are taken a batch at a time
before any of them is deleted (the listing is not changed under itself). */
static boolean folder_delete(
	char const *path,
	short depth,
	long *budget)
{
	char pattern[PATH_BYTES];
	char (*names)[MAX_PATH];
	boolean ok = TRUE;
	short pass;

	if (depth > MAXIMUM_FOLDER_DEPTH ||
		snprintf(pattern, sizeof(pattern), "%s\\*", path) >= (int)sizeof(pattern))
	{
		return FALSE;
	}
	names = malloc(sizeof(*names) * DELETE_BATCH);
	if (!names)
	{
		return FALSE;
	}
	for (pass = 0; ok && pass < MAXIMUM_DELETED_FILES / DELETE_BATCH + 1; pass++)
	{
		WIN32_FIND_DATAA data;
		HANDLE find = FindFirstFileA(pattern, &data);
		boolean directories[DELETE_BATCH];
		short count = 0;
		short index;

		if (find == INVALID_HANDLE_VALUE)
		{
			break;
		}
		do
		{
			if (!csstrcmp(data.cFileName, ".") || !csstrcmp(data.cFileName, ".."))
			{
				continue;
			}
			if (strchr(data.cFileName, '\\') || strchr(data.cFileName, '/') || strchr(data.cFileName, ':') ||
				csstrlen(data.cFileName) >= MAX_PATH)
			{
				error(_error_silent, "mod manager: '%s' holds a file whose name is a path: not deleted", path);
				ok = FALSE;
				break;
			}
			csstrcpy(names[count], data.cFileName);
			directories[count] = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			count++;
		} while (count < DELETE_BATCH && FindNextFileA(find, &data));
		CloseHandle(find);
		if (!ok || !count)
		{
			break;
		}
		for (index = 0; ok && index < count; index++)
		{
			char child[PATH_BYTES];

			if (--*budget < 0 ||
				snprintf(child, sizeof(child), "%s\\%s", path, names[index]) >= (int)sizeof(child))
			{
				ok = FALSE;
				break;
			}
			if (directories[index] && !mod_manager_link(child))
			{
				ok = folder_delete(child, (short)(depth + 1), budget);
			}
			else if (!DeleteFileA(child))
			{
				/* (a link to a folder, on Windows, is a folder's link) */
				ok = directories[index] && RemoveDirectoryA(child);
			}
		}
	}
	free(names);
	if (ok && !RemoveDirectoryA(path))
	{
		ok = FALSE;
	}

	return ok;
}

static void entry_needs_append(
	struct halo_mod_entry *entry,
	char const *text)
{
	long length = (long)strlen(entry->needs);

	snprintf(entry->needs + length, sizeof(entry->needs) - (size_t)length, "%s%s", length ? "; " : "", text);

	return;
}

/* the maps folder's file `name`.`extension` of `bytes` checked into `entry` */
static void map_entry_check(
	struct halo_mod_entry *entry,
	char const *name,
	char const *extension,
	char const *received_text)
{
	char path[PATH_BYTES];
	char inside[CACHE_FILE_STRING_BYTES];
	short format;
	short scenario_type;
	short status;
	boolean yelo = !csstrcasecmp(extension, "yelo");

	entry->kind = yelo ? HALO_MOD_KIND_OPENSAUCE : HALO_MOD_KIND_UNKNOWN;
	entry->deletable = mod_manager_file_name_safe(name);
	entry->enabled = !list_names(getenv("HALO_MAPS_DISABLED"), name);
	entry->source = entry->deletable && received_names(received_text, entry->file) ? HALO_MOD_SOURCE_MAP_SHARE :
		HALO_MOD_SOURCE_HAND;
	if (!entry->deletable)
	{
		snprintf(entry->why, sizeof(entry->why), "%s", T("Its file name has characters this game doesn't take"));
		return;
	}
	snprintf(path, sizeof(path), "%s%s.%s", cache_files_map_directory(), name, extension);
	status = custom_edition_cache_file_check(path, &format, &scenario_type, inside, sizeof(inside));
	if (status != _cache_file_status_ok)
	{
		snprintf(entry->why, sizeof(entry->why), "%s", T(cache_file_status_describe((enum cache_file_status)status)));
		return;
	}
	switch (format)
	{
	case _cache_file_format_xbox_cache:
		entry->kind = HALO_MOD_KIND_XBOX_MAP;
		if (scenario_type != _scenario_type_multiplayer)
		{
			snprintf(entry->why, sizeof(entry->why), "%s", T("An Xbox campaign map: only the Xbox's own levels play"));
		}
		else if (yelo)
		{
			snprintf(entry->why, sizeof(entry->why), "%s", T("An Xbox map must be a .map file"));
		}
		else if (!custom_edition_cache_xbox_multiplayer(name))
		{
			if (csstrcasecmp(inside, name))
			{
				snprintf(entry->why, sizeof(entry->why), T("It's named %.31s inside: rename the file %.31s.map"), inside,
					inside);
			}
			else
			{
				snprintf(entry->why, sizeof(entry->why), "%s", T("It's larger than an Xbox multiplayer map may be"));
			}
		}
		else
		{
			entry->valid = TRUE;
			entry->modes = HALO_MOD_MODE_MULTIPLAYER;
		}
		break;
	case _cache_file_format_custom_edition_cache:
		if (scenario_type == _scenario_type_multiplayer)
		{
			entry->kind = yelo ? HALO_MOD_KIND_OPENSAUCE : HALO_MOD_KIND_CE_MAP;
			entry->modes = HALO_MOD_MODE_MULTIPLAYER;
		}
		else if (scenario_type == _scenario_type_solo)
		{
			entry->kind = yelo ? HALO_MOD_KIND_OPENSAUCE : HALO_MOD_KIND_CE_CAMPAIGN;
			entry->modes = HALO_MOD_MODE_CAMPAIGN | HALO_MOD_MODE_COOP;
		}
		else
		{
			entry->kind = yelo ? HALO_MOD_KIND_OPENSAUCE : HALO_MOD_KIND_CE_MAP;
			snprintf(entry->why, sizeof(entry->why), "%s", T("Neither a multiplayer nor a campaign map"));
			break;
		}
		if ((long)strlen(name) > MAXIMUM_LISTED_NAME_LENGTH)
		{
			entry->modes = 0;
			snprintf(entry->why, sizeof(entry->why), T("Its name is longer than %d characters"), MAXIMUM_LISTED_NAME_LENGTH);
			break;
		}
		entry->valid = TRUE;
		if (!halo_custom_edition_enabled())
		{
			entry_needs_append(entry, T("PC maps On (Settings > Multiplayer > Modded maps)"));
		}
		{
			char missing[96];

			if (custom_edition_cache_missing_resource_maps(name, missing, sizeof(missing)))
			{
				char text[HALO_MODS_TEXT_BYTES];

				snprintf(text, sizeof(text), T("Halo PC's %.60s"), missing);
				entry_needs_append(entry, text);
			}
		}
		break;
	case _cache_file_format_mcc_cea_cache:
		snprintf(entry->why, sizeof(entry->why), "%s", T("A Halo: CE Anniversary map: it plays from the mods folder"));
		break;
	case _cache_file_format_resource_map:
		snprintf(entry->why, sizeof(entry->why), "%s", T("A resource map, not a map to play"));
		break;
	case _cache_file_format_other_cache:
		snprintf(entry->why, sizeof(entry->why), "%s", T("A map of a version this game doesn't take"));
		break;
	default:
		snprintf(entry->why, sizeof(entry->why), "%s", T("Not a Halo map"));
		break;
	}

	return;
}

/* the mods folder's survey's mod at `mod_index` into `entry` */
static void mod_entry_check(
	struct halo_mod_entry *entry,
	short mod_index)
{
	char const *folder = custom_edition_mods_mod_folder(mod_index);
	char const *key = custom_edition_mods_mod_key(mod_index);
	char const *title = custom_edition_mods_mod_title(mod_index);
	char const *problem = custom_edition_mods_mod_problem(mod_index);
	short valid = 0;
	short maps = custom_edition_mods_mod_maps(mod_index, &valid);

	entry->kind = HALO_MOD_KIND_MCC_MOD;
	entry->source = HALO_MOD_SOURCE_HAND;
	printable_copy(entry->file, sizeof(entry->file), folder ? folder : "");
	printable_copy(entry->name, sizeof(entry->name), key && key[0] ? key : entry->file);
	printable_copy(entry->title, sizeof(entry->title), title && title[0] ? title : entry->file);
	entry->deletable = custom_edition_mods_mod_folder_valid(mod_index);
	entry->enabled = key && key[0] && !custom_edition_mods_key_disabled(key);
	entry->maps = maps;
	entry->maps_valid = valid;
	entry->valid = valid > 0;
	entry->modes = entry->valid ? HALO_MOD_MODE_CAMPAIGN : 0;
	if (problem && problem[0])
	{
		printable_copy(entry->why, sizeof(entry->why), problem);
	}
	else if (!entry->valid)
	{
		snprintf(entry->why, sizeof(entry->why), "%s", T("No map in it is a Halo: CE Anniversary campaign map"));
	}
	if (entry->valid && !halo_mcc_mods_enabled())
	{
		entry_needs_append(entry, T("PC maps and MCC mods On (Settings > Multiplayer > Modded maps)"));
	}
	if (entry->deletable)
	{
		char path[PATH_BYTES];
		long budget = MAXIMUM_DELETED_FILES;

		snprintf(path, sizeof(path), "%s%s", CUSTOM_EDITION_MODS_DIRECTORY, folder);
		entry->kilobytes = folder_kilobytes(path, 0, &budget);
	}

	return;
}

static int entry_compare(
	void const *first,
	void const *second)
{
	struct halo_mod_entry const *a = first;
	struct halo_mod_entry const *b = second;
	int order = (int)csstrcasecmp(a->file, b->file);

	return order;
}

/* the look into `inventory` */
static short mod_manager_look(
	struct halo_mods_inventory *inventory)
{
	char pattern[PATH_BYTES];
	char received_text[RECEIVED_FILE_BYTES];
	WIN32_FIND_DATAA data;
	HANDLE find;
	short map_count;
	short mod_count;
	short mod_index;
	ULARGE_INTEGER free_bytes;

	csmemset(inventory, 0, sizeof(*inventory));
	received_read(received_text);
	snprintf(pattern, sizeof(pattern), "%s*", cache_files_map_directory());
	find = FindFirstFileA(pattern, &data);
	if (find != INVALID_HANDLE_VALUE)
	{
		do
		{
			struct halo_mod_entry *entry;
			char name[MAX_PATH];
			char const *dot = strrchr(data.cFileName, '.');
			long stem_length;

			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !dot || dot == data.cFileName ||
				(csstrcasecmp(dot, ".map") && csstrcasecmp(dot, ".yelo")))
			{
				continue;
			}
			stem_length = (long)(dot - data.cFileName);
			if (stem_length >= (long)sizeof(name))
			{
				continue;
			}
			csmemcpy(name, data.cFileName, stem_length);
			name[stem_length] = 0;
			if (map_share_name_is_stock(name) || name_in(name, reserved_names, NUMBEROF(reserved_names)))
			{
				continue;
			}
			if (inventory->count >= HALO_MODS_MAXIMUM - CUSTOM_EDITION_MODS_MAXIMUM)
			{
				error(_error_silent, "mod manager: the maps folder's first %d maps are listed",
					HALO_MODS_MAXIMUM - CUSTOM_EDITION_MODS_MAXIMUM);
				break;
			}
			entry = &inventory->entries[inventory->count++];
			printable_copy(entry->name, sizeof(entry->name), name);
			printable_copy(entry->file, sizeof(entry->file), data.cFileName);
			printable_copy(entry->title, sizeof(entry->title), name);
			entry->kilobytes = kilobytes_of(data.nFileSizeLow, data.nFileSizeHigh);
			/* (a name cut short, or made printable, is not the file's: never
			opened, turned off or deleted) */
			if ((long)strlen(data.cFileName) >= (long)sizeof(entry->file) || csstrcmp(entry->name, name) ||
				csstrcmp(entry->file, data.cFileName))
			{
				entry->kind = HALO_MOD_KIND_UNKNOWN;
				entry->enabled = TRUE;
				snprintf(entry->why, sizeof(entry->why), "%s", T("Its file name has characters this game doesn't take"));
				continue;
			}
			map_entry_check(entry, name, dot + 1, received_text);
		} while (FindNextFileA(find, &data));
		CloseHandle(find);
	}
	map_count = inventory->count;
	qsort(inventory->entries, (size_t)map_count, sizeof(inventory->entries[0]), entry_compare);

	mod_count = custom_edition_mods_survey();
	for (mod_index = 0; mod_index < mod_count && inventory->count < HALO_MODS_MAXIMUM; mod_index++)
	{
		mod_entry_check(&inventory->entries[inventory->count++], mod_index);
	}
	for (mod_index = 0; mod_index < inventory->count; mod_index++)
	{
		inventory->used_kilobytes += inventory->entries[mod_index].kilobytes;
		if (inventory->used_kilobytes < 0)
		{
			inventory->used_kilobytes = 0x7FFFFFFF;
		}
	}
	free_bytes.QuadPart = 0;
	if (GetDiskFreeSpaceExA("d:\\", &free_bytes, NULL, NULL))
	{
		inventory->free_kilobytes = kilobytes_of(free_bytes.LowPart, free_bytes.HighPart);
	}
	platform_log("mod manager: %d maps and %d mods listed, %d KB used, %d KB free", map_count,
		inventory->count - map_count, inventory->used_kilobytes, inventory->free_kilobytes);

	return (short)inventory->count;
}

/* what turning the map `name` off or on changed, as the lists see it now
(said for the player's report and the tests) */
static void map_state_said(
	char const *name,
	boolean enabled)
{
	char level_name[96];

	/* (the lists look again: custom_edition_maps.c reads the off list each
	time it looks) */
	custom_edition_maps_look_again();
	snprintf(level_name, sizeof(level_name), "levels\\test\\%s\\%s", name, name);
	platform_log("mod manager: %s turned %s: map lists %s, bots playlist %s, map sharing %s", name,
		enabled ? "on" : "off",
		custom_edition_maps_display_index(level_name) != NONE ? "yes" : "no",
		custom_edition_maps_multiplayer_listed(name) ? "yes" : "no",
		custom_edition_maps_shareable(level_name) ? "yes" : "no");

	return;
}

/* ---------- public code */

short mod_manager_scan(
	void)
{
	return mod_manager_look(&mod_manager_inventory);
}

short mod_manager_count(
	void)
{
	return (short)mod_manager_inventory.count;
}

struct halo_mod_entry const *mod_manager_entry(
	short index)
{
	return index >= 0 && index < mod_manager_inventory.count ? &mod_manager_inventory.entries[index] : NULL;
}

void mod_manager_totals(
	int *used_kilobytes,
	int *free_kilobytes)
{
	*used_kilobytes = mod_manager_inventory.used_kilobytes;
	*free_kilobytes = mod_manager_inventory.free_kilobytes;

	return;
}

short mod_manager_find(
	char const *file,
	int kind)
{
	short index;

	for (index = 0; file && index < mod_manager_inventory.count; index++)
	{
		struct halo_mod_entry const *entry = &mod_manager_inventory.entries[index];

		if ((entry->kind == HALO_MOD_KIND_MCC_MOD) == (kind == HALO_MOD_KIND_MCC_MOD) && !csstrcmp(entry->file, file))
		{
			return index;
		}
	}

	return NONE;
}

boolean mod_manager_set_enabled(
	short index,
	boolean enabled)
{
	struct halo_mod_entry *entry = index >= 0 && index < mod_manager_inventory.count ?
		&mod_manager_inventory.entries[index] : NULL;
	boolean mod;
	char const *variable;
	char list[LIST_BYTES];

	if (!entry || !entry->deletable)
	{
		return FALSE;
	}
	mod = entry->kind == HALO_MOD_KIND_MCC_MOD;
	variable = mod ? "HALO_MODS_DISABLED" : "HALO_MAPS_DISABLED";
	/* (a mod with no key has nothing to be named by) */
	if (mod && !custom_edition_mods_folder_name_valid(entry->file))
	{
		return FALSE;
	}
	list_change(list, sizeof(list), getenv(variable), entry->name, !enabled);
	list_set(variable, list);
	entry->enabled = !list_names(getenv(variable), entry->name);
	if (mod)
	{
		custom_edition_mods_look_again();
		custom_edition_maps_look_again();
		platform_log("mod manager: the mod %s (%s) turned %s", entry->file, entry->name, entry->enabled ? "on" : "off");
	}
	else
	{
		map_state_said(entry->name, entry->enabled);
	}

	return entry->enabled == enabled;
}

int mod_manager_delete(
	char const *file,
	int kind)
{
	char path[PATH_BYTES];
	char list[LIST_BYTES];
	int answer = HALO_MODS_ANSWER_FAILED;

	if (!file || !file[0] || (long)strlen(file) >= HALO_MODS_NAME_BYTES)
	{
		return HALO_MODS_ANSWER_FAILED;
	}
	if (kind == HALO_MOD_KIND_MCC_MOD)
	{
		short count;
		short mod_index;
		short which;
		long budget = MAXIMUM_DELETED_FILES;
		char key[HALO_MODS_NAME_BYTES];

		/* (only a folder of the mods folder of a name it takes) */
		if (!custom_edition_mods_folder_name_valid(file) ||
			snprintf(path, sizeof(path), "%s%s", CUSTOM_EDITION_MODS_DIRECTORY, file) >= (int)sizeof(path))
		{
			platform_log("mod manager: the mod folder '%.48s' is not one this game deletes", file);
			return HALO_MODS_ANSWER_FAILED;
		}
		count = custom_edition_mods_survey();
		for (mod_index = 0; mod_index < count; mod_index++)
		{
			if (custom_edition_mods_mod_folder_valid(mod_index) && !csstrcmp(custom_edition_mods_mod_folder(mod_index), file))
			{
				break;
			}
		}
		if (mod_index == count)
		{
			platform_log("mod manager: no mod folder '%s'", file);
			return HALO_MODS_ANSWER_FAILED;
		}
		for (which = 0; custom_edition_mods_mod_map_name(mod_index, which); which++)
		{
			if (halo_cache_map_in_use(custom_edition_mods_mod_map_name(mod_index, which)))
			{
				platform_log("mod manager: the mod %s is in use (%s): not deleted", file,
					custom_edition_mods_mod_map_name(mod_index, which));
				return HALO_MODS_ANSWER_IN_USE;
			}
		}
		csstrncpy(key, custom_edition_mods_mod_key(mod_index), sizeof(key) - 1);
		key[sizeof(key) - 1] = 0;
		if (mod_manager_link(path))
		{
			/* (the mods folder's link to a folder elsewhere: the link alone) */
			answer = DeleteFileA(path) || RemoveDirectoryA(path) ? HALO_MODS_ANSWER_DELETED : HALO_MODS_ANSWER_FAILED;
		}
		else
		{
			answer = folder_delete(path, 0, &budget) ? HALO_MODS_ANSWER_DELETED : HALO_MODS_ANSWER_FAILED;
		}
		if (key[0] && custom_edition_mods_key_disabled(key))
		{
			list_change(list, sizeof(list), getenv("HALO_MODS_DISABLED"), key, FALSE);
			list_set("HALO_MODS_DISABLED", list);
		}
		custom_edition_mods_look_again();
		custom_edition_maps_look_again();
		platform_log("mod manager: the mod folder %s %s", file,
			answer == HALO_MODS_ANSWER_DELETED ? "deleted" : "could not be deleted (some of it may be left)");
	}
	else
	{
		char name[HALO_MODS_NAME_BYTES];
		char const *dot = strrchr(file, '.');
		char const *other;
		long stem_length = dot ? (long)(dot - file) : 0;
		struct file_reference reference;

		/* (only a .map or .yelo of the maps folder, of a name this unit
		takes, never the Xbox's own nor a resource map) */
		if (!dot || stem_length <= 0 || (csstrcasecmp(dot, ".map") && csstrcasecmp(dot, ".yelo")))
		{
			return HALO_MODS_ANSWER_FAILED;
		}
		csmemcpy(name, file, stem_length);
		name[stem_length] = 0;
		if (!mod_manager_file_name_safe(name) || map_share_name_is_stock(name) ||
			name_in(name, reserved_names, NUMBEROF(reserved_names)) ||
			snprintf(path, sizeof(path), "%s%s", cache_files_map_directory(), file) >= (int)sizeof(path))
		{
			platform_log("mod manager: the map '%.48s' is not one this game deletes", file);
			return HALO_MODS_ANSWER_FAILED;
		}
		if (!file_exists(file_reference_create_from_path(&reference, path, FALSE)))
		{
			platform_log("mod manager: no map %s", file);
			return HALO_MODS_ANSWER_FAILED;
		}
		/* (nor one loaded, nor its copy in the cache partition, which would
		be played in its place: as map sharing replaces one) */
		if (halo_cache_map_in_use(name) || !cache_files_forget_cached_map(name))
		{
			platform_log("mod manager: the map %s is in use: not deleted", file);
			return HALO_MODS_ANSWER_IN_USE;
		}
		if (remove(path) != 0)
		{
			platform_log("mod manager: the map %s could not be deleted", file);
			return HALO_MODS_ANSWER_FAILED;
		}
		answer = HALO_MODS_ANSWER_DELETED;
		received_change(file, FALSE);
		/* (and a download of it that was cut off, kept to go on: map_share.c) */
		snprintf(path, sizeof(path), "%s%s.download", cache_files_map_directory(), name);
		remove(path);
		snprintf(path, sizeof(path), "%s%s.resume", cache_files_map_directory(), name);
		remove(path);
		/* (its picture, description and place in the off list go once no
		.map or .yelo of the name is left) */
		other = !csstrcasecmp(dot, ".map") ? "yelo" : "map";
		snprintf(path, sizeof(path), "%s%s.%s", cache_files_map_directory(), name, other);
		if (!file_exists(file_reference_create_from_path(&reference, path, FALSE)))
		{
			snprintf(path, sizeof(path), "%s%s.bmp", cache_files_map_directory(), name);
			remove(path);
			snprintf(path, sizeof(path), "%s%s.txt", cache_files_map_directory(), name);
			remove(path);
			if (list_names(getenv("HALO_MAPS_DISABLED"), name))
			{
				list_change(list, sizeof(list), getenv("HALO_MAPS_DISABLED"), name, FALSE);
				list_set("HALO_MAPS_DISABLED", list);
			}
		}
		custom_edition_cache_map_identity_forget(name);
		custom_edition_maps_look_again();
		platform_log("mod manager: the map %s deleted", file);
	}
	mod_manager_scan();

	return answer;
}

void mod_manager_map_received(
	char const *name,
	char const *extension)
{
	char file[HALO_MODS_NAME_BYTES];

	if (!name || !extension || !mod_manager_file_name_safe(name) ||
		snprintf(file, sizeof(file), "%s%s", name, extension) >= (int)sizeof(file))
	{
		return;
	}
	received_change(file, TRUE);

	return;
}

void mod_manager_update(
	boolean main_menu_loaded)
{
	int request = __atomic_load_n(&halo_mods_request, __ATOMIC_ACQUIRE);
	int answer = HALO_MODS_ANSWER_SCANNED;

	(void)main_menu_loaded;
	if (request == HALO_MODS_REQUEST_NONE)
	{
		return;
	}
	if (request == HALO_MODS_REQUEST_DELETE)
	{
		char target[HALO_MODS_NAME_BYTES];

		csmemcpy(target, halo_mods_target, sizeof(target));
		target[sizeof(target) - 1] = 0;
		answer = mod_manager_delete(target, halo_mods_target_kind);
		platform_log("mod manager: the settings panel's delete of %s: %s", target,
			answer == HALO_MODS_ANSWER_DELETED ? "deleted" : answer == HALO_MODS_ANSWER_IN_USE ? "in use" : "failed");
	}
	else
	{
		mod_manager_scan();
	}
	csmemcpy(&halo_mods_inventory, &mod_manager_inventory, sizeof(halo_mods_inventory));
	halo_mods_answer = answer;
	__atomic_store_n(&halo_mods_request, HALO_MODS_REQUEST_NONE, __ATOMIC_RELEASE);

	return;
}

char const *mod_manager_kind_name(
	int kind)
{
	switch (kind)
	{
	case HALO_MOD_KIND_XBOX_MAP:
		return T("Xbox map");
	case HALO_MOD_KIND_CE_MAP:
		return T("PC map");
	case HALO_MOD_KIND_CE_CAMPAIGN:
		return T("PC campaign map");
	case HALO_MOD_KIND_OPENSAUCE:
		return T("OpenSauce map");
	case HALO_MOD_KIND_MCC_MOD:
		return T("MCC mod");
	default:
		return T("Unknown file");
	}
}

char const *mod_manager_source_name(
	int source)
{
	return source == HALO_MOD_SOURCE_MAP_SHARE ? T("Received through map sharing") : T("Copied in by hand");
}

void mod_manager_modes_text(
	int modes,
	char *text,
	int size)
{
	int length = 0;

	text[0] = 0;
	if (modes & HALO_MOD_MODE_MULTIPLAYER)
	{
		length += snprintf(text + length, (size_t)(size - length), "%s", T("Multiplayer"));
	}
	if ((modes & HALO_MOD_MODE_CAMPAIGN) && length < size)
	{
		length += snprintf(text + length, (size_t)(size - length), "%s%s", length ? ", " : "", T("Campaign"));
	}
	if ((modes & HALO_MOD_MODE_COOP) && length < size)
	{
		length += snprintf(text + length, (size_t)(size - length), "%s%s", length ? ", " : "", T("Co-op"));
	}
	if ((modes & HALO_MOD_MODE_FORGE) && length < size)
	{
		length += snprintf(text + length, (size_t)(size - length), "%s%s", length ? ", " : "", T("Forge"));
	}
	if (!length)
	{
		snprintf(text, (size_t)size, "%s", T("None"));
	}

	return;
}
