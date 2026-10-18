/*
MENU_FUNCTIONS.C

The functions of OpenCE's multiplayer screens (port/vita/app0/menus, built
by menu_tags.c; ui_widget_event_handler_function_invoke and
ui_widget_game_data_function_invoke call them from PC_MENU_FUNCTION_BASE),
on internet play's server browser and codes (p2p.h, P2P_LOBBY_API 2) and the
settings panel's Play page settings. After OpenCE's menu_functions.c (CC0:
c04765d7 server browser, 6112dcfc passwords, 812ffeea the focused row,
2a6dc2b7 the lock icon, its Server Setup and password screens), written
for the Xbox's network game, which the Vita plays system link games over
internet play with:

- The Multiplayer screen ("port mp ...") stands for the Xbox's: SERVER
  BROWSER, JOIN BY CODE and CREATE GAME need internet play (the settings
  panel's Connection: Online; "port mp require online" fails otherwise, and
  the screen saying so opens); SYSTEM LINK is the Xbox's System Link screen
  (its list of games, where Y creates one), co-op the Campaign screen (Y on
  the difficulty hosts it: coop_menu.c), split screen and gametypes the
  Xbox's own.
- The Server Browser ("port browser ...") lists the public games
  (p2p_lobby_entry: a lock for a password, the name, map, gametype and
  players; the chosen one's Players and Rules lines below); A joins the
  game chosen (a locked one's password asked first, on the password
  screen), X asks the hosts again, Y sorts the list another way (players,
  name, map, gametype), the triggers page through it, B goes back. Every
  game listed is there (up to P2P_LOBBY_MAXIMUM_ENTRIES), its rows scrolling
  past the screen's twelve (browser_list.h). A joined game's host is
  reached as a system link game: the System Link screen then opens (with
  the main menu and Multiplayer behind it), where it is listed.
- JOIN BY CODE ("port code ...") joins a host's code (ABCD-EFGH), then the
  System Link screen opens as for the browser.
- Server Setup ("port setup ...", Create Game > Internet) sets the Play
  page's lobby name, Max players, Visibility (PUBLIC: listed in the server
  browser; PRIVATE: joined by its code) and password, as the panel sets
  them (vita_settings_set: kept in settings.txt), then START GAME opens the
  System Link screen's first step (the profile), after which the game is
  created at once (the System Link list's Y): the map list, the gametype,
  then the game's own lobby, which shows the code.

- With Menu style Halo 3, the Halo 3 multiplayer lobby ("port h3 mp ...",
  halo3/multiplayer.xml) stands for the Multiplayer screen and opens these
  same screens in Halo 3's look, under the same widget names: its NETWORK,
  GAME and MAP pickers, START GAME (ONLINE: Server Setup; SYSTEM LINK: the
  System Link screen's profile, then the new game's map list, as Server
  Setup's START), BROWSE GAMES and JOIN WITH CODE. GAME and MAP are what the
  Xbox's game type and map lists open on (kept as they keep them).

Text is typed on the Vita's system keyboard (system_link_shortcut.h
halo_text_input_*, vita_settings.c); elsewhere from HALO_TEST_TEXT_INPUT,
"|" between the answers ("!" cancels one), for the automated tests.
*/

#include "cseries.h"
#include "interface/ui_widget.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"

#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "saved games/playlist_profile.h"
#include "saved games/saved_game_files.h"
#include "tag_files/files.h"
#include "halo_menus.h"
#include "../src/lang.h"
#include "../src/p2p.h"
#include "../src/crossplay_guard.h"
#include "network_wire.h"
#include "browser_list.h"
#include "../src/system_link_shortcut.h"
#include "forge.h"
#include "../src/port_config.h"
#include "camera/observer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the platform layer's */
void platform_log(char const *format, ...);
int setenv(const char *name, const char *value, int overwrite);
unsigned long system_milliseconds(void);
void platform_show_message(char const *title, char const *message);
void platform_ask_question(char const *title, char const *text);
int platform_question_answer(void);

/* the game's (port) */
boolean ui_widget_port_start_network_game_server(void);
boolean ui_widget_port_cooperative_campaign_host_level(struct widget_instance *widget, struct event_record *event,
	char const *level, short difficulty);
void ui_widget_port_give_focus(struct widget_instance *list, struct widget_instance *child);
boolean system_link_shortcut_open(void);
struct widget_instance *ui_widget_port_active_screen(short local_player_index);
extern int (*halo_test_setting_hook)(const char *variable, const char *value);
extern volatile unsigned long halo_settings_generation;

boolean pc_menu_event_function_invoke(struct widget_instance *widget, struct event_record *event,
	long function_index, boolean *widget_deleted);
void pc_menu_game_data_function_invoke(struct widget_instance *widget, long function);
long pc_menu_functions_screen(long tag_index);
void pc_menu_functions_update(boolean main_menu_loaded);

/* ---------- constants */

#define UI_WIDGET_DEFINITION_TAG 'DeLa'
#define BROWSER_ROWS 12
#define LOBBY_NAME_LENGTH 15
#define PASSWORD_LENGTH (P2P_LOBBY_PASSWORD_SIZE - 1)

static char const server_list_screen[] =
	"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\server_list\\server_list_screen";
static char const map_select_screen[] =
	"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\connected_map_select_wrapper";
static char const password_screen_name[] = "pc\\join\\password\\screen";

/* in port_function_names' order (menu_tags.c) */
enum
{
	_function_unwired,
	_function_mp_screen_init,
	_function_mp_require_online,
	_function_mp_require_lan,
	_function_browser_init,
	_function_browser_dispose,
	_function_browser_refresh,
	_function_browser_join,
	_function_password_init,
	_function_password_edit,
	_function_password_join,
	_function_password_back,
	_function_setup_init,
	_function_setup_edit_name,
	_function_setup_edit_password,
	_function_setup_start,
	_function_setup_back,
	_function_code_init,
	_function_code_edit,
	_function_code_join,
	_function_code_back,
	_function_coop_campaign,
	_function_browser_sort,
	_function_browser_page_up,
	_function_browser_page_down,
	_function_h3_servers_on,
	_function_h3_servers_off,
	_function_h3_campaign_init,
	_function_h3_mission_set,
	_function_h3_difficulty_set,
	_function_h3_campaign_start,
	_function_h3_mission_focus,
	_function_h3_difficulty_focus,
	_function_forge_lobby_init,
	_function_forge_map_set,
	_function_forge_map_focus,
	_function_forge_start,
	_function_forge_browser_init,
	_function_forge_browser_choose,
	_function_forge_browser_back,
	_function_forge_browser_previous,
	_function_forge_browser_next,
	_function_forge_toggle,
	_function_forge_quit,
	_function_h3_mp_init,
	_function_h3_network_set,
	_function_h3_network_focus,
	_function_h3_host_system_link,
	_function_h3_browse_system_link,
	_function_h3_map_set,
	_function_h3_map_focus,
	_function_h3_gametype_set,
	_function_h3_gametype_focus,
	_function_h3_gametype_page_up,
	_function_h3_gametype_page_down,
	_function_h3_setup_hold,
	_function_h3_campaign_network_set,
	_function_h3_campaign_network_focus,
	_function_h3_campaign_host,
	_function_h3_campaign_focus,
	_function_h3_scene,
	_function_settings_panel,
};

/* in port_game_data_input_names' order */
enum
{
	_input_unwired,
	_input_mp_update_desc,
	_input_browser_update,
	_input_password_update,
	_input_setup_update,
	_input_code_update,
	_input_h3_nameplate,
	_input_h3_servers,
	_input_h3_campaign,
	_input_h3_missions,
	_input_h3_difficulties,
	_input_h3_mission_title,
	_input_forge_lobby,
	_input_forge_maps,
	_input_forge_browser,
	_input_forge_title,
	_input_forge_controls,
	_input_h3_mp_lobby,
	_input_h3_networks,
	_input_h3_maps,
	_input_h3_gametypes,
	_input_h3_campaign_networks,
};

/* what a line of typing is for */
enum
{
	_typing_none,
	_typing_password,
	_typing_lobby_name,
	_typing_lobby_password,
	_typing_code,
};

/* the password and code screens' help lines (their strings' order) */
enum
{
	_help_none,
	_help_ask,
	_help_wrong,
	_help_gone,
	_help_joining,
	_help_not_a_code,
};

/* ---------- structures */

/* a widget, as ui_widget.c has it */
enum
{
	_ui_widget_type_container,
	_ui_widget_type_text_box,
	_ui_widget_type_spinner_list,
	_ui_widget_type_column_list,
};

struct widget_animation_data
{
	short current_frame_index;
	short first_frame_index;
	short last_frame_index;
	short number_of_sprite_frames;
};

struct widget_instance
{
	long definition_tag_index;
	char const *name;
	short local_player_index;
	short horizontal_offset;
	short vertical_offset;
	short type;
	boolean visible;
	boolean render_regardless_of_controller_index;
	boolean disabled;
	boolean pause_game_time;
	boolean delete_recursion_lock;
	boolean widget_is_error_dialog;
	boolean close_if_local_player_controller_present;
	byte pad17;
	long creation_time;
	unsigned long milliseconds_to_auto_close;
	unsigned long auto_close_fade_time;
	real alpha_modifier;
	struct widget_instance *previous;
	struct widget_instance *next;
	struct widget_instance *parent;
	struct widget_instance *child;
	struct widget_instance *focused_child;
	union
	{
		struct
		{
			wchar_t *text;
			short string_list_index;
		} text_box;
		struct
		{
			short selected_index;
			short last_list_tab_direction;
			void *list_items;
			word number_of_items;
			struct widget_instance *extended_description;
			wchar_t *item_text;
		} list;
	} parameters;
	struct widget_animation_data animation;
};

typedef char verify_widget_instance_size[sizeof(struct widget_instance) == 0x58 ? 1 : -1];

/* ---------- globals */

static struct
{
	/* every game listed, sorted as the player chose (Y: browser_list.h),
	and the rows showing some of them (entries, entry_count: the rows
	filled), which scroll (view; page: a trigger's page asked for); the game
	chosen (its id stays its own as the list changes) */
	struct p2p_lobby_entry games[P2P_LOBBY_MAXIMUM_ENTRIES];
	long game_count;
	short sort;
	short page;
	struct browser_view view;
	struct p2p_lobby_entry entries[BROWSER_ROWS];
	long entry_count;
	struct p2p_lobby_entry chosen;
	boolean chosen_valid;
	/* a join of a public game under way: watched until its invite is
	handed on (the System Link screen then opens) */
	boolean join_watched;
	char status[96];
	/* the browser's message as last logged */
	char message_logged[128];
	/* the password screen's */
	char password[P2P_LOBBY_PASSWORD_SIZE];
	short password_help;
	/* the code screen's */
	char code[16];
	short code_help;
	/* Server Setup's, as shown */
	char lobby_name[LOBBY_NAME_LENGTH + 1];
	char lobby_password[P2P_LOBBY_PASSWORD_SIZE];
	/* the typing asked for */
	short typing;
	/* Server Setup's START: the System Link list, once the profile is
	chosen, gives way to a new game's map list; create_system_link: the
	Halo 3 lobby's START GAME on SYSTEM LINK asked it, not Create Game */
	boolean create_pending;
	boolean create_system_link;
	/* (the Halo 3 Create Game: its GAME TYPE or MAP picker opened, port h3
	setup hold) what the screen showed, which it shows again when it is back
	(setup_initialize) rather than the settings' */
	boolean setup_held;
	short held_maximum;
	boolean held_private;
	/* to be opened from the main loop: the System Link screen, the password
	screen */
	boolean open_system_link;
	boolean open_password;
	/* (crossplay_guard.h) a large PC game chosen: the player asked whether
	to join it anyway, the answer awaited */
	boolean large_asked;
	struct p2p_lobby_entry large_entry;
} menu_functions;

/* ---------- private code */

static struct widget_instance *named(struct widget_instance *widget, char const *name)
{
	struct widget_instance *child;

	if (!widget)
		return NULL;
	if (widget->name && !strcmp(widget->name, name))
		return widget;
	for (child = widget->child; child; child = child->next)
	{
		struct widget_instance *found = named(child, name);

		if (found)
			return found;
	}
	return NULL;
}

static struct widget_instance *screen_of(struct widget_instance *widget)
{
	while (widget && widget->parent)
		widget = widget->parent;
	return widget;
}

/* a text box's own text (one with no string list shows it): UTF-8, as the
port's text is (a translation's, lang.h, and the game list's), made the
game's UTF-16 (halo_menus_utf16); at most 480 characters (Forge's controls
are the longest) */
static void text_show(struct widget_instance *widget, char const *text)
{
	unsigned short characters[481];
	long length;

	if (!widget || widget->type != _ui_widget_type_text_box)
		return;
	length = halo_menus_utf16(text ? text : "", characters, (long)NUMBEROF(characters));
	widget->parameters.text_box.text = ui_widget_realloc(widget->parameters.text_box.text,
		(word)(length * sizeof(wchar_t)), __FILE__, __LINE__);
	if (!widget->parameters.text_box.text)
		return;
	memcpy(widget->parameters.text_box.text, characters, length * sizeof(wchar_t));
}

