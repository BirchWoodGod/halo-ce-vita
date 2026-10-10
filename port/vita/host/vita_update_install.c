/*
VITA_UPDATE_INSTALL.C

The Vita's updater's download, checks, unpacking and install
(vita_update_install.h), with the C library's files (newlib's on the Vita,
whose paths are "ux0:...") and posix_https_download, so the same code runs
on Linux under tests/update_install_test.c.

How the game replaces itself: as VitaDB Downloader updates itself (its
files written into ux0:app/VITADBDLD, then sceAppMgrLoadExec of
app0:eboot.bin), not through the system's promoter. A homebrew's folder
under ux0:app is plain files, and the running game's eboot.bin is already
in memory; the promoter (scePromoterUtilityPromotePkg, which VitaShell uses)
would need a second, separate updater app installed beside the game to run
while the game is closed (VitaShell's VSUPDATER), a head.bin made with the
package HMAC, and it would replace the whole folder. Here only the VPK's
files are replaced; the system's own files of the install
(sce_sys/package/, sce_pfs/) are never written; the title's registration
is the one VitaShell made, with the same title ID (checked against the
VPK's param.sfo). Unsafe homebrew is needed either way.

Every file is unpacked into <data>/update/stage first; the game's files are
touched only once everything is there and checked, and then each move is in
the journal (<data>/update/journal.txt, written whole before the first
move), so a failure, a crash or a power cut is undone (here, or at the next
start: update_install_recover).
*/

#include "vita_update_install.h"
#include "vita_update_verify.h"
#include "posix.h"

#include "monocypher-ed25519.h"
#ifndef Z_PREFIX
#define Z_PREFIX
#endif
#include "zlib.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define UPDATE_FOLDER "update"
#define FILE_MANIFEST_NEW "halo-update.new"
#define FILE_SIGNATURE_NEW "halo-update.sig.new"
#define FILE_MANIFEST "halo-update.txt"
#define FILE_VPK "halo.vpk.part"
#define FOLDER_STAGE "stage"
#define FOLDER_BACKUP "backup"
#define FILE_JOURNAL "journal.txt"
#define JOURNAL_MAGIC "halo-ce-vita-install 1"
#define PROBE_FILE ".halo_update_probe"

/* a VPK's files at most, their sizes in all, one name (update_entry_name_safe) */
#define ENTRY_MAXIMUM 512
#define UNPACKED_MAXIMUM (256UL * 1024 * 1024)
#define NAME_MAXIMUM 200
#define PATH_SIZE 768
#define DOWNLOAD_ATTEMPTS 3
/* room left on the card beyond what the update takes */
#define SPACE_MARGIN (16ULL * 1024 * 1024)

#ifdef UPDATE_INSTALL_TEST_HOOKS
/* (tests only, never a game build: a power cut after this many moves,
update_install_harness.c) */
int update_install_test_stop_after = -1;
#endif

struct staged_file
{
	/* (a folder's trailing '/' read in, then dropped) */
	char name[NAME_MAXIMUM + 2];
	int directory;
	unsigned long size;
	unsigned char sha512[64];
};

struct update_install_staged
{
	char version[64];
	int count;
	struct staged_file files[ENTRY_MAXIMUM];
};

/* ---------- paths and logging */

static void log_line(const struct update_install_config *config, const char *format, ...)
	__attribute__((format(printf, 2, 3)));

static void log_line(const struct update_install_config *config, const char *format, ...)
{
	char line[600];
	va_list arguments;

	if (!config->log)
		return;
	memcpy(line, "update: ", 8);
	va_start(arguments, format);
	vsnprintf(line + 8, sizeof(line) - 8, format, arguments);
	va_end(arguments);
	config->log(line);
}

/* <data>/update[/name]; a path that would not fit is left empty, which no
file operation takes (the folders' and names' bounds keep them well
short of PATH_SIZE) */
static void update_path(const struct update_install_config *config, char *path, const char *name)
{
	int length = name ? snprintf(path, PATH_SIZE, "%s/" UPDATE_FOLDER "/%s", config->data, name) :
		snprintf(path, PATH_SIZE, "%s/" UPDATE_FOLDER, config->data);

	if (length <= 0 || length >= PATH_SIZE)
		path[0] = 0;
}

/* <base>/<relative> (the same) */
static void join_path(char *path, const char *base, const char *relative)
{
	int length = snprintf(path, PATH_SIZE, "%s/%s", base, relative);

	if (length <= 0 || length >= PATH_SIZE)
		path[0] = 0;
}

static int is_directory(const char *path)
{
	struct stat information;

	return stat(path, &information) == 0 && S_ISDIR(information.st_mode);
}

static int is_file(const char *path)
{
	struct stat information;

	return stat(path, &information) == 0 && S_ISREG(information.st_mode);
}

static int make_directory(const char *path)
{
	return mkdir(path, 0777) == 0 || is_directory(path);
}

/* the folders above relative (a/b/c: base/a, base/a/b) made: 1 */
static int make_parents(const char *base, const char *relative)
{
	char path[PATH_SIZE];
	const char *slash;

	for (slash = strchr(relative, '/'); slash; slash = strchr(slash + 1, '/'))
	{
		int length = snprintf(path, sizeof(path), "%s/%.*s", base, (int)(slash - relative), relative);

		if (length <= 0 || length >= (int)sizeof(path) || !make_directory(path))
			return 0;
	}
	return 1;
}

