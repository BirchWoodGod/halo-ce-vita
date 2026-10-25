/*
MENU_BACKDROP.C

The live backdrop behind the Online menus: the campaign mission "Halo"
(a30) read from the player's own a30.map at run time - none of it shipped -
its first structure bsp (the crash site, the Forerunner tower, the
waterfall, the lakeshore) with its night skies, its trees and rocks and its
grass, and the menus' camera at a spot of it for each screen (main menu,
lobbies, CUSTOMIZATION: their backdrop= in port/vita/app0/menus, as their
scene= says where ui.map's camera is: menu_functions.c's h3 scenes).

How: the main menu stays ui.map's scenario, and a30 is brought beside it as
a scene pack (scene_packs.c, the machinery behind the lobbies' characters):
app0:packs/menu_backdrop.txt, a "backdrop" recipe, holds a30's structure bsp
a30_a - read from the map's bsp section, its vertices with it - and a copy
of its scenario trimmed to its skies, its scenery and their palette and its
detail objects' palette, with every tag those need (the bsp's shaders and
their bitmaps, its lightmaps, the skies' models, the trees' and rocks'
models; no sound, effect or damage). The pack is built once (a30.map read
on the packs' worker, the build in the memory window) and kept in
t:\packs; each time ui.map loads it is read into the tag cache's free room
after ui.map's tags (8.7 MB of tags; its 14 MB of pixels read from its file
as the texture cache wants them). Once it is loaded and its textures are
read, between two frames, ui.map's scenario is given a second structure bsp
(its own block of bsp references, the pack's bsp after ui.map's), and its
skies, scenery and their palette, detail objects' palette and decals are
the pack's scenario's (each kept, put back as the backdrop goes); then the
game's own bsp switch (scenario_switch_structure_bsp) goes to it: the
objects of ui.map's bsp let go, the trees and rocks placed by the game as on
any bsp switch (object_types_place_objects, its scenery in a30's places),
the sky, fog, lightmaps and grass the bsp's. Its load
(menu_backdrop_structure_bsp_load, scenario_structure_bsp_load's first
question: cache_files.c) reads nothing: its root is the pack's, its
vertex buffers registered with the pack's, its lightmaps' vertex buffers -
which the pack keeps among its index buffers, as the game's checks call a
bsp's lightmap vertex buffers - registered here.

Why not a30 as the main menu's scenario (Halo 3's way): a30's 15 MB of tags
and 6 MB bsp leave no room in the 22 MB tag cache for ui.map's 1.6 MB of
menus (and the main menu is ui.map's in the game's every corner); the bsp
alone, its sky and its scenery are 8.7 MB, beside ui.map's.

The lobbies switch in place (SWITCH LOBBY): only the camera moves (each
screen's backdrop= written into one of ui.map's camera points), the level
stays. A screen's backdrop_object= names who would stand at its spot (as
scene_object=, menu_scene_objects.c): none yet - the lobbies' characters
are left out of the backdrop for now.

Fallback, nothing breaks: Menu style other than Online, the scenes off
(menus.scenes), menus.backdrop false (HALO_MENU_BACKDROP=0), no a30.map (or
another version of it), a build or load that fails (no room in the tag
cache, the memory window, a damaged pack): ui.map's own ring, as before. A
map loaded (a game started) lets it go with ui.map's tags
(menu_backdrop_unloaded, scenario_tags_unload, before the packs go).
*/

/* ---------- headers */

#include "cseries.h"
#include "real_math.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "rasterizer/rasterizer_geometry.h"
#include "tag_files/tag_groups.h"
#include "sound/sound_classes.h"
#include "objects/objects.h"
#include "objects/object_types.h"
#include "devices/devices.h"
#include "game/game.h"

#include "halo_menus.h"
#include "scene_packs.h"
#include "text/draw_string.h"
#include "rasterizer/rasterizer.h"
#include "../src/lang.h"

#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);
int halo_main_menu_loaded(void);
int halo_main_menu_idle(void);
int config_boolean(const char *name);
unsigned long scene_packs_map_epoch(void);
void halo_tick_wait_for_render(void);
int menu_functions_prebuild_settled(void);
void menu_functions_scenes_refresh(void);
/* (sound_classes.c: no header says it - an undeclared call passed the gain
as a double, read as 0: the music went silent) */
void sound_class_set_gain(char const *name, real gain, short interpolation_ticks);

/* ---------- constants */

#define MENU_BACKDROP_PACK "menu_backdrop"
/* (a30's first bsp has 124: room for them) */
#define MENU_BACKDROP_SOUNDS 192
/* (a30's first bsp has one: its beam emitter) */
#define MENU_BACKDROP_MACHINES 16

enum
{
	/* the pack's roots (menu_backdrop.txt's tag lines' order) */
	_backdrop_root_scenario = 0,
	_backdrop_root_structure_bsp,
	NUMBER_OF_BACKDROP_ROOTS,

	/* (scene_pack.c's trimmed scenario: these offsets) */
	SCENARIO_DETAIL_OBJECT_PALETTE_OFFSET = 0x3C0,

	/* a bsp's header's signature (cache_files.c) */
	STRUCTURE_BSP_HEADER_SIGNATURE = 'sbsp',
};

typedef char menu_backdrop_detail_palette_offset[
	offsetof(struct scenario, detail_object_collection_palette) == SCENARIO_DETAIL_OBJECT_PALETTE_OFFSET ? 1 : -1];
typedef char menu_backdrop_scenery_offset[offsetof(struct scenario, scenery) == 0x210 ? 1 : -1];
typedef char menu_backdrop_machines_offset[offsetof(struct scenario, machines) == 0x294 &&
	offsetof(struct scenario, machine_palette) == 0x2A0 ? 1 : -1];
