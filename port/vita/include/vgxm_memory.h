/*
VGXM_MEMORY.H

The renderer's video memory (CDRAM): what holds it, how much is free, and
what can be given back when a render target needs it. Shared by the Vita's
renderer (port/vita/host/vita_gxm.c) and the gxm-null harness
(port/vita/null/vgxm_null.c), which each include it once, so the harness
runs the same bookkeeping, the same texture pool segments and the same
target memory as the Vita.

An application has 112 MB of CDRAM (every hardware log: "cdram 114688 KB"
before the renderer starts, 12288 KB after). It holds:
- GXM's parameter buffer (sceGxmInitialize's, HALO_GXM_PARAMETER_MB: 40 MB);
- the two display buffers (4 MB);
- the texture pool: 14 segments of 4 MB (56 MB in all), each made when the
  pool first fills into it, in CDRAM while that leaves the render targets
  their headroom (memory_target_headroom: what the screen-sized targets
  would need at 100% and 848 columns, and 2 MB for the small ones a scene
  adds later), in user RAM mapped for the GPU otherwise; a texture bigger
  than a segment gets a block of its own;
- the render targets: a block each for the big ones (the screen-sized
  ones, the shadow atlases, the camouflage's copy), shares of 256 KB blocks
  for the small ones (small_target_memory);
- the screen-sized targets' memory kept after a live change of the render
  scale, for that size to come back to (screen_block_keep, at most 16 MB).
Everything else the GPU reads is in user RAM: the game's memory window
(vertices, indices), the frame and worker rings, GXM's VDM/vertex/fragment
rings, the shader patcher's buffers, the visibility buffers.

What can be given back, safest first: the block cache (nothing uses it);
the small targets' blocks whose every share is spare; targets nothing
refers to any more (d3d8_gxm.c's sweep); screen-sized targets no frame has
used for ten seconds (made again when next drawn); the texture pool's
segments in CDRAM, top first, each replaced by user RAM (the textures in it
are decoded again as they are next used: vita_textures.c
vita_texture_cache_forget). The texture pool's user RAM segments go back
to CDRAM when it is free again (pool_promote).

HALO_CDRAM_RESERVE_MB=<n> (debug): n MB of CDRAM count as taken, so a
tighter Vita can be tried anywhere; the model of what is free
(memory_model_free: 112 MB less the parameter buffer and everything above)
then refuses what would not fit.

The free CDRAM (memory_cdram_free) is the model's, or the kernel's figure
where that follows what is freed and is smaller: the hardware's does, in
1 MB steps (vita_gxm.c memory_figure_check). On the owner's Vita the model
counted 9 MB free when a texture pool segment was refused (Extinction,
Oct 9) and 4.8 MB free when a 1.25 MB screen target was (a10, Oct 10):
CDRAM the bookkeeping does not see, or free only in pieces the kernel
cannot hand out; the texture pool then took the targets' headroom and
every live change needed the block cache flushed. HALO_CDRAM_FIGURE=model
(debug): the model's alone, as before. The harness has no kernel;
HALO_CDRAM_HIDDEN_MB=<n> (vgxm_null.c) gives it n MB of CDRAM that such
a figure counts and the model does not. HALO_CDRAM_HEADROOM_MB=<n> (debug): the
texture pool leaves the targets n MB instead of what they would need (0: it
takes CDRAM while there is any, as the single 56 MB block did).

The includer defines, before including this: struct block (uid, base,
size: uid -1 is a small target's share, 0 a chain level's part of the
first level's block), ALIGN, log_line, and
	static int memory_os_allocate(int user, unsigned int size, const char *name, struct block *block);
	static void memory_os_free(struct block *block);
	static long memory_os_free_kb(int user);  (-1: not known)
	static unsigned long memory_target_headroom(void);
memory_os_allocate maps the block for the GPU (read and write).
*/

#ifndef __HALO_VGXM_MEMORY_H
#define __HALO_VGXM_MEMORY_H

#define CDRAM_APPLICATION_BYTES (112ul * 1024 * 1024)
#define CDRAM_ALIGNMENT (256u * 1024)

enum
{
	_memory_display,
	_memory_pool,
	_memory_screen,   /* the screen-sized targets */
	_memory_target,   /* other targets with a block of their own (atlases, chains, the camouflage's copy) */
	_memory_small,    /* the small targets' shared blocks */
	_memory_cache,    /* the screen-sized targets' memory kept for later (screen_block_keep) */
	_memory_other,
	_memory_cdram_kinds,
	_memory_pool_user = _memory_cdram_kinds, /* the texture pool's segments in user RAM */
	_memory_kinds
};

static const char *const memory_kind_names[_memory_kinds] = {
	"display", "texture pool", "screen-sized targets", "other targets", "small-target blocks", "block cache",
	"other", "texture pool in user RAM",
};

