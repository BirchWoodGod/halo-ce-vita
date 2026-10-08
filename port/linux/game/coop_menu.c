/*
COOP_MENU.C

Co-op from the campaign's own menus. Campaign, a profile, a level and a
difficulty, as in single player; on the difficulty screen A plays the level
alone (as ever) and Y hosts a network game of it on the difficulty selected
(ui_widget_event_handler_functions.c ui_widget_port_cooperative_campaign_host;
network_server_manager.c network_game_server_port_cooperative_from_menu).
The game's own lobby (connected_pregame_screen) is then the waiting screen:
"Waiting for your partner" in its message bar, and here, where the other
machines' panels go, the level, the difficulty, this machine's name, how
the game is reached (system link, online and its code, ad hoc) and B to
cancel; online also whether it is listed in the server browser, private
unless chosen (OpenCE's network.coop_public), and X to change it
(coop_menu_toggle_public). The partner finds the game in the System Link
list (Multiplayer, System Link, or the Vita settings panel's Join a game)
as "<host>: <level> (<difficulty>)" and joins it as any game; once their
player is in, the lobby's countdown starts the level (a few seconds; the
host's A sooner).

This file: that text, drawn over the game's own screens in the menus'
fonts (the maps have no strings for it), from the level and difficulty
names of the maps' own string lists.
*/

#include "cseries.h"
#include "bungie_net/network/transport.h"
#include "cache/cache_files.h"
#include "interface/ui_widget.h"
#include "main/main.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/text_group.h"
#include "text/unicode.h"
#include "halo_port_limits.h"

#include "coop_menu.h"

#include <string.h>
#include <wchar.h>

/* the platform layer's (port/linux/src/port_config.c, p2p.c) */
int config_boolean(const char *name);
int config_write_boolean(const char *name, int value);
int p2p_running(void);
void p2p_lobby_set_coop_public(int listed);
int p2p_lobby_coop_public(int *has_password);
void platform_log(char const *format, ...);
int p2p_hosting_code(char *code, int size);

static char const coop_menu_level_names_tag[] = "ui\\shell\\main_menu\\map_list";
static char const coop_menu_difficulty_names_tag[] = "ui\\shell\\main_menu\\player_profiles_select\\difficulty_names";
static char const coop_menu_difficulty_screen[] = "ui\\shell\\main_menu\\difficulty_select\\difficulty_select_list_screen";
static char const coop_menu_pregame_screen[] =
	"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen";

/* a string of a string list tag, on one line (the level names break theirs
for the level select's boxes) */
static void coop_menu_string(
	char const *tag_name,
	short index,
	wchar_t *text,
	int count)
{
	long tag_index = tag_loaded(UNICODE_STRING_LIST_TAG, tag_name);
	wchar_t const *string = tag_index != NONE ? unicode_string_list_get_string(tag_index, index) : NULL;
	int length = 0;
	boolean space = FALSE;

	if (count <= 0)
		return;
	for (; string && *string && length < count - 1; string++)
	{
		if (*string == L'\r' || *string == L'\n' || *string == L' ' || *string == L'\t')
		{
			space = length > 0;
			continue;
		}
		if (space && length < count - 2)
			text[length++] = L' ';
		space = FALSE;
		text[length++] = *string;
	}
	text[length] = 0;
}

void coop_menu_level_name(
	char const *map_name,
	wchar_t *text,
	int count)
{
	short level_index = map_name ? main_get_solo_level_from_name(map_name) : NONE;

	if (count <= 0)
		return;
	text[0] = 0;
	if (level_index != NONE)
		coop_menu_string(coop_menu_level_names_tag, level_index, text, count);
}

void coop_menu_difficulty_name(
	short difficulty,
	wchar_t *text,
	int count)
{
	if (count <= 0)
		return;
	text[0] = 0;
	coop_menu_string(coop_menu_difficulty_names_tag, (short)PIN(difficulty, 0, 3), text, count);
}

void coop_menu_game_description(
	wchar_t const *host,
	char const *map_name,
	short difficulty,
	wchar_t *text,
	int count)
{
	wchar_t level[48];
	wchar_t difficulty_name[16];

	coop_menu_level_name(map_name, level, NUMBEROF(level));
	coop_menu_difficulty_name(difficulty, difficulty_name, NUMBEROF(difficulty_name));
	usnprintf(text, count, L"%ls: %ls (%ls)", host, level, difficulty_name);
	text[count - 1] = 0;
}

