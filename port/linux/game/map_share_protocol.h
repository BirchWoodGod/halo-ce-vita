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
  query (name, fingerprint)    ->
                               <-      offer (name, size, fingerprint, .map/.yelo)
                                       or refused (reason)
  [the player says yes]
  start                        ->
                               <-      data (offset, length, bytes) ... as the
                                       window and the host's rate allow
  ack (bytes written)          ->      (every MAP_SHARE_ACK_BYTES)
                               <-      done (SHA-256 of the file)
  ack (all of it)              ->
  cancel (either way, at any time; a host that leaves pregame refuses)
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
/* the bytes a host sends ahead of the joiner's acknowledgement: the
connection's outgoing queue (0x40000) never fills, and the lobby's own
messages to that machine wait behind at most this much */
#define MAP_SHARE_WINDOW_BYTES 0x10000
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
/* a cache header (cache_file_formats.h's CACHE_FILE_HEADER_BYTES) */
#define MAP_SHARE_HEADER_BYTES 0x800
#define MAP_SHARE_DIGEST_BYTES 32

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
	NUMBER_OF_MAP_SHARE_CHUNK_STATUSES
};

/* ---------- structures */

/* The messages, as decoded (network_messages.c defines their fields: the
shorts, then the longs, then the bytes, byte-swapped by the packet codec). */

/* _message_client_map_download */
struct map_share_request
{
	int16_t command;
	int16_t reason;
	/* ack: the bytes written */
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
	int16_t length;
	int16_t reserved;
	int32_t offset;
	uint8_t data[MAP_SHARE_CHUNK_BYTES];
};

/* the running SHA-256 (p2p_crypto.c's, halo_sha256_*), opaque here */
struct halo_sha256_stream
{
	unsigned long long opaque[16];
};

/* what a joiner keeps of the file it receives */
struct map_share_receiver
{
	uint32_t size;
	uint32_t received;
	uint32_t acknowledged;
	/* the cache header, kept as it arrives, checked once whole */
	uint8_t header[MAP_SHARE_HEADER_BYTES];
	int header_checked;
	struct halo_sha256_stream sha256;
	uint32_t crc;
};

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
known flags. */
int map_share_answer_valid(
	struct map_share_answer_message const *answer,
	char const *expected_name,
	uint32_t expected_identity);

/* Starts receiving a file of `size` bytes. */
void map_share_receiver_begin(
	struct map_share_receiver *receiver,
	uint32_t size);

/* Whether the data message's `offset` and `length` (from the network) are
the next bytes of the file, inside it and no longer than a chunk; when so,
counts `data` in (the digests, the header) and moves on. */
enum map_share_chunk_status map_share_receiver_accept(
	struct map_share_receiver *receiver,
	int32_t offset,
	int32_t length,
	uint8_t const *data);

/* Whether the joiner owes the host an acknowledgement: every
MAP_SHARE_ACK_BYTES, and at the end. */
int map_share_receiver_ack_due(
	struct map_share_receiver const *receiver);

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
