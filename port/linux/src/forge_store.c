/*
FORGE_STORE.C

Forge's map variants on the memory card (forge_store.h). The folders and
files are this machine's own paths (the platform layer's, not the game's
Xbox drives): ux0:data/haloce-vita/forge/<map>/<name>.hmv on the Vita,
<save root>/forge/<map>/<name>.hmv on Linux and PC.

What is never done here: open a name this port did not make (a folder whose
name is not a map's, a file whose name is not a map variant's are passed
over), take a byte of a file before the validator has passed all of it
(forge_file_decode, its folder's map expected), or leave a file half
written. A save writes <name>.hmv.tmp, checks the write (every byte, the
close), reads it back through the validator, and only then puts it in
place: the old file renamed <name>.hmv.old, the new one renamed in, the old
one deleted (a file system that does not rename over a file - the Vita's -
is served the same way). Whatever fails, the temporary file is deleted and
the old one is where it was. Before writing, the card must have the file's
size and FORGE_STORE_SPACE_MARGIN free.

(debug, not in release builds) HALO_TEST_FORGE_WRITE_FAIL=1: every save's
write stops half way, as on a full card (run_forge_test.sh).
*/

#include "platform.h"
#include "posix.h"
#include "lang.h"
#include "forge_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* ---------- constants */

#define FORGE_STORE_PATH_SIZE 512
#define FORGE_STORE_SPACE_MARGIN (256 * 1024)
#define FORGE_STORE_TEMPORARY_EXTENSION ".tmp"
#define FORGE_STORE_OLD_EXTENSION ".old"
/* the most map folders looked in */
#define FORGE_STORE_MAXIMUM_MAPS 64
/* a save under a title's name: the most numbers tried after it */
#define FORGE_STORE_MAXIMUM_NUMBER 999

#ifdef HALO_VITA
#define FORGE_STORE_ROOT "ux0:data/haloce-vita/forge"
#endif

/* ---------- private code */

static void forge_store_why(
	char *why,
	int why_size,
	char const *text)
{
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "%s", text);
}

static int forge_store_root(
	char *path,
	int size)
{
#ifdef HALO_VITA
	return snprintf(path, (size_t)size, "%s", FORGE_STORE_ROOT) < size;
#else
	return snprintf(path, (size_t)size, "%s/forge", platform_save_root()) < size;
#endif
}

static int forge_store_file_path(
	char const *map_name,
	char const *file_name,
	char const *extension,
	char *path,
	int size)
{
	char folder[FORGE_STORE_PATH_SIZE];

	if (!forge_file_name_valid(file_name) || !forge_store_folder(map_name, 0, folder, sizeof(folder)))
		return 0;
	return snprintf(path, (size_t)size, "%s/%s%s", folder, file_name, extension ? extension : "") < size;
}

static int forge_store_file_exists(
	char const *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && !(information.flags & _posix_file_is_directory);
}

/* a file's bytes, at most FORGE_FILE_MAXIMUM_SIZE (one larger is read one
byte past it, and refused by the validator); the count, or -1 */
static int forge_store_read(
	char const *path,
	unsigned char *bytes,
	int capacity)
{
	FILE *file = fopen(path, "rb");
	size_t count;

	if (!file)
		return -1;
	count = fread(bytes, 1, (size_t)capacity, file);
	fclose(file);
	return (int)count;
}

static int forge_store_write_fails(
	void)
{
#ifndef HALO_RELEASE
	char const *setting = getenv("HALO_TEST_FORGE_WRITE_FAIL");

	return setting && atoi(setting) != 0;
#else
	return 0;
#endif
}

/* bytes written to path whole: 1, or 0 (the file deleted) */
static int forge_store_write(
	char const *path,
	unsigned char const *bytes,
	int size)
{
	FILE *file = fopen(path, "wb");
	int written = 1;
	int count = size;

	if (!file)
		return 0;
	if (forge_store_write_fails())
	{
		count = size / 2;
		written = 0;
	}
	if (fwrite(bytes, 1, (size_t)count, file) != (size_t)count)
		written = 0;
	if (fflush(file) != 0)
		written = 0;
	if (fclose(file) != 0)
		written = 0;
	if (!written)
		remove(path);
	return written;
}

