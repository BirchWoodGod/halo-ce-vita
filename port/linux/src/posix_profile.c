/* posix_profile.c

HALO_PROFILE=<path> (Linux): a sampling profiler. SIGPROF interrupts the
process every millisecond of CPU time and the handler records where the
interrupted thread was (the program counter and link register); at exit the
samples are written to <path> as "pc lr tid" (hex, hex, decimal), for symbolising with the
executable (tools: llvm-addr2line / llvm-symbolizer). */

#if defined(__linux__) && !defined(HALO_VITA) && !defined(HALO_ANDROID)

#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include <sys/prctl.h>

/* a debugger may attach from any process (a hung run is inspected with
gdb -p; Yama otherwise allows only the parent) */
__attribute__((constructor)) static void allow_any_ptracer(void)
{
	prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
}

/* the host hooks the game's threading code takes from the Vita's host
(vita_main.c) as weak references: their desktop definitions */
unsigned long long vita_host_time_us(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	/* (a 32-bit division: the 64-bit one is a slow libcall on ARM) */
	return (unsigned long long)now.tv_sec * 1000000ULL + (unsigned long)now.tv_nsec / 1000UL;
}

void vita_host_sleep_us(unsigned long microseconds)
{
	struct timespec wait;

	wait.tv_sec = microseconds / 1000000UL;
	wait.tv_nsec = (long)(microseconds % 1000000UL) * 1000L;
	nanosleep(&wait, NULL);
}

void vita_host_pin_current_thread(int core)
{
	(void)core;
}

unsigned long vita_host_thread_id(void)
{
	return (unsigned long)pthread_self();
}

/* a thread's name for the cache lock's report (lruv_cache.c): none here,
where the id is pthread_self's */
int vita_host_thread_describe(unsigned long id, char *text, unsigned long size)
{
	(void)id;
	(void)text;
	(void)size;
	return 0;
}

#define MAXIMUM_SAMPLES (1 << 22)
/* HALO_PROFILE_STACK=1 (i386): each sample also records the return addresses
of up to this many frames, walked along the frame pointers (the build keeps
them: -fno-omit-frame-pointer), written after the thread id; the
symbolizer's --inclusive counts a function once per sample it is anywhere
on the stack of */
#define STACK_FRAMES 16
/* (stacks for the first this many samples: 16 MB of a 32-bit address space, beside the game's memory) */
#define STACK_SAMPLES (1 << 18)

static unsigned long *samples;
static unsigned long *stacks;
static volatile unsigned long sample_count;
/* HALO_PROFILE_START=<seconds> (with HALO_PROFILE_SECONDS): samples only
from that long after start-up (a level's fight rather than its loading) */
static volatile int sampling = 1;
static const char *output_path;

static void profile_signal(int signal_number, siginfo_t *information, void *context)
{
	ucontext_t *ucontext = context;
	unsigned long index;

	(void)signal_number;
	(void)information;
	if (!sampling)
		return;
	index = __atomic_fetch_add(&sample_count, 1, __ATOMIC_RELAXED);
	if (index >= MAXIMUM_SAMPLES)
		return;
#if defined(__arm__)
	samples[index * 3] = ucontext->uc_mcontext.arm_pc;
	samples[index * 3 + 1] = ucontext->uc_mcontext.arm_lr;
#elif defined(__i386__)
	samples[index * 3] = ucontext->uc_mcontext.gregs[REG_EIP];
	samples[index * 3 + 1] = 0;
	{
		/* (no link register: for a sample outside the executable - a libc
		memcpy - the first word near the stack top that points into the
		executable's code stands in for it, the likely return address) */
		extern char __executable_start[], etext[];
		unsigned long pc = ucontext->uc_mcontext.gregs[REG_EIP];

		if (pc < (unsigned long)__executable_start || pc >= (unsigned long)etext)
		{
			const unsigned long *stack = (const unsigned long *)ucontext->uc_mcontext.gregs[REG_ESP];
			int word;

			for (word = 0; word < 8; word++)
			{
				if (stack[word] >= (unsigned long)__executable_start && stack[word] < (unsigned long)etext)
				{
					samples[index * 3 + 1] = stack[word];
					break;
				}
			}
		}
		if (stacks && index < STACK_SAMPLES)
		{
			/* (a frame is [saved ebp, return address]; each saved frame
			is above the last and not far from it, else the walk ends -
			code without frame pointers uses ebp for anything) */
			unsigned long frame = ucontext->uc_mcontext.gregs[REG_EBP];
			unsigned long esp = ucontext->uc_mcontext.gregs[REG_ESP];
			unsigned long *out = &stacks[index * STACK_FRAMES];
			int depth;

			for (depth = 0; depth < STACK_FRAMES; depth++)
			{
				unsigned long next, return_address;

				if (frame < esp || frame > esp + (16ul << 20) || (frame & 3))
					break;
				next = ((const unsigned long *)frame)[0];
				return_address = ((const unsigned long *)frame)[1];
				if (return_address < (unsigned long)__executable_start || return_address >= (unsigned long)etext)
					break;
				out[depth] = return_address;
				if (next <= frame || next > frame + (1ul << 20))
				{
					depth++;
					break;
				}
				frame = next;
			}
			if (depth < STACK_FRAMES)
				out[depth] = 0;
		}
	}
#endif
	/* (the thread, to tell the render's samples from the tick's) */
	samples[index * 3 + 2] = (unsigned long)syscall(SYS_gettid);
}