static unsigned long memory_bytes[_memory_kinds];
/* the parameter buffer (GXM's own, in CDRAM), HALO_CDRAM_RESERVE_MB */
static unsigned long memory_parameter_bytes, memory_reserve_bytes;
/* the largest target allocation that found no CDRAM since the last
vgxm_cdram_wanted, and the count of refusals */
static unsigned long memory_wanted_bytes, memory_refusals;
/* HALO_CDRAM_HEADROOM_MB (-1: unset) */
static long memory_headroom_setting = -1;

static void memory_configure(unsigned long parameter_bytes)
{
	const char *setting = getenv("HALO_CDRAM_RESERVE_MB");

	memory_parameter_bytes = parameter_bytes;
	if (setting && atoi(setting) > 0 && atoi(setting) < 112)
	{
		memory_reserve_bytes = (unsigned long)atoi(setting) * 1024 * 1024;
		log_line("gxm: %d MB of CDRAM count as taken (HALO_CDRAM_RESERVE_MB)", atoi(setting));
	}
	setting = getenv("HALO_CDRAM_HEADROOM_MB");
	if (setting && *setting && atoi(setting) >= 0)
	{
		memory_headroom_setting = atoi(setting);
		log_line("gxm: the texture pool leaves the targets %ld MB of CDRAM (HALO_CDRAM_HEADROOM_MB)",
			memory_headroom_setting);
	}
}

static unsigned long memory_cdram_held(void)
{
	unsigned long held = 0;
	int kind;

	for (kind = 0; kind < _memory_cdram_kinds; kind++)
		held += memory_bytes[kind];
	return held;
}

/* 112 MB less the parameter buffer, everything held and the reserve */
static unsigned long memory_model_free(void)
{
	unsigned long taken = memory_parameter_bytes + memory_cdram_held() + memory_reserve_bytes;

	return taken < CDRAM_APPLICATION_BYTES ? CDRAM_APPLICATION_BYTES - taken : 0;
}

/* HALO_CDRAM_FIGURE=model (debug): the kernel's free CDRAM figure not
used (memory_cdram_free); 1 otherwise */
static int memory_figure_wanted(void)
{
	static int wanted = -1;

	if (wanted < 0)
	{
		const char *setting = getenv("HALO_CDRAM_FIGURE");

		wanted = !setting || strcmp(setting, "model") != 0;
	}
	return wanted;
}

/* the CDRAM free: the kernel's figure where it follows what is freed (the
hardware's, in 1 MB steps; not Vita3K's, which falls with every block
freed, nor the harness's without HALO_CDRAM_HIDDEN_MB: the model's then),
less the reserve, and no more than the model's */
static unsigned long memory_cdram_free(void)
{
	long real = memory_os_free_kb(0);
	unsigned long free;

	if (real < 0)
		return memory_model_free();
	free = (unsigned long)real * 1024;
	if (memory_reserve_bytes)
		free = free > memory_reserve_bytes ? free - memory_reserve_bytes : 0;
	if (free > memory_model_free())
		free = memory_model_free();
	return free;
}

/* a block of CDRAM; NULL when there is none (or the reserve's model has
none) */
static void *cdram_block_get(int kind, unsigned int size, const char *name, struct block *block)
{
	size = ALIGN(size, CDRAM_ALIGNMENT);
	memset(block, 0, sizeof(*block));
	if (memory_reserve_bytes && size > memory_model_free())
	{
		if (memory_refusals++ < 16)
			log_line("gxm: no CDRAM for %s (%u KB; %lu KB free with HALO_CDRAM_RESERVE_MB)", name, size / 1024,
				memory_model_free() / 1024);
		return NULL;
	}
	if (!memory_os_allocate(0, size, name, block))
	{
		memset(block, 0, sizeof(*block));
		return NULL;
	}
	memory_bytes[kind] += block->size;
	return block->base;
}

static void cdram_block_put(int kind, struct block *block)
{
	if (!block->base)
		return;
	memory_bytes[kind] -= block->size;
	memory_os_free(block);
	memset(block, 0, sizeof(*block));
}

/* ---------- the screen-sized targets' block cache

The memory of the screen-sized targets given back by a live change, kept
(up to SCREEN_BLOCK_CACHE_BYTES) for the next target of exactly that kind
and size instead of freed. Switching between settings then lands each size
where it was before: Vita3K's renderer keeps the surfaces it has seen by
address, and a surface of another size made where an old one was drew red
or nothing through the zoom's and pause menu's copies of the screen, and
once hung its renderer (triage/live-status.md; on the hardware the GPU has
no such cache). The oldest is freed first when the cache is full, and the
whole cache when CDRAM runs out (target_memory_get, pool_part_make,
vgxm_memory_trim). */
#define SCREEN_BLOCK_CACHE_BYTES (8u * 1024 * 1024)
#define SCREEN_BLOCK_CACHE_COUNT 24

