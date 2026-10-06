/*
FILL_STATS.C

HALO_FILL_STATS=N: an estimate of the pixels each render phase makes the
GPU shade, from one frame in N (the device's worker, d3d8_gxm.c, hands it
every draw and clear of the sampled frames). Each draw's positions are
computed on the CPU - its NV2A vertex program interpreted, as
nv2a_vsh_cg.c translates it, with the draw's constant snapshots and
streams - and its triangles culled and rasterized on a grid of one sample
per 2x2 pixels against a depth buffer of the same grid, with the draw's
depth test and write. Counted per phase (render.c's, by the record's
phase): the draws, the triangles left after culling, the samples covered
and those passing the depth test, the passing samples of blended draws,
and the draws with no passing sample at all. Every 30 sampled frames a
report, in units of the main target's area ("screens": a phase at 1.00
shaded as many pixels as the screen has): the GPU's fragment work, which
the Vita's tile renderer pays in full for blended and alpha-tested layers
(its hidden surface removal saves only opaque overdraw), by phase. The
particles' triangles are also binned by size. The stencil test, alpha test
and the fragment programs' discards are not modelled: an upper bound for
their draws. CPU-heavy (the harness, Vita3K): not for timing runs.
*/

#include "platform.h"
#include "vita_gxm.h"
#include "vita_xgpu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);
extern const char *halo_render_phase_names[64];

#define FILL_PHASES 65
#define FILL_OTHER 64
#define FILL_GRID 2
#define FILL_MAX_WIDTH 1024
#define FILL_MAX_HEIGHT 640
#define FILL_DEPTH_BUFFERS 4

static int fill_every = -1;
static unsigned long fill_frame;
static int fill_sampled;
static unsigned long fill_sampled_frames;
static unsigned long main_color_id;
static double main_area;

struct fill_phase
{
	double draws, triangles, covered, passed, blended, hidden_draws, offscreen_draws;
};
static struct fill_phase phases[FILL_PHASES][2];

/* particles' triangles by size (pixels): count and pixels */
#define SIZE_BINS 7
static const double size_bin_limit[SIZE_BINS] = { 4, 16, 64, 256, 1024, 4096, 1e30 };
static double size_bin_count[SIZE_BINS], size_bin_pixels[SIZE_BINS];
/* and their shaded pixels by the vertex colour's alpha (oD0.a, the
particles' fade): all three vertices 0, then the mean below each limit */
#define ALPHA_BINS 5
static const double alpha_bin_limit[ALPHA_BINS] = { 0, 0.05, 0.25, 0.5, 1.01 };
static double alpha_bin_pixels[ALPHA_BINS];
/* (the late sky's premise, render.c HALO_SKY_LATE) samples the objects'
and lightmaps' phases colour while still at the cleared depth without
covering them - blended, or not writing depth - which the sky drawn after
them would then cover: 0 is the premise holding */
static double sky_order_samples;

struct depth_buffer
{
	unsigned long id;
	unsigned long last_used;
	float *depth;
};
static struct depth_buffer depth_buffers[FILL_DEPTH_BUFFERS];
static unsigned long depth_use;

int halo_fill_stats_sampled(void)
{
	if (fill_every < 0)
	{
		const char *setting = getenv("HALO_FILL_STATS");

		fill_every = setting ? atoi(setting) : 0;
		if (fill_every < 0)
			fill_every = 0;
		fill_sampled = fill_every > 0;
	}
	return fill_sampled;
}

static float *depth_for(unsigned long id)
{
	int index, oldest = 0;

	if (!id)
		return NULL;
	for (index = 0; index < FILL_DEPTH_BUFFERS; index++)
	{
		if (depth_buffers[index].id == id)
		{
			depth_buffers[index].last_used = ++depth_use;
			return depth_buffers[index].depth;
		}
		if (depth_buffers[index].last_used < depth_buffers[oldest].last_used)
			oldest = index;
	}
	if (!depth_buffers[oldest].depth)
		depth_buffers[oldest].depth = malloc(sizeof(float) * (FILL_MAX_WIDTH / FILL_GRID) * (FILL_MAX_HEIGHT / FILL_GRID));
	if (!depth_buffers[oldest].depth)
		return NULL;
	depth_buffers[oldest].id = id;
	depth_buffers[oldest].last_used = ++depth_use;
	for (index = 0; index < (FILL_MAX_WIDTH / FILL_GRID) * (FILL_MAX_HEIGHT / FILL_GRID); index++)
		depth_buffers[oldest].depth[index] = 1.0f;
	return depth_buffers[oldest].depth;
}

