/*
VITA_NET.C

The platform layer's sockets (port/linux/src/posix.h) on the Vita's own
network library. The game's network code is Winsock's, and split screen
already needs it: its players join a server the game runs on the loopback
address, and with no sockets the server came up half made and crashed on
the first update (network_game_server_handle_client_machines). System link
on a Wi-Fi network goes through the same calls.

What differs from posix_net.c: the library starts on first use
(SCE_SYSMODULE_NET, sceNetInit, sceNetCtlInit); its errors are negative
codes whose low byte is the BSD errno, which Winsock's error numbers are
10000 more than; a SceNetSockaddrIn starts with a length byte where the
game's (Winsock's) address has a 16-bit family; there is no select, so
select is a one-shot epoll; and the stack is BSD's, whose connected
datagram sockets refuse a sendto naming a destination (posix_socket_sendto).
*/

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/pspnet_adhoc.h>
#include <psp2/pspnet_adhocctl.h>
#include <psp2/sysmodule.h>

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "posix.h"
#include "vita_host.h"

#define WSAEINVAL 10022
#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036
#define WSAENOPROTOOPT 10042
#define WSAENETDOWN 10050
#define WSAECONNRESET 10054
#define WSAENOBUFS 10055
#define WSAEISCONN 10056
#define WSANOTINITIALISED 10093

/* the network stack's pool holds every socket's buffers: the game's
endpoints ask for 256 KB each (transport_endpoint_winsock.c) - with 1 MB
the request failed (WSAENOBUFS), the sockets kept their small default
buffers, and in a match the host's connection to its own client filled
up and timed out: the player could not move and the host went down */
#define NET_MEMORY_SIZE (8 * 1024 * 1024)
#define SELECT_MAXIMUM 64
/* the library's socket identifiers are 0 to SCE_NET_ID_SOCKET_MAX (1023) */
#define SOCKET_IDENTIFIERS 1024

static __thread int last_error;
static int net_state; /* 0 not tried, 1 up, -1 failed */

/* ad hoc play (below): 0 not in a group; 1 joining one (the system dialog
is up); 2 in a group, whose address this machine has there; -1 the last
attempt failed */
static volatile int adhoc_state;
static SceNetEtherAddr adhoc_address;

static int net_ready(void)
{
	if (net_state == 0)
	{
		SceNetInitParam parameters;
		int result;

		net_state = -1;
		if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) < 0)
		{
			vita_host_log("net: the network module does not load");
			return 0;
		}
		memset(&parameters, 0, sizeof(parameters));
		parameters.memory = malloc(NET_MEMORY_SIZE);
		parameters.size = NET_MEMORY_SIZE;
		result = sceNetInit(&parameters);
		/* (already up: another part of the process started it) */
		if (result < 0 && (unsigned int)result != 0x80410110u /* SCE_NET_ERROR_EBUSY */)
		{
			vita_host_log("net: sceNetInit failed");
			return 0;
		}
		sceNetCtlInit();
		net_state = 1;
		vita_host_log("net: up");
	}
	return net_state > 0;
}

/* a library error (0x80410100 | the BSD errno) as Winsock's: Winsock's
socket errors are the BSD numbers plus 10000 (WSAEWOULDBLOCK 10035 ...
WSAEHOSTUNREACH 10065, and WSAEBADF, WSAEACCES, WSAEFAULT, WSAEINVAL,
WSAEMFILE below them); the library's own codes and the errors Winsock has
no number for are what Winsock says in their place */
static int winsock_error(int result)
{
	unsigned int code = (unsigned int)result;

	if ((code & 0xffffff00u) != 0x80410100u)
		return WSAENETDOWN;
	switch (code & 0xff)
	{
	case 12: /* ENOMEM */
	case 0xc9: /* ENOLIBMEM: the network pool is full */
		return WSAENOBUFS;
	case 32: /* EPIPE: a send to a connection the peer has closed */
		return WSAECONNRESET;
	case 0xc8: /* ENOTINIT */
		return WSANOTINITIALISED;
	default:
		return (code & 0xff) < 0x80 ? 10000 + (int)(code & 0xff) : WSAENETDOWN;
	}
}

/* a library result as the platform layer's: -1 with the Winsock error */
static int answer(int result)
{
	if (result < 0)
	{
		last_error = winsock_error(result);
		return -1;
	}
	last_error = 0;
	return result;
}

/* the connected datagram sockets whose peer a sendto has named
(posix_socket_sendto), with that peer; forgotten when the identifier is
made, connected again or closed */
static unsigned char connected_datagram[SOCKET_IDENTIFIERS];
static unsigned int connected_peer_address[SOCKET_IDENTIFIERS];
static unsigned short connected_peer_port[SOCKET_IDENTIFIERS];

static void forget_peer(int socket)
{
	if (socket >= 0 && socket < SOCKET_IDENTIFIERS)
		connected_datagram[socket] = 0;
}

static unsigned int address_to_vita(const void *address, int length, SceNetSockaddrIn *out)
{
	const unsigned char *bytes = address;
	unsigned short family;

	memset(out, 0, sizeof(*out));
	if (!address || length < 8)
		return 0;
	memcpy(&family, bytes, 2);
	out->sin_len = sizeof(*out);
	out->sin_family = (unsigned char)family;
	memcpy(&out->sin_port, bytes + 2, 2);
	memcpy(&out->sin_addr, bytes + 4, 4);
	return sizeof(*out);
}

static void address_from_vita(const SceNetSockaddrIn *in, void *address, int *length)
{
	unsigned char bytes[16];
	unsigned short family = in->sin_family;

	if (!address || !length)
		return;
	memset(bytes, 0, sizeof(bytes));
	memcpy(bytes, &family, 2);
	memcpy(bytes + 2, &in->sin_port, 2);
	memcpy(bytes + 4, &in->sin_addr, 4);
	memcpy(address, bytes, *length < (int)sizeof(bytes) ? (size_t)*length : sizeof(bytes));
	if (*length > (int)sizeof(bytes))
		*length = sizeof(bytes);
}

/* ---------- the trace (HALO_NET_TRACE=1 in env.txt)

What the game's sockets do, in halo.log ("net trace:" lines), to tell from
one run on the console which call fails or which traffic never arrives:

- each socket made (stream or datagram), bound, connected, listening,
  accepted and closed, with the addresses and the results;
- the options the game sets (buffer sizes, broadcast, non-blocking) and
  the sizes it reads back;
- the first datagrams each socket sends and receives, with the address;
- the first failing calls, with the library's raw error and the Winsock
  number the game sees (would-blocks are counted, and the first ones of
  sends logged: a full connection);
- every two seconds, each socket's counts since it was made: calls,
  successes, bytes, would-blocks, failures by Winsock number, connections
  closed, sendtos sent as sends; the selects'; and the network pool's free
  memory (sceNetGetStatisticsInfo). */

#define TRACE_CODES 4
#define TRACE_FAILURES_LOGGED 64
#define TRACE_SEND_BLOCKS_LOGGED 16
#define TRACE_DATAGRAMS_LOGGED 4
#define TRACE_PERIOD_US 2000000ULL