/* (the Halo 3 menus' nameplate, port h3 nameplate) the first local player's
active profile's name; with none yet (a first start), the player's name
(network.player_name: on the Vita its user name), else "Player". Not the
game's "get active plyr profile name", which asserts without a profile */
static void h3_nameplate_update(struct widget_instance *widget)
{
	long profile_index = player_ui_get_active_player_profile_index(0);
	struct player_profile profile;
	char name[64];
	int length = 0, index;

	name[0] = 0;
	if (profile_index != NONE && player_profile_get(profile_index, &profile))
	{
		for (index = 0; index < MAXIMUM_PLAYER_PROFILE_NAME_LENGTH && profile.player_name[index] &&
			length < (int)sizeof(name) - 4; index++)
		{
			unsigned long code = (unsigned short)profile.player_name[index];

			if (code < 0x80)
				name[length++] = (char)code;
			else if (code < 0x800)
			{
				name[length++] = (char)(0xC0 | (code >> 6));
				name[length++] = (char)(0x80 | (code & 0x3F));
			}
			else
			{
				name[length++] = (char)(0xE0 | (code >> 12));
				name[length++] = (char)(0x80 | ((code >> 6) & 0x3F));
				name[length++] = (char)(0x80 | (code & 0x3F));
			}
		}
		name[length] = 0;
	}
	if (!name[0])
		snprintf(name, sizeof(name), "%s", getenv("HALO_NET_PLAYER_NAME") && getenv("HALO_NET_PLAYER_NAME")[0] ?
			getenv("HALO_NET_PLAYER_NAME") : "Player");
	text_show(widget, name);
}

/* (the Halo 3 main menu's live servers, port h3 servers) up to three rows
(server_1 to server_3) of the listed games with the most players, "name -
map" and "players/max" (server_N_name, server_N_count); the rows and the ONLINE NOW label (servers_label) hidden
when there are none (internet play off, no games). Display only: ONLINE
opens the browser to join */
static void browser_test_games(void);

static void h3_servers_update(struct widget_instance *widget)
{
	static char said[256];
	struct widget_instance *screen = screen_of(widget);
	char line[256];
	long count, index, shown;
	int length = 0;

	count = p2p_lobby_entries(menu_functions.games, P2P_LOBBY_MAXIMUM_ENTRIES);
	menu_functions.game_count = count;
	browser_test_games();
	count = menu_functions.game_count;
	browser_list_sort(menu_functions.games, (int)count, _browser_sort_players);
	shown = 0;
	for (index = 0; index < 3; index++)
	{
		char name[24], text[P2P_LOBBY_NAME_SIZE + P2P_LOBBY_MAP_SIZE + 32];
		struct widget_instance *row;

		snprintf(name, sizeof(name), "server_%ld", index + 1);
		row = named(screen, name);
		if (index < count)
		{
			struct p2p_lobby_entry const *entry = &menu_functions.games[index];

			/* (Halo 3's nameplate: the name and map at the left, the players at the right) */
			snprintf(text, sizeof(text), "%s - %s", entry->name, entry->map);
			snprintf(name, sizeof(name), "server_%ld_name", index + 1);
			text_show(named(screen, name), text);
			snprintf(text, sizeof(text), "%d/%d", entry->players, entry->maximum);
			snprintf(name, sizeof(name), "server_%ld_count", index + 1);
			text_show(named(screen, name), text);
			if (row)
				widget_instance_set_visibility_recursive(row, TRUE);
			if (length < (int)sizeof(line) - 1)
				length += snprintf(line + length, sizeof(line) - (size_t)length, "%s%s (%d/%d)", shown ? ", " : "",
					entry->name, entry->players, entry->maximum);
			shown++;
		}
		else if (row)
			widget_instance_set_visibility_recursive(row, FALSE);
	}
	if (named(screen, "servers_label"))
		widget_instance_set_visibility_recursive(named(screen, "servers_label"), shown > 0);
	line[length] = 0;
	{
		char summary[256];

		snprintf(summary, sizeof(summary), "%ld of %ld: %s", shown, count, line);
		if (strcmp(summary, said))
		{
			snprintf(said, sizeof(said), "%s", summary);
			platform_log("menus: h3 servers: %ld shown of %ld%s%s", shown, count, shown ? ": " : "", line);
		}
	}
}

/* ---- the Halo 3 campaign lobby (halo3/campaign.xml): the mission and the
difficulty chosen there, then the game's own start (main menu switch to solo
game), as its level and difficulty lists do */

void main_set_map_name(char *map_name);
void main_defer_map_map_change(void);
void main_set_difficulty(word difficulty);
boolean game_state_test_persistent_storage(char *map_name, short *difficulty, boolean *corrupted);
char *main_get_map_name(void);
short game_difficulty_level_get(void);
static void string_show(struct widget_instance *widget, short index);

static char const *const h3_level_paths[NUMBER_OF_SINGLE_PLAYER_LEVELS] =
{
	"levels\\a10\\a10", "levels\\a30\\a30", "levels\\a50\\a50", "levels\\b30\\b30", "levels\\b40\\b40",
	"levels\\c10\\c10", "levels\\c20\\c20", "levels\\c40\\c40", "levels\\d20\\d20", "levels\\d40\\d40",
};
static char const *const h3_level_names[NUMBER_OF_SINGLE_PLAYER_LEVELS] =
{
	"The Pillar of Autumn", "Halo", "The Truth and Reconciliation", "The Silent Cartographer",
	"Assault on the Control Room", "343 Guilty Spark", "The Library", "Two Betrayals", "Keyes", "The Maw",
};
static char const *const h3_difficulty_names[4] = { "Easy", "Normal", "Heroic", "Legendary" };

/* the lobby's NETWORK: LOCAL (this Vita alone: the level started as the
Xbox's difficulty list's A starts it), or co-op hosted as the old Campaign
screen's Y on the difficulty hosts it (coop_menu.c): ONLINE CO-OP over
internet play (the settings panel's Connection: Online; its code shown in
the waiting screen), SYSTEM LINK CO-OP (or AD HOC CO-OP, the connection Ad
hoc) for the Vitas on the same Wi-Fi or near. The port hosts a network game
one way, over the connection the Vita runs: the two co-op choices differ in
what they ask of it, ONLINE needing internet play. */
enum
{
	_h3_campaign_local,
	_h3_campaign_online,
	_h3_campaign_system_link,
	NUMBER_OF_H3_CAMPAIGN_NETWORKS
};

static boolean internet_play(void);

static struct
{
	boolean initialized;
	short level;
	short difficulty;
	short network;
	/* the profile's saved game (its checkpoint): there, its level and
	difficulty, read as the lobby opens (as the Xbox's level list does) */
	boolean saved;
	char saved_map[256];
	short saved_difficulty;
} h3_campaign;

/* the first local player's active profile, FALSE with none */
static boolean h3_profile(struct player_profile *profile)
{
	long profile_index = player_ui_get_active_player_profile_index(0);

	return profile_index != NONE && player_profile_get(profile_index, profile);
}

/* how many levels the lobby offers: those the profile finished on any
difficulty, and the one after (all ten once the last is), at least the first */
static short h3_campaign_unlocked(void)
{
	struct player_profile profile;
	short level = NONE, difficulty;

	if (h3_profile(&profile))
		player_profile_get_highest_completed_solo_level(&profile, &level, &difficulty);
	return (short)PIN(level + 2, 1, NUMBER_OF_SINGLE_PLAYER_LEVELS);
}

static void h3_campaign_said(void)
{
	platform_log("menus: h3 campaign: %s on %s", h3_level_names[h3_campaign.level],
		h3_difficulty_names[h3_campaign.difficulty]);
}

static char const *h3_campaign_network_name(short network)
{
	if (network == _h3_campaign_online)
		return T("ONLINE CO-OP");
	if (network == _h3_campaign_system_link)
		return config_boolean("network.adhoc") ? T("AD HOC CO-OP") : T("SYSTEM LINK CO-OP");
	return T("LOCAL");
}

/* (port h3 campaign network set) the network picker's choice; ONLINE CO-OP
only with internet play (else FALSE: the screen saying how to turn it on
opens, as the multiplayer lobby's ONLINE does) */
static boolean h3_campaign_network_set(struct widget_instance *widget);

/* (the settings panel's Host co-op campaign, system_link_shortcut.c: the
Campaign screen opened for co-op) the lobby opens on co-op, online with
internet play */
void menu_functions_h3_campaign_coop(void)
{
	h3_campaign.network = internet_play() ? _h3_campaign_online : _h3_campaign_system_link;
	platform_log("menus: h3 campaign: network %s (host co-op campaign)", h3_campaign_network_name(h3_campaign.network));
}

static void h3_capitals(char *text)
{
	for (; *text; text++)
	{
		if (*text >= 'a' && *text <= 'z')
			*text = (char)(*text - 'a' + 'A');
	}
}

/* (the Halo 3 pause screen's title, port h3 mission title) the level played
and its difficulty, "THE PILLAR OF AUTUMN · HEROIC"; a level not of the
campaign, its map's name */
static void h3_mission_title_update(struct widget_instance *widget)
{
	char const *map = main_get_map_name(), *name = NULL, *slash;
	short difficulty = PIN(game_difficulty_level_get(), 0, 3);
	char text[96];
	short level;

	for (level = 0; level < NUMBER_OF_SINGLE_PLAYER_LEVELS && map; level++)
		if (!_stricmp(map, h3_level_paths[level]))
			name = T(h3_level_names[level]);
	if (!name)
	{
		slash = map ? strrchr(map, '\\') : NULL;
		name = slash ? slash + 1 : map ? map : "";
	}
	snprintf(text, sizeof(text), "%s \xc2\xb7 %s", name, T(h3_difficulty_names[difficulty]));
	h3_capitals(text);
	text_show(widget, text);
}

/* (the lobby opened) the level last played, on Normal the first time; what
was chosen since, after */
static void h3_campaign_initialize(void)
{
	struct player_profile profile;
	short unlocked = h3_campaign_unlocked();

	if (!h3_campaign.initialized)
	{
		h3_campaign.initialized = TRUE;
		h3_campaign.difficulty = 1;
		h3_campaign.level = 0;
		if (h3_profile(&profile))
			h3_campaign.level = profile.last_single_player_map_played;
	}
	h3_campaign.level = (short)PIN(h3_campaign.level, 0, unlocked - 1);
	/* (internet play turned off since ONLINE CO-OP was chosen: co-op over
	the connection the Vita has now) */
	if (h3_campaign.network == _h3_campaign_online && !internet_play())
		h3_campaign.network = _h3_campaign_system_link;
	{
		boolean corrupted = FALSE;

		h3_campaign.saved_map[0] = 0;
		h3_campaign.saved = game_state_test_persistent_storage(h3_campaign.saved_map, &h3_campaign.saved_difficulty,
			&corrupted) && !corrupted;
		if (h3_campaign.saved)
			platform_log("menus: h3 campaign: the saved game is %s on %s", h3_campaign.saved_map,
				h3_difficulty_names[PIN(h3_campaign.saved_difficulty, 0, 3)]);
	}
	h3_campaign_said();
}

/* a widget's place among its parent's children (a list item's), 0 for none */
static long h3_child_index(struct widget_instance *widget)
{
	struct widget_instance *child;
	long index = 0;

	if (!widget || !widget->parent)
		return 0;
	for (child = widget->parent->child; child && child != widget; child = child->next)
		index++;
	return child ? index : 0;
}

/* a list's child of that place given the focus (a picker's item) */
static void h3_focus_child(struct widget_instance *list, long index)
{
	struct widget_instance *child = list ? list->child : NULL;

	while (child && index-- > 0)
		child = child->next;
	if (child)
		ui_widget_port_give_focus(list, child);
}

/* the lobby's rows (mission_row, difficulty_row), its picture (picture: the
level's frame of the game's own level pictures), the difficulty emblem
(emblem) and the caption */
static void h3_campaign_update(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	struct widget_instance *found;
	char text[160];

	if (!h3_campaign.initialized)
		h3_campaign_initialize();
	/* (Halo 3's rows are capitals: the letters a to z made capitals, the
	others as they are) */
	snprintf(text, sizeof(text), "%s: %s", T("NETWORK"), h3_campaign_network_name(h3_campaign.network));
	h3_capitals(text);
	text_show(named(screen, "network_row"), text);
	/* (START GAME: the level here, or co-op hosted: the row of the network
	shown, the other hidden, as the multiplayer lobby's) */
	if ((found = named(screen, "start_row")) != NULL)
	{
		widget_instance_set_visibility_recursive(found, h3_campaign.network == _h3_campaign_local);
		found->disabled = h3_campaign.network != _h3_campaign_local;
	}
	if ((found = named(screen, "start_coop")) != NULL)
	{
		widget_instance_set_visibility_recursive(found, h3_campaign.network != _h3_campaign_local);
		found->disabled = h3_campaign.network == _h3_campaign_local;
	}
	if (h3_campaign.network == _h3_campaign_online)
		snprintf(text, sizeof(text), "%s", T("Co-op online: joined by your code."));
	else if (h3_campaign.network == _h3_campaign_system_link)
		snprintf(text, sizeof(text), "%s", config_boolean("network.adhoc") ?
			T("Co-op with the PS Vitas near you.") : T("Co-op with the PS Vitas on your Wi-Fi."));
	else
		snprintf(text, sizeof(text), "%s", T("This party is local to this PS Vita."));
	text_show(named(screen, "ready_sub"), text);
	snprintf(text, sizeof(text), "%s: %s", T("MISSION"), T(h3_level_names[h3_campaign.level]));
	h3_capitals(text);
	text_show(named(screen, "mission_row"), text);
	snprintf(text, sizeof(text), "%s: %s", T("DIFFICULTY"), T(h3_difficulty_names[h3_campaign.difficulty]));
	h3_capitals(text);
	text_show(named(screen, "difficulty_row"), text);
	/* (as the Xbox's: the saved game's level on its difficulty goes on from
	its checkpoint; on another difficulty, starting replaces it - the
	warning; another level starts at its start) */
	{
		/* (co-op starts the level at its start, the saved game left as it is) */
		boolean coop = h3_campaign.network != _h3_campaign_local;
		boolean same_level = !coop && h3_campaign.saved &&
			!_stricmp(h3_campaign.saved_map, h3_level_paths[h3_campaign.level]);
		boolean resumes = same_level && h3_campaign.saved_difficulty == h3_campaign.difficulty;
		struct widget_instance *warning = named(screen, "warning");

		snprintf(text, sizeof(text), "%s %s %s\n%s", T(h3_level_names[h3_campaign.level]), T("on"),
			T(h3_difficulty_names[h3_campaign.difficulty]), coop ? T("Co-op at Mission Start") :
			resumes ? T("at Last Checkpoint") : T("at Mission Start"));
		text_show(named(screen, "caption"), text);
		/* (said when it changes: the tests read it) */
		{
			static char said[192];
			char line[192];

			char *newline;

			snprintf(line, sizeof(line), "%s%s", text, same_level && !resumes ? " (replaces the saved game)" : "");
			while ((newline = strchr(line, '\n')) != NULL)
				*newline = ' ';
			if (strcmp(line, said))
			{
				snprintf(said, sizeof(said), "%s", line);
				platform_log("menus: h3 campaign caption: %s", line);
			}
		}
		if (warning)
		{
			widget_instance_set_visibility_recursive(warning, same_level && !resumes);
			text_show(warning, T("This difficulty starts the mission over\nand replaces your saved game."));
		}
	}
	if ((found = named(screen, "picture")) != NULL)
		found->animation.current_frame_index = h3_campaign.level;
	if ((found = named(screen, "emblem")) != NULL)
		found->animation.current_frame_index = h3_campaign.difficulty;
}

