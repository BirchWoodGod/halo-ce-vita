/*
XBOX_ADPCM_ENCODER.H

16-bit PCM written as the Xbox ADPCM the game's sound cache, its channels
and the mixer play (xbox_adpcm_encoder.c): for Halo Custom Edition's Ogg
Vorbis permutations once decoded (ogg_sound.c) and its 16-bit PCM ones
(custom_edition_sounds.c).
*/

#ifndef __HALO_LINUX_XBOX_ADPCM_ENCODER_H
#define __HALO_LINUX_XBOX_ADPCM_ENCODER_H

#include <stdint.h>

/* ---------- constants */

enum
{
	/* an Xbox ADPCM block: 64 samples a channel in 36 bytes a channel */
	XBOX_ADPCM_BLOCK_SAMPLES = 64,
	XBOX_ADPCM_BLOCK_BYTES = 36,
	XBOX_ADPCM_MAXIMUM_CHANNELS = 2,

	/* the rates (Hz) a rate converter takes: in from 1 kHz to 192 kHz, out
	from 1 kHz to 48 kHz (the most the sound cache plays) */
	XBOX_ADPCM_MINIMUM_RATE = 1000,
	XBOX_ADPCM_MAXIMUM_INPUT_RATE = 192000,
	XBOX_ADPCM_MAXIMUM_OUTPUT_RATE = 48000
};

enum xbox_adpcm_rate_mode
{
	/* the input's rate is the output's: frames pass through */
	_xbox_adpcm_rate_same,
	/* twice the output's: each pair of frames one frame, low-passed
	(1 2 1)/4 around the pair's first (Halo PC's 44 kHz mono sounds,
	played at 22 kHz, the only rate the game plays mono sounds at) */
	_xbox_adpcm_rate_halved,
	/* any other: interpolated linearly, low-passed (1 2 1)/4 first when
	the rate goes down */
	_xbox_adpcm_rate_linear
};

/* ---------- structures */

/* An output of whole Xbox ADPCM blocks being filled, frame by frame. */
struct xbox_adpcm_encoder
{
	uint8_t *output;
	uint32_t output_blocks;
	uint32_t block_bytes;
	uint32_t blocks_written;
	int channels;
	/* the block being filled, and the step index carried between blocks,
	by channel */
	short block[XBOX_ADPCM_BLOCK_SAMPLES][XBOX_ADPCM_MAXIMUM_CHANNELS];
	int block_frames;
	int step_index[XBOX_ADPCM_MAXIMUM_CHANNELS];
	/* nonzero once every block is written: further frames are left out */
	int full;

	/* the frames taken, and whether any were left out; the samples' sum of
	squares and largest magnitude (for the logs and the tests) */
	uint32_t frames;
	int truncated;
	double sum_of_squares;
	int peak;
};

/* An encoder's frames taken at another rate (xbox_adpcm_rate_begin). */
struct xbox_adpcm_rate
{
	struct xbox_adpcm_encoder *encoder;
	enum xbox_adpcm_rate_mode mode;
	uint32_t input_rate;
	uint32_t output_rate;
	/* halved: the input's frame before the pair being filtered, the pair's
	first frame, and whether there is one */
	int previous[XBOX_ADPCM_MAXIMUM_CHANNELS];
	int pending[XBOX_ADPCM_MAXIMUM_CHANNELS];
	int have_pending;
	/* linear: the low-pass's last two input frames (and how many it has),
	then the frame the next output frames are interpolated from, and the
	next output frame's place past it, in input frames times the output
	rate (below the output rate while it lies before the next input frame) */
	int filter[2][XBOX_ADPCM_MAXIMUM_CHANNELS];
	int filter_frames;
	int last[XBOX_ADPCM_MAXIMUM_CHANNELS];
	int have_last;
	uint32_t position;
};

/* ---------- prototypes */

/* bytes of Xbox ADPCM that hold `frames` frames of `channels` channels (1
or 2; 0 for another count) */
uint32_t xbox_adpcm_bytes(uint32_t frames, int channels);

/* Starts filling `output` (`output_bytes`; whole blocks of `channels`
channels are used, any rest is left alone). 0 when the arguments are not
usable (no output, or not one or two channels). */
int xbox_adpcm_encoder_begin(
	struct xbox_adpcm_encoder *encoder,
	void *output,
	uint32_t output_bytes,
	int channels);

/* One frame: a sample by channel, clamped to 16 bits. */
void xbox_adpcm_encoder_frame(
	struct xbox_adpcm_encoder *encoder,
	int const *frame);

/* The frames of `bytes` bytes of 16-bit little-endian PCM of the encoder's
channels, interleaved (Halo PC's caches; its tags are big-endian); a part
frame at the end is left out, so pieces of a longer input must be whole
frames. Reads only those bytes. */
void xbox_adpcm_encoder_pcm(
	struct xbox_adpcm_encoder *encoder,
	uint8_t const *pcm,
	uint32_t bytes);

/* Starts taking frames of `input_rate` for `encoder` (begun) at
`output_rate`. 0 when the rates are not within the bounds above (frames
given then are left out). */
int xbox_adpcm_rate_begin(
	struct xbox_adpcm_rate *rate,
	struct xbox_adpcm_encoder *encoder,
	long input_rate,
	long output_rate);

/* One frame of the input: a sample by the encoder's channel. */
void xbox_adpcm_rate_frame(
	struct xbox_adpcm_rate *rate,
	int const *frame);

/* The input's last frames (before xbox_adpcm_encoder_finish). */
void xbox_adpcm_rate_finish(
	struct xbox_adpcm_rate *rate);

/* The frames of output of `input_frames` frames at `input_rate` taken at
`output_rate`: as many, half (rounded up) for twice the rate, else the
output frames that lie before the input's end; 0 when the rates are not
within the bounds above, or the count passes 32 bits. */
uint32_t xbox_adpcm_rate_frames(
	uint64_t input_frames,
	long input_rate,
	long output_rate);

/* The 64 frames of one Xbox ADPCM block of `channels` channels (1 or 2:
XBOX_ADPCM_BLOCK_BYTES a channel), as the mixer decodes them (dsound_sdl.c,
decode_adpcm_blocks: the header's sample, then 63 nibbles; the padding
nibble is not played), interleaved into `frames`. */
void xbox_adpcm_block_decode(
	uint8_t const *block,
	int channels,
	short frames[XBOX_ADPCM_BLOCK_SAMPLES * XBOX_ADPCM_MAXIMUM_CHANNELS]);

/* Writes the part block (its last frame repeated), and silence in the
blocks no frame reached. Returns the output's frames of silence, after
the frames taken. */
uint32_t xbox_adpcm_encoder_finish(
	struct xbox_adpcm_encoder *encoder);

#endif
