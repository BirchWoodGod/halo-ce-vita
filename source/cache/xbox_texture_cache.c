/*
XBOX_TEXTURE_CACHE.C

symbols in this file:
001AE410 0020:
	_texture_cache_delete (0000)
001AE430 0010:
	_texture_cache_open (0000)
001AE440 0010:
	_texture_cache_idle (0000)
001AE450 0090:
	_texture_cache_bitmap_new (0000)
001AE4E0 0040:
	_texture_cache_bitmap_delete (0000)
001AE520 00e0:
	_texture_cache_steal_memory (0000)
001AE600 0060:
	_texture_cache_return_memory (0000)
001AE660 0030:
	_texture_cache_name_block_proc (0000)
001AE690 0090:
	_bitmap_format_to_d3d_format (0000)
001AE720 0090:
	_bitmap_format_to_d3d_linear_format (0000)
001AE7B0 0030:
	_compare (0000)
001AE7E0 0010:
	_IDirect3DDevice8_IsBusy@4 (0000)
001AE7F0 0010:
	_IDirect3DDevice8_KickPushBuffer@4 (0000)
001AE800 0010:
	_IDirect3DBaseTexture8_IsBusy@4 (0000)
001AE810 0010:
	_IDirect3DBaseTexture8_Register@8 (0000)
001AE820 0020:
	_texture_cache_flush (0000)
001AE840 0040:
	_texture_cache_locked_block_proc (0000)
001AE880 00a0:
	_texture_cache_delete_block_proc (0000)
001AE920 0150:
	_texture_cache_initialize_hardware_format (0000)
001AEA70 0100:
	_render_inverse_transform_screen_point (0000)
001AEB70 00b0:
	_texture_cache_new (0000)
001AEC20 0050:
	_texture_cache_close (0000)
001AEC70 00e0:
	_texture_cache_start_loading_bitmap (0000)
001AED50 0310:
	_texture_cache_debug_render (0000)
001AF060 01f0:
	__texture_cache_bitmap_get_hardware_format (0000)
002A7BD0 0090:
	_bitmap_d3d_format_tables (0000)
002A7C60 002e:
	??_C@_0CO@EKKOPCBA@?$CBTEST_FLAG?$CIbitmap?9?$DOflags?0?5_bitma@ (0000)
002A7C90 002a:
	??_C@_0CK@BDDINKJM@c?3?2halo?2SOURCE?2cache?2xbox_textur@ (0000)
002A7CBC 002a:
	??_C@_0CK@MBCIMJKB@?$CBxbox_texture_cache_globals?4stol@ (0000)
002A7CE8 0017:
	??_C@_0BH@KHBOCJGF@remaining_page_count?$DO0?$AA@ (0000)
002A7D00 0029:
	??_C@_0CJ@MMJPDHMC@xbox_texture_cache_globals?4stole@ (0000)
002A7D2C 0014:
	??_C@_0BE@ILIIMHBE@table?$FLformat?$FN?$CB?$DNNONE?$AA@ (0000)
002A7D40 0030:
	??_C@_0DA@KFGIBCJF@texture?9?$DObitmap?9?$DOcache_block_ind@ (0000)
002A7D70 0028:
	??_C@_0CI@OLENPGPB@xbox_texture_cache_globals?4base_@ (0000)
002A7D98 0021:
	??_C@_0CB@NHIMPHNC@xbox_texture_cache_globals?4cache@ (0000)
002A7DBC 0013:
	??_C@_0BD@GPJOJANJ@xbox?5texture?5cache?$AA@ (0000)
002A7DD0 0024:
	??_C@_0CE@IHAAKOAF@xbox_texture_cache_globals?4textu@ (0000)
002A7DF4 000d:
	??_C@_0N@HJPPIHML@xbox?5texture?$AA@ (0000)
002A7E04 0025:
	??_C@_0CF@LJFHGILO@new_texture_index?$DN?$DNcache_block_i@ (0000)
002A7E2C 000b:
	??_C@_0L@PCGBDGNI@?$HMt?$CFd?$HMt?$CFs?$CFs?$AA@ (0000)
002A7E38 0044:
	??_C@_0EE@BCDFBEJK@YOU?5GOT?5STABBED?$CB?$CB?$CB?$CB?5double?9click@ (0000)
004D1198 1618:
	_texture_cache_debug_bitmaps (0000)
	_xbox_texture_cache_globals (1600)
	_texture_cache_debug_options (1610)
	_debug_texture_cache (1612)
*/

/* ---------- headers */

#include "cseries/cseries.h"
#ifdef HALO_LINUX
#include "render_epoch.h"
void platform_log(const char *format, ...);
#include "load_profile.h"
#else
#define halo_cache_lock_acquire() ((void)0)
#define halo_cache_lock_release() ((void)0)
#endif
/* (port) the public functions take the shared cache lock: the tick loads
textures for the objects it creates while the render loads its own */
void texture_cache_delete(void);
void texture_cache_open(void);
void texture_cache_idle(void);
void texture_cache_return_memory(void);
void texture_cache_flush(void);
void texture_cache_new(void);
void texture_cache_close(void);
void texture_cache_debug_render(void);
void texture_cache_bitmap_new(
	long bitmap_tag_index,
	struct bitmap_data *bitmap);
static void texture_cache_delete_unlocked(
	void);
static void texture_cache_open_unlocked(
	void);
static void texture_cache_idle_unlocked(
	void);
