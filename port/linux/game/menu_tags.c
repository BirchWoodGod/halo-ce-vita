/*
MENU_TAGS.C

OpenCE's multiplayer screens (its PC-menu Multiplayer screen, Server
Browser, Create Game > Internet's Server Setup, the password and join-code
screens), as XML in the menus folder (halo_menus.h, read by
port/linux/src/menu_files.c), built into the tags the game's widgets are made
of when ui.map loads (scenario_tags_load), and added to its tag table: a
widget definition ('DeLa') for each <widget>, a unicode string list
('ustr') for each <strings> and each widget's own text, and a bitmap group
('bitm') for each <bitmap>. The game's widgets then run them as their own.
From OpenCE's menu_tags.c (OpenCommunityEdition/OpenCE, CC0, c9ee319a and
later), which builds all of the PC version's menus; here only these screens,
and their pictures and text are the player's own:

- A <bitmap resource="ui\..."> is the bitmap of that path in the Halo PC
  (Custom Edition) bitmaps.map in the maps folder - Bungie's PC art, which
  is never shipped - and a <strings resource="ui\..."> the string list of
  that path in its loc.map. A <frame png=...> is a PNG of the menus folder
  (OpenCE's own art, CC0), a <frame map=...> a frame of ui.map's.
- They are added only when both files are there, are Custom Edition
  resource maps and hold every path the files name (menu_tags_loaded): else
  nothing is added, and the Xbox's menus are as they were.
- The pictures and the loc.map text are read the first time the screens
  open (menu_tags_screen), not as ui.map loads, which reads only the XML:
  the main menu loads no slower. If they cannot be read then, the Xbox's
  Multiplayer screen opens as ever.

The screens stand for the Xbox's Multiplayer screen
(ui\shell\main_menu\multiplayer_type_select\multiplayer_type_select_screen):
each time the game opens that, ours opens instead (menu_tags_screen, called
by ui_widget_load_by_name_or_tag), with the history the game gives it.
Their own functions are menu_functions.c's.

The tag table cannot grow where it is (the tags' names follow it), so it
is copied, with ours after it; every existing tag keeps its index. All of it
is let go in scenario_tags_unload, before the next map's tags load.
*/

#include "cseries.h"
#include "cseries_windows.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmaps_internal.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"
#include "text/font_group.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "memory/zlib/zlib.h"
#include "cache_file_formats.h"

#include "halo_menus.h"
#include "../src/lang.h"

#include <stdlib.h>
#include <string.h>

/* the platform layer's (port/linux/src) */
void platform_log(char const *format, ...);
void platform_heap_usage(unsigned long *in_use, unsigned long *capacity);
int platform_halo_pc_resource_map(const char *maps_directory, const char *name, char *path, unsigned long path_size);
void platform_contiguous_usage(unsigned long *used, unsigned long *free_bytes);
unsigned long long vita_host_time_us(void);

/* cache_files.c's (port) */
void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances, long count);
char const *ui_widget_event_handler_function_name(long function_index);

void menu_tags_preload(char const *map_name);
void menu_tags_loaded(char const *map_name);
void menu_tags_unloaded(void);
long menu_tags_screen(long tag_index);
boolean menu_tags_bitmap_fits_widget(long bitmap_tag_index);
boolean pc_menu_tag(long tag_index);
char const *pc_menu_function_name(long function_index);
char const *pc_menu_game_data_input_name(long function_index);
boolean menu_tags_png_texture(char const *png, long width, long height, long smallest, struct bitmap_data *bitmap);
/* (system_link_shortcut.c's, for the settings panel) */
extern volatile int halo_pc_menus_state;
/* menu_functions.c's: a screen of the game's that the menus' flows open
another in place of (ours or the game's), or the tag itself */
long pc_menu_functions_screen(long tag_index);
/* (Menu style Halo 3) a network game's SELECT PROFILE with the profile
chosen before: the screen after it, the profile set; else the tag */
long pc_menu_functions_profile_skip(long tag_index);
/* forge.c's: whether a Forge game is played (its Start opens the Forge menu) */
boolean forge_game_active(void);
long menu_tags_ingame_screen(char const *name);
static void menu_tags_dump(long tag_index, int depth);

/* ---------- constants */

#define PC_MENU_TAG_PREFIX "pc\\"
#define XBOX_MULTIPLAYER_SCREEN "ui\\shell\\main_menu\\multiplayer_type_select\\multiplayer_type_select_screen"
/* a level's pause screen, and the Forge menu that stands for it in a Forge
game (halo3/forge.xml, whatever the Menu style) */
#define XBOX_PAUSE_SCREEN "ui\\shell\\solo_game\\pause_game\\pause_game"
#define FORGE_PAUSE_SCREEN "h3/forge/pause/screen"
/* a network game's pause screens (ui_widget.c ui_check_for_pause_game opens
one of them by the machine's players): a network Forge game's Start opens
the Forge menu in their place too, the host's and every joiner's, whatever
the Menu style (insider.21: the Xbox's multiplayer pause opened, and with
forge.c's own the Forge menu was closed by it at once) */
static char const *const network_pause_screens[] =
{
	"ui\\shell\\multiplayer_game\\pause_game\\1p_pause_game",
	"ui\\shell\\multiplayer_game\\pause_game\\2p_pause_game",
	"ui\\shell\\multiplayer_game\\pause_game\\4p_pause_game",
};
/* the Halo 3 multiplayer pause (halo3/mppause.xml): never in a Forge game,
whose Start opens the Forge menu (forge.c) */
#define H3_MULTIPLAYER_PAUSE_SCREEN "h3/mppause/screen"
#define UI_WIDGET_DEFINITION_TAG 'DeLa'
#define UNICODE_STRING_LIST_TAG 'ustr'
#define FONT_TAG 'font'
#define SOUND_TAG 'snd!'
#define MAXIMUM_STRINGS 64
/* (a bitmap of the player's: no larger, and no more frames, than the PC
version's own menus have) */
#define MAXIMUM_RESOURCE_FRAMES 64
#define MAXIMUM_RESOURCE_BYTES (4 * 1024 * 1024)
#define MAXIMUM_PNG_BYTES (512 * 1024)
/* the Halo 3 set's screens that stand for an Xbox one (main menu, campaign,
their pickers...) */
#define MAXIMUM_H3_SCREENS 64
/* the Xbox screens' widgets Menu style Halo CE adds rows to (classic_patch=) */
#define MAXIMUM_CLASSIC_PATCHES 32
/* (Menu style Online: the menus/online widgets that stand for a Halo 3 set's
widget of the same name under h3/) */
#define MAXIMUM_ONLINE_SCREENS 512

/* the layout of a bitmap tag and a string list tag held by a Custom Edition
resource map (the item is the whole tag, its addresses counting from the
item's start: cache_file_formats.c resource_tag_load) */
#define RESOURCE_BITMAP_BITMAPS_OFFSET 0x60
#define RESOURCE_BITMAP_DATA_BYTES 0x30
#define RESOURCE_STRING_ENTRY_BYTES 0x14

enum
{
	_bitmap_type_2d = 0,
	_bitmap_format_a8r8g8b8 = 11,
	_bitmap_format_dxt1 = 14,
	_bitmap_format_dxt5 = 16,
	_bitmap_has_power_of_two_dimensions_bit = 0,
	_bitmap_compressed_bit = 1,
};

enum
{
	_event_handler_close_current_widget_bit = 0,
	_event_handler_close_other_widget_bit,
	_event_handler_close_all_widgets_bit,
	_event_handler_open_widget_bit,
	_event_handler_reload_self_bit,
	_event_handler_reload_other_widget_bit,
	_event_handler_give_focus_to_widget_bit,
	_event_handler_run_function_bit,
	_event_handler_replace_self_with_widget_bit,
	_event_handler_go_back_to_previous_widget_bit,
	_event_handler_run_scenario_script_bit,
	_event_handler_try_to_branch_on_failure_bit,
};

enum
{
	_widget_type_container,
	_widget_type_text_box,
	_widget_type_spinner_list,
	_widget_type_column_list,
};

enum
{
	_list_items_generated_from_string_list_tag_bit = 1,
};

enum
{
	_child_widget_use_custom_controller_index_bit = 0,
};

/* ---------- structures */

struct cache_file_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

typedef char verify_cache_file_tag_instance_size[
	sizeof(struct cache_file_tag_instance) == 0x20 ? 1 : -1];

/* the widget definition, as ui_widget.c has it */
struct ui_widget_event_handler_reference
{
	long flags;
	short event_type;
	short function;
	struct tag_reference widget_tag;
	struct tag_reference sound_effect;
	char script[32];
};

struct ui_widget_child_reference
{
	struct tag_reference widget_tag;
	char name[32];
	long flags;
	short custom_controller_index;
	short vertical_offset;
	short horizontal_offset;
	byte unknown03A[0x50 - 0x3A];
};

struct ui_widget_conditional_reference
{
	struct tag_reference widget_tag;
	char name[32];
	long flags;
	short custom_controller_index;
	byte unknown036[0x50 - 0x36];
};

struct ui_widget_search_and_replace_reference
{
	char search_string[32];
	short replace_function;
};

struct ui_widget_game_data_input_reference
{
	short function;
	byte unknown002[0x24 - 0x02];
};

struct menu_widget_definition
{
	short type;
	short controller_index;
	char name[32];
	rectangle2d bounds;
	long flags;
	long milliseconds_to_auto_close;
	long auto_close_fade_time;
	struct tag_reference background_bitmap;
	struct tag_block game_data_inputs;
	struct tag_block event_handlers;
	struct tag_block search_and_replace_functions;
	byte unknown06C[0xEC - 0x6C];
	struct tag_reference text_label_string_list;
	struct tag_reference text_font;
	real_argb_color text_color;
	short justification;
	word text_box_flags;
	byte unknown120[0x12E - 0x120];
	short string_list_index;
	short horizontal_offset;
	short vertical_offset;
	byte unknown134[0x150 - 0x134];
	long list_flags;
	struct tag_reference list_header_bitmap;
	struct tag_reference list_footer_bitmap;
	rectangle2d list_header_bounds;
	rectangle2d list_footer_bounds;
	byte unknown184[0x1A4 - 0x184];
	struct tag_reference extended_description_widget;
	byte unknown1B4[0x2D4 - 0x1B4];
	struct tag_block conditional_widgets;
	byte unknown2E0[0x3E0 - 0x2E0];
	struct tag_block child_widgets;
};

typedef char verify_ui_widget_event_handler_reference_size[
	sizeof(struct ui_widget_event_handler_reference) == 0x48 ? 1 : -1];
typedef char verify_ui_widget_child_reference_size[
	sizeof(struct ui_widget_child_reference) == 0x50 ? 1 : -1];
typedef char verify_ui_widget_conditional_reference_size[
	sizeof(struct ui_widget_conditional_reference) == 0x50 ? 1 : -1];
typedef char verify_ui_widget_search_and_replace_reference_size[
	sizeof(struct ui_widget_search_and_replace_reference) == 0x22 ? 1 : -1];
typedef char verify_ui_widget_game_data_input_reference_size[
	sizeof(struct ui_widget_game_data_input_reference) == 0x24 ? 1 : -1];
typedef char verify_ui_widget_definition_text_color_offset[
	offsetof(struct menu_widget_definition, text_color) == 0x10C ? 1 : -1];
typedef char verify_ui_widget_definition_list_flags_offset[
	offsetof(struct menu_widget_definition, list_flags) == 0x150 ? 1 : -1];
typedef char verify_ui_widget_definition_conditional_widgets_offset[
	offsetof(struct menu_widget_definition, conditional_widgets) == 0x2D4 ? 1 : -1];
typedef char verify_ui_widget_definition_size[
	sizeof(struct menu_widget_definition) == 0x3EC ? 1 : -1];
typedef char verify_bitmap_data_size[sizeof(struct bitmap_data) == RESOURCE_BITMAP_DATA_BYTES ? 1 : -1];

/* a bitmap or string list of the player's, filled the first time the
screens open (menu_tags_art_load) */
struct menu_resource
{
	char const *path;
	long tag_index;
	char const *file;
	long line;
};

/* a frame read from a PNG of the menus folder, made a texture then too */
struct menu_png_frame
{
	struct bitmap_data *bitmap;
	char const *png;
	char const *file;
	long line;
};

/* ---------- globals */

/* the names the files may use, by their values */
static char const *const event_names[] =
{
	"a", "b", "x", "y", "black", "white", "left_trigger", "right_trigger",
	"up", "down", "left", "right", "start", "back", "left_thumb", "right_thumb",
	"stick_up", "stick_down", "stick_left", "stick_right",
	"right_stick_up", "right_stick_down", "right_stick_left", "right_stick_right",
	"created", "deleted",
};

static char const *const widget_flag_names[] =
{
	"pass_unhandled_to_focused_child", "pause_game", "flash_bitmap", "up_down_tabs_children",
	"left_right_tabs_children", "up_down_tabs_items", "left_right_tabs_items", "no_focused_child",
	"pass_unhandled_to_all_children", "render_any_controller", "pass_handled_to_all_children",
	"main_menu_if_no_history", "tag_controller_index", "nifty_fx", "no_history",
};

static char const *const widget_type_names[] = { "container", "text", "spinner", "column_list" };
static char const *const list_flag_names[] = { "items_in_code", "items_from_strings", "one_tooltip", "single_preview" };
static char const *const text_flag_names[] = { "editable", "password", "flashing", "no_focus_test" };
static char const *const align_names[] = { "left", "right", "center" };
static char const *const replace_names[] = { "none", "controller" };

/* the game data functions, as the tags name them
(ui_widget_game_data_input_functions.c) */
static char const *const game_data_input_names[] =
{
	"NULL", "player settings menu update desc", "unused", "playlist settings menu update desc",
	"gametype select menu update desc", "multiplayer type menu update desc", "solo level select update",
	"difficulty menu update desc", "build number textbox only", "server list update",
	"network pregame status update", "splitscreen pregame status update", "net splitscreen prejoin players",
	"mp profile list update", "3wide player profile list update", "plyr prof edit select menu upd8",
	"player profile small menu update", "game settings lists text update", "solo game objective text",
	"color picker update", "game settings lists pic update", "main menu fake animate",
	"mp level select update", "get active plyr profile name", "get edit plyr profile name",
	"get edit game settings name", "get active plyr profile color", "mp set textbox map name",
	"mp set textbox game ruleset", "mp set textbox teams noteams", "mp set textbox score limit",
	"mp set textbox score limit type", "mp set bitmap for map", "mp set bitmap for ruleset",
	"mp set textbox", "mp edit profile set rule text", "system link status check", "mp game directions",
	"teams no teams bitmap update", "warn if diff will nuke saved game", "dim if no net cable",
};

