/*
The Vita's compiled shader program files: the memory card's cache and the
shipped pack (port/vita/include/vita_shader_cache.h, which see for the
formats). Plain C: built for the Vita and for the desktop test.
*/
#include "vita_shader_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t vshc_source_hash(const char *source, int fragment)
{
	uint64_t hash = 14695981039346656037ULL ^ (uint64_t)fragment;

	while (*source)
		hash = (hash ^ (unsigned char)*source++) * 1099511628211ULL;
	return hash;
}

uint32_t vshc_checksum(const void *data, size_t size)
{
	const unsigned char *bytes = data;
	uint32_t hash = 2166136261U;

	while (size--)
		hash = (hash ^ *bytes++) * 16777619U;
	return hash;
}

uint64_t vshc_compile_id(const char *settings)
{
	char text[32];
	uint64_t hash = 14695981039346656037ULL;
	const char *pointer;

	snprintf(text, sizeof(text), "format %d;", VSHC_FORMAT_VERSION);
	for (pointer = text; *pointer; pointer++)
		hash = (hash ^ (unsigned char)*pointer) * 1099511628211ULL;
	for (pointer = settings; *pointer; pointer++)
		hash = (hash ^ (unsigned char)*pointer) * 1099511628211ULL;
	return hash;
}

static uint32_t read32(const unsigned char *bytes)
{
	uint32_t value;

	memcpy(&value, bytes, 4);
	return value;
}

static uint64_t read64(const unsigned char *bytes)
{
	uint64_t value;

	memcpy(&value, bytes, 8);
	return value;
}

int vshc_gxp_whole(const void *data, size_t size)
{
	const unsigned char *bytes = data;

	return size >= 16 && !memcmp(bytes, "GXP\0", 4) && read32(bytes + 8) == size;
}

const char *vshc_result_name(enum vshc_result result)
{
	switch (result)
	{
	case VSHC_OK: return "good";
	case VSHC_SHORT: return "cut short";
	case VSHC_NOT_OURS: return "in an older format (or not ours)";
	case VSHC_OTHER_COMPILE: return "made with other compiler settings";
	case VSHC_OTHER_GENERATOR: return "made by a build with another Cg generator";
	case VSHC_OTHER_SOURCE: return "the program of another Cg";
	case VSHC_BAD_SIZE: return "of the wrong size";
	case VSHC_BAD_CHECKSUM: return "damaged (checksum)";
	case VSHC_NOT_PROGRAM: return "not a whole program";
	}
	return "?";
}

void vshc_header_make(unsigned char *header, const struct vshc_ids *ids, uint64_t hash, const void *program,
	uint32_t program_size)
{
	uint32_t checksum = vshc_checksum(program, program_size);

	memcpy(header, VSHC_MAGIC, 8);
	memcpy(header + 8, &ids->compile_id, 8);
	memcpy(header + 16, &ids->generator_id, 8);
	memcpy(header + 24, &hash, 8);
	memcpy(header + 32, &program_size, 4);
	memcpy(header + 36, &checksum, 4);
}

enum vshc_result vshc_header_check(const unsigned char *header, size_t file_size, const struct vshc_ids *ids,
	uint64_t hash, uint32_t *program_size)
{
	uint32_t size;

	if (file_size < VSHC_HEADER_SIZE)
		return VSHC_SHORT;
	if (memcmp(header, VSHC_MAGIC, 8))
		return VSHC_NOT_OURS;
	if (read64(header + 8) != ids->compile_id)
		return VSHC_OTHER_COMPILE;
	if (read64(header + 16) != ids->generator_id)
		return VSHC_OTHER_GENERATOR;
	if (read64(header + 24) != hash)
		return VSHC_OTHER_SOURCE;
	size = read32(header + 32);
	if (file_size < (size_t)VSHC_HEADER_SIZE + size)
		return VSHC_SHORT;
	if (file_size != (size_t)VSHC_HEADER_SIZE + size || size < 16)
		return VSHC_BAD_SIZE;
	*program_size = size;
	return VSHC_OK;
}

