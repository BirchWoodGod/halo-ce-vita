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
#include "dynamic_resolution.h"
#include "blend_constant.h"
#include "sdl_platform.h"
#include "halo_ui_pointer.h"
#include "port_config.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef HALO_VITA
#include <sys/mman.h>
#endif
#include "vita_compat.h"
#include "vita_host.h"
#include "frame_timing.h"

void d3d8_surface_initialize(D3DSurface *surface, D3DFORMAT format, unsigned long width, unsigned long height);
void d3d8_surface_resize(D3DSurface *surface, D3DFORMAT format, unsigned long width, unsigned long height);
void *physical_memory_get_tag_cache_base_address(void);

#define TAG_CACHE_SIZE 0x01600000UL

/* ---------- the screen

The Xbox screen is 640x480; a wider one (display.screen_width, or
HALO_DISPLAY_WIDTH) widens the 3D view as on Android.

The settings panel changes the width (Aspect ratio: HALO_DISPLAY_WIDTH 848
or 640) and the render scale (HALO_RENDER_SCALE) while the game runs: after
a frame is presented (halo_screen_commit, from rasterizer_present's
rasterizer_screen_width_update, which then moves the game's screen and
title-safe bounds and its copies of the back buffer to the new width), the
worker and the GPU are waited for and the screen-sized targets made again
(screen_settings_apply). */

#define SCREEN_HEIGHT 480

static long screen_width;
static long ui_offset;
/* the widest the back buffer's memory holds (CreateDevice) */
static unsigned long screen_allocated_width;

static long screen_width_wanted(void)
{
	const char *display = getenv("HALO_DISPLAY_WIDTH");
	long width = config_integer("display.screen_width");

	if (width <= 0)
		width = display ? atol(display) : 640;
	if (width < 640)
		width = 640;
	if (width > 1024)
		width = 1024;
	return width & ~1L;
}

long halo_screen_width(void)
{
	if (!screen_width)
	{
		screen_width = screen_width_wanted();
		platform_log("screen: %ldx%d", screen_width, SCREEN_HEIGHT);
	}
	return screen_width;
}

void halo_screen_ui_offset(unsigned char centered)
{
	ui_offset = centered ? (halo_screen_width() - 640) / 2 : 0;
}

static void screen_settings_apply(void);
static void dynres_configure(void);
static void cdram_wanted_relieve(void);
static unsigned long cdram_census_frame(void);
static void cdram_census(const char *when);

long halo_screen_commit(void)
{
	extern volatile unsigned long halo_settings_generation;
	static unsigned long settings_seen;
	static int dynres_configured;
	unsigned long generation = __atomic_load_n(&halo_settings_generation, __ATOMIC_ACQUIRE);

	if (!screen_width)
		return halo_screen_width();
	if (!dynres_configured)
	{
		dynres_configured = 1;
		dynres_configure();
	}
	if (generation != settings_seen)
	{
		settings_seen = generation;
		screen_settings_apply();
		dynres_configure();
	}
	cdram_wanted_relieve();
	return screen_width;
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
	/* the Cg while its program is compiled in the background (shader 0
	meanwhile: its draws are skipped), else NULL */
	char *pending_source;
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
	/* its instructions' hash (the draw hash) */
	unsigned long instruction_hash;
	struct vertex_variant *variants;
};

/* ---------- pixel shaders */

struct fragment_entry
{
	struct fragment_entry *next;
	unsigned long hash;
	struct nv2a_pixel_shader_key key;
	unsigned long shader;
	/* as vertex_variant's */
	char *pending_source;
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
	/* whether a draw or a colour clear has gone into its target since it
	was made (the worker's: execute_draw, execute_command) */
	BOOL drawn;
	/* its target is a cell of its surface's atlas (render_target_atlas) */
	BOOL cell;
	/* a screen-sized target given back for CDRAM while unused (its id
	kept): made again when next drawn into (render_target_wake), not before
	the frame wake_frame after a try that found no memory */
	BOOL dormant;
	unsigned long wake_frame;
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
		/* the buffer is one the game never rewrites (halo_d3d_buffer_in_place) */
		BOOL in_place;
	} streams[16];
	UINT base_vertex_index;
	/* halo_d3d_stream_attribute: an input register the stream draws take
	from a stream of four floats per vertex instead of its current value
	(-1: none) */
	long extra_attribute_reg, extra_attribute_stream;

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
	/* by frame (the last few): the game's index of each slot's test */
	struct
	{
		unsigned long frame;
		unsigned long count;
		DWORD index[VGXM_VISIBILITY_SLOTS];
	} visibility_frames[8];
	unsigned long visibility_hint;

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
	/* blended draws left out for sampling a texture still loading (draw_samples_stand_in) */
	unsigned long stand_in_draws_left_out;
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
	/* new values blocks (record_values); materials found kept */
	unsigned long values_new, material_kept;
} stats;

/* draws recorded since start-up, never reset: the render profile counts
them per phase (halo_render_draw_counts) */
static unsigned long draw_counter_stream, draw_counter_immediate;

/* (port) a scissor rectangle for the draws recorded next, in normalised
device coordinates of the viewport (x right, y down, -1..1), or off (NULL):
the dynamic lights' passes over the structure (rasterizer_xbox_environment.c)
are clipped to the screen rectangle outside which their light adds nothing.
Mapped to the viewport's pixels when each draw is recorded, rounded outward,
and intersected with the viewport's own rectangle. */
static int device_scissor_on;
static float device_scissor[4];

/* (port) the sky drawn after the opaque scene (render.c, HALO_SKY_LATE):
while on, the draws recorded test depth, less-equal, without writing it,
at the depth the window was cleared to (1: the viewport's depth range is
made that one value), so they colour only the pixels nothing opaque has
covered - the pixels the sky's own draws, without depth, would have been
left visible on before the scene covered the rest. */
static int device_sky_depth;

void halo_d3d_sky_depth(int on)
{
	device_sky_depth = on != 0;
}

int halo_d3d_sky_late_supported(void)
{
	return 1;
}

void halo_d3d_scissor(const float *rectangle)
{
	device_scissor_on = rectangle != NULL;
	if (rectangle)
		memcpy(device_scissor, rectangle, sizeof(device_scissor));
}

static void scissor_apply(long clip[4])
{
	float width, height;
	long x0, y0, x1, y1;

	if (!device_scissor_on)
		return;
	width = (float)device.viewport.Width;
	height = (float)device.viewport.Height;
	x0 = (long)floorf((float)device.viewport.X + (device_scissor[0] + 1.0f) * 0.5f * width) - 1;
	y0 = (long)floorf((float)device.viewport.Y + (device_scissor[1] + 1.0f) * 0.5f * height) - 1;
	x1 = (long)ceilf((float)device.viewport.X + (device_scissor[2] + 1.0f) * 0.5f * width) + 1;
	y1 = (long)ceilf((float)device.viewport.Y + (device_scissor[3] + 1.0f) * 0.5f * height) + 1;
	if (x0 > clip[0]) clip[0] = x0;
	if (y0 > clip[1]) clip[1] = y0;
	if (x1 < clip[2]) clip[2] = x1;
	if (y1 < clip[3]) clip[3] = y1;
	if (clip[2] < clip[0]) clip[2] = clip[0];
	if (clip[3] < clip[1]) clip[3] = clip[1];
}

void halo_render_draw_counts(unsigned long *stream, unsigned long *immediate)
{
	*stream = draw_counter_stream;
	*immediate = draw_counter_immediate;
}

/* time spent in this layer (debug.gpu_stats) */
unsigned long long vita_host_time_us(void);
static unsigned long long layer_time, layer_entered, present_wait_time;
/* the game's wait for the worker at this frame's present (the hitch log) */
static unsigned long long frame_drain_us;
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

/* the active camouflage's copy of the screen (rasterizer_xbox_active_camouflage.c:
320x240, the only colour surface of that size). Its target was made the
first time a cloaked unit was seen, and late in a session - all 128 targets
made, CDRAM taken - none could be had: beta.1 drew the cloaked units black,
beta.2 and 3 left their distortion out, so a fully cloaked unit was not
drawn at all (#26: d40's stealth Flood combat forms, heard and felt but
unseen, visible only while a hit dropped their camouflage). Now the target
is made with the first targets (camo_target_reserve), handed to the copy
when it first asks, and never taken over by the recycling; and a distortion
pass whose copy still has no target samples the picture it draws over
(camo_fallback). HALO_CAMO_TARGET=0: as before (no reserve, left out);
=2: the fallback only (no reserve, recycled as any other) */
#define CAMO_COPY_WIDTH 320
#define CAMO_COPY_HEIGHT 240

static int camo_target_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		const char *setting = getenv("HALO_CAMO_TARGET");

		enabled = setting ? atoi(setting) : 1;
		if (enabled < 0 || enabled > 2)
			enabled = 1;
		if (enabled != 1)
			platform_log("active camouflage: %s (HALO_CAMO_TARGET=%d)", enabled ? "no reserved target, the distortion samples "
				"the picture when its copy has no target" : "no reserved target, a copy with no target leaves the distortion out",
				enabled);
	}
	return enabled != 0;
}

/* the copy's target made at the start and kept (HALO_CAMO_TARGET=1) */
static int camo_target_reserved(void)
{
	static int reserved = -1;

	if (reserved < 0)
	{
		const char *setting = getenv("HALO_CAMO_TARGET");

		reserved = !setting || atoi(setting) == 1 || atoi(setting) < 0 || atoi(setting) > 2;
	}
	return reserved;
}

static BOOL camo_copy_size(unsigned long width, unsigned long height, BOOL depth)
{
	return !depth && width == CAMO_COPY_WIDTH && height == CAMO_COPY_HEIGHT;
}

static unsigned long camo_reserved_id;
static struct vgxm_texture camo_reserved_texture;

/* made with the first targets, while slots and CDRAM are plenty */
static void camo_target_reserve(void)
{
	static int attempts;

	if (camo_reserved_id || attempts >= 4 || !camo_target_reserved())
		return;
	attempts++;
	camo_reserved_id = vgxm_target_create(CAMO_COPY_WIDTH, CAMO_COPY_HEIGHT, FALSE, &camo_reserved_texture);
	if (camo_reserved_id)
		platform_log("active camouflage: %dx%d target %lu reserved for the copy of the screen", CAMO_COPY_WIDTH,
			CAMO_COPY_HEIGHT, camo_reserved_id);
	else
		platform_log("active camouflage: cannot reserve a %dx%d target", CAMO_COPY_WIDTH, CAMO_COPY_HEIGHT);
	attempts = camo_reserved_id ? 4 : attempts;
}

/* a target that has been neither drawn into nor sampled for ten seconds,
taken over when no more targets can be made: they are never freed, and
every map's surfaces add their own - after an hour and a level change the
glow's 128x128 targets were not made any more (the bloom went). One of the
same size is taken as it is; failing that, the stalest of another size is
made again at this size (vgxm_target_remake gives its memory back first):
a surface of a size no other target has - the active camouflage's 320x240
copy of the screen, first drawn when a cloaked unit is first seen - could
otherwise never get one once the limit was reached, and the cloaked units
sampled its never-written memory and drew black. It leaves its old
surface's bucket; that surface gets a new target if it comes back */
static struct render_target_entry *render_target_recycle(unsigned long width, unsigned long height, BOOL depth)
{
	struct render_target_entry *entry, *stalest = NULL, **link;

	for (entry = render_targets; entry; entry = entry->next)
	{
		if (entry->id && !entry->chain_levels && !entry->cell && !entry->dormant && entry->target.depth == depth &&
			entry->last_used + 300 < device.frame &&
			!(camo_target_reserved() && camo_copy_size(entry->target.width, entry->target.height, entry->target.depth) &&
				!camo_copy_size(width, height, depth)))
		{
			if (entry->target.width == width && entry->target.height == height)
				break;
			if (!stalest || entry->last_used < stalest->last_used)
				stalest = entry;
		}
	}
	if (!entry)
		entry = stalest;
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
	if (entry->target.width != width || entry->target.height != height)
	{
		static unsigned long remade;

		if (++remade <= 20)
			platform_log("render target %lu remade from %lux%lu to %lux%lu (%lu so far)", entry->id,
				entry->target.width, entry->target.height, width, height, remade);
		if (!vgxm_target_remake(entry->id, width, height, depth, &entry->texture))
		{
			/* (its slot is empty now: the entry stays in the list, without
			a target, and is never looked up again) */
			entry->id = 0;
			return NULL;
		}
	}
	return entry;
}

/* Copies in an atlas (the worker's): a small colour surface the game draws
four or more copies of in a frame - the object shadows and their blurs, a
copy per object - has its copies made as cells of one atlas target
(vgxm_target_create_cell), so the worker's waves of them (small_target_wave)
draw all the shadows in one scene and all the blurs in another, instead of
a scene each; the copies made before the surface was seen to need it are
moved into cells at the next present. */
#define ATLAS_SURFACES 8

static unsigned long atlas_surfaces[ATLAS_SURFACES];
static BOOL atlas_surfaces_pending;

static BOOL render_target_atlas(unsigned long data)
{
	unsigned long index;

	for (index = 0; index < ATLAS_SURFACES; index++)
		if (atlas_surfaces[index] == data)
			return TRUE;
	return FALSE;
}

static unsigned long long render_target_name(unsigned long data, unsigned long version, unsigned long width,
	unsigned long height, BOOL depth)
{
	return ((unsigned long long)data << 24) ^ ((unsigned long long)version << 56) ^ (width << 12) ^ height ^
		((unsigned long long)depth << 62);
}

/* (the worker's present) the copies of the surfaces that now have an
atlas, made before it, moved into cells: the frame they were drawn and read
in is over, and each frame draws a copy before it reads it */
static void render_target_atlas_frame_end(void)
{
	struct render_target_entry *entry;

	if (!atlas_surfaces_pending)
		return;
	atlas_surfaces_pending = FALSE;
	for (entry = render_targets; entry; entry = entry->next)
	{
		struct vgxm_texture texture;
		unsigned long id;

		if (!entry->id || entry->cell || entry->version < 1 || entry->target.depth || entry->chain_levels ||
			!render_target_atlas(entry->target.data))
			continue;
		id = vgxm_target_create_cell(entry->target.data, entry->version, entry->target.width, entry->target.height,
			&texture);
		if (!id)
			continue;
		/* (its own target stays made, unused) */
		entry->id = id;
		entry->texture = texture;
		entry->cell = TRUE;
		entry->drawn = FALSE;
		vgxm_debug_name_target(id, render_target_name(entry->target.data, entry->version, entry->target.width,
			entry->target.height, FALSE));
	}
}

/* a dormant entry's target made again (screen_targets_doze gave its memory
back); FALSE while there is no memory for it */
static BOOL render_target_wake(struct render_target_entry *entry)
{
	static unsigned long woken;

	if (!entry->dormant)
		return TRUE;
	if (device.frame < entry->wake_frame)
		return FALSE;
	if (!vgxm_target_remake(entry->id, entry->target.width, entry->target.height, entry->target.depth, &entry->texture))
	{
		entry->wake_frame = device.frame + 30;
		return FALSE;
	}
	entry->dormant = FALSE;
	entry->drawn = FALSE;
	if (++woken <= 20)
		platform_log("render target %lu (%lux%lu %s) made again as it is drawn into (%lu so far)", entry->id,
			entry->target.width, entry->target.height, entry->target.depth ? "depth" : "colour", woken);
	return TRUE;
}

static struct render_target_entry *render_target_get_version(const D3DSurface *surface, unsigned long version)
{
	struct render_target_entry *entry, *placeholder = NULL, **link;
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
			if (entry->id)
				return render_target_wake(entry) ? entry : NULL;
			/* A surface no target could be made for. It used to stay
			without one for good: the recycling below only ran at the
			first request, and right after a level change every target is
			still too recently used to be taken over, so the surfaces of
			the new level that came too late (the glow's, the motion
			sensor's) kept a placeholder and their effect never showed
			again. Ask again every 30 frames; until then it has none. */
			if (device.frame < entry->last_used + 30)
				return NULL;
			placeholder = entry;
			break;
		}
	}
	if (placeholder)
	{
		for (link = render_target_bucket(surface->Data); *link; link = &(*link)->next_in_bucket)
		{
			if (*link == placeholder)
			{
				*link = placeholder->next_in_bucket;
				break;
			}
		}
	}
	camo_target_reserve();
	{
		struct vgxm_texture texture;
		unsigned long id = 0;
		BOOL cell = FALSE;

		if (camo_reserved_id && camo_copy_size(width, height, depth))
		{
			/* (the camouflage's copy: the target made for it at the start) */
			id = camo_reserved_id;
			texture = camo_reserved_texture;
			camo_reserved_id = 0;
		}

		if (version >= 4 && !depth && width <= 128 && height <= 128 && !render_target_atlas(surface->Data))
		{
			unsigned long index;

			for (index = 0; index < ATLAS_SURFACES && atlas_surfaces[index]; index++)
				;
			if (index < ATLAS_SURFACES)
			{
				atlas_surfaces[index] = surface->Data;
				atlas_surfaces_pending = TRUE;
			}
		}
		if (!id && version >= 1 && !depth && render_target_atlas(surface->Data))
		{
			id = vgxm_target_create_cell(surface->Data, version, width, height, &texture);
			cell = id != 0;
		}
		if (!id)
			id = vgxm_target_create(width, height, depth, &texture);
		if (id)
		{
			entry = placeholder;
			if (!entry)
			{
				entry = calloc(1, sizeof(*entry));
				entry->next = render_targets;
				render_targets = entry;
			}
			entry->id = id;
			entry->texture = texture;
			entry->cell = cell;
		}
		else if ((entry = render_target_recycle(width, height, depth)) != NULL)
		{
			static unsigned long recycled;

			/* (a placeholder taken over by the recycled entry stays in the
			list, without a target, and is never looked up again) */
			if (++recycled <= 20)
				platform_log("render target recycled for a %lux%lu %s surface (%lu so far)", width, height,
					depth ? "depth" : "colour", recycled);
		}
		else
		{
			entry = placeholder;
			if (!entry)
			{
				entry = calloc(1, sizeof(*entry));
				entry->next = render_targets;
				render_targets = entry;
				platform_log("cannot create a %lux%lu %s target", width, height, depth ? "depth" : "colour");
			}
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
	entry->drawn = FALSE;
	entry->last_used = device.frame + 1;
	entry->next_in_bucket = *render_target_bucket(entry->target.data);
	*render_target_bucket(entry->target.data) = entry;
	if (entry->id)
		vgxm_debug_name_target(entry->id, render_target_name(surface->Data, version, width, height, depth));
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
		if (entry->id && !entry->dormant && entry->target.data == data && !entry->target.depth &&
			entry->version == version && (!best || entry->last_rendered > best->last_rendered))
		{
			best = entry;
		}
	}
	return best;
}

