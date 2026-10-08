/*
DSOUND_SDL.C

Xbox DirectSound for the Linux build: a software mixer on an SDL3 audio
stream.

The game plays everything through DirectSound streams: 16-bit stereo PCM
(music and other uncompressed sounds) and Xbox ADPCM, mono or stereo, at 22
or 44 kHz. A packet is decoded to 16-bit PCM by the mixer as it plays it
(ADPCM a 64-sample block at a time, as far as each mix reaches), before it
completes: the sound cache may reuse its memory once it has. The mixer runs
on SDL's audio thread; for every voice it resamples to the output rate
(which is how SetFrequency changes pitch: linear interpolation, in fixed
point positions and with NEON where there is one) and applies:
	- the stream volume (millibels),
	- the front left and right mix bin volumes of 2D voices,
	- for 3D voices, DirectSound's inverse distance rolloff between the
	  minimum and maximum distance, an equal power pan from the source's
	  direction in listener space, and the low frequency part of the I3DL2
	  direct path, obstruction and occlusion levels.
Doppler, the high frequency filters, cones and I3DL2 reverb are not
modelled.

Packets the mixer has finished are completed from DirectSoundDoWork, which
the game calls every frame, and from Flush, never from the audio thread:
the game's completion callback is not meant to run concurrently with it.

Without an audio device, a clock thread runs the same mixer into a scratch
buffer, so streams still drain at their real rate.

audio.volume sets the master volume (default 1.0); audio.enabled = false
skips opening a device (port_config.c).
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef HALO_VITA
#include "vita_compat.h"
#endif
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#define OUTPUT_RATE 48000
#define OUTPUT_CHANNELS 2
#define MAXIMUM_STREAM_PACKETS 64
#define MIX_CHUNK_FRAMES 1024

#define XBOX_ADPCM_BLOCK_BYTES 36
#define XBOX_ADPCM_BLOCK_SAMPLES 64

/* ---------- voices */

struct voice_packet
{
	XMEDIAPACKET packet;
	short *samples;           /* interleaved, source channel count; NULL until decoded */
	unsigned long frames;
	BOOL finished;            /* played out by the mixer, not yet completed */
	BOOL decoded;             /* samples made from packet.pvBuffer (mix_voice_packet) */
	unsigned long decoded_frames; /* the frames decoded so far (ADPCM: as the mixer reaches them) */
};

struct sdl_stream
{
	/* must be first: in C an IDirectSoundStream is just { lpVtbl } */
	IDirectSoundStream object;
	struct sdl_stream *next;
	ULONG reference_count;
	LPFNXMEDIAOBJECTCALLBACK callback;
	LPVOID context;

	/* format */
	BOOL adpcm;
	unsigned long channels;
	DWORD sample_rate;
	DWORD frequency;

	BOOL paused;

	/* 2D gains */
	float volume;             /* SetVolume */
	float mix_left, mix_right;
	float headroom;

	/* 3D */
	BOOL has_3d;
	DWORD mode;
	float position[3];
	float minimum_distance, maximum_distance;
	float i3dl2_gain;

	struct voice_packet packets[MAXIMUM_STREAM_PACKETS];
	unsigned long packet_head;
	unsigned long packet_count;
	/* position inside the head packet, in source frames */
	double cursor;
	/* the last frame of the previous packet, for interpolating across packets */
	float previous[2];
	/* gains the mixer is ramping from, to avoid clicks */
	float current_left, current_right;
	BOOL gains_valid;
	/* the mix pass that last mixed this voice (mix) */
	unsigned long mix_pass;
};

static pthread_mutex_t mixer_lock = PTHREAD_MUTEX_INITIALIZER;
/* the voices' and the listener's parameters (volumes, positions, pitch,
pause): the game sets them under this lock alone, held for a few stores,
and the mixer reads a voice's under it at the start of its mix, so the
dozens of parameter calls a frame never wait for a mix */
static pthread_mutex_t parameter_lock = PTHREAD_MUTEX_INITIALIZER;
static struct sdl_stream *streams;

/* the listener, in DirectSound's left-handed +y up space */
static struct
{
	float position[3];
	float front[3];
	float top[3];
	float rolloff_factor;
	/* meters a unit (SetDistanceFactor): the game sets 3.048, a world unit
	being 10 feet. Only Doppler, which is not modelled, would use it */
	float distance_factor;
} listener = { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, 1.0f, 1.0f };

static float master_volume = 1.0f;

static float gain_from_millibels(LONG millibels)
{
	if (millibels <= DSBVOLUME_MIN)
		return 0.0f;
	return powf(10.0f, (float)millibels / 2000.0f);
}

/* ---------- decoding */

static const int ima_index_table[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
};

static const int ima_step_table[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767,
};

/* IMA ADPCM expansion from tables: the difference a nibble makes at each
step index and the step index it leads to, worked out once at start-up
with the same integer arithmetic a nibble-by-nibble expansion does */
static int ima_difference_table[89][16];
static unsigned char ima_next_index_table[89][16];
static int ima_tables_built;

static void ima_build_tables(void)
{
	int index, nibble;

	for (index = 0; index < 89; index++)
	{
		for (nibble = 0; nibble < 16; nibble++)
		{
			int step = ima_step_table[index];
			int difference = step >> 3;
			int next = index + ima_index_table[nibble];

			if (nibble & 1) difference += step >> 2;
			if (nibble & 2) difference += step >> 1;
			if (nibble & 4) difference += step;
			if (nibble & 8) difference = -difference;
			if (next < 0) next = 0;
			if (next > 88) next = 88;
			ima_difference_table[index][nibble] = difference;
			ima_next_index_table[index][nibble] = (unsigned char)next;
		}
	}
	ima_tables_built = 1;
}

static inline int ima_expand_fast(int nibble, int *predictor, int *index)
{
	int value = *predictor + ima_difference_table[*index][nibble];

	if (value > 32767) value = 32767;
	if (value < -32768) value = -32768;
	*predictor = value;
	*index = ima_next_index_table[*index][nibble];
	return value;
}

/* Xbox ADPCM: per block, a 4-byte header per channel (the first sample and
the step index), then 4-byte groups of eight nibbles, low nibble first,
alternating between channels; 64 samples per channel: the header's sample,
then 63 nibbles. The 64th nibble is padding (0 in every block of the game's
sounds): the Xbox does not play it.

Before 1.0.3 the port dropped the header's sample and played all 64
nibbles, the padding's included: every block's first sample was missing and
a made-up one ended it, a click every 64 samples (689 times a second for a
44 kHz sound) heard as a slight distortion of all the sound and music
(issue #6). HALO_ADPCM_LEGACY=1 decodes as before, for comparison. */
static int adpcm_legacy = -1;

/* blocks [first, end) of an Xbox ADPCM packet into samples (the whole
packet's interleaved 16-bit frames) */
static void decode_adpcm_blocks(const unsigned char *source, unsigned long channels, short *samples, unsigned long first,
	unsigned long end)
{
	unsigned long block_bytes = XBOX_ADPCM_BLOCK_BYTES * channels;
	unsigned long block, channel;

	if (!ima_tables_built)
		ima_build_tables();
	if (adpcm_legacy < 0)
		adpcm_legacy = getenv("HALO_ADPCM_LEGACY") && atoi(getenv("HALO_ADPCM_LEGACY"));
	for (block = first; block < end; block++)
	{
		const unsigned char *data = source + block * block_bytes;
		short *output = samples + block * XBOX_ADPCM_BLOCK_SAMPLES * channels;

		for (channel = 0; channel < channels; channel++)
		{
			const unsigned char *header = data + channel * 4;
			int predictor = (short)(header[0] | (header[1] << 8));
			int index = header[2] > 88 ? 88 : header[2];
			unsigned long group, byte;
			/* the sample the next nibble decodes to (the header's is the
			first) */
			unsigned long first_sample = adpcm_legacy ? 0 : 1;

			if (!adpcm_legacy)
				output[channel] = (short)predictor;
			for (group = 0; group < 8; group++)
			{
				const unsigned char *nibbles = data + 4 * channels + (group * channels + channel) * 4;

				for (byte = 0; byte < 4; byte++)
				{
					unsigned long sample = group * 8 + byte * 2 + first_sample;

					output[sample * channels + channel] = (short)ima_expand_fast(nibbles[byte] & 0xf, &predictor, &index);
					/* (the last nibble, padding, is not played) */
					if (sample + 1 < XBOX_ADPCM_BLOCK_SAMPLES)
						output[(sample + 1) * channels + channel] = (short)ima_expand_fast(nibbles[byte] >> 4, &predictor, &index);
				}
			}
		}
	}
}

