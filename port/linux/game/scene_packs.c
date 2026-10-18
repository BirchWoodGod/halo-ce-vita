/*
SCENE_PACKS.C

Scene packs in the running game (scene_packs.h, which says how they are
wanted and used; the pack itself, its builder and checks: scene_pack.c).

Where a pack is: its recipe in app0:packs/<name>.txt (halo_packs_file_read),
its file in t:\packs\<name>.pack (the save root's t folder: on the Vita
ux0:data/haloce-vita/saves/t/packs). A pack file is used only if its recipe's
hash, its builder's version and its source map's (the first of the recipe's
maps in the maps folder: its header and size) are what they are now;
otherwise, or if it is missing, it is built again, on a thread of its own
(one build at a time), with the tag cache's free room as the builder's
scratch for the source map's tag data (a multiplayer map's 7 MB would not fit
the Vita's C heap beside a session's): from the top of the room between the
map's tags (and the packs loaded) and the lowest structure bsp's place.

Loading, at the top of a frame (scene_packs_update, main.c, as the tick
does not run): the pack's tags read into the tag cache right after the
map's tags and the packs loaded before it, checked (scene_pack_load: a pack
that fails is deleted and built once more, then given up), given tag indices
after the table's (the map's, then the menus' tags: menu_tags.c, which
grew it first), its pixels' and samples' file offsets moved to its own
range, its vertex and index buffers registered as the map's are, then the
table (a copy with the pack's tags after it) put in place, the table first,
then its count (cache_files_set_tag_instances). The bytes the packs hold
after the map's tags are reserved: a structure bsp's load does not clear
them, its check does not take them for scratch (cache_files.c,
tag_validate.c), and a bsp whose place would overlap them is refused.

Unloading (scenario_tags_unload, before the menus' tags and the map's go):
a build stopped and waited for, every pack's buffers deregistered and file
closed, the table before the packs put back, the tables made freed.

Reads (scene_packs_read, cache_files_windows.c): a file offset from
SCENE_PACK_FILE_BASE is a pack's (MAXIMUM_LOADED_SCENE_PACKS ranges of
SCENE_PACK_RANGE_BYTES), served from its file at once, under a lock (the
render's and the tick's threads both read), zeros when the file cannot be
read.

HALO_SCENE_PACKS=0 turns them off (nothing is wanted, built or loaded).
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries_windows.h"
#include "cache/cache_files.h"
#include "cache/physical_memory_map.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "objects/object_types.h"

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

/* ---------- constants */

enum
{
	/* each loaded pack's file offsets (SCENE_PACK_FILE_BASE + its slot's) */
	SCENE_PACK_RANGE_BYTES = 0x04000000,
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
	short range;
	FILE *file;
	char why[SCENE_PACK_WHY_SIZE];
	/* (Forge's lobby: scene_packs_prebuild) built, not loaded */
	boolean build_only;
};

/* ---------- globals */

static struct
{
	struct scene_pack_slot packs[MAXIMUM_SCENE_PACK_SLOTS];
	short pack_count;
	/* the tag cache's bytes after the map's tags the loaded packs hold */
	unsigned long reserved;
	/* the table before the packs (the map's and the menus' tags), and the
	ones made since */
	void *original_instances;
	long original_count;
	void *tables[MAXIMUM_LOADED_SCENE_PACKS];
	short table_count;
	/* the build thread: the slot it builds (NONE), its scratch in the tag
	cache, whether it should stop */
	pthread_t thread;
	boolean thread_running;
	short building;
	unsigned char *scratch;
	unsigned long scratch_size;
	int volatile cancel;
	int volatile read_lock;
} scene_packs = { { { 0 } }, 0, 0, NULL, 0, { NULL }, 0, 0, FALSE, NONE };

/* (each map's tags loaded counts one: a character made on another map
is gone with it, menu_scene_objects.c) */
static unsigned long scene_packs_epoch = 1;

/* ---------- private code */

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
	if (!scene_pack_source_find(slot->recipe, cache_files_map_directory(), map, sizeof(map), &source_hash) ||
		strcmp(map, slot->info.source_map) || source_hash != slot->info.source_hash)
	{
		snprintf(slot->why, sizeof(slot->why), "its source map changed");
		return FALSE;
	}
	return TRUE;
}

