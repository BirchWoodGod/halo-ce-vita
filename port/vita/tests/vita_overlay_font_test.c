/*
VITA_OVERLAY_FONT_TEST.C

A desktop test of the settings panel's Halo 3 fonts (port/vita/host/
vita_overlay_font.c, included whole): a font file of tools/halo3_menu_art.py
read into the atlas, every character's pixels where its record says and no
two overlapping, a broken or truncated file refused, a full atlas refusing a
font rather than writing past its end; UTF-8 text measured and drawn (a quad
per character with pixels, the advances summed), in capitals when asked,
a character the font lacks drawn as '?', the buttons' symbols (1 to 5).

Run port/vita/tests/run_vita_overlay_font_test.sh FONT_FOLDER.
*/

#include "../host/vita_overlay_font.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

static unsigned char *read_file(const char *folder, const char *name, unsigned long *size)
{
	char path[1024];
	FILE *file;
	unsigned char *data;
	long length;

	snprintf(path, sizeof(path), "%s/%s", folder, name);
	file = fopen(path, "rb");
	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length);
	if (data && fread(data, 1, (size_t)length, file) != (size_t)length)
	{
		free(data);
		data = NULL;
	}
	fclose(file);
	*size = (unsigned long)length;
	return data;
}

struct quads
{
	int count;
	float x0[64], y0[64], x1[64], y1[64], u0[64], v0[64];
};

static void quad(void *user, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1)
{
	struct quads *quads = user;

	(void)u1;
	(void)v1;
	if (quads->count < 64)
	{
		quads->x0[quads->count] = x0;
		quads->y0[quads->count] = y0;
		quads->x1[quads->count] = x1;
		quads->y1[quads->count] = y1;
		quads->u0[quads->count] = u0;
		quads->v0[quads->count] = v0;
	}
	quads->count++;
}

/* a record of the file (code, advance, width, height, origin x, origin y) */
static void record(const unsigned char *data, unsigned int code, short out[6], unsigned long *pixels_at)
{
	int count = (short)(data[14] | data[15] << 8), index;
	unsigned long offset = 16 + 12UL * (unsigned long)count;

	memset(out, 0, 6 * sizeof(short));
	for (index = 0; index < count; index++)
	{
		const unsigned char *r = data + 16 + 12 * index;
		int k;

		for (k = 0; k < 6; k++)
			out[k] = (short)(r[2 * k] | r[2 * k + 1] << 8);
		if ((unsigned short)out[0] == code)
		{
			*pixels_at = offset;
			return;
		}
		offset += (unsigned long)(out[2] * out[3]);
	}
	memset(out, 0, 6 * sizeof(short));
}

