/* tick_hash.c

(debug) HALO_TICK_HASH=<file>: after every game tick, a 64-bit hash of the
whole game state (the arena game_state_malloc hands out, which is what a
saved game stores: objects, AI, physics, players, effects) is written to
<file> as "tick <game time> <hash>" (made of the allocations' hashes below;
an lruv cache's two function pointers are left out, since code addresses
differ between builds). Two builds that simulate identically
write identical files, so this is the oracle for changes meant to leave the
simulation exactly as it was (tick performance work).

Run it with HALO_FIXED_TICK=1 (every frame elapses exactly one tick and the
sound runs on the frame clock: halo_fixed_tick below) and the tick on the
main thread (no HALO_TICK_THREAD), so the ticks do not depend on the
machine's speed. Two more things make runs byte-identical:
	- every run starts from the same savegame.bin in the save root: the
	  game state arena starts as the last run left that file, and a few
	  fields are never initialised;
	- a build with every stack local zero-initialised (clang
	  -ftrivial-auto-var-init=zero, added by a compiler wrapper): stack
	  bytes the game copies into its state uninitialised (struct
	  location's bonus word, which nothing reads, and the like) are
	  otherwise whatever the stack held.
Then two runs of one binary agree tick for tick (b30, 3741 ticks), and a
change meant to leave the simulation alone is checked by running both
builds the same way and comparing (tools/tick_hash_compare.py).

HALO_TICK_HASH_DUMP=<game time>: also writes the whole game state at that
tick to <file>.<game time>.bin, to find which allocation (data/gamestate.txt
lists them in order) two runs first differ in.

HALO_TICK_HASH_MASK=1: leaves out what is scratch rather than simulation:
each object's magic_number (the object marker's stamp: every marker session
- the sound manager's obstruction vectors among them, whose count follows
the sound cache and mixer - writes it into the objects it visits, and only
the current session's stamp is ever compared) and the render's own
allocations in the game state (cached object render states, decal vertex
cache, decal vertices), and the objects' memory pool is hashed as its live
objects (its free and compacted-away bytes hold stale stamps). Two runs of one build then agree where they
otherwise drift apart on those alone (b30, the heavy-fight benchmark,
triage/perf2-status.md).

<file>.alloc gets each allocation's own hash after every tick (binary:
a 32-bit game time and one 64-bit hash per allocation), <file>.names their
names in the same order: tools/tick_hash_compare.py names the allocations
two runs differ in and the tick each first differs at. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* host paths, not the game's Xbox paths (port/linux/include/stdio.h) */
#undef fopen

void halo_game_state_range(void **base, unsigned long *size);
void halo_game_state_gpu_range(void **base, unsigned long *size);
int halo_game_state_allocation(int index, const char **name, void **base, unsigned long *size);
int halo_game_state_allocation_is_lruv_cache(int index);
long game_time_get(void);

/* HALO_FIXED_TICK=1: the knobs that make a run's simulation independent of
the machine's speed and of real time: main.c (one tick per frame),
random_math.c (local random seed), sound_manager.c (sound clock),
xbox_sound_cache.c (sound loads finish before they are used) and
dsound_sdl.c (the mixer advances one tick's worth of audio per frame) */
int halo_fixed_tick(void)
{
	static int fixed = -1;

	if (fixed < 0)
	{
		const char *setting = getenv("HALO_FIXED_TICK");

		fixed = setting && atoi(setting) != 0;
	}
	return fixed;
}

/* (HALO_TICK_HASH_MASK) the objects' marker stamps, cleared for the hash
and put back: objects.c */
int halo_tick_hash_object_marks(long *saved, int maximum, int restore);
unsigned long long halo_tick_hash_live_objects(void);

static int mask = -1;

static int masked_allocation(const char *name)
{
	return name && (!strcmp(name, "cached object render states") || !strcmp(name, "decal vertex cache") ||
		!strcmp(name, "decal vertices"));
}

static int state = -1;
static FILE *file, *allocations;
static int named;
static long dump_tick = -1;
static const char *path;

static unsigned long long hash_words(unsigned long long hash, const unsigned int *words, unsigned long count)
{
	unsigned long i;

	for (i = 0; i < count; i++)
	{
		hash ^= words[i];
		hash *= 0x100000001B3ULL;
	}
	return hash;
}

