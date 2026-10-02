/*
VGXM_NULL.C

The GXM renderer's interface (port/vita/include/vita_gxm.h) and the Vita
host's (vita_host.h) for a Linux build of the Vita's Direct3D device
(configure.py --linux-d3d gxm-null): every call the device makes is
answered, and nothing is drawn. The device's own work - the records, the
worker thread, the ring and uniform snapshots, the texture cache's decoding,
the shader translation - runs exactly as on the Vita, so its CPU cost per
draw and per frame ("frame N:" statistics under HALO_GPU_STATS=1,
HALO_DRAW_PROFILE=1) can be measured on any Linux machine, an ARM board in
particular, without the hardware. Shaders are identified by the hash of
their Cg; they are never compiled.
*/

#define _GNU_SOURCE
#include "platform.h"
#include "vita_gxm.h"
#include "vita_host.h"

#include <pthread.h>
#include <stddef.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---------- the host */

void *vita_host_arena(unsigned long *size)
{
	*size = PLATFORM_CONTIGUOUS_SIZE;
	return (void *)PLATFORM_CONTIGUOUS_BASE;
}

void vita_host_log(const char *line)
{
	platform_log("%s", line);
}

void vita_host_log_memory(const char *when)
{
	(void)when;
}

/* (vita_host_time_us, vita_host_sleep_us and vita_host_pin_current_thread
are the Linux platform layer's own: port/linux/src/posix_profile.c) */
void vita_host_pin_current_thread(int core);

struct thread_start
{
	void (*function)(void *);
	void *argument;
	int core;
};

static void *thread_trampoline(void *argument)
{
	struct thread_start start = *(struct thread_start *)argument;

	free(argument);
	vita_host_pin_current_thread(start.core);
	start.function(start.argument);
	return NULL;
}

int vita_host_thread_start(const char *name, void (*function)(void *), void *argument, int core)
{
	struct thread_start *start = malloc(sizeof(*start));
	pthread_t thread;

	(void)name;
	if (!start)
		return -1;
	start->function = function;
	start->argument = argument;
	start->core = core;
	if (pthread_create(&thread, NULL, thread_trampoline, start) != 0)
	{
		free(start);
		return -1;
	}
	pthread_detach(thread);
	return 0;
}

void vita_host_cpu_usage(unsigned char busy[3])
{
	busy[0] = busy[1] = busy[2] = 255;
}

/* (vita_pad.c's, which the device sets for the D-pad's meaning) */
int vita_menus_active;

void vita_host_pad_read(struct vita_host_pad *pad)
{
	memset(pad, 0, sizeof(*pad));
	pad->lx = pad->ly = pad->rx = pad->ry = 128;
}

/* ---------- the renderer */

/* as port/vita/host/vita_gxm.c */
#define RING_COUNT 4
#define RING_SIZE (6 * 1024 * 1024)
#define POOL_SIZE (56 * 1024 * 1024)
#define MAXIMUM_SHADERS 4096
#define MAXIMUM_TARGETS 256

static struct
{
	int ready;
	unsigned char *rings[RING_COUNT];
	unsigned int ring_offset, ring_index, ring_offset_peak;
	unsigned char *pool;
	unsigned int pool_offset;
	unsigned long long shader_hashes[MAXIMUM_SHADERS];
	unsigned int shader_count;
	unsigned int target_count;
	unsigned long draws, clears, presents, scene_draws;
	unsigned long color_target, depth_target;
} null;

int vgxm_initialize(void *arena, unsigned long arena_size)
{
	unsigned int index;

	(void)arena;
	(void)arena_size;
	if (null.ready)
		return 0;
	for (index = 0; index < RING_COUNT; index++)
	{
		null.rings[index] = malloc(RING_SIZE);
		if (!null.rings[index])
			return -1;
	}
	null.pool = malloc(POOL_SIZE);
	if (!null.pool)
		return -1;
	null.ready = 1;
	platform_log("gxm-null: the Vita's Direct3D device over a renderer that draws nothing (%u MB rings, %u MB pool)",
		RING_COUNT * RING_SIZE >> 20, POOL_SIZE >> 20);
	return 0;
}

/* FNV-1a over the source, with the kind folded in, as vita_gxm.c's */
static unsigned long long source_hash(const char *source, int fragment)
{
	unsigned long long hash = 1469598103934665603ull;

	while (*source)
		hash = (hash ^ (unsigned char)*source++) * 1099511628211ull;
	return hash ^ (fragment ? 1ull : 0ull);
}