/* a picker's list (the missions, the difficulties): the first count items
shown (the levels not open yet hidden), the picture's frame and the
description's string following the focused one */
static void h3_pick_update(struct widget_instance *list, long count, char const *picture, char const *description)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *found;
	long index = 0, focused = 0;

	for (child = list->child; child; child = child->next, index++)
	{
		boolean shown = index < count;

		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		if (child == list->focused_child)
			focused = index;
	}
	if ((found = named(screen, picture)) != NULL)
		found->animation.current_frame_index = (short)focused;
	string_show(named(screen, description), (short)focused);
}

static boolean h3_campaign_network_set(struct widget_instance *widget)
{
	short network = (short)PIN(h3_child_index(widget), 0, NUMBER_OF_H3_CAMPAIGN_NETWORKS - 1);

	if (network == _h3_campaign_online && !internet_play())
	{
		platform_log("menus: h3 campaign: ONLINE CO-OP chosen, internet play is off");
		return FALSE;
	}
	h3_campaign.network = network;
	platform_log("menus: h3 campaign: network %s", h3_campaign_network_name(network));
	return TRUE;
}

/* (port h3 campaign host: START GAME on a co-op network) the mission hosted
on the difficulty as co-op, as the Xbox's difficulty list's Y hosts it
(ui_widget_port_cooperative_campaign_host_level: the network game, this
player in it under the profile); the row then opens the game's lobby, the
waiting screen, with this lobby behind it. FALSE when it was not hosted
(the player told why by the game's own checks) */
static boolean h3_campaign_host(struct widget_instance *widget, struct event_record *event)
{
	char const *level = h3_level_paths[h3_campaign.level];

	if (h3_campaign.network == _h3_campaign_local)
		return FALSE;
	platform_log("menus: h3 campaign: host co-op %s on %s (%s)", level, h3_difficulty_names[h3_campaign.difficulty],
		h3_campaign.network == _h3_campaign_online ? "online" : "system link");
	/* (the level chosen as the Xbox's level list chooses it, before its
	difficulty list: its map change not deferred, which would load it - and
	end the network game - at the next frame) */
	main_set_map_name((char *)level);
	main_defer_map_map_change();
	if (!ui_widget_port_cooperative_campaign_host_level(widget, event, level, h3_campaign.difficulty))
	{
		platform_log("menus: h3 campaign: co-op was not hosted");
		return FALSE;
	}
	return TRUE;
}

/* ---- the Halo 3 menus' scenes: a camera of its own behind a lobby, as
Halo 3's menus have. ui.map's scenario (levels\ui\ui) has fifteen cutscene
camera points; its one script, the continuous ring_loop, flies eleven of
them (uicam, point_2 to point_11: camera_set each over 400 ticks, sleep
200), and four more, named for the Xbox's screens (load_campaign, settings,
multiplayer, new_campaign), are used by nothing. A screen with a scene=
(menus/halo3: the campaign, multiplayer and Forge lobbies) runs "port h3
scene" as it opens: its camera ("x y z yaw pitch": world units and
degrees, yaw 0 along +x, pitch up positive, as @camera) is written into
the point it names, ring_loop put to sleep and the camera cut there
(camera_set over 0 ticks unless the scene says: gliding there swung the
camera across the whole scene, the owner's "pans around like crazy"); the
main menu's scene="ring" cuts back to where the ring's flight was left (kept
in the settings point, which no screen uses) and wakes ring_loop, which
flies on from there. The screens opened from a lobby
(its pickers) have none: the camera stays. The point is written and the
script commands run at the top of the main loop, between frames
(menu_functions_scenes_update, main.c; the commands as the settings
panel's host commands, main_request_host_command_from), never inside the
widgets' update. Only with Menu style Halo 3, at the main menu of a local
game, unless menus.scenes (HALO_MENU_SCENES=0) is false. (After the spike
on spike/menu-scenes, Oct 18 2026.) */

boolean main_request_host_command_from(char const *command, char const *source);
int halo_main_menu_idle(void);

#define H3_SCENE_TICKS 0

static struct
{
	/* the scene of the screen opened (port h3 scene), for the top of the
	main loop: pending set last (release), taken there (acquire) */
	int pending;
	boolean ring;
	char point[32];
	boolean has_camera;
	real camera[5];
	long ticks;
	char screen[64];
	/* the characters standing in it (scene_object=: menu_scene_objects.c) */
	char objects[160];
	boolean has_objects;
	/* ring_loop put to sleep by a scene (ui.map's scripts start awake) */
	boolean ring_asleep;
	/* where the ring's flight was left, in the settings point */
	boolean ring_kept;
} h3_scenes;

static void h3_scene_keep_ring(void);

static boolean h3_scenes_enabled(void)
{
	char const *setting = getenv("HALO_MENU_SCENES");

	if (setting && *setting)
		return strcmp(setting, "0") && _stricmp(setting, "false");
	return config_boolean("menus.scenes");
}

/* the scene= of the menus' widget this screen was built from (its tag is
"pc\<name>", the name's '/' made '\'), NULL for none */
static char const *h3_scene_of(struct widget_instance *screen, char *name, int size, char const **objects)
{
	struct halo_menus const *menus = halo_menus_load();
	char const *tag_name = screen ? tag_get_name(screen->definition_tag_index) : NULL;
	long index;

	if (!menus || !tag_name || strncmp(tag_name, "pc\\", 3))
		return NULL;
	tag_name += 3;
	for (index = 0; index < menus->widget_count; index++)
	{
		struct halo_menu_widget const *widget = &menus->widgets[index];
		char const *a = widget->name, *b = tag_name;

		if (!widget->scene || !a)
			continue;
		while (*a && *b && (*a == *b || (*a == '/' && *b == '\\')))
			a++, b++;
		if (!*a && !*b)
		{
			snprintf(name, size, "%s", widget->name);
			if (objects)
				*objects = widget->scene_object;
			return widget->scene;
		}
	}
	return NULL;
}

/* scene="<point> [x y z yaw pitch [ticks]]": the point's name (letters,
digits, '_'), and the camera written into it, in range (finite, the
world's +-5000), if given */
static boolean h3_scene_parse(char const *scene)
{
	char word[32];
	float values[6];
	int used = 0, count;
	short index;

	h3_scenes.has_camera = FALSE;
	h3_scenes.ticks = H3_SCENE_TICKS;
	if (sscanf(scene, "%31s%n", word, &used) != 1 ||
		strspn(word, "abcdefghijklmnopqrstuvwxyz0123456789_") != strlen(word))
	{
		return FALSE;
	}
	snprintf(h3_scenes.point, sizeof(h3_scenes.point), "%s", word);
	h3_scenes.ring = !strcmp(word, "ring");
	count = sscanf(scene + used, "%f %f %f %f %f %f", &values[0], &values[1], &values[2], &values[3], &values[4],
		&values[5]);
	if (count <= 0)
		return TRUE;
	if (count < 5 || h3_scenes.ring)
		return FALSE;
	for (index = 0; index < 5; index++)
	{
		if (!(values[index] > -5000.f && values[index] < 5000.f))
			return FALSE;
		h3_scenes.camera[index] = values[index];
	}
	h3_scenes.has_camera = TRUE;
	if (count == 6)
		h3_scenes.ticks = PIN((long)values[5], 0, 600);
	return TRUE;
}

/* (port h3 scene, a screen's "created") the screen's scene asked for */
static void h3_scene_request(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	char name[64];
	char const *scene;

	if (!h3_scenes_enabled() || !halo_menu_style_halo3() || !halo_main_menu_idle())
		return;
	char const *objects = NULL;

	scene = h3_scene_of(screen, name, sizeof(name), &objects);
	if (!scene)
		return;
	/* (one asked for and not yet taken is replaced: the latest screen's) */
	__atomic_store_n(&h3_scenes.pending, 0, __ATOMIC_RELAXED);
	if (!h3_scene_parse(scene))
	{
		platform_log("menus: h3 scene: %s: scene=\"%s\" is not \"ring\" or \"<camera point> [x y z yaw pitch [ticks]]\"",
			name, scene);
		return;
	}
	snprintf(h3_scenes.screen, sizeof(h3_scenes.screen), "%s", name);
	h3_scenes.has_objects = objects && *objects;
	snprintf(h3_scenes.objects, sizeof(h3_scenes.objects), "%s", h3_scenes.has_objects ? objects : "");
	__atomic_store_n(&h3_scenes.pending, 1, __ATOMIC_RELEASE);
}

static struct scenario_cutscene_camera_point *h3_scene_point(char const *name)
{
	struct scenario *scenario = global_scenario_get();
	short index;

	if (!scenario || !name)
		return NULL;
	for (index = 0; index < scenario->cutscene_camera_points.count; index++)
	{
		struct scenario_cutscene_camera_point *point = TAG_BLOCK_GET_ELEMENT(
			&scenario->cutscene_camera_points, index, struct scenario_cutscene_camera_point);

		if (!strncmp(point->name, name, sizeof(point->name)))
			return point;
	}
	return NULL;
}

/* (main.c, the top of the main loop, between frames) the scene asked for:
its camera written into its point, the script commands queued for the host
commands' turn, which comes next */
void menu_functions_scenes_update(void)
{
	struct scenario_cutscene_camera_point *point;
	char command[96];

	/* (Menu style Halo CE chosen, or the scenes turned off, while a lobby's
	camera holds: the ring's flight back, as no Halo 3 main menu will ask) */
	if (h3_scenes.ring_asleep && (!halo_menu_style_halo3() || !h3_scenes_enabled()) && halo_main_menu_idle())
	{
		main_request_host_command_from("(wake ring_loop)", "menu scene");
		h3_scenes.ring_asleep = FALSE;
		platform_log("menus: h3 scene: the scenes are off now: the ring's own flight");
	}
	if (!__atomic_exchange_n(&h3_scenes.pending, 0, __ATOMIC_ACQUIRE))
		return;
	/* (the main menu gone meanwhile: a level loading, a network game) */
	if (!halo_main_menu_idle())
		return;
	if (h3_scenes.ring)
	{
		if (h3_scenes.ring_asleep)
		{
			if (h3_scenes.ring_kept)
				main_request_host_command_from("(camera_set settings 0)", "menu scene");
			main_request_host_command_from("(wake ring_loop)", "menu scene");
		}
		h3_scenes.ring_asleep = FALSE;
		menu_scene_objects_want(h3_scenes.screen, NULL, FALSE, 0.f, 0.f, 0.f, 0.f, 0.f);
		platform_log("menus: h3 scene: %s: the ring's own flight", h3_scenes.screen);
		return;
	}
	if ((point = h3_scene_point(h3_scenes.point)) == NULL)
	{
		platform_log("menus: h3 scene: %s: ui.map has no camera point %s", h3_scenes.screen, h3_scenes.point);
		return;
	}
	if (h3_scenes.has_camera)
	{
		/* (the screen's own camera in its point: the director reads the
		point as it glides there; no other screen's scene names it) */
		point->position.x = h3_scenes.camera[0];
		point->position.y = h3_scenes.camera[1];
		point->position.z = h3_scenes.camera[2];
		point->orientation.yaw = h3_scenes.camera[3] * (real)(3.14159265358979 / 180.0);
		point->orientation.pitch = h3_scenes.camera[4] * (real)(3.14159265358979 / 180.0);
		point->orientation.roll = 0.f;
	}
	/* (leaving the ring's flight: where it was kept, to come back to) */
	if (!h3_scenes.ring_asleep)
		h3_scene_keep_ring();
	/* (who stands there: menu_scene_objects.c, which makes them before the
	tick) */
	menu_scene_objects_want(h3_scenes.screen, h3_scenes.has_objects ? h3_scenes.objects : NULL, TRUE,
		point->position.x, point->position.y, point->position.z, point->orientation.yaw, point->orientation.pitch);
	main_request_host_command_from("(sleep -1 ring_loop)", "menu scene");
	snprintf(command, sizeof(command), "(camera_set %s %ld)", h3_scenes.point, h3_scenes.ticks);
	main_request_host_command_from(command, "menu scene");
	h3_scenes.ring_asleep = TRUE;
	platform_log("menus: h3 scene: %s: camera point %s at %.2f %.2f %.2f, yaw %.1f pitch %.1f, over %ld ticks",
		h3_scenes.screen, h3_scenes.point, point->position.x, point->position.y, point->position.z,
		point->orientation.yaw * (real)(180.0 / 3.14159265358979),
		point->orientation.pitch * (real)(180.0 / 3.14159265358979), h3_scenes.ticks);
}

