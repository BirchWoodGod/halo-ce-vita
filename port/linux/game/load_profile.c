/* load_profile.c

What a long frame was spent loading or saving. The places that load (a new
map, a game state save, revert or persistent save, a structure BSP switch,
cache file reads and writes, waits for the cache file thread) and every
file call (xbox_files.c: opens, reads, writes, directory and attribute
calls, closes) add their time and bytes to this frame's totals, from
whichever thread they run on, the file calls as the game thread's or
another's, with the slowest of each by name. main.c's frame-hitch line
names them for a frame over 100 ms (halo_load_profile_describe), so a hitch
on the hardware says what it waited for without a switch turned on: on the
Vita a file call waits for the memory card, which serves one at a time -
behind a map copy's write, a texture's read or a save.

HALO_LOAD_PROFILE=1 logs them on a line of their own besides, for a frame
longer than HALO_LOAD_PROFILE_MS (default 100):

  load-profile: frame 1037 took 6118.6 ms: new map 3510.2 ms | persistent
  header+checksum 812.0 ms 16384 KB | ...

Times on the cache file thread overlap the game thread's; the "wait" entry
is the game thread blocked on it. HALO_LOAD_PROFILE=0 turns it all off (each
place then costs a load of the switch); on, each costs two clock reads. */

#include <stdio.h>
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
	"file calls (game thread)",
	"file calls (other threads)",
};

int halo_load_profile_enabled = -1;
/* (HALO_LOAD_PROFILE=1: the load-profile line too) */
static int line_wanted;
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

		halo_load_profile_enabled = !setting || atoi(setting) != 0;
		line_wanted = setting && atoi(setting) != 0;
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

/* the game thread's index (render_epoch.c), learnt at its first frame's
end: a file call before it counts as another thread's */
int halo_thread_index(void);
static volatile int game_thread_index = -1;

/* the frame's slowest file call, the game thread's [0] and the others' [1] */
static struct
{
	unsigned long long us, bytes;
	char call[20];
	char path[56];
} slowest_file_call[2];
static volatile int slowest_lock;

void halo_load_profile_file_call(unsigned long long started_us, const char *call, const char *path,
	unsigned long long bytes)
{
	unsigned long long took;
	int game, game_index = __atomic_load_n(&game_thread_index, __ATOMIC_RELAXED);

	if (!started_us || !halo_load_profile_check())
		return;
	took = vita_host_time_us() - started_us;
	game = game_index >= 0 && halo_thread_index() == game_index;
	__atomic_fetch_add(&kind_us[game ? _halo_load_file_game_thread : _halo_load_file_other], took, __ATOMIC_RELAXED);
	__atomic_fetch_add(&kind_bytes[game ? _halo_load_file_game_thread : _halo_load_file_other], bytes, __ATOMIC_RELAXED);
	__atomic_fetch_add(&kind_count[game ? _halo_load_file_game_thread : _halo_load_file_other], 1, __ATOMIC_RELAXED);
	if (took > __atomic_load_n(&slowest_file_call[!game].us, __ATOMIC_RELAXED))
	{
		size_t length = path ? strlen(path) : 0;

		while (__atomic_exchange_n(&slowest_lock, 1, __ATOMIC_ACQUIRE))
			;
		if (took > slowest_file_call[!game].us)
		{
			/* (the path's end: the file's name and its folder) */
			slowest_file_call[!game].us = took;
			slowest_file_call[!game].bytes = bytes;
			snprintf(slowest_file_call[!game].call, sizeof(slowest_file_call[!game].call), "%s", call);
			snprintf(slowest_file_call[!game].path, sizeof(slowest_file_call[!game].path), "%s",
				length >= sizeof(slowest_file_call[!game].path) ? path + length - (sizeof(slowest_file_call[!game].path) - 1) :
					path ? path : "");
		}
		__atomic_store_n(&slowest_lock, 0, __ATOMIC_RELEASE);
	}
}

int halo_load_profile_describe(char *line, int size)
{
	int length = 0, kind, parts = 0;

	if (size <= 0)
		return 0;
	line[0] = 0;
	if (!halo_load_profile_check())
		return 0;
	for (kind = 0; kind < _halo_load_count && length < size - 96; kind++)
	{
		if (!kind_count[kind])
			continue;
		length += snprintf(line + length, (size_t)(size - length), "%s%s %.1f ms", parts++ ? " | " : "", kind_names[kind],
			kind_us[kind] / 1000.0);
		if (kind_bytes[kind] && length < size)
			length += snprintf(line + length, (size_t)(size - length), " %llu KB", kind_bytes[kind] / 1024);
		if (kind_count[kind] > 1 && length < size)
			length += snprintf(line + length, (size_t)(size - length), " (%lux)", kind_count[kind]);
		if ((kind == _halo_load_file_game_thread || kind == _halo_load_file_other) && length < size - 96)
		{
			int which = kind == _halo_load_file_other;

			while (__atomic_exchange_n(&slowest_lock, 1, __ATOMIC_ACQUIRE))
				;
			if (slowest_file_call[which].us)
			{
				length += snprintf(line + length, (size_t)(size - length), ", slowest %s %.1f ms", slowest_file_call[which].call,
					slowest_file_call[which].us / 1000.0);
				if (slowest_file_call[which].bytes && length < size)
					length += snprintf(line + length, (size_t)(size - length), " %llu KB", slowest_file_call[which].bytes / 1024);
				if (slowest_file_call[which].path[0] && length < size)
					length += snprintf(line + length, (size_t)(size - length), " %s", slowest_file_call[which].path);
			}
			__atomic_store_n(&slowest_lock, 0, __ATOMIC_RELEASE);
		}
	}
	if (length >= size)
		line[size - 1] = 0;
	return parts;
}

void halo_load_profile_frame_end(void)
{
	unsigned long long now, elapsed;
	char line[1024];
	size_t length;
	int kind;

	if (!halo_load_profile_check())
		return;
	if (game_thread_index < 0)
		__atomic_store_n(&game_thread_index, halo_thread_index(), __ATOMIC_RELAXED);
	now = vita_host_time_us();
	/* (the first frame counts from the first thing measured: a map loaded
	before it) */
	if (!frame_started_us)
		frame_started_us = first_measured_us;
	elapsed = frame_started_us ? now - frame_started_us : 0;
	frame_started_us = now;
	frame_index++;
	if (line_wanted && elapsed >= threshold_us)
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
	while (__atomic_exchange_n(&slowest_lock, 1, __ATOMIC_ACQUIRE))
		;
	memset(slowest_file_call, 0, sizeof(slowest_file_call));
	__atomic_store_n(&slowest_lock, 0, __ATOMIC_RELEASE);
}