void halo_fill_stats_clear(unsigned long flags, float depth, const long clip[4], unsigned long depth_id)
{
	float *buffer;
	long x, y, x0, y0, x1, y1;

	if (!fill_sampled || !(flags & D3DCLEAR_ZBUFFER) || !(buffer = depth_for(depth_id)))
		return;
	x0 = clip ? clip[0] / FILL_GRID : 0;
	y0 = clip ? clip[1] / FILL_GRID : 0;
	x1 = clip ? (clip[2] + FILL_GRID - 1) / FILL_GRID : FILL_MAX_WIDTH / FILL_GRID;
	y1 = clip ? (clip[3] + FILL_GRID - 1) / FILL_GRID : FILL_MAX_HEIGHT / FILL_GRID;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > FILL_MAX_WIDTH / FILL_GRID || x1 <= 0) x1 = FILL_MAX_WIDTH / FILL_GRID;
	if (y1 > FILL_MAX_HEIGHT / FILL_GRID || y1 <= 0) y1 = FILL_MAX_HEIGHT / FILL_GRID;
	for (y = y0; y < y1; y++)
		for (x = x0; x < x1; x++)
			buffer[y * (FILL_MAX_WIDTH / FILL_GRID) + x] = depth;
}

/* ---------- the vertex program, on the CPU (nv2a_vsh_cg.c's translation) */

static unsigned long field(const DWORD *instruction, int word, int low_bit, int bit_count)
{
	return (instruction[word] >> low_bit) & ((1UL << bit_count) - 1);
}

struct vsh_context
{
	const struct vgxm_draw *draw;
	float v[16][4];
	float r[13][4];
	float a0;
};

static void vsh_operand(struct vsh_context *context, const DWORD *instruction, char which, int relative, float out[4])
{
	static const int first[VITA_VC_CHUNKS] = VITA_VC_FIRST;
	static const int end[VITA_VC_CHUNKS] = VITA_VC_END;
	unsigned long negate, swizzle[4], index, mux;
	const float *source = NULL;
	static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	int component;

	switch (which)
	{
	case 'A':
		negate = field(instruction, 1, 8, 1);
		swizzle[0] = field(instruction, 1, 6, 2);
		swizzle[1] = field(instruction, 1, 4, 2);
		swizzle[2] = field(instruction, 1, 2, 2);
		swizzle[3] = field(instruction, 1, 0, 2);
		index = field(instruction, 2, 28, 4);
		mux = field(instruction, 2, 26, 2);
		break;
	case 'B':
		negate = field(instruction, 2, 25, 1);
		swizzle[0] = field(instruction, 2, 23, 2);
		swizzle[1] = field(instruction, 2, 21, 2);
		swizzle[2] = field(instruction, 2, 19, 2);
		swizzle[3] = field(instruction, 2, 17, 2);
		index = field(instruction, 2, 13, 4);
		mux = field(instruction, 2, 11, 2);
		break;
	default:
		negate = field(instruction, 2, 10, 1);
		swizzle[0] = field(instruction, 2, 8, 2);
		swizzle[1] = field(instruction, 2, 6, 2);
		swizzle[2] = field(instruction, 2, 4, 2);
		swizzle[3] = field(instruction, 2, 2, 2);
		index = (field(instruction, 2, 0, 2) << 2) | field(instruction, 3, 30, 2);
		mux = field(instruction, 3, 28, 2);
		break;
	}
	switch (mux)
	{
	case 1:
		source = context->r[index < 13 ? index : 0];
		break;
	case 2:
		source = context->v[field(instruction, 1, 9, 4)];
		break;
	case 3:
	{
		unsigned long constant = field(instruction, 1, 13, 8);
		int chunk;

		for (chunk = 0; chunk < VITA_VC_CHUNKS; chunk++)
			if ((long)constant >= first[chunk] && (long)constant < end[chunk])
				break;
		if (chunk < VITA_VC_CHUNKS && context->draw->vertex_chunks[chunk])
		{
			long offset = (long)constant - first[chunk];

			if (relative)
			{
				float position = context->a0 + (float)offset;
				long last = end[chunk] - first[chunk] - 1;

				offset = position < 0.0f ? 0 : position > (float)last ? last : (long)position;
			}
			source = (const float *)context->draw->vertex_chunks[chunk] + offset * 4;
		}
		break;
	}
	}
	if (!source)
		source = zero;
	for (component = 0; component < 4; component++)
		out[component] = negate ? -source[swizzle[component]] : source[swizzle[component]];
}