static void profile_write(void)
{
	FILE *file = fopen(output_path, "w");
	unsigned long index, count = sample_count < MAXIMUM_SAMPLES ? sample_count : MAXIMUM_SAMPLES;

	if (!file)
		return;
	for (index = 0; index < count; index++)
	{
		fprintf(file, "%lx %lx %lu", samples[index * 3], samples[index * 3 + 1], samples[index * 3 + 2]);
		if (stacks && index < STACK_SAMPLES)
		{
			int depth;

			for (depth = 0; depth < STACK_FRAMES && stacks[index * STACK_FRAMES + depth]; depth++)
				fprintf(file, " %lx", stacks[index * STACK_FRAMES + depth]);
		}
		fputc('\n', file);
	}
	fclose(file);
	{
		/* (the libraries' addresses, for samples outside the binary) */
		char maps_path[512], line[512];
		FILE *maps = fopen("/proc/self/maps", "r"), *copy;

		snprintf(maps_path, sizeof(maps_path), "%s.maps", output_path);
		copy = maps ? fopen(maps_path, "w") : NULL;
		while (maps && copy && fgets(line, sizeof(line), maps))
			fputs(line, copy);
		if (copy)
			fclose(copy);
		if (maps)
			fclose(maps);
	}
	fprintf(stderr, "profile: %lu samples to %s\n", count, output_path);
}

/* HALO_PROFILE_SECONDS: writes the profile and ends the process after that
long (a headless run has no window to close, and SDL swallows SIGTERM),
counted from HALO_PROFILE_START when that is given */
static void *profile_watchdog(void *seconds)
{
	/* (sleep returns early when the profiling signal lands on this thread,
	which ended runs after half a minute: it sleeps out the remainder) */
	unsigned remaining = (unsigned)(unsigned long)seconds;
	const char *start = getenv("HALO_PROFILE_START");

	if (start && atoi(start) > 0)
	{
		unsigned delay = (unsigned)atoi(start);

		while (delay)
			delay = sleep(delay);
		sampling = 1;
	}
	while (remaining)
		remaining = sleep(remaining);
	profile_write();
	_exit(0);
	return NULL;
}

__attribute__((constructor))
static void profile_start(void)
{
	struct sigaction action;
	struct itimerval timer;
	const char *seconds = getenv("HALO_PROFILE_SECONDS");

	output_path = getenv("HALO_PROFILE");
	if (!output_path || !*output_path)
		return;
	if (seconds && atoi(seconds) > 0 && getenv("HALO_PROFILE_START") && atoi(getenv("HALO_PROFILE_START")) > 0)
		sampling = 0;
	samples = calloc(MAXIMUM_SAMPLES * 3, sizeof(unsigned long));
#if defined(__i386__)
	if (getenv("HALO_PROFILE_STACK") && atoi(getenv("HALO_PROFILE_STACK")) > 0)
		stacks = calloc((size_t)STACK_SAMPLES * STACK_FRAMES, sizeof(unsigned long));
#endif
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = profile_signal;
	action.sa_flags = SA_SIGINFO | SA_RESTART;
	sigaction(SIGPROF, &action, NULL);
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 1000;
	timer.it_value = timer.it_interval;
	setitimer(ITIMER_PROF, &timer, NULL);
	atexit(profile_write);
	if (seconds && atoi(seconds) > 0)
	{
		pthread_t thread;

		pthread_create(&thread, NULL, profile_watchdog, (void *)(unsigned long)atoi(seconds));
	}
}

#endif

#ifdef __arm__
/* ---------- generic atomics for the game's unaligned fields (as
port/vita/host/vita_stubs.c): clang, told nothing is aligned, calls these
for an atomic on a short in a packed structure (object_lights.c) */

#include <stdbool.h>
#include <string.h>

static volatile int generic_atomic_lock;

static void generic_atomic_acquire(void)
{
	while (__atomic_load_n(&generic_atomic_lock, __ATOMIC_RELAXED) ||
		__atomic_exchange_n(&generic_atomic_lock, 1, __ATOMIC_ACQUIRE))
		;
}

static void generic_atomic_release(void)
{
	__atomic_store_n(&generic_atomic_lock, 0, __ATOMIC_RELEASE);
}

bool generic_atomic_compare_exchange(size_t size, void *object, void *expected, const void *desired, int success, int failure) __asm__("__atomic_compare_exchange");
bool generic_atomic_compare_exchange(size_t size, void *object, void *expected, const void *desired, int success, int failure)
{
	bool matched;

	(void)success;
	(void)failure;
	generic_atomic_acquire();
	matched = memcmp(object, expected, size) == 0;
	if (matched)
		memcpy(object, desired, size);
	else
		memcpy(expected, object, size);
	generic_atomic_release();
	return matched;
}

void generic_atomic_load(size_t size, const void *object, void *result, int order) __asm__("__atomic_load");
void generic_atomic_load(size_t size, const void *object, void *result, int order)
{
	(void)order;
	generic_atomic_acquire();
	memcpy(result, object, size);
	generic_atomic_release();
}

void generic_atomic_store(size_t size, void *object, const void *value, int order) __asm__("__atomic_store");
void generic_atomic_store(size_t size, void *object, const void *value, int order)
{
	(void)order;
	generic_atomic_acquire();
	memcpy(object, value, size);
	generic_atomic_release();
}

void generic_atomic_exchange(size_t size, void *object, const void *value, void *result, int order) __asm__("__atomic_exchange");
void generic_atomic_exchange(size_t size, void *object, const void *value, void *result, int order)
{
	(void)order;
	generic_atomic_acquire();
	memcpy(result, object, size);
	memcpy(object, value, size);
	generic_atomic_release();
}
#endif
