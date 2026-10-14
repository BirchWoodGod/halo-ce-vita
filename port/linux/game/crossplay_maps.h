/*
CROSSPLAY_MAPS.H

Which map a PC host's game is on, as the crossplay wire names it, and
whether this machine has that map (docs/crossplay.md 3.2 G6 and 6, task T3).
With crossplay on (network.crossplay, HALO_CROSSPLAY) this machine only joins
PC hosts: OpenCE, ChupathingyCE and DamnationCE. Their game settings record
names the map, and its map.version (which the Xbox game left 0) says which
copy of it the host plays, with OpenCE's meaning since its network version
22 (4e8ed2f1, 9d309f8d; CC0):

- custom_maps\<file>: a Halo Custom Edition map (cache version 609), by its
  own name. Since OpenCE's network version 29 (3e0ba551, build-206; CC0)
  its map.version is the map's version: the first 32 bits of the map
  file's hash (BLAKE2b, crossplay_map_file_hash; never 0,
  crossplay_map_hash_version), where 22 to 28 sent the header checksum (and
  27 and 28 a hashed name). This machine plays the host's version: its
  <file>.map, or else <file>_<8 hex digits of the version>.map (as OpenCE
  saves a download beside another version of its name), whichever's hash
  is that version (0: the host could not hash its map, and <file>.map is
  played with nothing compared);
- maps_md\<file>.md and maps_pc\<file>.pc: ChupathingyCE's HaloMD and Halo PC
  retail maps (cache version 7, halo_map_families.h), which no OpenCE build
  and not this one plays;
- any other name: one of the game's own maps, the Xbox's (levels\test\<file>
  \<file> for the multiplayer levels), map.version 0. On every one of the
  three PC builds these are the Xbox maps themselves, not Halo PC's: they
  run the Xbox game's NTSC (01.10.12.2276) and PAL (01.01.14.2342) caches,
  the builds this one plays with (cache_files.c), whose regions play
  together. Halo PC's own versions of the multiplayer levels are other maps
  (other tags): on a PC host they are Custom Edition maps,
  custom_maps\bloodgulch, compared by version like any other.

The Vita keeps every map in one folder (ux0:data/haloce-vita/maps): the map
<file>.map (or OpenSauce's <file>.yelo) there is the one compared. This machine
downloads no map from a PC host (OpenCE's map sharing, its distributed
kinds 83-86, is dropped as unknown here); a map that is missing, another
copy, or one this game cannot load is refused, naming the map, and never
asked of the host (this port's own map sharing never runs there).

The names come from a stranger: every name is checked here (length, the
characters a file name may have, no path climbing out of the maps folder,
no device or resource map name) before any file is looked for. This unit
needs none of the game, so that its tests and its fuzz target build it
alone (port/vita/tests/crossplay_maps_test.c, net_fuzz_crossplay_maps.c);
the game's side, the file looked at, is custom_edition_maps.c's
(custom_edition_maps_crossplay_check).
*/

#ifndef __CROSSPLAY_MAPS_H
#define __CROSSPLAY_MAPS_H

#include <stdint.h>

/* ---------- constants */

/* the level name a game keeps (game_engine.c's stage: 63 characters) */
#define CROSSPLAY_MAP_MAXIMUM_NAME_LENGTH 63
/* a map file's name, without its extension (the map list's and map
sharing's limit, MAP_SHARE_MAXIMUM_NAME_LENGTH), and room for it with a
version after it (crossplay_map_version_file_name) */
#define CROSSPLAY_MAP_MAXIMUM_FILE_LENGTH 25
#define CROSSPLAY_MAP_FILE_BYTES 40
/* the end of the file name of another version of a map (OpenCE's
MAP_SHARE_NAME_DIGITS): '_' and this many lowercase hexadecimal digits, its
version's */
#define CROSSPLAY_MAP_HASH_DIGITS 8
#define CROSSPLAY_MAP_MAXIMUM_VERSION_FILE_LENGTH (CROSSPLAY_MAP_MAXIMUM_FILE_LENGTH + 1 + CROSSPLAY_MAP_HASH_DIGITS)
/* a map file's hash (OpenCE's map_share_files.c: BLAKE2b of 32 bytes of the
label "OpenCE map 1", the file's size, 4 bytes little-endian, and the
BLAKE2b hashes of its pieces of this size, the last shorter) */
#define CROSSPLAY_MAP_HASH_BYTES 32
#define CROSSPLAY_MAP_HASH_PIECE_BYTES 0x40000

