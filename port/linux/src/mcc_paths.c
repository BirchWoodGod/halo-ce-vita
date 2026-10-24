/*
MCC_PATHS.C

(port) Where a player's Halo: The Master Chief Collection is, the Halo
content folder (mcc_paths.h): Steam's library folders, read from its
libraryfolders.vdf, each holding steamapps/common/Halo The Master Chief
Collection. The parser is plain C on the text it is given; the search
below it reads files with stdio alone (no directory listing: the names it
looks for are known), so that port/vita/tests/mcc_content_test.c tests both
on made-up Steam folders. The places a PC has are xbox_files.c's (Linux)
and win32_mcc.c's (Windows); the order is the PC install tool's
(tools/installer/halo_ce_vita_installer.py, find_mcc_folders).

The file is Valve's KeyValues text: quoted keys and values, braces for
blocks. Steam writes each library as a block with a "path" key ("0" {
"path" "/home/me/.local/share/Steam" ... }); older Steams wrote the path as
the value of a numbered key ("1" "D:\\SteamLibrary"). Both are taken; a
backslash escapes the next character, as Steam writes "\\" for each
backslash of a Windows path. Nothing else of the file is used, and a
malformed one yields the paths read before the fault.
*/

#include "mcc_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* reads the quoted string at text[*at] (just past its opening quote) into
`out` (`out_size` bytes, cut short when longer, and then not taken):
TRUE when its closing quote was found and it fit */
static int quoted_read(const char *text, unsigned long size, unsigned long *at, char *out, unsigned long out_size)
{
	unsigned long length = 0;
	int fits = 1;

	while (*at < size)
	{
		char character = text[(*at)++];

		if (character == '"')
		{
			out[length < out_size ? length : out_size - 1] = 0;
			return fits;
		}
		if (character == '\\' && *at < size)
			character = text[(*at)++];
		if (character == 0 || character == '\n' || character == '\r')
			return 0;
		if (length + 1 < out_size)
			out[length++] = character;
		else
			fits = 0;
	}
	return 0;
}

static int numbered(const char *key)
{
	if (!key[0])
		return 0;
	for (; *key; key++)
	{
		if (*key < '0' || *key > '9')
			return 0;
	}
	return 1;
}

int mcc_paths_library_folders(const char *text, unsigned long size, char folders[][MCC_PATHS_BYTES], int maximum)
{
	char key[MCC_PATHS_BYTES];
	char value[MCC_PATHS_BYTES];
	int have_key = 0;
	int count = 0;
	unsigned long at = 0;

	if (!text || maximum <= 0)
		return 0;
	while (at < size && count < maximum)
	{
		char character = text[at++];

		if (character == '{' || character == '}')
		{
			have_key = 0;
			continue;
		}
		if (character != '"')
			continue;
		if (!have_key)
		{
			if (!quoted_read(text, size, &at, key, sizeof(key)))
				key[0] = 0;
			have_key = 1;
			continue;
		}
		have_key = 0;
		if (!quoted_read(text, size, &at, value, sizeof(value)) || !value[0])
			continue;
		/* ("path", or an old Steam's numbered key with a folder's path) */
		if (!strcmp(key, "path") || (numbered(key) && (value[0] == '/' || strchr(value, ':'))))
		{
			int index;

			for (index = 0; index < count && strcmp(folders[index], value); index++)
			{
			}
			if (index == count)
				snprintf(folders[count++], MCC_PATHS_BYTES, "%s", value);
		}
	}
	return count;
}

int mcc_paths_mcc(const char *library, char *out, unsigned long out_size)
{
	unsigned long length;
	int written;

	if (!library || !library[0] || !out || !out_size)
		return 0;
	length = strlen(library);
	while (length > 1 && (library[length - 1] == '/' || library[length - 1] == '\\'))
		length--;
	written = snprintf(out, out_size, "%.*s/" MCC_PATHS_STEAM_MCC, (int)length, library);
	return written > 0 && (unsigned long)written < out_size;
}

int mcc_paths_linux_steam_folders(const char *home, char folders[][MCC_PATHS_BYTES], int maximum)
{
	static const char *const steams[] =
	{
		".local/share/Steam",
		".steam/steam",
		".steam/root",
		".var/app/com.valvesoftware.Steam/.local/share/Steam",
		"snap/steam/common/.local/share/Steam",
	};
	unsigned long length;
	int count = 0;
	unsigned index;

	if (!home || !home[0] || maximum <= 0)
		return 0;
	length = strlen(home);
	while (length > 1 && home[length - 1] == '/')
		length--;
	for (index = 0; index < sizeof(steams) / sizeof(steams[0]) && count < maximum; index++)
	{
		int written = snprintf(folders[count], MCC_PATHS_BYTES, "%.*s/%s", (int)length, home, steams[index]);

		if (written > 0 && written < MCC_PATHS_BYTES)
			count++;
	}
	return count;
}

