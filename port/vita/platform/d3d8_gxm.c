/*
D3D8_GXM.C

The Xbox Direct3D 8 device on the Vita's GPU: port/linux/src/d3d8_gl.c's
device, with its draws handed to the GXM renderer (port/vita/host/vita_gxm.c)
instead of OpenGL.

As on the Xbox, the GPU reads vertices and indices where the game keeps them
when they are part of the loaded map (the tag cache, which the GPU has
mapped); everything else (the dynamic vertices the game rewrites every
frame) is copied into the frame's ring for the draw. Vertex constants and
the other uniforms are snapshots in the ring, written when they change.
Render targets live in the GPU's own memory, found by the physical address
the game gave their surface, as in the OpenGL device.
*/

#include "vita_xgpu.h"
#include "vita_gxm.h"
#include "sdl_platform.h"
#include "halo_ui_pointer.h"
#include "port_config.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vita_compat.h"
#include "vita_host.h"
#include "frame_timing.h"

void d3d8_surface_initialize(D3DSurface *surface, D3DFORMAT format, unsigned long width, unsigned long height);
void d3d8_surface_resize(D3DSurface *surface, D3DFORMAT format, unsigned long width, unsigned long height);
void *physical_memory_get_tag_cache_base_address(void);

#define TAG_CACHE_SIZE 0x01600000UL

/* ---------- the screen

The Xbox screen is 640x480; a wider one (display.screen_width, or
HALO_DISPLAY_WIDTH) widens the 3D view as on Android. */

#define SCREEN_HEIGHT 480

static long screen_width;
static long ui_offset;

long halo_screen_width(void)
{
	if (!screen_width)
	{
		const char *display = getenv("HALO_DISPLAY_WIDTH");

		screen_width = config_integer("display.screen_width");
		if (screen_width <= 0)
			screen_width = display ? atol(display) : 640;
		if (screen_width < 640)
			screen_width = 640;
		if (screen_width > 1024)
			screen_width = 1024;
		screen_width &= ~1L;
		platform_log("screen: %ldx%d", screen_width, SCREEN_HEIGHT);
	}
	return screen_width;
}

void halo_screen_ui_offset(unsigned char centered)
{
	ui_offset = centered ? (halo_screen_width() - 640) / 2 : 0;
}

long halo_screen_commit(void)
{
	return halo_screen_width();
}

extern int vita_menus_active;

/* (no pointer on the Vita; the menus' state picks the D-pad's meaning,
port/vita/platform/vita_pad.c) */
int halo_ui_pointer_update(int menus_active, struct halo_ui_pointer *pointer)
{
	(void)pointer;
	vita_menus_active = menus_active != 0;
	return 0;
}

/* ---------- state the XDK header's inline functions read and write */

DWORD D3D__RenderState[D3DRS_MAX];
/* set by the render and texture stage state setters when they change a
value: the split records' render and stage states are compared only when
something changed since the last draw (the textures are compared at every
draw). Bit STATE_DIRTY_MATERIAL: a state of the record's material changed;
STATE_DIRTY_VALUES: one of its per-draw values (record_values) */
#define STATE_DIRTY_MATERIAL 1
#define STATE_DIRTY_VALUES 2
static int device_state_dirty = STATE_DIRTY_MATERIAL | STATE_DIRTY_VALUES;

/* The render states that are a draw's values rather than its material:
the pixel shader's constants, the fog's colour and range, and the cull mode
(the game sets them per object and part - the fog by the object's distance,
a two-sided part's second pass's winding - while the rest stays the same:
on b30 59% of the new materials differed from the last in these only) */
static const unsigned char render_state_values[D3DRS_MAX] = {
	[D3DRS_PSCONSTANT0_0] = 1, [D3DRS_PSCONSTANT0_1] = 1, [D3DRS_PSCONSTANT0_2] = 1, [D3DRS_PSCONSTANT0_3] = 1,
	[D3DRS_PSCONSTANT0_4] = 1, [D3DRS_PSCONSTANT0_5] = 1, [D3DRS_PSCONSTANT0_6] = 1, [D3DRS_PSCONSTANT0_7] = 1,
	[D3DRS_PSCONSTANT1_0] = 1, [D3DRS_PSCONSTANT1_1] = 1, [D3DRS_PSCONSTANT1_2] = 1, [D3DRS_PSCONSTANT1_3] = 1,
	[D3DRS_PSCONSTANT1_4] = 1, [D3DRS_PSCONSTANT1_5] = 1, [D3DRS_PSCONSTANT1_6] = 1, [D3DRS_PSCONSTANT1_7] = 1,
	[D3DRS_PSFINALCOMBINERCONSTANT0] = 1, [D3DRS_PSFINALCOMBINERCONSTANT1] = 1,
	[D3DRS_FOGSTART] = 1, [D3DRS_FOGEND] = 1, [D3DRS_FOGDENSITY] = 1, [D3DRS_FOGCOLOR] = 1,
	[D3DRS_CULLMODE] = 1,
};

/* the dirty bit a change of a render or stage state sets */
static int render_state_dirty_bit(unsigned long state)
{
	return state < D3DRS_MAX && render_state_values[state] ? STATE_DIRTY_VALUES : STATE_DIRTY_MATERIAL;
}

static int texture_state_dirty_bit(unsigned long type)
{
	return type >= D3DTSS_BUMPENVMAT00 && type <= D3DTSS_BUMPENVLOFFSET ? STATE_DIRTY_VALUES : STATE_DIRTY_MATERIAL;
}
DWORD D3D__TextureState[D3DTSS_MAXSTAGES][D3DTSS_MAX];
WORD *D3D__IndexData;
BYTE D3D__StateBlockDirty[1024];

/* ---------- vertex shaders */

#define VERTEX_SHADER_SIGNATURE 0x76736864UL /* 'vshd' */
#define VERTEX_PROGRAM_SLOTS 136

struct vertex_element
{
	unsigned char reg;
	unsigned char stream;
	unsigned char type;
	unsigned char bytes;
	unsigned short offset;
};

/* a program's Cg translation for one set of input registers */
struct vertex_variant
{
	struct vertex_variant *next;
	unsigned long provided_mask, packed_mask, color_mask;
	unsigned long shader;
};

struct vertex_shader_object
{
	unsigned long signature;
	unsigned long id;
	DWORD *instructions;
	unsigned long instruction_count;
	struct vertex_element elements[XGPU_VERTEX_ATTRIBUTE_COUNT];
	unsigned long element_count;
	unsigned long packed_mask;
	unsigned long color_mask;
	unsigned long provided_mask;
	/* the constant registers the program reads (by chunk: vita_xgpu.h) */
	struct nv2a_vertex_constant_usage usage;
	/* the oT outputs whose w the program writes (nv2a_vsh_cg.c) */
	unsigned long texcoord_w_mask;
	/* the input registers the program reads: an immediate-mode draw's
	vertices carry only those */
	unsigned long input_mask;
	struct vertex_variant *variants;
};

/* ---------- pixel shaders */

struct fragment_entry
{
	struct fragment_entry *next;
	unsigned long hash;
	struct nv2a_pixel_shader_key key;
	unsigned long shader;
};

#define FRAGMENT_BUCKETS 1024

static struct fragment_entry *fragment_buckets[FRAGMENT_BUCKETS];

/* ---------- render targets */

struct render_target_entry
{
	struct render_target_entry *next;
	struct render_target_entry *next_in_bucket;
	struct xgpu_render_target target;
	/* a small target rendered several times a frame has a copy per run,
	so the runs can be drawn before the main scene (the worker) */
	unsigned long version;
	unsigned long id;
	struct vgxm_texture texture;
	unsigned long last_rendered;
	/* the frame it was last drawn into or sampled (render_target_recycle) */
	unsigned long last_used;
	/* a texture rendered level by level (the water's ripple bump map): the
	levels' targets share one mip chain, the first level's texture covers
	it; -1 when the chain could not be made */
	long chain_levels;
};

#define RENDER_TARGET_BUCKET_COUNT 256

static struct render_target_entry *render_target_buckets[RENDER_TARGET_BUCKET_COUNT];
static struct render_target_entry *render_targets;

static struct render_target_entry **render_target_bucket(unsigned long data)
{
	return &render_target_buckets[((data >> 12) ^ (data >> 20)) % RENDER_TARGET_BUCKET_COUNT];
}

/* ---------- the device */

#define VISIBILITY_TEST_SLOTS 4096

struct gxm_device
{
	D3DPRESENT_PARAMETERS presentation;
	D3DSurface back_buffer;
	D3DSurface depth_buffer;
	D3DSurface *render_target;
	D3DSurface *depth_stencil;
	D3DVIEWPORT8 viewport;
	D3DMATRIX transforms[D3DTS_MAX];
	D3DBaseTexture *textures[D3DTSS_MAXSTAGES];
	D3DPalette *palettes[D3DTSS_MAXSTAGES];
	D3DSHADERCONSTANTMODE shader_constant_mode;

	struct vertex_shader_object *vertex_shader;
	struct vertex_shader_object *program_slots[VERTEX_PROGRAM_SLOTS];
	unsigned long program_address;
	float viewport_scale[4];
	float viewport_offset[4];

	struct
	{
		DWORD data;
		UINT stride;
	} streams[16];
	UINT base_vertex_index;

	BOOL immediate_active;
	D3DPRIMITIVETYPE immediate_type;
	/* the input registers an immediate draw's vertices carry (those its
	program reads: immediate_input_mask), taken at Begin, and their floats
	per vertex - a vertex keeps those, not all sixteen registers */
	unsigned long immediate_mask, immediate_floats;
	float *immediate_vertices;
	unsigned long immediate_count;
	unsigned long self_sampled;
	unsigned long immediate_capacity;

	BOOL visibility_test_active;
	/* the open test's slot in the frame's visibility buffer (1 on: tests
	are numbered in the order they begin, from 1 at each Present), which
	its draws count into; the game names a test only when it ends */
	unsigned long visibility_index;
	unsigned long visibility_tests_this_frame;
	/* by the game's test index: the slot that test counted into */
	unsigned short visibility_slot_of_test[VISIBILITY_TEST_SLOTS];

	/* per stage: the texture with its sampler state applied, and what it
	was made from (reused while the same) */
	struct vgxm_texture sampled[D3DTSS_MAXSTAGES];
	struct
	{
		const struct vgxm_texture *source;
		unsigned long control[4];
		DWORD state[6];
	} sampled_key[D3DTSS_MAXSTAGES];

	/* the vertex constants and, per chunk (vita_xgpu.h), its snapshot in
	this frame's ring (NULL when a register of it changed since); chunk D
	is copied up to the node matrices written lately: the registers written
	this frame and the last, and the length the snapshot has */
	float constants[XGPU_VERTEX_CONSTANT_COUNT][4];
	const void *chunk_snapshot[VITA_VC_CHUNKS];
	unsigned long d_snapshot_count;
	unsigned long d_extent_frame, d_extent_previous;
	/* the registers the last write from D's first register covered: the
	node matrices of the object being drawn; what lies past them is stale */
	unsigned long d_last_object_extent;
	/* the vertex programs' BUFFER[1] (vita_xgpu.h), likewise */
	float vertex_uniforms[VITA_VM_COUNT][4];
	const void *vertex_uniform_snapshot;
	/* an input register's current value changed since the snapshot (its
	other rows are still right): a draw reading those rows needs a new one,
	an immediate draw - whose inputs all come from its vertices - does not */
	BOOL vertex_attributes_changed;
	float fragment_uniforms[VITA_FU_COUNT][4];
	/* (two: vita_xgpu.h VITA_FU_A_COUNT) */
	const void *fragment_snapshot[2];

	/* indices 0..65535, for draws without their own */
	unsigned short *sequential_indices;

	unsigned long frame;
	unsigned long next_vertex_shader_id;
	BOOL gpu_ready;
	BOOL created;
};

static struct gxm_device device;

#define CONSTANT(index) (device.constants[index])

static struct
{
	unsigned long draws, immediate_draws, clears, presents, self_sampled, computed_draws, alpha_tested_draws, dropped_alpha_tests;
	/* draws whose state (key, textures, samplers, render states) equals the previous draw's: what a delta record could skip */
	unsigned long same_state_draws;
	unsigned long skipped_no_program, skipped_no_target, skipped_shader;
	unsigned long target_changes;
	unsigned long copied_bytes, direct_bytes;
	unsigned long vertex_snapshots, fragment_snapshots;
	/* copied bytes by kind: streams in the window, immediate vertices, indices, uniforms */
	unsigned long copied_streams, copied_immediate, copied_indices, copied_uniforms;
	/* the uniform bytes by kind: the vertex constant chunks and BUFFER[1]
	(the game's thread), the fragment snapshots (the worker's) */
	unsigned long copied_chunk[VITA_VC_CHUNKS], copied_vertex_misc, copied_fragment;
	/* split records: draws that reused the last state block, materials
	compared equal to the last after a setter marked them dirty, and new
	blocks; the worker's full translations of a block */
	unsigned long state_quick, state_equal, state_new, worker_builds;
	/* new material blocks among the new blocks; the worker's translations
	of a block that kept the last block's material (texture parts only) */
	unsigned long material_new, worker_texture_builds;
	/* new values blocks (record_values) */
	unsigned long values_new;
} stats;

/* draws recorded since start-up, never reset: the render profile counts
them per phase (halo_render_draw_counts) */
static unsigned long draw_counter_stream, draw_counter_immediate;

void halo_render_draw_counts(unsigned long *stream, unsigned long *immediate)
{
	*stream = draw_counter_stream;
	*immediate = draw_counter_immediate;
}

/* time spent in this layer (debug.gpu_stats) */
unsigned long long vita_host_time_us(void);
static unsigned long long layer_time, layer_entered, present_wait_time;
static int layer_depth;

static int gpu_stats_enabled(void);

/* (the clock is read only for the statistics: a read is a 0.7 us system
call on the Vita, and the draw calls came in and out through here) */
static void layer_enter(void)
{
	if (!layer_depth++ && gpu_stats_enabled())
		layer_entered = vita_host_time_us();
}

static void layer_leave(void)
{
	if (!--layer_depth && layer_entered)
	{
		layer_time += vita_host_time_us() - layer_entered;
		layer_entered = 0;
	}
}

static D3DDevice *device_pointer(void)
{
	return (D3DDevice *)&device;
}

static float dword_to_float(DWORD value)
{
	union { DWORD d; float f; } u;

	u.d = value;
	return u.f;
}

/* n / 255.0f for each byte, folded by the compiler (the same correctly
rounded quotients the divisions gave: a state block's fragment uniforms
convert nineteen colours, 76 divisions) */
#define UNIT4(n) (n) / 255.0f, ((n) + 1) / 255.0f, ((n) + 2) / 255.0f, ((n) + 3) / 255.0f
#define UNIT16(n) UNIT4(n), UNIT4((n) + 4), UNIT4((n) + 8), UNIT4((n) + 12)
#define UNIT64(n) UNIT16(n), UNIT16((n) + 16), UNIT16((n) + 32), UNIT16((n) + 48)
static const float byte_unit[256] = { UNIT64(0), UNIT64(64), UNIT64(128), UNIT64(192) };
#undef UNIT64
#undef UNIT16
#undef UNIT4

static void color_to_vec4(D3DCOLOR color, float *out)
{
	out[0] = byte_unit[(color >> 16) & 0xff];
	out[1] = byte_unit[(color >> 8) & 0xff];
	out[2] = byte_unit[color & 0xff];
	out[3] = byte_unit[(color >> 24) & 0xff];
}

static BOOL trace_frame(void)
{
	static long frame = -2;

	if (frame == -2)
		frame = config_integer("debug.gpu_trace_frame");
	return frame >= 0 && device.frame == (unsigned long)frame;
}

/* ---------- memory the GPU reads where it is: the loaded map */

static BOOL memory_is_static(const void *address, unsigned long size)
{
	static unsigned long base;

	if (!base)
		base = (unsigned long)physical_memory_get_tag_cache_base_address();
	return base && (unsigned long)address >= base && (unsigned long)address + size <= base + TAG_CACHE_SIZE;
}

/* bytes in this frame's ring; a full ring drops the draw */
static void *ring_copy(const void *data, unsigned long size)
{
	void *copy = vgxm_ring_alloc(size ? size : 4, 16);

	if (copy && size)
		memcpy(copy, data, size);
	stats.copied_bytes += size;
	return copy;
}

/* ---------- vertical blank emulation (as the OpenGL device) */

#define VERTICAL_BLANK_NANOSECONDS (1000000000L / 60)

static pthread_mutex_t vertical_blank_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t vertical_blank_condition = PTHREAD_COND_INITIALIZER;
static D3DCALLBACK vertical_blank_callback;
static unsigned long vertical_blank_count;
static volatile unsigned int flip_count;
static unsigned long pending_flips;
static BOOL vertical_blank_thread_started = FALSE;

