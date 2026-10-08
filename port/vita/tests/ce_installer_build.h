/*
CE_INSTALLER_BUILD.H

Small made-up Custom Edition installers for ce_installer_test.c and
ce_installer_fuzz.c (port/linux/src/posix_ce_installer.c): Microsoft
Cabinets whose folders are stored (no compression) or LZX with an
uncompressed block, the maps in them resource maps of a few bytes with the
right header, wrapped in a 32-bit Windows program whose resource tree holds
the cabinet as .rsrc/CABFILE/CAB1.CAB does. No Microsoft data: everything is
made here.
*/

#ifndef CE_INSTALLER_BUILD_H
#define CE_INSTALLER_BUILD_H

#include <stdlib.h>
#include <string.h>

struct build_buffer
{
	unsigned char *data;
	unsigned long size, capacity;
};

static void build_reserve(struct build_buffer *buffer, unsigned long bytes)
{
	if (buffer->size + bytes > buffer->capacity)
	{
		buffer->capacity = (buffer->size + bytes) * 2 + 256;
		buffer->data = realloc(buffer->data, buffer->capacity);
		if (!buffer->data)
			abort();
	}
}

static void build_bytes(struct build_buffer *buffer, const void *bytes, unsigned long count)
{
	if (!count)
		return;
	build_reserve(buffer, count);
	memcpy(buffer->data + buffer->size, bytes, count);
	buffer->size += count;
}

static void build_zeros(struct build_buffer *buffer, unsigned long count)
{
	build_reserve(buffer, count);
	memset(buffer->data + buffer->size, 0, count);
	buffer->size += count;
}

static void put16(unsigned char *at, unsigned long value)
{
	at[0] = (unsigned char)value;
	at[1] = (unsigned char)(value >> 8);
}

static void put32(unsigned char *at, unsigned long value)
{
	put16(at, value & 0xFFFF);
	put16(at + 2, value >> 16);
}

static void build_u16(struct build_buffer *buffer, unsigned long value)
{
	unsigned char bytes[2];

	put16(bytes, value);
	build_bytes(buffer, bytes, 2);
}

static void build_u32(struct build_buffer *buffer, unsigned long value)
{
	unsigned char bytes[4];

	put32(bytes, value);
	build_bytes(buffer, bytes, 4);
}

/* a resource map of `type` (1 bitmaps, 2 sounds, 3 loc) of `size` bytes:
its header (no items: the names and index at the end), then bytes made
from the seed */
static unsigned char *build_resource_map(unsigned long type, unsigned long size, unsigned long seed)
{
	unsigned char *map = malloc(size);
	unsigned long index;

	if (!map)
		abort();
	for (index = 0; index < size; index++)
		map[index] = (unsigned char)((index * 131 + seed * 7 + (index >> 8)) ^ seed);
	put32(map, type);
	put32(map + 4, size);
	put32(map + 8, size);
	put32(map + 12, 0);
	return map;
}

/* the cabinet's checksum of a block (cabd.c's cabd_checksum) */
static unsigned long build_checksum(const unsigned char *data, unsigned long bytes, unsigned long checksum)
{
	unsigned long words = bytes >> 2, index, value = 0;

	for (index = 0; index < words; index++, data += 4)
		checksum ^= (unsigned long)data[0] | ((unsigned long)data[1] << 8) | ((unsigned long)data[2] << 16) |
			((unsigned long)data[3] << 24);
	switch (bytes & 3)
	{
	case 3: value |= (unsigned long)*data++ << 16; /* fall through */
	case 2: value |= (unsigned long)*data++ << 8; /* fall through */
	case 1: value |= *data;
	}
	return (checksum ^ value) & 0xFFFFFFFFUL;
}

struct build_file
{
	const char *name;
	const unsigned char *data;
	unsigned long size;
	/* its folder (0 or 1) */
	int folder;
};

enum
{
	BUILD_STORED,
	/* LZX (a 2 MB window) with one uncompressed block: the folder is at most
	32 KB */
	BUILD_LZX,
};

