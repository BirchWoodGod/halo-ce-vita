/*
FORGE_FILES_TEST.C

Forge's map variant files (port/linux/game/forge_files.c): the writer, the
validator and the names, then a fuzzer over the validator (truncation,
every byte flipped, huge counts, bad offsets, bad tag paths, the wrong
map, and random damage, half of it with the checksum made right again so
that the checks past it are reached). Built with ASan and UBSan by
run_forge_files_test.sh; any crash, leak or undefined behaviour fails it.

  forge_files_test [iterations]     the tests, and that many random cases
                                    (200000 by default)
  forge_files_test --write <folder> the harness tests' files (run_forge_test.sh):
                                    good.hmv, missing.hmv, wrongmap.hmv and
                                    damaged.hmv, of Blood Gulch
  forge_files_test --dump <file>    a map variant read through the validator
                                    and its records printed, a line each
*/

#include "forge_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(condition, what) check((condition) != 0, what, __LINE__)

static void check(int passed, char const *what, int line)
{
	checks++;
	if (!passed)
	{
		failures++;
		printf("FAIL (line %d): %s\n", line, what);
	}
}

#define GROUP(a, b, c, d) (((unsigned int)(a) << 24) | ((unsigned int)(b) << 16) | ((unsigned int)(c) << 8) | (unsigned int)(d))

static unsigned int random_state = 12345;

static unsigned int next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static void add_path(struct forge_file_layout *layout, unsigned int group, char const *path)
{
	struct forge_file_path *entry = &layout->paths[layout->path_count++];

	entry->group_tag = group;
	snprintf(entry->path, sizeof(entry->path), "%s", path);
}

static void add_record(struct forge_file_layout *layout, int path, float x, float y, float z, float yaw)
{
	struct forge_file_record *record = &layout->records[layout->record_count++];

	memset(record, 0, sizeof(*record));
	record->path_index = (unsigned short)path;
	record->kind = _forge_record_object;
	record->team = -1;
	record->position[0] = x;
	record->position[1] = y;
	record->position[2] = z;
	record->angles[0] = yaw;
	record->respawn_seconds = 30;
	record->count = 1;
	record->flags = FORGE_RECORD_PLACE_AT_START;
}

/* a variant of Blood Gulch: a Warthog, two boulders, a rocket launcher,
an overshield */
static void sample_layout(struct forge_file_layout *layout)
{
	memset(layout, 0, sizeof(*layout));
	snprintf(layout->map_name, sizeof(layout->map_name), "bloodgulch");
	layout->map_hash = 0x1234ABCD;
	add_path(layout, GROUP('v', 'e', 'h', 'i'), "vehicles\\warthog\\warthog");
	add_path(layout, GROUP('s', 'c', 'e', 'n'), "scenery\\rocks\\boulder\\boulder");
	add_path(layout, GROUP('w', 'e', 'a', 'p'), "weapons\\rocket launcher\\rocket launcher");
	add_path(layout, GROUP('e', 'q', 'i', 'p'), "powerups\\over shield");
	add_record(layout, 0, 98.f, -158.f, 1.2f, 0.5f);
	add_record(layout, 1, 100.f, -156.f, 1.0f, -1.f);
	add_record(layout, 1, 102.f, -156.f, 1.0f, 3.14159f);
	add_record(layout, 2, 99.f, -161.f, 1.0f, 0.f);
	add_record(layout, 3, 101.f, -163.f, 1.0f, 0.f);
	snprintf(layout->title, sizeof(layout->title), "Gulch Test");
	snprintf(layout->author, sizeof(layout->author), "Tester");
	snprintf(layout->description, sizeof(layout->description), "A Warthog, boulders and a rocket launcher.");
	snprintf(layout->date, sizeof(layout->date), "2026-10-18 12:00");
}