/* a folder of the updater's (under <data>/update/, nothing else) and all in
it removed */
static void remove_tree_inner(const char *path, int depth)
{
	DIR *directory;
	struct dirent *entry;

	if (depth > 16)
		return;
	directory = opendir(path);
	if (!directory)
	{
		remove(path);
		return;
	}
	while ((entry = readdir(directory)) != NULL)
	{
		char child[PATH_SIZE];

		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		join_path(child, path, entry->d_name);
		if (is_directory(child))
			remove_tree_inner(child, depth + 1);
		else
			remove(child);
	}
	closedir(directory);
	rmdir(path);
}

static void remove_tree(const struct update_install_config *config, const char *path)
{
	char root[PATH_SIZE];
	size_t length;

	update_path(config, root, NULL);
	length = strlen(root);
	/* (a guard: never anything but the updater's own folder's contents) */
	if (strncmp(path, root, length) || path[length] != '/' || strstr(path, ".."))
		return;
	remove_tree_inner(path, 0);
}

/* ---------- files */

static char *read_small_file(const char *path, size_t maximum, size_t *size)
{
	FILE *file = fopen(path, "rb");
	char *text;

	*size = 0;
	if (!file)
		return NULL;
	text = malloc(maximum + 1);
	if (text)
	{
		*size = fread(text, 1, maximum + 1, file);
		if (ferror(file) || *size > maximum)
		{
			free(text);
			text = NULL;
			*size = 0;
		}
		else
			text[*size] = 0;
	}
	fclose(file);
	return text;
}

static int write_small_file(const char *path, const char *text, size_t size)
{
	FILE *file = fopen(path, "wb");
	int ok;

	if (!file)
		return 0;
	ok = fwrite(text, 1, size, file) == size;
	ok = fclose(file) == 0 && ok;
	return ok;
}

/* src's bytes into dst (written anew): 1 */
static int copy_file(const char *source, const char *destination)
{
	unsigned char buffer[16384];
	FILE *in = fopen(source, "rb"), *out;
	size_t count;
	int ok = 1;

	if (!in)
		return 0;
	out = fopen(destination, "wb");
	if (!out)
	{
		fclose(in);
		return 0;
	}
	while ((count = fread(buffer, 1, sizeof(buffer), in)) > 0)
		if (fwrite(buffer, 1, count, out) != count)
		{
			ok = 0;
			break;
		}
	if (ferror(in))
		ok = 0;
	fclose(in);
	if (fclose(out) != 0)
		ok = 0;
	return ok;
}

/* a file moved: renamed where it can be (the same memory card), else copied
over the destination and the source removed if it can be (the running
game's eboot.bin may refuse to go: the copy stands in) */
static int move_file(const char *source, const char *destination)
{
	if (rename(source, destination) == 0)
		return 1;
	if (!copy_file(source, destination))
		return 0;
	remove(source);
	return 1;
}

/* the SHA-512 of a file (and its size): 1 */
static int hash_file(const char *path, unsigned char hash[64], unsigned long *size,
	update_install_progress_proc progress, void *context, unsigned long total, int *cancelled)
{
	unsigned char buffer[16384];
	crypto_sha512_ctx sha;
	FILE *file = fopen(path, "rb");
	unsigned long done = 0;
	size_t count;

	if (!file)
		return 0;
	crypto_sha512_init(&sha);
	while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0)
	{
		crypto_sha512_update(&sha, buffer, count);
		done += (unsigned long)count;
		if (progress && progress(context, UPDATE_STAGE_VERIFY, done, total))
		{
			*cancelled = 1;
			fclose(file);
			return 0;
		}
	}
	if (ferror(file))
	{
		fclose(file);
		return 0;
	}
	fclose(file);
	crypto_sha512_final(&sha, hash);
	*size = done;
	return 1;
}

/* ---------- downloads */

struct download_progress
{
	update_install_progress_proc progress;
	void *context;
	int stage;
};

static int download_progress(void *context, posix_ulong received, posix_ulong total)
{
	struct download_progress *download = context;

	return download->progress ? download->progress(download->context, download->stage, (unsigned long)received,
		(unsigned long)total) : 0;
}

static int interrupted_error(const char *error)
{
	return !strncmp(error, "connection lost", 15) || !strncmp(error, "timed out", 9);
}

static int offline_error(const char *error)
{
	return !strncmp(error, "cannot look up", 14) || !strncmp(error, "cannot connect", 14);
}

/* an asset of the release into path: 1 done, -1 stopped, 0 failed */
static int download_asset(const struct update_install_config *config, const char *version, const char *asset,
	const char *path, unsigned long expected, unsigned long maximum, struct download_progress *progress, char *error,
	int error_size)
{
	char url[600];
	int attempt;

	if (!update_asset_url(url, sizeof(url), config->base, version, asset))
	{
		snprintf(error, (size_t)error_size, "no URL for %s", asset);
		return 0;
	}
	/* (a connection cut: tried again, a file of known size resumed) */
	for (attempt = 1;; attempt++)
	{
		int code = posix_https_download(url, config->user_agent, path, expected, maximum, update_redirect_allowed,
			download_progress, progress, error, error_size);

		if (code != 0 || attempt >= DOWNLOAD_ATTEMPTS || !interrupted_error(error))
			return code;
		log_line(config, "%s: interrupted (%s), trying again (%d)", asset, error, attempt + 1);
	}
}

