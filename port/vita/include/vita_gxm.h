/*
VITA_GXM.H

The GXM renderer (port/vita/host/vita_gxm.c, VitaSDK's GCC and ABI) as the
Direct3D device (port/vita/platform/d3d8_gxm.c, the game's ABI) drives it.
Everything crossing here is a 32-bit scalar, a float, a pointer, or a struct
of those, so both ABIs lay it out alike; Direct3D's enumerant values are used
as they are and translated on the GXM side.

Memory the GPU reads (vertices, indices, textures, uniforms) is either in the
contiguous window the host mapped for the GPU at start-up
(vita_host_arena), in the per-frame ring (vgxm_ring_alloc), or in the
texture pool (vgxm_pool_alloc).
*/

#ifndef __HALO_VITA_GXM_H
#define __HALO_VITA_GXM_H

/* ---------- start-up */

/* GXM, the display and the shader patcher; maps the contiguous window for
the GPU. 0 on success. */
int vgxm_initialize(void *arena, unsigned long arena_size);

/* ---------- shaders */

/* a compiled program for Cg source (from the shader cache on the memory
card when it was compiled before); 0 when it does not compile. The id is
also the key programs are linked by. */
unsigned long vgxm_shader_get(const char *source, int fragment);
/* the same without waiting for a compile (HALO_SHADER_ASYNC, on unless
it is 0): a program neither shipped in the VPK nor in the memory card's
cache is compiled on a thread of its own, and VGXM_SHADER_PENDING is
returned until it is ready (the render worker's draws skip it meanwhile,
and ask again) */
#define VGXM_SHADER_PENDING (~0UL)
unsigned long vgxm_shader_request(const char *source, int fragment);

/* ---------- memory */

/* GPU-visible bytes for this frame only (vertex and index copies, uniform
snapshots); NULL when the ring is full */
void *vgxm_ring_alloc(unsigned long size, unsigned long alignment);
/* the recorder starts the given frame's ring: the ring a frame's records
go into, reused every fourth frame */
void vgxm_ring_next(unsigned long frame);
/* the render worker's own GPU-visible bytes for the frame it executes
(its thread only; reused four presents later) */
void *vgxm_worker_alloc(unsigned long size, unsigned long alignment);
/* GPU-visible bytes that stay until vgxm_pool_reset (textures) */
void *vgxm_pool_alloc(unsigned long size, unsigned long alignment);
/* waits for the GPU and forgets every pool allocation */
void vgxm_pool_reset(void);
/* the pool full: waits for the GPU and frees the pool's next segment (the
oldest allocations, from the start again past the end), its memory in
*base and *size (0 if it had none) for what was decoded there to be
forgotten; 0 when none is left to free so (vgxm_pool_reset then) */
int vgxm_pool_recycle(void **base, unsigned long *size);
unsigned long vgxm_pool_used(void);

/* ---------- textures: 16-byte GXM control words */

enum
{
	_vgxm_texture_bgra8,      /* 32-bit B,G,R,A bytes (Direct3D's A8R8G8B8) */
	_vgxm_texture_dxt1,
	_vgxm_texture_dxt3,
	_vgxm_texture_dxt5,
};

enum
{
	_vgxm_texture_linear,     /* rows padded to 8 texels, mip levels following */
	_vgxm_texture_swizzled,   /* GXM's twiddled order, levels following */
	_vgxm_texture_cube,       /* six swizzled faces */
};

struct vgxm_texture
{
	unsigned long control[4];
};

int vgxm_texture_initialize(struct vgxm_texture *texture, const void *data, unsigned long format,
	unsigned long layout, unsigned long width, unsigned long height, unsigned long levels);

/* Direct3D sampler state (D3DTEXF_*, D3DTADDRESS_*) applied to a copy */
void vgxm_texture_set_sampler(struct vgxm_texture *texture, unsigned long min_filter, unsigned long mag_filter,
	unsigned long mip_filter, unsigned long address_u, unsigned long address_v, float lod_bias);
/* a copy samples only its first levels (fewer than it has; GXM's mip filter
off still picks among all of them, the nearest, where Direct3D's
D3DTEXF_NONE reads the first level only) */
void vgxm_texture_set_level_count(struct vgxm_texture *texture, unsigned long levels);

/* ---------- render targets */