/* the camera as the ring's flight left it, in ui.map's settings point (no
screen's scene names it) */
static void h3_scene_keep_ring(void)
{
	struct scenario_cutscene_camera_point *point = h3_scene_point("settings");
	struct observer_result const *camera = observer_get_camera(0);
	real horizontal;

	h3_scenes.ring_kept = FALSE;
	if (!point || !camera)
		return;
	horizontal = (real)sqrt(camera->forward.i * camera->forward.i + camera->forward.j * camera->forward.j);
	point->position = camera->position;
	point->orientation.yaw = (real)atan2(camera->forward.j, camera->forward.i);
	point->orientation.pitch = (real)atan2(camera->forward.k, horizontal);
	point->orientation.roll = 0.f;
	h3_scenes.ring_kept = TRUE;
}

/* ---- Forge (forge.c): its lobby (halo3/forge_lobby.xml: the map, its
picture, START FORGE), and over the game the object browser and the Forge
menu (halo3/forge.xml) */

#define FORGE_BROWSER_ROWS 12

static struct
{
	boolean initialized;
	/* the lobby's map (forge.c's list) */
	short map;
	/* the browser: the categories, or one category's items (category);
	the rows' view and the one chosen (browser_list.h), a trigger's page */
	boolean in_category;
	short category;
	struct browser_view view;
	int chosen;
	int page;
	/* what was last said in the log */
	char said[160];
} h3_forge;

/* the lobby opened: Blood Gulch the first time (Halo 3's first Forge map
is its open one), a map of the maps folder; what was chosen since, after */
static void h3_forge_lobby_initialize(void)
{
	short index;

	if (!h3_forge.initialized)
	{
		h3_forge.initialized = TRUE;
		h3_forge.map = 9;
	}
	for (index = 0; index < forge_map_count() && !forge_map_available(h3_forge.map); index++)
		h3_forge.map = (short)((h3_forge.map + 1) % forge_map_count());
	platform_log("menus: forge lobby: %s", forge_map_name(h3_forge.map));
}

/* the lobby's map row, picture (map_picture: its frame of ui.map's
pictures of the multiplayer maps) and caption, Halo 3's "Edit Objects on
<map>"; the status's second line when there is no map to play */
static void h3_forge_lobby_update(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	struct widget_instance *found;
	boolean available = forge_map_available(h3_forge.map);
	char text[160];

	if (!h3_forge.initialized)
		h3_forge_lobby_initialize();
	snprintf(text, sizeof(text), "%s: %s", T("MAP"), T(forge_map_name(h3_forge.map)));
	h3_capitals(text);
	text_show(named(screen, "map_row"), text);
	snprintf(text, sizeof(text), T("Edit Objects on %s"), T(forge_map_name(h3_forge.map)));
	text_show(named(screen, "caption"), available ? text : T("No multiplayer map is in the maps folder."));
	if (strcmp(text, h3_forge.said))
	{
		snprintf(h3_forge.said, sizeof(h3_forge.said), "%s", text);
		platform_log("menus: forge lobby caption: %s", text);
	}
	if ((found = named(screen, "map_picture")) != NULL)
		found->animation.current_frame_index = h3_forge.map;
}

/* the map picker: the maps of the maps folder shown, each its name, the
picture and the description following the focused one */
static void h3_forge_maps_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *found;
	short index = 0, focused = 0;

	for (child = list->child; child; child = child->next, index++)
	{
		boolean shown = forge_map_available(index);
		char text[96];

		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		snprintf(text, sizeof(text), "%s", T(forge_map_name(index)));
		h3_capitals(text);
		text_show(child, text);
		if (child == list->focused_child)
			focused = index;
	}
	if ((found = named(screen, "maps_picture")) != NULL)
		found->animation.current_frame_index = focused;
	string_show(named(screen, "maps_text"), focused);
}

/* the categories the loaded map has objects of, in the browser's order */
static short h3_forge_categories(short categories[NUMBER_OF_FORGE_CATEGORIES])
{
	short category, count = 0;

	for (category = 0; category < NUMBER_OF_FORGE_CATEGORIES; category++)
	{
		if (forge_category_item_count(category) > 0)
			categories[count++] = category;
	}
	return count;
}

/* the browser's rows (row_0 to row_11: row_name, row_count) on the
categories or a category's items, scrolling past twelve; its crumb, its
budget and its pane (info_name, info_text) on the one chosen */
static void h3_forge_browser_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focus = NULL;
	short categories[NUMBER_OF_FORGE_CATEGORIES];
	short category_count = h3_forge_categories(categories);
	short objects, ai;
	long count, index, focused_row = NONE;
	int target;
	char text[192], line[160];

	if (h3_forge.in_category && forge_category_item_count(h3_forge.category) <= 0)
	{
		h3_forge.in_category = FALSE;
		browser_view_reset(&h3_forge.view);
		h3_forge.chosen = 0;
	}
	count = h3_forge.in_category ? forge_category_item_count(h3_forge.category) : category_count;
	for (index = 0, child = list->child; child; child = child->next)
	{
		if (strncmp(child->name, "row_", 4))
			continue;
		if (child == list->focused_child)
			focused_row = index;
		index++;
	}
	target = browser_view_follow(&h3_forge.view, FORGE_BROWSER_ROWS, (int)count, (int)focused_row, h3_forge.chosen,
		h3_forge.page);
	h3_forge.page = 0;
	h3_forge.chosen = target;
	for (index = 0, child = list->child; child; child = child->next)
	{
		long shown = h3_forge.view.offset + index;

		if (strncmp(child->name, "row_", 4))
			continue;
		if (index >= h3_forge.view.visible)
		{
			widget_instance_set_visibility_recursive(child, FALSE);
			child->disabled = TRUE;
			index++;
			continue;
		}
		widget_instance_set_visibility_recursive(child, TRUE);
		child->disabled = FALSE;
		line[0] = text[0] = 0;
		if (h3_forge.in_category)
		{
			struct forge_item item;

			if (forge_category_item(h3_forge.category, (short)shown, &item))
			{
				snprintf(text, sizeof(text), "%s", item.name);
				if (item.placed > 0)
					snprintf(line, sizeof(line), T("%d placed"), item.placed);
			}
		}
		else
		{
			snprintf(text, sizeof(text), "%s", forge_category_name(categories[shown]));
			snprintf(line, sizeof(line), "%d", forge_category_item_count(categories[shown]));
		}
		h3_capitals(text);
		text_show(named(child, "row_name"), text);
		text_show(named(child, "row_count"), line);
		if (index == h3_forge.view.row)
			focus = child;
		index++;
	}
	if (focus && focus != list->focused_child)
		ui_widget_port_give_focus(list, focus);
	for (child = list->child; child; child = child->next)
	{
		if (!strncmp(child->name, "row_", 4))
			child->animation.current_frame_index = child == focus ? 1 : 0;
	}
	/* (the crumb, the budget, the pane) */
	forge_budget_get(&objects, &ai);
	snprintf(text, sizeof(text), "%d / %d", objects, FORGE_MAXIMUM_OBJECTS);
	text_show(named(screen, "budget"), text);
	if (h3_forge.in_category)
		snprintf(text, sizeof(text), "%s \xc2\xbb %s", T("OBJECTS"), forge_category_name(h3_forge.category));
	else
		snprintf(text, sizeof(text), "%s", T("OBJECTS"));
	h3_capitals(text);
	text_show(named(screen, "crumb"), text);
	text[0] = line[0] = 0;
	if (target >= 0 && h3_forge.in_category)
	{
		struct forge_item item;

		if (forge_category_item(h3_forge.category, (short)target, &item))
		{
			snprintf(text, sizeof(text), "%s", item.name);
			snprintf(line, sizeof(line), T("Placed: %d\nBudget: %d/%d objects, %d/%d AI"), item.placed, objects,
				FORGE_MAXIMUM_OBJECTS, ai, FORGE_MAXIMUM_AI);
		}
	}
	else if (target >= 0 && target < category_count)
	{
		snprintf(text, sizeof(text), "%s", forge_category_name(categories[target]));
		snprintf(line, sizeof(line), T("%d objects of this map.\nBudget: %d/%d objects, %d/%d AI"),
			forge_category_item_count(categories[target]), objects, FORGE_MAXIMUM_OBJECTS, ai, FORGE_MAXIMUM_AI);
	}
	text_show(named(screen, "info_name"), text);
	text_show(named(screen, "info_text"), line);
	/* (said as it changes: the tests read it) */
	{
		char said[160];

		snprintf(said, sizeof(said), "%s%s%s (%d of %ld)", h3_forge.in_category ?
			forge_category_name(h3_forge.category) : T("OBJECTS"), text[0] ? ": " : "", text, target + 1, count);
		if (strcmp(said, h3_forge.said))
		{
			snprintf(h3_forge.said, sizeof(h3_forge.said), "%s", said);
			platform_log("menus: forge browser: %s", said);
		}
	}
}

/* the category view of the browser's next (step 1) or previous (-1)
category of the map, from its first item */
static void h3_forge_browser_step(int step)
{
	short categories[NUMBER_OF_FORGE_CATEGORIES];
	short count = h3_forge_categories(categories);
	short index, at = 0;

	if (!h3_forge.in_category || count <= 0)
		return;
	for (index = 0; index < count; index++)
	{
		if (categories[index] == h3_forge.category)
			at = index;
	}
	h3_forge.category = categories[(at + step + count) % count];
	browser_view_reset(&h3_forge.view);
	h3_forge.chosen = 0;
}

/* A on a row: a category opened, or its item chosen (forge.c spawns it in
the monitor's hand and closes the browser) */
static void h3_forge_browser_choose(void)
{
	short categories[NUMBER_OF_FORGE_CATEGORIES];
	short count = h3_forge_categories(categories);

	if (h3_forge.chosen < 0)
		return;
	if (!h3_forge.in_category)
	{
		if (h3_forge.chosen >= count)
			return;
		h3_forge.in_category = TRUE;
		h3_forge.category = categories[h3_forge.chosen];
		browser_view_reset(&h3_forge.view);
		h3_forge.chosen = 0;
		return;
	}
	forge_browser_choose(h3_forge.category, (short)h3_forge.chosen);
}

/* B: from a category, back to the categories (on it); from them, closed */
static void h3_forge_browser_back(void)
{
	short categories[NUMBER_OF_FORGE_CATEGORIES];
	short count = h3_forge_categories(categories);
	short index;

	if (!h3_forge.in_category)
	{
		forge_menus_close_request();
		return;
	}
	h3_forge.in_category = FALSE;
	browser_view_reset(&h3_forge.view);
	h3_forge.chosen = 0;
	for (index = 0; index < count; index++)
	{
		if (categories[index] == h3_forge.category)
			h3_forge.chosen = index;
	}
}

/* the Forge menu's title (FORGE · <map>) and its monitor row's words */
static void h3_forge_title_update(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	char const *scenario = main_get_map_name();
	char text[128];
	short index;
	char const *name = "";

	for (index = 0; scenario && index < forge_map_count(); index++)
	{
		if (!_stricmp(scenario, forge_map_path(index)))
			name = T(forge_map_name(index));
	}
	snprintf(text, sizeof(text), "%s \xc2\xb7 %s", T("FORGE"), name);
	h3_capitals(text);
	text_show(widget, text);
	snprintf(text, sizeof(text), "%s", forge_monitor_active() ? T("PLAY AS SPARTAN") : T("MONITOR"));
	text_show(named(screen, "toggle"), text);
}

/* the Forge menu's controls: the Vita's buttons, or the keyboard's */
static void h3_forge_controls_update(struct widget_instance *widget)
{
#ifdef HALO_VITA
	text_show(widget, T("Fly: left stick. Up: Cross. Down: D-pad down.\nFaster: hold L. Look: right stick.\n"
		"Grab, drop: R. Turn, push: L + right stick.\nObjects: Square. Delete: Triangle.\n"
		"Copy: Circle. Upright: D-pad left.\nSpeed: D-pad right. Spartan: Select + D-pad up."));
#else
	text_show(widget, T("Fly: W A S D. Up: Space. Down: C.\nFaster: Shift. Look: mouse.\n"
		"Grab, drop: left button. Turn: right button.\nPush, pull: wheel. Objects: E. Delete: Del.\n"
		"Copy: F. Upright: X. Speed: Q.\nSpartan: Tab, or F1 + Z."));
#endif
}

/* a text box's string of its string list */
static void string_show(struct widget_instance *widget, short index)
{
	if (widget && widget->type == _ui_widget_type_text_box)
		widget->parameters.text_box.string_list_index = index;
}

static void stars(char *out, int size, char const *text)
{
	int length = 0;

	for (; text && *text && length < size - 1; text++)
		out[length++] = '*';
	out[length] = 0;
}

