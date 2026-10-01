/*
VITA_MAIN.C

The process: memory, clocks, the log, the settings passed as environment
variables, and the game's main (source/shell/shell_xbox.c, renamed
halo_main for the Vita build).

ux0:data/haloce-vita/env.txt holds NAME=value lines that are set in the
environment before the game starts (HALO_* settings, port_config.c), as the
desktop builds take them from the shell. The log is
ux0:data/haloce-vita/log.txt (stderr).
*/

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/appmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/devctl.h>
#include <psp2/power.h>
#include <psp2/apputil.h>
#include <psp2/sysmodule.h>

#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "vita_host.h"

#define VITA_DATA_DIRECTORY "ux0:data/haloce-vita"
/* the game's own files (settings, log, init.txt); the maps are shared with
the ones Xita's installer put on the card */
#define VITA_DEFAULT_DATA_ROOT VITA_DATA_DIRECTORY "/data"
#define VITA_DEFAULT_MAPS_ROOT "ux0:data/xita/haloce/maps"
#define ARENA_SIZE 0x07000000UL /* PLATFORM_CONTIGUOUS_SIZE */

/* newlib's heap (malloc) */
unsigned int _newlib_heap_size_user = 48 * 1024 * 1024;

/* The game runs on a thread of its own: its frames are large (the AI's
firing position search overflowed the main thread's default stack on
hardware; Linux gives 8 MB) */
#define GAME_THREAD_STACK_SIZE (16 * 1024 * 1024)

int halo_main(int argc, char **argv);

static void *arena;
static SceUID arena_block = -1;

/* a thread pinned to one core; 0 on success */
struct thread_start
{
	void (*function)(void *);
	void *argument;
};

static int thread_trampoline(SceSize size, void *argument)
{
	struct thread_start start = *(struct thread_start *)argument;

	(void)size;
	start.function(start.argument);
	return 0;
}

int vita_host_thread_start_priority(const char *name, void (*function)(void *), void *argument, int core, int priority)
{
	struct thread_start start;
	SceUID thread;
	static const int masks[3] = { 0x10000, 0x20000, 0x40000 }; /* SCE_KERNEL_CPU_MASK_USER_0..2 */

	start.function = function;
	start.argument = argument;
	thread = sceKernelCreateThread(name, thread_trampoline, priority, 1024 * 1024, 0,
		core >= 0 && core < 3 ? masks[core] : 0, NULL);
	if (thread < 0)
		return -1;
	sceKernelStartThread(thread, sizeof(start), &start);
	return 0;
}

int vita_host_thread_start(const char *name, void (*function)(void *), void *argument, int core)
{
	return vita_host_thread_start_priority(name, function, argument, core, 0x10000100);
}

void vita_host_sleep_us(unsigned long microseconds)
{
	sceKernelDelayThread((SceUInt)microseconds);
}

void vita_host_pin_current_thread(int core)
{
	static const int masks[3] = { 0x10000, 0x20000, 0x40000 }; /* SCE_KERNEL_CPU_MASK_USER_0..2 */

	if (core >= 0 && core < 3)
		sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), masks[core]);
	{
		/* HALO_THREAD_PRIORITY=<64..191>: the game's busy threads below the
		default, so GXM's display queue thread and the driver's are not
		starved when all three cores are busy (the worker's present
		blocked ~40 ms/frame with the tick on its own core) */
		const char *setting = getenv("HALO_THREAD_PRIORITY");
		int priority = setting ? atoi(setting) : 0;

		if (priority >= 64 && priority <= 191)
			sceKernelChangeThreadPriority(sceKernelGetThreadId(), priority);
	}
}

void vita_host_log_memory(const char *when)
{
	SceKernelFreeMemorySizeInfo info;
	char message[160];

	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	sceKernelGetFreeMemorySize(&info);
	snprintf(message, sizeof(message), "vita: free memory %s: user %d KB, cdram %d KB, phycont %d KB", when,
		info.size_user / 1024, info.size_cdram / 1024, info.size_phycont / 1024);
	vita_host_log(message);
}

void *vita_host_arena(unsigned long *size)
{
	if (!arena)
	{
		vita_host_log_memory("before the memory window");
		arena_block = sceKernelAllocMemBlock("halo_contiguous", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, ARENA_SIZE, NULL);
		if (arena_block >= 0)
			sceKernelGetMemBlockBase(arena_block, &arena);
		else
			fprintf(stderr, "vita: cannot allocate the %lu byte memory window: 0x%08x\n", ARENA_SIZE,
				(unsigned)arena_block);
	}
	*size = arena ? ARENA_SIZE : 0;
	return arena;
}

