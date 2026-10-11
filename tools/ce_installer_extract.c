/*
CE_INSTALLER_EXTRACT.C

Halo Custom Edition's bitmaps.map, sounds.map and loc.map out of the
player's own Custom Edition installer, on a PC, for players who would rather
copy the three files to the Vita than the installer (the game does the same
at start-up: port/linux/src/posix_ce_installer.c).

    ce_installer_extract halocesetup_en_1.00.exe [folder]

writes the three that are not in the folder yet (default: the current one).
Built alone, with any C compiler:

    cc -O2 -Iport/linux/src -Iport/third_party/libmspack -DHAVE_INTTYPES_H=1 \
        tools/ce_installer_extract.c port/linux/src/posix_ce_installer.c port/linux/src/lang.c \
        port/third_party/libmspack/{cabd,lzxd,mszipd,qtmd,system}.c -o ce_installer_extract
*/

#include "ce_installer.h"

#include <stdio.h>
#include <string.h>

static int show_progress(void *context, const char *file, unsigned long long done, unsigned long long total)
{
	static int last = -1;
	int percent = total ? (int)(done * 100 / total) : 100;

	(void)context;
	if (file[0] && percent != last)
	{
		fprintf(stderr, "\r%-12s %3d%%", file, percent);
		last = percent;
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *folder = argc > 2 ? argv[2] : ".";
	char error[512];
	int missing, index, result;

	if (argc < 2 || argc > 3 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
	{
		fprintf(stderr, "usage: %s halocesetup_en_1.00.exe [folder]\n"
			"Writes Halo Custom Edition's bitmaps.map, sounds.map and loc.map out of its installer\n"
			"into the folder (default: the current one), the ones not there yet.\n", argv[0]);
		return 2;
	}
	missing = ce_installer_missing(folder);
	if (!missing)
	{
		printf("bitmaps.map, sounds.map and loc.map are already in %s\n", folder);
		return 0;
	}
	result = ce_installer_extract(argv[1], folder, missing, show_progress, NULL, error, sizeof(error));
	fprintf(stderr, "\n");
	if (result != CE_INSTALLER_DONE)
	{
		fprintf(stderr, "%s\n", error);
		return 1;
	}
	for (index = 0; index < 3; index++)
		if (missing & (1 << index))
			printf("%s/%s\n", folder, ce_installer_file_name(1 << index));
	return 0;
}