/* whether the game has rendered into this surface (it has an entry, with a
target or without one) */
static BOOL render_target_entry_known(unsigned long data)
{
	struct render_target_entry *entry;

	for (entry = *render_target_bucket(data); entry; entry = entry->next_in_bucket)
		if (entry->target.data == data && !entry->target.depth)
			return TRUE;
	return FALSE;
}

static struct render_target_entry *render_target_entry_find(unsigned long data)
{
	return render_target_entry_find_version(data, 0);
}

/* the targets of a texture the game renders level by level, re-made as one
mip chain the first time it is sampled with its levels (the levels' earlier
targets are left unused).

Only the levels at least one GPU tile (32x32, SCE_GXM_TILE_SIZEX/Y) in
each direction are chained: the water's 128x128 ripple map chains 128, 64
and 32, and its 16x16 fourth level keeps a target of its own that nothing
samples. The texture side of the layout (linear levels one after another,
rows rounded up to 8 texels) is the one every mipmapped BGRA texture of the
texture cache already uses on the hardware (vita_textures.c LINEAR_ROW), but
the colour surfaces are not: the chain was the only place the GPU rendered
into a linear surface narrower than a tile (16 texels, rows 64 bytes apart)
or one that does not start a memory block, and b30's creek showed blue
streaks on the hardware only (#13/#20; Vita3K samples its own copy of a
render target's first level and cannot show it). With every chained level a
whole number of tiles, each level's rows are whole tiles wide and start
4 KB apart, however the pixel back end writes a tile out.
HALO_TARGET_CHAIN_MIN_SIZE=16 chains the small level again (as before);
HALO_TARGET_CHAIN=0 samples the first level only. */
static void render_target_chain(struct render_target_entry *base, const struct xgpu_texture_description *description,
	unsigned long data, unsigned long version)
{
	unsigned long ids[12], levels = description->levels, level;
	struct vgxm_texture texture;
	static long minimum_size = -1;

	if (base->chain_levels)
		return;
	base->chain_levels = -1;
	if (minimum_size < 0)
	{
		const char *setting = getenv("HALO_TARGET_CHAIN_MIN_SIZE");

		minimum_size = setting && atol(setting) > 0 ? atol(setting) : 32;
	}
	if (levels > 12)
		levels = 12;
	while (levels > 1 && ((base->target.width >> (levels - 1)) < (unsigned long)minimum_size ||
		(base->target.height >> (levels - 1)) < (unsigned long)minimum_size))
	{
		levels--;
	}
	if (levels < 2 || description->linear || description->cube_map || description->depth > 1 ||
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

	{
		/* (debug) HALO_LIGHT_CHECK=1: a vertex constant written as a value
		that is not a finite number (NaN or infinity: what it reaches draws
		black on the SGX) is logged with its register */
		static int check = -1;

		if (check < 0)
			check = getenv("HALO_LIGHT_CHECK") && atoi(getenv("HALO_LIGHT_CHECK"));
		if (check)
		{
			const float *values = (const float *)data;
			static unsigned long logged;
			unsigned long index;

			for (index = 0; index < count * 4; index++)
			{
				if (!(values[index] == values[index]) || values[index] > 3.0e38f || values[index] < -3.0e38f)
				{
					if (logged++ < 50)
						platform_log("light check: vertex constant c%lu.%c = %f (frame %lu, write of %lu from c%lu)",
							first + index / 4, "xyzw"[index % 4], (double)values[index], device.frame, count, first);
					break;
				}
			}
		}
	}

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
		{
			/* (the two surfaces' memory, the first blocks of the memory
			window, is made for 848 columns whatever the width: a 640-wide
			4:3 screen, HALO_DISPLAY_WIDTH=640, then leaves the game state
			after them at the same address, so saves load in either) */
			unsigned long allocated = width < 848 ? 848 : width;

			/* (the widest the screen can be made while the game runs) */
			screen_allocated_width = allocated;

			d3d8_surface_initialize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, allocated, height);
			d3d8_surface_initialize(&device.depth_buffer, D3DFMT_LIN_D24S8, allocated, height);
			if (allocated != width)
			{
				d3d8_surface_resize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, width, height);
				d3d8_surface_resize(&device.depth_buffer, D3DFMT_LIN_D24S8, width, height);
			}
		}
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
		device.extra_attribute_reg = -1;
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
when it begins, its draws count into that slot, and the frame's list of
slots remembers the index each was ended with. The GPU is a frame or more
behind when the game asks for a result (the lens flares, rasterizer_lights.c,
test once a frame each and read the results at the start of the next), so
a result is the count of the test with that index in the newest frame the
GPU has finished (its notification), one to three frames old - as on the
desktop's GL device - rather than a wait. The flares name their tests by
something that stays the same from frame to frame (rasterizer_lights.c
lens_flare_occlusion_test_key): a test that frame did not have reads 0.
This used to be a stub that read 0 for every test: no lens flare was ever
drawn on the Vita - not the lights' coronas, nor a10's calibration lights,
which the tutorial script turns from red to green. */

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
	if (device.visibility_index)
	{
		/* (the frame's list: begun anew at its first test) */
		typeof(device.visibility_frames[0]) *frame = &device.visibility_frames[device.frame % 8];

		if (frame->frame != device.frame)
		{
			frame->frame = device.frame;
			frame->count = 0;
		}
		while (frame->count < device.visibility_index)
			frame->index[frame->count++] = 0;
		frame->index[device.visibility_index - 1] = index;
	}
	return S_OK;
}

HRESULT WINAPI D3DDevice_GetVisibilityTestResult(DWORD index, UINT *result, ULONGLONG *time_stamp)
{
	unsigned long frame_number, slot, tried;
	int ring;

	if (time_stamp)
		*time_stamp = 0;
	if (!result)
		return S_OK;
	*result = 0;
	if (!device.gpu_ready || (ring = vgxm_visibility_newest(&frame_number)) < 0)
		return S_OK;
	{
		typeof(device.visibility_frames[0]) *frame = &device.visibility_frames[frame_number % 8];

		if (frame->frame != frame_number || !frame->count)
			return S_OK;
		/* (the flares ask in the order they tested: start where the last
		one was found) */
		slot = device.visibility_hint < frame->count ? device.visibility_hint : 0;
		for (tried = 0; tried < frame->count; tried++, slot = slot + 1 < frame->count ? slot + 1 : 0)
		{
			if (frame->index[slot] == index)
			{
				device.visibility_hint = slot + 1;
				*result = (UINT)vgxm_visibility_count(ring, slot + 1);
				break;
			}
		}
	}
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

/* (each method's dirty bit, made with the table: the game sets 6500
simple states a frame on b30, each through this call) */
static unsigned char simple_dirty_bit_of_method[0x800];
static int simple_state_tables_made;

void D3DFASTCALL D3DDevice_SetRenderState_Simple(DWORD method, DWORD value)
{
	/* (the inline D3DDevice_SetRenderState stores the value after this
	call: what the table holds is still the old value) */
	unsigned long slot = (method - 0x40000UL) >> 2;

	if (!simple_state_tables_made)
	{
		unsigned long state;

		for (state = 0; state < D3DRS_SIMPLE_MAX; state++)
		{
			simple_state_of_method[(D3DSIMPLERENDERSTATEENCODE[state] - 0x40000UL) >> 2] = (unsigned char)(state + 1);
			simple_dirty_bit_of_method[(D3DSIMPLERENDERSTATEENCODE[state] - 0x40000UL) >> 2] =
				(unsigned char)render_state_dirty_bit(state);
		}
		simple_state_tables_made = 1;
	}
	if ((method & 3) || slot >= sizeof(simple_state_of_method) || !simple_state_of_method[slot])
		device_state_dirty = STATE_DIRTY_MATERIAL | STATE_DIRTY_VALUES;
	else if (D3D__RenderState[simple_state_of_method[slot] - 1] != value)
		device_state_dirty |= simple_dirty_bit_of_method[slot];
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

static unsigned long hash_words(const void *data, unsigned long size);

struct vertex_shader_object;
static void declaration_masks(const struct vertex_shader_object *declaration, unsigned long *provided,
	unsigned long *packed, unsigned long *color);

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
	object->instruction_hash = object->instructions ?
		hash_words(object->instructions, object->instruction_count * 4 * sizeof(DWORD)) : 0;
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

/* a variant's program, asked for again while it compiles in the background
(vgxm_shader_request); its Cg is freed once there is an answer */
static void vertex_variant_resolve(struct vertex_shader_object *program, struct vertex_variant *variant)
{
	char *source = variant->pending_source;
	unsigned long shader = vgxm_shader_request(source, 0);

	if (shader == VGXM_SHADER_PENDING)
	{
		variant->shader = 0;
		return;
	}
	variant->shader = shader;
	variant->pending_source = NULL;
	if (!shader || debug_settings_dump())
	{
		dump_shader(source, "vs", hash_words(source, strlen(source) & ~3UL));
		if (debug_settings_dump())
			platform_log("vertex shader %lu (inputs %04lx): vs_%08lx.cg", program->id, variant->provided_mask,
				hash_words(source, strlen(source) & ~3UL));
	}
	if (!shader)
		platform_log("vertex shader %lu (inputs %04lx) does not compile", program->id, variant->provided_mask);
	free(source);
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
			if (variant->pending_source)
				vertex_variant_resolve(program, variant);
			return variant->shader;
		}
	}
	variant = calloc(1, sizeof(*variant));
	variant->provided_mask = provided_mask;
	variant->packed_mask = packed_mask;
	variant->color_mask = color_mask;
	source = nv2a_vertex_shader_to_cg(program->instructions, program->instruction_count, provided_mask, packed_mask,
		color_mask);
	variant->pending_source = source;
	vertex_variant_resolve(program, variant);
	variant->next = program->variants;
	program->variants = variant;
	return variant->shader;
}

/* as vertex_variant_resolve */
static void fragment_entry_resolve(struct fragment_entry *entry)
{
	char *source = entry->pending_source;
	unsigned long shader = vgxm_shader_request(source, 1);

	if (shader == VGXM_SHADER_PENDING)
	{
		entry->shader = 0;
		return;
	}
	entry->shader = shader;
	entry->pending_source = NULL;
	if (!shader && !debug_settings_dump())
		dump_shader(source, "ps", entry->hash);
	if (!shader)
		platform_log("pixel shader %08lx does not compile", entry->hash);
	free(source);
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
	{
		if (last->pending_source)
			fragment_entry_resolve(last);
		return last->shader;
	}
	hash = hash_words(key, sizeof(*key));
	bucket = &fragment_buckets[hash % FRAGMENT_BUCKETS];
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->hash == hash && !memcmp(&entry->key, key, sizeof(*key)))
		{
			last = entry;
			if (entry->pending_source)
				fragment_entry_resolve(entry);
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
	entry->pending_source = source;
	fragment_entry_resolve(entry);
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

/* the stages' bump environment states (D3DTSS_BUMPENVMAT00 to
D3DTSS_BUMPENVLOFFSET): a block of their own, which consecutive values
blocks share - they seldom change, the values every object */
struct record_bump
{
	DWORD bump[D3DTSS_MAXSTAGES][RECORD_VALUE_BUMP_COUNT];
};

struct record_values
{
	DWORD render_state[RECORD_VALUE_COUNT];
	const struct record_bump *bump;
};

struct record_state
{
	const struct record_material *material;
	const struct record_values *values;
	/* the stages with a texture (bit per stage), the textures' headers
and their palettes' colours: what the worker reads of them */
	unsigned long textures_present;
	DWORD texture_header[D3DTSS_MAXSTAGES][5];
	const D3DCOLOR *palette_data[D3DTSS_MAXSTAGES];
};

/* the targets as the game had them set (copies of the surfaces): a block
in the frame's arena that consecutive records share */
struct record_targets
{
	D3DSurface color_surface, depth_surface;
	BOOL color_valid, depth_valid;
};

/* (what the game's thread writes into a record comes first, together - a
record is written into a cold ring entry, where every line touched is a
cache miss - then what the worker fills in) */
struct draw_segment
{
	unsigned long first_index, index_count, visibility_index;
};

struct render_command
{
	unsigned long kind;
	/* (split records) the state block; NULL: the record was built in full */
	const struct record_state *state;
	const struct record_targets *targets;
	/* the depth target's presence and the constant-program flag */
	unsigned char has_depth;
	unsigned char simple;
	/* a small target drawn before the main scene: no big target is read */
	BOOL hoistable;
	/* (hoisted records) the first record of a run into its target, and the
	wave the worker runs it in (render_worker: small_target_wave) */
	unsigned char new_run;
	unsigned char wave;
	/* the records before it are run first, the held waves and the main
	scene's so far (a small target's copies ran out: command_begin) */
	unsigned char flush_before;
	/* (HALO_FILL_STATS) the render phase that recorded the draw (render.c) */
	signed char phase;
	/* recorded while the sky is drawn late (halo_d3d_sky_depth) */
	unsigned char sky_depth;
	/* the copy of a small target this run renders into */
	unsigned long color_version;
	/* draws */
	struct vertex_shader_object *program;
	unsigned long provided_mask, packed_mask, color_mask;
	BOOL immediate;
	/* the copy of a small target a stage reads, and that target's surface */
	unsigned long texture_version[D3DTSS_MAXSTAGES];
	unsigned long texture_target_data[D3DTSS_MAXSTAGES];
	/* (immediate draws merged across visibility tests, immediate_end) more
	than one: the draw is issued once per segment, each its own index range
	and visibility slot; 0 or 1: as one draw */
	unsigned long segment_count;
	const struct draw_segment *segments;
	struct vgxm_draw draw;
	/* (the worker's) */
	BOOL skip;
	struct nv2a_pixel_shader_key key;
	DWORD texture_header[D3DTSS_MAXSTAGES][5];
	BOOL texture_present[D3DTSS_MAXSTAGES];
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

/* The recording's buffers that last the whole run - the command ring (6 MB),
the state, material, values and bump arenas of the three frames in flight
(~7 MB), the material cache and the target arenas, ~16 MB in all - in
memory blocks of user RAM rather than the C heap: on the Vita newlib's
fixed 48 MB, which the game, the system's libraries and the shader compiler
share, and which they had filled to ~42 MB by the main menu, leaving a
session's later allocations ~6 MB. Zeroed; never freed. A request of
LASTING_OWN_BYTES or more has a block of its own, the smaller ones are
carved from blocks of LASTING_BLOCK_BYTES; the C heap when there is no
block. (The harness maps them: its C heap figures stay the Vita's.) */
#define LASTING_BLOCK_BYTES (1024ul * 1024)
#define LASTING_OWN_BYTES (512ul * 1024)

static void *lasting_alloc(unsigned long bytes, const char *name)
{
	static unsigned char *block;
	static unsigned long block_left;
	unsigned char *memory = NULL;

	bytes = (bytes + 63) & ~63ul;
	if (bytes > block_left)
	{
		unsigned long size = bytes >= LASTING_OWN_BYTES ? (bytes + 4095) & ~4095ul : LASTING_BLOCK_BYTES;
#ifdef HALO_VITA
		int uid = -1;

		memory = vita_host_block_alloc("halo renderer buffers", size, &uid);
		if (memory)
			memset(memory, 0, size);
#else
		memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (memory == MAP_FAILED)
			memory = NULL;
#endif
		if (!memory)
		{
			platform_log("Direct3D: no memory block for the %s (%lu KB): in the C heap", name, bytes / 1024);
			return calloc(1, bytes);
		}
		/* (a block of its own keeps what is left of the last one) */
		if (bytes < LASTING_OWN_BYTES)
		{
			block = memory;
			block_left = size;
		}
		else
			return memory;
	}
	memory = block;
	block += bytes;
	block_left -= bytes;
	return memory;
}

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

/* a stage addressed with D3DTADDRESS_BORDER: outside the texture it reads
the border colour. GXM has no such mode (vita_gxm.c clamps), and a clamp
smears the edge texels outward instead: the object shadows, projected onto
the ground with border addressing, streaked for metres down a slope from
the edge of their texture, and the flashlight's spot texture lit what lies
outside its cone. The fragment program tests the coordinate
(nv2a_psh_cg.c). HALO_TEXTURE_BORDER=0: clamped, as before. */
static void key_border(struct nv2a_pixel_shader_key *key, int stage, const DWORD *texture_state)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		const char *setting = getenv("HALO_TEXTURE_BORDER");

		enabled = !setting || atoi(setting) != 0;
	}
	if (stage == 0)
		key->border_mask = 0;
	key->border_color[stage] = 0;
	if (enabled && (texture_state[D3DTSS_ADDRESSU] == D3DTADDRESS_BORDER || texture_state[D3DTSS_ADDRESSV] == D3DTADDRESS_BORDER))
	{
		key->border_mask |= (unsigned char)(1U << stage);
		key->border_color[stage] = texture_state[D3DTSS_BORDERCOLOR];
	}
}