static void forge_store_entry_fill(
	struct forge_store_entry *entry,
	char const *map_name,
	char const *file_name,
	struct forge_file_layout const *layout,
	unsigned long size)
{
	memset(entry, 0, sizeof(*entry));
	snprintf(entry->map_name, sizeof(entry->map_name), "%s", map_name);
	snprintf(entry->file_name, sizeof(entry->file_name), "%s", file_name);
	entry->size = size;
	if (!layout)
	{
		entry->damaged = 1;
		snprintf(entry->title, sizeof(entry->title), "%s", file_name);
		return;
	}
	snprintf(entry->title, sizeof(entry->title), "%s", layout->title);
	snprintf(entry->author, sizeof(entry->author), "%s", layout->author);
	snprintf(entry->description, sizeof(entry->description), "%s", layout->description);
	snprintf(entry->date, sizeof(entry->date), "%s", layout->date);
	entry->objects = layout->record_count;
	entry->packs = layout->pack_count;
}

/* a map's folder's map variants added to the list */
static int forge_store_list_map(
	char const *map_name,
	struct forge_store_entry *entries,
	int count,
	int capacity,
	unsigned char *bytes,
	struct forge_file_layout *layout)
{
	char folder[FORGE_STORE_PATH_SIZE];
	char name[256];
	void *directory;

	if (!forge_store_folder(map_name, 0, folder, sizeof(folder)))
		return count;
	directory = posix_directory_open(folder);
	if (!directory)
		return count;
	while (count < capacity && posix_directory_next(directory, name, sizeof(name)))
	{
		char path[FORGE_STORE_PATH_SIZE];
		char why[96];
		int size;
		int length = (int)strlen(name);

		/* (a save's temporary file left by a stop part way: deleted) */
		if (length > 8 && !strcmp(name + length - 8, FORGE_FILE_EXTENSION FORGE_STORE_TEMPORARY_EXTENSION))
		{
			name[length - 4] = 0;
			if (forge_file_name_valid(name) && forge_store_file_path(map_name, name, FORGE_STORE_TEMPORARY_EXTENSION, path,
				sizeof(path)))
			{
				remove(path);
				platform_log("forge store: %s/%s.tmp, a save's temporary file, deleted", map_name, name);
			}
			continue;
		}
		if (!forge_file_name_valid(name) || !forge_store_file_path(map_name, name, NULL, path, sizeof(path)) ||
			!forge_store_file_exists(path))
		{
			continue;
		}
		size = forge_store_read(path, bytes, FORGE_FILE_MAXIMUM_SIZE + 1);
		if (size < 0)
			continue;
		if (forge_file_decode(bytes, size, map_name, layout, why, sizeof(why)) != _forge_file_ok)
		{
			platform_log("forge store: %s/%s refused: %s", map_name, name, why);
			forge_store_entry_fill(&entries[count++], map_name, name, NULL, (unsigned long)size);
			continue;
		}
		forge_store_entry_fill(&entries[count++], map_name, name, layout, (unsigned long)size);
	}
	posix_directory_close(directory);
	return count;
}

static int forge_store_entry_compare(
	void const *a,
	void const *b)
{
	struct forge_store_entry const *left = a;
	struct forge_store_entry const *right = b;
	int order = strcmp(left->map_name, right->map_name);

	if (order)
		return order;
	if (left->damaged != right->damaged)
		return left->damaged - right->damaged;
	order = strcasecmp(left->title, right->title);
	return order ? order : strcmp(left->file_name, right->file_name);
}

/* ---------- public code */

int forge_store_folder(
	char const *map_name,
	int make,
	char *path,
	int size)
{
	char root[FORGE_STORE_PATH_SIZE];

	if (!forge_store_root(root, sizeof(root)))
		return 0;
	if (make)
		posix_make_directory(root);
	if (!map_name)
		return snprintf(path, (size_t)size, "%s", root) < size;
	if (!forge_file_map_name_valid(map_name) || snprintf(path, (size_t)size, "%s/%s", root, map_name) >= size)
		return 0;
	if (make)
		posix_make_directory(path);
	return 1;
}

int forge_store_list(
	char const *map_name,
	struct forge_store_entry *entries,
	int capacity)
{
	unsigned char *bytes;
	struct forge_file_layout *layout;
	int count = 0;

	if (!entries || capacity <= 0)
		return 0;
	if (capacity > FORGE_STORE_MAXIMUM_LISTED)
		capacity = FORGE_STORE_MAXIMUM_LISTED;
	bytes = malloc(FORGE_FILE_MAXIMUM_SIZE + 1);
	layout = malloc(sizeof(*layout));
	if (!bytes || !layout)
	{
		free(bytes);
		free(layout);
		return 0;
	}
	if (map_name)
	{
		count = forge_store_list_map(map_name, entries, 0, capacity, bytes, layout);
	}
	else
	{
		char root[FORGE_STORE_PATH_SIZE];
		char maps[FORGE_STORE_MAXIMUM_MAPS][FORGE_FILE_MAP_NAME_SIZE];
		char name[256];
		int map_count = 0;
		int map_index;
		void *directory;

		/* (the map folders' names first: each is listed after the walk) */
		if (forge_store_folder(NULL, 0, root, sizeof(root)) && (directory = posix_directory_open(root)) != NULL)
		{
			while (map_count < FORGE_STORE_MAXIMUM_MAPS && posix_directory_next(directory, name, sizeof(name)))
			{
				if (forge_file_map_name_valid(name))
					snprintf(maps[map_count++], sizeof(maps[0]), "%s", name);
			}
			posix_directory_close(directory);
		}
		for (map_index = 0; map_index < map_count && count < capacity; map_index++)
			count = forge_store_list_map(maps[map_index], entries, count, capacity, bytes, layout);
	}
	free(bytes);
	free(layout);
	qsort(entries, (size_t)count, sizeof(entries[0]), forge_store_entry_compare);
	return count;
}

