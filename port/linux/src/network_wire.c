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
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);

/* the versions a crossplay client joins: the pinned OpenCE's (8102e711),
which ChupathingyCE hosts announce too (7d6d8f24). Exactly that one: 26 and
27 play otherwise with no message to tell, and 28 names a Custom Edition
map and its version otherwise (network_wire.h); DamnationCE (7be58b2b) is
still 26, and refused; OpenCE 6ec42856 is 32, not yet played against
(docs/crossplay.md 5.1, 10.7) */
static const unsigned short crossplay_accepted_versions[] = { HALO_PORT_CROSSPLAY_NETWORK_VERSION };

/* the game's wire and whether its host is a Vita (the game's threads and
the p2p thread read them; one writer at a time: the game's as it joins or
hosts, the p2p thread's as an invite's host answers) */
static volatile int wire;
/* (crossplay_guard.h's CROSSPLAY_PLATFORM_*: 0 a Vita) */
static volatile int host_platform;

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

void network_wire_choose(int crossplay, int platform, const char *why)
{
	crossplay = crossplay && network_wire_crossplay_available();
	platform = crossplay ? platform : 0;
	if (crossplay != wire || platform != host_platform)
	{
		platform_log("network: the %s wire (%s%s)", crossplay ? "crossplay" : "legacy Vita",
			why ? why : "a game", crossplay ? platform ? ", a PC's game" : ", a Vita's game" : "");
	}
	wire = crossplay;
	host_platform = platform;
}

void network_wire_reset(void)
{
	wire = 0;
	host_platform = 0;
}

unsigned int network_wire_version_of(int crossplay)
{
	return crossplay ? HALO_PORT_CROSSPLAY_NETWORK_VERSION : HALO_PORT_NETWORK_VERSION;
}

unsigned int network_wire_version(void)
{
	return network_wire_version_of(network_wire_crossplay());
}

int network_wire_v28_rules(void)
{
	/* (every version the crossplay wire joins is 28 or later) */
	return network_wire_crossplay() && HALO_PORT_CROSSPLAY_NETWORK_VERSION >= 28;
}

int network_wire_version_accepted_on(int crossplay, unsigned int version)
{
	size_t index;

	if (!crossplay)
		return version == HALO_PORT_NETWORK_VERSION;
	if (!network_wire_crossplay_available())
		return 0;
	for (index = 0; index < sizeof(crossplay_accepted_versions) / sizeof(crossplay_accepted_versions[0]); index++)
	{
		if (version == crossplay_accepted_versions[index])
			return 1;
	}
	return 0;
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

int network_wire_this_platform(void)
{
#if defined(HALO_VITA) || defined(HALO_DEDICATED_SERVER)
	return NETWORK_PLATFORM_VITA;
#else
#ifndef HALO_RELEASE
	const char *test = getenv("HALO_TEST_PLATFORM");

	if (test && !strcmp(test, "vita"))
		return NETWORK_PLATFORM_VITA;
#endif
	return NETWORK_PLATFORM_VITA_PC;
#endif
}

static const char *const audience_values[NUMBER_OF_NETWORK_AUDIENCES] = { "vita", "vita_pc", "everyone" };

const char *network_wire_audience_value(int audience)
{
	return audience >= 0 && audience < NUMBER_OF_NETWORK_AUDIENCES ? audience_values[audience] : audience_values[0];
}

int network_wire_audience_parse(const char *value)
{
	int audience;

	if (!value || !value[0])
		return -1;
	for (audience = 0; audience < NUMBER_OF_NETWORK_AUDIENCES; audience++)
	{
		if (!strcmp(value, audience_values[audience]))
			return audience;
	}
	/* (the retired network.host_crossplay's: "Vitas" and "Vitas and PC"; and
	"vitas") */
	if (!strcmp(value, "vitas"))
		return NETWORK_AUDIENCE_VITAS;
	if (!strcmp(value, "true") || !strcmp(value, "1"))
		return NETWORK_AUDIENCE_EVERYONE;
	if (!strcmp(value, "false") || !strcmp(value, "0"))
		return NETWORK_AUDIENCE_VITAS;
	return -1;
}

int network_wire_audience_default(void)
{
	return network_wire_this_platform() == NETWORK_PLATFORM_VITA_PC ? NETWORK_AUDIENCE_VITA_PC :
#ifdef HALO_DEDICATED_SERVER
		NETWORK_AUDIENCE_VITA_PC;
#else
		NETWORK_AUDIENCE_VITAS;
#endif
}

int network_wire_audience_first(void)
{
	return network_wire_this_platform() == NETWORK_PLATFORM_VITA_PC ? NETWORK_AUDIENCE_VITA_PC : NETWORK_AUDIENCE_VITAS;
}

/* (the settings the Vita's settings panel and the menus change live -
setenv - are the environment's first; then config.toml's) */
int network_wire_host_audience(void)
{
	int audience = network_wire_audience_parse(getenv("HALO_NET_WHO_CAN_JOIN"));

	/* (the retired setting: env.txt's, the tests', an older config.toml's
	true) */
	if (audience < 0)
		audience = network_wire_audience_parse(getenv("HALO_NET_HOST_CROSSPLAY"));
	if (audience < 0)
		audience = network_wire_audience_parse(config_string("network.who_can_join"));
	if (audience < 0 && config_boolean("network.host_crossplay"))
		audience = NETWORK_AUDIENCE_EVERYONE;
	if (audience < 0)
		audience = network_wire_audience_default();
	if (network_wire_host_pc_stand_in())
		audience = NETWORK_AUDIENCE_EVERYONE;
	/* (a PC host cannot host for Vitas only; with no crossplay wire, as in
	ad hoc play, there is no PC player of OpenCE's to take) */
	if (audience < network_wire_audience_first())
		audience = network_wire_audience_first();
	if (audience == NETWORK_AUDIENCE_EVERYONE && !network_wire_crossplay_available())
		audience = NETWORK_AUDIENCE_VITA_PC;
	return audience;
}

int network_wire_host_crossplay(void)
{
	return network_wire_crossplay_available() && network_wire_host_audience() == NETWORK_AUDIENCE_EVERYONE;
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