/* the port's event handler functions (menu_functions.c), from
PC_MENU_FUNCTION_BASE, in its order */
static char const *const port_function_names[] =
{
	"unwired",
	"port mp screen init", "port mp require online", "port mp require lan",
	"port browser init", "port browser dispose", "port browser refresh", "port browser join",
	"port password init", "port password edit", "port password join", "port password back",
	"port setup init", "port setup edit name", "port setup edit password", "port setup start",
	"port setup back",
	"port code init", "port code edit", "port code join", "port code back",
	"port coop campaign",
	"port browser sort", "port browser page up", "port browser page down",
	"port browser filter next", "port browser filter previous",
	"port h3 servers on", "port h3 servers off",
	"port h3 campaign init", "port h3 mission set", "port h3 difficulty set", "port h3 campaign start",
	"port h3 mission focus", "port h3 difficulty focus",
	"port forge lobby init", "port forge map set", "port forge map focus", "port forge start",
	"port forge browser init", "port forge browser choose", "port forge browser back",
	"port forge browser previous", "port forge browser next", "port forge toggle", "port forge quit",
	"port h3 mp init", "port h3 network set", "port h3 network focus", "port h3 host system link",
	"port h3 browse system link", "port h3 map set", "port h3 map focus", "port h3 gametype set",
	"port h3 gametype focus", "port h3 gametype page up", "port h3 gametype page down", "port h3 setup hold",
	"port h3 campaign network set", "port h3 campaign network focus", "port h3 campaign host",
	"port h3 campaign focus", "port h3 scene", "port settings panel",
	"port h3 profiles campaign", "port h3 profiles customize", "port h3 profile play", "port h3 profile edit",
	"port h3 profile create", "port h3 profile delete ask", "port h3 profile delete", "port h3 profile page up",
	"port h3 profile page down", "port h3 custom next", "port h3 custom previous", "port h3 custom save",
	"port h3 custom close",
	"port forge map pick", "port forge maps back", "port forge variant page up", "port forge variant page down",
	"port forge save",
	"port fileshare init", "port fileshare focus", "port fileshare page up", "port fileshare page down",
	"port fileshare kind", "port fileshare rename", "port fileshare ask delete", "port fileshare delete",
	"port fileshare open", "port fileshare enter",
	"port forge undo", "port forge redo", "port forge snap", "port forge property next", "port forge property previous",
	"port forge close", "port forge pack toggle",
	"port h3 mods init", "port h3 mods toggle", "port h3 mods details", "port h3 mods delete ask",
	"port h3 mods delete", "port h3 mods page up", "port h3 mods page down",
	"port h3 lan init", "port h3 lan dispose", "port h3 lan refresh",
	"port h3 adhoc host", "port h3 adhoc browse", "port h3 adhoc coop", "port h3 adhoc join", "port h3 adhoc cancel",
	"port forge new round", "port forge all ai", "port h3 map page up", "port h3 map page down",
	"port h3 profiles close",
	"port h3 friends init", "port h3 friends act", "port h3 friends second", "port h3 friends options ask",
	"port h3 friends option", "port h3 friends page up", "port h3 friends page down",
	"port browser profile first",
	"port h3 players init", "port h3 players act", "port h3 players option", "port h3 players page up",
	"port h3 players page down",
	"port h3 join system link", "port h3 join split screen", "port h3 join a", "port h3 join previous",
	"port h3 join next", "port h3 join leave", "port h3 pregame init",
	"port h3 party init", "port h3 party act", "port h3 party second", "port h3 party option",
	"port h3 party page up", "port h3 party page down", "port h3 party type", "port h3 party preset",
	"port forge clear dead ai", "port forge clear all ai",
	"port h3 mp pause party", "port h3 mp settings init", "port h3 mp settings save", "port h3 mp settings close",
	"port h3 host online", "port h3 game options",
	"port h3 live leave", "port h3 invite ask", "port h3 invite init", "port h3 invite act",
	"port forge start live", "port h3 maps back", "port h3 mp offline start",
	"port h3 gametypes init", "port h3 gametypes choose", "port h3 gametypes new", "port h3 gametypes page up",
	"port h3 gametypes page down", "port h3 gametype edit", "port h3 gametype copy", "port h3 gametype rename",
	"port h3 gametype delete later", "port h3 gametype delete ask", "port h3 gametype delete", "port h3 gametype base",
	"port h3 gametype next", "port h3 gametype previous", "port h3 gametype category", "port h3 gametype save",
	"port h3 gametype save quit", "port h3 gametype leave ask", "port h3 gametype leave", "port h3 gametype discard",
	"port h3 carnage init", "port h3 carnage continue", "port h3 carnage leave ask", "port h3 carnage leave",
	"port h3 carnage players", "port h3 carnage page", "port h3 carnage up", "port h3 carnage down",
	"port h3 dialog ok", "port h3 dialog yes", "port h3 dialog no",
	"port ce pregame init", "port ce variant next", "port ce custom",
	"port h3 scoreboard act", "port h3 scoreboard option", "port h3 scoreboard close", "port h3 scoreboard page up",
	"port h3 scoreboard page down",
	"port online custom tab next", "port online custom tab previous", "port online exit",
	"port online switch open", "port online switch close", "port online lobby leave",
	"port online switch multiplayer", "port online switch forge", "port online switch campaign",
	"port h3 profile current",
	"port online profile choose", "port online profile delete ask",
	"port online browser network next", "port online browser network previous",
	"port online browser filter next", "port online browser filter previous",
	"port host multiplayer", "port host forge", "port host campaign", "port host next", "port host previous",
	"port host edit",
};

/* the port's game data functions (menu_functions.c), from
PC_MENU_FUNCTION_BASE */
static char const *const port_game_data_input_names[] =
{
	"unwired",
	"port mp update desc", "port browser update", "port password update", "port setup update", "port code update",
	"port h3 nameplate", "port h3 servers", "port h3 campaign", "port h3 missions", "port h3 difficulties",
	"port h3 mission title",
	"port forge lobby", "port forge maps", "port forge browser", "port forge title", "port forge controls",
	"port h3 mp lobby", "port h3 networks", "port h3 maps", "port h3 gametypes",
	"port h3 campaign networks",
	"port h3 profiles", "port h3 profile delete text", "port h3 custom",
	"port fileshare", "port fileshare question",
	"port forge properties", "port forge packs",
	"port h3 mods", "port h3 mods details", "port h3 mods confirm",
	"port h3 adhoc joining", "port h3 network status",
	"port h3 friends", "port h3 friends options", "port h3 friends panel",
	"port h3 players", "port h3 players options",
	"port h3 join", "port h3 pregame",
	"port h3 party", "port h3 party options", "port h3 party chat", "port h3 party quick",
	"port h3 mp pause", "port h3 mp settings",
	"port h3 invite", "port h3 party plates",
	"port h3 gametype list", "port h3 gametype actions", "port h3 gametype question", "port h3 gametype bases",
	"port h3 gametype editor", "port h3 gametype options",
	"port h3 carnage",
	"port h3 dialog",
	"port ce lobby", "port ce profile",
	"port h3 scoreboard", "port h3 scoreboard options",
	"port online version", "port online custom", "port online switch", "port online lobby",
	"port host",
};

static struct
{
	/* what is let go when the map unloads */
	void **blocks;
	long block_count;
	struct bitmap_data **bitmaps;
	long bitmap_count;
	struct cache_file_tag_instance *original_instances;
	long original_count;
	/* the player's pictures and text, and the PNGs, read at first use */
	struct menu_resource *bitmap_resources;
	long bitmap_resource_count;
	struct menu_resource *string_resources;
	long string_resource_count;
	struct menu_png_frame *png_frames;
	long png_frame_count;
	/* the bitmaps drawn whole into their widgets' bounds (fit="widget") */
	long *fit_bitmaps;
	long fit_bitmap_count;
	boolean loaded;
	/* the art: not read yet (0), read (1), failed (-1: the Xbox's screen) */
	int art;
	long root_tag;
	long xbox_root_tag;
	/* the Halo 3 set's screens (menus/halo3): the Xbox screen each stands
	for while Menu style is Halo 3, and its own root; whether the set was
	said to be left out (its pictures could not be read) */
	long h3_count;
	long h3_from[MAXIMUM_H3_SCREENS];
	long h3_to[MAXIMUM_H3_SCREENS];
	char const *h3_root[MAXIMUM_H3_SCREENS];
	char const *h3_replaces[MAXIMUM_H3_SCREENS];
	boolean h3_left_out_said;
	/* Menu style Halo CE's additions (classic_patch="true"): the map's
	widget, its own definition and ours, which stands for it while Menu style
	is Halo CE (classic_patches_apply); whether ours are in place now */
	long classic_count;
	long classic_tags[MAXIMUM_CLASSIC_PATCHES];
	void *classic_original[MAXIMUM_CLASSIC_PATCHES];
	void *classic_patched[MAXIMUM_CLASSIC_PATCHES];
	int classic_applied;
	/* Menu style Online (menus/online): each Halo 3 widget that has an
	online/ widget of the same name, and that widget, which opens in its
	place - the Halo 3 set's flows and functions, the online set's look */
	long online_count;
	long online_from[MAXIMUM_ONLINE_SCREENS];
	long online_to[MAXIMUM_ONLINE_SCREENS];
	boolean online_said[MAXIMUM_ONLINE_SCREENS];
} menu_tags;

/* the build under way */
static struct
{
	struct halo_menus const *menus;
	long first_index;
	long first_salt;
	long next;
	/* each widget's, bitmap's and string list's tag index, and each
	widget's own text's and strings' (NONE: none) */
	long *widget_tags;
	long *bitmap_tags;
	long *font_tags;
	long *strings_tags;
	long *text_tags;
	long *spinner_tags;
	boolean failed;
	/* a level's map (the in-game set) */
	boolean ingame;
} build;

/* ---------- private code */

/* (the game's allocator, which malloc and free are here: cseries.h) */
static void *allocate(long size)
{
	void *block = malloc(size > 0 ? size : 1);

	if (!block)
	{
		build.failed = TRUE;
		return NULL;
	}
	memset(block, 0, size > 0 ? size : 1);
	/* (room for 64 more at a time) */
	if (!(menu_tags.block_count % 64))
	{
		void **blocks = realloc(menu_tags.blocks, (menu_tags.block_count + 64) * sizeof(*menu_tags.blocks));

		if (!blocks)
		{
			free(block);
			build.failed = TRUE;
			return NULL;
		}
		menu_tags.blocks = blocks;
	}
	menu_tags.blocks[menu_tags.block_count++] = block;
	return block;
}

/* an array grown by one (count is its count), or FALSE when out of memory */
static boolean array_grow(void **array, long count, long size)
{
	void *grown = realloc(*array, (count + 1) * size);

	if (!grown)
	{
		build.failed = TRUE;
		return FALSE;
	}
	*array = grown;
	return TRUE;
}

static void problem(char const *file, long line, char const *message, char const *detail)
{
	if (!build.failed)
		halo_menus_log(file, line, message, detail);
	build.failed = TRUE;
}

static long name_index(char const *name, char const *const *names, long count)
{
	long index;

	for (index = 0; name && index < count; index++)
	{
		if (!strcmp(name, names[index]))
			return index;
	}
	return NONE;
}

/* the bits of the space-separated names */
static long flags_parse(char const *text, char const *const *names, long count, char const *file, long line)
{
	long flags = 0;

	while (text && *text)
	{
		char word[64];
		long length, bit;

		text += strspn(text, " \t\r\n");
		length = (long)strcspn(text, " \t\r\n");
		if (!length)
			break;
		if (length >= (long)sizeof(word))
			length = sizeof(word) - 1;
		memcpy(word, text, length);
		word[length] = 0;
		text += strcspn(text, " \t\r\n");
		bit = name_index(word, names, count);
		if (bit == NONE)
			problem(file, line, "there is no flag", word);
		else
			flags |= 1L << bit;
	}
	return flags;
}

static void reference_clear(struct tag_reference *reference, long group_tag)
{
	reference->group_tag = group_tag;
	reference->name = "";
	reference->name_length = 0;
	reference->index = NONE;
}

static void reference_set(struct tag_reference *reference, long group_tag, long tag_index)
{
	if (tag_index == NONE)
	{
		reference_clear(reference, group_tag);
		return;
	}
	reference->group_tag = group_tag;
	reference->name = (char *)tag_get_name(tag_index);
	reference->name_length = (long)strlen(reference->name);
	reference->index = tag_index;
}

static long find(char const *name, long count, char const *(*name_of)(long))
{
	long index;

	for (index = 0; name && index < count; index++)
	{
		if (!strcmp(name_of(index), name))
			return index;
	}
	return NONE;
}

static char const *widget_name(long index) { return build.menus->widgets[index].name; }
static char const *bitmap_name(long index) { return build.menus->bitmaps[index].name; }
static char const *strings_name(long index) { return build.menus->string_lists[index].name; }

static long widget_named(char const *name)
{
	return find(name, build.menus->widget_count, widget_name);
}

static long map_tag(long group_tag, char const *name, char const *file, long line)
{
	long index = tag_loaded(group_tag, name);

	if (index == NONE)
		problem(file, line, "the map has no tag", name);
	return index;
}

/* a tag: the map's by its path (with a backslash), else one of ours */
static long tag_named(long group_tag, char const *name, char const *file, long line)
{
	long index = NONE;

	if (strchr(name, '\\'))
		return map_tag(group_tag, name, file, line);
	switch (group_tag)
	{
	case UI_WIDGET_DEFINITION_TAG:
		index = widget_named(name);
		index = index == NONE ? NONE : build.widget_tags[index];
		break;
	case BITMAP_GROUP_TAG:
		index = find(name, build.menus->bitmap_count, bitmap_name);
		index = index == NONE ? NONE : build.bitmap_tags[index];
		break;
	case UNICODE_STRING_LIST_TAG:
		index = find(name, build.menus->string_list_count, strings_name);
		index = index == NONE ? NONE : build.strings_tags[index];
		break;
	}
	if (index == NONE)
		problem(file, line, "there is nothing named", name);
	return index;
}

static long function_index(char const *name, char const *file, long line)
{
	char const *function_name;
	long index;

	index = name_index(name, port_function_names, NUMBEROF(port_function_names));
	if (index != NONE)
		return PC_MENU_FUNCTION_BASE + index;
	for (index = 0; (function_name = ui_widget_event_handler_function_name(index)) != NULL; index++)
	{
		if (!strcmp(name, function_name))
			return index;
	}
	problem(file, line, "there is no event handler function", name);
	return NONE;
}

static long game_data_input_index(char const *name, char const *file, long line)
{
	long index;

	index = name_index(name, port_game_data_input_names, NUMBEROF(port_game_data_input_names));
	if (index != NONE)
		return PC_MENU_FUNCTION_BASE + index;
	index = name_index(name, game_data_input_names, NUMBEROF(game_data_input_names));
	if (index != NONE)
		return index;
	problem(file, line, "there is no game data input", name);
	return NONE;
}

static long hex_digit(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return NONE;
}

