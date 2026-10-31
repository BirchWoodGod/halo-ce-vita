/*
NETVERSION_FETCH.C

The signed crossplay version table's fetch and keeping (netversion_fetch.h).

At start (WSAStartup, xnet.c) the table kept on the memory card is read and
checked (netversion_table.c) and, when it checks, put in effect
(network_wire_table_set); then a small thread of its own looks every
POLL_SECONDS whether a fetch is due (none yet this run, a day since the last
that reached GitHub, RETRY_SECONDS since one that did not) and allowed
(netversion_fetch.h), and runs each fetch on a thread of its own, with the
update check's stack: the signature file and the table downloaded
(posix_https_download: GitHub's authorities, no redirect but to GitHub's
release storage, NETVERSION_TABLE_MAXIMUM_SIZE bytes at most), checked, and
put in effect and kept only when newer than the table in effect. Kept as one
file written whole (NETVERSION_TABLE_KEPT_FILE). The downloads go to .new
files beside it, removed after.

Release builds ask FETCH_BASE alone. (tests only, never a release build)
HALO_TEST_NETVERSIONS_URL another base, http:// to this machine alone (the
tests' server); HALO_TEST_NETVERSIONS_FOLDER the folder kept in;
HALO_TEST_NETVERSIONS_INTERVAL seconds in place of a day.
*/

#include "netversion_fetch.h"
#include "halo_port_limits.h"

#if !defined(HALO_DEDICATED_SERVER) && defined(HALO_PORT_VITA_NETWORK)

#include "netversion_table.h"
#include "network_wire.h"
#include "platform.h"
#include "port_config.h"
#include "posix.h"
#include "../../vita/include/vita_version.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* a release of its own, "crossplay-table", whose assets the owner replaces
(tools/release/README.md, "Crossplay version table") */
#define FETCH_BASE "https://github.com/BirchWoodGod/halo-ce-vita/releases/download/crossplay-table"
#define USER_AGENT "HaloCEVita/" HALO_VITA_VERSION
#define FETCH_STACK_SIZE (256 * 1024)
#define SCHEDULER_STACK_SIZE (64 * 1024)
#define POLL_SECONDS 10
#define FETCH_INTERVAL_SECONDS (24 * 60 * 60)
#define RETRY_SECONDS (30 * 60)
#define PATH_SIZE 1024

static pthread_mutex_t fetch_lock = PTHREAD_MUTEX_INITIALIZER;
/* (never signalled: the scheduler's timed sleep, on a thread of
pthread_create's, as the Vita's pthread needs; vitasdk's timed wait may
oversleep a signal, never its deadline: halo_cond.h) */
static pthread_cond_t fetch_wake = PTHREAD_COND_INITIALIZER;
/* (under fetch_lock) the kept table read; the scheduler started; a fetch
running; the last fetch tried and the last that reached GitHub (0 none) */
static int loaded, scheduled, running;
static time_t last_tried, last_reached;
/* the table in effect (version 0: none, the built-in list): written at
start, then by the one fetch running */
static struct netversion_table current;
/* the last reason a fetch was not allowed, logged once */
static char refusal_logged[96];

/* ---------- where */

static void kept_path(char *path, size_t size, const char *name)
{
	const char *folder = NULL;

#ifndef HALO_RELEASE
	folder = getenv("HALO_TEST_NETVERSIONS_FOLDER");
#endif
	if (folder && folder[0])
		snprintf(path, size, "%s/%s", folder, name);
	else
#if defined(HALO_VITA)
		snprintf(path, size, "ux0:data/haloce-vita/%s", name);
#else
		snprintf(path, size, "%s/%s", platform_save_root(), name);
#endif
}

/* the base the files are asked of: FETCH_BASE, or a test's on this machine */
static const char *fetch_base(int *loopback)
{
	const char *test = NULL;

	*loopback = 0;
#ifndef HALO_RELEASE
	test = getenv("HALO_TEST_NETVERSIONS_URL");
#endif
	if (test && test[0])
	{
		if (!strncmp(test, "http://127.", 11) && strlen(test) < 200)
		{
			*loopback = 1;
			return test;
		}
		static int logged;

		if (!logged)
		{
			logged = 1;
			platform_log("netversions: HALO_TEST_NETVERSIONS_URL is not this machine's: GitHub's is asked");
		}
	}
	return FETCH_BASE;
}

static long fetch_interval(void)
{
#ifndef HALO_RELEASE
	const char *test = getenv("HALO_TEST_NETVERSIONS_INTERVAL");

	if (test && test[0] && atol(test) > 0)
		return atol(test);
#endif
	return FETCH_INTERVAL_SECONDS;
}

