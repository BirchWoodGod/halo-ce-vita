/*
VITA_STUBS.C

What the Linux platform layer asks of the host (port/linux/src/posix.h) and
the Vita does without: UPnP, the desktop's process and link handling,
Discord. Sockets are vita_net.c; files are port/linux/src/posix_files.c
itself, built here with the SDK's ABI.
*/

#include <psp2/kernel/rng.h>
#include <psp2/kernel/openpsid.h>
#include <stdio.h>
#include <psp2/kernel/threadmgr.h>
#include <unistd.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "posix.h"
#include "vita_compat.h"

/* (sockets: vita_net.c) */

/* the stack of a thread made with no attributes (pthread_create(&thread,
NULL, ...)): the SDK's pthread library takes this in place of its 32 KB
minimum. Internet play's threads (p2p.c's, which keeps two 754-socket
lists on its stack and selects over up to 128 sockets, its UPnP thread,
and ad hoc play's two in p2p_adhoc.c) were made for desktops' megabytes;
xbox_kernel.c gives the game's own threads theirs explicitly. One made with
attributes takes their size alone, and with none set (0) gets the library's
4 KiB minimum, not this: every pthread_attr_init before a pthread_create
needs its pthread_attr_setstacksize (the friend code backup's thread had
none in insider.21, and Argon2id overflowed it) */
unsigned int _pthread_stack_default_user = 256 * 1024;

void posix_random_bytes(void *buffer, posix_ulong size)
{
	unsigned char *bytes = buffer;

	while (size)
	{
		unsigned int value;
		posix_ulong count = size < sizeof(value) ? size : sizeof(value);

		if (sceKernelGetRandomNumber(&value, sizeof(value)) < 0)
			abort();
		memcpy(bytes, &value, count);
		bytes += count;
		size -= count;
	}
}

/* Only p2p_hardware_id's game-specific HMAC goes onto the wire. */
int posix_hardware_id_source(char *text, int size)
{
    SceKernelOpenPsId id;
    int index;
    if (size <= 0)
        return 0;
    text[0] = 0;
    if (size < 33 || sceKernelGetOpenPsId(&id) < 0)
        return 0;
    for (index = 0; index < 16; ++index)
        snprintf(text + index * 2, 3, "%02x", (unsigned char)id.id[index]);
    return 1;
}

int posix_upnp_forward_udp(unsigned short port, unsigned short preferred_port, posix_ulong *external_address, unsigned short *external_port,
	char *error, int error_size)
{
	(void)port; (void)preferred_port; (void)external_address; (void)external_port;
	if (error_size > 0)
	{
		strncpy(error, "no UPnP on the Vita", (size_t)error_size - 1);
		error[error_size - 1] = 0;
	}
	return 0;
}

void posix_upnp_stop_forwarding_udp(unsigned short external_port) { (void)external_port; }
int posix_command_line_argument(int index, char *buffer, posix_ulong size) { (void)index; (void)buffer; (void)size; return 0; }
int posix_user_secret(unsigned char *secret, int size) { (void)secret; (void)size; return 0; }
posix_ulong posix_process_id(void) { return 1; }
int posix_register_url_scheme(const char *scheme, const char *description) { (void)scheme; (void)description; return 0; }
int posix_discord_connect(void) { return -1; }
int posix_discord_write(int handle, const void *buffer, int length) { (void)handle; (void)buffer; (void)length; return -1; }
int posix_discord_read(int handle, void *buffer, int length) { (void)handle; (void)buffer; (void)length; return -1; }
void posix_discord_close(int handle) { (void)handle; }

/* newlib has no utimensat (posix_files.c sets file times with it): the
Vita keeps the times it sets itself */
int utimensat(int directory, const char *path, const struct timespec times[2], int flags)
{
	(void)directory; (void)path; (void)times; (void)flags;
	return 0;
}

/* nor clock_nanosleep (dsound_sdl.c paces its mixer with it) */
int clock_nanosleep(clockid_t clock, int flags, const struct timespec *request, struct timespec *remain)
{
	struct timespec now, duration;

	(void)remain;
	if (!(flags & TIMER_ABSTIME))
		return nanosleep(request, NULL) == 0 ? 0 : errno;
	clock_gettime(clock, &now);
	duration.tv_sec = request->tv_sec - now.tv_sec;
	duration.tv_nsec = request->tv_nsec - now.tv_nsec;
	if (duration.tv_nsec < 0)
	{
		duration.tv_nsec += 1000000000L;
		duration.tv_sec--;
	}
	if (duration.tv_sec < 0)
		return 0;
	return nanosleep(&duration, NULL) == 0 ? 0 : errno;
}

/* xbox_files.c looks for the executable's folder through /proc; the Vita's
data root comes from HALO_DATA_ROOT (vita_main.c) */
ssize_t readlink(const char *path, char *buffer, size_t size)
{
	(void)path; (void)buffer; (void)size;
	errno = ENOSYS;
	return -1;
}

/* Sleep(INFINITE) (xbox_kernel.c) */
int pause(void)
{
	for (;;)
		sceKernelDelayThread(1000000);
	return -1;
}