typedef char menu_backdrop_sky_offset[offsetof(struct scenario, sky_references) == 0x30 ? 1 : -1];

/* ---------- structures */

/* cache_files.c's struct cache_file_structure_bsp_header */
struct backdrop_bsp_header
{
	void *base_address;
	long vertex_buffer_count;
	void *vertex_buffers;
	long lightmap_vertex_buffer_count;
	void *lightmap_vertex_buffers;
	unsigned long signature;
};

static struct
{
	/* (each load of ui.map: scene_packs_map_epoch) */
	unsigned long epoch;
	/* the pack wanted, given up (and why, said once) */
	boolean wanted;
	boolean failed;
	/* (insider.25, the owner: the background chosen is the one shown) the
	live background chosen and not up yet: the menus over a plain dark
	screen, never the ring, with what it waits for - its build or read
	(pending), or why it cannot come (failed: failure) */
	boolean pending;
	boolean test_off;
	/* (its build's) the progress shown */
	long progress_shown;
	/* (its pictures' reads) the progress shown */
	long textures_shown;
	char failure[160];
	/* the main menu at rest since */
	unsigned long long rest_since;
	/* its bsp the game's now, ui.map's scenario's blocks the pack's */
	boolean active;
	struct scenario *scenario;
	struct tag_block kept_structure_bsps, kept_skies, kept_scenery, kept_scenery_palette, kept_detail_objects,
		kept_decals, kept_sound_scenery, kept_sound_scenery_palette, kept_machines, kept_machine_palette;
	/* the menu music's level set while it is behind them (the music
	class's gain: 1 none) */
	real music_gain;
	boolean music_set;
	/* its sound scenery made (the game places a level's at its start, not
	on a bsp switch): let go with it */
	long sounds[MENU_BACKDROP_SOUNDS];
	short sound_count;
	/* its machines made (the game places a level's at its start): let go
	with it */
	long machines[MENU_BACKDROP_MACHINES];
	short machine_count;
	/* ui.map's bsp reference, then the backdrop's */
	struct scenario_structure_bsp_reference references[2];
	struct backdrop_bsp_header header;
	long structure_bsp_index;
	unsigned long long activated_us;
} menu_backdrop;

/* ---------- private code */

static void menu_backdrop_sounds_quiet(void);
static void menu_backdrop_sounds_rise(boolean at_once);

static boolean menu_backdrop_enabled(void)
{
	char const *setting = getenv("HALO_MENU_BACKDROP");

	if (setting && *setting)
		return strcmp(setting, "0") && _stricmp(setting, "false");
	return config_boolean("menus.backdrop");
}

static boolean menu_backdrop_scenes_enabled(void)
{
	char const *setting = getenv("HALO_MENU_SCENES");

	if (setting && *setting)
		return strcmp(setting, "0") && _stricmp(setting, "false");
	return config_boolean("menus.scenes");
}

static void menu_backdrop_fail(char const *why)
{
	menu_backdrop.failed = TRUE;
	snprintf(menu_backdrop.failure, sizeof(menu_backdrop.failure), "%s", why ? why : "");
	platform_log("menu backdrop: the live background cannot come (the menus over a dark screen): %s", why);
}

/* the bsp's lightmaps' vertex buffers (the pack's index buffers: scene_pack.c)
registered as the vertex buffers they are, as a map's bsp's are as it loads
(structure_bsp_header_register_vertex_buffers); once for each load of the
pack */
static void menu_backdrop_register_lightmap_vertices(struct structure_bsp *bsp)
{
	long lightmap_index, registered = 0;

	for (lightmap_index = 0; lightmap_index < bsp->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(&bsp->lightmaps, lightmap_index,
			struct structure_lightmap);
		long material_index;

		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, material_index,
				struct structure_material);
			D3DVertexBuffer *buffer = material->lightmap_vertices.hardware_format;

			if (!buffer || (buffer->Common & D3DCOMMON_TYPE_MASK) == D3DCOMMON_TYPE_VERTEXBUFFER)
				continue;
			buffer->Common = D3DCOMMON_TYPE_VERTEXBUFFER | 1;
			IDirect3DVertexBuffer8_Register(buffer, NULL);
			registered++;
		}
	}
	if (registered)
		platform_log("menu backdrop: %ld lightmap vertex buffers registered", registered);
}

/* whether the pack's bsp and scenario fit each other and ui.map's: each
cluster's sky one of the skies, each detail object cell's collection one of
the palette's (the pack's checks checked each alone) */
static boolean menu_backdrop_consistent(struct scenario const *backdrop, struct structure_bsp const *bsp)
{
	long index;

	if (bsp->clusters.count <= 0 || bsp->collision_bsp.count <= 0)
		return FALSE;
	for (index = 0; index < bsp->clusters.count; index++)
	{
		struct structure_cluster const *cluster = TAG_BLOCK_GET_ELEMENT(&bsp->clusters, index, struct structure_cluster);

		if (cluster->sky_index != NONE && (cluster->sky_index < 0 || cluster->sky_index >= backdrop->sky_references.count))
			return FALSE;
	}
	return TRUE;
}

/* ui.map's scenario's own blocks back: its skies, scenery and palettes
(placed by the switch back to its bsp), then (references) its bsp's */
static void menu_backdrop_blocks_put_back(boolean references)
{
	struct scenario *scenario = menu_backdrop.scenario;

	if (!scenario)
		return;
	scenario->sky_references = menu_backdrop.kept_skies;
	scenario->scenery = menu_backdrop.kept_scenery;
	scenario->scenery_palette = menu_backdrop.kept_scenery_palette;
	scenario->detail_object_collection_palette = menu_backdrop.kept_detail_objects;
	scenario->decals = menu_backdrop.kept_decals;
	scenario->sound_scenery = menu_backdrop.kept_sound_scenery;
	scenario->sound_scenery_palette = menu_backdrop.kept_sound_scenery_palette;
	scenario->machines = menu_backdrop.kept_machines;
	scenario->machine_palette = menu_backdrop.kept_machine_palette;
	if (references)
	{
		scenario->structure_bsp_references = menu_backdrop.kept_structure_bsps;
		menu_backdrop.scenario = NULL;
	}
}

