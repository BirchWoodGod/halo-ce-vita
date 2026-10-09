/*
MAP_SHARE_PROTOCOL.C

The rules of map sharing that need nothing of the game
(map_share_protocol.h).
*/

#include "map_share_protocol.h"
#include "memory/zlib/zlib.h"
/* (N_ only, lang.h's mark: the descriptions are translated where they are
shown, map_share.c; this file is also built alone, without lang.c) */
#include "../src/lang.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

#define HEADER_SIGNATURE 0x68656164UL /* 'head' */
#define FOOTER_SIGNATURE 0x666F6F74UL /* 'foot' */
#define HEADER_VERSION_OFFSET 0x04
#define HEADER_FILE_LENGTH_OFFSET 0x08
#define HEADER_COMPRESSED_LENGTH_OFFSET 0x0C
#define HEADER_TAG_DATA_OFFSET_OFFSET 0x10
#define HEADER_TAG_DATA_SIZE_OFFSET 0x14
#define HEADER_NAME_OFFSET 0x20
#define HEADER_BUILD_OFFSET 0x40
#define HEADER_STRING_BYTES 0x20
#define HEADER_SCENARIO_TYPE_OFFSET 0x60
#define HEADER_CHECKSUM_OFFSET 0x64
#define HEADER_FOOTER_OFFSET 0x7FC
#define VERSION_XBOX 5
#define VERSION_CUSTOM_EDITION 609
#define SCENARIO_TYPE_SOLO 0
#define SCENARIO_TYPE_MULTIPLAYER 1
/* the cache partition's room for a multiplayer map, which an Xbox map's
decompressed length must stay below (custom_edition_cache.c,
custom_edition_cache_xbox_multiplayer) */
#define XBOX_MULTIPLAYER_MAXIMUM_LENGTH 0x02F00000UL
/* the tag index a cache's tag data starts with is at least this long */
#define MINIMUM_TAG_DATA_BYTES 0x28
/* a resume record's first bytes */
#define RESUME_MAGIC 0x52534D48UL /* 'HMSR' */
#define RESUME_VERSION 1
/* the offer flags of a file's kind */
#define KIND_FLAGS ((1 << _map_share_offer_yelo_bit) | (1 << _map_share_offer_custom_edition_bit))

/* (the capabilities a query's or start's reason carries stay valid reasons
to a host without them, which then takes the request) */
typedef char map_share_capabilities_are_reasons[
	(1 << NUMBER_OF_MAP_SHARE_CAPABILITIES) - 1 < NUMBER_OF_MAP_SHARE_REFUSALS ? 1 : -1];

/* ---------- globals */

/* the Xbox's own levels (custom_edition_maps.c's list): every machine has
its own copy, PAL and NTSC copies play together, and none is ever sent */
static char const *const stock_map_names[] =
{
	"beavercreek", "sidewinder", "damnation", "ratrace", "prisoner", "hangemhigh", "chillout",
	"carousel", "boardingaction", "bloodgulch", "wizard", "putput", "longest",
	"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
};

/* names a shared map may not have: Custom Edition's resource maps, and the
Xbox's user interface map */
static char const *const reserved_map_names[] =
{
	"bitmaps", "sounds", "loc", "ui",
};

/* names Windows keeps for devices, with any extension ("con.map" is the
console there): never a file the Windows build writes */
static char const *const device_names[] =
{
	"con", "prn", "aux", "nul",
	"com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
	"lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
};

/* ---------- private code */

static int lower(
	int character)
{
	return character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character;
}

static int names_equal(
	char const *first,
	char const *second)
{
	while (*first && lower((unsigned char)*first) == lower((unsigned char)*second))
	{
		first++;
		second++;
	}

	return lower((unsigned char)*first) == lower((unsigned char)*second);
}

static int name_in(
	char const *name,
	char const *const *names,
	unsigned count)
{
	unsigned index;

	for (index = 0; index < count; index++)
	{
		if (names_equal(name, names[index]))
		{
			return 1;
		}
	}

	return 0;
}

