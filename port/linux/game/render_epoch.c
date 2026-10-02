/* render_epoch.c

See render_epoch.h. The per-array marks live outside the game state (which
is saved to disk as it is): a small table of arrays keyed by address, each
with a byte per slot. An array is marked in its name's last byte, which
strncpy never writes, so the accessors' fast path is one byte load. */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cseries/cseries.h"
#include "memory/data.h"
#include "render_epoch.h"

/* (cseries.h makes free the game's debug_free, which looks for its own
header in front of the block; the mark tables come from the C library's
calloc, so they go back to the C library's free - freeing them with
debug_free at a map change corrupted the debug allocator's list, and
leaving a multiplayer game crashed in it) */
#undef free

void platform_log(const char *format, ...);
int halo_objects_pool_check(const char *when);

/* HALO_POOL_CHECK=1: the object pool's integrity walked at the tick's start
and the joins (a debugging aid; it found the in-tick compaction, and it
costs ~2.5% of the CPU) */
static int pool_check_wanted(void)
{
	static int wanted = -1;

	if (wanted < 0)
	{
		const char *setting = getenv("HALO_POOL_CHECK");
		wanted = setting && atoi(setting) != 0;
	}
	return wanted;
}

volatile int halo_epoch_active;
int halo_epoch_threaded;

/* ---------- the mutator */

static uintptr_t mutator_stack_low, mutator_stack_high;

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#elif !defined(HALO_WINDOWS) && !defined(HALO_ANDROID) /* (Windows' POSIX threads and the Android guest's musl have no pthread_getattr_np: the guesses below) */
#include <pthread.h>
/* glibc's, without _GNU_SOURCE (which upsets cseries.h) */
extern int pthread_getattr_np(pthread_t thread, pthread_attr_t *attributes);
#endif

void halo_epoch_register_mutator(void)
{
	/* the thread's stack is its identity: its bounds from the kernel (a
	guess from the stack size given at creation reached into the game
	thread's stack, and the render thread deep in a recursion passed as
	the tick) */
	char probe;
	uintptr_t here = (uintptr_t)&probe;
#ifdef __vita__
	SceKernelThreadInfo info;

	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &info) == 0 && info.stack && info.stackSize)
	{
		mutator_stack_low = (uintptr_t)info.stack;
		mutator_stack_high = (uintptr_t)info.stack + info.stackSize;
	}
#elif !defined(HALO_WINDOWS) && !defined(HALO_ANDROID)
	{
		pthread_attr_t attributes;
		void *stack = NULL;
		size_t size = 0;

		if (pthread_getattr_np(pthread_self(), &attributes) == 0)
		{
			pthread_attr_getstack(&attributes, &stack, &size);
			pthread_attr_destroy(&attributes);
		}
		if (stack && size)
		{
			mutator_stack_low = (uintptr_t)stack;
			mutator_stack_high = (uintptr_t)stack + size;
		}
	}
#endif
	if (!mutator_stack_low || here < mutator_stack_low || here >= mutator_stack_high)
	{
		/* no kernel answer: a narrow range around this frame's stack, which
		the tick's deepest calls stay within */
		mutator_stack_high = here + 4096;
		mutator_stack_low = here - (1UL << 20);
	}
	platform_log("render epoch: tick thread stack %p..%p (%lu KB)", (void *)mutator_stack_low, (void *)mutator_stack_high,
		(unsigned long)(mutator_stack_high - mutator_stack_low) / 1024);
}

int halo_epoch_on_mutator(void)
{
	char probe;
	uintptr_t here = (uintptr_t)&probe;

	return here >= mutator_stack_low && here < mutator_stack_high;
}

/* ---------- thread identity without a system call

A thread is told by its stack: the bounds come from the kernel once, when
the thread first asks, and afterwards its index is a few compares on a
stack address. (pthread_self() is a system call on the Vita, and the shared
cache lock asked it ~1800 times a frame.) */

