/*
NETWORK_WIRE_TEST.C

The network wire switch (port/linux/src/network_wire.c, docs/crossplay.md
T4): network.crossplay read once and kept for the run (a change takes a
restart); Off, the legacy Vita wire (network version 18, hosting allowed);
On, the crossplay wire (OpenCE's version 28 joined, nothing else: not 26 or
27, whose hosts play a few things otherwise with no message to tell; hosting
refused, with the player told to turn Crossplay off); ad hoc play always on
the legacy wire; the dedicated server (HALO_DEDICATED_SERVER) never on the
crossplay wire. Built by run_network_wire_test.sh, once for each case
(NETWORK_WIRE_TEST_CASE), as the switch is read once a run.
*/

#include "../../linux/src/network_wire.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- stand-ins: the settings and the translations */

static int crossplay_setting, adhoc_setting, reads;

int config_boolean(const char *name)
{
	reads++;
	if (!strcmp(name, "network.crossplay"))
		return crossplay_setting;
	if (!strcmp(name, "network.adhoc"))
		return adhoc_setting;
	return 0;
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

int main(int argc, char **argv)
{
	const char *which = argc > 1 ? argv[1] : "off";
	const char *why = NULL;

	crossplay_setting = !strcmp(which, "on") || !strcmp(which, "adhoc");
	adhoc_setting = !strcmp(which, "adhoc");
#ifdef HALO_DEDICATED_SERVER
	crossplay_setting = 1;
	check(!network_wire_crossplay() && network_wire_version() == HALO_PORT_NETWORK_VERSION &&
		!network_wire_v28_rules() && !network_wire_hosting_refused(&why),
		"the dedicated server: the legacy wire and its rules, Crossplay on or not");
	printf("%s (%d of %d checks failed)\n", failures ? "FAIL" : "PASS", failures, checks);
	return failures != 0;
#endif
	if (!strcmp(which, "on"))
	{
		check(network_wire_crossplay(), "Crossplay on: the crossplay wire");
		check(network_wire_version() == 28, "it speaks OpenCE's network version 28");
		check(network_wire_version_accepted(28), "it joins a host of version 28");
		check(network_wire_v28_rules(), "and plays version 28's rules (switch_bsp, lights, a second weapon)");
		check(!network_wire_version_accepted(26) && !network_wire_version_accepted(27),
			"not an older PC host of version 26 or 27 (played otherwise, no message to tell)");
		check(!network_wire_version_accepted(18) && !network_wire_version_accepted(25) &&
			!network_wire_version_accepted(29) && !network_wire_version_accepted(30) &&
			!network_wire_version_accepted(0) && !network_wire_version_accepted(0xFFFF),
			"and of no other version (18, 25, 29, 30, 0, 65535)");
		check(network_wire_hosting_refused(&why) && why && strstr(why, "turn Crossplay off"),
			"hosting refused, the player told to turn Crossplay off");
		setenv("HALO_TEST_CROSSPLAY_HOST", "1", 1);
#ifdef HALO_RELEASE
		check(network_wire_hosting_refused(NULL), "a release build: HALO_TEST_CROSSPLAY_HOST changes nothing");
#else
		check(!network_wire_hosting_refused(NULL), "a test build with HALO_TEST_CROSSPLAY_HOST=1: a PC host's stand-in");
#endif
		unsetenv("HALO_TEST_CROSSPLAY_HOST");
		{
			int before = reads;

			crossplay_setting = 0;
			check(network_wire_crossplay() && reads == before, "read once: a change takes a restart");
		}
	}
	else if (!strcmp(which, "adhoc"))
	{
		check(!network_wire_crossplay() && network_wire_version() == HALO_PORT_NETWORK_VERSION &&
			!network_wire_v28_rules(), "ad hoc: the legacy wire and its rules, though Crossplay is on (no PC reaches ad hoc)");
		check(!network_wire_hosting_refused(&why), "ad hoc: hosting allowed");
	}
	else
	{
		check(!network_wire_crossplay(), "Crossplay off: the legacy Vita wire");
		check(network_wire_version() == HALO_PORT_NETWORK_VERSION && HALO_PORT_NETWORK_VERSION == 18,
			"it speaks network version 18, as betas 1-4");
		check(!network_wire_v28_rules(), "and plays the betas' rules, not version 28's");
		check(network_wire_version_accepted(18) && !network_wire_version_accepted(26) &&
			!network_wire_version_accepted(28) && !network_wire_version_accepted(19) &&
			!network_wire_version_accepted(17),
			"it joins a host of version 18 only");
		why = NULL;
		check(!network_wire_hosting_refused(&why) && !why, "hosting allowed");
		{
			int before = reads;

			crossplay_setting = 1;
			check(!network_wire_crossplay() && reads == before, "read once: a change takes a restart");
		}
	}
	printf("%s (%d of %d checks failed)\n", failures ? "FAIL" : "PASS", failures, checks);
	return failures != 0;
}
