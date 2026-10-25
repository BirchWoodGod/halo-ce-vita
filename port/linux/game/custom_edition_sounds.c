/*
CUSTOM_EDITION_SOUNDS.C

The Ogg Vorbis and 16-bit PCM sounds of Halo Custom Edition maps (and of
the Halo: CE Anniversary maps of MCC's mods, custom_edition_mods.c), played
by this build's sound cache as Xbox ADPCM (custom_edition_cache.h).

The loader makes an Ogg Vorbis sound an Xbox ADPCM one to the sound
manager, its channels and the mixer (cache_file_formats.c, sound_prepare:
the sound's compression, and a 44 kHz mono sound's rate, the only one the
game plays mono sounds at being 22 kHz), and leaves its permutations Ogg
Vorbis (compression 3), their samples the stream in the map or in
sounds.map. When the sound cache loads such a permutation
(xbox_sound_cache.c) it gives it a block of the Xbox ADPCM's size, and a
thread of its own here makes the ADPCM and hands it to the block
(port/linux/src/ogg_sound.c), then marks the block loaded as the cache file
thread does a read: the game thread and the tick never decode, and never
wait for a decoding.

(port) The sound cache file. Decoding is what an Ogg Vorbis map costs: a
Halo: CE Anniversary map has 5000-5800 Ogg Vorbis permutations (47-61 MB of
streams, 130-180 MB of Xbox ADPCM), and on the owner's Vita (Oct 10 2026,
Ruby's Rebalanced b30) 103 of them took 19.6 s of the fourth core in 80 s
of play, a quarter of the core the map reader, the cache file thread and
the mixer share, where a stock map's Xbox ADPCM is read and played as it
is. So each permutation's ADPCM, once made, is kept in a file beside the
map's (custom_edition_cache_sound_cache_path: a mod's map's in its mod's
cache folder, d:\mods\<mod>\cache\<map>.sndcache, a maps folder map's in
d:\sound_cache\<map>.sndcache), and read from there the next time instead
of decoded: from the next time the sound cache loads it, in this game or
the next. While the thread has nothing to do it fills the file in the
background, the shortest permutations first (HALO_SOUND_CACHE_PREFILL=0:
not), on the Vita below every other thread (DECODER_PRIORITY), so it takes
only time the fourth core does not use; a permutation the sound cache asks
for stops it at once. HALO_SOUND_CACHE=0: no file at all, every
permutation decoded as it loads, as before; HALO_SOUND_CACHE_MB the file's
largest size (512 MB: every permutation of the largest of Ruby's maps).

The file is the player's, but taken as a stranger's: a header (its version,
the map's file name, size and header CRC-32, sounds.map's size, and the
CRC-32 of the list of permutations, the stream's place and size, its
ADPCM's size, channels and rate of each, in the order of the streams), a
table of one entry a permutation in that order, then the ADPCM. An entry
says where its ADPCM is in the file, the CRC-32 of the stream it was made
from and of the ADPCM, and has a CRC-32 of its own. A header that is not
this map's, of another version, or damaged makes a new file; an entry that
is not its permutation's, damaged, or whose ADPCM is not within the file is
empty; when one is read, the stream (read from the map as the decoder would)
must have the CRC-32 the entry says, and the ADPCM read the entry's, else
the entry is emptied, on disk too, and the permutation decoded. An entry is
written after its ADPCM, so a file cut short leaves an empty entry or one
whose ADPCM does not check. Nothing in the file is trusted further: the
ADPCM is the size the permutation's block is, and only ever copied into
it.

The block is the sound cache's, and the cache may delete it at any time
(the least recently used goes for a new one): what the thread makes goes
through a staging buffer, and each piece into the block under the lock,
only while the block is still the decoding's. A deleted block's decoding
is told so and the deleting thread does not wait (custom_edition_sound_cancel:
it waited up to the decoder's next read before, and on the Vita, where the
decoder held the fourth core for a second at a time, the tick waited that
long); the decoding goes on for the file alone while nothing else waits.

The ADPCM's size is the stream's length, which the permutation's buffer
size gives (Halo PC's 16-bit PCM bytes; the loader's sample_buffer_size
becomes the ADPCM bytes). Protected maps scramble that field, so where the
loader found it implausible, the stream's first and last pages say it the
first time the permutation is loaded: the stream's first and last few
kilobytes read on the game thread (custom_edition_sound_cache_bytes), as
it reads a Custom Edition map's ADPCM sounds whole today. Either way the
size is only the space the decoding fills: what the stream decodes to past
it is cut off, and short of it is silence. Such a permutation has no entry
in the sound cache file (the list of permutations is the loader's).

The thread reads the streams through the map's files opened again
(custom_edition_cache_reader_open), with positioned reads and no lock:
custom_edition_cache_read's lock kept the tick's and the render worker's
reads waiting while the decoder read.

The decoder's working memory (384 KB: Halo PC's streams use 120-165 KB),
with 66 KB for measuring streams and the sound cache file's table (32 bytes
an entry and 2 for the background's order: ~200 KB for 6000 permutations),
is a memory block of its own (halo_custom_edition_memory_alloc, not the C
heap), taken as a map with Ogg Vorbis sounds loads (or for its first such
sound) and given back as the map goes (custom_edition_sounds_stop). The
shortest waiting decoding is done first, so a weapon's sound is not kept
behind the music the sound manager prefetches (sound_manager.c, three
links ahead).

An Xbox ADPCM sound's 16-bit PCM permutations (compression none: Halo PC
plays them, and maps made with mods have them, extinction's scarab bolt
among them) go the same way: the loader leaves them PCM and makes their
buffer size the Xbox ADPCM's (cache_file_formats.c, sound_prepare), and
the thread here reads the PCM in pieces and encodes it
(xbox_adpcm_encoder.c), needing no working memory (and no file: encoding
is cheap). Read as Xbox ADPCM they were noise.

A 44 kHz mono sound of Xbox ADPCM or 16-bit PCM, which the game would not
play (mono sounds at 22 kHz only), the loader makes a 22 kHz one and marks
its permutations (CUSTOM_EDITION_PERMUTATION_*_HALVED, cache_file_formats.h)
with their buffer size the halved ADPCM's; the thread here reads their
samples in pieces (Xbox ADPCM decoded a block at a time,
xbox_adpcm_block_decode), takes them at half their rate and encodes them
(xbox_adpcm_rate, xbox_adpcm_encoder.c), as it does the Ogg Vorbis streams
(an Ogg Vorbis stream of another rate than its sound's is taken at the
sound's: ogg_sound.c). Nothing is decoded as the map loads: a permutation
is when the sound cache loads it, into the block it gives it.

A permutation longer than the sound cache gives one
(OGG_SOUND_MAXIMUM_ADPCM_BYTES: 42 s of 22 kHz stereo; a map's music) is
made a chain of permutations of ~256 KB each as the map loads
(custom_edition_sounds_map_loaded), as Halo's own music is: the pitch
range's permutations and the links are copied into a memory block of their
own, the range pointed at them, and the sound marked as having linked
permutations, so the sound manager plays the links in turn, asks for the
next ones ahead, and loops back to the first only after the last. Each
link is made Xbox ADPCM as the cache loads it: a 16-bit PCM one read from
its first frame, an Ogg Vorbis one in a stream decoder (working memory of
its own, two at most) taking up the decoding where the link before kept it
(ogg_sound_request.keep), the links of a stream decoded in order; else from
the stream's start.

HALO_OGG_TRACE=1 logs each decoding: the sound, the frames, the decoded
samples' RMS and peak, the time and working memory it took, and where its
ADPCM is (run_ce_ogg_sound_test.sh), and each permutation read from the
sound cache file or filled in the background. A permutation that does not
decode is logged once (a sound's first), whatever the setting, and plays
as silence.
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "cseries_windows.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "cache/cache_files.h"
#include "sound/sound_definitions.h"
#include "custom_edition_cache.h"
#include "cache_file_formats.h"
#include "../src/ogg_sound.h"
#include "../src/xbox_adpcm_encoder.h"
#include "../src/port_config.h"

#include <pthread.h>
#include "halo_cond.h"
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* the permutation's compressions made Xbox ADPCM here: 16-bit PCM and
	Ogg Vorbis (sound_manager.c) */
	SOUND_PERMUTATION_COMPRESSION_PCM = 0,
	SOUND_PERMUTATION_COMPRESSION_OGG_VORBIS = 3,
	/* 16-bit PCM (and streams, for their CRC-32) are read this many bytes
	at a time (whole frames) */
	PCM_READ_BYTES = 16 * 1024,
	/* the ADPCM goes to the block and the file this many bytes at a time:
	whole blocks of one channel and of two (36 * 2 * 224) */
	STAGING_BYTES = 16128,
	/* as many decodings can wait as the sound cache has blocks
	(xbox_sound_cache.c: 512) */
	MAXIMUM_WAITING_SOUNDS = 512,
	/* the stream's first bytes, and its last, read to measure it
	(custom_edition_sound_cache_bytes): first a few kilobytes of its end,
	then as many as an Ogg page can be (65307 bytes) */
	MEASURE_HEAD_BYTES = 512,
	MEASURE_TAIL_BYTES = 8192,
	MEASURE_LARGEST_TAIL_BYTES = 66 * 1024,
	/* the memory block: the decoder's working memory, then the measuring's
	(the sound cache's thread's), then the sound cache file's table */
	MEMORY_BLOCK_BYTES = OGG_SOUND_WORKING_BYTES + MEASURE_LARGEST_TAIL_BYTES + MEASURE_HEAD_BYTES,
	/* the decoding thread's stack: Tremor's deepest use is ~32 KB */
	DECODER_STACK_BYTES = 256 * 1024,

	/* a permutation of more Xbox ADPCM than the sound cache gives one
	(OGG_SOUND_MAXIMUM_ADPCM_BYTES) is made a chain of links of about this
	much each, of equal length: the sound manager asks for three links
	ahead of the one playing (sound_manager.c), which with the one playing
	take about the cache the cut-off permutation took (1 MB); 6 s of 22 kHz
	stereo, 3 s of 44 kHz */
	CHAIN_LINK_BYTES = 256 * 1024,
	/* the most of a permutation made a chain (the rest is cut off): 11 min
	of 44 kHz stereo, 45 of 22 kHz stereo */
	CHAIN_MAXIMUM_ADPCM_BYTES = 64 * 1024 * 1024,
	/* the most links a map's chains add (124 bytes each) */
	CHAIN_MAXIMUM_LINKS = 4096,
	/* the most permutations a pitch range may have (next_permutation_index
	is a short) */
	CHAIN_MAXIMUM_RANGE_PERMUTATIONS = 32767,
	/* an Ogg Vorbis permutation the loader found no plausible length for
	(none, or more than a permutation holds: Halo PC's word for a long
	sound, or a protected map's scrambled one) is measured as the map loads
	when its stream is this big or bigger: a stream that long a
	permutation cannot hold is (Vorbis is a twelfth of Xbox ADPCM at the
	least, but for near silence); a smaller one is measured as it first
	loads, as before, and played at most as long as a permutation holds */
	CHAIN_MEASURE_STREAM_BYTES = 32 * 1024,
	/* the Ogg Vorbis chains whose decoding is kept between their links
	(ogg_sound_request.keep), each in working memory of its own: the
	music and a long sound over it (more alternate, each link of theirs
	then decoded from the stream's start) */
	STREAM_DECODERS = 2,
#ifdef HALO_VITA
	/* (the Vita's 64-191, the lower the sooner) the decoding thread's: below
	every other thread's (the game's 160, the texture decoder's and the
	shader compiler's 180), so that it takes only time no other thread
	wants - the Vita does not share a core between threads of a priority,
	and at the cache file thread's and the map reader's the decoder kept the
	fourth core for a second at a time (HALO_OGG_DECODER_PRIORITY) */
	DECODER_PRIORITY = 189,
#endif

	/* the sound cache file (the description above): its header, its
	entries, and the most entries */
	SOUND_CACHE_FILE_VERSION = 1,
	SOUND_CACHE_FILE_HEADER_BYTES = 128,
	SOUND_CACHE_FILE_ENTRY_BYTES = 32,
	SOUND_CACHE_MAXIMUM_ENTRIES = 16384,
	SOUND_CACHE_DEFAULT_MEGABYTES = 512,
	SOUND_CACHE_PATH_BYTES = 256,
	/* (the header's fields) */
	SOUND_CACHE_HEADER_VERSION_OFFSET = 8,
	SOUND_CACHE_HEADER_HEADER_BYTES_OFFSET = 12,
	SOUND_CACHE_HEADER_ENTRY_BYTES_OFFSET = 16,
	SOUND_CACHE_HEADER_ENTRY_COUNT_OFFSET = 20,
	SOUND_CACHE_HEADER_MAP_BYTES_OFFSET = 24,
	SOUND_CACHE_HEADER_MAP_HEADER_CRC_OFFSET = 28,
	SOUND_CACHE_HEADER_SOUNDS_BYTES_OFFSET = 32,
	SOUND_CACHE_HEADER_LIST_CRC_OFFSET = 36,
	SOUND_CACHE_HEADER_NAME_OFFSET = 40,
	SOUND_CACHE_HEADER_CRC_OFFSET = SOUND_CACHE_FILE_HEADER_BYTES - 4,
	/* (an entry's: the permutation's, then where its ADPCM is and the
	CRC-32s; its own CRC-32 is of the rest, with its index) */
	SOUND_CACHE_ENTRY_SOURCE_OFFSET = 0,
	SOUND_CACHE_ENTRY_SOURCE_BYTES = 4,
	SOUND_CACHE_ENTRY_ADPCM_BYTES = 8,
	SOUND_CACHE_ENTRY_FORMAT = 12,
	SOUND_CACHE_ENTRY_DATA_OFFSET = 16,
	SOUND_CACHE_ENTRY_SOURCE_CRC = 20,
	SOUND_CACHE_ENTRY_ADPCM_CRC = 24,
	SOUND_CACHE_ENTRY_CRC = 28
};

