/*
NETWORK_CONNECTION.C

The connection the game plays over - Same Wi-Fi (system link on the Wi-Fi
network), Ad hoc (the Vitas near, with no router: p2p_adhoc.c) or Online
(internet play: p2p.c) - is one setting, the settings panel's Connection
(HALO_VITA_NETWORK: wifi, adhoc or online). The Halo 3 lobbies' NETWORK
sets the same setting (menu_functions.c), kept as the panel keeps it
(vita_settings_set: settings.txt on the Vita), so the two never disagree.

It was set up once, as the game started. Now it is switched live, from the
main loop (network_connection_update), when the setting changes and the
game is at its menus with no network game (no client and no server: the
game's sockets are all closed then, so internet play learns of every one it
will have, as when it starts with the game):
- Online: network.online on, internet play started (xnet.c's start, the
  same p2p_initialize) and the brokers reached (p2p_lobby_reach_brokers):
  Connecting... until a broker that carries the server browser is ready,
  then Online. None reached (p2p_lobby_brokers_status: every broker failed
  every way, after its time to reach them), it says why, and the setting
  goes back to Same Wi-Fi.
- Same Wi-Fi: internet play stopped (p2p_shutdown: its thread, sockets,
  peers and broker connections gone, a public listing taken down).
- Ad hoc: network.adhoc on (this machine's address ad hoc play's stand-in,
  vita_net.c), the bridge started (p2p_initialize in ad hoc play) and, on
  the Vita, the system's dialog joining the room's group
  (halo_adhoc_request); leaving it stops the bridge and leaves the group,
  the Vita's Wi-Fi back.
The checks are the same as at a start: the same p2p_initialize and
settings (only network.online and network.adhoc change, for this run:
config_set_override), the wire (network_wire.h) read again. The command
line's invite is taken at the first start only (p2p.c). What it runs and
how it goes is published for the settings panel (halo_network_connection,
halo_network_phase: system_link_shortcut.h), and said for the lobbies'
status line (network_connection_status).

A PC build has no Ad hoc (config_adhoc_offered: the PS Vitas' own): a
setting or a choice of it is Same Wi-Fi there.

(Debug, test builds) HALO_NETWORK_TEST_SWITCH="online,wifi,adhoc,..."
switches through the list, one each HALO_NETWORK_TEST_SWITCH_SECONDS (8)
at the main menu, as the lobby would, and logs this process's own threads
and open files after each (run_netns_online_test.sh netswitch: none left
behind).
*/

#include "cseries.h"
#include "networking/network_game_globals.h"

#include "network_connection.h"
#include "network_wire.h"
#include "../src/lang.h"
#include "../src/p2p.h"
#include "../src/port_config.h"
#include "../src/system_link_shortcut.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);
int setenv(const char *name, const char *value, int overwrite);
int p2p_lobby_brokers_status(char *text, int size);
void p2p_lobby_reach_brokers(void);
void xnet_internet_play_start(void);
boolean transport_network_available(void);
extern int (*halo_test_setting_hook)(const char *variable, const char *value);
extern volatile unsigned long halo_settings_generation;

volatile int halo_network_connection = -1;
volatile int halo_network_phase;

/* how long a failure is said in the status line */
#define NETWORK_FAILURE_SAID_MILLISECONDS 12000

static char const *const connection_values[NUMBER_OF_NETWORK_CONNECTIONS] = { "wifi", "adhoc", "online" };

static struct
{
	boolean initialized;
	/* what runs (NETWORK_CONNECTION_*) */
	short running;
	/* Online: connecting (no broker reached yet), or ready */
	boolean connecting;
	/* the last failure said, and until when */
	char why[128];
	unsigned long why_until;
	char said[160];
} connection;

