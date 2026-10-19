/*
SCENE_PACKS.C

Scene packs in the running game (scene_packs.h, which says how they are
wanted and used; the pack itself, its builder and checks: scene_pack.c).

Where a pack is: its recipe in app0:packs/<name>.txt (halo_packs_file_read),
its file in t:\packs\<name>.pack (the save root's t folder: on the Vita
ux0:data/haloce-vita/saves/t/packs). A pack file is used only if its recipe's
hash, its builder's version and its source map's identity (the recipe's map
it was built from, still in the maps folder: its header and size) are what
they are now; otherwise, or if it is missing, it is built again.

Nothing heavy on the game thread: one worker thread (below the game's) does
every pack's checks, builds, reads and validation, one job at a time, with
the tag cache's free room as its scratch (a multiplayer map's 7 MB of tag
data would not fit the Vita's C heap beside a session's): from the top of
the room between the map's tags (and the packs loaded) and the lowest
structure bsp's place.
- A job for a pack wanted now (scene_packs_want): the pack checked, built
  if it must be (with the other packs waiting for a build that name the
  same map: one read of it for all, scene_pack_build_many), then read into
  the room's start and checked there as any pack is (scene_pack_load_file:
  the file opened once, its tags read once and checked as read, its data's
  CRC through the handle that serves its reads later), given the tag
  indices, salt and file offsets the game's table will give it: staged.
- A prebuild's job (scene_packs_prebuild): every prebuild's checks, then
  the builds of one map's read (the map most of them name), paced (a
  millisecond each 128 KB, waiting while the game's own reads are pending:
  cache_files_reads_pending), paused while a map is copied to the cache
  partition, in a network game or away from the main menu; a pack wanted
  meanwhile that it is not building stops it (built again later).
- A build reads the cache partition's decompressed copy of its map when
  there is one of it (cache_files_cached_copy_open: its header the map's,
  byte for byte, with a checksum; a copy begun into its cache file meanwhile
  is seen and the build made again from the map), in place of inflating the
  compressed map twice.

At the top of a frame (scene_packs_update, main.c) the job done is joined
and its staged pack put in the game (pack_commit): given other indices if
the table changed since (scene_pack_rebase_again), its vertex and index
buffers registered as the map's are, then the table (a copy with the pack's
tags after it) put in place, the table first, then its count
(cache_files_set_tag_instances) - no read, no check: "the game thread's part"
it logs (0.01 ms on x86). A pack that fails its check is deleted and built
once more, then given up. The bytes the packs hold after the map's tags are
reserved: a structure bsp's load does not clear them, its check does not
take them for scratch (cache_files.c, tag_validate.c), and a bsp whose place
would overlap them is refused. The game thread's time in the packs' work
is logged once it reaches 16 ms ("scene packs: game thread: ...").

Unloading (scenario_tags_unload, before the menus' tags and the map's go):
a build stopped and waited for, every pack's buffers deregistered and file
closed, the table before the packs put back, the tables made freed.

Reads (scene_packs_read, cache_files_windows.c): a file offset from
SCENE_PACK_FILE_BASE is a pack's (each loaded pack's pixels and samples have
the offsets after the one loaded before it, all within
SCENE_PACK_FILE_RANGE_BYTES, up to MAXIMUM_LOADED_SCENE_PACKS packs), served
from its file under a lock, zeros when the file cannot be read: a bitmap's
by the cache file thread, as a map's are (a texture's read on the render's
thread held its frame for the memory card's time), others at once.
scene_packs_textures_ready begins a loaded pack's bitmaps' reads and says
when they are in (a menu scene waits for them: no stand-ins).

A recipe is app0:packs/<name>.txt, or one the game made and registered
(scene_packs_recipe_register: the player models from Custom Edition maps,
player_models.c). A pack of a Custom Edition map (its recipe's cemap lines:
scene_pack_ce.c) is built and loaded as any; one whose map is not in the
maps folder, or is another version of it, fails at once with the reason
(scene_packs_recipe_available says so before it is wanted).

HALO_SCENE_PACKS=0 turns them off (nothing is wanted, built or loaded).
HALO_TEST_SLOW_IO=<KB/s>[:all] (tests, not the Vita's or a release build):
the packs' reads and writes (with :all, their source maps' too) paced to a
slow memory card's rate on the thread that makes them.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries_windows.h"
#include "cache/cache_files.h"
#include "cache/physical_memory_map.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "game/game.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "game/game_globals.h"
#include "objects/object_types.h"
#include "bitmaps/bitmap_group.h"
#include "cache/texture_cache.h"
#include "sound/sound_definitions.h"

#include "custom_edition_cache.h"
#include "halo_menus.h"
#include "scene_pack.h"
#include "scene_packs.h"
#include "tag_schema.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
void platform_heap_usage(unsigned long *in_use, unsigned long *capacity);
unsigned long long vita_host_time_us(void);
void memory_watch_prepare_write(void *address, unsigned long size);
long cache_file_tag_data_size(void);
void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances, long count);
void tag_validate_reserve_after_tags(unsigned long bytes);
boolean sound_cache_sound_in_use(struct sound_permutation *sound);
int halo_main_menu_idle(void);
FILE *cache_files_cached_copy_open(char const *map, unsigned long *token);
boolean cache_files_cached_copy_unchanged(unsigned long token);
#ifdef HALO_VITA
void vita_host_thread_priority_set(int priority);
#endif
void halo_progress_heartbeat(void);
int halo_file_identity(const char *xbox_path, unsigned long *size, unsigned long *seconds, unsigned long *nanoseconds);
boolean custom_edition_maps_turned_off(char const *name);
void sound_cache_sound_delete(struct sound_permutation *sound);

/* ---------- constants */

enum
{
	/* the loaded packs' file offsets, from SCENE_PACK_FILE_BASE: each pack's
	from where the one loaded before it ends, at this alignment, all of them
	within SCENE_PACK_FILE_RANGE_BYTES (past 0x7FFFFFFF a file offset is
	negative to the game) */
	SCENE_PACK_RANGE_ALIGNMENT = 0x10000,
	SCENE_PACK_FILE_RANGE_BYTES = 0x10000000,
	/* the tag cache room left between the packs and the structure bsp */
	SCENE_PACK_ALIGNMENT = 16,
	BUILD_THREAD_STACK_BYTES = 256 * 1024,
	TAG_INSTANCE_BYTES = 32,
};

#define SCENE_PACKS_FOLDER "t:\\packs"

/* ---------- structures */

struct scene_pack_slot
{
	short state;
	/* (a load that failed is built once more) */
	boolean rebuilt;
	boolean wanted_now;
	char name[SCENE_PACK_NAME_SIZE];
	char path[96];
	struct scene_pack_recipe *recipe;
	struct scene_pack_info info;
	struct scene_pack_build_result build;
	boolean built;
	unsigned long long ready_us;
	/* loaded: its tags in the tag cache (bytes of them), its tags' first
	index in the game's table, its file range's slot, its file */
	unsigned char *tags;
	unsigned long bytes;
	long first_index;
	long count;
	/* its pixels' and samples' file offsets, from SCENE_PACK_FILE_BASE */
	unsigned long file_base;
	FILE *file;
	char why[SCENE_PACK_WHY_SIZE];
	/* (Forge's lobby: scene_packs_prebuild) built, not loaded */
	boolean build_only;
	/* (scene_packs_release) to be let go, loaded, once it is the last
	loaded and nothing uses it; a load that waits for room (said once) */
	boolean release_wanted;
	boolean waiting_said;
	/* (the worker's) its tags read, checked (as scene_pack_load_file
	checks a pack) and given the tag indices, salt and file offsets the
	game's table would give them next (staged_...), at tags, its file open
	(file): put in the game at the top of the next frame (pack_commit),
	there and then, or let go (pack_unstage) */
	boolean staged;
	long staged_first_index;
	long staged_salt;
	unsigned long staged_file_base;
	/* (the worker's) its load found no room: not tried again until the
	packs' room or tables change (what they were then) */
	boolean full;
	unsigned long full_reserved;
	short full_tables;
	/* (the worker's) its load refused it: deleted and built once more by
	the game thread */
	boolean refused;
	/* (the worker's) its build's progress while it builds, NULL otherwise */
	long volatile const *progress;
	/* when it was wanted (to be loaded), for the time it took */
	unsigned long long wanted_us;
};

/* ---------- globals */

static struct
{
	struct scene_pack_slot packs[MAXIMUM_SCENE_PACK_SLOTS];
	short pack_count;
	/* the tag cache's bytes after the map's tags the loaded packs hold, and
	the file offsets they hold (from SCENE_PACK_FILE_BASE) */
	unsigned long reserved;
	unsigned long file_used;
	/* the table before the packs (the map's and the menus' tags), and the
	ones made since */
	void *original_instances;
	long original_count;
	void *tables[MAXIMUM_LOADED_SCENE_PACKS];
	short table_count;
	/* the worker (one job at a time): its thread, its scratch in the tag
	cache (the free room), whether it should stop */
	pthread_t thread;
	boolean thread_running;
	unsigned char *scratch;
	unsigned long scratch_size;
	int volatile cancel;
	int volatile read_lock;
	/* the salt the next pack's tags are given at least (a pack let go: the
	next one's indices are its, never its tag indices) */
	long next_salt;
	/* the worker's job: the slots it checks and builds (their indices), the
	slot it then loads (NONE: none), whether it is a prebuild's (low: paced,
	paused while the game reads or plays), the game's table and file range
	as the slot loaded will find them (its indices, salt and file offsets,
	whether the table and range have room), done; stopped to make way for a
	pack wanted now (preempted: its slots built again later) */
	struct
	{
		short slots[MAXIMUM_SCENE_PACK_SLOTS];
		short slot_count;
		short load;
		int volatile low;
		long first_index;
		long salt;
		unsigned long file_base;
		boolean table_room;
		unsigned long file_room;
		int volatile done;
		boolean preempted;
	} job;
	/* the game thread's word to the worker: its builds pause (a map being
	copied to the cache partition, a network game joined; a prebuild's
	also while the game reads) */
	int volatile pause_builds;
	int volatile pause_prebuilds;
} scene_packs = { { { 0 } }, 0, 0, 0, NULL, 0, { NULL }, 0, 0, FALSE };

/* the worker's builds' results (its slots' progress points into them) */
static struct scene_pack_build_result scene_packs_build_results[MAXIMUM_SCENE_PACK_SLOTS];
/* (the worker thread's) whether this thread is the worker */
static __thread boolean scene_packs_on_worker = FALSE;

/* (each map's tags loaded counts one: a character made on another map
is gone with it, menu_scene_objects.c) */
static unsigned long scene_packs_epoch = 1;

/* recipes made by the game rather than read from app0:packs (the player
models found in Custom Edition maps: player_models.c), by name */
enum
{
	MAXIMUM_MADE_RECIPES = 32,
};

