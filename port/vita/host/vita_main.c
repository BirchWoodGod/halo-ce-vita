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
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/devctl.h>
#include <psp2/power.h>
#include <psp2/apputil.h>
#include <psp2/sysmodule.h>
#include <psp2/display.h>
#include <psp2/ctrl.h>

#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "vita_host.h"

#define VITA_DATA_DIRECTORY "ux0:data/haloce-vita"
/* the game's own files (settings, log, init.txt); the maps go in
ux0:data/haloce-vita/maps, or are shared with the ones Xita's installer
put on the card */
#define VITA_DEFAULT_DATA_ROOT VITA_DATA_DIRECTORY "/data"
#define VITA_DEFAULT_MAPS_ROOT VITA_DATA_DIRECTORY "/maps"
#define VITA_XITA_MAPS_ROOT "ux0:data/xita/haloce/maps"
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

/* set once the memory window is allocated (vita_host_log) */
static int log_thread_allowed;
/* where the window lands when nothing is allocated before it (v1.0 to
v1.0.2). The game state lives in it and campaign saves keep its absolute
pointers, so a save resumes only with the window where it was when the
save was made: nothing may be allocated in user memory before
vita_host_arena - no thread, no memory block, no growth of the C heap
(HALO_IO_BENCH's buffer moved it by 1 MB). A window elsewhere is logged
loudly. */
#define VITA_EXPECTED_WINDOW 0x86500000UL
static void io_bench(void);

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
		/* (the log's thread only now: see vita_host_log) */
		log_thread_allowed = 1;
		if (arena && (unsigned long)arena != VITA_EXPECTED_WINDOW)
		{
			char message[320];

			snprintf(message, sizeof(message),
				"vita: WARNING: the memory window is at %p, not at 0x%08lx where v1.0 to v1.0.2 put it: something was "
				"allocated before it (a change at start-up, a plugin?); campaign saves made with the window elsewhere "
				"will not resume (they start the level over)", arena, (unsigned long)VITA_EXPECTED_WINDOW);
			vita_host_log(message);
		}
		/* (debug) HALO_IO_BENCH=1: the memory card's read speed by request
		size - only now the window exists: its buffer, allocated before the
		window, grew the C heap and moved the window by 1 MB */
		if (getenv("HALO_IO_BENCH") && atoi(getenv("HALO_IO_BENCH")))
			io_bench();
	}
	*size = arena ? ARENA_SIZE : 0;
	return arena;
}

unsigned long long vita_host_time_us(void)
{
	return sceKernelGetProcessTimeWide();
}

/* The game looks for d:\bink\<movie>.bik before it opens a movie, and
the Vita plays the MP4 in movies/ in its place (bink_vita.c): an empty
.bik stands in for each MP4 there, so a movie copied in plays */
static void movie_placeholders(void)
{
	SceUID directory = sceIoDopen(VITA_DATA_DIRECTORY "/movies");
	SceIoDirent entry;

	if (directory < 0)
		return;
	sceIoMkdir(VITA_DEFAULT_DATA_ROOT "/bink", 0777);
	memset(&entry, 0, sizeof(entry));
	while (sceIoDread(directory, &entry) > 0)
	{
		size_t length = strlen(entry.d_name);
		char path[320];
		SceIoStat stat;

		if (length > 4 && strcasecmp(entry.d_name + length - 4, ".mp4") == 0 && length < 200)
		{
			snprintf(path, sizeof(path), VITA_DEFAULT_DATA_ROOT "/bink/%.*s.bik", (int)(length - 4), entry.d_name);
			if (sceIoGetstat(path, &stat) < 0)
			{
				SceUID file = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT, 0666);

				if (file >= 0)
				{
					sceIoWrite(file, "mp4 stand-in", 12);
					sceIoClose(file);
				}
			}
		}
		memset(&entry, 0, sizeof(entry));
	}
	sceIoDclose(directory);
}

/* ---------- the missing data screen

Without the maps the game cannot start: a page of text on the display
(drawn by the CPU, before anything else uses the screen) says what to copy
where, and START leaves */

#include "overlay_font.h"

