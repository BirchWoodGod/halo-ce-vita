/*
OGG_SOUND_FUZZ.C

A fuzz target for Halo Custom Edition's Ogg Vorbis sounds made Xbox ADPCM
(port/linux/src/ogg_sound.c, with Tremor and libogg): Custom Edition maps
come from strangers (the lobby's map download), and the sound cache decodes
what their sound tags point at. The input's first byte says how it is
played:
  bit 0     one channel or two
  bit 1     22 kHz or 44 kHz
  bit 2     the decoder's working memory: all of it, or the least it takes
            (so that running out of it is tried too)
  bit 3     the pages' checksums made good first (else only pages that
            happen to be whole get through libogg: with them made good, a
            change reaches Tremor's headers, codebooks and packets)
  bits 4-7  the output's length: none, or 1/8 to 15/8 of what the stream's
            last page says (or of 0.5 s when it says nothing)
then the stream. ogg_sound_measure reads the stream's first 512 and last
8192 bytes as the sound cache does (custom_edition_sounds.c), and
ogg_sound_transcode decodes it into an output with guards each side; every
input must only fail or succeed, writing within the output. A stream whose
identification header (its checksums made good) says another rate than the
sound's goes through the rate converter (xbox_adpcm_encoder.c). The same
bytes are then taken as Xbox ADPCM blocks of those channels, decoded
(xbox_adpcm_block_decode) and taken from a rate the bytes give (any from
1 kHz to 192 kHz, some out of those bounds) to one of the output's, as the
sound cache does a Custom Edition map's 44 kHz mono sounds, into an output
with guards (its length as above); none may write outside it.
run_ogg_sound_test.sh builds it with AddressSanitizer and UBSan, with
libFuzzer (OGG_FUZZ_SECONDS), and without it, when main below runs
OGG_FUZZ_ITERATIONS changes (bytes flipped, set to edge values, cut,
repeated, spliced from another) of the streams in ogg_sound_cases, from a
fixed seed.
*/

#include "ogg_sound.h"
#include "xbox_adpcm_encoder.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	GUARD_BYTES = 64,
	GUARD_VALUE = 0x5A,
	MAXIMUM_INPUT_BYTES = 1024 * 1024,
	HEAD_BYTES = 512,
	TAIL_BYTES = 8192,
};

static double working_memory[OGG_SOUND_WORKING_BYTES / sizeof(double)];
static unsigned char stream_copy[MAXIMUM_INPUT_BYTES];
static uint32_t stream_size;
static unsigned long statuses[NUMBER_OF_OGG_SOUND_STATUSES];
static unsigned long adpcm_passes;

static uint32_t crc_table[256];

static void crc_table_make(void)
{
	uint32_t index;

	for (index = 0; index < 256; index++)
	{
		uint32_t value = index << 24;
		int bit;

		for (bit = 0; bit < 8; bit++)
			value = value & 0x80000000UL ? (value << 1) ^ 0x04C11DB7UL : value << 1;
		crc_table[index] = value;
	}
}

static void pages_checksum(unsigned char *data, uint32_t size)
{
	uint32_t position = 0;

	if (!crc_table[1])
		crc_table_make();
	while (position + 27 <= size)
	{
		uint32_t segments, header, body = 0, index, crc = 0;

		if (memcmp(data + position, "OggS", 4))
		{
			position++;
			continue;
		}
		segments = data[position + 26];
		header = 27 + segments;
		if (position + header > size)
			break;
		for (index = 0; index < segments; index++)
			body += data[position + 27 + index];
		if (position + header + body > size)
			break;
		memset(data + position + 22, 0, 4);
		for (index = 0; index < header + body; index++)
			crc = (crc << 8) ^ crc_table[((crc >> 24) ^ data[position + index]) & 0xff];
		data[position + 22] = (unsigned char)crc;
		data[position + 23] = (unsigned char)(crc >> 8);
		data[position + 24] = (unsigned char)(crc >> 16);
		data[position + 25] = (unsigned char)(crc >> 24);
		position += header + body;
	}
}

static int stream_read(void *context, uint32_t offset, uint32_t size, void *buffer)
{
	(void)context;
	if (offset > stream_size || size > stream_size - offset)
		abort(); /* (the decoder reads only within the stream) */
	memcpy(buffer, stream_copy + offset, size);
	return 1;
}

