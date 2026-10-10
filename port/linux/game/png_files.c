/*
PNG_FILES.C

(port) Reading of a Halo: The Master Chief Collection mod's PNG pictures
(png_files.h): the chunks walked within the file, the header's kind and
size checked before anything is allocated, the image data inflated into
rows of exactly the size the header gives (zlib, the game's own), each row
unfiltered (PNG's five filters) and written out as a 32-bit Windows bitmap,
top row first, alpha dropped (the menus' pictures are opaque).
*/

/* ---------- headers */

#include "png_files.h"

#include "memory/zlib/zlib.h"

#include <stdlib.h>
#include <string.h>

/* ---------- constants */

#define PNG_SIGNATURE_BYTES 8
#define CHUNK_HEADER_BYTES 8
#define CHUNK_CRC_BYTES 4
#define HEADER_CHUNK_BYTES 13
#define CHUNK_TYPE(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define HEADER_CHUNK CHUNK_TYPE('I', 'H', 'D', 'R')
#define DATA_CHUNK CHUNK_TYPE('I', 'D', 'A', 'T')
#define END_CHUNK CHUNK_TYPE('I', 'E', 'N', 'D')
#define COLOR_TYPE_RGB 2
#define COLOR_TYPE_RGBA 6
/* a Windows bitmap's headers: the file's and BITMAPINFOHEADER */
#define BITMAP_HEADERS_BYTES 54

/* ---------- globals */

static char const *const png_file_status_descriptions[NUMBER_OF_PNG_FILE_STATUSES] =
{
	"ok",
	"not a PNG file",
	"not an 8-bit RGB or RGBA picture of at most 1024 pixels a side, not interlaced",
	"damaged",
	"out of memory",
};

static uint8_t const png_signature[PNG_SIGNATURE_BYTES] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

/* ---------- private code */

static uint32_t read_u32_big(
	uint8_t const *bytes)
{
	return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | (uint32_t)bytes[3];
}

static void write_u16_little(
	uint8_t *bytes,
	uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);

	return;
}

static void write_u32_little(
	uint8_t *bytes,
	uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);

	return;
}

/* zlib's memory: the C library's, as map_share_protocol.c's */
static voidpf zlib_allocate(
	voidpf opaque,
	uInt items,
	uInt size)
{
	(void)opaque;

	return calloc(items, size);
}

static void zlib_free(
	voidpf opaque,
	voidpf pointer)
{
	(void)opaque;
	free(pointer);

	return;
}

/* PNG's Paeth predictor */
static uint8_t paeth(
	int left,
	int up,
	int corner)
{
	int estimate = left + up - corner;
	int to_left = estimate > left ? estimate - left : left - estimate;
	int to_up = estimate > up ? estimate - up : up - estimate;
	int to_corner = estimate > corner ? estimate - corner : corner - estimate;

	if (to_left <= to_up && to_left <= to_corner)
	{
		return (uint8_t)left;
	}

	return (uint8_t)(to_up <= to_corner ? up : corner);
}

/* undoes the filter of each of the `height` rows of `stride` bytes (each
after its filter byte) at `rows`, in place; FALSE for an unknown filter */
static int rows_unfilter(
	uint8_t *rows,
	uint32_t stride,
	uint32_t height,
	uint32_t channels)
{
	uint32_t y;

	for (y = 0; y < height; y++)
	{
		uint8_t *row = rows + y * (stride + 1);
		uint8_t const *previous = y ? rows + (y - 1) * (stride + 1) + 1 : NULL;
		uint8_t filter = row[0];
		uint32_t x;

		row++;
		if (filter > 4)
		{
			return 0;
		}
		for (x = 0; x < stride; x++)
		{
			int left = x >= channels ? row[x - channels] : 0;
			int up = previous ? previous[x] : 0;
			int corner = previous && x >= channels ? previous[x - channels] : 0;

			switch (filter)
			{
			case 1:
				row[x] = (uint8_t)(row[x] + left);
				break;
			case 2:
				row[x] = (uint8_t)(row[x] + up);
				break;
			case 3:
				row[x] = (uint8_t)(row[x] + (left + up) / 2);
				break;
			case 4:
				row[x] = (uint8_t)(row[x] + paeth(left, up, corner));
				break;
			default:
				break;
			}
		}
	}

	return 1;
}

/* ---------- public code */

char const *png_file_status_describe(
	enum png_file_status status)
{
	return status >= 0 && status < NUMBER_OF_PNG_FILE_STATUSES ?
		png_file_status_descriptions[status] :
		"unknown status";
}

