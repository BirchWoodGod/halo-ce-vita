/*
MAP_SHARE_PROTOCOL.C

The rules of map sharing that need nothing of the game
(map_share_protocol.h).
*/

#include "map_share_protocol.h"

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
#define SCENARIO_TYPE_MULTIPLAYER 1
/* the cache partition's room for a multiplayer map, which an Xbox map's
decompressed length must stay below (custom_edition_cache.c,
custom_edition_cache_xbox_multiplayer) */
#define XBOX_MULTIPLAYER_MAXIMUM_LENGTH 0x02F00000UL
/* the tag index a cache's tag data starts with is at least this long */
#define MINIMUM_TAG_DATA_BYTES 0x28

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

int map_share_answer_valid(
	struct map_share_answer_message const *answer,
	char const *expected_name,
	uint32_t expected_identity)
{
	char name[MAP_SHARE_NAME_BYTES];

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

	return (uint32_t)answer->identity == expected_identity &&
		answer->identity != 0 &&
		answer->size >= MAP_SHARE_HEADER_BYTES &&
		(uint32_t)answer->size <= MAP_SHARE_MAXIMUM_FILE_BYTES &&
		!(answer->flags & ~(int32_t)((1 << NUMBER_OF_MAP_SHARE_OFFER_FLAGS) - 1));
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

enum map_share_chunk_status map_share_receiver_accept(
	struct map_share_receiver *receiver,
	int32_t offset,
	int32_t length,
	uint8_t const *data)
{
	if (length <= 0 || length > MAP_SHARE_CHUNK_BYTES)
	{
		return _map_share_chunk_bad_length;
	}
	if (offset < 0 || (uint32_t)offset != receiver->received)
	{
		return _map_share_chunk_out_of_order;
	}
	if (!range_inside((uint32_t)offset, (uint32_t)length, receiver->size))
	{
		return _map_share_chunk_past_end;
	}

	if ((uint32_t)offset < MAP_SHARE_HEADER_BYTES)
	{
		uint32_t header_bytes = MAP_SHARE_HEADER_BYTES - (uint32_t)offset;

		if (header_bytes > (uint32_t)length)
		{
			header_bytes = (uint32_t)length;
		}
		memcpy(receiver->header + offset, data, header_bytes);
	}
	halo_sha256_add(&receiver->sha256, data, (unsigned long)length);
	receiver->crc = crc32_update(receiver->crc, data, (uint32_t)length);
	receiver->received += (uint32_t)length;

	return _map_share_chunk_ok;
}

int map_share_receiver_ack_due(
	struct map_share_receiver const *receiver)
{
	return receiver->received - receiver->acknowledged >= MAP_SHARE_ACK_BYTES ||
		(receiver->received == receiver->size && receiver->acknowledged != receiver->size);
}

void map_share_receiver_digest(
	struct map_share_receiver *receiver,
	uint8_t digest[MAP_SHARE_DIGEST_BYTES])
{
	halo_sha256_end(&receiver->sha256, digest);

	return;
}

enum map_share_header_status map_share_header_validate(
	uint8_t const *header,
	uint32_t file_size,
	char const *expected_name,
	int *custom_edition)
{
	int32_t version;
	uint32_t file_length;
	uint32_t tag_data_offset;
	uint32_t tag_data_size;

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
	if ((header[HEADER_SCENARIO_TYPE_OFFSET] | header[HEADER_SCENARIO_TYPE_OFFSET + 1] << 8) != SCENARIO_TYPE_MULTIPLAYER)
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
		"too small",
		"no cache header signatures",
		"not an Xbox or Custom Edition cache",
		"unended header string",
		"named otherwise inside",
		"not a multiplayer map",
		"compressed Custom Edition cache",
		"bad file length",
		"tag data outside the file",
	};

	return (unsigned)status < NUMBER_OF_MAP_SHARE_HEADER_STATUSES ? descriptions[status] : "unknown";
}

char const *map_share_refusal_describe(
	enum map_share_refusal reason)
{
	static char const *const descriptions[NUMBER_OF_MAP_SHARE_REFUSALS] =
	{
		"none",
		"the host does not share that map",
		"the host's copy changed",
		"the map is too large to share",
		"the host is sending maps to other players",
		"the host's game has started",
		"the host could not read the map",
		"cancelled",
		"a bad message",
	};

	return (unsigned)reason < NUMBER_OF_MAP_SHARE_REFUSALS ? descriptions[reason] : "unknown";
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
