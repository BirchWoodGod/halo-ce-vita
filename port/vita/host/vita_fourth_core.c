/*
VITA_FOURTH_CORE.C

The Vita's fourth core (CPU 3: affinity 0x80000, SCE_KERNEL_CPU_MASK_SYSTEM)
for the port's background work, and the threads' run times in halo.log.

The system keeps the fourth core for its own processes. A thread of a game
asks for it with sceKernelChangeThreadCpuAffinityMask(thread, 0x80000); the
kernel's thread manager refuses that mask to a game application unless a
kernel plugin lifts the check (GrapheneCt's CapUnlocker, earlier
CoreUnlocker80000H, hooks SceKernelThreadMgr's affinity test for it), and
Vita3K refuses it too (SCE_KERNEL_ERROR_ILLEGAL_CPU_AFFINITY_MASK). So each
helper asks, reads its mask back, and stays where it was when refused or
given another mask.

"Fourth core helpers" (HALO_CPU3_AUX, Graphics > Advanced; read at
start-up) has the levels of Bruno Santana's modified build, whose setting
this follows (he found the fourth core usable with CapUnlocker, and the
whole tick there too much for it: the fourth core above 90% and the game at
10-15 FPS). Off. Audio (1): the sound mixer (SDL's audio thread) there.
All async (2): the mixer and the work no frame waits for each time - GXM's
display queue thread, which does little but at priority 64 puts one of the
game's threads off each time it wakes (HALO_FOURTH_CORE_DISPLAY=0 leaves
it on cores 0-2, a test), the cache file thread (the map's texture and
sound reads), the map decompression's copy, the checkpoint writer, the
shader compiler's background compiles, the old shader cache's clean-up and
the log's writer. The system's processes share the fourth core (about
10-15% of it while a game plays, as reported when the plugin came out): a
helper there is late at worst - a sound read or a mix late, the reason the
sound mixer's line says how late the audio device's calls came and how
long the game waited for the mixer's lock. The game's thread, the render
worker and the tick stay on cores 0-2. HALO_AUDIO_CORE (0-3) places the
mixer whatever the level.

The run times: the threads named here (the helpers and the game's busy
threads: the game thread, the render worker, the tick, the mixer, the
display queue) have their kernel run time (runClocks, microseconds) logged
with each frame-timing line (Performance logging) as milliseconds a frame,
with the core each last ran on, and each core's busy share from the
kernel's idle clocks, the fourth core's included (vita_cpu.c's, once a
second, are the overlay's).
*/

#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vita_host.h"

#define FOURTH_CORE_MASK 0x80000 /* SCE_KERNEL_CPU_MASK_SYSTEM */
#define MAXIMUM_WATCHED 16

static struct
{
	char role[24];
	SceUID thread;
	/* runClocks at the last report */
	unsigned long long run;
	/* on the fourth core (it asked and was given it) */
	int fourth;
	/* filled in (a report skips an entry still being written) */
	volatile int ready;
} watched[MAXIMUM_WATCHED];
static volatile int watched_count;

static SceKernelSystemInfo previous_system;
static unsigned long long previous_report;
static int have_previous_system;

/* Fourth core helpers' level: 0 off, 1 audio, 2 all async (HALO_CPU3_AUX,
read once) */
int vita_host_fourth_core_level(void)
{
	static int level = -1;

	if (level < 0)
	{
		const char *setting = getenv("HALO_CPU3_AUX");

		level = setting ? atoi(setting) : 0;
		if (level < 0 || level > 2)
			level = level < 0 ? 0 : 2;
	}
	return level;
}

static int watch(const char *role, int fourth)
{
	SceUID self = sceKernelGetThreadId();
	int index, count = __atomic_load_n(&watched_count, __ATOMIC_ACQUIRE);

	for (index = 0; index < count; index++)
		if (__atomic_load_n(&watched[index].ready, __ATOMIC_ACQUIRE) && watched[index].thread == self)
		{
			watched[index].fourth = fourth;
			return index;
		}
	index = __atomic_fetch_add(&watched_count, 1, __ATOMIC_ACQ_REL);
	if (index >= MAXIMUM_WATCHED)
	{
		__atomic_store_n(&watched_count, MAXIMUM_WATCHED, __ATOMIC_RELEASE);
		return -1;
	}
	snprintf(watched[index].role, sizeof(watched[index].role), "%s", role);
	watched[index].thread = self;
	watched[index].fourth = fourth;
	{
		SceKernelThreadInfo info;

		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		watched[index].run = sceKernelGetThreadInfo(self, &info) >= 0 ? info.runClocks : 0;
	}
	__atomic_store_n(&watched[index].ready, 1, __ATOMIC_RELEASE);
	return index;
}

void vita_host_thread_watch(const char *role)
{
	watch(role, 0);
}