/* the first frame of a permutation of the chains' table that is not a
link (one of a pitch range copied there with them) */
#define CHAIN_NOT_A_LINK 0xFFFFFFFFUL

/* sample_buffer_size of a permutation found unplayable */
#define SOUND_BUFFER_UNPLAYABLE 0xFFFFFFFFUL
/* the sound cache file's first bytes */
#define SOUND_CACHE_FILE_MAGIC "HCESNDC1"
/* (mixed into an entry's CRC-32 with its index: a zeroed entry never
checks) */
#define SOUND_CACHE_ENTRY_SALT 0x5EC7A11EUL

/* ---------- structures */

enum transcoding
{
	_transcoding_none,
	_transcoding_ogg_vorbis,
	_transcoding_pcm,
	/* (a 44 kHz mono sound's, made 22 kHz: CUSTOM_EDITION_PERMUTATION_*_HALVED) */
	_transcoding_pcm_halved,
	_transcoding_adpcm_halved
};

struct waiting_sound
{
	enum transcoding transcoding;
	/* the stream: in the combined space custom_edition_cache_read serves */
	unsigned long file_offset;
	uint32_t file_bytes;
	/* the cache block and its loaded flag (NULL: the background's, for the
	sound cache file alone) */
	byte *destination;
	uint32_t destination_bytes;
	boolean *loaded;
	int channels;
	long rate;
	/* its entry in the sound cache file (NONE: none) */
	long entry;
	/* the permutation, for the logs (its tags outlast the decoding:
	custom_edition_sounds_stop) */
	struct sound_permutation const *permutation;
	/* a link of a chain: its first frame in its sound's Xbox ADPCM (0 for
	any other permutation), its index and the chain's links (for the logs),
	and whether it is an Ogg Vorbis link, decoded in a stream decoder */
	boolean link;
	uint32_t first_frame;
	long link_index;
	long link_count;
	boolean stream_link;
};

/* a permutation's entry in the sound cache file (the description above) */
struct sound_cache_entry
{
	/* the permutation's: its stream's place in the combined space and
	size, its ADPCM's size, and its channels | rate << 8 */
	uint32_t source_offset;
	uint32_t source_bytes;
	uint32_t adpcm_bytes;
	uint32_t format;
	/* the file's: where its ADPCM is (0: nowhere), and the CRC-32s of the
	stream it was made from and of the ADPCM */
	uint32_t data_offset;
	uint32_t source_crc;
	uint32_t adpcm_crc;
	/* (for the logs and the background) the permutation, and whether it
	did not decode (not tried again) */
	struct sound_permutation const *permutation;
	boolean failed;
};

enum sound_cache_state
{
	_sound_cache_none,
	_sound_cache_opening,
	_sound_cache_open,
	_sound_cache_failed
};

/* a decoding (or a read from the sound cache file) under way */
struct decoding
{
	struct waiting_sound *sound;
	/* the block, while it is the decoding's (NULL once deleted) */
	byte *destination;
	/* writing the ADPCM to the sound cache file at data_offset, and its
	CRC-32 so far */
	boolean writing;
	uint32_t data_offset;
	uint32_t adpcm_crc;
	/* the stream's CRC-32, of its first source_crc_bytes */
	uint32_t source_crc;
	uint32_t source_crc_bytes;
	/* the background's (stops for any permutation the sound cache asks for) */
	boolean background;
	/* stopped by decoding_continues (not a failure of the stream) */
	boolean interrupted;
};

/* ---------- globals */

void platform_log(const char *format, ...);
/* (every platform's microsecond clock) */
unsigned long long vita_host_time_us(void);
void *halo_custom_edition_memory_alloc(unsigned long bytes);
void halo_custom_edition_memory_free(void *address);
/* (port_config.c: audio.enabled, false with HALO_NO_AUDIO and on a
dedicated server) */
int config_boolean(char const *name);
#ifdef HALO_VITA
/* (port/vita/host/vita_fourth_core.c, vita_main.c) */
int vita_host_fourth_core_join(const char *role, int level);
void vita_host_thread_priority_set(int priority);
#endif

static struct
{
	pthread_mutex_t lock;
	halo_cond_t wake;
	halo_cond_t done;
	boolean thread_started;
	boolean thread_failed;

	struct waiting_sound waiting[MAXIMUM_WAITING_SOUNDS];
	long waiting_count;

	/* the decoding being done, whether its block went, and whether the map
	is going */
	boolean running;
	boolean *running_loaded;
	volatile boolean running_cancelled;
	volatile boolean stopping;

	/* the memory block (MEMORY_BLOCK_BYTES, and the table's) */
	byte *memory;

	/* the chains (custom_edition_sounds_map_loaded), in a memory block of
	their own: the stream decoders' working memory (OGG_SOUND_WORKING_BYTES
	each), then the permutations of the pitch ranges given chains (their
	own and their links), then each one's first frame in its sound
	(CHAIN_NOT_A_LINK: not a link), its index in its chain and the chain's
	links */
	byte *chain_memory;
	struct sound_permutation *chain_permutations;
	uint32_t *chain_first_frames;
	short *chain_link_indices;
	short *chain_link_counts;
	long chain_permutation_count;
	byte *stream_working[STREAM_DECODERS];
	unsigned long stream_working_used[STREAM_DECODERS];
	long stream_decoder_count;
	unsigned long stream_uses;
	/* (links made Xbox ADPCM since the map came, and of them those whose
	stream decoding was taken up where the link before left it) */
	unsigned long link_decoded_count;
	unsigned long link_resumed_count;

	/* the sound cache file: its entries (sorted as in the file), the
	background's order (shortest first) and how far it got */
	enum sound_cache_state cache_state;
	struct sound_cache_entry *entries;
	uint16_t *background_order;
	long entry_count;
	long background_cursor;
	uint32_t list_crc;
	struct custom_edition_sound_cache_source source;
	char path[SOUND_CACHE_PATH_BYTES];
	char directory[SOUND_CACHE_PATH_BYTES];
	HANDLE file;
	boolean writable;
	/* where the table ends, the ADPCM ends, and the most the file may hold */
	uint32_t data_start;
	uint32_t data_end;
	uint32_t file_limit;
	/* the map's files, for the thread's reads */
	struct custom_edition_cache_reader reader;
	boolean reader_tried;

	/* (decodings since the map came, and their time, for the log: Ogg
	Vorbis and 16-bit PCM) */
	unsigned long decoded_count;
	unsigned long long decoded_microseconds;
	unsigned long encoded_count;
	unsigned long long encoded_microseconds;
	/* (the sound cache file's: read from it, written to it, filled in the
	background, held when it opened, and its entries refused) */
	unsigned long cache_reads;
	unsigned long long cache_read_microseconds;
	unsigned long long cache_read_bytes;
	unsigned long cache_writes;
	unsigned long long cache_written_bytes;
	unsigned long background_count;
	unsigned long long background_microseconds;
	unsigned long held_at_open;
	unsigned long entries_refused;
} custom_edition_sounds_globals = { PTHREAD_MUTEX_INITIALIZER, HALO_COND_INITIALIZER, HALO_COND_INITIALIZER };

static boolean failure_reported[MAXIMUM_WAITING_SOUNDS];
/* (the decoding thread's: a piece of 16-bit PCM, or of a stream; and the
ADPCM on its way to the block and the file) */
static byte pcm_piece[PCM_READ_BYTES];
static byte staging[STAGING_BYTES];

/* ---------- private code */

/* the memory block, taken the first time it is needed; NULL when there is
no room (with the lock held) */
static byte *memory_block(void)
{
	if (!custom_edition_sounds_globals.memory)
		custom_edition_sounds_globals.memory = (byte *)halo_custom_edition_memory_alloc(MEMORY_BLOCK_BYTES);
	return custom_edition_sounds_globals.memory;
}

static int trace_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
		enabled = getenv("HALO_OGG_TRACE") && atoi(getenv("HALO_OGG_TRACE"));
	return enabled;
}

/* a setting of 0 or more (`fallback` without one) */
static long setting_get(char const *name, long fallback)
{
	char const *value = getenv(name);

	return value && *value && atol(value) >= 0 ? atol(value) : fallback;
}

/* (for the log: the game's builds have no libm of their own) */
static double log_square_root(double value)
{
	double root = value > 1.0 ? value : 1.0;
	int step;

	if (value <= 0.0)
		return 0.0;
	for (step = 0; step < 64; step++)
		root = 0.5 * (root + value / root);
	return root;
}