/* the URL path's rest after the host: printable, no space; 1 if clean */
static int path_clean(const char *path)
{
	for (; *path; path++)
	{
		if ((unsigned char)*path <= ' ' || (unsigned char)*path >= 0x7F)
			return 0;
	}
	return 1;
}

/* GitHub's redirect of a release's asset to its storage (exactly those
hosts, https, no port or user), or the tests' loopback to loopback */
static int redirect_allowed(const char *from_url, const char *to_url)
{
	static const char *const hosts[] = { "release-assets.githubusercontent.com", "objects.githubusercontent.com" };
	size_t index;

	if (!from_url || !to_url)
		return 0;
	if (!strncmp(to_url, "https://", 8))
	{
		for (index = 0; index < sizeof(hosts) / sizeof(hosts[0]); index++)
		{
			size_t length = strlen(hosts[index]);

			if (!strncmp(to_url + 8, hosts[index], length) && to_url[8 + length] == '/')
				return path_clean(to_url + 8 + length);
		}
		return 0;
	}
	return !strncmp(from_url, "http://127.", 11) && !strncmp(to_url, "http://127.", 11) && path_clean(to_url);
}

/* ---------- the table */

/* a file of at most maximum bytes: its text (free() it) and size, or NULL */
static char *read_bounded(const char *path, size_t maximum, size_t *size)
{
	char *text = config_file_read(path, size);

	if (text && *size > maximum)
	{
		free(text);
		text = NULL;
	}
	if (!text)
		*size = 0;
	return text;
}

/* each row in the log, and what this build makes of it */
static void log_rows(const struct netversion_table *table)
{
	int index;

	for (index = 0; index < table->count; index++)
	{
		const struct netversion_row *row = &table->rows[index];

		if (row->version == HALO_PORT_NETWORK_VERSION)
			platform_log("netversions:   %u: the Vitas' own version, row ignored", (unsigned int)row->version);
		else if (row->kind == NETVERSION_ROW_REFUSE)
			platform_log("netversions:   refuse %u: %s", (unsigned int)row->version, row->reason);
		else if (network_wire_rules_built_in(row->version) && row->rules != row->version)
			platform_log("netversions:   join %u by %u's rules: %u is built in with its own rules, row ignored",
				(unsigned int)row->version, (unsigned int)row->rules, (unsigned int)row->version);
		else if (!network_wire_rules_built_in(row->rules))
			platform_log("netversions:   join %u by %u's rules: not rules this build has (a newer build's), not joined",
				(unsigned int)row->version, (unsigned int)row->rules);
		else
			platform_log("netversions:   join %u by %u's rules", (unsigned int)row->version, (unsigned int)row->rules);
	}
}

/* the table now in effect, said */
static void log_in_effect(const char *how)
{
	char joined[400];

	network_wire_crossplay_versions_text(joined, (int)sizeof(joined));
	platform_log("netversions: %s; the crossplay wire joins %s", how, joined);
}

/* (fetch_lock not held: the scheduler and fetch threads write current one
at a time) the kept table taken, once */
static void load_kept(void)
{
	char path[PATH_SIZE], error[96], how[PATH_SIZE + 64];
	unsigned char key[32];
	size_t size = 0;
	char *kept;
	int result;

	kept_path(path, sizeof(path), NETVERSION_TABLE_KEPT_FILE);
	config_file_settle(path);
	kept = read_bounded(path, NETVERSION_TABLE_KEPT_MAXIMUM_SIZE, &size);
	if (!kept)
	{
		log_in_effect("no crossplay version table kept: the built-in list");
		return;
	}
	netversion_table_public_key(key);
	memset(&current, 0, sizeof(current));
	if (size < NETVERSION_TABLE_KEPT_SIGNATURE_SIZE || kept[NETVERSION_TABLE_KEPT_SIGNATURE_SIZE - 1] != '\n')
	{
		result = NETVERSION_TABLE_MALFORMED;
		snprintf(error, sizeof(error), "not a kept table");
	}
	else
	{
		result = netversion_table_newer(kept + NETVERSION_TABLE_KEPT_SIGNATURE_SIZE,
			size - NETVERSION_TABLE_KEPT_SIGNATURE_SIZE, kept, NETVERSION_TABLE_KEPT_SIGNATURE_SIZE, key, &current, error,
			(int)sizeof(error));
	}
	free(kept);
	if (result != NETVERSION_TABLE_OK)
	{
		memset(&current, 0, sizeof(current));
		platform_log("netversions: the kept table %s not taken: %s", path, error);
		log_in_effect("the built-in list");
		return;
	}
	network_wire_table_set(&current);
	snprintf(how, sizeof(how), "the kept table %s taken: version %lu, %d rows", path, current.version, current.count);
	log_in_effect(how);
	log_rows(&current);
}

