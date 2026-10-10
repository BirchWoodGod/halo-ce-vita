/*
VITA_UPDATE_INSTALL.H

The Vita's updater's work (vita_update_install.c), with no screen in it:
vita_update.c drives it from the settings panel on the Vita, and
tests/update_install_test.c on Linux, against a local server and folders of
its own standing in for ux0:.

1. update_install_prepare: the release's manifest and signature downloaded
   (posix_https_download, from the release's fixed folder on GitHub) and
   checked (vita_update_verify.h); the space on the memory card checked;
   halo.vpk downloaded (resumed when a download of the same signed file was
   cut short), its size and SHA-512 checked against the manifest, and
   unpacked into the staging folder (<data>/update/stage), each name checked,
   each file's CRC, sizes and count bounded, its param.sfo's title checked.
2. update_install_commit: each staged file checked again (SHA-512, taken as
   it was unpacked), a journal written, then the game's files moved aside
   (<data>/update/backup) and the new ones moved into the game's folder
   (ux0:app/<title>), eboot.bin last. Any failure moves every file back.
3. The caller restarts the game (vita_update.c: sceAppMgrLoadExec).

update_install_recover, at start-up, finishes what a power cut or a crash
during step 2 left: with a journal there, every file is moved back as it
was. Nothing outside <data>/update and the game's own files is touched (the
data folder's saves, settings.txt, config.toml, maps and movies stay). A
failure removes what was downloaded and unpacked, but for a download cut
short by the network, kept to resume.

Writing into ux0:app needs HENkaku's "Enable Unsafe Homebrew" (the same
access VitaShell and VitaDB Downloader have); update_install_prepare looks
first and says so (UPDATE_INSTALL_NO_ACCESS).
*/

#ifndef VITA_UPDATE_INSTALL_H
#define VITA_UPDATE_INSTALL_H

#include "vita_update_verify.h"

enum
{
	UPDATE_INSTALL_OK = 0,
	UPDATE_INSTALL_FAILED = -1,
	UPDATE_INSTALL_CANCELLED = -2,
	/* the placeholder key: this build installs no update */
	UPDATE_INSTALL_NO_KEY = -3,
	/* the game's folder cannot be written (no unsafe homebrew) */
	UPDATE_INSTALL_NO_ACCESS = -4,
	UPDATE_INSTALL_NO_SPACE = -5,
	UPDATE_INSTALL_NO_CONNECTION = -6,
};

/* what the progress hears */
enum
{
	UPDATE_STAGE_MANIFEST,
	UPDATE_STAGE_DOWNLOAD,
	UPDATE_STAGE_VERIFY,
	UPDATE_STAGE_UNPACK,
	UPDATE_STAGE_INSTALL,
};

/* done of total in the stage; nonzero stops the work (heeded before the
install moves anything) */
typedef int (*update_install_progress_proc)(void *context, int stage, unsigned long done, unsigned long total);
typedef void (*update_install_log_proc)(const char *line);

struct update_install_config
{
	/* the data folder (ux0:data/haloce-vita): <data>/update is the updater's */
	char data[256];
	/* the game's folder (ux0:app/<title>) */
	char app[256];
	/* the running game's title ID and version */
	char title[16];
	char current_version[64];
	/* where the release's files are (UPDATE_DOWNLOAD_BASE; the tests' server) */
	char base[256];
	char user_agent[64];
	/* the free bytes of the memory card (NULL: not checked) */
	unsigned long long (*free_space)(const char *path);
	update_install_log_proc log;
};

/* the files unpacked, between prepare and commit */
struct update_install_staged;

/* steps 1 (above) for version (a tag without its v, newer than the running
build): UPDATE_INSTALL_OK with the staged files in *staged (for commit, or
update_install_discard), else a code, why in error, nothing staged and
nothing of the game touched */
int update_install_prepare(const struct update_install_config *config, const char *version,
	update_install_progress_proc progress, void *context, struct update_install_staged **staged, char *error,
	int error_size);

/* step 2: UPDATE_INSTALL_OK with the new version in the game's folder (the
staging, backup and download removed), else UPDATE_INSTALL_FAILED with why
in error and the game's folder as it was; staged is freed either way */
int update_install_commit(const struct update_install_config *config, struct update_install_staged *staged,
	update_install_progress_proc progress, void *context, char *error, int error_size);

/* the staged files forgotten and removed, the download kept (a later
prepare checks it again rather than downloading it again) */
void update_install_discard(const struct update_install_config *config, struct update_install_staged *staged);

/* at start-up: a commit cut short undone (the journal's files moved back),
and a staging or backup folder left over removed; the number of the game's
files put back (>0: the caller restarts the game, whose files have just
changed), 0 when there was nothing to undo, -1 when files could not be put
back (logged) */
int update_install_recover(const struct update_install_config *config);

#endif
