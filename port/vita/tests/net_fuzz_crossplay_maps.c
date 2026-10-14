/*
NET_FUZZ_CROSSPLAY_MAPS.C

A fuzz target for the crossplay join's map check (port/linux/game/
crossplay_maps.c): the map name a PC host's game settings record carries
(0x80 bytes, which need not be ended) and its map.version, against any kind
of file this machine may have of it. The game's side (the file looked at,
custom_edition_maps_crossplay_check) is not here: it looks only for a name
crossplay_map_name_parse took, by the file name it gave.

An input is the map version (4 bytes), what this machine's file is (8
bytes: its kind, the checksum's 4 bytes, then flags: Xbox multiplayer map,
stock problem, resource maps missing, PC maps on, a status byte, then
whether the file's hash is not a hashed name's), then the name's bytes (at
most 0x80, as the record's field). Whatever the input:
- a name taken gives a file name of at most 25 characters that map sharing
  would take (or one of the Xbox's levels' names), or a Custom Edition one
  named for its hash (OpenCE's v27): such a base, '_' and 8 lowercase hex
  digits; with no separator, drive or climb, and never a name past the
  record's field;
- a hashed name's map is played only when its file's hash is the name's;
- the name a message may show has at most 25 characters of the safe ones;
- the verdict is one there is, and a name refused is refused whatever the
  file.
Built as net_fuzz_map_share.c is, by run_net_fuzz_test.sh.
*/

#include "../../linux/game/crossplay_maps.h"
#include "../../linux/game/map_share_protocol.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RECORD_NAME_BYTES 0x80

static void check_file_name(char const *file, short kind)
{
	size_t length = strnlen(file, CROSSPLAY_MAP_FILE_BYTES);
	char base[CROSSPLAY_MAP_FILE_BYTES];
	unsigned long prefix;
	size_t index;
	int hashed;

	if (length == CROSSPLAY_MAP_FILE_BYTES || length == 0)
		abort();
	hashed = crossplay_map_name_hashed(file, base, &prefix);
	/* (a hash only after a Custom Edition map's name; any other name is
	held to 25 whole) */
	if (hashed && kind == _crossplay_map_kind_custom_edition)
	{
		if (length > CROSSPLAY_MAP_MAXIMUM_HASHED_FILE_LENGTH || !base[0] ||
			strlen(base) + 1 + CROSSPLAY_MAP_HASH_DIGITS != length || strncmp(base, file, strlen(base)) ||
			prefix > 0xFFFFFFFFUL)
			abort();
		if (!map_share_name_valid(base) && !map_share_name_is_stock(base))
			abort();
	}
	else
	{
		if (length > CROSSPLAY_MAP_MAXIMUM_FILE_LENGTH)
			abort();
		if (!map_share_name_valid(file) && !map_share_name_is_stock(file))
			abort();
	}
	for (index = 0; index < length; index++)
	{
		if (file[index] == '\\' || file[index] == '/' || file[index] == ':' || (unsigned char)file[index] < 0x20 ||
			(unsigned char)file[index] > 0x7E)
			abort();
	}
	if (strstr(file, ".."))
		abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct crossplay_local_map local;
	char *name;
	char file[CROSSPLAY_MAP_FILE_BYTES + 8];
	char shown[CROSSPLAY_MAP_FILE_BYTES + 8];
	unsigned long version;
	size_t name_size;
	size_t index;
	short kind;
	short verdict;

	if (size < 12)
		return 0;
	version = (unsigned long)data[0] | (unsigned long)data[1] << 8 | (unsigned long)data[2] << 16 |
		(unsigned long)data[3] << 24;
	memset(&local, 0, sizeof(local));
	local.file = (short)(data[4] % (NUMBER_OF_CROSSPLAY_MAP_FILES + 1));
	local.checksum = (unsigned long)data[5] | (unsigned long)data[6] << 8 | (unsigned long)data[7] << 16 |
		(unsigned long)data[8] << 24;
	local.xbox_multiplayer = data[9] & 1;
	local.stock_problem = (data[9] >> 1) & 1;
	local.resource_maps_missing = (data[9] >> 2) & 1;
	local.pc_maps_on = (data[9] >> 3) & 1;
	local.status = (short)data[10];
	local.hash_mismatch = data[11] & 1;
	name_size = size - 12 < RECORD_NAME_BYTES ? size - 12 : RECORD_NAME_BYTES;
	/* (exactly the bytes given, on the heap: AddressSanitizer sees a read
	past them) */
	name = malloc(name_size ? name_size : 1);
	if (!name)
		return 0;
	memcpy(name, data + 12, name_size);
	memset(file, 0x5A, sizeof(file));
	memset(shown, 0x5A, sizeof(shown));

	kind = crossplay_map_name_parse(name, (long)name_size, file);
	if (kind < 0 || kind >= NUMBER_OF_CROSSPLAY_MAP_KINDS)
		abort();
	for (index = CROSSPLAY_MAP_FILE_BYTES; index < sizeof(file); index++)
		if (file[index] != 0x5A)
			abort();
	if (kind == _crossplay_map_kind_invalid)
	{
		if (file[0])
			abort();
	}
	else
	{
		check_file_name(file, kind);
		/* (a name taken was ended within the bytes given) */
		if (!memchr(name, 0, name_size))
			abort();
	}

	verdict = crossplay_map_verdict(kind, file, version, &local);
	if (verdict < 0 || verdict >= NUMBER_OF_CROSSPLAY_MAP_VERDICTS)
		abort();
	if (kind == _crossplay_map_kind_invalid && verdict != _crossplay_map_name_refused)
		abort();
	if (verdict == _crossplay_map_playable &&
		kind != _crossplay_map_kind_xbox && kind != _crossplay_map_kind_custom_edition)
		abort();
	/* (a Custom Edition map played is the host's copy when the host said
	which) */
	if (verdict == _crossplay_map_playable && kind == _crossplay_map_kind_custom_edition &&
		(local.file != _crossplay_map_file_custom_edition || (version && local.checksum != version) ||
			!local.pc_maps_on || local.resource_maps_missing ||
			(crossplay_map_name_hashed(file, NULL, NULL) && local.hash_mismatch)))
		abort();
	/* (one of the game's own maps is never compared, and never played with a
	version) */
	if (verdict == _crossplay_map_playable && kind == _crossplay_map_kind_xbox && version)
		abort();
	if (!crossplay_map_verdict_describe(verdict)[0])
		abort();

	crossplay_map_name_printable(name, (long)name_size, shown);
	for (index = CROSSPLAY_MAP_FILE_BYTES; index < sizeof(shown); index++)
		if (shown[index] != 0x5A)
			abort();
	/* (a name taken shown as its file name, a hashed one with its hash; any
	other cut to 25) */
	if (strnlen(shown, CROSSPLAY_MAP_FILE_BYTES) > (kind == _crossplay_map_kind_invalid ?
		CROSSPLAY_MAP_MAXIMUM_FILE_LENGTH : CROSSPLAY_MAP_MAXIMUM_HASHED_FILE_LENGTH) ||
		(kind != _crossplay_map_kind_invalid && strcmp(shown, file)))
		abort();
	for (index = 0; shown[index]; index++)
	{
		char character = shown[index];

		if (character == '%' || character == '\\' || character == '/' || character == ':' ||
			(unsigned char)character < 0x20 || (unsigned char)character > 0x7E)
			abort();
	}
	free(name);

	return 0;
}