static uint32_t read_u32_le(byte const *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void write_u32_le(byte *bytes, uint32_t value)
{
	bytes[0] = (byte)(value & 0xFF);
	bytes[1] = (byte)((value >> 8) & 0xFF);
	bytes[2] = (byte)((value >> 16) & 0xFF);
	bytes[3] = (byte)(value >> 24);
}

/* how the sound cache loads a permutation: read (Xbox ADPCM), or made Xbox
ADPCM here */
static enum transcoding permutation_transcoding(
	struct sound_permutation const *permutation)
{
	if (!custom_edition_cache_tags_loaded())
		return _transcoding_none;
	if (permutation->compression == SOUND_PERMUTATION_COMPRESSION_OGG_VORBIS)
		return _transcoding_ogg_vorbis;
	if (permutation->compression == SOUND_PERMUTATION_COMPRESSION_PCM)
		return _transcoding_pcm;
	if (permutation->compression == CUSTOM_EDITION_PERMUTATION_PCM_HALVED)
		return _transcoding_pcm_halved;
	if (permutation->compression == CUSTOM_EDITION_PERMUTATION_XBOX_ADPCM_HALVED)
		return _transcoding_adpcm_halved;
	return _transcoding_none;
}

/* the sound a permutation belongs to (its runtime tag index, which the
tag check made the sound's own: tag_schema_effects.c, sound_check) */
static struct sound_definition *permutation_definition(
	struct sound_permutation const *permutation)
{
	return (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, (long)permutation->unknown3);
}

static int definition_channels(struct sound_definition const *definition)
{
	return definition->encoding == 1 ? 2 : 1;
}

static long definition_rate(struct sound_definition const *definition)
{
	return definition->sample_rate == 1 ? 44100 : 22050;
}

/* the permutation's place in the chains' table, or NONE */
static long chain_index(
	struct sound_permutation const *permutation)
{
	uintptr_t first = (uintptr_t)custom_edition_sounds_globals.chain_permutations;
	uintptr_t address = (uintptr_t)permutation;

	if (!first || address < first ||
		address >= first + (uintptr_t)custom_edition_sounds_globals.chain_permutation_count * sizeof(struct sound_permutation))
	{
		return NONE;
	}
	return (long)((address - first) / sizeof(struct sound_permutation));
}

/* whether the permutation is a link of a chain (its first included), and
its first frame in its sound */
static boolean chain_link(
	struct sound_permutation const *permutation,
	uint32_t *first_frame)
{
	long index = chain_index(permutation);

	if (index == NONE || custom_edition_sounds_globals.chain_first_frames[index] == CHAIN_NOT_A_LINK)
		return FALSE;
	if (first_frame)
		*first_frame = custom_edition_sounds_globals.chain_first_frames[index];
	return TRUE;
}

/* the Xbox ADPCM bytes the whole of a permutation is made (for a long one
more than the sound cache gives a permutation); 0 when that is not known
(an Ogg Vorbis permutation not measured) or it is muted (16-bit PCM not of
whole frames: the loader's buffer size 0) */
static uint32_t permutation_whole_bytes(
	struct sound_permutation const *permutation,
	struct sound_definition const *definition)
{
	enum transcoding transcoding = permutation_transcoding(permutation);
	int channels = definition_channels(definition);
	uint32_t size = permutation->samples.size > 0 ? (uint32_t)permutation->samples.size : 0;
	uint32_t frames;

	if (transcoding == _transcoding_ogg_vorbis)
		return permutation->sample_buffer_size == SOUND_BUFFER_UNPLAYABLE ? 0 : (uint32_t)permutation->sample_buffer_size;
	if (transcoding == _transcoding_none || !permutation->sample_buffer_size || !size)
		return 0;
	if (transcoding == _transcoding_adpcm_halved)
	{
		/* (64 frames a block, halved: 32) */
		frames = size / (XBOX_ADPCM_BLOCK_BYTES * (uint32_t)channels) * (XBOX_ADPCM_BLOCK_SAMPLES / 2);
	}
	else
	{
		frames = size / (2 * (uint32_t)channels);
		if (transcoding == _transcoding_pcm_halved)
			frames = frames / 2 + frames % 2;
	}
	return xbox_adpcm_bytes(frames, channels);
}

/* (debug) HALO_OGG_DECODE_SLOWDOWN=<n>: the decoding takes n times as
long as it does here, the thread sleeping after each piece of the stream
for n - 1 times the time the piece took - the Vita's fourth core decodes
some 30-60 times slower than a desktop's, shared with the cache file
thread, the map reader and the mixer - to see in the harness what waits
for a decoding */
static unsigned long long decode_slowdown_piece_started;

static void decode_slowdown_pace(
	void)
{
	static long factor = -1;
	unsigned long long piece_started = decode_slowdown_piece_started;
	unsigned long long now;

	if (factor < 0)
		factor = setting_get("HALO_OGG_DECODE_SLOWDOWN", 0);
	if (factor <= 1)
		return;
	now = vita_host_time_us();
	if (piece_started && now > piece_started && now - piece_started < 10000000ull)
	{
		unsigned long long sleep_us = (now - piece_started) * (unsigned long long)(factor - 1);

		Sleep((DWORD)(sleep_us / 1000 + 1));
	}
	decode_slowdown_piece_started = vita_host_time_us();
}

/* whether the decoding goes on: not when the map is going, nor the
background's once the sound cache asks for a permutation, nor one whose
block went unless it writes the sound cache file while nothing else
waits */
static boolean decoding_continues(
	struct decoding const *decoding)
{
	long waiting = __atomic_load_n(&custom_edition_sounds_globals.waiting_count, __ATOMIC_RELAXED);

	if (custom_edition_sounds_globals.stopping)
		return FALSE;
	if (decoding->background)
		return waiting == 0;
	if (decoding->destination && !custom_edition_sounds_globals.running_cancelled)
		return TRUE;
	return decoding->writing && waiting == 0;
}

/* `size` bytes of the map's files at `offset` (the combined space), through
the thread's own reader, else custom_edition_cache_read's */
static boolean map_read(unsigned long offset, uint32_t size, void *buffer)
{
	if (custom_edition_sounds_globals.reader.opened)
		return custom_edition_cache_reader_read(&custom_edition_sounds_globals.reader, offset, size, buffer);
	return custom_edition_cache_read(NONE, (long)offset, (long)size, buffer);
}

/* the stream's bytes, read from the map's files for the decoder (their
CRC-32 taken on the way when they follow what was read); the decoding stops
(a failed read) once it is not to go on */
static int stream_read(void *context, uint32_t offset, uint32_t size, void *buffer)
{
	struct decoding *decoding = (struct decoding *)context;
	struct waiting_sound const *sound = decoding->sound;

	decode_slowdown_pace();
	if (!decoding_continues(decoding))
	{
		decoding->interrupted = TRUE;
		return 0;
	}
	if (offset > sound->file_bytes || size > sound->file_bytes - offset ||
		!map_read(sound->file_offset + offset, size, buffer))
	{
		return 0;
	}
	if (offset == decoding->source_crc_bytes)
	{
		decoding->source_crc = cache_file_crc32_update(decoding->source_crc, buffer, size);
		decoding->source_crc_bytes += size;
	}
	return 1;
}

/* the stream's CRC-32 finished: the rest of it read (a decoding cut off
before the stream's end did not read it all); FALSE when it cannot be */
static boolean stream_crc_finish(struct decoding *decoding)
{
	struct waiting_sound const *sound = decoding->sound;

	while (decoding->source_crc_bytes < sound->file_bytes)
	{
		uint32_t size = sound->file_bytes - decoding->source_crc_bytes;

		if (size > PCM_READ_BYTES)
			size = PCM_READ_BYTES;
		if (!decoding_continues(decoding) || !map_read(sound->file_offset + decoding->source_crc_bytes, size, pcm_piece))
			return FALSE;
		decoding->source_crc = cache_file_crc32_update(decoding->source_crc, pcm_piece, size);
		decoding->source_crc_bytes += size;
	}
	return TRUE;
}

/* ---------- the sound cache file */

/* a positioned read or write of the sound cache file */
static boolean sound_cache_file_access(boolean write, uint32_t offset, void *buffer, uint32_t size)
{
	OVERLAPPED position;
	DWORD done = 0;
	BOOL result;

	if (!size)
		return TRUE;
	memset(&position, 0, sizeof(position));
	position.Offset = offset;
	result = write ?
		WriteFile(custom_edition_sounds_globals.file, buffer, size, &done, &position) :
		ReadFile(custom_edition_sounds_globals.file, buffer, size, &done, &position);
	return result && done == size;
}

/* a write that fails (the memory card full) ends the file's writes for the
map, said once */
static void sound_cache_file_write_failed(void)
{
	if (custom_edition_sounds_globals.writable)
	{
		custom_edition_sounds_globals.writable = FALSE;
		platform_log("sound cache: cannot write %s (the memory card full?); nothing more is kept for this map",
			custom_edition_sounds_globals.path);
	}
}

static void entry_encode(long index, byte *bytes)
{
	struct sound_cache_entry const *entry = &custom_edition_sounds_globals.entries[index];

	write_u32_le(bytes + SOUND_CACHE_ENTRY_SOURCE_OFFSET, entry->source_offset);
	write_u32_le(bytes + SOUND_CACHE_ENTRY_SOURCE_BYTES, entry->source_bytes);
	write_u32_le(bytes + SOUND_CACHE_ENTRY_ADPCM_BYTES, entry->adpcm_bytes);
	write_u32_le(bytes + SOUND_CACHE_ENTRY_FORMAT, entry->format);
	write_u32_le(bytes + SOUND_CACHE_ENTRY_DATA_OFFSET, entry->data_offset);
	write_u32_le(bytes + SOUND_CACHE_ENTRY_SOURCE_CRC, entry->data_offset ? entry->source_crc : 0);
	write_u32_le(bytes + SOUND_CACHE_ENTRY_ADPCM_CRC, entry->data_offset ? entry->adpcm_crc : 0);
	write_u32_le(bytes + SOUND_CACHE_ENTRY_CRC,
		cache_file_crc32_update(0, bytes, SOUND_CACHE_ENTRY_CRC) ^ (uint32_t)index ^ SOUND_CACHE_ENTRY_SALT);
}

/* the entry `index` written as it is in memory (an empty one with no
ADPCM) */
static void entry_write(long index)
{
	byte bytes[SOUND_CACHE_FILE_ENTRY_BYTES];

	if (!custom_edition_sounds_globals.writable)
		return;
	if (!custom_edition_sounds_globals.entries[index].data_offset)
		memset(bytes, 0, sizeof(bytes));
	else
		entry_encode(index, bytes);
	if (!sound_cache_file_access(TRUE, SOUND_CACHE_FILE_HEADER_BYTES + (uint32_t)index * SOUND_CACHE_FILE_ENTRY_BYTES,
		bytes, sizeof(bytes)))
	{
		sound_cache_file_write_failed();
	}
}

/* the entry `index`, found wrong as it was read (`why`), emptied, on disk
too */
static void entry_refuse(long index, char const *why)
{
	struct sound_cache_entry *entry = &custom_edition_sounds_globals.entries[index];

	if (custom_edition_sounds_globals.entries_refused++ < 8)
	{
		platform_log("sound cache: entry %ld (%.32s) refused: %s; decoded again", index,
			entry->permutation ? entry->permutation->name : "?", why);
	}
	entry->data_offset = 0;
	entry_write(index);
}

/* (the file's header for this map) */
static void header_encode(byte *header)
{
	struct custom_edition_sound_cache_source const *source = &custom_edition_sounds_globals.source;

	memset(header, 0, SOUND_CACHE_FILE_HEADER_BYTES);
	memcpy(header, SOUND_CACHE_FILE_MAGIC, 8);
	write_u32_le(header + SOUND_CACHE_HEADER_VERSION_OFFSET, SOUND_CACHE_FILE_VERSION);
	write_u32_le(header + SOUND_CACHE_HEADER_HEADER_BYTES_OFFSET, SOUND_CACHE_FILE_HEADER_BYTES);
	write_u32_le(header + SOUND_CACHE_HEADER_ENTRY_BYTES_OFFSET, SOUND_CACHE_FILE_ENTRY_BYTES);
	write_u32_le(header + SOUND_CACHE_HEADER_ENTRY_COUNT_OFFSET, (uint32_t)custom_edition_sounds_globals.entry_count);
	write_u32_le(header + SOUND_CACHE_HEADER_MAP_BYTES_OFFSET, (uint32_t)source->map_bytes);
	write_u32_le(header + SOUND_CACHE_HEADER_MAP_HEADER_CRC_OFFSET, (uint32_t)source->map_header_crc);
	write_u32_le(header + SOUND_CACHE_HEADER_SOUNDS_BYTES_OFFSET, (uint32_t)source->sounds_bytes);
	write_u32_le(header + SOUND_CACHE_HEADER_LIST_CRC_OFFSET, custom_edition_sounds_globals.list_crc);
	memcpy(header + SOUND_CACHE_HEADER_NAME_OFFSET, source->name, CUSTOM_EDITION_SOUND_CACHE_NAME_BYTES);
	write_u32_le(header + SOUND_CACHE_HEADER_CRC_OFFSET, cache_file_crc32_update(0, header, SOUND_CACHE_HEADER_CRC_OFFSET));
}

/* a new file: this map's header and an empty table, and nothing after it */
static boolean sound_cache_file_reset(void)
{
	byte header[SOUND_CACHE_FILE_HEADER_BYTES];
	uint32_t offset;
	long index;

	header_encode(header);
	memset(staging, 0, sizeof(staging));
	for (offset = SOUND_CACHE_FILE_HEADER_BYTES; offset < custom_edition_sounds_globals.data_start; )
	{
		uint32_t size = custom_edition_sounds_globals.data_start - offset;

		if (size > sizeof(staging))
			size = sizeof(staging);
		if (!sound_cache_file_access(TRUE, offset, staging, size))
			return FALSE;
		offset += size;
	}
	/* (the header last: a file cut short before it is not taken) */
	if (!sound_cache_file_access(TRUE, 0, header, sizeof(header)) ||
		SetFilePointer(custom_edition_sounds_globals.file, (LONG)custom_edition_sounds_globals.data_start, NULL, FILE_BEGIN) ==
			INVALID_SET_FILE_POINTER ||
		!SetEndOfFile(custom_edition_sounds_globals.file))
	{
		return FALSE;
	}
	for (index = 0; index < custom_edition_sounds_globals.entry_count; index++)
		custom_edition_sounds_globals.entries[index].data_offset = 0;
	custom_edition_sounds_globals.data_end = custom_edition_sounds_globals.data_start;
	return TRUE;
}

/* why the file's header is not this map's (NULL when it is) */
static char const *header_check(byte const *header, uint32_t file_bytes)
{
	byte expected[SOUND_CACHE_FILE_HEADER_BYTES];

	if (file_bytes < SOUND_CACHE_FILE_HEADER_BYTES)
		return file_bytes ? "cut short" : "new";
	if (memcmp(header, SOUND_CACHE_FILE_MAGIC, 8))
		return "not a sound cache file";
	if (read_u32_le(header + SOUND_CACHE_HEADER_CRC_OFFSET) != cache_file_crc32_update(0, header, SOUND_CACHE_HEADER_CRC_OFFSET))
		return "its header damaged";
	if (read_u32_le(header + SOUND_CACHE_HEADER_VERSION_OFFSET) != SOUND_CACHE_FILE_VERSION)
		return "another version's";
	header_encode(expected);
	if (memcmp(header, expected, SOUND_CACHE_FILE_HEADER_BYTES))
		return "another map's, or another copy's";
	if (file_bytes < custom_edition_sounds_globals.data_start)
		return "its table cut short";
	return NULL;
}

/* the entries the file holds, read from its table: each must check, be its
permutation's and have its ADPCM within the file; the others are empty */
static boolean sound_cache_file_table_read(uint32_t file_bytes)
{
	long index = 0;

	custom_edition_sounds_globals.data_end = custom_edition_sounds_globals.data_start;
	while (index < custom_edition_sounds_globals.entry_count)
	{
		long count = custom_edition_sounds_globals.entry_count - index;
		long piece;

		if (count > (long)(sizeof(staging) / SOUND_CACHE_FILE_ENTRY_BYTES))
			count = (long)(sizeof(staging) / SOUND_CACHE_FILE_ENTRY_BYTES);
		if (!sound_cache_file_access(FALSE, SOUND_CACHE_FILE_HEADER_BYTES + (uint32_t)index * SOUND_CACHE_FILE_ENTRY_BYTES,
			staging, (uint32_t)count * SOUND_CACHE_FILE_ENTRY_BYTES))
		{
			return FALSE;
		}
		for (piece = 0; piece < count; piece++, index++)
		{
			byte const *bytes = staging + piece * SOUND_CACHE_FILE_ENTRY_BYTES;
			struct sound_cache_entry *entry = &custom_edition_sounds_globals.entries[index];
			uint32_t data_offset = read_u32_le(bytes + SOUND_CACHE_ENTRY_DATA_OFFSET);
			byte expected[SOUND_CACHE_FILE_ENTRY_BYTES];
			boolean empty = TRUE;
			int byte_index;

			entry->data_offset = 0;
			for (byte_index = 0; byte_index < SOUND_CACHE_FILE_ENTRY_BYTES; byte_index++)
				empty = empty && !bytes[byte_index];
			if (empty)
				continue;
			/* (what this permutation's entry would be with that ADPCM) */
			entry->data_offset = data_offset;
			entry->source_crc = read_u32_le(bytes + SOUND_CACHE_ENTRY_SOURCE_CRC);
			entry->adpcm_crc = read_u32_le(bytes + SOUND_CACHE_ENTRY_ADPCM_CRC);
			entry_encode(index, expected);
			if (memcmp(bytes, expected, SOUND_CACHE_FILE_ENTRY_BYTES) ||
				data_offset < custom_edition_sounds_globals.data_start || data_offset > file_bytes ||
				entry->adpcm_bytes > file_bytes - data_offset)
			{
				entry->data_offset = 0;
				custom_edition_sounds_globals.entries_refused++;
				continue;
			}
			custom_edition_sounds_globals.held_at_open++;
			if (data_offset + entry->adpcm_bytes > custom_edition_sounds_globals.data_end)
				custom_edition_sounds_globals.data_end = data_offset + entry->adpcm_bytes;
		}
	}
	return TRUE;
}

/* the map's sound cache file opened (the thread's, before any decoding):
read if it is this map's, else made anew */
static void sound_cache_file_open(void)
{
	byte header[SOUND_CACHE_FILE_HEADER_BYTES];
	unsigned long long started = vita_host_time_us();
	char const *why;
	DWORD file_bytes;
	unsigned long long held_bytes = 0;
	long index;

	CreateDirectoryA(custom_edition_sounds_globals.directory, NULL);
	custom_edition_sounds_globals.file = CreateFileA(custom_edition_sounds_globals.path, GENERIC_READ | GENERIC_WRITE,
		0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (custom_edition_sounds_globals.file == INVALID_HANDLE_VALUE)
	{
		platform_log("sound cache: cannot open %s; this map's Ogg Vorbis sounds are decoded each time",
			custom_edition_sounds_globals.path);
		custom_edition_sounds_globals.file = NULL;
		custom_edition_sounds_globals.cache_state = _sound_cache_failed;
		return;
	}
	custom_edition_sounds_globals.writable = TRUE;
	file_bytes = GetFileSize(custom_edition_sounds_globals.file, NULL);
	if (file_bytes == INVALID_FILE_SIZE)
		file_bytes = 0;
	memset(header, 0, sizeof(header));
	why = file_bytes >= SOUND_CACHE_FILE_HEADER_BYTES && !sound_cache_file_access(FALSE, 0, header, sizeof(header)) ?
		"unreadable" : header_check(header, (uint32_t)file_bytes);
	if (!why && !sound_cache_file_table_read((uint32_t)file_bytes))
		why = "its table unreadable";
	if (why)
	{
		custom_edition_sounds_globals.held_at_open = 0;
		if (!sound_cache_file_reset())
		{
			platform_log("sound cache: cannot make %s; this map's Ogg Vorbis sounds are decoded each time",
				custom_edition_sounds_globals.path);
			CloseHandle(custom_edition_sounds_globals.file);
			custom_edition_sounds_globals.file = NULL;
			custom_edition_sounds_globals.writable = FALSE;
			custom_edition_sounds_globals.cache_state = _sound_cache_failed;
			return;
		}
	}
	for (index = 0; index < custom_edition_sounds_globals.entry_count; index++)
	{
		if (custom_edition_sounds_globals.entries[index].data_offset)
			held_bytes += custom_edition_sounds_globals.entries[index].adpcm_bytes;
	}
	platform_log("sound cache: %s %s: %lu of %ld Ogg Vorbis permutations held (%llu KB)%s%s, %lu entries refused, "
		"%.1f ms",
		why ? "made" : "opened", custom_edition_sounds_globals.path, custom_edition_sounds_globals.held_at_open,
		custom_edition_sounds_globals.entry_count, held_bytes / 1024, why ? "; the old one: " : "", why ? why : "",
		custom_edition_sounds_globals.entries_refused, (double)(vita_host_time_us() - started) / 1000.0);
	custom_edition_sounds_globals.cache_state = _sound_cache_open;
}

static void sound_cache_file_close(void)
{
	if (custom_edition_sounds_globals.file)
	{
		CloseHandle(custom_edition_sounds_globals.file);
		custom_edition_sounds_globals.file = NULL;
	}
	custom_edition_sounds_globals.writable = FALSE;
}

/* the entry of the permutation `sound` would be: (with the lock held) NONE
when the file has none for it */
static int entry_compare(
	uint32_t source_offset,
	uint32_t source_bytes,
	uint32_t adpcm_bytes,
	uint32_t format,
	struct sound_cache_entry const *entry)
{
	if (source_offset != entry->source_offset)
		return source_offset < entry->source_offset ? -1 : 1;
	if (source_bytes != entry->source_bytes)
		return source_bytes < entry->source_bytes ? -1 : 1;
	if (adpcm_bytes != entry->adpcm_bytes)
		return adpcm_bytes < entry->adpcm_bytes ? -1 : 1;
	if (format != entry->format)
		return format < entry->format ? -1 : 1;
	return 0;
}

static long entry_find(struct waiting_sound const *sound)
{
	long low = 0;
	long high = custom_edition_sounds_globals.entry_count;

	if (!custom_edition_sounds_globals.entries || sound->transcoding != _transcoding_ogg_vorbis)
		return NONE;
	while (low < high)
	{
		long middle = low + (high - low) / 2;
		int order = entry_compare((uint32_t)sound->file_offset, sound->file_bytes, sound->destination_bytes,
			(uint32_t)sound->channels | (uint32_t)sound->rate << 8, &custom_edition_sounds_globals.entries[middle]);

		if (!order)
			return middle;
		if (order < 0)
			high = middle;
		else
			low = middle + 1;
	}
	return NONE;
}

/* (qsort) */
static int entry_sort_compare(void const *a, void const *b)
{
	struct sound_cache_entry const *entry = (struct sound_cache_entry const *)a;

	return entry_compare(entry->source_offset, entry->source_bytes, entry->adpcm_bytes, entry->format,
		(struct sound_cache_entry const *)b);
}

static int background_sort_compare(void const *a, void const *b)
{
	struct sound_cache_entry const *entries = custom_edition_sounds_globals.entries;
	uint16_t first = *(uint16_t const *)a;
	uint16_t second = *(uint16_t const *)b;

	if (entries[first].adpcm_bytes != entries[second].adpcm_bytes)
		return entries[first].adpcm_bytes < entries[second].adpcm_bytes ? -1 : 1;
	return first < second ? -1 : first > second ? 1 : 0;
}

/* ---------- decoding */

/* the logs of a decoding (or encoding) of `sound`, which took `took`
microseconds */
static void sound_decode_report(
	struct waiting_sound const *sound,
	struct ogg_sound_result const *result,
	unsigned long long took,
	struct decoding const *decoding)
{
	boolean background = decoding->background;

	boolean pcm = sound->transcoding == _transcoding_pcm || sound->transcoding == _transcoding_pcm_halved;
	boolean adpcm = sound->transcoding == _transcoding_adpcm_halved;

	if (trace_enabled())
	{
		double samples = (double)result->frames_decoded * sound->channels;
		char link[96];

		/* (a link: which, from which frame, and how its frames were come to;
		after the ADPCM's place, which the tests read. The background's lines
		are not "ogg sound: ...", which run_ce_ogg_sound_test.sh takes for the
		ones the mixer plays) */
		link[0] = '\0';
		if (sound->link)
		{
			snprintf(link, sizeof(link), ", link %ld of %ld from frame %lu, %s %lu frames", sound->link_index + 1,
				sound->link_count, (unsigned long)sound->first_frame,
				result->resumed ? "resumed skipping" : "from the start skipping", (unsigned long)result->frames_skipped);
		}
		platform_log("%s sound%s: %.32s of %s: %s, %lu frames %d ch %ld Hz%s (+%lu silent%s), rms %.0f peak %d, "
			"%.2f ms, %lu KB working, adpcm %lu bytes at %p%s",
			pcm ? "pcm" : adpcm ? "adpcm" : "ogg", background ? " (background)" : "", sound->permutation->name,
			tag_get_name((long)sound->permutation->unknown3),
			ogg_sound_status_describe(result->status), (unsigned long)result->frames_decoded,
			result->stream_channels, result->stream_rate, result->halved ? " halved" : "",
			(unsigned long)result->frames_padded, result->truncated ? (sound->link ? ", more follows" : ", cut off") : "",
			samples > 0 ? log_square_root(result->sum_of_squares / samples) : 0.0, result->peak,
			(double)took / 1000.0, (unsigned long)(result->working_peak_bytes / 1024),
			(unsigned long)sound->destination_bytes, sound->destination, link);
	}
	if (result->status != _ogg_sound_ok && !result->stopped && !decoding->interrupted &&
		!custom_edition_sounds_globals.stopping)
	{
		long index = ((long)sound->permutation->unknown3 & 0xFFFF) % MAXIMUM_WAITING_SOUNDS;

		if (!failure_reported[index])
		{
			failure_reported[index] = TRUE;
			/* (platform_log: error() is the game thread's) */
			platform_log("custom edition: %s sound of %s (%.32s) does not decode: %s; it is silent",
				pcm ? "a 16-bit PCM" : adpcm ? "a 44 kHz Xbox ADPCM" : "an Ogg Vorbis", tag_get_name((long)sound->permutation->unknown3),
				sound->permutation->name, ogg_sound_status_describe(result->status));
		}
	}
	return;
}

/* (the encoder's sink) a piece of the ADPCM at `offset`: into the block,
under the lock and only while the block is the decoding's, and into the
sound cache file; 0 when the decoding is not to go on */
static int decoding_sink(void *context, uint8_t const *bytes, uint32_t size, uint32_t offset)
{
	struct decoding *decoding = (struct decoding *)context;
	struct waiting_sound const *sound = decoding->sound;

	if (decoding->destination)
	{
		pthread_mutex_lock(&custom_edition_sounds_globals.lock);
		if (!custom_edition_sounds_globals.running_cancelled &&
			offset <= sound->destination_bytes && size <= sound->destination_bytes - offset)
		{
			memcpy(decoding->destination + offset, bytes, size);
		}
		else
		{
			decoding->destination = NULL;
		}
		pthread_mutex_unlock(&custom_edition_sounds_globals.lock);
	}
	if (decoding->writing)
	{
		decoding->adpcm_crc = cache_file_crc32_update(decoding->adpcm_crc, bytes, size);
		if (!custom_edition_sounds_globals.writable ||
			!sound_cache_file_access(TRUE, decoding->data_offset + offset, (void *)bytes, size))
		{
			sound_cache_file_write_failed();
			decoding->writing = FALSE;
		}
	}
	if (!decoding_continues(decoding))
	{
		decoding->interrupted = TRUE;
		return 0;
	}
	return 1;
}

/* the decoding's ADPCM kept in the sound cache file, its entry written
after it */
static void decoding_keep(struct decoding *decoding)
{
	struct waiting_sound const *sound = decoding->sound;
	struct sound_cache_entry *entry = &custom_edition_sounds_globals.entries[sound->entry];

	if (!decoding->writing || !stream_crc_finish(decoding) || !custom_edition_sounds_globals.writable)
		return;
	entry->data_offset = decoding->data_offset;
	entry->source_crc = decoding->source_crc;
	entry->adpcm_crc = decoding->adpcm_crc;
	custom_edition_sounds_globals.data_end = decoding->data_offset + sound->destination_bytes;
	entry_write(sound->entry);
	if (!custom_edition_sounds_globals.writable)
	{
		entry->data_offset = 0;
		return;
	}
	custom_edition_sounds_globals.cache_writes++;
	custom_edition_sounds_globals.cache_written_bytes += sound->destination_bytes;
}

/* whether the decoding writes the sound cache file: an Ogg Vorbis
permutation with an empty entry, which the file has room for */
static boolean decoding_writes(struct waiting_sound const *sound)
{
	uint32_t data_end = custom_edition_sounds_globals.data_end;

	return sound->entry != NONE && custom_edition_sounds_globals.cache_state == _sound_cache_open &&
		custom_edition_sounds_globals.writable &&
		!custom_edition_sounds_globals.entries[sound->entry].data_offset &&
		data_end <= custom_edition_sounds_globals.file_limit &&
		sound->destination_bytes <= custom_edition_sounds_globals.file_limit - data_end;
}

/* the permutation's ADPCM read from the sound cache file into its block, a
piece at a time, once its stream is the one its entry was made from; FALSE
when the entry is not usable (emptied: the permutation is decoded) */
static boolean sound_read_cached(struct decoding *decoding)
{
	struct waiting_sound const *sound = decoding->sound;
	struct sound_cache_entry const *entry;
	unsigned long long started = vita_host_time_us();
	uint32_t crc = 0;
	uint32_t offset;

	if (sound->entry == NONE || custom_edition_sounds_globals.cache_state != _sound_cache_open ||
		!custom_edition_sounds_globals.entries[sound->entry].data_offset)
	{
		return FALSE;
	}
	entry = &custom_edition_sounds_globals.entries[sound->entry];
	if (!stream_crc_finish(decoding))
		return decoding_continues(decoding) ? FALSE : TRUE;
	if (decoding->source_crc != entry->source_crc)
	{
		entry_refuse(sound->entry, "the map's stream is not the one it was made from");
		return FALSE;
	}
	for (offset = 0; offset < sound->destination_bytes; )
	{
		uint32_t size = sound->destination_bytes - offset;

		if (size > sizeof(staging))
			size = sizeof(staging);
		if (!sound_cache_file_access(FALSE, entry->data_offset + offset, staging, size))
		{
			entry_refuse(sound->entry, "unreadable");
			return FALSE;
		}
		crc = cache_file_crc32_update(crc, staging, size);
		/* (a block that went, or the map going: nothing more to read) */
		if (!decoding_sink(decoding, staging, size, offset))
			return TRUE;
		offset += size;
	}
	if (crc != entry->adpcm_crc)
	{
		entry_refuse(sound->entry, "its ADPCM damaged");
		return FALSE;
	}
	custom_edition_sounds_globals.cache_reads++;
	custom_edition_sounds_globals.cache_read_microseconds += vita_host_time_us() - started;
	custom_edition_sounds_globals.cache_read_bytes += sound->destination_bytes;
	if (trace_enabled())
	{
		platform_log("ogg sound (sound cache): %.32s of %s: %lu bytes read in %.2f ms, adpcm at %p",
			sound->permutation->name, tag_get_name((long)sound->permutation->unknown3),
			(unsigned long)sound->destination_bytes, (double)(vita_host_time_us() - started) / 1000.0,
			sound->destination);
	}
	return TRUE;
}

/* the frames of a piece of 16-bit PCM (whole frames) or of Xbox ADPCM
(whole blocks), taken at the output's rate */
static void piece_frames(struct waiting_sound const *sound, struct xbox_adpcm_rate *rate, uint32_t size)
{
	int channels = sound->channels;
	int values[XBOX_ADPCM_MAXIMUM_CHANNELS];
	uint32_t offset;
	int channel;

	if (sound->transcoding == _transcoding_adpcm_halved)
	{
		uint32_t block_bytes = XBOX_ADPCM_BLOCK_BYTES * (uint32_t)channels;

		for (offset = 0; offset + block_bytes <= size && !rate->encoder->full; offset += block_bytes)
		{
			short frames[XBOX_ADPCM_BLOCK_SAMPLES * XBOX_ADPCM_MAXIMUM_CHANNELS];
			int frame;

			xbox_adpcm_block_decode(pcm_piece + offset, channels, frames);
			for (frame = 0; frame < XBOX_ADPCM_BLOCK_SAMPLES; frame++)
			{
				for (channel = 0; channel < channels; channel++)
					values[channel] = frames[frame * channels + channel];
				xbox_adpcm_rate_frame(rate, values);
			}
		}
	}
	else
	{
		for (offset = 0; offset + 2 * (uint32_t)channels <= size && !rate->encoder->full; offset += 2 * (uint32_t)channels)
		{
			for (channel = 0; channel < channels; channel++)
				values[channel] = (int16_t)(uint16_t)(pcm_piece[offset + 2 * channel] | pcm_piece[offset + 2 * channel + 1] << 8);
			xbox_adpcm_rate_frame(rate, values);
		}
	}
	/* (the block full before the piece's end: the rest left out) */
	if (offset < size && rate->encoder->full)
		rate->encoder->truncated = 1;
}

/* a 16-bit PCM permutation encoded into its block, read in pieces of
whole frames (a halved one's, or Xbox ADPCM's, of whole blocks, decoded and
taken at half their rate): what the samples do not fill is silence, and
what does not fit is left out (the block is the size the loader worked out
from them). A link of a chain is read from the frame (or block) its first
frame is made from, the halving's frame before it included, and the frames
made before its first are left out: the same frames as a whole decoding's
there */
static void pcm_encode(struct decoding *decoding)
{
	struct waiting_sound *sound = decoding->sound;
	struct xbox_adpcm_encoder encoder;
	struct xbox_adpcm_rate rate;
	struct ogg_sound_result result;
	unsigned long long started = vita_host_time_us();
	unsigned long long took;
	boolean halved = sound->transcoding != _transcoding_pcm;
	uint32_t unit_bytes = sound->transcoding == _transcoding_adpcm_halved ?
		XBOX_ADPCM_BLOCK_BYTES * (uint32_t)sound->channels : 2 * (uint32_t)sound->channels;
	uint32_t unit_frames = sound->transcoding == _transcoding_adpcm_halved ? XBOX_ADPCM_BLOCK_SAMPLES : 1;
	uint32_t piece_bytes = PCM_READ_BYTES / unit_bytes * unit_bytes;
	uint32_t offset = 0;
	uint32_t skip = 0;
	uint32_t warm = 0;

	memset(&result, 0, sizeof(result));
	result.status = _ogg_sound_ok;
	result.stream_channels = sound->channels;
	result.stream_rate = halved ? 2 * sound->rate : sound->rate;
	result.halved = halved;
	if (sound->first_frame)
	{
		uint64_t start = xbox_adpcm_rate_start(sound->first_frame, halved, unit_frames, &skip, &warm) / unit_frames * unit_bytes;

		offset = start < sound->file_bytes ? (uint32_t)start : sound->file_bytes;
	}
	result.frames_skipped = skip + warm;
	xbox_adpcm_encoder_begin_sink(&encoder, staging, sizeof(staging), sound->destination_bytes, sound->channels,
		decoding_sink, decoding);
	encoder.skip_frames = skip;
	encoder.warm_frames = warm;
	if (!xbox_adpcm_rate_begin(&rate, &encoder, result.stream_rate, sound->rate))
		result.status = _ogg_sound_unsupported_format;
	while (result.status == _ogg_sound_ok && offset < sound->file_bytes && !encoder.full)
	{
		uint32_t size = sound->file_bytes - offset < piece_bytes ? sound->file_bytes - offset : piece_bytes;

		if (!stream_read(decoding, offset, size, pcm_piece))
		{
			result.status = _ogg_sound_read_failed;
			break;
		}
		piece_frames(sound, &rate, size);
		offset += size;
	}
	if (offset < sound->file_bytes)
		encoder.truncated = 1;
	result.frames_skipped -= encoder.skip_frames + encoder.warm_frames;
	xbox_adpcm_rate_finish(&rate);
	result.frames_padded = xbox_adpcm_encoder_finish(&encoder);
	result.frames_decoded = encoder.frames;
	result.stopped = encoder.stopped;
	result.truncated = encoder.truncated && result.status == _ogg_sound_ok;
	result.sum_of_squares = encoder.sum_of_squares;
	result.peak = encoder.peak;
	took = vita_host_time_us() - started;
	custom_edition_sounds_globals.encoded_count++;
	custom_edition_sounds_globals.encoded_microseconds += took;
	sound_decode_report(sound, &result, took, decoding);
	return;
}

/* the stream an Ogg Vorbis link is of, for its decoding kept between
links (ogg_sound_request.stream_key): its place and size */
static uint64_t waiting_stream_key(struct waiting_sound const *sound)
{
	return (uint64_t)sound->file_bytes << 32 | (uint32_t)sound->file_offset;
}

/* the stream decoder for an Ogg Vorbis link: the one whose kept decoding
of its stream goes on at or before its first frame, else the one used
longest ago; NULL without one (the decoding thread's) */
static byte *stream_decoder(struct waiting_sound const *sound)
{
	uint64_t key = waiting_stream_key(sound);
	long chosen = NONE;
	long index;

	for (index = 0; index < custom_edition_sounds_globals.stream_decoder_count; index++)
	{
		uint32_t next_frame;

		if (ogg_sound_kept(custom_edition_sounds_globals.stream_working[index], OGG_SOUND_WORKING_BYTES, key, &next_frame) &&
			next_frame <= sound->first_frame)
		{
			chosen = index;
			break;
		}
		if (chosen == NONE ||
			custom_edition_sounds_globals.stream_working_used[index] < custom_edition_sounds_globals.stream_working_used[chosen])
		{
			chosen = index;
		}
	}
	if (chosen == NONE)
		return NULL;
	custom_edition_sounds_globals.stream_working_used[chosen] = ++custom_edition_sounds_globals.stream_uses;
	return custom_edition_sounds_globals.stream_working[chosen];
}

/* an Ogg Vorbis permutation made Xbox ADPCM, into its block (while it is
the decoding's) and into the sound cache file when its entry is empty: read
from the file when the entry holds it, else decoded */
static void sound_decode(struct decoding *decoding)
{
	struct waiting_sound *sound = decoding->sound;
	struct ogg_sound_request request;
	struct ogg_sound_result result;
	unsigned long long started = vita_host_time_us();
	unsigned long long took;

	if (sound->link)
		custom_edition_sounds_globals.link_decoded_count++;
	/* (HALO_OGG_DECODE_SLOWDOWN: the time from here to the first piece) */
	decode_slowdown_piece_started = started;
	if (sound->transcoding != _transcoding_ogg_vorbis)
	{
		pcm_encode(decoding);
		return;
	}
	if (!decoding->background && sound_read_cached(decoding))
		return;
	/* (decoded: the stream's CRC-32 taken again as it is read) */
	decoding->source_crc = 0;
	decoding->source_crc_bytes = 0;
	decoding->writing = decoding_writes(sound);
	decoding->data_offset = custom_edition_sounds_globals.data_end;
	decoding->adpcm_crc = 0;
	memset(&request, 0, sizeof(request));
	request.read = stream_read;
	request.read_context = decoding;
	request.input_bytes = sound->file_bytes;
	request.output = staging;
	request.output_bytes = sound->destination_bytes;
	request.sink = decoding_sink;
	request.sink_context = decoding;
	request.staging_bytes = sizeof(staging);
	request.channels = sound->channels;
	request.rate = sound->rate;
	request.working = custom_edition_sounds_globals.memory;
	request.working_bytes = OGG_SOUND_WORKING_BYTES;
	/* (a link: its frames from its first; an Ogg Vorbis one's decoding
	kept for the next link, and taken up from the link before where that
	kept it, in a stream decoder: else decoded from the stream's start) */
	request.first_frame = sound->first_frame;
	if (sound->stream_link)
	{
		byte *working = stream_decoder(sound);

		if (working)
		{
			request.working = working;
			request.keep = TRUE;
			request.stream_key = waiting_stream_key(sound);
		}
	}
	if (request.working)
	{
		ogg_sound_transcode(&request, &result);
		/* (HALO_OGG_DECODE_SLOWDOWN: the last piece's decoding) */
		decode_slowdown_pace();
	}
	else
	{
		/* (no working memory: the block is silence) */
		struct xbox_adpcm_encoder encoder;

		decoding->writing = FALSE;
		xbox_adpcm_encoder_begin_sink(&encoder, staging, sizeof(staging), sound->destination_bytes, sound->channels,
			decoding_sink, decoding);
		xbox_adpcm_encoder_finish(&encoder);
		memset(&result, 0, sizeof(result));
		result.status = _ogg_sound_out_of_memory;
	}
	took = vita_host_time_us() - started;
	if (decoding->background)
	{
		custom_edition_sounds_globals.background_count++;
		custom_edition_sounds_globals.background_microseconds += took;
	}
	else
	{
		custom_edition_sounds_globals.decoded_count++;
		custom_edition_sounds_globals.decoded_microseconds += took;
	}
	if (result.resumed)
		custom_edition_sounds_globals.link_resumed_count++;
	/* (kept when it decoded whole; one that does not decode is not tried
	again in the background) */
	if (result.status == _ogg_sound_ok && !result.stopped && !decoding->interrupted)
		decoding_keep(decoding);
	else if (result.status != _ogg_sound_ok && !result.stopped && !decoding->interrupted && sound->entry != NONE &&
		!custom_edition_sounds_globals.stopping)
		custom_edition_sounds_globals.entries[sound->entry].failed = TRUE;
	if (!decoding->background || (result.status == _ogg_sound_ok && !decoding->interrupted))
		sound_decode_report(sound, &result, took, decoding);
	return;
}

/* whether the waiting decoding `index` may be done next: an Ogg Vorbis
link may not while a link of the same stream from an earlier frame waits
(the links of a chain are decoded in order, each taking up the decoding
the one before kept; with the lock held) */
static boolean waiting_ready(long index)
{
	struct waiting_sound const *sound = &custom_edition_sounds_globals.waiting[index];
	long other;

	if (!sound->stream_link)
		return TRUE;
	for (other = 0; other < custom_edition_sounds_globals.waiting_count; other++)
	{
		struct waiting_sound const *earlier = &custom_edition_sounds_globals.waiting[other];

		if (earlier->stream_link && earlier->first_frame < sound->first_frame &&
			earlier->file_offset == sound->file_offset && earlier->file_bytes == sound->file_bytes)
		{
			return FALSE;
		}
	}
	return TRUE;
}

/* (with the lock held) the background's next permutation: the shortest
whose entry is empty and that has not failed */
static boolean background_next(struct waiting_sound *sound)
{
	static long enabled = -1;

	if (enabled < 0)
		enabled = setting_get("HALO_SOUND_CACHE_PREFILL", 1);
	if (!enabled || custom_edition_sounds_globals.cache_state != _sound_cache_open ||
		!custom_edition_sounds_globals.writable || custom_edition_sounds_globals.stopping ||
		!custom_edition_sounds_globals.memory)
	{
		return FALSE;
	}
	while (custom_edition_sounds_globals.background_cursor < custom_edition_sounds_globals.entry_count)
	{
		long index = custom_edition_sounds_globals.background_order[custom_edition_sounds_globals.background_cursor];
		struct sound_cache_entry const *entry = &custom_edition_sounds_globals.entries[index];

		if (entry->data_offset || entry->failed || !entry->permutation)
		{
			custom_edition_sounds_globals.background_cursor++;
			continue;
		}
		memset(sound, 0, sizeof(*sound));
		sound->transcoding = _transcoding_ogg_vorbis;
		sound->file_offset = entry->source_offset;
		sound->file_bytes = entry->source_bytes;
		sound->destination_bytes = entry->adpcm_bytes;
		sound->channels = (int)(entry->format & 0xFF);
		sound->rate = (long)(entry->format >> 8);
		sound->entry = index;
		sound->permutation = entry->permutation;
		/* (the file full: no more) */
		if (!decoding_writes(sound))
			return FALSE;
		return TRUE;
	}
	return FALSE;
}

static void *decoder_thread(void *parameter)
{
	struct waiting_sound sound;
	struct decoding decoding;

	(void)parameter;
#ifdef HALO_VITA
	/* (Fourth core helpers, All async: with the cache file thread) */
	vita_host_fourth_core_join("ogg sound decoder", 2);
	{
		long priority = setting_get("HALO_OGG_DECODER_PRIORITY", DECODER_PRIORITY);

		if (priority >= 64 && priority <= 191)
			vita_host_thread_priority_set((int)priority);
	}
#endif
	pthread_mutex_lock(&custom_edition_sounds_globals.lock);
	for (;;)
	{
		long shortest = NONE;
		long index;
		boolean background = FALSE;

		/* the map's sound cache file first */
		if (custom_edition_sounds_globals.cache_state == _sound_cache_opening)
		{
			custom_edition_sounds_globals.running = TRUE;
			custom_edition_sounds_globals.running_loaded = NULL;
			pthread_mutex_unlock(&custom_edition_sounds_globals.lock);
			if (!custom_edition_sounds_globals.reader_tried)
			{
				custom_edition_sounds_globals.reader_tried = TRUE;
				custom_edition_cache_reader_open(&custom_edition_sounds_globals.reader);
			}
			sound_cache_file_open();
			pthread_mutex_lock(&custom_edition_sounds_globals.lock);
			custom_edition_sounds_globals.running = FALSE;
			halo_cond_broadcast(&custom_edition_sounds_globals.done);
			continue;
		}
		if (custom_edition_sounds_globals.waiting_count == 0)
		{
			if (!background_next(&sound))
			{
				halo_cond_wait(&custom_edition_sounds_globals.wake, &custom_edition_sounds_globals.lock);
				continue;
			}
			background = TRUE;
		}
		else
		{
			/* (of those that may be done now: one always may, the earliest
			waiting link of each stream) */
			for (index = 0; index < custom_edition_sounds_globals.waiting_count; index++)
			{
				if ((shortest == NONE ||
					custom_edition_sounds_globals.waiting[index].destination_bytes <
						custom_edition_sounds_globals.waiting[shortest].destination_bytes) &&
					waiting_ready(index))
				{
					shortest = index;
				}
			}
			sound = custom_edition_sounds_globals.waiting[shortest];
			custom_edition_sounds_globals.waiting[shortest] =
				custom_edition_sounds_globals.waiting[--custom_edition_sounds_globals.waiting_count];
		}
		custom_edition_sounds_globals.running = TRUE;
		custom_edition_sounds_globals.running_loaded = sound.loaded;
		custom_edition_sounds_globals.running_cancelled = FALSE;
		pthread_mutex_unlock(&custom_edition_sounds_globals.lock);

		if (!custom_edition_sounds_globals.reader_tried)
		{
			custom_edition_sounds_globals.reader_tried = TRUE;
			custom_edition_cache_reader_open(&custom_edition_sounds_globals.reader);
		}
		memset(&decoding, 0, sizeof(decoding));
		decoding.sound = &sound;
		decoding.destination = sound.destination;
		decoding.background = background;
		sound_decode(&decoding);

		pthread_mutex_lock(&custom_edition_sounds_globals.lock);
		/* (the block is the cache's again: a cancelled one is being
		deleted, and its flag is not ours to set) */
		if (sound.loaded && !custom_edition_sounds_globals.running_cancelled)
			__atomic_store_n(sound.loaded, TRUE, __ATOMIC_RELEASE);
		custom_edition_sounds_globals.running = FALSE;
		custom_edition_sounds_globals.running_loaded = NULL;
		halo_cond_broadcast(&custom_edition_sounds_globals.done);
	}
	return NULL;
}

/* (with the lock held) */
static boolean decoder_thread_start(void)
{
	pthread_attr_t attributes;
	pthread_t thread;

	if (custom_edition_sounds_globals.thread_started)
		return TRUE;
	if (custom_edition_sounds_globals.thread_failed)
		return FALSE;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, DECODER_STACK_BYTES);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&thread, &attributes, decoder_thread, NULL) == 0)
	{
		custom_edition_sounds_globals.thread_started = TRUE;
	}
	else
	{
		custom_edition_sounds_globals.thread_failed = TRUE;
		error(_error_silent, "custom edition: no thread for Ogg Vorbis sounds; they are silent");
	}
	pthread_attr_destroy(&attributes);
	return custom_edition_sounds_globals.thread_started;
}

