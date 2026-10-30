/*
FONT_GROUP.C

symbols in this file:
0018C840 0070:
	_font_get_character_by_ascii_code (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "font_group.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes */

/* ---------- globals */

/* ---------- public code */

#ifdef HALO_LINUX
/* port: the Latin-1 letters spaced as their base letters. The UI fonts of the
Xbox's ui.map (small_ui, large_ui: the font tags as Bungie's English map has
them) carry the accented glyphs' pictures with advances and origins that are
not theirs - small_ui's "á" 14 pixels for a 7 wide picture, "é" 10, "ñ" 11,
its "Á" 7 for a 9 wide one -, so a translation's words (Spanish: "GUÁRDALO",
"NÚMERO") drew with a gap after one letter and overlapping after another.
Each accented glyph's ink (its picture's columns with any coverage) is put
where its base letter's ink starts and given the base letter's space after
it: its origin and advance, the picture itself untouched. Done once a font, as
it is first asked for one of them (a map's load gives the tag again, and the
mark with it); a letter with no base glyph, or a blank picture, is left as it
is */
#define FONT_LATIN1_FIXED 0x4C315846L /* 'L1XF' in the header's last pad */
/* (font_character: character, character_width, bitmap_width, bitmap_height,
bitmap_origin_x, bitmap_origin_y, hardware_character_index, pad, then the
pixels' offset) */
#define GLYPH_SHORTS(character) ((short *)(character))
#define GLYPH_PIXELS_OFFSET(character) (*(long *)((byte *)(character) + 16))

/* a glyph's ink: its first column with coverage and the one after its last;
FALSE for a blank or unreadable picture */
static boolean font_glyph_ink(
	struct font_header *font,
	struct font_character *character,
	short *ink_left,
	short *ink_right)
{
	short width = GLYPH_SHORTS(character)[2], height = GLYPH_SHORTS(character)[3];
	long offset = GLYPH_PIXELS_OFFSET(character);
	byte const *pixels = (byte const *)font->pixels.address;
	short x, y;

	*ink_left = width;
	*ink_right = 0;
	if (width <= 0 || height <= 0 || !pixels || offset < 0 || offset + (long)width * height > font->pixels.size)
		return FALSE;
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			if (pixels[offset + (long)y * width + x] >= 0x30)
			{
				if (x < *ink_left)
					*ink_left = x;
				if (x + 1 > *ink_right)
					*ink_right = (short)(x + 1);
			}
		}
	}
	return *ink_right > *ink_left;
}

static void font_latin1_advances(
	struct font_header *font)
{
	/* (each of 0xC0-0xFF: its base letter, 0 none) */
	static char const bases[64 + 1] =
		"AAAAAA\0CEEEEIIII\0NOOOOO\0OUUUUY\0\0aaaaaa\0ceeeeiiii\0nooooo\0ouuuuy\0y";
	word code;

	if (font->pad[8] == FONT_LATIN1_FIXED)
		return;
	font->pad[8] = FONT_LATIN1_FIXED;
	for (code = 0xC0; code <= 0xFF; code++)
	{
		struct font_character *accented = font_get_character_by_ascii_code(font, code);
		struct font_character *base;
		short base_left, base_right, ink_left, ink_right, lead, trail;

		if (!accented || !bases[code - 0xC0] || !(base = font_get_character_by_ascii_code(font, (word)bases[code - 0xC0])) ||
			!font_glyph_ink(font, base, &base_left, &base_right) || !font_glyph_ink(font, accented, &ink_left, &ink_right))
		{
			continue;
		}
		/* (the base's ink from the cursor - a picture is drawn from the cursor less its origin - and
		its space after the ink to the next letter) */
		lead = (short)(base_left - GLYPH_SHORTS(base)[4]);
		trail = (short)(GLYPH_SHORTS(base)[1] - (base_right - GLYPH_SHORTS(base)[4]));
		GLYPH_SHORTS(accented)[4] = (short)(ink_left - lead);
		GLYPH_SHORTS(accented)[1] = (short)(lead + (ink_right - ink_left) + trail);
	}
}
#endif

struct font_character *font_get_character_by_ascii_code(
	struct font_header *font,
	word character_code)
{
	struct font_character_table *character_table;
	struct font_character *character = NULL;

#ifdef HALO_LINUX
	if (character_code >= 0xC0 && character_code <= 0xFF && font->pad[8] != FONT_LATIN1_FIXED)
		font_latin1_advances(font);
#endif

	/* port: no glyph for a character past the font's tables, a table that
	isn't a whole 256 entries, or an entry past the font's characters (the
	map's font, and any string's characters, e.g. a player's name) */
	if (!VALID_INDEX(character_code >> 8, font->character_tables.count))
		return NULL;

	character_table = TAG_BLOCK_GET_ELEMENT(
		&font->character_tables,
		character_code >> 8,
		struct font_character_table);

	if (character_table->character_indices.count == 256)
	{
		short *character_index = TAG_BLOCK_GET_ELEMENT(&character_table->character_indices, character_code & 0xFF, short);

		if (VALID_INDEX(*character_index, font->characters.count))
		{
			character = tag_block_get_element_with_size(
				&font->characters,
				*character_index,
				FONT_CHARACTER_SIZE);
		}
	}

	return character;
}

/* ---------- private code */
