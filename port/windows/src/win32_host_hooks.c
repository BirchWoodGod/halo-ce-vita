/*
WIN32_HOST_HOOKS.C

The host hooks the native ports' timing and threading code calls, which the
Vita's host (port/vita/host/vita_main.c) and Linux (port/linux/src/
posix_profile.c) define too: a microsecond clock, a short sleep and thread
pinning.

Several game units declare vita_host_sleep_us, vita_host_pin_current_thread,
vita_host_thread_id and vita_host_time_us weak, which COFF makes weak externals, and lld-link
takes a second weak external of a name for a duplicate symbol unless the
name's definition came before it: tools/windows_build.py links this unit
ahead of every other.
*/

#include <windows.h>

unsigned long long vita_host_time_us(void)
{
	static LARGE_INTEGER frequency;
	LARGE_INTEGER counter;

	if (!frequency.QuadPart)
		QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&counter);
	return (unsigned long long)(counter.QuadPart / frequency.QuadPart) * 1000000ULL +
		(unsigned long long)(counter.QuadPart % frequency.QuadPart) * 1000000ULL /
		(unsigned long long)frequency.QuadPart;
}

void vita_host_sleep_us(unsigned long microseconds)
{
	unsigned long long deadline = vita_host_time_us() + microseconds;

	/* Sleep rounds up to the timer period: sleep until about a millisecond
	before the deadline, then yield until it passes (as clock_nanosleep in
	win32_posix.c) */
	for (;;)
	{
		unsigned long long now = vita_host_time_us();

		if (now >= deadline)
			return;
		if (deadline - now > 2000)
			Sleep((DWORD)((deadline - now) / 1000 - 1));
		else
			SwitchToThread();
	}
}

/* (the Vita's cores are pinned; the desktop's scheduler is left alone, as
on Linux) */
void vita_host_pin_current_thread(int core)
{
	(void)core;
}

/* the calling thread's id, for the cache lock's owner test (lruv_cache.c) */
unsigned long vita_host_thread_id(void)
{
	return (unsigned long)GetCurrentThreadId();
}

/* a thread's name for the cache lock's report (lruv_cache.c): none here */
int vita_host_thread_describe(unsigned long id, char *text, unsigned long size)
{
	(void)id;
	(void)text;
	(void)size;
	return 0;
}