static void *vertical_blank_thread(void *unused)
{
	struct timespec next;

	(void)unused;
	clock_gettime(CLOCK_MONOTONIC, &next);
	for (;;)
	{
		D3DCALLBACK callback;

		next.tv_nsec += VERTICAL_BLANK_NANOSECONDS;
		if (next.tv_nsec >= 1000000000L)
		{
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

		pthread_mutex_lock(&vertical_blank_lock);
		vertical_blank_count++;
		if (pending_flips)
		{
			pending_flips--;
			flip_count++;
		}
		callback = vertical_blank_callback;
		pthread_cond_broadcast(&vertical_blank_condition);
		pthread_mutex_unlock(&vertical_blank_lock);

		if (callback)
			callback(0);
	}
	return NULL;
}

static void vertical_blank_start(void)
{
	pthread_mutex_lock(&vertical_blank_lock);
	if (!vertical_blank_thread_started)
	{
		pthread_t thread;

		if (pthread_create(&thread, NULL, vertical_blank_thread, NULL) == 0)
		{
			pthread_detach(thread);
			vertical_blank_thread_started = TRUE;
		}
		else
		{
			platform_log("cannot start the vertical blank thread");
		}
	}
	pthread_mutex_unlock(&vertical_blank_lock);
}

volatile unsigned int *d3d_find_flipcount(void)
{
	return &flip_count;
}

void WINAPI D3DDevice_SetVerticalBlankCallback(D3DCALLBACK callback)
{
	pthread_mutex_lock(&vertical_blank_lock);
	vertical_blank_callback = callback;
	pthread_mutex_unlock(&vertical_blank_lock);
	vertical_blank_start();
}

void WINAPI D3DDevice_BlockUntilVerticalBlank(void)
{
	unsigned long count;

	vertical_blank_start();
	pthread_mutex_lock(&vertical_blank_lock);
	count = vertical_blank_count;
	while (vertical_blank_count == count)
		pthread_cond_wait(&vertical_blank_condition, &vertical_blank_lock);
	pthread_mutex_unlock(&vertical_blank_lock);
}

/* ---------- render targets */

static void surface_dimensions(const D3DSurface *surface, unsigned long *width, unsigned long *height, BOOL *depth)
{
	struct xgpu_texture_description description;
	DWORD format;

	xgpu_texture_describe(surface->Format, surface->Size, &description);
	*width = description.width;
	*height = description.height;
	format = description.format;
	*depth = format == D3DFMT_D24S8 || format == D3DFMT_F24S8 || format == D3DFMT_D16 || format == D3DFMT_F16 ||
		format == D3DFMT_LIN_D24S8 || format == D3DFMT_LIN_F24S8 || format == D3DFMT_LIN_D16 || format == D3DFMT_LIN_F16;
}

/* a target of the same size that has been neither drawn into nor sampled
for ten seconds, taken over when no more targets can be made: they are
never freed, and every map's surfaces add their own - after an hour and a
level change the glow's 128x128 targets were not made any more (the bloom
went). It leaves its old surface's bucket; that surface gets a new target
if it comes back */
static struct render_target_entry *render_target_recycle(unsigned long width, unsigned long height, BOOL depth)
{
	struct render_target_entry *entry, **link;

	for (entry = render_targets; entry; entry = entry->next)
	{
		if (entry->id && !entry->chain_levels && entry->target.width == width && entry->target.height == height &&
			entry->target.depth == depth && entry->last_used + 300 < device.frame)
		{
			break;
		}
	}
	if (!entry)
		return NULL;
	for (link = render_target_bucket(entry->target.data); *link; link = &(*link)->next_in_bucket)
	{
		if (*link == entry)
		{
			*link = entry->next_in_bucket;
			break;
		}
	}
	return entry;
}

static struct render_target_entry *render_target_get_version(const D3DSurface *surface, unsigned long version)
{
	struct render_target_entry *entry;
	unsigned long width, height;
	BOOL depth;

	if (!surface || !surface->Data)
		return NULL;
	surface_dimensions(surface, &width, &height, &depth);
	for (entry = *render_target_bucket(surface->Data); entry; entry = entry->next_in_bucket)
	{
		if (entry->target.data == surface->Data && entry->target.width == width &&
			entry->target.height == height && entry->target.depth == depth && entry->version == version)
		{
			return entry->id ? entry : NULL;
		}
	}
	{
		struct vgxm_texture texture;
		unsigned long id = vgxm_target_create(width, height, depth, &texture);

		if (id)
		{
			entry = calloc(1, sizeof(*entry));
			entry->id = id;
			entry->texture = texture;
			entry->next = render_targets;
			render_targets = entry;
		}
		else if ((entry = render_target_recycle(width, height, depth)) != NULL)
		{
			static unsigned long recycled;

			if (++recycled <= 20)
				platform_log("render target recycled for a %lux%lu %s surface (%lu so far)", width, height,
					depth ? "depth" : "colour", recycled);
		}
		else
		{
			entry = calloc(1, sizeof(*entry));
			entry->next = render_targets;
			render_targets = entry;
			platform_log("cannot create a %lux%lu %s target", width, height, depth ? "depth" : "colour");
		}
	}
	entry->version = version;
	memset(&entry->target, 0, sizeof(entry->target));
	entry->target.data = surface->Data;
	entry->target.width = width;
	entry->target.height = height;
	entry->target.depth = depth;
	entry->target.scale[0] = entry->target.scale[1] = 1.0f;
	entry->target.gl_width = width;
	entry->target.gl_height = height;
	entry->last_rendered = 0;
	entry->last_used = device.frame + 1;
	entry->next_in_bucket = *render_target_bucket(entry->target.data);
	*render_target_bucket(entry->target.data) = entry;
	return entry->id ? entry : NULL;
}

static struct render_target_entry *render_target_get(const D3DSurface *surface)
{
	return render_target_get_version(surface, 0);
}

static struct render_target_entry *render_target_entry_find_version(unsigned long data, unsigned long version)
{
	struct render_target_entry *entry, *best = NULL;

	for (entry = *render_target_bucket(data); entry; entry = entry->next_in_bucket)
	{
		if (entry->id && entry->target.data == data && !entry->target.depth && entry->version == version &&
			(!best || entry->last_rendered > best->last_rendered))
		{
			best = entry;
		}
	}
	return best;
}

static struct render_target_entry *render_target_entry_find(unsigned long data)
{
	return render_target_entry_find_version(data, 0);
}

/* the targets of a texture the game renders level by level, re-made as one
mip chain the first time it is sampled with its levels (the levels' earlier
targets are left unused) */
static void render_target_chain(struct render_target_entry *base, const struct xgpu_texture_description *description,
	unsigned long data, unsigned long version)
{
	unsigned long ids[12], levels = description->levels, level;
	struct vgxm_texture texture;

	if (base->chain_levels)
		return;
	base->chain_levels = -1;
	if (levels > 12)
		levels = 12;
	if (description->linear || description->cube_map || description->depth > 1 ||
		(base->target.width & (base->target.width - 1)) || (base->target.height & (base->target.height - 1)) ||
		vgxm_target_create_chain(base->target.width, base->target.height, levels, ids, &texture) != 0)
	{
		platform_log("cannot chain the %lux%lu target's %lu levels", base->target.width, base->target.height, levels);
		return;
	}
	for (level = 0; level < levels; level++)
	{
		unsigned long level_data = data + xgpu_texture_level_offset(description, level);
		unsigned long width = base->target.width >> level ? base->target.width >> level : 1;
		unsigned long height = base->target.height >> level ? base->target.height >> level : 1;
		struct render_target_entry *entry;

		for (entry = *render_target_bucket(level_data); entry; entry = entry->next_in_bucket)
			if (entry->target.data == level_data && entry->target.width == width && entry->target.height == height &&
				!entry->target.depth && entry->version == version)
				break;
		if (!entry)
		{
			entry = calloc(1, sizeof(*entry));
			entry->version = version;
			entry->target.data = level_data;
			entry->target.width = width;
			entry->target.height = height;
			entry->target.scale[0] = entry->target.scale[1] = 1.0f;
			entry->target.gl_width = width;
			entry->target.gl_height = height;
			entry->next = render_targets;
			render_targets = entry;
			entry->next_in_bucket = *render_target_bucket(level_data);
			*render_target_bucket(level_data) = entry;
		}
		entry->id = ids[level];
		entry->chain_levels = level ? -1 : (long)levels;
	}
	base->texture = texture;
	base->chain_levels = (long)levels;
}

struct xgpu_render_target *xgpu_render_target_find(unsigned long data)
{
	struct render_target_entry *entry = render_target_entry_find(data);

	return entry ? &entry->target : NULL;
}

/* points the renderer at the current targets; FALSE when there are none */
static BOOL bind_targets(BOOL *has_depth) __attribute__((unused));
static BOOL bind_targets(BOOL *has_depth)
{
	struct render_target_entry *color = render_target_get(device.render_target);
	struct render_target_entry *depth = render_target_get(device.depth_stencil);

	if (depth && !depth->target.depth)
		depth = NULL;
	if (color && color->target.depth)
		color = NULL;
	if (!color && !depth)
		return FALSE;
	if (color)
		color->last_rendered = color->last_used = device.frame + 1;
	if (depth)
		depth->last_used = device.frame + 1;
	vgxm_set_targets(color ? color->id : 0, depth ? depth->id : 0);
	*has_depth = depth != NULL;
	return TRUE;
}

/* ---------- vertex constants and the viewport */

/* debug.gpu_stats: the constant ranges the game writes, and how often they
change a value (a write of the same values costs no snapshot), for the
uniform buffer layout */
static struct { unsigned long first, count, writes, changes; } constant_writes[64];
static unsigned long constant_write_kinds;
/* debug.gpu_stats, read once a frame (at Present): a settings lookup takes
a lock and walks the settings by name, and constants_store asked for it on
every constant write when the statistics were off - a thousand and more
lookups a frame */
static int gpu_stats_on = -1;

static int gpu_stats_enabled(void)
{
	if (gpu_stats_on < 0)
		gpu_stats_on = config_boolean("debug.gpu_stats");
	return gpu_stats_on;
}

static void constant_write_note(unsigned long first, unsigned long count, int changed)
{
	/* (the kind written last is tried first: a model part writes the same
	two or three ranges, part after part) */
	static unsigned long last;
	unsigned long index = last;

	if (index >= constant_write_kinds || constant_writes[index].first != first || constant_writes[index].count != count)
	{
		for (index = 0; index < constant_write_kinds; index++)
			if (constant_writes[index].first == first && constant_writes[index].count == count)
				break;
	}
	if (index == constant_write_kinds)
	{
		if (constant_write_kinds >= sizeof(constant_writes) / sizeof(constant_writes[0]))
			return;
		constant_writes[constant_write_kinds].first = first;
		constant_writes[constant_write_kinds].count = count;
		constant_writes[constant_write_kinds].writes = 0;
		constant_writes[constant_write_kinds].changes = 0;
		constant_write_kinds++;
	}
	last = index;
	constant_writes[index].writes++;
	if (changed)
		constant_writes[index].changes++;
}

static const unsigned long chunk_first[VITA_VC_CHUNKS] = VITA_VC_FIRST;
static const unsigned long chunk_end[VITA_VC_CHUNKS] = VITA_VC_END;

/* bumped by every vertex constant write that changes a value: an
immediate draw is merged into the one before only while it stands */
static unsigned long constant_generation;

static void constants_store(unsigned long first, const void *data, unsigned long count)
{
	int changed = memcmp(&CONSTANT(first), data, count * sizeof(CONSTANT(0))) != 0;

	if (changed)
	{
		int chunk;

		constant_generation++;
		memcpy(&CONSTANT(first), data, count * sizeof(CONSTANT(0)));
		for (chunk = 0; chunk < VITA_VC_CHUNKS; chunk++)
			if (first < chunk_end[chunk] && first + count > chunk_first[chunk])
				device.chunk_snapshot[chunk] = NULL;
	}
	/* (a write into D, changed or not, says how far the node matrices go) */
	if (first + count > VITA_VC_D_FIRST && first < VITA_VC_D_FIRST + VITA_VC_D_COUNT)
	{
		unsigned long extent = first + count - VITA_VC_D_FIRST;

		if (extent > VITA_VC_D_COUNT)
			extent = VITA_VC_D_COUNT;
		if (extent > device.d_extent_frame)
			device.d_extent_frame = extent;
		if (first == VITA_VC_D_FIRST)
			device.d_last_object_extent = extent;
	}
	if (gpu_stats_enabled())
		constant_write_note(first, count, changed);
}

static void viewport_update_constants(void)
{
	float zscale = 16777215.0f;

	if (device.depth_stencil)
	{
		struct xgpu_texture_description description;

		xgpu_texture_describe(device.depth_stencil->Format, device.depth_stencil->Size, &description);
		if (description.format == D3DFMT_D16 || description.format == D3DFMT_LIN_D16 ||
			description.format == D3DFMT_F16 || description.format == D3DFMT_LIN_F16)
		{
			zscale = 65535.0f;
		}
	}
	device.viewport_scale[0] = device.viewport.Width * 0.5f;
	device.viewport_scale[1] = -(float)device.viewport.Height * 0.5f;
	device.viewport_scale[2] = zscale * (device.viewport.MaxZ - device.viewport.MinZ);
	device.viewport_scale[3] = 0.0f;
	device.viewport_offset[0] = device.viewport.X + device.viewport.Width * 0.5f;
	device.viewport_offset[1] = device.viewport.Y + device.viewport.Height * 0.5f;
	device.viewport_offset[2] = zscale * device.viewport.MinZ;
	device.viewport_offset[3] = 0.0f;
	memcpy(device.vertex_uniforms[VITA_VM_VIEWPORT_SCALE], device.viewport_scale, sizeof(device.viewport_scale));
	memcpy(device.vertex_uniforms[VITA_VM_VIEWPORT_OFFSET], device.viewport_offset, sizeof(device.viewport_offset));
	device.vertex_uniform_snapshot = NULL;
	if (!(device.shader_constant_mode & D3DSCM_NORESERVEDCONSTANTS))
	{
		constants_store(XGPU_VERTEX_CONSTANT_BIAS - 38, device.viewport_scale, 1);
		constants_store(XGPU_VERTEX_CONSTANT_BIAS - 37, device.viewport_offset, 1);
	}
}

/* ---------- device creation */

static void gpu_initialize(void)
{
	unsigned long index;

	device.sequential_indices = vgxm_pool_alloc(65536 * sizeof(unsigned short), 16);
	if (!device.sequential_indices)
	{
		platform_log("Direct3D: no GPU memory for the sequential indices");
		return;
	}
	for (index = 0; index < 65536; index++)
		device.sequential_indices[index] = (unsigned short)index;
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		device.vertex_uniforms[VITA_VM_ATTRIBUTES + index][3] = 1.0f;
	memory_watch_initialize();
	device.gpu_ready = TRUE;
}

Direct3D *WINAPI Direct3DCreate8(UINT sdk_version)
{
	(void)sdk_version;
	return (Direct3D *)1;
}

void WINAPI Direct3D_SetPushBufferSize(DWORD push_buffer_size, DWORD segment_count)
{
	(void)push_buffer_size;
	(void)segment_count;
}

HRESULT WINAPI Direct3D_CreateDevice(UINT adapter, D3DDEVTYPE device_type, void *unused, DWORD behavior_flags,
	D3DPRESENT_PARAMETERS *presentation_parameters, D3DDevice **returned_device)
{
	unsigned long width, height;
	int index;

	(void)adapter;
	(void)device_type;
	(void)unused;
	(void)behavior_flags;
	if (!device.created)
	{
		memset(&device, 0, sizeof(device));
		if (presentation_parameters)
			device.presentation = *presentation_parameters;
		width = device.presentation.BackBufferWidth ? device.presentation.BackBufferWidth : 640;
		height = device.presentation.BackBufferHeight ? device.presentation.BackBufferHeight : 480;
		d3d8_surface_initialize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, width, height);
		d3d8_surface_initialize(&device.depth_buffer, D3DFMT_LIN_D24S8, width, height);
		device.render_target = &device.back_buffer;
		device.depth_stencil = &device.depth_buffer;
		for (index = 0; index < D3DTS_MAX; index++)
		{
			device.transforms[index]._11 = 1.0f;
			device.transforms[index]._22 = 1.0f;
			device.transforms[index]._33 = 1.0f;
			device.transforms[index]._44 = 1.0f;
		}
		device.viewport.Width = width;
		device.viewport.Height = height;
		device.viewport.MaxZ = 1.0f;
		device.next_vertex_shader_id = 1;
		D3D__RenderState[D3DRS_ZENABLE] = TRUE;
		D3D__RenderState[D3DRS_ZWRITEENABLE] = TRUE;
		D3D__RenderState[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
		D3D__RenderState[D3DRS_COLORWRITEENABLE] = D3DCOLORWRITEENABLE_ALL;
		D3D__RenderState[D3DRS_SRCBLEND] = D3DBLEND_ONE;
		D3D__RenderState[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
		D3D__RenderState[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
		D3D__RenderState[D3DRS_CULLMODE] = D3DCULL_CCW;
		D3D__RenderState[D3DRS_FRONTFACE] = D3DFRONT_CW;
		D3D__RenderState[D3DRS_FILLMODE] = D3DFILL_SOLID;
		D3D__RenderState[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
		D3D__RenderState[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
		D3D__RenderState[D3DRS_STENCILMASK] = 0xff;
		D3D__RenderState[D3DRS_STENCILWRITEMASK] = 0xff;
		D3D__RenderState[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
		D3D__RenderState[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
		D3D__RenderState[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
		for (index = 0; index < D3DTSS_MAXSTAGES; index++)
		{
			D3D__TextureState[index][D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_ADDRESSW] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_MAGFILTER] = D3DTEXF_POINT;
			D3D__TextureState[index][D3DTSS_MINFILTER] = D3DTEXF_POINT;
			D3D__TextureState[index][D3DTSS_MAXANISOTROPY] = 1;
		}
		viewport_update_constants();

		if (!config_boolean("debug.null_renderer") && platform_video_initialize(width, height))
			gpu_initialize();
		else
			platform_log("Direct3D: running without the GPU (nothing is displayed)");
		device.created = TRUE;
	}
	*returned_device = device_pointer();
	return S_OK;
}

ULONG WINAPI D3DDevice_Release(void)
{
	return 1;
}

void WINAPI D3DDevice_GetDeviceCaps(D3DCAPS8 *caps)
{
	memset(caps, 0, sizeof(*caps));
	caps->DeviceType = D3DDEVTYPE_HAL;
	caps->MaxTextureWidth = 4096;
	caps->MaxTextureHeight = 4096;
	caps->MaxVolumeExtent = 512;
	caps->MaxTextureRepeat = 8192;
	caps->MaxTextureAspectRatio = 4096;
	caps->MaxAnisotropy = 4;
	caps->MaxTextureBlendStages = 4;
	caps->MaxSimultaneousTextures = 4;
	caps->MaxActiveLights = 8;
	caps->MaxVertexBlendMatrices = 4;
	caps->MaxPointSize = 64.0f;
	caps->MaxPrimitiveCount = 0xfffff;
	caps->MaxVertexIndex = 0xffff;
	caps->MaxStreams = 16;
	caps->MaxStreamStride = 255;
	caps->VertexShaderVersion = D3DVS_VERSION(1, 1);
	caps->MaxVertexShaderConst = 192;
	caps->PixelShaderVersion = D3DPS_VERSION(1, 1);
	caps->MaxPixelShaderValue = 1.0f;
}

void WINAPI D3DDevice_GetBackBuffer(INT back_buffer, D3DBACKBUFFER_TYPE type, D3DSurface **result)
{
	(void)back_buffer;
	(void)type;
	device.back_buffer.Common++;
	*result = &device.back_buffer;
}

HRESULT WINAPI D3DDevice_GetDepthStencilSurface(D3DSurface **result)
{
	*result = device.depth_stencil;
	if (!*result)
		return D3DERR_NOTFOUND;
	(*result)->Common++;
	return S_OK;
}

void WINAPI D3DDevice_SetRenderTarget(D3DSurface *render_target, D3DSurface *depth_stencil)
{
	if (trace_frame())
		platform_log("set render target %08lx depth %08lx", render_target ? (unsigned long)render_target->Data : 0,
			depth_stencil ? (unsigned long)depth_stencil->Data : 0);
	stats.target_changes++;
	if (render_target)
		device.render_target = render_target;
	device.depth_stencil = depth_stencil;
	if (device.render_target)
	{
		unsigned long width, height;
		BOOL depth;

		surface_dimensions(device.render_target, &width, &height, &depth);
		device.viewport.X = 0;
		device.viewport.Y = 0;
		device.viewport.Width = width;
		device.viewport.Height = height;
		device.viewport.MinZ = 0.0f;
		device.viewport.MaxZ = 1.0f;
	}
	viewport_update_constants();
}

void WINAPI D3DDevice_SetViewport(CONST D3DVIEWPORT8 *viewport)
{
	device.viewport = *viewport;
	viewport_update_constants();
}

void WINAPI D3DDevice_SetTransform(D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		device.transforms[state] = *matrix;
}

void WINAPI D3DDevice_GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		*matrix = device.transforms[state];
}

void WINAPI D3DDevice_SetFlickerFilter(DWORD filter) { (void)filter; }
void WINAPI D3DDevice_SetSoftDisplayFilter(BOOL enable) { (void)enable; }

void WINAPI D3DDevice_SetShaderConstantMode(D3DSHADERCONSTANTMODE mode)
{
	device.shader_constant_mode = mode;
	viewport_update_constants();
}

/* ---------- GPU synchronisation: the renderer keeps its own ordering */

BOOL WINAPI D3DDevice_IsBusy(void)
{
	return FALSE;
}

void WINAPI D3DDevice_KickPushBuffer(void)
{
}

void WINAPI D3DDevice_InsertCallback(D3DCALLBACKTYPE type, D3DCALLBACK callback, DWORD context)
{
	(void)type;
	if (callback)
		callback(context);
}

/* ---------- visibility (occlusion) tests: the draws inside one count the
samples that pass, which the renderer reads back once the GPU has them */

/* The game names a test when it ends it (EndVisibilityTest(index)), after
its draws were recorded, so each test is given the next slot of the frame
when it begins, its draws count into that slot, and the index is mapped to
the slot at the end. The lens flares (rasterizer_lights.c) test once a
frame each, in the same order from frame to frame, and read the results at
the start of the next frame; the GPU then still has that frame ahead of
it, so a result is the latest one the GPU has finished for that slot (one
to three frames old), as on the desktop's GL device, rather than a wait for
the GPU. This used to be a stub that read 0 for every test: no lens flare
was ever drawn on the Vita - not the lights' coronas, nor a10's calibration
lights, which the tutorial script turns from red to green. */

void WINAPI D3DDevice_BeginVisibilityTest(void)
{
	if (device.visibility_test_active)
		return;
	device.visibility_test_active = TRUE;
	/* (past the buffer's slots a test counts nothing: slot 0) */
	device.visibility_index = device.visibility_tests_this_frame < VGXM_VISIBILITY_SLOTS - 1 ?
		++device.visibility_tests_this_frame : 0;
}

HRESULT WINAPI D3DDevice_EndVisibilityTest(DWORD index)
{
	if (!device.visibility_test_active)
		return S_OK;
	device.visibility_test_active = FALSE;
	device.visibility_slot_of_test[index % VISIBILITY_TEST_SLOTS] = (unsigned short)device.visibility_index;
	return S_OK;
}

HRESULT WINAPI D3DDevice_GetVisibilityTestResult(DWORD index, UINT *result, ULONGLONG *time_stamp)
{
	unsigned long slot = device.visibility_slot_of_test[index % VISIBILITY_TEST_SLOTS];

	if (time_stamp)
		*time_stamp = 0;
	if (result)
		*result = device.gpu_ready && slot ? (UINT)vgxm_visibility_result(slot) : 0;
	return S_OK;
}

/* ---------- render and texture stage state */

/* The setters mark the device state dirty only when they change a value:
the game sets most of a model part's states anew for every part (the cull
mode, the blend, the pixel shader's constants), mostly to what they were,
and a dirty state makes the next draw compare the whole state (2 KB) with
the last record's. */

/* the simple render state a push buffer method sets (the inverse of
D3DSIMPLERENDERSTATEENCODE: methods 0x40000 + 4 * n, n below 0x800), +1, or
0 when the method is no simple state */
static unsigned char simple_state_of_method[0x800];

void D3DFASTCALL D3DDevice_SetRenderState_Simple(DWORD method, DWORD value)
{
	/* (the inline D3DDevice_SetRenderState stores the value after this
	call: what the table holds is still the old value) */
	unsigned long slot = (method - 0x40000UL) >> 2;

	if (!simple_state_of_method[(D3DSIMPLERENDERSTATEENCODE[0] - 0x40000UL) >> 2])
	{
		unsigned long state;

		for (state = 0; state < D3DRS_SIMPLE_MAX; state++)
			simple_state_of_method[(D3DSIMPLERENDERSTATEENCODE[state] - 0x40000UL) >> 2] = (unsigned char)(state + 1);
	}
	if ((method & 3) || slot >= sizeof(simple_state_of_method) || !simple_state_of_method[slot])
		device_state_dirty = STATE_DIRTY_MATERIAL | STATE_DIRTY_VALUES;
	else if (D3D__RenderState[simple_state_of_method[slot] - 1] != value)
		device_state_dirty |= render_state_dirty_bit(simple_state_of_method[slot] - 1);
}

void D3DFASTCALL D3DDevice_SetRenderState_Deferred(D3DRENDERSTATETYPE state, DWORD value)
{
	if ((unsigned long)state < D3DRS_MAX)
	{
		if (D3D__RenderState[state] != value)
			device_state_dirty |= render_state_dirty_bit(state);
		D3D__RenderState[state] = value;
	}
}

void WINAPI D3DDevice_SetRenderState_ZBias(DWORD value);

void WINAPI D3DDevice_SetRenderStateNotInline(D3DRENDERSTATETYPE state, DWORD value)
{
	if (state == D3DRS_ZBIAS)
		D3DDevice_SetRenderState_ZBias(value);
	else if ((unsigned long)state < D3DRS_MAX)
	{
		if (D3D__RenderState[state] != value)
			device_state_dirty |= render_state_dirty_bit(state);
		D3D__RenderState[state] = value;
	}
}

/* stores a render state, marking the state dirty if it changes */
static void render_state_store(unsigned long state, DWORD value)
{
	if (D3D__RenderState[state] != value)
	{
		device_state_dirty |= render_state_dirty_bit(state);
		D3D__RenderState[state] = value;
	}
}

void WINAPI D3DDevice_SetRenderState_ZBias(DWORD value)
{
	float offset = -(float)value;
	float slope = offset * 0.25f;
	DWORD enable = value != 0;
	DWORD slope_bits, offset_bits;

	memcpy(&slope_bits, &slope, sizeof(slope));
	memcpy(&offset_bits, &offset, sizeof(offset));
	render_state_store(D3DRS_POLYGONOFFSETZSLOPESCALE, slope_bits);
	render_state_store(D3DRS_POLYGONOFFSETZOFFSET, offset_bits);
	render_state_store(D3DRS_POINTOFFSETENABLE, enable);
	render_state_store(D3DRS_WIREFRAMEOFFSETENABLE, enable);
	render_state_store(D3DRS_SOLIDOFFSETENABLE, enable);
	render_state_store(D3DRS_ZBIAS, value);
}

#define COMPLEX_RENDER_STATE(name, state) \
	void WINAPI D3DDevice_SetRenderState_##name(DWORD value) { render_state_store(state, value); }

COMPLEX_RENDER_STATE(PSTextureModes, D3DRS_PSTEXTUREMODES)
COMPLEX_RENDER_STATE(VertexBlend, D3DRS_VERTEXBLEND)
COMPLEX_RENDER_STATE(FogColor, D3DRS_FOGCOLOR)
COMPLEX_RENDER_STATE(FillMode, D3DRS_FILLMODE)
COMPLEX_RENDER_STATE(BackFillMode, D3DRS_BACKFILLMODE)
COMPLEX_RENDER_STATE(TwoSidedLighting, D3DRS_TWOSIDEDLIGHTING)
COMPLEX_RENDER_STATE(NormalizeNormals, D3DRS_NORMALIZENORMALS)
COMPLEX_RENDER_STATE(ZEnable, D3DRS_ZENABLE)
COMPLEX_RENDER_STATE(StencilEnable, D3DRS_STENCILENABLE)
COMPLEX_RENDER_STATE(StencilFail, D3DRS_STENCILFAIL)
COMPLEX_RENDER_STATE(FrontFace, D3DRS_FRONTFACE)
COMPLEX_RENDER_STATE(CullMode, D3DRS_CULLMODE)
COMPLEX_RENDER_STATE(TextureFactor, D3DRS_TEXTUREFACTOR)
COMPLEX_RENDER_STATE(LogicOp, D3DRS_LOGICOP)
COMPLEX_RENDER_STATE(EdgeAntiAlias, D3DRS_EDGEANTIALIAS)
COMPLEX_RENDER_STATE(MultiSampleAntiAlias, D3DRS_MULTISAMPLEANTIALIAS)
COMPLEX_RENDER_STATE(MultiSampleMask, D3DRS_MULTISAMPLEMASK)
COMPLEX_RENDER_STATE(MultiSampleType, D3DRS_MULTISAMPLETYPE)
COMPLEX_RENDER_STATE(ShadowFunc, D3DRS_SHADOWFUNC)
COMPLEX_RENDER_STATE(LineWidth, D3DRS_LINEWIDTH)
COMPLEX_RENDER_STATE(Dxt1NoiseEnable, D3DRS_DXT1NOISEENABLE)
COMPLEX_RENDER_STATE(YuvEnable, D3DRS_YUVENABLE)
COMPLEX_RENDER_STATE(OcclusionCullEnable, D3DRS_OCCLUSIONCULLENABLE)
COMPLEX_RENDER_STATE(StencilCullEnable, D3DRS_STENCILCULLENABLE)
COMPLEX_RENDER_STATE(RopZCmpAlwaysRead, D3DRS_ROPZCMPALWAYSREAD)
COMPLEX_RENDER_STATE(RopZRead, D3DRS_ROPZREAD)
COMPLEX_RENDER_STATE(DoNotCullUncompressed, D3DRS_DONOTCULLUNCOMPRESSED)

/* stores a texture stage state, marking the state dirty if it changes */
static void texture_state_store(DWORD stage, unsigned long type, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES && type < D3DTSS_MAX && D3D__TextureState[stage][type] != value)
	{
		device_state_dirty |= texture_state_dirty_bit(type);
		D3D__TextureState[stage][type] = value;
	}
}

void D3DFASTCALL D3DDevice_SetTextureState_Deferred(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	texture_state_store(stage, (unsigned long)type, value);
}

void WINAPI D3DDevice_SetTextureState_TexCoordIndex(DWORD stage, DWORD value)
{
	texture_state_store(stage, D3DTSS_TEXCOORDINDEX, value);
}

void WINAPI D3DDevice_SetTextureState_BorderColor(DWORD stage, DWORD value)
{
	texture_state_store(stage, D3DTSS_BORDERCOLOR, value);
}

void WINAPI D3DDevice_SetTextureState_ColorKeyColor(DWORD stage, DWORD value)
{
	texture_state_store(stage, D3DTSS_COLORKEYCOLOR, value);
}

void WINAPI D3DDevice_SetTextureState_BumpEnv(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	texture_state_store(stage, (unsigned long)type, value);
}

void WINAPI D3DDevice_SetTexture(DWORD stage, D3DBaseTexture *texture)
{
	/* (no dirty mark: the textures and their headers are compared at each
	draw, so a texture whose header the game rewrites in place is seen) */
	if (stage < D3DTSS_MAXSTAGES)
		device.textures[stage] = texture;
}

void WINAPI D3DDevice_SetPalette(DWORD stage, D3DPalette *palette)
{
	/* (compared at each draw, as the textures) */
	if (stage < D3DTSS_MAXSTAGES)
		device.palettes[stage] = palette;
}

void WINAPI D3DDevice_SetPixelShaderProgram(D3DPIXELSHADERDEF *definition)
{
	/* (a definition the same as what the states hold, field by field, is
	no change: the dirty flag is kept as it is) */
	if (!definition)
		return;
	if (memcmp(&D3D__RenderState[D3DRS_PSALPHAINPUTS0], definition->PSAlphaInputs, sizeof(definition->PSAlphaInputs)) ||
		D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSABCD] != definition->PSFinalCombinerInputsABCD ||
		D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSEFG] != definition->PSFinalCombinerInputsEFG ||
		memcmp(&D3D__RenderState[D3DRS_PSCONSTANT0_0], definition->PSConstant0, sizeof(definition->PSConstant0)) ||
		memcmp(&D3D__RenderState[D3DRS_PSCONSTANT1_0], definition->PSConstant1, sizeof(definition->PSConstant1)) ||
		memcmp(&D3D__RenderState[D3DRS_PSALPHAOUTPUTS0], definition->PSAlphaOutputs, sizeof(definition->PSAlphaOutputs)) ||
		memcmp(&D3D__RenderState[D3DRS_PSRGBINPUTS0], definition->PSRGBInputs, sizeof(definition->PSRGBInputs)) ||
		D3D__RenderState[D3DRS_PSCOMPAREMODE] != definition->PSCompareMode ||
		D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0] != definition->PSFinalCombinerConstant0 ||
		D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1] != definition->PSFinalCombinerConstant1 ||
		memcmp(&D3D__RenderState[D3DRS_PSRGBOUTPUTS0], definition->PSRGBOutputs, sizeof(definition->PSRGBOutputs)) ||
		D3D__RenderState[D3DRS_PSCOMBINERCOUNT] != definition->PSCombinerCount ||
		D3D__RenderState[D3DRS_PSTEXTUREMODES] != definition->PSTextureModes ||
		D3D__RenderState[D3DRS_PSDOTMAPPING] != definition->PSDotMapping ||
		D3D__RenderState[D3DRS_PSINPUTTEXTURE] != definition->PSInputTexture)
	{
		device_state_dirty = STATE_DIRTY_MATERIAL | STATE_DIRTY_VALUES;
	}
	else
		return;
	memcpy(&D3D__RenderState[D3DRS_PSALPHAINPUTS0], definition->PSAlphaInputs, sizeof(definition->PSAlphaInputs));
	D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSABCD] = definition->PSFinalCombinerInputsABCD;
	D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSEFG] = definition->PSFinalCombinerInputsEFG;
	memcpy(&D3D__RenderState[D3DRS_PSCONSTANT0_0], definition->PSConstant0, sizeof(definition->PSConstant0));
	memcpy(&D3D__RenderState[D3DRS_PSCONSTANT1_0], definition->PSConstant1, sizeof(definition->PSConstant1));
	memcpy(&D3D__RenderState[D3DRS_PSALPHAOUTPUTS0], definition->PSAlphaOutputs, sizeof(definition->PSAlphaOutputs));
	memcpy(&D3D__RenderState[D3DRS_PSRGBINPUTS0], definition->PSRGBInputs, sizeof(definition->PSRGBInputs));
	D3D__RenderState[D3DRS_PSCOMPAREMODE] = definition->PSCompareMode;
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0] = definition->PSFinalCombinerConstant0;
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1] = definition->PSFinalCombinerConstant1;
	memcpy(&D3D__RenderState[D3DRS_PSRGBOUTPUTS0], definition->PSRGBOutputs, sizeof(definition->PSRGBOutputs));
	D3D__RenderState[D3DRS_PSCOMBINERCOUNT] = definition->PSCombinerCount;
	D3D__RenderState[D3DRS_PSTEXTUREMODES] = definition->PSTextureModes;
	D3D__RenderState[D3DRS_PSDOTMAPPING] = definition->PSDotMapping;
	D3D__RenderState[D3DRS_PSINPUTTEXTURE] = definition->PSInputTexture;
}

