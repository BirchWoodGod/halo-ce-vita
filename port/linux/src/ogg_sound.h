/*
OGG_SOUND.H

Halo Custom Edition's Ogg Vorbis sound permutations made Xbox ADPCM, the
format the game's sound cache, its channels and the mixer play
(ogg_sound.c).
*/

#ifndef __HALO_LINUX_OGG_SOUND_H
#define __HALO_LINUX_OGG_SOUND_H

#include <stddef.h>
#include <stdint.h>

/* ---------- constants */

enum
{
	/* an Xbox ADPCM block: 64 samples a channel in 36 bytes a channel */
	OGG_SOUND_ADPCM_BLOCK_SAMPLES = 64,
	OGG_SOUND_ADPCM_BLOCK_BYTES = 36,

	/* the decoder's working memory (ogg_sound_transcode): Halo PC's
	streams need 120-165 KB on a 32-bit machine (one channel or two), and
	libvorbis's most demanding setting (quality 10, 44 kHz stereo) 218 KB;
	a stream that needs more than it is given is refused. The heap's own
	bookkeeping takes a few KB of the least it can be given */
	OGG_SOUND_WORKING_BYTES = 384 * 1024,
	OGG_SOUND_MINIMUM_WORKING_BYTES = 32 * 1024,

	/* the input is read this many bytes at a time */
	OGG_SOUND_READ_BYTES = 16 * 1024,

	/* the highest output rate (Hz) */
	OGG_SOUND_MAXIMUM_RATE = 48000,

	/* the most Xbox ADPCM a permutation is given (the sound cache holds
	4 MB): 21 s of 44 kHz stereo, 85 s of 22 kHz mono. A longer sound is
	made a chain of permutations as it loads (custom_edition_sounds.c), as
	Halo's own music is (retail Xbox permutations are 377064 bytes at
	most, and Halo PC's tool splits long sounds into permutations of ~5 s);
	where it cannot be, it is cut off there */
	OGG_SOUND_MAXIMUM_ADPCM_BYTES = 1024 * 1024
};

enum ogg_sound_status
{
	_ogg_sound_ok,
	/* the input could not be read */
	_ogg_sound_read_failed,
	/* not an Ogg Vorbis stream, or a damaged one */
	_ogg_sound_not_vorbis,
	/* a stream of more than two channels, or of a rate out of the rate
	converter's bounds (1 to 192 kHz: xbox_adpcm_encoder.h) */
	_ogg_sound_unsupported_format,
	/* the stream needed more working memory than it is given */
	_ogg_sound_out_of_memory,
	/* the arguments (sizes, the output) are not usable */
	_ogg_sound_bad_arguments,
	NUMBER_OF_OGG_SOUND_STATUSES
};

/* ---------- structures */

/* reads `size` bytes at `offset` of the stream into `buffer`; nonzero on
success */
typedef int (*ogg_sound_read_proc)(void *context, uint32_t offset, uint32_t size, void *buffer);

struct ogg_sound_request
{
	ogg_sound_read_proc read;
	void *read_context;
	/* the stream's size in bytes */
	uint32_t input_bytes;

	/* the output: whole Xbox ADPCM blocks of `channels` channels (1 or 2),
	`output_bytes` of them; what the stream does not fill is silence, and
	what does not fit is left out */
	void *output;
	uint32_t output_bytes;
	/* or, with a sink, the output is never in memory whole: `output` is a
	staging of `staging_bytes` (at least a block's) and the sink takes the
	output's pieces in order, silence included, and may stop the decoding
	(xbox_adpcm_encoder_begin_sink) */
	int (*sink)(void *context, uint8_t const *bytes, uint32_t size, uint32_t offset);
	void *sink_context;
	uint32_t staging_bytes;
	int channels;
	/* the rate the sound is played at (Hz): the stream's, half of it (a
	44 kHz mono sound is played at 22 kHz, the only rate the game plays
	mono sounds at: each pair of frames becomes one), or any other the
	stream is interpolated to (a stream of another rate than its sound's) */
	long rate;