static void texture_cache_bitmap_new_unlocked(
	long bitmap_tag_index,
	struct bitmap_data *bitmap);
static void texture_cache_bitmap_delete_unlocked(
	struct bitmap_data *bitmap);
static void *texture_cache_steal_memory_unlocked(
	long size);
static void texture_cache_return_memory_unlocked(
	void);
static void texture_cache_flush_unlocked(
	void);
static void texture_cache_new_unlocked(
	void);
static void texture_cache_close_unlocked(
	void);
static void texture_cache_debug_render_unlocked(
	void);
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "cseries/sort.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps_internal.h"
#include "bitmaps/bitmaps_mipmap.h"
#include "cache/cache_files.h"
#include "cache/texture_cache.h"
#include "cache/xbox_texture_cache.h"
#include "cache/physical_memory_map.h"
#include "interface/interface.h"
#include "interface/terminal.h"
#include "main/console.h"
#include "math/integer_math.h"
#include "memory/data.h"
#include "memory/lruv_cache.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_swizzle.h"
#include "rasterizer/xbox/rasterizer_xbox.h"
#include "rasterizer/xbox/rasterizer_xbox_internal.h"
#include "render/render.h"
#include "render/render_debug.h"
#include "scenario/scenario.h"
#include "sound/sound_manager.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include <xtl.h>

/* ---------- constants */

enum
{
	XBOX_TEXTURE_CACHE_PAGE_COUNT = 0x580,
	XBOX_TEXTURE_CACHE_PAGE_SIZE_BITS = 14,
	XBOX_TEXTURE_CACHE_PAGE_SIZE = 1 << XBOX_TEXTURE_CACHE_PAGE_SIZE_BITS,
	XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE = 0x104000,
	XBOX_TEXTURE_CACHE_STEALABLE_PAGE_COUNT =
		XBOX_TEXTURE_CACHE_PAGE_COUNT -
		2 * (XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE / XBOX_TEXTURE_CACHE_PAGE_SIZE),
	XBOX_TEXTURE_CACHE_ENTRY_SIZE = 0x20,
	XBOX_TEXTURE_CACHE_SIZE = 0x1600000,
	XBOX_TEXTURE_CACHE_PROTECTION = 0x404,
};

enum
{
	_bitmap_type_2d,
	_bitmap_type_3d,
	_bitmap_type_cube_map,
	NUMBER_OF_BITMAP_TYPES,
};

enum
{
	_bitmap_format_a8,
	_bitmap_format_y8,
	_bitmap_format_ay8,
	_bitmap_format_a8y8,
	_bitmap_format_unused1,
	_bitmap_format_unused2,
	_bitmap_format_r5g6b5,
	_bitmap_format_unused3,
	_bitmap_format_a1r5g5b5,
	_bitmap_format_a4r4g4b4,
	_bitmap_format_x8r8g8b8,
	_bitmap_format_a8r8g8b8,
	_bitmap_format_unused4,
	_bitmap_format_unused5,
	_bitmap_format_dxt1,
	_bitmap_format_dxt3,
	_bitmap_format_dxt5,
	_bitmap_format_p8_bump,
	NUMBER_OF_BITMAP_FORMATS,
};

enum
{
	_bitmap_has_power_of_two_dimensions_bit,
	_bitmap_compressed_bit,
	_bitmap_palettized_bit,
	_bitmap_swizzled_bit,
	_bitmap_linear_bit,
	_bitmap_v16u16_bit,
	_bitmap_allocated_bit,
	_bitmap_cached_bit,
	NUMBER_OF_BITMAP_FLAGS,
};

enum
{
	_bitmap_d3d_format_table_regular,
	_bitmap_d3d_format_table_linear,
	NUMBER_OF_BITMAP_D3D_FORMAT_TABLES,
};

/* ---------- macros */

/* ---------- structures */

struct xbox_texture_cache_globals
{
	struct data_array *textures;
	byte *base_address;
	struct lruv_cache *cache;
	boolean stolen_memory;
};

struct xbox_texture_cache_texture
{
	short identifier;
	short read_request_handle;
	boolean loaded;
	boolean used;
	struct bitmap_data *bitmap;
	D3DBaseTexture hardware_format;
};

struct texture_cache_debug_options
{
	boolean graph;
	boolean list;
};

typedef char verify_xbox_texture_cache_textures_offset[
	offsetof(
		struct xbox_texture_cache_globals,
		textures) == 0 ? 1 : -1];
typedef char verify_xbox_texture_cache_base_address_offset[
	offsetof(
		struct xbox_texture_cache_globals,
		base_address) == 0x4 ? 1 : -1];
typedef char verify_xbox_texture_cache_cache_offset[
	offsetof(
		struct xbox_texture_cache_globals,
		cache) == 0x8 ? 1 : -1];
typedef char verify_xbox_texture_cache_stolen_memory_offset[
	offsetof(
		struct xbox_texture_cache_globals,
		stolen_memory) == 0xC ? 1 : -1];
typedef char verify_xbox_texture_cache_globals_size[
	sizeof(struct xbox_texture_cache_globals) == 0x10 ? 1 : -1];
typedef char verify_xbox_texture_cache_texture_loaded_offset[
	offsetof(
		struct xbox_texture_cache_texture,
		loaded) == 0x4 ? 1 : -1];
typedef char verify_xbox_texture_cache_texture_used_offset[
	offsetof(
		struct xbox_texture_cache_texture,
		used) == 0x5 ? 1 : -1];
