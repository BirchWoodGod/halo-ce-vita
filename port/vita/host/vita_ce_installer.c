/*
VITA_CE_INSTALLER.C

Halo Custom Edition's resource maps (bitmaps.map, sounds.map, loc.map) out
of the player's own Custom Edition installer: a halocesetup*.exe copied to
ux0:data/haloce-vita/ (or the maps folder) gives the ones missing from the
maps folder at start-up (vita_video.c, once the memory window and the
display are up), or from Modded maps' "Extract PC files" row
(vita_settings.c), on a thread of its own below the game's
(port/linux/src/posix_ce_installer.c does the work: about 2.7 MB held, the
installer streamed). Its progress is the settings panel's progress line
over the game, which circle cancels; then, the three in place, the player
is asked whether to delete the installer (170 MB). The PC multiplayer
menus look for the maps as ui.map loads (port/linux/game/menu_tags.c), so
the first start's menus are the Xbox's: once the question is answered, the
game restarts itself when the main menu is up and no network game holds it
(halo_main_menu_idle, main.c), or says to restart.
*/

#include <psp2/appmgr.h>
#include <psp2/io/devctl.h>
#include <psp2/kernel/processmgr.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ce_installer.h"
#include "lang.h"
#include "vita_host.h"

#define DATA_DIRECTORY "ux0:data/haloce-vita"
/* (each message in the language chosen: lang.c) */
#define TITLE T("Halo CE: PC map files")
/* what the three need, and a margin */
#define SPACE_NEEDED (166ULL * 1024 * 1024 + 16ULL * 1024 * 1024)

void vita_settings_progress(const char *title, const char *text);
int vita_settings_progress_cancelled(void);
void vita_settings_question(const char *title, const char *text);
int vita_settings_question_answer(void);
/* (main.c) the main menu is up, no network game: a restart loses nothing */
int halo_main_menu_idle(void);

static volatile int running;
static char installer[512];
static unsigned long long installer_size;
static unsigned long long started_us;

static const char *maps_directory(void)
{
	const char *directory = getenv("HALO_MAPS_ROOT");

	return directory && directory[0] ? directory : DATA_DIRECTORY "/maps";
}

/* an installer beside the game, or in the maps folder */
static int installer_find(char *path, int size, unsigned long long *bytes)
{
	return ce_installer_find(DATA_DIRECTORY, path, size, bytes) ||
		ce_installer_find(maps_directory(), path, size, bytes);
}

static const char *file_name(const char *path)
{
	const char *slash = strrchr(path, '/');

	return slash ? slash + 1 : path;
}

static void log_line(const char *format, const char *a, const char *b)
{
	char line[700];

	snprintf(line, sizeof(line), format, a, b);
	vita_host_log(line);
}

static unsigned long long free_space(void)
{
	struct { unsigned long long maximum, free; unsigned int cluster; unsigned int unknown; } space;
	uint64_t maximum = 0, free_bytes = 0;

	memset(&space, 0, sizeof(space));
	if (sceIoDevctl("ux0:", 0x3001, NULL, 0, &space, sizeof(space)) >= 0)
		return space.free;
	if (sceAppMgrGetDevInfo("ux0:", &maximum, &free_bytes) >= 0)
		return free_bytes;
	/* (unknown: a full card shows as a write that fails) */
	return SPACE_NEEDED;
}

static int show_progress(void *context, const char *file, unsigned long long done, unsigned long long total)
{
	char text[1200], left[96] = "", status[192];
	unsigned long long now = sceKernelGetProcessTimeWide(), elapsed = now - started_us;

	(void)context;
	/* (the time left, once there is a rate to go by) */
	if (done && total > done && elapsed > 5000000ULL)
	{
		unsigned long long seconds = (total - done) * (elapsed / 1000ULL) / done / 1000ULL;

		if (seconds >= 90)
			snprintf(left, sizeof(left), T(", about %llu min left"), (seconds + 30) / 60);
		else
			snprintf(left, sizeof(left), T(", about %llu s left"), seconds < 5 ? 5 : seconds);
	}
	if (!file[0] && !done)
		snprintf(status, sizeof(status), "%s", T("Reading the installer..."));
	else
		snprintf(status, sizeof(status), "%s %llu%%%s", file[0] ? file : T("Done:"), total ? done * 100 / total : 100ULL,
			left);
	snprintf(text, sizeof(text),
		T("Taking bitmaps.map, sounds.map and loc.map from %s, for Custom Edition maps and the PC menus.\n\n%s"),
		file_name(installer), status);
	vita_settings_progress(TITLE, text);
	return vita_settings_progress_cancelled();
}