static void vsh_write(float destination[4], unsigned long mask, const float value[4])
{
	if (mask & 8) destination[0] = value[0];
	if (mask & 4) destination[1] = value[1];
	if (mask & 2) destination[2] = value[2];
	if (mask & 1) destination[3] = value[3];
}

static void vsh_run(struct vsh_context *context, const DWORD *instructions, unsigned long count, float position[4],
	float diffuse[4])
{
	unsigned long index;
	float scratch[4];

	memset(context->r, 0, sizeof(context->r));
	context->r[12][3] = 1.0f;
	diffuse[0] = diffuse[1] = diffuse[2] = 0.0f;
	diffuse[3] = 1.0f;
	context->a0 = 0.0f;
	for (index = 0; index < count && index < 136; index++)
	{
		const DWORD *instruction = instructions + index * 4;
		unsigned long mac = field(instruction, 1, 21, 4);
		unsigned long ilu = field(instruction, 1, 25, 3);
		unsigned long mac_mask = field(instruction, 3, 24, 4);
		unsigned long temporary = field(instruction, 3, 20, 4);
		unsigned long ilu_mask = field(instruction, 3, 16, 4);
		unsigned long output_mask = field(instruction, 3, 12, 4);
		unsigned long output_is_register = field(instruction, 3, 11, 1);
		unsigned long output_address = field(instruction, 3, 3, 8);
		unsigned long output_from_ilu = field(instruction, 3, 2, 1);
		int relative = (int)field(instruction, 3, 1, 1);
		float a[4], b[4], c[4], m[4] = { 0, 0, 0, 0 }, l[4] = { 0, 0, 0, 0 }, x;
		int k;

		vsh_operand(context, instruction, 'A', relative, a);
		vsh_operand(context, instruction, 'B', relative, b);
		vsh_operand(context, instruction, 'C', relative, c);
		switch (mac)
		{
		case 1: case 13: memcpy(m, a, sizeof(m)); break;
		case 2: for (k = 0; k < 4; k++) m[k] = a[k] * b[k]; break;
		case 3: for (k = 0; k < 4; k++) m[k] = a[k] + c[k]; break;
		case 4: for (k = 0; k < 4; k++) m[k] = a[k] * b[k] + c[k]; break;
		case 5: m[0] = m[1] = m[2] = m[3] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; break;
		case 6: m[0] = m[1] = m[2] = m[3] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + b[3]; break;
		case 7: m[0] = m[1] = m[2] = m[3] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; break;
		case 8: m[0] = 1.0f; m[1] = a[1] * b[1]; m[2] = a[2]; m[3] = b[3]; break;
		case 9: for (k = 0; k < 4; k++) m[k] = a[k] < b[k] ? a[k] : b[k]; break;
		case 10: for (k = 0; k < 4; k++) m[k] = a[k] > b[k] ? a[k] : b[k]; break;
		case 11: for (k = 0; k < 4; k++) m[k] = a[k] < b[k] ? 1.0f : 0.0f; break;
		case 12: for (k = 0; k < 4; k++) m[k] = a[k] >= b[k] ? 1.0f : 0.0f; break;
		default: break;
		}
		switch (ilu)
		{
		case 1: memcpy(l, c, sizeof(l)); break;
		case 2: l[0] = l[1] = l[2] = l[3] = 1.0f / c[0]; break;
		case 3:
			x = 1.0f / c[0];
			if (x > 0.0f) x = x < 5.42101e-20f ? 5.42101e-20f : x > 1.884467e+19f ? 1.884467e+19f : x;
			else x = x < -1.884467e+19f ? -1.884467e+19f : x > -5.42101e-20f ? -5.42101e-20f : x;
			l[0] = l[1] = l[2] = l[3] = x;
			break;
		case 4: l[0] = l[1] = l[2] = l[3] = 1.0f / sqrtf(fabsf(c[0])); break;
		case 5: l[0] = exp2f(floorf(c[0])); l[1] = c[0] - floorf(c[0]); l[2] = exp2f(c[0]); l[3] = 1.0f; break;
		case 6:
			x = fabsf(c[0]);
			if (x == 0.0f) { l[0] = -1.0e30f; l[1] = 1.0f; l[2] = -1.0e30f; l[3] = 1.0f; }
			else { float e = floorf(log2f(x)); l[0] = e; l[1] = x / exp2f(e); l[2] = log2f(x); l[3] = 1.0f; }
			break;
		case 7:
			l[0] = 1.0f; l[1] = c[0] > 0.0f ? c[0] : 0.0f;
			l[2] = c[0] > 0.0f ? powf(c[1] > 0.0f ? c[1] : 0.0f, c[3] < -127.9961f ? -127.9961f : c[3] > 127.9961f ? 127.9961f : c[3]) : 0.0f;
			l[3] = 1.0f;
			break;
		default: break;
		}
		if (mac == 13)
			context->a0 = floorf(m[0] + 0.001f);
		else if (mac && mac_mask)
			vsh_write(context->r[temporary < 13 ? temporary : 0], mac_mask, m);
		if (ilu && ilu_mask)
			vsh_write(context->r[mac ? 1 : (temporary < 13 ? temporary : 0)], ilu_mask, l);
		if (output_mask && (output_from_ilu ? ilu : mac) && output_is_register && output_address == 0)
			vsh_write(context->r[12], output_mask, output_from_ilu ? l : m);
		if (output_mask && (output_from_ilu ? ilu : mac) && output_is_register && output_address == 3)
			vsh_write(diffuse, output_mask, output_from_ilu ? l : m);
		if (field(instruction, 3, 0, 1))
			break;
	}
	(void)scratch;
	memcpy(position, context->r[12], sizeof(float) * 4);
}

