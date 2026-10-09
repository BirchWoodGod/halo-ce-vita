/*
XBOX_ADPCM_ENCODER.C

16-bit PCM written as Xbox ADPCM, the format the mixer decodes
(dsound_sdl.c, decode_adpcm_blocks): per block and channel, the first
sample and the step index in a 4-byte header, then 63 nibbles in 4-byte
groups alternating between channels, low nibble first, and a padding
nibble of 0. The encoder predicts with the decoder's own arithmetic, so
what is played is what the encoder chose, and carries its step index from
block to block. Used for Halo Custom Edition's Ogg Vorbis permutations
(ogg_sound.c) and its 16-bit PCM ones (custom_edition_sounds.c), which the
sound cache holds as Xbox ADPCM.
*/

#include "xbox_adpcm_encoder.h"

#include <string.h>

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

/* ---------- blocks */

static void block_encode(struct xbox_adpcm_encoder *encoder)
{
	int channels = encoder->channels;
	uint8_t *data;
	int channel;
	int frame;

	/* (a part block is held: the last sample repeats, which the ear does
	not hear where silence would click) */
	for (frame = encoder->block_frames; frame < XBOX_ADPCM_BLOCK_SAMPLES; frame++)
		for (channel = 0; channel < channels; channel++)
			encoder->block[frame][channel] = frame ? encoder->block[frame - 1][channel] : 0;

	data = encoder->output + encoder->blocks_written * encoder->block_bytes;
	memset(data, 0, encoder->block_bytes);
	for (channel = 0; channel < channels; channel++)
	{
		int predictor = encoder->block[0][channel];
		int index = encoder->step_index[channel];
		int sample;

		data[channel * 4 + 0] = (uint8_t)(predictor & 0xff);
		data[channel * 4 + 1] = (uint8_t)((predictor >> 8) & 0xff);
		data[channel * 4 + 2] = (uint8_t)index;
		data[channel * 4 + 3] = 0;
		for (sample = 1; sample < XBOX_ADPCM_BLOCK_SAMPLES; sample++)
		{
			int nibble = ima_encode(encoder->block[sample][channel], &predictor, &index);
			int group = (sample - 1) / 8;
			int position = (sample - 1) % 8;
			uint8_t *byte = data + 4 * channels + (group * channels + channel) * 4 + position / 2;

			*byte |= (uint8_t)(position & 1 ? nibble << 4 : nibble);
		}
		encoder->step_index[channel] = index;
	}
	encoder->blocks_written++;
	encoder->block_frames = 0;
	if (encoder->blocks_written >= encoder->output_blocks)
		encoder->full = 1;
}

/* ---------- public code */

uint32_t xbox_adpcm_bytes(uint32_t frames, int channels)
{
	uint32_t blocks = frames / XBOX_ADPCM_BLOCK_SAMPLES + (frames % XBOX_ADPCM_BLOCK_SAMPLES ? 1 : 0);

	if (channels < 1 || channels > XBOX_ADPCM_MAXIMUM_CHANNELS)
		return 0;
	return blocks * XBOX_ADPCM_BLOCK_BYTES * (uint32_t)channels;
}

int xbox_adpcm_encoder_begin(
	struct xbox_adpcm_encoder *encoder,
	void *output,
	uint32_t output_bytes,
	int channels)
{
	memset(encoder, 0, sizeof(*encoder));
	if (!output || channels < 1 || channels > XBOX_ADPCM_MAXIMUM_CHANNELS)
	{
		encoder->full = 1;
		return 0;
	}
	encoder->output = (uint8_t *)output;
	encoder->channels = channels;
	encoder->block_bytes = XBOX_ADPCM_BLOCK_BYTES * (uint32_t)channels;
	encoder->output_blocks = output_bytes / encoder->block_bytes;
	encoder->full = encoder->output_blocks == 0;
	return 1;
}

void xbox_adpcm_encoder_frame(
	struct xbox_adpcm_encoder *encoder,
	int const *frame)
{
	int channel;

	if (encoder->full)
	{
		encoder->truncated = 1;
		return;
	}
	for (channel = 0; channel < encoder->channels; channel++)
	{
		int value = frame[channel];

		if (value > 32767)
			value = 32767;
		if (value < -32768)
			value = -32768;
		encoder->block[encoder->block_frames][channel] = (short)value;
		encoder->sum_of_squares += (double)value * value;
		if (value < 0)
			value = -value;
		if (value > encoder->peak)
			encoder->peak = value;
	}
	encoder->frames++;
	if (++encoder->block_frames == XBOX_ADPCM_BLOCK_SAMPLES)
		block_encode(encoder);
}

void xbox_adpcm_encoder_pcm(
	struct xbox_adpcm_encoder *encoder,
	uint8_t const *pcm,
	uint32_t bytes)
{
	uint32_t frame_bytes = 2 * (uint32_t)encoder->channels;
	uint32_t frames;
	uint32_t index;

	if (!pcm || encoder->channels < 1 || encoder->channels > XBOX_ADPCM_MAXIMUM_CHANNELS)
		return;
	frames = bytes / frame_bytes;
	for (index = 0; index < frames; index++)
	{
		uint8_t const *bytes_of_frame = pcm + index * frame_bytes;
		int frame[XBOX_ADPCM_MAXIMUM_CHANNELS];
		int channel;

		if (encoder->full)
		{
			encoder->truncated = 1;
			return;
		}
		for (channel = 0; channel < encoder->channels; channel++)
			frame[channel] = (int16_t)(uint16_t)(bytes_of_frame[2 * channel] | bytes_of_frame[2 * channel + 1] << 8);
		xbox_adpcm_encoder_frame(encoder, frame);
	}
	return;
}

uint32_t xbox_adpcm_encoder_finish(
	struct xbox_adpcm_encoder *encoder)
{
	uint32_t output_frames = encoder->output_blocks * XBOX_ADPCM_BLOCK_SAMPLES;

	if (!encoder->output)
		return 0;
	if (encoder->block_frames && !encoder->full)
		block_encode(encoder);
	/* the rest is silence: blocks of zeros (a first sample of 0 and
	nibbles of 0 are 0 throughout) */
	if (encoder->blocks_written < encoder->output_blocks)
	{
		memset(encoder->output + encoder->blocks_written * encoder->block_bytes, 0,
			(size_t)(encoder->output_blocks - encoder->blocks_written) * encoder->block_bytes);
		encoder->blocks_written = encoder->output_blocks;
		encoder->full = 1;
	}
	return encoder->frames < output_frames ? output_frames - encoder->frames : 0;
}
