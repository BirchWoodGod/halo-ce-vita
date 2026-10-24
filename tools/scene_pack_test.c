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
  scene_pack_test cefuzz <recipe> <maps folder> <iterations> [<seed>]
      a recipe of a Custom Edition map (scene_pack_ce.c) built from a
      damaged map, again and again: the map's bytes changed as the builder
      reads them (words of the roots of the pack's tags, of its tag index,
      bytes of the model data, pixels and samples a clean build read, of the
      tag data and of the header), never on disk; each build refused or, when
      it makes a pack, the pack refused or loaded clean twice; never a crash
      or a hang. Exits 1 on any failure.
  scene_pack_test buildmany <maps folder> <map> <out folder> <recipe.txt>...
      the recipes' packs made in one read of that map (each recipe must
      name it: scene_pack_build_many), each then loaded as the game loads
      it. SCENE_PACK_COPY_DIR=<folder>: the decompressed copies a build may
      read instead of inflating (<folder>/<map>.map, as the game's cache
      partition holds them: scene_pack_copy_source)
  scene_pack_test blocksfuzz <maps folder> <map> <iterations> [<seed>]
      Forge's building blocks (a recipe with "blocks": scene_pack_blocks.c)
      built from a damaged map, again and again: the map's tag data changed
      as the blocks read it (the instances, roots and first bitmaps of its
      shaders and bitmaps, any word), never on disk; each build refused or,
      when it makes a pack, the pack refused or loaded clean twice; never a
      crash or a hang. Exits 1 on any failure.
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
	long volatile progress;
	unsigned long long microseconds;
	char why[SCENE_PACK_WHY_SIZE];
};

unsigned char scene_pack_recipe_parse(char const *text, long length, struct scene_pack_recipe *recipe, char *why,
	long why_size);
unsigned long scene_pack_recipe_size(void);
char const *scene_pack_recipe_custom_edition_map(struct scene_pack_recipe const *recipe, short index);
unsigned char scene_pack_build(struct scene_pack_recipe const *recipe, char const *maps_directory,
	char const *pack_path, void *scratch, unsigned long scratch_size, int volatile const *cancel,
	struct scene_pack_build_result *result);
unsigned char scene_pack_build_many(struct scene_pack_recipe const *const *recipes, short count, char const *map,
	char const *maps_directory, char const *const *pack_paths, void *scratch, unsigned long scratch_size,
	int volatile const *cancel, struct scene_pack_build_result *results);
struct scene_pack_copy_source
{
	FILE *(*open)(char const *map, unsigned long *token);
	unsigned char (*unchanged)(unsigned long token);
};
extern struct scene_pack_copy_source const *scene_pack_copy_source;
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
	/* (the game's recipe, of which this knows the fields before the hash) */
	struct scene_pack_recipe *recipe = calloc(1, scene_pack_recipe_size());
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

/* ---------- damaged Custom Edition sources */

#include "../port/linux/game/cache_file_formats.h"

extern void (*scene_pack_ce_read_hook)(unsigned long offset, void *buffer, unsigned long size);

enum
{
	MAXIMUM_SOURCE_MUTATIONS = 4,
	MAXIMUM_SOURCE_READS = 4096,
};

/* the source map's bytes changed as the builder reads them (each at a file
offset), and what a clean build read of it */
static struct
{
	int count;
	unsigned long offsets[MAXIMUM_SOURCE_MUTATIONS];
	unsigned long values[MAXIMUM_SOURCE_MUTATIONS];
	int widths[MAXIMUM_SOURCE_MUTATIONS];
	/* (the clean build's reads: recording) */
	int recording;
	long read_count;
	unsigned long read_offsets[MAXIMUM_SOURCE_READS];
	unsigned long read_sizes[MAXIMUM_SOURCE_READS];
} source_damage;

static void source_damage_hook(unsigned long offset, void *buffer, unsigned long size)
{
	int index;

	if (source_damage.recording && source_damage.read_count < MAXIMUM_SOURCE_READS)
	{
		source_damage.read_offsets[source_damage.read_count] = offset;
		source_damage.read_sizes[source_damage.read_count] = size;
		source_damage.read_count++;
	}
	for (index = 0; index < source_damage.count; index++)
	{
		unsigned long at = source_damage.offsets[index];
		int byte;

		for (byte = 0; byte < source_damage.widths[index]; byte++)
		{
			if (at + byte >= offset && at + byte - offset < size)
				((unsigned char *)buffer)[at + byte - offset] = (unsigned char)(source_damage.values[index] >> (8 * byte));
		}
	}
}