/* ---------- the VPK (a zip: stored or deflated entries, no ZIP64, no
encryption) unpacked into the staging folder */

static unsigned long zip_word(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | (unsigned long)bytes[1] << 8;
}

static unsigned long zip_long(const unsigned char *bytes)
{
	return zip_word(bytes) | zip_word(bytes + 2) << 16;
}

static int same_name(const char *first, const char *second)
{
	/* (the memory card's names ignore case) */
	for (; *first && *second; first++, second++)
	{
		char a = *first >= 'A' && *first <= 'Z' ? (char)(*first - 'A' + 'a') : *first;
		char b = *second >= 'A' && *second <= 'Z' ? (char)(*second - 'A' + 'a') : *second;

		if (a != b)
			return 0;
	}
	return *first == *second;
}

/* a name the install never writes: the system's files of an installed app
(in any case: the memory card's names ignore it) */
static int system_name(const char *name)
{
	static const char *const prefixes[] = { "sce_sys/package", "sce_pfs" };
	size_t index, at;

	for (index = 0; index < sizeof(prefixes) / sizeof(*prefixes); index++)
	{
		for (at = 0; prefixes[index][at]; at++)
		{
			char character = name[at] >= 'A' && name[at] <= 'Z' ? (char)(name[at] - 'A' + 'a') : name[at];

			if (character != prefixes[index][at])
				break;
		}
		if (!prefixes[index][at])
			return 1;
	}
	return 0;
}

/* one entry's data, unpacked into path, its SHA-512 kept: 1 */
static int unpack_entry(FILE *zip, unsigned long data_offset, int method, unsigned long packed_size,
	unsigned long size, unsigned long crc, const char *path, unsigned char hash[64],
	update_install_progress_proc progress, void *context, unsigned long *done, unsigned long total, int *cancelled)
{
	static unsigned char input[16384], output[16384];
	crypto_sha512_ctx sha;
	z_stream stream;
	FILE *file;
	unsigned long remaining = packed_size, written = 0, checksum = crc32(0L, Z_NULL, 0);
	int ended = method == 0, ok = 1;

	if (fseek(zip, (long)data_offset, SEEK_SET) != 0)
		return 0;
	file = fopen(path, "wb");
	if (!file)
		return 0;
	memset(&stream, 0, sizeof(stream));
	if (method == 8 && inflateInit2(&stream, -MAX_WBITS) != Z_OK)
	{
		fclose(file);
		return 0;
	}
	crypto_sha512_init(&sha);
	while (ok && remaining)
	{
		size_t count = remaining < sizeof(input) ? remaining : sizeof(input);

		if (fread(input, 1, count, zip) != count)
		{
			ok = 0;
			break;
		}
		remaining -= (unsigned long)count;
		if (method == 0)
		{
			if (fwrite(input, 1, count, file) != count)
				ok = 0;
			checksum = crc32(checksum, input, (uInt)count);
			crypto_sha512_update(&sha, input, count);
			written += (unsigned long)count;
		}
		else
		{
			stream.next_in = input;
			stream.avail_in = (uInt)count;
			while (ok && !ended && (stream.avail_in || stream.avail_out == 0))
			{
				size_t produced;
				int result;

				stream.next_out = output;
				stream.avail_out = sizeof(output);
				result = inflate(&stream, Z_NO_FLUSH);
				if (result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR)
				{
					ok = 0;
					break;
				}
				produced = sizeof(output) - stream.avail_out;
				/* (never more than the entry says: a bomb stops here) */
				if (produced > size - written)
				{
					ok = 0;
					break;
				}
				if (produced && fwrite(output, 1, produced, file) != produced)
				{
					ok = 0;
					break;
				}
				checksum = crc32(checksum, output, (uInt)produced);
				crypto_sha512_update(&sha, output, produced);
				written += (unsigned long)produced;
				if (result == Z_STREAM_END)
					ended = 1;
				else if (result == Z_BUF_ERROR && !produced)
					break;
			}
			if (ended && (stream.avail_in || remaining))
				ok = 0;
		}
		*done += (unsigned long)count;
		if (ok && progress && progress(context, UPDATE_STAGE_UNPACK, *done, total))
		{
			*cancelled = 1;
			ok = 0;
		}
	}
	if (method == 8)
		inflateEnd(&stream);
	if (fclose(file) != 0)
		ok = 0;
	crypto_sha512_final(&sha, hash);
	return ok && ended && written == size && checksum == crc;
}

