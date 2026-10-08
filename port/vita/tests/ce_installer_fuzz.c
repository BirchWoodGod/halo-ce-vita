/*
CE_INSTALLER_FUZZ.C

A fuzz target for the Custom Edition installer extraction
(port/linux/src/posix_ce_installer.c, with libmspack's cabinet reader and
LZX decoder): the input's first byte picks what the rest is -
  even: an installer file as it is (the program's headers, its resource
        tree, the cabinets found in it or by looking through it, their
        headers and blocks, the maps' headers);
  odd:  the compressed data of a cabinet's one LZX folder (its window from
        the byte, 32 KB to 2 MB), holding loc.map, in blocks of up to
        32 KB each, without checksums: libmspack's LZX decoder on anything.
Every input must only fail or succeed: run_ce_installer_test.sh builds it
with AddressSanitizer and UBSan, with libFuzzer (CE_FUZZ_SECONDS), and
without it, when main below runs the made-up installers of
ce_installer_build.h and CE_FUZZ_ITERATIONS changes of them (bytes flipped,
set to edge values, cut, repeated), from a fixed seed.
*/

#include "ce_installer.h"
#include "ce_installer_build.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char fuzz_folder[512], fuzz_maps[600], fuzz_installer[700];

static void fuzz_setup(void)
{
	if (fuzz_folder[0])
		return;
	snprintf(fuzz_folder, sizeof(fuzz_folder), "%s/ce_installer_fuzz.%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp",
		(int)getpid());
	snprintf(fuzz_maps, sizeof(fuzz_maps), "%s/maps", fuzz_folder);
	snprintf(fuzz_installer, sizeof(fuzz_installer), "%s/halocesetup.exe", fuzz_folder);
	mkdir(fuzz_folder, 0777);
	mkdir(fuzz_maps, 0777);
}

static void fuzz_clear(void)
{
	static const char *const names[] = { "bitmaps.map", "sounds.map", "loc.map" };
	unsigned int index;

	for (index = 0; index < 3; index++)
	{
		char path[800];

		snprintf(path, sizeof(path), "%s/%s", fuzz_maps, names[index]);
		remove(path);
		snprintf(path, sizeof(path), "%s/%s.extract", fuzz_maps, names[index]);
		remove(path);
	}
}

static int fuzz_run(const unsigned char *data, unsigned long size)
{
	FILE *file = fopen(fuzz_installer, "wb");
	char error[512];
	int result;

	if (!file)
		return -1;
	if (size)
		fwrite(data, 1, size, file);
	fclose(file);
	result = ce_installer_extract(fuzz_installer, fuzz_maps, CE_INSTALLER_ALL, NULL, NULL, error, sizeof(error));
	/* (a result is one of the three, with a reason when it failed) */
	if (result != CE_INSTALLER_DONE && (result != CE_INSTALLER_FAILED || !error[0]))
		abort();
	if (result == CE_INSTALLER_DONE && ce_installer_missing(fuzz_maps))
		abort();
	fuzz_clear();
	return result;
}

/* a cabinet around an LZX stream: one folder of `window` bits holding
loc.map from its start (so all of the stream is unpacked), then bitmaps.map
and sounds.map, 16 bytes each at its end; the stream in blocks of up to
38 KB (libmspack's most) that each give 32 KB */
static struct build_buffer lzx_cabinet(const unsigned char *stream, unsigned long size, int window)
{
	enum { BLOCK_INPUT = 32768 + 6144 };
	struct build_buffer cabinet = { 0 };
	unsigned long blocks = size ? (size + BLOCK_INPUT - 1) / BLOCK_INPUT : 1, length = blocks * 32768, index, offset;
	static const char *const names[] = { "maps\\loc.map", "maps\\bitmaps.map", "maps\\sounds.map" };
	const unsigned long sizes[] = { length - 32, 16, 16 }, offsets[] = { 0, length - 32, length - 16 };
	unsigned long names_size = 0, data_offset;