static short connection_of(char const *value)
{
	short index;

	for (index = 0; value && index < NUMBER_OF_NETWORK_CONNECTIONS; index++)
	{
		if (!strcmp(value, connection_values[index]))
		{
			/* (a PC: no ad hoc play - config_adhoc_offered -, Same Wi-Fi,
			System Link, in its place; said once) */
			if (index == NETWORK_CONNECTION_ADHOC && !config_adhoc_offered())
			{
				static boolean said;

				if (!said)
				{
					said = TRUE;
					platform_log("network: Ad hoc is the PS Vitas' own: Same Wi-Fi (System Link) on this PC");
				}
				return NETWORK_CONNECTION_WIFI;
			}
			return index;
		}
	}
	return NETWORK_CONNECTION_WIFI;
}

static char const *connection_name(short which)
{
	return which == NETWORK_CONNECTION_ONLINE ? "Online" : which == NETWORK_CONNECTION_ADHOC ? "Ad hoc" : "Same Wi-Fi";
}

/* whether the last failure is still said */
static boolean failure_said(void)
{
	return connection.why[0] && (long)(connection.why_until - system_milliseconds()) > 0;
}

static void connection_publish(void)
{
	int phase = HALO_NETWORK_PHASE_READY;

	if (connection.running == NETWORK_CONNECTION_ONLINE && connection.connecting)
		phase = HALO_NETWORK_PHASE_CONNECTING;
	if (network_connection_wanted() != connection.running)
		phase = HALO_NETWORK_PHASE_WAITING;
	if (failure_said())
		phase = HALO_NETWORK_PHASE_FAILED;
	__atomic_store_n(&halo_network_phase, phase, __ATOMIC_RELEASE);
	__atomic_store_n(&halo_network_connection, connection.running, __ATOMIC_RELEASE);
}

/* (the first frame) what the game started with: the setting, as the Vita's
host applied it (vita_settings.c apply_network), else (elsewhere)
network.online and network.adhoc, which the setting then says */
static void connection_initialize(void)
{
	connection.initialized = TRUE;
	connection.running = config_boolean("network.adhoc") ? NETWORK_CONNECTION_ADHOC :
		config_boolean("network.online") ? NETWORK_CONNECTION_ONLINE : NETWORK_CONNECTION_WIFI;
	if (!getenv("HALO_VITA_NETWORK") || connection_of(getenv("HALO_VITA_NETWORK")) != connection.running)
		setenv("HALO_VITA_NETWORK", connection_values[connection.running], 1);
	/* (Online from the start: its brokers reached, for the status line) */
	if (connection.running == NETWORK_CONNECTION_ONLINE && p2p_running())
	{
		connection.connecting = TRUE;
		p2p_lobby_reach_brokers();
	}
	platform_log("network: %s (the connection the game started with)", connection_name(connection.running));
	connection_publish();
}

/* whether the connection may be switched now: at the menus, with no
network game (no client, no server: none of the game's sockets open) */
static boolean connection_safe(boolean main_menu_loaded)
{
	return main_menu_loaded && !global_network_game_client_get() && !global_network_game_server_get();
}