static uint32_t read_u32(
	uint8_t const *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void write_u32(
	uint8_t *bytes,
	uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);

	return;
}

/* zlib's memory: the C library's (never the game's own allocator, which
is the game thread's) */
static voidpf zlib_allocate(
	voidpf opaque,
	uInt items,
	uInt size)
{
	(void)opaque;

	return calloc(items, size);
}

static void zlib_free(
	voidpf opaque,
	voidpf pointer)
{
	(void)opaque;
	free(pointer);

	return;
}

static z_stream *zlib_stream_new(
	void)
{
	z_stream *stream = calloc(1, sizeof(z_stream));

	if (stream)
	{
		stream->zalloc = zlib_allocate;
		stream->zfree = zlib_free;
		stream->opaque = Z_NULL;
	}

	return stream;
}

/* whether `size` bytes at `offset` lie within `limit` bytes (no overflow) */
static int range_inside(
	uint32_t offset,
	uint32_t size,
	uint32_t limit)
{
	return offset <= limit && size <= limit - offset;
}

/* whether the header's string at `offset` ends within its field */
static int string_field_ended(
	uint8_t const *header,
	unsigned offset)
{
	return memchr(header + offset, 0, HEADER_STRING_BYTES) != NULL;
}

/* the CRC-32 zlib computes (crc32(), reflected 0xEDB88320) */
static uint32_t crc32_update(
	uint32_t crc,
	uint8_t const *data,
	uint32_t size)
{
	static uint32_t table[256];
	static int table_made;
	uint32_t index;

	if (!table_made)
	{
		for (index = 0; index < 256; index++)
		{
			uint32_t value = index;
			int bit;

			for (bit = 0; bit < 8; bit++)
			{
				value = value & 1 ? 0xEDB88320UL ^ (value >> 1) : value >> 1;
			}
			table[index] = value;
		}
		table_made = 1;
	}
	crc ^= 0xFFFFFFFFUL;
	for (index = 0; index < size; index++)
	{
		crc = table[(crc ^ data[index]) & 0xFF] ^ (crc >> 8);
	}

	return crc ^ 0xFFFFFFFFUL;
}

/* ---------- public code */

int map_share_name_is_stock(
	char const *name)
{
	return name && name_in(name, stock_map_names, sizeof(stock_map_names) / sizeof(stock_map_names[0]));
}

int map_share_name_valid(
	char const *name)
{
	unsigned length;

	if (!name || !name[0] || name[0] == '.')
	{
		return 0;
	}
	for (length = 0; name[length]; length++)
	{
		char character = name[length];

		if (length >= MAP_SHARE_MAXIMUM_NAME_LENGTH)
		{
			return 0;
		}
		/* printable ASCII, but what a path or a file system takes for
		something else (Windows' and FAT's: the Vita's memory card), ','
		(HALO_MAPS_DISABLED's list of maps turned off) and '%' (a name is
		never a format, but nor is it one by mistake): Custom Edition maps
		go about as "Race-Track-#1" and "[h3] coldsnap" */
		if (character < ' ' || character > '~' || strchr("/\\:*?\"<>|%,", character))
		{
			return 0;
		}
		if (character == '.' && name[length + 1] == '.')
		{
			return 0;
		}
	}
	/* (a trailing dot or space is dropped by some file systems: another
	file; a leading space, kept by some and not others) */
	if (name[length - 1] == '.' || name[length - 1] == ' ' || name[0] == ' ')
	{
		return 0;
	}

	{
		/* (a device's name before the first dot) */
		char stem[MAP_SHARE_NAME_BYTES];
		unsigned stem_length = 0;

		while (name[stem_length] && name[stem_length] != '.')
		{
			stem[stem_length] = name[stem_length];
			stem_length++;
		}
		stem[stem_length] = 0;
		if (name_in(stem, device_names, sizeof(device_names) / sizeof(device_names[0])))
		{
			return 0;
		}
	}

	return !map_share_name_is_stock(name) &&
		!name_in(name, reserved_map_names, sizeof(reserved_map_names) / sizeof(reserved_map_names[0]));
}

