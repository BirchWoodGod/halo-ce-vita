/* SPDX-License-Identifier: GPL-3.0-only
 * Vita compiler-runtime integration additions by kaboggs. */
#ifndef VITA_ATOMICS_H
#define VITA_ATOMICS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VITA_DECLARE_ATOMICS(n,t) \
t vita_atomic_load##n(const volatile void *,int) __asm__("__atomic_load_" #n); \
void vita_atomic_store##n(volatile void *,t,int) __asm__("__atomic_store_" #n); \
t vita_atomic_exchange##n(volatile void *,t,int) __asm__("__atomic_exchange_" #n); \
bool vita_atomic_cas##n(volatile void *,void *,t,int,int) __asm__("__atomic_compare_exchange_" #n); \
t vita_atomic_and##n(volatile void *,t,int) __asm__("__atomic_fetch_and_" #n); \
t vita_atomic_or##n(volatile void *,t,int) __asm__("__atomic_fetch_or_" #n)
VITA_DECLARE_ATOMICS(2,uint16_t);
VITA_DECLARE_ATOMICS(4,uint32_t);
#undef VITA_DECLARE_ATOMICS

void vita_atomic_load_generic(size_t,const void *,void *,int) __asm__("__atomic_load");
void vita_atomic_store_generic(size_t,void *,const void *,int) __asm__("__atomic_store");
void vita_atomic_exchange_generic(size_t,void *,const void *,void *,int) __asm__("__atomic_exchange");
bool vita_atomic_cas_generic(size_t,void *,void *,const void *,int,int) __asm__("__atomic_compare_exchange");
#endif