/* the VPK at vpk into stage, its files listed in staged: 1, else 0 with
why (*cancelled when progress stopped it) */
static int unpack(const struct update_install_config *config, const char *vpk, const char *stage,
	struct update_install_staged *staged, update_install_progress_proc progress, void *context, int *cancelled,
	char *error, int error_size)
{
	static unsigned char tail[65536 + 22];
	FILE *zip = fopen(vpk, "rb");
	long file_size;
	size_t tail_size, index;
	unsigned long entries = 0, directory_offset = 0, directory_size = 0, entry, unpacked = 0, done = 0;
	unsigned long long space_needed = 0;
	int found = 0, ok = 0, has_eboot = 0, has_sfo = 0;

	staged->count = 0;
	if (!zip)
	{
		snprintf(error, (size_t)error_size, "cannot open the download");
		return 0;
	}
	if (fseek(zip, 0, SEEK_END) != 0 || (file_size = ftell(zip)) < 22)
		goto malformed;
	/* the end of the central directory (no comment longer than 64 KB) */
	tail_size = (unsigned long)file_size < sizeof(tail) ? (size_t)file_size : sizeof(tail);
	if (fseek(zip, file_size - (long)tail_size, SEEK_SET) != 0 || fread(tail, 1, tail_size, zip) != tail_size)
		goto malformed;
	for (index = tail_size - 22 + 1; index-- > 0;)
		if (zip_long(tail + index) == 0x06054b50)
		{
			/* (one disk only) */
			if (zip_word(tail + index + 4) || zip_word(tail + index + 6) ||
				zip_word(tail + index + 8) != zip_word(tail + index + 10))
			{
				goto malformed;
			}
			entries = zip_word(tail + index + 10);
			directory_size = zip_long(tail + index + 12);
			directory_offset = zip_long(tail + index + 16);
			found = 1;
			break;
		}
	if (!found || !entries || entries > ENTRY_MAXIMUM || directory_offset > (unsigned long)file_size ||
		directory_size > (unsigned long)file_size - directory_offset)
	{
		goto malformed;
	}
	/* the names and sizes first: nothing is written for a VPK that breaks a
	bound */
	for (entry = 0; entry < entries; entry++)
	{
		unsigned char header[46];
		unsigned long name_length;
		struct staged_file *file = &staged->files[staged->count];
		int other;

		if (fseek(zip, (long)directory_offset, SEEK_SET) != 0 || fread(header, 1, sizeof(header), zip) != sizeof(header) ||
			zip_long(header) != 0x02014b50)
		{
			goto malformed;
		}
		name_length = zip_word(header + 28);
		if (!name_length || name_length > NAME_MAXIMUM + 1 || fread(file->name, 1, name_length, zip) != name_length)
			goto malformed;
		file->name[name_length] = 0;
		directory_offset += 46 + name_length + zip_word(header + 30) + zip_word(header + 32);
		file->directory = file->name[name_length - 1] == '/';
		if (file->directory)
			file->name[--name_length] = 0;
		if (!update_entry_name_safe(file->name, name_length) || system_name(file->name))
		{
			snprintf(error, (size_t)error_size, "the VPK holds a name not taken (%.60s)", file->name);
			goto failed;
		}
		/* (encrypted or ZIP64: not a VPK of ours) */
		if ((zip_word(header + 8) & 1) || zip_long(header + 20) == 0xFFFFFFFFUL || zip_long(header + 24) == 0xFFFFFFFFUL ||
			zip_long(header + 42) == 0xFFFFFFFFUL)
		{
			goto malformed;
		}
		if (!file->directory && zip_word(header + 10) != 0 && zip_word(header + 10) != 8)
		{
			snprintf(error, (size_t)error_size, "the VPK packs %.60s in a way this build cannot read", file->name);
			goto failed;
		}
		for (other = 0; other < staged->count; other++)
			if (same_name(staged->files[other].name, file->name))
			{
				snprintf(error, (size_t)error_size, "the VPK holds %.60s twice", file->name);
				goto failed;
			}
		file->size = file->directory ? 0 : zip_long(header + 24);
		if (file->size > UNPACKED_MAXIMUM - unpacked)
		{
			snprintf(error, (size_t)error_size, "the VPK unpacks to more than %lu MB", UNPACKED_MAXIMUM >> 20);
			goto failed;
		}
		unpacked += file->size;
		if (!file->directory && !strcmp(file->name, "eboot.bin"))
			has_eboot = 1;
		if (!file->directory && !strcmp(file->name, "sce_sys/param.sfo"))
			has_sfo = 1;
		staged->count++;
	}
	if (!has_eboot || !has_sfo)
	{
		snprintf(error, (size_t)error_size, "the VPK has no eboot.bin or sce_sys/param.sfo");
		goto failed;
	}
	space_needed = (unsigned long long)unpacked + SPACE_MARGIN;
	if (config->free_space && config->free_space(config->data) < space_needed)
	{
		snprintf(error, (size_t)error_size, "not enough space to unpack (%lu MB)", unpacked >> 20);
		goto failed;
	}
	/* then each one, from its local header */
	if (!make_directory(stage))
	{
		snprintf(error, (size_t)error_size, "cannot make %.200s", stage);
		goto failed;
	}
	directory_offset = zip_long(tail + index + 16);
	for (entry = 0; entry < (unsigned long)staged->count; entry++)
	{
		unsigned char header[46], local[30];
		struct staged_file *file = &staged->files[entry];
		unsigned long name_length, local_offset, data_offset, packed_size;
		char path[PATH_SIZE];

		if (fseek(zip, (long)directory_offset, SEEK_SET) != 0 || fread(header, 1, sizeof(header), zip) != sizeof(header))
			goto malformed;
		name_length = zip_word(header + 28);
		directory_offset += 46 + name_length + zip_word(header + 30) + zip_word(header + 32);
		join_path(path, stage, file->name);
		if (!make_parents(stage, file->name))
		{
			snprintf(error, (size_t)error_size, "cannot make the folders of %.60s", file->name);
			goto failed;
		}
		if (file->directory)
		{
			if (!make_directory(path))
			{
				snprintf(error, (size_t)error_size, "cannot make %.60s", file->name);
				goto failed;
			}
			continue;
		}
		local_offset = zip_long(header + 42);
		packed_size = zip_long(header + 20);
		if (local_offset > (unsigned long)file_size - 30 || fseek(zip, (long)local_offset, SEEK_SET) != 0 ||
			fread(local, 1, sizeof(local), zip) != sizeof(local) || zip_long(local) != 0x04034b50)
		{
			goto malformed;
		}
		data_offset = local_offset + 30 + zip_word(local + 26) + zip_word(local + 28);
		if (data_offset > (unsigned long)file_size || packed_size > (unsigned long)file_size - data_offset)
			goto malformed;
		if (!unpack_entry(zip, data_offset, (int)zip_word(header + 10), packed_size, file->size, zip_long(header + 16), path,
			file->sha512, progress, context, &done, (unsigned long)file_size, cancelled))
		{
			if (!*cancelled)
				snprintf(error, (size_t)error_size, "cannot unpack %.60s (damaged, or the memory card is full)", file->name);
			goto failed;
		}
	}
	ok = 1;
	goto done;

malformed:
	snprintf(error, (size_t)error_size, "the VPK is not a zip file this build reads");
failed:
	ok = 0;
done:
	fclose(zip);
	if (!ok)
		staged->count = 0;
	return ok;
}