void coop_menu_fit_line(
	wchar_t *text,
	short width,
	struct widget_instance *widget,
	boolean scroll)
{
	int length = (int)ustrlen(text);
	int offset;

	if (length <= 0 || !ui_widget_port_text_too_wide(widget, text, width))
		return;
	if (!scroll)
	{
		/* (the start, and "...") */
		wchar_t shown[64];

		for (offset = MIN(length, (int)NUMBEROF(shown) - 4); offset > 1; offset--)
		{
			ustrncpy(shown, text, offset);
			shown[offset] = 0;
			ustrncat(shown, L"...", NUMBEROF(shown) - 1);
			if (!ui_widget_port_text_too_wide(widget, shown, width))
				break;
		}
		ustrncpy(text, shown, (int)ustrlen(shown));
		text[ustrlen(shown)] = 0;
		return;
	}
	/* (scrolled: as far as the end fits, a character every 150 ms, held
	1.5 s at either end) */
	{
		int last;
		unsigned long period, now = system_milliseconds();

		for (last = 1; last < length && ui_widget_port_text_too_wide(widget, text + last, width); last++)
			;
		period = 1500 + (unsigned long)last * 150 + 1500;
		now %= period;
		offset = now < 1500 ? 0 : (int)MIN((unsigned long)last, (now - 1500) / 150);
		if (offset > 0)
			memmove(text, text + offset, (size_t)(length - offset + 1) * sizeof(wchar_t));
	}
}

boolean coop_menu_game_is_cooperative(
	struct network_game const *game)
{
	return game && game->variant.game_engine_index == 0 && main_get_solo_level_from_name(game->map.name) != NONE;
}

boolean coop_menu_available(
	void)
{
	char build[0x20];

	return cache_files_multiplayer_region(build) != NULL;
}

/* one line at (x, y) of the 640x480 menus, in the font and colour of a
widget of the maps' (its tag's name), its button names ("%b-button") drawn
as the menus' button icons */
static void coop_menu_draw(
	char const *style_widget,
	short x,
	short y,
	short width,
	wchar_t const *text)
{
	long font_index;
	real_argb_color color;
	rectangle2d bounds;

	if (!text[0] || !ui_widget_port_text_style(style_widget, &font_index, &color) || font_index == NONE)
		return;
	bounds.x0 = x;
	bounds.y0 = y;
	bounds.x1 = (short)(x + width);
	bounds.y1 = (short)(y + 40);
	draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);
	if (wcschr(text, L'%'))
		draw_string_and_hack_in_icons(&bounds, NULL, NULL, 0, text, FALSE);
	else
		rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
}

/* how the game is reached: ad hoc, online (and its code) or system link */
static void coop_menu_connection(
	wchar_t *text,
	int count)
{
	char code[32];

	if (config_boolean("network.adhoc"))
		usnprintf(text, count, L"Ad hoc");
	else if (config_boolean("network.online") && p2p_running() && p2p_hosting_code(code, (int)sizeof(code)) && code[0])
		usnprintf(text, count, L"Online, code %hs", code);
	else if (config_boolean("network.online") && p2p_running())
		usnprintf(text, count, L"Online");
	else
		usnprintf(text, count, L"System link");
	text[count - 1] = 0;
}

/* whether a co-op game hosted online is listed in the server browser (as
chosen, else network.coop_public, OpenCE's: private unless chosen; never
with the server browser off, network.public_lobby), and whether it asks for
the password */
static boolean coop_menu_public(
	boolean *password)
{
	int has_password = 0;
	boolean listed = p2p_lobby_coop_public(&has_password) != 0;

	if (password)
		*password = has_password != 0;
	return listed;
}

/* whether this machine waits alone in the lobby of co-op it hosts online
from the menus (the waiting screen's text, and X) */
static boolean coop_menu_waiting_online(
	void)
{
	struct network_game *game = network_game_get_game();

	return global_network_game_server_get() && network_game_server_port_cooperative_menu(NULL) && game &&
		game->machine_count < 2 && config_boolean("network.online") && p2p_running();
}

