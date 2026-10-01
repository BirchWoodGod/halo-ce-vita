/*
VITA_CPU.C

Per-core busy time, as Xita's xv_cpu.c measured it: the kernel's idle clock
per core (sceKernelGetSystemInfo), sampled about once a second. Busy is the
share of the interval the core was not idle; 255 means unknown (Vita3K
returns no counters, and the fourth core is the system's).
*/

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>

#include <string.h>

#include "vita_host.h"

#define PERIOD_US 1000000ULL

static SceKernelSystemInfo previous;
static unsigned long long previous_time, last_poll;
static int have_previous;
static unsigned char busy[3] = { 255, 255, 255 };

static unsigned int cpu_mask(unsigned int mask)
{
	return ((mask >> 16) | mask) & 7u;
}

void vita_host_cpu_usage(unsigned char out[3])
{
	unsigned long long now = sceKernelGetProcessTimeWide();
	SceKernelSystemInfo info;

	if (last_poll && now - last_poll < PERIOD_US)
	{
		memcpy(out, busy, 3);
		return;
	}
	last_poll = now;
	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	if (sceKernelGetSystemInfo(&info) < 0 || !cpu_mask(info.activeCpuMask))
	{
		busy[0] = busy[1] = busy[2] = 255;
		have_previous = 0;
	}
	else
	{
		if (have_previous && now > previous_time)
		{
			unsigned long long elapsed = now - previous_time;
			unsigned int active = cpu_mask(previous.activeCpuMask) & cpu_mask(info.activeCpuMask);
			unsigned int core;

			for (core = 0; core < 3; core++)
			{
				unsigned long long old_idle = previous.cpuInfo[core].idleClock, idle = info.cpuInfo[core].idleClock;
				unsigned long long idle_delta;

				busy[core] = 255;
				if (!(active & (1u << core)) || idle < old_idle)
					continue;
				idle_delta = idle - old_idle;
				if (idle_delta > elapsed && idle_delta - elapsed > 2000u)
					continue;
				busy[core] = idle_delta >= elapsed ? 0 : (unsigned char)(100 - idle_delta * 100 / elapsed);
			}
		}
		previous = info;
		previous_time = now;
		have_previous = 1;
	}
	memcpy(out, busy, 3);
}
