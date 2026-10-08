/*
MAP_SHARE_PROTOCOL.H

The rules of map sharing (map_share.c): a joiner that lacks the host's
custom map, or has another copy of it, downloads the host's copy over the
game's own connection to the host (its reliable stream: TCP on a LAN, the
internet play tunnel's KCP streams, the ad hoc bridge), in the pregame lobby
only. This unit holds what can be checked without the game, so that it is
tested on its own (port/vita/tests/map_share_test.c): the messages' fields,
the file names a map may have, the receiving side's bookkeeping, and the
cache header a downloaded map must have before it is ever opened as a map.

Every field comes from the network and is checked here before use. The file
is untrusted: its header is checked as soon as it arrives and again on disk,
its length and SHA-256 must be what the host said, and its fingerprint (what
custom_edition_cache_map_identity computes) the one the host's game
settings carry.

The conversation (one download at a time on a joiner, a few on a host):
  joiner                               host
  query (name, fingerprint,
    capabilities)              ->
                               <-      offer (name, size, fingerprint, .map/.yelo,
                                       what of the capabilities it has)
                                       or refused (reason)
  [the player says yes]
  start (capabilities, the
    offset to continue from)   ->
                               <-      the offer again (what it will do)
                               <-      data (offset, length, bytes) ... as the
                                       window and the host's rate allow
  ack (stream bytes taken)     ->      (every MAP_SHARE_ACK_BYTES)
                               <-      done (SHA-256 of the whole file)
  ack (all of it)              ->
  cancel (either way, at any time; a host that leaves pregame refuses)

Capabilities (1.1.0's test builds 1-3, network version 17, have none; their
messages are the same): a joiner says what it can do in its query's and
start's reason (which a host without them ignores, and which stays below
NUMBER_OF_MAP_SHARE_REFUSALS, so it takes the request), and a host offers
only those it has of what the joiner said (offer flags an older joiner never
sees). Without them the transfer is as before: the file's bytes from 0.
- resume: the start's offset is where the joiner's kept part of the file
  ends; the host sends from there (the done's SHA-256 is still the whole
  file's, which the joiner checks over its kept part read back and the rest).
- deflate: the data is a zlib stream of the file (from the start's offset),
  cut into whole data messages; offsets, lengths and acknowledgements count
  the stream's bytes. A host deflates a Custom Edition map only (an Xbox map
  is compressed already), and may store blocks as they are while its CPU,
  not the link, is what limits it (map_share_packer).
*/

#ifndef __MAP_SHARE_PROTOCOL_H
#define __MAP_SHARE_PROTOCOL_H

#include <stdint.h>

/* ---------- constants */

