/*
MCC_SOUND.C

(port) The sounds of MCC's own Halo CE maps (mcc_sound.h).
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "cseries_windows.h"
#include "cache_file_formats.h"
#include "mcc_sound.h"
#include "../src/fsb5.h"
#include "../src/mcc_sound_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);
unsigned long long vita_host_time_us(void);

/* ---------- constants */

/* the lists of the banks of CELT streams, read to count what is not played */
static char const *const celt_lists[] =
{
	"lst\\sounds_dialogs.lst.bin",
	"lst\\sounds_music.lst.bin",
	"lst\\sounds_stream.lst.bin",
	"lst\\sounds_debug.lst.bin",
};
#define CELT_LIST_COUNT ((int)(sizeof(celt_lists) / sizeof(celt_lists[0])))
/* the largest list file read (the dialogue's is 121 KB) */
#define LIST_MAXIMUM_BYTES (4u * 1024u * 1024u)

/* ---------- globals */

static struct
{
	boolean opened;
	HANDLE bank_file;
	struct fsb5 bank;
	struct mcc_sound_list adpcm_list;
	struct mcc_sound_list celt_list[CELT_LIST_COUNT];
	boolean celt_loaded[CELT_LIST_COUNT];
	boolean trace;
	/* when it opened, and what it holds (for the log) */
	unsigned long long opened_at;
	unsigned long memory_bytes;
} mcc_sound_globals;

/* ---------- private code */

static int file_read(void *context, uint64_t offset, uint32_t size, void *buffer)
{
	uint32_t done = 0;

	if (offset > 0xFFFFFFFFull)
		return 0;
	while (done < size)
	{
		OVERLAPPED position;
		DWORD request = size - done > 0x100000 ? 0x100000 : size - done;
		DWORD read = 0;

		memset(&position, 0, sizeof(position));
		position.Offset = (DWORD)offset + done;
		if (!ReadFile((HANDLE)context, (byte *)buffer + done, request, &read, &position) || !read)
			return 0;
		done += read;
	}
	return 1;
}

/* a whole small file into a malloc'd buffer; NULL when it is not there or
larger than LIST_MAXIMUM_BYTES */
static uint8_t *file_load(char const *name, uint32_t *size)
{
	char path[256];
	HANDLE handle;
	DWORD bytes;
	uint8_t *buffer;

	if (snprintf(path, sizeof(path), "%s%s", MCC_SOUND_DIRECTORY, name) >= (int)sizeof(path))
		return NULL;
	handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (handle == INVALID_HANDLE_VALUE)
		return NULL;
	bytes = GetFileSize(handle, NULL);
	if (bytes == INVALID_FILE_SIZE || bytes > LIST_MAXIMUM_BYTES || !(buffer = (uint8_t *)malloc(bytes ? bytes : 1)))
	{
		CloseHandle(handle);
		return NULL;
	}
	if (!file_read(handle, 0, bytes, buffer))
	{
		free(buffer);
		CloseHandle(handle);
		return NULL;
	}
	CloseHandle(handle);
	*size = bytes;
	return buffer;
}

static int source_find(void *context, char const *path, uint32_t *first, uint32_t *count, int *elsewhere)
{
	struct mcc_sound_list_record const *record = mcc_sound_list_find(&mcc_sound_globals.adpcm_list, path);
	char classic[MCC_SOUND_LIST_MAXIMUM_PATH_BYTES];
	int index;

	(void)context;
	*elsewhere = 0;
	/* the maps name the Anniversary sounds (sound\sfx\..., CELT streams of
	sounds_debug.fsb); the ADPCM bank holds the classic ones of the same
	names under sound\old_sfx\..., which MCC plays in its classic mode */
	if (!record && !_strnicmp(path, "sound\\sfx\\", 10) && strlen(path) + 4 < sizeof(classic))
	{
		snprintf(classic, sizeof(classic), "sound\\old_sfx\\%s", path + 10);
		record = mcc_sound_list_find(&mcc_sound_globals.adpcm_list, classic);
	}
	if (record)
	{
		*first = record->first;
		*count = record->streams;
		return 1;
	}
	for (index = 0; index < CELT_LIST_COUNT; index++)
	{
		if (mcc_sound_globals.celt_loaded[index] && mcc_sound_list_find(&mcc_sound_globals.celt_list[index], path))
		{
			*elsewhere = 1;
			break;
		}
	}
	return 0;
}

static int source_stream(void *context, uint32_t index, struct mcc_sound_stream *stream)
{
	struct fsb5_stream const *entry;

	(void)context;
	if (index >= mcc_sound_globals.bank.stream_count)
		return 0;
	entry = &mcc_sound_globals.bank.streams[index];
	if (entry->offset > 0xFFFFFFFFull)
		return 0;
	stream->offset = (uint32_t)entry->offset;
	stream->bytes = entry->bytes;
	stream->samples = entry->samples;
	stream->rate = entry->rate;
	stream->channels = entry->channels;
	stream->name_range = entry->name_range;
	stream->name_permutation = entry->name_permutation;
	stream->name = fsb5_stream_name(&mcc_sound_globals.bank, index);
	return 1;
}

static void source_trace(void *context, char const *line)
{
	(void)context;
	platform_log("mcc sound: %s\n", line);
}

/* ---------- public code */