/* printable ASCII, no spaces at either end, at most size - 1 characters */
static void sanitize(char *out, int size, char const *text)
{
	int length = 0;

	for (; text && *text && length < size - 1; text++)
	{
		if ((unsigned char)*text >= 0x20 && (unsigned char)*text <= 0x7E && !(*text == ' ' && !length))
			out[length++] = *text;
	}
	while (length > 0 && out[length - 1] == ' ')
		length--;
	out[length] = 0;
}

/* whether internet play runs (the panel's Connection: Online) */
static boolean internet_play(void)
{
	char text[96];

	return p2p_status(text, sizeof(text)) != 0;
}

/* a Play page setting, set as the settings panel sets it (on the Vita:
applied and kept in settings.txt) */
static void setting_set(char const *variable, char const *value)
{
	if (halo_test_setting_hook && halo_test_setting_hook(variable, value))
		return;
	setenv(variable, value, 1);
	if (!strcmp(variable, "HALO_NET_LOBBY_NAME"))
		p2p_lobby_set_name(value);
	else if (!strcmp(variable, "HALO_NET_LOBBY_PASSWORD"))
		p2p_lobby_set_password(value);
	else if (!strcmp(variable, "HALO_NET_HOST_PUBLIC"))
		p2p_lobby_set_public(!strcmp(value, "true"));
	__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
}

static char const *setting_get(char const *variable, char const *otherwise)
{
	char const *value = getenv(variable);

	return value && *value ? value : otherwise;
}

/* ---- the Halo 3 multiplayer lobby (halo3/multiplayer.xml): it stands for
the Xbox's Multiplayer screen, as OpenCE's does with Menu style Halo CE, and
opens the same flows (the server browser, Create Game, the code, the System
Link and split screen screens), laid out as Halo 3's custom games lobby. Its
NETWORK is the lobby's own choice, ONLINE (internet play: the settings
panel's Connection Online), SYSTEM LINK (Same Wi-Fi, or AD HOC with the
connection Ad hoc) or OFFLINE (split screen), and picks what START GAME and
BROWSE GAMES open. GAME and MAP are the game type and the map the Xbox's
lists, after START GAME, open on (the ones last used: kept as the Xbox's
lists keep them, saved_game_files.c), chosen in pickers as Halo 3's. */

enum
{
	_h3_network_online,
	_h3_network_system_link,
	_h3_network_offline,
	NUMBER_OF_H3_NETWORKS
};

#define H3_MAP_COUNT 13
/* (the mp_map_grafix frame of a map that is not the Xbox's: its "?") */
#define H3_MAP_OTHER_PICTURE 13
#define H3_GAMETYPE_ROWS 12
#define H3_MAXIMUM_GAMETYPES 100

/* the Xbox's multiplayer levels, in its level list's order (its pictures'
and descriptions' too: mp_map_grafix, map_data), and their names */
static char const *const h3_map_paths[H3_MAP_COUNT] =
{
	"levels\\test\\beavercreek\\beavercreek", "levels\\test\\sidewinder\\sidewinder",
	"levels\\test\\damnation\\damnation", "levels\\test\\ratrace\\ratrace", "levels\\test\\prisoner\\prisoner",
	"levels\\test\\hangemhigh\\hangemhigh", "levels\\test\\chillout\\chillout", "levels\\test\\carousel\\carousel",
	"levels\\test\\boardingaction\\boardingaction", "levels\\test\\bloodgulch\\bloodgulch",
	"levels\\test\\wizard\\wizard", "levels\\test\\putput\\putput", "levels\\test\\longest\\longest",
};
static char const *const h3_map_names[H3_MAP_COUNT] =
{
	"Battle Creek", "Sidewinder", "Damnation", "Rat Race", "Prisoner", "Hang 'Em High", "Chill Out", "Derelict",
	"Boarding Action", "Blood Gulch", "Wizard", "Chiron TL-34", "Longest",
};

static struct
{
	boolean initialized;
	short network;
	/* the map: one of the Xbox's (map_index), else another's path (a Custom
	Edition map, map_index NONE) */
	short map_index;
	char map_other[256];
	/* the game types the Xbox's list offers (valid ones), their names, the
	one chosen (NONE: none) */
	long gametypes[H3_MAXIMUM_GAMETYPES];
	char gametype_names[H3_MAXIMUM_GAMETYPES][48];
	short gametype_count;
	short gametype;
	/* the game type picker's rows (they scroll: browser_list.h) */
	struct browser_view gametype_view;
	short gametype_cursor;
	short gametype_page;
	char said[192];
} h3_mp;

static char const *h3_network_name(short network)
{
	if (network == _h3_network_online)
		return T("ONLINE");
	if (network == _h3_network_offline)
		return T("OFFLINE");
	/* (the connection Ad hoc: system link over the Vitas' own network) */
	return config_boolean("network.adhoc") ? T("AD HOC") : T("SYSTEM LINK");
}

/* a name of the game's (UTF-16) as UTF-8 */
static void h3_utf8(char *out, int size, wchar_t const *text)
{
	int length = 0;

	for (; text && *text && length < size - 4; text++)
	{
		unsigned long code = (unsigned short)*text;

		if (code < 0x80)
			out[length++] = (char)code;
		else if (code < 0x800)
		{
			out[length++] = (char)(0xC0 | (code >> 6));
			out[length++] = (char)(0x80 | (code & 0x3F));
		}
		else
		{
			out[length++] = (char)(0xE0 | (code >> 12));
			out[length++] = (char)(0x80 | ((code >> 6) & 0x3F));
			out[length++] = (char)(0x80 | (code & 0x3F));
		}
	}
	out[length] = 0;
}

/* the game types the Xbox's game type list offers (the first local
player's, the valid ones), and the one it opens on: the one last used, else
its first */
static void h3_mp_gametypes_read(void)
{
	long indices[H3_MAXIMUM_GAMETYPES];
	word count = H3_MAXIMUM_GAMETYPES;
	char directory[MAXIMUM_FILENAME_LENGTH + 1];
	long remembered = NONE;
	short index;

	playlist_profiles_enumerate_available_to_local_player_index(0, &count, indices);
	h3_mp.gametype_count = 0;
	for (index = 0; index < (short)MIN(count, H3_MAXIMUM_GAMETYPES); index++)
	{
		/* (playlist_profile_get_display_name's: MAX_GAMENAME, 128) */
		wchar_t name[128];
		short slot = h3_mp.gametype_count;

		if (indices[index] == NONE || !TEST_FLAG(indices[index], _saved_game_file_index_valid_bit))
			continue;
		h3_mp.gametypes[slot] = indices[index];
		h3_mp.gametype_names[slot][0] = 0;
		if (playlist_profile_get_display_name(indices[index], name))
			h3_utf8(h3_mp.gametype_names[slot], sizeof(h3_mp.gametype_names[slot]), name);
		h3_mp.gametype_count++;
	}
	if (saved_game_file_retrieve_last_used_multiplayer_variant_directory(directory))
		remembered = saved_game_file_find_profile_index_for_directory_path(directory, 1);
	h3_mp.gametype = h3_mp.gametype_count ? 0 : NONE;
	for (index = 0; index < h3_mp.gametype_count && remembered != NONE; index++)
	{
		if (h3_mp.gametypes[index] == remembered)
			h3_mp.gametype = index;
	}
}

/* the map the Xbox's level list opens on: the one last used, else its first */
static void h3_mp_map_read(void)
{
	char map[MAXIMUM_FILENAME_LENGTH + 1];
	short index;

	h3_mp.map_index = 0;
	h3_mp.map_other[0] = 0;
	map[0] = 0;
	if (!saved_game_file_retrieve_last_used_multiplayer_map(map) || !map[0])
		return;
	map[sizeof(map) - 1] = 0;
	for (index = 0; index < H3_MAP_COUNT; index++)
	{
		if (!_stricmp(map, h3_map_paths[index]))
		{
			h3_mp.map_index = index;
			return;
		}
	}
	h3_mp.map_index = NONE;
	snprintf(h3_mp.map_other, sizeof(h3_mp.map_other), "%s", map);
}

/* (the lobby opened) the network the first time (ONLINE with internet play,
else SYSTEM LINK), then the player's; the map and game type the Xbox's
lists would open on */
static void h3_mp_initialize(void)
{
	if (!h3_mp.initialized)
	{
		h3_mp.initialized = TRUE;
		h3_mp.network = internet_play() ? _h3_network_online : _h3_network_system_link;
	}
	h3_mp_map_read();
	h3_mp_gametypes_read();
}

static char const *h3_mp_map_name(void)
{
	char const *slash;

	if (h3_mp.map_index != NONE)
		return h3_map_names[h3_mp.map_index];
	slash = strrchr(h3_mp.map_other, '\\');
	return slash ? slash + 1 : h3_mp.map_other;
}

static char const *h3_mp_gametype_name(void)
{
	return h3_mp.gametype != NONE ? h3_mp.gametype_names[h3_mp.gametype] : T("NONE");
}

/* (port h3 mp lobby: the lobby, and Create Game's GAME TYPE and MAP rows)
the rows' values (network_row, game_row, map_row; game_value and
map_value), the rows each network offers (start_online, start_link,
start_offline, browse_online, browse_link, code_row), the map's picture
(map_picture: ui.map's mp_map_grafix), the caption, the players and the
status (players, ready, ready_sub) */
static void h3_mp_lobby_update(struct widget_instance *widget)
{
	static char const *const rows[] = { "start_online", "start_link", "start_offline", "browse_online", "browse_link",
		"code_row" };
	/* (each row's network: the code's ONLINE's) */
	static short const row_networks[] = { _h3_network_online, _h3_network_system_link, _h3_network_offline,
		_h3_network_online, _h3_network_system_link, _h3_network_online };
	struct widget_instance *screen = screen_of(widget);
	struct widget_instance *found;
	char text[192], said[192];
	short index;
	int maximum;

	if (!h3_mp.initialized)
		h3_mp_initialize();
	snprintf(text, sizeof(text), "%s: %s", T("NETWORK"), h3_network_name(h3_mp.network));
	h3_capitals(text);
	text_show(named(screen, "network_row"), text);
	snprintf(said, sizeof(said), "%s", text);
	snprintf(text, sizeof(text), "%s: %s", T("GAME"), h3_mp_gametype_name());
	h3_capitals(text);
	text_show(named(screen, "game_row"), text);
	snprintf(said + strlen(said), sizeof(said) - strlen(said), ", %s", text);
	snprintf(text, sizeof(text), "%s: %s", T("MAP"), h3_mp_map_name());
	h3_capitals(text);
	text_show(named(screen, "map_row"), text);
	snprintf(said + strlen(said), sizeof(said) - strlen(said), ", %s", text);
	snprintf(text, sizeof(text), "%s", h3_mp_gametype_name());
	h3_capitals(text);
	text_show(named(screen, "game_value"), text);
	snprintf(text, sizeof(text), "%s", h3_mp_map_name());
	h3_capitals(text);
	text_show(named(screen, "map_value"), text);
	for (index = 0; index < (short)NUMBEROF(rows); index++)
	{
		if ((found = named(screen, rows[index])) != NULL)
		{
			boolean shown = row_networks[index] == h3_mp.network;

			widget_instance_set_visibility_recursive(found, shown);
			found->disabled = !shown;
		}
	}
	if ((found = named(screen, "map_picture")) != NULL)
		found->animation.current_frame_index = h3_mp.map_index != NONE ? h3_mp.map_index : H3_MAP_OTHER_PICTURE;
	/* (Halo 3's: "Slayer on Valhalla") */
	snprintf(text, sizeof(text), T("%s on %s"), h3_mp_gametype_name(), h3_mp_map_name());
	text_show(named(screen, "caption"), text);
	/* ("1 Player (16 max)": the most players a game of that network takes) */
	maximum = h3_mp.network == _h3_network_offline ? 4 : h3_mp.network == _h3_network_system_link ? 16 :
		(int)PIN(atol(setting_get("HALO_NET_MAX_PLAYERS", "16")), 2, 16);
	snprintf(text, sizeof(text), T("1 Player (%d max)"), maximum);
	text_show(named(screen, "players"), text);
	text_show(named(screen, "ready"), T("Ready"));
	if (h3_mp.network == _h3_network_online)
		snprintf(text, sizeof(text), "%s", network_wire_crossplay() ? T("Crossplay is on: turn it off to host") :
			T("Host a game on the Internet, or join one."));
	else if (h3_mp.network == _h3_network_system_link)
		snprintf(text, sizeof(text), "%s", config_boolean("network.adhoc") ? T("Play with the PS Vitas near you.") :
			T("Play with the PS Vitas on your Wi-Fi network."));
	else
		snprintf(text, sizeof(text), "%s", T("This party is local to this PS Vita."));
	text_show(named(screen, "ready_sub"), text);
	/* (said when it changes: the tests read it) */
	if (named(screen, "network_row") && strcmp(said, h3_mp.said))
	{
		snprintf(h3_mp.said, sizeof(h3_mp.said), "%s", said);
		platform_log("menus: h3 mp lobby: %s", said);
	}
}

/* (port h3 gametypes) the game type picker's rows (item_1 to item_12): the
game types, scrolling past the twelve as the server browser's do (a step past
the first or last row, the triggers a page) */
static void h3_mp_gametypes_update(struct widget_instance *list)
{
	struct widget_instance *child, *focus = NULL;
	long index, focused_row = NONE, target;

	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		if (child == list->focused_child)
			focused_row = index;
	}
	target = browser_view_follow(&h3_mp.gametype_view, H3_GAMETYPE_ROWS, h3_mp.gametype_count, (int)focused_row,
		h3_mp.gametype_cursor, h3_mp.gametype_page);
	h3_mp.gametype_page = 0;
	h3_mp.gametype_cursor = (short)target;
	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		boolean shown = index < h3_mp.gametype_view.visible;
		char text[64];

		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		if (!shown)
			continue;
		snprintf(text, sizeof(text), "%s", h3_mp.gametype_names[h3_mp.gametype_view.offset + index]);
		h3_capitals(text);
		text_show(child, text);
		if (index == h3_mp.gametype_view.row)
			focus = child;
	}
	if (focus && focus != list->focused_child)
		ui_widget_port_give_focus(list, focus);
}