/* ---------- vertex shaders */

static unsigned long vertex_type_bytes(unsigned long type)
{
	switch (type)
	{
	case D3DVSDT_FLOAT1: return 4;
	case D3DVSDT_FLOAT2: return 8;
	case D3DVSDT_FLOAT3: return 12;
	case D3DVSDT_FLOAT4: return 16;
	case D3DVSDT_D3DCOLOR: return 4;
	case D3DVSDT_SHORT1: return 2;
	case D3DVSDT_SHORT2: return 4;
	case D3DVSDT_SHORT3: return 6;
	case D3DVSDT_SHORT4: return 8;
	case D3DVSDT_NORMSHORT1: return 2;
	case D3DVSDT_NORMSHORT2: return 4;
	case D3DVSDT_NORMSHORT3: return 6;
	case D3DVSDT_NORMSHORT4: return 8;
	case D3DVSDT_NORMPACKED3: return 4;
	case D3DVSDT_PBYTE1: return 1;
	case D3DVSDT_PBYTE2: return 2;
	case D3DVSDT_PBYTE3: return 3;
	case D3DVSDT_PBYTE4: return 4;
	case D3DVSDT_FLOAT2H: return 12;
	default: return 0;
	}
}

static void parse_declaration(struct vertex_shader_object *object, const DWORD *declaration)
{
	unsigned long stream = 0;
	unsigned long offsets[16] = { 0 };

	for (; declaration && *declaration != D3DVSD_END(); declaration++)
	{
		DWORD token = *declaration;
		unsigned long token_type = (token & D3DVSD_TOKENTYPEMASK) >> D3DVSD_TOKENTYPESHIFT;

		switch (token_type)
		{
		case D3DVSD_TOKEN_STREAM:
			stream = token & D3DVSD_STREAMNUMBERMASK;
			break;
		case D3DVSD_TOKEN_STREAMDATA:
			if (token & D3DVSD_DATALOADTYPEMASK)
			{
				unsigned long count = (token & D3DVSD_SKIPCOUNTMASK) >> D3DVSD_SKIPCOUNTSHIFT;

				offsets[stream] += (token & 0x08000000) ? count : count * 4;
			}
			else if (object->element_count < XGPU_VERTEX_ATTRIBUTE_COUNT)
			{
				struct vertex_element *element = &object->elements[object->element_count++];

				element->reg = (unsigned char)(token & D3DVSD_VERTEXREGMASK);
				element->stream = (unsigned char)stream;
				element->type = (unsigned char)((token & D3DVSD_DATATYPEMASK) >> D3DVSD_DATATYPESHIFT);
				element->bytes = (unsigned char)vertex_type_bytes(element->type);
				element->offset = (unsigned short)offsets[stream];
				offsets[stream] += element->bytes;
				if (element->type == D3DVSDT_NONE)
					continue;
				object->provided_mask |= 1UL << element->reg;
				if (element->type == D3DVSDT_NORMPACKED3)
					object->packed_mask |= 1UL << element->reg;
				if (element->type == D3DVSDT_D3DCOLOR)
					object->color_mask |= 1UL << element->reg;
			}
			break;
		case D3DVSD_TOKEN_CONSTMEM:
			declaration += ((token & D3DVSD_CONSTCOUNTMASK) >> D3DVSD_CONSTCOUNTSHIFT) * 4;
			break;
		case D3DVSD_TOKEN_EXT:
			declaration += (token & D3DVSD_EXTCOUNTMASK) >> D3DVSD_EXTCOUNTSHIFT;
			break;
		default:
			break;
		}
	}
}

HRESULT WINAPI D3DDevice_CreateVertexShader(CONST DWORD *declaration, CONST DWORD *function, DWORD *handle, DWORD usage)
{
	struct vertex_shader_object *object = calloc(1, sizeof(*object));

	(void)usage;
	if (!object)
		return E_OUTOFMEMORY;
	object->signature = VERTEX_SHADER_SIGNATURE;
	object->id = device.next_vertex_shader_id++;
	if (function)
	{
		object->instruction_count = function[0] >> 16;
		object->instructions = malloc(object->instruction_count * 4 * sizeof(DWORD));
		memcpy(object->instructions, function + 1, object->instruction_count * 4 * sizeof(DWORD));
	}
	parse_declaration(object, declaration);
	nv2a_vertex_shader_constant_usage(object->instructions, object->instruction_count, &object->usage);
	object->texcoord_w_mask = nv2a_vertex_shader_texcoord_w_mask(object->instructions, object->instruction_count);
	object->input_mask = nv2a_vertex_shader_input_mask(object->instructions, object->instruction_count);
	if (object->usage.relative_lowest < XGPU_VERTEX_CONSTANT_COUNT && object->usage.relative_lowest < VITA_VC_D_FIRST &&
		(object->usage.relative_lowest < chunk_first[VITA_VC_C1] || object->usage.relative_lowest >= chunk_end[VITA_VC_C1]))
	{
		/* (a relative read stays in its base's chunk: only the node
		matrices and the point lights are known to be indexed this way) */
		platform_log("vertex shader %lu reads constants relative to a0 from %lu, neither the node matrices nor the lights: clamped to that chunk",
			object->id, object->usage.relative_lowest);
	}
	if (config_boolean("debug.gpu_stats"))
	{
		unsigned long index, streams = 0;

		for (index = 0; index < object->element_count; index++)
			if (object->elements[index].type != D3DVSDT_NONE && (unsigned long)object->elements[index].stream + 1 > streams)
				streams = object->elements[index].stream + 1;
		platform_log("vertex shader %lu: %lu instructions, constants %lu..%lu, chunks %02lx%s (D absolute to %lu), %lu elements in %lu streams",
			object->id, object->instruction_count, object->usage.lowest, object->usage.highest, object->usage.chunk_mask,
			object->usage.relative ? " + relative reads" : "", object->usage.d_absolute_end, object->element_count, streams);
	}
	*handle = (DWORD)object;
	return S_OK;
}

static struct vertex_shader_object *vertex_shader_from_handle(DWORD handle)
{
	struct vertex_shader_object *object = (struct vertex_shader_object *)handle;

	if (!handle || (handle & 1) || object->signature != VERTEX_SHADER_SIGNATURE)
		return NULL;
	return object;
}

void WINAPI D3DDevice_DeleteVertexShader(DWORD handle)
{
	(void)handle;
}

void WINAPI D3DDevice_SetVertexShader(DWORD handle)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	if (object)
	{
		device.vertex_shader = object;
		device.program_address = 0;
		device.program_slots[0] = object;
	}
}

void WINAPI D3DDevice_LoadVertexShader(DWORD handle, DWORD address)
{
	if (address < VERTEX_PROGRAM_SLOTS)
		device.program_slots[address] = vertex_shader_from_handle(handle);
}

void WINAPI D3DDevice_SelectVertexShader(DWORD handle, DWORD address)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	if (object)
		device.vertex_shader = object;
	if (address < VERTEX_PROGRAM_SLOTS)
		device.program_address = address;
}

void WINAPI D3DDevice_GetVertexShaderSize(DWORD handle, UINT *size)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	*size = object ? object->instruction_count : 0;
}

void WINAPI D3DDevice_SetVertexShaderConstant(INT reg, CONST void *constant_data, DWORD constant_count)
{
	long first = reg + XGPU_VERTEX_CONSTANT_BIAS;

	if (first < 0 || first >= XGPU_VERTEX_CONSTANT_COUNT)
		return;
	if (first + (long)constant_count > XGPU_VERTEX_CONSTANT_COUNT)
		constant_count = XGPU_VERTEX_CONSTANT_COUNT - first;
	constants_store((unsigned long)first, constant_data, constant_count);
}

static struct vertex_shader_object *current_program(void)
{
	struct vertex_shader_object *program = device.program_slots[device.program_address];

	return program ? program : device.vertex_shader;
}

/* ---------- programs */

static int debug_settings_dump(void)
{
	static int dump = -1;

	if (dump < 0)
		dump = *config_string("debug.gpu_dump_shaders") != 0;
	return dump;
}

static unsigned long hash_words(const void *data, unsigned long size)
{
	const DWORD *words = data;
	unsigned long hash = 2166136261UL;

	for (size /= 4; size; size--)
		hash = (hash ^ *words++) * 16777619UL;
	return hash;
}

static void dump_shader(const char *source, const char *kind, unsigned long id)
{
	const char *directory = *config_string("debug.gpu_dump_shaders") ? config_string("debug.gpu_dump_shaders") : NULL;
	char path[512];
	FILE *file;

	if (!directory)
		return;
	snprintf(path, sizeof(path), "%s/%s_%08lx.cg", directory, kind, id);
	if ((file = fopen(path, "w")) != NULL)
	{
		fputs(source, file);
		fclose(file);
	}
}

/* the program's Cg for the inputs the declaration provides */
static unsigned long vertex_shader_get(struct vertex_shader_object *program, unsigned long provided_mask,
	unsigned long packed_mask, unsigned long color_mask)
{
	struct vertex_variant *variant;
	char *source;

	for (variant = program->variants; variant; variant = variant->next)
	{
		if (variant->provided_mask == provided_mask && variant->packed_mask == packed_mask &&
			variant->color_mask == color_mask)
		{
			return variant->shader;
		}
	}
	variant = calloc(1, sizeof(*variant));
	variant->provided_mask = provided_mask;
	variant->packed_mask = packed_mask;
	variant->color_mask = color_mask;
	source = nv2a_vertex_shader_to_cg(program->instructions, program->instruction_count, provided_mask, packed_mask,
		color_mask);
	variant->shader = vgxm_shader_get(source, 0);
	if (!variant->shader || debug_settings_dump())
		dump_shader(source, "vs", hash_words(source, strlen(source) & ~3UL));
	if (!variant->shader)
		platform_log("vertex shader %lu (inputs %04lx) does not compile", program->id, provided_mask);
	free(source);
	variant->next = program->variants;
	program->variants = variant;
	return variant->shader;
}

typedef char pixel_shader_key_size_assert[sizeof(struct nv2a_pixel_shader_key) % 4 == 0 ? 1 : -1];

static unsigned long fragment_shader_get(const struct nv2a_pixel_shader_key *key)
{
	static struct fragment_entry *last;
	unsigned long hash;
	struct fragment_entry **bucket;
	struct fragment_entry *entry;
	char *source;

	if (last && !memcmp(&last->key, key, sizeof(*key)))
		return last->shader;
	hash = hash_words(key, sizeof(*key));
	bucket = &fragment_buckets[hash % FRAGMENT_BUCKETS];
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->hash == hash && !memcmp(&entry->key, key, sizeof(*key)))
		{
			last = entry;
			return entry->shader;
		}
	}
	entry = calloc(1, sizeof(*entry));
	entry->hash = hash;
	entry->key = *key;
	source = nv2a_pixel_shader_to_cg(key);
	/* (dumped before the compile: a program the compiler never returns
	from can then still be read) */
	if (debug_settings_dump())
		dump_shader(source, "ps", hash);
	entry->shader = vgxm_shader_get(source, 1);
	if (!entry->shader && !debug_settings_dump())
		dump_shader(source, "ps", hash);
	if (!entry->shader)
		platform_log("pixel shader %08lx does not compile", hash);
	free(source);
	entry->next = *bucket;
	*bucket = entry;
	last = entry;
	return entry->shader;
}

