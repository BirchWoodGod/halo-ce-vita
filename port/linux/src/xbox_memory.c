/*
XBOX_MEMORY.C

Xbox contiguous memory (XPhysicalAlloc) and page protection for the Linux
build.

On the Xbox, physical memory at address P is visible at virtual address
0x80000000 + P. The game asks for its game state and tag cache at fixed
addresses that way (physical_memory_map.c), and Direct3D resources carry
physical addresses in their Data fields. A 32-bit Linux process on a 64-bit
kernel owns the whole 4 GB address space, so the layer reserves the same
virtual window at start-up and allocates page-granular blocks inside it:
placed requests at exactly the address asked for, the rest top-down as the
Xbox kernel does.

The experimental Halo Custom Edition map loading needs the window Custom
Edition tag data are linked to, 0x40440000, reserved the same way when
HALO_CUSTOM_EDITION is set (docs/custom_edition_caches.md).
*/

#include "platform.h"
#include "port_config.h"
#include "../game/cache_file_formats.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifndef HALO_VITA
#include <sys/mman.h>
#endif
#include <unistd.h>
#ifdef HALO_VITA
#include "vita_host.h"
#endif

#define PAGE_SIZE_BYTES 0x1000UL
#define CONTIGUOUS_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / PAGE_SIZE_BYTES)

/* per page: 0 free, otherwise the protection of the block (PAGE_*); the
first page of a block also records the block length */
static DWORD page_protection[CONTIGUOUS_PAGE_COUNT];
static unsigned long block_page_count[CONTIGUOUS_PAGE_COUNT];
static BOOL arena_reserved = FALSE;
static pthread_mutex_t arena_lock = PTHREAD_MUTEX_INITIALIZER;
static void *custom_edition_tag_cache = NULL;
static unsigned long window_floor_page(void);

#ifdef HALO_VITA
/* no page protection on the Vita: protections are only recorded */
unsigned long platform_contiguous_base;

/* Where the blocks are laid out from, top down. The game state lives in
the window and keeps absolute pointers (data arrays to their datums,
object headers to objects, memory pool links, and the tag data it points
into), which a campaign save keeps too: a save resumes only with every
block where it was. The window itself lands where the system puts it,
which moves by a megabyte whenever the program's data segment crosses a
megabyte (v1.0 and v1.0.1: window at 0x86400000; a build whose data
segment reached 10 MB: 0x86500000, and v1.0.1 saves did not resume). The
blocks are therefore laid out from v1.0's window top, 0x8D400000, down -
the game state at 0x8C0E4000 as in v1.0 and v1.0.1 - whenever the window
reaches that high; the space above stays unused. */
#define VITA_LAYOUT_TOP 0x8D400000UL
static unsigned long layout_top_page = CONTIGUOUS_PAGE_COUNT;

static void contiguous_arena_reserve(void)
{
	unsigned long size = 0;
	void *arena = vita_host_arena(&size);

	if (arena && size >= PLATFORM_CONTIGUOUS_SIZE)
	{
		unsigned long base = (unsigned long)arena;

		platform_contiguous_base = base;
		/* the first page stays unused: physical address 0 means none */
		page_protection[0] = PAGE_NOACCESS;
		block_page_count[0] = 1;
		arena_reserved = TRUE;
		if (VITA_LAYOUT_TOP > base + 64UL * 1024 * 1024 && VITA_LAYOUT_TOP <= base + PLATFORM_CONTIGUOUS_SIZE)
		{
			layout_top_page = (VITA_LAYOUT_TOP - base) / PAGE_SIZE_BYTES;
			platform_log("memory window at 0x%08lx: blocks laid out from 0x%08lx down, as in v1.0 and v1.0.1",
				base, VITA_LAYOUT_TOP);
		}
		else
		{
			platform_log("memory window at 0x%08lx: WARNING: it does not reach 0x%08lx, so the blocks are not where "
				"v1.0 and v1.0.1 put them, and their campaign saves will not resume (the level starts over)",
				base, VITA_LAYOUT_TOP);
		}
	}
	else
	{
		platform_log("the host has no %lu byte memory window (it has %lu)",
			(unsigned long)PLATFORM_CONTIGUOUS_SIZE, size);
	}
}
#else
static int protection_to_host(DWORD protect)
{
	switch (protect & 0xff)
	{
	case PAGE_NOACCESS: return PROT_NONE;
	case PAGE_READONLY: return PROT_READ;
	case PAGE_EXECUTE: return PROT_EXEC;
	case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
	case PAGE_EXECUTE_READWRITE: return PROT_READ | PROT_WRITE | PROT_EXEC;
	default: return PROT_READ | PROT_WRITE;
	}
}

