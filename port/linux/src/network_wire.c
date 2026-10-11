/*
NETWORK_WIRE.C

The network wire switch (network_wire.h, docs/crossplay.md): network.crossplay
read once and kept for the run.
*/

#include "port_config.h"
#include "halo_port_limits.h"
#include "network_wire.h"
#include "lang.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* the versions a crossplay client joins: the pinned OpenCE's (6399ed9d).
ChupathingyCE and DamnationCE hosts announce the same (docs/crossplay.md 2) */
static const unsigned short crossplay_accepted_versions[] = { HALO_PORT_CROSSPLAY_NETWORK_VERSION };

int network_wire_crossplay(void)
{
#ifdef HALO_DEDICATED_SERVER
	/* (the servers stay off the crossplay wire) */
	return 0;
#else
	/* (-1: not read yet. Read at the network's start, when signalling or the
	game first asks, and kept: a change takes a restart) */
	static volatile int wire = -1;

	if (wire < 0)
		wire = config_boolean("network.crossplay") && !config_boolean("network.adhoc");
	return wire;
#endif
}

unsigned int network_wire_version(void)
{
	return network_wire_crossplay() ? HALO_PORT_CROSSPLAY_NETWORK_VERSION : HALO_PORT_NETWORK_VERSION;
}

int network_wire_version_accepted(unsigned int version)
{
	size_t index;

	if (!network_wire_crossplay())
		return version == HALO_PORT_NETWORK_VERSION;
	for (index = 0; index < sizeof(crossplay_accepted_versions) / sizeof(crossplay_accepted_versions[0]); index++)
	{
		if (version == crossplay_accepted_versions[index])
			return 1;
	}
	return 0;
}

/* (automated tests only, never in release builds) HALO_TEST_CROSSPLAY_HOST=1:
this machine hosts on the crossplay wire, a stand-in for a PC host (OpenCE's
advertisement, join token and settings record, on a LAN), for the record's
end-to-end test (run_crossplay_record_test.sh); a Vita never does */
static int network_wire_test_host(void)
{
#ifdef HALO_RELEASE
	return 0;
#else
	const char *value = getenv("HALO_TEST_CROSSPLAY_HOST");

	return value && !strcmp(value, "1");
#endif
}

int network_wire_hosting_refused(const char **why)
{
	if (!network_wire_crossplay() || network_wire_test_host())
		return 0;
	if (why)
		*why = T("Crossplay is on: a Vita joins PC games but does not host them. "
			"To host a game, turn Crossplay off (Settings, Multiplayer) and restart.");
	return 1;
}
