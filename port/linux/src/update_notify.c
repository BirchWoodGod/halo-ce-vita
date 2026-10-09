/*
UPDATE_NOTIFY.C

The game's side of the update check (update_check.h): the player's press of
the settings panel's "Check for updates" (vita_settings.c; the tests' @update
command, main.c) starts one look on a thread of its own, which the game and
the panel never wait for; the panel shows the status as it changes, halo.log
gets one line, and a newer version found shows in the main menu's corner
until the game restarts (ui_widget_game_data_input_functions.c).

Nothing is asked of the network unless the player asks. A look is kept in
update_check.txt in the data folder (HALO_DATA_ROOT, where the Vita's
config.toml is; else beside config.toml): a
press within UPDATE_RECHECK_SECONDS of the last look that reached GitHub is
answered from it, so pressing again and again (or restarting and pressing)
does not run into GitHub's 60 requests an hour. Without a network (ad hoc
play, or no address) the look stops before any request: "no connection".

(tests) HALO_UPDATE_URL asks another URL instead of GitHub's - only an
http:// one to this machine (127.x.x.x, the tests' server) or another of
api.github.com.
*/

#include "lang.h"
#include "platform.h"
#include "port_config.h"
#include "posix.h"
#include "update_check.h"
#include "../../vita/include/vita_version.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CACHE_FILE "update_check.txt"
/* (fixed: the build's version and nothing about the player or the Vita) */
#define USER_AGENT "HaloCEVita/" HALO_VITA_VERSION
/* (the thread's: Mbed TLS's handshake runs on it) */
#define THREAD_STACK_SIZE (256 * 1024)

static pthread_mutex_t update_lock = PTHREAD_MUTEX_INITIALIZER;
static int update_running;
static int update_status = UPDATE_STATUS_NONE;
static int update_status_channel;
static char update_version[UPDATE_VERSION_SIZE];
static int update_menu_logged;

/* the Update channel: the setting, unless this build may not use it (a
pre-release on Stable: Experimental, said once) */
static int current_channel(void)
{
	static int refusal_logged;
	int fallback = update_channel_default(HALO_VITA_VERSION);
	int channel = update_channel_parse(getenv("HALO_UPDATE_CHANNEL"), fallback);

	if (!update_channel_allowed(channel, HALO_VITA_VERSION))
	{
		if (!refusal_logged)
		{
			refusal_logged = 1;
			platform_log("update: the Update channel %s is not for a pre-release build (%s): %s", update_channel_name(channel),
				HALO_VITA_VERSION, update_channel_name(fallback));
		}
		channel = fallback;
	}
	return channel;
}

/* in the data folder (HALO_DATA_ROOT: on the Vita, where config.toml is
too), else beside config.toml */
static void cache_path(char *path, size_t size)
{
	const char *root = getenv("HALO_DATA_ROOT");

	if (root && *root)
		snprintf(path, size, "%s/%s", root, CACHE_FILE);
	else
	{
		config_folder(path, size);
		snprintf(path + strlen(path), size - strlen(path), "%s", CACHE_FILE);
	}
}

static int cache_read(struct update_cache *cache)
{
	char path[1024];
	size_t size = 0;
	char *text;
	int ok;

	cache_path(path, sizeof(path));
	text = config_file_read(path, &size);
	if (!text)
		return 0;
	ok = update_cache_parse(text, size, cache);
	free(text);
	return ok;
}

static void cache_write(const struct update_cache *cache)
{
	char path[1024], text[256];
	int length = update_cache_format(text, sizeof(text), cache);

	cache_path(path, sizeof(path));
	if (length <= 0 || !config_file_write(path, text, (size_t)length))
		platform_log("update: cannot write %s", path);
}

/* the status a look's answer gives, and the version it is about */
static int status_of(const struct update_cache *cache, char *version, size_t size)
{
	version[0] = 0;
	if (!cache->ok)
		return UPDATE_STATUS_FAILED;
	if (cache->latest[0] && update_is_newer(cache->latest, HALO_VITA_VERSION))
	{
		snprintf(version, size, "%s", cache->latest);
		return UPDATE_STATUS_AVAILABLE;
	}
	snprintf(version, size, "%s", HALO_VITA_VERSION);
	return UPDATE_STATUS_UP_TO_DATE;
}