/* ---------- generic atomics (libatomic)

clang, told nothing is aligned (-fmax-type-align=1), turns an atomic on any
field it cannot prove aligned - a structure's member reached through a
pointer, not only a short in a packed structure - into these library calls:
some 90 in the game (d3d8_gxm.c's waits, the scene packs, the texture
decoder, the stream reads, the lights...), on words that other code reaches
with the processor's own atomics (ldrex/strex, plain aligned loads and
stores). A lock and a byte copy were atomic only against each other: an
exchange here and an inline increment on the same word could lose either
(the render worker's sleepers), and a byte-wise copy could be read torn.

So a naturally aligned object of 1, 2, 4 or 8 bytes - every cross-thread
field there is - takes the processor's atomic of its size (this file is
GCC's, which assumes the alignment of the types below); only a truly
unaligned one (a packed structure's short) takes the lock, as before, and
is atomic against the lock's other users only. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t atomic_8;
typedef uint16_t atomic_16 __attribute__((aligned(2)));
typedef uint32_t atomic_32 __attribute__((aligned(4)));
typedef uint64_t atomic_64 __attribute__((aligned(8)));

static volatile int generic_atomic_lock;

void vita_host_log(const char *line);

/* (the first few objects that take the lock, named: a packed field the
game's other code may reach with the processor's atomics, which the lock
does not exclude) */
static void generic_atomic_unaligned(size_t size, const volatile void *object)
{
	static volatile int logged;

	if (__atomic_add_fetch(&logged, 1, __ATOMIC_RELAXED) <= 4)
	{
		char line[128];

		snprintf(line, sizeof(line), "atomics: a %u-byte object at %p not aligned to its size: under the lock",
			(unsigned)size, (const void *)object);
		vita_host_log(line);
	}
}

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

/* the object's own size, naturally aligned: the processor's atomics */
static int generic_atomic_native(size_t size, const volatile void *object)
{
	return (size == 1 || size == 2 || size == 4 || size == 8) && ((uintptr_t)object & (size - 1)) == 0;
}

/* (the native ones, by size: `type` the size's, a and b its values) */
#define GENERIC_ATOMIC_BY_SIZE(size, operation) \
	switch (size) \
	{ \
	case 1: operation(atomic_8); break; \
	case 2: operation(atomic_16); break; \
	case 4: operation(atomic_32); break; \
	default: operation(atomic_64); break; \
	}

bool __atomic_compare_exchange(size_t size, void *object, void *expected, const void *desired, int success, int failure)
{
	bool matched;

	(void)success;
	(void)failure;
	if (generic_atomic_native(size, object))
	{
#define COMPARE_EXCHANGE(type) \
		{ \
			type was, want; \
			memcpy(&was, expected, sizeof(type)); \
			memcpy(&want, desired, sizeof(type)); \
			matched = __atomic_compare_exchange_n((type *)object, &was, want, false, __ATOMIC_SEQ_CST, \
				__ATOMIC_SEQ_CST); \
			if (!matched) \
				memcpy(expected, &was, sizeof(type)); \
		}
		GENERIC_ATOMIC_BY_SIZE(size, COMPARE_EXCHANGE)
#undef COMPARE_EXCHANGE
		return matched;
	}
	generic_atomic_unaligned(size, object);
	generic_atomic_acquire();
	matched = memcmp(object, expected, size) == 0;
	if (matched)
		memcpy(object, desired, size);
	else
		memcpy(expected, object, size);
	generic_atomic_release();
	return matched;
}

void __atomic_load(size_t size, const void *object, void *result, int order)
{
	(void)order;
	if (generic_atomic_native(size, object))
	{
#define LOAD(type) \
		{ \
			type value = __atomic_load_n((const type *)object, __ATOMIC_SEQ_CST); \
			memcpy(result, &value, sizeof(type)); \
		}
		GENERIC_ATOMIC_BY_SIZE(size, LOAD)
#undef LOAD
		return;
	}
	generic_atomic_unaligned(size, object);
	generic_atomic_acquire();
	memcpy(result, object, size);
	generic_atomic_release();
}

void __atomic_store(size_t size, void *object, const void *value, int order)
{
	(void)order;
	if (generic_atomic_native(size, object))
	{
#define STORE(type) \
		{ \
			type stored; \
			memcpy(&stored, value, sizeof(type)); \
			__atomic_store_n((type *)object, stored, __ATOMIC_SEQ_CST); \
		}
		GENERIC_ATOMIC_BY_SIZE(size, STORE)
#undef STORE
		return;
	}
	generic_atomic_unaligned(size, object);
	generic_atomic_acquire();
	memcpy(object, value, size);
	generic_atomic_release();
}

void __atomic_exchange(size_t size, void *object, const void *value, void *result, int order)
{
	(void)order;
	if (generic_atomic_native(size, object))
	{
#define EXCHANGE(type) \
		{ \
			type stored, was; \
			memcpy(&stored, value, sizeof(type)); \
			was = __atomic_exchange_n((type *)object, stored, __ATOMIC_SEQ_CST); \
			memcpy(result, &was, sizeof(type)); \
		}
		GENERIC_ATOMIC_BY_SIZE(size, EXCHANGE)
#undef EXCHANGE
		return;
	}
	generic_atomic_unaligned(size, object);
	generic_atomic_acquire();
	memcpy(result, object, size);
	memcpy(object, value, size);
	generic_atomic_release();
}
