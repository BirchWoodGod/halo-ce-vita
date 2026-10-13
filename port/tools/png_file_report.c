/*
PNG_FILE_REPORT.C

Reports whether the native builds read a PNG picture
(port/linux/game/png_files.c), for the tests (tools/test_png_files.py).

	png_file_report FILE [BITMAP]

The file gets a block of "key: value" lines; the Windows bitmap made of a
picture read is written to BITMAP. The exit status is 0 when the file was
read, 1 when it was refused, 2 when it could not be opened.
*/

/* ---------- headers */

#include "png_files.h"

#include <stdio.h>
#include <stdlib.h>

/* ---------- constants */

/* larger than any picture's file the tests make */
#define MAXIMUM_FILE_BYTES 0x01000000UL

/* ---------- public code */

int main(
	int argument_count,
	char **arguments)
{
	FILE *stream;
	uint8_t *file;
	uint8_t *bitmap;
	uint32_t bitmap_size;
	size_t size;
	enum png_file_status status;

	if (argument_count < 2 || argument_count > 3)
	{
		fprintf(stderr, "usage: png_file_report FILE [BITMAP]\n");
		return 2;
	}
	stream = fopen(arguments[1], "rb");
	file = malloc(MAXIMUM_FILE_BYTES);
	if (!stream || !file)
	{
		if (stream)
		{
			fclose(stream);
		}
		free(file);
		return 2;
	}
	size = fread(file, 1, MAXIMUM_FILE_BYTES, stream);
	fclose(stream);
	status = png_file_to_bitmap(file, (uint32_t)size, &bitmap, &bitmap_size);
	free(file);
	printf("status: %s\n", png_file_status_describe(status));
	if (status == _png_file_status_ok)
	{
		printf("bitmap_bytes: %lu\n", (unsigned long)bitmap_size);
		if (argument_count == 3)
		{
			FILE *output = fopen(arguments[2], "wb");

			if (!output || fwrite(bitmap, 1, bitmap_size, output) != bitmap_size)
			{
				printf("written: no\n");
			}
			if (output)
			{
				fclose(output);
			}
		}
		png_file_bitmap_free(bitmap);
	}

	return status == _png_file_status_ok ? 0 : 1;
}