#define MESSAGE_WIDTH 960
#define MESSAGE_HEIGHT 544

static void message_text(uint32_t *pixels, int x, int y, int scale, uint32_t color, const char *text)
{
	for (; *text; text++, x += 8 * scale)
	{
		unsigned char c = (unsigned char)*text;
		int row, column, dy, dx;

		if (c >= 'a' && c <= 'z')
			c = (unsigned char)(c - 'a' + 'A');
		if (c < 32 || c >= 128)
			continue;
		for (row = 0; row < 8; row++)
			for (column = 0; column < 8; column++)
				if (font[c - 32][row] & (0x80 >> column))
					for (dy = 0; dy < scale; dy++)
						for (dx = 0; dx < scale; dx++)
						{
							int px = x + column * scale + dx, py = y + row * scale + dy;

							if (px >= 0 && px < MESSAGE_WIDTH && py >= 0 && py < MESSAGE_HEIGHT)
								pixels[py * MESSAGE_WIDTH + px] = color;
						}
	}
}

static void show_missing_data(void)
{
	static const char *const lines[] = {
		/* (35 characters a line fit at this size) */
		"The game files are missing.",
		"",
		"Copy the maps folder of your",
		"Xbox Halo: Combat Evolved to",
		"",
		"  ux0:data/haloce-vita/maps/",
		"",
		"(ui.map, bloodgulch.map ...)",
		"",
		"Press START to exit.",
	};
	SceUID block = sceKernelAllocMemBlock("message", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 2 * 1024 * 1024, NULL);
	SceDisplayFrameBuf frame;
	SceCtrlData pad;
	uint32_t *pixels;
	unsigned int index;

	vita_host_log("vita: no ui.map in " VITA_DEFAULT_MAPS_ROOT " or " VITA_XITA_MAPS_ROOT);
	if (block < 0 || sceKernelGetMemBlockBase(block, (void **)&pixels) < 0)
		sceKernelExitProcess(0);
	for (index = 0; index < MESSAGE_WIDTH * MESSAGE_HEIGHT; index++)
		pixels[index] = 0xff1a1008u;
	message_text(pixels, 60, 50, 4, 0xff40ff40u, "HALO CE");
	for (index = 0; index < sizeof(lines) / sizeof(lines[0]); index++)
		message_text(pixels, 60, 130 + (int)index * 32, 3, 0xffe0e0e0u, lines[index]);
	memset(&frame, 0, sizeof(frame));
	frame.size = sizeof(frame);
	frame.base = pixels;
	frame.pitch = MESSAGE_WIDTH;
	frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
	frame.width = MESSAGE_WIDTH;
	frame.height = MESSAGE_HEIGHT;
	sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
	for (;;)
	{
		memset(&pad, 0, sizeof(pad));
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_START)
			break;
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
		sceDisplayWaitVblankStart();
	}
	sceKernelExitProcess(0);
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

