/*
SCENE_PACK_TEST.C

The scene packs (port/linux/game/scene_pack.c) alone, as the game builds and
loads them, with no game: a recipe built into a pack from a map, a pack
checked and loaded as the game loads one (its header, CRCs, its tags moved
by tag_relocate.c's walk and checked by tag_validate.c's with no correction
allowed, then given tag indices in a table), and packs damaged on purpose to
see that every one is refused or, if it is still a pack, loads clean again.

Built by ninja linux as build/linux/scene_pack_test, as map_validate is (the
game's objects of the pack, the validator, the tag walk and the port's zlib,
with the few functions of the game they call given here).

usage:
  scene_pack_test build <recipe.txt> <maps folder> <out.pack>
      the pack made, then loaded as the game loads it; its sizes and times
  scene_pack_test check <pack> [<first tag index>]
      loaded, given tag indices (from 5000 by default); its tags listed
      with -v
  scene_pack_test fuzz <pack> <iterations> [<seed>]
      damaged copies (cut short, grown, bytes changed with and without their
      CRCs made right, pointers and counts made wrong, the header's fields),
      each refused or, when still a pack, loaded clean twice; never a crash
      or a hang. Exits 1 on any failure.
*/

#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "zlib_prefixed.h"

/* ---------- scene_pack.h's, as the game's objects have them (-malign-double) */

#define SCENE_PACK_LINK_BASE 0x5C000000UL
#define SCENE_PACK_FILE_BASE 0x70000000UL
enum
{
	SCENE_PACK_HEADER_SIZE = 128,
	SCENE_PACK_MAXIMUM_MAPS = 16,
	SCENE_PACK_MAXIMUM_ROOTS = 16,
	SCENE_PACK_MAXIMUM_STRIP_GROUPS = 32,
	SCENE_PACK_MAXIMUM_STRIP_TAGS = 32,
	SCENE_PACK_NAME_SIZE = 32,
	SCENE_PACK_TAG_PATH_SIZE = 256,
	SCENE_PACK_WHY_SIZE = 160,
};

struct scene_pack_recipe_tag
{
	unsigned long group_tag;
	char path[SCENE_PACK_TAG_PATH_SIZE];
};

struct scene_pack_recipe
{
	char name[SCENE_PACK_NAME_SIZE];
	short map_count;
	short root_count;
	short strip_group_count;
	short strip_tag_count;
	char maps[SCENE_PACK_MAXIMUM_MAPS][SCENE_PACK_NAME_SIZE];
	struct scene_pack_recipe_tag roots[SCENE_PACK_MAXIMUM_ROOTS];
	unsigned long strip_groups[SCENE_PACK_MAXIMUM_STRIP_GROUPS];
	struct scene_pack_recipe_tag strip_tags[SCENE_PACK_MAXIMUM_STRIP_TAGS];
	unsigned long long hash;
};

struct scene_pack_info
{
	unsigned long file_size;
	unsigned long tags_offset;
	unsigned long tags_size;
	unsigned long data_offset;
	unsigned long data_size;
	long tag_count;
	long root_count;
	unsigned long tags_crc;
	unsigned long data_crc;
	unsigned long builder_version;
	unsigned long long recipe_hash;
	unsigned long long source_hash;
	char source_map[SCENE_PACK_NAME_SIZE];
};

struct scene_pack_build_result
{
	char map[SCENE_PACK_NAME_SIZE];
	long tag_count;
	long stripped_count;
	long buffer_count;
	unsigned long tag_bytes;
	unsigned long data_bytes;
	unsigned long file_bytes;
	unsigned long long source_bytes;
	unsigned long long microseconds;
	char why[SCENE_PACK_WHY_SIZE];
};

unsigned char scene_pack_recipe_parse(char const *text, long length, struct scene_pack_recipe *recipe, char *why,
	long why_size);
