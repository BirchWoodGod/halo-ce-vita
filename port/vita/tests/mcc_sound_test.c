/*
MCC_SOUND_TEST.C

The FSB5 reader (port/linux/src/fsb5.c) and the sound list parser
(port/linux/src/mcc_sound_list.c) on made-up banks and lists: no MCC file is
used or needed. run_mcc_sound_test.sh builds it with AddressSanitizer and
UBSan.

The bank built here has two IMA ADPCM streams like MCC's sounds_adpcm.fsb
does (version 1, a 60-byte header, an 8-byte word a stream, a name table of
"<leaf>_<pitch range>_<permutation>"): a mono 22 kHz one whose rate an extra
word overrides, and a stereo 44 kHz one with a loop; and the lists are made
the way MCC's are.
*/

#include "fsb5.h"
#include "mcc_sound_list.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do { if (!(condition)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

static void put32(uint8_t *at, uint32_t value)
{
	at[0] = (uint8_t)value;
	at[1] = (uint8_t)(value >> 8);
	at[2] = (uint8_t)(value >> 16);
	at[3] = (uint8_t)(value >> 24);
}

static void put64(uint8_t *at, uint64_t value)
{
	put32(at, (uint32_t)value);
	put32(at + 4, (uint32_t)(value >> 32));
}

/* a stream word: rate index, channels (0: one, 1: two), data offset (a multiple of 32), samples, extra flag */
static uint64_t stream_word(unsigned rate, unsigned channels, uint32_t data_offset, uint32_t samples, unsigned extra)
{
	return (uint64_t)extra | (uint64_t)rate << 1 | (uint64_t)channels << 5 | (uint64_t)(data_offset >> 5) << 7 | (uint64_t)samples << 34;
}

enum { STREAM_TABLE_BYTES = 44, NAME_TABLE_BYTES = 8 + 13 + 9, DATA_BYTES = 192, BANK_BYTES = 60 + STREAM_TABLE_BYTES + NAME_TABLE_BYTES + DATA_BYTES };

/* the two-stream bank, BANK_BYTES long */
static void bank_build(uint8_t *bank)
{
	uint8_t *table = bank + 60;
	uint8_t *names = table + STREAM_TABLE_BYTES;
	uint8_t *data = names + NAME_TABLE_BYTES;
	uint32_t index;

	memset(bank, 0, BANK_BYTES);
	memcpy(bank, "FSB5", 4);
	put32(bank + 4, 1);
	put32(bank + 8, 2);
	put32(bank + 12, STREAM_TABLE_BYTES);
	put32(bank + 16, NAME_TABLE_BYTES);
	put32(bank + 20, DATA_BYTES);
	put32(bank + 24, 7);
	/* stream 0: 22050 Hz (index 5), mono, 128 samples at 0, an extra word: rate 11025 */
	put64(table, stream_word(5, 0, 0, 128, 1));
	put32(table + 8, (2u << 25) | (4u << 1));
	put32(table + 12, 11025);
	/* stream 1: 44100 Hz (index 8), stereo, 64 samples at 96, extra words: an unknown type, then a loop (10 to 50) */
	put64(table + 16, stream_word(8, 1, 96, 64, 1));
	put32(table + 24, (9u << 25) | (4u << 1) | 1);
	put32(table + 28, 0xAABBCCDD);
	put32(table + 32, (3u << 25) | (8u << 1));
	put32(table + 36, 10);
	put32(table + 40, 50);
	put32(names, 8);
	put32(names + 4, 8 + 13);
	memcpy(names + 8, "brown2_1_0_0", 13);
	memcpy(names + 8 + 13, "run_3_12", 9);
	for (index = 0; index < DATA_BYTES; index++)
		data[index] = (uint8_t)index;
}

struct memory
{
	uint8_t const *bytes;
	uint64_t size;
};

static int memory_read(void *context, uint64_t offset, uint32_t size, void *buffer)
{
	struct memory const *memory = (struct memory const *)context;

	if (offset > memory->size || size > memory->size - offset)
		return 0;
	memcpy(buffer, memory->bytes + offset, size);
	return 1;
}

static int bank_open(uint8_t const *bytes, uint64_t size, int names, struct fsb5 *bank, char *error)
{
	struct memory memory = { bytes, size };

	return fsb5_open(memory_read, &memory, size, names, bank, error, 128);
}

