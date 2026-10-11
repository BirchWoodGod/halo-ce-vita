/*
CE_INSTALLER_TEST.C

A desktop test of the Custom Edition installer extraction
(port/linux/src/posix_ce_installer.c), on made-up installers
(ce_installer_build.h): the cabinet found among a Windows program's
resources, or by looking through the file (a .cab alone, one after the
program); the three resource maps written, stored and LZX-compressed, and
only the ones asked for; a map already in the folder kept; the best named
of two; and refused, leaving nothing behind (no <name>.extract, no map): a
block whose checksum is wrong, a map whose header is not its type's, a map
missing from the cabinet, a map too large, an installer too large, a file
that is no installer, a stop from the progress; then every truncation of an
installer and every byte of its headers changed, which must only fail.
Built with AddressSanitizer and UBSan by run_ce_installer_test.sh.

Run port/vita/tests/run_ce_installer_test.sh.
*/

#include "ce_installer.h"
#include "ce_installer_build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures, checks;
static char folder[512], maps[600], installer[600];

#define CHECK(condition) check((condition) != 0, #condition, __LINE__)

static void check(int ok, const char *what, int line)
{
	checks++;
	if (!ok)
	{
		failures++;
		fprintf(stderr, "FAIL line %d: %s\n", line, what);
	}
}

static void write_file(const char *path, const void *data, unsigned long size)
{
	FILE *file = fopen(path, "wb");

	if (!file || fwrite(data, 1, size, file) != size || fclose(file) != 0)
	{
		fprintf(stderr, "cannot write %s\n", path);
		exit(1);
	}
}

/* nonzero if the file holds exactly these bytes */
static int file_is(const char *path, const unsigned char *data, unsigned long size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *bytes = malloc(size + 1);
	size_t got;
	int same;

	if (!file || !bytes)
	{
		if (file)
			fclose(file);
		free(bytes);
		return 0;
	}
	got = fread(bytes, 1, size + 1, file);
	fclose(file);
	same = got == size && memcmp(bytes, data, size) == 0;
	free(bytes);
	return same;
}

static int exists(const char *name)
{
	char path[700];
	struct stat status;

	snprintf(path, sizeof(path), "%s/%s", maps, name);
	return stat(path, &status) == 0;
}

static void clear_maps(void)
{
	static const char *const names[] = { "bitmaps.map", "sounds.map", "loc.map", "bitmaps.map.extract",
		"sounds.map.extract", "loc.map.extract" };
	unsigned int index;

	for (index = 0; index < sizeof(names) / sizeof(names[0]); index++)
	{
		char path[700];

		snprintf(path, sizeof(path), "%s/%s", maps, names[index]);
		remove(path);
	}
}

/* nothing left behind by a refused extraction */
static int folder_clean(void)
{
	return !exists("bitmaps.map") && !exists("sounds.map") && !exists("loc.map") && !exists("bitmaps.map.extract") &&
		!exists("sounds.map.extract") && !exists("loc.map.extract");
}

struct progress_record
{
	unsigned long long last_done, total;
	int calls, backwards, stop_after;
};

static int record_progress(void *context, const char *file, unsigned long long done, unsigned long long total)
{
	struct progress_record *record = context;

	(void)file;
	if (done < record->last_done)
		record->backwards = 1;
	record->last_done = done;
	record->total = total;
	record->calls++;
	return record->stop_after && record->calls >= record->stop_after;
}

static int extract(const struct build_buffer *file, int which, struct progress_record *record, char *error)
{
	write_file(installer, file->data, file->size);
	return ce_installer_extract(installer, maps, which, record ? record_progress : NULL, record, error, 512);
}

/* the three maps (bitmaps 3 MB: several progress steps), and others */
static unsigned char *bitmaps, *sounds, *loc, *other;
static const unsigned long bitmaps_size = 3 * 1024 * 1024 + 77, sounds_size = 200000, loc_size = 20000;

static struct build_buffer standard_cabinet(int compression, int checksums)
{
	struct build_file files[] = {
		/* (folder 0: LZX with `compression`, which holds 32 KB at most) */
		{ "maps\\loc.map", loc, loc_size, 0 },
		{ "Eula.rtf", other, 1000, 0 },
		{ "redist\\instmsia.exe", other, 2000, 1 },
		{ "maps\\bitmaps.map", bitmaps, bitmaps_size, 1 },
		{ "maps\\beavercreek.map", other, 5000, 1 },
		{ "maps\\sounds.map", sounds, sounds_size, 1 },
	};

	return build_cabinet(files, (int)(sizeof(files) / sizeof(files[0])), compression, checksums);
}