/* the largest map shared (the largest Custom Edition maps with OpenSauce's
memory upgrades are a little over 200 MB) */
#define MAP_SHARE_MAXIMUM_FILE_BYTES (256UL * 1024UL * 1024UL)
/* a map's file name, without its extension: custom_edition_maps.c's
MAXIMUM_MAP_NAME_LENGTH (a level name must fit the game engine's 64
characters) */
#define MAP_SHARE_MAXIMUM_NAME_LENGTH 25
#define MAP_SHARE_NAME_BYTES 32
/* the file bytes of one data message (a message holds at most 0xFFF bytes,
header included) */
#define MAP_SHARE_CHUNK_BYTES 0xC00
/* the bytes (of the stream: the file's own, or deflate's) a host sends
ahead of the joiner's acknowledgement: enough for a home connection's
round trip (the Xbox's 64 KB window let an upload of 1 MB/s with a 100 ms
round trip, and the frames' waits on both sides, carry 330 KB/s) */
#define MAP_SHARE_WINDOW_BYTES 0x60000
/* ... with at most this much waiting in the connection's own outgoing queue
(0x40000, network_connection.c; the rest waits in the socket and the
tunnel): the queue never fills, and the lobby's own messages to that machine
wait behind at most this much */
#define MAP_SHARE_QUEUE_BYTES 0x8000
/* how often a joiner acknowledges what it has written */
#define MAP_SHARE_ACK_BYTES 0x4000
/* what all of a host's uploads together send per second at most (the
lobby's messages to the other machines keep the rest of the link);
HALO_MAP_SHARE_RATE_KB changes it */
#define MAP_SHARE_DEFAULT_BYTES_PER_SECOND (2UL * 1024UL * 1024UL)
/* the uploads a host serves at once */
#define MAP_SHARE_MAXIMUM_UPLOADS 4
/* a transfer that moves nothing for this long is given up */
#define MAP_SHARE_STALL_MILLISECONDS 20000
/* a joiner whose query has had no answer by then (a host of a build without
map sharing ignores it) is told the map is missing, as before */
#define MAP_SHARE_QUERY_TIMEOUT_MILLISECONDS 5000
/* a joiner's kept part of a download (<name>.download, resumed): written
down this often, and kept when at least this long */
#define MAP_SHARE_RESUME_SAVE_BYTES (1024UL * 1024UL)
#define MAP_SHARE_RESUME_MINIMUM_BYTES (256UL * 1024UL)
/* a kept part not taken up again for this long is deleted */
#define MAP_SHARE_RESUME_KEEP_SECONDS (14UL * 24UL * 60UL * 60UL)
/* the share of a host's time its uploads' reading, hashing and deflating
may take (HALO_MAP_SHARE_CPU_PERCENT) */
#define MAP_SHARE_DEFAULT_CPU_PERCENT 30
/* a deflate stream's zlib settings: level 1 (deflate's fastest: a map's
tag data and pictures shrink to 40-55%), a 16 KB window (128 KB of state
a host's upload, 24 KB a joiner's) */
#define MAP_SHARE_DEFLATE_LEVEL 1
#define MAP_SHARE_DEFLATE_WINDOW_BITS 14
#define MAP_SHARE_DEFLATE_MEMORY_LEVEL 7
/* a cache header (cache_file_formats.h's CACHE_FILE_HEADER_BYTES) */
#define MAP_SHARE_HEADER_BYTES 0x800
#define MAP_SHARE_DIGEST_BYTES 32

/* what a joiner can do: its query's and start's reason (map_share.h) */
enum
{
	/* it continues a download from the start's offset */
	_map_share_capability_resume_bit = 0,
	/* it takes the file as a deflate stream */
	_map_share_capability_deflate_bit,
	NUMBER_OF_MAP_SHARE_CAPABILITIES
};

/* a joiner's request's command */
enum map_share_command
{
	_map_share_command_query = 1,
	_map_share_command_start,
	_map_share_command_ack,
	_map_share_command_cancel,
	NUMBER_OF_MAP_SHARE_COMMANDS
};

/* a host's answer's kind */
enum map_share_answer
{
	_map_share_answer_offer = 1,
	_map_share_answer_done,
	_map_share_answer_refused,
	NUMBER_OF_MAP_SHARE_ANSWERS
};

/* why a host refused (also said by a joiner that gave up) */
enum map_share_refusal
{
	_map_share_refusal_none = 0,
	/* not the map the host's game plays, or not a custom map it has on */
	_map_share_refusal_not_shared,
	/* the host's copy is not the one its game settings name */
	_map_share_refusal_changed,
	_map_share_refusal_too_large,
	_map_share_refusal_busy,
	/* the host's game is no longer in its lobby */
	_map_share_refusal_not_in_lobby,
	_map_share_refusal_read_failed,
	_map_share_refusal_cancelled,
	_map_share_refusal_protocol,
	NUMBER_OF_MAP_SHARE_REFUSALS
};

/* an offer's flags */
enum
{
	/* the host's file is an OpenSauce ".yelo" (else ".map") */
	_map_share_offer_yelo_bit = 0,
	/* a Custom Edition cache (version 609; else an Xbox one), which the
	joiner plays only with its PC maps setting on */
	_map_share_offer_custom_edition_bit,
	/* (to a joiner that said it can) the host continues from the start's
	offset */
	_map_share_offer_resume_bit,
	/* (likewise) the data is a deflate stream */
	_map_share_offer_deflate_bit,
	NUMBER_OF_MAP_SHARE_OFFER_FLAGS
};