unsigned char scene_pack_build(struct scene_pack_recipe const *recipe, char const *maps_directory,
	char const *pack_path, void *scratch, unsigned long scratch_size, int volatile const *cancel,
	struct scene_pack_build_result *result);
unsigned char scene_pack_open(char const *path, struct scene_pack_info *info, char *why, long why_size);
unsigned char scene_pack_load(char const *path, struct scene_pack_info const *info, void *destination, char *why,
	long why_size);
void scene_pack_rebase(void *tags, long first_index, long first_salt, long file_delta);
void *scene_pack_instances(void *tags, long *count);
long scene_pack_find(void *tags, unsigned long group_tag, char const *path);

/* ---------- the game's few functions the objects call */

static int quiet;

void tag_validate_report(char const *message)
{
	if (!quiet)
		printf("%s\n", message);
}

void platform_log(const char *format, ...)
{
	va_list arguments;

	if (quiet)
		return;
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

void *halo_custom_edition_memory_alloc(unsigned long bytes)
{
	return calloc(1, bytes ? bytes : 1);
}

void halo_custom_edition_memory_free(void *address)
{
	free(address);
}

int halo_linux_vsnprintf(char *buffer, size_t count, char const *format, va_list arguments)
{
	return vsnprintf(buffer, count, format, arguments);
}

int halo_linux_snprintf(char *buffer, size_t count, char const *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vsnprintf(buffer, count, format, arguments);
	va_end(arguments);
	return result;
}

int halo_linux_sprintf(char *buffer, char const *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vsprintf(buffer, format, arguments);
	va_end(arguments);
	return result;
}

int halo_linux_fprintf(FILE *stream, char const *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vfprintf(stream, format, arguments);
	va_end(arguments);
	return result;
}

FILE *halo_linux_fopen(const char *path, const char *mode)
{
	return fopen(path, mode);
}

int halo_linux_remove(const char *path)
{
	return remove(path);
}

int halo_linux_rename(const char *old_path, const char *new_path)
{
	return rename(old_path, new_path);
}

void *csmemset(void *buffer, long c, unsigned long size)
{
	return memset(buffer, (int)c, size);
}

void *csmemcpy(void *destination, void const *source, unsigned long size)
{
	return memcpy(destination, source, size);
}

unsigned long csstrlen(char const *s)
{
	return strlen(s);
}

char *csstrcat(char *s1, char const *s2)
{
	return strcat(s1, s2);
}

long csstrcmp(char const *s1, char const *s2)
{
	return strcmp(s1, s2);
}

long csmemcmp(void const *p1, void const *p2, unsigned long size)
{
	return memcmp(p1, p2, size);
}

char *csstrcpy(char *destination, char const *source)
{
	return strcpy(destination, source);
}

void *debug_malloc(unsigned int size, unsigned char clear, char const *file, long line)
{
	(void)file;
	(void)line;
	return clear ? calloc(1, size ? size : 1) : malloc(size ? size : 1);
}

void *debug_realloc(void *pointer, unsigned int size, char const *file, long line)
{
	(void)file;
	(void)line;
	return realloc(pointer, size ? size : 1);
}

void debug_free(void *pointer, char const *file, long line)
{
	(void)file;
	(void)line;
	free(pointer);
}

void display_assert(char *information, char *file, long line, unsigned char fatal)
{
	fprintf(stderr, "scene_pack_test: assertion at %s:%ld: %s\n", file ? file : "?", line, information ? information : "");
	if (fatal)
		abort();
}

void system_exit(long code)
{
	exit((int)code);
}

unsigned long long vita_host_time_us(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long long)now.tv_sec * 1000000ULL + (unsigned long long)now.tv_nsec / 1000;
}

int *_errno(void)
{
	static int value;

	return &value;
}

/* ---------- code */