/* a30's sound scenery made, as the game makes a level's at its start
(object_new_from_scenario): each placed in the bsp, automatically placed */
static void menu_backdrop_sounds_make(struct scenario *scenario)
{
	long element_size = (long)object_type_definition_get(_object_type_sound_scenery)->placement_tag_block_element_size;
	short index;

	menu_backdrop.sound_count = 0;
	for (index = 0; index < scenario->sound_scenery.count && menu_backdrop.sound_count < MENU_BACKDROP_SOUNDS; index++)
	{
		struct scenario_object_datum *placement = (struct scenario_object_datum *)tag_block_get_element_with_size(
			&scenario->sound_scenery, index, element_size);
		long object_index;

		if (TEST_FLAG(placement->placement_flags, _scenario_object_placement_not_automatic_bit) ||
			scenario_leaf_index_from_point(&placement->position) == NONE)
		{
			continue;
		}
		object_index = object_new_from_scenario(placement, &scenario->sound_scenery_palette);
		if (object_index != NONE)
			menu_backdrop.sounds[menu_backdrop.sound_count++] = object_index;
	}
}

/* a30's machines made as the game makes a level's at its start (its beam
emitter by the tower: its own looping beam, as in a30), each placed in the
bsp, automatically placed */
static void menu_backdrop_machines_make(struct scenario *scenario)
{
	long element_size = (long)object_type_definition_get(_object_type_machine)->placement_tag_block_element_size;
	short index;

	menu_backdrop.machine_count = 0;
	for (index = 0; index < scenario->machines.count && menu_backdrop.machine_count < MENU_BACKDROP_MACHINES; index++)
	{
		struct scenario_object_datum *placement = (struct scenario_object_datum *)tag_block_get_element_with_size(
			&scenario->machines, index, element_size);
		long object_index;

		if (TEST_FLAG(placement->placement_flags, _scenario_object_placement_not_automatic_bit) ||
			scenario_leaf_index_from_point(&placement->position) == NONE)
		{
			continue;
		}
		object_index = object_new_from_scenario(placement, &scenario->machine_palette);
		if (object_index == NONE)
			continue;
		menu_backdrop.machines[menu_backdrop.machine_count++] = object_index;
		/* (a machine powered - a30's are by their device groups, which the
		pack has not: its beam emitter, a looping machine, then runs its own
		"device position" animation over and over as in a30, the blue beam
		shot up into the sky from the tower's foot every few seconds). Its
		first shot is fired as it is made, its sound (bluebeacon3) kept
		silent with the ambience then: menu_backdrop_sounds_quiet */
		device_port_set_state(object_index, 1.f, FALSE, 0.f);
	}
}

/* its sound scenery let go */
static void menu_backdrop_sounds_delete(void)
{
	short index;

	for (index = 0; index < menu_backdrop.sound_count; index++)
	{
		if (object_try_and_get(menu_backdrop.sounds[index]))
			object_delete(menu_backdrop.sounds[index]);
	}
	menu_backdrop.sound_count = 0;
	for (index = 0; index < menu_backdrop.machine_count; index++)
	{
		if (object_try_and_get(menu_backdrop.machines[index]))
			object_delete(menu_backdrop.machines[index]);
	}
	menu_backdrop.machine_count = 0;
}

/* the menu music under the backdrop (menus.backdrop_music, the settings
panel's Sound, Menu music: a percentage of its level, 30 the default - so
the level's ambience comes through -, 100 as it was), the music class's gain
eased to it over a second; read again while it is behind the menus: live */
static void menu_backdrop_music_level(void)
{
	char const *setting = getenv("HALO_MENU_MUSIC");
	long percent = setting && *setting ? atol(setting) : 30;
	real gain = (real)PIN(percent, 0, 100) / 100.f;

	if (menu_backdrop.music_set && gain == menu_backdrop.music_gain)
		return;
	sound_class_set_gain("music", gain, 30);
	menu_backdrop.music_gain = gain;
	menu_backdrop.music_set = TRUE;
	platform_log("menu backdrop: the menu music at %ld%% (the level's ambience under it)", PIN(percent, 0, 100));
}