enum
{
	TRACE_SEND,
	TRACE_RECEIVE,
};

struct trace_counts
{
	unsigned int calls, ok, blocked, failed, closed, as_send;
	unsigned long long bytes;
	int codes[TRACE_CODES];
	unsigned int code_counts[TRACE_CODES];
};

struct trace_socket
{
	int type;
	char local[24], peer[24];
	struct trace_counts counts[2];
	unsigned int datagrams_logged[2];
	unsigned int reported_calls;
};

static int trace_state = -1; /* -1 not read yet */
static pthread_mutex_t trace_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct trace_socket *trace_sockets[SOCKET_IDENTIFIERS];
static unsigned int trace_failures, trace_send_blocks;
static unsigned long long trace_last_report;
static struct
{
	unsigned int calls, ready, empty, failed, epoll_failed;
} trace_selects;

static int trace_on(void)
{
	if (trace_state < 0)
	{
		const char *setting = getenv("HALO_NET_TRACE");

		trace_state = setting && atoi(setting) > 0;
		if (trace_state)
			vita_host_log("net trace: on");
	}
	return trace_state;
}

static void trace_log(const char *format, ...)
{
	char line[320];
	va_list arguments;
	int length = snprintf(line, sizeof(line), "net trace: ");

	va_start(arguments, format);
	vsnprintf(line + length, sizeof(line) - (size_t)length, format, arguments);
	va_end(arguments);
	vita_host_log(line);
}

/* an address in network byte order as text */
static void trace_address_text(char *text, size_t size, unsigned int address, unsigned short port)
{
	snprintf(text, size, "%u.%u.%u.%u:%u", address & 0xff, (address >> 8) & 0xff, (address >> 16) & 0xff,
		address >> 24, (unsigned int)(unsigned short)((port >> 8) | (port << 8)));
}

static void trace_vita_address_text(char *text, size_t size, const SceNetSockaddrIn *address)
{
	if (address)
		trace_address_text(text, size, address->sin_addr.s_addr, address->sin_port);
	else
		snprintf(text, size, "-");
}

static const char *trace_type_name(int type)
{
	return type == SCE_NET_SOCK_STREAM ? "tcp" : type == SCE_NET_SOCK_DGRAM ? "udp" : "?";
}

/* the socket's record (made if there is none), under trace_mutex */
static struct trace_socket *trace_socket_of(int socket)
{
	if (socket < 0 || socket >= SOCKET_IDENTIFIERS)
		return NULL;
	if (!trace_sockets[socket])
	{
		trace_sockets[socket] = calloc(1, sizeof(struct trace_socket));
		if (trace_sockets[socket])
		{
			strcpy(trace_sockets[socket]->local, "-");
			strcpy(trace_sockets[socket]->peer, "-");
		}
	}
	return trace_sockets[socket];
}

/* the socket's addresses, as the library has them now, under trace_mutex */
static void trace_refresh_record(int socket, struct trace_socket *record)
{
	SceNetSockaddrIn local, peer;
	unsigned int local_length = sizeof(local), peer_length = sizeof(peer);

	memset(&local, 0, sizeof(local));
	memset(&peer, 0, sizeof(peer));
	if (sceNetGetsockname(socket, (SceNetSockaddr *)&local, &local_length) >= 0)
		trace_vita_address_text(record->local, sizeof(record->local), &local);
	if (sceNetGetpeername(socket, (SceNetSockaddr *)&peer, &peer_length) >= 0)
		trace_vita_address_text(record->peer, sizeof(record->peer), &peer);
}

static void trace_refresh_addresses(int socket)
{
	struct trace_socket *record;

	pthread_mutex_lock(&trace_mutex);
	record = trace_socket_of(socket);
	if (record)
		trace_refresh_record(socket, record);
	pthread_mutex_unlock(&trace_mutex);
}

/* a failing call that is not one socket's transfer (the selects' epolls) */
static void trace_failure(int socket, const char *call, int result)
{
	unsigned int failure_number = 0;

	pthread_mutex_lock(&trace_mutex);
	if (trace_failures < TRACE_FAILURES_LOGGED)
		failure_number = ++trace_failures;
	pthread_mutex_unlock(&trace_mutex);
	if (failure_number)
		trace_log("FAIL #%d %s raw 0x%08x -> winsock %d [%u/%d]", socket, call, (unsigned int)result,
			winsock_error(result), failure_number, TRACE_FAILURES_LOGGED);
}

static void trace_made(int socket, int type, const char *how)
{
	struct trace_socket *record;

	pthread_mutex_lock(&trace_mutex);
	if (socket >= 0 && socket < SOCKET_IDENTIFIERS && trace_sockets[socket])
	{
		/* (an identifier closed behind the platform layer's back) */
		free(trace_sockets[socket]);
		trace_sockets[socket] = NULL;
	}
	record = trace_socket_of(socket);
	if (record)
		record->type = type;
	pthread_mutex_unlock(&trace_mutex);
	{
		/* (the buffers it starts with: the game sets a stream socket's on
		no HALO_LINUX build, and the Vita's do not grow as Linux's do) */
		int send_size = -1, receive_size = -1;
		unsigned int length = sizeof(send_size);

		sceNetGetsockopt(socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDBUF, &send_size, &length);
		length = sizeof(receive_size);
		sceNetGetsockopt(socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVBUF, &receive_size, &length);
		trace_log("#%d %s made (%s), buffers send %d receive %d", socket, trace_type_name(type), how, send_size,
			receive_size);
	}
}

static void trace_counts_text(char *text, size_t size, const struct trace_counts *counts, int receive)
{
	int length = snprintf(text, size, "%u calls %u ok %llu B, %u would-block, %u failed", counts->calls, counts->ok,
		counts->bytes, counts->blocked, counts->failed);
	int index;

	for (index = 0; index < TRACE_CODES && counts->code_counts[index]; index++)
	{
		if (length > 0 && (size_t)length < size)
			length += snprintf(text + length, size - (size_t)length, " %dx%u", counts->codes[index],
				counts->code_counts[index]);
	}
	if (receive && counts->closed && length > 0 && (size_t)length < size)
		length += snprintf(text + length, size - (size_t)length, ", %u closed", counts->closed);
	if (!receive && counts->as_send && length > 0 && (size_t)length < size)
		snprintf(text + length, size - (size_t)length, ", %u sendto as send", counts->as_send);
}

/* one socket's line, under trace_mutex */
static void trace_socket_line(int socket, const struct trace_socket *record, const char *when)
{
	char sends[160], receives[160];

	trace_counts_text(sends, sizeof(sends), &record->counts[TRACE_SEND], 0);
	trace_counts_text(receives, sizeof(receives), &record->counts[TRACE_RECEIVE], 1);
	trace_log("%s#%d %s %s -> %s | send: %s | receive: %s", when, socket, trace_type_name(record->type), record->local,
		record->peer, sends, receives);
}

