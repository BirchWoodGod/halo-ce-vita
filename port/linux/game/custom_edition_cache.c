/*
CUSTOM_EDITION_CACHE.C

Halo Custom Edition and OpenSauce caches in the native builds' cache file
loader (custom_edition_cache.h). The loader runs only Xbox caches of this
build; without this unit it rejects a Custom Edition cache as "an old
version" and never looks for a ".yelo" file at all.

With HALO_CUSTOM_EDITION set (the platform then reserves the Custom Edition
tag cache, port/linux/src/xbox_memory.c), a Custom Edition map is loaded
instead: it is read in place rather than copied to the cache partition, and
its tags go to 0x40440000 through cache_file_formats.c, which also converts
what only needs their bytes changed. custom_edition_bitmaps.c and
custom_edition_geometry.c convert the rest with the game's own functions.
Every read the game makes of the map (structure BSPs, bitmap pixels, sound
samples) is served from the map, bitmaps.map or sounds.map according to
where its offset falls in their combined offset space.

A process that cannot have its tag cache at 0x40440000 (the Vita, whose
user memory starts higher) loads the tags into memory of its own instead:
cache_file_formats.c works on any copy of the tag cache, and once it and
the game's own conversions are done, the tags' pointers are moved to where
the copy is (port/linux/src/tag_relocate.c, with this build's tag layouts,
which the converted tags now have), as is each structure BSP when it is
read.
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "cseries_windows.h"
#include "tag_files/files.h"
#include "tag_files/tag_files.h"
#include "cache/cache_files.h"
#include "scenario/scenario_definitions.h"
#include "cache_file_formats.h"
#include "custom_edition_cache.h"
#include "custom_edition_maps.h"
#include "map_share_protocol.h"
#include "tag_schema.h"
#include "../src/lang.h"
#ifdef HALO_RELOCATABLE_TAG_CACHE
#include "tag_relocate.h"
#endif

#include "memory/zlib/zlib.h"

#include <pthread.h>
#include <stdlib.h>

/* ---------- constants */

#define MAP_FILE_EXTENSION ".map"
#define OPENSAUCE_MAP_FILE_EXTENSION ".yelo"
/* as the cache file loader's own map paths */
#define MAP_PATH_SIZE 256

/* where bitmaps.map and sounds.map start in the combined offset space; the
largest map is 0x24000000 bytes long (cache_file_formats.h) */
#define COMBINED_BITMAPS_OFFSET 0x40000000UL
#define COMBINED_SOUNDS_OFFSET 0x60000000UL
#define COMBINED_OFFSET_LIMIT 0x80000000UL

/* the piece of a whole map file read at a time for its CRC-32
(custom_edition_cache_map_identity) */
#define IDENTITY_READ_BYTES 0x10000

/* the platform's write tracking (port/linux/src/platform.h): told of every
write into guest memory the game's own code does not make, which the Vita,
without page protection, sees in no other way - its renderer keeps converted
textures until their pages are written */
void memory_watch_prepare_write(void *address, unsigned long size);

/* ---------- structures */

struct custom_edition_file
{
	/* the file, while opened */
	boolean opened;
	HANDLE handle;
	struct cache_file_source source;
};

struct custom_edition_cache_globals
{
	boolean tags_loaded;
	/* the tag cache and the bytes of it the loaded tags use */
	uint8_t *tag_cache;
	uint32_t loaded_bytes;
	/* (the tag cache's size: cache_file_tag_cache_contains) */
	uint32_t tag_cache_bytes;
	struct custom_edition_file map;
	struct custom_edition_file resource_files[NUMBER_OF_RESOURCE_MAP_TYPES];
	struct resource_map resource_map_storage[NUMBER_OF_RESOURCE_MAP_TYPES];
	struct resource_map *resource_maps[NUMBER_OF_RESOURCE_MAP_TYPES];
	/* the port reads from the tick thread (sounds, the textures it
	predicts) and the render thread (textures) at once: one at a time, each
	with its arrival (custom_edition_bitmap_pixels_arrived), under this */
	volatile int read_lock;
};

/* ---------- globals */

static struct custom_edition_cache_globals custom_edition_cache_globals;

/* ---------- prototypes */

static boolean custom_edition_cache_model_data_crc(
	struct custom_edition_load_report const *report,
	byte *buffer,
	uint32_t buffer_bytes,
	uint32_t *crc);
/* (game.c's: the loading screen while a map loads, its frames from the
thread that loads it) */
void game_loading_screen_begin(void);
void game_loading_screen_frame(real progress);
void game_loading_screen_end(void);

/* the load's progress, 0..1, for the loading screen (the game thread's) */
static real custom_edition_load_progress;

static void custom_edition_load_frame(
	void)
{
	game_loading_screen_frame(custom_edition_load_progress);

	return;
}

/* ---------- private code */

/* The map files are read with positioned reads through the platform's
file layer (port/linux/src/xbox_files.c), as Xbox maps are: on the Vita
straight into the reader's memory, in requests of up to a megabyte. They
were read through stdio, whose stream buffer on the Vita (newlib's BUFSIZ)
is 1 KB, so a read reached the memory card as 1 KB requests: ~3 MB/s, 20 s
of Extinction's tag data and checksum, 10 s of its model data, and each
texture and sound the game's caches stream in from bitmaps.map and
sounds.map during play, on the thread that draws or ticks, holding the
cache lock. */
#define FILE_READ_REQUEST_BYTES 0x100000

/* (the load's report, custom_edition_cache_tags_load: requests, bytes and
the time spent in them, by any thread) */
static struct
{
	unsigned long requests;
	unsigned long long bytes;
	unsigned long long microseconds;
} custom_edition_file_reads;

/* the time the tick (1) and the other threads (0) spent reading map files
here (objects.c's hitch report) */
volatile unsigned long long halo_map_read_us[2];
int halo_epoch_on_mutator(void);

/* (the platform's clock: port/linux/src/posix_profile.c, the Vita's host) */
unsigned long long vita_host_time_us(void);

static unsigned long long custom_edition_microseconds(
	void)
{
	return vita_host_time_us();
}

static int custom_edition_file_read(
	void *context,
	uint32_t offset,
	uint32_t size,
	void *buffer)
{
	unsigned long long started;
	uint32_t done = 0;

	/* (a loading screen frame when one is due, on the loading thread: the
	resource maps' small reads, hundreds of them) */
	custom_edition_load_frame();
	started = custom_edition_microseconds();
	while (done < size)
	{
		OVERLAPPED position;
		DWORD request = MIN(size - done, FILE_READ_REQUEST_BYTES);
		DWORD read = 0;

		csmemset(&position, 0, sizeof(position));
		position.Offset = offset + done;
		if (!ReadFile((HANDLE)context, (byte *)buffer + done, request, &read, &position) || !read)
		{
			break;
		}
		/* (the load's reader thread reads too) */
		__atomic_fetch_add(&custom_edition_file_reads.requests, 1, __ATOMIC_RELAXED);
		done += read;
	}
	{
		/* (debug) HALO_IO_THROTTLE_KBPS=<n>: as the platform's reads of Xbox
		maps (xbox_files.c), these take as long as at n KB/s - the Vita's
		memory card, 10000-13000 with large requests, ~3000 as stdio read
		these maps - to see in the harness what waits behind them */
		static long throttle = -1;

		if (throttle < 0)
			throttle = getenv("HALO_IO_THROTTLE_KBPS") ? atol(getenv("HALO_IO_THROTTLE_KBPS")) : 0;
		if (throttle > 0 && done)
			Sleep((DWORD)(1 + (unsigned long long)done * 1000ull / ((unsigned long long)throttle * 1024ull)));
	}
	__atomic_fetch_add(&custom_edition_file_reads.bytes, (unsigned long long)done, __ATOMIC_RELAXED);
	__atomic_fetch_add(&custom_edition_file_reads.microseconds, custom_edition_microseconds() - started, __ATOMIC_RELAXED);
	halo_map_read_us[halo_epoch_on_mutator() ? 1 : 0] += custom_edition_microseconds() - started;

	return done == size;
}

/* ---------- the load's reader */

