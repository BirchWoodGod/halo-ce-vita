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
sound cache holds as Xbox ADPCM; and, with its rate converter and block
decoder, for its 44 kHz mono sounds (Xbox ADPCM or 16-bit PCM), which the
game plays at 22 kHz only, and its Ogg Vorbis streams of another rate than
their sound's.
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

/* ---------- rates */

static int rate_within(long input_rate, long output_rate)
{
	return input_rate >= XBOX_ADPCM_MINIMUM_RATE && input_rate <= XBOX_ADPCM_MAXIMUM_INPUT_RATE &&
		output_rate >= XBOX_ADPCM_MINIMUM_RATE && output_rate <= XBOX_ADPCM_MAXIMUM_OUTPUT_RATE;
}

int xbox_adpcm_rate_begin(
	struct xbox_adpcm_rate *rate,
	struct xbox_adpcm_encoder *encoder,
	long input_rate,
	long output_rate)
{
	memset(rate, 0, sizeof(*rate));
	rate->encoder = encoder;
	if (!encoder || !rate_within(input_rate, output_rate))
	{
		rate->encoder = NULL;
		return 0;
	}
	rate->input_rate = (uint32_t)input_rate;
	rate->output_rate = (uint32_t)output_rate;
	rate->mode = input_rate == output_rate ? _xbox_adpcm_rate_same :
		input_rate == 2 * output_rate ? _xbox_adpcm_rate_halved : _xbox_adpcm_rate_linear;
	return 1;
}

/* (linear) the output frames that lie between the last frame and `frame` */
static void rate_interpolate(struct xbox_adpcm_rate *rate, int const *frame)
{
	int channels = rate->encoder->channels;
	int channel;

	if (!rate->have_last)
	{
		for (channel = 0; channel < channels; channel++)
			rate->last[channel] = frame[channel];
		rate->have_last = 1;
		rate->position = 0;
		return;
	}
	while (rate->position < rate->output_rate && !rate->encoder->full)
	{
		int output[XBOX_ADPCM_MAXIMUM_CHANNELS];

		for (channel = 0; channel < channels; channel++)
		{
			output[channel] = rate->last[channel] + (int)((int64_t)(frame[channel] - rate->last[channel]) *
				(int64_t)rate->position / (int64_t)rate->output_rate);
		}
		xbox_adpcm_encoder_frame(rate->encoder, output);
		rate->position += rate->input_rate;
	}
	/* (the output full: the place no longer matters) */
	if (rate->position >= rate->output_rate)
		rate->position -= rate->output_rate;
	for (channel = 0; channel < channels; channel++)
		rate->last[channel] = frame[channel];
}

/* (linear) a frame through the low-pass when the rate goes down: each
frame given out as (1 2 1)/4 of it and its neighbours, one frame late */
static void rate_filter(struct xbox_adpcm_rate *rate, int const *frame)
{
	int channels = rate->encoder->channels;
	int filtered[XBOX_ADPCM_MAXIMUM_CHANNELS];
	int channel;

	if (rate->input_rate <= rate->output_rate)
	{
		rate_interpolate(rate, frame);
		return;
	}
	if (rate->filter_frames == 0)
	{
		for (channel = 0; channel < channels; channel++)
			rate->filter[0][channel] = rate->filter[1][channel] = frame[channel];
		rate->filter_frames = 1;
		return;
	}
	for (channel = 0; channel < channels; channel++)
	{
		filtered[channel] = (rate->filter[0][channel] + 2 * rate->filter[1][channel] + frame[channel]) / 4;
		rate->filter[0][channel] = rate->filter[1][channel];
		rate->filter[1][channel] = frame[channel];
	}
	rate_interpolate(rate, filtered);
}

void xbox_adpcm_rate_frame(
	struct xbox_adpcm_rate *rate,
	int const *frame)
{
	int channels;
	int channel;

	if (!rate->encoder)
		return;
	channels = rate->encoder->channels;
	if (rate->encoder->full)
	{
		rate->encoder->truncated = 1;
		return;
	}
	switch (rate->mode)
	{
	case _xbox_adpcm_rate_same:
		xbox_adpcm_encoder_frame(rate->encoder, frame);
		break;
	case _xbox_adpcm_rate_halved:
		if (!rate->have_pending)
		{
			for (channel = 0; channel < channels; channel++)
				rate->pending[channel] = frame[channel];
			rate->have_pending = 1;
			break;
		}
		for (channel = 0; channel < channels; channel++)
		{
			int middle = rate->pending[channel];

			rate->pending[channel] = (rate->previous[channel] + 2 * middle + frame[channel]) / 4;
			rate->previous[channel] = frame[channel];
		}
		rate->have_pending = 0;
		xbox_adpcm_encoder_frame(rate->encoder, rate->pending);
		break;
	default:
		rate_filter(rate, frame);
		break;
	}
}

