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
#include "game_chat.h"
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
#include "dedicated_server.h"
#include "network_connection.h"
#include "player_models.h"
#include "game/players.h"
#include "game/game_engine.h"
#include "game/game_engine_playlist.h"
#include "bots.h"
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
#include "infection.h"
#include "map_share.h"
#include "carnage_report.h"
#include "latency_meter.h"
#include "game/game.h"
#include "cutscene/cinematics.h"

#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../vita/include/vita_version.h"

/* the platform layer's */
void platform_log(char const *format, ...);
int setenv(const char *name, const char *value, int overwrite);
unsigned long system_milliseconds(void);
void platform_show_message(char const *title, char const *message);
void platform_ask_question(char const *title, char const *text);
int platform_question_answer(void);

/* the game's (port) */
boolean ui_widget_port_start_network_game_server(void);
boolean ui_widget_port_start_network_game_server_on_map(char const *map_name);
boolean ui_widget_port_cooperative_campaign_host_level(struct widget_instance *widget, struct event_record *event,
	char const *level, short difficulty);
void ui_widget_port_give_focus(struct widget_instance *list, struct widget_instance *child);
boolean system_link_shortcut_open(void);
struct widget_instance *ui_widget_port_active_screen(short local_player_index);
void ui_widget_port_go_back(short screen_index);
boolean ui_widget_port_join_advertised_game(struct widget_instance *list, void const *server, boolean *widget_deleted);
boolean transport_network_available(void);
boolean menu_functions_forge_host(short map_index, char const *variant_file, char *why, int why_size);
/* menu_tags.c's: a screen of the set by its widget's name (NONE: none) */
long menu_tags_ingame_screen(char const *name);
/* (ui_widget.c: a screen held up over a player's view; the pointer) */
struct widget_instance *ui_widget_port_held_screen_show(short local_player_index, long tag_index, short x, short y);
void ui_widget_port_held_screen_hide(short local_player_index);
struct widget_instance *ui_widget_port_held_screen(short local_player_index);
boolean ui_widget_port_pointer(short *x, short *y, boolean *held);
void ui_widget_port_pointer_drag(boolean dragging);
/* (flaming_helmet.c) */
boolean flaming_helmet_player_shown(long player_index);
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
	_function_settings_panel_multiplayer,
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
	_function_h3_maps_back,
	_function_h3_mp_offline_start,
	_function_h3_gametypes_init,
	_function_h3_gametypes_choose,
	_function_h3_gametypes_new,
	_function_h3_gametypes_page_up,
	_function_h3_gametypes_page_down,
	_function_h3_gametype_edit,
	_function_h3_gametype_copy,
	_function_h3_gametype_rename,
	_function_h3_gametype_delete_later,
	_function_h3_gametype_delete_ask,
	_function_h3_gametype_delete,
	_function_h3_gametype_base,
	_function_h3_gametype_next,
	_function_h3_gametype_previous,
	_function_h3_gametype_category,
	_function_h3_gametype_save,
	_function_h3_gametype_save_quit,
	_function_h3_gametype_leave_ask,
	_function_h3_gametype_leave,
	_function_h3_gametype_discard,
	_function_h3_carnage_init,
	_function_h3_carnage_continue,
	_function_h3_carnage_leave_ask,
	_function_h3_carnage_leave,
	_function_h3_carnage_players,
	_function_h3_carnage_page,
	_function_h3_carnage_up,
	_function_h3_carnage_down,
	_function_h3_dialog_ok,
	_function_h3_dialog_yes,
	_function_h3_dialog_no,
	_function_ce_pregame_init,
	_function_ce_variant_next,
	_function_ce_custom,
	_function_h3_scoreboard_act,
	_function_h3_scoreboard_option,
	_function_h3_scoreboard_close,
	_function_h3_scoreboard_page_up,
	_function_h3_scoreboard_page_down,
	/* (Menu style Online: CUSTOMIZATION's tabs, menus/online/customize.xml) */
	_function_online_custom_tab_next,
	_function_online_custom_tab_previous,
	/* (Menu style Online: the main menu's EXIT, after its question) */
	_function_online_exit,
	/* (Menu style Online: the lobbies' SWITCH LOBBY) */
	_function_online_switch_open,
	_function_online_switch_close,
	_function_online_lobby_leave,
	_function_online_switch_multiplayer,
	_function_online_switch_forge,
	_function_online_switch_campaign,
	/* (Menu style Online: CAMPAIGN with the current profile, no SELECT
	PROFILE) */
	_function_h3_profile_current,
	/* (Menu style Online: CUSTOMIZATION's PROFILE tab) */
	_function_online_profile_choose,
	_function_online_profile_delete_ask,
	/* (Menu style Online: the SERVER BROWSER's network tabs and filters) */
	_function_online_browser_network_next,
	_function_online_browser_network_previous,
	_function_online_browser_filter_next,
	_function_online_browser_filter_previous,
	/* (Menu style Online: the lobbies' HOST SETTINGS, menus/online/host.xml) */
	_function_online_host_multiplayer,
	_function_online_host_forge,
	_function_online_host_campaign,
	_function_online_host_next,
	_function_online_host_previous,
	_function_online_host_edit,
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
	_input_h3_gametype_list,
	_input_h3_gametype_actions,
	_input_h3_gametype_question,
	_input_h3_gametype_bases,
	_input_h3_gametype_editor,
	_input_h3_gametype_options,
	_input_h3_carnage,
	_input_h3_dialog,
	_input_ce_lobby,
	_input_ce_profile,
	_input_h3_scoreboard,
	_input_h3_scoreboard_options,
	/* (Menu style Online: the main menu's version, CUSTOMIZATION's tabs) */
	_input_online_version,
	_input_online_custom,
	/* (Menu style Online: the lobbies' switcher and map card) */
	_input_online_switch,
	_input_online_lobby,
	/* (Menu style Online: HOST SETTINGS' rows) */
	_input_online_host,
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
	/* (the PC build: the classic menus' keyboard, virtual_keyboard.c, a
	profile's or a game variant's name) */
	_typing_classic,
	/* (GAME TYPES: the editor's NAME, a copy's name, a new name) */
	_typing_gametype_name,
	_typing_gametype_copy,
	_typing_gametype_rename,
	/* (FRIENDS' FRIEND CODE BACKUP: the file's name, its password, again) */
	_typing_backup_name,
	_typing_backup_password,
	_typing_backup_again,
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
	/* (BOTS: the spinner's index, setup_bots_values) */
	short held_bots;
	/* to be opened from the main loop: the System Link screen, the password
	screen */
	boolean open_system_link;
	/* (FRIENDS' PARTY row) the PARTY screen opened at the next frame */
	boolean open_party;
	/* (the Halo 3 lobby's Square on ONLINE) GAME OPTIONS opened at the next
	frame, over the lobby */
	boolean open_game_options;
	/* (FRIENDS' FRIEND CODE BACKUP) its question opened at the next frame,
	over FRIENDS */
	boolean open_friends_question;
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
/* (the lobby's START GAME on OFFLINE: the local game) asked: split screen's
SELECT PROFILE opened by it, its Cross starting the game; since: the game
set up, waiting for its players (0: none waited for); players_in: since
when they are in; added: the last ask to add them; started: its start asked
for, the level about to load */
static struct
{
	boolean asked;
	boolean started;
	unsigned long started_time;
	unsigned long since;
	unsigned long players_in;
	unsigned long added;
	/* a local game over, the Halo 3 lobby back up: the game let go at the
	next frame (pc_menu_functions_local_lobby_after_game) */
	boolean dispose_pending;
} h3_local;
static boolean h3_mp_local_start(void);
static void h3_mp_local_update(boolean main_menu_loaded);
/* (the live lobby, Menu style Halo 3: h3_live) */
static void h3_live_lobby_show(struct widget_instance *screen);
static boolean h3_live_follow(void);
static void h3_live_close(char const *why);
static boolean h3_live_start(void);
static boolean h3_live_open_now(char const *why);
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

/* (friends_game.c) the profile the nameplate shows: the active one, else
the one chosen, else the last used, else the first; NONE without one. Read
again at most every H3_NAMEPLATE_READ_MILLISECONDS, as the nameplate */
long menus_player_profile_index(void)
{
	h3_nameplate_read();
	return h3_plate.shown;
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
/* (port) with a Halo content folder (mcc_content.h), each mission whose map
it has is MCC's own campaign map, as the mods' (custom_edition_mods.c),
else the Xbox's: played on this machine alone, every one open (they keep
no profile progress), saved in the custom campaign maps' save,
savegame_custom.bin. Looked at as the lobby opens (h3_mcc_levels_find) */
static char const *const h3_mcc_level_paths[NUMBER_OF_SINGLE_PLAYER_LEVELS] =
{
	"mods\\mcc_a10", "mods\\mcc_a30", "mods\\mcc_a50", "mods\\mcc_b30", "mods\\mcc_b40",
	"mods\\mcc_c10", "mods\\mcc_c20", "mods\\mcc_c40", "mods\\mcc_d20", "mods\\mcc_d40",
};
boolean game_state_set_persistent_storage_custom(boolean custom);
boolean custom_edition_cache_playable(char const *map_name);

static boolean h3_mcc_levels[NUMBER_OF_SINGLE_PLAYER_LEVELS];
static boolean h3_mcc_any;

/* which missions the Halo content folder has (as the loader takes them) */
static void h3_mcc_levels_find(void)
{
	short level;

	h3_mcc_any = FALSE;
	for (level = 0; level < NUMBER_OF_SINGLE_PLAYER_LEVELS; level++)
	{
		h3_mcc_levels[level] = halo_content_library_found() && custom_edition_cache_playable(h3_mcc_level_paths[level]);
		h3_mcc_any |= h3_mcc_levels[level];
	}
	if (h3_mcc_any)
		platform_log("menus: h3 campaign: the missions of the Halo content folder are MCC's");
}

static boolean h3_mcc_campaign(void)
{
	return h3_mcc_any;
}

static boolean h3_mcc_level(short level)
{
	return h3_mcc_levels[PIN(level, 0, NUMBER_OF_SINGLE_PLAYER_LEVELS - 1)];
}

/* the mission's level name: MCC's own, else the Xbox level's */
static char const *h3_level_path(short level)
{
	level = (short)PIN(level, 0, NUMBER_OF_SINGLE_PLAYER_LEVELS - 1);
	return h3_mcc_levels[level] ? h3_mcc_level_paths[level] : h3_level_paths[level];
}

/* whether the mission can be played (map_checks.c; MCC's own: found as the
lobby opened), `why` not */
static short h3_mission_check(short level, char *why, long why_size)
{
	if (!h3_mcc_level(level))
		return map_check(h3_level_paths[level], why, why_size);
	if (why && why_size > 0)
		why[0] = 0;
	return _map_check_playable;
}

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
		/* (Ad hoc dialog is a Dev row, the tab hidden until Show dev
		settings is on: both named) */
		snprintf(text, sizeof(text), "%s", T("The PS Vita did not join the ad hoc group.\n"
			"Press A to try again. Or, in the settings\npanel, Game > Show dev settings: On, then\n"
			"Dev > Ad hoc dialog: another way."));
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
/* (a party plays online: below, with the live lobby) */
static boolean h3_party_online(char const *why);
static boolean h3_party_refuses(boolean online);

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

	/* (MCC's own: all ten, which keep no profile progress) */
	if (h3_mcc_campaign())
		return NUMBER_OF_SINGLE_PLAYER_LEVELS;
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
		if (!_stricmp(map, h3_level_paths[level]) || !_stricmp(map, h3_mcc_level_paths[level]))
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
	short unlocked;

	h3_mcc_levels_find();
	unlocked = h3_campaign_unlocked();

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

		/* (MCC's own: the custom campaign maps' save, which an MCC mission
		on its difficulty resumes) */
		boolean previous = game_state_set_persistent_storage_custom(h3_mcc_campaign());

		h3_campaign.saved_map[0] = 0;
		h3_campaign.saved = game_state_test_persistent_storage(h3_campaign.saved_map, &h3_campaign.saved_difficulty,
			&corrupted) && !corrupted;
		game_state_set_persistent_storage_custom(previous);
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
	playable = h3_mission_check(focused, why, sizeof(why)) == _map_check_playable;
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
	else if (h3_mcc_level(h3_campaign.level))
		snprintf(text, sizeof(text), "%s", T("MCC files, on this PS Vita alone. Sound effects only for now."));
	else
		snprintf(text, sizeof(text), "%s", T("This party is local to this PS Vita."));
	/* (the mission's level cannot be played: why; START GAME then says it
	again as the map fails to load, back at the main menu) */
	{
		char why[160];

		if (h3_mission_check(h3_campaign.level, why, sizeof(why)) != _map_check_playable)
			snprintf(text, sizeof(text), "%s", why);
		else if (h3_mcc_level(h3_campaign.level) && h3_campaign.network != _h3_campaign_local)
			snprintf(text, sizeof(text), "%s", T("MCC missions play on this machine alone: LOCAL."));
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
			!_stricmp(h3_campaign.saved_map, h3_level_path(h3_campaign.level));
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

	/* (in a party: no SYSTEM LINK or AD HOC co-op, which would leave
	internet play and the party with it; LOCAL is this machine's alone) */
	if (h3_party_refuses(network == _h3_campaign_online || network == _h3_campaign_local))
		return TRUE;
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
	/* (MCC's own missions are never hosted, as no mod's map is) */
	if (h3_mcc_level(h3_campaign.level))
	{
		platform_log("menus: h3 campaign: co-op not hosted: MCC missions play on this machine alone");
		return FALSE;
	}
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
	/* the main menu's ring flown on this load of ui.map (the camera is the
	ring's then; not before it: a lobby opened as ui.map loads - the live
	lobby after a game's carnage report - has the last map's camera, or
	none, which kept and cut back to left the main menu in the void,
	insider.21) */
	boolean ring_flown;
	/* the live backdrop's camera shown now (shown_valid: one is), so that
	a screen whose camera is elsewhere fades through black to it: the whole
	screen faded out (menu_functions_fade_draw, over the menus) with the old
	camera, the new one put in once it is black (fade_held, until
	fade_until: vita_host_time_us), and faded in (fade_phase, from
	fade_start) */
	boolean shown_valid;
	char shown_point[32];
	real shown_camera[5];
	real shown_field_of_view;
	boolean fade_held;
	unsigned long long fade_until;
	short fade_phase;
	unsigned long long fade_start;
	/* a screen opened at black (menu_functions_screen_open_deferred): what
	to open, and its scene then put in at once (the fade in after) */
	boolean open_pending;
	long open_tag;
	short open_local_player;
	long open_invoking_tag;
	long open_focus_parent_tag;
	short open_focus_index;
	boolean open_back;
	boolean opening;
	boolean cut_next;
	/* (opened at black: held black until its scene is made, then in) */
	boolean loading;
	unsigned long long loading_since;
} h3_scenes;

static void h3_scene_keep_ring(void);

static boolean h3_scenes_enabled(void)
{
	char const *setting = getenv("HALO_MENU_SCENES");

	if (setting && *setting)
		return strcmp(setting, "0") && _stricmp(setting, "false");
	return config_boolean("menus.scenes");
}

int menu_backdrop_active(void);

/* the scene= of the menus' widget this screen was built from (its tag is
"pc\<name>", the name's '/' made '\'), NULL for none; with the live
backdrop behind the menus (menu_backdrop.c), its backdrop= (and
backdrop_object=) instead, NULL for none: a screen with none keeps the
camera where it is */
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

		if ((!widget->scene && !widget->backdrop) || !a)
			continue;
		while (*a && *b && (*a == *b || (*a == '/' && *b == '\\')))
			a++, b++;
		if (!*a && !*b)
		{
			snprintf(name, size, "%s", widget->name);
			if (menu_backdrop_active())
			{
				if (objects)
					*objects = widget->backdrop_object;
				return widget->backdrop;
			}
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

	/* (the live backdrop, a lit level, not under the Online screens' deep
	blue haze, which was made for ui.map's dark ring: menu_backdrop.c) */
	{
		struct widget_instance *haze = named(screen, "haze");

		if (haze && (!strcmp(tag_get_name(haze->definition_tag_index), "pc\\online\\main\\haze") ||
			!strcmp(tag_get_name(haze->definition_tag_index), "pc\\h3\\main\\haze")))
			widget_instance_set_visibility_recursive(haze, !menu_backdrop_active());
	}
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

/* (menu_backdrop.c, as the live backdrop comes or goes) the screen open
now asks for its scene again: the backdrop's camera, or ui.map's */
void menu_functions_scenes_refresh(void)
{
	struct widget_instance *active = NULL;
	short index;

	for (index = 0; index < 4 && !active; index++)
		active = ui_widget_port_active_screen(index);
	if (active)
		h3_scene_request(active);
}

/* a camera point's orientation for a camera looking along forward, up its
up: camera_set reads a point's yaw, pitch and roll as the game's euler
angles (matrix4x3_rotation_from_angles: its pitch turns about the world's
y, not the camera's right, and its roll follows), not as a heading and an
elevation: a scene's "yaw pitch" (heading, elevation above level, the
camera saves' forward) written as they are looks level, and leaned, along
the world's y (a camera at yaw 90 with pitch 25 looked level and rolled
25 degrees); turned into the euler angles of that forward and a level up,
the camera looks where the scene says (and where menu_scene_objects.c
places its characters) */
static void h3_scene_point_orient(struct scenario_cutscene_camera_point *point, real_vector3d const *forward,
	real_vector3d const *up)
{
	/* (the matrix's rows forward, left (up x forward) and up:
	matrix4x3_rotation_to_angles) */
	real left_i = up->j * forward->k - up->k * forward->j;
	real cosine_pitch;

	point->orientation.pitch = (real)-asin(PIN(up->i, -1.f, 1.f));
	cosine_pitch = (real)cos(point->orientation.pitch);
	if (cosine_pitch > 0.0001f)
	{
		point->orientation.roll = (real)atan2(-up->j / cosine_pitch, up->k / cosine_pitch);
		point->orientation.yaw = (real)atan2(-left_i / cosine_pitch, forward->i / cosine_pitch);
	}
	else
	{
		point->orientation.roll = 0.f;
		point->orientation.yaw = (real)atan2(forward->j, forward->i);
	}
}

/* the same for a heading and an elevation (radians), its up level */
static void h3_scene_point_orient_angles(struct scenario_cutscene_camera_point *point, real yaw, real pitch)
{
	real_vector3d forward, up;

	forward.i = (real)(cos(yaw) * cos(pitch));
	forward.j = (real)(sin(yaw) * cos(pitch));
	forward.k = (real)sin(pitch);
	up.i = (real)(-sin(pitch) * cos(yaw));
	up.j = (real)(-sin(pitch) * sin(yaw));
	up.k = (real)cos(pitch);
	h3_scene_point_orient(point, &forward, &up);
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

/* (menu_backdrop.c) whether the lobbies' packs asked for at the main
menu's rest are all loaded or given up (or none is asked for: the
prebuild off), so that a pack wanted after them does not take their build's
room in the tag cache */
int menu_functions_prebuild_settled(void)
{
	unsigned long scene_packs_map_epoch(void);
	char const *setting = getenv("HALO_MENU_PREBUILD");
	short index;

	if (setting && *setting && (!strcmp(setting, "0") || !_stricmp(setting, "false")))
		return TRUE;
	if (h3_prebuild.epoch != scene_packs_map_epoch() || !h3_prebuild.asked)
		return FALSE;
	for (index = 0; index < h3_prebuild.count; index++)
	{
		if (!h3_prebuild.preloaded[index])
			return FALSE;
	}
	return TRUE;
}

/* the fade between two of the live backdrop's spots: the whole screen,
the menus with the backdrop, out to black over H3_SCENE_FADE_OUT_US (the
old camera behind), the new camera put in, then in over
H3_SCENE_FADE_IN_US - each frame's black a full-screen quad over
everything (menu_functions_fade_draw, interface.c's overlays: draw_quad,
the letterbox's, no shader of its own), eased, at the frame rate, not the
ticks'. A screen opened from the menus whose camera is elsewhere is not
opened at once (menu_functions_screen_open_deferred, B's too): the old
screen stays and fades out, the new one is opened at black, its scene put
in and made (packs, textures, poses: menu_scene_objects_ready), black held
till then, and only then faded in; the pad is let go meanwhile. HALO_MENU_FADE=0: cut at once, as before */
#define H3_SCENE_FADE_OUT_US 250000ULL
#define H3_SCENE_FADE_IN_US 350000ULL
/* (a screen opened at black: black held at least this long - a frame or
two drawn with it, the camera cut -, and at most this long for its scene) */
#define H3_SCENE_LOAD_LEAST_US 60000ULL
#define H3_SCENE_LOAD_MOST_US 6000000ULL
int menu_scene_objects_ready(void);
void ui_widget_port_focus_child_by_index(long focused_child_parent_widget_tag, struct widget_instance *widget,
	short focused_child_index);

enum
{
	_h3_scene_fade_none,
	_h3_scene_fade_out,
	_h3_scene_fade_in,
};

static boolean h3_scene_fade_enabled(void)
{
	char const *setting = getenv("HALO_MENU_FADE");

	return !(setting && *setting && (!strcmp(setting, "0") || !_stricmp(setting, "false")));
}

/* whether the scene taken goes to another of the backdrop's spots than the
one shown (then it fades) */
static boolean h3_scene_moves_backdrop(void)
{
	short index;

	if (!h3_scenes.shown_valid || h3_scenes.ring || !h3_scenes.has_camera || !menu_backdrop_active() ||
		!h3_scene_fade_enabled())
	{
		return FALSE;
	}
	if (strcmp(h3_scenes.point, h3_scenes.shown_point) || h3_scenes.field_of_view != h3_scenes.shown_field_of_view)
		return TRUE;
	for (index = 0; index < 5; index++)
	{
		if (h3_scenes.camera[index] != h3_scenes.shown_camera[index])
			return TRUE;
	}
	return FALSE;
}

static void h3_scene_apply(void);
real menu_functions_fade_alpha(void);

/* (ui_widget.c, a screen about to open) a screen whose live backdrop's
camera is elsewhere than the one shown: not opened now - the old screen
stays and fades to black, and it opens at black (menu_functions_scenes_update),
its camera put in then, and fades in: no frame of it before the black.
One at a time: another asked for meanwhile is let go (no double opens). TRUE
when it waits */
int menu_functions_screen_open_deferred(long tag_index, short local_player_index, long invoking_widget_tag,
	long focused_child_parent_widget_tag, short focused_child_index);

/* (ui_widget.c, B: back to the screen behind) the same, the screen behind
opened at black with the focus it had */
int menu_functions_screen_back_deferred(long tag_index, short local_player_index, long focused_child_parent_widget_tag,
	short focused_child_index)
{
	if (!menu_functions_screen_open_deferred(tag_index, local_player_index, NONE, focused_child_parent_widget_tag,
		focused_child_index))
	{
		return FALSE;
	}
	h3_scenes.open_back = TRUE;
	return TRUE;
}

/* (ui_widget.c's events) the pad let go while a screen opens at black */
int menu_functions_fade_input_held(void)
{
	return h3_scenes.open_pending || h3_scenes.loading;
}

int menu_functions_screen_open_deferred(long tag_index, short local_player_index, long invoking_widget_tag,
	long focused_child_parent_widget_tag, short focused_child_index)
{
	struct halo_menus const *menus;
	char const *tag_name, *backdrop = NULL;
	char point[32];
	float values[7];
	int count, used = 0;
	long index;

	if (h3_scenes.opening || !halo_menu_style_halo3() || !menu_backdrop_active() || !halo_main_menu_loaded() ||
		!h3_scene_fade_enabled())
	{
		return FALSE;
	}
	if (h3_scenes.open_pending)
		return TRUE;
	if (!h3_scenes.shown_valid || !(menus = halo_menus_load()) ||
		!(tag_name = tag_get_name(tag_index)) || strncmp(tag_name, "pc\\", 3))
	{
		return FALSE;
	}
	tag_name += 3;
	for (index = 0; index < menus->widget_count && !backdrop; index++)
	{
		struct halo_menu_widget const *widget = &menus->widgets[index];
		char const *a = widget->name, *b = tag_name;

		if (!widget->backdrop || !a)
			continue;
		while (*a && *b && (*a == *b || (*a == '/' && *b == '\\')))
			a++, b++;
		if (!*a && !*b)
			backdrop = widget->backdrop;
	}
	if (!backdrop || sscanf(backdrop, "%31s%n", point, &used) != 1)
		return FALSE;
	count = sscanf(backdrop + used, "%f %f %f %f %f %f %f", &values[0], &values[1], &values[2], &values[3], &values[4],
		&values[5], &values[6]);
	if (count >= 5 && !strcmp(point, h3_scenes.shown_point) && values[0] == h3_scenes.shown_camera[0] &&
		values[1] == h3_scenes.shown_camera[1] && values[2] == h3_scenes.shown_camera[2] &&
		values[3] == h3_scenes.shown_camera[3] && values[4] == h3_scenes.shown_camera[4])
	{
		return FALSE;
	}
	h3_scenes.open_pending = TRUE;
	h3_scenes.open_back = FALSE;
	h3_scenes.open_tag = tag_index;
	h3_scenes.open_local_player = local_player_index;
	h3_scenes.open_invoking_tag = invoking_widget_tag;
	h3_scenes.open_focus_parent_tag = focused_child_parent_widget_tag;
	h3_scenes.open_focus_index = focused_child_index;
	{
		real black = menu_functions_fade_alpha();

		h3_scenes.fade_phase = _h3_scene_fade_out;
		h3_scenes.fade_start = vita_host_time_us() - (unsigned long long)(black * (real)H3_SCENE_FADE_OUT_US);
		h3_scenes.fade_until = h3_scenes.fade_start + H3_SCENE_FADE_OUT_US;
	}
	platform_log("menus: h3 scene: %s opens at black (the old screen fading out)", tag_get_name(tag_index));
	return TRUE;
}

/* how black the screen is now, 0 to 1 (eased: smoothstep) */
real menu_functions_fade_alpha(void)
{
	unsigned long long now = vita_host_time_us();
	real amount;

	if (h3_scenes.fade_phase == _h3_scene_fade_none)
		return 0.f;
	if (!halo_main_menu_loaded())
	{
		h3_scenes.fade_phase = _h3_scene_fade_none;
		return 0.f;
	}
	if (h3_scenes.fade_phase == _h3_scene_fade_out && h3_scenes.loading)
		return 1.f;
	if (h3_scenes.fade_phase == _h3_scene_fade_out)
	{
		amount = now <= h3_scenes.fade_start ? 0.f : (real)(now - h3_scenes.fade_start) / (real)H3_SCENE_FADE_OUT_US;
		amount = PIN(amount, 0.f, 1.f);
	}
	else
	{
		amount = now <= h3_scenes.fade_start ? 0.f : (real)(now - h3_scenes.fade_start) / (real)H3_SCENE_FADE_IN_US;
		if (amount >= 1.f)
		{
			h3_scenes.fade_phase = _h3_scene_fade_none;
			return 0.f;
		}
		amount = 1.f - PIN(amount, 0.f, 1.f);
	}
	return amount * amount * (3.f - 2.f * amount);
}

/* (interface.c's full-screen overlays, last: over the 3D and the menus)
the fade's black */
void menu_functions_fade_draw(void)
{
	void draw_quad(rectangle2d *rectangle, pixel32 color);
	real alpha = menu_functions_fade_alpha();
	rectangle2d bounds;
	unsigned long level;

	if (alpha <= 0.f)
		return;
	level = (unsigned long)(alpha * 255.f + 0.5f);
	if (level > 255)
		level = 255;
	/* (well past the screen on every side: the interface's coordinates are
	the 4:3 menus' on a wide screen, the display's edges outside them) */
	bounds.x0 = bounds.y0 = -2048;
	bounds.x1 = bounds.y1 = 4096;
	draw_quad(&bounds, (pixel32)(level << 24));
}

void menu_functions_scenes_update(void)
{
	boolean taken;

	h3_prebuild_update();

	/* (Menu style Halo CE chosen, or the scenes turned off, while a lobby's
	camera holds: the ring's flight back, as no Halo 3 main menu will ask) */
	if (h3_scenes.ring_asleep && (!halo_menu_style_halo3() || !h3_scenes_enabled()) && halo_main_menu_loaded())
	{
		main_request_host_command_from("(wake ring_loop)", "menu scene");
		h3_scenes.ring_asleep = FALSE;
		platform_log("menus: h3 scene: the scenes are off now: the ring's own flight");
	}
	taken = __atomic_exchange_n(&h3_scenes.pending, 0, __ATOMIC_ACQUIRE);
	/* (a screen to open at black: the old one faded out under it, now
	opened - its scene then put in at once and faded in) */
	if (h3_scenes.open_pending)
	{
		if (!halo_main_menu_loaded())
		{
			h3_scenes.open_pending = FALSE;
			h3_scenes.loading = FALSE;
			h3_scenes.fade_phase = _h3_scene_fade_none;
			return;
		}
		if (vita_host_time_us() < h3_scenes.fade_until)
			return;
		struct widget_instance *opened;

		h3_scenes.open_pending = FALSE;
		h3_scenes.cut_next = TRUE;
		h3_scenes.opening = TRUE;
		opened = ui_widget_load_by_name_or_tag(NULL, h3_scenes.open_tag, NULL, h3_scenes.open_local_player,
			h3_scenes.open_invoking_tag, h3_scenes.open_back ? NONE : h3_scenes.open_focus_parent_tag,
			h3_scenes.open_back ? NONE : h3_scenes.open_focus_index);
		if (!opened)
			platform_log("menus: h3 scene: the screen faded to did not open");
		else if (h3_scenes.open_back)
		{
			platform_log("menus: h3 scene: back, focus %d of %s", h3_scenes.open_focus_index,
				h3_scenes.open_focus_parent_tag != NONE ? tag_get_name(h3_scenes.open_focus_parent_tag) : "none");
			ui_widget_port_focus_child_by_index(h3_scenes.open_focus_parent_tag, opened, h3_scenes.open_focus_index);
		}
		h3_scenes.opening = FALSE;
		/* (its scene, asked for as it was made: at once; black held while
		it is made - packs, textures, poses -, then the fade in) */
		taken = __atomic_exchange_n(&h3_scenes.pending, 0, __ATOMIC_ACQUIRE) || taken;
		if (taken)
		{
			h3_scene_apply();
			h3_scenes.cut_next = FALSE;
		}
		h3_scenes.loading = TRUE;
		h3_scenes.loading_since = vita_host_time_us();
		return;
	}
	/* (black held till the screen opened at black has its scene made) */
	if (h3_scenes.loading)
	{
		unsigned long long now = vita_host_time_us();

		if (taken || h3_scenes.cut_next)
		{
			h3_scenes.cut_next = FALSE;
			h3_scene_apply();
			h3_scenes.loading_since = now;
			return;
		}
		if (halo_main_menu_loaded() && now - h3_scenes.loading_since < H3_SCENE_LOAD_MOST_US &&
			(now - h3_scenes.loading_since < H3_SCENE_LOAD_LEAST_US || !menu_scene_objects_ready()))
		{
			return;
		}
		platform_log("menus: h3 scene: %s ready at black after %.1f ms, fading in", h3_scenes.screen,
			(double)(now - h3_scenes.loading_since) / 1000.0);
		h3_scenes.loading = FALSE;
		h3_scenes.fade_phase = _h3_scene_fade_in;
		h3_scenes.fade_start = now;
		return;
	}
	/* (fading out: the new camera put in once black - the latest screen's,
	one asked for meanwhile replacing it -, then faded in) */
	if (h3_scenes.fade_held)
	{
		if (!halo_main_menu_loaded())
		{
			h3_scenes.fade_held = FALSE;
			h3_scenes.fade_phase = _h3_scene_fade_none;
			h3_scenes.shown_valid = FALSE;
			return;
		}
		if (vita_host_time_us() < h3_scenes.fade_until)
			return;
		h3_scenes.fade_held = FALSE;
		h3_scene_apply();
		h3_scenes.fade_phase = _h3_scene_fade_in;
		h3_scenes.fade_start = vita_host_time_us();
		return;
	}
	if (!taken)
		return;
	/* (the main menu gone meanwhile: a level loading, a network game's
	map; a network game's lobby at the main menu keeps its scenes) */
	if (!halo_main_menu_loaded())
	{
		h3_scenes.shown_valid = FALSE;
		return;
	}
	if (h3_scenes.cut_next)
	{
		h3_scenes.cut_next = FALSE;
		h3_scene_apply();
		return;
	}
	if (h3_scene_moves_backdrop())
	{
		unsigned long long now = vita_host_time_us();
		real black = menu_functions_fade_alpha();

		/* (from the black there is: a fade in under way turned back) */
		h3_scenes.fade_held = TRUE;
		h3_scenes.fade_phase = _h3_scene_fade_out;
		h3_scenes.fade_start = now - (unsigned long long)(black * (real)H3_SCENE_FADE_OUT_US);
		h3_scenes.fade_until = h3_scenes.fade_start + H3_SCENE_FADE_OUT_US;
		platform_log("menus: h3 scene: %s: fading the backdrop out to its new spot", h3_scenes.screen);
		return;
	}
	h3_scene_apply();
}

/* the scene taken put in: ui.map's ring flight, or its camera written into
its point and cut to, with who stands there */
static void h3_scene_apply(void)
{
	struct scenario_cutscene_camera_point *point;
	char command[96];

	h3_scenes.shown_valid = FALSE;
	if (h3_scenes.ring)
	{
		if (h3_scenes.ring_asleep)
		{
			if (h3_scenes.ring_kept)
				main_request_host_command_from("(camera_set settings 0)", "menu scene");
			main_request_host_command_from("(wake ring_loop)", "menu scene");
		}
		h3_scenes.ring_asleep = FALSE;
		h3_scenes.ring_flown = TRUE;
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
		h3_scene_point_orient_angles(point, h3_scenes.camera[3] * (real)(3.14159265358979 / 180.0),
			h3_scenes.camera[4] * (real)(3.14159265358979 / 180.0));
		/* (camera_set reads it: 0 for the game's own) */
		point->field_of_view = h3_scenes.field_of_view * (real)(3.14159265358979 / 180.0);
	}
	/* (leaving the ring's flight: where it was kept, to come back to) */
	if (!h3_scenes.ring_asleep)
		h3_scene_keep_ring();
	/* (who stands there: menu_scene_objects.c, which makes them before the
	tick) */
	{
		/* (the characters in the camera's frame: its heading and elevation) */
		real yaw = h3_scenes.camera[3] * (real)(3.14159265358979 / 180.0);
		real pitch = h3_scenes.camera[4] * (real)(3.14159265358979 / 180.0);

		if (!h3_scenes.has_camera)
		{
			real_vector3d forward, up;

			vectors3d_from_euler_angles3d(&forward, &up, &point->orientation);
			yaw = (real)atan2(forward.j, forward.i);
			pitch = (real)atan2(forward.k, sqrt(forward.i * forward.i + forward.j * forward.j));
		}
		menu_scene_objects_want(h3_scenes.screen, h3_scenes.has_objects ? h3_scenes.objects : NULL, TRUE,
			point->position.x, point->position.y, point->position.z, yaw, pitch);
	}
	main_request_host_command_from("(sleep -1 ring_loop)", "menu scene");
	snprintf(command, sizeof(command), "(camera_set %s %ld)", h3_scenes.point, h3_scenes.ticks);
	main_request_host_command_from(command, "menu scene");
	/* (the backdrop's spot shown now: another screen's elsewhere fades) */
	if (h3_scenes.has_camera && menu_backdrop_active())
	{
		h3_scenes.shown_valid = TRUE;
		snprintf(h3_scenes.shown_point, sizeof(h3_scenes.shown_point), "%s", h3_scenes.point);
		memcpy(h3_scenes.shown_camera, h3_scenes.camera, sizeof(h3_scenes.shown_camera));
		h3_scenes.shown_field_of_view = h3_scenes.field_of_view;
	}
	h3_scenes.ring_asleep = TRUE;
	{
		char field_of_view[40] = "";

		if (h3_scenes.has_camera && h3_scenes.field_of_view != 0.f)
			snprintf(field_of_view, sizeof(field_of_view), ", field of view %.0f", h3_scenes.field_of_view);
		platform_log("menus: h3 scene: %s: camera point %s at %.2f %.2f %.2f, yaw %.1f pitch %.1f, over %ld ticks%s",
			h3_scenes.screen, h3_scenes.point, point->position.x, point->position.y, point->position.z,
			h3_scenes.has_camera ? h3_scenes.camera[3] : point->orientation.yaw * (real)(180.0 / 3.14159265358979),
			h3_scenes.has_camera ? h3_scenes.camera[4] : point->orientation.pitch * (real)(180.0 / 3.14159265358979),
			h3_scenes.ticks, field_of_view);
	}
}

/* the camera as the ring's flight left it, in ui.map's settings point (no
screen's scene names it) */
static void h3_scene_keep_ring(void)
{
	struct scenario_cutscene_camera_point *point = h3_scene_point("settings");
	struct observer_result const *camera = observer_get_camera(0);

	h3_scenes.ring_kept = FALSE;
	if (!point || !camera)
		return;
	/* (only the ring's own camera: flown on this map, in range, looking
	somewhere; else nothing kept, and the main menu wakes the ring's flight
	from the lobby's camera) */
	if (!h3_scenes.ring_flown || !(camera->position.x > -5000.f && camera->position.x < 5000.f) ||
		!(camera->position.y > -5000.f && camera->position.y < 5000.f) ||
		!(camera->position.z > -5000.f && camera->position.z < 5000.f) ||
		!(camera->forward.i * camera->forward.i + camera->forward.j * camera->forward.j +
			camera->forward.k * camera->forward.k > 0.01f))
	{
		platform_log("menus: h3 scene: the ring's camera not kept (%s: %.2f %.2f %.2f)", h3_scenes.ring_flown ?
			"out of range" : "the ring not flown on this map yet", camera->position.x, camera->position.y,
			camera->position.z);
		return;
	}
	point->position = camera->position;
	h3_scene_point_orient(point, &camera->forward, &camera->up);
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
	/* the note when it is START FORGE's "Still building <pack> (N%)": kept
	beside it so the lobby's update knows the note is that one and redraws
	its number as the pack builds, and clears it when the packs are done
	(insider.18: it stood on the number of the press until X again) */
	char note_building[FORGE_STORE_WHY_SIZE];
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

/* START FORGE refused for a pack being built: the note says which and how
far (h3_forge_lobby_update keeps its number moving) */
static void h3_forge_note_building(char const *label, long progress)
{
	snprintf(h3_forge.note, sizeof(h3_forge.note),
		T("Still building %s (%ld%%).\nSTART FORGE when it is done, or\nturn it off in EXTRA OBJECTS."), label, progress);
	snprintf(h3_forge.note_building, sizeof(h3_forge.note_building), "%s", h3_forge.note);
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

		/* (START FORGE's "Still building": its number kept moving each
		frame, the note gone once the packs are built - not the number of
		the press standing until the next) */
		if (h3_forge.note[0] && !strcmp(h3_forge.note, h3_forge.note_building))
		{
			if (forge_packs_building(label, sizeof(label), &progress))
				h3_forge_note_building(label, progress);
			else
				h3_forge.note[0] = 0;
		}
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
	/* (a network game's joiner has no solo map name: the level loaded's) */
	if (!name[0])
	{
		static char level[64];

		forge_level_name(level, (short)sizeof(level));
		name = level;
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
	char const *controls;
	char text[640];

#ifdef HALO_VITA
	controls = T("Fly: left stick. Up: Cross. Down: D-pad down.\nFaster: hold L. Look: right stick.\n"
		"Grab, drop: R. Copy: Circle. Delete: Triangle.\nObjects: Square. Holding one: its properties.\n"
		"Rotate: hold L + right stick; with R: roll.\nL + left stick: distance. L + D-pad right: snap.\n"
		"Upright: D-pad left. Speed: D-pad right.\nSpartan: Select + D-pad up.");
#elif defined(H3_BUTTON_ICONS)
	switch (halo_button_icons_shown())
	{
	case _halo_button_icons_xbox:
		controls = T("Fly: left stick. Up: A. Down: left stick click.\nFaster: hold LT. Look: right stick.\n"
			"Grab, drop: RT. Copy: B. Delete: Y.\nObjects: X. Holding one: its properties.\n"
			"Rotate: hold LT + right stick; with RT: roll.\nLT + left stick: distance. LT + LB: snap.\n"
			"Upright: RB. Speed: LB.\nSpartan: Back + right stick click.");
		break;
	case _halo_button_icons_playstation:
		controls = T("Fly: left stick. Up: Cross. Down: L3.\nFaster: hold L2. Look: right stick.\n"
			"Grab, drop: R2. Copy: Circle. Delete: Triangle.\nObjects: Square. Holding one: its properties.\n"
			"Rotate: hold L2 + right stick; with R2: roll.\nL2 + left stick: distance. L2 + L1: snap.\n"
			"Upright: R1. Speed: L1.\nSpartan: Share + R3.");
		break;
	default:
		controls = T("Fly: W A S D. Up: Space. Down: C.\nFaster: Shift. Look: mouse.\n"
			"Grab, drop: left button. Copy: F. Delete: Del.\nObjects: E. Holding one, E: its properties.\n"
			"Rotate: hold right button + mouse; + left: roll.\nDistance: wheel, or W S. Snap: right button + Q.\n"
			"Upright: X. Speed: Q.\nSpartan: Tab, or F1 + Z.");
		break;
	}
#else
	controls = T("Fly: W A S D. Up: Space. Down: C.\nFaster: Shift. Look: mouse.\n"
		"Grab, drop: left button. Copy: F. Delete: Del.\nObjects: E. Holding one, E: its properties.\n"
		"Rotate: hold right button + mouse; + left: roll.\nDistance: wheel, or W S. Snap: right button + Q.\n"
		"Upright: X. Speed: Q.\nSpartan: Tab, or F1 + Z.");
#endif
	/* (and a character's RESPAWN, Oct 22 2026: dead, it comes back; the
	player spawn points, the browser's SPAWNING) */
	snprintf(text, sizeof(text), "%s\n%s\n%s", controls, T("AI respawn: a character's properties, RESPAWN."),
		T("Spawn points: Objects, SPAWNING. Held: team, game type."));
	text_show(widget, text);
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
			char after;

			/* (the byte after the word is read only when the text holds the word: past a shorter text's
			end it was a read beyond the string) */
			if (strncmp(text, word, word_length))
				continue;
			after = text[word_length];
			if (!isalpha((unsigned char)after) && !(after & 0x80))
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
	/* the map's saved map variant played (forge_files.h: its file and
	title; none when variant_file is empty), of the Xbox's map variant_map
	(its index); the map picker showing a map's variants (Halo 3's: the map
	itself first, then each of its map variants), that map's row and the
	variants' rows (they scroll) */
	char variant_file[FORGE_FILE_NAME_SIZE];
	char variant_title[FORGE_FILE_TITLE_SIZE];
	short variant_map;
	boolean picking_variant;
	short pick_index;
	boolean pick_refocus;
	struct browser_view variant_view;
	int variant_chosen;
	char variant_said[192];
} h3_mp;

/* (the map picker's variants: after the Forge lobby's, whose lists they
share) */
static boolean h3_mp_variants_offer(long index);
static void h3_mp_variants_update(struct widget_instance *list);
static boolean h3_mp_variant_pick(struct widget_instance *widget);
static void h3_mp_variant_check(void);

/* whether the lobby plays a saved map variant of its map */
static boolean h3_mp_variant_chosen(void)
{
	return h3_mp.variant_file[0] && h3_mp.map_index != NONE && h3_mp.variant_map == h3_mp.map_index;
}

/* the lobby's map variant forgotten (another map, a damaged or gone file) */
static void h3_mp_variant_clear(void)
{
	h3_mp.variant_file[0] = 0;
	h3_mp.variant_title[0] = 0;
	h3_mp.variant_map = NONE;
}

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

static boolean h3_gametype_file_good(long profile_index);

/* the game types the Xbox's game type list offers (the first local
player's, the valid ones), and the one it opens on: the one last used, else
its first. A player's game type whose file fails its checksum (damaged or
edited since: the valid bit is the memory unit's map file's, from an earlier
run) is left out - playlist_profile_get would play Slayer's rules under its
name -; GAME TYPES says it is damaged */
static void h3_mp_gametypes_read(void)
{
	long indices[H3_MAXIMUM_GAMETYPES];
	word count = H3_MAXIMUM_GAMETYPES;
	char directory[MAXIMUM_FILENAME_LENGTH + 1];
	long remembered = NONE;
	short index, damaged = 0, unreadable = 0;

	playlist_profiles_enumerate_available_to_local_player_index(0, &count, indices);
	h3_mp.gametype_count = 0;
	for (index = 0; index < (short)MIN(count, H3_MAXIMUM_GAMETYPES); index++)
	{
		/* (playlist_profile_get_display_name's: MAX_GAMENAME, 128) */
		wchar_t name[128];
		short slot = h3_mp.gametype_count;

		if (indices[index] == NONE)
			continue;
		if (!TEST_FLAG(indices[index], _saved_game_file_index_valid_bit))
		{
			unreadable += TEST_FLAG(indices[index], _saved_game_file_index_read_only_bit) ? 0 : 1;
			continue;
		}
		if (!TEST_FLAG(indices[index], _saved_game_file_index_read_only_bit))
		{
			struct game_variant waited;

			/* (playlist_profile_get waits for a write still going on) */
			playlist_profile_get(indices[index], &waited);
			if (!h3_gametype_file_good(indices[index]))
			{
				damaged++;
				continue;
			}
		}
		h3_mp.gametypes[slot] = indices[index];
		h3_mp.gametype_names[slot][0] = 0;
		if (playlist_profile_get_display_name(indices[index], name))
			h3_utf8(h3_mp.gametype_names[slot], sizeof(h3_mp.gametype_names[slot]), name);
		h3_mp.gametype_count++;
	}
	if (damaged || unreadable)
		platform_log("menus: h3 mp: %d game types, %d of the player's left out (%d failing the checksum, %d unreadable)",
			h3_mp.gametype_count, damaged + unreadable, damaged, unreadable);
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
	h3_mp_variant_check();
}

static char const *h3_mp_map_name(void)
{
	static char name[48 + FORGE_FILE_TITLE_SIZE];
	char const *slash;
	short display;
	wchar_t const *custom;

	/* (a map variant: "Blood Gulch - My Fort", as the Forge lobby's MAP
	row says it) */
	if (h3_mp_variant_chosen())
	{
		snprintf(name, sizeof(name), "%s - %s", h3_map_names[h3_mp.map_index], h3_mp.variant_title);
		return name;
	}
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
	h3_party_online("the multiplayer lobby");
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
	/* ("1 Player (16 max)": the most players a game of that network takes -
	network.max_players on every network a game is hosted on, its 0 the
	Xbox's 16: network_server_manager.c; HOST SETTINGS' MAX PLAYERS) */
	maximum = (int)atol(setting_get("HALO_NET_MAX_PLAYERS", "16"));
	maximum = h3_mp.network == _h3_network_offline ? 4 : maximum < 2 ? 16 : (int)PIN(maximum, 2, 16);
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
	h3_mp.picking_variant = FALSE;
	h3_mp.pick_refocus = FALSE;
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

	/* (a map's variants shown: their rows) */
	if (h3_mp.picking_variant)
	{
		h3_mp_variants_update(list);
		return;
	}
	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		if (child == list->focused_child)
			focused_row = index;
	}
	/* (back from a map's variants: on that map's row) */
	if (h3_mp.pick_refocus)
	{
		focused_row = NONE;
		h3_mp.pick_refocus = FALSE;
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
static boolean h3_mp_map_choose(long index, char const *variant_file, char const *variant_title)
{
	short before_index = h3_mp.map_index;
	char before_other[sizeof(h3_mp.map_other)];
	char before_file[sizeof(h3_mp.variant_file)];
	char before_title[sizeof(h3_mp.variant_title)];
	short before_variant_map = h3_mp.variant_map;

	snprintf(before_other, sizeof(before_other), "%s", h3_mp.map_other);
	snprintf(before_file, sizeof(before_file), "%s", h3_mp.variant_file);
	snprintf(before_title, sizeof(before_title), "%s", h3_mp.variant_title);
	h3_mp_variant_clear();
	if (index < H3_MAP_COUNT)
	{
		h3_mp.map_index = (short)index;
		h3_mp.map_other[0] = 0;
		saved_game_file_remember_last_used_multiplayer_map(h3_map_paths[index]);
		/* (one of its saved map variants: placed in its games) */
		if (variant_file && variant_file[0])
		{
			snprintf(h3_mp.variant_file, sizeof(h3_mp.variant_file), "%s", variant_file);
			snprintf(h3_mp.variant_title, sizeof(h3_mp.variant_title), "%s", variant_title ? variant_title : "");
			h3_mp.variant_map = (short)index;
			platform_log("menus: h3 mp: map %s, map variant %s (%s)", h3_map_names[index], h3_mp.variant_title,
				h3_mp.variant_file);
		}
		else
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
	play with others, a map variant that cannot be played: said - the
	lobby's back on the game's map) */
	if (!h3_live_follow())
	{
		h3_mp.map_index = before_index;
		snprintf(h3_mp.map_other, sizeof(h3_mp.map_other), "%s", before_other);
		snprintf(h3_mp.variant_file, sizeof(h3_mp.variant_file), "%s", before_file);
		snprintf(h3_mp.variant_title, sizeof(h3_mp.variant_title), "%s", before_title);
		h3_mp.variant_map = before_variant_map;
		saved_game_file_remember_last_used_multiplayer_map((char *)(before_index != NONE ? h3_map_paths[before_index] :
			before_other));
		platform_log("menus: h3 live lobby: that map refused: still %s", h3_mp_map_name());
	}
	return TRUE;
}

static boolean h3_mp_map_set(struct widget_instance *widget)
{
	long index = h3_mp.map_view.offset + h3_child_index(widget);
	char why[160];

	/* (a map's variants shown: the one chosen, or the map itself) */
	if (h3_mp.picking_variant)
		return h3_mp_variant_pick(widget);
	if (index < 0 || index >= h3_mp.map_count)
		return TRUE;
	/* (a map that cannot be played: not chosen, the picker stays on it, its
	reason shown) */
	if (map_check(h3_mp.map_paths[index], why, sizeof(why)) != _map_check_playable)
	{
		platform_log("menus: h3 maps: %s can't be chosen: %s", h3_mp.map_names[index], why);
		return FALSE;
	}
	/* (one of the Xbox's maps with saved map variants: its variants shown,
	the map itself first - the picker stays -; a map with none chosen at
	once, as before) */
	if (h3_mp_variants_offer(index))
		return FALSE;
	return h3_mp_map_choose(index, NULL, NULL);
}

/* (port h3 network status: the lobbies' line under NETWORK) the connection's
status - Online, Connecting..., Same Wi-Fi, Ad hoc, No Wi-Fi, or why Online
failed (network_connection_status) - as Halo 3's Xbox LIVE indicator; for
OFFLINE and LOCAL, which use no connection, that it is this PS Vita alone */
static void h3_network_status_update(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	boolean campaign = named(screen, "mission_row") != NULL;
	char text[128];

	text[0] = 0;
	if (campaign ? h3_campaign.network != _h3_campaign_local : h3_mp.network != _h3_network_offline)
		network_connection_status(text, (int)sizeof(text));
	else
	{
		/* (no connection: said, not an empty line under NETWORK) */
		snprintf(text, sizeof(text), "%s", T("On this PS Vita only"));
	}
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

	/* (in a party: ONLINE alone - a party plays online; the owner, Oct 21
	2026) */
	if (h3_party_refuses(network == _h3_network_online))
		return TRUE;
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
	{
		if (halo_text_input_password)
			memset(halo_text_input_text, 0, sizeof(halo_text_input_text));
		return FALSE;
	}
	sanitize(text, size, halo_text_input_text);
	/* (a password typed: no copy of it kept here) */
	if (halo_text_input_password)
		memset(halo_text_input_text, 0, sizeof(halo_text_input_text));
	return TRUE;
}

#ifndef HALO_VITA
/* (the PC build, Menu style Halo CE: virtual_keyboard.c's launch) the
classic menus' names - a new profile's, a profile's rename, a game
variant's - typed in the PC build's text box, as the Halo 3 menus' are,
rather than on the Xbox's on-screen keyboard: CAPTION_INDEX the keyboard's
caption (8 a profile's name, 9 a game variant's, 10 either's rename), TEXT
the name now, MAXIMUM its characters. FALSE: not taken (a test build's
scripted run with no one to type, typing_available) - the Xbox's keyboard
as before */
static boolean typing_available(void);

boolean pc_menu_classic_typing_begin(short caption_index, wchar_t const *text, int maximum)
{
	char now[HALO_TEXT_INPUT_SIZE];
	int length = 0;

	if (!typing_available() || maximum <= 0)
		return FALSE;
	for (; text && *text && length < (int)sizeof(now) - 1; text++)
	{
		if (*text >= 0x20 && *text <= 0x7E)
			now[length++] = (char)*text;
	}
	now[length] = 0;
	typing_begin(_typing_classic, caption_index == 9 ? T("Game variant name") : T("Player name"), now,
		PIN(maximum, 1, HALO_TEXT_INPUT_SIZE - 1), FALSE);
	return TRUE;
}

/* (virtual_keyboard.c, each frame while it waits) 0 still typing; 1 typed,
TEXT (COUNT characters with its end) the name, sanitized as the Halo 3
menus' names are; -1 cancelled */
int pc_menu_classic_typing_poll(wchar_t *text, int count)
{
	char typed[HALO_TEXT_INPUT_SIZE];
	int state = __atomic_load_n(&halo_text_input_state, __ATOMIC_ACQUIRE);
	int index;

	if (menu_functions.typing != _typing_classic)
		return -1;
	if (state != HALO_TEXT_INPUT_DONE && state != HALO_TEXT_INPUT_CANCELLED)
		return 0;
	if (!typing_done(_typing_classic, typed, sizeof(typed)))
		return -1;
	for (index = 0; typed[index] && index < count - 1; index++)
		text[index] = (wchar_t)(unsigned char)typed[index];
	if (count > 0)
		text[index] = 0;
	return 1;
}
#endif

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
	/* (port: the tester's flaming helmet, a listed tester's alone:
	port/linux/src/p2p_flame.inc; player.flaming_helmet, not the profile's) */
	_h3_custom_flame,
	NUMBER_OF_H3_CUSTOM_ROWS
};

static char const *const h3_custom_rows[NUMBER_OF_H3_CUSTOM_ROWS] =
{
	"op_name", "op_color", "op_model", "op_stick", "op_buttons", "op_look", "op_invert", "op_flight", "op_autocenter",
	"op_vibration", "op_flame",
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
	/* (port) the flaming helmet's row shown (a listed tester: p2p_flame_listed,
	asked as the editor opens), the help's place without it */
	boolean flame_listed;
	boolean flame_asked;
	short help_offset;
	boolean help_offset_kept;
	/* (Menu style Halo CE) opened from the Xbox's profile settings (MORE
	OPTIONS): the profile's editing theirs, never ended here */
	boolean classic;
} h3_custom;

static void h3_custom_begin(long profile_index, struct player_profile *profile, char const *name);
/* (Menu style Halo CE's additions: the Xbox's pregame) */
static void ce_pregame_initialize(void);
static boolean ce_variant_next(void);
static void ce_lobby_update(struct widget_instance *list);

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

/* the current profile - the one the nameplate and CUSTOMIZATION show: the
first player's active one, else the one chosen or saved this session, the
last used, the first there is - or, with none at all, a new one named
"Player" (as CREATE NEW PROFILE makes one); NONE if there is none and none
could be made */
static long h3_profile_current(short controller_index)
{
	char status[96];
	wchar_t name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH];
	struct player_profile profile;
	long profile_index;
	short attempt;

	h3_plate.changed = TRUE;
	h3_nameplate_read();
	profile_index = h3_plate.shown;
	if (profile_index != NONE && player_profile_get(profile_index, &profile))
		return profile_index;
	for (attempt = 1; attempt <= 9; attempt++)
	{
		char typed[16];

		snprintf(typed, sizeof(typed), attempt == 1 ? "Player" : "Player %d", attempt);
		if (!h3_profile_name_check(typed, name, NULL, status, sizeof(status)))
			continue;
		profile_index = player_profile_new(controller_index != NONE ? controller_index : 0, name);
		if (profile_index == NONE)
			break;
		platform_log("menus: h3 profiles: no profile: created %s", typed);
		h3_nameplate_changed(profile_index);
		return profile_index;
	}
	platform_log("menus: h3 profiles: no profile, and none could be made");
	return NONE;
}

/* (CAMPAIGN, Menu style Online: SELECT PROFILE only in CUSTOMIZATION) the
current profile played as SELECT PROFILE's Cross plays one (h3_profile_play:
the campaign's controller, the active profile, the session's); FALSE: none
(SELECT PROFILE then, as before) */
static boolean h3_profile_play_current(short controller_index)
{
	struct player_profile profile;
	long profile_index = h3_profile_current(controller_index);
	char name[48];

	if (profile_index == NONE || !player_profile_get(profile_index, &profile))
		return FALSE;
	player_ui_reset_single_player_local_player_controllers();
	player_ui_set_single_player_local_player_controller(0, (short)(controller_index != NONE ? controller_index : 0));
	player_ui_set_active_player_profile(0, profile_index, &profile);
	h3_nameplate_changed(profile_index);
	h3_session_profile_chosen((short)(controller_index != NONE ? controller_index : 0), profile_index);
	h3_profile_name(name, sizeof(name), &profile);
	platform_log("menus: h3 profiles: %s (current) plays the campaign: no SELECT PROFILE", name);
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
	h3_custom.classic = FALSE;
	h3_custom_begin(h3_profiles.indices[item], profile, name);
	return TRUE;
}

/* the editor on the profile being edited (profile_index, read: profile),
named name */
static void h3_custom_begin(long profile_index, struct player_profile *profile, char const *name)
{
	h3_custom.index = profile_index;
	h3_custom.flame_asked = FALSE;
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
}

/* (Menu style Halo CE: the Xbox's profile settings' MORE OPTIONS, port ce
custom) the Halo 3 editor on the profile the Xbox's screens edit - its
model, flaming helmet and the rest -, its edits theirs: Circle saves the
profile (what the Xbox's rows changed too) and the Xbox's screen goes on
editing it (h3_custom.classic: the editing not ended) */
static boolean ce_custom_open(void)
{
	long index = player_ui_get_edit_profile_index();
	struct player_profile *profile = player_ui_get_edit_player_profile();
	char name[48];

	if (index == NONE || !profile)
	{
		platform_log("menus: ce customize: no profile being edited");
		return FALSE;
	}
	h3_profile_name(name, sizeof(name), profile);
	h3_custom.classic = TRUE;
	h3_custom_begin(index, profile, name);
	platform_log("menus: ce customize: MORE OPTIONS for %s (the Xbox's profile settings)", name);
	return TRUE;
}

/* (port ce profile: Menu style Halo CE's profile settings, a widget of
ours on them) the list's description as the Xbox's "plyr prof edit select
menu upd8" sets it - its words and picture by the focused row - counting the
Xbox's rows alone; on MORE OPTIONS (its row: more) the panel with the port's
words (more_help) */
static void ce_profile_description_update(struct widget_instance *widget)
{
	struct widget_instance *list = named(screen_of(widget), "profile_edit_select_list");
	struct widget_instance *description, *child, *help;
	boolean more;
	short index = 0;

	if (!list || !list->focused_child || !(description = list->parameters.list.extended_description) ||
		!description->child || !description->child->next)
	{
		return;
	}
	for (child = list->child; child && child != list->focused_child; child = child->next)
	{
		if (!child->name || strcmp(child->name, "more"))
			index++;
	}
	/* (MORE OPTIONS: the panel, its own words in it - more_help -, not the
	Xbox's picture and words) */
	more = list->focused_child->name && !strcmp(list->focused_child->name, "more");
	widget_instance_set_visibility_recursive(description, !more);
	description->visible = TRUE;
	if ((help = named(screen_of(widget), "more_help")) != NULL)
		widget_instance_set_visibility_recursive(help, more);
	if (more)
		return;
	description->child->parameters.text_box.string_list_index = index;
	description->child->next->animation.current_frame_index = index;
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
	case _h3_custom_flame:
		snprintf(text, size, "%s", config_boolean_live("player.flaming_helmet", "HALO_FLAMING_HELMET") ? T("ON") :
			T("OFF"));
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
	case _h3_custom_flame:
		/* (this machine's setting, as the settings panel's Flaming helmet:
		kept at once, not with the profile) */
		if (!h3_custom.flame_listed)
			return FALSE;
		setting_set("HALO_FLAMING_HELMET",
			config_boolean_live("player.flaming_helmet", "HALO_FLAMING_HELMET") ? "false" : "true");
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
/* (a listed tester's: a step further back and lower, the same place in the
window, so the flaming helmet's flame rises inside the window rather than
behind the panel above it - the helmet sat a few pixels under the window's
top; kept whether the flame is on or off, so it never jumps) */
#define H3_CUSTOM_SPARTAN_FLAME_PLACEMENT "1.75 0.645 -0.33 20"
/* (Menu style Online: large at the right, as the online CUSTOMIZATION's,
no window around it; online/customize.xml's camera) */
#define ONLINE_CUSTOM_SPARTAN_PLACEMENT "0.85 0.27 -0.42 20"

/* (debug: HALO_ONLINE_SPARTAN_PLACEMENT="<distance> <right> <up> <facing>"
in its place, to try others) */
static char const *online_custom_spartan_placement(void)
{
	char const *placement = getenv("HALO_ONLINE_SPARTAN_PLACEMENT");

	return placement && *placement ? placement : ONLINE_CUSTOM_SPARTAN_PLACEMENT;
}

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
			menu_scene_spartan_show(scene, halo_menu_style_online() ? online_custom_spartan_placement() :
				h3_custom.flame_listed ? H3_CUSTOM_SPARTAN_FLAME_PLACEMENT : H3_CUSTOM_SPARTAN_PLACEMENT, shown_color))
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
	/* (the tester's flaming helmet on it, while worn: flaming_helmet.c) */
	{
		long menu_scene_spartan_object(void);
		void flaming_helmet_preview(long object_index);

		flaming_helmet_preview(shown && h3_custom.flame_listed &&
			config_boolean_live("player.flaming_helmet", "HALO_FLAMING_HELMET") ? menu_scene_spartan_object() : NONE);
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
	{
		void flaming_helmet_preview(long object_index);

		flaming_helmet_preview(NONE);
	}
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
	/* (the flaming helmet's row: a listed tester's alone; the help a row
	lower for it) */
	if (!h3_custom.flame_asked)
	{
		h3_custom.flame_asked = TRUE;
		h3_custom.flame_listed = (boolean)p2p_flame_listed();
		platform_log("menus: h3 customize: flaming helmet row %s", h3_custom.flame_listed ? "shown (a listed tester)" :
			"hidden");
	}
	if ((found = named(list, h3_custom_rows[_h3_custom_flame])) != NULL)
	{
		widget_instance_set_visibility_recursive(found, h3_custom.flame_listed);
		found->disabled = !h3_custom.flame_listed;
	}
	if ((found = named(screen, "custom_help")) != NULL)
	{
		if (!h3_custom.help_offset_kept)
		{
			h3_custom.help_offset_kept = TRUE;
			h3_custom.help_offset = found->vertical_offset;
		}
		found->vertical_offset = (short)(h3_custom.help_offset + (h3_custom.flame_listed ? 18 : 0));
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

		/* (Menu style Online: the colour's name is COLORS' heading; the
		corner says only how a model's pack is coming) */
		snprintf(line, sizeof(line), "%s", halo_menu_style_online() ? "" :
			T(h3_color_names[h3_swatch_frame(profile->primary_color_index)]));
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
		if (!h3_custom.classic)
			player_ui_end_editing_profile();
		platform_log("menus: h3 customize: %s: no changes", name);
		return;
	}
	if (!player_ui_save_profile())
	{
		platform_log("menus: h3 customize: %s could not be saved", name);
		return;
	}
	/* (opened from the Xbox's profile settings: player_ui_save_profile ends the
	editing, which those screens still need - their B read it and closed the
	screen, "no saved game file is being edited" -; the saved profile taken up
	again, nothing changed in it yet) */
	if (h3_custom.classic)
		player_ui_begin_editing_profile(h3_custom.index);
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


/* ---- Menu style Online's CUSTOMIZATION (menus/online/customize.xml): the
Halo 3 editor's rows (h3_custom_update and h3_custom_change still read and
change them) in one list, shown a tab at a time - ARMORY (name, model, the
tester's flaming helmet, and the armor pieces Halo CE has no models for,
greyed: "not yet"), COLORS (the primary colour, with the game's eighteen in a
grid and its hex; the other colours greyed) and CONTROLS (the profile's
controller settings) -, L and R (the triggers) changing the tab. The rows of
the other tabs are hidden and passed over; the tab's are stacked from the
list's top. */
#define ONLINE_CUSTOM_ROW_HEIGHT 19
#define ONLINE_CUSTOM_TABS 4
/* (the tabs: PROFILE, ARMORY - the one it opens on -, COLORS, CONTROLS) */
enum
{
	_online_custom_tab_profile,
	_online_custom_tab_armory,
	_online_custom_tab_colors,
	_online_custom_tab_controls,
};
/* (PROFILE's rows: the first profiles, then CREATE NEW PROFILE) */
#define ONLINE_CUSTOM_PROFILE_ROWS 8
#define ONLINE_CUSTOM_GRID_COLUMNS 3
#define ONLINE_CUSTOM_GRID_WIDTH 34
#define ONLINE_CUSTOM_GRID_HEIGHT 18

static struct
{
	struct widget_instance *list;
	short tab;
	short shown_tab;
	short help_row;
	/* (PROFILE's delete question up: the editor it replaced keeps editing,
	and comes back on PROFILE) */
	boolean deleting;
} online_custom;

static struct
{
	char const *row;
	short tab;
	/* (a row the port cannot do yet: greyed, its words why) */
	char const *not_yet;
	/* (the heading over the help) */
	char const *label;
} const online_custom_rows[] =
{
	{ "pr_1", 0, NULL, N_("PROFILE") },
	{ "pr_2", 0, NULL, N_("PROFILE") },
	{ "pr_3", 0, NULL, N_("PROFILE") },
	{ "pr_4", 0, NULL, N_("PROFILE") },
	{ "pr_5", 0, NULL, N_("PROFILE") },
	{ "pr_6", 0, NULL, N_("PROFILE") },
	{ "pr_7", 0, NULL, N_("PROFILE") },
	{ "pr_8", 0, NULL, N_("PROFILE") },
	{ "pr_new", 0, NULL, N_("CREATE NEW PROFILE") },
	{ "op_name", 1, NULL, N_("PLAYER NAME") },
	{ "na_tag", 1, N_("A service tag beside your name. Not in this version yet."), N_("SERVICE TAG") },
	{ "op_model", 1, NULL, N_("MODEL") },
	{ "na_helmet", 1, N_("Helmets need armor models Halo CE does not have.\nNot in this version yet: MODEL picks who you\nplay as."), N_("HELMET") },
	{ "na_rshoulder", 1, N_("Shoulder pieces need armor models Halo CE does\nnot have. Not in this version yet."), N_("RIGHT SHOULDER") },
	{ "na_lshoulder", 1, N_("Shoulder pieces need armor models Halo CE does\nnot have. Not in this version yet."), N_("LEFT SHOULDER") },
	{ "na_chest", 1, N_("Chest pieces need armor models Halo CE does not\nhave. Not in this version yet."), N_("CHEST") },
	{ "op_flame", 1, NULL, N_("FLAMING HELMET") },
	{ "op_color", 2, NULL, N_("ARMOR PRIMARY") },
	{ "na_secondary", 2, N_("Halo CE armor takes one colour: the primary.\nNot in this version yet."), N_("ARMOR SECONDARY") },
	{ "na_visor", 2, N_("The visor's tint. Not in this version yet."), N_("ARMOR VISOR") },
	{ "na_lights", 2, N_("The armor's lights. Not in this version yet."), N_("ARMOR LIGHTS") },
	{ "op_stick", 3, NULL, N_("STICK LAYOUT") },
	{ "op_buttons", 3, NULL, N_("BUTTON LAYOUT") },
	{ "op_look", 3, NULL, N_("LOOK SENSITIVITY") },
	{ "op_invert", 3, NULL, N_("INVERT LOOK") },
	{ "op_flight", 3, NULL, N_("INVERT FLIGHT") },
	{ "op_autocenter", 3, NULL, N_("AUTO-CENTER") },
	{ "op_vibration", 3, NULL, N_("VIBRATION") },
};

/* (the game's colours, profile_color_table's, as tools/halo3_menu_art.py
draws them: the COLORS tab's hex) */
static unsigned long const online_custom_colours[H3_SWATCH_FRAMES - 1] =
{
	0xFFFFFF, 0x000000, 0xFE0000, 0x0201E3, 0x707E71, 0xFFFF01, 0x00FF01, 0xFF56B9, 0xAB10F4,
	0x01FFFF, 0x6493ED, 0xFF7F00, 0x1ECC91, 0x006401, 0x603814, 0xC69C6C, 0x9D0B0E, 0xF5999E,
};

static short online_custom_row_index(char const *name)
{
	short index;

	for (index = 0; name && index < (short)NUMBEROF(online_custom_rows); index++)
	{
		if (!strcmp(name, online_custom_rows[index].row))
			return index;
	}
	return NONE;
}

/* (Menu style Online) the title screen ("press to continue", online/title.xml)
over the main menu the first time it is up this run, opened from the
frame's update (pc_menu_functions_update), not from a widget's */
static struct
{
	boolean title_done;
	boolean title_pending;
	/* (SWITCH LOBBY: the lobby to open in the one up, the main menu behind
	it, at the frame's update) */
	char const *switch_to;
} online_menus;

/* (port online version: the main menu's) the version, the title screen
asked for the first time, EXIT hidden on the PS Vita (it closes from the
system's menu) */
static void online_main_update(struct widget_instance *widget)
{
	struct widget_instance *found;

	text_show(widget, "HALO CE VITA " HALO_VITA_VERSION_SHOWN);
	if (!online_menus.title_done)
	{
		online_menus.title_done = TRUE;
		online_menus.title_pending = !getenv("HALO_ONLINE_NO_TITLE");
	}
#if defined(HALO_VITA) || defined(HALO_ANDROID)
	if ((found = named(screen_of(widget), "exit")) != NULL)
	{
		widget_instance_set_visibility_recursive(found, FALSE);
		found->disabled = TRUE;
	}
#else
	(void)found;
#endif
}

static void online_menus_update(void)
{
	if (online_menus.switch_to)
	{
		long target = tag_loaded(UI_WIDGET_DEFINITION_TAG, online_menus.switch_to);
		long main_menu = tag_loaded(UI_WIDGET_DEFINITION_TAG, "ui\\shell\\main_menu\\main_menu");
		struct widget_instance *active = NULL;
		long index;

		for (index = 0; index < 4 && !active; index++)
			active = ui_widget_port_active_screen((short)index);
		if (target != NONE && active)
		{
			platform_log("menus: online switch lobby: %s in place of %s", online_menus.switch_to,
				tag_get_name(active->definition_tag_index));
			ui_widget_load_by_name_or_tag(NULL, target, NULL, active->local_player_index,
				main_menu != NONE ? main_menu : active->definition_tag_index, NONE, NONE);
		}
		online_menus.switch_to = NULL;
	}
	if (online_menus.title_pending)
	{
		long title = tag_loaded(UI_WIDGET_DEFINITION_TAG, "pc\\online\\title\\screen");
		struct widget_instance *active = NULL;
		long index;

		online_menus.title_pending = FALSE;
		for (index = 0; index < 4 && !active; index++)
			active = ui_widget_port_active_screen((short)index);
		if (title != NONE && active && halo_menu_style_online())
		{
			platform_log("menus: online title screen");
			ui_widget_load_by_name_or_tag(NULL, title, NULL, active->local_player_index, active->definition_tag_index,
				NONE, NONE);
		}
	}
}


/* ---- Menu style Online's lobbies (menus/online/multiplayer.xml): SWITCH
LOBBY opens a list beside it in the lobby itself (MULTIPLAYER, FORGE,
CAMPAIGN, each its words at the right), which takes the screen's focus
while open; its row replaces the lobby with the other, the party kept (the
lobby it replaces goes back where it came from: the main menu). The map's
card shows the map's description (ui.map's, as SELECT MAP does). */
static struct
{
	boolean open;
	short said;
} online_switch;

/* (port online switch: the lobby's list, whose first rows - switch_0 to
switch_2, beside SWITCH LOBBY - are the switcher's) those shown while it is
open, the focus kept on them then and off them otherwise; its words the
focused row's */
static void online_switch_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list), *child, *found, *first = NULL, *focus = NULL;
	short index, focused = NONE;
	char name[16];

	if ((found = named(screen, "switch_panel")) != NULL)
		widget_instance_set_visibility_recursive(found, online_switch.open);
	if ((found = named(screen, "switch_text")) != NULL)
		widget_instance_set_visibility_recursive(found, online_switch.open);
	/* (HOST SETTINGS' status, under the switcher's rows) */
	if ((found = named(screen, "network_status")) != NULL)
		widget_instance_set_visibility_recursive(found, !online_switch.open);
	for (index = 0; index < 3; index++)
	{
		snprintf(name, sizeof(name), "switch_%d", index);
		if ((child = named(list, name)) == NULL)
			continue;
		widget_instance_set_visibility_recursive(child, online_switch.open);
		child->disabled = !online_switch.open;
		if (!first)
			first = child;
		if (child == list->focused_child)
			focused = index;
		child->animation.current_frame_index = online_switch.open && child == list->focused_child ? 1 : 0;
	}
	if (online_switch.open && focused == NONE && first)
	{
		/* (opened: on the lobby it is, MULTIPLAYER, FORGE or CAMPAIGN) */
		char const *screen_name = tag_get_name(screen->definition_tag_index);
		short current = screen_name && strstr(screen_name, "\\forge\\") ? 1 :
			screen_name && strstr(screen_name, "\\campaign\\") ? 2 : 0;

		snprintf(name, sizeof(name), "switch_%d", current);
		focus = named(list, name);
		focused = current;
		platform_log("menus: online switch lobby: open");
	}
	else if (!online_switch.open && focused != NONE)
		focus = named(list, "switch_row");
	if (focus && focus != list->focused_child)
		ui_widget_port_give_focus(list, focus);
	string_show(named(screen, "switch_text"), focused != NONE ? focused : 0);
	if (online_switch.open && focused != online_switch.said)
	{
		online_switch.said = focused;
		platform_log("menus: online switch lobby: row %d", focused);
	}
}

/* (port online lobby: the lobby's screen) the map card's description: the
picture's map's (the game's own maps; none for another) */
static void online_lobby_update(struct widget_instance *screen)
{
	struct widget_instance *picture = named(screen, "map_picture"), *text = named(screen, "map_text");

	if (!text)
		return;
	if (picture && h3_mp.initialized && h3_mp.map_index != NONE && named(screen, "game_row"))
	{
		widget_instance_set_visibility_recursive(text, TRUE);
		string_show(text, picture->animation.current_frame_index);
	}
	else
		widget_instance_set_visibility_recursive(text, FALSE);
}

/* (port online custom tab next and previous: R and L) */
static void online_custom_tab_step(int step)
{
	online_custom.tab = (short)(((online_custom.tab + step) % ONLINE_CUSTOM_TABS + ONLINE_CUSTOM_TABS) %
		ONLINE_CUSTOM_TABS);
	platform_log("menus: online customize: tab %d", online_custom.tab);
}

/* ---- CUSTOMIZATION's PROFILE tab (Menu style Online): the profiles - the
one edited marked CURRENT -, CREATE NEW PROFILE; Cross switches to one (the
edits saved first, as Circle saves them; then it is the profile played, as
SELECT PROFILE's Cross makes it: the nameplate, the Spartan, the campaign's
and the games'), Square deletes one (the question, profiles.xml's; never the
one edited), Triangle makes one (its name typed) and switches to it. Its
card - level, difficulty, controls - in the help's place. CUSTOMIZATION
opens on the current profile (pc_menu_functions_profile_skip), no SELECT
PROFILE before it. */

static boolean h3_profile_play_current(short controller_index);

/* the profile played now: the one the editor edits */
static void online_profile_make_current(long profile_index)
{
	struct player_profile profile;

	if (profile_index == NONE || !player_profile_get(profile_index, &profile))
		return;
	player_ui_set_active_player_profile(0, profile_index, &profile);
	h3_nameplate_changed(profile_index);
	h3_session_profile_chosen(0, profile_index);
}

/* the PROFILE row's item (pr_<n>: n-1; pr_new: the count), NONE for none */
static short online_profile_item(struct widget_instance *widget)
{
	if (!widget || !widget->name || strncmp(widget->name, "pr_", 3))
		return NONE;
	if (!strcmp(widget->name, "pr_new"))
		return h3_profiles.count;
	return (short)(atoi(widget->name + 3) - 1);
}

/* (CUSTOMIZATION opening, Menu style Online) the current profile edited -
made if there is none -: FALSE when none can be (SELECT PROFILE then) */
static boolean online_custom_begin_current(void)
{
	long current;
	short index;

	online_custom.deleting = FALSE;
	if (!h3_profile_play_current(NONE))
		return FALSE;
	current = player_ui_get_active_player_profile_index(0);
	h3_profiles_initialize(_h3_profiles_customize);
	for (index = 0; index < h3_profiles.count; index++)
	{
		if (h3_profile_same(h3_profiles.indices[index], current))
		{
			if (!h3_profile_edit(index))
				return FALSE;
			online_custom.list = NULL;
			platform_log("menus: online customize: opened on the current profile, no SELECT PROFILE");
			return TRUE;
		}
	}
	return FALSE;
}

/* (Cross on a profile) switched to it */
static boolean online_profile_choose(struct widget_instance *widget)
{
	short item = online_profile_item(widget);
	char name[48];

	if (item < 0 || item >= h3_profiles.count)
		return FALSE;
	if (h3_profile_same(h3_profiles.indices[item], h3_custom.index))
		return TRUE;
	h3_custom_save();
	h3_custom_spartan_hide();
	if (!h3_profile_edit(item))
	{
		snprintf(h3_custom.status, sizeof(h3_custom.status), "%s", h3_profiles.status);
		return FALSE;
	}
	online_profile_make_current(h3_profiles.indices[item]);
	h3_profile_name(name, sizeof(name), &h3_profiles.profiles[item]);
	platform_log("menus: online customize: switched to %s", name);
	h3_profiles.read = FALSE;
	return TRUE;
}

/* (Square on a profile) the question asked: not of the one edited */
static boolean online_profile_delete_ask(struct widget_instance *list)
{
	short item = online_profile_item(list ? list->focused_child : NULL);

	if (online_custom.tab != _online_custom_tab_profile || item < 0 || item >= h3_profiles.count)
		return FALSE;
	if (h3_profile_same(h3_profiles.indices[item], h3_custom.index))
	{
		snprintf(h3_custom.status, sizeof(h3_custom.status), "%s",
			T("This is the profile you are using.\nSwitch to another to delete it."));
		return FALSE;
	}
	h3_custom.status[0] = 0;
	h3_profiles.cursor = item;
	if (!h3_profile_delete_ask())
		return FALSE;
	online_custom.deleting = TRUE;
	return TRUE;
}

/* (PROFILE's frame) the list read again when it changed; a new profile's
name typed: the edits saved, the profile made, switched to */
static void online_profile_tab_update(void)
{
	char text[64];

	if (typing_done(_typing_profile_new, text, sizeof(text)))
	{
		h3_custom_save();
		h3_custom_spartan_hide();
		h3_profiles.mode = _h3_profiles_customize;
		h3_profile_create(text);
		h3_profiles.open_pending = FALSE;
		if (player_ui_get_edit_player_profile())
			online_profile_make_current(h3_custom.index);
		else
			snprintf(h3_custom.status, sizeof(h3_custom.status), "%s", h3_profiles.status);
		h3_profiles.read = FALSE;
	}
	if (!h3_profiles.read)
	{
		h3_profiles.mode = _h3_profiles_customize;
		h3_profiles_read();
	}
}

/* a PROFILE row shown (a profile there is, or CREATE NEW PROFILE), its name
and its note */
static boolean online_profile_row_show(struct widget_instance *row)
{
	short item = online_profile_item(row);
	char text[64];

	/* (pr_<n> a profile there is; pr_new CREATE NEW PROFILE) */
	if (item == NONE || (strcmp(row->name, "pr_new") && (item >= h3_profiles.count || item >= ONLINE_CUSTOM_PROFILE_ROWS)))
		return FALSE;
	if (item == h3_profiles.count)
		snprintf(text, sizeof(text), "%s", T("CREATE NEW PROFILE"));
	else
	{
		h3_profile_name(text, sizeof(text), &h3_profiles.profiles[item]);
		h3_capitals(text);
	}
	text_show(named(row, "pr_label"), text);
	text_show(named(row, "pr_value"), item == h3_profiles.count ? "" :
		h3_profile_same(h3_profiles.indices[item], h3_custom.index) ? T("CURRENT") :
		h3_profiles.damaged[item] ? T("DAMAGED") : "");
	return TRUE;
}

/* (PROFILE) the focused profile's card in the help's place: its level, its
difficulty, its controls */
static void online_profile_card(struct widget_instance *screen, short focused_row)
{
	struct widget_instance *found;
	struct widget_instance *focused = online_custom.list ? online_custom.list->focused_child : NULL;
	short item = online_profile_item(focused);
	char heading[64], text[384];

	(void)focused_row;
	if ((found = named(screen, "grid")) != NULL)
		widget_instance_set_visibility_recursive(found, FALSE);
	if ((found = named(screen, "custom_help")) != NULL)
		widget_instance_set_visibility_recursive(found, FALSE);
	heading[0] = text[0] = 0;
	if (item >= 0 && item < h3_profiles.count)
	{
		struct player_profile *profile = &h3_profiles.profiles[item];
		short level, difficulty;
		char level_name[64], controls[96];

		h3_profile_name(heading, sizeof(heading), profile);
		h3_capitals(heading);
		if (h3_profiles.damaged[item])
			snprintf(text, sizeof(text), "%s", T("This profile's file is damaged: it can't be\nloaded or played. Delete it, or make a new one."));
		else
		{
			player_profile_get_highest_completed_solo_level(profile, &level, &difficulty);
			level = (short)MIN(level + 1, NUMBER_OF_SINGLE_PLAYER_LEVELS - 1);
			snprintf(level_name, sizeof(level_name), "%s", T(h3_level_names[PIN(level, 0, NUMBER_OF_SINGLE_PLAYER_LEVELS - 1)]));
			h3_capitals(level_name);
			snprintf(controls, sizeof(controls), "%s%s%s",
				T(h3_stick_names[PIN(profile->controller_settings.joystick_preset, 0, NUMBER_OF_JOYSTICK_PRESETS - 1)]),
				profile->controller_settings.invert_look ? ", " : "",
				profile->controller_settings.invert_look ? T("INVERTED") : "");
			snprintf(text, sizeof(text), "%s: %s\n%s: %s\n%s: %s\n%s", T("CURRENT LEVEL"), level_name, T("DIFFICULTY"),
				T(h3_difficulty_names[PIN(difficulty, 0, 3)]), T("CONTROLS"), controls,
				h3_profile_same(h3_profiles.indices[item], h3_custom.index) ? T("The profile you are using.") :
				T("Cross: use this profile. Square: delete it."));
		}
	}
	else
	{
		snprintf(heading, sizeof(heading), "%s", T("NEW PROFILE"));
		snprintf(text, sizeof(text), "%s", T("Make a profile of your own: type its name.\nYou switch to it once it is made."));
	}
	text_show(named(screen, "colour_name"), heading);
	if ((found = named(screen, "na_help")) != NULL)
	{
		widget_instance_set_visibility_recursive(found, TRUE);
		text_show(found, text);
	}
}

/* (port online custom: the editor's list, after port h3 custom) */
static void online_custom_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct player_profile *profile = player_ui_get_edit_player_profile();
	struct widget_instance *child, *first = NULL, *found;
	short slot = 0, focused_row = NONE, index;
	char text[64];

	if (list != online_custom.list)
	{
		/* (the editor opened again: its first tab) */
		online_custom.list = list;
		online_custom.tab = online_custom.deleting ? _online_custom_tab_profile : _online_custom_tab_armory;
		online_custom.shown_tab = NONE;
		/* (back from the delete question: the Spartan asked for again) */
		if (online_custom.deleting)
		{
			online_custom.deleting = FALSE;
			h3_custom.preview_asked = FALSE;
			h3_custom.preview_shown = FALSE;
			h3_profiles.read = FALSE;
		}
	}
	/* (PROFILE: the profiles read, a new one's name typed) */
	if (online_custom.tab == _online_custom_tab_profile)
		online_profile_tab_update();
	for (child = list->child; child; child = child->next)
	{
		short row = online_custom_row_index(child->name);
		boolean shown = row != NONE && online_custom_rows[row].tab == online_custom.tab;

		if (row != NONE && !strcmp(child->name, "op_flame"))
			shown = shown && h3_custom.flame_listed;
		if (shown && !strncmp(child->name, "pr_", 3))
			shown = online_profile_row_show(child);
		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		if (!shown)
			continue;
		child->vertical_offset = (short)(slot++ * ONLINE_CUSTOM_ROW_HEIGHT);
		/* (the arrows the focused row's alone, as h3_custom_update has them:
		shown again above with the row) */
		if ((found = named(child, "arrow_left")) != NULL)
			widget_instance_set_visibility_recursive(found, child == list->focused_child);
		if ((found = named(child, "arrow_right")) != NULL)
			widget_instance_set_visibility_recursive(found, child == list->focused_child);
		if (!first)
			first = child;
		if (child == list->focused_child)
			focused_row = row;
		if (online_custom_rows[row].not_yet)
		{
			text_show(named(child, "value"), T("N/A"));
			child->animation.current_frame_index = child == list->focused_child ? 1 : 0;
		}
	}
	/* (a new tab: its first row) */
	if (online_custom.shown_tab != online_custom.tab || focused_row == NONE)
	{
		online_custom.shown_tab = online_custom.tab;
		if (first && first != list->focused_child)
			ui_widget_port_give_focus(list, first);
		focused_row = first ? online_custom_row_index(first->name) : NONE;
	}
	for (index = 0; index < ONLINE_CUSTOM_TABS; index++)
	{
		snprintf(text, sizeof(text), "tab_%d", index);
		if ((found = named(screen, text)) != NULL)
			found->animation.current_frame_index = index == online_custom.tab ? 1 : 0;
	}
	/* (the help: the Halo 3 editor's for its rows, the "not yet" words for
	the greyed ones) */
	if (focused_row != NONE)
	{
		char const *not_yet = online_custom_rows[focused_row].not_yet;

		if ((found = named(screen, "custom_help")) != NULL)
			widget_instance_set_visibility_recursive(found, !not_yet);
		if ((found = named(screen, "na_help")) != NULL)
		{
			widget_instance_set_visibility_recursive(found, not_yet != NULL);
			if (not_yet)
				text_show(found, T(not_yet));
		}
		if (focused_row != online_custom.help_row)
		{
			online_custom.help_row = focused_row;
			platform_log("menus: online customize: row %s", online_custom_rows[focused_row].row);
		}
	}
	/* (PROFILE's buttons in the foot: Square deletes, Triangle makes one) */
	{
		static char const *const profile_foot[] = { "x_butn", "delete", "y_butn", "new" };

		for (index = 0; index < (short)NUMBEROF(profile_foot); index++)
		{
			if ((found = named(screen, profile_foot[index])) != NULL)
				widget_instance_set_visibility_recursive(found, online_custom.tab == _online_custom_tab_profile);
		}
	}
	/* (PROFILE: the focused profile's card in the help's place) */
	if (online_custom.tab == _online_custom_tab_profile && focused_row != NONE)
	{
		online_profile_card(screen, focused_row);
		return;
	}
	/* (COLORS: the grid, its chosen colour framed, and the colour's hex) */
	if ((found = named(screen, "grid")) != NULL)
		widget_instance_set_visibility_recursive(found, online_custom.tab == _online_custom_tab_colors);
	if (profile)
	{
		short frame = h3_swatch_frame(profile->primary_color_index);

		if ((found = named(screen, "grid_select")) != NULL)
		{
			found->horizontal_offset = (short)((frame % ONLINE_CUSTOM_GRID_COLUMNS) * ONLINE_CUSTOM_GRID_WIDTH);
			found->vertical_offset = (short)((frame / ONLINE_CUSTOM_GRID_COLUMNS) * ONLINE_CUSTOM_GRID_HEIGHT);
		}
		if (frame > 0)
			snprintf(text, sizeof(text), "#%06lX", online_custom_colours[frame - 1]);
		else
			snprintf(text, sizeof(text), "%s", T("RANDOM"));
		text_show(named(screen, "hex"), text);
		/* (the heading over the help: the colour's name on COLORS, else the
		row's) */
		if (online_custom.tab == _online_custom_tab_colors)
			snprintf(text, sizeof(text), "%s", T(h3_color_names[frame]));
		else
			snprintf(text, sizeof(text), "%s", focused_row != NONE ? T(online_custom_rows[focused_row].label) : "");
		text_show(named(screen, "colour_name"), text);
	}
}

static boolean typing_available(void);

/* ---- the Halo 3 GAME TYPES (halo3/gametypes.xml): it stands for the
Xbox's game type list (playlist_select\gametype_select_screen, the lobby's
EDIT GAME TYPES) and its edit screens. It reads and writes the game types as
those screens do, with the same functions on the same files
(z:\saved\playlists, blam.lst and SaveMeta.xbx):

- the list is the Xbox's (multiplayer_profiles_list_initialize:
  playlist_profiles_enumerate_available_to_local_player_index, the player's
  then the game's own, z:\saved\playlists\default_playlist), each named as
  the Xbox's list names it (playlist_profile_get_display_name);
- a game type is read as "playlist profile begin editing" reads it
  (playlist_profile_get), and saved as "playlist profile save changes"
  saves it (player_ui_save_profile: playlist_profile_save, which cleans it
  up with game_engine_variant_cleanup, writes the file, its checksum and its
  SaveMeta.xbx name; one of the game's own saved as a new one with
  playlist_profile_new, as the Xbox's save-as of a renamed default does),
  then remembered as the last one used, as the Xbox's save remembers it: the
  lobby's GAME is the game type saved;
- its settings are the ones the Xbox's edit screens change, each with the
  values their spinners offer (playlist_profile_change_player_options,
  _item_options, _indicator_options, _slayer_rules, _ctf_rules,
  _oddball_rules, _koth_rules, _racing_rules, playlist_profile_set_game
  _engine): a game type saved here reads back the same in the Halo CE
  menus' screens. A game type holds no time limit, friendly fire or
  betrayal penalty (the PC version's options, game_variant_options: a PC
  host's), and the help says so;
- DELETE is "final del playlist profile" (playlist_profile_delete: the
  player's own, never the game's); COPY and NEW make one as "create&edit
  playlist profile" does (playlist_profile_new), with the rules of the one
  copied or the base chosen. */

#define H3_GAMETYPES_ROWS 10
#define H3_GAMETYPE_BASE_ROWS 12
#define H3_GAMETYPES_MAXIMUM 100
#define H3_GAMETYPE_OPTION_ROWS 8
/* (a game type's name: its variant's human_readable_game_description,
eleven characters and the end: playlist_profile.c's
MAXIMUM_GAME_VARIANT_NAME_LENGTH) */
#define H3_GAMETYPE_NAME_LENGTH 11
/* (the variant's flags: one of the game's own, playlist_profile.c's
_game_variant_is_system_default_bit) */
#define H3_GAMETYPE_SYSTEM_DEFAULT_FLAG 0x0001

/* the editor's groups of settings, in its rows' order (after NAME and GAME) */
enum
{
	_h3_gametype_general,
	_h3_gametype_respawn,
	_h3_gametype_player,
	_h3_gametype_items,
	_h3_gametype_team,
	_h3_gametype_rules,
	NUMBER_OF_H3_GAMETYPE_GROUPS
};

/* the settings, each a field of the variant (game_engine.h) */
enum
{
	_h3_gametype_score,
	_h3_gametype_lives,
	_h3_gametype_respawn_time,
	_h3_gametype_respawn_growth,
	_h3_gametype_suicide_penalty,
	_h3_gametype_shields,
	_h3_gametype_health,
	_h3_gametype_invisible,
	_h3_gametype_odd_man_out,
	_h3_gametype_equipment,
	_h3_gametype_indicator,
	_h3_gametype_radar,
	_h3_gametype_friends,
	_h3_gametype_weapons,
	_h3_gametype_grenades,
	_h3_gametype_vehicles,
	_h3_gametype_teams,
	_h3_gametype_team_scoring,
	_h3_gametype_death_bonus,
	_h3_gametype_kill_penalty,
	_h3_gametype_kill_in_order,
	_h3_gametype_assault,
	_h3_gametype_single_flag,
	_h3_gametype_flag_must_reset,
	_h3_gametype_flag_at_home,
	_h3_gametype_ball_type,
	_h3_gametype_ball_count,
	_h3_gametype_random_start,
	_h3_gametype_ball_speed,
	_h3_gametype_trait_with_ball,
	_h3_gametype_trait_without_ball,
	_h3_gametype_moving_hill,
	_h3_gametype_race_type,
	/* (Infection's: this machine's settings, not the variant's - the
	settings panel's Infection page, infection.c's infection.* -, so the
	rules log and the dirty check leave them out) */
	_h3_gametype_first_zombies,
	_h3_gametype_zombie_speed,
	_h3_gametype_round_time,
	_h3_gametype_zombie_respawn,
	_h3_gametype_zombie_shields,
	_h3_gametype_tough_zombies,
	NUMBER_OF_H3_GAMETYPE_SETTINGS
};

/* Infection's settings: their variables, as the settings panel's
Infection page sets them, and their values (its rows') */
static struct
{
	char const *variable;
	short count;
	long values[9];
} const h3_gametype_infection_settings[] =
{
	{ "HALO_INFECTION_ZOMBIES", 6, { 4, 6, 8, 10, 12, 16 } },
	{ "HALO_INFECTION_SPEED", 6, { 100, 110, 115, 125, 135, 150 } },
	{ "HALO_INFECTION_TIME", 9, { 0, 3, 5, 6, 8, 10, 15, 20, 30 } },
	{ "HALO_INFECTION_RESPAWN", 5, { 3, 5, 8, 10, 15 } },
	{ "HALO_INFECTION_SHIELDS", 2, { 0, 1 } },
	{ "HALO_INFECTION_TOUGH", 2, { 0, 1 } },
};

/* what a pending open is */
enum
{
	_h3_gametype_open_none,
	_h3_gametype_open_editor,
	_h3_gametype_open_delete,
};

static char const *const h3_gametype_editor_rows[] =
{
	"op_name", "op_game", "op_general", "op_respawn", "op_player", "op_items", "op_team", "op_rules", "op_save",
};

/* the games, in the GAME row's order (the Xbox's game list's, Slayer first) */
static long const h3_gametype_engines[] =
{
	game_engine_slayer, game_engine_ctf, game_engine_oddball, game_engine_king, game_engine_race,
};

static struct
{
	/* the game types listed: the player's (saved_count of them), then the
	game's own; each one's file and name, and whether its file is good */
	long indices[H3_GAMETYPES_MAXIMUM];
	char names[H3_GAMETYPES_MAXIMUM][48];
	boolean damaged[H3_GAMETYPES_MAXIMUM];
	short count;
	short saved_count;
	/* the list's rows (they scroll: browser_list.h) on its items,
	CREATE NEW GAME TYPE then the game types; the item chosen; a trigger's
	page */
	struct browser_view view;
	short cursor;
	short page;
	/* the game type to choose when the list is next read (NONE: the
	lobby's) */
	long focus_index;
	char status[192];
	char said[256];
	/* a card's game type, as last read (NONE: none) */
	long card_index;
	struct game_variant card;
	boolean card_good;
	/* the game type an action is for */
	long chosen_index;
	char chosen_name[48];
	boolean chosen_builtin;
	/* the bases' rows (the game's own) */
	struct browser_view base_view;
	short base_cursor;
	short base_page;
	/* the editor: the game type edited (edit_index NONE: a new one, not
	saved yet), as read and as changed; the group of settings open */
	boolean editing;
	long edit_index;
	struct game_variant original;
	struct game_variant current;
	short group;
	char edit_status[192];
	char edit_said[512];
	/* what the main loop opens over the list (the editor, the delete
	question), and the editor gone back from (a question's SAVE CHANGES,
	DISCARD CHANGES) */
	short open_pending;
	boolean leave_pending;
	/* a game type saved, copied, renamed or deleted since the lobby was
	last opened */
	boolean changed;
} h3_gametypes = { { 0 }, { { 0 } }, { 0 }, 0, 0, { 0 }, 0, 0, NONE, "", "", NONE };

/* a game type's name (the variant's) as UTF-8 */
static void h3_gametype_name(char *out, int size, struct game_variant const *variant)
{
	wchar_t name[H3_GAMETYPE_NAME_LENGTH + 1];

	memcpy(name, variant->human_readable_game_description, sizeof(wchar_t) * H3_GAMETYPE_NAME_LENGTH);
	name[H3_GAMETYPE_NAME_LENGTH] = 0;
	h3_utf8(out, size, name);
}

static char const *h3_gametype_engine_name(long engine)
{
	switch (engine)
	{
	case game_engine_ctf:
		return T("CTF");
	case game_engine_oddball:
		return T("ODDBALL");
	case game_engine_king:
		return T("KING OF THE HILL");
	case game_engine_race:
		return T("RACE");
	default:
		return T("SLAYER");
	}
}

/* the game a game type plays, as shown: Infection's (a Slayer variant with
its magic, infection.c) its own name, so a copy of it reads as Infection */
static char const *h3_gametype_kind_name(struct game_variant const *variant)
{
	if (infection_variant(variant))
		return T("INFECTION");
	return h3_gametype_engine_name(variant->game_engine_index);
}

/* (the logs') the game's name, in English */
static char const *h3_gametype_engine_key(long engine)
{
	switch (engine)
	{
	case game_engine_ctf:
		return "ctf";
	case game_engine_oddball:
		return "oddball";
	case game_engine_king:
		return "king";
	case game_engine_race:
		return "race";
	case game_engine_slayer:
		return "slayer";
	default:
		return "none";
	}
}

/* the settings of a group, for a game (count); INFECTION: an Infection
game type's (its own group, Infection's settings, in Slayer's place) */
static int h3_gametype_group_settings_of(short group, long engine, boolean infection, short *settings)
{
	int count = 0;

	if (group == _h3_gametype_rules && infection)
	{
		short setting;

		for (setting = _h3_gametype_first_zombies; setting <= _h3_gametype_tough_zombies; setting++)
			settings[count++] = setting;
		return count;
	}

	switch (group)
	{
	case _h3_gametype_general:
		settings[count++] = _h3_gametype_score;
		settings[count++] = _h3_gametype_lives;
		break;
	case _h3_gametype_respawn:
		settings[count++] = _h3_gametype_respawn_time;
		settings[count++] = _h3_gametype_respawn_growth;
		settings[count++] = _h3_gametype_suicide_penalty;
		break;
	case _h3_gametype_player:
		settings[count++] = _h3_gametype_shields;
		settings[count++] = _h3_gametype_health;
		settings[count++] = _h3_gametype_invisible;
		settings[count++] = _h3_gametype_odd_man_out;
		settings[count++] = _h3_gametype_equipment;
		settings[count++] = _h3_gametype_indicator;
		settings[count++] = _h3_gametype_radar;
		settings[count++] = _h3_gametype_friends;
		break;
	case _h3_gametype_items:
		settings[count++] = _h3_gametype_weapons;
		settings[count++] = _h3_gametype_grenades;
		settings[count++] = _h3_gametype_vehicles;
		break;
	case _h3_gametype_team:
		settings[count++] = _h3_gametype_teams;
		if (engine == game_engine_race)
			settings[count++] = _h3_gametype_team_scoring;
		break;
	case _h3_gametype_rules:
		switch (engine)
		{
		case game_engine_ctf:
			settings[count++] = _h3_gametype_assault;
			settings[count++] = _h3_gametype_single_flag;
			settings[count++] = _h3_gametype_flag_must_reset;
			settings[count++] = _h3_gametype_flag_at_home;
			break;
		case game_engine_oddball:
			settings[count++] = _h3_gametype_ball_type;
			settings[count++] = _h3_gametype_ball_count;
			settings[count++] = _h3_gametype_random_start;
			settings[count++] = _h3_gametype_ball_speed;
			settings[count++] = _h3_gametype_trait_with_ball;
			settings[count++] = _h3_gametype_trait_without_ball;
			break;
		case game_engine_king:
			settings[count++] = _h3_gametype_moving_hill;
			break;
		case game_engine_race:
			settings[count++] = _h3_gametype_race_type;
			break;
		default:
			settings[count++] = _h3_gametype_death_bonus;
			settings[count++] = _h3_gametype_kill_penalty;
			settings[count++] = _h3_gametype_kill_in_order;
			break;
		}
		break;
	}
	return count;
}

/* the settings of a group, for a game type */
static int h3_gametype_group_settings(short group, struct game_variant const *variant, short *settings)
{
	return h3_gametype_group_settings_of(group, variant->game_engine_index, infection_variant(variant), settings);
}

static char const *h3_gametype_group_name(short group, struct game_variant const *variant)
{
	long engine = variant->game_engine_index;

	static char const *const names[NUMBER_OF_H3_GAMETYPE_GROUPS] =
	{
		N_("GENERAL SETTINGS"), N_("RESPAWN SETTINGS"), N_("PLAYER SETTINGS"), N_("WEAPONS AND VEHICLES"),
		N_("TEAM SETTINGS"), "",
	};

	if (group != _h3_gametype_rules)
		return T(names[PIN(group, 0, NUMBER_OF_H3_GAMETYPE_GROUPS - 1)]);
	if (infection_variant(variant))
		return T("INFECTION SETTINGS");
	switch (engine)
	{
	case game_engine_ctf:
		return T("CTF SETTINGS");
	case game_engine_oddball:
		return T("ODDBALL SETTINGS");
	case game_engine_king:
		return T("KING OF THE HILL SETTINGS");
	case game_engine_race:
		return T("RACE SETTINGS");
	default:
		return T("SLAYER SETTINGS");
	}
}

/* a group's note: what a game type holds none of, and where it is set */
static char const *h3_gametype_group_note(short group, struct game_variant const *variant)
{
	if (group == _h3_gametype_rules && infection_variant(variant))
		return T("This PS Vita's, for every Infection game it hosts: changed\nat once, as on the settings panel's Infection page.");
	switch (group)
	{
	case _h3_gametype_general:
		return T("A game type holds no time limit: a game lasts until its score\n(a PC host's game, or a dedicated server's, may set one).");
	case _h3_gametype_respawn:
		return T("A game type holds no betrayal penalty: a team killer respawns\nas anyone does (a PC host's game may add one).");
	case _h3_gametype_team:
		return T("A game type holds no friendly fire setting: team games hurt\nteammates as the Xbox game does (a PC host's game may change it).");
	default:
		return "";
	}
}

static char const *h3_gametype_setting_label(short setting, long engine)
{
	static char const *const labels[NUMBER_OF_H3_GAMETYPE_SETTINGS] =
	{
		"", N_("NUMBER OF LIVES"), N_("RESPAWN TIME"), N_("RESPAWN TIME GROWTH"), N_("SUICIDE PENALTY"),
		N_("SHIELDS"), N_("MAXIMUM HEALTH"), N_("INVISIBLE PLAYERS"), N_("ODD MAN OUT"), N_("STARTING EQUIPMENT"),
		N_("OBJECTIVE INDICATOR"), N_("PLAYERS ON RADAR"), N_("FRIEND INDICATORS"), N_("WEAPON SET"),
		N_("INFINITE GRENADES"), N_("VEHICLE SET"), N_("TEAMS"), N_("TEAM SCORING"), N_("DEATH BONUS"),
		N_("KILL PENALTY"), N_("KILL IN ORDER"), N_("ASSAULT"), N_("SINGLE FLAG"), N_("FLAG MUST RESET"),
		N_("FLAG AT HOME TO SCORE"), N_("BALL TYPE"), N_("NUMBER OF BALLS"), N_("RANDOM START"),
		N_("SPEED WITH BALL"), N_("TRAIT WITH BALL"), N_("TRAIT WITHOUT BALL"), N_("MOVING HILL"), N_("RACE TYPE"),
		N_("FIRST ZOMBIES"), N_("ZOMBIE SPEED"), N_("ROUND TIME"), N_("ZOMBIE RESPAWN"), N_("ZOMBIE SHIELDS"),
		N_("TOUGH ZOMBIES"),
	};

	if (setting == _h3_gametype_score)
	{
		switch (engine)
		{
		case game_engine_slayer:
			return T("KILLS TO WIN");
		case game_engine_ctf:
			return T("CAPTURES TO WIN");
		case game_engine_race:
			return T("LAPS TO WIN");
		default:
			return T("SCORE TO WIN");
		}
	}
	return T(labels[PIN(setting, 0, NUMBER_OF_H3_GAMETYPE_SETTINGS - 1)]);
}

static char const *h3_gametype_setting_help(short setting, struct game_variant const *variant)
{
	static char const *const helps[NUMBER_OF_H3_GAMETYPE_SETTINGS] =
	{
		"",
		N_("How many times a player can respawn. UNLIMITED: as\noften as they die."),
		N_("How long a dead player waits to respawn."),
		N_("Added to the respawn time with each death."),
		N_("Added to the respawn time of a player who kills\nthemselves."),
		N_("Whether players have energy shields."),
		N_("How much damage a player takes before dying: 100%\nis the usual."),
		N_("Every player is always camouflaged."),
		N_("The last player to die stays out of the game until\nsomeone else dies."),
		N_("CUSTOM: the weapons and grenades the map gives for\nthis game. GENERIC: the game's standard ones."),
		N_("How the objective is shown: on the motion tracker,\nas a waypoint on screen, or not at all."),
		N_("Whether the motion tracker shows other players."),
		N_("Teammates' names over their heads (waypoints)."),
		N_("The weapons placed on the map and carried at the\nstart. NO GRENADES: the usual weapons, no grenades."),
		N_("Grenades never run out."),
		N_("The vehicles on the map. MAP DEFAULT: the ones the\nmap places for this game."),
		N_("Players play in teams (red and blue)."),
		N_("A team's laps: its slowest player's (MINIMUM), its\nfastest's (MAXIMUM), or all of its players' (SUM)."),
		N_("Each death makes a player a little faster."),
		N_("Each kill makes a player a little slower."),
		N_("Only your target counts: kill the player named\non screen to score."),
		N_("Each team's flag is at the other team's base: they\nattack and defend the other way around."),
		N_("One flag: one team defends it, the other attacks, and\nthey switch sides after this long. OFF: two flags."),
		N_("A dropped flag can't be returned by touching it: it\nresets by itself after a while."),
		N_("A team scores only while its own flag is at home."),
		N_("NORMAL: hold the ball to score. REVERSE TAG: the\nball carrier is hunted. JUGGERNAUT: kill to score."),
		N_("How many balls are on the map."),
		N_("The ball starts at a random place on the map."),
		N_("How fast the ball carrier runs."),
		N_("What holding the ball does to its carrier."),
		N_("What players without the ball get."),
		N_("The hill moves to another place every minute."),
		N_("NORMAL: the checkpoints in order. ANY ORDER: all of\nthem, any order. RALLY: one at a time, where shown."),
		/* (the settings panel's Infection rows' help) */
		N_("Zombies at the start, chosen at random (one at least)"),
		N_("How fast zombies run; humans run at 100"),
		N_("The humans still alive then win"),
		N_("How soon a dead zombie comes back"),
		N_("Off: zombies have their body alone"),
		N_("On: zombies take half the damage"),
	};

	if (setting == _h3_gametype_score)
	{
		switch (variant->game_engine_index)
		{
		case game_engine_slayer:
			return T("The kills a player (or team) needs to win.");
		case game_engine_ctf:
			return T("The flag captures a team needs to win.");
		case game_engine_king:
			return T("The minutes in the hill a player (or team) needs\nto win.");
		case game_engine_race:
			return T("The laps a player (or team) needs to win.");
		default:
			return variant->game_engine_variant.oddball.oddball_ball_type == 2 ?
				T("The points a player (or team) needs to win: a kill\nas juggernaut, or of the juggernaut.") :
				T("The minutes holding the ball a player (or team)\nneeds to win.");
		}
	}
	if (setting == _h3_gametype_teams && variant->game_engine_index == game_engine_ctf)
		return T("Capture the Flag is always played in teams.");
	return T(helps[PIN(setting, 0, NUMBER_OF_H3_GAMETYPE_SETTINGS - 1)]);
}

/* the values a setting takes (the Xbox's spinners'), count */
static int h3_gametype_values(short setting, struct game_variant const *variant, long *values)
{
	static long const respawns[] = { 0, 150, 300, 450 };
	static long const lives[] = { 0, 1, 3, 5 };
	static long const health[] = { 50, 100, 150, 200, 300, 400 };
	static long const slayer_scores[] = { 5, 10, 15, 25, 50 };
	static long const ctf_scores[] = { 1, 3, 5, 10, 15 };
	static long const king_scores[] = { 1, 2, 5, 10, 15 };
	static long const race_scores[] = { 1, 3, 5, 10, 15, 25 };
	static long const single_flag[] = { 0, 1800, 3600, 5400, 9000, 18000 };
	long const *list = NULL;
	int count = 0, index;

	if (setting >= _h3_gametype_first_zombies && setting <= _h3_gametype_tough_zombies)
	{
		list = h3_gametype_infection_settings[setting - _h3_gametype_first_zombies].values;
		count = h3_gametype_infection_settings[setting - _h3_gametype_first_zombies].count;
		for (index = 0; index < count; index++)
			values[index] = list[index];
		return count;
	}
	switch (setting)
	{
	case _h3_gametype_score:
		switch (variant->game_engine_index)
		{
		case game_engine_slayer:
			list = slayer_scores, count = NUMBEROF(slayer_scores);
			break;
		case game_engine_ctf:
			list = ctf_scores, count = NUMBEROF(ctf_scores);
			break;
		case game_engine_race:
			list = race_scores, count = NUMBEROF(race_scores);
			break;
		default:
			/* (king and oddball: the same) */
			list = king_scores, count = NUMBEROF(king_scores);
			break;
		}
		break;
	case _h3_gametype_lives:
		list = lives, count = NUMBEROF(lives);
		break;
	case _h3_gametype_respawn_time:
	case _h3_gametype_respawn_growth:
	case _h3_gametype_suicide_penalty:
		list = respawns, count = NUMBEROF(respawns);
		break;
	case _h3_gametype_health:
		list = health, count = NUMBEROF(health);
		break;
	case _h3_gametype_single_flag:
		list = single_flag, count = NUMBEROF(single_flag);
		break;
	case _h3_gametype_indicator:
	case _h3_gametype_team_scoring:
	case _h3_gametype_ball_type:
	case _h3_gametype_ball_speed:
	case _h3_gametype_race_type:
		count = 3;
		break;
	case _h3_gametype_trait_with_ball:
	case _h3_gametype_trait_without_ball:
		count = 4;
		break;
	case _h3_gametype_vehicles:
		count = 5;
		break;
	case _h3_gametype_weapons:
		count = 11;
		break;
	case _h3_gametype_ball_count:
		for (index = 0; index < MAXIMUM_ODDBALLS; index++)
			values[index] = index + 1;
		return MAXIMUM_ODDBALLS;
	case _h3_gametype_teams:
		/* (CTF: teams always, game_engine_variant_cleanup) */
		if (variant->game_engine_index == game_engine_ctf)
		{
			values[0] = TRUE;
			return 1;
		}
		count = 2;
		break;
	default:
		/* (OFF, ON) */
		count = 2;
		break;
	}
	for (index = 0; index < count; index++)
		values[index] = list ? list[index] : index;
	return count;
}

static long h3_gametype_flag(struct game_variant const *variant, unsigned long flag)
{
	return (variant->universal_variant.flags & flag) != 0;
}

static void h3_gametype_flag_set(struct game_variant *variant, unsigned long flag, long on)
{
	if (on)
		variant->universal_variant.flags |= flag;
	else
		variant->universal_variant.flags &= ~flag;
}

/* a setting's value, as h3_gametype_values has them */
static long h3_gametype_get(short setting, struct game_variant const *variant)
{
	struct universal_variant const *universal = &variant->universal_variant;
	union game_engine_variant const *engine = &variant->game_engine_variant;

	if (setting >= _h3_gametype_first_zombies && setting <= _h3_gametype_tough_zombies)
	{
		struct infection_settings infection;

		infection_settings_configured(&infection);
		switch (setting)
		{
		case _h3_gametype_first_zombies: return infection.players_per_first_zombie;
		case _h3_gametype_zombie_speed: return infection.zombie_speed_percent;
		case _h3_gametype_round_time: return infection.time_limit_minutes;
		case _h3_gametype_zombie_respawn: return infection.zombie_respawn_seconds;
		case _h3_gametype_zombie_shields: return infection.zombie_shields != FALSE;
		default: return infection.zombie_tough != FALSE;
		}
	}
	switch (setting)
	{
	case _h3_gametype_score: return universal->score_to_win;
	case _h3_gametype_lives: return universal->lives;
	case _h3_gametype_respawn_time: return universal->respawn_time;
	case _h3_gametype_respawn_growth: return universal->respawn_time_growth;
	case _h3_gametype_suicide_penalty: return universal->suicide_penalty;
	case _h3_gametype_shields: return !h3_gametype_flag(variant, FLAG(_game_variant_no_shields_bit));
	case _h3_gametype_health: return (long)(universal->health * 100.0f + 0.5f);
	case _h3_gametype_invisible: return h3_gametype_flag(variant, FLAG(_game_variant_always_invisible_bit));
	case _h3_gametype_odd_man_out: return universal->odd_man_out != FALSE;
	/* (the generic starting equipment's bit: game_engine.c's
	_game_variant_generic_starting_equipment_bit, 5) */
	case _h3_gametype_equipment: return h3_gametype_flag(variant, FLAG(5));
	case _h3_gametype_indicator: return universal->goal_radar;
	case _h3_gametype_radar: return h3_gametype_flag(variant, FLAG(_game_variant_draw_object_in_motion_sensor_bit));
	case _h3_gametype_friends: return h3_gametype_flag(variant, FLAG(_game_variant_allow_friendly_navpoints_bit));
	case _h3_gametype_weapons: return universal->weapon_set;
	case _h3_gametype_grenades: return h3_gametype_flag(variant, FLAG(_game_variant_infinite_grenades_bit));
	case _h3_gametype_vehicles: return universal->vehicle_set;
	case _h3_gametype_teams: return universal->teams != FALSE;
	case _h3_gametype_team_scoring: return engine->race.team_scoring;
	case _h3_gametype_death_bonus: return !engine->slayer.no_death_bonus;
	case _h3_gametype_kill_penalty: return !engine->slayer.no_kill_penalty;
	case _h3_gametype_kill_in_order: return engine->slayer.kill_in_order != FALSE;
	case _h3_gametype_assault: return engine->ctf.assault != FALSE;
	case _h3_gametype_single_flag: return engine->ctf.single_flag_time;
	case _h3_gametype_flag_must_reset: return engine->ctf.flag_must_reset != FALSE;
	case _h3_gametype_flag_at_home: return engine->ctf.flag_at_home_to_score != FALSE;
	case _h3_gametype_ball_type: return engine->oddball.oddball_ball_type;
	case _h3_gametype_ball_count: return engine->oddball.ball_spawn_count;
	case _h3_gametype_random_start: return engine->oddball.random_start != FALSE;
	case _h3_gametype_ball_speed: return engine->oddball.speed_with_ball;
	case _h3_gametype_trait_with_ball: return engine->oddball.trait_with_ball;
	case _h3_gametype_trait_without_ball: return engine->oddball.trait_without_ball;
	case _h3_gametype_moving_hill: return engine->king.moving_hill != FALSE;
	case _h3_gametype_race_type: return engine->race.race_type;
	}
	return 0;
}

/* a setting changed, as the Xbox's edit screens change it */
static void h3_gametype_set(short setting, struct game_variant *variant, long value)
{
	struct universal_variant *universal = &variant->universal_variant;
	union game_engine_variant *engine = &variant->game_engine_variant;

	/* (Infection's: this machine's, set as the settings panel sets them) */
	if (setting >= _h3_gametype_first_zombies && setting <= _h3_gametype_tough_zombies)
	{
		char text[16];

		snprintf(text, sizeof(text), "%ld", value);
		setting_set(h3_gametype_infection_settings[setting - _h3_gametype_first_zombies].variable, text);
		return;
	}
	switch (setting)
	{
	case _h3_gametype_score: universal->score_to_win = value; break;
	case _h3_gametype_lives: universal->lives = value; break;
	case _h3_gametype_respawn_time: universal->respawn_time = value; break;
	case _h3_gametype_respawn_growth: universal->respawn_time_growth = value; break;
	case _h3_gametype_suicide_penalty: universal->suicide_penalty = value; break;
	case _h3_gametype_shields: h3_gametype_flag_set(variant, FLAG(_game_variant_no_shields_bit), !value); break;
	/* (the Xbox's maximum health spinner's floats) */
	case _h3_gametype_health: universal->health = (real)value / 100.0f; break;
	case _h3_gametype_invisible: h3_gametype_flag_set(variant, FLAG(_game_variant_always_invisible_bit), value); break;
	case _h3_gametype_odd_man_out: universal->odd_man_out = (boolean)(value != 0); break;
	case _h3_gametype_equipment: h3_gametype_flag_set(variant, FLAG(5), value); break;
	case _h3_gametype_indicator: universal->goal_radar = value; break;
	case _h3_gametype_radar:
		h3_gametype_flag_set(variant, FLAG(_game_variant_draw_object_in_motion_sensor_bit), value);
		break;
	case _h3_gametype_friends:
		h3_gametype_flag_set(variant, FLAG(_game_variant_allow_friendly_navpoints_bit), value);
		break;
	case _h3_gametype_weapons: universal->weapon_set = value; break;
	case _h3_gametype_grenades: h3_gametype_flag_set(variant, FLAG(_game_variant_infinite_grenades_bit), value); break;
	case _h3_gametype_vehicles: universal->vehicle_set = value; break;
	case _h3_gametype_teams:
		universal->teams = (boolean)(value != 0 || variant->game_engine_index == game_engine_ctf);
		break;
	case _h3_gametype_team_scoring: engine->race.team_scoring = value; break;
	case _h3_gametype_death_bonus: engine->slayer.no_death_bonus = (boolean)!value; break;
	case _h3_gametype_kill_penalty: engine->slayer.no_kill_penalty = (boolean)!value; break;
	case _h3_gametype_kill_in_order: engine->slayer.kill_in_order = (boolean)(value != 0); break;
	case _h3_gametype_assault: engine->ctf.assault = (boolean)(value != 0); break;
	case _h3_gametype_single_flag: engine->ctf.single_flag_time = value; break;
	case _h3_gametype_flag_must_reset: engine->ctf.flag_must_reset = (boolean)(value != 0); break;
	case _h3_gametype_flag_at_home: engine->ctf.flag_at_home_to_score = (boolean)(value != 0); break;
	case _h3_gametype_ball_type: engine->oddball.oddball_ball_type = value; break;
	case _h3_gametype_ball_count: engine->oddball.ball_spawn_count = value; break;
	case _h3_gametype_random_start: engine->oddball.random_start = (boolean)(value != 0); break;
	case _h3_gametype_ball_speed: engine->oddball.speed_with_ball = value; break;
	case _h3_gametype_trait_with_ball: engine->oddball.trait_with_ball = value; break;
	case _h3_gametype_trait_without_ball: engine->oddball.trait_without_ball = value; break;
	case _h3_gametype_moving_hill: engine->king.moving_hill = (boolean)(value != 0); break;
	case _h3_gametype_race_type: engine->race.race_type = value; break;
	}
}

/* a setting's value shown */
static void h3_gametype_value_text(short setting, struct game_variant const *variant, char *text, int size)
{
	static char const *const weapons[] =
	{
		N_("NORMAL"), N_("PISTOLS"), N_("RIFLES"), N_("PLASMA"), N_("SNIPERS"), N_("NO SNIPING"),
		N_("ROCKET LAUNCHERS"), N_("SHOTGUNS"), N_("SHORT RANGE"), N_("HUMAN"), N_("NO GRENADES"),
	};
	static char const *const vehicles[] =
	{
		N_("MAP DEFAULT"), N_("NONE"), N_("WARTHOGS"), N_("GHOSTS"), N_("SCORPIONS"),
	};
	static char const *const indicators[] = { N_("MOTION TRACKER"), N_("NAV POINTS"), N_("NONE") };
	static char const *const team_scorings[] = { N_("MINIMUM"), N_("MAXIMUM"), N_("SUM") };
	static char const *const ball_types[] = { N_("NORMAL"), N_("REVERSE TAG"), N_("JUGGERNAUT") };
	static char const *const speeds[] = { N_("SLOW"), N_("NORMAL"), N_("FAST") };
	static char const *const traits[] = { N_("NONE"), N_("INVISIBLE"), N_("EXTRA DAMAGE"), N_("DAMAGE RESISTANT") };
	static char const *const race_types[] = { N_("NORMAL"), N_("ANY ORDER"), N_("RALLY") };
	long value = h3_gametype_get(setting, variant);

#define H3_GAMETYPE_NAMED(names) \
	snprintf(text, size, "%s", value >= 0 && value < (long)NUMBEROF(names) ? T(names[value]) : "?")
	switch (setting)
	{
	case _h3_gametype_score:
		/* (king's minutes in the hill; oddball's minutes with the ball, but
		juggernaut's points) */
		if (variant->game_engine_index == game_engine_king ||
			(variant->game_engine_index == game_engine_oddball &&
				variant->game_engine_variant.oddball.oddball_ball_type != 2))
		{
			snprintf(text, size, T("%d MIN"), (int)value);
		}
		else
			snprintf(text, size, "%d", (int)value);
		break;
	case _h3_gametype_lives:
		if (value <= 0)
			snprintf(text, size, "%s", T("UNLIMITED"));
		else
			snprintf(text, size, "%d", (int)value);
		break;
	case _h3_gametype_respawn_time:
	case _h3_gametype_respawn_growth:
	case _h3_gametype_suicide_penalty:
		if (value <= 0)
			snprintf(text, size, "%s", setting == _h3_gametype_respawn_time ? T("INSTANT") : T("NONE"));
		else
			snprintf(text, size, T("%d SECONDS"), (int)((value + TICKS_PER_SECOND / 2) / TICKS_PER_SECOND));
		break;
	case _h3_gametype_health:
		snprintf(text, size, "%d%%", (int)value);
		break;
	case _h3_gametype_single_flag:
		if (value <= 0)
			snprintf(text, size, "%s", T("OFF"));
		else
			snprintf(text, size, T("%d MIN"), (int)((value + 30 * TICKS_PER_SECOND) / (60 * TICKS_PER_SECOND)));
		break;
	case _h3_gametype_equipment:
		snprintf(text, size, "%s", value ? T("GENERIC") : T("CUSTOM"));
		break;
	case _h3_gametype_indicator: H3_GAMETYPE_NAMED(indicators); break;
	case _h3_gametype_weapons: H3_GAMETYPE_NAMED(weapons); break;
	case _h3_gametype_vehicles: H3_GAMETYPE_NAMED(vehicles); break;
	case _h3_gametype_team_scoring: H3_GAMETYPE_NAMED(team_scorings); break;
	case _h3_gametype_ball_type: H3_GAMETYPE_NAMED(ball_types); break;
	case _h3_gametype_ball_speed: H3_GAMETYPE_NAMED(speeds); break;
	case _h3_gametype_trait_with_ball:
	case _h3_gametype_trait_without_ball:
		H3_GAMETYPE_NAMED(traits);
		break;
	case _h3_gametype_race_type: H3_GAMETYPE_NAMED(race_types); break;
	case _h3_gametype_ball_count:
		snprintf(text, size, "%d", (int)value);
		break;
	case _h3_gametype_first_zombies:
		snprintf(text, size, T("1 PER %d PLAYERS"), (int)value);
		break;
	case _h3_gametype_zombie_speed:
		snprintf(text, size, "%d%%", (int)value);
		break;
	case _h3_gametype_round_time:
		if (value <= 0)
			snprintf(text, size, "%s", T("NO LIMIT"));
		else
			snprintf(text, size, T("%d MIN"), (int)value);
		break;
	case _h3_gametype_zombie_respawn:
		snprintf(text, size, T("%d SECONDS"), (int)value);
		break;
	default:
		snprintf(text, size, "%s", value ? T("ON") : T("OFF"));
		break;
	}
#undef H3_GAMETYPE_NAMED
}

/* a setting stepped to its next value (step 1) or its last (-1), wrapping;
one not among them (a file made elsewhere) to the next one past it */
static void h3_gametype_step(short setting, struct game_variant *variant, int step)
{
	long values[MAXIMUM_ODDBALLS];
	int count = h3_gametype_values(setting, variant, values);
	long value = h3_gametype_get(setting, variant);
	int index, chosen = NONE;

	if (count <= 0)
		return;
	for (index = 0; index < count; index++)
	{
		if (values[index] == value)
			chosen = (index + step + count) % count;
	}
	if (chosen == NONE)
	{
		chosen = step > 0 ? 0 : count - 1;
		for (index = 0; index < count; index++)
		{
			if (step > 0 && values[index] > value)
			{
				chosen = index;
				break;
			}
			if (step < 0 && values[index] < value)
				chosen = index;
		}
	}
	h3_gametype_set(setting, variant, values[chosen]);
}

/* (the logs, in English) a variant's settings, every one its game has */
static void h3_gametype_rules_log(struct game_variant const *variant, char *text, int size)
{
	static char const *const keys[NUMBER_OF_H3_GAMETYPE_SETTINGS] =
	{
		"score", "lives", "respawn", "growth", "suicide", "shields", "health", "invisible", "odd_man_out",
		"generic_equipment", "indicator", "radar", "friends", "weapons", "infinite_grenades", "vehicles", "teams",
		"team_scoring", "death_bonus", "kill_penalty", "kill_in_order", "assault", "single_flag", "flag_must_reset",
		"flag_at_home", "ball_type", "balls", "random_start", "ball_speed", "trait_with_ball", "trait_without_ball",
		"moving_hill", "race_type",
	};
	short group;
	int length;

	length = snprintf(text, size, "%s", h3_gametype_engine_key(variant->game_engine_index));
	for (group = 0; group < NUMBER_OF_H3_GAMETYPE_GROUPS; group++)
	{
		short settings[H3_GAMETYPE_OPTION_ROWS];
		/* (the variant's own: Infection's group is this machine's) */
		int count = h3_gametype_group_settings_of(group, variant->game_engine_index, FALSE, settings), index;

		for (index = 0; index < count && length > 0 && length < size; index++)
		{
			length += snprintf(text + length, size - length, "%s%s %ld", length ? ", " : "", keys[settings[index]],
				h3_gametype_get(settings[index], variant));
		}
	}
}

/* (network_test.c's: a saved game type played) the same, for the logs */
void menu_functions_gametype_rules(struct game_variant const *variant, char *text, int size)
{
	h3_gametype_rules_log(variant, text, size);
}

/* (a card, the editor's) a variant's rules, a line each: its game and the
settings that tell it apart */
static void h3_gametype_rules_text(struct game_variant const *variant, boolean game, char *text, int size)
{
	static short const shown[] =
	{
		_h3_gametype_score, _h3_gametype_lives, _h3_gametype_teams, _h3_gametype_respawn_time, _h3_gametype_shields,
		_h3_gametype_health, _h3_gametype_weapons, _h3_gametype_vehicles,
	};
	int length = 0, index;
	char value[64];

	text[0] = 0;
	if (game)
		length = snprintf(text, size, "%s: %s", T("GAME"), h3_gametype_kind_name(variant));
	for (index = 0; index < (int)NUMBEROF(shown) && length >= 0 && length < size; index++)
	{
		h3_gametype_value_text(shown[index], variant, value, sizeof(value));
		length += snprintf(text + length, size - length, "%s%s: %s", length ? "\n" : "",
			h3_gametype_setting_label(shown[index], variant->game_engine_index), value);
	}
}

/* a game type read for a card (cached: a file's read), FALSE when it
cannot be */
static boolean h3_gametype_card(long profile_index, struct game_variant *variant)
{
	if (profile_index == NONE)
		return FALSE;
	if (profile_index != h3_gametypes.card_index)
	{
		h3_gametypes.card_index = profile_index;
		h3_gametypes.card_good = playlist_profile_get(profile_index, &h3_gametypes.card);
	}
	if (h3_gametypes.card_good)
		*variant = h3_gametypes.card;
	return h3_gametypes.card_good;
}

/* (playlist_profile_save writes on a thread of its own: playlist_profile
_read waits for it) a game type's write finished */
static void h3_gametype_write_wait(long profile_index)
{
	struct game_variant variant;

	if (profile_index != NONE)
		playlist_profile_get(profile_index, &variant);
	h3_gametypes.card_index = NONE;
}

static boolean h3_gametype_same(long a, long b)
{
	return a != NONE && b != NONE &&
		((unsigned long)a & ~PROFILE_INDEX_FLAGS) == ((unsigned long)b & ~PROFILE_INDEX_FLAGS);
}

/* whether a game type's file passes its checksum, read as
playlist_profile_get_from_path reads one (the list's valid bit is the
enumeration's, kept in the memory unit's map file from an earlier run:
playlist_profile_get gives Slayer's rules for a file damaged since) */
static boolean h3_gametype_file_good(long profile_index)
{
	char path[MAXIMUM_FILENAME_LENGTH + 1];
	struct game_variant variant;

	if (!saved_game_file_get_path_to_enclosing_directory(profile_index, path))
		return FALSE;
	csstrncat(path, "\\blam.lst", MAXIMUM_FILENAME_LENGTH);
	path[MAXIMUM_FILENAME_LENGTH] = 0;
	return playlist_profile_get_from_path(path, &variant);
}

/* (network_test.c's saved=) whether a game type's file passes its checksum */
boolean menu_functions_gametype_file_good(long profile_index)
{
	return h3_gametype_file_good(profile_index);
}

/* the game types read again (the Xbox's list's), the item chosen on the
one asked for (focus_index), else the lobby's (the last one used) */
static void h3_gametypes_read(void)
{
	long indices[H3_GAMETYPES_MAXIMUM];
	word count = H3_GAMETYPES_MAXIMUM;
	char directory[MAXIMUM_FILENAME_LENGTH + 1];
	long focus = h3_gametypes.focus_index;
	short pass, index;

	playlist_profiles_enumerate_available_to_local_player_index(0, &count, indices);
	h3_gametypes.count = 0;
	h3_gametypes.saved_count = 0;
	h3_gametypes.card_index = NONE;
	/* (the player's first, then the game's own) */
	for (pass = 0; pass < 2; pass++)
	{
		for (index = 0; index < (short)MIN(count, H3_GAMETYPES_MAXIMUM); index++)
		{
			wchar_t name[128];
			short slot = h3_gametypes.count;
			boolean builtin;

			if (indices[index] == NONE)
				continue;
			builtin = TEST_FLAG(indices[index], _saved_game_file_index_read_only_bit) != 0;
			if (builtin != (pass == 1))
				continue;
			/* (the game's own: only good ones, as the lobby's GAME lists them) */
			if (builtin && !TEST_FLAG(indices[index], _saved_game_file_index_valid_bit))
				continue;
			h3_gametypes.indices[slot] = indices[index];
			h3_gametypes.damaged[slot] = !TEST_FLAG(indices[index], _saved_game_file_index_valid_bit) ||
				(!builtin && !h3_gametype_file_good(indices[index]));
			h3_gametypes.names[slot][0] = 0;
			if (playlist_profile_get_display_name(indices[index], name))
				h3_utf8(h3_gametypes.names[slot], sizeof(h3_gametypes.names[slot]), name);
			h3_gametypes.count++;
			if (!builtin)
				h3_gametypes.saved_count++;
		}
	}
	if (focus == NONE && saved_game_file_retrieve_last_used_multiplayer_variant_directory(directory))
		focus = saved_game_file_find_profile_index_for_directory_path(directory, _saved_game_file_type_game_variant);
	h3_gametypes.cursor = 0;
	for (index = 0; index < h3_gametypes.count && focus != NONE; index++)
	{
		if (h3_gametype_same(h3_gametypes.indices[index], focus))
			h3_gametypes.cursor = (short)(index + 1);
	}
	h3_gametypes.focus_index = NONE;
	{
		short damaged = 0;

		for (index = 0; index < h3_gametypes.saved_count; index++)
			damaged += h3_gametypes.damaged[index] ? 1 : 0;
		platform_log("menus: h3 gametypes: %d game types: %d of the player's (%d damaged), %d of the game's",
			h3_gametypes.count, h3_gametypes.saved_count, damaged, h3_gametypes.count - h3_gametypes.saved_count);
	}
}

/* a typed name made a game type's (UTF-16, as the variant keeps it): its
spaces at the ends dropped, not empty, at most eleven characters, and no
other game type's (the list's, the game's own too, and no other saved
file's: saved_game_file_name_unique) unless it is the one's own (original);
FALSE with the reason in status */
static boolean h3_gametype_name_check(char const *typed, wchar_t *name, wchar_t const *original, char *status, int size)
{
	char trimmed[H3_GAMETYPE_NAME_LENGTH + 1];
	char other[48];
	boolean letter = FALSE;
	int start = 0, length, index;

	while (typed[start] == ' ')
		start++;
	snprintf(trimmed, sizeof(trimmed), "%s", typed + start);
	length = (int)strlen(trimmed);
	while (length > 0 && trimmed[length - 1] == ' ')
		trimmed[--length] = 0;
	memset(name, 0, (H3_GAMETYPE_NAME_LENGTH + 1) * sizeof(wchar_t));
	for (index = 0; index < length; index++)
	{
		name[index] = (wchar_t)(unsigned char)trimmed[index];
		if ((trimmed[index] >= '0' && trimmed[index] <= '9') || (trimmed[index] >= 'a' && trimmed[index] <= 'z') ||
			(trimmed[index] >= 'A' && trimmed[index] <= 'Z'))
		{
			letter = TRUE;
		}
	}
	if (!letter)
	{
		snprintf(status, size, "%s", T("A game type's name needs a letter or a number."));
		return FALSE;
	}
	if (original)
	{
		h3_utf8(other, sizeof(other), original);
		if (!strcmp(other, trimmed))
			return TRUE;
	}
	for (index = 0; index < h3_gametypes.count; index++)
	{
		if (!_stricmp(h3_gametypes.names[index], trimmed) && !(original && !_stricmp(other, trimmed)))
		{
			snprintf(status, size, "%s", T("Another game type has that name."));
			return FALSE;
		}
	}
	if (!saved_game_file_name_unique(name))
	{
		snprintf(status, size, "%s", T("Another saved file has that name."));
		return FALSE;
	}
	return TRUE;
}

/* a name for a new game type of another's: its own, else with a number
after it ("Slayer 2"), no other game type's */
static void h3_gametype_unique_name(char const *base, char *out, int size)
{
	int number;

	snprintf(out, size, "%.*s", H3_GAMETYPE_NAME_LENGTH, base);
	for (number = 2; number < 100; number++)
	{
		int index;
		boolean taken = FALSE;

		for (index = 0; index < h3_gametypes.count && !taken; index++)
			taken = !_stricmp(h3_gametypes.names[index], out);
		if (!taken)
			return;
		{
			char suffix[8];
			int keep;

			snprintf(suffix, sizeof(suffix), " %d", number);
			keep = H3_GAMETYPE_NAME_LENGTH - (int)strlen(suffix);
			snprintf(out, size, "%.*s%s", keep, base, suffix);
		}
	}
}

static void h3_gametypes_status(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(h3_gametypes.status, sizeof(h3_gametypes.status), format, arguments);
	va_end(arguments);
	platform_log("menus: h3 gametypes: %s", h3_gametypes.status);
}

static void h3_gametype_edit_status(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(h3_gametypes.edit_status, sizeof(h3_gametypes.edit_status), format, arguments);
	va_end(arguments);
	platform_log("menus: h3 gametype editor: %s", h3_gametypes.edit_status);
}

/* (port h3 gametypes init: the list opened, or back on it) */
static void h3_gametypes_initialize(void)
{
	h3_gametypes_read();
	browser_view_reset(&h3_gametypes.view);
	h3_gametypes.page = 0;
}

/* the game type of an item (0: CREATE NEW GAME TYPE), NONE for none */
static short h3_gametypes_item_type(short item)
{
	return item >= 1 && item <= h3_gametypes.count ? (short)(item - 1) : NONE;
}

/* (A on a row) the game type of the row chosen for its options, FALSE on
CREATE NEW GAME TYPE */
static boolean h3_gametypes_choose(struct widget_instance *widget)
{
	short type = h3_gametypes_item_type((short)(h3_gametypes.view.offset + h3_child_index(widget)));

	if (type == NONE)
		return FALSE;
	h3_gametypes.cursor = (short)(type + 1);
	h3_gametypes.chosen_index = h3_gametypes.indices[type];
	h3_gametypes.chosen_builtin = type >= h3_gametypes.saved_count;
	snprintf(h3_gametypes.chosen_name, sizeof(h3_gametypes.chosen_name), "%s", h3_gametypes.names[type]);
	h3_gametypes.status[0] = 0;
	/* (the list, back from the options, on it) */
	h3_gametypes.focus_index = h3_gametypes.chosen_index;
	platform_log("menus: h3 gametypes: %s chosen (%s)", h3_gametypes.chosen_name,
		h3_gametypes.chosen_builtin ? "the game's own" : "the player's");
	return TRUE;
}

/* (A on CREATE NEW GAME TYPE, Triangle) the bases, FALSE on another row */
static boolean h3_gametypes_new(struct widget_instance *widget)
{
	if (widget && widget->name && !strncmp(widget->name, "row_", 4) &&
		h3_gametypes.view.offset + h3_child_index(widget) != 0)
	{
		return FALSE;
	}
	if (h3_gametypes.count <= h3_gametypes.saved_count)
	{
		h3_gametypes_status("%s", T("The game's own game types can't be read now."));
		return FALSE;
	}
	browser_view_reset(&h3_gametypes.base_view);
	h3_gametypes.base_cursor = 0;
	h3_gametypes.base_page = 0;
	h3_gametypes.status[0] = 0;
	return TRUE;
}

/* the editor begun on a variant (edit_index NONE: a new game type) */
static void h3_gametype_edit_begin(long profile_index, struct game_variant const *variant)
{
	char name[48], rules[512];

	h3_gametypes.editing = TRUE;
	h3_gametypes.edit_index = profile_index;
	h3_gametypes.original = *variant;
	h3_gametypes.current = *variant;
	h3_gametypes.group = _h3_gametype_general;
	h3_gametypes.edit_status[0] = 0;
	h3_gametypes.edit_said[0] = 0;
	h3_gametypes.open_pending = _h3_gametype_open_editor;
	h3_gametype_name(name, sizeof(name), variant);
	h3_gametype_rules_log(variant, rules, sizeof(rules));
	platform_log("menus: h3 gametype editor: editing %s (%s): %s", name, profile_index == NONE ? "new" : "saved",
		rules);
}

/* a new game type of one of the game's own (a base, or EDIT on one): its
rules, a name of its own, not saved yet */
static boolean h3_gametype_new_of(long profile_index, char const *base_name)
{
	struct game_variant variant;
	char name[48];
	short index;

	if (!playlist_profile_get(profile_index, &variant))
	{
		h3_gametypes_status("%s", T("That game type can't be read now."));
		return FALSE;
	}
	/* (no longer the game's own: as the Xbox's save of a renamed default) */
	variant.flags &= ~H3_GAMETYPE_SYSTEM_DEFAULT_FLAG;
	h3_gametype_unique_name(base_name, name, sizeof(name));
	memset(variant.human_readable_game_description, 0, sizeof(variant.human_readable_game_description));
	for (index = 0; index < H3_GAMETYPE_NAME_LENGTH && name[index]; index++)
		variant.human_readable_game_description[index] = (wchar_t)(unsigned char)name[index];
	h3_gametype_edit_begin(NONE, &variant);
	return TRUE;
}

/* (port h3 gametype base: a base's A) the new game type's editor, over the
list */
static boolean h3_gametype_base(struct widget_instance *widget)
{
	short type = (short)(h3_gametypes.saved_count + h3_gametypes.base_view.offset + h3_child_index(widget));

	if (type < h3_gametypes.saved_count || type >= h3_gametypes.count)
		return FALSE;
	platform_log("menus: h3 gametypes: a new game type of %s", h3_gametypes.names[type]);
	return h3_gametype_new_of(h3_gametypes.indices[type], h3_gametypes.names[type]);
}

/* whether the chosen game type's file is damaged (its checksum failed:
playlist_profile_get would give Slayer's rules in its place), said */
static boolean h3_gametype_chosen_damaged(void)
{
	short index;

	for (index = 0; index < h3_gametypes.count; index++)
	{
		if (h3_gametypes.indices[index] == h3_gametypes.chosen_index && h3_gametypes.damaged[index])
		{
			h3_gametypes_status("%s", T("This game type's file is damaged and can't be loaded. Delete it."));
			return TRUE;
		}
	}
	return FALSE;
}

/* (port h3 gametype edit: the options' EDIT) the chosen one's editor, over
the list; one of the game's own a new one of it */
static boolean h3_gametype_edit(void)
{
	struct game_variant variant;

	if (h3_gametypes.chosen_index == NONE)
		return FALSE;
	if (h3_gametypes.chosen_builtin)
	{
		if (!h3_gametype_new_of(h3_gametypes.chosen_index, h3_gametypes.chosen_name))
			return FALSE;
		snprintf(h3_gametypes.edit_status, sizeof(h3_gametypes.edit_status), "%s",
			T("The game's own game types never change: SAVE makes this one a new game type of yours."));
		return TRUE;
	}
	/* (a damaged file: said on the list, which the options go back to) */
	if (h3_gametype_chosen_damaged())
		return TRUE;
	if (!playlist_profile_get(h3_gametypes.chosen_index, &variant))
	{
		h3_gametypes_status("%s", T("That game type can't be read now."));
		return FALSE;
	}
	h3_gametype_edit_begin(h3_gametypes.chosen_index, &variant);
	return TRUE;
}

/* (COPY, RENAME) the name asked for */
static boolean h3_gametype_ask_name(short typing)
{
	char name[48];

	if (h3_gametypes.chosen_index == NONE)
		return FALSE;
	if (h3_gametype_chosen_damaged())
		return TRUE;
	if (typing == _typing_gametype_rename && h3_gametypes.chosen_builtin)
	{
		h3_gametypes_status("%s", T("The game's own game types can't be renamed: COPY makes one of yours."));
		return FALSE;
	}
	if (!typing_available())
	{
		h3_gametypes_status("%s", T("Naming a game type needs the PS Vita's keyboard."));
		return TRUE;
	}
	if (typing == _typing_gametype_copy)
	{
		h3_gametype_unique_name(h3_gametypes.chosen_name, name, sizeof(name));
		typing_begin(typing, T("The copy's name"), name, H3_GAMETYPE_NAME_LENGTH, FALSE);
	}
	else
		typing_begin(typing, T("Game type name"), h3_gametypes.chosen_name, H3_GAMETYPE_NAME_LENGTH, FALSE);
	return TRUE;
}

/* a copy of the chosen game type made, named as typed (playlist_profile_new,
then its rules saved over the new file's) */
static void h3_gametype_copy_typed(char const *typed)
{
	struct game_variant variant;
	wchar_t name[H3_GAMETYPE_NAME_LENGTH + 1];
	long profile_index;

	if (!h3_gametype_name_check(typed, name, NULL, h3_gametypes.status, sizeof(h3_gametypes.status)))
	{
		platform_log("menus: h3 gametypes: the name \"%s\" was refused: %s", typed, h3_gametypes.status);
		return;
	}
	if (!playlist_profile_get(h3_gametypes.chosen_index, &variant))
	{
		h3_gametypes_status("%s", T("That game type can't be read now."));
		return;
	}
	profile_index = playlist_profile_new(0, name);
	if (profile_index == NONE)
	{
		h3_gametypes_status(T("\"%s\" couldn't be created."), typed);
		return;
	}
	variant.flags &= ~H3_GAMETYPE_SYSTEM_DEFAULT_FLAG;
	memcpy(variant.human_readable_game_description, name, sizeof(variant.human_readable_game_description));
	playlist_profile_save(profile_index, &variant);
	h3_gametype_write_wait(profile_index);
	h3_gametypes.changed = TRUE;
	h3_gametypes.focus_index = profile_index;
	h3_gametypes_read();
	{
		char shown[48];

		h3_utf8(shown, sizeof(shown), name);
		h3_gametypes_status(T("%s copied as %s."), h3_gametypes.chosen_name, shown);
	}
}

/* the chosen game type renamed as typed, as the Xbox's name edit and SAVE
CHANGES rename it (its variant's name saved: the file and SaveMeta.xbx) */
static void h3_gametype_rename_typed(char const *typed)
{
	struct game_variant variant;
	wchar_t name[H3_GAMETYPE_NAME_LENGTH + 1];
	char shown[48];

	if (!playlist_profile_get(h3_gametypes.chosen_index, &variant))
	{
		h3_gametypes_status("%s", T("That game type can't be read now."));
		return;
	}
	if (!h3_gametype_name_check(typed, name, variant.human_readable_game_description, h3_gametypes.status,
		sizeof(h3_gametypes.status)))
	{
		platform_log("menus: h3 gametypes: the name \"%s\" was refused: %s", typed, h3_gametypes.status);
		return;
	}
	h3_utf8(shown, sizeof(shown), name);
	if (!strcmp(shown, h3_gametypes.chosen_name))
		return;
	memcpy(variant.human_readable_game_description, name, sizeof(variant.human_readable_game_description));
	playlist_profile_save(h3_gametypes.chosen_index, &variant);
	h3_gametype_write_wait(h3_gametypes.chosen_index);
	h3_gametypes.changed = TRUE;
	h3_gametypes.focus_index = h3_gametypes.chosen_index;
	h3_gametypes_read();
	h3_gametypes_status(T("%s renamed %s."), h3_gametypes.chosen_name, shown);
}

/* (Square on the list, DELETE in the options) the game type to delete
after the question; the game's own and CREATE NEW GAME TYPE none */
static boolean h3_gametype_delete_ask(boolean from_list)
{
	if (from_list)
	{
		short type = h3_gametypes_item_type(h3_gametypes.cursor);

		if (type == NONE)
			return FALSE;
		h3_gametypes.chosen_index = h3_gametypes.indices[type];
		h3_gametypes.chosen_builtin = type >= h3_gametypes.saved_count;
		snprintf(h3_gametypes.chosen_name, sizeof(h3_gametypes.chosen_name), "%s", h3_gametypes.names[type]);
		h3_gametypes.focus_index = h3_gametypes.chosen_index;
	}
	if (h3_gametypes.chosen_index == NONE)
		return FALSE;
	if (h3_gametypes.chosen_builtin)
	{
		h3_gametypes_status("%s", T("The game's own game types can't be deleted."));
		return FALSE;
	}
	platform_log("menus: h3 gametypes: delete %s? asked", h3_gametypes.chosen_name);
	return TRUE;
}

/* the question's DELETE GAME TYPE: deleted as "final del playlist profile"
deletes it (a game type of the player's, never the game's own) */
static void h3_gametype_delete(void)
{
	long profile_index = h3_gametypes.chosen_index;

	h3_gametypes.chosen_index = NONE;
	if (profile_index == NONE || TEST_FLAG(profile_index, _saved_game_file_index_read_only_bit) ||
		saved_game_file_get_type(profile_index) != _saved_game_file_type_game_variant)
	{
		return;
	}
	playlist_profile_delete(profile_index);
	h3_gametypes.changed = TRUE;
	h3_gametypes.card_index = NONE;
	h3_gametypes_read();
	h3_gametypes_status(T("%s was deleted."), h3_gametypes.chosen_name);
}

static void h3_gametype_edit_end(void)
{
	h3_gametypes.editing = FALSE;
	h3_gametypes.edit_index = NONE;
}

static boolean h3_gametype_dirty(void)
{
	return h3_gametypes.editing && (h3_gametypes.edit_index == NONE ||
		memcmp(&h3_gametypes.original, &h3_gametypes.current, sizeof(h3_gametypes.current)) != 0);
}

/* (SAVE, the question's SAVE CHANGES) the game type saved as the Xbox's
SAVE CHANGES saves it: playlist_profile_save (game_engine_variant_cleanup,
the file, its checksum, its SaveMeta.xbx name), a new one's file made first
(playlist_profile_new); remembered as the last one used (the lobby's GAME);
FALSE, said, when it can't be */
static boolean h3_gametype_save(void)
{
	struct game_variant variant;
	wchar_t name[H3_GAMETYPE_NAME_LENGTH + 1];
	char typed[48], directory[MAXIMUM_FILENAME_LENGTH + 1], rules[512];
	long profile_index = h3_gametypes.edit_index;
	boolean created = FALSE;

	if (!h3_gametypes.editing)
		return FALSE;
	variant = h3_gametypes.current;
	h3_gametype_name(typed, sizeof(typed), &variant);
	/* (a new one: its name no other's; saved over, its own allowed) */
	if (!h3_gametype_name_check(typed, name, profile_index == NONE ? NULL :
		h3_gametypes.original.human_readable_game_description, h3_gametypes.edit_status,
		sizeof(h3_gametypes.edit_status)))
	{
		platform_log("menus: h3 gametype editor: %s can't be saved: %s", typed, h3_gametypes.edit_status);
		return FALSE;
	}
	memcpy(variant.human_readable_game_description, name, sizeof(variant.human_readable_game_description));
	if (profile_index == NONE)
	{
		profile_index = playlist_profile_new(0, name);
		if (profile_index == NONE)
		{
			h3_gametype_edit_status(T("\"%s\" couldn't be saved."), typed);
			return FALSE;
		}
		created = TRUE;
	}
	else if (!h3_gametype_dirty())
	{
		platform_log("menus: h3 gametype editor: %s: no changes", typed);
		h3_gametype_edit_end();
		return TRUE;
	}
	variant.flags &= ~H3_GAMETYPE_SYSTEM_DEFAULT_FLAG;
	playlist_profile_save(profile_index, &variant);
	h3_gametype_write_wait(profile_index);
	h3_gametypes.changed = TRUE;
	if (saved_game_file_get_path_to_enclosing_directory(profile_index, directory))
		saved_game_file_remember_last_used_multiplayer_variant_directory(directory);
	h3_gametype_rules_log(&variant, rules, sizeof(rules));
	platform_log("menus: h3 gametype editor: saved %s (%s): %s", typed, created ? "a new game type" : "changed", rules);
	h3_gametypes.focus_index = profile_index;
	snprintf(h3_gametypes.status, sizeof(h3_gametypes.status), T("%s was saved."), typed);
	h3_gametype_edit_end();
	return TRUE;
}

/* (port h3 gametype category: a group's A) the group the options screen
shows */
static boolean h3_gametype_category(struct widget_instance *widget)
{
	short row;

	for (row = 0; widget && widget->name && row < (short)NUMBEROF(h3_gametype_editor_rows); row++)
	{
		if (!strcmp(widget->name, h3_gametype_editor_rows[row]))
		{
			if (row < 2 || row - 2 >= NUMBER_OF_H3_GAMETYPE_GROUPS || !h3_gametypes.editing)
				return FALSE;
			h3_gametypes.group = (short)(row - 2);
			h3_gametypes.edit_status[0] = 0;
			return TRUE;
		}
	}
	return FALSE;
}

/* (port h3 gametype next and previous) the editor's NAME (typed) and GAME,
or a setting of the options screen's group, stepped */
static boolean h3_gametype_change(struct widget_instance *widget, int step)
{
	struct game_variant *variant = &h3_gametypes.current;
	char text[64];

	if (!h3_gametypes.editing || !widget || !widget->name)
		return FALSE;
	h3_gametypes.edit_status[0] = 0;
	if (!strcmp(widget->name, "op_name"))
	{
		if (!typing_available())
		{
			h3_gametype_edit_status("%s", T("Naming a game type needs the PS Vita's keyboard."));
			return TRUE;
		}
		h3_gametype_name(text, sizeof(text), variant);
		typing_begin(_typing_gametype_name, T("Game type name"), text, H3_GAMETYPE_NAME_LENGTH, FALSE);
		return TRUE;
	}
	if (!strcmp(widget->name, "op_game"))
	{
		/* (another game: its settings as its base's - the Xbox's game list
		cleared them, playlist_profile_set_game_engine -, and the score to
		win its base's, counted as the new game counts it) */
		long engine = variant->game_engine_index;
		int index, count = (int)NUMBEROF(h3_gametype_engines);
		struct game_variant base;

		for (index = 0; index < count && h3_gametype_engines[index] != engine; index++)
			;
		index = index < count ? (index + step + count) % count : 0;
		engine = h3_gametype_engines[index];
		switch (engine)
		{
		case game_engine_ctf: build_game_variant_ctf(&base); break;
		case game_engine_oddball: build_game_variant_oddball(&base); break;
		case game_engine_king: build_game_variant_king(&base); break;
		case game_engine_race: build_game_variant_race(&base); break;
		default: build_game_variant_slayer(&base); break;
		}
		variant->game_engine_index = engine;
		variant->game_engine_variant = base.game_engine_variant;
		variant->universal_variant.score_to_win = base.universal_variant.score_to_win;
		if (engine == game_engine_ctf)
			variant->universal_variant.teams = TRUE;
		platform_log("menus: h3 gametype editor: game %s", h3_gametype_engine_key(engine));
		return TRUE;
	}
	if (!strncmp(widget->name, "op_", 3) && widget->name[3] >= '1' && widget->name[3] <= '9')
	{
		short settings[H3_GAMETYPE_OPTION_ROWS];
		int count = h3_gametype_group_settings(h3_gametypes.group, variant, settings);
		int row = widget->name[3] - '1';

		if (row >= count)
			return FALSE;
		h3_gametype_step(settings[row], variant, step);
		h3_gametype_value_text(settings[row], variant, text, sizeof(text));
		platform_log("menus: h3 gametype editor: %s %s (%ld)", h3_gametype_setting_label(settings[row],
			variant->game_engine_index), text, h3_gametype_get(settings[row], variant));
		return TRUE;
	}
	return FALSE;
}

/* a row's focus shown: its bar lit, its arrows */
static void h3_gametype_row_focus(struct widget_instance *item, boolean focused, boolean arrows)
{
	struct widget_instance *found;

	item->animation.current_frame_index = focused ? 1 : 0;
	if ((found = named(item, "arrow_left")) != NULL)
		widget_instance_set_visibility_recursive(found, focused && arrows);
	if ((found = named(item, "arrow_right")) != NULL)
		widget_instance_set_visibility_recursive(found, focused && arrows);
}

/* (port h3 gametypes) the list's rows (row_1 to row_10: row_name,
row_note), scrolling past ten; the chosen one's card; the status; a name
typed for COPY or RENAME taken */
static void h3_gametypes_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focus = NULL;
	long index, focused_row = NONE;
	int target;
	short type;
	char text[512], line[64];

	if (typing_done(_typing_gametype_copy, text, sizeof(text)))
		h3_gametype_copy_typed(text);
	if (typing_done(_typing_gametype_rename, text, sizeof(text)))
		h3_gametype_rename_typed(text);
	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		if (child == list->focused_child)
			focused_row = index;
	}
	target = browser_view_follow(&h3_gametypes.view, H3_GAMETYPES_ROWS, h3_gametypes.count + 1, (int)focused_row,
		h3_gametypes.cursor, h3_gametypes.page);
	h3_gametypes.page = 0;
	h3_gametypes.cursor = (short)MAX(target, 0);
	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		long item = h3_gametypes.view.offset + index;
		boolean shown = index < h3_gametypes.view.visible;

		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		if (!shown)
			continue;
		type = h3_gametypes_item_type((short)item);
		if (type == NONE)
		{
			snprintf(text, sizeof(text), "%s", T("CREATE NEW GAME TYPE"));
			line[0] = 0;
		}
		else
		{
			snprintf(text, sizeof(text), "%s", h3_gametypes.names[type]);
			h3_capitals(text);
			if (h3_gametypes.damaged[type])
				snprintf(line, sizeof(line), "%s", T("DAMAGED"));
			else if (type >= h3_gametypes.saved_count)
				snprintf(line, sizeof(line), "%s", T("BUILT-IN"));
			else
				line[0] = 0;
		}
		text_show(named(child, "row_name"), text);
		text_show(named(child, "row_note"), line);
		child->animation.current_frame_index = index == h3_gametypes.view.row ? 1 : 0;
		if (index == h3_gametypes.view.row)
			focus = child;
	}
	if (focus && focus != list->focused_child)
		ui_widget_port_give_focus(list, focus);
	/* (the card) */
	type = h3_gametypes_item_type(h3_gametypes.cursor);
	if (type == NONE)
	{
		text_show(named(screen, "info_name"), T("NEW GAME TYPE"));
		text_show(named(screen, "info_kind"), "");
		text_show(named(screen, "info_text"),
			T("Make a game type of your own: choose\nthe game type it starts as, then change\nits rules and SAVE it. It is then in\nthe lobby's GAME, for every network."));
		snprintf(line, sizeof(line), "CREATE NEW GAME TYPE");
	}
	else
	{
		struct game_variant variant;
		boolean builtin = type >= h3_gametypes.saved_count;

		snprintf(text, sizeof(text), "%s", h3_gametypes.names[type]);
		h3_capitals(text);
		text_show(named(screen, "info_name"), text);
		if (h3_gametypes.damaged[type])
		{
			text_show(named(screen, "info_kind"), T("YOUR GAME TYPE"));
			text_show(named(screen, "info_text"), T("This game type's file is damaged\nand can't be loaded. Square deletes it."));
		}
		else if (h3_gametype_card(h3_gametypes.indices[type], &variant))
		{
			snprintf(text, sizeof(text), "%s \xc2\xb7 %s", h3_gametype_kind_name(&variant),
				builtin ? T("THE GAME'S OWN") : T("YOURS"));
			text_show(named(screen, "info_kind"), text);
			h3_gametype_rules_text(&variant, FALSE, text, sizeof(text));
			text_show(named(screen, "info_text"), text);
		}
		else
		{
			text_show(named(screen, "info_kind"), "");
			text_show(named(screen, "info_text"), T("That game type can't be read now."));
		}
		snprintf(line, sizeof(line), "%s%s", h3_gametypes.names[type], builtin ? " (built-in)" : "");
	}
	text_show(named(screen, "status"), h3_gametypes.status);
	{
		char said[256];

		snprintf(said, sizeof(said), "%s (%d of %d)", line, h3_gametypes.cursor, h3_gametypes.count);
		if (strcmp(said, h3_gametypes.said))
		{
			snprintf(h3_gametypes.said, sizeof(h3_gametypes.said), "%s", said);
			platform_log("menus: h3 gametypes: %s", said);
		}
	}
}

/* (port h3 gametype actions) the options' title and help; RENAME and DELETE
hidden for the game's own */
static void h3_gametype_actions_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child;
	char text[64];
	short focused = 0, index;

	snprintf(text, sizeof(text), "%s", h3_gametypes.chosen_name);
	h3_capitals(text);
	text_show(named(screen, "title"), text);
	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		boolean shown = !h3_gametypes.chosen_builtin || index < 2;

		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		if (child == list->focused_child)
			focused = index;
	}
	{
		static char const *const helps[] =
		{
			N_("Change its rules in the game type editor."),
			N_("Make a game type of yours with the same rules, under\nanother name."),
			N_("Give it another name."),
			N_("Delete it, after a question."),
		};

		if (h3_gametypes.chosen_builtin && focused == 0)
			text_show(named(screen, "help"), T("Change its rules in the game type editor: the game's\nown never change, so SAVE makes a new one of yours."));
		else
			text_show(named(screen, "help"), T(helps[PIN(focused, 0, (short)NUMBEROF(helps) - 1)]));
	}
}

/* (port h3 gametype question) the delete question's text, or the save
question's */
static void h3_gametype_question_update(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	char text[256], name[48];

	if (named(screen, "op_discard"))
	{
		h3_gametype_name(name, sizeof(name), &h3_gametypes.current);
		snprintf(text, sizeof(text), T("%s has changes that aren't saved."), name);
	}
	else
		snprintf(text, sizeof(text), T("Delete the game type %s?\nIt can't be brought back."), h3_gametypes.chosen_name);
	text_show(widget, text);
}

/* (port h3 gametype bases) the bases' rows (item_1 to item_12): the game's
own game types, scrolling past twelve; the chosen one's rules */
static void h3_gametype_bases_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focus = NULL;
	long index, focused_row = NONE;
	int bases = h3_gametypes.count - h3_gametypes.saved_count;
	int target;
	char text[512];

	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		if (child == list->focused_child)
			focused_row = index;
	}
	target = browser_view_follow(&h3_gametypes.base_view, H3_GAMETYPE_BASE_ROWS, bases, (int)focused_row,
		h3_gametypes.base_cursor, h3_gametypes.page);
	h3_gametypes.page = 0;
	h3_gametypes.base_cursor = (short)MAX(target, 0);
	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		boolean shown = index < h3_gametypes.base_view.visible;

		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		if (!shown)
			continue;
		snprintf(text, sizeof(text), "%s", h3_gametypes.names[h3_gametypes.saved_count + h3_gametypes.base_view.offset +
			index]);
		h3_capitals(text);
		text_show(child, text);
		if (index == h3_gametypes.base_view.row)
			focus = child;
	}
	if (focus && focus != list->focused_child)
		ui_widget_port_give_focus(list, focus);
	if (target >= 0 && target < bases)
	{
		struct game_variant variant;
		short type = (short)(h3_gametypes.saved_count + target);

		snprintf(text, sizeof(text), "%s", h3_gametypes.names[type]);
		h3_capitals(text);
		text_show(named(screen, "info_name"), text);
		if (h3_gametype_card(h3_gametypes.indices[type], &variant))
			h3_gametype_rules_text(&variant, TRUE, text, sizeof(text));
		else
			snprintf(text, sizeof(text), "%s", T("That game type can't be read now."));
		text_show(named(screen, "info_text"), text);
	}
}

/* (port h3 gametype editor) the editor's rows' values, the focused row's
arrows and help, the rules at the right, the status; a name typed taken */
static void h3_gametype_editor_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct game_variant *variant = &h3_gametypes.current;
	short row, focused = 0;
	char text[512], name[48];

	if (!h3_gametypes.editing)
		return;
	if (typing_done(_typing_gametype_name, text, sizeof(text)))
	{
		wchar_t typed[H3_GAMETYPE_NAME_LENGTH + 1];

		if (h3_gametype_name_check(text, typed, h3_gametypes.edit_index != NONE ?
			h3_gametypes.original.human_readable_game_description : NULL, h3_gametypes.edit_status,
			sizeof(h3_gametypes.edit_status)))
		{
			memcpy(variant->human_readable_game_description, typed, sizeof(variant->human_readable_game_description));
			platform_log("menus: h3 gametype editor: name %s", text);
		}
		else
			platform_log("menus: h3 gametype editor: the name \"%s\" was refused: %s", text, h3_gametypes.edit_status);
	}
	for (row = 0; row < (short)NUMBEROF(h3_gametype_editor_rows); row++)
	{
		struct widget_instance *item = named(list, h3_gametype_editor_rows[row]);
		boolean has_focus = item && item == list->focused_child;

		if (!item)
			continue;
		if (has_focus)
			focused = row;
		text[0] = 0;
		if (row == 0)
		{
			text_show(named(item, "label"), T("NAME"));
			h3_gametype_name(text, sizeof(text), variant);
		}
		else if (row == 1)
		{
			text_show(named(item, "label"), T("GAME"));
			snprintf(text, sizeof(text), "%s", h3_gametype_kind_name(variant));
		}
		else if (row - 2 < NUMBER_OF_H3_GAMETYPE_GROUPS)
			text_show(named(item, "label"), h3_gametype_group_name((short)(row - 2), variant));
		text_show(named(item, "value"), text);
		h3_gametype_row_focus(item, has_focus, row == 1);
	}
	h3_gametype_name(name, sizeof(name), variant);
	snprintf(text, sizeof(text), "%s", name);
	h3_capitals(text);
	text_show(named(screen, "info_name"), text);
	snprintf(text, sizeof(text), h3_gametypes.edit_index == NONE ? T("A new game type: %s, not saved yet.") :
		h3_gametype_dirty() ? T("%s: changed, not saved yet.") : "%s", name);
	text_show(named(screen, "subtitle"), text);
	h3_gametype_rules_text(variant, TRUE, text, sizeof(text));
	text_show(named(screen, "info_text"), text);
	{
		static char const *const helps[] =
		{
			N_("The game type's name, as the lobby's GAME lists it\n(11 characters at most). Cross types it."),
			N_("The game it plays. Another game starts with its own\nusual settings."),
			N_("The score to win and the number of lives. (A game type\nholds no time limit.)"),
			N_("How long dead players wait, and the suicide penalty."),
			N_("Shields, health, camouflage, starting equipment and\nwhat the motion tracker and waypoints show."),
			N_("The weapon set, infinite grenades and the vehicles."),
			N_("Whether players play in teams."),
			N_("The settings of its own game."),
			N_("Saves the game type: it is then in the lobby's GAME,\nSTART GAME, online games and the Halo CE menus."),
		};

		text_show(named(screen, "help"), T(helps[PIN(focused, 0, (short)NUMBEROF(helps) - 1)]));
	}
	text_show(named(screen, "status"), h3_gametypes.edit_status);
	{
		char said[512];

		h3_gametype_rules_log(variant, text, sizeof(text));
		snprintf(said, sizeof(said), "%s: %s", name, text);
		if (strcmp(said, h3_gametypes.edit_said))
		{
			snprintf(h3_gametypes.edit_said, sizeof(h3_gametypes.edit_said), "%s", said);
			platform_log("menus: h3 gametype editor: now %s", said);
		}
	}
}

/* (port h3 gametype options) a group's rows (op_1 to op_8: label, value),
the others hidden; the focused row's help, the group's note */
static void h3_gametype_options_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct game_variant *variant = &h3_gametypes.current;
	short settings[H3_GAMETYPE_OPTION_ROWS];
	int count = h3_gametype_group_settings(h3_gametypes.group, variant, settings);
	struct widget_instance *child;
	short index, focused = 0;
	char text[96], name[48];

	if (!h3_gametypes.editing)
		return;
	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		boolean shown = index < count;
		boolean has_focus = shown && child == list->focused_child;
		long values[MAXIMUM_ODDBALLS];

		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		if (!shown)
			continue;
		if (has_focus)
			focused = index;
		text_show(named(child, "label"), h3_gametype_setting_label(settings[index], variant->game_engine_index));
		h3_gametype_value_text(settings[index], variant, text, sizeof(text));
		text_show(named(child, "value"), text);
		h3_gametype_row_focus(child, has_focus, h3_gametype_values(settings[index], variant, values) > 1);
	}
	text_show(named(screen, "title"), h3_gametype_group_name(h3_gametypes.group, variant));
	h3_gametype_name(name, sizeof(name), variant);
	snprintf(text, sizeof(text), "%s \xc2\xb7 %s", name, h3_gametype_kind_name(variant));
	text_show(named(screen, "subtitle"), text);
	text_show(named(screen, "help"), count > 0 ? h3_gametype_setting_help(settings[focused], variant) : "");
	text_show(named(screen, "note"), h3_gametype_group_note(h3_gametypes.group, variant));
}

/* (the main loop) what the GAME TYPES screens asked for once back on the
list: the editor or the delete question opened over it; the editor gone
back from once its question closed */
static void h3_gametypes_main_loop(void)
{
	struct widget_instance *active = NULL;
	long index;

	for (index = 0; index < 4 && !active; index++)
		active = ui_widget_port_active_screen((short)index);
	/* (the place it is open at) */
	index--;
	if (h3_gametypes.open_pending != _h3_gametype_open_none)
	{
		long list = tag_loaded(UI_WIDGET_DEFINITION_TAG, "pc\\h3\\gametypes\\screen");
		long next = tag_loaded(UI_WIDGET_DEFINITION_TAG, h3_gametypes.open_pending == _h3_gametype_open_editor ?
			"pc\\h3\\gtedit\\screen" : "pc\\h3\\gtdel\\screen");

		h3_gametypes.open_pending = _h3_gametype_open_none;
		if (active && list != NONE && next != NONE && active->definition_tag_index == list)
		{
			ui_widget_load_by_name_or_tag(NULL, next, NULL, active->local_player_index, active->definition_tag_index,
				NONE, NONE);
		}
	}
	if (h3_gametypes.leave_pending)
	{
		long editor = tag_loaded(UI_WIDGET_DEFINITION_TAG, "pc\\h3\\gtedit\\screen");

		h3_gametypes.leave_pending = FALSE;
		if (active && editor != NONE && active->definition_tag_index == editor)
			ui_widget_port_go_back((short)index);
	}
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
	h3_custom.flame_asked = FALSE;
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
static short const multiplayer_pictures[] = { 0, 1, 0, 0, 1, 0, 1, 2, 2 };

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
		/* (HALO_TEST_BROWSER_BOTS=N: N of each game's players are bots, as a dedicated server's listing says) */
		if (getenv("HALO_TEST_BROWSER_BOTS") && atoi(getenv("HALO_TEST_BROWSER_BOTS")) > 0)
			entry->bots = MIN(atoi(getenv("HALO_TEST_BROWSER_BOTS")), entry->players);
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
		/* (HALO_TEST_BROWSER_SERVERS=1: every fourth one a dedicated
		server's, some of its players bots: the SERVERS and BOTS filters) */
		if (getenv("HALO_TEST_BROWSER_SERVERS") && atoi(getenv("HALO_TEST_BROWSER_SERVERS")) && index % 4 == 3)
		{
			entry->dedicated = 1;
			entry->bots = (int)(index % 5) + 1;
			if (entry->players < entry->bots)
				entry->players = entry->bots + 2;
			snprintf(entry->rules, sizeof(entry->rules), "%s on %s: under way (DEDICATED)", entry->gametype, entry->map);
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
		memcpy(entry->host, game->host, sizeof(entry->host));
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
		int lan_count = (int)menu_functions.game_count;

		join_watch();
		browser_large_watch();
		menu_functions.lan_online_count = p2p_lobby_entries(menu_functions.games + menu_functions.game_count,
			P2P_LOBBY_MAXIMUM_ENTRIES - (int)menu_functions.game_count);
		menu_functions.game_count += menu_functions.lan_online_count;
		/* (a game both System Link and online - an internet play peer's,
		whose broadcasts reach the System Link search too: one row, its
		online one) */
		menu_functions.game_count = browser_list_merge_lan(menu_functions.games, (int)menu_functions.game_count,
			&lan_count);
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

/* ---- the SERVER BROWSER of Menu style Online (menus/online/browser.xml,
standing for the LAN browser): the networks as tabs, L and R - the
connection itself, as HOST SETTINGS and the settings panel's Connection
choose it (network_connection_choose, h3_mp.network: one choice) -; the
filters, the D-pad's left and right: ALL, VITA and PC the browsers' own
(network.browser_filter, the settings panel's Browser shows), SERVERS a
dedicated server's games, BOTS the games with bots in them; the chosen
game's card at the right. (A Forge game is never listed, and the lists do
not say a friend's game: no such filters.) */
enum
{
	_online_browser_all,
	_online_browser_vita,
	_online_browser_pc,
	_online_browser_servers,
	_online_browser_bots,
	NUMBER_OF_ONLINE_BROWSER_FILTERS
};

static struct
{
	short filter;
	char said[96];
} online_browser;

static short const online_browser_networks[] = { _h3_network_online, _h3_network_system_link, _h3_network_adhoc };

/* (L and R) the next network: the connection switched, the lists looked
through again */
static void online_browser_network_step(int step)
{
	short current = h3_mp_network_of_connection(), index, at = 0, count = (short)NUMBEROF(online_browser_networks);

	for (index = 0; index < count; index++)
	{
		if (online_browser_networks[index] == current)
			at = index;
	}
	at = (short)(((at + step) % count + count) % count);
	/* (as the lobbies' NETWORK picker chooses one, h3_mp_network_set: a
	party plays online alone; the connection switched live, the lobby's
	network that one) */
	if (h3_party_refuses(online_browser_networks[at] == _h3_network_online))
		return;
	if (online_browser_networks[at] != h3_mp.network)
		h3_live_close("another network chosen");
	h3_live_retry();
	network_connection_choose(online_browser_networks[at] == _h3_network_online ? NETWORK_CONNECTION_ONLINE :
		online_browser_networks[at] == _h3_network_adhoc ? NETWORK_CONNECTION_ADHOC : NETWORK_CONNECTION_WIFI);
	h3_mp.network = online_browser_networks[at];
	menu_functions.status[0] = 0;
	menu_functions.chosen_valid = FALSE;
	menu_functions.lan_since = system_milliseconds();
	/* (the network before's games gone at once - the list and the card -,
	its late answers let go: "Searching..." until this one's come) */
	network_game_client_forget_listed_games();
	menu_functions.game_count = 0;
	menu_functions.lan_online_count = 0;
	browser_view_reset(&menu_functions.view);
	p2p_lobby_browse(internet_play() ? 1 : 0);
	if (internet_play())
		p2p_lobby_refresh();
	platform_log("menus: online server browser: network %s", h3_network_name(online_browser_networks[at]));
}

/* (the D-pad) the next filter (ONLINE's alone: System Link and ad hoc list
Vitas' games) */
static void online_browser_filter_step(int step)
{
	static char const *const wire[] = { "all", "vita", "pc", "all", "all" };

	if (h3_mp_network_of_connection() != _h3_network_online)
		return;
	online_browser.filter = (short)(((online_browser.filter + step) % NUMBER_OF_ONLINE_BROWSER_FILTERS +
		NUMBER_OF_ONLINE_BROWSER_FILTERS) % NUMBER_OF_ONLINE_BROWSER_FILTERS);
	setting_set("HALO_NET_BROWSER_FILTER", wire[online_browser.filter]);
	menu_functions.chosen_valid = FALSE;
	browser_view_reset(&menu_functions.view);
	platform_log("menus: online server browser: filter %d", online_browser.filter);
}

/* the games SERVERS and BOTS keep (the browser's list before its groups) */
static void online_browser_filter_games(void)
{
	long from, to = 0;

	if (!halo_menu_style_online() || h3_mp_network_of_connection() != _h3_network_online ||
		online_browser.filter < _online_browser_servers)
	{
		return;
	}
	for (from = 0; from < menu_functions.game_count; from++)
	{
		struct p2p_lobby_entry const *entry = &menu_functions.games[from];

		if (online_browser.filter == _online_browser_servers ? !entry->dedicated : entry->bots <= 0)
			continue;
		if (to != from)
			menu_functions.games[to] = *entry;
		to++;
	}
	menu_functions.game_count = to;
}

/* the screen's parts of its own: the tabs, the filters, the chosen game's
card (its map's picture, name, what it plays) */
static void online_browser_screen_update(struct widget_instance *screen, long target)
{
	struct widget_instance *found;
	short network = h3_mp_network_of_connection(), index;
	boolean online = network == _h3_network_online;
	char name[24], text[192];

	if (!named(screen, "net_tab_0"))
		return;
	/* (the filter the settings panel set, when it is one of its own) */
	if (online_browser.filter < _online_browser_servers)
		online_browser.filter = (short)network_wire_browser_filter();
	for (index = 0; index < (short)NUMBEROF(online_browser_networks); index++)
	{
		snprintf(name, sizeof(name), "net_tab_%d", index);
		if ((found = named(screen, name)) != NULL)
			found->animation.current_frame_index = online_browser_networks[index] == network ? 1 : 0;
	}
	for (index = 0; index < NUMBER_OF_ONLINE_BROWSER_FILTERS; index++)
	{
		snprintf(name, sizeof(name), "filter_%d", index);
		if ((found = named(screen, name)) != NULL)
		{
			widget_instance_set_visibility_recursive(found, online);
		}
	}
	/* (the chosen filter's own bar, its label's width) */
	for (index = 0; index < NUMBER_OF_ONLINE_BROWSER_FILTERS; index++)
	{
		snprintf(name, sizeof(name), "filter_bar_%d", index);
		if ((found = named(screen, name)) != NULL)
		{
			widget_instance_set_visibility_recursive(found, online && index == online_browser.filter);
			found->animation.current_frame_index = 1;
		}
	}
	if ((found = named(screen, "filter_butn")) != NULL)
		widget_instance_set_visibility_recursive(found, online);
	if ((found = named(screen, "filter_hint")) != NULL)
		widget_instance_set_visibility_recursive(found, online);
	/* the card */
	{
		boolean card = target != NONE && menu_functions.chosen_valid && !menu_functions.chosen.header;
		static char const *const parts[] = { "map_picture", "card_name", "card_info", "ticker_players_label",
			"ticker_players_value", "ticker_rules_label", "ticker_rules_value" };

		for (index = 0; index < (short)NUMBEROF(parts); index++)
		{
			if ((found = named(screen, parts[index])) != NULL)
				widget_instance_set_visibility_recursive(found, card);
		}
		if (!card)
			return;
		if ((found = named(screen, "map_picture")) != NULL)
		{
			short picture = H3_MAP_OTHER_PICTURE;

			for (index = 0; index < H3_MAP_COUNT; index++)
			{
				if (!_stricmp(T(h3_map_names[index]), menu_functions.chosen.map) ||
					!_stricmp(h3_map_names[index], menu_functions.chosen.map))
				{
					picture = index;
				}
			}
			found->animation.current_frame_index = picture;
		}
		text_show(named(screen, "card_name"), menu_functions.chosen.name);
		snprintf(text, sizeof(text), "%s%s%s\n%s%s%s", menu_functions.chosen.gametype[0] ? menu_functions.chosen.gametype :
			(menu_functions.chosen.coop ? T("Co-op") : ""), menu_functions.chosen.map[0] ? T(" on ") : "",
			menu_functions.chosen.map, menu_functions.chosen.tag, menu_functions.chosen.dedicated ? T(", dedicated server") : "",
			menu_functions.chosen.locked ? T(", password") : "");
		text_show(named(screen, "card_info"), text);
		/* (a listing that names no players: their count) */
		if (!menu_functions.chosen.players_line[0])
		{
			snprintf(text, sizeof(text), T("%d of %d"), menu_functions.chosen.players, menu_functions.chosen.maximum);
			text_show(named(screen, "ticker_players_value"), text);
		}
	}
	snprintf(text, sizeof(text), "%d %d %s", network, online_browser.filter,
		menu_functions.chosen_valid ? menu_functions.chosen.name : "-");
	if (strcmp(text, online_browser.said))
	{
		snprintf(online_browser.said, sizeof(online_browser.said), "%s", text);
		platform_log("menus: online server browser: network %s, filter %d, chosen %s", h3_network_name(network),
			online_browser.filter, menu_functions.chosen_valid ? menu_functions.chosen.name : "none");
	}
}

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
	online_browser_filter_games();
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
		/* (a dedicated server's bots, in the Halo 3 rows: the people and the
		bots apart, as long a way of saying so as fits - "6 players · 5 bots",
		"6 · 5 bots", "6+5/16"; the classic rows the total) */
		if (entry->bots > 0 && named(child, "row_badge"))
		{
			int people = entry->players - entry->bots;
			char players_text[24], bots_text[24];

			snprintf(players_text, sizeof(players_text), T("%d players"), people);
			snprintf(bots_text, sizeof(bots_text), T("%d bots"), entry->bots);
			snprintf(text, sizeof(text), "%s \xc2\xb7 %s", players_text, bots_text);
			if (row_column(child, "row_players", text))
			{
				snprintf(text, sizeof(text), "%d \xc2\xb7 %s", people, bots_text);
				if (row_column(child, "row_players", text))
				{
					if (entry->maximum > 0)
						snprintf(text, sizeof(text), "%d+%d/%d", people, entry->bots, entry->maximum);
					else
						snprintf(text, sizeof(text), "%d+%d", people, entry->bots);
				}
			}
		}
		/* (Menu style Halo CE's server browser, OpenCE's rows: the people and
		the bots apart too, "6+5/16", in its narrow column) */
		else if (entry->bots > 0 && entry->bots <= entry->players)
		{
			char bots_text[24];

			/* (said as "6+5 bots" while that fits the column, else "6+5/16": the Players line under
			the list names them both) */
			snprintf(bots_text, sizeof(bots_text), T("%d bots"), entry->bots);
			snprintf(text, sizeof(text), "%d+%s", entry->players - entry->bots, bots_text);
			if (row_column(child, "row_players", text))
			{
				if (entry->maximum > 0)
					snprintf(text, sizeof(text), "%d+%d/%d", entry->players - entry->bots, entry->bots, entry->maximum);
				else
					snprintf(text, sizeof(text), "%d+%d", entry->players - entry->bots, entry->bots);
			}
		}
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
	online_browser_screen_update(screen, target);
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

		if (p2p_party_join_warning(entry->players - entry->bots, entry->maximum, warning, sizeof(warning)) &&
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

/* (the Halo 3 GAME OPTIONS' BOTS: Bots online, bots.online - the bots a game
hosted online fills its lobby with, network_server_manager.c - its
spinner's strings' order) */
static char const *const setup_bots_values[] = { "full", "off", "1", "2", "3", "5", "7", "9", "11", "13" };

static short setup_bots_index(void)
{
	char const *value = setting_get("HALO_BOTS_ONLINE", "full");
	short index;

	if (!strcmp(value, "0"))
		return 1;
	for (index = 0; index < (short)NUMBEROF(setup_bots_values); index++)
	{
		if (!strcmp(value, setup_bots_values[index]))
			return index;
	}
	return 0;
}

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
	short bots = setup_bots_index();

	if (menu_functions.setup_held)
	{
		maximum = menu_functions.held_maximum;
		private_game = menu_functions.held_private;
		audience = menu_functions.held_audience;
		bots = menu_functions.held_bots;
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
	/* (BOTS: FULL, OFF, then the most bots) */
	spinner = spinner_of(screen, "op_bots");
	if (spinner)
		spinner->parameters.list.selected_index = (short)PIN(bots, 0, (short)NUMBEROF(setup_bots_values) - 1);
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
	spinner = spinner_of(screen, "op_bots");
	menu_functions.held_bots = spinner ? spinner->parameters.list.selected_index : setup_bots_index();
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
	/* (BOTS: Bots online, live - a lobby hosted now fills or empties as
	it changes, network_server_manager.c) */
	{
		struct widget_instance *bots = spinner_of(screen, "op_bots");

		if (bots)
		{
			setting_set("HALO_BOTS_ONLINE", setup_bots_values[PIN(bots->parameters.list.selected_index, 0,
				(short)NUMBEROF(setup_bots_values) - 1)]);
			platform_log("menus: GAME OPTIONS: Bots online %s", setting_get("HALO_BOTS_ONLINE", "full"));
		}
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
	{
		h3_live_public_chosen();
		/* (the lobby hosted on demand: its game now, public or private as
		just chosen; its settings taken below) */
		h3_live_open_now("GAME OPTIONS' START GAME");
	}
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
	_variants_mp,
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

/* ---- the multiplayer lobby's map variants (Halo 3's: the MAP picker,
a map chosen, shows the map itself first, then the player's saved map
variants of it; the variant is played in the lobby's game, its objects
placed as it starts - forge.c's forge_play_*: a local game's, or the
hosted game's, its joiners given the layout as network Forge gives it) */

/* a map of the Xbox's, its short name (bloodgulch: its map variants'
folder), NULL for another's or one Forge does not play */
static char const *h3_mp_map_short(long index)
{
	char const *leaf;

	if (index < 0 || index >= H3_MAP_COUNT)
		return NULL;
	leaf = strrchr(h3_map_paths[index], '\\');
	leaf = leaf ? leaf + 1 : h3_map_paths[index];
	return forge_map_of_short(leaf) != NONE ? leaf : NULL;
}

/* (A on a map) a map with saved map variants: its variants shown in the
picker (TRUE: the picker stays); else FALSE, the map chosen at once */
static boolean h3_mp_variants_offer(long index)
{
	struct forge_store_entry const *entry;
	char const *map_name = h3_mp_map_short(index);
	int count, row;

	if (!map_name)
		return FALSE;
	h3_variants_list(_variants_mp, map_name, TRUE);
	count = h3_forge_pick_rows(-1, &entry);
	if (count <= 1)
		return FALSE;
	h3_mp.picking_variant = TRUE;
	h3_mp.pick_index = (short)index;
	h3_mp.pick_refocus = TRUE;
	h3_mp.map_page = 0;
	h3_mp.variant_chosen = 0;
	h3_mp.variant_said[0] = 0;
	browser_view_reset(&h3_mp.variant_view);
	/* (the lobby's variant of that map chosen again) */
	if (h3_mp_variant_chosen() && h3_mp.map_index == index)
	{
		for (row = 1; row < count; row++)
		{
			h3_forge_pick_rows(row, &entry);
			if (entry && !strcmp(entry->file_name, h3_mp.variant_file))
				h3_mp.variant_chosen = row;
		}
	}
	platform_log("menus: h3 mp: %s's map variants (%d)", h3_map_names[index], count - 1);
	return TRUE;
}

/* (port h3 maps, a map's variants shown) the rows (item_0 to item_12): the
map itself, then its map variants, scrolling; the map's picture, the
variant's details in the description's place */
static void h3_mp_variants_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focus = NULL, *found;
	struct forge_store_entry const *entry;
	long index, focused_row = NONE, rows = 0;
	int target, count;
	char text[256];

	count = h3_forge_pick_rows(-1, &entry);
	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		if (child == list->focused_child)
			focused_row = index;
		rows++;
	}
	/* (just shown: the focus on the variant chosen, not the row pressed) */
	if (h3_mp.pick_refocus)
	{
		focused_row = NONE;
		h3_mp.pick_refocus = FALSE;
	}
	target = browser_view_follow(&h3_mp.variant_view, (int)rows, count, (int)focused_row, h3_mp.variant_chosen,
		h3_mp.map_page);
	h3_mp.map_page = 0;
	h3_mp.variant_chosen = target;
	for (index = 0, child = list->child; child; child = child->next, index++)
	{
		boolean shown = index < h3_mp.variant_view.visible;

		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		if (!shown)
			continue;
		h3_forge_pick_rows((int)(h3_mp.variant_view.offset + index), &entry);
		snprintf(text, sizeof(text), "%s", entry ? entry->title : T(h3_map_names[h3_mp.pick_index]));
		h3_capitals(text);
		text_show(child, text);
		if (index == h3_mp.variant_view.row)
			focus = child;
	}
	if (focus && focus != list->focused_child)
		ui_widget_port_give_focus(list, focus);
	if ((found = named(screen, "map_picture")) != NULL)
		found->animation.current_frame_index = h3_mp.map_displays[h3_mp.pick_index];
	if ((found = named(screen, "map_text")) != NULL)
		widget_instance_set_visibility_recursive(found, FALSE);
	h3_forge_pick_rows(target, &entry);
	if (entry)
		h3_variant_details(entry, FALSE, text, sizeof(text));
	else
		snprintf(text, sizeof(text), "%s", T("The original map: nothing placed yet."));
	if ((found = named(screen, "map_why")) != NULL)
	{
		widget_instance_set_visibility_recursive(found, TRUE);
		text_show(found, text);
	}
	{
		char said[192];

		snprintf(said, sizeof(said), "%s: %s (%d of %d)", h3_map_names[h3_mp.pick_index], entry ? entry->title :
			"the original", target + 1, count);
		if (strcmp(said, h3_mp.variant_said))
		{
			snprintf(h3_mp.variant_said, sizeof(h3_mp.variant_said), "%s", said);
			platform_log("menus: h3 mp variants: %s", said);
		}
	}
}

/* (A on a map's variants) a variant of it, or the map itself: the lobby's,
and back to the lobby (TRUE) */
static boolean h3_mp_variant_pick(struct widget_instance *widget)
{
	struct forge_store_entry const *entry;
	long row = h3_mp.variant_view.offset + h3_child_index(widget);

	h3_forge_pick_rows((int)row, &entry);
	h3_mp.picking_variant = FALSE;
	return h3_mp_map_choose(h3_mp.pick_index, entry ? entry->file_name : NULL, entry ? entry->title : NULL);
}

/* (port h3 maps back, B in the map picker) from a map's variants, back to
the maps on that map (FALSE: the picker stays); from the maps, back */
static boolean h3_mp_maps_back(void)
{
	if (!h3_mp.picking_variant)
		return TRUE;
	h3_mp.picking_variant = FALSE;
	h3_mp.map_cursor = h3_mp.pick_index;
	h3_mp.pick_refocus = TRUE;
	h3_mp.map_said[0] = 0;
	return FALSE;
}

/* the lobby's map variant still a file of its map (deleted in File Share:
none) */
static void h3_mp_variant_check(void)
{
	char const *map_name = h3_mp_variant_chosen() ? h3_mp_map_short(h3_mp.map_index) : NULL;

	if (h3_mp.variant_file[0] && (!map_name || !forge_store_exists(map_name, h3_mp.variant_file)))
	{
		platform_log("menus: h3 mp: the map variant %s is gone, or not of the lobby's map", h3_mp.variant_file);
		h3_mp_variant_clear();
	}
}

/* (the lobby's game: hosted, or the local one about to start) the map
variant played in it, if one is chosen: TRUE; FALSE, said, if it cannot be
(its file gone, damaged; Vitas and PC: map variants are the Vitas') - the
lobby's variant then forgotten, the map played as it is */
static boolean h3_mp_variant_play(boolean hosted, char const *map)
{
	char why[FORGE_STORE_WHY_SIZE];
	boolean played;

	h3_mp_variant_check();
	if (!h3_mp_variant_chosen())
	{
		/* (no map variant: no game of one hosted, none asked for locally) */
		if (hosted)
			forge_net_host_end();
		else
			forge_play_local_request(NULL, NULL, NULL, 0);
		return TRUE;
	}
	played = hosted ? forge_play_host_begin(map, h3_mp.variant_file, why, (int)sizeof(why)) :
		forge_play_local_request(map, h3_mp.variant_file, why, (int)sizeof(why));
	if (played)
	{
		platform_log("menus: h3 mp: the map variant %s is played on %s (%s)", h3_mp.variant_title, h3_map_names[h3_mp.map_index],
			hosted ? "the game hosted" : "the local game");
		return TRUE;
	}
	platform_log("menus: h3 mp: the map variant %s can't be played: %s: the map as it is", h3_mp.variant_title, why);
	platform_show_message(T("Halo: map variant"), why);
	h3_mp_variant_clear();
	if (hosted)
		forge_net_host_end();
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
		/* (a player spawn point's four rows: its team, game type, kind and
		yaw - forge_spawns.inc) */
		if (properties.spawn)
		{
			boolean shown = row <= _forge_property_yaw;

			switch (row)
			{
			case _forge_property_team:
				snprintf(text, sizeof(text), "%s: %s", T("TEAM"), properties.team == 0 ? T("RED") : properties.team == 1 ?
					T("BLUE") : T("ANY"));
				break;
			case _forge_property_respawn:
				snprintf(text, sizeof(text), "%s: %s", T("GAME TYPE"), properties.game_type_name);
				break;
			case _forge_property_place_at_start:
				snprintf(text, sizeof(text), "%s: %s", T("SPAWN"), properties.spawn_kind == _forge_spawn_initial ?
					T("INITIAL SPAWN") : T("RESPAWN POINT"));
				break;
			case _forge_property_yaw:
				snprintf(text, sizeof(text), "%s: %d\xc2\xb0", T("YAW"), properties.yaw);
				break;
			default:
				text[0] = 0;
				break;
			}
			widget_instance_set_visibility_recursive(child, shown);
			child->disabled = !shown;
			text_show(child, text);
			child->animation.current_frame_index = child == list->focused_child ? 1 : 0;
			continue;
		}
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
		/* (a character's own rows: hidden for any other object; the rest
		shown again after a spawn point's) */
		if (row >= _forge_property_ai_team)
		{
			widget_instance_set_visibility_recursive(child, properties.ai);
			child->disabled = !properties.ai;
		}
		else if (child->disabled)
		{
			widget_instance_set_visibility_recursive(child, TRUE);
			child->disabled = FALSE;
		}
		text_show(child, text);
		child->animation.current_frame_index = child == list->focused_child ? 1 : 0;
	}
	if (properties.spawn)
		snprintf(text, sizeof(text), "%s: spawn point, team %d, game type %s, %s, yaw %d", properties.name, properties.team,
			properties.game_type_name, properties.spawn_kind == _forge_spawn_initial ? "initial spawn" : "respawn point",
			properties.yaw);
	else if (properties.ai)
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
		text_show(named(screen, "info_text"), T("A game variant.\nGame variants are made and changed in\nMULTIPLAYER, EDIT GAME TYPES."));
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
			T("Game variants are changed in MULTIPLAYER, EDIT GAME TYPES."));
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
			T("Game variants are changed in MULTIPLAYER, EDIT GAME TYPES."));
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
		text_show(named(screen, "toggle"), "");
	}
	/* (Cross, Triangle and Square act on the chosen one: not shown with
	none - an empty list's Cross had a glyph and no label, insider.21) */
	{
		static char const *const hints[] = { "a_butn", "toggle", "y_butn", "details_label", "x_butn", "delete_label" };
		struct widget_instance *hint;
		int hint_index;

		for (hint_index = 0; hint_index < (int)NUMBEROF(hints); hint_index++)
		{
			if ((hint = named(screen, hints[hint_index])) != NULL)
				widget_instance_set_visibility_recursive(hint, chosen != NULL);
		}
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
five of its own (ADD FRIEND, APPEAR OFFLINE, FRIENDS CAN JOIN, PARTY, FRIEND
CODE BACKUP) and then the entries p2p_friends_list gives, in its order (requests, invites, friends
online, offline, requests sent, blocked), scrolling as the mod manager's.
The entry chosen stays chosen by its id and kind as the list changes */

#define H3_FRIENDS_ROWS 10
#define H3_FRIENDS_OWN_ROWS 5
#define H3_FRIENDS_MESSAGE_MILLISECONDS 6000
#define H3_FRIENDS_REFRESH_MILLISECONDS 500
#define H3_FRIENDS_PANEL_ROWS 5
/* (the options' party rows: not p2p_friends_act's) */
#define H3_FRIENDS_PARTY_INVITE 1000
#define H3_FRIENDS_PARTY_KICK 1001
/* (FRIEND CODE BACKUP's questions' rows: p2p_friends_backup_*) */
#define H3_FRIENDS_BACKUP_EXPORT 1002
#define H3_FRIENDS_BACKUP_RESTORE 1003
#define H3_FRIENDS_BACKUP_WITH_LIST 1004
#define H3_FRIENDS_BACKUP_CODE_ONLY 1005
#define H3_FRIENDS_BACKUP_CONFIRM 1006
#define H3_FRIENDS_BACKUP_CANCEL 1007


/* the three rows of the screen's own */
enum
{
	_h3_friends_row_add,
	_h3_friends_row_offline,
	_h3_friends_row_join,
	/* (the party: its screen, halo3/party.xml) */
	_h3_friends_row_party,
	/* (the friend code backed up to a file with a password, or restored:
	its question, then the typing) */
	_h3_friends_row_backup,
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
	/* the options' question: its entry, and what each of its rows does;
	(FRIEND CODE BACKUP's) its own title and words */
	struct p2p_friend_entry options_entry;
	int options[3];
	int option_count;
	char options_title[64];
	char options_text[256];
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

/* ---- FRIENDS' FRIEND CODE BACKUP (port/linux/src/p2p_friends.h,
p2p_friends_backup.inc): Cross on its row asks BACK UP FRIEND CODE or
RESTORE FRIEND CODE (FRIENDS' options question, its own words); then the
file's name (friends-backup), its password (8 characters or more), and to
back up the password again and WITH FRIENDS LIST or FRIEND CODE ONLY; the
work on friends' own thread, said under the list; a restore opened asks
CONFIRM or CANCEL with the codes before and after, and replaces nothing
until confirmed. What was typed is wiped once used */

enum
{
	_h3_backup_none,
	/* typing: the name, the password, the password again */
	_h3_backup_name,
	_h3_backup_password,
	_h3_backup_again,
	/* the question: with the friends list or not */
	_h3_backup_list,
	_h3_backup_working,
	/* the question: CONFIRM or CANCEL */
	_h3_backup_confirm,
};

static struct
{
	int step;
	boolean restore;
	char name[P2P_FRIENDS_BACKUP_NAME_SIZE];
	char password[P2P_FRIENDS_BACKUP_PASSWORD_MAXIMUM + 1];
	/* (its questions) when one was last shown: gone without an answer
	(Back), what was typed or opened let go */
	unsigned long question_time;
} h3_backup;

static void h3_backup_wipe(void)
{
	memset(h3_backup.password, 0, sizeof(h3_backup.password));
}

static void h3_backup_stop(char const *message)
{
	if (h3_backup.step == _h3_backup_confirm)
		p2p_friends_backup_cancel();
	h3_backup_wipe();
	h3_backup.step = _h3_backup_none;
	if (message && message[0])
		h3_friends_message(message);
}

/* FRIENDS' options question with the backup's words and rows */
static void h3_backup_question(char const *title, char const *text, int first, int second)
{
	memset(&h3_friends.options_entry, 0, sizeof(h3_friends.options_entry));
	snprintf(h3_friends.options_title, sizeof(h3_friends.options_title), "%s", title);
	snprintf(h3_friends.options_text, sizeof(h3_friends.options_text), "%s", text);
	h3_friends.options[0] = first;
	h3_friends.options[1] = second;
	h3_friends.option_count = 2;
	h3_backup.question_time = 0;
	menu_functions.open_friends_question = TRUE;
}

/* (the row's Cross) back up or restore? */
static void h3_backup_menu(void)
{
	if (h3_backup.step == _h3_backup_working)
	{
		h3_friends_message(p2p_friends_backup_text(P2P_FRIENDS_BACKUP_WORKING));
		return;
	}
	h3_backup_stop(NULL);
	h3_backup_question(T("FRIEND CODE BACKUP"),
		T("Save your friend code to a file with a password,\nor restore one. Anyone with the file and its\n"
		"password can be you: keep both safe."), H3_FRIENDS_BACKUP_EXPORT, H3_FRIENDS_BACKUP_RESTORE);
	platform_log("menus: h3 friends: the friend code backup's question");
}

/* (its questions' rows) */
static void h3_backup_option(int option)
{
	switch (option)
	{
	case H3_FRIENDS_BACKUP_EXPORT:
	case H3_FRIENDS_BACKUP_RESTORE:
		h3_backup.restore = option == H3_FRIENDS_BACKUP_RESTORE;
		h3_backup.step = _h3_backup_name;
		typing_begin(_typing_backup_name, T("Backup file name"), P2P_FRIENDS_BACKUP_DEFAULT_NAME,
			P2P_FRIENDS_BACKUP_NAME_SIZE - 1, FALSE);
		break;
	case H3_FRIENDS_BACKUP_WITH_LIST:
	case H3_FRIENDS_BACKUP_CODE_ONLY:
	{
		int outcome;

		if (h3_backup.step != _h3_backup_list)
			break;
		outcome = p2p_friends_backup_export(h3_backup.name, h3_backup.password, option == H3_FRIENDS_BACKUP_WITH_LIST);
		h3_backup_wipe();
		h3_backup.step = outcome == P2P_FRIENDS_BACKUP_WORKING ? _h3_backup_working : _h3_backup_none;
		h3_friends_message(p2p_friends_backup_text(outcome));
		platform_log("menus: h3 friends: backing up the friend code%s",
			option == H3_FRIENDS_BACKUP_WITH_LIST ? " and the friends list" : "");
		break;
	}
	case H3_FRIENDS_BACKUP_CONFIRM:
	{
		char code[P2P_FRIEND_CODE_SIZE], text[160];
		int outcome;

		if (h3_backup.step != _h3_backup_confirm)
			break;
		h3_backup.step = _h3_backup_none;
		outcome = p2p_friends_backup_confirm();
		if (outcome == P2P_FRIENDS_BACKUP_RESTORED)
		{
			p2p_friends_code(code, sizeof(code));
			snprintf(text, sizeof(text), T("Friend code restored: %s"), code);
			h3_friends_message(text);
		}
		else
			h3_friends_message(p2p_friends_backup_text(outcome));
		h3_friends_refresh(TRUE);
		break;
	}
	case H3_FRIENDS_BACKUP_CANCEL:
		h3_backup_stop(T("Not restored: your friend code stays"));
		break;
	}
}

/* (each frame of FRIENDS) what was typed taken, the work watched */
static void h3_backup_update(void)
{
	char typed[HALO_TEXT_INPUT_SIZE], text[256];
	int outcome;

	if (h3_backup.step == _h3_backup_name && menu_functions.typing != _typing_backup_name)
		h3_backup_stop(NULL);
	if (typing_done(_typing_backup_name, typed, sizeof(typed)) && h3_backup.step == _h3_backup_name)
	{
		if (!p2p_friends_backup_name(typed, h3_backup.name))
			h3_backup_stop(p2p_friends_backup_text(P2P_FRIENDS_BACKUP_BAD_NAME));
		else
		{
			h3_backup.step = _h3_backup_password;
			typing_begin(_typing_backup_password, h3_backup.restore ? T("The backup's password") :
				T("A password (8 characters or more)"), "", P2P_FRIENDS_BACKUP_PASSWORD_MAXIMUM, TRUE);
		}
	}
	else if (h3_backup.step == _h3_backup_password && typing_done(_typing_backup_password, typed, sizeof(typed)))
	{
		outcome = p2p_friends_backup_password_check(typed);
		if (h3_backup.restore)
		{
			outcome = p2p_friends_backup_restore(h3_backup.name, typed);
			h3_backup.step = outcome == P2P_FRIENDS_BACKUP_WORKING ? _h3_backup_working : _h3_backup_none;
			h3_friends_message(p2p_friends_backup_text(outcome));
			platform_log("menus: h3 friends: opening the backup %s", h3_backup.name);
		}
		else if (outcome != P2P_FRIENDS_BACKUP_IDLE)
			h3_backup_stop(p2p_friends_backup_text(outcome));
		else
		{
			snprintf(h3_backup.password, sizeof(h3_backup.password), "%s", typed);
			h3_backup.step = _h3_backup_again;
			typing_begin(_typing_backup_again, T("The password again"), "", P2P_FRIENDS_BACKUP_PASSWORD_MAXIMUM, TRUE);
		}
	}
	else if (h3_backup.step == _h3_backup_again && typing_done(_typing_backup_again, typed, sizeof(typed)))
	{
		if (strcmp(typed, h3_backup.password))
			h3_backup_stop(p2p_friends_backup_text(P2P_FRIENDS_BACKUP_MISMATCH));
		else
		{
			h3_backup.step = _h3_backup_list;
			h3_backup_question(T("BACK UP FRIEND CODE"),
				T("Put your friends list in the backup too?\nIt comes back with your code."), H3_FRIENDS_BACKUP_WITH_LIST,
				H3_FRIENDS_BACKUP_CODE_ONLY);
		}
	}
	/* (a typing cancelled: no typing of the step's going on) */
	else if ((h3_backup.step == _h3_backup_password && menu_functions.typing != _typing_backup_password) ||
		(h3_backup.step == _h3_backup_again && menu_functions.typing != _typing_backup_again))
	{
		h3_backup_stop(NULL);
	}
	memset(typed, 0, sizeof(typed));
	if (h3_backup.step == _h3_backup_working)
	{
		outcome = p2p_friends_backup_state(text, sizeof(text));
		if (outcome == P2P_FRIENDS_BACKUP_READY)
		{
			char code[P2P_FRIEND_CODE_SIZE], current[P2P_FRIEND_CODE_SIZE], words[256];
			int friend_count;

			if (!p2p_friends_backup_found(code, current, sizeof(code), &friend_count))
				h3_backup_stop(p2p_friends_backup_text(P2P_FRIENDS_BACKUP_NOTHING));
			else
			{
				char list[64];

				if (friend_count < 0)
					snprintf(list, sizeof(list), "%s", T("the friend code alone"));
				else
					snprintf(list, sizeof(list), T("%d friends in it"), friend_count);
				snprintf(words, sizeof(words), T("Your friend code %s becomes %s\n(%s). Yours now is kept on the memory card.\n"
					"One Vita at a time: friends see the one started last."), current, code, list);
				h3_backup.step = _h3_backup_confirm;
				h3_backup_question(T("RESTORE FRIEND CODE"), words, H3_FRIENDS_BACKUP_CONFIRM, H3_FRIENDS_BACKUP_CANCEL);
				h3_friends_message("");
			}
		}
		else if (outcome != P2P_FRIENDS_BACKUP_WORKING)
		{
			h3_backup.step = _h3_backup_none;
			if (outcome == P2P_FRIENDS_BACKUP_SAVED)
				snprintf(text + strlen(text), sizeof(text) - strlen(text), ". %s", T("Keep it and its password safe."));
			h3_friends_message(text);
		}
	}
	/* (a question gone without an answer: Back) */
	if ((h3_backup.step == _h3_backup_confirm || h3_backup.step == _h3_backup_list) && h3_backup.question_time &&
		system_milliseconds() - h3_backup.question_time > 750)
	{
		h3_backup_stop(h3_backup.step == _h3_backup_confirm ? T("Not restored: your friend code stays") :
			T("Not backed up"));
	}
}

/* (port h3 friends) the rows, the chosen one's facts, the code and status */
static void h3_friends_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *focused = list->focused_child, *focus = NULL;
	struct p2p_friend_entry const *chosen;
	long index, focused_row = NONE, target;
	char text[192], typed[HALO_TEXT_INPUT_SIZE];

	h3_backup_update();
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
			case _h3_friends_row_backup:
				row_column(child, "row_name", T("FRIEND CODE BACKUP"));
				row_column(child, "row_state", T("Save your code to a file, or restore it"));
				row_column(child, "row_tag", h3_backup.step == _h3_backup_working ? T("WORKING") : " ");
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
			N_("A file with a password keeps your code if you lose your memory card."),
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
		case _h3_friends_row_backup:
			h3_backup_menu();
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
	h3_friends.options_title[0] = 0;
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
	case H3_FRIENDS_BACKUP_EXPORT:
		return T("BACK UP FRIEND CODE");
	case H3_FRIENDS_BACKUP_RESTORE:
		return T("RESTORE FRIEND CODE");
	case H3_FRIENDS_BACKUP_WITH_LIST:
		return T("WITH FRIENDS LIST");
	case H3_FRIENDS_BACKUP_CODE_ONLY:
		return T("FRIEND CODE ONLY");
	case H3_FRIENDS_BACKUP_CONFIRM:
		return T("CONFIRM");
	case H3_FRIENDS_BACKUP_CANCEL:
		return T("CANCEL");
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

	/* (FRIEND CODE BACKUP's questions: their own words; seen now) */
	if (h3_friends.options_title[0])
	{
		text_show(named(screen, "options_title"), h3_friends.options_title);
		text_show(named(screen, "options_text"), h3_friends.options_text);
		h3_backup.question_time = system_milliseconds() ? system_milliseconds() : 1;
	}
	else
	{
		text_show(named(screen, "options_title"), h3_friends.options_entry.name);
		snprintf(text, sizeof(text), "%s\n%s", h3_friends.options_entry.code, h3_friends.options_entry.activity);
		text_show(named(screen, "options_text"), text);
	}
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
	if (h3_friends.options[option] >= H3_FRIENDS_BACKUP_EXPORT && h3_friends.options[option] <= H3_FRIENDS_BACKUP_CANCEL)
	{
		int chosen = h3_friends.options[option];

		/* (answered: the question's words not shown again for an entry's) */
		h3_friends.options_title[0] = 0;
		h3_friends.option_count = 0;
		h3_backup_option(chosen);
		return;
	}
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
	struct widget_instance *child, *focused = list->focused_child, *focus = NULL, *found;
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
	/* (Cross's Options: none for this PS Vita's own players, so not shown
	on their rows) */
	{
		boolean options = index >= 0 && h3_players.players[index].machine != h3_players.own_machine;

		if ((found = named(screen, "a_butn")) != NULL)
			widget_instance_set_visibility_recursive(found, options);
		if ((found = named(screen, "a_label")) != NULL)
			widget_instance_set_visibility_recursive(found, options);
	}
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
	/* (this PS Vita's own: no options - the detail says whose it is, and
	Cross's hint is not shown on its row) */
	if (h3_players.players[index].machine == h3_players.own_machine)
	{
		platform_log("menus: h3 players: no options for %s, a player of this PS Vita", h3_players.players[index].name);
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
		/* (MUTE CHAT: another machine's player; UNMUTE CHAT once muted) */
		boolean other = state != P2P_FRIENDS_PLAYER_THIS_VITA;

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
			if (strstr(child->name, "option_mute"))
			{
				widget_instance_set_visibility_recursive(child, other);
				child->disabled = !other;
				text_show(child, game_chat_name_muted(h3_players.players[index].name) ? T("UNMUTE CHAT") :
					T("MUTE CHAT"));
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
	else if (strstr(widget->name, "option_mute"))
	{
		int index = h3_players_chosen_index();
		char text[160];

		if (index < 0 || h3_players.players[index].machine == h3_players.own_machine)
			return;
		snprintf(text, sizeof(text), game_chat_mute_name(h3_players.players[index].name,
			!game_chat_name_muted(h3_players.players[index].name)) ? T("%s muted: their chat is hidden here") :
			T("%s unmuted"), h3_players.players[index].name);
		h3_players_message(text);
	}
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

static boolean h3_scoreboard_takes_triangle(void);

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
	/* (the Halo 3 scoreboard held: its Triangle opens it, whose rows have
	the players' options: h3_scoreboard_frame) */
	else if (!active && !game_engine_showing_postgame() && pad->buttons[_gamepad_binary_button_back] &&
		h3_scoreboard_takes_triangle())
	{
		return;
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

/* ---- port h3 carnage: the Halo 3 CARNAGE REPORT (halo3/carnage.xml) over a
multiplayer game's end, in the place of the game's own report
(game_engine_post_rasterize_post_game), on every machine of the game: opened
once the scores come up (the game's rasterize delay: game_engine_force_
single_screen) when Menu style is Halo 3 and nothing else is up, again if
something closed it. Its numbers are carnage_report.c's (the game's record:
a joined machine's the host's). Cross goes on to the lobby for the host (the
game's own report's A: network_game_server_reset_to_pregame, done at the
next frame, out of the widget's event), by itself after
H3_CARNAGE_CONTINUE_SECONDS on the report; a joined machine waits for the
host, its Circle leaves (asked first: the game's own report's B,
network_game_abort). The scores' own buttons wait while it is up
(carnage_report_holds_buttons). */

#define H3_CARNAGE_SCREEN "h3/carnage/screen"
#define H3_CARNAGE_ROWS 14
#define H3_CARNAGE_COLUMNS 5
#define H3_CARNAGE_CONTINUE_SECONDS 30
#define H3_CARNAGE_MESSAGE_MILLISECONDS 5000

enum
{
	_h3_carnage_summary,
	_h3_carnage_details,
	NUMBER_OF_H3_CARNAGE_PAGES
};

/* a page's column */
enum
{
	_h3_carnage_score,
	_h3_carnage_kills,
	_h3_carnage_deaths,
	_h3_carnage_assists,
	_h3_carnage_spread,
	_h3_carnage_suicides,
	_h3_carnage_betrayals,
	_h3_carnage_objective_0,
	_h3_carnage_objective_1,
	_h3_carnage_ratio,
	_h3_carnage_none,
};

static struct
{
	/* the report is up (or could not be: the game's own then) this game's end */
	boolean shown;
	boolean unavailable;
	short page;
	long offset;
	boolean offset_set;
	/* the host's time with the report up (ms), and the last frame's time */
	unsigned long up_milliseconds;
	unsigned long last_frame;
	/* the host's Cross or its time (gone on), a joined machine's LEAVE: done
	at the next frame */
	boolean go_on_pending;
	boolean gone_on;
	boolean leave_pending;
	char message[160];
	unsigned long message_time;
	char said[96];
	boolean end_said;
	struct carnage_report report;
} h3_carnage;

static long h3_carnage_continue_seconds(void)
{
#ifndef HALO_RELEASE
	char const *seconds = getenv("HALO_TEST_CARNAGE_SECONDS");

	if (seconds && atol(seconds) > 0)
		return atol(seconds);
#endif
	return H3_CARNAGE_CONTINUE_SECONDS;
}

/* (Menu style Halo 3, and menus.carnage_report - HALO_CARNAGE_REPORT - not
"classic") whether the Halo 3 report stands for the game's own */
static boolean h3_carnage_wanted(void)
{
	/* (its variable, live; else config.toml's) */
	char const *report;

	/* (Menu style Halo CE: the game's own unless menus.classic_carnage_report -
	HALO_CLASSIC_CARNAGE_REPORT - is "halo3") */
	if (!halo_menu_style_halo3())
	{
		report = getenv("HALO_CLASSIC_CARNAGE_REPORT");
		if (!report)
			report = config_string("menus.classic_carnage_report");
		return !strcmp(report, "halo3");
	}
	report = getenv("HALO_CARNAGE_REPORT");
	if (!report)
		report = config_string("menus.carnage_report");
	return strcmp(report, "classic") != 0;
}

/* this machine's network game, not split screen's (PLAYERS, LEAVE) */
static boolean h3_carnage_network(void)
{
	return global_network_game_client_get() != NULL && !network_game_is_splitscreen_local();
}

static boolean h3_carnage_hosting(void)
{
	return global_network_game_server_get() != NULL;
}

static void h3_carnage_say(char const *text)
{
	snprintf(h3_carnage.message, sizeof(h3_carnage.message), "%s", text);
	h3_carnage.message_time = system_milliseconds();
	platform_log("menus: h3 carnage: %s", text);
}

/* a place, as the game's report has it ("1st", "2nd"; the language's) */
static void h3_carnage_place(long place, char *text, int size)
{
	long number = place + 1;
	char const *suffix = "th";

	if (number % 100 < 11 || number % 100 > 13)
		suffix = number % 10 == 1 ? "st" : number % 10 == 2 ? "nd" : number % 10 == 3 ? "rd" : "th";
	snprintf(text, (size_t)size, "%ld%s", number, T(suffix));
}

/* the page's columns, in order (a game without teams has no betrayals; the
game type's own where it has them) */
static int h3_carnage_columns(struct carnage_report const *report, short page, int columns[H3_CARNAGE_COLUMNS])
{
	int count = 0, index;

	for (index = 0; index < H3_CARNAGE_COLUMNS; index++)
		columns[index] = _h3_carnage_none;
	if (page == _h3_carnage_summary)
	{
		columns[count++] = _h3_carnage_score;
		columns[count++] = _h3_carnage_kills;
		columns[count++] = _h3_carnage_deaths;
		columns[count++] = _h3_carnage_assists;
		columns[count++] = _h3_carnage_spread;
		return count;
	}
	columns[count++] = _h3_carnage_suicides;
	if (report->teams)
		columns[count++] = _h3_carnage_betrayals;
	if (carnage_report_objective_name(report->type, 0))
		columns[count++] = _h3_carnage_objective_0;
	if (carnage_report_objective_name(report->type, 1))
		columns[count++] = _h3_carnage_objective_1;
	columns[count++] = _h3_carnage_ratio;
	return count;
}

static char const *h3_carnage_column_name(struct carnage_report const *report, int column)
{
	switch (column)
	{
	case _h3_carnage_score:
		switch (report->type)
		{
		case game_engine_ctf:
			return T("FLAGS");
		case game_engine_oddball:
			return T("BALL TIME");
		case game_engine_king:
			return T("HILL TIME");
		case game_engine_race:
			return T("LAPS");
		}
		return T("SCORE");
	case _h3_carnage_kills:
		return T("KILLS");
	case _h3_carnage_deaths:
		return T("DEATHS");
	case _h3_carnage_assists:
		return T("ASSISTS");
	case _h3_carnage_spread:
		return "+/-";
	case _h3_carnage_suicides:
		return T("SUICIDES");
	case _h3_carnage_betrayals:
		return T("BETRAYALS");
	case _h3_carnage_objective_0:
		return T(carnage_report_objective_name(report->type, 0));
	case _h3_carnage_objective_1:
		return T(carnage_report_objective_name(report->type, 1));
	case _h3_carnage_ratio:
		return "K/D";
	}
	return "";
}

static void h3_carnage_value(struct carnage_report const *report, struct carnage_report_row const *row, int column,
	char *text, int size)
{
	switch (column)
	{
	case _h3_carnage_score:
		snprintf(text, (size_t)size, "%s", row->score[0] ? row->score : "0");
		break;
	case _h3_carnage_kills:
		snprintf(text, (size_t)size, "%ld", row->kills);
		break;
	case _h3_carnage_deaths:
		snprintf(text, (size_t)size, "%ld", row->deaths);
		break;
	case _h3_carnage_assists:
		snprintf(text, (size_t)size, "%ld", row->assists);
		break;
	case _h3_carnage_spread:
		snprintf(text, (size_t)size, row->kills > row->deaths ? "+%ld" : "%ld", row->kills - row->deaths);
		break;
	case _h3_carnage_suicides:
		snprintf(text, (size_t)size, "%ld", row->suicides);
		break;
	case _h3_carnage_betrayals:
		snprintf(text, (size_t)size, "%ld", row->betrayals);
		break;
	case _h3_carnage_objective_0:
	case _h3_carnage_objective_1:
		carnage_report_objective_text(report, row, column == _h3_carnage_objective_0 ? 0 : 1, text, size);
		break;
	case _h3_carnage_ratio:
		/* (kills for each death; a player never killed: their kills) */
		snprintf(text, (size_t)size, "%.2f", row->deaths ? (double)row->kills / (double)row->deaths : (double)row->kills);
		break;
	default:
		snprintf(text, (size_t)size, " ");
		break;
	}
}

/* (port h3 carnage: its rows' container, each frame) the report read again
(a late statistic of the host's shows), the title, the result, the page's
columns and the rows shown, the buttons this machine has */
static void h3_carnage_update(struct widget_instance *lines)
{
	struct widget_instance *screen = screen_of(lines);
	struct carnage_report *report = &h3_carnage.report;
	struct network_game *game = network_game_get_game();
	boolean host = h3_carnage_hosting();
	boolean network = h3_carnage_network();
	struct widget_instance *child, *found;
	int columns[H3_CARNAGE_COLUMNS];
	long index, local_row = NONE, first;
	char text[256], name[64], map[96];

	if (!carnage_report_read(report))
		report->row_count = 0;
	carnage_report_log(report, h3_carnage_hosting() ? "the host's report" : "the report (the host's numbers)");
	/* (the game type on the map, at the right of the title) */
	name[0] = map[0] = 0;
	if (game)
	{
		/* (the game type's own name; a variant with none, the game type's) */
		if (game->variant.human_readable_game_description[0])
			h3_mp_game_name(game, name, (int)sizeof(name));
		else
		{
			static char const *const types[] = { "", N_("Capture the Flag"), N_("Slayer"), N_("Oddball"),
				N_("King of the Hill"), N_("Race") };

			snprintf(name, sizeof(name), "%s", report->type > 0 && report->type < (long)NUMBEROF(types) ?
				T(types[report->type]) : "");
		}
		snprintf(map, sizeof(map), "%s", custom_edition_maps_level_title(game->map.name));
	}
	snprintf(text, sizeof(text), name[0] && map[0] ? T("%s on %s") : "%s%s", name, map);
	h3_capitals(text);
	text_show(named(screen, "game"), text);
	/* the result: the winner, or a tie */
	if (report->teams && report->winning_team != NONE)
	{
		for (index = 0; index < report->row_count; index++)
			if (report->rows[index].team_row && report->rows[index].team == report->winning_team)
				break;
		snprintf(text, sizeof(text), T("%s WINS"), index < report->row_count ? report->rows[index].name : "");
	}
	else if (!report->teams && report->winning_row != NONE)
		snprintf(text, sizeof(text), T("%s WINS"), report->rows[report->winning_row].name);
	else
		snprintf(text, sizeof(text), "%s", report->row_count ? T("TIE GAME") : "");
	h3_capitals(text);
	text_show(named(screen, "result"), text);
	/* under it: the teams' scores, or this machine's player's place */
	text[0] = 0;
	for (index = 0; index < report->row_count; index++)
	{
		struct carnage_report_row const *row = &report->rows[index];

		if (row->local && !row->team_row && local_row == NONE)
			local_row = index;
		if (report->teams && row->team_row)
			snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s%s %s", text[0] ? "   " : "", row->name,
				row->score[0] ? row->score : "0");
	}
	if (!report->teams && local_row != NONE)
	{
		char place[24];

		h3_carnage_place(report->rows[local_row].place, place, (int)sizeof(place));
		snprintf(text, sizeof(text), report->rows[local_row].tied ? T("You tied for %s of %ld") : T("You placed %s of %ld"),
			place, report->player_count);
	}
	text_show(named(screen, "result_sub"), text);
	/* the pages, the one shown lit */
	if ((found = named(screen, "tab_summary")) != NULL)
		found->animation.current_frame_index = h3_carnage.page == _h3_carnage_summary ? 1 : 0;
	if ((found = named(screen, "tab_details")) != NULL)
		found->animation.current_frame_index = h3_carnage.page == _h3_carnage_details ? 1 : 0;
	h3_carnage_columns(report, h3_carnage.page, columns);
	for (index = 0; index < H3_CARNAGE_COLUMNS; index++)
	{
		snprintf(name, sizeof(name), "header_%ld", index + 1);
		text_show(named(screen, name), h3_carnage_column_name(report, columns[index]));
	}
	/* (the first time, this machine's player in sight) */
	if (!h3_carnage.offset_set && report->row_count)
	{
		h3_carnage.offset_set = TRUE;
		h3_carnage.offset = local_row != NONE && local_row >= H3_CARNAGE_ROWS ? local_row - H3_CARNAGE_ROWS + 1 : 0;
	}
	h3_carnage.offset = PIN(h3_carnage.offset, 0, MAX(0, report->row_count - H3_CARNAGE_ROWS));
	first = h3_carnage.offset;
	for (index = 0, child = lines->child; child; child = child->next)
	{
		struct carnage_report_row const *row;
		long column;

		if (strncmp(child->name, "line_", 5))
			continue;
		if (first + index >= report->row_count)
		{
			widget_instance_set_visibility_recursive(child, FALSE);
			index++;
			continue;
		}
		row = &report->rows[first + index];
		widget_instance_set_visibility_recursive(child, TRUE);
		/* (this machine's players lit) */
		child->animation.current_frame_index = row->local && !row->team_row ? 1 : 0;
		/* (a team game's places are its teams') */
		if (report->teams && !row->team_row)
			text[0] = 0;
		else
			h3_carnage_place(row->place, text, (int)sizeof(text));
		text_show(named(child, "row_place"), text);
		if ((found = named(child, "row_won")) != NULL)
			widget_instance_set_visibility_recursive(found, row->won);
		snprintf(text, sizeof(text), "%s%s", row->name, row->quit ? T(" (left)") : "");
		row_column(child, "row_name", text);
		if ((found = named(child, "row_name")) != NULL)
			found->animation.current_frame_index = (short)(row->color < 0 || row->color >= 18 ? 0 : row->color + 1);
		for (column = 0; column < H3_CARNAGE_COLUMNS; column++)
		{
			char value[32];

			snprintf(name, sizeof(name), "row_%ld", column + 1);
			h3_carnage_value(report, row, columns[column], value, (int)sizeof(value));
			text_show(named(child, name), value);
		}
		index++;
	}
	if (report->row_count > H3_CARNAGE_ROWS)
		snprintf(text, sizeof(text), T("%ld-%ld of %ld: up and down for more"), first + 1,
			MIN(first + H3_CARNAGE_ROWS, report->row_count), report->row_count);
	else
		text[0] = 0;
	text_show(named(screen, "scroll"), text);
	text_show(named(screen, "message"), h3_carnage.message[0] &&
		system_milliseconds() - h3_carnage.message_time < H3_CARNAGE_MESSAGE_MILLISECONDS ? h3_carnage.message : "");
	/* the buttons: Cross the host's (once the scores are up), Triangle a
	network game's, Circle a joined machine's */
	{
		boolean go_on = host && game_engine_showing_postgame() && !h3_carnage.gone_on;

		if ((found = named(screen, "a_butn")) != NULL)
			widget_instance_set_visibility_recursive(found, go_on);
		text_show(named(screen, "a_label"), go_on ? T("Continue") : host ? "" : T("Waiting for the host"));
		if ((found = named(screen, "y_butn")) != NULL)
			widget_instance_set_visibility_recursive(found, network);
		if ((found = named(screen, "y_label")) != NULL)
			widget_instance_set_visibility_recursive(found, network);
		if ((found = named(screen, "b_butn")) != NULL)
			widget_instance_set_visibility_recursive(found, network && !host);
		if ((found = named(screen, "b_label")) != NULL)
			widget_instance_set_visibility_recursive(found, network && !host);
		text[0] = 0;
		if (go_on)
		{
			long left = h3_carnage_continue_seconds() - (long)(h3_carnage.up_milliseconds / 1000);

			snprintf(text, sizeof(text), T("To the lobby in %ld"), MAX(left, 0L));
		}
		text_show(named(screen, "status"), text);
	}
	snprintf(text, sizeof(text), "page %d, rows %ld-%ld of %ld, %s", h3_carnage.page, first + 1,
		MIN(first + H3_CARNAGE_ROWS, report->row_count), report->row_count, host ? "the host" : "joined");
	if (strcmp(text, h3_carnage.said))
	{
		snprintf(h3_carnage.said, sizeof(h3_carnage.said), "%s", text);
		platform_log("menus: h3 carnage: %s", text);
	}
}

/* (Cross) the host goes on to the lobby, everyone with it, at the next frame */
static void h3_carnage_continue(void)
{
	if (!h3_carnage_hosting())
	{
		h3_carnage_say(T("Waiting for the host to go on"));
		return;
	}
	if (!game_engine_showing_postgame() || h3_carnage.gone_on)
		return;
	h3_carnage.go_on_pending = TRUE;
}

/* (each frame, pc_menu_functions_update) the report opened over the game's
end, the host's time on it, what its buttons asked for */
static void h3_carnage_frame(boolean main_menu_loaded)
{
#ifndef HALO_DEDICATED_SERVER
	boolean over = !main_menu_loaded && game_engine_running() && game_engine_force_single_screen();
	unsigned long now = system_milliseconds();
	struct widget_instance *active;
	long tag;
	short index;

	if (!over)
	{
		if (h3_carnage.shown)
			platform_log("menus: h3 carnage: closed with the game");
		memset(&h3_carnage, 0, sizeof(h3_carnage) - sizeof(h3_carnage.report));
		carnage_report_set_shown(FALSE);
		return;
	}
	if (!h3_carnage.end_said)
	{
		h3_carnage.end_said = TRUE;
		platform_log("menus: h3 carnage: the game is over: %s", h3_carnage_wanted() ? "the Halo 3 carnage report" :
			halo_menu_style_halo3() ? "the game's own report (menus.carnage_report classic)" :
			"the game's own report (Menu style Halo CE, menus.classic_carnage_report classic)");
	}
	if (h3_carnage.leave_pending)
	{
		h3_carnage.leave_pending = FALSE;
		platform_log("menus: h3 carnage: LEAVE GAME: the game left");
		network_game_abort();
		return;
	}
	if (h3_carnage.go_on_pending)
	{
		struct network_game_server *server = global_network_game_server_get();

		h3_carnage.go_on_pending = FALSE;
		if (server && game_engine_showing_postgame() && !h3_carnage.gone_on)
		{
			h3_carnage.gone_on = TRUE;
			platform_log("menus: h3 carnage: the host goes on: the lobby");
			network_game_server_reset_to_pregame(server);
		}
		return;
	}
	if (h3_carnage.unavailable || !h3_carnage_wanted())
	{
		carnage_report_set_shown(FALSE);
		return;
	}
	tag = menu_tags_ingame_screen(H3_CARNAGE_SCREEN);
	if (tag == NONE)
	{
		h3_carnage.unavailable = TRUE;
		platform_log("menus: h3 carnage: not in this map's menus: the game's own report");
		return;
	}
	active = ui_widget_port_active_screen(0);
	/* (opened when nothing is up - the game's own report meanwhile -, and
	again if something closed it) */
	if (!active)
	{
		for (index = 1; index < MAXIMUM_LOCAL_PLAYERS; index++)
			if (ui_widget_port_active_screen(index))
				return;
		if (!ui_widget_load_by_name_or_tag(NULL, tag, NULL, 0, NONE, NONE, NONE))
		{
			h3_carnage.unavailable = TRUE;
			carnage_report_set_shown(FALSE);
			platform_log("menus: h3 carnage: the screen did not open: the game's own report");
			return;
		}
		platform_log("menus: h3 carnage: the carnage report %s (%s)", h3_carnage.shown ? "opened again" : "opened",
			h3_carnage_hosting() ? "the host" : "joined");
		h3_carnage.shown = TRUE;
		carnage_report_set_shown(TRUE);
		h3_carnage.last_frame = now;
		return;
	}
	/* (the host: by itself after a while on the report - not while another
	screen is over it) */
	if (h3_carnage.shown && h3_carnage_hosting() && !h3_carnage.gone_on && game_engine_showing_postgame() &&
		active->definition_tag_index == tag)
	{
		if (h3_carnage.last_frame)
			h3_carnage.up_milliseconds += MIN(now - h3_carnage.last_frame, 1000UL);
		if (h3_carnage.up_milliseconds >= (unsigned long)h3_carnage_continue_seconds() * 1000UL)
		{
			platform_log("menus: h3 carnage: %ld s on the report: the lobby by itself", h3_carnage_continue_seconds());
			h3_carnage.go_on_pending = TRUE;
		}
	}
	h3_carnage.last_frame = now;
#endif
}

/* ---- port h3 scoreboard: the Halo 3 SCOREBOARD (halo3/scoreboard.xml) in
the place of the game's own scores (game_engine.c
game_engine_rasterize_in_game_score) while Back is held in a multiplayer
game, with Menu style Halo 3 and menus.scoreboard (HALO_SCOREBOARD) not
"classic": every network (local and split screen, System Link, ad hoc,
online, a dedicated server's or a PC host's), each local player's own in
their view (split screen's smaller one, scoreboard_split). Held, it is a
screen held up without being open (ui_widget_port_held_screen_show): the
player plays on, nothing of the controller is taken, Back let go and it is
gone. Triangle while it is held (one player on this machine) opens it as a
screen of the player's own, where the D-pad, the stick, L and R and a drag
on the touch screen scroll it and Cross on a player opens their options
(MUTE CHAT, ADD FRIEND or the friend's state, KICK for the game's host: its
network_anticheat_drop, which refuses any machine not hosting); Circle, or
Back pressed again, closes it. The rows are the game's own ranking and
statistics (carnage_report_read_in_game: on a joined machine the host's, as
the distributed netcode sends them), read a few times a second
(H3_SCOREBOARD_READ_MILLISECONDS), the widgets' text set again only when
they or the rows shown changed: nothing is made each frame */

#define H3_SCOREBOARD_SCREEN "h3/scoreboard/screen"
#define H3_SCOREBOARD_SPLIT_SCREEN "h3/scoreboard_split/screen"
#define H3_SCOREBOARD_READ_MILLISECONDS 250
#define H3_SCOREBOARD_MESSAGE_MILLISECONDS 5000
/* (a drag on the touch screen: how far before it is one, and a row's
height, as the list's rows are placed) */
#define H3_SCOREBOARD_DRAG_SLOP 10
#define H3_SCOREBOARD_ROW_PITCH 15
/* (the open screen's list, on the menus' 640x480: a drag starts there; the
panel's left, and the first row's top) */
#define H3_SCOREBOARD_LIST_LEFT 141
#define H3_SCOREBOARD_LIST_TOP 36
#define H3_SCOREBOARD_LIST_RIGHT 499
#define H3_SCOREBOARD_LIST_BOTTOM 306
/* (held, the compact panel - 288 of the open panel's 396 - in the middle of
the screen: the open layout's left part, moved right by this) */
#define H3_SCOREBOARD_HELD_SHIFT 35
/* (held: with more players than this, the leaders and the player's own row,
and how many more) */
#define H3_SCOREBOARD_HELD_PLAYERS 16
/* (the views whose rows were last filled: the held ones, a local player
each, and the open screen) */
#define H3_SCOREBOARD_VIEWS (MAXIMUM_LOCAL_PLAYERS + 1)

/* the row frames (h3/scoreboard_row) */
enum
{
	_h3_scoreboard_row_plain,
	_h3_scoreboard_row_cursor,
	_h3_scoreboard_row_own,
};

struct h3_scoreboard_row
{
	/* a team's band (its score) or a player's row */
	boolean team_row;
	short team;
	/* a team's place (1 the leading team) */
	short rank;
	/* the emblem's colour (the game's eighteen; the team's in a team
	game; NONE) */
	short color;
	long player_index;
	short machine;
	/* this machine's player (NONE: another machine's) */
	short local_player_index;
	boolean bot;
	boolean party;
	boolean flame;
	boolean host;
	/* dead (respawning) or out of lives: dimmed */
	boolean dead;
	/* the ping's bars (0 none known), NONE where no ping is shown (not a
	network game, a bot) */
	short ping_bars;
	char name[48];
	char score[24];
	long kills;
	long deaths;
	long assists;
};

/* a list whose rows were filled: what for (the rows read, the first row
shown, the cursor's row) */
struct h3_scoreboard_view
{
	struct widget_instance *list;
	long creation_time;
	unsigned long generation;
	long offset;
	long cursor;
	short local_player_index;
};

static struct
{
	/* the game's rows as last read (H3_SCOREBOARD_READ_MILLISECONDS), and
	the title and rules over them */
	struct carnage_report report;
	struct h3_scoreboard_row rows[CARNAGE_REPORT_MAXIMUM_ROWS];
	long row_count;
	long player_count;
	boolean teams;
	boolean network;
	boolean hosting;
	unsigned long generation;
	unsigned long read_time;
	boolean read_once;
	char title[128];
	char rules[96];
	/* (each local player) Back held now, Triangle's last state, its held
	view shown */
	boolean back_down[MAXIMUM_LOCAL_PLAYERS];
	boolean y_down[MAXIMUM_LOCAL_PLAYERS];
	boolean held[MAXIMUM_LOCAL_PLAYERS];
	/* the screen's pictures were read (at the game's start, not at the first
	hold); the set is not in this map */
	boolean warmed;
	boolean unavailable;
	struct h3_scoreboard_view views[H3_SCOREBOARD_VIEWS];
	/* the open screen: its local player, Back let go since it opened, the
	view, the player chosen (their player index and name), a page asked */
	boolean open;
	short open_local_player;
	boolean open_back_released;
	struct browser_view view;
	short page;
	long chosen_player;
	char chosen_name[48];
	/* the selector's last text (the open screen: set again when it changes) */
	char selector_key[64];
	/* a drag on the touch screen: started at, the row chosen then */
	boolean touching;
	boolean dragging;
	short drag_start_y;
	long drag_start_row;
	char message[160];
	unsigned long message_time;
	boolean message_new;
	boolean message_shown;
	/* (what halo.log was last told: the tests read it) */
	char said_rows[1024];
	char said_options[160];
} h3_scoreboard;

static boolean h3_scoreboard_other_person(struct h3_scoreboard_row const *row);

/* (Menu style Halo 3, and menus.scoreboard - HALO_SCOREBOARD - not
"classic") whether Halo 3's scoreboard stands for the game's own */
static boolean h3_scoreboard_wanted(void)
{
	/* (its variable, live; else config.toml's) */
	char const *board;

	/* (Menu style Halo CE: the game's own unless menus.classic_scoreboard -
	HALO_CLASSIC_SCOREBOARD - is "halo3") */
	if (!halo_menu_style_halo3())
	{
		board = getenv("HALO_CLASSIC_SCOREBOARD");
		if (!board)
			board = config_string("menus.classic_scoreboard");
		return !strcmp(board, "halo3");
	}
	board = getenv("HALO_SCOREBOARD");
	if (!board)
		board = config_string("menus.scoreboard");
	return strcmp(board, "classic") != 0;
}

/* (tests, never in release builds) HALO_TEST_SCOREBOARD_PARTY and
HALO_TEST_SCOREBOARD_FLAME: names (commas between them) marked as the
party's and as wearing the flame, as a party and a checked tester would be;
HALO_TEST_SCOREBOARD_SPLIT=1 the split screen's view with one player */
static boolean h3_scoreboard_test_name(char const *variable, char const *name)
{
#ifndef HALO_RELEASE
	char const *names = getenv(variable);
	size_t length = strlen(name);

	while (names && *names && length)
	{
		char const *end = strchr(names, ',');
		size_t size = end ? (size_t)(end - names) : strlen(names);

		if (size == length && !strncmp(names, name, length))
			return TRUE;
		names = end ? end + 1 : NULL;
	}
#else
	(void)variable;
	(void)name;
#endif
	return FALSE;
}

static boolean h3_scoreboard_test_split(void)
{
#ifndef HALO_RELEASE
	char const *split = getenv("HALO_TEST_SCOREBOARD_SPLIT");

	return split && split[0] == '1';
#else
	return FALSE;
#endif
}

static boolean h3_scoreboard_split(void)
{
	return local_player_count() > 1 || h3_scoreboard_test_split();
}

static void h3_scoreboard_say(char const *text)
{
	snprintf(h3_scoreboard.message, sizeof(h3_scoreboard.message), "%s", text);
	h3_scoreboard.message_time = system_milliseconds();
	h3_scoreboard.message_new = TRUE;
	platform_log("menus: h3 scoreboard: %s", text);
}

/* the game's rows, the title (the game type on the map) and the rules (the
score to win, the time left), read again when they are older than
H3_SCOREBOARD_READ_MILLISECONDS */
static void h3_scoreboard_read(boolean now)
{
	struct carnage_report *report = &h3_scoreboard.report;
	struct network_game_client *client = global_network_game_client_get();
	struct network_game *game = network_game_get_game();
	unsigned long time = system_milliseconds();
	boolean in_party;
	long index, teams_seen = 0;
	char name[64], map[96];

	if (!now && h3_scoreboard.read_once && time - h3_scoreboard.read_time < H3_SCOREBOARD_READ_MILLISECONDS)
		return;
	h3_scoreboard.read_once = TRUE;
	h3_scoreboard.read_time = time;
	h3_scoreboard.generation++;
	h3_scoreboard.network = client && !network_game_is_splitscreen_local();
	h3_scoreboard.hosting = h3_scoreboard.network && global_network_game_server_get() != NULL;
	in_party = p2p_party_in_party() != 0;
	if (!carnage_report_read_in_game(report))
		report->row_count = 0;
	h3_scoreboard.teams = report->teams;
	h3_scoreboard.player_count = report->player_count;
	h3_scoreboard.row_count = 0;
	for (index = 0; index < report->row_count; index++)
	{
		struct carnage_report_row const *source = &report->rows[index];
		struct h3_scoreboard_row *row = &h3_scoreboard.rows[h3_scoreboard.row_count++];
		struct player_datum *player;

		memset(row, 0, sizeof(*row));
		row->team_row = source->team_row;
		row->team = source->team;
		if (row->team_row)
			row->rank = (short)(++teams_seen);
		row->color = source->color;
		row->player_index = source->player_index;
		row->machine = source->machine;
		row->local_player_index = source->local_player_index;
		row->ping_bars = NONE;
		snprintf(row->name, sizeof(row->name), "%s", source->name);
		snprintf(row->score, sizeof(row->score), "%s", source->score[0] ? source->score : "0");
		row->kills = source->kills;
		row->deaths = source->deaths;
		row->assists = source->assists;
		if (row->team_row)
			continue;
		player = row->player_index != NONE ? player_try_and_get(row->player_index) : NULL;
		row->bot = bots_player_is_bot(row->player_index) != 0;
		row->dead = player && (player->unit_index == NONE || game_engine_player_is_out_of_lives(row->player_index));
		row->flame = flaming_helmet_player_shown(row->player_index) ||
			h3_scoreboard_test_name("HALO_TEST_SCOREBOARD_FLAME", row->name);
		if (!row->bot)
		{
			/* (this machine's own players are in its party when it is in one;
			another machine's by their card or name: p2p_party_player_member) */
			row->party = (row->local_player_index != NONE ? in_party :
				h3_scoreboard.network && p2p_party_player_member(row->machine, row->name)) ||
				h3_scoreboard_test_name("HALO_TEST_SCOREBOARD_PARTY", row->name);
			if (h3_scoreboard.network && latency_meter_shown())
			{
				long ping = NONE;

				row->ping_bars = latency_meter_player_bars((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(row->player_index), &ping);
				/* (the host's players: this machine's on the host; on a joined
				machine those the host's table times at 0 ms, its own) */
				row->host = h3_scoreboard.hosting ? row->local_player_index != NONE :
					row->local_player_index == NONE && ping == 0;
			}
			else
				row->host = h3_scoreboard.hosting && row->local_player_index != NONE;
		}
	}
#ifndef HALO_RELEASE
	/* (tests) HALO_TEST_SCOREBOARD_EXTRA=N: N more bots' rows at the end (a
	free for all; the game itself holds sixteen players here): the scoreboard's
	more-than-sixteen view */
	if (!h3_scoreboard.teams && getenv("HALO_TEST_SCOREBOARD_EXTRA"))
	{
		long extra = MIN(atol(getenv("HALO_TEST_SCOREBOARD_EXTRA")), 40), more;

		for (more = 0; more < extra && h3_scoreboard.row_count < CARNAGE_REPORT_MAXIMUM_ROWS; more++)
		{
			struct h3_scoreboard_row *row = &h3_scoreboard.rows[h3_scoreboard.row_count++];

			memset(row, 0, sizeof(*row));
			row->player_index = NONE;
			row->machine = NONE;
			row->local_player_index = NONE;
			row->color = (short)(more % 18);
			row->bot = TRUE;
			row->ping_bars = NONE;
			snprintf(row->name, sizeof(row->name), "Extra%02ld", more + 1);
			snprintf(row->score, sizeof(row->score), "0");
			h3_scoreboard.player_count++;
		}
	}
#endif
	/* the game type on the map, the score to win, the time left */
	name[0] = map[0] = 0;
	if (game)
	{
		h3_mp_game_name(game, name, (int)sizeof(name));
		snprintf(map, sizeof(map), "%s", custom_edition_maps_level_title(game->map.name));
	}
	snprintf(h3_scoreboard.title, sizeof(h3_scoreboard.title), name[0] && map[0] ? T("%s on %s") : "%s%s", name, map);
	h3_capitals(h3_scoreboard.title);
	h3_scoreboard.rules[0] = 0;
	if (game && game->variant.universal_variant.score_to_win > 0)
		snprintf(h3_scoreboard.rules, sizeof(h3_scoreboard.rules), T("TO WIN %ld"),
			(long)game->variant.universal_variant.score_to_win);
	if (game_variant_options_get()->time_limit > 0 && game_engine_in_play())
	{
		long left = game_variant_options_get()->time_limit * 60L * TICKS_PER_SECOND - game_time_get();
		char time_text[32], line[64];

		left = MAX(left, 0) / TICKS_PER_SECOND;
		snprintf(time_text, sizeof(time_text), "%ld:%02ld", left / 60, left % 60);
		snprintf(line, sizeof(line), T("%s LEFT"), time_text);
		snprintf(h3_scoreboard.rules + strlen(h3_scoreboard.rules), sizeof(h3_scoreboard.rules) - strlen(h3_scoreboard.rules),
			"%s%s", h3_scoreboard.rules[0] ? " \xc2\xb7 " : "", line);
	}
	/* (said when the rows change: the tests compare the machines') */
#ifndef HALO_RELEASE
	{
		char line[sizeof(h3_scoreboard.said_rows)];
		size_t used;

		used = (size_t)snprintf(line, sizeof(line), "%ld rows, %s:", h3_scoreboard.row_count,
			h3_scoreboard.teams ? "teams" : "free for all");
		for (index = 0; index < h3_scoreboard.row_count && used < sizeof(line) - 1; index++)
		{
			struct h3_scoreboard_row const *row = &h3_scoreboard.rows[index];

			if (row->team_row)
				used += (size_t)snprintf(line + used, sizeof(line) - used, " | team %s %s", row->name, row->score);
			else
				used += (size_t)snprintf(line + used, sizeof(line) - used, " | %s %s %ld/%ld/%ld%s%s%s%s%s%s", row->name,
					row->score, row->kills, row->deaths, row->assists, row->local_player_index != NONE ? " own" : "",
					row->bot ? " bot" : "", row->host ? " host" : "", row->party ? " party" : "",
					row->flame ? " flame" : "", row->dead ? " dead" : "");
		}
		if (strcmp(line, h3_scoreboard.said_rows))
		{
			snprintf(h3_scoreboard.said_rows, sizeof(h3_scoreboard.said_rows), "%s", line);
			platform_log("menus: h3 scoreboard: %s", line);
		}
	}
#endif
}

/* the row of the player chosen (-1: gone) */
static long h3_scoreboard_chosen_row(void)
{
	long index;

	for (index = 0; index < h3_scoreboard.row_count; index++)
	{
		struct h3_scoreboard_row const *row = &h3_scoreboard.rows[index];

		if (!row->team_row && row->player_index == h3_scoreboard.chosen_player &&
			!strcmp(row->name, h3_scoreboard.chosen_name))
		{
			return index;
		}
	}
	return -1;
}

static void h3_scoreboard_choose(long index)
{
	if (index < 0 || index >= h3_scoreboard.row_count)
		return;
	h3_scoreboard.chosen_player = h3_scoreboard.rows[index].team_row ? NONE : h3_scoreboard.rows[index].player_index;
	snprintf(h3_scoreboard.chosen_name, sizeof(h3_scoreboard.chosen_name), "%s", h3_scoreboard.rows[index].name);
}

/* the list's view entry (made for a list not seen before: its rows filled
whole at once) */
static struct h3_scoreboard_view *h3_scoreboard_view_of(struct widget_instance *list, short local_player_index)
{
	struct h3_scoreboard_view *free_view = NULL;
	long index;

	for (index = 0; index < H3_SCOREBOARD_VIEWS; index++)
	{
		struct h3_scoreboard_view *view = &h3_scoreboard.views[index];

		if (view->list == list && view->creation_time == list->creation_time)
			return view;
		if (!free_view && (!view->list || view->local_player_index == local_player_index))
			free_view = view;
	}
	if (!free_view)
		free_view = &h3_scoreboard.views[0];
	memset(free_view, 0, sizeof(*free_view));
	free_view->list = list;
	free_view->creation_time = list->creation_time;
	free_view->offset = -1;
	free_view->cursor = -1;
	free_view->local_player_index = local_player_index;
	return free_view;
}

/* the views of a local player's lists forgotten (a screen made again: its
rows filled whole) */
static void h3_scoreboard_forget_views(short local_player_index)
{
	long index;

	for (index = 0; index < H3_SCOREBOARD_VIEWS; index++)
		if (h3_scoreboard.views[index].local_player_index == local_player_index)
			memset(&h3_scoreboard.views[index], 0, sizeof(h3_scoreboard.views[index]));
}

/* (open) a drag on the touch screen scrolls the rows under the finger: the
first row shown moved a row for each row's height it went (the choice kept
on a row shown); TRUE while it drags, *offset the first row */
static boolean h3_scoreboard_drag(long rows, long *offset)
{
	short x, y;
	boolean held;

	if (!ui_widget_port_pointer(&x, &y, &held) || !held)
	{
		h3_scoreboard.touching = FALSE;
		h3_scoreboard.dragging = FALSE;
		return FALSE;
	}
	if (!h3_scoreboard.touching)
	{
		/* (down on the list: a drag may start) */
		if (x < H3_SCOREBOARD_LIST_LEFT || x >= H3_SCOREBOARD_LIST_RIGHT || y < H3_SCOREBOARD_LIST_TOP ||
			y >= H3_SCOREBOARD_LIST_BOTTOM)
		{
			return FALSE;
		}
		h3_scoreboard.touching = TRUE;
		h3_scoreboard.dragging = FALSE;
		h3_scoreboard.drag_start_y = y;
		h3_scoreboard.drag_start_row = h3_scoreboard.view.offset;
		return FALSE;
	}
	if (!h3_scoreboard.dragging)
	{
		if (ABS(y - h3_scoreboard.drag_start_y) < H3_SCOREBOARD_DRAG_SLOP)
			return FALSE;
		/* (a drag: the finger's moves and lift are its, no highlight or tap) */
		h3_scoreboard.dragging = TRUE;
		ui_widget_port_pointer_drag(TRUE);
		platform_log("menus: h3 scoreboard: a drag on the rows");
	}
	*offset = PIN(h3_scoreboard.drag_start_row + (h3_scoreboard.drag_start_y - y) / H3_SCOREBOARD_ROW_PITCH, 0,
		MAX(0, h3_scoreboard.row_count - rows));
	return TRUE;
}

static void h3_scoreboard_show(struct widget_instance *widget, boolean shown)
{
	if (widget)
		widget_instance_set_visibility_recursive(widget, shown);
}

/* a row's widgets: what shows, its pictures' frames, its text. Held (not
open, not split screen's) the compact panel's: the narrow band and tint, the
own row's yellow outline of its own, no kills, deaths or assists */
static void h3_scoreboard_fill_row(struct widget_instance *child, struct h3_scoreboard_row const *row, boolean split,
	boolean open, boolean teams)
{
	struct widget_instance *found;
	boolean compact = !open && !split, team = row->team_row, player = !team;
	char text[32];

	h3_scoreboard_show(named(child, "row_band"), team && !compact);
	h3_scoreboard_show(named(child, "row_band_n"), team && compact);
	h3_scoreboard_show(named(child, "row_tint"), player && !compact);
	h3_scoreboard_show(named(child, "row_tint_n"), player && compact);
	h3_scoreboard_show(named(child, "row_own_n"), player && compact && row->local_player_index != NONE);
	h3_scoreboard_show(named(child, "row_rank"), team);
	h3_scoreboard_show(named(child, "row_team"), team);
	h3_scoreboard_show(named(child, "row_team_score"), team);
	h3_scoreboard_show(named(child, "row_emblem"), player);
	h3_scoreboard_show(named(child, "row_name"), player);
	h3_scoreboard_show(named(child, "row_score"), player);
	h3_scoreboard_show(named(child, "row_kills"), player && open);
	h3_scoreboard_show(named(child, "row_deaths"), player && open);
	h3_scoreboard_show(named(child, "row_assists"), player && open);
	h3_scoreboard_show(named(child, "row_host"), player && row->host);
	h3_scoreboard_show(named(child, "row_party"), player && row->party);
	h3_scoreboard_show(named(child, "row_flame"), player && row->flame);
	h3_scoreboard_show(named(child, "row_bot"), player && row->bot);
	/* (split screen's: the ping's place is BOT's on a bot's row) */
	h3_scoreboard_show(named(child, "row_ping"), player && row->ping_bars != NONE && !(split && row->bot));
	child->alpha_modifier = player && row->dead ? 0.45f : 1.0f;
	if (team)
	{
		short frame = (short)PIN(row->team, 0, 7);

		if ((found = named(child, "row_band")) != NULL)
			found->animation.current_frame_index = frame;
		if ((found = named(child, "row_band_n")) != NULL)
			found->animation.current_frame_index = frame;
		snprintf(text, sizeof(text), "%d", row->rank);
		text_show(named(child, "row_rank"), text);
		text_show(named(child, "row_team"), row->name);
		text_show(named(child, "row_team_score"), row->score);
		return;
	}
	/* (a team game's rows in their team's colour, a free for all's steel) */
	if ((found = named(child, "row_tint")) != NULL)
		found->animation.current_frame_index = (short)(teams ? PIN(row->team, 0, 7) : 8);
	if ((found = named(child, "row_tint_n")) != NULL)
		found->animation.current_frame_index = (short)(teams ? PIN(row->team, 0, 7) : 8);
	if ((found = named(child, "row_emblem")) != NULL)
		found->animation.current_frame_index = (short)(row->color < 0 || row->color >= 18 ? 0 : row->color + 1);
	if ((found = named(child, "row_ping")) != NULL)
		found->animation.current_frame_index = (short)PIN(row->ping_bars, 0, 4);
	text_show(named(child, "row_name"), row->name);
	text_show(named(child, "row_score"), row->score);
	snprintf(text, sizeof(text), "%ld", row->kills);
	text_show(named(child, "row_kills"), text);
	snprintf(text, sizeof(text), "%ld", row->deaths);
	text_show(named(child, "row_deaths"), text);
	snprintf(text, sizeof(text), "%ld", row->assists);
	text_show(named(child, "row_assists"), text);
}

/* (the open screen) what was said shown for a while, then gone; its text
set when it is said (h3_scoreboard_say marks it) */
static void h3_scoreboard_message_update(struct widget_instance *screen)
{
	boolean shown = h3_scoreboard.message[0] &&
		system_milliseconds() - h3_scoreboard.message_time < H3_SCOREBOARD_MESSAGE_MILLISECONDS;
	struct widget_instance *message;

	if (h3_scoreboard.message_shown == shown && !h3_scoreboard.message_new)
		return;
	message = named(screen, "message");
	if (message)
	{
		if (h3_scoreboard.message_new)
			text_show(message, h3_scoreboard.message);
		widget_instance_set_visibility_recursive(message, shown);
	}
	h3_scoreboard.message_shown = shown;
	h3_scoreboard.message_new = FALSE;
}

/* a widget of the screen put at a place of it (its left and top are 0 in the
XML: the rows' end is not known there) */
static void h3_scoreboard_place(struct widget_instance *screen, char const *name, short x, short y)
{
	struct widget_instance *widget = named(screen, name);

	if (widget)
	{
		widget->horizontal_offset = x;
		widget->vertical_offset = y;
	}
}

/* (the open screen) the selector strip: the player Cross acts on - their
emblem and name; set again only when they change, or when the rows were */
static void h3_scoreboard_selector_update(struct widget_instance *screen, boolean force)
{
	long chosen = h3_scoreboard_chosen_row();
	char key[sizeof(h3_scoreboard.selector_key)];
	struct widget_instance *emblem = named(screen, "selector_emblem");

	snprintf(key, sizeof(key), "%d:%s", chosen >= 0 ? h3_scoreboard.rows[chosen].color : NONE,
		chosen >= 0 ? h3_scoreboard.rows[chosen].name : "");
	if (!force && !strcmp(key, h3_scoreboard.selector_key))
		return;
	snprintf(h3_scoreboard.selector_key, sizeof(h3_scoreboard.selector_key), "%s", key);
	text_show(named(screen, "selector_name"), chosen >= 0 ? h3_scoreboard.rows[chosen].name : "");
	if (emblem)
	{
		widget_instance_set_visibility_recursive(emblem, chosen >= 0);
		if (chosen >= 0)
			emblem->animation.current_frame_index = (short)(h3_scoreboard.rows[chosen].color < 0 ||
				h3_scoreboard.rows[chosen].color >= 18 ? 0 : h3_scoreboard.rows[chosen].color + 1);
	}
}

/* (port h3 scoreboard: a held view's list or the open screen's, as it is
drawn) the rows read when due, the rows shown, the cursor's (open), the
title and rules */
static void h3_scoreboard_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	short local_player_index = screen ? (short)PIN(screen->local_player_index, 0, MAXIMUM_LOCAL_PLAYERS - 1) : 0;
	boolean open = screen && screen != ui_widget_port_held_screen(local_player_index);
	boolean split = list->type != _ui_widget_type_column_list;
	struct h3_scoreboard_view *view;
	struct widget_instance *child, *focus = NULL;
	long rows = 0, cap, index, offset, cursor = -1, own = -1, focused_row = -1, pinned = -1;
	boolean refill;
	char text[160];

	h3_scoreboard_read(FALSE);
	view = h3_scoreboard_view_of(list, local_player_index);
	for (child = list->child; child; child = child->next)
	{
		if (strncmp(child->name, "row_", 4))
			continue;
		if (child == list->focused_child)
			focused_row = rows;
		rows++;
	}
	cap = rows;
	/* (this local player's own row: its place in view when held) */
	for (index = 0; index < h3_scoreboard.row_count; index++)
		if (!h3_scoreboard.rows[index].team_row && h3_scoreboard.rows[index].local_player_index == local_player_index)
			own = index;
	if (open)
	{
		long chosen = h3_scoreboard_chosen_row(), target;
		boolean dragged;

		if (view->offset < 0)
		{
			/* (opened: the cursor on the player's own row) */
			browser_view_reset(&h3_scoreboard.view);
			focused_row = -1;
			if (chosen < 0)
				chosen = own >= 0 ? own : 0;
#ifndef HALO_RELEASE
			/* (tests) HALO_TEST_SCOREBOARD_CHOOSE=other: the cursor on the
			first other machine's person */
			if (getenv("HALO_TEST_SCOREBOARD_CHOOSE") && !strcmp(getenv("HALO_TEST_SCOREBOARD_CHOOSE"), "other"))
			{
				for (index = 0; index < h3_scoreboard.row_count; index++)
					if (h3_scoreboard_other_person(&h3_scoreboard.rows[index]))
					{
						chosen = index;
						break;
					}
			}
#endif
		}
		{
			long dragged_offset = h3_scoreboard.view.offset;

			dragged = h3_scoreboard_drag(rows, &dragged_offset);
			if (dragged)
			{
				/* (the rows moved; the choice on a row still shown) */
				long current = chosen >= 0 ? chosen : h3_scoreboard.view.offset + MAX(h3_scoreboard.view.row, 0);

				h3_scoreboard.view.offset = (int)dragged_offset;
				chosen = PIN(current, dragged_offset, MIN(dragged_offset + rows, h3_scoreboard.row_count) - 1);
			}
		}
		target = browser_view_follow(&h3_scoreboard.view, (int)rows, (int)h3_scoreboard.row_count,
			dragged ? -1 : (int)focused_row, (int)chosen, h3_scoreboard.page);
		h3_scoreboard.page = 0;
		if (target >= 0)
			h3_scoreboard_choose(target);
		offset = h3_scoreboard.view.offset;
		cursor = h3_scoreboard.view.row;
	}
	else
	{
		/* (held: the first rows - the leaders -, the player's own in the last
		row when it is further down) */
		offset = 0;
		if (!split && h3_scoreboard.player_count > H3_SCOREBOARD_HELD_PLAYERS)
			cap = MIN(rows, H3_SCOREBOARD_HELD_PLAYERS + 1);
		if (own >= cap)
			pinned = own;
	}
	refill = view->generation != h3_scoreboard.generation || view->offset != offset || view->cursor != pinned;
	view->generation = h3_scoreboard.generation;
	view->offset = offset;
	view->cursor = pinned;
	for (index = 0, child = list->child; child; child = child->next)
	{
		struct h3_scoreboard_row const *row;

		if (strncmp(child->name, "row_", 4))
			continue;
		if (offset + index >= h3_scoreboard.row_count || index >= cap)
		{
			if (refill)
			{
				widget_instance_set_visibility_recursive(child, FALSE);
				child->disabled = TRUE;
			}
			index++;
			continue;
		}
		row = &h3_scoreboard.rows[pinned >= 0 && index == cap - 1 ? pinned : offset + index];
		/* (the row's widgets set again only when the rows or those shown
		changed: each frame, only the cursor's) */
		if (refill)
		{
			widget_instance_set_visibility_recursive(child, TRUE);
			child->disabled = FALSE;
			h3_scoreboard_fill_row(child, row, split, open, h3_scoreboard.teams);
		}
		if (open && index == cursor)
		{
			focus = child;
			child->animation.current_frame_index = _h3_scoreboard_row_cursor;
		}
		else
			/* (held: the own row's outline is the narrow widget's) */
			child->animation.current_frame_index = !row->team_row && row->local_player_index != NONE &&
				(open || split) ? _h3_scoreboard_row_own : _h3_scoreboard_row_plain;
		index++;
	}
	if (open && focus && focus != list->focused_child)
		ui_widget_port_give_focus(list, focus);
#ifndef HALO_RELEASE
	/* (the open screen's rows shown and the cursor's, said as they change:
	the tests read them) */
	if (open)
	{
		static char said[160];
		char line[160];
		long chosen = h3_scoreboard_chosen_row();

		snprintf(line, sizeof(line), "rows %ld-%ld of %ld shown, the cursor on %s", offset + 1,
			MIN(offset + rows, h3_scoreboard.row_count), h3_scoreboard.row_count,
			chosen >= 0 ? h3_scoreboard.rows[chosen].name : "a team");
		if (strcmp(line, said))
		{
			snprintf(said, sizeof(said), "%s", line);
			platform_log("menus: h3 scoreboard: %s", line);
		}
	}
#endif
	if (!screen)
		return;
	if (!refill)
	{
		/* (between reads, only what was said and the selector, open) */
		if (open)
		{
			h3_scoreboard_message_update(screen);
			h3_scoreboard_selector_update(screen, FALSE);
		}
		return;
	}
	{
		boolean compact = !open && !split;
		long shown_rows = MAX(0, MIN(h3_scoreboard.row_count - offset, cap)), shown_players = 0, more;
		short y = (short)(H3_SCOREBOARD_LIST_TOP + shown_rows * H3_SCOREBOARD_ROW_PITCH + 4);
		short left = H3_SCOREBOARD_LIST_LEFT;
		boolean truncated = compact && h3_scoreboard.row_count > cap;

		for (index = 0; index < shown_rows; index++)
			if (!h3_scoreboard.rows[pinned >= 0 && index == cap - 1 ? pinned : offset + index].team_row)
				shown_players++;
		more = MAX(0, h3_scoreboard.player_count - shown_players);
		text_show(named(screen, "title"), h3_scoreboard.title);
		text_show(named(screen, "rules"), h3_scoreboard.rules);
		text_show(named(screen, "rules_n"), h3_scoreboard.rules);
		if (h3_scoreboard.row_count > rows && (open || split))
		{
			snprintf(text, sizeof(text), T("%ld-%ld of %ld"), offset + 1, MIN(offset + rows - (pinned >= 0 ? 1 : 0),
				h3_scoreboard.row_count), h3_scoreboard.row_count);
			text_show(named(screen, "scroll"), text);
		}
		else
			text_show(named(screen, "scroll"), "");
		/* (the compact panel's captions and column names; the open one's wider; split
		screen's own are always shown) */
		if (!split)
		{
			h3_scoreboard_show(named(screen, "caption"), open);
			h3_scoreboard_show(named(screen, "caption_n"), compact);
			h3_scoreboard_show(named(screen, "title"), open);
			h3_scoreboard_show(named(screen, "rules"), open);
			h3_scoreboard_show(named(screen, "rules_n"), compact);
			h3_scoreboard_show(named(screen, "header_band"), open);
			h3_scoreboard_show(named(screen, "header_band_n"), compact);
			h3_scoreboard_show(named(screen, "header_kills"), open);
			h3_scoreboard_show(named(screen, "header_deaths"), open);
			h3_scoreboard_show(named(screen, "header_assists"), open);
			/* (held: "Hold [Triangle] to activate", or "+N more - Hold [Triangle] to
			open" when the leaders and the player's own row are all it shows, under the
			last row; the words' widths, as the small font sets them: the glyph and the
			rest after the language's words) */
			snprintf(text, sizeof(text), T("+%ld more - Hold"), more);
			h3_scoreboard_place(screen, "hold_a", left, y);
			h3_scoreboard_place(screen, "hold_butn", (short)(left + (short)strlen(T("Hold")) * 7 + 6), y);
			h3_scoreboard_place(screen, "hold_b", (short)(left + (short)strlen(T("Hold")) * 7 + 24), y);
			text_show(named(screen, "more_a"), text);
			h3_scoreboard_place(screen, "more_a", left, y);
			h3_scoreboard_place(screen, "more_b", (short)(left + (short)strlen(text) * 7 + 24), y);
			h3_scoreboard_show(named(screen, "hold_a"), compact && !truncated);
			h3_scoreboard_show(named(screen, "hold_butn"), compact);
			h3_scoreboard_show(named(screen, "hold_b"), compact && !truncated);
			h3_scoreboard_show(named(screen, "more_a"), truncated);
			h3_scoreboard_show(named(screen, "more_b"), truncated);
			if (truncated)
				h3_scoreboard_place(screen, "hold_butn", (short)(left + (short)strlen(text) * 7 + 6), y);
			/* (open: the selector as a small row under the panel, the buttons' line
			under it, the rows shown and what was said under that) */
			h3_scoreboard_place(screen, "selector", left + 69, y);
			h3_scoreboard_place(screen, "selector_emblem", left + 74, y + 1);
			h3_scoreboard_place(screen, "selector_name", left + 92, y);
			h3_scoreboard_place(screen, "selector_arrows", left + 270, y + 1);
			h3_scoreboard_place(screen, "a_butn", left, y + 19);
			h3_scoreboard_place(screen, "a_label", left + 16, y + 18);
			h3_scoreboard_place(screen, "dpad_butn", left + 100, y + 19);
			h3_scoreboard_place(screen, "dpad_label", left + 116, y + 18);
			h3_scoreboard_place(screen, "b_butn", left + 200, y + 19);
			h3_scoreboard_place(screen, "b_label", left + 216, y + 18);
			h3_scoreboard_place(screen, "scroll", left, y + 36);
			h3_scoreboard_place(screen, "message", left + 96, y + 36);
			h3_scoreboard_show(named(screen, "selector"), open);
			h3_scoreboard_show(named(screen, "selector_emblem"), open);
			h3_scoreboard_show(named(screen, "selector_name"), open);
			h3_scoreboard_show(named(screen, "selector_arrows"), open);
			h3_scoreboard_show(named(screen, "a_butn"), open);
			h3_scoreboard_show(named(screen, "a_label"), open);
			h3_scoreboard_show(named(screen, "dpad_butn"), open);
			h3_scoreboard_show(named(screen, "dpad_label"), open);
			h3_scoreboard_show(named(screen, "b_butn"), open);
			h3_scoreboard_show(named(screen, "b_label"), open);
		}
	}
	if (open)
	{
		/* (a screen made again, or the rows read: what was said set again) */
		h3_scoreboard.message_new = TRUE;
		h3_scoreboard_message_update(screen);
		h3_scoreboard_selector_update(screen, TRUE);
	}
	else
		h3_scoreboard_show(named(screen, "message"), FALSE);
}

/* (port h3 scoreboard act: Cross on a row) the player chosen, their options
to open; a team's band opens none */
static boolean h3_scoreboard_act(struct widget_instance *widget)
{
	long index = -1;

	if (widget && !strncmp(widget->name, "row_", 4) && widget->name[4] >= '0' && widget->name[4] <= '9')
	{
		long row = atol(widget->name + 4) - 1;

		if (row >= 0 && h3_scoreboard.view.offset + row < h3_scoreboard.row_count)
			index = h3_scoreboard.view.offset + row;
	}
	if (index < 0)
		index = h3_scoreboard_chosen_row();
	if (index < 0 || h3_scoreboard.rows[index].team_row)
		return FALSE;
	h3_scoreboard_choose(index);
	h3_scoreboard.said_options[0] = 0;
	platform_log("menus: h3 scoreboard: options for %s", h3_scoreboard.rows[index].name);
	return TRUE;
}

/* another machine's person (not a bot, not this machine's own) */
static boolean h3_scoreboard_other_person(struct h3_scoreboard_row const *row)
{
	return h3_scoreboard.network && !row->team_row && !row->bot && row->local_player_index == NONE;
}

/* (port h3 scoreboard options) the box's title, words and rows: MUTE CHAT
and ADD FRIEND for another machine's person, KICK for the game's host */
static void h3_scoreboard_options_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list), *child;
	long index = h3_scoreboard_chosen_row();
	struct h3_scoreboard_row const *row;
	boolean other, kick;
	int state = P2P_FRIENDS_PLAYER_NONE;
	char text[160], said[160];

	if (index < 0)
		return;
	row = &h3_scoreboard.rows[index];
	other = h3_scoreboard_other_person(row);
	/* (KICK: the host's alone, never a joined machine's - a dedicated
	server's players kick no one; its admin does) */
	kick = other && h3_scoreboard.hosting;
	if (other)
		state = p2p_friends_player_state(row->machine);
	text_show(named(screen, "options_title"), row->name);
	if (row->local_player_index != NONE)
		snprintf(text, sizeof(text), "%s", T("You, on this PS Vita"));
	else if (row->bot)
		snprintf(text, sizeof(text), "%s", T("A bot: it plays on the host's machine"));
	else if (!h3_scoreboard.network)
		snprintf(text, sizeof(text), "%s", T("A player of this game"));
	else
		snprintf(text, sizeof(text), "%s", h3_players_state_text(state)[0] ? h3_players_state_text(state) :
			T("Add friend sends a friend request: they accept or decline."));
	text_show(named(screen, "options_text"), text);
	for (child = list->child; child; child = child->next)
	{
		boolean shown = TRUE;

		if (strstr(child->name, "option_mute"))
		{
			shown = other;
			if (shown)
				text_show(child, game_chat_name_muted(row->name) ? T("UNMUTE CHAT") : T("MUTE CHAT"));
		}
		else if (strstr(child->name, "option_friend"))
		{
			shown = other;
			if (shown)
				text_show(child, state == P2P_FRIENDS_PLAYER_FRIEND ? T("VIEW FRIEND") : T("ADD FRIEND"));
		}
		else if (strstr(child->name, "option_kick"))
			shown = kick;
		widget_instance_set_visibility_recursive(child, shown);
		child->disabled = !shown;
		child->animation.current_frame_index = child == list->focused_child ? _h3_scoreboard_row_cursor :
			_h3_scoreboard_row_plain;
	}
	snprintf(said, sizeof(said), "%s: back%s%s%s", row->name, other ? ", mute" : "", other ?
		(state == P2P_FRIENDS_PLAYER_FRIEND ? ", view friend" : ", add friend") : "", kick ? ", kick" : "");
	if (strcmp(said, h3_scoreboard.said_options))
	{
		snprintf(h3_scoreboard.said_options, sizeof(h3_scoreboard.said_options), "%s", said);
		platform_log("menus: h3 scoreboard: options %s", said);
	}
}

/* KICK, as the options' row does: the game's host alone (its
network_anticheat_drop, which every machine is told of; a joined machine is
refused here and there) */
static void h3_scoreboard_kick(struct h3_scoreboard_row const *row)
{
	char text[160];

	if (!h3_scoreboard.network || !global_network_game_server_get() || network_game_is_splitscreen_local())
		snprintf(text, sizeof(text), "%s", T("Only the game's host can kick a player"));
	else if (!h3_scoreboard_other_person(row) || !network_anticheat_drop(row->machine, _anticheat_drop_kick, NULL))
		snprintf(text, sizeof(text), "%s", T("That player is not in the game"));
	else
		snprintf(text, sizeof(text), T("Kicked %s"), row->name);
	h3_scoreboard_say(text);
}

/* (port h3 scoreboard option: a row of the options) */
static void h3_scoreboard_option(struct widget_instance *widget)
{
	long index = h3_scoreboard_chosen_row();
	struct h3_scoreboard_row const *row;
	char text[256];

	if (!widget || index < 0)
		return;
	row = &h3_scoreboard.rows[index];
	if (strstr(widget->name, "option_mute"))
	{
		if (!h3_scoreboard_other_person(row))
			return;
		snprintf(text, sizeof(text), game_chat_mute_name(row->name, !game_chat_name_muted(row->name)) ?
			T("%s muted: their chat is hidden here") : T("%s unmuted"), row->name);
		h3_scoreboard_say(text);
	}
	else if (strstr(widget->name, "option_friend"))
	{
		int state, outcome;

		if (!h3_scoreboard_other_person(row))
			return;
		state = p2p_friends_player_state(row->machine);
		if (state == P2P_FRIENDS_PLAYER_FRIEND)
		{
			/* (VIEW FRIEND: what the friends list says of them) */
			static struct p2p_friend_entry entries[P2P_FRIEND_ENTRIES_MAXIMUM];
			char id[P2P_FRIEND_ID_SIZE];
			int count, entry;

			snprintf(text, sizeof(text), T("%s is your friend"), row->name);
			if (p2p_friends_player_friend_id(row->machine, id))
			{
				count = p2p_friends_list(entries, (int)NUMBEROF(entries));
				for (entry = 0; entry < count; entry++)
					if (!strcmp(entries[entry].id, id) && entries[entry].activity[0])
						snprintf(text, sizeof(text), "%s: %s", entries[entry].name, entries[entry].activity);
			}
		}
		else
		{
			outcome = p2p_friends_player_ask(row->machine, P2P_FRIENDS_PLAYER_ADD);
			if (outcome == P2P_FRIENDS_DONE)
				snprintf(text, sizeof(text), T("Asking %s to be friends..."), row->name);
			else
				snprintf(text, sizeof(text), "%s", p2p_friends_outcome_text(outcome));
		}
		h3_scoreboard_say(text);
	}
	else if (strstr(widget->name, "option_kick"))
		h3_scoreboard_kick(row);
}

/* the open screen opened for the local player (Triangle with the scoreboard
held): 1 if it opened */
static boolean h3_scoreboard_open(short local_player_index, long tag)
{
	ui_widget_port_held_screen_hide(local_player_index);
	h3_scoreboard.held[local_player_index] = FALSE;
	h3_scoreboard.chosen_player = NONE;
	h3_scoreboard.chosen_name[0] = 0;
	h3_scoreboard.message[0] = 0;
	h3_scoreboard.page = 0;
	browser_view_reset(&h3_scoreboard.view);
	h3_scoreboard_forget_views(local_player_index);
	if (!ui_widget_load_by_name_or_tag(NULL, tag, NULL, local_player_index, NONE, NONE, NONE))
	{
		platform_log("menus: h3 scoreboard: the screen did not open");
		return FALSE;
	}
	h3_scoreboard.open = TRUE;
	h3_scoreboard.open_local_player = local_player_index;
	h3_scoreboard.open_back_released = FALSE;
	platform_log("menus: h3 scoreboard: opened (local player %d)", local_player_index);
	return TRUE;
}

/* (each frame, pc_menu_functions_update) each local player's scoreboard
held up while their Back is (and for every player as the game ends, as the
game's own shows), gone when it is let go or a screen of theirs opens;
Triangle while it is held opens it; Back pressed again closes the open
one */
static void h3_scoreboard_frame(boolean main_menu_loaded)
{
#ifndef HALO_DEDICATED_SERVER
	boolean in_round = !main_menu_loaded && game_engine_running() && !game_engine_force_single_screen();
	boolean wanted = in_round && h3_scoreboard_wanted() && !h3_scoreboard.unavailable;
	short count = in_round ? local_player_count() : 0;
	long tag = NONE, open_tag = NONE;
	short index;

	if (!in_round)
	{
		/* (the game over with the scoreboard open: closed, for the carnage
		report and the game's own scores) */
		if (h3_scoreboard.open && !main_menu_loaded)
		{
			struct widget_instance *active = ui_widget_port_active_screen(h3_scoreboard.open_local_player);
			long screen_tag = menu_tags_ingame_screen(H3_SCOREBOARD_SCREEN);
			long options_tag = menu_tags_ingame_screen("h3/scoreboard/options");

			if (active && active->definition_tag_index != NONE && (active->definition_tag_index == screen_tag ||
				active->definition_tag_index == options_tag))
			{
				platform_log("menus: h3 scoreboard: the game is over: closed");
				ui_widgets_close_all_for_local_player(h3_scoreboard.open_local_player);
			}
		}
		if (h3_scoreboard.warmed || h3_scoreboard.unavailable || h3_scoreboard.open)
		{
			for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
				ui_widget_port_held_screen_hide(index);
			memset(h3_scoreboard.views, 0, sizeof(h3_scoreboard.views));
			memset(h3_scoreboard.held, 0, sizeof(h3_scoreboard.held));
			h3_scoreboard.warmed = FALSE;
			h3_scoreboard.unavailable = FALSE;
			h3_scoreboard.open = FALSE;
			h3_scoreboard.read_once = FALSE;
			h3_scoreboard.said_rows[0] = 0;
		}
		return;
	}
	if (wanted)
	{
		open_tag = menu_tags_ingame_screen(H3_SCOREBOARD_SCREEN);
		tag = h3_scoreboard_split() ? menu_tags_ingame_screen(H3_SCOREBOARD_SPLIT_SCREEN) : open_tag;
		if (tag == NONE)
		{
			h3_scoreboard.unavailable = TRUE;
			platform_log("menus: h3 scoreboard: not in this map's menus: the game's own scores");
		}
		else if (!h3_scoreboard.warmed)
		{
			/* (its pictures read with the game's start, not at the first hold) */
			h3_scoreboard.warmed = TRUE;
			platform_log("menus: h3 scoreboard: ready (%s)", h3_scoreboard_split() ? "split screen's" : "full screen");
		}
	}
	/* (the open screen: gone when it is not the player's screen any more;
	Back let go, then pressed again, closes it) */
	if (h3_scoreboard.open)
	{
		short player = h3_scoreboard.open_local_player;
		struct widget_instance *active = ui_widget_port_active_screen(player);
		struct gamepad_state const *pad = input_has_gamepad(player) ? input_get_gamepad_state(player) : NULL;
		boolean back = pad && pad->buttons[_gamepad_binary_button_back];

		if (!active)
		{
			h3_scoreboard.open = FALSE;
			h3_scoreboard.touching = h3_scoreboard.dragging = FALSE;
			ui_widget_port_pointer_drag(FALSE);
			platform_log("menus: h3 scoreboard: closed");
		}
		else if (!back)
			h3_scoreboard.open_back_released = TRUE;
		else if (h3_scoreboard.open_back_released && open_tag != NONE && active->definition_tag_index == open_tag)
		{
			platform_log("menus: h3 scoreboard: Back again: closed");
			ui_widget_port_go_back(player);
		}
	}
	for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
	{
		struct gamepad_state const *pad = index < count && input_has_gamepad(index) ? input_get_gamepad_state(index) : NULL;
		boolean back = pad && pad->buttons[_gamepad_binary_button_back];
		boolean y = pad && pad->buttons[_gamepad_analog_button_y];
		boolean y_pressed = y && !h3_scoreboard.y_down[index];
		boolean show;

		h3_scoreboard.y_down[index] = y;
		h3_scoreboard.back_down[index] = back;
		show = wanted && tag != NONE && index < count && (back || !game_engine_in_play()) &&
			!ui_widget_port_active_screen(index);
		/* (Triangle while it is held: open, one player on this machine) */
		if (show && back && y_pressed && count == 1 && !h3_scoreboard_split() && open_tag != NONE &&
			h3_scoreboard_open(index, open_tag))
		{
			continue;
		}
		if (show)
		{
			boolean split = h3_scoreboard_split();
			/* (split screen's: in the middle of a view as wide as the screen -
			two players', the first of three -, at the left of a quarter) */
			short x = split ? (count <= 2 || (count == 3 && index == 0)) ? 160 : 0 : H3_SCOREBOARD_HELD_SHIFT;
			short y_offset = split && count == 1 ? 120 : 0;

			if (!h3_scoreboard.held[index])
				h3_scoreboard_forget_views(index);
			if (!ui_widget_port_held_screen_show(index, tag, x, y_offset))
			{
				h3_scoreboard.unavailable = TRUE;
				platform_log("menus: h3 scoreboard: the screen could not be held up: the game's own scores");
				show = FALSE;
			}
			else if (!h3_scoreboard.held[index])
			{
				h3_scoreboard_read(TRUE);
				platform_log("menus: h3 scoreboard: held up (local player %d, %s)", index,
					split ? "split screen's" : "full screen");
			}
		}
		if (!show && h3_scoreboard.held[index])
			ui_widget_port_held_screen_hide(index);
		h3_scoreboard.held[index] = show;
	}
#ifndef HALO_RELEASE
	/* (tests) HALO_TEST_SCOREBOARD_KICK=1: with the scoreboard held, the
	first other machine's person kicked once, as their options' KICK does:
	a joined machine must be refused */
	{
		static boolean kicked;
		char const *kick = getenv("HALO_TEST_SCOREBOARD_KICK");

		if (!kicked && kick && kick[0] == '1' && h3_scoreboard.held[0])
		{
			long row;

			for (row = 0; row < h3_scoreboard.row_count; row++)
			{
				if (h3_scoreboard_other_person(&h3_scoreboard.rows[row]))
				{
					kicked = TRUE;
					platform_log("menus: h3 scoreboard: test: KICK %s (%s)", h3_scoreboard.rows[row].name,
						h3_scoreboard.hosting ? "the host" : "joined");
					h3_scoreboard_kick(&h3_scoreboard.rows[row]);
					break;
				}
			}
		}
	}
#endif
#else
	(void)main_menu_loaded;
#endif
}

/* (game_engine.c) whether the Halo 3 scoreboard is up for the local player,
held or open: the game's own scores are not drawn */
boolean menu_functions_scoreboard_replaces(short local_player_index)
{
	struct widget_instance *active;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return FALSE;
	if (ui_widget_port_held_screen(local_player_index))
		return TRUE;
	active = h3_scoreboard.open && h3_scoreboard.open_local_player == local_player_index ?
		ui_widget_port_active_screen(local_player_index) : NULL;
	return active != NULL;
}

/* (xinput_sdl.c, the tests' scripted player) whether the open scoreboard,
or its options, is up */
boolean menu_functions_scoreboard_is_open(void)
{
	return h3_scoreboard.open;
}

/* (menu_functions_players_button) whether Triangle with Back held is the
scoreboard's (it opens it) rather than PLAYERS' */
static boolean h3_scoreboard_takes_triangle(void)
{
	return h3_scoreboard_wanted() && !h3_scoreboard.unavailable;
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
		/* (Menu style Halo 3, the lobby's START GAME on OFFLINE: the local
		game started with the lobby's GAME and MAP, no map or game type list
		after this screen, as the network games are hosted; split screen's
		map list as before when it cannot be: h3_mp_local_start) */
		if (h3_join.split && halo_menu_style_halo3() && h3_mp_local_start())
			return FALSE;
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
		snprintf(text, sizeof(text), "%s", h3_local.since || h3_local.started ? T("Starting the game...") :
			h3_join.split && h3_local.asked ? h3_buttons(T("Ready: Cross to start the game.")) :
			h3_join.split ? h3_buttons(T("Ready: Cross for the map.")) : h3_buttons(T("Ready: Cross for the games.")));
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

	/* (Menu style Online: SELECT PROFILE only in CUSTOMIZATION - none chosen
	this run, the current one, the first controller's) */
	if (h3_session.profile == NONE && halo_menu_style_online())
	{
		long current = h3_profile_current(0);

		if (current != NONE)
			h3_session_profile_chosen(0, current);
		plate = h3_session.plate;
	}
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
	/* (the lobby's map variant played in the game hosted: its joiners asked
	to play it, as a network Forge game's are; one that cannot be played is
	said and the map played as it is) */
	h3_mp_variant_play(TRUE, map);
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
	{
		char rules[512];

		h3_gametype_rules_log(&variant, rules, sizeof(rules));
		platform_log("menus: h3 mp: the game's rules: %s", rules);
	}
	return TRUE;
}

/* (Menu style Halo 3: split screen's SELECT PROFILE gone on, opened by the
lobby's START GAME on OFFLINE) the local game set up as split screen's map
list and game type list set it up (split_screen_game_initialize,
multiplayer_level_select, multiplayer_profile_set_for_game), with the
lobby's MAP and GAME - a saved game type of GAME TYPES' too, Infection's;
the lobby's map variant was asked for by START GAME (h3_mp_variant_play) -
and started once its players are in (h3_mp_local_update): TRUE; FALSE (no
map or game type, the map not playable, the game not made), split screen's
map list and game type list as before */
static boolean h3_mp_local_start(void)
{
	struct game_variant variant;
	char const *map = h3_mp.map_index != NONE ? h3_map_paths[h3_mp.map_index] : h3_mp.map_other;
	long variant_index = h3_mp.gametype != NONE ? h3_mp.gametypes[h3_mp.gametype] : NONE;
	struct network_game_server *server;
	char directory[256], why[160];

	if (!h3_local.asked)
		return FALSE;
	/* (pressed again while it starts) */
	if (h3_local.since || h3_local.started)
		return TRUE;
	if (!map || !map[0] || variant_index == NONE || !(variant_index & 0x80000000))
	{
		platform_log("menus: h3 mp: no map or game type chosen: split screen's map list");
		return FALSE;
	}
	if (map_check(map, why, sizeof(why)) != _map_check_playable)
	{
		platform_log("menus: h3 mp: %s can't be played: %s: split screen's map list", map, why);
		return FALSE;
	}
	if (!playlist_profile_get(variant_index, &variant))
	{
		platform_log("menus: h3 mp: the game type could not be read: split screen's map list");
		return FALSE;
	}
	/* (a new local game, as split screen's map list makes it: its server,
	taking no one from the network, and this machine's client of it) */
	dispose_global_network_game_client();
	dispose_global_network_game_server();
	network_game_accept_remote_connections(FALSE);
	game_engine_playlist_initialize();
	if (!create_global_network_game_server())
	{
		dispose_global_network_game_server();
		platform_log("menus: h3 mp: the local game could not be made: split screen's map list");
		return FALSE;
	}
	game_engine_playlist_begin();
	game_connection_set(_game_connection_network_server);
	if (!create_global_network_game_client() || (server = global_network_game_server_get()) == NULL)
	{
		dispose_global_network_game_client();
		dispose_global_network_game_server();
		platform_log("menus: h3 mp: the local game could not be made: split screen's map list");
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
	h3_local.since = system_milliseconds() ? system_milliseconds() : 1;
	h3_local.players_in = 0;
	h3_local.added = 0;
	platform_log("menus: h3 mp: the local game: %s on %s (the lobby's GAME and MAP), no map or game type list; "
		"started once its players are in", h3_mp_gametype_name(), h3_mp_map_name());
	{
		char rules[512];

		h3_gametype_rules_log(&variant, rules, sizeof(rules));
		platform_log("menus: h3 mp: the game's rules: %s", rules);
	}
	return TRUE;
}

/* (the local game not started) split screen's own lobby, over the screen
up, which B goes back to: its A starts the game as before */
static void h3_mp_local_lobby(void)
{
	struct widget_instance *active = NULL;
	long index;

	for (index = 0; index < 4 && !active; index++)
		active = ui_widget_port_active_screen((short)index);
	if (!active || !ui_widget_load_by_name_or_tag(
		"ui\\shell\\main_menu\\multiplayer_type_select\\split_screen\\pregame\\splitscreen_pregame_wrapper_normal",
		NONE, NULL, active->local_player_index, active->definition_tag_index, NONE, NONE))
	{
		platform_log("menus: h3 mp: split screen's lobby did not open");
	}
}

/* (each frame, the main loop) the local game h3_mp_local_start set up: the
players SELECT PROFILE joined added to it (as split screen's lobby, "net
splitscreen prejoin players", adds them), the settings' offline bots let in
(the server adds them in its lobby: network_server_manager.c), and the game
started at once (as the lobby's A with no countdown); not started in 15 s,
split screen's own lobby, whose A starts it */
static void h3_mp_local_update(boolean main_menu_loaded)
{
	struct network_game_client *client = global_network_game_client_get();
	struct network_game *game = NULL;
	unsigned long now = system_milliseconds();
	short state = NONE, local_player;
	long wanted = 0, in = 0, bots, room;

	/* (the level loading: the next START GAME asks again) */
	if (!main_menu_loaded)
	{
		h3_local.asked = FALSE;
		h3_local.started = FALSE;
	}
	/* (a local game over, back in the Halo 3 lobby: the game let go - the
	next START GAME makes a new one, h3_mp_local_start - unless the lobby's
	START GAME has made one already) */
	if (h3_local.dispose_pending && main_menu_loaded)
	{
		h3_local.dispose_pending = FALSE;
		if (!h3_local.since && !h3_local.started)
		{
			dispose_global_network_game_client();
			dispose_global_network_game_server();
			player_ui_clear_multiplayer_variant();
			platform_log("menus: h3 mp: the local game over: let go");
		}
	}
	/* (asked to start, but the game not begun in 5 s - one player and no
	bots, where a local game needs two: the desktop builds' - split screen's
	own lobby, which says what it waits for) */
	if (h3_local.started && now - h3_local.started_time > 5000)
	{
		h3_local.started = FALSE;
		platform_log("menus: h3 mp: the local game did not start in 5 s: split screen's lobby");
		h3_mp_local_lobby();
	}
	if (!h3_local.since)
		return;
	if (!main_menu_loaded || !client || !global_network_game_server_get())
	{
		platform_log("menus: h3 mp: the local game: %s", main_menu_loaded ? "its game was closed" : "the level loads");
		h3_local.since = 0;
		return;
	}
	if (network_game_client_get_state(client, &state) == 2)
		game = network_game_get_game();
	for (local_player = 0; game && local_player < 4; local_player++)
	{
		long index;
		boolean player_in = FALSE;

		if (!player_ui_local_player_wants_to_play_multiplayer(local_player))
			continue;
		wanted++;
		for (index = 0; index < (long)NUMBEROF(game->players) && !player_in; index++)
		{
			player_in = network_player_is_valid(&game->players[index]) &&
				network_game_player_is_local(&game->players[index]) &&
				game->players[index].controller_index == local_player;
		}
		if (player_in)
			in++;
		else if (now - h3_local.added >= 250 && !network_game_client_add_player(client, local_player))
			platform_log("menus: h3 mp: the local game: player %d could not be added", local_player + 1);
	}
	if (game && wanted && in < wanted && now - h3_local.added >= 250)
		h3_local.added = now ? now : 1;
	if (!game || !wanted || in < wanted)
	{
		if (now - h3_local.since > 15000)
		{
			platform_log("menus: h3 mp: the local game: its players not in after 15 s: split screen's lobby");
			h3_local.since = 0;
			h3_mp_local_lobby();
		}
		return;
	}
	if (!h3_local.players_in)
		h3_local.players_in = now ? now : 1;
	/* (the bots the settings ask for, as far as the game has room: a moment
	for the server to add them) */
	bots = bots_wanted_count();
	room = (long)game->maximum_players - in;
	if (bots > room)
		bots = room > 0 ? room : 0;
	if (game->player_count < in + bots && now - h3_local.players_in < 3000)
		return;
	platform_log("menus: h3 mp: the local game starts: %s on %s, %ld player%s, %ld bot%s", h3_mp_gametype_name(),
		h3_mp_map_name(), in, in == 1 ? "" : "s", (long)game->player_count - in, game->player_count - in == 1 ? "" : "s");
	h3_local.since = 0;
	h3_local.started = TRUE;
	h3_local.started_time = now;
	network_game_client_request_immediate_start();
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
	boolean can;

	/* (the multiplayer or Forge lobby on ONLINE, hosted on demand: its game
	now, the friends invited into it) */
	if (h3_mp.network == _h3_network_online || h3_forge.network == _h3_network_online)
		h3_live_open_now("INVITE FRIENDS");
	can = network_game_server_port_in_lobby(global_network_game_server_get()) && internet_play() &&
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
		if (!_stricmp(map_name, h3_level_paths[index]) || !_stricmp(map_name, h3_mcc_level_paths[index]))
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

ONLINE hosts on demand (LadyCassie, insider.20: the lobby went live by itself
a moment after MULTIPLAYER opened, its game hosted for nobody; the owner
agreed): the lobby opened there is this machine's alone (Ready) until the
player asks for its game - START GAME (START FORGE), L's INVITE FRIENDS,
GAME OPTIONS' START GAME (h3_live_open_now) - or someone could join it from
their own menus: a friend online (their one-press join needs a game to
join: the presence carries its sealed token, p2p_friends.c own_joinable) or
the party (its members follow the leader's game, p2p_party.inc). Then it
goes live as it did. SYSTEM LINK and AD HOC go live as the lobby is up, as
before: the Vitas around find a game there only by its broadcast, none
asks first.

Going live keeps the lobby as it looks: the scene stands (the main menu's
ticks stop for a network game's lobby, menu_scene_objects_settled; it is
not made again), the nameplate and the party's plates stay while the
game's players are all this machine's - the players' rows come once
another machine's player is in (h3_live_lobby_show) -, and the game's
server starts on the lobby's own map (ui_widget_port_start_network_game
_server_on_map): no copy of the playlist's first map begun and dropped
on the way, which read the memory card on the game thread and took its
buffer from the texture cache (LadyCassie's hitch: 404 ms).

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
	/* (ONLINE: hosted on demand) the player asked for this lobby's game -
	START GAME or START FORGE, INVITE FRIENDS, GAME OPTIONS' START GAME -:
	kept while the lobby is (hosted again as its wire changes), forgotten
	as it closes */
	boolean wanted;
	/* (ONLINE) someone could join the lobby from their own menus - a friend
	online, the party -, as looked at last (each second) */
	boolean others_could_join;
	unsigned long others_checked_time;
	/* (ONLINE) the lobby's waiting for the player said (the tests read it) */
	boolean said_local;
} h3_live = { FALSE, NONE, FALSE, FALSE, FALSE, 0, 0, "", FALSE, "", FALSE, FALSE, FALSE, FALSE, 0, FALSE };

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
	h3_party_online("the Forge lobby");
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
		/* (a network's: "1 Player (8 max)", network.max_players as a network
		Forge game takes it - 8 at most -, HOST SETTINGS' MAX PLAYERS) */
		int maximum = (int)atol(setting_get("HALO_NET_MAX_PLAYERS", "16"));

		h3_party_players_text(text, (int)sizeof(text), local ? 0 : MIN(maximum < 2 ? 16 : maximum, 8));
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
	h3_live.wanted = FALSE;
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
		/* (the same lobby, hosted again: public or private as chosen, and
		online as the player asked for it) */
		boolean chosen = h3_live.public_chosen, wanted = h3_live.wanted;

		h3_live.public_chosen = TRUE;
		h3_live_close(with_pc ? "WHO CAN JOIN is Vitas and PC now: hosted again on the crossplay wire" :
			"WHO CAN JOIN is Vitas now: hosted again on the Vitas' wire");
		h3_live.public_chosen = chosen;
		h3_live.wanted = wanted;
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
	/* (on the lobby's map from the start: the playlist's first map never
	copied meanwhile) */
	if (!ui_widget_port_start_network_game_server_on_map(h3_live.forge ? forge_map_path(h3_forge.map) :
		h3_mp.map_index != NONE ? h3_map_paths[h3_mp.map_index] : h3_mp.map_other))
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

/* (ONLINE, the lobby not live) whether someone could join its game from
their own menus, so that it is hosted before the player asks: a friend
online (the one-press join from their FRIENDS needs a game: the presence's
sealed token is its server's) or this machine's party (its members follow
the game of whoever leads; this machine's while it leads). Looked at once a
second */
static boolean h3_live_others_could_join(void)
{
	unsigned long now = system_milliseconds();

	if (!h3_live.others_checked_time || now - h3_live.others_checked_time >= 1000)
	{
		struct p2p_party_member party[P2P_PARTY_MAXIMUM];
		int online = 0;
		boolean could;

		h3_live.others_checked_time = now ? now : 1;
		p2p_friends_count(&online);
		could = online > 0 || p2p_party_members(party, P2P_PARTY_MAXIMUM) > 1;
		if (could != h3_live.others_could_join)
		{
			h3_live.others_could_join = could;
			if (could)
				platform_log("menus: h3 live lobby: %s: its game hosted for them", online > 0 ?
					"a friend is online" : "in a party");
		}
	}
	return h3_live.others_could_join;
}

/* (ONLINE, the player's own in the lobby, or its GAME OPTIONS: START GAME
or START FORGE, L's INVITE FRIENDS, GAME OPTIONS' START GAME) the lobby's
game hosted now, if it is not yet and can be - the network ready, nothing
hosted or joined already -, and wanted from now while the lobby is (its
wire's change hosts it again): TRUE if the lobby is live */
static boolean h3_live_open_now(char const *why)
{
	if (h3_live.live)
		return TRUE;
	if (h3_live.refused || H3_LIVE_NETWORK == _h3_network_offline || !halo_menu_style_halo3() ||
		!halo_main_menu_loaded() || global_network_game_server_get() || global_network_game_client_get() ||
		!h3_live_network_ready())
	{
		return FALSE;
	}
	platform_log("menus: h3 live lobby: %s: its game hosted now", why);
	h3_live_open();
	if (h3_live.live)
		h3_live.wanted = TRUE;
	return h3_live.live;
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
	short local_machine = client ? network_game_client_get_local_machine_index() : NONE;
	struct widget_instance *found;
	char text[192], line[256];
	long count, index;
	boolean with_pc, others = FALSE, invite;

	/* (another machine's player in the game: its players in the rows;
	until then the lobby as it was before it went live - the nameplate in
	the profile's colour, the party's plates, "1 Player (16 max)" -: going
	live changes nothing the player looks at but the words under Ready) */
	for (index = 0; game && index < (long)NUMBEROF(game->players) && !others; index++)
	{
		others = network_player_is_valid(&game->players[index]) &&
			game->players[index].machine_index != local_machine;
	}
	if ((found = named(screen, "nameplate")) != NULL)
		widget_instance_set_visibility_recursive(found, !others);
	/* (L: INVITE FRIENDS, online: the lobby's game hosted for it first if
	it is not yet, h3_invite_ask) */
	invite = game ? h3_live.network == _h3_network_online :
		H3_LIVE_NETWORK == _h3_network_online && halo_menu_style_halo3() && h3_live_network_ready();
	if ((found = named(screen, "l_butn")) != NULL)
		widget_instance_set_visibility_recursive(found, invite);
	if ((found = named(screen, "invite")) != NULL)
		widget_instance_set_visibility_recursive(found, invite);
	line[0] = 0;
	count = h3_roster_show(screen, game, TRUE, local_machine, others ? H3_LIVE_ROWS : 0, line, (int)sizeof(line));
	if (!game)
		return;
	if (others)
		snprintf(text, sizeof(text), T("PLAYERS %ld/%d"), count, (int)game->maximum_players);
	else
		h3_party_players_text(text, (int)sizeof(text), (int)game->maximum_players);
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
		h3_live.said_local = FALSE;
		/* (back at the main menu with no live game: the next lobby is a new
		one, private by default again, hosted on demand again) */
		if (active && main_tag != NONE && active->definition_tag_index == main_tag && !server)
		{
			if (h3_live.public_chosen)
				h3_live_forget_public();
			h3_live.wanted = FALSE;
		}
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
	/* (ONLINE: when the player asks for it, or someone could join it) */
	if (H3_LIVE_NETWORK == _h3_network_online && !h3_live.wanted && !h3_live_others_could_join())
	{
		if (!h3_live.said_local)
		{
			h3_live.said_local = TRUE;
			platform_log("menus: h3 live lobby: ready on ONLINE, not hosted: START GAME, INVITE FRIENDS or GAME OPTIONS "
				"hosts it (or a friend online, the party)");
		}
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

/* ---- a party plays online (the owner, Oct 21 2026: a party only works
online, so while this machine is in one the Halo 3 lobbies' NETWORK is
ONLINE, for its leader and every member: joining or making a party puts the
multiplayer and Forge lobbies on ONLINE (and the connection, internet play
started if it was not); OFFLINE (LOCAL), SYSTEM LINK and AD HOC chosen
there say why and change nothing; the settings panel's Connection the same,
vita_settings.c). The campaign lobby's LOCAL stays (a game of this machine's
alone); its SYSTEM LINK and AD HOC CO-OP are refused. */

/* whether this machine is in a party ((test builds) HALO_TEST_IN_PARTY: as
in one, run_menus_test.sh h3partyonline) */
static boolean h3_in_party(void)
{
#ifndef HALO_RELEASE
	if (getenv("HALO_TEST_IN_PARTY"))
		return TRUE;
#endif
	return p2p_party_in_party() != 0;
}

/* (in a party) the lobbies on ONLINE: TRUE if anything changed (said once,
why) */
static boolean h3_party_online(char const *why)
{
	boolean changed = FALSE;

	if (!h3_in_party())
		return FALSE;
	if (h3_mp.network != _h3_network_online)
	{
		h3_mp.network = _h3_network_online;
		changed = TRUE;
	}
	if (h3_forge.network_set && h3_forge.network != _h3_network_online)
	{
		h3_forge.network = _h3_network_online;
		changed = TRUE;
	}
	if (h3_campaign.network == _h3_campaign_system_link || h3_campaign.network == _h3_campaign_adhoc)
	{
		h3_campaign.network = _h3_campaign_online;
		changed = TRUE;
	}
	if (network_connection_wanted() != NETWORK_CONNECTION_ONLINE)
	{
		network_connection_choose(NETWORK_CONNECTION_ONLINE);
		changed = TRUE;
	}
	if (!changed)
		return FALSE;
	/* (a live lobby on another network: closed, open again on ONLINE) */
	if (h3_live.live && h3_live.network != _h3_network_online)
		h3_live_close("the party plays online");
	platform_log("menus: in a party (%s): the lobbies' NETWORK is ONLINE (a party plays online)", why);
	return TRUE;
}

/* (in a party) another network than ONLINE chosen: said why, nothing
changed (TRUE) */
static boolean h3_party_refuses(boolean online)
{
	if (online || !h3_in_party())
		return FALSE;
	platform_log("menus: in a party: another NETWORK than ONLINE refused (a party plays online)");
	platform_show_message(T("PARTY"), T("You're in a party, and a party plays online: the NETWORK stays ONLINE. To "
		"play on another network, leave the party first (PARTY, or the settings panel's Party page)."));
	return TRUE;
}

/* (friends_game.c, each frame at the menus) joined or made a party: the
lobbies on ONLINE */
void menu_functions_party_online(void)
{
	h3_party_online("joined or made");
}

/* (display_error_when_main_menu_loaded, the host's game closed) TRUE: the
host was the party's leader, this machine a member - nothing said, the
party follows the leader (p2p_party_host_left_quiet) */
boolean pc_menu_functions_host_left_quiet(void)
{
	if (!p2p_party_host_left_quiet())
		return FALSE;
	platform_log("menus: the host's game closed: the party's leader closed it; no error (the party follows the leader)");
	return TRUE;
}

/* (network_game_reset_to_pregame_ui) a local game over - OFFLINE's START
GAME's, or split screen's - at Menu style Halo 3: back to the Halo 3
multiplayer lobby, OFFLINE's START GAME focused, the local game let go (the
next START GAME makes a new one with the lobby's GAME and MAP,
h3_mp_local_start); not split screen's map list, the Xbox's postgame screen
(testers, insider.21): TRUE; FALSE (Menu style Halo CE, or no Halo 3 set):
the Xbox's */
boolean pc_menu_functions_local_lobby_after_game(void)
{
	long lobby_tag = menu_tags_ingame_screen("h3/mp/screen");
	long main_tag = menu_tags_ingame_screen("h3/main/screen");
	struct widget_instance *screen;

	if (!halo_menu_style_halo3() || lobby_tag == NONE || main_tag == NONE)
		return FALSE;
	/* (let go at the next frame, h3_mp_local_update: this is the client's
	own switch to its pregame, which goes on using it) */
	h3_local.dispose_pending = TRUE;
	h3_local.asked = FALSE;
	h3_local.since = 0;
	h3_local.started = FALSE;
	h3_mp.network = _h3_network_offline;
	if ((screen = ui_widget_load_by_name_or_tag(NULL, lobby_tag, NULL, 0, main_tag, NONE, NONE)) == NULL)
	{
		platform_log("menus: h3 mp: the local game over: the Halo 3 lobby did not open, split screen's map list");
		return FALSE;
	}
	{
		struct widget_instance *list = named(screen, "list");
		struct widget_instance *start = named(screen, "start_offline");

		if (list && start)
			ui_widget_port_give_focus(list, start);
	}
	platform_log("menus: h3 mp: the local game over, back to the Halo 3 lobby (OFFLINE)");
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
	boolean cooperative, host = server != NULL, online, waiting, listed = FALSE;
	/* (an int: p2p_lobby_hosting_listed writes one - through a boolean's address it wrote four bytes
	over the locals beside it on every frame of an online host's lobby) */
	int password = 0;
	short local_machine = client ? network_game_client_get_local_machine_index() : NONE;
	short seconds = client ? network_game_client_get_seconds_to_game_start(client) : -1;
	int reach_state = P2P_LOBBY_BROWSE_OFF, hosting_state = P2P_LOBBY_HOSTING_NONE;
	char text[192], code[32], line[384];
	wchar_t wide[64];
	long index, row = 0;
	short build_percent = 0;

	if (!game)
		return;
	cooperative = coop_menu_game_is_cooperative(game);
	online = config_boolean("network.online") && p2p_running();
	/* (the host's bots - Bots online - are players of machines of their own,
	not in the count: a host with them is not alone) */
	waiting = host && game->machine_count < 2 && network_game_server_dedicated_bot_count() == 0;
	code[0] = 0;
	if (host && online)
		p2p_hosting_code(code, (int)sizeof(code));
	if (host && online)
	{
		char reach[160];

		listed = p2p_lobby_hosting_listed(&password) != 0;
		/* (the host's, online: its code and PUBLIC/PRIVATE only once the
		brokers have it - its code is looked up there, its listing held
		there -: Connecting... before, why after none answered; insider.21
		showed both while still connecting) */
		reach_state = p2p_lobby_brokers_status(reach, (int)sizeof(reach));
		hosting_state = p2p_lobby_hosting_status(reach, (int)sizeof(reach));
		if (reach_state != P2P_LOBBY_BROWSE_EMPTY || hosting_state == P2P_LOBBY_HOSTING_UNREACHABLE)
			code[0] = 0;
	}
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
		/* (a joiner: its own build, which the host waits for: it saw
		"Starting..." for minutes, nothing said, and gave up) */
		else if (!host && forge_net_client_building_status(&percent))
			snprintf(text, sizeof(text), T("Building the extra objects (%d%%)"),
				(int)percent);
		else
			snprintf(text, sizeof(text), "%s", T("Starting..."));
	}
	else if (waiting)
		snprintf(text, sizeof(text), "%s", cooperative && game->maximum_players <= 2 ? T("Waiting for your partner") :
			T("Waiting for other players"));
	else if (host)
		snprintf(text, sizeof(text), "%s", h3_buttons(T("Ready: Cross starts the game")));
	else if (forge_net_client_building_status(&build_percent))
		snprintf(text, sizeof(text), T("Building the extra objects (%d%%)"), (int)build_percent);
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
	else if (host && online)
	{
		/* (its code not given out yet: the brokers not reached) */
		text_show(named(screen, "code_label"), T("NETWORK"));
		text_show(named(screen, "code"), T("ONLINE"));
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
		char const *state = listed ? "public" : "private";

		if (reach_state == P2P_LOBBY_BROWSE_UNREACHABLE || hosting_state == P2P_LOBBY_HOSTING_UNREACHABLE)
		{
			snprintf(text, sizeof(text), "%s", T("Not listed: can't reach the online game list"));
			state = "not reached";
		}
		else if (reach_state != P2P_LOBBY_BROWSE_EMPTY)
		{
			snprintf(text, sizeof(text), "%s", T("Connecting..."));
			state = "connecting";
		}
		else if (listed && hosting_state != P2P_LOBBY_HOSTING_LISTED)
		{
			snprintf(text, sizeof(text), "%s", T("Getting listed..."));
			state = "getting listed";
		}
		else
			snprintf(text, sizeof(text), "%s", listed ? (password ? T("PUBLIC: listed in the server browser,\nwith a password.") :
				T("PUBLIC: listed in the server browser.")) : T("PRIVATE: joined by its code."));
		text_show(named(screen, "visibility"), text);
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "; %s", state);
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
			if (host && online && code[0] && network_wire_crossplay() && p2p_hosting_invite(invite, (int)sizeof(invite)) &&
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

/* ---------- the Halo 3 dialog (halo3/dialog.xml) */

/* Menu style Halo 3: every message box and question the game shows is the
Halo 3 dialog - halo3/dialog.xml's screens, standing for the Xbox's boxes
(dialog_ui.xml, dialog_ingame.xml: menu_tags.c) and for the platform's own
(sdl_platform.c platform_show_message, platform_ask_question: map sharing,
the invites, the crossplay check). "port h3 dialog" gives each its title and
text, the port's own words in the player's language; an error's text box
(display_error) holds the error's number, which picks them. With Menu style
Halo CE the Xbox's and the platform's own boxes, as ever */

/* the screens, by their root's name: what each is */
enum
{
	_h3_dialog_error,
	_h3_dialog_abort,
	_h3_dialog_exit,
	_h3_dialog_revert,
	_h3_dialog_restart,
	_h3_dialog_quit,
	_h3_dialog_saving_progress,
	_h3_dialog_saving,
	_h3_dialog_creating,
	_h3_dialog_delete_profile,
	_h3_dialog_delete_gametype,
	_h3_dialog_message,
	_h3_dialog_question,
};

static struct
{
	char const *name;
	short kind;
} const h3_dialog_screens[] =
{
	{ "error", _h3_dialog_error }, { "notice", _h3_dialog_error }, { "half_error", _h3_dialog_error },
	{ "half_notice", _h3_dialog_error }, { "abort", _h3_dialog_abort }, { "abort_final", _h3_dialog_abort },
	{ "exit", _h3_dialog_exit }, { "revert", _h3_dialog_revert }, { "half_revert", _h3_dialog_revert },
	{ "restart", _h3_dialog_restart }, { "half_restart", _h3_dialog_restart }, { "half_quit", _h3_dialog_quit },
	{ "savequit", _h3_dialog_saving_progress }, { "saved", _h3_dialog_saving_progress },
	{ "saving", _h3_dialog_saving }, { "creating", _h3_dialog_creating }, { "delprof", _h3_dialog_delete_profile },
	{ "delgt", _h3_dialog_delete_gametype }, { "message", _h3_dialog_message }, { "question", _h3_dialog_question },
};

/* each error's title and text (ui_widget.h's error codes, in order): the
port's own words; NULL text, the map's (its displayed_error_messages) */
static struct
{
	char const *title;
	char const *text;
} const h3_dialog_errors[NUMBER_OF_ERROR_CODES] =
{
	{ N_("ERROR"), N_("Something went wrong.") },
	{ N_("NETWORK ERROR"), N_("There's a problem with the network connection.") },
	{ N_("GAME CLOSED"), N_("That game isn't open to join anymore.") },
	{ N_("COULDN'T JOIN"), N_("You couldn't join that game.") },
	{ N_("GAME ENDED"), N_("The host has closed the game.") },
	{ N_("NO NETWORK"), N_("There's no network connection. Check your Wi-Fi and try again.") },
	{ N_("CONNECTION LOST"), N_("The connection to the game was lost.") },
	{ N_("COULDN'T JOIN"), N_("Joining the game failed. It may be full, or gone.") },
	{ N_("OUT OF SYNC"), N_("Your game fell out of step with the host's, so it ended.") },
	{ N_("CONNECTION PROBLEM"), N_("The connection is having trouble. The game may end.") },
	{ N_("NETWORK ERROR"), NULL },
	{ N_("CONTROLLER"), N_("There's a problem with a controller.") },
	{ N_("CONTROLLER DISCONNECTED"), N_("Reconnect the controller, then press START to go on.") },
	{ N_("CONTROLLER DISCONNECTED"), N_("Reconnect the controller to go on.") },
	{ N_("STORAGE"), NULL },
	{ N_("SAVING"), NULL },
	{ N_("SAVING"), NULL },
	{ N_("SAVING"), NULL },
	{ N_("CO-OP"), N_("Each co-op player needs a controller of their own.") },
	{ N_("CO-OP"), N_("Co-op needs two controllers.") },
	{ N_("CONTROLLER"), NULL },
	{ N_("ERROR"), NULL },
	{ N_("REVERT TO CHECKPOINT"), NULL },
	{ N_("RESTART MISSION"), NULL },
	{ N_("DELETE PROFILE"), NULL },
	{ N_("DELETE GAME TYPE"), NULL },
	{ N_("CAN'T DELETE"), N_("The game's own game types can't be deleted.") },
	{ N_("NAME TAKEN"), N_("There's already a file with that name. Choose another one.") },
	{ N_("DISCARD CHANGES"), N_("Leave without saving your changes?") },
	{ N_("NAME NEEDED"), N_("Type a name first.") },
	{ N_("SAVING"), NULL },
	{ N_("DAMAGED FILE"), N_("A saved file is damaged and can't be used.") },
	{ N_("ERROR"), NULL },
	{ N_("STORAGE FULL"), NULL },
	{ N_("TOO MANY FILES"), N_("There are too many saved files. Delete some to save more.") },
	{ N_("DAMAGED DATA"), N_("The game's data can't be read. Its files may be damaged or missing.") },
	{ N_("TOO MANY FILES"), N_("No more of these files can be made. Delete one to make another.") },
	{ N_("PROFILE ERROR"), N_("The profile couldn't be saved.") },
	{ N_("SAVE ERROR"), N_("The game type couldn't be saved.") },
	{ N_("DAMAGED SAVE"), N_("The saved game is damaged and can't be loaded.") },
};

/* (the port's words for an error whose text is the port's: the Xbox's code
that carries it, display_error_text_deferred) */
#define H3_DIALOG_PORT_TEXT_ERROR _error_cannot_create_saved_game_file_with_empty_name
/* the messages waiting their turn (platform_show_message) */
#define H3_DIALOG_MESSAGES 4

static struct
{
	/* (platform_dialog_hook's, from any thread) */
	pthread_mutex_t lock;
	/* the dialog can show the platform's messages now: Menu style Halo 3,
	the dialog in the loaded map (each frame, pc_menu_functions_update) */
	volatile boolean ready;
	/* the platform's messages waiting, the first one shown */
	char message_titles[H3_DIALOG_MESSAGES][80];
	char messages[H3_DIALOG_MESSAGES][640];
	short message_count;
	boolean message_shown;
	/* its question: asked, shown (opened at least once), withdrawn */
	boolean question_asked;
	boolean question_shown;
	boolean question_withdrawn;
	char question_title[80];
	char question[640];
	/* the dialog last logged (its screen, made when, its title) */
	void const *logged_screen;
	long logged_creation;
	char logged[96];
} h3_dialog = { PTHREAD_MUTEX_INITIALIZER };

/* sdl_platform.c's */
extern int (*platform_dialog_hook)(int question, const char *title, const char *text);
void platform_question_answered(int yes);
void platform_show_message_box(const char *title, const char *message);
void platform_ask_question_box(const char *title, const char *text);

/* a text box's own text, as text_show, longer (a map download's question) */
static void h3_dialog_text(struct widget_instance *widget, char const *text)
{
	unsigned short characters[700];
	long length;

	if (!widget || widget->type != _ui_widget_type_text_box)
		return;
	length = halo_menus_utf16(text ? text : "", characters, (long)NUMBEROF(characters));
	widget->parameters.text_box.text = ui_widget_realloc(widget->parameters.text_box.text,
		(word)(length * sizeof(wchar_t)), __FILE__, __LINE__);
	if (widget->parameters.text_box.text)
		memcpy(widget->parameters.text_box.text, characters, length * sizeof(wchar_t));
}

/* whether a line of text is wider than the dialog's text box */
static boolean h3_dialog_too_wide(struct widget_instance *widget, char const *text, short width)
{
	unsigned short characters[700];

	halo_menus_utf16(text, characters, (long)NUMBEROF(characters));
	return ui_widget_port_text_too_wide(widget, (wchar_t const *)characters, width);
}

/* (the dialog's text box: the text in lines that fit its width, broken at
spaces; its own line breaks kept) the text shown, made once for each text
and box (the box measures it in its font) */
static void h3_dialog_text_wrapped(struct widget_instance *widget, char const *text)
{
	static struct
	{
		void const *widget;
		long creation;
		char text[700];
		char wrapped[760];
	} made;
	char line[700];
	char const *cursor = text;
	size_t length = 0;
	short width;

	if (!widget || !text)
		return;
	if (made.widget == widget && made.creation == widget->creation_time && !strcmp(made.text, text))
	{
		h3_dialog_text(widget, made.wrapped);
		return;
	}
	made.widget = widget;
	made.creation = widget->creation_time;
	snprintf(made.text, sizeof(made.text), "%s", text);
	made.wrapped[0] = 0;
	width = ui_widget_port_text_box_width(widget);
	line[0] = 0;
	while (*cursor)
	{
		size_t word_length = strcspn(cursor, " \n");
		char candidate[700];

		snprintf(candidate, sizeof(candidate), "%s%s%.*s", line, line[0] ? " " : "", (int)word_length, cursor);
		if (line[0] && width > 0 && h3_dialog_too_wide(widget, candidate, width))
		{
			length += snprintf(made.wrapped + length, length < sizeof(made.wrapped) ? sizeof(made.wrapped) - length : 0,
				"%s\n", line);
			snprintf(line, sizeof(line), "%.*s", (int)word_length, cursor);
		}
		else
			snprintf(line, sizeof(line), "%s", candidate);
		cursor += word_length;
		if (*cursor == '\n')
		{
			length += snprintf(made.wrapped + length, length < sizeof(made.wrapped) ? sizeof(made.wrapped) - length : 0,
				"%s\n", line);
			line[0] = 0;
		}
		if (*cursor)
			cursor++;
	}
	if (length < sizeof(made.wrapped))
		snprintf(made.wrapped + length, sizeof(made.wrapped) - length, "%s", line);
	h3_dialog_text(widget, made.wrapped);
}

/* the game's UTF-16 made UTF-8 (the map's error text, the port's own) */
static void h3_dialog_utf8(wchar_t const *text, char *out, size_t size)
{
	size_t length = 0;

	for (; text && *text && length + 4 < size; text++)
	{
		unsigned long character = (unsigned short)*text;

		if (character < 0x80)
			out[length++] = (char)character;
		else if (character < 0x800)
		{
			out[length++] = (char)(0xC0 | (character >> 6));
			out[length++] = (char)(0x80 | (character & 0x3F));
		}
		else
		{
			out[length++] = (char)(0xE0 | (character >> 12));
			out[length++] = (char)(0x80 | ((character >> 6) & 0x3F));
			out[length++] = (char)(0x80 | (character & 0x3F));
		}
	}
	out[length] = 0;
}

/* a title in capitals, Latin-1's accented letters too (the Spanish
"mapa" of "Halo: custom map"); "Halo: " left off, as the dialog is the game's */
static void h3_dialog_title_of(char const *title, char *out, size_t size)
{
	unsigned char *character;

	if (!title)
		title = "";
	if (!strncmp(title, "Halo: ", 6))
		title += 6;
	snprintf(out, size, "%s", title);
	for (character = (unsigned char *)out; *character; character++)
	{
		if (*character >= 'a' && *character <= 'z')
			*character = (unsigned char)(*character - 'a' + 'A');
		/* (U+00E0 to U+00FE but U+00F7: two bytes, C3 A0-BE) */
		else if (*character == 0xC3 && character[1] >= 0xA0 && character[1] <= 0xBE && character[1] != 0xB7)
			*++character -= 0x20;
	}
}

/* (the screen's root name) which dialog, NONE not one */
static short h3_dialog_kind(struct widget_instance *screen)
{
	char const *name = screen ? tag_get_name(screen->definition_tag_index) : NULL;
	static char const prefix[] = "pc\\h3\\dlg\\";
	size_t index;

	if (!name || strncmp(name, prefix, sizeof(prefix) - 1))
		return NONE;
	name += sizeof(prefix) - 1;
	for (index = 0; index < NUMBEROF(h3_dialog_screens); index++)
	{
		size_t length = strlen(h3_dialog_screens[index].name);

		if (!strncmp(name, h3_dialog_screens[index].name, length) && !strcmp(name + length, "\\screen"))
			return h3_dialog_screens[index].kind;
	}
	return NONE;
}

/* an error's words: the port's text for it (the error's box showing the
port's own), else this table's, else the map's */
static void h3_dialog_error_words(struct widget_instance *code_box, short code, char *title, size_t title_size,
	char *text, size_t text_size)
{
	wchar_t const *port_text = ui_widget_port_error_text_of(code_box);

	code = PIN(code, 0, NUMBER_OF_ERROR_CODES - 1);
	snprintf(title, title_size, "%s", T(h3_dialog_errors[code].title));
	if (port_text)
	{
		h3_dialog_utf8(port_text, text, text_size);
		if (code == H3_DIALOG_PORT_TEXT_ERROR)
			snprintf(title, title_size, "%s", T("NOTICE"));
		return;
	}
	switch (code)
	{
	case _error_warning_revert_to_saved_progress_will_be_lost:
		snprintf(text, text_size, "%s", T("Go back to your last checkpoint? What you've done since then will be lost."));
		return;
	case _error_warning_restart_level_progress_will_be_lost:
		snprintf(text, text_size, "%s", T("Start this mission over? Your progress in it will be lost."));
		return;
	case _error_warning_delete_player_profile:
		snprintf(text, text_size, "%s", T("Delete this profile? Its settings and campaign progress will be gone for good."));
		return;
	case _error_warning_delete_multiplayer_profile:
		snprintf(text, text_size, "%s", T("Delete this game type? It can't be undone."));
		return;
	case _error_controller_saving_file_warning:
	case _error_warning_saving_checkpoint:
#ifdef HALO_VITA
		snprintf(text, text_size, "%s", T("Saving. Don't turn off your PS Vita."));
#else
		snprintf(text, text_size, "%s", T("Saving. Don't close the game."));
#endif
		return;
	case _error_controller_creating_player_profile_warning:
		snprintf(text, text_size, "%s", T("Creating your profile..."));
		return;
	case _error_controller_creating_game_settings_file_warning:
		snprintf(text, text_size, "%s", T("Saving your game type..."));
		return;
	case _error_hard_drive_not_enough_free_space:
#ifdef HALO_VITA
		snprintf(text, text_size, "%s", T("There isn't enough free space on the memory card to save. Free some space, or go on without saving."));
#else
		snprintf(text, text_size, "%s", T("There isn't enough free space on the disk to save. Free some space, or go on without saving."));
#endif
		return;
	}
	if (h3_dialog_errors[code].text)
	{
		snprintf(text, text_size, "%s", T(h3_dialog_errors[code].text));
		return;
	}
	text[0] = 0;
	{
		long list = tag_loaded('ustr', "ui\\shell\\strings\\displayed_error_messages");
		wchar_t const *map_text = list != NONE ? unicode_string_list_get_string(list, code) : NULL;

		h3_dialog_utf8(map_text, text, text_size);
	}
}

/* (port h3 dialog: the dialog's screen, each frame) its title and text */
static void h3_dialog_update(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	short kind = h3_dialog_kind(screen);
	char title[96], text[700];
	short code = NONE;

	if (kind == NONE)
		return;
	title[0] = text[0] = 0;
	switch (kind)
	{
	case _h3_dialog_error:
	case _h3_dialog_abort:
	{
		/* (display_error's: the code box, the first child's first child;
		display_error_abort_to_dashboard's: the screen itself) */
		struct widget_instance *code_box = kind == _h3_dialog_abort ? screen : named(screen, "code_text");

		code = code_box ? code_box->parameters.text_box.string_list_index : NONE;
		h3_dialog_error_words(code_box, code, title, sizeof(title), text, sizeof(text));
		if (kind == _h3_dialog_abort)
		{
			size_t length = strlen(text);

			snprintf(text + length, sizeof(text) - length, "%s%s", length ? "\n\n" : "",
				named(screen, "row_continue") ?
				T("CONTINUE plays on without saving. QUIT GAME closes Halo.") : T("Halo has to close."));
		}
		break;
	}
	case _h3_dialog_exit:
		snprintf(title, sizeof(title), "%s", T("QUITTING"));
		snprintf(text, sizeof(text), "%s", T("Halo is closing..."));
		break;
	case _h3_dialog_revert:
		h3_dialog_error_words(NULL, _error_warning_revert_to_saved_progress_will_be_lost, title, sizeof(title), text,
			sizeof(text));
		break;
	case _h3_dialog_restart:
		h3_dialog_error_words(NULL, _error_warning_restart_level_progress_will_be_lost, title, sizeof(title), text,
			sizeof(text));
		break;
	case _h3_dialog_quit:
		snprintf(title, sizeof(title), "%s", T("QUIT MISSION"));
		snprintf(text, sizeof(text), "%s", T("Quit to the main menu? What you've done since your last checkpoint will be lost."));
		break;
	case _h3_dialog_saving_progress:
		snprintf(title, sizeof(title), "%s", T("SAVING"));
#ifdef HALO_VITA
		snprintf(text, sizeof(text), "%s", T("Saving your progress. Don't turn off your PS Vita."));
#else
		snprintf(text, sizeof(text), "%s", T("Saving your progress. Don't close the game."));
#endif
		break;
	case _h3_dialog_saving:
		h3_dialog_error_words(NULL, _error_controller_saving_file_warning, title, sizeof(title), text, sizeof(text));
		break;
	case _h3_dialog_creating:
		h3_dialog_error_words(NULL, _error_controller_creating_player_profile_warning, title, sizeof(title), text,
			sizeof(text));
		break;
	case _h3_dialog_delete_profile:
		h3_dialog_error_words(NULL, _error_warning_delete_player_profile, title, sizeof(title), text, sizeof(text));
		break;
	case _h3_dialog_delete_gametype:
		h3_dialog_error_words(NULL, _error_warning_delete_multiplayer_profile, title, sizeof(title), text,
			sizeof(text));
		break;
	case _h3_dialog_message:
		pthread_mutex_lock(&h3_dialog.lock);
		if (h3_dialog.message_count)
		{
			h3_dialog_title_of(h3_dialog.message_titles[0], title, sizeof(title));
			snprintf(text, sizeof(text), "%s", h3_dialog.messages[0]);
		}
		pthread_mutex_unlock(&h3_dialog.lock);
		break;
	case _h3_dialog_question:
		pthread_mutex_lock(&h3_dialog.lock);
		if (h3_dialog.question_asked)
		{
			h3_dialog_title_of(h3_dialog.question_title, title, sizeof(title));
			snprintf(text, sizeof(text), "%s", h3_dialog.question);
		}
		pthread_mutex_unlock(&h3_dialog.lock);
		break;
	}
	h3_dialog_text(named(screen, "title"), title);
	h3_dialog_text_wrapped(named(screen, "body"), text);
	/* (each dialog said once in the log as it opens: the tests read it) */
	if (h3_dialog.logged_screen != screen || h3_dialog.logged_creation != screen->creation_time ||
		strcmp(h3_dialog.logged, title))
	{
		h3_dialog.logged_screen = screen;
		h3_dialog.logged_creation = screen->creation_time;
		snprintf(h3_dialog.logged, sizeof(h3_dialog.logged), "%s", title);
		platform_log("menus: h3 dialog: %s \"%s\"", tag_get_name(screen->definition_tag_index), title);
		if (code != NONE)
			platform_log("menus: h3 dialog: error %d", (int)code);
	}
}

/* (port h3 dialog ok, yes, no: the port's message or question answered) */
static void h3_dialog_answer(long function)
{
	if (function == _function_h3_dialog_ok)
	{
		pthread_mutex_lock(&h3_dialog.lock);
		if (h3_dialog.message_count)
		{
			h3_dialog.message_count--;
			memmove(h3_dialog.messages[0], h3_dialog.messages[1], sizeof(h3_dialog.messages[0]) * h3_dialog.message_count);
			memmove(h3_dialog.message_titles[0], h3_dialog.message_titles[1],
				sizeof(h3_dialog.message_titles[0]) * h3_dialog.message_count);
		}
		h3_dialog.message_shown = FALSE;
		pthread_mutex_unlock(&h3_dialog.lock);
		platform_log("menus: h3 dialog: message dismissed");
		return;
	}
	pthread_mutex_lock(&h3_dialog.lock);
	h3_dialog.question_asked = FALSE;
	h3_dialog.question_shown = FALSE;
	pthread_mutex_unlock(&h3_dialog.lock);
	platform_question_answered(function == _function_h3_dialog_yes);
	platform_log("menus: h3 dialog: question answered %s", function == _function_h3_dialog_yes ? "yes" : "no");
}

/* (sdl_platform.c platform_dialog_hook: any thread) a message or question
of the platform's taken by the dialog while it can show it: Menu style
Halo 3, the dialog in the map (and a window someone watches: a test's
hidden one only with HALO_TEST_DIALOGS); text NULL, the question withdrawn */
static int h3_dialog_port_hook(int question, char const *title, char const *text)
{
	int taken = 0;

	pthread_mutex_lock(&h3_dialog.lock);
	if (!text)
	{
		if (question && h3_dialog.question_asked)
		{
			h3_dialog.question_asked = FALSE;
			h3_dialog.question_withdrawn = h3_dialog.question_shown;
			h3_dialog.question_shown = FALSE;
		}
		pthread_mutex_unlock(&h3_dialog.lock);
		return 1;
	}
	if (h3_dialog.ready && ((!config_boolean("debug.hidden_window") && !config_boolean("debug.null_renderer")) ||
		getenv("HALO_TEST_DIALOGS")))
	{
		if (question)
		{
			snprintf(h3_dialog.question_title, sizeof(h3_dialog.question_title), "%s", title ? title : "");
			snprintf(h3_dialog.question, sizeof(h3_dialog.question), "%s", text);
			h3_dialog.question_asked = TRUE;
			h3_dialog.question_shown = FALSE;
			h3_dialog.question_withdrawn = FALSE;
			taken = 1;
		}
		else if (h3_dialog.message_count < H3_DIALOG_MESSAGES)
		{
			snprintf(h3_dialog.message_titles[h3_dialog.message_count], sizeof(h3_dialog.message_titles[0]), "%s",
				title ? title : "");
			snprintf(h3_dialog.messages[h3_dialog.message_count], sizeof(h3_dialog.messages[0]), "%s", text);
			h3_dialog.message_count++;
			taken = 1;
		}
	}
	pthread_mutex_unlock(&h3_dialog.lock);
	return taken;
}

/* (each frame, the main loop) the platform's message or question opened in
the dialog when nothing else is up in front (an error's box, a cinematic,
another dialog of these): over the screen showing, or over the game; a
question withdrawn closes its dialog; the dialog gone from the map (Menu
style Halo CE chosen, a map without it), what waits goes to the platform's
own box */
static void h3_dialog_port_update(void)
{
	long menu_tags_generation(void);
	/* (the screens' tags, looked up once for each map's) */
	static long message_tag = NONE, question_tag = NONE, generation = -1;
	struct widget_instance *active = ui_widget_port_active_screen(0);
	boolean ready, open_message = FALSE, open_question = FALSE;
	char title[80], text[640];

	if (generation != menu_tags_generation())
	{
		generation = menu_tags_generation();
		message_tag = menu_tags_ingame_screen("h3/dlg/message/screen");
		question_tag = menu_tags_ingame_screen("h3/dlg/question/screen");
	}
	ready = halo_menu_style_halo3() && message_tag != NONE && question_tag != NONE;

	if (!platform_dialog_hook)
		platform_dialog_hook = h3_dialog_port_hook;
	h3_dialog.ready = ready;
	pthread_mutex_lock(&h3_dialog.lock);
	if (!ready)
	{
		/* (the platform's own box for what waits) */
		if (h3_dialog.question_asked)
		{
			snprintf(title, sizeof(title), "%s", h3_dialog.question_title);
			snprintf(text, sizeof(text), "%s", h3_dialog.question);
			h3_dialog.question_asked = FALSE;
			h3_dialog.question_shown = FALSE;
			pthread_mutex_unlock(&h3_dialog.lock);
			platform_ask_question_box(title, text);
			pthread_mutex_lock(&h3_dialog.lock);
		}
		while (h3_dialog.message_count)
		{
			snprintf(title, sizeof(title), "%s", h3_dialog.message_titles[0]);
			snprintf(text, sizeof(text), "%s", h3_dialog.messages[0]);
			h3_dialog.message_count--;
			memmove(h3_dialog.messages[0], h3_dialog.messages[1], sizeof(h3_dialog.messages[0]) * h3_dialog.message_count);
			memmove(h3_dialog.message_titles[0], h3_dialog.message_titles[1],
				sizeof(h3_dialog.message_titles[0]) * h3_dialog.message_count);
			pthread_mutex_unlock(&h3_dialog.lock);
			platform_show_message_box(title, text);
			pthread_mutex_lock(&h3_dialog.lock);
		}
		h3_dialog.message_shown = FALSE;
		pthread_mutex_unlock(&h3_dialog.lock);
		return;
	}
	/* (a question withdrawn: its dialog closed, if it is up) */
	if (h3_dialog.question_withdrawn)
	{
		h3_dialog.question_withdrawn = FALSE;
		if (active && active->definition_tag_index == question_tag)
		{
			pthread_mutex_unlock(&h3_dialog.lock);
			platform_log("menus: h3 dialog: the question was withdrawn");
			ui_widget_delete(active);
			return;
		}
	}
	/* (the dialog up already, or something else in front) */
	if (active && (active->definition_tag_index == message_tag || active->definition_tag_index == question_tag ||
		active->widget_is_error_dialog))
	{
		if (active->definition_tag_index == question_tag)
			h3_dialog.question_shown = TRUE;
		if (active->definition_tag_index == message_tag)
			h3_dialog.message_shown = TRUE;
		pthread_mutex_unlock(&h3_dialog.lock);
		return;
	}
	/* (asked and not up: opened, the question first; again if its screen
	went - a map's change -, as it is not answered) */
	if (h3_dialog.question_asked)
		open_question = TRUE;
	else if (h3_dialog.message_count)
		open_message = TRUE;
	pthread_mutex_unlock(&h3_dialog.lock);
	if ((!open_question && !open_message) || cinematic_in_progress())
		return;
	if (ui_widget_load_by_name_or_tag(NULL, open_question ? question_tag : message_tag, NULL, 0,
		active ? active->definition_tag_index : NONE, NONE, NONE))
	{
		pthread_mutex_lock(&h3_dialog.lock);
		if (open_question)
			h3_dialog.question_shown = TRUE;
		else
			h3_dialog.message_shown = TRUE;
		pthread_mutex_unlock(&h3_dialog.lock);
	}
}

/* ---- HOST SETTINGS (Menu style Online: menus/online/host.xml, the owner
Oct 24 2026): one page for the three lobbies' HOST SETTINGS row, Halo
Online's server settings laid out our way. Each row is a setting the game
already has and already applies, set as the settings panel, GAME OPTIONS
and the FRIENDS screen set it (setting_set: on the Vita kept in
settings.txt), nothing new on the wire:
- NETWORK: the lobby's network picker (the multiplayer and Forge lobbies'
  SELECT NETWORK, the campaign's co-op one), the connection switched live
  as before.
- LOBBY NAME (network.lobby_name) and PASSWORD (network.lobby_password,
  typed in the system's text box as Server Setup types it, never logged).
- PRIVACY: PUBLIC lists the game in the server browser (network.host_public;
  co-op network.coop_public, as X on co-op's waiting screen sets it);
  FRIENDS keeps it unlisted with Friends can join on (friends.join: a
  friend joins from FRIENDS in one press); INVITE ONLY unlisted with it off.
  The game's code and invites join it in all three, as before. The live
  lobby on ONLINE takes it as the host's choice (h3_live_public_chosen).
- WHO CAN JOIN (network.who_can_join; Forge Vitas only, forge_net.inc), as
  GAME OPTIONS' (the live lobby hosted again on its wire by h3_live).
- MAX PLAYERS (network.max_players, 2 to 16; Forge's 8 at most) or the
  campaign's CO-OP PLAYERS (network.coop_players: 2 to 4 on the Vitas).
- ONLINE BOTS (bots.online), the multiplayer lobby's on ONLINE.
Rows a network does not use are left out: OFFLINE and LOCAL keep NETWORK
alone; PRIVACY, WHO CAN JOIN, PASSWORD and ONLINE BOTS are ONLINE's. */

enum
{
	_online_host_multiplayer,
	_online_host_forge,
	_online_host_campaign,
};

enum
{
	_online_host_row_network,
	_online_host_row_campnet,
	_online_host_row_name,
	_online_host_row_privacy,
	_online_host_row_who,
	_online_host_row_max,
	_online_host_row_coop,
	_online_host_row_password,
	_online_host_row_bots,
	NUMBER_OF_ONLINE_HOST_ROWS
};

static char const *const online_host_rows[NUMBER_OF_ONLINE_HOST_ROWS] =
{
	"op_network", "op_campnet", "op_name", "op_privacy", "op_who", "op_max", "op_coop", "op_password", "op_bots",
};

enum
{
	_online_host_public,
	_online_host_friends,
	_online_host_invite,
	NUMBER_OF_ONLINE_HOST_PRIVACIES
};

#define ONLINE_HOST_ROW_SPACING 19
/* (Forge: a network Forge game takes 8 at most, network_server_manager.c) */
#define ONLINE_HOST_FORGE_PLAYERS 8
#if defined(HALO_VITA) || defined(HALO_NET_AS_VITA)
/* (co-op: 2, 3 and 4 experimental on the Vitas, the settings panel's) */
#define ONLINE_HOST_COOP_PLAYERS 4
#else
#define ONLINE_HOST_COOP_PLAYERS 16
#endif

static struct
{
	/* the lobby that opened the page (port host multiplayer, forge, campaign) */
	short lobby;
	/* the page as said last (the tests read it) */
	char said[256];
} online_host;

static short online_host_row(struct widget_instance *widget)
{
	short row;

	for (row = 0; widget && widget->name && row < NUMBER_OF_ONLINE_HOST_ROWS; row++)
	{
		if (!strcmp(widget->name, online_host_rows[row]))
			return row;
	}
	return NONE;
}

/* the lobby's network now, as the lobby shows it (the co-op networks as
the multiplayer ones: LOCAL as OFFLINE) */
static short online_host_network(void)
{
	if (online_host.lobby == _online_host_campaign)
	{
		if (h3_campaign.network != _h3_campaign_local)
			h3_campaign.network = h3_campaign_network_of_connection();
		return h3_campaign.network == _h3_campaign_online ? _h3_network_online :
			h3_campaign.network == _h3_campaign_adhoc ? _h3_network_adhoc :
			h3_campaign.network == _h3_campaign_system_link ? _h3_network_system_link : _h3_network_offline;
	}
	if (!h3_mp.initialized)
		h3_mp_initialize();
	if (online_host.lobby == _online_host_forge)
	{
		if (h3_forge.network_set && h3_forge.network != _h3_network_offline)
			h3_forge.network = h3_mp_network_of_connection();
		return h3_forge.network_set ? h3_forge.network : _h3_network_offline;
	}
	if (h3_mp.network != _h3_network_offline)
		h3_mp.network = h3_mp_network_of_connection();
	return h3_mp.network;
}

/* whether the row is the page's for the lobby and its network */
static boolean online_host_row_shown(short row, short network)
{
	boolean campaign = online_host.lobby == _online_host_campaign;

	switch (row)
	{
	case _online_host_row_network:
		return !campaign;
	case _online_host_row_campnet:
		return campaign;
	case _online_host_row_name:
		return network != _h3_network_offline;
	case _online_host_row_max:
		return network != _h3_network_offline && !campaign;
	case _online_host_row_coop:
		return network != _h3_network_offline && campaign;
	case _online_host_row_privacy:
	case _online_host_row_password:
		return network == _h3_network_online;
	case _online_host_row_who:
		return network == _h3_network_online && !campaign;
	case _online_host_row_bots:
		return network == _h3_network_online && online_host.lobby == _online_host_multiplayer;
	}
	return FALSE;
}

/* (PRIVACY) the game listed in the server browser: co-op's own setting; the
multiplayer and Forge lobbies' private on ONLINE until the host chooses
(h3_live_private_by_default) */
static boolean online_host_listed(void)
{
	if (online_host.lobby == _online_host_campaign)
		return p2p_lobby_coop_public(NULL) != 0;
	return h3_live.public_chosen && strcmp(setting_get("HALO_NET_HOST_PUBLIC", "true"), "false") != 0 &&
		config_boolean("network.public_lobby");
}

static short online_host_privacy(void)
{
	if (online_host_listed())
		return _online_host_public;
	return config_boolean_live("friends.join", "HALO_FRIENDS_JOIN") ? _online_host_friends : _online_host_invite;
}

static void online_host_privacy_set(short privacy)
{
	boolean listed = privacy == _online_host_public;

	setting_set("HALO_FRIENDS_JOIN", privacy == _online_host_invite ? "false" : "true");
	if (online_host.lobby == _online_host_campaign)
	{
		/* (as X on co-op's waiting screen sets it: coop_menu.c) */
		if (!halo_test_setting_hook || !halo_test_setting_hook("HALO_NET_COOP_PUBLIC", listed ? "true" : "false"))
		{
			if (!config_write_boolean("network.coop_public", listed))
				platform_log("menus: host settings: could not write network.coop_public to config.toml");
		}
		p2p_lobby_set_coop_public(listed);
	}
	else
	{
		setting_set("HALO_NET_HOST_PUBLIC", listed ? "true" : "false");
		/* (the live lobby's, or the one hosted next: the host's choice) */
		h3_live_public_chosen();
	}
}

/* the most players: the setting's, in the lobby's range */
static int online_host_players_range(int *least)
{
	*least = 2;
	return online_host.lobby == _online_host_campaign ? ONLINE_HOST_COOP_PLAYERS :
		online_host.lobby == _online_host_forge ? ONLINE_HOST_FORGE_PLAYERS : 16;
}

static int online_host_players(void)
{
	int least, most = online_host_players_range(&least);
	long value;

	if (online_host.lobby == _online_host_campaign)
	{
		char const *setting = getenv("HALO_NET_COOP_PLAYERS");

		value = setting && setting[0] ? atol(setting) : config_integer("network.coop_players");
	}
	else
	{
		/* (0: the Xbox's 16, network_server_manager.c) */
		value = atol(setting_get("HALO_NET_MAX_PLAYERS", "16"));
		if (value < 2)
			value = 16;
	}
	return (int)PIN(value, least, most);
}

static void online_host_players_set(int players)
{
	char value[8];

	snprintf(value, sizeof(value), "%d", players);
	if (online_host.lobby == _online_host_campaign)
	{
		setting_set("HALO_NET_COOP_PLAYERS", value);
		/* (read as co-op is hosted: network_server_manager.c) */
		config_set_override("network.coop_players", value);
	}
	else
		setting_set("HALO_NET_MAX_PLAYERS", value);
}

/* (WHO CAN JOIN) the choices: the PC build's from VITA PC; Forge's never
EVERYONE (Forge is played on the Vitas' wire) */
static int online_host_audience_last(void)
{
	return online_host.lobby == _online_host_forge ? NETWORK_AUDIENCE_EVERYONE - 1 : NUMBER_OF_NETWORK_AUDIENCES - 1;
}

static char const *online_host_audience_name(int audience)
{
	return audience == NETWORK_AUDIENCE_EVERYONE ? T("EVERYONE") : audience == NETWORK_AUDIENCE_VITA_PC ?
		T("VITA PC") : T("VITA");
}

/* (port host multiplayer, forge, campaign: the lobby's HOST SETTINGS row) */
static boolean online_host_open(short lobby)
{
	online_host.lobby = lobby;
	online_host.said[0] = 0;
	platform_log("menus: host settings: opened from the %s lobby", lobby == _online_host_campaign ? "campaign" :
		lobby == _online_host_forge ? "Forge" : "multiplayer");
	return TRUE;
}

/* (port host next and previous: A, right and left on a row of choices) */
static boolean online_host_change(struct widget_instance *widget, int step)
{
	short row = online_host_row(widget);
	char value[16];

	switch (row)
	{
	case _online_host_row_privacy:
	{
		short privacy = online_host_privacy();

		do
			privacy = (short)h3_custom_step(privacy, NUMBER_OF_ONLINE_HOST_PRIVACIES, step);
		while (privacy == _online_host_public && !config_boolean("network.public_lobby"));
		online_host_privacy_set(privacy);
		platform_log("menus: host settings: privacy %s", privacy == _online_host_public ? "public" :
			privacy == _online_host_friends ? "friends" : "invite only");
		return TRUE;
	}
	case _online_host_row_who:
	{
		int first = network_wire_audience_first(), last = online_host_audience_last();
		int audience = PIN(network_wire_host_audience(), first, last);

		audience = first + (int)h3_custom_step(audience - first, last - first + 1, step);
		setting_set("HALO_NET_WHO_CAN_JOIN", network_wire_audience_value(audience));
		platform_log("menus: host settings: who can join %s", network_wire_audience_value(audience));
		return TRUE;
	}
	case _online_host_row_max:
	case _online_host_row_coop:
	{
		int least, most = online_host_players_range(&least);
		int players = least + (int)h3_custom_step(online_host_players() - least, most - least + 1, step);

		online_host_players_set(players);
		platform_log("menus: host settings: %s %d", online_host.lobby == _online_host_campaign ? "co-op players" :
			"max players", players);
		return TRUE;
	}
	case _online_host_row_bots:
	{
		short bots = (short)h3_custom_step(setup_bots_index(), (long)NUMBEROF(setup_bots_values), step);

		snprintf(value, sizeof(value), "%s", setup_bots_values[bots]);
		setting_set("HALO_BOTS_ONLINE", value);
		platform_log("menus: host settings: online bots %s", value);
		return TRUE;
	}
	}
	return TRUE;
}

/* (port host edit: A on LOBBY NAME or PASSWORD) typed in the system's text
box, as Server Setup types them */
static boolean online_host_edit(struct widget_instance *widget)
{
	short row = online_host_row(widget);
	char now[P2P_LOBBY_PASSWORD_SIZE];

	if (row == _online_host_row_name)
	{
		sanitize(now, sizeof(now), setting_get("HALO_NET_LOBBY_NAME", ""));
		typing_begin(_typing_lobby_name, T("Lobby name"), now, LOBBY_NAME_LENGTH, FALSE);
	}
	else if (row == _online_host_row_password)
	{
		sanitize(now, sizeof(now), setting_get("HALO_NET_LOBBY_PASSWORD", ""));
		typing_begin(_typing_lobby_password, T("Password (empty: none)"), now, PASSWORD_LENGTH, TRUE);
		memset(now, 0, sizeof(now));
	}
	return TRUE;
}

/* a row's value as shown */
static void online_host_value(short row, short network, char *text, int size)
{
	char typed[P2P_LOBBY_PASSWORD_SIZE];

	text[0] = 0;
	switch (row)
	{
	case _online_host_row_network:
		snprintf(text, (size_t)size, "%s", online_host.lobby == _online_host_forge && network == _h3_network_offline ?
			T("LOCAL") : h3_network_name(network));
		break;
	case _online_host_row_campnet:
		snprintf(text, (size_t)size, "%s", h3_campaign_network_name(h3_campaign.network));
		break;
	case _online_host_row_name:
		sanitize(typed, sizeof(typed), setting_get("HALO_NET_LOBBY_NAME", ""));
		snprintf(text, (size_t)size, "%s", typed[0] ? typed : T("YOUR PROFILE'S NAME"));
		break;
	case _online_host_row_privacy:
	{
		short privacy = online_host_privacy();

		snprintf(text, (size_t)size, "%s", privacy == _online_host_public ? T("PUBLIC") :
			privacy == _online_host_friends ? T("FRIENDS") : T("INVITE ONLY"));
		break;
	}
	case _online_host_row_who:
		snprintf(text, (size_t)size, "%s", online_host_audience_name(PIN(network_wire_host_audience(),
			network_wire_audience_first(), online_host_audience_last())));
		break;
	case _online_host_row_max:
	case _online_host_row_coop:
		snprintf(text, (size_t)size, "%d", online_host_players());
		break;
	case _online_host_row_password:
		/* (never the password itself: its stars) */
		stars(typed, sizeof(typed), setting_get("HALO_NET_LOBBY_PASSWORD", ""));
		snprintf(text, (size_t)size, "%s", typed[0] ? typed : T("NONE"));
		break;
	case _online_host_row_bots:
		snprintf(text, (size_t)size, "%s", setup_bots_values[setup_bots_index()]);
		h3_capitals(text);
		break;
	}
	memset(typed, 0, sizeof(typed));
}

static char const *online_host_help(short row)
{
	boolean campaign = online_host.lobby == _online_host_campaign;

	switch (row)
	{
	case _online_host_row_network:
	case _online_host_row_campnet:
		return campaign ? T("Where your co-op is hosted: LOCAL plays on this PS Vita alone; the co-op networks switch\n"
			"the connection, as the settings panel's Connection does.") :
			T("Where your game is hosted: ONLINE on the Internet, SYSTEM LINK for the PS Vitas on your\n"
			"Wi-Fi, AD HOC for those near you, OFFLINE on this PS Vita alone.");
	case _online_host_row_name:
		return T("The name the server browser and the System Link list show for your games (15 letters).");
	case _online_host_row_privacy:
		return T("PUBLIC: listed in everyone's server browser. FRIENDS: not listed; your friends join from\n"
			"FRIENDS in one press. INVITE ONLY: your invites alone. Your game's code always joins.");
	case _online_host_row_who:
		return online_host.lobby == _online_host_forge ?
			T("VITA: PS Vitas only. VITA PC: PS Vitas and Halo CE Vita on PC. Forge is played on the\n"
			"Vitas' own network, never with EVERYONE.") :
			T("VITA: PS Vitas only. VITA PC: PS Vitas and Halo CE Vita on PC. EVERYONE: OpenCE players\n"
			"on PC too, listed in both browsers. The lobby is hosted again as it changes.");
	case _online_host_row_max:
	case _online_host_row_coop:
		return campaign ? T("The most players your co-op takes. You run the campaign for everyone: 3 and 4 are\n"
			"experimental on a PS Vita.") : online_host.lobby == _online_host_forge ?
			T("The most players your games take (Forge takes 8 at most).") :
			T("The most players your games take, 2 to 16.");
	case _online_host_row_password:
		return T("Asked of those who join from the server browser or from FRIENDS; your invites and your\n"
			"game's code need none. Empty: none.");
	case _online_host_row_bots:
		return T("FULL: bots fill every slot but one and leave as players join. OFF: none. Or the most bots.\n"
			"None while EVERYONE can join.");
	}
	return "";
}

/* (port host: its list) the rows the lobby and its network take, one under
another, their values, the focused one's arrows and help, the connection */
static void online_host_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child, *found;
	short network = online_host_network();
	short shown = 0, focused = NONE;
	short base = 0;
	char text[160], typed[P2P_LOBBY_PASSWORD_SIZE], said[256];

	if (list->type != _ui_widget_type_column_list)
		return;
	/* (typed: LOBBY NAME, its first 15; PASSWORD, empty for none) */
	if (typing_done(_typing_lobby_name, typed, sizeof(typed)) && typed[0])
	{
		snprintf(text, sizeof(text), "%.*s", LOBBY_NAME_LENGTH, typed);
		setting_set("HALO_NET_LOBBY_NAME", text);
		platform_log("menus: host settings: lobby name %s", text);
	}
	if (typing_done(_typing_lobby_password, typed, sizeof(typed)))
	{
		setting_set("HALO_NET_LOBBY_PASSWORD", typed);
		platform_log("menus: host settings: password %s", typed[0] ? "set" : "none");
	}
	memset(typed, 0, sizeof(typed));
	/* (the first row's place: the rows below it laid out from there) */
	for (child = list->child; child; child = child->next)
	{
		if (child->vertical_offset < base || child == list->child)
			base = child->vertical_offset;
	}
	if (list->focused_child && !online_host_row_shown(online_host_row(list->focused_child), network))
	{
		for (child = list->child; child; child = child->next)
		{
			if (online_host_row_shown(online_host_row(child), network))
			{
				ui_widget_port_give_focus(list, child);
				break;
			}
		}
	}
	snprintf(said, sizeof(said), "%s", online_host.lobby == _online_host_campaign ? "campaign" :
		online_host.lobby == _online_host_forge ? "forge" : "multiplayer");
	for (child = list->child; child; child = child->next)
	{
		short row = online_host_row(child);
		boolean visible = row != NONE && online_host_row_shown(row, network);
		boolean has_focus = child == list->focused_child;
		boolean choices = row == _online_host_row_privacy || row == _online_host_row_who || row == _online_host_row_max ||
			row == _online_host_row_coop || row == _online_host_row_bots;

		widget_instance_set_visibility_recursive(child, visible);
		child->disabled = !visible;
		if (!visible)
			continue;
		child->vertical_offset = (short)(base + shown++ * ONLINE_HOST_ROW_SPACING);
		if (has_focus)
			focused = row;
		online_host_value(row, network, text, (int)sizeof(text));
		text_show(named(child, "value"), text);
		if ((found = named(child, "arrow_left")) != NULL)
			widget_instance_set_visibility_recursive(found, has_focus && choices);
		if ((found = named(child, "arrow_right")) != NULL)
			widget_instance_set_visibility_recursive(found, has_focus && choices);
		child->animation.current_frame_index = has_focus ? 1 : 0;
		snprintf(said + strlen(said), sizeof(said) - strlen(said), ", %s %s", online_host_rows[row] + 3, text);
	}
	/* (the subtitle: the lobby's; the connection under the rows, as the
	lobby's HOST SETTINGS row says it) */
	text_show(named(screen, "subtitle"), online_host.lobby == _online_host_campaign ? T("CAMPAIGN LOBBY") :
		online_host.lobby == _online_host_forge ? T("FORGE LOBBY") : T("MULTIPLAYER LOBBY"));
	if (network != _h3_network_offline)
		network_connection_status(text, (int)sizeof(text));
	else
		snprintf(text, sizeof(text), "%s", T("On this PS Vita only"));
	{
		char line[192];

		snprintf(line, sizeof(line), "%s: %s", T("Connection"), text);
		text_show(named(screen, "status"), line);
	}
	text_show(named(screen, "help"), focused != NONE ? online_host_help(focused) : "");
	/* (said when it changes: the tests read it; the password as stars) */
	if (strcmp(said, online_host.said))
	{
		snprintf(online_host.said, sizeof(online_host.said), "%s", said);
		platform_log("menus: host settings: %s", said);
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
	/* (the classic Multiplayer screens that say internet play is off: A opens the panel at
	its Multiplayer tab, where Connection is; the handler fails, as above) */
	case _function_settings_panel_multiplayer:
#ifdef HALO_VITA
		__atomic_store_n(&halo_settings_panel_request, HALO_SETTINGS_PANEL_MULTIPLAYER, __ATOMIC_RELEASE);
		platform_log("menus: the settings panel's Multiplayer tab asked for");
		return FALSE;
#else
		platform_log("menus: the settings panel's Multiplayer tab asked for (none on this build)");
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
		if (h3_mission_check(level, why, sizeof(why)) != _map_check_playable)
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
		main_set_map_name((char *)h3_level_path(h3_campaign.level));
		main_set_difficulty((word)h3_campaign.difficulty);
		platform_log("menus: h3 campaign: start %s on %s", h3_level_path(h3_campaign.level),
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
				h3_forge_note_building(label, progress);
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
		/* (Menu style Halo CE, the lobby opened from the Xbox main menu's
		FORGE: no live lobby there - the settings panel's network Forge game's
		way, the profile, then the game hosted and the Xbox's pregame) */
		if (!halo_menu_style_halo3())
		{
			if (!transport_network_available())
			{
				snprintf(h3_forge.note, sizeof(h3_forge.note), "%s",
					T("No network: wait for it, or choose\nNETWORK: LOCAL."));
				platform_log("menus: forge lobby: START FORGE (Halo CE menus): no network");
				return FALSE;
			}
			if (h3_forge.network == _h3_network_online && !internet_play())
			{
				snprintf(h3_forge.note, sizeof(h3_forge.note), "%s", T("Internet play is off"));
				return FALSE;
			}
			platform_log("menus: forge lobby: START FORGE (Halo CE menus): the profile, then the game hosted on %s",
				forge_map_name(h3_forge.map));
			{
				/* (its own copy: menu_functions_forge_host writes the lobby's) */
				char variant[sizeof(h3_forge.variant_file)];

				snprintf(variant, sizeof(variant), "%s", h3_forge.variant_file);
				return menu_functions_forge_host(h3_forge.map, variant, h3_forge.note, sizeof(h3_forge.note));
			}
		}
		if (h3_forge.network == _h3_network_online)
			h3_live_open_now("START FORGE");
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
	/* (the Halo 3 carnage report: halo3/carnage.xml) */
	case _function_h3_carnage_init:
		h3_carnage.page = _h3_carnage_summary;
		h3_carnage.offset_set = FALSE;
		h3_carnage.message[0] = 0;
		h3_carnage.said[0] = 0;
		return TRUE;
	case _function_h3_carnage_continue:
		h3_carnage_continue();
		return TRUE;
	case _function_h3_carnage_leave_ask:
		/* (a joined machine's: the question; the host goes on instead) */
		return h3_carnage_network() && !h3_carnage_hosting();
	case _function_h3_carnage_leave:
		h3_carnage.leave_pending = TRUE;
		return TRUE;
	case _function_h3_carnage_players:
		if (!h3_carnage_network())
			return FALSE;
		h3_players.chosen_machine = -1;
		h3_players.message[0] = 0;
		platform_log("menus: the PLAYERS screen opened (the carnage report)");
		return TRUE;
	case _function_h3_carnage_page:
		h3_carnage.page = (short)((h3_carnage.page + 1) % NUMBER_OF_H3_CARNAGE_PAGES);
		return TRUE;
	case _function_h3_carnage_up:
		h3_carnage.offset--;
		return TRUE;
	case _function_h3_carnage_down:
		h3_carnage.offset++;
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
		/* (opened again: a live lobby refused tries again) */
		h3_live_retry();
		/* (back from EDIT GAME TYPES with a game type saved, renamed or
		deleted: a live lobby's game on its GAME as it is now) */
		if (h3_gametypes.changed)
		{
			h3_gametypes.changed = FALSE;
			h3_live_follow();
		}
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
		/* (the lobby hosted on demand: its game now, then its lobby) */
		h3_live_open_now("START GAME");
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
	case _function_h3_maps_back:
		return h3_mp_maps_back();
	case _function_h3_mp_offline_start:
	{
		/* (OFFLINE's START GAME: split screen's SELECT PROFILE next, whose
		Cross starts the local game with the lobby's GAME and MAP,
		h3_mp_local_start; the lobby's map variant played if the game's map
		is its) */
		char const *map = h3_mp.map_index != NONE ? h3_map_paths[h3_mp.map_index] : h3_mp.map_other;

		h3_mp_variant_play(FALSE, map);
		h3_local.asked = TRUE;
		h3_local.started = FALSE;
		h3_local.since = 0;
		platform_log("menus: h3 mp: START GAME on OFFLINE: split screen's SELECT PROFILE, then %s on %s",
			h3_mp_gametype_name(), h3_mp_map_name());
		return TRUE;
	}
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
		/* (Online's PROFILE tab: the delete question over it, the editing
		kept for its return) */
		if (online_custom.deleting)
			return TRUE;
		h3_custom_spartan_hide();
		/* (opened from the Xbox's profile settings: theirs to end) */
		if (player_ui_get_edit_player_profile() && !h3_custom.classic)
			player_ui_end_editing_profile();
		h3_custom.classic = FALSE;
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
	/* (Menu style Halo CE's additions: menus/classic) */
	case _function_ce_pregame_init:
		ce_pregame_initialize();
		return TRUE;
	case _function_ce_variant_next:
		return ce_variant_next();
	case _function_ce_custom:
		return ce_custom_open();
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
	case _function_h3_gametypes_init:
		h3_gametypes_initialize();
		return TRUE;
	/* (A on a row: its options, or on CREATE NEW GAME TYPE the bases - the
	other's handler fails, so that its screen does not open; Triangle the
	bases) */
	case _function_h3_gametypes_choose:
		return h3_gametypes_choose(widget);
	case _function_h3_gametypes_new:
		return h3_gametypes_new(widget);
	case _function_h3_gametypes_page_up:
		h3_gametypes.page = -1;
		return TRUE;
	case _function_h3_gametypes_page_down:
		h3_gametypes.page = 1;
		return TRUE;
	case _function_h3_gametype_edit:
		return h3_gametype_edit();
	case _function_h3_gametype_copy:
		return h3_gametype_ask_name(_typing_gametype_copy);
	case _function_h3_gametype_rename:
		return h3_gametype_ask_name(_typing_gametype_rename);
	/* (the options' DELETE: the question opened over the list, from the
	main loop) */
	case _function_h3_gametype_delete_later:
		if (!h3_gametype_delete_ask(FALSE))
			return FALSE;
		h3_gametypes.open_pending = _h3_gametype_open_delete;
		return TRUE;
	case _function_h3_gametype_delete_ask:
		return h3_gametype_delete_ask(TRUE);
	case _function_h3_gametype_delete:
		h3_gametype_delete();
		return TRUE;
	case _function_h3_gametype_base:
		return h3_gametype_base(widget);
	case _function_h3_gametype_next:
		return h3_gametype_change(widget, 1);
	case _function_h3_gametype_previous:
		return h3_gametype_change(widget, -1);
	case _function_h3_gametype_category:
		return h3_gametype_category(widget);
	case _function_h3_gametype_save:
		return h3_gametype_save();
	/* (the question's SAVE CHANGES: saved, the editor left for the list;
	not saved, back in the editor, which says why) */
	case _function_h3_gametype_save_quit:
		if (h3_gametype_save())
			h3_gametypes.leave_pending = TRUE;
		return TRUE;
	/* (Circle in the editor: with changes, the question; else gone) */
	case _function_h3_gametype_leave_ask:
		return h3_gametype_dirty();
	case _function_h3_gametype_leave:
		if (h3_gametypes.editing)
			platform_log("menus: h3 gametype editor: left, nothing changed");
		h3_gametype_edit_end();
		return TRUE;
	case _function_h3_gametype_discard:
		platform_log("menus: h3 gametype editor: the changes discarded");
		h3_gametype_edit_end();
		h3_gametypes.leave_pending = TRUE;
		return TRUE;
	/* (the Halo 3 dialog: the port's message or question answered) */
	case _function_h3_dialog_ok:
	case _function_h3_dialog_yes:
	case _function_h3_dialog_no:
		h3_dialog_answer(function_index);
		return TRUE;
	/* (the Halo 3 scoreboard: halo3/scoreboard.xml) */
	case _function_h3_scoreboard_act:
		return h3_scoreboard_act(widget);
	case _function_h3_scoreboard_option:
		h3_scoreboard_option(widget);
		return TRUE;
	case _function_h3_scoreboard_close:
		platform_log("menus: h3 scoreboard: Circle: closed");
		return TRUE;
	case _function_h3_scoreboard_page_up:
		h3_scoreboard.page = -1;
		return TRUE;
	case _function_h3_scoreboard_page_down:
		h3_scoreboard.page = 1;
		return TRUE;
	case _function_online_custom_tab_next:
		online_custom_tab_step(1);
		return TRUE;
	case _function_online_custom_tab_previous:
		online_custom_tab_step(-1);
		return TRUE;
	case _function_online_switch_open:
		online_switch.open = TRUE;
		online_switch.said = NONE;
		return TRUE;
	case _function_online_switch_close:
		if (online_switch.open)
			platform_log("menus: online switch lobby: closed");
		online_switch.open = FALSE;
		return TRUE;
	/* (B on an Online lobby: the switcher closed when it is open - the
	handler failing, so the lobby stays -, else the lobby left as the Halo
	3 lobby's B leaves it) */
	case _function_online_lobby_leave:
		if (online_switch.open)
		{
			online_switch.open = FALSE;
			platform_log("menus: online switch lobby: closed (B)");
			return FALSE;
		}
		h3_live_close("the host left the lobby");
		return TRUE;
	/* (SWITCH LOBBY's rows: the lobby's live game closed - the party is
	kept -, the other lobby opened in its place at the frame's update) */
	case _function_online_switch_multiplayer:
	case _function_online_switch_forge:
	case _function_online_switch_campaign:
		online_switch.open = FALSE;
		h3_live_close("the host switched lobby");
		online_menus.switch_to = function_index == _function_online_switch_multiplayer ?
			"ui\\shell\\main_menu\\multiplayer_type_select\\multiplayer_type_select_screen" :
			function_index == _function_online_switch_forge ? "pc\\h3\\forge\\lobby" :
			h3_profile_play_current(event ? event->controller_index : NONE) ?
			"ui\\shell\\main_menu\\solo_level_select\\solo_level_select_screen" :
			"ui\\shell\\main_menu\\load_campaign_player_profile";
		return TRUE;
	case _function_h3_profile_current:
		return h3_profile_play_current(event ? event->controller_index : NONE);
	case _function_online_profile_choose:
		return online_profile_choose(widget);
	case _function_online_profile_delete_ask:
		return online_profile_delete_ask(widget);
	case _function_online_browser_network_next:
	case _function_online_browser_network_previous:
		online_browser_network_step(function_index == _function_online_browser_network_next ? 1 : -1);
		return TRUE;
	case _function_online_browser_filter_next:
	case _function_online_browser_filter_previous:
		online_browser_filter_step(function_index == _function_online_browser_filter_next ? 1 : -1);
		return TRUE;
	case _function_online_host_multiplayer:
		return online_host_open(_online_host_multiplayer);
	case _function_online_host_forge:
		return online_host_open(_online_host_forge);
	case _function_online_host_campaign:
		return online_host_open(_online_host_campaign);
	case _function_online_host_next:
		return online_host_change(widget, 1);
	case _function_online_host_previous:
		return online_host_change(widget, -1);
	case _function_online_host_edit:
		return online_host_edit(widget);
	case _function_online_exit:
		platform_log("menus: online EXIT: the game closes");
		exit(EXIT_SUCCESS);
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
		/* (the Forge lobby's NETWORK picker: OFFLINE is Forge alone on this Vita, not the
		multiplayer lobby's split screen - the list's extra words) */
		if (h3_forge.lobby_last)
		{
			struct widget_instance *words = named(screen_of(widget), "network_text");

			if (words && words->type == _ui_widget_type_text_box &&
				words->parameters.text_box.string_list_index == _h3_network_offline)
				string_show(words, NUMBER_OF_H3_NETWORKS);
		}
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
	case _input_h3_dialog:
		h3_dialog_update(widget);
		break;
	case _input_ce_profile:
		ce_profile_description_update(widget);
		break;
	case _input_ce_lobby:
		if (widget->type == _ui_widget_type_column_list)
			ce_lobby_update(widget);
		break;
	case _input_h3_scoreboard:
		h3_scoreboard_update(widget);
		break;
	case _input_h3_scoreboard_options:
		if (widget->type == _ui_widget_type_column_list)
			h3_scoreboard_options_update(widget);
		break;
	case _input_online_version:
		online_main_update(widget);
		break;
	case _input_online_custom:
		if (widget->type == _ui_widget_type_column_list)
			online_custom_update(widget);
		break;
	case _input_online_switch:
		if (widget->type == _ui_widget_type_column_list)
			online_switch_update(widget);
		break;
	case _input_online_lobby:
		online_lobby_update(screen_of(widget));
		break;
	case _input_online_host:
		online_host_update(widget);
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
	case _input_h3_gametype_list:
		if (widget->type == _ui_widget_type_column_list)
			h3_gametypes_update(widget);
		break;
	case _input_h3_gametype_actions:
		if (widget->type == _ui_widget_type_column_list)
			h3_gametype_actions_update(widget);
		break;
	case _input_h3_gametype_question:
		h3_gametype_question_update(widget);
		break;
	case _input_h3_gametype_bases:
		if (widget->type == _ui_widget_type_column_list)
			h3_gametype_bases_update(widget);
		break;
	case _input_h3_gametype_editor:
		if (widget->type == _ui_widget_type_column_list)
			h3_gametype_editor_update(widget);
		break;
	case _input_h3_gametype_options:
		if (widget->type == _ui_widget_type_column_list)
			h3_gametype_options_update(widget);
		break;
	case _input_h3_carnage:
		h3_carnage_update(widget);
		break;
	default:
		break;
	}
}

/* (the settings panel's Forge page, Network on: system_link_shortcut.c, for
the Halo CE menus too) a network Forge game of that map and map variant
hosted once the System Link screen's profile is chosen, its lobby (the
game's) next; FALSE with why if the map cannot be */
/* ---- Menu style Halo CE's additions (menus/classic: the Xbox screens with
the port's rows, menu_tags.c classic_patch) */

/* (LOBBY OPTIONS' rows, as OpenCE's Multiplayer screen's) */
#define CE_LOBBY_ROW_SPACING 33

/* the Xbox's pregame (connected_pregame_screen) of a game this machine
hosts: the map variant its game is played with, chosen on its LOBBY
OPTIONS' MAP VARIANT (menus/classic/pregame.xml) among its map's (none
first), as the Halo 3 lobby's MAP picker chooses one - forge_play_host_begin,
the joiners asked to play it */
static struct
{
	/* the variant chosen (its file and title; none: "") and whether this
	pregame hosts it (forge_play_host_begin), the map it was chosen for */
	char file[FORGE_FILE_NAME_SIZE];
	char title[FORGE_FILE_TITLE_SIZE];
	boolean hosted;
	short map;
	char note[160];
	char said[256];
} ce_pregame = { "", "", FALSE, NONE, "", "" };

/* the game hosted here, in its lobby (else NULL) */
static struct network_game_server *ce_hosted_server(void)
{
	struct network_game_server *server = global_network_game_server_get();

	return server && network_game_server_port_in_lobby(server) ? server : NULL;
}

/* the lobby map (forge.c's) of the game hosted, NONE for one not in it */
static short ce_hosted_map(void)
{
	char const *map = main_get_multiplayer_map_name();
	short index;

	for (index = 0; map && map[0] && index < forge_map_count(); index++)
	{
		char const *path = forge_map_path(index);

		if (path && !_stricmp(path, map))
			return index;
	}
	return NONE;
}

/* (the pregame's created: port ce pregame init) a variant hosted before -
the game since ended, another map - forgotten */
static void ce_pregame_initialize(void)
{
	ce_pregame.note[0] = 0;
	if (ce_pregame.hosted && (!forge_net_hosting() || !ce_hosted_server() || ce_hosted_map() != ce_pregame.map))
		ce_pregame.hosted = FALSE;
	if (!ce_pregame.hosted)
		ce_pregame.file[0] = ce_pregame.title[0] = 0;
}

/* (port ce variant next: LOBBY OPTIONS' MAP VARIANT) the hosted map's next
map variant, after the last none again */
static boolean ce_variant_next(void)
{
	short map = ce_hosted_map();
	char why[FORGE_STORE_WHY_SIZE];
	int index, next = 0;

	ce_pregame.note[0] = 0;
	if (!ce_hosted_server() || map == NONE)
		return FALSE;
	/* (a network Forge game hosted: its own variant, not this) */
	if (forge_net_hosting() && !ce_pregame.hosted)
		return FALSE;
	h3_variants_list(_variants_lobby, forge_map_short(map), TRUE);
	/* (the one after the chosen one; none after the last) */
	for (index = 0; index < h3_variants.count; index++)
	{
		if (!h3_variants.entries[index].damaged && ce_pregame.file[0] &&
			!strcmp(h3_variants.entries[index].file_name, ce_pregame.file))
		{
			next = index + 1;
			break;
		}
	}
	while (next < h3_variants.count && h3_variants.entries[next].damaged)
		next++;
	if (next >= h3_variants.count || !h3_variants.count)
	{
		if (ce_pregame.hosted)
			forge_net_host_end();
		ce_pregame.hosted = FALSE;
		ce_pregame.file[0] = ce_pregame.title[0] = 0;
		platform_log("menus: ce pregame: no map variant: %s as it is", forge_map_name(map));
		return TRUE;
	}
	/* (the one played before let go first: one variant's session at a time) */
	if (ce_pregame.hosted)
		forge_net_host_end();
	ce_pregame.hosted = FALSE;
	if (!forge_play_host_begin(forge_map_path(map), h3_variants.entries[next].file_name, why, (int)sizeof(why)))
	{
		snprintf(ce_pregame.note, sizeof(ce_pregame.note), "%s", why);
		/* (kept, so that the next press goes on past it) */
		snprintf(ce_pregame.file, sizeof(ce_pregame.file), "%s", h3_variants.entries[next].file_name);
		ce_pregame.title[0] = 0;
		platform_log("menus: ce pregame: the map variant %s can't be played: %s", h3_variants.entries[next].title, why);
		return TRUE;
	}
	snprintf(ce_pregame.file, sizeof(ce_pregame.file), "%s", h3_variants.entries[next].file_name);
	snprintf(ce_pregame.title, sizeof(ce_pregame.title), "%s", h3_variants.entries[next].title);
	ce_pregame.hosted = TRUE;
	ce_pregame.map = map;
	platform_log("menus: ce pregame: the map variant %s is played on %s", ce_pregame.title, forge_map_name(map));
	return TRUE;
}

/* (port ce lobby: the pregame's LOBBY OPTIONS, its list) PLAYERS for
everyone; the host's INVITE FRIENDS (a game hosted online) and MAP VARIANT
(a map of the lobby's, not a network Forge game), a hidden row's place taken
by the rows under it; the description's panel: the hosted map's picture and
name, the map variant played, the focused row's words */
static void ce_lobby_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *row, *child, *found;
	struct network_game_server *server = ce_hosted_server();
	short map = ce_hosted_map();
	boolean invite = server && internet_play() && !network_game_is_splitscreen_local();
	boolean variant = server && map != NONE && (!forge_net_hosting() || ce_pregame.hosted);
	char text[256];
	short index = 0, shown = 0, base;

	if ((row = named(screen, "op_invite")) != NULL)
	{
		widget_instance_set_visibility_recursive(row, invite);
		row->disabled = !invite;
	}
	if ((row = named(screen, "op_variant")) != NULL)
	{
		widget_instance_set_visibility_recursive(row, variant);
		row->disabled = !variant;
	}
	/* (the rows shown one under the other, as the list's first stands) */
	base = list->child ? list->child->vertical_offset : 0;
	for (child = list->child; child; child = child->next)
	{
		if (child->visible)
			child->vertical_offset = (short)(base + shown++ * CE_LOBBY_ROW_SPACING);
	}
	if ((found = named(screen, "map_picture")) != NULL)
	{
		widget_instance_set_visibility_recursive(found, map != NONE);
		if (map != NONE)
			found->animation.current_frame_index = map;
	}
	text_show(named(screen, "map_name"), map != NONE ? T(forge_map_name(map)) : "");
	if (variant)
	{
		snprintf(text, sizeof(text), "%s:\r\n%s", T("MAP VARIANT"), ce_pregame.note[0] ? ce_pregame.note :
			ce_pregame.title[0] ? ce_pregame.title : T("NONE"));
		text_show(named(screen, "variant_value"), text);
	}
	else
		text_show(named(screen, "variant_value"), "");
	/* (a game hosted online: its code and PUBLIC or PRIVATE, in the panel under the
	words: its code only once the brokers have it, as the Halo 3 lobby's) */
	text[0] = 0;
	if (server && config_boolean("network.online") && p2p_running())
	{
		char code[32], reach[160];
		int password = 0;

		code[0] = 0;
		p2p_hosting_code(code, (int)sizeof(code));
		if (p2p_lobby_brokers_status(reach, (int)sizeof(reach)) != P2P_LOBBY_BROWSE_EMPTY ||
			p2p_lobby_hosting_status(reach, (int)sizeof(reach)) == P2P_LOBBY_HOSTING_UNREACHABLE)
			code[0] = 0;
		if (code[0])
			snprintf(text, sizeof(text), "%s %s  %s", T("CODE:"), code,
				p2p_lobby_hosting_listed(&password) ? T("PUBLIC") : T("PRIVATE"));
	}
	text_show(named(screen, "code_value"), text);
	for (child = list->child; child && child != list->focused_child; child = child->next)
		index++;
	string_show(named(screen, "lobby_help"), index);
	snprintf(text, sizeof(text), "PLAYERS%s%s%s%s", invite ? ", INVITE FRIENDS" : "", variant ? ", MAP VARIANT " : "",
		variant ? (ce_pregame.title[0] ? ce_pregame.title : "NONE") : "", ce_pregame.note[0] ? " (not played)" : "");
	if (strcmp(ce_pregame.said, text))
	{
		snprintf(ce_pregame.said, sizeof(ce_pregame.said), "%s", text);
		platform_log("menus: ce lobby options: %s", text);
	}
}

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

	/* (Menu style Online: CUSTOMIZATION opens on the current profile, its
	PROFILE tab for the others - no SELECT PROFILE before it -; SELECT
	PROFILE only when no profile can be had) */
	if (tag_index != NONE && halo_menu_style_online() &&
		tag_index == tag_loaded(UI_WIDGET_DEFINITION_TAG,
			"ui\\shell\\main_menu\\settings_select\\player_setup\\player_profile_select_screen"))
	{
		long editor = tag_loaded(UI_WIDGET_DEFINITION_TAG, "pc\\h3\\custom\\screen");

		if (editor != NONE && online_custom_begin_current())
			return editor;
		return tag_index;
	}
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
/* ---- the mouse on the ElDewrito screens (source/interface/ui_widget.c's
pointer: the rows are the d-pad's items already, clicked as A): the parts
the screens pick themselves - CUSTOMIZATION's tabs and its colours' grid,
the SERVER BROWSER's network tabs and filters, the L and R of the hints -
and a focused row's value arrows */
static short mouse_number_after(char const *name, char const *prefix)
{
	size_t length = strlen(prefix);

	if (!name || strncmp(name, prefix, length) || name[length] < '0' || name[length] > '9')
		return NONE;
	return (short)atoi(name + length);
}

static boolean mouse_on_screen(struct widget_instance *widget, char const *screen_tag)
{
	struct widget_instance *screen = screen_of(widget);
	char const *name = screen ? tag_get_name(screen->definition_tag_index) : NULL;

	return name && !strcmp(name, screen_tag);
}

int menu_functions_mouse_kind(struct widget_instance *widget)
{
	char const *name = widget ? widget->name : NULL;

	if (!name || !halo_menu_style_online())
		return 0;
	if (!strcmp(name, "arrow_left") || !strcmp(name, "arrow_right"))
	{
		/* (the focused row's: the arrows show on it alone) */
		if (!widget->parent || !widget->parent->parent || widget->parent->parent->focused_child != widget->parent)
			return 0;
		return !strcmp(name, "arrow_left") ? 2 : 3;
	}
	if (mouse_on_screen(widget, "pc\\online\\custom\\screen"))
	{
		if (mouse_number_after(name, "tab_label_") != NONE || mouse_number_after(name, "tab_") != NONE ||
			(mouse_number_after(name, "cell_") != NONE && online_custom.tab == _online_custom_tab_colors) ||
			!strcmp(name, "l_butn") || !strcmp(name, "r_butn"))
		{
			return 1;
		}
	}
	if (mouse_on_screen(widget, "pc\\online\\lan\\screen"))
	{
		if (mouse_number_after(name, "net_tab_") != NONE || mouse_number_after(name, "net_label_") != NONE ||
			mouse_number_after(name, "filter_") != NONE || !strcmp(name, "l_butn") || !strcmp(name, "r_butn"))
		{
			return 1;
		}
	}
	return 0;
}

void menu_functions_mouse_click(struct widget_instance *widget)
{
	char const *name = widget ? widget->name : NULL;
	short number;

	if (!name)
		return;
	if (mouse_on_screen(widget, "pc\\online\\custom\\screen"))
	{
		struct player_profile *profile = player_ui_get_edit_player_profile();

		if ((number = mouse_number_after(name, "tab_label_")) != NONE || (number = mouse_number_after(name, "tab_")) != NONE)
			online_custom_tab_step(number - online_custom.tab);
		else if (!strcmp(name, "l_butn") || !strcmp(name, "r_butn"))
			online_custom_tab_step(!strcmp(name, "l_butn") ? -1 : 1);
		else if ((number = mouse_number_after(name, "cell_")) != NONE && profile && number < H3_SWATCH_FRAMES)
		{
			/* (a colour of the grid: cell_0 RANDOM, then the game's) */
			profile->primary_color_index = (short)(number ? number - 1 : NONE);
			platform_log("menus: online customize: colour %d from the grid", profile->primary_color_index);
		}
		return;
	}
	if (mouse_on_screen(widget, "pc\\online\\lan\\screen"))
	{
		short network = h3_mp_network_of_connection(), at = 0, index;

		for (index = 0; index < (short)NUMBEROF(online_browser_networks); index++)
		{
			if (online_browser_networks[index] == network)
				at = index;
		}
		if ((number = mouse_number_after(name, "net_label_")) != NONE || (number = mouse_number_after(name, "net_tab_")) != NONE)
		{
			if (number != at)
				online_browser_network_step(number - at);
		}
		else if (!strcmp(name, "l_butn") || !strcmp(name, "r_butn"))
			online_browser_network_step(!strcmp(name, "l_butn") ? -1 : 1);
		else if ((number = mouse_number_after(name, "filter_")) != NONE && number != online_browser.filter)
			online_browser_filter_step(number - online_browser.filter);
	}
}

/* ---- the Menu style changed (the settings panel's Game, Menus, Menu style;
config.toml's menus.style): live - at the main menu, the menus closed and
the main menu opened again, in the new style (menu_tags.c reads the style
as each screen opens; the live backdrop comes or goes with it,
menu_backdrop.c; a lobby's characters let go with the scenes). A lobby's
network game is closed first (the party kept). In a game (no main menu) it
takes at the next main menu, as before. HALO_TEST_MENU_STYLE_SWITCH
("<ms>:<style>,...", never a release): the tests' changes, that long after
the main menu is first up */
static struct
{
	char style[16];
	boolean known;
	unsigned long first_up;
	int test_next;
} menu_style_watch;

/* (d3d8_gl.c screen_mode_choose) the narrowest the screen is drawn while
the menus are up: Menu style ElDewrito's screens are laid out over 800
columns (80 each side of the 640, as a 16:9 display shows them); on a
narrower display (4:3) they are drawn that wide, letterboxed, nothing cut
off. 640 otherwise (a game, the other styles) */
int halo_menus_minimum_width(void)
{
	return halo_menu_style_online() && halo_main_menu_loaded() ? 800 : 640;
}

void ui_widgets_close_all(void);

static void menu_style_current(char *style, int size)
{
	char const *value = getenv("HALO_MENU_STYLE");

	snprintf(style, (size_t)size, "%s", !halo_menu_style_halo3() ? "classic" : value && !strcmp(value, "online") ?
		"online" : "halo3");
}

static void menu_style_update(boolean main_menu_loaded)
{
	char style[16];
	long main_menu;

	if (!main_menu_loaded)
		return;
#ifndef HALO_RELEASE
	{
		char const *switches = getenv("HALO_TEST_MENU_STYLE_SWITCH");

		if (!menu_style_watch.first_up)
			menu_style_watch.first_up = system_milliseconds();
		if (switches)
		{
			char const *cursor = switches;
			int index = 0;

			while (cursor && *cursor)
			{
				unsigned long at = strtoul(cursor, NULL, 10);
				char const *colon = strchr(cursor, ':'), *comma = strchr(cursor, ',');
				char value[16];

				if (!colon)
					break;
				snprintf(value, sizeof(value), "%.*s", (int)(comma ? comma - colon - 1 : (long)strlen(colon + 1)), colon + 1);
				if (index == menu_style_watch.test_next && system_milliseconds() - menu_style_watch.first_up >= at)
				{
					setenv("HALO_MENU_STYLE", value, 1);
					menu_style_watch.test_next++;
					platform_log("menus: test: Menu style %s", value);
					break;
				}
				index++;
				cursor = comma ? comma + 1 : NULL;
			}
		}
	}
#endif
	menu_style_current(style, (int)sizeof(style));
	if (!menu_style_watch.known)
	{
		menu_style_watch.known = TRUE;
		snprintf(menu_style_watch.style, sizeof(menu_style_watch.style), "%s", style);
		return;
	}
	if (!strcmp(style, menu_style_watch.style))
		return;
	platform_log("menus: Menu style %s -> %s: the main menu again, in it", menu_style_watch.style, style);
	snprintf(menu_style_watch.style, sizeof(menu_style_watch.style), "%s", style);
	/* (a lobby's game closed first; the lobby's characters let go) */
	h3_live_close("the menu style changed");
	menu_scene_objects_want(NULL, NULL, FALSE, 0.f, 0.f, 0.f, 0.f, 0.f);
	main_menu = tag_loaded(UI_WIDGET_DEFINITION_TAG, "ui\\shell\\main_menu\\main_menu");
	if (main_menu == NONE)
		return;
	ui_widgets_close_all();
	if (!ui_widget_load_by_name_or_tag(NULL, main_menu, NULL, NONE, NONE, NONE, NONE))
		platform_log("menus: Menu style changed: the main menu did not open");
}

void pc_menu_functions_update(
	boolean main_menu_loaded)
{
	menu_style_update(main_menu_loaded);
	h3_dialog_port_update();
	h3_live_update(main_menu_loaded);
	h3_mp_local_update(main_menu_loaded);
	h3_carnage_frame(main_menu_loaded);
	h3_scoreboard_frame(main_menu_loaded);
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
	/* (FRIEND CODE BACKUP's question, over FRIENDS) */
	if (main_menu_loaded && menu_functions.open_friends_question)
	{
		long question = menu_tags_ingame_screen("h3/friends/options");
		struct widget_instance *active = ui_widget_port_active_screen(0);

		menu_functions.open_friends_question = FALSE;
		if (question == NONE || !active || !ui_widget_load_by_name_or_tag(NULL, question, NULL,
			active->local_player_index, active->definition_tag_index, NONE, NONE))
		{
			platform_log("menus: h3 friends: the backup's question did not open");
			h3_backup_stop(T("That did not open: try again"));
		}
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
		menu_functions.open_friends_question = FALSE;
		menu_functions.open_game_options = FALSE;
		menu_functions.open_system_link = FALSE;
		menu_functions.open_system_link_list = FALSE;
		menu_functions.browser_profile_pending = FALSE;
		menu_functions.browser_profile_chosen = FALSE;
		menu_functions.open_password = FALSE;
		menu_functions.create_pending = FALSE;
		h3_profiles.open_pending = FALSE;
		h3_gametypes.open_pending = _h3_gametype_open_none;
		h3_gametypes.leave_pending = FALSE;
		menu_functions.browse_lan_pending = FALSE;
		h3_adhoc.open_then = FALSE;
		h3_adhoc.then = _h3_adhoc_then_none;
		/* (ui.map's scripts start again with it: ring_loop awake; its
		settings point read again, nothing kept in it, the ring not flown) */
		h3_scenes.ring_asleep = FALSE;
		h3_scenes.ring_kept = FALSE;
		h3_scenes.ring_flown = FALSE;
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
	online_menus_update();
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
	/* (GAME TYPES: the editor or the delete question over the list, the
	editor closed under its question) */
	if (h3_gametypes.open_pending != _h3_gametype_open_none || h3_gametypes.leave_pending)
		h3_gametypes_main_loop();
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