#define THREAD_TABLE_SIZE 16
static struct { uintptr_t low, high; } thread_table[THREAD_TABLE_SIZE];
static volatile int thread_table_count;
static volatile int thread_table_lock;

static void stack_bounds(uintptr_t here, uintptr_t *low, uintptr_t *high)
{
	*low = *high = 0;
#ifdef __vita__
	{
		SceKernelThreadInfo info;

		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &info) == 0 && info.stack && info.stackSize)
		{
			*low = (uintptr_t)info.stack;
			*high = (uintptr_t)info.stack + info.stackSize;
		}
	}
#elif !defined(HALO_WINDOWS) && !defined(HALO_ANDROID)
	{
		pthread_attr_t attributes;
		void *stack = NULL;
		size_t size = 0;

		if (pthread_getattr_np(pthread_self(), &attributes) == 0)
		{
			pthread_attr_getstack(&attributes, &stack, &size);
			pthread_attr_destroy(&attributes);
		}
		if (stack && size)
		{
			*low = (uintptr_t)stack;
			*high = (uintptr_t)stack + size;
		}
	}
#endif
	if (!*low || here < *low || here >= *high)
	{
		/* no kernel answer: a range around this frame (8 MB below it) */
		*high = here + 4096;
		*low = here > (8UL << 20) ? here - (8UL << 20) : 0;
	}
}

int halo_thread_index(void)
{
	char probe;
	uintptr_t here = (uintptr_t)&probe;
	int index, count = __atomic_load_n(&thread_table_count, __ATOMIC_ACQUIRE);

	for (index = 0; index < count; index++)
		if (here >= thread_table[index].low && here < thread_table[index].high)
			return index;
	while (__atomic_exchange_n(&thread_table_lock, 1, __ATOMIC_ACQUIRE))
		;
	count = thread_table_count;
	for (index = 0; index < count; index++)
		if (here >= thread_table[index].low && here < thread_table[index].high)
			break;
	if (index == count && count < THREAD_TABLE_SIZE)
	{
		stack_bounds(here, &thread_table[count].low, &thread_table[count].high);
		__atomic_store_n(&thread_table_count, count + 1, __ATOMIC_RELEASE);
	}
	__atomic_store_n(&thread_table_lock, 0, __ATOMIC_RELEASE);
	if (index < THREAD_TABLE_SIZE)
		return index;
	/* (a seventeenth thread: its stack's megabyte stands in) */
	return THREAD_TABLE_SIZE + (int)((here >> 20) & 0x7fff);
}

/* ---------- datum marks */

#define ARRAY_TABLE_SIZE 256

struct marked_array
{
	struct data_array *data;
	unsigned char *marks;
	unsigned long marked;
	/* the array's size when the marks were made: a new map builds its
	arrays at the same addresses with other sizes */
	long maximum_count;
};

static struct marked_array arrays[ARRAY_TABLE_SIZE];
static unsigned long array_count;

#define MARK_FLAG(data) ((data)->name[sizeof((data)->name) - 1])

/* The array's table entry, one more than its slot, kept in the two padding
bytes between identifier_zero_invalid and signature (which nothing in the
game writes, data_initialize's memset aside): the lookups the game makes
by the thousand per frame (datum_get) then reach the marks without a hash
probe. A stale value (a saved game's, a discarded table's) fails the
entry's own address check and falls back to the probe. */
typedef char data_array_padding_assert[offsetof(struct data_array, signature) == 40 ? 1 : -1];
#define MARK_SLOT(data) (*(unsigned short *)((unsigned char *)(data) + 38))

static struct marked_array *array_find(const struct data_array *data, int create)
{
	unsigned long slot = ((uintptr_t)data >> 4) % ARRAY_TABLE_SIZE;
	unsigned long probes;
	unsigned long hint = MARK_SLOT(data);

	if (hint && hint <= ARRAY_TABLE_SIZE && arrays[hint - 1].data == data)
		return &arrays[hint - 1];

