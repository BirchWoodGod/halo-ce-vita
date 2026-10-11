/* debug_memory_threads_test.c: the game's allocator (source/cseries/
debug_memory.c, every malloc, free and realloc of the game's code through
cseries.h) from several threads at once.

A Vita joining a lobby with a Custom Edition map already in its maps folder
crashed as the map loaded (netns mapmid, v1.1.0-beta.2 and next-1.1): the
cache file thread rebuilt an arriving bitmap's hardware format (a malloc and
a free the size of the bitmap) while the game's thread allocated its arrays,
and the two at once in the allocator's list lost a block from it; its free
then found no previous ("previous" at debug_memory.c #446) and wrote through
NULL. Here threads allocate, fill, check, reallocate and free blocks of
their own as fast as they can, one of them the bitmap's way (megabytes at a
time), another walking and dumping the list as the console's commands do;
each block's bytes are checked before it goes. Every block must come back,
the list must end empty and the heap's size at zero, and no assertion may
fail (here every one is fatal).

Built by run_debug_memory_threads_test.sh with the game's flags, the
allocator included whole. */

#include "../../../source/cseries/debug_memory.c"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* (the C library's own, not the game's names for them: cseries.h and the
port's stdio.h) */
#undef printf
#undef fprintf
#undef vsnprintf
#undef memset
#undef malloc
#undef free
#undef realloc

/* ---------- what the allocator calls */

char temporary[256];

void display_assert(char *information, char *file, long line, boolean fatal)
{
	fprintf(stderr, "FAIL: assertion %s at %s,#%ld%s\n", information ? information : "?", file ? file : "?", line,
		fatal ? "" : " (warning)");
	if (fatal)
		abort();
}

int halo_assert_is_fatal(void)
{
	return 1;
}

void halo_assert_soft(const char *information, const char *file, long line)
{
	display_assert(information, file, line, TRUE);
}

void system_exit(long code)
{
	exit(code);
}

void halt_and_catch_fire(void)
{
	abort();
}

void error(short priority, const char *format, ...)
{
	(void)priority;
	(void)format;
}

void platform_log(const char *format, ...)
{
	(void)format;
}

char *csprintf(char *buffer, char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(buffer, sizeof(temporary), format, arguments);
	va_end(arguments);
	return buffer;
}

void *csmemset(void *destination, long value, unsigned long size)
{
	return __builtin_memset(destination, value, size);
}

void *system_malloc(long size)
{
	return __builtin_malloc(size);
}

void system_free(void *pointer)
{
	__builtin_free(pointer);
}

void *system_realloc(void *pointer, long size)
{
	return __builtin_realloc(pointer, size);
}

/* (a checksum of the header's bytes: crc.c's need not be the one) */
void crc_new(unsigned long *crc_reference)
{
	*crc_reference = 0xFFFFFFFFul;
}

void crc_checksum_buffer(unsigned long *crc_reference, void const *buffer, long buffer_size)
{
	const unsigned char *bytes = buffer;
	long index;

	for (index = 0; index < buffer_size; index++)
		*crc_reference = (*crc_reference ^ bytes[index]) * 16777619ul;
}

static unsigned long random_seed = 1;

unsigned long *get_global_local_random_seed_address(void)
{
	return &random_seed;
}

word seed_random(unsigned long *seed)
{
	*seed = *seed * 1664525ul + 1013904223ul;
	return (word)(*seed >> 16);
}

void WINAPI GlobalMemoryStatus(LPMEMORYSTATUS status)
{
	memset(status, 0, sizeof(*status));
}

/* (the dumps open no file here) */
FILE *halo_linux_fopen(const char *path, const char *mode)
{
	(void)path;
	(void)mode;
	return NULL;
}

int halo_linux_fprintf(FILE *file, const char *format, ...)
{
	(void)file;
	(void)format;
	return 0;
}

/* ---------- the threads */

enum
{
	MAXIMUM_THREADS = 16,
	BLOCKS_PER_THREAD = 48,
	BITMAP_BYTES = 2752512 /* Covenant V Marines' largest bitmap, 1024x512 at 32 bits, its mipmaps */
};

struct block
{
	unsigned char *pointer;
	unsigned long size;
	unsigned char pattern;
};

struct worker
{
	pthread_t thread;
	long index;
	unsigned long seed;
	unsigned long operations;
	unsigned long bad_bytes;
	struct block blocks[BLOCKS_PER_THREAD];
};

static struct worker workers[MAXIMUM_THREADS];
static volatile int stop;

static unsigned long next_random(struct worker *worker)
{
	worker->seed = worker->seed * 1103515245ul + 12345ul;
	return worker->seed >> 8;
}

static void block_fill(struct block *block)
{
	unsigned long index;

	for (index = 0; index < block->size; index += 61)
		block->pointer[index] = block->pattern;
	if (block->size)
		block->pointer[block->size - 1] = block->pattern;
}

