/*
VITA_UPDATE.C

The update installed from the settings panel (vita_update.h): the player's
second press of Check for updates, once it found a newer version, asks
whether to download and install it (the panel's question over the game),
and on yes, on a thread below the game's:

- only from the main menu with no network game (halo_main_menu_idle, as the
  Custom Edition installer's restart): the install ends in a restart;
- vita_update_install.c's prepare: the release's signed manifest, halo.vpk,
  its checks and unpacking, with the panel's progress line (circle stops it);
- then its commit (the progress line stays; it cannot be stopped) and the
  restart into the new version (sceAppMgrLoadExec of app0:eboot.bin, as
  VitaDB Downloader restarts itself after its update);
- any failure: a message saying why, the game as it was, a halo.log line.

Builds holding the placeholder key (vita_update_key.h) refuse at once and
say to install new versions with VitaShell; a Vita without HENkaku's unsafe
homebrew is told how to turn it on.
*/

#include <psp2/appmgr.h>
#include <psp2/io/devctl.h>
#include <psp2/kernel/processmgr.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lang.h"
#include "update_check.h"
#include "vita_host.h"
#include "vita_update.h"
#include "vita_update_install.h"
#include "vita_version.h"

#define DATA_DIRECTORY "ux0:data/haloce-vita"
#define TITLE T("Halo CE: update")

void vita_settings_progress(const char *title, const char *text);
int vita_settings_progress_cancelled(void);
void vita_settings_question(const char *title, const char *text);
int vita_settings_question_answer(void);
void vita_settings_message(const char *title, const char *text);
/* (main.c) the main menu is up, no network game: a restart loses nothing */
int halo_main_menu_idle(void);

static volatile int running;
static char update_version[UPDATE_VERSION_SIZE];

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
	/* (unknown: a full card shows as a write that fails, which is undone) */
	return ~0ULL;
}

