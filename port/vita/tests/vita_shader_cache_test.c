/*
Desktop test of port/vita/host/vita_shader_cache.c: the checks a compiled
shader program passes before the Vita hands it to the GPU (issue #28: after
an update, maps drew black until the shader cache folder was deleted).
run_vita_shader_cache_test.sh builds and runs it.

  vita_shader_cache_test                     the checks
  vita_shader_cache_test write-cache <dir>   cache files and their Cg, for tools/vita_shader_pack.py
  vita_shader_cache_test check-pack <pak> <dir> [compile id]   that pack, against those files
  vita_shader_cache_test shipped <pak>       the VPK's pack: this build's settings, every program whole
  vita_shader_cache_test reject <pak>        a pack this build must not use (another build's)
  vita_shader_cache_test sweep <dir>         another build's cache moved aside and removed later, in <dir>
*/
#include "vita_shader_cache.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures, checks;

#define CHECK(condition, ...) do { checks++; if (!(condition)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

/* vita_gxm.c's settings text for its shader_compile_settings (SCE_SHACCCG_PROFILE_VP 0, _FP 1, _ENGLISH 0) */
#define BUILD_SETTINGS "shacc profiles 0/1 optimization 3 warnings 1 locale 0 file halo.cg entry main"

/* the rule the cache used before this change (vita_gxm.c cache_read up to
v1.0.3-beta.6 and next-1.1 83206427): a file is a program if
sceGxmProgramCheck passes (the magic) and the size field is the file's */
static int legacy_accepts(const unsigned char *file, size_t size)
{
	return vshc_gxp_whole(file, size);
}

/* a stand-in GXP program: the magic, a version, the size, then bytes */
static unsigned char *fake_program(uint32_t size, unsigned seed)
{
	unsigned char *program = malloc(size);
	uint32_t index;

	for (index = 0; index < size; index++)
		program[index] = (unsigned char)(seed * 131 + index * 7);
	memcpy(program, "GXP\0", 4);
	program[4] = 1;
	program[5] = 5;
	memcpy(program + 8, &size, 4);
	return program;
}

/* a whole cache file: header and program */
static unsigned char *cache_file(const struct vshc_ids *ids, uint64_t hash, const unsigned char *program, uint32_t size)
{
	unsigned char *file = malloc(VSHC_HEADER_SIZE + size);

	vshc_header_make(file, ids, hash, program, size);
	memcpy(file + VSHC_HEADER_SIZE, program, size);
	return file;
}

static unsigned char *read_file(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *data;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc(length > 0 ? (size_t)length : 1);
	if (fread(data, 1, (size_t)length, file) != (size_t)length)
	{
		fclose(file);
		free(data);
		return NULL;
	}
	fclose(file);
	*size = (size_t)length;
	return data;
}