static unsigned long read_u32(unsigned char const *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

static void write_u32(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

static unsigned char *file_read(char const *path, long *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *bytes;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	*size = ftell(file);
	fseek(file, 0, SEEK_SET);
	bytes = malloc(*size ? *size : 1);
	if (bytes && fread(bytes, 1, *size, file) != (size_t)*size)
	{
		free(bytes);
		bytes = NULL;
	}
	fclose(file);
	return bytes;
}

static int file_write(char const *path, unsigned char const *bytes, long size)
{
	FILE *file = fopen(path, "wb");
	int written;

	if (!file)
		return 0;
	written = fwrite(bytes, 1, size, file) == (size_t)size;
	fclose(file);
	return written;
}

/* the pack at path loaded as the game loads one, given tag indices from
first_index: its tags (malloc's), or NULL and why */
static void *pack_load(char const *path, struct scene_pack_info *info, long first_index, char *why, long why_size)
{
	void *tags;

	if (!scene_pack_open(path, info, why, why_size))
		return NULL;
	tags = malloc(info->tags_size);
	if (!tags)
	{
		snprintf(why, why_size, "no memory");
		return NULL;
	}
	if (!scene_pack_load(path, info, tags, why, why_size))
	{
		free(tags);
		return NULL;
	}
	if (first_index >= 0)
		scene_pack_rebase(tags, first_index, 0x3000, 0x04000000);
	return tags;
}

static char *group_text(unsigned long group_tag, char text[5])
{
	text[0] = (char)(group_tag >> 24);
	text[1] = (char)(group_tag >> 16);
	text[2] = (char)(group_tag >> 8);
	text[3] = (char)group_tag;
	text[4] = 0;
	return text;
}

static void pack_list(void *tags, int verbose)
{
	long count, index;
	unsigned char *instances = scene_pack_instances(tags, &count);
	char groups[64][5];
	long group_counts[64];
	int group_count = 0, group;

	for (index = 0; index < count; index++)
	{
		unsigned char *instance = instances + index * 32;
		char text[5];

		group_text(read_u32(instance), text);
		for (group = 0; group < group_count && strcmp(groups[group], text); group++)
			;
		if (group == group_count && group_count < 64)
		{
			strcpy(groups[group_count], text);
			group_counts[group_count++] = 0;
		}
		if (group < group_count)
			group_counts[group]++;
		if (verbose)
			printf("  %08lx %s %s\n", read_u32(instance + 12), text, (char const *)read_u32(instance + 16));
	}
	printf("  groups:");
	for (group = 0; group < group_count; group++)
		printf(" %s %ld", groups[group], group_counts[group]);
	printf("\n");
}

static int command_build(char const *recipe_path, char const *maps, char const *out)
{
	long size;
	unsigned char *text = file_read(recipe_path, &size);
	struct scene_pack_recipe *recipe = calloc(1, sizeof(*recipe));
	struct scene_pack_build_result result;
	struct scene_pack_info info;
	char why[256];
	void *tags;
	unsigned long long started;

	if (!text || !recipe)
	{
		printf("scene_pack_test: %s cannot be read\n", recipe_path);
		return 1;
	}
	if (!scene_pack_recipe_parse((char const *)text, size, recipe, why, sizeof(why)))
	{
		printf("scene_pack_test: recipe refused: %s\n", why);
		return 1;
	}
	printf("recipe %s: %d tags, %d maps, %d groups and %d tags stripped, hash %016llx\n", recipe->name,
		recipe->root_count, recipe->map_count, recipe->strip_group_count, recipe->strip_tag_count, recipe->hash);
	if (!scene_pack_build(recipe, maps, out, NULL, 0, NULL, &result))
	{
		printf("scene_pack_test: build failed: %s\n", result.why);
		return 1;
	}
	printf("built %s from %s: %ld tags (%ld references stripped), %ld buffers, %lu KB of tags, %lu KB of pixels and samples, "
		"%lu KB file; %.1f MB of the map read, %.1f ms\n", out, result.map, result.tag_count, result.stripped_count,
		result.buffer_count, result.tag_bytes / 1024, result.data_bytes / 1024, result.file_bytes / 1024,
		(double)result.source_bytes / (1024.0 * 1024.0), (double)result.microseconds / 1000.0);
	started = vita_host_time_us();
	tags = pack_load(out, &info, 5000, why, sizeof(why));
	if (!tags)
	{
		printf("scene_pack_test: the pack built is refused: %s\n", why);
		return 1;
	}
	printf("loaded: %ld tags (%ld roots), %lu KB, checked and given indices in %.1f ms\n", info.tag_count,
		info.root_count, info.tags_size / 1024, (double)(vita_host_time_us() - started) / 1000.0);
	pack_list(tags, 0);
	free(tags);
	free(text);
	free(recipe);
	return 0;
}

static int command_check(char const *path, long first_index, int verbose)
{
	struct scene_pack_info info;
	char why[256];
	void *tags = pack_load(path, &info, first_index, why, sizeof(why));

	if (!tags)
	{
		printf("%s: refused: %s\n", path, why);
		return 1;
	}
	printf("%s: %ld tags from %s, %lu KB of tags, %lu KB of data\n", path, info.tag_count, info.source_map,
		info.tags_size / 1024, info.data_size / 1024);
	pack_list(tags, verbose);
	free(tags);
	return 0;
}

/* ---------- fuzzing */

static unsigned long long fuzz_state;
static sigjmp_buf fuzz_jump;
static volatile int fuzz_signal_number;

static unsigned long fuzz_random(void)
{
	fuzz_state = fuzz_state * 6364136223846793005ULL + 1442695040888963407ULL;
	return (unsigned long)(fuzz_state >> 33);
}

static void fuzz_signal(int signal_number)
{
	fuzz_signal_number = signal_number;
	siglongjmp(fuzz_jump, 1);
}

enum
{
	_fuzz_truncate,
	_fuzz_grow,
	_fuzz_flip_raw,
	_fuzz_flip_tags,
	_fuzz_word_tags,
	_fuzz_pointer_tags,
	_fuzz_count_tags,
	_fuzz_header,
	_fuzz_flip_data,
	NUMBER_OF_FUZZ_KINDS
};

static char const *const fuzz_kind_names[NUMBER_OF_FUZZ_KINDS] =
{
	"cut short", "grown", "bytes changed (CRCs left)", "tag bytes changed (CRCs made right)",
	"tag words made odd values", "tag pointers made wrong", "tag counts made huge", "header fields changed",
	"data bytes changed (CRCs made right)",
};

/* the header's CRCs made right for bytes (a pack of size bytes) */
static void fuzz_fix_crcs(unsigned char *bytes, long size)
{
	unsigned long tags_offset = read_u32(bytes + 20), tags_size = read_u32(bytes + 24);
	unsigned long data_offset = read_u32(bytes + 28), data_size = read_u32(bytes + 32);

	if (tags_offset <= (unsigned long)size && tags_size <= (unsigned long)size - tags_offset)
		write_u32(bytes + 44, crc32(0, bytes + tags_offset, tags_size));
	if (data_offset <= (unsigned long)size && data_size <= (unsigned long)size - data_offset)
		write_u32(bytes + 48, crc32(0, bytes + data_offset, data_size));
	write_u32(bytes + 124, 0);
	write_u32(bytes + 124, crc32(0, bytes, 128));
}

static unsigned long fuzz_odd_value(unsigned long tags_size)
{
	static unsigned long const values[] =
	{
		0, 1, 0xFFFFFFFFUL, 0x7FFFFFFFUL, 0x80000000UL, 0x10000UL, 0xFFFFUL, 0x8000UL, 0x803A6000UL,
		SCENE_PACK_LINK_BASE - 4, SCENE_PACK_LINK_BASE, SCENE_PACK_FILE_BASE, SCENE_PACK_FILE_BASE - 1,
	};
	unsigned long pick = fuzz_random() % (sizeof(values) / sizeof(values[0]) + 3);

	if (pick == sizeof(values) / sizeof(values[0]))
		return SCENE_PACK_LINK_BASE + tags_size - (fuzz_random() % 8);
	if (pick == sizeof(values) / sizeof(values[0]) + 1)
		return SCENE_PACK_LINK_BASE + (fuzz_random() % (tags_size + 64));
	if (pick == sizeof(values) / sizeof(values[0]) + 2)
		return fuzz_random() ^ (fuzz_random() << 16);
	return values[pick];
}

static int command_fuzz(char const *path, long iterations, unsigned long seed)
{
	long size;
	unsigned char *original = file_read(path, &size);
	unsigned char *bytes;
	char temporary[512];
	long refused[NUMBER_OF_FUZZ_KINDS], accepted[NUMBER_OF_FUZZ_KINDS];
	long iteration, failures = 0;
	unsigned long tags_offset, tags_size, data_offset, data_size;
	struct sigaction action;
	int kind;

	if (!original || size < 128)
	{
		printf("scene_pack_test: %s cannot be read\n", path);
		return 1;
	}
	tags_offset = read_u32(original + 20);
	tags_size = read_u32(original + 24);
	data_offset = read_u32(original + 28);
	data_size = read_u32(original + 32);
	bytes = malloc(size + 4096);
	snprintf(temporary, sizeof(temporary), "%s/scene_pack_fuzz.%d.pack", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp",
		(int)getpid());
	memset(refused, 0, sizeof(refused));
	memset(accepted, 0, sizeof(accepted));
	memset(&action, 0, sizeof(action));
	action.sa_handler = fuzz_signal;
	sigaction(SIGSEGV, &action, NULL);
	sigaction(SIGBUS, &action, NULL);
	sigaction(SIGFPE, &action, NULL);
	sigaction(SIGALRM, &action, NULL);
	sigaction(SIGABRT, &action, NULL);
	quiet = 1;
	for (iteration = 0; iteration < iterations; iteration++)
	{
		long mutated_size = size;
		struct scene_pack_info info;
		char why[256];
		void *tags;
		int changes, change;

		fuzz_state = (unsigned long long)seed * 1000003ULL + (unsigned long long)iteration;
		fuzz_random();
		kind = (int)(fuzz_random() % NUMBER_OF_FUZZ_KINDS);
		memcpy(bytes, original, size);
		changes = 1 + (int)(fuzz_random() % 4);
		switch (kind)
		{
		case _fuzz_truncate:
			mutated_size = (long)(fuzz_random() % (unsigned long)size);
			break;
		case _fuzz_grow:
			mutated_size = size + 1 + (long)(fuzz_random() % 4000);
			for (change = (int)size; change < mutated_size; change++)
				bytes[change] = (unsigned char)fuzz_random();
			break;
		case _fuzz_flip_raw:
			for (change = 0; change < changes; change++)
				bytes[fuzz_random() % (unsigned long)size] ^= (unsigned char)(1 + fuzz_random() % 255);
			break;
		case _fuzz_flip_tags:
			for (change = 0; change < changes; change++)
				bytes[tags_offset + fuzz_random() % tags_size] ^= (unsigned char)(1 + fuzz_random() % 255);
			fuzz_fix_crcs(bytes, mutated_size);
			break;
		case _fuzz_word_tags:
			for (change = 0; change < changes; change++)
				write_u32(bytes + tags_offset + (fuzz_random() % (tags_size / 4)) * 4, fuzz_odd_value(tags_size));
			fuzz_fix_crcs(bytes, mutated_size);
			break;
		case _fuzz_pointer_tags:
		case _fuzz_count_tags:
		{
			/* (a word that is a pointer of the pack's, or the count before
			one: made to point or count past it) */
			long tries;

			for (change = 0; change < changes; change++)
			{
				for (tries = 0; tries < 10000; tries++)
				{
					unsigned long at = tags_offset + (fuzz_random() % (tags_size / 4)) * 4;
					unsigned long value = read_u32(bytes + at);

					if (value >= SCENE_PACK_LINK_BASE && value < SCENE_PACK_LINK_BASE + tags_size)
					{
						if (kind == _fuzz_pointer_tags)
							write_u32(bytes + at, fuzz_odd_value(tags_size));
						else if (at >= tags_offset + 4)
							write_u32(bytes + at - 4, fuzz_random() % 2 ? 0x7FFFFFFFUL :
								read_u32(bytes + at - 4) + 1 + fuzz_random() % 100000);
						break;
					}
				}
			}
			fuzz_fix_crcs(bytes, mutated_size);
			break;
		}
		case _fuzz_header:
			for (change = 0; change < changes; change++)
				write_u32(bytes + 8 + (fuzz_random() % 29) * 4, fuzz_odd_value(tags_size));
			fuzz_fix_crcs(bytes, mutated_size);
			break;
		case _fuzz_flip_data:
			if (data_size)
			{
				for (change = 0; change < changes; change++)
					bytes[data_offset + fuzz_random() % data_size] ^= (unsigned char)(1 + fuzz_random() % 255);
			}
			fuzz_fix_crcs(bytes, mutated_size);
			break;
		}
		if (!file_write(temporary, bytes, mutated_size))
		{
			printf("scene_pack_test: %s cannot be written\n", temporary);
			return 1;
		}
		fuzz_signal_number = 0;
		if (sigsetjmp(fuzz_jump, 1))
		{
			alarm(0);
			printf("FAIL: iteration %ld (seed %lu, %s): signal %d\n", iteration, seed, fuzz_kind_names[kind],
				fuzz_signal_number);
			failures++;
			sigaction(SIGSEGV, &action, NULL);
			continue;
		}
		alarm(20);
		tags = pack_load(temporary, &info, 5000, why, sizeof(why));
		if (tags)
		{
			void *again;
			struct scene_pack_info info_again;

			accepted[kind]++;
			/* (once accepted, it must be again: nothing left to correct) */
			free(tags);
			again = pack_load(temporary, &info_again, 5000, why, sizeof(why));
			if (!again)
			{
				printf("FAIL: iteration %ld (seed %lu, %s): accepted, then refused: %s\n", iteration, seed,
					fuzz_kind_names[kind], why);
				failures++;
			}
			else
			{
				free(again);
			}
			if (kind == _fuzz_truncate || kind == _fuzz_grow || kind == _fuzz_flip_raw)
			{
				printf("FAIL: iteration %ld (seed %lu, %s): accepted\n", iteration, seed, fuzz_kind_names[kind]);
				failures++;
			}
		}
		else
		{
			refused[kind]++;
		}
		alarm(0);
	}
	quiet = 0;
	remove(temporary);
	printf("fuzz %s: %ld iterations from seed %lu\n", path, iterations, seed);
	for (kind = 0; kind < NUMBER_OF_FUZZ_KINDS; kind++)
		printf("  %-40s %6ld refused %6ld accepted\n", fuzz_kind_names[kind], refused[kind], accepted[kind]);
	printf("%s: %ld failures\n", failures ? "FAIL" : "PASS", failures);
	free(original);
	free(bytes);
	return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc == 5 && !strcmp(argv[1], "build"))
		return command_build(argv[2], argv[3], argv[4]);
	if (argc >= 3 && !strcmp(argv[1], "check"))
		return command_check(argv[2], argc >= 4 && strcmp(argv[3], "-v") ? atol(argv[3]) : 5000,
			argc >= 4 && !strcmp(argv[argc - 1], "-v"));
	if ((argc == 4 || argc == 5) && !strcmp(argv[1], "fuzz"))
		return command_fuzz(argv[2], atol(argv[3]), argc == 5 ? strtoul(argv[4], NULL, 10) : 1);
	fprintf(stderr, "usage: scene_pack_test build <recipe> <maps folder> <out.pack> | check <pack> [first] [-v] | "
		"fuzz <pack> <iterations> [seed]\n");
	return 2;
}