/* the title ID in the staged param.sfo: 1 when it is the game's */
static int staged_title_matches(const struct update_install_config *config, const char *stage, char *error,
	int error_size)
{
	char path[PATH_SIZE], title[16];
	size_t size;
	char *sfo;
	int ok;

	join_path(path, stage, "sce_sys/param.sfo");
	sfo = read_small_file(path, 65536, &size);
	ok = sfo && update_sfo_title((const unsigned char *)sfo, size, title, sizeof(title)) && !strcmp(title, config->title);
	if (!ok)
		snprintf(error, (size_t)error_size, "the VPK's param.sfo is not this game's (%s)", sfo ? title : "unreadable");
	free(sfo);
	return ok;
}

/* ---------- prepare */

/* the downloaded files and the staging folder removed (keep_download: the
VPK and its manifest stay, to resume) */
static void clean(const struct update_install_config *config, int keep_download)
{
	char path[PATH_SIZE];

	update_path(config, path, FILE_MANIFEST_NEW);
	remove(path);
	update_path(config, path, FILE_SIGNATURE_NEW);
	remove(path);
	update_path(config, path, FOLDER_STAGE);
	remove_tree(config, path);
	if (!keep_download)
	{
		update_path(config, path, FILE_VPK);
		remove(path);
		update_path(config, path, FILE_MANIFEST);
		remove(path);
	}
}

/* the code for a failed download */
static int download_failure(const char *error)
{
	return offline_error(error) ? UPDATE_INSTALL_NO_CONNECTION : UPDATE_INSTALL_FAILED;
}

