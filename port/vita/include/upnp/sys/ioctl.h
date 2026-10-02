/* (miniupnpc on the Vita: see vita_upnp_compat.h; only a named
interface's address is asked for this way, and never on the Vita) */
#ifndef __VITA_UPNP_SYS_IOCTL_H
#define __VITA_UPNP_SYS_IOCTL_H

static inline int ioctl(int descriptor, unsigned long request, ...)
{
	(void)descriptor;
	(void)request;
	return -1;
}

#endif
