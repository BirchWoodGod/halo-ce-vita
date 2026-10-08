/*
P2P_RESOLVER_CACHE_TEST.C

The resolver cache (port/linux/src/p2p_resolver_cache.c): the last good
address of each broker, STUN server and relay, for when looking one up
fails. Checked: only the names allowed (internet play's own settings) are
kept and answered; a newer lookup replaces an older one; a working lookup is
stored, never overridden (p2p.c asks the cache only when a lookup fails);
addresses no broker can be at are refused; the file's lines are checked one
by one (bad names, addresses, times, a line too long, duplicates, comments)
and written back to what is read again; too old an entry is not answered;
the cache holds at most P2P_RESOLVER_CACHE_ENTRIES names. Prints PASS or the
failures.

run_p2p_resolver_cache_test.sh builds it with the platform layer's flags,
then again with ASan and UBSan.
*/

#include "../../linux/src/p2p_resolver_cache.c"

#include <stdarg.h>

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	if (!condition)
	{
		failures++;
		printf("FAIL: %s\n", what);
	}
}

/* an address in network byte order, as the sockets have it */
static unsigned long address_of(int a, int b, int c, int d)
{
	unsigned char bytes[4] = { (unsigned char)a, (unsigned char)b, (unsigned char)c, (unsigned char)d };
	unsigned long address = 0;

	memcpy(&address, bytes, 4);
	return address;
}

