/*
VITA_UPNP_COMPAT.H

What miniupnpc (port/third_party/miniupnpc, for internet play's UPnP:
port/linux/src/posix_upnp.c) needs from a BSD system that VitaSDK's newlib
lacks, force-included in its build (tools/vita_build.py). Only its IPv4
search with no named interface runs on the Vita (posix_upnp.c asks for
nothing else), so the IPv6 and interface-name paths need only compile:
their constants and in6addr_any are placeholders, and net/if.h, sys/un.h
and sys/ioctl.h beside this file declare just enough for the code that
names them (minissdpd's local socket, which does not exist on the Vita,
and SIOCGIFADDR for a named interface).
*/

#ifndef __VITA_UPNP_COMPAT_H
#define __VITA_UPNP_COMPAT_H

#include <netinet/in.h>

#define IPV6_MULTICAST_IF 9
#define IPV6_MULTICAST_HOPS 10
#define IFNAMSIZ 16

static const struct in6_addr in6addr_any;

#endif