/* (the top of the main loop, between frames) the pack's bsp the game's */
static void menu_backdrop_activate(void)
{
	long roots[NUMBER_OF_BACKDROP_ROOTS];
	struct scenario *scenario = global_scenario_try_and_get();
	struct scenario *backdrop;
	struct structure_bsp *bsp;
	unsigned long long started = vita_host_time_us();

	if (scene_packs_roots(MENU_BACKDROP_PACK, roots, NUMBER_OF_BACKDROP_ROOTS) != NUMBER_OF_BACKDROP_ROOTS ||
		tag_get_group_tag(roots[_backdrop_root_scenario]) != SCENARIO_TAG ||
		tag_get_group_tag(roots[_backdrop_root_structure_bsp]) != STRUCTURE_BSP_TAG)
	{
		menu_backdrop_fail("its pack has not its scenario and structure bsp");
		return;
	}
	if (!scenario || scenario->structure_bsp_references.count != 1 || global_structure_bsp_index_get() != 0)
	{
		menu_backdrop_fail("ui.map's scenario is not as the main menu's is");
		return;
	}
	backdrop = tag_get(SCENARIO_TAG, roots[_backdrop_root_scenario]);
	bsp = tag_get(STRUCTURE_BSP_TAG, roots[_backdrop_root_structure_bsp]);
	if (!menu_backdrop_consistent(backdrop, bsp))
	{
		menu_backdrop_fail("its bsp's skies are not its scenario's");
		return;
	}
	/* (no frame drawn meanwhile: the render reads the skies and the bsp) */
	halo_tick_wait_for_render();
	menu_backdrop_register_lightmap_vertices(bsp);

	menu_backdrop.scenario = scenario;
	menu_backdrop.kept_structure_bsps = scenario->structure_bsp_references;
	menu_backdrop.kept_skies = scenario->sky_references;
	menu_backdrop.kept_scenery = scenario->scenery;
	menu_backdrop.kept_scenery_palette = scenario->scenery_palette;
	menu_backdrop.kept_detail_objects = scenario->detail_object_collection_palette;
	menu_backdrop.kept_decals = scenario->decals;
	menu_backdrop.kept_sound_scenery = scenario->sound_scenery;
	menu_backdrop.kept_sound_scenery_palette = scenario->sound_scenery_palette;
	menu_backdrop.kept_machines = scenario->machines;
	menu_backdrop.kept_machine_palette = scenario->machine_palette;

	memcpy(&menu_backdrop.references[0], TAG_BLOCK_GET_ELEMENT(&scenario->structure_bsp_references, 0,
		struct scenario_structure_bsp_reference), sizeof(menu_backdrop.references[0]));
	memset(&menu_backdrop.references[1], 0, sizeof(menu_backdrop.references[1]));
	menu_backdrop.references[1].structure_bsp.group_tag = STRUCTURE_BSP_TAG;
	menu_backdrop.references[1].structure_bsp.name = (char *)tag_get_name(roots[_backdrop_root_structure_bsp]);
	menu_backdrop.references[1].structure_bsp.name_length = (long)strlen(menu_backdrop.references[1].structure_bsp.name);
	menu_backdrop.references[1].structure_bsp.index = roots[_backdrop_root_structure_bsp];
	menu_backdrop.structure_bsp_index = roots[_backdrop_root_structure_bsp];
	menu_backdrop.header.base_address = bsp;
	menu_backdrop.header.vertex_buffer_count = 0;
	menu_backdrop.header.vertex_buffers = NULL;
	menu_backdrop.header.lightmap_vertex_buffer_count = 0;
	menu_backdrop.header.lightmap_vertex_buffers = NULL;
	menu_backdrop.header.signature = STRUCTURE_BSP_HEADER_SIGNATURE;

	scenario->structure_bsp_references.count = 2;
	scenario->structure_bsp_references.address = menu_backdrop.references;
	scenario->sky_references = backdrop->sky_references;
	scenario->scenery = backdrop->scenery;
	scenario->scenery_palette = backdrop->scenery_palette;
	scenario->detail_object_collection_palette = backdrop->detail_object_collection_palette;
	scenario->decals.count = 0;
	scenario->decals.address = NULL;
	/* (a30's ambience: its sound scenery placed with its scenery - the
	waterfall, the water -, its bsp's clusters' background sounds played as
	any level's, game_sound.c, where the camera is) */
	scenario->sound_scenery = backdrop->sound_scenery;
	scenario->sound_scenery_palette = backdrop->sound_scenery_palette;
	/* (a30's machines: the beam emitter by the tower, firing as in a30) */
	scenario->machines = backdrop->machines;
	scenario->machine_palette = backdrop->machine_palette;

	/* (its ambience and machines silent as they are made, then up) */
	menu_backdrop_sounds_quiet();
	scenario_switch_structure_bsp(1);
	if (global_structure_bsp_index_get() != 1)
	{
		/* (the switch refused it and went back to ui.map's) */
		menu_backdrop_blocks_put_back(TRUE);
		if (global_structure_bsp_index_get() != 0)
			scenario_switch_structure_bsp(0);
		menu_backdrop_sounds_rise(TRUE);
		menu_backdrop_fail("the game refused its bsp");
		return;
	}
	menu_backdrop.active = TRUE;
	menu_backdrop.activated_us = vita_host_time_us();
	menu_backdrop_sounds_make(scenario);
	menu_backdrop_machines_make(scenario);
	platform_log("menu backdrop: %d of %ld machines made", menu_backdrop.machine_count, backdrop->machines.count);
	platform_log("menu backdrop: a30's bsp %s behind the menus (%ld clusters, %ld skies, %ld scenery placed, %d of %ld "
		"sound scenery made; %ld background sounds), switched in %.1f ms", tag_get_name(menu_backdrop.structure_bsp_index),
		bsp->clusters.count, backdrop->sky_references.count, backdrop->scenery.count, menu_backdrop.sound_count,
		backdrop->sound_scenery.count,
		bsp->background_sound_palette.count, (double)(vita_host_time_us() - started) / 1000.0);
	menu_backdrop_music_level();
	/* (the screen open now: its backdrop's camera) */
	menu_functions_scenes_refresh();
}

/* the backdrop gone, ui.map's own bsp and ring back (the main menu stays) */
static void menu_backdrop_deactivate(char const *why)
{
	if (!menu_backdrop.active)
		return;
	halo_tick_wait_for_render();
	menu_backdrop.active = FALSE;
	menu_backdrop_sounds_delete();
	menu_backdrop_sounds_rise(TRUE);
	/* (its scenery and skies first: the switch places ui.map's own; its
	bsp references after, which the switch reads the backdrop's from) */
	menu_backdrop_blocks_put_back(FALSE);
	scenario_switch_structure_bsp(0);
	menu_backdrop_blocks_put_back(TRUE);
	/* (the menu music at its own level again) */
	if (menu_backdrop.music_set)
	{
		sound_class_set_gain("music", 1.f, 30);
		menu_backdrop.music_set = FALSE;
	}
	platform_log("menu backdrop: ui.map's own ring back (%s)", why);
	menu_functions_scenes_refresh();
}

