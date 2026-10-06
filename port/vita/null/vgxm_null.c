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
#define MAXIMUM_SHADERS 4096
#define MAXIMUM_TARGETS 256

static struct
{
	int ready;
	unsigned char *rings[RING_COUNT];
	unsigned int ring_offset, ring_index, ring_offset_peak;
	unsigned long long shader_hashes[MAXIMUM_SHADERS];
	unsigned int shader_count;
	unsigned int target_count;
	unsigned long draws, clears, presents, scene_draws;
	unsigned long color_target, depth_target;
} null;

#define ALIGN(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))

/* the video memory's bookkeeping, as the Vita's (port/vita/include/
vgxm_memory.h): blocks of the C heap stand in for CDRAM and user RAM, and
the free CDRAM is the model's (112 MB less GXM's parameter buffer, the
display and everything the renderer holds), so a live change finds the
memory a Vita would, and HALO_CDRAM_RESERVE_MB makes it as tight */
struct block
{
	int uid;
	void *base;
	unsigned int size;
};

#define log_line platform_log

static int memory_os_allocate(int user, unsigned int size, const char *name, struct block *block)
{
	static int uids;

	(void)user;
	(void)name;
	block->base = malloc(size);
	if (!block->base)
		return 0;
	block->uid = ++uids;
	block->size = size;
	return 1;
}

static void memory_os_free(struct block *block)
{
	free(block->base);
}

static long memory_os_free_kb(int user)
{
	(void)user;
	return -1;
}

static unsigned long memory_target_headroom(void);

#include "vgxm_memory.h"

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
	{
		/* (GXM's parameter buffer and the two 960x544 display buffers, in
		the Vita's CDRAM: counted, not made) */
		const char *setting = getenv("HALO_GXM_PARAMETER_MB");
		unsigned int megabytes = setting ? (unsigned int)atoi(setting) : 40;

		if (megabytes < 8 || megabytes > 64)
			megabytes = 40;
		memory_configure(megabytes * 1024ul * 1024);
		memory_bytes[_memory_display] = 2 * ALIGN(960u * 544 * 4, CDRAM_ALIGNMENT);
	}
	if (!pool_initialize())
		return -1;
	null.ready = 1;
	platform_log("gxm-null: the Vita's Direct3D device over a renderer that draws nothing (%u MB rings, %u MB pool)",
		RING_COUNT * RING_SIZE >> 20, POOL_BYTES >> 20);
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

volatile unsigned long long vgxm_compile_us, vgxm_shader_load_us, vgxm_link_us, vgxm_cache_write_us;
volatile unsigned long vgxm_compiles, vgxm_shader_loads, vgxm_links, vgxm_compiles_background;

/* HALO_SHADER_COLLECT=<directory>: each program's Cg written there as
<hash>.vp.cg or <hash>.fp.cg, named by the Vita's hash of it (vita_gxm.c's
source_hash, which the shader cache and the shipped pack are keyed by), for
tools/vita_shader_pack.py */
static void shader_collect(const char *source, int fragment)
{
	static int checked;
	static const char *directory;
	unsigned long long hash = 14695981039346656037ull ^ (unsigned long long)fragment;
	const char *each;
	char path[1024];
	FILE *file;

	if (!checked)
	{
		checked = 1;
		directory = getenv("HALO_SHADER_COLLECT");
		if (directory && !*directory)
			directory = NULL;
	}
	if (!directory)
		return;
	for (each = source; *each; each++)
		hash = (hash ^ (unsigned char)*each) * 1099511628211ull;
	snprintf(path, sizeof(path), "%s/%016llx.%s.cg", directory, hash, fragment ? "fp" : "vp");
	if (access(path, F_OK) == 0)
		return;
	if ((file = fopen(path, "wb")) != NULL)
	{
		fwrite(source, 1, strlen(source), file);
		fclose(file);
	}
}

unsigned long vgxm_shader_get(const char *source, int fragment)
{
	unsigned long long hash = source_hash(source, fragment);
	unsigned int index;

	for (index = 0; index < null.shader_count; index++)
		if (null.shader_hashes[index] == hash)
			return index + 1;
	if (null.shader_count >= MAXIMUM_SHADERS)
		return 0;
	shader_collect(source, fragment);
	null.shader_hashes[null.shader_count] = hash;
	return ++null.shader_count;
}

unsigned long vgxm_shader_request(const char *source, int fragment)
{
	return vgxm_shader_get(source, fragment);
}


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
	return pool_allocate(size, alignment);
}

void vgxm_pool_reset(void)
{
	if (!pool_floor)
		pool_floor = 65536 * 2;
	pool_forget();
}

unsigned long vgxm_pool_used(void)
{
	return pool_offset + pool_jumbo_bytes;
}

