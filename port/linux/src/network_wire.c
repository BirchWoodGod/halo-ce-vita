/*
NETWORK_WIRE.C

Which wire a game speaks (network_wire.h, docs/crossplay.md): chosen per
game, as it is joined or hosted.
*/

#include "port_config.h"
#include "halo_port_limits.h"
#include "network_wire.h"
#include "lang.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);

/* the versions a crossplay client joins, each played by its own rules
(network_wire.h): OpenCE's 8102e711 and ChupathingyCE's 7d6d8f24 (29), and
OpenCE's 6ec42856 (32). Exactly those: 26 and 27 play otherwise with no
message to tell, 28 names a Custom Edition map and its version otherwise,
30 and 31 were never released and play part of 32's rules alone;
DamnationCE (7be58b2b) is still 26, and refused (docs/crossplay.md 5.1,
10.9). In order, lowest first */
static const unsigned short crossplay_accepted_versions[] =
	{ HALO_PORT_CROSSPLAY_OLDER_NETWORK_VERSION, HALO_PORT_CROSSPLAY_NETWORK_VERSION };

/* the game's wire and whether its host is a Vita (the game's threads and
the p2p thread read them; one writer at a time: the game's as it joins or
hosts, the p2p thread's as an invite's host answers) */
static volatile int wire;
/* (crossplay_guard.h's CROSSPLAY_PLATFORM_*: 0 a Vita) */
static volatile int host_platform;
/* the game's crossplay version (one of crossplay_accepted_versions) while
wire is set: whose rules it plays */
static volatile unsigned int crossplay_version;

int network_wire_crossplay_available(void)
{
#if defined(HALO_DEDICATED_SERVER) || !defined(HALO_PORT_VITA_NETWORK)
	/* (the servers stay off the crossplay wire; a build that does not play
	as a Vita is on the PCs' side of the old line, legacy alone) */
	return 0;
#else
	return !config_boolean("network.adhoc");
#endif
}

int network_wire_crossplay(void)
{
	return wire;
}

int network_wire_host_platform(void)
{
	return wire ? host_platform : 0;
}

int network_wire_host_vita(void)
{
	return network_wire_host_platform() == 0;
}

static int crossplay_version_listed(unsigned int version)
{
	size_t index;

	for (index = 0; index < sizeof(crossplay_accepted_versions) / sizeof(crossplay_accepted_versions[0]); index++)
	{
		if (version == crossplay_accepted_versions[index])
			return 1;
	}
	return 0;
}

void network_wire_choose_version(unsigned int version, int platform, const char *why)
{
	int crossplay = version != HALO_PORT_NETWORK_VERSION && crossplay_version_listed(version) &&
		network_wire_crossplay_available();

	platform = crossplay ? platform : 0;
	version = crossplay ? version : 0;
	if (crossplay != wire || platform != host_platform || version != crossplay_version)
	{
		if (crossplay)
		{
			platform_log("network: the crossplay wire (%s%s), network version %u's rules", why ? why : "a game",
				platform ? ", a PC's game" : ", a Vita's game", version);
		}
		else
			platform_log("network: the legacy Vita wire (%s)", why ? why : "a game");
	}
	/* (the version before the wire: a reader seeing the wire set sees its
	rules) */
	crossplay_version = version;
	wire = crossplay;
	host_platform = platform;
}

void network_wire_choose(int crossplay, int platform, const char *why)
{
	network_wire_choose_version(crossplay ? network_wire_host_version() : HALO_PORT_NETWORK_VERSION, platform, why);
}

void network_wire_reset(void)
{
	wire = 0;
	host_platform = 0;
	crossplay_version = 0;
}

unsigned int network_wire_host_version(void)
{
#ifndef HALO_RELEASE
	/* (automated tests only: a host of the older version, whose clients
	the crossplay wire still joins) */
	const char *value = getenv("HALO_TEST_CROSSPLAY_HOST_VERSION");

	if (value && value[0])
	{
		char *end;
		unsigned long version = strtoul(value, &end, 10);

		if (!*end && version <= 0xFFFF && crossplay_version_listed((unsigned int)version))
			return (unsigned int)version;
	}
#endif
	return HALO_PORT_CROSSPLAY_NETWORK_VERSION;
}

unsigned int network_wire_announced_version(int crossplay)
{
#ifndef HALO_RELEASE
	const char *value = crossplay ? getenv("HALO_TEST_ANNOUNCE_VERSION") : NULL;

	if (value && value[0])
	{
		char *end;
		unsigned long version = strtoul(value, &end, 10);

		if (!*end && version > 0 && version <= 0xFFFF)
			return (unsigned int)version;
	}
#endif
	return network_wire_version_of(crossplay);
}

