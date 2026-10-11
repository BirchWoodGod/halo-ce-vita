/* shared_map_cache.c

The dedicated server's shared map cache (port/linux/DEDICATED_SERVER.md,
"Several servers on one machine"; built only into `ninja linux-server`,
HALO_DEDICATED_SERVER). The game plays an Xbox map from a copy it makes in
its cache partition (z:\cache000.map to cache005.map, cache_files_windows.c):
the map decompressed into one of six fixed slots, two for the campaign, one
for the main menu, three for multiplayer, which a map cycle of more than
three maps copies again and again. Each server keeps its own in its folder's
saves/, up to about 770 MB of them (the slots' size).

With a shared cache (HALO_MAP_CACHE: sv_map_cache in init.txt, -mapcache),
each map is decompressed once, into a file of its own in that folder, which
every server then reads: the slot holds the shared file's handle instead of
its own copy (cache_files_windows.c). Its name is the map's, its checksum's
and its decompressed length's (bloodgulch-1a2b3c4d-02c00000.map), so
another version of the map, or another language's, is another file.

- A shared copy is never written once it has its name: it is written as a
  temporary file (.NAME.PID.tmp), synced, and renamed into place, under an
  exclusive lock (NAME.lock, flock) that the server building it holds; a
  server finding it locked waits for the copy to appear (or the lock to be
  let go: the builder stopped, and it builds it itself). A crashed builder's
  temporary files are deleted by the next one, under the lock.
- Servers open it read-only. A folder they cannot write (made read-only
  once the cycle's maps are in it) is read and never written; a map it does
  not have is copied into the server's own slots, as without a shared cache.
- A map without a checksum (Invader writes none) cannot be told from
  another version of it, and is not shared.

Nothing here is reached from the network: the names are the server's own
maps' (its cycle), and only letters, digits, '_', '-' and '.' are taken. */

#if defined(HALO_DEDICATED_SERVER) && defined(__linux__)

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "platform.h"
#include "posix.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

HANDLE platform_file_handle(int descriptor, const char *path);

static struct
{
	int checked;
	char folder[PATH_MAX];
	/* the build under way: its lock, temporary and final names */
	int lock_descriptor;
	HANDLE handle;
	char temporary[PATH_MAX];
	char final[PATH_MAX];
} shared = { 0, "", -1, NULL, "", "" };

/* the folder, from HALO_MAP_CACHE (made if it is not there); NULL if none */
static const char *shared_folder(void)
{
	if (!shared.checked)
	{
		const char *setting = getenv("HALO_MAP_CACHE");

		shared.checked = 1;
		if (setting && setting[0] == '/' && strlen(setting) < sizeof(shared.folder) - 80)
		{
			struct posix_file_information status;

			snprintf(shared.folder, sizeof(shared.folder), "%s", setting);
			while (strlen(shared.folder) > 1 && shared.folder[strlen(shared.folder) - 1] == '/')
				shared.folder[strlen(shared.folder) - 1] = 0;
			if (posix_stat(shared.folder, &status) != 0)
				posix_make_directory(shared.folder);
			if (posix_stat(shared.folder, &status) != 0 || !(status.flags & _posix_file_is_directory))
			{
				platform_log("map cache: the shared map cache %s is not a folder (%s); each map is copied into "
					"this server's own cache", shared.folder, strerror(errno));
				shared.folder[0] = 0;
			}
			else
			{
				platform_log("map cache: shared, in %s", shared.folder);
			}
		}
		else if (setting && setting[0])
		{
			platform_log("map cache: the shared map cache \"%s\" is not a full path: not used", setting);
		}
	}
	return shared.folder[0] ? shared.folder : NULL;
}

int shared_map_cache_enabled(void)
{
	return shared_folder() != NULL;
}

/* the shared copy's file name for a map (its name, checksum, length): 0 if
the map cannot be shared */
static int shared_name(const char *name, unsigned long checksum, long file_length, char *result, size_t size)
{
	size_t index;

	if (!shared_folder() || !name || !name[0] || strlen(name) > 31 || name[0] == '.' || file_length <= 0 ||
		checksum == 0 || checksum == 0xFFFFFFFFUL)
	{
		return 0;
	}
	for (index = 0; name[index]; index++)
	{
		char character = name[index];

		if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.'))
		{
			return 0;
		}
	}
	snprintf(result, size, "%s-%08lx-%08lx.map", name, checksum & 0xFFFFFFFFUL, (unsigned long)file_length);
	for (index = 0; result[index]; index++)
	{
		if (result[index] >= 'A' && result[index] <= 'Z')
			result[index] = (char)(result[index] - 'A' + 'a');
	}
	return 1;
}

/* a read-only handle of a shared copy at path, at least file_length long;
INVALID_HANDLE_VALUE if none */
static HANDLE open_copy(const char *path, long file_length)
{
	struct posix_file_information status;
	int descriptor = open(path, O_RDONLY | O_CLOEXEC);

	if (descriptor < 0)
		return INVALID_HANDLE_VALUE;
	if (posix_fstat(descriptor, &status) != 0 || (status.flags & _posix_file_is_directory) ||
		(!status.size_high && status.size_low < (posix_ulong)file_length))
	{
		platform_log("map cache: %s is shorter than its map; not used", path);
		close(descriptor);
		return INVALID_HANDLE_VALUE;
	}
	return platform_file_handle(descriptor, path);
}