static void status_set(int status, int channel, const char *version)
{
	pthread_mutex_lock(&update_lock);
	update_status = status;
	update_status_channel = channel;
	snprintf(update_version, sizeof(update_version), "%s", version);
	update_running = 0;
	pthread_mutex_unlock(&update_lock);
}

static void log_answer(int status, int channel, const char *version, const char *how)
{
	if (status == UPDATE_STATUS_AVAILABLE)
		platform_log("update: %s is available (this is %s; Update channel %s%s): " UPDATE_RELEASES_PAGE, version,
			HALO_VITA_VERSION, update_channel_name(channel), how);
	else if (status == UPDATE_STATUS_UP_TO_DATE)
		platform_log("update: up to date (%s; Update channel %s%s)", HALO_VITA_VERSION, update_channel_name(channel), how);
}

/* the URL asked: GitHub's for the channel, or the tests' (HALO_UPDATE_URL) */
static const char *request_url(int channel, int *loopback)
{
	const char *test = getenv("HALO_UPDATE_URL");

	*loopback = 0;
	if (test && *test)
	{
		if (!strncmp(test, "http://127.", 11))
		{
			*loopback = 1;
			return test;
		}
		if (!strncmp(test, "https://api.github.com/", 23))
			return test;
		platform_log("update: HALO_UPDATE_URL is neither this machine's nor api.github.com's: GitHub's is asked");
	}
	return channel == UPDATE_CHANNEL_EXPERIMENTAL ? UPDATE_URL_ALL : UPDATE_URL_STABLE;
}

/* whether this machine is on a network that can reach the internet: an
address, not ad hoc play's stand-in (169.254.x.x) */
static int network_up(void)
{
	const char *network = getenv("HALO_VITA_NETWORK");
	unsigned long address;

	if (network && !strcmp(network, "adhoc"))
		return 0;
	address = posix_local_ipv4_address();
	return address && (address & 0xFFFF) != (169u | 254u << 8);
}

static void *update_thread(void *argument)
{
	int channel = (int)(long)argument, loopback;
	const char *url = request_url(channel, &loopback);
	struct update_cache cache;
	char error[256], version[UPDATE_VERSION_SIZE];
	char *body;
	int length, status;

	if (!loopback && !network_up())
	{
		platform_log("update: could not check: no internet connection");
		status_set(UPDATE_STATUS_NO_CONNECTION, channel, "");
		return NULL;
	}
	body = malloc(UPDATE_RESPONSE_MAXIMUM);
	if (!body)
	{
		platform_log("update: could not check: out of memory");
		status_set(UPDATE_STATUS_FAILED, channel, "");
		return NULL;
	}
	memset(&cache, 0, sizeof(cache));
	cache.checked = (long long)time(NULL);
	cache.channel = channel;
	length = posix_https_get(url, USER_AGENT, body, UPDATE_RESPONSE_MAXIMUM, error, sizeof(error));
	if (length < 0)
	{
		/* (no name lookup or no connection: as good as no network) */
		int offline = !strncmp(error, "cannot look up", 14) || !strncmp(error, "cannot connect", 14);

		platform_log("update: could not check: %s", error);
		free(body);
		if (offline)
		{
			status_set(UPDATE_STATUS_NO_CONNECTION, channel, "");
			return NULL;
		}
		cache_write(&cache);
		status_set(UPDATE_STATUS_FAILED, channel, "");
		return NULL;
	}
	switch (update_releases_pick(body, (size_t)length, channel, cache.latest, sizeof(cache.latest)))
	{
	case -1:
		platform_log("update: could not check: the answer (%d bytes) is not the releases list", length);
		break;
	case 0:
		cache.ok = 1;
		platform_log("update: no release for the Update channel %s in the answer", update_channel_name(channel));
		break;
	default:
		cache.ok = 1;
		break;
	}
	free(body);
	cache_write(&cache);
	status = status_of(&cache, version, sizeof(version));
	log_answer(status, channel, version, "");
	status_set(status, channel, version);
	return NULL;
}

