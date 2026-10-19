/*
PARTY_CHAT_OVERLAY.C

The party's chat over a level (port/linux/src/p2p_party.h): its newest lines
at the left of the screen, under the grenades and above the motion tracker,
each for PARTY_CHAT_SHOWN_MILLISECONDS after it came (then fading), in the
game's terminal font over a dark band: who said it (a member's name as the
party lists show it: a friend's as the friends list keeps it, another's with
their code's first characters) and what. The party's own word (a member
joined, left, leads) in another colour. Nothing is read from the lines but
text the party already cleaned (printable ASCII, no markup); nothing here
interprets it.

Typing and quick chat: the settings panel's Party page (SELECT + D-pad up
opens it at once, in play and in the menus), the Halo 3 PARTY screen in the
menus. The same GPU programs as the latency meter (the game's own text and
quads): no new shader.

interface.c's overlays call party_chat_overlay_draw each frame.
*/

#include "cseries.h"
#include "game/game.h"
#include "interface/interface.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "party_chat_overlay.h"
#include "../src/p2p_party.h"

#include <stdio.h>
#include <string.h>

enum
{
	PARTY_CHAT_OVERLAY_LINES = 5,
	PARTY_CHAT_SHOWN_MILLISECONDS = 10000,
	PARTY_CHAT_FADE_MILLISECONDS = 1500,
	/* the lines read again this often (the friends' lock, briefly) */
	PARTY_CHAT_POLL_MILLISECONDS = 250,
	PARTY_CHAT_LEFT_MARGIN = 12,
};

/* the band's top: this share of the screen's height down (under the
grenades and flashlight at the top left, clear of the motion tracker) */
#define PARTY_CHAT_TOP_FRACTION 0.38f

unsigned long system_milliseconds(void);
void draw_quad(rectangle2d *rectangle, pixel32 color);

static struct
{
	struct p2p_party_chat_line lines[PARTY_CHAT_OVERLAY_LINES];
	int count;
	/* when each line came here (by its number) */
	unsigned long came[PARTY_CHAT_OVERLAY_LINES];
	unsigned long polled_time;
	unsigned long newest;
	/* the newest line drawn over a level (the log says each once) */
	unsigned long drawn;
} party_chat_overlay;

void platform_log(char const *format, ...);

/* the newest lines, and when each came (a line new to the overlay: now) */
static void party_chat_overlay_poll(unsigned long now)
{
	struct p2p_party_chat_line lines[P2P_PARTY_CHAT_LINES];
	unsigned long came[PARTY_CHAT_OVERLAY_LINES];
	int count, first, index, old;
	int first_look = !party_chat_overlay.polled_time;

	if (party_chat_overlay.polled_time && now - party_chat_overlay.polled_time < PARTY_CHAT_POLL_MILLISECONDS)
		return;
	party_chat_overlay.polled_time = now ? now : 1;
	if (p2p_party_chat_newest() == party_chat_overlay.newest)
		return;
	count = p2p_party_chat_lines(lines, P2P_PARTY_CHAT_LINES, 0);
	first = count > PARTY_CHAT_OVERLAY_LINES ? count - PARTY_CHAT_OVERLAY_LINES : 0;
	for (index = first; index < count; index++)
	{
		came[index - first] = now;
		for (old = 0; old < party_chat_overlay.count; old++)
			if (party_chat_overlay.lines[old].number == lines[index].number)
				came[index - first] = party_chat_overlay.came[old];
	}
	party_chat_overlay.count = count - first;
	memcpy(party_chat_overlay.lines, lines + first, sizeof(lines[0]) * (size_t)party_chat_overlay.count);
	memcpy(party_chat_overlay.came, came, sizeof(came[0]) * (size_t)party_chat_overlay.count);
	party_chat_overlay.newest = count ? lines[count - 1].number : 0;
	/* (the first look: what was said before, in the menus, shows no more) */
	if (first_look)
		for (index = 0; index < party_chat_overlay.count; index++)
			party_chat_overlay.came[index] = now - PARTY_CHAT_SHOWN_MILLISECONDS;
}