typedef char verify_xbox_texture_cache_texture_bitmap_offset[
	offsetof(
		struct xbox_texture_cache_texture,
		bitmap) == 0x8 ? 1 : -1];
typedef char verify_xbox_texture_cache_texture_hardware_format_offset[
	offsetof(
		struct xbox_texture_cache_texture,
		hardware_format) == 0xC ? 1 : -1];
typedef char verify_xbox_texture_cache_texture_size[
	sizeof(struct xbox_texture_cache_texture) == 0x20 ? 1 : -1];
/* ---------- prototypes */

static boolean texture_cache_locked_block_proc(
	long block_index);
static void texture_cache_delete_block_proc(
	long block_index);
static const char *texture_cache_name_block_proc(
	long block_index);
static boolean compare(
	struct bitmap_data *first,
	struct bitmap_data *second);
static void texture_cache_initialize_hardware_format(
	struct bitmap_data *bitmap,
	D3DBaseTexture *texture);
static void render_inverse_transform_screen_point(
	real_point2d const *screen_position,
	real_point3d *world_position,
	real_vector3d *world_vector);
static boolean texture_cache_start_loading_bitmap(
	struct bitmap_data *bitmap,
	boolean block);

/* ---------- globals */

static const long bitmap_d3d_format_tables
	[NUMBER_OF_BITMAP_D3D_FORMAT_TABLES][NUMBER_OF_BITMAP_FORMATS] =
{
	{
		D3DFMT_A8,
		D3DFMT_L8,
		D3DFMT_AL8,
		D3DFMT_A8L8,
		NONE,
		NONE,
		D3DFMT_R5G6B5,
		NONE,
		D3DFMT_A1R5G5B5,
		D3DFMT_A4R4G4B4,
		D3DFMT_X8R8G8B8,
		D3DFMT_A8R8G8B8,
		NONE,
		NONE,
		D3DFMT_DXT1,
		D3DFMT_DXT3,
		D3DFMT_DXT5,
		D3DFMT_P8,
	},
	{
		D3DFMT_LIN_A8,
		D3DFMT_LIN_L8,
		D3DFMT_LIN_AL8,
		D3DFMT_LIN_A8L8,
		NONE,
		NONE,
		D3DFMT_LIN_R5G6B5,
		NONE,
		D3DFMT_LIN_A1R5G5B5,
		D3DFMT_LIN_A4R4G4B4,
		D3DFMT_LIN_X8R8G8B8,
		D3DFMT_LIN_A8R8G8B8,
		NONE,
		NONE,
		NONE,
		NONE,
		NONE,
		NONE,
	},
};
/* provisional name: the January map leaves this .bss array unnamed; it holds the debug
 * listing's cached bitmaps */
static struct bitmap_data *texture_cache_debug_bitmaps[XBOX_TEXTURE_CACHE_PAGE_COUNT];
static struct xbox_texture_cache_globals xbox_texture_cache_globals;
struct texture_cache_debug_options texture_cache_debug_options = {0};
boolean debug_texture_cache = FALSE;
static unsigned long texture_cache_last_failure_time = 0;
#ifdef HALO_LINUX
/* the pages in use: all of them, or HALO_TEXTURE_CACHE_PAGES's (debug) */
static long texture_cache_page_limit = XBOX_TEXTURE_CACHE_PAGE_COUNT;
#endif

/* ---------- public code */

static void texture_cache_delete_unlocked(
	void)
{
	data_dispose(xbox_texture_cache_globals.textures);
	lruv_delete(xbox_texture_cache_globals.cache);

	return;
}

static void texture_cache_open_unlocked(
	void)
{
	data_make_valid(xbox_texture_cache_globals.textures);

	return;
}

static void texture_cache_idle_unlocked(
	void)
{
	lruv_idle(xbox_texture_cache_globals.cache);

	return;
}

static void texture_cache_bitmap_new_unlocked(
	long bitmap_tag_index,
	struct bitmap_data *bitmap)
{
	struct bitmap_group *bitmap_group;

	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		157,
		!TEST_FLAG(bitmap->flags, _bitmap_cached_bit));
	SET_FLAG(bitmap->flags, _bitmap_cached_bit, TRUE);
	bitmap->cache_block_index = NONE;
	bitmap->base_address = NULL;
	bitmap->hardware_format = NULL;
	bitmap_group = bitmap_group_get(bitmap_tag_index);
	bitmap->pixels_offset += bitmap_group->pixel_data.file_offset;
	bitmap->pixels_size = bitmap_get_pixel_data_size(bitmap);
	bitmap->tag_index = bitmap_tag_index;
	bitmap->base_address = NULL;
	bitmap->hardware_format = NULL;
	bitmap->cache_block_index = NONE;

	return;
}

static void texture_cache_bitmap_delete_unlocked(
	struct bitmap_data *bitmap)
{
	long cache_block_index;

	if (TEST_FLAG(bitmap->flags, _bitmap_cached_bit))
	{
		cache_block_index = bitmap->cache_block_index;
		if (cache_block_index != NONE)
		{
			lruv_block_delete(
				xbox_texture_cache_globals.cache,
				cache_block_index);
		}
		SET_FLAG(bitmap->flags, _bitmap_cached_bit, FALSE);
		bitmap->cache_block_index = NONE;
		bitmap->base_address = NULL;
	}

	return;
}