static void test_fsb5(void)
{
	uint8_t bank_bytes[BANK_BYTES];
	uint8_t copy[BANK_BYTES];
	struct fsb5 bank;
	char error[128];
	struct memory memory;
	int what;

	bank_build(bank_bytes);
	if (!bank_open(bank_bytes, BANK_BYTES, 1, &bank, error))
	{
		printf("FAIL: the good bank did not open: %s\n", error);
		failures++;
		return;
	}
	CHECK(bank.version == 1 && bank.format == _fsb5_format_ima_adpcm && bank.stream_count == 2);
	CHECK(bank.data_offset == 60 + STREAM_TABLE_BYTES + NAME_TABLE_BYTES && bank.data_bytes == DATA_BYTES);
	CHECK(bank.streams[0].offset == bank.data_offset && bank.streams[0].bytes == 96);
	CHECK(bank.streams[0].samples == 128 && bank.streams[0].channels == 1 && bank.streams[0].rate == 11025 && !bank.streams[0].has_loop);
	CHECK(bank.streams[1].offset == bank.data_offset + 96 && bank.streams[1].bytes == 96);
	CHECK(bank.streams[1].samples == 64 && bank.streams[1].channels == 2 && bank.streams[1].rate == 44100);
	CHECK(bank.streams[1].has_loop && bank.streams[1].loop_start == 10 && bank.streams[1].loop_end == 50);
	CHECK(!strcmp(fsb5_stream_name(&bank, 0), "brown2_1_0_0") && !strcmp(fsb5_stream_name(&bank, 1), "run_3_12"));
	CHECK(bank.streams[0].name_range == 0 && bank.streams[0].name_permutation == 0);
	CHECK(bank.streams[1].name_range == 3 && bank.streams[1].name_permutation == 12);
	CHECK(fsb5_stream_name(&bank, 2) == NULL);
	CHECK(fsb5_ima_bytes(128, 1) == 72 && fsb5_ima_bytes(64, 2) == 72 && fsb5_ima_bytes(65, 1) == 72 && fsb5_ima_bytes(1, 2) == 72);
	CHECK(fsb5_ima_bytes(0, 1) == 0 && fsb5_ima_bytes(100, 0) == 0 && fsb5_ima_bytes(0xFFFFFFFFu, 8) == 0);
	fsb5_close(&bank);
	CHECK(bank.streams == NULL && bank.names == NULL);

	/* without the names */
	CHECK(bank_open(bank_bytes, BANK_BYTES, 0, &bank, error));
	CHECK(fsb5_stream_name(&bank, 0) == NULL && bank.streams[0].name_range == -1);
	fsb5_close(&bank);

	/* refused, each for its own reason, and nothing leaked (AddressSanitizer) */
	memcpy(copy, bank_bytes, BANK_BYTES);
	copy[0] = 'X';
	CHECK(!bank_open(copy, BANK_BYTES, 1, &bank, error) && strstr(error, "not an FSB5"));
	CHECK(bank.streams == NULL);
	memcpy(copy, bank_bytes, BANK_BYTES);
	put32(copy + 4, 2);
	CHECK(!bank_open(copy, BANK_BYTES, 1, &bank, error) && strstr(error, "version"));
	memcpy(copy, bank_bytes, BANK_BYTES);
	put32(copy + 8, 0);
	CHECK(!bank_open(copy, BANK_BYTES, 1, &bank, error));
	memcpy(copy, bank_bytes, BANK_BYTES);
	put32(copy + 8, 0x7FFFFFFF);
	CHECK(!bank_open(copy, BANK_BYTES, 1, &bank, error));
	memcpy(copy, bank_bytes, BANK_BYTES);
	put32(copy + 24, 99);
	CHECK(!bank_open(copy, BANK_BYTES, 1, &bank, error) && strstr(error, "format"));
	memcpy(copy, bank_bytes, BANK_BYTES);
	put32(copy + 20, DATA_BYTES + 1);
	CHECK(!bank_open(copy, BANK_BYTES, 1, &bank, error) && strstr(error, "file's"));
	CHECK(!bank_open(bank_bytes, BANK_BYTES - 1, 1, &bank, error));
	CHECK(!bank_open(bank_bytes, 10, 1, &bank, error));
	/* a stream past the data, and streams out of order */
	memcpy(copy, bank_bytes, BANK_BYTES);
	put64(copy + 60 + 16, stream_word(8, 1, 96 + 32 * 8, 64, 1));
	CHECK(!bank_open(copy, BANK_BYTES, 1, &bank, error));
	memcpy(copy, bank_bytes, BANK_BYTES);
	put64(copy + 60, stream_word(5, 0, 128, 128, 1));
	CHECK(!bank_open(copy, BANK_BYTES, 1, &bank, error) && strstr(error, "order"));
	/* a rate index FMOD does not use */
	memcpy(copy, bank_bytes, BANK_BYTES);
	put64(copy + 60 + 16, stream_word(13, 1, 96, 64, 1));
	CHECK(!bank_open(copy, BANK_BYTES, 1, &bank, error) && strstr(error, "rate"));
	/* extra data cut off by the table's end, and an extra word that never stops */
	memcpy(copy, bank_bytes, BANK_BYTES);
	put32(copy + 60 + 8, (2u << 25) | (400u << 1));
	CHECK(!bank_open(copy, BANK_BYTES, 1, &bank, error));
	memcpy(copy, bank_bytes, BANK_BYTES);
	put32(copy + 60 + 8, (2u << 25) | (4u << 1) | 1);
	put32(copy + 60 + 12, 0);
	CHECK(!bank_open(copy, BANK_BYTES, 1, &bank, error));
	/* a name past the name table */
	memcpy(copy, bank_bytes, BANK_BYTES);
	put32(copy + 60 + STREAM_TABLE_BYTES, 1000);
	CHECK(!bank_open(copy, BANK_BYTES, 1, &bank, error) && strstr(error, "name"));
	CHECK(bank_open(copy, BANK_BYTES, 0, &bank, error));
	fsb5_close(&bank);
	/* a name without its zero at the table's end is not a name */
	memcpy(copy, bank_bytes, BANK_BYTES);
	copy[60 + STREAM_TABLE_BYTES + NAME_TABLE_BYTES - 1] = 'x';
	CHECK(bank_open(copy, BANK_BYTES, 1, &bank, error));
	CHECK(fsb5_stream_name(&bank, 1) != NULL);
	fsb5_close(&bank);
	/* a read that fails */
	memory.bytes = bank_bytes;
	memory.size = 100;
	CHECK(!fsb5_open(memory_read, &memory, BANK_BYTES, 1, &bank, error, sizeof(error)));
	/* a version 0 bank has a 64-byte header */
	for (what = 0; what < 1; what++)
	{
		uint8_t v0[BANK_BYTES + 4];

		memset(v0, 0, sizeof(v0));
		memcpy(v0, bank_bytes, 60);
		memcpy(v0 + 64, bank_bytes + 60, BANK_BYTES - 60);
		put32(v0 + 4, 0);
		CHECK(bank_open(v0, sizeof(v0), 1, &bank, error));
		CHECK(bank.data_offset == 64 + STREAM_TABLE_BYTES + NAME_TABLE_BYTES && bank.streams[1].samples == 64);
		fsb5_close(&bank);
	}
	/* a CELT frame as FMOD writes one */
	{
		uint8_t frame[12] = { 0x17, 0xC3, 0x0D, 0xF3, 4, 0, 0, 0, 1, 2, 3, 4 };
		uint32_t payload = 0;

		CHECK(fsb5_celt_frame_check(frame, 12, &payload) && payload == 4);
		CHECK(!fsb5_celt_frame_check(frame, 11, &payload));
		frame[0] = 0;
		CHECK(!fsb5_celt_frame_check(frame, 12, NULL));
		CHECK(!fsb5_celt_frame_check(frame, 7, NULL));
	}
}