static pixel32 party_chat_pixel(real alpha, real red, real green, real blue)
{
	return ((pixel32)(PIN(alpha, 0.0f, 1.0f) * 255.0f + 0.5f) << 24) |
		((pixel32)(PIN(red, 0.0f, 1.0f) * 255.0f + 0.5f) << 16) |
		((pixel32)(PIN(green, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
		(pixel32)(PIN(blue, 0.0f, 1.0f) * 255.0f + 0.5f);
}

void party_chat_overlay_draw(
	void)
{
	extern int halo_main_menu_loaded(void);
	unsigned long now = system_milliseconds();
	long font_index;
	struct font_header *font;
	short line_height, top;
	int index, shown = 0;
	rectangle2d bounds;

	/* (read in the menus too: a line said there is not new in the level) */
	party_chat_overlay_poll(now);
	if (halo_main_menu_loaded() || !game_in_progress())
		return;
	for (index = 0; index < party_chat_overlay.count; index++)
		shown += now - party_chat_overlay.came[index] < PARTY_CHAT_SHOWN_MILLISECONDS;
	if (!shown)
		return;
	font_index = interface_get_tag_index(_interface_font_terminal);
	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0)
		return;
	bounds = render.camera.window_bounds;
	offset_rectangle2d(&bounds, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
	top = (short)(bounds.y0 + (bounds.y1 - bounds.y0) * PARTY_CHAT_TOP_FRACTION);
	for (index = 0; index < party_chat_overlay.count; index++)
	{
		const struct p2p_party_chat_line *line = &party_chat_overlay.lines[index];
		unsigned long age = now - party_chat_overlay.came[index];
		real alpha;
		real_argb_color color;
		char text[P2P_PARTY_NAME_SIZE + P2P_PARTY_CHAT_SIZE + 4];
		wchar_t wide[sizeof(text)];
		rectangle2d row, band;
		int at;

		if (age >= PARTY_CHAT_SHOWN_MILLISECONDS)
			continue;
		alpha = age + PARTY_CHAT_FADE_MILLISECONDS > PARTY_CHAT_SHOWN_MILLISECONDS ?
			(real)(PARTY_CHAT_SHOWN_MILLISECONDS - age) / (real)PARTY_CHAT_FADE_MILLISECONDS : 1.0f;
		if (line->notice)
			snprintf(text, sizeof(text), "%s", line->text);
		else
			snprintf(text, sizeof(text), "%s: %s", line->name, line->text);
		/* (the party's text is printable ASCII already: as wide characters) */
		for (at = 0; text[at] && at < (int)NUMBEROF(wide) - 1; at++)
			wide[at] = (wchar_t)(unsigned char)text[at];
		wide[at] = 0;
		row.x0 = (short)(bounds.x0 + PARTY_CHAT_LEFT_MARGIN);
		row.x1 = (short)(bounds.x0 + (bounds.x1 - bounds.x0) * 3 / 5);
		row.y0 = top;
		row.y1 = (short)(top + line_height);
		band = row;
		band.x0 = (short)(row.x0 - 4);
		band.x1 = (short)(row.x1 + 4);
		draw_quad(&band, party_chat_pixel(0.45f * alpha, 0.0f, 0.0f, 0.0f));
		color.alpha = alpha;
		if (line->notice)
		{
			color.red = 0.95f; color.green = 0.82f; color.blue = 0.45f;
		}
		else if (line->self)
		{
			color.red = 0.70f; color.green = 0.85f; color.blue = 1.0f;
		}
		else
		{
			color.red = color.green = color.blue = 0.95f;
		}
		draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);
		rasterizer_draw_unicode_string(&row, NULL, NULL, 0, wide);
		if (line->number > party_chat_overlay.drawn)
		{
			party_chat_overlay.drawn = line->number;
			platform_log("party: chat shown over the game: %s", text);
		}
		top = (short)(top + line_height + 2);
	}
}