static struct
{
	char name[SCENE_PACK_NAME_SIZE];
	char *text;
	unsigned long size;
} scene_packs_made_recipes[MAXIMUM_MADE_RECIPES];
static int volatile scene_packs_made_recipes_lock;

static void made_recipes_take(void)
{
	while (__atomic_exchange_n(&scene_packs_made_recipes_lock, 1, __ATOMIC_ACQUIRE))
		SwitchToThread();
}

static void made_recipes_give(void)
{
	__atomic_store_n(&scene_packs_made_recipes_lock, 0, __ATOMIC_RELEASE);
}

/* a recipe's text: a made one's, else app0:packs/<name>.txt's (the caller
lets it go with halo_menus_file_free... or free: *made says which) */
static unsigned char *recipe_text(char const *name, unsigned long *size, boolean *made)
{
	char file_name[SCENE_PACK_NAME_SIZE + 8];
	short index;

	*made = FALSE;
	made_recipes_take();
	for (index = 0; index < MAXIMUM_MADE_RECIPES; index++)
	{
		if (scene_packs_made_recipes[index].text && !strcmp(scene_packs_made_recipes[index].name, name))
		{
			unsigned char *copy = malloc(scene_packs_made_recipes[index].size + 1);

			if (copy)
			{
				memcpy(copy, scene_packs_made_recipes[index].text, scene_packs_made_recipes[index].size);
				copy[scene_packs_made_recipes[index].size] = 0;
				*size = scene_packs_made_recipes[index].size;
				*made = TRUE;
			}
			made_recipes_give();
			return copy;
		}
	}
	made_recipes_give();
	snprintf(file_name, sizeof(file_name), "%s.txt", name);
	return halo_packs_file_read(file_name, size);
}

static void recipe_text_free(unsigned char *text, boolean made)
{
	if (!text)
		return;
	if (made)
		free(text);
	else
		halo_menus_file_free(text);
}

boolean scene_packs_recipe_register(
	char const *name,
	char const *text)
{
	unsigned long size = text ? (unsigned long)strlen(text) : 0;
	short index, free_index = NONE;
	char *copy;

	if (!name || !*name || strlen(name) >= SCENE_PACK_NAME_SIZE || !size || size > SCENE_PACK_MAXIMUM_RECIPE_BYTES)
		return FALSE;
	copy = malloc(size + 1);
	if (!copy)
		return FALSE;
	memcpy(copy, text, size + 1);
	made_recipes_take();
	for (index = 0; index < MAXIMUM_MADE_RECIPES; index++)
	{
		if (scene_packs_made_recipes[index].text && !strcmp(scene_packs_made_recipes[index].name, name))
		{
			free(scene_packs_made_recipes[index].text);
			free_index = index;
			break;
		}
		if (!scene_packs_made_recipes[index].text && free_index == NONE)
			free_index = index;
	}
	if (free_index == NONE)
	{
		made_recipes_give();
		free(copy);
		return FALSE;
	}
	snprintf(scene_packs_made_recipes[free_index].name, SCENE_PACK_NAME_SIZE, "%s", name);
	scene_packs_made_recipes[free_index].text = copy;
	scene_packs_made_recipes[free_index].size = size;
	made_recipes_give();
	return TRUE;
}

/* ---------- private code */

/* (tests: HALO_TEST_SLOW_IO=<KB/s>[:all], never on the Vita or in a
release build) the packs' own reads and writes - a pack's load, its
pixels' reads, a build's writes; with :all, a build's reads of its source
map too - paced to that rate on the thread that makes them, as a slow
memory card would: a game thread that waits for one shows it (its frame,
below) */
static void slow_io_pace(unsigned long bytes, boolean source)
{
#if !defined(HALO_VITA) && !defined(HALO_RELEASE)
	static long rate = -1;
	static boolean all = FALSE;
	static __thread unsigned long long owed_us;

	if (rate < 0)
	{
		char const *setting = getenv("HALO_TEST_SLOW_IO");

		rate = setting ? atol(setting) : 0;
		all = setting && strstr(setting, ":all") != NULL;
		if (rate > 0)
			platform_log("scene packs: HALO_TEST_SLOW_IO: the packs' reads and writes%s at %ld KB/s",
				all ? ", and their source maps'," : "", rate);
	}
	if (rate <= 0 || (source && !all))
		return;
	owed_us += (unsigned long long)bytes * 1000000ull / ((unsigned long long)rate * 1024ull);
	if (owed_us >= 1000)
	{
		Sleep((DWORD)(owed_us / 1000));
		owed_us %= 1000;
	}
#else
	(void)bytes;
	(void)source;
#endif
}

/* the game's cache partition's decompressed copies of the maps
(cache_files_windows.c), which a build reads in place of inflating a map */
static struct scene_pack_copy_source const scene_packs_copy_source =
{
	cache_files_cached_copy_open,
	cache_files_cached_copy_unchanged
};

/* the game thread's time in the packs' work, once it is long enough to be
a frame's hitch: logged ("scene packs: game thread: ...") */
#define GAME_THREAD_LOG_US 16000ull

static void game_thread_time(char const *what, char const *name, unsigned long long started)
{
	unsigned long long spent = vita_host_time_us() - started;

	if (spent >= GAME_THREAD_LOG_US)
		platform_log("scene packs: game thread: %s%s%s took %.1f ms", what, name ? " " : "", name ? name : "",
			(double)spent / 1000.0);
}

static boolean scene_packs_enabled(void)
{
	char const *setting = getenv("HALO_SCENE_PACKS");

	return !setting || !*setting || (strcmp(setting, "0") && _stricmp(setting, "false"));
}

char const *scene_packs_state_name(
	short state)
{
	static char const *const names[NUMBER_OF_SCENE_PACK_STATES] =
	{
		"none", "building", "ready", "loaded", "failed"
	};

	return state >= 0 && state < NUMBER_OF_SCENE_PACK_STATES ? names[state] : "?";
}

static short state_get(struct scene_pack_slot const *slot)
{
	return __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE);
}

static void state_set(struct scene_pack_slot *slot, short state)
{
	__atomic_store_n(&slot->state, state, __ATOMIC_RELEASE);
}

static struct scene_pack_slot *slot_find(char const *name)
{
	short index;

	for (index = 0; index < scene_packs.pack_count; index++)
	{
		if (!strcmp(scene_packs.packs[index].name, name))
			return &scene_packs.packs[index];
	}
	return NULL;
}

/* the tag cache: its base, where the map's tags and the packs end, and where
the room for more ends (the lowest structure bsp's place) */
static unsigned char *tag_cache_room(unsigned char **room_end)
{
	unsigned char *base = physical_memory_get_tag_cache_base_address();
	unsigned char *start = base + ((cache_file_tag_data_size() + SCENE_PACK_ALIGNMENT - 1) & ~(SCENE_PACK_ALIGNMENT - 1)) +
		scene_packs.reserved;
	unsigned char *end = base + TAG_CACHE_SIZE;
	struct scenario *scenario = global_scenario_get();

	/* (as a map loads - Forge's and the player models' packs load as its
	tags are read, before the scenario is the game's - its own scenario
	tag: the only one of the map's tags; without it the room would run
	over the structure bsp's place, and the bsp then refused) */
	if (!scenario)
	{
		struct tag_iterator iterator;
		long scenario_index;

		tag_iterator_new(&iterator, SCENARIO_TAG);
		scenario_index = tag_iterator_next(&iterator);
		if (scenario_index != NONE && tag_get_group_tag(scenario_index) == SCENARIO_TAG)
			scenario = (struct scenario *)tag_get(SCENARIO_TAG, scenario_index);
	}
	if (scenario)
	{
		long index;

		for (index = 0; index < scenario->structure_bsp_references.count; index++)
		{
			struct scenario_structure_bsp_reference const *reference = TAG_BLOCK_GET_ELEMENT(
				&scenario->structure_bsp_references, index, struct scenario_structure_bsp_reference);
			unsigned char *place = reference->base_address;

			if (place >= base && place < end)
				end = place;
		}
	}
	*room_end = end > start ? end : start;
	return start;
}

/* the slot's pack file checked against its recipe and source map now (its
header, scene_pack_open; its source map, the one it was built from, still
one of the recipe's and of the same identity): TRUE if it may be loaded */
static boolean pack_current(struct scene_pack_slot *slot)
{
	unsigned long long source_hash;
	char map[SCENE_PACK_NAME_SIZE];

	if (!scene_pack_open(slot->path, &slot->info, slot->why, sizeof(slot->why)))
		return FALSE;
	if (slot->info.recipe_hash != slot->recipe->hash || slot->info.builder_version != SCENE_PACK_BUILDER_VERSION)
	{
		snprintf(slot->why, sizeof(slot->why), "its recipe or builder changed");
		return FALSE;
	}
	/* (an Xbox map's: any of the recipe's maps it was built from, as it was;
	a Custom Edition map's: the first there of the identity it gives) */
	if (slot->recipe->custom_edition_map_count ?
		(!scene_pack_source_check(slot->recipe, cache_files_map_directory(), map, sizeof(map), &source_hash, NULL, 0) ||
			strcmp(map, slot->info.source_map)) :
		!scene_pack_source_identity(slot->recipe, cache_files_map_directory(), slot->info.source_map, &source_hash))
	{
		snprintf(slot->why, sizeof(slot->why), "its source map changed");
		return FALSE;
	}
	if (source_hash != slot->info.source_hash)
	{
		snprintf(slot->why, sizeof(slot->why), "its source map changed");
		return FALSE;
	}
	return TRUE;
}

static void stamp_remove(struct scene_pack_slot const *slot);

/* (the worker; or the caller of scene_packs_load_now) a build's end, for
a slot: logged, and the pack built checked as any (pack_current) */
static boolean pack_built(struct scene_pack_slot *slot, struct scene_pack_build_result const *result, boolean built,
	short others)
{
	slot->build = *result;
	slot->progress = NULL;
	if (!built)
	{
		snprintf(slot->why, sizeof(slot->why), "%s", result->why[0] ? result->why : "it could not be built");
		platform_log("scene packs: %s: not built: %s", slot->name, slot->why);
		return FALSE;
	}
	slot->built = TRUE;
	stamp_remove(slot);
	platform_log("scene packs: %s: built from %s in %.1f ms%s: %ld tags (%ld references left out), %lu KB of tags, "
		"%lu KB of pixels and samples, %lu KB file (%.1f MB of the map read%s)", slot->name, result->map,
		(double)result->microseconds / 1000.0, others ? " with other packs in the same read" : "", result->tag_count,
		result->stripped_count, result->tag_bytes / 1024, result->data_bytes / 1024, result->file_bytes / 1024,
		(double)result->source_bytes / (1024.0 * 1024.0), others ? ", for all of them" : "");
	if (!pack_current(slot))
	{
		platform_log("scene packs: %s: the pack built cannot be opened: %s", slot->name, slot->why);
		return FALSE;
	}
	return TRUE;
}

