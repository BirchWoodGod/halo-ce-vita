/*
CUSTOM_EDITION_SOUNDS.C

The Ogg Vorbis sounds of Halo Custom Edition maps, played by this build's
sound cache as Xbox ADPCM (custom_edition_cache.h).

The loader makes an Ogg Vorbis sound an Xbox ADPCM one to the sound
manager, its channels and the mixer (cache_file_formats.c, sound_prepare:
the sound's compression, and a 44 kHz mono sound's rate, the only one the
game plays mono sounds at being 22 kHz), and leaves its permutations Ogg
Vorbis (compression 3), their samples the stream in the map or in
sounds.map. When the sound cache loads such a permutation
(xbox_sound_cache.c) it gives it a block of the Xbox ADPCM's size, and a
thread of its own here reads the stream, decodes it and writes the ADPCM
into the block (port/linux/src/ogg_sound.c), then marks the block loaded
as the cache file thread does a read: the game thread never decodes, and
never waits for a decoding.

The ADPCM's size is the stream's length, which the permutation's buffer
size gives (Halo PC's 16-bit PCM bytes; the loader's sample_buffer_size
becomes the ADPCM bytes). Protected maps scramble that field, so where the
loader found it implausible, the stream's first and last pages say it the
first time the permutation is loaded: the stream's first and last few
kilobytes read on the game thread (custom_edition_sound_cache_bytes), as
it reads a Custom Edition map's ADPCM sounds whole today. Either way the
size is only the space the decoding fills: what the stream decodes to past
it is cut off, and short of it is silence.

The decoder's working memory (384 KB: Halo PC's streams use 120-165 KB),
with 66 KB for measuring streams, is a memory block of its own
(halo_custom_edition_memory_alloc, not the C heap), taken for the first
Ogg Vorbis sound of a map and given back as the map goes
(custom_edition_sounds_stop). The shortest waiting decoding is done first,
so a weapon's sound is not kept behind the music the sound manager
prefetches (sound_manager.c, three links ahead). A block the cache deletes
while its decoding waits is forgotten, and while it runs the decoding is
cut short and waited for (custom_edition_sound_cancel): nothing is written
to a block after it has gone.

HALO_OGG_TRACE=1 logs each decoding: the sound, the frames, the decoded
samples' RMS and peak, the time and working memory it took, and where its
ADPCM is (run_ce_ogg_sound_test.sh). A permutation that does not decode is
logged once (a sound's first), whatever the setting, and plays as silence.
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "sound/sound_definitions.h"
#include "custom_edition_cache.h"
#include "../src/ogg_sound.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* the permutation's compression: Ogg Vorbis (sound_manager.c) */
	SOUND_PERMUTATION_COMPRESSION_OGG_VORBIS = 3,
	/* as many decodings can wait as the sound cache has blocks
	(xbox_sound_cache.c: 512) */
	MAXIMUM_WAITING_SOUNDS = 512,
	/* the stream's first bytes, and its last, read to measure it
	(custom_edition_sound_cache_bytes): first a few kilobytes of its end,
	then as many as an Ogg page can be (65307 bytes) */
	MEASURE_HEAD_BYTES = 512,
	MEASURE_TAIL_BYTES = 8192,
	MEASURE_LARGEST_TAIL_BYTES = 66 * 1024,
	/* the memory block: the decoder's working memory, then the measuring's
	(the sound cache's thread's) */
	MEMORY_BLOCK_BYTES = OGG_SOUND_WORKING_BYTES + MEASURE_LARGEST_TAIL_BYTES + MEASURE_HEAD_BYTES,
	/* the decoding thread's stack: Tremor's deepest use is ~32 KB */
	DECODER_STACK_BYTES = 256 * 1024,
};

/* sample_buffer_size of a permutation found unplayable */
#define SOUND_BUFFER_UNPLAYABLE 0xFFFFFFFFUL

/* ---------- structures */

struct waiting_sound
{
	/* the stream: in the combined space custom_edition_cache_read serves */
	unsigned long file_offset;
	uint32_t file_bytes;
	/* the cache block and its loaded flag */
	void *destination;
	uint32_t destination_bytes;
	boolean *loaded;
	int channels;
	long rate;
	/* the permutation, for the logs (its tags outlast the decoding:
	custom_edition_sounds_stop) */
	struct sound_permutation const *permutation;
};

