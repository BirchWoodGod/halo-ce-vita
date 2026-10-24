/*
MCC_PATHS.H

(port) Finding a player's Halo: The Master Chief Collection folder, the
Halo content folder, in Steam's libraries (mcc_paths.c)
(port/linux/game/mcc_content.c). Plain C on stdio alone: the PC builds
look for MCC with it at start (xbox_files.c), and
port/vita/tests/mcc_content_test.c tests it on made-up Steam folders.
*/

#ifndef __MCC_PATHS_H
#define __MCC_PATHS_H

#define MCC_PATHS_BYTES 512
/* MCC's folder in a Steam library (halo1 ... in it) */
#define MCC_PATHS_STEAM_MCC "steamapps/common/Halo The Master Chief Collection"
/* the most libraries taken of one libraryfolders.vdf */
#define MCC_PATHS_MAXIMUM_LIBRARIES 16
/* MCC's Steam app (its uninstall entry on Windows: "Steam App 976730") and
the folder Steam and the Xbox app name it */
#define MCC_PATHS_STEAM_APP_ID "976730"
#define MCC_PATHS_FOLDER_NAME "Halo The Master Chief Collection"
/* the most places one search takes (Steam folders, or MCC folders) */
#define MCC_PATHS_MAXIMUM_PLACES 32

/* how a search says what it looked at and found (NULL: nothing said) */
typedef void (*mcc_paths_log_function)(const char *format, ...);

/* The library folders a Steam libraryfolders.vdf (`text`, `size` bytes, no
terminator needed) names, each once, into `folders` (at most `maximum`);
returns how many. */
int mcc_paths_library_folders(const char *text, unsigned long size, char folders[][MCC_PATHS_BYTES], int maximum);

/* `library`'s MCC folder (MCC_PATHS_STEAM_MCC under it) into `out`:
FALSE when it does not fit. */
int mcc_paths_mcc(const char *library, char *out, unsigned long out_size);

/* The Steam folders a Linux desktop has under `home` (the native Steam's
~/.local/share/Steam, its links ~/.steam/steam and ~/.steam/root, the
Flatpak's ~/.var/app/com.valvesoftware.Steam/.local/share/Steam, the
Snap's ~/snap/steam/common/.local/share/Steam), in that order, into
`folders` (at most `maximum`; none for an empty `home`); returns how many.
Nothing is read: the caller looks at each. */
int mcc_paths_linux_steam_folders(const char *home, char folders[][MCC_PATHS_BYTES], int maximum);

/* ---------- the files read (stdio) */

/* Whether `root` is an MCC folder this build plays: its halo1/maps holds
one of Halo CE's stock maps (a10.map ... bloodgulch.map ...) whose cache
header is MCC's ('head', version 13). Only the first 8 bytes of a file are
read. `found` (may be NULL, MCC_PATHS_BYTES) gets that file's path. */
int mcc_paths_halo1_valid(const char *root, char *found);

/* MCC's folder from what a player named (paths.content): that folder, its
halo1, halo1/maps, halo1/maps/custom_edition, a Steam library holding it
(or that library's steamapps or steamapps/common), into `out`: FALSE when
none of them is a valid one (mcc_paths_halo1_valid). */
int mcc_paths_root_of(const char *chosen, char *out, unsigned long out_size);

/* The first valid MCC folder (mcc_paths_halo1_valid) of a Steam folder
`steam`: in each library its steamapps/libraryfolders.vdf (then
config/libraryfolders.vdf) names, in the file's order, then in `steam`
itself as a library. `out`: MCC_PATHS_BYTES. */
int mcc_paths_search_steam(const char *steam, char *out, mcc_paths_log_function log);

/* The search of a PC (xbox_files.c's when no folder is set): each Steam
folder of `steams` (mcc_paths_search_steam) in order, then each of `roots`
(MCC folders found otherwise: Windows' uninstall entry, the Xbox app's
XboxGames folders), the first valid one into `out` (MCC_PATHS_BYTES). A
folder named twice is looked at once. */
int mcc_paths_search(char steams[][MCC_PATHS_BYTES], int steam_count, char roots[][MCC_PATHS_BYTES], int root_count,
	char *out, mcc_paths_log_function log);

/* (Windows, port/windows/src/win32_mcc.c) where Windows says Steam and MCC
are: the Steam folders (the registry's HKCU Software\Valve\Steam
SteamPath, HKLM SOFTWARE\WOW6432Node\Valve\Steam and SOFTWARE\Valve\Steam
InstallPath, Program Files' Steam, each fixed drive's SteamLibrary, Steam
and Games\Steam) into `steams`, and MCC's folders (the uninstall entry
"Steam App 976730"'s InstallLocation, each fixed drive's
XboxGames\Halo The Master Chief Collection\Content) into `roots`; the
counts into `steam_count` and `root_count`. */
void mcc_paths_windows_places(char steams[][MCC_PATHS_BYTES], int *steam_count, char roots[][MCC_PATHS_BYTES],
	int *root_count, int maximum);

#endif