	/* the decoder's working memory (OGG_SOUND_WORKING_BYTES; at least
	OGG_SOUND_MINIMUM_WORKING_BYTES), aligned to 8 bytes; nothing else is
	allocated */
	void *working;
	size_t working_bytes;

	/* A piece of the stream's output (a link of a long sound's chain):
	the output starts at the stream's output frame `first_frame`, the
	frames before it decoded and left out. With `keep`, a decoding the
	output fills before the stream's end is kept in the working memory (as
	the stream `stream_key`, the caller's name for it), and a later
	request of the same stream, channels and rate with the same working
	memory whose first frame is not before where it stopped takes it up
	there (result.resumed) instead of decoding the stream from its start;
	the working memory must then be left alone between the two. The read
	procedure and its context may differ (they read the same stream). */
	uint32_t first_frame;
	int keep;
	uint64_t stream_key;
};

/* what a stream's first and last pages say (ogg_sound_measure) */
struct ogg_sound_stream_info
{
	int channels;
	long rate;
	/* the stream's length: the granule position of its last page that has
	one */
	uint64_t frames;
};

struct ogg_sound_result
{
	enum ogg_sound_status status;
	/* the stream's channels and rate (0 before its first header), and
	whether its rate was halved */
	int stream_channels;
	long stream_rate;
	int halved;
	/* the output's frames: decoded, and of silence after them */
	uint32_t frames_decoded;
	uint32_t frames_padded;
	/* nonzero when the stream had more than the output holds (the rest is
	left out, not decoded) */
	int truncated;
	/* nonzero when the sink stopped the decoding (the output not whole) */
	int stopped;
	/* the decoded samples' sum of squares and largest magnitude (16-bit
	PCM, before the ADPCM encoding; for the logs and the tests) */
	double sum_of_squares;
	int peak;
	/* the most working memory in use at once */
	size_t working_peak_bytes;
	/* (a piece: request.first_frame) whether a kept decoding was taken up,
	the frames decoded and left out before the output, and whether the
	decoding is kept for the next piece */
	int resumed;
	uint32_t frames_skipped;
	int kept;
};

/* ---------- prototypes */

/* bytes of Xbox ADPCM that hold `frames` frames of `channels` channels */
uint32_t ogg_sound_adpcm_bytes(uint32_t frames, int channels);

/* frames of output for a stream of `frames` frames at `stream_rate` played
at `rate`: as many, half (rounded up) when the stream's rate is twice the
output's, else as many as lie before its end at the output's rate
(xbox_adpcm_rate_frames); 0 when a rate is out of bounds */
uint32_t ogg_sound_output_frames(uint64_t frames, long stream_rate, long rate);

/* A stream's channels and rate from its first page (`head`: the stream's
first bytes, its identification header), and its length from the last
complete page of the same stream in `tail` (its last bytes) with a granule
position. Reads only within the two buffers. Nonzero when both were found;
the length is the stream's word, which ogg_sound_transcode does not trust
(the decoded frames fill the output, or are cut off). */
int ogg_sound_measure(
	uint8_t const *head,
	uint32_t head_bytes,
	uint8_t const *tail,
	uint32_t tail_bytes,
	struct ogg_sound_stream_info *info);

/* Decodes the Ogg Vorbis stream the request reads and writes it as Xbox
ADPCM into the request's output. Any input is safe: the stream is read in
pieces at offsets below input_bytes, every write stays within the output
and the working memory, and a stream that is damaged, of another format or
too demanding stops the decoding with a status (the output then holds what
was decoded, and silence). One call at a time (the working memory's
allocator is the decoder's). Returns the result's status. */
enum ogg_sound_status ogg_sound_transcode(
	struct ogg_sound_request const *request,
	struct ogg_sound_result *result);

/* Nonzero when the working memory (of `working_bytes`, as given to
ogg_sound_transcode) holds a kept decoding of the stream `stream_key`, and
the output frame it goes on with into `next_frame`. */
int ogg_sound_kept(
	void const *working,
	size_t working_bytes,
	uint64_t stream_key,
	uint32_t *next_frame);

char const *ogg_sound_status_describe(enum ogg_sound_status status);

#endif
