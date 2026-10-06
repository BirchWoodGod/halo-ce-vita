/*
The Vita's compiled shader program files: the memory card's cache and the
shipped pack (port/vita/include/vita_shader_cache.h, which see for the
formats). Plain C: built for the Vita and for the desktop test.
*/
#include "vita_shader_cache.h"

#include <stdio.h>
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