/* ---------- the files read */

/* Halo CE's stock maps in MCC's halo1/maps (mcc_content.c names the same):
any one of them with MCC's header makes the folder MCC's */
static const char *const halo1_stock_maps[] =
{
	"bloodgulch", "a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
	"beavercreek", "sidewinder", "damnation", "ratrace", "prisoner", "hangemhigh", "chillout", "carousel",
	"boardingaction", "wizard", "putput", "longest", "dangercanyon", "deathisland", "gephyrophobia",
	"icefields", "infinity", "timberland",
};
/* a cache file's header: 'head' little-endian ("daeh" as it lies), then
its version, 13 for MCC's Halo CE (mcc_content.c's table) */
#define HALO1_HEADER_SIGNATURE "daeh"
#define HALO1_CACHE_VERSION 13

/* `path` without its trailing separators into `out` */
static int path_trimmed(const char *path, char *out, unsigned long out_size)
{
	unsigned long length;
	int written;

	if (!path || !path[0])
		return 0;
	length = strlen(path);
	while (length > 1 && (path[length - 1] == '/' || path[length - 1] == '\\'))
		length--;
	written = snprintf(out, out_size, "%.*s", (int)length, path);
	return written > 0 && (unsigned long)written < out_size;
}

/* 1: a stock map with MCC's header; -1: stock maps there, none MCC's; 0: none */
static int halo1_state(const char *root, char *found)
{
	char base[MCC_PATHS_BYTES];
	int state = 0;
	unsigned index;

	if (!path_trimmed(root, base, sizeof(base)))
		return 0;
	for (index = 0; index < sizeof(halo1_stock_maps) / sizeof(halo1_stock_maps[0]); index++)
	{
		char path[MCC_PATHS_BYTES + 64];
		unsigned char header[8];
		FILE *stream;
		size_t got;

		snprintf(path, sizeof(path), "%s/halo1/maps/%s.map", base, halo1_stock_maps[index]);
		stream = fopen(path, "rb");
		if (!stream)
			continue;
		got = fread(header, 1, sizeof(header), stream);
		fclose(stream);
		if (got == sizeof(header) && !memcmp(header, HALO1_HEADER_SIGNATURE, 4) &&
			(header[4] | header[5] << 8 | header[6] << 16 | (unsigned long)header[7] << 24) == HALO1_CACHE_VERSION)
		{
			if (found)
				snprintf(found, MCC_PATHS_BYTES, "%.*s", MCC_PATHS_BYTES - 1, path);
			return 1;
		}
		state = -1;
	}
	return state;
}

int mcc_paths_halo1_valid(const char *root, char *found)
{
	return halo1_state(root, found) > 0;
}

/* the last name of `path` (after its last separator) */
static const char *last_name(const char *path)
{
	const char *slash = strrchr(path, '/');
	const char *backslash = strrchr(path, '\\');

	if (backslash && (!slash || backslash > slash))
		slash = backslash;
	return slash ? slash + 1 : path;
}

int mcc_paths_root_of(const char *chosen, char *out, unsigned long out_size)
{
	/* (a Steam library, its steamapps or its steamapps/common) */
	static const char *const below[] =
	{
		"", "/" MCC_PATHS_STEAM_MCC, "/common/" MCC_PATHS_FOLDER_NAME, "/" MCC_PATHS_FOLDER_NAME,
	};
	char base[MCC_PATHS_BYTES];
	char candidate[MCC_PATHS_BYTES + 96];
	unsigned index;
	int up = 0;

	if (!out || !out_size || !path_trimmed(chosen, base, sizeof(base)))
		return 0;
	/* (MCC's folders under the one holding halo1 a player may have named:
	halo1, halo1/maps, halo1/maps/custom_edition) */
	if (!strcmp(last_name(base), "custom_edition"))
		up = 3;
	else if (!strcmp(last_name(base), "maps"))
		up = 2;
	else if (!strcmp(last_name(base), "halo1"))
		up = 1;
	for (index = 0; index < sizeof(below) / sizeof(below[0]); index++)
	{
		snprintf(candidate, sizeof(candidate), "%s%s", base, below[index]);
		if (mcc_paths_halo1_valid(candidate, NULL))
			return snprintf(out, out_size, "%s", candidate) < (int)out_size;
	}
	for (; up > 0; up--)
	{
		char *name = (char *)last_name(base);

		if (name == base)
			return 0;
		name[-1] = 0;
		if (!base[0])
			return 0;
		if (mcc_paths_halo1_valid(base, NULL))
			return snprintf(out, out_size, "%s", base) < (int)out_size;
	}
	return 0;
}