static void trace_closed(int socket, int result)
{
	pthread_mutex_lock(&trace_mutex);
	if (socket >= 0 && socket < SOCKET_IDENTIFIERS && trace_sockets[socket])
	{
		trace_socket_line(socket, trace_sockets[socket], "closed ");
		free(trace_sockets[socket]);
		trace_sockets[socket] = NULL;
	}
	pthread_mutex_unlock(&trace_mutex);
	if (result < 0)
		trace_log("#%d close failed: raw 0x%08x", socket, (unsigned int)result);
}

/* every TRACE_PERIOD_US: the busy sockets' lines, the selects', the pool */
static void trace_report_if_due(void)
{
	unsigned long long now = vita_host_time_us();
	int socket;
	SceNetStatisticsInfo statistics;
	int statistics_result;

	if (now - trace_last_report < TRACE_PERIOD_US)
		return;
	pthread_mutex_lock(&trace_mutex);
	if (now - trace_last_report < TRACE_PERIOD_US)
	{
		pthread_mutex_unlock(&trace_mutex);
		return;
	}
	trace_last_report = now;
	for (socket = 0; socket < SOCKET_IDENTIFIERS; socket++)
	{
		struct trace_socket *record = trace_sockets[socket];
		unsigned int calls;

		if (!record)
			continue;
		calls = record->counts[TRACE_SEND].calls + record->counts[TRACE_RECEIVE].calls;
		/* (a socket idle since the last report is not repeated) */
		if (calls == record->reported_calls && calls)
			continue;
		record->reported_calls = calls;
		/* (a stream connect finishes after the call: its peer is known now) */
		if (!strcmp(record->peer, "-") || !strncmp(record->local, "0.0.0.0:0", 9))
			trace_refresh_record(socket, record);
		trace_socket_line(socket, record, "");
	}
	memset(&statistics, 0, sizeof(statistics));
	statistics_result = sceNetGetStatisticsInfo(&statistics, 0);
	trace_log("selects %u (%u ready, %u nothing, %u failed, %u epoll failures); pool free %d (least %d), "
		"kernel free %d (least %d), packets %d (0x%08x)", trace_selects.calls, trace_selects.ready,
		trace_selects.empty, trace_selects.failed, trace_selects.epoll_failed, statistics.libnet_mem_free_size,
		statistics.libnet_mem_free_min, statistics.kernel_mem_free_size, statistics.kernel_mem_free_min,
		statistics.packet_count, (unsigned int)statistics_result);
	pthread_mutex_unlock(&trace_mutex);
}

/* a send or receive call's result, with the address it went to or came from */
static void trace_transfer(int socket, int direction, const char *call, int result, const SceNetSockaddrIn *address,
	int as_send)
{
	struct trace_socket *record;
	char address_text[24];
	int log_datagram = 0, log_failure = 0, log_block = 0, code = 0;
	unsigned int failure_number = 0;

	pthread_mutex_lock(&trace_mutex);
	record = trace_socket_of(socket);
	if (record)
	{
		struct trace_counts *counts = &record->counts[direction];

		counts->calls++;
		if (as_send)
			counts->as_send++;
		if (result >= 0)
		{
			counts->ok++;
			counts->bytes += (unsigned long long)result;
			if (direction == TRACE_RECEIVE && result == 0 && record->type == SCE_NET_SOCK_STREAM)
				counts->closed++;
			if (record->type != SCE_NET_SOCK_STREAM && record->datagrams_logged[direction] < TRACE_DATAGRAMS_LOGGED)
			{
				record->datagrams_logged[direction]++;
				log_datagram = 1;
			}
		}
		else
		{
			int index;

			code = winsock_error(result);
			if (code == WSAEWOULDBLOCK)
			{
				counts->blocked++;
				if (direction == TRACE_SEND && trace_send_blocks < TRACE_SEND_BLOCKS_LOGGED)
				{
					trace_send_blocks++;
					log_block = 1;
				}
			}
			else
			{
				counts->failed++;
				for (index = 0; index < TRACE_CODES; index++)
				{
					if (!counts->code_counts[index] || counts->codes[index] == code)
					{
						counts->codes[index] = code;
						counts->code_counts[index]++;
						break;
					}
				}
				if (trace_failures < TRACE_FAILURES_LOGGED)
				{
					failure_number = ++trace_failures;
					log_failure = 1;
				}
			}
		}
	}
	pthread_mutex_unlock(&trace_mutex);
	trace_vita_address_text(address_text, sizeof(address_text), address);
	if (log_datagram)
		trace_log("#%d %s %d B%s%s%s", socket, call, result, address ? (direction == TRACE_SEND ? " to " : " from ") : "",
			address ? address_text : " (connected)", as_send ? " (sent as a send: connected peer)" : "");
	if (log_block)
		trace_log("#%d %s would block (send buffer full?) [%u/%d]", socket, call, trace_send_blocks,
			TRACE_SEND_BLOCKS_LOGGED);
	if (log_failure)
		trace_log("FAIL #%d %s raw 0x%08x -> winsock %d (address %s) [%u/%d]", socket, call, (unsigned int)result, code,
			address_text, failure_number, TRACE_FAILURES_LOGGED);
	trace_report_if_due();
}

/* a call other than a transfer that failed or is worth seeing */
static void trace_result(int socket, const char *call, int result, const char *detail)
{
	if (result < 0)
		trace_log("#%d %s%s -> raw 0x%08x (winsock %d)", socket, call, detail ? detail : "", (unsigned int)result,
			winsock_error(result));
	else
		trace_log("#%d %s%s -> %d", socket, call, detail ? detail : "", result);
}

int posix_socket_last_error(void)
{
	return last_error;
}

int posix_socket(int family, int type, int protocol)
{
	if (!net_ready())
	{
		last_error = WSAENETDOWN;
		return -1;
	}
	{
		int result = sceNetSocket("halo", family, type, protocol);

		forget_peer(result);
		if (trace_on())
		{
			if (result >= 0)
				trace_made(result, type, "socket");
			else
				trace_result(-1, "socket", result, trace_type_name(type));
		}
		return answer(result);
	}
}

int posix_socket_close(int socket)
{
	int result;

	forget_peer(socket);
	result = sceNetSocketClose(socket);
	if (trace_on())
		trace_closed(socket, result);
	return answer(result);
}

int posix_socket_bind(int socket, const void *address, int address_length)
{
	SceNetSockaddrIn vita_address;
	unsigned int length = address_to_vita(address, address_length, &vita_address);
	int result = sceNetBind(socket, (const SceNetSockaddr *)&vita_address, length);

	if (trace_on())
	{
		char detail[32] = " ";

		trace_vita_address_text(detail + 1, sizeof(detail) - 1, &vita_address);
		trace_result(socket, "bind", result, detail);
		if (result >= 0)
			trace_refresh_addresses(socket);
	}
	return answer(result);
}

int posix_socket_connect(int socket, const void *address, int address_length)
{
	SceNetSockaddrIn vita_address;
	unsigned int length = address_to_vita(address, address_length, &vita_address);
	int result;

	forget_peer(socket);
	result = sceNetConnect(socket, (const SceNetSockaddr *)&vita_address, length);
	if (trace_on())
	{
		char detail[32] = " to ";

		trace_vita_address_text(detail + 4, sizeof(detail) - 4, &vita_address);
		trace_result(socket, "connect", result, detail);
		trace_refresh_addresses(socket);
	}
	/* (a connect under way is WSAEWOULDBLOCK to the game, as in posix_net.c) */
	if (result < 0 && winsock_error(result) == WSAEINPROGRESS)
	{
		last_error = WSAEWOULDBLOCK;
		return -1;
	}
	return answer(result);
}

