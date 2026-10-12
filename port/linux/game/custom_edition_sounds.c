/*
CUSTOM_EDITION_SOUNDS.C

The Ogg Vorbis and 16-bit PCM sounds of Halo Custom Edition maps, played
by this build's sound cache as Xbox ADPCM (custom_edition_cache.h).

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

An Xbox ADPCM sound's 16-bit PCM permutations (compression none: Halo PC
plays them, and maps made with mods have them, extinction's scarab bolt
among them) go the same way: the loader leaves them PCM and makes their
buffer size the Xbox ADPCM's (cache_file_formats.c, sound_prepare), and
the thread here reads the PCM in pieces and encodes it
(xbox_adpcm_encoder.c), needing no working memory. Read as Xbox ADPCM they
were noise.

A 44 kHz mono sound of Xbox ADPCM or 16-bit PCM, which the game would not
play (mono sounds at 22 kHz only), the loader makes a 22 kHz one and marks
its permutations (CUSTOM_EDITION_PERMUTATION_*_HALVED, cache_file_formats.h)
with their buffer size the halved ADPCM's; the thread here reads their
samples in pieces (Xbox ADPCM decoded a block at a time,
xbox_adpcm_block_decode), takes them at half their rate and encodes them
(xbox_adpcm_rate, xbox_adpcm_encoder.c), as it does the Ogg Vorbis streams
(an Ogg Vorbis stream of another rate than its sound's is taken at the
sound's: ogg_sound.c). Nothing is decoded as the map loads: a permutation
is when the sound cache loads it, into the block it gives it.

A permutation longer than the sound cache gives one
(OGG_SOUND_MAXIMUM_ADPCM_BYTES: 42 s of 22 kHz stereo; a map's music) is
made a chain of permutations of ~256 KB each as the map loads
(custom_edition_sounds_map_loaded), as Halo's own music is: the pitch
range's permutations and the links are copied into a memory block of their
own, the range pointed at them, and the sound marked as having linked
permutations, so the sound manager plays the links in turn, asks for the
next ones ahead, and loops back to the first only after the last. Each
link is made Xbox ADPCM as the cache loads it: a 16-bit PCM one read from
its first frame, an Ogg Vorbis one in a stream decoder (working memory of
its own, two at most) taking up the decoding where the link before kept it
(ogg_sound_request.keep), the links of a stream decoded in order; else from
the stream's start.

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
#include "cache/cache_files.h"
#include "sound/sound_definitions.h"
#include "custom_edition_cache.h"
#include "cache_file_formats.h"
#include "../src/ogg_sound.h"
#include "../src/xbox_adpcm_encoder.h"
#include "../src/port_config.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* the permutation's compressions made Xbox ADPCM here: 16-bit PCM and
	Ogg Vorbis (sound_manager.c) */
	SOUND_PERMUTATION_COMPRESSION_PCM = 0,
	SOUND_PERMUTATION_COMPRESSION_OGG_VORBIS = 3,
	/* 16-bit PCM is read this many bytes at a time (whole frames) */
	PCM_READ_BYTES = 16 * 1024,
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

	/* a permutation of more Xbox ADPCM than the sound cache gives one
	(OGG_SOUND_MAXIMUM_ADPCM_BYTES) is made a chain of links of about this
	much each, of equal length: the sound manager asks for three links
	ahead of the one playing (sound_manager.c), which with the one playing
	take about the cache the cut-off permutation took (1 MB); 6 s of 22 kHz
	stereo, 3 s of 44 kHz */
	CHAIN_LINK_BYTES = 256 * 1024,
	/* the most of a permutation made a chain (the rest is cut off): 11 min
	of 44 kHz stereo, 45 of 22 kHz stereo */
	CHAIN_MAXIMUM_ADPCM_BYTES = 64 * 1024 * 1024,
	/* the most links a map's chains add (124 bytes each) */
	CHAIN_MAXIMUM_LINKS = 4096,
	/* the most permutations a pitch range may have (next_permutation_index
	is a short) */
	CHAIN_MAXIMUM_RANGE_PERMUTATIONS = 32767,
	/* an Ogg Vorbis permutation the loader found no plausible length for
	(none, or more than a permutation holds: Halo PC's word for a long
	sound, or a protected map's scrambled one) is measured as the map loads
	when its stream is this big or bigger: a stream that long a
	permutation cannot hold is (Vorbis is a twelfth of Xbox ADPCM at the
	least, but for near silence); a smaller one is measured as it first
	loads, as before, and played at most as long as a permutation holds */
	CHAIN_MEASURE_STREAM_BYTES = 32 * 1024,
	/* the Ogg Vorbis chains whose decoding is kept between their links
	(ogg_sound_request.keep), each in working memory of its own: the
	music and a long sound over it (more alternate, each link of theirs
	then decoded from the stream's start) */
	STREAM_DECODERS = 2,
};

/* the first frame of a permutation of the chains' table that is not a
link (one of a pitch range copied there with them) */
#define CHAIN_NOT_A_LINK 0xFFFFFFFFUL

/* sample_buffer_size of a permutation found unplayable */
#define SOUND_BUFFER_UNPLAYABLE 0xFFFFFFFFUL

/* ---------- structures */