static void vertex_inputs(struct vsh_context *context, unsigned long vertex, unsigned long input_mask,
	unsigned long packed_mask, unsigned long color_mask)
{
	const struct vgxm_draw *draw = context->draw;
	unsigned long reg, attribute;

	for (reg = 0; reg < 16; reg++)
	{
		float *value = context->v[reg];

		value[0] = value[1] = value[2] = 0.0f;
		value[3] = 1.0f;
		if (!(input_mask & (1ul << reg)))
			continue;
		for (attribute = 0; attribute < draw->attribute_count; attribute++)
			if (draw->attributes[attribute].reg == reg)
				break;
		if (attribute < draw->attribute_count)
		{
			const struct vgxm_attribute *a = &draw->attributes[attribute];
			const unsigned char *bytes = a->stream < draw->stream_count && draw->streams[a->stream] ?
				(const unsigned char *)draw->streams[a->stream] + vertex * draw->strides[a->stream] + a->offset : NULL;
			unsigned long component;

			for (component = 0; bytes && component < a->components && component < 4; component++)
			{
				switch (a->format)
				{
				case _vgxm_attribute_f32: memcpy(&value[component], bytes + 4 * component, 4); break;
				case _vgxm_attribute_u8n: value[component] = bytes[component] / 255.0f; break;
				case _vgxm_attribute_u8: value[component] = (float)bytes[component]; break;
				case _vgxm_attribute_s16: { short v; memcpy(&v, bytes + 2 * component, 2); value[component] = (float)v; break; }
				default: { short v; memcpy(&v, bytes + 2 * component, 2); value[component] = v / 32767.0f; break; }
				}
			}
			if (packed_mask & (1ul << reg))
			{
				float bx = value[0], by = value[1], bz = value[2], bw = value[3];
				float x = bx + fmodf(by, 8.0f) * 256.0f;
				float y = floorf(by / 8.0f) + fmodf(bz, 64.0f) * 32.0f;
				float z = floorf(bz / 64.0f) + bw * 4.0f;

				x = x >= 1024.0f ? x - 2048.0f : x;
				y = y >= 1024.0f ? y - 2048.0f : y;
				z = z >= 512.0f ? z - 1024.0f : z;
				value[0] = x / 1023.0f; value[1] = y / 1023.0f; value[2] = z / 511.0f; value[3] = 1.0f;
			}
			else if (color_mask & (1ul << reg))
			{
				float swap = value[0];

				value[0] = value[2];
				value[2] = swap;
			}
		}
		else if (draw->vertex_uniforms)
			memcpy(value, (const unsigned char *)draw->vertex_uniforms + (VITA_VM_ATTRIBUTES + reg) * 16, sizeof(float) * 4);
	}
}

