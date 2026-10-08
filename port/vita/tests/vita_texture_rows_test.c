/*
VITA_TEXTURE_ROWS_TEST.C

The movie's frame - a linear X8R8G8B8 texture rebuilt every frame - and the
other uncompressed textures laid out as GXM's linear rows (texture_decode in
port/vita/platform/vita_textures.c, included here) are decoded straight into
their rows: no copy of the whole level in the C heap. A 960x544 movie asked
the Vita's heap for 2 MB a frame, which v1.1.0-beta.1's start-up left too
little of in one piece (issue #38: the picture black, the sound playing).
Here every malloc over 1 MB fails, as it did there, and the rows must still
come out right: 960x544 (the Vita's screen), 640x360 and 848x480 (rows a
multiple of 8 texels), 854x480 (rows padded to 856), 1x1, and the alpha of
the X formats set. An L8 texture
(convert_texel) and a swizzled A8R8G8B8 one check decode_level's other two
paths.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* (the heap the movie found: nothing over 1 MB in one piece) */
static unsigned long large_mallocs;
static void *test_malloc(size_t size)
{
	if (size > 1024 * 1024)
	{
		large_mallocs++;
		return NULL;
	}
	return malloc(size);
}
#define malloc test_malloc

#include "../platform/vita_textures.c"

#undef malloc

/* ---------- what vita_textures.c calls outside texture_decode */

void platform_log(const char *format, ...) { (void)format; }
void *vgxm_pool_alloc(unsigned long size, unsigned long alignment) { (void)size; (void)alignment; return NULL; }
void vgxm_pool_reset(void) {}
int vgxm_pool_recycle(void **base, unsigned long *size) { *base = NULL; *size = 0; return 0; }
unsigned long vgxm_pool_used(void) { return 0; }
unsigned long long vita_host_time_us(void) { return 0; }
unsigned long memory_watch_generation(unsigned long address, unsigned long size) { (void)address; (void)size; return 1; }
void vita_host_sleep_us(unsigned long microseconds) { (void)microseconds; }
int vgxm_texture_initialize(struct vgxm_texture *texture, const void *data, unsigned long format, unsigned long layout,
	unsigned long width, unsigned long height, unsigned long levels)
{
	(void)texture; (void)data; (void)format; (void)layout; (void)width; (void)height; (void)levels;
	return 0;
}

static int failures;

#define CHECK(condition, ...) do { if (!(condition)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* a texture's format and size words as the Xbox has them */
static void describe(unsigned long format, int linear, unsigned long width, unsigned long height, unsigned long pitch,
	struct xgpu_texture_description *description, DWORD *format_word, DWORD *size_word)
{
	*format_word = (1 << D3DFORMAT_MIPMAP_SHIFT) | (format << D3DFORMAT_FORMAT_SHIFT) | (2 << D3DFORMAT_DIMENSION_SHIFT);
	if (linear)
		*size_word = ((pitch / D3DTEXTURE_PITCH_ALIGNMENT - 1) << D3DSIZE_PITCH_SHIFT) | ((height - 1) << D3DSIZE_HEIGHT_SHIFT) |
			(width - 1);
	else
	{
		unsigned long u = 0, v = 0;

		while ((1UL << u) < width)
			u++;
		while ((1UL << v) < height)
			v++;
		*format_word |= (u << D3DFORMAT_USIZE_SHIFT) | (v << D3DFORMAT_VSIZE_SHIFT) | (1 << D3DFORMAT_PSIZE_SHIFT);
		*size_word = 0;
	}
	xgpu_texture_describe(*format_word, *size_word, description);
}

