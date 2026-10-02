/* load_profile.c

HALO_LOAD_PROFILE=1: what a long frame was spent loading or saving. The
places that load (a new map, a game state save, revert or persistent save,
a structure BSP switch, cache file reads and writes, waits for the cache
file thread) add their time and bytes to this frame's totals, from
whichever thread they run on; at the frame's end, a frame longer than
HALO_LOAD_PROFILE_MS (default 100) is logged with every part that took
time:

  load-profile: frame 1037 took 6118.6 ms: new map 3510.2 ms | persistent
  header+checksum 812.0 ms 16384 KB | ...

Times on the cache file thread overlap the game thread's; the "wait" entry
is the game thread blocked on it. Off (the default), each place costs a
load of the switch. */

#include <stdlib.h>
#include <string.h>

#include "load_profile.h"

void platform_log(const char *format, ...);
unsigned long long vita_host_time_us(void);

static const char *const kind_names[_halo_load_count] =
{
	"game state save",
	"game state revert",
	"persistent header+checksum",
	"persistent read",
	"persistent write",
	"new map",
	"game load",
	"bsp switch",
	"bsp clear",
	"bsp read",
	"bsp relocate",
	"bsp vertex buffers",
	"cache read (game thread)",
	"cache read (other threads)",
	"cache write (map copy)",
	"wait for cache thread",
	"sound cache wait",
	"texture cache wait",
	"after-load procs",
};

int halo_load_profile_enabled = -1;
static unsigned long long threshold_us = 100000;
static volatile unsigned long long kind_us[_halo_load_count];
static volatile unsigned long long kind_bytes[_halo_load_count];
static volatile unsigned long kind_count[_halo_load_count];
static unsigned long long frame_started_us;
static unsigned long frame_index;

int halo_load_profile_check(void)
{
	if (halo_load_profile_enabled < 0)
	{
		const char *setting = getenv("HALO_LOAD_PROFILE");
		const char *threshold = getenv("HALO_LOAD_PROFILE_MS");

		halo_load_profile_enabled = setting && atoi(setting) != 0;
		if (threshold && atoi(threshold) > 0)
			threshold_us = (unsigned long long)atoi(threshold) * 1000ull;
	}
	return halo_load_profile_enabled;
}

static unsigned long long first_measured_us;

unsigned long long halo_load_profile_now(void)
{
	unsigned long long now;

	if (!halo_load_profile_check())
		return 0;
	now = vita_host_time_us();
	if (!first_measured_us)
		first_measured_us = now;
	return now;
}

void halo_load_profile_add(int kind, unsigned long long started_us, unsigned long long bytes)
{
	if (!halo_load_profile_check() || kind < 0 || kind >= _halo_load_count)
		return;
	__atomic_fetch_add(&kind_us[kind], vita_host_time_us() - started_us, __ATOMIC_RELAXED);
	__atomic_fetch_add(&kind_bytes[kind], bytes, __ATOMIC_RELAXED);
	__atomic_fetch_add(&kind_count[kind], 1, __ATOMIC_RELAXED);
}

void halo_load_profile_frame_end(void)
{
	unsigned long long now, elapsed;
	char line[1024];
	size_t length;
	int kind;

	if (!halo_load_profile_check())
		return;
	now = vita_host_time_us();
	/* (the first frame counts from the first thing measured: a map loaded
	before it) */
	if (!frame_started_us)
		frame_started_us = first_measured_us;
	elapsed = frame_started_us ? now - frame_started_us : 0;
	frame_started_us = now;
	frame_index++;
	if (elapsed >= threshold_us)
	{
		length = (size_t)snprintf(line, sizeof(line), "load-profile: frame %lu took %.1f ms:", frame_index, elapsed / 1000.0);
		for (kind = 0; kind < _halo_load_count && length < sizeof(line) - 96; kind++)
		{
			if (!kind_count[kind])
				continue;
			length += (size_t)snprintf(line + length, sizeof(line) - length, " %s %.1f ms",
				kind_names[kind], kind_us[kind] / 1000.0);
			if (kind_bytes[kind])
				length += (size_t)snprintf(line + length, sizeof(line) - length, " %llu KB", kind_bytes[kind] / 1024);
			if (kind_count[kind] > 1)
				length += (size_t)snprintf(line + length, sizeof(line) - length, " (%lux)", kind_count[kind]);
			length += (size_t)snprintf(line + length, sizeof(line) - length, " |");
		}
		platform_log("%s", line);
	}
	for (kind = 0; kind < _halo_load_count; kind++)
	{
		__atomic_store_n(&kind_us[kind], 0, __ATOMIC_RELAXED);
		__atomic_store_n(&kind_bytes[kind], 0, __ATOMIC_RELAXED);
		__atomic_store_n(&kind_count[kind], 0, __ATOMIC_RELAXED);
	}
}