int map_share_name_from_field(
	char const field[MAP_SHARE_NAME_BYTES],
	char name[MAP_SHARE_NAME_BYTES])
{
	if (!memchr(field, 0, MAP_SHARE_NAME_BYTES))
	{
		name[0] = 0;
		return 0;
	}
	memcpy(name, field, MAP_SHARE_NAME_BYTES);
	if (!map_share_name_valid(name))
	{
		name[0] = 0;
		return 0;
	}

	return 1;
}

int map_share_request_valid(
	struct map_share_request const *request)
{
	char name[MAP_SHARE_NAME_BYTES];

	if (request->command < _map_share_command_query || request->command >= NUMBER_OF_MAP_SHARE_COMMANDS ||
		request->reason < 0 || request->reason >= NUMBER_OF_MAP_SHARE_REFUSALS ||
		request->offset < 0 || (uint32_t)request->offset > MAP_SHARE_MAXIMUM_FILE_BYTES)
	{
		return 0;
	}

	return map_share_name_from_field(request->name, name);
}

uint32_t map_share_request_capabilities(
	struct map_share_request const *request)
{
	if (request->command != _map_share_command_query && request->command != _map_share_command_start)
	{
		return 0;
	}

	return (uint32_t)request->reason & ((1u << NUMBER_OF_MAP_SHARE_CAPABILITIES) - 1);
}

int map_share_answer_valid(
	struct map_share_answer_message const *answer,
	char const *expected_name,
	uint32_t expected_identity,
	uint32_t capabilities)
{
	char name[MAP_SHARE_NAME_BYTES];
	uint32_t known_flags = KIND_FLAGS;

	if (answer->kind < _map_share_answer_offer || answer->kind >= NUMBER_OF_MAP_SHARE_ANSWERS ||
		answer->reason < 0 || answer->reason >= NUMBER_OF_MAP_SHARE_REFUSALS ||
		!map_share_name_from_field(answer->name, name) ||
		!names_equal(name, expected_name))
	{
		return 0;
	}
	if (answer->kind == _map_share_answer_refused)
	{
		return 1;
	}
	if (capabilities & 1u << _map_share_capability_resume_bit)
	{
		known_flags |= 1u << _map_share_offer_resume_bit;
	}
	if (capabilities & 1u << _map_share_capability_deflate_bit)
	{
		known_flags |= 1u << _map_share_offer_deflate_bit;
	}
	if (capabilities & 1u << _map_share_capability_in_progress_bit)
	{
		known_flags |= 1u << _map_share_offer_in_progress_bit;
	}

	return (uint32_t)answer->identity == expected_identity &&
		answer->identity != 0 &&
		answer->size >= MAP_SHARE_HEADER_BYTES &&
		(uint32_t)answer->size <= MAP_SHARE_MAXIMUM_FILE_BYTES &&
		!((uint32_t)answer->flags & ~known_flags);
}

enum map_share_refusal map_share_host_serves(
	enum map_share_host_state host_state,
	uint32_t capabilities,
	int machine_joining,
	int accepts_late_joins,
	int in_progress_allowed,
	int *in_progress)
{
	*in_progress = 0;
	if (host_state == _map_share_host_lobby)
	{
		return _map_share_refusal_none;
	}
	if (host_state != _map_share_host_in_game || !(capabilities & 1u << _map_share_capability_in_progress_bit) ||
		!machine_joining || !accepts_late_joins || !in_progress_allowed)
	{
		return _map_share_refusal_not_in_lobby;
	}
	*in_progress = 1;

	return _map_share_refusal_none;
}

void map_share_host_limits(
	int in_game,
	uint32_t rate_kb,
	uint32_t ingame_rate_kb,
	uint32_t cpu_percent,
	uint32_t frame_microseconds,
	struct map_share_limits *limits)
{
	/* (KB/s past this would overflow the bytes: no link is that fast) */
	const uint32_t most_kb = 0x100000;
	uint32_t lobby_rate = rate_kb ? (rate_kb < most_kb ? rate_kb : most_kb) * 1024u : MAP_SHARE_DEFAULT_BYTES_PER_SECOND;