static unsigned long block_bad_bytes(const struct block *block, unsigned long size)
{
	unsigned long index, bad = 0;

	for (index = 0; index < size; index += 61)
		bad += block->pointer[index] != block->pattern;
	return bad;
}

static void block_free(struct worker *worker, struct block *block)
{
	unsigned long bad = block_bad_bytes(block, block->size);

	if (block->size)
		bad += block->pointer[block->size - 1] != block->pattern;
	worker->bad_bytes += bad;
	debug_free(block->pointer, "debug_memory_threads_test.c", __LINE__);
	block->pointer = NULL;
}

static void *worker_main(void *parameter)
{
	struct worker *worker = parameter;
	unsigned long index;

	while (!stop)
	{
		struct block *block = &worker->blocks[next_random(worker) % BLOCKS_PER_THREAD];
		unsigned long choice = next_random(worker) % 16;

		/* the cache file thread's way: a bitmap's buffer, swizzled into
		and freed */
		if (worker->index == 0 && choice == 0)
		{
			unsigned char *buffer = debug_malloc(BITMAP_BYTES, FALSE, "rasterizer_swizzle.c", 552);

			if (buffer)
			{
				buffer[0] = buffer[BITMAP_BYTES - 1] = 1;
				debug_free(buffer, "rasterizer_swizzle.c", 629);
			}
		}
		/* the console's walks of the list (a dump checks it inside: the
		lock taken again by its holder) */
		else if (worker->index == 1 && choice == 0)
		{
			debug_check_memory("debug_memory_threads_test.c", __LINE__);
			if (next_random(worker) % 8 == 0)
				debug_dump_memory_by_file();
		}
		else if (!block->pointer)
		{
			block->size = 1 + next_random(worker) % (choice < 2 ? 65536 : 512);
			block->pattern = (unsigned char)(worker->index * 16 + next_random(worker) % 16);
			block->pointer = debug_malloc(block->size, choice & 1, "debug_memory_threads_test.c", __LINE__);
			if (!block->pointer)
			{
				fprintf(stderr, "FAIL: no memory for %lu bytes\n", block->size);
				abort();
			}
			block_fill(block);
		}
		else if (choice < 4)
		{
			unsigned long size = 1 + next_random(worker) % 2048;
			unsigned long kept = size < block->size ? size : block->size;

			worker->bad_bytes += block_bad_bytes(block, kept);
			block->pointer = debug_realloc(block->pointer, size, "debug_memory_threads_test.c", __LINE__);
			block->size = size;
			block_fill(block);
		}
		else
		{
			block_free(worker, block);
		}
		worker->operations++;
	}
	for (index = 0; index < BLOCKS_PER_THREAD; index++)
		if (worker->blocks[index].pointer)
			block_free(worker, &worker->blocks[index]);
	return NULL;
}

int main(int argc, char **argv)
{
	long thread_count = argc > 1 ? atol(argv[1]) : 6;
	double seconds = argc > 2 ? atof(argv[2]) : 3.0;
	unsigned long operations = 0, bad_bytes = 0;
	struct timespec pause;
	long index;

	if (thread_count < 2 || thread_count > MAXIMUM_THREADS)
	{
		fprintf(stderr, "usage: %s [threads 2-%d] [seconds]\n", argv[0], MAXIMUM_THREADS);
		return 2;
	}
	debug_memory_manager_initialize();
	for (index = 0; index < thread_count; index++)
	{
		workers[index].index = index;
		workers[index].seed = 0x9E3779B9ul * (unsigned long)(index + 1) ^ (unsigned long)time(NULL);
		if (pthread_create(&workers[index].thread, NULL, worker_main, &workers[index]) != 0)
		{
			fprintf(stderr, "FAIL: no thread %ld\n", index);
			return 1;
		}
	}
	pause.tv_sec = (time_t)seconds;
	pause.tv_nsec = (long)((seconds - (double)pause.tv_sec) * 1e9);
	nanosleep(&pause, NULL);
	stop = 1;
	for (index = 0; index < thread_count; index++)
	{
		pthread_join(workers[index].thread, NULL);
		operations += workers[index].operations;
		bad_bytes += workers[index].bad_bytes;
	}
	debug_check_memory("debug_memory_threads_test.c", __LINE__);
	if (bad_bytes)
	{
		printf("FAIL: %lu bytes of blocks changed under their owners\n", bad_bytes);
		return 1;
	}
	if (debug_memory_globals.first_pointer || debug_memory_globals.current_heap_size)
	{
		printf("FAIL: every block freed, but the list %s and the heap's size is %ld\n",
			debug_memory_globals.first_pointer ? "still has blocks" : "is empty",
			debug_memory_globals.current_heap_size);
		return 1;
	}
	printf("PASS: %ld threads, %lu allocator calls in %.1f s: every block came back, the list empty\n",
		thread_count, operations, seconds);
	return 0;
}
