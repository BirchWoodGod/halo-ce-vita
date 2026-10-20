/*
PC_TEXT_ENTRY_OVERLAY.C

The PC build's text box (port/linux/src/pc_text_entry.h) drawn over
everything, in the Halo 3 menus' look: the screen dimmed, a dark blue
panel with the title (what the menu asks for: "New profile's name",
"Password"), a rule, the text in its field with a blinking caret (stars for
a password), how many characters of how many, and under it, once a
gamepad is used, the on-screen keyboard - four rows of ten keys and the row
of SHIFT, #+=, SPACE, DELETE and DONE, the key chosen in the menus' orange -
and a strip of the buttons that work, with the icons of the device in use
(display.button_icons: the keyboard's keys, an Xbox pad's, a
PlayStation's). The Halo 3 set's own fonts (menus/halo3: pc\h3\font_*)
where the map has them, the game's terminal font otherwise; quads and text
only, as the latency meter and the party's chat draw (no new shader).

interface.c's overlays call pc_text_entry_overlay_draw each frame, last.
*/

#ifndef HALO_VITA

#include "cseries.h"
#include "interface/hud_draw.h"
#include "interface/interface.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "../src/lang.h"
#include "../src/pc_text_entry.h"
#ifdef HALO_KEY_GLYPHS
#include "../../vita/include/vita_controls.h"
#include "../include/halo_button_icons.h"
#endif

#include "pc_text_entry_overlay.h"

#include <string.h>

unsigned long system_milliseconds(void);
void draw_quad(rectangle2d *rectangle, pixel32 color);
void platform_log(char const *format, ...);

/* (a hint's icon: none, its name written instead; VITA_GLYPH_NONE) */
#define NO_GLYPH 0

/* the panel, in the menus' 640x480 */
enum
{
	PANEL_WIDTH = 500,
	PANEL_HEIGHT = 134,
	PANEL_GRID_HEIGHT = 306,
	PANEL_MARGIN = 18,
	FIELD_TOP = 58,
	FIELD_HEIGHT = 34,
	FIELD_PADDING = 8,
	GRID_GAP = 14,
	CELL_WIDTH = 44,
	CELL_HEIGHT = 28,
	CELL_SPACING = 4,
	STRIP_HEIGHT = 22,
	ICON_SIZE = 16,
	CARET_BLINK_MS = 1060,
	REFUSED_FLASH_MS = 700,
};

/* the Halo 3 menus' colours (menus/halo3) */
#define COLOR_DIM 0xA0000000UL
#define COLOR_PANEL 0xF0142236UL
#define COLOR_PANEL_EDGE 0xFF35506DUL
#define COLOR_RULE 0xA0C5D0DDUL
#define COLOR_FIELD 0xF0060B14UL
#define COLOR_FIELD_EDGE 0xFFC98B3BUL
#define COLOR_CELL 0xD01B2C42UL
#define COLOR_CELL_CHOSEN 0xFFB4632AUL
#define COLOR_STRIP 0xE00C1626UL
#define COLOR_CARET 0xFFF2F6FAUL

static real_argb_color const color_title = { 1.0f, 0.949f, 0.965f, 0.980f };
static real_argb_color const color_text = { 1.0f, 0.933f, 0.949f, 0.965f };
static real_argb_color const color_dim_text = { 1.0f, 0.561f, 0.639f, 0.722f };
static real_argb_color const color_refused = { 1.0f, 0.878f, 0.376f, 0.251f };
static real_argb_color const color_key = { 1.0f, 0.773f, 0.816f, 0.867f };

/* a font: the Halo 3 set's by its name where the map has it, else the
game's terminal font */
static long overlay_font(char const *name)
{
	long index = tag_loaded(FONT_GROUP_TAG, name);

	return index != NONE ? index : interface_get_tag_index(_interface_font_terminal);
}

static short font_height(long font_index)
{
	struct font_header *font = font_definition_get(font_index);

	return (short)(font->ascending_height + font->descending_height);
}

/* UTF-8 (a translation) as the game's 16-bit text: the Basic Multilingual
Plane's characters, "?" for any other; and the typed text's '|', which the
game's text drawing reads as a code, drawn as a broken bar */
static void overlay_wide(wchar_t *out, int count, char const *utf8)
{
	unsigned char const *text = (unsigned char const *)utf8;
	int length = 0;

	while (*text && length < count - 1)
	{
		unsigned long character = *text++;
		int extra = character >= 0xF0 ? 3 : character >= 0xE0 ? 2 : character >= 0xC0 ? 1 : 0;

		if (extra)
		{
			character &= 0x3F >> extra;
			while (extra-- > 0 && (*text & 0xC0) == 0x80)
				character = (character << 6) | (*text++ & 0x3F);
		}
		if (character == '|')
			character = 0xA6;
		out[length++] = (wchar_t)(character > 0xFFFF ? '?' : character);
	}
	out[length] = 0;
}

