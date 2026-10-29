/*
MCC_SOUND_AUDIT.C

What the FSB5 reader and the list parser make of the player's own MCC sound
folder (halo1\sound\pc): every bank's header and stream table, every list,
and how they agree (run_mcc_sound_test.sh, "audit"). Reads only; prints
counts, writes nothing.

  mcc_sound_audit <the sound\pc folder>

Exits 1 when a list or bank is refused, a list names a stream the bank does
not have, or the ADPCM bank's streams are not covered once, in order, by its
list with names that agree.
*/

#include "fsb5.h"
#include "mcc_sound_list.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(condition, ...) do { if (!(condition)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

static int file_read(void *context, uint64_t offset, uint32_t size, void *buffer)
{
	FILE *file = (FILE *)context;

	return !fseeko(file, (off_t)offset, SEEK_SET) && fread(buffer, 1, size, file) == size;
}

static uint8_t *load(char const *path, uint32_t *size)
{
	FILE *file = fopen(path, "rb");
	uint8_t *bytes;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	bytes = (uint8_t *)malloc((size_t)length + 1);
	if (bytes && fread(bytes, 1, (size_t)length, file) != (size_t)length)
	{
		free(bytes);
		bytes = NULL;
	}
	fclose(file);
	*size = (uint32_t)length;
	return bytes;
}

static char const *const format_names[] = { "none", "pcm8", "pcm16", "pcm24", "pcm32", "float", "gcadpcm", "ima", "vag", "hevag", "xma", "mpeg", "celt", "at9", "xwma", "vorbis", "fadpcm", "opus" };

int main(int argc, char **argv)
{
	static char const *const banks[][2] = {
		{ "sounds_adpcm", "adpcm" }, { "sounds_debug", "debug" }, { "sounds_stream", "stream" }, { "sounds_music", "music" },
		{ "sounds_dialogs", "dialogs" }, { "sounds_dialogs_french", "dialogs_french" }, { "sounds_dialogs_german", "dialogs_german" },
		{ "sounds_dialogs_italian", "dialogs_italian" }, { "sounds_dialogs_spanish", "dialogs_spanish" }
	};
	char path[512];
	size_t index;

	if (argc != 2)
	{
		printf("usage: %s <halo1/sound/pc>\n", argv[0]);
		return 2;
	}
	for (index = 0; index < sizeof(banks) / sizeof(banks[0]); index++)
	{
		struct fsb5 bank;
		struct mcc_sound_list list;
		char error[128];
		FILE *file;
		uint8_t *bytes;
		uint32_t size = 0;
		uint32_t record_index;
		uint32_t covered = 0, gaps = 0, name_mismatch = 0, bad_range = 0, mono = 0, stereo = 0, rate22 = 0, rate44 = 0, other_rate = 0;
		uint64_t stream_bytes = 0;
		uint32_t celt_ok = 0;
		uint32_t expected = 0;
		uint32_t i;

		snprintf(path, sizeof(path), "%s/%s.fsb", argv[1], banks[index][0]);
		file = fopen(path, "rb");
		if (!file)
		{
			printf("%s: not there\n", banks[index][0]);
			continue;
		}
		fseeko(file, 0, SEEK_END);
		if (!fsb5_open(file_read, file, (uint64_t)ftello(file), 1, &bank, error, sizeof(error)))
		{
			CHECK(0, "%s.fsb: %s", banks[index][0], error);
			fclose(file);
			continue;
		}
		snprintf(path, sizeof(path), "%s/lst/%s.lst.bin", argv[1], banks[index][0]);
		bytes = load(path, &size);
		CHECK(bytes != NULL, "%s: not there", path);
		if (!bytes || !mcc_sound_list_parse(bytes, size, &list, error, sizeof(error)))
		{
			CHECK(bytes == NULL, "%s: %s", path, error);
			free(bytes);
			fsb5_close(&bank);
			fclose(file);
			continue;
		}
		free(bytes);
		for (i = 0; i < bank.stream_count; i++)
		{
			struct fsb5_stream const *stream = &bank.streams[i];

			stream_bytes += stream->bytes;
			if (stream->channels == 1) mono++; else stereo++;
			if (stream->rate == 22050) rate22++; else if (stream->rate == 44100) rate44++; else other_rate++;
		}
		for (record_index = 0; record_index < list.count; record_index++)
		{
			struct mcc_sound_list_record const *record = &list.records[record_index];
			uint32_t k;

			if ((uint64_t)record->first + record->streams > bank.stream_count)
			{
				bad_range++;
				continue;
			}
			if (record->first != expected)
				gaps++;
			expected = record->first + record->streams;
			covered += record->streams;
			for (k = 0; k < record->streams; k++)
			{
				struct fsb5_stream const *stream = &bank.streams[record->first + k];

				if (stream->name_permutation != (int32_t)k)
					name_mismatch++;
			}
		}
		if (bank.format == _fsb5_format_celt && bank.stream_count)
		{
			/* the first frame of every stream: FMOD's CELT header */
			for (i = 0; i < bank.stream_count; i++)
			{
				uint8_t head[16];

				if (bank.streams[i].bytes >= sizeof(head) && file_read(file, bank.streams[i].offset, sizeof(head), head) &&
					fsb5_celt_frame_check(head, bank.streams[i].bytes, NULL))
					celt_ok++;
			}
		}
		printf("%-24s %-5s %5lu streams (%lu mono, %lu stereo; %lu at 22050, %lu at 44100, %lu other), %6.1f MB; list %4lu sounds covering %5lu streams, %lu gaps, %lu bad ranges, %lu streams whose name's number is not the permutation's%s\n",
			banks[index][0], format_names[bank.format], (unsigned long)bank.stream_count, (unsigned long)mono, (unsigned long)stereo,
			(unsigned long)rate22, (unsigned long)rate44, (unsigned long)other_rate, (double)stream_bytes / 1048576.0,
			(unsigned long)list.count, (unsigned long)covered, (unsigned long)gaps, (unsigned long)bad_range, (unsigned long)name_mismatch, "");
		if (bank.format == _fsb5_format_celt)
			printf("%-24s       every stream starts with FMOD's CELT frame header: %lu of %lu\n", "", (unsigned long)celt_ok, (unsigned long)bank.stream_count);
		CHECK(bad_range == 0, "%s: a sound names streams the bank does not have", banks[index][0]);
		if (!strcmp(banks[index][1], "adpcm"))
		{
			CHECK(bank.format == _fsb5_format_ima_adpcm, "the ADPCM bank is not IMA ADPCM");
			CHECK(covered == bank.stream_count && gaps == 0, "the ADPCM bank's streams are not covered once and in order");
			CHECK(name_mismatch == 0, "the ADPCM bank's names disagree with its list");
			CHECK(other_rate == 0, "the ADPCM bank has rates other than 22050 and 44100");
		}
		mcc_sound_list_free(&list);
		fsb5_close(&bank);
		fclose(file);
	}
	printf(failures ? "mcc_sound_audit: %d failures\n" : "mcc_sound_audit: ok\n", failures);
	return failures ? 1 : 0;
}
