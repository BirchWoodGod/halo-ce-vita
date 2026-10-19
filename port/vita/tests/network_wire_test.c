/*
NETWORK_WIRE_TEST.C

The network wire (port/linux/src/network_wire.c, docs/crossplay.md): no
switch since Oct 19 2026, a wire chosen per game. With no game the legacy
Vita wire (network version 18); a game joined or hosted on the crossplay wire
speaks OpenCE's version 29 (and joins nothing else: not 26 or 27, whose hosts
play a few things otherwise with no message to tell, nor 28, whose hosts
name a Custom Edition map's version otherwise, nor 30 to 32, not yet played
against), its host a Vita's or a PC's; who can join a hosted game
(network.host_crossplay, live from the environment), the browsers' filter
(network.browser_filter, the retired network.crossplay's On as PC games);
ad hoc play, the dedicated server and a build that does not play as a Vita
always on the legacy wire. Built by run_network_wire_test.sh, once for each
build (a Vita's, its release, the dedicated server's, a PC build's).
*/

#include "../../linux/src/network_wire.c"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- stand-ins: the settings, the log and the translations */

static int crossplay_setting, adhoc_setting, host_crossplay_setting;
static const char *filter_setting = "all";

int config_boolean(const char *name)
{
	if (!strcmp(name, "network.crossplay"))
		return crossplay_setting;
	if (!strcmp(name, "network.adhoc"))
		return adhoc_setting;
	if (!strcmp(name, "network.host_crossplay"))
		return host_crossplay_setting;
	return 0;
}

const char *config_string(const char *name)
{
	if (!strcmp(name, "network.browser_filter"))
		return filter_setting;
	return "";
}

static int logged;