/* (the build thread, or the caller of scene_packs_load_now) the pack
checked, built if it must be: TRUE if it may be loaded */
static boolean pack_prepare(struct scene_pack_slot *slot, unsigned char *scratch, unsigned long scratch_size)
{
	if (pack_current(slot))
		return TRUE;
	platform_log("scene packs: %s: building it (%s)", slot->name, slot->why);
	CreateDirectoryA(SCENE_PACKS_FOLDER, NULL);
	if (!scene_pack_build(slot->recipe, cache_files_map_directory(), slot->path, scratch, scratch_size,
		&scene_packs.cancel, &slot->build))
	{
		snprintf(slot->why, sizeof(slot->why), "%s", slot->build.why);
		platform_log("scene packs: %s: not built: %s", slot->name, slot->why);
		return FALSE;
	}
	slot->built = TRUE;
	platform_log("scene packs: %s: built from %s in %.1f ms: %ld tags (%ld references left out), %lu KB of tags, "
		"%lu KB of pixels and samples, %lu KB file (%.1f MB of the map read)", slot->name, slot->build.map,
		(double)slot->build.microseconds / 1000.0, slot->build.tag_count, slot->build.stripped_count,
		slot->build.tag_bytes / 1024, slot->build.data_bytes / 1024, slot->build.file_bytes / 1024,
		(double)slot->build.source_bytes / (1024.0 * 1024.0));
	if (!pack_current(slot))
	{
		platform_log("scene packs: %s: the pack built cannot be opened: %s", slot->name, slot->why);
		return FALSE;
	}
	return TRUE;
}

static void *build_thread(void *context)
{
	struct scene_pack_slot *slot = context;

	state_set(slot, pack_prepare(slot, scene_packs.scratch, scene_packs.scratch_size) ? _scene_pack_state_ready :
		_scene_pack_state_failed);
	slot->ready_us = vita_host_time_us();
	return NULL;
}

/* the build thread waited for (stopped first if stop) */
static void build_thread_join(boolean stop)
{
	if (!scene_packs.thread_running)
		return;
	if (stop)
		scene_packs.cancel = TRUE;
	pthread_join(scene_packs.thread, NULL);
	/* (its scratch the free room's again) */
	if (scene_packs.scratch && scene_packs.scratch_size)
		csmemset(scene_packs.scratch, 0xCD, scene_packs.scratch_size);
	scene_packs.thread_running = FALSE;
	scene_packs.building = NONE;
	scene_packs.cancel = FALSE;
	scene_packs.scratch = NULL;
	scene_packs.scratch_size = 0;
}

/* a slot waiting to be built given the thread, if it is free */
static void build_thread_start(void)
{
	short index;
	pthread_attr_t attributes;
	unsigned char *room, *room_end;

	if (scene_packs.thread_running)
	{
		struct scene_pack_slot *slot = &scene_packs.packs[scene_packs.building];

		if (state_get(slot) == _scene_pack_state_building)
			return;
		build_thread_join(FALSE);
	}
	for (index = 0; index < scene_packs.pack_count; index++)
	{
		if (state_get(&scene_packs.packs[index]) == _scene_pack_state_building)
			break;
	}
	if (index == scene_packs.pack_count)
		return;
	room = tag_cache_room(&room_end);
	scene_packs.scratch = room;
	scene_packs.scratch_size = (unsigned long)(room_end - room);
	scene_packs.building = index;
	scene_packs.cancel = FALSE;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, BUILD_THREAD_STACK_BYTES);
	if (pthread_create(&scene_packs.thread, &attributes, build_thread, &scene_packs.packs[index]) == 0)
	{
		scene_packs.thread_running = TRUE;
	}
	else
	{
		scene_packs.building = NONE;
		snprintf(scene_packs.packs[index].why, sizeof(scene_packs.packs[index].why), "no thread to build it");
		state_set(&scene_packs.packs[index], _scene_pack_state_failed);
	}
	pthread_attr_destroy(&attributes);
}