static int layouts_equal(struct forge_file_layout const *a, struct forge_file_layout const *b)
{
	int index;

	if (a->flags != b->flags || strcmp(a->map_name, b->map_name) || a->map_hash != b->map_hash ||
		a->pack_count != b->pack_count || a->path_count != b->path_count || a->record_count != b->record_count ||
		strcmp(a->title, b->title) || strcmp(a->author, b->author) || strcmp(a->description, b->description) ||
		strcmp(a->date, b->date))
	{
		return 0;
	}
	for (index = 0; index < a->pack_count; index++)
	{
		if (memcmp(a->packs[index].recipe, b->packs[index].recipe, sizeof(a->packs[index].recipe)) ||
			strcmp(a->packs[index].name, b->packs[index].name))
		{
			return 0;
		}
	}
	for (index = 0; index < a->path_count; index++)
	{
		if (a->paths[index].group_tag != b->paths[index].group_tag || strcmp(a->paths[index].path, b->paths[index].path))
			return 0;
	}
	for (index = 0; index < a->record_count; index++)
	{
		struct forge_file_record const *x = &a->records[index];
		struct forge_file_record const *y = &b->records[index];

		if (x->path_index != y->path_index || x->kind != y->kind || x->team != y->team ||
			memcmp(x->position, y->position, sizeof(x->position)) || memcmp(x->angles, y->angles, sizeof(x->angles)) ||
			x->respawn_seconds != y->respawn_seconds || x->count != y->count || x->game_types != y->game_types ||
			x->variant != y->variant || x->flags != y->flags)
		{
			return 0;
		}
	}
	return 1;
}

/* the checksum made right again after a change, so the checks after it
are reached */
static void fix_checksum(unsigned char *bytes, int size)
{
	unsigned int crc;

	if (size < FORGE_FILE_HEADER_SIZE)
		return;
	crc = forge_file_crc32(0, bytes + FORGE_FILE_HEADER_SIZE, size - FORGE_FILE_HEADER_SIZE);
	bytes[60] = (unsigned char)crc;
	bytes[61] = (unsigned char)(crc >> 8);
	bytes[62] = (unsigned char)(crc >> 16);
	bytes[63] = (unsigned char)(crc >> 24);
}

static void put_u16(unsigned char *bytes, int offset, unsigned int value)
{
	bytes[offset] = (unsigned char)value;
	bytes[offset + 1] = (unsigned char)(value >> 8);
}

static void put_u32(unsigned char *bytes, int offset, unsigned int value)
{
	put_u16(bytes, offset, value & 0xFFFF);
	put_u16(bytes, offset + 2, value >> 16);
}

static struct forge_file_layout sample;
static struct forge_file_layout decoded;
static struct forge_file_layout other;
static unsigned char good[FORGE_FILE_MAXIMUM_SIZE];
static unsigned char work[FORGE_FILE_MAXIMUM_SIZE + 16];
static int good_size;

static int decode(unsigned char const *bytes, int size, char const *map)
{
	char why[96];

	return forge_file_decode(bytes, size, map, &decoded, why, sizeof(why));
}

/* an encode of the sample with one change, which must be refused */
static int encode_refused(struct forge_file_layout const *layout)
{
	int result = 0;

	return forge_file_encode(layout, work, sizeof(work), &result) < 0 && result != _forge_file_ok;
}

