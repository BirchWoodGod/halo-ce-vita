/*
POSIX_CE_INSTALLER.C

Halo Custom Edition's three resource maps (bitmaps.map, sounds.map and
loc.map), which the Custom Edition maps take their pictures, sounds and
text from and OpenCE's multiplayer menus read (port/linux/game/menu_tags.c),
written out of the player's own Custom Edition installer copied next to the
game: ce_installer.h.

The installer (halocesetup_en_1.00.exe: Microsoft Games' "AutoRun/Setup")
is a 32-bit Windows program whose resources hold a Microsoft Cabinet
(.rsrc/CABFILE/CAB1.CAB in the English 1.00 one: 117 files in 5 folders,
each LZX-compressed with a 2 MB window, the maps among them). The cabinet is
found by its MSCF signature among the program's resources (a bounded walk of
its resource tree), else by looking through the file for one, so another
language's or version's installer, or the .cab alone, does too. The cabinet
is read by libmspack's decompressor (port/third_party/libmspack, LGPL-2.1:
its README) through a mspack_system of this file's that sees only the
cabinet's part of the installer, streams it (no more than the LZX window,
its input block and two 256 KB file buffers are held, about 2.7 MB), and
writes each map to <maps>/<name>.extract, renamed once it is whole and
checked: its size as the cabinet gives it, the checksum of every block the
cabinet stores one for (libmspack checks them, and stops at a wrong one),
and its resource map header (port/linux/game/cache_file_formats.c's
resource_map_header_identify: the type, the names and index offsets within
the file, the item count fitting).

The installer is anyone's file: every offset the walk reads is checked
against the file and the resource section, the walk visits at most 4096
entries three levels deep, the files are capped in size, and the paths
written are this file's own (the cabinet's names are only compared).

This file is host C (the host's ABI and C library): the Linux build
compiles it as a posix_*.c, the Vita's host side (port/vita/host) with the
SDK's, and tools/ce_installer_extract.c alone. Not on Windows or Android.
*/

#include "ce_installer.h"
/* (the errors in the player's language: lang.c, linked with this file) */
#include "lang.h"

#include <mspack.h>
/* (libmspack's own: the decompressor's position in the folder it is
unpacking, for the progress) */
#include <cab.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

/* the largest installer read (the English 1.00 one is 178 MB) */
#define INSTALLER_MAXIMUM (1024ULL * 1024ULL * 1024ULL)
#define CABINETS_MAXIMUM 8
#define RESOURCE_ENTRIES_MAXIMUM 4096
#define SECTIONS_MAXIMUM 96
#define FILE_BUFFER_SIZE (256 * 1024)
/* progress at most every this many bytes */
#define PROGRESS_STEP (1024ULL * 1024ULL)

static const struct
{
	const char *name;
	/* the resource map header's type (cache_file_formats.h's
	resource_map_type) */
	unsigned long type;
	unsigned long maximum;
} resource_maps[3] = {
	{ "bitmaps.map", 1, 512UL * 1024UL * 1024UL },
	{ "sounds.map", 2, 256UL * 1024UL * 1024UL },
	{ "loc.map", 3, 16UL * 1024UL * 1024UL },
};

/* ---------- files */

static unsigned long read_u16(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8);
}