static void test_extracts(void)
{
	struct build_buffer cabinet = standard_cabinet(BUILD_LZX, 1);
	struct build_buffer program = build_installer(cabinet.data, cabinet.size, 1);
	struct progress_record record;
	char error[512];

	clear_maps();
	CHECK(ce_installer_missing(maps) == CE_INSTALLER_ALL);
	memset(&record, 0, sizeof(record));
	CHECK(extract(&program, CE_INSTALLER_ALL, &record, error) == CE_INSTALLER_DONE);
	CHECK(error[0] == 0);
	CHECK(ce_installer_missing(maps) == 0);
	{
		char path[700];

		snprintf(path, sizeof(path), "%s/bitmaps.map", maps);
		CHECK(file_is(path, bitmaps, bitmaps_size));
		snprintf(path, sizeof(path), "%s/sounds.map", maps);
		CHECK(file_is(path, sounds, sounds_size));
		snprintf(path, sizeof(path), "%s/loc.map", maps);
		CHECK(file_is(path, loc, loc_size));
	}
	CHECK(!exists("bitmaps.map.extract") && !exists("loc.map.extract") && !exists("sounds.map.extract"));
	/* (the work: each folder unpacked to the end of the last map in it) */
	CHECK(record.total == loc_size + (2000 + bitmaps_size + 5000 + sounds_size));
	CHECK(record.last_done == record.total && record.calls >= 3 && !record.backwards);

	/* only the ones asked for; one in the folder already stays as it is */
	clear_maps();
	write_file(installer, "x", 1);
	{
		char path[700];

		snprintf(path, sizeof(path), "%s/sounds.map", maps);
		write_file(path, "mine", 4);
		CHECK(ce_installer_missing(maps) == (CE_INSTALLER_BITMAPS | CE_INSTALLER_LOC));
		CHECK(extract(&program, CE_INSTALLER_LOC, NULL, error) == CE_INSTALLER_DONE);
		CHECK(exists("loc.map") && !exists("bitmaps.map"));
		CHECK(extract(&program, CE_INSTALLER_ALL, NULL, error) == CE_INSTALLER_DONE);
		CHECK(file_is(path, (const unsigned char *)"mine", 4));
		CHECK(!exists("sounds.map.extract"));
	}
	free(program.data);

	/* stored, without checksums; a .cab alone; a cabinet after the program
	(not among its resources) */
	free(cabinet.data);
	cabinet = standard_cabinet(BUILD_STORED, 0);
	clear_maps();
	CHECK(extract(&cabinet, CE_INSTALLER_ALL, NULL, error) == CE_INSTALLER_DONE);
	CHECK(ce_installer_missing(maps) == 0);
	program = build_installer(cabinet.data, cabinet.size, 0);
	clear_maps();
	CHECK(extract(&program, CE_INSTALLER_ALL, NULL, error) == CE_INSTALLER_DONE);
	CHECK(ce_installer_missing(maps) == 0);
	free(program.data);
	free(cabinet.data);
}

static void test_names(void)
{
	/* maps\<name> before <name> elsewhere, in any case and either slash */
	unsigned char *decoy = build_resource_map(1, 5000, 99);
	struct build_file files[] = {
		{ "data_files\\bitmaps.map", decoy, 5000, 0 },
		{ "MAPS/BITMAPS.MAP", bitmaps, 40000, 0 },
		{ "Maps\\Sounds.Map", sounds, sounds_size, 1 },
		{ "maps\\loc.map", loc, loc_size, 1 },
		{ "maps\\loc.map.bak", other, 100, 1 },
	};
	struct build_buffer cabinet, program;
	char error[512], path[700];

	put32(bitmaps + 4, 40000);
	put32(bitmaps + 8, 40000);
	cabinet = build_cabinet(files, 5, BUILD_STORED, 1);
	program = build_installer(cabinet.data, cabinet.size, 1);
	clear_maps();
	CHECK(extract(&program, CE_INSTALLER_ALL, NULL, error) == CE_INSTALLER_DONE);
	snprintf(path, sizeof(path), "%s/bitmaps.map", maps);
	CHECK(file_is(path, bitmaps, 40000));
	put32(bitmaps + 4, bitmaps_size);
	put32(bitmaps + 8, bitmaps_size);
	free(program.data);
	free(cabinet.data);
	free(decoy);
}