static unsigned long stage_texture_mode_of(const struct nv2a_pixel_shader_key *key, int stage)
{
	return (key->texture_modes >> (5 * stage)) & 0x1f;
}

/* the targets of a record; FALSE if there is nothing to draw into */
/* the colour target of the worker's last bound targets
(bind_recorded_targets), marked drawn once a draw or a clear goes in */
static struct render_target_entry *worker_color_entry;
/* (HALO_FILL_STATS: fill_stats.c) the worker's depth target, and the
estimate's entry points */
static unsigned long worker_depth_id;
int halo_fill_stats_sampled(void);
void halo_fill_stats_draw(const struct vgxm_draw *draw, const DWORD *instructions, unsigned long instruction_count,
	unsigned long input_mask, unsigned long packed_mask, unsigned long color_mask, int phase,
	unsigned long color_id, unsigned long depth_id);
void halo_fill_stats_clear(unsigned long flags, float depth, const long clip[4], unsigned long depth_id);
void halo_fill_stats_present(unsigned long color_id, unsigned long width, unsigned long height);
extern int halo_render_phase;

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

	const struct record_targets *targets = command->targets;

	if (last.color_valid == targets->color_valid && last.depth_valid == targets->depth_valid &&
		last.color_version == command->color_version &&
		(!targets->color_valid || !memcmp(&last.color, &targets->color_surface, sizeof(last.color))) &&
		(!targets->depth_valid || !memcmp(&last.depth, &targets->depth_surface, sizeof(last.depth))))
	{
		color = last.color_entry;
		depth = last.depth_entry;
		/* (an entry whose target was given back for now: made again) */
		if (color && color->dormant && !render_target_wake(color))
			color = NULL;
		if (depth && depth->dormant && !render_target_wake(depth))
			depth = NULL;
	}
	else
	{
		color = targets->color_valid ? render_target_get_version(&targets->color_surface, command->color_version) : NULL;
		depth = targets->depth_valid ? render_target_get(&targets->depth_surface) : NULL;
		if (depth && !depth->target.depth)
			depth = NULL;
		if (color && color->target.depth)
			color = NULL;
		last.color = targets->color_surface;
		last.depth = targets->depth_surface;
		last.color_version = command->color_version;
		last.color_valid = targets->color_valid;
		last.depth_valid = targets->depth_valid;
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
	worker_color_entry = color;
	worker_depth_id = depth ? depth->id : 0;
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
		unsigned long levels;
	} sampled_key[D3DTSS_MAXSTAGES];
	/* the render targets the stages sample (their scenes are waited for:
	vgxm_note_sampled_target) */
	unsigned long sampled_targets[D3DTSS_MAXSTAGES] = { 0 };
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		const DWORD *header = command->texture_header[stage];
		unsigned long mode = stage_texture_mode_of(&command->key, stage);
		const struct vgxm_texture *source = NULL;
		struct xgpu_texture_description description;
		struct render_target_entry *target;
		unsigned long sampled_levels;
		DWORD *state = command->sampler_state[stage];

		texture_scale[stage][0] = texture_scale[stage][1] = 1.0f;
		texture_scale[stage][2] = texture_scale[stage][3] = 1.0f;
		command->draw.textures[stage] = NULL;
		command->key.sampler_type[stage] = _xgpu_sampler_none;
		command->key.volume_slices_log2[stage] = command->key.volume_width_log2[stage] = 0;
		if (!command->texture_present[stage] || !header[1] || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
			continue;
		target = render_target_entry_find_version(header[1], command->texture_version[stage]);
		if (target && !target->drawn && !target->chain_levels)
		{
			/* a target nothing has been drawn into yet: its memory is the
			zeros it was made with. The screen effects' copies are drawn
			by one pass and sampled by the next; when the copy's draw was
			left out (its program still compiling, or a compile that
			failed late in a session) the next pass drew the zeros over
			the whole screen - a zoomed scope went black. Left out as
			well, so the picture stays as it was. */
			static unsigned long logged;

			if (logged++ < 8)
				platform_log("draw left out: stage %d samples render target %08lx before anything was drawn into it",
					stage, (unsigned long)header[1]);
			command->skip = TRUE;
			continue;
		}
		if (target)
		{
			target->last_used = device.frame + 1;
			sampled_targets[stage] = target->id;
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
			if (command->targets->color_valid && target->target.data == command->targets->color_surface.Data)
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
		else if (render_target_entry_known(header[1]) && camo_target_enabled() && command->key.texture_modes == 0x2623 &&
			stage == 2 && command->targets && command->targets->color_valid &&
			(target = render_target_entry_find(command->targets->color_surface.Data)) != NULL)
		{
			/* (camo_fallback) the active camouflage's distortion pass, whose
			copy of the screen has no target: it samples the target it
			draws into instead - what the GPU last stored of the picture,
			the coordinates normalised as the copy's would be - so a cloaked
			unit shows as a shimmer rather than not at all */
			static unsigned long logged;

			if (logged++ < 4)
				platform_log("active camouflage: the copy %08lx has no target, the distortion samples the picture",
					(unsigned long)header[1]);
			target->last_used = device.frame + 1;
			sampled_targets[stage] = target->id;
			xgpu_texture_describe(header[3], header[4], &description);
			if (description.linear && description.width && description.height)
			{
				texture_scale[stage][0] = 1.0f / (float)description.width;
				texture_scale[stage][1] = 1.0f / (float)description.height;
			}
			source = &target->texture;
			description.levels = 1;
			description.cube_map = FALSE;
			stats.self_sampled++;
		}
		else if (render_target_entry_known(header[1]))
		{
			/* a surface the game renders into that has no target now (none
			could be made): its own memory is never written - the targets
			are the GPU's - so sampling it reads zeros, which drew the
			active camouflage black. The draw is left out instead (a
			cloaked unit is then simply unseen) */
			static unsigned long logged;

			if (logged++ < 8)
				platform_log("draw left out: stage %d samples render target %08lx, which has no target", stage,
					(unsigned long)header[1]);
			command->skip = TRUE;
			continue;
		}
		else
		{
			/* (an immediate draw's - the HUD, the menus, sprites, the screen
			effects - is whole at once, as the texture streaming waits for
			theirs: a lower level stood in as blocks) */
			source = vita_texture_get(header, command->palette[stage], &description, !command->immediate);
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
		/* a chained target samples the levels the game declares now, and
		only its first with no mip filter (D3DTEXF_NONE: GXM's mip filter
		off still picks the nearest of all the texture's levels) */
		sampled_levels = 0;
		if (target && target->chain_levels > 1 &&
			(state[2] == D3DTEXF_NONE || description.levels < (unsigned long)target->chain_levels))
		{
			sampled_levels = state[2] == D3DTEXF_NONE ? 1 : description.levels;
		}
		if (sampled_key[stage].source != source ||
			memcmp(sampled_key[stage].control, source->control, sizeof(source->control)) ||
			memcmp(sampled_key[stage].state, state, sizeof(sampled_key[stage].state)) ||
			sampled_key[stage].levels != sampled_levels)
		{
			sampled[stage] = *source;
			vgxm_texture_set_sampler(&sampled[stage], state[0], state[1], state[2], state[3], state[4],
				dword_to_float(state[5]));
			if (sampled_levels)
				vgxm_texture_set_level_count(&sampled[stage], sampled_levels);
			sampled_key[stage].source = source;
			memcpy(sampled_key[stage].control, source->control, sizeof(source->control));
			memcpy(sampled_key[stage].state, state, sizeof(sampled_key[stage].state));
			sampled_key[stage].levels = sampled_levels;
		}
		command->draw.textures[stage] = &sampled[stage];
		command->key.sampler_type[stage] = description.cube_map ? _xgpu_sampler_cube :
			description.depth > 1 ? _xgpu_sampler_3d : _xgpu_sampler_2d;
		if (command->key.sampler_type[stage] == _xgpu_sampler_3d && !description.compressed && !description.linear &&
			description.width * description.depth <= 4096)
		{
			/* (its slices side by side: vita_textures.c) */
			unsigned char slices = 0, width = 0;

			while ((2UL << slices) <= description.depth)
				slices++;
			while ((2UL << width) <= description.width)
				width++;
			command->key.volume_slices_log2[stage] = slices;
			command->key.volume_width_log2[stage] = width;
		}
	}
	if (!command->skip)
		for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
			vgxm_note_sampled_target(sampled_targets[stage]);
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
		if (state->textures_present & (1UL << stage))
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
		const DWORD *bump = state->values->bump->bump[stage];

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

		command->texture_present[stage] = (state->textures_present >> stage) & 1;
		memcpy(command->texture_header[stage], state->texture_header[stage], sizeof(command->texture_header[stage]));
		command->palette[stage] = state->palette_data[stage];
		if (state->textures_present & (1UL << stage))
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
		key_border(key, stage, ts);
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
	draw->blend_color = rs[D3DRS_BLENDCOLOR];
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

static void fill_stats_draw(const struct render_command *command, const struct vgxm_draw *draw)
{
	halo_fill_stats_draw(draw, command->program->instructions, command->program->instruction_count,
		command->program->input_mask, command->immediate ? 0 : command->packed_mask,
		command->immediate ? 0 : command->color_mask, command->phase,
		worker_color_entry ? worker_color_entry->id : 0, worker_depth_id);
}

/* a draw to GXM, once per visibility test segment as recorded; without
count_visibility the draw counts for no test (the first of a blend's two
passes: blend_constant.h) */
static void issue_draw(const struct render_command *command, struct vgxm_draw *draw, int count_visibility)
{
	if (!count_visibility)
		draw->visibility_index = 0;
	if (command->segment_count > 1)
	{
		/* one draw per visibility test, as they were recorded: the same
		state, each its own triangles and slot */
		const unsigned short *indices = draw->indices;
		unsigned long index_count = draw->index_count, visibility_index = draw->visibility_index, segment;

		for (segment = 0; segment < command->segment_count; segment++)
		{
			draw->indices = indices + command->segments[segment].first_index;
			draw->index_count = command->segments[segment].index_count;
			draw->visibility_index = count_visibility ? command->segments[segment].visibility_index : 0;
			if (halo_fill_stats_sampled())
				fill_stats_draw(command, draw);
			vgxm_draw(draw);
		}
		draw->indices = indices;
		draw->index_count = index_count;
		draw->visibility_index = visibility_index;
	}
	else
	{
		if (halo_fill_stats_sampled())
			fill_stats_draw(command, draw);
		vgxm_draw(draw);
	}
}

/* the fragment uniforms' second buffer of a draw whose blend's constant is
folded into its program: a copy with the fold's two rows after it */
static const void *blend_fold_uniforms(const void *second, const struct blend_pass *pass)
{
	float (*copy)[4] = vgxm_worker_alloc((VITA_FU_FOLD_COUNT - VITA_FU_A_COUNT) * sizeof(float) * 4, 16);

	if (!copy)
		return NULL;
	memcpy(copy, second, (VITA_FU_COUNT - VITA_FU_A_COUNT) * sizeof(float) * 4);
	memcpy(copy[VITA_FU_BLEND_SCALE - VITA_FU_A_COUNT], pass->scale, sizeof(float) * 4);
	memcpy(copy[VITA_FU_BLEND_OFFSET - VITA_FU_A_COUNT], pass->offset, sizeof(float) * 4);
	return copy;
}

/* the log's line for each kind of blend made of GXM's factors, once */
static void blend_plan_note(const struct vgxm_draw *draw, const struct blend_plan *plan)
{
	static unsigned long noted[32];
	static int noted_count;
	unsigned long kind = draw->blend_source << 17 ^ draw->blend_destination << 2 ^ draw->blend_operation << 20 ^
		(unsigned long)plan->pass_count ^ (unsigned long)!plan->exact << 31;
	int index;

	for (index = 0; index < noted_count; index++)
		if (noted[index] == kind)
			return;
	if (noted_count >= 32)
		return;
	noted[noted_count++] = kind;
	platform_log("blend %lu/%lu op %lu, constant %08lx, writes %08lx: %s%s", draw->blend_source, draw->blend_destination,
		draw->blend_operation, draw->blend_color, draw->color_write, plan->how,
		plan->exact ? "" : " (GXM's factors cannot make it: an approximation)");
}

static void execute_draw(struct render_command *command)
{
	struct vgxm_draw *draw = &command->draw;
	float texture_scale[4][4];
	BOOL has_depth;
	int stage;
	/* (a blend GXM cannot take as it is: blend_constant.h) */
	struct blend_plan blend_plan;
	int blend_passes = 0;
	unsigned long plain_shader = 0, fold_shader = 0;

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
	if (draw->blend)
		blend_passes = blend_constant_plan(draw->blend_source, draw->blend_destination, draw->blend_operation,
			draw->color_write, draw->blend_color, &blend_plan);
	if (blend_passes)
	{
		/* the program as it is and/or its fold, as the passes need */
		int pass, plain = 0, fold = 0;

		for (pass = 0; pass < blend_plan.pass_count; pass++)
		{
			if (blend_plan.passes[pass].fold)
				fold = 1;
			else
				plain = 1;
		}
		if (plain)
			plain_shader = fragment_shader_get(&command->key);
		if (fold)
		{
			struct nv2a_pixel_shader_key fold_key = command->key;

			fold_key.blend_fold = 1;
			fold_shader = fragment_shader_get(&fold_key);
		}
		/* (none drawn until both are ready: one pass alone is wrong) */
		draw->fragment_shader = (plain && !plain_shader) || (fold && !fold_shader) ? 0 : plain ? plain_shader : fold_shader;
		blend_plan_note(draw, &blend_plan);
	}
	else
		draw->fragment_shader = fragment_shader_get(&command->key);
	if (command->sky_depth)
	{
		/* (after the record's states are built: they are shared and
		cached by state block, the sky's own say no depth test) */
		draw->depth_test = has_depth;
		draw->depth_write = 0;
		draw->depth_function = D3DCMP_LESSEQUAL;
		draw->viewport_scale[2] = 0.0f;
		draw->viewport_offset[2] = 1.0f;
	}
	{
		/* (debug) HALO_TRACE_CAMO=n: the first n draws of the active
		camouflage (rasterizer_xbox_active_camouflage.c): the screen copy
		into the secondary target (modes 1, a 320x240 target), the cloaked
		model's depth pass (no colour writes) and its distortion pass
		(modes 0x2623), with their state */
		static long trace = -1, traced;

		if (trace < 0)
		{
			const char *setting = getenv("HALO_TRACE_CAMO");
			trace = setting ? atol(setting) : 0;
		}
		if (trace && traced < trace && command->targets && command->targets->color_valid)
		{
			unsigned long width = 0, height = 0;
			BOOL depth_surface;
			BOOL distortion = command->key.texture_modes == 0x2623;
			BOOL depth_pass = draw->color_write == 0 && command->key.alpha_test_function && command->key.texture_modes == 1;

			surface_dimensions(&command->targets->color_surface, &width, &height, &depth_surface);
			if (distortion || (width == 320 && height == 240) || depth_pass)
			{
				const float *b = (const float *)draw->vertex_chunks[VITA_VC_B];
				const float *fa = (const float *)draw->fragment_uniforms[0];
				const float *fb = (const float *)draw->fragment_uniforms[1];

				traced++;
				platform_log("camo trace %ld: %s frame %lu target %08lx %lux%lu vs %lu ps %08lx modes %08lx "
					"z %d/%d func %lu cw %08lx blend %d %lu/%lu at %u indices %lu",
					traced, distortion ? "distortion" : depth_pass ? "depth pass" : "copy", device.frame,
					(unsigned long)command->targets->color_surface.Data, width, height, command->program->id,
					hash_words(&command->key, sizeof(command->key)), (unsigned long)command->key.texture_modes,
					(int)draw->depth_test, (int)draw->depth_write, draw->depth_function, draw->color_write, (int)draw->blend,
					draw->blend_source, draw->blend_destination, (unsigned)command->key.alpha_test_function,
					(unsigned long)draw->index_count);
				for (stage = 0; stage < 4; stage++)
				{
					if (command->texture_present[stage] && command->texture_header[stage][1])
					{
						struct render_target_entry *entry = render_target_entry_find_version(command->texture_header[stage][1],
							command->texture_version[stage]);

						platform_log("  stage %d: data %08lx version %lu %s sampler %d scale %.6f %.6f bound %d",
							stage, (unsigned long)command->texture_header[stage][1], command->texture_version[stage],
							entry ? "render target" : "texture", command->key.sampler_type[stage],
							texture_scale[stage][0], texture_scale[stage][1], draw->textures[stage] != NULL);
					}
				}
				{
					unsigned long a;

					for (a = 0; a < draw->attribute_count; a++)
						platform_log("  attribute v%u: format %u components %u stream %u offset %u stride %lu",
							draw->attributes[a].reg, draw->attributes[a].format, draw->attributes[a].components,
							draw->attributes[a].stream, draw->attributes[a].offset,
							(unsigned long)draw->strides[draw->attributes[a].stream]);
				}
				if (b)
					platform_log("  c12..c14: %g %g %g %g | %g %g %g %g | %g %g %g %g",
						b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11]);
				if (fa)
					platform_log("  final c0: %g %g %g %g", fa[VITA_FU_PS_FINAL_C0 * 4], fa[VITA_FU_PS_FINAL_C0 * 4 + 1],
						fa[VITA_FU_PS_FINAL_C0 * 4 + 2], fa[VITA_FU_PS_FINAL_C0 * 4 + 3]);
				if (fb)
					platform_log("  texture scale 2: %g %g", fb[(VITA_FU_TEXTURE_SCALE - VITA_FU_A_COUNT + 2) * 4],
						fb[(VITA_FU_TEXTURE_SCALE - VITA_FU_A_COUNT + 2) * 4 + 1]);
			}
		}
	}
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
		if (index == named_count && named_count < 64 && per_pair < 8 && draw->fragment_shader)
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
	draw->vertex_program_hash = command->program->instruction_hash;
	if (blend_passes)
	{
		/* each pass with its factors and program; the first of two only
		multiplies the colour target: the depth, stencil and visibility
		test are the second's */
		int pass;

		for (pass = 0; pass < blend_plan.pass_count; pass++)
		{
			const struct blend_pass *plan_pass = &blend_plan.passes[pass];
			struct vgxm_draw pass_draw = *draw;
			int first_of_two = blend_plan.pass_count == 2 && pass == 0;

			pass_draw.blend_source = plan_pass->source;
			pass_draw.blend_destination = plan_pass->destination;
			pass_draw.blend_operation = plan_pass->operation;
			pass_draw.fragment_shader = plan_pass->fold ? fold_shader : plain_shader;
			if (plan_pass->fold)
			{
				pass_draw.fragment_uniforms[1] = blend_fold_uniforms(draw->fragment_uniforms[1], plan_pass);
				if (!pass_draw.fragment_uniforms[1])
					return;
			}
			if (first_of_two)
			{
				pass_draw.depth_write = 0;
				pass_draw.stencil_fail = pass_draw.stencil_depth_fail = pass_draw.stencil_pass = D3DSTENCILOP_KEEP;
			}
			issue_draw(command, &pass_draw, !first_of_two);
		}
	}
	else
		issue_draw(command, draw, 1);
	if (worker_color_entry && draw->color_write)
		worker_color_entry->drawn = TRUE;
	DRAW_PROFILE_ADD(7, profile_from);
}

static void write_screenshot(struct render_target_entry *target);
static void write_display_screenshot(unsigned long frame);
int halo_trace_active(void);
static void dynres_frame_end(void);

static void execute_command(struct render_command *command)
{
	/* (timed for the statistics only, as layer_enter) */
	int timed = gpu_stats_on > 0;
	unsigned long long before = timed ? vita_host_time_us() : 0;
	BOOL has_depth;

	switch (command->kind)
	{
	case _command_draw:
		if (command->state)
		{
			/* A split record is expanded into a copy of what the game's
			thread wrote, not into its ring entry: the expansion (the key,
			texture headers, samplers and states, ~600 bytes) was written into
			the entry and read back once, right here, but the entry is cold -
			the ring holds two frames, ~6 MB - so each draw wrote ~19 lines no
			cache held, ~1 MB a frame streamed through the 512 KB L2 the
			game's and tick's threads share with the worker. The copy is
			hot. (A record built in full on the game's thread has its
			expansion in the entry already: run in place.) */
			static struct render_command expanded;
			const struct vgxm_draw *recorded = &command->draw;
			unsigned long streams = recorded->stream_count < VGXM_STREAM_COUNT ? recorded->stream_count : VGXM_STREAM_COUNT;
			unsigned long attributes = recorded->attribute_count < VGXM_ATTRIBUTE_COUNT ? recorded->attribute_count :
				VGXM_ATTRIBUTE_COUNT;

			memcpy(&expanded, command, offsetof(struct render_command, draw) + offsetof(struct vgxm_draw, strides));
			memcpy(expanded.draw.strides, recorded->strides, streams * sizeof(recorded->strides[0]));
			memcpy(expanded.draw.streams, recorded->streams, streams * sizeof(recorded->streams[0]));
			memcpy(expanded.draw.attributes, recorded->attributes, attributes * sizeof(recorded->attributes[0]));
			execute_draw(&expanded);
		}
		else
			execute_draw(command);
		break;
	case _command_clear:
		if (bind_recorded_targets(command, &has_depth))
		{
			unsigned long flags = command->clear_flags;

			if (!has_depth)
				flags &= ~(D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL);
			vgxm_clear(flags, command->clear_color, command->clear_depth, command->clear_stencil, command->clip);
			if (halo_fill_stats_sampled())
				halo_fill_stats_clear(flags, command->clear_depth, command->clip, worker_depth_id);
			if (worker_color_entry && (flags & D3DCLEAR_TARGET))
				worker_color_entry->drawn = TRUE;
		}
		break;
	case _command_present:
	{
		struct render_target_entry *back_buffer = command->targets->color_valid ? render_target_get(&command->targets->color_surface) : NULL;

		if (halo_trace_active())
			platform_log("trace: worker present %lu", command->frame);
		if (back_buffer)
		{
			if (command->screenshot)
				write_screenshot(back_buffer);
			/* (the frame's visibility counts: the game's frame they are of) */
			vgxm_visibility_frame(command->frame);
			vgxm_present(back_buffer->id, back_buffer->target.width, back_buffer->target.height);
			halo_fill_stats_present(back_buffer->id, back_buffer->target.width, back_buffer->target.height);
			if (command->screenshot)
				write_display_screenshot(command->frame);
		}
		if (halo_trace_active())
			platform_log("trace: worker presented %lu", command->frame);
		/* (the next frame's render scale, between the two) */
		dynres_frame_end();
		render_target_atlas_frame_end();
		{
			/* (the census during play: cdram_census) */
			static int census_done;

			if (!census_done && command->frame >= cdram_census_frame())
			{
				census_done = 1;
				cdram_census("during play");
			}
		}
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
#ifdef HALO_VITA
	vita_host_thread_watch("render worker");
#endif
	{
		/* the records of the frame that are not hoisted, run at its present,
		and the hoisted ones of the later waves (small_target_wave), run
		before them */
		static unsigned long deferred[COMMAND_RING], waved[COMMAND_RING];
		unsigned long deferred_count = 0, waved_count = 0, index = 0;
		unsigned int last_wave = 0;
		BOOL submit_begun = FALSE;

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
			/* (a present, or a record before which what came before is
			run: a small target's copies ran out, target_versions_ran_out) */
			if (command->kind == _command_present || command->flush_before)
			{
				unsigned long each;
				unsigned int wave;

				/* (the frame's main work goes to the GPU from here: the
				dynamic resolution's GPU time) */
				if (!submit_begun)
					vgxm_frame_submit_begin();
				submit_begun = TRUE;

				for (wave = 1; wave <= last_wave; wave++)
					for (each = 0; each < waved_count; each++)
						if (commands[waved[each] % COMMAND_RING].wave == wave)
							execute_command(&commands[waved[each] % COMMAND_RING]);
				waved_count = 0;
				last_wave = 0;
				for (each = 0; each < deferred_count; each++)
					execute_command(&commands[deferred[each] % COMMAND_RING]);
				deferred_count = 0;
			}
			if (command->kind == _command_present)
			{
				submit_begun = FALSE;
				execute_command(command);
				__atomic_store_n(&frames_presented, frames_presented + 1, __ATOMIC_RELEASE);
				__atomic_store_n(&command_tail, command_tail + index + 1, __ATOMIC_RELEASE);
				index = 0;
			}
			else if (command->hoistable && command->wave)
			{
				waved[waved_count++] = command_tail + index;
				if (command->wave > last_wave)
					last_wave = command->wave;
				index++;
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
	commands = lasting_alloc(COMMAND_RING * sizeof(*commands), "command ring");
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

/* Small targets drawn from the main scene. The sun glow
(rasterizer_xbox_lights.c) copies the back buffer around the sun into two
64x64 targets, blurs them into each other four times, each pass weighted
by the alpha the copy left in the target it draws into (DESTALPHA), and
draws the rays from the last one. The copies read the main scene and so
stayed in the frame's order, but the passes after them read only small
targets: they were hoisted ahead of the main scene, before the copies were
drawn, and each went into a fresh copy of its target, whose alpha nothing
had written. The blur was black and the rays added nothing (issue #31). A
small surface a run draws into from a screen-sized target is now drawn in
the game's order from then on, into one target (no copies), as on the
Xbox. HALO_SMALL_IN_ORDER=0: copied and hoisted, as before. */
#define MAXIMUM_IN_ORDER_SURFACES 8

static unsigned long in_order_surfaces[MAXIMUM_IN_ORDER_SURFACES];
static unsigned long in_order_surface_count;

static BOOL surface_in_order(unsigned long data)
{
	unsigned long index;

	for (index = 0; index < in_order_surface_count; index++)
		if (in_order_surfaces[index] == data)
			return TRUE;
	return FALSE;
}

static unsigned long *target_version_slot(unsigned long data);

/* (recording a draw) one of its stages reads this surface: a draw reading
an in-order surface is not hoisted, and a run into a copy of a small target
that reads a screen-sized target makes the small one an in-order surface
(this draw, and the rest of the frame, already without the copy) */
static void record_reads_surface(struct render_command *command, unsigned long data)
{
	const struct render_target_entry *read;
	unsigned long *version;
	static int enabled = -1;

	if (in_order_surface_count && surface_in_order(data))
	{
		command->hoistable = FALSE;
		return;
	}
	if (!command->color_version)
		return;
	if (enabled < 0)
	{
		const char *setting = getenv("HALO_SMALL_IN_ORDER");

		enabled = !setting || atoi(setting) != 0;
	}
	if (!enabled || in_order_surface_count >= MAXIMUM_IN_ORDER_SURFACES ||
		!(read = render_target_entry_find(data)) ||
		(read->target.width <= SMALL_TARGET_WIDTH && read->target.height <= SMALL_TARGET_WIDTH))
	{
		return;
	}
	in_order_surfaces[in_order_surface_count++] = command->targets->color_surface.Data;
	if ((version = target_version_slot(command->targets->color_surface.Data)) != NULL)
		*version = 0;
	command->color_version = 0;
	command->hoistable = FALSE;
	platform_log("small targets: %08lx is drawn from the screen-sized %08lx, in order and without copies",
		(unsigned long)command->targets->color_surface.Data, data);
}

static BOOL surface_is_small_cached(const D3DSurface *surface);
static BOOL surface_is_depth_cached(const D3DSurface *surface);

extern D3DBaseTexture d3d_stand_in_textures[];
extern const unsigned long d3d_stand_in_texture_count;

/* whether a stage the pixel shader reads has a texture streaming's stand-in
bound (rasterizer_xbox.c binds a texture still loading by a copy of the
default texture's header). Such a blended draw is left out until the bitmap
is in: the stand-ins are opaque white or grey, which the blended passes (the
assault rifle's compass, decals, effects) drew as white blocks. */
static BOOL draw_samples_stand_in(void)
{
	DWORD modes = D3D__RenderState[D3DRS_PSTEXTUREMODES];
	DWORD stage;
	/* (debug) HALO_STAND_IN_BLENDED=1: drawn with the stand-in, as before */
	static int blended = -1;

	if (blended < 0)
		blended = getenv("HALO_STAND_IN_BLENDED") && atoi(getenv("HALO_STAND_IN_BLENDED")) != 0;
	if (blended)
		return FALSE;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		const D3DBaseTexture *texture = device.textures[stage];

		if (((modes >> (stage * 5)) & 0x1f) && texture >= &d3d_stand_in_textures[0] &&
			texture < &d3d_stand_in_textures[d3d_stand_in_texture_count])
		{
			if (stats.stand_in_draws_left_out++ < 4)
				platform_log("blended draw left out: stage %lu samples a texture still loading", (unsigned long)stage);
			return TRUE;
		}
	}
	return FALSE;
}

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

/* A run into a small target whose copies ran out (more than 24 object
shadows in view, the armoury's racks of weapons in d40). It drew into the
last copy again, and being hoisted, ran ahead of the main scene's draws that
read that copy for the shadows before it: each of those shadows was drawn
with the last one's picture (a rifle's with a body's, stretched over the
floor), and they came and went as the count in view crossed 24. Now the
target's copies start again, and the worker first runs every record before
this one (the waves held and the main scene so far: render_worker), so
the copies are free; the main scene is split there, once each 24 copies. */
static unsigned long target_versions_ran_out_count;
static BOOL target_versions_flush_pending;

static void target_versions_ran_out(unsigned long *version)
{
	if (++target_versions_ran_out_count <= 4)
		platform_log("small targets: more than %d copies of one in a frame: the frame's records so far run first (%lu so far)",
			MAXIMUM_TARGET_VERSIONS, target_versions_ran_out_count);
	*version = 1;
	target_versions_flush_pending = TRUE;
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
	/* the visibility segments of a draw merged across visibility tests:
	where each test's triangles start in the indices, and its slot */
	struct draw_segment *segments;
	unsigned long segment_count, segment_capacity;
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
		if (held_immediate.segment_count > 1)
		{
			struct draw_segment *segments = vgxm_ring_alloc(held_immediate.segment_count * sizeof(*segments), 16);
			unsigned long segment;

			if (!segments)
				return;
			for (segment = 0; segment < held_immediate.segment_count; segment++)
			{
				segments[segment] = held_immediate.segments[segment];
				segments[segment].index_count = (segment + 1 < held_immediate.segment_count ?
					held_immediate.segments[segment + 1].first_index : held_immediate.index_count) -
					held_immediate.segments[segment].first_index;
			}
			command->segments = segments;
			command->segment_count = held_immediate.segment_count;
		}
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

/* the records' target blocks: per-frame arenas like the state blocks
(rotated with them, record_state_frame_end), the last block shared while
the targets stay the same; a present's own per arena */
#define TARGET_BLOCKS_PER_FRAME 4096
static struct record_targets *target_arenas[3];
static struct record_targets present_targets[3];
static unsigned long target_blocks_used;
static struct record_targets *targets_last;
static unsigned long state_arena_index;

static const struct record_targets *record_targets_current(const D3DSurface *color, const D3DSurface *depth)
{
	BOOL color_valid = color != NULL, depth_valid = depth != NULL;
	struct record_targets *block;

	if (targets_last && targets_last->color_valid == color_valid && targets_last->depth_valid == depth_valid &&
		(!color_valid || !memcmp(&targets_last->color_surface, color, sizeof(*color))) &&
		(!depth_valid || !memcmp(&targets_last->depth_surface, depth, sizeof(*depth))))
	{
		return targets_last;
	}
	if (!target_arenas[0])
	{
		int index;

		for (index = 0; index < 3; index++)
			target_arenas[index] = lasting_alloc(TARGET_BLOCKS_PER_FRAME * sizeof(struct record_targets), "target arenas");
	}
	if (!target_arenas[state_arena_index] || target_blocks_used >= TARGET_BLOCKS_PER_FRAME)
	{
		static int warned;

		if (!warned++)
			platform_log("Direct3D: more than %d target changes in a frame: the rest are not recorded", TARGET_BLOCKS_PER_FRAME);
		return NULL;
	}
	block = &target_arenas[state_arena_index][target_blocks_used++];
	memset(block, 0, sizeof(*block));
	block->color_valid = color_valid;
	block->depth_valid = depth_valid;
	if (color_valid)
		block->color_surface = *color;
	if (depth_valid)
		block->depth_surface = *depth;
	targets_last = block;
	return block;
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
		if (kind == _command_draw && D3D__RenderState[D3DRS_ALPHABLENDENABLE] && draw_samples_stand_in())
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
	{
		const struct record_targets *targets = NULL;

		/* (a present's are set by D3DDevice_Present) */
		if (kind != _command_present && !(targets = record_targets_current(device.render_target, device.depth_stencil)))
			return NULL;
		command = &commands[command_head % COMMAND_RING];
		command->kind = kind;
		command->state = NULL;
		command->segment_count = 0;
		command->targets = targets;
	}
	command->color_version = 0;
	command->hoistable = FALSE;
	command->new_run = command->targets && command->targets->color_valid &&
		last_recorded_target != command->targets->color_surface.Data;
	command->wave = 0;
	command->flush_before = 0;
	if (kind != _command_present && command->targets->color_valid && surface_is_small_cached(&command->targets->color_surface) &&
		!(command->targets->depth_valid && device.depth_stencil->Data && !surface_is_small(&command->targets->depth_surface)) &&
		!surface_in_order(command->targets->color_surface.Data))
	{
		unsigned long *version = target_version_slot(command->targets->color_surface.Data);

		if (version)
		{
			/* a new run into the target: a fresh copy */
			if (last_recorded_target != command->targets->color_surface.Data && *version < MAXIMUM_TARGET_VERSIONS)
				(*version)++;
			else if (last_recorded_target != command->targets->color_surface.Data)
				target_versions_ran_out(version);
			command->color_version = *version;
			command->hoistable = *version > 0;
		}
	}
	last_recorded_target = command->targets && command->targets->color_valid ? command->targets->color_surface.Data : 0;
	return command;
}

/* The worker runs a hoisted record (a run of draws into a copy of a small
target: the object shadows, the glow...) when it gets it, ahead of the
frame's main scene. Each run into a small target that samples another one
drawn this frame waits on the GPU for the scene that drew it
(HALO_GXM_RTT_SYNC), and the game draws each object's shadow and blurs it
before the next shadow: in a fight, 24 shadows were 48 scenes and 25 waits,
each wait stopping the GPU's vertex work until the scenes before it were
drawn. The hoisted records are now run in waves: a record that samples a
small target drawn this frame goes one wave after that target's records, so
the shadows are drawn one after another, then all their blurs, with one
wait between. Wave 0 runs as it comes, as before; the later waves at the
present, in order, before the main scene; within a wave, in the game's
order. What each target receives, and in which order, is unchanged: a run
that draws into a copy again (the copies run out at 24) or into one a
later wave still reads goes after those readers. HALO_TARGET_WAVES=0: every
hoisted record runs as it comes, as before. */
#define MAXIMUM_WAVES 48

static struct
{
	unsigned long data, version;
	unsigned char write_wave, read_wave, written;
} wave_targets[128];
static unsigned long wave_target_count;
static BOOL wave_overflow;

static int target_waves_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		const char *setting = getenv("HALO_TARGET_WAVES");

		enabled = !setting || atoi(setting) != 0;
		if (!enabled)
			platform_log("small targets: drawn in the game's order (HALO_TARGET_WAVES=0)");
	}
	return enabled;
}

static int wave_target_find(unsigned long data, unsigned long version, BOOL add)
{
	unsigned long index;

	for (index = 0; index < wave_target_count; index++)
		if (wave_targets[index].data == data && wave_targets[index].version == version)
			return (int)index;
	if (!add || wave_target_count >= sizeof(wave_targets) / sizeof(wave_targets[0]))
		return -1;
	memset(&wave_targets[wave_target_count], 0, sizeof(wave_targets[0]));
	wave_targets[wave_target_count].data = data;
	wave_targets[wave_target_count].version = version;
	return (int)wave_target_count++;
}

static void small_target_wave(struct render_command *command)
{
	unsigned int wave = 0;
	int target, stage;

	command->wave = 0;
	if (command->kind == _command_present)
	{
		wave_target_count = 0;
		wave_overflow = FALSE;
		return;
	}
	if (!command->hoistable || !target_waves_enabled())
		return;
	target = wave_target_find(command->targets->color_surface.Data, command->color_version, TRUE);
	if (target < 0 || wave_overflow)
	{
		/* (out of room: this and every later hoisted record of the frame
		go last, in order, which keeps every order that matters) */
		wave_overflow = TRUE;
		command->wave = MAXIMUM_WAVES - 1;
		return;
	}
	/* after what was drawn into the copy before and, for a run that draws
	it anew, after the records that read what was there */
	if (wave_targets[target].written)
	{
		wave = wave_targets[target].write_wave;
		if (command->new_run && wave_targets[target].read_wave > wave)
			wave = wave_targets[target].read_wave;
	}
	else if (wave_targets[target].read_wave > wave)
		wave = wave_targets[target].read_wave;
	/* a wave after the copies it samples */
	if (command->kind == _command_draw)
	{
		for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		{
			int read;

			if (!command->texture_version[stage] || !command->texture_target_data[stage])
				continue;
			read = wave_target_find(command->texture_target_data[stage], command->texture_version[stage], FALSE);
			/* (a run reading the copy it draws into: no order to keep) */
			if (read == target)
				continue;
			if (read >= 0 && wave_targets[read].written && wave_targets[read].write_wave + 1u > wave)
				wave = wave_targets[read].write_wave + 1u;
		}
	}
	if (wave >= MAXIMUM_WAVES - 1)
	{
		wave_overflow = TRUE;
		wave = MAXIMUM_WAVES - 1;
	}
	command->wave = (unsigned char)wave;
	wave_targets[target].write_wave = (unsigned char)wave;
	wave_targets[target].written = 1;
	if (command->kind == _command_draw)
	{
		for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		{
			int read;

			if (!command->texture_version[stage] || !command->texture_target_data[stage])
				continue;
			read = wave_target_find(command->texture_target_data[stage], command->texture_version[stage], TRUE);
			if (read >= 0 && read != target && wave_targets[read].read_wave < wave)
				wave_targets[read].read_wave = (unsigned char)wave;
		}
	}
}

static void command_commit(struct render_command *command)
{
	if (target_versions_flush_pending)
	{
		/* (the first record after the copies started again: the waves
		start again with it) */
		target_versions_flush_pending = FALSE;
		command->flush_before = 1;
		wave_target_count = 0;
		wave_overflow = FALSE;
	}
	if (worker_enabled)
	{
		small_target_wave(command);
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
			/* (read each time: the settings panel's Dev page switches it) */
			const char *setting = getenv("HALO_HANG_CRASH");
			int crash = setting && atoi(setting) != 0;

			hung_frame = frames_requested;
			if (crash)
				*(volatile int *)32 = 0;
			platform_log("waited 12 s for the worker to present frame %lu (shaders compiling?): waiting on", frames_requested);
			waited_from = vita_host_time_us();
		}
	}
}

/* (the tick thread, tick_thread.c halo_tick_wait_for_render: the main
thread is waiting for the tick and records nothing) waits until the worker
has carried out every frame recorded and the GPU has drawn them. The GPU
reads the structure bsp's vertices and indices where the tag data holds
them, and runs up to two frames behind the worker, which runs a frame
behind the game: a switch_bsp that cleared the bsp (0xCD) and read the next
one in while those frames were still being drawn drew the last frame before
the load - the one left on the display for the whole load - with the
level's geometry gone: a black screen on every section change on the
hardware (#20), where the GPU is the frame's bottleneck. */
void halo_render_wait_for_gpu(void)
{
	unsigned long long started = vita_host_time_us();

	if (worker_enabled > 0)
	{
		while (__atomic_load_n(&frames_presented, __ATOMIC_ACQUIRE) < frames_requested)
			vita_host_sleep_us(100);
	}
	vgxm_wait_gpu_idle();
	platform_log("structure bsp switch: waited %llu us for the worker and the GPU",
		(unsigned long long)(vita_host_time_us() - started));
}

/* ---------- the screen's targets changed while the game runs

(halo_screen_commit, the game's thread, after a frame is presented) A new
render scale or screen width from the settings panel: once the worker has
carried out every frame recorded and the GPU has drawn them, each target of
a screen-sized surface (480 lines, 640 columns or more: the back buffer, its
depth, the screen effects' copies of it) gives its memory back, and all are
made again at the new scale (vita_gxm.c target_make) and, those as wide as
the screen, at the new width; their entries keep their ids and surfaces
(the surfaces' new size is their new key), so nothing else changes hands.
The back buffer's and depth buffer's surfaces describe the new width (their
memory was made for 848 columns or more: CreateDevice); the game moves its
own bounds and copies after this returns (rasterizer_screen_width_update),
before it records the next frame. If the targets cannot all be had at the
new size (CDRAM taken or in pieces), they are made at the old one again,
the screen keeps it, and the settings panel says the change waits for a
restart (halo_screen_restart_needed). */

static int screen_restart_needed;

int halo_screen_restart_needed(void)
{
	return screen_restart_needed;
}

static int screen_sized_entry(const struct render_target_entry *entry)
{
	return entry->id && !entry->cell && entry->chain_levels <= 0 && entry->target.height == SCREEN_HEIGHT &&
		entry->target.width >= 640;
}

/* makes every screen-sized target at the render scale (vgxm) and, those
old_width wide, new_width wide; 1 if all were made. On failure the ones
made are given back and nothing is changed (the caller makes them again at
the old size) */
static int screen_targets_make(unsigned long old_width, unsigned long new_width, unsigned long *made)
{
	struct render_target_entry *entry;
	int all = TRUE;

	*made = 0;
	/* all given back first, so the new ones need not fit beside the old */
	for (entry = render_targets; entry; entry = entry->next)
		if (screen_sized_entry(entry))
			vgxm_target_release(entry->id);
	for (entry = render_targets; entry && all; entry = entry->next)
	{
		unsigned long width;

		/* (one given back for now is made when next drawn into, at the
		new size: render_target_wake) */
		if (!screen_sized_entry(entry) || entry->dormant)
			continue;
		width = entry->target.width == old_width ? new_width : entry->target.width;
		if (!vgxm_target_remake(entry->id, width, SCREEN_HEIGHT, entry->target.depth, &entry->texture))
			all = FALSE;
		else
			(*made)++;
	}
	if (!all)
	{
		for (entry = render_targets; entry; entry = entry->next)
			if (screen_sized_entry(entry))
				vgxm_target_release(entry->id);
	}
	return all;
}

/* the entries of the screen-sized surfaces old_width wide: new_width wide
now (their memory already is: screen_targets_make), and as yet undrawn */
static void screen_entries_rekey(unsigned long old_width, unsigned long new_width)
{
	struct render_target_entry *entry;

	for (entry = render_targets; entry; entry = entry->next)
	{
		if (!screen_sized_entry(entry))
			continue;
		if (entry->target.width == old_width)
		{
			entry->target.width = new_width;
			entry->target.gl_width = new_width;
		}
		/* (zeros until the next frame draws them: not sampled before) */
		entry->drawn = FALSE;
		entry->last_rendered = 0;
		/* (last_used stays: a copy unused for ten seconds is still one, for
		screen_targets_doze) */
	}
}

/* ---------- CDRAM when it runs short

(the game's thread, with the worker and the GPU idle: a live change of the
screen's targets, or a target that found no CDRAM - halo_screen_commit)
What can be given back, safest first (vgxm_memory.h): the block cache, the
small targets' blocks every share of which is back, the targets no entry
refers to (the copies moved into atlases, the levels a mip chain replaced),
the screen-sized targets no frame has used for ten seconds (the zoom's and
the pause menu's copies, made again when next drawn into:
render_target_wake), then the texture pool's segments in CDRAM, one at a
time from the top, each moved to user RAM (the textures in it decoded again
as they are used). */

/* a target allocation a frame made that found no CDRAM, noted before a
live change (whose own attempts' refusals are not kept): cdram_wanted_relieve */
static unsigned long cdram_wanted_pending;

/* HALO_CDRAM_RELIEF=0 (debug): nothing given back when CDRAM runs short
(as before: a live change that does not fit waits for a restart) */
static BOOL cdram_relief_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		const char *setting = getenv("HALO_CDRAM_RELIEF");

		enabled = !setting || atoi(setting) != 0;
		if (!enabled)
			platform_log("cdram: nothing given back when it runs short (HALO_CDRAM_RELIEF=0)");
	}
	return enabled != 0;
}

/* a screen-sized target unused for idle frames (ten seconds; 0: any; not
the back buffer nor its depth) given back, its entry dormant; the count */
static unsigned long screen_targets_doze(unsigned long idle)
{
	struct render_target_entry *entry;
	unsigned long count = 0;

	for (entry = render_targets; entry; entry = entry->next)
	{
		if (!screen_sized_entry(entry) || entry->dormant || (idle && entry->last_used + idle >= device.frame) ||
			(entry->target.data == device.back_buffer.Data && !entry->target.depth) ||
			(entry->target.data == device.depth_buffer.Data && entry->target.depth))
		{
			/* (the zoom's copy is a colour surface over the depth buffer's
			memory: not the depth buffer) */
			continue;
		}
		vgxm_target_release(entry->id);
		entry->dormant = TRUE;
		entry->wake_frame = 0;
		entry->drawn = FALSE;
		count++;
	}
	return count;
}

/* the targets no entry refers to given back (vgxm_targets_sweep) */
static unsigned long targets_unreferenced_sweep(void)
{
	static unsigned char referenced[VGXM_MAXIMUM_TARGETS + 1];
	struct render_target_entry *entry;

	memset(referenced, 0, sizeof(referenced));
	for (entry = render_targets; entry; entry = entry->next)
		if (entry->id && entry->id <= VGXM_MAXIMUM_TARGETS)
			referenced[entry->id] = 1;
	if (camo_reserved_id && camo_reserved_id <= VGXM_MAXIMUM_TARGETS)
		referenced[camo_reserved_id] = 1;
	return vgxm_targets_sweep(referenced, sizeof(referenced));
}

/* what costs no texture: the bytes freed (the dozing targets' at the
screen's next remake) */
static unsigned long cdram_relieve(unsigned long *dozed)
{
	unsigned long freed = targets_unreferenced_sweep();

	*dozed = screen_targets_doze(300);
	return freed + vgxm_memory_trim();
}

/* one texture pool segment moved to user RAM and its textures forgotten;
the CDRAM bytes freed (0: none left in CDRAM) */
static unsigned long cdram_relieve_pool(void)
{
	void *base;
	unsigned long size, freed, textures = 0;

	/* (no decode in the background may write into the moved memory) */
	vita_texture_decodes_quiesce();
	freed = vgxm_pool_demote(&base, &size);

	if (freed && size)
		textures = vita_texture_cache_forget(base, size);
	if (freed)
		platform_log("cdram: a texture pool part (%lu KB) moved out of CDRAM, %lu textures to decode again", freed / 1024,
			textures);
	return freed;
}

/* the texture pool's segments in user RAM moved back to CDRAM while the
targets keep their headroom */
static void cdram_pool_return(void)
{
	void *base;
	unsigned long size, moved = 0, textures = 0;

	vita_texture_decodes_quiesce();
	while (vgxm_pool_promote(&base, &size))
	{
		moved++;
		if (size)
			textures += vita_texture_cache_forget(base, size);
	}
	if (moved)
		platform_log("cdram: %lu texture pool segments back in CDRAM, %lu textures to decode again", moved, textures);
}

/* the census (HALO_CDRAM_CENSUS=1, or =<frame> for when, or performance
logging): what holds CDRAM, at a frame of play (900 by default, the worker's
present) and after each live change; with HALO_CDRAM_CENSUS, a line per
target with what it is */
static int cdram_census_level(void)
{
	static int level = -1;

	if (level < 0)
	{
		const char *setting = getenv("HALO_CDRAM_CENSUS");

		level = setting && atoi(setting) > 0 ? 2 : (getenv("HALO_PERF_LOG") && atoi(getenv("HALO_PERF_LOG"))) ||
			(getenv("HALO_FRAME_TIMING") && atoi(getenv("HALO_FRAME_TIMING")) > 0) ? 1 : 0;
	}
	return level;
}

static unsigned long cdram_census_frame(void)
{
	const char *setting = getenv("HALO_CDRAM_CENSUS");

	return setting && atol(setting) > 1 ? (unsigned long)atol(setting) : 900;
}

static void cdram_census(const char *when)
{
	static char roles[VGXM_MAXIMUM_TARGETS + 1][80];
	static const char *role_of[VGXM_MAXIMUM_TARGETS + 1];
	struct render_target_entry *entry;
	int level = cdram_census_level();

	if (!level)
		return;
	memset(role_of, 0, sizeof(role_of));
	for (entry = render_targets; entry; entry = entry->next)
	{
		unsigned long id = entry->id;

		if (!id || id > VGXM_MAXIMUM_TARGETS || role_of[id])
			continue;
		snprintf(roles[id], sizeof(roles[id]), "%s%s %08lx v%lu, used %ld frames ago%s",
			entry->target.data == device.back_buffer.Data && !entry->target.depth ? "the back buffer" :
			entry->target.data == device.depth_buffer.Data && entry->target.depth ? "the depth buffer" :
			camo_copy_size(entry->target.width, entry->target.height, entry->target.depth) ? "the camouflage's copy" :
			screen_sized_entry(entry) ? "a copy of the screen" : entry->chain_levels ? "a mip chain level" :
			entry->cell ? "an atlas cell" : "a surface",
			entry->dormant ? " (given back for now)" : "", (unsigned long)entry->target.data, entry->version,
			(long)device.frame - (long)entry->last_used, entry->drawn || entry->target.depth ? "" : ", not drawn since made");
		role_of[id] = roles[id];
	}
	if (camo_reserved_id && camo_reserved_id <= VGXM_MAXIMUM_TARGETS && !role_of[camo_reserved_id])
		role_of[camo_reserved_id] = "the camouflage's copy (reserved, not yet used)";
	platform_log("cdram census %s: %ldx%d at %.0f%%%s", when, screen_width, SCREEN_HEIGHT,
		vgxm_render_scale() * 100.0f, getenv("HALO_RENDER_SCALE") && !strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") ?
		" (dynamic)" : "");
	vgxm_memory_census(role_of, VGXM_MAXIMUM_TARGETS + 1, level >= 2);
}

/* (halo_screen_commit, after a present) a target found no CDRAM in a
frame (a scene's new surface: the effect it is for goes until it has
one): once the worker and the GPU are done, what costs no texture is given
back, then texture pool segments until that much is free; at most every
ten seconds (the entry asks again every 30 frames) */
static void cdram_wanted_relieve(void)
{
	static unsigned long relieved_frame;
	unsigned long wanted, freed, dozed, pool_parts = 0, moved;

	if (!device.created || !device.gpu_ready || (relieved_frame && device.frame < relieved_frame + 300) ||
		!cdram_relief_enabled())
	{
		return;
	}
	wanted = vgxm_cdram_wanted();
	if (cdram_wanted_pending > wanted)
		wanted = cdram_wanted_pending;
	cdram_wanted_pending = 0;
	if (!wanted)
		return;
	relieved_frame = device.frame;
	if (worker_enabled > 0)
	{
		while (__atomic_load_n(&frames_presented, __ATOMIC_ACQUIRE) < frames_requested)
			vita_host_sleep_us(100);
	}
	vgxm_wait_gpu_idle();
	freed = cdram_relieve(&dozed);
	while (vgxm_cdram_free() < wanted + 256 * 1024 && (moved = cdram_relieve_pool()) != 0)
	{
		freed += moved;
		pool_parts++;
	}
	platform_log("cdram: a target found no CDRAM (%lu KB): %lu KB given back (%lu texture pool parts moved to user "
		"RAM, %lu unused copies of the screen until next drawn), %lu KB free now", wanted / 1024, freed / 1024, pool_parts,
		dozed, vgxm_cdram_free() / 1024);
}

static void screen_settings_apply(void)
{
	const char *setting = getenv("HALO_RENDER_SCALE");
	/* (dynamic: the targets at the full size, drawn into a part of them -
	the dynamic resolution) */
	float scale = setting && strcmp(setting, "dynamic") ? (float)atof(setting) : 1.0f, old_scale = vgxm_render_scale(), drawn;
	long width = screen_width_wanted(), old_width = screen_width;
	unsigned long long started;
	unsigned long made = 0, targets, cdram, cached, cdram_free, freed = 0, dozed = 0, moved, pool_parts = 0, wanted;
	int restored = TRUE, made_new;

	if (scale < 0.5f || scale > 1.0f)
		scale = 1.0f;
	if (!device.created || !device.gpu_ready)
		return;
	if (scale == old_scale && width == old_width)
	{
		/* (back to what the screen has: nothing waits for a restart) */
		screen_restart_needed = FALSE;
		return;
	}
	if (!screen_allocated_width)
		screen_allocated_width = 848;
	if ((unsigned long)width > screen_allocated_width)
	{
		/* (wider than the back buffer's memory: at the next start) */
		platform_log("screen: %ld columns wait for a restart (the back buffer holds %lu)", width, screen_allocated_width);
		screen_restart_needed = TRUE;
		return;
	}
	started = vita_host_time_us();
	if (worker_enabled > 0)
	{
		while (__atomic_load_n(&frames_presented, __ATOMIC_ACQUIRE) < frames_requested)
			vita_host_sleep_us(100);
	}
	vgxm_wait_gpu_idle();
	/* (a frame's target that found no CDRAM, kept for cdram_wanted_relieve
	past the change's own refusals) */
	wanted = vgxm_cdram_wanted();
	if (wanted > cdram_wanted_pending)
		cdram_wanted_pending = wanted;
	drawn = vgxm_render_rect();
	vgxm_render_scale_set(scale);
	/* (to the dynamic resolution: the targets made at the full size are
	drawn at the scale they had, from which its controller starts,
	dynres_frame_end) */
	if (setting && !strcmp(setting, "dynamic") && drawn < scale)
		vgxm_render_rect_set(drawn);
	{
		/* (debug) HALO_CDRAM_DOZE_ALL=1: every copy of the screen given back
		at a live change, used or not, to be made again as it is next drawn
		into (tries render_target_wake) */
		const char *setting = getenv("HALO_CDRAM_DOZE_ALL");

		if (setting && atoi(setting))
			screen_targets_doze(0);
	}
	made_new = screen_targets_make((unsigned long)old_width, (unsigned long)width, &made);
	if (!made_new && cdram_relief_enabled())
	{
		/* (short of CDRAM: what costs nothing given back, then the
		texture pool's segments one at a time, each time tried again) */
		freed = cdram_relieve(&dozed);
		made_new = screen_targets_make((unsigned long)old_width, (unsigned long)width, &made);
		while (!made_new && (moved = cdram_relieve_pool()) != 0)
		{
			freed += moved;
			pool_parts++;
			made_new = screen_targets_make((unsigned long)old_width, (unsigned long)width, &made);
		}
	}
	if (made_new)
	{
		screen_entries_rekey((unsigned long)old_width, (unsigned long)width);
		screen_width = width;
		screen_restart_needed = FALSE;
		device.presentation.BackBufferWidth = (UINT)width;
		d3d8_surface_resize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, (unsigned long)width, SCREEN_HEIGHT);
		d3d8_surface_resize(&device.depth_buffer, D3DFMT_LIN_D24S8, (unsigned long)width, SCREEN_HEIGHT);
		/* (a smaller size: the texture pool's parts in user RAM back) */
		cdram_pool_return();
	}
	else
	{
		/* (the old size again: its memory was just given back) */
		vgxm_render_scale_set(old_scale);
		restored = screen_targets_make((unsigned long)old_width, (unsigned long)old_width, &made);
		if (!restored)
		{
			/* (should not be: the entries without a target ask for one
			again every 30 frames, render_target_get_version) */
			struct render_target_entry *entry;

			for (entry = render_targets; entry; entry = entry->next)
				if (screen_sized_entry(entry))
					entry->id = 0;
		}
		screen_entries_rekey((unsigned long)old_width, (unsigned long)old_width);
		screen_restart_needed = TRUE;
	}
	/* (the attempts' refusals are answered: not a target's in a frame) */
	vgxm_cdram_wanted();
	vgxm_target_stats(&targets, &cdram, &cached, &cdram_free);
	platform_log("screen: %ldx%d at %.0f%%%s: %lu screen-sized targets made again in %llu us; %lu target slots, "
		"targets hold %lu KB of CDRAM, %lu KB kept for other sizes, %lu KB free", screen_width, SCREEN_HEIGHT, vgxm_render_scale() * 100.0f,
		screen_restart_needed ? (restored ? " (the change waits for a restart: no memory for the new size)" :
		" (NO TARGETS: no memory for the old size either)") : "",
		made, (unsigned long long)(vita_host_time_us() - started), targets, cdram / 1024, cached / 1024,
		cdram_free / 1024);
	if (freed || dozed)
		platform_log("screen: CDRAM was short: %lu KB given back (%lu texture pool parts moved to user RAM), %lu unused "
			"copies of the screen given back until next drawn", freed / 1024, pool_parts, dozed);
	cdram_census("after a live change");
}