/* ---------- globals */

void platform_log(const char *format, ...);
/* (every platform's microsecond clock) */
unsigned long long vita_host_time_us(void);
void *halo_custom_edition_memory_alloc(unsigned long bytes);
void halo_custom_edition_memory_free(void *address);
#ifdef HALO_VITA
/* (port/vita/host/vita_fourth_core.c) */
int vita_host_fourth_core_join(const char *role, int level);
#endif

static struct
{
	pthread_mutex_t lock;
	pthread_cond_t wake;
	pthread_cond_t done;
	boolean thread_started;
	boolean thread_failed;

	struct waiting_sound waiting[MAXIMUM_WAITING_SOUNDS];
	long waiting_count;

	/* the decoding being done, and whether it is to stop */
	boolean running;
	boolean *running_loaded;
	volatile boolean running_cancelled;

	/* the memory block (MEMORY_BLOCK_BYTES) */
	byte *memory;
	/* (decodings since the map came, and their time, for the log) */
	unsigned long decoded_count;
	unsigned long long decoded_microseconds;
} custom_edition_sounds_globals = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER };

static boolean failure_reported[MAXIMUM_WAITING_SOUNDS];

/* ---------- private code */

/* the memory block, taken the first time it is needed; NULL when there is
no room (with the lock held) */
static byte *memory_block(void)
{
	if (!custom_edition_sounds_globals.memory)
		custom_edition_sounds_globals.memory = (byte *)halo_custom_edition_memory_alloc(MEMORY_BLOCK_BYTES);
	return custom_edition_sounds_globals.memory;
}

static int trace_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
		enabled = getenv("HALO_OGG_TRACE") && atoi(getenv("HALO_OGG_TRACE"));
	return enabled;
}

/* (for the log: the game's builds have no libm of their own) */
static double log_square_root(double value)
{
	double root = value > 1.0 ? value : 1.0;
	int step;

	if (value <= 0.0)
		return 0.0;
	for (step = 0; step < 64; step++)
		root = 0.5 * (root + value / root);
	return root;
}

static boolean permutation_is_ogg_vorbis(
	struct sound_permutation const *permutation)
{
	return custom_edition_cache_tags_loaded() &&
		permutation->compression == SOUND_PERMUTATION_COMPRESSION_OGG_VORBIS;
}

/* the sound a permutation belongs to (its runtime tag index, which the
tag check made the sound's own: tag_schema_effects.c, sound_check) */
static struct sound_definition *permutation_definition(
	struct sound_permutation const *permutation)
{
	return (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, (long)permutation->unknown3);
}

/* the stream's bytes, read from the map's files for the decoder; the
decoding stops (a failed read) once the block is being deleted */
static int stream_read(void *context, uint32_t offset, uint32_t size, void *buffer)
{
	struct waiting_sound const *sound = (struct waiting_sound const *)context;

	if (custom_edition_sounds_globals.running_cancelled ||
		offset > sound->file_bytes || size > sound->file_bytes - offset)
	{
		return 0;
	}
	return custom_edition_cache_read(NONE, (long)(sound->file_offset + offset), (long)size, buffer);
}