static void test_round_trip(void)
{
	int result = -1;
	char why[96];

	sample_layout(&sample);
	good_size = forge_file_encode(&sample, good, sizeof(good), &result);
	CHECK(good_size > FORGE_FILE_HEADER_SIZE && result == _forge_file_ok, "the sample is written");
	CHECK(forge_file_decode(good, good_size, "bloodgulch", &decoded, why, sizeof(why)) == _forge_file_ok,
		"the sample is read back");
	CHECK(layouts_equal(&sample, &decoded), "what is read back is what was written");
	CHECK(decode(good, good_size, NULL) == _forge_file_ok, "read with no map expected");
	CHECK(decode(good, good_size, "chillout") == _forge_file_wrong_map, "of another map: refused");
	CHECK(decoded.record_count == 0 && decoded.path_count == 0, "a refused file leaves an empty layout");
	/* (encoded twice, the same bytes) */
	CHECK(forge_file_encode(&sample, work, sizeof(work), &result) == good_size && !memcmp(work, good, (size_t)good_size),
		"written the same way twice");
	/* (too little room) */
	CHECK(forge_file_encode(&sample, work, good_size - 1, &result) < 0 && result == _forge_file_no_room,
		"no room: nothing written");
	/* (a full layout: every limit at once, still within 64 KB) */
	{
		int index;

		memset(&other, 0, sizeof(other));
		snprintf(other.map_name, sizeof(other.map_name), "bloodgulch");
		for (index = 0; index < FORGE_FILE_MAXIMUM_PACKS; index++)
		{
			other.packs[index].recipe[0] = (unsigned int)index;
			snprintf(other.packs[index].name, sizeof(other.packs[index].name), "pack %c abcdefghijklmnop", 'a' + index);
		}
		other.pack_count = FORGE_FILE_MAXIMUM_PACKS;
		for (index = 0; index < FORGE_FILE_MAXIMUM_PATHS; index++)
		{
			char path[FORGE_FILE_PATH_MAXIMUM_LENGTH + 1];

			memset(path, 'a', FORGE_FILE_PATH_MAXIMUM_LENGTH);
			path[FORGE_FILE_PATH_MAXIMUM_LENGTH] = 0;
			path[0] = (char)('a' + index % 26);
			path[1] = (char)('a' + index / 26 % 26);
			path[2] = '\\';
			add_path(&other, GROUP('s', 'c', 'e', 'n'), path);
		}
		for (index = 0; index < FORGE_FILE_MAXIMUM_RECORDS; index++)
			add_record(&other, index, (float)index, -4999.f, 4999.f, -3.14159f);
		memset(other.title, 'T', FORGE_FILE_TITLE_SIZE - 1);
		memset(other.author, 'A', FORGE_FILE_AUTHOR_SIZE - 1);
		memset(other.description, 'D', FORGE_FILE_DESCRIPTION_SIZE - 1);
		result = forge_file_encode(&other, work, sizeof(work), &result);
		CHECK(result > 0 && result <= FORGE_FILE_MAXIMUM_SIZE, "a layout at every limit fits 64 KB");
		CHECK(result > 0 && decode(work, result, "bloodgulch") == _forge_file_ok && layouts_equal(&other, &decoded),
			"and is read back");
	}
}

static void test_writer_refuses(void)
{
	struct forge_file_layout layout;

	sample_layout(&layout);
	snprintf(layout.paths[0].path, sizeof(layout.paths[0].path), "vehicles\\..\\warthog");
	CHECK(encode_refused(&layout), "a path with .. is not written");
	sample_layout(&layout);
	snprintf(layout.paths[0].path, sizeof(layout.paths[0].path), "\\vehicles\\warthog");
	CHECK(encode_refused(&layout), "a path from the root is not written");
	sample_layout(&layout);
	snprintf(layout.paths[0].path, sizeof(layout.paths[0].path), "Vehicles\\Warthog");
	CHECK(encode_refused(&layout), "capitals are not written");
	sample_layout(&layout);
	snprintf(layout.paths[0].path, sizeof(layout.paths[0].path), "vehicles/warthog");
	CHECK(encode_refused(&layout), "a / is not written");
	sample_layout(&layout);
	layout.paths[0].group_tag = GROUP('s', 'c', 'n', 'r');
	CHECK(encode_refused(&layout), "a scenario's group is not written");
	sample_layout(&layout);
	layout.paths[0].group_tag = GROUP('s', 'b', 's', 'p');
	CHECK(encode_refused(&layout), "a structure's group is not written");
	sample_layout(&layout);
	layout.paths[2] = layout.paths[1];
	CHECK(encode_refused(&layout), "a path twice is not written");
	sample_layout(&layout);
	layout.title[0] = 0;
	CHECK(encode_refused(&layout), "an empty title is not written");
	sample_layout(&layout);
	snprintf(layout.title, sizeof(layout.title), "bad\x01title");
	CHECK(encode_refused(&layout), "a control character is not written");
	sample_layout(&layout);
	memset(layout.title, 'x', sizeof(layout.title));
	CHECK(encode_refused(&layout), "an unterminated title is not written");
	sample_layout(&layout);
	snprintf(layout.date, sizeof(layout.date), "yesterday");
	CHECK(encode_refused(&layout), "a date not YYYY-MM-DD HH:MM is not written");
	sample_layout(&layout);
	snprintf(layout.map_name, sizeof(layout.map_name), "../maps");
	CHECK(encode_refused(&layout), "a map name with a path is not written");
	sample_layout(&layout);
	layout.records[0].angles[1] = 4.f;
	CHECK(encode_refused(&layout), "an angle past pi is not written");
	sample_layout(&layout);
	{
		unsigned int nan_bits = 0x7FC00000u;

		memcpy(&layout.records[0].position[0], &nan_bits, sizeof(float));
	}
	CHECK(encode_refused(&layout), "a NaN is not written");
	sample_layout(&layout);
	layout.records[0].position[2] = 5000.f;
	CHECK(encode_refused(&layout), "a place outside the world is not written");
	sample_layout(&layout);
	layout.records[0].team = 8;
	CHECK(encode_refused(&layout), "team 8 is not written");
	sample_layout(&layout);
	layout.records[0].respawn_seconds = FORGE_FILE_MAXIMUM_RESPAWN_SECONDS + 1;
	CHECK(encode_refused(&layout), "a respawn past its cap is not written");
	sample_layout(&layout);
	layout.records[0].count = 2;
	CHECK(encode_refused(&layout), "an object counted twice is not written");
	sample_layout(&layout);
	layout.records[0].flags = 0x80;
	CHECK(encode_refused(&layout), "an unknown flag is not written");
	sample_layout(&layout);
	layout.records[0].path_index = (unsigned short)layout.path_count;
	CHECK(encode_refused(&layout), "a path index past the paths is not written");
	sample_layout(&layout);
	layout.records[0].kind = _forge_record_vehicle_spawn;
	layout.records[0].path_index = 1;
	CHECK(encode_refused(&layout), "a vehicle spawn of scenery is not written");
	sample_layout(&layout);
	layout.records[0].kind = _forge_record_player_spawn;
	CHECK(encode_refused(&layout), "a player spawn naming an object is not written");
	layout.records[0].path_index = FORGE_FILE_NO_PATH;
	layout.records[0].count = 0;
	CHECK(encode_refused(&layout), "a netgame record without the header's flag is not written");
	layout.flags = FORGE_FILE_FLAG_NETGAME_SPAWNS;
	CHECK(!encode_refused(&layout), "a player spawn naming none is written");
	sample_layout(&layout);
	layout.records[0].kind = NUMBER_OF_FORGE_RECORD_KINDS;
	CHECK(encode_refused(&layout), "an unknown kind is not written");
	sample_layout(&layout);
	layout.record_count = FORGE_FILE_MAXIMUM_RECORDS + 1;
	CHECK(encode_refused(&layout), "257 records are not written");
	sample_layout(&layout);
	layout.flags = 0x100;
	CHECK(encode_refused(&layout), "an unknown header flag is not written");
}