/* "#RRGGBB" or "#AARRGGBB" */
static boolean parse_color(char const *text, real_argb_color *color)
{
	long length = text ? (long)strlen(text) : 0;
	long components[4] = { 255, 0, 0, 0 };
	long index;

	if ((length != 7 && length != 9) || text[0] != '#')
		return FALSE;
	for (index = 0; index < (length - 1) / 2; index++)
	{
		long high = hex_digit(text[1 + 2 * index]), low = hex_digit(text[2 + 2 * index]);

		if (high == NONE || low == NONE)
			return FALSE;
		components[length == 7 ? index + 1 : index] = high * 16 + low;
	}
	color->alpha = components[0] / 255.0f;
	color->red = components[1] / 255.0f;
	color->green = components[2] / 255.0f;
	color->blue = components[3] / 255.0f;
	return TRUE;
}

/* "top left bottom right" */
static void parse_bounds(char const *text, rectangle2d *bounds, char const *file, long line)
{
	long values[4] = { 0, 0, 0, 0 };
	long count = 0;

	while (text && *text && count < 4)
	{
		char *end;

		values[count] = strtol(text, &end, 10);
		if (end == text)
			break;
		count++;
		text = end + strspn(end, " ");
	}
	if (text && (count != 4 || *text))
	{
		problem(file, line, "bounds are \"top left bottom right\":", text);
		return;
	}
	if (count == 4)
	{
		bounds->y0 = (short)values[0];
		bounds->x0 = (short)values[1];
		bounds->y1 = (short)values[2];
		bounds->x1 = (short)values[3];
	}
}

/* a 'ustr' of the strings */
/* a text of the files in the player's language (port/linux/src/lang.c),
as the menus are built: each time ui.map loads, so the menus follow the
settings' Language row from the next main menu on. The key is the file's
text with its "\n"s made line breaks (tools/lang_check.py reads the XML so);
`buffer` holds it */
static char const *menu_text_translated(char const *original, char *buffer, long size)
{
	char const *text = original;
	long used = 0;

	for (; *text && used < size - 1; text++)
	{
		if (text[0] == '\\' && text[1] == 'n')
		{
			buffer[used++] = '\n';
			text++;
		}
		else
			buffer[used++] = *text;
	}
	buffer[used] = 0;
	/* (a text longer than the buffer: as the file has it) */
	return *text ? original : T(buffer);
}

static void *string_list_build(char const *const *strings, long count)
{
	struct string_list *list = allocate(sizeof(struct string_list));
	struct string_list_entry *entries = allocate(count * sizeof(struct string_list_entry));
	long index;

	if (!list || !entries)
		return list;
	list->strings.count = count;
	list->strings.address = entries;
	for (index = 0; index < count; index++)
	{
		char key[1024];
		char const *text = menu_text_translated(strings[index], key, (long)sizeof(key));
		long length = (long)strlen(text);
		unsigned short *characters = allocate((length * 2 + 2) * sizeof(unsigned short));
		long written;

		if (!characters)
			return list;
		written = halo_menus_utf16(text, characters, length * 2 + 2);
		entries[index].string.size = written * (long)sizeof(unsigned short);
		entries[index].string.address = characters;
	}
	return list;
}

/* the "|"-separated pieces of text, copied (at most MAXIMUM_STRINGS) */
static long split(char const *text, char const **pieces)
{
	long count = 0;

	while (text && count < MAXIMUM_STRINGS)
	{
		long length = (long)strcspn(text, "|");
		char *piece = allocate(length + 1);

		if (!piece)
			break;
		memcpy(piece, text, length);
		pieces[count++] = piece;
		if (!text[length])
			break;
		text += length + 1;
	}
	return count;
}

/* a bitmap group of frame_count frames, one sequence of them all */
static struct bitmap_group *bitmap_group_new(long frame_count)
{
	struct bitmap_group *group = allocate(sizeof(struct bitmap_group));
	struct bitmap_group_sequence *sequence = allocate(sizeof(struct bitmap_group_sequence));
	struct bitmap_data *bitmaps = frame_count ? allocate(frame_count * sizeof(struct bitmap_data)) : NULL;

	if (!group || !sequence || (frame_count && !bitmaps))
		return group;
	sequence->first_bitmap_index = 0;
	sequence->bitmap_count = (short)frame_count;
	group->sequences.count = 1;
	group->sequences.address = sequence;
	group->bitmaps.count = frame_count;
	group->bitmaps.address = bitmaps;
	return group;
}

static void *bitmap_build(struct halo_menu_bitmap const *source, long tag_index)
{
	struct bitmap_group *group;
	long frame;

	if (build.failed)
		return NULL;
	if (source->fit)
	{
		if (!array_grow((void **)&menu_tags.fit_bitmaps, menu_tags.fit_bitmap_count, sizeof(*menu_tags.fit_bitmaps)))
			return NULL;
		menu_tags.fit_bitmaps[menu_tags.fit_bitmap_count++] = tag_index;
	}
	/* (the player's: filled when the screens first open) */
	if (source->resource)
	{
		struct menu_resource *resource;

		if (!array_grow((void **)&menu_tags.bitmap_resources, menu_tags.bitmap_resource_count,
			sizeof(*menu_tags.bitmap_resources)))
			return NULL;
		resource = &menu_tags.bitmap_resources[menu_tags.bitmap_resource_count++];
		resource->path = source->resource;
		resource->tag_index = tag_index;
		resource->file = source->file;
		resource->line = source->line;
		return bitmap_group_new(0);
	}
	if (!source->frame_count)
	{
		problem(source->file, source->line, "a bitmap has no frames:", source->name);
		return NULL;
	}
	group = bitmap_group_new(source->frame_count);
	if (!group || !group->bitmaps.address)
		return group;
	for (frame = 0; frame < source->frame_count; frame++)
	{
		struct halo_menu_frame const *data = &build.menus->frames[source->first_frame + frame];
		struct bitmap_data *bitmap = (struct bitmap_data *)group->bitmaps.address + frame;

		if (data->map)
		{
			long group_index = map_tag(BITMAP_GROUP_TAG, data->map, source->file, source->line);
			struct bitmap_group *group_source;

			if (group_index == NONE)
				return group;
			group_source = bitmap_group_get(group_index);
			if (data->index < 0 || data->index >= group_source->bitmaps.count)
			{
				problem(source->file, source->line, "the map's bitmap has no such frame:", data->map);
				return group;
			}
			/* (a copy, which the texture cache loads from the map as its own) */
			memcpy(bitmap, (struct bitmap_data *)group_source->bitmaps.address + data->index, sizeof(*bitmap));
			bitmap->cache_block_index = NONE;
			bitmap->base_address = NULL;
			continue;
		}
		if (data->width <= 0 || data->height <= 0 || data->width > 2048 || data->height > 2048)
		{
			problem(source->file, source->line, "a frame is 1 to 2048 wide and high:", data->png);
			return group;
		}
		/* (its texture is made as a power of two's, below: another size
		would be read with the wrong rows, and spoil the textures after it) */
		if ((data->width & (data->width - 1)) || (data->height & (data->height - 1)))
		{
			problem(source->file, source->line, "a PNG frame's width and height are powers of two:", data->png);
			return group;
		}
		/* (its texture is made when the screens first open, from the PNG) */
		bitmap->signature = BITMAP_GROUP_TAG;
		bitmap->width = (short)data->width;
		bitmap->height = (short)data->height;
		bitmap->depth = 1;
		bitmap->type = _bitmap_type_2d;
		bitmap->format = _bitmap_format_a8r8g8b8;
		bitmap->flags = FLAG(_bitmap_has_power_of_two_dimensions_bit);
		bitmap->tag_index = tag_index;
		bitmap->cache_block_index = NONE;
		if (!array_grow((void **)&menu_tags.png_frames, menu_tags.png_frame_count, sizeof(*menu_tags.png_frames)))
			return group;
		menu_tags.png_frames[menu_tags.png_frame_count].bitmap = bitmap;
		menu_tags.png_frames[menu_tags.png_frame_count].png = data->png;
		menu_tags.png_frames[menu_tags.png_frame_count].file = source->file;
		menu_tags.png_frames[menu_tags.png_frame_count].line = source->line;
		menu_tags.png_frame_count++;
	}
	return group;
}

/* adds a conditional widget to the definition, if it has not got it */
static void conditional_add(struct menu_widget_definition *definition, long tag_index, long flags)
{
	struct ui_widget_conditional_reference *conditionals =
		(struct ui_widget_conditional_reference *)definition->conditional_widgets.address;
	struct ui_widget_conditional_reference *grown;
	long count = definition->conditional_widgets.count, index;

	for (index = 0; index < count; index++)
	{
		if (conditionals[index].widget_tag.index == tag_index)
			return;
	}
	grown = allocate((count + 1) * sizeof(*grown));
	if (!grown)
		return;
	/* (the game's memcpy asserts on a NULL source, even for nothing) */
	if (count)
		memcpy(grown, conditionals, count * sizeof(*grown));
	reference_set(&grown[count].widget_tag, UI_WIDGET_DEFINITION_TAG, tag_index);
	grown[count].flags = flags;
	definition->conditional_widgets.address = grown;
	definition->conditional_widgets.count = count + 1;
}

static void handler_build(struct ui_widget_event_handler_reference *handler, struct halo_menu_handler const *source,
	long event_type, struct menu_widget_definition *definition)
{
	char const *targets[5];
	long target_count = 0;

	handler->event_type = (short)event_type;
	reference_clear(&handler->widget_tag, UI_WIDGET_DEFINITION_TAG);
	reference_clear(&handler->sound_effect, SOUND_TAG);
	if (source->run)
	{
		handler->function = (short)function_index(source->run, source->file, source->line);
		SET_FLAG(handler->flags, _event_handler_run_function_bit, TRUE);
	}
	if (source->script)
	{
		strncpy(handler->script, source->script, sizeof(handler->script) - 1);
		SET_FLAG(handler->flags, _event_handler_run_scenario_script_bit, TRUE);
	}
	else if (source->label)
	{
		strncpy(handler->script, source->label, sizeof(handler->script) - 1);
	}
	if (source->open)
	{
		targets[target_count++] = source->open;
		SET_FLAG(handler->flags, _event_handler_open_widget_bit, TRUE);
	}
	if (source->replace)
	{
		targets[target_count++] = source->replace;
		SET_FLAG(handler->flags, _event_handler_replace_self_with_widget_bit, TRUE);
	}
	if (source->focus)
	{
		targets[target_count++] = source->focus;
		SET_FLAG(handler->flags, _event_handler_give_focus_to_widget_bit, TRUE);
	}
	if (source->back)
		SET_FLAG(handler->flags, _event_handler_go_back_to_previous_widget_bit, TRUE);
	if (source->branch)
		SET_FLAG(handler->flags, _event_handler_try_to_branch_on_failure_bit, TRUE);
	if (source->close)
	{
		if (!strcmp(source->close, "current"))
			SET_FLAG(handler->flags, _event_handler_close_current_widget_bit, TRUE);
		else if (!strcmp(source->close, "all"))
			SET_FLAG(handler->flags, _event_handler_close_all_widgets_bit, TRUE);
		else if (!strcmp(source->close, "other") && source->widget)
			SET_FLAG(handler->flags, _event_handler_close_other_widget_bit, TRUE);
		else
			problem(source->file, source->line, "close is \"current\", \"all\" or \"other\" (with widget=)",
				source->close);
	}
	if (source->reload)
	{
		if (!strcmp(source->reload, "self"))
			SET_FLAG(handler->flags, _event_handler_reload_self_bit, TRUE);
		else if (!strcmp(source->reload, "other") && source->widget)
			SET_FLAG(handler->flags, _event_handler_reload_other_widget_bit, TRUE);
		else
			problem(source->file, source->line, "reload is \"self\" or \"other\" (with widget=)", source->reload);
	}
	if (source->widget)
		targets[target_count++] = source->widget;
	if (target_count > 1)
	{
		long index;

		/* (several flags may share the one widget) */
		for (index = 1; index < target_count; index++)
		{
			if (strcmp(targets[index], targets[0]))
				problem(source->file, source->line, "a handler names one widget (open, replace, focus or widget)",
					NULL);
		}
	}
	if (target_count)
	{
		/* (the in-game set is built into every level's map, the campaign's
		and the multiplayer maps', which carry different screens of the
		game's: one a map lacks is opened by nothing there, not a reason to
		leave the set out - a multiplayer map has no campaign pause screen,
		so nothing there opens the Halo 3 one that names the campaign's
		questions) */
		if (build.ingame && strchr(targets[0], '\\') && tag_loaded(UI_WIDGET_DEFINITION_TAG, targets[0]) == NONE)
		{
			platform_log("menus: %s:%ld: this map has no %s: the handler opens nothing here", source->file,
				source->line, targets[0]);
			SET_FLAG(handler->flags, _event_handler_open_widget_bit, FALSE);
			SET_FLAG(handler->flags, _event_handler_replace_self_with_widget_bit, FALSE);
			SET_FLAG(handler->flags, _event_handler_give_focus_to_widget_bit, FALSE);
			SET_FLAG(handler->flags, _event_handler_close_other_widget_bit, FALSE);
			SET_FLAG(handler->flags, _event_handler_reload_other_widget_bit, FALSE);
		}
		else
		{
			reference_set(&handler->widget_tag, UI_WIDGET_DEFINITION_TAG,
				tag_named(UI_WIDGET_DEFINITION_TAG, targets[0], source->file, source->line));
		}
	}
	if (source->sound)
		reference_set(&handler->sound_effect, SOUND_TAG, map_tag(SOUND_TAG, source->sound, source->file, source->line));
	if (source->otherwise)
	{
		if (!source->run)
			problem(source->file, source->line, "otherwise needs run (the function whose failure it is for)", NULL);
		SET_FLAG(handler->flags, _event_handler_try_to_branch_on_failure_bit, TRUE);
		conditional_add(definition, tag_named(UI_WIDGET_DEFINITION_TAG, source->otherwise, source->file, source->line), 1);
	}
}

/* a font: large, small, terminal, or the map's by its path */
static long font_tag(char const *font, char const *file, long line)
{
	long index;
	char const *path;

	/* (the menus' own fonts first: <font>) */
	for (index = 0; index < build.menus->font_count; index++)
	{
		if (!strcmp(font, build.menus->fonts[index].name))
			return build.font_tags[index];
	}
	path = !strcmp(font, "large") ? "ui\\large_ui" :
		!strcmp(font, "small") ? "ui\\small_ui" :
		!strcmp(font, "terminal") ? "ui\\interstate" : font;
	return map_tag(FONT_TAG, path, file, line);
}