static void sound_decode(struct waiting_sound *sound)
{
	struct ogg_sound_request request;
	struct ogg_sound_result result;
	unsigned long long started = vita_host_time_us();
	unsigned long long took;

	memset(&request, 0, sizeof(request));
	request.read = stream_read;
	request.read_context = sound;
	request.input_bytes = sound->file_bytes;
	request.output = sound->destination;
	request.output_bytes = sound->destination_bytes;
	request.channels = sound->channels;
	request.rate = sound->rate;
	request.working = custom_edition_sounds_globals.memory;
	request.working_bytes = OGG_SOUND_WORKING_BYTES;
	if (request.working)
	{
		ogg_sound_transcode(&request, &result);
	}
	else
	{
		/* (no working memory: the block is silence) */
		memset(sound->destination, 0, sound->destination_bytes);
		memset(&result, 0, sizeof(result));
		result.status = _ogg_sound_out_of_memory;
	}
	took = vita_host_time_us() - started;
	custom_edition_sounds_globals.decoded_count++;
	custom_edition_sounds_globals.decoded_microseconds += took;

	if (trace_enabled())
	{
		double samples = (double)result.frames_decoded * sound->channels;

		platform_log("ogg sound: %.32s of %s: %s, %lu frames %d ch %ld Hz%s (+%lu silent%s), rms %.0f peak %d, "
			"%.2f ms, %lu KB working, adpcm %lu bytes at %p",
			sound->permutation->name, tag_get_name((long)sound->permutation->unknown3),
			ogg_sound_status_describe(result.status), (unsigned long)result.frames_decoded,
			result.stream_channels, result.stream_rate, result.halved ? " halved" : "",
			(unsigned long)result.frames_padded, result.truncated ? ", cut off" : "",
			samples > 0 ? log_square_root(result.sum_of_squares / samples) : 0.0, result.peak,
			(double)took / 1000.0, (unsigned long)(result.working_peak_bytes / 1024),
			(unsigned long)sound->destination_bytes, sound->destination);
	}
	if (result.status != _ogg_sound_ok && !custom_edition_sounds_globals.running_cancelled)
	{
		long index = ((long)sound->permutation->unknown3 & 0xFFFF) % MAXIMUM_WAITING_SOUNDS;

		if (!failure_reported[index])
		{
			failure_reported[index] = TRUE;
			/* (platform_log: error() is the game thread's) */
			platform_log("custom edition: an Ogg Vorbis sound of %s (%.32s) does not decode: %s; it is silent",
				tag_get_name((long)sound->permutation->unknown3), sound->permutation->name,
				ogg_sound_status_describe(result.status));
		}
	}
	return;
}

static void *decoder_thread(void *parameter)
{
	struct waiting_sound sound;

	(void)parameter;
#ifdef HALO_VITA
	/* (Fourth core helpers, All async: with the cache file thread) */
	vita_host_fourth_core_join("ogg sound decoder", 2);
#endif
	pthread_mutex_lock(&custom_edition_sounds_globals.lock);
	for (;;)
	{
		long shortest = 0;
		long index;

		while (custom_edition_sounds_globals.waiting_count == 0)
			pthread_cond_wait(&custom_edition_sounds_globals.wake, &custom_edition_sounds_globals.lock);
		for (index = 1; index < custom_edition_sounds_globals.waiting_count; index++)
		{
			if (custom_edition_sounds_globals.waiting[index].destination_bytes <
				custom_edition_sounds_globals.waiting[shortest].destination_bytes)
			{
				shortest = index;
			}
		}
		sound = custom_edition_sounds_globals.waiting[shortest];
		custom_edition_sounds_globals.waiting[shortest] =
			custom_edition_sounds_globals.waiting[--custom_edition_sounds_globals.waiting_count];
		custom_edition_sounds_globals.running = TRUE;
		custom_edition_sounds_globals.running_loaded = sound.loaded;
		custom_edition_sounds_globals.running_cancelled = FALSE;
		pthread_mutex_unlock(&custom_edition_sounds_globals.lock);

		sound_decode(&sound);

		pthread_mutex_lock(&custom_edition_sounds_globals.lock);
		/* (the block is the cache's again: a cancelled one is being
		deleted, and its flag is not ours to set) */
		if (!custom_edition_sounds_globals.running_cancelled)
			__atomic_store_n(sound.loaded, TRUE, __ATOMIC_RELEASE);
		custom_edition_sounds_globals.running = FALSE;
		custom_edition_sounds_globals.running_loaded = NULL;
		pthread_cond_broadcast(&custom_edition_sounds_globals.done);
	}
	return NULL;
}

/* (with the lock held) */
static boolean decoder_thread_start(void)
{
	pthread_attr_t attributes;
	pthread_t thread;

	if (custom_edition_sounds_globals.thread_started)
		return TRUE;
	if (custom_edition_sounds_globals.thread_failed)
		return FALSE;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, DECODER_STACK_BYTES);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&thread, &attributes, decoder_thread, NULL) == 0)
	{
		custom_edition_sounds_globals.thread_started = TRUE;
	}
	else
	{
		custom_edition_sounds_globals.thread_failed = TRUE;
		error(_error_silent, "custom edition: no thread for Ogg Vorbis sounds; they are silent");
	}
	pthread_attr_destroy(&attributes);
	return custom_edition_sounds_globals.thread_started;
}