/* the places looked at once each: by their own name, and on Linux by
where their links lead (~/.steam/steam is ~/.local/share/Steam) */
struct places_seen
{
	char names[MCC_PATHS_MAXIMUM_PLACES * 2][MCC_PATHS_BYTES];
	int count;
};

static int place_new(struct places_seen *seen, const char *path)
{
	char name[MCC_PATHS_BYTES];
	int index;

	if (!path_trimmed(path, name, sizeof(name)))
		return 0;
#if !defined(_WIN32) && !defined(HALO_VITA)
	{
		char *real = realpath(name, NULL);

		if (real)
		{
			snprintf(name, sizeof(name), "%.*s", MCC_PATHS_BYTES - 1, real);
			free(real);
		}
	}
#endif
	for (index = 0; index < seen->count; index++)
	{
		if (!strcmp(seen->names[index], name))
			return 0;
	}
	if (seen->count < (int)(sizeof(seen->names) / sizeof(seen->names[0])))
		snprintf(seen->names[seen->count++], MCC_PATHS_BYTES, "%s", name);
	return 1;
}

/* the MCC folder `root`, when valid, into `out`, said */
static int root_try(const char *root, const char *from, char *out, mcc_paths_log_function log)
{
	char found[MCC_PATHS_BYTES];
	int state = halo1_state(root, found);

	if (state > 0)
	{
		if (log)
			log("content: Halo MCC found (%s): %s (%s has MCC's header)", from, root, found);
		return path_trimmed(root, out, MCC_PATHS_BYTES);
	}
	if (state < 0 && log)
		log("content: %s (%s) has Halo CE's maps, but none with MCC's header (version %d): not used", root, from,
			HALO1_CACHE_VERSION);
	return 0;
}

/* the libraries a libraryfolders.vdf names (none when it is not there) */
static int libraries_read(const char *path, char folders[][MCC_PATHS_BYTES], int maximum)
{
	static char text[256 * 1024];
	FILE *stream = fopen(path, "rb");
	size_t size;

	if (!stream)
		return 0;
	size = fread(text, 1, sizeof(text), stream);
	fclose(stream);
	return mcc_paths_library_folders(text, (unsigned long)size, folders, maximum);
}

static int search_steam(const char *steam, struct places_seen *seen, char *out, mcc_paths_log_function log)
{
	static const char *const files[] = { "steamapps/libraryfolders.vdf", "config/libraryfolders.vdf" };
	char libraries[MCC_PATHS_MAXIMUM_LIBRARIES * 2 + 1][MCC_PATHS_BYTES];
	char base[MCC_PATHS_BYTES];
	int count = 0, index;
	unsigned file;

	if (!path_trimmed(steam, base, sizeof(base)))
		return 0;
	for (file = 0; file < sizeof(files) / sizeof(files[0]); file++)
	{
		char path[MCC_PATHS_BYTES + 64];
		int read;

		snprintf(path, sizeof(path), "%s/%s", base, files[file]);
		read = libraries_read(path, libraries + count, MCC_PATHS_MAXIMUM_LIBRARIES);
		if (read && log)
			log("content: Steam's %s names %d librar%s", path, read, read == 1 ? "y" : "ies");
		count += read;
	}
	/* (the Steam folder itself last: its own library, which the file names
	too, or an old Steam with none) */
	snprintf(libraries[count++], MCC_PATHS_BYTES, "%s", base);
	for (index = 0; index < count; index++)
	{
		char mcc[MCC_PATHS_BYTES + 96];
		char from[MCC_PATHS_BYTES + 32];

		if (!mcc_paths_mcc(libraries[index], mcc, sizeof(mcc)) || strlen(mcc) >= MCC_PATHS_BYTES || !place_new(seen, mcc))
			continue;
		snprintf(from, sizeof(from), "Steam library %.*s", MCC_PATHS_BYTES - 1, libraries[index]);
		if (root_try(mcc, from, out, log))
			return 1;
	}
	return 0;
}

int mcc_paths_search_steam(const char *steam, char *out, mcc_paths_log_function log)
{
	static struct places_seen seen;

	seen.count = 0;
	return out && search_steam(steam, &seen, out, log);
}

int mcc_paths_search(char steams[][MCC_PATHS_BYTES], int steam_count, char roots[][MCC_PATHS_BYTES], int root_count,
	char *out, mcc_paths_log_function log)
{
	static struct places_seen seen, steams_seen;
	int index;

	if (!out)
		return 0;
	out[0] = 0;
	seen.count = 0;
	steams_seen.count = 0;
	for (index = 0; index < steam_count; index++)
	{
		if (place_new(&steams_seen, steams[index]) && search_steam(steams[index], &seen, out, log))
			return 1;
	}
	for (index = 0; index < root_count; index++)
	{
		if (place_new(&seen, roots[index]) && root_try(roots[index], "Windows", out, log))
			return 1;
	}
	return 0;
}
