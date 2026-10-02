/* (miniupnpc on the Vita: see vita_upnp_compat.h; there is no minissdpd
socket to connect to, and the attempt fails) */
#ifndef __VITA_UPNP_SYS_UN_H
#define __VITA_UPNP_SYS_UN_H

#include <sys/socket.h>

struct sockaddr_un
{
	sa_family_t sun_family;
	char sun_path[108];
};

#endif