/* ---------- the sound list */

static size_t list_record(uint8_t *at, char const *path, uint32_t first, uint32_t streams)
{
	uint32_t length = (uint32_t)strlen(path) + 1;

	put32(at, length);
	memcpy(at + 4, path, length);
	put32(at + 4 + length, first);
	put32(at + 8 + length, streams);
	return 12 + length;
}

static size_t list_build(uint8_t *list)
{
	size_t size = 8;

	put32(list, 1);
	put32(list + 4, 4);
	size += list_record(list + size, "sound\\old_sfx\\weapons\\assault rifle\\fire", 0, 4);
	size += list_record(list + size, "sound\\old_sfx\\vehicles\\warthog_engine5\\med", 4, 3);
	size += list_record(list + size, "sound\\old_sfx\\vehicles\\warthog_engine5\\low", 7, 3);
	size += list_record(list + size, "levels\\test\\timberland\\sound\\timberland_stream", 10, 3);
	return size;
}

static void test_list(void)
{
	uint8_t bytes[1024];
	uint8_t copy[1024];
	size_t size = list_build(bytes);
	struct mcc_sound_list list;
	struct mcc_sound_list_record const *record;
	char error[128];

	CHECK(mcc_sound_list_parse(bytes, (uint32_t)size, &list, error, sizeof(error)));
	CHECK(list.count == 4);
	record = mcc_sound_list_find(&list, "sound\\old_sfx\\weapons\\assault rifle\\fire");
	CHECK(record && record->first == 0 && record->streams == 4);
	record = mcc_sound_list_find(&list, "SOUND/OLD_SFX/WEAPONS/ASSAULT RIFLE/FIRE.sound");
	CHECK(record && record->first == 0);
	record = mcc_sound_list_find(&list, "sound\\old_sfx\\vehicles\\warthog_engine5\\low");
	CHECK(record && record->first == 7 && record->streams == 3);
	CHECK(!strcmp(mcc_sound_list_path(&list, record), "sound\\old_sfx\\vehicles\\warthog_engine5\\low"));
	record = mcc_sound_list_find(&list, "levels\\test\\timberland\\sound\\timberland_stream");
	CHECK(record && record->first == 10);
	CHECK(!mcc_sound_list_find(&list, "sound\\old_sfx\\vehicles\\warthog_engine5"));
	CHECK(!mcc_sound_list_find(&list, "sound\\old_sfx\\weapons\\assault rifle\\fir"));
	CHECK(!mcc_sound_list_find(&list, ""));
	mcc_sound_list_free(&list);

	/* refused: a version, a count the file cannot hold, cut short, a path without its zero or with a control character, bytes after the end */
	memcpy(copy, bytes, size);
	put32(copy, 2);
	CHECK(!mcc_sound_list_parse(copy, (uint32_t)size, &list, error, sizeof(error)) && strstr(error, "version"));
	memcpy(copy, bytes, size);
	put32(copy + 4, 5);
	CHECK(!mcc_sound_list_parse(copy, (uint32_t)size, &list, error, sizeof(error)));
	memcpy(copy, bytes, size);
	put32(copy + 4, 0x7FFFFFFF);
	CHECK(!mcc_sound_list_parse(copy, (uint32_t)size, &list, error, sizeof(error)));
	CHECK(!mcc_sound_list_parse(bytes, (uint32_t)size - 1, &list, error, sizeof(error)));
	CHECK(!mcc_sound_list_parse(bytes, 7, &list, error, sizeof(error)));
	CHECK(!mcc_sound_list_parse(bytes, 0, &list, NULL, 0));
	memcpy(copy, bytes, size);
	copy[8 + 4 + 4] = 1;
	CHECK(!mcc_sound_list_parse(copy, (uint32_t)size, &list, error, sizeof(error)) && strstr(error, "text"));
	memcpy(copy, bytes, size);
	put32(copy + 8, 2000);
	CHECK(!mcc_sound_list_parse(copy, (uint32_t)size, &list, error, sizeof(error)));
	memcpy(copy, bytes, size);
	put32(copy + 8, 1);
	CHECK(!mcc_sound_list_parse(copy, (uint32_t)size, &list, error, sizeof(error)));
	memcpy(copy, bytes, size);
	copy[size] = 0;
	CHECK(!mcc_sound_list_parse(copy, (uint32_t)size + 1, &list, error, sizeof(error)) && strstr(error, "follow"));
	/* an empty list, and a path listed twice (the first is found) */
	memset(copy, 0, 8);
	put32(copy, 1);
	CHECK(mcc_sound_list_parse(copy, 8, &list, error, sizeof(error)) && list.count == 0 && !mcc_sound_list_find(&list, "a"));
	mcc_sound_list_free(&list);
	{
		size_t at = 8;

		put32(copy, 1);
		put32(copy + 4, 2);
		at += list_record(copy + at, "sound\\a", 5, 1);
		at += list_record(copy + at, "SOUND\\A", 9, 1);
		CHECK(mcc_sound_list_parse(copy, (uint32_t)at, &list, error, sizeof(error)));
		record = mcc_sound_list_find(&list, "sound\\a");
		CHECK(record && record->first == 5);
		mcc_sound_list_free(&list);
	}
}

int main(void)
{
	test_fsb5();
	test_list();
	if (failures)
	{
		printf("mcc_sound_test: %d failures\n", failures);
		return 1;
	}
	printf("mcc_sound_test: ok\n");
	return 0;
}
