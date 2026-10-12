/*
OGG_SOUND.C

Halo Custom Edition's Ogg Vorbis sound permutations (compression 3) made
Xbox ADPCM, which is what the game's sound cache holds, its channels are
made for (sound_dsound_xbox.c: compressed channels are Xbox ADPCM, mono
22 kHz or stereo) and the mixer decodes (dsound_sdl.c). Transcoding once,
as the sound cache loads a permutation, leaves the rest of the sound system
as it is: a permutation takes the cache memory an Xbox sound of its length
would (36 bytes for 64 samples a channel; the Ogg Vorbis stream is ~3-4
times smaller, 16-bit PCM ~3.6 times bigger).

The decoder is Tremor, Xiph.Org's integer Vorbis decoder, with libogg for
the pages (port/third_party/tremor and libogg; BSD licence). Halo PC's maps
come from strangers, so the decoder runs boxed in:
	- it allocates only from the working memory the caller gives it
	  (OGG_SOUND_WORKING_BYTES), through a TLSF heap made there for each
	  stream (port/third_party/tlsf). An allocation that does not fit ends
	  the decoding at once (longjmp back to ogg_sound_transcode): Tremor
	  does not check its allocations, and the heap is thrown away whole, so
	  nothing is freed or used after that. libogg/include/ogg/os_types.h
	  sends Tremor's and libogg's allocations here.
	- the stream is read in pieces (OGG_SOUND_READ_BYTES) at offsets
	  within its size, so a stream of any size needs no buffer of its size;
	- only the first logical stream is decoded, of one or two channels
	  (Tremor's other alloca are sized by the channels) at the rate the
	  sound says;
	- the output is whole ADPCM blocks within the given size: a stream
	  longer than the sound says is cut off where the output ends (and not
	  decoded further), a shorter one is followed by silence.
Tremor's remaining stack use is bounded by Vorbis's largest block (8192
samples: two arrays of 4096 entries in vorbis_book_decodevs_add, 32 KB on
a 32-bit machine).

The ADPCM is written by xbox_adpcm_encoder.c, which also writes Custom
Edition's 16-bit PCM permutations (custom_edition_sounds.c).
*/

#include "ogg_sound.h"
#include "xbox_adpcm_encoder.h"

#include <string.h>

#ifndef HALO_OGG_SOUND_NO_DECODER
#include <ogg/ogg.h>
#include "../../third_party/tremor/ivorbiscodec.h"
#include "../../third_party/tlsf/tlsf.h"

#include <setjmp.h>
#endif

/* ---------- constants */

enum
{
	MAXIMUM_CHANNELS = 2,
	VORBIS_HEADER_COUNT = 3,
	/* Tremor's samples have 9 fractional bits more than 16-bit PCM
	(vorbisfile.c, ov_read) */
	TREMOR_SAMPLE_SHIFT = 9,
	/* the working memory's own alignment */
	WORKING_ALIGNMENT = 8,
};

uint32_t ogg_sound_adpcm_bytes(uint32_t frames, int channels)
{
	return xbox_adpcm_bytes(frames, channels);
}

uint32_t ogg_sound_output_frames(uint64_t frames, long stream_rate, long rate)
{
	if (rate <= 0 || rate > OGG_SOUND_MAXIMUM_RATE || frames > UINT32_MAX)
		return 0;
	return xbox_adpcm_rate_frames(frames, stream_rate, rate);
}

/* ---------- a stream's first and last pages */

enum
{
	/* an Ogg page header: "OggS", version, type, granule position (8),
	serial number (4), sequence (4), checksum (4), segment count, then the
	segments' lacing values */
	PAGE_HEADER_BYTES = 27,
	PAGE_TYPE_OFFSET = 5,
	PAGE_GRANULE_OFFSET = 6,
	PAGE_SERIAL_OFFSET = 14,
	PAGE_SEGMENT_COUNT_OFFSET = 26,
	PAGE_TYPE_FIRST_BIT = 0x02,
	/* Vorbis's identification header: type 1, "vorbis", version (4),
	channels (1), rate (4) */
	IDENTIFICATION_BYTES = 30,
	IDENTIFICATION_CHANNELS_OFFSET = 11,
	IDENTIFICATION_RATE_OFFSET = 12,
};

