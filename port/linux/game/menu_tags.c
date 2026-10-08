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
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "memory/zlib/zlib.h"
#include "cache_file_formats.h"

#include "halo_menus.h"

#include <stdlib.h>
#include <string.h>

/* the platform layer's (port/linux/src) */
void platform_log(char const *format, ...);
void platform_heap_usage(unsigned long *in_use, unsigned long *capacity);
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
boolean pc_menu_tag(long tag_index);
char const *pc_menu_function_name(long function_index);
char const *pc_menu_game_data_input_name(long function_index);
/* (system_link_shortcut.c's, for the settings panel) */
extern volatile int halo_pc_menus_state;
/* menu_functions.c's: a screen of the game's that the menus' flows open
another in place of (ours or the game's), or the tag itself */
long pc_menu_functions_screen(long tag_index);

/* ---------- constants */

#define PC_MENU_TAG_PREFIX "pc\\"
#define XBOX_MULTIPLAYER_SCREEN "ui\\shell\\main_menu\\multiplayer_type_select\\multiplayer_type_select_screen"
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
	"port mp require controllers",
};

/* the port's game data functions (menu_functions.c), from
PC_MENU_FUNCTION_BASE */
static char const *const port_game_data_input_names[] =
{
	"unwired",
	"port mp update desc", "port browser update", "port password update", "port setup update", "port code update",
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
	boolean loaded;
	/* the art: not read yet (0), read (1), failed (-1: the Xbox's screen) */
	int art;
	long root_tag;
	long xbox_root_tag;
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
	long *strings_tags;
	long *text_tags;
	long *spinner_tags;
	boolean failed;
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
		long length = (long)strlen(strings[index]);
		unsigned short *characters = allocate((length * 2 + 2) * sizeof(unsigned short));
		long written;

		if (!characters)
			return list;
		written = halo_menus_utf16(strings[index], characters, length * 2 + 2);
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
		reference_set(&handler->widget_tag, UI_WIDGET_DEFINITION_TAG,
			tag_named(UI_WIDGET_DEFINITION_TAG, targets[0], source->file, source->line));
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
	char const *path = !strcmp(font, "large") ? "ui\\large_ui" :
		!strcmp(font, "small") ? "ui\\small_ui" :
		!strcmp(font, "terminal") ? "ui\\interstate" : font;

	return map_tag(FONT_TAG, path, file, line);
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
	if (source->bitmap)
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
	memset(&menu_tags, 0, sizeof(menu_tags));
	menu_tags.root_tag = menu_tags.xbox_root_tag = NONE;
}

/* ---------- the player's Halo PC data */

struct menu_resource_file
{
	HANDLE handle;
	struct cache_file_source source;
	struct resource_map map;
	boolean opened;
};

static int menu_resource_read(void *context, uint32_t offset, uint32_t size, void *buffer)
{
	uint32_t done = 0;

	while (done < size)
	{
		OVERLAPPED position;
		DWORD read = 0;

		memset(&position, 0, sizeof(position));
		position.Offset = offset + done;
		if (!ReadFile((HANDLE)context, (byte *)buffer + done, size - done, &read, &position) || !read)
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
	snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), name);
	file->handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (file->handle == INVALID_HANDLE_VALUE)
		return FALSE;
	size = GetFileSize(file->handle, NULL);
	if (size == INVALID_FILE_SIZE)
	{
		CloseHandle(file->handle);
		return FALSE;
	}
	file->source.context = file->handle;
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

static boolean menu_png_frame_load(struct menu_png_frame const *frame)
{
	struct bitmap_data *bitmap = frame->bitmap;
	unsigned long size = 0;
	byte *data = halo_menus_file_read(frame->png, &size);
	byte *pixels = data && size <= MAXIMUM_PNG_BYTES ? png_decode(data, size, bitmap->width, bitmap->height) : NULL;

	halo_menus_file_free(data);
	if (!pixels)
	{
		halo_menus_log(frame->file, frame->line, "a PNG of the menus folder cannot be read (8-bit RGB or RGBA, "
			"the frame's size):", frame->png);
		return FALSE;
	}
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

	platform_heap_usage(&heap_before, &heap_capacity);
	platform_contiguous_usage(&window_before, &window_free);
	if (!menu_resource_open(&bitmaps, "bitmaps", _resource_map_bitmaps, TRUE))
		return FALSE;
	if (!menu_resource_open(&locale, "loc", _resource_map_locale, TRUE))
	{
		menu_resource_close(&bitmaps, TRUE);
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
	platform_log("menus: the Halo PC pictures and text %s in %lu ms (%ld bitmaps, %ld string lists, %ld PNGs); "
		"C heap %+ld KB, memory window %+ld KB (%lu KB free)",
		success ? "read" : "could not be read", (unsigned long)((vita_host_time_us() - started) / 1000),
		menu_tags.bitmap_resource_count, menu_tags.string_resource_count, menu_tags.png_frame_count,
		((long)heap_after - (long)heap_before) / 1024, ((long)window_after - (long)window_before) / 1024,
		window_free / 1024);
	return success;
}

/* whether the maps folder has a bitmaps.map and a loc.map (a quick look:
no file opened) */
static boolean menu_resource_files_present(void)
{
	static char const *const names[] = { "bitmaps", "loc" };
	int index;

	for (index = 0; index < 2; index++)
	{
		char path[300];
		DWORD attributes;

		snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), names[index]);
		attributes = GetFileAttributesA(path);
		if (attributes == (DWORD)-1 || (attributes & FILE_ATTRIBUTE_DIRECTORY))
			return FALSE;
	}
	return TRUE;
}