/* the map a build of the lead slot (of slots[], those needing a build)
reads: of the lead's recipe's maps there, one read in place (its maps
folder file not compressed, or the game's decompressed copy of it:
scene_pack_source_seekable) first, then (shared: not for a pack wanted now,
which a larger map's read would delay) the one most of the others also
name (one read for them all), then the recipe's order. FALSE if none */
static boolean build_map_choose(struct scene_pack_slot *const *slots, short count, short lead, boolean shared, char *map,
	long map_size)
{
	struct scene_pack_recipe const *recipe = slots[lead]->recipe;
	char const *directory = cache_files_map_directory();
	long best_score = -1;
	short index, other;

	for (index = 0; index < recipe->map_count; index++)
	{
		unsigned long long hash;
		long score;

		if (!scene_pack_source_identity(recipe, directory, recipe->maps[index], &hash))
			continue;
		score = scene_pack_source_seekable(directory, recipe->maps[index]) ? 1000 : 0;
		for (other = 0; shared && other < count; other++)
		{
			if (other != lead && slots[other] && !slots[other]->recipe->custom_edition_map_count &&
				scene_pack_source_identity(slots[other]->recipe, directory, recipe->maps[index], &hash))
			{
				score++;
			}
		}
		if (score > best_score)
		{
			best_score = score;
			snprintf(map, (size_t)map_size, "%s", recipe->maps[index]);
		}
	}
	return best_score >= 0;
}

/* (the worker; or the caller of scene_packs_load_now) the slots' packs
checked, and those not current built - those that can share one read of a
map in one (scene_pack_build_many), the first's group first; each slot
ready, or failed (its why). only_first: the first slot's group alone, the
others left needing it (*left: how many). shared: a map the most others
also name chosen first (a prebuild's: one read for them all), else the
first slot's own choice (a pack wanted now: no larger read for it) */
static void packs_prepare(struct scene_pack_slot **slots, short count, unsigned char *scratch, unsigned long scratch_size,
	boolean only_first, boolean shared, short *left)
{
	struct scene_pack_slot *need[MAXIMUM_SCENE_PACK_SLOTS];
	short index;

	if (left)
		*left = 0;
	for (index = 0; index < count; index++)
	{
		need[index] = NULL;
		if (!slots[index] || scene_packs.cancel)
			continue;
		if (pack_current(slots[index]))
		{
			state_set(slots[index], _scene_pack_state_ready);
			continue;
		}
		need[index] = slots[index];
	}
	for (;;)
	{
		struct scene_pack_recipe const *recipes[MAXIMUM_SCENE_PACK_SLOTS];
		char const *paths[MAXIMUM_SCENE_PACK_SLOTS];
		struct scene_pack_slot *group[MAXIMUM_SCENE_PACK_SLOTS];
		char map[SCENE_PACK_NAME_SIZE];
		short lead, group_count = 0;
		boolean built;

		for (lead = 0; lead < count && !need[lead]; lead++)
			;
		if (lead == count || scene_packs.cancel)
			break;
		CreateDirectoryA(SCENE_PACKS_FOLDER, NULL);
		/* (a Custom Edition map's: alone) */
		if (need[lead]->recipe->custom_edition_map_count)
		{
			struct scene_pack_slot *slot = need[lead];

			need[lead] = NULL;
			platform_log("scene packs: %s: building it (%s)", slot->name, slot->why);
			slot->progress = &scene_packs_build_results[0].progress;
			built = scene_pack_build(slot->recipe, cache_files_map_directory(), slot->path, scratch, scratch_size,
				&scene_packs.cancel, &scene_packs_build_results[0]);
			state_set(slot, pack_built(slot, &scene_packs_build_results[0], built, 0) ? _scene_pack_state_ready :
				_scene_pack_state_failed);
			if (only_first)
				break;
			continue;
		}
		if (!build_map_choose(need, count, lead, shared, map, sizeof(map)))
		{
			struct scene_pack_slot *slot = need[lead];

			need[lead] = NULL;
			snprintf(slot->why, sizeof(slot->why), "none of its maps (%s...) is in the maps folder", slot->recipe->maps[0]);
			platform_log("scene packs: %s: not built: %s", slot->name, slot->why);
			state_set(slot, _scene_pack_state_failed);
			if (only_first)
				break;
			continue;
		}
		for (index = lead; index < count; index++)
		{
			unsigned long long hash;

			if (need[index] && !need[index]->recipe->custom_edition_map_count &&
				scene_pack_source_identity(need[index]->recipe, cache_files_map_directory(), map, &hash))
			{
				group[group_count] = need[index];
				recipes[group_count] = need[index]->recipe;
				paths[group_count] = need[index]->path;
				need[index]->progress = &scene_packs_build_results[group_count].progress;
				scene_packs_build_results[group_count].progress = 0;
				platform_log("scene packs: %s: building it (%s) from %s%s", need[index]->name, need[index]->why, map,
					scene_pack_source_seekable(cache_files_map_directory(), map) ? ", read in place" : "");
				group_count++;
				need[index] = NULL;
			}
		}
		built = scene_pack_build_many(recipes, group_count, map, cache_files_map_directory(), paths, scratch, scratch_size,
			&scene_packs.cancel, scene_packs_build_results);
		(void)built;
		for (index = 0; index < group_count; index++)
		{
			boolean this_built = !scene_packs_build_results[index].why[0];

			/* (stopped to make way for another: built again later) */
			if (scene_packs.cancel && scene_packs.job.preempted)
			{
				group[index]->progress = NULL;
				continue;
			}
			state_set(group[index], pack_built(group[index], &scene_packs_build_results[index], this_built,
				(short)(group_count - 1)) ? _scene_pack_state_ready : _scene_pack_state_failed);
		}
		if (only_first)
			break;
	}
	for (index = 0; left && index < count; index++)
	{
		if (need[index])
			(*left)++;
	}
}

/* ---------- a pack's data checked once

A pack's data (its pixels and samples: up to 36 MB, covenant_ai's) is read
whole for its CRC once: after it is built, or the first time it is loaded
(or, for a prebuild, in the background), a record of the file then checked
is kept beside it (t:\packs\<name>.ok: its size and time, as the file
system gives them, and its header's CRCs). A later load whose file has that
size, time and header reads its tags and checks them as ever (their CRC,
their move, the tag check) but not its data again - the first Forge game
with every pack on read 60 MB from the memory card at Forge's start, no
frame for well over 8 s. A file written again (a build, a copy, a damage
that rewrites it) has another time or size: checked whole again. */

static void stamp_path(struct scene_pack_slot const *slot, char *path, long size)
{
	snprintf(path, (size_t)size, "%s.ok", slot->path);
}

/* the pack file's size and time, as the file system gives them: FALSE if
it cannot say */
static boolean pack_file_identity(struct scene_pack_slot const *slot, unsigned long *size, unsigned long *seconds,
	unsigned long *nanoseconds)
{
	return halo_file_identity(slot->path, size, seconds, nanoseconds) == 0;
}