/* the ticks simulated since start-up (the scripted test player's clock
under HALO_FIXED_TICK: port/linux/src/xinput_sdl.c) */
volatile unsigned long halo_ticks_simulated;

#define MAXIMUM_SAVED_MARKS 4096
static long saved_marks[MAXIMUM_SAVED_MARKS];

void halo_tick_hash_after_tick(void)
{
	int saved_mark_count = 0;
	void *base, *gpu_base;
	unsigned long size, gpu_size;
	unsigned long long hash = 0xCBF29CE484222325ULL;
	long time;

	halo_ticks_simulated++;
	if (state < 0)
	{
		const char *dump = getenv("HALO_TICK_HASH_DUMP");

		path = getenv("HALO_TICK_HASH");
		file = path && *path ? fopen(path, "w") : NULL;
		state = file != NULL;
		if (file)
		{
			char name[512];

			snprintf(name, sizeof(name), "%s.alloc", path);
			allocations = fopen(name, "wb");
		}
		if (dump)
			dump_tick = atol(dump);
	}
	if (!state)
		return;
	if (mask < 0)
		mask = getenv("HALO_TICK_HASH_MASK") && atoi(getenv("HALO_TICK_HASH_MASK"));
	if (mask)
		saved_mark_count = halo_tick_hash_object_marks(saved_marks, MAXIMUM_SAVED_MARKS, 0);
	halo_game_state_range(&base, &size);
	halo_game_state_gpu_range(&gpu_base, &gpu_size);
	time = game_time_get();
	{
		const char *name;
		void *allocation;
		unsigned long allocation_size;
		unsigned int stamp = (unsigned int)time;
		int index;

		if (!named)
		{
			char names_path[512];
			FILE *names;

			snprintf(names_path, sizeof(names_path), "%s.names", path);
			names = fopen(names_path, "w");
			for (index = 0; names && halo_game_state_allocation(index, &name, &allocation, &allocation_size); index++)
				fprintf(names, "%s %lu\n", name ? name : "?", allocation_size);
			if (names)
				fclose(names);
			named = 1;
		}
		if (allocations)
			fwrite(&stamp, sizeof(stamp), 1, allocations);
		for (index = 0; halo_game_state_allocation(index, &name, &allocation, &allocation_size); index++)
		{
			const unsigned int *words = (const unsigned int *)allocation;
			unsigned long long allocation_hash;

			if (mask && masked_allocation(name))
				allocation_hash = 0;
			else if (mask && name && !strcmp(name, "objects"))
				allocation_hash = halo_tick_hash_live_objects();
			else if (halo_game_state_allocation_is_lruv_cache(index) && allocation_size >= 0x28)
			{
				/* (an lruv cache's two procs, at 0x20 and 0x24, are code
				addresses, which differ between builds) */
				static const unsigned int zeros[2];

				allocation_hash = hash_words(0xCBF29CE484222325ULL, words, 8);
				allocation_hash = hash_words(allocation_hash, zeros, 2);
				allocation_hash = hash_words(allocation_hash, words + 10, allocation_size / 4 - 10);
			}
			else
				allocation_hash = hash_words(0xCBF29CE484222325ULL, words, allocation_size / 4);

			if (allocations)
				fwrite(&allocation_hash, sizeof(allocation_hash), 1, allocations);
			hash ^= allocation_hash;
			hash *= 0x100000001B3ULL;
		}
	}
	fprintf(file, "tick %ld %016llx\n", time, hash);
	if (time == dump_tick)
	{
		char name[512];
		FILE *out;

		snprintf(name, sizeof(name), "%s.%ld.bin", path, time);
		out = fopen(name, "wb");
		if (out)
		{
			fwrite(base, 1, size, out);
			fwrite(gpu_base, 1, gpu_size, out);
			fclose(out);
		}
	}
	/* (the dump above is masked too) */
	if (mask)
		halo_tick_hash_object_marks(saved_marks, saved_mark_count, 1);
	if ((time & 63) == 0)
	{
		fflush(file);
		if (allocations)
			fflush(allocations);
	}
}