static short *decode_pcm(const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frame_count)
{
	unsigned long frames = size / (2 * channels);
	short *samples = malloc((frames ? frames : 1) * channels * sizeof(short));

	if (samples)
		memcpy(samples, source, frames * channels * sizeof(short));
	*frame_count = samples ? frames : 0;
	return samples;
}

/* ---------- 3D */

static float dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void spatialize(const struct sdl_stream *stream, float *left, float *right)
{
	float offset[3], right_axis[3], distance, attenuation, pan, side, ahead;
	int axis;

	if (stream->mode == DS3DMODE_HEADRELATIVE)
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = stream->position[axis];
		side = offset[0];
		ahead = offset[2];
	}
	else
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = stream->position[axis] - listener.position[axis];
		/* left-handed: right = top x front */
		right_axis[0] = listener.top[1] * listener.front[2] - listener.top[2] * listener.front[1];
		right_axis[1] = listener.top[2] * listener.front[0] - listener.top[0] * listener.front[2];
		right_axis[2] = listener.top[0] * listener.front[1] - listener.top[1] * listener.front[0];
		side = dot3(offset, right_axis);
		ahead = dot3(offset, listener.front);
	}
	/* in the game's units, those of the minimum and maximum distances: the
	distance factor turns units into meters for Doppler, and scaling by it
	here put every 3D sound 3 times as far away, up to 10 dB quieter */
	distance = sqrtf(dot3(offset, offset));

	/* DirectSound's inverse distance law, held beyond the maximum distance */
	attenuation = 1.0f;
	if (distance > stream->minimum_distance && stream->minimum_distance > 0.0f)
	{
		float clamped = distance < stream->maximum_distance ? distance : stream->maximum_distance;

		attenuation = stream->minimum_distance /
			(stream->minimum_distance + listener.rolloff_factor * (clamped - stream->minimum_distance));
	}

	/* equal power pan; close sources and sources straight ahead or behind
	stay centred, and neither ear drops below a quarter */
	{
		float horizontal = sqrtf(side * side + ahead * ahead);
		float angle;

		pan = horizontal > 1.0e-4f ? side / horizontal : 0.0f;
		if (distance < stream->minimum_distance && stream->minimum_distance > 0.0f)
			pan *= distance / stream->minimum_distance;
		pan *= 0.75f;
		angle = (pan + 1.0f) * 0.25f * 3.14159265f;
		*left = cosf(angle) * 1.41421356f * 0.70710678f;
		*right = sinf(angle) * 1.41421356f * 0.70710678f;
	}
	*left *= attenuation * stream->i3dl2_gain;
	*right *= attenuation * stream->i3dl2_gain;
}

static void voice_gains(const struct sdl_stream *stream, float *left, float *right)
{
	if (stream->has_3d && stream->mode != DS3DMODE_DISABLE)
	{
		spatialize(stream, left, right);
	}
	else
	{
		*left = stream->mix_left;
		*right = stream->mix_right;
	}
	*left *= stream->volume * master_volume;
	*right *= stream->volume * master_volume;
}

/* ---------- mixing */

static float packet_sample(const struct voice_packet *packet, unsigned long frame, unsigned long channel,
	unsigned long channels)
{
	return packet->samples[frame * channels + channel] * (1.0f / 32768.0f);
}

/* the first packet of a stream that still has frames to play at the
cursor, marking the ones the cursor has passed finished; NULL once the
stream has run dry */
static void voice_packet_decode(const struct sdl_stream *stream, struct voice_packet *packet)
{
	unsigned long frames = 0;

	if (stream->adpcm)
	{
		/* (the samples' memory now, the blocks as the mixer reaches them:
		voice_packet_decoded) */
		unsigned long blocks = packet->packet.dwMaxSize / (XBOX_ADPCM_BLOCK_BYTES * stream->channels);

		frames = blocks * XBOX_ADPCM_BLOCK_SAMPLES;
		packet->samples = malloc((blocks ? blocks : 1) * XBOX_ADPCM_BLOCK_SAMPLES * stream->channels * sizeof(short));
		packet->decoded_frames = 0;
	}
	else
	{
		packet->samples = decode_pcm(packet->packet.pvBuffer, packet->packet.dwMaxSize, stream->channels, &frames);
		packet->decoded_frames = frames;
	}
	packet->frames = packet->samples ? frames : 0;
	packet->decoded = TRUE;
}

/* an ADPCM packet decoded through frame end at least (whole blocks, each
block being independent of the others): a packet is decoded as it plays,
spread over the mixes rather than all at the first (a long sound's whole
packet was a spike in one mix, and a voice stopped early had decoded what
it never played). The samples are the same */
static void voice_packet_decoded(const struct sdl_stream *stream, struct voice_packet *packet, unsigned long end)
{
	unsigned long first, last;

	if (end > packet->frames)
		end = packet->frames;
	if (end <= packet->decoded_frames)
		return;
	first = packet->decoded_frames / XBOX_ADPCM_BLOCK_SAMPLES;
	last = (end + XBOX_ADPCM_BLOCK_SAMPLES - 1) / XBOX_ADPCM_BLOCK_SAMPLES;
	decode_adpcm_blocks(packet->packet.pvBuffer, stream->channels, packet->samples, first, last);
	packet->decoded_frames = last * XBOX_ADPCM_BLOCK_SAMPLES;
}

static struct voice_packet *mix_voice_packet(struct sdl_stream *stream)
{
	for (;;)
	{
		struct voice_packet *packet = NULL;
		unsigned long position;

		for (position = 0; position < stream->packet_count; position++)
		{
			struct voice_packet *candidate = &stream->packets[(stream->packet_head + position) % MAXIMUM_STREAM_PACKETS];

			if (!candidate->finished)
			{
				packet = candidate;
				break;
			}
		}
		if (!packet)
			return NULL;
		if (!packet->decoded)
			voice_packet_decode(stream, packet);
		if (stream->cursor < (double)packet->frames)
			return packet;
		stream->cursor -= (double)packet->frames;
		if (packet->frames)
		{
			unsigned long last = packet->frames - 1;

			voice_packet_decoded(stream, packet, packet->frames);
			stream->previous[0] = packet_sample(packet, last, 0, stream->channels);
			stream->previous[1] = packet_sample(packet, last, stream->channels - 1, stream->channels);
		}
		packet->finished = TRUE;
	}
}

/* a voice's parameters for one mix, read under parameter_lock (mix_voice) */
struct voice_mix_parameters
{
	BOOL playing;
	double step;
	float target_left, target_right;
};

static void voice_mix_parameters(struct sdl_stream *stream, struct voice_mix_parameters *parameters)
{
	parameters->playing = FALSE;
	if (!stream->packet_count || !stream->sample_rate)
		return;
	pthread_mutex_lock(&parameter_lock);
	if (!stream->paused)
	{
		parameters->playing = TRUE;
		parameters->step = (double)(stream->frequency ? stream->frequency : stream->sample_rate) / OUTPUT_RATE;
		voice_gains(stream, &parameters->target_left, &parameters->target_right);
	}
	pthread_mutex_unlock(&parameter_lock);
}