/* an LZX stream of one uncompressed block holding the bytes: the stream's
E8 flag off, the block's type and length, its alignment, R0-R2, the bytes */
static void build_lzx_stream(struct build_buffer *stream, const unsigned char *data, unsigned long size)
{
	unsigned long bits = (3UL << 28) | (size << 4);

	build_u16(stream, bits >> 16);
	build_u16(stream, bits & 0xFFFF);
	build_u32(stream, 1);
	build_u32(stream, 1);
	build_u32(stream, 1);
	build_bytes(stream, data, size);
	if (size & 1)
		build_zeros(stream, 1);
}

/* a folder's blocks: stored 32 KB at a time, or one LZX frame holding an
uncompressed block */
static void build_folder_blocks(struct build_buffer *blocks, const unsigned char *data, unsigned long size,
	int compression, int checksums, unsigned long *block_count)
{
	unsigned long offset = 0;

	*block_count = 0;
	if (compression == BUILD_LZX)
	{
		struct build_buffer frame = { 0 };
		unsigned char header[8];

		build_lzx_stream(&frame, data, size);
		put32(header, 0);
		put16(header + 4, frame.size);
		put16(header + 6, size);
		if (checksums)
			put32(header, build_checksum(header + 4, 4, build_checksum(frame.data, frame.size, 0)));
		build_bytes(blocks, header, 8);
		build_bytes(blocks, frame.data, frame.size);
		free(frame.data);
		*block_count = 1;
		return;
	}
	while (offset < size || !*block_count)
	{
		unsigned long bytes = size - offset > 32768 ? 32768 : size - offset;
		unsigned char header[8];

		put32(header, 0);
		put16(header + 4, bytes);
		put16(header + 6, bytes);
		if (checksums)
			put32(header, build_checksum(header + 4, 4, build_checksum(data + offset, bytes, 0)));
		build_bytes(blocks, header, 8);
		build_bytes(blocks, data + offset, bytes);
		offset += bytes;
		++*block_count;
	}
}

/* a cabinet of the files, in two folders at most (folder 0 with
`compression`, folder 1 stored) */
static struct build_buffer build_cabinet(const struct build_file *files, int count, int compression, int checksums)
{
	struct build_buffer cabinet = { 0 }, names = { 0 }, blocks[2] = { { 0 }, { 0 } };
	struct build_buffer contents[2] = { { 0 }, { 0 } };
	unsigned long offsets[64], block_counts[2] = { 0, 0 }, header_size, folders_offset, files_offset, data_offset;
	int index, folder_count = 1;

	for (index = 0; index < count; index++)
	{
		int folder = files[index].folder ? 1 : 0;

		if (folder)
			folder_count = 2;
		offsets[index] = contents[folder].size;
		build_bytes(&contents[folder], files[index].data, files[index].size);
	}
	for (index = 0; index < folder_count; index++)
		build_folder_blocks(&blocks[index], contents[index].data, contents[index].size,
			index ? BUILD_STORED : compression, checksums, &block_counts[index]);
	for (index = 0; index < count; index++)
		build_bytes(&names, files[index].name, strlen(files[index].name) + 1);
	header_size = 36;
	folders_offset = header_size;
	files_offset = folders_offset + 8UL * folder_count;
	data_offset = files_offset + 16UL * count + names.size;

	build_bytes(&cabinet, "MSCF", 4);
	build_u32(&cabinet, 0);
	build_u32(&cabinet, data_offset + blocks[0].size + blocks[1].size);
	build_u32(&cabinet, 0);
	build_u32(&cabinet, files_offset);
	build_u32(&cabinet, 0);
	cabinet.data[cabinet.size++] = 3;
	cabinet.data[cabinet.size++] = 1;
	build_u16(&cabinet, folder_count);
	build_u16(&cabinet, count);
	build_u16(&cabinet, 0);
	build_u16(&cabinet, 0x1234);
	build_u16(&cabinet, 0);
	for (index = 0; index < folder_count; index++)
	{
		build_u32(&cabinet, data_offset + (index ? blocks[0].size : 0));
		build_u16(&cabinet, block_counts[index]);
		build_u16(&cabinet, index == 0 && compression == BUILD_LZX ? 0x1503 : 0);
	}
	for (index = 0; index < count; index++)
	{
		build_u32(&cabinet, files[index].size);
		build_u32(&cabinet, offsets[index]);
		build_u16(&cabinet, files[index].folder ? 1 : 0);
		build_u16(&cabinet, 0x3091);
		build_u16(&cabinet, 0x7800);
		build_u16(&cabinet, 0x20);
		build_bytes(&cabinet, files[index].name, strlen(files[index].name) + 1);
	}
	build_bytes(&cabinet, blocks[0].data, blocks[0].size);
	build_bytes(&cabinet, blocks[1].data, blocks[1].size);
	free(names.data);
	free(blocks[0].data);
	free(blocks[1].data);
	free(contents[0].data);
	free(contents[1].data);
	return cabinet;
}