static unsigned long stage_texture_mode(int stage)
{
	return (D3D__RenderState[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f;
}

/* ---------- the render worker

The game's draws are recorded here, on its thread, with everything a draw
depends on captured or copied into the frame's ring; the worker thread
(core 1) turns each record into GPU work: the textures, the shaders and the
GXM calls. The game waits for the worker at Present, so a frame's GPU work
is done before the next frame changes anything. HALO_RENDER_THREAD=0 runs
the records inline, on the game's thread, for comparison. */

enum
{
	_command_draw,
	_command_clear,
	_command_present,
};

/* HALO_RECORD_SPLIT (default on): the device state a draw is made from,
copied whole into a per-frame block when it changes, and translated into
the shader key, the fragment uniforms and the draw states by the worker
(core 1) instead of the game's thread - the translation was most of the
record's cost, and the game's thread is the frame's wall */
struct record_material
{
	DWORD render_state[D3DRS_MAX];
	DWORD texture_state[D3DTSS_MAXSTAGES][D3DTSS_MAX];
};

/* (the render and stage states, 1.1 KB, are a block of their own that
consecutive state blocks share: a third of a frame's new blocks differed
from the last only in their textures - another object with the same
material settings) */
/* (the material leaves out the draw's values - render_state_values and the
stages' bump environment states: they are 0 there - and the state block
points to them in a block of their own, so an object with the material of
the one before but its own fog or constants, or a two-sided part's second
pass, takes the last material) */
enum
{
	RECORD_VALUE_PS_C0 = 0,     /* [8] */
	RECORD_VALUE_PS_C1 = 8,     /* [8] */
	RECORD_VALUE_FINAL_C0 = 16,
	RECORD_VALUE_FINAL_C1,
	RECORD_VALUE_FOG_START,
	RECORD_VALUE_FOG_END,
	RECORD_VALUE_FOG_DENSITY,
	RECORD_VALUE_FOG_COLOR,
	RECORD_VALUE_CULL_MODE,
	RECORD_VALUE_COUNT
};
static const unsigned char record_value_state[RECORD_VALUE_COUNT] = {
	D3DRS_PSCONSTANT0_0, D3DRS_PSCONSTANT0_1, D3DRS_PSCONSTANT0_2, D3DRS_PSCONSTANT0_3,
	D3DRS_PSCONSTANT0_4, D3DRS_PSCONSTANT0_5, D3DRS_PSCONSTANT0_6, D3DRS_PSCONSTANT0_7,
	D3DRS_PSCONSTANT1_0, D3DRS_PSCONSTANT1_1, D3DRS_PSCONSTANT1_2, D3DRS_PSCONSTANT1_3,
	D3DRS_PSCONSTANT1_4, D3DRS_PSCONSTANT1_5, D3DRS_PSCONSTANT1_6, D3DRS_PSCONSTANT1_7,
	D3DRS_PSFINALCOMBINERCONSTANT0, D3DRS_PSFINALCOMBINERCONSTANT1,
	D3DRS_FOGSTART, D3DRS_FOGEND, D3DRS_FOGDENSITY, D3DRS_FOGCOLOR, D3DRS_CULLMODE,
};
#define RECORD_VALUE_BUMP_COUNT (D3DTSS_BUMPENVLOFFSET - D3DTSS_BUMPENVMAT00 + 1)

struct record_values
{
	DWORD render_state[RECORD_VALUE_COUNT];
	/* D3DTSS_BUMPENVMAT00..D3DTSS_BUMPENVLOFFSET per stage */
	DWORD bump[D3DTSS_MAXSTAGES][RECORD_VALUE_BUMP_COUNT];
};

struct record_state
{
	const struct record_material *material;
	const struct record_values *values;
	DWORD texture_header[D3DTSS_MAXSTAGES][5];
	D3DBaseTexture *textures[D3DTSS_MAXSTAGES];
	D3DPalette *palettes[D3DTSS_MAXSTAGES];
	const D3DCOLOR *palette_data[D3DTSS_MAXSTAGES];
};

struct render_command
{
	BOOL skip;
	/* (split records) the state block, the depth target's presence and
	the constant-program flag; NULL state: the record was built in full */
	const struct record_state *state;
	unsigned char has_depth;
	unsigned char simple;
	unsigned long kind;
	/* both: the targets as the game had them set (copies of the surfaces),
	and the copy of a small target this run renders into */
	D3DSurface color_surface, depth_surface;
	BOOL color_valid, depth_valid;
	unsigned long color_version;
	/* a small target drawn before the main scene: no big target is read */
	BOOL hoistable;
	/* draws */
	struct vgxm_draw draw;
	struct vertex_shader_object *program;
	unsigned long provided_mask, packed_mask, color_mask;
	BOOL immediate;
	struct nv2a_pixel_shader_key key;
	DWORD texture_header[D3DTSS_MAXSTAGES][5];
	BOOL texture_present[D3DTSS_MAXSTAGES];
	/* the copy of a small target a stage reads */
	unsigned long texture_version[D3DTSS_MAXSTAGES];
	const D3DCOLOR *palette[D3DTSS_MAXSTAGES];
	DWORD sampler_state[D3DTSS_MAXSTAGES][6];
	/* clears */
	unsigned long clear_flags, clear_color, clear_stencil;
	float clear_depth;
	long clip[4];
	/* presents */
	BOOL screenshot;
	unsigned long frame;
};

#define COMMAND_RING 6144

/* a single-producer, single-consumer ring: the game's thread advances the
head, the worker the tail; neither locks, and a thread with nothing to do
sleeps briefly rather than being signalled (a signal per draw is a system
call per draw) */
static struct render_command *commands;
static volatile unsigned long command_head, command_tail;
static volatile unsigned long frames_requested, frames_presented;
void vita_host_sleep_us(unsigned long microseconds);
static int worker_enabled = -1;
static unsigned long long worker_time, worker_kind_time[3];
static unsigned long blend_histogram[16][16];
/* the host's hang watchdog (vita_main.c) watches this */
volatile unsigned long halo_present_counter;

/* ---------- executing records (the worker's side) */

static unsigned long stage_texture_mode_of(const struct nv2a_pixel_shader_key *key, int stage)
{
	return (key->texture_modes >> (5 * stage)) & 0x1f;
}

/* the targets of a record; FALSE if there is nothing to draw into */
static BOOL bind_recorded_targets(const struct render_command *command, BOOL *has_depth)
{
	/* the entries of the last record's targets, reused while the surfaces
	are the same (hundreds of draws in a row go to the same targets; each
	lookup decoded the surface header and walked a bucket) */
	static struct
	{
		D3DSurface color, depth;
		unsigned long color_version;
		BOOL color_valid, depth_valid;
		struct render_target_entry *color_entry, *depth_entry;
	} last;
	struct render_target_entry *color, *depth;

	if (last.color_valid == command->color_valid && last.depth_valid == command->depth_valid &&
		last.color_version == command->color_version &&
		(!command->color_valid || !memcmp(&last.color, &command->color_surface, sizeof(last.color))) &&
		(!command->depth_valid || !memcmp(&last.depth, &command->depth_surface, sizeof(last.depth))))
	{
		color = last.color_entry;
		depth = last.depth_entry;
	}
	else
	{
		color = command->color_valid ? render_target_get_version(&command->color_surface, command->color_version) : NULL;
		depth = command->depth_valid ? render_target_get(&command->depth_surface) : NULL;
		if (depth && !depth->target.depth)
			depth = NULL;
		if (color && color->target.depth)
			color = NULL;
		last.color = command->color_surface;
		last.depth = command->depth_surface;
		last.color_version = command->color_version;
		last.color_valid = command->color_valid;
		last.depth_valid = command->depth_valid;
		last.color_entry = color;
		last.depth_entry = depth;
	}
	if (!color && !depth)
		return FALSE;
	if (color)
		color->last_rendered = color->last_used = device.frame + 1;
	if (depth)
		depth->last_used = device.frame + 1;
	vgxm_set_targets(color ? color->id : 0, depth ? depth->id : 0);
	*has_depth = depth != NULL;
	return TRUE;
}

/* the stages' textures, with their sampler state, and the texture scale the
fragment uniforms need */
static void bind_recorded_textures(struct render_command *command, float texture_scale[4][4])
{
	/* per stage: the texture with its sampler state applied, reused while
	the same */
	static struct vgxm_texture sampled[D3DTSS_MAXSTAGES];
	static struct
	{
		const struct vgxm_texture *source;
		unsigned long control[4];
		DWORD state[6];
	} sampled_key[D3DTSS_MAXSTAGES];
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		const DWORD *header = command->texture_header[stage];
		unsigned long mode = stage_texture_mode_of(&command->key, stage);
		const struct vgxm_texture *source = NULL;
		struct xgpu_texture_description description;
		struct render_target_entry *target;
		DWORD *state = command->sampler_state[stage];

		texture_scale[stage][0] = texture_scale[stage][1] = 1.0f;
		texture_scale[stage][2] = texture_scale[stage][3] = 1.0f;
		command->draw.textures[stage] = NULL;
		command->key.sampler_type[stage] = _xgpu_sampler_none;
		if (!command->texture_present[stage] || !header[1] || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
			continue;
		target = render_target_entry_find_version(header[1], command->texture_version[stage]);
		if (target)
		{
			target->last_used = device.frame + 1;
			xgpu_texture_describe(header[3], header[4], &description);
			{
				/* (HALO_TARGET_CHAIN=0: level 0 only, as before) */
				static int chain_on = -1;

				if (chain_on < 0)
					chain_on = !getenv("HALO_TARGET_CHAIN") || atoi(getenv("HALO_TARGET_CHAIN")) != 0;
				if (chain_on && description.levels > 1 && !target->chain_levels)
					render_target_chain(target, &description, header[1], command->texture_version[stage]);
			}
			source = &target->texture;
			if (command->color_valid && target->target.data == command->color_surface.Data)
			{
				/* the draw samples the very target it draws into (the
				game's render-primary textures alias the back buffer for
				screen effects): counted, and HALO_SKIP_SELF_SAMPLED=1
				drops it, to measure what the GPU makes of it */
				static int skip_self_sampled = -1;

				if (skip_self_sampled < 0)
				{
					const char *setting = getenv("HALO_SKIP_SELF_SAMPLED");
					skip_self_sampled = setting && atoi(setting) != 0;
				}
				stats.self_sampled++;
				if (skip_self_sampled)
					command->skip = TRUE;
			}
			if (description.linear)
			{
				texture_scale[stage][0] = 1.0f / (float)target->target.width;
				texture_scale[stage][1] = 1.0f / (float)target->target.height;
			}
			/* (a chained target samples its levels, up to those the game
			declares now) */
			description.levels = target->chain_levels > 1 ? 
				(description.levels < (unsigned long)target->chain_levels ? description.levels : (unsigned long)target->chain_levels) : 1;
			description.cube_map = FALSE;
		}
		else
		{
			source = vita_texture_get(header, command->palette[stage], &description);
			{
				/* (experiment) HALO_LINEAR_SCALE_OFF=1: no 1/size scale for
				linear textures that are not render targets */
				static int scale_off = -1;

				if (scale_off < 0)
				{
					const char *setting = getenv("HALO_LINEAR_SCALE_OFF");
					scale_off = setting && atoi(setting) != 0;
				}
				if (description.linear && !scale_off)
				{
					texture_scale[stage][0] = 1.0f / (float)description.width;
					texture_scale[stage][1] = 1.0f / (float)description.height;
				}
			}
		}
		if (!source)
			continue;
		if (description.levels <= 1)
			state[2] = D3DTEXF_NONE;
		if (sampled_key[stage].source != source ||
			memcmp(sampled_key[stage].control, source->control, sizeof(source->control)) ||
			memcmp(sampled_key[stage].state, state, sizeof(sampled_key[stage].state)))
		{
			sampled[stage] = *source;
			vgxm_texture_set_sampler(&sampled[stage], state[0], state[1], state[2], state[3], state[4],
				dword_to_float(state[5]));
			sampled_key[stage].source = source;
			memcpy(sampled_key[stage].control, source->control, sizeof(source->control));
			memcpy(sampled_key[stage].state, state, sizeof(sampled_key[stage].state));
		}
		command->draw.textures[stage] = &sampled[stage];
		command->key.sampler_type[stage] = description.cube_map ? _xgpu_sampler_cube :
			description.depth > 1 ? _xgpu_sampler_3d : _xgpu_sampler_2d;
	}
}

/* HALO_DRAW_PROFILE=1: where a draw's CPU goes, on the game thread (the
record) and on the worker (the execute), in us per draw, with the frame
statistics */
static int draw_profile = -1;
static unsigned long long draw_profile_us[12];
/* the record's first segment, finer: command_begin, the key and draw clearing, the stage loop, the fragment uniform gather, the fragment uniform build */
static unsigned long long draw_fine_us[6];
#define DRAW_FINE_ADD(slot, from) do { if (draw_sampled) { unsigned long long now_ = vita_host_time_us(); draw_fine_us[slot] += now_ - (from); (from) = now_; } } while (0)
static unsigned long draw_profile_draws;

/* HALO_DRAW_PROFILE=N: one draw in N is timed (each probe is a 0.7 us
system call on the Vita: timing every draw doubled the record) */
static int draw_sampled, worker_sampled;
static unsigned long draw_counter, worker_counter, worker_profile_draws;

static int draw_profile_on(void)
{
	if (draw_profile < 0)
	{
		const char *setting = getenv("HALO_DRAW_PROFILE");
		draw_profile = setting && atoi(setting) > 0 ? atoi(setting) : 0;
	}
	return draw_profile;
}
#define DRAW_PROFILE_NOW() (draw_sampled ? vita_host_time_us() : 0)
#define DRAW_PROFILE_ADD(slot, from) do { if (((slot) >= 4 && (slot) <= 7) ? worker_sampled : draw_sampled) { unsigned long long now_ = vita_host_time_us(); draw_profile_us[slot] += now_ - (from); (from) = now_; } } while (0)

/* the worker's translation of a split record: what record_draw builds on
the game's thread from the device state, built here from the record's state
block (the same code, reading the block). The last block's results are kept
and reused while consecutive draws share it. */
static struct
{
	const struct record_state *state;
	/* the block's material: a block with the same material (and program,
	depth and simple flag) shares everything but the texture parts */
	const struct record_material *material;
	/* the values the fragment values were made from */
	const struct record_values *values;
	const struct vertex_shader_object *program;
	unsigned char has_depth, simple;
	unsigned long frame;
	/* the results, copied into the next record that shares the block */
	struct nv2a_pixel_shader_key key;
	DWORD texture_header[D3DTSS_MAXSTAGES][5];
	BOOL texture_present[D3DTSS_MAXSTAGES];
	const D3DCOLOR *palette[D3DTSS_MAXSTAGES];
	DWORD sampler_state[D3DTSS_MAXSTAGES][6];
	struct vgxm_draw draw_states;
	const void *fragment_uniforms[2];
	/* the fragment values behind the snapshots (reused across blocks
	with equal values) */
	float fragment_values[VITA_FU_COUNT][4];
	BOOL fragment_valid;
} worker_build;

/* the texture scale rows of the fragment values: 1 for a swizzled
texture, markers naming a linear one */
static void worker_texture_scale_markers(const struct record_state *state, float values[VITA_FU_COUNT][4])
{
	const DWORD *rs = state->material->render_state;
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		const DWORD *header = state->texture_header[stage];

		values[VITA_FU_TEXTURE_SCALE + stage][0] = 1.0f;
		values[VITA_FU_TEXTURE_SCALE + stage][1] = 1.0f;
		values[VITA_FU_TEXTURE_SCALE + stage][2] = 1.0f;
		values[VITA_FU_TEXTURE_SCALE + stage][3] = 1.0f;
		if (state->textures[stage])
		{
			struct xgpu_texture_description description;

			xgpu_texture_describe(header[3], header[4], &description);
			if (description.linear)
			{
				/* (identity markers, as fragment_uniforms_update: the
				worker writes the real scale in execute_draw) */
				values[VITA_FU_TEXTURE_SCALE + stage][0] = (float)header[1];
				values[VITA_FU_TEXTURE_SCALE + stage][1] = (float)header[3];
				values[VITA_FU_TEXTURE_SCALE + stage][2] = (float)header[4];
				values[VITA_FU_TEXTURE_SCALE + stage][3] = (float)((rs[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f);
			}
		}
	}
}

static void worker_fragment_values(const struct record_state *state, float values[VITA_FU_COUNT][4])
{
	const DWORD *rs = state->material->render_state;
	const DWORD *v = state->values->render_state;
	int stage;

	memset(values, 0, sizeof(float) * 4 * VITA_FU_COUNT);
	for (stage = 0; stage < 8; stage++)
	{
		color_to_vec4(v[RECORD_VALUE_PS_C0 + stage], values[VITA_FU_PS_C0 + stage]);
		color_to_vec4(v[RECORD_VALUE_PS_C1 + stage], values[VITA_FU_PS_C1 + stage]);
	}
	color_to_vec4(v[RECORD_VALUE_FINAL_C0], values[VITA_FU_PS_FINAL_C0]);
	color_to_vec4(v[RECORD_VALUE_FINAL_C1], values[VITA_FU_PS_FINAL_C1]);
	color_to_vec4(v[RECORD_VALUE_FOG_COLOR], values[VITA_FU_FOG_COLOR]);
	values[VITA_FU_FOG_PARAMETERS][0] = dword_to_float(v[RECORD_VALUE_FOG_START]);
	values[VITA_FU_FOG_PARAMETERS][1] = dword_to_float(v[RECORD_VALUE_FOG_END]);
	values[VITA_FU_FOG_PARAMETERS][2] = dword_to_float(v[RECORD_VALUE_FOG_DENSITY]);
	values[VITA_FU_MISCELLANEOUS][0] = (float)(rs[D3DRS_ALPHAREF] & 0xff);
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		const DWORD *bump = state->values->bump[stage];

		values[VITA_FU_BUMP_MATRIX + stage][0] = dword_to_float(bump[D3DTSS_BUMPENVMAT00 - D3DTSS_BUMPENVMAT00]);
		values[VITA_FU_BUMP_MATRIX + stage][1] = dword_to_float(bump[D3DTSS_BUMPENVMAT01 - D3DTSS_BUMPENVMAT00]);
		values[VITA_FU_BUMP_MATRIX + stage][2] = dword_to_float(bump[D3DTSS_BUMPENVMAT10 - D3DTSS_BUMPENVMAT00]);
		values[VITA_FU_BUMP_MATRIX + stage][3] = dword_to_float(bump[D3DTSS_BUMPENVMAT11 - D3DTSS_BUMPENVMAT00]);
		values[VITA_FU_BUMP_LUMINANCE + stage][0] = dword_to_float(bump[D3DTSS_BUMPENVLSCALE - D3DTSS_BUMPENVMAT00]);
		values[VITA_FU_BUMP_LUMINANCE + stage][1] = dword_to_float(bump[D3DTSS_BUMPENVLOFFSET - D3DTSS_BUMPENVMAT00]);
	}
	worker_texture_scale_markers(state, values);
}

/* the cull mode names the screen winding to discard */
static unsigned long worker_cull(const struct record_state *state)
{
	DWORD cull = state->values->render_state[RECORD_VALUE_CULL_MODE];

	return cull == D3DCULL_NONE ? 0 : cull;
}

/* the parts of a record that depend on its textures rather than its
material: the stages' presence, headers and palettes, and the key's
coordinate fetches; nonzero when a stage takes computed coordinates */
static int worker_texture_bits(struct render_command *command, const struct record_state *state,
	const struct vertex_shader_object *program, struct nv2a_pixel_shader_key *key)
{
	const DWORD *rs = state->material->render_state;
	int stage, computed_stage_draw = 0;
	static int raw_texcoords = -1;

	if (raw_texcoords < 0)
	{
		const char *setting = getenv("HALO_RAW_TEXCOORDS");
		raw_texcoords = !setting || atoi(setting) != 0;
	}
	key->raw_coordinates = 0;
	key->projective_coordinates = 0;
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		unsigned long mode = (rs[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f;

		command->texture_present[stage] = state->textures[stage] != NULL;
		memcpy(command->texture_header[stage], state->texture_header[stage], sizeof(command->texture_header[stage]));
		command->palette[stage] = state->palette_data[stage];
		if (state->textures[stage])
		{
			if (raw_texcoords && mode == 1)
			{
				struct xgpu_texture_description description;

				xgpu_texture_describe(state->texture_header[stage][3], state->texture_header[stage][4], &description);
				if (!description.linear && !description.cube_map)
				{
					if (!(program->texcoord_w_mask & (1UL << stage)))
						key->raw_coordinates |= (unsigned char)(1U << stage);
					else
					{
						key->projective_coordinates |= (unsigned char)(1U << stage);
						computed_stage_draw = 1;
					}
				}
				else
					computed_stage_draw = 1;
			}
			else if (mode == 1)
				computed_stage_draw = 1;
		}
	}
	return computed_stage_draw;
}

/* the fragment uniform snapshots for these values: a half equal to the
last block's keeps its snapshot */
static void worker_fragment_snapshots(float values[VITA_FU_COUNT][4], int first_half)
{
	int half;

	for (half = first_half; half < 2; half++)
	{
		unsigned long first = half ? VITA_FU_A_COUNT : 0;
		unsigned long bytes = (half ? VITA_FU_COUNT - VITA_FU_A_COUNT : VITA_FU_A_COUNT) * sizeof(values[0]);

		if (!worker_build.fragment_valid || !worker_build.fragment_uniforms[half] ||
			memcmp(values[first], worker_build.fragment_values[first], bytes))
		{
			void *copy = vgxm_worker_alloc(bytes, 16);

			if (copy)
				memcpy(copy, values[first], bytes);
			worker_build.fragment_uniforms[half] = copy;
			memcpy(worker_build.fragment_values[first], values[first], bytes);
			stats.fragment_snapshots++;
			stats.copied_uniforms += bytes;
			stats.copied_fragment += bytes;
		}
	}
	worker_build.fragment_valid = TRUE;
}


/* the worker's next frame: its ring starts afresh (vgxm_present) */
static void worker_build_frame_end(void)
{
	worker_build.state = NULL;
	worker_build.fragment_valid = FALSE;
}

static BOOL worker_build_record(struct render_command *command)
{
	const struct record_state *state = command->state;
	const DWORD *rs = state->material->render_state;
	struct vgxm_draw *draw = &command->draw;
	struct nv2a_pixel_shader_key *key = &command->key;
	struct vertex_shader_object *program = command->program;
	BOOL has_depth = command->has_depth;
	int stage, computed_stage_draw = 0;

	if (worker_build.state == state && worker_build.program == program && worker_build.has_depth == command->has_depth &&
		worker_build.simple == command->simple)
	{
		*key = worker_build.key;
		memcpy(command->texture_header, worker_build.texture_header, sizeof(command->texture_header));
		memcpy(command->texture_present, worker_build.texture_present, sizeof(command->texture_present));
		memcpy(command->palette, worker_build.palette, sizeof(command->palette));
		memcpy(command->sampler_state, worker_build.sampler_state, sizeof(command->sampler_state));
		memcpy(&draw->depth_test, &worker_build.draw_states.depth_test,
			offsetof(struct vgxm_draw, color_write) + sizeof(draw->color_write) - offsetof(struct vgxm_draw, depth_test));
		draw->cull = worker_build.draw_states.cull;
		draw->depth_bias_slope = worker_build.draw_states.depth_bias_slope;
		draw->depth_bias_units = worker_build.draw_states.depth_bias_units;
		draw->fragment_uniforms[0] = worker_build.fragment_uniforms[0];
		draw->fragment_uniforms[1] = worker_build.fragment_uniforms[1];
		return draw->fragment_uniforms[0] && draw->fragment_uniforms[1];
	}
	if (worker_build.state && worker_build.material == state->material && worker_build.program == program &&
		worker_build.has_depth == command->has_depth && worker_build.simple == command->simple)
	{
		/* another block of the same material: the key, samplers and render
		states are the last block's, and only what the textures decide is
		made anew - the coordinate fetches, the texture parts, and the
		texture scale markers of the second fragment uniform half */
		float values[VITA_FU_COUNT][4];

		stats.worker_texture_builds++;
		*key = worker_build.key;
		computed_stage_draw = worker_texture_bits(command, state, program, key);
		memcpy(command->sampler_state, worker_build.sampler_state, sizeof(command->sampler_state));
		memcpy(&draw->depth_test, &worker_build.draw_states.depth_test,
			offsetof(struct vgxm_draw, color_write) + sizeof(draw->color_write) - offsetof(struct vgxm_draw, depth_test));
		draw->cull = worker_cull(state);
		worker_build.draw_states.cull = draw->cull;
		draw->depth_bias_slope = worker_build.draw_states.depth_bias_slope;
		draw->depth_bias_units = worker_build.draw_states.depth_bias_units;
		if (state->values != worker_build.values)
		{
			/* (another object's values: both halves anew - each kept if
			equal to the last) */
			worker_fragment_values(state, values);
			worker_fragment_snapshots(values, 0);
			worker_build.values = state->values;
		}
		else
		{
			memcpy(values, worker_build.fragment_values, sizeof(values));
			worker_texture_scale_markers(state, values);
			/* (the first half is the values': kept, unless its snapshot
			could not be made) */
			worker_fragment_snapshots(values, worker_build.fragment_uniforms[0] ? 1 : 0);
		}
		draw->fragment_uniforms[0] = worker_build.fragment_uniforms[0];
		draw->fragment_uniforms[1] = worker_build.fragment_uniforms[1];
		worker_build.state = state;
		worker_build.key = *key;
		memcpy(worker_build.texture_header, command->texture_header, sizeof(command->texture_header));
		memcpy(worker_build.texture_present, command->texture_present, sizeof(command->texture_present));
		memcpy(worker_build.palette, command->palette, sizeof(command->palette));
		if (computed_stage_draw)
			stats.computed_draws++;
		return draw->fragment_uniforms[0] && draw->fragment_uniforms[1];
	}

	stats.worker_builds++;
	memset(key, 0, sizeof(*key));
	memcpy(key->combiner_state, rs, sizeof(key->combiner_state));
	memset(&key->combiner_state[D3DRS_PSCONSTANT0_0], 0, 16 * sizeof(DWORD));
	key->combiner_state[D3DRS_PSFINALCOMBINERCONSTANT0] = 0;
	key->combiner_state[D3DRS_PSFINALCOMBINERCONSTANT1] = 0;
	key->texture_modes = rs[D3DRS_PSTEXTUREMODES];
	computed_stage_draw = worker_texture_bits(command, state, program, key);
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		const DWORD *ts = state->material->texture_state[stage];

		key->alpha_kill[stage] = ts[D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key->color_sign[stage] = (unsigned char)((ts[D3DTSS_COLORSIGN] >> 28) & 0xf);
		command->sampler_state[stage][0] = ts[D3DTSS_MINFILTER];
		command->sampler_state[stage][1] = ts[D3DTSS_MAGFILTER];
		command->sampler_state[stage][2] = ts[D3DTSS_MIPFILTER];
		command->sampler_state[stage][3] = ts[D3DTSS_ADDRESSU];
		command->sampler_state[stage][4] = ts[D3DTSS_ADDRESSV];
		command->sampler_state[stage][5] = ts[D3DTSS_MIPMAPLODBIAS];
	}
	key->alpha_test_function = rs[D3DRS_ALPHATESTENABLE] ? rs[D3DRS_ALPHAFUNC] : 0;
	{
		static int simple_mode = -1, simple_all = -1, no_alpha_test = -1, dropped_alpha_tests = -1;

		if (simple_mode < 0)
		{
			const char *setting = getenv("HALO_SIMPLE_FRAG_MODE"), *all = getenv("HALO_SIMPLE_FRAG_ALL");
			const char *no = getenv("HALO_NO_ALPHA_TEST"), *keep = getenv("HALO_KEEP_ALPHA_TEST");

			simple_mode = setting && atoi(setting) ? atoi(setting) : 1;
			simple_all = all && atoi(all) != 0;
			no_alpha_test = no && atoi(no) != 0;
			dropped_alpha_tests = !(keep && atoi(keep) != 0);
		}
		key->pad = (command->simple || simple_all) ? simple_mode : 0;
		if (no_alpha_test)
			key->alpha_test_function = 0;
		else if (dropped_alpha_tests && key->alpha_test_function && rs[D3DRS_ALPHABLENDENABLE] && rs[D3DRS_SRCBLEND] == D3DBLEND_SRCALPHA &&
			(rs[D3DRS_DESTBLEND] == D3DBLEND_INVSRCALPHA || rs[D3DRS_DESTBLEND] == D3DBLEND_ONE) &&
			!(has_depth && rs[D3DRS_ZENABLE] && rs[D3DRS_ZWRITEENABLE]) &&
			!(has_depth && rs[D3DRS_STENCILENABLE] && (rs[D3DRS_STENCILPASS] != D3DSTENCILOP_KEEP ||
				rs[D3DRS_STENCILFAIL] != D3DSTENCILOP_KEEP || rs[D3DRS_STENCILZFAIL] != D3DSTENCILOP_KEEP)) &&
			((key->alpha_test_function == D3DCMP_GREATER && (rs[D3DRS_ALPHAREF] & 0xff) == 0) ||
				(key->alpha_test_function == D3DCMP_GREATEREQUAL && (rs[D3DRS_ALPHAREF] & 0xff) <= 1)))
		{
			key->alpha_test_function = 0;
			stats.dropped_alpha_tests++;
		}
		if (key->alpha_test_function)
			stats.alpha_tested_draws++;
	}
	key->fog_enable = rs[D3DRS_FOGENABLE] != 0;
	key->fog_table_mode = (unsigned char)rs[D3DRS_FOGTABLEMODE];

	draw->depth_test = has_depth && rs[D3DRS_ZENABLE];
	draw->depth_write = draw->depth_test && rs[D3DRS_ZWRITEENABLE];
	draw->depth_function = rs[D3DRS_ZFUNC];
	draw->stencil_test = has_depth && rs[D3DRS_STENCILENABLE];
	draw->stencil_function = rs[D3DRS_STENCILFUNC];
	draw->stencil_reference = rs[D3DRS_STENCILREF];
	draw->stencil_read_mask = rs[D3DRS_STENCILMASK];
	draw->stencil_write_mask = rs[D3DRS_STENCILWRITEMASK];
	draw->stencil_fail = rs[D3DRS_STENCILFAIL];
	draw->stencil_depth_fail = rs[D3DRS_STENCILZFAIL];
	draw->stencil_pass = rs[D3DRS_STENCILPASS];
	draw->blend = rs[D3DRS_ALPHABLENDENABLE] != 0;
	draw->blend_source = rs[D3DRS_SRCBLEND];
	draw->blend_destination = rs[D3DRS_DESTBLEND];
	draw->blend_operation = rs[D3DRS_BLENDOP];
	draw->color_write = rs[D3DRS_COLORWRITEENABLE];
	draw->cull = worker_cull(state);
	draw->depth_bias_slope = draw->depth_bias_units = 0.0f;
	if (rs[D3DRS_SOLIDOFFSETENABLE])
	{
		draw->depth_bias_slope = dword_to_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]);
		draw->depth_bias_units = dword_to_float(rs[D3DRS_POLYGONOFFSETZOFFSET]);
	}

	{
		float values[VITA_FU_COUNT][4];

		worker_fragment_values(state, values);
		worker_fragment_snapshots(values, 0);
		draw->fragment_uniforms[0] = worker_build.fragment_uniforms[0];
		draw->fragment_uniforms[1] = worker_build.fragment_uniforms[1];
	}

	worker_build.state = state;
	worker_build.material = state->material;
	worker_build.values = state->values;
	worker_build.program = program;
	worker_build.has_depth = command->has_depth;
	worker_build.simple = command->simple;
	worker_build.key = *key;
	memcpy(worker_build.texture_header, command->texture_header, sizeof(command->texture_header));
	memcpy(worker_build.texture_present, command->texture_present, sizeof(command->texture_present));
	memcpy(worker_build.palette, command->palette, sizeof(command->palette));
	memcpy(worker_build.sampler_state, command->sampler_state, sizeof(command->sampler_state));
	worker_build.draw_states = *draw;
	if (computed_stage_draw)
		stats.computed_draws++;
	return draw->fragment_uniforms[0] && draw->fragment_uniforms[1];
}

static void execute_draw(struct render_command *command)
{
	struct vgxm_draw *draw = &command->draw;
	float texture_scale[4][4];
	BOOL has_depth;
	int stage;

	unsigned long long profile_from;

	worker_sampled = draw_profile_on() && ++worker_counter % (unsigned long)draw_profile == 0;
	if (worker_sampled)
		worker_profile_draws++;
	profile_from = worker_sampled ? vita_host_time_us() : 0;
	if (command->state && !worker_build_record(command))
		return;
	DRAW_PROFILE_ADD(4, profile_from);
	if (!bind_recorded_targets(command, &has_depth))
	{
		stats.skipped_no_target++;
		return;
	}
	DRAW_PROFILE_ADD(4, profile_from);
	command->skip = FALSE;
	bind_recorded_textures(command, texture_scale);
	if (command->skip)
		return;
	DRAW_PROFILE_ADD(5, profile_from);
	/* the texture scale, known only now: a linear texture's 1/size goes
	into a copy of the second uniform buffer of this draw's own (the
	record's snapshot is shared with the draws around it, and is only made
	by worker_build_record above, so it is read here, not before) */
	if (!draw->fragment_uniforms[1])
		return;
	{
		/* (the snapshot holds 1 for a swizzled texture and markers naming a
		linear one: any difference from the real scale needs the copy) */
		const float (*snapshot)[4] = (const float (*)[4])draw->fragment_uniforms[1] + (VITA_FU_TEXTURE_SCALE - VITA_FU_A_COUNT);

		for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
			if (memcmp(snapshot[stage], texture_scale[stage], sizeof(float) * 4))
				break;
	}
	if (stage < D3DTSS_MAXSTAGES)
	{
		unsigned long bytes = (VITA_FU_COUNT - VITA_FU_A_COUNT) * sizeof(float) * 4;
		float (*copy)[4] = vgxm_worker_alloc(bytes, 16);

		if (!copy)
			return;
		memcpy(copy, draw->fragment_uniforms[1], bytes);
		for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
			memcpy(copy[VITA_FU_TEXTURE_SCALE - VITA_FU_A_COUNT + stage], texture_scale[stage], sizeof(float) * 4);
		draw->fragment_uniforms[1] = copy;
	}
	draw->fragment_shader = fragment_shader_get(&command->key);
	{
		/* (debug) HALO_TRACE_LINEAR=1: the first draws that sample a 640x480
		linear texture (the movie), with their vertices */
		static int trace = -1, traced;

		if (trace < 0)
		{
			const char *setting = getenv("HALO_TRACE_LINEAR");
			trace = setting && atoi(setting) != 0;
		}
		if (trace && traced < 4 && command->texture_present[0] && command->texture_header[0][4] &&
			(command->texture_header[0][4] & 0xfff) == 639)
		{
			const float *v = (const float *)draw->streams[0];
			unsigned long stride = draw->strides[0] / 4, a, i;

			traced++;
			platform_log("linear trace: immediate %d vs %lu ps %08lx modes %08lx raw %x proj %x attributes %lu stride %lu scale %.6f %.6f",
				command->immediate, command->program->id, hash_words(&command->key, sizeof(command->key)),
				(unsigned long)command->key.texture_modes, command->key.raw_coordinates, command->key.projective_coordinates,
				draw->attribute_count, draw->strides[0], texture_scale[0][0], texture_scale[0][1]);
			for (a = 0; a < draw->attribute_count; a++)
				platform_log("  attribute reg %u offset %u", draw->attributes[a].reg, draw->attributes[a].offset);
			{
				const float *c2 = (const float *)draw->vertex_chunks[VITA_VC_C2];

				platform_log("  cC2[0..4]: %p %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f", (const void *)c2,
					c2 ? c2[0] : -1.0f, c2 ? c2[1] : -1.0f, c2 ? c2[2] : -1.0f, c2 ? c2[3] : -1.0f,
					c2 ? c2[16] : -1.0f, c2 ? c2[17] : -1.0f, c2 ? c2[18] : -1.0f, c2 ? c2[19] : -1.0f);
			}
			for (i = 0; v && i < 4 && i < draw->index_count; i++)
				platform_log("  vertex %lu: %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f", i,
					v[i * stride + 0], v[i * stride + 1], v[i * stride + 2], v[i * stride + 3],
					stride > 4 ? v[i * stride + 4] : 0.0f, stride > 5 ? v[i * stride + 5] : 0.0f, stride > 6 ? v[i * stride + 6] : 0.0f, stride > 7 ? v[i * stride + 7] : 0.0f,
					stride > 8 ? v[i * stride + 8] : 0.0f, stride > 9 ? v[i * stride + 9] : 0.0f, stride > 10 ? v[i * stride + 10] : 0.0f, stride > 11 ? v[i * stride + 11] : 0.0f);
		}
	}
	if (draw->blend)
	{
		/* the programs the blended passes run, named once each, up to a
		few per blend pair (their Cg is ps_<hash>.cg under
		HALO_GPU_DUMP_SHADERS) */
		/* (a program is named by its shader id: hashing the key each draw
		cost the worker a 256-byte hash per blended draw) */
		static unsigned long named[64], named_pair[64];
		static int named_count;
		unsigned long pair = draw->blend_source << 16 | draw->blend_destination;
		int index, per_pair = 0;

		for (index = 0; index < named_count; index++)
		{
			if (named[index] == draw->fragment_shader && named_pair[index] == pair)
				break;
			if (named_pair[index] == pair)
				per_pair++;
		}
		if (index == named_count && named_count < 64 && per_pair < 8)
		{
			unsigned long hash = hash_words(&command->key, sizeof(command->key));

			named[named_count++] = draw->fragment_shader;
			named_pair[named_count - 1] = pair;
			platform_log("blend %lu/%lu draw: ps %08lx (shader %lu, modes %08lx, combiners %lu, samplers %d %d %d %d)",
				draw->blend_source, draw->blend_destination, hash, draw->fragment_shader,
				(unsigned long)command->key.texture_modes, (unsigned long)(command->key.combiner_state[D3DRS_PSCOMBINERCOUNT] & 0xff),
				command->key.sampler_type[0], command->key.sampler_type[1], command->key.sampler_type[2], command->key.sampler_type[3]);
		}
	}
	draw->vertex_shader = command->immediate ?
		vertex_shader_get(command->program, command->provided_mask, 0, 0) :
		vertex_shader_get(command->program, command->provided_mask, command->packed_mask, command->color_mask);
	if (!draw->fragment_shader || !draw->vertex_shader)
	{
		stats.skipped_shader++;
		return;
	}
	DRAW_PROFILE_ADD(6, profile_from);
	draw->vertex_input_mask = command->program->input_mask;
	vgxm_draw(draw);
	DRAW_PROFILE_ADD(7, profile_from);
}

static void write_screenshot(struct render_target_entry *target);
int halo_trace_active(void);

static void execute_command(struct render_command *command)
{
	/* (timed for the statistics only, as layer_enter) */
	int timed = gpu_stats_on > 0;
	unsigned long long before = timed ? vita_host_time_us() : 0;
	BOOL has_depth;

	switch (command->kind)
	{
	case _command_draw:
		execute_draw(command);
		break;
	case _command_clear:
		if (bind_recorded_targets(command, &has_depth))
		{
			unsigned long flags = command->clear_flags;

			if (!has_depth)
				flags &= ~(D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL);
			vgxm_clear(flags, command->clear_color, command->clear_depth, command->clear_stencil, command->clip);
		}
		break;
	case _command_present:
	{
		struct render_target_entry *back_buffer = command->color_valid ? render_target_get(&command->color_surface) : NULL;

		if (halo_trace_active())
			platform_log("trace: worker present %lu", command->frame);
		if (back_buffer)
		{
			if (command->screenshot)
				write_screenshot(back_buffer);
			vgxm_present(back_buffer->id, back_buffer->target.width, back_buffer->target.height);
		}
		if (halo_trace_active())
			platform_log("trace: worker presented %lu", command->frame);
		worker_build_frame_end();
		vita_texture_cache_begin_frame();
		break;
	}
	}
	if (timed)
	{
		before = vita_host_time_us() - before;
		worker_time += before;
		worker_kind_time[command->kind] += before;
	}
}

static void *render_worker(void *unused)
{
	(void)unused;
	/* (a pthread, since it waits on pthread condition variables; pinned
	to the second core) */
	vita_host_pin_current_thread(1);
	{
		/* the records of the frame that are not hoisted, run at its present */
		static unsigned long deferred[COMMAND_RING];
		unsigned long deferred_count = 0, index = 0;

		for (;;)
		{
			struct render_command *command;
			unsigned long spins = 0;

			while (__atomic_load_n(&command_head, __ATOMIC_ACQUIRE) == command_tail + index)
			{
				/* a short spin covers the gap between draws; then sleep */
				if (++spins < 200)
					continue;
				vita_host_sleep_us(50);
			}
			command = &commands[(command_tail + index) % COMMAND_RING];
			if (command->kind == _command_present)
			{
				unsigned long each;

				for (each = 0; each < deferred_count; each++)
					execute_command(&commands[deferred[each] % COMMAND_RING]);
				deferred_count = 0;
				execute_command(command);
				__atomic_store_n(&frames_presented, frames_presented + 1, __ATOMIC_RELEASE);
				__atomic_store_n(&command_tail, command_tail + index + 1, __ATOMIC_RELEASE);
				index = 0;
			}
			else if (command->hoistable)
			{
				execute_command(command);
				index++;
			}
			else
			{
				deferred[deferred_count++] = command_tail + index;
				index++;
			}
		}
	}
	return NULL;
}

/* ---------- recording (the game's side) */

static void worker_start(void)
{
	const char *setting = getenv("HALO_RENDER_THREAD");

	worker_enabled = !setting || atoi(setting) != 0;
	commands = calloc(COMMAND_RING, sizeof(*commands));
	if (!commands)
		worker_enabled = 0;
	if (worker_enabled)
	{
		pthread_t thread;
		pthread_attr_t attributes;

		pthread_attr_init(&attributes);
		pthread_attr_setstacksize(&attributes, 1024 * 1024);
		if (pthread_create(&thread, &attributes, render_worker, NULL) != 0)
		{
			platform_log("cannot start the render worker: rendering on the game's thread");
			worker_enabled = 0;
		}
		pthread_attr_destroy(&attributes);
	}
	platform_log("render worker: %s", worker_enabled ? "on core 1" : "off (inline)");
}

/* Targets no wider than this are "small": each run of draws into one gets
a copy of its own, so the worker can render every small run before the
frame's main scene (the game switches to them and back many times a frame,
which costs the GPU a store and reload of the main scene each time). */
#define SMALL_TARGET_WIDTH 256
#define MAXIMUM_TARGET_VERSIONS 24

static struct
{
	unsigned long data;
	unsigned long version;
} target_versions[32];
static unsigned long target_version_count;
static unsigned long last_recorded_target;

static BOOL surface_is_small_cached(const D3DSurface *surface);
static BOOL surface_is_depth_cached(const D3DSurface *surface);

static BOOL surface_is_small(const D3DSurface *surface)
{
	unsigned long width, height;
	BOOL depth;

	if (!surface || !surface->Data)
		return FALSE;
	surface_dimensions(surface, &width, &height, &depth);
	return width <= SMALL_TARGET_WIDTH && height <= SMALL_TARGET_WIDTH;
}

static unsigned long *target_version_slot(unsigned long data)
{
	unsigned long index;

	for (index = 0; index < target_version_count; index++)
	{
		if (target_versions[index].data == data)
			return &target_versions[index].version;
	}
	if (target_version_count >= sizeof(target_versions) / sizeof(target_versions[0]))
		return NULL;
	target_versions[target_version_count].data = data;
	target_versions[target_version_count].version = 0;
	return &target_versions[target_version_count++].version;
}

/* the draw being recorded runs the constant fragment program
(HALO_SIMPLE_FRAG_BLENDS) */
static int simple_fragment;

/* An immediate-mode draw (the HUD, text, particles, lens flares, effects:
a few vertices each) is held back, its vertices on the CPU, and the next
immediate draw with the same primitive list, program, state and constants
- nothing recorded between them - adds its vertices to it instead of
being a draw of its own: the same triangles in the same order, as one
draw. Anything else recorded commits the held draw first
(command_begin). HALO_IMMEDIATE_MERGE=0 draws each on its own */
static struct
{
	struct render_command *command;
	D3DPRIMITIVETYPE type;
	unsigned long count, stride, constants;
	float *vertices;
	unsigned long capacity;
	/* the triangle family (lists, strips, fans, quads) is held as one
	indexed triangle list, which any of them can join */
	int triangles;
	unsigned short *indices;
	unsigned long index_count, index_capacity;
} held_immediate;

static int immediate_triangle_family(D3DPRIMITIVETYPE type)
{
	return type == D3DPT_TRIANGLELIST || type == D3DPT_TRIANGLESTRIP || type == D3DPT_TRIANGLEFAN ||
		type == D3DPT_POLYGON || type == D3DPT_QUADLIST || type == D3DPT_QUADSTRIP;
}

/* a primitive's triangles as indices from base on, each with the facing
it has as a strip or fan (odd strip triangles turned back) */
static int immediate_hold_triangles(D3DPRIMITIVETYPE type, unsigned long base, unsigned long count)
{
	unsigned long triangles, needed, i;
	unsigned short *out;

	switch (type)
	{
	case D3DPT_TRIANGLELIST: triangles = count / 3; break;
	case D3DPT_QUADLIST: triangles = count / 4 * 2; break;
	default: triangles = count >= 3 ? count - 2 : 0; break;
	}
	needed = held_immediate.index_count + triangles * 3;
	if (needed > held_immediate.index_capacity)
	{
		held_immediate.index_capacity = needed > held_immediate.index_capacity * 2 ? needed : held_immediate.index_capacity * 2;
		held_immediate.indices = realloc(held_immediate.indices, held_immediate.index_capacity * sizeof(unsigned short));
		if (!held_immediate.indices)
			return 0;
	}
	out = held_immediate.indices + held_immediate.index_count;
	for (i = 0; i < triangles; i++, out += 3)
	{
		unsigned long a, b, c;

		switch (type)
		{
		case D3DPT_TRIANGLELIST: a = i * 3; b = a + 1; c = a + 2; break;
		case D3DPT_QUADLIST: /* (0 1 2) and (0 2 3), as converted_indices */
			a = i / 2 * 4;
			b = (i & 1) ? a + 2 : a + 1;
			c = (i & 1) ? a + 3 : a + 2;
			break;
		case D3DPT_TRIANGLEFAN: case D3DPT_POLYGON: a = 0; b = i + 1; c = i + 2; break;
		default: /* strips and quad strips */
			if (i & 1) { a = i + 1; b = i; c = i + 2; } else { a = i; b = i + 1; c = i + 2; }
			break;
		}
		out[0] = (unsigned short)(base + a);
		out[1] = (unsigned short)(base + b);
		out[2] = (unsigned short)(base + c);
	}
	held_immediate.index_count = needed;
	return 1;
}
static unsigned long merged_immediate_draws, merge_rejected[5];

static void command_commit(struct render_command *command);
static unsigned short *converted_indices(D3DPRIMITIVETYPE type, const unsigned short *indices, unsigned long count,
	unsigned long *out_count, unsigned long *out_primitive);
static BOOL needs_conversion(D3DPRIMITIVETYPE type);
static unsigned long gxm_primitive(D3DPRIMITIVETYPE type);

static void immediate_commit_held(void)
{
	struct render_command *command = held_immediate.command;
	struct vgxm_draw *draw;
	unsigned long bytes;
	float *packed;

	if (!command)
		return;
	held_immediate.command = NULL;
	draw = &command->draw;
	bytes = held_immediate.count * held_immediate.stride;
	packed = vgxm_ring_alloc(bytes, 16);
	stats.copied_immediate += bytes;
	stats.copied_bytes += bytes;
	if (!packed)
		return;
	memcpy(packed, held_immediate.vertices, bytes);
	draw->streams[0] = packed;
	if (held_immediate.triangles)
	{
		unsigned short *indices = held_immediate.index_count ?
			vgxm_ring_alloc(held_immediate.index_count * sizeof(unsigned short) + 4, 16) : NULL;

		if (!indices)
			return;
		memcpy(indices, held_immediate.indices, held_immediate.index_count * sizeof(unsigned short));
		draw->indices = indices;
		draw->index_count = held_immediate.index_count;
		draw->primitive = D3DPT_TRIANGLELIST;
		stats.copied_indices += held_immediate.index_count * sizeof(unsigned short);
	}
	else if (needs_conversion(held_immediate.type))
	{
		draw->indices = converted_indices(held_immediate.type, NULL, held_immediate.count, &draw->index_count,
			&draw->primitive);
		if (!draw->indices)
			return;
	}
	else
	{
		draw->primitive = gxm_primitive(held_immediate.type);
		draw->indices = device.sequential_indices;
		draw->index_count = held_immediate.count;
	}
	command_commit(command);
}

static struct render_command *command_begin(unsigned long kind)
{
	struct render_command *command;

	/* (the held immediate draw goes first, in its place) */
	immediate_commit_held();

	if (worker_enabled < 0)
		worker_start();
	if (!commands)
		return NULL;
	if (worker_enabled)
	{
		while (command_head - __atomic_load_n(&command_tail, __ATOMIC_ACQUIRE) >= COMMAND_RING)
			vita_host_sleep_us(100);
	}
	{
		/* HALO_NO_SMALL_TARGETS=1: nothing is drawn into the small targets
		(object shadows, reflections), to measure what they cost the GPU */
		static int no_small_targets = -1;

		if (no_small_targets < 0)
		{
			const char *setting = getenv("HALO_NO_SMALL_TARGETS");
			no_small_targets = setting && atoi(setting) != 0;
		}
		if (no_small_targets && kind != _command_present && device.render_target && surface_is_small_cached(device.render_target))
			return NULL;
	}
	{
		/* HALO_SKIP_DRAWS=1: no draws at all (the GPU's idle baseline);
		HALO_SKIP_BLENDED=1: no blended draws (the lighting passes, effects
		and transparents: what the tile renderer cannot hide-surface-remove) */
		static int skip_draws = -1, skip_blended = -1;

		if (skip_draws < 0)
		{
			const char *a = getenv("HALO_SKIP_DRAWS"), *b = getenv("HALO_SKIP_BLENDED");
			skip_draws = a && atoi(a) != 0;
			skip_blended = b && atoi(b) != 0;
		}
		if (kind == _command_draw && (skip_draws || (skip_blended && D3D__RenderState[D3DRS_ALPHABLENDENABLE])))
			return NULL;
		{
			/* HALO_SKIP_ADDITIVE=1: no additive blends (ONE, ONE: the lighting
			passes and glows); HALO_SKIP_ALPHA=1: no alpha blends (SRCALPHA,
			INVSRCALPHA: particles, transparents, decals, HUD) */
			static int skip_additive = -1, skip_alpha = -1;
			/* HALO_SKIP_BLENDS=s/d,s/d,...: the blend pairs (the histogram's
			indices) whose draws are dropped, for the GPU attribution */
			static unsigned char skip_pair[16][16];
			/* HALO_SIMPLE_FRAG_BLENDS=s/d,...: those draws run a constant
			fragment program instead of their own (nv2a_psh_cg.c) */
			static unsigned char simple_pair[16][16];

			if (skip_additive < 0)
			{
				const char *a = getenv("HALO_SKIP_ADDITIVE"), *b = getenv("HALO_SKIP_ALPHA"), *c = getenv("HALO_SKIP_BLENDS");
				const char *d = getenv("HALO_SIMPLE_FRAG_BLENDS");
				skip_additive = a && atoi(a) != 0;
				skip_alpha = b && atoi(b) != 0;
				while (c && *c)
				{
					int ps = -1, pd = -1;

					if (sscanf(c, "%d/%d", &ps, &pd) == 2 && ps >= 0 && ps < 16 && pd >= 0 && pd < 16)
						skip_pair[ps][pd] = 1;
					c = strchr(c, ',');
					if (c)
						c++;
				}
				while (d && *d)
				{
					int ps = -1, pd = -1;

					if (sscanf(d, "%d/%d", &ps, &pd) == 2 && ps >= 0 && ps < 16 && pd >= 0 && pd < 16)
						simple_pair[ps][pd] = 1;
					d = strchr(d, ',');
					if (d)
						d++;
				}
			}
			if (kind == _command_draw && D3D__RenderState[D3DRS_ALPHABLENDENABLE])
			{
				DWORD source = D3D__RenderState[D3DRS_SRCBLEND], destination = D3D__RenderState[D3DRS_DESTBLEND];

				/* which blends the frame draws with (the GPU's cost is in
				the blended draws): counted for the frame statistics */
				/* (the Xbox's values are GL's: 0, 1, then 768..776; 2..10
				stand for 768..776 in the histogram) */
				if (source >= 768 && source <= 776)
					source = source - 768 + 2;
				if (destination >= 768 && destination <= 776)
					destination = destination - 768 + 2;
				if (source < 16 && destination < 16)
				{
					blend_histogram[source][destination]++;
					if (skip_pair[source][destination])
						return NULL;
					simple_fragment = simple_pair[source][destination];
				}
				if (skip_additive && destination == D3DBLEND_ONE)
					return NULL;
				if (skip_alpha && destination != D3DBLEND_ONE)
					return NULL;
			}
			else if (kind == _command_draw)
				blend_histogram[0][0]++;
		}
	}
	command = &commands[command_head % COMMAND_RING];
	command->kind = kind;
	command->state = NULL;
	command->color_valid = device.render_target != NULL;
	command->depth_valid = device.depth_stencil != NULL;
	command->color_version = 0;
	command->hoistable = FALSE;
	if (device.render_target)
		command->color_surface = *device.render_target;
	if (device.depth_stencil)
		command->depth_surface = *device.depth_stencil;
	if (kind != _command_present && command->color_valid && surface_is_small_cached(&command->color_surface) &&
		!(command->depth_valid && device.depth_stencil->Data && !surface_is_small(&command->depth_surface)))
	{
		unsigned long *version = target_version_slot(command->color_surface.Data);

		if (version)
		{
			/* a new run into the target: a fresh copy */
			if (last_recorded_target != command->color_surface.Data && *version < MAXIMUM_TARGET_VERSIONS)
				(*version)++;
			command->color_version = *version;
			command->hoistable = *version > 0;
		}
	}
	last_recorded_target = command->color_valid ? command->color_surface.Data : 0;
	return command;
}

static void command_commit(struct render_command *command)
{
	if (worker_enabled)
	{
		__atomic_store_n(&command_head, command_head + 1, __ATOMIC_RELEASE);
	}
	else
	{
		command_head++;
		execute_command(command);
		command_tail++;
		if (command->kind == _command_present)
			frames_presented++;
	}
}

/* waits until the worker has presented all but the latest frame recorded:
the game runs a frame ahead of the GPU work */
static void worker_drain(void)
{
	if (!worker_enabled)
		return;
	unsigned long long waited_from = 0;

	while (__atomic_load_n(&frames_presented, __ATOMIC_ACQUIRE) + 1 < frames_requested)
	{
		vita_host_sleep_us(50);
		/* the worker not finishing a frame for 12 s is logged (a first
		launch compiles every shader on the device, seconds each, and the
		game thread waits here through it); HALO_HANG_CRASH=1 makes it a
		deliberate crash for a dump instead - which, on the Vita, wedged the
		shell when the dump never completed (the Sept 30 "freezes") */
		if (!waited_from)
			waited_from = vita_host_time_us();
		else if (vita_host_time_us() - waited_from > 12000000ull)
		{
			static volatile unsigned long hung_frame;
			static int crash = -1;

			if (crash < 0)
			{
				const char *setting = getenv("HALO_HANG_CRASH");
				crash = setting && atoi(setting) != 0;
			}
			hung_frame = frames_requested;
			if (crash)
				*(volatile int *)32 = 0;
			platform_log("waited 12 s for the worker to present frame %lu (shaders compiling?): waiting on", frames_requested);
			waited_from = vita_host_time_us();
		}
	}
}

/* has the target a depth format? (no GPU work: the worker creates targets) */
static BOOL surface_is_depth(const D3DSurface *surface)
{
	unsigned long width, height;
	BOOL depth;

	if (!surface || !surface->Data)
		return FALSE;
	surface_dimensions(surface, &width, &height, &depth);
	return depth;
}

/* the description of a stage's texture, decoded from its header words once
per texture change rather than the two or three times a draw's record
asked (the game thread's; the worker describes on its own) */
static const struct xgpu_texture_description *stage_description(int stage, const D3DBaseTexture *texture)
{
	static struct
	{
		DWORD format, size;
		struct xgpu_texture_description description;
	} cache[D3DTSS_MAXSTAGES];

	if (cache[stage].format != texture->Format || cache[stage].size != texture->Size || !cache[stage].description.width)
	{
		xgpu_texture_describe(texture->Format, texture->Size, &cache[stage].description);
		cache[stage].format = texture->Format;
		cache[stage].size = texture->Size;
	}
	return &cache[stage].description;
}

/* a texture addressed in texels (a pitch in its header, or a linear
format): its fetches need the 1/size scale */
static BOOL texture_is_linear(int stage, const D3DBaseTexture *texture)
{
	return stage_description(stage, texture)->linear;
}

/* the surfaces' small and depth tests, decoded once per surface change
(every draw asks about the targets it goes to) */
static BOOL surface_is_small_cached(const D3DSurface *surface)
{
	static D3DSurface last;
	static BOOL small;

	if (!surface || !surface->Data)
		return FALSE;
	if (last.Data != surface->Data || last.Format != surface->Format || last.Size != surface->Size)
	{
		last = *surface;
		small = surface_is_small(surface);
	}
	return small;
}

static BOOL surface_is_depth_cached(const D3DSurface *surface)
{
	static D3DSurface last;
	static BOOL depth;

	if (!surface || !surface->Data)
		return FALSE;
	if (last.Data != surface->Data || last.Format != surface->Format || last.Size != surface->Size)
	{
		last = *surface;
		depth = surface_is_depth(surface);
	}
	return depth;
}

static void fragment_uniforms_update(void)
{
	float values[VITA_FU_COUNT][4];
	int stage;
	unsigned long long fine_from = DRAW_PROFILE_NOW();
	/* the render states, texture stage states and textures the uniforms
	are made from, kept from the last update: when none changed (most
	draws) the values are what they were, and there is nothing to do -
	computing and comparing them cost 5+ us a draw */
	static DWORD inputs_cache[80];
	DWORD inputs[80];
	int count = 0;

	for (stage = 0; stage < 8; stage++)
	{
		inputs[count++] = D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage];
		inputs[count++] = D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage];
	}
	inputs[count++] = D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0];
	inputs[count++] = D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1];
	inputs[count++] = D3D__RenderState[D3DRS_FOGCOLOR];
	inputs[count++] = D3D__RenderState[D3DRS_FOGSTART];
	inputs[count++] = D3D__RenderState[D3DRS_FOGEND];
	inputs[count++] = D3D__RenderState[D3DRS_FOGDENSITY];
	inputs[count++] = D3D__RenderState[D3DRS_ALPHAREF];
	inputs[count++] = D3D__RenderState[D3DRS_PSTEXTUREMODES];
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		DWORD *state = D3D__TextureState[stage];
		D3DBaseTexture *texture = device.textures[stage];

		inputs[count++] = state[D3DTSS_BUMPENVMAT00];
		inputs[count++] = state[D3DTSS_BUMPENVMAT01];
		inputs[count++] = state[D3DTSS_BUMPENVMAT10];
		inputs[count++] = state[D3DTSS_BUMPENVMAT11];
		inputs[count++] = state[D3DTSS_BUMPENVLSCALE];
		inputs[count++] = state[D3DTSS_BUMPENVLOFFSET];
		/* the texture matters to the uniforms only through its scale,
		which is 1 unless the texture is linear (a render target, a HUD
		bitmap): a swizzled texture's identity is left out, so a snapshot
		serves across texture changes (they were most of the snapshots) */
		if (texture && texture_is_linear(stage, texture))
		{
			inputs[count++] = texture->Data;
			inputs[count++] = texture->Format;
			inputs[count++] = texture->Size;
			inputs[count++] = (DWORD)stage_texture_mode(stage) | 0x80000000UL;
		}
		else
		{
			inputs[count++] = 0;
			inputs[count++] = 0;
			inputs[count++] = 0;
			inputs[count++] = (DWORD)stage_texture_mode(stage);
		}
	}
	DRAW_FINE_ADD(3, fine_from);
	if (device.fragment_snapshot[0] && device.fragment_snapshot[1] && !memcmp(inputs, inputs_cache, count * sizeof(DWORD)))
		return;
	memcpy(inputs_cache, inputs, count * sizeof(DWORD));

	memset(values, 0, sizeof(values));
	for (stage = 0; stage < 8; stage++)
	{
		color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage], values[VITA_FU_PS_C0 + stage]);
		color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage], values[VITA_FU_PS_C1 + stage]);
	}
	color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0], values[VITA_FU_PS_FINAL_C0]);
	color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1], values[VITA_FU_PS_FINAL_C1]);
	color_to_vec4(D3D__RenderState[D3DRS_FOGCOLOR], values[VITA_FU_FOG_COLOR]);
	values[VITA_FU_FOG_PARAMETERS][0] = dword_to_float(D3D__RenderState[D3DRS_FOGSTART]);
	values[VITA_FU_FOG_PARAMETERS][1] = dword_to_float(D3D__RenderState[D3DRS_FOGEND]);
	values[VITA_FU_FOG_PARAMETERS][2] = dword_to_float(D3D__RenderState[D3DRS_FOGDENSITY]);
	values[VITA_FU_MISCELLANEOUS][0] = (float)(D3D__RenderState[D3DRS_ALPHAREF] & 0xff);
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		DWORD *state = D3D__TextureState[stage];
		D3DBaseTexture *texture = device.textures[stage];

		values[VITA_FU_BUMP_MATRIX + stage][0] = dword_to_float(state[D3DTSS_BUMPENVMAT00]);
		values[VITA_FU_BUMP_MATRIX + stage][1] = dword_to_float(state[D3DTSS_BUMPENVMAT01]);
		values[VITA_FU_BUMP_MATRIX + stage][2] = dword_to_float(state[D3DTSS_BUMPENVMAT10]);
		values[VITA_FU_BUMP_MATRIX + stage][3] = dword_to_float(state[D3DTSS_BUMPENVMAT11]);
		values[VITA_FU_BUMP_LUMINANCE + stage][0] = dword_to_float(state[D3DTSS_BUMPENVLSCALE]);
		values[VITA_FU_BUMP_LUMINANCE + stage][1] = dword_to_float(state[D3DTSS_BUMPENVLOFFSET]);
		/* the texture scale: 1 for a swizzled texture; for a linear one the
		worker fills it in (bind_recorded_textures), and the texture is
		then part of what makes the snapshot distinct */
		values[VITA_FU_TEXTURE_SCALE + stage][0] = 1.0f;
		values[VITA_FU_TEXTURE_SCALE + stage][1] = 1.0f;
		values[VITA_FU_TEXTURE_SCALE + stage][2] = 1.0f;
		values[VITA_FU_TEXTURE_SCALE + stage][3] = 1.0f;
		if (texture && texture_is_linear(stage, texture))
		{
			values[VITA_FU_TEXTURE_SCALE + stage][0] = (float)texture->Data;
			values[VITA_FU_TEXTURE_SCALE + stage][1] = (float)texture->Format;
			values[VITA_FU_TEXTURE_SCALE + stage][2] = (float)texture->Size;
			values[VITA_FU_TEXTURE_SCALE + stage][3] = (float)stage_texture_mode(stage);
		}
	}
	{
		int half;

		for (half = 0; half < 2; half++)
		{
			unsigned long first = half ? VITA_FU_A_COUNT : 0;
			unsigned long bytes = (half ? VITA_FU_COUNT - VITA_FU_A_COUNT : VITA_FU_A_COUNT) * sizeof(values[0]);

			if (!device.fragment_snapshot[half] || memcmp(values[first], device.fragment_uniforms[first], bytes))
			{
				memcpy(device.fragment_uniforms[first], values[first], bytes);
				device.fragment_snapshot[half] = ring_copy(values[first], bytes);
				stats.fragment_snapshots++;
				stats.copied_uniforms += bytes;
			}
		}
	}
	DRAW_FINE_ADD(4, fine_from);
}

