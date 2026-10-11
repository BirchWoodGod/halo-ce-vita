/*
UPDATE_CHECK_TEST.C

The update check's parts with no system in them (port/linux/src/
update_check.c): version order (1.1.0 > 1.1.0-beta.3 > 1.1.0-beta.2 >
1.0.3), the tags taken (exactly v1.2.3 or v1.2.3-beta.4, 1 to 3 digits
each), the channels (a pre-release build on Experimental only; Stable takes
no pre-release, Experimental takes both), the releases' JSON as GitHub sends
it (an array, or /releases/latest's one object: drafts, nested objects with
their own tag_name, long notes, the first 32 releases only) and as it should
not be (malformed, truncated, nested too deep), and the cache file's text.
Built with AddressSanitizer and UBSan by run_update_check_test.sh.
*/

#include "update_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	if (!condition)
	{
		printf("FAIL %s\n", what);
		failures++;
	}
}

static int compare(const char *first, const char *second)
{
	struct update_version a, b;

	if (!update_version_parse(first, &a) || !update_version_parse(second, &b))
		return 99;
	return update_version_compare(&a, &b);
}

static void test_versions(void)
{
	/* (each newer than the next) */
	static const char *const order[] = {
		"2.0.0", "1.10.0", "1.2.10", "1.2.9", "1.1.0", "1.1.0-rc.1", "1.1.0-beta.11", "1.1.0-beta.3", "1.1.0-beta.2",
		"1.1.0-beta", "1.1.0-alpha.beta", "1.1.0-alpha.1", "1.1.0-alpha", "1.0.3", "1.0.3-beta.6", "0.9.9",
	};
	struct update_version version;
	size_t i, j;
	char text[200];

	for (i = 0; i < sizeof(order) / sizeof(order[0]); i++)
		for (j = 0; j < sizeof(order) / sizeof(order[0]); j++)
		{
			int expected = i == j ? 0 : i < j ? 1 : -1;

			snprintf(text, sizeof(text), "compare %s %s = %d", order[i], order[j], expected);
			check(compare(order[i], order[j]) == expected, text);
		}
	check(compare("v1.1.0-beta.3", "1.1.0-beta.3") == 0, "a tag's v");
	check(compare("1.1.0+build.5", "1.1.0") == 0, "build metadata ignored");
	check(compare("1.1.0-beta.03", "1.1.0-beta.3") == 0, "leading zeros of a number");
	check(update_is_newer("1.1.0", "1.1.0-beta.3"), "1.1.0 newer than beta.3");
	check(update_is_newer("1.1.0-beta.4", "1.1.0-beta.3"), "beta.4 newer than beta.3");
	check(!update_is_newer("1.1.0-beta.3", "1.1.0-beta.3"), "same is not newer");
	check(!update_is_newer("1.0.3", "1.1.0-beta.3"), "1.0.3 older than 1.1.0-beta.3");
	check(!update_is_newer("garbage", "1.0.0") && !update_is_newer("1.0.0", "garbage"), "not versions: never newer");
	{
		static const char *const bad[] = {
			"", "v", "1", "1.2", "1.2.", "1.2.3.4", "1.0.2.2", "01.2.3x", "1.2.3-", "1.2.3-beta..1", "1.2.3-beta.",
			"1.2.3+", "1.2.3 ", " 1.2.3", "1.2.3-be ta", "1234567890.0.0", "-1.2.3", "1.2.3-\xc3\xa9",
			"1.2.3-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		};

		for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		{
			snprintf(text, sizeof(text), "not a version: \"%s\"", bad[i]);
			check(!update_version_parse(bad[i], &version), text);
		}
	}
	check(update_version_parse("123456789.0.0", &version) && version.major == 123456789, "nine digits read");
	{
		char release[16];

		check(update_version_release("1.1.0-beta.3", release, sizeof(release)) && !strcmp(release, "1.1.0"),
			"1.1.0-beta.3 leads to 1.1.0");
		check(update_version_release("v12.0.7", release, sizeof(release)) && !strcmp(release, "12.0.7"), "release of v12.0.7");
		check(!update_version_release("1.1.0", release, 5), "release text too long for its buffer");
	}
}

static void test_tags(void)
{
	static const char *const good[] = { "v1.1.0", "v1.1.0-beta.3", "v0.0.0", "v999.999.999-beta.999", "v1.0.3" };
	static const char *const bad[] = {
		"1.1.0", "V1.1.0", "v1.1", "v1.1.0.2", "v1.0.2.2", "v1000.0.0", "v1.1.0-beta", "v1.1.0-beta.", "v1.1.0-beta.1000",
		"v1.1.0-rc.1", "v1.1.0-BETA.1", "v1.1.0-beta.3 ", " v1.1.0", "v1.1.0+1", "v1.1.0-beta.3.1", "v", "", "v1..0",
		"v1.1.0-beta.-1", "v-1.1.0",
	};
	char text[96];
	size_t i;

	for (i = 0; i < sizeof(good) / sizeof(good[0]); i++)
	{
		snprintf(text, sizeof(text), "tag taken: %s", good[i]);
		check(update_tag_valid(good[i]), text);
	}
	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
	{
		snprintf(text, sizeof(text), "tag refused: \"%s\"", bad[i]);
		check(!update_tag_valid(bad[i]), text);
	}
}

static void test_channels(void)
{
	check(update_channel_default("1.1.0-beta.3") == UPDATE_CHANNEL_EXPERIMENTAL, "a beta build starts on Experimental");
	check(update_channel_default("1.1.0") == UPDATE_CHANNEL_STABLE, "a release build starts on Stable");
	check(update_build_is_prerelease("1.1.0-beta.3") && !update_build_is_prerelease("1.1.0"), "pre-release builds");
	check(update_build_is_prerelease("not a version"), "a build that is not a version is careful");
	check(!update_channel_allowed(UPDATE_CHANNEL_STABLE, "1.1.0-beta.3"), "a beta build may not use Stable");
	check(update_channel_allowed(UPDATE_CHANNEL_EXPERIMENTAL, "1.1.0-beta.3"), "a beta build uses Experimental");
	check(update_channel_allowed(UPDATE_CHANNEL_STABLE, "1.1.0") && update_channel_allowed(UPDATE_CHANNEL_EXPERIMENTAL,
		"1.1.0"), "a release build may use both");
	check(!update_channel_allowed(0, "1.1.0") && !update_channel_allowed(7, "1.1.0"), "no other channel");
	check(update_channel_parse("Stable", 0) == UPDATE_CHANNEL_STABLE, "Stable parsed");
	check(update_channel_parse("EXPERIMENTAL", 0) == UPDATE_CHANNEL_EXPERIMENTAL, "Experimental parsed");
	check(update_channel_parse("beta", 42) == 42 && update_channel_parse("off", 42) == 42 &&
		update_channel_parse("", 42) == 42 && update_channel_parse(NULL, 42) == 42 &&
		update_channel_parse("stablex", 42) == 42, "anything else: the fallback");
	check(!strcmp(update_channel_name(UPDATE_CHANNEL_STABLE), "stable") &&
		!strcmp(update_channel_name(UPDATE_CHANNEL_EXPERIMENTAL), "experimental"), "channel names");
}

static int pick(const char *json, int channel, char *latest)
{
	return update_releases_pick(json, strlen(json), channel, latest, UPDATE_VERSION_SIZE);
}

static void test_releases(void)
{
	char latest[UPDATE_VERSION_SIZE];
	const char *list =
		"[{\"url\":\"x\",\"html_url\":\"https://evil.example/\",\"tag_name\":\"v1.1.0-beta.2\",\"draft\":false,"
		"\"prerelease\":true,\"author\":{\"login\":\"a\",\"tag_name\":\"v9.9.9\"},\"assets\":[{\"name\":\"halo.vpk\","
		"\"uploader\":{\"tag_name\":\"v8.8.8\"}}],\"body\":\"notes \\\"quoted\\\" \\u00e9 \\n\",\"id\":123,\"x\":-1.5e3},"
		"{\"tag_name\":\"v1.1.0-beta.4\",\"draft\":true,\"prerelease\":true},"
		"{\"tag_name\":\"v1.0.3\",\"draft\":false,\"prerelease\":false},"
		"{\"tag_name\":\"v1.0.2.2\",\"draft\":false,\"prerelease\":false},"
		"{\"tag_name\":\"v1.1.0-beta.1\",\"draft\":false,\"prerelease\":true}]";

	check(pick(list, UPDATE_CHANNEL_EXPERIMENTAL, latest) == 1 && !strcmp(latest, "1.1.0-beta.2"),
		"Experimental: the newest pre-release, not the draft, not a nested tag_name");
	check(pick(list, UPDATE_CHANNEL_STABLE, latest) == 1 && !strcmp(latest, "1.0.3"), "Stable: the newest release");
	check(pick("[]", UPDATE_CHANNEL_EXPERIMENTAL, latest) == 0 && !latest[0], "no releases");
	check(pick("{\"tag_name\":\"v1.0.3\",\"prerelease\":false,\"draft\":false}", UPDATE_CHANNEL_STABLE, latest) == 1 &&
		!strcmp(latest, "1.0.3"), "/releases/latest's one object");
	check(pick("  {\"tag_name\":\"v1.1.0\"}\r\n", UPDATE_CHANNEL_STABLE, latest) == 1 && !strcmp(latest, "1.1.0"),
		"space around it; no prerelease or draft key");
	check(pick("[{\"tag_name\":\"v1.2.0-beta.1\",\"prerelease\":false}]", UPDATE_CHANNEL_STABLE, latest) == 0,
		"Stable: a pre-release version not marked as one is still passed over");
	check(pick("[{\"tag_name\":\"v1.2.0-beta.1\",\"prerelease\":false}]", UPDATE_CHANNEL_EXPERIMENTAL, latest) == 1,
		"Experimental takes it");
	check(pick("[{\"tag_name\":\"v1.2.0\",\"tag_name\":\"v9.0.0\"}]", UPDATE_CHANNEL_STABLE, latest) == 0,
		"a release with its tag_name twice is passed over");
	check(pick("[{\"tag_name\":\"v1.2.0\",\"draft\":\"no\"}]", UPDATE_CHANNEL_STABLE, latest) == 0,
		"a draft key that is not a boolean");
	check(pick("[{\"tag_name\":12}]", UPDATE_CHANNEL_STABLE, latest) == 0, "a tag_name that is not a string");
	check(pick("[{\"tag_name\":\"v1.2.0\\u0000\"}]", UPDATE_CHANNEL_STABLE, latest) == 0, "a tag with a NUL escape");
	check(pick("[{\"tag_name\":\"v1.2.0-rc.1\"}]", UPDATE_CHANNEL_EXPERIMENTAL, latest) == 0, "a tag of another form");
	check(pick("[{\"tag_name\":\"v1.2.0\"},5,\"x\",null,[1,[2]],true]", UPDATE_CHANNEL_STABLE, latest) == 1,
		"other values among the releases");
	{
		/* (only the first 32 releases are looked at) */
		char json[4096];
		int length = 1, index;

		json[0] = '[';
		for (index = 0; index < 40; index++)
			length += snprintf(json + length, sizeof(json) - (size_t)length, "%s{\"tag_name\":\"v1.0.%d\"}", index ? "," : "",
				index == 35 ? 99 : index);
		snprintf(json + length, sizeof(json) - (size_t)length, "]");
		check(pick(json, UPDATE_CHANNEL_STABLE, latest) == 1 && !strcmp(latest, "1.0.31"), "the first 32 releases only");
	}
	{
		/* (a long string, a release's notes, read past) */
		size_t size = 300000, at;
		char *json = malloc(size + 64);

		at = (size_t)sprintf(json, "[{\"body\":\"");
		memset(json + at, 'a', size);
		at += size;
		sprintf(json + at, "\",\"tag_name\":\"v3.0.0\"}]");
		check(pick(json, UPDATE_CHANNEL_STABLE, latest) == 1 && !strcmp(latest, "3.0.0"), "a 300 KB string read past");
		free(json);
	}
	{
		static const char *const malformed[] = {
			"", "   ", "[", "[{", "[{\"tag_name\"", "[{\"tag_name\":", "[{\"tag_name\":\"v1.0.0\"", "[{\"tag_name\":\"v1.0.0\"}",
			"[{\"tag_name\":\"v1.0.0\"},]", "[,]", "{\"a\" 1}", "{'a':1}", "[\"\\x\"]", "[\"\\u12\"]", "[\"\\u12G4\"]",
			"[\"a\nb\"]", "[tru]", "[nul]", "[1 2]", "[{} {}]", "{\"a\":1}x", "[]]", "nope", "[-]", "\"str\"", "123",
			"[1111111111111111111111111111111111111111111111111111111111111111111111111]",
		};
		size_t i;
		char text[160];

		for (i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++)
		{
			snprintf(text, sizeof(text), "malformed: %s", malformed[i]);
			check(pick(malformed[i], UPDATE_CHANNEL_EXPERIMENTAL, latest) == -1 && !latest[0], text);
		}
	}
	{
		/* (nesting: 16 deep read, deeper refused) */
		char json[256];
		int depth;

		for (depth = 14; depth <= 20; depth++)
		{
			int length = 0, index;
			char text[64];

			for (index = 0; index < depth; index++)
				json[length++] = '[';
			for (index = 0; index < depth; index++)
				json[length++] = ']';
			json[length] = 0;
			snprintf(text, sizeof(text), "nested %d deep", depth);
			check(pick(json, UPDATE_CHANNEL_STABLE, latest) == (depth <= 16 ? 0 : -1), text);
		}
	}
	{
		/* (truncated anywhere: never a release, never past the end) */
		size_t length = strlen(list), cut;
		int bad = 0;

		for (cut = 0; cut < length; cut++)
		{
			char *copy = malloc(cut ? cut : 1);

			memcpy(copy, list, cut);
			if (update_releases_pick(copy, cut, UPDATE_CHANNEL_EXPERIMENTAL, latest, sizeof(latest)) != -1)
				bad++;
			free(copy);
		}
		check(!bad, "every truncation of the list refused");
	}
	check(update_releases_pick(NULL, 0, UPDATE_CHANNEL_STABLE, latest, sizeof(latest)) == -1, "no text");
	check(update_releases_pick("[]", 2, UPDATE_CHANNEL_STABLE, latest, 0) == -1, "no room");
	check(update_releases_pick("[{\"tag_name\":\"v1.0.0\"}]", 23, UPDATE_CHANNEL_STABLE, latest, 4) == 0,
		"a version too long for latest");
}

static void test_cache(void)
{
	struct update_cache cache, read;
	char text[256];
	int length;

	memset(&cache, 0, sizeof(cache));
	cache.checked = 1791554278;
	cache.channel = UPDATE_CHANNEL_EXPERIMENTAL;
	cache.ok = 1;
	strcpy(cache.latest, "1.1.0-beta.4");
	length = update_cache_format(text, sizeof(text), &cache);
	check(length > 0 && update_cache_parse(text, (size_t)length, &read) && read.checked == cache.checked &&
		read.channel == cache.channel && read.ok && !strcmp(read.latest, "1.1.0-beta.4"), "cache written and read");
	check(!update_cache_format(text, 40, &cache), "cache text too long for its buffer");
	cache.ok = 0;
	length = update_cache_format(text, sizeof(text), &cache);
	check(length > 0 && update_cache_parse(text, (size_t)length, &read) && !read.ok && !read.latest[0],
		"a failed look's cache");
	{
		static const char *const bad[] = {
			"", "checked=1\n", "checked=1\nchannel=stable\n", "checked=x\nchannel=stable\nresult=ok\n",
			"checked=1\nchannel=beta\nresult=ok\n", "checked=1\nchannel=stable\nresult=maybe\n",
			"checked=1\nchannel=stable\nresult=ok\nlatest=nope\n", "checked=1234567890123\nchannel=stable\nresult=ok\n",
			"checked=1\nchannel=stable\nresult=ok\nno equals sign\n",
		};
		size_t i;
		char what[96];

		for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		{
			snprintf(what, sizeof(what), "bad cache %zu", i);
			check(!update_cache_parse(bad[i], strlen(bad[i]), &read), what);
		}
	}
	check(update_cache_parse("# x\r\nchecked=5\r\nchannel=Stable\r\nresult=ok\r\nlatest=\r\nother=1\r\n", 60, &read) &&
		read.checked == 5 && read.channel == UPDATE_CHANNEL_STABLE, "CRLF, comments, unknown keys");
	check(update_cache_fresh(1000, 1059, 60) && !update_cache_fresh(1000, 1060, 60), "a minute");
	check(!update_cache_fresh(1000, 999, 60), "a look ahead of the clock does not count");
	check(!update_cache_fresh(0, 10, 60), "no look");
}

int main(void)
{
	test_versions();
	test_tags();
	test_channels();
	test_releases();
	test_cache();
	printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