static void test_cache_files(void)
{
	const char *source = "float4 main(float2 t : TEXCOORD0, uniform sampler2D s) : COLOR { return tex2D(s, t); }\n";
	const char *other_source = "float4 main(uniform float4 c[1] : BUFFER[0]) : COLOR { return c[0]; }\n";
	uint64_t hash = vshc_source_hash(source, 1), other_hash = vshc_source_hash(other_source, 1);
	uint64_t compile_id = vshc_compile_id(BUILD_SETTINGS);
	struct vshc_ids old_build = { compile_id, 0x1111111111111111ULL };
	struct vshc_ids new_build = { compile_id, 0x2222222222222222ULL };
	struct vshc_ids other_settings = { vshc_compile_id("shacc profiles 0/1 optimization 2 warnings 1 locale 0 file halo.cg entry main"),
		0x2222222222222222ULL };
	unsigned char *program = fake_program(1000, 1), *other_program = fake_program(900, 2);
	unsigned char *file;
	const void *found;
	uint32_t found_size, size = 1000;
	size_t length, cut;
	enum vshc_result result;

	CHECK(hash != other_hash && hash != vshc_source_hash(source, 0), "the hash tells the sources and kinds apart");
	CHECK(other_settings.compile_id != compile_id, "other compiler settings: another compile id");

	/* the reproduction: what the cache accepted before (a raw program under
	the source's hash) - an older build's file, another Cg's program put
	under this name, a program with damaged bytes - all used */
	CHECK(legacy_accepts(program, size), "before: an older build's raw program is used");
	CHECK(legacy_accepts(other_program, 900), "before: another Cg's program under this hash is used");
	program[500] ^= 0x40;
	CHECK(legacy_accepts(program, size), "before: a program with damaged bytes is used");
	program[500] ^= 0x40;
	/* now: none of them */
	CHECK(vshc_file_check(program, size, &new_build, hash, &found, &found_size) == VSHC_NOT_OURS,
		"now: an older build's raw program is not used");

	/* a good file */
	file = cache_file(&new_build, hash, program, size);
	length = VSHC_HEADER_SIZE + size;
	result = vshc_file_check(file, length, &new_build, hash, &found, &found_size);
	CHECK(result == VSHC_OK && found == file + VSHC_HEADER_SIZE && found_size == size, "a good file is used (%s)",
		vshc_result_name(result));
	/* made by the build before (another generator id), or with other settings */
	free(file);
	file = cache_file(&old_build, hash, program, size);
	CHECK(vshc_file_check(file, length, &new_build, hash, &found, &found_size) == VSHC_OTHER_GENERATOR,
		"a file of a build with another generator is not used");
	free(file);
	file = cache_file(&other_settings, hash, program, size);
	CHECK(vshc_file_check(file, length, &new_build, hash, &found, &found_size) == VSHC_OTHER_COMPILE,
		"a file made with other compiler settings is not used");
	free(file);
	/* another Cg's program under this name */
	file = cache_file(&new_build, other_hash, other_program, 900);
	CHECK(vshc_file_check(file, VSHC_HEADER_SIZE + 900, &new_build, hash, &found, &found_size) == VSHC_OTHER_SOURCE,
		"another Cg's program under this name is not used");
	free(file);

	/* cut short anywhere, a byte too many, any one byte changed */
	file = cache_file(&new_build, hash, program, size);
	for (cut = 0; cut < length; cut++)
	{
		result = vshc_file_check(file, cut, &new_build, hash, &found, &found_size);
		if (result == VSHC_OK)
			break;
	}
	CHECK(cut == length, "a file cut short at %zu bytes is used", cut);
	{
		unsigned char *longer = malloc(length + 1);

		memcpy(longer, file, length);
		longer[length] = 0;
		CHECK(vshc_file_check(longer, length + 1, &new_build, hash, &found, &found_size) == VSHC_BAD_SIZE,
			"a file with bytes after the program is not used");
		free(longer);
	}
	{
		size_t index, used = 0, bad_checksum = 0;

		for (index = 0; index < length; index++)
		{
			file[index] ^= 0x01;
			result = vshc_file_check(file, length, &new_build, hash, &found, &found_size);
			used += result == VSHC_OK;
			bad_checksum += result == VSHC_BAD_CHECKSUM;
			file[index] ^= 0x01;
		}
		CHECK(used == 0, "%zu files with one byte changed are used", used);
		CHECK(bad_checksum >= size, "a changed program byte fails the checksum (%zu of %u)", bad_checksum, size);
	}
	/* zeroed program bytes (a card's clusters never written) */
	memset(file + VSHC_HEADER_SIZE + 64, 0, 512);
	CHECK(vshc_file_check(file, length, &new_build, hash, &found, &found_size) == VSHC_BAD_CHECKSUM,
		"a program with zeroed blocks is not used");
	free(file);
	/* a checksum that holds but no GXP program */
	{
		unsigned char *junk = calloc(1, 64);

		file = cache_file(&new_build, hash, junk, 64);
		CHECK(vshc_file_check(file, VSHC_HEADER_SIZE + 64, &new_build, hash, &found, &found_size) == VSHC_NOT_PROGRAM,
			"bytes that are not a program are not used");
		free(file);
		free(junk);
	}
	free(program);
	free(other_program);
}