/* a font of the menus' own (<font>, tools/halo3_menu_art.py): its file read
and made a font tag as the game's are (text/font_group.h): one table of the
256 first characters, each character its advance, box and origin, and the
pixels, 8 bits each, that rasterizer_text.c copies to its glyph cache. NULL
(the widgets' text not drawn) if the file is not one */
struct menu_font_character
{
	word character;
	short character_width;
	short bitmap_width;
	short bitmap_height;
	short bitmap_origin_x;
	short bitmap_origin_y;
	short hardware_character_index;
	word pad;
	long pixels_offset;
};

typedef char verify_menu_font_character_size[sizeof(struct menu_font_character) == FONT_CHARACTER_SIZE ? 1 : -1];

static short read_short(byte const *data)
{
	return (short)(data[0] | (data[1] << 8));
}

/* (a <font sharp=...>) the variant for the display's pixels per unit: the
least of those at or above it (a few hundredths below counts), else the
greatest; its scale times 1000 (0: none, the file itself) */
static char const *font_sharp_variant(struct halo_menu_font const *source, char *path, int size, long *scale)
{
	char const *at = source->sharp;
	float wanted = halo_ui_pixel_scale(), best = 0.0f, largest = 0.0f;
	char largest_path[128] = "";

	*scale = 0;
	path[0] = 0;
	/* (debug: HALO_SHARP_FONTS=0, the 640x480 fonts scaled up, as before) */
	if (!at || wanted < 1.05f || (getenv("HALO_SHARP_FONTS") && !strcmp(getenv("HALO_SHARP_FONTS"), "0")))
		return NULL;
	while (*at)
	{
		char item[160];
		size_t length;
		char *colon;
		float value;

		at += strspn(at, " \t\r\n");
		length = strcspn(at, " \t\r\n");
		if (!length)
			break;
		snprintf(item, sizeof(item), "%.*s", (int)length, at);
		at += length;
		colon = strchr(item, ':');
		if (!colon)
			continue;
		*colon = 0;
		value = (float)atof(item);
		if (value <= 1.0f)
			continue;
		if (value >= wanted - 0.03f && (!best || value < best))
		{
			best = value;
			snprintf(path, size, "%s", colon + 1);
		}
		if (value > largest)
		{
			largest = value;
			snprintf(largest_path, sizeof(largest_path), "%s", colon + 1);
		}
	}
	if (!best && largest)
	{
		best = largest;
		snprintf(path, size, "%s", largest_path);
	}
	if (!best)
		return NULL;
	*scale = (long)(best * 1000.0f + 0.5f);
	return path;
}

static void *font_build(struct halo_menu_font const *source)
{
	unsigned long size = 0;
	char sharp_path[128];
	long sharp_scale = 0;
	char const *path = font_sharp_variant(source, sharp_path, sizeof(sharp_path), &sharp_scale);
	byte *data = halo_menus_file_read(path ? path : source->path, &size);
	struct font_header *font = NULL;
	struct font_character_table *table;
	short *indices;
	struct menu_font_character *characters;
	byte *pixels;
	long count = 0, index, pixel_bytes = 0;

	if (!data || size < 16 || memcmp(data, "H3FN", 4) || read_short(data + 4) != 1 ||
		(count = read_short(data + 14)) <= 0 || size < 16 + (unsigned long)count * 12)
	{
		problem(source->file, source->line, "a font file of the menus folder cannot be read:", source->path);
		halo_menus_file_free(data);
		return NULL;
	}
	for (index = 0; index < count; index++)
	{
		byte const *record = data + 16 + index * 12;

		pixel_bytes += (long)read_short(record + 4) * read_short(record + 6);
	}
	if (size != 16 + (unsigned long)count * 12 + (unsigned long)pixel_bytes)
	{
		problem(source->file, source->line, "a font file of the menus folder is not whole:", source->path);
		halo_menus_file_free(data);
		return NULL;
	}
	font = allocate(sizeof(*font));
	table = allocate(sizeof(*table));
	indices = allocate(256 * sizeof(short));
	characters = allocate(count * sizeof(*characters));
	pixels = allocate(pixel_bytes ? pixel_bytes : 1);
	if (!font || !table || !indices || !characters || !pixels)
	{
		halo_menus_file_free(data);
		return font;
	}
	memset(font, 0, sizeof(*font));
	/* (a sharp variant: its texels per unit, draw_string.c) */
	if (sharp_scale)
	{
		font->pad[0] = 0x53485250L;
		font->pad[1] = sharp_scale;
		platform_log("menus: font %s drawn sharp: %s, %.3f texels a unit (the display %.3f pixels a unit)",
			source->name, path, sharp_scale / 1000.0, halo_ui_pixel_scale());
	}
	font->ascending_height = read_short(data + 6);
	font->descending_height = read_short(data + 8);
	font->leading_height = read_short(data + 10);
	font->leading_width = read_short(data + 12);
	for (index = 0; index < 256; index++)
		indices[index] = NONE;
	table->character_indices.count = 256;
	table->character_indices.address = indices;
	font->character_tables.count = 1;
	font->character_tables.address = table;
	for (index = 0; index < NUMBER_OF_TEXT_STYLES; index++)
		reference_clear(&font->style_fonts[index], FONT_TAG);
	pixel_bytes = 0;
	for (index = 0; index < count; index++)
	{
		byte const *record = data + 16 + index * 12;
		struct menu_font_character *character = &characters[index];
		word code = (word)read_short(record);

		character->character = code;
		character->character_width = read_short(record + 2);
		character->bitmap_width = read_short(record + 4);
		character->bitmap_height = read_short(record + 6);
		character->bitmap_origin_x = read_short(record + 8);
		character->bitmap_origin_y = read_short(record + 10);
		character->hardware_character_index = NONE;
		character->pad = 0;
		character->pixels_offset = pixel_bytes;
		pixel_bytes += (long)character->bitmap_width * character->bitmap_height;
		if (code < 256)
			indices[code] = (short)index;
	}
	memcpy(pixels, data + 16 + count * 12, pixel_bytes);
	font->characters.count = count;
	font->characters.address = characters;
	font->pixels.size = pixel_bytes;
	font->pixels.address = pixels;
	halo_menus_file_free(data);
	return font;
}

static long event_words_count(char const *text)
{
	long count = 0;

	while (text && *text)
	{
		long length;

		text += strspn(text, " \t\r\n");
		length = (long)strcspn(text, " \t\r\n");
		if (!length)
			break;
		count++;
		text += length;
	}
	return count;
}

/* a block of the map's widget's followed by ours (or ours alone, the map's
dropped) */
static void block_join(struct tag_block *joined, struct tag_block const *map, struct tag_block const *ours,
	long element_size, boolean dropped)
{
	long map_count = dropped ? 0 : map->count;
	long count = map_count + ours->count;
	byte *elements;

	joined->count = 0;
	joined->address = NULL;
	if (!count)
		return;
	elements = allocate(count * element_size);
	if (!elements)
		return;
	if (map_count)
		memcpy(elements, map->address, map_count * element_size);
	if (ours->count)
		memcpy(elements + map_count * element_size, ours->address, ours->count * element_size);
	joined->count = count;
	joined->address = elements;
}

static boolean drop_named(struct halo_menu_widget const *source, char const *what)
{
	return source->drop && strstr(source->drop, what) != NULL;
}

/* (extends=) the map's widget's definition with what the element gives in
its place (halo_menus.h), built as ours is (built); the map without the
widget (a level's map: its screens differ): ours alone, said */
static void *widget_extend(struct halo_menu_widget const *source, struct menu_widget_definition *built)
{
	long tag_index = tag_loaded(UI_WIDGET_DEFINITION_TAG, source->extends);
	struct menu_widget_definition const *map;
	struct menu_widget_definition *definition;

	if (tag_index == NONE || !(map = tag_get(UI_WIDGET_DEFINITION_TAG, tag_index)))
		return built;
	definition = allocate(sizeof(*definition));
	if (!definition)
		return built;
	*definition = *map;
	/* (an Xbox screen's widget it stands for keeps its name: the logs, and
	the Xbox's own code that looks for it by name, see the map's) */
	if (!source->classic_patch)
		memcpy(definition->name, built->name, sizeof(definition->name));
	if (source->bitmap)
		definition->background_bitmap = built->background_bitmap;
	if (source->flags)
		definition->flags = built->flags;
	if (source->font)
		definition->text_font = built->text_font;
	if (source->color)
		definition->text_color = built->text_color;
	if (source->align)
		definition->justification = built->justification;
	if (source->text || source->string_list || source->strings)
	{
		definition->text_label_string_list = built->text_label_string_list;
		definition->string_list_index = built->string_list_index;
	}
	if (source->text_x)
		definition->horizontal_offset = built->horizontal_offset;
	if (source->text_y)
		definition->vertical_offset = built->vertical_offset;
	if (source->has_width)
		definition->bounds.x1 = (short)(definition->bounds.x0 + source->width);
	if (source->has_height)
		definition->bounds.y1 = (short)(definition->bounds.y0 + source->height);
	if (source->description)
		definition->extended_description_widget = built->extended_description_widget;
	/* (a widget of ours - not a classic patch, which keeps the map's tag and
	so its trust, ui_widget_function_denied - takes the map's look alone:
	never its events or data inputs, which would run as ours, the port's
	functions and the main menu's allowed - a level's map is the player's,
	maybe a Custom Edition one) */
	block_join(&definition->child_widgets, &map->child_widgets, &built->child_widgets,
		sizeof(struct ui_widget_child_reference), drop_named(source, "children"));
	block_join(&definition->event_handlers, &map->event_handlers, &built->event_handlers,
		sizeof(struct ui_widget_event_handler_reference), !source->classic_patch || drop_named(source, "handlers"));
	block_join(&definition->game_data_inputs, &map->game_data_inputs, &built->game_data_inputs,
		sizeof(struct ui_widget_game_data_input_reference), !source->classic_patch || drop_named(source, "inputs"));
	if (source->classic_patch)
	{
		long index;

		for (index = 0; index < menu_tags.classic_count && menu_tags.classic_tags[index] != tag_index; index++)
			;
		if (index < menu_tags.classic_count || menu_tags.classic_count == MAXIMUM_CLASSIC_PATCHES)
			problem(source->file, source->line, index < menu_tags.classic_count ?
				"two widgets patch the map's" : "too many classic patches:", source->extends);
		else
		{
			menu_tags.classic_tags[index] = tag_index;
			menu_tags.classic_original[index] = (void *)map;
			menu_tags.classic_patched[index] = definition;
			menu_tags.classic_count++;
		}
	}
	return definition;
}

