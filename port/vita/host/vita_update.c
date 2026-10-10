/*
VITA_UPDATE.C

The update downloaded from the settings panel (vita_update.h): the
player's second press of Check for updates, once it found a newer version,
asks whether to download it (the panel's question over the game), and on
yes, on a thread below the game's (any time: a download of a few MB on a
low-priority thread does not cost the game frames), runs
vita_update_download.c's fetch with the panel's progress line (circle stops
it): the release's signed manifest checked, halo.vpk downloaded and its
SHA-512 checked, then kept as ux0:data/haloce-vita/update/halo-<version>.vpk.
The game is a safe homebrew (vita-make-fself -s) and cannot write its own
folder: the message says to install that file with VitaShell, and the panel
and the main menu's corner say so until the player has (each start checks
the kept file again: vita_update_scan_at_start).

Builds holding the placeholder key (vita_update_key.h) refuse at once and
point to the releases page. Only a test build (HALO_UPDATE_TEST_SOURCES,
tools/release/README.md) takes HALO_UPDATE_DOWNLOAD_BASE from env.txt; a
release build downloads from BirchWoodGod/halo-ce-vita alone.
*/

#include <psp2/appmgr.h>
#include <psp2/io/devctl.h>
#include <psp2/kernel/processmgr.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lang.h"
#include "update_check.h"
#include "vita_host.h"
#include "vita_update.h"
#include "vita_update_download.h"
#include "vita_version.h"

#define DATA_DIRECTORY "ux0:data/haloce-vita"
#define TITLE T("Halo CE: update")

void vita_settings_progress(const char *title, const char *text);
int vita_settings_progress_cancelled(void);
void vita_settings_question(const char *title, const char *text);
int vita_settings_question_answer(void);
void vita_settings_message(const char *title, const char *text);

static volatile int running;
static char update_version[UPDATE_VERSION_SIZE];
/* the downloaded and verified VPK to install */
static pthread_mutex_t ready_lock = PTHREAD_MUTEX_INITIALIZER;
static char ready_version[UPDATE_VERSION_SIZE];
static char ready_path[512];
/* (one look at the update folder at a time: the start's and a download) */
static pthread_mutex_t work_lock = PTHREAD_MUTEX_INITIALIZER;

static void log_line(const char *line)
{
	vita_host_log(line);
}

static unsigned long long free_space(const char *path)
{
	struct { unsigned long long maximum, free; unsigned int cluster; unsigned int unknown; } space;
	uint64_t maximum = 0, free_bytes = 0;
	char device[8];
	const char *colon = strchr(path, ':');

	/* (the device of path: "ux0:") */
	snprintf(device, sizeof(device), "%.*s", colon && colon - path < 6 ? (int)(colon - path + 1) : 4,
		colon ? path : "ux0:");
	memset(&space, 0, sizeof(space));
	if (sceIoDevctl(device, 0x3001, NULL, 0, &space, sizeof(space)) >= 0)
		return space.free;
	if (sceAppMgrGetDevInfo(device, &maximum, &free_bytes) >= 0)
		return free_bytes;
	/* (unknown: a full card shows as a write that fails) */
	return ~0ULL;
}

/* the folder, the running game's title and version, where the files are */
static int configure(struct update_download_config *config)
{
	const char *data = getenv("HALO_DATA_ROOT");
	char title[256];

	memset(config, 0, sizeof(*config));
	memset(title, 0, sizeof(title));
	/* (the running app's TITLE_ID: SCE_APPMGR_APP_PARAM_TITLE_ID) */
	if (sceAppMgrAppParamGetString(0, 12, title, sizeof(title)) < 0 || strlen(title) != 9)
		return 0;
	snprintf(config->title, sizeof(config->title), "%s", title);
	snprintf(config->data, sizeof(config->data), "%s", data && data[0] ? data : DATA_DIRECTORY);
	snprintf(config->current_version, sizeof(config->current_version), "%s", HALO_VITA_VERSION);
	snprintf(config->base, sizeof(config->base), "%s", UPDATE_DOWNLOAD_BASE);
#ifdef HALO_UPDATE_TEST_SOURCES
	{
		/* (a test build: HALO_UPDATE_DOWNLOAD_BASE, another repository's
		releases on GitHub or a loopback http:// server, Vita3K's; only what
		update_asset_url takes, and the files still need this build's key) */
		const char *base = getenv("HALO_UPDATE_DOWNLOAD_BASE");
		char probe[600];

		if (base && base[0])
		{
			if (strlen(base) < sizeof(config->base) &&
				update_asset_url(probe, sizeof(probe), base, "1.0.0", UPDATE_ASSET_MANIFEST))
			{
				snprintf(config->base, sizeof(config->base), "%s", base);
			}
			else
				vita_host_log("update: HALO_UPDATE_DOWNLOAD_BASE is not one taken: GitHub's own is used");
		}
	}
#endif
	snprintf(config->user_agent, sizeof(config->user_agent), "HaloCEVita/" HALO_VITA_VERSION);
	config->free_space = free_space;
	config->log = log_line;
	return 1;
}

static void ready_set(const char *version, const char *path)
{
	pthread_mutex_lock(&ready_lock);
	snprintf(ready_version, sizeof(ready_version), "%s", version);
	snprintf(ready_path, sizeof(ready_path), "%s", path);
	pthread_mutex_unlock(&ready_lock);
	/* (the main menu's corner: update_notify.c) */
	update_check_set_ready(version);
}

int vita_update_ready(char *version, int version_size, char *path, int path_size)
{
	int ready;

	pthread_mutex_lock(&ready_lock);
	ready = ready_version[0] && update_is_newer(ready_version, HALO_VITA_VERSION);
	if (version && version_size > 0)
		snprintf(version, (size_t)version_size, "%s", ready ? ready_version : "");
	if (path && path_size > 0)
		snprintf(path, (size_t)path_size, "%s", ready ? ready_path : "");
	pthread_mutex_unlock(&ready_lock);
	return ready;
}

