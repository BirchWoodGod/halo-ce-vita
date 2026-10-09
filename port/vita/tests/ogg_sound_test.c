/*
OGG_SOUND_TEST.C

Tests of Halo Custom Edition's Ogg Vorbis sounds made Xbox ADPCM
(port/linux/src/ogg_sound.c), run by run_ogg_sound_test.sh with
AddressSanitizer and UBSan:
	- the four streams in ogg_sound_cases (half-scale sines, 440 Hz and
	  660 Hz on the right, 0.5 s; 22 and 44 kHz, mono and stereo) are
	  measured and decoded to the length they say, the 44 kHz mono one at
	  22 kHz; what the mixer's ADPCM decoder makes of the output
	  (dsound_sdl.c, decode_adpcm_blocks, copied here) is the sine, in step
	  (20 dB at least above the difference) and at its pitch;
	- an output too short is filled and the rest left out, one too long is
	  followed by silence, a mono stream is played on both sides of a
	  stereo sound and a stereo one mixed for a mono sound;
	- too little working memory, a rate the sound cannot have, bad
	  arguments, something that is not Ogg Vorbis, every cut of the
	  streams, and bytes changed in them (with the pages' checksums made
	  good again, so the changes reach the decoder) fail cleanly, writing
	  only within the output;
	- with --resource-map <sounds.map> (Halo PC's, the player's own; not in
	  this repository), every Ogg Vorbis permutation in it is decoded to
	  the length its buffer size says.
Exits 0 when every test passed.
*/

#include "ogg_sound.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	GUARD_BYTES = 256,
	GUARD_VALUE = 0xA5,
};

static int failures;
static int checks;

