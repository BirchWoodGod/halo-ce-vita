/*
MCC_SOUND_FUZZ.C

A fuzz target for MCC's sound banks and lists (port/linux/src/fsb5.c,
mcc_sound_list.c): the player's own files, but taken as a stranger's (a
shared folder, a damaged card). The input's first byte says which parser
reads the rest: even, an FSB5 bank (with its names read when bit 1 is set),
odd, a sound list. Each input is copied to a heap block of exactly its size,
so that AddressSanitizer sees a read past it. Every input must only be
refused or accepted; an accepted bank must keep every stream's bytes within
its file and within its data, with names that end inside their table, and an
accepted list must find each of its paths again.

run_mcc_sound_test.sh builds it with AddressSanitizer and UBSan, with
libFuzzer (MCC_FUZZ_SECONDS) and without it, when main below runs
MCC_FUZZ_ITERATIONS changes (bytes flipped or set to edge values, cut,
extended, repeated, spliced) of a made-up bank and list from a fixed seed.
*/

#include "fsb5.h"
#include "mcc_sound_list.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

static unsigned long accepted_banks, refused_banks, accepted_lists, refused_lists;

static void check_bank(uint8_t const *bytes, size_t size, int names)
{
	struct memory memory = { bytes, size };
	struct fsb5 bank;
	char error[96];
	uint32_t index;
	uint64_t total = 0;

	if (!fsb5_open(memory_read, &memory, size, names, &bank, error, sizeof(error)))
	{
		refused_banks++;
		if (bank.streams || bank.names)
			abort();
		return;
	}
	accepted_banks++;
	if (bank.data_offset > size || bank.data_bytes > size - bank.data_offset || bank.stream_count == 0)
		abort();
	for (index = 0; index < bank.stream_count; index++)
	{
		struct fsb5_stream const *stream = &bank.streams[index];
		char const *name = fsb5_stream_name(&bank, index);

		if (stream->offset < bank.data_offset || stream->offset + stream->bytes > bank.data_offset + bank.data_bytes)
			abort();
		if (index && stream->offset < bank.streams[index - 1].offset)
			abort();
		total += stream->bytes;
		if (stream->channels == 0 || stream->rate == 0)
			abort();
		if (name && strlen(name) >= bank.names_bytes)
			abort();
		if (!names && name)
			abort();
		(void)fsb5_ima_bytes(stream->samples, stream->channels);
	}
	if (total > bank.data_bytes)
		abort();
	fsb5_close(&bank);
}

static void check_list(uint8_t const *bytes, size_t size)
{
	struct mcc_sound_list list;
	char error[96];
	uint32_t index;

	if (!mcc_sound_list_parse(bytes, (uint32_t)size, &list, error, sizeof(error)))
	{
		refused_lists++;
		if (list.records || list.paths)
			abort();
		return;
	}
	accepted_lists++;
	for (index = 0; index < list.count; index++)
	{
		char const *path = mcc_sound_list_path(&list, &list.records[index]);
		struct mcc_sound_list_record const *found = mcc_sound_list_find(&list, path);

		if (!found || strcasecmp(mcc_sound_list_path(&list, found), path))
			abort();
	}
	mcc_sound_list_free(&list);
}

int LLVMFuzzerTestOneInput(uint8_t const *data, size_t size)
{
	uint8_t *copy;

	if (size < 1 || size > 1024 * 1024)
		return 0;
	copy = (uint8_t *)malloc(size - 1 ? size - 1 : 1);
	if (!copy)
		return 0;
	memcpy(copy, data + 1, size - 1);
	if (data[0] & 1)
		check_list(copy, size - 1);
	else
		check_bank(copy, size - 1, (data[0] >> 1) & 1);
	free(copy);
	return 0;
}

#ifndef LIBFUZZER

static unsigned long long random_state = 0x2545F4914F6CDD1Dull;

static unsigned long next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return (unsigned long)(random_state >> 11);
}

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

/* a bank of `count` streams of 64 samples each, 72 bytes, 96 apart; with names, and an extra word on the first */
static size_t bank_seed(uint8_t *out, unsigned count, int with_names)
{
	uint32_t table_bytes = count * 8 + 12;
	uint32_t names_bytes = with_names ? count * 4 + count * 12 : 0;
	uint32_t data_bytes = count * 96;
	uint8_t *table = out + 60;
	uint8_t *names = table + table_bytes;
	unsigned index;

	memset(out, 0, 60 + table_bytes + names_bytes + data_bytes);
	memcpy(out, "FSB5", 4);
	put32(out + 4, 1);
	put32(out + 8, count);
	put32(out + 12, table_bytes);
	put32(out + 16, names_bytes);
	put32(out + 20, data_bytes);
	put32(out + 24, 7);
	for (index = 0; index < count; index++)
	{
		uint64_t word = (5ull << 1) | (uint64_t)((index * 96) >> 5) << 7 | 64ull << 34 | (index == 0 ? 1 : 0);

		put64(table + index * 8 + (index ? 12 : 0), word);
	}
	put32(table + 8, (3u << 25) | (8u << 1));
	put32(table + 12, 5);
	put32(table + 16, 20);
	for (index = 0; index < count; index++)
	{
		put32(names + index * 4, count * 4 + index * 12);
		snprintf((char *)names + count * 4 + index * 12, 12, "s%u_0_%u", index % 100, index % 100);
	}
	return 60 + table_bytes + names_bytes + data_bytes;
}