int update_install_prepare(const struct update_install_config *config, const char *version,
	update_install_progress_proc progress, void *context, struct update_install_staged **staged_out, char *error,
	int error_size)
{
	char path[PATH_SIZE], vpk[PATH_SIZE], stage[PATH_SIZE], why[256] = "";
	char *manifest_text = NULL, *signature_text = NULL, *kept = NULL;
	size_t manifest_size = 0, signature_size = 0, kept_size = 0;
	unsigned char key[32], hash[64];
	struct update_manifest manifest;
	struct update_install_staged *staged = NULL;
	struct download_progress download = { progress, context, UPDATE_STAGE_MANIFEST };
	unsigned long size = 0;
	int result = UPDATE_INSTALL_FAILED, code, cancelled = 0;
	FILE *probe;

	*staged_out = NULL;
	error[0] = 0;
	log_line(config, "preparing %s (this is %s)", version, config->current_version);
	if (!update_public_key(key))
	{
		snprintf(error, (size_t)error_size, "no update key in this build");
		log_line(config, "refused: %s", error);
		return UPDATE_INSTALL_NO_KEY;
	}
	/* (the game's folder writable: unsafe homebrew) */
	join_path(path, config->app, PROBE_FILE);
	probe = fopen(path, "wb");
	if (!probe)
	{
		snprintf(error, (size_t)error_size, "cannot write in %s (unsafe homebrew is off)", config->app);
		log_line(config, "refused: %s", error);
		return UPDATE_INSTALL_NO_ACCESS;
	}
	fclose(probe);
	remove(path);
	update_path(config, path, FILE_JOURNAL);
	if (is_file(path))
	{
		snprintf(error, (size_t)error_size, "an earlier install was cut short: restart Halo first");
		log_line(config, "refused: %s", error);
		return UPDATE_INSTALL_FAILED;
	}
	update_path(config, path, NULL);
	if (!make_directory(config->data) || !make_directory(path))
	{
		snprintf(error, (size_t)error_size, "cannot make %s", path);
		return UPDATE_INSTALL_FAILED;
	}
	clean(config, 1);

	/* the manifest and its signature, checked before anything else */
	update_path(config, path, FILE_MANIFEST_NEW);
	code = download_asset(config, version, UPDATE_ASSET_MANIFEST, path, 0, UPDATE_MANIFEST_MAXIMUM, &download, why,
		sizeof(why));
	if (code == 1)
	{
		manifest_text = read_small_file(path, UPDATE_MANIFEST_MAXIMUM, &manifest_size);
		update_path(config, path, FILE_SIGNATURE_NEW);
		code = download_asset(config, version, UPDATE_ASSET_SIGNATURE, path, 0, UPDATE_SIGNATURE_FILE_MAXIMUM, &download,
			why, sizeof(why));
		if (code == 1)
			signature_text = read_small_file(path, UPDATE_SIGNATURE_FILE_MAXIMUM, &signature_size);
	}
	if (code != 1)
	{
		result = code < 0 ? UPDATE_INSTALL_CANCELLED : download_failure(why);
		snprintf(error, (size_t)error_size, code < 0 ? "stopped" : "the signed manifest could not be downloaded: %s",
			why);
		goto failed;
	}
	code = update_manifest_check(manifest_text, manifest_size, signature_text, signature_size, key, version,
		config->current_version, config->title, &manifest, why, sizeof(why));
	if (code != UPDATE_VERIFY_OK)
	{
		result = code == UPDATE_VERIFY_NO_KEY ? UPDATE_INSTALL_NO_KEY : UPDATE_INSTALL_FAILED;
		snprintf(error, (size_t)error_size, "%s", why);
		goto failed;
	}
	log_line(config, "manifest for %s signed by the update key: %s, %lu bytes", manifest.version, manifest.file,
		manifest.size);

	/* a download cut short is resumed only for the very same signed file */
	update_path(config, vpk, FILE_VPK);
	update_path(config, path, FILE_MANIFEST);
	kept = read_small_file(path, UPDATE_MANIFEST_MAXIMUM, &kept_size);
	if (!kept || kept_size != manifest_size || memcmp(kept, manifest_text, manifest_size))
		remove(vpk);
	if (!write_small_file(path, manifest_text, manifest_size))
	{
		snprintf(error, (size_t)error_size, "cannot write %s (is the memory card full?)", path);
		goto failed;
	}
	if (config->free_space)
	{
		unsigned long long free_bytes = config->free_space(config->data);
		/* (the download, its files unpacked, and room to spare) */
		unsigned long long needed = 2ULL * manifest.size + SPACE_MARGIN;

		if (free_bytes < needed)
		{
			snprintf(error, (size_t)error_size, "%llu MB free; the update needs about %llu MB", free_bytes >> 20,
				(needed + (1 << 20) - 1) >> 20);
			result = UPDATE_INSTALL_NO_SPACE;
			goto failed;
		}
	}

	/* halo.vpk, to the signed size at most (a cut connection: tried again,
	resumed) */
	download.stage = UPDATE_STAGE_DOWNLOAD;
	code = download_asset(config, version, UPDATE_ASSET_VPK, vpk, manifest.size, manifest.size, &download, why,
		sizeof(why));
	if (code < 0)
	{
		result = UPDATE_INSTALL_CANCELLED;
		snprintf(error, (size_t)error_size, "stopped");
		goto failed;
	}
	if (code == 0)
	{
		result = download_failure(why);
		snprintf(error, (size_t)error_size, "the download failed: %s", why);
		/* (cut by the network: kept, the next try resumes it) */
		if (interrupted_error(why) || offline_error(why))
		{
			log_line(config, "failed: %s (the part downloaded is kept, to resume)", error);
			clean(config, 1);
			free(manifest_text);
			free(signature_text);
			free(kept);
			return result;
		}
		goto failed;
	}
	/* its size and SHA-512: the manifest's */
	if (!hash_file(vpk, hash, &size, progress, context, manifest.size, &cancelled) || size != manifest.size ||
		memcmp(hash, manifest.sha512, sizeof(hash)))
	{
		result = cancelled ? UPDATE_INSTALL_CANCELLED : UPDATE_INSTALL_FAILED;
		snprintf(error, (size_t)error_size, cancelled ? "stopped" : "the download is not the file the manifest signs");
		goto failed;
	}
	log_line(config, "%s matches its signed SHA-512", UPDATE_ASSET_VPK);

	/* unpacked, its title checked */
	staged = calloc(1, sizeof(*staged));
	if (!staged)
	{
		snprintf(error, (size_t)error_size, "out of memory");
		goto failed;
	}
	snprintf(staged->version, sizeof(staged->version), "%s", manifest.version);
	update_path(config, stage, FOLDER_STAGE);
	if (!unpack(config, vpk, stage, staged, progress, context, &cancelled, why, sizeof(why)))
	{
		result = cancelled ? UPDATE_INSTALL_CANCELLED : UPDATE_INSTALL_FAILED;
		snprintf(error, (size_t)error_size, "%s", cancelled ? "stopped" : why);
		goto failed;
	}
	if (!staged_title_matches(config, stage, error, error_size))
		goto failed;
	log_line(config, "%s unpacked: %d entries, ready to install", manifest.version, staged->count);
	free(manifest_text);
	free(signature_text);
	free(kept);
	*staged_out = staged;
	return UPDATE_INSTALL_OK;

failed:
	if (result == UPDATE_INSTALL_CANCELLED)
		log_line(config, "stopped by the player; nothing installed");
	else
		log_line(config, "failed: %s; nothing installed", error);
	free(manifest_text);
	free(signature_text);
	free(kept);
	free(staged);
	clean(config, 0);
	return result;
}