int update_check_request(void)
{
	int channel = current_channel();
	struct update_cache cache;
	long long now = (long long)time(NULL);
	pthread_t thread;
	pthread_attr_t attributes;
	int started;

	pthread_mutex_lock(&update_lock);
	if (update_running)
	{
		pthread_mutex_unlock(&update_lock);
		return UPDATE_STATUS_CHECKING;
	}
	/* (a recent look's answer again: GitHub is not asked) */
	if (cache_read(&cache) && cache.channel == channel && update_cache_fresh(cache.checked, now, UPDATE_RECHECK_SECONDS))
	{
		char version[UPDATE_VERSION_SIZE], how[48];
		int status = status_of(&cache, version, sizeof(version));

		update_status = status;
		update_status_channel = channel;
		snprintf(update_version, sizeof(update_version), "%s", version);
		pthread_mutex_unlock(&update_lock);
		snprintf(how, sizeof(how), "; the look of %lld s ago", now - cache.checked);
		if (status == UPDATE_STATUS_FAILED)
			platform_log("update: could not check (the look of %lld s ago)", now - cache.checked);
		log_answer(status, channel, version, how);
		return status;
	}
	update_running = 1;
	update_status = UPDATE_STATUS_CHECKING;
	update_status_channel = channel;
	update_version[0] = 0;
	pthread_mutex_unlock(&update_lock);
	platform_log("update: checking for a new version (Update channel %s)", update_channel_name(channel));
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, THREAD_STACK_SIZE);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	started = pthread_create(&thread, &attributes, update_thread, (void *)(long)channel) == 0;
	pthread_attr_destroy(&attributes);
	if (!started)
	{
		platform_log("update: could not check: no thread");
		status_set(UPDATE_STATUS_FAILED, channel, "");
		return UPDATE_STATUS_FAILED;
	}
	return UPDATE_STATUS_CHECKING;
}

int update_check_status(char *version, int size)
{
	int channel = current_channel(), status;

	pthread_mutex_lock(&update_lock);
	status = update_status;
	if (status != UPDATE_STATUS_CHECKING && update_status_channel != channel)
		status = UPDATE_STATUS_NONE;
	if (version && size > 0)
		snprintf(version, (size_t)size, "%s", status == UPDATE_STATUS_NONE ? "" : update_version);
	pthread_mutex_unlock(&update_lock);
	return status;
}

int update_check_menu_text(unsigned short *text, int count)
{
	char version[UPDATE_VERSION_SIZE], line[160];
	const unsigned char *cursor;
	int length = 0;

	if (count <= 0)
		return 0;
	text[0] = 0;
	if (update_check_status(version, sizeof(version)) != UPDATE_STATUS_AVAILABLE)
		return 0;
	/* (short: the build number's text box is about 24 characters wide) */
	snprintf(line, sizeof(line), T("Update: %s"), version);
	if (!update_menu_logged)
	{
		update_menu_logged = 1;
		platform_log("update: the main menu shows \"%s\"", line);
	}
	/* (UTF-8 to the game's UTF-16) */
	for (cursor = (const unsigned char *)line; *cursor && length + 1 < count; length++)
	{
		unsigned int code = *cursor++;

		if (code >= 0xE0 && code < 0xF0 && (cursor[0] & 0xC0) == 0x80 && (cursor[1] & 0xC0) == 0x80)
		{
			code = (code & 0x0F) << 12 | (cursor[0] & 0x3Fu) << 6 | (cursor[1] & 0x3Fu);
			cursor += 2;
		}
		else if (code >= 0xC2 && code < 0xE0 && (cursor[0] & 0xC0) == 0x80)
		{
			code = (code & 0x1F) << 6 | (cursor[0] & 0x3Fu);
			cursor++;
		}
		else if (code >= 0x80)
		{
			code = '?';
		}
		text[length] = (unsigned short)code;
	}
	text[length] = 0;
	return 1;
}