static void *widget_build(long widget_index)
{
	struct halo_menus const *menus = build.menus;
	struct halo_menu_widget const *source = &menus->widgets[widget_index];
	struct menu_widget_definition *definition = allocate(sizeof(struct menu_widget_definition));
	char const *leaf = strrchr(source->name, '/') ? strrchr(source->name, '/') + 1 : source->name;
	long child, handler, input, conditional, replace, count;

	if (!definition)
		return NULL;
	strncpy(definition->name, leaf, sizeof(definition->name) - 1);
	definition->type = _widget_type_container;
	/* (extends= a widget this map has not - a level's map of another kind:
	the multiplayer pause's rows on a campaign map -: an empty widget, its
	children, events and inputs not looked for, and nothing stands for the
	map's; said once, the set still added) */
	if (source->extends && tag_loaded(UI_WIDGET_DEFINITION_TAG, source->extends) == NONE)
	{
		platform_log("menus: %s:%ld: this map has no %s for %s to extend: left out here", source->file, source->line,
			source->extends, source->name);
		return definition;
	}
	if (source->type)
	{
		long type = name_index(source->type, widget_type_names, NUMBEROF(widget_type_names));

		if (type == NONE)
			problem(source->file, source->line, "there is no widget type", source->type);
		else
			definition->type = (short)type;
	}
	definition->controller_index = 4;
	if (source->controller && strcmp(source->controller, "any"))
	{
		if (strlen(source->controller) != 1 || source->controller[0] < '1' || source->controller[0] > '4')
			problem(source->file, source->line, "controller is 1 to 4, or \"any\":", source->controller);
		else
			definition->controller_index = (short)(source->controller[0] - '1');
	}
	definition->bounds.y0 = (short)source->top;
	definition->bounds.x0 = (short)source->left;
	definition->bounds.y1 = (short)(source->top + (source->has_height ? source->height : 480));
	definition->bounds.x1 = (short)(source->left + (source->has_width ? source->width : 640));
	definition->flags = flags_parse(source->flags, widget_flag_names, NUMBEROF(widget_flag_names),
		source->file, source->line);
	definition->milliseconds_to_auto_close = source->auto_close;
	definition->auto_close_fade_time = source->auto_close_fade;
	reference_clear(&definition->background_bitmap, BITMAP_GROUP_TAG);
	if (source->bitmap && strcmp(source->bitmap, "none"))
	{
		reference_set(&definition->background_bitmap, BITMAP_GROUP_TAG,
			tag_named(BITMAP_GROUP_TAG, source->bitmap, source->file, source->line));
	}
	/* the game data inputs */
	for (count = 0, input = source->first_input; input != HALO_MENU_NONE; input = menus->inputs[input].next)
		count++;
	if (count)
	{
		struct ui_widget_game_data_input_reference *inputs = allocate(count * sizeof(*inputs));

		definition->game_data_inputs.count = count;
		definition->game_data_inputs.address = inputs;
		for (count = 0, input = source->first_input; inputs && input != HALO_MENU_NONE; input = menus->inputs[input].next)
		{
			struct halo_menu_input const *data = &menus->inputs[input];

			inputs[count++].function = (short)game_data_input_index(data->input, data->file, data->line);
		}
	}
	/* search and replace */
	for (count = 0, replace = source->first_replace; replace != HALO_MENU_NONE; replace = menus->replaces[replace].next)
		count++;
	if (count)
	{
		struct ui_widget_search_and_replace_reference *replaces = allocate(count * sizeof(*replaces));

		definition->search_and_replace_functions.count = count;
		definition->search_and_replace_functions.address = replaces;
		for (count = 0, replace = source->first_replace; replaces && replace != HALO_MENU_NONE;
			replace = menus->replaces[replace].next)
		{
			struct halo_menu_replace const *item = &menus->replaces[replace];
			long function = item->function ? name_index(item->function, replace_names, NUMBEROF(replace_names)) : 0;

			if (function == NONE)
				problem(item->file, item->line, "a replace function is none or controller:", item->function);
			strncpy(replaces[count].search_string, item->search, sizeof(replaces[count].search_string) - 1);
			replaces[count++].replace_function = (short)(function == 1 ? 1 : 0);
		}
	}
	/* the text */
	reference_clear(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG);
	reference_clear(&definition->text_font, FONT_TAG);
	if ((source->text != NULL) + (source->strings != NULL) + (source->string_list != NULL) > 1)
		problem(source->file, source->line, "a widget has one of text, strings and string_list:", source->name);
	if (build.text_tags[widget_index] != NONE)
	{
		reference_set(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG, build.text_tags[widget_index]);
	}
	else if (build.spinner_tags[widget_index] != NONE)
	{
		reference_set(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG, build.spinner_tags[widget_index]);
		definition->list_flags |= FLAG(_list_items_generated_from_string_list_tag_bit);
	}
	else if (source->string_list)
	{
		reference_set(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG,
			tag_named(UNICODE_STRING_LIST_TAG, source->string_list, source->file, source->line));
	}
	definition->string_list_index = (short)source->string_index;
	/* (our own text is white in the large font, unless given) */
	if (source->text || source->strings)
	{
		definition->text_color.alpha = definition->text_color.red = 1.0f;
		definition->text_color.green = definition->text_color.blue = 1.0f;
		if (!source->font)
			reference_set(&definition->text_font, FONT_TAG, font_tag("large", source->file, source->line));
	}
	if (source->color && !parse_color(source->color, &definition->text_color))
		problem(source->file, source->line, "a color is #RRGGBB or #AARRGGBB:", source->color);
	if (source->font)
		reference_set(&definition->text_font, FONT_TAG, font_tag(source->font, source->file, source->line));
	if (source->align)
	{
		long align = name_index(source->align, align_names, NUMBEROF(align_names));

		if (align == NONE)
			problem(source->file, source->line, "align is \"left\", \"right\" or \"center\":", source->align);
		else
			definition->justification = (short)align;
	}
	definition->text_box_flags = (word)flags_parse(source->text_flags, text_flag_names, NUMBEROF(text_flag_names),
		source->file, source->line);
	definition->horizontal_offset = (short)source->text_x;
	definition->vertical_offset = (short)source->text_y;
	/* the list */
	definition->list_flags |= flags_parse(source->list_flags, list_flag_names, NUMBEROF(list_flag_names),
		source->file, source->line);
	if (source->strings && (definition->type != _widget_type_spinner_list || source->first_child != HALO_MENU_NONE))
		problem(source->file, source->line, "strings are for a spinner with no children:", source->name);
	if (TEST_FLAG(definition->list_flags, _list_items_generated_from_string_list_tag_bit) &&
		(definition->type != _widget_type_spinner_list || source->first_child != HALO_MENU_NONE ||
			definition->text_label_string_list.index == NONE))
		problem(source->file, source->line, "items_from_strings is for a spinner with strings and no children:",
			source->name);
	reference_clear(&definition->list_header_bitmap, BITMAP_GROUP_TAG);
	reference_clear(&definition->list_footer_bitmap, BITMAP_GROUP_TAG);
	if (source->header_bitmap)
	{
		reference_set(&definition->list_header_bitmap, BITMAP_GROUP_TAG,
			tag_named(BITMAP_GROUP_TAG, source->header_bitmap, source->file, source->line));
	}
	if (source->footer_bitmap)
	{
		reference_set(&definition->list_footer_bitmap, BITMAP_GROUP_TAG,
			tag_named(BITMAP_GROUP_TAG, source->footer_bitmap, source->file, source->line));
	}
	parse_bounds(source->header_bounds, &definition->list_header_bounds, source->file, source->line);
	parse_bounds(source->footer_bounds, &definition->list_footer_bounds, source->file, source->line);
	reference_clear(&definition->extended_description_widget, UI_WIDGET_DEFINITION_TAG);
	if (source->description)
	{
		if (definition->type != _widget_type_column_list)
			problem(source->file, source->line, "a description is for a column list:", source->name);
		reference_set(&definition->extended_description_widget, UI_WIDGET_DEFINITION_TAG,
			tag_named(UI_WIDGET_DEFINITION_TAG, source->description, source->file, source->line));
	}
	/* the children */
	for (count = 0, child = source->first_child; child != HALO_MENU_NONE; child = menus->children[child].next)
		count++;
	if (definition->type == _widget_type_spinner_list && count != 0 && count != 1 && count != 3)
		problem(source->file, source->line, "a spinner has 0, 1 or 3 children:", source->name);
	if (count)
	{
		struct ui_widget_child_reference *children = allocate(count * sizeof(*children));

		definition->child_widgets.count = count;
		definition->child_widgets.address = children;
		for (count = 0, child = source->first_child; children && child != HALO_MENU_NONE; child = menus->children[child].next)
		{
			struct halo_menu_child const *entry = &menus->children[child];
			long tag_index = entry->nested != HALO_MENU_NONE ? build.widget_tags[entry->nested] :
				tag_named(UI_WIDGET_DEFINITION_TAG, entry->widget, entry->file, entry->line);
			char const *name = entry->nested != HALO_MENU_NONE ? menus->widgets[entry->nested].name : entry->widget;
			char const *child_leaf = strrchr(name, '/') ? strrchr(name, '/') + 1 : name;

			if (strrchr(child_leaf, '\\'))
				child_leaf = strrchr(child_leaf, '\\') + 1;
			reference_set(&children[count].widget_tag, UI_WIDGET_DEFINITION_TAG, tag_index);
			strncpy(children[count].name, child_leaf, sizeof(children[count].name) - 1);
			children[count].horizontal_offset = (short)entry->x;
			children[count].vertical_offset = (short)entry->y;
			if (entry->controller)
			{
				if (strlen(entry->controller) != 1 || entry->controller[0] < '1' || entry->controller[0] > '4')
					problem(entry->file, entry->line, "a child's controller is 1 to 4:", entry->controller);
				SET_FLAG(children[count].flags, _child_widget_use_custom_controller_index_bit, TRUE);
				children[count].custom_controller_index = (short)(entry->controller[0] - '1');
			}
			count++;
		}
	}
	/* the conditional widgets */
	for (conditional = source->first_conditional; conditional != HALO_MENU_NONE;
		conditional = menus->conditionals[conditional].next)
	{
		struct halo_menu_conditional const *item = &menus->conditionals[conditional];

		conditional_add(definition, tag_named(UI_WIDGET_DEFINITION_TAG, item->widget, item->file, item->line),
			item->if_failed ? 1 : 0);
	}
	/* the event handlers: one for each of each <on>'s events */
	for (count = 0, handler = source->first_handler; handler != HALO_MENU_NONE; handler = menus->handlers[handler].next)
		count += event_words_count(menus->handlers[handler].event);
	if (count)
	{
		struct ui_widget_event_handler_reference *handlers = allocate(count * sizeof(*handlers));

		definition->event_handlers.count = count;
		definition->event_handlers.address = handlers;
		for (count = 0, handler = source->first_handler; handlers && handler != HALO_MENU_NONE;
			handler = menus->handlers[handler].next)
		{
			struct halo_menu_handler const *on = &menus->handlers[handler];
			char const *text = on->event;

			while (text && *text)
			{
				char word[64];
				long length, event;

				text += strspn(text, " \t\r\n");
				length = (long)strcspn(text, " \t\r\n");
				if (!length)
					break;
				if (length >= (long)sizeof(word))
					length = sizeof(word) - 1;
				memcpy(word, text, length);
				word[length] = 0;
				text += strcspn(text, " \t\r\n");
				event = name_index(word, event_names, NUMBEROF(event_names));
				if (event == NONE)
					problem(on->file, on->line, "there is no event", word);
				handler_build(&handlers[count++], on, event, definition);
			}
		}
	}
	if (source->extends)
		return widget_extend(source, definition);
	return definition;
}

/* the tag table with count more entries, the existing ones as they are */
static struct cache_file_tag_instance *instances_grow(long count, long *first_index, long *first_salt)
{
	long existing, index, salt = 0;
	struct cache_file_tag_instance *instances = cache_files_tag_instances(&existing);
	struct cache_file_tag_instance *grown;

	if (!instances)
		return NULL;
	/* (every tag's absolute index fits a tag index's 16 bits, short of
	NONE's) */
	if (existing < 0 || count < 0 || existing > 0x7FFF - count)
	{
		platform_log("menus: %ld tags and the map's %ld are too many for a tag table", count, existing);
		return NULL;
	}
	grown = allocate((existing + count) * sizeof(*grown));
	if (!grown)
		return NULL;
	memcpy(grown, instances, existing * sizeof(*grown));
	for (index = 0; index < existing; index++)
	{
		long instance_salt = (unsigned long)instances[index].tag_index >> 16;

		if (instance_salt > salt)
			salt = instance_salt;
	}
	menu_tags.original_instances = instances;
	menu_tags.original_count = existing;
	*first_index = existing;
	*first_salt = (salt + 1) & 0x7FFF;
	return grown;
}

static long next_tag(void)
{
	long tag_index = ((build.first_salt + build.next) << 16) | (build.first_index + build.next);

	build.next++;
	return tag_index;
}

static void instance_set(struct cache_file_tag_instance *instances, long group_tag, long tag_index, char const *name,
	char const *suffix, void *definition)
{
	struct cache_file_tag_instance *instance = &instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index)];
	char *copy = allocate((long)strlen(PC_MENU_TAG_PREFIX) + (long)strlen(name) + (long)strlen(suffix) + 1);

	if (copy)
	{
		char *character;

		strcpy(copy, PC_MENU_TAG_PREFIX);
		strcat(copy, name);
		strcat(copy, suffix);
		/* (named as the map's tags are) */
		for (character = copy; *character; character++)
		{
			if (*character == '/')
				*character = '\\';
		}
	}
	instance->group_tag = group_tag;
	instance->parent_group_tags[0] = NONE;
	instance->parent_group_tags[1] = NONE;
	instance->tag_index = tag_index;
	/* (the name is read by tag_loaded: of none, when the copy failed, which
	fails the build) */
	instance->name = copy ? copy : "";
	instance->base_address = definition;
}

static void menu_tags_release(void)
{
	long index;

	for (index = 0; index < menu_tags.bitmap_count; index++)
	{
		if (menu_tags.bitmaps[index]->hardware_format)
			rasterizer_bitmap_delete(menu_tags.bitmaps[index]);
	}
	if (menu_tags.original_instances)
		cache_files_set_tag_instances(menu_tags.original_instances, menu_tags.original_count);
	for (index = 0; index < menu_tags.block_count; index++)
		free(menu_tags.blocks[index]);
	if (menu_tags.blocks)
		free(menu_tags.blocks);
	if (menu_tags.bitmaps)
		free(menu_tags.bitmaps);
	if (menu_tags.bitmap_resources)
		free(menu_tags.bitmap_resources);
	if (menu_tags.string_resources)
		free(menu_tags.string_resources);
	if (menu_tags.png_frames)
		free(menu_tags.png_frames);
	if (menu_tags.fit_bitmaps)
		free(menu_tags.fit_bitmaps);
	memset(&menu_tags, 0, sizeof(menu_tags));
	menu_tags.root_tag = menu_tags.xbox_root_tag = NONE;
}

/* (Menu style Halo CE's additions) the Xbox screens' widgets the port adds
rows to: ours while Menu style is Halo CE, the map's own while it is Halo 3
(a style changed in the settings panel takes at the next screen, as the Halo
3 set's does) */
static void classic_patches_apply(void)
{
	int classic = !halo_menu_style_halo3();
	struct cache_file_tag_instance *instances;
	long count, index;

	if (!menu_tags.classic_count || menu_tags.classic_applied == (classic ? 1 : -1))
		return;
	instances = cache_files_tag_instances(&count);
	if (!instances)
		return;
	for (index = 0; index < menu_tags.classic_count; index++)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(menu_tags.classic_tags[index]);

		if (absolute >= 0 && absolute < count)
			instances[absolute].base_address = classic ? menu_tags.classic_patched[index] : menu_tags.classic_original[index];
	}
	menu_tags.classic_applied = classic ? 1 : -1;
	platform_log("menus: Menu style %s: the Xbox screens' %ld widgets %s", classic ? "Halo CE" : "Halo 3",
		menu_tags.classic_count, classic ? "with the port's rows" : "as the map has them");
}

/* ---------- the player's Halo PC data */

struct menu_resource_file
{
	HANDLE handle;
	struct cache_file_source source;
	struct resource_map map;
	boolean opened;
	enum resource_map_type type;
};

static int menu_resource_read(void *context, uint32_t offset, uint32_t size, void *buffer)
{
	struct menu_resource_file *file = context;
	uint32_t done = 0;

	/* (read ahead once ui.map loaded: halo_menus_art_preread) */
	if (halo_menus_art_take(file->type, offset, size, buffer))
		return TRUE;
	while (done < size)
	{
		OVERLAPPED position;
		DWORD read = 0;

		memset(&position, 0, sizeof(position));
		position.Offset = offset + done;
		if (!ReadFile(file->handle, (byte *)buffer + done, size - done, &read, &position) || !read)
			break;
		done += read;
	}
	return done == size;
}

/* the maps folder's <name>.map, a Custom Edition resource map of the type */
static boolean menu_resource_open(struct menu_resource_file *file, char const *name, enum resource_map_type type,
	boolean index)
{
	char path[300];
	DWORD size;
	enum cache_file_status status;

	memset(file, 0, sizeof(*file));
	/* (the maps folder's, else the Halo content folder's custom_edition one) */
	platform_halo_pc_resource_map(cache_files_map_directory(), name, path, sizeof(path));
	file->handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (file->handle == INVALID_HANDLE_VALUE)
		return FALSE;
	size = GetFileSize(file->handle, NULL);
	if (size == INVALID_FILE_SIZE)
	{
		CloseHandle(file->handle);
		return FALSE;
	}
	file->type = type;
	file->source.context = file;
	file->source.read = menu_resource_read;
	file->source.size = (uint32_t)size;
	file->opened = TRUE;
	/* (only its header, while ui.map loads: is it one) */
	if (!index)
	{
		struct cache_file_identity identity;

		if (cache_file_identify(&file->source, &identity) != _cache_file_status_ok ||
			identity.format != _cache_file_format_resource_map || identity.resource_map_type != type)
		{
			platform_log("menus: %s is not a Halo Custom Edition %s", path, resource_map_type_describe(type));
			CloseHandle(file->handle);
			file->opened = FALSE;
			return FALSE;
		}
		return TRUE;
	}
	status = resource_map_open(&file->source, type, &file->map);
	if (status != _cache_file_status_ok)
	{
		platform_log("menus: %s: %s", path, cache_file_status_describe(status));
		CloseHandle(file->handle);
		file->opened = FALSE;
		return FALSE;
	}
	return TRUE;
}

static void menu_resource_close(struct menu_resource_file *file, boolean index)
{
	if (!file->opened)
		return;
	if (index)
		resource_map_close(&file->map);
	CloseHandle(file->handle);
	file->opened = FALSE;
}

static struct resource_map_item const *menu_resource_find(struct menu_resource_file *file, char const *path)
{
	int32_t index;

	for (index = 0; index < file->map.item_count; index++)
	{
		if (file->map.items[index].name && !strcmp(file->map.items[index].name, path))
			return &file->map.items[index];
	}
	return NULL;
}