/* ---------- public code */

/* whether the backdrop is behind the menus now (the screens' backdrop=
cameras, not their scene=) */
int menu_backdrop_active(void)
{
	return menu_backdrop.active;
}

/* what the live background waits for, said on its dark screen and on the
black of a return (menu_backdrop_fade_draw): "Preparing the live
background... N%" while its pack is built (the build's progress) or its
pictures read (theirs), else moving dots - or why it cannot come */
static void menu_backdrop_pending_line(char *line, size_t size)
{
	if (menu_backdrop.failed)
	{
		snprintf(line, size, "%s", strstr(menu_backdrop.failure, "a30") ?
			T("The live background needs a30.map: Settings > Game > Menus") :
			T("The live background could not be made: Settings > Game > Menus"));
	}
	else
	{
		long progress = -1;
		short state = menu_backdrop.wanted ? scene_packs_status(MENU_BACKDROP_PACK, &progress, NULL, 0) : 0;

		/* (built now: its build's progress, never going back; then, read and
		checked after its build, 99% until it is up) */
		if (menu_backdrop.wanted && state == _scene_pack_state_building && progress >= 0 && progress <= 100)
			menu_backdrop.progress_shown = MAX(menu_backdrop.progress_shown, PIN(progress, 1, 99));
		else if (menu_backdrop.wanted && menu_backdrop.progress_shown && state != _scene_pack_state_failed)
			menu_backdrop.progress_shown = 99;
		if (menu_backdrop.progress_shown)
			snprintf(line, size, T("Preparing the live background... %ld%%"), menu_backdrop.progress_shown);
		/* (its pictures read into the texture cache, a few at a time:
		scene_packs_textures_ready) */
		else if (menu_backdrop.textures_shown > 0)
			snprintf(line, size, T("Preparing the live background... %ld%%"), menu_backdrop.textures_shown);
		else
		{
			static char const *const dots[] = { ".", "..", "..." };

			snprintf(line, size, "%s%s", T("Preparing the live background"),
				dots[(vita_host_time_us() / 400000ull) % 3]);
		}
	}
}

static void menu_backdrop_line_draw(char const *line, real alpha)
{
	rectangle2d bounds;
	long font_index;
	unsigned short text[200];
	real_argb_color color;

	font_index = tag_loaded('font', "pc\\h3\\font_small");
	if (font_index == NONE || alpha <= 0.f)
		return;
	color.alpha = 0.8f * alpha;
	color.red = color.green = color.blue = 0.85f;
	bounds.x0 = 0;
	bounds.x1 = 640;
	/* (above the title screen's notice and the menus' footer) */
	bounds.y0 = 412;
	bounds.y1 = 436;
	draw_string_set_draw_mode(font_index, NONE, 2, 0, &color);
	halo_menus_utf16(line, text, (long)(sizeof(text) / sizeof(text[0])));
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, (wchar_t const *)text);
}

/* whether the live background is the one chosen for the menus loaded now
(Menu style ElDewrito, the scenes on, Menu background Halo (live)) */
static boolean menu_backdrop_live_chosen(void)
{
	return halo_menu_style_online() && menu_backdrop_scenes_enabled() && menu_backdrop_enabled();
}

/* (render.c, under the menus) the live background chosen and not up: a
plain dark screen in place of the ring, and a line saying what it waits for
- "Preparing the live background... N%" while its pack is built or read -
or why it cannot come. Also on the first frame of a load of ui.map, drawn
before menu_backdrop_update has seen it (the load comes after it in the
main loop): that frame showed the ring (insider.25, leaving a game) */
void menu_backdrop_pending_draw(void)
{
	void draw_quad(rectangle2d *rectangle, pixel32 color);
	rectangle2d bounds;
	char line[200];
	boolean fresh_load;

	if (!halo_main_menu_loaded())
		return;
	fresh_load = menu_backdrop.epoch != scene_packs_map_epoch();
	if (!menu_backdrop.pending && !(fresh_load && menu_backdrop_live_chosen()))
		return;
	bounds.x0 = bounds.y0 = -2048;
	bounds.x1 = bounds.y1 = 4096;
	draw_quad(&bounds, (pixel32)0xFF05070A);
	if (fresh_load)
		return;
	menu_backdrop_pending_line(line, sizeof(line));
	menu_backdrop_line_draw(line, 1.f);
}

/* ---------- the way in and out (insider.25: the owner, Cass)

With the live background chosen the ring is never shown, and the menus
come and go as their screens do (menu_functions.c's fade): a game left
fades to black before the main menu's load (menu_backdrop_leave_hold, the
main loop's), the main menu is opened at black and held there - "Preparing
the live background..." on it - till the background is up, then faded in.
If it is long in coming (its pack built or read, its pictures read from the
memory card: seconds on the Vita) the menus are faded in over its dark
screen after MENU_BACKDROP_HOLD_MOST_US, usable meanwhile, and when it is
ready they fade out, it is switched to at black, and they fade back in. The
pad is let go while the screen is black. HALO_MENU_FADE=0: no fades (the
black and the dark screen stay: the ring is not shown). */
#define MENU_BACKDROP_FADE_OUT_US 250000ull
#define MENU_BACKDROP_FADE_IN_US 350000ull
/* (a game left: at least this many of its frames in its fade) */
#define MENU_BACKDROP_LEAVE_FRAMES 6
#define MENU_BACKDROP_HOLD_MOST_US 2500000ull
/* (the line on the black: once it has lasted this long) */
#define MENU_BACKDROP_HOLD_LINE_US 400000ull
/* (the ambience and machines silent this long after the switch, then up
over MENU_BACKDROP_SOUND_RISE_TICKS) */
#define MENU_BACKDROP_SOUND_QUIET_US 1500000ull
#define MENU_BACKDROP_SOUND_RISE_TICKS 60