static void *texture_cache_steal_memory_unlocked(
	long size)
{
	long page_count = size / XBOX_TEXTURE_CACHE_PAGE_SIZE + 1;
	long remaining_page_count =
		XBOX_TEXTURE_CACHE_STEALABLE_PAGE_COUNT - page_count;
	byte *base_address =
		(byte *)physical_memory_get_texture_cache_base_address() +
		remaining_page_count * XBOX_TEXTURE_CACHE_PAGE_SIZE;
	long stolen_size = page_count * XBOX_TEXTURE_CACHE_PAGE_SIZE;
	byte *stolen_address =
		base_address + XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE;
	byte *end_guard_address =
		base_address + XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE + stolen_size;

	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		0x13F,
		remaining_page_count>0);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		0x140,
		!xbox_texture_cache_globals.stolen_memory);
#ifdef HALO_LINUX
	lruv_resize(
		xbox_texture_cache_globals.cache,
		MIN(remaining_page_count, texture_cache_page_limit));
#else
	lruv_resize(
		xbox_texture_cache_globals.cache,
		remaining_page_count);
#endif
	XPhysicalProtect(
		stolen_address,
		stolen_size,
		PAGE_READWRITE);
	XPhysicalProtect(
		base_address,
		XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE,
		PAGE_READONLY);
	XPhysicalProtect(
		end_guard_address,
		XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE,
		PAGE_READONLY);
	xbox_texture_cache_globals.stolen_memory = TRUE;

	return stolen_address;
}

static void texture_cache_return_memory_unlocked(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		345,
		xbox_texture_cache_globals.stolen_memory);
#ifdef HALO_LINUX
	lruv_resize(
		xbox_texture_cache_globals.cache,
		texture_cache_page_limit);
#else
	lruv_resize(
		xbox_texture_cache_globals.cache,
		XBOX_TEXTURE_CACHE_PAGE_COUNT);
#endif
	XPhysicalProtect(
		physical_memory_get_texture_cache_base_address(),
		XBOX_TEXTURE_CACHE_SIZE,
		XBOX_TEXTURE_CACHE_PROTECTION);
	xbox_texture_cache_globals.stolen_memory = FALSE;

	return;
}

static const char *texture_cache_name_block_proc(
	long block_index)
{
	struct xbox_texture_cache_texture *texture = datum_get(
		xbox_texture_cache_globals.textures,
		block_index);

	return tag_get_name(texture->bitmap->tag_index);
}

long bitmap_format_to_d3d_format(
	short format,
	word flags)
{
	const long *table =
		bitmap_d3d_format_tables[_bitmap_d3d_format_table_regular];

	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		481,
		format>=0 && format<NUMBER_OF_BITMAP_FORMATS);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		482,
		table[format]!=NONE);
	if (TEST_FLAG(flags, _bitmap_v16u16_bit) &&
		(format == _bitmap_format_x8r8g8b8 || format == _bitmap_format_a8r8g8b8))
	{
		return D3DFMT_V16U16;
	}

	return table[format];
}

long bitmap_format_to_d3d_linear_format(
	short format,
	word flags)
{
	const long *table =
		bitmap_d3d_format_tables[_bitmap_d3d_format_table_linear];

	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		518,
		format>=0 && format<NUMBER_OF_BITMAP_FORMATS);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		519,
		table[format]!=NONE);
	if (TEST_FLAG(flags, _bitmap_v16u16_bit) &&
		(format == _bitmap_format_x8r8g8b8 || format == _bitmap_format_a8r8g8b8))
	{
		return D3DFMT_LIN_V16U16;
	}

	return table[format];
}

static boolean compare(
	struct bitmap_data *first,
	struct bitmap_data *second)
{
	long difference = rasterizer_xbox_bitmap_get_pixel_data_size(first) -
		rasterizer_xbox_bitmap_get_pixel_data_size(second);

	return difference > 0;
}

static void texture_cache_flush_unlocked(
	void)
{
	IDirect3DDevice8_KickPushBuffer(global_d3d_device);
	IDirect3DDevice8_IsBusy(global_d3d_device);
	lruv_flush(xbox_texture_cache_globals.cache);

	return;
}

static boolean texture_cache_locked_block_proc(
	long block_index)
{
#ifdef HALO_LINUX
	/* (port) a block without its texture datum (seen at map load with
	the tick thread loading textures too): reported, and not locked */
	struct xbox_texture_cache_texture *texture = datum_try_and_get(
		xbox_texture_cache_globals.textures,
		block_index);

	if (!texture)
	{
		static int reported;

		if (reported++ < 8)
			platform_log("texture cache: block 0x%08lx has no texture datum (textures count %d, first free %d, actual %d)",
				(unsigned long)block_index, xbox_texture_cache_globals.textures->count,
				xbox_texture_cache_globals.textures->first_free_absolute_index, xbox_texture_cache_globals.textures->actual_count);
		return FALSE;
	}
#else
	struct xbox_texture_cache_texture *texture = datum_get(
		xbox_texture_cache_globals.textures,
		block_index);
#endif

	return !texture->loaded ||
		IDirect3DBaseTexture8_IsBusy(&texture->hardware_format);
}