static size_t list_seed(uint8_t *out)
{
	static char const *const paths[] = { "sound\\old_sfx\\a\\b", "sound\\old_sfx\\a\\c\\low", "levels\\x\\sound\\y" };
	size_t at = 8;
	unsigned index;

	put32(out, 1);
	put32(out + 4, 3);
	for (index = 0; index < 3; index++)
	{
		uint32_t length = (uint32_t)strlen(paths[index]) + 1;

		put32(out + at, length);
		memcpy(out + at + 4, paths[index], length);
		put32(out + at + 4 + length, index * 3);
		put32(out + at + 8 + length, 3);
		at += 12 + length;
	}
	return at;
}

static size_t mutate(uint8_t *data, size_t size, size_t capacity, uint8_t const *other, size_t other_size)
{
	unsigned changes = 1 + next_random() % 4;

	while (changes--)
	{
		switch (next_random() % 7)
		{
		case 0:
			if (size > 1)
				data[1 + next_random() % (size - 1)] ^= (uint8_t)(1u << (next_random() % 8));
			break;
		case 1:
			if (size > 1)
				data[1 + next_random() % (size - 1)] = (uint8_t)next_random();
			break;
		case 2:
			if (size > 4)
			{
				static uint32_t const edges[] = { 0, 1, 0x7F, 0x80, 0xFF, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu, 0xFFFF, 0x10000 };

				put32(data + 1 + next_random() % (size - 4), edges[next_random() % 10]);
			}
			break;
		case 3:
			if (size > 2)
				size = 1 + next_random() % (size - 1);
			break;
		case 4:
			if (size + 16 < capacity)
			{
				size_t extra = 1 + next_random() % 16;

				while (extra--)
					data[size++] = (uint8_t)next_random();
			}
			break;
		case 5:
			if (size > 16 && size + 16 < capacity)
			{
				size_t from = 1 + next_random() % (size - 8);

				memmove(data + from + 8, data + from, size - from);
				memcpy(data + from, data + 1 + next_random() % (size - 8), 8);
				size += 8;
			}
			break;
		default:
			if (other_size > 8 && size > 8)
			{
				size_t length = 1 + next_random() % 8;
				size_t from = 1 + next_random() % (other_size - length);

				memcpy(data + 1 + next_random() % (size - length), other + from, length);
			}
			break;
		}
	}
	return size;
}

int main(void)
{
	static uint8_t seeds[4][4096];
	static size_t seed_sizes[4];
	static uint8_t input[8192];
	unsigned long iterations = getenv("MCC_FUZZ_ITERATIONS") ? strtoul(getenv("MCC_FUZZ_ITERATIONS"), NULL, 10) : 20000;
	unsigned long iteration;
	int index;

	/* seeds: the first byte says the parser (bit 1: names) */
	seeds[0][0] = 2;
	seed_sizes[0] = 1 + bank_seed(seeds[0] + 1, 2, 1);
	seeds[1][0] = 0;
	seed_sizes[1] = 1 + bank_seed(seeds[1] + 1, 5, 0);
	seeds[2][0] = 1;
	seed_sizes[2] = 1 + list_seed(seeds[2] + 1);
	seeds[3][0] = 2;
	seed_sizes[3] = 1 + bank_seed(seeds[3] + 1, 17, 1);
	for (index = 0; index < 4; index++)
		LLVMFuzzerTestOneInput(seeds[index], seed_sizes[index]);
	if (!accepted_banks || !accepted_lists)
	{
		printf("FAIL: the seeds were not accepted (%lu banks, %lu lists)\n", accepted_banks, accepted_lists);
		return 1;
	}
	for (iteration = 0; iteration < iterations; iteration++)
	{
		int seed = (int)(next_random() % 4);
		int other = (int)(next_random() % 4);
		size_t size = seed_sizes[seed];

		memcpy(input, seeds[seed], size);
		size = mutate(input, size, sizeof(input), seeds[other], seed_sizes[other]);
		LLVMFuzzerTestOneInput(input, size);
	}
	printf("PASS: %lu changed inputs: %lu banks accepted, %lu refused; %lu lists accepted, %lu refused\n",
		iterations + 4, accepted_banks, refused_banks, accepted_lists, refused_lists);
	return 0;
}

#endif