/* whether the pack file (its header in slot->info) is the one whose data
was checked: its record there and the same */
static boolean stamp_valid(struct scene_pack_slot const *slot)
{
	char path[128];
	char text[160];
	unsigned long size, seconds, nanoseconds;
	unsigned long stamp_size = 0, stamp_seconds = 0, stamp_nanoseconds = 0, stamp_data_crc = 0, stamp_tags_crc = 0;
	FILE *file;
	size_t length;

	if (!pack_file_identity(slot, &size, &seconds, &nanoseconds))
		return FALSE;
	stamp_path(slot, path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return FALSE;
	length = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[length] = 0;
	return sscanf(text, "HCEVPACKOK %lu %lu %lu %lx %lx", &stamp_size, &stamp_seconds, &stamp_nanoseconds, &stamp_data_crc,
		&stamp_tags_crc) == 5 && stamp_size == size && stamp_seconds == seconds && stamp_nanoseconds == nanoseconds &&
		size == slot->info.file_size && stamp_data_crc == slot->info.data_crc && stamp_tags_crc == slot->info.tags_crc;
}

/* the record of the pack file whose data was just checked (as its header in
slot->info says) */
static void stamp_write(struct scene_pack_slot const *slot)
{
	char path[128];
	unsigned long size, seconds, nanoseconds;
	FILE *file;

	stamp_path(slot, path, sizeof(path));
	if (!pack_file_identity(slot, &size, &seconds, &nanoseconds) || size != slot->info.file_size)
	{
		remove(path);
		return;
	}
	file = fopen(path, "wb");
	if (!file)
		return;
	fprintf(file, "HCEVPACKOK %lu %lu %lu %08lx %08lx\n", size, seconds, nanoseconds, slot->info.data_crc, slot->info.tags_crc);
	fclose(file);
}

static void stamp_remove(struct scene_pack_slot const *slot)
{
	char path[128];

	stamp_path(slot, path, sizeof(path));
	remove(path);
}

/* (the worker, a prebuild's job) the pack's data checked now if it has no
record yet, in the background, so that its load is quick when it is
wanted: FALSE if it is damaged (deleted, to be built again) */
static boolean pack_data_verify(struct scene_pack_slot *slot)
{
	FILE *file;
	boolean good;

	if (stamp_valid(slot))
		return TRUE;
	file = fopen(slot->path, "rb");
	good = file && scene_pack_open_file(file, &slot->info, slot->why, sizeof(slot->why)) &&
		scene_pack_data_check(file, &slot->info, slot->why, sizeof(slot->why));
	if (file)
		fclose(file);
	if (scene_packs.cancel)
		return TRUE;
	if (good)
	{
		stamp_write(slot);
		platform_log("scene packs: %s: its data checked in the background (%lu KB)", slot->name, slot->info.data_size / 1024);
	}
	else
	{
		platform_log("scene packs: %s: refused: %s", slot->name, slot->why);
		stamp_remove(slot);
		remove(slot->path);
	}
	return good;
}

/* (the worker; or the caller of scene_packs_load_now) the slot's pack,
built and current, read into room (room_size bytes) and checked there (the
pack opened once, its tags read once and checked as read, its data's CRC by
the same handle, which then serves its reads: scene_pack_load_file), given
the job's tag indices, salt and file offsets: staged. Otherwise full (no
room: the tag cache, the table, the file range) or refused (damaged) */
static void pack_stage(struct scene_pack_slot *slot, unsigned char *room, unsigned long room_size)
{
	unsigned long long started = vita_host_time_us();
	FILE *file;
	long count;
	boolean data_checked;

	slot->full = slot->refused = FALSE;
	file = fopen(slot->path, "rb");
	if (!file || !scene_pack_open_file(file, &slot->info, slot->why, sizeof(slot->why)))
	{
		if (file)
			fclose(file);
		else
			snprintf(slot->why, sizeof(slot->why), "it cannot be read");
		slot->refused = TRUE;
		return;
	}
	if (slot->info.recipe_hash != slot->recipe->hash || slot->info.builder_version != SCENE_PACK_BUILDER_VERSION)
	{
		fclose(file);
		snprintf(slot->why, sizeof(slot->why), "it changed after it was checked");
		slot->refused = TRUE;
		return;
	}
	if (slot->info.tags_size > room_size || !scene_packs.job.table_room || slot->info.data_size > scene_packs.job.file_room)
	{
		fclose(file);
		snprintf(slot->why, sizeof(slot->why), slot->info.tags_size > room_size ? "the tag cache has %lu KB free for its %lu KB" :
			!scene_packs.job.table_room ? "as many packs as a map takes are loaded (%lu, %lu)" :
			"the packs loaded hold as many pixels and samples as the game reads (%lu, %lu)", room_size / 1024,
			slot->info.tags_size / 1024);
		slot->full = TRUE;
		return;
	}
	data_checked = stamp_valid(slot);
	if (!scene_pack_load_file_checked(file, &slot->info, room, data_checked, slot->why, sizeof(slot->why)))
	{
		fclose(file);
		csmemset(room, 0xCD, slot->info.tags_size);
		slot->refused = TRUE;
		return;
	}
	if (!data_checked)
		stamp_write(slot);
	scene_pack_instances(room, &count);
	if (count <= 0 || scene_packs.job.first_index < 0 || scene_packs.job.first_index > 0x7FFF - count)
	{
		fclose(file);
		csmemset(room, 0xCD, slot->info.tags_size);
		snprintf(slot->why, sizeof(slot->why), "the map's %ld tags and its %ld are too many for a tag table",
			scene_packs.job.first_index, count);
		slot->refused = TRUE;
		return;
	}
	scene_pack_rebase(room, scene_packs.job.first_index, scene_packs.job.salt, (long)scene_packs.job.file_base);
	slot->tags = room;
	slot->file = file;
	slot->staged_first_index = scene_packs.job.first_index;
	slot->staged_salt = scene_packs.job.salt;
	slot->staged_file_base = scene_packs.job.file_base;
	slot->staged = TRUE;
	platform_log("scene packs: %s: read and checked on the %s in %.1f ms (%lu KB of tags, %lu KB of pixels and samples%s)",
		slot->name, scene_packs_on_worker ? "worker" : "game thread", (double)(vita_host_time_us() - started) / 1000.0,
		slot->info.tags_size / 1024, slot->info.data_size / 1024,
		data_checked ? ", checked before: the file unchanged since" : ", checked whole");
}

/* a staged pack let go (not to be loaded now): its tag cache room the free
room's again, its file closed */
static void pack_unstage(struct scene_pack_slot *slot)
{
	if (!slot->staged)
		return;
	csmemset(slot->tags, 0xCD, slot->info.tags_size);
	if (slot->file)
		fclose(slot->file);
	slot->file = NULL;
	slot->tags = NULL;
	slot->staged = FALSE;
}

/* (the worker thread) its job: its slots checked and built (the slot it
loads, and those sharing a read with it, first), that slot staged, then
the others built */
static void *worker_thread(void *context)
{
	struct scene_pack_slot *slots[MAXIMUM_SCENE_PACK_SLOTS];
	struct scene_pack_slot *load = scene_packs.job.load != NONE ? &scene_packs.packs[scene_packs.job.load] : NULL;
	short index, left = 0;

	(void)context;
	(void)left;
	scene_packs_on_worker = TRUE;
#ifdef HALO_VITA
	/* (below the game's threads; a prebuild's lower still) */
	vita_host_thread_priority_set(scene_packs.job.low ? 191 : 175);
#endif
	for (index = 0; index < scene_packs.job.slot_count; index++)
		slots[index] = &scene_packs.packs[scene_packs.job.slots[index]];
	/* (a slot to load: built, with those that share its read, if it must
	be - it is the job's first slot then -, and staged, the job done (the
	others' builds the next job's, once it is loaded); else every slot's
	builds) */
	if (load)
	{
		if (state_get(load) == _scene_pack_state_building && scene_packs.job.slot_count && slots[0] == load)
			packs_prepare(slots, scene_packs.job.slot_count, scene_packs.scratch, scene_packs.scratch_size, TRUE, FALSE, &left);
		if (!scene_packs.cancel && state_get(load) == _scene_pack_state_ready)
			pack_stage(load, scene_packs.scratch, scene_packs.scratch_size);
	}
	else if (scene_packs.job.slot_count)
	{
		/* (a prebuild's job: its first slot's read alone, those sharing it
		with it; the next read the next job's - a pack built meanwhile is
		loaded between them) */
		packs_prepare(slots, scene_packs.job.slot_count, scene_packs.scratch, scene_packs.scratch_size, scene_packs.job.low,
			TRUE, NULL);
		/* (a prebuild's: the data of each pack ready checked now, in the
		background, if it never was: its load is then quick) */
		for (index = 0; scene_packs.job.low && index < scene_packs.job.slot_count && !scene_packs.cancel; index++)
		{
			if (state_get(slots[index]) == _scene_pack_state_ready && !pack_data_verify(slots[index]))
				state_set(slots[index], _scene_pack_state_building);
		}
	}
	__atomic_store_n(&scene_packs.job.done, TRUE, __ATOMIC_RELEASE);
	return NULL;
}

/* (scene_pack_io_hook) a build's or a load's read or write done: paced in
the tests (slow_io_pace); on the worker, paused while the game wants it to
(a map copied to the cache partition, a network game; a prebuild also
while the game's own reads are waiting), and a prebuild's spaced out */
static void worker_io(unsigned long bytes, boolean source)
{
	static __thread unsigned long since_yield;
	int cache_files_reads_pending(void);

	slow_io_pace(bytes, source);
	/* (a pack read on the game thread - scene_packs_load_now at a map's
	load - is progress, not a hang: the Vita's watchdog) */
	if (!scene_packs_on_worker)
	{
		halo_progress_heartbeat();
		return;
	}
	/* (a pack wanted now pauses only its source map's reads, the heavy
	part: its own pack's few megabytes are read meanwhile, its scene not
	held for a whole map's copy) */
	while (!scene_packs.cancel && ((__atomic_load_n(&scene_packs.pause_builds, __ATOMIC_ACQUIRE) &&
		(source || scene_packs.job.low)) ||
		(scene_packs.job.low && __atomic_load_n(&scene_packs.pause_prebuilds, __ATOMIC_ACQUIRE))))
	{
		Sleep(20);
	}
	if (scene_packs.job.low)
	{
		since_yield += bytes;
		if (since_yield >= 128 * 1024)
		{
			since_yield = 0;
			/* (the game's reads first: a texture or a sound waiting) */
			while (!scene_packs.cancel && scene_packs.job.low && cache_files_reads_pending())
				Sleep(4);
			Sleep(1);
		}
	}
}

/* the worker waited for (stopped first if stop) */
static void build_thread_join(boolean stop)
{
	short index;

	if (!scene_packs.thread_running)
		return;
	if (stop)
		scene_packs.cancel = TRUE;
	pthread_join(scene_packs.thread, NULL);
	/* (its scratch the free room's again, but a pack it staged) */
	if (scene_packs.scratch && scene_packs.scratch_size)
	{
		struct scene_pack_slot *load = scene_packs.job.load != NONE ? &scene_packs.packs[scene_packs.job.load] : NULL;
		unsigned long staged = load && load->staged ?
			(load->info.tags_size + SCENE_PACK_ALIGNMENT - 1) & ~(unsigned long)(SCENE_PACK_ALIGNMENT - 1) : 0;

		if (stop && load)
		{
			pack_unstage(load);
			staged = 0;
		}
		if (scene_packs.scratch_size > staged)
			csmemset(scene_packs.scratch + staged, 0xCD, scene_packs.scratch_size - staged);
	}
	/* (a job stopped to make way: what it had not built, to build again) */
	for (index = 0; index < scene_packs.job.slot_count; index++)
	{
		struct scene_pack_slot *slot = &scene_packs.packs[scene_packs.job.slots[index]];

		slot->progress = NULL;
		if (scene_packs.job.preempted && state_get(slot) == _scene_pack_state_building)
			platform_log("scene packs: %s: its build makes way for a pack wanted now", slot->name);
	}
	scene_packs.thread_running = FALSE;
	scene_packs.cancel = FALSE;
	scene_packs.scratch = NULL;
	scene_packs.scratch_size = 0;
	scene_packs.job.slot_count = 0;
	scene_packs.job.load = NONE;
	scene_packs.job.done = FALSE;
	scene_packs.job.preempted = FALSE;
}

/* the game's table and file range as the next pack loaded finds them: its
first tag index, its salt, its file offsets, whether there is room */
static void table_next(long *first_index, long *salt, unsigned long *file_base, boolean *table_room, unsigned long *file_room)
{
	long existing_count = 0, index, highest = 0;
	void *existing = cache_files_tag_instances(&existing_count);

	for (index = 0; existing && index < existing_count; index++)
	{
		long instance_salt = (long)(*(unsigned long *)((unsigned char *)existing + index * TAG_INSTANCE_BYTES + 12) >> 16);

		if (instance_salt > highest)
			highest = instance_salt;
	}
	highest = MAX((highest + 1) & 0x7FFF, scene_packs.next_salt);
	if (highest <= 0 || highest > 0x7FFF)
		highest = 1;
	*first_index = existing ? existing_count : NONE;
	*salt = highest;
	*file_base = scene_packs.file_used;
	*table_room = existing && scene_packs.table_count < MAXIMUM_LOADED_SCENE_PACKS;
	*file_room = SCENE_PACK_FILE_RANGE_BYTES - scene_packs.file_used;
}

/* whether a slot is wanted loaded now (not a prebuild's, not let go) */
static boolean slot_wanted_loaded(struct scene_pack_slot const *slot)
{
	return !slot->build_only && !slot->release_wanted;
}

/* whether a ready slot's load may be tried (not waiting for room that has
not changed since) */
static boolean slot_load_due(struct scene_pack_slot const *slot)
{
	return !slot->full || slot->full_reserved != scene_packs.reserved || slot->full_tables != scene_packs.table_count;
}

/* the worker given its next job, if it is free: the first pack wanted
loaded (with the builds that can share its read), else every prebuild
(paced, paused while the game is busy) */
static void build_thread_start(void)
{
	short index, load = NONE;
	pthread_attr_t attributes;
	unsigned char *room, *room_end;
	boolean low = TRUE;

	if (scene_packs.thread_running)
		return;
	scene_packs.job.slot_count = 0;
	for (index = 0; index < scene_packs.pack_count && load == NONE; index++)
	{
		struct scene_pack_slot *slot = &scene_packs.packs[index];
		short state = state_get(slot);

		if (slot_wanted_loaded(slot) && (state == _scene_pack_state_building ||
			(state == _scene_pack_state_ready && slot_load_due(slot))))
		{
			load = index;
		}
	}
	if (load != NONE)
	{
		low = FALSE;
		if (state_get(&scene_packs.packs[load]) == _scene_pack_state_building)
			scene_packs.job.slots[scene_packs.job.slot_count++] = load;
	}
	for (index = 0; index < scene_packs.pack_count; index++)
	{
		if (index != load && state_get(&scene_packs.packs[index]) == _scene_pack_state_building)
			scene_packs.job.slots[scene_packs.job.slot_count++] = index;
	}
	/* (a prebuild alone waits while the game is busy) */
	if (load == NONE && (!scene_packs.job.slot_count || scene_packs.pause_prebuilds || scene_packs.pause_builds))
	{
		scene_packs.job.slot_count = 0;
		return;
	}
	room = tag_cache_room(&room_end);
	scene_packs.scratch = room;
	scene_packs.scratch_size = (unsigned long)(room_end - room);
	scene_packs.job.load = load;
	scene_packs.job.low = low;
	scene_packs.job.done = FALSE;
	scene_packs.job.preempted = FALSE;
	table_next(&scene_packs.job.first_index, &scene_packs.job.salt, &scene_packs.job.file_base, &scene_packs.job.table_room,
		&scene_packs.job.file_room);
	scene_packs.cancel = FALSE;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, BUILD_THREAD_STACK_BYTES);
	if (pthread_create(&scene_packs.thread, &attributes, worker_thread, NULL) == 0)
	{
		scene_packs.thread_running = TRUE;
	}
	else
	{
		for (index = 0; index < scene_packs.job.slot_count; index++)
		{
			struct scene_pack_slot *slot = &scene_packs.packs[scene_packs.job.slots[index]];

			snprintf(slot->why, sizeof(slot->why), "no thread to build it");
			state_set(slot, _scene_pack_state_failed);
		}
		if (load != NONE)
		{
			snprintf(scene_packs.packs[load].why, sizeof(scene_packs.packs[load].why), "no thread to load it");
			state_set(&scene_packs.packs[load], _scene_pack_state_failed);
		}
		scene_packs.job.slot_count = 0;
		scene_packs.job.load = NONE;
		scene_packs.scratch = NULL;
		scene_packs.scratch_size = 0;
	}
	pthread_attr_destroy(&attributes);
}