/* a colour target of 32-bit BGRA pixels, or a depth-stencil target (D24S8);
returns its id, 0 on failure. A colour target can be sampled through the
texture it fills in. */
unsigned long vgxm_target_create(unsigned long width, unsigned long height, int depth,
	struct vgxm_texture *texture);
/* makes target id again at another size or kind (its memory given back
first, then allocated anew), for a target nothing uses any more; its
texture is filled in again. 1 on success; on failure the id has no target */
int vgxm_target_remake(unsigned long id, unsigned long width, unsigned long height, int depth,
	struct vgxm_texture *texture);
/* gives target id's memory back, keeping the id (made again with
vgxm_target_remake): between frames, with the GPU idle, so a set of
targets can be made at another size without the old ones' memory in the
way. Never a cell of an atlas */
void vgxm_target_release(unsigned long id);
/* the render scale of the screen-sized targets (480 lines, 640 columns
and more: HALO_RENDER_SCALE), and a new one for the targets made from now
on (the settings panel's change, between frames: d3d8_gxm.c) */
float vgxm_render_scale(void);
void vgxm_render_scale_set(float scale);
/* the dynamic resolution (d3d8_gxm.c, on the worker between two frames, or
with it idle): the screen-sized targets drawn into their top left, scale
times the size asked for, their memory as it was made (at most the render
scale; 1: the whole target). Returns the scale in effect. Viewports, clips,
clears, visibility counts, the blit to the display and screenshots follow;
the colour targets' textures cover the drawn part once vgxm_target_texture
is called again for each (a target made from now on has it already) */
float vgxm_render_rect_set(float scale);
float vgxm_render_rect(void);
void vgxm_target_texture(unsigned long id, struct vgxm_texture *texture);
/* (the log) the target slots made, the CDRAM the targets hold, the CDRAM
kept for the screen-sized targets' sizes not in use (vita_gxm.c
screen_block_keep), and the CDRAM free */
void vgxm_target_stats(unsigned long *targets, unsigned long *cdram_bytes, unsigned long *cached_bytes,
	unsigned long *cdram_free);
/* ---------- video memory (CDRAM: port/vita/include/vgxm_memory.h)

The CDRAM free (the hardware's figure where it can be trusted, the model's
otherwise, less HALO_CDRAM_RESERVE_MB's reserve). The rest is for the
device's live changes and relief (d3d8_gxm.c), between frames, with the
worker and the GPU idle: vgxm_cdram_wanted, the largest target allocation
that found no CDRAM since it was last asked (0: none); vgxm_memory_trim
frees what costs nothing (the block cache, the small targets' blocks with
every share back), the bytes freed; vgxm_targets_sweep gives back every
target whose id is not marked in referenced[] (count entries; never an
atlas or its cells), the bytes they held; vgxm_pool_demote moves the
texture pool's highest part in CDRAM to user RAM (the CDRAM bytes freed, 0
if none), and vgxm_pool_promote the lowest in user RAM back to CDRAM when
the targets keep their headroom (1 if one moved): the textures decoded in
*base's *size bytes are to be forgotten (vita_texture_cache_forget).
vgxm_memory_census logs what holds CDRAM; with detail, a line per target,
with roles[id] (count entries) saying what each is */
/* (the most target ids there are) */
#define VGXM_MAXIMUM_TARGETS 256
unsigned long vgxm_cdram_free(void);
unsigned long vgxm_cdram_wanted(void);
unsigned long vgxm_memory_trim(void);
unsigned long vgxm_targets_sweep(const unsigned char *referenced, unsigned long count);
unsigned long vgxm_pool_demote(void **base, unsigned long *size);
int vgxm_pool_promote(void **base, unsigned long *size);
void vgxm_memory_census(const char *const *roles, unsigned long count, int detail);
/* colour targets for each level of one linear mip chain (levels one after
another, rows aligned to 8 texels, as the texture cache's own mipmapped
textures), so a texture the game renders level by level (the water's
ripple bump map) samples with its mipmaps; the ids go to ids[], the texture
over the whole chain to texture. 0 on success */
int vgxm_target_create_chain(unsigned long width, unsigned long height, unsigned long levels,
	unsigned long *ids, struct vgxm_texture *texture);