	for (probes = 0; probes < ARRAY_TABLE_SIZE; probes++, slot = (slot + 1) % ARRAY_TABLE_SIZE)
	{
		if (arrays[slot].data == data)
		{
			/* (a new map's array at the same address: the marks span any
			size, and the table was reset at the map change) */
			arrays[slot].maximum_count = data->maximum_count;
			MARK_SLOT(data) = (unsigned short)(slot + 1);
			return &arrays[slot];
		}
		if (!arrays[slot].data)
		{
			if (!create || array_count + 1 >= ARRAY_TABLE_SIZE)
				return NULL;
			/* (the render may look the array up while the tick adds it:
			the marks exist before the entry is visible) */
			/* a byte per slot for any size a data array can have (a short
			count), so the buffer never moves under the other thread */
			arrays[slot].marks = calloc(32768, 1);
			arrays[slot].marked = 0;
			arrays[slot].maximum_count = data->maximum_count;
			__atomic_store_n(&arrays[slot].data, (struct data_array *)data, __ATOMIC_RELEASE);
			MARK_SLOT(data) = (unsigned short)(slot + 1);
			array_count++;
			return &arrays[slot];
		}
	}
	return NULL;
}

/* the entry through the array's own slot: the fast path of every lookup */
static inline struct marked_array *array_of(const struct data_array *data)
{
	unsigned long hint = MARK_SLOT(data);

	if (hint && hint <= ARRAY_TABLE_SIZE && arrays[hint - 1].data == data)
		return &arrays[hint - 1];
	return array_find(data, 0);
}

/* every mark set this epoch, in order, so the join visits the marked
slots alone rather than scanning every array's; a full log falls back to
the scan */
#define MARK_LOG_COUNT 16384
static struct { struct marked_array *entry; long absolute_index; } mark_log[MARK_LOG_COUNT];
static unsigned long mark_log_count, mark_log_overflowed;

void halo_game_state_range(void **base, unsigned long *size);

static int in_game_state(const struct data_array *data)
{
	void *base;
	unsigned long size;

	halo_game_state_range(&base, &size);
	return (const char *)data >= (const char *)base && (const char *)data < (const char *)base + size;
}

/* the datums created this epoch, in order, for constructors' scopes */
#define CREATED_LOG_COUNT 8192
static struct { struct marked_array *entry; long absolute_index; } created_log[CREATED_LOG_COUNT];
static unsigned long created_log_count;

static int mark(struct data_array *data, long absolute_index, unsigned char value)
{
	struct marked_array *entry;

	if (!in_game_state(data))
		return 0;
	entry = array_find(data, 1);
	if (!entry || !entry->marks || absolute_index < 0 || absolute_index >= data->maximum_count)
		return 0;
	if (!entry->marks[absolute_index])
	{
		entry->marked++;
		if (mark_log_count < MARK_LOG_COUNT)
		{
			mark_log[mark_log_count].entry = entry;
			mark_log[mark_log_count].absolute_index = absolute_index;
			mark_log_count++;
		}
		else
			mark_log_overflowed = 1;
	}
	entry->marks[absolute_index] = value;
	MARK_FLAG(data) = 1;
	/* (the mark is visible before what the caller writes next: a new
	datum's identifier, datum_new) */
	__atomic_thread_fence(__ATOMIC_RELEASE);
	if (value == _halo_epoch_datum_created && created_log_count < CREATED_LOG_COUNT)
	{
		created_log[created_log_count].entry = entry;
		created_log[created_log_count].absolute_index = absolute_index;
		created_log_count++;
	}
	return 1;
}

unsigned long halo_epoch_scope_begin(void)
{
	return created_log_count;
}

void halo_epoch_scope_ready(unsigned long scope)
{
	if (!halo_epoch_active || !halo_epoch_on_mutator())
		return;
	while (created_log_count > scope)
	{
		struct marked_array *entry = created_log[--created_log_count].entry;
		long absolute_index = created_log[created_log_count].absolute_index;

		if (entry->marks[absolute_index] == _halo_epoch_datum_created)
		{
			entry->marks[absolute_index] = 0;
			entry->marked--;
		}
	}
}