static short text_width(long font_index, wchar_t const *text)
{
	rectangle2d bounds = { 0, 0, 2000, 2000 };
	rectangle2d text_bounds, cursor_bounds;

	if (!text[0])
		return 0;
	draw_string_set_draw_mode(font_index, NONE, 0, 0, &color_text);
	draw_unicode_string_compute_bounds(&bounds, text, &text_bounds, &cursor_bounds);
	return (short)(text_bounds.x1 - text_bounds.x0);
}

static void draw_text(long font_index, short justification, real_argb_color const *color, rectangle2d *where,
	wchar_t const *text)
{
	draw_string_set_draw_mode(font_index, NONE, justification, 0, color);
	rasterizer_draw_unicode_string(where, NULL, NULL, 0, text);
}

static void frame_rectangle(rectangle2d const *rectangle, pixel32 color)
{
	rectangle2d edge = *rectangle;

	edge.y1 = (short)(edge.y0 + 1);
	draw_quad(&edge, color);
	edge.y0 = (short)(rectangle->y1 - 1);
	edge.y1 = rectangle->y1;
	draw_quad(&edge, color);
	edge = *rectangle;
	edge.x1 = (short)(edge.x0 + 1);
	draw_quad(&edge, color);
	edge.x0 = (short)(rectangle->x1 - 1);
	edge.x1 = rectangle->x1;
	draw_quad(&edge, color);
}

/* the field: the text, scrolled so the caret shows, the caret blinking */
static void draw_field(struct pc_text_entry_view const *view, rectangle2d const *field, long font_index,
	unsigned long now)
{
	wchar_t text[PC_TEXT_TEXT_SIZE];
	wchar_t before[PC_TEXT_TEXT_SIZE];
	short room = (short)(field->x1 - field->x0 - 2 * FIELD_PADDING);
	short height = font_height(font_index);
	int first = 0;
	short caret_x;
	rectangle2d where, caret;

	overlay_wide(text, PC_TEXT_TEXT_SIZE, view->shown);
	/* (the first character shown: the caret in the field's width) */
	for (;;)
	{
		memcpy(before, text + first, sizeof(wchar_t) * (size_t)(view->caret - first));
		before[view->caret - first] = 0;
		if (first >= view->caret || text_width(font_index, before) <= room - 4)
			break;
		first++;
	}
	caret_x = (short)(field->x0 + FIELD_PADDING + text_width(font_index, before));
	where.x0 = (short)(field->x0 + FIELD_PADDING);
	where.x1 = (short)(field->x1 - FIELD_PADDING);
	where.y0 = (short)(field->y0 + (FIELD_HEIGHT - height) / 2);
	where.y1 = (short)(where.y0 + height + 4);
	draw_text(font_index, 0, &color_text, &where, text + first);
	if ((now - view->opened_ms) % CARET_BLINK_MS < CARET_BLINK_MS * 6 / 10)
	{
		caret.x0 = caret_x;
		caret.x1 = (short)(caret_x + 2);
		caret.y0 = (short)(where.y0 - 1);
		caret.y1 = (short)(where.y0 + height + 1);
		draw_quad(&caret, COLOR_CARET);
	}
}

/* the grid's key's label */
static void grid_label(struct pc_text_entry_view const *view, int row, int column, wchar_t *label, int count)
{
	char text[2] = { 0, 0 };

	if (row < PC_TEXT_GRID_CHARACTER_ROWS)
	{
		text[0] = pc_text_entry_grid_character(view->symbols, view->shift, row, column);
		overlay_wide(label, count, text);
		return;
	}
	switch (column / 2)
	{
	case PC_TEXT_ACTION_SHIFT: overlay_wide(label, count, view->shift ? "abc" : "ABC"); break;
	case PC_TEXT_ACTION_SYMBOLS: overlay_wide(label, count, view->symbols ? "abc" : "#+="); break;
	case PC_TEXT_ACTION_SPACE: overlay_wide(label, count, T("SPACE")); break;
	case PC_TEXT_ACTION_DELETE: overlay_wide(label, count, T("DELETE")); break;
	default: overlay_wide(label, count, T("DONE")); break;
	}
}

