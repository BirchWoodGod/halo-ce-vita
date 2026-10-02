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

/* ---------- render targets */

/* a colour target of 32-bit BGRA pixels, or a depth-stencil target (D24S8);
returns its id, 0 on failure. A colour target can be sampled through the
texture it fills in. */
unsigned long vgxm_target_create(unsigned long width, unsigned long height, int depth,
	struct vgxm_texture *texture);
/* colour targets for each level of one linear mip chain (levels one after
another, rows aligned to 8 texels, as the texture cache's own mipmapped
textures), so a texture the game renders level by level (the water's
ripple bump map) samples with its mipmaps; the ids go to ids[], the texture
over the whole chain to texture. 0 on success */
int vgxm_target_create_chain(unsigned long width, unsigned long height, unsigned long levels,
	unsigned long *ids, struct vgxm_texture *texture);
/* where subsequent draws and clears go; either may be 0 */
void vgxm_set_targets(unsigned long color, unsigned long depth);

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
#define VGXM_STREAM_COUNT 16

struct vgxm_draw
{
	unsigned long vertex_shader;
	unsigned long fragment_shader;

	/* vertex layout: the attributes (by register) and their streams */
	unsigned long attribute_count;
	struct vgxm_attribute attributes[VGXM_ATTRIBUTE_COUNT];
	unsigned long stream_count;
	unsigned long strides[VGXM_STREAM_COUNT];
	const void *streams[VGXM_STREAM_COUNT];

	/* the vertex program's constant chunks (vita_xgpu.h: BUFFER[0] and
	[2..6]; NULL for a chunk the program does not read), its BUFFER[1], and
	the fragment program's BUFFER[0] */
	const void *vertex_chunks[6];
	/* (for the null renderer's draw hash) the registers of chunk D's
	snapshot the program can read - its absolute reads and the object's node
	matrices - and the input registers the vertex program reads */
	unsigned long vertex_chunk_d_registers;
	unsigned long vertex_input_mask;
	/* (the draw hash) the Xbox vertex program's own hash, whichever Cg
	translation of it (by the inputs its streams provide) runs */
	unsigned long vertex_program_hash;
	const void *vertex_uniforms;
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
	/* the window transform: x = ndc.x * scale[0] + offset[0], likewise y,
	and depth = ndc.z * scale[2] + offset[2] */
	float viewport_offset[3];
	float viewport_scale[3];
	/* pixels [x0, x1) x [y0, y1) */
	long clip[4];

	/* D3DPRIMITIVETYPE; quads arrive as triangles */
	unsigned long primitive;
	const unsigned short *indices;
	unsigned long index_count;
	/* set when the draw counts samples for a visibility test: its slot in
	the frame's visibility buffer, 1 to VGXM_VISIBILITY_SLOTS - 1 */
	unsigned long visibility_index;
};

void vgxm_draw(const struct vgxm_draw *draw);

/* D3DCLEAR_* flags and D3DCOLOR; clip as in vgxm_draw */
void vgxm_clear(unsigned long flags, unsigned long color, float depth, unsigned long stencil, const long clip[4]);

/* ---------- visibility tests */

/* a frame's visibility test slots (each frame's buffer has this many) */
#define VGXM_VISIBILITY_SLOTS 1024

/* the samples that passed in this slot's test in the latest frame the GPU
has finished, in the game's pixels (unscaled by the render scale) */
unsigned long vgxm_visibility_result(unsigned long slot);

/* ---------- frames */

/* ends the frame's scenes, shows the colour target (the game's back
buffer, width x height of it) on the display and starts the next frame */
void vgxm_present(unsigned long color_target, unsigned long width, unsigned long height);

/* a line of the renderer's cache sizes (shaders, linked programs,
targets, scenes this frame) for the frame statistics */
const char *vgxm_counts(void);

/* the numbers the overlay shows (XV_FPS=1): frames per second, the game
tick and render times in milliseconds */
void vgxm_overlay_set(float fps, float tick_ms, float render_ms);
/* the performance overlay on or off (the settings panel's switch) */
void vgxm_overlay_enable(int enabled);
/* the settings panel (vita_settings.c): text is its lines separated by
'\n' (the first a title, the last a hint), selected the highlighted line;
NULL hides it */
void vgxm_menu_set(const char *text, int selected);

/* the colour target's pixels in rows of 32-bit BGRA, for screenshots
(waits for the GPU); NULL if there is no such target */
const void *vgxm_target_pixels(unsigned long color_target, unsigned long *pitch);

#endif
