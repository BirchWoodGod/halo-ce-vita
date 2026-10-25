/*
NETWORK_WIRE_TEST.C

The network wire (port/linux/src/network_wire.c, docs/crossplay.md): no
switch since Oct 19 2026, a wire chosen per game. With no game the legacy
Vita wire (network version 18); a game joined or hosted on the crossplay wire
speaks the version of its host, 29 or 32, each by its own rules (and joins
nothing else: not 26 or 27, whose hosts play a few things otherwise with no
message to tell, nor 28, whose hosts name a Custom Edition map's version
otherwise, nor 30 or 31, never released, nor 33 and up, not played), a host
for everyone announcing 32, its host a Vita's or a PC's; who can join a
hosted game (network.who_can_join, live from the environment: Vitas only,
Vitas and Halo CE Vita PC, everyone; the retired two-way
network.host_crossplay read as Vitas only or everyone), this build's
platform (a Vita, or the port's PC build: the join request's mark;
HALO_TEST_PLATFORM in a test build), the host's refusal of the PC build,
the browsers' filter
(network.browser_filter, the retired network.crossplay's On as PC games);
ad hoc play, the dedicated server and a build that does not play as a Vita
always on the legacy wire. Built by run_network_wire_test.sh, once for each
build (a Vita's, the PC build's and its release, the dedicated server's, a
build that does not play as a Vita).
*/

#include "../../linux/src/network_wire.c"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- stand-ins: the settings, the log and the translations */

static int crossplay_setting, adhoc_setting, host_crossplay_setting;
static const char *filter_setting = "all", *who_setting = "";

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
	if (!strcmp(name, "network.who_can_join"))
		return who_setting;
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

static int versions_only3(int crossplay, unsigned int version, unsigned int other, unsigned int third)
{
	static const unsigned int tried[] = { 0, 4, 17, 18, 19, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
		0xFFFF };
	size_t index;

	for (index = 0; index < sizeof(tried) / sizeof(tried[0]); index++)
	{
		if (network_wire_version_accepted_on(crossplay, tried[index]) !=
			(tried[index] == version || tried[index] == other || tried[index] == third))
		{
			return 0;
		}
	}
	return 1;
}

static int versions_only(int crossplay, unsigned int version, unsigned int other)
{
	return versions_only3(crossplay, version, other, other);
}

static int no_rules_past_28(void)
{
	return !network_wire_v30_rules() && !network_wire_v31_rules() && !network_wire_v32_rules() && !network_wire_v33_rules() &&
		!network_wire_v34_rules() && !network_wire_v35_rules() && !network_wire_v36_rules();
}