enum png_file_status png_file_to_bitmap(
	uint8_t const *file,
	uint32_t file_size,
	uint8_t **bitmap,
	uint32_t *bitmap_size)
{
	uint32_t offset = PNG_SIGNATURE_BYTES;
	uint32_t width;
	uint32_t height;
	uint32_t channels;
	uint32_t stride;
	uint32_t rows_bytes;
	uint32_t pixels_bytes;
	uint8_t *rows;
	uint8_t *out;
	z_stream stream;
	int inflated = 0;
	int ended = 0;
	uint32_t y;

	*bitmap = NULL;
	*bitmap_size = 0;
	if (file_size < PNG_SIGNATURE_BYTES + CHUNK_HEADER_BYTES + HEADER_CHUNK_BYTES + CHUNK_CRC_BYTES ||
		memcmp(file, png_signature, PNG_SIGNATURE_BYTES) ||
		read_u32_big(file + offset) != HEADER_CHUNK_BYTES ||
		read_u32_big(file + offset + 4) != HEADER_CHUNK)
	{
		return _png_file_status_not_png;
	}
	{
		uint8_t const *header = file + offset + CHUNK_HEADER_BYTES;

		width = read_u32_big(header);
		height = read_u32_big(header + 4);
		/* 8-bit RGB or RGBA, deflate, PNG's filters, not interlaced */
		if (!width || !height || width > PNG_FILE_MAXIMUM_DIMENSION || height > PNG_FILE_MAXIMUM_DIMENSION ||
			header[8] != 8 || (header[9] != COLOR_TYPE_RGB && header[9] != COLOR_TYPE_RGBA) ||
			header[10] || header[11] || header[12])
		{
			return _png_file_status_unsupported;
		}
		channels = header[9] == COLOR_TYPE_RGBA ? 4 : 3;
	}
	/* (at most 1024 by 1024 by 4: no product here overflows) */
	stride = width * channels;
	rows_bytes = (stride + 1) * height;
	pixels_bytes = width * height * 4;
	rows = malloc(rows_bytes);
	out = malloc(BITMAP_HEADERS_BYTES + pixels_bytes);
	if (!rows || !out)
	{
		free(rows);
		free(out);
		return _png_file_status_out_of_memory;
	}
	memset(&stream, 0, sizeof(stream));
	stream.zalloc = zlib_allocate;
	stream.zfree = zlib_free;
	stream.next_out = rows;
	stream.avail_out = rows_bytes;
	if (inflateInit(&stream) != Z_OK)
	{
		free(rows);
		free(out);
		return _png_file_status_out_of_memory;
	}

	/* the chunks, each within the file: the image data inflated as it comes */
	offset += CHUNK_HEADER_BYTES + HEADER_CHUNK_BYTES + CHUNK_CRC_BYTES;
	while (!ended && file_size - offset >= CHUNK_HEADER_BYTES)
	{
		uint32_t length = read_u32_big(file + offset);
		uint32_t type = read_u32_big(file + offset + 4);

		if (length > file_size - offset - CHUNK_HEADER_BYTES ||
			CHUNK_CRC_BYTES > file_size - offset - CHUNK_HEADER_BYTES - length)
		{
			break;
		}
		if (type == DATA_CHUNK && !inflated && length)
		{
			int result;

			stream.next_in = (Bytef *)(file + offset + CHUNK_HEADER_BYTES);
			stream.avail_in = length;
			result = inflate(&stream, Z_SYNC_FLUSH);
			if (result == Z_STREAM_END)
			{
				inflated = 1;
			}
			else if (result != Z_OK && result != Z_BUF_ERROR)
			{
				break;
			}
		}
		else if (type == END_CHUNK)
		{
			ended = 1;
		}
		offset += CHUNK_HEADER_BYTES + length + CHUNK_CRC_BYTES;
	}
	inflateEnd(&stream);
	if (!inflated || stream.avail_out || !rows_unfilter(rows, stride, height, channels))
	{
		free(rows);
		free(out);
		return _png_file_status_damaged;
	}

	/* the bitmap: 32-bit, its height negative (the top row first) */
	memset(out, 0, BITMAP_HEADERS_BYTES);
	out[0] = 'B';
	out[1] = 'M';
	write_u32_little(out + 2, BITMAP_HEADERS_BYTES + pixels_bytes);
	write_u32_little(out + 10, BITMAP_HEADERS_BYTES);
	write_u32_little(out + 14, 40);
	write_u32_little(out + 18, width);
	write_u32_little(out + 22, (uint32_t)0 - height);
	write_u16_little(out + 26, 1);
	write_u16_little(out + 28, 32);
	write_u32_little(out + 34, pixels_bytes);
	for (y = 0; y < height; y++)
	{
		uint8_t const *row = rows + y * (stride + 1) + 1;
		uint8_t *pixel = out + BITMAP_HEADERS_BYTES + y * width * 4;
		uint32_t x;

		for (x = 0; x < width; x++, row += channels, pixel += 4)
		{
			pixel[0] = row[2];
			pixel[1] = row[1];
			pixel[2] = row[0];
			pixel[3] = 0xFF;
		}
	}
	free(rows);
	*bitmap = out;
	*bitmap_size = BITMAP_HEADERS_BYTES + pixels_bytes;

	return _png_file_status_ok;
}

void png_file_bitmap_free(
	uint8_t *bitmap)
{
	free(bitmap);

	return;
}