/* A Custom Edition map is loaded on the game thread
(custom_edition_cache_tags_load): its tags, their conversion and the
models' buffers are the game's state, made with the game's own functions
and its allocator, which are the game thread's alone. Its big reads - the
tag data, the structure BSPs and the model data, 45-70 MB of a large map -
are made on a thread of their own, the load's reader (the fourth core's
with Fourth core helpers), while the game thread shows the loading screen
(game_loading_screen_frame, game.c) or converts what was read before it: on the
owner's Vita (Oct 9, Hugeass) the game thread read 74 MB in 1091 file calls
in one 12 s frame, nothing drawn, the hang watchdog's 8 s passed. The reader
takes the CRC-32s the map's checksum wants of what it reads, so the model
data (Extinction's 25 MB) is read once, by the models' conversion, rather
than once more for the checksum alone. */

enum
{
	LOAD_READ_QUEUE_LENGTH = 16,
	LOAD_READER_STACK_BYTES = 0x10000,
	/* the model data's pieces the checksum has seen, apart (see
	model_checksum_take): the conversion reads it a buffer after another,
	in order or the reverse, so its pieces join as they come */
	MAXIMUM_MODEL_CHECKSUM_PIECES = 64,
};

enum
{
	_read_job_crc_none,
	/* the CRC-32 from 0 of the bytes read */
	_read_job_crc_running,
	/* the bytes read are model data, whose part of the checksum they are */
	_read_job_crc_model_data,
};

static struct
{
	pthread_mutex_t lock;
	pthread_cond_t wake;
	boolean started;
	boolean failed;
	struct custom_edition_read_job *queue[LOAD_READ_QUEUE_LENGTH];
	short head;
	short count;
} load_reader = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER };

/* the model data's pieces whose CRC-32 (from 0) the reader took, in order,
none touching another (model_checksum_take); only the reader writes them
while the models convert, and the game thread reads them once it is idle */
static struct
{
	boolean active;
	boolean overflowed;
	uint32_t model_data_offset;
	uint32_t model_data_bytes;
	short count;
	struct
	{
		uint32_t start;
		uint32_t end;
		uint32_t crc;
	} pieces[MAXIMUM_MODEL_CHECKSUM_PIECES];
} model_checksum;

void custom_edition_load_progress_set(
	real progress)
{
	custom_edition_load_progress = progress;
	custom_edition_load_frame();

	return;
}

/* the piece [start, end) of the model data, `bytes` its bytes, joined to
the pieces taken before: the parts of it not taken yet have their CRC-32
taken and are put in their place, a piece next to another joined to it
(the CRC of A then B: cache_file_crc32_shift(a, length of B) ^ b) */
static void model_checksum_piece_put(
	uint32_t start,
	uint32_t end,
	uint32_t crc)
{
	short index;
	short at;

	for (at = 0; at < model_checksum.count && model_checksum.pieces[at].end <= start; at++)
	{
	}
	/* after the one before it */
	if (at > 0 && model_checksum.pieces[at - 1].end == start)
	{
		at--;
		model_checksum.pieces[at].crc = cache_file_crc32_shift(model_checksum.pieces[at].crc, end - start) ^ crc;
		model_checksum.pieces[at].end = end;
	}
	else
	{
		if (model_checksum.count == MAXIMUM_MODEL_CHECKSUM_PIECES)
		{
			model_checksum.overflowed = TRUE;
			return;
		}
		for (index = model_checksum.count; index > at; index--)
		{
			model_checksum.pieces[index] = model_checksum.pieces[index - 1];
		}
		model_checksum.count++;
		model_checksum.pieces[at].start = start;
		model_checksum.pieces[at].end = end;
		model_checksum.pieces[at].crc = crc;
	}
	/* before the one after it */
	if (at + 1 < model_checksum.count && model_checksum.pieces[at + 1].start == model_checksum.pieces[at].end)
	{
		model_checksum.pieces[at].crc = cache_file_crc32_shift(
			model_checksum.pieces[at].crc,
			model_checksum.pieces[at + 1].end - model_checksum.pieces[at + 1].start) ^ model_checksum.pieces[at + 1].crc;
		model_checksum.pieces[at].end = model_checksum.pieces[at + 1].end;
		for (index = at + 1; index + 1 < model_checksum.count; index++)
		{
			model_checksum.pieces[index] = model_checksum.pieces[index + 1];
		}
		model_checksum.count--;
	}

	return;
}

/* the bytes of [start, end) of the model data, read into `bytes`: what of
it no piece holds yet is taken */
static void model_checksum_take(
	uint32_t start,
	uint32_t end,
	byte const *bytes)
{
	uint32_t gaps[MAXIMUM_MODEL_CHECKSUM_PIECES + 1][2];
	short gap_count = 0;
	uint32_t cursor = start;
	short index;

	for (index = 0; index < model_checksum.count && cursor < end; index++)
	{
		if (model_checksum.pieces[index].end <= cursor)
		{
			continue;
		}
		if (model_checksum.pieces[index].start >= end)
		{
			break;
		}
		if (model_checksum.pieces[index].start > cursor)
		{
			gaps[gap_count][0] = cursor;
			gaps[gap_count++][1] = model_checksum.pieces[index].start;
		}
		cursor = model_checksum.pieces[index].end;
	}
	if (cursor < end)
	{
		gaps[gap_count][0] = cursor;
		gaps[gap_count++][1] = end;
	}
	for (index = 0; index < gap_count && !model_checksum.overflowed; index++)
	{
		model_checksum_piece_put(
			gaps[index][0],
			gaps[index][1],
			cache_file_crc32_update(0, bytes + (gaps[index][0] - start), gaps[index][1] - gaps[index][0]));
	}

	return;
}

/* (on the reader's thread, or the caller's without it) */
static void read_job_run(
	struct custom_edition_read_job *job)
{
	uint32_t done = 0;
	boolean read = TRUE;

	job->crc = 0;
	while (read && done < job->size)
	{
		uint32_t request = MIN(job->size - done, FILE_READ_REQUEST_BYTES);

		read = custom_edition_file_read(job->context, job->offset + done, request, (byte *)job->buffer + done) != 0;
		if (read && job->crc_kind == _read_job_crc_running)
		{
			job->crc = cache_file_crc32_update(job->crc, (byte *)job->buffer + done, request);
		}
		done += request;
	}
	if (read && job->crc_kind == _read_job_crc_model_data && model_checksum.active)
	{
		uint32_t start = job->offset - model_checksum.model_data_offset;

		model_checksum_take(start, start + job->size, (byte const *)job->buffer);
	}
	__atomic_store_n(&job->state, read ? _custom_edition_read_job_done : _custom_edition_read_job_failed, __ATOMIC_RELEASE);

	return;
}

static void *load_reader_thread(
	void *parameter)
{
	(void)parameter;
#ifdef HALO_VITA
	/* (Fourth core helpers, All async: with the cache file thread) */
	vita_host_fourth_core_join("custom map reader", 2);
#endif
	pthread_mutex_lock(&load_reader.lock);
	for (;;)
	{
		struct custom_edition_read_job *job;

		while (!load_reader.count)
		{
			pthread_cond_wait(&load_reader.wake, &load_reader.lock);
		}
		job = load_reader.queue[load_reader.head];
		load_reader.head = (short)((load_reader.head + 1) % LOAD_READ_QUEUE_LENGTH);
		load_reader.count--;
		pthread_mutex_unlock(&load_reader.lock);
		read_job_run(job);
		pthread_mutex_lock(&load_reader.lock);
	}

	return NULL;
}

/* the reader's thread, started at the first load and kept (HALO_CE_LOAD_READER=0:
none, every read on the loading thread as before) */
static boolean load_reader_start(
	void)
{
	pthread_attr_t attributes;
	pthread_t thread;

	if (load_reader.started || load_reader.failed)
	{
		return load_reader.started;
	}
	if (getenv("HALO_CE_LOAD_READER") && !atoi(getenv("HALO_CE_LOAD_READER")))
	{
		load_reader.failed = TRUE;
		return FALSE;
	}
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, LOAD_READER_STACK_BYTES);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&thread, &attributes, load_reader_thread, NULL) == 0)
	{
		load_reader.started = TRUE;
	}
	else
	{
		load_reader.failed = TRUE;
		error(_error_silent, "custom edition: no thread for the map's reads; they are made as it loads");
	}
	pthread_attr_destroy(&attributes);

	return load_reader.started;
}

void custom_edition_read_job_submit(
	struct custom_edition_read_job *job)
{
	__atomic_store_n(&job->state, _custom_edition_read_job_queued, __ATOMIC_RELAXED);
	if (!load_reader.started)
	{
		/* (no reader: read here) */
		read_job_run(job);
		return;
	}
	for (;;)
	{
		pthread_mutex_lock(&load_reader.lock);
		if (load_reader.count < LOAD_READ_QUEUE_LENGTH)
		{
			load_reader.queue[(load_reader.head + load_reader.count) % LOAD_READ_QUEUE_LENGTH] = job;
			load_reader.count++;
			pthread_cond_signal(&load_reader.wake);
			pthread_mutex_unlock(&load_reader.lock);
			return;
		}
		pthread_mutex_unlock(&load_reader.lock);
		/* (its queue full: once there is room, the reads in their order) */
		custom_edition_load_frame();
		Sleep(1);
	}
}