/* the paths, the running game's title and version */
static int configure(struct update_install_config *config)
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
	snprintf(config->app, sizeof(config->app), "ux0:app/%s", title);
	snprintf(config->current_version, sizeof(config->current_version), "%s", HALO_VITA_VERSION);
	snprintf(config->base, sizeof(config->base), "%s", UPDATE_DOWNLOAD_BASE);
	{
		/* (tests: HALO_UPDATE_DOWNLOAD_BASE, another repository's releases
		on GitHub or a loopback http:// server, Vita3K's; only what
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
	snprintf(config->user_agent, sizeof(config->user_agent), "HaloCEVita/" HALO_VITA_VERSION);
	config->free_space = free_space;
	config->log = log_line;
	return 1;
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
	case UPDATE_STAGE_VERIFY:
		snprintf(status, sizeof(status), T("Checking the download: %lu%%"), percent);
		break;
	case UPDATE_STAGE_UNPACK:
		snprintf(status, sizeof(status), T("Unpacking: %lu%%"), percent);
		break;
	default:
		snprintf(status, sizeof(status), T("Installing: %lu of %lu files. Do not turn the Vita off."), done, total);
		break;
	}
	snprintf(text, sizeof(text), T("Halo CE %s\n\n%s"), update_version, status);
	vita_settings_progress(TITLE, text);
	return stage < UPDATE_STAGE_INSTALL && vita_settings_progress_cancelled();
}

/* the message for a failure */
static void say_failure(int result, const char *error)
{
	char text[1024];

	switch (result)
	{
	case UPDATE_INSTALL_CANCELLED:
		snprintf(text, sizeof(text), "%s", T("Stopped. Nothing was installed."));
		break;
	case UPDATE_INSTALL_NO_KEY:
		snprintf(text, sizeof(text), T("This build cannot install updates itself (it has no update key). Install %s "
			"with VitaShell from %s."), update_version, UPDATE_RELEASES_PAGE);
		break;
	case UPDATE_INSTALL_NO_ACCESS:
		snprintf(text, sizeof(text), T("Halo cannot write its own folder. Turn on Enable Unsafe Homebrew in HENkaku "
			"Settings (Settings > HENkaku Settings), then try again, or install %s with VitaShell from %s."),
			update_version, UPDATE_RELEASES_PAGE);
		break;
	case UPDATE_INSTALL_NO_SPACE:
		snprintf(text, sizeof(text), T("Not enough space on the memory card: %s. Nothing was installed."), error);
		break;
	case UPDATE_INSTALL_NO_CONNECTION:
		snprintf(text, sizeof(text), T("Could not reach GitHub (%s). Nothing was installed; try again later."), error);
		break;
	default:
		snprintf(text, sizeof(text), T("The update was refused or failed: %s. Halo is unchanged."), error);
		break;
	}
	vita_settings_message(TITLE, text);
}

static void update_thread(void *argument)
{
	struct update_install_config config;
	struct update_install_staged *staged = NULL;
	char text[1024], error[512] = "", line[700];
	int answer, result, error_code;

	(void)argument;
	snprintf(text, sizeof(text), T("Halo CE %s is out (this is %s).\n\nDownload and install it now? Its signature is "
		"checked before anything is installed, then the game restarts into it. Saves, settings and maps stay."),
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
		say_failure(UPDATE_INSTALL_FAILED, "this app's title ID is unknown");
		running = 0;
		return;
	}
	show_progress(NULL, UPDATE_STAGE_MANIFEST, 0, 1);
	result = update_install_prepare(&config, update_version, show_progress, NULL, &staged, error, sizeof(error));
	if (result != UPDATE_INSTALL_OK)
	{
		vita_settings_progress(NULL, NULL);
		say_failure(result, error);
		running = 0;
		return;
	}
	/* (the restart that ends it: only with nothing to lose) */
	if (!halo_main_menu_idle())
	{
		vita_settings_progress(NULL, NULL);
		update_install_discard(&config, staged);
		vita_host_log("update: downloaded and checked, put off: a game is under way");
		snprintf(text, sizeof(text), T("%s is downloaded and checked. Return to the main menu, then press Check for "
			"updates again to install it (the game restarts)."), update_version);
		vita_settings_message(TITLE, text);
		running = 0;
		return;
	}
	result = update_install_commit(&config, staged, show_progress, NULL, error, sizeof(error));
	if (result != UPDATE_INSTALL_OK)
	{
		vita_settings_progress(NULL, NULL);
		say_failure(result, error);
		running = 0;
		return;
	}
	snprintf(text, sizeof(text), T("Halo CE %s is installed. Restarting..."), update_version);
	vita_settings_progress(TITLE, text);
	snprintf(line, sizeof(line), "update: %s installed; restarting into it", update_version);
	vita_host_log(line);
	vita_host_sleep_us(1500000);
	error_code = sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
	vita_settings_progress(NULL, NULL);
	snprintf(line, sizeof(line), "update: the restart failed (0x%08X)", (unsigned int)error_code);
	vita_host_log(line);
	snprintf(text, sizeof(text), T("Halo CE %s is installed. Close Halo and start it again to play it."), update_version);
	vita_settings_message(TITLE, text);
	running = 0;
}

int vita_update_start(void)
{
	char version[UPDATE_VERSION_SIZE];

	if (__atomic_exchange_n(&running, 1, __ATOMIC_ACQ_REL))
		return -1;
	if (update_check_status(version, sizeof(version)) != UPDATE_STATUS_AVAILABLE)
	{
		running = 0;
		return -1;
	}
	snprintf(update_version, sizeof(update_version), "%s", version);
	if (!halo_main_menu_idle())
	{
		vita_settings_message(TITLE, T("Updates install from the main menu, between games: the game restarts at the "
			"end. Leave this game, then press Check for updates again."));
		running = 0;
		return 0;
	}
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

void vita_update_recover_at_start(void)
{
	struct update_install_config config;

	if (!configure(&config))
		return;
	if (update_install_recover(&config) > 0)
	{
		int error_code;
		char line[96];

		/* (the game's files were put back: the old game, started afresh) */
		vita_host_log("update: an install cut short was undone; restarting");
		error_code = sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
		snprintf(line, sizeof(line), "update: the restart failed (0x%08X)", (unsigned int)error_code);
		vita_host_log(line);
	}
}