#define CROSSPLAY_MAP_CUSTOM_EDITION_PREFIX "custom_maps\\"
#define CROSSPLAY_MAP_HALOMD_PREFIX "maps_md\\"
#define CROSSPLAY_MAP_HALO_PC_PREFIX "maps_pc\\"

/* ---------- enums */

/* what a PC host's map name names */
enum crossplay_map_kind
{
	/* not a name this game takes from a host: too long, characters a file
	name may not have, a path, a device's or a resource map's name */
	_crossplay_map_kind_invalid = 0,
	/* the game's own maps: the Xbox's (map.version 0) */
	_crossplay_map_kind_xbox,
	/* custom_maps\<file>: a Custom Edition map */
	_crossplay_map_kind_custom_edition,
	/* ChupathingyCE's maps_md\<file>.md and maps_pc\<file>.pc */
	_crossplay_map_kind_halomd,
	_crossplay_map_kind_halo_pc_retail,
	NUMBER_OF_CROSSPLAY_MAP_KINDS
};

/* what this machine's file of that map is (custom_edition_maps.c) */
enum crossplay_map_file
{
	_crossplay_map_file_absent = 0,
	/* an Xbox cache */
	_crossplay_map_file_xbox,
	/* a Custom Edition cache whose header this game takes */
	_crossplay_map_file_custom_edition,
	/* a cache file this game cannot take (`status`, cache_file_formats.h's:
	compressed, too big, damaged, another version), or not a cache */
	_crossplay_map_file_unusable,
	NUMBER_OF_CROSSPLAY_MAP_FILES
};

/* whether a PC host's map is played here, or why not */
enum crossplay_map_verdict
{
	_crossplay_map_playable = 0,
	/* the name is not one taken (kind invalid) */
	_crossplay_map_name_refused,
	/* a HaloMD or Halo PC retail map (ChupathingyCE's) */
	_crossplay_map_halomd,
	_crossplay_map_halo_pc_retail,
	/* one of the game's own maps with a map.version, which no PC build sends:
	a copy it does not say how to compare */
	_crossplay_map_identity_unknown,
	/* no file of that name */
	_crossplay_map_missing,
	/* a Custom Edition map of another version than the host's (its hash's
	first 32 bits not the record's map version) */
	_crossplay_map_custom_edition_different,
	/* a Custom Edition map the host names, an Xbox map here (Halo PC's
	Blood Gulch, custom_maps\bloodgulch, and this machine's bloodgulch.map
	the Xbox's, or a modded Xbox map of that name) */
	_crossplay_map_custom_edition_is_xbox_here,
	/* an Xbox map the host names, a Custom Edition one here (not the
	host's map: Halo PC's under an Xbox level's name with PC maps on, or a
	custom map of that name) */
	_crossplay_map_xbox_is_custom_edition_here,
	/* a file this game cannot load (compressed, too big, damaged) */
	_crossplay_map_unusable,
	/* a Custom Edition map whose resource maps (bitmaps.map, sounds.map,
	loc.map) are not in the maps folder */
	_crossplay_map_needs_resource_maps,
	/* a Custom Edition map, with PC maps (game.custom_edition) off */
	_crossplay_map_needs_pc_maps,
	/* an Xbox map that is not a multiplayer map the cache partition takes
	(not one of the Xbox's levels) */
	_crossplay_map_not_multiplayer,
	/* one of the Xbox's levels, whose file here is cut short, damaged or
	missing (cache_files_xbox_map_problem) */
	_crossplay_map_stock_unloadable,
	NUMBER_OF_CROSSPLAY_MAP_VERDICTS
};

/* ---------- structures */