static void test_reader_refuses(void)
{
	int size;
	int offset;

	/* truncated at every length */
	for (size = 0; size < good_size; size++)
	{
		memcpy(work, good, (size_t)size);
		if (decode(work, size, "bloodgulch") == _forge_file_ok)
		{
			CHECK(0, "a truncated file was taken");
			break;
		}
		/* (and with the size and checksum made to fit) */
		if (size >= FORGE_FILE_HEADER_SIZE)
		{
			put_u32(work, 56, (unsigned int)(size - FORGE_FILE_HEADER_SIZE));
			fix_checksum(work, size);
			if (decode(work, size, "bloodgulch") == _forge_file_ok)
			{
				CHECK(0, "a truncated file, its size and checksum made right, was taken");
				break;
			}
		}
	}
	CHECK(size == good_size, "every truncation refused");
	/* longer, and too large */
	memcpy(work, good, (size_t)good_size);
	work[good_size] = 0;
	CHECK(decode(work, good_size + 1, "bloodgulch") == _forge_file_bad_payload_size, "a byte more: refused");
	CHECK(decode(work, FORGE_FILE_MAXIMUM_SIZE + 1, "bloodgulch") == _forge_file_too_large, "past 64 KB: refused");
	/* every byte flipped: refused, but for the map hash (taken: it warns) */
	for (offset = 0; offset < good_size; offset++)
	{
		int bit;

		for (bit = 0; bit < 8; bit++)
		{
			int result;

			memcpy(work, good, (size_t)good_size);
			work[offset] ^= (unsigned char)(1 << bit);
			result = decode(work, good_size, "bloodgulch");
			if (offset >= 44 && offset < 48)
			{
				if (result != _forge_file_ok)
					CHECK(0, "a flipped map hash was refused");
			}
			else if (result == _forge_file_ok)
			{
				printf("  (byte %d bit %d)\n", offset, bit);
				CHECK(0, "a flipped bit was taken");
			}
		}
	}
	/* huge counts */
	memcpy(work, good, (size_t)good_size);
	put_u16(work, 50, 0xFFFF);
	CHECK(decode(work, good_size, "bloodgulch") == _forge_file_too_many, "65535 paths: refused");
	memcpy(work, good, (size_t)good_size);
	put_u16(work, 52, FORGE_FILE_MAXIMUM_RECORDS + 1);
	CHECK(decode(work, good_size, "bloodgulch") == _forge_file_too_many, "257 records: refused");
	memcpy(work, good, (size_t)good_size);
	put_u16(work, 48, FORGE_FILE_MAXIMUM_PACKS + 1);
	CHECK(decode(work, good_size, "bloodgulch") == _forge_file_too_many, "9 packs: refused");
	memcpy(work, good, (size_t)good_size);
	put_u16(work, 54, FORGE_FILE_MAXIMUM_META + 1);
	CHECK(decode(work, good_size, "bloodgulch") == _forge_file_too_many, "meta past 2 KB: refused");
	/* counts that do not fit the bytes (checksum right) */
	memcpy(work, good, (size_t)good_size);
	put_u16(work, 52, 200);
	fix_checksum(work, good_size);
	CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_payload_size, "more records than bytes: refused");
	memcpy(work, good, (size_t)good_size);
	put_u16(work, 50, 1);
	fix_checksum(work, good_size);
	CHECK(decode(work, good_size, "bloodgulch") != _forge_file_ok, "fewer paths than written: refused");
	/* bad offsets: the payload's size not the bytes' */
	memcpy(work, good, (size_t)good_size);
	put_u32(work, 56, 0xFFFFFFF0u);
	CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_payload_size, "a payload size past the file: refused");
	/* a path's length past the end, and bad paths (checksum right) */
	{
		int first_path = FORGE_FILE_HEADER_SIZE;

		memcpy(work, good, (size_t)good_size);
		work[first_path + 4] = 255;
		fix_checksum(work, good_size);
		CHECK(decode(work, good_size, "bloodgulch") != _forge_file_ok, "a path 255 long: refused");
		memcpy(work, good, (size_t)good_size);
		work[first_path + 4] = 0;
		fix_checksum(work, good_size);
		CHECK(decode(work, good_size, "bloodgulch") != _forge_file_ok, "a path of nothing: refused");
		memcpy(work, good, (size_t)good_size);
		memcpy(work + first_path + 5, "..", 2);
		fix_checksum(work, good_size);
		CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_path, "a path with ..: refused");
		memcpy(work, good, (size_t)good_size);
		work[first_path + 5] = '\\';
		fix_checksum(work, good_size);
		CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_path, "a path from the root: refused");
		memcpy(work, good, (size_t)good_size);
		work[first_path + 5] = 'V';
		fix_checksum(work, good_size);
		CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_path, "a capital: refused");
		memcpy(work, good, (size_t)good_size);
		work[first_path + 6] = '%';
		fix_checksum(work, good_size);
		CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_path, "a %: refused");
		memcpy(work, good, (size_t)good_size);
		work[first_path + 6] = 0;
		fix_checksum(work, good_size);
		CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_path, "a zero inside a path: refused");
		memcpy(work, good, (size_t)good_size);
		put_u32(work, first_path, GROUP('s', 'c', 'n', 'r'));
		fix_checksum(work, good_size);
		CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_group, "a scenario's group: refused");
		memcpy(work, good, (size_t)good_size);
		put_u32(work, first_path, GROUP('h', 's', 'c', '*'));
		fix_checksum(work, good_size);
		CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_group, "a script's group: refused");
	}
	/* the wrong map, a bad map name */
	memcpy(work, good, (size_t)good_size);
	CHECK(decode(work, good_size, "chillout") == _forge_file_wrong_map, "of another map: refused");
	memcpy(work + 12, "../../maps\0", 11);
	CHECK(decode(work, good_size, NULL) == _forge_file_bad_map_name, "a map name with a path: refused");
	memcpy(work, good, (size_t)good_size);
	work[12 + 20] = 'x';
	CHECK(decode(work, good_size, NULL) == _forge_file_bad_map_name, "bytes after the map name: refused");
	/* a record's bad values (checksum right): every record field */
	{
		int records = good_size - (FORGE_FILE_RECORD_SIZE * sample.record_count) -
			(int)(strlen(sample.title) + strlen(sample.author) + strlen(sample.description) + strlen(sample.date) + 4);
		static struct
		{
			int offset;
			int size;
			unsigned int value;
			char const *what;
		} const cases[] =
		{
			{ 0, 2, 99, "a path index past the paths" },
			{ 2, 1, NUMBER_OF_FORGE_RECORD_KINDS, "an unknown kind" },
			{ 3, 1, 0x80, "team -128" },
			{ 3, 1, 9, "team 9" },
			{ 4, 4, 0x7F800000u, "an infinite x" },
			{ 8, 4, 0x7FC00001u, "a NaN y" },
			{ 12, 4, 0x45FA0000u, "z 8000" },
			{ 16, 4, 0x40800000u, "yaw 4" },
			{ 20, 4, 0xFF800000u, "an infinite pitch" },
			{ 28, 2, 0xFFFF, "a respawn of 65535 s" },
			{ 30, 1, 2, "an object counted twice" },
			{ 31, 2, 0x20, "an unknown game type" },
			{ 33, 1, 16, "variant 16" },
			{ 34, 1, 0x02, "an unknown flag" },
			{ 35, 1, 1, "a reserved byte" },
			{ 47, 1, 0x80, "the last reserved byte" },
		};
		int index;

		for (index = 0; index < (int)(sizeof(cases) / sizeof(cases[0])); index++)
		{
			int at = records + FORGE_FILE_RECORD_SIZE + cases[index].offset;
			char what[96];

			memcpy(work, good, (size_t)good_size);
			if (cases[index].size == 1)
				work[at] = (unsigned char)cases[index].value;
			else if (cases[index].size == 2)
				put_u16(work, at, cases[index].value);
			else
				put_u32(work, at, cases[index].value);
			fix_checksum(work, good_size);
			snprintf(what, sizeof(what), "%s: refused", cases[index].what);
			CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_record, what);
		}
	}
	/* the meta's texts (checksum right) */
	{
		int meta = good_size - (int)(strlen(sample.title) + strlen(sample.author) + strlen(sample.description) +
			strlen(sample.date) + 4);

		memcpy(work, good, (size_t)good_size);
		work[good_size - 1] = 'x';
		fix_checksum(work, good_size);
		CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_meta, "the date not ended: refused");
		memcpy(work, good, (size_t)good_size);
		work[meta] = 0x01;
		fix_checksum(work, good_size);
		CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_meta, "a control character in the title: refused");
		memcpy(work, good, (size_t)good_size);
		work[meta] = 0xC0;
		work[meta + 1] = 0x80;
		fix_checksum(work, good_size);
		CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_meta, "an overlong UTF-8 form: refused");
		memcpy(work, good, (size_t)good_size);
		work[meta] = 0;
		fix_checksum(work, good_size);
		CHECK(decode(work, good_size, "bloodgulch") == _forge_file_bad_meta, "an empty title: refused");
	}
}

