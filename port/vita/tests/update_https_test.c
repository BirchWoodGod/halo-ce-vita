/*
UPDATE_HTTPS_TEST.C

The update check's HTTPS request (port/linux/src/posix_https.c, with Mbed
TLS and posix_net.c's sockets) against update_test_server.py:
  update_https_test HTTP_BASE TLS_BASE [internet]
- the releases list by Content-Length and chunked, read and parsed;
- refused: a redirect (to another host), an error status, a body over the
  cap (by length and chunked), a body cut short (both ways), a bad chunk,
  no length, headers over 16 KB, a server that sends nothing (the deadline);
- refused: plain http to anything but a loopback address, and URLs that are
  not http(s);
- TLS to the test server's own certificate: refused (it chains to none of
  the authorities built in), before any request is sent;
- with "internet": api.github.com's releases, over TLS checked against the
  built-in authorities (one request; GitHub's limit is 60 an hour).
*/

#include "posix.h"
#include "update_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures, checks;
static char body[UPDATE_RESPONSE_MAXIMUM];

static void check(int condition, const char *what, const char *error)
{
	checks++;
	printf("%s %s%s%s\n", condition ? "PASS" : "FAIL", what, error && error[0] ? ": " : "", error ? error : "");
	if (!condition)
		failures++;
}

static int get(const char *base, const char *path, char *error)
{
	char url[512];

	snprintf(url, sizeof(url), "%s%s", base, path);
	return posix_https_get(url, "HaloCEVita/test", body, sizeof(body), error, 256);
}

int main(int argc, char **argv)
{
	const char *http = argv[1], *https = argv[2];
	char error[256], latest[UPDATE_VERSION_SIZE];
	int length;
	time_t started;

	if (argc < 3)
		return 2;
	length = get(http, "/releases/v1.1.0-beta.4/1", error);
	check(length > 0 && update_releases_pick(body, (size_t)length, UPDATE_CHANNEL_EXPERIMENTAL, latest, sizeof(latest)) ==
		1 && !strcmp(latest, "1.1.0-beta.4"), "list by Content-Length", error);
	length = get(http, "/chunked/v1.1.0-beta.5", error);
	check(length > 0 && update_releases_pick(body, (size_t)length, UPDATE_CHANNEL_EXPERIMENTAL, latest, sizeof(latest)) ==
		1 && !strcmp(latest, "1.1.0-beta.5"), "list chunked", error);
	length = get(http, "/latest/v1.1.0", error);
	check(length > 0 && update_releases_pick(body, (size_t)length, UPDATE_CHANNEL_STABLE, latest, sizeof(latest)) == 1 &&
		!strcmp(latest, "1.1.0"), "latest release", error);
	{
		static const struct { const char *path, *what; } refused[] = {
			{ "/redirect", "a redirect to another host" },
			{ "/status/403", "status 403 (rate limit)" },
			{ "/status/500", "status 500" },
			{ "/big", "a body over the cap by Content-Length" },
			{ "/big-chunked", "a body over the cap, chunked" },
			{ "/cut", "a body cut short" },
			{ "/cut-chunked", "a chunked body without its last chunk" },
			{ "/bad-chunk", "a chunk size that is not hex" },
			{ "/no-length", "no length" },
			{ "/headers", "headers over 16 KB" },
		};
		size_t index;

		for (index = 0; index < sizeof(refused) / sizeof(refused[0]); index++)
		{
			length = get(http, refused[index].path, error);
			check(length < 0 && error[0] && !body[0], refused[index].what, error);
		}
	}
	started = time(NULL);
	length = get(http, "/slow", error);
	check(length < 0 && time(NULL) - started <= 17, "a server that sends nothing: the deadline", error);
	length = posix_https_get("http://10.0.0.1/x", "HaloCEVita/test", body, sizeof(body), error, sizeof(error));
	check(length < 0 && strstr(error, "loopback"), "plain http to another machine", error);
	length = posix_https_get("ftp://127.0.0.1/x", "HaloCEVita/test", body, sizeof(body), error, sizeof(error));
	check(length < 0, "not http(s)", error);
	length = posix_https_get("https://a b/x", "HaloCEVita/test", body, sizeof(body), error, sizeof(error));
	check(length < 0, "a bad host", error);
	length = posix_https_get("http://127.0.0.1:99999/x", "HaloCEVita/test", body, sizeof(body), error, sizeof(error));
	check(length < 0, "a bad port", error);
	length = posix_https_get("http://127.0.0.1:1/x", "HaloCEVita/test\r\nCookie: x", body, sizeof(body), error,
		sizeof(error));
	check(length < 0, "a User-Agent with a line break", error);
	length = get(https, "/releases/v1.1.0-beta.4/1", error);
	check(length < 0 && strstr(error, "certificate"), "TLS to a certificate of no authority built in", error);
	if (argc > 3 && !strcmp(argv[3], "internet"))
	{
		length = posix_https_get(UPDATE_URL_ALL, "HaloCEVita/test", body, sizeof(body), error, sizeof(error));
		check(length > 0 && update_releases_pick(body, (size_t)length, UPDATE_CHANNEL_EXPERIMENTAL, latest,
			sizeof(latest)) == 1, "api.github.com's releases over checked TLS", error);
		printf("  api.github.com: %d bytes, newest (Experimental) %s\n", length, latest);
	}
	printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