int forge_store_load(
	char const *map_name,
	char const *file_name,
	struct forge_file_layout *layout,
	char *why,
	int why_size)
{
	char path[FORGE_STORE_PATH_SIZE];
	char reason[96];
	unsigned char *bytes;
	int size;
	int result;

	if (!forge_store_file_path(map_name, file_name, NULL, path, sizeof(path)))
	{
		forge_store_why(why, why_size, T("Not a map variant's name."));
		return 0;
	}
	bytes = malloc(FORGE_FILE_MAXIMUM_SIZE + 1);
	if (!bytes)
	{
		forge_store_why(why, why_size, T("Not enough memory to read the map variant."));
		return 0;
	}
	size = forge_store_read(path, bytes, FORGE_FILE_MAXIMUM_SIZE + 1);
	if (size < 0)
	{
		free(bytes);
		forge_store_why(why, why_size, T("The map variant couldn't be read."));
		return 0;
	}
	result = forge_file_decode(bytes, size, map_name, layout, reason, sizeof(reason));
	free(bytes);
	if (result != _forge_file_ok)
	{
		platform_log("forge store: %s/%s refused: %s", map_name, file_name, reason);
		forge_store_why(why, why_size, result == _forge_file_wrong_map ? T("The map variant is of another map.") :
			T("The map variant is damaged and can't be used."));
		return 0;
	}
	platform_log("forge store: %s/%s read: \"%s\", %d objects", map_name, file_name, layout->title, layout->record_count);
	return 1;
}

