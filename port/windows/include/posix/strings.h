/* POSIX <strings.h>: the case-insensitive comparisons, which the C runtime
names differently */

#ifndef __HALO_WINDOWS_STRINGS_H
#define __HALO_WINDOWS_STRINGS_H

#include <string.h>

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#endif
