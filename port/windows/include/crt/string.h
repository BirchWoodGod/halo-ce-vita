/*
STRING.H

Windows <string.h>. The game defines its own strnlen with a different
signature (source/cseries/cseries.h), which halo_windows_prefix.h renames in
game code so that it can coexist with the C runtime's; the runtime's is
declared here under its own name.
*/

#pragma push_macro("strnlen")
#undef strnlen
#include_next <string.h>
#pragma pop_macro("strnlen")

/* POSIX strtok_r (xinput_sdl.c's HALO_TEST_PAD): the C runtime's strtok_s
has its arguments and meaning */
#ifndef strtok_r
#define strtok_r strtok_s
#endif