int main(void)
{
	/* (test_out.sh's stand-in for a Vita: each check sets its own) */
	unsetenv("HALO_TEST_PLATFORM");
	/* (no game: the legacy wire, every build) */
	check(!network_wire_crossplay() && network_wire_version() == HALO_PORT_NETWORK_VERSION &&
		HALO_PORT_NETWORK_VERSION == 18 && !network_wire_v28_rules() && network_wire_host_vita(),
		"no game: the legacy Vita wire, network version 18, the betas' rules, a Vita's");
	check(versions_only(0, 18, 18), "the legacy wire joins version 18 alone");
	check(no_rules_past_28(), "no game: none of 30, 31 or 32's rules");
	check(network_wire_of_version(18) == 0, "a host of 18 plays the legacy wire");

	/* this build's platform, and the join request's mark of it */
#if defined(HALO_VITA) || defined(HALO_DEDICATED_SERVER)
	check(network_wire_this_platform() == NETWORK_PLATFORM_VITA, "a Vita (or the dedicated server): a Vita");
	setenv("HALO_TEST_PLATFORM", "pc", 1);
	check(network_wire_this_platform() == NETWORK_PLATFORM_VITA, "HALO_TEST_PLATFORM changes nothing here");
	unsetenv("HALO_TEST_PLATFORM");
#else
	check(network_wire_this_platform() == NETWORK_PLATFORM_VITA_PC, "a desktop build: the PC build");
	setenv("HALO_TEST_PLATFORM", "vita", 1);
#ifdef HALO_RELEASE
	check(network_wire_this_platform() == NETWORK_PLATFORM_VITA_PC, "a release: HALO_TEST_PLATFORM changes nothing");
#else
	check(network_wire_this_platform() == NETWORK_PLATFORM_VITA, "a test build: HALO_TEST_PLATFORM=vita stands in for a Vita");
#endif
	unsetenv("HALO_TEST_PLATFORM");
#endif
	check(network_wire_join_platform_of_mark(network_wire_join_platform_mark()) == network_wire_this_platform(),
		"the join request's mark says this build's platform");
	check(network_wire_join_platform_of_mark(0x5601) == NETWORK_PLATFORM_VITA &&
		network_wire_join_platform_of_mark(0x5602) == NETWORK_PLATFORM_VITA_PC, "a Vita's mark, the PC build's");
	check(network_wire_join_platform_of_mark(0) == NETWORK_WIRE_JOIN_PLATFORM_UNKNOWN &&
		network_wire_join_platform_of_mark('a') == NETWORK_WIRE_JOIN_PLATFORM_UNKNOWN &&
		network_wire_join_platform_of_mark(0x5600) == NETWORK_WIRE_JOIN_PLATFORM_UNKNOWN &&
		network_wire_join_platform_of_mark(0x5603) == NETWORK_WIRE_JOIN_PLATFORM_UNKNOWN &&
		network_wire_join_platform_of_mark(0xFFFF) == NETWORK_WIRE_JOIN_PLATFORM_UNKNOWN,
		"no mark (an older build's, a name's character): unknown");
	check(network_wire_audience_parse("vita") == NETWORK_AUDIENCE_VITAS &&
		network_wire_audience_parse("vitas") == NETWORK_AUDIENCE_VITAS &&
		network_wire_audience_parse("vita_pc") == NETWORK_AUDIENCE_VITA_PC &&
		network_wire_audience_parse("everyone") == NETWORK_AUDIENCE_EVERYONE &&
		network_wire_audience_parse("false") == NETWORK_AUDIENCE_VITAS &&
		network_wire_audience_parse("true") == NETWORK_AUDIENCE_EVERYONE &&
		network_wire_audience_parse("1") == NETWORK_AUDIENCE_EVERYONE && network_wire_audience_parse("") == -1 &&
		network_wire_audience_parse("pc") == -1 && network_wire_audience_parse(NULL) == -1,
		"Who can join's values, and the retired setting's (false: Vitas only, true: everyone)");
	check(!strcmp(network_wire_audience_value(NETWORK_AUDIENCE_VITA_PC), "vita_pc"), "its values written");
#if defined(HALO_DEDICATED_SERVER) || !defined(HALO_PORT_VITA_NETWORK)
	/* (the dedicated server, a build that does not play as a Vita) */
	host_crossplay_setting = 1;
	setenv("HALO_TEST_CROSSPLAY_HOST", "1", 1);
	check(!network_wire_crossplay_available() && !network_wire_host_crossplay(),
		"no crossplay wire here: it hosts on the Vitas' wire, whatever is set");
#ifdef HALO_DEDICATED_SERVER
	host_crossplay_setting = 0;
	unsetenv("HALO_TEST_CROSSPLAY_HOST");
	check(network_wire_host_audience() == NETWORK_AUDIENCE_VITA_PC && !network_wire_host_refuses(NETWORK_PLATFORM_VITA_PC),
		"the dedicated server takes the PC build, as shipped");
	who_setting = "vita";
	check(network_wire_host_refuses(NETWORK_PLATFORM_VITA_PC), "... and refuses it when set for Vita only");
	who_setting = "";
	setenv("HALO_TEST_CROSSPLAY_HOST", "1", 1);
#endif
	network_wire_choose(1, 0, "a test");
	check(!network_wire_crossplay() && network_wire_version() == 18 && !network_wire_v28_rules() && no_rules_past_28(),
		"asked for the crossplay wire, it stays on the legacy wire and its rules");
	network_wire_choose_version(32, 2, "a test");
	check(!network_wire_crossplay() && network_wire_version() == 18 && no_rules_past_28(),
		"asked for a game of 32, the legacy wire and its rules");
	check(!network_wire_version_accepted_on(1, 29) && !network_wire_version_accepted_on(1, 32) &&
		network_wire_of_version(29) == -1 && network_wire_of_version(32) == -1,
		"and joins no crossplay host");
	unsetenv("HALO_TEST_CROSSPLAY_HOST");
	printf("%s (%d of %d checks failed)\n", failures ? "FAIL" : "PASS", failures, checks);
	return failures != 0;
#else
	check(network_wire_crossplay_available(), "a Vita: the crossplay wire available");
	check(versions_only3(1, 29, 32, 38), "the crossplay wire joins 29, 32 and 38 alone (not 26-28, 30, 31, 33-37, 39 and up)");
	check(network_wire_of_version(29) == 1 && network_wire_of_version(32) == 1 && network_wire_of_version(38) == 1 &&
		network_wire_of_version(28) == -1 && network_wire_of_version(30) == -1 && network_wire_of_version(31) == -1 &&
		network_wire_of_version(33) == -1 && network_wire_of_version(37) == -1 && network_wire_of_version(39) == -1 &&
		network_wire_of_version(26) == -1, "29, 32 and 38 play the crossplay wire; 26, 28, 30, 31, 33-37, 39 neither wire");
	check(network_wire_version_of(1) == 32 && network_wire_host_version() == 32 && network_wire_version_of(0) == 18,
		"the versions each wire announces: a host for Vitas and PC, 32");
	{
		char text[32];

		network_wire_crossplay_versions_text(text, (int)sizeof(text));
		check(!strcmp(text, "29, 32 and 38"), "the versions joined, for the log: 29, 32 and 38");
	}
	check(network_wire_crossplay_version_near(26) == 29 && network_wire_crossplay_version_near(28) == 29 &&
		network_wire_crossplay_version_near(30) == 32 && network_wire_crossplay_version_near(31) == 32 &&
		network_wire_crossplay_version_near(33) == 38 && network_wire_crossplay_version_near(37) == 38 &&
		network_wire_crossplay_version_near(0xFFFF) == 38,
		"the version to look for instead: 29 for 26-28, 32 for 30-31, 38 for 33-37 and newer");

	/* a game joined on the crossplay wire: a PC's of 32, of 29, then a Vita's */
	network_wire_choose_version(32, 2, "a PC host's game of 32");
	check(network_wire_crossplay() && network_wire_version() == 32 && network_wire_v28_rules() && network_wire_v30_rules() &&
		network_wire_v31_rules() && network_wire_v32_rules() && !network_wire_host_vita() && network_wire_host_platform() == 2,
		"a PC's game of 32 joined: the crossplay wire, 32, the rules of 28 to 32, its host a PC's");
	check(!network_wire_v33_rules() && !network_wire_v34_rules() && !network_wire_v35_rules() && !network_wire_v36_rules(),
		"... and none of 33-36's");
	network_wire_choose_version(38, 3, "a ChupathingyCE server's game of 38");
	check(network_wire_crossplay() && network_wire_version() == 38 && network_wire_v32_rules() && network_wire_v33_rules() &&
		network_wire_v34_rules() && network_wire_v35_rules() && network_wire_v36_rules() && network_wire_host_platform() == 3,
		"a PC's game of 38 joined: the crossplay wire, 38, the rules of 28 to 36");
	network_wire_choose_version(32, 2, "a PC host's game of 32");
	check(network_wire_version_accepted(29) && network_wire_version_accepted(32) && !network_wire_version_accepted(18),
		"on that wire 29 and 32 are joined, 18 not");
	network_wire_choose_version(29, 3, "a ChupathingyCE host's game of 29");
	check(network_wire_crossplay() && network_wire_version() == 29 && network_wire_v28_rules() && no_rules_past_28() &&
		network_wire_host_platform() == 3,
		"a PC's game of 29 joined: the crossplay wire, 29, version 28's rules and none of 30-32's");
	network_wire_choose_version(31, 2, "a game of 31");
	check(!network_wire_crossplay() && network_wire_version() == 18 && no_rules_past_28(),
		"a version the wire does not join (31): never played, the legacy wire");
	network_wire_choose_version(18, 2, "a Vita's game");
	check(!network_wire_crossplay() && network_wire_host_vita() && network_wire_version() == 18, "18: the legacy wire");
	network_wire_choose(1, 0, "a Vita's game for Vitas and PC");
	check(network_wire_crossplay() && network_wire_host_vita() && network_wire_version() == 32 && network_wire_v32_rules(),
		"a Vita's crossplay game (hosted): its host a Vita, 32 and its rules");
	setenv("HALO_TEST_CROSSPLAY_HOST_VERSION", "29", 1);
#ifdef HALO_RELEASE
	check(network_wire_host_version() == 32, "a release build: HALO_TEST_CROSSPLAY_HOST_VERSION changes nothing");
#else
	network_wire_choose(1, 0, "a test host of 29");
	check(network_wire_host_version() == 29 && network_wire_version() == 29 && network_wire_version_of(1) == 29 &&
		no_rules_past_28(), "a test build with HALO_TEST_CROSSPLAY_HOST_VERSION=29: hosts 29, by its rules");
	setenv("HALO_TEST_CROSSPLAY_HOST_VERSION", "31", 1);
	check(network_wire_host_version() == 32, "... and never a version the wire does not join (31): 32");
#endif
	unsetenv("HALO_TEST_CROSSPLAY_HOST_VERSION");
	network_wire_choose(0, 2, "a Vita's game");
	check(!network_wire_crossplay() && network_wire_host_vita() && network_wire_host_platform() == 0,
		"a legacy game: the legacy wire, a Vita's whatever was said");
	network_wire_choose(1, 3, "another");
	network_wire_reset();
	check(!network_wire_crossplay() && network_wire_host_vita() && network_wire_version() == 18 && no_rules_past_28(),
		"reset: the legacy wire again");

	/* who can join: the setting, live from the environment; a Vita's
	default Vitas only, the PC build's Vitas and Halo CE Vita PC, which is
	its least */
	{
		int pc = network_wire_this_platform() == NETWORK_PLATFORM_VITA_PC;
		int least = pc ? NETWORK_AUDIENCE_VITA_PC : NETWORK_AUDIENCE_VITAS;

		check(network_wire_audience_default() == least && network_wire_audience_first() == least &&
			network_wire_host_audience() == least && !network_wire_host_crossplay(),
			pc ? "the PC build hosts for Vitas and Halo CE Vita PC, as shipped" : "a Vita hosts for Vitas only, as shipped");
		check(network_wire_host_refuses(NETWORK_PLATFORM_VITA_PC) == !pc && !network_wire_host_refuses(NETWORK_PLATFORM_VITA) &&
			!network_wire_host_refuses(NETWORK_WIRE_JOIN_PLATFORM_UNKNOWN),
			pc ? "the PC build refuses no one" : "Vitas only: the PC build refused; a Vita, and a build that says nothing, not");
		who_setting = "vita_pc";
		check(network_wire_host_audience() == NETWORK_AUDIENCE_VITA_PC && !network_wire_host_crossplay() &&
			!network_wire_host_refuses(NETWORK_PLATFORM_VITA_PC), "vita_pc: the legacy wire, the PC build joins");
		who_setting = "vita";
		check(network_wire_host_audience() == least,
			pc ? "vitas on the PC build: Vitas and Halo CE Vita PC (a PC host is no Vita)" : "vitas: Vitas only");
		who_setting = "everyone";
		check(network_wire_host_audience() == NETWORK_AUDIENCE_EVERYONE && network_wire_host_crossplay() &&
			!network_wire_host_refuses(NETWORK_PLATFORM_VITA_PC), "everyone: the crossplay wire");
		network_wire_choose(network_wire_host_crossplay(), 0, "hosting for everyone");
		check(network_wire_crossplay() && network_wire_version() == HALO_PORT_CROSSPLAY_NETWORK_VERSION &&
			network_wire_announced_version(1) == HALO_PORT_CROSSPLAY_NETWORK_VERSION && network_wire_v32_rules(),
			"everyone: hosted at OpenCE's 32, announced 32, by 32's rules");
		network_wire_reset();
		who_setting = "vita_pc";
		network_wire_choose(network_wire_host_crossplay(), 0, "hosting for Vitas and Vita PC");
		check(!network_wire_crossplay() && network_wire_version() == HALO_PORT_NETWORK_VERSION && no_rules_past_28(),
			"vita_pc: hosted at 18, none of 30-32's rules");
		network_wire_reset();
		who_setting = "everyone";
		who_setting = "";
		host_crossplay_setting = 1;
		check(network_wire_host_audience() == NETWORK_AUDIENCE_EVERYONE,
			"the retired network.host_crossplay true (Vitas and PC): everyone");
		setenv("HALO_NET_HOST_CROSSPLAY", "false", 1);
		check(network_wire_host_audience() == least, "the retired HALO_NET_HOST_CROSSPLAY=false over config.toml's: Vitas only");
		setenv("HALO_NET_HOST_CROSSPLAY", "true", 1);
		host_crossplay_setting = 0;
		check(network_wire_host_crossplay(), "the retired HALO_NET_HOST_CROSSPLAY=true: everyone");
		setenv("HALO_NET_WHO_CAN_JOIN", "vita_pc", 1);
		check(network_wire_host_audience() == NETWORK_AUDIENCE_VITA_PC, "the panel's HALO_NET_WHO_CAN_JOIN over the retired one");
		setenv("HALO_NET_WHO_CAN_JOIN", "everyone", 1);
		adhoc_setting = 1;
		check(!network_wire_crossplay_available() && !network_wire_host_crossplay() &&
			network_wire_host_audience() == NETWORK_AUDIENCE_VITA_PC, "ad hoc: no crossplay wire, everyone the Vitas' wire");
		network_wire_choose(1, 0, "ad hoc");
		check(!network_wire_crossplay(), "ad hoc: asked for the crossplay wire, the legacy one");
		adhoc_setting = 0;
		setenv("HALO_NET_WHO_CAN_JOIN", "vita", 1);
		network_wire_choose(1, 0, "a crossplay game");
		check(!network_wire_host_refuses(NETWORK_PLATFORM_VITA_PC), "on the crossplay wire nothing is refused");
		network_wire_reset();
		unsetenv("HALO_NET_WHO_CAN_JOIN");
		unsetenv("HALO_NET_HOST_CROSSPLAY");
#ifndef HALO_RELEASE
		/* (a test build standing in for a Vita) */
		setenv("HALO_TEST_PLATFORM", "vita", 1);
		setenv("HALO_NET_WHO_CAN_JOIN", "vita", 1);
		check(network_wire_host_audience() == NETWORK_AUDIENCE_VITAS && network_wire_host_refuses(NETWORK_PLATFORM_VITA_PC),
			"a Vita's stand-in for Vitas only refuses the PC build");
		unsetenv("HALO_NET_WHO_CAN_JOIN");
		unsetenv("HALO_TEST_PLATFORM");
#endif
	}

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