struct source_file
{
	FILE *file;
	uint32_t size;
};

static int source_file_read(void *context, uint32_t offset, uint32_t size, void *buffer)
{
	struct source_file *source = context;

	return offset <= source->size && size <= source->size - offset && !fseek(source->file, (long)offset, SEEK_SET) &&
		fread(buffer, 1, size, source->file) == size;
}

/* the file offsets of the roots of the tags of a pack's (by name) in the
source map's tag data: how many */
static long source_roots(char const *map_path, void *tags, unsigned long *roots, long most,
	struct cache_file_identity *identity)
{
	struct source_file source;
	struct cache_file_source file_source;
	unsigned char header[0x28];
	unsigned char *instances, *names;
	long count, index, found = 0, pack_count;
	unsigned char *pack_instances = scene_pack_instances(tags, &pack_count);

	source.file = fopen(map_path, "rb");
	if (!source.file)
		return 0;
	fseek(source.file, 0, SEEK_END);
	source.size = (uint32_t)ftell(source.file);
	file_source.context = &source;
	file_source.read = source_file_read;
	file_source.size = source.size;
	if (cache_file_identify(&file_source, identity) != _cache_file_status_ok ||
		!source_file_read(&source, identity->tag_data_offset, sizeof(header), header) ||
		(count = (long)read_u32(header + 12)) <= 0 || count > 0x8000)
	{
		fclose(source.file);
		return 0;
	}
	instances = malloc((unsigned long)count * 32);
	names = malloc(identity->tag_data_size);
	if (!instances || !names || !source_file_read(&source, identity->tag_data_offset, identity->tag_data_size, names))
	{
		fclose(source.file);
		free(instances);
		free(names);
		return 0;
	}
	memcpy(instances, names + (read_u32(header) - CUSTOM_EDITION_TAG_CACHE_ADDRESS), (unsigned long)count * 32);
	for (index = 0; index < count && found < most; index++)
	{
		unsigned long name_offset = read_u32(instances + index * 32 + 16) - CUSTOM_EDITION_TAG_CACHE_ADDRESS;
		unsigned long root = read_u32(instances + index * 32 + 20);
		long pack;

		if (name_offset >= identity->tag_data_size || !root || read_u32(instances + index * 32 + 24))
			continue;
		for (pack = 0; pack < pack_count; pack++)
		{
			char const *pack_name = (char const *)read_u32(pack_instances + pack * 32 + 16);
			char const *space = strrchr(pack_name, ' ');
			/* (a protected map's tag is named by its index in the pack:
			protected\<map>\<group> <index>, scene_pack_ce.c) */
			int same = !strncmp(pack_name, "protected\\", 10) && space ? atol(space + 1) == index :
				!strcmp(pack_name, (char const *)names + name_offset);

			if (read_u32(pack_instances + pack * 32) == read_u32(instances + index * 32) && same)
			{
				roots[found++] = identity->tag_data_offset + (root - CUSTOM_EDITION_TAG_CACHE_ADDRESS);
				break;
			}
		}
	}
	fclose(source.file);
	free(instances);
	free(names);
	return found;
}