int halo_epoch_datum_delete(struct data_array *data, long absolute_index)
{
	if (!halo_epoch_active || !halo_epoch_on_mutator())
		return 0;
	return mark(data, absolute_index, _halo_epoch_datum_tombstoned);
}

void halo_epoch_datum_created(struct data_array *data, long absolute_index)
{
	if (!halo_epoch_active || !halo_epoch_on_mutator())
		return;
	mark(data, absolute_index, _halo_epoch_datum_created);
}

int halo_epoch_datum_hidden_from_caller(const struct data_array *data, long absolute_index)
{
	int state;

	/* the caller has just read the datum's identifier: the marks are read
	after it (datum_new marks a new datum before writing its identifier) */
	if (halo_epoch_active)
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
	if (!MARK_FLAG(data))
		return 0;
	state = halo_epoch_datum_state(data, absolute_index);
	if (state == _halo_epoch_datum_live)
		return 0;
	return halo_epoch_on_mutator() ? state == _halo_epoch_datum_tombstoned : state == _halo_epoch_datum_created;
}

/* A datum the render thread creates or deletes in the game state while a
tick runs is outside the epoch's protocol: the delete is not deferred (the
slot is freed and the array's count shrinks under the tick's walks) and the
new slot is taken from the same free list the tick allocates from. The
render must not do it; this names the array the first times it happens
(the grenade crash on b30 was lights_preprocess_scene retiring an effect's
light from the render thread). */
static const struct data_array *reader_owned_arrays[4];
static unsigned long reader_owned_array_count;

void halo_epoch_reader_owned(const struct data_array *data)
{
	if (reader_owned_array_count < sizeof(reader_owned_arrays) / sizeof(reader_owned_arrays[0]))
		reader_owned_arrays[reader_owned_array_count++] = data;
}

void halo_epoch_check_reader_mutation(const struct data_array *data, long absolute_index, const char *what)
{
	static struct { const struct data_array *data; const char *what; } seen[32];
	static unsigned long seen_count, total;
	unsigned long index;

	if (!halo_epoch_active || halo_epoch_on_mutator() || !in_game_state(data))
		return;
	for (index = 0; index < reader_owned_array_count; index++)
		if (reader_owned_arrays[index] == data)
			return;
	total++;
	for (index = 0; index < seen_count; index++)
		if (seen[index].data == data && seen[index].what == what)
			return;
	if (seen_count < sizeof(seen) / sizeof(seen[0]))
	{
		seen[seen_count].data = data;
		seen[seen_count].what = what;
		seen_count++;
		platform_log("render epoch: %s of %s #%ld on the render thread while a tick runs (unsafe, %lu so far)",
			what, data->name, absolute_index, total);
	}
}

static const struct data_array *guarded_arrays[4];
static unsigned long guarded_array_count;

void halo_epoch_guard_constructions(struct data_array *data)
{
	if (guarded_array_count < sizeof(guarded_arrays) / sizeof(guarded_arrays[0]))
		guarded_arrays[guarded_array_count++] = data;
}

int halo_epoch_datum_hidden_from_get(const struct data_array *data, long absolute_index)
{
	int state;

	if (halo_epoch_active)
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
	if (!MARK_FLAG(data))
		return 0;
	state = halo_epoch_datum_state(data, absolute_index);
	if (state == _halo_epoch_datum_live)
		return 0;
	if (halo_epoch_on_mutator())
		return state == _halo_epoch_datum_tombstoned;
	if (state == _halo_epoch_datum_created)
	{
		unsigned long index;

		for (index = 0; index < guarded_array_count; index++)
			if (guarded_arrays[index] == data)
				return 1;
	}
	return 0;
}

