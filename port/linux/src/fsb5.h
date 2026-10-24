/*
FSB5.H

(port) FMOD's sound bank, version 5 (FSB5): the container Halo: The Master
Chief Collection keeps its Halo CE sounds in (halo1\sound\pc\*.fsb). The
reader opens a bank through a read function (a positioned read of the
file), reads its header and the table of its streams and, when asked, its
name table, and says where each stream's bytes are, its format, channels,
rate and loop points. It reads no stream: the bytes are the caller's.

The layout is the one FMOD's own tools write and vgmstream (ISC licence,
src/meta/fsb5.c; Adam Gashlin, bnnm and others) documents; this reader is
written from that description, no code of vgmstream's is in it:

	0x00  "FSB5"
	0x04  version (0, or 1)
	0x08  stream count
	0x0C  size of the stream table (bytes)
	0x10  size of the name table
	0x14  size of the stream data
	0x18  format (enum fsb5_format)
	0x1C  zero
	0x20  version 1: flags, then a 16-byte hash and 8 bytes (header: 0x3C);
	      version 0: two zero words, hash and 8 bytes (header: 0x40)
	stream table: for each stream a 64-bit word, low bits first:
	      bit 0       extra data follows
	      bits 1-4    rate (an index: 4000 8000 11000 11025 16000 22050
	                  24000 32000 44100 48000 96000)
	      bits 5-6    channels (1, 2, 6, 8)
	      bits 7-33   data offset / 32, in the stream data
	      bits 34-63  sample (frame) count
	    then, while the last extra word's bit 0 is set, extra words: a
	    32-bit word (bit 0: another follows; bits 1-24: the size of the
	    data after it; bits 25-31: the type: 1 channels, 2 rate, 3 loop
	    start and end, others the format's own) and that data
	name table: an offset (from the table's start) for each stream, then
	      the names, zero-terminated
	stream data
A stream ends where the next one starts (the last, where the data ends).

The reader trusts nothing: every size is checked against the file's, every
offset against the data, and a bank that is not whole (the tables cut off,
streams out of order, a stream past the data) is refused with a reason.
*/

#ifndef __FSB5_H
#define __FSB5_H

#include <stdint.h>

/* ---------- constants */

/* the most streams a bank may have (FMOD's limit is far above any bank) */
#define FSB5_MAXIMUM_STREAMS 262144
/* the stream table and name table may not be larger than this */
#define FSB5_MAXIMUM_TABLE_BYTES (64u * 1024u * 1024u)

enum fsb5_format
{
	_fsb5_format_none,
	_fsb5_format_pcm8,
	_fsb5_format_pcm16,
	_fsb5_format_pcm24,
	_fsb5_format_pcm32,
	_fsb5_format_pcm_float,
	_fsb5_format_gc_adpcm,
	/* IMA ADPCM: the Xbox's, 36-byte blocks of 64 samples a channel
	(stereo: the channels' 4-byte groups interleaved), which is also what
	Halo's own Xbox ADPCM sounds are */
	_fsb5_format_ima_adpcm,
	_fsb5_format_vag,
	_fsb5_format_hevag,
	_fsb5_format_xma,
	_fsb5_format_mpeg,
	/* CELT (libcelt 0.11 frames with FMOD's own header: fsb5_celt_frame_check) */
	_fsb5_format_celt,
	_fsb5_format_at9,
	_fsb5_format_xwma,
	_fsb5_format_vorbis,
	_fsb5_format_fadpcm,
	_fsb5_format_opus,
	NUMBER_OF_FSB5_FORMATS
};

/* ---------- structures */

struct fsb5_stream
{
	/* the stream's bytes: its offset from the start of the FILE (not of
	the data) and its length, up to the next stream's start (so with the
	padding FMOD aligns them with) */
	uint64_t offset;
	uint32_t bytes;
	/* samples (frames) a channel, rate in Hz, channels */
	uint32_t samples;
	uint32_t rate;
	uint8_t channels;
	/* the loop (FMOD writes both, in samples; the end is inclusive) when
	has_loop */
	uint8_t has_loop;
	uint32_t loop_start;
	uint32_t loop_end;
	/* the name's trailing "_<a>_<b>" numbers (Halo's banks name their
	streams "<sound>_<pitch range>_<permutation>"), -1 when the name has
	none or the names were not read */
	int32_t name_range;
	int32_t name_permutation;
	/* the name table's offset of its name (names read), else 0xFFFFFFFF */
	uint32_t name_offset;
};

struct fsb5
{
	uint32_t version;
	enum fsb5_format format;
	uint32_t stream_count;
	uint64_t file_bytes;
	/* where the stream data starts in the file, and its size */
	uint64_t data_offset;
	uint32_t data_bytes;
	struct fsb5_stream *streams;
	/* the name table (when read), and its size */
	char *names;
	uint32_t names_bytes;
};

/* reads `size` bytes at `offset` of the bank into `buffer`; nonzero when
all were read */
typedef int (*fsb5_read_function)(void *context, uint64_t offset, uint32_t size, void *buffer);

/* ---------- prototypes/FSB5.C */

/* Opens the bank of `file_bytes` bytes `read` serves: its header and
stream table, and with `read_names` its name table too. 1 on success (free
it with fsb5_close), else 0 with `error` (`error_size` bytes; may be NULL)
saying why; `bank` is then empty. */
int fsb5_open(
	fsb5_read_function read,
	void *context,
	uint64_t file_bytes,
	int read_names,
	struct fsb5 *bank,
	char *error,
	unsigned long error_size);
void fsb5_close(struct fsb5 *bank);

/* The name of stream `index`, or NULL (names not read, a bad index, or a
name that is not terminated inside the table). */
char const *fsb5_stream_name(struct fsb5 const *bank, uint32_t index);

/* How many bytes of IMA ADPCM `samples` frames of `channels` take: 36 bytes
for each 64 frames (the last block whole) and each channel. 0 if it does not
fit 32 bits. */
uint32_t fsb5_ima_bytes(uint32_t samples, unsigned channels);

/* Whether `frame` (`size` bytes) starts a CELT frame as FMOD writes one: a
32-bit big-endian 0x17C30DF3 and the frame's size (little-endian), which
the bytes must hold. 1 when it does, with the payload size in
`payload_bytes` (may be NULL). */
int fsb5_celt_frame_check(uint8_t const *frame, uint32_t size, uint32_t *payload_bytes);

#endif