/* ---------- the dynamic resolution

HALO_RENDER_SCALE=dynamic (the settings panel's Render resolution: Dynamic):
the screen-sized targets are made at the full size (the render scale 1, or
the old size when a live change found no CDRAM for it: vgxm_render_scale is
the ceiling) and each frame is drawn into the top left part of them, from
HALO_DYNAMIC_RES_MIN (0.5) of the size up to the whole, in 32nds. The
scale follows the GPU's time for the frames before (vgxm_gpu_frame_next;
dynamic_resolution.c: down fast, up slowly) against the frame limit's
budget (HALO_FRAME_CAP: 33.3 ms at 30 FPS and when off, 16.7 at 60). It
changes on the worker, after a present and before the next frame's first
record: the targets' rectangles (vgxm_render_rect_set) and their entries'
textures move together, and every effect that samples the screen or a copy
of it goes through those textures in the game's own coordinates
(bind_recorded_textures: the glow, the zoom's copy, the camouflage's, the
heat haze, the water), so nothing is made again and nothing hitches.

The game's thread reads the settings (dynres_configure, at the first
present and after a change in the panel) into dynres_config and bumps its
generation; the worker takes them at its next frame end. Debug:
HALO_DYNRES_FORCE=<percent> holds that scale (with
HALO_RENDER_SCALE=dynamic: the targets at the full size);
HALO_DYNRES_CYCLE=<frames> steps 100%..50%..100% two 32nds at a time every
that many frames; HALO_DYNRES_SIM=<ms> stands in for the GPU's time, that
many ms at 100% and growing with the pixels (the null renderer times
nothing); HALO_DYNRES_BUDGET=<ms> another budget. With the performance
logging (HALO_FRAME_TIMING) the changes go to halo.log, a line a second at
most, and a summary every 300 frames. */

