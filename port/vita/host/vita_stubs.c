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
xbox_kernel.c gives the game's own threads theirs explicitly */
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

/* ---------- generic atomics (libatomic) for the game's unaligned fields

clang, told nothing is aligned (-fmax-type-align=1), turns an atomic on a
short in a packed structure into these library calls. One lock serves
them all; only a light's flags word uses them (object_lights.c). */

#include <stdbool.h>
#include <string.h>

static volatile int generic_atomic_lock;

static void generic_atomic_acquire(void)
{
	while (__atomic_exchange_n(&generic_atomic_lock, 1, __ATOMIC_ACQUIRE))
		;
}

static void generic_atomic_release(void)
{
	__atomic_store_n(&generic_atomic_lock, 0, __ATOMIC_RELEASE);
}

bool __atomic_compare_exchange(size_t size, void *object, void *expected, const void *desired, int success, int failure)
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

void __atomic_load(size_t size, const void *object, void *result, int order)
{
	(void)order;
	generic_atomic_acquire();
	memcpy(result, object, size);
	generic_atomic_release();
}

void __atomic_store(size_t size, void *object, const void *value, int order)
{
	(void)order;
	generic_atomic_acquire();
	memcpy(object, value, size);
	generic_atomic_release();
}

void __atomic_exchange(size_t size, void *object, const void *value, void *result, int order)
{
	(void)order;
	generic_atomic_acquire();
	memcpy(result, object, size);
	memcpy(object, value, size);
	generic_atomic_release();
}