/* ---------- public code */

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

/* ui.map's tags loaded: ours added, if the player's Halo PC data is there
(only their names and the XML: the rest at first use) */
void menu_tags_loaded(
	char const *map_name)
{
	struct halo_menus const *menus;
	struct cache_file_tag_instance *instances;
	long widget_count, own_lists = 0, total, index;
	unsigned long long started = vita_host_time_us(), checked, read;

	menu_tags.root_tag = menu_tags.xbox_root_tag = NONE;
	if (strcmp(map_name, "ui"))
		return;
	/* (until they are added: the Xbox's menus) */
	halo_pc_menus_state = -1;
	if (getenv("HALO_MENUS") && !strcmp(getenv("HALO_MENUS"), "xbox"))
		return;
	/* (the player's Halo PC data: only that the files are there; what they
	are, when the screens first open) */
	if (!menu_resource_files_present())
		return;
	checked = vita_host_time_us();
	menus = halo_menus_load();
	if (!menus)
		return;
	read = vita_host_time_us();
	memset(&build, 0, sizeof(build));
	build.menus = menus;
	widget_count = menus->widget_count;
	build.widget_tags = malloc((widget_count + 1) * sizeof(long));
	build.text_tags = malloc((widget_count + 1) * sizeof(long));
	build.spinner_tags = malloc((widget_count + 1) * sizeof(long));
	build.bitmap_tags = malloc((menus->bitmap_count + 1) * sizeof(long));
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
	menu_tags.xbox_root_tag = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER_SCREEN);
	if (menu_tags.xbox_root_tag == NONE || widget_named(menus->root) == NONE)
	{
		platform_log("menus: there is no screen %s (the Xbox's, or ours: %s)", XBOX_MULTIPLAYER_SCREEN, menus->root);
		goto failed;
	}
	total = widget_count + own_lists + menus->string_list_count + menus->bitmap_count;
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
	/* the tags: the bitmaps and strings first, which the widgets name; the
	widgets' names in the table before any is built, which they find there */
	for (index = 0; index < menus->bitmap_count && !build.failed; index++)
	{
		instance_set(instances, BITMAP_GROUP_TAG, build.bitmap_tags[index], menus->bitmaps[index].name, "",
			bitmap_build(&menus->bitmaps[index], build.bitmap_tags[index]));
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
	menu_tags.root_tag = build.widget_tags[widget_named(menus->root)];
	menu_tags.loaded = TRUE;
	halo_pc_menus_state = 1;
	platform_log("menus: OpenCE's multiplayer screens: %ld widgets, %ld string lists and %ld bitmaps added to "
		"ui.map's %ld tags in %lu us (the Halo PC files checked %lu, the XML read %lu, the tags %lu; the pictures "
		"and text are read when they first open)",
		widget_count, own_lists + menus->string_list_count, menus->bitmap_count, build.first_index,
		(unsigned long)(vita_host_time_us() - started), (unsigned long)(checked - started),
		(unsigned long)(read - checked), (unsigned long)(vita_host_time_us() - read));
	goto done;

failed:
	platform_log("menus: OpenCE's multiplayer screens not added; the game's own");
	menu_tags_release();

done:
	free(build.widget_tags);
	free(build.text_tags);
	free(build.spinner_tags);
	free(build.strings_tags);
	free(build.bitmap_tags);
	memset(&build, 0, sizeof(build));
}

void menu_tags_unloaded(
	void)
{
	if (menu_tags.loaded || menu_tags.block_count || menu_tags.original_instances)
		menu_tags_release();
}

/* (ui_widget_load_by_name_or_tag) a screen the game opens: ours in place of
the Xbox's Multiplayer screen (the Halo PC pictures and text read the first
time), and those menu_functions.c's flows replace; else the tag itself */
long menu_tags_screen(
	long tag_index)
{
	if (!menu_tags.loaded || tag_index == NONE)
		return tag_index;
	if (tag_index == menu_tags.xbox_root_tag && menu_tags.art >= 0)
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
		return pc_menu_functions_screen(tag_index);
	return tag_index;
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
