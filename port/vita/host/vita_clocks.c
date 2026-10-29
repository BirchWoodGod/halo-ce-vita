/*
VITA_CLOCKS.C

The clocks by what the game is doing (Sierra-III's battery handoff, Oct 21
2026): play at the top clocks main has always asked for (444/222/222/166,
from Bruno Santana's build: ARM, bus, GPU, crossbar), lower ones where
nothing waits on the CPU or the GPU. The device decides when
(d3d8_gxm.c clocks_governor: the menus, pause and loading screens after a
second of them, play at once); this sets them.

  play     main's floors, raised to and never lowered from more (a PSVshell
           or other overclocking plugin's 500 MHz stays)
  loading  the GPU and crossbar at idle's, the ARM and bus at play's (the
           load is the CPU's work: a slower CPU would only make it longer)
  idle     HALO_CLOCK_IDLE=arm/bus/gpu/xbar (default 333/222/111/111, the
           system's own for a game)

No clock is ever set below the one found at start: an overclocking
plugin's choice stands, and the idle clocks are then at most what the
system gave the game.

The battery's charge goes to halo.log once a minute (vita_host_battery_log,
from the heartbeat thread), so a tester's log shows the drain:
HALO_BATTERY_LOG=1 on, 0 off; unset (the Dev tab's Battery log: Auto), on
in a pre-release build (an insider's, a beta's), off in a release.
*/

#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vita_host.h"
#include "vita_version.h"

/* the clocks of each level: ARM, bus, GPU, crossbar */
static int found[4], play[4], loading[4], idle[4];
static int current = -1, started;

static int at_least(int value, int floor)
{
	return value > floor ? value : floor;
}

void vita_host_clocks_start(void)
{
	found[0] = scePowerGetArmClockFrequency();
	found[1] = scePowerGetBusClockFrequency();
	found[2] = scePowerGetGpuClockFrequency();
	found[3] = scePowerGetGpuXbarClockFrequency();
	/* the clocks the game needs as floors: raised to them, never lowered
	from more (a 500 MHz profile of PSVshell or another overclocking plugin
	stays; 444/222/222/166 are the system's highest otherwise) - from
	Bruno Santana's modified build */
	if (found[0] < 444)
		scePowerSetArmClockFrequency(444);
	if (found[1] < 222)
		scePowerSetBusClockFrequency(222);
	if (found[2] < 222)
		scePowerSetGpuClockFrequency(222);
	if (found[3] < 166)
		scePowerSetGpuXbarClockFrequency(166);
	play[0] = scePowerGetArmClockFrequency();
	play[1] = scePowerGetBusClockFrequency();
	play[2] = scePowerGetGpuClockFrequency();
	play[3] = scePowerGetGpuXbarClockFrequency();
	current = VITA_CLOCKS_PLAY;
	started = 1;
}

/* (at the first change, once env.txt is read) the lower levels' clocks */
static void lower_clocks_configure(void)
{
	static int configured;
	const char *setting = getenv("HALO_CLOCK_IDLE");
	int wanted[4] = { 333, 222, 111, 111 }, index;
	char message[160];

	if (configured)
		return;
	configured = 1;
	if (setting)
		sscanf(setting, "%d/%d/%d/%d", &wanted[0], &wanted[1], &wanted[2], &wanted[3]);
	for (index = 0; index < 4; index++)
	{
		/* (never below what the system gave, never above play's) */
		idle[index] = at_least(wanted[index], found[index]);
		if (idle[index] > play[index])
			idle[index] = play[index];
		loading[index] = index < 2 ? play[index] : idle[index];
	}
	snprintf(message, sizeof(message), "clocks: found %d/%d/%d/%d at start; play %d/%d/%d/%d, idle %d/%d/%d/%d",
		found[0], found[1], found[2], found[3], play[0], play[1], play[2], play[3], idle[0], idle[1], idle[2], idle[3]);
	vita_host_log(message);
}

void vita_host_clocks_set(int level)
{
	static const char *const names[3] = { "play", "loading", "idle" };
	const int *clocks;
	unsigned long long before;
	char message[128];

	if (!started || level == current || level < VITA_CLOCKS_PLAY || level > VITA_CLOCKS_IDLE)
		return;
	lower_clocks_configure();
	clocks = level == VITA_CLOCKS_PLAY ? play : level == VITA_CLOCKS_LOADING ? loading : idle;
	before = sceKernelGetProcessTimeWide();
	/* (up: the bus before what runs on it; down: after) */
	if (level < current)
	{
		scePowerSetBusClockFrequency(clocks[1]);
		scePowerSetGpuXbarClockFrequency(clocks[3]);
		scePowerSetGpuClockFrequency(clocks[2]);
		scePowerSetArmClockFrequency(clocks[0]);
	}
	else
	{
		scePowerSetArmClockFrequency(clocks[0]);
		scePowerSetGpuClockFrequency(clocks[2]);
		scePowerSetGpuXbarClockFrequency(clocks[3]);
		scePowerSetBusClockFrequency(clocks[1]);
	}
	current = level;
	snprintf(message, sizeof(message), "clocks: %s, %d/%d/%d/%d now (set in %llu us)", names[level],
		scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency(),
		scePowerGetGpuXbarClockFrequency(), (unsigned long long)(sceKernelGetProcessTimeWide() - before));
	vita_host_log(message);
}

/* (the heartbeat thread, every 2 s) a line a minute: the charge in percent
and mAh, what went since the last line, the voltage, the charger, the
clocks' level */
void vita_host_battery_log(void)
{
	static const char *const names[3] = { "play", "loading", "idle" };
	static unsigned long long last;
	static int last_capacity = -1;
	unsigned long long now = sceKernelGetProcessTimeWide();
	const char *setting = getenv("HALO_BATTERY_LOG");
	int on = setting && setting[0] ? atoi(setting) != 0 : strchr(HALO_VITA_VERSION, '-') != NULL;
	int percent, capacity, full, volts, charging, online;
	char since[48], message[224];

	if (!on || (last && now - last < 60000000ULL))
		return;
	percent = scePowerGetBatteryLifePercent();
	capacity = scePowerGetBatteryRemainCapacity();
	full = scePowerGetBatteryFullCapacity();
	volts = scePowerGetBatteryVolt();
	charging = scePowerIsBatteryCharging();
	online = scePowerIsPowerOnline();
	if (last && last_capacity >= 0 && capacity >= 0)
		snprintf(since, sizeof(since), ", %+d mAh in %.1f min", capacity - last_capacity, (now - last) / 60000000.0);
	else
		since[0] = 0;
	snprintf(message, sizeof(message), "battery: %d%%, %d of %d mAh%s, %d.%03d V, %s, clocks %s %d/%d/%d/%d, up %llu min",
		percent, capacity, full, since, volts / 1000, volts % 1000,
		charging ? "charging" : online ? "on the charger" : "on battery", current >= 0 ? names[current] : "unset",
		scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency(),
		scePowerGetGpuXbarClockFrequency(), now / 60000000ULL);
	vita_host_log(message);
	last = now;
	last_capacity = capacity;
}
