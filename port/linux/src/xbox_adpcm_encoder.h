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
	XBOX_ADPCM_MAXIMUM_CHANNELS = 2
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

/* Writes the part block (its last frame repeated), and silence in the
blocks no frame reached. Returns the output's frames of silence, after
the frames taken. */
uint32_t xbox_adpcm_encoder_finish(
	struct xbox_adpcm_encoder *encoder);

#endif