/* `bytes` bytes of the stream at `offset` into `buffer`, read on the
caller's thread */
static boolean stream_read_now(
	struct sound_permutation const *permutation,
	uint32_t offset,
	uint32_t bytes,
	void *buffer)
{
	return custom_edition_cache_read(NONE, (long)(permutation->samples.file_offset + offset), (long)bytes, buffer);
}

/* the ADPCM bytes of a stream its first and last pages describe, played
by the sound `definition` (all of them: more than the sound cache gives a
permutation, for a long sound); 0 when they do not go together. Its first page
holds only the identification header (Vorbis I: 58 bytes); its last page
is looked for in its last few kilobytes, then in as many as a page can be
(a high quality stream's are tens of kilobytes) */
static uint32_t measured_adpcm_bytes(
	struct sound_permutation const *permutation,
	struct sound_definition const *definition)
{
	uint32_t size = (uint32_t)permutation->samples.size;
	int channels = definition_channels(definition);
	long rate = definition_rate(definition);
	struct ogg_sound_stream_info info;
	uint32_t tail_bytes = size < MEASURE_TAIL_BYTES ? size : MEASURE_TAIL_BYTES;
	uint32_t head_bytes = size < MEASURE_HEAD_BYTES ? size : MEASURE_HEAD_BYTES;
	uint32_t frames;
	uint32_t bytes;
	int found;
	byte *memory;
	byte *measure_tail;
	byte *measure_head;