static void test_id_file(void)
{
	struct vshc_ids ids = { vshc_compile_id(BUILD_SETTINGS), 0xfba70cbbb7d77a8fULL };
	struct vshc_ids other = { ids.compile_id, ids.generator_id ^ 1 };
	char text[64];

	vshc_id_text(text, sizeof(text), &ids);
	CHECK(strlen(text) < 63 && !strncmp(text, VSHC_MAGIC " ", 9), "the id file's text: %s", text);
	CHECK(vshc_id_matches(text, strlen(text), &ids), "this build's id file matches");
	CHECK(!vshc_id_matches(text, strlen(text), &other), "another generator's id file does not");
	CHECK(!vshc_id_matches(text, strlen(text) - 1, &ids), "a cut id file does not");
	CHECK(!vshc_id_matches("", 0, &ids), "no id file (a cache from before the ids) does not");
	CHECK(!vshc_id_matches("HCEVSHC1 0 0\n", 13, &ids), "an older id file does not");

	/* the only files the cache removes */
	CHECK(vshc_cache_file_name("0123456789abcdef.gxp"), "a cache file");
	CHECK(vshc_cache_file_name("0123456789abcdef.tmp"), "a file left half written");
	CHECK(vshc_cache_file_name("cache.id") && vshc_cache_file_name("cache.id.tmp"), "the id file");
	CHECK(!vshc_cache_file_name("0123456789ABCDEF.gxp"), "upper case is not ours");
	CHECK(!vshc_cache_file_name("0123456789abcde.gxp"), "15 digits is not ours");
	CHECK(!vshc_cache_file_name("0123456789abcdef0.gxp"), "17 digits is not ours");
	CHECK(!vshc_cache_file_name("0123456789abcdef.gxp.bak"), "a longer name is not ours");
	CHECK(!vshc_cache_file_name("0123456789abcdef.vp.cg"), "a collected source is not ours");
	CHECK(!vshc_cache_file_name("settings.txt") && !vshc_cache_file_name("maps") &&
		!vshc_cache_file_name("saves") && !vshc_cache_file_name("halo.log") && !vshc_cache_file_name("..") &&
		!vshc_cache_file_name("") && !vshc_cache_file_name("config.toml"), "settings, maps, saves, logs are not ours");
}

/* a pack of count programs (the tool's format), its hashes 1000 + 7 i */
static unsigned char *make_pack(unsigned int count, uint64_t compile_id, size_t *size)
{
	size_t offset = VSHP_HEADER_SIZE + (size_t)count * VSHP_ENTRY_SIZE, capacity = offset + count * 400 + 16;
	unsigned char *pack = calloc(1, capacity);
	uint32_t zero = 0;
	uint64_t generator_id = 0x77;
	unsigned int index;

	memcpy(pack, VSHP_MAGIC, 8);
	memcpy(pack + 8, &count, 4);
	memcpy(pack + 12, &zero, 4);
	memcpy(pack + 16, &compile_id, 8);
	memcpy(pack + 24, &generator_id, 8);
	for (index = 0; index < count; index++)
	{
		uint32_t length = 100 + index * 10, checksum, aligned = (uint32_t)((offset + 15) & ~(size_t)15);
		unsigned char *program = fake_program(length, index);
		uint64_t hash = 1000 + 7 * (uint64_t)index;
		unsigned char *entry = pack + VSHP_HEADER_SIZE + (size_t)index * VSHP_ENTRY_SIZE;

		memcpy(pack + aligned, program, length);
		checksum = vshc_checksum(program, length);
		memcpy(entry, &hash, 8);
		memcpy(entry + 8, &aligned, 4);
		memcpy(entry + 12, &length, 4);
		memcpy(entry + 16, &checksum, 4);
		memcpy(entry + 20, &zero, 4);
		offset = aligned + length;
		free(program);
	}
	*size = offset;
	return pack;
}