enum vshc_result vshc_program_check(const unsigned char *header, const void *program, uint32_t program_size)
{
	if (read32(header + 32) != program_size)
		return VSHC_BAD_SIZE;
	if (vshc_checksum(program, program_size) != read32(header + 36))
		return VSHC_BAD_CHECKSUM;
	if (!vshc_gxp_whole(program, program_size))
		return VSHC_NOT_PROGRAM;
	return VSHC_OK;
}

enum vshc_result vshc_file_check(const void *data, size_t size, const struct vshc_ids *ids, uint64_t hash,
	const void **program, uint32_t *program_size)
{
	const unsigned char *bytes = data;
	enum vshc_result result;
	uint32_t length;

	if ((result = vshc_header_check(bytes, size, ids, hash, &length)) != VSHC_OK)
		return result;
	if ((result = vshc_program_check(bytes, bytes + VSHC_HEADER_SIZE, length)) != VSHC_OK)
		return result;
	*program = bytes + VSHC_HEADER_SIZE;
	*program_size = length;
	return VSHC_OK;
}

void vshc_id_text(char *text, size_t size, const struct vshc_ids *ids)
{
	snprintf(text, size, "%s %016llx %016llx\n", VSHC_MAGIC, (unsigned long long)ids->compile_id,
		(unsigned long long)ids->generator_id);
}

int vshc_id_matches(const char *contents, size_t size, const struct vshc_ids *ids)
{
	char text[64];

	vshc_id_text(text, sizeof(text), ids);
	return size == strlen(text) && !memcmp(contents, text, size);
}

int vshc_cache_file_name(const char *name)
{
	int index;

	if (!strcmp(name, VSHC_ID_FILE_NAME) || !strcmp(name, VSHC_ID_FILE_NAME ".tmp"))
		return 1;
	for (index = 0; index < 16; index++)
	{
		char c = name[index];

		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return 0;
	}
	return !strcmp(name + 16, ".gxp") || !strcmp(name + 16, ".tmp");
}

/* ---------- another build's cache: moved aside, removed later */

int vshc_old_folder_index(const char *entry, const char *name)
{
	size_t length = strlen(name), suffix = strlen(VSHC_OLD_FOLDER_SUFFIX);
	const char *digits = entry + length + suffix;
	int value = 0;

	if (strncmp(entry, name, length) || strncmp(entry + length, VSHC_OLD_FOLDER_SUFFIX, suffix) || !*digits ||
		*digits == '0' || strlen(digits) > 2)
		return 0;
	for (; *digits; digits++)
	{
		if (*digits < '0' || *digits > '9')
			return 0;
		value = value * 10 + (*digits - '0');
	}
	return value <= VSHC_OLD_FOLDERS_MAX ? value : 0;
}

enum vshc_retire_result vshc_retire(const struct vshc_fs *fs, const char *parent, const char *name, int *index)
{
	char path[256], old_path[300];
	int n;

	snprintf(path, sizeof(path), "%s/%s", parent, name);
	*index = 0;
	for (n = 1; n <= VSHC_OLD_FOLDERS_MAX; n++)
	{
		snprintf(old_path, sizeof(old_path), "%s/%s" VSHC_OLD_FOLDER_SUFFIX "%d", parent, name, n);
		if (!fs->exists(fs->context, old_path))
			break;
	}
	if (n <= VSHC_OLD_FOLDERS_MAX && fs->rename(fs->context, path, old_path) >= 0)
	{
		*index = n;
		/* (if this fails, nothing is cached this run; the next start makes it) */
		fs->make_directory(fs->context, path);
		return VSHC_RETIRE_RENAMED;
	}
	snprintf(old_path, sizeof(old_path), "%s/" VSHC_SWEEP_FILE_NAME, path);
	return fs->write_text(fs->context, old_path, "sweep\n") >= 0 ? VSHC_RETIRE_IN_PLACE : VSHC_RETIRE_FAILED;
}