static void texture_cache_delete_block_proc(
	long block_index)
{
	struct xbox_texture_cache_texture *texture;
	struct xbox_texture_cache_texture *cache_entry;

	texture = datum_get(
		xbox_texture_cache_globals.textures,
		block_index);
#ifdef HALO_LINUX
	{
		/* (port) a block still loading is deleted only by a flush, a
		resize or the bitmap's own deletion, whose public functions wait
		for the loads before they take the cache lock (below); a wait here
		holds the lock, so a long one is named */
		unsigned long wait_started = 0, reported = 0;

		do
		{
			cache_entry = datum_get(
				xbox_texture_cache_globals.textures,
				block_index);
			if (cache_entry->loaded)
				break;
			if (!wait_started)
				wait_started = reported = system_milliseconds();
			else if (system_milliseconds() - reported > 3000)
			{
				reported = system_milliseconds();
				platform_log("texture cache: deleting %s has waited %lu s for its load (request %d), holding the cache lock",
					tag_get_name(cache_entry->bitmap->tag_index), (reported - wait_started) / 1000,
					cache_entry->read_request_handle);
			}
			SwitchToThread();
		}
		while (TRUE);
	}
#else
	do
	{
		cache_entry = datum_get(
			xbox_texture_cache_globals.textures,
			block_index);
	}
	while (!cache_entry->loaded ||
		IDirect3DBaseTexture8_IsBusy(&cache_entry->hardware_format));
#endif

	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		0x187,
		texture->bitmap->cache_block_index==block_index);
	texture->bitmap->cache_block_index = NONE;
	texture->bitmap->base_address = NULL;
	datum_delete(
		xbox_texture_cache_globals.textures,
		block_index);

	return;
}

static void texture_cache_initialize_hardware_format(
	struct bitmap_data *bitmap,
	D3DBaseTexture *texture)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		532,
		bitmap);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		533,
		texture);

	texture->Data = 0;
	texture->Lock = 0;
	texture->Common = D3DCOMMON_TYPE_TEXTURE | 1;
	if (TEST_FLAG(bitmap->flags, _bitmap_linear_bit))
	{
		texture->Format =
			(bitmap_format_to_d3d_linear_format(bitmap->format, bitmap->flags) << D3DFORMAT_FORMAT_SHIFT) |
			(1 << D3DFORMAT_MIPMAP_SHIFT) |
			(2 << D3DFORMAT_DIMENSION_SHIFT) |
			D3DFORMAT_BORDERSOURCE_COLOR |
			D3DFORMAT_DMACHANNEL_A;
		texture->Size =
			((bitmap_mipmap_get_row_pitch(bitmap, 0) / D3DTEXTURE_PITCH_ALIGNMENT - 1) << D3DSIZE_PITCH_SHIFT) |
			((bitmap->height - 1) << D3DSIZE_HEIGHT_SHIFT) |
			(bitmap->width - 1);
	}
	else
	{
		texture->Format =
			(floor_log2(bitmap->depth) << D3DFORMAT_PSIZE_SHIFT) |
			(floor_log2(bitmap->height) << D3DFORMAT_VSIZE_SHIFT) |
			(floor_log2(bitmap->width) << D3DFORMAT_USIZE_SHIFT) |
			(bitmap_format_to_d3d_format(bitmap->format, bitmap->flags) << D3DFORMAT_FORMAT_SHIFT) |
			((bitmap->type == _bitmap_type_3d ? 3 : 2) << D3DFORMAT_DIMENSION_SHIFT) |
			((rasterizer_xbox_bitmap_get_max_mipmap_count(bitmap) + 1) << D3DFORMAT_MIPMAP_SHIFT) |
			(bitmap->type == _bitmap_type_cube_map ? D3DFORMAT_CUBEMAP : 0) |
			D3DFORMAT_BORDERSOURCE_COLOR |
			D3DFORMAT_DMACHANNEL_A;
		texture->Size = 0;
	}
	IDirect3DBaseTexture8_Register(texture, bitmap->base_address);

	return;
}

static void render_inverse_transform_screen_point(
	real_point2d const *screen_position,
	real_point3d *world_position,
	real_vector3d *world_vector)
{
	real screen_x;
	real screen_y;
	real_vector3d delta0;
	real_vector3d delta1;
	real_point3d point;

	screen_x = screen_position->x * (1.0f / 640.0f);
	screen_y = 1.0f - screen_position->y * (1.0f / 480.0f);
	add_vectors3d(
		(real_vector3d const *)&render.frustum.world_vertices[4],
		global_zero_vector3d,
		(real_vector3d *)world_position);

	delta0.i = render.frustum.world_vertices[1].n[0] - render.frustum.world_vertices[0].n[0];
	delta0.j = render.frustum.world_vertices[1].n[1] - render.frustum.world_vertices[0].n[1];
	delta0.k = render.frustum.world_vertices[1].n[2] - render.frustum.world_vertices[0].n[2];
	delta1.i = render.frustum.world_vertices[2].n[0] - render.frustum.world_vertices[0].n[0];
	delta1.j = render.frustum.world_vertices[2].n[1] - render.frustum.world_vertices[0].n[1];
	delta1.k = render.frustum.world_vertices[2].n[2] - render.frustum.world_vertices[0].n[2];

	point.x = delta0.i * screen_x + render.frustum.world_vertices[0].n[0];
	point.y = delta0.j * screen_x + render.frustum.world_vertices[0].n[1];
	point.z = delta0.k * screen_x + render.frustum.world_vertices[0].n[2];
	point.x = delta1.i * screen_y + point.x;
	point.y = delta1.j * screen_y + point.y;
	point.z = delta1.k * screen_y + point.z;

	world_vector->i = point.x - world_position->x;
	world_vector->j = point.y - world_position->y;
	world_vector->k = point.z - world_position->z;

	return;
}