/* ---------- rasterizing */

struct fill_vertex
{
	/* clip space (x, y, z, w) and the Xbox's screen position for the winding */
	float clip[4];
	float screen_x, screen_y;
	/* oD0's alpha, saturated as the Cg does */
	float alpha;
};

static unsigned long *vertex_stamp;
static struct fill_vertex *vertex_cache;
static unsigned long vertex_generation;

static int depth_passes(unsigned long function, float z, float stored)
{
	const float epsilon = 2e-5f;

	switch (function)
	{
	case 0x200: return 0;
	case 0x201: return z < stored - epsilon * 0.0f && z < stored;
	case 0x202: return fabsf(z - stored) <= epsilon;
	case 0x203: return z <= stored + epsilon;
	case 0x204: return z > stored;
	case 0x205: return fabsf(z - stored) > epsilon;
	case 0x206: return z >= stored - epsilon;
	default: return 1;
	}
}

struct raster_context
{
	const struct vgxm_draw *draw;
	float *depth;
	long clip[4];
	double covered, passed;
	int bin_triangles;
	int alpha_bin;
	int sky_order_check;
};

/* one triangle in window coordinates (x, y, z), x/y in the game's pixels */
static void raster_triangle(struct raster_context *raster, const float a[3], const float b[3], const float c[3])
{
	float area = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
	float minx, maxx, miny, maxy;
	long x0, x1, y0, y1, x, y;
	double covered = 0, passed = 0;
	const int stride = FILL_MAX_WIDTH / FILL_GRID;

	if (area == 0.0f || area != area)
		return;
	minx = fminf(a[0], fminf(b[0], c[0]));
	maxx = fmaxf(a[0], fmaxf(b[0], c[0]));
	miny = fminf(a[1], fminf(b[1], c[1]));
	maxy = fmaxf(a[1], fmaxf(b[1], c[1]));
	if (maxx < (float)raster->clip[0] || minx > (float)raster->clip[2] || maxy < (float)raster->clip[1] || miny > (float)raster->clip[3])
		return;
	/* samples at (G*i + G/2, G*j + G/2) */
	x0 = (long)floorf((fmaxf(minx, (float)raster->clip[0]) - FILL_GRID / 2.0f) / FILL_GRID);
	x1 = (long)ceilf((fminf(maxx, (float)raster->clip[2]) - FILL_GRID / 2.0f) / FILL_GRID);
	y0 = (long)floorf((fmaxf(miny, (float)raster->clip[1]) - FILL_GRID / 2.0f) / FILL_GRID);
	y1 = (long)ceilf((fminf(maxy, (float)raster->clip[3]) - FILL_GRID / 2.0f) / FILL_GRID);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= stride) x1 = stride - 1;
	if (y1 >= FILL_MAX_HEIGHT / FILL_GRID) y1 = FILL_MAX_HEIGHT / FILL_GRID - 1;
	for (y = y0; y <= y1; y++)
	{
		float py = (float)(y * FILL_GRID) + FILL_GRID / 2.0f;

		if (py < (float)raster->clip[1] || py >= (float)raster->clip[3])
			continue;
		for (x = x0; x <= x1; x++)
		{
			float px = (float)(x * FILL_GRID) + FILL_GRID / 2.0f;
			float w0 = ((b[0] - px) * (c[1] - py) - (c[0] - px) * (b[1] - py)) / area;
			float w1 = ((c[0] - px) * (a[1] - py) - (a[0] - px) * (c[1] - py)) / area;
			float w2 = 1.0f - w0 - w1;
			float z;

			if (px < (float)raster->clip[0] || px >= (float)raster->clip[2])
				continue;
			if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
				continue;
			z = w0 * a[2] + w1 * b[2] + w2 * c[2];
			if (z < 0.0f || z > 1.0f)
				continue;
			covered++;
			if (raster->sky_order_check && raster->depth && raster->depth[y * stride + x] >= 1.0f &&
				(!raster->draw->depth_test || depth_passes(raster->draw->depth_function, z, raster->depth[y * stride + x])))
				sky_order_samples++;
			if (raster->depth && raster->draw->depth_test)
			{
				float *stored = &raster->depth[y * stride + x];

				if (!depth_passes(raster->draw->depth_function, z, *stored))
					continue;
				if (raster->draw->depth_write)
					*stored = z;
			}
			else if (raster->depth && raster->draw->depth_write)
				raster->depth[y * stride + x] = z;
			passed++;
		}
	}
	raster->covered += covered;
	raster->passed += passed;
	if (raster->bin_triangles)
	{
		double pixels = passed * FILL_GRID * FILL_GRID;
		int bin;

		for (bin = 0; bin < SIZE_BINS - 1 && fabs(area) * 0.5 >= size_bin_limit[bin]; bin++)
			;
		size_bin_count[bin]++;
		size_bin_pixels[bin] += pixels;
		alpha_bin_pixels[raster->alpha_bin] += pixels;
	}
}