void xbox_adpcm_rate_finish(
	struct xbox_adpcm_rate *rate)
{
	int channels;
	int channel;

	if (!rate->encoder || rate->encoder->full)
		return;
	channels = rate->encoder->channels;
	if (rate->mode == _xbox_adpcm_rate_halved && rate->have_pending)
	{
		/* (the last frame of an odd count) */
		xbox_adpcm_encoder_frame(rate->encoder, rate->pending);
		rate->have_pending = 0;
	}
	else if (rate->mode == _xbox_adpcm_rate_linear)
	{
		int last[XBOX_ADPCM_MAXIMUM_CHANNELS];

		/* the low-pass's last frame, then the output frames up to the
		input's end (the last frame held) */
		if (rate->input_rate > rate->output_rate && rate->filter_frames)
		{
			for (channel = 0; channel < channels; channel++)
				last[channel] = (rate->filter[0][channel] + 3 * rate->filter[1][channel]) / 4;
			rate->filter_frames = 0;
			rate_interpolate(rate, last);
		}
		if (rate->have_last)
		{
			for (channel = 0; channel < channels; channel++)
				last[channel] = rate->last[channel];
			rate_interpolate(rate, last);
			rate->have_last = 0;
		}
	}
}

uint32_t xbox_adpcm_rate_frames(
	uint64_t input_frames,
	long input_rate,
	long output_rate)
{
	uint64_t frames;

	if (!rate_within(input_rate, output_rate) || input_frames > UINT32_MAX)
		return 0;
	if (input_rate == output_rate)
		return (uint32_t)input_frames;
	if (input_rate == 2 * output_rate)
		return (uint32_t)((input_frames + 1) / 2);
	/* (the output frames k with k * input_rate below input_frames * output_rate) */
	frames = (input_frames * (uint64_t)output_rate + (uint64_t)input_rate - 1) / (uint64_t)input_rate;
	return frames > UINT32_MAX ? 0 : (uint32_t)frames;
}

/* ---------- decoding */

void xbox_adpcm_block_decode(
	uint8_t const *block,
	int channels,
	short frames[XBOX_ADPCM_BLOCK_SAMPLES * XBOX_ADPCM_MAXIMUM_CHANNELS])
{
	int channel;

	if (channels < 1 || channels > XBOX_ADPCM_MAXIMUM_CHANNELS)
		return;
	for (channel = 0; channel < channels; channel++)
	{
		uint8_t const *header = block + channel * 4;
		int predictor = (short)(header[0] | (header[1] << 8));
		int index = header[2] > 88 ? 88 : header[2];
		int group;

		frames[channel] = (short)predictor;
		for (group = 0; group < 8; group++)
		{
			uint8_t const *nibbles = block + 4 * channels + (group * channels + channel) * 4;
			int byte;

			for (byte = 0; byte < 4; byte++)
			{
				int sample = group * 8 + byte * 2 + 1;
				int nibble;

				for (nibble = 0; nibble < 2 && sample + nibble < XBOX_ADPCM_BLOCK_SAMPLES; nibble++)
				{
					int code = nibble ? nibbles[byte] >> 4 : nibbles[byte] & 0xf;
					int step = ima_step_table[index];
					int difference = step >> 3;
					int value;

					if (code & 1)
						difference += step >> 2;
					if (code & 2)
						difference += step >> 1;
					if (code & 4)
						difference += step;
					if (code & 8)
						difference = -difference;
					value = predictor + difference;
					if (value > 32767)
						value = 32767;
					if (value < -32768)
						value = -32768;
					predictor = value;
					index += ima_index_table[code];
					if (index < 0)
						index = 0;
					if (index > 88)
						index = 88;
					frames[(sample + nibble) * channels + channel] = (short)value;
				}
			}
		}
	}
}