int vgxm_texture_initialize(struct vgxm_texture *texture, const void *data, unsigned long format,
	unsigned long layout, unsigned long width, unsigned long height, unsigned long levels)
{
	/* control words that differ whenever the texture does, as GXM's would */
	/* (an offset in the pool, not the address: the same in every run, for
	the draw hash) */
	long offset = pool_offset_of(data);

	texture->control[0] = offset >= 0 ? (unsigned long)offset : (unsigned long)data;
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

void vgxm_texture_set_level_count(struct vgxm_texture *texture, unsigned long levels)
{
	/* (in the texture words' top bits, for the draw hash) */
	texture->control[1] = (texture->control[1] & 0x0ffffffful) | (unsigned long)(levels & 0xf) << 28;
}

/* the render scale, and each target's memory as the Vita's renderer makes
it (vita_gxm.c target_make: the screen-sized targets at the scale, rows of
32 pixels, depth in whole tiles, small colour targets in shares of 256 KB
blocks, the screen-sized ones' blocks kept after a live change), so a live
change takes and gives back what it would on the Vita */
static float null_render_scale = -1.0f;

static struct null_target
{
	struct block memory;
	int depth, scale_kind;
	unsigned int width, height, asked_width;
} null_targets[MAXIMUM_TARGETS + 1];

float vgxm_render_scale(void)
{
	if (null_render_scale < 0.0f)
	{
		const char *setting = getenv("HALO_RENDER_SCALE");

		null_render_scale = setting && strcmp(setting, "dynamic") ? (float)atof(setting) : 1.0f;
		if (null_render_scale < 0.5f || null_render_scale > 1.0f)
			null_render_scale = 1.0f;
	}
	return null_render_scale;
}

void vgxm_render_scale_set(float scale)
{
	null_render_scale = scale < 0.5f || scale > 1.0f ? 1.0f : scale;
}

static void null_target_release(unsigned long id)
{
	struct null_target *target;

	if (!id || id > MAXIMUM_TARGETS)
		return;
	target = &null_targets[id];
	target_memory_give_back(&target->memory, target->depth, target->scale_kind, target->width, target->height);
	memset(target, 0, sizeof(*target));
}

/* the memory of target id at this size; 0 when there is none */
static int null_target_make(unsigned long id, unsigned long width, unsigned long height, int depth)
{
	struct null_target *target;
	float scale = vgxm_render_scale();
	unsigned long asked_width = width, bytes;

	if (!id || id > MAXIMUM_TARGETS)
		return 0;
	target = &null_targets[id];
	memset(target, 0, sizeof(*target));
	target->scale_kind = height == 480 && width >= 640;
	if (scale < 1.0f && target->scale_kind)
	{
		width = (unsigned long)(width * scale + 0.5f) & ~1UL;
		height = (unsigned long)(height * scale + 0.5f) & ~1UL;
	}
	target->depth = depth;
	target->width = (unsigned int)width;
	target->height = (unsigned int)height;
	target->asked_width = (unsigned int)asked_width;
	bytes = depth ? 4 * ALIGN(width, 32) * ALIGN(height, 32) : 4 * ALIGN(width, 32) * height;
	if (!target_memory_get(&target->memory, (unsigned int)bytes, depth, target->scale_kind, target->width,
		target->height, depth ? "depth target" : "colour target"))
	{
		memset(target, 0, sizeof(*target));
		return 0;
	}
	return 1;
}

/* the dynamic resolution's rectangle, as on the Vita (vita_gxm.c
vgxm_render_rect_set): the screen-sized targets' textures describe it (the
words the draw hash reads) */
static float null_rect_wanted = 1.0f;
static unsigned long null_target_asked[MAXIMUM_TARGETS + 1];

float vgxm_render_rect(void)
{
	float scale = vgxm_render_scale();

	return null_rect_wanted < scale ? null_rect_wanted : scale;
}

float vgxm_render_rect_set(float scale)
{
	null_rect_wanted = scale < 0.25f || scale > 1.0f ? 1.0f : scale;
	return vgxm_render_rect();
}

static unsigned long null_rect_words(unsigned long width, unsigned long height)
{
	float scale = vgxm_render_rect();

	if (height == 480 && width >= 640 && scale < 1.0f)
	{
		width = (unsigned long)(width * scale + 0.5f) & ~1UL;
		height = (unsigned long)(height * scale + 0.5f) & ~1UL;
	}
	return width | height << 16;
}

void vgxm_target_texture(unsigned long id, struct vgxm_texture *texture)
{
	if (!id || id > MAXIMUM_TARGETS || !texture || !null_target_asked[id] || !null_targets[id].memory.base)
		return;
	texture->control[2] = null_rect_words(null_target_asked[id] & 0xffff, null_target_asked[id] >> 16);
}

void vgxm_frame_submit_begin(void)
{
}

int vgxm_gpu_frame_next(struct vgxm_gpu_frame *frame)
{
	/* (no GPU to time: d3d8_gxm.c's HALO_DYNRES_SIM stands in) */
	(void)frame;
	return 0;
}

void vgxm_overlay_dynamic(int dynamic)
{
	(void)dynamic;
}

/* atlases as on the Vita (vita_gxm.c vgxm_target_create_cell): a cell is
a target id of its own, without memory, whose scenes are its atlas's (the
scene count) */
static unsigned int cell_atlas[MAXIMUM_TARGETS + 1];
static struct { unsigned long key, width, height; unsigned int id, cells[24]; } null_atlases[6];

static int null_target_is_atlas(unsigned long id)
{
	int atlas;

	for (atlas = 0; atlas < 6; atlas++)
		if (null_atlases[atlas].id == id)
			return 1;
	return 0;
}

void vgxm_target_release(unsigned long id)
{
	if (id && id <= MAXIMUM_TARGETS && !cell_atlas[id])
		null_target_release(id);
}

void vgxm_target_stats(unsigned long *targets, unsigned long *cdram_bytes, unsigned long *cached_bytes,
	unsigned long *cdram_free)
{
	*targets = null.target_count;
	*cdram_bytes = memory_bytes[_memory_screen] + memory_bytes[_memory_target] + memory_bytes[_memory_small];
	*cached_bytes = memory_bytes[_memory_cache];
	*cdram_free = memory_cdram_free();
}

static unsigned long memory_target_headroom(void)
{
	unsigned long ceiling = 0, id;

	for (id = 1; id <= null.target_count && id <= MAXIMUM_TARGETS; id++)
		if (null_targets[id].scale_kind && null_targets[id].memory.base)
			ceiling += memory_screen_ceiling(null_targets[id].depth, null_targets[id].asked_width);
	return memory_headroom_from(ceiling);
}

unsigned long vgxm_cdram_free(void)
{
	return memory_cdram_free();
}

unsigned long vgxm_cdram_wanted(void)
{
	unsigned long wanted = memory_wanted_bytes;

	memory_wanted_bytes = 0;
	return wanted;
}

unsigned long vgxm_memory_trim(void)
{
	unsigned long freed = screen_blocks_flush();

	return freed + small_blocks_release();
}

unsigned long vgxm_pool_demote(void **base, unsigned long *size)
{
	return pool_demote(base, size);
}

int vgxm_pool_promote(void **base, unsigned long *size)
{
	return pool_promote(base, size);
}

unsigned long vgxm_targets_sweep(const unsigned char *referenced, unsigned long count)
{
	unsigned long id, bytes = 0, swept = 0;

	for (id = 1; id <= null.target_count && id <= MAXIMUM_TARGETS; id++)
	{
		if ((id < count && referenced[id]) || cell_atlas[id] || null_target_is_atlas(id) || !null_targets[id].memory.base)
			continue;
		if (null_targets[id].memory.uid)
			bytes += null_targets[id].memory.size;
		null_target_release(id);
		swept++;
	}
	if (swept)
		platform_log("gxm: %lu targets nothing refers to given back (%lu KB)", swept, bytes / 1024);
	return bytes;
}

void vgxm_memory_census(const char *const *roles, unsigned long count, int detail)
{
	char line[480];
	unsigned long id;

	memory_census_line(line, sizeof(line), 0);
	platform_log("cdram census: %s", line);
	memory_census_line(line, sizeof(line), 1);
	platform_log("cdram census: %s", line);
	if (!detail)
		return;
	for (id = 1; id <= null.target_count && id <= MAXIMUM_TARGETS; id++)
	{
		const struct null_target *target = &null_targets[id];
		const char *role = id < count && roles[id] ? roles[id] : null_target_is_atlas(id) ?
			"the copies of a small surface (the shadows, their blurs)" : "nothing refers to it";

		if (cell_atlas[id])
			continue;
		if (!target->memory.base)
		{
			if (id < count && roles[id])
				platform_log("cdram census: target %lu: no memory now (%s)", id, role);
			continue;
		}
		platform_log("cdram census: target %lu: %ux%u %s, %u KB %s%s: %s", id, target->width, target->height,
			target->depth ? "depth" : "colour", target->memory.size / 1024,
			target->memory.uid == -1 ? "share of a small-target block" : target->memory.uid == 0 ? "in its chain's block" :
			"block of its own", null_target_is_atlas(id) ? " (an atlas)" : target->scale_kind ? " (screen-sized)" : "", role);
	}
}

unsigned long vgxm_target_create(unsigned long width, unsigned long height, int depth, struct vgxm_texture *texture)
{
	/* (debug) HALO_TARGET_LIMIT=n, as on the Vita (vita_gxm.c) */
	static int limit = -1;

	if (limit < 0)
	{
		const char *setting = getenv("HALO_TARGET_LIMIT");

		limit = setting && atoi(setting) > 0 && atoi(setting) < MAXIMUM_TARGETS ? atoi(setting) : MAXIMUM_TARGETS;
	}
	if (null.target_count >= (unsigned int)limit || !width || !height)
		return 0;
	if (getenv("HALO_TRACE_FILES"))
		fprintf(stderr, "trace: target %lux%lu depth %d (%u made)\n", width, height, depth, null.target_count);
	if (!null_target_make(null.target_count + 1, width, height, depth))
		return 0;
	null.target_count++;
	if (null.target_count <= MAXIMUM_TARGETS)
		null_target_asked[null.target_count] = height == 480 && width >= 640 ? width | height << 16 : 0;
	if (texture)
	{
		texture->control[0] = 0x80000000ul | null.target_count;
		texture->control[1] = depth ? 0x100 : 0;
		texture->control[2] = null_rect_words(width, height);
		texture->control[3] = 0;
	}
	return null.target_count;
}

unsigned long vgxm_target_create_cell(unsigned long key, unsigned long index, unsigned long width, unsigned long height,
	struct vgxm_texture *texture)
{
	static int enabled = -1;
	unsigned int atlas;
	unsigned long id;

	if (enabled < 0)
		enabled = !getenv("HALO_TARGET_ATLAS") || atoi(getenv("HALO_TARGET_ATLAS")) != 0;
	if (!enabled || !width || !height || width > 128 || height > 128 || index < 1 || index > 24)
		return 0;
	for (atlas = 0; atlas < 6; atlas++)
		if (null_atlases[atlas].id && null_atlases[atlas].key == key && null_atlases[atlas].width == width &&
			null_atlases[atlas].height == height)
			break;
	if (atlas == 6)
	{
		for (atlas = 0; atlas < 6 && null_atlases[atlas].id; atlas++)
			;
		if (atlas == 6 || !(id = vgxm_target_create(width * 6, height * 4, 0, NULL)))
			return 0;
		null_atlases[atlas].key = key;
		null_atlases[atlas].width = width;
		null_atlases[atlas].height = height;
		null_atlases[atlas].id = (unsigned int)id;
	}
	id = null_atlases[atlas].cells[index - 1];
	if (!id)
	{
		/* (a slot without memory: the atlas's) */
		if (null.target_count >= MAXIMUM_TARGETS)
			return 0;
		id = ++null.target_count;
		null_target_asked[id] = 0;
		null_atlases[atlas].cells[index - 1] = (unsigned int)id;
		cell_atlas[id] = null_atlases[atlas].id;
	}
	if (texture)
	{
		texture->control[0] = 0x80000000ul | id;
		texture->control[1] = 0;
		texture->control[2] = width | height << 16;
		texture->control[3] = 0;
	}
	return id;
}

int vgxm_target_remake(unsigned long id, unsigned long width, unsigned long height, int depth,
	struct vgxm_texture *texture)
{
	if (!id || id > null.target_count || id > MAXIMUM_TARGETS || !width || !height || cell_atlas[id])
		return 0;
	null_target_release(id);
	null_target_asked[id] = height == 480 && width >= 640 ? width | height << 16 : 0;
	if (!null_target_make(id, width, height, depth))
	{
		platform_log("gxm: cannot remake target %lu as %lux%lu %s", id, width, height, depth ? "depth" : "colour");
		return 0;
	}
	if (texture)
	{
		texture->control[0] = 0x80000000ul | id;
		texture->control[1] = depth ? 0x100 : 0;
		texture->control[2] = null_rect_words(width, height);
		texture->control[3] = 0;
	}
	return 1;
}

/* a mip chain as on the Vita: one block (or share) for every level, held
by the first level's slot */
int vgxm_target_create_chain(unsigned long width, unsigned long height, unsigned long levels,
	unsigned long *ids, struct vgxm_texture *texture)
{
	struct block chain;
	unsigned long level, size = 0;

	if (!levels || null.target_count + levels > MAXIMUM_TARGETS)
		return -1;
	for (level = 0; level < levels; level++)
		size += 4 * ALIGN(width >> level ? width >> level : 1, 8) * (height >> level ? height >> level : 1);
	if (!target_memory_get(&chain, (unsigned int)size, 0, 0, 0, 0, "colour target chain"))
		return -1;
	for (level = 0; level < levels; level++)
	{
		unsigned long id = ++null.target_count;

		memset(&null_targets[id], 0, sizeof(null_targets[id]));
		null_targets[id].width = (unsigned int)(width >> level ? width >> level : 1);
		null_targets[id].height = (unsigned int)(height >> level ? height >> level : 1);
		null_targets[id].memory = chain;
		if (level)
			null_targets[id].memory.uid = 0;
		null_target_asked[id] = 0;
		ids[level] = id;
	}
	if (texture)
	{
		texture->control[0] = 0x80000000ul | ids[0];
		texture->control[1] = levels << 8;
		texture->control[2] = width | height << 16;
		texture->control[3] = 0;
	}
	return 0;
}

/* The scenes the Vita's renderer would begin for the draws and clears
(vita_gxm.c scene_ensure, with HALO_GXM_RTT_SYNC and HALO_GXM_SCENE_DRAWS
at their defaults): counted, with the scenes that wait for the scenes
before them, and logged every 300 presents as the Vita's "gxm:" line has
them, so a change to the order of the records is measured without the
hardware ("gxm-null scenes"). */
static struct
{
	int in_scene, sampled_cell_conflict;
	unsigned long scene_cell;
	unsigned long scene_color, scene_depth, presented_target;
	unsigned int scene_serial, wait_serial, sampled_serial, scene_draws;
	int texture_scene_since_wait;
	unsigned int written_serial[MAXIMUM_TARGETS + 1];
	unsigned long scenes, waits, splits, frames;
} scenes;

static unsigned long scene_target(unsigned long color)
{
	return color && color <= MAXIMUM_TARGETS && cell_atlas[color] ? cell_atlas[color] : color;
}

static int scene_dependency_needed(unsigned int open_serial)
{
	if (scenes.sampled_cell_conflict)
		return 1;
	if (scenes.sampled_serial && scenes.sampled_serial >= scenes.wait_serial && scenes.sampled_serial != open_serial)
		return 1;
	return !open_serial && scenes.texture_scene_since_wait && null.color_target &&
		null.color_target == scenes.presented_target;
}

static void scene_ensure(void)
{
	unsigned long wanted = scene_target(null.color_target);

	if (scenes.in_scene && scenes.scene_color == wanted && scenes.scene_depth == null.depth_target)
	{
		if (scene_dependency_needed(scenes.scene_serial))
			scenes.splits++;
		else if (scenes.scene_draws < 300)
		{
			scenes.scene_cell = wanted != null.color_target ? null.color_target : 0;
			if (scenes.scene_cell)
				scenes.written_serial[scenes.scene_cell] = scenes.scene_serial;
			goto done;
		}
	}
	scenes.in_scene = 0;
	if (!null.color_target && !null.depth_target)
		goto done;
	scenes.scenes++;
	scenes.scene_serial++;
	if (scene_dependency_needed(0))
	{
		scenes.wait_serial = scenes.scene_serial;
		scenes.texture_scene_since_wait = 0;
		scenes.waits++;
	}
	if (null.color_target != scenes.presented_target)
		scenes.texture_scene_since_wait = 1;
	if (null.color_target <= MAXIMUM_TARGETS)
		scenes.written_serial[null.color_target] = scenes.scene_serial;
	if (wanted <= MAXIMUM_TARGETS)
		scenes.written_serial[wanted] = scenes.scene_serial;
	if (null.depth_target <= MAXIMUM_TARGETS)
		scenes.written_serial[null.depth_target] = scenes.scene_serial;
	scenes.in_scene = 1;
	scenes.scene_draws = 0;
	scenes.scene_cell = wanted != null.color_target ? null.color_target : 0;
	scenes.scene_color = wanted;
	scenes.scene_depth = null.depth_target;
done:
	scenes.scene_draws++;
	scenes.sampled_serial = 0;
	scenes.sampled_cell_conflict = 0;
}

static void scenes_present(unsigned long color_target)
{
	scenes.in_scene = 0;
	scenes.presented_target = color_target;
	if (++scenes.frames == 300)
	{
		static int enabled = -1;

		if (enabled < 0)
			enabled = getenv("HALO_GPU_STATS") && atoi(getenv("HALO_GPU_STATS"));
		if (enabled)
			platform_log("gxm-null scenes: %.1f scenes/frame (%.1f splits), %.1f scene waits/frame, %.2f scenes begun again to wait/frame",
				scenes.scenes / 300.0, 0.0, scenes.waits / 300.0, scenes.splits / 300.0);
		scenes.scenes = scenes.waits = scenes.splits = scenes.frames = 0;
	}
}

/* (debug) a name for a target, for HALO_DRAW_HASH=4: the surface and the
copy it stands for, whatever order the targets were made in */
static unsigned long long target_names[MAXIMUM_TARGETS + 1];

void vgxm_debug_name_target(unsigned long id, unsigned long long name)
{
	if (id && id <= MAXIMUM_TARGETS)
		target_names[id] = name;
}

void vgxm_set_targets(unsigned long color, unsigned long depth)
{
	null.color_target = color;
	null.depth_target = depth;
}

void vgxm_note_sampled_target(unsigned long id)
{
	if (id && id <= MAXIMUM_TARGETS && scenes.written_serial[id] > scenes.sampled_serial)
		scenes.sampled_serial = scenes.written_serial[id];
	if (id && id <= MAXIMUM_TARGETS && cell_atlas[id] && scenes.in_scene && cell_atlas[id] == scenes.scene_color &&
		id != null.color_target && scenes.written_serial[id] == scenes.scene_serial)
		scenes.sampled_cell_conflict = 1;
}

/* HALO_DRAW_HASH=1: every draw and clear folded into a hash of what the GPU
would be given - the programs, the targets, the states, the bytes of each
uniform buffer, the texture words, the indices and the vertex bytes each
attribute reads - logged per frame ("draw hash"). With a repeatable run
(HALO_FIXED_DT=1 HALO_TICK_THREAD=0) two builds that draw the same frames
log the same hashes, whatever their records look like on the way. */
static int draw_hash_on = -1;
static unsigned long long draw_hash, draw_hash_draws;

/* HALO_DRAW_HASH_PARTS=1: a hash per kind of input too (programs, each
uniform chunk, the textures, the states, the vertices...), logged with the
present's, to see what two runs disagree on */
static int draw_hash_parts = -1, draw_hash_part;
static unsigned long long draw_part_hash[16];

static void hash_bytes(const void *data, unsigned long size)
{
	const unsigned char *bytes = data;
	unsigned long long hash = draw_hash, part = draw_part_hash[draw_hash_part];

	while (size--)
	{
		hash = (hash ^ *bytes) * 1099511628211ull;
		part = (part ^ *bytes++) * 1099511628211ull;
	}
	draw_hash = hash;
	draw_part_hash[draw_hash_part] = part;
}

static void hash_word(unsigned long long value)
{
	hash_bytes(&value, sizeof(value));
}

static void hash_word32(unsigned long value)
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

		draw_hash_on = setting ? atoi(setting) : 0;
		draw_hash = 1469598103934665603ull;
	}
	return draw_hash_on;
}