static void test_names(void)
{
	char name[FORGE_FILE_NAME_SIZE];

	forge_file_name_from_title("Blood Gulch: Rockets!", 1, name, sizeof(name));
	CHECK(!strcmp(name, "blood_gulch_rockets.hmv"), "a title made a file name");
	forge_file_name_from_title("Blood Gulch: Rockets!", 3, name, sizeof(name));
	CHECK(!strcmp(name, "blood_gulch_rockets_3.hmv"), "and numbered");
	forge_file_name_from_title("../../../etc/passwd", 1, name, sizeof(name));
	CHECK(!strcmp(name, "etc_passwd.hmv") && forge_file_name_valid(name), "a path in a title is only letters");
	forge_file_name_from_title("\xc3\x91" "and" "\xc3\xba", 1, name, sizeof(name));
	CHECK(forge_file_name_valid(name), "an accented title makes a valid name");
	forge_file_name_from_title("!!!", 1, name, sizeof(name));
	CHECK(!strcmp(name, "variant.hmv"), "a title of no letters: variant");
	forge_file_name_from_title("A very long title of a map variant indeed", 9999, name, sizeof(name));
	CHECK(forge_file_name_valid(name), "a long title, numbered, still a valid name");
	forge_file_name_received(0xDEADBEEF, name, sizeof(name));
	CHECK(!strcmp(name, "rdeadbeef.hmv") && forge_file_name_valid(name), "a received file's name is made by us");
	CHECK(forge_file_name_valid("gulch.hmv") && forge_file_name_valid("a-b_c9.hmv"), "good names");
	CHECK(!forge_file_name_valid("../gulch.hmv") && !forge_file_name_valid("a/b.hmv") &&
		!forge_file_name_valid("a\\b.hmv") && !forge_file_name_valid("Gulch.hmv") && !forge_file_name_valid(".hmv") &&
		!forge_file_name_valid("gulch.hmv.tmp") && !forge_file_name_valid("gulch.HMV") &&
		!forge_file_name_valid("gulch") && !forge_file_name_valid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaa.hmv") &&
		!forge_file_name_valid("") && !forge_file_name_valid(NULL) && !forge_file_name_valid("a b.hmv"),
		"bad names refused");
	CHECK(forge_file_map_name_valid("bloodgulch") && !forge_file_map_name_valid("") &&
		!forge_file_map_name_valid("blood/gulch") && !forge_file_map_name_valid("..") &&
		!forge_file_map_name_valid("BloodGulch"), "map names");
	CHECK(forge_file_text_valid("\xc3\x91" "and" "\xc3\xba", 32) && !forge_file_text_valid("\xc3", 32) &&
		!forge_file_text_valid("\xed\xa0\x80", 32) && !forge_file_text_valid("\xf4\x90\x80\x80", 32) &&
		!forge_file_text_valid("tab\there", 32) && !forge_file_text_valid("\xc2\x85", 32) &&
		!forge_file_text_valid("12345678", 8) && forge_file_text_valid("1234567", 8), "texts");
	CHECK(forge_file_path_valid("vehicles\\warthog\\warthog", 24) && !forge_file_path_valid("a\\\\b", 4) &&
		!forge_file_path_valid("a\\", 2) && !forge_file_path_valid("a:b", 3), "paths");
	{
		float reduced = forge_file_angle_reduce(7.f);

		CHECK(reduced > 0.71f && reduced < 0.72f, "an angle brought within pi");
		CHECK(forge_file_angle_reduce(-7.f) < -0.71f, "and a negative one");
	}
	CHECK(forge_file_crc32(0, (unsigned char const *)"123456789", 9) == 0xCBF43926u, "the CRC-32 is IEEE's");
}