enum transcoding
{
	_transcoding_none,
	_transcoding_ogg_vorbis,
	_transcoding_pcm,
	/* (a 44 kHz mono sound's, made 22 kHz: CUSTOM_EDITION_PERMUTATION_*_HALVED) */
	_transcoding_pcm_halved,
	_transcoding_adpcm_halved
};

struct waiting_sound
{
	enum transcoding transcoding;
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
	/* a link of a chain: its first frame in its sound's Xbox ADPCM (0 for
	any other permutation), its index and the chain's links (for the logs),
	and whether it is an Ogg Vorbis link, decoded in a stream decoder */
	boolean link;
	uint32_t first_frame;
	long link_index;
	long link_count;
	boolean stream_link;
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

	/* the chains (custom_edition_sounds_map_loaded), in a memory block of
	their own: the stream decoders' working memory (OGG_SOUND_WORKING_BYTES
	each), then the permutations of the pitch ranges given chains (their
	own and their links), then each one's first frame in its sound
	(CHAIN_NOT_A_LINK: not a link), its index in its chain and the chain's
	links */
	byte *chain_memory;
	struct sound_permutation *chain_permutations;
	uint32_t *chain_first_frames;
	short *chain_link_indices;
	short *chain_link_counts;
	long chain_permutation_count;
	byte *stream_working[STREAM_DECODERS];
	unsigned long stream_working_used[STREAM_DECODERS];
	long stream_decoder_count;
	unsigned long stream_uses;
	/* (links made Xbox ADPCM since the map came, and of them those whose
	stream decoding was taken up where the link before left it) */
	unsigned long link_decoded_count;
	unsigned long link_resumed_count;
	/* (decodings since the map came, and their time, for the log: Ogg
	Vorbis and 16-bit PCM) */
	unsigned long decoded_count;
	unsigned long long decoded_microseconds;
	unsigned long encoded_count;
	unsigned long long encoded_microseconds;
} custom_edition_sounds_globals = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER };

static boolean failure_reported[MAXIMUM_WAITING_SOUNDS];
/* (the decoding thread's: a piece of 16-bit PCM) */
static byte pcm_piece[PCM_READ_BYTES];

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

/* how the sound cache loads a permutation: read (Xbox ADPCM), or made Xbox
ADPCM here */
static enum transcoding permutation_transcoding(
	struct sound_permutation const *permutation)
{
	if (!custom_edition_cache_tags_loaded())
		return _transcoding_none;
	if (permutation->compression == SOUND_PERMUTATION_COMPRESSION_OGG_VORBIS)
		return _transcoding_ogg_vorbis;
	if (permutation->compression == SOUND_PERMUTATION_COMPRESSION_PCM)
		return _transcoding_pcm;
	if (permutation->compression == CUSTOM_EDITION_PERMUTATION_PCM_HALVED)
		return _transcoding_pcm_halved;
	if (permutation->compression == CUSTOM_EDITION_PERMUTATION_XBOX_ADPCM_HALVED)
		return _transcoding_adpcm_halved;
	return _transcoding_none;
}

/* the sound a permutation belongs to (its runtime tag index, which the
tag check made the sound's own: tag_schema_effects.c, sound_check) */
static struct sound_definition *permutation_definition(
	struct sound_permutation const *permutation)
{
	return (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, (long)permutation->unknown3);
}

static int definition_channels(struct sound_definition const *definition)
{
	return definition->encoding == 1 ? 2 : 1;
}

static long definition_rate(struct sound_definition const *definition)
{
	return definition->sample_rate == 1 ? 44100 : 22050;
}

/* the permutation's place in the chains' table, or NONE */
static long chain_index(
	struct sound_permutation const *permutation)
{
	uintptr_t first = (uintptr_t)custom_edition_sounds_globals.chain_permutations;
	uintptr_t address = (uintptr_t)permutation;

	if (!first || address < first ||
		address >= first + (uintptr_t)custom_edition_sounds_globals.chain_permutation_count * sizeof(struct sound_permutation))
	{
		return NONE;
	}
	return (long)((address - first) / sizeof(struct sound_permutation));
}

/* whether the permutation is a link of a chain (its first included), and
its first frame in its sound */
static boolean chain_link(
	struct sound_permutation const *permutation,
	uint32_t *first_frame)
{
	long index = chain_index(permutation);

	if (index == NONE || custom_edition_sounds_globals.chain_first_frames[index] == CHAIN_NOT_A_LINK)
		return FALSE;
	if (first_frame)
		*first_frame = custom_edition_sounds_globals.chain_first_frames[index];
	return TRUE;
}

/* the Xbox ADPCM bytes the whole of a permutation is made (for a long one
more than the sound cache gives a permutation); 0 when that is not known
(an Ogg Vorbis permutation not measured) or it is muted (16-bit PCM not of
whole frames: the loader's buffer size 0) */
static uint32_t permutation_whole_bytes(
	struct sound_permutation const *permutation,
	struct sound_definition const *definition)
{
	enum transcoding transcoding = permutation_transcoding(permutation);
	int channels = definition_channels(definition);
	uint32_t size = permutation->samples.size > 0 ? (uint32_t)permutation->samples.size : 0;
	uint32_t frames;