void update_install_discard(const struct update_install_config *config, struct update_install_staged *staged)
{
	free(staged);
	clean(config, 1);
}

/* ---------- commit, and undoing it */

struct journal_entry
{
	int existed;
	char name[NAME_MAXIMUM + 1];
};

/* the journal's entries (count) read: 1 when it is whole (its "end" line),
0 when it was cut short (nothing moved yet), -1 when it is not a journal */
static int journal_read(const char *path, struct journal_entry *entries, int *count)
{
	size_t size = 0, at;
	char *text = read_small_file(path, ENTRY_MAXIMUM * (NAME_MAXIMUM + 4) + 64, &size);
	const size_t magic_length = sizeof(JOURNAL_MAGIC) - 1;
	int whole = 0;

	*count = 0;
	if (!text)
		return -1;
	if (size < magic_length + 1 || memcmp(text, JOURNAL_MAGIC "\n", magic_length + 1))
	{
		free(text);
		return -1;
	}
	for (at = magic_length + 1; at < size;)
	{
		char *end = memchr(text + at, '\n', size - at);
		size_t length;

		if (!end)
			break;
		length = (size_t)(end - (text + at));
		if (length == 3 && !memcmp(text + at, "end", 3))
		{
			whole = 1;
			break;
		}
		if (length < 3 || (text[at] != '0' && text[at] != '1') || text[at + 1] != ' ' || length - 2 > NAME_MAXIMUM ||
			*count >= ENTRY_MAXIMUM || !update_entry_name_safe(text + at + 2, length - 2))
		{
			free(text);
			return -1;
		}
		entries[*count].existed = text[at] == '1';
		memcpy(entries[*count].name, text + at + 2, length - 2);
		entries[*count].name[length - 2] = 0;
		(*count)++;
		at += length + 1;
	}
	free(text);
	return whole;
}

/* the game's folder as the journal says it was: the files moved aside put
back, the new ones removed; the number changed, or -1 if one could not be
put back */
static int journal_undo(const struct update_install_config *config, const struct journal_entry *entries, int count)
{
	char backup[PATH_SIZE];
	int index, changed = 0, failed = 0;

	update_path(config, backup, FOLDER_BACKUP);
	for (index = count; index-- > 0;)
	{
		char saved[PATH_SIZE], installed[PATH_SIZE];

		join_path(saved, backup, entries[index].name);
		join_path(installed, config->app, entries[index].name);
		if (entries[index].existed && is_file(saved))
		{
			remove(installed);
			if (move_file(saved, installed))
				changed++;
			else
			{
				failed = 1;
				log_line(config, "CANNOT PUT BACK %s (it is in %s)", installed, saved);
			}
		}
		else if (!entries[index].existed && is_file(installed))
		{
			remove(installed);
			changed++;
		}
	}
	return failed ? -1 : changed;
}

/* eboot.bin last: until it moves, the game is the old one */
static int install_order(const struct update_install_staged *staged, int position)
{
	int index, seen = 0;

	for (index = 0; index < staged->count; index++)
	{
		if (staged->files[index].directory || !strcmp(staged->files[index].name, "eboot.bin"))
			continue;
		if (seen++ == position)
			return index;
	}
	for (index = 0; index < staged->count; index++)
		if (!staged->files[index].directory && !strcmp(staged->files[index].name, "eboot.bin"))
			return seen == position ? index : -1;
	return -1;
}