	if (!in_game)
	{
		limits->bytes_per_second = lobby_rate;
		limits->window_bytes = MAP_SHARE_WINDOW_BYTES;
		limits->cpu_percent = cpu_percent ? cpu_percent : MAP_SHARE_DEFAULT_CPU_PERCENT;
		limits->frame_microseconds = frame_microseconds ? frame_microseconds : 15000;
	}
	else
	{
		limits->bytes_per_second = ingame_rate_kb ?
			(ingame_rate_kb < most_kb ? ingame_rate_kb : most_kb) * 1024u : MAP_SHARE_INGAME_BYTES_PER_SECOND;
		if (limits->bytes_per_second > lobby_rate)
		{
			limits->bytes_per_second = lobby_rate;
		}
		limits->window_bytes = MAP_SHARE_INGAME_WINDOW_BYTES;
		limits->cpu_percent = cpu_percent ? cpu_percent : MAP_SHARE_INGAME_CPU_PERCENT;
		limits->frame_microseconds = frame_microseconds ? frame_microseconds : MAP_SHARE_INGAME_FRAME_MICROSECONDS;
	}
	/* (at least a chunk a second, a percent and a millisecond: never stuck;
	a share of the time no more than all of it) */
	if (limits->bytes_per_second < MAP_SHARE_CHUNK_BYTES)
	{
		limits->bytes_per_second = MAP_SHARE_CHUNK_BYTES;
	}
	if (limits->cpu_percent > 100)
	{
		limits->cpu_percent = 100;
	}
	if (limits->frame_microseconds < 1000)
	{
		limits->frame_microseconds = 1000;
	}
	if (limits->frame_microseconds > 100000)
	{
		limits->frame_microseconds = 100000;
	}

	return;
}

void map_share_receiver_begin(
	struct map_share_receiver *receiver,
	uint32_t size)
{
	memset(receiver, 0, sizeof(*receiver));
	receiver->size = size;
	halo_sha256_begin(&receiver->sha256);

	return;
}

void map_share_receiver_add(
	struct map_share_receiver *receiver,
	uint8_t const *data,
	uint32_t length)
{
	uint32_t offset = receiver->received;

	if (offset < MAP_SHARE_HEADER_BYTES)
	{
		uint32_t header_bytes = MAP_SHARE_HEADER_BYTES - offset;

		if (header_bytes > length)
		{
			header_bytes = length;
		}
		memcpy(receiver->header + offset, data, header_bytes);
	}
	halo_sha256_add(&receiver->sha256, data, (unsigned long)length);
	/* (the CRC is the fingerprint of a map whose header has no checksum,
	map_share_identity: of any other, once its header is here, it is not
	needed) */
	if (offset < MAP_SHARE_HEADER_BYTES ||
		!read_u32(receiver->header + HEADER_CHECKSUM_OFFSET) ||
		read_u32(receiver->header + HEADER_CHECKSUM_OFFSET) == 0xFFFFFFFFUL)
	{
		receiver->crc = crc32_update(receiver->crc, data, length);
	}
	receiver->received += length;

	return;
}

int map_share_receiver_start_stream(
	struct map_share_receiver *receiver,
	int deflate)
{
	map_share_receiver_end(receiver);
	receiver->stream_ended = 0;
	receiver->stream_received = deflate ? 0 : receiver->received;
	receiver->acknowledged = receiver->stream_received;
	if (deflate)
	{
		z_stream *stream = zlib_stream_new();

		if (!stream)
		{
			return 0;
		}
		if (inflateInit2(stream, MAP_SHARE_DEFLATE_WINDOW_BITS) != Z_OK)
		{
			free(stream);
			return 0;
		}
		receiver->inflater = stream;
	}

	return 1;
}