static const void *vertex_uniforms_snapshot(BOOL immediate)
{
	float miscellaneous[4];

	miscellaneous[0] = D3D__RenderState[D3DRS_POINTSIZE] ? dword_to_float(D3D__RenderState[D3DRS_POINTSIZE]) : 1.0f;
	miscellaneous[1] = (float)ui_offset;
	miscellaneous[2] = miscellaneous[3] = 0.0f;
	if (memcmp(device.vertex_uniforms[VITA_VM_MISCELLANEOUS], miscellaneous, sizeof(miscellaneous)))
	{
		memcpy(device.vertex_uniforms[VITA_VM_MISCELLANEOUS], miscellaneous, sizeof(miscellaneous));
		device.vertex_uniform_snapshot = NULL;
	}
	if (!device.vertex_uniform_snapshot || (device.vertex_attributes_changed && !immediate))
	{
		device.vertex_attributes_changed = FALSE;
		device.vertex_uniform_snapshot = ring_copy(device.vertex_uniforms, sizeof(device.vertex_uniforms));
		stats.copied_uniforms += sizeof(device.vertex_uniforms);
		stats.copied_vertex_misc += sizeof(device.vertex_uniforms);
	}
	return device.vertex_uniform_snapshot;
}

/* the program's constants in the ring: a snapshot per chunk it reads, each
serving later draws until a register of it changes. Chunk D (the node
matrices) is copied up to the highest register written this frame or the
last (an object's matrices are written just before its draws; a program's
absolute reads in D are covered too). FALSE when the ring is full. */
static BOOL constants_snapshot(const struct vertex_shader_object *program, struct vgxm_draw *draw)
{
	int chunk;

	for (chunk = 0; chunk < VITA_VC_CHUNKS; chunk++)
	{
		unsigned long first = chunk_first[chunk], count = chunk_end[chunk] - chunk_first[chunk];

		draw->vertex_chunks[chunk] = NULL;
		if (!(program->usage.chunk_mask & (1UL << chunk)))
			continue;
		if (chunk == VITA_VC_D)
		{
			count = program->usage.d_absolute_end;
			if (program->usage.relative)
			{
				/* a program indexing the node matrices reads the matrices of
				the object being drawn, which rasterizer_set_model_skinning
				wrote from D's first register just before (3 per node): the
				snapshot covers them, not every register written lately - the
				largest model's 100+ registers for a one-node prop's 3
				(HALO_D_EXTENT_FRAME=1: up to the highest register written
				this frame or the last, as before) */
				static int frame_extent = -1;
				unsigned long extent;

				if (frame_extent < 0)
				{
					const char *setting = getenv("HALO_D_EXTENT_FRAME");

					frame_extent = setting && atoi(setting) != 0;
				}
				if (frame_extent)
					extent = device.d_extent_frame > device.d_extent_previous ? device.d_extent_frame : device.d_extent_previous;
				else
					extent = device.d_last_object_extent;
				if (extent > count)
					count = extent;
			}
			if (count < 3)
				count = 3;
			if (device.chunk_snapshot[chunk] && device.d_snapshot_count < count)
				device.chunk_snapshot[chunk] = NULL;
		}
		if (!device.chunk_snapshot[chunk])
		{
			device.chunk_snapshot[chunk] = ring_copy(device.constants[first], count * sizeof(device.constants[0]));
			if (!device.chunk_snapshot[chunk])
				return FALSE;
			if (chunk == VITA_VC_D)
				device.d_snapshot_count = count;
			stats.vertex_snapshots++;
			stats.copied_uniforms += count * sizeof(device.constants[0]);
			stats.copied_chunk[chunk] += count * sizeof(device.constants[0]);
		}
		draw->vertex_chunks[chunk] = device.chunk_snapshot[chunk];
		if (chunk == VITA_VC_D)
		{
			/* (the draw hash covers the program's absolute reads and the
			object's matrices, not the stale registers past them) */
			unsigned long hashed = program->usage.d_absolute_end;

			if (program->usage.relative && device.d_last_object_extent > hashed)
				hashed = device.d_last_object_extent;
			draw->vertex_chunk_d_registers = hashed < device.d_snapshot_count ? hashed : device.d_snapshot_count;
		}
	}
	return TRUE;
}