/* HALO_IO_BENCH=1: sequential reads of a map file (16 MB per request size,
each size from a part of the file not read before, so no cache answers)
at 64 KB, 256 KB, 1 MB and 4 MB a request, logged in MB/s, then the same
with sceIoPread at increasing offsets (what the cache file thread does).
It runs once the memory window exists (vita_host_arena): its buffer grows
the C heap, which moved the window when the bench ran before it. */
static void io_bench(void)
{
	static const unsigned long sizes[] = { 64 * 1024, 256 * 1024, 1024 * 1024, 4 * 1024 * 1024 };
	const char *root = getenv("HALO_MAPS_ROOT");
	char path[320], message[200];
	unsigned char *buffer = malloc(4 * 1024 * 1024);
	unsigned long index, region = 0;
	int pass;
	SceUID file;

	snprintf(path, sizeof(path), "%s/a10.map", root ? root : VITA_DEFAULT_MAPS_ROOT);
	file = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (file < 0 || !buffer)
	{
		snprintf(message, sizeof(message), "io bench: cannot open %s (0x%08x)", path, (unsigned)file);
		vita_host_log(message);
		free(buffer);
		return;
	}
	for (pass = 0; pass < 2; pass++)
	{
		for (index = 0; index < sizeof(sizes) / sizeof(sizes[0]); index++, region++)
		{
			unsigned long long start_offset = (unsigned long long)region * 16 * 1024 * 1024;
			unsigned long long before = sceKernelGetProcessTimeWide(), after;
			unsigned long done = 0;

			if (!pass)
				sceIoLseek(file, start_offset, SCE_SEEK_SET);
			while (done < 16 * 1024 * 1024)
			{
				int result = pass ?
					sceIoPread(file, buffer, sizes[index], start_offset + done) :
					sceIoRead(file, buffer, sizes[index]);

				if (result <= 0)
					break;
				done += (unsigned long)result;
			}
			after = sceKernelGetProcessTimeWide();
			snprintf(message, sizeof(message), "io bench: %s %4lu KB requests: %lu KB in %.1f ms = %.1f MB/s",
				pass ? "pread" : "read ", sizes[index] / 1024, done / 1024, (after - before) / 1000.0,
				after > before ? (done / 1048576.0) / ((after - before) / 1000000.0) : 0.0);
			vita_host_log(message);
		}
	}
	sceIoClose(file);
	free(buffer);
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
	/* (debug) HALO_HEARTBEAT=1: a line every 2 s in heartbeat.txt, to tell
	from afar whether the game still runs; the power tick is always sent */
	int write_beats = getenv("HALO_HEARTBEAT") && atoi(getenv("HALO_HEARTBEAT"));

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
		if (!write_beats)
			continue;
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
	/* the last session's log is kept (a crash leaves no other trace) */
	sceIoRemove(VITA_DATA_DIRECTORY "/halo-prev.log");
	sceIoRename(VITA_DATA_DIRECTORY "/halo.log", VITA_DATA_DIRECTORY "/halo-prev.log");
	{
		/* (the realtime stamp tells one launch's log from the next) */
		char message[96];

		snprintf(message, sizeof(message), "vita: Halo CE starting, realtime %ld", (long)time(NULL));
		vita_host_log(message);
	}

	memset(&init, 0, sizeof(init));
	memset(&boot, 0, sizeof(boot));
	sceAppUtilInit(&init, &boot);
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);

	sceIoMkdir(VITA_DEFAULT_DATA_ROOT, 0777);
	setenv("HALO_DATA_ROOT", VITA_DEFAULT_DATA_ROOT, 0);
	{
		/* (the maps where this game keeps them, else Xita's copy) */
		SceIoStat stat;

		int own = sceIoGetstat(VITA_DEFAULT_MAPS_ROOT "/ui.map", &stat) >= 0;
		int xita = !own && sceIoGetstat(VITA_XITA_MAPS_ROOT "/ui.map", &stat) >= 0;

		setenv("HALO_MAPS_ROOT", xita ? VITA_XITA_MAPS_ROOT : VITA_DEFAULT_MAPS_ROOT, 0);
		if (!own && !xita)
			show_missing_data();
	}
	setenv("HALO_SAVE_ROOT", VITA_DATA_DIRECTORY "/saves", 0);
	setenv("HALO_NET_ONLINE", "false", 0);
	/* the Vita's 16:9 screen: 480 lines of 848 columns (the port widens the
	3D view and centres the menus) */
	setenv("HALO_DISPLAY_WIDTH", "848", 0);
	setenv("HALO_UPDATE_AUTO", "false", 0);
	read_environment_file();
	vita_settings_load();
	movie_placeholders();
	vita_net_adhoc_probe();
	vita_net_selftest();
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

	/* (debug) HALO_STARTUP_CHECKS=1: the clocks and the cost of the
	primitives the render path leans on, logged */
	if (getenv("HALO_STARTUP_CHECKS") && atoi(getenv("HALO_STARTUP_CHECKS")))
	{
		clock_check();
		primitive_benchmarks();
	}
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

