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

The ADPCM is the format the mixer decodes (dsound_sdl.c,
decode_adpcm_blocks): per block and channel, the first sample and the step
index in a 4-byte header, then 63 nibbles in 4-byte groups alternating
between channels, low nibble first, and a padding nibble of 0. The encoder
predicts with the decoder's own arithmetic, so what is played is what the
encoder chose, and carries its step index from block to block.
*/

#include "ogg_sound.h"

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

/* ---------- IMA ADPCM */

static const short ima_step_table[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767,
};

static const signed char ima_index_table[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
};

/* the nibble that brings the decoder nearest `target` from `*predictor` at
step index `*index`, and the decoder's state after it (dsound_sdl.c,
ima_expand_fast: the same arithmetic) */
static int ima_encode(int target, int *predictor, int *index)
{
	int step = ima_step_table[*index];
	int difference = target - *predictor;
	int nibble = 0;
	int decoded;
	int next;

	if (difference < 0)
	{
		nibble = 8;
		difference = -difference;
	}
	if (difference >= step)
	{
		nibble |= 4;
		difference -= step;
	}
	if (difference >= step >> 1)
	{
		nibble |= 2;
		difference -= step >> 1;
	}
	if (difference >= step >> 2)
	{
		nibble |= 1;
	}

	decoded = step >> 3;
	if (nibble & 1)
		decoded += step >> 2;
	if (nibble & 2)
		decoded += step >> 1;
	if (nibble & 4)
		decoded += step;
	if (nibble & 8)
		decoded = -decoded;
	decoded += *predictor;
	if (decoded > 32767)
		decoded = 32767;
	if (decoded < -32768)
		decoded = -32768;
	next = *index + ima_index_table[nibble];
	if (next < 0)
		next = 0;
	if (next > 88)
		next = 88;
	*predictor = decoded;
	*index = next;
	return nibble;
}

uint32_t ogg_sound_adpcm_bytes(uint32_t frames, int channels)
{
	uint32_t blocks = frames / OGG_SOUND_ADPCM_BLOCK_SAMPLES +
		(frames % OGG_SOUND_ADPCM_BLOCK_SAMPLES ? 1 : 0);

	if (channels < 1 || channels > MAXIMUM_CHANNELS)
		return 0;
	return blocks * OGG_SOUND_ADPCM_BLOCK_BYTES * (uint32_t)channels;
}