/* the file's next bytes, from the stream: inside the file, counted in,
written */
static enum map_share_chunk_status map_share_receiver_take(
	struct map_share_receiver *receiver,
	uint8_t const *data,
	uint32_t length,
	map_share_write_function write,
	void *context)
{
	if (!range_inside(receiver->received, length, receiver->size))
	{
		return _map_share_chunk_past_end;
	}
	map_share_receiver_add(receiver, data, length);
	if (write && !write(context, data, length))
	{
		return _map_share_chunk_write_failed;
	}

	return _map_share_chunk_ok;
}

enum map_share_chunk_status map_share_receiver_accept(
	struct map_share_receiver *receiver,
	int32_t offset,
	int32_t length,
	uint8_t const *data,
	map_share_write_function write,
	void *context)
{
	z_stream *stream = receiver->inflater;
	enum map_share_chunk_status status;

	if (length <= 0 || length > MAP_SHARE_CHUNK_BYTES)
	{
		return _map_share_chunk_bad_length;
	}
	if (offset < 0 || (uint32_t)offset != receiver->stream_received)
	{
		return _map_share_chunk_out_of_order;
	}
	if (!stream)
	{
		status = map_share_receiver_take(receiver, data, (uint32_t)length, write, context);
		if (status == _map_share_chunk_ok)
		{
			receiver->stream_received += (uint32_t)length;
		}
		return status;
	}

	/* inflated a piece at a time (the zlib stream's end, then nothing, must
	come with the file's last byte) */
	if (receiver->stream_ended)
	{
		return _map_share_chunk_bad_stream;
	}
	stream->next_in = (Bytef *)data;
	stream->avail_in = (uInt)length;
	for (;;)
	{
		/* (static: 16 KB, and one frame receives at a time) */
		static uint8_t output[0x4000];
		uint32_t produced;
		int result;

		stream->next_out = output;
		stream->avail_out = sizeof(output);
		result = inflate(stream, Z_SYNC_FLUSH);
		produced = (uint32_t)(sizeof(output) - stream->avail_out);
		if (result != Z_OK && result != Z_STREAM_END && !(result == Z_BUF_ERROR && !produced))
		{
			return _map_share_chunk_bad_stream;
		}
		if (produced)
		{
			status = map_share_receiver_take(receiver, output, produced, write, context);
			if (status != _map_share_chunk_ok)
			{
				return status == _map_share_chunk_past_end ? _map_share_chunk_bad_stream : status;
			}
		}
		if (result == Z_STREAM_END)
		{
			receiver->stream_ended = 1;
			if (stream->avail_in || receiver->received != receiver->size)
			{
				return _map_share_chunk_bad_stream;
			}
			break;
		}
		if (!stream->avail_in && stream->avail_out)
		{
			break;
		}
		if (result == Z_BUF_ERROR)
		{
			/* (no progress with input and room left) */
			return _map_share_chunk_bad_stream;
		}
	}
	receiver->stream_received += (uint32_t)length;

	return _map_share_chunk_ok;
}

int map_share_receiver_complete(
	struct map_share_receiver const *receiver)
{
	return receiver->received == receiver->size && (!receiver->inflater || receiver->stream_ended);
}

int map_share_receiver_ack_due(
	struct map_share_receiver const *receiver)
{
	return receiver->stream_received - receiver->acknowledged >= MAP_SHARE_ACK_BYTES ||
		(map_share_receiver_complete(receiver) && receiver->acknowledged != receiver->stream_received);
}

void map_share_receiver_end(
	struct map_share_receiver *receiver)
{
	if (receiver->inflater)
	{
		inflateEnd(receiver->inflater);
		free(receiver->inflater);
		receiver->inflater = NULL;
	}

	return;
}

void map_share_receiver_digest(
	struct map_share_receiver *receiver,
	uint8_t digest[MAP_SHARE_DIGEST_BYTES])
{
	halo_sha256_end(&receiver->sha256, digest);

	return;
}

void map_share_receiver_digest_so_far(
	struct map_share_receiver const *receiver,
	uint8_t digest[MAP_SHARE_DIGEST_BYTES])
{
	struct halo_sha256_stream copy = receiver->sha256;

	halo_sha256_end(&copy, digest);

	return;
}