int forge_store_save(
	struct forge_file_layout const *layout,
	char const *file_name,
	char *saved_name,
	int saved_size,
	char *why,
	int why_size)
{
	char folder[FORGE_STORE_PATH_SIZE];
	char name[FORGE_FILE_NAME_SIZE];
	char path[FORGE_STORE_PATH_SIZE];
	char temporary[FORGE_STORE_PATH_SIZE];
	char old[FORGE_STORE_PATH_SIZE];
	unsigned char *bytes;
	struct forge_file_layout *check;
	int size;
	int result;
	int replaced = 0;

	if (!layout || !forge_store_folder(layout->map_name, 1, folder, sizeof(folder)))
	{
		forge_store_why(why, why_size, T("The map variant couldn't be saved."));
		return 0;
	}
	/* (the name: the one given, else the title's, numbered past those
	there) */
	if (file_name && file_name[0])
	{
		snprintf(name, sizeof(name), "%s", file_name);
	}
	else
	{
		int number;

		name[0] = 0;
		for (number = 1; number <= FORGE_STORE_MAXIMUM_NUMBER; number++)
		{
			forge_file_name_from_title(layout->title, number, name, sizeof(name));
			if (forge_store_file_path(layout->map_name, name, NULL, path, sizeof(path)) && !forge_store_file_exists(path))
				break;
			name[0] = 0;
		}
	}
	if (!forge_store_file_path(layout->map_name, name, NULL, path, sizeof(path)) ||
		!forge_store_file_path(layout->map_name, name, FORGE_STORE_TEMPORARY_EXTENSION, temporary, sizeof(temporary)) ||
		!forge_store_file_path(layout->map_name, name, FORGE_STORE_OLD_EXTENSION, old, sizeof(old)))
	{
		forge_store_why(why, why_size, T("Too many map variants of that name."));
		return 0;
	}
	bytes = malloc(FORGE_FILE_MAXIMUM_SIZE + 1);
	check = malloc(sizeof(*check));
	if (!bytes || !check)
	{
		free(bytes);
		free(check);
		forge_store_why(why, why_size, T("Not enough memory to save the map variant."));
		return 0;
	}
	size = forge_file_encode(layout, bytes, FORGE_FILE_MAXIMUM_SIZE, &result);
	if (size <= 0)
	{
		platform_log("forge store: %s not written: %s", name, forge_file_result_name(result));
		free(bytes);
		free(check);
		forge_store_why(why, why_size, T("The map variant couldn't be saved."));
		return 0;
	}
	/* (room on the card for it, and some) */
	{
		posix_ulong free_low = 0, free_high = 0, total_low = 0, total_high = 0;

		if (posix_disk_space(folder, &free_low, &free_high, &total_low, &total_high) == 0 && !free_high &&
			free_low < (posix_ulong)(size + FORGE_STORE_SPACE_MARGIN))
		{
			platform_log("forge store: %s not written: %lu bytes free", name, (unsigned long)free_low);
			free(bytes);
			free(check);
			forge_store_why(why, why_size, T("The memory card is full. Nothing was saved."));
			return 0;
		}
	}
	remove(temporary);
	if (!forge_store_write(temporary, bytes, size))
	{
		platform_log("forge store: %s not written: the write failed (a full card?)", name);
		free(bytes);
		free(check);
		forge_store_why(why, why_size, T("Couldn't write it (is the card full?).\nNothing was saved."));
		return 0;
	}
	/* (read back as any file is, before it takes the old one's place) */
	result = forge_store_read(temporary, bytes, FORGE_FILE_MAXIMUM_SIZE + 1);
	if (result != size || forge_file_decode(bytes, size, layout->map_name, check, NULL, 0) != _forge_file_ok)
	{
		remove(temporary);
		platform_log("forge store: %s not written: it did not read back", name);
		free(bytes);
		free(check);
		forge_store_why(why, why_size, T("Couldn't write it (is the card full?).\nNothing was saved."));
		return 0;
	}
	free(bytes);
	free(check);
	if (forge_store_file_exists(path))
	{
		remove(old);
		if (rename(path, old) != 0)
		{
			remove(temporary);
			forge_store_why(why, why_size, T("The old map variant couldn't be replaced.\nNothing was saved."));
			return 0;
		}
		replaced = 1;
	}
	if (rename(temporary, path) != 0)
	{
		remove(temporary);
		if (replaced)
			rename(old, path);
		forge_store_why(why, why_size, T("The map variant couldn't be put in place.\nNothing was saved."));
		return 0;
	}
	if (replaced)
		remove(old);
	if (saved_name && saved_size > 0)
		snprintf(saved_name, (size_t)saved_size, "%s", name);
	platform_log("forge store: %s/%s written: \"%s\", %d objects, %d bytes%s", layout->map_name, name, layout->title,
		layout->record_count, size, replaced ? " (replacing the old one)" : "");
	return 1;
}

int forge_store_delete(
	char const *map_name,
	char const *file_name)
{
	char path[FORGE_STORE_PATH_SIZE];

	if (!forge_store_file_path(map_name, file_name, NULL, path, sizeof(path)) || remove(path) != 0)
		return 0;
	platform_log("forge store: %s/%s deleted", map_name, file_name);
	return 1;
}

int forge_store_rename(
	char const *map_name,
	char const *file_name,
	char const *title,
	char *why,
	int why_size)
{
	struct forge_file_layout *layout = malloc(sizeof(*layout));
	int saved;

	if (!layout)
	{
		forge_store_why(why, why_size, T("Not enough memory to read the map variant."));
		return 0;
	}
	if (!forge_store_load(map_name, file_name, layout, why, why_size))
	{
		free(layout);
		return 0;
	}
	if (!title || !title[0] || strlen(title) >= sizeof(layout->title) ||
		!forge_file_text_valid(title, (int)sizeof(layout->title)))
	{
		free(layout);
		forge_store_why(why, why_size, T("That name can't be used."));
		return 0;
	}
	snprintf(layout->title, sizeof(layout->title), "%s", title);
	saved = forge_store_save(layout, file_name, NULL, 0, why, why_size);
	free(layout);
	return saved;
}

int forge_store_exists(
	char const *map_name,
	char const *file_name)
{
	char path[FORGE_STORE_PATH_SIZE];

	return forge_store_file_path(map_name, file_name, NULL, path, sizeof(path)) && forge_store_file_exists(path);
}

void forge_store_date_now(
	char *date,
	int size)
{
	time_t now = time(NULL);
	struct tm *local = localtime(&now);

	if (!local || local->tm_year < 100 || local->tm_year > 8099)
	{
		if (size > 0)
			date[0] = 0;
		return;
	}
	snprintf(date, (size_t)size, "%04d-%02d-%02d %02d:%02d", local->tm_year + 1900, local->tm_mon + 1, local->tm_mday,
		local->tm_hour, local->tm_min);
}