void halo_epoch_datum_ready(struct data_array *data, long absolute_index)
{
	struct marked_array *entry;

	if (!MARK_FLAG(data) || !halo_epoch_active || !halo_epoch_on_mutator())
		return;
	entry = array_of(data);
	if (!entry || absolute_index < 0 || absolute_index >= data->maximum_count)
		return;
	if (entry->marks[absolute_index] == _halo_epoch_datum_created)
	{
		entry->marks[absolute_index] = 0;
		entry->marked--;
	}
}

int halo_epoch_datum_state(const struct data_array *data, long absolute_index)
{
	const struct marked_array *entry;

	if (!MARK_FLAG(data))
		return _halo_epoch_datum_live;
	entry = array_of(data);
	if (!entry || absolute_index < 0 || absolute_index >= data->maximum_count)
		return _halo_epoch_datum_live;
	return entry->marks[absolute_index];
}

/* the join, per marked slot: a tombstone becomes a free slot (datum_delete's
identifier clearing and first-free maintenance, done late) */
static void sweep_slot(struct marked_array *entry, long absolute_index)
{
	struct data_array *data = entry->data;
	unsigned char value = entry->marks[absolute_index];

	if (!value)
		return;
	entry->marks[absolute_index] = 0;
	entry->marked--;
	if (value == _halo_epoch_datum_tombstoned)
	{
		struct datum_header *header = (struct datum_header *)((unsigned char *)data->data + data->size * absolute_index);

		header->identifier = 0;
		if (absolute_index < data->first_free_absolute_index)
			data->first_free_absolute_index = (short)absolute_index;
	}
}

/* the join, per array that had marks: the count catches up with the
slots freed at its end, and the lookups' fast path is back */
static void sweep_end(struct marked_array *entry)
{
	struct data_array *data = entry->data;

	while (data->count > 0)
	{
		struct datum_header *header = (struct datum_header *)((unsigned char *)data->data + data->size * (data->count - 1));

		if (header->identifier)
			break;
		data->count--;
	}
	entry->marked = 0;
	MARK_FLAG(data) = 0;
}

/* the join's fallback when the mark log overflowed: every slot of an array */
static void sweep_all(struct marked_array *entry)
{
	long absolute_index;

	for (absolute_index = 0; absolute_index < entry->data->maximum_count && entry->marked; absolute_index++)
		sweep_slot(entry, absolute_index);
}

/* ---------- deferred pool frees */

#define DEFERRED_FREE_COUNT 4096

static struct { struct memory_pool *pool; void *block; unsigned long size; void *previous, *next; unsigned long signature; void *reference; unsigned long tick; } deferred_frees[DEFERRED_FREE_COUNT];
static unsigned long deferred_epoch_ticks;
static unsigned long deferred_free_count, deferred_free_overflows;

int halo_epoch_pool_free(struct memory_pool *pool, void *block)
{
	if (!halo_epoch_active || !halo_epoch_on_mutator())
		return 0;
	if (deferred_free_count >= DEFERRED_FREE_COUNT)
	{
		if (deferred_free_overflows++ < 4)
			platform_log("render epoch: deferred free list full, freeing now");
		return 0;
	}
	deferred_frees[deferred_free_count].pool = pool;
	deferred_frees[deferred_free_count].block = block;
	{
		/* what the block looked like when deferred, to name what changed
		if it no longer holds at the join */
		const unsigned long *words = block;

		deferred_frees[deferred_free_count].signature = words[0];
		deferred_frees[deferred_free_count].size = words[1];
		deferred_frees[deferred_free_count].previous = (void *)words[4];
		deferred_frees[deferred_free_count].next = (void *)words[3];
		deferred_frees[deferred_free_count].reference = (void *)words[2];
		deferred_frees[deferred_free_count].tick = deferred_epoch_ticks;
	}
	deferred_free_count++;
	return 1;
}