enum map_share_header_status map_share_header_validate(
	uint8_t const *header,
	uint32_t file_size,
	char const *expected_name,
	int *custom_edition)
{
	return map_share_header_validate_level(header, file_size, expected_name, 0, custom_edition);
}

enum map_share_header_status map_share_header_validate_level(
	uint8_t const *header,
	uint32_t file_size,
	char const *expected_name,
	int campaign,
	int *custom_edition)
{
	int32_t version;
	uint32_t file_length;
	uint32_t tag_data_offset;
	uint32_t tag_data_size;
	int scenario_type;

	*custom_edition = 0;
	if (file_size < MAP_SHARE_HEADER_BYTES || file_size > MAP_SHARE_MAXIMUM_FILE_BYTES)
	{
		return file_size < MAP_SHARE_HEADER_BYTES ? _map_share_header_too_small : _map_share_header_bad_length;
	}
	if (read_u32(header) != HEADER_SIGNATURE || read_u32(header + HEADER_FOOTER_OFFSET) != FOOTER_SIGNATURE)
	{
		return _map_share_header_bad_signatures;
	}
	version = (int32_t)read_u32(header + HEADER_VERSION_OFFSET);
	if (version != VERSION_XBOX && version != VERSION_CUSTOM_EDITION)
	{
		return _map_share_header_unsupported_version;
	}
	*custom_edition = version == VERSION_CUSTOM_EDITION;
	if (!string_field_ended(header, HEADER_NAME_OFFSET) || !string_field_ended(header, HEADER_BUILD_OFFSET))
	{
		return _map_share_header_unterminated_string;
	}
	/* an Xbox map is known by the name inside it (the cache partition finds
	its copy by it: custom_edition_cache_xbox_multiplayer); a Custom Edition
	map by its file's, which may differ from the scenario's inside */
	if (!*custom_edition && !names_equal((char const *)header + HEADER_NAME_OFFSET, expected_name))
	{
		return _map_share_header_wrong_name;
	}
	/* (a network co-op game's Custom Edition campaign map: a solo scenario;
	never an Xbox one, which only the cache partition's multiplayer maps
	are) */
	scenario_type = header[HEADER_SCENARIO_TYPE_OFFSET] | header[HEADER_SCENARIO_TYPE_OFFSET + 1] << 8;
	if (scenario_type != SCENARIO_TYPE_MULTIPLAYER &&
		!(campaign && *custom_edition && scenario_type == SCENARIO_TYPE_SOLO))
	{
		return _map_share_header_not_multiplayer;
	}

	file_length = read_u32(header + HEADER_FILE_LENGTH_OFFSET);
	tag_data_offset = read_u32(header + HEADER_TAG_DATA_OFFSET_OFFSET);
	tag_data_size = read_u32(header + HEADER_TAG_DATA_SIZE_OFFSET);
	if (*custom_edition)
	{
		/* read in place: every byte the header points at is in the file */
		if (read_u32(header + HEADER_COMPRESSED_LENGTH_OFFSET))
		{
			return _map_share_header_compressed;
		}
		if (file_length < MAP_SHARE_HEADER_BYTES || file_length > file_size)
		{
			return _map_share_header_bad_length;
		}
	}
	else if (file_length < MAP_SHARE_HEADER_BYTES || file_length >= XBOX_MULTIPLAYER_MAXIMUM_LENGTH)
	{
		/* decompressed into the cache partition, where the offsets point */
		return _map_share_header_bad_length;
	}
	if (tag_data_offset < MAP_SHARE_HEADER_BYTES ||
		tag_data_size < MINIMUM_TAG_DATA_BYTES ||
		!range_inside(tag_data_offset, tag_data_size, file_length))
	{
		return _map_share_header_bad_tag_data;
	}

	return _map_share_header_ok;
}