	if (transcoding == _transcoding_ogg_vorbis)
		return permutation->sample_buffer_size == SOUND_BUFFER_UNPLAYABLE ? 0 : (uint32_t)permutation->sample_buffer_size;
	if (transcoding == _transcoding_none || !permutation->sample_buffer_size || !size)
		return 0;
	if (transcoding == _transcoding_adpcm_halved)
	{
		/* (64 frames a block, halved: 32) */
		frames = size / (XBOX_ADPCM_BLOCK_BYTES * (uint32_t)channels) * (XBOX_ADPCM_BLOCK_SAMPLES / 2);
	}
	else
	{
		frames = size / (2 * (uint32_t)channels);
		if (transcoding == _transcoding_pcm_halved)
			frames = frames / 2 + frames % 2;
	}
	return xbox_adpcm_bytes(frames, channels);
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

/* the logs of a decoding (or encoding) of `sound`, which took `took`
microseconds */
static void sound_decode_report(
	struct waiting_sound const *sound,
	struct ogg_sound_result const *result,
	unsigned long long took)
{
	boolean pcm = sound->transcoding == _transcoding_pcm || sound->transcoding == _transcoding_pcm_halved;
	boolean adpcm = sound->transcoding == _transcoding_adpcm_halved;

	if (trace_enabled())
	{
		double samples = (double)result->frames_decoded * sound->channels;
		char link[96];

		/* (a link: which, from which frame, and how its frames were come to;
		after the ADPCM's place, which the tests read) */
		link[0] = '\0';
		if (sound->link)
		{
			snprintf(link, sizeof(link), ", link %ld of %ld from frame %lu, %s %lu frames", sound->link_index + 1,
				sound->link_count, (unsigned long)sound->first_frame,
				result->resumed ? "resumed skipping" : "from the start skipping", (unsigned long)result->frames_skipped);
		}
		platform_log("%s sound: %.32s of %s: %s, %lu frames %d ch %ld Hz%s (+%lu silent%s), rms %.0f peak %d, "
			"%.2f ms, %lu KB working, adpcm %lu bytes at %p%s",
			pcm ? "pcm" : adpcm ? "adpcm" : "ogg", sound->permutation->name, tag_get_name((long)sound->permutation->unknown3),
			ogg_sound_status_describe(result->status), (unsigned long)result->frames_decoded,
			result->stream_channels, result->stream_rate, result->halved ? " halved" : "",
			(unsigned long)result->frames_padded, result->truncated ? (sound->link ? ", more follows" : ", cut off") : "",
			samples > 0 ? log_square_root(result->sum_of_squares / samples) : 0.0, result->peak,
			(double)took / 1000.0, (unsigned long)(result->working_peak_bytes / 1024),
			(unsigned long)sound->destination_bytes, sound->destination, link);
	}
	if (result->status != _ogg_sound_ok && !custom_edition_sounds_globals.running_cancelled)
	{
		long index = ((long)sound->permutation->unknown3 & 0xFFFF) % MAXIMUM_WAITING_SOUNDS;

		if (!failure_reported[index])
		{
			failure_reported[index] = TRUE;
			/* (platform_log: error() is the game thread's) */
			platform_log("custom edition: %s sound of %s (%.32s) does not decode: %s; it is silent",
				pcm ? "a 16-bit PCM" : adpcm ? "a 44 kHz Xbox ADPCM" : "an Ogg Vorbis", tag_get_name((long)sound->permutation->unknown3),
				sound->permutation->name, ogg_sound_status_describe(result->status));
		}
	}
	return;
}

/* the frames of a piece of 16-bit PCM (whole frames) or of Xbox ADPCM
(whole blocks), taken at the output's rate */
static void piece_frames(struct waiting_sound const *sound, struct xbox_adpcm_rate *rate, uint32_t size)
{
	int channels = sound->channels;
	int values[XBOX_ADPCM_MAXIMUM_CHANNELS];
	uint32_t offset;
	int channel;

	if (sound->transcoding == _transcoding_adpcm_halved)
	{
		uint32_t block_bytes = XBOX_ADPCM_BLOCK_BYTES * (uint32_t)channels;

		for (offset = 0; offset + block_bytes <= size && !rate->encoder->full; offset += block_bytes)
		{
			short frames[XBOX_ADPCM_BLOCK_SAMPLES * XBOX_ADPCM_MAXIMUM_CHANNELS];
			int frame;

			xbox_adpcm_block_decode(pcm_piece + offset, channels, frames);
			for (frame = 0; frame < XBOX_ADPCM_BLOCK_SAMPLES; frame++)
			{
				for (channel = 0; channel < channels; channel++)
					values[channel] = frames[frame * channels + channel];
				xbox_adpcm_rate_frame(rate, values);
			}
		}
	}
	else
	{
		for (offset = 0; offset + 2 * (uint32_t)channels <= size && !rate->encoder->full; offset += 2 * (uint32_t)channels)
		{
			for (channel = 0; channel < channels; channel++)
				values[channel] = (int16_t)(uint16_t)(pcm_piece[offset + 2 * channel] | pcm_piece[offset + 2 * channel + 1] << 8);
			xbox_adpcm_rate_frame(rate, values);
		}
	}
	/* (the block full before the piece's end: the rest left out) */
	if (offset < size && rate->encoder->full)
		rate->encoder->truncated = 1;
}

/* a 16-bit PCM permutation encoded into its block, read in pieces of
whole frames (a halved one's, or Xbox ADPCM's, of whole blocks, decoded and
taken at half their rate): what the samples do not fill is silence, and
what does not fit is left out (the block is the size the loader worked out
from them). A link of a chain is read from the frame (or block) its first
frame is made from, the halving's frame before it included, and the frames
made before its first are left out: the same frames as a whole decoding's
there */
static void pcm_encode(struct waiting_sound *sound)
{
	struct xbox_adpcm_encoder encoder;
	struct xbox_adpcm_rate rate;
	struct ogg_sound_result result;
	unsigned long long started = vita_host_time_us();
	unsigned long long took;
	boolean halved = sound->transcoding != _transcoding_pcm;
	uint32_t unit_bytes = sound->transcoding == _transcoding_adpcm_halved ?
		XBOX_ADPCM_BLOCK_BYTES * (uint32_t)sound->channels : 2 * (uint32_t)sound->channels;
	uint32_t unit_frames = sound->transcoding == _transcoding_adpcm_halved ? XBOX_ADPCM_BLOCK_SAMPLES : 1;
	uint32_t piece_bytes = PCM_READ_BYTES / unit_bytes * unit_bytes;
	uint32_t offset = 0;
	uint32_t skip = 0;
	uint32_t warm = 0;

	memset(&result, 0, sizeof(result));
	result.status = _ogg_sound_ok;
	result.stream_channels = sound->channels;
	result.stream_rate = halved ? 2 * sound->rate : sound->rate;
	result.halved = halved;
	if (sound->first_frame)
	{
		uint64_t start = xbox_adpcm_rate_start(sound->first_frame, halved, unit_frames, &skip, &warm) / unit_frames * unit_bytes;

		offset = start < sound->file_bytes ? (uint32_t)start : sound->file_bytes;
	}
	result.frames_skipped = skip + warm;
	xbox_adpcm_encoder_begin(&encoder, sound->destination, sound->destination_bytes, sound->channels);
	encoder.skip_frames = skip;
	encoder.warm_frames = warm;
	if (!xbox_adpcm_rate_begin(&rate, &encoder, result.stream_rate, sound->rate))
		result.status = _ogg_sound_unsupported_format;
	while (result.status == _ogg_sound_ok && offset < sound->file_bytes && !encoder.full)
	{
		uint32_t size = sound->file_bytes - offset < piece_bytes ? sound->file_bytes - offset : piece_bytes;

		if (!stream_read(sound, offset, size, pcm_piece))
		{
			result.status = _ogg_sound_read_failed;
			break;
		}
		piece_frames(sound, &rate, size);
		offset += size;
	}
	if (offset < sound->file_bytes)
		encoder.truncated = 1;
	result.frames_skipped -= encoder.skip_frames + encoder.warm_frames;
	xbox_adpcm_rate_finish(&rate);
	result.frames_padded = xbox_adpcm_encoder_finish(&encoder);
	result.frames_decoded = encoder.frames;
	result.truncated = encoder.truncated && result.status == _ogg_sound_ok;
	result.sum_of_squares = encoder.sum_of_squares;
	result.peak = encoder.peak;
	took = vita_host_time_us() - started;
	custom_edition_sounds_globals.encoded_count++;
	custom_edition_sounds_globals.encoded_microseconds += took;
	sound_decode_report(sound, &result, took);
	return;
}

/* the stream an Ogg Vorbis link is of, for its decoding kept between
links (ogg_sound_request.stream_key): its place and size */
static uint64_t waiting_stream_key(struct waiting_sound const *sound)
{
	return (uint64_t)sound->file_bytes << 32 | (uint32_t)sound->file_offset;
}

/* the stream decoder for an Ogg Vorbis link: the one whose kept decoding
of its stream goes on at or before its first frame, else the one used
longest ago; NULL without one (the decoding thread's) */
static byte *stream_decoder(struct waiting_sound const *sound)
{
	uint64_t key = waiting_stream_key(sound);
	long chosen = NONE;
	long index;

	for (index = 0; index < custom_edition_sounds_globals.stream_decoder_count; index++)
	{
		uint32_t next_frame;

		if (ogg_sound_kept(custom_edition_sounds_globals.stream_working[index], OGG_SOUND_WORKING_BYTES, key, &next_frame) &&
			next_frame <= sound->first_frame)
		{
			chosen = index;
			break;
		}
		if (chosen == NONE ||
			custom_edition_sounds_globals.stream_working_used[index] < custom_edition_sounds_globals.stream_working_used[chosen])
		{
			chosen = index;
		}
	}
	if (chosen == NONE)
		return NULL;
	custom_edition_sounds_globals.stream_working_used[chosen] = ++custom_edition_sounds_globals.stream_uses;
	return custom_edition_sounds_globals.stream_working[chosen];
}

static void sound_decode(struct waiting_sound *sound)
{
	struct ogg_sound_request request;
	struct ogg_sound_result result;
	unsigned long long started = vita_host_time_us();
	unsigned long long took;

	if (sound->link)
		custom_edition_sounds_globals.link_decoded_count++;
	if (sound->transcoding != _transcoding_ogg_vorbis)
	{
		pcm_encode(sound);
		return;
	}
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
	/* (a link: its frames from its first; an Ogg Vorbis one's decoding
	kept for the next link, and taken up from the link before where that
	kept it, in a stream decoder: else decoded from the stream's start) */
	request.first_frame = sound->first_frame;
	if (sound->stream_link)
	{
		byte *working = stream_decoder(sound);

		if (working)
		{
			request.working = working;
			request.keep = TRUE;
			request.stream_key = waiting_stream_key(sound);
		}
	}
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
	if (result.resumed)
		custom_edition_sounds_globals.link_resumed_count++;

	sound_decode_report(sound, &result, took);
	return;
}

/* whether the waiting decoding `index` may be done next: an Ogg Vorbis
link may not while a link of the same stream from an earlier frame waits
(the links of a chain are decoded in order, each taking up the decoding
the one before kept; with the lock held) */
static boolean waiting_ready(long index)
{
	struct waiting_sound const *sound = &custom_edition_sounds_globals.waiting[index];
	long other;

	if (!sound->stream_link)
		return TRUE;
	for (other = 0; other < custom_edition_sounds_globals.waiting_count; other++)
	{
		struct waiting_sound const *earlier = &custom_edition_sounds_globals.waiting[other];

		if (earlier->stream_link && earlier->first_frame < sound->first_frame &&
			earlier->file_offset == sound->file_offset && earlier->file_bytes == sound->file_bytes)
		{
			return FALSE;
		}
	}
	return TRUE;
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
		long shortest = NONE;
		long index;

		while (custom_edition_sounds_globals.waiting_count == 0)
			pthread_cond_wait(&custom_edition_sounds_globals.wake, &custom_edition_sounds_globals.lock);
		/* (of those that may be done now: one always may, the earliest
		waiting link of each stream) */
		for (index = 0; index < custom_edition_sounds_globals.waiting_count; index++)
		{
			if ((shortest == NONE ||
				custom_edition_sounds_globals.waiting[index].destination_bytes <
					custom_edition_sounds_globals.waiting[shortest].destination_bytes) &&
				waiting_ready(index))
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
by the sound `definition` (all of them: more than the sound cache gives a
permutation, for a long sound); 0 when they do not go together. Its first page
holds only the identification header (Vorbis I: 58 bytes); its last page
is looked for in its last few kilobytes, then in as many as a page can be
(a high quality stream's are tens of kilobytes) */
static uint32_t measured_adpcm_bytes(
	struct sound_permutation const *permutation,
	struct sound_definition const *definition)
{
	uint32_t size = (uint32_t)permutation->samples.size;
	int channels = definition_channels(definition);
	long rate = definition_rate(definition);
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
	bytes = ogg_sound_adpcm_bytes(frames, channels);
	return bytes;
}

/* ---------- public code */

boolean custom_edition_sound_is_transcoded(
	struct sound_permutation const *permutation)
{
	return permutation_transcoding(permutation) != _transcoding_none;
}

long custom_edition_sound_cache_bytes(
	struct sound_permutation *permutation)
{
	enum transcoding transcoding = permutation_transcoding(permutation);

	if (transcoding == _transcoding_none)
		return permutation->samples.size;
	/* (16-bit PCM, and a halved permutation: the loader's size, 0 when it
	was not whole frames) */
	if (transcoding != _transcoding_ogg_vorbis)
	{
		return permutation->sample_buffer_size <= OGG_SOUND_MAXIMUM_ADPCM_BYTES ?
			(long)permutation->sample_buffer_size : 0;
	}
	if (permutation->sample_buffer_size == 0)
	{
		struct sound_definition const *definition = permutation_definition(permutation);
		uint32_t bytes = definition ? measured_adpcm_bytes(permutation, definition) : 0;
		uint32_t block_bytes = definition ? OGG_SOUND_ADPCM_BLOCK_BYTES * (uint32_t)definition_channels(definition) : 1;

		/* (more than the cache gives a permutation is cut off: a long sound
		is made a chain as the map loads, custom_edition_sounds_map_loaded,
		unless its stream was too small to be measured then) */
		if (bytes > OGG_SOUND_MAXIMUM_ADPCM_BYTES)
			bytes = OGG_SOUND_MAXIMUM_ADPCM_BYTES / block_bytes * block_bytes;
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

boolean custom_edition_sound_is_chained(
	struct sound_permutation const *permutation)
{
	return chain_link(permutation, NULL);
}

/* a permutation's chain: its links and the blocks of each but the last
(whole Xbox ADPCM blocks of `channels` channels, of equal length but the
last: the loop comes back to the first link when the last starts, so a last
link of a few blocks would leave the first no time to load), for all of
its `whole_bytes` up to CHAIN_MAXIMUM_ADPCM_BYTES; 0 links when it needs
no chain */
static long chain_layout(
	uint32_t whole_bytes,
	int channels,
	uint32_t *link_blocks)
{
	uint32_t block_bytes = OGG_SOUND_ADPCM_BLOCK_BYTES * (uint32_t)channels;
	uint32_t blocks;
	uint32_t links;

	*link_blocks = 0;
	if (whole_bytes <= OGG_SOUND_MAXIMUM_ADPCM_BYTES)
		return 0;
	if (whole_bytes > CHAIN_MAXIMUM_ADPCM_BYTES)
		whole_bytes = CHAIN_MAXIMUM_ADPCM_BYTES;
	blocks = whole_bytes / block_bytes;
	links = (blocks * block_bytes + CHAIN_LINK_BYTES - 1) / CHAIN_LINK_BYTES;
	*link_blocks = (blocks + links - 1) / links;
	return (long)((blocks + *link_blocks - 1) / *link_blocks);
}

/* the links a pitch range's chains add to it (0 when it has none, or they
would make it more than a range may have) */
static long range_links_added(
	struct sound_pitch_range const *range,
	struct sound_definition const *definition)
{
	long added = 0;
	long index;

	for (index = 0; index < range->permutations.count; index++)
	{
		struct sound_permutation const *permutation =
			(struct sound_permutation const *)range->permutations.address + index;
		uint32_t link_blocks;
		long links = chain_layout(permutation_whole_bytes(permutation, definition), definition_channels(definition),
			&link_blocks);

		if (links > 1)
			added += links - 1;
	}
	return range->permutations.count + added > CHAIN_MAXIMUM_RANGE_PERMUTATIONS ? 0 : added;
}

/* whether the map's sound `definition` and its pitch range `range` are
ones whose chains are made (the same answer as the map is planned and as
the chains are made: custom_edition_sounds_map_loaded) */
static boolean range_chained(
	struct sound_pitch_range const *range,
	struct sound_definition const *definition,
	long *links_so_far)
{
	long added = range_links_added(range, definition);

	if (!added || *links_so_far + added > CHAIN_MAXIMUM_LINKS)
		return FALSE;
	*links_so_far += added;
	return TRUE;
}

/* the permutations of the pitch range `range` of `definition` (of the
sound `tag_index`) and their chains' links, written at `permutations`
(the chains' table from `table_index`), which the range then has; returns
how many */
static long range_chains_make(
	struct sound_pitch_range *range,
	struct sound_definition const *definition,
	long tag_index,
	long table_index)
{
	struct sound_permutation const *own = (struct sound_permutation const *)range->permutations.address;
	struct sound_permutation *permutations = custom_edition_sounds_globals.chain_permutations + table_index;
	long count = range->permutations.count;
	int channels = definition_channels(definition);
	long rate = definition_rate(definition);
	boolean linked = TEST_FLAG(definition->flags, _sound_definition_linked_permutations_bit);
	long index;

	memcpy(permutations, own, (size_t)count * sizeof(*permutations));
	for (index = 0; index < count; index++)
		custom_edition_sounds_globals.chain_first_frames[table_index + index] = CHAIN_NOT_A_LINK;
	for (index = 0; index < range->permutations.count; index++)
	{
		uint32_t whole_bytes = permutation_whole_bytes(&own[index], definition);
		uint32_t block_bytes = OGG_SOUND_ADPCM_BLOCK_BYTES * (uint32_t)channels;
		uint32_t link_blocks;
		long links = chain_layout(whole_bytes, channels, &link_blocks);
		uint32_t blocks = (whole_bytes > CHAIN_MAXIMUM_ADPCM_BYTES ? CHAIN_MAXIMUM_ADPCM_BYTES : whole_bytes) / block_bytes;
		/* (the chain goes on with what the permutation was followed by, in
		a sound that had chains of its own) */
		short after = linked ? own[index].next_permutation_index : NONE;
		long link;

		if (links < 2)
			continue;
		for (link = 0; link < links; link++)
		{
			long link_index = link ? count + link - 1 : index;
			struct sound_permutation *permutation = &permutations[link_index];
			uint32_t first_block = (uint32_t)link * link_blocks;
			uint32_t link_frames;

			*permutation = own[index];
			permutation->sample_buffer_size = MIN(link_blocks, blocks - first_block) * block_bytes;
			permutation->next_permutation_index = link + 1 < links ? (short)(count + link) : after;
			custom_edition_sounds_globals.chain_first_frames[table_index + link_index] = first_block * OGG_SOUND_ADPCM_BLOCK_SAMPLES;
			custom_edition_sounds_globals.chain_link_indices[table_index + link_index] = (short)link;
			custom_edition_sounds_globals.chain_link_counts[table_index + link_index] = (short)links;
			/* the mouth's 30 ticks a second, from the link's first */
			link_frames = MIN(link_blocks, blocks - first_block) * OGG_SOUND_ADPCM_BLOCK_SAMPLES;
			if (own[index].mouth_data.size > 0 && own[index].mouth_data.address)
			{
				long first_tick = (long)((uint64_t)first_block * OGG_SOUND_ADPCM_BLOCK_SAMPLES * 30 / (uint64_t)rate);
				long ticks = (long)(((uint64_t)link_frames * 30 + (uint64_t)rate - 1) / (uint64_t)rate);

				if (first_tick > own[index].mouth_data.size - 1)
					first_tick = own[index].mouth_data.size - 1;
				permutation->mouth_data.address = (byte *)own[index].mouth_data.address + first_tick;
				permutation->mouth_data.size = MAX(1, MIN(ticks, own[index].mouth_data.size - first_tick));
			}
		}
		platform_log("custom edition: %s (%.32s) is %lu frames of %d ch %ld Hz (%lu KB of Xbox ADPCM), played as a chain "
			"of %ld permutations of %lu KB%s",
			tag_get_name(tag_index), own[index].name, (unsigned long)(blocks * OGG_SOUND_ADPCM_BLOCK_SAMPLES), channels, rate,
			(unsigned long)(whole_bytes / 1024), links, (unsigned long)(link_blocks * block_bytes / 1024),
			whole_bytes > CHAIN_MAXIMUM_ADPCM_BYTES ? " (the rest cut off)" : "");
		count += links - 1;
	}
	return count;
}

void custom_edition_sounds_map_loaded(
	void)
{
	struct tag_iterator iterator;
	long tag_index;
	long links_so_far = 0;
	long table_count = 0;
	long stream_chains = 0;
	long chains = 0;
	unsigned long table_bytes;
	unsigned long bytes;
	long used;
	long index;

	/* (a map without sound plays none: the dedicated server) */
	if (!custom_edition_cache_tags_loaded() || !config_boolean("audio.enabled") ||
		custom_edition_sounds_globals.chain_memory)
	{
		return;
	}
	/* what the chains need: the lengths the loader could not give of
	streams that may be long measured now, as they would be as they first
	load */
	tag_iterator_new(&iterator, SOUND_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct sound_definition *definition = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, tag_index);
		long range_index;

		for (range_index = 0; range_index < definition->pitch_ranges.count; range_index++)
		{
			struct sound_pitch_range *range = TAG_BLOCK_GET_ELEMENT(&definition->pitch_ranges, range_index, struct sound_pitch_range);

			for (index = 0; index < range->permutations.count; index++)
			{
				struct sound_permutation *permutation = TAG_BLOCK_GET_ELEMENT(&range->permutations, index, struct sound_permutation);

				if (permutation_transcoding(permutation) == _transcoding_ogg_vorbis && permutation->sample_buffer_size == 0 &&
					permutation->samples.size >= CHAIN_MEASURE_STREAM_BYTES)
				{
					uint32_t whole_bytes = measured_adpcm_bytes(permutation, definition);

					/* (none: left to be measured as it loads, and said then) */
					if (whole_bytes)
						permutation->sample_buffer_size = whole_bytes;
				}
			}
			if (range_chained(range, definition, &links_so_far))
			{
				table_count += range->permutations.count + range_links_added(range, definition);
				for (index = 0; index < range->permutations.count; index++)
				{
					struct sound_permutation const *permutation =
						TAG_BLOCK_GET_ELEMENT(&range->permutations, index, struct sound_permutation);
					uint32_t link_blocks;

					if (chain_layout(permutation_whole_bytes(permutation, definition), definition_channels(definition),
						&link_blocks) > 1)
					{
						chains++;
						if (permutation_transcoding(permutation) == _transcoding_ogg_vorbis)
							stream_chains++;
					}
				}
			}
		}
	}
	if (!table_count)
		return;

	/* the memory: the stream decoders, then the table */
	custom_edition_sounds_globals.stream_decoder_count = MIN(stream_chains, STREAM_DECODERS);
	table_bytes = (unsigned long)table_count * (sizeof(struct sound_permutation) + sizeof(uint32_t) + 2 * sizeof(short));
	bytes = (unsigned long)custom_edition_sounds_globals.stream_decoder_count * OGG_SOUND_WORKING_BYTES + table_bytes;
	custom_edition_sounds_globals.chain_memory = (byte *)halo_custom_edition_memory_alloc(bytes);
	if (!custom_edition_sounds_globals.chain_memory)
	{
		error(_error_silent, "custom edition: no room for the chains of %ld long sounds; they are cut off", chains);
		custom_edition_sounds_globals.stream_decoder_count = 0;
		return;
	}
	for (index = 0; index < custom_edition_sounds_globals.stream_decoder_count; index++)
	{
		custom_edition_sounds_globals.stream_working[index] =
			custom_edition_sounds_globals.chain_memory + (unsigned long)index * OGG_SOUND_WORKING_BYTES;
		custom_edition_sounds_globals.stream_working_used[index] = 0;
	}
	custom_edition_sounds_globals.chain_permutations = (struct sound_permutation *)(custom_edition_sounds_globals.chain_memory +
		(unsigned long)custom_edition_sounds_globals.stream_decoder_count * OGG_SOUND_WORKING_BYTES);
	custom_edition_sounds_globals.chain_first_frames = (uint32_t *)(custom_edition_sounds_globals.chain_permutations + table_count);
	custom_edition_sounds_globals.chain_link_indices = (short *)(custom_edition_sounds_globals.chain_first_frames + table_count);
	custom_edition_sounds_globals.chain_link_counts = custom_edition_sounds_globals.chain_link_indices + table_count;
	custom_edition_sounds_globals.chain_permutation_count = table_count;

	/* the chains, the same ranges as above: each range given its
	permutations and their links in the table */
	links_so_far = 0;
	used = 0;
	tag_iterator_new(&iterator, SOUND_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct sound_definition *definition = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, tag_index);
		boolean chained = FALSE;
		long range_index;

		for (range_index = 0; range_index < definition->pitch_ranges.count; range_index++)
		{
			struct sound_pitch_range *range = TAG_BLOCK_GET_ELEMENT(&definition->pitch_ranges, range_index, struct sound_pitch_range);
			long count;

			if (!range_chained(range, definition, &links_so_far) ||
				used + range->permutations.count + range_links_added(range, definition) > table_count)
			{
				continue;
			}
			count = range_chains_make(range, definition, tag_index, used);
			range->permutations.address = custom_edition_sounds_globals.chain_permutations + used;
			range->permutations.count = count;
			used += count;
			chained = TRUE;
		}
		/* the sound plays its chains (sound_definition_next_permutation);
		one that had none of its own has every other permutation end its
		chain, as any of its permutations did */
		if (chained && !TEST_FLAG(definition->flags, _sound_definition_linked_permutations_bit))
		{
			SET_FLAG(definition->flags, _sound_definition_linked_permutations_bit, TRUE);
			for (range_index = 0; range_index < definition->pitch_ranges.count; range_index++)
			{
				struct sound_pitch_range *range = TAG_BLOCK_GET_ELEMENT(&definition->pitch_ranges, range_index, struct sound_pitch_range);

				for (index = 0; index < range->permutations.count; index++)
				{
					struct sound_permutation *permutation = TAG_BLOCK_GET_ELEMENT(&range->permutations, index, struct sound_permutation);

					if (!chain_link(permutation, NULL))
						permutation->next_permutation_index = NONE;
				}
			}
		}
	}
	platform_log("custom edition: %ld long sounds made chains of permutations (%ld permutations, %lu KB, and %ld stream "
		"decoders of %lu KB)", chains, table_count, table_bytes / 1024, custom_edition_sounds_globals.stream_decoder_count,
		(unsigned long)OGG_SOUND_WORKING_BYTES / 1024);
	return;
}

boolean custom_edition_sound_load(
	struct sound_permutation const *permutation,
	void *destination,
	long destination_bytes,
	boolean *loaded)
{
	struct sound_definition const *definition = permutation_definition(permutation);
	enum transcoding transcoding = permutation_transcoding(permutation);
	struct waiting_sound *sound;
	boolean queued = FALSE;

	pthread_mutex_lock(&custom_edition_sounds_globals.lock);
	if (definition && destination_bytes > 0 && transcoding != _transcoding_none &&
		custom_edition_sounds_globals.waiting_count < MAXIMUM_WAITING_SOUNDS &&
		decoder_thread_start())
	{
		/* (16-bit PCM needs no working memory) */
		if (transcoding == _transcoding_ogg_vorbis)
			memory_block();
		sound = &custom_edition_sounds_globals.waiting[custom_edition_sounds_globals.waiting_count++];
		sound->transcoding = transcoding;
		sound->file_offset = (unsigned long)permutation->samples.file_offset;
		sound->file_bytes = (uint32_t)permutation->samples.size;
		sound->destination = destination;
		sound->destination_bytes = (uint32_t)destination_bytes;
		sound->loaded = loaded;
		sound->channels = definition_channels(definition);
		sound->rate = definition_rate(definition);
		sound->permutation = permutation;
		sound->first_frame = 0;
		sound->link = chain_link(permutation, &sound->first_frame);
		sound->link_index = sound->link ? custom_edition_sounds_globals.chain_link_indices[chain_index(permutation)] : 0;
		sound->link_count = sound->link ? custom_edition_sounds_globals.chain_link_counts[chain_index(permutation)] : 0;
		sound->stream_link = sound->link && transcoding == _transcoding_ogg_vorbis;
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
	/* (the tags that named the chains go with the map) */
	if (custom_edition_sounds_globals.chain_memory)
	{
		long index;

		halo_custom_edition_memory_free(custom_edition_sounds_globals.chain_memory);
		custom_edition_sounds_globals.chain_memory = NULL;
		custom_edition_sounds_globals.chain_permutations = NULL;
		custom_edition_sounds_globals.chain_first_frames = NULL;
		custom_edition_sounds_globals.chain_link_indices = NULL;
		custom_edition_sounds_globals.chain_link_counts = NULL;
		custom_edition_sounds_globals.chain_permutation_count = 0;
		for (index = 0; index < STREAM_DECODERS; index++)
			custom_edition_sounds_globals.stream_working[index] = NULL;
		custom_edition_sounds_globals.stream_decoder_count = 0;
	}
	if (custom_edition_sounds_globals.decoded_count || custom_edition_sounds_globals.encoded_count)
	{
		platform_log("ogg sound: %lu Ogg Vorbis permutations decoded with the map, %.1f ms in all; "
			"%lu 16-bit PCM ones encoded, %.1f ms; %lu of them links of long sounds' chains (%lu taken up from the link before)",
			custom_edition_sounds_globals.decoded_count,
			(double)custom_edition_sounds_globals.decoded_microseconds / 1000.0,
			custom_edition_sounds_globals.encoded_count,
			(double)custom_edition_sounds_globals.encoded_microseconds / 1000.0,
			custom_edition_sounds_globals.link_decoded_count, custom_edition_sounds_globals.link_resumed_count);
	}
	custom_edition_sounds_globals.decoded_count = 0;
	custom_edition_sounds_globals.decoded_microseconds = 0;
	custom_edition_sounds_globals.encoded_count = 0;
	custom_edition_sounds_globals.encoded_microseconds = 0;
	custom_edition_sounds_globals.link_decoded_count = 0;
	custom_edition_sounds_globals.link_resumed_count = 0;
	memset(failure_reported, 0, sizeof(failure_reported));
	pthread_mutex_unlock(&custom_edition_sounds_globals.lock);
	return;
}