/* a folder's entry names (each under 64 bytes; longer ones are not ours) */
struct name_list
{
	char (*names)[64];
	unsigned long count, capacity;
	int full;
};

static void name_list_add(void *argument, const char *name)
{
	struct name_list *list = argument;

	if (strlen(name) >= sizeof(list->names[0]) || list->full)
		return;
	if (list->count == list->capacity)
	{
		unsigned long capacity = list->capacity ? list->capacity * 2 : 64;
		char (*grown)[64] = realloc(list->names, capacity * sizeof(list->names[0]));

		if (!grown)
		{
			list->full = 1;
			return;
		}
		list->names = grown;
		list->capacity = capacity;
	}
	strcpy(list->names[list->count++], name);
}

static int name_list_read(const struct vshc_fs *fs, const char *path, struct name_list *list)
{
	memset(list, 0, sizeof(*list));
	return fs->list(fs->context, path, name_list_add, list) >= 0;
}

int vshc_sweep_pending(const struct vshc_fs *fs, const char *parent, const char *name)
{
	struct name_list list;
	unsigned long index;
	int pending = 0;
	char path[256];

	snprintf(path, sizeof(path), "%s/%s/" VSHC_SWEEP_FILE_NAME, parent, name);
	if (fs->exists(fs->context, path))
		return 1;
	if (name_list_read(fs, parent, &list))
		for (index = 0; index < list.count && !pending; index++)
			pending = vshc_old_folder_index(list.names[index], name) != 0;
	free(list.names);
	return pending;
}

/* one folder: an old one emptied of the cache's files and removed, or the
cache folder (in_place) emptied of the files another build wrote; 1 done,
0 something stays, -1 stopped by pace */
static int sweep_folder(const struct vshc_fs *fs, const char *folder, const struct vshc_ids *ids, int in_place,
	int (*pace)(void *argument), void *argument, struct vshc_sweep_counts *counts)
{
	struct name_list list;
	unsigned long index;
	int done = 1;
	char path[320];

	/* (the names first, then the removals: a folder changed while it is
	listed can skip entries) */
	if (!name_list_read(fs, folder, &list))
		return 0;
	for (index = 0; index < list.count; index++)
	{
		const char *entry = list.names[index];

		if (!vshc_cache_file_name(entry))
		{
			/* (the in-place marker goes last) */
			if (!(in_place && !strcmp(entry, VSHC_SWEEP_FILE_NAME)))
				counts->left++;
			continue;
		}
		snprintf(path, sizeof(path), "%s/%s", folder, entry);
		if (in_place)
		{
			unsigned char header[VSHC_HEADER_SIZE];
			uint64_t compile_id, generator_id;

			/* this build's id file and files being written stay, and a
			program this build wrote */
			if (strcmp(entry + strlen(entry) - 4, ".gxp"))
				continue;
			if (fs->read_head(fs->context, path, header, sizeof(header)) == (int)sizeof(header) &&
				!memcmp(header, VSHC_MAGIC, 8))
			{
				memcpy(&compile_id, header + 8, 8);
				memcpy(&generator_id, header + 16, 8);
				if (compile_id == ids->compile_id && generator_id == ids->generator_id)
					continue;
			}
		}
		if (pace && !pace(argument))
		{
			done = -1;
			break;
		}
		if (fs->remove(fs->context, path) >= 0)
			counts->removed++;
		else
		{
			counts->left++;
			done = 0;
		}
	}
	free(list.names);
	if (done == 1 && list.full)
		done = 0;
	if (done != 1)
		return done;
	if (in_place)
	{
		snprintf(path, sizeof(path), "%s/" VSHC_SWEEP_FILE_NAME, folder);
		return fs->remove(fs->context, path) >= 0;
	}
	/* (fails, and the folder stays, if anything else is in it) */
	if (fs->remove_directory(fs->context, folder) < 0)
		return 0;
	counts->folders++;
	return 1;
}