HANDLE shared_map_cache_open(const char *name, unsigned long checksum, long file_length)
{
	char file[64], path[PATH_MAX];

	if (!shared_name(name, checksum, file_length, file, sizeof(file)))
		return INVALID_HANDLE_VALUE;
	snprintf(path, sizeof(path), "%s/%s", shared.folder, file);
	return open_copy(path, file_length);
}

/* (under the lock) the temporary files of builds of this copy that stopped
(their servers gone: the lock was free) */
static void delete_stale_temporaries(const char *file)
{
	DIR *directory = opendir(shared.folder);
	struct dirent *entry;
	size_t length = strlen(file);

	if (!directory)
		return;
	while ((entry = readdir(directory)) != NULL)
	{
		const char *found = entry->d_name;
		size_t found_length = strlen(found);

		if (found[0] == '.' && !strncmp(found + 1, file, length) && found[1 + length] == '.' && found_length > 4 &&
			!strcmp(found + found_length - 4, ".tmp"))
		{
			char path[PATH_MAX];

			snprintf(path, sizeof(path), "%s/%s", shared.folder, found);
			if (unlink(path) == 0)
				platform_log("map cache: deleted %s, left by a build that stopped", found);
		}
	}
	closedir(directory);
}

int shared_map_cache_build_begin(const char *name, unsigned long checksum, long file_length, long size,
	HANDLE *handle)
{
	char file[64], lock_path[PATH_MAX];
	int descriptor;

	*handle = INVALID_HANDLE_VALUE;
	if (shared.lock_descriptor >= 0 || !shared_name(name, checksum, file_length, file, sizeof(file)))
		return -1;
	snprintf(lock_path, sizeof(lock_path), "%s/%s.lock", shared.folder, file);
	shared.lock_descriptor = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (shared.lock_descriptor < 0)
	{
		/* (a folder this server cannot write: read only) */
		platform_log("map cache: cannot add %s to the shared map cache (%s); copied into this server's own", name,
			strerror(errno));
		return -1;
	}
	if (flock(shared.lock_descriptor, LOCK_EX | LOCK_NB) != 0)
	{
		int busy = errno == EWOULDBLOCK;

		close(shared.lock_descriptor);
		shared.lock_descriptor = -1;
		return busy ? 0 : -1;
	}
	snprintf(shared.final, sizeof(shared.final), "%s/%s", shared.folder, file);
	/* (made by another server while the lock was being taken: the caller
	opens it) */
	if (access(shared.final, F_OK) == 0)
	{
		close(shared.lock_descriptor);
		shared.lock_descriptor = -1;
		return 0;
	}
	delete_stale_temporaries(file);
	snprintf(shared.temporary, sizeof(shared.temporary), "%s/.%s.%ld.tmp", shared.folder, file, (long)getpid());
	descriptor = open(shared.temporary, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	/* (the slot's size, as the copy wants it: sparse, only the map is written) */
	if (descriptor < 0 || ftruncate(descriptor, size) != 0)
	{
		platform_log("map cache: cannot write %s (%s); %s is copied into this server's own cache", shared.temporary,
			strerror(errno), name);
		if (descriptor >= 0)
		{
			close(descriptor);
			unlink(shared.temporary);
		}
		close(shared.lock_descriptor);
		shared.lock_descriptor = -1;
		return -1;
	}
	shared.handle = platform_file_handle(descriptor, shared.temporary);
	if (shared.handle == INVALID_HANDLE_VALUE)
	{
		unlink(shared.temporary);
		close(shared.lock_descriptor);
		shared.lock_descriptor = -1;
		return -1;
	}
	*handle = shared.handle;
	platform_log("map cache: decompressing %s into the shared map cache (%s)", name, file);
	return 1;
}

HANDLE shared_map_cache_build_end(int finished, long file_length)
{
	HANDLE result = INVALID_HANDLE_VALUE;

	if (shared.lock_descriptor < 0)
		return INVALID_HANDLE_VALUE;
	if (finished)
	{
		int descriptor = open(shared.temporary, O_RDONLY | O_CLOEXEC);

		/* (its data on the disk before its name: a copy with its name is a
		whole one, whatever stops this machine) */
		if (descriptor >= 0 && fsync(descriptor) == 0 && rename(shared.temporary, shared.final) == 0)
		{
			int folder = open(shared.folder, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

			if (folder >= 0)
			{
				fsync(folder);
				close(folder);
			}
			CloseHandle(shared.handle);
			result = open_copy(shared.final, file_length);
			platform_log("map cache: %s added to the shared map cache", strrchr(shared.final, '/') + 1);
		}
		else
		{
			/* (kept for this session alone: the file goes once closed) */
			platform_log("map cache: could not add %s to the shared map cache (%s); used by this server alone",
				strrchr(shared.final, '/') + 1, strerror(errno));
			unlink(shared.temporary);
			result = shared.handle;
		}
		if (descriptor >= 0)
			close(descriptor);
	}
	else
	{
		CloseHandle(shared.handle);
		unlink(shared.temporary);
	}
	shared.handle = NULL;
	close(shared.lock_descriptor);
	shared.lock_descriptor = -1;
	return result;
}

#else

/* (the dedicated server's alone) */
typedef int shared_map_cache_unused;

#endif