	pthread_mutex_lock(&custom_edition_sounds_globals.lock);
	memory = memory_block();
	pthread_mutex_unlock(&custom_edition_sounds_globals.lock);
	if (!memory)
		return 0;
	measure_tail = memory + OGG_SOUND_WORKING_BYTES;
	measure_head = measure_tail + MEASURE_LARGEST_TAIL_BYTES;
	if (!size || !stream_read_now(permutation, size - tail_bytes, tail_bytes, measure_tail) ||
		(size > tail_bytes && !stream_read_now(permutation, 0, head_bytes, measure_head)))
	{
		return 0;
	}
	found = ogg_sound_measure(size > tail_bytes ? measure_head : measure_tail, head_bytes, measure_tail, tail_bytes, &info);
	if (!found && size > tail_bytes)
	{
		tail_bytes = size < MEASURE_LARGEST_TAIL_BYTES ? size : MEASURE_LARGEST_TAIL_BYTES;
		if (!stream_read_now(permutation, size - tail_bytes, tail_bytes, measure_tail))
			return 0;
		found = ogg_sound_measure(size > tail_bytes ? measure_head : measure_tail, head_bytes, measure_tail, tail_bytes,
			&info);
	}
	frames = found ? ogg_sound_output_frames(info.frames, info.rate, rate) : 0;
	if (!frames || info.channels < 1 || info.channels > 2)
		return 0;
	bytes = ogg_sound_adpcm_bytes(frames, channels);
	return bytes;
}