/* the picker's choice (port h3 gametype set): the game type the Xbox's list
opens on, as its own choice keeps it */
static void h3_mp_gametype_set(struct widget_instance *widget)
{
	char directory[MAXIMUM_FILENAME_LENGTH + 1];
	long index = h3_mp.gametype_view.offset + h3_child_index(widget);

	if (index < 0 || index >= h3_mp.gametype_count)
		return;
	h3_mp.gametype = (short)index;
	if (saved_game_file_get_path_to_enclosing_directory(h3_mp.gametypes[index], directory))
		saved_game_file_remember_last_used_multiplayer_variant_directory(directory);
	platform_log("menus: h3 mp: game type %s", h3_mp.gametype_names[index]);
}

static void h3_mp_map_set(struct widget_instance *widget)
{
	short index = (short)PIN(h3_child_index(widget), 0, H3_MAP_COUNT - 1);

	h3_mp.map_index = index;
	h3_mp.map_other[0] = 0;
	saved_game_file_remember_last_used_multiplayer_map(h3_map_paths[index]);
	platform_log("menus: h3 mp: map %s", h3_map_names[index]);
}

/* (port h3 network set) the network picker's choice; ONLINE only with
internet play (else FALSE: the screen saying how to turn it on opens) */
static boolean h3_mp_network_set(struct widget_instance *widget)
{
	short network = (short)PIN(h3_child_index(widget), 0, NUMBER_OF_H3_NETWORKS - 1);

	if (network == _h3_network_online && !internet_play())
	{
		platform_log("menus: h3 mp: ONLINE chosen, internet play is off");
		return FALSE;
	}
	h3_mp.network = network;
	platform_log("menus: h3 mp: network %s", h3_network_name(network));
	return TRUE;
}

/* ---- typing */

static void typing_begin(short typing, char const *title, char const *text, int maximum, boolean password)
{
	menu_functions.typing = typing;
	snprintf(halo_text_input_title, sizeof(halo_text_input_title), "%s", title);
	snprintf(halo_text_input_text, sizeof(halo_text_input_text), "%s", text ? text : "");
	halo_text_input_maximum = maximum;
	halo_text_input_password = password;
#ifdef HALO_VITA
	__atomic_store_n(&halo_text_input_state, HALO_TEXT_INPUT_REQUESTED, __ATOMIC_RELEASE);
#else
	{
		/* (HALO_TEST_TEXT_INPUT: the next answer, "!" a cancel; none, or a
		release build: cancelled) */
		static int answered;
#ifndef HALO_RELEASE
		char const *answers = getenv("HALO_TEST_TEXT_INPUT");
#else
		char const *answers = NULL;
#endif
		int index = 0;
		char answer[HALO_TEXT_INPUT_SIZE];
		int length = 0;

		while (answers && *answers && index < answered)
		{
			if (*answers++ == '|')
				index++;
		}
		while (answers && *answers && *answers != '|' && length < (int)sizeof(answer) - 1)
			answer[length++] = *answers++;
		answer[length] = 0;
		answered++;
		if (answers && (length || *answers == '|') && strcmp(answer, "!"))
		{
			snprintf(halo_text_input_text, sizeof(halo_text_input_text), "%s", answer);
			__atomic_store_n(&halo_text_input_state, HALO_TEXT_INPUT_DONE, __ATOMIC_RELEASE);
		}
		else
			__atomic_store_n(&halo_text_input_state, HALO_TEXT_INPUT_CANCELLED, __ATOMIC_RELEASE);
		platform_log("menus: typing \"%s\": %s", title, length && strcmp(answer, "!") ? "typed" : "cancelled");
	}
#endif
}

/* the text typed for the typing, once (TRUE), else FALSE */
static boolean typing_done(short typing, char *text, int size)
{
	int state = __atomic_load_n(&halo_text_input_state, __ATOMIC_ACQUIRE);

	if (menu_functions.typing != typing || (state != HALO_TEXT_INPUT_DONE && state != HALO_TEXT_INPUT_CANCELLED))
		return FALSE;
	menu_functions.typing = _typing_none;
	__atomic_store_n(&halo_text_input_state, HALO_TEXT_INPUT_IDLE, __ATOMIC_RELEASE);
	if (state != HALO_TEXT_INPUT_DONE)
		return FALSE;
	sanitize(text, size, halo_text_input_text);
	return TRUE;
}

/* ---- joining */

static void join_begin(struct p2p_lobby_entry const *entry, char const *password)
{
	if (!p2p_lobby_join(entry->id, password ? password : ""))
	{
		snprintf(menu_functions.status, sizeof(menu_functions.status), T("%s is no longer listed"), entry->name);
		menu_functions.password_help = _help_gone;
		platform_log("menus: the public game %s is no longer listed", entry->name);
		return;
	}
	menu_functions.join_watched = TRUE;
	menu_functions.password_help = _help_joining;
	snprintf(menu_functions.status, sizeof(menu_functions.status), T("Joining %s..."), entry->name);
	platform_log("menus: joining the public game %s%s", entry->name, password && *password ? " with its password" : "");
}

/* a join under way: once its invite is handed on, the System Link screen
(where the game shows once its host is reached) */
static void join_watch(void)
{
	int state;

	if (!menu_functions.join_watched)
		return;
	state = p2p_lobby_join_state();
	if (state == P2P_LOBBY_JOIN_WRONG_PASSWORD)
	{
		menu_functions.join_watched = FALSE;
		menu_functions.password_help = _help_wrong;
		menu_functions.password[0] = 0;
		snprintf(menu_functions.status, sizeof(menu_functions.status), "%s", T("Wrong password"));
		platform_log("menus: the password was wrong");
	}
	else if (state == P2P_LOBBY_JOIN_GONE)
	{
		menu_functions.join_watched = FALSE;
		menu_functions.password_help = _help_gone;
		snprintf(menu_functions.status, sizeof(menu_functions.status), "%s", T("That game is gone"));
		if (menu_functions.chosen_valid)
			p2p_lobby_mark_failed(menu_functions.chosen.id);
		platform_log("menus: the public game is gone");
	}
	else if (state == P2P_LOBBY_JOIN_JOINING)
	{
		menu_functions.join_watched = FALSE;
		menu_functions.open_system_link = TRUE;
		platform_log("menus: the public game's invite handed on; the System Link screen next");
	}
}

/* ---- the Multiplayer screen */

/* (each item's description and picture: by its place in the list) */
static short const multiplayer_pictures[] = { 0, 1, 0, 0, 1, 0, 1, 2 };

static void multiplayer_update(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	struct widget_instance *child;
	short index = 0;

	for (child = list->child; child && child != list->focused_child; child = child->next)
		index++;
	if (!child || !description)
		return;
	{
		struct widget_instance *picture = named(description, "description_picture");
		struct widget_instance *text = named(description, "description_text");

		if (picture)
			picture->animation.current_frame_index = multiplayer_pictures[index % NUMBEROF(multiplayer_pictures)];
		string_show(text, index);
	}
}

/* ---- the server browser */

static void browser_large_watch(void);

static void row_column(struct widget_instance *row, char const *name, char const *text)
{
	text_show(named(row, name), text);
}

/* (automated tests only, never in release builds) HALO_TEST_BROWSER_GAMES=N:
N made-up games after those listed, for the browser's scrolling and sorts
(run_netns_online_test.sh browserscroll); not joinable. With
HALO_TEST_BROWSER_PC=1 too, both platforms (run_menus_test.sh h3browser):
every second one a PC game (its badge PC, as the crossplay wire's), every
third from the third one with a password */
static void browser_test_games(void)
{
#ifndef HALO_RELEASE
	static char const *const maps[] = { "Blood Gulch", "Wizard", "Hang 'Em High", "Prisoner", "Chill Out" };
	static char const *const gametypes[] = { "Slayer", "CTF", "King", "Oddball" };
	char const *value = getenv("HALO_TEST_BROWSER_GAMES");
	char const *both = getenv("HALO_TEST_BROWSER_PC");
	long count = value ? atol(value) : 0, index;

	for (index = 0; index < count && menu_functions.game_count < P2P_LOBBY_MAXIMUM_ENTRIES; index++)
	{
		struct p2p_lobby_entry *entry = &menu_functions.games[menu_functions.game_count++];

		memset(entry, 0, sizeof(*entry));
		snprintf(entry->id, sizeof(entry->id), "test%02ld", index);
		snprintf(entry->name, sizeof(entry->name), "Test game %02ld", index);
		snprintf(entry->map, sizeof(entry->map), "%s", maps[index % 5]);
		snprintf(entry->gametype, sizeof(entry->gametype), "%s", gametypes[index % 4]);
		entry->players = (int)(index * 7 % 16);
		entry->maximum = 16;
		entry->open = entry->compatible = 1;
		if (both && atoi(both))
		{
			if (index % 2)
				snprintf(entry->badge, sizeof(entry->badge), "%s", T("PC"));
			entry->locked = index % 3 == 2;
		}
	}
#endif
}

static void browser_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focused = list->focused_child, *focus = NULL;
	char line[96], message[128];
	long index, focused_row = NONE, chosen = NONE, target;

	join_watch();
	browser_large_watch();
	menu_functions.game_count = p2p_lobby_entries(menu_functions.games, P2P_LOBBY_MAXIMUM_ENTRIES);
	browser_test_games();
	browser_list_sort(menu_functions.games, (int)menu_functions.game_count, menu_functions.sort);
	/* the row the menus focus (a step up or down past the first or last
	scrolls: browser_view_follow), and where the game chosen went */
	for (index = 0, child = list->child; child; child = child->next)
	{
		if (strncmp(child->name, "row_", 4))
			continue;
		if (child == focused)
			focused_row = index;
		index++;
	}
	for (index = 0; menu_functions.chosen_valid && index < menu_functions.game_count; index++)
	{
		if (!strcmp(menu_functions.games[index].id, menu_functions.chosen.id))
		{
			chosen = index;
			break;
		}
	}
	target = browser_view_follow(&menu_functions.view, BROWSER_ROWS, (int)menu_functions.game_count, (int)focused_row,
		(int)chosen, menu_functions.page);
	menu_functions.page = 0;
	menu_functions.entry_count = menu_functions.view.visible;
	for (index = 0; index < menu_functions.entry_count; index++)
		menu_functions.entries[index] = menu_functions.games[menu_functions.view.offset + index];
	/* the rows: one for each game shown, the rest hidden */
	for (index = 0, child = list->child; child; child = child->next)
	{
		struct p2p_lobby_entry const *entry;
		char text[48];

		if (strncmp(child->name, "row_", 4))
			continue;
		if (index >= menu_functions.entry_count)
		{
			widget_instance_set_visibility_recursive(child, FALSE);
			child->disabled = TRUE;
			index++;
			continue;
		}
		entry = &menu_functions.entries[index];
		widget_instance_set_visibility_recursive(child, TRUE);
		child->disabled = FALSE;
		/* (the lock: a game with a password, 2a6dc2b7; the Halo 3 rows' is
		their badge's) */
		if (named(child, "row_lock"))
			widget_instance_set_visibility_recursive(named(child, "row_lock"), entry->locked != 0);
		/* (the Halo 3 browser's rows: the platform's badge, VITA or PC, with
		the lock beside it for a password - row_badge's frames: VITA, PC, then
		the same locked - and the rest of a PC game's badges, SERVER or
		EXPERIMENTAL, before its name) */
		if (named(child, "row_badge"))
		{
			char const *rest = entry->badge;
			size_t pc = strlen(T("PC"));

			named(child, "row_badge")->animation.current_frame_index =
				(short)((entry->badge[0] ? 1 : 0) + (entry->locked ? 2 : 0));
			if (!strncmp(rest, T("PC"), pc))
				rest += pc;
			while (*rest == ' ')
				rest++;
			if (*rest)
			{
				char badged[P2P_LOBBY_NAME_SIZE + sizeof(entry->badge) + 4];

				snprintf(badged, sizeof(badged), "[%s] %s", rest, entry->name);
				row_column(child, "row_name", badged);
			}
			else
				row_column(child, "row_name", entry->name);
		}
		/* (a PC game's badges first: crossplay_guard.h) */
		else if (entry->badge[0])
		{
			char badged[P2P_LOBBY_NAME_SIZE + sizeof(entry->badge) + 4];

			snprintf(badged, sizeof(badged), "[%s] %s", entry->badge, entry->name);
			row_column(child, "row_name", badged);
		}
		else
			row_column(child, "row_name", entry->name);
		row_column(child, "row_map", entry->map);
		row_column(child, "row_game", entry->gametype);
		snprintf(text, sizeof(text), "%d/%d", entry->players, entry->maximum);
		row_column(child, "row_players", text);
		if (index == menu_functions.view.row)
			focus = child;
		index++;
	}
	/* the focus on the chosen game's row (the menus' own step undone where
	the rows scrolled instead), the focused row outlined (its focused frame,
	812ffeea) */
	if (focus && focus != focused)
		ui_widget_port_give_focus(list, focus);
	for (child = list->child; child; child = child->next)
	{
		if (!strncmp(child->name, "row_", 4))
			child->animation.current_frame_index = child == focus ? 1 : 0;
	}
	if (target != NONE)
	{
#ifndef HALO_RELEASE
		/* (the tests': each game chosen, said once) */
		if (getenv("HALO_TEST_BROWSER_GAMES") &&
			(!menu_functions.chosen_valid || strcmp(menu_functions.chosen.id, menu_functions.games[target].id)))
		{
			platform_log("menus: the server browser's game %ld of %ld chosen: %s (rows from %d)", target + 1,
				menu_functions.game_count, menu_functions.games[target].name, menu_functions.view.offset + 1);
		}
#endif
		menu_functions.chosen = menu_functions.games[target];
		menu_functions.chosen_valid = TRUE;
	}
	else
		menu_functions.chosen_valid = FALSE;
	text_show(named(screen, "ticker_players_value"), target != NONE ? menu_functions.chosen.players_line : "");
	text_show(named(screen, "ticker_rules_value"), target != NONE ? menu_functions.chosen.rules : "");
	/* the status at the top right, and with no game to list, why, in the
	list's own space (the browser_message line): the brokers out of reach,
	or reached with no public games (p2p_lobby_browse_status) */
	message[0] = 0;
	if (menu_functions.status[0])
		snprintf(line, sizeof(line), "%s", menu_functions.status);
	else if (!internet_play())
		snprintf(line, sizeof(line), "%s", T("Internet play is off"));
	else
	{
		char text[96];

		switch (p2p_lobby_browse_status(text, sizeof(text)))
		{
		case P2P_LOBBY_BROWSE_UNREACHABLE:
			snprintf(line, sizeof(line), "%s", T("Can't reach the game list"));
			snprintf(message, sizeof(message), "%s", text);
			break;
		case P2P_LOBBY_BROWSE_EMPTY:
			snprintf(line, sizeof(line), "%s", T("No public games"));
			snprintf(message, sizeof(message), "%s", text);
			break;
		case P2P_LOBBY_BROWSE_GAMES:
			/* (each count's words whole, for the translations) */
			if (menu_functions.game_count == 1)
				snprintf(line, sizeof(line), "%s", T("1 public game"));
			else
				snprintf(line, sizeof(line), T("%ld public games"), menu_functions.game_count);
			/* (the rows shown, when they scroll, and the sort: Y) */
			if (menu_functions.game_count > BROWSER_ROWS)
			{
				size_t length = strlen(line);

				snprintf(line + length, sizeof(line) - length, ", %ld-%ld", (long)menu_functions.view.offset + 1,
					(long)menu_functions.view.offset + menu_functions.entry_count);
			}
			{
				static char const *const sorts[NUMBER_OF_BROWSER_SORTS] =
				{
					"by players", "by name", "by map", "by gametype"
				};
				size_t length = strlen(line);

				snprintf(line + length, sizeof(line) - length, ", %s", T(sorts[menu_functions.sort]));
			}
			break;
		default:
			snprintf(line, sizeof(line), "%s", T("Looking for public games..."));
			break;
		}
		if (menu_functions.entry_count)
			message[0] = 0;
	}
	text_show(named(screen, "browser_status"), line);
	text_show(named(screen, "browser_message"), message);
	/* (said in the log as it changes) */
	if (strcmp(message, menu_functions.message_logged))
	{
		if (message[0])
			platform_log("menus: the server browser says \"%s\"", message);
		snprintf(menu_functions.message_logged, sizeof(menu_functions.message_logged), "%s", message);
	}
}