/* (the game thread, the top of a frame) a staged pack put in the game: its
indices made the table's now if the table changed since it was staged, its
buffers registered, the table grown, its bytes reserved. Cheap: no read,
no check (the worker made them). TRUE if loaded; FALSE, unstaged, if the
table has no room for it now */
static boolean pack_commit(struct scene_pack_slot *slot)
{
	unsigned long long started = vita_host_time_us();
	unsigned char *room, *room_end;
	long first_index, salt, existing_count, count;
	unsigned long file_base, file_room;
	boolean table_room;
	void *existing;
	unsigned char *table, *instances;

	room = tag_cache_room(&room_end);
	table_next(&first_index, &salt, &file_base, &table_room, &file_room);
	existing = cache_files_tag_instances(&existing_count);
	instances = scene_pack_instances(slot->tags, &count);
	/* (packs let go since it was staged: the room begins lower now - read
	there again, by the next job) */
	if (slot->tags != room)
	{
		pack_unstage(slot);
		return FALSE;
	}
	if (!existing || !table_room || slot->info.data_size > file_room ||
		existing_count > 0x7FFF - count || !(table = malloc((unsigned long)(existing_count + count) * TAG_INSTANCE_BYTES)))
	{
		snprintf(slot->why, sizeof(slot->why), "the game's table or tag cache changed before it could be loaded");
		slot->full = TRUE;
		slot->full_reserved = scene_packs.reserved;
		slot->full_tables = scene_packs.table_count;
		pack_unstage(slot);
		return FALSE;
	}
	if (first_index != slot->staged_first_index || salt != slot->staged_salt || file_base != slot->staged_file_base)
	{
		platform_log("scene packs: %s: given indices %ld (salt %ld) again, not %ld (salt %ld)", slot->name, first_index, salt,
			slot->staged_first_index, slot->staged_salt);
		scene_pack_rebase_again(slot->tags, slot->staged_first_index, first_index, salt,
			(long)file_base - (long)slot->staged_file_base);
	}
	scene_packs.next_salt = (salt + 1) & 0x7FFF;
	memcpy(table, existing, (unsigned long)existing_count * TAG_INSTANCE_BYTES);
	memcpy(table + existing_count * TAG_INSTANCE_BYTES, instances, (unsigned long)count * TAG_INSTANCE_BYTES);
	tags_header_register_vertex_and_index_buffers((struct cache_file_tag_header *)slot->tags);
	slot->staged = FALSE;
	slot->full = FALSE;
	slot->bytes = (slot->info.tags_size + SCENE_PACK_ALIGNMENT - 1) & ~(SCENE_PACK_ALIGNMENT - 1);
	slot->first_index = existing_count;
	slot->count = count;
	slot->file_base = file_base;
	scene_packs.file_used = (file_base + slot->info.data_size + SCENE_PACK_RANGE_ALIGNMENT - 1) &
		~(unsigned long)(SCENE_PACK_RANGE_ALIGNMENT - 1);
	if (!scene_packs.original_instances)
	{
		scene_packs.original_instances = existing;
		scene_packs.original_count = existing_count;
	}
	scene_packs.tables[scene_packs.table_count++] = table;
	scene_packs.reserved += slot->bytes;
	tag_validate_reserve_after_tags(scene_packs.reserved);
	state_set(slot, _scene_pack_state_loaded);
	cache_files_set_tag_instances(table, existing_count + count);
	platform_log("scene packs: %s: loaded from %s in %.2f ms (the game thread's part): %ld tags (%ld to %ld of the "
		"table), %lu KB of the tag cache (%lu KB held by packs, %lu KB free after them), %lu KB of pixels and samples in "
		"its file", slot->name, slot->info.source_map, (double)(vita_host_time_us() - started) / 1000.0,
		count, existing_count, existing_count + count - 1, slot->bytes / 1024,
		scene_packs.reserved / 1024, (unsigned long)(room_end - room - slot->bytes) / 1024, slot->info.data_size / 1024);
	if (slot->wanted_us)
		platform_log("scene packs: %s: loaded %.1f ms after it was wanted", slot->name,
			(double)(vita_host_time_us() - slot->wanted_us) / 1000.0);
	return TRUE;
}

/* (the game thread) a slot whose worker's load refused it: deleted, built
once more, then given up */
static void slot_refused(struct scene_pack_slot *slot)
{
	slot->refused = FALSE;
	platform_log("scene packs: %s: refused: %s", slot->name, slot->why);
	stamp_remove(slot);
	remove(slot->path);
	if (!slot->rebuilt)
	{
		slot->rebuilt = TRUE;
		state_set(slot, _scene_pack_state_building);
	}
	else
	{
		state_set(slot, _scene_pack_state_failed);
	}
}

static void packs_release(void);

/* (the game thread) the worker's job done: its staged pack loaded (or let
go, not wanted now), its refusals and waits seen to */
static void job_finish(void)
{
	short load = scene_packs.job.load;
	struct scene_pack_slot *slot = load != NONE ? &scene_packs.packs[load] : NULL;

	build_thread_join(FALSE);
	/* (the packs let go meanwhile go first: a model passed in the preview
	is let go before the next is put above it) */
	packs_release();
	if (!slot)
		return;
	if (slot->staged)
	{
		if (slot_wanted_loaded(slot))
			pack_commit(slot);
		else
			pack_unstage(slot);
	}
	else if (slot->refused)
	{
		slot_refused(slot);
	}
	else if (slot->full)
	{
		slot->full_reserved = scene_packs.reserved;
		slot->full_tables = scene_packs.table_count;
		if (!slot->waiting_said)
			platform_log("scene packs: %s: waits: %s", slot->name, slot->why);
		slot->waiting_said = TRUE;
	}
}

/* whether a Custom Edition recipe's map is one the player turned off (the
settings panel's Modded maps: HALO_MAPS_DISABLED), and why: its pack is
neither offered nor built nor loaded */
static boolean recipe_turned_off(struct scene_pack_recipe const *recipe, char *why, long why_size)
{
	char map[SCENE_PACK_NAME_SIZE];

	if (!recipe->custom_edition_map_count ||
		!scene_pack_source_check(recipe, cache_files_map_directory(), map, sizeof(map), NULL, NULL, 0) ||
		!custom_edition_maps_turned_off(map))
	{
		return FALSE;
	}
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "%s.map is turned off (the settings' Modded maps)", map);
	return TRUE;
}

/* a slot for the pack of that recipe (its recipe read), or NULL */
static struct scene_pack_slot *slot_new(char const *name)
{
	struct scene_pack_slot *slot;
	unsigned char *text;
	unsigned long size = 0;
	char why[SCENE_PACK_WHY_SIZE];
	boolean made;

	if (scene_packs.pack_count >= MAXIMUM_SCENE_PACK_SLOTS || !name || strlen(name) >= SCENE_PACK_NAME_SIZE)
		return NULL;
	slot = &scene_packs.packs[scene_packs.pack_count++];
	memset(slot, 0, sizeof(*slot));
	snprintf(slot->name, sizeof(slot->name), "%s", name);
	text = recipe_text(name, &size, &made);
	slot->recipe = malloc(sizeof(struct scene_pack_recipe));
	if (!text || !slot->recipe || !scene_pack_recipe_parse((char const *)text, (long)size, slot->recipe, why, sizeof(why)) ||
		strcmp(slot->recipe->name, name))
	{
		snprintf(slot->why, sizeof(slot->why), "%s", !text ? "its recipe cannot be read" :
			slot->recipe && slot->recipe->name[0] && strcmp(slot->recipe->name, name) ? "its recipe is another pack's" : why);
		state_set(slot, _scene_pack_state_failed);
		platform_log("scene packs: %s: %s", name, slot->why);
	}
	else if (recipe_turned_off(slot->recipe, slot->why, sizeof(slot->why)))
	{
		state_set(slot, _scene_pack_state_failed);
		platform_log("scene packs: %s: %s", name, slot->why);
	}
	else
	{
		snprintf(slot->path, sizeof(slot->path), "%s\\%s.pack", SCENE_PACKS_FOLDER, name);
		state_set(slot, _scene_pack_state_building);
	}
	recipe_text_free(text, made);
	return slot;
}

