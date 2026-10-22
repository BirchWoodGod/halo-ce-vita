/*
MCC_PATHS.H

(port) Finding a player's Halo: The Master Chief Collection folder, the
Halo content folder, in Steam's libraries (mcc_paths.c)
(port/linux/game/mcc_content.c). Plain C: no file is read here.
*/

#ifndef __MCC_PATHS_H
#define __MCC_PATHS_H

#define MCC_PATHS_BYTES 512
/* MCC's folder in a Steam library (halo1 ... in it) */
#define MCC_PATHS_STEAM_MCC "steamapps/common/Halo The Master Chief Collection"
/* the most libraries taken of one libraryfolders.vdf */
#define MCC_PATHS_MAXIMUM_LIBRARIES 16

/* The library folders a Steam libraryfolders.vdf (`text`, `size` bytes, no
terminator needed) names, each once, into `folders` (at most `maximum`);
returns how many. */
int mcc_paths_library_folders(const char *text, unsigned long size, char folders[][MCC_PATHS_BYTES], int maximum);

/* `library`'s MCC folder (MCC_PATHS_STEAM_MCC under it) into `out`:
FALSE when it does not fit. */
int mcc_paths_mcc(const char *library, char *out, unsigned long out_size);

#endif