/* the table and its signature as one file, whole or not at all: 1, else 0 */
static int keep_table(const char *path, const char *text, size_t size, const char *signature)
{
	char *kept = malloc(NETVERSION_TABLE_KEPT_SIGNATURE_SIZE + size);
	int written;

	if (!kept)
		return 0;
	/* (its 128 digits, whichever line end the file had: it passed the check) */
	memcpy(kept, signature, NETVERSION_TABLE_KEPT_SIGNATURE_SIZE - 1);
	kept[NETVERSION_TABLE_KEPT_SIGNATURE_SIZE - 1] = '\n';
	memcpy(kept + NETVERSION_TABLE_KEPT_SIGNATURE_SIZE, text, size);
	written = config_file_replace(path, kept, NETVERSION_TABLE_KEPT_SIGNATURE_SIZE + size);
	free(kept);
	return written;
}

/* ---------- a fetch */

static int offline_error(const char *error)
{
	return !strncmp(error, "cannot look up", 14) || !strncmp(error, "cannot connect", 14);
}

/* one file of the base into path: 1, else 0 with why in error */
static int download(const char *base, const char *name, const char *path, unsigned long maximum, char *error,
	int error_size)
{
	char url[400];

	if (snprintf(url, sizeof(url), "%s/%s", base, name) >= (int)sizeof(url))
	{
		snprintf(error, (size_t)error_size, "the URL is too long");
		return 0;
	}
	remove(path);
	return posix_https_download(url, USER_AGENT, path, 0, (posix_ulong)maximum, redirect_allowed, NULL, NULL, error,
		error_size) == 1;
}

/* the fetch: 1 if GitHub answered (a table taken, or not newer, or refused),
0 if it was not reached */
static int fetch(void)
{
	char path[PATH_SIZE], signature_path[PATH_SIZE], kept[PATH_SIZE];
	char error[256], how[160];
	unsigned char key[32];
	size_t size = 0, signature_size = 0;
	char *text = NULL, *signature = NULL;
	int loopback, reached = 0, result;
	const char *base = fetch_base(&loopback);
	struct netversion_table candidate;

	kept_path(path, sizeof(path), NETVERSION_TABLE_FILE ".new");
	kept_path(signature_path, sizeof(signature_path), NETVERSION_TABLE_SIGNATURE_FILE ".new");
	kept_path(kept, sizeof(kept), NETVERSION_TABLE_KEPT_FILE);
	platform_log("netversions: fetching the crossplay version table (in effect: version %lu)", current.version);
	if (!download(base, NETVERSION_TABLE_SIGNATURE_FILE, signature_path, NETVERSION_TABLE_SIGNATURE_MAXIMUM_SIZE, error,
			(int)sizeof(error)) ||
		!download(base, NETVERSION_TABLE_FILE, path, NETVERSION_TABLE_MAXIMUM_SIZE, error, (int)sizeof(error)))
	{
		/* (an answer, such as a 404 for a table not published yet, is
		GitHub reached: asked again in a day) */
		reached = !offline_error(error) && strncmp(error, "timed out", 9) && strncmp(error, "connection lost", 15);
		platform_log("netversions: could not fetch the table: %s", error);
		goto done;
	}
	reached = 1;
	text = read_bounded(path, NETVERSION_TABLE_MAXIMUM_SIZE, &size);
	signature = read_bounded(signature_path, NETVERSION_TABLE_SIGNATURE_MAXIMUM_SIZE, &signature_size);
	if (!text || !signature)
	{
		platform_log("netversions: the fetched table could not be read");
		goto done;
	}
	netversion_table_public_key(key);
	memcpy(&candidate, &current, sizeof(candidate));
	result = netversion_table_newer(text, size, signature, signature_size, key, &candidate, error, (int)sizeof(error));
	if (result != NETVERSION_TABLE_OK)
	{
		platform_log("netversions: the fetched table is not taken: %s (the one in effect stays)", error);
		goto done;
	}
	pthread_mutex_lock(&fetch_lock);
	memcpy(&current, &candidate, sizeof(current));
	pthread_mutex_unlock(&fetch_lock);
	network_wire_table_set(&candidate);
	/* (kept for the next start: the exact bytes that were checked) */
	if (!keep_table(kept, text, size, signature))
		platform_log("netversions: could not keep the table in %s", kept);
	snprintf(how, sizeof(how), "the fetched table taken: version %lu, %d rows", candidate.version, candidate.count);
	log_in_effect(how);
	log_rows(&candidate);

done:
	free(text);
	free(signature);
	remove(path);
	remove(signature_path);
	return reached;
}