static unsigned long read_u32(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

/* reads `bytes` at `offset`, all of them within the file's `size` */
static int read_at(FILE *file, unsigned long long size, unsigned long long offset, void *buffer, unsigned long bytes)
{
	if (offset > size || bytes > size - offset)
		return 0;
	if (fseeko(file, (off_t)offset, SEEK_SET) != 0)
		return 0;
	return fread(buffer, 1, bytes, file) == bytes;
}

static int file_size(const char *path, unsigned long long *size)
{
	struct stat status;

	if (stat(path, &status) != 0 || !S_ISREG(status.st_mode))
		return 0;
	*size = (unsigned long long)status.st_size;
	return 1;
}

const char *ce_installer_file_name(int which)
{
	int index;

	for (index = 0; index < 3; index++)
		if (which == 1 << index)
			return resource_maps[index].name;
	return "";
}

int ce_installer_missing(const char *maps_directory)
{
	int index, missing = 0;

	for (index = 0; index < 3; index++)
	{
		char path[1024];
		unsigned long long size;

		snprintf(path, sizeof(path), "%s/%s", maps_directory, resource_maps[index].name);
		if (!file_size(path, &size))
			missing |= 1 << index;
	}
	return missing;
}

int ce_installer_find(const char *directory, char *path, int path_size, unsigned long long *size)
{
	DIR *folder = opendir(directory);
	struct dirent *entry;
	int found = 0;

	while (folder && !found && (entry = readdir(folder)) != NULL)
	{
		size_t length = strlen(entry->d_name);

		if (length < 15 || strncasecmp(entry->d_name, "halocesetup", 11) != 0 ||
			strcasecmp(entry->d_name + length - 4, ".exe") != 0)
			continue;
		snprintf(path, (size_t)path_size, "%s/%s", directory, entry->d_name);
		found = file_size(path, size) && *size > 0;
	}
	if (folder)
		closedir(folder);
	return found;
}

/* ---------- the cabinets in the installer */

/* a cabinet: a part of the installer */
struct window
{
	const char *path;
	unsigned long long base, length;
	struct extraction *extraction;
};

struct section
{
	unsigned long address, virtual_size, raw_offset, raw_size;
};

/* the file offset of `bytes` at the address `rva`, all within one
section's data and the file */
static int rva_offset(const struct section *sections, int count, unsigned long long size, unsigned long rva,
	unsigned long bytes, unsigned long long *offset)
{
	int index;

	for (index = 0; index < count; index++)
	{
		const struct section *section = &sections[index];
		unsigned long within;

		if (rva < section->address)
			continue;
		within = rva - section->address;
		if (within >= section->raw_size || bytes > section->raw_size - within)
			continue;
		*offset = (unsigned long long)section->raw_offset + within;
		return *offset <= size && bytes <= size - *offset;
	}
	return 0;
}

static int cabinet_at(FILE *file, unsigned long long size, unsigned long long offset)
{
	unsigned char signature[8];

	return read_at(file, size, offset, signature, sizeof(signature)) && memcmp(signature, "MSCF\0\0\0\0", 8) == 0;
}

/* the cabinets among a Windows program's resources (any of them that is
one); 0 for a file that is not such a program */
static int resource_cabinets(FILE *file, unsigned long long size, const char *path, struct window *windows,
	int maximum)
{
	unsigned char header[64], signature[24], optional[240], bytes[40];
	struct section sections[SECTIONS_MAXIMUM];
	unsigned long header_offset, section_count, optional_size, directories, directory_count;
	unsigned long resource_rva, resource_size;
	unsigned long long resource_offset;
	unsigned long stack[64], depths[64];
	int stack_count = 0, entries_seen = 0, found = 0, index;

	if (!read_at(file, size, 0, header, sizeof(header)) || header[0] != 'M' || header[1] != 'Z')
		return 0;
	header_offset = read_u32(header + 0x3C);
	if (header_offset < sizeof(header) || header_offset > 1024 * 1024 ||
		!read_at(file, size, header_offset, signature, sizeof(signature)) || memcmp(signature, "PE\0\0", 4) != 0)
		return 0;
	section_count = read_u16(signature + 6);
	optional_size = read_u16(signature + 20);
	if (!section_count || section_count > SECTIONS_MAXIMUM || optional_size < 2 || optional_size > sizeof(optional) ||
		!read_at(file, size, header_offset + sizeof(signature), optional, optional_size))
		return 0;
	/* (PE32's data directories, or PE32+'s) */
	switch (read_u16(optional))
	{
	case 0x10B:
		directories = 96;
		break;
	case 0x20B:
		directories = 112;
		break;
	default:
		return 0;
	}
	if (directories + 3 * 8 > optional_size)
		return 0;
	directory_count = read_u32(optional + directories - 4);
	if (directory_count < 3)
		return 0;
	resource_rva = read_u32(optional + directories + 2 * 8);
	resource_size = read_u32(optional + directories + 2 * 8 + 4);
	for (index = 0; index < (int)section_count; index++)
	{
		if (!read_at(file, size, header_offset + sizeof(signature) + optional_size + (unsigned long)index * 40, bytes, 40))
			return 0;
		sections[index].virtual_size = read_u32(bytes + 8);
		sections[index].address = read_u32(bytes + 12);
		sections[index].raw_size = read_u32(bytes + 16);
		sections[index].raw_offset = read_u32(bytes + 20);
	}
	if (!resource_rva || resource_size < 16 ||
		!rva_offset(sections, (int)section_count, size, resource_rva, 16, &resource_offset))
		return 0;
	/* (the directory's own part of the section: what the offsets in it may
	reach) */
	if (resource_size > size - resource_offset)
		resource_size = (unsigned long)(size - resource_offset);

	/* the tree: types, names, languages; the leaves are the data */
	stack[stack_count] = 0;
	depths[stack_count++] = 0;
	while (stack_count && found < maximum)
	{
		unsigned long directory = stack[--stack_count], depth = depths[stack_count];
		unsigned long entry_count, entry;

		if (directory > resource_size - 16 ||
			!read_at(file, size, resource_offset + directory, bytes, 16))
			continue;
		entry_count = read_u16(bytes + 12) + read_u16(bytes + 14);
		for (entry = 0; entry < entry_count && found < maximum; entry++)
		{
			unsigned long entry_offset = directory + 16 + entry * 8, target;

			if (++entries_seen > RESOURCE_ENTRIES_MAXIMUM)
				return found;
			if (entry_offset > resource_size - 8 || !read_at(file, size, resource_offset + entry_offset, bytes, 8))
				break;
			target = read_u32(bytes + 4);
			if (target & 0x80000000UL)
			{
				if (depth < 2 && stack_count < (int)(sizeof(stack) / sizeof(stack[0])))
				{
					stack[stack_count] = target & 0x7FFFFFFFUL;
					depths[stack_count++] = depth + 1;
				}
			}
			else if (target <= resource_size - 16 && read_at(file, size, resource_offset + target, bytes, 16))
			{
				unsigned long data_rva = read_u32(bytes), data_size = read_u32(bytes + 4);
				unsigned long long data_offset;

				if (data_size >= 36 && rva_offset(sections, (int)section_count, size, data_rva, data_size, &data_offset) &&
					cabinet_at(file, size, data_offset))
				{
					windows[found].path = path;
					windows[found].base = data_offset;
					windows[found++].length = data_size;
				}
			}
		}
	}
	return found;
}

/* the cabinets anywhere in the file (the .cab alone, or an installer whose
resources were not read): a signature, then a header that fits; cut_short
set when one runs past the end of the file (a copy cut short) */
static int scanned_cabinets(FILE *file, unsigned long long size, const char *path, struct window *windows,
	int maximum, int *cut_short)
{
	enum { CHUNK = 64 * 1024, HEADER = 36 };
	unsigned char *buffer = malloc(CHUNK + HEADER);
	unsigned long long offset = 0;
	int found = 0;

	if (!buffer)
		return 0;
	while (offset < size && found < maximum)
	{
		unsigned long bytes = (unsigned long)(size - offset < CHUNK + HEADER ? size - offset : CHUNK + HEADER);
		unsigned long at;
		unsigned long long next = offset + CHUNK;

		if (!read_at(file, size, offset, buffer, bytes))
			break;
		for (at = 0; at + HEADER <= bytes && at < CHUNK && found < maximum; at++)
		{
			unsigned long cabinet_size, files_offset;

			if (buffer[at] != 'M' || memcmp(buffer + at, "MSCF\0\0\0\0", 8) != 0)
				continue;
			cabinet_size = read_u32(buffer + at + 8);
			files_offset = read_u32(buffer + at + 16);
			if (buffer[at + 25] != 1 || cabinet_size < HEADER || files_offset < HEADER || files_offset >= cabinet_size)
				continue;
			if (cabinet_size > size - (offset + at))
			{
				*cut_short = 1;
				continue;
			}
			windows[found].path = path;
			windows[found].base = offset + at;
			windows[found++].length = cabinet_size;
			/* (on after it: a cabinet's own data holds no other) */
			next = offset + at + cabinet_size;
			break;
		}
		offset = next;
	}
	free(buffer);
	return found;
}

/* ---------- libmspack's view of the files */

struct output
{
	const char *path;
	unsigned long long written;
	int failed;
	struct extraction *extraction;
};

struct extraction
{
	struct mscab_decompressor *decompressor;
	ce_installer_progress_proc progress;
	void *context;
	/* the file being written (NULL while the cabinets are read) */
	const char *file;
	/* the work of the files written before it; where its own starts in the
	folder (the folder's bytes before it are unpacked too, and those after
	the file before it in the folder, if that was), and its own */
	unsigned long long done_before, start, work, total, reported;
	int stopped;
};

struct handle
{
	FILE *file;
	char *buffer;
	unsigned long long base, length, position;
	struct output *output;
	struct extraction *extraction;
};

/* the progress, every megabyte; nonzero once it asked to stop */
static int extraction_tick(struct extraction *extraction)
{
	struct mscab_decompressor_p *decompressor = (struct mscab_decompressor_p *)extraction->decompressor;
	unsigned long long position, done;

	if (!extraction->file || extraction->stopped)
		return extraction->stopped;
	position = decompressor && decompressor->d ? decompressor->d->offset : 0;
	done = position > extraction->start ? position - extraction->start : 0;
	if (done > extraction->work)
		done = extraction->work;
	done += extraction->done_before;
	if (done < extraction->reported + PROGRESS_STEP)
		return 0;
	extraction->reported = done;
	if (extraction->progress && extraction->progress(extraction->context, extraction->file, done, extraction->total))
		extraction->stopped = 1;
	return extraction->stopped;
}

static struct mspack_file *system_open(struct mspack_system *self, const char *filename, int mode)
{
	struct handle *handle = calloc(1, sizeof(*handle));

	(void)self;
	if (!handle)
		return NULL;
	if (mode == MSPACK_SYS_OPEN_READ)
	{
		const struct window *window = (const struct window *)filename;

		handle->file = fopen(window->path, "rb");
		handle->base = window->base;
		handle->length = window->length;
		handle->extraction = window->extraction;
	}
	else if (mode == MSPACK_SYS_OPEN_WRITE)
	{
		struct output *output = (struct output *)filename;

		handle->file = fopen(output->path, "wb");
		handle->output = output;
		handle->extraction = output->extraction;
	}
	if (!handle->file)
	{
		free(handle);
		return NULL;
	}
	handle->buffer = malloc(FILE_BUFFER_SIZE);
	if (handle->buffer)
		setvbuf(handle->file, handle->buffer, _IOFBF, FILE_BUFFER_SIZE);
	if (!handle->output && fseeko(handle->file, (off_t)handle->base, SEEK_SET) != 0)
	{
		fclose(handle->file);
		free(handle->buffer);
		free(handle);
		return NULL;
	}
	return (struct mspack_file *)handle;
}

static void system_close(struct mspack_file *file)
{
	struct handle *handle = (struct handle *)file;

	if (!handle)
		return;
	/* (a full memory card shows here, as the buffer is written) */
	if (fclose(handle->file) != 0 && handle->output)
		handle->output->failed = 1;
	free(handle->buffer);
	free(handle);
}

static int system_read(struct mspack_file *file, void *buffer, int bytes)
{
	struct handle *handle = (struct handle *)file;
	size_t got;

	if (bytes < 0 || handle->output)
		return -1;
	if (handle->extraction && extraction_tick(handle->extraction))
		return -1;
	if ((unsigned long long)bytes > handle->length - handle->position)
		bytes = (int)(handle->length - handle->position);
	got = bytes ? fread(buffer, 1, (size_t)bytes, handle->file) : 0;
	handle->position += got;
	return got == (size_t)bytes ? bytes : (ferror(handle->file) ? -1 : (int)got);
}

static int system_write(struct mspack_file *file, void *buffer, int bytes)
{
	struct handle *handle = (struct handle *)file;

	if (bytes < 0 || !handle->output)
		return -1;
	if (handle->extraction && extraction_tick(handle->extraction))
		return -1;
	if (bytes && fwrite(buffer, 1, (size_t)bytes, handle->file) != (size_t)bytes)
	{
		handle->output->failed = 1;
		return -1;
	}
	handle->output->written += (unsigned long long)bytes;
	return bytes;
}

static int system_seek(struct mspack_file *file, off_t offset, int mode)
{
	struct handle *handle = (struct handle *)file;
	long long position;

	if (handle->output)
		return -1;
	switch (mode)
	{
	case MSPACK_SYS_SEEK_START:
		position = (long long)offset;
		break;
	case MSPACK_SYS_SEEK_CUR:
		position = (long long)handle->position + (long long)offset;
		break;
	case MSPACK_SYS_SEEK_END:
		position = (long long)handle->length + (long long)offset;
		break;
	default:
		return -1;
	}
	if (position < 0 || (unsigned long long)position > handle->length ||
		fseeko(handle->file, (off_t)(handle->base + (unsigned long long)position), SEEK_SET) != 0)
		return -1;
	handle->position = (unsigned long long)position;
	return 0;
}

static off_t system_tell(struct mspack_file *file)
{
	struct handle *handle = (struct handle *)file;

	return handle->output ? (off_t)handle->output->written : (off_t)handle->position;
}

static void system_message(struct mspack_file *file, const char *format, ...)
{
	(void)file;
	(void)format;
}

static void *system_alloc(struct mspack_system *self, size_t bytes)
{
	(void)self;
	return malloc(bytes);
}

static void system_free(void *buffer)
{
	free(buffer);
}

static void system_copy(void *source, void *destination, size_t bytes)
{
	memmove(destination, source, bytes);
}

static struct mspack_system file_system = {
	system_open, system_close, system_read, system_write, system_seek, system_tell, system_message,
	system_alloc, system_free, system_copy, NULL,
};

/* ---------- the maps */

/* how well a cabinet file's name names the resource map: 2 for
maps\<name>, 1 for <name> in another folder, else 0 */
static int name_match(const char *cabinet_name, const char *name)
{
	const char *last = cabinet_name, *cursor;

	for (cursor = cabinet_name; *cursor; cursor++)
		if (*cursor == '\\' || *cursor == '/')
			last = cursor + 1;
	if (strcasecmp(last, name) != 0)
		return 0;
	return last - cabinet_name == 5 && strncasecmp(cabinet_name, "maps", 4) == 0 ? 2 : 1;
}

/* the header of a resource map of `type` (cache_file_formats.c's
resource_map_header_identify) in a file of `size` */
static int resource_map_header_valid(const char *path, unsigned long type, unsigned long long size)
{
	unsigned char header[16];
	FILE *file = fopen(path, "rb");
	unsigned long names_offset, index_offset, count;
	int ok;

	if (!file)
		return 0;
	ok = fread(header, 1, sizeof(header), file) == sizeof(header);
	fclose(file);
	if (!ok)
		return 0;
	names_offset = read_u32(header + 4);
	index_offset = read_u32(header + 8);
	count = read_u32(header + 12);
	return read_u32(header) == type && names_offset >= sizeof(header) && index_offset >= names_offset &&
		index_offset <= size && count < 0x80000000UL && count <= (size - index_offset) / 12;
}

struct chosen
{
	struct mscabd_file *file;
	int cabinet, folder, map, score;
};

static int chosen_order(const void *first, const void *second)
{
	const struct chosen *a = first, *b = second;

	if (a->cabinet != b->cabinet)
		return a->cabinet - b->cabinet;
	if (a->folder != b->folder)
		return a->folder - b->folder;
	return a->file->offset < b->file->offset ? -1 : a->file->offset > b->file->offset;
}

static void set_error(char *error, int error_size, const char *format, const char *name)
{
	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, format, name);
}