boolean custom_edition_read_job_wait(
	struct custom_edition_read_job *job)
{
	int state;

	while ((state = __atomic_load_n(&job->state, __ATOMIC_ACQUIRE)) == _custom_edition_read_job_queued)
	{
		custom_edition_load_frame();
		Sleep(1);
	}

	return state == _custom_edition_read_job_done;
}

/* (custom_edition_load_hooks' read_crc: the tag data and the structure
BSPs) */
static int custom_edition_load_read_crc(
	void *context,
	uint32_t offset,
	uint32_t size,
	void *buffer,
	uint32_t *crc)
{
	struct custom_edition_read_job job;

	csmemset(&job, 0, sizeof(job));
	job.context = context;
	job.offset = offset;
	job.size = size;
	job.buffer = buffer;
	job.crc_kind = _read_job_crc_running;
	custom_edition_read_job_submit(&job);
	if (!custom_edition_read_job_wait(&job))
	{
		return FALSE;
	}
	*crc = job.crc;

	return TRUE;
}

/* a part of the load, for its line in debug.txt: the time since `phase`
was marked, the reads made in it and the time they took; marks again */
struct custom_edition_load_phase
{
	unsigned long long started;
	unsigned long requests;
	unsigned long long bytes, microseconds;
};

static void custom_edition_load_phase_mark(
	struct custom_edition_load_phase *phase)
{
	phase->started = custom_edition_microseconds();
	phase->requests = custom_edition_file_reads.requests;
	phase->bytes = custom_edition_file_reads.bytes;
	phase->microseconds = custom_edition_file_reads.microseconds;

	return;
}

static int custom_edition_load_phase_describe(
	struct custom_edition_load_phase *phase,
	char const *name,
	char *text,
	size_t size)
{
	int length = snprintf(
		text,
		size,
		"%s %lu ms (%lu KB in %lu reads, %lu ms of them)",
		name,
		(unsigned long)((custom_edition_microseconds() - phase->started) / 1000),
		(unsigned long)((custom_edition_file_reads.bytes - phase->bytes) / 1024),
		custom_edition_file_reads.requests - phase->requests,
		(unsigned long)((custom_edition_file_reads.microseconds - phase->microseconds) / 1000));

	custom_edition_load_phase_mark(phase);

	return length;
}

static boolean custom_edition_file_open(
	struct custom_edition_file *file,
	char const *path)
{
	HANDLE handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	DWORD size;

	file->opened = FALSE;
	if (handle == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}
	size = GetFileSize(handle, NULL);
	if (size == INVALID_FILE_SIZE)
	{
		CloseHandle(handle);
		return FALSE;
	}
	file->opened = TRUE;
	file->handle = handle;
	file->source.context = handle;
	file->source.read = custom_edition_file_read;
	file->source.size = (uint32_t)size;

	return TRUE;
}

static void custom_edition_file_close(
	struct custom_edition_file *file)
{
	if (file->opened)
	{
		CloseHandle(file->handle);
		file->opened = FALSE;
	}

	return;
}

static boolean file_path_exists(
	char const *path)
{
	struct file_reference reference;

	return file_exists(file_reference_create_from_path(&reference, path, FALSE));
}

/* the file that holds the map `map_name` names: <maps>\<name>.map, or the
OpenSauce <maps>\<name>.yelo when there is no .map */
static boolean custom_edition_map_path(
	char const *map_name,
	char *path)
{
	char const *directory = cache_files_map_directory();
	char const *name = tag_name_strip_path(map_name);

	if (strlen(directory) + strlen(name) + strlen(OPENSAUCE_MAP_FILE_EXTENSION) >= MAP_PATH_SIZE)
	{
		return FALSE;
	}
	sprintf(path, "%s%s%s", directory, name, MAP_FILE_EXTENSION);
	if (!file_path_exists(path))
	{
		sprintf(path, "%s%s%s", directory, name, OPENSAUCE_MAP_FILE_EXTENSION);
	}

	return file_path_exists(path);
}

/* A cache's resource map of `type`: <maps>\bitmaps.map and so on, or for an
OpenSauce cache built with a mod set, <maps>\data_files\<mod>-bitmaps.map
(the mod set file name is an assumption: docs/custom_edition_caches.md). */
static boolean custom_edition_resource_map_path(
	struct cache_file_identity const *identity,
	enum resource_map_type type,
	char *path)
{
	char const *directory = cache_files_map_directory();
	char const *type_name = resource_map_type_describe(type);

	if (identity->has_opensauce_header &&
		TEST_FLAG(identity->opensauce.flags, _opensauce_cache_uses_mod_data_files_bit))
	{
		if (strlen(directory) + strlen(identity->opensauce.mod_name) + strlen(type_name) + 32 >= MAP_PATH_SIZE)
		{
			return FALSE;
		}
		sprintf(path, "%sdata_files\\%s-%s%s", directory, identity->opensauce.mod_name, type_name, MAP_FILE_EXTENSION);
	}
	else
	{
		sprintf(path, "%s%s%s", directory, type_name, MAP_FILE_EXTENSION);
	}

	return TRUE;
}

static void custom_edition_cache_files_close(
	void)
{
	struct custom_edition_cache_globals *globals = &custom_edition_cache_globals;
	short type;

	for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
	{
		if (globals->resource_maps[type])
		{
			resource_map_close(globals->resource_maps[type]);
			globals->resource_maps[type] = NULL;
		}
		custom_edition_file_close(&globals->resource_files[type]);
	}
	custom_edition_file_close(&globals->map);

	return;
}

/* Converts the loaded map's models for this build from its model data,
which custom_edition_models_convert reads a part at a time. */
static boolean custom_edition_cache_models_convert(
	uint8_t *tag_cache,
	struct custom_edition_load_report const *report)
{
	return custom_edition_models_convert(
		tag_cache,
		report->tag_data_bytes + report->resource_tag_bytes,
		report);
}

static void custom_edition_cache_load_failure_reason(
	char const *reason);

/* The tags custom_edition_cache_load loaded and custom_edition_cache_convert
converted (and moved), checked as an Xbox map's are before the game reads
them (tag_validate.c): their pixels and samples in the files of the combined
offset space. */
static boolean custom_edition_cache_tags_validate(
	uint8_t *tag_cache,
	uint32_t tag_cache_bytes,
	struct custom_edition_load_report const *report)
{
	struct custom_edition_cache_globals *globals = &custom_edition_cache_globals;
	struct tag_validate_file_range ranges[MAXIMUM_TAG_VALIDATE_FILE_RANGES];
	short range_count = 0;

	ranges[range_count].offset = 0;
	ranges[range_count++].size = report->identity.file_size;
	if (globals->resource_files[_resource_map_bitmaps].opened)
	{
		ranges[range_count].offset = COMBINED_BITMAPS_OFFSET;
		ranges[range_count++].size = globals->resource_files[_resource_map_bitmaps].source.size;
	}
	if (globals->resource_files[_resource_map_sounds].opened)
	{
		ranges[range_count].offset = COMBINED_SOUNDS_OFFSET;
		ranges[range_count++].size = globals->resource_files[_resource_map_sounds].source.size;
	}
	if (!tag_validate_custom_edition_tags(
		tag_cache,
		(long)(report->tag_data_bytes + report->resource_tag_bytes),
		tag_cache_bytes,
		ranges,
		range_count,
		report->identity.name))
	{
		error(_error_silent, "custom edition: the map's tags failed the tag check (above)");
		custom_edition_cache_load_failure_reason(tag_validate_out_of_memory() ?
			T("out of memory: restart the game") : T("this map file is damaged or not supported"));
		return FALSE;
	}
	if (tag_validate_corrections())
	{
		error(_error_silent, "custom edition: the map's tags needed %ld corrections (above)",
			tag_validate_corrections());
	}

	return TRUE;
}

/* Makes the tags custom_edition_cache_load loaded into `tag_cache` this
build's: their resource offsets combined, their bytes converted, then
checked as an Xbox map's tags are; then their bitmaps checked, their
models converted. */
static boolean custom_edition_cache_tags_convert(
	uint8_t *tag_cache,
	uint32_t tag_cache_bytes,
	struct custom_edition_load_report const *report)
{
	uint32_t loaded_bytes = report->tag_data_bytes + report->resource_tag_bytes;
	struct custom_edition_conversion_report conversion;
	enum cache_file_status status;