int main(void)
{
	const unsigned long now = 1791481094UL;
	unsigned long address, stored;
	char text[4096];
	int index, length;

	/* ---- names allowed: only those are stored and answered */
	p2p_resolver_cache_reset();
	p2p_resolver_cache_allow("broker.emqx.io");
	p2p_resolver_cache_allow("Broker.HiveMQ.com");
	p2p_resolver_cache_allow("198.51.100.1");
	check(p2p_resolver_cache_allowed("broker.emqx.io"), "an allowed name is allowed");
	check(p2p_resolver_cache_allowed("broker.hivemq.com"), "names are compared without case");
	check(!p2p_resolver_cache_allowed("198.51.100.1"), "an address is not a name to keep");
	check(!p2p_resolver_cache_allowed("evil.example.com"), "a name not allowed is not");
	check(!p2p_resolver_cache_store("evil.example.com", address_of(1, 2, 3, 4), now),
		"a name not allowed is not stored (nothing from the network)");
	check(!p2p_resolver_cache_lookup("evil.example.com", now, &address, NULL), "nor answered");
	check(!p2p_resolver_cache_lookup("broker.emqx.io", now, &address, NULL), "nothing kept yet");

	/* ---- a lookup kept, and a newer one replacing it */
	check(p2p_resolver_cache_store("broker.emqx.io", address_of(34, 243, 217, 54), now), "a lookup is kept");
	check(p2p_resolver_cache_lookup("broker.emqx.io", now + 60, &address, &stored) &&
		address == address_of(34, 243, 217, 54) && stored == now, "and answered with its time");
	check(p2p_resolver_cache_lookup("BROKER.EMQX.IO", now + 60, &address, NULL), "whatever the case asked in");
	check(!p2p_resolver_cache_store("broker.emqx.io", address_of(34, 243, 217, 54), now + 60),
		"the same address again soon is not a change to write");
	check(p2p_resolver_cache_store("broker.emqx.io", address_of(35, 172, 255, 228), now + 120),
		"another address is a change");
	check(p2p_resolver_cache_lookup("broker.emqx.io", now + 180, &address, NULL) &&
		address == address_of(35, 172, 255, 228), "the newest address is answered");
	check(p2p_resolver_cache_store("broker.emqx.io", address_of(35, 172, 255, 228), now + 120 + 86400),
		"the same address a day later is written again (its time)");

	/* ---- addresses no broker is at */
	check(!p2p_resolver_cache_store("broker.hivemq.com", address_of(0, 0, 0, 0), now), "0.0.0.0 refused");
	check(!p2p_resolver_cache_store("broker.hivemq.com", address_of(127, 0, 0, 1), now), "loopback refused");
	check(!p2p_resolver_cache_store("broker.hivemq.com", address_of(224, 0, 0, 1), now), "multicast refused");
	check(!p2p_resolver_cache_store("broker.hivemq.com", address_of(255, 255, 255, 255), now), "broadcast refused");
	check(!p2p_resolver_cache_store("broker.hivemq.com", address_of(169, 254, 1, 1), now), "link-local refused");
	check(p2p_resolver_cache_store("broker.hivemq.com", address_of(192, 168, 0, 10), now),
		"a private address (a broker on the LAN) is kept");

	/* ---- too old: not answered */
	check(!p2p_resolver_cache_lookup("broker.hivemq.com", now + P2P_RESOLVER_CACHE_MAXIMUM_AGE + 1, &address, NULL),
		"an address older than the most age is not answered");
	check(p2p_resolver_cache_lookup("broker.hivemq.com", now + P2P_RESOLVER_CACHE_MAXIMUM_AGE, &address, NULL),
		"one just within it is");

	/* ---- written, and read back the same */
	length = p2p_resolver_cache_save(text, sizeof(text));
	check(length > 0 && strstr(text, "broker.emqx.io 35.172.255.228 ") && strstr(text, "broker.hivemq.com 192.168.0.10 "),
		"the file holds each name and address");
	check(!strstr(text, "evil"), "and nothing not allowed");
	p2p_resolver_cache_reset();
	check(p2p_resolver_cache_load(text, now + 200) == 2, "read back: two entries");
	check(!p2p_resolver_cache_lookup("broker.emqx.io", now + 200, &address, NULL),
		"read, but not answered until its name is allowed again");
	p2p_resolver_cache_allow("broker.emqx.io");
	check(p2p_resolver_cache_lookup("broker.emqx.io", now + 200, &address, NULL) &&
		address == address_of(35, 172, 255, 228), "answered once allowed");

	/* ---- the file's lines checked one by one */
	p2p_resolver_cache_reset();
	p2p_resolver_cache_allow("good.example.com");
	p2p_resolver_cache_allow("also.example.com");
	p2p_resolver_cache_allow("old.example.com");
	p2p_resolver_cache_allow("future.example.com");
	snprintf(text, sizeof(text),
		"# a comment\r\n"
		"good.example.com 203.0.113.7 %lu\r\n"
		"good.example.com 203.0.113.8 %lu\n"
		"bad_name.example.com 203.0.113.9 %lu\n"
		"-dash.example.com 203.0.113.9 %lu\n"
		"also.example.com 203.0.113.256 %lu\n"
		"also.example.com 203.0.113 %lu\n"
		"also.example.com 203.0.113.9.1 %lu\n"
		"also.example.com 127.0.0.1 %lu\n"
		"also.example.com  203.0.113.9 %lu\n"
		"also.example.com 203.0.113.9 12x\n"
		"also.example.com 203.0.113.9\n"
		"old.example.com 203.0.113.10 %lu\n"
		"future.example.com 203.0.113.11 %lu\n"
		"198.51.100.1 198.51.100.1 %lu\n"
		"\n"
		"also.example.com 203.0.113.12 %lu",
		now, now, now, now, now, now, now, now, now, now - P2P_RESOLVER_CACHE_MAXIMUM_AGE - 10, now + 5 * 86400, now,
		now);
	check(p2p_resolver_cache_load(text, now) == 2, "only the two good lines are taken");
	check(p2p_resolver_cache_lookup("good.example.com", now, &address, NULL) && address == address_of(203, 0, 113, 7),
		"the first of a name's lines is taken");
	check(p2p_resolver_cache_lookup("also.example.com", now, &address, NULL) && address == address_of(203, 0, 113, 12),
		"a last line without its end is read");
	check(!p2p_resolver_cache_lookup("old.example.com", now, &address, NULL), "a line too old is left out");
	check(!p2p_resolver_cache_lookup("future.example.com", now, &address, NULL), "a line from the future is left out");
	{
		char long_line[600];

		memset(long_line, 'a', sizeof(long_line) - 1);
		long_line[sizeof(long_line) - 1] = 0;
		p2p_resolver_cache_reset();
		check(p2p_resolver_cache_load(long_line, now) == 0, "a line too long is left out");
		check(p2p_resolver_cache_load("", now) == 0 && p2p_resolver_cache_load(NULL, now) == 0, "nothing is nothing");
	}

	/* ---- at most P2P_RESOLVER_CACHE_ENTRIES names: the oldest gives way */
	p2p_resolver_cache_reset();
	for (index = 0; index < P2P_RESOLVER_CACHE_ENTRIES + 4; index++)
	{
		char name[32];

		snprintf(name, sizeof(name), "b%d.example.com", index);
		p2p_resolver_cache_allow(name);
		p2p_resolver_cache_store(name, address_of(203, 0, 113, index + 1), now + (unsigned long)index);
	}
	check(!p2p_resolver_cache_allowed("b16.example.com"), "at most P2P_RESOLVER_CACHE_ENTRIES names are allowed");
	length = p2p_resolver_cache_save(text, sizeof(text));
	check(p2p_resolver_cache_load(text, now) == 0, "loading what is held already takes nothing twice");
	check(strstr(text, "b15.example.com") && !strstr(text, "b16.example.com"), "and kept");

	/* ---- a host name check: what is not a name is never kept */
	p2p_resolver_cache_reset();
	p2p_resolver_cache_allow("");
	p2p_resolver_cache_allow(NULL);
	p2p_resolver_cache_allow("a b.example.com");
	p2p_resolver_cache_allow(".example.com");
	check(!p2p_resolver_cache_allowed("a b.example.com") && !p2p_resolver_cache_allowed(".example.com") &&
		!p2p_resolver_cache_allowed(""), "not names: never allowed");

	if (failures)
		printf("%d of %d checks failed\n", failures, checks);
	else
		printf("PASS (%d checks)\n", checks);
	return failures ? 1 : 0;
}