/* HALO_DRAW_HASH=4: each target's draws and clears hashed in their order,
the targets' hashes summed at the present (each under the target's name,
vgxm_debug_name_target): a build that runs the records of different
targets in another order - the worker's waves of small targets - hashes
the same as long as every target is drawn the same, in the same order */
#define TARGET_CHAINS (MAXIMUM_TARGETS + 1)
static unsigned long long target_chain[TARGET_CHAINS], draw_hash_saved;

static unsigned long long draw_hash_target_word(unsigned long id)
{
	if (draw_hash_on == 4 && id && id < TARGET_CHAINS && target_names[id])
		return target_names[id];
	return id;
}

static void draw_hash_target_begin(void)
{
	if (draw_hash_on != 4)
		return;
	draw_hash_saved = draw_hash;
	draw_hash = 1469598103934665603ull;
}

static void draw_hash_target_end(void)
{
	unsigned long target = null.color_target ? null.color_target : null.depth_target;

	if (draw_hash_on != 4)
		return;
	if (target < TARGET_CHAINS)
		target_chain[target] = (target_chain[target] ^ draw_hash) * 1099511628211ull + 1;
	draw_hash = draw_hash_saved;
}

static unsigned long long draw_hash_targets_sum(void)
{
	unsigned long long sum = 0;
	unsigned long target;

	for (target = 0; target < TARGET_CHAINS; target++)
	{
		if (target_chain[target])
		{
			unsigned long long name = draw_hash_target_word(target);

			sum += (target_chain[target] ^ (name * 0x9e3779b97f4a7c15ull)) * 0xff51afd7ed558ccdull;
			target_chain[target] = 0;
		}
	}
	return sum;
}