/* whether a loaded pack may go now: no object made of its tags (an object
deleted goes at the objects' next update), none of its sounds playing */
static boolean pack_unused(struct scene_pack_slot const *slot)
{
	struct object_iterator iterator;
	struct object_datum *object;
	unsigned char *instances;
	long count, index;

	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		long absolute = object->definition_index & 0xFFFF;

		if (object->definition_index != NONE && absolute >= slot->first_index && absolute < slot->first_index + slot->count)
			return FALSE;
	}
	instances = scene_pack_instances(slot->tags, &count);
	for (index = 0; index < count; index++)
	{
		unsigned char const *instance = instances + index * TAG_INSTANCE_BYTES;

		if (*(unsigned long const *)instance == SOUND_DEFINITION_TAG)
		{
			struct sound_definition *sound = sound_definition_get((long)*(unsigned long const *)(instance + 12));
			long range, permutation;

			for (range = 0; range < sound->pitch_ranges.count; range++)
			{
				struct sound_pitch_range *pitch_range = TAG_BLOCK_GET_ELEMENT(&sound->pitch_ranges, range,
					struct sound_pitch_range);

				for (permutation = 0; permutation < pitch_range->permutations.count; permutation++)
				{
					if (sound_cache_sound_in_use(TAG_BLOCK_GET_ELEMENT(&pitch_range->permutations, permutation,
						struct sound_permutation)))
					{
						return FALSE;
					}
				}
			}
		}
	}
	return TRUE;
}

/* the last loaded pack let go (unused: pack_unused), at the top of a frame:
its pixels and samples out of the caches, its buffers deregistered, the
table before it put back, its tag cache room and file range given back,
its slot freed (wanted again, it is loaded again from its file) */
static void pack_unload(short slot_index)
{
	struct scene_pack_slot *slot = &scene_packs.packs[slot_index];
	unsigned char *instances;
	long count, index;

	instances = scene_pack_instances(slot->tags, &count);
	for (index = 0; index < count; index++)
	{
		unsigned char const *instance = instances + index * TAG_INSTANCE_BYTES;
		unsigned long group_tag = *(unsigned long const *)instance;
		long tag_index = (long)*(unsigned long const *)(instance + 12);

		if (group_tag == BITMAP_GROUP_TAG)
		{
			struct bitmap_group *group = bitmap_group_get(tag_index);
			long bitmap;

			for (bitmap = 0; bitmap < group->bitmaps.count; bitmap++)
				texture_cache_bitmap_delete(TAG_BLOCK_GET_ELEMENT(&group->bitmaps, bitmap, struct bitmap_data));
		}
		else if (group_tag == SOUND_DEFINITION_TAG)
		{
			struct sound_definition *sound = sound_definition_get(tag_index);
			long range, permutation;

			for (range = 0; range < sound->pitch_ranges.count; range++)
			{
				struct sound_pitch_range *pitch_range = TAG_BLOCK_GET_ELEMENT(&sound->pitch_ranges, range,
					struct sound_pitch_range);

				for (permutation = 0; permutation < pitch_range->permutations.count; permutation++)
					sound_cache_sound_delete(TAG_BLOCK_GET_ELEMENT(&pitch_range->permutations, permutation,
						struct sound_permutation));
			}
		}
	}
	tags_header_deregister_vertex_and_index_buffers((struct cache_file_tag_header *)slot->tags);
	/* (the table before it: the one made for the pack loaded before, or the
	map's own) */
	scene_packs.table_count--;
	cache_files_set_tag_instances(scene_packs.table_count ? scene_packs.tables[scene_packs.table_count - 1] :
		scene_packs.original_instances, slot->first_index);
	free(scene_packs.tables[scene_packs.table_count]);
	scene_packs.tables[scene_packs.table_count] = NULL;
	if (!scene_packs.table_count)
	{
		scene_packs.original_instances = NULL;
		scene_packs.original_count = 0;
	}
	while (__atomic_exchange_n(&scene_packs.read_lock, 1, __ATOMIC_ACQUIRE))
		SwitchToThread();
	if (slot->file)
		fclose(slot->file);
	slot->file = NULL;
	__atomic_store_n(&scene_packs.read_lock, 0, __ATOMIC_RELEASE);
	scene_packs.reserved -= slot->bytes;
	scene_packs.file_used = slot->file_base;
	tag_validate_reserve_after_tags(scene_packs.reserved);
	csmemset(slot->tags, 0xCD, slot->bytes);
	platform_log("scene packs: %s: let go (%lu KB of the tag cache given back, %lu KB held by packs)", slot->name,
		slot->bytes / 1024, scene_packs.reserved / 1024);
	if (slot->recipe)
		free(slot->recipe);
	memmove(slot, slot + 1, (size_t)(scene_packs.pack_count - slot_index - 1) * sizeof(*slot));
	scene_packs.pack_count--;
	memset(&scene_packs.packs[scene_packs.pack_count], 0, sizeof(*slot));
}

/* the packs asked to go let go, the last loaded first, while the last one
loaded is one of them and unused */
static void packs_release(void)
{
	for (;;)
	{
		short index, last = NONE;

		for (index = 0; index < scene_packs.pack_count; index++)
		{
			struct scene_pack_slot const *slot = &scene_packs.packs[index];

			if (state_get(slot) == _scene_pack_state_loaded &&
				(last == NONE || slot->first_index > scene_packs.packs[last].first_index))
			{
				last = index;
			}
		}
		if (last == NONE || !scene_packs.packs[last].release_wanted || !pack_unused(&scene_packs.packs[last]))
			return;
		pack_unload(last);
	}
}

/* ---------- public code */

short scene_packs_want(
	char const *name)
{
	struct scene_pack_slot *slot;
	long count;

	if (!scene_packs_enabled() || !name || !*name || cache_file_tag_data_size() <= 0 ||
		custom_edition_cache_tags_loaded() || !cache_files_tag_instances(&count))
	{
		return _scene_pack_state_none;
	}
	slot = slot_find(name);
	if (!slot)
	{
		slot = slot_new(name);
		if (!slot)
			return _scene_pack_state_failed;
		build_thread_start();
	}
	/* (one built only, wanted now: loaded as any; one asked to go kept) */
	if (!slot_wanted_loaded(slot) || !slot->wanted_us)
	{
		slot->wanted_us = vita_host_time_us();
		/* (a prebuild under way that is not building this one stops to make
		way for it; one that is, goes on at the pace of a pack wanted now) */
		if (scene_packs.thread_running && scene_packs.job.low && state_get(slot) != _scene_pack_state_loaded)
		{
			short index;
			boolean in_job = FALSE;

			for (index = 0; index < scene_packs.job.slot_count; index++)
				in_job |= &scene_packs.packs[scene_packs.job.slots[index]] == slot;
			if (in_job)
			{
				scene_packs.job.low = FALSE;
			}
			else
			{
				scene_packs.job.preempted = TRUE;
				scene_packs.cancel = TRUE;
			}
		}
	}
	slot->build_only = FALSE;
	slot->release_wanted = FALSE;
	return state_get(slot);
}

void scene_packs_release(
	char const *name)
{
	struct scene_pack_slot *slot = name ? slot_find(name) : NULL;

	if (slot && !slot->release_wanted)
	{
		slot->release_wanted = TRUE;
		/* (one not loaded yet goes at once: never loaded) */
		if (state_get(slot) == _scene_pack_state_ready)
			slot->build_only = TRUE;
	}
}

short scene_packs_prebuild(
	char const *name)
{
	struct scene_pack_slot *slot;
	long count;

	if (!scene_packs_enabled() || !name || !*name || cache_file_tag_data_size() <= 0 ||
		custom_edition_cache_tags_loaded() || !cache_files_tag_instances(&count))
	{
		return _scene_pack_state_none;
	}
	slot = slot_find(name);
	if (!slot)
	{
		slot = slot_new(name);
		if (!slot)
			return _scene_pack_state_failed;
		slot->build_only = TRUE;
		/* (its job begun at the top of the next frame, scene_packs_update,
		with every prebuild asked for meanwhile: those sharing a map's read
		share it) */
	}
	return state_get(slot);
}

short scene_packs_status(
	char const *name,
	long *progress,
	char *why,
	long why_size)
{
	struct scene_pack_slot *slot = name ? slot_find(name) : NULL;
	short state;

	if (progress)
		*progress = 0;
	if (why && why_size > 0)
		why[0] = 0;
	if (!slot)
		return _scene_pack_state_none;
	state = state_get(slot);
	if (progress)
	{
		long volatile const *building = slot->progress;

		*progress = state == _scene_pack_state_building ? (building ? PIN(*building, 0, 99) : 0) :
			state == _scene_pack_state_failed ? 0 : 100;
	}
	if (why && why_size > 0 && state == _scene_pack_state_failed)
		snprintf(why, (size_t)why_size, "%s", slot->why);
	return state;
}

char const *scene_packs_owner(
	long tag_index)
{
	short index;
	long absolute = tag_index & 0xFFFF;

	if (tag_index == NONE)
		return NULL;
	for (index = 0; index < scene_packs.pack_count; index++)
	{
		struct scene_pack_slot const *slot = &scene_packs.packs[index];

		if (state_get(slot) == _scene_pack_state_loaded && absolute >= slot->first_index &&
			absolute < slot->first_index + slot->count)
		{
			return slot->name;
		}
	}
	return NULL;
}

/* a recipe read and parsed (app0:packs/<name>.txt), into recipe: FALSE if
it cannot be */
static boolean recipe_read(char const *name, struct scene_pack_recipe *recipe)
{
	unsigned char *text;
	unsigned long size = 0;
	char why[SCENE_PACK_WHY_SIZE];
	boolean parsed;
	boolean made;

	if (!name || strlen(name) >= SCENE_PACK_NAME_SIZE)
		return FALSE;
	text = recipe_text(name, &size, &made);
	if (!text)
		return FALSE;
	parsed = scene_pack_recipe_parse((char const *)text, (long)size, recipe, why, sizeof(why)) && !strcmp(recipe->name, name);
	recipe_text_free(text, made);
	return parsed;
}

unsigned long long scene_packs_recipe_hash(
	char const *name)
{
	struct scene_pack_slot *slot = name ? slot_find(name) : NULL;
	struct scene_pack_recipe *recipe;
	unsigned long long hash = 0;

	if (slot && slot->recipe && state_get(slot) != _scene_pack_state_failed)
		return slot->recipe->hash;
	recipe = malloc(sizeof(*recipe));
	if (recipe && recipe_read(name, recipe))
		hash = recipe->hash;
	if (recipe)
		free(recipe);
	return hash;
}

