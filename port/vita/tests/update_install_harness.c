/*
UPDATE_INSTALL_HARNESS.C

The Vita updater's whole path on Linux (port/vita/host/vita_update_install.c
with posix_https.c, Mbed TLS and posix_net.c's sockets), for
run_update_install_test.sh, against update_install_server.py and folders
standing for ux0:data/haloce-vita and ux0:app/HCEV00001; the restart (the
Vita's sceAppMgrLoadExec, vita_update.c) is not part of it.

  update_install_harness ACTION DATA APP BASE VERSION

ACTION:
  install        prepare, then commit
  prepare        prepare, then discard (the download kept)
  cancel         prepare, the progress stopping it during the download
  tamper         prepare, a staged file changed, then commit
  powercut       prepare, then commit stopped after N moves, N given after
                 VERSION (as a power cut leaves it: UPDATE_INSTALL_TEST_HOOKS)
  recover        update_install_recover
  nospace        prepare with 1 MB free
  download URL PATH EXPECTED   posix_https_download alone (EXPECTED 0: any
                 size), GitHub's redirect allow-list

Prints "result <code> <why>" (and the updater's log lines); exit status 0.
The running build is 1.1.0-beta.3, title HCEV00001.
*/

#include "vita_update_install.h"
#include "posix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

extern int update_install_test_stop_after;

static int cancel_at_download;

static void log_line(const char *line)
{
	printf("log %s\n", line);
}

static unsigned long long free_space(const char *path)
{
	struct statvfs information;

	if (statvfs(path, &information) != 0)
		return 0;
	return (unsigned long long)information.f_bavail * information.f_frsize;
}

static unsigned long long one_megabyte(const char *path)
{
	(void)path;
	return 1024 * 1024;
}

static int progress(void *context, int stage, unsigned long done, unsigned long total)
{
	static int last_stage = -1;

	(void)context;
	if (stage != last_stage)
	{
		printf("stage %d %lu/%lu\n", stage, done, total);
		last_stage = stage;
	}
	return cancel_at_download && stage == UPDATE_STAGE_DOWNLOAD && done > 0;
}

int main(int argc, char **argv)
{
	struct update_install_config config;
	struct update_install_staged *staged = NULL;
	char error[512] = "";
	const char *action;
	int result;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc < 6)
		return 2;
	action = argv[1];
	memset(&config, 0, sizeof(config));
	snprintf(config.data, sizeof(config.data), "%s", argv[2]);
	snprintf(config.app, sizeof(config.app), "%s", argv[3]);
	snprintf(config.base, sizeof(config.base), "%s", argv[4]);
	snprintf(config.title, sizeof(config.title), "HCEV00001");
	snprintf(config.current_version, sizeof(config.current_version), "1.1.0-beta.3");
	snprintf(config.user_agent, sizeof(config.user_agent), "HaloCEVita/test");
	config.free_space = free_space;
	config.log = log_line;
	if (!strcmp(action, "download"))
	{
		if (argc < 9)
			return 2;
		result = posix_https_download(argv[6], config.user_agent, argv[7], strtoul(argv[8], NULL, 10), 64UL << 20,
			update_redirect_allowed, NULL, NULL, error, sizeof(error));
		printf("result %d %s\n", result, error);
		return 0;
	}
	if (!strcmp(action, "recover"))
	{
		result = update_install_recover(&config);
		printf("result %d\n", result);
		return 0;
	}
	if (!strcmp(action, "nospace"))
		config.free_space = one_megabyte;
	if (!strcmp(action, "cancel"))
		cancel_at_download = 1;
	result = update_install_prepare(&config, argv[5], progress, NULL, &staged, error, sizeof(error));
	if (result != UPDATE_INSTALL_OK)
	{
		printf("result %d %s\n", result, error);
		return 0;
	}
	if (!strcmp(action, "prepare"))
	{
		update_install_discard(&config, staged);
		printf("result %d prepared\n", result);
		return 0;
	}
	if (!strcmp(action, "tamper"))
	{
		char path[600];
		FILE *file;

		snprintf(path, sizeof(path), "%s/update/stage/lang/es.txt", config.data);
		file = fopen(path, "ab");
		if (file)
		{
			fputs("tampered", file);
			fclose(file);
		}
	}
	if (!strcmp(action, "powercut"))
		update_install_test_stop_after = argc > 6 ? atoi(argv[6]) : 1;
	result = update_install_commit(&config, staged, progress, NULL, error, sizeof(error));
	printf("result %d %s\n", result, error);
	return 0;
}