/* map_share_header_validate's verdicts */
enum map_share_header_status
{
	_map_share_header_ok = 0,
	_map_share_header_too_small,
	_map_share_header_bad_signatures,
	_map_share_header_unsupported_version,
	_map_share_header_unterminated_string,
	_map_share_header_wrong_name,
	_map_share_header_not_multiplayer,
	_map_share_header_compressed,
	_map_share_header_bad_length,
	_map_share_header_bad_tag_data,
	NUMBER_OF_MAP_SHARE_HEADER_STATUSES
};

/* map_share_receiver_accept's verdicts */
enum map_share_chunk_status
{
	_map_share_chunk_ok = 0,
	/* not where the file has reached */
	_map_share_chunk_out_of_order,
	_map_share_chunk_bad_length,
	_map_share_chunk_past_end,
	/* a deflate stream that does not inflate, or goes on past its end */
	_map_share_chunk_bad_stream,
	/* the file could not be written */
	_map_share_chunk_write_failed,
	NUMBER_OF_MAP_SHARE_CHUNK_STATUSES
};

/* ---------- structures */

/* The messages, as decoded (network_messages.c defines their fields: the
shorts, then the longs, then the bytes, byte-swapped by the packet codec). */

/* _message_client_map_download */
struct map_share_request
{
	int16_t command;
	/* query, start: the joiner's capabilities (bits); cancel: why */
	int16_t reason;
	/* ack: the stream's bytes taken; start: where the joiner's kept part
	ends (with the resume capability) */
	int32_t offset;
	/* the fingerprint the host's game settings carry */
	int32_t identity;
	char name[MAP_SHARE_NAME_BYTES];
};

/* _message_server_map_download_answer */
struct map_share_answer_message
{
	int16_t kind;
	int16_t reason;
	int32_t size;
	int32_t identity;
	int32_t flags;
	char name[MAP_SHARE_NAME_BYTES];
	/* done: the SHA-256 of the file */
	uint8_t digest[MAP_SHARE_DIGEST_BYTES];
};

/* _message_server_map_download_data */
struct map_share_data_message
{
	/* the stream's bytes in it */
	int16_t length;
	int16_t reserved;
	/* where they are in the stream (the file, or a deflate stream from the
	start's offset) */
	int32_t offset;
	uint8_t data[MAP_SHARE_CHUNK_BYTES];
};

/* the running SHA-256 (p2p_crypto.c's, halo_sha256_*), opaque here */
struct halo_sha256_stream
{
	unsigned long long opaque[16];
};

/* writes the next `length` bytes of the file; nonzero when written */
typedef int (*map_share_write_function)(
	void *context,
	uint8_t const *data,
	uint32_t length);

/* what a joiner keeps of the file it receives */
struct map_share_receiver
{
	uint32_t size;
	/* the file's bytes in hand (a kept part's included) */
	uint32_t received;
	/* the stream's: taken, and acknowledged to the host */
	uint32_t stream_received;
	uint32_t acknowledged;
	/* the cache header, kept as it arrives, checked once whole */
	uint8_t header[MAP_SHARE_HEADER_BYTES];
	int header_checked;
	struct halo_sha256_stream sha256;
	uint32_t crc;
	/* a deflate stream: its zlib state (allocated), and whether it ended */
	void *inflater;
	int stream_ended;
};

/* a host's deflate stream of a file */
struct map_share_packer
{
	/* (allocated) */
	void *deflater;
	/* the level asked (MAP_SHARE_DEFLATE_LEVEL, or 0: blocks stored as they
	are, which costs a copy), and the one the stream has */
	int level;
	int stream_level;
	/* the file's last bytes given (Z_FINISH): no more level changes */
	int finishing;
	int ended;
};

/* what a joiner writes down of a download it may continue
(<name>.resume, beside <name>.download) */
struct map_share_resume
{
	char name[MAP_SHARE_NAME_BYTES];
	/* the host's file: its fingerprint, size, and kind (offer flags: .yelo,
	Custom Edition) */
	uint32_t identity;
	uint32_t size;
	uint32_t flags;
	/* the bytes kept, and their SHA-256 */
	uint32_t kept;
	uint8_t kept_digest[MAP_SHARE_DIGEST_BYTES];
	/* when written (seconds since 1970) */
	uint32_t saved_time;
};
#define MAP_SHARE_RESUME_RECORD_BYTES (8 + MAP_SHARE_NAME_BYTES + 5 * 4 + MAP_SHARE_DIGEST_BYTES)