/* the stream's bytes as Xbox ADPCM blocks, decoded and taken from a rate
its last two bytes give to one its third from last picks */
static void adpcm_rate_pass(const uint8_t *data, int channels, int length)
{
	static long const output_rates[] = { 22050, 44100, 11025, 16000, 32000, 48000, 8000, 48001 };
	uint32_t block_bytes = XBOX_ADPCM_BLOCK_BYTES * (uint32_t)channels;
	uint32_t blocks = stream_size / block_bytes;
	struct xbox_adpcm_encoder encoder;
	struct xbox_adpcm_rate rate;
	unsigned char *output;
	uint32_t output_bytes, frames, block, index;
	long input_rate = 44100, output_rate = 22050;

	(void)data;
	if (stream_size >= 3)
	{
		/* (500 Hz to 197 kHz: the bounds and past them) */
		input_rate = 500 + (long)((stream_copy[stream_size - 1] | stream_copy[stream_size - 2] << 8) * 3);
		output_rate = output_rates[stream_copy[stream_size - 3] & 7];
	}
	frames = xbox_adpcm_rate_frames((uint64_t)blocks * XBOX_ADPCM_BLOCK_SAMPLES, input_rate, output_rate);
	output_bytes = xbox_adpcm_bytes((uint32_t)((uint64_t)frames * length / 8), channels);
	if (output_bytes > OGG_SOUND_MAXIMUM_ADPCM_BYTES)
		output_bytes = OGG_SOUND_MAXIMUM_ADPCM_BYTES;
	output = malloc(output_bytes + 2 * GUARD_BYTES);
	if (!output)
		return;
	memset(output, GUARD_VALUE, output_bytes + 2 * GUARD_BYTES);
	xbox_adpcm_encoder_begin(&encoder, output + GUARD_BYTES, output_bytes, channels);
	if (xbox_adpcm_rate_begin(&rate, &encoder, input_rate, output_rate) != (xbox_adpcm_rate_frames(1, input_rate, output_rate) != 0))
		abort(); /* (the converter and the count agree on the bounds) */
	for (block = 0; block < blocks && !encoder.full; block++)
	{
		short decoded[XBOX_ADPCM_BLOCK_SAMPLES * XBOX_ADPCM_MAXIMUM_CHANNELS];
		int frame;

		xbox_adpcm_block_decode(stream_copy + block * block_bytes, channels, decoded);
		for (frame = 0; frame < XBOX_ADPCM_BLOCK_SAMPLES; frame++)
		{
			int values[XBOX_ADPCM_MAXIMUM_CHANNELS];
			int channel;

			for (channel = 0; channel < channels; channel++)
				values[channel] = decoded[frame * channels + channel];
			xbox_adpcm_rate_frame(&rate, values);
		}
	}
	xbox_adpcm_rate_finish(&rate);
	xbox_adpcm_encoder_finish(&encoder);
	/* (the converter gives out at most the frames counted, and the output
	holds no more than its blocks) */
	if (encoder.frames > frames || encoder.frames > output_bytes / block_bytes * XBOX_ADPCM_BLOCK_SAMPLES)
		abort();
	for (index = 0; index < GUARD_BYTES; index++)
	{
		if (output[index] != GUARD_VALUE || output[GUARD_BYTES + output_bytes + index] != GUARD_VALUE)
			abort();
	}
	adpcm_passes++;
	free(output);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct ogg_sound_stream_info info;
	struct ogg_sound_request request;
	struct ogg_sound_result result;
	unsigned char *output;
	uint32_t output_bytes, frames, index;
	int channels, length;
	long rate;

	if (!size || size - 1 > MAXIMUM_INPUT_BYTES)
		return 0;
	channels = data[0] & 1 ? 2 : 1;
	rate = data[0] & 2 ? 44100 : 22050;
	length = data[0] >> 4;
	stream_size = (uint32_t)size - 1;
	memcpy(stream_copy, data + 1, stream_size);
	if (data[0] & 8)
		pages_checksum(stream_copy, stream_size);

	/* as the sound cache measures it */
	frames = rate / 2;
	if (ogg_sound_measure(stream_copy, stream_size < HEAD_BYTES ? stream_size : HEAD_BYTES,
		stream_copy + (stream_size > TAIL_BYTES ? stream_size - TAIL_BYTES : 0),
		stream_size < TAIL_BYTES ? stream_size : TAIL_BYTES, &info))
	{
		uint32_t measured = ogg_sound_output_frames(info.frames, info.rate, rate);

		if (measured)
			frames = measured;
	}
	output_bytes = ogg_sound_adpcm_bytes((uint32_t)((uint64_t)frames * length / 8), channels);
	if (output_bytes > OGG_SOUND_MAXIMUM_ADPCM_BYTES)
		output_bytes = OGG_SOUND_MAXIMUM_ADPCM_BYTES;
	output = malloc(output_bytes + 2 * GUARD_BYTES);
	if (!output)
		return 0;
	memset(output, GUARD_VALUE, output_bytes + 2 * GUARD_BYTES);

	memset(&request, 0, sizeof(request));
	request.read = stream_read;
	request.input_bytes = stream_size;
	request.output = output + GUARD_BYTES;
	request.output_bytes = output_bytes;
	request.channels = channels;
	request.rate = rate;
	request.working = working_memory;
	request.working_bytes = data[0] & 4 ? OGG_SOUND_MINIMUM_WORKING_BYTES : sizeof(working_memory);
	ogg_sound_transcode(&request, &result);
	if ((unsigned)result.status >= NUMBER_OF_OGG_SOUND_STATUSES ||
		result.frames_decoded + result.frames_padded >
			output_bytes / (OGG_SOUND_ADPCM_BLOCK_BYTES * channels) * OGG_SOUND_ADPCM_BLOCK_SAMPLES)
	{
		abort();
	}
	statuses[result.status]++;
	for (index = 0; index < GUARD_BYTES; index++)
	{
		if (output[index] != GUARD_VALUE || output[GUARD_BYTES + output_bytes + index] != GUARD_VALUE)
			abort();
	}
	free(output);
	adpcm_rate_pass(data, channels, length);
	return 0;
}

