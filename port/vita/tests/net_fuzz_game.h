/*
NET_FUZZ_GAME.H

What the game's own code under a network fuzz target calls from the rest of
the game (cseries, errors), here: its memory and string functions as the C
library's, its log printed with NET_FUZZ_LOG set, and a failed assertion
(fatal: DEBUG builds' rule) aborting, so that the fuzzer reports it as a
crash. Included once, after the code under test, by the targets built with
the game's flags (net_fuzz_messages.c, net_fuzz_map_share.c).
*/

#ifndef __NET_FUZZ_GAME_H
#define __NET_FUZZ_GAME_H

/* (the game's headers name these the cseries functions) */
#define memcpy __builtin_memcpy
#define memset __builtin_memset
#define memmove __builtin_memmove
#define memcmp __builtin_memcmp
#define vsnprintf __builtin_vsnprintf
#define abort __builtin_abort

char temporary[256];

void *csmemcpy(void *destination, const void *source, unsigned long size) { return memcpy(destination, source, size); }
void *csmemset(void *buffer, long c, unsigned long size) { return memset(buffer, (int)c, size); }
void *csmemmove(void *destination, const void *source, unsigned long size) { return memmove(destination, source, size); }
long csmemcmp(const void *p1, const void *p2, unsigned long size) { return memcmp(p1, p2, size); }
char *csstrncpy(char *s1, const char *s2, unsigned long size)
{
	unsigned long index;

	for (index = 0; index < size && s2[index]; index++)
		s1[index] = s2[index];
	for (; index < size; index++)
		s1[index] = 0;
	return s1;
}
char *csstrcpy(char *destination, const char *source)
{
	char *result = destination;

	while ((*destination++ = *source++))
		;
	return result;
}

int halo_linux_vsnprintf(char *buffer, size_t count, const char *format, va_list arguments)
{
	return vsnprintf(buffer, count, format, arguments);
}

char *csprintf(char *buffer, char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(buffer, 256, format, arguments);
	va_end(arguments);
	return buffer;
}

char *getenv(const char *name);

void error(short priority, const char *format, ...)
{
	char line[1024];
	va_list arguments;

	(void)priority;
	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	if (getenv("NET_FUZZ_LOG"))
		__builtin_fprintf(stderr, "  [error] %s\n", line);
}

int halo_assert_is_fatal(void) { return 1; }
void halo_assert_soft(const char *information, const char *file, long line) { (void)information; (void)file; (void)line; }
void display_assert(char *information, char *file, long line, boolean fatal)
{
	__builtin_fprintf(stderr, "assertion failed: %s (%s:%ld)\n", information ? information : "", file, line);
	if (fatal)
		abort();
}
void system_exit(long code)
{
	(void)code;
	abort();
}
void halt_and_catch_fire(void)
{
	abort();
}

void *debug_malloc(unsigned int size, boolean clear, const char *file, long line)
{
	void *result = __builtin_malloc(size);

	(void)file; (void)line;
	if (result && clear)
		memset(result, 0, size);
	return result;
}
void debug_free(void *pointer, const char *file, long line)
{
	(void)file; (void)line;
	__builtin_free(pointer);
}

#endif