static struct
{
	struct block memory;
	int depth;
	unsigned int width, height;
} screen_blocks[SCREEN_BLOCK_CACHE_COUNT];

static void screen_block_free(int index)
{
	cdram_block_put(_memory_cache, &screen_blocks[index].memory);
	memset(&screen_blocks[index], 0, sizeof(screen_blocks[index]));
}

/* frees the cache; the bytes freed */
static unsigned long screen_blocks_flush(void)
{
	unsigned long freed = 0;
	int index;

	for (index = 0; index < SCREEN_BLOCK_CACHE_COUNT; index++)
		if (screen_blocks[index].memory.base)
		{
			freed += screen_blocks[index].memory.size;
			screen_block_free(index);
		}
	return freed;
}

static void screen_block_keep(const struct block *memory, int depth, unsigned int width, unsigned int height)
{
	int index;

	/* (the oldest freed while there is no room; [0] is the newest) */
	while (screen_blocks[SCREEN_BLOCK_CACHE_COUNT - 1].memory.base ||
		(memory_bytes[_memory_cache] + memory->size > SCREEN_BLOCK_CACHE_BYTES && memory_bytes[_memory_cache]))
	{
		for (index = SCREEN_BLOCK_CACHE_COUNT - 1; index >= 0 && !screen_blocks[index].memory.base; index--)
			;
		screen_block_free(index);
	}
	memmove(&screen_blocks[1], &screen_blocks[0], (SCREEN_BLOCK_CACHE_COUNT - 1) * sizeof(screen_blocks[0]));
	screen_blocks[0].memory = *memory;
	screen_blocks[0].depth = depth;
	screen_blocks[0].width = width;
	screen_blocks[0].height = height;
	memory_bytes[_memory_screen] -= memory->size;
	memory_bytes[_memory_cache] += memory->size;
}

static int screen_block_take(struct block *memory, int depth, unsigned int width, unsigned int height)
{
	int index;

	for (index = 0; index < SCREEN_BLOCK_CACHE_COUNT; index++)
	{
		if (screen_blocks[index].memory.base && screen_blocks[index].depth == depth &&
			screen_blocks[index].width == width && screen_blocks[index].height == height)
		{
			*memory = screen_blocks[index].memory;
			memory_bytes[_memory_cache] -= memory->size;
			memory_bytes[_memory_screen] += memory->size;
			memmove(&screen_blocks[index], &screen_blocks[index + 1],
				(SCREEN_BLOCK_CACHE_COUNT - 1 - index) * sizeof(screen_blocks[0]));
			memset(&screen_blocks[SCREEN_BLOCK_CACHE_COUNT - 1], 0, sizeof(screen_blocks[0]));
			return 1;
		}
	}
	return 0;
}

/* ---------- small targets

CDRAM for a small colour target (the glow's and the shadows' 128x128s,
mip chains): CDRAM blocks come in 256 KB steps, and a block each held a
64 KB 128x128 target in 256 KB, so two dozen of them took 6 MB of the
12 MB left and the next failed (b30, "cannot allocate colour target").
Small targets are carved from shared 256 KB blocks instead; a share given
back (a target remade at another size, or one nothing refers to) is kept
for the next small target that fits in it, and a block all of whose shares
are back is freed when CDRAM is wanted (small_blocks_release). */
#define SMALL_TARGET_BLOCK (256 * 1024)
#define MAXIMUM_SMALL_BLOCKS 64
#define MAXIMUM_SMALL_TARGET_SPARES 256

static struct
{
	struct block memory;
	unsigned int carved;
} small_blocks[MAXIMUM_SMALL_BLOCKS];
static int small_block_current = -1;

static struct
{
	void *base;
	unsigned int size;
} small_target_spares[MAXIMUM_SMALL_TARGET_SPARES];

/* a small target's share of a block given back (the block stays) */
static void small_target_give_back(void *base, unsigned int size)
{
	int i;

	for (i = 0; i < MAXIMUM_SMALL_TARGET_SPARES; i++)
	{
		if (!small_target_spares[i].base)
		{
			small_target_spares[i].base = base;
			small_target_spares[i].size = size;
			return;
		}
	}
	/* (no room: the share is lost, and its block stays) */
}

