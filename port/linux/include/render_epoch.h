/* render_epoch.h

The render epoch: the window in which a game tick (on its own thread,
tick_thread.c) runs alongside the render of the frame before it. The render
walks game state the tick relinks (cluster reference lists, object headers,
effects, decals, lights), and Halo's accessors assume nothing is torn from
under them. Rather than guarding each structure, the epoch makes the tick's
reclamation lazy:

  - a datum the tick deletes is tombstoned: its memory and identifier stay
    intact until the join, the tick's own accessors and iterators treat it as
    gone, the render's still see it (and draw it one frame late at worst);
  - a datum the tick creates is hidden from the render's iterators, whose
    scans would otherwise meet it half-initialised;
  - a memory pool block the tick frees stays allocated until the join;
  - a reference-list insert publishes its node with a release fence.

Stale links then always lead to intact nodes: an unlinked node keeps its
next pointer, no slot is reused, and relinks only ever skip forward, so a
walker mixing old and new links terminates.

The tick thread is the mutator; the game thread (the render) the reader.
The epoch ends at the join, which sweeps the tombstones and frees the
deferred blocks on the game thread, alone. */

#ifndef __HALO_RENDER_EPOCH_H
#define __HALO_RENDER_EPOCH_H

struct data_array;
struct memory_pool;

/* nonzero while a tick overlaps a render */
extern volatile int halo_epoch_active;
/* nonzero once the tick has a thread of its own (HALO_TICK_THREAD=1) */
extern int halo_epoch_threaded;

/* the tick thread, on itself, before its first tick */
void halo_epoch_register_mutator(void);
/* the tick thread, as the tick starts */
void halo_epoch_begin(void);
/* the game thread, once the tick has been joined: sweeps */
void halo_epoch_end(void);
/* the game state was replaced under the epoch's marks (a checkpoint
revert, a saved game loaded, a new map): they and the deferred frees
refer to memory that no longer holds what they marked, so they are
dropped without touching it */
void halo_epoch_discard(void);

/* is the calling thread the tick thread? */
int halo_epoch_on_mutator(void);
/* changes with every game state / map replacement (halo_epoch_discard) */
extern unsigned long halo_map_generation;
/* a small number naming the calling thread, by its stack (no system call
after the thread's first call) */
int halo_thread_index(void);

/* the tick deletes a datum during an epoch: tombstones it; 0 when the
delete must proceed as usual */
int halo_epoch_datum_delete(struct data_array *data, long absolute_index);
/* the tick created a datum during an epoch: it is under construction,
hidden from the render, until its constructor says it is ready (or the
join) */
void halo_epoch_datum_created(struct data_array *data, long absolute_index);
void halo_epoch_datum_ready(struct data_array *data, long absolute_index);
/* a constructor's scope: every datum the tick creates between begin and
ready (the object, its widgets, lights, attachments) is ready together */
unsigned long halo_epoch_scope_begin(void);
void halo_epoch_scope_ready(unsigned long scope);

enum
{
	_halo_epoch_datum_live,
	_halo_epoch_datum_tombstoned,
	_halo_epoch_datum_created
};
int halo_epoch_datum_state(const struct data_array *data, long absolute_index);
/* the accessors' test: a tombstone is hidden from the tick, a datum created
this epoch from the render; live otherwise. One byte load when the array
has no marks. */
int halo_epoch_datum_hidden_from_caller(const struct data_array *data, long absolute_index);
/* the lookups' test (datum_get): a tombstone is hidden from the tick; a
datum under construction is hidden from the render only in the arrays
with a constructor long enough to matter (objects: the render reaches
them through links the constructor publishes), any other new datum is
visible at once, a link to it being written after it is filled in */
void halo_epoch_guard_constructions(struct data_array *data);
int halo_epoch_datum_hidden_from_get(const struct data_array *data, long absolute_index);

/* datum_new / datum_delete: logs (the first times per array) a game-state
datum created or deleted by a thread other than the tick while a tick runs,
which the epoch does not protect */
void halo_epoch_check_reader_mutation(const struct data_array *data, long absolute_index, const char *what);
/* an array in the game state that only the render uses (the cached object
render states): exempt from the check */
void halo_epoch_reader_owned(const struct data_array *data);

/* the marks' table (render_epoch.c), read by data.h's inline lookups: an
array with marks has its entry's index plus one in its two padding bytes
at 38, and a byte per slot, 0 for a live datum */
#define HALO_EPOCH_ARRAY_TABLE_SIZE 256
struct halo_epoch_marked_array
{
	struct data_array *data;
	unsigned char *marks;
	unsigned long marked;
	/* the array's size when the marks were made: a new map builds its
	arrays at the same addresses with other sizes */
	long maximum_count;
};
extern struct halo_epoch_marked_array halo_epoch_marked_arrays[HALO_EPOCH_ARRAY_TABLE_SIZE];

/* the tick frees a memory pool block during an epoch: defers it; 0 when the
free must proceed as usual */
int halo_epoch_pool_free(struct memory_pool *pool, void *block);

/* lruv_cache.c: the one recursive lock over the caches the tick and the
render share (the LRUV caches, the texture cache) */
void halo_cache_lock_acquire(void);
void halo_cache_lock_release(void);
/* around a wait for the IO thread: lets the lock go if held, and takes it back */
int halo_cache_lock_suspend(void);
void halo_cache_lock_resume(int depth);

/* the game's marker passes (structure clusters, objects, lights: a global
counter and "visited" fields) are used by both the render and the tick;
each kind is held by one thread at a time when the tick has a thread (the
render's visibility walk and the tick's collision tests otherwise tripped
the "marker already begun" assertion, whose fatal path drew from the tick
thread and wedged the Vita) */
enum { _halo_marker_cluster, _halo_marker_object, _halo_marker_light, _halo_marker_count };
void halo_marker_lock(int which);
void halo_marker_unlock(int which);

/* memory_pool.c: releases a block by its header, no validation */
void memory_pool_block_release(struct memory_pool *pool, void *block);

#endif