/* the game of a row (by its name, row_<n>), or the focused one */
static struct p2p_lobby_entry const *browser_entry(struct widget_instance *widget)
{
	if (widget && !strncmp(widget->name, "row_", 4) && strncmp(widget->name, "row_name", 8))
	{
		long row = atol(widget->name + 4) - 1;

		if (row >= 0 && row < menu_functions.entry_count)
			return &menu_functions.entries[row];
	}
	return menu_functions.chosen_valid ? &menu_functions.chosen : NULL;
}

/* a game chosen to join, past the crossplay guardrails: its password
first, if it has one */
static void browser_join_entry(struct p2p_lobby_entry const *entry)
{
	if (entry->locked)
	{
		/* (its password first: the password screen, from the main loop) */
		menu_functions.password[0] = 0;
		menu_functions.password_help = _help_ask;
		menu_functions.open_password = TRUE;
		return;
	}
	join_begin(entry, NULL);
}

/* the player's answer about the large or co-op PC game chosen
(crossplay_guard.h): remembered, so that its lobby does not ask again; yes
joins it */
static void browser_large_answered(int yes)
{
	struct p2p_lobby_entry const *entry = &menu_functions.large_entry;

	menu_functions.large_asked = FALSE;
	crossplay_guard_answer(entry->name, yes, system_milliseconds());
	platform_log("menus: the %s PC game %s (%d of %d players): the player said %s", entry->coop ? "co-op" : "large",
		entry->name, entry->players, entry->maximum, yes ? "join anyway" : "no");
	if (!yes)
	{
		snprintf(menu_functions.status, sizeof(menu_functions.status), "%s", T("Not joined"));
		return;
	}
	browser_join_entry(entry);
}

/* (each frame on the browser) the answer, once the player gives it */
static void browser_large_watch(void)
{
	int answer;

	if (!menu_functions.large_asked)
		return;
	answer = platform_question_answer();
	if (answer >= 0)
		browser_large_answered(answer);
}

static boolean browser_join(struct widget_instance *widget)
{
	struct p2p_lobby_entry const *entry = browser_entry(widget);

	/* (a question unanswered stays no lock: a game chosen again asks anew) */
	if (!entry)
		return TRUE;
	menu_functions.large_asked = FALSE;
	menu_functions.chosen = *entry;
	menu_functions.chosen_valid = TRUE;
	menu_functions.status[0] = 0;
	if (entry->own)
	{
		snprintf(menu_functions.status, sizeof(menu_functions.status), "%s", T("That is your own game"));
		return TRUE;
	}
	/* (crossplay_guard.h: a large or co-op PC game, Experimental, asked
	about once) */
	if (network_wire_crossplay() && (entry->experimental || entry->coop) &&
		crossplay_guard_answered(entry->name, system_milliseconds()) != 1)
	{
		int answer = crossplay_guard_test_answer();
		int verdict = entry->coop ? CROSSPLAY_GUARD_COOP : CROSSPLAY_GUARD_LARGE;

		menu_functions.large_entry = *entry;
		menu_functions.large_asked = TRUE;
		platform_log("menus: the %s PC game %s (%d of %d players): asking the player", entry->coop ? "co-op" : "large",
			entry->name, entry->players, entry->maximum);
		if (answer >= 0)
			browser_large_answered(answer);
		else
			platform_ask_question(crossplay_guard_title(verdict), crossplay_guard_question(verdict, entry->players));
		return TRUE;
	}
	browser_join_entry(entry);
	return TRUE;
}

/* ---- the password screen */

static void password_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	char text[P2P_LOBBY_PASSWORD_SIZE];

	join_watch();
	if (typing_done(_typing_password, text, sizeof(text)))
	{
		snprintf(menu_functions.password, sizeof(menu_functions.password), "%s", text);
		menu_functions.password_help = _help_ask;
	}
	stars(text, sizeof(text), menu_functions.password);
	text_show(named(screen, "password_value"), text[0] ? text : "-");
	text_show(named(screen, "password_game"), menu_functions.chosen_valid ? menu_functions.chosen.name : "");
	string_show(named(screen, "password_help"), menu_functions.password_help);
}

/* ---- Server Setup */

static struct widget_instance *spinner_of(struct widget_instance *screen, char const *row)
{
	struct widget_instance *found = named(screen, row);

	for (found = found ? found->child : NULL; found; found = found->next)
	{
		if (found->type == _ui_widget_type_spinner_list)
			return found;
	}
	return NULL;
}

static void setup_initialize(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	struct widget_instance *spinner;
	long maximum = atol(setting_get("HALO_NET_MAX_PLAYERS", "16"));
	boolean private_game = !strcmp(setting_get("HALO_NET_HOST_PUBLIC", "true"), "false");

	menu_functions.create_pending = FALSE;
	/* (back from the Halo 3 Create Game's GAME TYPE or MAP picker: what it
	showed, the name and password typed kept; else the settings') */
	if (menu_functions.setup_held)
	{
		maximum = menu_functions.held_maximum;
		private_game = menu_functions.held_private;
	}
	else
	{
		sanitize(menu_functions.lobby_name, sizeof(menu_functions.lobby_name), setting_get("HALO_NET_LOBBY_NAME", "Halo"));
		sanitize(menu_functions.lobby_password, sizeof(menu_functions.lobby_password),
			setting_get("HALO_NET_LOBBY_PASSWORD", ""));
	}
	menu_functions.setup_held = FALSE;
	/* (Max players 2 to 16: the spinner's strings) */
	spinner = spinner_of(screen, "op_max_players");
	if (spinner)
		spinner->parameters.list.selected_index = (short)(PIN(maximum, 2, 16) - 2);
	/* (PUBLIC, PRIVATE) */
	spinner = spinner_of(screen, "op_visibility");
	if (spinner)
		spinner->parameters.list.selected_index = private_game ? 1 : 0;
}

/* (port h3 setup hold) the Halo 3 Create Game's GAME TYPE or MAP picker
opening: what its rows show, for when it is back */
static void setup_hold(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	struct widget_instance *spinner = spinner_of(screen, "op_max_players");

	menu_functions.held_maximum = (short)(spinner ? PIN(spinner->parameters.list.selected_index + 2, 2, 16) : 16);
	spinner = spinner_of(screen, "op_visibility");
	menu_functions.held_private = spinner && spinner->parameters.list.selected_index == 1;
	menu_functions.setup_held = TRUE;
}

static void setup_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child;
	char text[P2P_LOBBY_PASSWORD_SIZE];
	short row = 0;

	if (typing_done(_typing_lobby_name, text, sizeof(text)) && text[0])
		snprintf(menu_functions.lobby_name, sizeof(menu_functions.lobby_name), "%.*s", LOBBY_NAME_LENGTH, text);
	if (typing_done(_typing_lobby_password, text, sizeof(text)))
		snprintf(menu_functions.lobby_password, sizeof(menu_functions.lobby_password), "%s", text);
	text_show(named(screen, "name_value"), menu_functions.lobby_name);
	stars(text, sizeof(text), menu_functions.lobby_password);
	text_show(named(screen, "password_value"), text[0] ? text : T("NONE"));
	/* the help: the focused row's */
	for (child = list->child; child && child != list->focused_child; child = child->next)
		row++;
	string_show(named(screen, "setup_help"), row);
	if (!internet_play())
		text_show(named(screen, "setup_status"), T("Internet play is off"));
	else
	{
		/* (whether the online game list can be reached, before the game is
		hosted: p2p_lobby_brokers_status) */
		char text[96];

		p2p_lobby_brokers_status(text, sizeof(text));
		text_show(named(screen, "setup_status"), text);
	}
}

static boolean setup_start(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	struct widget_instance *spinner;
	char value[8];

	setting_set("HALO_NET_LOBBY_NAME", menu_functions.lobby_name);
	spinner = spinner_of(screen, "op_max_players");
	snprintf(value, sizeof(value), "%d", spinner ? PIN(spinner->parameters.list.selected_index + 2, 2, 16) : 16);
	setting_set("HALO_NET_MAX_PLAYERS", value);
	spinner = spinner_of(screen, "op_visibility");
	setting_set("HALO_NET_HOST_PUBLIC", spinner && spinner->parameters.list.selected_index == 1 ? "false" : "true");
	setting_set("HALO_NET_LOBBY_PASSWORD", menu_functions.lobby_password);
	menu_functions.create_pending = TRUE;
	menu_functions.create_system_link = FALSE;
	platform_log("menus: Server Setup: %s, %s players, %s%s; the profile, then the map list", menu_functions.lobby_name,
		value, spinner && spinner->parameters.list.selected_index == 1 ? "private" : "public",
		menu_functions.lobby_password[0] ? ", a password" : "");
	return TRUE;
}

/* ---- the code screen */

static void code_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	char text[HALO_TEXT_INPUT_SIZE];

	if (typing_done(_typing_code, text, sizeof(text)))
	{
		snprintf(menu_functions.code, sizeof(menu_functions.code), "%.12s", text);
		menu_functions.code_help = _help_ask;
	}
	text_show(named(screen, "code_value"), menu_functions.code[0] ? menu_functions.code : "-");
	string_show(named(screen, "code_help"), menu_functions.code_help);
}

static boolean code_join(void)
{
	if (!menu_functions.code[0] || !p2p_join_code(menu_functions.code))
	{
		menu_functions.code_help = _help_not_a_code;
		return TRUE;
	}
	menu_functions.code_help = _help_joining;
	menu_functions.open_system_link = TRUE;
	platform_log("menus: joining the code %s; the System Link screen next", menu_functions.code);
	return TRUE;
}

/* ---------- public code */