/* the mix buffers are float arrays: telling the compiler so lets the Vita
build (-fmax-type-align=1 takes nothing as aligned) load and store them
straight from floating point registers instead of through integer ones */
typedef float aligned_float __attribute__((aligned(4)));

/* the per-frame positions of a run of frames: the cursor before each
frame, its whole part and its fraction (mix_voice_reference; one mixer runs
at a time) */
#define RUN_FRAMES 2048
static double run_cursors[RUN_FRAMES + 1];
static unsigned long run_indices[RUN_FRAMES];
static float run_fractions[RUN_FRAMES];

/* the mixer as it was before 1.0.5 (double cursor per frame, scalar),
kept as the reference HALO_AUDIO_VERIFY compares mix_voice with */
static void mix_voice_reference(struct sdl_stream *stream, const struct voice_mix_parameters *parameters,
	float *mix_output, unsigned long frames)
{
	aligned_float *output = (aligned_float *)mix_output;
	const aligned_float *fractions = (const aligned_float *)run_fractions;
	double step = parameters->step;
	float target_left = parameters->target_left, target_right = parameters->target_right;
	float left, right, ramp_left, ramp_right;
	unsigned long frame;

	if (!parameters->playing)
		return;
	if (!stream->gains_valid)
	{
		stream->current_left = target_left;
		stream->current_right = target_right;
		stream->gains_valid = TRUE;
	}
	left = stream->current_left;
	right = stream->current_right;
	ramp_left = (target_left - left) / (float)frames;
	ramp_right = (target_right - right) / (float)frames;

	frame = 0;
	while (frame < frames)
	{
		struct voice_packet *packet = mix_voice_packet(stream);
		const short *samples;
		unsigned long last, packet_frames, run, k;

		if (!packet)
			break;
		samples = packet->samples;
		packet_frames = packet->frames;
		last = packet_frames - 1;
		run = frames - frame;
		if (run > RUN_FRAMES)
			run = RUN_FRAMES;
		{
			double cursor = stream->cursor;

			for (k = 0; k < run; k++)
			{
				unsigned long index = (unsigned long)cursor;

				run_cursors[k] = cursor;
				run_indices[k] = index;
				run_fractions[k] = (float)(cursor - (double)index);
				cursor += step;
			}
			run_cursors[run] = cursor;
			/* (the frames this run can reach, and the next) */
			voice_packet_decoded(stream, packet, (unsigned long)cursor + 2);
		}
		if (stream->channels == 1)
		{
			for (k = 0; k < run; k++)
			{
				unsigned long index = run_indices[k];
				float a0, b0, sample_left;

				if (index >= packet_frames)
					break;
				a0 = samples[index] * (1.0f / 32768.0f);
				b0 = index < last ? samples[index + 1] * (1.0f / 32768.0f) : a0;
				sample_left = a0 + (b0 - a0) * fractions[k];
				output[(frame + k) * 2] += sample_left * left;
				output[(frame + k) * 2 + 1] += sample_left * right;
				left += ramp_left;
				right += ramp_right;
			}
		}
		else
		{
			for (k = 0; k < run; k++)
			{
				unsigned long index = run_indices[k];
				float a0, a1, b0, b1, sample_left, sample_right;

				if (index >= packet_frames)
					break;
				a0 = samples[index * 2] * (1.0f / 32768.0f);
				a1 = samples[index * 2 + 1] * (1.0f / 32768.0f);
				if (index < last)
				{
					b0 = samples[index * 2 + 2] * (1.0f / 32768.0f);
					b1 = samples[index * 2 + 3] * (1.0f / 32768.0f);
				}
				else
				{
					b0 = a0;
					b1 = a1;
				}
				sample_left = a0 + (b0 - a0) * fractions[k];
				sample_right = a1 + (b1 - a1) * fractions[k];
				output[(frame + k) * 2] += sample_left * left;
				output[(frame + k) * 2 + 1] += sample_right * right;
				left += ramp_left;
				right += ramp_right;
			}
		}
		stream->cursor = run_cursors[k];
		frame += k;
	}
	stream->current_left = target_left;
	stream->current_right = target_right;
}

/* one run of a voice inside one packet: up to run frames of output from
position (the packet frame, in 32.32 fixed point) on, stepping step a frame,
until a frame's sample index is past the packet; returns the frames mixed.
Each frame is the sample at its index linearly interpolated towards the
next by its fraction (the packet's last sample is held), times the gains,
which ramp by ramp_* a frame, as mix_voice_reference does.

Positions are whole numbers: the index is the position's high word and the
fraction its low word, converted to a float as the reference rounds its
double's fraction, so no floating point value is ever moved to an integer
register (on the Vita's Cortex-A9 each such move stalls the pipeline, and
the reference made one a frame). With NEON, four frames at a time: the
samples are gathered by lane loads (a mono frame's two, a stereo frame's
four in one load) and interpolated, scaled and added in vectors; the
gains of the four lanes step by four ramps at once, which may round them
differently from the reference's one at a time in their last bit. */
static unsigned long mix_run(const short *samples, unsigned long packet_frames, BOOL mono, uint64_t position,
	uint64_t step, aligned_float *output, unsigned long run, float *left_gain, float *right_gain, float ramp_left,
	float ramp_right)
{
	unsigned long k = 0, last = packet_frames - 1;
	float left = *left_gain, right = *right_gain;

#ifdef __ARM_NEON
	if (run >= 4 && (position + 3 * step) >> 32 < last)
	{
		uint32_t fraction = (uint32_t)position, fraction_step = (uint32_t)step;
		uint32_t fraction_lanes[4] = { fraction, fraction + fraction_step, fraction + 2 * fraction_step,
			fraction + 3 * fraction_step };
		float left_lanes[4], right_lanes[4];
		uint32x4_t fractions = vld1q_u32(fraction_lanes);
		uint32x4_t fraction_step4 = vdupq_n_u32(4 * fraction_step);
		float32x4_t lefts, rights, left_step4 = vdupq_n_f32(4.0f * ramp_left), right_step4 = vdupq_n_f32(4.0f * ramp_right);
		uint64_t step2 = 2 * step, step4 = 4 * step;
		int lane;

		for (lane = 0; lane < 4; lane++)
		{
			left_lanes[lane] = left;
			right_lanes[lane] = right;
			left += ramp_left;
			right += ramp_right;
		}
		lefts = vld1q_f32(left_lanes);
		rights = vld1q_f32(right_lanes);
		for (; k + 4 <= run; k += 4)
		{
			uint64_t position1 = position + step, position3 = position1 + step2;
			unsigned long index0 = (unsigned long)(position >> 32), index1 = (unsigned long)(position1 >> 32);
			unsigned long index2 = (unsigned long)((position + step2) >> 32), index3 = (unsigned long)(position3 >> 32);
			float32x4_t fraction_values = vcvtq_n_f32_u32(fractions, 32);
			float32x4x2_t mixed = vld2q_f32(output + k * 2);

			if (index3 >= last)
				break;
			if (mono)
			{
				int16x4_t a = vdup_n_s16(0), b = vdup_n_s16(0);
				float32x4_t a_values, b_values, values;

				a = vld1_lane_s16(samples + index0, a, 0);
				b = vld1_lane_s16(samples + index0 + 1, b, 0);
				a = vld1_lane_s16(samples + index1, a, 1);
				b = vld1_lane_s16(samples + index1 + 1, b, 1);
				a = vld1_lane_s16(samples + index2, a, 2);
				b = vld1_lane_s16(samples + index2 + 1, b, 2);
				a = vld1_lane_s16(samples + index3, a, 3);
				b = vld1_lane_s16(samples + index3 + 1, b, 3);
				a_values = vcvtq_n_f32_s32(vmovl_s16(a), 15);
				b_values = vcvtq_n_f32_s32(vmovl_s16(b), 15);
				values = vaddq_f32(a_values, vmulq_f32(vsubq_f32(b_values, a_values), fraction_values));
				mixed.val[0] = vaddq_f32(mixed.val[0], vmulq_f32(values, lefts));
				mixed.val[1] = vaddq_f32(mixed.val[1], vmulq_f32(values, rights));
			}
			else
			{
				/* (each frame's left, right, next left, next right) */
				int16x8_t frames01 = vcombine_s16(vld1_s16(samples + index0 * 2), vld1_s16(samples + index1 * 2));
				int16x8_t frames23 = vcombine_s16(vld1_s16(samples + index2 * 2), vld1_s16(samples + index3 * 2));
				int16x8x2_t sides = vuzpq_s16(frames01, frames23);
				int16x4x2_t lefts_ab = vuzp_s16(vget_low_s16(sides.val[0]), vget_high_s16(sides.val[0]));
				int16x4x2_t rights_ab = vuzp_s16(vget_low_s16(sides.val[1]), vget_high_s16(sides.val[1]));
				float32x4_t a_left = vcvtq_n_f32_s32(vmovl_s16(lefts_ab.val[0]), 15);
				float32x4_t b_left = vcvtq_n_f32_s32(vmovl_s16(lefts_ab.val[1]), 15);
				float32x4_t a_right = vcvtq_n_f32_s32(vmovl_s16(rights_ab.val[0]), 15);
				float32x4_t b_right = vcvtq_n_f32_s32(vmovl_s16(rights_ab.val[1]), 15);
				float32x4_t left_values = vaddq_f32(a_left, vmulq_f32(vsubq_f32(b_left, a_left), fraction_values));
				float32x4_t right_values = vaddq_f32(a_right, vmulq_f32(vsubq_f32(b_right, a_right), fraction_values));

				mixed.val[0] = vaddq_f32(mixed.val[0], vmulq_f32(left_values, lefts));
				mixed.val[1] = vaddq_f32(mixed.val[1], vmulq_f32(right_values, rights));
			}
			vst2q_f32(output + k * 2, mixed);
			fractions = vaddq_u32(fractions, fraction_step4);
			lefts = vaddq_f32(lefts, left_step4);
			rights = vaddq_f32(rights, right_step4);
			position += step4;
		}
		left = vgetq_lane_f32(lefts, 0);
		right = vgetq_lane_f32(rights, 0);
	}
#endif
	for (; k < run; k++)
	{
		unsigned long index = (unsigned long)(position >> 32);
		float fraction = (float)(uint32_t)position * (1.0f / 4294967296.0f);

		if (index >= packet_frames)
			break;
		if (mono)
		{
			float a0 = samples[index] * (1.0f / 32768.0f);
			float b0 = index < last ? samples[index + 1] * (1.0f / 32768.0f) : a0;
			float sample_left = a0 + (b0 - a0) * fraction;

			output[k * 2] += sample_left * left;
			output[k * 2 + 1] += sample_left * right;
		}
		else
		{
			float a0 = samples[index * 2] * (1.0f / 32768.0f);
			float a1 = samples[index * 2 + 1] * (1.0f / 32768.0f);
			float b0 = a0, b1 = a1;
			float sample_left, sample_right;

			if (index < last)
			{
				b0 = samples[index * 2 + 2] * (1.0f / 32768.0f);
				b1 = samples[index * 2 + 3] * (1.0f / 32768.0f);
			}
			sample_left = a0 + (b0 - a0) * fraction;
			sample_right = a1 + (b1 - a1) * fraction;
			output[k * 2] += sample_left * left;
			output[k * 2 + 1] += sample_right * right;
		}
		left += ramp_left;
		right += ramp_right;
		position += step;
	}
	*left_gain = left;
	*right_gain = right;
	return k;
}

