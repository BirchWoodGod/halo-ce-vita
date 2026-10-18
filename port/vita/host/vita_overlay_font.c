/*
VITA_OVERLAY_FONT.C

The settings panel's Halo 3 fonts (vita_overlay_font.h). A file: "H3FN",
version 1, ascent, descent, two unused shorts, the count; a record per
character (code, advance, box width and height, origin x and y: the pen
minus the box's left, the baseline minus its top), in order of code; then
the boxes' pixels, 8 bits each, character after character. The characters
go into the atlas in rows (a row as tall as its tallest), a pixel apart so
filtering never takes a neighbour's. Codes above 255 are not kept.
*/

#include "vita_overlay_font.h"

#include <stdlib.h>
#include <string.h>

static int read_short(const unsigned char *data)
{
	return (short)(data[0] | data[1] << 8);
}

int overlay_atlas_init(struct overlay_atlas *atlas, int width, int height)
{
	memset(atlas, 0, sizeof(*atlas));
	atlas->pixels = calloc((size_t)width * (size_t)height, 1);
	if (!atlas->pixels)
		return -1;
	atlas->width = width;
	atlas->height = height;
	return 0;
}

void overlay_atlas_free(struct overlay_atlas *atlas)
{
	free(atlas->pixels);
	memset(atlas, 0, sizeof(*atlas));
}

/* a place for a box (-1: the atlas is full) */
static int atlas_place(struct overlay_atlas *atlas, int width, int height, int *u, int *v)
{
	if (width + 1 > atlas->width)
		return -1;
	if (atlas->shelf_x + width + 1 > atlas->width)
	{
		atlas->shelf_y += atlas->shelf_height;
		atlas->shelf_x = 0;
		atlas->shelf_height = 0;
	}
	if (atlas->shelf_y + height + 1 > atlas->height)
		return -1;
	*u = atlas->shelf_x;
	*v = atlas->shelf_y;
	atlas->shelf_x += width + 1;
	if (height + 1 > atlas->shelf_height)
		atlas->shelf_height = height + 1;
	if (atlas->shelf_y + atlas->shelf_height > atlas->used_height)
		atlas->used_height = atlas->shelf_y + atlas->shelf_height;
	return 0;
}

int overlay_face_read(struct overlay_face *face, struct overlay_atlas *atlas, const unsigned char *data,
	unsigned long size)
{
	struct overlay_atlas saved = *atlas;
	unsigned long pixels, needed;
	int count, index;

	memset(face, 0, sizeof(*face));
	if (!data || size < 16 || memcmp(data, "H3FN", 4) || read_short(data + 4) != 1)
		return -1;
	count = read_short(data + 14);
	if (count < 0 || size < 16 + 12UL * (unsigned long)count)
		return -1;
	/* (the whole file first: its pixels all there) */
	pixels = 16 + 12UL * (unsigned long)count;
	needed = pixels;
	for (index = 0; index < count; index++)
	{
		const unsigned char *r = data + 16 + 12 * index;
		int width = read_short(r + 4), height = read_short(r + 6);

		if (width < 0 || height < 0)
			return -1;
		needed += (unsigned long)width * (unsigned long)height;
	}
	if (needed != size)
		return -1;
	face->ascent = (short)read_short(data + 6);
	face->descent = (short)read_short(data + 8);
	for (index = 0; index < count; index++)
	{
		const unsigned char *r = data + 16 + 12 * index;
		unsigned int code = (unsigned short)read_short(r);
		struct overlay_glyph glyph;
		int u = 0, v = 0, y;

		glyph.advance = (short)read_short(r + 2);
		glyph.width = (short)read_short(r + 4);
		glyph.height = (short)read_short(r + 6);
		glyph.origin_x = (short)read_short(r + 8);
		glyph.origin_y = (short)read_short(r + 10);
		if (glyph.width && glyph.height)
		{
			if (atlas_place(atlas, glyph.width, glyph.height, &u, &v) < 0)
			{
				/* (the atlas as it was: none of this font's characters) */
				int keep = saved.used_height;

				memset(atlas->pixels + keep * atlas->width, 0, (size_t)(atlas->used_height - keep) * (size_t)atlas->width);
				saved.pixels = atlas->pixels;
				*atlas = saved;
				memset(face, 0, sizeof(*face));
				return -1;
			}
			for (y = 0; y < glyph.height; y++)
				memcpy(atlas->pixels + (v + y) * atlas->width + u, data + pixels + (unsigned long)(y * glyph.width),
					(size_t)glyph.width);
		}
		pixels += (unsigned long)glyph.width * (unsigned long)glyph.height;
		glyph.u = (unsigned short)u;
		glyph.v = (unsigned short)v;
		if (code < 256)
		{
			face->glyphs[code] = glyph;
			face->present[code] = 1;
		}
	}
	face->loaded = 1;
	return 0;
}

/* the next character of UTF-8 text (a broken sequence: its first byte) */
static unsigned int next_code(const char **text)
{
	const unsigned char *at = (const unsigned char *)*text;
	unsigned int code = at[0];
	int extra = code >= 0xF0 ? 3 : code >= 0xE0 ? 2 : code >= 0xC0 ? 1 : 0, index;

	for (index = 1; index <= extra; index++)
		if ((at[index] & 0xC0) != 0x80)
			extra = 0;
	if (extra)
	{
		code &= 0x3F >> extra;
		for (index = 1; index <= extra; index++)
			code = code << 6 | (at[index] & 0x3F);
	}
	*text += 1 + extra;
	return code;
}

static const struct overlay_glyph *glyph_of(const struct overlay_face *face, unsigned int code, int upper)
{
	if (upper && ((code >= 'a' && code <= 'z') || (code >= 0xE0 && code <= 0xFE && code != 0xF7)))
		code -= 0x20;
	if (code > 255 || !face->present[code])
		code = '?';
	return face->present[code] ? &face->glyphs[code] : NULL;
}

float overlay_face_width(const struct overlay_face *face, const char *text, int upper)
{
	float width = 0.0f;

	while (text && *text)
	{
		const struct overlay_glyph *glyph = glyph_of(face, next_code(&text), upper);

		if (glyph)
			width += glyph->advance;
	}
	return width;
}

float overlay_face_draw(const struct overlay_face *face, const struct overlay_atlas *atlas, float x, float baseline,
	const char *text, int upper, overlay_quad_function quad, void *user)
{
	float pen = x;

	while (text && *text)
	{
		const struct overlay_glyph *glyph = glyph_of(face, next_code(&text), upper);

		if (!glyph)
			continue;
		if (glyph->width && glyph->height)
		{
			float x0 = pen - glyph->origin_x, y0 = baseline - glyph->origin_y;

			quad(user, x0, y0, x0 + glyph->width, y0 + glyph->height, (float)glyph->u / (float)atlas->width,
				(float)glyph->v / (float)atlas->height, (float)(glyph->u + glyph->width) / (float)atlas->width,
				(float)(glyph->v + glyph->height) / (float)atlas->height);
		}
		pen += glyph->advance;
	}
	return pen - x;
}