static void project(const struct vgxm_draw *draw, const float clip[4], float out[3])
{
	float inverse = 1.0f / clip[3];

	out[0] = clip[0] * inverse * draw->viewport_scale[0] + draw->viewport_offset[0];
	out[1] = clip[1] * inverse * draw->viewport_scale[1] + draw->viewport_offset[1];
	out[2] = clip[2] * inverse * draw->viewport_scale[2] + draw->viewport_offset[2];
}

/* a triangle clipped against w > epsilon (a polygon of up to 4 vertices), then fanned */
static void raster_clipped(struct raster_context *raster, const struct fill_vertex *v[3])
{
	const float epsilon = 1e-4f;
	float polygon[4][4];
	int count = 0, i;
	float window[4][3];

	for (i = 0; i < 3; i++)
	{
		const float *p = v[i]->clip, *q = v[(i + 1) % 3]->clip;
		int p_in = p[3] > epsilon, q_in = q[3] > epsilon;

		if (p_in)
			memcpy(polygon[count++], p, sizeof(polygon[0]));
		if (p_in != q_in && count < 4)
		{
			float t = (epsilon - p[3]) / (q[3] - p[3]);
			int k;

			for (k = 0; k < 4; k++)
				polygon[count][k] = p[k] + t * (q[k] - p[k]);
			count++;
		}
	}
	if (count < 3)
		return;
	for (i = 0; i < count; i++)
		project(raster->draw, polygon[i], window[i]);
	for (i = 1; i + 1 < count; i++)
		raster_triangle(raster, window[0], window[i], window[i + 1]);
}

