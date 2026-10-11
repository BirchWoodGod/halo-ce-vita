/* tick_detail.c

(HALO_TICK_PROFILE=3) the tick's time inside an object's type update, every
300 ticks as "tick-detail (ms/tick)": each part's update (unit, biped,
item, weapon...: object_types.c) and the biped update's stages (bipeds.c).
For finding what the Vita's bipeds spend 0.4-0.6 ms a tick each on; the
clock is read twice per timed step (a system call each on the Vita), so the
numbers include some of their own cost. */

#include <stdio.h>
#include <stdlib.h>

#include "fine_profile.h"

unsigned long long vita_host_time_us(void) __attribute__((weak));
void platform_log(const char *format, ...);

#define DETAIL_SLOTS 32

static int enabled = -1;
static const char *names[DETAIL_SLOTS];
static unsigned long long totals[DETAIL_SLOTS];
static unsigned long counts[DETAIL_SLOTS];
static unsigned long ticks;

int halo_tick_detail_enabled(void)
{
	if (enabled < 0)
	{
		const char *setting = getenv("HALO_TICK_PROFILE");

		enabled = setting && atoi(setting) >= 3 && vita_host_time_us;
	}
	return enabled;
}

unsigned long long halo_tick_detail_begin(void)
{
	return halo_tick_detail_enabled() && halo_fine_tick_on ? halo_fine_tick_now() : 0;
}

/* a step's name is a string constant; steps are found by it */
void halo_tick_detail_end(const char *name, unsigned long long started)
{
	int slot;

	if (!halo_tick_detail_enabled() || !halo_fine_tick_on || !name)
		return;
	for (slot = 0; slot < DETAIL_SLOTS; slot++)
	{
		if (names[slot] == name || !names[slot])
		{
			names[slot] = name;
			totals[slot] += halo_fine_tick_now() - started;
			counts[slot]++;
			return;
		}
	}
}

/* once a tick (game.c) */
void halo_tick_detail_report(void)
{
	char line[1280];
	int n = 0, slot;
	/* (per timed tick: fine_profile.h) */
	static struct halo_fine_mark mark;
	unsigned long timed;

	if (!halo_tick_detail_enabled() || ++ticks % 300)
		return;
	timed = halo_fine_tick_since(&mark);
	if (!timed)
		timed = 1;
	for (slot = 0; slot < DETAIL_SLOTS && names[slot]; slot++)
	{
		n += snprintf(line + n, sizeof(line) - n, " %s %.2f(%.1f)", names[slot], totals[slot] / 1000.0 / timed,
			(double)counts[slot] / timed);
		totals[slot] = 0;
		counts[slot] = 0;
	}
	platform_log("tick-detail (ms/tick, calls/tick):%s | %s", line, mark.note);
}

/* (HALO_NET_PROFILE=1) the same for the networked game's frame work
(network_game_globals.c, network_server_manager.c, network_client_manager.c):
a "net-detail (ms/frame, calls/frame)" line every 300 frames (main.c) */
static int net_enabled = -1;
static const char *net_names[DETAIL_SLOTS];
static unsigned long long net_totals[DETAIL_SLOTS];
static unsigned long net_counts[DETAIL_SLOTS];
static unsigned long net_frames;

static int net_detail_enabled(void)
{
	if (net_enabled < 0)
	{
		const char *setting = getenv("HALO_NET_PROFILE");

		net_enabled = setting && atoi(setting) != 0 && vita_host_time_us;
	}
	return net_enabled;
}

unsigned long long halo_net_detail_begin(void)
{
	return net_detail_enabled() ? vita_host_time_us() : 0;
}

void halo_net_detail_end(const char *name, unsigned long long started)
{
	int slot;

	if (!net_detail_enabled() || !name)
		return;
	for (slot = 0; slot < DETAIL_SLOTS; slot++)
	{
		if (net_names[slot] == name || !net_names[slot])
		{
			net_names[slot] = name;
			net_totals[slot] += vita_host_time_us() - started;
			net_counts[slot]++;
			return;
		}
	}
}

/* once a frame (main.c) */
void halo_net_detail_report(void)
{
	char line[1024];
	int n = 0, slot;

	if (!net_detail_enabled() || ++net_frames % 300)
		return;
	for (slot = 0; slot < DETAIL_SLOTS && net_names[slot]; slot++)
	{
		n += snprintf(line + n, sizeof(line) - n, " %s %.2f(%lu)", net_names[slot], net_totals[slot] / 1000.0 / 300.0,
			net_counts[slot] / 300);
		net_totals[slot] = 0;
		net_counts[slot] = 0;
	}
	platform_log("net-detail (ms/frame, calls/frame):%s", line);
}

/* (HALO_TICK_PROFILE=1 or more) the distributed netcode's host work each
tick (network_distributed.c, network_coop.c, network_actors.c): a
"net-tick (ms/tick, calls/tick)" line every 300 ticks, apart from the
tick-detail line (a Vita's log line holds 1024 characters). For a co-op
host's cost by the number of machines it sends to: "datagram" is each
datagram's send (sealed for internet play), nested in the step that filled
it. */
static int net_tick_enabled = -1;
static const char *net_tick_names[DETAIL_SLOTS];
static unsigned long long net_tick_totals[DETAIL_SLOTS];
static unsigned long net_tick_counts[DETAIL_SLOTS];
static unsigned long net_tick_ticks;

static int net_tick_detail_enabled(void)
{
	if (net_tick_enabled < 0)
	{
		const char *setting = getenv("HALO_TICK_PROFILE");

		net_tick_enabled = setting && atoi(setting) != 0 && vita_host_time_us;
	}
	return net_tick_enabled;
}

unsigned long long halo_net_tick_begin(void)
{
	return net_tick_detail_enabled() ? vita_host_time_us() : 0;
}

void halo_net_tick_end(const char *name, unsigned long long started)
{
	int slot;

	if (!net_tick_detail_enabled() || !name)
		return;
	for (slot = 0; slot < DETAIL_SLOTS; slot++)
	{
		if (net_tick_names[slot] == name || !net_tick_names[slot])
		{
			net_tick_names[slot] = name;
			net_tick_totals[slot] += vita_host_time_us() - started;
			net_tick_counts[slot]++;
			return;
		}
	}
}

/* once a networked tick (network_distributed.c) */
void halo_net_tick_report(void)
{
	char line[1024];
	int n = 0, slot;

	if (!net_tick_detail_enabled() || ++net_tick_ticks % 300)
		return;
	for (slot = 0; slot < DETAIL_SLOTS && net_tick_names[slot] && n < (int)sizeof(line); slot++)
	{
		n += snprintf(line + n, sizeof(line) - n, " %s %.3f(%.1f)", net_tick_names[slot],
			net_tick_totals[slot] / 1000.0 / 300.0, net_tick_counts[slot] / 300.0);
		net_tick_totals[slot] = 0;
		net_tick_counts[slot] = 0;
	}
	platform_log("net-tick (ms/tick, calls/tick):%s", line);
}