enum
{
	_backdrop_fade_none,
	/* a game left: its last frames fading to black */
	_backdrop_fade_leaving,
	/* the main menu loaded at black, the background awaited */
	_backdrop_fade_hold,
	/* the menus over the dark screen, fading out for the background */
	_backdrop_fade_swap_out,
	_backdrop_fade_in,
};

static struct
{
	short phase;
	unsigned long long start;
	/* (the hold's: the menus to be faded in over the dark screen, not at
	the background) */
	boolean gave_up;
	/* (leaving: the game's frames drawn meanwhile - a save's hitch left
	no frame of its fade) */
	short frames;
	/* the ambience's level held at 0 since (0 none) */
	unsigned long long sound_quiet_since;
} menu_backdrop_fade;

static boolean menu_backdrop_fade_enabled(void)
{
	char const *setting = getenv("HALO_MENU_FADE");

	return !(setting && *setting && (!strcmp(setting, "0") || !_stricmp(setting, "false")));
}

static void menu_backdrop_fade_set(short phase)
{
	menu_backdrop_fade.phase = phase;
	menu_backdrop_fade.start = vita_host_time_us();
}

/* (sound_classes.c) a class's level now, no easing */
void sound_class_set_gain_now(char const *name, real gain);

/* the backdrop's machines (the beam) and ambience silent as it comes, then
up: the beam emitter made fires its first shot at once (a30's
bluebeacon3), heard as a blast the moment the background came back
(insider.25, Cass) */
static void menu_backdrop_sounds_quiet(void)
{
	sound_class_set_gain_now("device_machinery", 0.f);
	sound_class_set_gain_now("ambient", 0.f);
	menu_backdrop_fade.sound_quiet_since = vita_host_time_us();
}

static void menu_backdrop_sounds_rise(boolean at_once)
{
	if (!menu_backdrop_fade.sound_quiet_since)
		return;
	if (at_once)
	{
		sound_class_set_gain_now("device_machinery", 1.f);
		sound_class_set_gain_now("ambient", 1.f);
	}
	else
	{
		sound_class_set_gain("device_machinery", 1.f, MENU_BACKDROP_SOUND_RISE_TICKS);
		sound_class_set_gain("ambient", 1.f, MENU_BACKDROP_SOUND_RISE_TICKS);
	}
	menu_backdrop_fade.sound_quiet_since = 0;
}

/* how black the screen is for the background's way in and out, 0 to 1
(eased as menu_functions_fade_alpha) */
static real menu_backdrop_fade_amount(void)
{
	unsigned long long now = vita_host_time_us();
	real amount;

	switch (menu_backdrop_fade.phase)
	{
	case _backdrop_fade_leaving:
		/* (a game's frames: the main menu not loaded yet) */
		if (halo_main_menu_loaded())
			return 1.f;
		amount = MIN((real)(now - menu_backdrop_fade.start) / (real)MENU_BACKDROP_FADE_OUT_US,
			(real)menu_backdrop_fade.frames / (real)MENU_BACKDROP_LEAVE_FRAMES);
		break;
	case _backdrop_fade_hold:
		return halo_main_menu_loaded() ? 1.f : 0.f;
	case _backdrop_fade_swap_out:
		amount = (real)(now - menu_backdrop_fade.start) / (real)MENU_BACKDROP_FADE_OUT_US;
		break;
	case _backdrop_fade_in:
		amount = 1.f - (real)(now - menu_backdrop_fade.start) / (real)MENU_BACKDROP_FADE_IN_US;
		break;
	default:
		/* (ui.map just loaded, the background chosen, its first frame
		drawn before menu_backdrop_update saw it: black, as the hold) */
		if (halo_main_menu_loaded() && menu_backdrop.epoch != scene_packs_map_epoch() &&
			menu_backdrop_live_chosen())
		{
			return 1.f;
		}
		return 0.f;
	}
	if (!menu_backdrop_fade_enabled())
		return menu_backdrop_fade.phase == _backdrop_fade_in ? 0.f : 1.f;
	amount = PIN(amount, 0.f, 1.f);
	return amount * amount * (3.f - 2.f * amount);
}

/* (menu_functions_fade_draw, over everything) the black of the way in and
out, and on the hold's black what it waits for */
real menu_backdrop_fade_alpha(void)
{
	return menu_backdrop_fade_amount();
}

void menu_backdrop_fade_line_draw(void)
{
	char line[200];
	unsigned long long held;

	if (menu_backdrop_fade.phase != _backdrop_fade_hold || !halo_main_menu_loaded() ||
		menu_backdrop.epoch != scene_packs_map_epoch())
	{
		return;
	}
	held = vita_host_time_us() - menu_backdrop_fade.start;
	if (held < MENU_BACKDROP_HOLD_LINE_US)
		return;
	menu_backdrop_pending_line(line, sizeof(line));
	menu_backdrop_line_draw(line, PIN((real)(held - MENU_BACKDROP_HOLD_LINE_US) / 300000.f, 0.f, 1.f));
}

/* (ui_widget.c's events, through menu_functions_fade_input_held) the pad
let go while the menus are black or going */
int menu_backdrop_input_held(void)
{
	return halo_main_menu_loaded() && (menu_backdrop_fade.phase == _backdrop_fade_hold ||
		menu_backdrop_fade.phase == _backdrop_fade_swap_out);
}