int ce_installer_extract(const char *installer, const char *maps_directory, int which,
	ce_installer_progress_proc progress, void *context, char *error, int error_size)
{
	struct window windows[CABINETS_MAXIMUM];
	struct mscabd_cabinet *cabinets[CABINETS_MAXIMUM];
	struct chosen chosen[3];
	struct extraction extraction;
	struct mscab_decompressor *decompressor = NULL;
	unsigned long long size;
	FILE *file;
	int window_count, cabinet_count = 0, chosen_count = 0, index, result = CE_INSTALLER_FAILED, cut_short = 0;

	set_error(error, error_size, "%s", "");
	memset(&extraction, 0, sizeof(extraction));
	memset(chosen, 0, sizeof(chosen));
	extraction.progress = progress;
	extraction.context = context;
	which &= CE_INSTALLER_ALL;
	if (!file_size(installer, &size) || !(file = fopen(installer, "rb")))
	{
		set_error(error, error_size, T("%s cannot be read."), installer);
		return CE_INSTALLER_FAILED;
	}
	if (size > INSTALLER_MAXIMUM)
	{
		fclose(file);
		set_error(error, error_size, T("%s is too large for a Halo Custom Edition installer."), installer);
		return CE_INSTALLER_FAILED;
	}
	window_count = resource_cabinets(file, size, installer, windows, CABINETS_MAXIMUM);
	if (!window_count)
		window_count = scanned_cabinets(file, size, installer, windows, CABINETS_MAXIMUM, &cut_short);
	fclose(file);
	if (!window_count)
	{
		if (cut_short)
			set_error(error, error_size, T("%s is cut short (an unfinished copy?): copy it again."), installer);
		else
			set_error(error, error_size, T("%s is not a Halo Custom Edition installer (it holds no cabinet)."), installer);
		return CE_INSTALLER_FAILED;
	}
	decompressor = mspack_create_cab_decompressor(&file_system);
	if (!decompressor)
	{
		set_error(error, error_size, "%s", T("Not enough memory to read the installer."));
		return CE_INSTALLER_FAILED;
	}
	extraction.decompressor = decompressor;

	/* the cabinets' files: the best named of each map wanted */
	for (index = 0; index < window_count; index++)
	{
		struct mscabd_cabinet *cabinet;
		struct mscabd_file *entry;

		windows[index].extraction = &extraction;
		cabinet = decompressor->open(decompressor, (const char *)&windows[index]);
		if (!cabinet)
			continue;
		cabinets[cabinet_count++] = cabinet;
		for (entry = cabinet->files; entry; entry = entry->next)
		{
			int map;

			for (map = 0; map < 3; map++)
			{
				int score = (which & (1 << map)) ? name_match(entry->filename, resource_maps[map].name) : 0;
				int slot;

				if (!score || entry->length > resource_maps[map].maximum || entry->length < 16)
					continue;
				for (slot = 0; slot < chosen_count && chosen[slot].map != map; slot++)
					;
				if (slot < chosen_count && chosen[slot].score >= score)
					continue;
				if (slot == chosen_count)
					chosen_count++;
				chosen[slot].file = entry;
				chosen[slot].cabinet = cabinet_count - 1;
				chosen[slot].map = map;
				chosen[slot].score = score;
				{
					struct mscabd_folder *folder;
					int folder_index = 0;

					for (folder = cabinet->folders; folder && folder != entry->folder; folder = folder->next)
						folder_index++;
					chosen[slot].folder = folder_index;
				}
			}
		}
	}
	for (index = 0; index < 3; index++)
	{
		int slot;

		for (slot = 0; slot < chosen_count && chosen[slot].map != index; slot++)
			;
		if ((which & (1 << index)) && slot == chosen_count)
		{
			if (!cabinet_count)
				set_error(error, error_size, "%s", T("The installer's cabinet is damaged."));
			else
				set_error(error, error_size, T("The installer holds no %s."), resource_maps[index].name);
			goto done;
		}
	}

	/* in the cabinets' order, so a folder is unpacked once; the work: each
	file's folder unpacked from where the file before it in that folder
	ended (or the folder's start) to its end */
	qsort(chosen, (size_t)chosen_count, sizeof(chosen[0]), chosen_order);
	for (index = 0; index < chosen_count; index++)
	{
		unsigned long long start = index && chosen[index - 1].cabinet == chosen[index].cabinet &&
			chosen[index - 1].folder == chosen[index].folder ?
			(unsigned long long)chosen[index - 1].file->offset + chosen[index - 1].file->length : 0;

		extraction.total += (unsigned long long)chosen[index].file->offset + chosen[index].file->length - start;
	}
	for (index = 0; index < chosen_count; index++)
	{
		const struct chosen *map = &chosen[index];
		const char *name = resource_maps[map->map].name;
		char partial[1024], path[1024];
		struct output output;
		unsigned long long start = index && chosen[index - 1].cabinet == map->cabinet &&
			chosen[index - 1].folder == map->folder ?
			(unsigned long long)chosen[index - 1].file->offset + chosen[index - 1].file->length : 0;
		unsigned long long end = (unsigned long long)map->file->offset + map->file->length;
		unsigned long long existing;
		int status;

		snprintf(partial, sizeof(partial), "%s/%s.extract", maps_directory, name);
		snprintf(path, sizeof(path), "%s/%s", maps_directory, name);
		memset(&output, 0, sizeof(output));
		output.path = partial;
		output.extraction = &extraction;
		extraction.file = name;
		extraction.start = start;
		extraction.work = end - start;
		status = decompressor->extract(decompressor, map->file, (const char *)&output);
		extraction.file = NULL;
		extraction.done_before += end - start;
		if (status != MSPACK_ERR_OK || output.failed || extraction.stopped)
		{
			remove(partial);
			if (extraction.stopped)
			{
				set_error(error, error_size, "%s", T("Stopped."));
				result = CE_INSTALLER_STOPPED;
			}
			else if (output.failed || status == MSPACK_ERR_WRITE || status == MSPACK_ERR_OPEN)
				set_error(error, error_size, T("%s could not be written: is the memory card full?"), name);
			else if (status == MSPACK_ERR_CHECKSUM)
				set_error(error, error_size, T("%s is damaged in the installer (a checksum is wrong)."), name);
			else if (status == MSPACK_ERR_READ || status == MSPACK_ERR_SEEK)
				set_error(error, error_size, T("The installer could not be read (at %s)."), name);
			else if (status == MSPACK_ERR_NOMEMORY)
				set_error(error, error_size, T("Not enough memory to unpack %s."), name);
			else
				set_error(error, error_size, T("%s is damaged in the installer (it does not unpack)."), name);
			goto done;
		}
		if (output.written != map->file->length ||
			!resource_map_header_valid(partial, resource_maps[map->map].type, output.written))
		{
			remove(partial);
			set_error(error, error_size, T("%s from the installer is not a Halo Custom Edition resource map."), name);
			goto done;
		}
		/* (one copied in meanwhile stays) */
		if (file_size(path, &existing))
			remove(partial);
		else if (rename(partial, path) != 0)
		{
			remove(partial);
			set_error(error, error_size, T("%s could not be renamed into place."), name);
			goto done;
		}
	}
	if (progress && !extraction.stopped)
		progress(context, "", extraction.total, extraction.total);
	result = CE_INSTALLER_DONE;

done:
	for (index = 0; index < cabinet_count; index++)
		decompressor->close(decompressor, cabinets[index]);
	mspack_destroy_cab_decompressor(decompressor);
	return result;
}