/* random damage to the good file (half the time the checksum made right
again), and random bytes: never a crash, and a file taken is one that
writes back as itself and reads back the same */
static void fuzz(long iterations)
{
	long iteration;
	long taken = 0;

	for (iteration = 0; iteration < iterations; iteration++)
	{
		int size;
		int changes;
		int index;

		if (next_random() % 8 == 0)
		{
			size = (int)(next_random() % (FORGE_FILE_MAXIMUM_SIZE + 2));
			for (index = 0; index < size; index++)
				work[index] = (unsigned char)next_random();
			if (size >= 4 && next_random() % 2)
				memcpy(work, "HFRG", 4);
		}
		else
		{
			size = good_size;
			memcpy(work, good, (size_t)size);
			changes = 1 + (int)(next_random() % 6);
			for (index = 0; index < changes; index++)
			{
				int at = (int)(next_random() % (unsigned int)size);

				switch (next_random() % 6)
				{
				case 0: work[at] ^= (unsigned char)(1 << (next_random() % 8)); break;
				case 1: work[at] = (unsigned char)next_random(); break;
				case 2: work[at] = 0; break;
				case 3: work[at] = 0xFF; break;
				case 4: size = at > 0 ? at : size; break;
				case 5:
					if (size < FORGE_FILE_MAXIMUM_SIZE)
						work[size++] = (unsigned char)next_random();
					break;
				}
			}
			if (next_random() % 2 && size >= FORGE_FILE_HEADER_SIZE)
			{
				if (next_random() % 2)
					put_u32(work, 56, (unsigned int)(size - FORGE_FILE_HEADER_SIZE));
				fix_checksum(work, size);
			}
		}
		if (decode(work, size, next_random() % 4 ? "bloodgulch" : NULL) == _forge_file_ok)
		{
			int result;
			int written;

			taken++;
			memcpy(&other, &decoded, sizeof(other));
			CHECK(forge_file_layout_check(&other, NULL) == _forge_file_ok, "a file taken passes the layout's checks");
			written = forge_file_encode(&other, work, sizeof(work), &result);
			CHECK(written > 0, "a file taken can be written back");
			if (written > 0)
				CHECK(decode(work, written, NULL) == _forge_file_ok && layouts_equal(&other, &decoded),
					"and reads back the same");
		}
	}
	printf("fuzz: %ld cases, %ld taken (the map hash's and unchanged ones)\n", iterations, taken);
}