/* mixes one voice into output (frames of stereo float), a run of frames
(mix_run) for each packet it plays from. The cursor stays a double between
runs: each run starts from it in fixed point (32 fractional bits) and it
moves on by the run's length times the step, so the fixed point steps'
rounding never adds up past one run. */
static void mix_voice(struct sdl_stream *stream, const struct voice_mix_parameters *parameters, float *mix_output,
	unsigned long frames)
{
	aligned_float *output = (aligned_float *)mix_output;
	double step = parameters->step;
	uint64_t fixed_step;
	float left, right, ramp_left, ramp_right;
	unsigned long frame;

	if (!parameters->playing)
		return;
	if (!stream->gains_valid)
	{
		stream->current_left = parameters->target_left;
		stream->current_right = parameters->target_right;
		stream->gains_valid = TRUE;
	}
	left = stream->current_left;
	right = stream->current_right;
	ramp_left = (parameters->target_left - left) / (float)frames;
	ramp_right = (parameters->target_right - right) / (float)frames;
	fixed_step = (uint64_t)(step * 4294967296.0 + 0.5);

	frame = 0;
	while (frame < frames)
	{
		struct voice_packet *packet = mix_voice_packet(stream);
		double cursor = stream->cursor;
		uint32_t whole;
		uint64_t position;
		unsigned long mixed;

		if (!packet)
			break;
		whole = (uint32_t)cursor;
		position = ((uint64_t)whole << 32) | (uint32_t)((cursor - (double)whole) * 4294967296.0);
		/* (the frames this run can reach, and the next) */
		voice_packet_decoded(stream, packet, (unsigned long)((position + (uint64_t)(frames - frame) * fixed_step) >> 32) + 2);
		mixed = mix_run(packet->samples, packet->frames, stream->channels == 1, position, fixed_step, output + frame * 2,
			frames - frame, &left, &right, ramp_left, ramp_right);
		stream->cursor = cursor + (double)mixed * step;
		frame += mixed;
	}
	stream->current_left = parameters->target_left;
	stream->current_right = parameters->target_right;
}

/* (HALO_RENDER_PROFILE) the mixer's time and the game's waits for it,
reported by DirectSoundDoWork every 300 calls */
unsigned long long vita_host_time_us(void) __attribute__((weak));
static volatile unsigned long long statistics_mix_us, statistics_wait_us;
static volatile unsigned long statistics_mixes, statistics_voices, statistics_waits;
/* the device's calls for audio: the longest time between two, and how many
came over 25 ms apart (SDL asks for 512 frames, 10.7 ms: a gap past two of
those has let the device's queue run dry, heard as a crackle) */
static volatile unsigned long long statistics_callback_gap_us;
static volatile unsigned long statistics_callbacks_late;

static unsigned long long statistics_now(void)
{
	return vita_host_time_us ? vita_host_time_us() : 0;
}

/* game threads waiting for mixer_lock: the mixer hands the lock over
between two voices when it sees one (mix) */
static volatile int game_lock_wanted;

/* mixer_lock taken by the game's threads: the time spent waiting for the
mixer is counted (a lock that is free costs no clock read) */
static void game_lock(void)
{
	unsigned long long started;

	if (!pthread_mutex_trylock(&mixer_lock))
		return;
	started = statistics_now();
	__atomic_add_fetch(&game_lock_wanted, 1, __ATOMIC_SEQ_CST);
	pthread_mutex_lock(&mixer_lock);
	__atomic_sub_fetch(&game_lock_wanted, 1, __ATOMIC_SEQ_CST);
	statistics_wait_us += statistics_now() - started;
	statistics_waits++;
}


/* (debug) HALO_AUDIO_VERIFY=1: every voice is mixed twice from the same
state and parameters, by mix_voice_reference and by mix_voice; the largest
difference between the two and any difference in where they left the voice
are reported with the mixer's statistics, and HALO_AUDIO_DUMP_REF=<file>
dumps the reference's whole mix as HALO_AUDIO_DUMP does mix_voice's. The
output is mix_voice's, which also moves the voice on */
#define VERIFY_FRAMES 2048
static int audio_verify = -1;
static float verify_mix[VERIFY_FRAMES * OUTPUT_CHANNELS];
static float verify_reference[VERIFY_FRAMES * OUTPUT_CHANNELS], verify_voice[VERIFY_FRAMES * OUTPUT_CHANNELS];
static volatile double verify_largest_difference, verify_largest_cursor_difference;
static volatile unsigned long verify_voices, verify_state_mismatches, verify_samples_different;