static void *small_target_memory(unsigned int *share_size)
{
	unsigned int size = *share_size;
	int i, best = -1;

	size = ALIGN(size, 4096);
	if (size > SMALL_TARGET_BLOCK / 2)
		return NULL;
	for (i = 0; i < MAXIMUM_SMALL_TARGET_SPARES; i++)
	{
		if (small_target_spares[i].base && small_target_spares[i].size >= size &&
			(best < 0 || small_target_spares[i].size < small_target_spares[best].size))
		{
			best = i;
		}
	}
	if (best >= 0)
	{
		void *base = small_target_spares[best].base;

		small_target_spares[best].base = NULL;
		*share_size = small_target_spares[best].size;
		return base;
	}
	if (small_block_current < 0 || small_blocks[small_block_current].carved + size > SMALL_TARGET_BLOCK)
	{
		for (i = 0; i < MAXIMUM_SMALL_BLOCKS && small_blocks[i].memory.base; i++)
			;
		if (i == MAXIMUM_SMALL_BLOCKS || !cdram_block_get(_memory_small, SMALL_TARGET_BLOCK, "colour targets",
			&small_blocks[i].memory))
		{
			return NULL;
		}
		small_blocks[i].carved = 0;
		small_block_current = i;
	}
	small_blocks[small_block_current].carved += size;
	*share_size = size;
	return (unsigned char *)small_blocks[small_block_current].memory.base + small_blocks[small_block_current].carved - size;
}

/* frees the small targets' blocks all of whose shares are back; the bytes
freed */
static unsigned long small_blocks_release(void)
{
	unsigned long freed = 0;
	int block, spare;

	for (block = 0; block < MAXIMUM_SMALL_BLOCKS; block++)
	{
		unsigned char *base = small_blocks[block].memory.base;
		unsigned int back = 0;

		if (!base)
			continue;
		for (spare = 0; spare < MAXIMUM_SMALL_TARGET_SPARES; spare++)
			if (small_target_spares[spare].base >= (void *)base &&
				small_target_spares[spare].base < (void *)(base + SMALL_TARGET_BLOCK))
			{
				back += small_target_spares[spare].size;
			}
		if (back != small_blocks[block].carved)
			continue;
		for (spare = 0; spare < MAXIMUM_SMALL_TARGET_SPARES; spare++)
			if (small_target_spares[spare].base >= (void *)base &&
				small_target_spares[spare].base < (void *)(base + SMALL_TARGET_BLOCK))
			{
				small_target_spares[spare].base = NULL;
			}
		freed += small_blocks[block].memory.size;
		cdram_block_put(_memory_small, &small_blocks[block].memory);
		small_blocks[block].carved = 0;
		if (small_block_current == block)
			small_block_current = -1;
	}
	return freed;
}

/* ---------- a target's memory */

/* a target's memory: a screen-sized one's from the block cache when one of
its kind and size is there, a small colour one's a share of a block, else a
block of its own (the cache freed for a second try). NULL when there is no
CDRAM: the size is noted for the next relief (vgxm_cdram_wanted) */
static void *target_memory_get(struct block *memory, unsigned int bytes, int depth, int screen_kind, unsigned int width,
	unsigned int height, const char *name)
{
	int kind = screen_kind ? _memory_screen : _memory_target;

	memset(memory, 0, sizeof(*memory));
	if (screen_kind && screen_block_take(memory, depth, width, height))
		return memory->base;
	if (!depth)
	{
		unsigned int share_size = bytes;
		void *base = small_target_memory(&share_size);

		if (base)
		{
			/* (uid -1: a share, given back to the spares, not freed) */
			memory->uid = -1;
			memory->base = base;
			memory->size = share_size;
			return base;
		}
	}
	if (cdram_block_get(kind, bytes, name, memory) || (screen_blocks_flush() && cdram_block_get(kind, bytes, name, memory)))
		return memory->base;
	if (ALIGN(bytes, CDRAM_ALIGNMENT) > memory_wanted_bytes)
		memory_wanted_bytes = ALIGN(bytes, CDRAM_ALIGNMENT);
	return NULL;
}

/* gives a target's memory back: a share to the spares, a screen-sized
target's block to the cache, any other block freed; a chain level's part
of its first level's block (uid 0) stays with the first level */
static void target_memory_give_back(struct block *memory, int depth, int screen_kind, unsigned int width,
	unsigned int height)
{
	if (!memory->base)
		return;
	if (memory->uid == -1)
		small_target_give_back(memory->base, memory->size);
	else if (memory->uid == 0)
		;
	else if (screen_kind)
		screen_block_keep(memory, depth, width, height);
	else
		cdram_block_put(_memory_target, memory);
	memset(memory, 0, sizeof(*memory));
}

/* what a screen-sized target would hold at 100% and at least 848 columns
(the headroom: memory_target_headroom) */
static unsigned long memory_screen_ceiling(int depth, unsigned int width)
{
	if (width < 848)
		width = 848;
	return ALIGN(ALIGN(width, 32) * (depth ? ALIGN(480, 32) : 480) * 4, CDRAM_ALIGNMENT);
}

/* the headroom the texture pool leaves the targets: the screen-sized
targets' bytes at their ceiling (the most they have needed at once, so one
given back for now - a copy not drawn for a while - still counts) less what
they hold, and SMALL_TARGET_HEADROOM for the targets a scene adds later
(the shadows' atlases are 1.5 MB each) */
#define SMALL_TARGET_HEADROOM (2u * 1024 * 1024)