/* The log goes to ux0:data/haloce-vita/halo.log through a thread of its
own: written from the game's threads, three sceIoWrite calls a line waited
for the memory card, which a background write or the cache file thread's
reads keep busy for up to a second (the frame after a checkpoint froze on a
log line while the checkpoint was being written). A line is copied into a
ring the log thread empties; a line that says it crashes for the dump is
written at once, with everything before it, since the crash follows. */
#define LOG_RING_SIZE (256 * 1024)
static char log_ring[LOG_RING_SIZE];
static volatile unsigned long log_head, log_tail; /* written up to / queued up to */
static volatile int log_lock;
static SceUID log_file = -1, log_semaphore = -1;
static int log_thread_state; /* 0 none, 1 running, -1 could not start */

static void log_acquire(void)
{
	while (__atomic_exchange_n(&log_lock, 1, __ATOMIC_ACQUIRE))
		;
}

static void log_release(void)
{
	__atomic_store_n(&log_lock, 0, __ATOMIC_RELEASE);
}

/* writes what the ring holds (the log thread, or a crashing line's thread) */
static void log_drain(void)
{
	static volatile int draining;
	char chunk[16 * 1024];

	while (__atomic_exchange_n(&draining, 1, __ATOMIC_ACQUIRE))
		sceKernelDelayThread(100);
	for (;;)
	{
		unsigned long length, start, first;

		log_acquire();
		length = log_tail - log_head;
		if (length > sizeof(chunk))
			length = sizeof(chunk);
		start = log_head % LOG_RING_SIZE;
		first = length < LOG_RING_SIZE - start ? length : LOG_RING_SIZE - start;
		memcpy(chunk, log_ring + start, first);
		memcpy(chunk + first, log_ring, length - first);
		log_head += length;
		log_release();
		if (!length)
			break;
		if (log_file >= 0)
			sceIoWrite(log_file, chunk, length);
	}
	__atomic_store_n(&draining, 0, __ATOMIC_RELEASE);
}

static int log_thread(SceSize arguments_size, void *arguments)
{
	(void)arguments_size;
	(void)arguments;
	for (;;)
	{
		sceKernelWaitSema(log_semaphore, 1, NULL);
		log_drain();
	}
	return 0;
}

void vita_host_log(const char *line)
{
	char text[1100];
	unsigned long long now = sceKernelGetProcessTimeWide();
	int length;
	int crashing = strstr(line, "crash") != NULL;

	if (log_file < 0)
		log_file = sceIoOpen(VITA_DATA_DIRECTORY "/halo.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
	length = snprintf(text, sizeof(text), "%7llu.%03llu %s\n", now / 1000000ULL, now / 1000ULL % 1000ULL, line);
	if (length < 0)
		return;
	if (length >= (int)sizeof(text))
	{
		length = sizeof(text) - 1;
		text[length - 1] = '\n';
	}
	sceClibPrintf("%s", text);
	/* (the thread, its stack and its semaphore are made after the memory
	window: made before it, at the first line logged, they moved the window
	- and with it the game state, whose absolute pointers a campaign save
	keeps - so the previous build's save resumed into a crash in
	update_queues_reset_and_fill_with_lies) */
	if (log_thread_state == 0 && log_thread_allowed)
	{
		log_thread_state = -1;
		log_semaphore = sceKernelCreateSema("halo log", 0, 0, 0x7fffffff, NULL);
		if (log_semaphore >= 0)
		{
			SceUID thread = sceKernelCreateThread("halo log", log_thread, 0x10000100, 32 * 1024, 0, 0, NULL);

			if (thread >= 0 && sceKernelStartThread(thread, 0, NULL) >= 0)
				log_thread_state = 1;
		}
	}
	log_acquire();
	if (log_thread_state == 1 && !crashing && log_tail - log_head + (unsigned long)length <= LOG_RING_SIZE)
	{
		unsigned long start = log_tail % LOG_RING_SIZE;
		unsigned long first = (unsigned long)length < LOG_RING_SIZE - start ? (unsigned long)length : LOG_RING_SIZE - start;

		memcpy(log_ring + start, text, first);
		memcpy(log_ring, text + first, (unsigned long)length - first);
		log_tail += (unsigned long)length;
		log_release();
		sceKernelSignalSema(log_semaphore, 1);
		return;
	}
	log_release();
	/* (no thread, a full ring, or a crash on its way: in order, now) */
	log_drain();
	if (log_file >= 0)
		sceIoWrite(log_file, text, length);
}