/* ---------- public code */

/* the map's Ogg Vorbis permutations listed for its sound cache file, and
the file opened on the decoding thread (custom_edition_sounds_map_loaded) */
static void sound_cache_map_loaded(
	void)
{
	struct tag_iterator iterator;
	long tag_index;
	long count = 0;
	long kept;
	long index;
	unsigned long table_bytes;
	byte *memory;
	char path[SOUND_CACHE_PATH_BYTES];
	char directory[SOUND_CACHE_PATH_BYTES];
	struct custom_edition_sound_cache_source source;
	uint32_t list_crc = 0;
	int pass;

	/* (no sound cache file without sound: a dedicated server's maps, or
	HALO_NO_AUDIO's) */
	if (!custom_edition_cache_tags_loaded() || !setting_get("HALO_SOUND_CACHE", 1) || !config_boolean("audio.enabled") ||
		!custom_edition_cache_sound_cache_path(path, sizeof(path), directory, sizeof(directory), &source))
	{
		return;
	}
	/* the Ogg Vorbis permutations whose ADPCM's size the loader gave
	(counted, then listed) */
	for (pass = 0; pass < 2; pass++)
	{
		struct sound_cache_entry *entries = custom_edition_sounds_globals.entries;

		count = 0;
		tag_iterator_new(&iterator, SOUND_DEFINITION_TAG);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE)
		{
			struct sound_definition const *definition = (struct sound_definition const *)tag_get(SOUND_DEFINITION_TAG, tag_index);
			long pitch_range_index;

			for (pitch_range_index = 0; pitch_range_index < definition->pitch_ranges.count; pitch_range_index++)
			{
				struct sound_pitch_range const *pitch_range =
					TAG_BLOCK_GET_ELEMENT(&definition->pitch_ranges, pitch_range_index, struct sound_pitch_range);
				long permutation_index;

				for (permutation_index = 0; permutation_index < pitch_range->permutations.count; permutation_index++)
				{
					struct sound_permutation const *permutation =
						TAG_BLOCK_GET_ELEMENT(&pitch_range->permutations, permutation_index, struct sound_permutation);

					if (permutation->compression != SOUND_PERMUTATION_COMPRESSION_OGG_VORBIS ||
						!permutation->sample_buffer_size || permutation->sample_buffer_size > OGG_SOUND_MAXIMUM_ADPCM_BYTES ||
						permutation->samples.size <= 0 || permutation->samples.file_offset < 0 ||
						(long)permutation->unknown3 != tag_index)
					{
						continue;
					}
					if (pass && count < custom_edition_sounds_globals.entry_count)
					{
						entries[count].source_offset = (uint32_t)permutation->samples.file_offset;
						entries[count].source_bytes = (uint32_t)permutation->samples.size;
						entries[count].adpcm_bytes = (uint32_t)permutation->sample_buffer_size;
						entries[count].format = (definition->encoding == 1 ? 2U : 1U) |
							(uint32_t)(definition->sample_rate == 1 ? 44100 : 22050) << 8;
						entries[count].data_offset = 0;
						entries[count].failed = FALSE;
						entries[count].permutation = permutation;
					}
					count++;
				}
			}
		}
		if (pass)
			break;
		if (!count)
			return;
		if (count > SOUND_CACHE_MAXIMUM_ENTRIES)
		{
			error(_error_silent, "sound cache: %ld Ogg Vorbis permutations, more than %d; no sound cache file", count,
				(int)SOUND_CACHE_MAXIMUM_ENTRIES);
			return;
		}
		/* the memory block with the table after the decoder's */
		table_bytes = (unsigned long)count * (sizeof(struct sound_cache_entry) + sizeof(uint16_t));
		pthread_mutex_lock(&custom_edition_sounds_globals.lock);
		memory = custom_edition_sounds_globals.memory ? NULL :
			(byte *)halo_custom_edition_memory_alloc(MEMORY_BLOCK_BYTES + table_bytes);
		if (memory)
		{
			custom_edition_sounds_globals.memory = memory;
			custom_edition_sounds_globals.entries = (struct sound_cache_entry *)(memory + MEMORY_BLOCK_BYTES);
			custom_edition_sounds_globals.background_order = (uint16_t *)(custom_edition_sounds_globals.entries + count);
			custom_edition_sounds_globals.entry_count = count;
		}
		pthread_mutex_unlock(&custom_edition_sounds_globals.lock);
		if (!memory)
		{
			error(_error_silent, "sound cache: no room for the table of %ld Ogg Vorbis permutations; no sound cache file", count);
			return;
		}
	}
	/* sorted by stream, the same permutations' entries one */
	qsort(custom_edition_sounds_globals.entries, (size_t)count, sizeof(struct sound_cache_entry), entry_sort_compare);
	for (index = 0, kept = 0; index < count; index++)
	{
		struct sound_cache_entry *entry = &custom_edition_sounds_globals.entries[index];

		if (kept && !entry_sort_compare(entry, &custom_edition_sounds_globals.entries[kept - 1]))
			continue;
		custom_edition_sounds_globals.entries[kept++] = *entry;
	}
	for (index = 0; index < kept; index++)
	{
		struct sound_cache_entry const *entry = &custom_edition_sounds_globals.entries[index];
		byte bytes[16];

		write_u32_le(bytes, entry->source_offset);
		write_u32_le(bytes + 4, entry->source_bytes);
		write_u32_le(bytes + 8, entry->adpcm_bytes);
		write_u32_le(bytes + 12, entry->format);
		list_crc = cache_file_crc32_update(list_crc, bytes, sizeof(bytes));
		custom_edition_sounds_globals.background_order[index] = (uint16_t)index;
	}

	pthread_mutex_lock(&custom_edition_sounds_globals.lock);
	custom_edition_sounds_globals.entry_count = kept;
	qsort(custom_edition_sounds_globals.background_order, (size_t)kept, sizeof(uint16_t), background_sort_compare);
	custom_edition_sounds_globals.background_cursor = 0;
	custom_edition_sounds_globals.list_crc = list_crc;
	custom_edition_sounds_globals.source = source;
	memcpy(custom_edition_sounds_globals.path, path, sizeof(path));
	memcpy(custom_edition_sounds_globals.directory, directory, sizeof(directory));
	custom_edition_sounds_globals.data_start = SOUND_CACHE_FILE_HEADER_BYTES + (uint32_t)kept * SOUND_CACHE_FILE_ENTRY_BYTES;
	custom_edition_sounds_globals.data_end = custom_edition_sounds_globals.data_start;
	{
		unsigned long long limit = (unsigned long long)setting_get("HALO_SOUND_CACHE_MB", SOUND_CACHE_DEFAULT_MEGABYTES) << 20;

		/* (and within a 32-bit offset) */
		custom_edition_sounds_globals.file_limit = limit > 0xFFF00000ull ? 0xFFF00000UL : (uint32_t)limit;
	}
	custom_edition_sounds_globals.cache_state = _sound_cache_opening;
	if (decoder_thread_start())
		halo_cond_signal(&custom_edition_sounds_globals.wake);
	else
		custom_edition_sounds_globals.cache_state = _sound_cache_failed;
	pthread_mutex_unlock(&custom_edition_sounds_globals.lock);
	return;
}