static unsigned long memory_screen_ceiling_peak;

static unsigned long memory_headroom_from(unsigned long screen_ceiling)
{
	if (screen_ceiling > memory_screen_ceiling_peak)
		memory_screen_ceiling_peak = screen_ceiling;
	if (memory_headroom_setting >= 0)
		return (unsigned long)memory_headroom_setting * 1024 * 1024;
	return (memory_screen_ceiling_peak > memory_bytes[_memory_screen] ?
		memory_screen_ceiling_peak - memory_bytes[_memory_screen] : 0) + SMALL_TARGET_HEADROOM;
}

/* ---------- the texture pool

Decoded textures, bump-allocated until the pool is full, then filled
again a segment at a time - the segment whose textures were drawn longest
ago (pool_recycle) - or all forgotten at once when that cannot be
(vita_textures.c decodes each again as it is used). The
pool's offsets run over POOL_SEGMENTS segments of POOL_SEGMENT_BYTES, each
a block of its own, made when the pool first reaches it (a level's
textures took 22 MB at b30 and 39 MB at a10 in their first 150 s; the pool
filled only after 6 minutes of the beach): in CDRAM while that leaves the
targets their headroom, else in user RAM (the GPU samples it more slowly;
POOL_USER_MAXIMUM at most, and USER_FREE_MARGIN_KB of user RAM left), else
the pool counts as full. An allocation never straddles two segments; one
bigger than a segment (a Custom Edition map's 2048x2048) has a block of its
own (pool_jumbo). The first segment (the sequential indices at its start,
d3d8_gxm.c) is CDRAM and stays. */
#define POOL_SEGMENT_BYTES (4u * 1024 * 1024)
#define POOL_SEGMENTS VGXM_POOL_SEGMENTS
#define POOL_BYTES (POOL_SEGMENTS * POOL_SEGMENT_BYTES)
#define POOL_JUMBO_BLOCKS 8
#define POOL_USER_MAXIMUM (32u * 1024 * 1024)
#define USER_FREE_MARGIN_KB (16 * 1024)

struct pool_part
{
	struct block memory;
	int user;
};

static struct pool_part pool_segments[POOL_SEGMENTS], pool_jumbo[POOL_JUMBO_BLOCKS];
/* the next free offset, the offset a reset goes back to (what the
sequential indices take), the large textures' bytes; the highest offset
allocated since the pool was last emptied (below it every segment made
holds textures, wherever the next free offset is) */
static unsigned int pool_offset, pool_floor, pool_jumbo_bytes, pool_high;
/* the segments moved by pool_demote / pool_promote; the pool's releases */
static unsigned long pool_moves[2];
static unsigned long pool_releases;

/* The recycling (pool_recycle). Once the pool has been filled to its end
(or to a segment that could not be made) it is filled again a segment at a
time: one segment's textures are forgotten, and the allocations go there
until it is full, the rest staying. Emptying the whole pool at once made
the Vita decode every texture in view again in one frame (1-2 s of Custom
Edition maps, 16-18 MB). The segment taken is the one whose textures were
drawn longest ago (vita_textures.c marks each segment's last frame drawn
from), the oldest filled first among equals. Taking them in turn round the
pool as a ring, whatever they held, made a large map's full pool forget
textures in view as soon as their turn came: each was decoded again into
the segment being filled, and filled it, and the next segment in turn -
in view too - went next, so a pool that held all the view (44 MB of the
48 MB a Vita made in a 16-player Extinction game) went round decoding 4 MB
a frame and more, at 3-10 frames a second, for as long as the game lasted
(the owner's, Oct 9 2026; the harness the same with the Vita's free CDRAM
and user RAM: HALO_CDRAM_RESERVE_MB, HALO_USER_FREE_MB). Once filled again,
an allocation must end below pool_ring_clear, the end of the segment being
filled (pool_ring_current). */
static int pool_ring_lap;
static unsigned int pool_ring_clear, pool_ring_current;
static unsigned long pool_ring_recycled;
/* (the census) the recycled segments drawn from in the frame before */
static unsigned long pool_ring_recycled_recent;

static void pool_part_free(struct pool_part *part)
{
	if (part->user)
	{
		memory_bytes[_memory_pool_user] -= part->memory.size;
		memory_os_free(&part->memory);
	}
	else
		cdram_block_put(_memory_pool, &part->memory);
	memset(part, 0, sizeof(*part));
}

static int pool_part_user(struct pool_part *part, unsigned int size, const char *name)
{
	long user_free = memory_os_free_kb(1);

	if (memory_bytes[_memory_pool_user] + size > POOL_USER_MAXIMUM ||
		(user_free >= 0 && (unsigned long)user_free < size / 1024 + USER_FREE_MARGIN_KB) ||
		!memory_os_allocate(1, size, name, &part->memory))
	{
		memset(&part->memory, 0, sizeof(part->memory));
		return 0;
	}
	memory_bytes[_memory_pool_user] += part->memory.size;
	part->user = 1;
	return 1;
}