volatile unsigned long long vgxm_compile_us;
volatile unsigned long vgxm_compiles;

unsigned long vgxm_shader_get(const char *source, int fragment)
{
	unsigned long long hash = source_hash(source, fragment);
	unsigned int index;

	for (index = 0; index < null.shader_count; index++)
		if (null.shader_hashes[index] == hash)
			return index + 1;
	if (null.shader_count >= MAXIMUM_SHADERS)
		return 0;
	null.shader_hashes[null.shader_count] = hash;
	return ++null.shader_count;
}

#define ALIGN(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))

void *vgxm_ring_alloc(unsigned long size, unsigned long alignment)
{
	unsigned int reserved = __atomic_fetch_add(&null.ring_offset, (unsigned int)(size + alignment), __ATOMIC_RELAXED);
	unsigned int offset = ALIGN(reserved, (unsigned int)alignment);

	if (offset + size > RING_SIZE)
	{
		static unsigned int reported;

		if (reported++ < 8)
			platform_log("gxm-null: the frame ring is full (%u bytes)", RING_SIZE);
		return NULL;
	}
	return null.rings[null.ring_index] + offset;
}

static unsigned char *null_worker_rings[RING_COUNT];
static unsigned int null_worker_offset, null_worker_index;

void *vgxm_worker_alloc(unsigned long size, unsigned long alignment)
{
	unsigned int offset = ALIGN(null_worker_offset, (unsigned int)alignment);

	if (!null_worker_rings[null_worker_index])
		null_worker_rings[null_worker_index] = malloc(2 * 1024 * 1024);
	if (!null_worker_rings[null_worker_index] || offset + size > 2 * 1024 * 1024)
		return NULL;
	null_worker_offset = offset + (unsigned int)size;
	return null_worker_rings[null_worker_index] + offset;
}

void vgxm_ring_next(unsigned long frame)
{
	if (null.ring_offset > null.ring_offset_peak)
		null.ring_offset_peak = null.ring_offset;
	null.ring_index = (unsigned int)(frame % RING_COUNT);
	__atomic_store_n(&null.ring_offset, 0u, __ATOMIC_RELEASE);
}

void *vgxm_pool_alloc(unsigned long size, unsigned long alignment)
{
	unsigned int offset = ALIGN(null.pool_offset, (unsigned int)alignment);

	if (offset + size > POOL_SIZE)
		return NULL;
	null.pool_offset = offset + (unsigned int)size;
	return null.pool + offset;
}

static unsigned int pool_floor;

void vgxm_pool_reset(void)
{
	if (!pool_floor)
		pool_floor = 65536 * 2;
	null.pool_offset = pool_floor;
}

unsigned long vgxm_pool_used(void)
{
	return null.pool_offset;
}

int vgxm_texture_initialize(struct vgxm_texture *texture, const void *data, unsigned long format,
	unsigned long layout, unsigned long width, unsigned long height, unsigned long levels)
{
	/* control words that differ whenever the texture does, as GXM's would */
	/* (an offset in the pool, not the address: the same in every run, for
	the draw hash) */
	texture->control[0] = (const unsigned char *)data >= null.pool && (const unsigned char *)data < null.pool + POOL_SIZE ?
		(unsigned long)((const unsigned char *)data - null.pool) : (unsigned long)data;
	texture->control[1] = format | layout << 4 | levels << 8;
	texture->control[2] = width | height << 16;
	texture->control[3] = 0;
	return 0;
}

void vgxm_texture_set_sampler(struct vgxm_texture *texture, unsigned long min_filter, unsigned long mag_filter,
	unsigned long mip_filter, unsigned long address_u, unsigned long address_v, float lod_bias)
{
	texture->control[3] = min_filter | mag_filter << 4 | mip_filter << 8 | address_u << 12 | address_v << 16 |
		((unsigned long)(lod_bias * 16.0f) & 0xff) << 20;
}

unsigned long vgxm_target_create(unsigned long width, unsigned long height, int depth, struct vgxm_texture *texture)
{
	if (null.target_count >= MAXIMUM_TARGETS)
		return 0;
	null.target_count++;
	if (texture)
	{
		texture->control[0] = 0x80000000ul | null.target_count;
		texture->control[1] = depth ? 0x100 : 0;
		texture->control[2] = width | height << 16;
		texture->control[3] = 0;
	}
	return null.target_count;
}