/* the calling thread onto the fourth core, whatever the setting: 3 when it
moved, -1 when it stays (it is watched either way) */
int vita_host_fourth_core_move(const char *role)
{
	SceUID self = sceKernelGetThreadId();
	int before = sceKernelGetThreadCpuAffinityMask(self);
	int result = sceKernelChangeThreadCpuAffinityMask(self, FOURTH_CORE_MASK);
	int after = sceKernelGetThreadCpuAffinityMask(self);
	char message[200];

	if (result >= 0 && after == FOURTH_CORE_MASK)
	{
		/* (the core it runs on now says the move happened: a thread is
		never left on a core its mask leaves out) */
		snprintf(message, sizeof(message), "fourth core: %s on core 3 (affinity 0x%x, was 0x%x; running on cpu %d)", role,
			(unsigned)after, (unsigned)before, sceKernelGetCpuId());
		vita_host_log(message);
		watch(role, 1);
		return 3;
	}
	/* refused, or another mask given: the one it had */
	if (after != before && before >= 0)
		sceKernelChangeThreadCpuAffinityMask(self, before);
	if (result < 0)
		snprintf(message, sizeof(message), "fourth core: not allowed (0x%08x): %s stays on %s (affinity 0x%x)",
			(unsigned)result, role, before > 0 && before != 0x70000 ? "its core" : "cores 0-2", (unsigned)before);
	else
		snprintf(message, sizeof(message), "fourth core: asked 0x%x, given 0x%x: %s stays on %s (affinity 0x%x)",
			FOURTH_CORE_MASK, (unsigned)after, role, before > 0 && before != 0x70000 ? "its core" : "cores 0-2",
			(unsigned)before);
	vita_host_log(message);
	watch(role, 0);
	return -1;
}

/* the calling thread onto the fourth core if Fourth core helpers is at
`level` or above */
int vita_host_fourth_core_join(const char *role, int level)
{
	if (vita_host_fourth_core_level() < level)
	{
		char message[200];

		snprintf(message, sizeof(message), "fourth core: %s stays on cores 0-2 (Graphics > Advanced > Fourth core "
			"helpers: %s)", role, vita_host_fourth_core_level() ? "Audio" : "Off");
		vita_host_log(message);
		watch(role, 0);
		return -1;
	}
	return vita_host_fourth_core_move(role);
}

/* with each frame-timing line: the watched threads' run time a frame, and
the cores' busy shares, since the last */
void vita_host_thread_times_report(unsigned long frames)
{
	char line[1000];
	int length, index, shown = 0, count = __atomic_load_n(&watched_count, __ATOMIC_ACQUIRE);
	unsigned long long now = sceKernelGetProcessTimeWide(), fourth_total = 0;
	SceKernelSystemInfo system;

	if (!frames)
		return;
	if (count > MAXIMUM_WATCHED)
		count = MAXIMUM_WATCHED;
	length = snprintf(line, sizeof(line), "thread-times (ms/frame, %lu frames)", frames);
	for (index = 0; index < count; index++)
	{
		SceKernelThreadInfo info;
		unsigned long long run;

		if (!__atomic_load_n(&watched[index].ready, __ATOMIC_ACQUIRE))
			continue;
		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (sceKernelGetThreadInfo(watched[index].thread, &info) < 0)
			continue;
		run = info.runClocks >= watched[index].run ? info.runClocks - watched[index].run : 0;
		watched[index].run = info.runClocks;
		if (watched[index].fourth)
			fourth_total += run;
		if (length < (int)sizeof(line))
			length += snprintf(line + length, sizeof(line) - (size_t)length, "%s %s %.2f [cpu %d%s]",
				shown++ ? " |" : ":", watched[index].role, (double)run / 1000.0 / (double)frames,
				(int)info.lastExecutedCpuId, watched[index].fourth ? ", core 3" : "");
	}
	vita_host_log(line);

	memset(&system, 0, sizeof(system));
	system.size = sizeof(system);
	/* (Vita3K answers with nothing filled in: no core active) */
	if (sceKernelGetSystemInfo(&system) < 0 || !(system.activeCpuMask & 0xf000f))
	{
		have_previous_system = 0;
		snprintf(line, sizeof(line), "core-load: unknown (no idle clocks); fourth core helpers %.2f ms/frame",
			(double)fourth_total / 1000.0 / (double)frames);
		vita_host_log(line);
		return;
	}
	if (have_previous_system && now > previous_report)
	{
		unsigned long long elapsed = now - previous_report;
		unsigned int core;

		length = snprintf(line, sizeof(line), "core-load (busy %% over %.1f s):", (double)elapsed / 1e6);
		for (core = 0; core < 4; core++)
		{
			unsigned long long old_idle = previous_system.cpuInfo[core].idleClock, idle = system.cpuInfo[core].idleClock;

			if (idle < old_idle || (idle - old_idle > elapsed && idle - old_idle - elapsed > 2000u))
				length += snprintf(line + length, sizeof(line) - (size_t)length, " core%u ?", core);
			else
				length += snprintf(line + length, sizeof(line) - (size_t)length, " core%u %u%%", core,
					idle - old_idle >= elapsed ? 0u : (unsigned)(100 - (idle - old_idle) * 100 / elapsed));
		}
		snprintf(line + length, sizeof(line) - (size_t)length, "; active mask 0x%x; fourth core helpers %.2f ms/frame",
			(unsigned)system.activeCpuMask, (double)fourth_total / 1000.0 / (double)frames);
		vita_host_log(line);
	}
	previous_system = system;
	previous_report = now;
	have_previous_system = 1;
}