static void *fetch_thread(void *unused)
{
	int reached;

	(void)unused;
	reached = fetch();
	pthread_mutex_lock(&fetch_lock);
	if (reached)
		last_reached = time(NULL);
	running = 0;
	pthread_mutex_unlock(&fetch_lock);
	return NULL;
}

/* ---------- when */

/* whether this machine is on a network that can reach the internet: an
address, not ad hoc play's stand-in (169.254.x.x) */
static int network_up(void)
{
	posix_ulong address = posix_local_ipv4_address();

	return address && (address & 0xFFFF) != (169u | 254u << 8);
}

/* whether a fetch may ask the network now: 1, else 0 and why */
static int fetch_allowed(int loopback, char *why, size_t size)
{
	why[0] = 0;
	if (!network_wire_crossplay_available())
		snprintf(why, size, "no crossplay here (ad hoc play, or a build that does not play as a Vita)");
	else if (!config_boolean("network.crossplay_updates"))
		snprintf(why, size, "network.crossplay_updates is off");
#ifndef HALO_VITA
	else if (!config_boolean("update.auto"))
		snprintf(why, size, "update.auto is off");
#endif
	else if (!config_boolean("network.online"))
		snprintf(why, size, "internet play is off");
	else if (!loopback && !network_up())
		snprintf(why, size, "no internet connection");
	return !why[0];
}

/* one look: a fetch started when due and allowed */
static void poll_once(void)
{
	time_t now = time(NULL);
	char why[96];
	int due, loopback;
	long interval = fetch_interval();

	fetch_base(&loopback);
	pthread_mutex_lock(&fetch_lock);
	/* (a clock set back: as if due) */
	due = !running && (!last_tried || now < last_tried ||
		(last_reached ? now - last_reached >= interval : now - last_tried >= (interval < RETRY_SECONDS ? interval :
			RETRY_SECONDS)));
	pthread_mutex_unlock(&fetch_lock);
	if (!due)
		return;
	if (!fetch_allowed(loopback, why, sizeof(why)))
	{
		if (strcmp(why, refusal_logged))
		{
			snprintf(refusal_logged, sizeof(refusal_logged), "%s", why);
			platform_log("netversions: not fetching the table: %s", why);
		}
		return;
	}
	refusal_logged[0] = 0;
	pthread_mutex_lock(&fetch_lock);
	if (running)
	{
		pthread_mutex_unlock(&fetch_lock);
		return;
	}
	running = 1;
	last_tried = now;
	last_reached = 0;
	pthread_mutex_unlock(&fetch_lock);
	{
		pthread_t thread;
		pthread_attr_t attributes;
		int started;

		pthread_attr_init(&attributes);
		pthread_attr_setstacksize(&attributes, FETCH_STACK_SIZE);
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
		started = pthread_create(&thread, &attributes, fetch_thread, NULL) == 0;
		pthread_attr_destroy(&attributes);
		if (!started)
		{
			platform_log("netversions: could not fetch the table: no thread");
			pthread_mutex_lock(&fetch_lock);
			running = 0;
			pthread_mutex_unlock(&fetch_lock);
		}
	}
}

static void *scheduler_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		struct timespec deadline;

		poll_once();
		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += POLL_SECONDS;
		pthread_mutex_lock(&fetch_lock);
		pthread_cond_timedwait(&fetch_wake, &fetch_lock, &deadline);
		pthread_mutex_unlock(&fetch_lock);
	}
	return NULL;
}

void netversion_fetch_start(void)
{
	int load, start;

	pthread_mutex_lock(&fetch_lock);
	load = !loaded;
	loaded = 1;
	pthread_mutex_unlock(&fetch_lock);
	if (load)
		load_kept();
	pthread_mutex_lock(&fetch_lock);
	start = !scheduled;
	scheduled = 1;
	pthread_mutex_unlock(&fetch_lock);
	if (start)
	{
		pthread_t thread;
		pthread_attr_t attributes;

		pthread_attr_init(&attributes);
		pthread_attr_setstacksize(&attributes, SCHEDULER_STACK_SIZE);
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&thread, &attributes, scheduler_thread, NULL) != 0)
		{
			platform_log("netversions: no thread to fetch the table with: the kept table and the built-in list");
			pthread_mutex_lock(&fetch_lock);
			scheduled = 0;
			pthread_mutex_unlock(&fetch_lock);
		}
		pthread_attr_destroy(&attributes);
	}
}

#else

/* (no crossplay wire here, a dedicated server's or a build that does not
play as a Vita: nothing to keep up to date) */
void netversion_fetch_start(void)
{
}

#endif
