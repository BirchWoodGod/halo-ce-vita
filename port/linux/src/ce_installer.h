/*
CE_INSTALLER.H

Halo Custom Edition's resource maps (bitmaps.map, sounds.map, loc.map) out
of the player's own Custom Edition installer (halocesetup_en_1.00.exe and
the like), for the Custom Edition maps and OpenCE's multiplayer menus:
posix_ce_installer.c. Plain C types, for the platform layer, the Vita's
host side and tools/ce_installer_extract.c alike.
*/

#ifndef __HALO_LINUX_CE_INSTALLER_H
#define __HALO_LINUX_CE_INSTALLER_H

/* the resource maps, as bits */
#define CE_INSTALLER_BITMAPS 1
#define CE_INSTALLER_SOUNDS 2
#define CE_INSTALLER_LOC 4
#define CE_INSTALLER_ALL 7

enum
{
	CE_INSTALLER_DONE,
	CE_INSTALLER_FAILED,
	/* (the progress procedure asked to stop) */
	CE_INSTALLER_STOPPED,
};

/* called as the extraction goes, at most every megabyte or so: the file
being extracted, and the work done of all of it (bytes decompressed);
nonzero stops it */
typedef int (*ce_installer_progress_proc)(void *context, const char *file, unsigned long long done,
	unsigned long long total);

/* the resource maps (CE_INSTALLER_* bits) missing from the folder */
int ce_installer_missing(const char *maps_directory);

/* the file name of a resource map bit ("bitmaps.map") */
const char *ce_installer_file_name(int which);

/* a Custom Edition installer in the folder: halocesetup*.exe in any case;
nonzero with its path and size */
int ce_installer_find(const char *directory, char *path, int path_size, unsigned long long *size);

/* writes the resource maps `which` holds out of the installer (or a .cab)
into the folder: each to <name>.extract first, checked (its size as the
cabinet gives it, the cabinet's block checksums, its resource map header),
then renamed. A map already in the folder is left as it is. Returns
CE_INSTALLER_DONE, or another with the reason (for the player) in error.
Called from any thread; it holds about 2.7 MB while it runs (the LZX window,
its input block, two 256 KB file buffers) */
int ce_installer_extract(const char *installer, const char *maps_directory, int which,
	ce_installer_progress_proc progress, void *context, char *error, int error_size);

#endif