/* The device state the record is made from, as it was at the previous draw
of the frame, and that draw's record: a draw whose state is the same (about
half of a frame's draws repeat the previous one's materials and textures)
copies the record's state blocks instead of building them - the key, the
texture headers, the samplers, the fragment uniforms, the render states -
which was most of the record's cost on the Vita. */
struct record_shadow_state
{
	DWORD render_state[D3DRS_MAX];
	DWORD texture_state[D3DTSS_MAXSTAGES][D3DTSS_MAX];
	D3DBaseTexture *textures[D3DTSS_MAXSTAGES];
	D3DPalette *palettes[D3DTSS_MAXSTAGES];
	struct vertex_shader_object *program, *declaration;
	D3DSurface *render_target, *depth_stencil;
	D3DVIEWPORT8 viewport;
	float viewport_scale[4], viewport_offset[4];
	BOOL visibility_test_active;
	unsigned long visibility_index;
	BOOL immediate;
	unsigned long ui_offset;
	/* (held_shadow) the record's material and values blocks, which hold the
	render and stage states as they were: then they are not copied here */
	const struct record_material *material;
	const struct record_values *values;
};
static struct record_shadow_state record_shadow;
/* the state of the held immediate draw (immediate_end): the next one joins
it only if it is the same */
static struct record_shadow_state held_shadow;

static void shadow_capture(struct record_shadow_state *shadow, struct vertex_shader_object *program, BOOL immediate,
	const struct record_state *state)
{
	/* (the material and values blocks made from the current states stand
	in for a copy of them: 1.1 KB per immediate draw) */
	shadow->material = state ? state->material : NULL;
	shadow->values = state ? state->values : NULL;
	if (!state)
	{
		memcpy(shadow->render_state, D3D__RenderState, sizeof(shadow->render_state));
		memcpy(shadow->texture_state, D3D__TextureState, sizeof(shadow->texture_state));
	}
	memcpy(shadow->textures, device.textures, sizeof(shadow->textures));
	memcpy(shadow->palettes, device.palettes, sizeof(shadow->palettes));
	shadow->program = program;
	shadow->declaration = device.vertex_shader;
	shadow->render_target = device.render_target;
	shadow->depth_stencil = device.depth_stencil;
	shadow->viewport = device.viewport;
	memcpy(shadow->viewport_scale, device.viewport_scale, sizeof(shadow->viewport_scale));
	memcpy(shadow->viewport_offset, device.viewport_offset, sizeof(shadow->viewport_offset));
	shadow->visibility_test_active = device.visibility_test_active;
	shadow->visibility_index = device.visibility_index;
	shadow->immediate = immediate;
	shadow->ui_offset = (unsigned long)ui_offset;
}

static struct record_material *material_last;
static struct record_values *values_last;

/* the current render and stage states are the material's, but for the
draw's values (render_state_values, the bump environment states), which
the material holds as 0 */
static BOOL material_matches_current(const struct record_material *material)
{
	static const unsigned char ranges[][2] = {
		{ 0, D3DRS_PSCONSTANT0_0 }, { D3DRS_PSCONSTANT1_7 + 1, D3DRS_PSFINALCOMBINERCONSTANT0 },
		{ D3DRS_PSFINALCOMBINERCONSTANT1 + 1, D3DRS_FOGSTART }, { D3DRS_FOGDENSITY + 1, D3DRS_FOGCOLOR },
		{ D3DRS_FOGCOLOR + 1, D3DRS_CULLMODE }, { D3DRS_CULLMODE + 1, D3DRS_MAX },
	};
	int range, stage;

	for (range = 0; range < (int)(sizeof(ranges) / sizeof(ranges[0])); range++)
		if (memcmp(&material->render_state[ranges[range][0]], &D3D__RenderState[ranges[range][0]],
			(ranges[range][1] - ranges[range][0]) * sizeof(DWORD)))
			return FALSE;
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		if (memcmp(material->texture_state[stage], D3D__TextureState[stage], D3DTSS_BUMPENVMAT00 * sizeof(DWORD)) ||
			memcmp(&material->texture_state[stage][D3DTSS_BUMPENVLOFFSET + 1], &D3D__TextureState[stage][D3DTSS_BUMPENVLOFFSET + 1],
				(D3DTSS_MAX - D3DTSS_BUMPENVLOFFSET - 1) * sizeof(DWORD)))
			return FALSE;
	return TRUE;
}

static BOOL values_match_current(const struct record_values *values)
{
	int index, stage;

	for (index = 0; index < RECORD_VALUE_COUNT; index++)
		if (values->render_state[index] != D3D__RenderState[record_value_state[index]])
			return FALSE;
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		if (memcmp(values->bump[stage], &D3D__TextureState[stage][D3DTSS_BUMPENVMAT00], sizeof(values->bump[stage])))
			return FALSE;
	return TRUE;
}

/* the render and stage states equal the shadow's: with material and values
blocks, no setter changed a value since those blocks were the last made
(the states are then theirs), else compared */
static BOOL shadow_states_match(const struct record_shadow_state *shadow)
{
	if (shadow->material)
	{
		if (!device_state_dirty && material_last == shadow->material && values_last == shadow->values)
			return TRUE;
		return material_matches_current(shadow->material) && values_match_current(shadow->values);
	}
	return !memcmp(shadow->render_state, D3D__RenderState, sizeof(shadow->render_state)) &&
		!memcmp(shadow->texture_state, D3D__TextureState, sizeof(shadow->texture_state));
}

static BOOL shadow_matches(const struct record_shadow_state *shadow, struct vertex_shader_object *program, BOOL immediate)
{
	return shadow->program == program && shadow->declaration == device.vertex_shader &&
		shadow->render_target == device.render_target && shadow->depth_stencil == device.depth_stencil &&
		shadow->visibility_test_active == device.visibility_test_active &&
		shadow->visibility_index == device.visibility_index && shadow->immediate == immediate &&
		shadow->ui_offset == (unsigned long)ui_offset &&
		!memcmp(shadow->textures, device.textures, sizeof(shadow->textures)) &&
		!memcmp(shadow->palettes, device.palettes, sizeof(shadow->palettes)) &&
		!memcmp(&shadow->viewport, &device.viewport, sizeof(device.viewport)) &&
		!memcmp(shadow->viewport_scale, device.viewport_scale, sizeof(shadow->viewport_scale)) &&
		!memcmp(shadow->viewport_offset, device.viewport_offset, sizeof(shadow->viewport_offset)) &&
		shadow_states_match(shadow);
}
static struct render_command *record_previous;

static void record_shadow_fields(struct vertex_shader_object *program, BOOL immediate)
{
	memcpy(record_shadow.textures, device.textures, sizeof(record_shadow.textures));
	memcpy(record_shadow.palettes, device.palettes, sizeof(record_shadow.palettes));
	record_shadow.program = program;
	record_shadow.declaration = device.vertex_shader;
	record_shadow.render_target = device.render_target;
	record_shadow.depth_stencil = device.depth_stencil;
	record_shadow.viewport = device.viewport;
	memcpy(record_shadow.viewport_scale, device.viewport_scale, sizeof(record_shadow.viewport_scale));
	memcpy(record_shadow.viewport_offset, device.viewport_offset, sizeof(record_shadow.viewport_offset));
	record_shadow.visibility_test_active = device.visibility_test_active;
	record_shadow.visibility_index = device.visibility_index;
	record_shadow.immediate = immediate;
	record_shadow.ui_offset = (unsigned long)ui_offset;
}

static BOOL record_state_unchanged(struct vertex_shader_object *program, BOOL immediate)
{
	if (!record_previous)
		return FALSE;
	if (memcmp(record_shadow.textures, device.textures, sizeof(record_shadow.textures)) ||
		memcmp(record_shadow.palettes, device.palettes, sizeof(record_shadow.palettes)) ||
		record_shadow.program != program || record_shadow.declaration != device.vertex_shader ||
		record_shadow.render_target != device.render_target || record_shadow.depth_stencil != device.depth_stencil ||
		memcmp(&record_shadow.viewport, &device.viewport, sizeof(device.viewport)) ||
		memcmp(record_shadow.viewport_scale, device.viewport_scale, sizeof(record_shadow.viewport_scale)) ||
		memcmp(record_shadow.viewport_offset, device.viewport_offset, sizeof(record_shadow.viewport_offset)) ||
		record_shadow.visibility_test_active != device.visibility_test_active ||
		record_shadow.visibility_index != device.visibility_index || record_shadow.immediate != immediate ||
		record_shadow.ui_offset != (unsigned long)ui_offset)
		return FALSE;
	return !memcmp(record_shadow.render_state, D3D__RenderState, sizeof(record_shadow.render_state)) &&
		!memcmp(record_shadow.texture_state, D3D__TextureState, sizeof(record_shadow.texture_state));
}

/* the split records' state blocks: three per-frame arenas (the game
records frame N+1 while the worker executes frame N), rotated at Present;
a full arena falls back to records built in full */
#define STATE_BLOCKS_PER_FRAME 2048
static struct record_state *state_arenas[3];
static struct record_material *material_arenas[3];
static struct record_values *values_arenas[3];
static unsigned long state_arena_index, state_blocks_used, material_blocks_used, values_blocks_used;
static struct record_state *state_last;

static int record_split_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		const char *setting = getenv("HALO_RECORD_SPLIT");
		int index;

		enabled = !setting || atoi(setting) != 0;
		for (index = 0; enabled && index < 3; index++)
		{
			state_arenas[index] = malloc(STATE_BLOCKS_PER_FRAME * sizeof(struct record_state));
			material_arenas[index] = malloc(STATE_BLOCKS_PER_FRAME * sizeof(struct record_material));
			values_arenas[index] = malloc(STATE_BLOCKS_PER_FRAME * sizeof(struct record_values));
			if (!state_arenas[index] || !material_arenas[index] || !values_arenas[index])
				enabled = 0;
		}
		platform_log("draw records: %s", enabled ? "split (the worker translates the state)" : "built in full on the game's thread");
	}
	return enabled;
}

static void record_state_frame_end(void)
{
	device_state_dirty = STATE_DIRTY_MATERIAL | STATE_DIRTY_VALUES;
	state_arena_index = (state_arena_index + 1) % 3;
	state_blocks_used = 0;
	material_blocks_used = 0;
	values_blocks_used = 0;
	state_last = NULL;
	material_last = NULL;
	values_last = NULL;
}

static const struct record_state *record_state_current(void)
{
	DWORD headers[D3DTSS_MAXSTAGES][5];
	const D3DCOLOR *palette_data[D3DTSS_MAXSTAGES];
	struct record_state *block;
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		if (device.textures[stage])
			memcpy(headers[stage], device.textures[stage], sizeof(headers[stage]));
		else
			memset(headers[stage], 0, sizeof(headers[stage]));
		palette_data[stage] = device.palettes[stage] && device.palettes[stage]->Data ?
			(const D3DCOLOR *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.palettes[stage]->Data) : NULL;
	}
	/* the render and stage states: the last material while nothing of it
	changed, or one equal to it; else a new one (with the draw's values at
	0); likewise the values */
	if (!material_last || (device_state_dirty & STATE_DIRTY_MATERIAL))
	{
		if (!material_last || !material_matches_current(material_last))
		{
			int index, stage;

			if (material_blocks_used >= STATE_BLOCKS_PER_FRAME)
				return NULL;
			material_last = &material_arenas[state_arena_index][material_blocks_used++];
			memcpy(material_last->render_state, D3D__RenderState, sizeof(material_last->render_state));
			memcpy(material_last->texture_state, D3D__TextureState, sizeof(material_last->texture_state));
			for (index = 0; index < RECORD_VALUE_COUNT; index++)
				material_last->render_state[record_value_state[index]] = 0;
			for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
				memset(&material_last->texture_state[stage][D3DTSS_BUMPENVMAT00], 0, RECORD_VALUE_BUMP_COUNT * sizeof(DWORD));
			stats.material_new++;
		}
		else
			stats.state_equal++;
	}
	if (!values_last || (device_state_dirty & STATE_DIRTY_VALUES))
	{
		if (!values_last || !values_match_current(values_last))
		{
			int index, stage;

			if (values_blocks_used >= STATE_BLOCKS_PER_FRAME)
				return NULL;
			values_last = &values_arenas[state_arena_index][values_blocks_used++];
			for (index = 0; index < RECORD_VALUE_COUNT; index++)
				values_last->render_state[index] = D3D__RenderState[record_value_state[index]];
			for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
				memcpy(values_last->bump[stage], &D3D__TextureState[stage][D3DTSS_BUMPENVMAT00], sizeof(values_last->bump[stage]));
			stats.values_new++;
		}
	}
	device_state_dirty = 0;
	if (state_last && state_last->material == material_last && state_last->values == values_last &&
		!memcmp(state_last->textures, device.textures, sizeof(state_last->textures)) &&
		!memcmp(state_last->palettes, device.palettes, sizeof(state_last->palettes)) &&
		!memcmp(state_last->palette_data, palette_data, sizeof(palette_data)) &&
		!memcmp(state_last->texture_header, headers, sizeof(headers)))
	{
		stats.state_quick++;
		return state_last;
	}
	stats.state_new++;
	if (state_blocks_used >= STATE_BLOCKS_PER_FRAME)
		return NULL;
	block = &state_arenas[state_arena_index][state_blocks_used++];
	block->material = material_last;
	block->values = values_last;
	memcpy(block->textures, device.textures, sizeof(block->textures));
	memcpy(block->palettes, device.palettes, sizeof(block->palettes));
	memcpy(block->palette_data, palette_data, sizeof(palette_data));
	memcpy(block->texture_header, headers, sizeof(headers));
	state_last = block;
	return block;
}

/* records everything but the vertex data and the primitives; NULL when the
draw cannot be made */
static struct render_command *record_draw(BOOL immediate)
{
	unsigned long long profile_from;

	draw_sampled = draw_profile_on() && ++draw_counter % (unsigned long)draw_profile == 0;
	profile_from = draw_sampled ? vita_host_time_us() : 0;
	int computed_stage_draw = 0;
	struct vertex_shader_object *program = current_program();
	struct vertex_shader_object *declaration = device.vertex_shader;
	struct render_command *command;
	struct vgxm_draw *draw;
	struct nv2a_pixel_shader_key *key;
	DWORD *rs = D3D__RenderState;
	BOOL has_depth;
	int stage;

	if (!device.gpu_ready || !program || !declaration || !program->instructions)
	{
		stats.skipped_no_program++;
		return NULL;
	}
	if (!device.render_target && !device.depth_stencil)
	{
		stats.skipped_no_target++;
		return NULL;
	}
	command = command_begin(_command_draw);
	if (!command)
		return NULL;
	DRAW_FINE_ADD(0, profile_from);
	draw = &command->draw;
	/* (not the whole draw, 478 bytes into a cold ring entry: every field is
	set before it is read - the layout grows from these counts, the worker
	sets the programs, textures, states and fragment uniforms of a split
	record, the full record below sets them here - and the attribute and
	stream arrays are read up to their counts) */
	draw->attribute_count = 0;
	draw->stream_count = 0;
	draw->vertex_chunk_d_registers = 0;
	has_depth = command->depth_valid && surface_is_depth_cached(&command->depth_surface);
	{
		/* HALO_RECORD_SHORTCUT=1 turns the same-state shortcut on (opt-in:
		a run with it froze the Vita at 110 s, cause unknown) */
		static int shortcut = -1;

		if (shortcut < 0)
		{
			const char *setting = getenv("HALO_RECORD_SHORTCUT");
			shortcut = setting && atoi(setting) != 0;
		}
		if (!shortcut)
			record_previous = NULL;
	}
	if (record_state_unchanged(program, immediate) && record_previous != command)
	{
		const struct render_command *previous = record_previous;

		memcpy(&command->program, &previous->program,
			offsetof(struct render_command, sampler_state) + sizeof(command->sampler_state) - offsetof(struct render_command, program));
		memcpy(&draw->depth_test, &previous->draw.depth_test,
			offsetof(struct vgxm_draw, clip) + sizeof(draw->clip) - offsetof(struct vgxm_draw, depth_test));
		command->hoistable = command->hoistable && previous->hoistable;
		simple_fragment = 0;
		draw->fragment_uniforms[0] = device.fragment_snapshot[0];
		draw->fragment_uniforms[1] = device.fragment_snapshot[1];
		draw->vertex_uniforms = vertex_uniforms_snapshot(immediate);
		if (!draw->fragment_uniforms[0] || !draw->fragment_uniforms[1] || !draw->vertex_uniforms ||
			!constants_snapshot(program, draw))
		{
			record_previous = NULL;
			return NULL;
		}
		draw->visibility_index = device.visibility_test_active ? device.visibility_index : 0;
		if (immediate)
			{ stats.immediate_draws++; draw_counter_immediate++; }
		else
			{ stats.draws++; draw_counter_stream++; }
		stats.same_state_draws++;
		if (draw_sampled)
			draw_profile_draws++;
		record_previous = command;
		return command;
	}
	DRAW_PROFILE_ADD(0, profile_from);
	if (record_split_enabled())
	{
		const struct record_state *state = record_state_current();

		DRAW_PROFILE_ADD(1, profile_from);
		if (state)
		{
			command->state = state;
			command->has_depth = (unsigned char)(has_depth != 0);
			command->simple = (unsigned char)simple_fragment;
			simple_fragment = 0;
			command->program = program;
			command->immediate = immediate;
			command->provided_mask = declaration->provided_mask;
			command->packed_mask = declaration->packed_mask;
			command->color_mask = declaration->color_mask;
			for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
			{
				D3DBaseTexture *texture = device.textures[stage];

				command->texture_version[stage] = 0;
				if (texture)
				{
					unsigned long index;

					for (index = 0; index < target_version_count; index++)
						if (target_versions[index].data == texture->Data)
							command->texture_version[stage] = target_versions[index].version;
					if (command->hoistable && !command->texture_version[stage] && render_target_entry_find(texture->Data))
						command->hoistable = FALSE;
				}
			}
			DRAW_PROFILE_ADD(2, profile_from);
			draw->vertex_uniforms = vertex_uniforms_snapshot(immediate);
			if (!draw->vertex_uniforms || !constants_snapshot(program, draw))
				return NULL;
			DRAW_PROFILE_ADD(8, profile_from);
			draw->viewport_scale[0] = device.viewport_scale[0] != 0.0f ? device.viewport_scale[0] : 1.0f;
			draw->viewport_scale[1] = device.viewport_scale[1] != 0.0f ? device.viewport_scale[1] : 1.0f;
			draw->viewport_scale[2] = device.viewport.MaxZ - device.viewport.MinZ;
			draw->viewport_offset[0] = device.viewport_offset[0];
			draw->viewport_offset[1] = device.viewport_offset[1];
			draw->viewport_offset[2] = device.viewport.MinZ;
			draw->clip[0] = (long)device.viewport.X;
			draw->clip[1] = (long)device.viewport.Y;
			draw->clip[2] = (long)(device.viewport.X + device.viewport.Width);
			draw->clip[3] = (long)(device.viewport.Y + device.viewport.Height);
			draw->visibility_index = device.visibility_test_active ? device.visibility_index : 0;
			if (immediate)
				{ stats.immediate_draws++; draw_counter_immediate++; }
			else
				{ stats.draws++; draw_counter_stream++; }
			if (draw_sampled)
				draw_profile_draws++;
			DRAW_PROFILE_ADD(9, profile_from);
			return command;
		}
	}
	command->program = program;
	command->immediate = immediate;
	command->provided_mask = declaration->provided_mask;
	command->packed_mask = declaration->packed_mask;
	command->color_mask = declaration->color_mask;