/* a block for a part of the pool: CDRAM while the targets keep their
headroom (whatever is free, for the first segment), user RAM otherwise */
static int pool_part_make(struct pool_part *part, unsigned int size, const char *name, int first)
{
	unsigned long need = ALIGN(size, CDRAM_ALIGNMENT);

	memset(part, 0, sizeof(*part));
	if (first || memory_cdram_free() + memory_bytes[_memory_cache] >= need + memory_target_headroom())
	{
		if (cdram_block_get(_memory_pool, size, name, &part->memory) ||
			(screen_blocks_flush() && cdram_block_get(_memory_pool, size, name, &part->memory)))
		{
			return 1;
		}
	}
	return !first && pool_part_user(part, size, name);
}

static int pool_initialize(void)
{
	return pool_part_make(&pool_segments[0], POOL_SEGMENT_BYTES, "texture pool", 1);
}

static void *pool_allocate(unsigned long size, unsigned long alignment)
{
	unsigned int offset = ALIGN(pool_offset, (unsigned int)alignment), index;

	if (size > POOL_SEGMENT_BYTES)
	{
		for (index = 0; index < POOL_JUMBO_BLOCKS && pool_jumbo[index].memory.base; index++)
			;
		if (index == POOL_JUMBO_BLOCKS || pool_offset + pool_jumbo_bytes + size > POOL_BYTES ||
			!pool_part_make(&pool_jumbo[index], (unsigned int)size, "texture pool (large)", 0))
		{
			return NULL;
		}
		pool_jumbo_bytes += pool_jumbo[index].memory.size;
		return pool_jumbo[index].memory.base;
	}
	if (offset % POOL_SEGMENT_BYTES + size > POOL_SEGMENT_BYTES)
		offset = ALIGN(offset, POOL_SEGMENT_BYTES);
	index = offset / POOL_SEGMENT_BYTES;
	if (index >= POOL_SEGMENTS || offset + size + pool_jumbo_bytes > POOL_BYTES)
		return NULL;
	/* (a later lap: the next segment still holds the last lap's textures) */
	if (pool_ring_lap && offset + size > pool_ring_clear)
		return NULL;
	if (!pool_segments[index].memory.base &&
		(pool_ring_lap || !pool_part_make(&pool_segments[index], POOL_SEGMENT_BYTES, "texture pool", 0)))
	{
		/* (once filled again, no segment is made: one that found no room
		the first time is not asked for at every allocation that reaches
		it - the owner's Vita asked the kernel again twice a second) */
		return NULL;
	}
	pool_offset = offset + (unsigned int)size;
	if (pool_offset > pool_high)
		pool_high = pool_offset;
	pool_ring_current = index;
	return (unsigned char *)pool_segments[index].memory.base + offset % POOL_SEGMENT_BYTES;
}

/* (the GPU idle) every allocation forgotten; the large textures' blocks
and the segments in user RAM freed, to be made again as the pool fills, in
CDRAM if it has room by then */
static void pool_forget(void)
{
	int index;

	for (index = 0; index < POOL_JUMBO_BLOCKS; index++)
		if (pool_jumbo[index].memory.base)
			pool_part_free(&pool_jumbo[index]);
	pool_jumbo_bytes = 0;
	for (index = 1; index < POOL_SEGMENTS; index++)
		if (pool_segments[index].user)
			pool_part_free(&pool_segments[index]);
	pool_offset = pool_high = pool_floor;
	pool_ring_lap = 0;
	pool_ring_clear = 0;
	pool_ring_current = 0;
}

/* (the GPU idle; a map gone) every allocation forgotten and every part
but the first segment freed - CDRAM ones too, which pool_forget keeps -
to be made again as the next map fills the pool: a pool grown to a large
map's 56 MB otherwise held all of CDRAM but the targets' headroom for the
rest of the session (the owner's Vita, beta.2, Oct 8: 14 segments held,
then a live change's depth target and, after joining a game on carousel, a
texture pool segment found no CDRAM, and every texture failed). The
CDRAM bytes given back */
/* the bytes the pool's parts hold (CDRAM and user RAM) */
static unsigned long pool_held(void)
{
	unsigned long held = pool_jumbo_bytes;
	int index;

	for (index = 0; index < POOL_SEGMENTS; index++)
		held += pool_segments[index].memory.base ? pool_segments[index].memory.size : 0;
	return held;
}