static void draw_grid(struct pc_text_entry_view const *view, short left, short top, long font_index)
{
	short height = font_height(font_index);
	int row, column;

	for (row = 0; row < PC_TEXT_GRID_ROWS; row++)
	{
		int span = row == PC_TEXT_GRID_CHARACTER_ROWS ? 2 : 1;

		for (column = 0; column < PC_TEXT_GRID_COLUMNS; column += span)
		{
			rectangle2d cell, where;
			wchar_t label[16];
			int chosen = view->row == row && (view->column == column || (span == 2 && view->column == column + 1));

			cell.x0 = (short)(left + column * (CELL_WIDTH + CELL_SPACING));
			cell.x1 = (short)(cell.x0 + span * CELL_WIDTH + (span - 1) * CELL_SPACING);
			cell.y0 = (short)(top + row * (CELL_HEIGHT + CELL_SPACING));
			cell.y1 = (short)(cell.y0 + CELL_HEIGHT);
			draw_quad(&cell, chosen ? COLOR_CELL_CHOSEN : COLOR_CELL);
			grid_label(view, row, column, label, (int)NUMBEROF(label));
			where = cell;
			where.y0 = (short)(cell.y0 + (CELL_HEIGHT - height) / 2);
			where.y1 = (short)(where.y0 + height + 4);
			draw_text(font_index, 2, chosen ? &color_title : &color_key, &where, label);
		}
	}
}

/* the strip's hints: an icon (a glyph of halo_button_icons.h; NO_GLYPH for
none, its name then written) and a word, each after the last */
struct hint
{
	short glyph;
	char const *name;
	char const *word;
};

static short draw_hint(short x, short y, struct hint const *hint, long font_index)
{
	wchar_t text[48];
	rectangle2d where;
	short height = font_height(font_index);

#ifdef HALO_KEY_GLYPHS
	if (hint->glyph != NO_GLYPH)
	{
		real_rectangle2d icon;

		icon.x0 = (real)x;
		icon.x1 = (real)(x + ICON_SIZE);
		icon.y0 = (real)(y + (STRIP_HEIGHT - ICON_SIZE) / 2);
		icon.y1 = icon.y0 + (real)ICON_SIZE;
		if (hud_vita_glyph_draw_in_rect(hint->glyph, &icon, 0xFFFFFFFFUL))
			x = (short)(x + ICON_SIZE + 4);
	}
#endif
	where.x0 = x;
	where.x1 = (short)(x + 300);
	where.y0 = (short)(y + (STRIP_HEIGHT - height) / 2);
	where.y1 = (short)(where.y0 + height + 4);
	if (hint->name && hint->name[0])
	{
		overlay_wide(text, (int)NUMBEROF(text), hint->name);
		draw_text(font_index, 0, &color_key, &where, text);
		where.x0 = (short)(where.x0 + text_width(font_index, text) + 5);
	}
	overlay_wide(text, (int)NUMBEROF(text), T(hint->word));
	draw_text(font_index, 0, &color_text, &where, text);
	return (short)(where.x0 + text_width(font_index, text) + 14);
}

/* the buttons that work, as the device in use shows them: the keyboard's
keys, or the gamepad's (the grid shown) */
static void draw_hints(struct pc_text_entry_view const *view, rectangle2d const *strip, long font_index)
{
	static struct hint const keyboard[] =
	{
		{ NO_GLYPH, "Enter", N_("Accept") }, { NO_GLYPH, "Esc", N_("Cancel") },
		{ NO_GLYPH, "Ctrl+V", N_("Paste") },
	};
	static struct hint const pad_names[] =
	{
		{ NO_GLYPH, "A", N_("Select") }, { NO_GLYPH, "B", N_("Delete") },
		{ NO_GLYPH, "X", N_("Space") }, { NO_GLYPH, "Y", N_("Cancel") },
		{ NO_GLYPH, "Start", N_("Accept") },
	};
	struct hint hints[5];
	int count, index;
	short x = (short)(strip->x0 + 8);

	if (view->grid)
	{
		count = (int)NUMBEROF(pad_names);
		memcpy(hints, pad_names, sizeof(pad_names));
#ifdef HALO_KEY_GLYPHS
		{
			/* (the pad's own icons: an Xbox pad's buttons, or a
			PlayStation's) */
			static short const xbox_buttons[5] = { 0, 1, 2, 3, 12 };
			static short const playstation[5] = { VITA_GLYPH_CROSS, VITA_GLYPH_CIRCLE, VITA_GLYPH_SQUARE,
				VITA_GLYPH_TRIANGLE, VITA_GLYPH_START };
			int playstation_icons = halo_button_icons_shown() == _halo_button_icons_playstation;

			for (index = 0; index < count; index++)
			{
				hints[index].glyph = playstation_icons ? playstation[index] : (short)(HALO_GLYPH_XBOX_FIRST + xbox_buttons[index]);
				hints[index].name = NULL;
			}
		}
#endif
	}
	else
	{
		count = (int)NUMBEROF(keyboard);
		memcpy(hints, keyboard, sizeof(keyboard));
#ifdef HALO_KEY_GLYPHS
		hints[0].glyph = (short)(HALO_GLYPH_KEY_FIRST + HALO_KEY_ENTER);
		hints[0].name = NULL;
		hints[1].glyph = (short)(HALO_GLYPH_KEY_FIRST + HALO_KEY_ESC);
		hints[1].name = NULL;
#endif
	}
	for (index = 0; index < count; index++)
		x = draw_hint(x, strip->y0, &hints[index], font_index);
}