/* a texture's words, a target's id as its name (HALO_DRAW_HASH=4) */
static void hash_texture_words(const struct vgxm_texture *texture)
{
	unsigned long words[4];

	memcpy(words, texture->control, sizeof(words));
	if (draw_hash_on == 4 && (words[0] & 0x80000000ul))
	{
		unsigned long long name = draw_hash_target_word(words[0] & 0x7ffffffful);

		words[0] = (unsigned long)name ^ (unsigned long)(name >> 32) ^ 0x80000000ul;
	}
	hash_bytes(words, sizeof(words));
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

/* HALO_DRAW_HASH=2: a hash that does not see how the primitives are split
into draws - each triangle (line, point) is hashed with its draw's state
(everything but the primitive type and the indices) and its vertices' bytes,
in order - so a build that merges draws with the same state into one, the
same primitives in the same order, hashes the same */
static void hash_vertex(const struct vgxm_draw *draw, unsigned long index)
{
	unsigned long attribute;

	for (attribute = 0; attribute < draw->attribute_count; attribute++)
	{
		const struct vgxm_attribute *a = &draw->attributes[attribute];

		if (a->stream < draw->stream_count && draw->streams[a->stream])
			hash_bytes((const unsigned char *)draw->streams[a->stream] + index * draw->strides[a->stream] + a->offset,
				attribute_bytes(a));
	}
}

/* HALO_DRAW_HASH=3: as 2, but a vertex is hashed as the values of the input
registers its program reads - from its streams, decoded, or from the
uniform buffer's current values - and the program by the Xbox program's
own hash, so a draw that takes a register from its vertices instead of from
its uniforms (the same values) hashes the same */
static void hash_vertex_inputs(const struct vgxm_draw *draw, unsigned long index)
{
	unsigned long reg, attribute;

	for (reg = 0; reg < 16; reg++)
	{
		float value[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

		if (!(draw->vertex_input_mask & (1ul << reg)))
			continue;
		for (attribute = 0; attribute < draw->attribute_count; attribute++)
			if (draw->attributes[attribute].reg == reg)
				break;
		if (attribute < draw->attribute_count)
		{
			const struct vgxm_attribute *a = &draw->attributes[attribute];
			const unsigned char *bytes = a->stream < draw->stream_count && draw->streams[a->stream] ?
				(const unsigned char *)draw->streams[a->stream] + index * draw->strides[a->stream] + a->offset : NULL;
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
		}
		else if (draw->vertex_uniforms)
			memcpy(value, (const unsigned char *)draw->vertex_uniforms + (3 + reg) * 16, sizeof(value));
		hash_floats(value, sizeof(value));
	}
}

static void hash_vertex_any(const struct vgxm_draw *draw, unsigned long index)
{
	if (draw_hash_on == 3)
		hash_vertex_inputs(draw, index);
	else
		hash_vertex(draw, index);
}

static void draw_hash_primitives(const struct vgxm_draw *draw, unsigned long long state)
{
	unsigned long i, n = draw->index_count;
	const unsigned short *x = draw->indices;

	if (!x)
		return;
	switch (draw->primitive)
	{
	case 5: /* D3DPT_TRIANGLELIST */
		for (i = 0; i + 2 < n; i += 3)
		{
			draw_hash_draws++, hash_word(state ^ 3);
			hash_vertex_any(draw, x[i]); hash_vertex_any(draw, x[i + 1]); hash_vertex_any(draw, x[i + 2]);
		}
		break;
	case 6: /* D3DPT_TRIANGLESTRIP: odd triangles turned back, as a merged list holds them */
		for (i = 0; i + 2 < n; i++)
		{
			draw_hash_draws++, hash_word(state ^ 3);
			if (i & 1) { hash_vertex_any(draw, x[i + 1]); hash_vertex_any(draw, x[i]); }
			else { hash_vertex_any(draw, x[i]); hash_vertex_any(draw, x[i + 1]); }
			hash_vertex_any(draw, x[i + 2]);
		}
		break;
	case 7: /* D3DPT_TRIANGLEFAN */
		for (i = 0; i + 2 < n; i++)
		{
			draw_hash_draws++, hash_word(state ^ 3);
			hash_vertex_any(draw, x[0]); hash_vertex_any(draw, x[i + 1]); hash_vertex_any(draw, x[i + 2]);
		}
		break;
	case 2: /* D3DPT_LINELIST */
		for (i = 0; i + 1 < n; i += 2)
		{
			draw_hash_draws++, hash_word(state ^ 2);
			hash_vertex_any(draw, x[i]); hash_vertex_any(draw, x[i + 1]);
		}
		break;
	default:
		for (i = 0; i < n; i++)
		{
			draw_hash_draws++, hash_word(state ^ (1 + ((unsigned long long)draw->primitive << 8)));
			hash_vertex_any(draw, x[i]);
		}
		break;
	}
}

static void draw_hash_add(const struct vgxm_draw *draw)
{
	static const unsigned long chunk_registers[6] = { 12, 5, 11, 32, 0, 8 };
	unsigned long index, low = 0xffff, high = 0, attribute;
	unsigned long long main_hash = draw_hash;

	if (draw_hash_trace == -2)
	{
		const char *setting = getenv("HALO_DRAW_HASH_TRACE");

		draw_hash_trace = setting ? atol(setting) : -1;
	}
	if (draw_hash_on == 2 || draw_hash_on == 3)
		draw_hash = 1469598103934665603ull;
	draw_hash_target_begin();
	draw_hash_part = 0;
	hash_word(0xd7a3);
	hash_word(draw_hash_target_word(null.color_target));
	hash_word(draw_hash_target_word(null.depth_target));
	/* (the programs by their source: the ids number them in the order
	they were first used) */
	if (draw_hash_on == 3)
		hash_word(draw->vertex_program_hash);
	else
		hash_word(draw->vertex_shader && draw->vertex_shader <= null.shader_count ? null.shader_hashes[draw->vertex_shader - 1] : 0);
	hash_word(draw->fragment_shader && draw->fragment_shader <= null.shader_count ? null.shader_hashes[draw->fragment_shader - 1] : 0);
	if (draw_hash_on != 3)
	{
		hash_word(draw->attribute_count);
		for (attribute = 0; attribute < draw->attribute_count; attribute++)
		{
			const struct vgxm_attribute *a = &draw->attributes[attribute];

			hash_word(a->reg | (unsigned long)a->format << 8 | (unsigned long)a->components << 16);
		}
	}
	TRACE_PART("programs+attributes");
	draw_hash_part = 1;
	for (index = 0; index < 6; index++)
	{
		unsigned long registers = index == 4 ? draw->vertex_chunk_d_registers : chunk_registers[index];

		hash_word(draw->vertex_chunks[index] ? registers : 0xffffffff);
		if (draw->vertex_chunks[index])
			hash_floats(draw->vertex_chunks[index], registers * 16);
		TRACE_PART("chunk");
		draw_hash_part = 2 + index;
	}
	if (draw->vertex_uniforms)
	{
		/* the viewport and miscellaneous rows, and the current value of
		each input the program reads that no stream feeds */
		unsigned long provided = 0;

		for (attribute = 0; attribute < draw->attribute_count; attribute++)
			provided |= 1ul << draw->attributes[attribute].reg;
		hash_floats(draw->vertex_uniforms, 3 * 16);
		for (index = 0; index < 16 && draw_hash_on != 3; index++)
			if ((draw->vertex_input_mask & ~provided) & (1ul << index))
				hash_floats((const unsigned char *)draw->vertex_uniforms + (3 + index) * 16, 16);
	}
	TRACE_PART("vertex uniforms");
	draw_hash_part = 8;
	if (draw->fragment_uniforms[0])
		hash_floats(draw->fragment_uniforms[0], 18 * 16);
	if (draw->fragment_uniforms[1])
		hash_floats(draw->fragment_uniforms[1], 15 * 16);
	TRACE_PART("fragment uniforms");
	draw_hash_part = 9;
	for (index = 0; index < 4; index++)
	{
		if (draw->textures[index])
			hash_texture_words(draw->textures[index]);
		else
			hash_word(0);
	}
	TRACE_PART("textures");
	draw_hash_part = 10;
	/* (the states, the depth bias, the viewport and the clip, field by
	field: the draw's layout may change) */
	hash_bytes(&draw->depth_test, offsetof(struct vgxm_draw, color_write) + sizeof(draw->color_write) -
		offsetof(struct vgxm_draw, depth_test));
	hash_word32(draw->cull);
	hash_bytes(&draw->depth_bias_slope, sizeof(draw->depth_bias_slope));
	hash_bytes(&draw->depth_bias_units, sizeof(draw->depth_bias_units));
	hash_bytes(draw->viewport_offset, sizeof(draw->viewport_offset));
	hash_bytes(draw->viewport_scale, sizeof(draw->viewport_scale));
	hash_bytes(draw->clip, sizeof(draw->clip));
	if (draw_hash_on == 2 || draw_hash_on == 3)
	{
		unsigned long long state;

		hash_word(draw->visibility_index);
		state = draw_hash;
		draw_hash = main_hash;
		draw_hash_part = 11;
		/* (the present's count is then of primitives) */
		draw_hash_primitives(draw, state);
		return;
	}
	hash_word(draw->primitive);
	hash_word(draw->index_count);
	hash_word(draw->visibility_index);
	TRACE_PART("states");
	draw_hash_part = 11;
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
	draw_hash_part = 12;
	draw_hash_draws++;
	draw_hash_target_end();
}

/* a draw that writes no colour, depth or stencil and counts no samples for
a visibility test leaves the picture as it was: it is not hashed */
static int draw_writes_nothing(const struct vgxm_draw *draw)
{
	return !draw->color_write && !draw->depth_write && !draw->visibility_index &&
		!(draw->stencil_test && draw->stencil_write_mask && (draw->stencil_fail != D3DSTENCILOP_KEEP ||
			draw->stencil_depth_fail != D3DSTENCILOP_KEEP || draw->stencil_pass != D3DSTENCILOP_KEEP));
}

void vgxm_draw(const struct vgxm_draw *draw)
{
	if (!null.ready || !draw->index_count)
		return;
	null.draws++;
	null.scene_draws++;
	scene_ensure();
	if (draw_hash_enabled() && !draw_writes_nothing(draw))
		draw_hash_add(draw);
}

void vgxm_clear(unsigned long flags, unsigned long color, float depth, unsigned long stencil, const long clip[4])
{
	null.clears++;
	scene_ensure();
	if (draw_hash_enabled())
	{
		draw_hash_target_begin();
		hash_word(0xc1ea);
		hash_word(draw_hash_target_word(null.color_target));
		hash_word(draw_hash_target_word(null.depth_target));
		hash_word(flags);
		hash_word(color);
		hash_bytes(&depth, sizeof(depth));
		hash_word(stencil);
		hash_bytes(clip, 4 * sizeof(long));
		draw_hash_target_end();
	}
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

void vgxm_wait_gpu_idle(void)
{
	/* (no GPU: the draws were done when they were made) */
}

void vgxm_present(unsigned long color_target, unsigned long width, unsigned long height)
{
	(void)color_target;
	(void)width;
	(void)height;
	scenes_present(color_target);
	if (draw_hash_enabled())
	{
		if (draw_hash_on == 4)
			draw_hash ^= draw_hash_targets_sum();
		platform_log("draw hash: present %lu draws %llu hash %016llx", null.presents, draw_hash_draws, draw_hash);
		if (draw_hash_parts < 0)
			draw_hash_parts = getenv("HALO_DRAW_HASH_PARTS") && atoi(getenv("HALO_DRAW_HASH_PARTS"));
		if (draw_hash_parts)
		{
			/* (part 0 the programs' own words... 1 the uniform chunk A, through 6 E, 7 vertex
			uniforms, 8 fragment uniforms, 9 textures, 10 states, 11 vertices, 12 clears) */
			char line[400];
			int n = 0, part;

			for (part = 0; part < 13; part++)
			{
				n += snprintf(line + n, sizeof(line) - n, " %04llx", draw_part_hash[part] & 0xffff);
				draw_part_hash[part] = 0;
			}
			platform_log("draw hash parts: present %lu%s", null.presents, line);
		}
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

const void *vgxm_target_pixels(unsigned long color_target, unsigned long *pitch, unsigned long *width,
	unsigned long *height)
{
	(void)color_target;
	(void)width;
	(void)height;
	*pitch = 0;
	return NULL;
}

void vgxm_upscale_filter_set(int filter)
{
	(void)filter;
}

const void *vgxm_display_pixels(unsigned long *pitch, unsigned long *width, unsigned long *height)
{
	(void)width;
	(void)height;
	*pitch = 0;
	return NULL;
}
