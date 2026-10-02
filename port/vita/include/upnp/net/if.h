/* (miniupnpc on the Vita: see vita_upnp_compat.h; no interface names) */
#ifndef __VITA_UPNP_NET_IF_H
#define __VITA_UPNP_NET_IF_H

#include <sys/socket.h>

struct ifreq
{
	char ifr_name[16];
	struct sockaddr ifr_addr;
};

#define SIOCGIFADDR 0

static inline unsigned int if_nametoindex(const char *name)
{
	(void)name;
	return 0;
}

#endif