	custom_edition_cache_combine_resource_offsets(
		tag_cache,
		loaded_bytes,
		COMBINED_BITMAPS_OFFSET,
		COMBINED_SOUNDS_OFFSET);
	status = custom_edition_cache_convert(tag_cache, loaded_bytes, &conversion);
	custom_edition_load_progress_set(0.48f);
	if (status != _cache_file_status_ok)
	{
		error(
			_error_silent,
			"custom edition: cannot convert '%s': %s",
			custom_edition_cache_tag_name(tag_cache, loaded_bytes, conversion.problem_tag_index),
			cache_file_status_describe(status));
		return FALSE;
	}
	error(
		_error_silent,
		"custom edition: %ld shaders renumbered, %ld transparent chicago extended shaders made transparent chicago shaders, %ld bitmaps prepared%s",
		(long)conversion.shaders_retyped,
		(long)conversion.chicago_extended_shaders,
		(long)conversion.bitmaps_prepared,
		conversion.script_nodes_reduced ? ", OpenSauce's script nodes made this build's number" : "");
	if (conversion.animation_overlays_disabled)
	{
		error(
			_error_silent,
			"custom edition: %ld animation overlays named animations their graphs do not have and were disabled",
			(long)conversion.animation_overlays_disabled);
	}
	if (conversion.sounds_undecodable)
	{
		error(
			_error_silent,
			"custom edition: %ld sounds use a compression this build cannot decode and will not play",
			(long)conversion.sounds_undecodable);
	}
	if (conversion.sounds_ogg_vorbis)
	{
		error(
			_error_silent,
			"custom edition: %ld Ogg Vorbis sounds are decoded to Xbox ADPCM as they load",
			(long)conversion.sounds_ogg_vorbis);
	}
	if (conversion.hud_placements_rescaled)
	{
		error(
			_error_silent,
			"custom edition: %ld HUD elements drawn from Halo PC's double resolution bitmaps were given half their scale",
			(long)conversion.hud_placements_rescaled);
	}
	if (conversion.score_hint_converted)
	{
		error(_error_silent, "custom edition: the multiplayer score hint names the BACK button where Halo PC names a key");
	}

#ifdef HALO_RELOCATABLE_TAG_CACHE
	/* The tags, in this build's layouts now but for the models (whose
	Custom Edition layout the walk knows too), moved to where they are
	before the game's own code below reads them through their pointers */
	if ((uint32_t)(unsigned long)tag_cache != custom_edition_cache_linked_address())
	{
		if (!halo_tag_relocate_linked_tags(tag_cache, loaded_bytes, custom_edition_cache_linked_address(), tag_cache_bytes))
		{
			error(_error_silent, "custom edition: out of memory moving the tags");
			return FALSE;
		}
		custom_edition_cache_tags_moved((uint32_t)(unsigned long)tag_cache);
		custom_edition_load_progress_set(0.50f);
		error(
			_error_silent,
			"custom edition: tags moved from 0x%08lX to %p",
			(unsigned long)custom_edition_cache_linked_address(),
			tag_cache);
	}
#endif

	/* (from OpenCE, MrBruh's "Validate map tags before loading" and "Load,
	check and run Halo Custom Edition and OpenSauce maps") the tags, made
	this build's and where they are now, checked as an Xbox map's are
	before the game's own code reads them (tag_validate.c): their pixels
	and samples in the files of the combined offset space */
	if (!custom_edition_cache_tags_validate(tag_cache, tag_cache_bytes, report))
	{
		return FALSE;
	}
	custom_edition_load_progress_set(0.53f);

	if (!custom_edition_bitmaps_verify(tag_cache, loaded_bytes))
	{
		return FALSE;
	}
	custom_edition_bitmaps_reduce(tag_cache, loaded_bytes);
	custom_edition_load_progress_set(0.55f);
	return custom_edition_reordered_bitmaps_find(tag_cache, loaded_bytes) &&
		custom_edition_scripts_convert(tag_cache, loaded_bytes) &&
		custom_edition_cache_models_convert(tag_cache, report);
}

/* the answers custom_edition_cache_identify_file gave, by map name, for a
while: the game asks each frame whether the map it is precaching or about
to load is a Custom Edition one (cache_files_precache_map_loaded, from the
lobby's precaching and the server's checks), and each answer was the map's
file looked for, opened, its header read and closed - several file calls a
frame on the game thread, each waiting on the Vita for the memory card,
which serves one request at a time: behind a map copy's write it took
hundreds of milliseconds, the lobby's hitches with nothing else happening.
An answer stands IDENTITY_MEMORY_MS, and a map share download forgets its
map's (custom_edition_cache_map_identity_forget). */
enum
{
	REMEMBERED_ANSWERS = 8,
	IDENTITY_MEMORY_MS = 10000,
};
static struct
{
	char name[CACHE_FILE_STRING_BYTES];
	unsigned long when;
	boolean identified;
	struct cache_file_identity identity;
} remembered_answers[REMEMBERED_ANSWERS];
static short next_remembered_answer;
static volatile int remembered_answers_lock;

static void remembered_answers_take(
	void)
{
	while (__atomic_exchange_n(&remembered_answers_lock, 1, __ATOMIC_ACQUIRE))
	{
	}

	return;
}

static void remembered_answers_give(
	void)
{
	__atomic_store_n(&remembered_answers_lock, 0, __ATOMIC_RELEASE);

	return;
}

static void remembered_answers_forget(
	char const *name)
{
	short index;

	remembered_answers_take();
	for (index = 0; index < REMEMBERED_ANSWERS; index++)
	{
		if (!name || !csstrcasecmp(remembered_answers[index].name, name))
		{
			remembered_answers[index].name[0] = 0;
		}
	}
	remembered_answers_give();

	return;
}

/* Whether the map `map_name` names is a Custom Edition cache, whose header
is then described in `identity`, whatever the setting; and when
`resource_maps_used`, the resource maps it takes tags from
(custom_edition_cache_resource_maps_used). */
static boolean custom_edition_cache_identify_file(
	char const *map_name,
	struct cache_file_identity *identity,
	uint32_t *resource_maps_used)
{
	char path[MAP_PATH_SIZE];
	struct custom_edition_file file;
	boolean identified = FALSE;
	char const *name = tag_name_strip_path(map_name);
	unsigned long now = system_milliseconds();
	boolean rememberable = !resource_maps_used && strlen(name) < CACHE_FILE_STRING_BYTES;
	short index;

	if (rememberable)
	{
		remembered_answers_take();
		for (index = 0; index < REMEMBERED_ANSWERS; index++)
		{
			if (remembered_answers[index].name[0] &&
				now - remembered_answers[index].when < IDENTITY_MEMORY_MS &&
				!csstrcasecmp(remembered_answers[index].name, name))
			{
				identified = remembered_answers[index].identified;
				if (identified)
				{
					*identity = remembered_answers[index].identity;
				}
				remembered_answers_give();
				return identified;
			}
		}
		remembered_answers_give();
	}
	if (custom_edition_map_path(map_name, path) &&
		custom_edition_file_open(&file, path))
	{
		if (cache_file_identify(&file.source, identity) == _cache_file_status_ok &&
			identity->format == _cache_file_format_custom_edition_cache)
		{
			identified = TRUE;
			if (resource_maps_used)
			{
				*resource_maps_used = custom_edition_cache_resource_maps_used(&file.source, identity);
			}
		}
		custom_edition_file_close(&file);
	}
	if (rememberable)
	{
		remembered_answers_take();
		for (index = 0; index < REMEMBERED_ANSWERS; index++)
		{
			if (!csstrcasecmp(remembered_answers[index].name, name))
			{
				break;
			}
		}
		if (index == REMEMBERED_ANSWERS)
		{
			index = next_remembered_answer;
			next_remembered_answer = (short)((next_remembered_answer + 1) % REMEMBERED_ANSWERS);
		}
		csstrncpy(remembered_answers[index].name, name, CACHE_FILE_STRING_BYTES - 1);
		remembered_answers[index].name[CACHE_FILE_STRING_BYTES - 1] = 0;
		remembered_answers[index].when = now;
		remembered_answers[index].identified = identified;
		if (identified)
		{
			remembered_answers[index].identity = *identity;
		}
		remembered_answers_give();
	}

	return identified;
}