/* (main.c, before the main menu's load) a game left with the live
background chosen: its last frames fade to black first. TRUE while the load
waits */
int menu_backdrop_leave_hold(void)
{
	if (halo_main_menu_loaded() || !game_in_progress() || !menu_backdrop_live_chosen())
	{
		if (menu_backdrop_fade.phase == _backdrop_fade_leaving)
			menu_backdrop_fade_set(_backdrop_fade_hold);
		return FALSE;
	}
	if (menu_backdrop_fade.phase != _backdrop_fade_leaving)
	{
		menu_backdrop_fade_set(_backdrop_fade_leaving);
		menu_backdrop_fade.frames = 0;
		platform_log("menu backdrop: the game left fades to black before the main menu");
	}
	else if (menu_backdrop_fade.frames < SHORT_MAX)
		menu_backdrop_fade.frames++;
	if (menu_backdrop_fade_enabled() && (vita_host_time_us() - menu_backdrop_fade.start < MENU_BACKDROP_FADE_OUT_US ||
		menu_backdrop_fade.frames < MENU_BACKDROP_LEAVE_FRAMES))
	{
		return TRUE;
	}
	/* (black now: the main menu loaded, held black) */
	menu_backdrop_fade_set(_backdrop_fade_hold);
	menu_backdrop_fade.gave_up = FALSE;
	return FALSE;
}

/* (menu_backdrop_update, the main menu's) the fade's turns: TRUE when the
background may be switched to now (black) */
static boolean menu_backdrop_fade_update(boolean wanted_here, boolean ready)
{
	unsigned long long now = vita_host_time_us();
	boolean fading = menu_backdrop_fade_enabled();

	if (menu_backdrop_fade.sound_quiet_since && now - menu_backdrop_fade.sound_quiet_since > MENU_BACKDROP_SOUND_QUIET_US)
		menu_backdrop_sounds_rise(FALSE);
	if (!wanted_here)
	{
		/* (the ring chosen, or another style: nothing held) */
		if (menu_backdrop_fade.phase != _backdrop_fade_none && menu_backdrop_fade.phase != _backdrop_fade_in)
			menu_backdrop_fade_set(fading ? _backdrop_fade_in : _backdrop_fade_none);
		return FALSE;
	}
	switch (menu_backdrop_fade.phase)
	{
	case _backdrop_fade_leaving:
		/* (loaded without the leave's end seen: held) */
		menu_backdrop_fade_set(_backdrop_fade_hold);
		menu_backdrop_fade.gave_up = FALSE;
		return FALSE;
	case _backdrop_fade_hold:
		if (ready)
			return TRUE;
		if (menu_backdrop.failed || now - menu_backdrop_fade.start > MENU_BACKDROP_HOLD_MOST_US)
		{
			/* (long in coming, or it cannot come: the menus over its
			dark screen, usable) */
			menu_backdrop_fade.gave_up = TRUE;
			platform_log("menu backdrop: the menus faded in over the dark screen (%s)",
				menu_backdrop.failed ? "it cannot come" : "still preparing");
			menu_backdrop_fade_set(fading ? _backdrop_fade_in : _backdrop_fade_none);
		}
		return FALSE;
	case _backdrop_fade_swap_out:
		if (!ready)
		{
			menu_backdrop_fade_set(fading ? _backdrop_fade_in : _backdrop_fade_none);
			return FALSE;
		}
		return !fading || now - menu_backdrop_fade.start >= MENU_BACKDROP_FADE_OUT_US;
	case _backdrop_fade_in:
		if (!fading || now - menu_backdrop_fade.start >= MENU_BACKDROP_FADE_IN_US)
			menu_backdrop_fade.phase = _backdrop_fade_none;
		else if (ready)
		{
			/* (ready while the menus come in over the dark screen: out
			again from where it is) */
			real black = menu_backdrop_fade_amount();

			menu_backdrop_fade.phase = _backdrop_fade_swap_out;
			menu_backdrop_fade.start = now - (unsigned long long)(black * (real)MENU_BACKDROP_FADE_OUT_US);
			return FALSE;
		}
		return FALSE;
	default:
		if (!ready)
			return FALSE;
		if (!fading)
			return TRUE;
		/* (the menus shown over the dark screen: out to black first) */
		menu_backdrop_fade_set(_backdrop_fade_swap_out);
		return FALSE;
	}
}