static void test_pack(void)
{
	uint64_t compile_id = vshc_compile_id(BUILD_SETTINGS);
	struct vshp_pack pack;
	size_t size, cut;
	unsigned char *data = make_pack(20, compile_id, &size);
	uint32_t length;
	unsigned int index, found = 0;
	const unsigned char *program;

	CHECK(vshp_open(&pack, data, size, compile_id) == VSHC_OK && pack.count == 20, "a good pack opens");
	for (index = 0; index < 20; index++)
		found += vshp_find(&pack, 1000 + 7 * (uint64_t)index, &length) != NULL && length == 100 + index * 10;
	CHECK(found == 20, "every program of a good pack is found (%u)", found);
	CHECK(!vshp_find(&pack, 1001, &length) && !vshp_find(&pack, 0, &length) && !vshp_find(&pack, ~0ULL, &length),
		"a hash not in the pack is not found");
	program = vshp_find(&pack, 1000 + 7 * 5, &length);
	data[program - data + 50] ^= 0x10;
	CHECK(!vshp_find(&pack, 1000 + 7 * 5, &length), "a program with a damaged byte is not used");
	CHECK(vshp_find(&pack, 1000 + 7 * 6, &length) != NULL, "the others still are");
	data[program - data + 50] ^= 0x10;
	CHECK(vshp_open(&pack, data, size, compile_id ^ 1) == VSHC_OTHER_COMPILE && !pack.count,
		"a pack made with other compiler settings is not used");
	memcpy(data, "HCEVSHP1", 8);
	CHECK(vshp_open(&pack, data, size, compile_id) == VSHC_NOT_OURS, "a pack of the first format is not used");
	memcpy(data, VSHP_MAGIC, 8);
	for (cut = 0; cut < size; cut++)
	{
		if (vshp_open(&pack, data, cut, compile_id) == VSHC_OK)
			break;
	}
	CHECK(cut == size, "a pack cut short at %zu bytes is used", cut);
	/* an index out of order, or pointing outside the file */
	{
		unsigned char saved[8];
		uint32_t far = (uint32_t)size;

		memcpy(saved, data + VSHP_HEADER_SIZE + 3 * VSHP_ENTRY_SIZE, 8);
		memset(data + VSHP_HEADER_SIZE + 3 * VSHP_ENTRY_SIZE, 0, 8);
		CHECK(vshp_open(&pack, data, size, compile_id) != VSHC_OK, "an unsorted index is not used");
		memcpy(data + VSHP_HEADER_SIZE + 3 * VSHP_ENTRY_SIZE, saved, 8);
		memcpy(saved, data + VSHP_HEADER_SIZE + 4 * VSHP_ENTRY_SIZE + 8, 4);
		memcpy(data + VSHP_HEADER_SIZE + 4 * VSHP_ENTRY_SIZE + 8, &far, 4);
		CHECK(vshp_open(&pack, data, size, compile_id) == VSHC_BAD_SIZE, "an entry outside the file is not used");
		memcpy(data + VSHP_HEADER_SIZE + 4 * VSHP_ENTRY_SIZE + 8, saved, 4);
	}
	CHECK(vshp_open(&pack, data, size, compile_id) == VSHC_OK, "the pack is whole again");
	free(data);
}

/* <dir>/<hash>.fp.cg and <dir>/progs/<hash>.gxp for a few sources, as the Vita's collecting and cache write them */
static int write_cache(const char *directory)
{
	struct vshc_ids ids = { vshc_compile_id(BUILD_SETTINGS), 0x3333333333333333ULL };
	unsigned int index;

	for (index = 0; index < 6; index++)
	{
		char source[128], path[512];
		uint64_t hash;
		unsigned char *program = fake_program(200 + 30 * index, index), *file;
		uint32_t size = 200 + 30 * index;
		FILE *out;

		snprintf(source, sizeof(source), "float4 main() : COLOR { return float4(%u, 0, 0, 1); }\n", index);
		hash = vshc_source_hash(source, 1);
		snprintf(path, sizeof(path), "%s/%016llx.fp.cg", directory, (unsigned long long)hash);
		if (!(out = fopen(path, "wb")))
			return 1;
		fputs(source, out);
		fclose(out);
		file = cache_file(&ids, hash, program, size);
		snprintf(path, sizeof(path), "%s/progs/%016llx.gxp", directory, (unsigned long long)hash);
		if (!(out = fopen(path, "wb")))
			return 1;
		fwrite(file, 1, VSHC_HEADER_SIZE + size, out);
		fclose(out);
		free(file);
		free(program);
	}
	return 0;
}