unsigned int network_wire_version_of(int crossplay)
{
	return crossplay ? network_wire_host_version() : HALO_PORT_NETWORK_VERSION;
}

unsigned int network_wire_version(void)
{
	unsigned int version = crossplay_version;

	return network_wire_crossplay() && version ? version : HALO_PORT_NETWORK_VERSION;
}

/* the game's crossplay version, 0 on the legacy wire */
static unsigned int game_crossplay_version(void)
{
	unsigned int version = crossplay_version;

	return network_wire_crossplay() ? version : 0;
}

int network_wire_v28_rules(void)
{
	/* (every version the crossplay wire joins is 28 or later) */
	return game_crossplay_version() >= 28;
}

int network_wire_v30_rules(void)
{
	return game_crossplay_version() >= 30;
}

int network_wire_v31_rules(void)
{
	return game_crossplay_version() >= 31;
}

int network_wire_v32_rules(void)
{
	return game_crossplay_version() >= 32;
}

void network_wire_crossplay_versions_text(char *text, int size)
{
	size_t index, count = sizeof(crossplay_accepted_versions) / sizeof(crossplay_accepted_versions[0]);
	int length = 0;

	if (!text || size <= 0)
		return;
	text[0] = 0;
	for (index = 0; index < count && length >= 0 && length < size; index++)
	{
		length += snprintf(text + length, (size_t)(size - length), "%s%u",
			!index ? "" : index + 1 == count ? " and " : ", ", (unsigned int)crossplay_accepted_versions[index]);
	}
}

unsigned int network_wire_crossplay_version_near(unsigned int version)
{
	size_t index, count = sizeof(crossplay_accepted_versions) / sizeof(crossplay_accepted_versions[0]);

	for (index = 0; index < count; index++)
	{
		if (crossplay_accepted_versions[index] > version)
			return crossplay_accepted_versions[index];
	}
	return crossplay_accepted_versions[count - 1];
}

int network_wire_version_accepted_on(int crossplay, unsigned int version)
{
	if (!crossplay)
		return version == HALO_PORT_NETWORK_VERSION;
	if (!network_wire_crossplay_available())
		return 0;
	return crossplay_version_listed(version);
}

int network_wire_version_accepted(unsigned int version)
{
	return network_wire_version_accepted_on(network_wire_crossplay(), version);
}

int network_wire_of_version(unsigned int version)
{
	if (network_wire_version_accepted_on(0, version))
		return 0;
	if (network_wire_version_accepted_on(1, version))
		return 1;
	return -1;
}

int network_wire_host_pc_stand_in(void)
{
#ifdef HALO_RELEASE
	return 0;
#else
	const char *value = getenv("HALO_TEST_CROSSPLAY_HOST");

	return value && !strcmp(value, "1") && network_wire_crossplay_available();
#endif
}

/* (the settings the Vita's settings panel and the menus change live -
setenv, after the settings were read - are the environment's first, else
config.toml's) */

int network_wire_host_crossplay(void)
{
	const char *value = getenv("HALO_NET_HOST_CROSSPLAY");
	int chosen = value && value[0] ? !strcmp(value, "true") || !strcmp(value, "1") :
		config_boolean("network.host_crossplay");

	return network_wire_crossplay_available() && (chosen || network_wire_host_pc_stand_in());
}

int network_wire_browser_filter(void)
{
	const char *chosen = getenv("HALO_NET_BROWSER_FILTER");
	const char *filter = chosen && chosen[0] ? chosen : config_string("network.browser_filter");

	if (filter && !strcmp(filter, "vita"))
		return NETWORK_WIRE_FILTER_VITA;
	if (filter && !strcmp(filter, "pc"))
		return NETWORK_WIRE_FILTER_PC;
	/* (the retired switch's On, the filter not chosen since: PC games, as
	it showed them) */
	if (!(chosen && chosen[0]) && config_boolean("network.crossplay"))
		return NETWORK_WIRE_FILTER_PC;
	return NETWORK_WIRE_FILTER_ALL;
}

int network_wire_browser_shows(int vita_host)
{
	switch (network_wire_browser_filter())
	{
	case NETWORK_WIRE_FILTER_VITA:
		return vita_host != 0;
	case NETWORK_WIRE_FILTER_PC:
		return !vita_host;
	}
	return 1;
}
