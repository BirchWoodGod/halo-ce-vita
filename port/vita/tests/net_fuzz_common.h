/*
NET_FUZZ_COMMON.H

What the network fuzz targets (net_fuzz_*.c) share: the platform layer's
functions that internet play calls (posix_*, config_*, platform_log), here
as stand-ins with no sockets and a clock the target moves, so that each
input runs the same way every time. Included once, after the code under
test, in each target (they are built as the game is: the game's ABI, with
HALO_VITA; run_net_fuzz_test.sh).

The stand-in sockets: sendto and send succeed and keep what they sent last
(net_fuzz_sent), recvfrom and recv return what a target queued
(net_fuzz_queue_receive) and else would block; select says nothing is
ready.
*/

#ifndef __NET_FUZZ_COMMON_H
#define __NET_FUZZ_COMMON_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- the clock */

static unsigned long net_fuzz_clock = 1000000;

__attribute__((stdcall)) unsigned long GetTickCount(void)
{
	return net_fuzz_clock;
}

__attribute__((stdcall)) void Sleep(unsigned long milliseconds)
{
	net_fuzz_clock += milliseconds;
}

/* ---------- the log: printed with NET_FUZZ_LOG set, and always checked
to end (a peer's text must never be a format) */

static int net_fuzz_log_lines;

void platform_log(const char *format, ...)
{
	char line[2048];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	net_fuzz_log_lines++;
	if (getenv("NET_FUZZ_LOG"))
		fprintf(stderr, "  [log] %s\n", line);
}

const char *platform_data_root(void)
{
	return "/nonexistent";
}

/* ---------- settings */

static const char *net_fuzz_brokers = "broker.test:1883";
static const char *net_fuzz_stun = "stun.test:3478";

int config_boolean(const char *name)
{
	return !strcmp(name, "network.online");
}

long config_integer(const char *name)
{
	(void)name;
	return 0;
}

double config_real(const char *name)
{
	(void)name;
	return 0.0;
}

const char *config_string(const char *name)
{
	if (!strcmp(name, "network.signalling_brokers"))
		return net_fuzz_brokers;
	if (!strcmp(name, "network.stun_servers"))
		return net_fuzz_stun;
	return "";
}

/* ---------- randomness: a counter, so every run is the same */

static unsigned long long net_fuzz_random_state = 0x9E3779B97F4A7C15ULL;

void posix_random_bytes(void *buffer, posix_ulong size)
{
	unsigned char *bytes = buffer;
	posix_ulong index;

	for (index = 0; index < size; index++)
	{
		net_fuzz_random_state = net_fuzz_random_state * 6364136223846793005ULL + 1442695040888963407ULL;
		bytes[index] = (unsigned char)(net_fuzz_random_state >> 56);
	}
}

/* ---------- sockets */

enum
{
	NET_FUZZ_QUEUE_SIZE = 8,
	NET_FUZZ_PACKET_SIZE = 4096,
};

static struct
{
	unsigned char data[NET_FUZZ_PACKET_SIZE];
	int size;
	int socket;
	unsigned long address;
	unsigned short port;
} net_fuzz_queue[NET_FUZZ_QUEUE_SIZE];
static int net_fuzz_queue_count;
static int net_fuzz_next_socket = 100;
static unsigned short net_fuzz_next_port = 40000;
static unsigned char net_fuzz_sent[NET_FUZZ_PACKET_SIZE];
static int net_fuzz_sent_size;
static int net_fuzz_sent_count;
static int net_fuzz_last_error;

/* what recvfrom/recv on socket return next (socket -1: any) */
static void net_fuzz_queue_receive(int socket, const void *data, int size, unsigned long address, unsigned short port)
{
	if (net_fuzz_queue_count >= NET_FUZZ_QUEUE_SIZE || size < 0 || size > NET_FUZZ_PACKET_SIZE)
		return;
	memcpy(net_fuzz_queue[net_fuzz_queue_count].data, data, (size_t)size);
	net_fuzz_queue[net_fuzz_queue_count].size = size;
	net_fuzz_queue[net_fuzz_queue_count].socket = socket;
	net_fuzz_queue[net_fuzz_queue_count].address = address;
	net_fuzz_queue[net_fuzz_queue_count].port = port;
	net_fuzz_queue_count++;
}

static int net_fuzz_dequeue(int socket, void *buffer, int length, void *address, int *address_length)
{
	int index;

	for (index = 0; index < net_fuzz_queue_count; index++)
	{
		if (net_fuzz_queue[index].socket == socket || net_fuzz_queue[index].socket < 0)
		{
			int size = net_fuzz_queue[index].size < length ? net_fuzz_queue[index].size : length;

			memcpy(buffer, net_fuzz_queue[index].data, (size_t)size);
			if (address && address_length && *address_length >= (int)sizeof(struct sockaddr_in))
			{
				struct sockaddr_in *from = address;

				memset(from, 0, sizeof(*from));
				from->sin_family = AF_INET;
				from->sin_addr.s_addr = net_fuzz_queue[index].address;
				from->sin_port = net_fuzz_queue[index].port;
				*address_length = sizeof(*from);
			}
			memmove(&net_fuzz_queue[index], &net_fuzz_queue[index + 1],
				(size_t)(net_fuzz_queue_count - index - 1) * sizeof(net_fuzz_queue[0]));
			net_fuzz_queue_count--;
			return size;
		}
	}
	net_fuzz_last_error = WSAEWOULDBLOCK;
	return -1;
}