/* Whether Custom Edition maps may run (game.custom_edition) and the map
`map_name` names is a Custom Edition cache, whose header is then described
in `identity`. */
static boolean custom_edition_cache_identify(
	char const *map_name,
	struct cache_file_identity *identity)
{
	return halo_custom_edition_enabled() && custom_edition_cache_identify_file(map_name, identity, NULL);
}

static void custom_edition_cache_report_log(
	struct custom_edition_load_report const *report)
{
	error(
		_error_silent,
		"custom edition: %ld tags, 0x%lX bytes of tag data and 0x%lX of tags from resource maps (bitmaps %ld, sounds %ld, loc %ld)",
		(long)report->tag_count,
		(unsigned long)report->tag_data_bytes,
		(unsigned long)report->resource_tag_bytes,
		(long)report->resource_tag_counts[_resource_map_bitmaps],
		(long)report->resource_tag_counts[_resource_map_sounds],
		(long)report->resource_tag_counts[_resource_map_locale]);
	error(
		_error_silent,
		"custom edition: %ld structure BSPs (%ld materials), %ld bitmap and %ld sound ranges checked, %ld pointers relocated, checksum %s",
		(long)report->structure_bsp_count,
		(long)report->structure_bsp_materials_checked,
		(long)report->bitmap_data_ranges_checked,
		(long)report->sound_sample_ranges_checked,
		(long)report->relocated_pointer_count,
		report->checksum_deferred ? "taken with the models" :
		TEST_FLAG(report->warnings, _custom_edition_warning_checksum_mismatch_bit) ? "mismatched" : "matched");

	return;
}

/* The last Custom Edition map loaded, and why it could not be, for the
player (custom_edition_cache_load_failure_show): a map that cannot be loaded
for want of memory, a resource map or anything else takes the player back
to the menu with this, rather than stopping the game. */
static struct
{
	char map_name[MAP_PATH_SIZE];
	char reason[256];
	boolean failed;
} custom_edition_load_failure;

static void custom_edition_cache_load_failure_begin(
	char const *map_name)
{
	csstrncpy(custom_edition_load_failure.map_name, tag_name_strip_path(map_name), MAP_PATH_SIZE - 1);
	custom_edition_load_failure.map_name[MAP_PATH_SIZE - 1] = 0;
	custom_edition_load_failure.reason[0] = 0;
	custom_edition_load_failure.failed = FALSE;

	return;
}

/* the player's reason for a load's failure (the first one given stands: the
most precise, a generic one following it) */
static void custom_edition_cache_load_failure_reason(
	char const *reason)
{
	custom_edition_load_failure.failed = TRUE;
	if (!custom_edition_load_failure.reason[0])
	{
		csstrncpy(custom_edition_load_failure.reason, reason, sizeof(custom_edition_load_failure.reason) - 1);
		custom_edition_load_failure.reason[sizeof(custom_edition_load_failure.reason) - 1] = 0;
	}

	return;
}

/* the reason for a failed custom_edition_cache_load, by its status, in the
player's language */
static char const *custom_edition_cache_load_status_reason(
	enum cache_file_status status,
	struct custom_edition_load_report const *report)
{
	switch (status)
	{
	case _cache_file_status_out_of_memory:
		return T("there is not enough memory for it");
	case _cache_file_status_read_failed:
		return T("its file could not be read");
	case _cache_file_status_missing_resource_map:
	case _cache_file_status_missing_resource_item:
		return T("it needs Halo Custom Edition's bitmaps.map, sounds.map and loc.map in the maps folder");
	case _cache_file_status_bad_scenario_tag:
		return TEST_FLAG(report->warnings, _custom_edition_warning_protected_bit) ?
			T("it is a protected map whose tags this port cannot read") :
			T("it has no scenario this port can find (a protected or damaged map)");
	default:
		return T(cache_file_status_describe(status));
	}
}

/* the C heap (on the Vita newlib's fixed 48 MB, which the system's
libraries share; 0: no fixed size) and the memory window, for a map that
runs out of either */
static void custom_edition_cache_heap_log(
	char const *when)
{
	unsigned long in_use;
	unsigned long capacity;
	unsigned long window_used;
	unsigned long window_free;

	platform_heap_usage(&in_use, &capacity);
	platform_contiguous_usage(&window_used, &window_free);
	error(
		_error_silent,
		"custom edition: %s: C heap %lu KB in use (of %lu KB), memory window %lu KB in use, %lu KB free",
		when,
		in_use / 1024,
		capacity / 1024,
		window_used / 1024,
		window_free / 1024);

	return;
}

/* ---------- public code */

boolean custom_edition_cache_refuse(
	void const *header,
	char const *build,
	char const *scenario_name,
	boolean fatal)
{
	int has_opensauce_header;

	if (cache_file_header_format(header, &has_opensauce_header) != _cache_file_format_custom_edition_cache)
	{
		return FALSE;
	}

	/* temporary holds 256 characters: bound the name and build strings */
	csprintf(
		temporary,
		"'%.96s' is a Halo Custom Edition cache%s (build %.31s): this build recognizes it but cannot run it (docs/custom_edition_caches.md)",
		scenario_name,
		has_opensauce_header ? " with an OpenSauce header" : "",
		build);
	error(_error_silent, "%s", temporary);
	if (fatal)
	{
		vassert(FALSE, temporary);
	}

	return TRUE;
}

void opensauce_cache_path_find(
	char *path,
	long path_size)
{
	long stem_length = (long)strlen(path) - (long)strlen(MAP_FILE_EXTENSION);
	struct file_reference reference;

	/* OpenSauce looks for the .map first, then the .yelo
	(cache_files_yelo.cpp, c_map_file_finder::SearchPath) */
	if (stem_length < 0 ||
		strcmp(path + stem_length, MAP_FILE_EXTENSION) ||
		stem_length + (long)strlen(OPENSAUCE_MAP_FILE_EXTENSION) >= path_size ||
		file_exists(file_reference_create_from_path(&reference, path, FALSE)))
	{
		return;
	}

	strcpy(path + stem_length, OPENSAUCE_MAP_FILE_EXTENSION);
	if (!file_exists(file_reference_create_from_path(&reference, path, FALSE)))
	{
		strcpy(path + stem_length, MAP_FILE_EXTENSION);
	}

	return;
}

boolean custom_edition_cache_playable(
	char const *map_name)
{
	struct cache_file_identity identity;

	return custom_edition_cache_identify(map_name, &identity);
}

boolean custom_edition_cache_is_custom_edition(
	char const *map_name)
{
	struct cache_file_identity identity;

	return custom_edition_cache_identify_file(map_name, &identity, NULL);
}

boolean custom_edition_cache_missing_resource_maps(
	char const *map_name,
	char *missing,
	long missing_size)
{
	struct cache_file_identity identity;
	uint32_t used = 0;
	long length = 0;
	short type;

	if (missing_size > 0)
	{
		missing[0] = 0;
	}
	if (!custom_edition_cache_identify_file(map_name, &identity, &used))
	{
		return FALSE;
	}
	for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
	{
		char path[MAP_PATH_SIZE];
		char const *directory = cache_files_map_directory();

		if (!TEST_FLAG(used, type) ||
			!custom_edition_resource_map_path(&identity, (enum resource_map_type)type, path) ||
			file_path_exists(path))
		{
			continue;
		}
		/* (named as in the maps folder: "data_files\<mod>-bitmaps.map" for
		an OpenSauce mod set) */
		if (length < missing_size - 1)
		{
			length += snprintf(missing + length, (size_t)(missing_size - length), "%s%s", length ? ", " : "",
				!strncmp(path, directory, strlen(directory)) ? path + strlen(directory) : path);
		}
		else
		{
			length = missing_size;
		}
	}

	return missing_size > 0 && missing[0];
}