int main(int argc, char **argv)
{
	const char *folder = argc > 1 ? argv[1] : ".";
	unsigned long size, small_size, at = 0;
	unsigned char *data = read_file(folder, "overlay_item.bin", &size);
	unsigned char *small = read_file(folder, "overlay_small.bin", &small_size);
	struct overlay_atlas atlas, tiny;
	static struct overlay_face face, face_small, broken;
	short a[6], b[6];
	int overlap = 0, misplaced = 0, outside = 0, code;
	struct quads quads;
	float width;

	if (!data || !small)
	{
		printf("FAIL the font files in %s (tools/halo3_menu_art.py)\n", folder);
		return 1;
	}
	check(overlay_atlas_init(&atlas, 512, 512) == 0, "an atlas of 512x512");
	check(overlay_face_read(&face, &atlas, data, size) == 0, "the item font read");
	check(overlay_face_read(&face_small, &atlas, small, small_size) == 0, "a second font in the same atlas");
	check(face.ascent > 0 && face.descent >= 0, "its ascent and descent");

	/* every character's pixels where its record says, inside the atlas */
	for (code = 0; code < 256; code++)
	{
		const struct overlay_glyph *g = &face.glyphs[code];
		unsigned long pixels = 0;
		int y;

		if (!face.present[code])
			continue;
		record(data, (unsigned int)code, a, &pixels);
		if (g->advance != a[1] || g->width != a[2] || g->height != a[3] || g->origin_x != a[4] || g->origin_y != a[5])
			misplaced++;
		if (g->u + g->width > atlas.width || g->v + g->height > atlas.used_height)
			outside++;
		for (y = 0; y < g->height; y++)
			if (memcmp(atlas.pixels + (g->v + y) * atlas.width + g->u, data + pixels + (unsigned long)(y * g->width),
				(size_t)g->width))
				misplaced++;
	}
	check(!misplaced, "every character's record and pixels as the file has them");
	check(!outside, "every character inside the atlas's used rows");
	/* no two characters (both fonts) overlapping */
	for (code = 0; code < 512 && !overlap; code++)
	{
		const struct overlay_face *f1 = code < 256 ? &face : &face_small;
		const struct overlay_glyph *g1 = &f1->glyphs[code & 255];
		int other;

		if (!f1->present[code & 255] || !g1->width)
			continue;
		for (other = code + 1; other < 512; other++)
		{
			const struct overlay_face *f2 = other < 256 ? &face : &face_small;
			const struct overlay_glyph *g2 = &f2->glyphs[other & 255];

			if (!f2->present[other & 255] || !g2->width)
				continue;
			if (g1->u < g2->u + g2->width && g2->u < g1->u + g1->width && g1->v < g2->v + g2->height &&
				g2->v < g1->v + g1->height)
			{
				overlap = 1;
				break;
			}
		}
	}
	check(!overlap, "no two characters overlapping in the atlas");
	for (code = 1; code <= 5; code++)
		check(face.present[code] && face.glyphs[code].width > 0, "a button's symbol (1 to 5)");

	/* measured and drawn */
	record(data, 'A', a, &at);
	record(data, 'B', b, &at);
	width = overlay_face_width(&face, "AB", 0);
	check(width == (float)(a[1] + b[1]), "\"AB\" as wide as its advances");
	check(overlay_face_width(&face, "ab", 1) == width, "\"ab\" in capitals as wide as \"AB\"");
	check(overlay_face_width(&face, "\xc3\xb1", 1) == (float)face.glyphs[0xD1].advance, "UTF-8 n with a tilde in capitals");
	check(overlay_face_width(&face, "\xe2\x86\x92", 0) == (float)face.glyphs['?'].advance, "a character it lacks as '?'");
	memset(&quads, 0, sizeof(quads));
	width = overlay_face_draw(&face, &atlas, 100.0f, 50.0f, "A B", 0, quad, &quads);
	check(quads.count == 2, "a quad per character with pixels (not the space)");
	check(width == (float)(a[1] + face.glyphs[' '].advance + b[1]), "drawing returns the width");
	check(quads.x0[0] == 100.0f - a[4] && quads.y0[0] == 50.0f - a[5], "'A' placed by its origin from the pen");
	check(quads.x1[0] - quads.x0[0] == (float)a[2] && quads.y1[0] - quads.y0[0] == (float)a[3], "'A' its own size");
	check(quads.u0[0] == (float)face.glyphs['A'].u / (float)atlas.width &&
		quads.v0[0] == (float)face.glyphs['A'].v / (float)atlas.height, "'A' sampled where it is in the atlas");
	check(quads.x0[1] == 100.0f + (float)(a[1] + face.glyphs[' '].advance) - b[4], "'B' after the space");
	check(overlay_face_draw(&face, &atlas, 0.0f, 0.0f, NULL, 0, quad, &quads) == 0.0f, "no text, nothing drawn");

	/* refused */
	memset(&broken, 0, sizeof(broken));
	check(overlay_face_read(&broken, &atlas, data, 15) < 0, "a file shorter than its header refused");
	check(overlay_face_read(&broken, &atlas, data, size - 1) < 0, "a truncated file refused");
	data[0] = 'X';
	check(overlay_face_read(&broken, &atlas, data, size) < 0, "a file that is not H3FN refused");
	check(!broken.loaded, "a refused font not loaded");
	data[0] = 'H';
	check(overlay_atlas_init(&tiny, 64, 16) == 0, "a tiny atlas");
	check(overlay_face_read(&broken, &tiny, data, size) < 0, "a font too large for the atlas refused");
	check(tiny.used_height <= tiny.height, "the full atlas not written past");
	overlay_atlas_free(&tiny);
	overlay_atlas_free(&atlas);
	free(data);
	free(small);
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures != 0;
}
