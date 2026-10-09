/*
UPDATE_CHECK.H

Telling the player whether a newer Halo CE for PS Vita is out, when they ask
(notification only: nothing is downloaded or installed, and nothing is
asked of the network unless the player asks). The settings panel's "Check
for updates" (Controls > Advanced) asks GitHub's public API for the
project's releases on a thread of its own and shows one line: up to date,
the newer version and where to get it, or that it could not check. halo.log
says it too, and the main menu's corner shows a found update until the game
restarts.

update_check.c is the part with no system in it (the versions, the
releases' JSON, the channel, the cache file's text), which the tests and the
fuzzer build alone; update_notify.c is the game's side (the thread, the cache
file, what the menu and the panel show); posix_https.c fetches the releases
(TLS with Mbed TLS over the platform's sockets, the same code on the Vita
and on Linux). Only integers and strings cross between them.

The Update channel setting (HALO_UPDATE_CHANNEL, the panel's row beside the
button): "experimental" looks at pre-releases and releases, "stable" at
releases only. A pre-release build (HALO_VITA_VERSION with a suffix,
1.1.0-beta.3) is always on Experimental - Stable is shown greyed out, and a
saved "stable" is ignored (logged once): its player chose betas, and stable
updates arrive with the release (1.1.0). A release build offers both and
starts on Stable.
*/

#ifndef UPDATE_CHECK_H
#define UPDATE_CHECK_H

#include <stddef.h>

/* the project whose releases are looked at, and where the player gets them */
#define UPDATE_REPOSITORY "BirchWoodGod/halo-ce-vita"
#define UPDATE_RELEASES_PAGE "github.com/" UPDATE_REPOSITORY "/releases"
/* the newest few releases (pre-releases among them), and the newest release
that is not one */
#define UPDATE_URL_ALL "https://api.github.com/repos/" UPDATE_REPOSITORY "/releases?per_page=6"
#define UPDATE_URL_STABLE "https://api.github.com/repos/" UPDATE_REPOSITORY "/releases/latest"
/* the most of a response read: more is refused (six releases with their
notes are ~60 KB) */
#define UPDATE_RESPONSE_MAXIMUM (256 * 1024)
/* a version's text, at most (with its NUL) */
#define UPDATE_VERSION_SIZE 64
/* a look this recent (seconds) is answered again from the cache file, not
GitHub (whose limit is 60 requests an hour from an address without a
token): a second press, or a restart and a press */
#define UPDATE_RECHECK_SECONDS 60L

enum
{
	UPDATE_CHANNEL_STABLE = 1,
	UPDATE_CHANNEL_EXPERIMENTAL,
};

/* ---------- versions (semantic versioning: 1.1.0 > 1.1.0-beta.3 >
1.1.0-beta.2 > 1.0.3) */

struct update_version
{
	unsigned long major, minor, patch;
	/* the pre-release's identifiers, dot separated ("beta.3"), "" for a
	release */
	char prerelease[UPDATE_VERSION_SIZE];
};

/* reads "1.1.0-beta.3" or a tag's "v1.1.0-beta.3" (build metadata after a
'+' is read and ignored); 1 if it is a version, else 0 */
int update_version_parse(const char *text, struct update_version *version);
/* <0, 0 or >0 as first is older than, the same as or newer than second */
int update_version_compare(const struct update_version *first, const struct update_version *second);
/* whether latest is a version newer than current (0 when either is not a
version) */
int update_is_newer(const char *latest, const char *current);

/* whether a release's tag_name is one taken: exactly v<1-3 digits>.<1-3
digits>.<1-3 digits>, optionally -beta.<1-3 digits> (v1.1.0, v1.1.0-beta.3);
any other tag's release is passed over */
int update_tag_valid(const char *tag);

/* ---------- the channel */

/* whether a build of this version is a pre-release (on Experimental only) */
int update_build_is_prerelease(const char *current_version);
/* the channel a build of this version starts with: Experimental for a
pre-release, else Stable */
int update_channel_default(const char *current_version);
/* whether a build of this version may use the channel (a pre-release:
Experimental only) */
int update_channel_allowed(int channel, const char *current_version);
/* "stable" or "experimental" (any case); anything else: fallback */
int update_channel_parse(const char *text, int fallback);
const char *update_channel_name(int channel);
/* the release a version leads to, its x.y.z ("1.1.0" for 1.1.0-beta.3):
1, else 0 */
int update_version_release(const char *version, char *release, int size);

/* ---------- the releases' JSON (GitHub's: an array of releases, or one
release object)

Read with bounded sizes and depth; of each release (the first 32) only
tag_name, prerelease and draft are looked at - no name, notes or URL of it
is kept or shown. A release is taken when its tag passes
update_tag_valid, it is not a draft, and the channel takes it (Stable:
neither marked a pre-release nor with a pre-release version). The newest taken goes to latest (without
the tag's v): 1; none taken: 0; text that is not JSON as expected: -1. */
int update_releases_pick(const char *json, size_t size, int channel, char *latest, int latest_size);

/* ---------- the cache file (when the last look was, for which channel,
what it found) */

struct update_cache
{
	long long checked; /* seconds since 1970 */
	int channel;
	int ok; /* 0: the look failed (tried again a day later) */
	char latest[UPDATE_VERSION_SIZE]; /* "" when none was found */
};

/* the file's text; its length (< size), or 0 if it does not fit */
int update_cache_format(char *text, int size, const struct update_cache *cache);
/* reads the file's text (size bytes, not necessarily NUL terminated); 1 if
it is a cache file, else 0 */
int update_cache_parse(const char *text, size_t size, struct update_cache *cache);
/* whether a look at checked still counts at now: less than seconds before
(a time ahead of the clock never does) */
int update_cache_fresh(long long checked, long long now, long seconds);

/* ---------- the game's (update_notify.c) */

/* what the last look said, for the Update channel chosen now */
enum
{
	UPDATE_STATUS_NONE, /* none yet (or for the other channel) */
	UPDATE_STATUS_CHECKING,
	UPDATE_STATUS_UP_TO_DATE,
	UPDATE_STATUS_AVAILABLE,
	UPDATE_STATUS_NO_CONNECTION,
	UPDATE_STATUS_FAILED,
};

/* the player's press: a look in the background (never waited for), or the
last one's answer again when it is under UPDATE_RECHECK_SECONDS old or one
is under way; the status after it */
int update_check_request(void);
/* the status, and the version it is about (the newer one for AVAILABLE,
this build's for UP_TO_DATE; else "") */
int update_check_status(char *version, int size);
/* the main menu's line once a look found a newer version ("Update:
1.1.0-beta.4", in the player's language) as the game's UTF-16:
1, else 0 and text empty */
int update_check_menu_text(unsigned short *text, int count);

#endif
