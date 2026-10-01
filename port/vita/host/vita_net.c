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
game's (Winsock's) address has a 16-bit family; and there is no select, so
select is a one-shot epoll.
*/

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

#include <stdlib.h>
#include <string.h>

#include "posix.h"
#include "vita_host.h"

#define WSAEINVAL 10022
#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036
#define WSAENOPROTOOPT 10042
#define WSAENETDOWN 10050

#define NET_MEMORY_SIZE (1024 * 1024)
#define SELECT_MAXIMUM 64

static __thread int last_error;
static int net_state; /* 0 not tried, 1 up, -1 failed */

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

/* a library result as the platform layer's: -1 with the Winsock error */
static int answer(int result)
{
	if (result < 0)
	{
		last_error = 10000 + (result & 0xff);
		return -1;
	}
	last_error = 0;
	return result;
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
	return answer(sceNetSocket("halo", family, type, protocol));
}

int posix_socket_close(int socket)
{
	return answer(sceNetSocketClose(socket));
}

int posix_socket_bind(int socket, const void *address, int address_length)
{
	SceNetSockaddrIn vita_address;
	unsigned int length = address_to_vita(address, address_length, &vita_address);

	return answer(sceNetBind(socket, (const SceNetSockaddr *)&vita_address, length));
}

int posix_socket_connect(int socket, const void *address, int address_length)
{
	SceNetSockaddrIn vita_address;
	unsigned int length = address_to_vita(address, address_length, &vita_address);
	int result = sceNetConnect(socket, (const SceNetSockaddr *)&vita_address, length);

	/* (a connect under way is WSAEWOULDBLOCK to the game, as in posix_net.c) */
	if (result < 0 && 10000 + (result & 0xff) == WSAEINPROGRESS)
	{
		last_error = WSAEWOULDBLOCK;
		return -1;
	}
	return answer(result);
}

int posix_socket_listen(int socket, int backlog)
{
	return answer(sceNetListen(socket, backlog));
}

int posix_socket_accept(int socket, void *address, int *address_length)
{
	SceNetSockaddrIn vita_address;
	unsigned int length = sizeof(vita_address);
	int result = sceNetAccept(socket, (SceNetSockaddr *)&vita_address, &length);

	if (result >= 0)
		address_from_vita(&vita_address, address, address_length);
	return answer(result);
}

int posix_socket_send(int socket, const void *buffer, int length, int flags)
{
	return answer(sceNetSend(socket, buffer, (unsigned int)length, flags));
}

int posix_socket_sendto(int socket, const void *buffer, int length, int flags,
	const void *address, int address_length)
{
	SceNetSockaddrIn vita_address;
	unsigned int vita_length = address_to_vita(address, address_length, &vita_address);

	return answer(sceNetSendto(socket, buffer, (unsigned int)length, flags,
		vita_length ? (const SceNetSockaddr *)&vita_address : NULL, vita_length));
}

int posix_socket_recv(int socket, void *buffer, int length, int flags)
{
	return answer(sceNetRecv(socket, buffer, (unsigned int)length, flags));
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
	return answer(result);
}

int posix_socket_shutdown(int socket, int how)
{
	return answer(sceNetShutdown(socket, how));
}

int posix_socket_set_nonblocking(int socket, int nonblocking)
{
	int value = nonblocking ? 1 : 0;

	return answer(sceNetSetsockopt(socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &value, sizeof(value)));
}

int posix_socket_set_nodelay(int socket)
{
	int value = 1;

	return answer(sceNetSetsockopt(socket, SCE_NET_IPPROTO_TCP, SCE_NET_TCP_NODELAY, &value, sizeof(value)));
}

int posix_socket_bytes_available(int socket, posix_ulong *count)
{
	/* (no FIONREAD: the next datagram or the buffered stream bytes, up to
	the scratch buffer, by a peek that does not wait) */
	static char scratch[4096];
	int result = sceNetRecv(socket, scratch, sizeof(scratch), SCE_NET_MSG_PEEK | SCE_NET_MSG_DONTWAIT);

	if (result < 0 && 10000 + (result & 0xff) == WSAEWOULDBLOCK)
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
	if (!known_option(level, name))
	{
		last_error = 0;
		return 0;
	}
	return answer(sceNetSetsockopt(socket, level, name, value, (unsigned int)length));
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
	epoll = sceNetEpollCreate("halo_select", 0);
	if (epoll < 0)
		return answer(epoll);
	for (index = 0; index < count; index++)
	{
		SceNetEpollEvent event;

		memset(&event, 0, sizeof(event));
		/* (errors and hang-ups always reported) */
		event.events = entries[index].wanted | SCE_NET_EPOLLERR | SCE_NET_EPOLLHUP;
		event.data.u32 = (unsigned int)index;
		sceNetEpollControl(epoll, SCE_NET_EPOLL_CTL_ADD, entries[index].socket, &event);
	}
	result = sceNetEpollWait(epoll, events, count, timeout);
	sceNetEpollDestroy(epoll);
	if (result < 0)
		return answer(result);
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
	/* (nothing ready leaves the last error as it was, as Winsock does) */
	if (ready > 0)
		last_error = 0;
	return ready;
}

posix_ulong posix_local_ipv4_address(void)
{
	SceNetCtlInfo information;
	SceNetInAddr address;

	if (!net_ready())
		return 0;
	memset(&information, 0, sizeof(information));
	if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &information) < 0)
		return 0;
	if (sceNetInetPton(SCE_NET_AF_INET, information.ip_address, &address) <= 0)
		return 0;
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

#include <psp2/pspnet_adhoc.h>
#include <psp2/pspnet_adhocctl.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>

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
