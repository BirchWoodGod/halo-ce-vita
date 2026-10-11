/*
UPDATE_DOWNLOAD_HARNESS.C

The Vita updater's whole path on Linux (port/vita/host/vita_update_download.c
with posix_https.c, Mbed TLS and posix_net.c's sockets), for
run_update_download_test.sh, against update_download_server.py and a folder
standing for ux0:data/haloce-vita.

  update_download_harness ACTION DATA BASE VERSION [CURRENT]

ACTION:
  fetch       update_download_fetch of VERSION
  cancel      the same, the progress stopping it during the download
  nospace     the same with 1 MB free
  kept        update_download_kept (VERSION unused)
  download URL PATH EXPECTED   posix_https_download alone (EXPECTED 0: any
              size), GitHub's redirect allow-list (in place of VERSION
              CURRENT)

CURRENT: the running build's version (default 1.1.0-beta.3); title
HCEV00001. Prints "result <code> <why or path>" (and the updater's log
lines); exit status 0.
*/

#include "vita_update_download.h"
#include "posix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

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
	struct update_download_config config;
	char error[512] = "", path[600] = "", version[64] = "";
	const char *action;
	int result;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc < 5)
		return 2;
	action = argv[1];
	memset(&config, 0, sizeof(config));
	snprintf(config.data, sizeof(config.data), "%s", argv[2]);
	snprintf(config.base, sizeof(config.base), "%s", argv[3]);
	snprintf(config.title, sizeof(config.title), "HCEV00001");
	snprintf(config.current_version, sizeof(config.current_version), "%s", argc > 5 ? argv[5] : "1.1.0-beta.3");
	snprintf(config.user_agent, sizeof(config.user_agent), "HaloCEVita/test");
	config.free_space = free_space;
	config.log = log_line;
	if (!strcmp(action, "download"))
	{
		if (argc < 7)
			return 2;
		result = posix_https_download(argv[4], config.user_agent, argv[5], strtoul(argv[6], NULL, 10), 64UL << 20,
			update_redirect_allowed, NULL, NULL, error, sizeof(error));
		printf("result %d %s\n", result, error);
		return 0;
	}
	if (!strcmp(action, "kept"))
	{
		result = update_download_kept(&config, version, sizeof(version), path, sizeof(path));
		printf("result %d %s %s\n", result, version, path);
		return 0;
	}
	if (!strcmp(action, "nospace"))
		config.free_space = one_megabyte;
	if (!strcmp(action, "cancel"))
		cancel_at_download = 1;
	result = update_download_fetch(&config, argv[4], progress, NULL, path, sizeof(path), error, sizeof(error));
	printf("result %d %s\n", result, result == UPDATE_DOWNLOAD_OK ? path : error);
	return 0;
}
