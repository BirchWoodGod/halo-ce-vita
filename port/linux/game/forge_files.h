/*
FORGE_FILES.H

Forge's map variants as files (forge_files.c): the layout file of the
design (docs/superpowers/specs/2026-10-18-forge-design.md, "The layout
file", "Validation"), written and read here, and checked here before
anything reads a byte of it, ours or another player's alike. Pure C with
no game dependency (the unit test and the fuzzer build it alone:
port/vita/tests/forge_files_test.c).

A map variant is <name>.hmv ("Halo map variant") in the Forge folder's
folder of its map (forge_store.h). Binary, little-endian:

	header (64 bytes)
	  magic "HFRG", version u16 (1), header size u16 (64), flags u32
	  (bit 0: it has netgame spawns), map name char[32] (the scenario's
	  short name, bloodgulch), map hash u32 (the cache file's checksum: a
	  mismatch warns), pack count u16, path count u16, record count u16,
	  meta size u16, payload size u32, checksum u32 (CRC-32 of the payload)
	packs   pack count x 32: { recipe hash u32[2], name char[24] }
	paths   path count x { group tag u32, length u8, path char[length] }
	records record count x 48: { path index u16, kind u8, team i8,
	        position f32[3], yaw pitch roll f32[3], respawn s u16,
	        count u8, game types u16, variant u8, flags u8,
	        reserved u8[13] }
	meta    meta size bytes: title, author, description and date, each
	        UTF-8 and ended by a zero byte, in that order

Tag paths, never tag indices: a record whose tag the map does not have is
skipped when the variant is applied (forge.c), not refused here.
*/

#ifndef __FORGE_FILES_H
#define __FORGE_FILES_H

/* ---------- constants */

#define FORGE_FILE_EXTENSION ".hmv"
#define FORGE_FILE_VERSION 1
#define FORGE_FILE_HEADER_SIZE 64
#define FORGE_FILE_MAXIMUM_SIZE 65536
#define FORGE_FILE_MAXIMUM_PATHS 256
#define FORGE_FILE_MAXIMUM_PACKS 8
/* the Forge budget's objects (forge.h FORGE_MAXIMUM_OBJECTS) */
#define FORGE_FILE_MAXIMUM_RECORDS 256
#define FORGE_FILE_MAXIMUM_META 2048
#define FORGE_FILE_PATH_MAXIMUM_LENGTH 127
#define FORGE_FILE_PACK_SIZE 32
#define FORGE_FILE_PACK_NAME_SIZE 24
#define FORGE_FILE_RECORD_SIZE 48
#define FORGE_FILE_RECORD_RESERVED_SIZE 13
/* the texts' room, each one's terminator included */
#define FORGE_FILE_MAP_NAME_SIZE 32
#define FORGE_FILE_TITLE_SIZE 32
#define FORGE_FILE_AUTHOR_SIZE 32
#define FORGE_FILE_DESCRIPTION_SIZE 128
/* "YYYY-MM-DD HH:MM", or empty */
#define FORGE_FILE_DATE_SIZE 17
/* a file name: <stem>.hmv, the stem [a-z0-9_-] */
#define FORGE_FILE_NAME_SIZE 40
#define FORGE_FILE_STEM_MAXIMUM_LENGTH 28
/* the flying camera's and the game's world: every coordinate within it */
#define FORGE_FILE_MAXIMUM_COORDINATE 5000.f
#define FORGE_FILE_MAXIMUM_RESPAWN_SECONDS 600
#define FORGE_FILE_MAXIMUM_COUNT 16
#define FORGE_FILE_MAXIMUM_TEAM 7
#define FORGE_FILE_MAXIMUM_VARIANT 15
/* the game types (ctf, slayer, oddball, king of the hill, race) */
#define FORGE_FILE_GAME_TYPES_MASK 0x1F
/* a record's path index when its kind names no object */
#define FORGE_FILE_NO_PATH 0xFFFF

/* the header's flags */
enum
{
	FORGE_FILE_FLAG_NETGAME_SPAWNS = 1 << 0,
	FORGE_FILE_FLAGS_KNOWN = FORGE_FILE_FLAG_NETGAME_SPAWNS,
};

/* a record's flags */
enum
{
	/* placed when the variant starts (else only a netgame's spawn places it) */
	FORGE_RECORD_PLACE_AT_START = 1 << 0,
	FORGE_RECORD_FLAGS_KNOWN = FORGE_RECORD_PLACE_AT_START,
};

/* a record's kind */
enum
{
	_forge_record_object = 0,
	_forge_record_squad,
	_forge_record_player_spawn,
	_forge_record_weapon_spawn,
	_forge_record_vehicle_spawn,
	_forge_record_flag,
	_forge_record_hill,
	_forge_record_teleporter_in,
	_forge_record_teleporter_out,
	_forge_record_kill_zone,
	NUMBER_OF_FORGE_RECORD_KINDS
};

