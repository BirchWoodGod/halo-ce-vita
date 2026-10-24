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
	char failure[160];
	/* the main menu at rest since */
	unsigned long long rest_since;
	/* its bsp the game's now, ui.map's scenario's blocks the pack's */
	boolean active;
	struct scenario *scenario;
	struct tag_block kept_structure_bsps, kept_skies, kept_scenery, kept_scenery_palette, kept_detail_objects,
		kept_decals, kept_sound_scenery, kept_sound_scenery_palette;
	/* the menu music's level set while it is behind them (the music
	class's gain: 1 none) */
	real music_gain;
	boolean music_set;
	/* its sound scenery made (the game places a level's at its start, not
	on a bsp switch): let go with it */
	long sounds[MENU_BACKDROP_SOUNDS];
	short sound_count;
	/* ui.map's bsp reference, then the backdrop's */
	struct scenario_structure_bsp_reference references[2];
	struct backdrop_bsp_header header;
	long structure_bsp_index;
	unsigned long long activated_us;
} menu_backdrop;

/* ---------- private code */

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

	scenario_switch_structure_bsp(1);
	if (global_structure_bsp_index_get() != 1)
	{
		/* (the switch refused it and went back to ui.map's) */
		menu_backdrop_blocks_put_back(TRUE);
		if (global_structure_bsp_index_get() != 0)
			scenario_switch_structure_bsp(0);
		menu_backdrop_fail("the game refused its bsp");
		return;
	}
	menu_backdrop.active = TRUE;
	menu_backdrop.activated_us = vita_host_time_us();
	menu_backdrop_sounds_make(scenario);
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

/* (render.c, under the menus) the live background chosen and not up: a
plain dark screen in place of the ring, and a line saying what it waits for
- "Preparing the live background... N%" while its pack is built or read -
or why it cannot come */
void menu_backdrop_pending_draw(void)
{
	void draw_quad(rectangle2d *rectangle, pixel32 color);
	rectangle2d bounds;
	long font_index;
	char line[200];
	unsigned short text[200];
	real_argb_color color;

	if (!menu_backdrop.pending || !halo_main_menu_loaded())
		return;
	bounds.x0 = bounds.y0 = -2048;
	bounds.x1 = bounds.y1 = 4096;
	draw_quad(&bounds, (pixel32)0xFF05070A);
	if (menu_backdrop.failed)
	{
		snprintf(line, sizeof(line), "%s", strstr(menu_backdrop.failure, "a30") ?
			T("The live background needs a30.map: Settings > Game > Menus") :
			T("The live background could not be made: Settings > Game > Menus"));
	}
	else
	{
		long progress = -1;
		short state = menu_backdrop.wanted ? scene_packs_status(MENU_BACKDROP_PACK, &progress, NULL, 0) : 0;

		if (menu_backdrop.wanted && state == _scene_pack_state_building && progress >= 0 && progress <= 100)
			snprintf(line, sizeof(line), T("Preparing the live background... %ld%%"), progress);
		else
		{
			static char const *const dots[] = { ".", "..", "..." };

			snprintf(line, sizeof(line), "%s%s", T("Preparing the live background"),
				dots[(vita_host_time_us() / 400000ull) % 3]);
		}
	}
	font_index = tag_loaded('font', "pc\\h3\\font_small");
	if (font_index == NONE)
		return;
	color.alpha = 0.8f;
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
	}
	wanted_here = halo_main_menu_loaded() && halo_menu_style_online() && menu_backdrop_scenes_enabled() &&
		menu_backdrop_enabled();
	menu_backdrop.pending = wanted_here && !menu_backdrop.active && !menu_backdrop.test_off;
	if (!wanted_here)
	{
		if (menu_backdrop.active && halo_main_menu_loaded())
			menu_backdrop_deactivate(halo_menu_style_online() ? "the backdrop turned off" : "another menu style");
		menu_backdrop.rest_since = 0;
		return;
	}
	/* (the menu music's level, changed in the settings panel: live) */
	if (menu_backdrop.active)
		menu_backdrop_music_level();
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
		return;
	if (!menu_backdrop.wanted)
	{
		char why[160];
		unsigned long long now = vita_host_time_us();

		/* (asked for once the main menu has rested and the lobbies' packs
		are in: its build takes the tag cache's free room as theirs do) */
		if (!halo_main_menu_idle())
		{
			menu_backdrop.rest_since = 0;
			return;
		}
		if (!menu_backdrop.rest_since)
			menu_backdrop.rest_since = now;
		if (now - menu_backdrop.rest_since < 1000000ull || !menu_functions_prebuild_settled())
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
	if (state != _scene_pack_state_loaded || !scene_packs_textures_ready(MENU_BACKDROP_PACK))
		return;
	menu_backdrop_activate();
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
}