static void extraction_thread(void *argument)
{
	const char *maps = maps_directory();
	int missing = ce_installer_missing(maps), result;
	char error[512], text[2048];
	unsigned long long space = free_space();

	(void)argument;
	if (!missing)
	{
		running = 0;
		return;
	}
	if (space < SPACE_NEEDED)
	{
		snprintf(text, sizeof(text), T("The memory card has %llu MB free; bitmaps.map, sounds.map and loc.map need "
			"about 180 MB. Free some space, then restart the game or use Modded maps' Extract PC files."),
			space >> 20);
		vita_settings_message(TITLE, text);
		log_line("ce installer: %s: not enough space on ux0: (%s)", file_name(installer), "");
		running = 0;
		return;
	}
	log_line("ce installer: taking the resource maps from %s into %s", installer, maps);
	started_us = sceKernelGetProcessTimeWide();
	show_progress(NULL, "", 0, 1);
	result = ce_installer_extract(installer, maps, missing, show_progress, NULL, error, sizeof(error));
	vita_settings_progress(NULL, NULL);
	{
		char line[700];

		snprintf(line, sizeof(line), "ce installer: %s in %llu s%s%s",
			result == CE_INSTALLER_DONE ? "done" : result == CE_INSTALLER_STOPPED ? "stopped" : "failed",
			(sceKernelGetProcessTimeWide() - started_us) / 1000000ULL, error[0] ? ": " : "", error);
		vita_host_log(line);
	}
	if (result == CE_INSTALLER_STOPPED)
		vita_settings_message(TITLE, T("Stopped. The installer stays; the game takes the files from it at the next "
			"start, or from Multiplayer > Modded maps."));
	else if (result != CE_INSTALLER_DONE)
	{
		snprintf(text, sizeof(text), T("The files could not be taken from %s: %s"), file_name(installer), error);
		vita_settings_message(TITLE, text);
	}
	else
	{
		int answer;

		snprintf(text, sizeof(text), T("bitmaps.map, sounds.map and loc.map are in the maps folder: Custom Edition "
			"maps can be played (Modded maps > PC maps), and the PC multiplayer menus come after a restart%s.\n\n"
			"Delete %s (%llu MB) to free the space?"),
			halo_main_menu_idle() ? T(", which follows this question") : "", file_name(installer), installer_size >> 20);
		vita_settings_question(TITLE, text);
		while ((answer = vita_settings_question_answer()) < 0)
			vita_host_sleep_us(100000);
		if (answer == 1)
		{
			if (remove(installer) == 0)
				log_line("ce installer: %s deleted%s", installer, "");
			else
			{
				log_line("ce installer: %s could not be deleted%s", installer, "");
				vita_settings_message(TITLE, T("The installer could not be deleted. Delete it with VitaShell."));
			}
		}
		/* the PC menus are read as ui.map loads: a restart brings them,
		unless a game is under way (the player restarts when it suits) */
		if (halo_main_menu_idle())
		{
			int error_code;

			vita_settings_progress(TITLE, T("Restarting the game for the PC multiplayer menus..."));
			vita_host_log("ce installer: restarting the game for the PC menus");
			vita_host_sleep_us(1500000);
			error_code = sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
			vita_settings_progress(NULL, NULL);
			snprintf(text, sizeof(text), "ce installer: the restart failed (0x%08X)", (unsigned int)error_code);
			vita_host_log(text);
			vita_settings_message(TITLE, T("The game could not restart itself: close it and start it again for the PC "
				"multiplayer menus."));
		}
		else
			vita_settings_message(TITLE, T("Restart Halo after this game to get the PC multiplayer menus."));
	}
	running = 0;
}

int vita_ce_installer_state(char *name, int size)
{
	char path[512];
	unsigned long long bytes;

	if (running)
	{
		snprintf(name, (size_t)size, "%s", file_name(installer));
		return 2;
	}
	if (!ce_installer_missing(maps_directory()) || !installer_find(path, sizeof(path), &bytes))
		return 0;
	snprintf(name, (size_t)size, "%s", file_name(path));
	return 1;
}

int vita_ce_installer_start(void)
{
	if (__atomic_exchange_n(&running, 1, __ATOMIC_ACQ_REL))
		return -1;
	if (!ce_installer_missing(maps_directory()) || !installer_find(installer, sizeof(installer), &installer_size))
	{
		running = 0;
		return -1;
	}
	/* (below the game's threads, on any core) */
	if (vita_host_thread_start_priority("halo ce installer", extraction_thread, NULL, -1, 180) != 0)
	{
		running = 0;
		return -1;
	}
	return 0;
}