static int show_progress(void *context, int stage, unsigned long done, unsigned long total)
{
	char text[512], status[160];
	unsigned long percent = total ? (unsigned long)((unsigned long long)done * 100 / total) : 0;

	(void)context;
	/* (no sleep while it works) */
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
	switch (stage)
	{
	case UPDATE_STAGE_MANIFEST:
		snprintf(status, sizeof(status), "%s", T("Fetching the signed manifest..."));
		break;
	case UPDATE_STAGE_DOWNLOAD:
		snprintf(status, sizeof(status), T("Downloading: %lu.%lu of %lu.%lu MB (%lu%%)"), done >> 20,
			(done & 0xFFFFF) * 10 >> 20, total >> 20, (total & 0xFFFFF) * 10 >> 20, percent);
		break;
	default:
		snprintf(status, sizeof(status), T("Checking the download: %lu%%"), percent);
		break;
	}
	snprintf(text, sizeof(text), T("Halo CE %s\n\n%s"), update_version, status);
	vita_settings_progress(TITLE, text);
	return vita_settings_progress_cancelled();
}

/* where the kept VPK is, as the message says it */
static void say_ready(const char *version, const char *path)
{
	char text[1024];

	snprintf(text, sizeof(text), T("%s downloaded and verified. Install it with VitaShell: %s"), version, path);
	vita_settings_message(TITLE, text);
}

/* the message for a failure */
static void say_failure(int result, const char *error)
{
	char text[1024];

	switch (result)
	{
	case UPDATE_DOWNLOAD_CANCELLED:
		snprintf(text, sizeof(text), "%s", T("Stopped. Nothing was kept."));
		break;
	case UPDATE_DOWNLOAD_NO_KEY:
		snprintf(text, sizeof(text), T("This build cannot check updates (it has no update key). Get %s from %s and "
			"install it with VitaShell."), update_version, UPDATE_RELEASES_PAGE);
		break;
	case UPDATE_DOWNLOAD_NO_SPACE:
		snprintf(text, sizeof(text), T("Not enough space on the memory card: %s. Nothing was kept."), error);
		break;
	case UPDATE_DOWNLOAD_NO_CONNECTION:
		snprintf(text, sizeof(text), T("Could not reach GitHub (%s). Try again later."), error);
		break;
	default:
		snprintf(text, sizeof(text), T("The update was refused or failed: %s. Nothing was kept."), error);
		break;
	}
	vita_settings_message(TITLE, text);
}

static void update_thread(void *argument)
{
	struct update_download_config config;
	char text[1024], error[512] = "", path[512];
	int answer, result;

	(void)argument;
	snprintf(text, sizeof(text), T("Halo CE %s is out (this is %s).\n\nDownload it now? Its signature is checked, then "
		"it is kept in the data folder for you to install with VitaShell. Saves, settings and maps stay."),
		update_version, HALO_VITA_VERSION);
	vita_settings_question(TITLE, text);
	while ((answer = vita_settings_question_answer()) < 0)
		vita_host_sleep_us(100000);
	if (answer != 1)
	{
		vita_host_log("update: the player said not now");
		running = 0;
		return;
	}
	if (!configure(&config))
	{
		vita_host_log("update: cannot read this app's title ID");
		say_failure(UPDATE_DOWNLOAD_FAILED, "this app's title ID is unknown");
		running = 0;
		return;
	}
	show_progress(NULL, UPDATE_STAGE_MANIFEST, 0, 1);
	pthread_mutex_lock(&work_lock);
	result = update_download_fetch(&config, update_version, show_progress, NULL, path, sizeof(path), error,
		sizeof(error));
	pthread_mutex_unlock(&work_lock);
	vita_settings_progress(NULL, NULL);
	if (result != UPDATE_DOWNLOAD_OK)
		say_failure(result, error);
	else
	{
		ready_set(update_version, path);
		say_ready(update_version, path);
	}
	running = 0;
}

int vita_update_start(void)
{
	char version[UPDATE_VERSION_SIZE], ready[UPDATE_VERSION_SIZE], path[512];

	if (__atomic_exchange_n(&running, 1, __ATOMIC_ACQ_REL))
		return 0;
	if (update_check_status(version, sizeof(version)) != UPDATE_STATUS_AVAILABLE)
	{
		running = 0;
		return -1;
	}
	/* (downloaded already: where it is, again) */
	if (vita_update_ready(ready, sizeof(ready), path, sizeof(path)) && !update_is_newer(version, ready))
	{
		running = 0;
		say_ready(ready, path);
		return 0;
	}
	snprintf(update_version, sizeof(update_version), "%s", version);
	/* (below the game's threads, on any core) */
	if (vita_host_thread_start_priority("halo update", update_thread, NULL, -1, 180) != 0)
	{
		running = 0;
		return -1;
	}
	return 0;
}

int vita_update_running(void)
{
	return running;
}

static void scan_thread(void *argument)
{
	struct update_download_config config;
	char version[UPDATE_VERSION_SIZE], path[512];

	(void)argument;
	pthread_mutex_lock(&work_lock);
	if (configure(&config) && update_download_kept(&config, version, sizeof(version), path, sizeof(path)))
		ready_set(version, path);
	pthread_mutex_unlock(&work_lock);
}

void vita_update_scan_at_start(void)
{
	/* (checking a kept VPK's SHA-512 takes a moment: not on the start's way) */
	if (vita_host_thread_start_priority("halo update scan", scan_thread, NULL, -1, 190) != 0)
		vita_host_log("update: cannot start the check of a kept update");
}