static struct
{
	volatile unsigned long generation;
	int enabled, floor, force, cycle, log;
	float budget_ms, sim_ms;
} dynres_config;

static struct
{
	unsigned long generation;
	int enabled, floor, force, cycle, log;
	float budget_ms, sim_ms;
	struct dynres_controller controller;
	/* the level the targets are drawn at now (32nds) */
	int level;
	unsigned long frames;
	/* the log: changes since the last line, the level then, the time of
	the last line; the summary's frames, levels, GPU times */
	unsigned long changes_pending, summary_changes;
	int logged_level;
	unsigned long long logged_us;
	unsigned long summary_frames, summary_over;
	int summary_minimum, summary_maximum;
	float summary_level_sum, summary_gpu_sum, summary_gpu_maximum;
	float last_gpu_ms, last_tail_ms;
} dynres;

static int dynres_level_of(float scale)
{
	int level = (int)(scale * DYNRES_UNITS + 0.5f);

	return level < 1 ? 1 : level > DYNRES_UNITS ? DYNRES_UNITS : level;
}

/* (the game's thread) */
static void dynres_configure(void)
{
	const char *scale = getenv("HALO_RENDER_SCALE"), *minimum = getenv("HALO_DYNAMIC_RES_MIN");
	const char *force = getenv("HALO_DYNRES_FORCE"), *cycle = getenv("HALO_DYNRES_CYCLE");
	const char *sim = getenv("HALO_DYNRES_SIM"), *budget = getenv("HALO_DYNRES_BUDGET");
	const char *cap = getenv("HALO_FRAME_CAP"), *timing = getenv("HALO_FRAME_TIMING");
	int floor = minimum ? dynres_level_of((float)atof(minimum)) : DYNRES_UNITS / 2;
	int enabled, forced = force && atoi(force) > 0 ? dynres_level_of(atoi(force) / 100.0f) : 0;
	float budget_ms = cap && atoi(cap) > 0 ? 1000.0f / (float)atoi(cap) : 1000.0f / 30.0f;

	/* (frame interpolation: the tick's budget whatever the frame limit -
	the frames between ticks are drawn only while there is time for them
	(main.c), and the resolution is not lowered to make that time; the
	panel's value, which the game reads as the next frame starts) */
	{
		int halo_interpolation_enabled(void);
		const char *interpolation = getenv("HALO_INTERPOLATION");

		if (interpolation ? !strcmp(interpolation, "true") || !strcmp(interpolation, "1") : halo_interpolation_enabled())
			budget_ms = 1000.0f / 30.0f;
	}
	if (floor < DYNRES_UNITS / 2)
		floor = DYNRES_UNITS / 2;
	if (forced && forced < DYNRES_UNITS / 2)
		forced = DYNRES_UNITS / 2;
	if (budget && atof(budget) > 1.0)
		budget_ms = (float)atof(budget);
	enabled = (scale && !strcmp(scale, "dynamic")) || forced || (cycle && atoi(cycle) > 0);
	if (enabled != dynres_config.enabled || floor != dynres_config.floor || forced != dynres_config.force ||
		(enabled && budget_ms != dynres_config.budget_ms))
		platform_log("dynamic resolution: %s%s (floor %d%%, budget %.1f ms%s%s)", enabled ? "on" : "off",
			forced ? ", held" : "", floor * 100 / DYNRES_UNITS, (double)budget_ms,
			cycle && atoi(cycle) > 0 ? ", cycling" : "", sim && atof(sim) > 0.0 ? ", GPU simulated" : "");
	dynres_config.enabled = enabled;
	dynres_config.floor = floor;
	dynres_config.force = forced;
	dynres_config.cycle = cycle && atoi(cycle) > 0 ? atoi(cycle) : 0;
	dynres_config.sim_ms = sim && atof(sim) > 0.0 ? (float)atof(sim) : 0.0f;
	dynres_config.budget_ms = budget_ms;
	dynres_config.log = timing && atoi(timing) > 0;
	__atomic_store_n(&dynres_config.generation, dynres_config.generation + 1, __ATOMIC_RELEASE);
}