/* a copy of a small colour target as a cell of an atlas shared by the
copies of one surface (key) at one size: index 1 and up picks the cell;
the texture is over the cell's pixels. 0 if there is none (too big, out
of cells or targets, HALO_TARGET_ATLAS=0): make a target of its own */
unsigned long vgxm_target_create_cell(unsigned long key, unsigned long index, unsigned long width, unsigned long height,
	struct vgxm_texture *texture);
/* where subsequent draws and clears go; either may be 0 */
void vgxm_set_targets(unsigned long color, unsigned long depth);
/* the next draw samples this target: its scene waits for the scene that
drew it if there was no wait since (HALO_GXM_RTT_SYNC) */
void vgxm_note_sampled_target(unsigned long id);
/* (debug, the null renderer's HALO_DRAW_HASH=4) the surface and copy a
target stands for; nothing on the Vita */
void vgxm_debug_name_target(unsigned long id, unsigned long long name);

/* ---------- drawing */

enum
{
	_vgxm_attribute_f32,
	_vgxm_attribute_u8n,      /* D3DCOLOR bytes and PBYTE, normalised */
	_vgxm_attribute_u8,       /* NORMPACKED3 as its four raw bytes */
	_vgxm_attribute_s16,
	_vgxm_attribute_s16n,
};

struct vgxm_attribute
{
	unsigned char reg;
	unsigned char format;
	unsigned char components;
	unsigned char stream;
	unsigned short offset;
	unsigned short pad;
};

#define VGXM_ATTRIBUTE_COUNT 16
/* (the Direct3D device feeds at most three: two in a declaration and one
for halo_d3d_stream_attribute) */
#define VGXM_STREAM_COUNT 4

/* (the fields the Direct3D device's record writes on the game's thread
come first, together, then those its worker sets: a draw is written into a
cold ring entry, and every line it touches there is a cache miss) */
struct vgxm_draw
{
	/* vertex layout: the attributes (by register) and their streams */
	unsigned long attribute_count;
	unsigned long stream_count;
	/* D3DPRIMITIVETYPE; quads arrive as triangles */
	unsigned long primitive;
	unsigned long index_count;
	const unsigned short *indices;
	/* set when the draw counts samples for a visibility test: its slot in
	the frame's visibility buffer, 1 to VGXM_VISIBILITY_SLOTS - 1 */
	unsigned long visibility_index;
	/* the vertex program's BUFFER[1] */
	const void *vertex_uniforms;
	/* (for the null renderer's draw hash) the registers of chunk D's
	snapshot the program can read - its absolute reads and the object's node
	matrices */
	unsigned long vertex_chunk_d_registers;
	/* the vertex program's constant chunks (vita_xgpu.h: BUFFER[0] and
	[2..6]; NULL for a chunk the program does not read) */
	const void *vertex_chunks[6];
	/* the window transform: x = ndc.x * scale[0] + offset[0], likewise y,
	and depth = ndc.z * scale[2] + offset[2] */
	float viewport_offset[3];
	float viewport_scale[3];
	/* pixels [x0, x1) x [y0, y1) */
	long clip[4];
	unsigned long strides[VGXM_STREAM_COUNT];
	const void *streams[VGXM_STREAM_COUNT];
	struct vgxm_attribute attributes[VGXM_ATTRIBUTE_COUNT];

	/* (set by the worker) */
	unsigned long vertex_shader;
	unsigned long fragment_shader;
	/* the fragment program's BUFFER[0] and [1] */
	const void *fragment_uniforms[2];
	/* per texture stage, NULL when unbound */
	const struct vgxm_texture *textures[4];
	/* Direct3D render state values */
	unsigned long depth_test, depth_write, depth_function;
	unsigned long stencil_test, stencil_function, stencil_reference, stencil_read_mask, stencil_write_mask;
	unsigned long stencil_fail, stencil_depth_fail, stencil_pass;
	unsigned long blend, blend_source, blend_destination, blend_operation;
	/* D3DCOLORWRITEENABLE_* bits */
	unsigned long color_write;
	/* 0 none, else D3DCULL_CW or D3DCULL_CCW: the winding that is discarded */
	unsigned long cull;
	float depth_bias_slope, depth_bias_units;
	/* (for the null renderer's draw hash) the input registers the vertex
	program reads, and the Xbox program's own hash, whichever Cg
	translation of it (by the inputs its streams provide) runs */
	unsigned long vertex_input_mask;
	unsigned long vertex_program_hash;
};