boolean scene_packs_recipe_available(
	char const *name,
	char *why,
	long why_size)
{
	struct scene_pack_recipe *recipe = malloc(sizeof(*recipe));
	boolean available = FALSE;

	if (why && why_size > 0)
		why[0] = 0;
	if (recipe && recipe_read(name, recipe))
		available = scene_pack_source_check(recipe, cache_files_map_directory(), NULL, 0, NULL, why, why_size) &&
			!recipe_turned_off(recipe, why, why_size);
	else if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "its recipe cannot be read");
	if (recipe)
		free(recipe);
	return available;
}

boolean scene_packs_recipe_covered(
	char const *name)
{
	struct scene_pack_recipe *recipe = malloc(sizeof(*recipe));
	boolean covered = FALSE;
	short index;

	if (recipe && recipe_read(name, recipe) && recipe->root_count > 0)
	{
		covered = TRUE;
		for (index = 0; index < recipe->root_count; index++)
		{
			if (scene_pack_recipe_root_is_multiplayer_unit(&recipe->roots[index]) ||
				tag_loaded((long)recipe->roots[index].group_tag, recipe->roots[index].path) == NONE)
			{
				covered = FALSE;
			}
		}
	}
	if (recipe)
		free(recipe);
	return covered;
}

boolean scene_packs_load_now(
	char const *name)
{
	struct scene_pack_slot *slot;
	short state = scene_packs_want(name);
	short tries;

	if (state == _scene_pack_state_none || !(slot = slot_find(name)))
		return FALSE;
	/* (here, now: the worker's job finished first - one that is not this
	slot's stopped -, then this one's build and load on this thread) */
	for (tries = 0; tries < 3 && (state == _scene_pack_state_building || state == _scene_pack_state_ready); tries++)
	{
		unsigned char *room, *room_end;
		short index;
		boolean in_job = scene_packs.job.load != NONE && &scene_packs.packs[scene_packs.job.load] == slot;

		for (index = 0; index < scene_packs.job.slot_count; index++)
			in_job |= &scene_packs.packs[scene_packs.job.slots[index]] == slot;
		if (scene_packs.thread_running)
		{
			if (!in_job)
			{
				scene_packs.job.preempted = TRUE;
				scene_packs.cancel = TRUE;
			}
			scene_packs.job.low = FALSE;
			job_finish();
		}
		state = state_get(slot);
		if (state == _scene_pack_state_loaded || state == _scene_pack_state_failed)
			break;
		room = tag_cache_room(&room_end);
		if (state == _scene_pack_state_building)
		{
			struct scene_pack_slot *slots[1];

			slots[0] = slot;
			packs_prepare(slots, 1, room, (unsigned long)(room_end - room), TRUE, FALSE, NULL);
		}
		if (state_get(slot) != _scene_pack_state_ready)
			break;
		table_next(&scene_packs.job.first_index, &scene_packs.job.salt, &scene_packs.job.file_base,
			&scene_packs.job.table_room, &scene_packs.job.file_room);
		slot->full = FALSE;
		pack_stage(slot, room, (unsigned long)(room_end - room));
		if (slot->staged)
			pack_commit(slot);
		else if (slot->refused)
			slot_refused(slot);
		else
			break;
		state = state_get(slot);
	}
	build_thread_start();
	return state_get(slot) == _scene_pack_state_loaded;
}

boolean scene_packs_textures_ready(
	char const *name)
{
	void *_texture_cache_bitmap_get_hardware_format(struct bitmap_data *bitmap, boolean block, boolean load);
	struct scene_pack_slot *slot = name ? slot_find(name) : NULL;
	unsigned char *instances;
	long count, index;
	boolean ready = TRUE;

	if (!slot || state_get(slot) != _scene_pack_state_loaded)
		return FALSE;
	instances = scene_pack_instances(slot->tags, &count);
	for (index = 0; index < count; index++)
	{
		unsigned char const *instance = instances + index * TAG_INSTANCE_BYTES;

		if (*(unsigned long const *)instance == BITMAP_GROUP_TAG)
		{
			struct bitmap_group *group = bitmap_group_get((long)*(unsigned long const *)(instance + 12));
			long bitmap;

			/* (each one's read begun, if it was not: none waited for) */
			for (bitmap = 0; bitmap < group->bitmaps.count; bitmap++)
			{
				struct bitmap_data *data = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, bitmap, struct bitmap_data);

				if (data->pixels_size > 0 && !_texture_cache_bitmap_get_hardware_format(data, FALSE, TRUE))
					ready = FALSE;
			}
		}
	}
	return ready;
}

long scene_packs_tag(
	char const *name,
	unsigned long group_tag,
	char const *path)
{
	struct scene_pack_slot *slot = name ? slot_find(name) : NULL;
	long index, count;

	if (!slot || state_get(slot) != _scene_pack_state_loaded)
		return NONE;
	index = scene_pack_find(slot->tags, group_tag, path);
	if (index == NONE)
		return NONE;
	return (long)*(unsigned long *)((unsigned char *)scene_pack_instances(slot->tags, &count) +
		index * TAG_INSTANCE_BYTES + 12);
}

short scene_packs_roots(
	char const *name,
	long *tag_indices,
	short most)
{
	struct scene_pack_slot *slot = name ? slot_find(name) : NULL;
	unsigned char *instances;
	long count;
	short index;

	if (!slot || state_get(slot) != _scene_pack_state_loaded)
		return 0;
	instances = scene_pack_instances(slot->tags, &count);
	for (index = 0; index < most && index < slot->info.root_count && index < count; index++)
		tag_indices[index] = (long)*(unsigned long *)(instances + index * TAG_INSTANCE_BYTES + 12);
	return index;
}

static void scene_packs_update_internal(void);

void scene_packs_update(
	void)
{
	unsigned long long started = vita_host_time_us();

	if (!scene_pack_io_hook)
	{
		scene_pack_io_hook = worker_io;
		scene_pack_copy_source = &scene_packs_copy_source;
		scene_packs.job.load = NONE;
	}
	scene_packs_update_internal();
	game_thread_time("the frame's update", NULL, started);
}

static void scene_packs_update_internal(
	void)
{
	int cache_files_reads_pending(void);
	boolean copying = cache_files_precache_in_progress();
	boolean network = game_connection() != _game_connection_local;

	if (!scene_packs.pack_count)
		return;
	/* (the worker's pauses: a map copied to the cache partition, a network
	game; a prebuild's also away from the main menu at rest) */
	__atomic_store_n(&scene_packs.pause_builds, copying || network, __ATOMIC_RELEASE);
	__atomic_store_n(&scene_packs.pause_prebuilds, copying || network || !halo_main_menu_idle(), __ATOMIC_RELEASE);
	if (scene_packs.thread_running && __atomic_load_n(&scene_packs.job.done, __ATOMIC_ACQUIRE))
		job_finish();
	/* (a build takes the free room for its scratch: loads and releases
	wait for it) */
	if (scene_packs.thread_running)
		return;
	packs_release();
	build_thread_start();
}

void scene_packs_unloaded(
	void)
{
	short index;

	build_thread_join(TRUE);
	scene_packs_epoch++;
	for (index = 0; index < scene_packs.pack_count; index++)
	{
		struct scene_pack_slot *slot = &scene_packs.packs[index];

		pack_unstage(slot);
		if (state_get(slot) == _scene_pack_state_loaded)
			tags_header_deregister_vertex_and_index_buffers((struct cache_file_tag_header *)slot->tags);
		while (__atomic_exchange_n(&scene_packs.read_lock, 1, __ATOMIC_ACQUIRE))
			SwitchToThread();
		if (slot->file)
			fclose(slot->file);
		slot->file = NULL;
		__atomic_store_n(&scene_packs.read_lock, 0, __ATOMIC_RELEASE);
		if (slot->recipe)
			free(slot->recipe);
	}
	if (scene_packs.original_instances)
		cache_files_set_tag_instances(scene_packs.original_instances, scene_packs.original_count);
	for (index = 0; index < scene_packs.table_count; index++)
		free(scene_packs.tables[index]);
	if (scene_packs.pack_count)
		platform_log("scene packs: the map's %d packs gone (%lu KB of the tag cache given back)", scene_packs.pack_count,
			scene_packs.reserved / 1024);
	memset(&scene_packs.packs, 0, sizeof(scene_packs.packs));
	scene_packs.pack_count = 0;
	scene_packs.reserved = 0;
	scene_packs.file_used = 0;
	scene_packs.original_instances = NULL;
	scene_packs.original_count = 0;
	scene_packs.table_count = 0;
	scene_packs.next_salt = 0;
	scene_packs.job.load = NONE;
	scene_packs.job.slot_count = 0;
	tag_validate_reserve_after_tags(0);
}

unsigned long scene_packs_map_epoch(
	void)
{
	return scene_packs_epoch;
}

unsigned long scene_packs_reserved_bytes(
	void)
{
	return scene_packs.reserved;
}

boolean scene_packs_offset(
	long offset)
{
	return (unsigned long)offset >= SCENE_PACK_FILE_BASE &&
		(unsigned long)offset - SCENE_PACK_FILE_BASE < SCENE_PACK_FILE_RANGE_BYTES;
}

boolean scene_packs_read(
	long offset,
	long size,
	void *buffer)
{
	unsigned long relative = (unsigned long)offset - SCENE_PACK_FILE_BASE;
	boolean read = FALSE;
	short index;

	if ((unsigned long)offset < SCENE_PACK_FILE_BASE || relative >= SCENE_PACK_FILE_RANGE_BYTES)
		return FALSE;
	if (size > 0)
		memory_watch_prepare_write(buffer, (unsigned long)size);
	if (size > 0)
		slow_io_pace((unsigned long)size, FALSE);
	while (__atomic_exchange_n(&scene_packs.read_lock, 1, __ATOMIC_ACQUIRE))
		SwitchToThread();
	for (index = 0; index < scene_packs.pack_count; index++)
	{
		struct scene_pack_slot *slot = &scene_packs.packs[index];

		unsigned long local = relative - slot->file_base;

		if (state_get(slot) == _scene_pack_state_loaded && slot->file && size >= 0 && relative >= slot->file_base &&
			local <= slot->info.data_size && (unsigned long)size <= slot->info.data_size - local)
		{
			read = !fseek(slot->file, (long)(slot->info.data_offset + local), SEEK_SET) &&
				fread(buffer, 1, (size_t)size, slot->file) == (size_t)size;
			break;
		}
	}
	__atomic_store_n(&scene_packs.read_lock, 0, __ATOMIC_RELEASE);
	if (!read && size > 0)
	{
		platform_log("scene packs: cannot read %ld bytes at %08lx", size, (unsigned long)offset);
		csmemset(buffer, 0, size);
	}
	return TRUE;
}

