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

/* (xbox_adpcm_encoder_begin_sink) takes `size` bytes of whole blocks that
lie at `offset` in the output; 0 stops the output */
typedef int (*xbox_adpcm_sink_proc)(void *context, uint8_t const *bytes, uint32_t size, uint32_t offset);

/* An output of whole Xbox ADPCM blocks being filled, frame by frame. */
struct xbox_adpcm_encoder
{
	/* the output, or with a sink the staging its pieces are written to */
	uint8_t *output;
	uint32_t output_blocks;
	uint32_t block_bytes;
	uint32_t blocks_written;
	int channels;
	/* (a sink's) the staging's blocks and those written to it not yet
	given, and whether the sink stopped the output */
	xbox_adpcm_sink_proc sink;
	void *sink_context;
	uint32_t staging_blocks;
	uint32_t staged_blocks;
	int stopped;
	/* the block being filled, and the step index carried between blocks,
	by channel */
	short block[XBOX_ADPCM_BLOCK_SAMPLES][XBOX_ADPCM_MAXIMUM_CHANNELS];
	int block_frames;
	int step_index[XBOX_ADPCM_MAXIMUM_CHANNELS];
	/* nonzero once every block is written: further frames are left out */
	int full;
	/* frames still to be left out before the first one taken, then frames
	still to be encoded only for the step index they leave (whole blocks,
	the frames held included: xbox_adpcm_encoder_warm), so that the first
	block written is as an encoding of the frames before would have made
	it: a piece of a longer output that starts past its beginning (a link
	of a long sound's chain, custom_edition_sounds.c) */
	uint32_t skip_frames;
	uint32_t warm_frames;
	/* nonzero: once every block is written, the frames given go on filling
	the block being filled (a block's worth at most) instead of being left
	out, for xbox_adpcm_encoder_continue to start the next output with (a
	rate converter's frames made from one input frame, ogg_sound.c) */
	int hold;

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

/* The next `frames` frames, the frames it holds (hold) the first of them,
are encoded only for the step index they leave (whole blocks: the output
then starts at a block's first frame), before an output is begun or
continued; with fewer than it holds, those it holds are let go. */
void xbox_adpcm_encoder_warm(
	struct xbox_adpcm_encoder *encoder,
	uint32_t frames);

/* Starts filling another output with the frames that follow those of the
output it filled, the step index carried and the frames it held (hold)
written first (unless they warm it); the skip and the warming are left as
they are. 0 when the output is not usable (none: every frame is then left
out). A sink's encoder continues into the plain output given, not its
sink. */
int xbox_adpcm_encoder_continue(
	struct xbox_adpcm_encoder *encoder,
	void *output,
	uint32_t output_bytes);

/* The same, into an output of `output_bytes` given to `sink` through
`staging` as xbox_adpcm_encoder_begin_sink's (a link of a long sound's
chain made into the sound cache's block a piece at a time,
custom_edition_sounds.c). 0 when it is not usable (no sink, or a staging
of less than a block: every frame is then left out). */
int xbox_adpcm_encoder_continue_sink(
	struct xbox_adpcm_encoder *encoder,
	void *staging,
	uint32_t staging_bytes,
	uint32_t output_bytes,
	xbox_adpcm_sink_proc sink,
	void *sink_context);

/* Starts filling an output of `output_bytes` (whole blocks of `channels`
channels are used) that is never in memory whole: its blocks are written
into `staging` (`staging_bytes`, at least a block's) and given to `sink`
with their place in the output each time the staging is full, and the rest
at xbox_adpcm_encoder_finish, silence included, so the sink takes every
byte of the output once, in order. A sink that returns 0 stops the output:
the encoder is full, `stopped` is set and the sink is not called again.
0 when the arguments are not usable. */
int xbox_adpcm_encoder_begin_sink(
	struct xbox_adpcm_encoder *encoder,
	void *staging,
	uint32_t staging_bytes,
	uint32_t output_bytes,
	int channels,
	xbox_adpcm_sink_proc sink,
	void *sink_context);

/* One frame: a sample by channel, clamped to 16 bits (left out while
frames are skipped). */
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

/* Where an input of units of `unit_frames` frames (1: 16-bit PCM;
XBOX_ADPCM_BLOCK_SAMPLES: Xbox ADPCM blocks) taken at its rate, or at half
it when `halved`, is to be taken from for its output to go on from output
frame `first_frame` (a block's first) as a taking of the whole input does
there (a link of a long sound's chain: custom_edition_sounds.c): returns
the input frame to start from (a unit's first; halved, the frame before an
output frame's pair is taken again, for the low-pass), with the output
frames then to skip and to warm the encoder with (a block's, from cold:
xbox_adpcm_encoder.skip_frames and warm_frames) into `skip_frames` and
`warm_frames`. */
uint64_t xbox_adpcm_rate_start(
	uint32_t first_frame,
	int halved,
	uint32_t unit_frames,
	uint32_t *skip_frames,
	uint32_t *warm_frames);

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