/* a 32-bit Windows program (PE32, one .rsrc section) whose resources hold
`payload` as CABFILE/CAB1.CAB/0 - or, with `resource_payload` 0, a small
icon there and the payload after the section */
static struct build_buffer build_installer(const unsigned char *payload, unsigned long payload_size,
	int resource_payload)
{
	struct build_buffer program = { 0 }, section = { 0 };
	static const unsigned char icon[48] = { 'n', 'o', 't', ' ', 'a', ' ', 'c', 'a', 'b' };
	const unsigned long section_rva = 0x1000, section_offset = 0x200;
	unsigned long data_offset;

	/* the tree: root (one named type) -> CABFILE (one named entry) ->
	CAB1.CAB (one language) -> the data entry, then the names, the data */
	build_zeros(&section, 16);
	put16(section.data + 12, 1);
	build_u32(&section, 0x80000000UL | 0x60);
	build_u32(&section, 0x80000000UL | 0x18);
	build_zeros(&section, 16);
	put16(section.data + 0x18 + 12, 1);
	build_u32(&section, 0x80000000UL | 0x70);
	build_u32(&section, 0x80000000UL | 0x30);
	build_zeros(&section, 16);
	put16(section.data + 0x30 + 14, 1);
	build_u32(&section, 1033);
	build_u32(&section, 0x48);
	build_zeros(&section, 0x48 - section.size);
	data_offset = 0x90;
	build_u32(&section, section_rva + data_offset);
	build_u32(&section, resource_payload ? payload_size : sizeof(icon));
	build_u32(&section, 0);
	build_u32(&section, 0);
	/* (the names: UTF-16, counted) */
	build_zeros(&section, 0x60 - section.size);
	build_u16(&section, 7);
	build_bytes(&section, "C\0A\0B\0F\0I\0L\0E\0", 14);
	build_zeros(&section, 0x70 - section.size);
	build_u16(&section, 8);
	build_bytes(&section, "C\0A\0B\0" "1\0.\0C\0A\0B\0", 16);
	build_zeros(&section, data_offset - section.size);
	if (resource_payload)
		build_bytes(&section, payload, payload_size);
	else
		build_bytes(&section, icon, sizeof(icon));
	build_zeros(&section, (0x200 - (section.size & 0x1FF)) & 0x1FF);

	build_zeros(&program, section_offset);
	program.data[0] = 'M';
	program.data[1] = 'Z';
	put32(program.data + 0x3C, 0x40);
	memcpy(program.data + 0x40, "PE\0\0", 4);
	put16(program.data + 0x44, 0x14C);
	put16(program.data + 0x46, 1);
	put16(program.data + 0x54, 224);
	put16(program.data + 0x56, 0x10F);
	put16(program.data + 0x58, 0x10B);
	put32(program.data + 0x58 + 92, 16);
	put32(program.data + 0x58 + 96 + 16, section_rva);
	put32(program.data + 0x58 + 96 + 20, section.size);
	memcpy(program.data + 0x58 + 224, ".rsrc", 5);
	put32(program.data + 0x58 + 224 + 8, section.size);
	put32(program.data + 0x58 + 224 + 12, section_rva);
	put32(program.data + 0x58 + 224 + 16, section.size);
	put32(program.data + 0x58 + 224 + 20, section_offset);
	build_bytes(&program, section.data, section.size);
	if (!resource_payload)
	{
		build_zeros(&program, 333);
		build_bytes(&program, payload, payload_size);
	}
	free(section.data);
	return program;
}

#endif
