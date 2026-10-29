/*
VITA_UPDATE_DOWNLOAD.C

The Vita's updater's downloads and checks (vita_update_download.h), with
the C library's files (newlib's on the Vita, whose paths are "ux0:...") and
posix_https_download, so the same code runs on Linux under
tests/update_download_harness.c.

The game is a safe homebrew and installs nothing: a release that passes
every check is kept in <data>/update for the player to install with
VitaShell, as halo-<version>.vpk beside the manifest and signature that
vouch for it (so each start can check it again before the panel and the
main menu point at it). The files in <data>/update:

  halo-update.new, halo-update.sig.new   a manifest and signature just
                                         downloaded, not yet checked
  halo.vpk.part, halo-update.txt         a download under way (or cut
                                         short) and the checked manifest it
                                         is for (resumed only for that one)
  halo-<version>.vpk, .txt, .txt.sig     a checked release, to install
*/

#include "vita_update_download.h"
#include "vita_update_verify.h"
#include "update_check.h"
#include "posix.h"

#include "monocypher-ed25519.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define UPDATE_FOLDER "update"
#define FILE_MANIFEST_NEW "halo-update.new"
#define FILE_SIGNATURE_NEW "halo-update.sig.new"
#define FILE_MANIFEST "halo-update.txt"
#define FILE_PART "halo.vpk.part"
#define PATH_SIZE 768
/* room left on the card beyond the download */
#define SPACE_MARGIN (16ULL * 1024 * 1024)

/* ---------- paths and logging */

static void log_line(const struct update_download_config *config, const char *format, ...)
	__attribute__((format(printf, 2, 3)));

