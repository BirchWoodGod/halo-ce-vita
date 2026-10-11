/*
OGG/OS_TYPES.H

(port) In place of libogg's include/ogg/os_types.h (and the config_types.h
its build generates): libogg's and Tremor's integer types from <stdint.h>,
and their allocations (_ogg_malloc and the rest) from the working memory
that port/linux/src/ogg_sound.c gives the decoder, never the C heap. Those
allocators do not return NULL: an allocation that does not fit ends the
decoding (ogg_sound.c). Tremor's alloca is the compiler's.
*/

#ifndef _OS_TYPES_H
#define _OS_TYPES_H

#include <stddef.h>
#include <stdint.h>

void *halo_ogg_malloc(size_t bytes);
void *halo_ogg_calloc(size_t count, size_t size);
void *halo_ogg_realloc(void *pointer, size_t bytes);
void halo_ogg_free(void *pointer);

#define _ogg_malloc halo_ogg_malloc
#define _ogg_calloc halo_ogg_calloc
#define _ogg_realloc halo_ogg_realloc
#define _ogg_free halo_ogg_free

typedef int16_t ogg_int16_t;
typedef uint16_t ogg_uint16_t;
typedef int32_t ogg_int32_t;
typedef uint32_t ogg_uint32_t;
typedef int64_t ogg_int64_t;
typedef uint64_t ogg_uint64_t;

#if (defined(__GNUC__) || defined(__clang__)) && !defined(alloca)
#define alloca __builtin_alloca
#endif

#endif
