/*
WIN32_MCC.C

(port) Where Windows says Steam and Halo: The Master Chief Collection are,
for the Halo content folder's search at start (mcc_paths.h,
port/linux/src/xbox_files.c): the PC install tool's places
(tools/installer/halo_ce_vita_installer.py, steam_library_folders and
find_mcc_folders) in C. Only read: the registry's values and whether a
drive's folders are there.
*/

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mcc_paths.h"

/* `name` of `key` (a REG_SZ or REG_EXPAND_SZ value) into `out`
(MCC_PATHS_BYTES), in the registry view `view` (0, KEY_WOW64_64KEY or
KEY_WOW64_32KEY) */
static int registry_text(HKEY hive, const char *key, const char *name, REGSAM view, char *out)
{
	HKEY handle;
	DWORD type = 0;
	DWORD size = MCC_PATHS_BYTES - 1;
	LONG result;

	if (RegOpenKeyExA(hive, key, 0, KEY_READ | view, &handle) != ERROR_SUCCESS)
		return 0;
	result = RegQueryValueExA(handle, name, NULL, &type, (BYTE *)out, &size);
	RegCloseKey(handle);
	if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || !size)
		return 0;
	out[size < MCC_PATHS_BYTES ? size : MCC_PATHS_BYTES - 1] = 0;
	return out[0] != 0;
}

static void place_add(char places[][MCC_PATHS_BYTES], int *count, int maximum, const char *path)
{
	if (path && path[0] && *count < maximum)
		snprintf(places[(*count)++], MCC_PATHS_BYTES, "%s", path);
}

void mcc_paths_windows_places(char steams[][MCC_PATHS_BYTES], int *steam_count, char roots[][MCC_PATHS_BYTES],
	int *root_count, int maximum)
{
	static const struct
	{
		HKEY hive;
		const char *key;
		const char *name;
	} steam_keys[] =
	{
		{ HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath" },
		{ HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath" },
		{ HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", "InstallPath" },
	};
	static const char *const program_files[] = { "ProgramFiles(x86)", "ProgramFiles" };
	static const char *const drive_libraries[] = { "SteamLibrary", "Steam", "Program Files (x86)\\Steam", "Games\\Steam" };
	static const REGSAM views[] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
	char text[MCC_PATHS_BYTES];
	char path[MCC_PATHS_BYTES + 96];
	DWORD drives = GetLogicalDrives();
	/* (no "insert a disk" box for an empty drive) */
	UINT error_mode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
	unsigned index;
	char letter;

	*steam_count = 0;
	*root_count = 0;
	for (index = 0; index < sizeof(steam_keys) / sizeof(steam_keys[0]); index++)
	{
		if (registry_text(steam_keys[index].hive, steam_keys[index].key, steam_keys[index].name, 0, text))
			place_add(steams, steam_count, maximum, text);
	}
	for (index = 0; index < sizeof(program_files) / sizeof(program_files[0]); index++)
	{
		const char *folder = getenv(program_files[index]);

		if (folder && *folder)
		{
			snprintf(path, sizeof(path), "%s\\Steam", folder);
			place_add(steams, steam_count, maximum, path);
		}
	}
	/* (Steam's uninstall entry of MCC: where it was installed) */
	snprintf(path, sizeof(path), "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App %s",
		MCC_PATHS_STEAM_APP_ID);
	for (index = 0; index < sizeof(views) / sizeof(views[0]); index++)
	{
		if (registry_text(HKEY_LOCAL_MACHINE, path, "InstallLocation", views[index], text))
			place_add(roots, root_count, maximum, text);
	}
	/* (each fixed drive, C: to Z:: the libraries Steam no longer lists, and
	the Xbox app's XboxGames folder) */
	for (letter = 'C'; letter <= 'Z'; letter++)
	{
		char drive[4] = { letter, ':', '\\', 0 };

		if (!(drives & (1u << (letter - 'A'))) || GetDriveTypeA(drive) != DRIVE_FIXED)
			continue;
		for (index = 0; index < sizeof(drive_libraries) / sizeof(drive_libraries[0]); index++)
		{
			DWORD attributes;

			snprintf(path, sizeof(path), "%c:\\%s\\steamapps", letter, drive_libraries[index]);
			attributes = GetFileAttributesA(path);
			if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				snprintf(path, sizeof(path), "%c:\\%s", letter, drive_libraries[index]);
				place_add(steams, steam_count, maximum, path);
			}
		}
		snprintf(path, sizeof(path), "%c:\\XboxGames\\%s\\Content", letter, MCC_PATHS_FOLDER_NAME);
		{
			DWORD attributes = GetFileAttributesA(path);

			if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
				place_add(roots, root_count, maximum, path);
		}
	}
	SetErrorMode(error_mode);
}