boolean custom_edition_sound_is_transcoded(
	struct sound_permutation const *permutation)
{
	return permutation_transcoding(permutation) != _transcoding_none;
}

long custom_edition_sound_cache_bytes(
	struct sound_permutation *permutation)
{
	enum transcoding transcoding = permutation_transcoding(permutation);

	if (transcoding == _transcoding_none)
		return permutation->samples.size;
	/* (16-bit PCM, and a halved permutation: the loader's size, 0 when it
	was not whole frames) */
	if (transcoding != _transcoding_ogg_vorbis)
	{
		return permutation->sample_buffer_size <= OGG_SOUND_MAXIMUM_ADPCM_BYTES ?
			(long)permutation->sample_buffer_size : 0;
	}
	if (permutation->sample_buffer_size == 0)
	{
		struct sound_definition const *definition = permutation_definition(permutation);
		uint32_t bytes = definition ? measured_adpcm_bytes(permutation, definition) : 0;
		uint32_t block_bytes = definition ? OGG_SOUND_ADPCM_BLOCK_BYTES * (uint32_t)definition_channels(definition) : 1;

		/* (more than the cache gives a permutation is cut off: a long sound
		is made a chain as the map loads, custom_edition_sounds_map_loaded,
		unless its stream was too small to be measured then) */
		if (bytes > OGG_SOUND_MAXIMUM_ADPCM_BYTES)
			bytes = OGG_SOUND_MAXIMUM_ADPCM_BYTES / block_bytes * block_bytes;
		permutation->sample_buffer_size = bytes ? bytes : SOUND_BUFFER_UNPLAYABLE;
		if (!bytes)
		{
			error(_error_silent, "custom edition: an Ogg Vorbis sound of %s (%.32s) has no length; it is not played",
				tag_get_name((long)permutation->unknown3), permutation->name);
		}
	}
	if (permutation->sample_buffer_size == SOUND_BUFFER_UNPLAYABLE ||
		permutation->sample_buffer_size > OGG_SOUND_MAXIMUM_ADPCM_BYTES)
	{
		return 0;
	}
	return (long)permutation->sample_buffer_size;
}

boolean custom_edition_sound_is_chained(
	struct sound_permutation const *permutation)
{
	return chain_link(permutation, NULL);
}

/* a permutation's chain: its links and the blocks of each but the last
(whole Xbox ADPCM blocks of `channels` channels, of equal length but the
last: the loop comes back to the first link when the last starts, so a last
link of a few blocks would leave the first no time to load), for all of
its `whole_bytes` up to CHAIN_MAXIMUM_ADPCM_BYTES; 0 links when it needs
no chain */
static long chain_layout(
	uint32_t whole_bytes,
	int channels,
	uint32_t *link_blocks)
{
	uint32_t block_bytes = OGG_SOUND_ADPCM_BLOCK_BYTES * (uint32_t)channels;
	uint32_t blocks;
	uint32_t links;

	*link_blocks = 0;
	if (whole_bytes <= OGG_SOUND_MAXIMUM_ADPCM_BYTES)
		return 0;
	if (whole_bytes > CHAIN_MAXIMUM_ADPCM_BYTES)
		whole_bytes = CHAIN_MAXIMUM_ADPCM_BYTES;
	blocks = whole_bytes / block_bytes;
	links = (blocks * block_bytes + CHAIN_LINK_BYTES - 1) / CHAIN_LINK_BYTES;
	*link_blocks = (blocks + links - 1) / links;
	return (long)((blocks + *link_blocks - 1) / *link_blocks);
}

/* the links a pitch range's chains add to it (0 when it has none, or they
would make it more than a range may have) */
static long range_links_added(
	struct sound_pitch_range const *range,
	struct sound_definition const *definition)
{
	long added = 0;
	long index;

	for (index = 0; index < range->permutations.count; index++)
	{
		struct sound_permutation const *permutation =
			(struct sound_permutation const *)range->permutations.address + index;
		uint32_t link_blocks;
		long links = chain_layout(permutation_whole_bytes(permutation, definition), definition_channels(definition),
			&link_blocks);

		if (links > 1)
			added += links - 1;
	}
	return range->permutations.count + added > CHAIN_MAXIMUM_RANGE_PERMUTATIONS ? 0 : added;
}

/* whether the map's sound `definition` and its pitch range `range` are
ones whose chains are made (the same answer as the map is planned and as
the chains are made: custom_edition_sounds_map_loaded) */
static boolean range_chained(
	struct sound_pitch_range const *range,
	struct sound_definition const *definition,
	long *links_so_far)
{
	long added = range_links_added(range, definition);

	if (!added || *links_so_far + added > CHAIN_MAXIMUM_LINKS)
		return FALSE;
	*links_so_far += added;
	return TRUE;
}

/* the permutations of the pitch range `range` of `definition` (of the
sound `tag_index`) and their chains' links, written at `permutations`
(the chains' table from `table_index`), which the range then has; returns
how many */
static long range_chains_make(
	struct sound_pitch_range *range,
	struct sound_definition const *definition,
	long tag_index,
	long table_index)
{
	struct sound_permutation const *own = (struct sound_permutation const *)range->permutations.address;
	struct sound_permutation *permutations = custom_edition_sounds_globals.chain_permutations + table_index;
	long count = range->permutations.count;
	int channels = definition_channels(definition);
	long rate = definition_rate(definition);
	boolean linked = TEST_FLAG(definition->flags, _sound_definition_linked_permutations_bit);
	long index;

	memcpy(permutations, own, (size_t)count * sizeof(*permutations));
	for (index = 0; index < count; index++)
		custom_edition_sounds_globals.chain_first_frames[table_index + index] = CHAIN_NOT_A_LINK;
	for (index = 0; index < range->permutations.count; index++)
	{
		uint32_t whole_bytes = permutation_whole_bytes(&own[index], definition);
		uint32_t block_bytes = OGG_SOUND_ADPCM_BLOCK_BYTES * (uint32_t)channels;
		uint32_t link_blocks;
		long links = chain_layout(whole_bytes, channels, &link_blocks);
		uint32_t blocks = (whole_bytes > CHAIN_MAXIMUM_ADPCM_BYTES ? CHAIN_MAXIMUM_ADPCM_BYTES : whole_bytes) / block_bytes;
		/* (the chain goes on with what the permutation was followed by, in
		a sound that had chains of its own) */
		short after = linked ? own[index].next_permutation_index : NONE;
		long link;

		if (links < 2)
			continue;
		for (link = 0; link < links; link++)
		{
			long link_index = link ? count + link - 1 : index;
			struct sound_permutation *permutation = &permutations[link_index];
			uint32_t first_block = (uint32_t)link * link_blocks;
			uint32_t link_frames;

			*permutation = own[index];
			permutation->sample_buffer_size = MIN(link_blocks, blocks - first_block) * block_bytes;
			permutation->next_permutation_index = link + 1 < links ? (short)(count + link) : after;
			custom_edition_sounds_globals.chain_first_frames[table_index + link_index] = first_block * OGG_SOUND_ADPCM_BLOCK_SAMPLES;
			custom_edition_sounds_globals.chain_link_indices[table_index + link_index] = (short)link;
			custom_edition_sounds_globals.chain_link_counts[table_index + link_index] = (short)links;
			/* the mouth's 30 ticks a second, from the link's first */
			link_frames = MIN(link_blocks, blocks - first_block) * OGG_SOUND_ADPCM_BLOCK_SAMPLES;
			if (own[index].mouth_data.size > 0 && own[index].mouth_data.address)
			{
				long first_tick = (long)((uint64_t)first_block * OGG_SOUND_ADPCM_BLOCK_SAMPLES * 30 / (uint64_t)rate);
				long ticks = (long)(((uint64_t)link_frames * 30 + (uint64_t)rate - 1) / (uint64_t)rate);

				if (first_tick > own[index].mouth_data.size - 1)
					first_tick = own[index].mouth_data.size - 1;
				permutation->mouth_data.address = (byte *)own[index].mouth_data.address + first_tick;
				permutation->mouth_data.size = MAX(1, MIN(ticks, own[index].mouth_data.size - first_tick));
			}
		}
		platform_log("custom edition: %s (%.32s) is %lu frames of %d ch %ld Hz (%lu KB of Xbox ADPCM), played as a chain "
			"of %ld permutations of %lu KB%s",
			tag_get_name(tag_index), own[index].name, (unsigned long)(blocks * OGG_SOUND_ADPCM_BLOCK_SAMPLES), channels, rate,
			(unsigned long)(whole_bytes / 1024), links, (unsigned long)(link_blocks * block_bytes / 1024),
			whole_bytes > CHAIN_MAXIMUM_ADPCM_BYTES ? " (the rest cut off)" : "");
		count += links - 1;
	}
	return count;
}