int posix_socket_listen(int socket, int backlog)
{
	int result = sceNetListen(socket, backlog);

	if (trace_on())
		trace_result(socket, "listen", result, NULL);
	return answer(result);
}

int posix_socket_accept(int socket, void *address, int *address_length)
{
	SceNetSockaddrIn vita_address;
	unsigned int length = sizeof(vita_address);
	int result = sceNetAccept(socket, (SceNetSockaddr *)&vita_address, &length);

	if (result >= 0)
		address_from_vita(&vita_address, address, address_length);
	if (trace_on())
	{
		if (result >= 0)
		{
			char how[32];

			snprintf(how, sizeof(how), "accepted on #%d", socket);
			trace_made(result, SCE_NET_SOCK_STREAM, how);
			trace_refresh_addresses(result);
		}
		else if (winsock_error(result) != WSAEWOULDBLOCK)
		{
			trace_result(socket, "accept", result, NULL);
		}
	}
	return answer(result);
}

int posix_socket_send(int socket, const void *buffer, int length, int flags)
{
	int result = sceNetSend(socket, buffer, (unsigned int)length, flags);

	if (trace_on())
		trace_transfer(socket, TRACE_SEND, "send", result, NULL, 0);
	return answer(result);
}

/* whether the socket is a connected one whose peer is this address: known
from an earlier send, or asked of the library (and then remembered) */
static int is_connected_peer(int socket, const SceNetSockaddrIn *address, int ask)
{
	SceNetSockaddrIn peer;
	unsigned int length = sizeof(peer);

	if (socket < 0 || socket >= SOCKET_IDENTIFIERS)
		return 0;
	if (connected_datagram[socket])
		return connected_peer_address[socket] == address->sin_addr.s_addr &&
			connected_peer_port[socket] == address->sin_port;
	if (!ask)
		return 0;
	memset(&peer, 0, sizeof(peer));
	if (sceNetGetpeername(socket, (SceNetSockaddr *)&peer, &length) < 0 ||
		peer.sin_addr.s_addr != address->sin_addr.s_addr || peer.sin_port != address->sin_port)
	{
		return 0;
	}
	connected_peer_address[socket] = peer.sin_addr.s_addr;
	connected_peer_port[socket] = peer.sin_port;
	connected_datagram[socket] = 1;
	return 1;
}

/* A connected datagram socket takes a sendto that names its peer on
Winsock and Linux (Vita3K, which runs these calls on the host's sockets,
too), but the Vita's stack is BSD's, whose udp_output refuses any
destination on a connected socket with EISCONN. The game's client connects
its datagram endpoint to the server (network_connection_connect) and sends
its in-game update, every 16 ms, to the server's address
(network_game_client_send_update -> write_to_endpoint, unreliable, its
failure unlogged): on the Vita every update failed, so the host never had
the client's input - the player could look but not move - and it stalled
128 ticks on and removed the client 2 s later ("forcibly removing client
system ... due to timeout in-game", 6 s into the match). The lobby's
messages, on the stream connection, never met it. A sendto naming the
connected peer is a send; one naming another address stays refused, as BSD
cannot send it. */
int posix_socket_sendto(int socket, const void *buffer, int length, int flags,
	const void *address, int address_length)
{
	SceNetSockaddrIn vita_address;
	unsigned int vita_length = address_to_vita(address, address_length, &vita_address);
	int result, as_send = 0;

	if (vita_length && is_connected_peer(socket, &vita_address, 0))
	{
		result = sceNetSend(socket, buffer, (unsigned int)length, flags);
		if (trace_on())
			trace_transfer(socket, TRACE_SEND, "sendto", result, &vita_address, 1);
		return answer(result);
	}
	result = sceNetSendto(socket, buffer, (unsigned int)length, flags,
		vita_length ? (const SceNetSockaddr *)&vita_address : NULL, vita_length);
	if (result < 0 && vita_length && winsock_error(result) == WSAEISCONN &&
		is_connected_peer(socket, &vita_address, 1))
	{
		static int logged;

		if (!logged)
		{
			logged = 1;
			vita_host_log("net: a connected datagram socket refused a sendto to its peer (EISCONN): sending it as a send");
		}
		if (trace_on())
			trace_log("#%d sendto refused (EISCONN, raw 0x%08x): its connected peer, sent as a send", socket,
				(unsigned int)result);
		result = sceNetSend(socket, buffer, (unsigned int)length, flags);
		as_send = 1;
	}
	if (trace_on())
		trace_transfer(socket, TRACE_SEND, "sendto", result, vita_length ? &vita_address : NULL, as_send);
	return answer(result);
}

int posix_socket_recv(int socket, void *buffer, int length, int flags)
{
	int result = sceNetRecv(socket, buffer, (unsigned int)length, flags);

	if (trace_on())
		trace_transfer(socket, TRACE_RECEIVE, "recv", result, NULL, 0);
	return answer(result);
}

int posix_socket_recvfrom(int socket, void *buffer, int length, int flags,
	void *address, int *address_length)
{
	SceNetSockaddrIn vita_address;
	unsigned int vita_length = sizeof(vita_address);
	int result;

	memset(&vita_address, 0, sizeof(vita_address));
	result = sceNetRecvfrom(socket, buffer, (unsigned int)length, flags, (SceNetSockaddr *)&vita_address, &vita_length);
	if (result >= 0)
		address_from_vita(&vita_address, address, address_length);
	if (trace_on())
		trace_transfer(socket, TRACE_RECEIVE, "recvfrom", result, result >= 0 ? &vita_address : NULL, 0);
	return answer(result);
}

int posix_socket_shutdown(int socket, int how)
{
	int result = sceNetShutdown(socket, how);

	if (trace_on())
		trace_result(socket, "shutdown", result, NULL);
	return answer(result);
}

int posix_socket_set_nonblocking(int socket, int nonblocking)
{
	int value = nonblocking ? 1 : 0;
	int result = sceNetSetsockopt(socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &value, sizeof(value));

	if (trace_on())
		trace_result(socket, "non-blocking", result, nonblocking ? " on" : " off");
	return answer(result);
}

int posix_socket_set_nodelay(int socket)
{
	int value = 1;
	int result = sceNetSetsockopt(socket, SCE_NET_IPPROTO_TCP, SCE_NET_TCP_NODELAY, &value, sizeof(value));

	if (trace_on())
		trace_result(socket, "TCP_NODELAY", result, NULL);
	return answer(result);
}

int posix_socket_bytes_available(int socket, posix_ulong *count)
{
	/* (no FIONREAD: the next datagram or the buffered stream bytes, up to
	the scratch buffer, by a peek that does not wait) */
	static char scratch[4096];
	int result = sceNetRecv(socket, scratch, sizeof(scratch), SCE_NET_MSG_PEEK | SCE_NET_MSG_DONTWAIT);

	if (result < 0 && winsock_error(result) == WSAEWOULDBLOCK)
		result = 0;
	if (result >= 0)
		*count = (posix_ulong)result;
	return answer(result < 0 ? result : 0);
}

