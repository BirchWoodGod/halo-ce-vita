/*
HALO_MENUS.H

The menus written in XML (port/vita/app0/menus, OpenCE's format), as the
platform layer reads them (port/linux/src/menu_files.c) for the game, which
builds them into widget tags when ui.map loads (port/linux/game/menu_tags.c).
Plain C89 types, for both: each element keeps its attributes as written, and
the file and line it came from; the game reads their meaning.

From OpenCE (OpenCommunityEdition/OpenCE, CC0, c9ee319a "Add the PC menus"),
with what the Vita adds: a <bitmap> or <strings> may be the player's own
Halo PC data (resource="ui\..."), read from the Custom Edition bitmaps.map
and loc.map in the maps folder, as Bungie's art and text never ship.

A file holds <menus> with <bitmap> (holding <frame>s), <strings> (holding
<string>s) and <widget> elements; a <widget> holds <widget> and <child>
(its children), <on> (an event handler), <data> (a game data input),
<conditional> and <replace> elements. port/vita/app0/menus/README.txt
describes them.
*/

#ifndef HALO_MENUS_H
#define HALO_MENUS_H

#define HALO_MENU_NONE (-1L)
/* the port's event handler and game data functions (menu_functions.c) are
numbered from here, clear of the Xbox's (0 to 101 and 0 to 40) */
#define PC_MENU_FUNCTION_BASE 256

/* a frame: a PNG of the menus folder drawn at width by height, or the
loaded map's bitmap's frame (map: its bitmap group tag, index: the bitmap's
number in it) */
struct halo_menu_frame
{
	char const *png;
	char const *map;
	long index;
	long width, height;
	long x, y;
};

/* a bitmap: its frames, or (resource) every frame of the bitmap of that
path in the player's bitmaps.map */
struct halo_menu_bitmap
{
	char const *name;
	char const *resource;
	/* fit="widget": each frame drawn whole into its widget's bounds,
	scaled (a picture of more pixels than the 640x480 screen gives it, so
	sharp on the Vita's 960x544 interface layer), not 1:1 from its top left */
	char const *fit;
	/* its frames: frame_count from first_frame in the frames */
	long first_frame, frame_count;
	char const *file;
	long line;
};

/* a string list: its strings, or (resource) the string list of that path
in the player's loc.map */
struct halo_menu_strings
{
	char const *name;
	char const *resource;
	/* its strings: count from first in the strings */
	long first, count;
	char const *file;
	long line;
};

struct halo_menu_handler
{
	char const *event, *run, *script, *open, *replace, *close, *widget, *focus, *reload, *sound, *otherwise, *label;
	long back, branch;
	long next;
	char const *file;
	long line;
};

struct halo_menu_input
{
	char const *input;
	long next;
	char const *file;
	long line;
};

/* a child: a <widget> inside its parent (nested), or one named (<child>) */
struct halo_menu_child
{
	long nested;
	char const *widget;
	long x, y;
	char const *controller;
	long next;
	char const *file;
	long line;
};

struct halo_menu_conditional
{
	char const *widget;
	long if_failed;
	long next;
	char const *file;
	long line;
};

struct halo_menu_replace
{
	char const *search, *function;
	long next;
	char const *file;
	long line;
};

struct halo_menu_widget
{
	char const *name, *type, *controller, *flags, *bitmap, *text, *strings, *values, *setting, *font, *color,
		*align, *description, *string_list, *list_flags, *text_flags, *header_bitmap, *footer_bitmap,
		*header_bounds, *footer_bounds;
	/* (a Halo 3 screen) the camera behind it: scene="ring" (ui.map's own
	loop, ring_loop) or scene="<ui.map's camera point> [x y z yaw pitch
	[ticks]]" (menu_functions.c h3 scenes) */
	char const *scene;
	/* (a Halo 3 screen with a scene) what stands in it:
	scene_object="<pack>[:<tag>] [<distance> <right> <up> <facing>]
	[<option>=<value> ...] [; ...]" (menu_scene_objects.c; the packs:
	scene_packs.c) */
	char const *scene_object;
	/* (Menu style Halo CE's additions) extends="<ui.map widget path>": the
	widget is a copy of the map's look (its text, bounds, children), with
	what the element gives in place of the map's: bitmap, flags, font,
	color, align, text or string_list (string_index with it) and
	width/height; its <child> elements after the map's own, or in their
	place with drop="children"; its <on> and <data> its own (never the
	map's: they would run as ours, menu_tags.c). classic_patch="true" (with
	extends): the map's widget itself is this one while Menu style is Halo
	CE (menu_tags.c swaps the tag's definition as each screen opens), the
	map's own while it is Halo 3 - an Xbox screen keeps its name, history,
	look and events (drop="handlers inputs" leaves those out), with the rows
	the port adds; still the map's widget, it runs none of the port's
	functions (ui_widget_function_denied): those go on widgets of ours */
	char const *extends, *drop, *classic_patch;
	long x, y, left, top, width, height, text_x, text_y, auto_close, auto_close_fade, string_index;
	long has_width, has_height;
	/* indices in their arrays, or HALO_MENU_NONE */
	long parent, first_child, first_handler, first_input, first_conditional, first_replace;
	char const *file;
	long line;
};

