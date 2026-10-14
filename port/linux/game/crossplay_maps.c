/*
CROSSPLAY_MAPS.C

A PC host's map, as the crossplay wire names it, and whether this machine
plays it (crossplay_maps.h). The names and their map.version are OpenCE's
since its network version 22 (4e8ed2f1 "Tell a joining player which map
they are missing", 9d309f8d "Refuse a Custom Edition map of another version
than the host's"; cache_files_map_version and custom_edition_map_checksum at
6399ed9d), as DamnationCE (7be58b2b) has them, and ChupathingyCE's families
(halo_map_families.h, map_family_wire_name, 8cb79900); the hashed names of
OpenCE's network version 27 (a2aad18f, map_share_files.c's
map_share_name_hashed and its file hash, as at b1ef7f7e), as ChupathingyCE
plays them from a map of the base name (a8cb4905); all CC0.
*/

#include "crossplay_maps.h"
#include "map_share_protocol.h"
#include "../../third_party/monocypher/monocypher.h" /* (BLAKE2b, the map hash) */

#include <stddef.h>
#include <string.h>

/* ---------- private code */

static int lower(
	int character)
{
	return character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character;
}

/* whether `name` starts with `prefix`, letters of either case (OpenCE's
_strnicmp) */
static int prefix_is(
	char const *name,
	char const *prefix)
{
	while (*prefix)
	{
		if (lower((unsigned char)*name) != lower((unsigned char)*prefix))
		{
			return 0;
		}
		name++;
		prefix++;
	}

	return 1;
}

/* whether `name` (`length` characters) ends with `suffix`, letters of
either case */
static int suffix_is(
	char const *name,
	size_t length,
	char const *suffix)
{
	size_t suffix_length = strlen(suffix);

	return length >= suffix_length && prefix_is(name + length - suffix_length, suffix);
}

/* whether `name` (`length` characters) ends in '_' and
CROSSPLAY_MAP_HASH_DIGITS lowercase hexadecimal digits, with a character
before them (OpenCE's map_share_name_hashed: its digits are written in
lowercase, and an uppercase one is no hash, so the name is a plain one) */
static int hashed_end(
	char const *name,
	size_t length,
	unsigned long *prefix)
{
	unsigned long value = 0;
	size_t index;

	if (length < CROSSPLAY_MAP_HASH_DIGITS + 2 || name[length - CROSSPLAY_MAP_HASH_DIGITS - 1] != '_')
	{
		return 0;
	}
	for (index = length - CROSSPLAY_MAP_HASH_DIGITS; index < length; index++)
	{
		char character = name[index];

		if (character >= '0' && character <= '9')
		{
			value = (value << 4) | (unsigned long)(character - '0');
		}
		else if (character >= 'a' && character <= 'f')
		{
			value = (value << 4) | (unsigned long)(character - 'a' + 10);
		}
		else
		{
			return 0;
		}
	}
	if (prefix)
	{
		*prefix = value & 0xFFFFFFFFUL;
	}

	return 1;
}

/* whether `file` is a name a map file may have here: map sharing's rules,
which keep out the characters a file system takes for something else,
device names and the resource maps' and ui's names; the Xbox's levels'
names besides, which a Custom Edition map may have (Halo PC's Blood Gulch
is custom_maps\bloodgulch) */
static int file_name_taken(
	char const *file)
{
	return map_share_name_valid(file) || map_share_name_is_stock(file);
}

/* ---------- public code */

int crossplay_map_name_hashed(
	char const *file,
	char base[CROSSPLAY_MAP_FILE_BYTES],
	unsigned long *prefix)
{
	size_t length = file ? strlen(file) : 0;
	int hashed;

	if (prefix)
	{
		*prefix = 0;
	}
	if (length >= CROSSPLAY_MAP_FILE_BYTES)
	{
		if (base)
		{
			base[0] = 0;
		}
		return 0;
	}
	hashed = hashed_end(file, length, prefix);
	if (base)
	{
		length = hashed ? length - CROSSPLAY_MAP_HASH_DIGITS - 1 : length;
		memcpy(base, file ? file : "", length);
		base[length] = 0;
	}

	return hashed;
}