/* the pack's tags put in the game (the thread is not building: its scratch
is free): TRUE if it is loaded */
static boolean pack_load(struct scene_pack_slot *slot)
{
	unsigned char *room, *room_end;
	unsigned long long started = vita_host_time_us();
	unsigned long heap_before = 0, heap_after = 0, capacity = 0;
	void *existing;
	unsigned char *table;
	unsigned char *instances;
	long existing_count, count, index, salt = 0;
	short range;

	platform_heap_usage(&heap_before, &capacity);
	room = tag_cache_room(&room_end);
	if (scene_packs.thread_running && scene_packs.scratch && room_end > scene_packs.scratch)
		room_end = scene_packs.scratch;
	if (slot->info.tags_size > (unsigned long)(room_end - room))
	{
		snprintf(slot->why, sizeof(slot->why), "the tag cache has %lu KB free for its %lu KB",
			(unsigned long)(room_end - room) / 1024, slot->info.tags_size / 1024);
		return FALSE;
	}
	for (range = 0; range < MAXIMUM_LOADED_SCENE_PACKS; range++)
	{
		for (index = 0; index < scene_packs.pack_count; index++)
		{
			if (state_get(&scene_packs.packs[index]) == _scene_pack_state_loaded && scene_packs.packs[index].range == range)
				break;
		}
		if (index == scene_packs.pack_count)
			break;
	}
	existing = cache_files_tag_instances(&existing_count);
	if (!existing || range == MAXIMUM_LOADED_SCENE_PACKS || scene_packs.table_count >= MAXIMUM_LOADED_SCENE_PACKS)
	{
		snprintf(slot->why, sizeof(slot->why), "no map's tags to load it beside");
		return FALSE;
	}
	if (!scene_pack_load(slot->path, &slot->info, room, slot->why, sizeof(slot->why)))
	{
		/* (whatever it left there is the free room's again) */
		csmemset(room, 0xCD, slot->info.tags_size);
		return FALSE;
	}
	instances = scene_pack_instances(room, &count);
	if (existing_count < 0 || count <= 0 || existing_count > 0x7FFF - count)
	{
		csmemset(room, 0xCD, slot->info.tags_size);
		snprintf(slot->why, sizeof(slot->why), "the map's %ld tags and its %ld are too many for a tag table",
			existing_count, count);
		return FALSE;
	}
	table = malloc((unsigned long)(existing_count + count) * TAG_INSTANCE_BYTES);
	slot->file = fopen(slot->path, "rb");
	if (!table || !slot->file)
	{
		if (table)
			free(table);
		if (slot->file)
			fclose(slot->file);
		slot->file = NULL;
		csmemset(room, 0xCD, slot->info.tags_size);
		snprintf(slot->why, sizeof(slot->why), "there is no memory for its tag table, or its file cannot be opened");
		return FALSE;
	}
	for (index = 0; index < existing_count; index++)
	{
		long instance_salt = (long)(*(unsigned long *)((unsigned char *)existing + index * TAG_INSTANCE_BYTES + 12) >> 16);

		if (instance_salt > salt)
			salt = instance_salt;
	}
	scene_pack_rebase(room, existing_count, (salt + 1) & 0x7FFF, (long)range * SCENE_PACK_RANGE_BYTES);
	memcpy(table, existing, (unsigned long)existing_count * TAG_INSTANCE_BYTES);
	memcpy(table + existing_count * TAG_INSTANCE_BYTES, instances, (unsigned long)count * TAG_INSTANCE_BYTES);
	tags_header_register_vertex_and_index_buffers((struct cache_file_tag_header *)room);
	slot->tags = room;
	slot->bytes = (slot->info.tags_size + SCENE_PACK_ALIGNMENT - 1) & ~(SCENE_PACK_ALIGNMENT - 1);
	slot->first_index = existing_count;
	slot->count = count;
	slot->range = range;
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
	platform_heap_usage(&heap_after, &capacity);
	platform_log("scene packs: %s: loaded from %s in %.1f ms: %ld tags (%ld to %ld of the table), %lu KB of the tag cache "
		"(%lu KB held by packs, %lu KB free after them), %lu KB of pixels and samples in its file, C heap %+ld KB",
		slot->name, slot->info.source_map, (double)(vita_host_time_us() - started) / 1000.0, count, existing_count,
		existing_count + count - 1, slot->bytes / 1024, scene_packs.reserved / 1024,
		(unsigned long)(room_end - room - slot->bytes) / 1024, slot->info.data_size / 1024,
		((long)heap_after - (long)heap_before) / 1024);
	return TRUE;
}

/* a slot for the pack of that recipe (its recipe read), or NULL */
static struct scene_pack_slot *slot_new(char const *name)
{
	struct scene_pack_slot *slot;
	unsigned char *text;
	unsigned long size = 0;
	char file_name[SCENE_PACK_NAME_SIZE + 8];
	char why[SCENE_PACK_WHY_SIZE];

	if (scene_packs.pack_count >= MAXIMUM_SCENE_PACK_SLOTS || !name || strlen(name) >= SCENE_PACK_NAME_SIZE)
		return NULL;
	slot = &scene_packs.packs[scene_packs.pack_count++];
	memset(slot, 0, sizeof(*slot));
	slot->range = NONE;
	snprintf(slot->name, sizeof(slot->name), "%s", name);
	snprintf(file_name, sizeof(file_name), "%s.txt", name);
	text = halo_packs_file_read(file_name, &size);
	slot->recipe = malloc(sizeof(struct scene_pack_recipe));
	if (!text || !slot->recipe || !scene_pack_recipe_parse((char const *)text, (long)size, slot->recipe, why, sizeof(why)) ||
		strcmp(slot->recipe->name, name))
	{
		snprintf(slot->why, sizeof(slot->why), "%s", !text ? "its recipe cannot be read" :
			slot->recipe && slot->recipe->name[0] && strcmp(slot->recipe->name, name) ? "its recipe is another pack's" : why);
		state_set(slot, _scene_pack_state_failed);
		platform_log("scene packs: %s: %s", name, slot->why);
	}
	else
	{
		snprintf(slot->path, sizeof(slot->path), "%s\\%s.pack", SCENE_PACKS_FOLDER, name);
		state_set(slot, _scene_pack_state_building);
	}
	if (text)
		halo_menus_file_free(text);
	return slot;
}