/* Winsock's SOL_SOCKET (0xffff) and its option numbers are the BSD ones the
library uses; Xbox-only options (SO_ENCRYPT) are accepted and ignored */
static int known_option(int level, int name)
{
	if (level != SCE_NET_SOL_SOCKET)
		return 1;
	switch (name)
	{
	case SCE_NET_SO_REUSEADDR: case SCE_NET_SO_KEEPALIVE: case SCE_NET_SO_BROADCAST: case SCE_NET_SO_LINGER:
	case SCE_NET_SO_SNDBUF: case SCE_NET_SO_RCVBUF: case SCE_NET_SO_ERROR: case SCE_NET_SO_TYPE:
		return 1;
	default:
		return 0;
	}
}

int posix_socket_setsockopt(int socket, int level, int name, const void *value, int length)
{
	int result;
	char detail[64] = "";

	if (trace_on())
		snprintf(detail, sizeof(detail), " level 0x%x option 0x%x = %d", (unsigned int)level, (unsigned int)name,
			value && length >= 4 ? *(const int *)value : -1);
	if (!known_option(level, name))
	{
		if (trace_on())
			trace_result(socket, "setsockopt (ignored)", 0, detail);
		last_error = 0;
		return 0;
	}
	result = sceNetSetsockopt(socket, level, name, value, (unsigned int)length);
	if (trace_on())
		trace_result(socket, "setsockopt", result, detail);
	return answer(result);
}

int posix_socket_getsockopt(int socket, int level, int name, void *value, int *length)
{
	unsigned int vita_length = (unsigned int)*length;
	int result;

	if (!known_option(level, name))
	{
		last_error = WSAENOPROTOOPT;
		return -1;
	}
	result = sceNetGetsockopt(socket, level, name, value, &vita_length);
	*length = (int)vita_length;
	if (trace_on())
	{
		char detail[64];

		snprintf(detail, sizeof(detail), " level 0x%x option 0x%x: %d", (unsigned int)level, (unsigned int)name,
			result >= 0 && vita_length >= 4 ? *(int *)value : -1);
		trace_result(socket, "getsockopt", result, detail);
	}
	return answer(result);
}

int posix_socket_getsockname(int socket, void *address, int *address_length)
{
	SceNetSockaddrIn vita_address;
	unsigned int length = sizeof(vita_address);
	int result = sceNetGetsockname(socket, (SceNetSockaddr *)&vita_address, &length);

	if (result >= 0)
		address_from_vita(&vita_address, address, address_length);
	return answer(result);
}

int posix_socket_getpeername(int socket, void *address, int *address_length)
{
	SceNetSockaddrIn vita_address;
	unsigned int length = sizeof(vita_address);
	int result = sceNetGetpeername(socket, (SceNetSockaddr *)&vita_address, &length);

	if (result >= 0)
		address_from_vita(&vita_address, address, address_length);
	return answer(result);
}

/* select over a one-shot epoll: each socket once, with the events of the
sets it is in; the sets are cut down to the ready sockets as Winsock does */
struct select_entry
{
	int socket;
	unsigned int wanted, ready;
};

static int select_add(struct select_entry *entries, int count, int socket, unsigned int events)
{
	int index;

	if (socket < 0)
		return count;
	for (index = 0; index < count; index++)
		if (entries[index].socket == socket)
		{
			entries[index].wanted |= events;
			return count;
		}
	if (count == SELECT_MAXIMUM)
		return count;
	entries[count].socket = socket;
	entries[count].wanted = events;
	entries[count].ready = 0;
	return count + 1;
}

static void select_keep(const struct select_entry *entries, int entry_count, int *sockets, int *count,
	unsigned int events)
{
	int index, kept = 0;

	for (index = 0; index < *count; index++)
	{
		int entry;

		for (entry = 0; entry < entry_count; entry++)
			if (entries[entry].socket == sockets[index] && (entries[entry].ready & events))
			{
				sockets[kept++] = sockets[index];
				break;
			}
	}
	*count = kept;
}

int posix_socket_select(int *read, int *read_count, int *write, int *write_count,
	int *error, int *error_count, posix_long timeout_seconds, posix_long timeout_microseconds, int infinite)
{
	struct select_entry entries[SELECT_MAXIMUM];
	SceNetEpollEvent events[SELECT_MAXIMUM];
	int count = 0, index, epoll, result, ready = 0;
	int timeout = infinite ? -1 : (int)(timeout_seconds * 1000000 + timeout_microseconds);

	for (index = 0; read && index < *read_count; index++)
		count = select_add(entries, count, read[index], SCE_NET_EPOLLIN);
	for (index = 0; write && index < *write_count; index++)
		count = select_add(entries, count, write[index], SCE_NET_EPOLLOUT);
	for (index = 0; error && index < *error_count; index++)
		count = select_add(entries, count, error[index], SCE_NET_EPOLLERR);
	if (count == 0)
	{
		if (!infinite && timeout > 0)
			vita_host_sleep_us((unsigned long)timeout);
		return 0;
	}
	if (trace_on())
		__atomic_fetch_add(&trace_selects.calls, 1, __ATOMIC_RELAXED);
	epoll = sceNetEpollCreate("halo_select", 0);
	if (epoll < 0)
	{
		if (trace_on())
		{
			__atomic_fetch_add(&trace_selects.epoll_failed, 1, __ATOMIC_RELAXED);
			trace_failure(-1, "epoll create", epoll);
		}
		return answer(epoll);
	}
	for (index = 0; index < count; index++)
	{
		SceNetEpollEvent event;

		memset(&event, 0, sizeof(event));
		/* (errors and hang-ups always reported) */
		event.events = entries[index].wanted | SCE_NET_EPOLLERR | SCE_NET_EPOLLHUP;
		event.data.u32 = (unsigned int)index;
		result = sceNetEpollControl(epoll, SCE_NET_EPOLL_CTL_ADD, entries[index].socket, &event);
		/* (a socket the epoll does not take is never ready: seen in the trace) */
		if (result < 0 && trace_on())
		{
			__atomic_fetch_add(&trace_selects.epoll_failed, 1, __ATOMIC_RELAXED);
			trace_failure(entries[index].socket, "epoll add", result);
		}
	}
	result = sceNetEpollWait(epoll, events, count, timeout);
	sceNetEpollDestroy(epoll);
	if (result < 0)
	{
		if (trace_on())
		{
			__atomic_fetch_add(&trace_selects.failed, 1, __ATOMIC_RELAXED);
			trace_failure(-1, "epoll wait", result);
		}
		return answer(result);
	}
	for (index = 0; index < result; index++)
	{
		unsigned int entry = events[index].data.u32;

		if (entry < (unsigned int)count)
		{
			unsigned int happened = events[index].events;

			/* (a closed or failed socket reads, as on BSD: the read says why) */
			if (happened & (SCE_NET_EPOLLERR | SCE_NET_EPOLLHUP))
				happened |= SCE_NET_EPOLLIN;
			entries[entry].ready = happened & (entries[entry].wanted | SCE_NET_EPOLLERR);
		}
	}
	/* a socket whose connect failed is not writeable but in error, as in
	Winsock (the game takes writeable as connected) */
	for (index = 0; index < count; index++)
	{
		if (entries[index].ready & SCE_NET_EPOLLOUT)
		{
			int pending = 0;
			unsigned int length = sizeof(pending);

			if (sceNetGetsockopt(entries[index].socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR, &pending, &length) >= 0 &&
				pending)
			{
				entries[index].ready &= ~SCE_NET_EPOLLOUT;
				entries[index].ready |= SCE_NET_EPOLLERR;
			}
		}
	}
	if (read)
		select_keep(entries, count, read, read_count, SCE_NET_EPOLLIN);
	if (write)
		select_keep(entries, count, write, write_count, SCE_NET_EPOLLOUT);
	if (error)
		select_keep(entries, count, error, error_count, SCE_NET_EPOLLERR);
	ready = (read ? *read_count : 0) + (write ? *write_count : 0) + (error ? *error_count : 0);
	if (trace_on())
	{
		__atomic_fetch_add(ready > 0 ? &trace_selects.ready : &trace_selects.empty, 1, __ATOMIC_RELAXED);
		trace_report_if_due();
	}
	/* (nothing ready leaves the last error as it was, as Winsock does) */
	if (ready > 0)
		last_error = 0;
	return ready;
}