/* why a file was refused (forge_file_decode), in the order checked */
enum
{
	_forge_file_ok = 0,
	_forge_file_too_small,
	_forge_file_too_large,
	_forge_file_bad_magic,
	_forge_file_bad_version,
	_forge_file_bad_header_size,
	_forge_file_bad_flags,
	_forge_file_bad_map_name,
	_forge_file_wrong_map,
	_forge_file_too_many,
	_forge_file_bad_payload_size,
	_forge_file_bad_checksum,
	_forge_file_bad_pack,
	_forge_file_bad_path,
	_forge_file_bad_group,
	_forge_file_duplicate_path,
	_forge_file_bad_record,
	_forge_file_bad_meta,
	_forge_file_no_room,
	NUMBER_OF_FORGE_FILE_RESULTS
};

/* ---------- structures */

struct forge_file_pack
{
	unsigned int recipe[2];
	char name[FORGE_FILE_PACK_NAME_SIZE + 1];
};

struct forge_file_path
{
	unsigned int group_tag;
	/* (its length, at most FORGE_FILE_PATH_MAXIMUM_LENGTH) */
	char path[FORGE_FILE_PATH_MAXIMUM_LENGTH + 1];
};

struct forge_file_record
{
	unsigned short path_index;
	unsigned char kind;
	signed char team;
	float position[3];
	/* yaw, pitch, roll, radians, within +-pi */
	float angles[3];
	unsigned short respawn_seconds;
	unsigned char count;
	unsigned short game_types;
	unsigned char variant;
	unsigned char flags;
};

/* a map variant, as read or to be written */
struct forge_file_layout
{
	unsigned int flags;
	char map_name[FORGE_FILE_MAP_NAME_SIZE];
	unsigned int map_hash;
	short pack_count;
	short path_count;
	short record_count;
	struct forge_file_pack packs[FORGE_FILE_MAXIMUM_PACKS];
	struct forge_file_path paths[FORGE_FILE_MAXIMUM_PATHS];
	struct forge_file_record records[FORGE_FILE_MAXIMUM_RECORDS];
	char title[FORGE_FILE_TITLE_SIZE];
	char author[FORGE_FILE_AUTHOR_SIZE];
	char description[FORGE_FILE_DESCRIPTION_SIZE];
	char date[FORGE_FILE_DATE_SIZE];
};

/* ---------- prototypes/FORGE_FILES.C */

/* the CRC-32 (IEEE) of size bytes, from crc (0 to start) */
unsigned int forge_file_crc32(
	unsigned int crc,
	unsigned char const *bytes,
	int size);

/* a file read: every check of the design, in order, before anything is
taken from it (expected_map: the map it must be of, NULL for any). The
layout is filled only when it passes; _forge_file_ok, else why not (and,
in why, the words of it for the log, when why is not NULL) */
int forge_file_decode(
	unsigned char const *bytes,
	int size,
	char const *expected_map,
	struct forge_file_layout *layout,
	char *why,
	int why_size);

/* a layout written: checked as a file read is (a layout that would not be
read back is not written); the bytes written, or -1 (why in result when
not NULL) */
int forge_file_encode(
	struct forge_file_layout const *layout,
	unsigned char *bytes,
	int capacity,
	int *result);

/* a layout's own checks (the ones a file read gets after its framing):
_forge_file_ok or why not */
int forge_file_layout_check(
	struct forge_file_layout const *layout,
	char const *expected_map);

/* a result's words (English, for the log) */
char const *forge_file_result_name(
	int result);

/* whether a group tag is one Forge places (vehi weap eqip scen bipd mach
ctrl lifi actv) */
int forge_file_group_allowed(
	unsigned int group_tag);

/* whether a tag path is one a file may name: 1 to 127 characters of
[a-z0-9_ \-.], no "..", no empty part, not starting or ending with \ */
int forge_file_path_valid(
	char const *path,
	int length);

/* whether a map's short name is [a-z0-9_], 1 to 31 characters */
int forge_file_map_name_valid(
	char const *name);

/* whether a text is valid UTF-8 of at most maximum bytes with no control
character */
int forge_file_text_valid(
	char const *text,
	int maximum);

/* whether a file name is a map variant's: <stem>.hmv, the stem 1 to 28 of
[a-z0-9_-]; nothing else is ever opened in the Forge folder */
int forge_file_name_valid(
	char const *file_name);

/* a file name made from a title (lower case letters and digits, the rest
'_', at most 24), with a number after it when number > 1: "gulch_2.hmv" */
void forge_file_name_from_title(
	char const *title,
	int number,
	char *file_name,
	int size);

/* a received variant's file name, made by us from its hash, never from the
sender: "r0123abcd.hmv" */
void forge_file_name_received(
	unsigned int hash,
	char *file_name,
	int size);

/* an angle in radians brought within +-pi (the writer's) */
float forge_file_angle_reduce(
	float angle);

#endif