unsigned long long vita_host_time_us(void)
{
	return sceKernelGetProcessTimeWide();
}

static void read_environment_file(void)
{
	FILE *file = fopen(VITA_DATA_DIRECTORY "/env.txt", "r");
	char line[512];

	if (!file)
		return;
	while (fgets(line, sizeof(line), file))
	{
		char *equals = strchr(line, '=');
		char *end = line + strlen(line);

		while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '))
			*--end = 0;
		if (line[0] == '#' || !equals)
			continue;
		*equals = 0;
		setenv(line, equals + 1, 1);
		{
			char message[600];
			snprintf(message, sizeof(message), "vita: %s=%s", line, equals + 1);
			vita_host_log(message);
		}
	}
	fclose(file);
}

/* the clocks the platform layer's waits rely on (xbox_kernel.c), logged */
static void clock_check(void)
{
	struct timespec realtime, monotonic, deadline;
	struct timeval tv;
	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
	unsigned long long before, after;
	char message[256];
	int result;

	clock_gettime(CLOCK_REALTIME, &realtime);
	clock_gettime(CLOCK_MONOTONIC, &monotonic);
	gettimeofday(&tv, NULL);
	snprintf(message, sizeof(message), "vita: clocks: realtime %lld.%09ld monotonic %lld.%09ld gettimeofday %lld.%06ld time %lld",
		(long long)realtime.tv_sec, realtime.tv_nsec, (long long)monotonic.tv_sec, monotonic.tv_nsec,
		(long long)tv.tv_sec, (long)tv.tv_usec, (long long)time(NULL));
	vita_host_log(message);
	deadline = realtime;
	deadline.tv_nsec += 50000000L;
	if (deadline.tv_nsec >= 1000000000L)
	{
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000L;
	}
	before = sceKernelGetProcessTimeWide();
	pthread_mutex_lock(&lock);
	result = pthread_cond_timedwait(&condition, &lock, &deadline);
	pthread_mutex_unlock(&lock);
	after = sceKernelGetProcessTimeWide();
	snprintf(message, sizeof(message), "vita: a 50 ms timed wait returned %d after %llu us", result, after - before);
	vita_host_log(message);
}

/* what the primitives the render path leans on cost here (the log names
them: a thread id or the process time per call, and copying into uncached
memory, as the frame rings are, by memcpy and by NEON stores) */
#include <arm_neon.h>

static void neon_copy(void *destination, const void *source, unsigned int bytes)
{
	uint8_t *d = destination;
	const uint8_t *s = source;

	for (; bytes >= 32; bytes -= 32, d += 32, s += 32)
	{
		uint8x16_t a = vld1q_u8(s), b = vld1q_u8(s + 16);

		vst1q_u8(d, a);
		vst1q_u8(d + 16, b);
	}
	for (; bytes >= 16; bytes -= 16, d += 16, s += 16)
		vst1q_u8(d, vld1q_u8(s));
	if (bytes)
		memcpy(d, s, bytes);
}

static void primitive_benchmarks(void)
{
	enum { COUNT = 2000, BLOCK = 1536, WINDOW = 256 * 1024 };
	unsigned long long before, after;
	char message[256];
	unsigned int index, sink = 0;
	SceUID block;
	void *uncached = NULL, *cached = malloc(WINDOW);
	unsigned char *source = malloc(BLOCK);

	before = sceKernelGetProcessTimeWide();
	for (index = 0; index < COUNT; index++)
		sink += (unsigned int)(uintptr_t)pthread_self();
	after = sceKernelGetProcessTimeWide();
	snprintf(message, sizeof(message), "vita: primitives: pthread_self %.2f us", (after - before) / (double)COUNT);
	vita_host_log(message);
	before = sceKernelGetProcessTimeWide();
	for (index = 0; index < COUNT; index++)
		sink += (unsigned int)sceKernelGetThreadId();
	after = sceKernelGetProcessTimeWide();
	snprintf(message, sizeof(message), "vita: primitives: sceKernelGetThreadId %.2f us", (after - before) / (double)COUNT);
	vita_host_log(message);
	before = sceKernelGetProcessTimeWide();
	for (index = 0; index < COUNT; index++)
		sink += (unsigned int)sceKernelGetProcessTimeWide();
	after = sceKernelGetProcessTimeWide();
	snprintf(message, sizeof(message), "vita: primitives: sceKernelGetProcessTimeWide %.2f us", (after - before) / (double)COUNT);
	vita_host_log(message);

	block = sceKernelAllocMemBlock("benchmark", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, WINDOW, NULL);
	if (block >= 0)
		sceKernelGetMemBlockBase(block, &uncached);
	if (uncached && cached && source)
	{
		unsigned int offset = 0;
		unsigned char *target;
		int pass;

		memset(source, 0x5a, BLOCK);
		for (pass = 0; pass < 4; pass++)
		{
			target = pass < 2 ? uncached : cached;
			before = sceKernelGetProcessTimeWide();
			for (index = 0; index < COUNT; index++)
			{
				if (pass & 1)
					neon_copy(target + offset, source, BLOCK);
				else
					memcpy(target + offset, source, BLOCK);
				offset = (offset + BLOCK + 64) % (WINDOW - BLOCK - 64);
			}
			after = sceKernelGetProcessTimeWide();
			snprintf(message, sizeof(message), "vita: primitives: %s copy of %d bytes into %s memory: %.2f us each, %.0f MB/s",
				pass & 1 ? "NEON" : "memcpy", (int)BLOCK, pass < 2 ? "uncached" : "cached",
				(after - before) / (double)COUNT, (double)BLOCK * COUNT / ((after - before) ? (after - before) : 1));
			vita_host_log(message);
		}
	}
	if (block >= 0)
		sceKernelFreeMemBlock(block);
	free(cached);
	free(source);
	(void)sink;
}

