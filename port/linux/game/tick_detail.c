/* tick_detail.c

(HALO_TICK_PROFILE=3) the tick's time inside an object's type update, every
300 ticks as "tick-detail (ms/tick)": each part's update (unit, biped,
item, weapon...: object_types.c) and the biped update's stages (bipeds.c).
For finding what the Vita's bipeds spend 0.4-0.6 ms a tick each on; the
clock is read twice per timed step (a system call each on the Vita), so the
numbers include some of their own cost. */

#include <stdio.h>
#include <stdlib.h>

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
	return halo_tick_detail_enabled() ? vita_host_time_us() : 0;
}

/* a step's name is a string constant; steps are found by it */
void halo_tick_detail_end(const char *name, unsigned long long started)
{
	int slot;

	if (!halo_tick_detail_enabled() || !name)
		return;
	for (slot = 0; slot < DETAIL_SLOTS; slot++)
	{
		if (names[slot] == name || !names[slot])
		{
			names[slot] = name;
			totals[slot] += vita_host_time_us() - started;
			counts[slot]++;
			return;
		}
	}
}

/* once a tick (game.c) */
void halo_tick_detail_report(void)
{
	char line[1024];
	int n = 0, slot;

	if (!halo_tick_detail_enabled() || ++ticks % 300)
		return;
	for (slot = 0; slot < DETAIL_SLOTS && names[slot]; slot++)
	{
		n += snprintf(line + n, sizeof(line) - n, " %s %.2f(%lu)", names[slot], totals[slot] / 1000.0 / 300.0,
			counts[slot] / 300);
		totals[slot] = 0;
		counts[slot] = 0;
	}
	platform_log("tick-detail (ms/tick, calls/tick):%s", line);
}