/* `bytes` bytes of the stream at `offset` into `buffer`, read on the
caller's thread */
static boolean stream_read_now(
	struct sound_permutation const *permutation,
	uint32_t offset,
	uint32_t bytes,
	void *buffer)
{
	return custom_edition_cache_read(NONE, (long)(permutation->samples.file_offset + offset), (long)bytes, buffer);
}

/* the ADPCM bytes of a stream its first and last pages describe, played
by the sound `definition`; 0 when they do not go together. Its first page
holds only the identification header (Vorbis I: 58 bytes); its last page
is looked for in its last few kilobytes, then in as many as a page can be
(a high quality stream's are tens of kilobytes) */
static uint32_t measured_adpcm_bytes(
	struct sound_permutation const *permutation,
	struct sound_definition const *definition)
{
	uint32_t size = (uint32_t)permutation->samples.size;
	int channels = definition->encoding == 1 ? 2 : 1;
	long rate = definition->sample_rate == 1 ? 44100 : 22050;
	struct ogg_sound_stream_info info;
	uint32_t tail_bytes = size < MEASURE_TAIL_BYTES ? size : MEASURE_TAIL_BYTES;
	uint32_t head_bytes = size < MEASURE_HEAD_BYTES ? size : MEASURE_HEAD_BYTES;
	uint32_t frames;
	uint32_t bytes;
	int found;
	byte *memory;
	byte *measure_tail;
	byte *measure_head;

	pthread_mutex_lock(&custom_edition_sounds_globals.lock);
	memory = memory_block();
	pthread_mutex_unlock(&custom_edition_sounds_globals.lock);
	if (!memory)
		return 0;
	measure_tail = memory + OGG_SOUND_WORKING_BYTES;
	measure_head = measure_tail + MEASURE_LARGEST_TAIL_BYTES;
	if (!size || !stream_read_now(permutation, size - tail_bytes, tail_bytes, measure_tail) ||
		(size > tail_bytes && !stream_read_now(permutation, 0, head_bytes, measure_head)))
	{
		return 0;
	}
	found = ogg_sound_measure(size > tail_bytes ? measure_head : measure_tail, head_bytes, measure_tail, tail_bytes, &info);
	if (!found && size > tail_bytes)
	{
		tail_bytes = size < MEASURE_LARGEST_TAIL_BYTES ? size : MEASURE_LARGEST_TAIL_BYTES;
		if (!stream_read_now(permutation, size - tail_bytes, tail_bytes, measure_tail))
			return 0;
		found = ogg_sound_measure(size > tail_bytes ? measure_head : measure_tail, head_bytes, measure_tail, tail_bytes,
			&info);
	}
	frames = found ? ogg_sound_output_frames(info.frames, info.rate, rate) : 0;
	if (!frames || info.channels < 1 || info.channels > 2)
		return 0;
	/* (more than the cache gives a permutation is cut off) */
	bytes = ogg_sound_adpcm_bytes(frames, channels);
	if (bytes > OGG_SOUND_MAXIMUM_ADPCM_BYTES)
		bytes = OGG_SOUND_MAXIMUM_ADPCM_BYTES / (OGG_SOUND_ADPCM_BLOCK_BYTES * channels) * (OGG_SOUND_ADPCM_BLOCK_BYTES * channels);
	return bytes;
}

/* ---------- public code */

boolean custom_edition_sound_is_ogg_vorbis(
	struct sound_permutation const *permutation)
{
	return permutation_is_ogg_vorbis(permutation);
}

long custom_edition_sound_cache_bytes(
	struct sound_permutation *permutation)
{
	if (!permutation_is_ogg_vorbis(permutation))
		return permutation->samples.size;
	if (permutation->sample_buffer_size == 0)
	{
		struct sound_definition const *definition = permutation_definition(permutation);
		uint32_t bytes = definition ? measured_adpcm_bytes(permutation, definition) : 0;

		permutation->sample_buffer_size = bytes ? bytes : SOUND_BUFFER_UNPLAYABLE;
		if (!bytes)
		{
			error(_error_silent, "custom edition: an Ogg Vorbis sound of %s (%.32s) has no length; it is not played",
				tag_get_name((long)permutation->unknown3), permutation->name);
		}
	}
	if (permutation->sample_buffer_size == SOUND_BUFFER_UNPLAYABLE ||
		permutation->sample_buffer_size > OGG_SOUND_MAXIMUM_ADPCM_BYTES)
	{
		return 0;
	}
	return (long)permutation->sample_buffer_size;
}