static uint32_t read_u32_le(uint8_t const *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

/* the page at `page` (within `bytes` bytes): its header and body bytes,
or 0 when it is not a whole page there */
static uint32_t page_extent(uint8_t const *page, uint32_t bytes, uint32_t *header_bytes)
{
	uint32_t segments;
	uint32_t body = 0;
	uint32_t index;

	if (bytes < PAGE_HEADER_BYTES || memcmp(page, "OggS", 4) || page[4] != 0)
		return 0;
	segments = page[PAGE_SEGMENT_COUNT_OFFSET];
	if (bytes < PAGE_HEADER_BYTES + segments)
		return 0;
	for (index = 0; index < segments; index++)
		body += page[PAGE_HEADER_BYTES + index];
	*header_bytes = PAGE_HEADER_BYTES + segments;
	if (bytes - *header_bytes < body)
		return 0;
	return *header_bytes + body;
}

int ogg_sound_measure(
	uint8_t const *head,
	uint32_t head_bytes,
	uint8_t const *tail,
	uint32_t tail_bytes,
	struct ogg_sound_stream_info *info)
{
	uint32_t header_bytes;
	uint32_t page_bytes;
	uint32_t serial;
	uint8_t const *packet;
	uint32_t position;

	memset(info, 0, sizeof(*info));
	/* the first page: a stream's first, starting with the identification
	header */
	if (!head || !tail)
		return 0;
	page_bytes = page_extent(head, head_bytes, &header_bytes);
	if (!page_bytes || !(head[PAGE_TYPE_OFFSET] & PAGE_TYPE_FIRST_BIT) ||
		page_bytes - header_bytes < IDENTIFICATION_BYTES)
	{
		return 0;
	}
	packet = head + header_bytes;
	if (packet[0] != 1 || memcmp(packet + 1, "vorbis", 6) || read_u32_le(packet + 7) != 0)
		return 0;
	serial = read_u32_le(head + PAGE_SERIAL_OFFSET);
	info->channels = packet[IDENTIFICATION_CHANNELS_OFFSET];
	info->rate = (long)(read_u32_le(packet + IDENTIFICATION_RATE_OFFSET) & 0x7fffffff);

	/* the last whole page of the stream with a granule position */
	if (tail_bytes < PAGE_HEADER_BYTES)
		return 0;
	for (position = tail_bytes - PAGE_HEADER_BYTES + 1; position-- > 0;)
	{
		uint8_t const *page = tail + position;
		uint32_t low, high;

		if (page[0] != 'O' || !page_extent(page, tail_bytes - position, &header_bytes) ||
			read_u32_le(page + PAGE_SERIAL_OFFSET) != serial)
		{
			continue;
		}
		low = read_u32_le(page + PAGE_GRANULE_OFFSET);
		high = read_u32_le(page + PAGE_GRANULE_OFFSET + 4);
		/* (-1: no packet ends on the page; negative is not a length) */
		if (high & 0x80000000)
			continue;
		info->frames = (uint64_t)high << 32 | low;
		return 1;
	}
	return 0;
}

#ifndef HALO_OGG_SOUND_NO_DECODER
/* ---------- the decoder's state */

enum
{
	/* the state's mark while a decoding is kept in its working memory */
	KEPT_MARK = 0x4B455054,
};

/* The decoder's state, at the start of its working memory (the heap
follows it), so that a decoding can be kept there between two pieces of a
stream's output (ogg_sound_request.keep), and so that what the decoding
changes is what is in memory when an allocation that does not fit
longjmps back. */
struct decoder_state
{
	/* KEPT_MARK while a decoding is kept: of which stream, channels and
	rate, and the output frame it goes on with */
	uint32_t kept;
	uint64_t stream_key;
	int channels;
	long output_rate;
	uint32_t next_frame;

	struct ogg_sound_request const *request;
	struct ogg_sound_result *result;

	/* the working memory's heap, and the bytes in use in it */
	tlsf_t heap;
	size_t heap_in_use;
	jmp_buf out_of_memory;

	/* the output, and the stream's frames taken at its rate (halved, or
	interpolated from another rate: xbox_adpcm_encoder.c) */
	struct xbox_adpcm_encoder encoder;
	struct xbox_adpcm_rate rate;

	/* the stream, the bytes of it read, whether its first logical stream
	has begun and its last page come, and whether the decoding is over
	(the stream ended, or its input) */
	ogg_sync_state sync;
	ogg_stream_state stream;
	vorbis_info info;
	vorbis_comment comment;
	vorbis_dsp_state dsp;
	vorbis_block block_state;
	uint32_t input_offset;
	int begun;
	int stream_started;
	int stream_ended;
	int headers;
	int finished;
};

/* (the state's bytes, the heap aligned after them) */
#define DECODER_STATE_BYTES ((sizeof(struct decoder_state) + 15) & ~(size_t)15)

/* the state of the decoding being done (ogg_sound_transcode) */
static struct decoder_state *decoder;

/* ---------- the working memory (ogg/os_types.h) */

static void out_of_memory(void)
{
	longjmp(decoder->out_of_memory, 1);
}

static void heap_note(void *pointer)
{
	decoder->heap_in_use += tlsf_block_size(pointer);
	if (decoder->heap_in_use > decoder->result->working_peak_bytes)
		decoder->result->working_peak_bytes = decoder->heap_in_use;
}

void *halo_ogg_malloc(size_t bytes)
{
	void *pointer;

	if (!decoder || !decoder->heap)
		out_of_memory();
	pointer = tlsf_malloc(decoder->heap, bytes ? bytes : 1);
	if (!pointer)
		out_of_memory();
	heap_note(pointer);
	return pointer;
}

void *halo_ogg_calloc(size_t count, size_t size)
{
	void *pointer;

	if (size && count > (size_t)-1 / size)
		out_of_memory();
	pointer = halo_ogg_malloc(count * size);
	memset(pointer, 0, count * size ? count * size : 1);
	return pointer;
}

void halo_ogg_free(void *pointer)
{
	if (!pointer || !decoder || !decoder->heap)
		return;
	decoder->heap_in_use -= tlsf_block_size(pointer);
	tlsf_free(decoder->heap, pointer);
}

void *halo_ogg_realloc(void *pointer, size_t bytes)
{
	size_t old_size;
	void *moved;

	if (!pointer)
		return halo_ogg_malloc(bytes);
	if (!decoder || !decoder->heap)
		out_of_memory();
	old_size = tlsf_block_size(pointer);
	moved = tlsf_realloc(decoder->heap, pointer, bytes ? bytes : 1);
	if (!moved)
		out_of_memory();
	decoder->heap_in_use -= old_size;
	heap_note(moved);
	return moved;
}

/* ---------- the output */

/* one frame of the stream, in 16-bit PCM by stream channel */
static void stream_frame(int const *samples, int stream_channels)
{
	int converted[MAXIMUM_CHANNELS];
	int channel;

	/* the output's channels: a stereo stream of a mono sound mixed, a
	mono stream of a stereo sound on both sides */
	if (decoder->request->channels == stream_channels)
	{
		for (channel = 0; channel < stream_channels; channel++)
			converted[channel] = samples[channel];
	}
	else if (decoder->request->channels == 1)
	{
		converted[0] = (samples[0] + samples[1]) / 2;
	}
	else
	{
		converted[0] = converted[1] = samples[0];
	}

	xbox_adpcm_rate_frame(&decoder->rate, converted);
}

/* the frames the decoder has ready, until the output is full: those it
did not take stay the decoder's (a kept decoding's next piece starts with
them) */
static void pcm_drain(void)
{
	ogg_int32_t **pcm;
	int frames;

	while (!decoder->encoder.full && (frames = vorbis_synthesis_pcmout(&decoder->dsp, &pcm)) > 0)
	{
		int stream_channels = decoder->info.channels;
		int frame;

		for (frame = 0; frame < frames && !decoder->encoder.full; frame++)
		{
			int samples[MAXIMUM_CHANNELS];
			int channel;

			for (channel = 0; channel < stream_channels; channel++)
				samples[channel] = (int)(pcm[channel][frame] >> TREMOR_SAMPLE_SHIFT);
			stream_frame(samples, stream_channels);
		}
		if (frame < frames)
			decoder->result->truncated = 1;
		vorbis_synthesis_read(&decoder->dsp, frame);
	}
	return;
}

/* ---------- the stream */

/* a packet of the stream: its three headers, then audio. Returns the
status that ends the decoding, else _ogg_sound_ok */
static enum ogg_sound_status packet_take(ogg_packet *packet)
{
	if (decoder->headers < VORBIS_HEADER_COUNT)
	{
		if (vorbis_synthesis_headerin(&decoder->info, &decoder->comment, packet) < 0)
			return _ogg_sound_not_vorbis;
		decoder->headers++;
		if (decoder->headers == 1)
		{
			decoder->result->stream_channels = decoder->info.channels;
			decoder->result->stream_rate = decoder->info.rate;
			decoder->result->halved = decoder->info.rate == 2 * decoder->request->rate;
			if (decoder->info.channels < 1 || decoder->info.channels > MAXIMUM_CHANNELS ||
				!ogg_sound_output_frames(1, decoder->info.rate, decoder->request->rate) ||
				!xbox_adpcm_rate_begin(&decoder->rate, &decoder->encoder, decoder->info.rate, decoder->request->rate))
			{
				return _ogg_sound_unsupported_format;
			}
		}
		else if (decoder->headers == VORBIS_HEADER_COUNT)
		{
			if (vorbis_synthesis_init(&decoder->dsp, &decoder->info) != 0 ||
				vorbis_block_init(&decoder->dsp, &decoder->block_state) != 0)
			{
				return _ogg_sound_not_vorbis;
			}
		}
		return _ogg_sound_ok;
	}
	/* (a damaged audio packet is skipped, as players do) */
	if (vorbis_synthesis(&decoder->block_state, packet) == 0)
		vorbis_synthesis_blockin(&decoder->dsp, &decoder->block_state);
	return _ogg_sound_ok;
}

/* a page: the first logical stream's (its packets are taken out one by
one: stream_run). Returns the status that ends the decoding, else
_ogg_sound_ok */
static enum ogg_sound_status page_take(ogg_page *page)
{
	if (!decoder->stream_started)
	{
		/* (whatever comes before a stream's first page is not ours) */
		if (!ogg_page_bos(page))
			return _ogg_sound_ok;
		if (ogg_stream_init(&decoder->stream, ogg_page_serialno(page)) != 0)
			return _ogg_sound_out_of_memory;
		decoder->stream_started = 1;
	}
	/* (another logical stream's page, multiplexed or chained, is passed
	over; so is one after the stream's last) */
	if (decoder->stream_ended || ogg_page_serialno(page) != decoder->stream.serialno)
		return _ogg_sound_ok;
	if (ogg_stream_pagein(&decoder->stream, page) != 0)
		return _ogg_sound_ok;
	if (ogg_page_eos(page))
		decoder->stream_ended = 1;
	return _ogg_sound_ok;
}

/* Decodes until the output is full (the rest left for a kept decoding's
next piece) or the decoding is over (finished: the stream's last page
taken, or the input read). Returns the status that ends the decoding, else
_ogg_sound_ok. */
static enum ogg_sound_status stream_run(void)
{
	struct ogg_sound_request const *request = decoder->request;
	enum ogg_sound_status status;

	if (!decoder->begun)
	{
		decoder->begun = 1;
		ogg_sync_init(&decoder->sync);
		vorbis_info_init(&decoder->info);
		vorbis_comment_init(&decoder->comment);
	}
	for (;;)
	{
		ogg_packet packet;
		ogg_page page;
		uint32_t size;
		char *buffer;
		int result;

		/* the frames the last packet made */
		if (decoder->headers >= VORBIS_HEADER_COUNT)
		{
			pcm_drain();
			if (decoder->encoder.full)
				return _ogg_sound_ok;
		}
		/* the next packet of the pages taken */
		if (decoder->stream_started)
		{
			result = ogg_stream_packetout(&decoder->stream, &packet);
			/* (a gap in the stream: the next packet is taken) */
			if (result < 0)
				continue;
			if (result > 0)
			{
				status = packet_take(&packet);
				/* (an output full from the start ends the decoding at its
				first packet) */
				if (status != _ogg_sound_ok || decoder->encoder.full)
					return status;
				continue;
			}
			if (decoder->stream_ended)
			{
				decoder->finished = 1;
				return _ogg_sound_ok;
			}
		}
		/* the next page of the bytes read */
		result = ogg_sync_pageout(&decoder->sync, &page);
		/* (bytes that are not a page are skipped) */
		if (result < 0)
			continue;
		if (result > 0)
		{
			status = page_take(&page);
			if (status != _ogg_sound_ok)
				return status;
			continue;
		}
		/* more of the stream */
		if (decoder->input_offset >= request->input_bytes)
		{
			decoder->finished = 1;
			return decoder->headers < VORBIS_HEADER_COUNT ? _ogg_sound_not_vorbis : _ogg_sound_ok;
		}
		size = request->input_bytes - decoder->input_offset;
		if (size > OGG_SOUND_READ_BYTES)
			size = OGG_SOUND_READ_BYTES;
		buffer = ogg_sync_buffer(&decoder->sync, (long)size);
		if (!buffer)
			return _ogg_sound_out_of_memory;
		if (!request->read(request->read_context, decoder->input_offset, size, buffer))
			return _ogg_sound_read_failed;
		ogg_sync_wrote(&decoder->sync, (long)size);
		decoder->input_offset += size;
	}
}

/* whether the state holds a kept decoding that `request` goes on with */
static int decoding_resumable(struct decoder_state const *state, struct ogg_sound_request const *request)
{
	return request->keep && state->kept == KEPT_MARK && state->stream_key == request->stream_key &&
		state->channels == request->channels && state->output_rate == request->rate &&
		request->first_frame >= state->next_frame;
}

enum ogg_sound_status ogg_sound_transcode(
	struct ogg_sound_request const *request,
	struct ogg_sound_result *result)
{
	/* (volatile: set on both sides of the setjmp) */
	volatile enum ogg_sound_status status;
	struct decoder_state *state;

	memset(result, 0, sizeof(*result));
	if (!request || !request->read || !request->output ||
		request->channels < 1 || request->channels > MAXIMUM_CHANNELS ||
		request->rate <= 0 || request->rate > OGG_SOUND_MAXIMUM_RATE ||
		!request->working || ((uintptr_t)request->working & (WORKING_ALIGNMENT - 1)) ||
		request->working_bytes < OGG_SOUND_MINIMUM_WORKING_BYTES)
	{
		result->status = _ogg_sound_bad_arguments;
		return result->status;
	}
	state = (struct decoder_state *)request->working;
	if (decoding_resumable(state, request) && state->finished)
	{
		struct xbox_adpcm_encoder silence;

		/* the stream ended before this piece: silence, and still kept as
		ended (no piece after it decodes the stream again to find that) */
		xbox_adpcm_encoder_begin(&silence, request->output, request->output_bytes, request->channels);
		result->frames_padded = xbox_adpcm_encoder_finish(&silence);
		result->resumed = 1;
		result->kept = 1;
		result->frames_skipped = request->first_frame - state->next_frame;
		result->stream_channels = state->info.channels;
		result->stream_rate = state->info.rate;
		result->halved = state->info.rate == 2 * request->rate;
		result->status = _ogg_sound_ok;
		return result->status;
	}
	if (decoding_resumable(state, request))
	{
		uint32_t skip = request->first_frame - state->next_frame;

		/* the kept decoding: its frames held past the last output are this
		one's first; those before this one's first (a piece skipped) only
		warm the encoder, as they would have the whole output's (or, short
		of whole blocks, are skipped) */
		if (!skip)
		{
		}
		else if (skip % XBOX_ADPCM_BLOCK_SAMPLES == 0)
		{
			xbox_adpcm_encoder_warm(&state->encoder, skip);
		}
		else if (skip >= (uint32_t)state->encoder.block_frames)
		{
			state->encoder.skip_frames = skip - (uint32_t)state->encoder.block_frames;
			state->encoder.block_frames = 0;
		}
		else
		{
			memmove(state->encoder.block[0], state->encoder.block[skip],
				(size_t)(state->encoder.block_frames - (int)skip) * sizeof(state->encoder.block[0]));
			state->encoder.block_frames -= (int)skip;
		}
		xbox_adpcm_encoder_continue(&state->encoder, request->output, request->output_bytes);
		result->resumed = 1;
		result->frames_skipped = skip;
		result->stream_channels = state->info.channels;
		result->stream_rate = state->info.rate;
		result->halved = state->info.rate == 2 * request->rate;
		result->working_peak_bytes = state->heap_in_use;
	}
	else
	{
		memset(state, 0, sizeof(*state));
		state->channels = request->channels;
		state->output_rate = request->rate;
		xbox_adpcm_encoder_begin(&state->encoder, request->output, request->output_bytes, request->channels);
		/* (the frames before the first decoded and left out, warming the
		encoder as they would have the whole output's) */
		state->encoder.skip_frames = request->first_frame % XBOX_ADPCM_BLOCK_SAMPLES;
		state->encoder.warm_frames = request->first_frame - state->encoder.skip_frames;
		state->encoder.hold = request->keep;
		result->frames_skipped = request->first_frame;
		state->heap = tlsf_create_with_pool((uint8_t *)request->working + DECODER_STATE_BYTES,
			request->working_bytes - DECODER_STATE_BYTES);
	}
	/* (kept again only once this piece is done) */
	state->kept = 0;
	state->request = request;
	state->result = result;
	decoder = state;

	if (!state->heap)
	{
		status = _ogg_sound_bad_arguments;
	}
	else if (setjmp(state->out_of_memory) == 0)
	{
		status = stream_run();
		/* (the last frames of a stream of another rate) */
		if (state->finished)
			xbox_adpcm_rate_finish(&state->rate);
	}
	else
	{
		status = _ogg_sound_out_of_memory;
	}

	/* (the last part block, and silence after it) */
	result->frames_padded = xbox_adpcm_encoder_finish(&state->encoder);
	result->frames_decoded = state->encoder.frames;
	/* (frames skipped short of the output: none of it was reached) */
	result->frames_skipped -= state->encoder.skip_frames + state->encoder.warm_frames;
	result->truncated |= state->encoder.truncated;
	result->sum_of_squares = state->encoder.sum_of_squares;
	result->peak = state->encoder.peak;
	result->status = status;
	if (status == _ogg_sound_ok && request->keep && (state->finished || state->encoder.full) &&
		request->first_frame <= UINT32_MAX - state->encoder.frames)
	{
		/* kept for the next piece (one the output filled before the
		stream's end, or one the stream ended in: what follows is silence) */
		state->kept = KEPT_MARK;
		state->stream_key = request->stream_key;
		state->next_frame = request->first_frame + state->encoder.frames;
		result->kept = 1;
		result->truncated |= !state->finished;
	}
	/* else, and once the stream has ended, the heap goes whole: nothing in
	it is used again */
	if (!result->kept || state->finished)
		state->heap = NULL;
	state->request = NULL;
	state->result = NULL;
	decoder = NULL;
	return status;
}

int ogg_sound_kept(
	void const *working,
	size_t working_bytes,
	uint64_t stream_key,
	uint32_t *next_frame)
{
	struct decoder_state const *state = (struct decoder_state const *)working;

	if (!working || ((uintptr_t)working & (WORKING_ALIGNMENT - 1)) || working_bytes < OGG_SOUND_MINIMUM_WORKING_BYTES ||
		state->kept != KEPT_MARK || state->stream_key != stream_key)
	{
		return 0;
	}
	if (next_frame)
		*next_frame = state->next_frame;
	return 1;
}

#else
/* (a build without setjmp, the Android guest's: the output is silence) */
enum ogg_sound_status ogg_sound_transcode(
	struct ogg_sound_request const *request,
	struct ogg_sound_result *result)
{
	memset(result, 0, sizeof(*result));
	if (request && request->output)
		memset(request->output, 0, request->output_bytes);
	result->status = _ogg_sound_unsupported_format;
	return result->status;
}

int ogg_sound_kept(
	void const *working,
	size_t working_bytes,
	uint64_t stream_key,
	uint32_t *next_frame)
{
	(void)working;
	(void)working_bytes;
	(void)stream_key;
	(void)next_frame;
	return 0;
}
#endif

char const *ogg_sound_status_describe(enum ogg_sound_status status)
{
	static char const *const descriptions[NUMBER_OF_OGG_SOUND_STATUSES] =
	{
		"decoded",
		"the stream could not be read",
		"not an Ogg Vorbis stream, or a damaged one",
		"a format the sound cannot play (more than two channels, or a rate out of bounds)",
		"the stream needs more working memory than the decoder has",
		"bad arguments",
	};

	return (unsigned)status < NUMBER_OF_OGG_SOUND_STATUSES ? descriptions[status] : "?";
}