/* memory_pool.c asks before handing out memory: does [start, start+size)
overlap a block whose free is deferred? */
int halo_epoch_deferred_overlaps(const void *start, unsigned long size, const char *what)
{
	unsigned long slot;

	if (!halo_epoch_active)
		return 0;
	for (slot = 0; slot < deferred_free_count; slot++)
	{
		const unsigned char *block = deferred_frees[slot].block;
		unsigned long block_size = deferred_frees[slot].size;

		if ((const unsigned char *)start < block + block_size && (const unsigned char *)start + size > block)
		{
			platform_log("memory pool: %s at %p (%lu bytes) overlaps deferred block #%lu at %p (%lu bytes)",
				what, start, size, slot, (void *)block, block_size);
			return 1;
		}
	}
	return 0;
}

/* bumped whenever the game state (and with it the loaded map) is replaced:
what caches tag data per map checks it */
unsigned long halo_map_generation = 1;

void halo_epoch_discard(void)
{
	unsigned long slot;

	halo_map_generation++;

	/* (no tick runs while the game state is replaced: the table can go) */
	for (slot = 0; slot < ARRAY_TABLE_SIZE; slot++)
	{
		if (!arrays[slot].data)
			continue;
		free(arrays[slot].marks);
		arrays[slot].marks = NULL;
		arrays[slot].marked = 0;
		arrays[slot].data = NULL;
	}
	array_count = 0;
	deferred_free_count = 0;
	created_log_count = 0;
	mark_log_count = 0;
	mark_log_overflowed = 0;
	platform_log("render epoch: marks discarded (game state replaced)");
}

/* ---------- assertions (cseries.h) */

int halo_assert_is_fatal(void)
{
	return !halo_epoch_threaded || halo_epoch_on_mutator();
}

void halo_assert_soft(const char *information, const char *file, long line)
{
	static struct { const char *file; long line; } seen[64];
	static unsigned long seen_count;
	unsigned long index;

	for (index = 0; index < seen_count; index++)
		if (seen[index].file == file && seen[index].line == line)
			return;
	if (seen_count < sizeof(seen) / sizeof(seen[0]))
	{
		seen[seen_count].file = file;
		seen[seen_count].line = line;
		seen_count++;
	}
	platform_log("render assertion skipped (torn read?): %s in %s,#%ld", information ? information : "?", file, line);
}

/* ---------- the marker locks */

static volatile int marker_held[_halo_marker_count];
void vita_host_sleep_us(unsigned long microseconds) __attribute__((weak));

void halo_marker_lock(int which)
{
	unsigned long spins = 0;

	if (!halo_epoch_threaded)
		return;
	while (__atomic_exchange_n(&marker_held[which], 1, __ATOMIC_ACQUIRE))
	{
		if (++spins > 200)
		{
			if (vita_host_sleep_us)
				vita_host_sleep_us(20);
			if (spins > 150000)
			{
				/* ~3 s: a wedge; named, and every thread into the dump */
				platform_log("marker lock %d: waited 3 s (%s thread): deadlock, crashing for the dump",
					which, halo_epoch_on_mutator() ? "tick" : "render");
				*(volatile int *)24 = 0;
			}
		}
	}
}

void halo_marker_unlock(int which)
{
	if (!halo_epoch_threaded)
		return;
	__atomic_store_n(&marker_held[which], 0, __ATOMIC_RELEASE);
}

/* ---------- deferred compaction (memory_pool.c) */

#include "memory/memory_pool.h"

static struct memory_pool *compaction_wanted[4];
static unsigned long compaction_wanted_count, compactions_deferred;

int halo_epoch_compaction_defer(struct memory_pool *pool)
{
	unsigned long index;

	if (!halo_epoch_active || !halo_epoch_on_mutator())
		return 0;
	for (index = 0; index < compaction_wanted_count; index++)
		if (compaction_wanted[index] == pool)
			return 1;
	if (compaction_wanted_count < sizeof(compaction_wanted) / sizeof(compaction_wanted[0]))
		compaction_wanted[compaction_wanted_count++] = pool;
	compactions_deferred++;
	return 1;
}

/* ---------- the epoch */