static unsigned long pool_release(void)
{
	unsigned long freed = 0;
	int index;

	pool_forget();
	for (index = 1; index < POOL_SEGMENTS; index++)
		if (pool_segments[index].memory.base)
		{
			freed += pool_segments[index].user ? 0 : pool_segments[index].memory.size;
			pool_part_free(&pool_segments[index]);
		}
	pool_releases++;
	return freed;
}

/* whether a segment can be filled again: made, and below the share the
large textures leave */
static int pool_segment_recyclable(unsigned int index)
{
	return pool_segments[index].memory.base && index * POOL_SEGMENT_BYTES + POOL_SEGMENT_BYTES + pool_jumbo_bytes <=
		POOL_BYTES;
}

/* (the GPU idle) the pool full ahead of the next allocation: a segment made
free for the allocations to go on in - of those made, the one whose
textures were drawn from longest ago by segment_frames[] (each segment's
last frame; NULL: the next after the one being filled, round the pool),
the first after the one being filled among equals, never that one while
another can be - with its memory in *base and *size for the textures
decoded there to be forgotten (size 0: no memory there); 0 when no segment
can be filled again */
static int pool_recycle(void **base, unsigned long *size, const unsigned long *segment_frames, unsigned long frame)
{
	unsigned int step, index, chosen = POOL_SEGMENTS;

	*base = NULL;
	*size = 0;
	for (step = 1; step <= POOL_SEGMENTS; step++)
	{
		index = (pool_ring_current + step) % POOL_SEGMENTS;
		if (!pool_segment_recyclable(index) || (index == pool_ring_current && chosen < POOL_SEGMENTS))
			continue;
		if (chosen == POOL_SEGMENTS ||
			(segment_frames && (long)(segment_frames[index] - segment_frames[chosen]) < 0))
		{
			chosen = index;
		}
	}
	if (chosen == POOL_SEGMENTS)
		return 0;
	*base = pool_segments[chosen].memory.base;
	*size = POOL_SEGMENT_BYTES;
	pool_ring_lap = 1;
	pool_ring_current = chosen;
	pool_ring_clear = (chosen + 1) * POOL_SEGMENT_BYTES;
	/* (the first segment's start: the sequential indices) */
	pool_offset = chosen ? chosen * POOL_SEGMENT_BYTES : pool_floor;
	pool_ring_recycled++;
	if (segment_frames && frame - segment_frames[chosen] <= 1)
		pool_ring_recycled_recent++;
	return 1;
}

/* (the GPU idle) moves the pool's highest part in CDRAM (not the first
segment) to user RAM, or frees it if nothing is decoded there; the CDRAM
bytes freed (0: none could be moved). *base and *size: the old memory,
whose textures are to be forgotten (vita_texture_cache_forget; size 0 when
it held none) */
static unsigned long pool_demote(void **base, unsigned long *size)
{
	struct pool_part *part = NULL, moved;
	unsigned long freed;
	int index, used = 1, jumbo = 0;

	*base = NULL;
	*size = 0;
	for (index = POOL_SEGMENTS - 1; index >= 1 && !part; index--)
		if (pool_segments[index].memory.base && !pool_segments[index].user)
		{
			part = &pool_segments[index];
			used = pool_high > (unsigned int)index * POOL_SEGMENT_BYTES;
		}
	for (index = 0; index < POOL_JUMBO_BLOCKS && !part; index++)
		if (pool_jumbo[index].memory.base && !pool_jumbo[index].user)
		{
			part = &pool_jumbo[index];
			jumbo = 1;
		}
	if (!part)
		return 0;
	*base = part->memory.base;
	*size = used ? part->memory.size : 0;
	freed = part->memory.size;
	if (!used)
	{
		/* (made again when the pool reaches it) */
		pool_part_free(part);
		pool_moves[0]++;
		return freed;
	}
	memset(&moved, 0, sizeof(moved));
	if (!pool_part_user(&moved, part->memory.size, jumbo ? "texture pool (large)" : "texture pool"))
	{
		/* (no user RAM: the segment goes, and its offsets with it until the
		pool is next emptied; a large texture's block goes) */
		if (jumbo)
			pool_jumbo_bytes -= part->memory.size;
		pool_part_free(part);
		pool_moves[0]++;
		return freed;
	}
	pool_part_free(part);
	*part = moved;
	pool_moves[0]++;
	return freed;
}

