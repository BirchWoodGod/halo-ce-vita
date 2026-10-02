/*
MOCK_SCENET.C

The Vita's network library (sceNet) for a desktop test of
port/vita/host/vita_net.c, over Linux sockets but with the Vita's
behaviour where it differs from Linux's (run_vita_net_test.sh builds it
32-bit, the SDK headers' sizes):

- errors are returned, not left in errno: 0x80410100 | the BSD errno
  (EAGAIN 35, EINPROGRESS 36, EISCONN 56 ..., not Linux's numbers);
- a SceNetSockaddrIn starts with a length byte and a one-byte family;
- the option numbers are BSD's (SO_REUSEADDR 4, SO_SNDBUF 0x1001 ...) and
  non-blocking mode is the SO_NBIO option;
- MSG_DONTWAIT is 0x80;
- epoll timeouts are in microseconds;
- the Vita's stack is BSD's: a datagram socket that is connected refuses
  a sendto that names a destination with EISCONN, where Linux (and Vita3K,
  which runs the game's calls on the host's sockets) sends it. Set
  MOCK_SCENET_LINUX_SENDTO=1 to get Linux's behaviour instead.

The ad hoc and system calls vita_net.c also makes are stubs.
*/

#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/pspnet_adhoc.h>
#include <psp2/pspnet_adhocctl.h>
#include <psp2/sysmodule.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* ---------- errors */

static int bsd_errno(int error)
{
	switch (error)
	{
	case EAGAIN: return 35;
	case EINPROGRESS: return 36;
	case EALREADY: return 37;
	case ENOTSOCK: return 38;
	case EDESTADDRREQ: return 39;
	case EMSGSIZE: return 40;
	case EPROTOTYPE: return 41;
	case ENOPROTOOPT: return 42;
	case EPROTONOSUPPORT: return 43;
	case EOPNOTSUPP: return 45;
	case EAFNOSUPPORT: return 47;
	case EADDRINUSE: return 48;
	case EADDRNOTAVAIL: return 49;
	case ENETDOWN: return 50;
	case ENETUNREACH: return 51;
	case ENETRESET: return 52;
	case ECONNABORTED: return 53;
	case ECONNRESET: return 54;
	case ENOBUFS: return 55;
	case EISCONN: return 56;
	case ENOTCONN: return 57;
	case ESHUTDOWN: return 58;
	case ETIMEDOUT: return 60;
	case ECONNREFUSED: return 61;
	case EHOSTUNREACH: return 65;
	default: return error < 35 ? error : 22; /* (1-34 are the same numbers) */
	}
}

static int fail_with(int bsd)
{
	return (int)(0x80410100u | (unsigned int)bsd);
}

static int result_of(int result)
{
	return result < 0 ? fail_with(bsd_errno(errno)) : result;
}

/* ---------- addresses */

static void to_linux(const SceNetSockaddr *address, struct sockaddr_in *out)
{
	const SceNetSockaddrIn *in = (const SceNetSockaddrIn *)address;

	memset(out, 0, sizeof(*out));
	out->sin_family = in->sin_family;
	out->sin_port = in->sin_port;
	out->sin_addr.s_addr = in->sin_addr.s_addr;
}

static void from_linux(const struct sockaddr_in *in, SceNetSockaddr *address, unsigned int *length)
{
	SceNetSockaddrIn out;

	memset(&out, 0, sizeof(out));
	out.sin_len = sizeof(out);
	out.sin_family = (unsigned char)in->sin_family;
	out.sin_port = in->sin_port;
	out.sin_addr.s_addr = in->sin_addr.s_addr;
	if (address && length)
	{
		memcpy(address, &out, *length < sizeof(out) ? *length : sizeof(out));
		*length = sizeof(out);
	}
}

/* ---------- sockets */

int sceNetSocket(const char *name, int domain, int type, int protocol)
{
	(void)name;
	return result_of(socket(domain, type, protocol));
}

int sceNetSocketClose(int s)
{
	return result_of(close(s));
}