boolean custom_edition_cache_xbox_multiplayer(
	char const *map_name)
{
	/* the cache partition's room for a multiplayer map
	(cache_files_windows.c, MULTIPLAYER_CACHE_FILE_MAXIMUM_SIZE), which its
	decompressed length must stay below */
	enum { MULTIPLAYER_CACHE_FILE_MAXIMUM_SIZE = 0x02F00000 };
	char const *name = tag_name_strip_path(map_name);
	char path[MAP_PATH_SIZE];
	struct custom_edition_file file;
	struct cache_file_identity identity;
	boolean multiplayer = FALSE;

	if (strlen(cache_files_map_directory()) + strlen(name) + strlen(MAP_FILE_EXTENSION) >= MAP_PATH_SIZE)
	{
		return FALSE;
	}
	sprintf(path, "%s%s%s", cache_files_map_directory(), name, MAP_FILE_EXTENSION);
	if (!custom_edition_file_open(&file, path))
	{
		return FALSE;
	}
	if (cache_file_identify(&file.source, &identity) == _cache_file_status_ok &&
		identity.format == _cache_file_format_xbox_cache &&
		identity.scenario_type == _scenario_type_multiplayer)
	{
		/* the cache partition knows a copied map by the name in its header
		(cache_files_windows.c, cached_map_files_find_map) */
		if (csstrcasecmp(identity.name, name))
		{
			error(_error_silent, "custom maps: '%s' is named '%s' inside: rename the file to %s.map", path, identity.name, identity.name);
		}
		else if (identity.file_length >= MULTIPLAYER_CACHE_FILE_MAXIMUM_SIZE)
		{
			error(
				_error_silent,
				"custom maps: '%s' is 0x%lX bytes decompressed, more than the Xbox's multiplayer maps may be (0x%X)",
				path,
				(unsigned long)identity.file_length,
				MULTIPLAYER_CACHE_FILE_MAXIMUM_SIZE);
		}
		else
		{
			multiplayer = TRUE;
		}
	}
	custom_edition_file_close(&file);

	return multiplayer;
}

/* the identities custom_edition_cache_map_identity worked out, by file
name and size: the CRC of a map without a checksum reads the whole file */
enum { REMEMBERED_IDENTITIES = 8 };
static struct
{
	char name[CACHE_FILE_STRING_BYTES];
	uint32_t size;
	unsigned long identity;
} remembered_identities[REMEMBERED_IDENTITIES];

boolean custom_edition_cache_map_file_path(
	char const *map_name,
	char *path,
	long path_size)
{
	char found[MAP_PATH_SIZE];

	if (!custom_edition_map_path(map_name, found) || (long)strlen(found) >= path_size)
	{
		return FALSE;
	}
	strcpy(path, found);

	return TRUE;
}

void custom_edition_cache_map_identity_forget(
	char const *map_name)
{
	char const *name = tag_name_strip_path(map_name);
	short index;

	remembered_answers_forget(name);
	custom_edition_maps_file_forget(name);

	for (index = 0; index < REMEMBERED_IDENTITIES; index++)
	{
		if (!csstrcasecmp(remembered_identities[index].name, name))
		{
			remembered_identities[index].identity = 0;
			remembered_identities[index].name[0] = 0;
		}
	}

	return;
}

unsigned long custom_edition_cache_map_identity(
	char const *map_name)
{
	static short next_remembered;
	char const *name = tag_name_strip_path(map_name);
	char path[MAP_PATH_SIZE];
	struct custom_edition_file file;
	struct cache_file_identity identity;
	unsigned long result = 0;
	short index;

	if (strlen(name) >= CACHE_FILE_STRING_BYTES ||
		!custom_edition_map_path(name, path) ||
		!custom_edition_file_open(&file, path))
	{
		return 0;
	}
	for (index = 0; index < REMEMBERED_IDENTITIES; index++)
	{
		if (remembered_identities[index].identity &&
			remembered_identities[index].size == file.source.size &&
			!csstrcasecmp(remembered_identities[index].name, name))
		{
			custom_edition_file_close(&file);
			return remembered_identities[index].identity;
		}
	}
	if (cache_file_identify(&file.source, &identity) == _cache_file_status_ok &&
		(identity.format == _cache_file_format_xbox_cache || identity.format == _cache_file_format_custom_edition_cache))
	{
		uLong crc = 0;

		/* (map_share_identity: the header's checksum with the file's
		length, or for a header without one, Invader's Xbox maps, the CRC-32
		of the whole file; map sharing works out the same of a download,
		map_share_protocol.c) */
		if (!identity.checksum || identity.checksum == 0xFFFFFFFFUL)
		{
			byte buffer[IDENTITY_READ_BYTES];
			uint32_t offset;

			crc = crc32(0L, Z_NULL, 0);
			for (offset = 0; offset < file.source.size; offset += IDENTITY_READ_BYTES)
			{
				uint32_t chunk = MIN(file.source.size - offset, IDENTITY_READ_BYTES);

				if (!file.source.read(file.source.context, offset, chunk, buffer))
				{
					crc = 0;
					break;
				}
				crc = crc32(crc, buffer, chunk);
			}
		}
		result = map_share_identity((uint32_t)identity.checksum, file.source.size, (uint32_t)crc);
		csstrncpy(remembered_identities[next_remembered].name, name, CACHE_FILE_STRING_BYTES - 1);
		remembered_identities[next_remembered].size = file.source.size;
		remembered_identities[next_remembered].identity = result;
		next_remembered = (short)((next_remembered + 1) % REMEMBERED_IDENTITIES);
	}
	custom_edition_file_close(&file);

	return result;
}

boolean custom_edition_cache_multiplayer(
	char const *map_name)
{
	struct cache_file_identity identity;

	return custom_edition_cache_identify(map_name, &identity) &&
		identity.scenario_type == _scenario_type_multiplayer;
}

boolean custom_edition_cache_campaign(
	char const *map_name)
{
	struct cache_file_identity identity;

	return custom_edition_cache_identify(map_name, &identity) &&
		identity.format == _cache_file_format_custom_edition_cache &&
		identity.scenario_type == _scenario_type_solo;
}

boolean custom_edition_level_name(
	char const *level_name)
{
	long prefix_length = (long)strlen(CUSTOM_EDITION_LEVEL_NAME_PREFIX);

	/* (the file name after it as map sharing takes one, map_share_name_valid:
	no path, no folder above, nothing a file system takes for something else) */
	return level_name &&
		!_strnicmp(level_name, CUSTOM_EDITION_LEVEL_NAME_PREFIX, (size_t)prefix_length) &&
		map_share_name_valid(level_name + prefix_length);
}