/* ad hoc play (HALO_NET_ADHOC, vita_main.c from the settings panel): an
address for the game even without Wi-Fi. In the ad hoc group the Vita has
no infrastructure address, and the game takes a machine without one as
having no network (XNetGetEthernetLinkStatus, xnet.c: "not connected",
and no system link); its traffic to the other machines goes through
internet play's stand-ins on 127.0.0.1 (p2p_adhoc.c), so this address is
never sent to. 169.254 and the MAC's last two bytes (link-local, as an IP
stack would pick), or 169.254.1.1 before there is a group. */
static posix_ulong adhoc_stand_in_address(void)
{
	const char *setting = getenv("HALO_NET_ADHOC");
	unsigned char low = 1, high = 1;

	if (!setting || (strcmp(setting, "true") && strcmp(setting, "1")))
		return 0;
	if (adhoc_state == 2)
	{
		high = adhoc_address.data[4];
		low = adhoc_address.data[5] ? adhoc_address.data[5] : 1;
	}
	return 169u | 254u << 8 | (unsigned int)high << 16 | (unsigned int)low << 24;
}

posix_ulong posix_local_ipv4_address(void)
{
	SceNetCtlInfo information;
	SceNetInAddr address;

	if (!net_ready())
		return adhoc_stand_in_address();
	memset(&information, 0, sizeof(information));
	if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &information) < 0 ||
		sceNetInetPton(SCE_NET_AF_INET, information.ip_address, &address) <= 0)
		return adhoc_stand_in_address();
	return address.s_addr;
}

posix_ulong posix_resolve_ipv4(const char *host)
{
	SceNetInAddr address;
	int resolver;

	if (!host || !net_ready())
		return 0;
	if (sceNetInetPton(SCE_NET_AF_INET, host, &address) > 0)
		return address.s_addr;
	resolver = sceNetResolverCreate("halo", NULL, 0);
	if (resolver < 0)
		return 0;
	/* (two tries of two seconds) */
	if (sceNetResolverStartNtoa(resolver, host, &address, 2000000, 2, 0) < 0)
		address.s_addr = 0;
	sceNetResolverDestroy(resolver);
	return address.s_addr;
}

/* ---------- ad hoc probe (HALO_ADHOC_PROBE=1)

What the Vita does for an app that asks for ad hoc play, logged step by
step in halo.log: the PSP-style ad hoc libraries start with an ad hoc ID,
and the SDK names no call that creates or joins a group, so the probe looks
for the system joining one on its own (an ad hoc state, an ad hoc address,
peers) and tries a peer-to-peer datagram socket with a broadcast. Run it on
two Vitas side by side to see them find each other. */

static void probe_log(const char *step, int result)
{
	char line[160];

	snprintf(line, sizeof(line), "adhoc probe: %s -> 0x%08x", step, (unsigned int)result);
	vita_host_log(line);
}

static int adhoc_probe_thread(SceSize arguments_size, void *arguments)
{
	SceNetAdhocctlAdhocId adhoc_id;
	SceNetEtherAddr mac;
	SceNetInAddr address;
	int result, state = -1, round, socket, value = 1;

	(void)arguments_size;
	(void)arguments;
	if (!net_ready())
		return 0;
	probe_log("load SCE_SYSMODULE_PSPNET_ADHOC", sceSysmoduleLoadModule(SCE_SYSMODULE_PSPNET_ADHOC));
	probe_log("sceNetAdhocInit", sceNetAdhocInit());
	memset(&adhoc_id, 0, sizeof(adhoc_id));
	adhoc_id.type = SCE_NET_ADHOCCTL_ADHOCTYPE_PRODUCT_ID;
	memcpy(adhoc_id.data, "HCEV00001", SCE_NET_ADHOCCTL_ADHOCID_LEN);
	probe_log("sceNetAdhocctlInit(HCEV00001)", sceNetAdhocctlInit(&adhoc_id));
	memset(&mac, 0, sizeof(mac));
	result = sceNetAdhocctlGetEtherAddr(&mac);
	{
		char line[96];

		snprintf(line, sizeof(line), "adhoc probe: MAC %02x:%02x:%02x:%02x:%02x:%02x (0x%08x)", mac.data[0], mac.data[1],
			mac.data[2], mac.data[3], mac.data[4], mac.data[5], (unsigned int)result);
		vita_host_log(line);
	}
	for (round = 0; round < 10; round++)
	{
		int peers_length = 0;
		char line[128];

		result = sceNetCtlAdhocGetState(&state);
		memset(&address, 0, sizeof(address));
		sceNetCtlAdhocGetInAddr(&address);
		sceNetAdhocctlGetPeerList(&peers_length, NULL);
		snprintf(line, sizeof(line), "adhoc probe: round %d state %d (0x%08x) address %08x peer bytes %d", round, state,
			(unsigned int)result, address.s_addr, peers_length);
		vita_host_log(line);
		sceKernelDelayThread(1000000);
	}
	socket = sceNetSocket("halo_p2p", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM_P2P, 0);
	probe_log("sceNetSocket(DGRAM_P2P)", socket);
	if (socket >= 0)
	{
		SceNetSockaddrIn target;

		probe_log("SO_BROADCAST", sceNetSetsockopt(socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &value, sizeof(value)));
		memset(&target, 0, sizeof(target));
		target.sin_len = sizeof(target);
		target.sin_family = SCE_NET_AF_INET;
		target.sin_port = (unsigned short)((2302 >> 8) | ((2302 & 0xff) << 8));
		target.sin_vport = (unsigned short)((2302 >> 8) | ((2302 & 0xff) << 8));
		probe_log("bind P2P :2302", sceNetBind(socket, (const SceNetSockaddr *)&target, sizeof(target)));
		target.sin_addr.s_addr = 0xffffffffu;
		for (round = 0; round < 10; round++)
		{
			char buffer[64];
			SceNetSockaddrIn from;
			unsigned int from_length = sizeof(from);

			probe_log("sendto broadcast", sceNetSendto(socket, "halo ad hoc probe", 17, 0, (const SceNetSockaddr *)&target,
				sizeof(target)));
			result = sceNetRecvfrom(socket, buffer, sizeof(buffer), SCE_NET_MSG_DONTWAIT, (SceNetSockaddr *)&from, &from_length);
			if (result > 0)
			{
				char line[128];

				snprintf(line, sizeof(line), "adhoc probe: received %d bytes from %08x", result, from.sin_addr.s_addr);
				vita_host_log(line);
			}
			sceKernelDelayThread(1000000);
		}
		sceNetSocketClose(socket);
	}
	vita_host_log("adhoc probe: done");
	return 0;
}