static uint32_t read_u32(byte const *bytes)
{
	return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* fills a bitmap tag of ours with the frames of the player's bitmap */
static boolean menu_resource_bitmap_load(struct menu_resource_file *file, struct menu_resource const *resource)
{
	struct resource_map_item const *item = menu_resource_find(file, resource->path);
	struct bitmap_group *group = tag_get(BITMAP_GROUP_TAG, resource->tag_index);
	byte *tag;
	uint32_t count, address;
	long frame;

	if (!item || item->size < RESOURCE_BITMAP_BITMAPS_OFFSET + 8 || item->size > 0x10000)
	{
		halo_menus_log(resource->file, resource->line, "the Halo PC bitmaps.map has no bitmap", resource->path);
		return FALSE;
	}
	tag = malloc(item->size);
	if (!tag || !file->source.read(file->source.context, item->data_offset, item->size, tag))
	{
		free(tag);
		return FALSE;
	}
	count = read_u32(tag + RESOURCE_BITMAP_BITMAPS_OFFSET);
	address = read_u32(tag + RESOURCE_BITMAP_BITMAPS_OFFSET + 4);
	if (count < 1 || count > MAXIMUM_RESOURCE_FRAMES || address > item->size ||
		count * RESOURCE_BITMAP_DATA_BYTES > item->size - address)
	{
		halo_menus_log(resource->file, resource->line, "the Halo PC bitmap is not one these menus read:",
			resource->path);
		free(tag);
		return FALSE;
	}
	{
		struct bitmap_group *grown = bitmap_group_new((long)count);

		if (!grown || !grown->bitmaps.address || build.failed)
		{
			free(tag);
			return FALSE;
		}
		group->sequences = grown->sequences;
		group->bitmaps = grown->bitmaps;
	}
	for (frame = 0; frame < (long)count; frame++)
	{
		struct bitmap_data *bitmap = (struct bitmap_data *)group->bitmaps.address + frame;
		struct bitmap_data pc;
		void *pixels;

		memcpy(&pc, tag + address + frame * RESOURCE_BITMAP_DATA_BYTES, sizeof(pc));
		/* (2D pictures of the formats the menus have, power-of-two sized) */
		if (pc.type != _bitmap_type_2d || pc.width <= 0 || pc.height <= 0 || pc.width > 2048 || pc.height > 2048 ||
			(pc.width & (pc.width - 1)) || (pc.height & (pc.height - 1)) || pc.depth != 1 ||
			pc.format < 0 || pc.format > _bitmap_format_dxt5 || pc.mipmap_count < 0 || pc.mipmap_count > 11 ||
			pc.pixels_size <= 0 || pc.pixels_size > MAXIMUM_RESOURCE_BYTES || pc.pixels_offset < 0 ||
			(uint32_t)pc.pixels_offset > file->source.size ||
			(uint32_t)pc.pixels_size > file->source.size - (uint32_t)pc.pixels_offset)
		{
			halo_menus_log(resource->file, resource->line, "a frame of the Halo PC bitmap is not one these menus read:",
				resource->path);
			free(tag);
			return FALSE;
		}
		memset(bitmap, 0, sizeof(*bitmap));
		bitmap->signature = BITMAP_GROUP_TAG;
		bitmap->width = pc.width;
		bitmap->height = pc.height;
		bitmap->depth = 1;
		bitmap->type = _bitmap_type_2d;
		bitmap->format = pc.format;
		/* (the Xbox's meaning of the PC version's flags: power of two,
		compressed; its others - external, and a bit that is the Xbox's
		"cached" - are not ours) */
		bitmap->flags = (unsigned short)(pc.flags & (FLAG(_bitmap_has_power_of_two_dimensions_bit) |
			FLAG(_bitmap_compressed_bit)));
		bitmap->registration_point = pc.registration_point;
		bitmap->mipmap_count = pc.mipmap_count;
		bitmap->tag_index = resource->tag_index;
		bitmap->cache_block_index = NONE;
		/* (its texture holds as many bytes as its levels need) */
		if ((unsigned long)pc.pixels_size < (unsigned long)bitmap_get_pixel_data_size(bitmap))
		{
			halo_menus_log(resource->file, resource->line, "a frame of the Halo PC bitmap is short:", resource->path);
			free(tag);
			return FALSE;
		}
		pixels = malloc(pc.pixels_size);
		if (!pixels || !file->source.read(file->source.context, (uint32_t)pc.pixels_offset, (uint32_t)pc.pixels_size,
			pixels))
		{
			free(pixels);
			free(tag);
			return FALSE;
		}
		/* (the pixels as the tag has them, unswizzled; the texture's are
		made of them as the game makes any changed bitmap's) */
		bitmap->base_address = pixels;
		if (!rasterizer_bitmap_new(bitmap) || !bitmap->hardware_format)
		{
			bitmap->base_address = NULL;
			free(pixels);
			free(tag);
			return FALSE;
		}
		rasterizer_bitmap_changed(bitmap);
		bitmap->base_address = NULL;
		free(pixels);
		if (!array_grow((void **)&menu_tags.bitmaps, menu_tags.bitmap_count, sizeof(*menu_tags.bitmaps)))
		{
			rasterizer_bitmap_delete(bitmap);
			free(tag);
			return FALSE;
		}
		menu_tags.bitmaps[menu_tags.bitmap_count++] = bitmap;
	}
	free(tag);
	return TRUE;
}

/* fills a string list tag of ours with the player's string list */
static boolean menu_resource_strings_load(struct menu_resource_file *file, struct menu_resource const *resource)
{
	struct resource_map_item const *item = menu_resource_find(file, resource->path);
	struct string_list *list = tag_get(UNICODE_STRING_LIST_TAG, resource->tag_index);
	struct string_list_entry *entries;
	byte *tag;
	uint32_t count, address, index;

	if (!item || item->size < 12 || item->size > 0x40000)
	{
		halo_menus_log(resource->file, resource->line, "the Halo PC loc.map has no string list", resource->path);
		return FALSE;
	}
	tag = allocate((long)item->size + 2);
	if (!tag || !file->source.read(file->source.context, item->data_offset, item->size, tag))
		return FALSE;
	count = read_u32(tag);
	address = read_u32(tag + 4);
	if (count > MAXIMUM_STRINGS * 4 || address > item->size || count * RESOURCE_STRING_ENTRY_BYTES > item->size - address)
	{
		halo_menus_log(resource->file, resource->line, "the Halo PC string list is not one these menus read:",
			resource->path);
		return FALSE;
	}
	entries = allocate((long)(count ? count : 1) * sizeof(*entries));
	if (!entries)
		return FALSE;
	for (index = 0; index < count; index++)
	{
		byte const *entry = tag + address + index * RESOURCE_STRING_ENTRY_BYTES;
		uint32_t size = read_u32(entry), text = read_u32(entry + 12);

		/* (UTF-16, ended by a zero within its size) */
		if ((size & 1) || size < 2 || text > item->size || size > item->size - text ||
			tag[text + size - 1] || tag[text + size - 2])
		{
			halo_menus_log(resource->file, resource->line, "a string of the Halo PC string list is not one these menus read:",
				resource->path);
			return FALSE;
		}
		entries[index].string.size = (long)size;
		entries[index].string.address = tag + text;
	}
	list->strings.count = (long)count;
	list->strings.address = entries;
	return TRUE;
}

/* ---------- OpenCE's art: PNGs (8-bit RGB or RGBA, not interlaced) */

static uint32_t read_u32_big(byte const *bytes)
{
	return ((uint32_t)bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
}

static long paeth(long a, long b, long c)
{
	long p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);

	return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* the PNG's pixels, as width x height A8R8G8B8 (B, G, R, A in memory);
NULL if it is not such a PNG, or of another size */
static byte *png_decode(byte const *data, unsigned long size, long width, long height)
{
	static byte const signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	unsigned long position = 8, idat_size = 0;
	long channels = 0, row_bytes, x, y;
	byte *idat = NULL, *raw = NULL, *pixels = NULL;
	z_stream stream;
	boolean ended = FALSE;

	if (size < 8 || memcmp(data, signature, 8))
		return NULL;
	while (position + 12 <= size && !ended)
	{
		uint32_t length = read_u32_big(data + position);
		byte const *type = data + position + 4, *chunk = data + position + 8;

		if (length > size - position - 12)
			break;
		if (!memcmp(type, "IHDR", 4) && length >= 13)
		{
			/* (8 bits a channel, RGB or RGBA, the usual compression and
			filters, not interlaced) */
			if ((long)read_u32_big(chunk) != width || (long)read_u32_big(chunk + 4) != height || chunk[8] != 8 ||
				(chunk[9] != 2 && chunk[9] != 6) || chunk[10] || chunk[11] || chunk[12])
				break;
			channels = chunk[9] == 6 ? 4 : 3;
		}
		else if (!memcmp(type, "IDAT", 4) && channels)
		{
			byte *grown = realloc(idat, idat_size + length + 1);

			if (!grown)
				break;
			idat = grown;
			memcpy(idat + idat_size, chunk, length);
			idat_size += length;
		}
		else if (!memcmp(type, "IEND", 4))
			ended = TRUE;
		position += 12 + length;
	}
	if (!ended || !channels || !idat)
	{
		free(idat);
		return NULL;
	}
	row_bytes = width * channels;
	raw = malloc((row_bytes + 1) * height);
	pixels = malloc(width * height * 4);
	if (raw && pixels)
	{
		memset(&stream, 0, sizeof(stream));
		stream.next_in = idat;
		stream.avail_in = (uInt)idat_size;
		stream.next_out = raw;
		stream.avail_out = (uInt)((row_bytes + 1) * height);
		if (inflateInit(&stream) == Z_OK)
		{
			int result = inflate(&stream, Z_FINISH);

			inflateEnd(&stream);
			if (result != Z_STREAM_END || stream.total_out != (uLong)((row_bytes + 1) * height))
				height = 0;
		}
		else
			height = 0;
		/* the filters undone, in place */
		for (y = 0; y < height; y++)
		{
			byte *row = raw + y * (row_bytes + 1) + 1;
			byte const *above = y ? row - (row_bytes + 1) : NULL;
			byte filter = row[-1];

			for (x = 0; x < row_bytes; x++)
			{
				long left = x >= channels ? row[x - channels] : 0;
				long up = above ? above[x] : 0;
				long corner = above && x >= channels ? above[x - channels] : 0;

				switch (filter)
				{
				case 1: row[x] = (byte)(row[x] + left); break;
				case 2: row[x] = (byte)(row[x] + up); break;
				case 3: row[x] = (byte)(row[x] + ((left + up) >> 1)); break;
				case 4: row[x] = (byte)(row[x] + paeth(left, up, corner)); break;
				default: break;
				}
			}
			for (x = 0; x < width; x++)
			{
				byte *out = pixels + (y * width + x) * 4;

				out[0] = row[x * channels + 2];
				out[1] = row[x * channels + 1];
				out[2] = row[x * channels];
				out[3] = channels == 4 ? row[x * channels + 3] : 0xFF;
			}
		}
	}
	free(idat);
	free(raw);
	if (!height)
	{
		free(pixels);
		return NULL;
	}
	return pixels;
}

/* a PNG's path with "{lang}" in it: the language's (lang_current, as the
menus load: menus/classic's words drawn in each language), else English's
(en) - the language having none of its own; NULL: the path as written */
static char const *menu_png_language_path(char const *png, char const *code, char *path, int size)
{
	char const *at = strstr(png, "{lang}");

	if (!at)
		return NULL;
	snprintf(path, (size_t)size, "%.*s%s%s", (int)(at - png), png, code, at + 6);
	return path;
}

unsigned char *menu_words_frame(char const *png, long width, long height);

static boolean menu_png_frame_load(struct menu_png_frame const *frame)
{
	struct bitmap_data *bitmap = frame->bitmap;
	unsigned long size = 0;
	char path[256];
	char const *png = menu_png_language_path(frame->png, lang_current(), path, (int)sizeof(path));
	byte *data = png ? NULL : halo_menus_art_png(frame->png, &size);
	byte *pixels;

	/* (the main menu's new rows: the game's own letters, menu_words.c) */
	pixels = menu_words_frame(frame->png, bitmap->width, bitmap->height);
	if (pixels)
	{
		if (data)
			halo_menus_file_free(data);
		goto made;
	}
	if (png)
	{
		data = halo_menus_file_read(png, &size);
		if (!data)
			data = halo_menus_file_read(menu_png_language_path(frame->png, "en", path, (int)sizeof(path)), &size);
	}
	else if (!data)
		data = halo_menus_file_read(frame->png, &size);
	pixels = data && size <= MAXIMUM_PNG_BYTES ? png_decode(data, size, bitmap->width, bitmap->height) : NULL;

	halo_menus_file_free(data);
	if (!pixels)
	{
		halo_menus_log(frame->file, frame->line, "a PNG of the menus folder cannot be read (8-bit RGB or RGBA, "
			"the frame's size):", frame->png);
		return FALSE;
	}
made:
	bitmap->base_address = pixels;
	if (!rasterizer_bitmap_new(bitmap) || !bitmap->hardware_format)
	{
		bitmap->base_address = NULL;
		free(pixels);
		return FALSE;
	}
	rasterizer_bitmap_changed(bitmap);
	bitmap->base_address = NULL;
	free(pixels);
	if (!array_grow((void **)&menu_tags.bitmaps, menu_tags.bitmap_count, sizeof(*menu_tags.bitmaps)))
		return FALSE;
	menu_tags.bitmaps[menu_tags.bitmap_count++] = bitmap;
	return TRUE;
}

/* the pictures and the text of the player's, and the PNGs: the first time
the screens open */
static boolean menu_tags_art_load(void)
{
	struct menu_resource_file bitmaps, locale;
	unsigned long long started = vita_host_time_us();
	unsigned long heap_before, heap_capacity, window_before, window_free, heap_after, window_after;
	boolean success = TRUE;
	long index;

	/* (what was read ahead, once read: halo_menus_art_preread) */
	halo_menus_art_wait();
	platform_heap_usage(&heap_before, &heap_capacity);
	platform_contiguous_usage(&window_before, &window_free);
	if (!menu_resource_open(&bitmaps, "bitmaps", _resource_map_bitmaps, TRUE))
	{
		halo_menus_art_release();
		return FALSE;
	}
	if (!menu_resource_open(&locale, "loc", _resource_map_locale, TRUE))
	{
		menu_resource_close(&bitmaps, TRUE);
		halo_menus_art_release();
		return FALSE;
	}
	for (index = 0; index < menu_tags.bitmap_resource_count && success; index++)
		success = menu_resource_bitmap_load(&bitmaps, &menu_tags.bitmap_resources[index]);
	for (index = 0; index < menu_tags.string_resource_count && success; index++)
		success = menu_resource_strings_load(&locale, &menu_tags.string_resources[index]);
	for (index = 0; index < menu_tags.png_frame_count && success; index++)
		success = menu_png_frame_load(&menu_tags.png_frames[index]);
	menu_resource_close(&bitmaps, TRUE);
	menu_resource_close(&locale, TRUE);
	success = success && !build.failed;
	platform_heap_usage(&heap_after, &heap_capacity);
	platform_contiguous_usage(&window_after, &window_free);
	/* (after the measure: what was read ahead was there before it too) */
	halo_menus_art_release();
	platform_log("menus: the Halo PC pictures and text %s in %lu ms (%ld bitmaps, %ld string lists, %ld PNGs); "
		"C heap %+ld KB, memory window %+ld KB (%lu KB free)",
		success ? "read" : "could not be read", (unsigned long)((vita_host_time_us() - started) / 1000),
		menu_tags.bitmap_resource_count, menu_tags.string_resource_count, menu_tags.png_frame_count,
		((long)heap_after - (long)heap_before) / 1024, ((long)window_after - (long)window_before) / 1024,
		window_free / 1024);
	return success;
}

/* the reads menu_tags_art_load will make, begun on a thread of their own
now that the screens are added (halo_menus_art_preread) */
static void menu_tags_art_preread(void)
{
	struct halo_menus_art_plan plan;
	char const **paths;
	long count = menu_tags.bitmap_resource_count + menu_tags.string_resource_count + menu_tags.png_frame_count;
	long index, at = 0;

	if (menu_tags.art)
		return;
	paths = malloc((count ? count : 1) * sizeof(*paths));
	if (!paths)
		return;
	memset(&plan, 0, sizeof(plan));
	plan.map_directory = cache_files_map_directory();
	plan.bitmap_paths = paths + at;
	for (index = 0; index < menu_tags.bitmap_resource_count; index++)
		paths[at++] = menu_tags.bitmap_resources[index].path;
	plan.bitmap_count = menu_tags.bitmap_resource_count;
	plan.string_paths = paths + at;
	for (index = 0; index < menu_tags.string_resource_count; index++)
		paths[at++] = menu_tags.string_resources[index].path;
	plan.string_count = menu_tags.string_resource_count;
	plan.png_names = paths + at;
	/* (a language's PNG, "{lang}": read when it loads, the language's then) */
	plan.png_count = 0;
	for (index = 0; index < menu_tags.png_frame_count; index++)
	{
		if (!strstr(menu_tags.png_frames[index].png, "{lang}"))
		{
			paths[at++] = menu_tags.png_frames[index].png;
			plan.png_count++;
		}
	}
	plan.bitmaps_block_offset = RESOURCE_BITMAP_BITMAPS_OFFSET;
	plan.bitmap_data_bytes = RESOURCE_BITMAP_DATA_BYTES;
	plan.pixels_offset_offset = offsetof(struct bitmap_data, pixels_offset);
	plan.pixels_size_offset = offsetof(struct bitmap_data, pixels_size);
	plan.maximum_frames = MAXIMUM_RESOURCE_FRAMES;
	plan.maximum_pixel_bytes = MAXIMUM_RESOURCE_BYTES;
	plan.maximum_tag_bytes = 0x10000;
	halo_menus_art_preread(&plan);
	free(paths);
}

/* whether the maps folder (or the Halo content folder's custom_edition) has a
bitmaps.map and a loc.map (a quick look:
no file opened) */
static boolean menu_resource_files_present(void)
{
	static char const *const names[] = { "bitmaps", "loc" };
	int index;

	for (index = 0; index < 2; index++)
	{
		char path[300];
		DWORD attributes;

		platform_halo_pc_resource_map(cache_files_map_directory(), names[index], path, sizeof(path));
		attributes = GetFileAttributesA(path);
		if (attributes == (DWORD)-1 || (attributes & FILE_ATTRIBUTE_DIRECTORY))
			return FALSE;
	}
	return TRUE;
}

/* ---------- public code */

/* (hud_draw.c: the in-game PlayStation button icons, halo3/glyph_atlas.png)
a PNG of the menus folder, `width` x `height` (powers of two), made a
texture of its own in `bitmap` with its mipmaps down to `smallest` pixels
across (each the average of four of the one above), kept by the caller
for as long as it likes: no map's tags hold it, so no map's unloading
lets it go. FALSE (and `bitmap` cleared) if the PNG cannot be read or made
a texture. The render thread's, with the rasterizer up */
boolean menu_tags_png_texture(
	char const *png,
	long width,
	long height,
	long smallest,
	struct bitmap_data *bitmap)
{
	unsigned long size = 0;
	byte *data;
	byte *pixels;
	byte *mipmapped;
	long level_width, level_height, total, levels;

	memset(bitmap, 0, sizeof(*bitmap));
	if (width <= 0 || height <= 0 || width > 2048 || height > 2048 || (width & (width - 1)) ||
		(height & (height - 1)))
		return FALSE;
	data = halo_menus_file_read(png, &size);
	pixels = data && size <= MAXIMUM_PNG_BYTES ? png_decode(data, size, width, height) : NULL;
	halo_menus_file_free(data);
	if (!pixels)
	{
		platform_log("menus: %s cannot be read (an 8-bit RGB or RGBA PNG of %ldx%ld)", png, width, height);
		return FALSE;
	}
	/* the levels, one after another (bitmap_mipmap_address) */
	total = 0;
	levels = 0;
	for (level_width = width, level_height = height;; level_width /= 2, level_height /= 2)
	{
		total += level_width * level_height * 4;
		if (level_width / 2 < MAX(smallest, 1) || level_height / 2 < 1)
			break;
		levels++;
	}
	mipmapped = realloc(pixels, total);
	if (!mipmapped)
	{
		free(pixels);
		return FALSE;
	}
	pixels = mipmapped;
	{
		byte *above = pixels;
		long level;

		level_width = width;
		level_height = height;
		for (level = 0; level < levels; level++)
		{
			byte *below = above + level_width * level_height * 4;
			long x, y, channel;

			for (y = 0; y < level_height / 2; y++)
			{
				for (x = 0; x < level_width / 2; x++)
				{
					byte const *top = above + ((y * 2) * level_width + x * 2) * 4;
					byte const *bottom = top + level_width * 4;

					for (channel = 0; channel < 4; channel++)
						below[(y * (level_width / 2) + x) * 4 + channel] =
							(byte)((top[channel] + top[4 + channel] + bottom[channel] + bottom[4 + channel] + 2) / 4);
				}
			}
			above = below;
			level_width /= 2;
			level_height /= 2;
		}
	}
	bitmap->signature = BITMAP_GROUP_TAG;
	bitmap->width = (short)width;
	bitmap->height = (short)height;
	bitmap->depth = 1;
	bitmap->type = _bitmap_type_2d;
	bitmap->format = _bitmap_format_a8r8g8b8;
	bitmap->flags = FLAG(_bitmap_has_power_of_two_dimensions_bit);
	bitmap->mipmap_count = (short)levels;
	bitmap->tag_index = NONE;
	bitmap->cache_block_index = NONE;
	bitmap->pixels_size = total;
	bitmap->base_address = pixels;
	if (!rasterizer_bitmap_new(bitmap) || !bitmap->hardware_format)
	{
		free(pixels);
		memset(bitmap, 0, sizeof(*bitmap));
		return FALSE;
	}
	rasterizer_bitmap_changed(bitmap);
	bitmap->base_address = NULL;
	free(pixels);
	return TRUE;
}

/* (scenario_tags_load, before ui.map's tags are read) the menus' XML read
on a thread of its own meanwhile, if the Halo PC files are there */
void menu_tags_preload(
	char const *map_name)
{
	if (!strcmp(map_name, "ui") && !(getenv("HALO_MENUS") && !strcmp(getenv("HALO_MENUS"), "xbox")) &&
		menu_resource_files_present())
	{
		halo_menus_preload();
	}
}

/* (menu_functions.c: the tags it keeps the indices of, looked up again
when this changes) the maps' tags loaded or let go since the start */
static long menu_tags_changes;

/* ui.map's tags loaded: ours added, if the player's Halo PC data is there
(only their names and the XML: the rest at first use). A level's map (any
other): the in-game set (ingame.txt: the Halo 3 pause screen), the same way,
with no Multiplayer screen */
void menu_tags_loaded(
	char const *map_name)
{
	struct halo_menus const *menus;
	struct cache_file_tag_instance *instances;
	long widget_count, own_lists = 0, total, index;
	unsigned long long started = vita_host_time_us(), checked, read;
	boolean ingame = strcmp(map_name, "ui") != 0;

	menu_tags_changes++;
	menu_tags.root_tag = menu_tags.xbox_root_tag = NONE;
	menu_tags.classic_count = 0;
	menu_tags.classic_applied = 0;
	/* (until they are added: the Xbox's menus) */
	if (!ingame)
		halo_pc_menus_state = -1;
	if (getenv("HALO_MENUS") && !strcmp(getenv("HALO_MENUS"), "xbox"))
		return;
	/* (the player's Halo PC data: only that the files are there; what they
	are, when the screens first open) */
	if (!menu_resource_files_present())
		return;
	checked = vita_host_time_us();
	menus = ingame ? halo_menus_load_ingame() : halo_menus_load();
	if (!menus)
		return;
	read = vita_host_time_us();
	memset(&build, 0, sizeof(build));
	build.menus = menus;
	build.ingame = ingame;
	widget_count = menus->widget_count;
	build.widget_tags = malloc((widget_count + 1) * sizeof(long));
	build.text_tags = malloc((widget_count + 1) * sizeof(long));
	build.spinner_tags = malloc((widget_count + 1) * sizeof(long));
	build.bitmap_tags = malloc((menus->bitmap_count + 1) * sizeof(long));
	build.font_tags = malloc((menus->font_count + 1) * sizeof(long));
	build.strings_tags = malloc((menus->string_list_count + 1) * sizeof(long));
	if (!build.widget_tags || !build.text_tags || !build.spinner_tags || !build.bitmap_tags || !build.strings_tags)
		goto failed;
	for (index = 0; index < widget_count; index++)
	{
		own_lists += (menus->widgets[index].text != NULL) + (menus->widgets[index].strings != NULL);
		/* (names are unique) */
		if (widget_named(menus->widgets[index].name) != index)
			problem(menus->widgets[index].file, menus->widgets[index].line, "two widgets are named",
				menus->widgets[index].name);
	}
	menu_tags.xbox_root_tag = ingame ? NONE : tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER_SCREEN);
	if (!ingame && (menu_tags.xbox_root_tag == NONE || widget_named(menus->root) == NONE))
	{
		platform_log("menus: there is no screen %s (the Xbox's, or ours: %s)", XBOX_MULTIPLAYER_SCREEN, menus->root);
		goto failed;
	}
	total = widget_count + own_lists + menus->string_list_count + menus->bitmap_count + menus->font_count;
	instances = build.failed ? NULL : instances_grow(total, &build.first_index, &build.first_salt);
	if (!instances)
		goto failed;
	/* each new tag's index */
	for (index = 0; index < widget_count; index++)
	{
		build.widget_tags[index] = next_tag();
		build.text_tags[index] = menus->widgets[index].text ? next_tag() : NONE;
		build.spinner_tags[index] = menus->widgets[index].strings ? next_tag() : NONE;
	}
	for (index = 0; index < menus->string_list_count; index++)
		build.strings_tags[index] = next_tag();
	for (index = 0; index < menus->bitmap_count; index++)
		build.bitmap_tags[index] = next_tag();
	for (index = 0; index < menus->font_count; index++)
		build.font_tags[index] = next_tag();
	/* the tags: the bitmaps and strings first, which the widgets name; the
	widgets' names in the table before any is built, which they find there */
	for (index = 0; index < menus->bitmap_count && !build.failed; index++)
	{
		instance_set(instances, BITMAP_GROUP_TAG, build.bitmap_tags[index], menus->bitmaps[index].name, "",
			bitmap_build(&menus->bitmaps[index], build.bitmap_tags[index]));
	}
	for (index = 0; index < menus->font_count && !build.failed; index++)
	{
		instance_set(instances, FONT_TAG, build.font_tags[index], menus->fonts[index].name, "",
			font_build(&menus->fonts[index]));
	}
	for (index = 0; index < menus->string_list_count && !build.failed; index++)
	{
		struct halo_menu_strings const *list = &menus->string_lists[index];

		if (list->resource)
		{
			struct menu_resource *resource;

			if (!array_grow((void **)&menu_tags.string_resources, menu_tags.string_resource_count,
				sizeof(*menu_tags.string_resources)))
				break;
			resource = &menu_tags.string_resources[menu_tags.string_resource_count++];
			resource->path = list->resource;
			resource->tag_index = build.strings_tags[index];
			resource->file = list->file;
			resource->line = list->line;
		}
		instance_set(instances, UNICODE_STRING_LIST_TAG, build.strings_tags[index], list->name, "",
			list->resource ? string_list_build(NULL, 0) : string_list_build(menus->strings + list->first, list->count));
	}
	for (index = 0; index < widget_count && !build.failed; index++)
	{
		struct halo_menu_widget const *widget = &menus->widgets[index];

		if (widget->text)
		{
			instance_set(instances, UNICODE_STRING_LIST_TAG, build.text_tags[index], widget->name, " text",
				string_list_build(&widget->text, 1));
		}
		if (widget->strings)
		{
			char const *pieces[MAXIMUM_STRINGS];
			long count = split(widget->strings, pieces);

			instance_set(instances, UNICODE_STRING_LIST_TAG, build.spinner_tags[index], widget->name, " strings",
				string_list_build(pieces, count));
		}
		instance_set(instances, UI_WIDGET_DEFINITION_TAG, build.widget_tags[index], widget->name, "", NULL);
	}
	if (build.failed)
		goto failed;
	cache_files_set_tag_instances(instances, build.first_index + total);
	for (index = 0; index < widget_count && !build.failed; index++)
		instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(build.widget_tags[index])].base_address = widget_build(index);
	if (build.failed)
		goto failed;
	menu_tags.root_tag = ingame ? NONE : build.widget_tags[widget_named(menus->root)];
	/* (the Halo 3 set: each screen and the Xbox screen it stands for, both
	there) */
	menu_tags.h3_count = 0;
	menu_tags.h3_left_out_said = FALSE;
	for (index = 0; index < menus->screen_count && menu_tags.h3_count < MAXIMUM_H3_SCREENS; index++)
	{
		struct halo_menu_screen const *screen = &menus->screens[index];
		long from = tag_loaded(UI_WIDGET_DEFINITION_TAG, screen->replaces);
		long widget = widget_named(screen->root);

		if (from == NONE || widget == NONE)
		{
			/* (a level's map without the screen: a multiplayer map has no
			campaign pause screen to stand for) */
			if (ingame && from == NONE && widget != NONE)
				platform_log("menus: this map has no %s: %s stands for nothing here", screen->replaces, screen->root);
			else
				problem(screen->file, screen->line, from == NONE ? "the map has no screen" : "no widget is named",
					from == NONE ? screen->replaces : screen->root);
			continue;
		}
		menu_tags.h3_from[menu_tags.h3_count] = from;
		menu_tags.h3_to[menu_tags.h3_count] = build.widget_tags[widget];
		menu_tags.h3_root[menu_tags.h3_count] = screen->root;
		menu_tags.h3_replaces[menu_tags.h3_count] = screen->replaces;
		menu_tags.h3_count++;
	}
	/* (the online set: an online/<name> widget stands for h3/<name>, when
	Menu style is Online, as it opens: menu_tags_online) */
	menu_tags.online_count = 0;
	for (index = 0; index < widget_count; index++)
	{
		char const *name = menus->widgets[index].name;
		char h3_name[160];
		long h3_widget;

		if (!name || strncmp(name, "online/", 7))
			continue;
		snprintf(h3_name, sizeof(h3_name), "h3/%s", name + 7);
		if ((h3_widget = widget_named(h3_name)) == NONE)
			continue;
		if (menu_tags.online_count == MAXIMUM_ONLINE_SCREENS)
		{
			problem(menus->widgets[index].file, menus->widgets[index].line, "too many online widgets stand for "
				"Halo 3 ones", name);
			break;
		}
		menu_tags.online_said[menu_tags.online_count] = FALSE;
		menu_tags.online_from[menu_tags.online_count] = build.widget_tags[h3_widget];
		menu_tags.online_to[menu_tags.online_count] = build.widget_tags[index];
		menu_tags.online_count++;
	}
	if (menu_tags.online_count)
		platform_log("menus: online set: %ld widgets stand for Halo 3 ones (Menu style %s)", menu_tags.online_count,
			halo_menu_style_online() ? "Online" : "not Online");
	/* (debug: HALO_MENUS_DUMP_TAGS=<path>;<path>...: those screens of the
	map's dumped as it loads, as HALO_MENUS_DUMP dumps one as it opens) */
	if (getenv("HALO_MENUS_DUMP_TAGS"))
	{
		char paths[1024];
		char *path, *rest;

		snprintf(paths, sizeof(paths), "%s", getenv("HALO_MENUS_DUMP_TAGS"));
		for (path = strtok_r(paths, ";", &rest); path; path = strtok_r(NULL, ";", &rest))
		{
			long tag = tag_loaded(UI_WIDGET_DEFINITION_TAG, path);

			if (tag != NONE)
				menu_tags_dump(tag, 0);
		}
	}
	if (menus->screen_count)
		platform_log("menus: Halo 3 set: %ld of %ld screens (Menu style %s)", menu_tags.h3_count, menus->screen_count,
			halo_menu_style_halo3() ? "Halo 3" : "Halo CE");
	menu_tags.loaded = TRUE;
	classic_patches_apply();
	if (!ingame)
		halo_pc_menus_state = 1;
	menu_tags_art_preread();
	platform_log("menus: %s: %ld widgets, %ld string lists and %ld bitmaps added to "
		"%s.map's %ld tags in %lu us (the Halo PC files checked %lu, the XML read %lu, the tags %lu; the pictures "
		"and text are read when they first open)", ingame ? "the in-game screens" : "OpenCE's multiplayer screens",
		widget_count, own_lists + menus->string_list_count, menus->bitmap_count, map_name, build.first_index,
		(unsigned long)(vita_host_time_us() - started), (unsigned long)(checked - started),
		(unsigned long)(read - checked), (unsigned long)(vita_host_time_us() - read));
	goto done;

failed:
	platform_log("menus: %s not added; the game's own", ingame ? "the in-game screens" : "OpenCE's multiplayer screens");
	menu_tags_release();

done:
	free(build.widget_tags);
	free(build.text_tags);
	free(build.spinner_tags);
	free(build.strings_tags);
	free(build.bitmap_tags);
	free(build.font_tags);
	memset(&build, 0, sizeof(build));
}