static void mix_one_voice(struct sdl_stream *stream, float *output, unsigned long frames)
{
	struct voice_mix_parameters parameters;

	voice_mix_parameters(stream, &parameters);
	if (audio_verify <= 0 || frames > VERIFY_FRAMES)
	{
		mix_voice(stream, &parameters, output, frames);
		return;
	}
	{
		/* (what mix_voice changes in a voice, put back between the two:
		the parameters are the game's, set under their own lock) */
		double cursor = stream->cursor, reference_cursor;
		float previous[2] = { stream->previous[0], stream->previous[1] };
		float current_left = stream->current_left, current_right = stream->current_right;
		BOOL gains_valid = stream->gains_valid;
		BOOL finished[MAXIMUM_STREAM_PACKETS], reference_finished[MAXIMUM_STREAM_PACKETS];
		unsigned long index;

		for (index = 0; index < MAXIMUM_STREAM_PACKETS; index++)
			finished[index] = stream->packets[index].finished;
		memset(verify_reference, 0, frames * OUTPUT_CHANNELS * sizeof(float));
		memset(verify_voice, 0, frames * OUTPUT_CHANNELS * sizeof(float));
		mix_voice_reference(stream, &parameters, verify_reference, frames);
		reference_cursor = stream->cursor;
		for (index = 0; index < MAXIMUM_STREAM_PACKETS; index++)
		{
			reference_finished[index] = stream->packets[index].finished;
			stream->packets[index].finished = finished[index];
		}
		stream->cursor = cursor;
		stream->previous[0] = previous[0];
		stream->previous[1] = previous[1];
		stream->current_left = current_left;
		stream->current_right = current_right;
		stream->gains_valid = gains_valid;
		mix_voice(stream, &parameters, verify_voice, frames);
		for (index = 0; index < MAXIMUM_STREAM_PACKETS; index++)
			if (reference_finished[index] != stream->packets[index].finished)
				break;
		if (index < MAXIMUM_STREAM_PACKETS)
			verify_state_mismatches++;
		if (fabs(reference_cursor - stream->cursor) > verify_largest_cursor_difference)
			verify_largest_cursor_difference = fabs(reference_cursor - stream->cursor);
		for (index = 0; index < frames * OUTPUT_CHANNELS; index++)
		{
			double difference = fabs((double)verify_reference[index] - (double)verify_voice[index]);

			if (difference > verify_largest_difference)
				verify_largest_difference = difference;
			verify_samples_different += difference != 0.0;
			output[index] += verify_voice[index];
			verify_mix[index] += verify_reference[index];
		}
		verify_voices++;
	}
}

/* soft limit rather than wrap or hard clip when many voices pile up */
static void limit(float *output, unsigned long samples)
{
	unsigned long sample;

	for (sample = 0; sample < samples; sample++)
	{
		float value = output[sample];

		if (value > 0.8f || value < -0.8f)
		{
			float sign = value < 0.0f ? -1.0f : 1.0f;
			float excess = fabsf(value) - 0.8f;

			output[sample] = sign * (0.8f + 0.2f * tanhf(excess / 0.2f));
		}
	}
}

/* (debug) HALO_AUDIO_DUMP and HALO_AUDIO_DUMP_REF: a file for a mix, opened
at the first */
static void dump_mix(const char *setting, FILE **dump, int *checked, const float *output, unsigned long frames)
{
	if (!*checked)
	{
		const char *path = getenv(setting);

		*checked = 1;
		if (path && *path)
			*dump = fopen(path, "wb");
	}
	if (*dump)
		fwrite(output, sizeof(float) * OUTPUT_CHANNELS, frames, *dump);
}

/* stream_release and IDirectSound_CreateSoundStream change the stream list
while the mixer may be between two voices: a pass that sees the count move
starts over from the head and skips the voices it has mixed already */
static unsigned long stream_list_generation;
static unsigned long mix_pass;

static void mix(float *output, unsigned long frames)
{
	struct sdl_stream *stream;
	unsigned long generation, voices = 0;
	unsigned long long started = statistics_now();

	memset(output, 0, frames * OUTPUT_CHANNELS * sizeof(float));
	if (audio_verify < 0)
		audio_verify = getenv("HALO_AUDIO_VERIFY") && atoi(getenv("HALO_AUDIO_VERIFY"));
	if (audio_verify > 0 && frames <= VERIFY_FRAMES)
		memset(verify_mix, 0, frames * OUTPUT_CHANNELS * sizeof(float));
	/* a game thread that wants the lock (to queue or complete packets)
	gets it between two voices, so it waits for one voice's mix at most
	rather than all of them: the mixer runs on its own thread at a higher
	priority than the game's, and with many voices playing (heavy combat
	on the Vita) a whole mix takes milliseconds that the sound update on
	the tick thread spent waiting. Letting go and taking the lock again
	at once is not enough (the mixer takes it back before the waiter
	wakes): the mixer waits, briefly, until the waiter has it */
	pthread_mutex_lock(&mixer_lock);
	mix_pass++;
	generation = stream_list_generation;
	stream = streams;
	while (stream)
	{
		struct sdl_stream *next;

		if (stream->mix_pass != mix_pass)
		{
			stream->mix_pass = mix_pass;
			if (stream->packet_count && !stream->paused)
				voices++;
			mix_one_voice(stream, output, frames);
			next = stream->next;
			if (__atomic_load_n(&game_lock_wanted, __ATOMIC_SEQ_CST))
			{
				unsigned long spins = 0;

				pthread_mutex_unlock(&mixer_lock);
				/* (bounded: a waiter slow to wake costs the mixer at
				most this, never the audio) */
				while (__atomic_load_n(&game_lock_wanted, __ATOMIC_SEQ_CST) && ++spins < 20000)
					;
				pthread_mutex_lock(&mixer_lock);
				if (generation != stream_list_generation)
				{
					generation = stream_list_generation;
					next = streams;
				}
			}
		}
		else
		{
			next = stream->next;
		}
		stream = next;
	}
	pthread_mutex_unlock(&mixer_lock);
	statistics_mix_us += statistics_now() - started;
	statistics_mixes++;
	statistics_voices += voices;
	limit(output, frames * OUTPUT_CHANNELS);
	{
		/* (debug) HALO_AUDIO_DUMP=<file>: the mix as it goes to the device,
		raw 32-bit float stereo at 48 kHz, for listening to and measuring
		the output without a device (e.g. sox -t f32 -r 48000 -c 2) */
		static FILE *dump, *reference_dump;
		static int dump_checked, reference_dump_checked;

		dump_mix("HALO_AUDIO_DUMP", &dump, &dump_checked, output, frames);
		if (audio_verify > 0 && frames <= VERIFY_FRAMES)
		{
			limit(verify_mix, frames * OUTPUT_CHANNELS);
			dump_mix("HALO_AUDIO_DUMP_REF", &reference_dump, &reference_dump_checked, verify_mix, frames);
		}
	}
}

/* ---------- output */

static SDL_AudioStream *audio_stream;
static BOOL audio_started = FALSE;
static BOOL frame_locked_mixing = FALSE;

void vita_host_pin_current_thread(int core) __attribute__((weak));
#ifdef HALO_VITA
int vita_host_fourth_core_move(const char *role);
void vita_host_thread_watch(const char *role);
#endif