static void decode_case(const char *name, unsigned long format, unsigned long width, unsigned long height,
	unsigned long bytes_per_texel)
{
	struct xgpu_texture_description description;
	struct texture_build build;
	DWORD format_word, size_word;
	unsigned long pitch = (width * bytes_per_texel + 63) & ~63UL, row = LINEAR_ROW(width), x, y, bad = 0;
	unsigned char *source;
	uint32_t *rows = calloc(row * height + 16, 4);

	describe(format, 1, width, height, pitch, &description, &format_word, &size_word);
	CHECK(description.linear && description.width == width && description.height == height &&
		description.pitch >= width * bytes_per_texel, "%s: described %lux%lu linear %d pitch %lu", name, description.width,
		description.height, description.linear, description.pitch);
	/* (the rows as the description has them: one row's is its own width) */
	pitch = description.pitch;
	source = calloc(pitch * height, 1);
	for (y = 0; y < height; y++)
		for (x = 0; x < width * bytes_per_texel; x++)
			source[y * pitch + x] = (unsigned char)(x * 7 + y * 13 + 1);
	/* (a guard past the rows) */
	rows[row * height] = 0xdeadbeef;
	memset(&build, 0, sizeof(build));
	build.description = &description;
	build.base = source;
	build.channel_order = _custom_edition_channels_xbox;
	build.format_word = format_word;
	build.size_word = size_word;
	build.bytes = pitch * height;
	build.preallocated = rows;
	build.preallocated_size = row * height * 4;
	large_mallocs = 0;
	CHECK(texture_decode(&build), "%s: not decoded (%lu mallocs over 1 MB refused)", name, large_mallocs);
	CHECK(large_mallocs == 0, "%s: %lu mallocs over 1 MB", name, large_mallocs);
	CHECK(build.layout == _vgxm_texture_linear && build.width == width && build.height == height && build.levels == 1,
		"%s: layout %lu %lux%lu levels %lu", name, build.layout, build.width, build.height, build.levels);
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
		{
			const unsigned char *texel = source + y * pitch + x * bytes_per_texel;
			uint32_t want = bytes_per_texel == 4 ?
				((uint32_t)texel[0] | (uint32_t)texel[1] << 8 | (uint32_t)texel[2] << 16 | (uint32_t)texel[3] << 24) :
				0xff000000u | (uint32_t)texel[0] * 0x010101u;

			if (format == D3DFMT_LIN_X8R8G8B8)
				want |= 0xff000000u;
			if (rows[y * row + x] != want && bad++ < 4)
				printf("FAIL: %s: texel %lu,%lu is %08x, not %08x\n", name, x, y, rows[y * row + x], want);
		}
	if (bad)
		failures++;
	CHECK(rows[row * height] == 0xdeadbeef, "%s: wrote past its rows", name);
	free(source);
	free(rows);
	printf("%s: %lux%lu rows of %lu texels %s\n", name, width, height, row, bad ? "WRONG" : "ok");
}

/* a power-of-two A8R8G8B8 swizzled texture of a non-square shape, decoded
through the same path's swizzled branch: one Morton-ordered texel each */
static void swizzled_case(void)
{
	struct xgpu_texture_description description;
	unsigned long width = 8, height = 4, x, y, bad = 0;
	DWORD format_word, size_word;
	uint32_t source[32], rows[4 * 8 + 1];

	describe(D3DFMT_A8R8G8B8, 0, width, height, 0, &description, &format_word, &size_word);
	for (x = 0; x < 32; x++)
		source[x] = 0x01000000u * (unsigned)x + (unsigned)x;
	rows[32] = 0xdeadbeef;
	decode_level(&description, 0, (const unsigned char *)source, NULL, (unsigned long *)rows, 0);
	{
		struct swizzle_masks masks = swizzle_masks(width, height, 1);

		for (y = 0; y < height; y++)
			for (x = 0; x < width; x++)
				if (rows[y * width + x] != source[spread(masks.x, x) | spread(masks.y, y)] && bad++ < 4)
					printf("FAIL: swizzled: texel %lu,%lu\n", x, y);
	}
	if (bad)
		failures++;
	CHECK(rows[32] == 0xdeadbeef, "swizzled: wrote past its texels");
	printf("swizzled 8x4: %s\n", bad ? "WRONG" : "ok");
}

int main(void)
{
	/* the movie's frame (bink_playback.c: linear X8R8G8B8, rows of 4 x its width) */
	decode_case("movie 960x544", D3DFMT_LIN_X8R8G8B8, 960, 544, 4);
	decode_case("movie 640x360", D3DFMT_LIN_X8R8G8B8, 640, 360, 4);
	decode_case("movie 848x480", D3DFMT_LIN_X8R8G8B8, 848, 480, 4);
	decode_case("movie 854x480", D3DFMT_LIN_X8R8G8B8, 854, 480, 4);
	decode_case("A8R8G8B8 1x1", D3DFMT_LIN_A8R8G8B8, 1, 1, 4);
	decode_case("A8R8G8B8 1024x600", D3DFMT_LIN_A8R8G8B8, 1024, 600, 4);
	decode_case("L8 302x30", D3DFMT_LIN_L8, 302, 30, 1);
	swizzled_case();
	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("all passed\n");
	return 0;
}