int sceNetBind(int s, const SceNetSockaddr *address, unsigned int length)
{
	struct sockaddr_in in;

	if (!address || length < sizeof(SceNetSockaddrIn))
		return fail_with(22);
	to_linux(address, &in);
	return result_of(bind(s, (struct sockaddr *)&in, sizeof(in)));
}

int sceNetConnect(int s, const SceNetSockaddr *address, unsigned int length)
{
	struct sockaddr_in in;

	if (!address || length < sizeof(SceNetSockaddrIn))
		return fail_with(22);
	to_linux(address, &in);
	return result_of(connect(s, (struct sockaddr *)&in, sizeof(in)));
}

int sceNetListen(int s, int backlog)
{
	return result_of(listen(s, backlog));
}

int sceNetAccept(int s, SceNetSockaddr *address, unsigned int *length)
{
	struct sockaddr_in in;
	socklen_t in_length = sizeof(in);
	int result = accept(s, (struct sockaddr *)&in, &in_length);

	if (result >= 0)
		from_linux(&in, address, length);
	return result_of(result);
}

static int linux_flags(int flags)
{
	int out = 0;

	if (flags & SCE_NET_MSG_PEEK)
		out |= MSG_PEEK;
	if (flags & SCE_NET_MSG_DONTWAIT)
		out |= MSG_DONTWAIT;
	if (flags & SCE_NET_MSG_WAITALL)
		out |= MSG_WAITALL;
	return out | MSG_NOSIGNAL;
}

int sceNetSend(int s, const void *buffer, unsigned int length, int flags)
{
	return result_of((int)send(s, buffer, length, linux_flags(flags)));
}

int mock_scenet_sendto_calls, mock_scenet_eisconn_refusals;

int sceNetSendto(int s, const void *buffer, unsigned int length, int flags, const SceNetSockaddr *to,
	unsigned int to_length)
{
	struct sockaddr_in in, peer;
	socklen_t peer_length = sizeof(peer);
	int type = 0;
	socklen_t type_length = sizeof(type);
	const char *linux_sendto = getenv("MOCK_SCENET_LINUX_SENDTO");

	mock_scenet_sendto_calls++;
	if (!to)
		return result_of((int)sendto(s, buffer, length, linux_flags(flags), NULL, 0));
	if (to_length < sizeof(SceNetSockaddrIn))
		return fail_with(22);
	/* BSD's udp_output: a connected socket takes no destination */
	if (!(linux_sendto && atoi(linux_sendto)) &&
		getsockopt(s, SOL_SOCKET, SO_TYPE, &type, &type_length) == 0 && type == SOCK_DGRAM &&
		getpeername(s, (struct sockaddr *)&peer, &peer_length) == 0)
	{
		mock_scenet_eisconn_refusals++;
		return fail_with(56);
	}
	to_linux(to, &in);
	return result_of((int)sendto(s, buffer, length, linux_flags(flags), (struct sockaddr *)&in, sizeof(in)));
}

int sceNetRecv(int s, void *buffer, unsigned int length, int flags)
{
	return result_of((int)recv(s, buffer, length, linux_flags(flags)));
}

int sceNetRecvfrom(int s, void *buffer, unsigned int length, int flags, SceNetSockaddr *from, unsigned int *from_length)
{
	struct sockaddr_in in;
	socklen_t in_length = sizeof(in);
	int result;

	memset(&in, 0, sizeof(in));
	result = (int)recvfrom(s, buffer, length, linux_flags(flags), (struct sockaddr *)&in, &in_length);
	if (result >= 0)
		from_linux(&in, from, from_length);
	return result_of(result);
}

int sceNetShutdown(int s, int how)
{
	return result_of(shutdown(s, how));
}