/* the map's long sounds made chains (custom_edition_sounds_map_loaded) */
static void chains_map_loaded(
	void)
{
	struct tag_iterator iterator;
	long tag_index;
	long links_so_far = 0;
	long table_count = 0;
	long stream_chains = 0;
	long chains = 0;
	unsigned long table_bytes;
	unsigned long bytes;
	long used;
	long index;

	/* (a map without sound plays none: the dedicated server) */
	if (!custom_edition_cache_tags_loaded() || !config_boolean("audio.enabled") ||
		custom_edition_sounds_globals.chain_memory)
	{
		return;
	}
	/* what the chains need: the lengths the loader could not give of
	streams that may be long measured now, as they would be as they first
	load */
	tag_iterator_new(&iterator, SOUND_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct sound_definition *definition = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, tag_index);
		long range_index;

		for (range_index = 0; range_index < definition->pitch_ranges.count; range_index++)
		{
			struct sound_pitch_range *range = TAG_BLOCK_GET_ELEMENT(&definition->pitch_ranges, range_index, struct sound_pitch_range);

			for (index = 0; index < range->permutations.count; index++)
			{
				struct sound_permutation *permutation = TAG_BLOCK_GET_ELEMENT(&range->permutations, index, struct sound_permutation);

				if (permutation_transcoding(permutation) == _transcoding_ogg_vorbis && permutation->sample_buffer_size == 0 &&
					permutation->samples.size >= CHAIN_MEASURE_STREAM_BYTES)
				{
					uint32_t whole_bytes = measured_adpcm_bytes(permutation, definition);

					/* (none: left to be measured as it loads, and said then) */
					if (whole_bytes)
						permutation->sample_buffer_size = whole_bytes;
				}
			}
			if (range_chained(range, definition, &links_so_far))
			{
				table_count += range->permutations.count + range_links_added(range, definition);
				for (index = 0; index < range->permutations.count; index++)
				{
					struct sound_permutation const *permutation =
						TAG_BLOCK_GET_ELEMENT(&range->permutations, index, struct sound_permutation);
					uint32_t link_blocks;

					if (chain_layout(permutation_whole_bytes(permutation, definition), definition_channels(definition),
						&link_blocks) > 1)
					{
						chains++;
						if (permutation_transcoding(permutation) == _transcoding_ogg_vorbis)
							stream_chains++;
					}
				}
			}
		}
	}
	if (!table_count)
		return;

	/* the memory: the stream decoders, then the table */
	custom_edition_sounds_globals.stream_decoder_count = MIN(stream_chains, STREAM_DECODERS);
	table_bytes = (unsigned long)table_count * (sizeof(struct sound_permutation) + sizeof(uint32_t) + 2 * sizeof(short));
	bytes = (unsigned long)custom_edition_sounds_globals.stream_decoder_count * OGG_SOUND_WORKING_BYTES + table_bytes;
	custom_edition_sounds_globals.chain_memory = (byte *)halo_custom_edition_memory_alloc(bytes);
	if (!custom_edition_sounds_globals.chain_memory)
	{
		error(_error_silent, "custom edition: no room for the chains of %ld long sounds; they are cut off", chains);
		custom_edition_sounds_globals.stream_decoder_count = 0;
		return;
	}
	for (index = 0; index < custom_edition_sounds_globals.stream_decoder_count; index++)
	{
		custom_edition_sounds_globals.stream_working[index] =
			custom_edition_sounds_globals.chain_memory + (unsigned long)index * OGG_SOUND_WORKING_BYTES;
		custom_edition_sounds_globals.stream_working_used[index] = 0;
	}
	custom_edition_sounds_globals.chain_permutations = (struct sound_permutation *)(custom_edition_sounds_globals.chain_memory +
		(unsigned long)custom_edition_sounds_globals.stream_decoder_count * OGG_SOUND_WORKING_BYTES);
	custom_edition_sounds_globals.chain_first_frames = (uint32_t *)(custom_edition_sounds_globals.chain_permutations + table_count);
	custom_edition_sounds_globals.chain_link_indices = (short *)(custom_edition_sounds_globals.chain_first_frames + table_count);
	custom_edition_sounds_globals.chain_link_counts = custom_edition_sounds_globals.chain_link_indices + table_count;
	custom_edition_sounds_globals.chain_permutation_count = table_count;

	/* the chains, the same ranges as above: each range given its
	permutations and their links in the table */
	links_so_far = 0;
	used = 0;
	tag_iterator_new(&iterator, SOUND_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct sound_definition *definition = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, tag_index);
		boolean chained = FALSE;
		long range_index;

		for (range_index = 0; range_index < definition->pitch_ranges.count; range_index++)
		{
			struct sound_pitch_range *range = TAG_BLOCK_GET_ELEMENT(&definition->pitch_ranges, range_index, struct sound_pitch_range);
			long count;

			if (!range_chained(range, definition, &links_so_far) ||
				used + range->permutations.count + range_links_added(range, definition) > table_count)
			{
				continue;
			}
			count = range_chains_make(range, definition, tag_index, used);
			range->permutations.address = custom_edition_sounds_globals.chain_permutations + used;
			range->permutations.count = count;
			used += count;
			chained = TRUE;
		}
		/* the sound plays its chains (sound_definition_next_permutation);
		one that had none of its own has every other permutation end its
		chain, as any of its permutations did */
		if (chained && !TEST_FLAG(definition->flags, _sound_definition_linked_permutations_bit))
		{
			SET_FLAG(definition->flags, _sound_definition_linked_permutations_bit, TRUE);
			for (range_index = 0; range_index < definition->pitch_ranges.count; range_index++)
			{
				struct sound_pitch_range *range = TAG_BLOCK_GET_ELEMENT(&definition->pitch_ranges, range_index, struct sound_pitch_range);

				for (index = 0; index < range->permutations.count; index++)
				{
					struct sound_permutation *permutation = TAG_BLOCK_GET_ELEMENT(&range->permutations, index, struct sound_permutation);

					if (!chain_link(permutation, NULL))
						permutation->next_permutation_index = NONE;
				}
			}
		}
	}
	platform_log("custom edition: %ld long sounds made chains of permutations (%ld permutations, %lu KB, and %ld stream "
		"decoders of %lu KB)", chains, table_count, table_bytes / 1024, custom_edition_sounds_globals.stream_decoder_count,
		(unsigned long)OGG_SOUND_WORKING_BYTES / 1024);
	return;
}

void custom_edition_sounds_map_loaded(
	void)
{
	/* the sound cache file's list first: the loader's own permutations, as
	they are before the chains take their pitch ranges' places (a link is
	kept in no sound cache file: custom_edition_sound_load) */
	sound_cache_map_loaded();
	chains_map_loaded();
	return;
}

boolean custom_edition_sound_load(
	struct sound_permutation const *permutation,
	void *destination,
	long destination_bytes,
	boolean *loaded)
{
	struct sound_definition const *definition = permutation_definition(permutation);
	enum transcoding transcoding = permutation_transcoding(permutation);
	struct waiting_sound *sound;
	boolean queued = FALSE;

	pthread_mutex_lock(&custom_edition_sounds_globals.lock);
	if (definition && destination_bytes > 0 && transcoding != _transcoding_none &&
		custom_edition_sounds_globals.waiting_count < MAXIMUM_WAITING_SOUNDS &&
		decoder_thread_start())
	{
		/* (16-bit PCM needs no working memory) */
		if (transcoding == _transcoding_ogg_vorbis)
			memory_block();
		sound = &custom_edition_sounds_globals.waiting[custom_edition_sounds_globals.waiting_count];
		sound->transcoding = transcoding;
		sound->file_offset = (unsigned long)permutation->samples.file_offset;
		sound->file_bytes = (uint32_t)permutation->samples.size;
		sound->destination = (byte *)destination;
		sound->destination_bytes = (uint32_t)destination_bytes;
		sound->loaded = loaded;
		sound->channels = definition_channels(definition);
		sound->rate = definition_rate(definition);
		sound->permutation = permutation;
		sound->first_frame = 0;
		sound->link = chain_link(permutation, &sound->first_frame);
		sound->link_index = sound->link ? custom_edition_sounds_globals.chain_link_indices[chain_index(permutation)] : 0;
		sound->link_count = sound->link ? custom_edition_sounds_globals.chain_link_counts[chain_index(permutation)] : 0;
		sound->stream_link = sound->link && transcoding == _transcoding_ogg_vorbis;
		/* (a link has no entry in the sound cache file: its ADPCM is a piece
		of a long sound's, made as it loads) */
		sound->entry = sound->link ? NONE : entry_find(sound);
		*loaded = FALSE;
		queued = TRUE;
		/* (counted once it is whole: the decoder's thread reads the count
		without the lock) */
		__atomic_store_n(&custom_edition_sounds_globals.waiting_count, custom_edition_sounds_globals.waiting_count + 1,
			__ATOMIC_RELEASE);
		halo_cond_signal(&custom_edition_sounds_globals.wake);
	}
	pthread_mutex_unlock(&custom_edition_sounds_globals.lock);
	if (!queued)
	{
		/* (silence, at once: the cache's block is not left loading) */
		if (destination_bytes > 0)
			memset(destination, 0, (size_t)destination_bytes);
		__atomic_store_n(loaded, TRUE, __ATOMIC_RELEASE);
	}
	return queued;
}

void custom_edition_sound_cancel(
	boolean *loaded)
{
	long index;

	pthread_mutex_lock(&custom_edition_sounds_globals.lock);
	for (index = 0; index < custom_edition_sounds_globals.waiting_count; index++)
	{
		if (custom_edition_sounds_globals.waiting[index].loaded == loaded)
		{
			custom_edition_sounds_globals.waiting[index] =
				custom_edition_sounds_globals.waiting[--custom_edition_sounds_globals.waiting_count];
			break;
		}
	}
	/* (the decoding running for this block writes no more of it, under
	the lock: nothing to wait for) */
	if (custom_edition_sounds_globals.running && custom_edition_sounds_globals.running_loaded == loaded)
		custom_edition_sounds_globals.running_cancelled = TRUE;
	pthread_mutex_unlock(&custom_edition_sounds_globals.lock);
	return;
}

void custom_edition_sounds_stop(
	void)
{
	unsigned long long held_bytes = 0;
	unsigned long held = 0;
	long index;

	pthread_mutex_lock(&custom_edition_sounds_globals.lock);
	custom_edition_sounds_globals.waiting_count = 0;
	custom_edition_sounds_globals.stopping = TRUE;
	if (custom_edition_sounds_globals.running)
	{
		custom_edition_sounds_globals.running_cancelled = TRUE;
		while (custom_edition_sounds_globals.running)
			halo_cond_wait(&custom_edition_sounds_globals.done, &custom_edition_sounds_globals.lock);
	}
	for (index = 0; index < custom_edition_sounds_globals.entry_count; index++)
	{
		if (custom_edition_sounds_globals.entries[index].data_offset)
		{
			held++;
			held_bytes += custom_edition_sounds_globals.entries[index].adpcm_bytes;
		}
	}
	sound_cache_file_close();
	custom_edition_cache_reader_close(&custom_edition_sounds_globals.reader);
	custom_edition_sounds_globals.reader_tried = FALSE;
	if (custom_edition_sounds_globals.memory)
	{
		halo_custom_edition_memory_free(custom_edition_sounds_globals.memory);
		custom_edition_sounds_globals.memory = NULL;
	}
	/* (the tags that named the chains go with the map) */
	if (custom_edition_sounds_globals.chain_memory)
	{
		halo_custom_edition_memory_free(custom_edition_sounds_globals.chain_memory);
		custom_edition_sounds_globals.chain_memory = NULL;
		custom_edition_sounds_globals.chain_permutations = NULL;
		custom_edition_sounds_globals.chain_first_frames = NULL;
		custom_edition_sounds_globals.chain_link_indices = NULL;
		custom_edition_sounds_globals.chain_link_counts = NULL;
		custom_edition_sounds_globals.chain_permutation_count = 0;
		for (index = 0; index < STREAM_DECODERS; index++)
			custom_edition_sounds_globals.stream_working[index] = NULL;
		custom_edition_sounds_globals.stream_decoder_count = 0;
	}
	if (custom_edition_sounds_globals.decoded_count || custom_edition_sounds_globals.encoded_count ||
		custom_edition_sounds_globals.cache_reads || custom_edition_sounds_globals.background_count)
	{
		platform_log("ogg sound: %lu Ogg Vorbis permutations decoded with the map, %.1f ms in all; "
			"%lu 16-bit PCM ones encoded, %.1f ms; %lu of them links of long sounds' chains (%lu taken up from the link before)",
			custom_edition_sounds_globals.decoded_count,
			(double)custom_edition_sounds_globals.decoded_microseconds / 1000.0,
			custom_edition_sounds_globals.encoded_count,
			(double)custom_edition_sounds_globals.encoded_microseconds / 1000.0,
			custom_edition_sounds_globals.link_decoded_count, custom_edition_sounds_globals.link_resumed_count);
	}
	if (custom_edition_sounds_globals.entry_count)
	{
		platform_log("sound cache: %lu permutations read from the file, %.1f ms (%llu KB); %lu written (%llu KB); "
			"%lu decoded in the background, %.1f ms; %lu of %ld held (%llu KB), %lu entries refused",
			custom_edition_sounds_globals.cache_reads,
			(double)custom_edition_sounds_globals.cache_read_microseconds / 1000.0,
			custom_edition_sounds_globals.cache_read_bytes / 1024,
			custom_edition_sounds_globals.cache_writes,
			custom_edition_sounds_globals.cache_written_bytes / 1024,
			custom_edition_sounds_globals.background_count,
			(double)custom_edition_sounds_globals.background_microseconds / 1000.0,
			held, custom_edition_sounds_globals.entry_count, held_bytes / 1024,
			custom_edition_sounds_globals.entries_refused);
	}
	custom_edition_sounds_globals.cache_state = _sound_cache_none;
	custom_edition_sounds_globals.entries = NULL;
	custom_edition_sounds_globals.background_order = NULL;
	custom_edition_sounds_globals.entry_count = 0;
	custom_edition_sounds_globals.background_cursor = 0;
	custom_edition_sounds_globals.decoded_count = 0;
	custom_edition_sounds_globals.decoded_microseconds = 0;
	custom_edition_sounds_globals.encoded_count = 0;
	custom_edition_sounds_globals.encoded_microseconds = 0;
	custom_edition_sounds_globals.link_decoded_count = 0;
	custom_edition_sounds_globals.link_resumed_count = 0;
	custom_edition_sounds_globals.cache_reads = 0;
	custom_edition_sounds_globals.cache_read_microseconds = 0;
	custom_edition_sounds_globals.cache_read_bytes = 0;
	custom_edition_sounds_globals.cache_writes = 0;
	custom_edition_sounds_globals.cache_written_bytes = 0;
	custom_edition_sounds_globals.background_count = 0;
	custom_edition_sounds_globals.background_microseconds = 0;
	custom_edition_sounds_globals.held_at_open = 0;
	custom_edition_sounds_globals.entries_refused = 0;
	custom_edition_sounds_globals.stopping = FALSE;
	memset(failure_reported, 0, sizeof(failure_reported));
	pthread_mutex_unlock(&custom_edition_sounds_globals.lock);
	return;
}