static int write_file(char const *folder, char const *name, unsigned char const *bytes, int size)
{
	char path[512];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", folder, name);
	file = fopen(path, "wb");
	if (!file || fwrite(bytes, 1, (size_t)size, file) != (size_t)size)
	{
		printf("could not write %s\n", path);
		if (file)
			fclose(file);
		return 1;
	}
	fclose(file);
	return 0;
}

/* the harness tests' files */
static int write_samples(char const *folder)
{
	struct forge_file_layout layout;
	int size;
	int result;
	int status = 0;

	sample_layout(&layout);
	snprintf(layout.title, sizeof(layout.title), "Sample Gulch");
	size = forge_file_encode(&layout, work, sizeof(work), &result);
	status |= size <= 0 || write_file(folder, "good.hmv", work, size);
	/* (a tag Blood Gulch does not have: skipped and counted when applied) */
	add_path(&layout, GROUP('v', 'e', 'h', 'i'), "vehicles\\banshee\\banshee_mp");
	add_record(&layout, layout.path_count - 1, 97.f, -160.f, 2.f, 0.f);
	snprintf(layout.title, sizeof(layout.title), "Missing Tag");
	size = forge_file_encode(&layout, work, sizeof(work), &result);
	status |= size <= 0 || write_file(folder, "missing.hmv", work, size);
	sample_layout(&layout);
	snprintf(layout.map_name, sizeof(layout.map_name), "chillout");
	snprintf(layout.title, sizeof(layout.title), "Wrong Map");
	size = forge_file_encode(&layout, work, sizeof(work), &result);
	status |= size <= 0 || write_file(folder, "wrongmap.hmv", work, size);
	sample_layout(&layout);
	snprintf(layout.title, sizeof(layout.title), "Damaged");
	size = forge_file_encode(&layout, work, sizeof(work), &result);
	if (size > 0)
		work[size - 20] ^= 0x55;
	status |= size <= 0 || write_file(folder, "damaged.hmv", work, size);
	printf("%s: the sample files written to %s\n", status ? "FAIL" : "ok", folder);
	return status;
}

