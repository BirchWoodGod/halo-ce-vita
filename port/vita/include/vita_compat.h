/* vita_compat.h: what newlib lacks, provided by port/vita/host/vita_stubs.c */
#ifndef __HALO_VITA_COMPAT_H
#define __HALO_VITA_COMPAT_H

#include <time.h>

#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 4
#endif

int clock_nanosleep(clockid_t clock, int flags, const struct timespec *request, struct timespec *remain);

#endif
