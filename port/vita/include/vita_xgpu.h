/*
VITA_XGPU.H

The Vita's Direct3D device (port/vita/platform/d3d8_gxm.c) and its Cg shader
translators (nv2a_vsh_cg.c, nv2a_psh_cg.c): what the generated programs read
from their uniform buffers, and where.

Every program reads its uniforms from BUFFER[0], a snapshot in GPU memory the
device writes when they change, so a draw only points the GPU at it.
*/

#ifndef __HALO_VITA_XGPU_H
#define __HALO_VITA_XGPU_H

#include "xgpu.h"

/* ---------- the vertex uniform buffers, in float4 registers

The vertex constants c[0..192) (D3D register -96 is 0) are split into
chunks by how often the game rewrites them, each a uniform buffer of its
own, so a draw's snapshots copy only the chunks that changed since the last
draw (one buffer of them all meant a 3 KB copy whenever any register
changed: 2 MB a frame into uncached memory on the Vita). Measured on b30:
  A: 0..11    view and projection, per view
  B: 12..16   texture animation and specular tint, per model part
  C1: 17..27  lighting, per object
  C2: 28..59  fog, misc, the viewport (58, 59), per view
  D: 60..183  node matrices (-36 up, 3 per node), per object; the only
              registers read relative to a0 (bases 60, 61, 62), copied up
              to the highest node matrix written lately
  E: 184..191 read by a few programs, never written
BUFFER[1] holds the rest (vm). */

#define VITA_VC_CHUNKS 6
#define VITA_VC_A 0
#define VITA_VC_B 1
#define VITA_VC_C1 2
#define VITA_VC_C2 3
#define VITA_VC_D 4
#define VITA_VC_E 5
/* each chunk's first register and its BUFFER index */
#define VITA_VC_FIRST { 0, 12, 17, 28, 60, 184 }
#define VITA_VC_END { 12, 17, 28, 60, 184, 192 }
#define VITA_VC_BUFFER { 0, 2, 3, 4, 5, 6 }
#define VITA_VC_D_FIRST 60
#define VITA_VC_D_COUNT 124

/* what a vertex program reads of the constants */
struct nv2a_vertex_constant_usage
{
	unsigned long chunk_mask;     /* bit per chunk (VITA_VC_*) it reads */
	unsigned long lowest, highest; /* absolute reads, inclusive (0..191) */
	unsigned long d_absolute_end; /* absolute reads in D: one past the highest, from 60 (0 if none) */
	int relative;                 /* reads the node matrices (chunk D) relative to a0 */
	unsigned long relative_lowest; /* the lowest base of any relative read (192 if none) */
};

#define VITA_VM_VIEWPORT_SCALE 0
#define VITA_VM_VIEWPORT_OFFSET 1
/* x: point size, y: the menus' horizontal shift (screen_offset) */
#define VITA_VM_MISCELLANEOUS 2
/* the current value of each input register a draw's streams do not feed */
#define VITA_VM_ATTRIBUTES 3
#define VITA_VM_COUNT (VITA_VM_ATTRIBUTES + XGPU_VERTEX_ATTRIBUTE_COUNT)

/* ---------- the fragment uniform buffer */

#define VITA_FU_PS_C0 0            /* [8] */
#define VITA_FU_PS_C1 8            /* [8] */
#define VITA_FU_PS_FINAL_C0 16
#define VITA_FU_PS_FINAL_C1 17
#define VITA_FU_FOG_COLOR 18
#define VITA_FU_FOG_PARAMETERS 19  /* start, end, density */
#define VITA_FU_MISCELLANEOUS 20   /* x: alpha reference (0..255) */
#define VITA_FU_BUMP_MATRIX 21     /* [4] */
#define VITA_FU_BUMP_LUMINANCE 25  /* [4] */
#define VITA_FU_TEXTURE_SCALE 29   /* [4] */
#define VITA_FU_COUNT 33
/* two buffers: A = registers 0..17 (the combiner constants: BUFFER[0],
which change per material and part), B = 18..32 (fog, alpha reference,
bump, texture scale: BUFFER[1], per view), so a part's constants do not
re-copy the rest */
#define VITA_FU_A_COUNT 18

/* ---------- translators */

/* A vertex program as Cg. Of the input registers, those in provided_mask
come from the draw's streams (packed_mask: NORMPACKED3, bound as four raw
bytes; color_mask: D3DCOLOR, bound as B,G,R,A bytes), the rest from the
uniform buffer's current values. Returns a malloc'd string. */
char *nv2a_vertex_shader_to_cg(const DWORD *instructions, unsigned long instruction_count,
	unsigned long provided_mask, unsigned long packed_mask, unsigned long color_mask);

unsigned long nv2a_vertex_shader_texcoord_w_mask(const DWORD *instructions, unsigned long instruction_count);
/* the input registers (bit per v0..v15) the program reads */
unsigned long nv2a_vertex_shader_input_mask(const DWORD *instructions, unsigned long instruction_count);
/* the constant registers a vertex program reads, by chunk */
void nv2a_vertex_shader_constant_usage(const DWORD *instructions, unsigned long instruction_count,
	struct nv2a_vertex_constant_usage *usage);

char *nv2a_pixel_shader_to_cg(const struct nv2a_pixel_shader_key *key);

/* ---------- textures (vita_textures.c) */

struct vgxm_texture;

/* the GXM texture for an Xbox texture header, decoding it into the
texture pool when it is new or its memory was written since; *description
receives what it is. NULL when the format is not handled. */
const struct vgxm_texture *vita_texture_get(const DWORD *resource, const D3DCOLOR *palette,
	struct xgpu_texture_description *description);
void vita_texture_cache_begin_frame(void);

#endif