/* a font of the menus' own (<font name=... file=...>): a file of the menus
folder that tools/halo3_menu_art.py writes (its characters' sizes and
places, and their 8-bit pixels), made a font tag of the game's
(menu_tags.c) that a widget's font= names */
struct halo_menu_font
{
	char const *name, *path;
	char const *file;
	long line;
};
/* a screen of the Halo 3 set (menus/halo3): its root widget, and the Xbox
screen it stands for while Menu style is Halo 3 (<menus root=... replaces=...>) */
struct halo_menu_screen
{
	char const *root, *replaces;
	char const *file;
	long line;
};
struct halo_menus
{
	struct halo_menu_bitmap *bitmaps;
	long bitmap_count;
	struct halo_menu_frame *frames;
	long frame_count;
	struct halo_menu_strings *string_lists;
	long string_list_count;
	char const **strings;
	long string_count;
	struct halo_menu_widget *widgets;
	long widget_count;
	struct halo_menu_child *children;
	long child_count;
	struct halo_menu_handler *handlers;
	long handler_count;
	struct halo_menu_input *inputs;
	long input_count;
	struct halo_menu_conditional *conditionals;
	long conditional_count;
	struct halo_menu_replace *replaces;
	long replace_count;
	/* the screen that stands for the Xbox's Multiplayer screen:
	<menus root="..."> (the last given without replaces=) */
	char const *root;
	/* the Halo 3 set's screens (<menus root= replaces=>) */
	struct halo_menu_screen *screens;
	long screen_count;
	/* the menus' own fonts (<font>) */
	struct halo_menu_font *fonts;
	long font_count;
};

/* the menus, read from the files (once; the same each call after); NULL if
there are none, or a file has an error (logged) */
struct halo_menus const *halo_menus_load(void);
/* the Menu style (HALO_MENU_STYLE, menus.style): 1 for the Halo 3-style
screens (menus/halo3), 0 for Halo CE's own; read again each call, so a
change in the settings panel takes at the next screen */
int halo_menu_style_halo3(void);
/* 1 for Menu style Online (HALO_MENU_STYLE "online"): the Halo 3 set's flows
(halo_menu_style_halo3 is 1 for it too), with menus/online's screens - an
online/<name> widget opens in place of h3/<name> - for those it has */
int halo_menu_style_online(void);
/* the same begun on a thread of its own (once), which halo_menus_load waits
for */
void halo_menus_preload(void);
/* the menus a level's map gets (ingame.txt: the Halo 3 pause screen), read
the first time one loads; NULL when there are none or they have a problem */
struct halo_menus const *halo_menus_load_ingame(void);
/* a file of the menus folder (a frame's PNG), read whole, NULL if it cannot
be read; halo_menus_file_free lets it go (the platform layer's heap, not
the game's) */
unsigned char *halo_menus_file_read(char const *path, unsigned long *size);
void halo_menus_file_free(unsigned char *data);
/* (port) the scenes in the Halo 3 menus' lobbies
(port/linux/game/menu_scene_objects.c): the screen's scene_object= (NULL:
none) in front of its camera (menu_functions.c, at a frame's top), and the
update that makes, holds and deletes its objects (main.c, before the tick) */
void menu_scene_objects_want(char const *screen, char const *objects, int has_camera, float x, float y, float z,
	float yaw, float pitch);