int update_install_commit(const struct update_install_config *config, struct update_install_staged *staged,
	update_install_progress_proc progress, void *context, char *error, int error_size)
{
	static struct journal_entry entries[ENTRY_MAXIMUM];
	char stage[PATH_SIZE], backup[PATH_SIZE], journal[PATH_SIZE], path[PATH_SIZE];
	int files = 0, index, position, moved = 0;
	FILE *file;

	error[0] = 0;
	update_path(config, stage, FOLDER_STAGE);
	update_path(config, backup, FOLDER_BACKUP);
	update_path(config, journal, FILE_JOURNAL);
	if (!staged)
	{
		snprintf(error, (size_t)error_size, "nothing staged");
		return UPDATE_INSTALL_FAILED;
	}
	for (index = 0; index < staged->count; index++)
		files += !staged->files[index].directory;
	/* each staged file again as it was unpacked (the staging folder is on
	the memory card, which other homebrew can write) */
	for (index = 0; index < staged->count; index++)
	{
		const struct staged_file *staged_file = &staged->files[index];
		unsigned char hash[64];
		unsigned long size = 0;
		int cancelled = 0;

		if (staged_file->directory)
			continue;
		join_path(path, stage, staged_file->name);
		if (!hash_file(path, hash, &size, NULL, NULL, 0, &cancelled) || size != staged_file->size ||
			memcmp(hash, staged_file->sha512, sizeof(hash)))
		{
			snprintf(error, (size_t)error_size, "%.60s changed after it was checked", staged_file->name);
			goto refused;
		}
	}
	/* the journal, whole, before the first move */
	remove_tree(config, backup);
	if (!make_directory(backup))
	{
		snprintf(error, (size_t)error_size, "cannot make %s", backup);
		goto refused;
	}
	file = fopen(journal, "wb");
	if (!file)
	{
		snprintf(error, (size_t)error_size, "cannot write %s", journal);
		goto refused;
	}
	fprintf(file, JOURNAL_MAGIC "\n");
	for (position = 0; position < files; position++)
	{
		int order = install_order(staged, position);
		const struct staged_file *staged_file;

		if (order < 0)
		{
			fclose(file);
			remove(journal);
			snprintf(error, (size_t)error_size, "the staged files have no eboot.bin");
			goto refused;
		}
		staged_file = &staged->files[order];

		join_path(path, config->app, staged_file->name);
		entries[position].existed = is_file(path);
		snprintf(entries[position].name, sizeof(entries[position].name), "%s", staged_file->name);
		fprintf(file, "%d %s\n", entries[position].existed, staged_file->name);
	}
	fprintf(file, "end\n");
	if (ferror(file) | (fclose(file) != 0))
	{
		remove(journal);
		snprintf(error, (size_t)error_size, "cannot write %s (is the memory card full?)", journal);
		goto refused;
	}
	log_line(config, "installing %s: %d files into %s", staged->version, files, config->app);
	/* the moves: the old file aside, the new one in */
	for (position = 0; position < files; position++)
	{
		char installed[PATH_SIZE], saved[PATH_SIZE], incoming[PATH_SIZE];
		const char *name = entries[position].name;

		join_path(installed, config->app, name);
		join_path(saved, backup, name);
		join_path(incoming, stage, name);
		if (entries[position].existed && (!make_parents(backup, name) || !move_file(installed, saved)))
		{
			snprintf(error, (size_t)error_size, "cannot move %.60s aside", name);
			break;
		}
		if (!make_parents(config->app, name) || !move_file(incoming, installed))
		{
			snprintf(error, (size_t)error_size, "cannot put %.60s in place", name);
			break;
		}
		moved++;
		if (progress)
			progress(context, UPDATE_STAGE_INSTALL, (unsigned long)moved, (unsigned long)files);
#ifdef UPDATE_INSTALL_TEST_HOOKS
		if (update_install_test_stop_after >= 0 && moved >= update_install_test_stop_after)
		{
			/* (left as a power cut leaves it: the journal, the backup) */
			free(staged);
			return UPDATE_INSTALL_FAILED;
		}
#endif
	}
	if (moved < files)
	{
		int undone = journal_undo(config, entries, files);

		if (undone >= 0)
		{
			remove(journal);
			log_line(config, "failed: %s; the game's files are as they were (%d put back)", error, undone);
		}
		else
			log_line(config, "failed: %s; SOME FILES COULD NOT BE PUT BACK (tried again at the next start)", error);
		free(staged);
		clean(config, 0);
		if (undone >= 0)
			remove_tree(config, backup);
		return UPDATE_INSTALL_FAILED;
	}
	/* done: what was kept for undoing removed */
	remove(journal);
	remove_tree(config, backup);
	log_line(config, "%s installed (%d files)", staged->version, files);
	free(staged);
	clean(config, 0);
	return UPDATE_INSTALL_OK;

refused:
	log_line(config, "failed: %s; nothing installed", error);
	free(staged);
	clean(config, 0);
	return UPDATE_INSTALL_FAILED;
}

int update_install_recover(const struct update_install_config *config)
{
	static struct journal_entry entries[ENTRY_MAXIMUM];
	char journal[PATH_SIZE], path[PATH_SIZE];
	int count = 0, whole, result = 0;

	update_path(config, journal, FILE_JOURNAL);
	if (is_file(journal))
	{
		whole = journal_read(journal, entries, &count);
		if (whole > 0)
		{
			result = journal_undo(config, entries, count);
			if (result < 0)
			{
				log_line(config, "an install cut short could not be undone: the journal stays");
				return -1;
			}
			log_line(config, "an install cut short was undone (%d of the game's files put back)", result);
		}
		else if (whole == 0)
			log_line(config, "an install's journal was cut short before any file moved: removed");
		else
			log_line(config, "%s is not a journal: removed", journal);
		remove(journal);
	}
	update_path(config, path, FOLDER_BACKUP);
	remove_tree(config, path);
	update_path(config, path, FOLDER_STAGE);
	remove_tree(config, path);
	return result;
}