char const *map_share_header_status_describe(
	enum map_share_header_status status)
{
	static char const *const descriptions[NUMBER_OF_MAP_SHARE_HEADER_STATUSES] =
	{
		"ok",
		N_("too small"),
		N_("no cache header signatures"),
		N_("not an Xbox or Custom Edition cache"),
		N_("unended header string"),
		N_("named otherwise inside"),
		N_("not a multiplayer map"),
		N_("compressed Custom Edition cache"),
		N_("bad file length"),
		N_("tag data outside the file"),
	};

	return (unsigned)status < NUMBER_OF_MAP_SHARE_HEADER_STATUSES ? descriptions[status] : N_("unknown");
}

char const *map_share_refusal_describe(
	enum map_share_refusal reason)
{
	static char const *const descriptions[NUMBER_OF_MAP_SHARE_REFUSALS] =
	{
		"none",
		N_("the host does not share that map"),
		N_("the host's copy changed"),
		N_("the map is too large to share"),
		N_("the host is sending maps to other players"),
		N_("the host's game has started"),
		N_("the host could not read the map"),
		N_("cancelled"),
		N_("a bad message"),
	};

	return (unsigned)reason < NUMBER_OF_MAP_SHARE_REFUSALS ? descriptions[reason] : N_("unknown");
}

uint32_t map_share_identity(
	uint32_t header_checksum,
	uint32_t file_size,
	uint32_t file_crc)
{
	uint32_t identity;

	if (header_checksum && header_checksum != 0xFFFFFFFFUL)
	{
		/* the header's checksum, as Bungie's tools, Custom Edition's and
		Invader's write it, with the length the file has */
		identity = header_checksum ^ (uint32_t)(file_size * 0x9E3779B1UL);
	}
	else
	{
		/* none (Invader's Xbox maps): the CRC-32 of the whole file */
		identity = file_crc;
	}

	return identity ? identity : 1;
}

uint32_t map_share_receiver_identity(
	struct map_share_receiver const *receiver)
{
	return map_share_identity(read_u32(receiver->header + HEADER_CHECKSUM_OFFSET), receiver->size, receiver->crc);
}

enum map_share_downloads map_share_downloads_policy(
	char const *setting,
	int public_game)
{
	if (setting && !strcmp(setting, MAP_SHARE_DOWNLOADS_NEVER))
	{
		return _map_share_downloads_refused;
	}
	if (public_game)
	{
		return setting && !strcmp(setting, MAP_SHARE_DOWNLOADS_NOT_PUBLIC) ?
			_map_share_downloads_refused_public : _map_share_downloads_ask_warning;
	}

	return _map_share_downloads_ask;
}

void map_share_host_name_text(
	char *text,
	long text_size,
	uint16_t const *name,
	long length)
{
	long index, used = 0;

	if (text_size <= 0)
	{
		return;
	}
	for (index = 0; name && index < length && name[index] && used < text_size - 1; index++)
	{
		uint16_t character = name[index];

		if (!used && character == ' ')
		{
			continue;
		}
		text[used++] = character >= 0x20 && character < 0x7F ? (char)character : '?';
	}
	while (used > 0 && text[used - 1] == ' ')
	{
		used--;
	}
	text[used] = 0;
	if (!used)
	{
		snprintf(text, (size_t)text_size, "the host");
	}

	return;
}

int map_share_packer_begin(
	struct map_share_packer *packer)
{
	z_stream *stream = zlib_stream_new();

	memset(packer, 0, sizeof(*packer));
	if (!stream)
	{
		return 0;
	}
	if (deflateInit2(stream, MAP_SHARE_DEFLATE_LEVEL, Z_DEFLATED, MAP_SHARE_DEFLATE_WINDOW_BITS,
		MAP_SHARE_DEFLATE_MEMORY_LEVEL, Z_DEFAULT_STRATEGY) != Z_OK)
	{
		free(stream);
		return 0;
	}
	packer->deflater = stream;
	packer->level = packer->stream_level = MAP_SHARE_DEFLATE_LEVEL;

	return 1;
}