/* the slot's pack loaded, now that it is ready (its thread done): built
once more if its load fails */
static void slot_load(struct scene_pack_slot *slot)
{
	if (pack_load(slot))
		return;
	platform_log("scene packs: %s: refused: %s", slot->name, slot->why);
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
	/* (one built only, wanted now: loaded as any) */
	slot->build_only = FALSE;
	return state_get(slot);
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
		build_thread_start();
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
		*progress = state == _scene_pack_state_building ? PIN(slot->build.progress, 0, 99) :
			state == _scene_pack_state_failed ? 0 : 100;
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
	char file_name[SCENE_PACK_NAME_SIZE + 8];
	char why[SCENE_PACK_WHY_SIZE];
	boolean parsed;

	if (!name || strlen(name) >= SCENE_PACK_NAME_SIZE)
		return FALSE;
	snprintf(file_name, sizeof(file_name), "%s.txt", name);
	text = halo_packs_file_read(file_name, &size);
	if (!text)
		return FALSE;
	parsed = scene_pack_recipe_parse((char const *)text, (long)size, recipe, why, sizeof(why)) && !strcmp(recipe->name, name);
	halo_menus_file_free(text);
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
			if (tag_loaded((long)recipe->roots[index].group_tag, recipe->roots[index].path) == NONE)
				covered = FALSE;
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

	if (state == _scene_pack_state_none || !(slot = slot_find(name)))
		return FALSE;
	while (state == _scene_pack_state_building || state == _scene_pack_state_ready)
	{
		if (state == _scene_pack_state_building && scene_packs.thread_running &&
			&scene_packs.packs[scene_packs.building] == slot)
		{
			build_thread_join(FALSE);
		}
		else if (state == _scene_pack_state_building)
		{
			/* (another's build first: then this one's, here) */
			unsigned char *room, *room_end;

			build_thread_join(FALSE);
			room = tag_cache_room(&room_end);
			state_set(slot, pack_prepare(slot, room, (unsigned long)(room_end - room)) ? _scene_pack_state_ready :
				_scene_pack_state_failed);
		}
		if (state_get(slot) == _scene_pack_state_ready)
			slot_load(slot);
		state = state_get(slot);
	}
	build_thread_start();
	return state == _scene_pack_state_loaded;
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

void scene_packs_update(
	void)
{
	short index;

	if (!scene_packs.pack_count)
		return;
	if (scene_packs.thread_running && state_get(&scene_packs.packs[scene_packs.building]) != _scene_pack_state_building)
		build_thread_join(FALSE);
	/* (a build takes the whole free room for its scratch: loads wait) */
	if (scene_packs.thread_running)
		return;
	for (index = 0; index < scene_packs.pack_count; index++)
	{
		struct scene_pack_slot *slot = &scene_packs.packs[index];

		if (state_get(slot) == _scene_pack_state_ready && !slot->build_only)
			slot_load(slot);
	}
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
	scene_packs.original_instances = NULL;
	scene_packs.original_count = 0;
	scene_packs.table_count = 0;
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

boolean scene_packs_read(
	long offset,
	long size,
	void *buffer)
{
	unsigned long relative = (unsigned long)offset - SCENE_PACK_FILE_BASE;
	short range = (short)(relative / SCENE_PACK_RANGE_BYTES);
	unsigned long local = relative % SCENE_PACK_RANGE_BYTES;
	boolean read = FALSE;
	short index;

	if ((unsigned long)offset < SCENE_PACK_FILE_BASE || range >= MAXIMUM_LOADED_SCENE_PACKS)
		return FALSE;
	if (size > 0)
		memory_watch_prepare_write(buffer, (unsigned long)size);
	while (__atomic_exchange_n(&scene_packs.read_lock, 1, __ATOMIC_ACQUIRE))
		SwitchToThread();
	for (index = 0; index < scene_packs.pack_count; index++)
	{
		struct scene_pack_slot *slot = &scene_packs.packs[index];

		if (state_get(slot) == _scene_pack_state_loaded && slot->range == range && slot->file && size >= 0 &&
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