/* this machine's file of the map a PC host names (custom_edition_maps.c) */
struct crossplay_local_map
{
	/* enum crossplay_map_file */
	short file;
	/* cache_file_formats.h's enum cache_file_status, for an unusable file */
	short status;
	/* a Custom Edition cache's version: its hash's first 32 bits
	(crossplay_map_hash_version; 0 if it could not be read whole) */
	unsigned long version;
	/* an Xbox cache: a multiplayer map the cache partition takes
	(custom_edition_cache_xbox_multiplayer) */
	int xbox_multiplayer;
	/* one of the Xbox's levels: its file here cannot be played as one
	(cache_files_xbox_map_problem: missing, cut short, damaged, Halo PC's) */
	int stock_problem;
	/* a Custom Edition cache: its resource maps are not all in the maps
	folder (custom_edition_cache_missing_resource_maps) */
	int resource_maps_missing;
	/* PC maps (game.custom_edition) on */
	int pc_maps_on;
};

/* reads `size` bytes at `offset` of a file into `buffer`: nonzero if all
were (cache_file_formats.h's cache_file_source read, by its types) */
typedef int (*crossplay_map_read_proc)(void *context, uint32_t offset, uint32_t size, void *buffer);

/* ---------- prototypes/CROSSPLAY_MAPS.C */

/* What the map name `name` (the record's, `size` bytes, which need not be
ended) names; the map's file name, without folder or extension, in `file`
(CROSSPLAY_MAP_FILE_BYTES) for any but an invalid name, else empty. */
short crossplay_map_name_parse(
	char const *name,
	long size,
	char file[CROSSPLAY_MAP_FILE_BYTES]);

/* A map file's hash, as OpenCE tells its maps' versions by (CROSSPLAY_MAP_HASH_BYTES
in `hash`), read through `read` (`size` bytes) in pieces of at most
`buffer_size` bytes into `buffer`; nonzero if the file was read whole. */
int crossplay_map_file_hash(
	crossplay_map_read_proc read,
	void *context,
	uint32_t size,
	void *buffer,
	uint32_t buffer_size,
	unsigned char hash[CROSSPLAY_MAP_HASH_BYTES]);

/* the version of a map of this hash, as a game record's map.version gives
it: its first 32 bits (the first bytes, big-endian), never 0 (OpenCE's
map_share_hash_version: 0 is a version nothing is compared for) */
unsigned long crossplay_map_hash_version(
	unsigned char const hash[CROSSPLAY_MAP_HASH_BYTES]);

/* the file name, without its extension, of another version of the map
`file` (a name crossplay_map_name_parse took, at most
CROSSPLAY_MAP_MAXIMUM_FILE_LENGTH): <file>_<the version's 8 lowercase hex
digits>, in `named` (CROSSPLAY_MAP_FILE_BYTES); 0, `named` empty, for a
version 0 or a file name of another length */
int crossplay_map_version_file_name(
	char const *file,
	unsigned long version,
	char named[CROSSPLAY_MAP_FILE_BYTES]);

/* Whether `file` names one of the Xbox's levels (multiplayer or campaign),
in letters of either case. */
int crossplay_map_file_is_stock(
	char const *file);

/* Whether a PC host's map, of kind `kind` and file name `file`
(crossplay_map_name_parse's) and version `host_version` (the record's
map.version), is played with this machine's file of it (`local`), and if not
why (enum crossplay_map_verdict). The checks in their order: the name, the
version, the file there, its kind and copy, then whether it loads here. */
short crossplay_map_verdict(
	short kind,
	char const *file,
	unsigned long host_version,
	struct crossplay_local_map const *local);

/* The record's map name, `size` bytes, as a message may show it: its file
name when it has one, else at most CROSSPLAY_MAP_MAXIMUM_FILE_LENGTH
characters of the name, any but letters, digits, '_', '-', '.', ' ' and
brackets as '?'; in `printable` (CROSSPLAY_MAP_FILE_BYTES). */
void crossplay_map_name_printable(
	char const *name,
	long size,
	char printable[CROSSPLAY_MAP_FILE_BYTES]);

/* the verdict's name for logs */
char const *crossplay_map_verdict_describe(
	short verdict);

#endif /* __CROSSPLAY_MAPS_H */