static void log_line(const struct update_download_config *config, const char *format, ...)
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
file operation takes (the folder's bound keeps it well short) */
static void update_path(const struct update_download_config *config, char *path, const char *name)
{
	int length = name ? snprintf(path, PATH_SIZE, "%s/" UPDATE_FOLDER "/%s", config->data, name) :
		snprintf(path, PATH_SIZE, "%s/" UPDATE_FOLDER, config->data);

	if (length <= 0 || length >= PATH_SIZE)
		path[0] = 0;
}

/* <data>/update/halo-<version><suffix> */
static void kept_path(const struct update_download_config *config, char *path, const char *version, const char *suffix)
{
	char name[128];

	snprintf(name, sizeof(name), "halo-%.60s%s", version, suffix);
	update_path(config, path, name);
}

static int is_file(const char *path)
{
	struct stat information;

	return stat(path, &information) == 0 && S_ISREG(information.st_mode);
}

static int make_directory(const char *path)
{
	struct stat information;

	return mkdir(path, 0777) == 0 || (stat(path, &information) == 0 && S_ISDIR(information.st_mode));
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

/* the SHA-512 of a file (and its size): 1 */
static int hash_file(const char *path, unsigned char hash[64], unsigned long *size,
	update_download_progress_proc progress, void *context, unsigned long total, int *cancelled)
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

/* whether the file at path is the one the manifest names (size, SHA-512) */
static int file_matches(const char *path, const struct update_manifest *manifest,
	update_download_progress_proc progress, void *context, int *cancelled)
{
	unsigned char hash[64];
	unsigned long size = 0;

	return hash_file(path, hash, &size, progress, context, manifest->size, cancelled) && size == manifest->size &&
		!memcmp(hash, manifest->sha512, sizeof(hash));
}

/* a kept release's three files removed */
static void remove_kept(const struct update_download_config *config, const char *version)
{
	static const char *const suffixes[] = { ".vpk", ".txt", ".txt.sig" };
	char path[PATH_SIZE];
	size_t index;

	for (index = 0; index < sizeof(suffixes) / sizeof(*suffixes); index++)
	{
		kept_path(config, path, version, suffixes[index]);
		remove(path);
	}
}

/* the version a kept release's file name holds ("halo-1.1.0-beta.4.txt"):
1 when the name is one (a release tag's version), else 0 */
static int kept_name_version(const char *name, const char *suffix, char *version, size_t size)
{
	size_t length = strlen(name), suffix_length = strlen(suffix);
	char tag[80];

	if (length <= 5 + suffix_length || strncmp(name, "halo-", 5) || strcmp(name + length - suffix_length, suffix) ||
		length - 5 - suffix_length >= size || length - 5 - suffix_length >= sizeof(tag) - 1)
	{
		return 0;
	}
	memcpy(version, name + 5, length - 5 - suffix_length);
	version[length - 5 - suffix_length] = 0;
	snprintf(tag, sizeof(tag), "v%s", version);
	return update_tag_valid(tag);
}

#define KEPT_NAMES_MAXIMUM 32

/* the names in <data>/update of kept releases' files (suffix), at most
KEPT_NAMES_MAXIMUM, read before any is removed */
static int kept_names(const struct update_download_config *config, const char *suffix, char names[][96])
{
	char folder[PATH_SIZE], version[64];
	DIR *directory;
	struct dirent *entry;
	int count = 0;

	update_path(config, folder, NULL);
	directory = opendir(folder);
	if (!directory)
		return 0;
	while (count < KEPT_NAMES_MAXIMUM && (entry = readdir(directory)) != NULL)
		if (strlen(entry->d_name) < 96 && kept_name_version(entry->d_name, suffix, version, sizeof(version)))
			snprintf(names[count++], 96, "%.95s", entry->d_name);
	closedir(directory);
	return count;
}

/* every kept release but keep (NULL: all) removed, and stray files of
theirs (a .vpk without its manifest) */
static void remove_other_kept(const struct update_download_config *config, const char *keep)
{
	static const char *const suffixes[] = { ".vpk", ".txt", ".txt.sig" };
	char names[KEPT_NAMES_MAXIMUM][96], version[64], path[PATH_SIZE];
	size_t kind;

	for (kind = 0; kind < sizeof(suffixes) / sizeof(*suffixes); kind++)
	{
		int count = kept_names(config, suffixes[kind], names), index;

		for (index = 0; index < count; index++)
		{
			kept_name_version(names[index], suffixes[kind], version, sizeof(version));
			if (keep && !strcmp(version, keep))
				continue;
			update_path(config, path, names[index]);
			remove(path);
			log_line(config, "%s removed", names[index]);
		}
	}
}

/* ---------- downloads */

struct download_progress
{
	update_download_progress_proc progress;
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

/* an asset of the release into path: 1 done, -1 stopped, 0 failed (a cut
connection tried again, a file of known size resumed) */
static int download_asset(const struct update_download_config *config, const char *version, const char *asset,
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
	for (attempt = 1;; attempt++)
	{
		int code = posix_https_download(url, config->user_agent, path, expected, maximum, update_redirect_allowed,
			download_progress, progress, error, error_size);

		if (code != 0 || attempt >= 3 || !interrupted_error(error))
			return code;
		log_line(config, "%s: interrupted (%s), trying again (%d)", asset, error, attempt + 1);
	}
}

/* the files of a download not kept removed (keep_part: the VPK under way
and its manifest stay, to resume) */
static void clean(const struct update_download_config *config, int keep_part)
{
	char path[PATH_SIZE];

	update_path(config, path, FILE_MANIFEST_NEW);
	remove(path);
	update_path(config, path, FILE_SIGNATURE_NEW);
	remove(path);
	if (!keep_part)
	{
		update_path(config, path, FILE_PART);
		remove(path);
		update_path(config, path, FILE_MANIFEST);
		remove(path);
	}
}

/* ---------- fetch */

int update_download_fetch(const struct update_download_config *config, const char *version,
	update_download_progress_proc progress, void *context, char *kept, int kept_size, char *error, int error_size)
{
	char path[PATH_SIZE], part[PATH_SIZE], target[PATH_SIZE], why[256] = "";
	char *manifest_text = NULL, *signature_text = NULL, *resumed = NULL;
	size_t manifest_size = 0, signature_size = 0, resumed_size = 0;
	unsigned char key[32];
	struct update_manifest manifest;
	struct download_progress download = { progress, context, UPDATE_STAGE_MANIFEST };
	int result = UPDATE_DOWNLOAD_FAILED, code, cancelled = 0, keep_part = 0;

	error[0] = 0;
	if (kept && kept_size > 0)
		kept[0] = 0;
	log_line(config, "downloading %s (this is %s)", version, config->current_version);
	if (!update_public_key(key))
	{
		snprintf(error, (size_t)error_size, "no update key in this build");
		log_line(config, "refused: %s", error);
		return UPDATE_DOWNLOAD_NO_KEY;
	}
	update_path(config, path, NULL);
	if (!make_directory(config->data) || !make_directory(path))
	{
		snprintf(error, (size_t)error_size, "cannot make %s", path);
		log_line(config, "failed: %s", error);
		return UPDATE_DOWNLOAD_FAILED;
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
		result = code < 0 ? UPDATE_DOWNLOAD_CANCELLED : offline_error(why) ? UPDATE_DOWNLOAD_NO_CONNECTION :
			UPDATE_DOWNLOAD_FAILED;
		snprintf(error, (size_t)error_size, code < 0 ? "stopped" : "the signed manifest could not be downloaded: %s",
			why);
		/* (a release that will not come: the part of another stays) */
		keep_part = 1;
		goto failed;
	}
	code = update_manifest_check(manifest_text, manifest_size, signature_text, signature_size, key, version,
		config->current_version, config->title, &manifest, why, sizeof(why));
	if (code != UPDATE_VERIFY_OK)
	{
		result = code == UPDATE_VERIFY_NO_KEY ? UPDATE_DOWNLOAD_NO_KEY : UPDATE_DOWNLOAD_FAILED;
		snprintf(error, (size_t)error_size, "%s", why);
		keep_part = 1;
		goto failed;
	}
	log_line(config, "manifest for %s signed by the update key: %s, %lu bytes", manifest.version, manifest.file,
		manifest.size);
	kept_path(config, target, manifest.version, ".vpk");

	/* kept already, and still the signed file: taken as it is */
	if (is_file(target) && file_matches(target, &manifest, progress, context, &cancelled))
	{
		log_line(config, "%s is kept already and matches its signed SHA-512", target);
		goto keep;
	}
	if (cancelled)
	{
		result = UPDATE_DOWNLOAD_CANCELLED;
		snprintf(error, (size_t)error_size, "stopped");
		keep_part = 1;
		goto failed;
	}

	/* a download cut short is resumed only for the very same signed file */
	update_path(config, part, FILE_PART);
	update_path(config, path, FILE_MANIFEST);
	resumed = read_small_file(path, UPDATE_MANIFEST_MAXIMUM, &resumed_size);
	if (!resumed || resumed_size != manifest_size || memcmp(resumed, manifest_text, manifest_size))
		remove(part);
	if (!write_small_file(path, manifest_text, manifest_size))
	{
		snprintf(error, (size_t)error_size, "cannot write %s (is the memory card full?)", path);
		goto failed;
	}
	if (config->free_space)
	{
		struct stat information;
		unsigned long long have = stat(part, &information) == 0 ? (unsigned long long)information.st_size : 0;
		unsigned long long needed = (have < manifest.size ? manifest.size - have : 0) + SPACE_MARGIN;
		unsigned long long free_bytes = config->free_space(config->data);

		if (free_bytes < needed)
		{
			snprintf(error, (size_t)error_size, "%llu MB free; the download needs about %llu MB", free_bytes >> 20,
				(needed + (1 << 20) - 1) >> 20);
			result = UPDATE_DOWNLOAD_NO_SPACE;
			goto failed;
		}
	}

	/* the VPK the manifest names (halo-ce-vita-<version>-vita.vpk, or
	halo.vpk: update_manifest_check took only those), to the signed size at
	most */
	download.stage = UPDATE_STAGE_DOWNLOAD;
	code = download_asset(config, version, manifest.file, part, manifest.size, manifest.size, &download, why,
		sizeof(why));
	if (code < 0)
	{
		result = UPDATE_DOWNLOAD_CANCELLED;
		snprintf(error, (size_t)error_size, "stopped");
		goto failed;
	}
	if (code == 0)
	{
		result = offline_error(why) ? UPDATE_DOWNLOAD_NO_CONNECTION : UPDATE_DOWNLOAD_FAILED;
		snprintf(error, (size_t)error_size, "the download failed: %s", why);
		/* (cut by the network: kept, the next try resumes it) */
		keep_part = interrupted_error(why) || offline_error(why);
		goto failed;
	}
	/* its size and SHA-512: the manifest's */
	if (!file_matches(part, &manifest, progress, context, &cancelled))
	{
		result = cancelled ? UPDATE_DOWNLOAD_CANCELLED : UPDATE_DOWNLOAD_FAILED;
		snprintf(error, (size_t)error_size, cancelled ? "stopped" : "the download is not the file the manifest signs");
		goto failed;
	}
	remove(target);
	if (rename(part, target) != 0)
	{
		snprintf(error, (size_t)error_size, "cannot rename the download to %s", target);
		goto failed;
	}
	log_line(config, "%s matches its signed SHA-512", manifest.file);

keep:
	/* the manifest and signature that vouch for it, beside it */
	kept_path(config, path, manifest.version, ".txt");
	if (!write_small_file(path, manifest_text, manifest_size))
	{
		snprintf(error, (size_t)error_size, "cannot write %s (is the memory card full?)", path);
		remove(target);
		goto failed;
	}
	kept_path(config, path, manifest.version, ".txt.sig");
	if (!write_small_file(path, signature_text, signature_size))
	{
		snprintf(error, (size_t)error_size, "cannot write %s (is the memory card full?)", path);
		remove_kept(config, manifest.version);
		goto failed;
	}
	clean(config, 0);
	remove_other_kept(config, manifest.version);
	if (kept && kept_size > 0)
		snprintf(kept, (size_t)kept_size, "%s", target);
	log_line(config, "%s downloaded and verified: install %s with VitaShell", manifest.version, target);
	free(manifest_text);
	free(signature_text);
	free(resumed);
	return UPDATE_DOWNLOAD_OK;

failed:
	if (result == UPDATE_DOWNLOAD_CANCELLED)
		log_line(config, "stopped by the player; nothing kept");
	else
		log_line(config, "failed: %s; nothing kept%s", error, keep_part ? " (a download under way stays, to resume)" : "");
	free(manifest_text);
	free(signature_text);
	free(resumed);
	clean(config, keep_part);
	return result;
}

/* ---------- at start-up */

int update_download_kept(const struct update_download_config *config, char *version, int version_size, char *kept,
	int kept_size)
{
	char names[KEPT_NAMES_MAXIMUM][96], best[64] = "", name_version[64], path[PATH_SIZE];
	unsigned char key[32];
	int count, index;

	if (version_size > 0)
		version[0] = 0;
	if (kept_size > 0)
		kept[0] = 0;
	if (!update_public_key(key))
		return 0;
	count = kept_names(config, ".txt", names);
	/* (each release's manifest names it; its other files follow it) */
	for (index = 0; index < count; index++)
	{
		char path[PATH_SIZE], why[160];
		char *text, *signature;
		size_t size = 0, signature_size = 0;
		struct update_manifest manifest;
		int code, cancelled = 0;

		kept_name_version(names[index], ".txt", name_version, sizeof(name_version));
		kept_path(config, path, name_version, ".txt");
		text = read_small_file(path, UPDATE_MANIFEST_MAXIMUM, &size);
		kept_path(config, path, name_version, ".txt.sig");
		signature = read_small_file(path, UPDATE_SIGNATURE_FILE_MAXIMUM, &signature_size);
		code = update_manifest_check(text, size, signature, signature_size, key, name_version, config->current_version,
			config->title, &manifest, why, sizeof(why));
		free(text);
		free(signature);
		kept_path(config, path, name_version, ".vpk");
		if (code == UPDATE_VERIFY_NOT_NEWER)
		{
			log_line(config, "halo-%s.vpk is no newer than this build (installed): removed", name_version);
			remove_kept(config, name_version);
			continue;
		}
		if (code != UPDATE_VERIFY_OK || !file_matches(path, &manifest, NULL, NULL, &cancelled))
		{
			log_line(config, "halo-%s.vpk failed its check (%s): removed", name_version,
				code != UPDATE_VERIFY_OK ? why : "not the file its manifest signs");
			remove_kept(config, name_version);
			continue;
		}
		if (!best[0] || update_is_newer(name_version, best))
			snprintf(best, sizeof(best), "%s", name_version);
	}
	/* (a .vpk left without its manifest, or an older one: removed) */
	remove_other_kept(config, best[0] ? best : NULL);
	if (!best[0])
		return 0;
	snprintf(version, (size_t)version_size, "%s", best);
	kept_path(config, path, best, ".vpk");
	snprintf(kept, (size_t)kept_size, "%s", path);
	log_line(config, "%s is downloaded and verified: install %s with VitaShell", best, path);
	return 1;
}