static int linux_option(int level, int name, int *linux_level, int *linux_name)
{
	if (level == SCE_NET_SOL_SOCKET)
	{
		*linux_level = SOL_SOCKET;
		switch (name)
		{
		case SCE_NET_SO_REUSEADDR: *linux_name = SO_REUSEADDR; return 0;
		case SCE_NET_SO_KEEPALIVE: *linux_name = SO_KEEPALIVE; return 0;
		case SCE_NET_SO_BROADCAST: *linux_name = SO_BROADCAST; return 0;
		case SCE_NET_SO_LINGER: *linux_name = SO_LINGER; return 0;
		case SCE_NET_SO_SNDBUF: *linux_name = SO_SNDBUF; return 0;
		case SCE_NET_SO_RCVBUF: *linux_name = SO_RCVBUF; return 0;
		case SCE_NET_SO_ERROR: *linux_name = SO_ERROR; return 0;
		case SCE_NET_SO_TYPE: *linux_name = SO_TYPE; return 0;
		default: return -1;
		}
	}
	*linux_level = level;
	*linux_name = name;
	return 0;
}

int sceNetSetsockopt(int s, int level, int name, const void *value, unsigned int length)
{
	int linux_level, linux_name;

	if (level == SCE_NET_SOL_SOCKET && name == SCE_NET_SO_NBIO)
	{
		int flags = fcntl(s, F_GETFL);

		if (flags < 0)
			return result_of(-1);
		flags = *(const int *)value ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
		return result_of(fcntl(s, F_SETFL, flags));
	}
	if (linux_option(level, name, &linux_level, &linux_name) != 0)
		return fail_with(42);
	return result_of(setsockopt(s, linux_level, linux_name, value, length));
}

int sceNetGetsockopt(int s, int level, int name, void *value, unsigned int *length)
{
	int linux_level, linux_name, result;
	socklen_t linux_length = *length;

	if (linux_option(level, name, &linux_level, &linux_name) != 0)
		return fail_with(42);
	result = getsockopt(s, linux_level, linux_name, value, &linux_length);
	*length = linux_length;
	/* (a BSD SO_ERROR is the BSD errno) */
	if (result == 0 && linux_level == SOL_SOCKET && linux_name == SO_ERROR && *(int *)value)
		*(int *)value = bsd_errno(*(int *)value);
	return result_of(result);
}

int sceNetGetsockname(int s, SceNetSockaddr *address, unsigned int *length)
{
	struct sockaddr_in in;
	socklen_t in_length = sizeof(in);
	int result = getsockname(s, (struct sockaddr *)&in, &in_length);

	if (result >= 0)
		from_linux(&in, address, length);
	return result_of(result);
}

int sceNetGetpeername(int s, SceNetSockaddr *address, unsigned int *length)
{
	struct sockaddr_in in;
	socklen_t in_length = sizeof(in);
	int result = getpeername(s, (struct sockaddr *)&in, &in_length);

	if (result >= 0)
		from_linux(&in, address, length);
	return result_of(result);
}

/* ---------- epoll (timeouts in microseconds) */

int sceNetEpollCreate(const char *name, int flags)
{
	(void)name;
	(void)flags;
	return result_of(epoll_create1(0));
}

int sceNetEpollControl(int eid, int op, int id, SceNetEpollEvent *event)
{
	struct epoll_event linux_event;

	memset(&linux_event, 0, sizeof(linux_event));
	if (event)
	{
		if (event->events & SCE_NET_EPOLLIN)
			linux_event.events |= EPOLLIN;
		if (event->events & SCE_NET_EPOLLOUT)
			linux_event.events |= EPOLLOUT;
		if (event->events & SCE_NET_EPOLLERR)
			linux_event.events |= EPOLLERR;
		if (event->events & SCE_NET_EPOLLHUP)
			linux_event.events |= EPOLLHUP;
		linux_event.data.u32 = event->data.u32;
	}
	return result_of(epoll_ctl(eid, op == SCE_NET_EPOLL_CTL_ADD ? EPOLL_CTL_ADD :
		op == SCE_NET_EPOLL_CTL_MOD ? EPOLL_CTL_MOD : EPOLL_CTL_DEL, id, &linux_event));
}