int posix_socket_last_error(void) { return net_fuzz_last_error; }
int posix_socket(int family, int type, int protocol) { (void)family; (void)type; (void)protocol; return net_fuzz_next_socket++; }
int posix_socket_close(int socket) { (void)socket; return 0; }
int posix_socket_bind(int socket, const void *address, int address_length) { (void)socket; (void)address; (void)address_length; return 0; }
int posix_socket_connect(int socket, const void *address, int address_length)
{
	(void)socket; (void)address; (void)address_length;
	net_fuzz_last_error = WSAEWOULDBLOCK;
	return -1;
}
int posix_socket_listen(int socket, int backlog) { (void)socket; (void)backlog; return 0; }
int posix_socket_accept(int socket, void *address, int *address_length)
{
	(void)socket; (void)address; (void)address_length;
	net_fuzz_last_error = WSAEWOULDBLOCK;
	return -1;
}
int posix_socket_send(int socket, const void *buffer, int length, int flags)
{
	(void)socket; (void)flags;
	net_fuzz_sent_size = length < NET_FUZZ_PACKET_SIZE ? length : NET_FUZZ_PACKET_SIZE;
	memcpy(net_fuzz_sent, buffer, (size_t)net_fuzz_sent_size);
	net_fuzz_sent_count++;
	return length;
}
int posix_socket_sendto(int socket, const void *buffer, int length, int flags, const void *address,
	int address_length)
{
	(void)address; (void)address_length;
	return posix_socket_send(socket, buffer, length, flags);
}
int posix_socket_recv(int socket, void *buffer, int length, int flags)
{
	(void)flags;
	return net_fuzz_dequeue(socket, buffer, length, NULL, NULL);
}
int posix_socket_recvfrom(int socket, void *buffer, int length, int flags, void *address, int *address_length)
{
	(void)flags;
	return net_fuzz_dequeue(socket, buffer, length, address, address_length);
}
int posix_socket_set_nonblocking(int socket, int nonblocking) { (void)socket; (void)nonblocking; return 0; }
int posix_socket_set_nodelay(int socket) { (void)socket; return 0; }
int posix_socket_setsockopt(int socket, int level, int name, const void *value, int length)
{
	(void)socket; (void)level; (void)name; (void)value; (void)length;
	return 0;
}
int posix_socket_getsockname(int socket, void *address, int *address_length)
{
	struct sockaddr_in *bound = address;

	(void)socket;
	memset(bound, 0, sizeof(*bound));
	bound->sin_family = AF_INET;
	bound->sin_port = (unsigned short)((net_fuzz_next_port >> 8) | (net_fuzz_next_port << 8));
	net_fuzz_next_port++;
	*address_length = sizeof(*bound);
	return 0;
}
int posix_socket_select(int *read, int *read_count, int *write, int *write_count, int *error, int *error_count,
	posix_long seconds, posix_long microseconds, int unused)
{
	(void)read; (void)write; (void)error; (void)seconds; (void)microseconds; (void)unused;
	*read_count = 0;
	*write_count = 0;
	if (error_count)
		*error_count = 0;
	return 0;
}
posix_ulong posix_local_ipv4_address(void) { return 0x0A00000A; }
posix_ulong posix_resolve_ipv4(const char *host) { (void)host; return 0x01020304; }
void posix_resolve_error(char *text, int size) { snprintf(text, (size_t)size, "fuzz"); }
int posix_socket_getsockopt(int socket, int level, int name, void *value, int *length)
{
	(void)socket; (void)level; (void)name; (void)value; (void)length;
	return -1;
}
int config_file_write(const char *path, const char *text, size_t size) { (void)path; (void)text; (void)size; return 1; }
int posix_command_line_argument(int index, char *buffer, posix_ulong size) { (void)index; (void)buffer; (void)size; return 0; }
int posix_register_url_scheme(const char *scheme, const char *description) { (void)scheme; (void)description; return 0; }
int posix_upnp_forward_udp(unsigned short port, unsigned short preferred_port, posix_ulong *external_address,
	unsigned short *external_port, char *error, int error_size)
{
	(void)port; (void)preferred_port; (void)external_address; (void)external_port;
	snprintf(error, (size_t)error_size, "none");
	return 0;
}
void posix_upnp_stop_forwarding_udp(unsigned short external_port) { (void)external_port; }
int posix_user_secret(unsigned char *secret, int size) { memset(secret, 7, (size_t)size); return 1; }
int posix_hardware_id_source(char *text, int size) { snprintf(text, (size_t)size, "net-fuzz"); return 1; }

#endif