void vita_net_adhoc_probe(void)
{
	SceUID thread;

	if (!getenv("HALO_ADHOC_PROBE") || !atoi(getenv("HALO_ADHOC_PROBE")))
		return;
	thread = sceKernelCreateThread("adhoc_probe", adhoc_probe_thread, 0x10000100, 0x10000, 0, 0, NULL);
	if (thread >= 0)
		sceKernelStartThread(thread, 0, NULL);
}

/* ---------- ad hoc play's group (posix_adhoc_*, for port/linux/src/p2p_adhoc.c)

The PSP-style ad hoc network (ScePspnetAdhoc), which is what Vita homebrew
that plays ad hoc uses (vitaQuake's net_adhoc_psp2.c, on hardware): the
system's network check dialog joins a group (vita_adhoc_connect), and
datagrams then go between machines' MAC addresses on PDP ports. The SDK
names no call that creates or joins a group (no sceNetAdhocctlCreate or
Connect): the dialog is the way in, which is why HALO_ADHOC_PROBE, which
only starts the libraries, never saw a group.

PDP calls with no flag wait (up to their timeout); with
SCE_NET_ADHOC_F_NONBLOCK they return would-block at once. A receive is a
wait with a timeout, so the bridge's thread sleeps until a datagram comes;
if this firmware's waits return at once instead (seen as many empty
receives each much shorter than asked), receives fall back to polling
every 2 ms, logged once. */

static int adhoc_pdp = -1;
static int adhoc_polling;
static unsigned int adhoc_short_waits;

static int adhoc_failed(int result)
{
	switch ((unsigned int)result)
	{
	case SCE_ERROR_NET_ADHOC_WOULD_BLOCK:
	case SCE_ERROR_NET_ADHOC_TIMEOUT:
		return 0;
	default:
		return result < 0;
	}
}

int posix_adhoc_ready(unsigned char *address)
{
	if (adhoc_state != 2)
		return 0;
	memcpy(address, adhoc_address.data, 6);
	return 1;
}

int posix_adhoc_open(unsigned short port)
{
	char line[96];

	posix_adhoc_close();
	/* (a 32 KB receive buffer; the SDK's examples use 8 KB, tried next) */
	adhoc_pdp = sceNetAdhocPdpCreate(&adhoc_address, port, 0x8000, 0);
	if (adhoc_pdp < 0)
		adhoc_pdp = sceNetAdhocPdpCreate(&adhoc_address, port, 0x2000, 0);
	snprintf(line, sizeof(line), "adhoc: PDP port %u -> 0x%08x", port, (unsigned int)adhoc_pdp);
	vita_host_log(line);
	if (adhoc_pdp < 0)
	{
		adhoc_pdp = -1;
		return -1;
	}
	adhoc_short_waits = 0;
	return 0;
}

void posix_adhoc_close(void)
{
	if (adhoc_pdp >= 0)
		sceNetAdhocPdpDelete(adhoc_pdp, 0);
	adhoc_pdp = -1;
}

int posix_adhoc_send(const unsigned char *address, unsigned short port, const void *data, int size)
{
	int result;

	if (adhoc_pdp < 0)
		return -1;
	result = sceNetAdhocPdpSend(adhoc_pdp, (const SceNetEtherAddr *)address, port, data, size, 0,
		SCE_NET_ADHOC_F_NONBLOCK);
	if (adhoc_failed(result))
	{
		static int logged;

		if (logged++ < 8)
		{
			char line[96];

			snprintf(line, sizeof(line), "adhoc: PDP send of %d bytes -> 0x%08x", size, (unsigned int)result);
			vita_host_log(line);
		}
		return -1;
	}
	return result < 0 ? 0 : size;
}

int posix_adhoc_receive(unsigned char *address, unsigned short *port, void *data, int size, posix_ulong timeout)
{
	SceNetEtherAddr from;
	SceUShort16 from_port = 0;
	int length = size;
	int result;
	unsigned long long started = sceKernelGetProcessTimeWide();

	if (adhoc_pdp < 0)
		return -1;
	result = sceNetAdhocPdpRecv(adhoc_pdp, &from, &from_port, data, &length, adhoc_polling ? 0 : (unsigned int)timeout,
		adhoc_polling ? SCE_NET_ADHOC_F_NONBLOCK : 0);
	if (result >= 0)
	{
		adhoc_short_waits = 0;
		memcpy(address, from.data, 6);
		*port = from_port;
		return length;
	}
	if (adhoc_failed(result))
	{
		char line[96];

		snprintf(line, sizeof(line), "adhoc: PDP receive -> 0x%08x (the group is gone?)", (unsigned int)result);
		vita_host_log(line);
		return -1;
	}
	if (adhoc_polling)
	{
		sceKernelDelayThread(2000);
		return 0;
	}
	/* (a wait that came back at once, empty: count, and poll if they keep
	on) */
	if (timeout >= 4000 && sceKernelGetProcessTimeWide() - started < timeout / 4)
	{
		if (++adhoc_short_waits == 20)
		{
			adhoc_polling = 1;
			vita_host_log("adhoc: PDP receives do not wait on this firmware: polling every 2 ms");
		}
		sceKernelDelayThread(2000);
	}
	else
	{
		adhoc_short_waits = 0;
	}
	return 0;
}

/* ---------- self-test (HALO_NET_SELFTEST=1)

The platform layer's calls as the game makes them for a split screen or
system link game - loopback datagrams, a loopback stream connect and
accept, select readiness, a broadcast - logged with their results, to see
on hardware what Vita3K's host sockets hide */

static void selftest_log(const char *step, int result)
{
	char line[160];

	snprintf(line, sizeof(line), "net selftest: %s -> %d (error %d)", step, result, result < 0 ? last_error : 0);
	vita_host_log(line);
}

static void winsock_address(unsigned char out[16], unsigned int address_host_order, unsigned short port)
{
	unsigned short family = SCE_NET_AF_INET;

	memset(out, 0, 16);
	memcpy(out, &family, 2);
	out[2] = (unsigned char)(port >> 8);
	out[3] = (unsigned char)port;
	out[4] = (unsigned char)(address_host_order >> 24);
	out[5] = (unsigned char)(address_host_order >> 16);
	out[6] = (unsigned char)(address_host_order >> 8);
	out[7] = (unsigned char)address_host_order;
}