void menu_scene_objects_update(void);
/* (port) whether the scene asked for stands settled - made, its poses
taken (or none to make, or it cannot be) - for a network game's lobby,
which stops the main menu's ticks (the Halo 3 live lobby opens once it is:
menu_functions.c h3_live). A scene not made by then is none while the
network game lasts: all of it or none */
int menu_scene_objects_settled(void);
/* (port) the Customization screen's live preview: the menu_spartan pack's
Spartan in the menu scene open now (camera NULL) or in front of the camera
"<ui.map camera point> [x y z yaw pitch]" (degrees, as scene=), placed
"<distance> <right> <up> <facing>" (NULL: 1.6 0.45 -0.35 15), in Halo CE's
multiplayer colour color_index (0 to 17; NONE: the active profile's).
FALSE if there is no scene or the arguments are not ones. It appears once its
pack is loaded (built from the player's multiplayer maps the first time),
and never if it cannot be. _color recolours it live; _hide removes it. Safe
from a widget's event */
int menu_scene_spartan_show(char const *camera, char const *placement, short color_index);
/* (port) the preview's character: the scene pack of a player model from a
Custom Edition map (player_models.c's pm_<hash>), NULL for the menu
Spartan's (menu_spartan); held in the menu Spartan's rifle. The one
standing there is made again as the new pack loads. Its pack's state, how far
its build is and why it failed (scene_packs_status) */
void menu_scene_spartan_model(char const *pack);
short menu_scene_spartan_state(long *progress, char *why, long why_size);
void menu_scene_spartan_color(short color_index);
void menu_scene_spartan_hide(void);
/* (port) whether the preview's Spartan stands there now (made: its pack
loaded), so that a screen keeps its fallback until it does */
int menu_scene_spartan_shown(void);
/* (port) a file of the scene packs' recipes folder (app0:packs/ on the
Vita; elsewhere HALO_PACKS_FOLDER, else the packs folder beside the menus
folder), as halo_menus_file_read reads the menus': scene_packs.c */
unsigned char *halo_packs_file_read(char const *path, unsigned long *size);
/* text as UTF-16, its terminator included, "\n" written as the game's line
breaks ("\r\n"): the number of characters written (at most capacity) */
long halo_menus_utf16(char const *utf8, unsigned short *out, long capacity);
/* logs a problem in a file's element */
void halo_menus_log(char const *file, long line, char const *message, char const *detail);

/* The reads the screens make of the player's Halo PC pictures and text the
first time they open (menu_tags.c's menu_tags_art_load: bitmaps.map's and
loc.map's index, the bitmaps' tags and pixels, the string lists, the PNGs),
made beforehand on a thread of their own once ui.map has loaded
(halo_menus_art_preread): the first opening then takes the bytes from
memory (halo_menus_art_take, halo_menus_art_png) rather than the memory
card - 62 file calls, 1.5 MB, 0.5-0.75 s of the game thread on the Vita
each time the menus loaded. What the thread did not read is read as before.
halo_menus_art_wait waits for the thread, halo_menus_art_release lets its
bytes go. */
struct halo_menus_art_plan
{
	char const *map_directory;
	char const *const *bitmap_paths;
	long bitmap_count;
	char const *const *string_paths;
	long string_count;
	char const *const *png_names;
	long png_count;
	/* (the Halo PC bitmap tag as menu_tags.c reads it: where its bitmaps
	block is, a bitmap's size and where its pixels' place and size are, and
	the limits it reads within) */
	unsigned long bitmaps_block_offset;
	unsigned long bitmap_data_bytes;
	unsigned long pixels_offset_offset;
	unsigned long pixels_size_offset;
	unsigned long maximum_frames;
	unsigned long maximum_pixel_bytes;
	unsigned long maximum_tag_bytes;
};

void halo_menus_art_preread(struct halo_menus_art_plan const *plan);
void halo_menus_art_wait(void);
/* `size` bytes at `offset` of the resource map of `resource_map_type`
(cache_file_formats.h), when the thread read exactly those: 1 */
int halo_menus_art_take(int resource_map_type, unsigned long offset, unsigned long size, void *buffer);
/* the PNG `name` the thread read, now the caller's (halo_menus_file_free),
or NULL */
unsigned char *halo_menus_art_png(char const *name, unsigned long *size);
void halo_menus_art_release(void);

#endif