/* (main.c, the top of the main loop, before the menus' scenes) */
void menu_backdrop_update(void)
{
	boolean wanted_here;
	short state;

	if (menu_backdrop.epoch != scene_packs_map_epoch())
	{
		/* (another load of ui.map, or another map: what was is gone with
		its tags, menu_backdrop_unloaded) */
		memset(&menu_backdrop, 0, sizeof(menu_backdrop));
		menu_backdrop.epoch = scene_packs_map_epoch();
		/* (the main menu loaded with the background chosen, a game's leave
		or not - the start's -: at black till it is up) */
		if (halo_main_menu_loaded() && menu_backdrop_live_chosen())
		{
			menu_backdrop_fade_set(_backdrop_fade_hold);
			menu_backdrop_fade.gave_up = FALSE;
		}
	}
	if (!halo_main_menu_loaded())
	{
		if (menu_backdrop_fade.phase != _backdrop_fade_leaving)
			menu_backdrop_fade.phase = _backdrop_fade_none;
		return;
	}
	wanted_here = halo_main_menu_loaded() && menu_backdrop_live_chosen();
	menu_backdrop.pending = wanted_here && !menu_backdrop.active && !menu_backdrop.test_off;
	if (!wanted_here)
	{
		menu_backdrop_fade_update(FALSE, FALSE);
		if (menu_backdrop.active && halo_main_menu_loaded())
			menu_backdrop_deactivate(halo_menu_style_online() ? "the backdrop turned off" : "another menu style");
		menu_backdrop.rest_since = 0;
		return;
	}
	/* (the menu music's level, changed in the settings panel: live) */
	if (menu_backdrop.active)
	{
		menu_backdrop_music_level();
	}
	/* (tests: HALO_MENU_BACKDROP_TEST_OFF=<seconds>, the backdrop let go
that long after it came, as the settings panel's Live backdrop Off lets
it go; the ring's flight back) */
	if (menu_backdrop.active && getenv("HALO_MENU_BACKDROP_TEST_OFF") &&
		vita_host_time_us() - menu_backdrop.activated_us > (unsigned long long)atol(getenv("HALO_MENU_BACKDROP_TEST_OFF")) * 1000000ull)
	{
		menu_backdrop_deactivate("HALO_MENU_BACKDROP_TEST_OFF");
		menu_backdrop.failed = TRUE;
		menu_backdrop.test_off = TRUE;
		return;
	}
	if (menu_backdrop.active || menu_backdrop.failed)
	{
		/* (up - the menus fading in at it -, or given up: the hold's end) */
		if (menu_backdrop.active && menu_backdrop_fade.phase == _backdrop_fade_hold)
			menu_backdrop_fade_set(menu_backdrop_fade_enabled() ? _backdrop_fade_in : _backdrop_fade_none);
		else
			menu_backdrop_fade_update(TRUE, FALSE);
		return;
	}
	if (!menu_backdrop.wanted)
	{
		char why[160];
		unsigned long long now = vita_host_time_us();

		/* (asked for once the main menu has rested and the lobbies' packs
		are in: its build takes the tag cache's free room as theirs do) */
		menu_backdrop_fade_update(TRUE, FALSE);
		if (!halo_main_menu_idle())
		{
			menu_backdrop.rest_since = 0;
			return;
		}
		if (!menu_backdrop.rest_since)
			menu_backdrop.rest_since = now;
		/* (no rest asked while the main menu is held black for it: a game
		left, the start) */
		if ((menu_backdrop_fade.phase != _backdrop_fade_hold && now - menu_backdrop.rest_since < 1000000ull) ||
			!menu_functions_prebuild_settled())
			return;
		if (!scene_packs_recipe_available(MENU_BACKDROP_PACK, why, sizeof(why)))
		{
			menu_backdrop_fail(why);
			return;
		}
		menu_backdrop.wanted = TRUE;
		state = scene_packs_want(MENU_BACKDROP_PACK);
		platform_log("menu backdrop: its pack wanted (%s)", scene_packs_state_name(state));
		return;
	}
	state = scene_packs_status(MENU_BACKDROP_PACK, NULL, NULL, 0);
	if (state == _scene_pack_state_failed || state == _scene_pack_state_none)
	{
		char why[160];

		why[0] = 0;
		scene_packs_status(MENU_BACKDROP_PACK, NULL, why, sizeof(why));
		menu_backdrop_fail(why[0] ? why : "its pack was not loaded");
		return;
	}
	if (state != _scene_pack_state_loaded)
	{
		menu_backdrop_fade_update(TRUE, FALSE);
		return;
	}
	/* (its pictures read a few at a time: scene_packs_textures_ready) */
	{
		boolean textures_ready = scene_packs_textures_ready(MENU_BACKDROP_PACK);
		long percent = scene_packs_textures_percent(MENU_BACKDROP_PACK);

		if (percent > 0)
			menu_backdrop.textures_shown = MAX(menu_backdrop.textures_shown, PIN(percent, 1, 99));
		/* (switched to at black: the hold's, or the menus faded out) */
		if (!menu_backdrop_fade_update(TRUE, textures_ready))
			return;
	}
	menu_backdrop_activate();
	if (menu_backdrop.active)
	{
		/* (the menus faded in at it: its camera put in by the activation) */
		menu_backdrop_fade_set(menu_backdrop_fade_enabled() ? _backdrop_fade_in : _backdrop_fade_none);
		platform_log("menu backdrop: up at black, the menus fading in");
	}
}

/* (cache_files.c, scenario_structure_bsp_load's first question) whether
the bsp reference is the backdrop's: then its header (none read: the pack's
bsp, its buffers registered), NULL if it cannot be loaded */
int menu_backdrop_structure_bsp_load(struct scenario_structure_bsp_reference const *reference, void **header)
{
	if (!reference || reference != &menu_backdrop.references[1] || !menu_backdrop.scenario)
		return FALSE;
	*header = menu_backdrop.header.base_address ? &menu_backdrop.header : NULL;
	return TRUE;
}

/* (scenario_structure_bsp_unload's) whether it is the backdrop's: the
pack's table keeps its root */
int menu_backdrop_structure_bsp_is(struct scenario_structure_bsp_reference const *reference)
{
	return reference && reference == &menu_backdrop.references[1];
}

/* (cache_files.c, scenario_tags_unload: a map loaded, the menus left)
ui.map's scenario as it was, before the packs go */
void menu_backdrop_unloaded(void)
{
	if (menu_backdrop.active || menu_backdrop.scenario)
	{
		platform_log("menu backdrop: let go with ui.map (%.1f s behind the menus)",
			(double)(vita_host_time_us() - menu_backdrop.activated_us) / 1000000.0);
	}
	menu_backdrop_blocks_put_back(TRUE);
	menu_backdrop.active = FALSE;
	/* (its classes' levels are the new map's) */
	menu_backdrop_fade.sound_quiet_since = 0;
}