int vgxm_target_create_chain(unsigned long width, unsigned long height, unsigned long levels,
	unsigned long *ids, struct vgxm_texture *texture)
{
	unsigned long level;

	for (level = 0; level < levels; level++)
	{
		ids[level] = vgxm_target_create(width >> level ? width >> level : 1, height >> level ? height >> level : 1, 0,
			level ? NULL : texture);
		if (!ids[level])
			return -1;
	}
	return 0;
}

void vgxm_set_targets(unsigned long color, unsigned long depth)
{
	null.color_target = color;
	null.depth_target = depth;
}

/* HALO_DRAW_HASH=1: every draw and clear folded into a hash of what the GPU
would be given - the programs, the targets, the states, the bytes of each
uniform buffer, the texture words, the indices and the vertex bytes each
attribute reads - logged per frame ("draw hash"). With a repeatable run
(HALO_FIXED_DT=1 HALO_TICK_THREAD=0) two builds that draw the same frames
log the same hashes, whatever their records look like on the way. */
static int draw_hash_on = -1;
static unsigned long long draw_hash, draw_hash_draws;

static void hash_bytes(const void *data, unsigned long size)
{
	const unsigned char *bytes = data;
	unsigned long long hash = draw_hash;

	while (size--)
		hash = (hash ^ *bytes++) * 1099511628211ull;
	draw_hash = hash;
}

static void hash_word(unsigned long long value)
{
	hash_bytes(&value, sizeof(value));
}

/* uniform words, each NaN as one value: the game uploads some vectors
whose unused lane is whatever was on its stack (C1's lighting rows), a NaN
that differs from run to run */
static void hash_floats(const void *data, unsigned long size)
{
	const unsigned int *words = data;
	unsigned long index;

	for (index = 0; index < size / 4; index++)
	{
		unsigned int word = words[index];

		if ((word & 0x7f800000u) == 0x7f800000u && (word & 0x007fffffu))
			word = 0x7fc00000u;
		hash_bytes(&word, sizeof(word));
	}
}

static int draw_hash_enabled(void)
{
	if (draw_hash_on < 0)
	{
		const char *setting = getenv("HALO_DRAW_HASH");

		draw_hash_on = setting && atoi(setting) != 0;
		draw_hash = 1469598103934665603ull;
	}
	return draw_hash_on;
}

static unsigned long attribute_bytes(const struct vgxm_attribute *attribute)
{
	switch (attribute->format)
	{
	case _vgxm_attribute_f32: return 4ul * attribute->components;
	case _vgxm_attribute_s16: case _vgxm_attribute_s16n: return 2ul * attribute->components;
	default: return attribute->components;
	}
}

/* HALO_DRAW_HASH_TRACE=n: the running hash after each part of every draw
of present n, to find what two runs disagree on */
static long draw_hash_trace = -2;
#define TRACE_PART(name) do { if (draw_hash_trace == (long)null.presents) \
	platform_log("draw hash trace: draw %llu %s %016llx", draw_hash_draws, name, draw_hash); } while (0)