static void texture_cache_new_unlocked(
	void)
{
	xbox_texture_cache_globals.textures = data_new(
		"xbox texture",
		XBOX_TEXTURE_CACHE_PAGE_COUNT,
		XBOX_TEXTURE_CACHE_ENTRY_SIZE);
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		98,
		xbox_texture_cache_globals.textures != NULL,
		"xbox_texture_cache_globals.textures");
	xbox_texture_cache_globals.cache = lruv_new(
		"xbox texture cache",
		XBOX_TEXTURE_CACHE_PAGE_COUNT,
		XBOX_TEXTURE_CACHE_PAGE_SIZE_BITS,
		XBOX_TEXTURE_CACHE_PAGE_COUNT,
		texture_cache_delete_block_proc,
		texture_cache_locked_block_proc);
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		102,
		xbox_texture_cache_globals.cache != NULL,
		"xbox_texture_cache_globals.cache");
	xbox_texture_cache_globals.base_address =
		physical_memory_get_texture_cache_base_address();
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		105,
		xbox_texture_cache_globals.base_address != NULL,
		"xbox_texture_cache_globals.base_address");
#ifdef HALO_LINUX
	{
		/* (debug) HALO_TEXTURE_CACHE_PAGES=<n>: a texture cache of n 16 KB
		pages instead of 1408, for eviction churn in tests (the cache
		lock's, #18) */
		const char *setting = getenv("HALO_TEXTURE_CACHE_PAGES");
		long pages = setting ? atol(setting) : 0;

		if (pages > 0 && pages < XBOX_TEXTURE_CACHE_PAGE_COUNT)
		{
			texture_cache_page_limit = pages;
			lruv_resize(xbox_texture_cache_globals.cache, pages);
			platform_log("texture cache: %ld pages (HALO_TEXTURE_CACHE_PAGES)", pages);
		}
	}
#endif

	return;
}

static void texture_cache_close_unlocked(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		133,
		!xbox_texture_cache_globals.stolen_memory);
	texture_cache_flush();
	data_make_invalid(xbox_texture_cache_globals.textures);

	return;
}

static boolean texture_cache_start_loading_bitmap(
	struct bitmap_data *bitmap,
	boolean block)
{
	long cache_block_index;
	byte *base_address;
	long size = rasterizer_xbox_bitmap_get_pixel_data_size(bitmap);

	size = MAX(size, bitmap->pixels_size);
	cache_block_index = lruv_block_new(
		xbox_texture_cache_globals.cache,
		size);
	if (cache_block_index != NONE)
	{
		long new_texture_index;
		struct xbox_texture_cache_texture *texture;

		base_address = xbox_texture_cache_globals.base_address +
			(unsigned long)lruv_block_get_address(
				xbox_texture_cache_globals.cache,
				cache_block_index);
		new_texture_index = datum_new_at_index(
			xbox_texture_cache_globals.textures,
			cache_block_index);
		texture = datum_get(
			xbox_texture_cache_globals.textures,
			cache_block_index);
		match_assert(
			"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
			431,
			new_texture_index==cache_block_index);
		bitmap->cache_block_index = cache_block_index;
		bitmap->base_address = base_address;
		texture->bitmap = bitmap;
		texture_cache_initialize_hardware_format(bitmap, &texture->hardware_format);
		texture->read_request_handle = cache_file_read(
			bitmap->tag_index,
			bitmap->pixels_offset,
			bitmap->pixels_size,
			base_address,
			&texture->loaded,
			block);

		return TRUE;
	}

	return FALSE;
}

