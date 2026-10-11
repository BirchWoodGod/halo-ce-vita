/*
STDLIB.H

Windows <stdlib.h>, which includes <limits.h>: if that defined the limits
the game's cseries.h declares itself, they are removed again (see
crt/limits.h).
*/

#ifndef LONG_MAX
#define HALO_WINDOWS_STDLIB_DEFINES_LIMITS
#endif

#include_next <stdlib.h>

#ifdef HALO_WINDOWS_STDLIB_DEFINES_LIMITS
#undef HALO_WINDOWS_STDLIB_DEFINES_LIMITS
#undef LONG_MAX
#undef LONG_MIN
#undef CHAR_MAX
#undef CHAR_MIN
#endif

/* POSIX setenv, which the C runtime does not have (main.c's @set test
command, menu_functions.c's Play page settings, map_share.c), over its
_putenv_s: port/windows/src/win32_posix.c */
int setenv(const char *name, const char *value, int overwrite);