/* the pack the tool made from write_cache's files: every source's program, the ids carried over */
static void check_pack(const char *pack_path, const char *directory)
{
	struct vshp_pack pack;
	size_t size;
	unsigned char *data = read_file(pack_path, &size);
	DIR *listing = opendir(directory);
	struct dirent *entry;
	unsigned int sources = 0, found = 0;

	CHECK(data && listing, "the pack and the sources are there");
	if (!data || !listing)
		return;
	CHECK(vshp_open(&pack, data, size, vshc_compile_id(BUILD_SETTINGS)) == VSHC_OK, "the tool's pack opens");
	CHECK(pack.generator_id == 0x3333333333333333ULL, "the tool's pack keeps the cache's generator id");
	while ((entry = readdir(listing)) != NULL)
	{
		size_t source_size;
		char path[512];
		unsigned char *source;
		uint32_t length;

		if (strlen(entry->d_name) != 22 || strcmp(entry->d_name + 16, ".fp.cg"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
		source = read_file(path, &source_size);
		source = realloc(source, source_size + 1);
		source[source_size] = 0;
		sources++;
		found += vshp_find(&pack, vshc_source_hash((char *)source, 1), &length) != NULL;
		free(source);
	}
	closedir(listing);
	CHECK(sources == 6 && found == sources, "every source's program is in the tool's pack (%u of %u)", found, sources);
	free(data);
}

/* the VPK's pack: made with this build's compiler settings, every program whole */
static void check_shipped(const char *pack_path)
{
	struct vshp_pack pack;
	size_t size;
	unsigned char *data = read_file(pack_path, &size);
	unsigned int index, found = 0;

	CHECK(data != NULL, "%s is there", pack_path);
	if (!data)
		return;
	CHECK(vshp_open(&pack, data, size, vshc_compile_id(BUILD_SETTINGS)) == VSHC_OK, "%s opens with this build's compile id",
		pack_path);
	for (index = 0; index < pack.count; index++)
	{
		uint64_t hash;
		uint32_t length;

		memcpy(&hash, data + VSHP_HEADER_SIZE + (size_t)index * VSHP_ENTRY_SIZE, 8);
		found += vshp_find(&pack, hash, &length) != NULL;
	}
	CHECK(pack.count > 200 && found == pack.count, "every shipped program is whole (%u of %u)", found, pack.count);
	printf("shipped pack: %u programs, %zu bytes, generator id %016llx\n", pack.count, size,
		(unsigned long long)pack.generator_id);
	free(data);
}

static void check_rejected(const char *pack_path)
{
	struct vshp_pack pack;
	size_t size;
	unsigned char *data = read_file(pack_path, &size);
	enum vshc_result result;

	CHECK(data != NULL, "%s is there", pack_path);
	if (!data)
		return;
	result = vshp_open(&pack, data, size, vshc_compile_id(BUILD_SETTINGS));
	CHECK(result != VSHC_OK, "%s (another build's pack) is not used", pack_path);
	printf("%s: %s\n", pack_path, vshc_result_name(result));
	free(data);
}

/* ---------- another build's cache moved aside, removed later (vshc_retire, vshc_sweep) */

struct test_fs
{
	int fail_rename;
	unsigned long renames, removes, lists;
};

static int fs_rename(void *context, const char *from, const char *to)
{
	struct test_fs *fs = context;

	fs->renames++;
	return fs->fail_rename ? -1 : rename(from, to);
}

static int fs_make_directory(void *context, const char *path)
{
	(void)context;
	return mkdir(path, 0777);
}

static int fs_remove(void *context, const char *path)
{
	((struct test_fs *)context)->removes++;
	return unlink(path);
}

static int fs_remove_directory(void *context, const char *path)
{
	(void)context;
	return rmdir(path);
}

static int fs_write_text(void *context, const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");

	(void)context;
	if (!file)
		return -1;
	fputs(text, file);
	return fclose(file) == 0 ? 0 : -1;
}

static int fs_exists(void *context, const char *path)
{
	struct stat st;

	(void)context;
	return stat(path, &st) == 0;
}

static int fs_read_head(void *context, const char *path, void *buffer, unsigned int size)
{
	FILE *file = fopen(path, "rb");
	size_t count;

	(void)context;
	if (!file)
		return -1;
	count = fread(buffer, 1, size, file);
	fclose(file);
	return (int)count;
}

static int fs_list(void *context, const char *path, void (*each)(void *argument, const char *name), void *argument)
{
	DIR *listing = opendir(path);
	struct dirent *entry;

	((struct test_fs *)context)->lists++;
	if (!listing)
		return -1;
	while ((entry = readdir(listing)) != NULL)
		if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
			each(argument, entry->d_name);
	closedir(listing);
	return 0;
}

static unsigned long count_entries(const char *path)
{
	DIR *listing = opendir(path);
	struct dirent *entry;
	unsigned long count = 0;

	if (!listing)
		return (unsigned long)-1;
	while ((entry = readdir(listing)) != NULL)
		count += strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..");
	closedir(listing);
	return count;
}

static void plant(const char *folder, const struct vshc_ids *ids, unsigned long first, unsigned long count)
{
	unsigned char *program = fake_program(64, 3);
	unsigned long index;
	char path[600];

	mkdir(folder, 0777);
	for (index = first; index < first + count; index++)
	{
		uint64_t hash = 0x1000000000000000ULL + index * 0x9e3779b9ULL;
		unsigned char *file = cache_file(ids, hash, program, 64);
		FILE *out;

		snprintf(path, sizeof(path), "%s/%016llx.gxp", folder, (unsigned long long)hash);
		out = fopen(path, "wb");
		fwrite(file, 1, VSHC_HEADER_SIZE + 64, out);
		fclose(out);
		free(file);
	}
	free(program);
}

static unsigned long pace_budget, pace_calls;

static int pace_limited(void *argument)
{
	(void)argument;
	pace_calls++;
	return pace_budget ? (pace_budget--, 1) : 0;
}

static void test_sweep(const char *root)
{
	struct test_fs state = { 0, 0, 0, 0 };
	struct vshc_fs fs = { &state, fs_rename, fs_make_directory, fs_remove, fs_remove_directory, fs_write_text,
		fs_exists, fs_read_head, fs_list };
	struct vshc_ids old_build = { 0x5555555555555555ULL, 0x1111111111111111ULL };
	struct vshc_ids new_build = { 0x5555555555555555ULL, 0x2222222222222222ULL };
	struct vshc_sweep_counts counts;
	unsigned long removed, folders;
	char cache[512], path[600], old1[512], old2[512];
	enum vshc_retire_result result;
	int index;

	snprintf(cache, sizeof(cache), "%s/shaders", root);
	snprintf(old1, sizeof(old1), "%s/shaders.old-1", root);
	snprintf(old2, sizeof(old2), "%s/shaders.old-2", root);

	CHECK(vshc_old_folder_index("shaders.old-1", "shaders") == 1 && vshc_old_folder_index("shaders.old-99", "shaders") == 99,
		"old folder names are read");
	CHECK(!vshc_old_folder_index("shaders.old-0", "shaders") && !vshc_old_folder_index("shaders.old-01", "shaders") &&
		!vshc_old_folder_index("shaders.old-100", "shaders") && !vshc_old_folder_index("shaders.old-", "shaders") &&
		!vshc_old_folder_index("shaders.old-1x", "shaders") && !vshc_old_folder_index("shadersx.old-1", "shaders") &&
		!vshc_old_folder_index("shaders", "shaders") && !vshc_old_folder_index("maps", "shaders") &&
		!vshc_old_folder_index("saves.old-1", "shaders"), "no other name is an old folder");

	/* what must never be touched: other folders beside it, even holding cache-like names */
	snprintf(path, sizeof(path), "%s/maps", root);
	plant(path, &old_build, 0, 3);
	snprintf(path, sizeof(path), "%s/saves", root);
	mkdir(path, 0777);
	snprintf(path, sizeof(path), "%s/saves/0123456789abcdef.gxp", root);
	fs_write_text(NULL, path, "a save named like a program\n");

	/* another build's cache of 600 programs, its id file, and a file of someone else's */
	plant(cache, &old_build, 0, 600);
	snprintf(path, sizeof(path), "%s/" VSHC_ID_FILE_NAME, cache);
	fs_write_text(NULL, path, "HCEVSHC2 5555555555555555 1111111111111111\n");
	snprintf(path, sizeof(path), "%s/notes.txt", cache);
	fs_write_text(NULL, path, "mine\n");
	CHECK(!vshc_sweep_pending(&fs, root, "shaders"), "nothing to clean up before");

	/* start-up: one rename and one folder made, no file removed */
	state.removes = state.renames = 0;
	result = vshc_retire(&fs, root, "shaders", &index);
	CHECK(result == VSHC_RETIRE_RENAMED && index == 1, "the cache is moved to shaders.old-1 (%d, %d)", (int)result, index);
	CHECK(state.removes == 0 && state.renames == 1, "start-up removes nothing (%lu removals, %lu renames)", state.removes,
		state.renames);
	CHECK(count_entries(cache) == 0 && count_entries(old1) == 602, "an empty cache folder, the old one whole (%lu, %lu)",
		count_entries(cache), count_entries(old1));
	CHECK(vshc_sweep_pending(&fs, root, "shaders"), "a clean-up is pending");

	/* another update before the clean-up ran: shaders.old-2 */
	plant(cache, &old_build, 1000, 5);
	result = vshc_retire(&fs, root, "shaders", &index);
	CHECK(result == VSHC_RETIRE_RENAMED && index == 2 && count_entries(old2) == 5, "a second update: shaders.old-2 (%d)",
		index);

	/* the clean-up stopped after 250 removals (the game quit) ... */
	pace_budget = 250;
	pace_calls = 0;
	CHECK(!vshc_sweep(&fs, root, "shaders", &new_build, pace_limited, NULL, &counts) && counts.removed == 250,
		"a clean-up stopped part way (%lu removed)", counts.removed);
	removed = counts.removed;
	folders = counts.folders;
	CHECK(vshc_sweep_pending(&fs, root, "shaders"), "still pending after it stopped");
	/* ... goes on at the next start: everything but the file of someone else's */
	pace_budget = 100000;
	CHECK(!vshc_sweep(&fs, root, "shaders", &new_build, pace_limited, NULL, &counts), "a folder with a foreign file stays");
	removed += counts.removed;
	folders += counts.folders;
	CHECK(removed == 606 && folders == 1 && counts.left == 1,
		"the rest removed at the next start (%lu files, %lu folders in all, %lu left)", removed, folders, counts.left);
	CHECK(count_entries(old1) == 1 && !fs_exists(NULL, old2), "only the foreign file is left (%lu)", count_entries(old1));
	snprintf(path, sizeof(path), "%s/notes.txt", old1);
	unlink(path);
	CHECK(vshc_sweep(&fs, root, "shaders", &new_build, pace_limited, NULL, &counts) && counts.folders == 1 &&
		!fs_exists(NULL, old1), "then the folder goes");
	CHECK(!vshc_sweep_pending(&fs, root, "shaders"), "nothing pending at the end");
	snprintf(path, sizeof(path), "%s/maps", root);
	CHECK(count_entries(path) == 3, "the folders beside it are untouched (maps %lu)", count_entries(path));
	snprintf(path, sizeof(path), "%s/saves/0123456789abcdef.gxp", root);
	CHECK(fs_exists(NULL, path), "a save named like a program is untouched");

	/* where the rename fails: marked, swept in place, this build's files kept */
	rmdir(cache);
	plant(cache, &old_build, 0, 40);
	plant(cache, &new_build, 2000, 7);
	snprintf(path, sizeof(path), "%s/" VSHC_ID_FILE_NAME, cache);
	fs_write_text(NULL, path, "HCEVSHC2 5555555555555555 2222222222222222\n");
	snprintf(path, sizeof(path), "%s/fedcba9876543210.tmp", cache);
	fs_write_text(NULL, path, "being written\n");
	snprintf(path, sizeof(path), "%s/0000000000000001.gxp", cache);
	fs_write_text(NULL, path, "an older build's raw program\n");
	state.fail_rename = 1;
	result = vshc_retire(&fs, root, "shaders", &index);
	CHECK(result == VSHC_RETIRE_IN_PLACE && index == 0, "a failed rename: swept in place (%d)", (int)result);
	CHECK(vshc_sweep_pending(&fs, root, "shaders"), "the in-place sweep is pending");
	pace_budget = 10;
	CHECK(!vshc_sweep(&fs, root, "shaders", &new_build, pace_limited, NULL, &counts) && counts.removed == 10 &&
		vshc_sweep_pending(&fs, root, "shaders"), "stopped part way, still marked");
	pace_budget = 100000;
	CHECK(vshc_sweep(&fs, root, "shaders", &new_build, pace_limited, NULL, &counts) && counts.removed == 31,
		"the in-place sweep finishes (%lu removed)", counts.removed);
	CHECK(count_entries(cache) == 9 && !vshc_sweep_pending(&fs, root, "shaders"),
		"this build's 7 programs, its id file and the file being written stay (%lu)", count_entries(cache));
}

static int run_sweep(const char *root)
{
	test_sweep(root);
	printf("%d of %d checks passed\n", checks - failures, checks);
	return failures != 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "sweep"))
		return run_sweep(argv[2]);
	if (argc == 3 && !strcmp(argv[1], "write-cache"))
		return write_cache(argv[2]);
	if (argc == 4 && !strcmp(argv[1], "check-pack"))
		check_pack(argv[2], argv[3]);
	else if (argc == 3 && !strcmp(argv[1], "shipped"))
		check_shipped(argv[2]);
	else if (argc == 3 && !strcmp(argv[1], "reject"))
		check_rejected(argv[2]);
	else
	{
		test_cache_files();
		test_id_file();
		test_pack();
		printf("compile id for this build's settings: %016llx\n", (unsigned long long)vshc_compile_id(BUILD_SETTINGS));
	}
	printf("%d of %d checks passed\n", checks - failures, checks);
	return failures != 0;
}
