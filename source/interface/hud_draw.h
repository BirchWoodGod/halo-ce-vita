/*
HUD_DRAW.H

Narrow cross-translation-unit interface owned by HUD_DRAW.C.
*/

#ifndef __HUD_DRAW_H
#define __HUD_DRAW_H
#pragma once

#include "math/integer_math.h"
#include "math/real_math.h"

/* ---------- constants */

enum
{
	STACK_BUFFER_LENGTH = 0x80,
	STACK_BUFFER_FILL = 0x62626262,
};

enum hud_draw_flags
{
	_hud_draw_flashing_bit = 0,
	_hud_draw_disabled_bit,
	_hud_draw_in_multiplayer_bit,

	NUMBER_OF_HUD_DRAW_FLAGS
};

/* ---------- macros */

/* The stack sentinel: a guarded function starts with
	long return_eip = get_return_eip();
	long stack_buffer[STACK_BUFFER_LENGTH];
	csmemset(stack_buffer, 0x62, sizeof(stack_buffer));
and ends with this check of both its return address and its fill pattern. */
#define match_assert_stack_frame(file, line) \
{ \
	short corrupt_index = check_stack_buffer(stack_buffer); \
	match_vassert(file, line, return_eip==get_return_eip(), "corrupt return address!"); \
	match_vassert(file, line, corrupt_index==NONE, csprintf(temporary, "corrupt stack at %d!", corrupt_index)); \
}

/* ---------- structures */

struct bitmap_data;
struct hud_absolute_placement_definition;
struct hud_color_definition;
struct hud_placement_definition;
struct meter_hud_element_definition;
struct number_hud_element_definition;
struct static_hud_element_definition;
struct weapon_hud_overlay_definition;

/* ---------- prototypes/HUD_DRAW.C */

#ifdef HALO_LINUX
struct static_hud_element_definition;
void hud_draw_set_scope_centered(
	boolean centered);
boolean hud_static_element_shown_by_zoom(
	struct static_hud_element_definition const *element);
real hud_mcc_bitmap_scale(
	long bitmap_group_index,
	boolean on_canvas);
#endif

long get_return_eip(
	void);
real hud_globals_get_scale(
	boolean in_multiplayer);
void hud_retrieve_bitmap_and_bounding_rect(
	long bitmap_group_index,
	short sequence_index,
	short frame_index,
	struct bitmap_data const **bitmap,
	real_rectangle2d const **clip);
long fast_ftol_C(
	real value);
pixel32 real_alpha_intensity_to_pixel32(
	real alpha,
	real intensity);
void hud_calculate_point(
	short local_player_index,
	struct hud_absolute_placement_definition const *absolute_placement,
	struct hud_placement_definition const *placement,
	struct bitmap_data const *bitmap_data,
	boolean in_multiplayer,
	real override_scale,
	point2d *result);
long get_flash_duration(
	struct hud_color_definition const *hud_color);
pixel32 get_flash_color(
	struct hud_color_definition const *hud_color,
	long reference_value);
void hud_draw_weapon_overlays(
	short local_player_index,
	struct hud_absolute_placement_definition const *absolute_placement,
	struct weapon_hud_overlay_definition const *overlays,
	long type_flags,
	long reference_time,
	short draw_flags,
	boolean in_multiplayer);
void hud_draw_bitmap(
	struct bitmap_data const *bitmap,
	struct hud_absolute_placement_definition const *absolute_placement,
	struct hud_placement_definition const *placement,
	real_rectangle2d const *clip,
	real scale,
	real theta,
	pixel32 color,
	boolean in_multiplayer,
	boolean is_interface_bitmap,
	boolean is_crosshair_bitmap);
void hud_draw_meter(
	short local_player_index,
	struct hud_absolute_placement_definition const *placement,
	struct meter_hud_element_definition const *meter,
	byte min_value,
	byte max_value,
	short draw_flags,
	real reference_time,
	real reference_value);
void hud_draw_static_element(
	short local_player_index,
	struct hud_absolute_placement_definition const *placement,
	struct static_hud_element_definition const *static_element,
	short draw_flags,
	long flash_reference_time);
void hud_draw_bitmap_direct(
	struct bitmap_data const *bitmap,
	short placement,
	point2d const *point,
	real_rectangle2d const *clip,
	real scale,
	real theta,
	pixel32 color,
	boolean is_interface_bitmap);
void hud_draw_numbers(
	short local_player_index,
	struct hud_absolute_placement_definition const *absolute_placement,
	struct number_hud_element_definition const *numbers,
	short value,
	short decimal_value,
	short draw_flags,
	long flash_reference_time,
	real override_scale);

/* (port) the PlayStation button icons (the settings panel's Button icons):
the Vita's, and the PC builds' (Linux and Windows: HALO_BUTTON_ICONS) */
#if defined(HALO_VITA) || (defined(HALO_LINUX) && (defined(__linux__) || defined(_WIN32)) && !defined(HALO_ANDROID))
#define HALO_BUTTON_GLYPHS 1
#endif
/* (port) and the desktop build's keyboard and mouse icons, and the Xbox's
in the Halo 3 menus (display.button_icons: port/linux/include/
halo_button_icons.h); never the Vita's */
#if defined(HALO_BUTTON_GLYPHS) && !defined(HALO_VITA)
#define HALO_KEY_GLYPHS 1
#endif

#ifdef HALO_BUTTON_GLYPHS
/* (port) the Vita's button icons, PlayStation's (hud_draw.c): a glyph is
a VITA_GLYPH_* (port/vita/include/vita_controls.h), 0 for the Xbox's icon */
short hud_vita_button_glyph(
	short icon_index);
short hud_vita_menu_button_glyph(
	long bitmap_group_index);
short hud_vita_glyph_face_icon(
	short glyph);
boolean hud_vita_glyph_drawn(
	short glyph);
wchar_t const *hud_vita_glyph_word(
	short glyph);
wchar_t const *hud_vita_glyph_text(
	short glyph);
#ifdef HALO_VITA
void hud_vita_score_hint(
	wchar_t *buffer,
	long buffer_size);
#endif
boolean hud_vita_glyph_draw(
	short glyph,
	struct bitmap_data const *bitmap,
	short placement,
	point2d const *point,
	real_rectangle2d const *clip,
	real scale,
	pixel32 color);
boolean hud_vita_glyph_draw_in_rect(
	short glyph,
	real_rectangle2d const *rectangle,
	pixel32 color);
#ifdef HALO_KEY_GLYPHS
/* the multiplayer score hint ("Hold BACK for score") naming the key Back is
on, with the keyboard's icons (game_engine.c) */
void hud_key_score_hint(
	wchar_t *buffer,
	long buffer_size);
/* how much wider than the Xbox icon's sprite a glyph that hud_vita_glyph_draw
draws there is (a wider keycap: SPACE, ENTER ...), in pixels: what a line of
text moves on by past the sprite's width; 0 for the others */
real hud_vita_glyph_flow_extra(
	short glyph,
	struct bitmap_data const *bitmap,
	short placement,
	real_rectangle2d const *clip,
	real scale);
#endif
#endif

/* ---------- public code */

__inline short check_stack_buffer(
	long *buffer)
{
	short index;

	for (index = STACK_BUFFER_LENGTH-1; index>=0; index--)
	{
		if (buffer[index]!=STACK_BUFFER_FILL)
			return index;
	}

	return NONE;
}

#endif /* __HUD_DRAW_H */