long menu_tags_generation(
	void)
{
	return menu_tags_changes;
}

void menu_tags_unloaded(
	void)
{
	menu_tags_changes++;
	/* (what was read ahead and never taken) */
	halo_menus_art_release();
	if (menu_tags.loaded || menu_tags.block_count || menu_tags.original_instances)
		menu_tags_release();
}

/* (debug: HALO_MENUS_DUMP=<a screen's tag path>) that screen's widget tree as
ui.map has it, logged as it opens: each widget's type, bounds, picture,
text, data inputs, event handlers (what they run and open) and children -
what a Halo 3 screen that stands for it must do the same */
static char const *dump_name(struct tag_reference const *reference)
{
	return reference->index != NONE ? tag_get_name(reference->index) : "-";
}

static void menu_tags_dump(long tag_index, int depth)
{
	struct menu_widget_definition *definition = tag_get(UI_WIDGET_DEFINITION_TAG, tag_index);
	long index;

	if (!definition || depth > 9)
		return;
	platform_log("menus dump: %*s%s type %d at %d,%d-%d,%d flags %08lx bitmap %s text %s[%d] font %s auto close %ld/%ld",
		depth * 2, "", tag_get_name(tag_index), (int)definition->type, (int)definition->bounds.x0,
		(int)definition->bounds.y0, (int)definition->bounds.x1, (int)definition->bounds.y1,
		(unsigned long)definition->flags, dump_name(&definition->background_bitmap),
		dump_name(&definition->text_label_string_list), (int)definition->string_list_index,
		dump_name(&definition->text_font), definition->milliseconds_to_auto_close, definition->auto_close_fade_time);
	for (index = 0; index < definition->game_data_inputs.count; index++)
	{
		struct ui_widget_game_data_input_reference *input = TAG_BLOCK_GET_ELEMENT(&definition->game_data_inputs, index,
			struct ui_widget_game_data_input_reference);
		long function = input->function;

		platform_log("menus dump: %*s  data input %ld (%s)", depth * 2, "", function,
			function >= PC_MENU_FUNCTION_BASE ? pc_menu_game_data_input_name(function - PC_MENU_FUNCTION_BASE) :
			function >= 0 && function < (long)NUMBEROF(game_data_input_names) ? game_data_input_names[function] : "?");
	}
	for (index = 0; index < definition->event_handlers.count; index++)
	{
		struct ui_widget_event_handler_reference *handler = TAG_BLOCK_GET_ELEMENT(&definition->event_handlers, index,
			struct ui_widget_event_handler_reference);
		char const *function = handler->function >= PC_MENU_FUNCTION_BASE ?
			pc_menu_function_name(handler->function - PC_MENU_FUNCTION_BASE) :
			ui_widget_event_handler_function_name(handler->function);

		platform_log("menus dump: %*s  on event %d flags %08lx run %d (%s) widget %s script \"%.32s\"", depth * 2, "",
			(int)handler->event_type, (unsigned long)handler->flags, (int)handler->function, function ? function : "-",
			dump_name(&handler->widget_tag), handler->script);
		/* (and the widget it opens, once: HALO_MENUS_DUMP_OPENED=1) */
		if (handler->widget_tag.index != NONE && getenv("HALO_MENUS_DUMP_OPENED"))
		{
			static long dumped[64];
			static int dumped_count;
			int seen;

			for (seen = 0; seen < dumped_count && dumped[seen] != handler->widget_tag.index; seen++)
				;
			if (seen == dumped_count && dumped_count < (int)NUMBEROF(dumped))
			{
				dumped[dumped_count++] = handler->widget_tag.index;
				platform_log("menus dump: %*s  (it opens:)", depth * 2, "");
				menu_tags_dump(handler->widget_tag.index, depth + 1);
			}
		}
	}
	for (index = 0; index < definition->child_widgets.count; index++)
	{
		struct ui_widget_child_reference *child = TAG_BLOCK_GET_ELEMENT(&definition->child_widgets, index,
			struct ui_widget_child_reference);

		platform_log("menus dump: %*s  child at %d,%d flags %08lx:", depth * 2, "", (int)child->horizontal_offset,
			(int)child->vertical_offset, (unsigned long)child->flags);
		if (child->widget_tag.index != NONE)
			menu_tags_dump(child->widget_tag.index, depth + 1);
	}
}