static void test_refusals(void)
{
	struct build_buffer cabinet = standard_cabinet(BUILD_STORED, 1), program;
	struct progress_record record;
	char error[512];

	/* a block's checksum wrong: in bitmaps.map's folder (folder 1) */
	program = build_installer(cabinet.data, cabinet.size, 1);
	{
		unsigned long index;

		for (index = 0; index + 64 <= program.size; index++)
			if (!memcmp(program.data + index, sounds + 1000, 64))
				break;
		CHECK(index + 64 <= program.size);
		if (index + 64 <= program.size)
			program.data[index + 10] ^= 0x40;
	}
	clear_maps();
	CHECK(extract(&program, CE_INSTALLER_ALL, NULL, error) == CE_INSTALLER_FAILED);
	CHECK(strstr(error, "checksum") != NULL);
	/* (loc.map, in the folder before, is whole and kept) */
	CHECK(exists("loc.map") && !exists("sounds.map") && !exists("sounds.map.extract") && !exists("bitmaps.map.extract"));
	free(program.data);

	/* a stop from the progress: nothing left */
	program = build_installer(cabinet.data, cabinet.size, 1);
	clear_maps();
	memset(&record, 0, sizeof(record));
	record.stop_after = 2;
	CHECK(extract(&program, CE_INSTALLER_ALL, &record, error) == CE_INSTALLER_STOPPED);
	CHECK(!exists("bitmaps.map") && !exists("bitmaps.map.extract") && !exists("sounds.map"));
	free(program.data);
	free(cabinet.data);

	/* a map whose header is another type's */
	{
		struct build_file files[] = {
			{ "maps\\bitmaps.map", sounds, sounds_size, 0 },
			{ "maps\\sounds.map", sounds, sounds_size, 0 },
			{ "maps\\loc.map", loc, loc_size, 0 },
		};

		cabinet = build_cabinet(files, 3, BUILD_STORED, 1);
		clear_maps();
		CHECK(extract(&cabinet, CE_INSTALLER_ALL, NULL, error) == CE_INSTALLER_FAILED);
		CHECK(strstr(error, "bitmaps.map") && strstr(error, "resource map"));
		CHECK(folder_clean());
		free(cabinet.data);
	}
	/* loc.map missing: nothing written */
	{
		struct build_file files[] = {
			{ "maps\\bitmaps.map", bitmaps, bitmaps_size, 0 },
			{ "maps\\sounds.map", sounds, sounds_size, 0 },
		};

		cabinet = build_cabinet(files, 2, BUILD_STORED, 1);
		clear_maps();
		CHECK(extract(&cabinet, CE_INSTALLER_ALL, NULL, error) == CE_INSTALLER_FAILED);
		CHECK(strstr(error, "no loc.map") != NULL);
		CHECK(folder_clean());
		/* (but asked for the two alone, it does) */
		CHECK(extract(&cabinet, CE_INSTALLER_BITMAPS | CE_INSTALLER_SOUNDS, NULL, error) == CE_INSTALLER_DONE);
		CHECK(exists("bitmaps.map") && exists("sounds.map"));
		clear_maps();
		free(cabinet.data);
	}
	/* a map larger than its cap (loc.map: 16 MB) is not taken: its header
	says 20 MB */
	{
		struct build_buffer forged = standard_cabinet(BUILD_STORED, 1);
		unsigned char *entry = NULL;
		unsigned long index;

		for (index = 0; index + 12 < forged.size; index++)
			if (!memcmp(forged.data + index, "maps\\loc.map", 13))
				entry = forged.data + index - 16;
		CHECK(entry != NULL);
		if (entry)
			put32(entry, 20UL * 1024 * 1024);
		clear_maps();
		CHECK(extract(&forged, CE_INSTALLER_ALL, NULL, error) == CE_INSTALLER_FAILED);
		CHECK(strstr(error, "no loc.map") != NULL);
		CHECK(folder_clean());
		free(forged.data);
	}
	/* no installer at all */
	{
		static const char words[] = "MZ just some text, and MSCF\0\0\0\0 that is no cabinet";
		struct build_buffer text = { 0 };
		char path[700];

		build_bytes(&text, words, sizeof(words) - 1);
		CHECK(extract(&text, CE_INSTALLER_ALL, NULL, error) == CE_INSTALLER_FAILED);
		CHECK(strstr(error, "not a Halo Custom Edition installer") != NULL);
		free(text.data);
		snprintf(path, sizeof(path), "%s/none.exe", folder);
		CHECK(ce_installer_extract(path, maps, CE_INSTALLER_ALL, NULL, NULL, error, 512) == CE_INSTALLER_FAILED);
		CHECK(strstr(error, "cannot be read") != NULL);
	}
	/* an installer over 1 GB (a sparse file) */
	{
		FILE *file = fopen(installer, "wb");

		CHECK(file && fseeko(file, (off_t)1100 * 1024 * 1024, SEEK_SET) == 0 && fputc(0, file) == 0);
		if (file)
			fclose(file);
		CHECK(ce_installer_extract(installer, maps, CE_INSTALLER_ALL, NULL, NULL, error, 512) == CE_INSTALLER_FAILED);
		CHECK(strstr(error, "too large") != NULL);
		remove(installer);
	}
}