#define CHECK(condition, ...) \
	do { checks++; if (!(condition)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double working_memory[OGG_SOUND_WORKING_BYTES / sizeof(double)];

/* ---------- streams in memory */

struct stream
{
	unsigned char *data;
	uint32_t size;
};

static int stream_read(void *context, uint32_t offset, uint32_t size, void *buffer)
{
	struct stream const *stream = (struct stream const *)context;

	if (offset > stream->size || size > stream->size - offset)
		return 0;
	memcpy(buffer, stream->data + offset, size);
	return 1;
}

static struct stream stream_load(char const *path)
{
	struct stream stream = { NULL, 0 };
	FILE *file = fopen(path, "rb");
	long size;

	if (!file)
		return stream;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	stream.data = malloc(size > 0 ? (size_t)size : 1);
	if (stream.data && size > 0 && fread(stream.data, 1, (size_t)size, file) == (size_t)size)
		stream.size = (uint32_t)size;
	fclose(file);
	return stream;
}

/* ---------- the output, between guards */

struct output
{
	unsigned char *memory;
	unsigned char *bytes;
	uint32_t size;
};

static struct output output_new(uint32_t size)
{
	struct output output;

	output.memory = malloc(size + 2 * GUARD_BYTES);
	memset(output.memory, GUARD_VALUE, size + 2 * GUARD_BYTES);
	output.bytes = output.memory + GUARD_BYTES;
	output.size = size;
	return output;
}

static int output_guards_intact(struct output const *output)
{
	uint32_t index;

	for (index = 0; index < GUARD_BYTES; index++)
	{
		if (output->memory[index] != GUARD_VALUE || output->bytes[output->size + index] != GUARD_VALUE)
			return 0;
	}
	return 1;
}

static enum ogg_sound_status transcode(struct stream *stream, struct output *output, int channels, long rate,
	void *working, size_t working_bytes, struct ogg_sound_result *result)
{
	struct ogg_sound_request request;

	memset(&request, 0, sizeof(request));
	request.read = stream_read;
	request.read_context = stream;
	request.input_bytes = stream->size;
	request.output = output->bytes;
	request.output_bytes = output->size;
	request.channels = channels;
	request.rate = rate;
	request.working = working;
	request.working_bytes = working_bytes;
	return ogg_sound_transcode(&request, result);
}

/* ---------- the mixer's Xbox ADPCM decoder (dsound_sdl.c) */

static const short step_table[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767,
};
static const signed char index_table[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static int expand(int nibble, int *predictor, int *index)
{
	int step = step_table[*index];
	int difference = step >> 3;

	if (nibble & 1) difference += step >> 2;
	if (nibble & 2) difference += step >> 1;
	if (nibble & 4) difference += step;
	if (nibble & 8) difference = -difference;
	*predictor += difference;
	if (*predictor > 32767) *predictor = 32767;
	if (*predictor < -32768) *predictor = -32768;
	*index += index_table[nibble];
	if (*index < 0) *index = 0;
	if (*index > 88) *index = 88;
	return *predictor;
}

/* every block of `bytes` into interleaved samples; returns the frames */
static uint32_t adpcm_decode(unsigned char const *source, uint32_t bytes, int channels, short *samples)
{
	uint32_t block_bytes = OGG_SOUND_ADPCM_BLOCK_BYTES * (uint32_t)channels;
	uint32_t blocks = bytes / block_bytes;
	uint32_t block;

	for (block = 0; block < blocks; block++)
	{
		unsigned char const *data = source + block * block_bytes;
		short *output = samples + block * OGG_SOUND_ADPCM_BLOCK_SAMPLES * channels;
		int channel;

		for (channel = 0; channel < channels; channel++)
		{
			unsigned char const *header = data + channel * 4;
			int predictor = (short)(header[0] | (header[1] << 8));
			int index = header[2] > 88 ? 88 : header[2];
			int group, byte;

			output[channel] = (short)predictor;
			for (group = 0; group < 8; group++)
			{
				unsigned char const *nibbles = data + 4 * channels + (group * channels + channel) * 4;

				for (byte = 0; byte < 4; byte++)
				{
					int sample = group * 8 + byte * 2 + 1;

					output[sample * channels + channel] = (short)expand(nibbles[byte] & 0xf, &predictor, &index);
					if (sample + 1 < OGG_SOUND_ADPCM_BLOCK_SAMPLES)
						output[(sample + 1) * channels + channel] = (short)expand(nibbles[byte] >> 4, &predictor, &index);
				}
			}
		}
	}
	return blocks * OGG_SOUND_ADPCM_BLOCK_SAMPLES;
}

/* the ratio, in dB, of a half-scale sine at `frequency` (in step from the
first frame) to its difference from channel `channel` of the samples */
static double sine_signal_to_noise(short const *samples, uint32_t frames, int channels, int channel, double frequency,
	long rate)
{
	double signal = 0.0, noise = 0.0;
	uint32_t frame;

	for (frame = 0; frame < frames; frame++)
	{
		double expected = 0.5 * 32767.0 * sin(2.0 * M_PI * frequency * (double)frame / (double)rate);
		double difference = samples[frame * channels + channel] - expected;

		signal += expected * expected;
		noise += difference * difference;
	}
	return noise > 0.0 ? 10.0 * log10(signal / noise) : 99.0;
}

static uint32_t zero_crossings(short const *samples, uint32_t frames, int channels, int channel)
{
	uint32_t frame, count = 0;

	for (frame = 1; frame < frames; frame++)
	{
		if ((samples[(frame - 1) * channels + channel] < 0) != (samples[frame * channels + channel] < 0))
			count++;
	}
	return count;
}

/* ---------- Ogg page checksums (the fuzzing below makes them good again) */

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

/* every whole page's checksum, from its bytes as they are */
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

/* ---------- the cases */

struct fixture
{
	char const *name;
	long stream_rate;
	int stream_channels;
	/* the rate it is played at (the game's: 22 kHz mono, else as it is) */
	long rate;
	uint32_t frames;
	struct stream stream;
};

static struct fixture fixtures[] =
{
	{ "sine_22050_1.ogg", 22050, 1, 22050, 11025 },
	{ "sine_22050_2.ogg", 22050, 2, 22050, 11025 },
	{ "sine_44100_1.ogg", 44100, 1, 22050, 11025 },
	{ "sine_44100_2.ogg", 44100, 2, 44100, 22050 },
};
enum { FIXTURE_COUNT = sizeof(fixtures) / sizeof(fixtures[0]) };

static void test_fixture(struct fixture *fixture)
{
	struct ogg_sound_stream_info info;
	struct ogg_sound_result result;
	uint32_t bytes = ogg_sound_adpcm_bytes(fixture->frames, fixture->stream_channels);
	struct output output = output_new(bytes);
	short *samples = malloc(sizeof(short) * (bytes / OGG_SOUND_ADPCM_BLOCK_BYTES) * OGG_SOUND_ADPCM_BLOCK_SAMPLES + 2);
	uint32_t frames;
	double rms;
	int channel;

	CHECK(ogg_sound_measure(fixture->stream.data, fixture->stream.size, fixture->stream.data, fixture->stream.size, &info),
		"%s: not measured", fixture->name);
	CHECK(info.channels == fixture->stream_channels && info.rate == fixture->stream_rate &&
		ogg_sound_output_frames(info.frames, info.rate, fixture->rate) == fixture->frames,
		"%s: measured %d channels %ld Hz %llu frames", fixture->name, info.channels, info.rate,
		(unsigned long long)info.frames);

	transcode(&fixture->stream, &output, fixture->stream_channels, fixture->rate, working_memory, sizeof(working_memory),
		&result);
	rms = result.frames_decoded ? sqrt(result.sum_of_squares / ((double)result.frames_decoded * fixture->stream_channels)) : 0;
	printf("  %s: %s, %u frames (+%u silent), rms %.0f, peak %d, %zu KB working%s\n", fixture->name,
		ogg_sound_status_describe(result.status), result.frames_decoded, result.frames_padded, rms, result.peak,
		result.working_peak_bytes / 1024, result.halved ? ", halved" : "");
	CHECK(result.status == _ogg_sound_ok, "%s: %s", fixture->name, ogg_sound_status_describe(result.status));
	CHECK(result.frames_decoded == fixture->frames && !result.truncated &&
		result.frames_padded < OGG_SOUND_ADPCM_BLOCK_SAMPLES,
		"%s: %u frames decoded, %u padded, truncated %d", fixture->name, result.frames_decoded, result.frames_padded,
		result.truncated);
	CHECK(result.halved == (fixture->rate != fixture->stream_rate), "%s: halved %d", fixture->name, result.halved);
	/* (a half-scale sine: 11585) */
	CHECK(rms > 10500 && rms < 12500, "%s: rms %.0f", fixture->name, rms);
	CHECK(output_guards_intact(&output), "%s: written outside the output", fixture->name);

	frames = adpcm_decode(output.bytes, output.size, fixture->stream_channels, samples);
	CHECK(frames >= fixture->frames, "%s: %u frames of ADPCM", fixture->name, frames);
	for (channel = 0; channel < fixture->stream_channels; channel++)
	{
		double frequency = channel ? 660.0 : 440.0;
		double snr = sine_signal_to_noise(samples, fixture->frames, fixture->stream_channels, channel, frequency,
			fixture->rate);
		uint32_t crossings = zero_crossings(samples, fixture->frames, fixture->stream_channels, channel);
		uint32_t expected = (uint32_t)(2.0 * frequency * fixture->frames / fixture->rate);

		printf("    channel %d as played: %.1f dB above the difference from a %.0f Hz sine, %u zero crossings (%u)\n",
			channel, snr, frequency, crossings, expected);
		CHECK(snr > 20.0, "%s channel %d: %.1f dB", fixture->name, channel, snr);
		CHECK(crossings + 4 >= expected && crossings <= expected + 4, "%s channel %d: %u crossings, not %u",
			fixture->name, channel, crossings, expected);
	}
	free(samples);
	free(output.memory);
}

/* an output shorter than the stream, and one longer */
static void test_lengths(struct fixture *fixture)
{
	int channels = fixture->stream_channels;
	uint32_t half = fixture->frames / 2 / OGG_SOUND_ADPCM_BLOCK_SAMPLES * OGG_SOUND_ADPCM_BLOCK_SAMPLES;
	uint32_t block_bytes = OGG_SOUND_ADPCM_BLOCK_BYTES * (uint32_t)channels;
	struct ogg_sound_result result;
	struct output output = output_new(ogg_sound_adpcm_bytes(half, channels));
	uint32_t index;
	int silent = 1;

	transcode(&fixture->stream, &output, channels, fixture->rate, working_memory, sizeof(working_memory), &result);
	CHECK(result.status == _ogg_sound_ok && result.truncated && result.frames_decoded == half && !result.frames_padded,
		"%s cut to %u frames: %s, %u decoded, %u padded, truncated %d", fixture->name, half,
		ogg_sound_status_describe(result.status), result.frames_decoded, result.frames_padded, result.truncated);
	CHECK(output_guards_intact(&output), "%s cut: written outside the output", fixture->name);
	free(output.memory);

	/* twice as long, and a part block more (which is not used) */
	output = output_new(ogg_sound_adpcm_bytes(fixture->frames * 2, channels) + block_bytes / 2);
	transcode(&fixture->stream, &output, channels, fixture->rate, working_memory, sizeof(working_memory), &result);
	CHECK(result.status == _ogg_sound_ok && !result.truncated && result.frames_decoded == fixture->frames &&
		result.frames_decoded + result.frames_padded == (output.size / block_bytes) * OGG_SOUND_ADPCM_BLOCK_SAMPLES,
		"%s lengthened: %u decoded, %u padded", fixture->name, result.frames_decoded, result.frames_padded);
	for (index = ogg_sound_adpcm_bytes(fixture->frames, channels); index < output.size / block_bytes * block_bytes; index++)
		silent &= output.bytes[index] == 0;
	CHECK(silent, "%s lengthened: the rest is not silence", fixture->name);
	CHECK(output.bytes[output.size - 1] == GUARD_VALUE, "%s lengthened: a part block was written", fixture->name);
	CHECK(output_guards_intact(&output), "%s lengthened: written outside the output", fixture->name);
	free(output.memory);

	/* none */
	output = output_new(0);
	transcode(&fixture->stream, &output, channels, fixture->rate, working_memory, sizeof(working_memory), &result);
	CHECK(result.status == _ogg_sound_ok && !result.frames_decoded && output_guards_intact(&output),
		"%s into nothing: %s, %u frames", fixture->name, ogg_sound_status_describe(result.status), result.frames_decoded);
	free(output.memory);
}

/* the other number of channels than the stream's */
static void test_channels(void)
{
	struct ogg_sound_result result;
	struct fixture *mono = &fixtures[0], *stereo = &fixtures[1];
	struct output output = output_new(ogg_sound_adpcm_bytes(mono->frames, 2));
	short *samples = malloc(sizeof(short) * 2 * (mono->frames + OGG_SOUND_ADPCM_BLOCK_SAMPLES));
	uint32_t frame;
	int same = 1;

	transcode(&mono->stream, &output, 2, 22050, working_memory, sizeof(working_memory), &result);
	CHECK(result.status == _ogg_sound_ok && result.frames_decoded == mono->frames, "mono as stereo: %s, %u frames",
		ogg_sound_status_describe(result.status), result.frames_decoded);
	adpcm_decode(output.bytes, output.size, 2, samples);
	for (frame = 0; frame < mono->frames; frame++)
		same &= samples[frame * 2] == samples[frame * 2 + 1];
	CHECK(same, "mono as stereo: the sides differ");
	CHECK(sine_signal_to_noise(samples, mono->frames, 2, 1, 440.0, 22050) > 20.0, "mono as stereo: not the sine");
	free(output.memory);

	output = output_new(ogg_sound_adpcm_bytes(stereo->frames, 1));
	transcode(&stereo->stream, &output, 1, 22050, working_memory, sizeof(working_memory), &result);
	CHECK(result.status == _ogg_sound_ok && result.frames_decoded == stereo->frames, "stereo as mono: %s, %u frames",
		ogg_sound_status_describe(result.status), result.frames_decoded);
	adpcm_decode(output.bytes, output.size, 1, samples);
	/* (440 Hz and 660 Hz mixed: their sum's crossings, 4 a 1/220 s) */
	CHECK(zero_crossings(samples, stereo->frames, 1, 0) > 400, "stereo as mono: %u crossings",
		zero_crossings(samples, stereo->frames, 1, 0));
	CHECK(output_guards_intact(&output), "stereo as mono: written outside the output");
	free(output.memory);
	free(samples);
}

static void test_refusals(void)
{
	struct fixture *fixture = &fixtures[1];
	struct ogg_sound_result result;
	struct output output = output_new(ogg_sound_adpcm_bytes(fixture->frames, 2));
	struct ogg_sound_request request;
	unsigned char garbage[4096];
	struct stream stream;
	uint32_t index;

	/* too little working memory: refused, and the output silence */
	transcode(&fixture->stream, &output, 2, 22050, working_memory, OGG_SOUND_MINIMUM_WORKING_BYTES, &result);
	CHECK(result.status == _ogg_sound_out_of_memory, "too little working memory: %s",
		ogg_sound_status_describe(result.status));
	for (index = 0; index < output.size && !output.bytes[index]; index++)
		;
	CHECK(index == output.size && output_guards_intact(&output), "too little working memory: the output is not silence");

	/* a 22 kHz stream for a 44 kHz sound, or a 11025 Hz one */
	transcode(&fixture->stream, &output, 2, 44100, working_memory, sizeof(working_memory), &result);
	CHECK(result.status == _ogg_sound_unsupported_format, "22 kHz for 44 kHz: %s", ogg_sound_status_describe(result.status));
	transcode(&fixture->stream, &output, 2, 11025, working_memory, sizeof(working_memory), &result);
	CHECK(result.status == _ogg_sound_ok && result.halved && result.frames_decoded == (fixture->frames + 1) / 2,
		"22 kHz for 11 kHz: %s, halved %d, %u frames", ogg_sound_status_describe(result.status), result.halved,
		result.frames_decoded);

	/* bad arguments */
	memset(&request, 0, sizeof(request));
	request.read = stream_read;
	request.read_context = &fixture->stream;
	request.input_bytes = fixture->stream.size;
	request.output = output.bytes;
	request.output_bytes = output.size;
	request.channels = 3;
	request.rate = 22050;
	request.working = working_memory;
	request.working_bytes = sizeof(working_memory);
	CHECK(ogg_sound_transcode(&request, &result) == _ogg_sound_bad_arguments, "three channels taken");
	request.channels = 2;
	request.working = (char *)working_memory + 4;
	CHECK(ogg_sound_transcode(&request, &result) == _ogg_sound_bad_arguments, "misaligned working memory taken");
	request.working = working_memory;
	request.working_bytes = OGG_SOUND_MINIMUM_WORKING_BYTES - 8;
	CHECK(ogg_sound_transcode(&request, &result) == _ogg_sound_bad_arguments, "too small working memory taken");
	request.working_bytes = sizeof(working_memory);
	request.rate = 0;
	CHECK(ogg_sound_transcode(&request, &result) == _ogg_sound_bad_arguments, "a rate of 0 taken");
	request.rate = 22050;
	request.output = NULL;
	CHECK(ogg_sound_transcode(&request, &result) == _ogg_sound_bad_arguments, "no output taken");

	/* not Ogg Vorbis */
	for (index = 0; index < sizeof(garbage); index++)
		garbage[index] = (unsigned char)(index * 2654435761u >> 24);
	stream.data = garbage;
	stream.size = sizeof(garbage);
	transcode(&stream, &output, 2, 22050, working_memory, sizeof(working_memory), &result);
	CHECK(result.status == _ogg_sound_not_vorbis && output_guards_intact(&output), "garbage: %s",
		ogg_sound_status_describe(result.status));
	/* an Ogg page of something else: the stream's first page with its
	packet's type changed */
	memcpy(garbage, fixture->stream.data, 58);
	garbage[28] = 0x80;
	pages_checksum(garbage, 58);
	stream.size = 58;
	transcode(&stream, &output, 2, 22050, working_memory, sizeof(working_memory), &result);
	CHECK(result.status == _ogg_sound_not_vorbis, "another codec's page: %s", ogg_sound_status_describe(result.status));
	free(output.memory);
}

static void test_measure(void)
{
	struct ogg_sound_stream_info info;
	struct fixture *fixture = &fixtures[3];
	unsigned char page[64];
	uint32_t cut;

	/* the stream cut short: the last page is not whole, or there is none */
	for (cut = 0; cut < fixture->stream.size; cut += 97)
	{
		int measured = ogg_sound_measure(fixture->stream.data, cut, fixture->stream.data, cut, &info);

		CHECK(!measured || info.frames <= fixture->frames, "cut at %u: measured %llu frames", cut,
			(unsigned long long)info.frames);
	}
	/* a first page that is not Vorbis's */
	memcpy(page, fixture->stream.data, sizeof(page));
	page[29] = 'X';
	CHECK(!ogg_sound_measure(page, sizeof(page), fixture->stream.data, fixture->stream.size, &info),
		"not Vorbis measured");
	CHECK(!ogg_sound_measure(NULL, 0, NULL, 0, &info), "nothing measured");
	/* a last page with no granule position (-1) is passed over */
	{
		unsigned char *copy = malloc(fixture->stream.size);
		int measured;

		memcpy(copy, fixture->stream.data, fixture->stream.size);
		for (cut = fixture->stream.size - 27; cut > 0 && memcmp(copy + cut, "OggS", 4); cut--)
			;
		memset(copy + cut + 6, 0xff, 8);
		measured = ogg_sound_measure(copy, fixture->stream.size, copy, fixture->stream.size, &info);
		CHECK(measured && info.frames < (uint64_t)fixture->frames, "a last page of no position: %d, %llu frames",
			measured, (unsigned long long)info.frames);
		free(copy);
	}
	CHECK(ogg_sound_output_frames(100, 44100, 22050) == 50 && ogg_sound_output_frames(101, 44100, 22050) == 51 &&
		ogg_sound_output_frames(100, 22050, 22050) == 100 && ogg_sound_output_frames(100, 32000, 22050) == 0 &&
		ogg_sound_output_frames(100, 22050, 44100) == 0 && ogg_sound_output_frames(1ull << 33, 22050, 22050) == 0,
		"output frames");
	CHECK(ogg_sound_adpcm_bytes(1, 1) == 36 && ogg_sound_adpcm_bytes(64, 2) == 72 && ogg_sound_adpcm_bytes(65, 2) == 144 &&
		ogg_sound_adpcm_bytes(0, 1) == 0 && ogg_sound_adpcm_bytes(64, 3) == 0, "ADPCM bytes");
}

/* every cut of the streams, and every byte changed (the pages' checksums
made good, so the change reaches the decoder) */
static void test_damage(struct fixture *fixture)
{
	struct ogg_sound_result result;
	struct output output = output_new(ogg_sound_adpcm_bytes(fixture->frames, fixture->stream_channels));
	struct stream damaged;
	uint32_t position;
	unsigned long statuses[NUMBER_OF_OGG_SOUND_STATUSES] = { 0 };
	int intact = 1;

	damaged.data = malloc(fixture->stream.size);
	for (position = 0; position < fixture->stream.size; position += 13)
	{
		damaged.size = position;
		memcpy(damaged.data, fixture->stream.data, position);
		transcode(&damaged, &output, fixture->stream_channels, fixture->rate, working_memory, sizeof(working_memory),
			&result);
		statuses[result.status]++;
		intact &= output_guards_intact(&output);
	}
	damaged.size = fixture->stream.size;
	for (position = 0; position < fixture->stream.size; position += 3)
	{
		static unsigned char const values[] = { 0x00, 0xff, 0x80, 0x7f, 0x01 };

		memcpy(damaged.data, fixture->stream.data, fixture->stream.size);
		damaged.data[position] ^= position & 1 ? (unsigned char)(1 << (position % 8)) : 0;
		if (!(position & 1))
			damaged.data[position] = values[(position / 2) % 5];
		pages_checksum(damaged.data, damaged.size);
		transcode(&damaged, &output, fixture->stream_channels, fixture->rate, working_memory, sizeof(working_memory),
			&result);
		statuses[result.status]++;
		intact &= output_guards_intact(&output);
	}
	printf("  %s cut and changed: %lu decoded, %lu not Vorbis, %lu unsupported, %lu out of memory\n", fixture->name,
		statuses[_ogg_sound_ok], statuses[_ogg_sound_not_vorbis], statuses[_ogg_sound_unsupported_format],
		statuses[_ogg_sound_out_of_memory]);
	CHECK(intact, "%s damaged: written outside the output", fixture->name);
	free(damaged.data);
	free(output.memory);
}

/* ---------- Halo PC's sounds.map (OpenSauce data_file_structures.hpp; the
loader's resource_sound_load: an entry is the sound's header, then its
pitch ranges, whose addresses count from the first) */

static uint32_t u32_at(unsigned char const *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void test_resource_map(char const *path)
{
	struct stream map = stream_load(path);
	uint32_t index_offset, count, item;
	unsigned long tried = 0, decoded = 0, exact = 0;
	double longest = 0.0;

	if (!map.size || map.size < 16)
	{
		CHECK(0, "%s: not read", path);
		return;
	}
	index_offset = u32_at(map.data + 8);
	count = u32_at(map.data + 12);
	for (item = 0; item < count && index_offset + (item + 1) * 12 <= map.size; item++)
	{
		uint32_t size = u32_at(map.data + index_offset + item * 12 + 4);
		uint32_t offset = u32_at(map.data + index_offset + item * 12 + 8);
		unsigned char const *entry = map.data + offset;
		uint32_t pitch_ranges, range;
		int channels;
		long rate;

		if (offset > map.size || size > map.size - offset || size < 0xA4 || entry[0x6E] != 3)
			continue;
		channels = entry[0x6C] == 1 ? 2 : 1;
		rate = entry[0x06] == 1 ? 44100 : 22050;
		pitch_ranges = u32_at(entry + 0x98);
		for (range = 0; range < pitch_ranges && 0xA4 + (range + 1) * 0x48 <= size; range++)
		{
			unsigned char const *pitch_range = entry + 0xA4 + range * 0x48;
			uint32_t permutations = u32_at(pitch_range + 0x3C);
			uint32_t first = u32_at(pitch_range + 0x40);
			uint32_t permutation;

			for (permutation = 0; permutation < permutations; permutation++)
			{
				uint32_t at = 0xA4 + first + permutation * 0x7C;
				unsigned char const *element;
				struct ogg_sound_result result;
				struct stream stream;
				struct output output;
				uint32_t frames;

				if (first > size || at + 0x7C > size)
					break;
				element = entry + at;
				stream.data = map.data + u32_at(element + 0x48);
				stream.size = u32_at(element + 0x40);
				if (u32_at(element + 0x48) > map.size || stream.size > map.size - u32_at(element + 0x48) ||
					element[0x28] != 3)
				{
					continue;
				}
				/* (a 44 kHz mono sound is played at 22 kHz) */
				frames = ogg_sound_output_frames(u32_at(element + 0x38) / (2 * channels), rate,
					channels == 1 ? 22050 : rate);
				output = output_new(ogg_sound_adpcm_bytes(frames, channels));
				transcode(&stream, &output, channels, channels == 1 ? 22050 : rate, working_memory, sizeof(working_memory),
					&result);
				tried++;
				decoded += result.status == _ogg_sound_ok;
				exact += result.status == _ogg_sound_ok && result.frames_decoded == frames;
				longest = longest > (double)frames / (channels == 1 ? 22050 : rate) ? longest :
					(double)frames / (channels == 1 ? 22050 : rate);
				CHECK(result.status == _ogg_sound_ok && result.frames_decoded == frames && output_guards_intact(&output),
					"%s: item %u permutation %u: %s, %u of %u frames", path, item, permutation,
					ogg_sound_status_describe(result.status), result.frames_decoded, frames);
				free(output.memory);
			}
		}
	}
	printf("  %s: %lu Ogg Vorbis permutations, %lu decoded, %lu to the length their buffer size says (longest %.1f s)\n",
		path, tried, decoded, exact, longest);
	CHECK(tried > 0, "%s: no Ogg Vorbis permutations", path);
	free(map.data);
}

int main(int argc, char **argv)
{
	char const *cases = getenv("OGG_SOUND_CASES") ? getenv("OGG_SOUND_CASES") : "ogg_sound_cases";
	int index;

	for (index = 0; index < FIXTURE_COUNT; index++)
	{
		char path[1024];

		snprintf(path, sizeof(path), "%s/%s", cases, fixtures[index].name);
		fixtures[index].stream = stream_load(path);
		if (!fixtures[index].stream.size)
		{
			printf("FAIL: no %s (OGG_SOUND_CASES)\n", path);
			return 1;
		}
	}
	printf("streams:\n");
	for (index = 0; index < FIXTURE_COUNT; index++)
		test_fixture(&fixtures[index]);
	printf("lengths, channels, refusals, measuring:\n");
	for (index = 0; index < FIXTURE_COUNT; index++)
		test_lengths(&fixtures[index]);
	test_channels();
	test_refusals();
	test_measure();
	printf("damage:\n");
	for (index = 0; index < FIXTURE_COUNT; index++)
		test_damage(&fixtures[index]);
	for (index = 1; index + 1 < argc; index++)
	{
		if (!strcmp(argv[index], "--resource-map"))
		{
			printf("resource map:\n");
			test_resource_map(argv[++index]);
		}
	}
	for (index = 0; index < FIXTURE_COUNT; index++)
		free(fixtures[index].stream.data);
	printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
