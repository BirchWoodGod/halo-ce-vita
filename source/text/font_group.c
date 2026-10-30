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
/* port: the Latin-1 letters spaced as their base letters, and the
apostrophe as the comma. The UI fonts of the Xbox's ui.map (small_ui,
large_ui: the font tags as Bungie's English map has them) carry the accented
glyphs' pictures with advances and origins that are not theirs - small_ui's
"a acute" 14 pixels for a 7 wide picture, "e acute" 10, "n tilde" 11, its
"A acute" 7 for a 9 wide one, its "I acute" 5 wide where "I" is 2 -, and
small_ui's apostrophes advance 6 pixels for a 3 wide mark, so a translation's
words (Spanish: "GUÁRDALO", "VEHÍCULOS") and the port's ("can't") drew with a
gap after one letter and overlapping after another. Each accented glyph's
body - its rows below the base letter's top (the x-height for a small letter)
- is put where the base letter's ink is, the base letter's space after it; its
mark above, when it leans off the body's middle and the picture has room, is
centred over it (the picture's rows moved, in the font's own pixels: Spanish
"SÍ"'s accent sat right of the stem). Done once a font, as it is first asked
for one of them (a map's load gives the tag again, and the mark with it); a
letter with no base glyph, or a blank picture, is left as it is */
#define FONT_LATIN1_FIXED 0x4C315846L /* 'L1XF' in the header's last pad */
/* (font_character: character, character_width, bitmap_width, bitmap_height,
bitmap_origin_x, bitmap_origin_y, hardware_character_index, pad, then the
pixels' offset) */
#define GLYPH_SHORTS(character) ((short *)(character))
#define GLYPH_PIXELS_OFFSET(character) (*(long *)((byte *)(character) + 16))

/* a glyph's pixels (NULL for a blank or unreadable picture) */
static byte *font_glyph_pixels(
	struct font_header *font,
	struct font_character *character)
{
	short width = GLYPH_SHORTS(character)[2], height = GLYPH_SHORTS(character)[3];
	long offset = GLYPH_PIXELS_OFFSET(character);

	if (width <= 0 || height <= 0 || !font->pixels.address || offset < 0 ||
		offset + (long)width * height > font->pixels.size)
	{
		return NULL;
	}
	return (byte *)font->pixels.address + offset;
}