void mcc_sound_close(void)
{
	int index;

	for (index = 0; index < CELT_LIST_COUNT; index++)
		mcc_sound_list_free(&mcc_sound_globals.celt_list[index]);
	mcc_sound_list_free(&mcc_sound_globals.adpcm_list);
	fsb5_close(&mcc_sound_globals.bank);
	if (mcc_sound_globals.bank_file)
		CloseHandle(mcc_sound_globals.bank_file);
	memset(&mcc_sound_globals, 0, sizeof(mcc_sound_globals));
}

static void reason_set(char *reason, long reason_size, char const *format, char const *detail)
{
	if (reason && reason_size > 0)
		snprintf(reason, (size_t)reason_size, format, detail);
}

boolean mcc_sound_open(
	struct mcc_sound_source *source,
	uint32_t combined_offset,
	uint32_t bank_bytes,
	char *reason,
	long reason_size)
{
	char path[256];
	char detail[96];
	uint8_t *bytes;
	uint32_t size;
	int index;

	unsigned long long started = vita_host_time_us();

	memset(source, 0, sizeof(*source));
	memset(&mcc_sound_globals, 0, sizeof(mcc_sound_globals));
	snprintf(path, sizeof(path), "%s%s", MCC_SOUND_DIRECTORY, MCC_SOUND_ADPCM_BANK);
	mcc_sound_globals.bank_file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (mcc_sound_globals.bank_file == INVALID_HANDLE_VALUE)
	{
		mcc_sound_globals.bank_file = NULL;
		reason_set(reason, reason_size, "%s is not in MCC's sound folder", MCC_SOUND_ADPCM_BANK);
		return FALSE;
	}
	if (!fsb5_open(file_read, mcc_sound_globals.bank_file, bank_bytes, 1, &mcc_sound_globals.bank, detail, sizeof(detail)))
	{
		reason_set(reason, reason_size, "sounds_adpcm.fsb: %s", detail);
		mcc_sound_close();
		return FALSE;
	}
	if (mcc_sound_globals.bank.format != _fsb5_format_ima_adpcm)
	{
		reason_set(reason, reason_size, "sounds_adpcm.fsb: %s", "not IMA ADPCM");
		mcc_sound_close();
		return FALSE;
	}
	bytes = file_load(MCC_SOUND_ADPCM_LIST, &size);
	if (!bytes)
	{
		reason_set(reason, reason_size, "%s is not in MCC's sound folder", MCC_SOUND_ADPCM_LIST);
		mcc_sound_close();
		return FALSE;
	}
	if (!mcc_sound_list_parse(bytes, size, &mcc_sound_globals.adpcm_list, detail, sizeof(detail)))
	{
		free(bytes);
		reason_set(reason, reason_size, "sounds_adpcm.lst.bin: %s", detail);
		mcc_sound_close();
		return FALSE;
	}
	free(bytes);
	for (index = 0; index < CELT_LIST_COUNT; index++)
	{
		bytes = file_load(celt_lists[index], &size);
		if (bytes)
		{
			mcc_sound_globals.celt_loaded[index] = mcc_sound_list_parse(bytes, size, &mcc_sound_globals.celt_list[index], detail, sizeof(detail)) != 0;
			free(bytes);
		}
	}
	{
		char const *trace = getenv("HALO_MCC_SOUND_TRACE");

		mcc_sound_globals.trace = trace && trace[0] && trace[0] != '0';
	}
	mcc_sound_globals.memory_bytes = mcc_sound_globals.bank.stream_count * (unsigned long)sizeof(struct fsb5_stream) +
		mcc_sound_globals.bank.names_bytes + mcc_sound_globals.adpcm_list.count * 16ul + mcc_sound_globals.adpcm_list.path_bytes;
	for (index = 0; index < CELT_LIST_COUNT; index++)
		mcc_sound_globals.memory_bytes += mcc_sound_globals.celt_list[index].count * 16ul + mcc_sound_globals.celt_list[index].path_bytes;
	mcc_sound_globals.opened_at = started;
	mcc_sound_globals.opened = TRUE;
	source->find = source_find;
	source->stream = source_stream;
	source->trace = mcc_sound_globals.trace ? source_trace : NULL;
	source->combined_offset = combined_offset;
	source->bank_bytes = bank_bytes;
	return TRUE;
}

void mcc_sound_log(struct custom_edition_conversion_report const *report)
{
	long total = report->mcc_sounds_mapped + report->mcc_sounds_mismatched + report->mcc_sounds_in_celt_banks + report->mcc_sounds_unlisted;

	error(_error_silent,
		"mcc sound: %ld of the map's %ld sounds play from sounds_adpcm.fsb (%ld permutations, %lu KB of Xbox ADPCM read from the bank as it lies, none decoded); still silent: %ld in the CELT banks (dialogue, music, streams), %ld that no list names, %ld that do not match their streams; the banks' and lists' tables took %lu KB for %lu ms (freed now)",
		(long)report->mcc_sounds_mapped, total, (long)report->mcc_sound_permutations_mapped,
		(unsigned long)(report->mcc_sound_adpcm_bytes / 1024), (long)report->mcc_sounds_in_celt_banks,
		(long)report->mcc_sounds_unlisted, (long)report->mcc_sounds_mismatched,
		(mcc_sound_globals.memory_bytes + 1023) / 1024,
		(unsigned long)((vita_host_time_us() - mcc_sound_globals.opened_at) / 1000));
}