boolean pc_menu_event_function_invoke(
	struct widget_instance *widget,
	struct event_record *event,
	long function_index,
	boolean *widget_deleted)
{
	(void)widget_deleted;
	switch (function_index)
	{
	case _function_mp_screen_init:
		menu_functions.create_pending = FALSE;
		menu_functions.status[0] = 0;
		return TRUE;
	case _function_mp_require_online:
		return internet_play();
	case _function_mp_require_lan:
		return TRUE;
	case _function_browser_init:
		menu_functions.status[0] = 0;
		menu_functions.join_watched = FALSE;
		menu_functions.chosen_valid = FALSE;
		menu_functions.page = 0;
		browser_view_reset(&menu_functions.view);
		p2p_lobby_browse(1);
		return TRUE;
	case _function_browser_dispose:
		/* (a locked game's password screen next: its listing is kept, which
		the join needs) */
		if (!menu_functions.open_password)
			p2p_lobby_browse(0);
		return TRUE;
	case _function_browser_refresh:
		menu_functions.status[0] = 0;
		p2p_lobby_refresh();
		return TRUE;
	case _function_browser_sort:
		/* (Y: the next sort, from the top) */
		menu_functions.sort = (short)((menu_functions.sort + 1) % NUMBER_OF_BROWSER_SORTS);
		menu_functions.chosen_valid = FALSE;
		browser_view_reset(&menu_functions.view);
		platform_log("menus: the server browser sorts %s", menu_functions.sort == _browser_sort_players ? "by players" :
			menu_functions.sort == _browser_sort_name ? "by name" : menu_functions.sort == _browser_sort_map ? "by map" :
			"by gametype");
		return TRUE;
	case _function_browser_page_up:
		menu_functions.page = -1;
		return TRUE;
	case _function_browser_page_down:
		menu_functions.page = 1;
		return TRUE;
	case _function_browser_join:
		return browser_join(widget);
	case _function_password_init:
		menu_functions.password[0] = 0;
		menu_functions.password_help = _help_ask;
		return TRUE;
	case _function_password_edit:
		typing_begin(_typing_password, T("Password"), "", PASSWORD_LENGTH, TRUE);
		return TRUE;
	case _function_password_join:
		if (menu_functions.chosen_valid && !menu_functions.join_watched)
			join_begin(&menu_functions.chosen, menu_functions.password);
		return TRUE;
	case _function_setup_init:
		setup_initialize(widget);
		/* (the brokers reached now, so that Server Setup can say whether
		the online game list is there) */
		p2p_lobby_reach_brokers();
		return TRUE;
	case _function_setup_edit_name:
		typing_begin(_typing_lobby_name, T("Lobby name"), menu_functions.lobby_name, LOBBY_NAME_LENGTH, FALSE);
		return TRUE;
	case _function_setup_edit_password:
		typing_begin(_typing_lobby_password, T("Password (empty: none)"), menu_functions.lobby_password, PASSWORD_LENGTH,
			TRUE);
		return TRUE;
	case _function_setup_start:
		return setup_start(widget);
	case _function_code_init:
		menu_functions.code[0] = 0;
		menu_functions.code_help = _help_ask;
		return TRUE;
	case _function_code_edit:
		typing_begin(_typing_code, T("Code (ABCD-EFGH)"), menu_functions.code, 9, FALSE);
		return TRUE;
	case _function_code_join:
		return code_join();
	case _function_password_back:
		/* (the password screen gone: the browser's listings with it, unless
		a join is under way) */
		if (!menu_functions.join_watched)
			p2p_lobby_browse(0);
		return TRUE;
	case _function_setup_back:
		menu_functions.create_pending = FALSE;
		/* fall through */
	case _function_code_back:
	/* (the Halo 3 main menu's live servers: the online list looked at while
	it is open, as the server browser does) */
	case _function_h3_servers_on:
		if (internet_play())
			p2p_lobby_browse(1);
		return TRUE;
	case _function_h3_servers_off:
		p2p_lobby_browse(0);
		return TRUE;
	/* (the Halo 3 main menu's Start: on the Vita the settings panel opens, and
	the handler fails so that the Xbox's profile settings do not open under
	it; elsewhere those open, as before) */
	case _function_settings_panel:
#ifdef HALO_VITA
		__atomic_store_n(&halo_settings_panel_request, 1, __ATOMIC_RELEASE);
		platform_log("menus: the settings panel asked for");
		return FALSE;
#else
		return TRUE;
#endif
	case _function_h3_campaign_init:
		h3_campaign_initialize();
		return TRUE;
	case _function_h3_mission_set:
		h3_campaign.level = (short)PIN(h3_child_index(widget), 0, h3_campaign_unlocked() - 1);
		h3_campaign_said();
		return TRUE;
	case _function_h3_difficulty_set:
		h3_campaign.difficulty = (short)PIN(h3_child_index(widget), 0, 3);
		h3_campaign_said();
		return TRUE;
	/* (a picker opened: the focus on the lobby's choice, as Halo 3's) */
	case _function_h3_mission_focus:
		h3_focus_child(widget, h3_campaign.level);
		return TRUE;
	case _function_h3_difficulty_focus:
		h3_focus_child(widget, h3_campaign.difficulty);
		return TRUE;
	case _function_h3_campaign_network_set:
		return h3_campaign_network_set(widget);
	case _function_h3_campaign_network_focus:
		h3_focus_child(widget, h3_campaign.network);
		return TRUE;
	case _function_h3_campaign_host:
		return h3_campaign_host(widget, event);
	/* (the lobby's list made: MISSION focused, NETWORK above it; back from a
	picker the game gives the focus back to the row that opened it) */
	case _function_h3_campaign_focus:
		h3_focus_child(widget, 1);
		return TRUE;
	case _function_h3_scene:
		h3_scene_request(widget);
		return TRUE;
	case _function_h3_campaign_start:
		/* (then the game's own "main menu switch to solo game", the next
		handler, as the Xbox's difficulty list does after "set difficulty") */
		main_set_map_name((char *)h3_level_paths[h3_campaign.level]);
		main_set_difficulty((word)h3_campaign.difficulty);
		platform_log("menus: h3 campaign: start %s on %s", h3_level_paths[h3_campaign.level],
			h3_difficulty_names[h3_campaign.difficulty]);
		return TRUE;
	case _function_forge_lobby_init:
		h3_forge_lobby_initialize();
		return TRUE;
	case _function_forge_map_set:
		if (forge_map_available((short)h3_child_index(widget)))
			h3_forge.map = (short)h3_child_index(widget);
		platform_log("menus: forge lobby: %s", forge_map_name(h3_forge.map));
		return TRUE;
	case _function_forge_map_focus:
		h3_focus_child(widget, h3_forge.map);
		return TRUE;
	case _function_forge_start:
		/* (START FORGE: the local game of the map, in Forge mode; nothing
		with no map to play) */
		if (!forge_map_available(h3_forge.map))
			return FALSE;
		platform_log("menus: forge lobby: start %s", forge_map_path(h3_forge.map));
		forge_game_start(h3_forge.map);
		return TRUE;
	case _function_forge_browser_init:
		h3_forge.page = 0;
		h3_forge.said[0] = 0;
		return TRUE;
	case _function_forge_browser_choose:
		h3_forge_browser_choose();
		return TRUE;
	case _function_forge_browser_back:
		h3_forge_browser_back();
		return TRUE;
	case _function_forge_browser_previous:
		h3_forge_browser_step(-1);
		return TRUE;
	case _function_forge_browser_next:
		h3_forge_browser_step(1);
		return TRUE;
	case _function_forge_toggle:
		forge_toggle_request();
		return TRUE;
	case _function_forge_quit:
		/* (as the pause screen's quit, with no game saved: a Forge game
		is not the campaign's; forge.c goes once A is let go) */
		platform_log("menus: forge: leaving to the main menu");
		forge_quit_request();
		return TRUE;
	/* (the Halo 3 multiplayer lobby, its pickers and Create Game) */
	case _function_h3_mp_init:
		h3_mp_initialize();
		return TRUE;
	case _function_h3_network_set:
		return h3_mp_network_set(widget);
	case _function_h3_network_focus:
		h3_focus_child(widget, h3_mp.network);
		return TRUE;
	case _function_h3_host_system_link:
		/* (START GAME on SYSTEM LINK: the System Link list, once the profile
		is chosen, gives way to a new game's map list, as Server Setup's
		START does: pc_menu_functions_screen) */
		menu_functions.create_pending = TRUE;
		menu_functions.create_system_link = TRUE;
		platform_log("menus: h3 mp: host a system link game; the profile, then the map list");
		return TRUE;
	case _function_h3_browse_system_link:
		menu_functions.create_pending = FALSE;
		return TRUE;
	case _function_h3_map_set:
		h3_mp_map_set(widget);
		return TRUE;
	case _function_h3_map_focus:
		h3_focus_child(widget, h3_mp.map_index != NONE ? h3_mp.map_index : 0);
		return TRUE;
	case _function_h3_gametype_set:
		h3_mp_gametype_set(widget);
		return TRUE;
	case _function_h3_gametype_focus:
		h3_mp_gametypes_read();
		browser_view_reset(&h3_mp.gametype_view);
		h3_mp.gametype_cursor = h3_mp.gametype;
		h3_mp.gametype_page = 0;
		return TRUE;
	case _function_h3_gametype_page_up:
		h3_mp.gametype_page = -1;
		return TRUE;
	case _function_h3_gametype_page_down:
		h3_mp.gametype_page = 1;
		return TRUE;
	case _function_h3_setup_hold:
		setup_hold(widget);
		return TRUE;
	case _function_coop_campaign:
	case _function_unwired:
	default:
		return TRUE;
	}
}

void pc_menu_game_data_function_invoke(
	struct widget_instance *widget,
	long function)
{
	switch (function)
	{
	case _input_mp_update_desc:
		if (widget->type == _ui_widget_type_column_list)
			multiplayer_update(widget);
		break;
	case _input_browser_update:
		browser_update(widget);
		break;
	case _input_password_update:
		password_update(widget);
		break;
	case _input_setup_update:
		setup_update(widget);
		break;
	case _input_code_update:
		code_update(widget);
		break;
	case _input_h3_nameplate:
		h3_nameplate_update(widget);
		break;
	case _input_h3_servers:
		h3_servers_update(widget);
		break;
	case _input_h3_campaign:
		h3_campaign_update(widget);
		break;
	case _input_h3_missions:
		h3_pick_update(widget, h3_campaign_unlocked(), "mission_picture", "mission_text");
		break;
	case _input_h3_difficulties:
		h3_pick_update(widget, 4, "difficulty_emblem", "difficulty_text");
		break;
	case _input_h3_mission_title:
		h3_mission_title_update(widget);
		break;
	case _input_forge_lobby:
		h3_forge_lobby_update(widget);
		break;
	case _input_forge_maps:
		h3_forge_maps_update(widget);
		break;
	case _input_forge_browser:
		if (widget->type == _ui_widget_type_column_list)
			h3_forge_browser_update(widget);
		break;
	case _input_forge_title:
		h3_forge_title_update(widget);
		break;
	case _input_forge_controls:
		h3_forge_controls_update(widget);
		break;
	case _input_h3_mp_lobby:
		h3_mp_lobby_update(widget);
		break;
	case _input_h3_networks:
		h3_pick_update(widget, NUMBER_OF_H3_NETWORKS, "network_picture", "network_text");
		break;
	case _input_h3_maps:
		h3_pick_update(widget, H3_MAP_COUNT, "map_picture", "map_text");
		break;
	case _input_h3_gametypes:
		if (widget->type == _ui_widget_type_column_list)
			h3_mp_gametypes_update(widget);
		break;
	case _input_h3_campaign_networks:
		h3_pick_update(widget, NUMBER_OF_H3_CAMPAIGN_NETWORKS, "campnet_picture", "campnet_text");
		break;
	default:
		break;
	}
}

/* (menu_tags_screen) a screen of the game's that opens: Server Setup's START
went to the System Link screen's profile; once chosen, the new game's map
list, its server started (as the System Link list's Y does) */
long pc_menu_functions_screen(
	long tag_index)
{
	if (menu_functions.create_pending && tag_index == tag_loaded(UI_WIDGET_DEFINITION_TAG, server_list_screen))
	{
		long map_select = tag_loaded(UI_WIDGET_DEFINITION_TAG, map_select_screen);
		/* (which screen asked: Create Game's, online, or the Halo 3 lobby's
		START GAME on SYSTEM LINK) */
		char const *asked = menu_functions.create_system_link ? "Multiplayer > System Link" : "Create Game > Internet";

		menu_functions.create_pending = FALSE;
		menu_functions.create_system_link = FALSE;
		if (map_select != NONE && ui_widget_port_start_network_game_server())
		{
			platform_log("menus: %s: the game's server started; the map list", asked);
			return map_select;
		}
		platform_log("menus: %s: the server did not start; the System Link list", asked);
	}
	return tag_index;
}

/* (each frame, the main loop) the screens the functions asked for, opened
outside the widgets' own events */
void pc_menu_functions_update(
	boolean main_menu_loaded)
{
	if (!main_menu_loaded)
	{
		menu_functions.open_system_link = FALSE;
		menu_functions.open_password = FALSE;
		menu_functions.create_pending = FALSE;
		/* (ui.map's scripts start again with it: ring_loop awake) */
		h3_scenes.ring_asleep = FALSE;
		return;
	}
	if (menu_functions.open_password)
	{
		long password_screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, password_screen_name);
		struct widget_instance *active = NULL;
		long index;

		/* (the browser behind it, which B goes back to; its listings kept
while it closes: browser dispose) */
		for (index = 0; index < 4 && !active; index++)
			active = ui_widget_port_active_screen((short)index);
		if (password_screen != NONE && active)
		{
			ui_widget_load_by_name_or_tag(NULL, password_screen, NULL, active->local_player_index,
				active->definition_tag_index, NONE, NONE);
		}
		menu_functions.open_password = FALSE;
	}
	/* (a join's watch goes on from here as the screens change) */
	join_watch();
	if (menu_functions.open_system_link)
	{
		menu_functions.open_system_link = FALSE;
		if (!system_link_shortcut_open())
			platform_log("menus: the System Link screen did not open");
	}
}