/* Reserve the window before anything else can map into it. */
__attribute__((constructor(101)))
static void contiguous_arena_reserve(void)
{
	void *wanted = (void *)PLATFORM_CONTIGUOUS_BASE;
	void *result = mmap(wanted, PLATFORM_CONTIGUOUS_SIZE, PROT_NONE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);

	if (result == wanted)
	{
		arena_reserved = TRUE;
	}
	else
	{
		if (result != MAP_FAILED)
			munmap(result, PLATFORM_CONTIGUOUS_SIZE);
		platform_log("cannot reserve the Xbox contiguous memory window at %p (%s)",
			wanted, strerror(errno));
	}
}
#endif

/* The Custom Edition tag cache, with room for OpenSauce's memory upgrades,
reserved and committed (lazily, pages are backed when touched) before
anything else can map into it; only when asked for, since it takes 36 MB of
address space below 2 GB. */
#ifndef HALO_VITA
__attribute__((constructor(102)))
static void custom_edition_tag_cache_reserve(void)
{
	void *wanted = (void *)CUSTOM_EDITION_TAG_CACHE_ADDRESS;
	void *result;

	/* (HALO_CUSTOM_EDITION_RELOCATE=1: none, so that the tags are loaded
	elsewhere and moved, as on the Vita - for the harness) */
	/* (the environment variable only: the settings are not read this
	early; without the window the tags are moved instead) */
	if (!getenv("HALO_CUSTOM_EDITION") || !strcmp(getenv("HALO_CUSTOM_EDITION"), "0") ||
		(getenv("HALO_CUSTOM_EDITION_RELOCATE") && atoi(getenv("HALO_CUSTOM_EDITION_RELOCATE"))))
		return;
	result = mmap(wanted, CUSTOM_EDITION_TAG_CACHE_BYTES_UPGRADED, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
	if (result == wanted)
	{
		custom_edition_tag_cache = result;
	}
	else
	{
		if (result != MAP_FAILED)
			munmap(result, CUSTOM_EDITION_TAG_CACHE_BYTES_UPGRADED);
		platform_log("cannot reserve the Custom Edition tag cache at %p (%s)",
			wanted, strerror(errno));
	}
}
#endif

int halo_custom_edition_enabled(void)
{
	/* game.custom_edition (port_config.c); its environment variable, which
	the Vita's settings panel sets ("0" off), is read first and every time,
	so the panel's change takes at the next map list */
	const char *setting = getenv("HALO_CUSTOM_EDITION");

	if (setting)
		return setting[0] && strcmp(setting, "0") != 0 && strcmp(setting, "false") != 0;
	return config_boolean("game.custom_edition");
}

void *halo_custom_edition_tag_cache(void)
{
	return custom_edition_tag_cache;
}

#ifdef HALO_VITA
static int custom_edition_block = -1;
#else
static unsigned long custom_edition_mapping_bytes;
#endif
static void *custom_edition_allocation;

void *halo_custom_edition_tag_cache_acquire(unsigned long bytes)
{
	if (custom_edition_tag_cache)
		return bytes <= CUSTOM_EDITION_TAG_CACHE_BYTES_UPGRADED ? custom_edition_tag_cache : NULL;
	halo_custom_edition_tag_cache_release();
	bytes = (bytes + 0xFFFFUL) & ~0xFFFFUL;
#ifdef HALO_VITA
	/* a memory block of its own, outside the window (whose layout the
	campaign saves depend on), for as long as the map is loaded */
	custom_edition_allocation = vita_host_block_alloc("halo_custom_edition", bytes, &custom_edition_block);
#else
	{
		void *result = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

		if (result != MAP_FAILED)
		{
			custom_edition_allocation = result;
			custom_edition_mapping_bytes = bytes;
		}
	}
#endif
	if (!custom_edition_allocation)
		platform_log("custom edition: no room for a %lu KB tag cache", bytes / 1024);
	else
		platform_log("custom edition: %lu KB tag cache at %p", bytes / 1024, custom_edition_allocation);
	return custom_edition_allocation;
}

void halo_custom_edition_tag_cache_release(void)
{
	if (!custom_edition_allocation)
		return;
#ifdef HALO_VITA
	vita_host_block_free(custom_edition_block);
	custom_edition_block = -1;
#else
	munmap(custom_edition_allocation, custom_edition_mapping_bytes);
	custom_edition_mapping_bytes = 0;
#endif
	custom_edition_allocation = NULL;
}

/* Memory blocks of their own (user memory: neither the C heap, a fixed
48 MB on the Vita that the system's libraries share, nor the window, whose
room below the game's blocks the map's Direct3D buffers need) for a Custom
Edition map's structure BSP vertices, its relocation bitmap and the working
memory of its conversion; zeroed. A few at a time. */
#define CUSTOM_EDITION_MEMORY_BLOCKS 8

static struct
{
	void *address;
	unsigned long bytes;
	int uid;
} custom_edition_memory_blocks[CUSTOM_EDITION_MEMORY_BLOCKS];

void *halo_custom_edition_memory_alloc(unsigned long bytes)
{
	void *result = NULL;
	int index;
	int uid = -1;

	bytes = (bytes + PAGE_SIZE_BYTES - 1) & ~(PAGE_SIZE_BYTES - 1);
	if (!bytes)
		bytes = PAGE_SIZE_BYTES;
	pthread_mutex_lock(&arena_lock);
	for (index = 0; index < CUSTOM_EDITION_MEMORY_BLOCKS && custom_edition_memory_blocks[index].address; index++)
		;
	if (index < CUSTOM_EDITION_MEMORY_BLOCKS)
	{
#ifdef HALO_VITA
		result = vita_host_block_alloc("halo_custom_edition_data", bytes, &uid);
		if (result)
			memset(result, 0, bytes);
#else
		result = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (result == MAP_FAILED)
			result = NULL;
#endif
		if (result)
		{
			custom_edition_memory_blocks[index].address = result;
			custom_edition_memory_blocks[index].bytes = bytes;
			custom_edition_memory_blocks[index].uid = uid;
		}
	}
	pthread_mutex_unlock(&arena_lock);
	if (!result)
		platform_log("custom edition: no room for a %lu KB memory block", bytes / 1024);
	return result;
}

void halo_custom_edition_memory_free(void *address)
{
	int index;

	if (!address)
		return;
	pthread_mutex_lock(&arena_lock);
	for (index = 0; index < CUSTOM_EDITION_MEMORY_BLOCKS; index++)
	{
		if (custom_edition_memory_blocks[index].address == address)
		{
#ifdef HALO_VITA
			vita_host_block_free(custom_edition_memory_blocks[index].uid);
#else
			munmap(address, custom_edition_memory_blocks[index].bytes);
#endif
			custom_edition_memory_blocks[index].address = NULL;
			break;
		}
	}
	pthread_mutex_unlock(&arena_lock);
}

void platform_contiguous_usage(unsigned long *used, unsigned long *free_bytes)
{
#ifdef HALO_VITA
	unsigned long top = layout_top_page;
#else
	unsigned long top = CONTIGUOUS_PAGE_COUNT;
#endif
	unsigned long page, used_pages = 0, free_pages = 0;

	pthread_mutex_lock(&arena_lock);
	for (page = window_floor_page(); page < top; page++)
	{
		if (page_protection[page])
			used_pages++;
		else
			free_pages++;
	}
	pthread_mutex_unlock(&arena_lock);
	*used = used_pages * PAGE_SIZE_BYTES;
	*free_bytes = free_pages * PAGE_SIZE_BYTES;
}

void (*platform_memory_renderer_report)(char *text, unsigned long size);
void (*platform_renderer_map_unloaded)(void);

void platform_memory_log(const char *when, const char *map_name)
{
	unsigned long heap_used, heap_capacity, window_used, window_free;
	char renderer[192];

	platform_heap_usage(&heap_used, &heap_capacity);
	platform_contiguous_usage(&window_used, &window_free);
	renderer[0] = 0;
	if (platform_memory_renderer_report)
		platform_memory_renderer_report(renderer, sizeof(renderer));
	platform_log("memory: %s %s: C heap %lu KB in use (of %lu KB), window %lu KB in use, %lu KB free%s%s", when,
		map_name && *map_name ? map_name : "-", heap_used / 1024, heap_capacity / 1024, window_used / 1024,
		window_free / 1024, renderer[0] ? "; " : "", renderer);
#ifndef HALO_VITA
	{
		void platform_heap_census_log(const char *when);

		platform_heap_census_log(when);
	}
#endif
}

BOOL platform_is_contiguous(const void *address)
{
	unsigned long value = (unsigned long)address;

	return value >= PLATFORM_CONTIGUOUS_BASE && value - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE;
}

static BOOL pages_free(unsigned long first, unsigned long count)
{
	unsigned long page;

	if (first + count > CONTIGUOUS_PAGE_COUNT)
		return FALSE;
	for (page = first; page < first + count; page++)
	{
		if (page_protection[page])
			return FALSE;
	}
	return TRUE;
}

/* The lowest page a block may be laid out at (top down) without a place
asked for: 0, but in the harness HALO_WINDOW_FLOOR_KB=n keeps them n KB up,
so that the room below the game's blocks is the Vita's - its blocks are laid
out from 106 MB up the 112 MB window (VITA_LAYOUT_TOP), the harness's from
the top of 128 MB: 22528 KB matches */
static unsigned long window_floor_page(void)
{
#ifdef HALO_VITA
	return 0;
#else
	static long floor = -1;

	if (floor < 0)
	{
		const char *setting = getenv("HALO_WINDOW_FLOOR_KB");

		floor = setting ? atol(setting) / 4 : 0;
		if (floor < 0 || (unsigned long)floor >= CONTIGUOUS_PAGE_COUNT)
			floor = 0;
	}
	return (unsigned long)floor;
#endif
}

void *platform_contiguous_alloc(unsigned long size, unsigned long alignment,
	unsigned long physical_address, DWORD protect)
{
	unsigned long count = (size + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES;
	unsigned long alignment_pages = alignment > PAGE_SIZE_BYTES ? alignment / PAGE_SIZE_BYTES : 1;
	unsigned long first = CONTIGUOUS_PAGE_COUNT;
	unsigned long page;
	void *address;

	if (!count)
		count = 1;
	protect &= ~(PAGE_WRITECOMBINE | PAGE_NOCACHE);
	if (!protect)
		protect = PAGE_READWRITE;

	pthread_mutex_lock(&arena_lock);
#ifdef HALO_VITA
	if (!arena_reserved)
		contiguous_arena_reserve();
#endif
	if (!arena_reserved)
	{
		pthread_mutex_unlock(&arena_lock);
		return NULL;
	}
	if (physical_address != PLATFORM_ANY_PHYSICAL_ADDRESS)
	{
		unsigned long wanted = (physical_address & ~PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;

		if (pages_free(wanted, count))
			first = wanted;
	}
	else if (count <= CONTIGUOUS_PAGE_COUNT)
	{
		/* top-down first fit, like the Xbox contiguous allocator */
#ifdef HALO_VITA
		unsigned long top = layout_top_page;
#else
		unsigned long top = CONTIGUOUS_PAGE_COUNT;
#endif
		unsigned long candidate = count <= top ? top - count : 0;
		unsigned long floor = window_floor_page();

		for (;;)
		{
			candidate -= candidate % alignment_pages;
			if (candidate < floor)
				break;
			if (pages_free(candidate, count))
			{
				first = candidate;
				break;
			}
			if (candidate == 0)
				break;
			candidate--;
		}
	}
	if (first == CONTIGUOUS_PAGE_COUNT)
	{
		pthread_mutex_unlock(&arena_lock);
		return NULL;
	}

	address = (void *)(PLATFORM_CONTIGUOUS_BASE + first * PAGE_SIZE_BYTES);
	memory_watch_forget(address, count * PAGE_SIZE_BYTES);
#ifdef HALO_VITA
	memset(address, 0, count * PAGE_SIZE_BYTES);
#else
	/* map fresh zeroed pages over the reservation */
	if (mmap(address, count * PAGE_SIZE_BYTES, protection_to_host(protect),
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != address)
	{
		pthread_mutex_unlock(&arena_lock);
		return NULL;
	}
#endif
	for (page = first; page < first + count; page++)
		page_protection[page] = protect;
	block_page_count[first] = count;
#ifdef HALO_VITA
	{
		/* the lowest page ever handed out: blocks come top-down */
		static unsigned long lowest = CONTIGUOUS_PAGE_COUNT;

		if (first < lowest && count >= 256)
		{
			lowest = first;
			platform_log("memory window: %lu KB block, %lu KB of %lu KB in use at most", count * 4,
				(CONTIGUOUS_PAGE_COUNT - lowest) * 4, CONTIGUOUS_PAGE_COUNT * 4);
		}
	}
#endif
	pthread_mutex_unlock(&arena_lock);
	return address;
}

void platform_contiguous_free(void *address)
{
	unsigned long first, count, page;

	if (!platform_is_contiguous(address))
		return;
	first = ((unsigned long)address - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;
	pthread_mutex_lock(&arena_lock);
	count = block_page_count[first];
	if (count)
	{
		memory_watch_forget(address, count * PAGE_SIZE_BYTES);
#ifndef HALO_VITA
		mmap(address, count * PAGE_SIZE_BYTES, PROT_NONE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
#endif
		for (page = first; page < first + count; page++)
			page_protection[page] = 0;
		block_page_count[first] = 0;
	}
	pthread_mutex_unlock(&arena_lock);
}

/* ---------- XAPI */

LPVOID WINAPI XPhysicalAlloc(SIZE_T size, ULONG_PTR physical_address, ULONG_PTR alignment, DWORD protect)
{
	/* A highest-acceptable address inside the window places the block
	there, which is how the game gets its fixed game state and tag cache
	addresses; anything else may go anywhere. */
	void *result = platform_contiguous_alloc(size, alignment,
		physical_address < PLATFORM_CONTIGUOUS_SIZE ? physical_address : PLATFORM_ANY_PHYSICAL_ADDRESS,
		protect);

	if (!result)
	{
		platform_log("XPhysicalAlloc: cannot allocate %lu bytes (physical address 0x%08lx)",
			(unsigned long)size, (unsigned long)physical_address);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
	}
	return result;
}

VOID WINAPI XPhysicalFree(LPVOID address)
{
	platform_contiguous_free(address);
}

BOOL WINAPI VirtualProtect(LPVOID address, SIZE_T size, DWORD new_protect, PDWORD old_protect)
{
	unsigned long start = (unsigned long)address & ~(PAGE_SIZE_BYTES - 1);
	unsigned long end = ((unsigned long)address + size + PAGE_SIZE_BYTES - 1) & ~(PAGE_SIZE_BYTES - 1);

	if (old_protect)
		*old_protect = platform_is_contiguous(address) ?
			page_protection[(start - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES] : PAGE_READWRITE;
	memory_watch_forget((void *)start, end - start);
#ifndef HALO_VITA
	if (mprotect((void *)start, end - start, protection_to_host(new_protect)) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
#endif
	if (platform_is_contiguous((void *)start))
	{
		unsigned long page;

		pthread_mutex_lock(&arena_lock);
		for (page = (start - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;
			page < (end - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES && page < CONTIGUOUS_PAGE_COUNT;
			page++)
		{
			if (page_protection[page])
				page_protection[page] = new_protect & ~(PAGE_WRITECOMBINE | PAGE_NOCACHE);
		}
		pthread_mutex_unlock(&arena_lock);
	}
	return TRUE;
}

VOID WINAPI XPhysicalProtect(LPVOID address, SIZE_T size, DWORD new_protect)
{
	VirtualProtect(address, size, new_protect, NULL);
}

DWORD WINAPI XQueryMemoryProtect(LPVOID address)
{
	DWORD protect = PAGE_READWRITE;

	if (platform_is_contiguous(address))
	{
		pthread_mutex_lock(&arena_lock);
		protect = page_protection[((unsigned long)address - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES];
		pthread_mutex_unlock(&arena_lock);
		if (!protect)
			protect = PAGE_NOACCESS;
	}
	return protect;
}