/* a file read through the validator, its header and records printed */
static int dump(char const *path)
{
	FILE *file = fopen(path, "rb");
	int size;
	int index;
	char why[96];

	if (!file)
	{
		printf("could not read %s\n", path);
		return 1;
	}
	size = (int)fread(work, 1, sizeof(work), file);
	fclose(file);
	if (forge_file_decode(work, size, NULL, &decoded, why, sizeof(why)) != _forge_file_ok)
	{
		printf("refused: %s\n", why);
		return 1;
	}
	printf("map %s title \"%s\" author \"%s\" date \"%s\" paths %d records %d\n", decoded.map_name, decoded.title,
		decoded.author, decoded.date, decoded.path_count, decoded.record_count);
	for (index = 0; index < decoded.pack_count; index++)
		printf("pack %d: %s %08x%08x\n", index, decoded.packs[index].name, decoded.packs[index].recipe[1],
			decoded.packs[index].recipe[0]);
	for (index = 0; index < decoded.record_count; index++)
	{
		struct forge_file_record const *record = &decoded.records[index];

		printf("record %d: %s kind %d team %d respawn %d flags %d at %.2f %.2f %.2f angles %.3f %.3f %.3f\n", index,
			decoded.paths[record->path_index].path, record->kind, record->team, record->respawn_seconds, record->flags,
			record->position[0], record->position[1], record->position[2], record->angles[0], record->angles[1],
			record->angles[2]);
	}
	return 0;
}

int main(int argc, char **argv)
{
	long iterations = 200000;

	if (argc == 3 && !strcmp(argv[1], "--write"))
		return write_samples(argv[2]);
	if (argc == 3 && !strcmp(argv[1], "--dump"))
		return dump(argv[2]);
	if (argc == 2)
		iterations = atol(argv[1]);
	test_round_trip();
	test_writer_refuses();
	test_reader_refuses();
	test_names();
	fuzz(iterations);
	printf("%d checks, %d failed\n", checks, failures);
	if (failures)
	{
		printf("FAIL\n");
		return 1;
	}
	printf("PASS\n");
	return 0;
}