/* ---------- the target's checks: seeds */

void net_fuzz_write_seed(char const *folder, char const *name, void const *data, unsigned long size);

int net_fuzz_checks(void)
{
	static char const *const names[] =
	{
		"levels\\test\\bloodgulch\\bloodgulch",
		"custom_maps\\pcgulch",
		"custom_maps\\bloodgulch",
		"maps_md\\hugeass.md",
		"maps_pc\\bloodgulch.pc",
		"custom_maps\\..\\..\\x",
		"custom_maps\\pcgulch_69cd51eb",
		"custom_maps\\.._69cd51eb",
	};
	char const *folder = getenv("NET_FUZZ_SEED_DIR");
	int failures = 0;
	unsigned index;

	for (index = 0; index < sizeof(names) / sizeof(names[0]); index++)
	{
		uint8_t seed[12 + RECORD_NAME_BYTES];
		char file[CROSSPLAY_MAP_FILE_BYTES];
		size_t length = strlen(names[index]) + 1;

		memset(seed, 0, sizeof(seed));
		seed[0] = 0xF4; seed[1] = 0xB9; seed[2] = 0x74; seed[3] = 0xFB;
		seed[4] = (uint8_t)(index % NUMBER_OF_CROSSPLAY_MAP_FILES);
		seed[5] = 0xF4; seed[6] = 0xB9; seed[7] = 0x74; seed[8] = 0xFB;
		seed[9] = 0x09;
		memcpy(seed + 12, names[index], length);
		LLVMFuzzerTestOneInput(seed, 12 + length);
		if ((crossplay_map_name_parse(names[index], (long)length, file) == _crossplay_map_kind_invalid) !=
			(index == 5 || index == 7))
		{
			__builtin_printf("FAIL crossplay maps: seed '%s'\n", names[index]);
			failures++;
		}
		if (folder)
		{
			char seed_name[32];

			__builtin_snprintf(seed_name, sizeof(seed_name), "seed-%u", index);
			net_fuzz_write_seed(folder, seed_name, seed, 12 + length);
		}
	}

	return failures;
}