int vshc_sweep(const struct vshc_fs *fs, const char *parent, const char *name, const struct vshc_ids *ids,
	int (*pace)(void *argument), void *argument, struct vshc_sweep_counts *counts)
{
	struct name_list list;
	unsigned long index;
	int done = 1, result;
	char path[256];

	memset(counts, 0, sizeof(*counts));
	snprintf(path, sizeof(path), "%s/%s/" VSHC_SWEEP_FILE_NAME, parent, name);
	if (fs->exists(fs->context, path))
	{
		snprintf(path, sizeof(path), "%s/%s", parent, name);
		if ((result = sweep_folder(fs, path, ids, 1, pace, argument, counts)) < 0)
			return 0;
		done = result;
	}
	if (!name_list_read(fs, parent, &list))
		return 0;
	for (index = 0; index < list.count; index++)
	{
		if (!vshc_old_folder_index(list.names[index], name))
			continue;
		snprintf(path, sizeof(path), "%s/%s", parent, list.names[index]);
		if ((result = sweep_folder(fs, path, ids, 0, pace, argument, counts)) != 1)
		{
			done = 0;
			/* (stopped: the rest waits too) */
			if (result < 0)
				break;
		}
	}
	free(list.names);
	return done;
}

enum vshc_result vshp_open(struct vshp_pack *pack, const void *data, size_t size, uint64_t compile_id)
{
	const unsigned char *bytes = data;
	unsigned int count, index;
	uint64_t previous = 0;

	memset(pack, 0, sizeof(*pack));
	if (size < VSHP_HEADER_SIZE)
		return VSHC_SHORT;
	if (memcmp(bytes, VSHP_MAGIC, 8))
		return VSHC_NOT_OURS;
	if (read64(bytes + 16) != compile_id)
		return VSHC_OTHER_COMPILE;
	count = read32(bytes + 8);
	if ((size - VSHP_HEADER_SIZE) / VSHP_ENTRY_SIZE < count)
		return VSHC_SHORT;
	/* every entry inside the file, sorted (the lookup halves the range) */
	for (index = 0; index < count; index++)
	{
		const unsigned char *entry = bytes + VSHP_HEADER_SIZE + (size_t)index * VSHP_ENTRY_SIZE;
		uint64_t hash = read64(entry);
		uint32_t offset = read32(entry + 8), length = read32(entry + 12);

		if ((uint64_t)offset + length > size || offset < VSHP_HEADER_SIZE + (size_t)count * VSHP_ENTRY_SIZE ||
			(offset & 15) || length < 16)
			return VSHC_BAD_SIZE;
		if (index && hash <= previous)
			return VSHC_NOT_OURS;
		previous = hash;
	}
	pack->data = bytes;
	pack->size = size;
	pack->count = count;
	pack->compile_id = compile_id;
	pack->generator_id = read64(bytes + 24);
	return VSHC_OK;
}

const void *vshp_find(const struct vshp_pack *pack, uint64_t hash, uint32_t *program_size)
{
	unsigned int low = 0, high = pack->count;

	while (low < high)
	{
		unsigned int middle = low + (high - low) / 2;
		const unsigned char *entry = pack->data + VSHP_HEADER_SIZE + (size_t)middle * VSHP_ENTRY_SIZE;
		uint64_t entry_hash = read64(entry);

		if (entry_hash < hash)
			low = middle + 1;
		else if (entry_hash > hash)
			high = middle;
		else
		{
			const unsigned char *program = pack->data + read32(entry + 8);
			uint32_t length = read32(entry + 12);

			if (vshc_checksum(program, length) != read32(entry + 16) || !vshc_gxp_whole(program, length))
				return NULL;
			*program_size = length;
			return program;
		}
	}
	return NULL;
}