static void test_find(void)
{
	char path[700], found[700];
	unsigned long long size = 0;

	CHECK(!ce_installer_find(folder, found, sizeof(found), &size));
	snprintf(path, sizeof(path), "%s/halocesetup.txt", folder);
	write_file(path, "x", 1);
	snprintf(path, sizeof(path), "%s/HaloCESetup_EN_1.00.EXE", folder);
	write_file(path, "12345", 5);
	CHECK(ce_installer_find(folder, found, sizeof(found), &size) && size == 5 && !strcmp(found, path));
	remove(path);
	snprintf(path, sizeof(path), "%s/halocesetup.txt", folder);
	remove(path);
	CHECK(!ce_installer_find(folder, found, sizeof(found), &size));
	CHECK(!strcmp(ce_installer_file_name(CE_INSTALLER_SOUNDS), "sounds.map"));
}

/* every truncation, and every byte of the headers changed: only fails */
static void test_damage(void)
{
	struct build_file files[] = {
		{ "maps\\loc.map", loc, 3000, 0 },
		{ "maps\\bitmaps.map", bitmaps, 3000, 1 },
		{ "maps\\sounds.map", sounds, 3000, 1 },
	};
	struct build_buffer cabinet, program;
	unsigned long length, index, wrong = 0, done = 0;
	char error[512];
	int compression;

	put32(loc + 4, 3000);
	put32(loc + 8, 3000);
	put32(bitmaps + 4, 3000);
	put32(bitmaps + 8, 3000);
	put32(sounds + 4, 3000);
	put32(sounds + 8, 3000);
	for (compression = BUILD_STORED; compression <= BUILD_LZX; compression++)
	{
		cabinet = build_cabinet(files, 3, compression, 1);
		program = build_installer(cabinet.data, cabinet.size, 1);
		/* (the cabinet ends 0x290 + its size in: after it, the section's
		padding) */
		for (length = 0; length < 0x290 + cabinet.size; length += length < 0x400 ? 1 : 7)
		{
			struct build_buffer cut = program;
			int result;

			cut.size = length;
			clear_maps();
			result = extract(&cut, CE_INSTALLER_ALL, NULL, error);
			if (result == CE_INSTALLER_DONE)
				done++;
		}
		/* (each byte of the program's headers, the resource tree and the
		cabinet's headers, set to three values) */
		for (index = 0; index < program.size && index < 0x200 + 0x100 + 200; index++)
		{
			static const unsigned char values[] = { 0x00, 0xFF, 0x80 };
			unsigned int value;

			if (index >= 0x160 && index < 0x200)
				continue;
			for (value = 0; value < sizeof(values); value++)
			{
				unsigned char saved = program.data[index];
				int result;

				program.data[index] = values[value] == saved ? (unsigned char)~saved : values[value];
				clear_maps();
				result = extract(&program, CE_INSTALLER_ALL, NULL, error);
				program.data[index] = saved;
				if (result == CE_INSTALLER_DONE && ce_installer_missing(maps))
					wrong++;
			}
		}
		free(program.data);
		free(cabinet.data);
	}
	/* (a truncation never gives the three maps) */
	CHECK(done == 0);
	CHECK(wrong == 0);
	clear_maps();
}

int main(void)
{
	snprintf(folder, sizeof(folder), "%s/ce_installer_test.%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp",
		(int)getpid());
	snprintf(maps, sizeof(maps), "%s/maps", folder);
	snprintf(installer, sizeof(installer), "%s/maps/halocesetup_en_1.00.exe", folder);
	if (mkdir(folder, 0777) != 0 || mkdir(maps, 0777) != 0)
	{
		fprintf(stderr, "cannot make %s\n", maps);
		return 1;
	}
	bitmaps = build_resource_map(1, bitmaps_size, 1);
	sounds = build_resource_map(2, sounds_size, 2);
	loc = build_resource_map(3, loc_size, 3);
	other = build_resource_map(7, 10000, 4);

	test_extracts();
	test_names();
	test_refusals();
	test_find();
	test_damage();

	clear_maps();
	remove(installer);
	rmdir(maps);
	rmdir(folder);
	free(bitmaps);
	free(sounds);
	free(loc);
	free(other);
	if (failures)
	{
		fprintf(stderr, "FAIL %d of %d checks\n", failures, checks);
		return 1;
	}
	printf("PASS %d checks: the Custom Edition installer extraction\n", checks);
	return 0;
}