static struct cache_file_tag_header *custom_edition_cache_tags_load_private(
	char const *map_name,
	void *header)
{
	struct custom_edition_cache_globals *globals = &custom_edition_cache_globals;
	uint8_t *tag_cache;
	uint32_t tag_cache_bytes;
	struct custom_edition_load_report report;
	struct cache_file_identity identity;
	char path[MAP_PATH_SIZE];
	enum cache_file_status status;
	short type;
	/* (the load's parts in debug.txt, with the card's share of each) */
	struct custom_edition_load_phase phase;
	char phases[320];
	int phases_length = 0;

	custom_edition_load_phase_mark(&phase);
	assert(!globals->tags_loaded);
	custom_edition_cache_load_failure_begin(map_name);
	if (!custom_edition_map_path(map_name, path) || !custom_edition_file_open(&globals->map, path))
	{
		error(_error_silent, "custom edition: cannot open the map '%s'", map_name);
		custom_edition_cache_load_failure_reason(T("its file could not be opened"));
		return NULL;
	}
	status = cache_file_identify(&globals->map.source, &identity);
	if (status != _cache_file_status_ok ||
		identity.format != _cache_file_format_custom_edition_cache ||
		identity.file_size > COMBINED_BITMAPS_OFFSET ||
		!globals->map.source.read(globals->map.source.context, 0, CACHE_FILE_HEADER_BYTES, header))
	{
		error(_error_silent, "custom edition: '%s' is not a loadable cache (%s)", path, cache_file_status_describe(status));
		custom_edition_cache_load_failure_reason(T(cache_file_status_describe(status)));
		custom_edition_cache_files_close();
		return NULL;
	}
	error(_error_silent, "custom edition: loading '%s' (build %s%s)",
		path,
		identity.build,
		identity.has_opensauce_header ? ", OpenSauce" : "");
	custom_edition_cache_heap_log("before loading");
	/* the tag cache the map's tags are linked to the start of: at
	0x40440000 when the platform has it there, else elsewhere and moved
	below */
	tag_cache_bytes = custom_edition_tag_cache_bytes(&identity);
	tag_cache = halo_custom_edition_tag_cache_acquire(tag_cache_bytes);
	if (!tag_cache)
	{
		error(_error_silent, "custom edition: no room for the 0x%lX byte tag cache of '%s'", (unsigned long)tag_cache_bytes, path);
		custom_edition_cache_load_failure_reason(T("there is not enough memory for its tags"));
		custom_edition_cache_files_close();
		return NULL;
	}
#ifndef HALO_RELOCATABLE_TAG_CACHE
	if ((uint32_t)(unsigned long)tag_cache != custom_edition_cache_linked_address())
	{
		error(_error_silent, "custom edition: this build cannot move the tags of '%s' off 0x40440000", path);
		custom_edition_cache_load_failure_reason(T("this build cannot move its tags"));
		halo_custom_edition_tag_cache_release();
		custom_edition_cache_files_close();
		return NULL;
	}
#endif

	for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
	{
		char resource_path[MAP_PATH_SIZE];

		if (!custom_edition_resource_map_path(&identity, (enum resource_map_type)type, resource_path) ||
			!custom_edition_file_open(&globals->resource_files[type], resource_path))
		{
			error(_error_silent, "custom edition: no resource map '%s'", resource_path);
			continue;
		}
		status = resource_map_open(
			&globals->resource_files[type].source,
			(enum resource_map_type)type,
			&globals->resource_map_storage[type]);
		if (status != _cache_file_status_ok)
		{
			error(_error_silent, "custom edition: resource map '%s': %s", resource_path, cache_file_status_describe(status));
			continue;
		}
		globals->resource_maps[type] = &globals->resource_map_storage[type];
	}

	/* the combined offset space serves bitmaps.map and sounds.map after the
	map itself */
	if ((globals->resource_files[_resource_map_bitmaps].opened &&
		globals->resource_files[_resource_map_bitmaps].source.size > COMBINED_SOUNDS_OFFSET - COMBINED_BITMAPS_OFFSET) ||
		(globals->resource_files[_resource_map_sounds].opened &&
		globals->resource_files[_resource_map_sounds].source.size > COMBINED_OFFSET_LIMIT - COMBINED_SOUNDS_OFFSET))
	{
		error(_error_silent, "custom edition: a resource map is too large for this loader");
		custom_edition_cache_load_failure_reason(T("a resource map in the maps folder is too large"));
		custom_edition_cache_files_close();
		return NULL;
	}

	phases_length += custom_edition_load_phase_describe(&phase, "opening", phases + phases_length, sizeof(phases) - phases_length);
	custom_edition_load_progress_set(0.05f);
	/* the map's big reads on the load's reader, the CRC-32s the checksum
	takes of them with them, and the model data's left to the models'
	conversion (custom_edition_cache_model_data_crc) */
	{
		struct custom_edition_load_hooks hooks;

		csmemset(&hooks, 0, sizeof(hooks));
		hooks.source = &globals->map.source;
		hooks.read_crc = custom_edition_load_read_crc;
		hooks.defer_model_checksum = TRUE;
		custom_edition_cache_load_hooks(&hooks);
	}
	status = custom_edition_cache_load(
		&globals->map.source,
		globals->resource_maps,
		tag_cache,
		tag_cache_bytes,
		&report);
	custom_edition_cache_load_hooks(NULL);
	csmemset(&model_checksum, 0, sizeof(model_checksum));
	if (status == _cache_file_status_ok && report.checksum_deferred)
	{
		model_checksum.model_data_offset = report.model_data_offset;
		model_checksum.model_data_bytes = report.model_data_bytes;
		model_checksum.active = TRUE;
	}
	custom_edition_load_progress_set(0.45f);
	phases_length += custom_edition_load_phase_describe(
		&phase,
		", tags and checksum",
		phases + phases_length,
		sizeof(phases) - phases_length);
	if (status != _cache_file_status_ok)
	{
		error(
			_error_silent,
			"custom edition: cannot load '%s': %s (tag %ld, 0x%08lX)",
			path,
			cache_file_status_describe(status),
			(long)report.problem_tag_index,
			(unsigned long)report.problem_location);
		custom_edition_cache_load_failure_reason(custom_edition_cache_load_status_reason(status, &report));
		halo_custom_edition_tag_cache_release();
		custom_edition_cache_files_close();
		return NULL;
	}
	custom_edition_cache_report_log(&report);
	if (!custom_edition_cache_tags_convert(tag_cache, tag_cache_bytes, &report))
	{
		error(_error_silent, "custom edition: cannot run '%s'", path);
		custom_edition_cache_load_failure_reason(T("its tags could not be converted for this port"));
		custom_edition_models_dispose();
		custom_edition_bitmaps_dispose();
#ifdef HALO_RELOCATABLE_TAG_CACHE
		halo_tag_relocate_linked_release();
#endif
		custom_edition_cache_tags_moved(custom_edition_cache_linked_address());
		halo_custom_edition_tag_cache_release();
		custom_edition_cache_files_close();
		return NULL;
	}
	/* the checksum, now that the models' conversion has read the model
	data: what it did not read is read now */
	if (report.checksum_deferred)
	{
		enum { CHECKSUM_BUFFER_BYTES = 0x80000 };
		byte *buffer = halo_custom_edition_memory_alloc(CHECKSUM_BUFFER_BYTES);
		uint32_t model_data_crc;

		if (buffer && custom_edition_cache_model_data_crc(&report, buffer, CHECKSUM_BUFFER_BYTES, &model_data_crc))
		{
			custom_edition_cache_checksum_finish(&report, model_data_crc);
			error(
				_error_silent,
				"custom edition: checksum %s (0x%08lX)",
				TEST_FLAG(report.warnings, _custom_edition_warning_checksum_mismatch_bit) ? "mismatched" : "matched",
				(unsigned long)report.computed_checksum);
		}
		else
		{
			error(_error_silent, "custom edition: the model data could not be read for the checksum");
		}
		halo_custom_edition_memory_free(buffer);
	}
	model_checksum.active = FALSE;
	globals->tag_cache = tag_cache;
	globals->loaded_bytes = report.tag_data_bytes + report.resource_tag_bytes;
	globals->tag_cache_bytes = tag_cache_bytes;
	globals->tags_loaded = TRUE;
	custom_edition_cache_heap_log("loaded");
	custom_edition_load_phase_describe(&phase, ", conversion", phases + phases_length, sizeof(phases) - phases_length);
	error(_error_silent, "custom edition: load: %s", phases);

	return (struct cache_file_tag_header *)tag_cache;
}

struct cache_file_tag_header *custom_edition_cache_tags_load(
	char const *map_name,
	void *header)
{
	struct cache_file_tag_header *tag_header;

	/* the loading screen meanwhile, and the reader for the big reads */
	load_reader_start();
	custom_edition_load_progress = 0.0f;
	game_loading_screen_begin();
	tag_header = custom_edition_cache_tags_load_private(map_name, header);
	model_checksum.active = FALSE;
	custom_edition_cache_load_hooks(NULL);
	game_loading_screen_end();

	return tag_header;
}

void custom_edition_cache_load_failure_note(
	char const *reason)
{
	custom_edition_cache_load_failure_reason(reason);

	return;
}

/* port: an Xbox (or any) map the loader refuses as damaged or unsupported
(cache_files.c): recorded so main_new_map shows the player a message and
returns to the menu, as it does for a Custom Edition map, rather than the
game stopping fatally (which, for a downloaded map from a stranger, would
be a crash the host could cause). */
void halo_map_load_refused(
	char const *map_name,
	char const *reason)
{
	/* (a more precise reason recorded earlier for the same map stands) */
	if (custom_edition_load_failure.failed &&
		!csstrcasecmp(custom_edition_load_failure.map_name, tag_name_strip_path(map_name)))
	{
		return;
	}
	custom_edition_cache_load_failure_begin(map_name);
	/* (the loader's generic reasons, cache_files.c's and main.c's, in the
	player's language; the others come translated) */
	if (!csstrcmp(reason, "this map file is damaged or not supported"))
	{
		reason = T("this map file is damaged or not supported");
	}
	else if (!csstrcmp(reason, "out of memory: restart the game"))
	{
		reason = T("out of memory: restart the game");
	}
	custom_edition_cache_load_failure_reason(reason);

	return;
}

boolean custom_edition_cache_load_failure_show(
	char const *map_name)
{
	void platform_show_message(char const *title, char const *message);
	char message[480];

	if (!custom_edition_load_failure.failed ||
		csstrcasecmp(custom_edition_load_failure.map_name, tag_name_strip_path(map_name)))
	{
		return FALSE;
	}
	/* (an Xbox level's file, which could not be precached: cache_files.c;
	the menus' own map, ui, is no custom map either) */
	{
		boolean menus = !csstrcasecmp(custom_edition_load_failure.map_name, "ui");
		char const *title = menus ? T("the main menu") :
			custom_edition_maps_level_title(custom_edition_load_failure.map_name);
		boolean xbox_level = menus || csstrcmp(title, custom_edition_load_failure.map_name) != 0;

		snprintf(
			message,
			sizeof(message),
			xbox_level ? T("Couldn't load %s: %s.") : T("The custom map %s could not be loaded: %s."),
			title,
			custom_edition_load_failure.reason[0] ? custom_edition_load_failure.reason : T("see debug.txt"));
		error(_error_silent, "custom edition: %s", message);
		platform_show_message(xbox_level ? T("Halo: map") : T("Halo: custom map"), message);
	}
	custom_edition_load_failure.failed = FALSE;

	return TRUE;
}