static unsigned long epochs, sweeps_logged;

void halo_epoch_begin(void)
{
	deferred_epoch_ticks++;
	if (pool_check_wanted() && halo_objects_pool_check("at the tick's start"))
		*(volatile int *)48 = 0;
	__atomic_store_n(&halo_epoch_active, 1, __ATOMIC_RELEASE);
}

void halo_epoch_end(void)
{
	unsigned long slot, tombstones = 0, arrays_swept = 0;

	__atomic_store_n(&halo_epoch_active, 0, __ATOMIC_RELEASE);
	if (pool_check_wanted() && halo_objects_pool_check("after the tick, before the sweep"))
		*(volatile int *)32 = 0;
	/* the marked slots, from the log (a mark cleared before the join, a
	constructor's, is a no-op here) */
	for (slot = 0; slot < mark_log_count; slot++)
	{
		if (mark_log[slot].entry->marked)
			sweep_slot(mark_log[slot].entry, mark_log[slot].absolute_index);
		tombstones++;
	}
	mark_log_count = 0;
	for (slot = 0; slot < ARRAY_TABLE_SIZE; slot++)
	{
		if (!arrays[slot].data)
			continue;
		if (arrays[slot].marked)
		{
			/* (the log overflowed: marks the log does not name) */
			sweep_all(&arrays[slot]);
			arrays[slot].marked = 0;
		}
		if (MARK_FLAG(arrays[slot].data))
		{
			arrays_swept++;
			sweep_end(&arrays[slot]);
		}
	}
	if (mark_log_overflowed)
	{
		static unsigned long overflows_logged;

		mark_log_overflowed = 0;
		if (overflows_logged++ < 4)
			platform_log("render epoch: the mark log overflowed (%d marks): every array was scanned at the join", MARK_LOG_COUNT);
	}
	for (slot = 0; slot < deferred_free_count; slot++)
	{
		const unsigned long *words = deferred_frees[slot].block;

		if (words[1] != deferred_frees[slot].size || words[0] != deferred_frees[slot].signature)
		{
			/* the header itself was overwritten (links change legitimately
			when a neighbour is released first): the owner slot names the
			object, the dump shows the writer's leftovers */
			const unsigned long *owner = deferred_frees[slot].reference;

			platform_log("deferred block #%lu at %p OVERWRITTEN since its free (epoch %lu): size %lu -> %lu, signature 0x%08lx -> 0x%08lx; owner slot %p (header at %p, datum now %p); words: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx",
				slot, deferred_frees[slot].block, deferred_frees[slot].tick, deferred_frees[slot].size, words[1], deferred_frees[slot].signature, words[0],
				(void *)owner, owner ? (void *)((const unsigned char *)owner - 8) : NULL, owner ? (void *)*owner : NULL,
				words[6], words[7], words[8], words[9], words[10], words[11], words[12], words[13]);
			*(volatile int *)72 = 0;
		}
		memory_pool_block_release(deferred_frees[slot].pool, deferred_frees[slot].block);
	}
	created_log_count = 0;
	if (pool_check_wanted() && halo_objects_pool_check("after the deferred frees"))
		*(volatile int *)40 = 0;
	for (slot = 0; slot < compaction_wanted_count; slot++)
	{
		memory_pool_compact(compaction_wanted[slot]);
		if (compactions_deferred <= 8)
			platform_log("render epoch: compacted a memory pool at the join (asked for during the tick)");
	}
	compaction_wanted_count = 0;
	if (compaction_wanted_count == 0 && pool_check_wanted() && halo_objects_pool_check("after compaction"))
		*(volatile int *)80 = 0;
	if ((tombstones || deferred_free_count) && sweeps_logged < 8)
	{
		sweeps_logged++;
		platform_log("render epoch %lu: swept %lu marks in %lu arrays, freed %lu blocks",
			epochs, tombstones, arrays_swept, deferred_free_count);
	}
	deferred_free_count = 0;
	epochs++;
}
