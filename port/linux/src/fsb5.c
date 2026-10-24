/*
FSB5.C

(port) The FSB5 reader (fsb5.h). Plain C: the file is read through the
function it is given, so that port/vita/tests/fsb5_test.c and its fuzz
target run it on made-up banks with no game file.
*/

#include "fsb5.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	HEADER_BYTES_VERSION_0 = 0x40,
	HEADER_BYTES_VERSION_1 = 0x3C,
	HEADER_READ_BYTES = 0x40,
	MAXIMUM_VERSION = 1,
	EXTRA_TYPE_CHANNELS = 1,
	EXTRA_TYPE_RATE = 2,
	EXTRA_TYPE_LOOP = 3,
	/* the longest run of extra words one stream may have (a bank of 6
	channels of Vorbis has a few) */
	MAXIMUM_EXTRA_WORDS = 64
};

static uint32_t const rate_table[11] = { 4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000 };
static uint8_t const channel_table[4] = { 1, 2, 6, 8 };

/* ---------- private code */

static uint32_t u32_le(uint8_t const *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint64_t u64_le(uint8_t const *bytes)
{
	return (uint64_t)u32_le(bytes) | (uint64_t)u32_le(bytes + 4) << 32;
}

static void error_set(char *error, unsigned long error_size, char const *text)
{
	if (error && error_size)
	{
		snprintf(error, error_size, "%s", text);
	}
}

/* the "_<a>_<b>" a name ends with, into *a and *b (-1 for none) */
static void name_numbers(char const *name, size_t length, int32_t *a, int32_t *b)
{
	size_t at = length;
	int32_t values[2];
	int found;

	*a = -1;
	*b = -1;
	for (found = 1; found >= 0; found--)
	{
		size_t digits = 0;
		int64_t value = 0;

		while (at > 0 && name[at - 1] >= '0' && name[at - 1] <= '9')
		{
			at--;
			digits++;
			if (digits <= 9)
			{
				/* (the digits come last first) */
				int64_t scale = 1;
				size_t index;

				for (index = 1; index < digits; index++)
					scale *= 10;
				value += (int64_t)(name[at] - '0') * scale;
			}
		}
		if (!digits || digits > 9 || at == 0 || name[at - 1] != '_')
		{
			return;
		}
		at--;
		values[found] = (int32_t)value;
	}
	*a = values[0];
	*b = values[1];
}

/* ---------- public code */

void fsb5_close(struct fsb5 *bank)
{
	free(bank->streams);
	free(bank->names);
	memset(bank, 0, sizeof(*bank));
}

char const *fsb5_stream_name(struct fsb5 const *bank, uint32_t index)
{
	uint32_t offset;
	uint32_t at;

	if (!bank->names || index >= bank->stream_count)
		return NULL;
	offset = bank->streams[index].name_offset;
	if (offset >= bank->names_bytes)
		return NULL;
	for (at = offset; at < bank->names_bytes; at++)
	{
		if (!bank->names[at])
			return bank->names + offset;
	}
	return NULL;
}

uint32_t fsb5_ima_bytes(uint32_t samples, unsigned channels)
{
	uint64_t blocks = ((uint64_t)samples + 63) / 64;
	uint64_t bytes = blocks * 36 * channels;

	return channels && bytes <= 0xFFFFFFFFull ? (uint32_t)bytes : 0;
}

int fsb5_celt_frame_check(uint8_t const *frame, uint32_t size, uint32_t *payload_bytes)
{
	uint32_t payload;

	if (size < 8 || frame[0] != 0x17 || frame[1] != 0xC3 || frame[2] != 0x0D || frame[3] != 0xF3)
		return 0;
	payload = u32_le(frame + 4);
	if (payload > size - 8)
		return 0;
	if (payload_bytes)
		*payload_bytes = payload;
	return 1;
}

int fsb5_open(
	fsb5_read_function read,
	void *context,
	uint64_t file_bytes,
	int read_names,
	struct fsb5 *bank,
	char *error,
	unsigned long error_size)
{
	uint8_t header[HEADER_READ_BYTES];
	uint8_t *table = NULL;
	uint8_t *name_table = NULL;
	uint32_t stream_table_bytes, name_table_bytes, data_bytes, format, header_bytes;
	uint64_t total;
	uint32_t count;
	uint32_t position = 0;
	uint32_t index;
	char const *failure = NULL;

	memset(bank, 0, sizeof(*bank));
	if (file_bytes < HEADER_BYTES_VERSION_1 || !read(context, 0, HEADER_BYTES_VERSION_1, header))
	{
		error_set(error, error_size, "too short or unreadable");
		return 0;
	}
	if (memcmp(header, "FSB5", 4))
	{
		error_set(error, error_size, "not an FSB5 bank");
		return 0;
	}
	bank->version = u32_le(header + 4);
	if (bank->version > MAXIMUM_VERSION)
	{
		error_set(error, error_size, "an FSB5 version this reader does not know");
		memset(bank, 0, sizeof(*bank));
		return 0;
	}
	header_bytes = bank->version == 1 ? HEADER_BYTES_VERSION_1 : HEADER_BYTES_VERSION_0;
	count = u32_le(header + 8);
	stream_table_bytes = u32_le(header + 12);
	name_table_bytes = u32_le(header + 16);
	data_bytes = u32_le(header + 20);
	format = u32_le(header + 24);
	total = (uint64_t)header_bytes + stream_table_bytes + name_table_bytes + data_bytes;
	if (count == 0 || count > FSB5_MAXIMUM_STREAMS)
		failure = "no streams, or too many";
	else if (format >= NUMBER_OF_FSB5_FORMATS)
		failure = "a format this reader does not know";
	else if (stream_table_bytes > FSB5_MAXIMUM_TABLE_BYTES || name_table_bytes > FSB5_MAXIMUM_TABLE_BYTES ||
		(uint64_t)count * 8 > stream_table_bytes)
		failure = "the stream table does not fit its size";
	else if (total != file_bytes)
		failure = "the sizes in the header are not the file's";
	if (failure)
	{
		error_set(error, error_size, failure);
		memset(bank, 0, sizeof(*bank));
		return 0;
	}
	bank->format = (enum fsb5_format)format;
	bank->stream_count = count;
	bank->file_bytes = file_bytes;
	bank->data_offset = (uint64_t)header_bytes + stream_table_bytes + name_table_bytes;
	bank->data_bytes = data_bytes;
	bank->streams = (struct fsb5_stream *)calloc(count, sizeof(struct fsb5_stream));
	table = (uint8_t *)malloc(stream_table_bytes);
	if (!bank->streams || !table)
	{
		failure = "out of memory";
		goto fail;
	}
	if (!read(context, header_bytes, stream_table_bytes, table))
	{
		failure = "the stream table could not be read";
		goto fail;
	}

	for (index = 0; index < count; index++)
	{
		struct fsb5_stream *stream = &bank->streams[index];
		uint64_t word;
		uint64_t data_offset;
		int extra;
		int words = 0;

		if (stream_table_bytes - position < 8)
		{
			failure = "the stream table ends inside a stream";
			goto fail;
		}
		word = u64_le(table + position);
		position += 8;
		extra = (int)(word & 1);
		if (((word >> 1) & 0xF) >= 11)
		{
			failure = "a stream has a rate index FMOD does not use";
			goto fail;
		}
		stream->rate = rate_table[(word >> 1) & 0xF];
		stream->channels = channel_table[(word >> 5) & 3];
		data_offset = ((word >> 7) & 0x7FFFFFFull) << 5;
		stream->samples = (uint32_t)(word >> 34);
		if (data_offset > data_bytes)
		{
			failure = "a stream starts past the data";
			goto fail;
		}
		stream->offset = bank->data_offset + data_offset;
		stream->name_range = -1;
		stream->name_permutation = -1;
		stream->name_offset = 0xFFFFFFFFu;
		while (extra)
		{
			uint32_t flag;
			uint32_t type, size;

			if (++words > MAXIMUM_EXTRA_WORDS || stream_table_bytes - position < 4)
			{
				failure = "a stream's extra data is cut off or endless";
				goto fail;
			}
			flag = u32_le(table + position);
			position += 4;
			extra = (int)(flag & 1);
			size = (flag >> 1) & 0xFFFFFF;
			type = flag >> 25;
			if (size > stream_table_bytes - position)
			{
				failure = "a stream's extra data does not fit the table";
				goto fail;
			}
			if (type == EXTRA_TYPE_CHANNELS && size >= 1)
			{
				if (table[position] == 0)
				{
					failure = "a stream has no channels";
					goto fail;
				}
				stream->channels = table[position];
			}
			else if (type == EXTRA_TYPE_RATE && size >= 4)
			{
				uint32_t rate = u32_le(table + position);

				if (rate == 0 || rate > 1000000)
				{
					failure = "a stream has an impossible rate";
					goto fail;
				}
				stream->rate = rate;
			}
			else if (type == EXTRA_TYPE_LOOP && size >= 4)
			{
				stream->has_loop = 1;
				stream->loop_start = u32_le(table + position);
				stream->loop_end = size >= 8 ? u32_le(table + position + 4) : 0;
			}
			position += size;
		}
	}
	/* a stream ends where the next starts */
	for (index = 0; index < count; index++)
	{
		uint64_t end = index + 1 < count ? bank->streams[index + 1].offset : bank->data_offset + data_bytes;

		if (end < bank->streams[index].offset)
		{
			failure = "the streams are not in order";
			goto fail;
		}
		if (end - bank->streams[index].offset > 0xFFFFFFFFull)
		{
			failure = "a stream is larger than 4 GB";
			goto fail;
		}
		bank->streams[index].bytes = (uint32_t)(end - bank->streams[index].offset);
	}

	if (read_names && name_table_bytes)
	{
		if ((uint64_t)count * 4 > name_table_bytes)
		{
			failure = "the name table is smaller than its offsets";
			goto fail;
		}
		name_table = (uint8_t *)malloc(name_table_bytes + 1);
		if (!name_table)
		{
			failure = "out of memory";
			goto fail;
		}
		if (!read(context, (uint64_t)header_bytes + stream_table_bytes, name_table_bytes, name_table))
		{
			failure = "the name table could not be read";
			goto fail;
		}
		name_table[name_table_bytes] = 0;
		bank->names = (char *)name_table;
		bank->names_bytes = name_table_bytes + 1;
		name_table = NULL;
		for (index = 0; index < count; index++)
		{
			uint32_t offset = u32_le((uint8_t const *)bank->names + (size_t)index * 4);
			char const *name;

			if (offset >= name_table_bytes)
			{
				failure = "a name is past the name table";
				goto fail;
			}
			bank->streams[index].name_offset = offset;
			name = fsb5_stream_name(bank, index);
			if (name)
				name_numbers(name, strlen(name), &bank->streams[index].name_range, &bank->streams[index].name_permutation);
		}
	}
	free(table);
	return 1;

fail:
	free(table);
	free(name_table);
	error_set(error, error_size, failure);
	fsb5_close(bank);
	return 0;
}