boolean custom_edition_cache_model_data_submit(
	struct custom_edition_load_report const *report,
	unsigned long offset,
	unsigned long size,
	void *buffer,
	struct custom_edition_read_job *job)
{
	struct custom_edition_file const *map = &custom_edition_cache_globals.map;

	csmemset(job, 0, sizeof(*job));
	if (!map->opened ||
		offset > report->model_data_bytes ||
		size > report->model_data_bytes - offset)
	{
		job->state = _custom_edition_read_job_failed;
		return FALSE;
	}
	job->context = map->source.context;
	job->offset = report->model_data_offset + (uint32_t)offset;
	job->size = (uint32_t)size;
	job->buffer = buffer;
	job->crc_kind = _read_job_crc_model_data;
	custom_edition_read_job_submit(job);

	return TRUE;
}

boolean custom_edition_cache_model_data_read(
	struct custom_edition_load_report const *report,
	unsigned long offset,
	unsigned long size,
	void *buffer)
{
	struct custom_edition_read_job job;

	return custom_edition_cache_model_data_submit(report, offset, size, buffer, &job) &&
		custom_edition_read_job_wait(&job);
}

/* The CRC-32 from 0 of the whole model data of the map being loaded, into
`*crc`: the pieces the models' conversion read (model_checksum), and what
lies between them read now through `buffer`; or, should they have been too
many apart, all of it read again. FALSE when the map cannot be read. */
static boolean custom_edition_cache_model_data_crc(
	struct custom_edition_load_report const *report,
	byte *buffer,
	uint32_t buffer_bytes,
	uint32_t *crc)
{
	uint32_t model_data_bytes = report->model_data_bytes;
	uint32_t cursor;

	/* the first gap, a buffer of it at a time: each read joins the first
	piece (or is the first, from 0), so the pieces become one */
	while (!model_checksum.overflowed)
	{
		uint32_t gap_start;
		uint32_t gap_end;

		if (!model_checksum.count)
		{
			gap_start = 0;
			gap_end = model_data_bytes;
		}
		else if (model_checksum.pieces[0].start > 0)
		{
			gap_start = 0;
			gap_end = model_checksum.pieces[0].start;
		}
		else
		{
			gap_start = model_checksum.pieces[0].end;
			gap_end = model_checksum.count > 1 ? model_checksum.pieces[1].start : model_data_bytes;
		}
		if (gap_start >= gap_end)
		{
			break;
		}
		if (!custom_edition_cache_model_data_read(report, gap_start, MIN(gap_end - gap_start, buffer_bytes), buffer))
		{
			return FALSE;
		}
	}
	if (!model_data_bytes)
	{
		*crc = 0;
		return TRUE;
	}
	if (!model_checksum.overflowed &&
		model_checksum.count == 1 &&
		model_checksum.pieces[0].start == 0 &&
		model_checksum.pieces[0].end == model_data_bytes)
	{
		*crc = model_checksum.pieces[0].crc;
		return TRUE;
	}
	/* (too many pieces apart: all of it, in order) */
	error(_error_silent, "custom edition: the model data was read in too many pieces for its checksum; read again");
	model_checksum.active = FALSE;
	*crc = 0;
	for (cursor = 0; cursor < model_data_bytes; )
	{
		struct custom_edition_file const *map = &custom_edition_cache_globals.map;
		struct custom_edition_read_job job;
		uint32_t size = MIN(model_data_bytes - cursor, buffer_bytes);

		csmemset(&job, 0, sizeof(job));
		job.context = map->source.context;
		job.offset = report->model_data_offset + cursor;
		job.size = size;
		job.buffer = buffer;
		job.crc_kind = _read_job_crc_running;
		custom_edition_read_job_submit(&job);
		if (!custom_edition_read_job_wait(&job))
		{
			return FALSE;
		}
		*crc = cache_file_crc32_shift(*crc, size) ^ job.crc;
		cursor += size;
	}

	return TRUE;
}

boolean custom_edition_cache_tags_loaded(
	void)
{
	return custom_edition_cache_globals.tags_loaded;
}

void *custom_edition_cache_tag_cache(
	unsigned long *size)
{
	struct custom_edition_cache_globals *globals = &custom_edition_cache_globals;

	*size = globals->tags_loaded ? globals->tag_cache_bytes : 0;

	return globals->tags_loaded ? globals->tag_cache : NULL;
}

void custom_edition_cache_tags_unload(
	void)
{
	/* the structure BSP goes first, as scenario_structure_bsp_unload would
	have released it */
	custom_edition_structure_bsp_unload();
	custom_edition_models_dispose();
	custom_edition_bitmaps_dispose();
	/* (before the files: a decoding reads them) */
	custom_edition_sounds_stop();
	custom_edition_cache_files_close();
	custom_edition_cache_globals.tags_loaded = FALSE;
	custom_edition_cache_globals.tag_cache = NULL;
	custom_edition_cache_globals.loaded_bytes = 0;
	custom_edition_cache_globals.tag_cache_bytes = 0;
#ifdef HALO_RELOCATABLE_TAG_CACHE
	halo_tag_relocate_linked_release();
#endif
	custom_edition_cache_tags_moved(custom_edition_cache_linked_address());
	halo_custom_edition_tag_cache_release();

	return;
}

void custom_edition_cache_structure_bsp_moved(
	void *structure_bsp,
	long size)
{
#ifdef HALO_RELOCATABLE_TAG_CACHE
	struct custom_edition_cache_globals *globals = &custom_edition_cache_globals;

	if (globals->tags_loaded &&
		(uint32_t)(unsigned long)globals->tag_cache != custom_edition_cache_linked_address())
	{
		halo_tag_relocate_linked_structure_bsp(globals->tag_cache, structure_bsp, (unsigned long)size);
	}
#endif

	return;
}

boolean custom_edition_cache_read(
	long tag_index,
	long offset,
	long size,
	void *buffer)
{
	struct custom_edition_cache_globals *globals = &custom_edition_cache_globals;
	struct custom_edition_file *file;
	unsigned long file_offset;
	boolean read;

	if ((unsigned long)offset >= COMBINED_SOUNDS_OFFSET)
	{
		file = &globals->resource_files[_resource_map_sounds];
		file_offset = (unsigned long)offset - COMBINED_SOUNDS_OFFSET;
	}
	else if ((unsigned long)offset >= COMBINED_BITMAPS_OFFSET)
	{
		file = &globals->resource_files[_resource_map_bitmaps];
		file_offset = (unsigned long)offset - COMBINED_BITMAPS_OFFSET;
	}
	else
	{
		file = &globals->map;
		file_offset = (unsigned long)offset;
	}

	/* (January read its map from its main thread only; the port's tick
	and render threads both read: read_lock) */
	while (__atomic_load_n(&globals->read_lock, __ATOMIC_RELAXED) ||
		__atomic_exchange_n(&globals->read_lock, 1, __ATOMIC_ACQUIRE))
	{
		SwitchToThread();
	}
	read = file->opened && size >= 0;
	if (size > 0)
	{
		memory_watch_prepare_write(buffer, (unsigned long)size);
	}
	/* (straight into the reader's memory: the platform's file layer fills
	write-watched memory through a bounce buffer where pages are protected,
	and on the Vita in one request) */
	if (read && size > 0)
	{
		read = file->source.read(file->source.context, file_offset, (uint32_t)size, buffer);
	}
	if (!read)
	{
		/* the loader checked every range the tags give, so this is an I/O
		failure: the reader gets zeros rather than whatever was there */
		error(_error_silent, "custom edition: cannot read 0x%lX bytes at 0x%08lX", (unsigned long)size, (unsigned long)offset);
		if (size > 0)
		{
			csmemset(buffer, 0, size);
		}
	}
	else if (tag_index != NONE)
	{
		custom_edition_bitmap_pixels_arrived(globals->tag_cache, globals->loaded_bytes, tag_index, offset, buffer);
	}
	__atomic_store_n(&globals->read_lock, 0, __ATOMIC_RELEASE);

	return read;
}
