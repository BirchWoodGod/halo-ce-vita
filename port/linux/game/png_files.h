/*
PNG_FILES.H

(port) Reading of the PNG pictures a Halo: The Master Chief Collection mod
gives its levels (their campaign files' thumbnails: custom_edition_mods.c),
for the campaign's level list. Only the kinds the mods use are read: 8 bits
a channel, RGB or RGBA, not interlaced, at most PNG_FILE_MAXIMUM_DIMENSION
pixels a side. A picture read is given back as a Windows bitmap in memory
(32-bit, top row first), which bmp_files.c fits to the menus as it fits a
Custom Edition map's.

The code is standalone C with fixed-width types and zlib: the same file is
compiled into the native game builds (with the game's zlib) and into the
host tool port/tools/png_file_report.c, which the tests drive
(tools/test_png_files.py). A picture file is untrusted input: every chunk
is checked against the file, the decompressed rows against the room their
header says they take, and nothing is written past what was allocated.
*/

#ifndef __PNG_FILES_H
#define __PNG_FILES_H

/* ---------- headers */

#include <stdint.h>

/* ---------- constants */

/* the widest and tallest picture read: the mods' thumbnails are 316 by 160 */
#define PNG_FILE_MAXIMUM_DIMENSION 1024

enum png_file_status
{
	_png_file_status_ok,
	/* no PNG signature, or no header chunk first */
	_png_file_status_not_png,
	/* not 8-bit RGB or RGBA, or interlaced, or larger than read */
	_png_file_status_unsupported,
	/* a chunk past the file's end, compressed data that is not, rows of an
	unknown filter, or fewer rows than the header says */
	_png_file_status_damaged,
	_png_file_status_out_of_memory,

	NUMBER_OF_PNG_FILE_STATUSES
};

/* ---------- prototypes */

char const *png_file_status_describe(
	enum png_file_status status);

/* Reads the `file_size` bytes at `file` as a PNG this module reads: ok
with `*bitmap` a Windows bitmap file of its picture (`*bitmap_size` bytes,
which png_file_bitmap_free gives back: the C library's memory, which the
game's own free does not take), else NULL. */
enum png_file_status png_file_to_bitmap(
	uint8_t const *file,
	uint32_t file_size,
	uint8_t **bitmap,
	uint32_t *bitmap_size);
void png_file_bitmap_free(
	uint8_t *bitmap);

#endif