/* (the GPU idle) moves the pool's lowest segment in user RAM back to CDRAM
when that still leaves the targets their headroom; 1 if one moved (*base
and *size as pool_demote's) */
static int pool_promote(void **base, unsigned long *size)
{
	struct pool_part moved;
	int index;

	*base = NULL;
	*size = 0;
	for (index = 1; index < POOL_SEGMENTS && !pool_segments[index].user; index++)
		;
	if (index == POOL_SEGMENTS)
		return 0;
	if (memory_cdram_free() + memory_bytes[_memory_cache] < POOL_SEGMENT_BYTES + memory_target_headroom())
		return 0;
	if (!cdram_block_get(_memory_pool, POOL_SEGMENT_BYTES, "texture pool", &moved.memory) &&
		!(screen_blocks_flush() && cdram_block_get(_memory_pool, POOL_SEGMENT_BYTES, "texture pool", &moved.memory)))
	{
		return 0;
	}
	moved.user = 0;
	*base = pool_segments[index].memory.base;
	*size = pool_high > (unsigned int)index * POOL_SEGMENT_BYTES ? pool_segments[index].memory.size : 0;
	pool_part_free(&pool_segments[index]);
	pool_segments[index] = moved;
	pool_moves[1]++;
	return 1;
}

/* the segment a pool address is in; -1 outside the segments (a large
texture's block, or none) */
static int pool_segment_of(const void *data)
{
	int index;

	for (index = 0; index < POOL_SEGMENTS; index++)
		if (pool_segments[index].memory.base && (const unsigned char *)data >= (unsigned char *)pool_segments[index].memory.base &&
			(const unsigned char *)data < (unsigned char *)pool_segments[index].memory.base + POOL_SEGMENT_BYTES)
		{
			return index;
		}
	return -1;
}

/* a pool address's offset (the harness's draw hash: the same in every
run); -1 outside the pool */
static long __attribute__((unused)) pool_offset_of(const void *data)
{
	int index;

	for (index = 0; index < POOL_SEGMENTS; index++)
		if (pool_segments[index].memory.base && (const unsigned char *)data >= (unsigned char *)pool_segments[index].memory.base &&
			(const unsigned char *)data < (unsigned char *)pool_segments[index].memory.base + POOL_SEGMENT_BYTES)
		{
			return (long)(index * POOL_SEGMENT_BYTES + ((const unsigned char *)data - (unsigned char *)pool_segments[index].memory.base));
		}
	return -1;
}

/* ---------- the census */

/* the census's lines: 0 what holds CDRAM, 1 the texture pool and the free */
static int memory_census_line(char *text, unsigned long size, int part)
{
	unsigned int index, cdram_segments = 0, user_segments = 0;
	int length;

	if (part == 0)
	{
		long real = memory_os_free_kb(0);

		length = snprintf(text, size, "parameter buffer %lu KB, display %lu KB, texture pool %lu KB, screen-sized targets "
			"%lu KB, other targets %lu KB, small-target blocks %lu KB, block cache %lu KB, other %lu KB: %lu KB held, "
			"%lu KB free", memory_parameter_bytes / 1024, memory_bytes[_memory_display] / 1024,
			memory_bytes[_memory_pool] / 1024, memory_bytes[_memory_screen] / 1024, memory_bytes[_memory_target] / 1024,
			memory_bytes[_memory_small] / 1024, memory_bytes[_memory_cache] / 1024, memory_bytes[_memory_other] / 1024,
			(memory_parameter_bytes + memory_cdram_held()) / 1024, memory_cdram_free() / 1024);
		/* (the hardware's figure: what CDRAM holds that this does not count
		- GXM's own, the system's) */
		if (real >= 0 && length > 0 && (unsigned long)length < size)
			length += snprintf(text + length, size - length, "; %ld KB not counted here",
				(long)(CDRAM_APPLICATION_BYTES / 1024) - (long)((memory_parameter_bytes + memory_cdram_held()) / 1024) - real);
		return length;
	}
	for (index = 0; index < POOL_SEGMENTS; index++)
	{
		if (pool_segments[index].memory.base && pool_segments[index].user)
			user_segments++;
		else if (pool_segments[index].memory.base)
			cdram_segments++;
	}
	length = snprintf(text, size, "texture pool: filled to %u KB (+%u KB large), segments %u in CDRAM, %u in user RAM "
		"(%lu KB), %u not made; headroom kept for the targets %lu KB; segments moved to user RAM %lu, back to CDRAM %lu",
		pool_high / 1024, pool_jumbo_bytes / 1024, cdram_segments, user_segments,
		memory_bytes[_memory_pool_user] / 1024, POOL_SEGMENTS - cdram_segments - user_segments,
		memory_target_headroom() / 1024, pool_moves[0], pool_moves[1]);
	if (pool_releases && length > 0 && (unsigned long)length < size)
		length += snprintf(text + length, size - length, "; given back at map changes %lu times", pool_releases);
	if (pool_ring_recycled && length > 0 && (unsigned long)length < size)
		length += snprintf(text + length, size - length, "; segments filled again %lu (%lu drawn from in the frame "
			"before)", pool_ring_recycled, pool_ring_recycled_recent);
	if (memory_reserve_bytes && length > 0 && (unsigned long)length < size)
		length += snprintf(text + length, size - length, "; %lu KB reserved (HALO_CDRAM_RESERVE_MB)",
			memory_reserve_bytes / 1024);
	return length;
}

#endif
