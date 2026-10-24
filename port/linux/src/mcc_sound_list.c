/*
MCC_SOUND_LIST.C

(port) The sound lists of MCC's Halo CE (mcc_sound_list.h).
*/

#include "mcc_sound_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t u32_le(uint8_t const *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void error_set(char *error, unsigned long error_size, char const *format, unsigned long value)
{
	if (error && error_size)
		snprintf(error, error_size, format, value);
}

/* a path's character as compared: lower case, '/' as '\' */
static int key_character(char character)
{
	if (character >= 'A' && character <= 'Z')
		return character - 'A' + 'a';
	if (character == '/')
		return '\\';
	return (unsigned char)character;
}

static int key_compare(char const *a, char const *b)
{
	for (;; a++, b++)
	{
		int ca = key_character(*a);
		int cb = key_character(*b);

		if (ca != cb)
			return ca < cb ? -1 : 1;
		if (!ca)
			return 0;
	}
}

static struct mcc_sound_list const *sort_list;

static int sort_compare(void const *left, void const *right)
{
	struct mcc_sound_list const *list = sort_list;
	int result = key_compare(list->paths + list->records[*(uint32_t const *)left].path_offset,
		list->paths + list->records[*(uint32_t const *)right].path_offset);

	if (result)
		return result;
	/* (a path listed twice: the first stays first) */
	return *(uint32_t const *)left < *(uint32_t const *)right ? -1 : *(uint32_t const *)left > *(uint32_t const *)right;
}

void mcc_sound_list_free(struct mcc_sound_list *list)
{
	free(list->records);
	free(list->sorted);
	free(list->paths);
	memset(list, 0, sizeof(*list));
}

int mcc_sound_list_parse(
	uint8_t const *bytes,
	uint32_t size,
	struct mcc_sound_list *list,
	char *error,
	unsigned long error_size)
{
	uint32_t count;
	uint32_t at = 8;
	uint32_t index;
	uint32_t pool = 0;

	memset(list, 0, sizeof(*list));
	if (size < 8)
	{
		error_set(error, error_size, "too short (%lu bytes)", size);
		return 0;
	}
	if (u32_le(bytes) != MCC_SOUND_LIST_VERSION)
	{
		error_set(error, error_size, "version %lu is not 1", u32_le(bytes));
		return 0;
	}
	count = u32_le(bytes + 4);
	/* (a record is 13 bytes at the least) */
	if (count > MCC_SOUND_LIST_MAXIMUM_RECORDS || (uint64_t)count * 13 > size - 8)
	{
		error_set(error, error_size, "%lu records cannot fit", count);
		return 0;
	}
	list->records = (struct mcc_sound_list_record *)calloc(count ? count : 1, sizeof(*list->records));
	list->sorted = (uint32_t *)calloc(count ? count : 1, sizeof(uint32_t));
	list->paths = (char *)malloc(size);
	if (!list->records || !list->sorted || !list->paths)
	{
		mcc_sound_list_free(list);
		error_set(error, error_size, "out of memory (%lu records)", count);
		return 0;
	}
	for (index = 0; index < count; index++)
	{
		uint32_t path_bytes;
		uint32_t scan;

		if (size - at < 4)
			goto cut;
		path_bytes = u32_le(bytes + at);
		at += 4;
		if (path_bytes < 2 || path_bytes > MCC_SOUND_LIST_MAXIMUM_PATH_BYTES)
		{
			error_set(error, error_size, "record %lu has an impossible path length", index);
			goto fail;
		}
		if (size - at < path_bytes || size - at - path_bytes < 8)
			goto cut;
		if (bytes[at + path_bytes - 1] != 0)
		{
			error_set(error, error_size, "record %lu's path is not zero-terminated", index);
			goto fail;
		}
		for (scan = 0; scan + 1 < path_bytes; scan++)
		{
			if (bytes[at + scan] == 0 || bytes[at + scan] < 0x20 || bytes[at + scan] >= 0x7F)
			{
				error_set(error, error_size, "record %lu's path has a character that is not text", index);
				goto fail;
			}
		}
		memcpy(list->paths + pool, bytes + at, path_bytes);
		list->records[index].path_offset = pool;
		pool += path_bytes;
		at += path_bytes;
		list->records[index].first = u32_le(bytes + at);
		list->records[index].streams = u32_le(bytes + at + 4);
		at += 8;
		list->sorted[index] = index;
	}
	if (at != size)
	{
		error_set(error, error_size, "%lu bytes follow the last record", size - at);
		goto fail;
	}
	list->count = count;
	list->path_bytes = pool;
	sort_list = list;
	qsort(list->sorted, count, sizeof(uint32_t), sort_compare);
	sort_list = NULL;
	return 1;

cut:
	error_set(error, error_size, "cut off inside record %lu", index);
fail:
	mcc_sound_list_free(list);
	return 0;
}

char const *mcc_sound_list_path(struct mcc_sound_list const *list, struct mcc_sound_list_record const *record)
{
	return list->paths + record->path_offset;
}

struct mcc_sound_list_record const *mcc_sound_list_find(struct mcc_sound_list const *list, char const *path)
{
	char key[MCC_SOUND_LIST_MAXIMUM_PATH_BYTES];
	size_t length = strlen(path);
	uint32_t low = 0;
	uint32_t high = list->count;

	if (length >= 6 && !key_compare(path + length - 6, ".sound"))
		length -= 6;
	if (length >= sizeof(key))
		return NULL;
	memcpy(key, path, length);
	key[length] = 0;
	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2;
		struct mcc_sound_list_record const *record = &list->records[list->sorted[middle]];
		int result = key_compare(key, list->paths + record->path_offset);

		if (!result)
		{
			/* (the first of equal paths) */
			while (middle > 0 && !key_compare(key, list->paths + list->records[list->sorted[middle - 1]].path_offset))
				middle--;
			return &list->records[list->sorted[middle]];
		}
		if (result < 0)
			high = middle;
		else
			low = middle + 1;
	}
	return NULL;
}