	for (index = 0; index < 3; index++)
		names_size += strlen(names[index]) + 1;
	data_offset = 36 + 8 + 3 * 16 + names_size;
	build_bytes(&cabinet, "MSCF", 4);
	build_u32(&cabinet, 0);
	build_u32(&cabinet, data_offset + blocks * 8 + size);
	build_u32(&cabinet, 0);
	build_u32(&cabinet, 44);
	build_u32(&cabinet, 0);
	build_bytes(&cabinet, "\3\1", 2);
	build_u16(&cabinet, 1);
	build_u16(&cabinet, 3);
	build_u32(&cabinet, 0);
	build_u16(&cabinet, 0);
	build_u32(&cabinet, data_offset);
	build_u16(&cabinet, blocks);
	build_u16(&cabinet, 3 | (unsigned long)window << 8);
	for (index = 0; index < 3; index++)
	{
		build_u32(&cabinet, sizes[index]);
		build_u32(&cabinet, offsets[index]);
		build_u16(&cabinet, 0);
		build_u32(&cabinet, 0);
		build_u16(&cabinet, 0x20);
		build_bytes(&cabinet, names[index], strlen(names[index]) + 1);
	}
	for (offset = 0, index = 0; index < blocks; index++)
	{
		unsigned long bytes = size - offset > BLOCK_INPUT ? BLOCK_INPUT : size - offset;

		build_u32(&cabinet, 0);
		build_u16(&cabinet, bytes);
		build_u16(&cabinet, 32768);
		build_bytes(&cabinet, stream + offset, bytes);
		offset += bytes;
	}
	return cabinet;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	fuzz_setup();
	if (!size || size > 4 * 1024 * 1024)
		return 0;
	if (data[0] & 1)
	{
		struct build_buffer cabinet = lzx_cabinet(data + 1, (unsigned long)size - 1, 15 + (data[0] >> 1) % 7);

		fuzz_run(cabinet.data, cabinet.size);
		free(cabinet.data);
	}
	else
		fuzz_run(data + 1, (unsigned long)size - 1);
	return 0;
}

#ifndef CE_FUZZ_LIBFUZZER
static unsigned long long random_state = 0x9E3779B97F4A7C15ULL;

static unsigned long next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return (unsigned long)(random_state >> 11);
}

/* a change of the seed: bytes flipped or set to an edge value, a 32-bit
edge value, a cut, a part repeated */
static unsigned long mutate(unsigned char *data, unsigned long size, unsigned long capacity)
{
	static const unsigned long edges[] = { 0, 1, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000UL,
		0xFFFFFFFFUL, 32768, 32769, 65535 };
	unsigned long changes = 1 + next_random() % 4, change;

	for (change = 0; change < changes && size > 1; change++)
	{
		unsigned long at = 1 + next_random() % (size - 1);

		switch (next_random() % 6)
		{
		case 0:
			data[at] ^= (unsigned char)(1 << (next_random() % 8));
			break;
		case 1:
			data[at] = (unsigned char)edges[next_random() % 5];
			break;
		case 2:
			if (at + 4 <= size)
				put32(data + at, edges[next_random() % (sizeof(edges) / sizeof(edges[0]))]);
			break;
		case 3:
			if (at + 2 <= size)
				put16(data + at, edges[next_random() % 8]);
			break;
		case 4:
			size = at;
			break;
		default:
		{
			unsigned long length = 1 + next_random() % 64;

			if (at + length <= size && size + length <= capacity)
			{
				memmove(data + at + length, data + at, size - at);
				size += length;
			}
			break;
		}
		}
	}
	return size;
}