/* the targets drawn at level from the next frame on (the worker, between
frames; or the game's thread with the worker idle) */
static void dynres_apply(int level)
{
	struct render_target_entry *entry;
	float applied = vgxm_render_rect_set((float)level / DYNRES_UNITS);

	for (entry = render_targets; entry; entry = entry->next)
		if (screen_sized_entry(entry) && !entry->target.depth)
			vgxm_target_texture(entry->id, &entry->texture);
	dynres.level = dynres_level_of(applied);
}

static void dynres_log_change(int from)
{
	unsigned long long now = vita_host_time_us();

	dynres.changes_pending++;
	dynres.summary_changes++;
	if (!dynres.log)
		return;
	if (!dynres.logged_us)
		dynres.logged_level = from;
	if (now - dynres.logged_us < 1000000ull)
		return;
	platform_log("dynamic resolution: %d%% (from %d%%; GPU %.1f ms, tail %.1f, budget %.1f; %lu changes in %.1f s)",
		dynres.level * 100 / DYNRES_UNITS, dynres.logged_level * 100 / DYNRES_UNITS, (double)dynres.last_gpu_ms,
		(double)dynres.last_tail_ms, (double)dynres.budget_ms, dynres.changes_pending,
		dynres.logged_us ? (now - dynres.logged_us) / 1000000.0 : 0.0);
	dynres.changes_pending = 0;
	dynres.logged_level = dynres.level;
	dynres.logged_us = now;
}

/* a frame the GPU finished, at the level it was drawn at */
static int dynres_measured(float gpu_ms, float tail_ms, int frame_level, float interval_ms)
{
	int level = dynres.controller.level;

	dynres.last_gpu_ms = gpu_ms;
	dynres.last_tail_ms = tail_ms;
	if (dynres.enabled && !dynres.force && !dynres.cycle)
		level = dynres_controller_frame(&dynres.controller, gpu_ms, tail_ms, frame_level, interval_ms);
	if (dynres.log && gpu_ms * 1000.0f < 250000.0f)
	{
		dynres.summary_frames++;
		dynres.summary_level_sum += (float)frame_level;
		dynres.summary_gpu_sum += gpu_ms;
		if (gpu_ms > dynres.summary_gpu_maximum)
			dynres.summary_gpu_maximum = gpu_ms;
		if (gpu_ms > dynres.budget_ms)
			dynres.summary_over++;
		if (!dynres.summary_minimum || frame_level < dynres.summary_minimum)
			dynres.summary_minimum = frame_level;
		if (frame_level > dynres.summary_maximum)
			dynres.summary_maximum = frame_level;
		if (dynres.summary_frames == 300)
		{
			platform_log("dynamic resolution: %lu frames at %d..%d%% (mean %.0f%%), GPU mean %.1f ms, max %.1f, "
				"%lu over the %.1f ms budget, %lu changes", dynres.summary_frames,
				dynres.summary_minimum * 100 / DYNRES_UNITS, dynres.summary_maximum * 100 / DYNRES_UNITS,
				(double)(dynres.summary_level_sum * 100.0f / DYNRES_UNITS / dynres.summary_frames),
				(double)(dynres.summary_gpu_sum / dynres.summary_frames), (double)dynres.summary_gpu_maximum,
				dynres.summary_over, (double)dynres.budget_ms, dynres.summary_changes);
			dynres.summary_frames = dynres.summary_over = dynres.summary_changes = 0;
			dynres.summary_minimum = dynres.summary_maximum = 0;
			dynres.summary_level_sum = dynres.summary_gpu_sum = dynres.summary_gpu_maximum = 0.0f;
		}
	}
	return level;
}