void halo_fill_stats_draw(const struct vgxm_draw *draw, const DWORD *instructions, unsigned long instruction_count,
	unsigned long input_mask, unsigned long packed_mask, unsigned long color_mask, int phase,
	unsigned long color_id, unsigned long depth_id)
{
	struct vsh_context context;
	struct raster_context raster;
	struct fill_phase *stats;
	unsigned long i, n = draw->index_count, triangles = 0;
	const unsigned short *x = draw->indices;
	int main_target;
	float scale[3], offset[3], misc_y;
	const float *vm = (const float *)draw->vertex_uniforms;

	if (!fill_sampled || !x || !instructions || !vm)
		return;
	if (draw->primitive < 5 || draw->primitive > 7)
		return;
	if (!vertex_cache)
	{
		vertex_cache = malloc(sizeof(*vertex_cache) * 65536);
		vertex_stamp = calloc(65536, sizeof(*vertex_stamp));
		if (!vertex_cache || !vertex_stamp)
			return;
	}
	vertex_generation++;
	main_target = color_id && color_id == main_color_id;
	if (phase < 0 || phase >= FILL_OTHER)
		phase = FILL_OTHER;
	stats = &phases[phase][main_target ? 0 : 1];
	memset(&context, 0, sizeof(context));
	context.draw = draw;
	for (i = 0; i < 3; i++)
	{
		scale[i] = vm[VITA_VM_VIEWPORT_SCALE * 4 + i] != 0.0f ? vm[VITA_VM_VIEWPORT_SCALE * 4 + i] : 1.0f;
		offset[i] = vm[VITA_VM_VIEWPORT_OFFSET * 4 + i];
	}
	misc_y = vm[VITA_VM_MISCELLANEOUS * 4 + 1];
	memset(&raster, 0, sizeof(raster));
	raster.draw = draw;
	raster.depth = draw->depth_test || draw->depth_write ? depth_for(depth_id) : NULL;
	memcpy(raster.clip, draw->clip, sizeof(raster.clip));
	if (raster.clip[2] > FILL_MAX_WIDTH) raster.clip[2] = FILL_MAX_WIDTH;
	if (raster.clip[3] > FILL_MAX_HEIGHT) raster.clip[3] = FILL_MAX_HEIGHT;
	/* (the effects: particles' sprites - transparent groups of effect shaders) */
	raster.bin_triangles = (phase == 42 || phase == 53) && main_target && draw->blend && draw->color_write;
	raster.sky_order_check = (phase == 3 || phase == 5) && main_target && draw->color_write &&
		(draw->blend || !draw->depth_test || !draw->depth_write);
	for (i = 0; i + 2 < n; i += draw->primitive == 5 ? 3 : 1)
	{
		const struct fill_vertex *v[3];
		unsigned long index[3];
		int k;
		float area;

		if (draw->primitive == 5)
			index[0] = x[i], index[1] = x[i + 1], index[2] = x[i + 2];
		else if (draw->primitive == 6)
		{
			if (i & 1)
				index[0] = x[i + 1], index[1] = x[i], index[2] = x[i + 2];
			else
				index[0] = x[i], index[1] = x[i + 1], index[2] = x[i + 2];
		}
		else
			index[0] = x[0], index[1] = x[i + 1], index[2] = x[i + 2];
		if (index[0] == index[1] || index[1] == index[2] || index[0] == index[2])
			continue;
		for (k = 0; k < 3; k++)
		{
			struct fill_vertex *vertex = &vertex_cache[index[k]];

			if (vertex_stamp[index[k]] != vertex_generation)
			{
				float position[4], ndc[3], diffuse[4];

				vertex_inputs(&context, index[k], input_mask, packed_mask, color_mask);
				vsh_run(&context, instructions, instruction_count, position, diffuse);
				vertex->alpha = diffuse[3] < 0.0f ? 0.0f : diffuse[3] > 1.0f ? 1.0f : diffuse[3];
				ndc[0] = (position[0] + 0.5f + misc_y - offset[0]) / scale[0];
				ndc[1] = (position[1] + 0.5f - offset[1]) / scale[1];
				ndc[2] = (position[2] - offset[2]) / scale[2];
				vertex->clip[0] = ndc[0] * position[3];
				vertex->clip[1] = ndc[1] * position[3];
				vertex->clip[2] = ndc[2] * position[3];
				vertex->clip[3] = position[3];
				vertex->screen_x = position[0];
				vertex->screen_y = position[1];
				vertex_stamp[index[k]] = vertex_generation;
			}
			v[k] = vertex;
		}
		/* the winding as the Xbox saw it (its screen, y down); behind the
		eye (w < 0) the screen position flips, so only for triangles in front */
		if (draw->cull && v[0]->clip[3] > 0.0f && v[1]->clip[3] > 0.0f && v[2]->clip[3] > 0.0f)
		{
			area = (v[1]->screen_x - v[0]->screen_x) * (v[2]->screen_y - v[0]->screen_y) -
				(v[2]->screen_x - v[0]->screen_x) * (v[1]->screen_y - v[0]->screen_y);
			if ((draw->cull == 0x900 && area > 0.0f) || (draw->cull == 0x901 && area < 0.0f))
				continue;
		}
		triangles++;
		if (raster.bin_triangles)
		{
			float mean = (v[0]->alpha + v[1]->alpha + v[2]->alpha) / 3.0f;

			for (raster.alpha_bin = 0; raster.alpha_bin < ALPHA_BINS - 1 && mean > alpha_bin_limit[raster.alpha_bin];
				raster.alpha_bin++)
				;
		}
		raster_clipped(&raster, v);
	}
	stats->draws++;
	stats->triangles += triangles;
	stats->covered += raster.covered;
	if (draw->color_write)
	{
		stats->passed += raster.passed;
		if (draw->blend)
			stats->blended += raster.passed;
	}
	if (!raster.covered)
		stats->offscreen_draws++;
	else if (!raster.passed)
		stats->hidden_draws++;
}