int main(int argc, char **argv)
{
	unsigned long iterations = getenv("CE_FUZZ_ITERATIONS") ? strtoul(getenv("CE_FUZZ_ITERATIONS"), NULL, 10) : 20000;
	unsigned char *bitmaps = build_resource_map(1, 3000, 1), *sounds = build_resource_map(2, 2000, 2),
		*loc = build_resource_map(3, 1000, 3);
	struct build_file files[] = {
		{ "maps\\loc.map", loc, 1000, 0 },
		{ "maps\\bitmaps.map", bitmaps, 3000, 1 },
		{ "maps\\sounds.map", sounds, 2000, 1 },
	};
	struct build_buffer seeds[5];
	unsigned long index, results[3] = { 0, 0, 0 };
	unsigned char *work;
	unsigned long capacity = 128 * 1024;
	int seed;

	fuzz_setup();
	/* the kept cases first (each file named) */
	for (seed = 1; seed < argc; seed++)
	{
		FILE *file = fopen(argv[seed], "rb");
		static unsigned char data[1 << 20];
		size_t size;

		if (!file)
			continue;
		size = fread(data, 1, sizeof(data), file);
		fclose(file);
		LLVMFuzzerTestOneInput(data, size);
	}
	{
		struct build_buffer stored = build_cabinet(files, 3, BUILD_STORED, 1);
		struct build_buffer lzx = build_cabinet(files, 3, BUILD_LZX, 1);
		unsigned char *block = build_resource_map(3, 32768, 5);

		seeds[0] = build_installer(stored.data, stored.size, 1);
		seeds[1] = build_installer(lzx.data, lzx.size, 1);
		seeds[2] = build_installer(lzx.data, lzx.size, 0);
		seeds[3] = lzx;
		/* (an LZX stream: an uncompressed block of 32 KB, a whole frame) */
		memset(&seeds[4], 0, sizeof(seeds[4]));
		build_lzx_stream(&seeds[4], block, 32768);
		free(block);
		free(stored.data);
	}
	/* (the seeds: each is an installer whose maps come out) */
	for (seed = 0; seed < 5; seed++)
	{
		struct build_buffer cabinet = { 0 };
		int result;

		if (seed == 4)
			cabinet = lzx_cabinet(seeds[4].data, seeds[4].size, 21);
		result = seed == 4 ? fuzz_run(cabinet.data, cabinet.size) : fuzz_run(seeds[seed].data, seeds[seed].size);
		free(cabinet.data);
		/* (the LZX stream's maps are no resource maps: it unpacks, then fails
		the header check) */
		if (seed < 4 ? result != CE_INSTALLER_DONE : result != CE_INSTALLER_FAILED)
		{
			fprintf(stderr, "FAIL seed %d gives %d\n", seed, result);
			return 1;
		}
	}
	work = malloc(capacity);
	for (index = 0; index < iterations; index++)
	{
		const struct build_buffer *source = &seeds[index % 5];
		unsigned long size = source->size + 1 > capacity ? capacity : source->size + 1;
		int result;

		/* (the first byte: an installer as it is, or the LZX stream's window) */
		work[0] = index % 5 == 4 ? (unsigned char)(1 | (next_random() % 7) << 1) : 0;
		memcpy(work + 1, source->data, size - 1);
		size = mutate(work, size, capacity);
		if (work[0] & 1)
		{
			struct build_buffer cabinet = lzx_cabinet(work + 1, size - 1, 15 + (work[0] >> 1) % 7);

			result = fuzz_run(cabinet.data, cabinet.size);
			free(cabinet.data);
		}
		else
			result = fuzz_run(work + 1, size - 1);
		if (result >= 0 && result < 3)
			results[result]++;
	}
	printf("fuzz: %lu changed installers and LZX streams: %lu extracted, %lu refused\n", iterations, results[0],
		results[1]);
	printf("PASS fuzz: no fault\n");
	free(work);
	for (seed = 0; seed < 5; seed++)
		free(seeds[seed].data);
	free(bitmaps);
	free(sounds);
	free(loc);
	fuzz_clear();
	rmdir(fuzz_maps);
	remove(fuzz_installer);
	rmdir(fuzz_folder);
	return 0;
}
#endif