static void texture_cache_debug_render_unlocked(
	void)
{
	if (texture_cache_debug_options.graph)
	{
		byte page_usage[XBOX_TEXTURE_CACHE_PAGE_COUNT];
		real_point3d world_positions[2];
		real_point2d screen_positions[2];
		real_point3d *world_position;
		real_point2d *screen_position;
		real_argb_color const *colors[4];
		short window_width;
		real distance;
		real scale;
		short x_offset;
		short y_offset;
		real_vector3d world_vector;
		long page_index;
		long state_index;
		long point_index;

		window_width = render.camera.window_bounds.x1 -
			render.camera.window_bounds.x0;
		colors[0] = global_real_argb_red;
		colors[1] = global_real_argb_green;
		colors[2] = global_real_argb_blue;
		colors[3] = NULL;
		lruv_cache_get_page_usage(xbox_texture_cache_globals.cache, page_usage);

		for (page_index = 0; page_index < XBOX_TEXTURE_CACHE_PAGE_COUNT; page_index++)
		{
			x_offset = render.camera.window_bounds.x0 -
				render.camera.viewport_bounds.x0;
			y_offset = (render.camera.window_bounds.y0 -
				render.camera.viewport_bounds.y0) * 4;

			for (state_index = 0; state_index < 3; state_index++)
			{
				if (page_usage[page_index] & FLAG(state_index))
				{
					screen_positions[0].x = (real)(x_offset + page_index % window_width);
					screen_positions[0].y = (real)(y_offset +
						(state_index + (page_index / window_width) * 4) * 10);
					screen_positions[1].x = (real)(x_offset + page_index % window_width);
					screen_positions[1].y = (real)(y_offset +
						(state_index + (page_index / window_width) * 4) * 10 + 10);
					distance = render.camera.z_near + 0.001f;

					world_position = world_positions;
					screen_position = screen_positions;
					for (point_index = 2; point_index; point_index--)
					{
						render_inverse_transform_screen_point(
							screen_position,
							world_position,
							&world_vector);
						scale = distance /
							dot_product3d(&render.camera.forward, &world_vector);
						world_position->x += world_vector.i * scale;
						world_position->y += world_vector.j * scale;
						world_position->z += world_vector.k * scale;
						screen_position++;
						world_position++;
					}

					render_debug_line(
						TRUE,
						&world_positions[0],
						&world_positions[1],
						colors[state_index]);
				}
			}
		}
	}

	if (texture_cache_debug_options.list)
	{
		char string[1024];
		struct data_iterator iterator;
		short tab_stops[2];
		rectangle2d bounds;
		struct xbox_texture_cache_texture *texture;
		short bitmap_count = 0;
		long font_index;
		long bitmap_index;

		data_iterator_new(&iterator, xbox_texture_cache_globals.textures);
		while ((texture = data_iterator_next(&iterator)) != NULL)
		{
			struct bitmap_data *bitmap = texture->bitmap;

			if (bitmap->tag_index != NONE)
			{
				texture_cache_debug_bitmaps[bitmap_count++] = bitmap;
			}
		}
		qsort_4byte(
			(long *)texture_cache_debug_bitmaps,
			bitmap_count,
			(boolean (*)(long, long))compare);

		font_index = interface_get_tag_index(_interface_font_terminal);
		tab_stops[0] = rasterizer_globals.reserved04.frame_bounds.x0;
		tab_stops[1] = rasterizer_globals.reserved04.frame_bounds.x0 + 110;
		draw_string_set_tab_stops(tab_stops, 2);
		if (font_index != NONE)
		{
			draw_string_set_font(font_index);
		}

		for (bitmap_index = bitmap_count - 1; bitmap_index >= 0; bitmap_index--)
		{
			char const *touched_string = lruv_block_touched(
				xbox_texture_cache_globals.cache,
				texture_cache_debug_bitmaps[bitmap_index]->cache_block_index) ? "" : "*";

			sprintf(
				string,
				"|t%d|t%s%s",
				rasterizer_xbox_bitmap_get_pixel_data_size(texture_cache_debug_bitmaps[bitmap_index]),
				touched_string,
				tag_get_name(texture_cache_debug_bitmaps[bitmap_index]->tag_index));
			bounds.y0 = (bitmap_count - bitmap_index) * 10 + 35;
			bounds.x0 = 10;
			bounds.y1 = bounds.x1 = SHORT_MAX;
			draw_string_set_color(global_real_argb_yellow);
			rasterizer_draw_string(&bounds, NULL, NULL, 0, string);
		}
	}

	return;
}

