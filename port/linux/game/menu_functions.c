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
- With Menu style Halo 3, the Halo 3 SELECT PROFILE and CUSTOMIZATION
  ("port h3 profile ...", "port h3 custom ...": halo3/profiles.xml,
  customize.xml) stand for the Xbox's profile screens and its profile
  settings, and read and write the profiles with the Xbox screens' own
  functions ("the Halo 3 profile screens" below).
- Its BROWSE GAMES opens the Halo 3 games browser (the LAN browser, "port
  h3 lan ...", halo3/network.xml) in place of the Xbox's System Link list,
  after the same profile screen: the server browser's rows, sorts and pages
  over the games the System Link list would show (the game's own discovery,
  network_game_client_listed_games: each field checked there), A joining
  one as A on its row there joins it (ui_widget_port_join_advertised_game:
  the same checks, the game's lobby next). With the connection Online the
  online list's public games come after them in the same list (the System
  Link ones marked [LAN]): A joins one as the server browser does (its
  password first), and its host's game, once reached and listed, is joined
  from this list (menu_functions_lan_join_listed), with no System Link
  screen. System Link has no passwords: no System Link row is locked.
- The lobbies' NETWORK is the settings panel's Connection: ONLINE, SYSTEM
  LINK and AD HOC (ONLINE CO-OP, SYSTEM LINK CO-OP and AD HOC CO-OP) choose
  it, and it switches live (network_connection.c: internet play started or
  stopped, ad hoc play's group joined or left); a short status line under
  NETWORK says how it goes. AD HOC's rows join the ad hoc room's group first
  when the Vita is not in it ("port h3 adhoc ...").

Text is typed on the Vita's system keyboard (system_link_shortcut.h
halo_text_input_*, vita_settings.c), and on the PC build in its own text
box (port/linux/src/pc_text_entry.c: the keyboard, or an on-screen keyboard
for a gamepad); a test build's HALO_TEST_TEXT_INPUT answers in its place,
"|" between the answers ("!" cancels one), for the automated tests.
*/

#include "cseries.h"
#include "interface/ui_widget.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"

#include "interface/event_manager.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "saved games/playlist_profile.h"
#include "saved games/saved_game_files.h"
#include "tag_files/files.h"
#include "cache/cache_files.h"
#include "halo_menus.h"
#include "../src/lang.h"
#include "../src/p2p.h"
#include "../src/p2p_friends.h"
#include "../src/p2p_party.h"
#include "../src/crossplay_guard.h"
#include "network_wire.h"
#include "network_anticheat.h"
#include "browser_list.h"
#include "../src/system_link_shortcut.h"
#include "forge.h"
#include "forge_blocks.h"
#include "mod_manager.h"
#include "custom_edition_maps.h"
#include "map_checks.h"
#include "../src/port_config.h"
#include "../src/pc_text_entry.h"
#include "camera/observer.h"
#include "../src/forge_store.h"
#include "scene_packs.h"
#include "networking/network_client_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include "network_connection.h"
#include "player_models.h"
#include "game/players.h"
#include "game/game_engine.h"
#include "networking/network_server_manager.h"
#include "halo_port_limits.h"
/* (the desktop's button icons: display.button_icons) */
#if !defined(HALO_VITA) && !defined(HALO_ANDROID)
#define H3_BUTTON_ICONS 1
#include "halo_button_icons.h"
#include <ctype.h>
#endif
#include "input/input.h"
#include "coop_menu.h"
#include "map_share.h"

#include <math.h>
#include <stdarg.h>
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
boolean ui_widget_port_join_advertised_game(struct widget_instance *list, void const *server, boolean *widget_deleted);
boolean transport_network_available(void);
/* menu_tags.c's: a screen of the set by its widget's name (NONE: none) */
long menu_tags_ingame_screen(char const *name);
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
static char const profiles_screen[] =
	"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\4way_profile_select\\4way_start2join_screen";
static char const pregame_screen[] =
	"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen";
/* the Halo 3 LAN browser (halo3/network.xml), and how long it says it is
looking before it says it found none */
static char const lan_screen[] = "h3/lan/screen";
/* (a games browser row of the System Link list's: its id's start) */
#define LAN_ID_PREFIX "lan:"
#define LAN_LOOKING_MILLISECONDS 4000

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
	_function_browser_filter_next,
	_function_browser_filter_previous,
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
	_function_h3_profiles_campaign,
	_function_h3_profiles_customize,
	_function_h3_profile_play,
	_function_h3_profile_edit,
	_function_h3_profile_create,
	_function_h3_profile_delete_ask,
	_function_h3_profile_delete,
	_function_h3_profile_page_up,
	_function_h3_profile_page_down,
	_function_h3_custom_next,
	_function_h3_custom_previous,
	_function_h3_custom_save,
	_function_h3_custom_close,
	_function_forge_map_pick,
	_function_forge_maps_back,
	_function_forge_variant_page_up,
	_function_forge_variant_page_down,
	_function_forge_save,
	_function_fileshare_init,
	_function_fileshare_focus,
	_function_fileshare_page_up,
	_function_fileshare_page_down,
	_function_fileshare_kind,
	_function_fileshare_rename,
	_function_fileshare_ask_delete,
	_function_fileshare_delete,
	_function_fileshare_open,
	_function_fileshare_enter,
	_function_forge_undo,
	_function_forge_redo,
	_function_forge_snap,
	_function_forge_property_next,
	_function_forge_property_previous,
	_function_forge_close,
	_function_forge_pack_toggle,
	_function_h3_mods_init,
	_function_h3_mods_toggle,
	_function_h3_mods_details,
	_function_h3_mods_delete_ask,
	_function_h3_mods_delete,
	_function_h3_mods_page_up,
	_function_h3_mods_page_down,
	_function_h3_lan_init,
	_function_h3_lan_dispose,
	_function_h3_lan_refresh,
	_function_h3_adhoc_host,
	_function_h3_adhoc_browse,
	_function_h3_adhoc_coop,
	_function_h3_adhoc_join,
	_function_h3_adhoc_cancel,
	_function_forge_new_round,
	_function_forge_all_ai,
	_function_h3_map_page_up,
	_function_h3_map_page_down,
	_function_h3_profiles_close,
	_function_h3_friends_init,
	_function_h3_friends_act,
	_function_h3_friends_second,
	_function_h3_friends_options_ask,
	_function_h3_friends_option,
	_function_h3_friends_page_up,
	_function_h3_friends_page_down,
	_function_browser_profile_first,
	_function_h3_players_init,
	_function_h3_players_act,
	_function_h3_players_option,
	_function_h3_players_page_up,
	_function_h3_players_page_down,
	_function_h3_join_system_link,
	_function_h3_join_split_screen,
	_function_h3_join_a,
	_function_h3_join_previous,
	_function_h3_join_next,
	_function_h3_join_leave,
	_function_h3_pregame_init,
	_function_h3_party_init,
	_function_h3_party_act,
	_function_h3_party_second,
	_function_h3_party_option,
	_function_h3_party_page_up,
	_function_h3_party_page_down,
	_function_h3_party_type,
	_function_h3_party_preset,
	_function_forge_clear_dead_ai,
	_function_forge_clear_all_ai,
	_function_h3_mp_pause_party,
	_function_h3_mp_settings_init,
	_function_h3_mp_settings_save,
	_function_h3_mp_settings_close,
	_function_h3_host_online,
	_function_h3_game_options,
	_function_h3_live_leave,
	_function_h3_invite_ask,
	_function_h3_invite_init,
	_function_h3_invite_act,
	_function_forge_start_live,
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
	_input_h3_profiles,
	_input_h3_profile_delete_text,
	_input_h3_custom,
	_input_fileshare,
	_input_fileshare_question,
	_input_forge_properties,
	_input_forge_packs,
	_input_h3_mods,
	_input_h3_mods_details,
	_input_h3_mods_confirm,
	_input_h3_adhoc_joining,
	_input_h3_network_status,
	_input_h3_friends,
	_input_h3_friends_options,
	_input_h3_friends_panel,
	_input_h3_players,
	_input_h3_players_options,
	_input_h3_join,
	_input_h3_pregame,
	_input_h3_party,
	_input_h3_party_options,
	_input_h3_party_chat,
	_input_h3_party_quick,
	_input_h3_mp_pause,
	_input_h3_mp_settings,
	_input_h3_invite,
	_input_h3_party_plates,
};

/* what a line of typing is for */
enum
{
	_typing_none,
	_typing_password,
	_typing_lobby_name,
	_typing_lobby_password,
	_typing_code,
	_typing_profile_new,
	_typing_profile_name,
	_typing_forge_name,
	_typing_fileshare_rename,
	_typing_friend_code,
	_typing_party_chat,
	/* (a friend's game with a password: typed, then joined) */
	_typing_friend_password,
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
	chosen (its id stays its own as the list changes). The Vitas' games
	first, then the PCs', a header row before each group (browser_list_group:
	two more than the games) */
	struct p2p_lobby_entry games[P2P_LOBBY_MAXIMUM_ENTRIES + 2];
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
	/* (the settings panel's network Forge game: menu_functions_forge_host)
	the game created a Forge game of h3_forge's map, its lobby next */
	boolean create_forge;
	/* (the Halo 3 Create Game: its GAME TYPE or MAP picker opened, port h3
	setup hold) what the screen showed, which it shows again when it is back
	(setup_initialize) rather than the settings' */
	boolean setup_held;
	short held_maximum;
	boolean held_private;
	/* (WHO CAN JOIN: NETWORK_AUDIENCE_*) */
	short held_audience;
	/* to be opened from the main loop: the System Link screen, the password
	screen */
	boolean open_system_link;
	/* (FRIENDS' PARTY row) the PARTY screen opened at the next frame */
	boolean open_party;
	/* (the Halo 3 lobby's Square on ONLINE) GAME OPTIONS opened at the next
	frame, over the lobby */
	boolean open_game_options;
	/* (a game hosted from the Halo 3 lobby) SELECT PROFILE was shown on
	the way (profile_shown): the game's lobby takes it out of the history,
	so that B goes back to the multiplayer lobby (pregame_drop_profile) */
	boolean profile_shown;
	boolean pregame_drop_profile;
	/* (the party's leader) the game whose open slots were said too few:
	pressed again soon, joined */
	char party_warned_id[P2P_LOBBY_ID_SIZE];
	unsigned long party_warned_time;
	boolean open_password;
	/* (crossplay_guard.h) a large PC game chosen: the player asked whether
	to join it anyway, the answer awaited */
	boolean large_asked;
	struct p2p_lobby_entry large_entry;
	/* the Halo 3 lobby's BROWSE GAMES on SYSTEM LINK or AD HOC: the System
	Link list, once the profile is chosen, gives way to the Halo 3 LAN
	browser; lan: the browser's rows are the System Link list's games (the
	LAN browser), since when it looks (lan_since), its list as last logged */
	boolean browse_lan_pending;
	/* (a join refused by the map share, from the Halo 3 games browser:
	when it was asked, to open the browser again over the main menu the
	refusal left at; 0 none) */
	unsigned long browser_after_refusal;
	/* (Menu style Halo CE) SERVER BROWSER: the profile screen first, then
	the server browser in place of the System Link list it leads to
	(browser_profile_pending); a game joined from it, the profile chosen,
	goes on to the System Link list at once (browser_profile_chosen,
	open_system_link_list), not to the profile again */
	boolean browser_profile_pending;
	boolean browser_profile_chosen;
	boolean open_system_link_list;
	boolean lan;
	/* (Online: the public games listed after the System Link list's) */
	long lan_online_count;
	unsigned long lan_since;
	char lan_said[192];
	/* (when it was last logged: browser_log_due) */
	unsigned long lan_said_at;
} menu_functions;

/* ---------- private code */

/* (the party's plates, h3_party_plates_update) the players the lobbies' and
the main menu's count says: this machine and its party's members shown
(1 out of a party), and that count's words: "1 Player", "3 Players",
"3 Players (16 max)" with a maximum (0: none) - Halo 3's count over the
nameplates */
static int h3_party_players = 1;

static void h3_party_players_text(char *text, int size, int maximum)
{
	if (maximum > 0)
		snprintf(text, (size_t)size, h3_party_players == 1 ? T("1 Player (%d max)") : T("%d Players (%d max)"),
			h3_party_players == 1 ? maximum : h3_party_players, maximum);
	else if (h3_party_players == 1)
		snprintf(text, (size_t)size, "%s", T("1 Player"));
	else
		snprintf(text, (size_t)size, T("%d Players"), h3_party_players);
}

/* (the Halo 3 multiplayer flows: a profile chosen this run is not asked
for again) */
static void h3_session_profile_chosen(short plate, long profile_index);
static void h3_session_forget(void);
/* (the live lobby, Menu style Halo 3: h3_live) */
static void h3_live_lobby_show(struct widget_instance *screen);
static boolean h3_live_follow(void);
static void h3_live_close(char const *why);
static boolean h3_live_start(void);
static boolean h3_live_follow_wire(boolean reopen);
static boolean h3_live_keep(void);
static void h3_live_retry(void);
static void h3_live_public_chosen(void);
/* (the Forge lobby, halo3/forge_lobby.xml: its live game, h3_live's) */
static void h3_live_forge_follow(void);
static boolean h3_live_forge_start(void);
static boolean h3_live_forge_live(void);

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

/* (the Halo 3 menus' nameplate, port h3 nameplate: the main menu's, the
lobbies', the pause screen's) the profile it shows: the first local
player's active one; with none active yet (the main menu after a start),
the one Customize saved or Select Profile chose this session, else the one
last used (the game's z:\lastprof.txt), else the first there is. Its name
and primary colour are read once and kept (reading a profile is a file's
read), read again when the profile shown changes, when Customize saves
(h3_nameplate_changed) and every few seconds */
#define H3_NAMEPLATE_READ_MILLISECONDS 4000
/* (the profiles looked through for the first: the profile screens' most) */
#define H3_NAMEPLATE_PROFILES 100

static struct
{
	/* the profile Customize saved or Select Profile chose (NONE) */
	long chosen;
	/* what was read: of which profile (NONE: none could be), with the
	active one then, when; its name (UTF-8) and colour (NONE: none) */
	boolean read;
	boolean changed;
	long shown;
	long active;
	unsigned long read_at;
	char name[64];
	short color;
	char said[96];
} h3_plate = { NONE, FALSE, FALSE, NONE, NONE, 0, "", NONE, "" };

/* a profile's name as UTF-8 */
static void h3_profile_utf8(char *name, int size, struct player_profile const *profile)
{
	int length = 0, index;

	for (index = 0; index < MAXIMUM_PLAYER_PROFILE_NAME_LENGTH && profile->player_name[index] && length < size - 4; index++)
	{
		unsigned long code = (unsigned short)profile->player_name[index];

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

/* (Customize saved a profile, Select Profile chose one) the nameplate shows
it, read again at once */
static void h3_nameplate_changed(long profile_index)
{
	if (profile_index != NONE)
		h3_plate.chosen = profile_index;
	h3_plate.changed = TRUE;
}

static void h3_nameplate_read(void)
{
	long active = player_ui_get_active_player_profile_index(0);
	unsigned long now = system_milliseconds();
	long candidates[4];
	short index;

	/* (not again on the clock while a map copy is under way: the profiles'
	reads, ~30 file calls of the saved games' map file on the game thread,
	each waited on the Vita for the memory card behind the copy; a profile
	chosen or saved is read again at once) */
	if (h3_plate.read && !h3_plate.changed && active == h3_plate.active &&
		(now - h3_plate.read_at < H3_NAMEPLATE_READ_MILLISECONDS || cache_files_precache_in_progress()))
	{
		return;
	}
	h3_plate.read = TRUE;
	h3_plate.changed = FALSE;
	h3_plate.active = active;
	h3_plate.read_at = now;
	h3_plate.shown = NONE;
	h3_plate.name[0] = 0;
	h3_plate.color = NONE;
	candidates[0] = active;
	candidates[1] = h3_plate.chosen;
	candidates[2] = active == NONE && h3_plate.chosen == NONE ? player_ui_get_player1_last_used_profile_index() : NONE;
	candidates[3] = NONE;
	if (candidates[0] == NONE && candidates[1] == NONE && candidates[2] == NONE)
	{
		long first[H3_NAMEPLATE_PROFILES];
		word count = H3_NAMEPLATE_PROFILES;

		player_profiles_enumerate_available_to_local_player_index(0, &count, first, FALSE);
		if (count > 0)
			candidates[3] = first[0];
	}
	for (index = 0; index < (short)NUMBEROF(candidates); index++)
	{
		struct player_profile profile;

		if (candidates[index] == NONE || !player_profile_get(candidates[index], &profile))
			continue;
		h3_profile_utf8(h3_plate.name, (int)sizeof(h3_plate.name), &profile);
		if (!h3_plate.name[0])
			continue;
		h3_plate.shown = candidates[index];
		h3_plate.color = profile.primary_color_index;
		break;
	}
}

/* the name the nameplate shows: its profile's; with none (a first start),
the player's name (network.player_name: on the Vita its user name), else
"Player". Not the game's "get active plyr profile name", which asserts
without a profile */
static void h3_player_name(char *name, int size)
{
	h3_nameplate_read();
	snprintf(name, (size_t)size, "%s", h3_plate.name);
	if (!name[0])
		snprintf(name, (size_t)size, "%s", getenv("HALO_NET_PLAYER_NAME") && getenv("HALO_NET_PLAYER_NAME")[0] ?
			getenv("HALO_NET_PLAYER_NAME") : "Player");
}

static void h3_nameplate_update(struct widget_instance *widget)
{
	char name[64];
	char said[96];
	short frame;

	h3_player_name(name, sizeof(name));
	/* (the bar's frame: 0 Halo 3's green for none, then the game's colours
	in its order, as the swatches) */
	frame = (short)(h3_plate.color < 0 || h3_plate.color >= 18 ? 0 : h3_plate.color + 1);
	text_show(widget, name);
	if (widget)
		widget->animation.current_frame_index = frame;
	snprintf(said, sizeof(said), "%s, colour %d (frame %d)%s", name, h3_plate.color, frame,
		h3_plate.shown == NONE ? ", no profile" : h3_plate.shown == h3_plate.active ? ", the active profile" : "");
	if (strcmp(said, h3_plate.said))
	{
		snprintf(h3_plate.said, sizeof(h3_plate.said), "%s", said);
		platform_log("menus: nameplate: %s", said);
	}
}

/* (the Halo 3 main menu's live servers, port h3 servers) up to three rows
(server_1 to server_3) of the listed games with the most players, "name -
map" and "players/max" (server_N_name, server_N_count); the rows and the ONLINE NOW label (servers_label) hidden
when there are none (internet play off, no games). Display only: ONLINE
opens the browser to join */
static void browser_test_games(void);

/* whether a list's change may be logged now: the first at once, then at
most one each BROWSER_LOG_MILLISECONDS (the list as it is then) */
#define BROWSER_LOG_MILLISECONDS 5000UL

static boolean browser_log_due(unsigned long *logged_at)
{
	unsigned long now = system_milliseconds();

	if (*logged_at && now - *logged_at < BROWSER_LOG_MILLISECONDS)
		return FALSE;
	*logged_at = now ? now : 1;
	return TRUE;
}

static void h3_servers_update(struct widget_instance *widget)
{
	static char said[256];
	static unsigned long said_at;
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
		if (strcmp(summary, said) && browser_log_due(&said_at))
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

/* ---- ad hoc play in the Halo 3 lobbies (the multiplayer lobby's AD HOC,
the campaign lobby's AD HOC CO-OP). Choosing it switches the connection to
Ad hoc (network_connection.c), which asks the system's dialog to join the
ad hoc room's group. AD HOC's START GAME and BROWSE GAMES, and AD HOC
CO-OP's START GAME, first join the group when the Vita is not in it, as the
panel's Host and Join do (h3/adhoc/joining), then go on as they would have:
the profile and the map list, the profile and the LAN browser, or co-op
hosted and the game's lobby. */

static char const *setting_get(char const *variable, char const *otherwise);

enum
{
	_h3_adhoc_then_none,
	_h3_adhoc_then_host,
	_h3_adhoc_then_browse,
	_h3_adhoc_then_coop,
};

static struct
{
	/* the joining dialog: what follows the group joined, whether the join
	was asked, and that next step, to be opened from the main loop
	(open_then); co-op not hosted then (coop_failed) */
	short then;
	boolean join_asked;
	boolean open_then;
	boolean coop_failed;
	char said[160];
} h3_adhoc;

/* (port h3 adhoc host, browse, coop: the first handler of AD HOC's START
GAME and BROWSE GAMES, of AD HOC CO-OP's START GAME) in the group, TRUE:
the row's next handler goes on (START GAME's host and BROWSE GAMES's LAN
browser set up here, as SYSTEM LINK's are); else FALSE, and the joining
dialog opens, which joins it and then goes on itself */
static boolean h3_adhoc_ready(short then)
{
	if (__atomic_load_n(&halo_adhoc_group, __ATOMIC_ACQUIRE) == HALO_ADHOC_GROUP_IN)
	{
		if (then == _h3_adhoc_then_host && h3_live_start())
			return TRUE;
		if (then == _h3_adhoc_then_host)
		{
			menu_functions.create_pending = TRUE;
			menu_functions.create_system_link = TRUE;
			platform_log("menus: h3 mp: host an ad hoc game; the profile, then the map list");
		}
		else if (then == _h3_adhoc_then_browse)
		{
			h3_live_close("the host browses the games");
			menu_functions.create_pending = FALSE;
			menu_functions.browse_lan_pending = TRUE;
			platform_log("menus: h3 mp: browse the ad hoc games; the profile, then the LAN browser");
		}
		return TRUE;
	}
	h3_adhoc.then = then;
	h3_adhoc.join_asked = FALSE;
	h3_adhoc.open_then = FALSE;
	h3_adhoc.coop_failed = FALSE;
	platform_log("menus: h3 adhoc: not in the ad hoc group: the joining dialog first");
	return FALSE;
}

/* (port h3 adhoc join: the joining dialog opened, or its A) the group
joined through the system's dialog (the Vita's host: system_link_shortcut.h) */
static void h3_adhoc_join(void)
{
	int group = __atomic_load_n(&halo_adhoc_group, __ATOMIC_ACQUIRE);

	if (group == HALO_ADHOC_GROUP_IN || group == HALO_ADHOC_GROUP_JOINING || group == HALO_ADHOC_GROUP_OFF ||
		__atomic_load_n(&halo_adhoc_request, __ATOMIC_ACQUIRE) != HALO_ADHOC_REQUEST_NONE)
	{
		return;
	}
	h3_adhoc.join_asked = TRUE;
	h3_adhoc.coop_failed = FALSE;
	__atomic_store_n(&halo_adhoc_request, HALO_ADHOC_REQUEST_JOIN, __ATOMIC_RELEASE);
	platform_log("menus: h3 adhoc: the system's dialog asked for (ad hoc room %s)", setting_get("HALO_ADHOC_ROOM", "1"));
}

/* (port h3 adhoc joining: the joining dialog's text, each frame) how the
join goes; joined, the step that follows is opened from the main loop */
static void h3_adhoc_joining_update(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	int group = __atomic_load_n(&halo_adhoc_group, __ATOMIC_ACQUIRE);
	boolean asking = h3_adhoc.join_asked &&
		__atomic_load_n(&halo_adhoc_request, __ATOMIC_ACQUIRE) == HALO_ADHOC_REQUEST_JOIN;
	char text[256];
	boolean failed = FALSE;

	if (h3_adhoc.coop_failed)
		snprintf(text, sizeof(text), "%s", T("In the ad hoc group, but co-op was not\n"
			"hosted. Go back and try START GAME again."));
	else if (asking || group == HALO_ADHOC_GROUP_JOINING || (group == HALO_ADHOC_GROUP_NONE && h3_adhoc.join_asked) ||
		(group == HALO_ADHOC_GROUP_OFF && network_connection_wanted() == NETWORK_CONNECTION_ADHOC))
	{
		snprintf(text, sizeof(text), T("The system's dialog joins the PS Vitas\nnear you, in ad hoc room %s."),
			setting_get("HALO_ADHOC_ROOM", "1"));
	}
	else if (group == HALO_ADHOC_GROUP_IN)
	{
		snprintf(text, sizeof(text), "%s", T("In the ad hoc group."));
		if (h3_adhoc.then != _h3_adhoc_then_none && !h3_adhoc.open_then)
			h3_adhoc.open_then = TRUE;
	}
	else if (group == HALO_ADHOC_GROUP_OFF)
		snprintf(text, sizeof(text), "%s", T("Ad hoc play needs the settings panel's\nConnection: Ad hoc."));
	else
	{
		failed = TRUE;
		snprintf(text, sizeof(text), "%s", T("The PS Vita did not join the ad hoc group.\n"
			"Press A to try again, or choose another\nAd hoc dialog way (settings panel, Dev tab)."));
	}
	text_show(named(screen, "adhoc_text"), text);
	/* (A, Try again: after a failure only) */
	if (named(screen, "again"))
		widget_instance_set_visibility_recursive(named(screen, "again"), failed);
	if (named(screen, "a_butn"))
	{
		widget_instance_set_visibility_recursive(named(screen, "a_butn"), failed);
		named(screen, "a_butn")->disabled = !failed;
	}
	/* (said when it changes: the tests read it) */
	if (strcmp(text, h3_adhoc.said))
	{
		char line[256];
		char *newline;

		snprintf(h3_adhoc.said, sizeof(h3_adhoc.said), "%s", text);
		snprintf(line, sizeof(line), "%s", text);
		while ((newline = strchr(line, '\n')) != NULL)
			*newline = ' ';
		platform_log("menus: h3 adhoc: the joining dialog says \"%s\"", line);
	}
}

/* the lobby's NETWORK: LOCAL (this Vita alone: the level started as the
Xbox's difficulty list's A starts it), or co-op hosted as the old Campaign
screen's Y on the difficulty hosts it (coop_menu.c): ONLINE CO-OP over
internet play (its code shown in the waiting screen), SYSTEM LINK CO-OP for
the Vitas on the same Wi-Fi, AD HOC CO-OP for the Vitas near (its group
joined first). The co-op choices are the connection (the settings panel's
Connection, network_connection.c): choosing one switches it, and the lobby
shows the panel's choice as its own. */
enum
{
	_h3_campaign_local,
	_h3_campaign_online,
	_h3_campaign_system_link,
	_h3_campaign_adhoc,
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
	difficulty, read as the lobby opens (as the Xbox's level list does);
	damaged: there and not readable (the Xbox's level list says "Unable to
	load saved game file") */
	boolean saved;
	boolean damaged;
	char saved_map[256];
	short saved_difficulty;
	/* the profile the lobby was opened for: another one's starts again on
	its last level */
	long profile_index;
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
		return T("SYSTEM LINK CO-OP");
	if (network == _h3_campaign_adhoc)
		return T("AD HOC CO-OP");
	return T("LOCAL");
}

/* (port h3 campaign network set) the network picker's choice: a co-op one
the connection too */
static boolean h3_campaign_network_set(struct widget_instance *widget);

/* the co-op network of the connection chosen */
static short h3_campaign_network_of_connection(void)
{
	short connection = network_connection_wanted();

	return connection == NETWORK_CONNECTION_ONLINE ? _h3_campaign_online : connection == NETWORK_CONNECTION_ADHOC ?
		_h3_campaign_adhoc : _h3_campaign_system_link;
}

/* (the settings panel's Host co-op campaign, system_link_shortcut.c: the
Campaign screen opened for co-op) the lobby opens on co-op, over the
connection */
void menu_functions_h3_campaign_coop(void)
{
	h3_campaign.network = h3_campaign_network_of_connection();
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

	if (!h3_campaign.initialized || h3_campaign.profile_index != player_ui_get_active_player_profile_index(0))
	{
		h3_campaign.initialized = TRUE;
		h3_campaign.profile_index = player_ui_get_active_player_profile_index(0);
		h3_campaign.difficulty = 1;
		h3_campaign.level = 0;
		if (h3_profile(&profile))
			h3_campaign.level = profile.last_single_player_map_played;
	}
	h3_campaign.level = (short)PIN(h3_campaign.level, 0, unlocked - 1);
	/* (co-op: over the connection, which the settings panel may have
	changed since) */
	if (h3_campaign.network != _h3_campaign_local)
		h3_campaign.network = h3_campaign_network_of_connection();
	{
		boolean corrupted = FALSE;

		h3_campaign.saved_map[0] = 0;
		h3_campaign.saved = game_state_test_persistent_storage(h3_campaign.saved_map, &h3_campaign.saved_difficulty,
			&corrupted) && !corrupted;
		h3_campaign.damaged = corrupted;
		if (corrupted)
			platform_log("menus: h3 campaign: the saved game is damaged: it can't be loaded");
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
/* (port h3 missions) the focused mission's level file missing or one that
cannot be played: why, in its description's place (mission_why) */
static void h3_missions_why(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *found;
	short index = 0, focused = NONE;
	char why[160];
	boolean playable;

	if (list->type != _ui_widget_type_column_list)
		return;
	for (child = list->child; child; child = child->next, index++)
	{
		if (child == list->focused_child)
			focused = index;
	}
	if (focused < 0 || focused >= NUMBER_OF_SINGLE_PLAYER_LEVELS)
		return;
	playable = map_check(h3_level_paths[focused], why, sizeof(why)) == _map_check_playable;
	if ((found = named(screen, "mission_text")) != NULL)
		widget_instance_set_visibility_recursive(found, playable);
	if ((found = named(screen, "mission_why")) != NULL)
	{
		widget_instance_set_visibility_recursive(found, !playable);
		text_show(found, playable ? "" : why);
	}
	/* (a mission that cannot be played said once as it is focused: the
	tests read it) */
	{
		static short said = NONE;

		if (!playable && said != focused)
			platform_log("menus: h3 missions: %s: can't be played: %s", h3_level_names[focused], why);
		said = playable ? NONE : focused;
	}
}

static void h3_campaign_update(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	struct widget_instance *found;
	char text[160];

	if (!h3_campaign.initialized)
		h3_campaign_initialize();
	/* (co-op follows the connection: the settings panel's change shows) */
	if (h3_campaign.network != _h3_campaign_local)
		h3_campaign.network = h3_campaign_network_of_connection();
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
		boolean shown = h3_campaign.network == _h3_campaign_online || h3_campaign.network == _h3_campaign_system_link;

		widget_instance_set_visibility_recursive(found, shown);
		found->disabled = !shown;
	}
	/* (AD HOC CO-OP's: its group joined first) */
	if ((found = named(screen, "start_adhoc")) != NULL)
	{
		widget_instance_set_visibility_recursive(found, h3_campaign.network == _h3_campaign_adhoc);
		found->disabled = h3_campaign.network != _h3_campaign_adhoc;
	}
	if (h3_campaign.network == _h3_campaign_online)
		snprintf(text, sizeof(text), "%s", T("Co-op online: joined by your code."));
	else if (h3_campaign.network == _h3_campaign_system_link)
		snprintf(text, sizeof(text), "%s", T("Co-op with the PS Vitas on your Wi-Fi."));
	else if (h3_campaign.network == _h3_campaign_adhoc)
		snprintf(text, sizeof(text), "%s", T("Co-op with the PS Vitas near you."));
	else
		snprintf(text, sizeof(text), "%s", T("This party is local to this PS Vita."));
	/* (the mission's level cannot be played: why; START GAME then says it
	again as the map fails to load, back at the main menu) */
	{
		char why[160];

		if (map_check(h3_level_paths[h3_campaign.level], why, sizeof(why)) != _map_check_playable)
			snprintf(text, sizeof(text), "%s", why);
	}
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
			/* (a damaged saved game said, as the Xbox's level list says it:
			the missions then start at their start) */
			widget_instance_set_visibility_recursive(warning, (same_level && !resumes) || (h3_campaign.damaged && !coop));
			text_show(warning, h3_campaign.damaged ?
				T("Your saved game is damaged and can't be\nloaded: missions start at their start.") :
				T("This difficulty starts the mission over\nand replaces your saved game."));
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
static void h3_pick_update(struct widget_instance *list, long count, long hidden, char const *picture,
	char const *description)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *found;
	long index = 0, focused = 0, row = 0;
	/* (the rows shown one under another: one left out, the next take its
	place, from the first two's spacing) */
	short top = list->child ? list->child->vertical_offset : 0;
	short step = list->child && list->child->next ? list->child->next->vertical_offset - top : 0;

	for (child = list->child; child; child = child->next, index++)
	{
		boolean shown = index < count && index != hidden;

		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		if (hidden != NONE && shown && step > 0)
			child->vertical_offset = (short)(top + step * row);
		row += shown;
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

	/* (a co-op network is the connection: switched live) */
	if (network != _h3_campaign_local)
	{
		network_connection_choose(network == _h3_campaign_online ? NETWORK_CONNECTION_ONLINE :
			network == _h3_campaign_adhoc ? NETWORK_CONNECTION_ADHOC : NETWORK_CONNECTION_WIFI);
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
		h3_campaign.network == _h3_campaign_online ? "online" : h3_campaign.network == _h3_campaign_adhoc ? "ad hoc" :
		"system link");
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
widgets' update. Only with Menu style Halo 3, at the main menu (a network
game's lobby there too: the live lobby, the game's lobby), unless
menus.scenes (HALO_MENU_SCENES=0) is false. (After the spike
on spike/menu-scenes, Oct 18 2026.) */

boolean main_request_host_command_from(char const *command, char const *source);
int halo_main_menu_idle(void);
int halo_main_menu_loaded(void);

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
	/* its field of view (degrees; 0: the game's own, 70) */
	real field_of_view;
	char screen[64];
	/* the characters standing in it (scene_object=: menu_scene_objects.c) */
	char objects[1024];
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

/* scene="<point> [x y z yaw pitch [ticks [fov]]]": the point's name
(letters, digits, '_'), and the camera written into it, in range (finite,
the world's +-5000), if given, with its field of view (degrees, 10 to 120;
none or 0: the game's own) */
static boolean h3_scene_parse(char const *scene)
{
	char word[32];
	float values[7];
	int used = 0, count;
	short index;

	h3_scenes.has_camera = FALSE;
	h3_scenes.ticks = H3_SCENE_TICKS;
	h3_scenes.field_of_view = 0.f;
	if (sscanf(scene, "%31s%n", word, &used) != 1 ||
		strspn(word, "abcdefghijklmnopqrstuvwxyz0123456789_") != strlen(word))
	{
		return FALSE;
	}
	snprintf(h3_scenes.point, sizeof(h3_scenes.point), "%s", word);
	h3_scenes.ring = !strcmp(word, "ring");
	count = sscanf(scene + used, "%f %f %f %f %f %f %f", &values[0], &values[1], &values[2], &values[3], &values[4],
		&values[5], &values[6]);
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
	if (count >= 6)
		h3_scenes.ticks = PIN((long)values[5], 0, 600);
	if (count == 7)
	{
		if (values[6] != 0.f && !(values[6] >= 10.f && values[6] <= 120.f))
			return FALSE;
		h3_scenes.field_of_view = values[6];
	}
	return TRUE;
}

/* (port h3 scene, a screen's "created") the screen's scene asked for */
static void h3_scene_request(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	char name[64];
	char const *scene;

	/* (a network game's lobby at the main menu too: the live lobby's, the
	game's lobby) */
	if (!h3_scenes_enabled() || !halo_menu_style_halo3() || !halo_main_menu_loaded())
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
/* the lobbies' scenes' packs (every scene_object= of the menus: its
entries' packs and their weapons' - menu_campaign, menu_multiplayer,
menu_monitor), the profile card's Spartan (menu_spartan) and the first
profile's player model's, built in the background once the main menu has
rested a while (scene_packs_prebuild: a prebuild's job, paced, below the
game's threads, paused while the game reads, copies a map or plays on the
network, made way for by any pack wanted now), each loaded once built, the
card's first (SELECT PROFILE shows its Spartan at once, a lobby its scene
as soon as its textures are read: 2.6 MB of the tag cache for the four,
the room a campaign map's build needs left). Once a map's load (ui.map's
each time). HALO_MENU_PREBUILD=0, the scenes off, Menu style Halo CE:
none */
#define H3_PREBUILD_REST_US 2000000ull
#define H3_PREBUILD_MOST 16

static struct
{
	unsigned long epoch;
	unsigned long long rest_since;
	boolean asked;
	/* the packs asked for (the card's first), and which are loaded (or
	given up) */
	char names[H3_PREBUILD_MOST][48];
	short count;
	boolean preloaded[H3_PREBUILD_MOST];
} h3_prebuild;

/* a pack's name not yet in names (count of them): added */
static void h3_prebuild_add(char names[][48], short *count, char const *name, long length)
{
	short index;

	if (length <= 0 || length >= 48 || *count >= H3_PREBUILD_MOST)
		return;
	for (index = 0; index < *count; index++)
	{
		if ((long)strlen(names[index]) == length && !strncmp(names[index], name, (size_t)length))
			return;
	}
	memcpy(names[*count], name, (size_t)length);
	names[*count][length] = 0;
	(*count)++;
}

static void h3_prebuild_update(void)
{
	unsigned long scene_packs_map_epoch(void);
	char const *setting = getenv("HALO_MENU_PREBUILD");
	unsigned long long now = vita_host_time_us();

	if ((setting && *setting && (!strcmp(setting, "0") || !_stricmp(setting, "false"))) || !h3_scenes_enabled() ||
		!halo_menu_style_halo3() || !halo_main_menu_idle() || game_connection() != _game_connection_local)
	{
		h3_prebuild.rest_since = 0;
		return;
	}
	if (h3_prebuild.epoch != scene_packs_map_epoch())
	{
		memset(&h3_prebuild, 0, sizeof(h3_prebuild));
		h3_prebuild.epoch = scene_packs_map_epoch();
	}
	if (!h3_prebuild.rest_since)
		h3_prebuild.rest_since = now;
	if (now - h3_prebuild.rest_since < H3_PREBUILD_REST_US)
		return;
	if (!h3_prebuild.asked)
	{
		struct halo_menus const *menus = halo_menus_load();
		char (*names)[48] = h3_prebuild.names;
		char said[256];
		short count = 0, index;
		int chosen = player_models_panel_chosen();
		char const *model = chosen > 0 ? player_models_pack((short)(chosen - 1)) : NULL;
		long widget;

		h3_prebuild.asked = TRUE;
		/* (the card's Spartan first: SELECT PROFILE is the first stop) */
		if (model)
			h3_prebuild_add(names, &count, model, (long)strlen(model));
		h3_prebuild_add(names, &count, "menu_spartan", (long)strlen("menu_spartan"));
		for (widget = 0; menus && widget < menus->widget_count; widget++)
		{
			char const *cursor = menus->widgets[widget].scene_object;

			while (cursor && *cursor)
			{
				char const *end = strchr(cursor, ';');
				char const *stop = end ? end : cursor + strlen(cursor);
				char const *word = cursor;
				char const *weapon;

				while (word < stop && (*word == ' ' || *word == '\t' || *word == '\r' || *word == '\n'))
					word++;
				h3_prebuild_add(names, &count, word, (long)strcspn(word, ": \t\r\n;"));
				/* (a weapon of another pack: weapon=<pack>:<tag>) */
				for (weapon = word; (weapon = strstr(weapon, "weapon=")) != NULL && weapon < stop; weapon += 7)
				{
					long length = (long)strcspn(weapon + 7, ": \t\r\n;");

					if (weapon[7 + length] == ':')
						h3_prebuild_add(names, &count, weapon + 7, length);
				}
				cursor = end ? end + 1 : NULL;
			}
		}
		h3_prebuild.count = count;
		said[0] = 0;
		for (index = 0; index < count; index++)
		{
			short state = scene_packs_prebuild(names[index]);

			snprintf(said + strlen(said), sizeof(said) - strlen(said), "%s%s %s", index ? ", " : "", names[index],
				scene_packs_state_name(state));
		}
		platform_log("menus: h3 prebuild: the main menu at rest: %s", said);
		return;
	}
	/* (each loaded once built, in the order asked: the card's first) */
	{
		short index;

		for (index = 0; index < h3_prebuild.count; index++)
		{
			short state;

			if (h3_prebuild.preloaded[index])
				continue;
			state = scene_packs_status(h3_prebuild.names[index], NULL, NULL, 0);
			if (state == _scene_pack_state_ready || state == _scene_pack_state_loaded)
			{
				h3_prebuild.preloaded[index] = TRUE;
				platform_log("menus: h3 prebuild: %s %s", h3_prebuild.names[index],
					scene_packs_state_name(scene_packs_want(h3_prebuild.names[index])));
			}
			else if (state == _scene_pack_state_failed || state == _scene_pack_state_none)
			{
				h3_prebuild.preloaded[index] = TRUE;
			}
		}
	}
}

void menu_functions_scenes_update(void)
{
	struct scenario_cutscene_camera_point *point;
	char command[96];

	h3_prebuild_update();

	/* (Menu style Halo CE chosen, or the scenes turned off, while a lobby's
	camera holds: the ring's flight back, as no Halo 3 main menu will ask) */
	if (h3_scenes.ring_asleep && (!halo_menu_style_halo3() || !h3_scenes_enabled()) && halo_main_menu_loaded())
	{
		main_request_host_command_from("(wake ring_loop)", "menu scene");
		h3_scenes.ring_asleep = FALSE;
		platform_log("menus: h3 scene: the scenes are off now: the ring's own flight");
	}
	if (!__atomic_exchange_n(&h3_scenes.pending, 0, __ATOMIC_ACQUIRE))
		return;
	/* (the main menu gone meanwhile: a level loading, a network game's
	map; a network game's lobby at the main menu keeps its scenes) */
	if (!halo_main_menu_loaded())
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
		/* (camera_set reads it: 0 for the game's own) */
		point->field_of_view = h3_scenes.field_of_view * (real)(3.14159265358979 / 180.0);
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
	{
		char field_of_view[40] = "";

		if (h3_scenes.has_camera && h3_scenes.field_of_view != 0.f)
			snprintf(field_of_view, sizeof(field_of_view), ", field of view %.0f", h3_scenes.field_of_view);
		platform_log("menus: h3 scene: %s: camera point %s at %.2f %.2f %.2f, yaw %.1f pitch %.1f, over %ld ticks%s",
			h3_scenes.screen, h3_scenes.point, point->position.x, point->position.y, point->position.z,
			point->orientation.yaw * (real)(180.0 / 3.14159265358979),
			point->orientation.pitch * (real)(180.0 / 3.14159265358979), h3_scenes.ticks, field_of_view);
	}
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
	/* the lobby's map variant (none: an empty file name), and its
	picker's rows (NONE first), the one chosen there, a trigger's page */
	char variant_file[FORGE_FILE_NAME_SIZE];
	char variant_title[FORGE_FILE_TITLE_SIZE];
	struct browser_view variant_view;
	int variant_chosen;
	int variant_page;
	/* (the map picker) the variants of pick_map shown in its list, the
	focus to be given to the variant chosen at the next update */
	boolean picking_variant;
	short pick_map;
	boolean pick_refocus;
	/* why START FORGE did not start (the lobby's note), until another
	choice */
	char note[FORGE_STORE_WHY_SIZE];
	/* the NETWORK row said last (the tests read it); the lobby's first
	opening this run (MAP focused) */
	char network_said[96];
	boolean focused_once;
	/* the lobby's NETWORK (h3_mp's networks: OFFLINE - LOCAL here, a local
	game - by default, as the lobby always was; set at its first update),
	and whether the network picker was opened from this lobby (the last
	lobby shown) rather than the multiplayer lobby */
	short network;
	boolean network_set;
	boolean lobby_last;
} h3_forge;

static void h3_forge_variant_check(void);
/* (the lobby's NETWORK, its live game's players and status: below, with
the multiplayer lobby's) */
static void h3_forge_lobby_network_update(struct widget_instance *screen);

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
	h3_forge_variant_check();
	h3_forge.note[0] = 0;
	platform_log("menus: forge lobby: %s%s%s", forge_map_name(h3_forge.map), h3_forge.variant_file[0] ? ", map variant " : "",
		h3_forge.variant_title);
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
	/* ("MAP: BLOOD GULCH - MY FORT", Halo 3's: the map and its variant) */
	if (h3_forge.variant_file[0])
		snprintf(text, sizeof(text), "%s: %s - %s", T("MAP"), T(forge_map_name(h3_forge.map)), h3_forge.variant_title);
	else
		snprintf(text, sizeof(text), "%s: %s", T("MAP"), T(forge_map_name(h3_forge.map)));
	h3_capitals(text);
	text_show(named(screen, "map_row"), text);
	/* (EXTRA OBJECTS: how many of the packs it brings; those built in the
	background meanwhile) */
	{
		short pack, on = 0;

		for (pack = 0; pack < forge_pack_count(); pack++)
			on += forge_pack_enabled(pack) ? 1 : 0;
		snprintf(text, sizeof(text), T("%s: %d OF %d"), T("EXTRA OBJECTS"), on, forge_pack_count());
		h3_capitals(text);
		text_show(named(screen, "packs_row"), text);
		/* (the player waits for them here: a map's copy shares the memory
		card with them rather than holding them) */
		scene_packs_prebuilds_wanted_now();
		forge_packs_prebuild();
		/* (the building blocks of the map chosen, one that can be played:
		forge_blocks.c) */
		if (available)
			forge_blocks_prebuild(forge_map_short(h3_forge.map));
	}
	/* (the note: why START FORGE did not start, else a pack being built,
	else the game's rules) */
	{
		static char said_note[192];
		char label[64], line[192], map[64];
		long progress = 0, copied = 0;

		if (h3_forge.note[0])
			snprintf(line, sizeof(line), "%s", h3_forge.note);
		else if (forge_packs_building(label, sizeof(label), &progress))
		{
			snprintf(line, sizeof(line), T("Building %s... %ld%%"), label, progress);
			/* (held for a map's copy to the cache partition: what it waits
			for, and how far that is, not a number standing still) */
			if (scene_packs_builds_held(map, sizeof(map), &copied))
			{
				size_t length = strlen(line);

				snprintf(line + length, sizeof(line) - length, "\n");
				length = strlen(line);
				snprintf(line + length, sizeof(line) - length, T("Waiting for %s to copy... (copy %ld%%)"), T(map), copied);
			}
		}
		else
			snprintf(line, sizeof(line), "%s", T("No score or time limit."));
		text_show(named(screen, "note"), line);
		if (strcmp(line, said_note))
		{
			snprintf(said_note, sizeof(said_note), "%s", line);
			platform_log("menus: forge lobby note at %lu ms: %s", system_milliseconds(), line);
		}
	}
	if (h3_forge.variant_file[0])
		snprintf(text, sizeof(text), T("%s on %s"), h3_forge.variant_title, T(forge_map_name(h3_forge.map)));
	else
		snprintf(text, sizeof(text), T("Edit Objects on %s"), T(forge_map_name(h3_forge.map)));
	text_show(named(screen, "caption"), available ? text : T("No multiplayer map is in the maps folder."));
	if (strcmp(text, h3_forge.said))
	{
		snprintf(h3_forge.said, sizeof(h3_forge.said), "%s", text);
		platform_log("menus: forge lobby caption: %s", text);
	}
	if ((found = named(screen, "map_picture")) != NULL)
		found->animation.current_frame_index = h3_forge.map;
	h3_forge_lobby_network_update(screen);
}

/* the map picker: the maps of the maps folder shown, each its name, the
picture and the description following the focused one */
static void h3_forge_variants_update(struct widget_instance *list);

static void h3_forge_maps_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *found;
	short index = 0, focused = 0;
	char title[96];

	/* (a map chosen: its variants in the list, Halo 3's way) */
	if (h3_forge.picking_variant)
	{
		h3_forge_variants_update(list);
		return;
	}
	snprintf(title, sizeof(title), "%s", T("SELECT MAP"));
	text_show(named(screen, "title"), title);
	if ((found = named(screen, "maps_text")) != NULL)
		widget_instance_set_visibility_recursive(found, TRUE);
	if ((found = named(screen, "variant_name")) != NULL)
		widget_instance_set_visibility_recursive(found, FALSE);
	if ((found = named(screen, "variant_text")) != NULL)
		widget_instance_set_visibility_recursive(found, FALSE);
	for (child = list->child; child; child = child->next, index++)
	{
		/* (a map whose file is there but cannot be played - cut short,
		damaged -: shown, marked, its reason in the description's place, and
		not chosen, h3_forge_map_pick) */
		short state = forge_map_state(index, NULL, 0);
		boolean shown = state != _map_check_missing;
		char text[96];

		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		snprintf(text, sizeof(text), "%s", T(forge_map_name(index)));
		h3_capitals(text);
		if (state == _map_check_unusable)
			snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s", T(" (DAMAGED)"));
		text_show(child, text);
		if (child == list->focused_child)
			focused = index;
	}
	if ((found = named(screen, "maps_picture")) != NULL)
		found->animation.current_frame_index = focused;
	{
		char why[160];
		boolean unusable = forge_map_state(focused, why, sizeof(why)) == _map_check_unusable;

		if ((found = named(screen, "maps_text")) != NULL)
			widget_instance_set_visibility_recursive(found, !unusable);
		if ((found = named(screen, "variant_text")) != NULL)
			widget_instance_set_visibility_recursive(found, unusable);
		if (unusable)
			text_show(named(screen, "variant_text"), why);
		else
			string_show(named(screen, "maps_text"), focused);
		/* (the focused map said, once: the tests read it) */
		{
			static char said[192];
			char line[192];

			snprintf(line, sizeof(line), "%s%s%s", forge_map_name(focused), unusable ? ": can't be played: " : "",
				unusable ? why : "");
			if (strcmp(line, said))
			{
				snprintf(said, sizeof(said), "%s", line);
				platform_log("menus: forge maps: %s", line);
			}
		}
	}
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
	short objects, ai, dead_ai;
	long count, index, focused_row = NONE;
	int target;
	char text[192], line[256];

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
				/* (a character's side when none of it is placed: the AI
				category's groups) */
				if (item.placed > 0)
					snprintf(line, sizeof(line), T("%d placed"), item.placed);
				else if (item.group[0])
					snprintf(line, sizeof(line), "%s", item.group);
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
	forge_budget_get(&objects, &ai, &dead_ai);
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
	/* (the dead characters apart: they hold their places until CLEAR DEAD
	AI) */
	if (line[0] && dead_ai > 0)
	{
		size_t length = strlen(line);

		snprintf(line + length, sizeof(line) - length, "\n%s", T("Dead AI come back next round."));
		length = strlen(line);
		if (ai + dead_ai >= FORGE_MAXIMUM_AI)
			snprintf(line + length, sizeof(line) - length, "\n%s", T("Clear dead AI to place more."));
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
static boolean typing_done(short typing, char *text, int size);
static void h3_forge_save(void);

static void h3_forge_title_update(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	char const *scenario = main_get_map_name();
	char text[FORGE_STORE_WHY_SIZE + 64];
	short index;
	char const *name = "";

	/* (SAVE MAP VARIANT's name typed: forge.c saves it at its next update,
	and its words come back here) */
	if (typing_done(_typing_forge_name, text, FORGE_FILE_TITLE_SIZE))
	{
		char author[FORGE_FILE_AUTHOR_SIZE];

		if (text[0])
		{
			h3_player_name(author, sizeof(author));
			forge_save_request(text, author);
			platform_log("menus: forge: save the map variant as \"%s\"", text);
		}
	}
	{
		char status[FORGE_STORE_WHY_SIZE];
		short undo, redo;

		/* (UNDO and REDO with how many steps each has, SNAP's state) */
		forge_undo_available(&undo, &redo);
		snprintf(text, sizeof(text), undo ? "%s (%d)" : "%s", T("UNDO"), undo);
		text_show(named(screen, "undo"), text);
		snprintf(text, sizeof(text), redo ? "%s (%d)" : "%s", T("REDO"), redo);
		text_show(named(screen, "redo"), text);
		snprintf(text, sizeof(text), "%s: %s", T("SNAP"), forge_snap_enabled() ? T("ON") : T("OFF"));
		text_show(named(screen, "snap"), text);
		snprintf(text, sizeof(text), "%s: %s", T("ALL AI"), forge_ai_all_state() == _forge_ai_all_on ? T("ON") : forge_ai_all_state() == _forge_ai_all_off ? T("OFF") : T("MIXED"));
		text_show(named(screen, "allai"), text);
		/* (CLEAR DEAD AI and CLEAR ALL AI with how many each takes) */
		{
			short objects, ai, dead_ai;

			forge_budget_get(&objects, &ai, &dead_ai);
			snprintf(text, sizeof(text), dead_ai ? "%s (%d)" : "%s", T("CLEAR DEAD AI"), dead_ai);
			text_show(named(screen, "cleardead"), text);
			snprintf(text, sizeof(text), ai + dead_ai ? "%s (%d)" : "%s", T("CLEAR ALL AI"), ai + dead_ai);
			text_show(named(screen, "clearall"), text);
		}
		forge_menu_status(status, sizeof(status));
		if (status[0])
			snprintf(text, sizeof(text), "%s", status);
		else if (forge_variant_title()[0])
			snprintf(text, sizeof(text), T("Map variant: %s"), forge_variant_title());
		else
			snprintf(text, sizeof(text), "%s", T("Nothing saved yet."));
		text_show(named(screen, "status"), text);
	}

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

/* the Forge menu's controls: the Vita's buttons; on the desktop the
keyboard's, or the gamepad's the button icons show (display.button_icons:
an Xbox pad's or a PlayStation's, by SDL's names for its buttons) */
static void h3_forge_controls_update(struct widget_instance *widget)
{
#ifdef HALO_VITA
	text_show(widget, T("Fly: left stick. Up: Cross. Down: D-pad down.\nFaster: hold L. Look: right stick.\n"
		"Grab, drop: R. Copy: Circle. Delete: Triangle.\nObjects: Square. Holding one: its properties.\n"
		"Rotate: hold L + right stick; with R: roll.\nL + left stick: distance. L + D-pad right: snap.\n"
		"Upright: D-pad left. Speed: D-pad right.\nSpartan: Select + D-pad up."));
#elif defined(H3_BUTTON_ICONS)
	switch (halo_button_icons_shown())
	{
	case _halo_button_icons_xbox:
		text_show(widget, T("Fly: left stick. Up: A. Down: left stick click.\nFaster: hold LT. Look: right stick.\n"
			"Grab, drop: RT. Copy: B. Delete: Y.\nObjects: X. Holding one: its properties.\n"
			"Rotate: hold LT + right stick; with RT: roll.\nLT + left stick: distance. LT + LB: snap.\n"
			"Upright: RB. Speed: LB.\nSpartan: Back + right stick click."));
		break;
	case _halo_button_icons_playstation:
		text_show(widget, T("Fly: left stick. Up: Cross. Down: L3.\nFaster: hold L2. Look: right stick.\n"
			"Grab, drop: R2. Copy: Circle. Delete: Triangle.\nObjects: Square. Holding one: its properties.\n"
			"Rotate: hold L2 + right stick; with R2: roll.\nL2 + left stick: distance. L2 + L1: snap.\n"
			"Upright: R1. Speed: L1.\nSpartan: Share + R3."));
		break;
	default:
		text_show(widget, T("Fly: W A S D. Up: Space. Down: C.\nFaster: Shift. Look: mouse.\n"
			"Grab, drop: left button. Copy: F. Delete: Del.\nObjects: E. Holding one, E: its properties.\n"
			"Rotate: hold right button + mouse; + left: roll.\nDistance: wheel, or W S. Snap: right button + Q.\n"
			"Upright: X. Speed: Q.\nSpartan: Tab, or F1 + Z."));
		break;
	}
#else
	text_show(widget, T("Fly: W A S D. Up: Space. Down: C.\nFaster: Shift. Look: mouse.\n"
		"Grab, drop: left button. Copy: F. Delete: Del.\nObjects: E. Holding one, E: its properties.\n"
		"Rotate: hold right button + mouse; + left: roll.\nDistance: wheel, or W S. Snap: right button + Q.\n"
		"Upright: X. Speed: Q.\nSpartan: Tab, or F1 + Z."));
#endif
}

#ifndef H3_BUTTON_ICONS
#define h3_buttons(text) (text)
#else
/* a help line's PlayStation buttons (Cross, Circle, Square, Triangle: the
menus' A B X Y, as the Vita names them) named as the button icons show
them on the desktop: the keyboard's keys (Enter, Esc, E, Tab) or the
Xbox pad's letters; a PlayStation pad's stay. Whole words, in the player's
language; the text as it is when nothing changes. Four lines at a time */
static char const *h3_buttons(char const *text)
{
	static char buffers[4][512];
	static int next;
	static char const *const playstation[4] = { N_("Cross"), N_("Circle"), N_("Square"), N_("Triangle") };
	static char const *const keyboard[4] = { N_("Enter"), "Esc", "E", "Tab" };
	static char const *const xbox_names[4] = { "A", "B", "X", "Y" };
	int shown = halo_button_icons_shown();
	char const *const *names = shown == _halo_button_icons_keyboard ? keyboard : xbox_names;
	char *out;
	int length = 0;

	if (!text || shown == _halo_button_icons_playstation)
		return text;
	out = buffers[next];
	next = (next + 1) % 4;
	while (*text && length < (int)sizeof(buffers[0]) - 1)
	{
		/* (a word starts after a letter or a UTF-8 letter's bytes no more) */
		boolean word_start = length == 0 || !(isalpha((unsigned char)out[length - 1]) || (out[length - 1] & 0x80));
		int index;
		int matched = 0;

		for (index = 0; word_start && index < 4 && !matched; index++)
		{
			char const *word = T(playstation[index]);
			size_t word_length = strlen(word);
			char after = text[word_length];

			if (!strncmp(text, word, word_length) && !isalpha((unsigned char)after) && !(after & 0x80))
			{
				char const *name = names == keyboard && index < 1 ? T(names[index]) : names[index];
				int name_length = (int)strlen(name);

				if (length + name_length >= (int)sizeof(buffers[0]) - 1)
					break;
				memcpy(out + length, name, name_length);
				length += name_length;
				text += word_length;
				matched = 1;
			}
		}
		if (!matched)
			out[length++] = *text++;
	}
	out[length] = 0;

	return out;
}
#endif

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

/* whether internet play runs (the panel's Connection: Online); ad hoc play
runs internet play's tunnel too (p2p_adhoc.c), but reaches no internet */
static boolean internet_play(void)
{
	char text[96];

	return !config_boolean("network.adhoc") && p2p_status(text, sizeof(text)) != 0;
}

/* a setting the settings panel and these screens change live: its
environment variable as they set it, else config.toml's */
static boolean config_boolean_live(char const *name, char const *variable)
{
	char const *value = getenv(variable);

	if (value && value[0])
		return !strcmp(value, "true") || !strcmp(value, "1");
	return config_boolean(name) != 0;
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
panel's Connection Online), SYSTEM LINK (the Vitas on the same Wi-Fi), AD
HOC (the Vitas near: ad hoc play above) or OFFLINE (split screen), and
picks what START GAME and BROWSE GAMES open; the first three are the
connection (the settings panel's Connection, network_connection.c),
switched live when chosen, its status said under NETWORK. GAME and
MAP are the game type and the map the Xbox's lists, after START GAME, open
on (the ones last used: kept as the Xbox's lists keep them,
saved_game_files.c), chosen in pickers as Halo 3's. */

enum
{
	_h3_network_online,
	_h3_network_system_link,
	_h3_network_adhoc,
	_h3_network_offline,
	NUMBER_OF_H3_NETWORKS
};

#define H3_MAP_COUNT 13
/* (the mp_map_grafix frame of a map that is not the Xbox's: its "?") */
#define H3_MAP_OTHER_PICTURE 13
/* the map picker: thirteen rows that scroll over the Xbox's maps and then
the custom ones the mod manager has on (custom_edition_maps.h), each
level's path kept */
#define H3_MAP_ROWS 13
#define H3_MAXIMUM_MAPS (H3_MAP_COUNT + CUSTOM_EDITION_MAPS_MAXIMUM)
#define H3_MAP_PATH_SIZE 160
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
	/* the map picker's maps (the Xbox list's levels: the Xbox's, then the
	custom ones turned on), each one's path, display index (its picture's
	frame and description's string, custom_edition_maps.h) and name; its
	rows (they scroll) */
	short map_count;
	char map_paths[H3_MAXIMUM_MAPS][H3_MAP_PATH_SIZE];
	short map_displays[H3_MAXIMUM_MAPS];
	char map_names[H3_MAXIMUM_MAPS][48];
	struct browser_view map_view;
	short map_cursor;
	short map_page;
	char said[192];
	char map_said[288];
} h3_mp;

static char const *h3_network_name(short network)
{
	if (network == _h3_network_online)
		return T("ONLINE");
	if (network == _h3_network_offline)
		return T("OFFLINE");
	/* (the connection Ad hoc: system link over the Vitas' own network) */
	return network == _h3_network_adhoc ? T("AD HOC") : T("SYSTEM LINK");
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

/* (the lobby's map one of the Xbox's whose file is missing or cannot be
played - Ben's 0-byte beavercreek.map, Battle Creek being the lobby's
first: its live lobby never opened -) the first that can be, said */
static void h3_mp_map_playable(void)
{
	char why[160];
	short index;

	if (h3_mp.map_index == NONE || map_check(h3_map_paths[h3_mp.map_index], why, sizeof(why)) == _map_check_playable)
		return;
	for (index = 0; index < H3_MAP_COUNT; index++)
	{
		if (map_check_playable(h3_map_paths[index]))
		{
			platform_log("menus: h3 mp: %s can't be played (%s): the lobby's map is %s", h3_map_names[h3_mp.map_index], why,
				h3_map_names[index]);
			h3_mp.map_index = index;
			return;
		}
	}
	platform_log("menus: h3 mp: no multiplayer map can be played (%s)", why);
}

/* the map the Xbox's level list opens on: the one last used, else its
first; one that can be played (h3_mp_map_playable) */
static void h3_mp_map_read(void)
{
	char map[MAXIMUM_FILENAME_LENGTH + 1];
	short index;

	h3_mp.map_index = 0;
	h3_mp.map_other[0] = 0;
	map[0] = 0;
	if (!saved_game_file_retrieve_last_used_multiplayer_map(map) || !map[0])
	{
		h3_mp_map_playable();
		return;
	}
	map[sizeof(map) - 1] = 0;
	for (index = 0; index < H3_MAP_COUNT; index++)
	{
		if (!_stricmp(map, h3_map_paths[index]))
		{
			h3_mp.map_index = index;
			h3_mp_map_playable();
			return;
		}
	}
	/* (a custom map turned off in the mod manager, or deleted, is in no
map list: the Xbox's list opens on its first map, and so does the lobby) */
	if (custom_edition_maps_display_index(map) == NONE)
	{
		platform_log("menus: h3 mp: the last map used, %s, is not in the map list (off or gone)", map);
		h3_mp_map_playable();
		return;
	}
	h3_mp.map_index = NONE;
	snprintf(h3_mp.map_other, sizeof(h3_mp.map_other), "%s", map);
}

/* the lobby's network of the connection chosen */
static short h3_mp_network_of_connection(void)
{
	short connection = network_connection_wanted();

	return connection == NETWORK_CONNECTION_ONLINE ? _h3_network_online : connection == NETWORK_CONNECTION_ADHOC ?
		_h3_network_adhoc : _h3_network_system_link;
}

/* (the lobby opened) the network: the connection's (the one used last: the
settings panel keeps it), or OFFLINE when it was chosen this run; the map
and game type the Xbox's lists would open on */
static void h3_mp_initialize(void)
{
	if (!h3_mp.initialized)
		h3_mp.initialized = TRUE;
	if (h3_mp.network != _h3_network_offline)
		h3_mp.network = h3_mp_network_of_connection();
	menu_functions.browse_lan_pending = FALSE;
	h3_mp_map_read();
	h3_mp_gametypes_read();
}

static char const *h3_mp_map_name(void)
{
	static char name[48];
	char const *slash;
	short display;
	wchar_t const *custom;

	if (h3_mp.map_index != NONE)
		return h3_map_names[h3_mp.map_index];
	/* (a custom map: its own name, as the Xbox's list names it) */
	display = custom_edition_maps_display_index(h3_mp.map_other);
	if (display != NONE && (custom = custom_edition_maps_name(display)) != NULL)
	{
		h3_utf8(name, sizeof(name), custom);
		if (name[0])
			return name;
	}
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
start_adhoc, start_offline, browse_online, browse_link, browse_adhoc,
code_row), the map's picture (map_picture: ui.map's mp_map_grafix), the
caption, the players and the status (players, ready, ready_sub) */
static void h3_mp_lobby_update(struct widget_instance *widget)
{
	static char const *const rows[] = { "start_online", "start_link", "start_adhoc", "start_offline", "browse_online",
		"browse_link", "browse_adhoc", "code_row" };
	/* (each row's network: the code's ONLINE's) */
	static short const row_networks[] = { _h3_network_online, _h3_network_system_link, _h3_network_adhoc,
		_h3_network_offline, _h3_network_online, _h3_network_system_link, _h3_network_adhoc, _h3_network_online };
	struct widget_instance *screen = screen_of(widget);
	struct widget_instance *found;
	char text[192], said[192];
	short index;
	int maximum;

	if (!h3_mp.initialized)
		h3_mp_initialize();
	/* (the connection's networks follow it: the settings panel's change
	shows) */
	if (h3_mp.network != _h3_network_offline)
		h3_mp.network = h3_mp_network_of_connection();
	if (named(screen, "network_row"))
		h3_forge.lobby_last = FALSE;
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
	/* (Square: GAME OPTIONS, ONLINE's) */
	if ((found = named(screen, "x_butn")) != NULL)
		widget_instance_set_visibility_recursive(found, h3_mp.network == _h3_network_online);
	if ((found = named(screen, "options")) != NULL)
		widget_instance_set_visibility_recursive(found, h3_mp.network == _h3_network_online);
	if ((found = named(screen, "map_picture")) != NULL)
	{
		/* (the game's own level pictures: a custom map's display index
		draws its own picture, else the "?") */
		short display = h3_mp.map_index == NONE ? custom_edition_maps_display_index(h3_mp.map_other) : NONE;

		found->animation.current_frame_index = h3_mp.map_index != NONE ? h3_mp.map_index :
			display != NONE ? display : H3_MAP_OTHER_PICTURE;
	}
	/* (Halo 3's: "Slayer on Valhalla") */
	snprintf(text, sizeof(text), T("%s on %s"), h3_mp_gametype_name(), h3_mp_map_name());
	text_show(named(screen, "caption"), text);
	/* ("1 Player (16 max)": the most players a game of that network takes) */
	maximum = h3_mp.network == _h3_network_offline ? 4 : h3_mp.network != _h3_network_online ? 16 :
		(int)PIN(atol(setting_get("HALO_NET_MAX_PLAYERS", "16")), 2, 16);
	h3_party_players_text(text, (int)sizeof(text), maximum);
	text_show(named(screen, "players"), text);
	text_show(named(screen, "ready"), T("Ready"));
	/* (online: who can join a game hosted, network.who_can_join - the
	server browser lists the Vitas' games and the PCs' together, tagged) */
	if (h3_mp.network == _h3_network_online)
		snprintf(text, sizeof(text), "%s", network_wire_host_crossplay() ?
			T("Host a game Vitas and PC players can join, or join one.") : T("Host a game on the Internet, or join one."));
	else if (h3_mp.network == _h3_network_system_link)
		snprintf(text, sizeof(text), "%s", T("Play with the PS Vitas on your Wi-Fi network."));
	else if (h3_mp.network == _h3_network_adhoc)
		snprintf(text, sizeof(text), "%s", T("Play with the PS Vitas near you."));
	else
		snprintf(text, sizeof(text), "%s", T("This party is local to this PS Vita."));
	text_show(named(screen, "ready_sub"), text);
	/* (the live lobby's players and status over these: h3_live) */
	if (named(screen, "network_row"))
		h3_live_lobby_show(screen);
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

	short before = h3_mp.gametype;

	if (index < 0 || index >= h3_mp.gametype_count)
		return;
	h3_mp.gametype = (short)index;
	if (saved_game_file_get_path_to_enclosing_directory(h3_mp.gametypes[index], directory))
		saved_game_file_remember_last_used_multiplayer_variant_directory(directory);
	platform_log("menus: h3 mp: game type %s", h3_mp.gametype_names[index]);
	/* (a live lobby's game on it; refused, the lobby's back on the game's) */
	if (!h3_live_follow() && before != NONE && before < h3_mp.gametype_count)
	{
		h3_mp.gametype = before;
		if (saved_game_file_get_path_to_enclosing_directory(h3_mp.gametypes[before], directory))
			saved_game_file_remember_last_used_multiplayer_variant_directory(directory);
		platform_log("menus: h3 live lobby: game type %s refused: still %s", h3_mp.gametype_names[index],
			h3_mp.gametype_names[before]);
	}
}

/* (port h3 map focus: the map picker opened) its maps read as the Xbox's
level list reads them: its thirteen, then the custom multiplayer maps in
the maps folder that the mod manager has on (custom_edition_maps.c looks
again), each with its name, picture and description; the rows on the map
chosen */
static void h3_mp_maps_read(void)
{
	char *stock[H3_MAP_COUNT];
	char **levels;
	short count = 0;
	short index;

	for (index = 0; index < H3_MAP_COUNT; index++)
		stock[index] = (char *)h3_map_paths[index];
	levels = custom_edition_maps_level_list(stock, H3_MAP_COUNT, &count);
	h3_mp.map_count = 0;
	for (index = 0; levels && index < count && index < H3_MAXIMUM_MAPS; index++)
	{
		short slot = h3_mp.map_count;
		wchar_t const *name;

		if (!levels[index] || strlen(levels[index]) >= H3_MAP_PATH_SIZE)
			continue;
		snprintf(h3_mp.map_paths[slot], sizeof(h3_mp.map_paths[slot]), "%s", levels[index]);
		h3_mp.map_displays[slot] = custom_edition_maps_level_display_index(index);
		h3_mp.map_names[slot][0] = 0;
		if (index < H3_MAP_COUNT)
			snprintf(h3_mp.map_names[slot], sizeof(h3_mp.map_names[slot]), "%s", T(h3_map_names[index]));
		else if ((name = custom_edition_maps_name(h3_mp.map_displays[slot])) != NULL)
			h3_utf8(h3_mp.map_names[slot], sizeof(h3_mp.map_names[slot]), name);
		if (!h3_mp.map_names[slot][0])
		{
			char const *slash = strrchr(levels[index], '\\');

			snprintf(h3_mp.map_names[slot], sizeof(h3_mp.map_names[slot]), "%s", slash ? slash + 1 : levels[index]);
		}
		h3_mp.map_count++;
	}
	/* (the rows on the map chosen: the Xbox's, else a custom one by path) */
	h3_mp.map_cursor = 0;
	for (index = 0; index < h3_mp.map_count; index++)
	{
		if ((h3_mp.map_index != NONE && index == h3_mp.map_index) ||
			(h3_mp.map_index == NONE && h3_mp.map_other[0] && !_stricmp(h3_mp.map_paths[index], h3_mp.map_other)))
		{
			h3_mp.map_cursor = index;
		}
	}
	browser_view_reset(&h3_mp.map_view);
	h3_mp.map_page = 0;
	h3_mp.map_said[0] = 0;
	platform_log("menus: h3 mp: the map picker: %d maps (%d custom)", h3_mp.map_count,
		MAX(h3_mp.map_count - H3_MAP_COUNT, 0));
}

/* (port h3 maps) the map picker's rows (item_0 to item_12): the maps,
scrolling past the thirteen as the game types do (a step past the first or
last row, the triggers a page); the focused one's picture (the game's own
level pictures, which draw a custom map's own, else its "?") and
description */
static void h3_mp_maps_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focus = NULL, *found;
	long index, focused_row = NONE, target;

	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		if (child == list->focused_child)
			focused_row = index;
	}
	target = browser_view_follow(&h3_mp.map_view, H3_MAP_ROWS, h3_mp.map_count, (int)focused_row, h3_mp.map_cursor,
		h3_mp.map_page);
	h3_mp.map_page = 0;
	h3_mp.map_cursor = (short)target;
	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		boolean shown = index < h3_mp.map_view.visible;
		char text[64];

		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		if (!shown)
			continue;
		snprintf(text, sizeof(text), "%s", h3_mp.map_names[h3_mp.map_view.offset + index]);
		h3_capitals(text);
		/* (one of the Xbox's maps whose file is not there, or cannot be
		played: marked, not chosen, h3_mp_map_set) */
		switch (map_check(h3_mp.map_paths[h3_mp.map_view.offset + index], NULL, 0))
		{
		case _map_check_missing:
			snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s", T(" (MISSING)"));
			break;
		case _map_check_unusable:
			snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s", T(" (DAMAGED)"));
			break;
		}
		text_show(child, text);
		if (index == h3_mp.map_view.row)
			focus = child;
	}
	if (focus && focus != list->focused_child)
		ui_widget_port_give_focus(list, focus);
	if (target >= 0 && target < h3_mp.map_count)
	{
		char said[288];
		char why[160];
		boolean playable = map_check(h3_mp.map_paths[target], why, sizeof(why)) == _map_check_playable;

		if ((found = named(screen, "map_picture")) != NULL)
			found->animation.current_frame_index = h3_mp.map_displays[target];
		string_show(named(screen, "map_text"), h3_mp.map_displays[target]);
		/* (why it cannot be played, in its description's place) */
		if ((found = named(screen, "map_text")) != NULL)
			widget_instance_set_visibility_recursive(found, playable);
		if ((found = named(screen, "map_why")) != NULL)
		{
			widget_instance_set_visibility_recursive(found, !playable);
			text_show(found, playable ? "" : why);
		}
		snprintf(said, sizeof(said), "%s (%ld of %d)%s%s", h3_mp.map_names[target], target + 1, h3_mp.map_count,
			playable ? "" : ": can't be played: ", playable ? "" : why);
		if (strcmp(said, h3_mp.map_said))
		{
			snprintf(h3_mp.map_said, sizeof(h3_mp.map_said), "%s", said);
			platform_log("menus: h3 maps: %s", said);
		}
	}
}

/* (port h3 map set) the picker's choice: the map the Xbox's level list
opens on, as its own choice keeps it (a custom map by its level's path:
START GAME, Create Game and the map's sharing take it from there) */
static boolean h3_mp_map_set(struct widget_instance *widget)
{
	long index = h3_mp.map_view.offset + h3_child_index(widget);
	short before_index = h3_mp.map_index;
	char before_other[sizeof(h3_mp.map_other)];
	char why[160];

	if (index < 0 || index >= h3_mp.map_count)
		return TRUE;
	/* (a map that cannot be played: not chosen, the picker stays on it, its
	reason shown) */
	if (map_check(h3_mp.map_paths[index], why, sizeof(why)) != _map_check_playable)
	{
		platform_log("menus: h3 maps: %s can't be chosen: %s", h3_mp.map_names[index], why);
		return FALSE;
	}
	snprintf(before_other, sizeof(before_other), "%s", h3_mp.map_other);
	if (index < H3_MAP_COUNT)
	{
		h3_mp.map_index = (short)index;
		h3_mp.map_other[0] = 0;
		saved_game_file_remember_last_used_multiplayer_map(h3_map_paths[index]);
		platform_log("menus: h3 mp: map %s", h3_map_names[index]);
	}
	else
	{
		h3_mp.map_index = NONE;
		snprintf(h3_mp.map_other, sizeof(h3_mp.map_other), "%s", h3_mp.map_paths[index]);
		saved_game_file_remember_last_used_multiplayer_map(h3_mp.map_paths[index]);
		platform_log("menus: h3 mp: map %s (a custom map: %s)", h3_mp.map_names[index], h3_mp.map_paths[index]);
	}
	/* (a live lobby's game on it; refused - a map this version does not
	play with others: said - the lobby's back on the game's map) */
	if (!h3_live_follow())
	{
		h3_mp.map_index = before_index;
		snprintf(h3_mp.map_other, sizeof(h3_mp.map_other), "%s", before_other);
		saved_game_file_remember_last_used_multiplayer_map((char *)(before_index != NONE ? h3_map_paths[before_index] :
			before_other));
		platform_log("menus: h3 live lobby: that map refused: still %s", h3_mp_map_name());
	}
	return TRUE;
}

/* (port h3 network status: the lobbies' line under NETWORK) the connection's
status - Online, Connecting..., Same Wi-Fi, Ad hoc, No Wi-Fi, or why Online
failed (network_connection_status) - as Halo 3's Xbox LIVE indicator; none
for OFFLINE and LOCAL, which use no connection */
static void h3_network_status_update(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	boolean campaign = named(screen, "mission_row") != NULL;
	char text[128];

	text[0] = 0;
	if (campaign ? h3_campaign.network != _h3_campaign_local : h3_mp.network != _h3_network_offline)
		network_connection_status(text, (int)sizeof(text));
	text_show(widget, text);
}

/* (port h3 network set) the network picker's choice: ONLINE, SYSTEM LINK
and AD HOC the connection too, switched live (network_connection.c) */
static boolean h3_mp_network_set(struct widget_instance *widget)
{
	short network = (short)PIN(h3_child_index(widget), 0, NUMBER_OF_H3_NETWORKS - 1);
	/* (the picker of the lobby shown last: the multiplayer lobby's, or
	the Forge lobby's own NETWORK) */
	short *lobby_network = h3_forge.lobby_last ? &h3_forge.network : &h3_mp.network;

	/* (a live lobby's game closed first: the connection switches only
	with no network game, and the lobby opens again on the new one) */
	if (network != *lobby_network)
		h3_live_close("another network chosen");
	h3_live_retry();
	if (network != _h3_network_offline)
	{
		network_connection_choose(network == _h3_network_online ? NETWORK_CONNECTION_ONLINE :
			network == _h3_network_adhoc ? NETWORK_CONNECTION_ADHOC : NETWORK_CONNECTION_WIFI);
	}
	*lobby_network = network;
	platform_log("menus: %s: network %s", h3_forge.lobby_last ? "forge lobby" : "h3 mp", h3_network_name(network));
	return TRUE;
}

/* ---- typing */

#if !defined(HALO_VITA) && !defined(HALO_RELEASE)
/* (test builds) a scripted run's presses (HALO_TEST_PAD) with neither the
answers (HALO_TEST_TEXT_INPUT) nor typing of its own (HALO_TEST_KEYS): no
one to type in the text box, so none is asked for - typing cancelled, a
name not asked (typing_available) - as before the PC build had one */
static boolean typing_scripted_without_answers(void)
{
	char const *pad = getenv("HALO_TEST_PAD");

	return pad && pad[0] && !getenv("HALO_TEST_TEXT_INPUT") && !getenv("HALO_TEST_KEYS");
}
#endif

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
		/* (tests, never in a release build: HALO_TEST_TEXT_INPUT, the next
		answer, "!" a cancel, cancelled once they are all given) */
#ifndef HALO_RELEASE
		char const *answers = typing_scripted_without_answers() ? "" : getenv("HALO_TEST_TEXT_INPUT");
#else
		char const *answers = NULL;
#endif
		static int answered;
		int index = 0;
		char answer[HALO_TEXT_INPUT_SIZE];
		int length = 0;

		/* the PC build's own text box, as the Vita's system keyboard
		(port/linux/src/pc_text_entry.c, drawn by pc_text_entry_overlay.c):
		every PC build, a release one too, types there */
		if (!answers)
		{
			pc_text_entry_request();
			platform_log("menus: typing \"%s\": the text box asked for", title);
			return;
		}
		while (*answers && index < answered)
		{
			if (*answers++ == '|')
				index++;
		}
		while (*answers && *answers != '|' && length < (int)sizeof(answer) - 1)
			answer[length++] = *answers++;
		answer[length] = 0;
		answered++;
		if ((length || *answers == '|') && strcmp(answer, "!"))
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

/* ---- the Halo 3 profile screens (halo3/profiles.xml, customize.xml):
SELECT PROFILE stands for the Xbox's profile screens, the campaign's
(ui\shell\main_menu\load_campaign_player_profile) and the settings'
(player_profile_select_screen, which the main menu's CUSTOMIZATION opens),
and CUSTOMIZATION's editor for the Xbox's profile settings (the name, the
colour, the controller and advanced controller settings). They read and
write the profiles as the Xbox's screens do, with the same functions on the
same fields of the same files (blam.sav, beside it SaveMeta.xbx):

- the list is the 3-wide list's (player_profile_list_initialize): the
  profiles player_profiles_enumerate_available_to_local_player_index gives,
  not the default ones; each card shows what the Xbox's card does (the name,
  the colour, the level after the highest finished and its difficulty, the
  controls);
- a profile chosen for the campaign is set as "sp set p1 controller choice"
  and "3wide plyr prof set for game" set it (player_ui_set_active_player
  _profile), and the level select opens (the Halo 3 campaign lobby stands
  for it); one chosen to customize is edited as "player profile begin
  editing" edits it (player_ui_begin_editing_profile);
- a profile whose file failed its checksum (not valid: a 0-byte blam.sav)
  is neither played nor edited, as the Xbox refuses it ("Unable to load
  saved game file"): the screen says so, and the player picks another,
  makes one or deletes it;
- CREATE NEW PROFILE makes one as "create&edit player profile" does
  (player_profile_new, the name typed on the Vita's keyboard and checked as
  the Xbox's keyboard checks it: player_name_clean, not empty, no other
  profile's), then goes on as choosing it would;
- Square deletes one, after a question, as "final del player profile" does
  (player_profile_delete, never a default profile);
- the editor changes the edit profile's fields as "plyr prf save cntl
  settings", "plyr prf save adv cntl set" and "color picker select color"
  set them (the colour may also be none again: the host picks one), and
  going back saves them as "player profile save changes" does
  (player_ui_save_profile: player_profile_save, its file and its
  SaveMeta.xbx name), only when something changed. The profile in use is
  then the saved one (its controls applied: player_ui_set_active_player
  _profile).

The settings panel's Controls tab keeps the Vita's own settings (look
speed, invert, the sticks' deadzone, which Vita button is each Xbox button,
gyro aiming): they act on top of the profile's, which travel with the
profile. Where they meet (look sensitivity, invert look, the button
layout) the editor's help says how; with both inverts on it says they
cancel out. */

#define H3_PROFILE_ROWS 10
#define H3_MAXIMUM_PROFILES 100
/* (the swatches' frames: none, then the game's colours) */
#define H3_SWATCH_FRAMES 19
#define PROFILE_INDEX_FLAGS 0xC0000000UL

enum
{
	_h3_profiles_campaign,
	_h3_profiles_customize,
};

/* the editor's rows, in halo3/customize.xml's order */
enum
{
	_h3_custom_name,
	_h3_custom_color,
	/* (port: a player model from a Custom Edition map, player_models.c) */
	_h3_custom_model,
	_h3_custom_stick,
	_h3_custom_buttons,
	_h3_custom_look,
	_h3_custom_invert,
	_h3_custom_flight,
	_h3_custom_autocenter,
	_h3_custom_vibration,
	NUMBER_OF_H3_CUSTOM_ROWS
};

static char const *const h3_custom_rows[NUMBER_OF_H3_CUSTOM_ROWS] =
{
	"op_name", "op_color", "op_model", "op_stick", "op_buttons", "op_look", "op_invert", "op_flight", "op_autocenter",
	"op_vibration",
};
/* (the game's colours, in its order: player_profile.c profile_color_table) */
static char const *const h3_color_names[H3_SWATCH_FRAMES] =
{
	N_("RANDOM"), N_("WHITE"), N_("BLACK"), N_("RED"), N_("BLUE"), N_("GRAY"), N_("YELLOW"), N_("GREEN"), N_("PINK"),
	N_("PURPLE"), N_("CYAN"), N_("COBALT"), N_("ORANGE"), N_("TEAL"), N_("SAGE"), N_("BROWN"), N_("TAN"),
	N_("MAROON"), N_("SALMON"),
};
static char const *const h3_stick_names[NUMBER_OF_JOYSTICK_PRESETS] =
{
	N_("STANDARD"), N_("SOUTHPAW"), N_("LEGACY"), N_("LEGACY SOUTHPAW"),
};
/* (player_ui.c set_local_player_controls_from_player_profile: what each
preset moves) */
static char const *const h3_button_names[NUMBER_OF_BUTTON_PRESETS] =
{
	N_("STANDARD"), N_("SWAP FIRE/GRENADE"), N_("SWAP JUMP/GRENADE"), N_("SWAP MELEE/GRENADE"), N_("SWAP MELEE/ZOOM"),
};

boolean player_name_clean(wchar_t *name, long count);

static struct
{
	short mode;
	/* the profiles listed, as read when the screen opened or changed: each
	one's index, profile and whether its file could be read */
	long indices[H3_MAXIMUM_PROFILES];
	struct player_profile profiles[H3_MAXIMUM_PROFILES];
	boolean damaged[H3_MAXIMUM_PROFILES];
	short count;
	boolean read;
	/* the rows (they scroll: browser_list.h) on the items, the profiles then
	CREATE NEW PROFILE; the item chosen; a trigger's page */
	struct browser_view view;
	short cursor;
	short page;
	/* the profile to choose when the list is next read (NONE: the one in
	use, else the one last used) */
	long focus_index;
	char status[160];
	boolean status_kept;
	/* the item the status is said of (-2: the one chosen next frame): it
	goes when another is chosen */
	short status_item;
	/* a profile just made: its controller, and the screen to go on to,
	opened from the main loop */
	short controller_index;
	boolean open_pending;
	/* the profile Square asked to delete */
	long delete_index;
	char delete_name[48];
	char said[192];
	/* (port) the card's Spartan asked for (menu_scene_spartan_show), in
	this colour and model's pack (NONE, "": none asked), and whether it
	stands there (what was last logged) */
	boolean spartan_asked;
	short spartan_color;
	char spartan_pack[48];
	boolean spartan_shown;
} h3_profiles;

/* the profile the editor edits, and what it last said */
static struct
{
	long index;
	char status[160];
	char said[256];
	/* the preview's Spartan asked for (menu_scene_spartan_show), in this
	colour (the game's index, NONE for none) */
	boolean preview_asked;
	short preview_color;
	/* the window shows the Spartan, not the swatch (what was last logged) */
	boolean preview_shown;
	/* (port) the player model chosen in the editor (player_models.c's
	index, NONE the Spartan), the profile's when it opened (saved with
	Circle when they differ), and what the preview last said of its pack */
	short model;
	short model_saved;
	char model_said[160];
} h3_custom;

static void h3_profiles_spartan(struct widget_instance *screen, struct player_profile const *profile,
	long profile_index);
static void h3_wrap(char *text, int line_length);
/* (the card's column's line, in characters) */
#define H3_PROFILES_CARD_LINE 15

/* two profile indices of the same file (their flags aside: a profile just
made, and the list's) */
static boolean h3_profile_same(long a, long b)
{
	return a != NONE && b != NONE &&
		((unsigned long)a & ~PROFILE_INDEX_FLAGS) == ((unsigned long)b & ~PROFILE_INDEX_FLAGS);
}

static void h3_profile_name(char *out, int size, struct player_profile const *profile)
{
	wchar_t name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1];

	memcpy(name, profile->player_name, sizeof(profile->player_name));
	name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH] = 0;
	h3_utf8(out, size, name);
}

/* the swatch frame of a profile's colour (0: none) */
static short h3_swatch_frame(short color)
{
	return (short)(color < 0 || color >= H3_SWATCH_FRAMES - 1 ? 0 : color + 1);
}

/* the profiles, read again (the Xbox's list initialize), the item chosen on
the profile asked for, the one in use or the one last used */
static void h3_profiles_read(void)
{
	word count = H3_MAXIMUM_PROFILES;
	long focus = h3_profiles.focus_index;
	short index;

	if (focus == NONE)
		focus = player_ui_get_active_player_profile_index(0);
	if (focus == NONE)
		focus = player_ui_get_player1_last_used_profile_index();
	player_profiles_enumerate_available_to_local_player_index(0, &count, h3_profiles.indices, FALSE);
	h3_profiles.count = (short)MIN(count, H3_MAXIMUM_PROFILES);
	h3_profiles.cursor = 0;
	for (index = 0; index < h3_profiles.count; index++)
	{
		long profile_index = h3_profiles.indices[index];

		memset(&h3_profiles.profiles[index], 0, sizeof(h3_profiles.profiles[index]));
		/* (a file that failed its checksum reads as defaults with its name,
		player_profile_read; one that cannot be read at all, its name) */
		h3_profiles.damaged[index] = !TEST_FLAG(profile_index, _saved_game_file_index_valid_bit);
		if (!player_profile_get(profile_index, &h3_profiles.profiles[index]))
		{
			wchar_t const *name = saved_game_file_get_display_name(profile_index);
			short character;

			h3_profiles.damaged[index] = TRUE;
			h3_profiles.profiles[index].primary_color_index = NONE;
			for (character = 0; name && character < MAXIMUM_PLAYER_PROFILE_NAME_LENGTH - 1 && name[character]; character++)
				h3_profiles.profiles[index].player_name[character] = name[character];
		}
		if (h3_profile_same(profile_index, focus))
			h3_profiles.cursor = index;
	}
	browser_view_reset(&h3_profiles.view);
	h3_profiles.page = 0;
	h3_profiles.focus_index = NONE;
	h3_profiles.read = TRUE;
	h3_profiles.said[0] = 0;
	{
		char line[192];
		int length = 0;

		line[0] = 0;
		for (index = 0; index < h3_profiles.count && length < (int)sizeof(line) - 40; index++)
		{
			char name[48];

			h3_profile_name(name, sizeof(name), &h3_profiles.profiles[index]);
			length += snprintf(line + length, sizeof(line) - (size_t)length, "%s%s%s", index ? ", " : "", name,
				h3_profiles.damaged[index] ? " (damaged)" : "");
		}
		platform_log("menus: h3 profiles: %d profile%s (%s)%s%s", h3_profiles.count, h3_profiles.count == 1 ? "" : "s",
			h3_profiles.mode == _h3_profiles_customize ? "customization" : "campaign", h3_profiles.count ? ": " : "",
			line);
	}
}

/* (a screen's "created": port h3 profiles campaign, port h3 profiles
customize) the list read as it opens, as the Xbox's list initialize does;
the campaign's controllers forgotten, as its screen's "sp reset controller
choices" does */
static void h3_profiles_initialize(short mode)
{
	h3_profiles.mode = mode;
	h3_profiles.delete_index = NONE;
	/* (what the question's DELETE PROFILE said stays, over the list read
	again) */
	if (!h3_profiles.status_kept)
		h3_profiles.status[0] = 0;
	h3_profiles.status_kept = FALSE;
	h3_profiles.open_pending = FALSE;
	if (mode == _h3_profiles_campaign)
		player_ui_reset_single_player_local_player_controllers();
	h3_profiles_read();
	/* (the card's Spartan: the profiles' player models, the maps folder
	looked through once a session) */
	player_models_scan(FALSE);
	h3_profiles.spartan_asked = FALSE;
	h3_profiles.spartan_shown = FALSE;
}

/* the item of a row (row_<n>), else the one chosen */
static short h3_profiles_item(struct widget_instance *widget)
{
	if (widget && widget->name && !strncmp(widget->name, "row_", 4) && widget->name[4] >= '0' && widget->name[4] <= '9')
	{
		long row = atol(widget->name + 4) - 1;

		if (row >= 0 && row < h3_profiles.view.visible)
			return (short)(h3_profiles.view.offset + row);
	}
	return h3_profiles.cursor;
}

/* a typed name made a profile's (UTF-16), checked as the Xbox's keyboard
checks one (virtual_keyboard.c's Done): its spaces at the ends dropped, a
character the ban command can name (player_name_clean), not empty, and no
other profile's (saved_game_file_name_unique) unless it is the profile's
own (original); FALSE with the reason in status */
static boolean h3_profile_name_check(char const *typed, wchar_t *name, wchar_t const *original, char *status, int size)
{
	short index;
	boolean same = original != NULL;

	memset(name, 0, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH * sizeof(wchar_t));
	for (index = 0; index < MAXIMUM_PLAYER_PROFILE_NAME_LENGTH - 1 && typed[index]; index++)
		name[index] = (wchar_t)(unsigned char)typed[index];
	if (!player_name_clean(name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH) || !name[0])
	{
		snprintf(status, size, "%s", T("A profile's name needs a letter or a number."));
		return FALSE;
	}
	for (index = 0; same && index < MAXIMUM_PLAYER_PROFILE_NAME_LENGTH; index++)
	{
		if (name[index] != original[index])
			same = FALSE;
		if (!name[index] || !original[index])
			break;
	}
	if (!same && !saved_game_file_name_unique(name))
	{
		snprintf(status, size, "%s", T("Another profile has that name."));
		return FALSE;
	}
	return TRUE;
}

/* the profile of an item chosen: the campaign's, as the Xbox's profile
screen chooses it ("sp set p1 controller choice", "3wide plyr prof set for
game"); TRUE when it is set, and the level select (the Halo 3 lobby) opens */
static boolean h3_profile_play(short item, short controller_index)
{
	struct player_profile profile;
	long profile_index;
	char name[48];

	if (item < 0 || item >= h3_profiles.count)
		return FALSE;
	profile_index = h3_profiles.indices[item];
	h3_profile_name(name, sizeof(name), &h3_profiles.profiles[item]);
	if (h3_profiles.damaged[item])
	{
		snprintf(h3_profiles.status, sizeof(h3_profiles.status), "%s",
			T("This profile's file is damaged and can't be loaded.\nChoose another profile, or create a new one."));
		h3_profiles.status_item = -2;
		platform_log("menus: h3 profiles: %s is damaged: not chosen", name);
		return FALSE;
	}
	if (!player_profile_get(profile_index, &profile))
	{
		snprintf(h3_profiles.status, sizeof(h3_profiles.status), "%s", T("This profile can't be read now."));
		h3_profiles.status_item = -2;
		return FALSE;
	}
	player_ui_set_single_player_local_player_controller(0, (short)(controller_index != NONE ? controller_index : 0));
	player_ui_set_active_player_profile(0, profile_index, &profile);
	h3_nameplate_changed(profile_index);
	h3_session_profile_chosen((short)(controller_index != NONE ? controller_index : 0), profile_index);
	platform_log("menus: h3 profiles: chose %s (campaign)", name);
	return TRUE;
}

/* the profile of an item chosen to customize, edited as the Xbox's "player
profile begin editing" edits it; TRUE when it is, and the editor opens */
static boolean h3_profile_edit(short item)
{
	struct player_profile *profile;
	char name[48];

	if (item < 0 || item >= h3_profiles.count)
		return FALSE;
	h3_profile_name(name, sizeof(name), &h3_profiles.profiles[item]);
	if (h3_profiles.damaged[item])
	{
		snprintf(h3_profiles.status, sizeof(h3_profiles.status), "%s",
			T("This profile's file is damaged and can't be loaded.\nDelete it, or create a new profile."));
		h3_profiles.status_item = -2;
		platform_log("menus: h3 profiles: %s is damaged: not edited", name);
		return FALSE;
	}
	player_ui_begin_editing_profile(h3_profiles.indices[item]);
	profile = player_ui_get_edit_player_profile();
	if (!profile)
	{
		snprintf(h3_profiles.status, sizeof(h3_profiles.status), "%s", T("This profile can't be read now."));
		h3_profiles.status_item = -2;
		return FALSE;
	}
	h3_custom.index = h3_profiles.indices[item];
	h3_custom.status[0] = 0;
	h3_custom.said[0] = 0;
	h3_custom.preview_asked = FALSE;
	h3_custom.preview_shown = FALSE;
	/* (the maps folder looked through again: a map added since) */
	player_models_scan(TRUE);
	h3_custom.model = h3_custom.model_saved = player_models_profile_choice(h3_custom.index);
	h3_custom.model_said[0] = 0;
	platform_log("menus: h3 customize: %d player models in the maps folder's Custom Edition maps; this profile's: %d",
		player_models_count(), h3_custom.model);
	platform_log("menus: h3 customize: editing %s: color %d, stick %d, buttons %d, look %d, invert %d, flight %d, "
		"autocenter %d, vibration %s", name, profile->primary_color_index, profile->controller_settings.joystick_preset,
		profile->controller_settings.button_preset, profile->controller_settings.look_sensitivity,
		profile->controller_settings.invert_look, profile->controller_settings.flight_stick_aircraft_controls,
		profile->controller_settings.autocenter, profile->controller_settings.vibration_disabled ? "off" : "on");
	return TRUE;
}

/* (CREATE NEW PROFILE, Triangle) its name asked for */
static void h3_profile_create_begin(short controller_index)
{
	h3_profiles.controller_index = controller_index;
	h3_profiles.status[0] = 0;
	typing_begin(_typing_profile_new, T("New profile's name"), "", MAXIMUM_PLAYER_PROFILE_NAME_LENGTH - 1, FALSE);
}

/* the name typed: the profile made as "create&edit player profile" makes it
(player_profile_new), chosen in the list; then the campaign goes on with it
(the lobby) or the editor opens, from the main loop */
static void h3_profile_create(char const *typed)
{
	wchar_t name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH];
	long profile_index;
	short index;

	if (!h3_profile_name_check(typed, name, NULL, h3_profiles.status, sizeof(h3_profiles.status)))
	{
		h3_profiles.status_item = -2;
		platform_log("menus: h3 profiles: the name \"%s\" was refused: %s", typed, h3_profiles.status);
		return;
	}
	profile_index = player_profile_new(h3_profiles.controller_index != NONE ? h3_profiles.controller_index : 0, name);
	if (profile_index == NONE)
	{
		snprintf(h3_profiles.status, sizeof(h3_profiles.status), "%s", T("The profile could not be created."));
		h3_profiles.status_item = -2;
		platform_log("menus: h3 profiles: \"%s\" could not be created", typed);
		return;
	}
	h3_profiles.focus_index = profile_index;
	h3_profiles_read();
	platform_log("menus: h3 profiles: created %s", typed);
	for (index = 0; index < h3_profiles.count; index++)
	{
		if (!h3_profile_same(h3_profiles.indices[index], profile_index))
			continue;
		if (h3_profiles.mode == _h3_profiles_customize ? h3_profile_edit(index) :
			h3_profile_play(index, h3_profiles.controller_index))
		{
			h3_profiles.open_pending = TRUE;
		}
		return;
	}
}

/* (Square) the profile of the item chosen, to be deleted after the
question; FALSE (no question) for CREATE NEW PROFILE */
static boolean h3_profile_delete_ask(void)
{
	short item = h3_profiles.cursor;

	if (item < 0 || item >= h3_profiles.count)
		return FALSE;
	h3_profiles.delete_index = h3_profiles.indices[item];
	h3_profile_name(h3_profiles.delete_name, sizeof(h3_profiles.delete_name), &h3_profiles.profiles[item]);
	return TRUE;
}

/* the question's DELETE PROFILE: deleted as "final del player profile"
deletes it (never a default profile, only a player profile's index) */
static void h3_profile_delete(void)
{
	long profile_index = h3_profiles.delete_index;

	h3_profiles.delete_index = NONE;
	if (profile_index == NONE || TEST_FLAG(profile_index, _saved_game_file_index_read_only_bit) ||
		saved_game_file_get_type(profile_index) != _saved_game_file_type_player_profile)
	{
		return;
	}
	player_profile_delete(profile_index);
	h3_session_forget();
	snprintf(h3_profiles.status, sizeof(h3_profiles.status), T("%s was deleted."), h3_profiles.delete_name);
	h3_profiles.status_item = -2;
	platform_log("menus: h3 profiles: deleted %s", h3_profiles.delete_name);
	h3_profiles.status_kept = TRUE;
	h3_profiles.read = FALSE;
}

/* (port h3 profiles: the list's rows, row_1 to row_10: row_swatch, row_name,
row_note) the profiles, then CREATE NEW PROFILE, scrolling past ten; the
chosen one's card (info_*: the Xbox card's), the status, the help */
static void h3_profiles_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focus = NULL, *found;
	long index, focused_row = NONE, items;
	short target;
	char text[192];

	if (typing_done(_typing_profile_new, text, sizeof(text)))
		h3_profile_create(text);
	if (!h3_profiles.read)
		h3_profiles_read();
	items = h3_profiles.count + 1;
	for (index = 0, child = list->child; child; child = child->next)
	{
		if (strncmp(child->name, "row_", 4))
			continue;
		if (child == list->focused_child)
			focused_row = index;
		index++;
	}
	target = (short)browser_view_follow(&h3_profiles.view, H3_PROFILE_ROWS, (int)items, (int)focused_row,
		h3_profiles.cursor, h3_profiles.page);
	h3_profiles.page = 0;
	h3_profiles.cursor = target;
	if (h3_profiles.status_item == -2)
		h3_profiles.status_item = target;
	else if (target != h3_profiles.status_item)
		h3_profiles.status[0] = 0;
	for (index = 0, child = list->child; child; child = child->next)
	{
		long item = h3_profiles.view.offset + index;
		boolean profile = item < h3_profiles.count;

		if (strncmp(child->name, "row_", 4))
			continue;
		if (index >= h3_profiles.view.visible)
		{
			widget_instance_set_visibility_recursive(child, FALSE);
			child->disabled = TRUE;
			index++;
			continue;
		}
		widget_instance_set_visibility_recursive(child, TRUE);
		child->disabled = FALSE;
		if (profile)
		{
			h3_profile_name(text, sizeof(text), &h3_profiles.profiles[item]);
			h3_capitals(text);
		}
		else
			snprintf(text, sizeof(text), "%s", T("CREATE NEW PROFILE"));
		text_show(named(child, "row_name"), text);
		text_show(named(child, "row_note"), profile && h3_profiles.damaged[item] ? T("DAMAGED") : "");
		if ((found = named(child, "row_swatch")) != NULL)
		{
			widget_instance_set_visibility_recursive(found, profile);
			if (profile)
				found->animation.current_frame_index = h3_swatch_frame(h3_profiles.profiles[item].primary_color_index);
		}
		if (index == h3_profiles.view.row)
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
	/* the card: the Xbox's (the name, the colour, the level after the
	highest finished, its difficulty, the controls) */
	{
		static char const *const labels[] = { "info_level_label", "info_level", "info_skill_label", "info_skill",
			"info_controls_label", "info_controls" };
		boolean card = target >= 0 && target < h3_profiles.count && !h3_profiles.damaged[target];
		char note[256];

		for (index = 0; index < (long)NUMBEROF(labels); index++)
		{
			if ((found = named(screen, labels[index])) != NULL)
				widget_instance_set_visibility_recursive(found, card);
		}
		note[0] = 0;
		if (target >= 0 && target < h3_profiles.count)
		{
			struct player_profile const *profile = &h3_profiles.profiles[target];
			short level, difficulty;

			h3_profile_name(text, sizeof(text), profile);
			text_show(named(screen, "info_name"), text);
			if ((found = named(screen, "info_swatch")) != NULL)
			{
				widget_instance_set_visibility_recursive(found, TRUE);
				found->animation.current_frame_index = h3_swatch_frame(profile->primary_color_index);
			}
			player_profile_get_highest_completed_solo_level((struct player_profile *)profile, &level, &difficulty);
			level = (short)MIN(level + 1, NUMBER_OF_SINGLE_PLAYER_LEVELS - 1);
			snprintf(text, sizeof(text), "%s", T(h3_level_names[PIN(level, 0, NUMBER_OF_SINGLE_PLAYER_LEVELS - 1)]));
			h3_capitals(text);
			h3_wrap(text, H3_PROFILES_CARD_LINE);
			text_show(named(screen, "info_level"), text);
			snprintf(text, sizeof(text), "%s", T(h3_difficulty_names[PIN(difficulty, 0, 3)]));
			h3_capitals(text);
			text_show(named(screen, "info_skill"), text);
			snprintf(text, sizeof(text), "%s%s%s",
				T(h3_stick_names[PIN(profile->controller_settings.joystick_preset, 0, NUMBER_OF_JOYSTICK_PRESETS - 1)]),
				profile->controller_settings.invert_look ? ", " : "",
				profile->controller_settings.invert_look ? T("INVERTED") : "");
			h3_wrap(text, H3_PROFILES_CARD_LINE);
			text_show(named(screen, "info_controls"), text);
			if (h3_profiles.damaged[target])
				snprintf(note, sizeof(note), "%s", T("This profile's file is damaged:\nit can't be loaded or played.\n"
					"Choose another profile, make a\nnew one, or delete this one."));
			h3_profiles_spartan(card ? screen : NULL, card ? profile : NULL, h3_profiles.indices[target]);
		}
		else
		{
			text_show(named(screen, "info_name"), T("NEW PROFILE"));
			if ((found = named(screen, "info_swatch")) != NULL)
				widget_instance_set_visibility_recursive(found, FALSE);
			snprintf(note, sizeof(note), "%s", h3_profiles.count ? T("Make a profile of your own:\ntype its name.") :
				T("There are no profiles yet.\nMake one to play: type its name."));
			h3_profiles_spartan(screen, NULL, NONE);
		}
		text_show(named(screen, "info_text"), note);
	}
	text_show(named(screen, "status"), h3_profiles.status);
	/* (said as it changes: the tests read it) */
	{
		char said[192];

		if (target >= 0 && target < h3_profiles.count)
			h3_profile_name(text, sizeof(text), &h3_profiles.profiles[target]);
		else
			snprintf(text, sizeof(text), "%s", "CREATE NEW PROFILE");
		snprintf(said, sizeof(said), "%s (%d of %ld)%s%s", text, target + 1, items, h3_profiles.status[0] ? ": " : "",
			h3_profiles.status);
		{
			char *newline;

			while ((newline = strchr(said, '\n')) != NULL)
				*newline = ' ';
		}
		if (strcmp(said, h3_profiles.said))
		{
			snprintf(h3_profiles.said, sizeof(h3_profiles.said), "%s", said);
			platform_log("menus: h3 profiles: %s", said);
		}
	}
}

/* text broken into lines of at most that many characters at its spaces
(the card's narrow column, beside its Spartan) */
static void h3_wrap(char *text, int line_length)
{
	char *line = text;

	while ((int)strlen(line) > line_length)
	{
		char *space = NULL, *cursor;

		for (cursor = line; *cursor && cursor - line <= line_length; cursor++)
		{
			if (*cursor == ' ')
				space = cursor;
		}
		if (!space)
			break;
		*space = '\n';
		line = space + 1;
	}
}

/* SELECT PROFILE's card (and CUSTOMIZATION's list, which shares it): the
focused profile's Spartan in the card's window, as the editor's preview
(h3_custom_spartan_preview: the same scene packs' Spartan,
menu_scene_spartan_show), in the profile's colour and its player model when
one is chosen for it (player_models.c), in front of the screen's own camera
(its scene=: the editor's, so that the editor opened from the list keeps
it). It follows the focus; none for CREATE NEW PROFILE, a damaged profile,
the scenes off or no pack (the card's colour swatch stays, as ever).
screen NULL lets it go (the screen gone), unless the editor's preview has
taken its place. */
#define H3_PROFILES_SPARTAN_PLACEMENT "2.4 1.15 -0.36 20"

static void h3_profiles_spartan(struct widget_instance *screen, struct player_profile const *profile,
	long profile_index)
{
	char name[64];
	char const *scene;
	char const *pack;
	short color;
	boolean shown;

	if (!screen || !profile)
	{
		if (h3_profiles.spartan_asked && !(screen == NULL && h3_custom.preview_asked))
		{
			menu_scene_spartan_hide();
			menu_scene_spartan_model(NULL);
			platform_log("menus: h3 profiles: the card's Spartan hidden");
		}
		h3_profiles.spartan_asked = FALSE;
		h3_profiles.spartan_shown = FALSE;
		return;
	}
	color = profile->primary_color_index >= 0 ? profile->primary_color_index : 0;
	pack = player_models_pack(player_models_profile_choice(profile_index));
	if (!h3_profiles.spartan_asked || strcmp(h3_profiles.spartan_pack, pack ? pack : ""))
	{
		menu_scene_spartan_model(pack);
		snprintf(h3_profiles.spartan_pack, sizeof(h3_profiles.spartan_pack), "%s", pack ? pack : "");
	}
	if (!h3_profiles.spartan_asked)
	{
		if (h3_scenes_enabled() && (scene = h3_scene_of(screen, name, sizeof(name), NULL)) != NULL &&
			menu_scene_spartan_show(scene, H3_PROFILES_SPARTAN_PLACEMENT, color))
		{
			h3_profiles.spartan_asked = TRUE;
			h3_profiles.spartan_color = color;
			platform_log("menus: h3 profiles: the card's Spartan asked for in colour %d (%s)", color,
				pack ? pack : "the Spartan");
		}
	}
	else if (color != h3_profiles.spartan_color)
	{
		h3_profiles.spartan_color = color;
		menu_scene_spartan_color(color);
		platform_log("menus: h3 profiles: the card's Spartan recoloured %d", color);
	}
	shown = h3_profiles.spartan_asked && menu_scene_spartan_shown();
	if (shown != h3_profiles.spartan_shown)
	{
		h3_profiles.spartan_shown = shown;
		platform_log("menus: h3 profiles: the card shows %s", shown ? "the Spartan" : "the swatch");
	}
}

/* (port h3 profile delete text: the question) its words */
static void h3_profile_delete_text_update(struct widget_instance *widget)
{
	char text[192];

	snprintf(text, sizeof(text), T("Delete the profile %s?\nIts campaign progress and saved game go with it."),
		h3_profiles.delete_name);
	text_show(widget, text);
}

/* the editor's value of a row, as the edit profile has it */
static void h3_custom_value(short row, struct player_profile const *profile, char *text, int size)
{
	struct player_profile_controller_settings const *settings = &profile->controller_settings;

	text[0] = 0;
	switch (row)
	{
	case _h3_custom_name:
		h3_profile_name(text, size, profile);
		break;
	case _h3_custom_color:
		snprintf(text, size, "%s", T(h3_color_names[h3_swatch_frame(profile->primary_color_index)]));
		break;
	case _h3_custom_model:
	{
		struct player_model model;

		if (player_models_get(h3_custom.model, &model))
			snprintf(text, size, "%s", model.name);
		else
			snprintf(text, size, "%s", T("SPARTAN"));
		h3_capitals(text);
		break;
	}
	case _h3_custom_stick:
		snprintf(text, size, "%s", T(h3_stick_names[PIN(settings->joystick_preset, 0, NUMBER_OF_JOYSTICK_PRESETS - 1)]));
		break;
	case _h3_custom_buttons:
		snprintf(text, size, "%s", T(h3_button_names[PIN(settings->button_preset, 0, NUMBER_OF_BUTTON_PRESETS - 1)]));
		break;
	case _h3_custom_look:
		snprintf(text, size, "%d", PIN(settings->look_sensitivity, 1, NUMBER_OF_LOOK_SENSITIVITY_SETTINGS));
		break;
	case _h3_custom_invert:
		snprintf(text, size, "%s", settings->invert_look ? T("YES") : T("NO"));
		break;
	case _h3_custom_flight:
		snprintf(text, size, "%s", settings->flight_stick_aircraft_controls ? T("YES") : T("NO"));
		break;
	case _h3_custom_autocenter:
		snprintf(text, size, "%s", settings->autocenter ? T("ON") : T("OFF"));
		break;
	case _h3_custom_vibration:
		snprintf(text, size, "%s", settings->vibration_disabled ? T("OFF") : T("ON"));
		break;
	}
}

static short h3_custom_row(struct widget_instance *widget)
{
	short row;

	for (row = 0; widget && widget->name && row < NUMBER_OF_H3_CUSTOM_ROWS; row++)
	{
		if (!strcmp(widget->name, h3_custom_rows[row]))
			return row;
	}
	return NONE;
}

/* a value stepped through (step 1 or -1, wrapping) */
static long h3_custom_step(long value, long count, int step)
{
	return ((value + step) % count + count) % count;
}

/* (port h3 custom next and previous: A, the d-pad's or the stick's right
and left on a row) the row's field of the edit profile changed as the
Xbox's settings change it (its spinners' choices: player_profile_change
_controller_settings, player_profile_change_advanced_controller_settings,
player_profile_color_picker_select_color); A on the name types it */
static boolean h3_custom_change(struct widget_instance *widget, int step)
{
	struct player_profile *profile = player_ui_get_edit_player_profile();
	struct player_profile_controller_settings *settings;
	short row = h3_custom_row(widget);
	char text[64];

	if (!profile || row == NONE)
		return FALSE;
	settings = &profile->controller_settings;
	h3_custom.status[0] = 0;
	switch (row)
	{
	case _h3_custom_name:
		h3_profile_name(text, sizeof(text), profile);
		typing_begin(_typing_profile_name, T("Player name"), text, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH - 1, FALSE);
		return TRUE;
	case _h3_custom_color:
		/* (none, then the game's colours: frame - 1) */
		profile->primary_color_index = (short)(h3_custom_step(h3_swatch_frame(profile->primary_color_index),
			H3_SWATCH_FRAMES, step) - 1);
		if (profile->primary_color_index < 0)
			profile->primary_color_index = NONE;
		break;
	case _h3_custom_model:
		/* (the Spartan, then each model found: index + 1) */
		h3_custom.model = (short)(h3_custom_step(h3_custom.model + 1, player_models_count() + 1, step) - 1);
		h3_custom.model_said[0] = 0;
		break;
	case _h3_custom_stick:
		settings->joystick_preset = (byte)h3_custom_step(PIN(settings->joystick_preset, 0, NUMBER_OF_JOYSTICK_PRESETS - 1),
			NUMBER_OF_JOYSTICK_PRESETS, step);
		break;
	case _h3_custom_buttons:
		settings->button_preset = (byte)h3_custom_step(PIN(settings->button_preset, 0, NUMBER_OF_BUTTON_PRESETS - 1),
			NUMBER_OF_BUTTON_PRESETS, step);
		break;
	case _h3_custom_look:
		/* (1 to 10, as the Xbox's spinner: its choice + 1) */
		settings->look_sensitivity = (byte)(h3_custom_step(PIN(settings->look_sensitivity, 1,
			NUMBER_OF_LOOK_SENSITIVITY_SETTINGS) - 1, NUMBER_OF_LOOK_SENSITIVITY_SETTINGS, step) + 1);
		break;
	case _h3_custom_invert:
		settings->invert_look = !settings->invert_look;
		break;
	case _h3_custom_flight:
		settings->flight_stick_aircraft_controls = !settings->flight_stick_aircraft_controls;
		break;
	case _h3_custom_autocenter:
		settings->autocenter = !settings->autocenter;
		break;
	case _h3_custom_vibration:
		settings->vibration_disabled = !settings->vibration_disabled;
		break;
	}
	h3_custom_value(row, profile, text, sizeof(text));
	platform_log("menus: h3 customize: %s %s", h3_custom_rows[row] + 3, text);
	return TRUE;
}

/* the live Spartan in the profile's colour, in the editor panel's window:
the scene packs' menu_spartan (menu_scene_objects.c) in front of the
editor's own camera (its screen's scene=), placed where the window is, made
again in the new colour as COLOR changes (color: the game's index; NONE, a
profile the host gives a colour, shows Halo's default, white). Nothing with
the scenes off (no camera of the editor's: the ring's flight would be
behind) or before the scene is known; the window keeps the colour's swatch
until the Spartan stands there (menu_scene_spartan_shown: its pack loaded,
built from the player's multiplayer map the first time). */
#define H3_CUSTOM_SPARTAN_PLACEMENT "1.53 0.59 -0.25 20"

static void h3_custom_spartan_preview(struct widget_instance *screen, short color)
{
	char name[64];
	char const *scene;
	short shown_color = color >= 0 ? color : 0;
	boolean shown;
	struct widget_instance *found;

	/* (the model chosen: its pack, built the first time it is shown) */
	menu_scene_spartan_model(player_models_pack(h3_custom.model));
	if (!h3_custom.preview_asked)
	{
		if (h3_scenes_enabled() && (scene = h3_scene_of(screen, name, sizeof(name), NULL)) != NULL &&
			menu_scene_spartan_show(scene, H3_CUSTOM_SPARTAN_PLACEMENT, shown_color))
		{
			h3_custom.preview_asked = TRUE;
			h3_custom.preview_color = shown_color;
			platform_log("menus: h3 customize: preview Spartan asked for in colour %d", shown_color);
		}
	}
	else if (shown_color != h3_custom.preview_color)
	{
		h3_custom.preview_color = shown_color;
		menu_scene_spartan_color(shown_color);
		platform_log("menus: h3 customize: preview Spartan recoloured %d", shown_color);
	}
	/* (the swatch in its panel until the Spartan stands in the window) */
	shown = h3_custom.preview_asked && menu_scene_spartan_shown();
	if ((found = named(screen, "preview_frame")) != NULL)
		widget_instance_set_visibility_recursive(found, !shown);
	if ((found = named(screen, "preview_swatch")) != NULL)
		widget_instance_set_visibility_recursive(found, !shown);
	if (shown != h3_custom.preview_shown)
	{
		h3_custom.preview_shown = shown;
		platform_log("menus: h3 customize: the preview shows %s", shown ? "the Spartan" : "the swatch");
	}
}

/* the preview's Spartan let go (the editor gone) */
static void h3_custom_spartan_hide(void)
{
	if (h3_custom.preview_asked)
		platform_log("menus: h3 customize: preview Spartan hidden");
	menu_scene_spartan_hide();
	menu_scene_spartan_model(NULL);
	h3_custom.preview_asked = FALSE;
	h3_custom.preview_shown = FALSE;
}

/* (port h3 custom: the editor's list) the rows' values, the focused row's
arrows and help, the colour's swatches and the preview, the profile's name,
what the panel's settings mean for it */
static void h3_custom_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct player_profile *profile = player_ui_get_edit_player_profile();
	struct widget_instance *found;
	short row, focused = 0;
	char text[192];

	if (!profile)
		return;
	if (typing_done(_typing_profile_name, text, sizeof(text)))
	{
		struct player_profile original;
		wchar_t name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH];

		if (!player_profile_get(h3_custom.index, &original))
			memcpy(&original, profile, sizeof(original));
		if (h3_profile_name_check(text, name, original.player_name, h3_custom.status, sizeof(h3_custom.status)))
		{
			memcpy(profile->player_name, name, sizeof(profile->player_name));
			platform_log("menus: h3 customize: name %s", text);
		}
		else
			platform_log("menus: h3 customize: the name \"%s\" was refused: %s", text, h3_custom.status);
	}
	for (row = 0; row < NUMBER_OF_H3_CUSTOM_ROWS; row++)
	{
		struct widget_instance *item = named(list, h3_custom_rows[row]);
		boolean has_focus = item && item == list->focused_child;

		if (!item)
			continue;
		if (has_focus)
			focused = row;
		h3_custom_value(row, profile, text, sizeof(text));
		text_show(named(item, "value"), text);
		item->animation.current_frame_index = has_focus ? 1 : 0;
		if ((found = named(item, "arrow_left")) != NULL)
			widget_instance_set_visibility_recursive(found, has_focus);
		if ((found = named(item, "arrow_right")) != NULL)
			widget_instance_set_visibility_recursive(found, has_focus);
	}
	h3_profile_name(text, sizeof(text), profile);
	h3_capitals(text);
	text_show(named(screen, "profile_name"), text);
	if ((found = named(screen, "swatch")) != NULL)
		found->animation.current_frame_index = h3_swatch_frame(profile->primary_color_index);
	if ((found = named(screen, "preview_swatch")) != NULL)
		found->animation.current_frame_index = h3_swatch_frame(profile->primary_color_index);
	h3_custom_spartan_preview(screen, profile->primary_color_index);
	/* (under the preview: the colour's name, or how the model's pack is
	coming: built the first time, or why it cannot be) */
	{
		struct player_model model;
		char line[192], why[160];
		long progress = 0;
		short state = menu_scene_spartan_state(&progress, why, sizeof(why));

		snprintf(line, sizeof(line), "%s", T(h3_color_names[h3_swatch_frame(profile->primary_color_index)]));
		if (player_models_get(h3_custom.model, &model))
		{
			if (state == _scene_pack_state_building)
				snprintf(line, sizeof(line), T("Building the model... %ld%%"), progress);
			else if (state == _scene_pack_state_failed)
				snprintf(line, sizeof(line), "%s", T("This model can't be built"));
			if (state == _scene_pack_state_failed && !h3_custom.status[0])
				snprintf(h3_custom.status, sizeof(h3_custom.status), T("%s can't be used: %s"), model.name, why);
			snprintf(why, sizeof(why), "%s: %s", model.name, scene_packs_state_name(state));
			if (strcmp(why, h3_custom.model_said) && state != _scene_pack_state_building)
			{
				snprintf(h3_custom.model_said, sizeof(h3_custom.model_said), "%s", why);
				platform_log("menus: h3 customize: model %s", why);
			}
		}
		text_show(named(screen, "preview_name"), line);
	}
	string_show(named(screen, "custom_help"), focused);
	/* (the settings panel's Invert look flips the stick again, on this
	Vita: with both on, said) */
	if (!h3_custom.status[0] && profile->controller_settings.invert_look && atoi(setting_get("XV_INVERT_Y", "0")))
	{
		text_show(named(screen, "status"),
			T("The settings panel's Invert look is on too: on this PS Vita the two cancel out."));
	}
	else
		text_show(named(screen, "status"), h3_custom.status);
	{
		struct player_profile_controller_settings const *settings = &profile->controller_settings;
		char said[256];

		h3_profile_name(text, sizeof(text), profile);
		snprintf(said, sizeof(said), "%s: color %d, stick %d, buttons %d, look %d, invert %d, flight %d, autocenter %d, "
			"vibration %s", text, profile->primary_color_index, settings->joystick_preset, settings->button_preset,
			settings->look_sensitivity, settings->invert_look, settings->flight_stick_aircraft_controls,
			settings->autocenter, settings->vibration_disabled ? "off" : "on");
		if (strcmp(said, h3_custom.said))
		{
			snprintf(h3_custom.said, sizeof(h3_custom.said), "%s", said);
			platform_log("menus: h3 customize: now %s", said);
		}
	}
}

/* (port h3 custom save: Circle, back) the changes saved as the Xbox's
"player profile save changes" saves them (player_ui_save_profile: the
profile's file and its SaveMeta.xbx name), when there are any; the profile
in use then the saved one, its controls applied */
static void h3_custom_save(void)
{
	struct player_profile *edit = player_ui_get_edit_player_profile();
	struct player_profile saved;
	char name[48];

	if (!edit)
		return;
	memcpy(&saved, edit, sizeof(saved));
	h3_profile_name(name, sizeof(name), &saved);
	/* (port: the player model, kept in the port's own file, not the
	profile's: player_models.c) */
	if (h3_custom.model != h3_custom.model_saved)
	{
		boolean kept = player_models_profile_choose(h3_custom.index, h3_custom.model);

		platform_log("menus: h3 customize: %s: model %d %s", name, h3_custom.model, kept ? "saved" : "could not be saved");
		if (kept)
			h3_custom.model_saved = h3_custom.model;
	}
	if (!player_ui_edit_profile_is_dirty())
	{
		player_ui_end_editing_profile();
		platform_log("menus: h3 customize: %s: no changes", name);
		return;
	}
	if (!player_ui_save_profile())
	{
		platform_log("menus: h3 customize: %s could not be saved", name);
		return;
	}
	if (h3_profile_same(player_ui_get_active_player_profile_index(0), h3_custom.index))
		player_ui_set_active_player_profile(0, player_ui_get_active_player_profile_index(0), &saved);
	h3_profiles.focus_index = h3_custom.index;
	/* (the nameplate shows it, its new name and colour, at once) */
	h3_nameplate_changed(h3_custom.index);
	platform_log("menus: h3 customize: saved %s: color %d, stick %d, buttons %d, look %d, invert %d, flight %d, "
		"autocenter %d, vibration %s", name, saved.primary_color_index, saved.controller_settings.joystick_preset,
		saved.controller_settings.button_preset, saved.controller_settings.look_sensitivity,
		saved.controller_settings.invert_look, saved.controller_settings.flight_stick_aircraft_controls,
		saved.controller_settings.autocenter, saved.controller_settings.vibration_disabled ? "off" : "on");
}

/* ---- the Halo 3 multiplayer pause screen (halo3/mppause.xml): it stands
for the Xbox's 1p_pause_game; its quit is the Xbox's own function */

/* the pause's rows, in the list's order */
enum
{
	_h3_mp_pause_resume,
	_h3_mp_pause_players,
	_h3_mp_pause_settings,
	_h3_mp_pause_party,
	_h3_mp_pause_leave,
	_h3_mp_pause_end,
	NUMBER_OF_H3_MP_PAUSE_ROWS
};
#define H3_MP_PAUSE_ROW_SPACING 24
/* (SETTINGS: ALL SETTINGS, the list's last row, and its place under the
profile's rows) */
#define H3_MP_SETTINGS_PANEL_ROW 7
#define H3_MP_SETTINGS_PANEL_Y 146

static struct
{
	char said[256];
	/* (SETTINGS) the local player whose profile is edited, the profile, and
	what the screen last said */
	short local_player_index;
	long profile_index;
	char settings_said[256];
} h3_mp_pause;

/* the game type's name, as the game's lobby says it: the variant's own,
else the game's */
static void h3_mp_game_name(struct network_game const *game, char *out, int size)
{
	wchar_t wide[NUMBEROF(game->variant.human_readable_game_description) + 1];
	long index;

	for (index = 0; index < (long)NUMBEROF(game->variant.human_readable_game_description) &&
		game->variant.human_readable_game_description[index]; index++)
		wide[index] = game->variant.human_readable_game_description[index];
	wide[index] = 0;
	h3_utf8(out, size, wide);
	if (!out[0])
		snprintf(out, (size_t)size, "%s", T("Slayer"));
}

/* the local player a screen is for (its controller's: a multiplayer game's
local players are their controllers') */
static short h3_mp_local_player(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);

	return screen ? (short)PIN(screen->local_player_index, 0, MAXIMUM_LOCAL_PLAYERS - 1) : 0;
}

/* (port h3 mp pause: the pause's list) the title (game type and map), the
score to win, the players, the game's panel; the rows shown (PARTY in a
party only, LEAVE GAME or END GAME as this machine joined or hosts the
game) one under another, the column behind them as long */
static void h3_mp_pause_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child;
	struct network_game *game = network_game_get_game();
	struct p2p_party_member party[P2P_PARTY_MAXIMUM];
	boolean host = global_network_game_server_get() != NULL;
	/* (HALO_TEST_MP_PAUSE_PARTY: as in a party, its row shown: tests) */
	boolean in_party = p2p_party_members(party, P2P_PARTY_MAXIMUM) > 1 || getenv("HALO_TEST_MP_PAUSE_PARTY");
	char name[64], map[96], text[320], line[384];
	short row, shown = 0, base = list->child ? list->child->vertical_offset : 0;

	name[0] = map[0] = 0;
	if (game)
	{
		h3_mp_game_name(game, name, (int)sizeof(name));
		snprintf(map, sizeof(map), "%s", custom_edition_maps_level_title(game->map.name));
	}
	snprintf(text, sizeof(text), "%s \xc2\xb7 %s", name, map);
	h3_capitals(text);
	text_show(named(screen, "title"), text);
	if (game)
	{
		snprintf(text, sizeof(text), T("%s, score to win %ld"), game->variant.universal_variant.teams ?
			T("Team game") : T("Free for all"), (long)game->variant.universal_variant.score_to_win);
		text_show(named(screen, "rules"), text);
		snprintf(text, sizeof(text), game->player_count == 1 ? T("%d Player") : T("%d Players"), game->player_count);
		text_show(named(screen, "count"), text);
		snprintf(text, sizeof(text), T("%s on %s"), name, map);
		snprintf(text + strlen(text), sizeof(text) - strlen(text), "\n%s\n\n%s", host ?
			T("You host this game.") : T("You joined this game."), host ?
			T("END GAME ends it for every player.") : T("LEAVE GAME: the others play on."));
		text_show(named(screen, "game"), text);
	}
	for (row = 0, child = list->child; child; child = child->next, row++)
	{
		boolean visible = row != _h3_mp_pause_party || in_party;

		if (row == _h3_mp_pause_leave)
			visible = !host;
		else if (row == _h3_mp_pause_end)
			visible = host;
		widget_instance_set_visibility_recursive(child, visible);
		child->disabled = !visible;
		if (visible)
			child->vertical_offset = (short)(base + shown++ * H3_MP_PAUSE_ROW_SPACING);
	}
	if ((child = named(screen, "column_4")) != NULL)
		widget_instance_set_visibility_recursive(child, shown <= 4);
	if ((child = named(screen, "column_5")) != NULL)
		widget_instance_set_visibility_recursive(child, shown > 4);
	/* (said once each time it changes: the tests read it) */
	snprintf(line, sizeof(line), "%s on %s, %s, %s, %d players; rows: resume, players, settings%s, %s", name, map,
		game && game->variant.universal_variant.teams ? "teams" : "free for all", host ? "the host" : "joined",
		game ? game->player_count : 0, in_party ? ", party" : "", host ? "end game" : "leave game");
	if (strcmp(line, h3_mp_pause.said))
	{
		snprintf(h3_mp_pause.said, sizeof(h3_mp_pause.said), "%s", line);
		platform_log("menus: h3 mp pause: %s", line);
	}
}

/* (port h3 mp settings init: SETTINGS opened) the profile the local player
plays with, edited as CUSTOMIZATION edits it (h3_custom_change steps its
rows); none to edit without one */
static void h3_mp_settings_initialize(struct widget_instance *widget)
{
	short local_player_index = h3_mp_local_player(widget);
	long profile_index = player_ui_get_active_player_profile_index(local_player_index);

	h3_mp_pause.local_player_index = local_player_index;
	h3_mp_pause.profile_index = NONE;
	h3_mp_pause.settings_said[0] = 0;
	h3_custom.status[0] = 0;
	if (player_ui_get_edit_player_profile())
		player_ui_end_editing_profile();
	if (profile_index != NONE)
	{
		player_ui_begin_editing_profile(profile_index);
		if (player_ui_get_edit_player_profile())
			h3_mp_pause.profile_index = profile_index;
	}
	h3_custom.index = h3_mp_pause.profile_index;
	platform_log("menus: h3 mp settings: local player %d, profile %s", local_player_index,
		h3_mp_pause.profile_index != NONE ? "edited" : "none (only ALL SETTINGS)");
}

/* (port h3 mp settings: its list) the rows' values (CUSTOMIZATION's), the
focused row's arrows and help, the profile's name; without a profile its
rows hidden, ALL SETTINGS alone */
static void h3_mp_settings_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct player_profile *profile = player_ui_get_edit_player_profile();
	struct widget_instance *child, *found;
	short index, focused = 0, base = list->child ? list->child->vertical_offset : 0;
	char text[192], said[256];

	if (h3_mp_pause.profile_index == NONE)
		profile = NULL;
	/* (no profile: ALL SETTINGS alone, at the top, focused) */
	if (!profile && list->focused_child && list->focused_child->disabled)
		h3_focus_child(list, H3_MP_SETTINGS_PANEL_ROW);
	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		short row = h3_custom_row(child);
		boolean has_focus = child == list->focused_child;

		if (index == H3_MP_SETTINGS_PANEL_ROW)
			child->vertical_offset = (short)(base + (profile ? H3_MP_SETTINGS_PANEL_Y : 0));

		if (has_focus)
			focused = index;
		if (row != NONE)
		{
			widget_instance_set_visibility_recursive(child, profile != NULL);
			child->disabled = profile == NULL;
			if (!profile)
				continue;
			h3_custom_value(row, profile, text, sizeof(text));
			text_show(named(child, "value"), text);
			if ((found = named(child, "arrow_left")) != NULL)
				widget_instance_set_visibility_recursive(found, has_focus);
			if ((found = named(child, "arrow_right")) != NULL)
				widget_instance_set_visibility_recursive(found, has_focus);
		}
		child->animation.current_frame_index = has_focus ? 1 : 0;
	}
	string_show(named(screen, "custom_help"), focused);
	if (profile)
	{
		h3_profile_name(text, sizeof(text), profile);
		h3_capitals(text);
	}
	else
		snprintf(text, sizeof(text), "%s", T("No profile: these are a profile's settings"));
	text_show(named(screen, "profile_name"), text);
	if (profile && !h3_custom.status[0] && profile->controller_settings.invert_look &&
		atoi(setting_get("XV_INVERT_Y", "0")))
	{
		text_show(named(screen, "status"),
			T("The settings panel's Invert look is on too: on this PS Vita the two cancel out."));
	}
	else
		text_show(named(screen, "status"), h3_custom.status);
	if (profile)
	{
		struct player_profile_controller_settings const *settings = &profile->controller_settings;

		snprintf(said, sizeof(said), "stick %d, buttons %d, look %d, invert %d, flight %d, autocenter %d, vibration %s",
			settings->joystick_preset, settings->button_preset, settings->look_sensitivity, settings->invert_look,
			settings->flight_stick_aircraft_controls, settings->autocenter, settings->vibration_disabled ? "off" : "on");
	}
	else
		snprintf(said, sizeof(said), "no profile");
	if (strcmp(said, h3_mp_pause.settings_said))
	{
		snprintf(h3_mp_pause.settings_said, sizeof(h3_mp_pause.settings_said), "%s", said);
		platform_log("menus: h3 mp settings: now %s", said);
	}
}

/* (port h3 mp settings save: Circle) the changes saved to the profile as
CUSTOMIZATION's Circle saves them (player_ui_save_profile), and the local
player's controls made the saved profile's at once
(player_ui_set_active_player_profile: the Xbox's way) */
static void h3_mp_settings_save(void)
{
	struct player_profile *edit = player_ui_get_edit_player_profile();
	struct player_profile saved;

	if (!edit || h3_mp_pause.profile_index == NONE)
		return;
	memcpy(&saved, edit, sizeof(saved));
	if (!player_ui_edit_profile_is_dirty())
	{
		player_ui_end_editing_profile();
		platform_log("menus: h3 mp settings: no changes");
		return;
	}
	if (!player_ui_save_profile())
	{
		platform_log("menus: h3 mp settings: the profile could not be saved");
		return;
	}
	if (player_ui_get_active_player_profile_index(h3_mp_pause.local_player_index) == h3_mp_pause.profile_index)
		player_ui_set_active_player_profile(h3_mp_pause.local_player_index, h3_mp_pause.profile_index, &saved);
	platform_log("menus: h3 mp settings: saved and played: stick %d, buttons %d, look %d, invert %d, flight %d, "
		"autocenter %d, vibration %s", saved.controller_settings.joystick_preset, saved.controller_settings.button_preset,
		saved.controller_settings.look_sensitivity, saved.controller_settings.invert_look,
		saved.controller_settings.flight_stick_aircraft_controls, saved.controller_settings.autocenter,
		saved.controller_settings.vibration_disabled ? "off" : "on");
}

/* the party's leader warned of a game with fewer open slots than the party
(by its id): nonzero if the same game was warned of a moment ago (a second
press joins it); else remembered now */
static boolean party_warning_confirmed(char const *id);

/* ---- joining */

static void join_begin(struct p2p_lobby_entry const *entry, char const *password)
{
	/* (one join for one press: the press that answers the large game's
	question reached the browser too, as Cross on the same row, and joined
	the game a second time 14 ms after the answer's join - the owner's
	insider.15 log -; the same game begun again within two seconds is that
	press) */
	static char begun_id[sizeof(entry->id)];
	static unsigned long begun_at;
	unsigned long now = system_milliseconds();

	if (begun_at && now - begun_at < 2000 && !strcmp(begun_id, entry->id))
	{
		platform_log("menus: the public game %s: joining already (the same press)", entry->name);
		return;
	}
	snprintf(begun_id, sizeof(begun_id), "%s", entry->id);
	begun_at = now ? now : 1;
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
		/* (the Halo 3 games browser searches already: the host's game is
		joined there once its list shows it, menu_functions_lan_join_listed) */
		if (menu_functions.lan)
		{
			platform_log("menus: the public game's invite handed on; joined from the games browser once listed");
			return;
		}
		/* (the profile chosen before the browser: the System Link list at
		once, where the host's game is joined once listed) */
		if (menu_functions.browser_profile_chosen)
		{
			menu_functions.open_system_link_list = TRUE;
			platform_log("menus: the public game's invite handed on; the System Link list next (the profile chosen)");
			return;
		}
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
static boolean lan_with_online(void);

/* a browser row's column (row_name, row_game, row_map, row_players): its
text cut to the column's width with "..." when wider - a line wider than its
box draws nothing at all (a long single word: a PC server's name, a custom
map's), and runs into the next column else -, "-" when there is none, and a
character the menus' fonts do not draw (beyond Latin-1) as "?". TRUE when
it was cut */
static boolean row_column(struct widget_instance *row, char const *name, char const *text)
{
	struct widget_instance *widget = named(row, name);
	unsigned short characters[128];
	long length, index;
	short width;
	boolean cut = FALSE;

	if (!widget || widget->type != _ui_widget_type_text_box)
		return FALSE;
	length = halo_menus_utf16(text && *text ? text : "-", characters, (long)NUMBEROF(characters));
	for (index = 0; index < length - 1; index++)
	{
		if (characters[index] > 0xFF)
			characters[index] = '?';
	}
	width = ui_widget_port_text_box_width(widget);
	if (width > 0 && length > 1 && ui_widget_port_text_too_wide(widget, (wchar_t const *)characters, width))
	{
		/* (the start, and "...", as much as fits) */
		long kept;

		for (kept = MIN(length - 1, (long)NUMBEROF(characters) - 4); kept > 1; kept--)
		{
			characters[kept] = '.';
			characters[kept + 1] = '.';
			characters[kept + 2] = '.';
			characters[kept + 3] = 0;
			if (!ui_widget_port_text_too_wide(widget, (wchar_t const *)characters, width))
				break;
		}
		length = kept + 4;
		cut = TRUE;
	}
	widget->parameters.text_box.text = ui_widget_realloc(widget->parameters.text_box.text,
		(word)(length * sizeof(wchar_t)), __FILE__, __LINE__);
	if (widget->parameters.text_box.text)
		memcpy(widget->parameters.text_box.text, characters, length * sizeof(wchar_t));
	return cut;
}

/* (automated tests only, never in release builds) HALO_TEST_BROWSER_GAMES=N:
N made-up games after those listed, for the browser's scrolling and sorts
(run_netns_online_test.sh browserscroll); not joinable. With
HALO_TEST_BROWSER_PC=1 too, both platforms (run_menus_test.sh h3browser):
every second one a PC game (its badge PC, as the crossplay wire's), every
third from the third one with a password (not in the games browser without
the online list: System Link has none; run_menus_test.sh h3lan). With
HALO_TEST_BROWSER_VITA_PC=1 as well, every sixth Vita one from the fifth the
PC build's (VITA (PC)) and every tenth one for Vitas only
(run_menus_test.sh h3vitapc) */
static void browser_test_games(void)
{
#ifndef HALO_RELEASE
	static char const *const maps[] = { "Blood Gulch", "Wizard", "Hang 'Em High", "Prisoner", "Chill Out" };
	static char const *const gametypes[] = { "Slayer", "CTF", "King", "Oddball" };
	char const *value = getenv("HALO_TEST_BROWSER_GAMES");
	char const *both = getenv("HALO_TEST_BROWSER_PC");
	char const *vita_pc = getenv("HALO_TEST_BROWSER_VITA_PC");
	long count = value ? atol(value) : 0, index;

	/* (HALO_TEST_BROWSER_ODD=1: the odd listings, made into entries as the
	real ones are, p2p_lobby.c) */
	menu_functions.game_count += p2p_lobby_test_entries(menu_functions.games + menu_functions.game_count,
		P2P_LOBBY_MAXIMUM_ENTRIES - (int)menu_functions.game_count);

	for (index = 0; index < count && menu_functions.game_count < P2P_LOBBY_MAXIMUM_ENTRIES; index++)
	{
		struct p2p_lobby_entry *entry = &menu_functions.games[menu_functions.game_count++];

		memset(entry, 0, sizeof(*entry));
		/* (the games browser without the online list: as its own rows) */
		snprintf(entry->id, sizeof(entry->id), "%stest%02ld", menu_functions.lan && !lan_with_online() ? LAN_ID_PREFIX :
			"", index);
		snprintf(entry->name, sizeof(entry->name), "Test game %02ld", index);
		/* (HALO_TEST_BROWSER_NAMES=1: names of every sort of letter, as a
		public list's are - run_menus_test.sh browserrows: each row's name,
		map and game drawn, with all the glyphs a screen of them takes) */
		if (getenv("HALO_TEST_BROWSER_NAMES") && atoi(getenv("HALO_TEST_BROWSER_NAMES")))
		{
			static char const *const names[] =
			{
				"Stink Fist", "DONT SKIP CINE.", "Krazy's COOP", "JACKDAWS LOVE MY BIG SPHINX OF QUARTZ",
				"sphinx of black quartz, judge my vow", "Warthog Debug 1", "jesus", "PACK MY BOX WITH FIVE DOZEN JUGS",
				"GUNGAME++ 24*7 = 168h!", "MLGTS 2v2 US3 @ 60Hz", "the quick brown fox jumps over", "ELMEME",
				"Quiet Zebra Vaults %&$", "#1 xQuJoKe [V] <3", "Bumpy Wombat (EU) 0-9: 0123456789", "~Yeti_Kraken~ {/\\|}",
			};
			static char const *const custom_maps[] =
			{
				"airborne_aco", "The Truth and Reconciliation", "hugeass@ce", "Yoyorast_Island@ce", "bigassv2,104", "Extinction",
			};

			snprintf(entry->name, sizeof(entry->name), "%s", names[index % NUMBEROF(names)]);
			/* (and the rules and players a public list's carry, whole: the
			ticker's texts beside the rows) */
			snprintf(entry->rules, sizeof(entry->rules), "Slayer to 50 on %s: under way (DEDICATED)",
				maps[index % 5]);
			snprintf(entry->players_line, sizeof(entry->players_line),
				"%ld of 16: Krazy, Stink Fist, xQuJoKe, Yeti_Kraken, Wombat +%ld more", index * 7 % 16, index % 9);
			snprintf(entry->map, sizeof(entry->map), "%s", index % 3 ? maps[index % 5] : custom_maps[index / 3 % NUMBEROF(custom_maps)]);
		}
		else
			snprintf(entry->map, sizeof(entry->map), "%s", maps[index % 5]);
		snprintf(entry->gametype, sizeof(entry->gametype), "%s", gametypes[index % 4]);
		entry->players = (int)(index * 7 % 16);
		entry->maximum = 16;
		entry->open = entry->compatible = 1;
		snprintf(entry->tag, sizeof(entry->tag), "%s", crossplay_guard_tag(CROSSPLAY_PLATFORM_VITA));
		snprintf(entry->badge, sizeof(entry->badge), "%s", entry->tag);
		if (both && atoi(both))
		{
			/* (every second one a PC game, OpenCE's or ChupathingyCE's in
			turn; every fourth Vita one for Vitas and PC) */
			if (index % 2)
			{
				entry->platform = index % 4 == 1 ? CROSSPLAY_PLATFORM_OPENCE : CROSSPLAY_PLATFORM_CHUPA;
				entry->crossplay = 1;
			}
			else
				entry->crossplay = index % 4 == 2;
			/* (and every sixth Vita one the PC build's, VITA (PC); every
			tenth one for Vitas only) */
			if (entry->platform == CROSSPLAY_PLATFORM_VITA && vita_pc && atoi(vita_pc))
				entry->marks = (index % 6 == 4 ? CROSSPLAY_HOST_PC_BUILD : 0) |
					(index % 10 == 0 && !entry->crossplay ? CROSSPLAY_HOST_VITAS_ONLY : 0);
			snprintf(entry->tag, sizeof(entry->tag), "%s", crossplay_guard_host_tag(entry->platform, entry->marks));
			crossplay_guard_host_badge(entry->badge, (int)sizeof(entry->badge), entry->platform, entry->marks,
				entry->crossplay, 0, CROSSPLAY_GUARD_OK, network_wire_this_platform() == NETWORK_PLATFORM_VITA_PC);
			entry->locked = index % 3 == 2 && (!menu_functions.lan || lan_with_online());
		}
		/* (the filter's, as the real lists') */
		if (!network_wire_browser_shows(crossplay_guard_vita_game(entry->platform, entry->marks)))
			menu_functions.game_count--;
	}
#endif
}

/* the games the browser lists, its header rows (browser_list_group) not
counted */
static long browser_games(void)
{
	return menu_functions.game_count - browser_list_header_count(menu_functions.games, (int)menu_functions.game_count);
}

/* the games the rows show, as the player counts them (1 on; the header
rows not counted): the first and the last */
static long browser_shown_first(void)
{
	long offset = menu_functions.view.offset;

	return offset + 1 - browser_list_header_count(menu_functions.games, (int)offset);
}

static long browser_shown_last(void)
{
	long end = menu_functions.view.offset + menu_functions.entry_count;

	return end - browser_list_header_count(menu_functions.games, (int)end);
}

/* the browsers' filter (network.browser_filter, "Browser shows": the
settings panel's, and the D-pad's left and right on the browsers): its
name as the hint says it */
static char const *browser_filter_name(void)
{
	switch (network_wire_browser_filter())
	{
	case NETWORK_WIRE_FILTER_VITA:
		return T("Vita games");
	case NETWORK_WIRE_FILTER_PC:
		return T("PC games");
	}
	return T("All games");
}

/* the groups as they change, in the log (the tests read it): the headers'
names in their order */
static void browser_groups_log(void)
{
	static char said[96];
	char line[96] = "";
	long index;

	for (index = 0; index < menu_functions.game_count; index++)
	{
		size_t length = strlen(line);

		if (menu_functions.games[index].header)
			snprintf(line + length, sizeof(line) - length, "%s%s", length ? ", " : "", menu_functions.games[index].name);
	}
	if (strcmp(line, said))
	{
		snprintf(said, sizeof(said), "%s", line);
		platform_log("menus: the browser's groups (%s): %s", browser_filter_name(), line[0] ? line : "none");
	}
}

/* ---- the Halo 3 LAN browser (halo3/network.xml): the server browser's
screen and rules over the games the System Link list shows - the game's own
discovery (network_client_manager.c: its advertisements, every field
checked there, network_game_client_listed_games) - with BROWSE GAMES on
SYSTEM LINK or AD HOC */

/* whether a browser row is the System Link list's (its id "lan:" and the
advertisement's) or the online list's */
static boolean lan_entry(struct p2p_lobby_entry const *entry)
{
	return !strncmp(entry->id, LAN_ID_PREFIX, sizeof(LAN_ID_PREFIX) - 1);
}

/* whether the games browser lists the online games too: internet play runs
(the connection Online) */
static boolean lan_with_online(void)
{
	return menu_functions.lan && internet_play();
}

/* a scenario's name as the game lists show it: an Xbox map's or campaign
level's title, else the name itself */
static char const *lan_map_title(char const *map)
{
	short index;

	for (index = 0; index < H3_MAP_COUNT; index++)
	{
		char const *leaf = strrchr(h3_map_paths[index], '\\');

		if (leaf && !_stricmp(leaf + 1, map))
			return h3_map_names[index];
	}
	for (index = 0; index < NUMBER_OF_SINGLE_PLAYER_LEVELS; index++)
	{
		char const *leaf = strrchr(h3_level_paths[index], '\\');

		if (leaf && !_stricmp(leaf + 1, map))
			return h3_level_names[index];
	}
	return map;
}

/* the browser's games: the System Link list's, as the online browser's
entries (p2p_lobby_entry: the same rows, sorts and lines) - each one's id
"lan:" and its advertisement's, the name and map as checked, the gametype
the game engine's name, the players; the badge VITA, or PC for a PC
build's game (a crossplay host's, with its badges) - then, with the
connection Online, every public game the online list has (p2p_lobby.c's,
checked there) */
static void lan_entries(void)
{
	static char const *const engines[] = { "", "CTF", "Slayer", "Oddball", "King of the Hill", "Race" };
	static char const *const difficulties[] = { "Easy", "Normal", "Heroic", "Legendary" };
	/* (the System Link list holds 9 at most: network_client_manager.c) */
	static struct network_listed_game listed[16];
	short count = network_game_client_listed_games(listed, (short)NUMBEROF(listed));
	short index;

	menu_functions.game_count = 0;
	for (index = 0; index < count; index++)
	{
		struct network_listed_game const *game = &listed[index];
		struct p2p_lobby_entry *entry = &menu_functions.games[menu_functions.game_count++];
		int length;

		memset(entry, 0, sizeof(*entry));
		snprintf(entry->id, sizeof(entry->id), "%s%s", LAN_ID_PREFIX, game->id);
		snprintf(entry->name, sizeof(entry->name), "%s", game->name);
		snprintf(entry->map, sizeof(entry->map), "%s", lan_map_title(game->map));
		entry->coop = game->cooperative;
		entry->difficulty = game->cooperative ? game->difficulty : -1;
		snprintf(entry->gametype, sizeof(entry->gametype), "%s", entry->coop ? T("Co-op") :
			game->engine_type >= 0 && game->engine_type < (short)NUMBEROF(engines) ? engines[game->engine_type] : "");
		entry->players = game->players;
		entry->maximum = game->maximum;
		entry->open = game->open;
		entry->has_teams = game->has_teams;
		entry->compatible = 1;
		/* (every row's tag, as the online browser's: VITA for a Vita's game -
		"+PC" when PC players can join it too - or the PC build's, with its
		badges, a PC co-op game Experimental (no game engine, as OpenCE hosts
		co-op); a PC build of this tree's games are the PCs') */
		entry->crossplay = game->crossplay;
		entry->platform = game->platform;
		entry->marks = game->marks;
#ifndef HALO_PORT_VITA_NETWORK
		entry->platform = CROSSPLAY_PLATFORM_PC;
		entry->marks = 0;
#endif
		if (game->pc_coop)
			entry->coop = TRUE;
		{
			int verdict = entry->platform == CROSSPLAY_PLATFORM_VITA ? CROSSPLAY_GUARD_OK :
				crossplay_guard_judge(entry->coop, entry->players, entry->maximum);

			entry->experimental = verdict != CROSSPLAY_GUARD_OK;
			snprintf(entry->tag, sizeof(entry->tag), "%s", crossplay_guard_host_tag(entry->platform, entry->marks));
			crossplay_guard_host_badge(entry->badge, (int)sizeof(entry->badge), entry->platform, entry->marks,
				entry->crossplay, 0, verdict, network_wire_this_platform() == NETWORK_PLATFORM_VITA_PC);
		}
		/* the Rules line, as the online browser's: "Slayer on Blood Gulch",
		"Co-op: The Pillar of Autumn, Heroic", ": full or starting" */
		if (entry->coop)
		{
			length = snprintf(entry->rules, sizeof(entry->rules), T("Co-op: %s%s%s"), entry->map, ", ",
				T(difficulties[PIN(game->difficulty, 0, 3)]));
		}
		else
		{
			length = snprintf(entry->rules, sizeof(entry->rules), T("%s on %s"), entry->gametype[0] ? entry->gametype :
				T("A game"), entry->map);
		}
		length = PIN(length, 0, (int)sizeof(entry->rules) - 1);
		if (!entry->open)
			snprintf(entry->rules + length, sizeof(entry->rules) - (size_t)length, "%s", T(": full or starting"));
		snprintf(entry->players_line, sizeof(entry->players_line), T("%d of %d"), entry->players, entry->maximum);
	}
	/* (Online: the public games after them; their joins watched as the
	server browser's) */
	if (lan_with_online())
	{
		join_watch();
		browser_large_watch();
		menu_functions.lan_online_count = p2p_lobby_entries(menu_functions.games + menu_functions.game_count,
			P2P_LOBBY_MAXIMUM_ENTRIES - (int)menu_functions.game_count);
		menu_functions.game_count += menu_functions.lan_online_count;
	}
	else
		menu_functions.lan_online_count = 0;
}

/* (each frame) the browser's title and status: SYSTEM LINK GAMES or AD HOC
GAMES, how many games (looking for a few seconds after it opens), and with
none, why, in the list's space; the list logged as it changes */
static void lan_status(struct widget_instance *screen, char *line, int line_size, char *message, int message_size)
{
	boolean adhoc = config_boolean("network.adhoc") != 0;
	boolean looking = system_milliseconds() - menu_functions.lan_since < LAN_LOOKING_MILLISECONDS;
	char said[192];
	long index;

	boolean online = lan_with_online();
	long games = browser_games(), shown;

	text_show(named(screen, "lan_title"), adhoc ? T("AD HOC GAMES") : online ? T("CUSTOM GAMES") : T("SYSTEM LINK GAMES"));
	message[0] = 0;
	if (menu_functions.status[0])
		snprintf(line, line_size, "%s", menu_functions.status);
	else if (!games)
		snprintf(line, line_size, "%s", looking ? T("Looking for games...") : T("No games found"));
	else if (online)
	{
		long lan_count = 0;

		for (index = 0; index < menu_functions.game_count; index++)
			lan_count += lan_entry(&menu_functions.games[index]);
		snprintf(line, line_size, T("%ld on your network, %ld online"), lan_count, games - lan_count);
	}
	else if (games == 1)
		snprintf(line, line_size, "%s", adhoc ? T("1 game near you") : T("1 game on your network"));
	else
		snprintf(line, line_size, adhoc ? T("%ld games near you") : T("%ld games on your network"), games);
	if (menu_functions.game_count > BROWSER_ROWS && !menu_functions.status[0])
	{
		size_t length = strlen(line);

		snprintf(line + length, line_size - length, ", %ld-%ld", browser_shown_first(), browser_shown_last());
	}
	if (menu_functions.game_count && !menu_functions.status[0])
	{
		static char const *const sorts[NUMBER_OF_BROWSER_SORTS] = { "by players", "by name", "by map", "by gametype" };
		size_t length = strlen(line);

		snprintf(line + length, line_size - length, ", %s", T(sorts[menu_functions.sort]));
	}
	if (!games && !looking)
	{
		char why[96];

		/* (Online: why there is no public game either - the brokers out of
		reach - else none on the network) */
		if (online && p2p_lobby_browse_status(why, (int)sizeof(why)) == P2P_LOBBY_BROWSE_UNREACHABLE)
			snprintf(message, message_size, "%s", why);
		else
			snprintf(message, message_size, "%s", adhoc ? T("No games found near you") : T("No games found on your network"));
	}
	/* (the list as it changes: the tests read it) */
	snprintf(said, sizeof(said), "%ld", games);
	for (index = 0, shown = 0; index < menu_functions.game_count && shown < 4; index++)
	{
		struct p2p_lobby_entry const *entry = &menu_functions.games[index];
		size_t length = strlen(said);

		if (entry->header)
			continue;
		snprintf(said + length, sizeof(said) - length, "%s%s%s (%s, %s, %d/%d, %s)", shown++ ? ", " : ": ",
			online && lan_entry(entry) ? "[LAN] " : "", entry->name, entry->map, entry->gametype, entry->players,
			entry->maximum, entry->badge[0] ? entry->badge : "?");
	}
	/* (on a change, at most once in BROWSER_LOG_MILLISECONDS: the players'
	counts change all the time online, and an insider.10 tester's log had
	the list 138 times, 50 games each, as the browser filled) */
	if (strcmp(said, menu_functions.lan_said) && browser_log_due(&menu_functions.lan_said_at))
	{
		snprintf(menu_functions.lan_said, sizeof(menu_functions.lan_said), "%s", said);
		platform_log("menus: the LAN browser lists %s", said);
	}
}

/* A on a row: its game, if still listed, joined as A on its row in the
System Link list joins it (the game's checks; its lobby next, the browser
deleted: *widget_deleted) */
static boolean lan_join(struct widget_instance *widget, boolean *widget_deleted);

static void browser_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focused = list->focused_child, *focus = NULL;
	char line[96], message[128];
	long index, focused_row = NONE, chosen = NONE, target;

	if (menu_functions.lan)
		lan_entries();
	else
	{
		join_watch();
		browser_large_watch();
		menu_functions.game_count = p2p_lobby_entries(menu_functions.games, P2P_LOBBY_MAXIMUM_ENTRIES);
	}
	browser_test_games();
	browser_list_sort(menu_functions.games, (int)menu_functions.game_count, menu_functions.sort);
	/* (the groups: VITA GAMES, then PC GAMES, each its header with its count) */
	menu_functions.game_count = browser_list_group(menu_functions.games, (int)menu_functions.game_count,
		(int)NUMBEROF(menu_functions.games), T("VITA GAMES (%d)"), T("PC GAMES (%d)"));
	browser_groups_log();
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
	target = browser_view_follow_groups(&menu_functions.view, menu_functions.games, BROWSER_ROWS,
		(int)menu_functions.game_count, (int)focused_row, (int)chosen, menu_functions.page);
	menu_functions.page = 0;
	menu_functions.entry_count = menu_functions.view.visible;
	for (index = 0; index < menu_functions.entry_count; index++)
		menu_functions.entries[index] = menu_functions.games[menu_functions.view.offset + index];
	/* the rows: one for each game shown, the rest hidden */
	for (index = 0, child = list->child; child; child = child->next)
	{
		struct p2p_lobby_entry const *entry;
		char text[48], game[64];
		boolean name_cut = FALSE, map_cut;

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
		/* (a group's header: its name and count alone, no badge, lock or
		columns; never chosen, browser_view_follow_groups) */
		if (entry->header)
		{
			if (named(child, "row_lock"))
				widget_instance_set_visibility_recursive(named(child, "row_lock"), FALSE);
			if (named(child, "row_badge"))
				widget_instance_set_visibility_recursive(named(child, "row_badge"), FALSE);
			row_column(child, "row_name", entry->name);
			if (named(child, "row_map"))
				widget_instance_set_visibility_recursive(named(child, "row_map"), FALSE);
			if (named(child, "row_game"))
				widget_instance_set_visibility_recursive(named(child, "row_game"), FALSE);
			if (named(child, "row_players"))
				widget_instance_set_visibility_recursive(named(child, "row_players"), FALSE);
#ifndef HALO_RELEASE
			if (getenv("HALO_TEST_BROWSER_ODD") || getenv("HALO_TEST_BROWSER_NAMES"))
			{
				static char said[BROWSER_ROWS][160];
				long row = index < BROWSER_ROWS ? index : BROWSER_ROWS - 1;

				if (strcmp(entry->name, said[row]))
				{
					snprintf(said[row], sizeof(said[row]), "%s", entry->name);
					platform_log("menus: browser row %ld: [%s]", row + 1, entry->name);
				}
			}
#endif
			index++;
			continue;
		}
		/* (the lock: a game with a password, 2a6dc2b7; the Halo 3 rows' is
		their badge's) */
		if (named(child, "row_lock"))
			widget_instance_set_visibility_recursive(named(child, "row_lock"), entry->locked != 0);
		/* (the Halo 3 browser's rows: the platform's badge, VITA or PC, with
		the lock beside it for a password - row_badge's frames: VITA, PC, then
		the same locked - and the rest of the row's badge before its name: a
		PC build's own (OPENCE, CHUPA: the art says PC), SERVER, EXPERIMENTAL,
		or "+PC" for a Vita's game PC players can join too) */
		if (named(child, "row_badge"))
		{
			char const *rest = entry->badge;
			size_t tag = strlen(entry->tag);

			named(child, "row_badge")->animation.current_frame_index =
				(short)((entry->platform != CROSSPLAY_PLATFORM_VITA ? 1 : 0) + (entry->locked ? 2 : 0));
			/* (the tag the art says itself: VITA, PC; the PC build's VITA
			(PC): VITA, and "(PC)" before the name) */
			if (entry->platform == CROSSPLAY_PLATFORM_VITA && (entry->marks & CROSSPLAY_HOST_PC_BUILD))
				tag = strlen(crossplay_guard_tag(CROSSPLAY_PLATFORM_VITA));
			if (tag && !strncmp(rest, entry->tag, tag) && (entry->platform == CROSSPLAY_PLATFORM_VITA ||
				entry->platform == CROSSPLAY_PLATFORM_PC))
			{
				rest += tag;
			}
			while (*rest == ' ')
				rest++;
			if (*rest)
			{
				char badged[P2P_LOBBY_NAME_SIZE + sizeof(entry->badge) + 4];

				snprintf(badged, sizeof(badged), *rest == '(' ? "%s %s" : "[%s] %s", rest, entry->name);
				name_cut = row_column(child, "row_name", badged);
			}
			/* (the System Link list's among the online ones: "[LAN]") */
			else if (lan_with_online() && lan_entry(entry))
			{
				char badged[P2P_LOBBY_NAME_SIZE + 8];

				snprintf(badged, sizeof(badged), "[%s] %s", T("LAN"), entry->name);
				name_cut = row_column(child, "row_name", badged);
			}
			else
				name_cut = row_column(child, "row_name", entry->name);
		}
		/* (a PC game's badges first: crossplay_guard.h) */
		else if (entry->badge[0])
		{
			char badged[P2P_LOBBY_NAME_SIZE + sizeof(entry->badge) + 4];

			snprintf(badged, sizeof(badged), "[%s] %s", entry->badge, entry->name);
			name_cut = row_column(child, "row_name", badged);
		}
		else
			name_cut = row_column(child, "row_name", entry->name);
		map_cut = row_column(child, "row_map", entry->map);
		/* (co-op: its difficulty beside it - "Co-op · Legendary" in the Halo
		3 rows, whose font has the dot -, when the listing says it) */
		{
			static char const *const difficulties[] = { "Easy", "Normal", "Heroic", "Legendary" };

			if (entry->coop && entry->difficulty >= 0 && entry->difficulty < 4)
			{
				snprintf(game, sizeof(game), named(child, "row_badge") ? "%s \xc2\xb7 %s" : "%s (%s)",
					entry->gametype[0] ? entry->gametype : T("Co-op"), T(difficulties[entry->difficulty]));
			}
			else
				snprintf(game, sizeof(game), "%s", entry->coop && !entry->gametype[0] ? T("Co-op") : entry->gametype);
			row_column(child, "row_game", game);
		}
		/* (the players: "-" for a count not said) */
		if (entry->maximum > 0)
			snprintf(text, sizeof(text), "%d/%d", entry->players, entry->maximum);
		else if (entry->players > 0)
			snprintf(text, sizeof(text), "%d", entry->players);
		else
			snprintf(text, sizeof(text), "-");
		row_column(child, "row_players", text);
#ifndef HALO_RELEASE
		/* (the tests': each row as shown, said as it changes) */
		if (getenv("HALO_TEST_BROWSER_ODD") || getenv("HALO_TEST_BROWSER_NAMES"))
		{
			static char said[BROWSER_ROWS][160];
			char line[160];
			long row = index < BROWSER_ROWS ? index : BROWSER_ROWS - 1;

			snprintf(line, sizeof(line), "%s%s | %s | %s%s | %s", entry->name, name_cut ? " (cut)" : "", game[0] ? game : "-",
				entry->map[0] ? entry->map : "-", map_cut ? " (cut)" : "", text);
			if (strcmp(line, said[row]))
			{
				snprintf(said[row], sizeof(said[row]), "%s", line);
				platform_log("menus: browser row %ld: %s", row + 1, line);
			}
		}
#endif
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
			platform_log("menus: the server browser's game %d of %ld chosen: %s (rows from %d)",
				browser_list_game_number(menu_functions.games, (int)menu_functions.game_count, (int)target),
				browser_games(), menu_functions.games[target].name, menu_functions.view.offset + 1);
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
	if (menu_functions.lan)
		lan_status(screen, line, (int)sizeof(line), message, (int)sizeof(message));
	else if (menu_functions.status[0])
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
			if (browser_games() == 1)
				snprintf(line, sizeof(line), "%s", T("1 public game"));
			else
				snprintf(line, sizeof(line), T("%ld public games"), browser_games());
			/* (the rows shown, when they scroll, and the sort: Y) */
			if (menu_functions.game_count > BROWSER_ROWS)
			{
				size_t length = strlen(line);

				snprintf(line + length, sizeof(line) - length, ", %ld-%ld", browser_shown_first(), browser_shown_last());
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
	/* (the filter: the Halo 3 browsers' hint says it, the D-pad's; OpenCE's
	browser's status, unless all games) */
	if (named(screen, "filter"))
		text_show(named(screen, "filter"), browser_filter_name());
	else if (network_wire_browser_filter() != NETWORK_WIRE_FILTER_ALL)
	{
		size_t length = strlen(line);

		snprintf(line + length, sizeof(line) - length, ", %s", browser_filter_name());
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
			return menu_functions.entries[row].header ? NULL : &menu_functions.entries[row];
	}
	return menu_functions.chosen_valid && !menu_functions.chosen.header ? &menu_functions.chosen : NULL;
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

static boolean browser_join(struct widget_instance *widget, boolean *widget_deleted)
{
	struct p2p_lobby_entry const *entry = browser_entry(widget);

	/* (the games browser: its System Link rows joined as the System Link
	list joins them; its online rows as the server browser's, below) */
	if (menu_functions.lan && (!entry || lan_entry(entry)))
		return lan_join(widget, widget_deleted);
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
	/* (the party's leader: told first of a game with fewer open slots than
	the party; A again joins) */
	{
		char warning[192];

		if (p2p_party_join_warning(entry->players, entry->maximum, warning, sizeof(warning)) &&
			!party_warning_confirmed(entry->id))
		{
			snprintf(menu_functions.status, sizeof(menu_functions.status), "%s %s", warning, h3_buttons(T("Cross again: join anyway.")));
			return TRUE;
		}
	}
	/* (crossplay_guard.h: a large or co-op PC game, Experimental, asked
	about once; a Vita's game never) */
	if (entry->platform != CROSSPLAY_PLATFORM_VITA && (entry->experimental || entry->coop) &&
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

static boolean lan_join(struct widget_instance *widget, boolean *widget_deleted)
{
	struct p2p_lobby_entry const *entry = browser_entry(widget);
	/* (the System Link list's widget, for the game: the row's list) */
	struct widget_instance *list = widget && widget->parent && !strncmp(widget->name, "row_", 4) ? widget->parent :
		widget;
	void const *server;
	char name[P2P_LOBBY_NAME_SIZE];

	if (!entry)
		return TRUE;
	menu_functions.chosen = *entry;
	menu_functions.chosen_valid = TRUE;
	menu_functions.status[0] = 0;
	snprintf(name, sizeof(name), "%s", entry->name);
	server = lan_entry(entry) ? network_game_client_listed_game_by_id(entry->id + sizeof(LAN_ID_PREFIX) - 1) : NULL;
	if (!server)
	{
		snprintf(menu_functions.status, sizeof(menu_functions.status), "%s", T("That game is gone"));
		platform_log("menus: the LAN browser's game %s is no longer listed", name);
		return TRUE;
	}
	platform_log("menus: the LAN browser joins %s", name);
	if (!ui_widget_port_join_advertised_game(list, server, widget_deleted))
	{
		/* (the player told why by the game: a closed game, another network
		version) */
		snprintf(menu_functions.status, sizeof(menu_functions.status), "%s", T("Not joined"));
		platform_log("menus: the LAN browser's game %s was not joined", name);
	}
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
	/* (who can join: network.who_can_join, network_wire.h) */
	int audience = network_wire_host_audience();

	menu_functions.create_pending = FALSE;
	/* (back from the Halo 3 Create Game's GAME TYPE or MAP picker: what it
	showed, the name and password typed kept; else the settings') */
	if (menu_functions.setup_held)
	{
		maximum = menu_functions.held_maximum;
		private_game = menu_functions.held_private;
		audience = menu_functions.held_audience;
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
	/* (VITA, VITA PC, EVERYONE: NETWORK_AUDIENCE_*'s order; the PC build's
	spinner, its strings_pc, from VITA PC: it never hosts for Vitas only) */
	spinner = spinner_of(screen, "op_who");
	if (spinner)
		spinner->parameters.list.selected_index = (short)(PIN(audience, network_wire_audience_first(),
			NUMBER_OF_NETWORK_AUDIENCES - 1) - network_wire_audience_first());
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
	spinner = spinner_of(screen, "op_who");
	menu_functions.held_audience = (short)(spinner ? spinner->parameters.list.selected_index +
		network_wire_audience_first() : network_wire_audience_default());
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

	char password_before[P2P_LOBBY_PASSWORD_SIZE];

	snprintf(password_before, sizeof(password_before), "%s", setting_get("HALO_NET_LOBBY_PASSWORD", ""));
	setting_set("HALO_NET_LOBBY_NAME", menu_functions.lobby_name);
	spinner = spinner_of(screen, "op_max_players");
	snprintf(value, sizeof(value), "%d", spinner ? PIN(spinner->parameters.list.selected_index + 2, 2, 16) : 16);
	setting_set("HALO_NET_MAX_PLAYERS", value);
	spinner = spinner_of(screen, "op_visibility");
	setting_set("HALO_NET_HOST_PUBLIC", spinner && spinner->parameters.list.selected_index == 1 ? "false" : "true");
	setting_set("HALO_NET_LOBBY_PASSWORD", menu_functions.lobby_password);
	{
		struct widget_instance *who = spinner_of(screen, "op_who");

		if (who)
			setting_set("HALO_NET_WHO_CAN_JOIN", network_wire_audience_value(PIN(who->parameters.list.selected_index +
				network_wire_audience_first(), network_wire_audience_first(), NUMBER_OF_NETWORK_AUDIENCES - 1)));
	}
	/* (a live lobby's game hosted for others than WHO CAN JOIN says now: a
	game's wire is its own for its life (network_wire.h), so it closes, its
	joiners told, and START GAME hosts on the one chosen as before - the
	game's lobby, then back on the lobby it is the live game again) */
	h3_live_follow_wire(FALSE);
	/* (GAME OPTIONS' START GAME on ONLINE, Menu style Halo 3: public or
	private chosen for the lobby - its live game's now, or the one hosted
	next and kept by the lobby - the panel's Visibility its default) */
	if (halo_menu_style_halo3() && h3_mp.network == _h3_network_online)
		h3_live_public_chosen();
	/* (a live lobby, Menu style Halo 3: its game takes them now - its name
	and most players from the settings each frame, public or private and a
	password changed its listing - and START GAME goes on to its lobby) */
	if (h3_live_start())
	{
		p2p_lobby_set_public(spinner && spinner->parameters.list.selected_index == 1 ? 0 : 1);
		if (strcmp(password_before, menu_functions.lobby_password))
			p2p_lobby_set_password(menu_functions.lobby_password);
		platform_log("menus: Server Setup: %s, %s players, %s%s, %s; the live lobby's game takes them, its lobby next",
			menu_functions.lobby_name, value, spinner && spinner->parameters.list.selected_index == 1 ? "private" : "public",
			menu_functions.lobby_password[0] ? ", a password" : "", network_wire_crossplay() ? "Vitas and PC" :
			network_wire_host_audience() == NETWORK_AUDIENCE_VITA_PC ? "Vitas and Vita PC" : "Vitas");
		return TRUE;
	}
	menu_functions.create_pending = TRUE;
	menu_functions.create_system_link = FALSE;
	platform_log("menus: Server Setup: %s, %s players, %s%s, %s; the profile, then %s", menu_functions.lobby_name,
		value, spinner && spinner->parameters.list.selected_index == 1 ? "private" : "public",
		menu_functions.lobby_password[0] ? ", a password" : "", network_wire_host_crossplay() ? "Vitas and PC" :
		network_wire_host_audience() == NETWORK_AUDIENCE_VITA_PC ? "Vitas and Vita PC" : "Vitas",
		halo_menu_style_halo3() ? "the game's lobby" : "the map list");
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

/* ---- map variants (port/linux/src/forge_store.h): the Forge lobby's MAP
VARIANT and its picker (halo3/forge_lobby.xml), the Forge menu's SAVE MAP
VARIANT (halo3/forge.xml), and File Share (halo3/fileshare.xml). The files
are read (each through the validator) when a screen that lists them opens,
or after a change, never each frame */

#define VARIANT_ROWS 12

/* whose the list of map variants is */
enum
{
	_variants_nobody,
	_variants_lobby,
	_variants_fileshare,
};

/* File Share's kinds */
enum
{
	_fileshare_map_variants,
	_fileshare_game_variants,
	NUMBER_OF_FILESHARE_KINDS
};

static struct
{
	/* the lobby picker's (one map's: map_name) or File Share's (every
	map's) */
	short owner;
	char map_name[FORGE_FILE_MAP_NAME_SIZE];
	struct forge_store_entry entries[FORGE_STORE_MAXIMUM_LISTED];
	short count;
} h3_variants;

static struct
{
	short kind;
	struct browser_view view;
	int chosen;
	int page;
	/* what the last action came to */
	char status[FORGE_STORE_WHY_SIZE];
	/* the map variant asked about (deleted, renamed): its map and file */
	char asked_map[FORGE_FILE_MAP_NAME_SIZE];
	char asked_file[FORGE_FILE_NAME_SIZE];
	char asked_title[FORGE_FILE_TITLE_SIZE];
	char said[192];
} h3_fileshare;

/* whether text can be typed: the Vita's keyboard, the PC build's text box
(pc_text_entry.c), the tests' answers (HALO_TEST_TEXT_INPUT): yes, but for
a test build's scripted run with no one to type */
static boolean typing_available(void)
{
#if !defined(HALO_VITA) && !defined(HALO_RELEASE)
	return !typing_scripted_without_answers();
#else
	return TRUE;
#endif
}

/* the map variants of a map (map_name) or of every map (NULL) read for a
screen, unless they are its already (again, with again) */
static void h3_variants_list(short owner, char const *map_name, boolean again)
{
	if (!again && h3_variants.owner == owner &&
		!strcmp(h3_variants.map_name, map_name ? map_name : ""))
	{
		return;
	}
	h3_variants.count = (short)forge_store_list(map_name, h3_variants.entries, FORGE_STORE_MAXIMUM_LISTED);
	h3_variants.owner = owner;
	snprintf(h3_variants.map_name, sizeof(h3_variants.map_name), "%s", map_name ? map_name : "");
	platform_log("menus: %d map variants%s%s", h3_variants.count, map_name ? " of " : "", map_name ? map_name : "");
}

/* a map's name (forge.c's, translated) by its short name, the short name
itself for one not in the lobby's list */
static char const *h3_variant_map_name(char const *map_name)
{
	short map_index = forge_map_of_short(map_name);

	return map_index != NONE ? T(forge_map_name(map_index)) : map_name;
}

/* a map variant's details for a pane (File Share's, with its map) */
static void h3_variant_details(struct forge_store_entry const *entry, boolean in_fileshare, char *text, int size)
{
	int length = 0;

	text[0] = 0;
	if (in_fileshare)
		length += snprintf(text + length, (size_t)(size - length), T("Map: %s\n"), h3_variant_map_name(entry->map_name));
	if (entry->damaged)
	{
		snprintf(text + length, (size_t)(size - length), "%s", in_fileshare ?
			h3_buttons(T("This file is damaged and can't be used.\nTriangle deletes it.")) :
			T("This file is damaged and can't be used.\nDelete it in File Share."));
		return;
	}
	length += snprintf(text + length, (size_t)(size - length), T("Objects: %d\n"), entry->objects);
	if (entry->author[0] && length < size)
		length += snprintf(text + length, (size_t)(size - length), T("Created by: %s\n"), entry->author);
	if (entry->date[0] && length < size)
		length += snprintf(text + length, (size_t)(size - length), T("Saved: %s\n"), entry->date);
	if (entry->description[0] && length < size)
		snprintf(text + length, (size_t)(size - length), "%s", entry->description);
}

/* the lobby's map variant still a file of its map (deleted, or another map
chosen: none) */
static void h3_forge_variant_check(void)
{
	if (h3_forge.variant_file[0] && !forge_store_exists(forge_map_short(h3_forge.map), h3_forge.variant_file))
	{
		platform_log("menus: forge lobby: the map variant %s is gone", h3_forge.variant_file);
		h3_forge.variant_file[0] = 0;
		h3_forge.variant_title[0] = 0;
	}
}

/* the map picker's variants of pick_map: how many rows (the map itself,
then each of its map variants that is not damaged), and the entry of row
`row` (NULL for the map itself, or past them) */
static int h3_forge_pick_rows(int row, struct forge_store_entry const **entry)
{
	int index, rows = 1;

	*entry = NULL;
	for (index = 0; index < h3_variants.count; index++)
	{
		if (h3_variants.entries[index].damaged)
			continue;
		if (rows == row)
			*entry = &h3_variants.entries[index];
		rows++;
	}
	return rows;
}

/* (the map picker, a map chosen) its rows (item_0 to item_12): the map
itself, then its map variants, scrolling; the picture, the variant's
details in the description's place, the map's name as the title */
static void h3_forge_variants_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focus = NULL, *found;
	struct forge_store_entry const *entry;
	long index, focused_row = NONE, rows = 0;
	int target;
	int count;
	char text[256];

	h3_variants_list(_variants_lobby, forge_map_short(h3_forge.pick_map), FALSE);
	count = h3_forge_pick_rows(-1, &entry);
	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		if (child == list->focused_child)
			focused_row = index;
		rows++;
	}
	/* (just opened: the focus is the map's row, not a variant's) */
	if (h3_forge.pick_refocus)
	{
		focused_row = NONE;
		h3_forge.pick_refocus = FALSE;
	}
	target = browser_view_follow(&h3_forge.variant_view, (int)rows, count, (int)focused_row, h3_forge.variant_chosen,
		h3_forge.variant_page);
	h3_forge.variant_page = 0;
	h3_forge.variant_chosen = target;
	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		boolean shown = index < h3_forge.variant_view.visible;
		long shown_index = h3_forge.variant_view.offset + index;

		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		if (!shown)
			continue;
		h3_forge_pick_rows((int)shown_index, &entry);
		snprintf(text, sizeof(text), "%s", entry ? entry->title : T(forge_map_name(h3_forge.pick_map)));
		h3_capitals(text);
		text_show(child, text);
		if (index == h3_forge.variant_view.row)
			focus = child;
	}
	if (focus && focus != list->focused_child)
		ui_widget_port_give_focus(list, focus);
	snprintf(text, sizeof(text), "%s", T(forge_map_name(h3_forge.pick_map)));
	h3_capitals(text);
	text_show(named(screen, "title"), text);
	if ((found = named(screen, "maps_picture")) != NULL)
		found->animation.current_frame_index = h3_forge.pick_map;
	if ((found = named(screen, "maps_text")) != NULL)
		widget_instance_set_visibility_recursive(found, FALSE);
	if ((found = named(screen, "variant_name")) != NULL)
		widget_instance_set_visibility_recursive(found, TRUE);
	if ((found = named(screen, "variant_text")) != NULL)
		widget_instance_set_visibility_recursive(found, TRUE);
	h3_forge_pick_rows(target, &entry);
	if (!entry)
	{
		text_show(named(screen, "variant_name"), T(forge_map_name(h3_forge.pick_map)));
		text_show(named(screen, "variant_text"), T("The original map: nothing placed yet."));
	}
	else
	{
		text_show(named(screen, "variant_name"), entry->title);
		h3_variant_details(entry, FALSE, text, sizeof(text));
		text_show(named(screen, "variant_text"), text);
	}
	{
		char said[192];

		snprintf(said, sizeof(said), "%s: %s (%d of %d)", forge_map_name(h3_forge.pick_map), entry ? entry->title :
			"the original", target + 1, count);
		if (strcmp(said, h3_forge.said))
		{
			snprintf(h3_forge.said, sizeof(h3_forge.said), "%s", said);
			platform_log("menus: forge variants: %s", said);
		}
	}
}

/* (port forge map pick, A in the map picker) a map: its variants shown in
the list (FALSE: the picker stays); a variant of it, or the map itself:
the lobby's, and back to the lobby (TRUE) */
static boolean h3_forge_map_pick(struct widget_instance *widget)
{
	struct forge_store_entry const *entry;
	long index;

	if (!h3_forge.picking_variant)
	{
		short map = (short)h3_child_index(widget);
		char why[160];

		if (forge_map_state(map, why, sizeof(why)) != _map_check_playable)
		{
			platform_log("menus: forge maps: %s can't be chosen: %s", forge_map_name(map), why);
			return FALSE;
		}
		h3_forge.pick_map = map;
		h3_forge.picking_variant = TRUE;
		h3_forge.pick_refocus = TRUE;
		h3_forge.said[0] = 0;
		h3_variants_list(_variants_lobby, forge_map_short(map), TRUE);
		browser_view_reset(&h3_forge.variant_view);
		h3_forge.variant_page = 0;
		h3_forge.variant_chosen = 0;
		/* (the lobby's variant of that map chosen again) */
		if (map == h3_forge.map && h3_forge.variant_file[0])
		{
			int row, count = h3_forge_pick_rows(-1, &entry);

			for (row = 1; row < count; row++)
			{
				h3_forge_pick_rows(row, &entry);
				if (entry && !strcmp(entry->file_name, h3_forge.variant_file))
					h3_forge.variant_chosen = row;
			}
		}
		platform_log("menus: forge lobby: %s's variants", forge_map_name(map));
		return FALSE;
	}
	index = h3_forge.variant_view.offset + h3_child_index(widget);
	h3_forge_pick_rows((int)index, &entry);
	h3_forge.map = h3_forge.pick_map;
	h3_forge.picking_variant = FALSE;
	h3_forge.note[0] = 0;
	if (entry)
	{
		snprintf(h3_forge.variant_file, sizeof(h3_forge.variant_file), "%s", entry->file_name);
		snprintf(h3_forge.variant_title, sizeof(h3_forge.variant_title), "%s", entry->title);
		platform_log("menus: forge lobby: %s, map variant %s (%s)", forge_map_name(h3_forge.map), entry->title,
			entry->file_name);
	}
	else
	{
		h3_forge.variant_file[0] = 0;
		h3_forge.variant_title[0] = 0;
		platform_log("menus: forge lobby: %s", forge_map_name(h3_forge.map));
	}
	/* (a live lobby's game on it: its joiners' lobbies follow) */
	h3_live_forge_follow();
	return TRUE;
}

/* (port forge maps back, B in the map picker) from a map's variants, back
to the maps on that map (FALSE: the picker stays); from the maps, back to
the lobby (TRUE) */
static boolean h3_forge_maps_back(struct widget_instance *widget)
{
	struct widget_instance *list;

	if (!h3_forge.picking_variant)
		return TRUE;
	h3_forge.picking_variant = FALSE;
	h3_forge.said[0] = 0;
	list = named(screen_of(widget), "list");
	if (list)
		h3_focus_child(list, h3_forge.pick_map);
	return FALSE;
}

/* (the Forge menu's SAVE MAP VARIANT) the name asked for on the system's
keyboard (the variant's own, else "<map> Variant"), then saved by forge.c
(h3_forge_title_update hands the name on); where no keyboard is, saved at
once under that name */
static void h3_forge_save(void)
{
	char name[FORGE_FILE_TITLE_SIZE];
	char author[FORGE_FILE_AUTHOR_SIZE];
	char const *scenario = main_get_map_name();
	char const *map = "";
	short index;

	for (index = 0; scenario && index < forge_map_count(); index++)
	{
		if (!_stricmp(scenario, forge_map_path(index)))
			map = T(forge_map_name(index));
	}
	if (forge_variant_title()[0])
		snprintf(name, sizeof(name), "%s", forge_variant_title());
	else
		snprintf(name, sizeof(name), T("%s Variant"), map[0] ? map : T("Forge"));
	if (typing_available())
	{
		typing_begin(_typing_forge_name, T("Map variant name"), name, FORGE_FILE_TITLE_SIZE - 1, FALSE);
		return;
	}
	h3_player_name(author, sizeof(author));
	sanitize(name, sizeof(name), name);
	forge_save_request(name, author);
}

/* (port forge properties) the properties window's rows (row_0 team, row_1
respawn, row_2 placed at the start, row_3 to row_5 yaw, pitch and roll,
row_6 their reset, row_7 and row_8 a character's AI team and AI) and the
object's name; closed when no object of the player's is held any more */
static void h3_forge_properties_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child;
	struct forge_properties properties;
	char text[224];
	short row;
	static char said[224];

	if (!forge_properties_get(&properties))
	{
		forge_menus_close_request();
		return;
	}
	text_show(named(screen, "name"), properties.name);
	for (row = 0, child = list->child; child; child = child->next, row++)
	{
		switch (row)
		{
		case _forge_property_team:
			snprintf(text, sizeof(text), "%s: %s", T("TEAM"), properties.team == 0 ? T("RED") : properties.team == 1 ?
				T("BLUE") : T("NEUTRAL"));
			break;
		case _forge_property_respawn:
			if (properties.respawn_seconds)
				snprintf(text, sizeof(text), T("RESPAWN: %d S"), properties.respawn_seconds);
			else
				snprintf(text, sizeof(text), "%s: %s", T("RESPAWN"), T("NEVER"));
			break;
		case _forge_property_place_at_start:
			snprintf(text, sizeof(text), "%s: %s", T("PLACE AT START"), properties.place_at_start ? T("YES") : T("NO"));
			break;
		case _forge_property_yaw:
			snprintf(text, sizeof(text), "%s: %d\xc2\xb0", T("YAW"), properties.yaw);
			break;
		case _forge_property_pitch:
			snprintf(text, sizeof(text), "%s: %d\xc2\xb0", T("PITCH"), properties.pitch);
			break;
		case _forge_property_roll:
			snprintf(text, sizeof(text), "%s: %d\xc2\xb0", T("ROLL"), properties.roll);
			break;
		case _forge_property_reset_rotation:
			snprintf(text, sizeof(text), "%s", T("RESET ROTATION"));
			break;
		case _forge_property_ai_team:
			snprintf(text, sizeof(text), "%s: %s", T("AI TEAM"), properties.ai_team_name);
			break;
		default:
			snprintf(text, sizeof(text), "%s: %s", T("AI"), properties.ai_on ? T("ON") : T("OFF"));
			break;
		}
		/* (a character's own rows: hidden for any other object) */
		if (row >= _forge_property_ai_team)
		{
			widget_instance_set_visibility_recursive(child, properties.ai);
			child->disabled = !properties.ai;
		}
		text_show(child, text);
		child->animation.current_frame_index = child == list->focused_child ? 1 : 0;
	}
	if (properties.ai)
		snprintf(text, sizeof(text), "%s: team %d, respawn %d, %s, yaw %d pitch %d roll %d, AI team %d, AI %s", properties.name,
			properties.team, properties.respawn_seconds, properties.place_at_start ? "at start" : "not at start", properties.yaw,
			properties.pitch, properties.roll, properties.ai_team, properties.ai_on ? "on" : "off");
	else
		snprintf(text, sizeof(text), "%s: team %d, respawn %d, %s, yaw %d pitch %d roll %d", properties.name, properties.team,
			properties.respawn_seconds, properties.place_at_start ? "at start" : "not at start", properties.yaw,
			properties.pitch, properties.roll);
	if (strcmp(text, said))
	{
		snprintf(said, sizeof(said), "%s", text);
		platform_log("menus: forge properties: %s", said);
	}
}

/* (port forge packs) EXTRA OBJECTS' rows (item_0 to item_2: each pack, on
or off), the focused one's description and state: ready, being built and
how far, or why it cannot be; the packs on built in the background */
static void h3_forge_packs_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child;
	short pack, focused = 0;
	char text[256], why[160];
	long progress = 0;
	short state;
	static char said[192];

	forge_packs_prebuild();
	for (pack = 0, child = list->child; child; child = child->next, pack++)
	{
		boolean shown = pack < forge_pack_count();

		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		if (!shown)
			continue;
		snprintf(text, sizeof(text), "%s: %s", forge_pack_label(pack), forge_pack_enabled(pack) ? T("ON") : T("OFF"));
		h3_capitals(text);
		text_show(child, text);
		if (child == list->focused_child)
			focused = pack;
	}
	text_show(named(screen, "info_name"), forge_pack_label(focused));
	text_show(named(screen, "info_text"), forge_pack_description(focused));
	state = forge_pack_status(focused, &progress, why, sizeof(why));
	if (!forge_pack_enabled(focused))
		snprintf(text, sizeof(text), "%s", T("Off: Forge leaves its objects out."));
	else if (state == _scene_pack_state_building)
		snprintf(text, sizeof(text), T("Building it... %ld%%"), progress);
	else if (state == _scene_pack_state_failed)
		snprintf(text, sizeof(text), T("Unavailable: %s"), why);
	else if (state == _scene_pack_state_ready || state == _scene_pack_state_loaded)
		snprintf(text, sizeof(text), "%s", T("Ready: its objects are in the object browser."));
	else
		text[0] = 0;
	text_show(named(screen, "info_state"), text);
	snprintf(why, sizeof(why), "%s: %s", forge_pack_label(focused), text);
	if (strcmp(why, said) && state != _scene_pack_state_building)
	{
		snprintf(said, sizeof(said), "%s", why);
		platform_log("menus: forge packs: %s", said);
	}
}

/* ---- File Share */

static short h3_fileshare_count(void)
{
	return h3_fileshare.kind == _fileshare_map_variants ? h3_variants.count : h3_mp.gametype_count;
}

/* the map variant chosen, NULL for none (or game variants shown) */
static struct forge_store_entry const *h3_fileshare_entry(void)
{
	if (h3_fileshare.kind != _fileshare_map_variants || h3_variants.owner != _variants_fileshare ||
		h3_fileshare.chosen < 0 || h3_fileshare.chosen >= h3_variants.count)
	{
		return NULL;
	}
	return &h3_variants.entries[h3_fileshare.chosen];
}

static void h3_fileshare_status(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(h3_fileshare.status, sizeof(h3_fileshare.status), format, arguments);
	va_end(arguments);
	platform_log("menus: fileshare: %s", h3_fileshare.status);
}

/* (port fileshare) the rows (row_0 to row_11: row_name, row_map), the
kind, the chosen one's picture and details, what the last action came to;
a name typed for Rename taken */
static void h3_fileshare_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focus = NULL, *found;
	long index, focused_row = NONE;
	int target;
	int count;
	char text[320], line[160];

	if (typing_done(_typing_fileshare_rename, text, FORGE_FILE_TITLE_SIZE))
	{
		char why[FORGE_STORE_WHY_SIZE];

		if (!text[0])
			h3_fileshare_status("%s", T("That name can't be used."));
		else if (forge_store_rename(h3_fileshare.asked_map, h3_fileshare.asked_file, text, why, sizeof(why)))
		{
			h3_fileshare_status(T("Renamed \"%s\"."), text);
			if (!strcmp(h3_fileshare.asked_file, h3_forge.variant_file) &&
				!strcmp(h3_fileshare.asked_map, forge_map_short(h3_forge.map)))
			{
				snprintf(h3_forge.variant_title, sizeof(h3_forge.variant_title), "%s", text);
			}
		}
		else
			h3_fileshare_status("%s", why);
		h3_variants.owner = _variants_nobody;
	}
	h3_variants_list(_variants_fileshare, NULL, FALSE);
	count = h3_fileshare_count();
	for (index = 0, child = list->child; child; child = child->next)
	{
		if (strncmp(child->name, "row_", 4))
			continue;
		if (child == list->focused_child)
			focused_row = index;
		index++;
	}
	target = browser_view_follow(&h3_fileshare.view, VARIANT_ROWS, count, (int)focused_row, h3_fileshare.chosen,
		h3_fileshare.page);
	h3_fileshare.page = 0;
	h3_fileshare.chosen = target;
	for (index = 0, child = list->child; child; child = child->next)
	{
		long shown = h3_fileshare.view.offset + index;

		if (strncmp(child->name, "row_", 4))
			continue;
		if (index >= h3_fileshare.view.visible)
		{
			widget_instance_set_visibility_recursive(child, FALSE);
			child->disabled = TRUE;
			index++;
			continue;
		}
		widget_instance_set_visibility_recursive(child, TRUE);
		child->disabled = FALSE;
		if (h3_fileshare.kind == _fileshare_map_variants)
		{
			struct forge_store_entry const *entry = &h3_variants.entries[shown];

			if (entry->damaged)
				snprintf(text, sizeof(text), "%s", T("DAMAGED FILE"));
			else
				snprintf(text, sizeof(text), "%s", entry->title);
			snprintf(line, sizeof(line), "%s", h3_variant_map_name(entry->map_name));
		}
		else
		{
			snprintf(text, sizeof(text), "%s", h3_mp.gametype_names[shown]);
			line[0] = 0;
		}
		h3_capitals(text);
		text_show(named(child, "row_name"), text);
		text_show(named(child, "row_map"), line);
		if (index == h3_fileshare.view.row)
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
	/* (the kind, the count, the pane, the status) */
	snprintf(text, sizeof(text), "%s  \xc2\xb7  %s", h3_fileshare.kind == _fileshare_map_variants ? T("MAP VARIANTS") :
		T("map variants"), h3_fileshare.kind == _fileshare_game_variants ? T("GAME VARIANTS") : T("game variants"));
	text_show(named(screen, "kind"), text);
	snprintf(text, sizeof(text), h3_fileshare.kind == _fileshare_map_variants ? T("%d map variants") :
		T("%d game variants"), count);
	text_show(named(screen, "count"), text);
	found = named(screen, "picture");
	if (h3_fileshare.kind == _fileshare_map_variants && target >= 0 && target < h3_variants.count)
	{
		struct forge_store_entry const *entry = &h3_variants.entries[target];
		short map_index = forge_map_of_short(entry->map_name);

		if (found)
		{
			widget_instance_set_visibility_recursive(found, map_index != NONE);
			found->animation.current_frame_index = map_index != NONE ? map_index : 0;
		}
		text_show(named(screen, "info_name"), entry->title);
		h3_variant_details(entry, TRUE, text, sizeof(text));
		text_show(named(screen, "info_text"), text);
		snprintf(line, sizeof(line), "%s (%s)", entry->title, entry->map_name);
	}
	else if (h3_fileshare.kind == _fileshare_game_variants && target >= 0 && target < h3_mp.gametype_count)
	{
		if (found)
			widget_instance_set_visibility_recursive(found, FALSE);
		text_show(named(screen, "info_name"), h3_mp.gametype_names[target]);
		text_show(named(screen, "info_text"), T("A game variant.\nGame variants are made and changed in\nMULTIPLAYER, GAME."));
		snprintf(line, sizeof(line), "%s", h3_mp.gametype_names[target]);
	}
	else
	{
		if (found)
			widget_instance_set_visibility_recursive(found, FALSE);
		if (h3_fileshare.kind == _fileshare_map_variants)
		{
			text_show(named(screen, "info_name"), T("NO MAP VARIANTS"));
			text_show(named(screen, "info_text"), T("Make one in Forge: place objects, then\nStart, SAVE MAP VARIANT."));
		}
		else
		{
			text_show(named(screen, "info_name"), T("NO GAME VARIANTS"));
			text_show(named(screen, "info_text"), "");
		}
		snprintf(line, sizeof(line), "none");
	}
	text_show(named(screen, "status"), h3_fileshare.status);
	/* (game variants are only listed: their actions' keys hidden) */
	{
		static char const *const actions[] = { "a_butn", "open", "x_butn", "rename", "y_butn", "delete" };

		for (index = 0; index < (long)NUMBEROF(actions); index++)
		{
			if ((found = named(screen, actions[index])) != NULL)
				widget_instance_set_visibility_recursive(found, h3_fileshare.kind == _fileshare_map_variants);
		}
	}
	{
		char said[192];

		snprintf(said, sizeof(said), "%s: %s (%d of %d)", h3_fileshare.kind == _fileshare_map_variants ? "map variants" :
			"game variants", line, target + 1, count);
		if (strcmp(said, h3_fileshare.said))
		{
			snprintf(h3_fileshare.said, sizeof(h3_fileshare.said), "%s", said);
			platform_log("menus: fileshare: %s", said);
		}
	}
}

/* (port fileshare rename) Square: the chosen map variant's new name typed */
static void h3_fileshare_rename(void)
{
	struct forge_store_entry const *entry = h3_fileshare_entry();

	if (!entry || entry->damaged)
	{
		h3_fileshare_status("%s", h3_fileshare.kind == _fileshare_map_variants ? T("There is no map variant to rename.") :
			T("Game variants are changed in MULTIPLAYER, GAME."));
		return;
	}
	if (!typing_available())
	{
		h3_fileshare_status("%s", T("Renaming needs the PS Vita's keyboard."));
		return;
	}
	snprintf(h3_fileshare.asked_map, sizeof(h3_fileshare.asked_map), "%s", entry->map_name);
	snprintf(h3_fileshare.asked_file, sizeof(h3_fileshare.asked_file), "%s", entry->file_name);
	snprintf(h3_fileshare.asked_title, sizeof(h3_fileshare.asked_title), "%s", entry->title);
	typing_begin(_typing_fileshare_rename, T("Map variant name"), entry->title, FORGE_FILE_TITLE_SIZE - 1, FALSE);
}

/* (port fileshare ask delete) Triangle: the chosen map variant asked about
(the question screen opens), FALSE with none */
static boolean h3_fileshare_ask_delete(void)
{
	struct forge_store_entry const *entry = h3_fileshare_entry();

	if (!entry)
	{
		h3_fileshare_status("%s", h3_fileshare.kind == _fileshare_map_variants ? T("There is no map variant to delete.") :
			T("Game variants are changed in MULTIPLAYER, GAME."));
		return FALSE;
	}
	snprintf(h3_fileshare.asked_map, sizeof(h3_fileshare.asked_map), "%s", entry->map_name);
	snprintf(h3_fileshare.asked_file, sizeof(h3_fileshare.asked_file), "%s", entry->file_name);
	snprintf(h3_fileshare.asked_title, sizeof(h3_fileshare.asked_title), "%s", entry->title);
	platform_log("menus: fileshare: delete %s/%s? asked", entry->map_name, entry->file_name);
	return TRUE;
}

/* (port fileshare delete) the question's A: the map variant asked about
deleted */
static void h3_fileshare_delete(void)
{
	if (!h3_fileshare.asked_file[0])
		return;
	if (forge_store_delete(h3_fileshare.asked_map, h3_fileshare.asked_file))
	{
		h3_fileshare_status(T("Deleted \"%s\"."), h3_fileshare.asked_title);
		if (!strcmp(h3_fileshare.asked_file, h3_forge.variant_file) &&
			!strcmp(h3_fileshare.asked_map, forge_map_short(h3_forge.map)))
		{
			h3_forge.variant_file[0] = 0;
			h3_forge.variant_title[0] = 0;
		}
	}
	else
	{
		h3_fileshare_status(T("\"%s\" couldn't be deleted."), h3_fileshare.asked_title);
	}
	h3_fileshare.asked_file[0] = 0;
	h3_variants.owner = _variants_nobody;
}

/* (port fileshare open) A: the Forge lobby, on the chosen map variant's
map with it chosen; FALSE (the lobby does not open) when it cannot be */
static boolean h3_fileshare_open(void)
{
	struct forge_store_entry const *entry = h3_fileshare_entry();
	short map_index;

	if (!entry)
	{
		h3_fileshare_status("%s", h3_fileshare.kind == _fileshare_map_variants ? T("There is no map variant to open.") :
			T("Game variants are played from MULTIPLAYER."));
		return FALSE;
	}
	if (entry->damaged)
	{
		h3_fileshare_status("%s", T("This file is damaged and can't be used."));
		return FALSE;
	}
	map_index = forge_map_of_short(entry->map_name);
	if (map_index == NONE || !forge_map_available(map_index))
	{
		h3_fileshare_status(T("%s isn't in the maps folder."), h3_variant_map_name(entry->map_name));
		return FALSE;
	}
	h3_forge.initialized = TRUE;
	h3_forge.map = map_index;
	snprintf(h3_forge.variant_file, sizeof(h3_forge.variant_file), "%s", entry->file_name);
	snprintf(h3_forge.variant_title, sizeof(h3_forge.variant_title), "%s", entry->title);
	h3_forge.note[0] = 0;
	h3_fileshare.status[0] = 0;
	platform_log("menus: fileshare: open %s/%s in Forge", entry->map_name, entry->file_name);
	return TRUE;
}

/* ---- the Halo 3 MOD MANAGER (halo3/mods.xml, the main menu's): the
player's custom maps and mods (mod_manager.c), Halo 3's file list - a row
each (name, type, size, ON / OFF / FAILED), scrolling past the screen's
ten as the server browser's do (browser_list.h), the chosen one's facts
under the list (the modes it plays in, the game's check, what it needs,
where it came from), the space used and free at the top right. Cross turns
the row's off or on, Triangle opens its details, Square asks before
deleting it (the answers' rows, NO first). The settings panel's Mod manager
page does the same (vita_settings.c). */

#define H3_MODS_ROWS 10
/* how long a delete's outcome stays at the top right */
#define H3_MODS_MESSAGE_MILLISECONDS 5000

static struct
{
	struct browser_view view;
	short page;
	/* the entry chosen, by its file and kind: it stays chosen as the list
	changes under it */
	char chosen_file[HALO_MODS_NAME_BYTES];
	int chosen_kind;
	boolean chosen_valid;
	char message[160];
	unsigned long message_time;
	/* the list last shown (a new one, the screen made again after the
	details or the question: its rows follow the entry chosen, not the
	focus it was made with) */
	struct widget_instance *list;
	long list_creation_time;
} h3_mods;

/* "12.3 MB" ("512 KB", "3.1 GB") */
static void h3_mods_size_text(char *text, int size, int kilobytes)
{
	if (kilobytes >= 1024 * 1024)
		snprintf(text, (size_t)size, "%d.%d GB", kilobytes / (1024 * 1024), (kilobytes % (1024 * 1024)) * 10 / (1024 * 1024));
	else if (kilobytes >= 1024)
		snprintf(text, (size_t)size, "%d.%d MB", kilobytes / 1024, (kilobytes % 1024) * 10 / 1024);
	else
		snprintf(text, (size_t)size, "%d KB", kilobytes);
}

static char const *h3_mods_state_name(struct halo_mod_entry const *entry)
{
	return !entry->valid ? T("FAILED") : entry->enabled ? T("ON") : T("OFF");
}

/* the entry chosen, or NULL */
static struct halo_mod_entry const *h3_mods_chosen(void)
{
	short index = h3_mods.chosen_valid ? mod_manager_find(h3_mods.chosen_file, h3_mods.chosen_kind) : NONE;

	return mod_manager_entry(index);
}

static void h3_mods_choose(struct halo_mod_entry const *entry)
{
	h3_mods.chosen_valid = entry != NULL;
	if (entry)
	{
		snprintf(h3_mods.chosen_file, sizeof(h3_mods.chosen_file), "%s", entry->file);
		h3_mods.chosen_kind = entry->kind;
	}
}

static void h3_mods_message(char const *text)
{
	snprintf(h3_mods.message, sizeof(h3_mods.message), "%s", text);
	h3_mods.message_time = system_milliseconds();
	platform_log("menus: h3 mods: %s", text);
}

/* each entry as the list shows it, said in the log (the tests read it) */
static void h3_mods_said(void)
{
	short index;

	platform_log("menus: h3 mods: %d entries", mod_manager_count());
	for (index = 0; index < mod_manager_count(); index++)
	{
		struct halo_mod_entry const *entry = mod_manager_entry(index);
		char modes[64];

		mod_manager_modes_text(entry->modes, modes, sizeof(modes));
		platform_log("menus: h3 mods: %s: %s, %s, %d KB, %s%s%s, works in %s%s%s", entry->file,
			mod_manager_kind_name(entry->kind), entry->enabled ? "on" : "off", entry->kilobytes,
			entry->valid ? "passed" : "failed", entry->why[0] ? ": " : "", entry->why, modes,
			entry->needs[0] ? ", needs " : "", entry->needs);
	}
}

static void h3_mods_initialize(void)
{
	mod_manager_scan();
	browser_view_reset(&h3_mods.view);
	h3_mods.page = 0;
	h3_mods.message[0] = 0;
	if (!h3_mods_chosen())
		h3_mods_choose(mod_manager_entry(0));
	h3_mods_said();
}

/* (port h3 mods) the rows, the chosen one's facts, the totals */
static void h3_mods_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focused = list->focused_child, *focus = NULL;
	struct halo_mod_entry const *chosen;
	long index, focused_row = NONE, chosen_index, target;
	int used, free_space;
	char text[192], used_text[24], free_text[24];

	if (list != h3_mods.list || list->creation_time != h3_mods.list_creation_time)
	{
		h3_mods.list = list;
		h3_mods.list_creation_time = list->creation_time;
		browser_view_reset(&h3_mods.view);
		focused = NULL;
	}
	for (index = 0, child = list->child; child; child = child->next)
	{
		if (strncmp(child->name, "row_", 4))
			continue;
		if (child == focused)
			focused_row = index;
		index++;
	}
	chosen_index = h3_mods.chosen_valid ? mod_manager_find(h3_mods.chosen_file, h3_mods.chosen_kind) : NONE;
	target = browser_view_follow(&h3_mods.view, H3_MODS_ROWS, mod_manager_count(), (int)focused_row, (int)chosen_index,
		h3_mods.page);
	h3_mods.page = 0;
	for (index = 0, child = list->child; child; child = child->next)
	{
		struct halo_mod_entry const *entry;

		if (strncmp(child->name, "row_", 4))
			continue;
		if (index >= h3_mods.view.visible)
		{
			widget_instance_set_visibility_recursive(child, FALSE);
			child->disabled = TRUE;
			index++;
			continue;
		}
		entry = mod_manager_entry((short)(h3_mods.view.offset + index));
		widget_instance_set_visibility_recursive(child, TRUE);
		child->disabled = FALSE;
		row_column(child, "row_name", entry->kind == HALO_MOD_KIND_MCC_MOD ? entry->title : entry->file);
		row_column(child, "row_kind", mod_manager_kind_name(entry->kind));
		h3_mods_size_text(text, sizeof(text), entry->kilobytes);
		row_column(child, "row_size", text);
		row_column(child, "row_state", h3_mods_state_name(entry));
		if (index == h3_mods.view.row)
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
	h3_mods_choose(target != NONE ? mod_manager_entry((short)target) : NULL);
	chosen = h3_mods_chosen();
	/* the chosen one's facts */
	if (chosen)
	{
		mod_manager_modes_text(chosen->modes, text, sizeof(text));
		text_show(named(screen, "detail_modes"), text);
		if (chosen->valid && chosen->why[0])
			snprintf(text, sizeof(text), "%s (%s)", T("Passed"), chosen->why);
		else if (chosen->valid)
			snprintf(text, sizeof(text), "%s", T("Passed"));
		else
			snprintf(text, sizeof(text), "%s: %s", T("Failed"), chosen->why);
		text_show(named(screen, "detail_check"), text);
		text_show(named(screen, "detail_needs"), chosen->needs[0] ? chosen->needs : T("Nothing more"));
		text_show(named(screen, "detail_source"), mod_manager_source_name(chosen->source));
		text_show(named(screen, "toggle"), chosen->enabled ? T("Turn off") : T("Turn on"));
	}
	else
	{
		text_show(named(screen, "detail_modes"), "");
		text_show(named(screen, "detail_check"), "");
		text_show(named(screen, "detail_needs"), "");
		text_show(named(screen, "detail_source"), "");
	}
	text_show(named(screen, "mods_message"), mod_manager_count() ? "" :
		T("No custom maps or mods yet. Copy maps to ux0:data/haloce-vita/maps,\nmods to ux0:data/haloce-vita/mods."));
	/* the totals, or a delete's outcome for a few seconds */
	if (h3_mods.message[0] && system_milliseconds() - h3_mods.message_time < H3_MODS_MESSAGE_MILLISECONDS)
		snprintf(text, sizeof(text), "%s", h3_mods.message);
	else
	{
		mod_manager_totals(&used, &free_space);
		h3_mods_size_text(used_text, sizeof(used_text), used);
		h3_mods_size_text(free_text, sizeof(free_text), free_space);
		if (mod_manager_count() == 1)
			snprintf(text, sizeof(text), T("1 item, %s used, %s free"), used_text, free_text);
		else
			snprintf(text, sizeof(text), T("%d items, %s used, %s free"), mod_manager_count(), used_text, free_text);
	}
	text_show(named(screen, "mods_status"), text);
}

/* the entry of a row (row_<n>), or the chosen one */
static struct halo_mod_entry const *h3_mods_row_entry(struct widget_instance *widget)
{
	if (widget && !strncmp(widget->name, "row_", 4) && widget->name[4] >= '0' && widget->name[4] <= '9')
	{
		long row = atol(widget->name + 4) - 1;

		if (row >= 0 && row < h3_mods.view.visible)
			return mod_manager_entry((short)(h3_mods.view.offset + row));
	}
	return h3_mods_chosen();
}

/* (port h3 mods toggle: a row's Cross) off or on */
static void h3_mods_toggle(struct widget_instance *widget)
{
	struct halo_mod_entry const *entry = h3_mods_row_entry(widget);
	short index;

	if (!entry)
		return;
	h3_mods_choose(entry);
	index = mod_manager_find(entry->file, entry->kind);
	if (!mod_manager_set_enabled(index, !entry->enabled))
	{
		h3_mods_message(T("This one can't be turned off here"));
		return;
	}
	entry = mod_manager_entry(index);
	platform_log("menus: h3 mods: %s turned %s", entry->file, entry->enabled ? "on" : "off");
}

/* (port h3 mods details: the details' facts) */
static void h3_mods_details_update(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	struct halo_mod_entry const *entry = h3_mods_chosen();
	char text[480], modes[64], size_text[24];
	int length = 0;

	if (!entry)
		return;
	text_show(named(screen, "details_title"), entry->kind == HALO_MOD_KIND_MCC_MOD ? entry->title : entry->file);
	mod_manager_modes_text(entry->modes, modes, sizeof(modes));
	h3_mods_size_text(size_text, sizeof(size_text), entry->kilobytes);
	length += snprintf(text + length, sizeof(text) - (size_t)length, "%s: %s\n", T("Type"), mod_manager_kind_name(entry->kind));
	length += snprintf(text + length, sizeof(text) - (size_t)length, "%s: %s%s\n", T("File"),
		entry->kind == HALO_MOD_KIND_MCC_MOD ? "mods/" : "maps/", entry->file);
	length += snprintf(text + length, sizeof(text) - (size_t)length, "%s: %s\n", T("Size"), size_text);
	length += snprintf(text + length, sizeof(text) - (size_t)length, "%s: %s\n", T("From"), mod_manager_source_name(entry->source));
	length += snprintf(text + length, sizeof(text) - (size_t)length, "%s: %s\n", T("Works in"), modes);
	if (entry->kind == HALO_MOD_KIND_MCC_MOD)
		length += snprintf(text + length, sizeof(text) - (size_t)length, T("Maps: %d, %d of them play\n"), entry->maps,
			entry->maps_valid);
	length += snprintf(text + length, sizeof(text) - (size_t)length, "%s: %s%s%s\n", T("Check"),
		entry->valid ? T("Passed") : T("Failed"), entry->why[0] ? "\n  " : "", entry->why);
	length += snprintf(text + length, sizeof(text) - (size_t)length, "%s: %s\n", T("Needs"),
		entry->needs[0] ? entry->needs : T("Nothing more"));
	snprintf(text + length, sizeof(text) - (size_t)length, "%s: %s", T("Status"),
		entry->enabled ? T("On: in the map lists") : T("Off: kept, but in no map list"));
	text_show(named(screen, "details_text"), text);
}

/* (port h3 mods confirm: the question's words) */
static void h3_mods_confirm_update(struct widget_instance *widget)
{
	struct halo_mod_entry const *entry = h3_mods_chosen();
	char text[256];

	if (!entry)
		return;
	if (entry->kind == HALO_MOD_KIND_MCC_MOD)
		snprintf(text, sizeof(text), T("Delete the mod %.40s and everything in its folder?\nIt can't be undone."), entry->file);
	else
		snprintf(text, sizeof(text), T("Delete %.40s, with its picture and description?\nIt can't be undone."), entry->file);
	text_show(named(screen_of(widget), "confirm_text"), text);
}

/* (port h3 mods delete ask: Square) the question opens for one this game
deletes; for another, why not */
static boolean h3_mods_delete_ask(void)
{
	struct halo_mod_entry const *entry = h3_mods_chosen();

	if (!entry)
		return FALSE;
	if (!entry->deletable)
	{
		h3_mods_message(T("This file's name isn't one this game deletes: use VitaShell"));
		return FALSE;
	}
	platform_log("menus: h3 mods: delete %s?", entry->file);
	return TRUE;
}

/* (port h3 mods delete: YES, DELETE IT) */
static void h3_mods_delete(void)
{
	struct halo_mod_entry const *entry = h3_mods_chosen();
	char file[HALO_MODS_NAME_BYTES], text[160];
	int kind, answer;

	if (!entry)
		return;
	snprintf(file, sizeof(file), "%s", entry->file);
	kind = entry->kind;
	answer = mod_manager_delete(file, kind);
	if (answer == HALO_MODS_ANSWER_DELETED)
		snprintf(text, sizeof(text), T("Deleted %.48s"), file);
	else if (answer == HALO_MODS_ANSWER_IN_USE)
		snprintf(text, sizeof(text), T("%.40s is in use: leave it first"), file);
	else
		snprintf(text, sizeof(text), T("%.48s could not be deleted"), file);
	h3_mods_message(text);
	h3_mods_said();
}


/* ---- the Halo 3 FRIENDS screen and the lobbies' FRIENDS panel
(halo3/friends.xml; port/linux/src/p2p_friends.h): the list's rows are
three of its own (ADD FRIEND, APPEAR OFFLINE, FRIENDS CAN JOIN) and then the
entries p2p_friends_list gives, in its order (requests, invites, friends
online, offline, requests sent, blocked), scrolling as the mod manager's.
The entry chosen stays chosen by its id and kind as the list changes */

#define H3_FRIENDS_ROWS 10
#define H3_FRIENDS_OWN_ROWS 4
#define H3_FRIENDS_MESSAGE_MILLISECONDS 6000
#define H3_FRIENDS_REFRESH_MILLISECONDS 500
#define H3_FRIENDS_PANEL_ROWS 5
/* (the options' party rows: not p2p_friends_act's) */
#define H3_FRIENDS_PARTY_INVITE 1000
#define H3_FRIENDS_PARTY_KICK 1001


/* the three rows of the screen's own */
enum
{
	_h3_friends_row_add,
	_h3_friends_row_offline,
	_h3_friends_row_join,
	/* (the party: its screen, halo3/party.xml) */
	_h3_friends_row_party,
};

static struct
{
	struct browser_view view;
	short page;
	struct p2p_friend_entry entries[P2P_FRIEND_ENTRIES_MAXIMUM];
	int count;
	unsigned long refreshed_time;
	/* the item chosen (an own row's index, or an entry's id and kind) */
	int chosen_item;
	char chosen_id[P2P_FRIEND_ID_SIZE];
	int chosen_kind;
	char message[160];
	unsigned long message_time;
	struct widget_instance *list;
	long list_creation_time;
	/* the options' question: its entry, and what each of its rows does */
	struct p2p_friend_entry options_entry;
	int options[3];
	int option_count;
	/* the lobbies' panel: its own copy, refreshed each second */
	struct p2p_friend_entry panel_entries[P2P_FRIEND_ENTRIES_MAXIMUM];
	int panel_count;
	unsigned long panel_time;
	/* (its status line, and the lines it took, as last said) */
	char status_said[96];
	int status_lines;
} h3_friends;

static void h3_friends_message(char const *text)
{
	snprintf(h3_friends.message, sizeof(h3_friends.message), "%s", text);
	h3_friends.message_time = system_milliseconds();
	platform_log("menus: h3 friends: %s", text);
}

static void h3_friends_refresh(boolean now)
{
	unsigned long time = system_milliseconds();

	if (!now && h3_friends.refreshed_time && time - h3_friends.refreshed_time < H3_FRIENDS_REFRESH_MILLISECONDS)
		return;
	h3_friends.refreshed_time = time ? time : 1;
	h3_friends.count = p2p_friends_list(h3_friends.entries, P2P_FRIEND_ENTRIES_MAXIMUM);
}

static char const *h3_friends_kind_name(int kind)
{
	static char const *const names[] = { "request", "invite", "friend", "request sent", "blocked" };

	return kind >= 0 && kind < (int)NUMBEROF(names) ? names[kind] : "?";
}

/* the list as shown, said in the log (the tests read it) */
static void h3_friends_said(void)
{
	char code[P2P_FRIEND_CODE_SIZE], status[96];
	int index;

	p2p_friends_code(code, sizeof(code));
	p2p_friends_status(status, sizeof(status));
	platform_log("menus: h3 friends: code %s; %s; %d entries", code, status, h3_friends.count);
	for (index = 0; index < h3_friends.count; index++)
	{
		struct p2p_friend_entry const *entry = &h3_friends.entries[index];

		platform_log("menus: h3 friends: %s %s (%s): %s%s", h3_friends_kind_name(entry->kind), entry->name, entry->code,
			entry->activity, entry->joinable ? " [JOIN]" : "");
	}
}

static void h3_friends_initialize(void)
{
	browser_view_reset(&h3_friends.view);
	h3_friends.page = 0;
	h3_friends.message[0] = 0;
	h3_friends.chosen_item = 0;
	h3_friends.chosen_id[0] = 0;
	h3_friends_refresh(TRUE);
	h3_friends_said();
}

/* the item an entry's index is, and the entry of an item (NULL: an own row) */
static struct p2p_friend_entry const *h3_friends_item_entry(int item)
{
	return item >= H3_FRIENDS_OWN_ROWS && item - H3_FRIENDS_OWN_ROWS < h3_friends.count ?
		&h3_friends.entries[item - H3_FRIENDS_OWN_ROWS] : NULL;
}

static int h3_friends_chosen_index(void)
{
	int index;

	if (!h3_friends.chosen_id[0])
		return h3_friends.chosen_item < H3_FRIENDS_OWN_ROWS ? h3_friends.chosen_item : 0;
	for (index = 0; index < h3_friends.count; index++)
		if (h3_friends.entries[index].kind == h3_friends.chosen_kind &&
			!strcmp(h3_friends.entries[index].id, h3_friends.chosen_id))
			return H3_FRIENDS_OWN_ROWS + index;
	return -1;
}

static void h3_friends_choose(int item)
{
	struct p2p_friend_entry const *entry = h3_friends_item_entry(item);

	h3_friends.chosen_item = item;
	h3_friends.chosen_id[0] = 0;
	if (entry)
	{
		snprintf(h3_friends.chosen_id, sizeof(h3_friends.chosen_id), "%s", entry->id);
		h3_friends.chosen_kind = entry->kind;
	}
}

/* what Cross and Square do to an entry, as the strip says it */
static char const *h3_friends_first_name(struct p2p_friend_entry const *entry)
{
	if (!entry)
		return T("Select");
	switch (entry->kind)
	{
	case P2P_FRIEND_KIND_REQUEST:
	case P2P_FRIEND_KIND_INVITE:
		return T("Accept");
	case P2P_FRIEND_KIND_FRIEND:
		return entry->joinable ? T("Join") : "";
	}
	return "";
}

static char const *h3_friends_second_name(struct p2p_friend_entry const *entry)
{
	if (!entry)
		return "";
	switch (entry->kind)
	{
	case P2P_FRIEND_KIND_REQUEST:
		return T("Decline");
	case P2P_FRIEND_KIND_INVITE:
		return T("Ignore");
	case P2P_FRIEND_KIND_FRIEND:
		return entry->invitable ? T("Invite") : "";
	case P2P_FRIEND_KIND_OUTGOING:
		return T("Cancel");
	}
	return "";
}

/* the tag at a row's right */
static char const *h3_friends_tag(struct p2p_friend_entry const *entry)
{
	switch (entry->kind)
	{
	case P2P_FRIEND_KIND_REQUEST:
		return T("REQUEST");
	case P2P_FRIEND_KIND_INVITE:
		return T("INVITE");
	case P2P_FRIEND_KIND_FRIEND:
		return entry->joinable ? T("JOINABLE") : entry->online ? T("ONLINE") : "";
	case P2P_FRIEND_KIND_OUTGOING:
		return T("SENT");
	case P2P_FRIEND_KIND_BLOCKED:
		return T("BLOCKED");
	}
	return "";
}

/* a friend's game with a password being joined (its password typed, then
opened on the lobby's thread: h3_friends_update) */
static struct
{
	char id[P2P_FRIEND_ID_SIZE];
	char name[P2P_FRIEND_NAME_SIZE];
	boolean typing, watching;
} h3_friends_password;

/* (port h3 friends) the rows, the chosen one's facts, the code and status */
static void h3_friends_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focused = list->focused_child, *focus = NULL;
	struct p2p_friend_entry const *chosen;
	long index, focused_row = NONE, target;
	char text[192], typed[HALO_TEXT_INPUT_SIZE];

	if (typing_done(_typing_friend_code, typed, sizeof(typed)))
	{
		int result = p2p_friends_add(typed);

		h3_friends_message(p2p_friends_add_text(result));
		h3_friends_refresh(TRUE);
	}
	/* (a friend's game with a password: typed, opened, then joined as any) */
	if (h3_friends_password.typing && typing_done(_typing_friend_password, typed, sizeof(typed)))
	{
		int outcome = p2p_friends_join_password(h3_friends_password.id, typed);

		memset(typed, 0, sizeof(typed));
		h3_friends_password.typing = FALSE;
		h3_friends_password.watching = outcome == P2P_FRIENDS_DONE;
		h3_friends_message(outcome == P2P_FRIENDS_DONE ? T("Opening the game with its password...") :
			p2p_friends_outcome_text(outcome));
	}
	if (h3_friends_password.watching)
	{
		int state = p2p_lobby_join_state();

		if (state == P2P_LOBBY_JOIN_JOINING)
		{
			h3_friends_password.watching = FALSE;
			snprintf(text, sizeof(text), T("Joining %s's game..."), h3_friends_password.name);
			h3_friends_message(text);
			menu_functions.open_system_link = TRUE;
		}
		else if (state == P2P_LOBBY_JOIN_WRONG_PASSWORD || state == P2P_LOBBY_JOIN_GONE)
		{
			h3_friends_password.watching = FALSE;
			h3_friends_message(state == P2P_LOBBY_JOIN_WRONG_PASSWORD ? T("That is not the game's password") :
				T("Their game can't be joined now"));
		}
	}
	h3_friends_refresh(FALSE);
	if (list != h3_friends.list || list->creation_time != h3_friends.list_creation_time)
	{
		h3_friends.list = list;
		h3_friends.list_creation_time = list->creation_time;
		browser_view_reset(&h3_friends.view);
		focused = NULL;
	}
	for (index = 0, child = list->child; child; child = child->next)
	{
		if (strncmp(child->name, "row_", 4))
			continue;
		if (child == focused)
			focused_row = index;
		index++;
	}
	target = browser_view_follow(&h3_friends.view, H3_FRIENDS_ROWS, H3_FRIENDS_OWN_ROWS + h3_friends.count,
		(int)focused_row, h3_friends_chosen_index(), h3_friends.page);
	h3_friends.page = 0;
	for (index = 0, child = list->child; child; child = child->next)
	{
		long item = h3_friends.view.offset + index;
		struct p2p_friend_entry const *entry;

		if (strncmp(child->name, "row_", 4))
			continue;
		if (index >= h3_friends.view.visible)
		{
			widget_instance_set_visibility_recursive(child, FALSE);
			child->disabled = TRUE;
			index++;
			continue;
		}
		widget_instance_set_visibility_recursive(child, TRUE);
		child->disabled = FALSE;
		entry = h3_friends_item_entry((int)item);
		if (!entry)
		{
			switch (item)
			{
			case _h3_friends_row_add:
				row_column(child, "row_name", T("ADD FRIEND"));
				row_column(child, "row_state", T("Type a friend's code"));
				row_column(child, "row_tag", " ");
				break;
			case _h3_friends_row_offline:
				row_column(child, "row_name", T("APPEAR OFFLINE"));
				row_column(child, "row_state", T("Friends see you offline"));
				row_column(child, "row_tag", config_boolean_live("friends.appear_offline", "HALO_FRIENDS_APPEAR_OFFLINE") ?
					T("ON") : T("OFF"));
				break;
			case _h3_friends_row_join:
				row_column(child, "row_name", T("FRIENDS CAN JOIN"));
				row_column(child, "row_state", T("Your games, public or private, in one press"));
				row_column(child, "row_tag", config_boolean_live("friends.join", "HALO_FRIENDS_JOIN") ? T("YES") : T("NO"));
				break;
			default:
			{
				char party[96], tag[16];
				int size = p2p_party_status(party, sizeof(party));

				snprintf(tag, sizeof(tag), "%d/%d", size, P2P_PARTY_MAXIMUM);
				row_column(child, "row_name", T("PARTY"));
				row_column(child, "row_state", party);
				row_column(child, "row_tag", size ? tag : " ");
				break;
			}
			}
		}
		else
		{
			char name[P2P_FRIEND_NAME_SIZE + 8];

			/* (a friend on this project's PC build: "(PC)" after the name) */
			snprintf(name, sizeof(name), entry->pc ? "%s (PC)" : "%s", entry->name);
			row_column(child, "row_name", name);
			row_column(child, "row_state", entry->activity);
			row_column(child, "row_tag", h3_friends_tag(entry)[0] ? h3_friends_tag(entry) : " ");
		}
		if (index == h3_friends.view.row)
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
	if (target != NONE)
		h3_friends_choose((int)target);
	chosen = h3_friends_item_entry(h3_friends_chosen_index());
	p2p_friends_code(text, sizeof(text));
	text_show(named(screen, "code_value"), text);
	if (h3_friends.message[0] && system_milliseconds() - h3_friends.message_time < H3_FRIENDS_MESSAGE_MILLISECONDS)
		text_show(named(screen, "friends_message"), h3_friends.message);
	else
		text_show(named(screen, "friends_message"), "");
	p2p_friends_status(text, sizeof(text));
	text_show(named(screen, "friends_status"), text);
	if (chosen)
	{
		snprintf(text, sizeof(text), "%s   %s", chosen->name, chosen->kind == P2P_FRIEND_KIND_OUTGOING ? "" : chosen->code);
		text_show(named(screen, "detail_name"), text);
		if (chosen->kind == P2P_FRIEND_KIND_FRIEND && chosen->online && chosen->maximum)
			snprintf(text, sizeof(text), T("%s - %d of %d players"), chosen->activity, chosen->players, chosen->maximum);
		else
			snprintf(text, sizeof(text), "%s", chosen->activity);
		text_show(named(screen, "detail_state"), text);
		switch (chosen->kind)
		{
		case P2P_FRIEND_KIND_REQUEST:
			text_show(named(screen, "detail_help"), T("Accept to be friends: you see each other online and join."));
			break;
		case P2P_FRIEND_KIND_INVITE:
			text_show(named(screen, "detail_help"), T("Accept joins their game now."));
			break;
		case P2P_FRIEND_KIND_FRIEND:
			if (chosen->party[0])
				text_show(named(screen, "detail_help"), chosen->party);
			else
				text_show(named(screen, "detail_help"), chosen->joinable ? T("Their game is open: join it in one press.") :
					chosen->online ? T("Their game can't be joined now. Host one and invite them.") :
					T("They're offline, or appearing offline."));
			break;
		case P2P_FRIEND_KIND_OUTGOING:
			text_show(named(screen, "detail_help"), T("Waiting for them to accept."));
			break;
		default:
			text_show(named(screen, "detail_help"), T("Blocked: their requests and invites never reach you."));
			break;
		}
	}
	else
	{
		static char const *const helps[] = {
			N_("Give your code to a friend, or type theirs: both must agree."),
			N_("Your friends see you offline; you still see them."),
			N_("Friends join your public games without a password in one press."),
			N_("Your party: who's in it, and leave. Triangle on a friend invites them."),
		};
		int item = h3_friends_chosen_index();

		text_show(named(screen, "detail_name"), "");
		text_show(named(screen, "detail_state"), "");
		text_show(named(screen, "detail_help"), h3_buttons(T(helps[item >= 0 && item < H3_FRIENDS_OWN_ROWS ? item : 0])));
	}
	text_show(named(screen, "a_label"), h3_friends_first_name(chosen));
	text_show(named(screen, "x_label"), h3_friends_second_name(chosen));
	/* (a button with nothing to do: its icon hidden too) */
	if (named(screen, "a_butn"))
		widget_instance_set_visibility_recursive(named(screen, "a_butn"), h3_friends_first_name(chosen)[0] != 0);
	if (named(screen, "x_butn"))
		widget_instance_set_visibility_recursive(named(screen, "x_butn"), h3_friends_second_name(chosen)[0] != 0);
}

/* the item of a row (row_<n>), or the chosen one */
static int h3_friends_row_item(struct widget_instance *widget)
{
	if (widget && !strncmp(widget->name, "row_", 4) && widget->name[4] >= '0' && widget->name[4] <= '9')
	{
		long row = atol(widget->name + 4) - 1;

		if (row >= 0 && row < h3_friends.view.visible)
			return (int)(h3_friends.view.offset + row);
	}
	return h3_friends_chosen_index();
}

/* an action on an entry, its outcome said (a join: the System Link screen
next, as JOIN WITH CODE; a game with a password asks for it first) */
static void h3_friends_do(struct p2p_friend_entry const *entry, int action)
{
	struct p2p_friend_entry copy = *entry;
	int outcome = p2p_friends_act(copy.id, copy.kind, action);
	char text[160];

	if (outcome == P2P_FRIENDS_PASSWORD)
	{
		snprintf(h3_friends_password.id, sizeof(h3_friends_password.id), "%s", copy.id);
		snprintf(h3_friends_password.name, sizeof(h3_friends_password.name), "%s", copy.name);
		h3_friends_password.typing = TRUE;
		typing_begin(_typing_friend_password, T("Password"), "", P2P_LOBBY_PASSWORD_SIZE - 1, TRUE);
		h3_friends_message(p2p_friends_outcome_text(outcome));
		return;
	}
	if (outcome == P2P_FRIENDS_DONE && (action == P2P_FRIENDS_JOIN || action == P2P_FRIENDS_INVITE_ACCEPT))
	{
		snprintf(text, sizeof(text), T("Joining %s's game..."), copy.name);
		h3_friends_message(text);
		menu_functions.open_system_link = TRUE;
	}
	else if (outcome == P2P_FRIENDS_DONE && action == P2P_FRIENDS_INVITE)
	{
		snprintf(text, sizeof(text), T("Invited %s"), copy.name);
		h3_friends_message(text);
	}
	else
		h3_friends_message(p2p_friends_outcome_text(outcome));
	h3_friends_refresh(TRUE);
}

/* (port h3 friends act: a row's Cross) */
static void h3_friends_act(struct widget_instance *widget)
{
	int item = h3_friends_row_item(widget);
	struct p2p_friend_entry const *entry = h3_friends_item_entry(item);

	h3_friends_choose(item);
	if (!entry)
	{
		boolean on;

		switch (item)
		{
		case _h3_friends_row_add:
			typing_begin(_typing_friend_code, T("Friend code"), "", P2P_FRIEND_CODE_SIZE - 1, FALSE);
			break;
		case _h3_friends_row_offline:
			on = !config_boolean_live("friends.appear_offline", "HALO_FRIENDS_APPEAR_OFFLINE");
			setting_set("HALO_FRIENDS_APPEAR_OFFLINE", on ? "true" : "false");
			h3_friends_message(on ? T("Appearing offline") : T("Appearing online"));
			break;
		case _h3_friends_row_join:
			on = !config_boolean_live("friends.join", "HALO_FRIENDS_JOIN");
			setting_set("HALO_FRIENDS_JOIN", on ? "true" : "false");
			h3_friends_message(on ? T("Friends can join your games, public or private") :
				T("Friends join only by your invites"));
			break;
		case _h3_friends_row_party:
			menu_functions.open_party = TRUE;
			platform_log("menus: h3 friends: the PARTY screen next");
			break;
		}
		return;
	}
	switch (entry->kind)
	{
	case P2P_FRIEND_KIND_REQUEST:
		h3_friends_do(entry, P2P_FRIENDS_ACCEPT);
		break;
	case P2P_FRIEND_KIND_INVITE:
		h3_friends_do(entry, P2P_FRIENDS_INVITE_ACCEPT);
		break;
	case P2P_FRIEND_KIND_FRIEND:
	{
		char warning[192];

		/* (the party's leader: told first of a game with fewer open slots than
		the party; Cross again joins) */
		if (entry->joinable && p2p_party_join_warning(entry->players, entry->maximum, warning, sizeof(warning)) &&
			!party_warning_confirmed(entry->id))
		{
			snprintf(warning + strlen(warning), sizeof(warning) - strlen(warning), " %s", h3_buttons(T("Cross again: join anyway.")));
			h3_friends_message(warning);
			break;
		}
		h3_friends_do(entry, P2P_FRIENDS_JOIN);
		break;
	}
	case P2P_FRIEND_KIND_OUTGOING:
		h3_friends_message(T("Waiting for them to accept."));
		break;
	default:
		h3_friends_message(h3_buttons(T("Triangle: unblock")));
		break;
	}
}

/* (port h3 friends second: Square) */
static void h3_friends_second(void)
{
	struct p2p_friend_entry const *entry = h3_friends_item_entry(h3_friends_chosen_index());

	if (!entry)
		return;
	switch (entry->kind)
	{
	case P2P_FRIEND_KIND_REQUEST:
		h3_friends_do(entry, P2P_FRIENDS_DECLINE);
		break;
	case P2P_FRIEND_KIND_INVITE:
		h3_friends_do(entry, P2P_FRIENDS_INVITE_IGNORE);
		break;
	case P2P_FRIEND_KIND_FRIEND:
		h3_friends_do(entry, P2P_FRIENDS_INVITE);
		break;
	case P2P_FRIEND_KIND_OUTGOING:
		h3_friends_do(entry, P2P_FRIENDS_CANCEL);
		break;
	}
}

/* (port h3 friends options ask: Triangle) the options' question opens for
an entry that has some */
static boolean h3_friends_options_ask(void)
{
	struct p2p_friend_entry const *entry = h3_friends_item_entry(h3_friends_chosen_index());

	h3_friends.option_count = 0;
	if (!entry)
		return FALSE;
	h3_friends.options_entry = *entry;
	switch (entry->kind)
	{
	case P2P_FRIEND_KIND_FRIEND:
		/* (the party's: invite them, or the leader removes them) */
		if (p2p_party_friend_state(entry->id) == 1)
		{
			if (p2p_party_leading())
				h3_friends.options[h3_friends.option_count++] = H3_FRIENDS_PARTY_KICK;
		}
		else if (p2p_party_invitable(entry->id))
			h3_friends.options[h3_friends.option_count++] = H3_FRIENDS_PARTY_INVITE;
		h3_friends.options[h3_friends.option_count++] = P2P_FRIENDS_REMOVE;
		h3_friends.options[h3_friends.option_count++] = P2P_FRIENDS_BLOCK;
		break;
	case P2P_FRIEND_KIND_REQUEST:
		h3_friends.options[h3_friends.option_count++] = P2P_FRIENDS_ACCEPT;
		h3_friends.options[h3_friends.option_count++] = P2P_FRIENDS_DECLINE;
		h3_friends.options[h3_friends.option_count++] = P2P_FRIENDS_BLOCK;
		break;
	case P2P_FRIEND_KIND_INVITE:
		h3_friends.options[h3_friends.option_count++] = P2P_FRIENDS_INVITE_ACCEPT;
		h3_friends.options[h3_friends.option_count++] = P2P_FRIENDS_INVITE_IGNORE;
		break;
	case P2P_FRIEND_KIND_OUTGOING:
		h3_friends.options[h3_friends.option_count++] = P2P_FRIENDS_CANCEL;
		break;
	case P2P_FRIEND_KIND_BLOCKED:
		h3_friends.options[h3_friends.option_count++] = P2P_FRIENDS_UNBLOCK;
		break;
	}
	platform_log("menus: h3 friends: options for %s %s", h3_friends_kind_name(entry->kind), entry->name);
	return h3_friends.option_count > 0;
}

static char const *h3_friends_option_name(int action)
{
	switch (action)
	{
	case H3_FRIENDS_PARTY_INVITE:
		return T("INVITE TO PARTY");
	case H3_FRIENDS_PARTY_KICK:
		return T("REMOVE FROM PARTY");
	case P2P_FRIENDS_REMOVE:
		return T("REMOVE FRIEND");
	case P2P_FRIENDS_BLOCK:
		return T("BLOCK");
	case P2P_FRIENDS_UNBLOCK:
		return T("UNBLOCK");
	case P2P_FRIENDS_ACCEPT:
	case P2P_FRIENDS_INVITE_ACCEPT:
		return T("ACCEPT");
	case P2P_FRIENDS_DECLINE:
		return T("DECLINE");
	case P2P_FRIENDS_INVITE_IGNORE:
		return T("IGNORE");
	case P2P_FRIENDS_CANCEL:
		return T("CANCEL REQUEST");
	}
	return "";
}

/* (port h3 friends options: the question's words and rows) */
static void h3_friends_options_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child;
	char text[192];

	text_show(named(screen, "options_title"), h3_friends.options_entry.name);
	snprintf(text, sizeof(text), "%s\n%s", h3_friends.options_entry.code, h3_friends.options_entry.activity);
	text_show(named(screen, "options_text"), text);
	for (child = list->child; child; child = child->next)
	{
		long option;

		if (strncmp(child->name, "option_", 7) || !(child->name[7] >= '1' && child->name[7] <= '3'))
			continue;
		option = child->name[7] - '1';
		if (option < h3_friends.option_count)
		{
			widget_instance_set_visibility_recursive(child, TRUE);
			child->disabled = FALSE;
			text_show(child, h3_friends_option_name(h3_friends.options[option]));
		}
		else
		{
			widget_instance_set_visibility_recursive(child, FALSE);
			child->disabled = TRUE;
		}
	}
}

/* (port h3 friends option: a row of the question) */
static void h3_friends_option(struct widget_instance *widget)
{
	long option = widget && !strncmp(widget->name, "option_", 7) ? widget->name[7] - '1' : -1;

	if (option < 0 || option >= h3_friends.option_count)
		return;
	if (h3_friends.options[option] == H3_FRIENDS_PARTY_INVITE || h3_friends.options[option] == H3_FRIENDS_PARTY_KICK)
	{
		int invite = h3_friends.options[option] == H3_FRIENDS_PARTY_INVITE;
		int outcome = invite ? p2p_party_invite(h3_friends.options_entry.id) : p2p_party_kick(h3_friends.options_entry.id);
		char text[160];

		if (outcome == P2P_PARTY_DONE)
			snprintf(text, sizeof(text), invite ? T("Invited %s to your party") : T("Removed %s from the party"),
				h3_friends.options_entry.name);
		else
			snprintf(text, sizeof(text), "%s", p2p_party_outcome_text(outcome));
		h3_friends_message(text);
		h3_friends_refresh(TRUE);
		return;
	}
	h3_friends_do(&h3_friends.options_entry, h3_friends.options[option]);
}

/* (port h3 party plates: the lobbies' and the main menu's) the party as
Halo 3 shows it: each member but this machine a nameplate of its own,
stacked under the player's (h3/main/nameplate), the longest-standing first,
added as they join and taken away as they leave; the leader's marked
LEADER (this machine's own nameplate too when it leads). Members tell no
colour of theirs (the party's messages carry none: p2p_party.h), so each
member's plate is the nameplate's own green, the player's in the
profile's colour above them. Hidden where the player's nameplate is (a
live lobby lists the game's players in its place: h3_live_lobby_show).
The main menu's live servers (h3/main/servers) move down under the last
plate. The PARTY screen (FRIENDS' PARTY row, the settings panel's
Multiplayer > Party) keeps the party's steps: leave, remove, mute, chat */
#define H3_PARTY_PLATES (P2P_PARTY_MAXIMUM - 1)
#define H3_PARTY_PLATE_SPACING 18

#ifndef HALO_RELEASE
/* (the tests' made-up party, HALO_TEST_PARTY_PLATES: comma-separated
NAME[*][@FROM-UNTIL], '*' the leader, FROM and UNTIL seconds since the
plates were first drawn - the member in the party between them -; this
machine its leader unless one is marked) */
static int h3_party_plates_fake(struct p2p_party_member *members, int maximum)
{
	static unsigned long first;
	char const *setting = getenv("HALO_TEST_PARTY_PLATES");
	char list[256], *entry, *rest;
	unsigned long seconds;
	int count = 1, leader_marked = 0;

	if (!setting || !setting[0] || maximum < 2)
		return 0;
	if (!first)
		first = system_milliseconds() ? system_milliseconds() : 1;
	seconds = (system_milliseconds() - first) / 1000;
	memset(members, 0, sizeof(*members) * (size_t)maximum);
	snprintf(list, sizeof(list), "%s", setting);
	for (entry = strtok_r(list, ",", &rest); entry && count < maximum; entry = strtok_r(NULL, ",", &rest))
	{
		char *at = strchr(entry, '@');
		unsigned long from = 0, until = ~0UL;
		size_t length;
		int leader;

		if (at)
		{
			*at++ = 0;
			from = strtoul(at, &at, 10);
			if (*at == '-')
				until = strtoul(at + 1, NULL, 10);
		}
		length = strlen(entry);
		leader = length && entry[length - 1] == '*';
		if (leader)
			entry[--length] = 0;
		if (!length || seconds < from || seconds >= until)
			continue;
		snprintf(members[count].id, sizeof(members[count].id), "test-%d", count);
		snprintf(members[count].name, sizeof(members[count].name), "%s", entry);
		members[count].leader = leader && !leader_marked;
		members[count].online = 1;
		leader_marked |= members[count].leader;
		count++;
	}
	if (count < 2)
		return 0;
	members[0].self = 1;
	members[0].leader = !leader_marked;
	h3_player_name(members[0].name, (int)sizeof(members[0].name));
	return count;
}
#endif

static void h3_party_plates_update(struct widget_instance *container)
{
	static struct p2p_party_member party[P2P_PARTY_MAXIMUM];
	static unsigned long party_time;
	static int party_count;
	static char said[256];
	struct widget_instance *screen = screen_of(container);
	struct widget_instance *plate = named(screen, "nameplate"), *found;
	unsigned long time = system_milliseconds();
	boolean shown = plate && plate->visible;
	char line[256];
	int member, row = 0, length = 0;
	boolean self_leads = FALSE;

	/* (read again each second, as the FRIENDS panel's) */
	if (!party_time || time - party_time >= 1000)
	{
		party_time = time ? time : 1;
		party_count = p2p_party_members(party, P2P_PARTY_MAXIMUM);
#ifndef HALO_RELEASE
		if (party_count < 2)
			party_count = h3_party_plates_fake(party, P2P_PARTY_MAXIMUM);
#endif
	}
	line[0] = 0;
	for (member = 0; shown && party_count > 1 && member < party_count; member++)
	{
		char name[24];

		if (party[member].self)
		{
			self_leads = party[member].leader != 0;
			continue;
		}
		if (row >= H3_PARTY_PLATES)
			break;
		row++;
		snprintf(name, sizeof(name), "party_plate_%d", row);
		if ((found = named(container, name)) != NULL)
		{
			widget_instance_set_visibility_recursive(found, TRUE);
			found->animation.current_frame_index = 0;
		}
		snprintf(name, sizeof(name), "party_name_%d", row);
		row_column(container, name, party[member].name);
		snprintf(name, sizeof(name), "party_tag_%d", row);
		text_show(named(container, name), party[member].leader ? T("LEADER") : "");
		if (length < (int)sizeof(line) - 1)
			length += snprintf(line + length, sizeof(line) - (size_t)length, "%s%s%s", row > 1 ? ", " : "",
				party[member].name, party[member].leader ? " (leader)" : "");
	}
	for (member = row + 1; member <= H3_PARTY_PLATES; member++)
	{
		char name[24];

		snprintf(name, sizeof(name), "party_plate_%d", member);
		if ((found = named(container, name)) != NULL)
			widget_instance_set_visibility_recursive(found, FALSE);
	}
	if ((found = named(container, "party_self_tag")) != NULL)
	{
		text_show(found, row && self_leads ? T("LEADER") : "");
		widget_instance_set_visibility_recursive(found, row > 0);
	}
	/* (the count over the nameplates: the player and the members shown -
	the lobbies' own words take it, h3_party_players_text -; the main menu's
	and the campaign lobby's "1 Player" (h3/main/players) said here) */
	if (shown)
		h3_party_players = row + 1;
	if (shown && !named(screen, "invite") && (found = named(screen, "players")) != NULL)
	{
		char count[64];

		h3_party_players_text(count, (int)sizeof(count), 0);
		text_show(found, count);
	}
	/* (the main menu's live servers under the last plate) */
	if ((found = named(screen, "servers")) != NULL)
		found->vertical_offset = (short)(row * H3_PARTY_PLATE_SPACING);
	/* (said each time it changes: the tests read it) */
	{
		char summary[256];
		char own[64];

		h3_player_name(own, (int)sizeof(own));
		if (row)
			snprintf(summary, sizeof(summary), "%d under %s%s: %s", row, own, self_leads ? " (leader)" : "", line);
		else
			snprintf(summary, sizeof(summary), "none%s", shown ? "" : " (the nameplate hidden)");
		if (strcmp(summary, said))
		{
			/* (no word of none at a start: only once plates were shown) */
			if (row || said[0])
				platform_log("menus: party plates: %s", summary);
			snprintf(said, sizeof(said), "%s", summary);
		}
	}
}

/* (port h3 friends panel: the lobbies') who is online and what they do */
/* a line of the port's text in a text box, in at most two lines: as it is
when it fits the box's width, else broken at the last space that leaves
both parts fitting (its "\n"), else cut with "..." (row_column); the
lines it took, 0 when cut */
static boolean text_too_wide(struct widget_instance *widget, char const *text, short width)
{
	unsigned short characters[192];
	long length = halo_menus_utf16(text, characters, (long)NUMBEROF(characters)), index;

	for (index = 0; index < length - 1; index++)
	{
		if (characters[index] > 0xFF)
			characters[index] = '?';
	}
	return ui_widget_port_text_too_wide(widget, (wchar_t const *)characters, width);
}

static int text_show_two_lines(struct widget_instance *widget, char const *text)
{
	char lines[192];
	short width;
	int split;

	if (!widget || widget->type != _ui_widget_type_text_box)
		return 0;
	width = ui_widget_port_text_box_width(widget);
	if (width <= 0 || !text_too_wide(widget, text, width))
	{
		text_show(widget, text);
		return 1;
	}
	snprintf(lines, sizeof(lines), "%s", text);
	for (split = (int)strlen(lines) - 1; split > 0; split--)
	{
		if (lines[split] != ' ')
			continue;
		lines[split] = 0;
		if (!text_too_wide(widget, lines, width) && !text_too_wide(widget, lines + split + 1, width))
		{
			lines[split] = '\n';
			text_show(widget, lines);
			return 2;
		}
		lines[split] = ' ';
	}
	row_column(widget, widget->name, text);
	return 0;
}

static void h3_friends_panel_update(struct widget_instance *panel)
{
	unsigned long time = system_milliseconds();
	char text[96];
	int shown = 0, online = 0, requests = 0, index, row;

	if (!h3_friends.panel_time || time - h3_friends.panel_time >= 1000)
	{
		h3_friends.panel_time = time ? time : 1;
		h3_friends.panel_count = p2p_friends_list(h3_friends.panel_entries, P2P_FRIEND_ENTRIES_MAXIMUM);
	}
	for (index = 0; index < h3_friends.panel_count; index++)
	{
		online += h3_friends.panel_entries[index].kind == P2P_FRIEND_KIND_FRIEND && h3_friends.panel_entries[index].online;
		requests += h3_friends.panel_entries[index].kind == P2P_FRIEND_KIND_REQUEST ||
			h3_friends.panel_entries[index].kind == P2P_FRIEND_KIND_INVITE;
	}
	{
		static struct p2p_party_member party[P2P_PARTY_MAXIMUM];
		static unsigned long party_time;
		static int party_count;

		/* (the party's size in the count; its members are nameplates of
		their own under the player's, Halo 3's: h3_party_plates_update) */
		if (!party_time || time - party_time >= 1000)
		{
			party_time = time ? time : 1;
			party_count = p2p_party_members(party, P2P_PARTY_MAXIMUM);
		}
		if (party_count > 1)
			snprintf(text, sizeof(text), T("PARTY %d/%d, %d ONLINE"), party_count, P2P_PARTY_MAXIMUM, online);
		else if (requests)
			snprintf(text, sizeof(text), T("%d ONLINE, %d NEW"), online, requests);
		else
			snprintf(text, sizeof(text), T("%d ONLINE"), online);
		text_show(named(panel, "lobby_count"), text);
		row = 1;
	}
	for (index = 0; row <= H3_FRIENDS_PANEL_ROWS; row++)
	{
		char name[24], state[24];
		char const *name_text = "", *state_text = "";

		/* (requests and invites first, then the friends online) */
		for (; index < h3_friends.panel_count; index++)
		{
			struct p2p_friend_entry const *entry = &h3_friends.panel_entries[index];

			if (entry->kind == P2P_FRIEND_KIND_REQUEST || entry->kind == P2P_FRIEND_KIND_INVITE ||
				(entry->kind == P2P_FRIEND_KIND_FRIEND && entry->online))
			{
				name_text = entry->name;
				state_text = entry->activity;
				index++;
				shown++;
				break;
			}
		}
		if (!name_text[0] && row == 1)
		{
			p2p_friends_status(text, sizeof(text));
			state_text = text;
		}
		snprintf(name, sizeof(name), "lobby_name_%d", row);
		snprintf(state, sizeof(state), "lobby_state_%d", row);
		text_show(named(panel, name), name_text);
		if (row == 1 && !name_text[0])
		{
			/* (the status across the panel, in its own box: two lines when
			one is too narrow - "Go online to see your friends" was cut
			short in the name's column, and Spanish is longer) */
			int lines = text_show_two_lines(named(panel, "lobby_status"), state_text);

			text_show(named(panel, state), "");
			if (strcmp(state_text, h3_friends.status_said) || lines != h3_friends.status_lines)
			{
				snprintf(h3_friends.status_said, sizeof(h3_friends.status_said), "%s", state_text);
				h3_friends.status_lines = lines;
				platform_log("menus: h3 friends panel: status \"%s\" %s", state_text, lines == 1 ? "fits one line" :
					lines == 2 ? "fits two lines" : "cut");
			}
		}
		else if (row == 1)
			text_show(named(panel, "lobby_status"), "");
		/* (a doing wider than its column cut with "...", as the lists' rows) */
		else if (state_text[0])
			row_column(panel, state, state_text);
		else
			text_show(named(panel, state), "");
	}
	/* (said once each time it changes: the tests read it) */
	{
		static char said[160];
		char line[160];

		snprintf(line, sizeof(line), "%s, %d shown", text, shown);
		if (strcmp(line, said))
		{
			snprintf(said, sizeof(said), "%s", line);
			platform_log("menus: h3 friends panel: %s", line);
		}
	}
}

static boolean party_warning_confirmed(char const *id)
{
	unsigned long now = system_milliseconds();

	if (!strcmp(menu_functions.party_warned_id, id) && menu_functions.party_warned_time &&
		now - menu_functions.party_warned_time < 8000)
	{
		menu_functions.party_warned_id[0] = 0;
		platform_log("menus: party: joined anyway (fewer open slots than the party)");
		return TRUE;
	}
	snprintf(menu_functions.party_warned_id, sizeof(menu_functions.party_warned_id), "%s", id);
	menu_functions.party_warned_time = now ? now : 1;
	platform_log("menus: party: the game has fewer open slots than the party: the leader warned");
	return FALSE;
}

/* ---- the Halo 3 PARTY screen (halo3/party.xml; port/linux/src/p2p_party.h):
LEAVE PARTY, the party invites waiting (Cross joins the party, Square says
no), then the members (the longest-standing first, the leader marked, what
each does or why they could not follow); Cross on a member: its options
(MUTE or UNMUTE; REMOVE FROM PARTY, the leader's; LEAVE PARTY on this
machine's own row) */

#define H3_PARTY_ROWS 10
#define H3_PARTY_MESSAGE_MILLISECONDS 6000

/* the options' rows */
enum
{
	_h3_party_option_mute = 1,
	_h3_party_option_unmute,
	_h3_party_option_kick,
	_h3_party_option_leave,
};

static struct
{
	struct browser_view view;
	short page;
	struct p2p_party_member members[P2P_PARTY_MAXIMUM];
	int member_count;
	struct p2p_party_invite invites[P2P_FRIEND_INVITES_MAXIMUM];
	int invite_count;
	unsigned long refreshed_time;
	/* the item chosen: LEAVE PARTY (-1), or an invite's or a member's id */
	char chosen_id[P2P_FRIEND_ID_SIZE];
	char message[160];
	unsigned long message_time;
	struct widget_instance *list;
	long list_creation_time;
	struct p2p_party_member options_member;
	int options[2];
	int option_count;
	char said[400];
} h3_party;

static void h3_party_message(char const *text)
{
	snprintf(h3_party.message, sizeof(h3_party.message), "%s", text);
	h3_party.message_time = system_milliseconds();
	platform_log("menus: h3 party: %s", text);
}

static void h3_party_chat_outcome(int outcome);

/* the rows' items: LEAVE PARTY (in a party), the invites, the members */
static int h3_party_leave_rows(void)
{
	return h3_party.member_count > 1 ? 1 : 0;
}

static int h3_party_items(void)
{
	return h3_party_leave_rows() + h3_party.invite_count + h3_party.member_count;
}

static struct p2p_party_invite const *h3_party_item_invite(int item)
{
	item -= h3_party_leave_rows();
	return item >= 0 && item < h3_party.invite_count ? &h3_party.invites[item] : NULL;
}

static struct p2p_party_member const *h3_party_item_member(int item)
{
	item -= h3_party_leave_rows() + h3_party.invite_count;
	return item >= 0 && item < h3_party.member_count ? &h3_party.members[item] : NULL;
}

static char const *h3_party_item_id(int item)
{
	struct p2p_party_invite const *invite = h3_party_item_invite(item);
	struct p2p_party_member const *member = h3_party_item_member(item);

	return invite ? invite->id : member ? member->id : "";
}

static int h3_party_chosen_index(void)
{
	int item;

	for (item = 0; item < h3_party_items(); item++)
		if (!strcmp(h3_party_item_id(item), h3_party.chosen_id))
			return item;
	return 0;
}

/* the party read again (twice a second, or now), and said as it changes */
static void h3_party_refresh(boolean now)
{
	unsigned long time = system_milliseconds();
	char line[400], status[96];
	int index;

	if (!now && h3_party.refreshed_time && time - h3_party.refreshed_time < 500)
		return;
	h3_party.refreshed_time = time ? time : 1;
	h3_party.member_count = p2p_party_members(h3_party.members, P2P_PARTY_MAXIMUM);
	h3_party.invite_count = p2p_party_invites(h3_party.invites, P2P_FRIEND_INVITES_MAXIMUM);
	p2p_party_status(status, sizeof(status));
	snprintf(line, sizeof(line), "%s", status);
	for (index = 0; index < h3_party.member_count; index++)
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s%s%s%s%s", index ? ", " : "; members: ",
			h3_party.members[index].name, h3_party.members[index].leader ? " (leader)" : "",
			h3_party.members[index].self ? " (you)" : "", h3_party.members[index].note[0] ? " - " : "",
			h3_party.members[index].note);
	for (index = 0; index < h3_party.invite_count; index++)
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s", index ? ", " : "; invites: ",
			h3_party.invites[index].name);
	if (strcmp(line, h3_party.said))
	{
		snprintf(h3_party.said, sizeof(h3_party.said), "%s", line);
		platform_log("menus: h3 party: %s", line);
	}
}

static void h3_party_initialize(void)
{
	browser_view_reset(&h3_party.view);
	h3_party.page = 0;
	h3_party.message[0] = 0;
	h3_party.chosen_id[0] = 0;
	h3_party.said[0] = 0;
	h3_party_refresh(TRUE);
}

/* (port h3 party) the rows, the chosen one's facts */
static void h3_party_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focused = list->focused_child, *focus = NULL;
	struct p2p_party_member const *member;
	struct p2p_party_invite const *invite;
	long index, focused_row = NONE, target;
	char text[256];
	int chosen;

	{
		char typed[HALO_TEXT_INPUT_SIZE];

		if (typing_done(_typing_party_chat, typed, sizeof(typed)) && typed[0])
		{
			int outcome = p2p_party_chat_send(typed);

			platform_log("menus: h3 party: a message typed: %s", p2p_party_outcome_text(outcome));
			h3_party_chat_outcome(outcome);
		}
	}
	h3_party_refresh(FALSE);
	if (list != h3_party.list || list->creation_time != h3_party.list_creation_time)
	{
		h3_party.list = list;
		h3_party.list_creation_time = list->creation_time;
		browser_view_reset(&h3_party.view);
		focused = NULL;
	}
	for (index = 0, child = list->child; child; child = child->next)
	{
		if (strncmp(child->name, "row_", 4))
			continue;
		if (child == focused)
			focused_row = index;
		index++;
	}
	target = browser_view_follow(&h3_party.view, H3_PARTY_ROWS, h3_party_items(), (int)focused_row,
		h3_party_chosen_index(), h3_party.page);
	h3_party.page = 0;
	for (index = 0, child = list->child; child; child = child->next)
	{
		long item = h3_party.view.offset + index;

		if (strncmp(child->name, "row_", 4))
			continue;
		if (index >= h3_party.view.visible)
		{
			widget_instance_set_visibility_recursive(child, FALSE);
			child->disabled = TRUE;
			index++;
			continue;
		}
		widget_instance_set_visibility_recursive(child, TRUE);
		child->disabled = FALSE;
		invite = h3_party_item_invite((int)item);
		member = h3_party_item_member((int)item);
		if (invite)
		{
			snprintf(text, sizeof(text), T("Invited you: %d of %d in their party"), invite->size, P2P_PARTY_MAXIMUM);
			row_column(child, "row_name", invite->name);
			row_column(child, "row_state", text);
			row_column(child, "row_tag", T("INVITE"));
		}
		else if (member)
		{
			char name[P2P_PARTY_NAME_SIZE + 8];

			/* (a member on this project's PC build: "(PC)" after the name) */
			snprintf(name, sizeof(name), member->pc ? "%s (PC)" : "%s", member->name);
			row_column(child, "row_name", name);
			row_column(child, "row_state", member->note[0] ? member->note : member->self ? T("You") :
				member->online ? T("Online") : T("Not heard from"));
			row_column(child, "row_tag", member->leader ? T("LEADER") : member->muted ? T("MUTED") : " ");
		}
		else
		{
			row_column(child, "row_name", T("LEAVE PARTY"));
			row_column(child, "row_state", T("You stay where you are; the party goes on"));
			row_column(child, "row_tag", " ");
		}
		if (index == h3_party.view.row)
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
	if (target != NONE)
		snprintf(h3_party.chosen_id, sizeof(h3_party.chosen_id), "%s", h3_party_item_id((int)target));
	chosen = h3_party_chosen_index();
	invite = h3_party_item_invite(chosen);
	member = h3_party_item_member(chosen);
	p2p_party_status(text, sizeof(text));
	text_show(named(screen, "party_status"), text);
	if (invite)
		snprintf(text, sizeof(text), "%s\n%s", invite->name, h3_buttons(T("Cross joins their party: you'll follow them from game to game.")));
	else if (member)
		snprintf(text, sizeof(text), "%s   %s\n%s", member->name, member->code, member->self ? T("You") :
			member->is_friend ? T("Your friend") : T("Not on your friends list: shown with their code"));
	else if (h3_party_items())
		snprintf(text, sizeof(text), "%s", T("Leave the party: the others stay together."));
	else
		snprintf(text, sizeof(text), "%s", h3_buttons(T("No party yet: on FRIENDS, Triangle on a friend, INVITE TO PARTY.")));
	text_show(named(screen, "detail"), text);
	text_show(named(screen, "party_message"), h3_party.message[0] &&
		system_milliseconds() - h3_party.message_time < H3_PARTY_MESSAGE_MILLISECONDS ? h3_party.message : "");
	text_show(named(screen, "a_label"), invite ? T("Join party") : member ? T("Options") : h3_party_items() ? T("Leave") : "");
	text_show(named(screen, "x_label"), invite ? T("No") : h3_party.member_count > 1 ? T("Quick chat") : "");
	if (named(screen, "a_butn"))
		widget_instance_set_visibility_recursive(named(screen, "a_butn"), h3_party_items() != 0);
	if (named(screen, "x_butn"))
		widget_instance_set_visibility_recursive(named(screen, "x_butn"), invite != NULL || h3_party.member_count > 1);
	if (named(screen, "y_butn"))
		widget_instance_set_visibility_recursive(named(screen, "y_butn"), h3_party.member_count > 1);
	if (named(screen, "y_label"))
		widget_instance_set_visibility_recursive(named(screen, "y_label"), h3_party.member_count > 1);
}

/* (port h3 party act: a row's Cross) TRUE: the member's options open */
static boolean h3_party_act(struct widget_instance *widget)
{
	struct p2p_party_member const *member;
	struct p2p_party_invite const *invite;
	int item = h3_party_chosen_index(), outcome;

	if (widget && !strncmp(widget->name, "row_", 4) && widget->name[4] >= '0' && widget->name[4] <= '9')
	{
		long row = atol(widget->name + 4) - 1;

		if (row >= 0 && row < h3_party.view.visible)
			item = (int)(h3_party.view.offset + row);
	}
	snprintf(h3_party.chosen_id, sizeof(h3_party.chosen_id), "%s", h3_party_item_id(item));
	invite = h3_party_item_invite(item);
	member = h3_party_item_member(item);
	if (invite)
	{
		struct p2p_party_invite copy = *invite;

		outcome = p2p_party_answer(copy.id, 1);
		h3_party_message(outcome == P2P_PARTY_DONE ? T("Joining the party...") : p2p_party_outcome_text(outcome));
		h3_party_refresh(TRUE);
		return FALSE;
	}
	if (!member)
	{
		if (!h3_party_items())
			return FALSE;
		outcome = p2p_party_leave();
		h3_party_message(outcome == P2P_PARTY_DONE ? T("You left the party") : p2p_party_outcome_text(outcome));
		h3_party_refresh(TRUE);
		return FALSE;
	}
	h3_party.options_member = *member;
	h3_party.option_count = 0;
	if (member->self)
		h3_party.options[h3_party.option_count++] = _h3_party_option_leave;
	else
	{
		h3_party.options[h3_party.option_count++] = member->muted ? _h3_party_option_unmute : _h3_party_option_mute;
		if (p2p_party_leading())
			h3_party.options[h3_party.option_count++] = _h3_party_option_kick;
	}
	platform_log("menus: h3 party: options for %s", member->name);
	return TRUE;
}

/* (port h3 party second: Square) an invite said no to; else QUICK CHAT
opens (TRUE) in a party */
static boolean h3_party_second(void)
{
	struct p2p_party_invite const *invite = h3_party_item_invite(h3_party_chosen_index());

	if (invite)
	{
		struct p2p_party_invite copy = *invite;
		int outcome = p2p_party_answer(copy.id, 0);

		h3_party_message(outcome == P2P_PARTY_DONE ? T("Party invite declined") : p2p_party_outcome_text(outcome));
		h3_party_refresh(TRUE);
		return FALSE;
	}
	if (h3_party.member_count < 2)
	{
		h3_party_message(p2p_party_outcome_text(P2P_PARTY_NOT_IN_PARTY));
		return FALSE;
	}
	platform_log("menus: h3 party: QUICK CHAT");
	return TRUE;
}

/* (port h3 party type: Triangle) a message typed (the system's keyboard on
the Vita), sent once typed (h3_party_update) */
static void h3_party_type(void)
{
	if (h3_party.member_count < 2)
	{
		h3_party_message(p2p_party_outcome_text(P2P_PARTY_NOT_IN_PARTY));
		return;
	}
	typing_begin(_typing_party_chat, T("Party chat"), "", P2P_PARTY_CHAT_LENGTH, FALSE);
}

/* a chat's outcome said (sent: nothing; the line shows) */
static void h3_party_chat_outcome(int outcome)
{
	if (outcome != P2P_PARTY_DONE)
		h3_party_message(p2p_party_outcome_text(outcome));
}

/* (port h3 party quick) QUICK CHAT's rows: the presets in the language
chosen */
static void h3_party_quick_update(struct widget_instance *list)
{
	struct widget_instance *child;

	for (child = list->child; child; child = child->next)
	{
		long preset;

		if (strncmp(child->name, "preset_", 7) || !(child->name[7] >= '1' && child->name[7] <= '8'))
			continue;
		preset = child->name[7] - '1';
		text_show(child, p2p_party_chat_preset_text((int)preset));
	}
}

/* (port h3 party preset: a row of QUICK CHAT) sent */
static void h3_party_preset(struct widget_instance *widget)
{
	long preset = widget && !strncmp(widget->name, "preset_", 7) ? widget->name[7] - '1' : -1;
	int outcome;

	if (preset < 0 || preset >= P2P_PARTY_PRESETS)
		return;
	outcome = p2p_party_chat_preset((int)preset);
	platform_log("menus: h3 party: quick chat \"%s\": %s", p2p_party_chat_preset_text((int)preset),
		p2p_party_outcome_text(outcome));
	h3_party_chat_outcome(outcome);
}

/* (port h3 party chat) the chat's newest lines, oldest first */
static void h3_party_chat_update(struct widget_instance *panel)
{
	struct p2p_party_chat_line lines[P2P_PARTY_CHAT_LINES];
	int count = p2p_party_chat_lines(lines, P2P_PARTY_CHAT_LINES, 0), first = count > 5 ? count - 5 : 0, row;

	for (row = 0; row < 5; row++)
	{
		char name[16], text[P2P_PARTY_NAME_SIZE + P2P_PARTY_CHAT_SIZE + 4];

		snprintf(name, sizeof(name), "chat_%d", row + 1);
		text[0] = 0;
		if (first + row < count)
		{
			struct p2p_party_chat_line const *line = &lines[first + row];

			if (line->notice)
				snprintf(text, sizeof(text), "%s", line->text);
			else
				snprintf(text, sizeof(text), "%s: %s", line->name, line->text);
		}
		row_column(panel, name, text[0] ? text : " ");
	}
}

static char const *h3_party_option_name(int option)
{
	switch (option)
	{
	case _h3_party_option_mute:
		return T("MUTE");
	case _h3_party_option_unmute:
		return T("UNMUTE");
	case _h3_party_option_kick:
		return T("REMOVE FROM PARTY");
	case _h3_party_option_leave:
		return T("LEAVE PARTY");
	}
	return "";
}

/* (port h3 party options) the question's words and rows */
static void h3_party_options_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child;
	char text[192];

	text_show(named(screen, "options_title"), h3_party.options_member.name);
	snprintf(text, sizeof(text), "%s\n%s", h3_party.options_member.code, h3_party.options_member.self ?
		T("Leave the party: the others stay together.") : T("Mute hides their chat here; they're never told."));
	text_show(named(screen, "options_text"), text);
	for (child = list->child; child; child = child->next)
	{
		long option;

		if (strncmp(child->name, "option_", 7) || !(child->name[7] >= '1' && child->name[7] <= '2'))
			continue;
		option = child->name[7] - '1';
		if (option < h3_party.option_count)
		{
			widget_instance_set_visibility_recursive(child, TRUE);
			child->disabled = FALSE;
			text_show(child, h3_party_option_name(h3_party.options[option]));
		}
		else
		{
			widget_instance_set_visibility_recursive(child, FALSE);
			child->disabled = TRUE;
		}
	}
}

/* (port h3 party option: a row of the question) */
static void h3_party_option(struct widget_instance *widget)
{
	long option = widget && !strncmp(widget->name, "option_", 7) ? widget->name[7] - '1' : -1;
	int outcome = P2P_PARTY_GONE;
	char text[160];

	if (option < 0 || option >= h3_party.option_count)
		return;
	switch (h3_party.options[option])
	{
	case _h3_party_option_mute:
	case _h3_party_option_unmute:
		outcome = p2p_party_mute(h3_party.options_member.id, h3_party.options[option] == _h3_party_option_mute);
		snprintf(text, sizeof(text), outcome != P2P_PARTY_DONE ? "%s" : h3_party.options[option] == _h3_party_option_mute ?
			T("%s muted") : T("%s unmuted"), outcome != P2P_PARTY_DONE ? p2p_party_outcome_text(outcome) :
			h3_party.options_member.name);
		break;
	case _h3_party_option_kick:
		outcome = p2p_party_kick(h3_party.options_member.id);
		snprintf(text, sizeof(text), outcome == P2P_PARTY_DONE ? T("Removed %s from the party") : "%s",
			outcome == P2P_PARTY_DONE ? h3_party.options_member.name : p2p_party_outcome_text(outcome));
		break;
	default:
		outcome = p2p_party_leave();
		snprintf(text, sizeof(text), "%s", outcome == P2P_PARTY_DONE ? T("You left the party") :
			p2p_party_outcome_text(outcome));
		break;
	}
	h3_party_message(text);
	h3_party_refresh(TRUE);
}

#define H3_PARTY_SCREEN "h3/party/screen"

/* the PARTY screen opened over the screen open (FRIENDS: B goes back to it) */
static void h3_party_open(void)
{
	long screen = menu_tags_ingame_screen(H3_PARTY_SCREEN);
	struct widget_instance *active = ui_widget_port_active_screen(0);

	if (screen == NONE || !active || !ui_widget_load_by_name_or_tag(NULL, screen, NULL, active->local_player_index,
		active->definition_tag_index, NONE, NONE))
	{
		platform_log("menus: the PARTY screen did not open");
		return;
	}
	platform_log("menus: the PARTY screen opened");
}


/* ---- the Halo 3 PLAYERS screen (halo3/players.xml): the players of the
network game this machine plays, the cursor on one, Cross for its options
(ADD FRIEND, BLOCK: p2p_friends_player_ask, the two machines' friend cards
exchanged over internet play first). The players as the game's record has
them (network_game_client_get_game): each name, team and machine; this
machine's own marked, no options */

#define H3_PLAYERS_ROWS 10
#define H3_PLAYERS_MESSAGE_MILLISECONDS 6000

struct h3_player
{
	char name[24];
	int machine;
	int team;
};

static struct
{
	struct browser_view view;
	short page;
	struct h3_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS + 8];
	int count;
	int own_machine;
	boolean teams;
	/* the player chosen, by its machine and name */
	int chosen_machine;
	char chosen_name[24];
	char message[160];
	unsigned long message_time;
	struct widget_instance *list;
	long list_creation_time;
} h3_players;

/* the game's players (and, the tests', made-up ones: HALO_TEST_PLAYERS_FAKE) */
static void h3_players_read(void)
{
	struct network_game_client *client = global_network_game_client_get();
	int index;

	h3_players.count = 0;
	h3_players.own_machine = -1;
	h3_players.teams = FALSE;
	if (client && !network_game_is_splitscreen_local())
	{
		struct network_game *game = network_game_client_get_game(client);

		h3_players.own_machine = network_game_client_get_machine_index(client);
		h3_players.teams = game->variant.universal_variant.teams != 0;
		for (index = 0; index < game->player_count && index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
		{
			struct network_player *player = &game->players[index];
			struct h3_player *entry = &h3_players.players[h3_players.count];
			int character;

			if (!network_player_is_valid(player))
				continue;
			for (character = 0; character < 11 && player->name[character]; character++)
				entry->name[character] = player_name_character_ascii(player->name[character]);
			entry->name[character] = 0;
			entry->machine = player->machine_index;
			entry->team = player->team_index;
			h3_players.count++;
		}
	}
#ifndef HALO_RELEASE
	{
		char const *fake = getenv("HALO_TEST_PLAYERS_FAKE");
		int count = fake ? atoi(fake) : 0;

		for (index = 0; index < count && index < 8; index++)
		{
			struct h3_player *entry = &h3_players.players[h3_players.count++];

			snprintf(entry->name, sizeof(entry->name), "Spartan %d", index + 1);
			entry->machine = 10 + index;
			entry->team = index & 1;
		}
		if (count && h3_players.own_machine < 0)
			h3_players.own_machine = 0;
	}
#endif
}

static void h3_players_message(char const *text)
{
	snprintf(h3_players.message, sizeof(h3_players.message), "%s", text);
	h3_players.message_time = system_milliseconds();
	platform_log("menus: h3 players: %s", text);
}

static char const *h3_players_state_text(int state)
{
	switch (state)
	{
	case P2P_FRIENDS_PLAYER_THIS_VITA:
		return T("This PS Vita");
	case P2P_FRIENDS_PLAYER_ASKING:
		return T("Asking...");
	case P2P_FRIENDS_PLAYER_NOT_TAKING:
		return T("Not taking friend requests");
	case P2P_FRIENDS_PLAYER_FRIEND:
		return T("Friend");
	case P2P_FRIENDS_PLAYER_REQUEST_SENT:
		return T("Request sent");
	case P2P_FRIENDS_PLAYER_ASKED_YOU:
		return T("Wants to be your friend");
	case P2P_FRIENDS_PLAYER_BLOCKED:
		return T("Blocked");
	}
	return "";
}

static int h3_players_state(struct h3_player const *player)
{
	return player->machine == h3_players.own_machine ? P2P_FRIENDS_PLAYER_THIS_VITA :
		p2p_friends_player_state(player->machine);
}

static void h3_players_initialize(void)
{
	browser_view_reset(&h3_players.view);
	h3_players.page = 0;
	/* (the player chosen kept: back from its options, the cursor is on it
	again; menu_functions_players_open forgets it) */
	h3_players_read();
	platform_log("menus: h3 players: %d players, this machine %d", h3_players.count, h3_players.own_machine);
	{
		int index;

		for (index = 0; index < h3_players.count; index++)
			platform_log("menus: h3 players: %s (machine %d)%s", h3_players.players[index].name,
				h3_players.players[index].machine, h3_players.players[index].machine == h3_players.own_machine ?
				" [this PS Vita]" : "");
	}
}

static int h3_players_chosen_index(void)
{
	int index;

	for (index = 0; index < h3_players.count; index++)
		if (h3_players.players[index].machine == h3_players.chosen_machine &&
			!strcmp(h3_players.players[index].name, h3_players.chosen_name))
			return index;
	return -1;
}

static char const *h3_players_team_name(int team)
{
	static char const *const names[] = { N_("Red"), N_("Blue"), N_("Green"), N_("Yellow"), N_("Purple"),
		N_("Orange"), N_("Brown"), N_("Pink") };

	return team >= 0 && team < (int)NUMBEROF(names) ? T(names[team]) : "-";
}

/* whether this machine hosts the network game the PLAYERS screen shows (its
KICK and BAN) */
static boolean h3_players_hosting(void)
{
	return global_network_game_server_get() != NULL && !network_game_is_splitscreen_local();
}

/* (port h3 players) the rows, the chosen one's facts */
static void h3_players_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focused = list->focused_child, *focus = NULL;
	long index, focused_row = NONE, target;
	char text[192], status[96];

	h3_players_read();
	if (list != h3_players.list || list->creation_time != h3_players.list_creation_time)
	{
		h3_players.list = list;
		h3_players.list_creation_time = list->creation_time;
		browser_view_reset(&h3_players.view);
		focused = NULL;
	}
	for (index = 0, child = list->child; child; child = child->next)
	{
		if (strncmp(child->name, "row_", 4))
			continue;
		if (child == focused)
			focused_row = index;
		index++;
	}
	target = browser_view_follow(&h3_players.view, H3_PLAYERS_ROWS, h3_players.count, (int)focused_row,
		h3_players_chosen_index(), h3_players.page);
	h3_players.page = 0;
	for (index = 0, child = list->child; child; child = child->next)
	{
		struct h3_player const *player;

		if (strncmp(child->name, "row_", 4))
			continue;
		if (index >= h3_players.view.visible)
		{
			widget_instance_set_visibility_recursive(child, FALSE);
			child->disabled = TRUE;
			index++;
			continue;
		}
		player = &h3_players.players[h3_players.view.offset + index];
		widget_instance_set_visibility_recursive(child, TRUE);
		child->disabled = FALSE;
		row_column(child, "row_name", player->name);
		row_column(child, "row_team", h3_players.teams ? h3_players_team_name(player->team) : "-");
		row_column(child, "row_state", h3_players_state_text(h3_players_state(player))[0] ?
			h3_players_state_text(h3_players_state(player)) : " ");
		if (index == h3_players.view.row)
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
	if (target != NONE)
	{
		h3_players.chosen_machine = h3_players.players[target].machine;
		snprintf(h3_players.chosen_name, sizeof(h3_players.chosen_name), "%s", h3_players.players[target].name);
	}
	index = h3_players_chosen_index();
	if (index >= 0)
	{
		struct h3_player const *player = &h3_players.players[index];

		if (player->machine == h3_players.own_machine)
			snprintf(text, sizeof(text), "%s\n%s", player->name, T("A player of this PS Vita"));
		else
			snprintf(text, sizeof(text), "%s\n%s", player->name, h3_buttons(h3_players_hosting() ?
				T("Cross: add them as a friend, block, kick or ban them") : T("Cross: add them as a friend or block them")));
		text_show(named(screen, "detail"), text);
	}
	else
		text_show(named(screen, "detail"), h3_players.count ? "" : T("No network game: players show here in one"));
	text_show(named(screen, "players_message"), h3_players.message[0] &&
		system_milliseconds() - h3_players.message_time < H3_PLAYERS_MESSAGE_MILLISECONDS ? h3_players.message : "");
	p2p_friends_status(status, sizeof(status));
	snprintf(text, sizeof(text), "%d %s - %s", h3_players.count, h3_players.count == 1 ? T("player") : T("players"),
		status);
	text_show(named(screen, "players_status"), text);
}

/* (port h3 players act: a row's Cross) the options open for another
machine's player */
static boolean h3_players_act(struct widget_instance *widget)
{
	int index;

	if (widget && !strncmp(widget->name, "row_", 4) && widget->name[4] >= '0' && widget->name[4] <= '9')
	{
		long row = atol(widget->name + 4) - 1;

		if (row >= 0 && row < h3_players.view.visible && h3_players.view.offset + row < h3_players.count)
		{
			struct h3_player const *player = &h3_players.players[h3_players.view.offset + row];

			h3_players.chosen_machine = player->machine;
			snprintf(h3_players.chosen_name, sizeof(h3_players.chosen_name), "%s", player->name);
		}
	}
	index = h3_players_chosen_index();
	if (index < 0)
		return FALSE;
	if (h3_players.players[index].machine == h3_players.own_machine)
	{
		h3_players_message(T("A player of this PS Vita"));
		return FALSE;
	}
	platform_log("menus: h3 players: options for %s (machine %d)", h3_players.players[index].name,
		h3_players.players[index].machine);
	return TRUE;
}

/* (the host's KICK and BAN) the chosen player's machine dropped:
network_anticheat.c tells every machine and logs it; a ban keeps it out of
this host's games while it runs */
static void h3_players_drop(short how)
{
	int index = h3_players_chosen_index();
	char text[160];

	if (index < 0)
		return;
	if (!h3_players_hosting())
		snprintf(text, sizeof(text), "%s", T("Only the game's host can do that"));
	else if (h3_players.players[index].machine == h3_players.own_machine ||
		!network_anticheat_drop(h3_players.players[index].machine, how, NULL))
	{
		snprintf(text, sizeof(text), "%s", T("That player is not in the game"));
	}
	else
	{
		snprintf(text, sizeof(text), how == _anticheat_drop_kick ? T("Kicked %s") : T("Banned %s from your games"),
			h3_players.players[index].name);
	}
	h3_players_message(text);
}

/* (port h3 players options) the question's words */
static void h3_players_options_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	int index = h3_players_chosen_index();
	char text[160];

	if (index < 0)
		return;
	text_show(named(screen, "options_title"), h3_players.players[index].name);
	snprintf(text, sizeof(text), "%s", h3_players_state_text(h3_players_state(&h3_players.players[index])));
	text_show(named(screen, "options_text"), text[0] ? text : T("Add friend sends a friend request: they accept or decline."));
	/* (INVITE TO PARTY: another machine's player not blocked; their card
	asked first if not had, the invite only to a friend's) */
	{
		struct widget_instance *child;
		int state = h3_players_state(&h3_players.players[index]);
		boolean party = state != P2P_FRIENDS_PLAYER_THIS_VITA && state != P2P_FRIENDS_PLAYER_BLOCKED &&
			state != P2P_FRIENDS_PLAYER_NOT_TAKING;

		/* (KICK and BAN: the game's host's alone, of another machine's
		player) */
		boolean host = h3_players_hosting() && state != P2P_FRIENDS_PLAYER_THIS_VITA;

		for (child = list->child; child; child = child->next)
		{
			if (strstr(child->name, "option_party"))
			{
				widget_instance_set_visibility_recursive(child, party);
				child->disabled = !party;
			}
			if (strstr(child->name, "option_kick") || strstr(child->name, "option_ban"))
			{
				widget_instance_set_visibility_recursive(child, host);
				child->disabled = !host;
			}
		}
	}
}

/* an option for the chosen player (the tests' steps call it too) */
static void h3_players_do(int intent)
{
	int index = h3_players_chosen_index(), outcome;
	char text[160];

	if (index < 0)
		return;
	outcome = p2p_friends_player_ask(h3_players.players[index].machine, intent);
	if (outcome == P2P_FRIENDS_DONE)
		snprintf(text, sizeof(text), intent == P2P_FRIENDS_PLAYER_BLOCK ? T("Blocking %s...") :
			T("Asking %s to be friends..."), h3_players.players[index].name);
	else
		snprintf(text, sizeof(text), "%s", p2p_friends_outcome_text(outcome));
	h3_players_message(text);
}

/* (port h3 players option: a row of the question) */
static void h3_players_option(struct widget_instance *widget)
{
	if (!widget)
		return;
	if (!strcmp(widget->name, "option_add") || strstr(widget->name, "option_add"))
		h3_players_do(P2P_FRIENDS_PLAYER_ADD);
	else if (strstr(widget->name, "option_block"))
		h3_players_do(P2P_FRIENDS_PLAYER_BLOCK);
	else if (strstr(widget->name, "option_kick"))
		h3_players_drop(_anticheat_drop_kick);
	else if (strstr(widget->name, "option_ban"))
		h3_players_drop(_anticheat_drop_session_ban);
	else if (strstr(widget->name, "option_party"))
	{
		int index = h3_players_chosen_index(), outcome;
		char id[P2P_FRIEND_ID_SIZE], text[160];

		if (index < 0)
			return;
		/* (their card had, a friend's: invited now; else asked for first) */
		if (p2p_friends_player_friend_id(h3_players.players[index].machine, id))
		{
			outcome = p2p_party_invite(id);
			snprintf(text, sizeof(text), outcome == P2P_PARTY_DONE ? T("Invited %s to your party") : "%s",
				outcome == P2P_PARTY_DONE ? h3_players.players[index].name : p2p_party_outcome_text(outcome));
		}
		else
		{
			outcome = p2p_friends_player_ask(h3_players.players[index].machine, P2P_FRIENDS_PLAYER_PARTY);
			snprintf(text, sizeof(text), outcome == P2P_FRIENDS_DONE ? T("Inviting %s to your party...") : "%s",
				outcome == P2P_FRIENDS_DONE ? h3_players.players[index].name : p2p_friends_outcome_text(outcome));
		}
		h3_players_message(text);
	}
}

#define H3_PLAYERS_SCREEN "h3/players/screen"
#define H3_PLAYERS_PREGAME_SCREEN "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen"

/* the PLAYERS screen opened: over the game's lobby (B goes back to it), or in
a level (the scoreboard, the carnage report); 1 if it opened */
int menu_functions_players_open(boolean main_menu_loaded)
{
	long screen = menu_tags_ingame_screen(H3_PLAYERS_SCREEN);
	struct widget_instance *active = ui_widget_port_active_screen(0);

	if (screen == NONE)
		return 0;
	h3_players.chosen_machine = -1;
	h3_players.message[0] = 0;
	if (main_menu_loaded)
	{
		if (!active || !ui_widget_load_by_name_or_tag(NULL, screen, NULL, active->local_player_index,
			active->definition_tag_index, NONE, NONE))
		{
			return 0;
		}
	}
	else if (active || !ui_widget_load_by_name_or_tag(NULL, screen, NULL, 0, NONE, NONE, NONE))
		return 0;
	platform_log("menus: the PLAYERS screen opened (%s)", main_menu_loaded ? "the game's lobby" :
		game_engine_showing_postgame() ? "the carnage report" : "the scoreboard");
	return 1;
}

/* Triangle in the game's lobby, on the carnage report, or with the
scoreboard up (Back held): the PLAYERS screen */
void menu_functions_players_button(boolean main_menu_loaded)
{
	struct network_game_client *client = global_network_game_client_get();
	const struct gamepad_state *pad;
	struct widget_instance *active;

	if (!client || network_game_is_splitscreen_local() || !input_has_gamepad(0))
		return;
	pad = input_get_gamepad_state(0);
	if (!pad || pad->buttons[_gamepad_analog_button_y] != 1)
		return;
	active = ui_widget_port_active_screen(0);
	if (main_menu_loaded)
	{
		long pregame = tag_loaded('DeLa', H3_PLAYERS_PREGAME_SCREEN);

		/* (or the Halo 3 lobby that stands for it, halo3/pregame.xml) */
		long h3_pregame_tag = menu_tags_ingame_screen("h3/pregame/screen");

		if (active && ((pregame != NONE && active->definition_tag_index == pregame) ||
			(h3_pregame_tag != NONE && active->definition_tag_index == h3_pregame_tag)))
			menu_functions_players_open(TRUE);
	}
	else if (!active && (game_engine_showing_postgame() || pad->buttons[_gamepad_binary_button_back]))
		menu_functions_players_open(FALSE);
}

/* (friends_game.c: the tests' steps) the first other machine's player
chosen and added, as its options' ADD FRIEND does: 1 if there was one */
int menu_functions_players_add_first(void)
{
	int index;

	h3_players_read();
	for (index = 0; index < h3_players.count; index++)
		if (h3_players.players[index].machine != h3_players.own_machine)
		{
			h3_players.chosen_machine = h3_players.players[index].machine;
			snprintf(h3_players.chosen_name, sizeof(h3_players.chosen_name), "%s", h3_players.players[index].name);
			platform_log("menus: h3 players: options for %s (machine %d)", h3_players.players[index].name,
				h3_players.players[index].machine);
			h3_players_do(P2P_FRIENDS_PLAYER_ADD);
			return 1;
		}
	return 0;
}

/* ---------- public code */

/* (friends_game.c: a friend's game joined, its invite handed on) the System
Link screen at the next frame, as JOIN WITH CODE opens it, where the host's
game is joined once listed */
void menu_functions_open_system_link(void)
{
	menu_functions.open_system_link = TRUE;
}

/* (map_share.c: a join refused - the host's map not sent, the game under
way -, the message shown) Menu style Halo 3: back to the games browser, with
the message, rather than at the main menu */
void menu_functions_join_refused(void)
{
	if (!halo_menu_style_halo3())
		return;
	menu_functions.browser_after_refusal = system_milliseconds() | 1;
}

/* (system_link_shortcut.c: the host an invite, a code or the online list
joined, once its game is listed) the game joined from the Halo 3 games
browser when it is the screen open, as A on its row joins it: 1 joined, 0
listed but not joined (the player told why), -1 the browser is not open */
int menu_functions_lan_join_listed(void const *game)
{
	struct widget_instance *screen = ui_widget_port_active_screen(0);
	struct widget_instance *list;
	boolean deleted = FALSE;

	if (!menu_functions.lan || !screen || !game || !(list = named(screen, "list")) ||
		list->type != _ui_widget_type_column_list || !list->focused_child)
	{
		return -1;
	}
	platform_log("menus: the games browser joins the joined host's game");
	return ui_widget_port_join_advertised_game(list, game, &deleted) ? 1 : 0;
}

/* ---- port h3 join: the Halo 3 SELECT PROFILE of a multiplayer game
(halo3/join.xml, join_split.xml), standing for the Xbox's four-way "press
START to join" screens and doing what they did. Each plate is a
controller's (the XML's child controller=): its first Cross (or START)
joins that controller's player to the game, as the Xbox's quarter's "join
controller to mp game"; left and right then choose its profile from those
the Xbox's list offers that controller ("player profile list initialize":
the player's own and the default ones, a profile another player has
marked), Cross sets it as the Xbox's "1wide plyr prof set for game" did
(with its checks), and the plate is ready; Cross on a ready plate, once
every player in is ready, goes on to the screen the Xbox's went on to (the
XML's open=: the games list or the new game's map list, which the flows
replace as before). Circle un-readies a plate, then leaves it; on an empty
plate it goes back. */
#define H3_JOIN_PLATES 4
#define H3_JOIN_PROFILES 100

enum
{
	_h3_join_out,
	_h3_join_choosing,
	_h3_join_ready,
};

static struct
{
	boolean split;
	short state[H3_JOIN_PLATES];
	long profiles[H3_JOIN_PLATES][H3_JOIN_PROFILES];
	word count[H3_JOIN_PLATES];
	short cursor[H3_JOIN_PLATES];
	char said[H3_JOIN_PLATES][128];
	char next_said[160];
} h3_join;

/* a plate's controller: its own (controller=), else the event's */
static short h3_join_plate(struct widget_instance *widget, struct event_record *event)
{
	short controller = widget && widget->local_player_index != NONE ? widget->local_player_index :
		event ? event->controller_index : NONE;

	return controller >= 0 && controller < H3_JOIN_PLATES ? controller : NONE;
}

/* the controller's profiles, read again (the in-use marks change as the
other plates choose), the cursor kept on the profile it was on */
static void h3_join_profiles_read(short plate)
{
	long on = h3_join.count[plate] && h3_join.cursor[plate] < (short)h3_join.count[plate] ?
		h3_join.profiles[plate][h3_join.cursor[plate]] & 0x7FFFFFFF : NONE;
	word count = H3_JOIN_PROFILES;
	short index;

	player_profiles_enumerate_available_to_local_player_index(plate, &count, h3_join.profiles[plate], TRUE);
	h3_join.count[plate] = count > H3_JOIN_PROFILES ? H3_JOIN_PROFILES : count;
	h3_join.cursor[plate] = 0;
	if (on == NONE)
	{
		/* (first: the profile this controller has, else player 1's last) */
		long active = player_ui_get_active_player_profile_index(plate);

		on = active != NONE ? active : plate == 0 ? player_ui_get_player1_last_used_profile_index() : NONE;
	}
	for (index = 0; on != NONE && index < (short)h3_join.count[plate]; index++)
	{
		if ((h3_join.profiles[plate][index] & 0x7FFFFFFF) == (on & 0x7FFFFFFF))
		{
			h3_join.cursor[plate] = index;
			break;
		}
	}
}

/* a profile's name as the Xbox's list shows it: a default profile's, its
name from ui.map's default_player_profile_names */
static void h3_join_profile_name(struct player_profile const *profile, char *name, int size)
{
	if (TEST_FLAG(profile->flags, _player_profile_default_profile_bit))
	{
		long names_tag_index = tag_loaded(UNICODE_STRING_LIST_TAG, "ui\\shell\\strings\\default_player_profile_names");
		wchar_t const *default_name = names_tag_index != NONE ?
			unicode_string_list_get_string(names_tag_index, (short)(profile->flags >> 8)) : NULL;
		wchar_t copy[24];
		long length;

		for (length = 0; default_name && default_name[length] && length < (long)NUMBEROF(copy) - 1; length++)
			copy[length] = default_name[length];
		copy[length] = 0;
		h3_utf8(name, size, copy);
		if (!name[0])
			snprintf(name, (size_t)size, "%s", T("Default profile"));
		return;
	}
	h3_profile_utf8(name, size, profile);
}

static void h3_join_initialize(boolean split)
{
	csmemset(&h3_join, 0, sizeof(h3_join));
	h3_join.split = split;
	platform_log("menus: h3 join: SELECT PROFILE for %s", split ? "split screen" : "a network game");
}

/* (Cross or START on a plate) TRUE when the screen goes on */
static boolean h3_join_a(struct widget_instance *widget, struct event_record *event)
{
	short plate = h3_join_plate(widget, event);
	short other;

	if (plate == NONE)
		return FALSE;
	switch (h3_join.state[plate])
	{
	case _h3_join_out:
		player_ui_local_player_joined_multiplayer_game(plate);
		h3_join.state[plate] = _h3_join_choosing;
		h3_join_profiles_read(plate);
		platform_log("menus: h3 join: player %d joins", plate + 1);
		return FALSE;
	case _h3_join_choosing:
	{
		long entry;
		struct player_profile profile;

		h3_join_profiles_read(plate);
		if (!h3_join.count[plate])
			return FALSE;
		entry = h3_join.profiles[plate][h3_join.cursor[plate]];
		/* (as the Xbox's: one another player has is refused) */
		if (!(entry & 0x80000000))
		{
			display_error_deferred(31, plate, TRUE, FALSE);
			ui_play_audio_feedback_sound(4);
			platform_log("menus: h3 join: player %d: that profile is another player's", plate + 1);
			return FALSE;
		}
		if (!player_profile_get(entry, &profile))
			return FALSE;
		/* (a name the host's ban command could not name: player_name_valid) */
		if (profile.player_name[0] && !player_name_valid(profile.player_name, NUMBEROF(profile.player_name)))
		{
			display_error_text_deferred(
				TW(L"Sorry, this profile's\r\nname can't be used in\r\nmultiplayer. Please\r\nrename the profile."), plate);
			ui_play_audio_feedback_sound(4);
			return FALSE;
		}
		player_ui_set_active_player_profile(plate, entry, &profile);
		h3_join.state[plate] = _h3_join_ready;
		{
			char name[64];

			h3_join_profile_name(&profile, name, (int)sizeof(name));
			platform_log("menus: h3 join: player %d is ready with the profile %s", plate + 1, name);
		}
		return FALSE;
	}
	default:
		for (other = 0; other < H3_JOIN_PLATES; other++)
		{
			if (h3_join.state[other] == _h3_join_choosing)
			{
				platform_log("menus: h3 join: player %d is still choosing", other + 1);
				return FALSE;
			}
		}
		platform_log("menus: h3 join: on to %s", h3_join.split ? "split screen's map list" : "the games");
		/* (one player alone, a network game's: not asked again this run) */
		if (!h3_join.split)
		{
			menu_functions.profile_shown = TRUE;
			short ready = 0, alone = NONE;

			for (other = 0; other < H3_JOIN_PLATES; other++)
			{
				if (h3_join.state[other] == _h3_join_ready)
				{
					ready++;
					alone = other;
				}
			}
			if (ready == 1)
				h3_session_profile_chosen(alone, player_ui_get_active_player_profile_index(alone));
			else
				h3_session_forget();
		}
		return TRUE;
	}
}

static void h3_join_step(struct widget_instance *widget, struct event_record *event, short step)
{
	short plate = h3_join_plate(widget, event);

	if (plate == NONE || h3_join.state[plate] != _h3_join_choosing)
		return;
	h3_join_profiles_read(plate);
	if (h3_join.count[plate])
		h3_join.cursor[plate] = (short)((h3_join.cursor[plate] + step + h3_join.count[plate]) % h3_join.count[plate]);
}

/* (Circle on a plate) TRUE when the screen goes back: a plate no one is in */
static boolean h3_join_leave(struct widget_instance *widget, struct event_record *event)
{
	short plate = h3_join_plate(widget, event);

	if (plate == NONE)
		return TRUE;
	switch (h3_join.state[plate])
	{
	case _h3_join_ready:
		h3_join.state[plate] = _h3_join_choosing;
		return FALSE;
	case _h3_join_choosing:
		player_ui_clear_multiplayer_autojoin_for_local_player(plate);
		h3_join.state[plate] = _h3_join_out;
		platform_log("menus: h3 join: player %d leaves", plate + 1);
		return FALSE;
	default:
		return TRUE;
	}
}

static void h3_join_update(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	short plate, in = 0, ready = 0;
	char text[160];

	for (plate = 0; plate < H3_JOIN_PLATES; plate++)
	{
		char slot_name[16];
		struct widget_instance *slot, *found;
		char name[64] = "";
		short color = NONE;
		short state = h3_join.state[plate];

		snprintf(slot_name, sizeof(slot_name), "slot_%d", plate + 1);
		if ((slot = named(screen, slot_name)) == NULL)
			continue;
		in += state != _h3_join_out;
		ready += state == _h3_join_ready;
		if (state != _h3_join_out && h3_join.count[plate])
		{
			struct player_profile profile;

			if (player_profile_get(h3_join.profiles[plate][h3_join.cursor[plate]], &profile))
			{
				h3_join_profile_name(&profile, name, (int)sizeof(name));
				color = profile.primary_color_index;
			}
			if (!name[0])
				snprintf(name, sizeof(name), "%s", T("Default profile"));
		}
		snprintf(text, sizeof(text), T("PLAYER %d"), plate + 1);
		text_show(named(slot, "slot_title"), text);
		text_show(named(slot, "slot_name"), state == _h3_join_out ? "" : name);
		if ((found = named(slot, "slot_swatch")) != NULL)
		{
			widget_instance_set_visibility_recursive(found, state != _h3_join_out);
			found->animation.current_frame_index = h3_swatch_frame(color);
		}
		if ((found = named(slot, "slot_left")) != NULL)
			widget_instance_set_visibility_recursive(found, state == _h3_join_choosing && h3_join.count[plate] > 1);
		if ((found = named(slot, "slot_right")) != NULL)
			widget_instance_set_visibility_recursive(found, state == _h3_join_choosing && h3_join.count[plate] > 1);
		text_show(named(slot, "slot_state"), state == _h3_join_out ? h3_buttons(T("Press Cross or START to join.")) :
			state == _h3_join_choosing ? h3_buttons(T("Left and right: your profile.\nCross: choose it.")) :
			h3_buttons(T("READY. Circle: change the profile.")));
		snprintf(text, sizeof(text), "%s|%s", state == _h3_join_out ? "out" : state == _h3_join_choosing ? "choosing" : "ready",
			name);
		if (strcmp(text, h3_join.said[plate]))
		{
			snprintf(h3_join.said[plate], sizeof(h3_join.said[plate]), "%s", text);
			platform_log("menus: h3 join: plate %d: %s%s%s", plate + 1, state == _h3_join_out ? "press to join" :
				state == _h3_join_choosing ? "choosing" : "ready", name[0] ? ", " : "", name);
		}
	}
	if (!in)
		snprintf(text, sizeof(text), "%s", h3_buttons(T("Press Cross to join with your profile.")));
	else if (ready < in)
		snprintf(text, sizeof(text), "%s", h3_buttons(T("Choose your profile, then Cross.")));
	else
		snprintf(text, sizeof(text), "%s", h3_join.split ? h3_buttons(T("Ready: Cross for the map.")) :
			h3_buttons(T("Ready: Cross for the games.")));
	text_show(named(screen, "next"), text);
}

/* ---- the Halo 3 multiplayer flows, nothing asked twice: START GAME hosts
with the lobby's GAME and MAP (no map or game type list after it), and the
profile chosen on SELECT PROFILE (or the campaign's) is not asked again in
this run - the network games' SELECT PROFILE goes straight on to what it
led to, with that profile, as its plate's Cross did - unless another player
joined with it (split screen's has its own screen, always asked) */

static struct
{
	/* the profile last chosen alone (NONE: none yet, or several players),
	its name (checked again: a profile deleted moves the others' indices)
	and its controller */
	long profile;
	wchar_t name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1];
	short plate;
} h3_session = { NONE, { 0 }, NONE };

static void h3_session_forget(void)
{
	h3_session.profile = NONE;
}

/* (SELECT PROFILE gone on with one player in, the campaign's profile
chosen) the profile not to ask for again */
static void h3_session_profile_chosen(short plate, long profile_index)
{
	struct player_profile profile;

	if (profile_index == NONE || plate == NONE || !player_profile_get(profile_index, &profile))
	{
		h3_session.profile = NONE;
		return;
	}
	h3_session.profile = profile_index & 0x7FFFFFFF;
	h3_session.plate = plate;
	memcpy(h3_session.name, profile.player_name, sizeof(h3_session.name) - sizeof(wchar_t));
	h3_session.name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH] = 0;
}

/* (a network game's SELECT PROFILE about to open, Menu style Halo 3) with
a profile chosen alone this run, still there and still free: what the
screen does done at once - the Xbox's "clear multiplayer player joins" as
it opens, the player joined ("join controller to mp game") with that
profile ("1wide plyr prof set for game", with its checks) - and TRUE: the
screen it went on to opens in its place. FALSE: SELECT PROFILE asks */
static boolean h3_session_profile_join(void)
{
	long entries[H3_JOIN_PROFILES];
	word count = H3_JOIN_PROFILES;
	struct player_profile profile;
	short plate = h3_session.plate;
	long entry = NONE;
	short index;

	if (h3_session.profile == NONE || plate < 0 || plate >= H3_JOIN_PLATES)
		return FALSE;
	player_profiles_enumerate_available_to_local_player_index(plate, &count, entries, TRUE);
	for (index = 0; index < (short)MIN(count, H3_JOIN_PROFILES); index++)
	{
		if ((entries[index] & 0x7FFFFFFF) == h3_session.profile)
			entry = entries[index];
	}
	if (entry == NONE || !(entry & 0x80000000) || !player_profile_get(entry, &profile) ||
		memcmp(profile.player_name, h3_session.name, sizeof(h3_session.name) - sizeof(wchar_t)) ||
		(profile.player_name[0] && !player_name_valid(profile.player_name, NUMBEROF(profile.player_name))))
	{
		platform_log("menus: h3 join: the profile chosen before is gone or taken: SELECT PROFILE");
		h3_session.profile = NONE;
		return FALSE;
	}
	dispose_global_network_game_client();
	dispose_global_network_game_server();
	player_ui_clear_multiplayer_joins();
	player_ui_clear_multiplayer_variant();
	h3_join_initialize(FALSE);
	player_ui_local_player_joined_multiplayer_game(plate);
	player_ui_set_active_player_profile(plate, entry, &profile);
	menu_functions.profile_shown = FALSE;
	{
		char name[64];

		h3_join_profile_name(&profile, name, (int)sizeof(name));
		platform_log("menus: h3 join: the profile %s chosen before: not asked again", name);
	}
	return TRUE;
}

/* (a game hosted from the Halo 3 lobby or Create Game, its server just
started) the lobby's MAP and GAME set as the Xbox's map list's and game
type list's choices set them (multiplayer_level_select, its check of a map
this version does not play with others included;
multiplayer_profile_set_for_game): TRUE, the game's lobby next; FALSE (the
map refused, no game type), their lists, as before */
static boolean h3_mp_host_choices(void)
{
	struct game_variant variant;
	char const *map = h3_mp.map_index != NONE ? h3_map_paths[h3_mp.map_index] : h3_mp.map_other;
	long variant_index = h3_mp.gametype != NONE ? h3_mp.gametypes[h3_mp.gametype] : NONE;
	struct network_game_server *server = global_network_game_server_get();
	char build[0x20], directory[256];

	if (!server || !map || !map[0] || variant_index == NONE || !(variant_index & 0x80000000))
	{
		platform_log("menus: h3 mp: no map or game type chosen: the map list");
		return FALSE;
	}
	{
		char why[160];

		if (map_check(map, why, sizeof(why)) != _map_check_playable)
		{
			platform_log("menus: h3 mp: %s can't be played: %s: the map list", map, why);
			return FALSE;
		}
	}
	if (!cache_files_map_plays_multiplayer(map, build))
	{
		cache_files_show_multiplayer_unavailable(map, build);
		platform_log("menus: h3 mp: %s is not played with others: the map list", map);
		return FALSE;
	}
	if (!playlist_profile_get(variant_index, &variant))
	{
		platform_log("menus: h3 mp: the game type could not be read: the map list");
		return FALSE;
	}
	main_set_multiplayer_map_name((char *)map);
	game_engine_override_map_name((char *)map);
	network_game_server_change_map_name(server, (char *)map);
	saved_game_file_remember_last_used_multiplayer_map((char *)map);
	if (saved_game_file_get_path_to_enclosing_directory(variant_index, directory))
		saved_game_file_remember_last_used_multiplayer_variant_directory(directory);
	player_ui_set_game_variant(&variant);
	network_game_server_change_game_variant(server, &variant);
	platform_log("menus: h3 mp: hosting %s on %s (the lobby's GAME and MAP); the game's lobby", h3_mp_gametype_name(),
		h3_mp_map_name());
	return TRUE;
}

/* ---- INVITE FRIENDS (halo3/friends.xml h3/invite/screen): L in the live
multiplayer lobby and in the game's lobby (multiplayer and co-op),
the host's: the friends online, Cross sends the chosen one the friends'
invite into this machine's game (p2p_friends_act P2P_FRIENDS_INVITE: sealed
for that friend alone, its own limit of one a friend a while; a game with a
password, or full, takes none: said). The settings panel's Friends page
does the same with Square on a friend. */

#define H3_INVITE_ROWS 10

static struct
{
	struct browser_view view;
	/* the friends online, of p2p_friends_list's (read into the FRIENDS
	screen's list, which is not open meanwhile: no second copy of it) */
	struct p2p_friend_entry entries[P2P_FRIENDS_MAXIMUM];
	int count;
	unsigned long refreshed_time;
	/* the friend chosen, by id */
	char chosen_id[P2P_FRIEND_ID_SIZE];
	char message[160];
	unsigned long message_time;
	struct widget_instance *list;
	long list_creation_time;
	char said[256];
} h3_invite;

/* (port h3 invite ask: the lobbies' INVITE FRIENDS) whether this machine
hosts a game friends can be invited into now: online, its own server */
static boolean h3_invite_ask(void)
{
	boolean can = network_game_server_port_in_lobby(global_network_game_server_get()) && internet_play() &&
		!network_game_is_splitscreen_local();

	platform_log("menus: h3 invite: INVITE FRIENDS %s", can ? "opens" : "not now (no game of this machine's online)");
	return can;
}

static void h3_invite_refresh(boolean now)
{
	unsigned long time = system_milliseconds();
	int index;

	if (!now && h3_invite.refreshed_time && time - h3_invite.refreshed_time < H3_FRIENDS_REFRESH_MILLISECONDS)
		return;
	h3_invite.refreshed_time = time ? time : 1;
	h3_friends_refresh(TRUE);
	h3_invite.count = 0;
	for (index = 0; index < h3_friends.count && h3_invite.count < P2P_FRIENDS_MAXIMUM; index++)
	{
		if (h3_friends.entries[index].kind == P2P_FRIEND_KIND_FRIEND && h3_friends.entries[index].online)
			h3_invite.entries[h3_invite.count++] = h3_friends.entries[index];
	}
}

static void h3_invite_initialize(void)
{
	int index;

	browser_view_reset(&h3_invite.view);
	h3_invite.chosen_id[0] = 0;
	h3_invite.message[0] = 0;
	h3_invite.said[0] = 0;
	h3_invite_refresh(TRUE);
	platform_log("menus: h3 invite: %d friends online", h3_invite.count);
	for (index = 0; index < h3_invite.count; index++)
	{
		platform_log("menus: h3 invite: %s: %s%s", h3_invite.entries[index].name, h3_invite.entries[index].activity,
			h3_invite.entries[index].invitable ? "" : " (not invitable now)");
	}
}

static int h3_invite_chosen_index(void)
{
	int index;

	for (index = 0; index < h3_invite.count; index++)
	{
		if (!strcmp(h3_invite.entries[index].id, h3_invite.chosen_id))
			return index;
	}
	return 0;
}

/* (port h3 invite act: a row's Cross) the invite sent, its outcome said */
static void h3_invite_act(struct widget_instance *widget)
{
	int item = h3_invite_chosen_index();
	struct p2p_friend_entry entry;
	int outcome;

	if (widget && !strncmp(widget->name, "row_", 4) && widget->name[4] >= '0' && widget->name[4] <= '9')
	{
		long row = atol(widget->name + 4) - 1;

		if (row >= 0 && row < h3_invite.view.visible)
			item = (int)(h3_invite.view.offset + row);
	}
	if (item < 0 || item >= h3_invite.count)
		return;
	entry = h3_invite.entries[item];
	outcome = p2p_friends_act(entry.id, entry.kind, P2P_FRIENDS_INVITE);
	if (outcome == P2P_FRIENDS_DONE)
		snprintf(h3_invite.message, sizeof(h3_invite.message), T("Invited %s"), entry.name);
	else
		snprintf(h3_invite.message, sizeof(h3_invite.message), "%s", p2p_friends_outcome_text(outcome));
	h3_invite.message_time = system_milliseconds();
	platform_log("menus: h3 invite: %s: %s", entry.name, h3_invite.message);
	h3_invite_refresh(TRUE);
}

/* (port h3 invite) the rows: each friend online, what they are doing,
INVITE when they can be; the outcome; how many online */
static void h3_invite_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focused = list->focused_child, *focus = NULL;
	long index, focused_row = NONE, target;
	char text[192];

	h3_invite_refresh(FALSE);
	if (list != h3_invite.list || list->creation_time != h3_invite.list_creation_time)
	{
		h3_invite.list = list;
		h3_invite.list_creation_time = list->creation_time;
		browser_view_reset(&h3_invite.view);
		focused = NULL;
	}
	for (index = 0, child = list->child; child; child = child->next)
	{
		if (strncmp(child->name, "row_", 4))
			continue;
		if (child == focused)
			focused_row = index;
		index++;
	}
	target = browser_view_follow(&h3_invite.view, H3_INVITE_ROWS, h3_invite.count, (int)focused_row,
		h3_invite_chosen_index(), 0);
	for (index = 0, child = list->child; child; child = child->next)
	{
		struct p2p_friend_entry const *entry;

		if (strncmp(child->name, "row_", 4))
			continue;
		if (index >= h3_invite.view.visible)
		{
			widget_instance_set_visibility_recursive(child, FALSE);
			child->disabled = TRUE;
			index++;
			continue;
		}
		widget_instance_set_visibility_recursive(child, TRUE);
		child->disabled = FALSE;
		entry = &h3_invite.entries[h3_invite.view.offset + index];
		row_column(child, "row_name", entry->name);
		row_column(child, "row_state", entry->activity);
		row_column(child, "row_tag", entry->invitable ? T("INVITABLE") : " ");
		if (index == h3_invite.view.row)
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
	if (target != NONE && target >= 0 && target < h3_invite.count)
		snprintf(h3_invite.chosen_id, sizeof(h3_invite.chosen_id), "%s", h3_invite.entries[target].id);
	snprintf(text, sizeof(text), T("%d ONLINE"), h3_invite.count);
	text_show(named(screen, "invite_status"), text);
	if (!h3_invite.count)
	{
		p2p_friends_status(text, sizeof(text));
		text_show(named(screen, "invite_help"), text[0] ? text : T("None of your friends is online."));
	}
	else
		text_show(named(screen, "invite_help"), h3_invite.entries[0].invitable ?
			h3_buttons(T("Cross invites a friend into your game.")) : p2p_friends_outcome_text(P2P_FRIENDS_NOT_JOINABLE));
	text_show(named(screen, "invite_message"), h3_invite.message[0] &&
		system_milliseconds() - h3_invite.message_time < H3_FRIENDS_MESSAGE_MILLISECONDS ? h3_invite.message : "");
	if ((child = named(screen, "a_butn")) != NULL)
		widget_instance_set_visibility_recursive(child, h3_invite.count > 0);
	if ((child = named(screen, "a_label")) != NULL)
		widget_instance_set_visibility_recursive(child, h3_invite.count > 0);
	snprintf(text, sizeof(text), "%d online", h3_invite.count);
	for (index = 0; index < h3_invite.count; index++)
	{
		snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s%s%s", index ? ", " : ": ",
			h3_invite.entries[index].name, h3_invite.entries[index].invitable ? "" : " (not invitable)");
	}
	if (strcmp(text, h3_invite.said))
	{
		snprintf(h3_invite.said, sizeof(h3_invite.said), "%s", text);
		platform_log("menus: h3 invite: %s", text);
	}
}

/* ---- port h3 pregame: the Halo 3 pre-game lobby (halo3/pregame.xml),
standing for the Xbox's ENLISTED PLAYERS screen: what is played, how the
game is reached (its code, public or private), the players; the buttons
are the Xbox screen's own functions, in the XML. Everything here is shown
from the game's record (network_game_get_game) and the host's listing. */
#define H3_PREGAME_ROWS 16

static struct
{
	char said[384];
} h3_pregame;

/* the frame of a level of the campaign's ten in ui.map's sp_levels, NONE
for another (a Custom Edition campaign map) */
static short h3_pregame_level(char const *map_name)
{
	short index;

	for (index = 0; map_name && index < (short)NUMBEROF(h3_level_paths); index++)
	{
		if (!_stricmp(map_name, h3_level_paths[index]))
			return index;
	}
	return NONE;
}

/* ---- the live lobby (Menu style Halo 3; the owner's model, Halo 3's): the
multiplayer lobby on ONLINE, SYSTEM LINK or AD HOC is the game's lobby from
the moment it is up. Its game's server is started then (as START GAME
started it), with the lobby's GAME and MAP set as the Xbox's lists set them
(h3_mp_host_choices), and the profile the lobby's nameplate shows (or the
one chosen this run: h3_session) joined to it; so friends, the party,
anyone with the code or from the games browser (a public game) join it
while the host is still choosing - every join through the same checks as
ever (version, password, identity, the map's). What the host changes there
goes to every joiner the way a game's settings went between two games of
the Xbox's (network_game_server_change_map_name, _change_game_variant: every
build's client takes them in its lobby, 1.1.0's too); GAME OPTIONS' name and
most players the server takes from the settings each frame, public or
private and the password the listing (p2p_lobby_set_public, _set_password).

START GAME opens the game's lobby (halo3/pregame.xml, the Xbox's ENLISTED
PLAYERS) on that game, no other server, its countdown begun at once
(network_game_server_port_start_soon: Halo 3's few seconds, once the
game can start; the game itself waits for every machine to have the map, as
ever); B there stops the countdown and goes back to the lobby, the game
kept (pc_menu_functions_live_lobby_back). B on the lobby (and the main menu
reached any other way), another NETWORK, BROWSE GAMES or JOIN WITH CODE
close the game: its joiners are told the host left (the game's own
graceful exit, network_game_server_dispose).

Not live: OFFLINE; the network not ready (Online still connecting, no
Wi-Fi, out of the ad hoc group - AD HOC's START GAME still opens the
system's dialog); no
profile at all (START GAME asks for one, as before); a map not played with
others. The lobby is as it was then, and START GAME hosts as before.
Menu style Halo CE is unchanged.

Who can join (network.who_can_join): the lobby's game is hosted as any
game, on the wire WHO CAN JOIN says as its server is made (network_wire.h):
Vitas, the Vitas' wire; Vitas and PC (ONLINE and SYSTEM LINK; ad hoc is
always the Vitas'), OpenCE's crossplay wire, its PC joiners through the
crossplay host's hardened checks (docs/crossplay.md 7.4). What the lobby
adds on that wire is nothing new to a PC: the game's settings sent again in
its lobby (network_game_server_send_game_data_pregame, as every join sends
them; a Custom Edition map named as OpenCE names it) and the countdown its
host begins. WHO CAN JOIN changed while live (GAME OPTIONS' START GAME, the
settings panel with the lobby up) closes the game, its joiners told, and it
is hosted again on the chosen wire (h3_live_follow_wire). */

#define H3_LIVE_ROWS 8
/* (START GAME's countdown: Halo 3's) */
#define H3_LIVE_COUNTDOWN_MILLISECONDS 5999
/* (the lobby up this long, its scene settled, before its game starts: the
camera's move and the poses take the main menu's ticks, which a network
game's lobby stops; at most the second) */
#define H3_LIVE_SETTLE_MILLISECONDS 1000
#define H3_LIVE_SCENE_WAIT_MILLISECONDS 8000

static struct
{
	/* the lobby's game runs: its server made for the lobby (h3_live_open) */
	boolean live;
	/* the network it runs on */
	short network;
	/* not opened, and why said: not tried again until the lobby opens again
	or its network, map or game type changes */
	boolean refused;
	/* START GAME on it: the game's lobby next (pc_menu_functions_profile
	_skip), its countdown begun there (h3_live_pregame) */
	boolean start_pending;
	/* in the game's lobby since START GAME (the lobby screen not up) */
	boolean in_pregame;
	unsigned long opened_time;
	/* the lobby's screen up since (0: not up) */
	unsigned long lobby_up_since;
	/* the players said last (the tests read them) */
	char said[256];
	/* ONLINE: the host chose public or private for this lobby (GAME
	OPTIONS' START GAME, the settings panel's Visibility while it is live);
	until then the lobby is private (h3_live_private_by_default). Kept
	while the lobby's game goes on - its game played, the lobby again, its
	wire changed - and forgotten when the lobby closes */
	boolean public_chosen;
	/* the settings panel's Visibility (HALO_NET_HOST_PUBLIC) as it was when
	the lobby opened or was last looked at: a change while live is the
	host's choice for this lobby */
	char public_setting[8];
	/* the lobby's game went on to be played (START GAME): after it the
	host comes back to the lobby, the game kept (pc_menu_functions_live
	_lobby_after_game) */
	boolean game_of_lobby;
	/* the lobby is the Forge lobby's (halo3/forge_lobby.xml): its game a
	network Forge game of its map (forge_net.inc), START FORGE its start */
	boolean forge;
} h3_live = { FALSE, NONE, FALSE, FALSE, FALSE, 0, 0, "", FALSE, "", FALSE, FALSE };

/* the network of the lobby whose game it is: the multiplayer lobby's, or
the Forge lobby's own NETWORK */
#define H3_LIVE_NETWORK (h3_live.forge ? h3_forge.network : h3_mp.network)

/* the lobby's game type and map as said (the Forge lobby's: Forge, its map) */
static char const *h3_live_gametype_name(void)
{
	return h3_live.forge ? T("Forge") : h3_mp_gametype_name();
}

static char const *h3_live_map_name(void)
{
	return h3_live.forge ? T(forge_map_name(h3_forge.map)) : h3_mp_map_name();
}

/* (the Forge lobby's live game) its map and the Forge variant set on the
game, as the multiplayer lobby's GAME and MAP are (h3_mp_host_choices), and
the game a network Forge game (forge_net_host_begin: its joiners asked to
play it, those that cannot refused from it): FALSE if it cannot be */
static boolean h3_forge_host_choices(void)
{
	struct game_variant variant;
	char const *map = forge_map_path(h3_forge.map);
	struct network_game_server *server = global_network_game_server_get();
	char build[0x20];
	char why[FORGE_STORE_WHY_SIZE];

	if (!server || !map || !forge_map_available(h3_forge.map))
	{
		platform_log("menus: forge lobby: no map to host");
		return FALSE;
	}
	if (!cache_files_map_plays_multiplayer(map, build))
	{
		cache_files_show_multiplayer_unavailable(map, build);
		platform_log("menus: forge lobby: %s is not played with others", map);
		return FALSE;
	}
	if (!forge_net_host_begin(map, h3_forge.variant_file, why, (int)sizeof(why)))
	{
		snprintf(h3_forge.note, sizeof(h3_forge.note), "%s", why);
		platform_log("menus: forge lobby: not hosted: %s", why);
		return FALSE;
	}
	main_set_multiplayer_map_name((char *)map);
	game_engine_override_map_name((char *)map);
	network_game_server_change_map_name(server, (char *)map);
	forge_variant_build(&variant);
	player_ui_set_game_variant(&variant);
	network_game_server_change_game_variant(server, &variant);
	platform_log("menus: forge lobby: hosting Forge on %s%s%s", forge_map_name(h3_forge.map),
		h3_forge.variant_file[0] ? ", map variant " : "", h3_forge.variant_title);
	return TRUE;
}

/* the lobby's GAME and MAP (or the Forge lobby's map) set on its game */
static boolean h3_live_host_choices(void)
{
	return h3_live.forge ? h3_forge_host_choices() : h3_mp_host_choices();
}

/* (the Forge lobby) whether its game is live; its map or map variant chosen
(its live game on it); START FORGE on a network (its game's lobby next, the
countdown at once: TRUE if live) */
static boolean h3_live_forge_live(void)
{
	return h3_live.live && h3_live.forge;
}

static void h3_live_forge_follow(void)
{
	if (h3_live_forge_live())
		h3_live_follow();
}

static void h3_live_lobby_show(struct widget_instance *screen);

/* (port forge lobby) the Forge lobby's NETWORK row (LOCAL for OFFLINE, a
local game; ONLINE, SYSTEM LINK, AD HOC: the connection's), START FORGE's
row of it (a local game's, or the live game's), and, live, its players, who
can join and L, INVITE FRIENDS (online), as the multiplayer lobby shows them */
static void h3_forge_lobby_network_update(struct widget_instance *screen)
{
	struct widget_instance *found;
	char text[96];
	boolean local;

	if (!h3_mp.initialized)
		h3_mp_initialize();
	h3_forge.lobby_last = TRUE;
	if (!h3_forge.network_set)
	{
		h3_forge.network_set = TRUE;
		h3_forge.network = _h3_network_offline;
	}
	if (h3_forge.network != _h3_network_offline)
		h3_forge.network = h3_mp_network_of_connection();
	local = h3_forge.network == _h3_network_offline;
	snprintf(text, sizeof(text), "%s: %s", T("NETWORK"), local ? T("LOCAL") : h3_network_name(h3_forge.network));
	h3_capitals(text);
	text_show(named(screen, "network_row"), text);
	if ((found = named(screen, "start_row")) != NULL)
	{
		widget_instance_set_visibility_recursive(found, local);
		found->disabled = !local;
	}
	if ((found = named(screen, "start_live")) != NULL)
	{
		widget_instance_set_visibility_recursive(found, !local);
		found->disabled = local;
	}
	if ((found = named(screen, "network_status")) != NULL)
		widget_instance_set_visibility_recursive(found, !local);
	/* (not live: the network's words; live: the game's, h3_live) */
	if (!h3_live_forge_live())
	{
		h3_party_players_text(text, (int)sizeof(text), 0);
		text_show(named(screen, "players"), text);
		text_show(named(screen, "ready"), T("Ready"));
		text_show(named(screen, "ready_sub"), local ? T("This party is local to this PS Vita.") :
			h3_forge.network == _h3_network_online ? T("Forge with friends on the Internet.") :
			h3_forge.network == _h3_network_adhoc ? T("Forge with the PS Vitas near you.") :
			T("Forge with the PS Vitas on your Wi-Fi."));
	}
	h3_live_lobby_show(screen);
	if (strcmp(text, h3_forge.network_said))
	{
		snprintf(h3_forge.network_said, sizeof(h3_forge.network_said), "%s", text);
		platform_log("menus: forge lobby: %s", text);
	}
}

/* the setting's value now ("true", "false") */
static void h3_live_public_setting(char *value, int size)
{
	snprintf(value, (size_t)size, "%s", setting_get("HALO_NET_HOST_PUBLIC", "true"));
}

/* (ONLINE, the lobby's game about to be hosted or kept) private by default:
the lobby is not listed by itself - friends (their one-press join, sealed
for each), the party, invites and its code reach it - until the host
chooses Public for it (GAME OPTIONS, the settings panel's Visibility); the
panel's Visibility otherwise only GAME OPTIONS' default */
static void h3_live_private_by_default(void)
{
	h3_live_public_setting(h3_live.public_setting, (int)sizeof(h3_live.public_setting));
	if (H3_LIVE_NETWORK != _h3_network_online || h3_live.public_chosen)
		return;
	p2p_lobby_set_public(0);
	platform_log("menus: h3 live lobby: private until the host chooses Public (GAME OPTIONS, the settings panel)");
}

/* (GAME OPTIONS' START GAME on ONLINE) the host chose for this lobby */
static void h3_live_public_chosen(void)
{
	h3_live.public_chosen = TRUE;
	h3_live_public_setting(h3_live.public_setting, (int)sizeof(h3_live.public_setting));
}

/* the lobby gone: a game hosted otherwise listed as the settings say again,
the choice forgotten */
static void h3_live_forget_public(void)
{
	if (!h3_live.public_chosen)
		p2p_lobby_set_public(config_boolean("network.host_public"));
	h3_live.public_chosen = FALSE;
}

/* the lobby closed: its game disposed (its joiners told the host left) */
static void h3_live_close(char const *why)
{
	if (!h3_live.live)
		return;
	h3_live.live = FALSE;
	h3_live.start_pending = FALSE;
	h3_live.in_pregame = FALSE;
	h3_live.said[0] = 0;
	dispose_global_network_game_client();
	dispose_global_network_game_server();
	network_game_accept_remote_connections(FALSE);
	player_ui_clear_multiplayer_variant();
	h3_live_forget_public();
	forge_net_host_end();
	platform_log("menus: h3 live lobby: closed (%s): its joiners are told the host left", why);
}

/* whether the lobby's network can carry a game now (FALSE: not yet, or not
at all: nothing said) */
static boolean h3_live_network_ready(void)
{
	char text[128];

	switch (H3_LIVE_NETWORK)
	{
	case _h3_network_online:
		return internet_play() && network_connection_running() == NETWORK_CONNECTION_ONLINE &&
			network_connection_status(text, (int)sizeof(text)) == NETWORK_STATUS_READY;
	case _h3_network_system_link:
		return network_connection_running() == NETWORK_CONNECTION_WIFI &&
			network_connection_status(text, (int)sizeof(text)) == NETWORK_STATUS_READY && transport_network_available();
	case _h3_network_adhoc:
		return network_connection_running() == NETWORK_CONNECTION_ADHOC &&
			__atomic_load_n(&halo_adhoc_group, __ATOMIC_ACQUIRE) == HALO_ADHOC_GROUP_IN;
	}
	return FALSE;
}

/* the live game's wire against WHO CAN JOIN's now (network.who_can_join:
the Create Game screens' row, the settings panel's Vita and PC page): a
game speaks one wire for its life (network_wire.h), Vitas' alone or Vitas'
and PCs' (OpenCE's), so a live game hosted for others than chosen is closed,
its joiners told the host left; reopen: the lobby opens again by itself on
the chosen wire (h3_live_update, the lobby up). TRUE if it was closed */
static boolean h3_live_follow_wire(boolean reopen)
{
	boolean with_pc = network_wire_host_crossplay() != 0;

	if (!h3_live.live || !global_network_game_server_get() || (network_wire_crossplay() != 0) == with_pc)
		return FALSE;
	{
		/* (the same lobby, hosted again: public or private as chosen) */
		boolean chosen = h3_live.public_chosen;

		h3_live.public_chosen = TRUE;
		h3_live_close(with_pc ? "WHO CAN JOIN is Vitas and PC now: hosted again on the crossplay wire" :
			"WHO CAN JOIN is Vitas now: hosted again on the Vitas' wire");
		h3_live.public_chosen = chosen;
		if (!chosen)
			p2p_lobby_set_public(0);
	}
	if (reopen)
		h3_live_retry();
	return TRUE;
}

static void h3_live_refuse(char const *why)
{
	h3_live.refused = TRUE;
	h3_live_forget_public();
	platform_log("menus: h3 live lobby: not opened: %s (START GAME hosts as before)", why);
}

/* (the main loop, the lobby up and its network ready) its game started:
the profile joined (the one chosen this run, else the nameplate's), the
server started, the lobby's GAME and MAP set */
static void h3_live_open(void)
{
	/* (the nameplate's profile, none chosen this run: the run's from now
	on, as SELECT PROFILE's choice would be - the owner, insider.15: BROWSE
	GAMES from the live lobby asked SELECT PROFILE for the profile the lobby
	already played with -; one that cannot join is forgotten) */
	boolean nameplate = h3_session.profile == NONE;
	boolean joined;

	if (nameplate)
	{
		h3_nameplate_read();
		if (h3_plate.shown != NONE)
			h3_session_profile_chosen(0, h3_plate.shown);
	}
	joined = h3_session_profile_join();
	if (nameplate && !joined)
		h3_session_forget();
	if (!joined)
	{
		h3_live_refuse("no profile to play with");
		return;
	}
	/* (before its server: never listed a moment by itself) */
	h3_live_private_by_default();
	if (!ui_widget_port_start_network_game_server())
	{
		h3_live_refuse("the game's server did not start");
		return;
	}
	if (!h3_live_host_choices())
	{
		dispose_global_network_game_client();
		dispose_global_network_game_server();
		network_game_accept_remote_connections(FALSE);
		player_ui_clear_multiplayer_variant();
		forge_net_host_end();
		h3_live_refuse("its map or game type cannot be played with others");
		return;
	}
	h3_live.live = TRUE;
	h3_live.network = H3_LIVE_NETWORK;
	h3_live.start_pending = FALSE;
	h3_live.in_pregame = FALSE;
	h3_live.opened_time = system_milliseconds();
	h3_live.said[0] = 0;
	platform_log("menus: h3 live lobby: open on %s (%s on %s): others join it now", h3_network_name(H3_LIVE_NETWORK),
		h3_live_gametype_name(), h3_live_map_name());
}

/* (the lobby opened again, another network) a refused lobby tried again */
static void h3_live_retry(void)
{
	h3_live.refused = FALSE;
}

/* (the lobby's MAP or GAME chosen) the live game on it, as its settings
went to the joiners between two games: FALSE if the map or game type was
refused (the lobby goes back to the game's) */
static boolean h3_live_follow(void)
{
	struct network_game_server *server = global_network_game_server_get();

	h3_live.refused = FALSE;
	if (!h3_live.live)
		return TRUE;
	if (!server || !network_game_server_port_in_lobby(server))
		return TRUE;
	if (!h3_live_host_choices())
		return FALSE;
	platform_log("menus: h3 live lobby: now %s on %s, sent to its joiners", h3_live_gametype_name(), h3_live_map_name());
	return TRUE;
}

/* the players of the game now: in the lobby's rows (row_1 ...), each
in their colour, their team, YOU (this machine's) and a joiner's map copy
(the host's); the rest of the rows hidden; how many players there are, the
names said in line */
static long h3_roster_show(struct widget_instance *screen, struct network_game const *game, boolean host,
	short local_machine, long rows, char *line, int line_size)
{
	struct widget_instance *found;
	long index, row = 0, count = 0;

	for (index = 0; game && index < (long)NUMBEROF(game->players); index++)
	{
		struct network_player const *player = &game->players[index];
		char row_name[16], name[48], note[48];
		struct widget_instance *row_widget;

		if (!network_player_is_valid((struct network_player *)player))
			continue;
		count++;
		{
			wchar_t player_name[13];

			csmemcpy(player_name, player->name, sizeof(player->name));
			player_name[12] = 0;
			h3_utf8(name, (int)sizeof(name), player_name);
		}
		if (line)
			snprintf(line + strlen(line), (size_t)line_size - strlen(line), "%s%s", count > 1 ? ", " : "; players: ", name);
		snprintf(row_name, sizeof(row_name), "row_%ld", row + 1);
		if (row >= rows || (row_widget = named(screen, row_name)) == NULL)
			continue;
		widget_instance_set_visibility_recursive(row_widget, TRUE);
		text_show(named(row_widget, "row_name"), name);
		if ((found = named(row_widget, "row_swatch")) != NULL)
			found->animation.current_frame_index = h3_swatch_frame(player->primary_color_index);
		note[0] = 0;
		if (game->variant.universal_variant.teams)
			snprintf(note, sizeof(note), "%s", player->team_index == 0 ? T("RED") : player->team_index == 1 ? T("BLUE") : "");
		if (player->machine_index == local_machine)
			snprintf(note + strlen(note), sizeof(note) - strlen(note), "%s%s", note[0] ? " " : "", T("YOU"));
		else if (host)
		{
			short percent = map_share_server_machine_percent(player->machine_index);

			if (percent != NONE)
				snprintf(note + strlen(note), sizeof(note) - strlen(note), T("%smap %d%%"), note[0] ? " " : "", (int)percent);
		}
		text_show(named(row_widget, "row_note"), note);
		row++;
	}
	for (; row < H3_PREGAME_ROWS; row++)
	{
		char row_name[16];

		snprintf(row_name, sizeof(row_name), "row_%ld", row + 1);
		if ((found = named(screen, row_name)) != NULL)
			widget_instance_set_visibility_recursive(found, FALSE);
	}
	return count;
}

/* (port h3 mp lobby) the lobby's live parts: its players in rows (the
nameplate when not live), how many and the most, what the lobby is now, the
INVITE FRIENDS row (online) */
static void h3_live_lobby_show(struct widget_instance *screen)
{
	struct network_game *game = h3_live.live ? network_game_get_game() : NULL;
	struct network_game_client *client = global_network_game_client_get();
	struct widget_instance *found;
	char text[192], line[256];
	long count;
	boolean with_pc;

	if ((found = named(screen, "nameplate")) != NULL)
		widget_instance_set_visibility_recursive(found, game == NULL);
	/* (L: INVITE FRIENDS, online) */
	if ((found = named(screen, "l_butn")) != NULL)
		widget_instance_set_visibility_recursive(found, game != NULL && h3_live.network == _h3_network_online);
	if ((found = named(screen, "invite")) != NULL)
		widget_instance_set_visibility_recursive(found, game != NULL && h3_live.network == _h3_network_online);
	line[0] = 0;
	count = h3_roster_show(screen, game, TRUE, client ? network_game_client_get_local_machine_index() : NONE,
		game ? H3_LIVE_ROWS : 0, line, (int)sizeof(line));
	if (!game)
		return;
	snprintf(text, sizeof(text), T("PLAYERS %ld/%d"), count, (int)game->maximum_players);
	text_show(named(screen, "players"), text);
	if (count > 1)
		snprintf(text, sizeof(text), T("Lobby open: %ld players"), count);
	else
		snprintf(text, sizeof(text), "%s", T("Lobby open"));
	text_show(named(screen, "ready"), text);
	/* (who can join: the game's wire, WHO CAN JOIN's as it was hosted) */
	with_pc = network_wire_crossplay() != 0;
	if (h3_live.network == _h3_network_online)
	{
		int password = 0;
		boolean listed = p2p_lobby_hosting_listed(&password) != 0;

		if (h3_live.forge)
			text_show(named(screen, "ready_sub"), T("Never listed: friends, the party, your\ninvites and your code join it."));
		else if (with_pc)
			text_show(named(screen, "ready_sub"), listed ? T("Public, Vitas and PC: friends join,\nand anyone from the games browser.") :
				T("Private, Vitas and PC: friends, your\ninvites and your code join it."));
		else
			text_show(named(screen, "ready_sub"), listed ? T("Public: friends join, and anyone from\nthe games browser.") :
				T("Private: friends, your invites and\nyour code join it."));
	}
	else
		text_show(named(screen, "ready_sub"), h3_live.forge ? (h3_live.network == _h3_network_adhoc ?
			T("The PS Vitas near you can join.\nSTART FORGE takes everyone here.") :
			T("The PS Vitas on your Wi-Fi can join.\nSTART FORGE takes everyone here.")) :
			h3_live.network == _h3_network_adhoc ?
			T("The PS Vitas near you can join.\nSTART GAME plays with everyone here.") : with_pc ?
			T("PS Vitas and PCs on your Wi-Fi can\njoin. START GAME plays with everyone.") :
			T("The PS Vitas on your Wi-Fi can join.\nSTART GAME plays with everyone here."));
	snprintf(line + strlen(line), sizeof(line) - strlen(line), "; %ld/%d%s", count, (int)game->maximum_players,
		with_pc ? "; Vitas and PC" : "");
	if (strcmp(line, h3_live.said))
	{
		snprintf(h3_live.said, sizeof(h3_live.said), "%s", line);
		platform_log("menus: h3 live lobby: %s on %s%s", h3_live_gametype_name(), h3_live_map_name(), line);
	}
}

/* (the main loop) the live lobby opened when its screen is up and its
network ready, kept while its game is in its lobby, closed once the host
is back at the main menu; the host's player kept in its game (the lobby
does what the game's lobby's "net splitscreen prejoin players" does) */
static void h3_live_update(boolean main_menu_loaded)
{
	struct widget_instance *active = ui_widget_port_active_screen(0);
	long lobby_tag = menu_tags_ingame_screen("h3/mp/screen");
	long forge_tag = menu_tags_ingame_screen("h3/forge/lobby");
	long main_tag = menu_tags_ingame_screen("h3/main/screen");
	/* (the multiplayer lobby, or the Forge lobby: each live the same way,
	the Forge lobby's game a network Forge game, forge_net.inc) */
	boolean mp_up = active && lobby_tag != NONE && active->definition_tag_index == lobby_tag;
	boolean forge_up = active && forge_tag != NONE && active->definition_tag_index == forge_tag;
	boolean lobby_up = mp_up || forge_up;
	struct network_game_server *server = global_network_game_server_get();
	struct network_game_client *client = global_network_game_client_get();

	if (!main_menu_loaded)
	{
		/* (its game started: the game's now) */
		if (h3_live.live)
		{
			h3_live.game_of_lobby = server != NULL;
			platform_log("menus: h3 live lobby: its game started");
		}
		h3_live.live = FALSE;
		h3_live.start_pending = FALSE;
		h3_live.in_pregame = FALSE;
		return;
	}
	/* (the lobby's game gone - quit, closed, lost -: nothing to come back to) */
	if (!server)
		h3_live.game_of_lobby = FALSE;
	if (h3_live.live)
	{
		short state = NONE;

		/* (disposed by another flow: SELECT PROFILE's, a join's) */
		if (!server || !client || !network_game_server_port_in_lobby(server))
		{
			h3_live.live = FALSE;
			h3_live.start_pending = FALSE;
			h3_live.in_pregame = FALSE;
			/* (its game under way keeps its listing as the lobby left it,
			and the host comes back to the lobby after it) */
			h3_live.game_of_lobby = server != NULL && !network_game_server_port_in_lobby(server);
			if (!server)
				h3_live_forget_public();
			platform_log("menus: h3 live lobby: its game is no longer this lobby's");
			return;
		}
		if (active && main_tag != NONE && active->definition_tag_index == main_tag)
		{
			h3_live_close("the host is back at the main menu");
			return;
		}
		/* (the other lobby opened: its game is another kind) */
		if ((mp_up && h3_live.forge) || (forge_up && !h3_live.forge))
		{
			h3_live_close(forge_up ? "the Forge lobby opened" : "the multiplayer lobby opened");
			return;
		}
		if (lobby_up)
			h3_live.in_pregame = FALSE;
		/* (the connection another now - Online's fallback to Same Wi-Fi -:
		the game was the old one's) */
		if (H3_LIVE_NETWORK != h3_live.network)
		{
			h3_live_close("the lobby's network changed");
			return;
		}
		/* (the settings panel's Visibility changed while live: the host's
		choice for this lobby - the panel listed or unlisted it at once) */
		{
			char now[8];

			h3_live_public_setting(now, (int)sizeof(now));
			if (strcmp(now, h3_live.public_setting))
			{
				snprintf(h3_live.public_setting, sizeof(h3_live.public_setting), "%s", now);
				if (h3_live.network == _h3_network_online)
				{
					h3_live.public_chosen = TRUE;
					platform_log("menus: h3 live lobby: %s, as the settings panel chose", strcmp(now, "false") ? "public" : "private");
				}
			}
		}
		/* (WHO CAN JOIN changed on the settings panel, the lobby up: hosted
		again on that wire; the game's lobby's game plays on as it began) */
		if (lobby_up && h3_live_follow_wire(TRUE))
			return;
		/* (the host's player in its game, as the game's lobby adds it) */
		if (network_game_client_get_state(client, &state) == 2)
		{
			struct network_game *game = network_game_get_game();
			short local_player;

			for (local_player = 0; game && local_player < 4; local_player++)
			{
				long index;
				boolean in = FALSE;

				if (!player_ui_local_player_wants_to_play_multiplayer(local_player))
					continue;
				for (index = 0; index < (long)NUMBEROF(game->players) && !in; index++)
				{
					in = network_player_is_valid(&game->players[index]) &&
						network_game_player_is_local(&game->players[index]) &&
						game->players[index].controller_index == local_player;
				}
				if (!in && !network_game_client_add_player(client, local_player))
					platform_log("menus: h3 live lobby: local player %d could not be added to its game", local_player);
			}
		}
		return;
	}
	if (!lobby_up)
	{
		h3_live.lobby_up_since = 0;
		/* (back at the main menu with no live game: the next lobby is a new
		one, private by default again) */
		if (h3_live.public_chosen && active && main_tag != NONE && active->definition_tag_index == main_tag && !server)
			h3_live_forget_public();
		return;
	}
	if (!h3_live.lobby_up_since)
		h3_live.lobby_up_since = system_milliseconds() ? system_milliseconds() : 1;
	/* (which lobby's game: the multiplayer lobby's or the Forge lobby's) */
	h3_live.forge = forge_up;
	/* (a game the lobby's START GAME hosted the way it did before it was
	live - not ready then, or no profile - its lobby left with B, the game
	kept as the Xbox's lobby keeps it: the lobby's live game now, its
	players with it, when the network carries it; else closed, its joiners
	told: never a game left behind the lobby, holding the connection) */
	if (server && client && !h3_live.refused && H3_LIVE_NETWORK != _h3_network_offline && halo_menu_style_halo3() &&
		network_game_server_port_in_lobby(server) && system_milliseconds() - h3_live.lobby_up_since >= H3_LIVE_SETTLE_MILLISECONDS)
	{
		if (!h3_live_keep())
		{
			h3_live.live = TRUE;
			h3_live_close("the game it hosted before cannot go on from it");
		}
		return;
	}
	if (h3_live.refused || H3_LIVE_NETWORK == _h3_network_offline || !halo_menu_style_halo3() || server || client ||
		!h3_live_network_ready())
	{
		return;
	}
	/* (once its scene stands, posed, its camera in place: a network game's
	lobby stops the main menu's ticks - menu_scene_objects_settled -; at
	most H3_LIVE_SCENE_WAIT_MILLISECONDS, its scene then none) */
	{
		unsigned long up = system_milliseconds() - h3_live.lobby_up_since;

		if (up < H3_LIVE_SETTLE_MILLISECONDS || (!menu_scene_objects_settled() && up < H3_LIVE_SCENE_WAIT_MILLISECONDS))
			return;
	}
	h3_live_open();
}

/* (START GAME on the lobby: ONLINE's, SYSTEM LINK's, AD HOC's) the live
game's lobby next, no other server: TRUE if the lobby is live */
static boolean h3_live_start(void)
{
	if (!h3_live.live || !global_network_game_server_get())
		return FALSE;
	menu_functions.create_pending = FALSE;
	menu_functions.create_system_link = FALSE;
	h3_live.start_pending = TRUE;
	platform_log("menus: h3 live lobby: START GAME: the game's lobby, its countdown begun");
	return TRUE;
}

static boolean h3_live_forge_start(void)
{
	if (!h3_live_forge_live())
		return FALSE;
	return h3_live_start();
}

/* (the game's lobby opening, port h3 pregame init) after START GAME on a
live lobby: the countdown begun at once */
static void h3_live_pregame(void)
{
	struct network_game_server *server = global_network_game_server_get();

	if (!h3_live.live || !h3_live.start_pending)
		return;
	h3_live.start_pending = FALSE;
	h3_live.in_pregame = TRUE;
	if (server && network_game_server_port_start_soon(server, H3_LIVE_COUNTDOWN_MILLISECONDS))
		platform_log("menus: h3 live lobby: the game's lobby: the countdown begun");
	else
		platform_log("menus: h3 live lobby: the game's lobby: waiting for players before the countdown");
}

/* a game in its lobby behind the lobby (kept by B on the game's lobby, or
played and over) the lobby's live game, when the network carries it and
the lobby's GAME and MAP can be played: TRUE */
static boolean h3_live_keep(void)
{
	if (!h3_live_network_ready() || !h3_live_host_choices())
		return FALSE;
	h3_live_private_by_default();
	h3_live.live = TRUE;
	h3_live.network = H3_LIVE_NETWORK;
	h3_live.start_pending = FALSE;
	h3_live.in_pregame = FALSE;
	h3_live.opened_time = system_milliseconds();
	h3_live.said[0] = 0;
	platform_log("menus: h3 live lobby: its game kept, live on %s (%s on %s)", h3_network_name(H3_LIVE_NETWORK),
		h3_live_gametype_name(), h3_live_map_name());
	return TRUE;
}

/* (network_game_reset_to_pregame_ui, the host's: a game over, the main
menu loaded again) a game the live lobby started comes back to the lobby,
not to the Xbox's map choice: the Halo 3 multiplayer lobby opened (B on it
goes to the main menu, closing the game as ever), where its game is the
lobby's live game again, kept with its joiners - who wait in the game's
lobby as between any two games - its GAME and MAP the lobby's
(h3_live_update's kept game). TRUE if the lobby opened */
boolean pc_menu_functions_live_lobby_after_game(void)
{
	/* (the Forge lobby's game: back to the Forge lobby) */
	long lobby_tag = menu_tags_ingame_screen(h3_live.forge ? "h3/forge/lobby" : "h3/mp/screen");
	long main_tag = menu_tags_ingame_screen("h3/main/screen");
	struct network_game_server *server = global_network_game_server_get();
	struct widget_instance *screen;

	if (!h3_live.game_of_lobby)
		return FALSE;
	h3_live.game_of_lobby = FALSE;
	if (!server || !halo_menu_style_halo3() || lobby_tag == NONE || main_tag == NONE)
		return FALSE;
	network_game_server_pause_countdown(server, TRUE);
	h3_live.refused = FALSE;
	h3_live.lobby_up_since = 0;
	if ((screen = ui_widget_load_by_name_or_tag(NULL, lobby_tag, NULL, 0, main_tag, NONE, NONE)) == NULL)
		return FALSE;
	/* (START GAME focused: the next game is the host's to start) */
	{
		static char const *const start_rows[NUMBER_OF_H3_NETWORKS] = { "start_online", "start_link", "start_adhoc",
			"start_offline" };
		struct widget_instance *list = named(screen, "list");
		struct widget_instance *start = h3_live.forge ? named(screen, "start_live") :
			h3_mp.network >= 0 && h3_mp.network < NUMBER_OF_H3_NETWORKS ? named(screen, start_rows[h3_mp.network]) : NULL;

		if (list && start)
			ui_widget_port_give_focus(list, start);
	}
	platform_log("menus: h3 live lobby: the game over, back to the lobby with its game and players");
	/* (live at once: START GAME pressed straight away starts this game
	again, never a server of its own; else the lobby's update closes it,
	its joiners told, as any game it cannot go on with) */
	h3_live_keep();
	return TRUE;
}

/* (display_error, the host's game closed: _error_network_server_shut_down)
the text naming the network the game was played on - the Xbox's says
System Link whatever it was: online (internet play, a friend's or a code's
game too), System Link (the same Wi-Fi), ad hoc */
wchar_t const *pc_menu_functions_host_left_text(void)
{
	short running = network_connection_running();

	platform_log("menus: the host's game closed: said for %s", running == NETWORK_CONNECTION_ONLINE ? "online play" :
		running == NETWORK_CONNECTION_ADHOC ? "ad hoc" : "System Link");
	switch (running)
	{
	case NETWORK_CONNECTION_ONLINE:
		return TW(L"The online game has\r\nclosed down.");
	case NETWORK_CONNECTION_ADHOC:
		return TW(L"The ad hoc game has\r\nclosed down.");
	default:
		return TW(L"The System Link game\r\nhas closed down.");
	}
}

/* (netgame_unjoin_player: B on the game's lobby) the host of a live lobby
goes back to it with its game kept, the countdown stopped: TRUE (nothing
else done); FALSE for any other lobby */
boolean pc_menu_functions_live_lobby_back(void)
{
	struct network_game_server *server = global_network_game_server_get();

	if (!h3_live.live || !server || !network_game_server_port_in_lobby(server))
		return FALSE;
	network_game_server_pause_countdown(server, TRUE);
	h3_live.start_pending = FALSE;
	h3_live.in_pregame = FALSE;
	platform_log("menus: h3 live lobby: back from the game's lobby: the countdown stopped, the game kept");
	return TRUE;
}

static void h3_pregame_update(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	struct widget_instance *found;
	struct network_game *game = network_game_get_game();
	struct network_game_client *client = global_network_game_client_get();
	struct network_game_server *server = global_network_game_server_get();
	boolean cooperative, host = server != NULL, online, waiting, listed = FALSE, password = FALSE;
	short local_machine = client ? network_game_client_get_local_machine_index() : NONE;
	short seconds = client ? network_game_client_get_seconds_to_game_start(client) : -1;
	char text[192], code[32], line[384];
	wchar_t wide[64];
	long index, row = 0;

	if (!game)
		return;
	cooperative = coop_menu_game_is_cooperative(game);
	online = config_boolean("network.online") && p2p_running();
	waiting = host && game->machine_count < 2;
	code[0] = 0;
	if (host && online)
		p2p_hosting_code(code, (int)sizeof(code));
	if (host && online)
		listed = p2p_lobby_hosting_listed(&password) != 0;
	text_show(named(screen, "title"), cooperative ? T("CO-OP LOBBY") : T("MULTIPLAYER LOBBY"));
	/* (what the lobby waits for, or its countdown) */
	if (seconds > 0)
		snprintf(text, sizeof(text), T("Starting in %d:%02d"), seconds / 60, seconds % 60);
	else if (seconds == 0)
	{
		char builder[64];
		short percent = 0;

		/* (the host: a joiner still building the extra objects holds the
		start, forge_net.inc) */
		if (host && forge_net_server_holds_start(server, builder, (int)sizeof(builder), &percent))
			snprintf(text, sizeof(text), T("Waiting for %s to build the extra objects (%d%%)"),
				builder[0] ? builder : "?", (int)percent);
		else
			snprintf(text, sizeof(text), "%s", T("Starting..."));
	}
	else if (waiting)
		snprintf(text, sizeof(text), "%s", cooperative && game->maximum_players <= 2 ? T("Waiting for your partner") :
			T("Waiting for other players"));
	else if (host)
		snprintf(text, sizeof(text), "%s", h3_buttons(T("Ready: Cross starts the game")));
	else
		snprintf(text, sizeof(text), "%s", T("Waiting for the host to start"));
	text_show(named(screen, "status"), text);
	/* (the log's line: the countdown once, not each second) */
	snprintf(line, sizeof(line), "%s", seconds > 0 ? "Starting (the countdown)" : text);
	text_show(named(screen, "status_sub"), waiting ? (cooperative ? T("Others join from MULTIPLAYER: BROWSE\nGAMES or JOIN WITH CODE.") :
		T("Others join from BROWSE GAMES or\nJOIN WITH CODE.")) : seconds > 0 ? h3_buttons(T("Square: wait longer.")) : "");
	/* (what is played: the mission and its difficulty, or the game type on
	the map) */
	if (cooperative)
	{
		short level = h3_pregame_level(game->map.name);
		char level_name[96], difficulty[32];

		coop_menu_level_name(game->map.name, wide, NUMBEROF(wide));
		h3_utf8(level_name, (int)sizeof(level_name), wide);
		coop_menu_difficulty_name(game->difficulty, wide, NUMBEROF(wide));
		h3_utf8(difficulty, (int)sizeof(difficulty), wide);
		snprintf(text, sizeof(text), "%s\n%s", level_name, difficulty);
		if ((found = named(screen, "mission_picture")) != NULL)
		{
			widget_instance_set_visibility_recursive(found, level != NONE);
			found->animation.current_frame_index = level != NONE ? level : 0;
		}
		if ((found = named(screen, "emblem")) != NULL)
		{
			widget_instance_set_visibility_recursive(found, TRUE);
			found->animation.current_frame_index = (short)PIN(game->difficulty, 0, 3);
		}
		if ((found = named(screen, "map_picture")) != NULL)
			widget_instance_set_visibility_recursive(found, FALSE);
	}
	else
	{
		char gametype[48], map[96];
		short map_index;

		for (index = 0; index < (long)NUMBEROF(game->variant.human_readable_game_description) &&
			game->variant.human_readable_game_description[index]; index++)
			wide[index] = game->variant.human_readable_game_description[index];
		wide[index] = 0;
		h3_utf8(gametype, (int)sizeof(gametype), wide);
		snprintf(map, sizeof(map), "%s", custom_edition_maps_level_title(game->map.name));
		snprintf(text, sizeof(text), T("%s on %s"), gametype[0] ? gametype : T("Slayer"), map);
		for (map_index = 0; map_index < H3_MAP_COUNT && _stricmp(game->map.name, h3_map_paths[map_index]); map_index++)
			;
		if ((found = named(screen, "map_picture")) != NULL)
		{
			short display = map_index < H3_MAP_COUNT ? NONE : custom_edition_maps_display_index(game->map.name);

			widget_instance_set_visibility_recursive(found, TRUE);
			found->animation.current_frame_index = map_index < H3_MAP_COUNT ? map_index :
				display != NONE ? display : H3_MAP_OTHER_PICTURE;
		}
		if ((found = named(screen, "mission_picture")) != NULL)
			widget_instance_set_visibility_recursive(found, FALSE);
		if ((found = named(screen, "emblem")) != NULL)
			widget_instance_set_visibility_recursive(found, FALSE);
	}
	text_show(named(screen, "caption"), text);
	{
		char flat[192];
		char *newline;

		snprintf(flat, sizeof(flat), "%s", text);
		while ((newline = strchr(flat, '\n')) != NULL)
			*newline = ' ';
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "; %s", flat);
	}
	/* (how it is reached: online its code, large, as the game lists show
	it; system link; ad hoc) */
	if (config_boolean("network.adhoc"))
	{
		text_show(named(screen, "code_label"), T("NETWORK"));
		text_show(named(screen, "code"), T("AD HOC"));
	}
	else if (online && code[0])
	{
		text_show(named(screen, "code_label"), T("CODE: others join with it"));
		text_show(named(screen, "code"), code);
	}
	else if (online)
	{
		text_show(named(screen, "code_label"), T("NETWORK"));
		text_show(named(screen, "code"), T("ONLINE"));
	}
	else
	{
		text_show(named(screen, "code_label"), T("NETWORK"));
		text_show(named(screen, "code"), T("SYSTEM LINK"));
	}
	h3_utf8(text, (int)sizeof(text), game->name);
	snprintf(line + strlen(line), sizeof(line) - strlen(line), "; %s%s; %s", code[0] ? "code " : "", code[0] ? code :
		online ? "online" : config_boolean("network.adhoc") ? "ad hoc" : "system link", text);
	/* (the host's, online: listed in the server browser or not, and what
	Square does: co-op's choice while it waits, coop_menu.c) */
	if (host && online)
	{
		snprintf(text, sizeof(text), "%s", listed ? (password ? T("PUBLIC: listed in the server browser,\nwith a password.") :
			T("PUBLIC: listed in the server browser.")) : T("PRIVATE: joined by its code."));
		text_show(named(screen, "visibility"), text);
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "; %s", listed ? "public" : "private");
	}
	else
		text_show(named(screen, "visibility"), host ? "" : T("The host chooses the map and starts."));
	{
		char name[48];

		h3_utf8(name, (int)sizeof(name), game->name);
		/* (the host: its game's name and tag - VITA, or VITA+PC when PC
		players can join it; a client: what hosts the game, in words) */
		if (host)
		{
			char badge[24];

			/* (VITA (PC) on the PC build) */
			crossplay_guard_host_badge(badge, (int)sizeof(badge), CROSSPLAY_PLATFORM_VITA,
				network_wire_this_platform() == NETWORK_PLATFORM_VITA_PC ? CROSSPLAY_HOST_PC_BUILD : 0,
				network_wire_crossplay(), 0, CROSSPLAY_GUARD_OK, 0);
			snprintf(text, sizeof(text), T("Listed as: %s (%s)"), name, badge);
		}
		else
			snprintf(text, sizeof(text), "%s", crossplay_guard_platform_line(network_wire_host_platform(),
				network_wire_crossplay()));
		text_show(named(screen, "listed"), text);
		/* (the host of a game for Vitas and PC, online: its join link, which
		PC players (OpenCE) open as their own links - a bearer token, as the
		code is: on the screen, never in the log - docs/crossplay.md T16) */
		{
			char invite[96];

			text[0] = 0;
			if (host && online && network_wire_crossplay() && p2p_hosting_invite(invite, (int)sizeof(invite)) &&
				strlen(invite) > 38)
			{
				snprintf(text, sizeof(text), "%s\n%.38s\n%s", T("PC players (OpenCE) join with:"), invite, invite + 38);
				snprintf(line + strlen(line), sizeof(line) - strlen(line), "; PC link shown");
			}
			text_show(named(screen, "pc_link"), text);
		}
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "; %s", network_wire_crossplay() ?
			(network_wire_host_vita() ? "crossplay wire, a Vita's game" : "crossplay wire, a PC's game") :
			"the Vitas' wire");
	}
	{
		boolean toggles = cooperative && waiting && online && config_boolean("network.public_lobby") &&
			network_game_server_port_cooperative_menu(NULL);

		text_show(named(screen, "x_text"), toggles ? (listed ? T("Make it private") : T("Make it public")) :
			seconds > 0 ? T("Delay the start") : "");
		if ((found = named(screen, "x_butn")) != NULL)
			widget_instance_set_visibility_recursive(found, toggles || seconds > 0);
	}
	text_show(named(screen, "b_text"), host ? T("Cancel") : T("Leave"));
	/* (L: INVITE FRIENDS, the host's online) */
	if ((found = named(screen, "l_butn")) != NULL)
		widget_instance_set_visibility_recursive(found, host && online);
	if ((found = named(screen, "l_text")) != NULL)
		widget_instance_set_visibility_recursive(found, host && online);
	/* (alone in the host's lobby, Cross starts nothing yet: not shown, as
	the Xbox's lobby hid it; nor a joiner's before the countdown - the host
	starts the game, and a live lobby's host holds it in the lobby: its
	Cross only hurries a countdown under way) */
	if ((found = named(screen, "a_butn")) != NULL)
		widget_instance_set_visibility_recursive(found, !waiting && (host || seconds >= 0));
	if ((found = named(screen, "a_text")) != NULL)
		widget_instance_set_visibility_recursive(found, !waiting && (host || seconds >= 0));
	/* (the players: each machine's, this one's first) */
	row = h3_roster_show(screen, game, host, local_machine, H3_PREGAME_ROWS, line, (int)sizeof(line));
	/* (the most the game takes: the host's settings' - network.max_players,
	16 unless chosen - or co-op's network.coop_players, as its record has
	them) */
	snprintf(text, sizeof(text), T("PLAYERS %ld/%d"), row, (int)game->maximum_players);
	text_show(named(screen, "players"), text);
	snprintf(line + strlen(line), sizeof(line) - strlen(line), "; %ld/%d", row, (int)game->maximum_players);
	/* (said as it changes: the tests read it) */
	if (strcmp(line, h3_pregame.said))
	{
		snprintf(h3_pregame.said, sizeof(h3_pregame.said), "%s", line);
		platform_log("menus: h3 pregame: %s", line);
	}
}

boolean pc_menu_event_function_invoke(
	struct widget_instance *widget,
	struct event_record *event,
	long function_index,
	boolean *widget_deleted)
{
	switch (function_index)
	{
	case _function_mp_screen_init:
		menu_functions.create_pending = FALSE;
		menu_functions.browser_profile_pending = FALSE;
		menu_functions.browser_profile_chosen = FALSE;
		menu_functions.status[0] = 0;
		return TRUE;
	case _function_browser_profile_first:
		/* (SERVER BROWSER: as SYSTEM LINK and CREATE GAME, the profile first;
		internet play off: "port mp require online" opens why instead) */
		menu_functions.browser_profile_pending = internet_play() ||
			network_connection_wanted() == NETWORK_CONNECTION_ONLINE;
		if (menu_functions.browser_profile_pending)
			platform_log("menus: Server Browser: the profile, then the server browser");
		return TRUE;
	case _function_mp_require_online:
		/* (ONLINE chosen: internet play starts at the next frame, before
		anything is hosted or joined) */
		return internet_play() || network_connection_wanted() == NETWORK_CONNECTION_ONLINE;
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
	case _function_browser_filter_next:
	case _function_browser_filter_previous:
	{
		/* (the D-pad's right and left: all games, the Vitas' alone, the PCs'
		alone, kept as the settings panel's Browser shows is) */
		static char const *const filters[] = { "all", "vita", "pc" };
		int filter = network_wire_browser_filter();

		filter = (filter + (function_index == _function_browser_filter_next ? 1 : 2)) % 3;
		setting_set("HALO_NET_BROWSER_FILTER", filters[filter]);
		menu_functions.chosen_valid = FALSE;
		browser_view_reset(&menu_functions.view);
		platform_log("menus: the server browser shows %s", filter == NETWORK_WIRE_FILTER_VITA ? "the Vitas' games" :
			filter == NETWORK_WIRE_FILTER_PC ? "the PCs' games" : "every game");
		return TRUE;
	}
	case _function_browser_join:
		return browser_join(widget, widget_deleted);
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
		__atomic_store_n(&halo_settings_panel_request, HALO_SETTINGS_PANEL_OPEN, __ATOMIC_RELEASE);
		platform_log("menus: the settings panel asked for");
		return FALSE;
#else
		platform_log("menus: the settings panel asked for (none on this build)");
		return TRUE;
#endif
	case _function_h3_campaign_init:
		h3_campaign_initialize();
		return TRUE;
	case _function_h3_mission_set:
	{
		short level = (short)PIN(h3_child_index(widget), 0, h3_campaign_unlocked() - 1);
		char why[160];

		/* (a level that cannot be played: not chosen, its reason shown) */
		if (map_check(h3_level_paths[level], why, sizeof(why)) != _map_check_playable)
		{
			platform_log("menus: h3 campaign: %s can't be chosen: %s", h3_level_names[level], why);
			return FALSE;
		}
		h3_campaign.level = level;
		h3_campaign_said();
		return TRUE;
	}
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
		/* (the first time this run, MAP focused, as before NETWORK came
		first) */
		if (!h3_forge.focused_once)
		{
			struct widget_instance *list = named(screen_of(widget), "list");

			h3_forge.focused_once = TRUE;
			if (list)
				h3_focus_child(list, 1);
		}
		return TRUE;
	case _function_forge_map_set:
		if (forge_map_available((short)h3_child_index(widget)) && h3_forge.map != (short)h3_child_index(widget))
		{
			h3_forge.map = (short)h3_child_index(widget);
			/* (another map: its own variants, none chosen) */
			h3_forge.variant_file[0] = 0;
			h3_forge.variant_title[0] = 0;
			h3_forge.note[0] = 0;
		}
		platform_log("menus: forge lobby: %s", forge_map_name(h3_forge.map));
		return TRUE;
	case _function_forge_map_focus:
		h3_forge.picking_variant = FALSE;
		h3_focus_child(widget, h3_forge.map);
		return TRUE;
	case _function_forge_start:
		/* (START FORGE: the local game of the map, in Forge mode; nothing
		with no map to play; on a network the lobby's live game's, START
		FORGE's row of it) */
		if (h3_forge.network != _h3_network_offline)
			return FALSE;
		/* (a map that cannot be played: said, as a pack being built is) */
		if (forge_map_state(h3_forge.map, h3_forge.note, sizeof(h3_forge.note)) != _map_check_playable)
		{
			platform_log("menus: forge lobby: not started: %s", h3_forge.note);
			return FALSE;
		}
		{
			char label[64];
			long progress = 0;

			/* (a pack still being built: not now - its build on the map's
			load would hold the screen for as long) */
			if (forge_packs_building(label, sizeof(label), &progress))
			{
				snprintf(h3_forge.note, sizeof(h3_forge.note),
					T("Still building %s (%ld%%).\nSTART FORGE when it is done, or\nturn it off in EXTRA OBJECTS."), label,
					progress);
				platform_log("menus: forge lobby: not started: %s is being built (%ld%%)", label, progress);
				return FALSE;
			}
		}
		platform_log("menus: forge lobby: start %s%s%s", forge_map_path(h3_forge.map),
			h3_forge.variant_file[0] ? " with the map variant " : "", h3_forge.variant_file);
		if (!forge_game_start(h3_forge.map, h3_forge.variant_file, h3_forge.note, sizeof(h3_forge.note)))
		{
			platform_log("menus: forge lobby: not started: %s", h3_forge.note);
			return FALSE;
		}
		return TRUE;
	case _function_forge_start_live:
		/* (START FORGE on ONLINE, SYSTEM LINK or AD HOC: the live game's
		lobby, everyone in it taken into the Forge game; not live - the
		network not ready, no profile -: said, nothing opened) */
		if (h3_forge.network == _h3_network_offline || !forge_map_available(h3_forge.map))
			return FALSE;
		if (h3_live_forge_start())
		{
			platform_log("menus: forge lobby: START FORGE: the game's lobby, everyone in it into Forge on %s",
				forge_map_name(h3_forge.map));
			return TRUE;
		}
		if (!h3_forge.note[0])
			snprintf(h3_forge.note, sizeof(h3_forge.note), "%s",
				T("The lobby isn't open yet: wait for the\nnetwork, or choose NETWORK: LOCAL."));
		platform_log("menus: forge lobby: START FORGE: the lobby is not live yet");
		return FALSE;
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
		/* (opened again: a live lobby refused tries again) */
		h3_live_retry();
		return TRUE;
	case _function_h3_network_set:
		return h3_mp_network_set(widget);
	case _function_h3_network_focus:
		h3_focus_child(widget, h3_forge.lobby_last ? h3_forge.network : h3_mp.network);
		return TRUE;
	case _function_h3_host_system_link:
		/* (START GAME on SYSTEM LINK: the System Link list, once the profile
		is chosen, gives way to a new game's map list, as Server Setup's
		START does: pc_menu_functions_screen) */
		if (h3_live_start())
			return TRUE;
		menu_functions.create_pending = TRUE;
		menu_functions.create_system_link = TRUE;
		platform_log("menus: h3 mp: host a system link game; the profile, then the game's lobby");
		return TRUE;
	case _function_h3_host_online:
		/* (START GAME on ONLINE: hosted at once with Create Game's settings -
		the settings panel's Play page, GAME OPTIONS - the lobby's GAME and
		MAP, once the profile is chosen: pc_menu_functions_screen) */
		if (!internet_play() && network_connection_wanted() != NETWORK_CONNECTION_ONLINE)
			return TRUE;
		if (h3_live_start())
			return TRUE;
		menu_functions.create_pending = TRUE;
		menu_functions.create_system_link = FALSE;
		platform_log("menus: h3 mp: host an online game (%s, %s players, %s%s); the profile, then the game's lobby",
			setting_get("HALO_NET_LOBBY_NAME", "Halo"), setting_get("HALO_NET_MAX_PLAYERS", "16"),
			!strcmp(setting_get("HALO_NET_HOST_PUBLIC", "true"), "false") ? "private" : "public",
			setting_get("HALO_NET_LOBBY_PASSWORD", "")[0] ? ", a password" : "");
		return TRUE;
	case _function_h3_game_options:
		/* (Square on the lobby: GAME OPTIONS on ONLINE only) */
		if (h3_mp.network == _h3_network_online)
		{
			menu_functions.open_game_options = TRUE;
			platform_log("menus: h3 mp: GAME OPTIONS");
		}
		return TRUE;
	case _function_h3_invite_ask:
		return h3_invite_ask();
	case _function_h3_invite_init:
		h3_invite_initialize();
		return TRUE;
	case _function_h3_invite_act:
		h3_invite_act(widget);
		return TRUE;
	case _function_h3_live_leave:
		/* (B on the lobby) */
		h3_live_close("the host left the lobby");
		return TRUE;
	case _function_h3_browse_system_link:
		/* (BROWSE GAMES on SYSTEM LINK: the System Link list, once the
		profile is chosen, gives way to the Halo 3 LAN browser:
		pc_menu_functions_screen) */
		h3_live_close("the host browses the games");
		menu_functions.create_pending = FALSE;
		menu_functions.browse_lan_pending = TRUE;
		platform_log("menus: h3 mp: browse the %s games; the profile, then the LAN browser", h3_mp.network ==
			_h3_network_online ? "online and system link" : "system link");
		return TRUE;
	case _function_h3_map_set:
		return h3_mp_map_set(widget);
	case _function_h3_map_focus:
		h3_mp_maps_read();
		return TRUE;
	case _function_h3_map_page_up:
	case _function_h3_map_page_down:
		h3_mp.map_page = function_index == _function_h3_map_page_up ? -1 : 1;
		return TRUE;
	case _function_h3_join_system_link:
	case _function_h3_join_split_screen:
		h3_join_initialize(function_index == _function_h3_join_split_screen);
		return TRUE;
	case _function_h3_join_a:
		return h3_join_a(widget, event);
	case _function_h3_join_previous:
	case _function_h3_join_next:
		h3_join_step(widget, event, function_index == _function_h3_join_previous ? -1 : 1);
		return TRUE;
	case _function_h3_join_leave:
		return h3_join_leave(widget, event);
	case _function_h3_pregame_init:
		h3_pregame.said[0] = 0;
		platform_log("menus: h3 pregame: the Halo 3 lobby for the game's ENLISTED PLAYERS");
		/* (START GAME on a live lobby: its countdown begun) */
		h3_live_pregame();
		/* (hosted from the Halo 3 lobby past SELECT PROFILE: out of the
		history, B goes back to the multiplayer lobby, whose choices made
		the game) */
		if (menu_functions.pregame_drop_profile)
		{
			menu_functions.pregame_drop_profile = FALSE;
			ui_widgets_pop_stack(screen_of(widget)->local_player_index);
			platform_log("menus: h3 pregame: SELECT PROFILE out of the history (B: the multiplayer lobby)");
		}
		return TRUE;
	case _function_h3_party_init:
		h3_party_initialize();
		return TRUE;
	case _function_h3_party_act:
		return h3_party_act(widget);
	case _function_h3_party_second:
		return h3_party_second();
	case _function_h3_party_type:
		h3_party_type();
		return TRUE;
	case _function_h3_party_preset:
		h3_party_preset(widget);
		return TRUE;
	case _function_h3_party_option:
		h3_party_option(widget);
		return TRUE;
	case _function_h3_party_page_up:
		h3_party.page = -1;
		return TRUE;
	case _function_h3_party_page_down:
		h3_party.page = 1;
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
	/* (the Halo 3 profile screens: halo3/profiles.xml, customize.xml) */
	case _function_h3_profiles_campaign:
		h3_profiles_initialize(_h3_profiles_campaign);
		return TRUE;
	case _function_h3_profiles_customize:
		h3_profiles_initialize(_h3_profiles_customize);
		return TRUE;
	/* (A on a row: the profile played, or edited, as the screen's mode is -
	the other's handler fails, so that its screen does not open - or on
	CREATE NEW PROFILE its name asked for) */
	case _function_h3_profile_play:
	case _function_h3_profile_edit:
	{
		short mode = function_index == _function_h3_profile_play ? _h3_profiles_campaign : _h3_profiles_customize;
		short item = h3_profiles_item(widget);

		if (h3_profiles.mode != mode)
			return FALSE;
		if (item >= h3_profiles.count)
		{
			h3_profile_create_begin(event ? event->controller_index : NONE);
			return FALSE;
		}
		return mode == _h3_profiles_campaign ? h3_profile_play(item, event ? event->controller_index : NONE) :
			h3_profile_edit(item);
	}
	case _function_h3_profile_create:
		h3_profile_create_begin(event ? event->controller_index : NONE);
		return TRUE;
	case _function_h3_profile_delete_ask:
		return h3_profile_delete_ask();
	case _function_h3_profile_delete:
		h3_profile_delete();
		return TRUE;
	case _function_h3_profile_page_up:
		h3_profiles.page = -1;
		return TRUE;
	case _function_h3_profile_page_down:
		h3_profiles.page = 1;
		return TRUE;
	case _function_h3_custom_next:
		return h3_custom_change(widget, 1);
	case _function_h3_custom_previous:
		return h3_custom_change(widget, -1);
	case _function_h3_custom_save:
		h3_custom_save();
		return TRUE;
	/* (the editor gone without its Circle: nothing saved, as the Xbox's
	edit screen left without SAVE CHANGES) */
	case _function_h3_custom_close:
		h3_custom_spartan_hide();
		if (player_ui_get_edit_player_profile())
			player_ui_end_editing_profile();
		return TRUE;
	case _function_forge_map_pick:
		return h3_forge_map_pick(widget);
	case _function_forge_maps_back:
		return h3_forge_maps_back(widget);
	case _function_forge_variant_page_up:
		h3_forge.variant_page = -1;
		return TRUE;
	case _function_forge_variant_page_down:
		h3_forge.variant_page = 1;
		return TRUE;
	case _function_forge_save:
		h3_forge_save();
		return TRUE;
	case _function_forge_undo:
	case _function_forge_redo:
		forge_undo_request(function_index == _function_forge_redo);
		return TRUE;
	case _function_forge_snap:
		forge_snap_toggle_request();
		return TRUE;
	case _function_forge_property_next:
	case _function_forge_property_previous:
		forge_property_request((short)h3_child_index(widget), function_index == _function_forge_property_next ? 1 : -1);
		return TRUE;
	case _function_forge_close:
		forge_menus_close_request();
		return TRUE;
	case _function_forge_new_round:
		/* (done once the menu has closed: the engine resets a map only in
		a running game) */
		platform_log("menus: forge: new round");
		forge_new_round_request();
		return TRUE;
	case _function_forge_all_ai:
		forge_ai_all_request();
		return TRUE;
	/* (the multiplayer pause's PARTY: on the Vita the settings panel's
	Party page, its chat and quick chat) */
	case _function_h3_mp_pause_party:
#ifdef HALO_VITA
		__atomic_store_n(&halo_settings_panel_request, HALO_SETTINGS_PANEL_PARTY, __ATOMIC_RELEASE);
#endif
		platform_log("menus: h3 mp pause: the Party page asked for");
		return FALSE;
	case _function_h3_mp_settings_init:
		h3_mp_settings_initialize(widget);
		return TRUE;
	case _function_h3_mp_settings_save:
		h3_mp_settings_save();
		return TRUE;
	/* (SETTINGS gone without its Circle: nothing saved) */
	case _function_h3_mp_settings_close:
		if (player_ui_get_edit_player_profile())
			player_ui_end_editing_profile();
		h3_mp_pause.profile_index = NONE;
		return TRUE;
	case _function_forge_clear_dead_ai:
	case _function_forge_clear_all_ai:
		/* (CLEAR ALL AI from its question) */
		platform_log("menus: forge: clear %s AI", function_index == _function_forge_clear_all_ai ? "all" : "dead");
		forge_clear_ai_request(function_index == _function_forge_clear_all_ai);
		return TRUE;
	case _function_forge_pack_toggle:
	{
		short pack = (short)h3_child_index(widget);

		forge_pack_set(pack, !forge_pack_enabled(pack));
		h3_forge.note[0] = 0;
		return TRUE;
	}
	case _function_fileshare_enter:
		/* (the main menu's FILE SHARE: map variants, nothing said yet; back
		from a question or the lobby what was said stays) */
		h3_fileshare.kind = _fileshare_map_variants;
		h3_fileshare.status[0] = 0;
		return TRUE;
	case _function_fileshare_init:
		h3_variants_list(_variants_fileshare, NULL, TRUE);
		h3_mp_gametypes_read();
		h3_fileshare.said[0] = 0;
		return TRUE;
	case _function_fileshare_focus:
		browser_view_reset(&h3_fileshare.view);
		h3_fileshare.chosen = 0;
		h3_fileshare.page = 0;
		return TRUE;
	case _function_fileshare_page_up:
		h3_fileshare.page = -1;
		return TRUE;
	case _function_fileshare_page_down:
		h3_fileshare.page = 1;
		return TRUE;
	case _function_fileshare_kind:
		h3_fileshare.kind = (short)((h3_fileshare.kind + 1) % NUMBER_OF_FILESHARE_KINDS);
		if (h3_fileshare.kind == _fileshare_game_variants)
			h3_mp_gametypes_read();
		browser_view_reset(&h3_fileshare.view);
		h3_fileshare.chosen = 0;
		h3_fileshare.status[0] = 0;
		return TRUE;
	case _function_fileshare_rename:
		h3_fileshare_rename();
		return TRUE;
	case _function_fileshare_ask_delete:
		return h3_fileshare_ask_delete();
	case _function_fileshare_delete:
		h3_fileshare_delete();
		return TRUE;
	case _function_fileshare_open:
		return h3_fileshare_open();
	/* (the Halo 3 MOD MANAGER) */
	case _function_h3_mods_init:
		h3_mods_initialize();
		return TRUE;
	case _function_h3_mods_toggle:
		h3_mods_toggle(widget);
		return TRUE;
	case _function_h3_mods_details:
		/* (the details open for an entry; with none, nothing) */
		return h3_mods_chosen() != NULL;
	case _function_h3_mods_delete_ask:
		return h3_mods_delete_ask();
	case _function_h3_mods_delete:
		h3_mods_delete();
		return TRUE;
	case _function_h3_mods_page_up:
		h3_mods.page = -1;
		return TRUE;
	case _function_h3_mods_page_down:
		h3_mods.page = 1;
		return TRUE;
	/* (the Halo 3 LAN browser: the server browser's functions in its mode,
	its game list the System Link list's, which the game's own "initialize
	net game server list" starts, and Square starts again, after these) */
	case _function_h3_lan_init:
		menu_functions.lan = TRUE;
		menu_functions.status[0] = 0;
		menu_functions.chosen_valid = FALSE;
		menu_functions.page = 0;
		menu_functions.lan_since = system_milliseconds();
		menu_functions.lan_said[0] = 0;
		menu_functions.lan_said_at = 0;
		menu_functions.join_watched = FALSE;
		browser_view_reset(&menu_functions.view);
		/* (Online: the public games too, as the server browser lists them) */
		if (internet_play())
			p2p_lobby_browse(1);
		platform_log("menus: the LAN browser (%s)", config_boolean("network.adhoc") ? "ad hoc" : internet_play() ?
			"system link and online" : "system link");
		return TRUE;
	case _function_h3_lan_dispose:
		menu_functions.lan = FALSE;
		/* (a locked online game's password screen next: its listing kept,
		which the join needs; the server browser's rule) */
		if (!menu_functions.open_password)
			p2p_lobby_browse(0);
		return TRUE;
	case _function_h3_lan_refresh:
		menu_functions.status[0] = 0;
		menu_functions.lan_since = system_milliseconds();
		if (internet_play())
			p2p_lobby_refresh();
		platform_log("menus: the LAN browser looks again");
		return TRUE;
	/* (ad hoc play: AD HOC's and AD HOC CO-OP's rows, the joining dialog) */
	case _function_h3_adhoc_host:
		return h3_adhoc_ready(_h3_adhoc_then_host);
	case _function_h3_adhoc_browse:
		return h3_adhoc_ready(_h3_adhoc_then_browse);
	case _function_h3_adhoc_coop:
		return h3_adhoc_ready(_h3_adhoc_then_coop);
	case _function_h3_adhoc_join:
		h3_adhoc_join();
		return TRUE;
	case _function_h3_adhoc_cancel:
		/* (the joining dialog gone: what was to follow with it, unless the
		main loop is opening it now) */
		if (!h3_adhoc.open_then)
			h3_adhoc.then = _h3_adhoc_then_none;
		h3_adhoc.said[0] = 0;
		return TRUE;
	/* (SELECT PROFILE gone: its card's Spartan let go, unless the editor
	opened from it shows its own already) */
	case _function_h3_profiles_close:
		h3_profiles_spartan(NULL, NULL, NONE);
		return TRUE;
	case _function_h3_friends_init:
		h3_friends_initialize();
		return TRUE;
	case _function_h3_friends_act:
		h3_friends_act(widget);
		return TRUE;
	case _function_h3_friends_second:
		h3_friends_second();
		return TRUE;
	case _function_h3_friends_options_ask:
		return h3_friends_options_ask();
	case _function_h3_friends_option:
		h3_friends_option(widget);
		return TRUE;
	case _function_h3_friends_page_up:
		h3_friends.page = -1;
		return TRUE;
	case _function_h3_friends_page_down:
		h3_friends.page = 1;
		return TRUE;
	case _function_h3_players_init:
		h3_players_initialize();
		return TRUE;
	case _function_h3_players_act:
		return h3_players_act(widget);
	case _function_h3_players_option:
		h3_players_option(widget);
		return TRUE;
	case _function_h3_players_page_up:
		h3_players.page = -1;
		return TRUE;
	case _function_h3_players_page_down:
		h3_players.page = 1;
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
		h3_pick_update(widget, h3_campaign_unlocked(), NONE, "mission_picture", "mission_text");
		h3_missions_why(widget);
		break;
	case _input_h3_difficulties:
		h3_pick_update(widget, 4, NONE, "difficulty_emblem", "difficulty_text");
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
		/* (AD HOC: the PS Vitas' own, none on a PC) */
		h3_pick_update(widget, NUMBER_OF_H3_NETWORKS, config_adhoc_offered() ? NONE : _h3_network_adhoc, "network_picture",
			"network_text");
		break;
	case _input_h3_maps:
		if (widget->type == _ui_widget_type_column_list)
			h3_mp_maps_update(widget);
		break;
	case _input_h3_gametypes:
		if (widget->type == _ui_widget_type_column_list)
			h3_mp_gametypes_update(widget);
		break;
	case _input_h3_campaign_networks:
		h3_pick_update(widget, NUMBER_OF_H3_CAMPAIGN_NETWORKS, config_adhoc_offered() ? NONE : _h3_campaign_adhoc,
			"campnet_picture", "campnet_text");
		break;
	case _input_h3_profiles:
		if (widget->type == _ui_widget_type_column_list)
			h3_profiles_update(widget);
		break;
	case _input_h3_profile_delete_text:
		h3_profile_delete_text_update(widget);
		break;
	case _input_h3_custom:
		if (widget->type == _ui_widget_type_column_list)
			h3_custom_update(widget);
		break;
	case _input_fileshare:
		if (widget->type == _ui_widget_type_column_list)
			h3_fileshare_update(widget);
		break;
	case _input_forge_packs:
		if (widget->type == _ui_widget_type_column_list)
			h3_forge_packs_update(widget);
		break;
	case _input_forge_properties:
		if (widget->type == _ui_widget_type_column_list)
			h3_forge_properties_update(widget);
		break;
	case _input_fileshare_question:
	{
		char text[192];

		snprintf(text, sizeof(text), T("\"%s\" on %s will be deleted.\nThis can't be undone."), h3_fileshare.asked_title,
			h3_variant_map_name(h3_fileshare.asked_map));
		text_show(widget, text);
		break;
	}
	case _input_h3_mods:
		if (widget->type == _ui_widget_type_column_list)
			h3_mods_update(widget);
		break;
	case _input_h3_mods_details:
		h3_mods_details_update(widget);
		break;
	case _input_h3_mods_confirm:
		h3_mods_confirm_update(widget);
		break;
	case _input_h3_adhoc_joining:
		h3_adhoc_joining_update(widget);
		break;
	case _input_h3_network_status:
		h3_network_status_update(widget);
		break;
	case _input_h3_friends:
		if (widget->type == _ui_widget_type_column_list)
			h3_friends_update(widget);
		break;
	case _input_h3_friends_options:
		if (widget->type == _ui_widget_type_column_list)
			h3_friends_options_update(widget);
		break;
	case _input_h3_friends_panel:
		h3_friends_panel_update(widget);
		break;
	case _input_h3_party_plates:
		h3_party_plates_update(widget);
		break;
	case _input_h3_players:
		if (widget->type == _ui_widget_type_column_list)
			h3_players_update(widget);
		break;
	case _input_h3_players_options:
		if (widget->type == _ui_widget_type_column_list)
			h3_players_options_update(widget);
		break;
	case _input_h3_join:
		h3_join_update(widget);
	case _input_h3_pregame:
		h3_pregame_update(widget);
		break;
	case _input_h3_party:
		if (widget->type == _ui_widget_type_column_list)
			h3_party_update(widget);
		break;
	case _input_h3_party_options:
		if (widget->type == _ui_widget_type_column_list)
			h3_party_options_update(widget);
		break;
	case _input_h3_party_chat:
		h3_party_chat_update(widget);
		break;
	case _input_h3_party_quick:
		if (widget->type == _ui_widget_type_column_list)
			h3_party_quick_update(widget);
		break;
	case _input_h3_mp_pause:
		if (widget->type == _ui_widget_type_column_list)
			h3_mp_pause_update(widget);
		break;
	case _input_h3_mp_settings:
		if (widget->type == _ui_widget_type_column_list)
			h3_mp_settings_update(widget);
		break;
	case _input_h3_invite:
		if (widget->type == _ui_widget_type_column_list)
			h3_invite_update(widget);
		break;
	default:
		break;
	}
}

/* (the settings panel's Forge page, Network on: system_link_shortcut.c, for
the Halo CE menus too) a network Forge game of that map and map variant
hosted once the System Link screen's profile is chosen, its lobby (the
game's) next; FALSE with why if the map cannot be */
boolean menu_functions_forge_host(
	short map_index,
	char const *variant_file,
	char *why,
	int why_size)
{
	if (map_index < 0 || map_index >= forge_map_count() || !forge_map_available(map_index))
	{
		snprintf(why, (size_t)why_size, "%s", T("No multiplayer map is in the maps folder."));
		return FALSE;
	}
	h3_forge.map = map_index;
	snprintf(h3_forge.variant_file, sizeof(h3_forge.variant_file), "%s", variant_file ? variant_file : "");
	h3_forge.variant_title[0] = 0;
	h3_forge_variant_check();
	menu_functions.create_pending = TRUE;
	menu_functions.create_system_link = network_connection_running() != NETWORK_CONNECTION_ONLINE;
	menu_functions.create_forge = TRUE;
	platform_log("menus: forge: the settings panel's network Forge game of %s: the profile, then its lobby",
		forge_map_name(map_index));
	return TRUE;
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

		boolean forge = menu_functions.create_forge;

		menu_functions.create_pending = FALSE;
		menu_functions.create_system_link = FALSE;
		menu_functions.create_forge = FALSE;
		if (map_select != NONE && ui_widget_port_start_network_game_server())
		{
			/* (the settings panel's network Forge game: its map and the Forge
			variant, the game a Forge game, its lobby at once - the Halo CE
			menus' pregame screen) */
			if (forge)
			{
				long pregame = tag_loaded(UI_WIDGET_DEFINITION_TAG, pregame_screen);

				if (pregame != NONE && h3_forge_host_choices())
				{
					menu_functions.pregame_drop_profile = menu_functions.profile_shown;
					menu_functions.profile_shown = FALSE;
					platform_log("menus: forge: the network Forge game's server started; the game's lobby");
					return pregame;
				}
				platform_log("menus: forge: the network Forge game could not be hosted: the map list");
				forge_net_host_end();
				return map_select;
			}
			/* (Menu style Halo 3: the lobby's GAME and MAP, chosen there,
			not asked for again: the game's lobby at once) */
			long pregame = tag_loaded(UI_WIDGET_DEFINITION_TAG, pregame_screen);

			if (halo_menu_style_halo3() && pregame != NONE && h3_mp_host_choices())
			{
				menu_functions.pregame_drop_profile = menu_functions.profile_shown;
				menu_functions.profile_shown = FALSE;
				platform_log("menus: %s: the game's server started; the game's lobby", asked);
				return pregame;
			}
			platform_log("menus: %s: the game's server started; the map list", asked);
			return map_select;
		}
		platform_log("menus: %s: the server did not start; the System Link list", asked);
	}
	/* (Menu style Halo CE's SERVER BROWSER: the profile chosen, OpenCE's
	server browser in the System Link list's place, the profile screen
	behind it) */
	if (menu_functions.browser_profile_pending && tag_index == tag_loaded(UI_WIDGET_DEFINITION_TAG, server_list_screen))
	{
		long browser = tag_loaded(UI_WIDGET_DEFINITION_TAG, "pc\\browser\\screen");

		menu_functions.browser_profile_pending = FALSE;
		if (browser != NONE)
		{
			menu_functions.browser_profile_chosen = TRUE;
			platform_log("menus: Server Browser: the profile chosen; the server browser");
			return browser;
		}
		platform_log("menus: no server browser: the System Link list");
	}
	/* (the Halo 3 lobby's BROWSE GAMES on SYSTEM LINK or AD HOC: the Halo 3
	LAN browser, which starts the same search, with the profile screen
	behind it as the System Link list has) */
	if (menu_functions.browse_lan_pending && tag_index == tag_loaded(UI_WIDGET_DEFINITION_TAG, server_list_screen))
	{
		long lan = menu_tags_ingame_screen(lan_screen);

		menu_functions.browse_lan_pending = FALSE;
		if (lan != NONE)
		{
			platform_log("menus: the Halo 3 LAN browser for the System Link list");
			return lan;
		}
		platform_log("menus: no Halo 3 LAN browser: the System Link list");
	}
	return tag_index;
}

/* (menu_tags_screen, Menu style Halo 3) a network game's SELECT PROFILE
about to open with the profile chosen before this run: the screen it goes on
to, the profile set as its Cross sets it (h3_session_profile_join); else the
tag itself */
long pc_menu_functions_profile_skip(
	long tag_index)
{
	long list;

	if (tag_index == NONE || tag_index != tag_loaded(UI_WIDGET_DEFINITION_TAG, profiles_screen))
		return tag_index;
	/* (START GAME on a live lobby: its game's lobby, the player in it
	already) */
	if (h3_live.start_pending && h3_live.live)
	{
		long pregame = tag_loaded(UI_WIDGET_DEFINITION_TAG, pregame_screen);

		if (pregame != NONE)
			return pregame;
		h3_live.start_pending = FALSE;
	}
	list = tag_loaded(UI_WIDGET_DEFINITION_TAG, server_list_screen);
	if (list == NONE || !h3_session_profile_join())
		return tag_index;
	return list;
}

/* (the main loop) the ad hoc group joined from the joining dialog: what was
to follow, opened over it (the dialog keeps no history: B goes back past it,
to the lobby) - the profile screen for START GAME (the map list after it)
or BROWSE GAMES (the LAN browser after it), or co-op hosted and the game's
lobby; the dialog says so when co-op was not hosted */
static void h3_adhoc_go_on(void)
{
	struct widget_instance *active = NULL;
	short then = h3_adhoc.then;
	long index;

	h3_adhoc.open_then = FALSE;
	h3_adhoc.then = _h3_adhoc_then_none;
	for (index = 0; index < 4 && !active; index++)
		active = ui_widget_port_active_screen((short)index);
	if (!active || then == _h3_adhoc_then_none)
		return;
	platform_log("menus: h3 adhoc: in the ad hoc group: %s", then == _h3_adhoc_then_host ? "host a game" :
		then == _h3_adhoc_then_browse ? "browse the games" : "host co-op");
	if (then == _h3_adhoc_then_coop)
	{
		if (!h3_campaign_host(active, NULL) || !ui_widget_load_by_name_or_tag(pregame_screen, NONE, NULL,
			active->local_player_index, active->definition_tag_index, NONE, NONE))
		{
			h3_adhoc.coop_failed = TRUE;
		}
		return;
	}
	if (!transport_network_available())
	{
		platform_log("menus: h3 adhoc: no network after all");
		return;
	}
	if (!h3_adhoc_ready(then))
		return;
	if (!ui_widget_load_by_name_or_tag(profiles_screen, NONE, NULL, active->local_player_index,
		active->definition_tag_index, NONE, NONE))
	{
		menu_functions.create_pending = FALSE;
		menu_functions.browse_lan_pending = FALSE;
		platform_log("menus: h3 adhoc: the profile screen did not open");
	}
}

/* (each frame, the main loop) the screens the functions asked for, opened
outside the widgets' own events */
void pc_menu_functions_update(
	boolean main_menu_loaded)
{
	h3_live_update(main_menu_loaded);
	/* (a join the host's map refused: the games browser again once the
	main menu is up - the Xbox's error over it dismissed -, not the main
	menu; given up after 30 s) */
	if (menu_functions.browser_after_refusal)
	{
		struct widget_instance *active = ui_widget_port_active_screen(0);
		long main_tag = menu_tags_ingame_screen("h3/main/screen");

		if (system_milliseconds() - menu_functions.browser_after_refusal > 30000 || !halo_menu_style_halo3())
			menu_functions.browser_after_refusal = 0;
		else if (main_menu_loaded && active && main_tag != NONE && active->definition_tag_index == main_tag &&
			!global_network_game_client_get())
		{
			/* (as BROWSE GAMES opens it: the profile - chosen already, set at
			once -, then the games browser in the System Link list's place;
			B on it goes back to the main menu) */
			menu_functions.browser_after_refusal = 0;
			menu_functions.create_pending = FALSE;
			menu_functions.browse_lan_pending = TRUE;
			if (ui_widget_load_by_name_or_tag(profiles_screen, NONE, NULL, active->local_player_index,
				active->definition_tag_index, NONE, NONE))
			{
				platform_log("menus: the join was refused: the games browser again");
			}
			else
			{
				menu_functions.browse_lan_pending = FALSE;
				platform_log("menus: the join was refused: the games browser did not open");
			}
		}
	}
	if (main_menu_loaded && menu_functions.open_party)
	{
		menu_functions.open_party = FALSE;
		h3_party_open();
	}
	/* (GAME OPTIONS over the lobby, which B goes back to; internet play
	off, the screen saying how to turn it on) */
	if (main_menu_loaded && menu_functions.open_game_options)
	{
		long options = tag_loaded(UI_WIDGET_DEFINITION_TAG, internet_play() ||
			network_connection_wanted() == NETWORK_CONNECTION_ONLINE ? "pc\\h3\\setup\\screen" : "pc\\h3\\mp\\offline");
		struct widget_instance *active = NULL;
		long index;

		menu_functions.open_game_options = FALSE;
		for (index = 0; index < 4 && !active; index++)
			active = ui_widget_port_active_screen((short)index);
		if (options != NONE && active)
		{
			ui_widget_load_by_name_or_tag(NULL, options, NULL, active->local_player_index, active->definition_tag_index,
				NONE, NONE);
		}
	}
	if (!main_menu_loaded)
	{
		menu_functions.open_party = FALSE;
		menu_functions.open_game_options = FALSE;
		menu_functions.open_system_link = FALSE;
		menu_functions.open_system_link_list = FALSE;
		menu_functions.browser_profile_pending = FALSE;
		menu_functions.browser_profile_chosen = FALSE;
		menu_functions.open_password = FALSE;
		menu_functions.create_pending = FALSE;
		h3_profiles.open_pending = FALSE;
		menu_functions.browse_lan_pending = FALSE;
		h3_adhoc.open_then = FALSE;
		h3_adhoc.then = _h3_adhoc_then_none;
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
	/* (a profile made on the Halo 3 profile screen: the screen choosing it
	opens, the campaign's level select - the Halo 3 lobby - or the editor,
	with the profile screen behind it) */
	if (h3_profiles.open_pending)
	{
		long next = tag_loaded(UI_WIDGET_DEFINITION_TAG, h3_profiles.mode == _h3_profiles_customize ?
			"pc\\h3\\custom\\screen" : "ui\\shell\\main_menu\\solo_level_select\\solo_level_select_screen");
		struct widget_instance *active = NULL;
		long index;

		h3_profiles.open_pending = FALSE;
		for (index = 0; index < 4 && !active; index++)
			active = ui_widget_port_active_screen((short)index);
		if (next != NONE && active)
		{
			ui_widget_load_by_name_or_tag(NULL, next, NULL, active->local_player_index, active->definition_tag_index,
				NONE, NONE);
		}
	}
	/* (a join's watch goes on from here as the screens change) */
	join_watch();
	if (h3_adhoc.open_then)
		h3_adhoc_go_on();
	if (menu_functions.open_system_link)
	{
		menu_functions.open_system_link = FALSE;
		/* (Menu style Halo 3: the Halo 3 games browser in the System Link
		list's place, where the host's game is joined once listed, as A on
		its row: menu_functions_lan_join_listed; no Xbox screen) */
		if (halo_menu_style_halo3())
		{
			menu_functions.create_pending = FALSE;
			menu_functions.browse_lan_pending = TRUE;
		}
		if (!system_link_shortcut_open())
			platform_log("menus: the System Link screen did not open");
	}
	if (menu_functions.open_system_link_list)
	{
		long list = tag_loaded(UI_WIDGET_DEFINITION_TAG, server_list_screen);
		struct widget_instance *active = NULL;
		long index;

		menu_functions.open_system_link_list = FALSE;
		menu_functions.browser_profile_chosen = FALSE;
		for (index = 0; index < 4 && !active; index++)
			active = ui_widget_port_active_screen((short)index);
		/* (over the server browser, which B goes back to) */
		if (list == NONE || !active || !ui_widget_load_by_name_or_tag(NULL, list, NULL, active->local_player_index,
			active->definition_tag_index, NONE, NONE))
		{
			platform_log("menus: the System Link list did not open; the System Link screen");
			if (!system_link_shortcut_open())
				platform_log("menus: the System Link screen did not open");
		}
	}
}