uint32_t ogg_sound_output_frames(uint64_t frames, long stream_rate, long rate)
{
	if (rate <= 0 || rate > OGG_SOUND_MAXIMUM_RATE || frames > UINT32_MAX)
		return 0;
	if (stream_rate == rate)
		return (uint32_t)frames;
	if (stream_rate == 2 * rate)
		return (uint32_t)((frames + 1) / 2);
	return 0;
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

/* (static, not on the stack: what the decoding changes must be what is in
memory when an allocation that does not fit longjmps back) */
static struct
{
	struct ogg_sound_request const *request;
	struct ogg_sound_result *result;

	/* the working memory's heap, and the bytes in use in it */
	tlsf_t heap;
	size_t heap_in_use;
	jmp_buf out_of_memory;

	/* the output */
	uint8_t *output;
	uint32_t output_blocks;
	uint32_t block_bytes;
	uint32_t blocks_written;
	/* the block being filled: frames of the output's channels */
	short block[OGG_SOUND_ADPCM_BLOCK_SAMPLES][MAXIMUM_CHANNELS];
	int block_frames;
	/* the encoder's step index, by channel */
	int step_index[MAXIMUM_CHANNELS];
	/* the stream's rate is halved: the stream's frame before the pair being
	filtered, the pair's first frame, and whether there is one */
	int previous[MAXIMUM_CHANNELS];
	int pending[MAXIMUM_CHANNELS];
	int have_pending;
	int halved;
	int full;

	/* the stream */
	ogg_sync_state sync;
	ogg_stream_state stream;
	vorbis_info info;
	vorbis_comment comment;
	vorbis_dsp_state dsp;
	vorbis_block block_state;
	int stream_started;
	int headers;
} decoder;

/* ---------- the working memory (ogg/os_types.h) */

static void out_of_memory(void)
{
	longjmp(decoder.out_of_memory, 1);
}

static void heap_note(void *pointer)
{
	decoder.heap_in_use += tlsf_block_size(pointer);
	if (decoder.heap_in_use > decoder.result->working_peak_bytes)
		decoder.result->working_peak_bytes = decoder.heap_in_use;
}

void *halo_ogg_malloc(size_t bytes)
{
	void *pointer;

	if (!decoder.heap)
		out_of_memory();
	pointer = tlsf_malloc(decoder.heap, bytes ? bytes : 1);
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
	if (!pointer || !decoder.heap)
		return;
	decoder.heap_in_use -= tlsf_block_size(pointer);
	tlsf_free(decoder.heap, pointer);
}

void *halo_ogg_realloc(void *pointer, size_t bytes)
{
	size_t old_size;
	void *moved;

	if (!pointer)
		return halo_ogg_malloc(bytes);
	if (!decoder.heap)
		out_of_memory();
	old_size = tlsf_block_size(pointer);
	moved = tlsf_realloc(decoder.heap, pointer, bytes ? bytes : 1);
	if (!moved)
		out_of_memory();
	decoder.heap_in_use -= old_size;
	heap_note(moved);
	return moved;
}

/* ---------- the output */

static void block_encode(void)
{
	int channels = decoder.request->channels;
	uint8_t *data;
	int channel;
	int frame;

	/* (a part block is held: the last sample repeats, which the ear does
	not hear where silence would click) */
	for (frame = decoder.block_frames; frame < OGG_SOUND_ADPCM_BLOCK_SAMPLES; frame++)
		for (channel = 0; channel < channels; channel++)
			decoder.block[frame][channel] = frame ? decoder.block[frame - 1][channel] : 0;

	data = decoder.output + decoder.blocks_written * decoder.block_bytes;
	memset(data, 0, decoder.block_bytes);
	for (channel = 0; channel < channels; channel++)
	{
		int predictor = decoder.block[0][channel];
		int index = decoder.step_index[channel];
		int sample;

		data[channel * 4 + 0] = (uint8_t)(predictor & 0xff);
		data[channel * 4 + 1] = (uint8_t)((predictor >> 8) & 0xff);
		data[channel * 4 + 2] = (uint8_t)index;
		data[channel * 4 + 3] = 0;
		for (sample = 1; sample < OGG_SOUND_ADPCM_BLOCK_SAMPLES; sample++)
		{
			int nibble = ima_encode(decoder.block[sample][channel], &predictor, &index);
			int group = (sample - 1) / 8;
			int position = (sample - 1) % 8;
			uint8_t *byte = data + 4 * channels + (group * channels + channel) * 4 + position / 2;

			*byte |= (uint8_t)(position & 1 ? nibble << 4 : nibble);
		}
		decoder.step_index[channel] = index;
	}
	decoder.blocks_written++;
	decoder.block_frames = 0;
	if (decoder.blocks_written >= decoder.output_blocks)
		decoder.full = 1;
}

/* one frame of the output's channels */
static void output_frame(int const *frame)
{
	int channel;

	if (decoder.full)
	{
		decoder.result->truncated = 1;
		return;
	}
	for (channel = 0; channel < decoder.request->channels; channel++)
	{
		int value = frame[channel];

		if (value > 32767)
			value = 32767;
		if (value < -32768)
			value = -32768;
		decoder.block[decoder.block_frames][channel] = (short)value;
		decoder.result->sum_of_squares += (double)value * value;
		if (value < 0)
			value = -value;
		if (value > decoder.result->peak)
			decoder.result->peak = value;
	}
	decoder.result->frames_decoded++;
	if (++decoder.block_frames == OGG_SOUND_ADPCM_BLOCK_SAMPLES)
		block_encode();
}

/* one frame of the stream, in 16-bit PCM by stream channel */
static void stream_frame(int const *samples, int stream_channels)
{
	int converted[MAXIMUM_CHANNELS];
	int channel;

	/* the output's channels: a stereo stream of a mono sound mixed, a
	mono stream of a stereo sound on both sides */
	if (decoder.request->channels == stream_channels)
	{
		for (channel = 0; channel < stream_channels; channel++)
			converted[channel] = samples[channel];
	}
	else if (decoder.request->channels == 1)
	{
		converted[0] = (samples[0] + samples[1]) / 2;
	}
	else
	{
		converted[0] = converted[1] = samples[0];
	}

	if (decoder.halved)
	{
		/* each pair of frames one frame, low-passed (1 2 1)/4 around the
		pair's first */
		if (!decoder.have_pending)
		{
			for (channel = 0; channel < decoder.request->channels; channel++)
				decoder.pending[channel] = converted[channel];
			decoder.have_pending = 1;
			return;
		}
		for (channel = 0; channel < decoder.request->channels; channel++)
		{
			int middle = decoder.pending[channel];

			decoder.pending[channel] = (decoder.previous[channel] + 2 * middle + converted[channel]) / 4;
			decoder.previous[channel] = converted[channel];
		}
		decoder.have_pending = 0;
		output_frame(decoder.pending);
		return;
	}
	output_frame(converted);
}

/* the frames the decoder has ready */
static void pcm_drain(void)
{
	ogg_int32_t **pcm;
	int frames;

	while (!decoder.full && (frames = vorbis_synthesis_pcmout(&decoder.dsp, &pcm)) > 0)
	{
		int stream_channels = decoder.info.channels;
		int frame;

		for (frame = 0; frame < frames && !decoder.full; frame++)
		{
			int samples[MAXIMUM_CHANNELS];
			int channel;

			for (channel = 0; channel < stream_channels; channel++)
				samples[channel] = (int)(pcm[channel][frame] >> TREMOR_SAMPLE_SHIFT);
			stream_frame(samples, stream_channels);
		}
		if (frame < frames)
			decoder.result->truncated = 1;
		vorbis_synthesis_read(&decoder.dsp, frames);
	}
	return;
}

/* ---------- the stream */

/* a packet of the stream: its three headers, then audio. Returns the
status that ends the decoding, else _ogg_sound_ok */
static enum ogg_sound_status packet_take(ogg_packet *packet)
{
	if (decoder.headers < VORBIS_HEADER_COUNT)
	{
		if (vorbis_synthesis_headerin(&decoder.info, &decoder.comment, packet) < 0)
			return _ogg_sound_not_vorbis;
		decoder.headers++;
		if (decoder.headers == 1)
		{
			decoder.result->stream_channels = decoder.info.channels;
			decoder.result->stream_rate = decoder.info.rate;
			decoder.halved = decoder.info.rate != decoder.request->rate;
			decoder.result->halved = decoder.halved;
			if (decoder.info.channels < 1 || decoder.info.channels > MAXIMUM_CHANNELS ||
				!ogg_sound_output_frames(1, decoder.info.rate, decoder.request->rate))
			{
				return _ogg_sound_unsupported_format;
			}
		}
		else if (decoder.headers == VORBIS_HEADER_COUNT)
		{
			if (vorbis_synthesis_init(&decoder.dsp, &decoder.info) != 0 ||
				vorbis_block_init(&decoder.dsp, &decoder.block_state) != 0)
			{
				return _ogg_sound_not_vorbis;
			}
		}
		return _ogg_sound_ok;
	}
	/* (a damaged audio packet is skipped, as players do) */
	if (vorbis_synthesis(&decoder.block_state, packet) == 0)
		vorbis_synthesis_blockin(&decoder.dsp, &decoder.block_state);
	pcm_drain();
	return _ogg_sound_ok;
}

/* a page: the first logical stream's packets. Returns nonzero when the
decoding is over (its last page, the output full, or a status) */
static int page_take(ogg_page *page, enum ogg_sound_status *status)
{
	ogg_packet packet;
	int result;

	if (!decoder.stream_started)
	{
		/* (whatever comes before a stream's first page is not ours) */
		if (!ogg_page_bos(page))
			return 0;
		if (ogg_stream_init(&decoder.stream, ogg_page_serialno(page)) != 0)
		{
			*status = _ogg_sound_out_of_memory;
			return 1;
		}
		decoder.stream_started = 1;
	}
	/* (another logical stream's page, multiplexed or chained, is passed
	over) */
	if (ogg_page_serialno(page) != decoder.stream.serialno)
		return 0;
	if (ogg_stream_pagein(&decoder.stream, page) != 0)
		return 0;
	while ((result = ogg_stream_packetout(&decoder.stream, &packet)) != 0)
	{
		/* (a gap in the stream: the next packet is taken) */
		if (result < 0)
			continue;
		*status = packet_take(&packet);
		if (*status != _ogg_sound_ok || decoder.full)
			return 1;
	}
	return ogg_page_eos(page);
}

static enum ogg_sound_status stream_decode(void)
{
	struct ogg_sound_request const *request = decoder.request;
	enum ogg_sound_status status = _ogg_sound_ok;
	uint32_t offset = 0;

	ogg_sync_init(&decoder.sync);
	vorbis_info_init(&decoder.info);
	vorbis_comment_init(&decoder.comment);
	while (offset < request->input_bytes)
	{
		uint32_t size = request->input_bytes - offset;
		char *buffer;
		ogg_page page;
		int result;

		if (size > OGG_SOUND_READ_BYTES)
			size = OGG_SOUND_READ_BYTES;
		buffer = ogg_sync_buffer(&decoder.sync, (long)size);
		if (!buffer)
			return _ogg_sound_out_of_memory;
		if (!request->read(request->read_context, offset, size, buffer))
			return _ogg_sound_read_failed;
		ogg_sync_wrote(&decoder.sync, (long)size);
		offset += size;
		while ((result = ogg_sync_pageout(&decoder.sync, &page)) != 0)
		{
			/* (bytes that are not a page are skipped) */
			if (result < 0)
				continue;
			if (page_take(&page, &status))
				return status;
		}
	}
	return decoder.headers < VORBIS_HEADER_COUNT ? _ogg_sound_not_vorbis : _ogg_sound_ok;
}

enum ogg_sound_status ogg_sound_transcode(
	struct ogg_sound_request const *request,
	struct ogg_sound_result *result)
{
	/* (volatile: set on both sides of the setjmp) */
	volatile enum ogg_sound_status status;

	memset(result, 0, sizeof(*result));
	memset(&decoder, 0, sizeof(decoder));
	if (!request || !request->read || !request->output ||
		request->channels < 1 || request->channels > MAXIMUM_CHANNELS ||
		request->rate <= 0 || request->rate > OGG_SOUND_MAXIMUM_RATE ||
		!request->working || ((uintptr_t)request->working & (WORKING_ALIGNMENT - 1)) ||
		request->working_bytes < OGG_SOUND_MINIMUM_WORKING_BYTES)
	{
		result->status = _ogg_sound_bad_arguments;
		return result->status;
	}
	decoder.request = request;
	decoder.result = result;
	decoder.output = (uint8_t *)request->output;
	decoder.block_bytes = OGG_SOUND_ADPCM_BLOCK_BYTES * (uint32_t)request->channels;
	decoder.output_blocks = request->output_bytes / decoder.block_bytes;
	decoder.full = decoder.output_blocks == 0;

	decoder.heap = tlsf_create_with_pool(request->working, request->working_bytes);
	if (!decoder.heap)
	{
		status = _ogg_sound_bad_arguments;
	}
	else if (setjmp(decoder.out_of_memory) == 0)
	{
		status = stream_decode();
		/* (the last frame of a halved stream, and the last part block) */
		if (decoder.have_pending && !decoder.full)
			output_frame(decoder.pending);
	}
	else
	{
		status = _ogg_sound_out_of_memory;
	}
	/* the heap goes whole: nothing in it is used again */
	decoder.heap = NULL;

	if (decoder.block_frames && !decoder.full)
		block_encode();
	/* the rest is silence: blocks of zeros (a first sample of 0 and
	nibbles of 0 are 0 throughout) */
	if (decoder.blocks_written < decoder.output_blocks)
	{
		memset(decoder.output + decoder.blocks_written * decoder.block_bytes, 0,
			(size_t)(decoder.output_blocks - decoder.blocks_written) * decoder.block_bytes);
	}
	if (result->frames_decoded < decoder.output_blocks * OGG_SOUND_ADPCM_BLOCK_SAMPLES)
		result->frames_padded = decoder.output_blocks * OGG_SOUND_ADPCM_BLOCK_SAMPLES - result->frames_decoded;
	result->status = status;
	decoder.request = NULL;
	decoder.result = NULL;
	return status;
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
	(void)ima_encode;
	result->status = _ogg_sound_unsupported_format;
	return result->status;
}
#endif

char const *ogg_sound_status_describe(enum ogg_sound_status status)
{
	static char const *const descriptions[NUMBER_OF_OGG_SOUND_STATUSES] =
	{
		"decoded",
		"the stream could not be read",
		"not an Ogg Vorbis stream, or a damaged one",
		"a format the sound cannot play (more than two channels, or another rate)",
		"the stream needs more working memory than the decoder has",
		"bad arguments",
	};

	return (unsigned)status < NUMBER_OF_OGG_SOUND_STATUSES ? descriptions[status] : "?";
}
