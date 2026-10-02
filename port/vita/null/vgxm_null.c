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
	texture->control[0] = (unsigned long)data;
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

void vgxm_draw(const struct vgxm_draw *draw)
{
	if (!null.ready || !draw->index_count)
		return;
	null.draws++;
	null.scene_draws++;
}

void vgxm_clear(unsigned long flags, unsigned long color, float depth, unsigned long stencil, const long clip[4])
{
	(void)flags;
	(void)color;
	(void)depth;
	(void)stencil;
	(void)clip;
	null.clears++;
}

void vgxm_visibility_frame(unsigned long frame)
{
	(void)frame;
}

int vgxm_visibility_newest(unsigned long *frame)
{
	(void)frame;
	return -1;
}

unsigned long vgxm_visibility_count(int buffer, unsigned long slot)
{
	(void)buffer;
	(void)slot;
	return 0;
}

void vgxm_present(unsigned long color_target, unsigned long width, unsigned long height)
{
	(void)color_target;
	(void)width;
	(void)height;
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

const void *vgxm_target_pixels(unsigned long color_target, unsigned long *pitch, unsigned long *width,
	unsigned long *height)
{
	(void)color_target;
	(void)width;
	(void)height;
	*pitch = 0;
	return NULL;
}
