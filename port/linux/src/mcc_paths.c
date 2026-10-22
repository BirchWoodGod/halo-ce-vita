/*
MCC_PATHS.C

(port) Where a player's Halo: The Master Chief Collection keeps its halo1
folder (mcc_paths.h): Steam's library folders, read from its
libraryfolders.vdf, each holding steamapps/common/Halo The Master Chief
Collection/halo1. Plain C on the text it is given, no file read here
(xbox_files.c reads the files and looks for the folders), so that
port/vita/tests/mcc_paths_test.c tests it with made-up files.

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

int mcc_paths_halo1(const char *library, char *out, unsigned long out_size)
{
	unsigned long length;
	int written;

	if (!library || !library[0] || !out || !out_size)
		return 0;
	length = strlen(library);
	while (length > 1 && (library[length - 1] == '/' || library[length - 1] == '\\'))
		length--;
	written = snprintf(out, out_size, "%.*s/" MCC_PATHS_STEAM_HALO1, (int)length, library);
	return written > 0 && (unsigned long)written < out_size;
}