/* the hang watchdog: once the game has drawn 60 frames, 8 s without another
one is a hang, and a deliberate crash gets every thread into the core dump */
extern volatile unsigned long halo_present_counter;

/* a heartbeat file, one line every 2 s from its own thread: tells a
process that is alive with a dead log from one that is frozen */
static void heartbeat_thread(void *unused)
{
	unsigned long beats = 0;

	(void)unused;
	for (;;)
	{
		SceUID file;
		char line[64];
		int length;

		sceKernelDelayThread(2000000);
		/* the game is played with the sticks and buttons, which the system
		counts as activity, but a cinematic or a long load is not: without
		this the Vita dims and goes to sleep in the middle of one */
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
		file = sceIoOpen(VITA_DATA_DIRECTORY "/heartbeat.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
		if (file < 0)
			continue;
		length = snprintf(line, sizeof(line), "%lu presents %lu at %llu us\n", ++beats, halo_present_counter,
			(unsigned long long)sceKernelGetProcessTimeWide());
		sceIoWrite(file, line, length);
		sceIoClose(file);
	}
}

/* HALO_CRASH_AT=<seconds>: a thread that touches no file and no lock, and
at that time deliberately crashes the process for a dump of every thread -
for a freeze that even the watchdog cannot report (if this thread cannot
run either, the process is suspended by the system) */
static void tripwire_thread(void *argument)
{
	unsigned long seconds = (unsigned long)argument;
	static volatile unsigned long tripwire_ticks;

	while (tripwire_ticks < seconds)
	{
		sceKernelDelayThread(1000000);
		tripwire_ticks++;
	}
	*(volatile int *)48 = 0;
}

static void watchdog_thread(void *unused)
{
	unsigned long last = 0, same = 0;

	(void)unused;
	for (;;)
	{
		unsigned long now;

		sceKernelDelayThread(1000000);
		now = halo_present_counter;
		if (now < 10)
			continue;
		same = now == last ? same + 1 : 0;
		last = now;
		if (same >= 8)
		{
			/* 8 s without a present: logged, and HALO_HANG_CRASH=1 makes it
			a deliberate crash for a dump (the crash comes first then: a log
			write could block on whatever the hung thread holds) - a dump of
			this process stalled and wedged the shell on Sept 30 */
			static volatile unsigned long watchdog_frame;

			watchdog_frame = now;
			if (getenv("HALO_HANG_CRASH") && atoi(getenv("HALO_HANG_CRASH")))
				*(volatile int *)0 = 0;
			vita_host_log("watchdog: 8 s without a present (shaders compiling?); HALO_HANG_CRASH=1 would crash for a dump");
			same = 0;
		}
	}
}

static int game_thread(SceSize arguments_size, void *arguments_data)
{
	static char *arguments[] = { "halo", NULL };

	(void)arguments_size;
	(void)arguments_data;
	/* (above the game's threads, which spin at the default priority
	while they wait on each other: at their priority it never ran) */
	{
		int started = vita_host_thread_start_priority("watchdog", watchdog_thread, NULL, -1, 64);

		if (started < 0)
			started = vita_host_thread_start("watchdog", watchdog_thread, NULL, 2);
		vita_host_log(started < 0 ? "watchdog: could not start" : "watchdog: started");
	}
	vita_host_thread_start_priority("heartbeat", heartbeat_thread, NULL, -1, 96);

	sceKernelExitThread(halo_main(1, arguments));
	return 0;
}

int main(int argc, char **argv)
{
	static char *arguments[] = { "halo", NULL };
	SceAppUtilInitParam init;
	SceAppUtilBootParam boot;

	(void)argc;
	(void)argv;
	sceIoMkdir(VITA_DATA_DIRECTORY, 0777);
	sceIoMkdir(VITA_DATA_DIRECTORY "/saves", 0777);
	if (freopen(VITA_DATA_DIRECTORY "/log.txt", "w", stderr))
		setvbuf(stderr, NULL, _IONBF, 0);
	sceIoRemove(VITA_DATA_DIRECTORY "/halo.log");
	vita_host_log("vita: Halo CE Vita starting");

	memset(&init, 0, sizeof(init));
	memset(&boot, 0, sizeof(boot));
	sceAppUtilInit(&init, &boot);
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);

	sceIoMkdir(VITA_DEFAULT_DATA_ROOT, 0777);
	setenv("HALO_DATA_ROOT", VITA_DEFAULT_DATA_ROOT, 0);
	setenv("HALO_MAPS_ROOT", VITA_DEFAULT_MAPS_ROOT, 0);
	setenv("HALO_SAVE_ROOT", VITA_DATA_DIRECTORY "/saves", 0);
	setenv("HALO_NET_ONLINE", "false", 0);
	/* the Vita's 16:9 screen: 480 lines of 848 columns (the port widens the
	3D view and centres the menus) */
	setenv("HALO_DISPLAY_WIDTH", "848", 0);
	setenv("HALO_UPDATE_AUTO", "false", 0);
	read_environment_file();
	{
		const char *crash_at = getenv("HALO_CRASH_AT");

		if (crash_at && atoi(crash_at) > 0)
			vita_host_thread_start_priority("tripwire", tripwire_thread, (void *)(unsigned long)atoi(crash_at), -1, 64);
	}
	{
		char message[128];
		snprintf(message, sizeof(message), "vita: arm %d MHz, bus %d MHz, gpu %d MHz", scePowerGetArmClockFrequency(),
			scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency());
		vita_host_log(message);
	}

	clock_check();
	primitive_benchmarks();
	{
		/* free space on the memory card: the cache files of the maps the
		game decompresses take up to 765 MB */
		struct { unsigned long long maximum, free; unsigned int cluster; unsigned int unknown; } space;
		char message[128];

		memset(&space, 0, sizeof(space));
		if (sceIoDevctl("ux0:", 0x3001, NULL, 0, &space, sizeof(space)) >= 0)
		{
			snprintf(message, sizeof(message), "vita: ux0: %llu MB free of %llu MB", space.free >> 20, space.maximum >> 20);
			vita_host_log(message);
		}
		else
		{
			/* (the devctl is refused on some firmware: the app manager's
			view of the device) */
			uint64_t maximum = 0, free = 0;

			if (sceAppMgrGetDevInfo("ux0:", &maximum, &free) >= 0)
				snprintf(message, sizeof(message), "vita: ux0: %llu MB free of %llu MB (app manager)", (unsigned long long)free >> 20, (unsigned long long)maximum >> 20);
			else
				snprintf(message, sizeof(message), "vita: ux0: free space unknown (devctl and app manager both refused)");
			vita_host_log(message);
		}
	}
	{
		SceUID thread = sceKernelCreateThread("halo", game_thread, 0x10000100, GAME_THREAD_STACK_SIZE, 0,
			0x10000 /* core 0 */, NULL);
		int status = 0;

		if (thread < 0)
		{
			vita_host_log("vita: cannot create the game thread");
			return halo_main(1, arguments);
		}
		sceKernelStartThread(thread, 0, NULL);
		sceKernelWaitThreadEnd(thread, &status, NULL);
		return status;
	}
}

void vita_host_log(const char *line)
{
	static SceUID file = -1;
	char stamp[32];
	unsigned long long now = sceKernelGetProcessTimeWide();
	int length;

	if (file < 0)
		file = sceIoOpen(VITA_DATA_DIRECTORY "/halo.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
	length = snprintf(stamp, sizeof(stamp), "%7llu.%03llu ", now / 1000000ULL, now / 1000ULL % 1000ULL);
	sceClibPrintf("%s%s\n", stamp, line);
	if (file >= 0)
	{
		sceIoWrite(file, stamp, length);
		sceIoWrite(file, line, strlen(line));
		sceIoWrite(file, "\n", 1);
	}
}