void *_texture_cache_bitmap_get_hardware_format(
	struct bitmap_data *bitmap,
	boolean block,
	boolean load)
{
	void *hardware_format = NULL;

#ifdef HALO_LINUX
	{
		/* (harness) a repeatable run never draws without a texture that
		is still loading (main.c) */
		int halo_repeatable_run(void);

		if (load && halo_repeatable_run())
			block = TRUE;
	}
#endif
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		210,
		load || !block);
	if (TEST_FLAG(bitmap->flags, _bitmap_cached_bit))
	{
#ifdef HALO_LINUX
		/* (port) the load's start, the lookup and the touch under the cache
		lock: the tick (predicted resources, the lights' texture samples)
		and the render start and look up loads at once, and the other
		thread's evictions deleted the block in between - the texture datums
		were made outside the lock, and a block evicted between the lookup
		and the touch crashed in lruv_block_touch (the gxm-null harness,
		with a small texture cache) */
		struct xbox_texture_cache_texture *texture = NULL;

		halo_cache_lock_acquire();
		if (bitmap->cache_block_index == NONE && load)
		{
			texture_cache_start_loading_bitmap(bitmap, block);
		}
		if (bitmap->cache_block_index != NONE)
		{
			texture = datum_get(
				xbox_texture_cache_globals.textures,
				bitmap->cache_block_index);
			lruv_block_touch(
				xbox_texture_cache_globals.cache,
				bitmap->cache_block_index);
		}
		halo_cache_lock_release();
		if (texture)
		{
#else
		if (bitmap->cache_block_index == NONE && load)
		{
			texture_cache_start_loading_bitmap(bitmap, block);
		}
		if (bitmap->cache_block_index != NONE)
		{
			struct xbox_texture_cache_texture *texture = datum_get(
				xbox_texture_cache_globals.textures,
				bitmap->cache_block_index);

			lruv_block_touch(
				xbox_texture_cache_globals.cache,
				bitmap->cache_block_index);
#endif
			if (block && !texture->loaded)
			{
				if (debug_texture_cache)
				{
					console_warning(
						"%s",
						tag_get_name(bitmap->tag_index));
				}
				cache_file_promote_read(texture->read_request_handle);
			}
#ifdef HALO_LINUX
			{
			unsigned long long wait_started = block && !texture->loaded ? halo_load_profile_now() : 0;
#endif
			do
			{
				if (texture->loaded)
				{
					if (!texture->used)
					{
						texture->used = TRUE;
					}
					hardware_format = &texture->hardware_format;
				}
				else
				{
					if (system_milliseconds() - sound_render_time() > 132)
					{
						sound_idle();
					}
#ifdef HALO_LINUX
					{
						/* the wait is for the IO thread, whose completions
						may need the cache lock this thread holds */
						int lock_depth = halo_cache_lock_suspend();
						SwitchToThread();
						halo_cache_lock_resume(lock_depth);
					}
#else
					SwitchToThread();
#endif
				}
			}
			while (!hardware_format && block);
#ifdef HALO_LINUX
			if (wait_started)
				halo_load_profile_add(_halo_load_texture_cache_wait, wait_started, 0);
			}
#endif
		}
	}
	else
	{
		hardware_format = bitmap->hardware_format;
	}

	if (block && !hardware_format)
	{
		if (system_milliseconds() - texture_cache_last_failure_time > 10000)
		{
			terminal_printf(
				global_real_argb_purple,
				"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
			error(
				_error_silent,
				"YOU GOT STABBED!!!! double-click \"GETSTABBED.BAT\" on your PC now!!!");
			terminal_printf(
				global_real_argb_purple,
				"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
			lruv_debug_to_file(
				"d:\\stabbed.txt",
				tag_get_name(bitmap->tag_index),
				bitmap->pixels_size,
				xbox_texture_cache_globals.cache,
				scenario_debug_to_file,
				texture_cache_name_block_proc);
			texture_cache_last_failure_time = system_milliseconds();
		}
		hardware_format = rasterizer_get_bitmap_default_hardware_format(bitmap);
		match_assert(
			"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
			295,
			hardware_format);
	}

	return hardware_format;
}

/* ---------- the locked public functions (port) */

#ifdef HALO_LINUX
/* The deletions that can meet a texture still loading (a flush, a resize,
a bitmap's deletion) wait for the textures' loads first, without the cache
lock:
texture_cache_delete_block_proc waits for a load while the lock is held,
and nothing else could take the lock meanwhile. (The cache file thread
needs no lock to finish a load, so it was a stall, not a deadlock; a read
that never finished - the request slot two threads claimed at once,
cache_files_windows.c - held it for good.) */
static void texture_cache_wait_for_bitmap_load(
	struct bitmap_data *bitmap)
{
	long cache_block_index;
	struct xbox_texture_cache_texture *texture;

	if (!bitmap || !TEST_FLAG(bitmap->flags, _bitmap_cached_bit))
		return;
	cache_block_index = bitmap->cache_block_index;
	if (cache_block_index == NONE)
		return;
	texture = datum_try_and_get(
		xbox_texture_cache_globals.textures,
		cache_block_index);
	while (texture &&
		!*(volatile boolean *)&texture->loaded &&
		*(volatile long *)&bitmap->cache_block_index == cache_block_index)
	{
		SwitchToThread();
	}
}

/* every texture's: the texture datums are read without the lock (a datum
made meanwhile is waited for too, or missed; its load is short either
way) */
static void texture_cache_wait_for_loads(
	void)
{
	struct data_array *textures = xbox_texture_cache_globals.textures;
	short index;

	if (!textures || !textures->data)
		return;
	for (index = 0; index < textures->count; index++)
	{
		struct xbox_texture_cache_texture *texture = (struct xbox_texture_cache_texture *)
			((byte *)textures->data + textures->size * index);

		while (*(volatile short *)&texture->identifier && !*(volatile boolean *)&texture->loaded)
			SwitchToThread();
	}
}
#endif

void texture_cache_delete(
	void)
{
	halo_cache_lock_acquire();
	texture_cache_delete_unlocked();
	halo_cache_lock_release();
}

void texture_cache_open(
	void)
{
	halo_cache_lock_acquire();
	texture_cache_open_unlocked();
	halo_cache_lock_release();
}

void texture_cache_idle(
	void)
{
	halo_cache_lock_acquire();
	texture_cache_idle_unlocked();
	halo_cache_lock_release();
}

void texture_cache_bitmap_new(
	long bitmap_tag_index,
	struct bitmap_data *bitmap)
{
	halo_cache_lock_acquire();
	texture_cache_bitmap_new_unlocked(bitmap_tag_index, bitmap);
	halo_cache_lock_release();
}

void texture_cache_bitmap_delete(
	struct bitmap_data *bitmap)
{
#ifdef HALO_LINUX
	texture_cache_wait_for_bitmap_load(bitmap);
#endif
	halo_cache_lock_acquire();
	texture_cache_bitmap_delete_unlocked(bitmap);
	halo_cache_lock_release();
}

void *texture_cache_steal_memory(
	long size)
{
	void * result;
#ifdef HALO_LINUX
	texture_cache_wait_for_loads();
#endif
	halo_cache_lock_acquire();
	result = texture_cache_steal_memory_unlocked(size);
	halo_cache_lock_release();
	return result;
}

void texture_cache_return_memory(
	void)
{
	halo_cache_lock_acquire();
	texture_cache_return_memory_unlocked();
	halo_cache_lock_release();
}

void texture_cache_flush(
	void)
{
#ifdef HALO_LINUX
	texture_cache_wait_for_loads();
#endif
	halo_cache_lock_acquire();
	texture_cache_flush_unlocked();
	halo_cache_lock_release();
}

void texture_cache_new(
	void)
{
	halo_cache_lock_acquire();
	texture_cache_new_unlocked();
	halo_cache_lock_release();
}

void texture_cache_close(
	void)
{
#ifdef HALO_LINUX
	texture_cache_wait_for_loads();
#endif
	halo_cache_lock_acquire();
	texture_cache_close_unlocked();
	halo_cache_lock_release();
}

void texture_cache_debug_render(
	void)
{
	halo_cache_lock_acquire();
	texture_cache_debug_render_unlocked();
	halo_cache_lock_release();
}