static int command_cefuzz(char const *recipe_path, char const *maps, long iterations, unsigned long seed)
{
	long size;
	unsigned char *text = file_read(recipe_path, &size);
	struct scene_pack_recipe *recipe = calloc(1, scene_pack_recipe_size());
	struct scene_pack_build_result *result = calloc(1, sizeof(*result) + 64);
	struct scene_pack_info info;
	struct cache_file_identity identity;
	char why[256], pack_path[512], map_path[512];
	char const *map_name;
	unsigned long *roots;
	long root_count, iteration, failures = 0;
	long built = 0, refused_build = 0, loaded = 0, refused_load = 0;
	long kinds[5] = { 0 };
	void *tags;
	struct sigaction action;

	if (!text || !recipe || !result || !scene_pack_recipe_parse((char const *)text, size, recipe, why, sizeof(why)))
	{
		printf("scene_pack_test: the recipe cannot be read\n");
		return 1;
	}
	map_name = scene_pack_recipe_custom_edition_map(recipe, 0);
	if (!map_name)
	{
		printf("scene_pack_test: the recipe names no Custom Edition map\n");
		return 1;
	}
	snprintf(map_path, sizeof(map_path), "%s/%s.map", maps, map_name);
	snprintf(pack_path, sizeof(pack_path), "%s/scene_pack_cefuzz.%d.pack", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp",
		(int)getpid());
	/* a clean build first: what it reads, the pack's tags' roots in the map */
	scene_pack_ce_read_hook = source_damage_hook;
	source_damage.recording = 1;
	if (!scene_pack_build(recipe, maps, pack_path, NULL, 0, NULL, result))
	{
		printf("scene_pack_test: the clean build failed: %s\n", result->why);
		return 1;
	}
	source_damage.recording = 0;
	tags = pack_load(pack_path, &info, -1, why, sizeof(why));
	if (!tags)
	{
		printf("scene_pack_test: the clean pack is refused: %s\n", why);
		return 1;
	}
	roots = malloc(sizeof(unsigned long) * 4096);
	root_count = roots ? source_roots(map_path, tags, roots, 4096, &identity) : 0;
	free(tags);
	printf("cefuzz %s from %s: %ld of the pack's tags found in the map, %ld reads of the map (model data, pixels, samples)\n",
		recipe->name, map_name, root_count, source_damage.read_count);
	if (!root_count)
		return 1;
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
		int change;

		fuzz_state = (unsigned long long)seed * 1000003ULL + (unsigned long long)iteration;
		fuzz_random();
		source_damage.count = 1 + (int)(fuzz_random() % MAXIMUM_SOURCE_MUTATIONS);
		for (change = 0; change < source_damage.count; change++)
		{
			unsigned long pick = fuzz_random() % 100;
			int kind;

			/* (a word of one of the pack's tags' roots; of the tag index and
			instances; of what a clean build read past the tag data: model
			data, pixels, samples; a byte of the tag data; of the header) */
			if (pick < 50)
			{
				kind = 0;
				source_damage.offsets[change] = roots[fuzz_random() % (unsigned long)root_count] + (fuzz_random() % 0x200) / 4 * 4;
			}
			else if (pick < 62)
			{
				kind = 1;
				source_damage.offsets[change] = identity.tag_data_offset + (fuzz_random() % (0x28 + 3620 * 32)) / 4 * 4;
			}
			else if (pick < 82 && source_damage.read_count)
			{
				long read = (long)(fuzz_random() % (unsigned long)source_damage.read_count);

				kind = 2;
				source_damage.offsets[change] = source_damage.read_offsets[read] +
					(source_damage.read_sizes[read] ? fuzz_random() % source_damage.read_sizes[read] : 0);
			}
			else if (pick < 96)
			{
				kind = 3;
				source_damage.offsets[change] = identity.tag_data_offset + fuzz_random() % identity.tag_data_size;
			}
			else
			{
				kind = 4;
				source_damage.offsets[change] = fuzz_random() % 0x800;
			}
			kinds[kind]++;
			source_damage.widths[change] = kind == 2 || kind == 3 || kind == 4 ? 1 + (int)(fuzz_random() % 2) * 3 : 4;
			source_damage.values[change] = fuzz_random() % 3 ? fuzz_odd_value(0x100000) : fuzz_random();
		}
		fuzz_signal_number = 0;
		if (sigsetjmp(fuzz_jump, 1))
		{
			alarm(0);
			printf("FAIL: iteration %ld (seed %lu): signal %d\n", iteration, seed, fuzz_signal_number);
			failures++;
			sigaction(SIGSEGV, &action, NULL);
			continue;
		}
		alarm(60);
		if (scene_pack_build(recipe, maps, pack_path, NULL, 0, NULL, result))
		{
			built++;
			tags = pack_load(pack_path, &info, 5000, why, sizeof(why));
			if (tags)
			{
				void *again;

				loaded++;
				free(tags);
				again = pack_load(pack_path, &info, 5000, why, sizeof(why));
				if (!again)
				{
					printf("FAIL: iteration %ld (seed %lu): accepted, then refused: %s\n", iteration, seed, why);
					failures++;
				}
				else
				{
					free(again);
				}
			}
			else
			{
				refused_load++;
			}
		}
		else
		{
			refused_build++;
		}
		alarm(0);
	}
	quiet = 0;
	source_damage.count = 0;
	scene_pack_ce_read_hook = NULL;
	remove(pack_path);
	printf("cefuzz %s: %ld iterations from seed %lu (changes: %ld in its tags' roots, %ld in the tag index, %ld in its model "
		"data, pixels and samples, %ld elsewhere in the tag data, %ld in the header)\n", recipe->name, iterations, seed,
		kinds[0], kinds[1], kinds[2], kinds[3], kinds[4]);
	printf("  the build refused %ld; built %ld: %ld loaded clean twice, %ld refused when loaded\n", refused_build, built,
		loaded, refused_load);
	printf("%s: %ld failures\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}

/* ---------- damaged sources of Forge's blocks (scene_pack_blocks.c) */

extern void (*scene_pack_blocks_source_hook)(unsigned char *tags, unsigned long size, unsigned long address,
	unsigned long instances, long count);

enum
{
	MAXIMUM_BLOCKS_MUTATIONS = 6,
};

static struct
{
	int count;
	/* (what each change hits: 0 an instance's field, 1 a shader's or
	bitmap's root, 2 a shader's bitmap's group or first bitmap, 3 any word of
	the tag data) */
	int kinds[MAXIMUM_BLOCKS_MUTATIONS];
	unsigned long picks[MAXIMUM_BLOCKS_MUTATIONS];
	unsigned long values[MAXIMUM_BLOCKS_MUTATIONS];
	long applied;
} blocks_damage;

static unsigned long tags_u32(unsigned char const *tags, unsigned long size, unsigned long offset)
{
	return size >= 4 && offset <= size - 4 ? read_u32(tags + offset) : 0;
}

/* the source's tag data changed where the blocks read it: an instance of a
shader or bitmap (its group, tag index, name or root), a word of a shader's
or bitmap's root, of a bitmap's first bitmap, or anywhere */
static void blocks_damage_hook(unsigned char *tags, unsigned long size, unsigned long address, unsigned long instances,
	long count)
{
	int change;

	for (change = 0; change < blocks_damage.count; change++)
	{
		unsigned long pick = blocks_damage.picks[change];
		unsigned long at = 0xFFFFFFFFUL;
		long candidates = 0, index;
		long chosen = -1;

		/* (the n-th shader or bitmap of the source, n from pick) */
		for (index = 0; index < count && instances - address + (unsigned long)(index + 1) * 32 <= size; index++)
		{
			unsigned long group = tags_u32(tags, size, instances - address + (unsigned long)index * 32);

			if (group == 'senv' || (!(pick & 3) && (group == 'soso' || group == 'bitm')))
				candidates++;
		}
		if (candidates)
		{
			long wanted = (long)(pick % (unsigned long)candidates);

			for (index = 0; index < count && instances - address + (unsigned long)(index + 1) * 32 <= size; index++)
			{
				unsigned long group = tags_u32(tags, size, instances - address + (unsigned long)index * 32);

				if ((group == 'senv' || (!(pick & 3) && (group == 'soso' || group == 'bitm'))) && !wanted--)
				{
					chosen = index;
					break;
				}
			}
		}
		if (chosen != -1)
		{
			unsigned long instance = instances - address + (unsigned long)chosen * 32;
			unsigned long root = tags_u32(tags, size, instance + 20) - address;

			switch (blocks_damage.kinds[change])
			{
			case 0:
				at = instance + ((pick >> 8) % 6) * 4;
				break;
			case 1:
				at = root + ((pick >> 8) % (0x344 / 4)) * 4;
				break;
			case 2:
				/* (an environment shader's base or detail map's bitmap group:
				its bitmaps block, at 0x60, and its first bitmap's words) */
				{
					static unsigned long const maps[] = { 0x88, 0xB8, 0xCC, 0xFC };
					unsigned long reference = tags_u32(tags, size, root + maps[(pick >> 4) % 4] + 12);
					long bitmap = (long)(reference & 0xFFFF);
					unsigned long bitmap_root = bitmap < count ?
						tags_u32(tags, size, instances - address + (unsigned long)bitmap * 32 + 20) - address : 0xFFFFFFFFUL;

					if ((pick >> 6) & 1)
						at = bitmap_root + ((pick >> 8) % 0x1C) * 4;
					else
						at = tags_u32(tags, size, bitmap_root + 0x60 + 4) - address + ((pick >> 8) % 12) * 4;
				}
				break;
			}
		}
		if (blocks_damage.kinds[change] == 3)
			at = ((pick >> 8) % (size / 4)) * 4;
		if (size >= 4 && at <= size - 4)
		{
			write_u32(tags + at, blocks_damage.values[change]);
			blocks_damage.applied++;
		}
	}
}

static int command_blocksfuzz(char const *maps, char const *map, long iterations, unsigned long seed)
{
	struct scene_pack_recipe *recipe = calloc(1, scene_pack_recipe_size());
	struct scene_pack_build_result *result = calloc(1, sizeof(*result) + 64);
	struct scene_pack_info info;
	char text[256], why[256], pack_path[512];
	long iteration, failures = 0;
	long built = 0, refused_build = 0, loaded = 0, refused_load = 0;
	long kinds[4] = { 0 };
	void *tags;
	struct sigaction action;

	snprintf(text, sizeof(text), "pack blocks_%s\nmap %s\nblocks\n", map, map);
	if (!recipe || !result || !scene_pack_recipe_parse(text, (long)strlen(text), recipe, why, sizeof(why)))
	{
		printf("scene_pack_test: the blocks' recipe cannot be made: %s\n", why);
		return 1;
	}
	snprintf(pack_path, sizeof(pack_path), "%s/scene_pack_blocksfuzz.%d.pack", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp",
		(int)getpid());
	if (!scene_pack_build(recipe, maps, pack_path, NULL, 0, NULL, result) ||
		!(tags = pack_load(pack_path, &info, 5000, why, sizeof(why))))
	{
		printf("scene_pack_test: the clean build failed: %s\n", result->why);
		return 1;
	}
	free(tags);
	memset(&action, 0, sizeof(action));
	action.sa_handler = fuzz_signal;
	sigaction(SIGSEGV, &action, NULL);
	sigaction(SIGBUS, &action, NULL);
	sigaction(SIGFPE, &action, NULL);
	sigaction(SIGALRM, &action, NULL);
	sigaction(SIGABRT, &action, NULL);
	scene_pack_blocks_source_hook = blocks_damage_hook;
	quiet = 1;
	for (iteration = 0; iteration < iterations; iteration++)
	{
		int change;

		fuzz_state = (unsigned long long)seed * 1000003ULL + (unsigned long long)iteration;
		fuzz_random();
		blocks_damage.count = 1 + (int)(fuzz_random() % MAXIMUM_BLOCKS_MUTATIONS);
		for (change = 0; change < blocks_damage.count; change++)
		{
			unsigned long pick = fuzz_random() % 100;

			blocks_damage.kinds[change] = pick < 30 ? 0 : pick < 75 ? 1 : pick < 90 ? 2 : 3;
			kinds[blocks_damage.kinds[change]]++;
			blocks_damage.picks[change] = fuzz_random();
			blocks_damage.values[change] = fuzz_random() % 3 ? fuzz_odd_value(0x100000) : fuzz_random();
		}
		fuzz_signal_number = 0;
		if (sigsetjmp(fuzz_jump, 1))
		{
			alarm(0);
			printf("FAIL: iteration %ld (seed %lu): signal %d\n", iteration, seed, fuzz_signal_number);
			failures++;
			sigaction(SIGSEGV, &action, NULL);
			continue;
		}
		alarm(60);
		if (scene_pack_build(recipe, maps, pack_path, NULL, 0, NULL, result))
		{
			built++;
			tags = pack_load(pack_path, &info, 5000, why, sizeof(why));
			if (tags)
			{
				void *again;

				loaded++;
				free(tags);
				again = pack_load(pack_path, &info, 5000, why, sizeof(why));
				if (!again)
				{
					printf("FAIL: iteration %ld (seed %lu): accepted, then refused: %s\n", iteration, seed, why);
					failures++;
				}
				else
				{
					free(again);
				}
			}
			else
			{
				refused_load++;
			}
		}
		else
		{
			refused_build++;
		}
		alarm(0);
	}
	quiet = 0;
	scene_pack_blocks_source_hook = NULL;
	remove(pack_path);
	printf("blocksfuzz %s: %ld iterations from seed %lu (%ld changes made: %ld in shaders' and bitmaps' instances, %ld in "
		"their roots, %ld in bitmaps' first bitmap, %ld anywhere in the tag data)\n", map, iterations, seed,
		blocks_damage.applied, kinds[0], kinds[1], kinds[2], kinds[3]);
	printf("  the build refused %ld; built %ld: %ld loaded clean twice, %ld refused when loaded\n", refused_build, built,
		loaded, refused_load);
	printf("%s: %ld failures\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}

/* (SCENE_PACK_COPY_DIR) the maps' decompressed copies */
static FILE *copy_dir_open(char const *map, unsigned long *token)
{
	char path[1024];

	snprintf(path, sizeof(path), "%s/%s.map", getenv("SCENE_PACK_COPY_DIR"), map);
	*token = 1;
	return fopen(path, "rb");
}

static struct scene_pack_copy_source const copy_dir_source = { copy_dir_open, NULL };

static int command_build_many(char const *maps, char const *map, char const *out_folder, int recipe_count,
	char **recipe_paths)
{
	struct scene_pack_recipe *recipes[16];
	struct scene_pack_build_result results[16];
	char paths[16][512];
	char const *path_pointers[16];
	int index, failures = 0;
	unsigned char built;

	if (recipe_count < 1 || recipe_count > 16)
		return 2;
	for (index = 0; index < recipe_count; index++)
	{
		long size;
		unsigned char *text = file_read(recipe_paths[index], &size);
		char why[256];

		recipes[index] = calloc(1, scene_pack_recipe_size());
		if (!text || !recipes[index] || !scene_pack_recipe_parse((char const *)text, size, recipes[index], why, sizeof(why)))
		{
			printf("scene_pack_test: %s refused\n", recipe_paths[index]);
			return 1;
		}
		free(text);
		snprintf(paths[index], sizeof(paths[index]), "%s/%s.pack", out_folder, recipes[index]->name);
		path_pointers[index] = paths[index];
	}
	if (getenv("SCENE_PACK_COPY_DIR"))
		scene_pack_copy_source = &copy_dir_source;
	built = scene_pack_build_many((struct scene_pack_recipe const *const *)recipes, (short)recipe_count, map, maps,
		path_pointers, NULL, 0, NULL, results);
	for (index = 0; index < recipe_count; index++)
	{
		struct scene_pack_info info;
		char why[256];
		void *tags;

		if (results[index].why[0])
		{
			printf("scene_pack_test: %s not built: %s\n", recipes[index]->name, results[index].why);
			failures++;
			continue;
		}
		printf("built %s from %s: %ld tags, %lu KB of tags, %lu KB of pixels and samples, %lu KB file; %.1f MB of the map "
			"read, %.1f ms (all %d)\n", paths[index], results[index].map, results[index].tag_count,
			results[index].tag_bytes / 1024, results[index].data_bytes / 1024, results[index].file_bytes / 1024,
			(double)results[index].source_bytes / (1024.0 * 1024.0), (double)results[index].microseconds / 1000.0,
			recipe_count);
		tags = pack_load(paths[index], &info, 5000, why, sizeof(why));
		if (!tags)
		{
			printf("scene_pack_test: %s refused: %s\n", paths[index], why);
			failures++;
			continue;
		}
		printf("loaded %s: %ld tags\n", recipes[index]->name, info.tag_count);
		free(tags);
	}
	return built && !failures ? 0 : 1;
}

void tag_validate_pack_allow_structure(int allowed);

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	/* SCENE_PACK_TEST_BACKDROP=1: the packs may hold a structure bsp and a
	scenario, as the menus' backdrop's (menu_backdrop.txt) */
	if (getenv("SCENE_PACK_TEST_BACKDROP"))
		tag_validate_pack_allow_structure(1);
	if (argc == 5 && !strcmp(argv[1], "build"))
		return command_build(argv[2], argv[3], argv[4]);
	if (argc >= 6 && !strcmp(argv[1], "buildmany"))
		return command_build_many(argv[2], argv[3], argv[4], argc - 5, argv + 5);
	if (getenv("SCENE_PACK_COPY_DIR"))
		scene_pack_copy_source = &copy_dir_source;
	if (argc >= 3 && !strcmp(argv[1], "check"))
		return command_check(argv[2], argc >= 4 && strcmp(argv[3], "-v") ? atol(argv[3]) : 5000,
			argc >= 4 && !strcmp(argv[argc - 1], "-v"));
	if ((argc == 4 || argc == 5) && !strcmp(argv[1], "fuzz"))
		return command_fuzz(argv[2], atol(argv[3]), argc == 5 ? strtoul(argv[4], NULL, 10) : 1);
	if ((argc == 5 || argc == 6) && !strcmp(argv[1], "cefuzz"))
		return command_cefuzz(argv[2], argv[3], atol(argv[4]), argc == 6 ? strtoul(argv[5], NULL, 10) : 1);
	if ((argc == 5 || argc == 6) && !strcmp(argv[1], "blocksfuzz"))
		return command_blocksfuzz(argv[2], argv[3], atol(argv[4]), argc == 6 ? strtoul(argv[5], NULL, 10) : 1);
	fprintf(stderr, "usage: scene_pack_test build <recipe> <maps folder> <out.pack> | check <pack> [first] [-v] | "
		"fuzz <pack> <iterations> [seed] | cefuzz <recipe> <maps folder> <iterations> [seed] | "
		"blocksfuzz <maps folder> <map> <iterations> [seed]\n");
	return 2;
}