static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
	float buffer[MIX_CHUNK_FRAMES * OUTPUT_CHANNELS];
	static int pinned;

	(void)userdata;
	(void)total_amount;
	if (!pinned)
	{
		/* (Vita) SDL starts its audio thread on any core, above the
		game's priority: wherever it lands it preempts that core's thread
		for the length of a mix, the tick's on the third core included.
		HALO_AUDIO_CORE=0-2 pins it (default -1: left to the system; on
		core 1, with the render worker, the user heard crackle in the b30
		fight, while unpinned it inflated the tick's sound time).
		HALO_AUDIO_CORE=3: the Vita's fourth core, where the system allows
		it (vita_fourth_core.c; a test: the game waits for the mixer's lock,
		and the system's processes there run first - the sound mixer's line
		says how late the device's calls came) */
		pinned = 1;
		if (vita_host_pin_current_thread)
		{
			const char *setting = getenv("HALO_AUDIO_CORE");
			int core = setting ? atoi(setting) : -1;

			if (core >= 0 && core <= 2)
				vita_host_pin_current_thread(core);
#ifdef HALO_VITA
			if (core == 3)
				vita_host_fourth_core_move("sound mixer");
			else
				vita_host_thread_watch("sound mixer");
#endif
		}
	}
	{
		static unsigned long long previous_call;
		unsigned long long now = statistics_now();

		if (previous_call && now > previous_call)
		{
			unsigned long long gap = now - previous_call;

			if (gap > statistics_callback_gap_us)
				statistics_callback_gap_us = gap;
			if (gap > 25000)
				statistics_callbacks_late++;
		}
		previous_call = now;
	}
	while (additional_amount > 0)
	{
		unsigned long frames = (unsigned long)additional_amount / (OUTPUT_CHANNELS * sizeof(float));

		if (frames > MIX_CHUNK_FRAMES)
			frames = MIX_CHUNK_FRAMES;
		if (!frames)
			frames = 1;
		mix(buffer, frames);
		SDL_PutAudioStreamData(stream, buffer, (int)(frames * OUTPUT_CHANNELS * sizeof(float)));
		additional_amount -= (int)(frames * OUTPUT_CHANNELS * sizeof(float));
	}
}

/* without a device, drain voices in real time */
static void *silent_clock_thread(void *parameter)
{
	float buffer[480 * OUTPUT_CHANNELS];
	struct timespec next;

	(void)parameter;
	clock_gettime(CLOCK_MONOTONIC, &next);
	for (;;)
	{
		mix(buffer, 480);
		next.tv_nsec += 10000000L;
		if (next.tv_nsec >= 1000000000L)
		{
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
	}
	return NULL;
}

static void audio_start(void)
{
	SDL_AudioSpec spec;

	if (audio_started)
		return;
	audio_started = TRUE;
	ima_build_tables();
	master_volume = (float)config_real("audio.volume");

	{
		/* (debug) HALO_FIXED_TICK: no device and no clock thread; the
		game's frames drive the mixer instead (DirectSoundDoWork), one
		tick's worth of audio each, so voices finish on the same frame in
		every run */
		const char *setting = getenv("HALO_FIXED_TICK");

		if (setting && atoi(setting))
		{
			frame_locked_mixing = TRUE;
			return;
		}
	}
	if (config_boolean("audio.enabled") && platform_sdl_initialize())
	{
		spec.format = SDL_AUDIO_F32;
		spec.channels = OUTPUT_CHANNELS;
		spec.freq = OUTPUT_RATE;
		SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
		audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);
		if (audio_stream)
		{
			SDL_ResumeAudioStreamDevice(audio_stream);
			return;
		}
		platform_log("cannot open an audio device (%s); sound is silent", SDL_GetError());
	}
	{
		pthread_t thread;

		pthread_create(&thread, NULL, silent_clock_thread, NULL);
		pthread_detach(thread);
	}
}

/* ---------- completion */

static void packet_release(struct voice_packet *entry)
{
	free(entry->samples);
	entry->samples = NULL;
}

/* completes the head packet; called with the lock held, which the game's
callback runs without */
static void stream_complete_head(struct sdl_stream *stream, DWORD status, DWORD completed_size)
{
	struct voice_packet *entry = &stream->packets[stream->packet_head];
	XMEDIAPACKET packet = entry->packet;

	packet_release(entry);
	entry->finished = FALSE;
	stream->packet_head = (stream->packet_head + 1) % MAXIMUM_STREAM_PACKETS;
	stream->packet_count--;
	if (packet.pdwCompletedSize)
		*packet.pdwCompletedSize = completed_size;
	if (packet.pdwStatus)
		*packet.pdwStatus = status;
	if (stream->callback)
	{
		pthread_mutex_unlock(&mixer_lock);
		stream->callback(stream->context, packet.pContext, status);
		game_lock();
	}
	else if (packet.hCompletionEvent)
	{
		SetEvent(packet.hCompletionEvent);
	}
}

static void streams_complete_finished(void)
{
	struct sdl_stream *stream;

	game_lock();
	for (stream = streams; stream; stream = stream->next)
	{
		while (stream->packet_count && stream->packets[stream->packet_head].finished)
			stream_complete_head(stream, XMEDIAPACKET_STATUS_SUCCESS, stream->packets[stream->packet_head].packet.dwMaxSize);
	}
	pthread_mutex_unlock(&mixer_lock);
}

/* ---------- stream interface */

static struct sdl_stream *stream_from_interface(void *stream)
{
	return (struct sdl_stream *)stream;
}

static ULONG STDMETHODCALLTYPE stream_add_reference(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);
	ULONG count;

	game_lock();
	count = ++stream->reference_count;
	pthread_mutex_unlock(&mixer_lock);
	return count;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object);

static ULONG STDMETHODCALLTYPE stream_release(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);
	struct sdl_stream **link;
	ULONG count;

	game_lock();
	count = --stream->reference_count;
	pthread_mutex_unlock(&mixer_lock);
	if (count)
		return count;

	stream_flush(object);
	game_lock();
	for (link = &streams; *link; link = &(*link)->next)
	{
		if (*link == stream)
		{
			*link = stream->next;
			break;
		}
	}
	stream_list_generation++;
	pthread_mutex_unlock(&mixer_lock);
	free(stream);
	return 0;
}