void pc_text_entry_overlay_draw(
	void)
{
	static unsigned long logged_opened;
	struct pc_text_entry_view view;
	unsigned long now = system_milliseconds();
	rectangle2d bounds, panel, rule, field, strip, where;
	long title_font, item_font, small_font;
	short top, left, height;
	wchar_t text[PC_TEXT_TEXT_SIZE];
	char count[32];

	pc_text_entry_view(&view);
	if (!view.open)
		return;
	title_font = overlay_font("pc\\h3\\font_title");
	item_font = overlay_font("pc\\h3\\font_item");
	small_font = overlay_font("pc\\h3\\font_small");
	if (title_font == NONE || item_font == NONE || small_font == NONE)
		return;
	if (logged_opened != view.opened_ms)
	{
		logged_opened = view.opened_ms;
		platform_log("text entry: drawn (%s fonts)", tag_loaded(FONT_GROUP_TAG, "pc\\h3\\font_item") != NONE ?
			"the Halo 3 menus'" : "the game's terminal");
	}
	bounds = render.camera.window_bounds;
	offset_rectangle2d(&bounds, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
	draw_quad(&bounds, COLOR_DIM);

	height = (short)(view.grid ? PANEL_GRID_HEIGHT : PANEL_HEIGHT);
	left = (short)((bounds.x0 + bounds.x1) / 2 - PANEL_WIDTH / 2);
	top = (short)((bounds.y0 + bounds.y1) / 2 - height / 2);
	panel.x0 = left;
	panel.x1 = (short)(left + PANEL_WIDTH);
	panel.y0 = top;
	panel.y1 = (short)(top + height);
	draw_quad(&panel, COLOR_PANEL);
	frame_rectangle(&panel, COLOR_PANEL_EDGE);

	/* the title and the count */
	where.x0 = (short)(panel.x0 + PANEL_MARGIN);
	where.x1 = (short)(panel.x1 - PANEL_MARGIN - 70);
	where.y0 = (short)(panel.y0 + 12);
	where.y1 = (short)(where.y0 + font_height(title_font) + 4);
	overlay_wide(text, PC_TEXT_TEXT_SIZE, view.title);
	draw_text(title_font, 0, &color_title, &where, text);
	snprintf(count, sizeof(count), "%d/%d", view.length, view.maximum);
	overlay_wide(text, PC_TEXT_TEXT_SIZE, count);
	where.x0 = (short)(panel.x1 - PANEL_MARGIN - 70);
	where.x1 = (short)(panel.x1 - PANEL_MARGIN);
	where.y0 = (short)(panel.y0 + 18);
	where.y1 = (short)(where.y0 + font_height(small_font) + 4);
	draw_text(small_font, 1, view.refused_ms && now - view.refused_ms < REFUSED_FLASH_MS ? &color_refused :
		&color_dim_text, &where, text);
	rule.x0 = panel.x0;
	rule.x1 = panel.x1;
	rule.y0 = (short)(panel.y0 + 44);
	rule.y1 = (short)(rule.y0 + 2);
	draw_quad(&rule, COLOR_RULE);

	/* the field */
	field.x0 = (short)(panel.x0 + PANEL_MARGIN);
	field.x1 = (short)(panel.x1 - PANEL_MARGIN);
	field.y0 = (short)(panel.y0 + FIELD_TOP);
	field.y1 = (short)(field.y0 + FIELD_HEIGHT);
	draw_quad(&field, COLOR_FIELD);
	frame_rectangle(&field, COLOR_FIELD_EDGE);
	draw_field(&view, &field, item_font, now);

	/* the on-screen keyboard */
	if (view.grid)
	{
		short grid_width = (short)(PC_TEXT_GRID_COLUMNS * CELL_WIDTH + (PC_TEXT_GRID_COLUMNS - 1) * CELL_SPACING);

		draw_grid(&view, (short)((panel.x0 + panel.x1) / 2 - grid_width / 2), (short)(field.y1 + GRID_GAP), item_font);
	}

	/* the strip of the buttons that work */
	strip.x0 = (short)(panel.x0 + 1);
	strip.x1 = (short)(panel.x1 - 1);
	strip.y1 = (short)(panel.y1 - 1);
	strip.y0 = (short)(strip.y1 - STRIP_HEIGHT);
	draw_quad(&strip, COLOR_STRIP);
	draw_hints(&view, &strip, small_font);
}

#else
/* (the Vita: the system's keyboard) */
typedef int pc_text_entry_overlay_none;
#endif