int sceNetEpollWait(int eid, SceNetEpollEvent *events, int maxevents, int timeout)
{
	struct epoll_event linux_events[64];
	int index, result;

	if (maxevents > 64)
		maxevents = 64;
	result = epoll_wait(eid, linux_events, maxevents, timeout < 0 ? -1 : (timeout + 999) / 1000);
	for (index = 0; index < result; index++)
	{
		memset(&events[index], 0, sizeof(events[index]));
		if (linux_events[index].events & EPOLLIN)
			events[index].events |= SCE_NET_EPOLLIN;
		if (linux_events[index].events & EPOLLOUT)
			events[index].events |= SCE_NET_EPOLLOUT;
		if (linux_events[index].events & EPOLLERR)
			events[index].events |= SCE_NET_EPOLLERR;
		if (linux_events[index].events & EPOLLHUP)
			events[index].events |= SCE_NET_EPOLLHUP;
		events[index].data.u32 = linux_events[index].data.u32;
	}
	return result_of(result);
}

int sceNetEpollDestroy(int eid)
{
	return result_of(close(eid));
}

int sceNetGetStatisticsInfo(SceNetStatisticsInfo *info, int flags)
{
	(void)flags;
	memset(info, 0, sizeof(*info));
	info->libnet_mem_free_size = 7 * 1024 * 1024;
	info->libnet_mem_free_min = 6 * 1024 * 1024;
	return 0;
}

/* ---------- the library's start and addresses */

int sceNetInit(SceNetInitParam *param)
{
	(void)param;
	return 0;
}

int sceNetCtlInit(void)
{
	return 0;
}

int sceNetCtlInetGetInfo(int code, SceNetCtlInfo *info)
{
	(void)code;
	strcpy(info->ip_address, "192.168.1.50");
	return 0;
}

int sceNetInetPton(int af, const char *src, void *dst)
{
	return inet_pton(af, src, dst);
}

int sceNetResolverCreate(const char *name, SceNetResolverParam *param, int flags)
{
	(void)name;
	(void)param;
	(void)flags;
	return fail_with(45);
}

int sceNetResolverStartNtoa(int rid, const char *hostname, SceNetInAddr *addr, int timeout, int retry, int flags)
{
	(void)rid; (void)hostname; (void)addr; (void)timeout; (void)retry; (void)flags;
	return fail_with(45);
}

int sceNetResolverDestroy(int rid)
{
	(void)rid;
	return 0;
}

int sceSysmoduleLoadModule(SceSysmoduleModuleId id)
{
	(void)id;
	return 0;
}

/* ---------- stubs for the ad hoc probe and the self-test's thread */

int sceNetAdhocInit(void) { return -1; }
int sceNetAdhocctlInit(const SceNetAdhocctlAdhocId *adhoc_id) { (void)adhoc_id; return -1; }
int sceNetAdhocctlGetEtherAddr(SceNetEtherAddr *addr) { (void)addr; return -1; }
int sceNetAdhocctlGetPeerList(int *buflen, void *buf) { (void)buflen; (void)buf; return -1; }
int sceNetCtlAdhocGetState(int *state) { (void)state; return -1; }
int sceNetCtlAdhocGetInAddr(SceNetInAddr *inaddr) { (void)inaddr; return -1; }

SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int initPriority, SceSize stackSize,
	SceUInt attr, int cpuAffinityMask, const SceKernelThreadOptParam *option)
{
	(void)name; (void)entry; (void)initPriority; (void)stackSize; (void)attr; (void)cpuAffinityMask; (void)option;
	return -1;
}

int sceKernelStartThread(SceUID thid, SceSize arglen, void *argp)
{
	(void)thid; (void)arglen; (void)argp;
	return -1;
}

int sceKernelDelayThread(SceUInt delay)
{
	usleep(delay);
	return 0;
}

/* ---------- vita_host.h's, for vita_net.c */

int mock_log_quiet;
char mock_log_last[1024];
int mock_log_lines;

void vita_host_log(const char *line)
{
	snprintf(mock_log_last, sizeof(mock_log_last), "%s", line);
	mock_log_lines++;
	if (!mock_log_quiet)
		printf("  [log] %s\n", line);
}

void vita_host_sleep_us(unsigned long microseconds)
{
	usleep(microseconds);
}

unsigned long long vita_host_time_us(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long long)now.tv_sec * 1000000ULL + (unsigned long long)now.tv_nsec / 1000ULL;
}