/* the ink of a glyph's rows first_row to end_row (end excluded): its first
column with coverage and the one after its last; FALSE for none */
static boolean font_glyph_ink_rows(
	struct font_header *font,
	struct font_character *character,
	short first_row,
	short end_row,
	short *ink_left,
	short *ink_right)
{
	byte const *pixels = font_glyph_pixels(font, character);
	short width = GLYPH_SHORTS(character)[2], x, y;

	*ink_left = width;
	*ink_right = 0;
	if (!pixels)
		return FALSE;
	first_row = MAX(first_row, 0);
	end_row = MIN(end_row, GLYPH_SHORTS(character)[3]);
	for (y = first_row; y < end_row; y++)
	{
		for (x = 0; x < width; x++)
		{
			if (pixels[(long)y * width + x] >= 0x30)
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

static boolean font_glyph_ink(
	struct font_header *font,
	struct font_character *character,
	short *ink_left,
	short *ink_right)
{
	return font_glyph_ink_rows(font, character, 0, GLYPH_SHORTS(character)[3], ink_left, ink_right);
}

/* a glyph's rows first_row to end_row moved right by shift columns (left
when negative), the columns left empty */
static void font_glyph_shift_rows(
	struct font_header *font,
	struct font_character *character,
	short first_row,
	short end_row,
	short shift)
{
	byte *pixels = font_glyph_pixels(font, character);
	short width = GLYPH_SHORTS(character)[2], x, y;

	if (!pixels || !shift)
		return;
	for (y = MAX(first_row, 0); y < MIN(end_row, GLYPH_SHORTS(character)[3]); y++)
	{
		byte *row = pixels + (long)y * width;

		if (shift > 0)
		{
			for (x = (short)(width - 1); x >= 0; x--)
				row[x] = x - shift >= 0 ? row[x - shift] : 0;
		}
		else
		{
			for (x = 0; x < width; x++)
				row[x] = x - shift < width ? row[x - shift] : 0;
		}
	}
}

static void font_latin1_advances(
	struct font_header *font)
{
	/* (each of 0xC0-0xFF: its base letter, 0 none) */
	static char const bases[64 + 1] =
		"AAAAAA\0CEEEEIIII\0NOOOOO\0OUUUUY\0\0aaaaaa\0ceeeeiiii\0nooooo\0ouuuuy\0y";
	static word const apostrophes[] = { 0x27, 0x2019 };
	struct font_character *x_glyph, *comma;
	short x_height = 0, index;
	word code;

	if (font->pad[8] == FONT_LATIN1_FIXED)
		return;
	font->pad[8] = FONT_LATIN1_FIXED;
	if ((x_glyph = font_get_character_by_ascii_code(font, 'x')) != NULL)
		x_height = GLYPH_SHORTS(x_glyph)[5];
	for (code = 0xC0; code <= 0xFF; code++)
	{
		struct font_character *accented = font_get_character_by_ascii_code(font, code);
		struct font_character *base;
		short base_left, base_right, body_left, body_right, mark_left, mark_right, top, body_row, lead, d2, shift;

		if (!accented || !bases[code - 0xC0] || !(base = font_get_character_by_ascii_code(font, (word)bases[code - 0xC0])) ||
			!font_glyph_ink(font, base, &base_left, &base_right))
		{
			continue;
		}
		/* (the body: the rows from the base letter's top - a small letter's x-height, past an "i"'s dot
		- down; a picture's row y stands top - y over the baseline: origin_y less y) */
		top = (short)(bases[code - 0xC0] >= 'a' && x_height > 0 ? x_height : GLYPH_SHORTS(base)[5]);
		body_row = (short)MAX(GLYPH_SHORTS(accented)[5] - top, 0);
		if (!font_glyph_ink_rows(font, accented, body_row, GLYPH_SHORTS(accented)[3], &body_left, &body_right))
			continue;
		/* (the mark above, centred over the body as far as the picture has room: its rows, or the
		body's, moved) */
		if (body_row > 0 && font_glyph_ink_rows(font, accented, 0, body_row, &mark_left, &mark_right))
		{
			d2 = (short)((mark_left + mark_right) - (body_left + body_right));
			shift = (short)(d2 / 2);
			if (shift > 0)
			{
				/* (the mark right of the body: the body moved right, or the mark left, as far as each may) */
				short body_room = (short)(GLYPH_SHORTS(accented)[2] - body_right), mark_room = mark_left;
				short body_shift = MIN(shift, body_room), mark_shift = MIN((short)(shift - body_shift), mark_room);

				font_glyph_shift_rows(font, accented, body_row, GLYPH_SHORTS(accented)[3], body_shift);
				font_glyph_shift_rows(font, accented, 0, body_row, (short)-mark_shift);
				body_left = (short)(body_left + body_shift);
				body_right = (short)(body_right + body_shift);
			}
			else if (shift < 0)
			{
				short mark_room = (short)(GLYPH_SHORTS(accented)[2] - mark_right), body_room = body_left;
				short mark_shift = MIN((short)-shift, mark_room), body_shift = MIN((short)(-shift - mark_shift), body_room);

				font_glyph_shift_rows(font, accented, 0, body_row, mark_shift);
				font_glyph_shift_rows(font, accented, body_row, GLYPH_SHORTS(accented)[3], (short)-body_shift);
				body_left = (short)(body_left - body_shift);
				body_right = (short)(body_right - body_shift);
			}
		}
		/* (the body's ink where the base's is from the cursor - a picture is drawn from the cursor less
		its origin -, and the base's space after its ink after the body's: a body drawn narrower than
		its base letter - small_ui's "U acute" - is no gap) */
		lead = (short)(base_left - GLYPH_SHORTS(base)[4]);
		GLYPH_SHORTS(accented)[4] = (short)(body_left - lead);
		GLYPH_SHORTS(accented)[1] = (short)(GLYPH_SHORTS(base)[1] - ((base_right - base_left) - (body_right - body_left)));
	}
	/* (the apostrophes: the comma's space before and after the mark, a pixel after it at least; only
	ever narrower than the font has them) */
	comma = font_get_character_by_ascii_code(font, ',');
	for (index = 0; comma && index < (short)NUMBEROF(apostrophes); index++)
	{
		struct font_character *apostrophe = font_get_character_by_ascii_code(font, apostrophes[index]);
		short comma_left, comma_right, ink_left, ink_right, lead, trail, advance;

		if (!apostrophe || !font_glyph_ink(font, comma, &comma_left, &comma_right) ||
			!font_glyph_ink(font, apostrophe, &ink_left, &ink_right))
		{
			continue;
		}
		lead = (short)(comma_left - GLYPH_SHORTS(comma)[4]);
		trail = (short)MAX(GLYPH_SHORTS(comma)[1] - (comma_right - GLYPH_SHORTS(comma)[4]), 1);
		advance = (short)(lead + (ink_right - ink_left) + trail);
		if (advance < GLYPH_SHORTS(apostrophe)[1])
		{
			GLYPH_SHORTS(apostrophe)[4] = (short)(ink_left - lead);
			GLYPH_SHORTS(apostrophe)[1] = advance;
		}
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
	if ((character_code >= 0xC0 && character_code <= 0xFF || character_code == 0x27 || character_code == 0x2019) &&
		font->pad[8] != FONT_LATIN1_FIXED)
	{
		font_latin1_advances(font);
	}
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