/* (X in the waiting screen, ui_widget_event_handler_functions.c) a co-op
game hosted online from the menus: listed in the server browser or not,
set as the settings panel sets a row (HALO_NET_COOP_PUBLIC: on the Vita kept
in settings.txt; elsewhere in config.toml, as OpenCE's Server Setup keeps
it); FALSE when it is no such game, or the server browser is off (the key
does what it did) */
boolean coop_menu_toggle_public(
	void)
{
	extern int (*halo_test_setting_hook)(const char *variable, const char *value);
	boolean listed;

	if (!coop_menu_waiting_online() || !config_boolean("network.public_lobby"))
		return FALSE;
	listed = !coop_menu_public(NULL);
	if (!halo_test_setting_hook || !halo_test_setting_hook("HALO_NET_COOP_PUBLIC", listed ? "true" : "false"))
	{
		if (!config_write_boolean("network.coop_public", listed))
			platform_log("co-op: could not write network.coop_public to config.toml");
	}
	p2p_lobby_set_coop_public(listed);
	platform_log("co-op: %s", listed ? "public: listed in the server browser" : "private: joined by its code");
	return TRUE;
}

/* the styles: a button's label, a panel's name, the message bar's text */
static char const coop_menu_label_style[] = "ui\\shell\\main_menu\\=create_new";
static char const coop_menu_name_style[] =
	"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\local_machine_name";
static char const coop_menu_text_style[] =
	"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\choose_teams_text";

void coop_menu_render(
	long screen_tag_index)
{
	char const *name;

	if (screen_tag_index == NONE || !(name = tag_get_name(screen_tag_index)))
		return;
	/* the difficulty screen: the Y button's label beside the button
	(ui_widget.c puts the maps' Y button where the profile screen has its
	Create New) */
	if (!csstrcmp(name, coop_menu_difficulty_screen))
	{
		if (coop_menu_available())
			coop_menu_draw(coop_menu_label_style, 100, 414, 200, L"=PLAY CO-OP");
		return;
	}
	/* the lobby of co-op hosted from the menus, while the partner is not in:
	where the other machines' panels were (the lobby shows the partner's
	alone: ui_widget_game_data_input_functions.c) */
	if (!csstrcmp(name, coop_menu_pregame_screen) && network_game_server_port_cooperative_menu(NULL))
	{
		struct network_game *game = network_game_get_game();
		wchar_t line[64];
		wchar_t machine_name[33];
		short x = 333, width = 244, y = 222;

		if (!game || game->machine_count >= 2)
			return;
		coop_menu_level_name(game->map.name, line, NUMBEROF(line));
		coop_menu_draw(coop_menu_name_style, x, y, width, line);
		y += 24;
		coop_menu_difficulty_name(game->difficulty, line, NUMBEROF(line));
		coop_menu_draw(coop_menu_name_style, x, y, width, line);
		y += 36;
		csmemset(machine_name, 0, sizeof(machine_name));
		network_game_generate_local_machine_name(machine_name);
#ifdef HALO_PORT_VITA_NETWORK
		usnprintf(line, NUMBEROF(line), L"This Vita: %ls", machine_name);
#else
		usnprintf(line, NUMBEROF(line), L"This machine: %ls", machine_name);
#endif
		/* (the name the partner's list shows, when it is not the machine's:
		the settings' lobby name, network_server_manager.c) */
		if (game->name[0] && ustrcmp(game->name, machine_name))
			usnprintf(line, NUMBEROF(line), L"Listed as: %ls", game->name);
		line[NUMBEROF(line) - 1] = 0;
		coop_menu_draw(coop_menu_text_style, x, y, width, line);
		y += 24;
		coop_menu_connection(line, NUMBEROF(line));
		coop_menu_draw(coop_menu_text_style, x, y, width, line);
		/* (online: listed or not, and X to change it) */
		if (coop_menu_waiting_online())
		{
			boolean password;
			boolean listed = coop_menu_public(&password);

			y += 24;
			if (!listed)
				usnprintf(line, NUMBEROF(line), L"Private: joined by its code");
			else
				usnprintf(line, NUMBEROF(line), L"Public: listed%ls", password ? L", password" : L"");
			line[NUMBEROF(line) - 1] = 0;
			coop_menu_draw(coop_menu_text_style, x, y, width, line);
			if (config_boolean("network.public_lobby"))
			{
				y += 24;
				coop_menu_draw(coop_menu_text_style, x, y, width,
					listed ? L"%x-button: make it private" : L"%x-button: make it public");
			}
			/* (the cancel line closer: inside the panel's box) */
			y -= 10;
		}
		y += 36;
		coop_menu_draw(coop_menu_text_style, x, y, width, L"Press %b-button to cancel");
	}
}