void platform_log(const char *format, ...)
{
	va_list arguments;

	logged++;
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

const char *lang_text(const char *english)
{
	return english;
}

/* ---------- the checks */

static int failures, checks;

static void check(int good, const char *what)
{
	checks++;
	failures += !good;
	printf("%s %s\n", good ? "PASS" : "FAIL", what);
}

static int versions_only(int crossplay, unsigned int version)
{
	static const unsigned int tried[] = { 0, 4, 17, 18, 19, 25, 26, 27, 28, 29, 30, 31, 32, 0xFFFF };
	size_t index;

	for (index = 0; index < sizeof(tried) / sizeof(tried[0]); index++)
	{
		if (network_wire_version_accepted_on(crossplay, tried[index]) != (tried[index] == version))
			return 0;
	}
	return 1;
}

int main(void)
{
	/* (no game: the legacy wire, every build) */
	check(!network_wire_crossplay() && network_wire_version() == HALO_PORT_NETWORK_VERSION &&
		HALO_PORT_NETWORK_VERSION == 18 && !network_wire_v28_rules() && network_wire_host_vita(),
		"no game: the legacy Vita wire, network version 18, the betas' rules, a Vita's");
	check(versions_only(0, 18), "the legacy wire joins version 18 alone");
	check(network_wire_of_version(18) == 0, "a host of 18 plays the legacy wire");
#if defined(HALO_DEDICATED_SERVER) || !defined(HALO_NET_AS_VITA)
	/* (the dedicated server, a build that does not play as a Vita) */
	host_crossplay_setting = 1;
	setenv("HALO_TEST_CROSSPLAY_HOST", "1", 1);
	check(!network_wire_crossplay_available() && !network_wire_host_crossplay(),
		"no crossplay wire here: it hosts for Vitas alone, whatever is set");
	network_wire_choose(1, 0, "a test");
	check(!network_wire_crossplay() && network_wire_version() == 18 && !network_wire_v28_rules(),
		"asked for the crossplay wire, it stays on the legacy wire and its rules");
	check(!network_wire_version_accepted_on(1, 29) && network_wire_of_version(29) == -1,
		"and joins no crossplay host");
	unsetenv("HALO_TEST_CROSSPLAY_HOST");
	printf("%s (%d of %d checks failed)\n", failures ? "FAIL" : "PASS", failures, checks);
	return failures != 0;
#else
	check(network_wire_crossplay_available(), "a Vita: the crossplay wire available");
	check(versions_only(1, 29), "the crossplay wire joins 29 alone (not 26-28, not 30-32)");
	check(network_wire_of_version(29) == 1 && network_wire_of_version(28) == -1 && network_wire_of_version(32) == -1 &&
		network_wire_of_version(26) == -1, "29 plays the crossplay wire; 26, 28, 32 neither wire");
	check(network_wire_version_of(1) == 29 && network_wire_version_of(0) == 18, "the versions each wire announces");

	/* a game joined on the crossplay wire: a PC's, then a Vita's */
	network_wire_choose(1, 2, "a PC host's game");
	check(network_wire_crossplay() && network_wire_version() == 29 && network_wire_v28_rules() &&
		!network_wire_host_vita() && network_wire_host_platform() == 2,
		"a PC's game joined: the crossplay wire, 29, version 28's rules, its host a PC's");
	check(network_wire_version_accepted(29) && !network_wire_version_accepted(18), "on that wire 29 is joined, 18 not");
	network_wire_choose(1, 0, "a Vita's game for Vitas and PC");
	check(network_wire_crossplay() && network_wire_host_vita(), "a Vita's crossplay game: its host a Vita");
	network_wire_choose(0, 2, "a Vita's game");
	check(!network_wire_crossplay() && network_wire_host_vita() && network_wire_host_platform() == 0,
		"a legacy game: the legacy wire, a Vita's whatever was said");
	network_wire_choose(1, 3, "another");
	network_wire_reset();
	check(!network_wire_crossplay() && network_wire_host_vita(), "reset: the legacy wire again");

	/* who can join: the setting, live from the environment */
	check(!network_wire_host_crossplay(), "hosting: Vitas, as shipped");
	host_crossplay_setting = 1;
	check(network_wire_host_crossplay(), "network.host_crossplay true: Vitas and PC");
	setenv("HALO_NET_HOST_CROSSPLAY", "false", 1);
	check(!network_wire_host_crossplay(), "the panel's Vitas (HALO_NET_HOST_CROSSPLAY) over config.toml's");
	setenv("HALO_NET_HOST_CROSSPLAY", "true", 1);
	host_crossplay_setting = 0;
	check(network_wire_host_crossplay(), "the panel's Vitas and PC over config.toml's");
	adhoc_setting = 1;
	check(!network_wire_crossplay_available() && !network_wire_host_crossplay(), "ad hoc: no crossplay wire, Vitas alone");
	network_wire_choose(1, 0, "ad hoc");
	check(!network_wire_crossplay(), "ad hoc: asked for the crossplay wire, the legacy one");
	adhoc_setting = 0;
	unsetenv("HALO_NET_HOST_CROSSPLAY");

	/* the PC host's stand-in, the tests' */
	setenv("HALO_TEST_CROSSPLAY_HOST", "1", 1);
#ifdef HALO_RELEASE
	check(!network_wire_host_pc_stand_in() && !network_wire_host_crossplay(),
		"a release build: HALO_TEST_CROSSPLAY_HOST changes nothing");
#else
	check(network_wire_host_pc_stand_in() && network_wire_host_crossplay(),
		"a test build with HALO_TEST_CROSSPLAY_HOST=1: a PC host's stand-in, on the crossplay wire");
#endif
	unsetenv("HALO_TEST_CROSSPLAY_HOST");

	/* the browsers' filter */
	check(network_wire_browser_filter() == NETWORK_WIRE_FILTER_ALL && network_wire_browser_shows(1) &&
		network_wire_browser_shows(0), "Browser shows: all games, as shipped");
	filter_setting = "vita";
	check(network_wire_browser_shows(1) && !network_wire_browser_shows(0), "vita: the Vitas' games alone");
	filter_setting = "pc";
	check(!network_wire_browser_shows(1) && network_wire_browser_shows(0), "pc: the PCs' games alone");
	filter_setting = "all";
	crossplay_setting = 1;
	check(network_wire_browser_filter() == NETWORK_WIRE_FILTER_PC,
		"the retired network.crossplay On, the filter not chosen: PC games, as the switch showed");
	setenv("HALO_NET_BROWSER_FILTER", "all", 1);
	check(network_wire_browser_filter() == NETWORK_WIRE_FILTER_ALL, "a filter chosen since (the panel's): it holds");
	setenv("HALO_NET_BROWSER_FILTER", "vita", 1);
	check(network_wire_browser_filter() == NETWORK_WIRE_FILTER_VITA, "the panel's Vita games, live");
	unsetenv("HALO_NET_BROWSER_FILTER");
	crossplay_setting = 0;
	setenv("HALO_NET_BROWSER_FILTER", "nonsense", 1);
	check(network_wire_browser_filter() == NETWORK_WIRE_FILTER_ALL, "a value it does not know: all");
	unsetenv("HALO_NET_BROWSER_FILTER");
#endif
	printf("%s (%d of %d checks failed)\n", failures ? "FAIL" : "PASS", failures, checks);
	return failures != 0;
}