void vgxm_draw(const struct vgxm_draw *draw);

/* D3DCLEAR_* flags and D3DCOLOR; clip as in vgxm_draw */
void vgxm_clear(unsigned long flags, unsigned long color, float depth, unsigned long stencil, const long clip[4]);

/* ---------- visibility tests */

/* a frame's visibility test slots (each frame's buffer has this many) */
#define VGXM_VISIBILITY_SLOTS 1024

/* (the worker, before vgxm_present) the number the game gave the frame,
which its visibility counts are then known by */
void vgxm_visibility_frame(unsigned long frame);
/* the newest frame the GPU has finished whose visibility counts are kept:
its buffer (-1: none yet) and the frame's number */
int vgxm_visibility_newest(unsigned long *frame);
/* the samples that passed in a slot's test in that buffer, in the game's
pixels (unscaled by the render scale) */
unsigned long vgxm_visibility_count(int buffer, unsigned long slot);

/* ---------- frames */

/* ends the frame's scenes, shows the colour target (the game's back
buffer, width x height of it) on the display and starts the next frame */
void vgxm_present(unsigned long color_target, unsigned long width, unsigned long height);
/* waits until the GPU has finished every frame presented so far (any
thread; the frames' scenes all end with the present's notification) */
void vgxm_wait_gpu_idle(void);

/* the GPU's time for the frames presented (the dynamic resolution's
measure): the worker marks when it starts handing a frame's main work to
the GPU (vgxm_frame_submit_begin, before the records held for the present);
vgxm_gpu_frame_next gives each frame the GPU has finished since, oldest
first, 1 while there is one. gpu_ms is from the later of the submission's
start and the previous frame's finish to the frame's finish, tail_ms from
the later of its last scene's submission and that finish (most of gpu_ms
when the GPU is the wall), interval_ms from the previous frame's finish;
scale is the render rectangle it was drawn at. None on the null renderer */
struct vgxm_gpu_frame
{
	unsigned long frame;
	float gpu_ms, tail_ms, interval_ms, scale;
};
void vgxm_frame_submit_begin(void);
int vgxm_gpu_frame_next(struct vgxm_gpu_frame *frame);

/* a line of the renderer's cache sizes (shaders, linked programs,
targets, scenes this frame) for the frame statistics */
const char *vgxm_counts(void);

/* the numbers the overlay shows (XV_FPS=1): frames per second, the game
tick and render times in milliseconds */
void vgxm_overlay_set(float fps, float tick_ms, float render_ms);
/* the performance overlay: 0 off, 1 the full one (frames per second, tick
and render times, the cores' load), 2 frames per second only (XV_FPS, the
settings panel's switch) */
void vgxm_overlay_enable(int enabled);
/* the full overlay's RES line marks the dynamic resolution as running */
void vgxm_overlay_dynamic(int dynamic);
/* the frame's scale to the display: 0 smooth (bilinear), 1 sharp
(nearest); HALO_UPSCALE_FILTER at start-up */
void vgxm_upscale_filter_set(int filter);
/* the display's buffers: 3 (triple buffering, frame interpolation's: the
third made the first time) or 2, from the next frame on */
void vgxm_display_buffering(int buffers);
/* (debug) the display buffer last presented, 960x544 (the screenshots'
HALO_SCREENSHOT_DISPLAY=1) */
const void *vgxm_display_pixels(unsigned long *pitch, unsigned long *width, unsigned long *height);
/* the settings panel (vita_settings.c): text is its lines separated by
'\n' (the first a title, or a tab bar when it starts with '\t': the tabs'
names between '|', the shown one after a '*'; the last a hint; a line that
starts with '!' is a warning), selected the highlighted line; NULL hides it */
void vgxm_menu_set(const char *text, int selected);
/* a system dialog (vita_net.c's network check) is up: the system draws it
over each frame presented while active (host side only) */
void vgxm_common_dialog(int active);

/* the colour target's pixels in rows of 32-bit BGRA, for screenshots
(waits for the GPU), and its size (smaller than asked for when the render
scale made it so); NULL if there is no such target */
const void *vgxm_target_pixels(unsigned long color_target, unsigned long *pitch, unsigned long *width,
	unsigned long *height);

#endif
