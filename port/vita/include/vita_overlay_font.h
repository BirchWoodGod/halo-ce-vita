/*
VITA_OVERLAY_FONT.H

The settings panel's Halo 3 fonts (vita_overlay_font.c): the H3FN files of
tools/halo3_menu_art.py (app0:menus/halo3/overlay_*.bin) read into one
8-bit atlas, and UTF-8 text measured and laid out as a quad per character.
No GXM here: vita_gxm.c makes the atlas a texture and draws the quads.
*/

#ifndef VITA_OVERLAY_FONT_H
#define VITA_OVERLAY_FONT_H

struct overlay_glyph
{
	short advance, width, height, origin_x, origin_y;
	unsigned short u, v;
};

struct overlay_face
{
	int loaded;
	short ascent, descent;
	unsigned char present[256];
	struct overlay_glyph glyphs[256];
};

/* the characters' pixels (coverage), packed in rows of characters */
struct overlay_atlas
{
	unsigned char *pixels;
	int width, height, used_height;
	int shelf_x, shelf_y, shelf_height;
};

/* the quad of one character: its screen rectangle and its atlas's (0..1) */
typedef void (*overlay_quad_function)(void *user, float x0, float y0, float x1, float y1, float u0, float v0,
	float u1, float v1);

int overlay_atlas_init(struct overlay_atlas *atlas, int width, int height);
void overlay_atlas_free(struct overlay_atlas *atlas);
/* 0, or -1 for a file that is not a whole H3FN version 1 or does not fit */
int overlay_face_read(struct overlay_face *face, struct overlay_atlas *atlas, const unsigned char *data,
	unsigned long size);
/* text in UTF-8; upper: in capitals */
float overlay_face_width(const struct overlay_face *face, const char *text, int upper);
/* drawn from the pen at x on the baseline; returns its width */
float overlay_face_draw(const struct overlay_face *face, const struct overlay_atlas *atlas, float x, float baseline,
	const char *text, int upper, overlay_quad_function quad, void *user);

#endif