int crossplay_map_file_hash(
	crossplay_map_read_proc read,
	void *context,
	uint32_t size,
	void *buffer,
	uint32_t buffer_size,
	unsigned char hash[CROSSPLAY_MAP_HASH_BYTES])
{
	static char const label[] = "OpenCE map 1";
	unsigned char size_bytes[4];
	unsigned char piece_hash[CROSSPLAY_MAP_HASH_BYTES];
	crypto_blake2b_ctx whole;
	uint32_t offset;

	memset(hash, 0, CROSSPLAY_MAP_HASH_BYTES);
	if (!read || !buffer || !buffer_size)
	{
		return 0;
	}
	size_bytes[0] = (unsigned char)size;
	size_bytes[1] = (unsigned char)(size >> 8);
	size_bytes[2] = (unsigned char)(size >> 16);
	size_bytes[3] = (unsigned char)(size >> 24);
	crypto_blake2b_init(&whole, CROSSPLAY_MAP_HASH_BYTES);
	crypto_blake2b_update(&whole, (unsigned char const *)label, sizeof(label) - 1);
	crypto_blake2b_update(&whole, size_bytes, sizeof(size_bytes));
	/* (each piece hashed as it is read, a buffer at a time: no piece held
	whole) */
	for (offset = 0; offset < size; )
	{
		uint32_t piece_end = size - offset < CROSSPLAY_MAP_HASH_PIECE_BYTES ? size : offset + CROSSPLAY_MAP_HASH_PIECE_BYTES;
		crypto_blake2b_ctx piece;

		crypto_blake2b_init(&piece, CROSSPLAY_MAP_HASH_BYTES);
		while (offset < piece_end)
		{
			uint32_t count = piece_end - offset < buffer_size ? piece_end - offset : buffer_size;

			if (!read(context, offset, count, buffer))
			{
				crypto_wipe(&piece, sizeof(piece));
				crypto_wipe(&whole, sizeof(whole));
				return 0;
			}
			crypto_blake2b_update(&piece, buffer, count);
			offset += count;
		}
		crypto_blake2b_final(&piece, piece_hash);
		crypto_blake2b_update(&whole, piece_hash, sizeof(piece_hash));
	}
	crypto_blake2b_final(&whole, hash);

	return 1;
}

unsigned long crossplay_map_hash_prefix(
	unsigned char const hash[CROSSPLAY_MAP_HASH_BYTES])
{
	return (unsigned long)hash[0] << 24 | (unsigned long)hash[1] << 16 | (unsigned long)hash[2] << 8 |
		(unsigned long)hash[3];
}

int crossplay_map_file_is_stock(
	char const *file)
{
	return file && map_share_name_is_stock(file);
}

short crossplay_map_name_parse(
	char const *name,
	long size,
	char file[CROSSPLAY_MAP_FILE_BYTES])
{
	char const *end;
	char const *leaf;
	char const *extension = NULL;
	size_t length;
	size_t leaf_length;
	size_t index;
	short kind;

	file[0] = 0;
	if (!name || size <= 0 || !(end = memchr(name, 0, (size_t)size)))
	{
		return _crossplay_map_kind_invalid;
	}
	length = (size_t)(end - name);
	if (!length || length > CROSSPLAY_MAP_MAXIMUM_NAME_LENGTH)
	{
		return _crossplay_map_kind_invalid;
	}
	/* (printable ASCII; no other separator than the tag path's backslash, no
	drive, nothing climbing a folder: the record's own check,
	network_game_client_map_name_is_valid, as well) */
	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)name[index];

		if (character < 0x20 || character > 0x7E || character == '/' || character == ':')
		{
			return _crossplay_map_kind_invalid;
		}
	}
	if (strstr(name, ".."))
	{
		return _crossplay_map_kind_invalid;
	}
	if (prefix_is(name, CROSSPLAY_MAP_CUSTOM_EDITION_PREFIX))
	{
		kind = _crossplay_map_kind_custom_edition;
		leaf = name + strlen(CROSSPLAY_MAP_CUSTOM_EDITION_PREFIX);
	}
	else if (prefix_is(name, CROSSPLAY_MAP_HALOMD_PREFIX))
	{
		kind = _crossplay_map_kind_halomd;
		leaf = name + strlen(CROSSPLAY_MAP_HALOMD_PREFIX);
		extension = ".md";
	}
	else if (prefix_is(name, CROSSPLAY_MAP_HALO_PC_PREFIX))
	{
		kind = _crossplay_map_kind_halo_pc_retail;
		leaf = name + strlen(CROSSPLAY_MAP_HALO_PC_PREFIX);
		extension = ".pc";
	}
	else
	{
		/* (the game's own: the cache takes the name after the last backslash,
		cache_files.c) */
		kind = _crossplay_map_kind_xbox;
		leaf = strrchr(name, '\\');
		leaf = leaf ? leaf + 1 : name;
	}
	/* (a file in the folder the prefix names, no deeper) */
	if (kind != _crossplay_map_kind_xbox && strchr(leaf, '\\'))
	{
		return _crossplay_map_kind_invalid;
	}
	leaf_length = strlen(leaf);
	if (extension)
	{
		if (!suffix_is(leaf, leaf_length, extension))
		{
			return _crossplay_map_kind_invalid;
		}
		leaf_length -= strlen(extension);
	}
	/* (a Custom Edition map named for its hash, OpenCE's since its version
	27: the base before it is held to the rules of a whole name, and the
	whole is that and the hash; the rest as before) */
	if (kind == _crossplay_map_kind_custom_edition && hashed_end(leaf, leaf_length, NULL))
	{
		char base[CROSSPLAY_MAP_FILE_BYTES];
		size_t base_length = leaf_length - CROSSPLAY_MAP_HASH_DIGITS - 1;

		if (base_length > CROSSPLAY_MAP_MAXIMUM_FILE_LENGTH)
		{
			return _crossplay_map_kind_invalid;
		}
		memcpy(base, leaf, base_length);
		base[base_length] = 0;
		if (!file_name_taken(base))
		{
			return _crossplay_map_kind_invalid;
		}
		memcpy(file, leaf, leaf_length);
		file[leaf_length] = 0;
		return kind;
	}
	if (!leaf_length || leaf_length > CROSSPLAY_MAP_MAXIMUM_FILE_LENGTH)
	{
		return _crossplay_map_kind_invalid;
	}
	memcpy(file, leaf, leaf_length);
	file[leaf_length] = 0;
	/* (a name a map file may have here: file_name_taken) */
	if (!file_name_taken(file))
	{
		file[0] = 0;
		return _crossplay_map_kind_invalid;
	}

	return kind;
}