	key = &command->key;
	memset(key, 0, sizeof(*key));
	memcpy(key->combiner_state, D3D__RenderState, sizeof(key->combiner_state));
	memset(&key->combiner_state[D3DRS_PSCONSTANT0_0], 0, 16 * sizeof(DWORD));
	key->combiner_state[D3DRS_PSFINALCOMBINERCONSTANT0] = 0;
	key->combiner_state[D3DRS_PSFINALCOMBINERCONSTANT1] = 0;
	key->texture_modes = D3D__RenderState[D3DRS_PSTEXTUREMODES];
	DRAW_FINE_ADD(1, profile_from);
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		D3DBaseTexture *texture = device.textures[stage];
		DWORD *state = D3D__TextureState[stage];

		key->alpha_kill[stage] = state[D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key->color_sign[stage] = (unsigned char)((state[D3DTSS_COLORSIGN] >> 28) & 0xf);
		command->texture_present[stage] = texture != NULL;
		command->texture_version[stage] = 0;
		if (texture)
		{
			unsigned long index;
			/* HALO_RAW_TEXCOORDS=0 turns the varying-coordinate fetch off */
			static int raw_texcoords = -1;

			if (raw_texcoords < 0)
			{
				const char *setting = getenv("HALO_RAW_TEXCOORDS");
				raw_texcoords = !setting || atoi(setting) != 0;
			}
			memcpy(command->texture_header[stage], texture, sizeof(command->texture_header[stage]));
			if (raw_texcoords && stage_texture_mode(stage) == 1)
			{
				const struct xgpu_texture_description *description = stage_description(stage, texture);

				if (!description->linear && !description->cube_map)
				{
					if (!(program->texcoord_w_mask & (1UL << stage)))
						key->raw_coordinates |= (unsigned char)(1U << stage);
					else
					{
						/* the program writes the coordinate's w: a
						projective read of the varying (the divide is the
						iterator's, not the program's) */
						key->projective_coordinates |= (unsigned char)(1U << stage);
						computed_stage_draw = 1;
					}
				}
				else
					computed_stage_draw = 1;
			}
			else if (stage_texture_mode(stage) == 1)
				computed_stage_draw = 1;
			for (index = 0; index < target_version_count; index++)
			{
				if (target_versions[index].data == texture->Data)
					command->texture_version[stage] = target_versions[index].version;
			}
			/* reading a target that is not a copy (the main scene): the
			draw must stay in order */
			if (command->hoistable && !command->texture_version[stage] && render_target_entry_find(texture->Data))
				command->hoistable = FALSE;
		}
		command->palette[stage] = device.palettes[stage] && device.palettes[stage]->Data ?
			(const D3DCOLOR *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.palettes[stage]->Data) : NULL;
		command->sampler_state[stage][0] = state[D3DTSS_MINFILTER];
		command->sampler_state[stage][1] = state[D3DTSS_MAGFILTER];
		command->sampler_state[stage][2] = state[D3DTSS_MIPFILTER];
		command->sampler_state[stage][3] = state[D3DTSS_ADDRESSU];
		command->sampler_state[stage][4] = state[D3DTSS_ADDRESSV];
		command->sampler_state[stage][5] = state[D3DTSS_MIPMAPLODBIAS];
	}
	DRAW_FINE_ADD(2, profile_from);
	key->alpha_test_function = rs[D3DRS_ALPHATESTENABLE] ? rs[D3DRS_ALPHAFUNC] : 0;
	{
		/* HALO_SIMPLE_FRAG_MODE=1 (a constant) or 2 (textures, no combiners)
		for the HALO_SIMPLE_FRAG_BLENDS draws; HALO_SIMPLE_FRAG_ALL=1 puts
		every draw on that mode (the combiner arithmetic's share of the GPU) */
		static int simple_mode = -1, simple_all = -1;

		if (simple_mode < 0)
		{
			const char *setting = getenv("HALO_SIMPLE_FRAG_MODE"), *all = getenv("HALO_SIMPLE_FRAG_ALL");
			simple_mode = setting && atoi(setting) ? atoi(setting) : 1;
			simple_all = all && atoi(all) != 0;
		}
		key->pad = (simple_fragment || simple_all) ? simple_mode : 0;
	}
	simple_fragment = 0;
	{
		/* HALO_NO_ALPHA_TEST=1: no discard in any fragment program, to
		measure what the alpha test costs the GPU (it defeats early depth
		rejection) */
		static int no_alpha_test = -1;

		if (no_alpha_test < 0)
		{
			const char *setting = getenv("HALO_NO_ALPHA_TEST");
			no_alpha_test = setting && atoi(setting) != 0;
		}
		if (no_alpha_test)
			key->alpha_test_function = 0;
		/* a discard in a fragment program costs the tile renderer its
		early visibility for the whole draw (the alpha-tested blended
		passes were most of the GPU's frame). It is dropped where it
		cannot change the image: a source-alpha blend (SRCALPHA with
		INVSRCALPHA or ONE) whose test only rejects fragments of zero
		alpha (GREATER 0, or GREATEREQUAL 1 - the same in 8 bits) - such a
		fragment blends to no change - with no depth or stencil write,
		which the discard would otherwise have prevented */
		else if (key->alpha_test_function && rs[D3DRS_ALPHABLENDENABLE] && rs[D3DRS_SRCBLEND] == D3DBLEND_SRCALPHA &&
			(rs[D3DRS_DESTBLEND] == D3DBLEND_INVSRCALPHA || rs[D3DRS_DESTBLEND] == D3DBLEND_ONE) &&
			!(has_depth && rs[D3DRS_ZENABLE] && rs[D3DRS_ZWRITEENABLE]) &&
			!(has_depth && rs[D3DRS_STENCILENABLE] && (rs[D3DRS_STENCILPASS] != D3DSTENCILOP_KEEP ||
				rs[D3DRS_STENCILFAIL] != D3DSTENCILOP_KEEP || rs[D3DRS_STENCILZFAIL] != D3DSTENCILOP_KEEP)) &&
			((key->alpha_test_function == D3DCMP_GREATER && (rs[D3DRS_ALPHAREF] & 0xff) == 0) ||
				(key->alpha_test_function == D3DCMP_GREATEREQUAL && (rs[D3DRS_ALPHAREF] & 0xff) <= 1)))
		{
			static int dropped_alpha_tests = -1;

			if (dropped_alpha_tests < 0)
			{
				const char *setting = getenv("HALO_KEEP_ALPHA_TEST");
				dropped_alpha_tests = !(setting && atoi(setting) != 0);
			}
			if (dropped_alpha_tests)
			{
				key->alpha_test_function = 0;
				stats.dropped_alpha_tests++;
			}
		}
		if (key->alpha_test_function)
			stats.alpha_tested_draws++;
	}
	key->fog_enable = rs[D3DRS_FOGENABLE] != 0;
	key->fog_table_mode = (unsigned char)rs[D3DRS_FOGTABLEMODE];
	DRAW_PROFILE_ADD(0, profile_from);

	fragment_uniforms_update();
	draw->fragment_uniforms[0] = device.fragment_snapshot[0];
	draw->fragment_uniforms[1] = device.fragment_snapshot[1];
	DRAW_PROFILE_ADD(1, profile_from);
	draw->vertex_uniforms = vertex_uniforms_snapshot(immediate);
	if (!draw->fragment_uniforms[0] || !draw->fragment_uniforms[1] || !draw->vertex_uniforms || !constants_snapshot(program, draw))
	{
		record_previous = NULL;
		return NULL;
	}
	DRAW_PROFILE_ADD(8, profile_from);

	draw->depth_test = has_depth && rs[D3DRS_ZENABLE];
	draw->depth_write = draw->depth_test && rs[D3DRS_ZWRITEENABLE];
	draw->depth_function = rs[D3DRS_ZFUNC];
	draw->stencil_test = has_depth && rs[D3DRS_STENCILENABLE];
	draw->stencil_function = rs[D3DRS_STENCILFUNC];
	draw->stencil_reference = rs[D3DRS_STENCILREF];
	draw->stencil_read_mask = rs[D3DRS_STENCILMASK];
	draw->stencil_write_mask = rs[D3DRS_STENCILWRITEMASK];
	draw->stencil_fail = rs[D3DRS_STENCILFAIL];
	draw->stencil_depth_fail = rs[D3DRS_STENCILZFAIL];
	draw->stencil_pass = rs[D3DRS_STENCILPASS];
	draw->blend = rs[D3DRS_ALPHABLENDENABLE] != 0;
	draw->blend_source = rs[D3DRS_SRCBLEND];
	draw->blend_destination = rs[D3DRS_DESTBLEND];
	draw->blend_operation = rs[D3DRS_BLENDOP];
	draw->color_write = rs[D3DRS_COLORWRITEENABLE];
	/* the cull mode names the screen winding to discard */
	draw->cull = rs[D3DRS_CULLMODE] == D3DCULL_NONE ? 0 : rs[D3DRS_CULLMODE];
	draw->depth_bias_slope = draw->depth_bias_units = 0.0f;
	if (rs[D3DRS_SOLIDOFFSETENABLE])
	{
		draw->depth_bias_slope = dword_to_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]);
		draw->depth_bias_units = dword_to_float(rs[D3DRS_POLYGONOFFSETZOFFSET]);
	}
	draw->viewport_scale[0] = device.viewport_scale[0] != 0.0f ? device.viewport_scale[0] : 1.0f;
	draw->viewport_scale[1] = device.viewport_scale[1] != 0.0f ? device.viewport_scale[1] : 1.0f;
	draw->viewport_scale[2] = device.viewport.MaxZ - device.viewport.MinZ;
	draw->viewport_offset[0] = device.viewport_offset[0];
	draw->viewport_offset[1] = device.viewport_offset[1];
	draw->viewport_offset[2] = device.viewport.MinZ;
	draw->clip[0] = (long)device.viewport.X;
	draw->clip[1] = (long)device.viewport.Y;
	draw->clip[2] = (long)(device.viewport.X + device.viewport.Width);
	draw->clip[3] = (long)(device.viewport.Y + device.viewport.Height);
	draw->visibility_index = device.visibility_test_active ? device.visibility_index : 0;
	if (immediate)
		{ stats.immediate_draws++; draw_counter_immediate++; }
	else
		{ stats.draws++; draw_counter_stream++; }
	memcpy(record_shadow.render_state, D3D__RenderState, sizeof(record_shadow.render_state));
	memcpy(record_shadow.texture_state, D3D__TextureState, sizeof(record_shadow.texture_state));
	record_shadow_fields(program, immediate);
	record_previous = command;
	DRAW_PROFILE_ADD(2, profile_from);
	if (draw_sampled)
		draw_profile_draws++;
	if (computed_stage_draw)
	{
		/* the draws whose 2D fetches still take computed coordinates
		(linear textures, or the program writes the coordinate's w),
		counted per frame and named once each (key hash, program) */
		static unsigned long named[24];
		static int named_count;
		unsigned long hash = hash_words(key, sizeof(*key));
		int index;

		stats.computed_draws++;
		for (index = 0; index < named_count; index++)
			if (named[index] == hash)
				break;
		if (index == named_count && named_count < 24)
		{
			named[named_count++] = hash;
			platform_log("computed-coordinate draw: ps %08lx (vs id %lu w-mask %lx, modes %08lx, projective %x, textures %s%s%s%s)",
				hash, program->id, program->texcoord_w_mask, (unsigned long)key->texture_modes, key->projective_coordinates,
				command->texture_present[0] ? "0" : "", command->texture_present[1] ? "1" : "",
				command->texture_present[2] ? "2" : "", command->texture_present[3] ? "3" : "");
		}
	}
	return command;
}

/* ---------- vertex data */

static void attribute_format(unsigned long type, unsigned char *format, unsigned char *components)
{
	switch (type)
	{
	case D3DVSDT_FLOAT1: *format = _vgxm_attribute_f32; *components = 1; break;
	case D3DVSDT_FLOAT2: *format = _vgxm_attribute_f32; *components = 2; break;
	case D3DVSDT_FLOAT3: case D3DVSDT_FLOAT2H: *format = _vgxm_attribute_f32; *components = 3; break;
	case D3DVSDT_FLOAT4: *format = _vgxm_attribute_f32; *components = 4; break;
	case D3DVSDT_D3DCOLOR: *format = _vgxm_attribute_u8n; *components = 4; break;
	case D3DVSDT_SHORT1: *format = _vgxm_attribute_s16; *components = 1; break;
	case D3DVSDT_SHORT2: *format = _vgxm_attribute_s16; *components = 2; break;
	case D3DVSDT_SHORT3: *format = _vgxm_attribute_s16; *components = 3; break;
	case D3DVSDT_SHORT4: *format = _vgxm_attribute_s16; *components = 4; break;
	case D3DVSDT_NORMSHORT1: *format = _vgxm_attribute_s16n; *components = 1; break;
	case D3DVSDT_NORMSHORT2: *format = _vgxm_attribute_s16n; *components = 2; break;
	case D3DVSDT_NORMSHORT3: *format = _vgxm_attribute_s16n; *components = 3; break;
	case D3DVSDT_NORMSHORT4: *format = _vgxm_attribute_s16n; *components = 4; break;
	case D3DVSDT_NORMPACKED3: *format = _vgxm_attribute_u8; *components = 4; break;
	case D3DVSDT_PBYTE1: *format = _vgxm_attribute_u8n; *components = 1; break;
	case D3DVSDT_PBYTE2: *format = _vgxm_attribute_u8n; *components = 2; break;
	case D3DVSDT_PBYTE3: *format = _vgxm_attribute_u8n; *components = 3; break;
	case D3DVSDT_PBYTE4: *format = _vgxm_attribute_u8n; *components = 4; break;
	default: *format = _vgxm_attribute_f32; *components = 4; break;
	}
}

/* the declaration's attributes and streams for vertices [first, first +
count) of each stream, where index i of the draw reads vertex first + i;
streams outside the loaded map are copied into the ring. FALSE if the ring
is full. */
static BOOL setup_streams(struct vgxm_draw *draw, unsigned long first, unsigned long count)
{
	struct vertex_shader_object *declaration = device.vertex_shader;
	unsigned long stream_slot[16];
	unsigned long index;

	for (index = 0; index < 16; index++)
		stream_slot[index] = ~0UL;
	for (index = 0; index < declaration->element_count; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];
		unsigned long stream = element->stream;
		unsigned long stride = device.streams[stream].stride;
		struct vgxm_attribute *attribute;

		if (element->type == D3DVSDT_NONE || !device.streams[stream].data)
			continue;
		if (stream_slot[stream] == ~0UL)
		{
			const unsigned char *base = (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data);
			unsigned long bytes = stride ? stride * count : 64;
			const unsigned char *start = base + first * stride;

			stream_slot[stream] = draw->stream_count++;
			draw->strides[stream_slot[stream]] = stride;
			if (memory_is_static(start, bytes))
			{
				stats.direct_bytes += bytes;
				draw->streams[stream_slot[stream]] = start;
			}
			else
			{
				stats.copied_streams += bytes;
				draw->streams[stream_slot[stream]] = ring_copy(start, bytes);
				if (!draw->streams[stream_slot[stream]])
					return FALSE;
			}
		}
		attribute = &draw->attributes[draw->attribute_count++];
		attribute->reg = element->reg;
		attribute->stream = (unsigned char)stream_slot[stream];
		attribute->offset = element->offset;
		attribute_format(element->type, &attribute->format, &attribute->components);
	}
	return TRUE;
}

static unsigned long gxm_primitive(D3DPRIMITIVETYPE type)
{
	switch (type)
	{
	case D3DPT_QUADSTRIP: return D3DPT_TRIANGLESTRIP;
	case D3DPT_POLYGON: return D3DPT_TRIANGLEFAN;
	default: return (unsigned long)type;
	}
}

/* indices a primitive GXM lacks is drawn with, in the ring: quads as two
triangles each, line strips and loops as lines; NULL for the others */
static unsigned short *converted_indices(D3DPRIMITIVETYPE type, const unsigned short *indices, unsigned long count,
	unsigned long *out_count, unsigned long *out_primitive)
{
	unsigned short *result;
	unsigned long index;

#define SOURCE(i) ((unsigned short)(indices ? indices[i] : (i)))
	switch (type)
	{
	case D3DPT_QUADLIST:
	{
		unsigned long quads = count / 4, quad;

		result = vgxm_ring_alloc(quads * 6 * sizeof(unsigned short) + 4, 16);
		if (!result)
			return NULL;
		for (quad = 0; quad < quads; quad++)
		{
			result[quad * 6 + 0] = SOURCE(quad * 4);
			result[quad * 6 + 1] = SOURCE(quad * 4 + 1);
			result[quad * 6 + 2] = SOURCE(quad * 4 + 2);
			result[quad * 6 + 3] = SOURCE(quad * 4);
			result[quad * 6 + 4] = SOURCE(quad * 4 + 2);
			result[quad * 6 + 5] = SOURCE(quad * 4 + 3);
		}
		*out_count = quads * 6;
		*out_primitive = D3DPT_TRIANGLELIST;
		return result;
	}
	case D3DPT_LINESTRIP:
	case D3DPT_LINELOOP:
	{
		unsigned long segments = count < 2 ? 0 : type == D3DPT_LINELOOP ? count : count - 1;

		result = vgxm_ring_alloc(segments * 2 * sizeof(unsigned short) + 4, 16);
		if (!result)
			return NULL;
		for (index = 0; index < segments; index++)
		{
			result[index * 2] = SOURCE(index);
			result[index * 2 + 1] = SOURCE((index + 1) % count);
		}
		*out_count = segments * 2;
		*out_primitive = D3DPT_LINELIST;
		return result;
	}
	default:
		return NULL;
	}
#undef SOURCE
}

static BOOL needs_conversion(D3DPRIMITIVETYPE type)
{
	return type == D3DPT_QUADLIST || type == D3DPT_LINESTRIP || type == D3DPT_LINELOOP;
}

void WINAPI D3DDevice_SetStreamSource(UINT stream_number, D3DVertexBuffer *stream_data, UINT stride)
{
	if (stream_number >= 16)
		return;
	device.streams[stream_number].data = stream_data ? stream_data->Data : 0;
	device.streams[stream_number].stride = stride;
}

void WINAPI D3DDevice_SetIndices(D3DIndexBuffer *index_data, UINT base_vertex_index)
{
	device.base_vertex_index = base_vertex_index;
	D3D__IndexData = index_data ? (WORD *)index_data->Data : NULL;
}

static void draw_vertices(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT vertex_count)
{
	struct render_command *command;
	struct vgxm_draw *draw;
	unsigned long long profile_from;

	if (!vertex_count || vertex_count > 65536 || !(command = record_draw(FALSE)))
		return;
	profile_from = DRAW_PROFILE_NOW();
	draw = &command->draw;
	if (!setup_streams(draw, start_vertex, vertex_count))
		return;
	if (needs_conversion(primitive_type))
	{
		draw->indices = converted_indices(primitive_type, NULL, vertex_count, &draw->index_count, &draw->primitive);
		if (!draw->indices)
			return;
	}
	else
	{
		draw->primitive = gxm_primitive(primitive_type);
		draw->indices = device.sequential_indices;
		draw->index_count = vertex_count;
	}
	DRAW_PROFILE_ADD(3, profile_from);
	command_commit(command);
}

void WINAPI D3DDevice_DrawVertices(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT vertex_count)
{
	layer_enter();
	draw_vertices(primitive_type, start_vertex, vertex_count);
	layer_leave();
}

/* the smallest and largest index of the draw */
static void index_extent(const WORD *indices, unsigned long count, unsigned long *minimum, unsigned long *maximum)
{
	unsigned long index, low = 0xffff, high = 0;

	for (index = 0; index < count; index++)
	{
		if (indices[index] < low)
			low = indices[index];
		if (indices[index] > high)
			high = indices[index];
	}
	*minimum = low;
	*maximum = high;
}

static void draw_indexed_vertices(D3DPRIMITIVETYPE primitive_type, UINT vertex_count, CONST WORD *index_data)
{
	struct render_command *command;
	struct vgxm_draw *draw;
	unsigned long minimum, maximum, stream;
	BOOL streams_static = TRUE;
	struct vertex_shader_object *declaration;

	unsigned long long profile_from;

	if (!vertex_count || !index_data || !(command = record_draw(FALSE)))
		return;
	profile_from = DRAW_PROFILE_NOW();
	draw = &command->draw;
	declaration = device.vertex_shader;
	for (stream = 0; stream < declaration->element_count; stream++)
	{
		const struct vertex_element *element = &declaration->elements[stream];
		const unsigned char *base;

		if (element->type == D3DVSDT_NONE || !device.streams[element->stream].data)
			continue;
		base = (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[element->stream].data);
		if (!memory_is_static(base, 1))
			streams_static = FALSE;
	}
	if (streams_static)
	{
		minimum = 0;
		maximum = 0;
		if (!setup_streams(draw, device.base_vertex_index, 1))
			return;
	}
	else
	{
		index_extent(index_data, vertex_count, &minimum, &maximum);
		if (!setup_streams(draw, device.base_vertex_index + minimum, maximum - minimum + 1))
			return;
		for (stream = 0; stream < draw->stream_count; stream++)
			draw->streams[stream] = (const unsigned char *)draw->streams[stream] - minimum * draw->strides[stream];
	}
	if (needs_conversion(primitive_type))
	{
		draw->indices = converted_indices(primitive_type, index_data, vertex_count, &draw->index_count, &draw->primitive);
		if (!draw->indices)
			return;
	}
	else
	{
		draw->primitive = gxm_primitive(primitive_type);
		draw->index_count = vertex_count;
		if (memory_is_static(index_data, vertex_count * sizeof(WORD)))
		{
			draw->indices = index_data;
		}
		else
		{
			draw->indices = ring_copy(index_data, vertex_count * sizeof(WORD));
			stats.copied_indices += vertex_count * sizeof(WORD);
			if (!draw->indices)
				return;
		}
	}
	DRAW_PROFILE_ADD(3, profile_from);
	command_commit(command);
	DRAW_PROFILE_ADD(10, profile_from);
}

void WINAPI D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE primitive_type, UINT vertex_count, CONST WORD *index_data)
{
	layer_enter();
	draw_indexed_vertices(primitive_type, vertex_count, index_data);
	layer_leave();
}

/* ---------- immediate mode */