/* ---------- prototypes/MAP_SHARE_PROTOCOL.C */

/* Whether `name` may be a shared map's file name (no extension): 1 to
MAP_SHARE_MAXIMUM_NAME_LENGTH printable ASCII characters but / \ : * ? " < >
|, ',' and %, not starting with '.' or a space nor ending with either, no "..",
not a device's name (con, nul, com1...), and not the name of one of the Xbox's own levels
(stock maps are never shared: every machine has its own) nor of Custom
Edition's resource maps (bitmaps, sounds, loc). */
int map_share_name_valid(
	char const *name);

/* Whether `name` names one of the Xbox's 23 levels (13 multiplayer, 10
campaign), in either case. */
int map_share_name_is_stock(
	char const *name);

/* Copies the name field `field` (MAP_SHARE_NAME_BYTES, from the network) to
`name` when it is ended within the field and valid; else 0. */
int map_share_name_from_field(
	char const field[MAP_SHARE_NAME_BYTES],
	char name[MAP_SHARE_NAME_BYTES]);

/* Whether a decoded request's fields are in range (its name valid). */
int map_share_request_valid(
	struct map_share_request const *request);

/* Whether a decoded answer's fields are in range: a known kind and reason;
an offer for `expected_name` and `expected_identity` (the joiner's),
1 to MAP_SHARE_MAXIMUM_FILE_BYTES long, at least a cache header, with only
known flags, and only those of `capabilities` (the joiner's) it said. */
int map_share_answer_valid(
	struct map_share_answer_message const *answer,
	char const *expected_name,
	uint32_t expected_identity,
	uint32_t capabilities);

/* A request's capabilities (a query's, a start's): only those known. */
uint32_t map_share_request_capabilities(
	struct map_share_request const *request);

/* Starts receiving a file of `size` bytes (a receiver used before is
ended first: map_share_receiver_end). */
void map_share_receiver_begin(
	struct map_share_receiver *receiver,
	uint32_t size);

/* Counts in the file's next `length` bytes (the digests, the header) that
did not come over the network: a kept part, read back. */
void map_share_receiver_add(
	struct map_share_receiver *receiver,
	uint8_t const *data,
	uint32_t length);

/* The stream starts at the file's bytes in hand: the file's own bytes
(offsets the file's), or (`deflate`) a deflate stream from 0. FALSE when
the inflater cannot be had (out of memory). */
int map_share_receiver_start_stream(
	struct map_share_receiver *receiver,
	int deflate);

/* Whether the data message's `offset` and `length` (from the network) are
the stream's next bytes, no longer than a chunk, and what they hold the
file's next bytes, inside it; when so, counts them in (the digests, the
header), has `write` write them, and moves on. */
enum map_share_chunk_status map_share_receiver_accept(
	struct map_share_receiver *receiver,
	int32_t offset,
	int32_t length,
	uint8_t const *data,
	map_share_write_function write,
	void *context);

/* Whether all of the file is here (and a deflate stream ended). */
int map_share_receiver_complete(
	struct map_share_receiver const *receiver);

/* Whether the joiner owes the host an acknowledgement: every
MAP_SHARE_ACK_BYTES of the stream, and at its end. */
int map_share_receiver_ack_due(
	struct map_share_receiver const *receiver);

/* The receiver's inflater freed (any time; again is harmless). */
void map_share_receiver_end(
	struct map_share_receiver *receiver);

/* The SHA-256 of what has arrived so far (the receiver goes on). */
void map_share_receiver_digest_so_far(
	struct map_share_receiver const *receiver,
	uint8_t digest[MAP_SHARE_DIGEST_BYTES]);

/* The file's SHA-256, once all of it has arrived. */
void map_share_receiver_digest(
	struct map_share_receiver *receiver,
	uint8_t digest[MAP_SHARE_DIGEST_BYTES]);