static HRESULT STDMETHODCALLTYPE stream_get_info(IDirectSoundStream *object, LPXMEDIAINFO information)
{
	struct sdl_stream *stream = stream_from_interface(object);

	memset(information, 0, sizeof(*information));
	information->dwFlags = XMO_STREAMF_FIXED_SAMPLE_SIZE | XMO_STREAMF_INPUT_ASYNC;
	information->dwInputSize = stream->adpcm ? XBOX_ADPCM_BLOCK_BYTES * stream->channels : 2 * stream->channels;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_get_status(IDirectSoundStream *object, LPDWORD status)
{
	struct sdl_stream *stream = stream_from_interface(object);

	*status = __atomic_load_n(&stream->packet_count, __ATOMIC_ACQUIRE) < MAXIMUM_STREAM_PACKETS ? XMO_STATUSF_ACCEPT_INPUT_DATA : 0;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_process(IDirectSoundStream *object, LPCXMEDIAPACKET input, LPCXMEDIAPACKET output)
{
	struct sdl_stream *stream = stream_from_interface(object);
	struct voice_packet *entry;
	unsigned long frames;

	(void)output;
	if (!input)
		return E_INVALIDARG;
	/* The packet is decoded when the mixer first reaches it
	(mix_voice_packet), not here: this is the game's thread, the tick's on
	the Vita, where a heavy fight queued tens of packets a frame. The
	packet's memory (the sound cache's) stays the game's to keep until the
	packet completes, which is after the mixer has played it. */
	frames = stream->adpcm ?
		input->dwMaxSize / (XBOX_ADPCM_BLOCK_BYTES * stream->channels) * XBOX_ADPCM_BLOCK_SAMPLES :
		input->dwMaxSize / (2 * stream->channels);
	game_lock();
	if (stream->packet_count == MAXIMUM_STREAM_PACKETS)
	{
		pthread_mutex_unlock(&mixer_lock);
		return E_OUTOFMEMORY;
	}
	entry = &stream->packets[(stream->packet_head + stream->packet_count) % MAXIMUM_STREAM_PACKETS];
	entry->packet = *input;
	entry->samples = NULL;
	entry->decoded = FALSE;
	entry->frames = frames;
	entry->finished = FALSE;
	if (input->pdwStatus)
		*input->pdwStatus = XMEDIAPACKET_STATUS_PENDING;
	if (input->pdwCompletedSize)
		*input->pdwCompletedSize = 0;
	if (!stream->packet_count)
	{
		/* a stream that ran dry starts over */
		stream->cursor = 0.0;
		stream->gains_valid = FALSE;
	}
	stream->packet_count++;
	pthread_mutex_unlock(&mixer_lock);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_discontinuity(IDirectSoundStream *object)
{
	(void)object;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);

	game_lock();
	while (stream->packet_count)
	{
		struct voice_packet *head = &stream->packets[stream->packet_head];

		stream_complete_head(stream, head->finished ? XMEDIAPACKET_STATUS_SUCCESS : XMEDIAPACKET_STATUS_FLUSHED,
			head->finished ? head->packet.dwMaxSize : 0);
	}
	stream->cursor = 0.0;
	pthread_mutex_unlock(&mixer_lock);
	return S_OK;
}

static IDirectSoundStreamVtbl stream_vtable =
{
	stream_add_reference,
	stream_release,
	stream_get_info,
	stream_get_status,
	stream_process,
	stream_discontinuity,
	stream_flush,
};

/* ---------- the DirectSound object */

struct sdl_direct_sound
{
	ULONG reference_count;
};

static struct sdl_direct_sound direct_sound = { 0 };

HRESULT WINAPI DirectSoundCreate(LPGUID device_id, LPDIRECTSOUND *result, LPUNKNOWN outer)
{
	(void)device_id;
	(void)outer;
	audio_start();
	direct_sound.reference_count++;
	*result = (LPDIRECTSOUND)&direct_sound;
	return DS_OK;
}

ULONG WINAPI IDirectSound_Release(LPDIRECTSOUND sound)
{
	(void)sound;
	return direct_sound.reference_count ? --direct_sound.reference_count : 0;
}

VOID WINAPI DirectSoundDoWork(void)
{
	static int enabled = -1;
	static unsigned long calls;
	static unsigned long long last_report;

	if (frame_locked_mixing)
	{
		static float buffer[(OUTPUT_RATE / 30) * OUTPUT_CHANNELS];

		mix(buffer, OUTPUT_RATE / 30);
	}
	streams_complete_finished();
	if (enabled < 0)
	{
		const char *setting = getenv("HALO_RENDER_PROFILE");

		enabled = setting && atoi(setting) != 0;
	}
	if (enabled && ++calls % 300 == 0)
	{
		unsigned long mixes = statistics_mixes;
		unsigned long long now = statistics_now();
		double elapsed_us = last_report && now > last_report ? (double)(now - last_report) : 0.0;

		last_report = now;
		platform_log("sound mixer: %.1f%% of a core (%.2f ms/mix, %.1f voices), game waits for the mixer %.2f ms/frame (%lu waits);"
			" device calls: longest gap %.1f ms, %lu over 25 ms",
			elapsed_us > 0.0 ? 100.0 * (double)statistics_mix_us / elapsed_us : 0.0,
			mixes ? (double)statistics_mix_us / 1000.0 / (double)mixes : 0.0,
			mixes ? (double)statistics_voices / (double)mixes : 0.0,
			(double)statistics_wait_us / 1000.0 / 300.0, statistics_waits,
			(double)statistics_callback_gap_us / 1000.0, statistics_callbacks_late);
		statistics_callback_gap_us = 0;
		statistics_callbacks_late = 0;
		statistics_mix_us = statistics_wait_us = 0;
		statistics_mixes = statistics_voices = statistics_waits = 0;
		if (audio_verify > 0)
			platform_log("audio verify: %lu voice mixes, largest difference from the reference %.3g (%lu samples differ), "
				"cursor %.3g, %lu with packets finished differently", verify_voices, verify_largest_difference,
				verify_samples_different, verify_largest_cursor_difference, verify_state_mismatches);
	}
}

VOID WINAPI DirectSoundUseFullHRTF(void)
{
}

HRESULT WINAPI IDirectSound_GetCaps(LPDIRECTSOUND sound, LPDSCAPS caps)
{
	(void)sound;
	memset(caps, 0, sizeof(*caps));
	caps->dwFree2DBuffers = 64;
	caps->dwFree3DBuffers = 64;
	caps->dwFreeBufferSGEs = 2047;
	caps->dwMemoryAllocated = 0;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_GetSpeakerConfig(LPDIRECTSOUND sound, LPDWORD speaker_config)
{
	(void)sound;
	*speaker_config = DSSPEAKER_STEREO;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_DownloadEffectsImage(LPDIRECTSOUND sound, LPCVOID image, DWORD image_size,
	LPCDSEFFECTIMAGELOC image_location, LPDSEFFECTIMAGEDESC *image_description)
{
	(void)sound;
	(void)image;
	(void)image_size;
	(void)image_location;
	if (image_description)
		*image_description = NULL;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CommitDeferredSettings(LPDIRECTSOUND sound) { (void)sound; return DS_OK; }
HRESULT WINAPI IDirectSound_SetMixBinHeadroom(LPDIRECTSOUND sound, DWORD mix_bin_mask, DWORD headroom) { (void)sound; (void)mix_bin_mask; (void)headroom; return DS_OK; }
HRESULT WINAPI IDirectSound_SetI3DL2Listener(LPDIRECTSOUND sound, LPCDSI3DL2LISTENER listener_properties, DWORD apply) { (void)sound; (void)listener_properties; (void)apply; return DS_OK; }

HRESULT WINAPI IDirectSound_SetDistanceFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	pthread_mutex_lock(&parameter_lock);
	listener.distance_factor = factor > 0.0f ? factor : 1.0f;
	pthread_mutex_unlock(&parameter_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetRolloffFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	pthread_mutex_lock(&parameter_lock);
	listener.rolloff_factor = factor >= 0.0f ? factor : 1.0f;
	pthread_mutex_unlock(&parameter_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetPosition(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	pthread_mutex_lock(&parameter_lock);
	listener.position[0] = x;
	listener.position[1] = y;
	listener.position[2] = z;
	pthread_mutex_unlock(&parameter_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetVelocity(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)sound; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }

static void normalize3(float *vector)
{
	float length = sqrtf(dot3(vector, vector));

	if (length > 1.0e-6f)
	{
		vector[0] /= length;
		vector[1] /= length;
		vector[2] /= length;
	}
}

HRESULT WINAPI IDirectSound_SetOrientation(LPDIRECTSOUND sound, FLOAT x_front, FLOAT y_front, FLOAT z_front,
	FLOAT x_top, FLOAT y_top, FLOAT z_top, DWORD apply)
{
	(void)sound;
	(void)apply;
	pthread_mutex_lock(&parameter_lock);
	listener.front[0] = x_front;
	listener.front[1] = y_front;
	listener.front[2] = z_front;
	listener.top[0] = x_top;
	listener.top[1] = y_top;
	listener.top[2] = z_top;
	normalize3(listener.front);
	normalize3(listener.top);
	pthread_mutex_unlock(&parameter_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundStream(LPDIRECTSOUND sound, LPCDSSTREAMDESC description,
	LPDIRECTSOUNDSTREAM *result, LPUNKNOWN outer)
{
	struct sdl_stream *stream = calloc(1, sizeof(*stream));
	const WAVEFORMATEX *format = description->lpwfxFormat;

	(void)sound;
	(void)outer;
	if (!stream)
		return E_OUTOFMEMORY;
	stream->object.lpVtbl = &stream_vtable;
	stream->reference_count = 1;
	stream->callback = description->lpfnCallback;
	stream->context = description->lpvContext;
	stream->adpcm = format && format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM;
	stream->channels = format && format->nChannels == 2 ? 2 : 1;
	stream->sample_rate = format ? format->nSamplesPerSec : 0;
	stream->frequency = stream->sample_rate;
	stream->volume = 1.0f;
	/* DirectSound's default mix bins: a mono voice to both fronts, a
	stereo voice's channels to the front left and right */
	stream->mix_left = 1.0f;
	stream->mix_right = 1.0f;
	stream->has_3d = (description->dwFlags & DSSTREAMCAPS_CTRL3D) != 0;
	stream->mode = DS3DMODE_NORMAL;
	stream->minimum_distance = DS3D_DEFAULTMINDISTANCE;
	stream->maximum_distance = DS3D_DEFAULTMAXDISTANCE;
	stream->i3dl2_gain = 1.0f;
	game_lock();
	stream->next = streams;
	streams = stream;
	stream_list_generation++;
	pthread_mutex_unlock(&mixer_lock);
	*result = &stream->object;
	return DS_OK;
}

/* January-era DirectSound exports the game declares itself
(sound_dsound_xbox.c); the XDK 3911 headers no longer carry them */

void __stdcall DirectSoundStopStream(LPDIRECTSOUNDSTREAM stream)
{
	stream_flush(stream);
}

unsigned long __stdcall DirectSoundGetStreamVoiceStatus(LPDIRECTSOUNDSTREAM stream)
{
	struct sdl_stream *record = stream_from_interface(stream);
	unsigned long active;

	/* (a word the mixer never changes: packets are completed on the
	game's threads) */
	active = __atomic_load_n(&record->packet_count, __ATOMIC_ACQUIRE) != 0;
	return active;
}

#define STREAM_SETTER(body) \
	struct sdl_stream *record = stream_from_interface(stream); \
	pthread_mutex_lock(&parameter_lock); \
	body; \
	pthread_mutex_unlock(&parameter_lock); \
	return DS_OK;

HRESULT WINAPI IDirectSoundStream_SetFrequency(LPDIRECTSOUNDSTREAM stream, DWORD frequency)
{
	STREAM_SETTER(record->frequency = frequency ? frequency : record->sample_rate)
}

HRESULT WINAPI IDirectSoundStream_SetVolume(LPDIRECTSOUNDSTREAM stream, LONG volume)
{
	STREAM_SETTER(record->volume = gain_from_millibels(volume))
}

HRESULT WINAPI IDirectSoundStream_SetMixBins(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask)
{
	STREAM_SETTER(
		record->mix_left = (mix_bin_mask & DSMIXBIN_FRONT_LEFT) ? 1.0f : 0.0f;
		record->mix_right = (mix_bin_mask & DSMIXBIN_FRONT_RIGHT) ? 1.0f : 0.0f)
}

/* volumes come in the order of the set bits of the mask */
HRESULT WINAPI IDirectSoundStream_SetMixBinVolumes(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask, const LONG *volumes)
{
	struct sdl_stream *record = stream_from_interface(stream);
	unsigned long bit, index = 0;

	pthread_mutex_lock(&parameter_lock);
	for (bit = 0; bit < 32; bit++)
	{
		if (!(mix_bin_mask & (1UL << bit)))
			continue;
		if ((1UL << bit) == DSMIXBIN_FRONT_LEFT)
			record->mix_left = gain_from_millibels(volumes[index]);
		else if ((1UL << bit) == DSMIXBIN_FRONT_RIGHT)
			record->mix_right = gain_from_millibels(volumes[index]);
		index++;
	}
	pthread_mutex_unlock(&parameter_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundStream_SetMode(LPDIRECTSOUNDSTREAM stream, DWORD mode, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->mode = mode)
}

HRESULT WINAPI IDirectSoundStream_SetPosition(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->position[0] = x; record->position[1] = y; record->position[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetMinDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->minimum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetMaxDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->maximum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetI3DL2Source(LPDIRECTSOUNDSTREAM stream, LPCDSI3DL2BUFFER source, DWORD apply)
{
	LONG direct;

	(void)apply;
	/* the low frequency part of the direct path */
	direct = source->lDirect +
		(LONG)(source->Obstruction.lHFLevel * source->Obstruction.flLFRatio) +
		(LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	if (direct > 0)
		direct = 0;
	{
		STREAM_SETTER(record->i3dl2_gain = gain_from_millibels(direct))
	}
}

HRESULT WINAPI IDirectSoundStream_Pause(LPDIRECTSOUNDSTREAM stream, DWORD pause)
{
	STREAM_SETTER(record->paused = pause == DSSTREAMPAUSE_PAUSE)
}

HRESULT WINAPI IDirectSoundStream_SetVelocity(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)stream; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeAngles(LPDIRECTSOUNDSTREAM stream, DWORD inside, DWORD outside, DWORD apply) { (void)stream; (void)inside; (void)outside; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOrientation(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)stream; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOutsideVolume(LPDIRECTSOUNDSTREAM stream, LONG volume, DWORD apply) { (void)stream; (void)volume; (void)apply; return DS_OK; }

/* ---------- buffers

The game's only buffer is a silent looping one that keeps the voice
processor busy; it needs no mixing. */

struct null_buffer
{
	ULONG reference_count;
	LPVOID data;
	DWORD size;
	BOOL playing;
};

HRESULT WINAPI DirectSoundCreateBuffer(LPCDSBUFFERDESC description, LPDIRECTSOUNDBUFFER *result)
{
	struct null_buffer *buffer = calloc(1, sizeof(*buffer));

	(void)description;
	if (!buffer)
		return E_OUTOFMEMORY;
	buffer->reference_count = 1;
	*result = (LPDIRECTSOUNDBUFFER)buffer;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundBuffer(LPDIRECTSOUND sound, LPCDSBUFFERDESC description,
	LPDIRECTSOUNDBUFFER *result, LPUNKNOWN outer)
{
	(void)sound;
	(void)outer;
	return DirectSoundCreateBuffer(description, result);
}

ULONG WINAPI IDirectSoundBuffer_Release(LPDIRECTSOUNDBUFFER buffer)
{
	struct null_buffer *record = (struct null_buffer *)buffer;
	ULONG count = --record->reference_count;

	if (!count)
		free(record);
	return count;
}

HRESULT WINAPI IDirectSoundBuffer_SetBufferData(LPDIRECTSOUNDBUFFER buffer, LPVOID data, DWORD size)
{
	struct null_buffer *record = (struct null_buffer *)buffer;

	record->data = data;
	record->size = size;
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Play(LPDIRECTSOUNDBUFFER buffer, DWORD reserved1, DWORD reserved2, DWORD flags)
{
	(void)reserved1;
	(void)reserved2;
	(void)flags;
	((struct null_buffer *)buffer)->playing = TRUE;
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Stop(LPDIRECTSOUNDBUFFER buffer)
{
	((struct null_buffer *)buffer)->playing = FALSE;
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetCurrentPosition(LPDIRECTSOUNDBUFFER buffer, DWORD play_cursor) { (void)buffer; (void)play_cursor; return DS_OK; }
HRESULT WINAPI IDirectSoundBuffer_SetLoopRegion(LPDIRECTSOUNDBUFFER buffer, DWORD loop_start, DWORD loop_length) { (void)buffer; (void)loop_start; (void)loop_length; return DS_OK; }
HRESULT WINAPI IDirectSoundBuffer_SetPitch(LPDIRECTSOUNDBUFFER buffer, LONG pitch) { (void)buffer; (void)pitch; return DS_OK; }
HRESULT WINAPI IDirectSoundBuffer_SetVolume(LPDIRECTSOUNDBUFFER buffer, LONG volume) { (void)buffer; (void)volume; return DS_OK; }