short crossplay_map_verdict(
	short kind,
	char const *file,
	unsigned long host_version,
	struct crossplay_local_map const *local)
{
	switch (kind)
	{
	case _crossplay_map_kind_xbox:
		/* (no PC build sends a version with one of the game's own maps: their
		regions' copies play together, and are never compared) */
		if (host_version)
		{
			return _crossplay_map_identity_unknown;
		}
		/* (one of the Xbox's levels: this machine's file must be the Xbox
		map, whole, as on the legacy wire: custom_edition_maps_stock_problem) */
		if (crossplay_map_file_is_stock(file))
		{
			if (local->stock_problem)
			{
				return local->file == _crossplay_map_file_absent ? _crossplay_map_missing :
					_crossplay_map_stock_unloadable;
			}
			return local->file == _crossplay_map_file_custom_edition ? _crossplay_map_xbox_is_custom_edition_here :
				_crossplay_map_playable;
		}
		switch (local->file)
		{
		case _crossplay_map_file_absent:
			return _crossplay_map_missing;
		case _crossplay_map_file_custom_edition:
			return _crossplay_map_xbox_is_custom_edition_here;
		case _crossplay_map_file_xbox:
			return local->xbox_multiplayer ? _crossplay_map_playable : _crossplay_map_not_multiplayer;
		default:
			return _crossplay_map_unusable;
		}
	case _crossplay_map_kind_custom_edition:
		switch (local->file)
		{
		case _crossplay_map_file_absent:
			return _crossplay_map_missing;
		case _crossplay_map_file_xbox:
			return _crossplay_map_custom_edition_is_xbox_here;
		case _crossplay_map_file_custom_edition:
			break;
		default:
			return _crossplay_map_unusable;
		}
		/* (0: a host that could not read its own map's checksum; nothing to
		compare, as OpenCE's clients do) */
		if (host_version && (local->checksum & 0xFFFFFFFFUL) != (host_version & 0xFFFFFFFFUL))
		{
			return _crossplay_map_custom_edition_different;
		}
		/* (named for its hash: this file's must begin with the name's digits,
		whatever the checksum says; another version of the map otherwise) */
		if (crossplay_map_name_hashed(file, NULL, NULL) && local->hash_mismatch)
		{
			return _crossplay_map_custom_edition_different;
		}
		if (local->resource_maps_missing)
		{
			return _crossplay_map_needs_resource_maps;
		}
		if (!local->pc_maps_on)
		{
			return _crossplay_map_needs_pc_maps;
		}
		return _crossplay_map_playable;
	case _crossplay_map_kind_halomd:
		return _crossplay_map_halomd;
	case _crossplay_map_kind_halo_pc_retail:
		return _crossplay_map_halo_pc_retail;
	default:
		return _crossplay_map_name_refused;
	}
}

void crossplay_map_name_printable(
	char const *name,
	long size,
	char printable[CROSSPLAY_MAP_FILE_BYTES])
{
	char const *end = name && size > 0 ? memchr(name, 0, (size_t)size) : NULL;
	size_t length = end ? (size_t)(end - name) : (name && size > 0 ? (size_t)size : 0);
	size_t index;

	if (crossplay_map_name_parse(name, size, printable) != _crossplay_map_kind_invalid)
	{
		return;
	}
	if (length > CROSSPLAY_MAP_MAXIMUM_FILE_LENGTH)
	{
		length = CROSSPLAY_MAP_MAXIMUM_FILE_LENGTH;
	}
	for (index = 0; index < length; index++)
	{
		char character = name[index];

		printable[index] = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || strchr("_-. []()", character) ? character : '?';
	}
	printable[length] = 0;

	return;
}

char const *crossplay_map_verdict_describe(
	short verdict)
{
	static char const *const descriptions[NUMBER_OF_CROSSPLAY_MAP_VERDICTS] =
	{
		"playable",
		"a map name this game does not take",
		"a HaloMD map",
		"a Halo PC retail map",
		"one of the game's own maps with a version",
		"missing",
		"another copy of the Custom Edition map",
		"a Custom Edition map, the Xbox map here",
		"an Xbox map, a Custom Edition map here",
		"a file this game cannot load",
		"its resource maps missing",
		"PC maps off",
		"not a multiplayer map",
		"the Xbox level's file cannot be played",
	};

	return verdict >= 0 && verdict < NUMBER_OF_CROSSPLAY_MAP_VERDICTS ? descriptions[verdict] : "unknown";
}