static void connection_switch(short to)
{
	short from = connection.running;

	platform_log("network: switching from %s to %s", connection_name(from), connection_name(to));
	if (from != NETWORK_CONNECTION_WIFI)
		p2p_shutdown();
	/* (out of the ad hoc group: the Vita's Wi-Fi back) */
	if (from == NETWORK_CONNECTION_ADHOC)
		__atomic_store_n(&halo_adhoc_request, HALO_ADHOC_REQUEST_LEAVE, __ATOMIC_RELEASE);
	/* (for this run, as the Vita's host sets them at a start: the
	environment its address reads, vita_net.c, and the settings) */
	setenv("HALO_NET_ONLINE", to == NETWORK_CONNECTION_ONLINE ? "true" : "false", 1);
	setenv("HALO_NET_ADHOC", to == NETWORK_CONNECTION_ADHOC ? "true" : "false", 1);
	config_set_override("network.online", to == NETWORK_CONNECTION_ONLINE ? "true" : "false");
	config_set_override("network.adhoc", to == NETWORK_CONNECTION_ADHOC ? "true" : "false");
	network_wire_reset();
	connection.running = to;
	connection.connecting = FALSE;
	if (to != NETWORK_CONNECTION_WIFI)
	{
		xnet_internet_play_start();
		if (!p2p_running())
		{
			snprintf(connection.why, sizeof(connection.why), "%s", T("Internet play did not start"));
			connection.why_until = system_milliseconds() + NETWORK_FAILURE_SAID_MILLISECONDS;
			platform_log("network: %s did not start; Same Wi-Fi", connection_name(to));
			network_connection_choose(NETWORK_CONNECTION_WIFI);
			connection_switch(NETWORK_CONNECTION_WIFI);
			return;
		}
	}
	if (to == NETWORK_CONNECTION_ONLINE)
	{
		connection.connecting = TRUE;
		p2p_lobby_reach_brokers();
	}
	/* (the room's group joined: the system's dialog, the Vita's host) */
	if (to == NETWORK_CONNECTION_ADHOC)
		__atomic_store_n(&halo_adhoc_request, HALO_ADHOC_REQUEST_JOIN, __ATOMIC_RELEASE);
	platform_log("network: %s", connection_name(to));
}

/* (Online, connecting) a broker reached: Online; none reachable: why, and
back to Same Wi-Fi */
static void connection_watch_online(void)
{
	char text[128];
	int state;

	if (connection.running != NETWORK_CONNECTION_ONLINE || !connection.connecting)
		return;
	state = p2p_lobby_brokers_status(text, (int)sizeof(text));
	if (state == P2P_LOBBY_BROWSE_EMPTY)
	{
		connection.connecting = FALSE;
		platform_log("network: Online (a broker reached)");
	}
	else if (state == P2P_LOBBY_BROWSE_UNREACHABLE)
	{
		connection.connecting = FALSE;
		snprintf(connection.why, sizeof(connection.why), "%s", text);
		connection.why_until = system_milliseconds() + NETWORK_FAILURE_SAID_MILLISECONDS;
		platform_log("network: Online failed (%s); back to Same Wi-Fi", text);
		network_connection_choose(NETWORK_CONNECTION_WIFI);
	}
}

/* (debug, test builds) HALO_NETWORK_TEST_SWITCH: the next connection of the
list each few seconds at the main menu, and this process's threads and open
files said after each switch */
static void connection_test_switch(boolean main_menu_loaded)
{
#ifndef HALO_RELEASE
	static int index;
	static boolean done;
	static unsigned long last;
	char const *list = getenv("HALO_NETWORK_TEST_SWITCH");
	char const *seconds = getenv("HALO_NETWORK_TEST_SWITCH_SECONDS");
	char word[16];
	int at, length;

	if (!list || !*list || !main_menu_loaded || done)
		return;
	if (!last)
		last = system_milliseconds();
	if (system_milliseconds() - last < (unsigned long)(seconds ? atoi(seconds) : 8) * 1000UL)
		return;
	last = system_milliseconds();
	for (at = 0; *list && at < index; list++)
	{
		if (*list == ',')
			at++;
	}
	length = (int)strcspn(list, ",");
	if (!length || length >= (int)sizeof(word))
	{
		platform_log("network test: the switches are done");
		done = TRUE;
		return;
	}
	memcpy(word, list, (size_t)length);
	word[length] = 0;
	index++;
	platform_log("network test: switch %d: %s", index, word);
	network_connection_choose(connection_of(word));
#else
	(void)main_menu_loaded;
#endif
}