boolean custom_edition_sound_load(
	struct sound_permutation const *permutation,
	void *destination,
	long destination_bytes,
	boolean *loaded)
{
	struct sound_definition const *definition = permutation_definition(permutation);
	struct waiting_sound *sound;
	boolean queued = FALSE;

	pthread_mutex_lock(&custom_edition_sounds_globals.lock);
	if (definition && destination_bytes > 0 &&
		custom_edition_sounds_globals.waiting_count < MAXIMUM_WAITING_SOUNDS &&
		decoder_thread_start())
	{
		memory_block();
		sound = &custom_edition_sounds_globals.waiting[custom_edition_sounds_globals.waiting_count++];
		sound->file_offset = (unsigned long)permutation->samples.file_offset;
		sound->file_bytes = (uint32_t)permutation->samples.size;
		sound->destination = destination;
		sound->destination_bytes = (uint32_t)destination_bytes;
		sound->loaded = loaded;
		sound->channels = definition->encoding == 1 ? 2 : 1;
		sound->rate = definition->sample_rate == 1 ? 44100 : 22050;
		sound->permutation = permutation;
		*loaded = FALSE;
		queued = TRUE;
		pthread_cond_signal(&custom_edition_sounds_globals.wake);
	}
	pthread_mutex_unlock(&custom_edition_sounds_globals.lock);
	if (!queued)
	{
		/* (silence, at once: the cache's block is not left loading) */
		if (destination_bytes > 0)
			memset(destination, 0, (size_t)destination_bytes);
		__atomic_store_n(loaded, TRUE, __ATOMIC_RELEASE);
	}
	return queued;
}

void custom_edition_sound_cancel(
	boolean *loaded)
{
	long index;

	pthread_mutex_lock(&custom_edition_sounds_globals.lock);
	for (index = 0; index < custom_edition_sounds_globals.waiting_count; index++)
	{
		if (custom_edition_sounds_globals.waiting[index].loaded == loaded)
		{
			custom_edition_sounds_globals.waiting[index] =
				custom_edition_sounds_globals.waiting[--custom_edition_sounds_globals.waiting_count];
			break;
		}
	}
	if (custom_edition_sounds_globals.running && custom_edition_sounds_globals.running_loaded == loaded)
	{
		custom_edition_sounds_globals.running_cancelled = TRUE;
		while (custom_edition_sounds_globals.running && custom_edition_sounds_globals.running_loaded == loaded)
			pthread_cond_wait(&custom_edition_sounds_globals.done, &custom_edition_sounds_globals.lock);
	}
	pthread_mutex_unlock(&custom_edition_sounds_globals.lock);
	return;
}

void custom_edition_sounds_stop(
	void)
{
	pthread_mutex_lock(&custom_edition_sounds_globals.lock);
	custom_edition_sounds_globals.waiting_count = 0;
	if (custom_edition_sounds_globals.running)
	{
		custom_edition_sounds_globals.running_cancelled = TRUE;
		while (custom_edition_sounds_globals.running)
			pthread_cond_wait(&custom_edition_sounds_globals.done, &custom_edition_sounds_globals.lock);
	}
	if (custom_edition_sounds_globals.memory)
	{
		halo_custom_edition_memory_free(custom_edition_sounds_globals.memory);
		custom_edition_sounds_globals.memory = NULL;
	}
	if (custom_edition_sounds_globals.decoded_count)
	{
		platform_log("ogg sound: %lu Ogg Vorbis permutations decoded with the map, %.1f ms in all",
			custom_edition_sounds_globals.decoded_count,
			(double)custom_edition_sounds_globals.decoded_microseconds / 1000.0);
	}
	custom_edition_sounds_globals.decoded_count = 0;
	custom_edition_sounds_globals.decoded_microseconds = 0;
	memset(failure_reported, 0, sizeof(failure_reported));
	pthread_mutex_unlock(&custom_edition_sounds_globals.lock);
	return;
}