/* the console's (hs.c, init.txt, HALO_TEST_COMMANDS):
  pack want <name>    wanted with this map (built on a thread, loaded at a
                      frame's top)
  pack load <name>    loaded now (built here first if it must be)
  pack list           every pack's state, and a loaded one's tags' range
Each logs what it did ("scene packs: ..."). */
boolean scene_packs_command(
	char const *arguments)
{
	char word[16];
	char name[SCENE_PACK_NAME_SIZE];
	short index;

	word[0] = name[0] = 0;
	if (sscanf(arguments, "%15s %31s", word, name) < 1)
		return FALSE;
	if (!strcmp(word, "want") && name[0])
	{
		platform_log("scene packs: %s wanted: %s", name, scene_packs_state_name(scene_packs_want(name)));
		return TRUE;
	}
	if (!strcmp(word, "load") && name[0])
	{
		boolean loaded = scene_packs_load_now(name);
		struct scene_pack_slot *slot = slot_find(name);

		platform_log("scene packs: %s %s%s%s", name, loaded ? "loaded" : "not loaded", loaded || !slot ? "" : ": ",
			loaded || !slot ? "" : slot->why);
		return TRUE;
	}
	/* (tests) "pack spawn <name> [<tag index of its recipe>] [ride]": that
	tag of a loaded pack (a unit, a vehicle, an item) made 3 world units in
	front of the first local player, a vehicle ridden at once with ride */
	if (!strcmp(word, "spawn") && name[0])
	{
		long roots[SCENE_PACK_MAXIMUM_ROOTS];
		int root = 0;
		char ride[8];
		short count = scene_packs_roots(name, roots, SCENE_PACK_MAXIMUM_ROOTS);
		long player_index = local_player_get_player_index(0);
		struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
		struct object_datum *unit = player && player->unit_index != NONE ? object_try_and_get(player->unit_index) : NULL;
		struct object_placement_data placement;
		long object_index;

		ride[0] = 0;
		sscanf(arguments, "%*s %*s %d %7s", &root, ride);
		if (root < 0 || root >= count || !unit)
		{
			platform_log("scene packs: spawn %s %d: not loaded, no such tag, or no player", name, root);
			return TRUE;
		}
		object_placement_data_new(&placement, roots[root], NONE);
		SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
		placement.forward = unit->object.forward;
		placement.forward.k = 0.f;
		normalize3d(&placement.forward);
		placement.up = *global_up3d;
		placement.position.x = unit->object.position.x + placement.forward.i * 3.f;
		placement.position.y = unit->object.position.y + placement.forward.j * 3.f;
		placement.position.z = unit->object.position.z + 0.5f;
		object_index = object_new(&placement);
		platform_log("scene packs: spawn %s: %s at %.2f %.2f %.2f: %s", name, tag_get_name(roots[root]),
			placement.position.x, placement.position.y, placement.position.z, object_index != NONE ? "made" : "not made");
		if (object_index != NONE && !strcmp(ride, "ride"))
		{
			boolean entered = unit_enter_seat(player->unit_index, object_index, 0);

			platform_log("scene packs: spawn %s: the player %s its seat 0", name, entered ? "is in" : "could not enter");
		}
		return TRUE;
	}
	/* (tests) "pack where": the first local player's unit, the vehicle it
	is in, where, and its vitality */
	if (!strcmp(word, "where"))
	{
		long player_index = local_player_get_player_index(0);
		struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
		struct object_datum *unit = player && player->unit_index != NONE ? object_try_and_get(player->unit_index) : NULL;
		struct object_datum *vehicle = unit && unit->object.parent_object_index != NONE ?
			object_try_and_get(unit->object.parent_object_index) : NULL;

		if (!unit)
		{
			platform_log("scene packs: where: no player");
			return TRUE;
		}
		platform_log("scene packs: where: the player at %.2f %.2f %.2f, body %.2f shield %.2f, in %s%s%s", unit->object.position.x,
			unit->object.position.y, unit->object.position.z, unit->object.body_vitality, unit->object.shield_vitality,
			vehicle ? tag_get_name(vehicle->definition_index) : "no vehicle", vehicle ? " at " : "", "");
		if (vehicle)
			platform_log("scene packs: where: the vehicle at %.2f %.2f %.2f, velocity %.2f %.2f %.2f", vehicle->object.position.x,
				vehicle->object.position.y, vehicle->object.position.z, vehicle->object.translational_velocity.i,
				vehicle->object.translational_velocity.j, vehicle->object.translational_velocity.k);
		return TRUE;
	}
	/* (tests) "pack ride": the first local player into seat 0 of the nearest
	vehicle */
	if (!strcmp(word, "ride"))
	{
		long player_index = local_player_get_player_index(0);
		struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
		struct object_datum *unit = player && player->unit_index != NONE ? object_try_and_get(player->unit_index) : NULL;
		struct object_iterator iterator;
		struct object_datum *object;
		long nearest = NONE;
		real nearest_distance = 1e9f;

		if (!unit)
			return TRUE;
		object_iterator_new(&iterator, _object_mask_vehicle, 0);
		while ((object = object_iterator_next(&iterator)) != NULL)
		{
			real distance = distance_squared3d(&object->object.position, &unit->object.position);

			if (distance < nearest_distance)
			{
				nearest_distance = distance;
				nearest = iterator.index;
			}
		}
		platform_log("scene packs: ride: %s %s", nearest != NONE ? tag_get_name(object_get(nearest)->definition_index) : "no vehicle",
			nearest != NONE && unit_enter_seat(player->unit_index, nearest, 0) ? "entered" : "not entered");
		return TRUE;
	}
	/* (tests) "pack board <count>": that many bipeds of the map's own
	multiplayer unit made beside the first local player's vehicle and put in
	its seats after the driver's (passengers); "pack riders": the units in
	the player's vehicle, by seat, and where */
	if (!strcmp(word, "board") || !strcmp(word, "riders"))
	{
		long player_index = local_player_get_player_index(0);
		struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
		struct object_datum *unit = player && player->unit_index != NONE ? object_try_and_get(player->unit_index) : NULL;
		long vehicle_index = unit ? unit->object.parent_object_index : NONE;
		struct object_datum *vehicle = vehicle_index != NONE ? object_try_and_get(vehicle_index) : NULL;

		if (!vehicle)
		{
			platform_log("scene packs: %s: the player is in no vehicle", word);
			return TRUE;
		}
		if (!strcmp(word, "board"))
		{
			struct game_globals *globals = scenario_get_game_globals();
			struct game_globals_multiplayer_information *information = globals && globals->multiplayer_information.count ?
				TAG_BLOCK_GET_ELEMENT(&globals->multiplayer_information, 0, struct game_globals_multiplayer_information) : NULL;
			long biped_tag = information ? information->unit.index : NONE;
			int wanted = atoi(name), made = 0;
			short seat;
			struct unit_definition *definition = (struct unit_definition *)tag_get('unit', vehicle->definition_index);

			for (seat = 1; biped_tag != NONE && definition && seat < definition->unit.seats.count && made < wanted; seat++)
			{
				struct object_placement_data placement;
				long biped_index;

				object_placement_data_new(&placement, biped_tag, NONE);
				placement.position = vehicle->object.position;
				placement.position.z += 2.f + (real)made;
				biped_index = object_new(&placement);
				if (biped_index == NONE)
					break;
				if (unit_enter_seat(biped_index, vehicle_index, seat))
				{
					made++;
					platform_log("scene packs: board: a passenger in seat %d", seat);
				}
				else
				{
					object_delete(biped_index);
				}
			}
			platform_log("scene packs: board: %d passengers aboard %s", made, tag_get_name(vehicle->definition_index));
		}
		else
		{
			struct object_iterator iterator;
			struct object_datum *object;
			long riders = 0;

			object_iterator_new(&iterator, _object_mask_unit, 0);
			while ((object = object_iterator_next(&iterator)) != NULL)
			{
				struct unit_datum *rider = (struct unit_datum *)object;

				if (object->object.parent_object_index == vehicle_index && iterator.index != vehicle_index)
				{
					riders++;
					/* (a rider's place is its vehicle's frame's) */
					platform_log("scene packs: riders: seat %d: %s%s at %.2f %.2f %.2f in it", rider->unit.parent_seat_index,
						iterator.index == player->unit_index ? "the player, " : "", tag_get_name(object->definition_index),
						object->object.position.x, object->object.position.y, object->object.position.z);
				}
			}
			platform_log("scene packs: riders: %ld in %s at %.2f %.2f %.2f", riders, tag_get_name(vehicle->definition_index),
				vehicle->object.position.x, vehicle->object.position.y, vehicle->object.position.z);
		}
		return TRUE;
	}
	/* (tests) "pack census": projectiles, units alive and dead, vehicles */
	if (!strcmp(word, "census"))
	{
		struct object_iterator iterator;
		struct object_datum *object;
		long projectiles = 0, alive = 0, dead = 0, vehicles = 0;

		object_iterator_new(&iterator, _object_mask_projectile, 0);
		while (object_iterator_next(&iterator))
			projectiles++;
		object_iterator_new(&iterator, _object_mask_unit, 0);
		while ((object = object_iterator_next(&iterator)) != NULL)
		{
			if (object->object.type == _object_type_vehicle)
				vehicles++;
			else if (TEST_FLAG(object->object.damage_flags, _object_dead_bit))
				dead++;
			else
				alive++;
		}
		platform_log("scene packs: census: %ld projectiles, %ld characters alive, %ld dead, %ld vehicles", projectiles, alive,
			dead, vehicles);
		return TRUE;
	}
	/* (tests) "pack preview [colour|off]": the Customization screen's preview
	(menu_scene_objects.c) */
	if (!strcmp(word, "preview"))
	{
		if (!strcmp(name, "off"))
			menu_scene_spartan_hide();
		else if (name[0] && !strcmp(name, "camera"))
			platform_log("scene packs: preview: %s", menu_scene_spartan_show("multiplayer -40 -1.8 15.5 0 0", "1.6 -0.3 -0.4 -10",
				NONE) ? "shown" : "no scene");
		else if (name[0] && menu_scene_spartan_show(NULL, NULL, (short)atoi(name)) == FALSE)
			menu_scene_spartan_color((short)atoi(name));
		else if (!name[0])
			platform_log("scene packs: preview: %s", menu_scene_spartan_show(NULL, NULL, NONE) ? "shown" : "no scene");
		return TRUE;
	}
	if (!strcmp(word, "list"))
	{
		for (index = 0; index < scene_packs.pack_count; index++)
		{
			struct scene_pack_slot const *slot = &scene_packs.packs[index];

			platform_log("scene packs: %s: %s, %ld tags from %ld, %lu KB of the tag cache%s%s", slot->name,
				scene_packs_state_name(state_get(slot)), slot->count, slot->first_index, slot->bytes / 1024,
				slot->why[0] ? "; " : "", slot->why);
		}
		platform_log("scene packs: %d packs, %lu KB of the tag cache held", scene_packs.pack_count,
			scene_packs.reserved / 1024);
		return TRUE;
	}
	return FALSE;
}