void halo_fill_stats_present(unsigned long color_id, unsigned long width, unsigned long height)
{
	int phase, target;

	if (fill_every < 0)
		halo_fill_stats_sampled();
	if (!fill_every)
		return;
	if (fill_sampled)
	{
		fill_sampled_frames++;
		if (color_id == main_color_id && main_area <= 0.0)
			main_area = (double)width * height;
	}
	if (color_id != main_color_id)
	{
		/* the presented target is the main one: (re)start the counts */
		main_color_id = color_id;
		main_area = (double)width * height;
		memset(phases, 0, sizeof(phases));
		memset(size_bin_count, 0, sizeof(size_bin_count));
		memset(alpha_bin_pixels, 0, sizeof(alpha_bin_pixels));
		memset(size_bin_pixels, 0, sizeof(size_bin_pixels));
		fill_sampled_frames = 0;
	}
	if (fill_sampled_frames >= 30 && main_area > 0.0)
	{
		double samples_per_screen = main_area / (FILL_GRID * FILL_GRID) * fill_sampled_frames;
		double total[4] = { 0, 0, 0, 0 };
		char line[2400];
		int length = 0;

		for (phase = 0; phase < FILL_PHASES; phase++)
		{
			total[0] += phases[phase][0].covered;
			total[1] += phases[phase][0].passed;
			total[2] += phases[phase][0].blended;
			total[3] += phases[phase][1].passed;
		}
		platform_log("fill-stats (%lu frames, main target %.0fx%.0f px, in screens/frame): covered %.2f shaded %.2f "
			"blended %.2f | other targets shaded %.2f screens",
			fill_sampled_frames, (double)width, (double)height, total[0] / samples_per_screen, total[1] / samples_per_screen,
			total[2] / samples_per_screen, total[3] / samples_per_screen);
		for (target = 0; target < 2; target++)
		{
			length = 0;
			for (phase = 0; phase < FILL_PHASES && length < (int)sizeof(line) - 120; phase++)
			{
				const struct fill_phase *p = &phases[phase][target];
				const char *name = phase == FILL_OTHER ? "other" : halo_render_phase_names[phase];

				if (!p->draws)
					continue;
				length += snprintf(line + length, sizeof(line) - length, " %s %.0f/%.0f/%.2f/%.2f/%.2f/%.0f/%.0f",
					name ? name : "?", p->draws / fill_sampled_frames, p->triangles / fill_sampled_frames,
					p->covered / samples_per_screen, p->passed / samples_per_screen, p->blended / samples_per_screen,
					p->hidden_draws / fill_sampled_frames, p->offscreen_draws / fill_sampled_frames);
			}
			platform_log("fill-phases %s (draws/triangles/covered/shaded/blended screens/hidden draws/offscreen draws, per frame):%s",
				target ? "other targets" : "main", line);
		}
		length = 0;
		for (phase = 0; phase < SIZE_BINS; phase++)
			length += snprintf(line + length, sizeof(line) - length, " <%.0f:%.0f/%.3f", size_bin_limit[phase] > 1e29 ? 1e9 : size_bin_limit[phase],
				size_bin_count[phase] / fill_sampled_frames, size_bin_pixels[phase] / (FILL_GRID * FILL_GRID) / samples_per_screen);
		platform_log("fill-effects (effect shaders' triangles by area in px: count/shaded screens per frame):%s", line);
		length = 0;
		for (phase = 0; phase < ALPHA_BINS; phase++)
			length += snprintf(line + length, sizeof(line) - length, " %s%.2f:%.3f", phase ? "<=" : "=", alpha_bin_limit[phase],
				alpha_bin_pixels[phase] / (FILL_GRID * FILL_GRID) / samples_per_screen);
		platform_log("fill-effects by vertex alpha (shaded screens per frame):%s", line);
		platform_log("fill-sky-order: %.0f samples a frame coloured at the cleared depth without covering it before the late sky",
			sky_order_samples / fill_sampled_frames);
		sky_order_samples = 0;
		memset(alpha_bin_pixels, 0, sizeof(alpha_bin_pixels));
		memset(phases, 0, sizeof(phases));
		memset(size_bin_count, 0, sizeof(size_bin_count));
		memset(size_bin_pixels, 0, sizeof(size_bin_pixels));
		fill_sampled_frames = 0;
	}
	fill_frame++;
	fill_sampled = fill_every > 0 && fill_frame % (unsigned long)fill_every == 0;
}
