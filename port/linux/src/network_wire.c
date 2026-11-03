/*
NETWORK_WIRE.C

Which wire a game speaks (network_wire.h, docs/crossplay.md): chosen per
game, as it is joined or hosted.
*/

#include "port_config.h"
#include "halo_port_limits.h"
#include "network_wire.h"
#include "netversion_table.h"
#include "lang.h"

#include <pthread.h>
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
10.9). And 38 (Oct 24 2026: OpenCE's and ChupathingyCE's servers, their
rules network_wire.h's network_wire_v33_rules ...), not 33 to 37, which no
release announces. And 39 (Oct 27 2026: OpenCE's since its dedicated
server, 815d3a0c; 38's rules, docs/opence-net39-spec.md). And 40 and 41
(Oct 2026: a larger join request, a co-op switch; 39's rules otherwise,
docs/opence-net41-spec.md). In order, lowest first */
static const unsigned short crossplay_accepted_versions[] =
	{ HALO_PORT_CROSSPLAY_OLDER_NETWORK_VERSION, HALO_PORT_CROSSPLAY_NETWORK_VERSION,
		HALO_PORT_CROSSPLAY_38_NETWORK_VERSION, HALO_PORT_CROSSPLAY_39_NETWORK_VERSION,
		HALO_PORT_CROSSPLAY_40_NETWORK_VERSION, HALO_PORT_CROSSPLAY_NEWEST_NETWORK_VERSION };

/* the game's wire and whether its host is a Vita (the game's threads and
the p2p thread read them; one writer at a time: the game's as it joins or
hosts, the p2p thread's as an invite's host answers) */
static volatile int wire;
/* (crossplay_guard.h's CROSSPLAY_PLATFORM_*: 0 a Vita) */
static volatile int host_platform;
/* the game's crossplay version (one of crossplay_accepted_versions) while
wire is set: whose rules it plays; and the version its host announces (the
same, or one the crossplay version table plays by a built-in version's
rules) */
static volatile unsigned int crossplay_version;
static volatile unsigned int crossplay_announced;

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

/* whether a version is one built in (each with rules of its own) */
static int crossplay_version_built_in(unsigned int version)
{
	size_t index;

	for (index = 0; index < sizeof(crossplay_accepted_versions) / sizeof(crossplay_accepted_versions[0]); index++)
	{
		if (version == crossplay_accepted_versions[index])
			return 1;
	}
	return 0;
}

/* ---------- the signed crossplay version table (netversion_table.h)

The rows of the newest table that checked (netversion_fetch.c), none until
one does: the built-in list alone. A version is joined on the crossplay wire
when it is built in and not refused by a row, or a join row names it with
the rules of a built-in version; it plays the rules of that version (its
own, when built in: a row never remaps one). Read by the game's threads and
the p2p thread, written by the fetch's: under table_lock, never long */
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;
static struct netversion_table table;

void network_wire_table_set(const struct netversion_table *taken)
{
	pthread_mutex_lock(&table_lock);
	if (taken)
		memcpy(&table, taken, sizeof(table));
	else
		memset(&table, 0, sizeof(table));
	if (table.count < 0 || table.count > NETVERSION_TABLE_MAXIMUM_ROWS)
		table.count = 0;
	pthread_mutex_unlock(&table_lock);
}

unsigned long network_wire_table_version(void)
{
	unsigned long version;

	pthread_mutex_lock(&table_lock);
	version = table.version;
	pthread_mutex_unlock(&table_lock);
	return version;
}

int network_wire_rules_built_in(unsigned int rules)
{
	return rules != HALO_PORT_NETWORK_VERSION && crossplay_version_built_in(rules);
}

/* (table_lock held) the table's row of a version, or NULL (netversion_table_find's,
here so that this file needs no other) */
static const struct netversion_row *table_row_locked(unsigned int version)
{
	int index;

	for (index = 0; index < table.count && index < NETVERSION_TABLE_MAXIMUM_ROWS; index++)
	{
		if (table.rows[index].version == version)
			return &table.rows[index];
	}
	return NULL;
}

/* (table_lock held) the crossplay rules a version plays: a built-in
version, else 0 (not joined); why not in reason (if not NULL), when the
table says */
static unsigned int crossplay_rules_locked(unsigned int version, char *reason, int size)
{
	const struct netversion_row *row;

	if (reason && size > 0)
		reason[0] = 0;
	if (!version || version == HALO_PORT_NETWORK_VERSION || version > 0xFFFF)
		return 0;
	row = table_row_locked(version);
	if (row && row->kind == NETVERSION_ROW_REFUSE)
	{
		if (reason && size > 0)
			snprintf(reason, (size_t)size, "%s", row->reason);
		return 0;
	}
	if (crossplay_version_built_in(version))
		return version;
	if (row && row->kind == NETVERSION_ROW_JOIN)
	{
		if (network_wire_rules_built_in(row->rules))
			return row->rules;
		/* (a newer build's rules) */
		if (reason && size > 0)
			snprintf(reason, (size_t)size, "%s", T("needs a newer Halo CE for PS Vita - update to join"));
	}
	return 0;
}

static unsigned int crossplay_rules(unsigned int version, char *reason, int size)
{
	unsigned int rules;

	pthread_mutex_lock(&table_lock);
	rules = crossplay_rules_locked(version, reason, size);
	pthread_mutex_unlock(&table_lock);
	return rules;
}

static int crossplay_version_listed(unsigned int version)
{
	return crossplay_rules(version, NULL, 0) != 0;
}

unsigned int network_wire_rules_version(unsigned int version)
{
	unsigned int rules = crossplay_rules(version, NULL, 0);

	return rules ? rules : version;
}

int network_wire_crossplay_refusal(unsigned int version, char *reason, int size)
{
	char why[NETVERSION_TABLE_REASON_SIZE + 64];

	if (reason && size > 0)
		reason[0] = 0;
	if (crossplay_rules(version, why, (int)sizeof(why)) || !why[0])
		return 0;
	if (reason && size > 0)
		snprintf(reason, (size_t)size, "%s", why);
	return 1;
}

/* the crossplay versions joined, lowest first, at most maximum: their count */
static int crossplay_joined_versions(unsigned short *versions, int maximum)
{
	size_t index, built_in = sizeof(crossplay_accepted_versions) / sizeof(crossplay_accepted_versions[0]);
	int count = 0, row;

	pthread_mutex_lock(&table_lock);
	/* (both lists are in order: merged) */
	index = 0;
	row = 0;
	while ((index < built_in || row < table.count) && count < maximum)
	{
		unsigned int version;

		if (row >= table.count ||
			(index < built_in && crossplay_accepted_versions[index] <= table.rows[row].version))
		{
			version = crossplay_accepted_versions[index++];
			if (row < table.count && table.rows[row].version == version)
				row++;
		}
		else
			version = table.rows[row++].version;
		if (crossplay_rules_locked(version, NULL, 0))
			versions[count++] = (unsigned short)version;
	}
	pthread_mutex_unlock(&table_lock);
	return count;
}

/* the wire and its version: rules, a built-in version's, or 0 the legacy wire */
static void wire_set(unsigned int version, unsigned int rules, int platform, const char *why)
{
	int crossplay = rules != 0;

	platform = crossplay ? platform : 0;
	if (crossplay != wire || platform != host_platform || rules != crossplay_version ||
		(crossplay && version != crossplay_announced))
	{
		if (crossplay && rules != version)
		{
			platform_log("network: the crossplay wire (%s%s), network version %u, played by %u's rules "
				"(the crossplay version table %lu)", why ? why : "a game", platform ? ", a PC's game" : ", a Vita's game",
				version, rules, network_wire_table_version());
		}
		else if (crossplay)
		{
			platform_log("network: the crossplay wire (%s%s), network version %u's rules", why ? why : "a game",
				platform ? ", a PC's game" : ", a Vita's game", version);
		}
		else
			platform_log("network: the legacy Vita wire (%s)", why ? why : "a game");
	}
	/* (the version before the wire: a reader seeing the wire set sees its
	rules) */
	crossplay_version = rules;
	crossplay_announced = crossplay ? version : 0;
	wire = crossplay;
	host_platform = platform;
}

void network_wire_choose_version(unsigned int version, int platform, const char *why)
{
	wire_set(version, version != HALO_PORT_NETWORK_VERSION && network_wire_crossplay_available() ?
		crossplay_rules(version, NULL, 0) : 0, platform, why);
}

void network_wire_choose(int crossplay, int platform, const char *why)
{
	/* (a game of this machine's: the built-in list alone, so that no table,
	whatever it refuses, changes what a host plays: a table is about whom a
	client joins) */
	unsigned int version = crossplay ? network_wire_host_version() : HALO_PORT_NETWORK_VERSION;

	wire_set(version, crossplay && network_wire_rules_built_in(version) && network_wire_crossplay_available() ? version : 0,
		platform, why);
}

void network_wire_reset(void)
{
	wire = 0;
	host_platform = 0;
	crossplay_version = 0;
	crossplay_announced = 0;
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

		if (!*end && version <= 0xFFFF && network_wire_rules_built_in((unsigned int)version))
			return (unsigned int)version;
	}
#endif
	return HALO_PORT_CROSSPLAY_NETWORK_VERSION;
}

unsigned int network_wire_version_heard(unsigned int version)
{
#ifndef HALO_RELEASE
	/* (automated tests only: HALO_TEST_VERSION_HEARD=from:to) */
	const char *value = getenv("HALO_TEST_VERSION_HEARD");

	if (value && value[0])
	{
		char *end;
		unsigned long from = strtoul(value, &end, 10), to;

		if (*end == ':')
		{
			to = strtoul(end + 1, &end, 10);
			if (!*end && from == version && to > 0 && to <= 0xFFFF)
				return (unsigned int)to;
		}
	}
#endif
	return version;
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

int network_wire_v33_rules(void)
{
	return game_crossplay_version() >= 33;
}

int network_wire_v34_rules(void)
{
	return game_crossplay_version() >= 34;
}

int network_wire_v35_rules(void)
{
	return game_crossplay_version() >= 35;
}

int network_wire_v36_rules(void)
{
	return game_crossplay_version() >= 36;
}

int network_wire_v39_rules(void)
{
	return game_crossplay_version() >= 39;
}

int network_wire_v40_rules(void)
{
	return game_crossplay_version() >= 40;
}

int network_wire_v41_rules(void)
{
	return game_crossplay_version() >= 41;
}

int network_wire_join_request_padding(void)
{
	/* (read once each: the p2p thread may be choosing another) */
	unsigned int announced = crossplay_announced;

	return network_wire_crossplay() && (network_wire_v40_rules() || announced >= 40) ?
		NETWORK_WIRE_V40_JOIN_REQUEST_PADDING : 0;
}

int network_wire_rejection_is_name_refused(int code)
{
	/* (OpenCE 39's _rejection_code 7, the same number as this project's
	Vitas only: a Vita's host sends that on the legacy wire alone, so a PC
	host's 7 on the crossplay wire is OpenCE's. Not by the version: a PC
	host of 38 sends no 7 at all, and its 7 would mean nothing else) */
	return code == 7 && network_wire_crossplay() && !network_wire_host_vita();
}

const char *network_wire_rejection_text(int code)
{
	/* (the same test as code 7's: a Vita's host never sends these, and
	the legacy wire's numbers are this project's) */
	if (!network_wire_crossplay() || network_wire_host_vita())
		return NULL;
	switch (code)
	{
	case NETWORK_WIRE_REJECTION_VPN_LINK_REQUIRED:
	case NETWORK_WIRE_REJECTION_VPN_BLOCKED:
		return T("This server does not allow VPN or datacenter connections. Turn off any VPN and try again.");
	case NETWORK_WIRE_REJECTION_VPN_LINK_INVALID:
	case NETWORK_WIRE_REJECTION_VPN_LINK_TOO_NEW:
	case NETWORK_WIRE_REJECTION_VPN_LINK_IN_USE:
		return T("This server could not check a Discord link. This game does not use Discord links.");
	case NETWORK_WIRE_REJECTION_ADDRESS_FULL:
		return T("This server has too many players from your address.");
	}
	return NULL;
}

void network_wire_crossplay_versions_text(char *text, int size)
{
	unsigned short versions[NETVERSION_TABLE_MAXIMUM_ROWS + 8];
	int index, count = crossplay_joined_versions(versions, (int)(sizeof(versions) / sizeof(versions[0])));
	int length = 0;

	if (!text || size <= 0)
		return;
	text[0] = 0;
	if (!count)
	{
		snprintf(text, (size_t)size, "%s", T("none"));
		return;
	}
	for (index = 0; index < count && length >= 0 && length < size; index++)
	{
		length += snprintf(text + length, (size_t)(size - length), "%s%u",
			!index ? "" : index + 1 == count ? " and " : ", ", (unsigned int)versions[index]);
	}
}

unsigned int network_wire_crossplay_version_near(unsigned int version)
{
	unsigned short versions[NETVERSION_TABLE_MAXIMUM_ROWS + 8];
	int index, count = crossplay_joined_versions(versions, (int)(sizeof(versions) / sizeof(versions[0])));

	if (!count)
		return HALO_PORT_CROSSPLAY_NETWORK_VERSION;
	for (index = 0; index < count; index++)
	{
		if (versions[index] > version)
			return versions[index];
	}
	return versions[count - 1];
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

unsigned short network_wire_join_platform_mark(void)
{
	return (unsigned short)(NETWORK_WIRE_JOIN_PLATFORM_MARK | (network_wire_this_platform() + 1));
}

int network_wire_join_platform_of_mark(unsigned short mark)
{
	int low = mark & 0xFF;

	if ((mark & 0xFF00) != NETWORK_WIRE_JOIN_PLATFORM_MARK || low < 1 || low > NETWORK_PLATFORM_VITA_PC + 1)
		return NETWORK_WIRE_JOIN_PLATFORM_UNKNOWN;
	return low - 1;
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

int network_wire_host_refuses(int platform)
{
	return !network_wire_crossplay() && platform == NETWORK_PLATFORM_VITA_PC &&
		network_wire_host_audience() == NETWORK_AUDIENCE_VITAS;
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
