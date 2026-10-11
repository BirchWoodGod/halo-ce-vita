/*
VITA_UPDATE_DOWNLOAD.H

The Vita's updater's work (vita_update_download.c), with no screen in it:
vita_update.c drives it from the settings panel on the Vita, and
tests/update_download_harness.c on Linux, against a local server and a
folder of its own standing in for ux0:data/haloce-vita.

The game is a safe homebrew (vita-make-fself -s): it cannot write its own
folder in ux0:app, so it does not install anything. It downloads a newer
release, checks it, and keeps it where the player installs it with
VitaShell:

1. update_download_fetch: the release's manifest and signature downloaded
   (posix_https_download, from the release's fixed folder on GitHub) and
   checked (vita_update_verify.h); the space on the memory card checked;
   halo.vpk downloaded (resumed when a download of the same signed file was
   cut short), its size and SHA-512 checked against the manifest; then kept
   as <data>/update/halo-<version>.vpk beside its signed manifest
   (halo-<version>.txt, halo-<version>.txt.sig), any older one removed.
   A refusal keeps nothing of it (a download cut by the network is kept, to
   resume); one already kept and still matching is taken as it is.
2. update_download_kept, at start-up: each kept VPK checked again (its
   manifest's signature, version, title, size, SHA-512); the newest that is
   newer than the running build is the one to install; one no newer (it has
   been installed) or failing a check is removed.

Nothing outside <data>/update is touched (the data folder's saves,
settings.txt, config.toml, maps and movies stay).
*/

#ifndef VITA_UPDATE_DOWNLOAD_H
#define VITA_UPDATE_DOWNLOAD_H

#include "vita_update_verify.h"

enum
{
	UPDATE_DOWNLOAD_OK = 0,
	UPDATE_DOWNLOAD_FAILED = -1,
	UPDATE_DOWNLOAD_CANCELLED = -2,
	/* the placeholder key: this build keeps no update */
	UPDATE_DOWNLOAD_NO_KEY = -3,
	UPDATE_DOWNLOAD_NO_SPACE = -5,
	UPDATE_DOWNLOAD_NO_CONNECTION = -6,
};

/* what the progress hears */
enum
{
	UPDATE_STAGE_MANIFEST,
	UPDATE_STAGE_DOWNLOAD,
	UPDATE_STAGE_VERIFY,
};

/* done of total in the stage; nonzero stops the work */
typedef int (*update_download_progress_proc)(void *context, int stage, unsigned long done, unsigned long total);
typedef void (*update_download_log_proc)(const char *line);

struct update_download_config
{
	/* the data folder (ux0:data/haloce-vita): <data>/update is the updater's */
	char data[256];
	/* the running game's title ID and version */
	char title[16];
	char current_version[64];
	/* where the release's files are (UPDATE_DOWNLOAD_BASE; a test build's,
	HALO_UPDATE_TEST_SOURCES; the tests' server) */
	char base[256];
	char user_agent[64];
	/* the free bytes of the memory card (NULL: not checked) */
	unsigned long long (*free_space)(const char *path);
	update_download_log_proc log;
};

/* step 1 for version (a tag without its v, newer than the running build):
UPDATE_DOWNLOAD_OK with the kept VPK's path in path, else a code and why in
error */
int update_download_fetch(const struct update_download_config *config, const char *version,
	update_download_progress_proc progress, void *context, char *path, int path_size, char *error, int error_size);

/* step 2: 1 with the version and path of the VPK to install, 0 when there
is none (or this build has no key: then nothing is removed either) */
int update_download_kept(const struct update_download_config *config, char *version, int version_size, char *path,
	int path_size);

#endif