/* the input registers an immediate draw's vertices carry: those the
program reads (its other inputs come from the uniform buffer, unread); a
vertex's 16 registers would be 256 bytes into uncached memory */
static unsigned long immediate_input_mask(const struct vertex_shader_object *program)
{
	unsigned long mask = program ? program->input_mask & ((1UL << XGPU_VERTEX_ATTRIBUTE_COUNT) - 1) : 0;
	/* HALO_IMMEDIATE_PACK=0: every register, as before the packing */
	static int pack = -1;

	if (pack < 0)
	{
		const char *setting = getenv("HALO_IMMEDIATE_PACK");
		pack = !setting || atoi(setting) != 0;
	}
	if (!pack || !program)
		mask = (1UL << XGPU_VERTEX_ATTRIBUTE_COUNT) - 1;
	return mask ? mask : 1;
}

void WINAPI D3DDevice_Begin(D3DPRIMITIVETYPE primitive_type)
{
	unsigned long mask, count = 0;

	device.immediate_active = TRUE;
	device.immediate_type = primitive_type;
	device.immediate_count = 0;
	device.immediate_mask = mask = immediate_input_mask(current_program());
	for (; mask; mask &= mask - 1)
		count++;
	device.immediate_floats = count * 4;
}

static void immediate_emit(void)
{
	unsigned long floats = device.immediate_floats, mask;
	float *out;
	int reg;

	if (device.immediate_count == device.immediate_capacity)
	{
		device.immediate_capacity = device.immediate_capacity ? device.immediate_capacity * 2 : 256;
		device.immediate_vertices = realloc(device.immediate_vertices,
			device.immediate_capacity * XGPU_VERTEX_ATTRIBUTE_COUNT * 4 * sizeof(float));
	}
	/* (the registers the draw carries, packed in register order: the draw's
	attribute order, immediate_end) */
	out = device.immediate_vertices + device.immediate_count * floats;
	for (mask = device.immediate_mask, reg = 0; mask; mask >>= 1, reg++)
	{
		if (mask & 1)
		{
			memcpy(out, device.vertex_uniforms[VITA_VM_ATTRIBUTES + reg], 4 * sizeof(float));
			out += 4;
		}
	}
	device.immediate_count++;
}

static int immediate_merge_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		const char *setting = getenv("HALO_IMMEDIATE_MERGE");
		enabled = !setting || atoi(setting) != 0;
	}
	return enabled;
}

/* the vertices, as the draw carries them (emitted packed already) */
static void immediate_pack(const struct vgxm_draw *draw, unsigned long first, unsigned long count, float *packed)
{
	(void)draw;
	memcpy(packed, device.immediate_vertices + first * device.immediate_floats, count * device.immediate_floats * sizeof(float));
}

static int immediate_hold_room(unsigned long floats)
{
	if (floats <= held_immediate.capacity)
		return 1;
	held_immediate.capacity = floats > held_immediate.capacity * 2 ? floats : held_immediate.capacity * 2;
	held_immediate.vertices = realloc(held_immediate.vertices, held_immediate.capacity * sizeof(float));
	return held_immediate.vertices != NULL;
}

static void immediate_end(void)
{
	unsigned long index, count = device.immediate_count;
	D3DPRIMITIVETYPE type = device.immediate_type;
	struct render_command *command;
	struct vgxm_draw *draw;
	/* the vertices carry only the input registers the program reads (its
	other inputs come from the uniform buffer, unread): a vertex's 16
	registers would be 256 bytes into uncached memory */
	unsigned long mask, stride;

	device.immediate_active = FALSE;
	if (!count || count > 65536)
		return;
	if (held_immediate.command && gpu_stats_enabled())
	{
		/* (why the draw before could not take this one) */
		if (!immediate_triangle_family(type) || !held_immediate.triangles) merge_rejected[0]++;
		else if (0) merge_rejected[1]++;
		else if (held_immediate.constants != constant_generation) merge_rejected[2]++;
		else if (!shadow_matches(&held_shadow, current_program(), TRUE)) merge_rejected[4]++;
	}
	if (held_immediate.command && held_immediate.triangles && immediate_triangle_family(type) && immediate_merge_enabled() &&
		held_immediate.constants == constant_generation && held_immediate.count + count <= 65536 &&
		shadow_matches(&held_shadow, current_program(), TRUE))
	{
		draw = &held_immediate.command->draw;
		if (immediate_hold_room((held_immediate.count + count) * (held_immediate.stride / sizeof(float))) &&
			immediate_hold_triangles(type, held_immediate.count, count))
		{
			immediate_pack(draw, 0, count, held_immediate.vertices + held_immediate.count * (held_immediate.stride / sizeof(float)));
			held_immediate.count += count;
			merged_immediate_draws++;
			return;
		}
	}
	immediate_commit_held();
	if (!(command = record_draw(TRUE)))
		return;
	draw = &command->draw;
	/* (the registers the vertices were gathered with at Begin: the program
	cannot change between Begin and End) */
	mask = device.immediate_mask;
	if (mask != immediate_input_mask(command->program))
	{
		static int warned;

		if (!warned++)
			platform_log("immediate draw: the vertex program changed between Begin and End");
	}
	draw->attribute_count = 0;
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
	{
		struct vgxm_attribute *attribute;

		if (!(mask & (1UL << index)))
			continue;
		attribute = &draw->attributes[draw->attribute_count];
		attribute->reg = (unsigned char)index;
		attribute->format = _vgxm_attribute_f32;
		attribute->components = 4;
		attribute->stream = 0;
		attribute->offset = (unsigned short)(draw->attribute_count * 4 * sizeof(float));
		draw->attribute_count++;
	}
	stride = draw->attribute_count * 4 * sizeof(float);
	draw->stream_count = 1;
	draw->strides[0] = stride;
	command->provided_mask = mask;
	/* held, not yet committed: the next immediate draw may join it */
	if (!immediate_hold_room(count * (stride / sizeof(float))))
		return;
	immediate_pack(draw, 0, count, held_immediate.vertices);
	held_immediate.command = command;
	held_immediate.type = type;
	held_immediate.count = count;
	held_immediate.stride = stride;
	held_immediate.constants = constant_generation;
	held_immediate.index_count = 0;
	shadow_capture(&held_shadow, current_program(), TRUE, command->state);
	held_immediate.triangles = immediate_triangle_family(type) && immediate_merge_enabled() &&
		immediate_hold_triangles(type, 0, count);
	if (!immediate_merge_enabled())
		immediate_commit_held();
}

void WINAPI D3DDevice_End(void)
{
	layer_enter();
	immediate_end();
	layer_leave();
}

static void set_attribute(INT reg, float a, float b, float c, float d)
{
	BOOL emit = FALSE;
	float *value;

	if (reg == D3DVSDE_VERTEX)
	{
		reg = 0;
		emit = TRUE;
	}
	if (reg < 0 || reg >= XGPU_VERTEX_ATTRIBUTE_COUNT)
		return;
	value = device.vertex_uniforms[VITA_VM_ATTRIBUTES + reg];
	if (value[0] != a || value[1] != b || value[2] != c || value[3] != d)
	{
		value[0] = a;
		value[1] = b;
		value[2] = c;
		value[3] = d;
		device.vertex_attributes_changed = TRUE;
	}
	if (device.immediate_active && (emit || reg == 0))
		immediate_emit();
}

void WINAPI D3DDevice_SetVertexData2f(INT reg, FLOAT a, FLOAT b)
{
	set_attribute(reg, a, b, 0.0f, 1.0f);
}

void WINAPI D3DDevice_SetVertexData4f(INT reg, FLOAT a, FLOAT b, FLOAT c, FLOAT d)
{
	set_attribute(reg, a, b, c, d);
}

void WINAPI D3DDevice_SetVertexData2s(INT reg, SHORT a, SHORT b)
{
	set_attribute(reg, (float)a, (float)b, 0.0f, 1.0f);
}

void WINAPI D3DDevice_SetVertexData4ub(INT reg, BYTE a, BYTE b, BYTE c, BYTE d)
{
	set_attribute(reg, a / 255.0f, b / 255.0f, c / 255.0f, d / 255.0f);
}

void WINAPI D3DDevice_SetVertexDataColor(INT reg, D3DCOLOR color)
{
	float value[4];

	color_to_vec4(color, value);
	set_attribute(reg, value[0], value[1], value[2], value[3]);
}

/* ---------- clearing */

static void record_clear(unsigned long flags, D3DCOLOR color, float z, DWORD stencil, const long clip[4])
{
	struct render_command *command = command_begin(_command_clear);

	if (!command)
		return;
	command->clear_flags = flags;
	command->clear_color = color;
	command->clear_depth = z;
	command->clear_stencil = stencil;
	memcpy(command->clip, clip, sizeof(command->clip));
	command_commit(command);
}

static void clear(DWORD count, CONST D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	long clip[4];
	DWORD index;

	if (!device.gpu_ready || (!device.render_target && !device.depth_stencil))
		return;
	stats.clears++;
	if (!count || !rectangles)
	{
		clip[0] = (long)device.viewport.X;
		clip[1] = (long)device.viewport.Y;
		clip[2] = (long)(device.viewport.X + device.viewport.Width);
		clip[3] = (long)(device.viewport.Y + device.viewport.Height);
		record_clear(flags, color, z, stencil, clip);
		return;
	}
	for (index = 0; index < count; index++)
	{
		clip[0] = rectangles[index].x1 > (long)device.viewport.X ? rectangles[index].x1 : (long)device.viewport.X;
		clip[1] = rectangles[index].y1 > (long)device.viewport.Y ? rectangles[index].y1 : (long)device.viewport.Y;
		clip[2] = rectangles[index].x2 < (long)(device.viewport.X + device.viewport.Width) ?
			rectangles[index].x2 : (long)(device.viewport.X + device.viewport.Width);
		clip[3] = rectangles[index].y2 < (long)(device.viewport.Y + device.viewport.Height) ?
			rectangles[index].y2 : (long)(device.viewport.Y + device.viewport.Height);
		if (clip[0] >= clip[2] || clip[1] >= clip[3])
			continue;
		clip[0] += ui_offset;
		clip[2] += ui_offset;
		record_clear(flags, color, z, stencil, clip);
	}
}

void WINAPI D3DDevice_Clear(DWORD count, CONST D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	layer_enter();
	clear(count, rectangles, flags, color, z, stencil);
	layer_leave();
}

/* ---------- presentation */

static void write_screenshot_named(struct render_target_entry *target, const char *prefix)
{
	const char *directory = *config_string("debug.screenshot_directory") ?
		config_string("debug.screenshot_directory") : NULL;
	unsigned long width = target->target.width, height = target->target.height, pitch = 0, row, column;
	const unsigned char *pixels;
	unsigned char header[54] = { 'B', 'M' };
	unsigned long image_size = width * height * 4;
	unsigned char *line;
	char path[512];
	FILE *file;

	if (!directory)
		return;
	pixels = vgxm_target_pixels(target->id, &pitch);
	if (!pixels)
		return;
	snprintf(path, sizeof(path), "%s/%s%05lu.bmp", directory, prefix, device.frame);
	file = fopen(path, "wb");
	if (!file)
		return;
	*(unsigned int *)(header + 2) = (unsigned int)(54 + image_size);
	*(unsigned int *)(header + 10) = 54;
	*(unsigned int *)(header + 14) = 40;
	*(int *)(header + 18) = (int)width;
	*(int *)(header + 22) = -(int)height;
	*(unsigned short *)(header + 26) = 1;
	*(unsigned short *)(header + 28) = 32;
	*(unsigned int *)(header + 34) = (unsigned int)image_size;
	fwrite(header, 1, sizeof(header), file);
	line = malloc(width * 4);
	for (row = 0; row < height; row++)
	{
		memcpy(line, pixels + row * pitch, width * 4);
		for (column = 0; column < width; column++)
			line[column * 4 + 3] = 0xff;
		fwrite(line, 1, width * 4, file);
	}
	free(line);
	fclose(file);
	platform_log("screenshot %s", path);
}

static void write_screenshot(struct render_target_entry *target)
{
	struct render_target_entry *entry;

	write_screenshot_named(target, "frame");
	/* (debug) HALO_SCREENSHOT_CHAINS=1: with them, the chained targets'
	first levels (the water's bump map) */
	if (getenv("HALO_SCREENSHOT_CHAINS") && atoi(getenv("HALO_SCREENSHOT_CHAINS")))
		for (entry = render_targets; entry; entry = entry->next)
			if (entry->chain_levels > 1)
				write_screenshot_named(entry, "chain");
	/* (debug) HALO_SCREENSHOT_SMALL=1: every target smaller than the
	screen too (shadows, lights, the motion sensor), named by address */
	if (getenv("HALO_SCREENSHOT_SMALL") && atoi(getenv("HALO_SCREENSHOT_SMALL")))
		for (entry = render_targets; entry; entry = entry->next)
			if (entry->target.width < 256 && !entry->target.depth)
			{
				char prefix[32];

				snprintf(prefix, sizeof(prefix), "small%08lx_", entry->target.data);
				write_screenshot_named(entry, prefix);
			}
}

/* (debug) HALO_TRACE_AFTER_MS=n: from n ms of process time on, the game
thread, the worker, the tick thread and the texture cache log a line at
each step (to place a crash that leaves no dump) */
int halo_trace_active(void)
{
	static long after = -2;

	if (after == -2)
	{
		const char *setting = getenv("HALO_TRACE_AFTER_MS");
		after = setting ? atol(setting) : -1;
	}
	return after >= 0 && vita_host_time_us() / 1000 >= (unsigned long long)after;
}

void WINAPI D3DDevice_Present(CONST RECT *source_rectangle, CONST RECT *destination_rectangle,
	void *unused, void *unused2)
{
	static long screenshot_every = -1;

	(void)source_rectangle;
	(void)destination_rectangle;
	(void)unused;
	(void)unused2;
	if (screenshot_every < 0)
		screenshot_every = config_integer("debug.screenshot_every");
	{
		void vita_texture_locks_flush(void);

		vita_texture_locks_flush();
	}

	if (device.gpu_ready)
	{
		struct render_command *command;
		unsigned long long before;
		float frame_ms, tick_ms, render_ms;

		halo_frame_timing_recent(&frame_ms, &tick_ms, &render_ms);
		vgxm_overlay_set(frame_ms > 0.0f ? 1000.0f / frame_ms : 0.0f, tick_ms, render_ms);
		command = command_begin(_command_present);
		before = vita_host_time_us();

		if (command)
		{
			command->color_surface = device.back_buffer;
			command->color_valid = TRUE;
			command->depth_valid = FALSE;
			command->screenshot = screenshot_every > 0 && device.frame && device.frame % (unsigned long)screenshot_every == 0;
			{
				/* (debug) HALO_SCREENSHOT_FIRST / _LAST=n: only the frames
				from / up to n (a burst of every frame around one moment) */
				static long first = -2, last = -2;

				if (first == -2)
				{
					first = getenv("HALO_SCREENSHOT_FIRST") ? atol(getenv("HALO_SCREENSHOT_FIRST")) : -1;
					last = getenv("HALO_SCREENSHOT_LAST") ? atol(getenv("HALO_SCREENSHOT_LAST")) : -1;
				}
				if ((first >= 0 && device.frame < (unsigned long)first) || (last >= 0 && device.frame > (unsigned long)last))
					command->screenshot = FALSE;
			}
			command->frame = device.frame;
			frames_requested++;
			command_commit(command);
		}
		if (halo_trace_active())
			platform_log("trace: present %lu drain", device.frame);
		worker_drain();
		if (halo_trace_active())
			platform_log("trace: present %lu drained", device.frame);
		present_wait_time += vita_host_time_us() - before;
		/* the next frame's ring: every snapshot is written anew */
		vgxm_ring_next(device.frame + 1);
		device.vertex_uniform_snapshot = NULL;
		device.fragment_snapshot[0] = device.fragment_snapshot[1] = NULL;
		memset(device.chunk_snapshot, 0, sizeof(device.chunk_snapshot));
		record_previous = NULL;
		record_state_frame_end();
		/* (the next frame's visibility tests count into its own buffer) */
		device.visibility_tests_this_frame = 0;
		device.d_extent_previous = device.d_extent_frame;
		device.d_extent_frame = 0;
		target_version_count = 0;
		last_recorded_target = 0;
	}
	device.frame++;
	halo_present_counter++;
	stats.presents++;
	{
		/* the hitch log: a frame over 100 ms is named with what it spent
		(the game's thread between presents, the wait for the worker, the
		textures the worker decoded and the shaders compiled meanwhile) */
		extern volatile unsigned long long vita_texture_build_us, vgxm_compile_us;
		extern volatile unsigned long vita_texture_builds, vita_texture_build_bytes, vgxm_compiles;
		static unsigned long long previous_present;
		static unsigned long hitches_logged;
		unsigned long long now = vita_host_time_us();

		if (previous_present && now - previous_present > 100000ull && hitches_logged < 200)
		{
			hitches_logged++;
			platform_log("hitch: frame %lu took %.1f ms; textures decoded %lu (%lu KB) in %.1f ms, shaders compiled %lu in %.1f ms",
				device.frame, (now - previous_present) / 1000.0, vita_texture_builds, vita_texture_build_bytes / 1024,
				vita_texture_build_us / 1000.0, vgxm_compiles, vgxm_compile_us / 1000.0);
		}
		vita_texture_build_us = 0;
		vita_texture_builds = 0;
		vita_texture_build_bytes = 0;
		vgxm_compile_us = 0;
		vgxm_compiles = 0;
		previous_present = now;
	}
	{
		/* HALO_TEST_WATCHDOG=1: the game thread spins for good at frame
		120, to prove the hang watchdog produces a dump */
		static int test_watchdog = -1;

		if (test_watchdog < 0)
		{
			const char *setting = getenv("HALO_TEST_WATCHDOG");
			test_watchdog = setting && atoi(setting) != 0;
		}
		if (test_watchdog && device.frame == 30)
		{
			platform_log("watchdog test: the game thread spins from here");
			for (;;)
				;
		}
	}
	gpu_stats_on = config_boolean("debug.gpu_stats");
	if (gpu_stats_on && device.frame % 60 == 0 && stats.presents)
	{
		platform_log("immediate draws merged into the one before: %.1f a frame; not merged: not triangles %.1f, (unused) %.1f, "
			"constants changed %.1f, another draw between %.1f, state changed %.1f", merged_immediate_draws / (double)stats.presents,
			merge_rejected[0] / (double)stats.presents, merge_rejected[1] / (double)stats.presents, merge_rejected[2] / (double)stats.presents,
			merge_rejected[3] / (double)stats.presents, merge_rejected[4] / (double)stats.presents);
		merged_immediate_draws = 0;
		memset(merge_rejected, 0, sizeof(merge_rejected));
	}
	if (gpu_stats_on && device.frame % 60 == 0)
	{
		platform_log("frame %lu: %lu draws, %lu immediate, %lu clears, %lu target changes; skipped %lu no program, "
			"%lu no target, %lu shader; %lu same-state; %lu KB copied (streams %lu, immediate %lu, indices %lu) + %lu KB uniforms, %lu KB direct, %lu KB textures; record %.3f ms/frame, "
			"worker %.3f ms/frame (draw %.2f clear %.2f present %.2f), wait at present %.2f ms/frame; %lu vertex + %lu fragment uniform snapshots/frame; %s; %lu self-sampled draws/frame, %lu computed-coordinate draws/frame, alpha tests kept %lu dropped %lu per frame",
			device.frame, stats.draws / stats.presents, stats.immediate_draws / stats.presents,
			stats.clears / stats.presents, stats.target_changes / stats.presents, stats.skipped_no_program,
			stats.skipped_no_target, stats.skipped_shader, stats.same_state_draws / stats.presents,
			(stats.copied_streams + stats.copied_immediate + stats.copied_indices) / stats.presents / 1024,
			stats.copied_streams / stats.presents / 1024, stats.copied_immediate / stats.presents / 1024,
			stats.copied_indices / stats.presents / 1024, stats.copied_uniforms / stats.presents / 1024,
			stats.direct_bytes / stats.presents / 1024, vgxm_pool_used() / 1024,
			layer_time / 1000.0 / stats.presents, worker_time / 1000.0 / stats.presents,
			worker_kind_time[_command_draw] / 1000.0 / stats.presents, worker_kind_time[_command_clear] / 1000.0 / stats.presents,
			worker_kind_time[_command_present] / 1000.0 / stats.presents,
			present_wait_time / 1000.0 / stats.presents,
			stats.vertex_snapshots / stats.presents, stats.fragment_snapshots / stats.presents, vgxm_counts(),
			stats.self_sampled / stats.presents, stats.computed_draws / stats.presents,
			stats.alpha_tested_draws / stats.presents, stats.dropped_alpha_tests / stats.presents);
		{
			char line[256];
			int n = 0, source, destination;

			for (source = 0; source < 16; source++)
				for (destination = 0; destination < 16; destination++)
					if (blend_histogram[source][destination] >= stats.presents * 8)
						n += snprintf(line + n, sizeof(line) - n, " %d/%d:%lu", source, destination,
							blend_histogram[source][destination] / stats.presents);
			platform_log("blends per frame (src/dst:draws; 0/0 = opaque; 0 zero 1 one 2 srccolor 3 invsrccolor 4 srcalpha 5 invsrcalpha 6 dstalpha 7 invdstalpha 8 dstcolor 9 invdstcolor 10 srcalphasat):%s", line);
			memset(blend_histogram, 0, sizeof(blend_histogram));
		}
		{
			/* the constant writes per frame: register range (game numbering,
			-96..95) = writes/changes */
			char line[512];
			int n = 0;
			unsigned long index;

			for (index = 0; index < constant_write_kinds && n < (int)sizeof(line) - 40; index++)
			{
				if (constant_writes[index].writes < stats.presents)
					continue;
				n += snprintf(line + n, sizeof(line) - n, " %ld+%lu=%lu/%lu", (long)constant_writes[index].first - XGPU_VERTEX_CONSTANT_BIAS,
					constant_writes[index].count, constant_writes[index].writes / stats.presents, constant_writes[index].changes / stats.presents);
			}
			platform_log("constant writes per frame (first+count=writes/changes):%s", line);
			constant_write_kinds = 0;
			if (draw_profile > 0 && draw_profile_draws)
			{
				double per = 1.0 / draw_profile_draws;
				double worker_per = worker_profile_draws ? 1.0 / worker_profile_draws : 0.0;

				platform_log("draw profile (us/draw, 1 in %d of %lu draws timed): record: begin %.1f state %.1f versions %.1f constants %.1f tail %.1f | streams %.1f commit %.1f | execute: build+targets %.1f textures %.1f shaders %.1f gxm draw %.1f",
					draw_profile, draw_profile_draws * (unsigned long)draw_profile, draw_profile_us[0] * per, draw_profile_us[1] * per, draw_profile_us[2] * per, draw_profile_us[8] * per, draw_profile_us[9] * per,
					draw_profile_us[3] * per, draw_profile_us[10] * per,
					draw_profile_us[4] * worker_per, draw_profile_us[5] * worker_per, draw_profile_us[6] * worker_per, draw_profile_us[7] * worker_per);
				worker_profile_draws = 0;
				platform_log("record fine (us/draw): begin %.1f key-clear %.1f stages %.1f fu-gather %.1f fu-build %.1f",
					draw_fine_us[0] * per, draw_fine_us[1] * per, draw_fine_us[2] * per, draw_fine_us[3] * per, draw_fine_us[4] * per);
				memset(draw_fine_us, 0, sizeof(draw_fine_us));
				memset(draw_profile_us, 0, sizeof(draw_profile_us));
				draw_profile_draws = 0;
			}
		}
		platform_log("uniform KB/frame by kind: vertex chunks A %.1f B %.1f C1 %.1f C2 %.1f D %.1f E %.1f, vertex misc %.1f, fragment (worker) %.1f",
			stats.copied_chunk[0] / 1024.0 / stats.presents, stats.copied_chunk[1] / 1024.0 / stats.presents,
			stats.copied_chunk[2] / 1024.0 / stats.presents, stats.copied_chunk[3] / 1024.0 / stats.presents,
			stats.copied_chunk[4] / 1024.0 / stats.presents, stats.copied_chunk[5] / 1024.0 / stats.presents,
			stats.copied_vertex_misc / 1024.0 / stats.presents, stats.copied_fragment / 1024.0 / stats.presents);
		platform_log("state blocks per frame: %lu reused, %lu new (%lu new materials, %lu new values, %lu compared equal); worker builds %lu (+%lu texture-only)",
			stats.state_quick / stats.presents, stats.state_new / stats.presents, stats.material_new / stats.presents,
			stats.values_new / stats.presents,
			stats.state_equal / stats.presents, stats.worker_builds / stats.presents, stats.worker_texture_builds / stats.presents);
		memset(&stats, 0, sizeof(stats));
		layer_time = 0;
		worker_time = 0;
		memset(worker_kind_time, 0, sizeof(worker_kind_time));
		present_wait_time = 0;
	}
	platform_pump_events();

	pthread_mutex_lock(&vertical_blank_lock);
	while (pending_flips >= 2)
		pthread_cond_wait(&vertical_blank_condition, &vertical_blank_lock);
	pending_flips++;
	pthread_mutex_unlock(&vertical_blank_lock);
}

HRESULT WINAPI D3DDevice_PersistDisplay(void)
{
	return S_OK;
}