static int net_selftest_thread(SceSize arguments_size, void *arguments)
{
	unsigned char address[16], from[16];
	char buffer[64];
	int from_length, result, a, b, listener, client, accepted, value = 1, sockets[2], count, empty = 0;

	(void)arguments_size;
	(void)arguments;
	sceKernelDelayThread(3000000);
	vita_host_log("net selftest: start");
	{
		char line[64];
		posix_ulong local = posix_local_ipv4_address();

		snprintf(line, sizeof(line), "net selftest: local address %08lx", (unsigned long)local);
		vita_host_log(line);
	}
	/* datagrams over the loopback address */
	a = posix_socket(SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
	b = posix_socket(SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
	selftest_log("udp sockets", a < 0 || b < 0 ? -1 : a);
	selftest_log("udp a nonblocking", posix_socket_set_nonblocking(a, 1));
	winsock_address(address, 0x7f000001u, 2400);
	selftest_log("udp a bind 127.0.0.1:2400", posix_socket_bind(a, address, 16));
	winsock_address(address, 0, 0);
	selftest_log("udp b bind any:0", posix_socket_bind(b, address, 16));
	winsock_address(address, 0x7f000001u, 2400);
	selftest_log("udp b sendto a", posix_socket_sendto(b, "halo loopback", 13, 0, address, 16));
	sockets[0] = a;
	count = 1;
	selftest_log("udp select a readable (100 ms)", posix_socket_select(sockets, &count, NULL, &empty, NULL, &empty, 0, 100000, 0));
	{
		posix_ulong available = 0;

		selftest_log("udp a bytes available", posix_socket_bytes_available(a, &available) < 0 ? -1 : (int)available);
	}
	from_length = 16;
	memset(from, 0, sizeof(from));
	result = posix_socket_recvfrom(a, buffer, sizeof(buffer), 0, from, &from_length);
	selftest_log("udp a recvfrom", result);
	{
		char line[128];

		snprintf(line, sizeof(line), "net selftest: from family %u port %u address %u.%u.%u.%u length %d", from[0] | from[1] << 8,
			from[2] << 8 | from[3], from[4], from[5], from[6], from[7], from_length);
		vita_host_log(line);
	}
	result = posix_socket_recvfrom(a, buffer, sizeof(buffer), 0, from, &from_length);
	selftest_log("udp a recvfrom when empty (expect error 10035)", result);
	/* broadcast to the port a listens on */
	selftest_log("udp b SO_BROADCAST", posix_socket_setsockopt(b, 0xffff, 0x0020, &value, sizeof(value)));
	winsock_address(address, 0xffffffffu, 2400);
	selftest_log("udp b sendto broadcast:2400", posix_socket_sendto(b, "halo broadcast", 14, 0, address, 16));
	sceKernelDelayThread(100000);
	from_length = 16;
	selftest_log("udp a recvfrom broadcast", posix_socket_recvfrom(a, buffer, sizeof(buffer), 0, from, &from_length));
	/* the game's client update: a sendto naming the peer of a connected
	datagram socket, which BSD's stack refuses (EISCONN) and
	posix_socket_sendto sends as a send */
	winsock_address(address, 0x7f000001u, 2400);
	selftest_log("udp b connect a", posix_socket_connect(b, address, 16));
	{
		SceNetSockaddrIn target;
		char line[128];

		address_to_vita(address, 16, &target);
		result = sceNetSendto(b, "raw", 3, 0, (const SceNetSockaddr *)&target, sizeof(target));
		snprintf(line, sizeof(line), "net selftest: raw sceNetSendto naming the connected peer -> 0x%08x "
			"(0x80410138 = EISCONN, BSD's rule)", (unsigned int)result);
		vita_host_log(line);
	}
	selftest_log("udp b sendto a while connected (expect 14)", posix_socket_sendto(b, "halo connected", 14, 0, address, 16));
	sceKernelDelayThread(50000);
	for (count = 0; count < 3; count++)
	{
		from_length = 16;
		selftest_log("udp a recvfrom (14 = the connected sendto arrived)",
			posix_socket_recvfrom(a, buffer, sizeof(buffer), 0, from, &from_length));
	}
	posix_socket_close(a);
	posix_socket_close(b);

	/* a stream connect and accept over the loopback address */
	listener = posix_socket(SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
	client = posix_socket(SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
	selftest_log("tcp sockets", listener < 0 || client < 0 ? -1 : listener);
	selftest_log("tcp listener nonblocking", posix_socket_set_nonblocking(listener, 1));
	winsock_address(address, 0x7f000001u, 2401);
	selftest_log("tcp listener bind 127.0.0.1:2401", posix_socket_bind(listener, address, 16));
	selftest_log("tcp listen", posix_socket_listen(listener, 4));
	selftest_log("tcp client nonblocking", posix_socket_set_nonblocking(client, 1));
	selftest_log("tcp client connect (expect -1 error 10035)", posix_socket_connect(client, address, 16));
	sockets[0] = client;
	count = 1;
	selftest_log("tcp select client writable (500 ms)", posix_socket_select(NULL, &empty, sockets, &count, NULL, &empty, 0, 500000, 0));
	sockets[0] = listener;
	count = 1;
	selftest_log("tcp select listener readable (500 ms)", posix_socket_select(sockets, &count, NULL, &empty, NULL, &empty, 0, 500000, 0));
	from_length = 16;
	accepted = posix_socket_accept(listener, from, &from_length);
	selftest_log("tcp accept", accepted);
	selftest_log("tcp client send", posix_socket_send(client, "halo stream", 11, 0));
	sceKernelDelayThread(50000);
	if (accepted >= 0)
	{
		selftest_log("tcp accepted nonblocking", posix_socket_set_nonblocking(accepted, 1));
		selftest_log("tcp accepted recv", posix_socket_recv(accepted, buffer, sizeof(buffer), 0));
		selftest_log("tcp accepted recv when empty (expect error 10035)", posix_socket_recv(accepted, buffer, sizeof(buffer), 0));
		selftest_log("tcp accepted send", posix_socket_send(accepted, "reply", 5, 0));
		sceKernelDelayThread(50000);
		selftest_log("tcp client recv", posix_socket_recv(client, buffer, sizeof(buffer), 0));
		posix_socket_close(accepted);
	}
	posix_socket_close(client);
	posix_socket_close(listener);
	vita_host_log("net selftest: done");
	return 0;
}

void vita_net_selftest(void)
{
	SceUID thread;

	/* (the trace runs it too: the calls' results on this console, before
	the game's) */
	if ((!getenv("HALO_NET_SELFTEST") || !atoi(getenv("HALO_NET_SELFTEST"))) &&
		(!getenv("HALO_NET_TRACE") || !atoi(getenv("HALO_NET_TRACE"))))
	{
		return;
	}
	thread = sceKernelCreateThread("net_selftest", net_selftest_thread, 0x10000100, 0x10000, 0, 0, NULL);
	if (thread >= 0)
		sceKernelStartThread(thread, 0, NULL);
}