/* (the worker, after a present) the frames the GPU finished, and the next
frame's level */
static void dynres_frame_end(void)
{
	unsigned long generation = __atomic_load_n(&dynres_config.generation, __ATOMIC_ACQUIRE);
	struct vgxm_gpu_frame frame;
	int ceiling = dynres_level_of(vgxm_render_scale()), level;

	if (generation != dynres.generation)
	{
		int was_enabled = dynres.enabled;

		dynres.generation = generation;
		dynres.enabled = dynres_config.enabled;
		dynres.floor = dynres_config.floor;
		dynres.force = dynres_config.force;
		dynres.cycle = dynres_config.cycle;
		dynres.sim_ms = dynres_config.sim_ms;
		dynres.budget_ms = dynres_config.budget_ms;
		dynres.log = dynres_config.log;
		if (dynres.enabled && !was_enabled)
		{
			/* (from the scale drawn until now: at the first frame the
			ceiling, switched on in a game the fixed scale it had - from
			the ceiling, the Vita went from 50% to 100% in a Covenant
			fight and spent seconds over the budget coming back down,
			Oct 7) */
			dynres.level = dynres_level_of(vgxm_render_rect());
			dynres_controller_start(&dynres.controller, dynres.floor, ceiling, dynres.budget_ms, dynres.level);
		}
		vgxm_overlay_dynamic(dynres.enabled);
	}
	if (!dynres.level)
		dynres.level = dynres_level_of(vgxm_render_rect());
	dynres_controller_limits(&dynres.controller, dynres.floor, ceiling, dynres.budget_ms);
	level = dynres.controller.level;
	dynres.frames++;
	/* (drained whether on or not: the overlay's GPU line) */
	while (vgxm_gpu_frame_next(&frame))
		if (!dynres.sim_ms)
			level = dynres_measured(frame.gpu_ms, frame.tail_ms, dynres_level_of(frame.scale), frame.interval_ms);
	if (dynres.sim_ms)
	{
		/* (debug) a GPU whose time grows with the pixels, 8% noise, its
		frames finished at once */
		static unsigned int noise = 12345;
		float scale = (float)dynres.level / DYNRES_UNITS, gpu;

		noise = noise * 1103515245u + 12345u;
		gpu = dynres.sim_ms * scale * scale * (1.0f + 0.08f * (((noise >> 8) & 0xffff) / 32767.5f - 1.0f));
		level = dynres_measured(gpu, gpu * 0.7f, dynres.level, gpu > dynres.budget_ms ? gpu : dynres.budget_ms);
	}
	if (!dynres.enabled)
		level = DYNRES_UNITS;
	else if (dynres.force)
		level = dynres.force;
	else if (dynres.cycle)
	{
		/* 32, 30 .. 16, 18 .. 30, again */
		unsigned long step = (dynres.frames / (unsigned long)dynres.cycle) % 16;

		level = step <= 8 ? DYNRES_UNITS - 2 * (int)step : DYNRES_UNITS / 2 + 2 * (int)(step - 8);
	}
	if (level > ceiling)
		level = ceiling;
	if (level != dynres.level)
	{
		int from = dynres.level;

		dynres_apply(level);
		if (dynres.level != from && dynres.enabled)
			dynres_log_change(from);
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
			/* A D snapshot spans the whole chunk, the registers past those
			copied left as the ring has them but for the last, which is 0. A
			skinned vertex names a second node with a weight of 0 when it has
			none (the Warthog's windshield, the energy shield: index byte
			0xfd), which the program's read clamps to D's last register, 123.
			That register sat past the copy, in whatever the frame put in the
			ring after it: weighted by 0, harmless - unless those bytes were
			a NaN or an infinity, and the vertex was NaN and the part not
			drawn. With the split records the fragment values go to the
			worker's ring and the vertex constants follow one another here,
			and the windshield went missing in about a third of the frames.
			The rest of the gap is read only through node indices past the
			object's matrices, which no vertex names. */
			unsigned long span = chunk == VITA_VC_D ? VITA_VC_D_COUNT : count;
			unsigned char *snapshot = vgxm_ring_alloc(span * sizeof(device.constants[0]), 16);

			if (!snapshot)
				return FALSE;
			memcpy(snapshot, device.constants[first], count * sizeof(device.constants[0]));
			if (span > count)
				memset(snapshot + (span - 1) * sizeof(device.constants[0]), 0, sizeof(device.constants[0]));
			stats.copied_bytes += count * sizeof(device.constants[0]);
			device.chunk_snapshot[chunk] = snapshot;
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

/* A material once made is kept for the rest of the run, found again by
the hash of its states: a map uses a few hundred (b30 341, a10 635), while
a frame made 130-300 new material blocks of 1.1 KB in a per-frame arena -
memory no cache had held since, every line a miss on the Vita's 512 KB L2
(~4400 line fills a frame). A kept material stays in the caches and is
compared, not copied. Made-up blocks are content, so they never go stale;
past MATERIAL_CACHE_CAPACITY new ones go to the frame's arena as before. */
#define MATERIAL_CACHE_CAPACITY 2048
#define MATERIAL_CACHE_SLOTS 4096
static struct
{
	unsigned long hash;
	struct record_material *material;
} material_cache[MATERIAL_CACHE_SLOTS];
static struct record_material *material_cache_pool;
static unsigned long material_cache_used;

/* the hash of the current states as a material holds them (without the
draw's values) */
static unsigned long material_hash_current(void)
{
	static const unsigned char ranges[][2] = {
		{ 0, D3DRS_PSCONSTANT0_0 }, { D3DRS_PSCONSTANT1_7 + 1, D3DRS_PSFINALCOMBINERCONSTANT0 },
		{ D3DRS_PSFINALCOMBINERCONSTANT1 + 1, D3DRS_FOGSTART }, { D3DRS_FOGDENSITY + 1, D3DRS_FOGCOLOR },
		{ D3DRS_FOGCOLOR + 1, D3DRS_CULLMODE }, { D3DRS_CULLMODE + 1, D3DRS_MAX },
	};
	unsigned long hash = 2166136261UL;
	int range, stage, index;

	for (range = 0; range < (int)(sizeof(ranges) / sizeof(ranges[0])); range++)
		for (index = ranges[range][0]; index < ranges[range][1]; index++)
			hash = (hash ^ D3D__RenderState[index]) * 16777619UL;
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		for (index = 0; index < D3DTSS_BUMPENVMAT00; index++)
			hash = (hash ^ D3D__TextureState[stage][index]) * 16777619UL;
		for (index = D3DTSS_BUMPENVLOFFSET + 1; index < D3DTSS_MAX; index++)
			hash = (hash ^ D3D__TextureState[stage][index]) * 16777619UL;
	}
	return hash;
}

static BOOL values_match_current(const struct record_values *values)
{
	int index, stage;

	for (index = 0; index < RECORD_VALUE_COUNT; index++)
		if (values->render_state[index] != D3D__RenderState[record_value_state[index]])
			return FALSE;
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		if (memcmp(values->bump->bump[stage], &D3D__TextureState[stage][D3DTSS_BUMPENVMAT00], sizeof(values->bump->bump[stage])))
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

static BOOL shadow_matches(const struct record_shadow_state *shadow, struct vertex_shader_object *program, BOOL immediate);

/* shadow_matches but for the visibility test: the draws of consecutive
visibility tests (a lens flare's occlusion quads, a thousand a frame on
a10) differ in nothing else */
static BOOL shadow_matches_but_visibility(struct record_shadow_state *shadow, struct vertex_shader_object *program, BOOL immediate)
{
	BOOL active = shadow->visibility_test_active, matches;
	unsigned long index = shadow->visibility_index;

	shadow->visibility_test_active = device.visibility_test_active;
	shadow->visibility_index = device.visibility_index;
	matches = shadow_matches(shadow, program, immediate);
	shadow->visibility_test_active = active;
	shadow->visibility_index = index;
	return matches;
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
static struct record_bump *bump_arenas[3];
static struct record_bump *bump_last;
static unsigned long bump_blocks_used;
static unsigned long state_blocks_used, material_blocks_used, values_blocks_used;
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
			state_arenas[index] = lasting_alloc(STATE_BLOCKS_PER_FRAME * sizeof(struct record_state), "state arenas");
			material_arenas[index] = lasting_alloc(STATE_BLOCKS_PER_FRAME * sizeof(struct record_material),
				"material arenas");
			values_arenas[index] = lasting_alloc(STATE_BLOCKS_PER_FRAME * sizeof(struct record_values), "values arenas");
			bump_arenas[index] = lasting_alloc(STATE_BLOCKS_PER_FRAME * sizeof(struct record_bump), "bump arenas");
			if (!state_arenas[index] || !material_arenas[index] || !values_arenas[index] || !bump_arenas[index])
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
	target_blocks_used = 0;
	targets_last = NULL;
	bump_blocks_used = 0;
	bump_last = NULL;
}

static const struct record_state *record_state_current(void)
{
	DWORD headers[D3DTSS_MAXSTAGES][5];
	const D3DCOLOR *palette_data[D3DTSS_MAXSTAGES];
	struct record_state *block;
	unsigned long textures_present = 0;
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		if (device.textures[stage])
		{
			memcpy(headers[stage], device.textures[stage], sizeof(headers[stage]));
			textures_present |= 1UL << stage;
		}
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
			unsigned long hash = material_hash_current(), slot;
			struct record_material *kept = NULL;

			for (slot = hash % MATERIAL_CACHE_SLOTS; material_cache[slot].material; slot = (slot + 1) % MATERIAL_CACHE_SLOTS)
			{
				if (material_cache[slot].hash == hash && material_matches_current(material_cache[slot].material))
				{
					kept = material_cache[slot].material;
					break;
				}
			}
			if (kept)
			{
				material_last = kept;
				stats.material_kept++;
				goto material_found;
			}
			if (!material_cache_pool)
				material_cache_pool = lasting_alloc(MATERIAL_CACHE_CAPACITY * sizeof(*material_cache_pool), "material cache");
			if (material_cache_pool && material_cache_used < MATERIAL_CACHE_CAPACITY)
			{
				material_last = &material_cache_pool[material_cache_used++];
				material_cache[slot].hash = hash;
				material_cache[slot].material = material_last;
			}
			else
			{
				if (material_blocks_used >= STATE_BLOCKS_PER_FRAME)
					return NULL;
				material_last = &material_arenas[state_arena_index][material_blocks_used++];
			}
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
	material_found:;
	}
	if (!values_last || (device_state_dirty & STATE_DIRTY_VALUES))
	{
		if (!values_last || !values_match_current(values_last))
		{
			int index, stage;

			if (values_blocks_used >= STATE_BLOCKS_PER_FRAME)
				return NULL;
			/* (the bump states: the last block while they are the same) */
			for (stage = 0; bump_last && stage < D3DTSS_MAXSTAGES; stage++)
				if (memcmp(bump_last->bump[stage], &D3D__TextureState[stage][D3DTSS_BUMPENVMAT00], sizeof(bump_last->bump[stage])))
					break;
			if (!bump_last || stage < D3DTSS_MAXSTAGES)
			{
				if (bump_blocks_used >= STATE_BLOCKS_PER_FRAME)
					return NULL;
				bump_last = &bump_arenas[state_arena_index][bump_blocks_used++];
				for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
					memcpy(bump_last->bump[stage], &D3D__TextureState[stage][D3DTSS_BUMPENVMAT00], sizeof(bump_last->bump[stage]));
			}
			values_last = &values_arenas[state_arena_index][values_blocks_used++];
			for (index = 0; index < RECORD_VALUE_COUNT; index++)
				values_last->render_state[index] = D3D__RenderState[record_value_state[index]];
			values_last->bump = bump_last;
			stats.values_new++;
		}
	}
	device_state_dirty = 0;
	/* (a texture is what its header says; another texture object with the
	same header draws the same) */
	if (state_last && state_last->material == material_last && state_last->values == values_last &&
		state_last->textures_present == textures_present &&
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
	block->textures_present = textures_present;
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
	command->phase = (signed char)halo_render_phase;
	command->sky_depth = (unsigned char)device_sky_depth;
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
	has_depth = command->targets->depth_valid && surface_is_depth_cached(&command->targets->depth_surface);
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
	if (device.extra_attribute_reg < 0 && record_state_unchanged(program, immediate) && record_previous != command)
	{
		const struct render_command *previous = record_previous;

		command->program = previous->program;
		command->provided_mask = previous->provided_mask;
		command->packed_mask = previous->packed_mask;
		command->color_mask = previous->color_mask;
		command->immediate = previous->immediate;
		command->key = previous->key;
		memcpy(command->texture_header, previous->texture_header, sizeof(command->texture_header));
		memcpy(command->texture_present, previous->texture_present, sizeof(command->texture_present));
		memcpy(command->texture_version, previous->texture_version, sizeof(command->texture_version));
		memcpy(command->texture_target_data, previous->texture_target_data, sizeof(command->texture_target_data));
		memcpy(command->palette, previous->palette, sizeof(command->palette));
		memcpy(command->sampler_state, previous->sampler_state, sizeof(command->sampler_state));
		memcpy(&draw->depth_test, &previous->draw.depth_test,
			offsetof(struct vgxm_draw, color_write) + sizeof(draw->color_write) - offsetof(struct vgxm_draw, depth_test));
		draw->cull = previous->draw.cull;
		draw->depth_bias_slope = previous->draw.depth_bias_slope;
		draw->depth_bias_units = previous->draw.depth_bias_units;
		memcpy(draw->viewport_offset, previous->draw.viewport_offset, sizeof(draw->viewport_offset));
		memcpy(draw->viewport_scale, previous->draw.viewport_scale, sizeof(draw->viewport_scale));
		memcpy(draw->clip, previous->draw.clip, sizeof(draw->clip));
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
			declaration_masks(declaration, &command->provided_mask, &command->packed_mask, &command->color_mask);
			for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
			{
				D3DBaseTexture *texture = device.textures[stage];

				command->texture_version[stage] = 0;
				command->texture_target_data[stage] = 0;
				if (texture)
				{
					unsigned long index;

					for (index = 0; index < target_version_count; index++)
						if (target_versions[index].data == texture->Data)
							command->texture_version[stage] = target_versions[index].version;
					if ((D3D__RenderState[D3DRS_PSTEXTUREMODES] >> (stage * 5)) & 0x1f)
						command->texture_target_data[stage] = texture->Data;
					if (command->hoistable && command->texture_target_data[stage] && !command->texture_version[stage] &&
						render_target_entry_find(texture->Data))
						command->hoistable = FALSE;
					if (command->texture_target_data[stage])
						record_reads_surface(command, texture->Data);
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
			scissor_apply(draw->clip);
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
	declaration_masks(declaration, &command->provided_mask, &command->packed_mask, &command->color_mask);

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
		key_border(key, stage, state);
		command->texture_present[stage] = texture != NULL;
		command->texture_version[stage] = 0;
		command->texture_target_data[stage] = texture && ((D3D__RenderState[D3DRS_PSTEXTUREMODES] >> (stage * 5)) & 0x1f) ?
			texture->Data : 0;
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
			draw must stay in order. Only a stage the pixel shader samples
			reads it (bind_recorded_textures binds no other): a target left
			bound on a stage the draw does not sample - the active
			camouflage's copy of the screen stays on stage 2 after the
			cloaked units are drawn - held the motion sensor's blips in
			the frame's order, a scene of their own and a split of the
			main scene more each frame a cloaked unit was in view (#33) */
			if (command->hoistable && command->texture_target_data[stage] && !command->texture_version[stage] &&
				render_target_entry_find(texture->Data))
				command->hoistable = FALSE;
			if (command->texture_target_data[stage])
				record_reads_surface(command, texture->Data);
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
	draw->blend_color = rs[D3DRS_BLENDCOLOR];
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
	scissor_apply(draw->clip);
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
		if (stream_slot[stream] == ~0UL && draw->stream_count >= VGXM_STREAM_COUNT)
		{
			static int warned;

			if (!warned++)
				platform_log("a draw with more than %d vertex streams is not drawn", VGXM_STREAM_COUNT);
			return FALSE;
		}
		if (stream_slot[stream] == ~0UL)
		{
			const unsigned char *base = (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data);
			unsigned long bytes = stride ? stride * count : 64;
			const unsigned char *start = base + first * stride;

			stream_slot[stream] = draw->stream_count++;
			draw->strides[stream_slot[stream]] = stride;
			if (device.streams[stream].in_place || memory_is_static(start, bytes))
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
	if (device.extra_attribute_reg >= 0 && !(declaration->provided_mask & (1UL << device.extra_attribute_reg)) &&
		device.streams[device.extra_attribute_stream].data && draw->attribute_count < VGXM_ATTRIBUTE_COUNT)
	{
		/* (halo_d3d_stream_attribute's register, after the declaration's) */
		unsigned long stream = (unsigned long)device.extra_attribute_stream;
		unsigned long stride = device.streams[stream].stride ? device.streams[stream].stride : 16;
		const unsigned char *start = (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data) +
			first * stride;
		struct vgxm_attribute *attribute;

		if (stream_slot[stream] == ~0UL && draw->stream_count >= VGXM_STREAM_COUNT)
			return FALSE;
		if (stream_slot[stream] == ~0UL)
		{
			stream_slot[stream] = draw->stream_count++;
			draw->strides[stream_slot[stream]] = stride;
			if (memory_is_static(start, stride * count))
			{
				stats.direct_bytes += stride * count;
				draw->streams[stream_slot[stream]] = start;
			}
			else
			{
				stats.copied_streams += stride * count;
				draw->streams[stream_slot[stream]] = ring_copy(start, stride * count);
				if (!draw->streams[stream_slot[stream]])
					return FALSE;
			}
		}
		attribute = &draw->attributes[draw->attribute_count++];
		attribute->reg = (unsigned char)device.extra_attribute_reg;
		attribute->stream = (unsigned char)stream_slot[stream];
		attribute->offset = 0;
		attribute->format = _vgxm_attribute_f32;
		attribute->components = 4;
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

/* (port) Vertex buffers the game made once and never rewrites, outside the
tag cache: a Custom Edition map's converted model and structure BSP
geometry (port/linux/game/custom_edition_geometry.c). Like the tag cache's
geometry, they lie in the window the GPU maps, and are read in place rather
than copied into the ring every draw (Vita3K, Blood Gulch built as a Custom
Edition map: the ring's use fell from 1058 KB to 152 KB a frame, the Xbox
map's being 131 KB). Kept by buffer, in an open-addressed table. */
#define IN_PLACE_BUFFER_SLOTS 4096
#define IN_PLACE_REMOVED ((const void *)1)
static const void *in_place_buffers[IN_PLACE_BUFFER_SLOTS];

static unsigned long in_place_slot(const void *buffer)
{
	return (unsigned long)(((unsigned long)buffer >> 3) * 2654435761UL) % IN_PLACE_BUFFER_SLOTS;
}

static BOOL buffer_in_place(const void *buffer)
{
	unsigned long slot = in_place_slot(buffer), probe;

	if (!buffer)
		return FALSE;
	for (probe = 0; probe < IN_PLACE_BUFFER_SLOTS && in_place_buffers[slot]; probe++)
	{
		if (in_place_buffers[slot] == buffer)
			return TRUE;
		slot = (slot + 1) % IN_PLACE_BUFFER_SLOTS;
	}
	return FALSE;
}

void halo_d3d_buffer_in_place(const void *buffer, int in_place)
{
	unsigned long slot, probe;

	if (!buffer)
		return;
	layer_enter();
	slot = in_place_slot(buffer);
	if (!in_place || !buffer_in_place(buffer))
	{
		for (probe = 0; probe < IN_PLACE_BUFFER_SLOTS; probe++)
		{
			const void *entry = in_place_buffers[slot];

			if (in_place && (!entry || entry == IN_PLACE_REMOVED))
			{
				in_place_buffers[slot] = buffer;
				break;
			}
			if (!in_place && entry == buffer)
			{
				in_place_buffers[slot] = IN_PLACE_REMOVED;
				break;
			}
			if (!entry)
				break;
			slot = (slot + 1) % IN_PLACE_BUFFER_SLOTS;
		}
		if (in_place && probe == IN_PLACE_BUFFER_SLOTS)
		{
			static int warned;

			if (!warned++)
				platform_log("Direct3D: more than %d vertex buffers to read in place; the rest are copied", IN_PLACE_BUFFER_SLOTS);
		}
	}
	/* (the current streams are copied until set again) */
	for (slot = 0; slot < 16; slot++)
		device.streams[slot].in_place = FALSE;
	layer_leave();
}

void WINAPI D3DDevice_SetStreamSource(UINT stream_number, D3DVertexBuffer *stream_data, UINT stride)
{
	if (stream_number >= 16)
		return;
	device.streams[stream_number].data = stream_data ? stream_data->Data : 0;
	device.streams[stream_number].stride = stride;
	device.streams[stream_number].in_place = buffer_in_place(stream_data);
}

/* (port) the stream draws that follow take input register reg from stream
stream (four floats a vertex, from its first byte) rather than from the
register's current value, until reg -1 is given: the decals' batches carry
each decal's colour per vertex this way (rasterizer_xbox_decals.c). The
program reads the same values, from its vertices instead of its uniforms */
void halo_d3d_stream_attribute(long reg, long stream)
{
	if (reg < 0 || reg >= XGPU_VERTEX_ATTRIBUTE_COUNT || stream < 0 || stream >= 16)
		reg = -1;
	device.extra_attribute_reg = reg;
	device.extra_attribute_stream = stream;
}

/* the declaration's input masks, with halo_d3d_stream_attribute's register */
static void declaration_masks(const struct vertex_shader_object *declaration, unsigned long *provided,
	unsigned long *packed, unsigned long *color)
{
	*provided = declaration->provided_mask;
	*packed = declaration->packed_mask;
	*color = declaration->color_mask;
	if (device.extra_attribute_reg >= 0 && !(*provided & (1UL << device.extra_attribute_reg)) &&
		device.streams[device.extra_attribute_stream].data)
	{
		*provided |= 1UL << device.extra_attribute_reg;
	}
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
		if (!device.streams[element->stream].in_place && !memory_is_static(base, 1))
			streams_static = FALSE;
	}
	if (device.extra_attribute_reg >= 0 && device.streams[device.extra_attribute_stream].data &&
		!memory_is_static((const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[device.extra_attribute_stream].data), 1))
	{
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
		unsigned long capacity = device.immediate_capacity ? device.immediate_capacity * 2 : 256;
		float *grown = realloc(device.immediate_vertices, capacity * XGPU_VERTEX_ATTRIBUTE_COUNT * 4 * sizeof(float));

		if (!grown)
		{
			/* (the heap is exhausted: the draw is dropped - its vertices
			so far and the rest, which no longer gather until the next
			Begin - and the buffer kept) */
			static int warned;

			if (!warned++)
				platform_log("immediate draw: out of memory for %lu vertices, the draw is dropped", capacity);
			device.immediate_active = FALSE;
			device.immediate_count = 0;
			return;
		}
		device.immediate_vertices = grown;
		device.immediate_capacity = capacity;
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

/* HALO_VISIBILITY_MERGE=0: an immediate draw of another visibility test
is never merged into the one before */
static int visibility_merge_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		const char *setting = getenv("HALO_VISIBILITY_MERGE");
		enabled = !setting || atoi(setting) != 0;
	}
	return enabled;
}

static int held_segment_add(unsigned long first_index, unsigned long visibility_index)
{
	if (held_immediate.segment_count == held_immediate.segment_capacity)
	{
		unsigned long capacity = held_immediate.segment_capacity ? held_immediate.segment_capacity * 2 : 64;
		struct draw_segment *grown = realloc(held_immediate.segments, capacity * sizeof(*grown));

		if (!grown)
			return 0;
		held_immediate.segments = grown;
		held_immediate.segment_capacity = capacity;
	}
	held_immediate.segments[held_immediate.segment_count].first_index = first_index;
	held_immediate.segments[held_immediate.segment_count].visibility_index = visibility_index;
	held_immediate.segment_count++;
	return 1;
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
	{
		unsigned long capacity = floats > held_immediate.capacity * 2 ? floats : held_immediate.capacity * 2;
		float *grown = realloc(held_immediate.vertices, capacity * sizeof(float));

		if (!grown)
		{
			/* (the held vertices and capacity are kept, so the next draw
			does not take a freed or NULL buffer for room; the caller drops
			this draw) */
			static int warned;

			if (!warned++)
				platform_log("immediate draw: out of memory holding %lu floats, the draw is dropped", floats);
			return 0;
		}
		held_immediate.vertices = grown;
		held_immediate.capacity = capacity;
	}
	return 1;
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
		held_immediate.constants == constant_generation && held_immediate.count + count <= 65536)
	{
		/* the same state: its triangles join the held draw's; the same but
		for the visibility test: they join it as a segment of their own,
		issued as a draw of its own with its own slot (the worker), and
		the game's thread records one draw for a run of tests (the index
		data of a segment kept 4-byte aligned) */
		unsigned long visibility_index = device.visibility_test_active ? device.visibility_index : 0;
		int same = shadow_matches(&held_shadow, current_program(), TRUE);
		int segment = !same && visibility_merge_enabled() && !(held_immediate.index_count & 1) &&
			shadow_matches_but_visibility(&held_shadow, current_program(), TRUE);

		if (same || segment)
		{
			unsigned long first_index = held_immediate.index_count;

			draw = &held_immediate.command->draw;
			if (immediate_hold_room((held_immediate.count + count) * (held_immediate.stride / sizeof(float))) &&
				immediate_hold_triangles(type, held_immediate.count, count) &&
				(!segment || held_segment_add(first_index, visibility_index)))
			{
				immediate_pack(draw, 0, count, held_immediate.vertices + held_immediate.count * (held_immediate.stride / sizeof(float)));
				held_immediate.count += count;
				merged_immediate_draws++;
				if (segment)
				{
					held_shadow.visibility_test_active = device.visibility_test_active;
					held_shadow.visibility_index = device.visibility_index;
				}
				return;
			}
			/* (a failed allocation: the indices added are dropped) */
			held_immediate.index_count = first_index;
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
	held_immediate.segment_count = 0;
	held_segment_add(0, draw->visibility_index);
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

static void write_screenshot_pixels(const unsigned char *pixels, unsigned long pitch, unsigned long width,
	unsigned long height, const char *prefix)
{
	const char *directory = *config_string("debug.screenshot_directory") ?
		config_string("debug.screenshot_directory") : NULL;
	unsigned long image_size = width * height * 4, row, column;
	unsigned char header[54] = { 'B', 'M' };
	unsigned char *line;
	char path[512];
	FILE *file;

	if (!directory || !pixels)
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
	for (row = 0; line && row < height; row++)
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

static void write_screenshot_named(struct render_target_entry *target, const char *prefix)
{
	unsigned long width = target->target.width, height = target->target.height, pitch = 0;
	const unsigned char *pixels;

	if (!*config_string("debug.screenshot_directory"))
		return;
	pixels = vgxm_target_pixels(target->id, &pitch, &width, &height);
	write_screenshot_pixels(pixels, pitch, width, height, prefix);
}

/* (debug, HALO_TEST_COMMANDS "@shot name") the name of a frame's display
screenshot, kept for the worker by the frame's number */
static char shot_names[4][64];
static unsigned long shot_frames[4];

/* (debug) HALO_SCREENSHOT_DISPLAY=1: a screenshot frame's display buffer
too, 960x544 as shown (the scale to the display, black bars, the panel),
named display<frame>.bmp (or <name><frame>.bmp after "@shot name"); the
display's colour order (ABGR) swapped back */
static void write_display_screenshot(unsigned long frame)
{
	static int wanted = -1;
	const char *prefix = "display";
	unsigned long pitch = 0, width = 0, height = 0, row, column;
	const unsigned char *pixels;
	unsigned char *copy;

	if (wanted < 0)
		wanted = getenv("HALO_SCREENSHOT_DISPLAY") && atoi(getenv("HALO_SCREENSHOT_DISPLAY"));
	if (shot_frames[frame & 3] == frame && shot_names[frame & 3][0])
		prefix = shot_names[frame & 3];
	else if (!wanted)
		return;
	if (!(pixels = vgxm_display_pixels(&pitch, &width, &height)))
		return;
	copy = malloc(width * height * 4);
	if (!copy)
		return;
	for (row = 0; row < height; row++)
		for (column = 0; column < width; column++)
		{
			const unsigned char *from = pixels + row * pitch + column * 4;
			unsigned char *to = copy + (row * width + column) * 4;

			to[0] = from[2];
			to[1] = from[1];
			to[2] = from[0];
			to[3] = 0xff;
		}
	write_screenshot_pixels(copy, width * 4, width, height, prefix);
	free(copy);
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
		{
			/* (frame interpolation's frames go out triple buffered:
			vita_gxm.c, "the display's buffers") */
			int halo_interpolation_enabled(void);

			vgxm_display_buffering(halo_interpolation_enabled() ? 3 : 2);
		}
		command = command_begin(_command_present);
		before = vita_host_time_us();

		if (command)
		{
			struct record_targets *targets = &present_targets[state_arena_index];

			memset(targets, 0, sizeof(*targets));
			targets->color_surface = device.back_buffer;
			targets->color_valid = TRUE;
			command->targets = targets;
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
			{
				/* (debug) "@shot name" (HALO_TEST_COMMANDS): this frame's
				display saved as name<frame>.bmp */
				extern char halo_screenshot_name[64];

				shot_names[device.frame & 3][0] = 0;
				if (halo_screenshot_name[0])
				{
					command->screenshot = TRUE;
					shot_frames[device.frame & 3] = device.frame;
					strncpy(shot_names[device.frame & 3], halo_screenshot_name, sizeof(shot_names[0]) - 1);
					shot_names[device.frame & 3][sizeof(shot_names[0]) - 1] = 0;
					halo_screenshot_name[0] = 0;
				}
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
		frame_drain_us = vita_host_time_us() - before;
		present_wait_time += frame_drain_us;
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
		extern volatile unsigned long long vita_texture_build_us, vgxm_compile_us, vgxm_shader_load_us, vgxm_link_us,
			vgxm_cache_write_us;
		extern volatile unsigned long vita_texture_builds, vita_texture_build_bytes, vgxm_compiles, vgxm_shader_loads,
			vgxm_links, vgxm_compiles_background;
		extern volatile unsigned long vita_texture_background_builds, vita_texture_background_bytes, vita_texture_stand_ins;
		static unsigned long long previous_present;
		static unsigned long hitches_logged;
		unsigned long long now = vita_host_time_us();

		if (previous_present && now - previous_present > 100000ull && hitches_logged < 200)
		{
			hitches_logged++;
			platform_log("hitch: frame %lu took %.1f ms; textures decoded %lu (%lu KB) in %.1f ms (%lu stand-ins made, %lu (%lu KB)"
				" decoded in the background swapped in), shaders compiled %lu in %.1f ms"
				" (loaded %lu in %.1f ms, cache writes %.1f ms, linked %lu in %.1f ms, %lu compiled in the background),"
				" waited %.1f ms for the worker",
				device.frame, (now - previous_present) / 1000.0, vita_texture_builds, vita_texture_build_bytes / 1024,
				vita_texture_build_us / 1000.0, vita_texture_stand_ins, vita_texture_background_builds,
				vita_texture_background_bytes / 1024, vgxm_compiles, vgxm_compile_us / 1000.0, vgxm_shader_loads,
				vgxm_shader_load_us / 1000.0, vgxm_cache_write_us / 1000.0, vgxm_links, vgxm_link_us / 1000.0, vgxm_compiles_background,
				frame_drain_us / 1000.0);
		}
		vita_texture_build_us = 0;
		vita_texture_builds = 0;
		vita_texture_build_bytes = 0;
		vita_texture_stand_ins = vita_texture_background_builds = vita_texture_background_bytes = 0;
		vgxm_compile_us = 0;
		vgxm_compiles = 0;
		vgxm_shader_load_us = vgxm_link_us = vgxm_cache_write_us = 0;
		vgxm_shader_loads = vgxm_links = vgxm_compiles_background = 0;
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
	/* (HALO_DRAW_PROFILE=N: its report every 300 frames, with or without
	the GPU statistics) */
	if (draw_profile > 0 && device.frame % 300 == 0 && draw_profile_draws)
	{
		double per = 1.0 / draw_profile_draws;
		double worker_per = worker_profile_draws ? 1.0 / worker_profile_draws : 0.0;

		platform_log("draw profile (us/draw, 1 in %d of %lu draws timed): record: begin %.2f state %.2f versions %.2f constants %.2f tail %.2f | streams %.2f commit %.2f | execute: build+targets %.2f textures %.2f shaders %.2f gxm draw %.2f",
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
		}
		platform_log("uniform KB/frame by kind: vertex chunks A %.1f B %.1f C1 %.1f C2 %.1f D %.1f E %.1f, vertex misc %.1f, fragment (worker) %.1f",
			stats.copied_chunk[0] / 1024.0 / stats.presents, stats.copied_chunk[1] / 1024.0 / stats.presents,
			stats.copied_chunk[2] / 1024.0 / stats.presents, stats.copied_chunk[3] / 1024.0 / stats.presents,
			stats.copied_chunk[4] / 1024.0 / stats.presents, stats.copied_chunk[5] / 1024.0 / stats.presents,
			stats.copied_vertex_misc / 1024.0 / stats.presents, stats.copied_fragment / 1024.0 / stats.presents);
		platform_log("state blocks per frame: %lu reused, %lu new (%lu new materials, %lu kept materials (%lu kept in all), %lu new values, %lu compared equal); worker builds %lu (+%lu texture-only)",
			stats.state_quick / stats.presents, stats.state_new / stats.presents, stats.material_new / stats.presents,
			stats.material_kept / stats.presents, material_cache_used,
			stats.values_new / stats.presents,
			stats.state_equal / stats.presents, stats.worker_builds / stats.presents, stats.worker_texture_builds / stats.presents);
		memset(&stats, 0, sizeof(stats));
		layer_time = 0;
		worker_time = 0;
		memset(worker_kind_time, 0, sizeof(worker_kind_time));
		present_wait_time = 0;
	}
	platform_pump_events();

	/* (the flips are counted down by the vertical blank thread, which the
	game starts when it sets its callback: frames presented before - the
	loading screen of a map precache taking seconds, on a slow disk or
	with HALO_IO_THROTTLE_KBPS - waited for it for ever) */
	vertical_blank_start();
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