/* (ui_widget.c, a widget's background) whether its bitmap is drawn whole
into the widget's bounds, scaled (<bitmap fit="widget">: a picture of more
pixels than its place in the 640x480 screen, sharp on the Vita's interface
layer), rather than 1:1 from its top left */
boolean menu_tags_bitmap_fits_widget(
	long bitmap_tag_index)
{
	long index;

	if (bitmap_tag_index == NONE)
		return FALSE;
	for (index = 0; index < menu_tags.fit_bitmap_count; index++)
	{
		if (menu_tags.fit_bitmaps[index] == bitmap_tag_index)
			return TRUE;
	}
	return FALSE;
}

/* (Menu style Online) the online set's widget for a Halo 3 one, when it has
one; else the tag itself. Read each time, as the Menu style is */
static long menu_tags_online(
	long tag_index)
{
	long index;

	if (tag_index == NONE || !menu_tags.online_count || !halo_menu_style_online())
		return tag_index;
	for (index = 0; index < menu_tags.online_count; index++)
	{
		if (menu_tags.online_from[index] == tag_index)
		{
			/* (said once a load: the flows ask for their screens by name
			every frame) */
			if (!menu_tags.online_said[index])
			{
				menu_tags.online_said[index] = TRUE;
				platform_log("menus: online screen %s for %s", tag_get_name(menu_tags.online_to[index]),
					tag_get_name(tag_index));
			}
			return menu_tags.online_to[index];
		}
	}
	return tag_index;
}

/* (ui_widget_load_by_name_or_tag) a screen the game opens: ours in place of
the Xbox's Multiplayer screen (the Halo PC pictures and text read the first
time), and those menu_functions.c's flows replace; else the tag itself */
long menu_tags_screen(
	long tag_index)
{
	if (tag_index != NONE && getenv("HALO_MENUS_DUMP") && !strcmp(getenv("HALO_MENUS_DUMP"), tag_get_name(tag_index)))
		menu_tags_dump(tag_index, 0);
	if (!menu_tags.loaded || tag_index == NONE)
		return tag_index;
	/* (the Xbox screens with Menu style Halo CE's rows, or without) */
	classic_patches_apply();
	/* (a Forge game: its menu for the level's pause screen, which would
	save the game and offer the campaign's checkpoints) */
	if (forge_game_active())
	{
		char const *pause = tag_index == tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_PAUSE_SCREEN) ? XBOX_PAUSE_SCREEN : NULL;
		short index;

		for (index = 0; !pause && index < (short)NUMBEROF(network_pause_screens); index++)
		{
			if (tag_index == tag_loaded(UI_WIDGET_DEFINITION_TAG, network_pause_screens[index]))
				pause = network_pause_screens[index];
		}
		if (pause)
		{
			long forge_menu = menu_tags_ingame_screen(FORGE_PAUSE_SCREEN);

			if (forge_menu != NONE)
			{
				platform_log("menus: the Forge menu for %s", pause);
				return forge_menu;
			}
		}
	}
	/* (Menu style Halo 3: the Halo 3 set's screen for the Xbox's, its
	pictures read the first time, as the Multiplayer screen's; read again
	each time, a change in the settings panel takes at the next screen) */
	if (halo_menu_style_halo3())
	{
		long index;
		/* (a network game's SELECT PROFILE, a profile chosen before this
		run: not asked again, the screen it goes on to in its place) */
		long skipped = menu_tags.root_tag != NONE ? pc_menu_functions_profile_skip(tag_index) : tag_index;

		if (skipped != tag_index)
			return menu_tags_screen(skipped);
		for (index = 0; index < menu_tags.h3_count; index++)
		{
			if (tag_index != menu_tags.h3_from[index])
				continue;
			if (forge_game_active() && !strcmp(menu_tags.h3_root[index], H3_MULTIPLAYER_PAUSE_SCREEN))
				return tag_index;
			if (!menu_tags.art)
				menu_tags.art = menu_tags_art_load() ? 1 : -1;
			if (menu_tags.art < 0)
			{
				if (!menu_tags.h3_left_out_said)
					platform_log("menus: Halo 3 set left out: its pictures could not be read; the game's own screens");
				menu_tags.h3_left_out_said = TRUE;
				/* (the settings panel's word on the online menus: ui.map's) */
				if (menu_tags.root_tag != NONE)
					halo_pc_menus_state = -1;
				return tag_index;
			}
			platform_log("menus: Halo 3 screen %s for %s", menu_tags.h3_root[index], menu_tags.h3_replaces[index]);
			return menu_tags_online(menu_tags.h3_to[index]);
		}
	}
	/* (a level's map has no Multiplayer screen: NONE) */
	if (tag_index != NONE && tag_index == menu_tags.xbox_root_tag && menu_tags.art >= 0)
	{
		if (!menu_tags.art)
		{
			menu_tags.art = menu_tags_art_load() ? 1 : -1;
			if (menu_tags.art < 0)
			{
				halo_pc_menus_state = -1;
				platform_log("menus: OpenCE's multiplayer screens left out; the game's own Multiplayer screen");
				return tag_index;
			}
		}
		return menu_tags.root_tag;
	}
	if (menu_tags.art > 0)
	{
		/* (a screen the flows open in another's place: the Halo 3 set's
		for it, when it has one - the game's lobby after a game hosted) */
		long replaced = pc_menu_functions_screen(tag_index);

		if (replaced != tag_index && replaced != NONE && halo_menu_style_halo3())
			return menu_tags_screen(replaced);
		return menu_tags_online(replaced);
	}
	return menu_tags_online(tag_index);
}

/* (forge.c) a screen of the set built into the loaded map, by its widget's
name, its pictures read the first time (as menu_tags_screen reads them for
the screens that stand for the game's); NONE if the set or the screen is not
there, or its pictures could not be read */
long menu_tags_ingame_screen(
	char const *name)
{
	long tag_index;
	char path[128];
	char *character;

	if (!menu_tags.loaded || !name)
		return NONE;
	/* (named as instance_set names ours) */
	snprintf(path, sizeof(path), "%s%s", PC_MENU_TAG_PREFIX, name);
	for (character = path; *character; character++)
	{
		if (*character == '/')
			*character = '\\';
	}
	tag_index = tag_loaded(UI_WIDGET_DEFINITION_TAG, path);
	if (tag_index == NONE || !pc_menu_tag(tag_index))
		return NONE;
	if (!menu_tags.art)
		menu_tags.art = menu_tags_art_load() ? 1 : -1;
	return menu_tags.art > 0 ? menu_tags_online(tag_index) : NONE;
}

boolean pc_menu_tag(
	long tag_index)
{
	return menu_tags.loaded && tag_index != NONE &&
		DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index) >= menu_tags.original_count;
}

char const *pc_menu_function_name(
	long function_index)
{
	return function_index >= 0 && function_index < (long)NUMBEROF(port_function_names) ?
		port_function_names[function_index] : NULL;
}

char const *pc_menu_game_data_input_name(
	long function_index)
{
	return function_index >= 0 && function_index < (long)NUMBEROF(port_game_data_input_names) ?
		port_game_data_input_names[function_index] : NULL;
}