/* (debug, test builds) after a switch: this process's threads and open
files (Linux's /proc), for the tests' leak check; none on Windows (no
/proc: posix_net.c's census is not in its build) */
static void connection_test_census(void)
{
#if !defined(HALO_RELEASE) && !defined(HALO_VITA) && !defined(_WIN32)
	extern int posix_process_census(char *names, int size, int *own, int *files);
	char names[512];
	int threads, own, files;

	if (!getenv("HALO_NETWORK_TEST_SWITCH"))
		return;
	threads = posix_process_census(names, (int)sizeof(names), &own, &files);
	platform_log("network test: %s: %d of its own threads (%d in all), %d open files", connection_name(connection.running),
		own, threads, files);
	platform_log("network test: threads: %s", names);
#endif
}

/* ---------- public code */

short network_connection_wanted(void)
{
	return connection_of(getenv("HALO_VITA_NETWORK"));
}

short network_connection_running(void)
{
	return connection.initialized ? connection.running : network_connection_wanted();
}

void network_connection_choose(short which)
{
	char const *value;

	/* (none on a PC: System Link) */
	if (which == NETWORK_CONNECTION_ADHOC && !config_adhoc_offered())
		which = NETWORK_CONNECTION_WIFI;
	value = connection_values[PIN(which, 0, NUMBER_OF_NETWORK_CONNECTIONS - 1)];

	if (network_connection_wanted() == which && getenv("HALO_VITA_NETWORK"))
		return;
	/* (as the panel sets it: kept in settings.txt on the Vita) */
	if (!halo_test_setting_hook || !halo_test_setting_hook("HALO_VITA_NETWORK", value))
	{
		setenv("HALO_VITA_NETWORK", value, 1);
		__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
	}
	platform_log("network: %s chosen", connection_name(which));
}

int network_connection_status(char *text, int size)
{
	short wanted = network_connection_wanted();
	int status;

	if (size <= 0)
		return NETWORK_STATUS_READY;
	if (failure_said())
	{
		snprintf(text, (size_t)size, "%s", connection.why);
		return NETWORK_STATUS_FAILED;
	}
	if (wanted != connection.running)
	{
		snprintf(text, (size_t)size, "%s", wanted == NETWORK_CONNECTION_ONLINE ? T("Connecting...") :
			T("Switching..."));
		return NETWORK_STATUS_SWITCHING;
	}
	status = NETWORK_STATUS_READY;
	if (connection.running == NETWORK_CONNECTION_ADHOC)
	{
		int group = __atomic_load_n(&halo_adhoc_group, __ATOMIC_ACQUIRE);

		snprintf(text, (size_t)size, "%s", group == HALO_ADHOC_GROUP_JOINING ? T("Ad hoc: joining...") :
			group == HALO_ADHOC_GROUP_IN ? T("Ad hoc") : T("Ad hoc: not in a group"));
	}
	else if (!transport_network_available())
	{
		snprintf(text, (size_t)size, "%s", T("No Wi-Fi"));
		status = NETWORK_STATUS_FAILED;
	}
	else if (connection.running == NETWORK_CONNECTION_ONLINE)
	{
		snprintf(text, (size_t)size, "%s", connection.connecting ? T("Connecting...") : T("Online"));
		if (connection.connecting)
			status = NETWORK_STATUS_SWITCHING;
	}
	else
		snprintf(text, (size_t)size, "%s", T("Same Wi-Fi"));
	return status;
}

void network_connection_update(boolean main_menu_loaded)
{
	short wanted;
	char text[160];

	if (!connection.initialized)
		connection_initialize();
	connection_test_switch(main_menu_loaded);
	wanted = network_connection_wanted();
	if (wanted != connection.running && connection_safe(main_menu_loaded))
	{
		/* (a new choice: the last failure is not said over it) */
		if (wanted != NETWORK_CONNECTION_WIFI)
			connection.why[0] = 0;
		connection_switch(wanted);
		connection_test_census();
	}
	connection_watch_online();
	connection_publish();
	/* (the status as it changes: the tests read it) */
	network_connection_status(text, (int)sizeof(text));
	if (strcmp(text, connection.said))
	{
		snprintf(connection.said, sizeof(connection.said), "%s", text);
		platform_log("network: status \"%s\"", text);
	}
}