static void draw_hash_add(const struct vgxm_draw *draw)
{
	static const unsigned long chunk_registers[6] = { 12, 5, 11, 32, 0, 8 };
	unsigned long index, low = 0xffff, high = 0, attribute;

	if (draw_hash_trace == -2)
	{
		const char *setting = getenv("HALO_DRAW_HASH_TRACE");

		draw_hash_trace = setting ? atol(setting) : -1;
	}
	hash_word(0xd7a3);
	hash_word(null.color_target);
	hash_word(null.depth_target);
	hash_word(draw->vertex_shader);
	hash_word(draw->fragment_shader);
	hash_word(draw->attribute_count);
	for (attribute = 0; attribute < draw->attribute_count; attribute++)
	{
		const struct vgxm_attribute *a = &draw->attributes[attribute];

		hash_word(a->reg | (unsigned long)a->format << 8 | (unsigned long)a->components << 16);
	}
	TRACE_PART("programs+attributes");
	for (index = 0; index < 6; index++)
	{
		unsigned long registers = index == 4 ? draw->vertex_chunk_d_registers : chunk_registers[index];

		hash_word(draw->vertex_chunks[index] ? registers : 0xffffffff);
		if (draw->vertex_chunks[index])
			hash_floats(draw->vertex_chunks[index], registers * 16);
		TRACE_PART("chunk");
	}
	if (draw->vertex_uniforms)
	{
		/* the viewport and miscellaneous rows, and the current value of
		each input the program reads that no stream feeds */
		unsigned long provided = 0;

		for (attribute = 0; attribute < draw->attribute_count; attribute++)
			provided |= 1ul << draw->attributes[attribute].reg;
		hash_floats(draw->vertex_uniforms, 3 * 16);
		for (index = 0; index < 16; index++)
			if ((draw->vertex_input_mask & ~provided) & (1ul << index))
				hash_floats((const unsigned char *)draw->vertex_uniforms + (3 + index) * 16, 16);
	}
	TRACE_PART("vertex uniforms");
	if (draw->fragment_uniforms[0])
		hash_floats(draw->fragment_uniforms[0], 18 * 16);
	if (draw->fragment_uniforms[1])
		hash_floats(draw->fragment_uniforms[1], 15 * 16);
	TRACE_PART("fragment uniforms");
	for (index = 0; index < 4; index++)
	{
		if (draw->textures[index])
			hash_bytes(draw->textures[index]->control, sizeof(draw->textures[index]->control));
		else
			hash_word(0);
	}
	TRACE_PART("textures");
	hash_bytes(&draw->depth_test, offsetof(struct vgxm_draw, primitive) - offsetof(struct vgxm_draw, depth_test));
	hash_word(draw->primitive);
	hash_word(draw->index_count);
	hash_word(draw->visibility_index);
	TRACE_PART("states");
	if (draw->indices)
	{
		hash_bytes(draw->indices, draw->index_count * sizeof(unsigned short));
		for (index = 0; index < draw->index_count; index++)
		{
			if (draw->indices[index] < low)
				low = draw->indices[index];
			if (draw->indices[index] > high)
				high = draw->indices[index];
		}
	}
	/* the bytes each attribute reads, vertex by vertex, in the referenced range */
	for (index = low; draw->indices && index <= high; index++)
	{
		for (attribute = 0; attribute < draw->attribute_count; attribute++)
		{
			const struct vgxm_attribute *a = &draw->attributes[attribute];

			if (a->stream < draw->stream_count && draw->streams[a->stream])
				hash_bytes((const unsigned char *)draw->streams[a->stream] + index * draw->strides[a->stream] + a->offset,
					attribute_bytes(a));
		}
	}
	TRACE_PART("vertices");
	draw_hash_draws++;
}

void vgxm_draw(const struct vgxm_draw *draw)
{
	if (!null.ready || !draw->index_count)
		return;
	null.draws++;
	null.scene_draws++;
	if (draw_hash_enabled())
		draw_hash_add(draw);
}

void vgxm_clear(unsigned long flags, unsigned long color, float depth, unsigned long stencil, const long clip[4])
{
	null.clears++;
	if (draw_hash_enabled())
	{
		hash_word(0xc1ea);
		hash_word(null.color_target);
		hash_word(null.depth_target);
		hash_word(flags);
		hash_word(color);
		hash_bytes(&depth, sizeof(depth));
		hash_word(stencil);
		hash_bytes(clip, 4 * sizeof(long));
	}
}

unsigned long vgxm_visibility_result(unsigned long index)
{
	(void)index;
	return 0;
}

void vgxm_present(unsigned long color_target, unsigned long width, unsigned long height)
{
	(void)color_target;
	(void)width;
	(void)height;
	if (draw_hash_enabled())
	{
		platform_log("draw hash: present %lu draws %llu hash %016llx", null.presents, draw_hash_draws, draw_hash);
		draw_hash = 1469598103934665603ull;
		draw_hash_draws = 0;
	}
	null.presents++;
	null.scene_draws = 0;
	null_worker_index = (null_worker_index + 1) % RING_COUNT;
	null_worker_offset = 0;
}

const char *vgxm_counts(void)
{
	static char line[160];

	snprintf(line, sizeof(line), "shaders %u, programs (null), targets %u, ring peak %u KB",
		null.shader_count, null.target_count, null.ring_offset_peak / 1024);
	return line;
}

void vgxm_overlay_set(float fps, float tick_ms, float render_ms)
{
	(void)fps;
	(void)tick_ms;
	(void)render_ms;
}

const void *vgxm_target_pixels(unsigned long color_target, unsigned long *pitch)
{
	(void)color_target;
	*pitch = 0;
	return NULL;
}
