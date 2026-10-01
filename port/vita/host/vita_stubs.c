/*
VITA_STUBS.C

What the Linux platform layer asks of the host (port/linux/src/posix.h) and
the Vita does without: networking (sockets fail as if no network were
up), UPnP, the desktop's process and link handling, Discord. Files are
port/linux/src/posix_files.c itself, built here with the SDK's ABI.
*/

#include <psp2/kernel/rng.h>
#include <psp2/kernel/threadmgr.h>
#include <unistd.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "posix.h"
#include "vita_compat.h"

/* WSAENETDOWN */
#define NO_NETWORK 10050

int posix_socket_last_error(void) { return NO_NETWORK; }
int posix_socket(int family, int type, int protocol) { (void)family; (void)type; (void)protocol; return -1; }
int posix_socket_close(int socket) { (void)socket; return -1; }
int posix_socket_bind(int socket, const void *address, int address_length) { (void)socket; (void)address; (void)address_length; return -1; }
int posix_socket_connect(int socket, const void *address, int address_length) { (void)socket; (void)address; (void)address_length; return -1; }
int posix_socket_listen(int socket, int backlog) { (void)socket; (void)backlog; return -1; }
int posix_socket_accept(int socket, void *address, int *address_length) { (void)socket; (void)address; (void)address_length; return -1; }
int posix_socket_send(int socket, const void *buffer, int length, int flags) { (void)socket; (void)buffer; (void)length; (void)flags; return -1; }
int posix_socket_sendto(int socket, const void *buffer, int length, int flags, const void *address, int address_length)
{ (void)socket; (void)buffer; (void)length; (void)flags; (void)address; (void)address_length; return -1; }
int posix_socket_recv(int socket, void *buffer, int length, int flags) { (void)socket; (void)buffer; (void)length; (void)flags; return -1; }
int posix_socket_recvfrom(int socket, void *buffer, int length, int flags, void *address, int *address_length)
{ (void)socket; (void)buffer; (void)length; (void)flags; (void)address; (void)address_length; return -1; }
int posix_socket_shutdown(int socket, int how) { (void)socket; (void)how; return -1; }
int posix_socket_set_nonblocking(int socket, int nonblocking) { (void)socket; (void)nonblocking; return -1; }
int posix_socket_bytes_available(int socket, posix_ulong *count) { (void)socket; *count = 0; return -1; }
int posix_socket_set_nodelay(int socket) { (void)socket; return -1; }
int posix_socket_setsockopt(int socket, int level, int name, const void *value, int length)
{ (void)socket; (void)level; (void)name; (void)value; (void)length; return -1; }
int posix_socket_getsockopt(int socket, int level, int name, void *value, int *length)
{ (void)socket; (void)level; (void)name; (void)value; (void)length; return -1; }
int posix_socket_getsockname(int socket, void *address, int *address_length) { (void)socket; (void)address; (void)address_length; return -1; }
int posix_socket_getpeername(int socket, void *address, int *address_length) { (void)socket; (void)address; (void)address_length; return -1; }

int posix_socket_select(int *read, int *read_count, int *write, int *write_count,
	int *error, int *error_count, posix_long timeout_seconds, posix_long timeout_microseconds, int infinite)
{
	(void)read; (void)write; (void)error; (void)timeout_seconds; (void)timeout_microseconds; (void)infinite;
	*read_count = *write_count = *error_count = 0;
	return -1;
}

posix_ulong posix_local_ipv4_address(void) { return 0; }
posix_ulong posix_resolve_ipv4(const char *host) { (void)host; return 0; }

void posix_random_bytes(void *buffer, posix_ulong size)
{
	unsigned char *bytes = buffer;

	while (size)
	{
		unsigned int value;
		posix_ulong count = size < sizeof(value) ? size : sizeof(value);

		sceKernelGetRandomNumber(&value, sizeof(value));
		memcpy(bytes, &value, count);
		bytes += count;
		size -= count;
	}
}

int posix_upnp_forward_udp(unsigned short port, posix_ulong *external_address, unsigned short *external_port,
	char *error, int error_size)
{
	(void)port; (void)external_address; (void)external_port;
	if (error_size > 0)
	{
		strncpy(error, "no UPnP on the Vita", (size_t)error_size - 1);
		error[error_size - 1] = 0;
	}
	return 0;
}

void posix_upnp_stop_forwarding_udp(unsigned short external_port) { (void)external_port; }
int posix_command_line_argument(int index, char *buffer, posix_ulong size) { (void)index; (void)buffer; (void)size; return 0; }
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