long map_share_packer_pack(
	struct map_share_packer *packer,
	uint8_t const *input,
	uint32_t input_size,
	int last,
	uint32_t *taken,
	uint8_t *output,
	uint32_t output_size)
{
	z_stream *stream = packer->deflater;
	int result;

	*taken = 0;
	if (!stream || packer->ended)
	{
		return stream ? 0 : -1;
	}
	stream->next_out = output;
	stream->avail_out = (uInt)output_size;
	/* (not once the stream is finishing: deflate takes only Z_FINISH then) */
	if (packer->level != packer->stream_level && !packer->finishing)
	{
		/* everything the stream holds flushed first (deflateParams flushes
		only what its room takes, and the new level's function would take
		up the rest): done once the flush leaves room */
		stream->next_in = (Bytef *)input;
		stream->avail_in = 0;
		result = deflate(stream, Z_SYNC_FLUSH);
		if (result != Z_OK && result != Z_BUF_ERROR)
		{
			return -1;
		}
		if (!stream->avail_out)
		{
			return (long)output_size;
		}
		result = deflateParams(stream, packer->level, Z_DEFAULT_STRATEGY);
		if (result != Z_OK && result != Z_BUF_ERROR)
		{
			return -1;
		}
		packer->stream_level = packer->level;
	}
	stream->next_in = (Bytef *)input;
	stream->avail_in = (uInt)input_size;
	packer->finishing |= last;
	result = deflate(stream, last ? Z_FINISH : Z_NO_FLUSH);
	if (result == Z_STREAM_END)
	{
		packer->ended = 1;
	}
	else if (result != Z_OK && result != Z_BUF_ERROR)
	{
		return -1;
	}
	*taken = input_size - (uint32_t)stream->avail_in;

	return (long)(output_size - stream->avail_out);
}

void map_share_packer_end(
	struct map_share_packer *packer)
{
	if (packer->deflater)
	{
		deflateEnd(packer->deflater);
		free(packer->deflater);
		packer->deflater = NULL;
	}

	return;
}

void map_share_resume_encode(
	struct map_share_resume const *resume,
	uint8_t bytes[MAP_SHARE_RESUME_RECORD_BYTES])
{
	uint8_t *at = bytes;

	memset(bytes, 0, MAP_SHARE_RESUME_RECORD_BYTES);
	write_u32(at, RESUME_MAGIC);
	write_u32(at + 4, RESUME_VERSION);
	at += 8;
	memcpy(at, resume->name, MAP_SHARE_NAME_BYTES);
	at[MAP_SHARE_NAME_BYTES - 1] = 0;
	at += MAP_SHARE_NAME_BYTES;
	write_u32(at, resume->identity);
	write_u32(at + 4, resume->size);
	write_u32(at + 8, resume->flags);
	write_u32(at + 12, resume->kept);
	write_u32(at + 16, resume->saved_time);
	at += 20;
	memcpy(at, resume->kept_digest, MAP_SHARE_DIGEST_BYTES);

	return;
}

int map_share_resume_decode(
	uint8_t const bytes[MAP_SHARE_RESUME_RECORD_BYTES],
	struct map_share_resume *resume)
{
	uint8_t const *at = bytes + 8;

	memset(resume, 0, sizeof(*resume));
	if (read_u32(bytes) != RESUME_MAGIC || read_u32(bytes + 4) != RESUME_VERSION ||
		!map_share_name_from_field((char const *)at, resume->name))
	{
		return 0;
	}
	at += MAP_SHARE_NAME_BYTES;
	resume->identity = read_u32(at);
	resume->size = read_u32(at + 4);
	resume->flags = read_u32(at + 8);
	resume->kept = read_u32(at + 12);
	resume->saved_time = read_u32(at + 16);
	at += 20;
	memcpy(resume->kept_digest, at, MAP_SHARE_DIGEST_BYTES);

	return resume->identity != 0 &&
		resume->size >= MAP_SHARE_HEADER_BYTES && resume->size <= MAP_SHARE_MAXIMUM_FILE_BYTES &&
		!(resume->flags & ~(uint32_t)KIND_FLAGS) &&
		resume->kept <= resume->size;
}