#ifndef OGG_FUZZ_LIBFUZZER
static unsigned long long random_state = 0x9E3779B97F4A7C15ULL;

static unsigned long next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return (unsigned long)(random_state >> 11);
}

struct seed
{
	unsigned char *data;
	unsigned long size;
};

/* a change of the stream (after the first byte): bytes flipped or set to
an edge value, a cut, a part repeated, a part of another seed spliced in */
static unsigned long mutate(unsigned char *data, unsigned long size, unsigned long capacity, struct seed const *seeds,
	int seed_count)
{
	static const unsigned char edges[] = { 0, 1, 0x7F, 0x80, 0xFF };
	unsigned long changes = 1 + next_random() % 6, change;

	for (change = 0; change < changes && size > 1; change++)
	{
		unsigned long at = 1 + next_random() % (size - 1);

		switch (next_random() % 6)
		{
		case 0:
		case 1:
			data[at] ^= (unsigned char)(1 << (next_random() % 8));
			break;
		case 2:
			data[at] = edges[next_random() % sizeof(edges)];
			break;
		case 3:
			if (next_random() % 4 == 0)
				size = at;
			break;
		case 4:
		{
			unsigned long length = 1 + next_random() % 256;

			if (at + length <= size && size + length <= capacity)
			{
				memmove(data + at + length, data + at, size - at);
				size += length;
			}
			break;
		}
		default:
		{
			struct seed const *other = &seeds[next_random() % seed_count];
			unsigned long from = next_random() % other->size;
			unsigned long length = 1 + next_random() % 512;

			if (from + length <= other->size && at + length <= size)
				memcpy(data + at, other->data + from, length);
			break;
		}
		}
	}
	return size;
}

int main(int argc, char **argv)
{
	static char const *const names[] = { "sine_22050_1.ogg", "sine_22050_2.ogg", "sine_44100_1.ogg", "sine_44100_2.ogg" };
	char const *cases = getenv("OGG_SOUND_CASES") ? getenv("OGG_SOUND_CASES") : "ogg_sound_cases";
	unsigned long iterations = getenv("OGG_FUZZ_ITERATIONS") ? strtoul(getenv("OGG_FUZZ_ITERATIONS"), NULL, 10) : 20000;
	struct seed seeds[4];
	static unsigned char input[MAXIMUM_INPUT_BYTES / 4];
	unsigned long iteration;
	int seed_count = 0, index;

	(void)argc;
	(void)argv;
	for (index = 0; index < 4; index++)
	{
		char path[1024];
		FILE *file;
		long size;

		snprintf(path, sizeof(path), "%s/%s", cases, names[index]);
		file = fopen(path, "rb");
		if (!file)
		{
			printf("FAIL: no %s (OGG_SOUND_CASES)\n", path);
			return 1;
		}
		fseek(file, 0, SEEK_END);
		size = ftell(file);
		fseek(file, 0, SEEK_SET);
		seeds[seed_count].data = malloc((size_t)size + 1);
		/* (the first byte: as the stream is, its checksums made good,
		the whole output) */
		seeds[seed_count].data[0] = (unsigned char)((index & 1) | (index & 2) | 8 | 8 << 4);
		if (fread(seeds[seed_count].data + 1, 1, (size_t)size, file) != (size_t)size)
			return 1;
		seeds[seed_count].size = (unsigned long)size + 1;
		seed_count++;
		fclose(file);
	}
	for (index = 0; index < seed_count; index++)
		LLVMFuzzerTestOneInput(seeds[index].data, seeds[index].size);
	for (iteration = 0; iteration < iterations; iteration++)
	{
		struct seed const *seed = &seeds[next_random() % seed_count];
		unsigned long size = seed->size;

		memcpy(input, seed->data, size);
		/* (mostly with good checksums, which get the changes to Tremor) */
		input[0] = (unsigned char)(next_random() & 0xff);
		if (next_random() % 4)
			input[0] |= 8;
		size = mutate(input, size, sizeof(input), seeds, seed_count);
		LLVMFuzzerTestOneInput(input, size);
	}
	printf("PASS: %lu changed streams: %lu decoded, %lu not Vorbis, %lu unsupported, %lu out of working memory, "
		"%lu other\n", iterations + seed_count, statuses[_ogg_sound_ok], statuses[_ogg_sound_not_vorbis],
		statuses[_ogg_sound_unsupported_format], statuses[_ogg_sound_out_of_memory],
		statuses[_ogg_sound_read_failed] + statuses[_ogg_sound_bad_arguments]);
	printf("PASS: %lu of them also as Xbox ADPCM at other rates, written within the output\n", adpcm_passes);
	for (index = 0; index < seed_count; index++)
		free(seeds[index].data);
	return 0;
}
#endif