/* Checks a cache header (`header`, MAP_SHARE_HEADER_BYTES) of a file
`file_size` bytes long that should be the multiplayer map `expected_name`:
the signatures, an Xbox (5) or Custom Edition (609) version, its strings
ended, its name the map's (either case), a multiplayer scenario, and its
lengths and tag data inside the file (a Custom Edition cache: uncompressed,
its length no longer than the file; an Xbox cache is compressed on disk and
its decompressed length must fit the cache partition). *custom_edition: the
version was 609. */
enum map_share_header_status map_share_header_validate(
	uint8_t const *header,
	uint32_t file_size,
	char const *expected_name,
	int *custom_edition);

char const *map_share_header_status_describe(
	enum map_share_header_status status);
char const *map_share_refusal_describe(
	enum map_share_refusal reason);

/* A map's fingerprint (custom_edition_cache.c's identity): from the
header's checksum and the file's length when the header has one, else the
CRC-32 of the whole file; never 0. */
uint32_t map_share_identity(
	uint32_t header_checksum,
	uint32_t file_size,
	uint32_t file_crc);

/* The fingerprint of the file received, from its header and running CRC
(once all of it has arrived). */
uint32_t map_share_receiver_identity(
	struct map_share_receiver const *receiver);

/* ---------- a joiner's answer to an offer */

/* the setting "Map downloads" (Multiplayer > Modded maps on the Vita,
network.map_downloads elsewhere: HALO_MAP_SHARE_FROM) */
#define MAP_SHARE_DOWNLOADS_ASK "ask"
#define MAP_SHARE_DOWNLOADS_NOT_PUBLIC "private"
#define MAP_SHARE_DOWNLOADS_NEVER "never"

enum map_share_downloads
{
	/* the player is asked */
	_map_share_downloads_ask = 0,
	/* asked, warned that the host is a public lobby's game's (a stranger's) */
	_map_share_downloads_ask_warning,
	/* not asked: downloads from public games are off */
	_map_share_downloads_refused_public,
	/* not asked: downloads are off */
	_map_share_downloads_refused,
};

/* What a joiner does with its host's offer of a map: `setting` the
setting's value (NULL or anything unknown: ask), `public_game` whether the
game was joined from the public lobby. */
enum map_share_downloads map_share_downloads_policy(
	char const *setting,
	int public_game);

/* The host's name as the question shows it (`text`, `text_size` bytes):
its game's name, `length` UTF-16 units of `name` (up to the first zero),
printable ASCII kept, anything else made '?', spaces at its ends dropped;
"the host" when nothing is left. */
void map_share_host_name_text(
	char *text,
	long text_size,
	uint16_t const *name,
	long length);

/* Starts a deflate stream (MAP_SHARE_DEFLATE_LEVEL); FALSE when its state
cannot be had (out of memory). */
int map_share_packer_begin(
	struct map_share_packer *packer);

/* Deflates the file's next bytes `input` (`input_size`; `last`: the file
ends with them) into `output` (`output_size`). Returns the bytes written to
`output`, and the input bytes it took in *taken (the caller gives the rest
again); `packer->ended` once the stream has ended. A level asked for (0 or
MAP_SHARE_DEFLATE_LEVEL) takes effect once what the stream holds has been
flushed. Negative: zlib failed. */
long map_share_packer_pack(
	struct map_share_packer *packer,
	uint8_t const *input,
	uint32_t input_size,
	int last,
	uint32_t *taken,
	uint8_t *output,
	uint32_t output_size);

void map_share_packer_end(
	struct map_share_packer *packer);

/* A resume record to its bytes (little-endian, a magic and version first),
and back: FALSE when they are not one (another version, a bad name, the
kept part outside the file). */
void map_share_resume_encode(
	struct map_share_resume const *resume,
	uint8_t bytes[MAP_SHARE_RESUME_RECORD_BYTES]);
int map_share_resume_decode(
	uint8_t const bytes[MAP_SHARE_RESUME_RECORD_BYTES],
	struct map_share_resume *resume);

/* p2p_crypto.c's running SHA-256 */
void halo_sha256_begin(
	struct halo_sha256_stream *context);
void halo_sha256_add(
	struct halo_sha256_stream *context,
	void const *data,
	unsigned long size);
void halo_sha256_end(
	struct halo_sha256_stream *context,
	uint8_t digest[MAP_SHARE_DIGEST_BYTES]);

#endif
