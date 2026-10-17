/* Vita compiler-runtime integration additions by kaboggs.
 * Extends the generic unaligned-field support previously in vita_stubs.c.
 * Clang 18 emits sized __atomic_*_2/_4 calls with -fmax-type-align=1.
 * Aligned operands use GCC's ARMv7 atomic instructions, so they interoperate
 * with inline atomics in the SDK host. Unaligned operands share one lock and
 * use memcpy; never issue an unaligned LDREX/STREX or an ordinary racing load.
 * Sized compare-exchange's library ABI has no weak argument (strong is valid).
 * ABI reference: LLVM compiler-rt/lib/builtins/atomic.c; no LLVM code copied.
 */
#include "vita_atomics.h"
#include <string.h>

static volatile unsigned int unaligned_lock;

/* kaboggs: unaligned_acquire - serialize all generic/sized fallback operations. */
static void unaligned_acquire(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    while (__atomic_load_n(&unaligned_lock,__ATOMIC_RELAXED) ||
           __atomic_exchange_n(&unaligned_lock,1,__ATOMIC_ACQUIRE)) {}
}

/* kaboggs: unaligned_release - conservative sequentially consistent fallback. */
static void unaligned_release(void)
{
    __atomic_store_n(&unaligned_lock,0,__ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

/* kaboggs: sized runtime operations - this file is built by SDK GCC,
 * without the game's -fmax-type-align=1 flag, so these builtins stay inline. */
#define VITA_ATOMIC_WIDTH(n,t) \
t vita_atomic_load##n(const volatile void *p,int order) { \
    t value; \
    if (!((uintptr_t)p&(sizeof(t)-1))) return __atomic_load_n((const volatile t *)p,order); \
    unaligned_acquire();memcpy(&value,(const void *)p,sizeof(value));unaligned_release();return value; \
} \
void vita_atomic_store##n(volatile void *p,t value,int order) { \
    if (!((uintptr_t)p&(sizeof(t)-1))) {__atomic_store_n((volatile t *)p,value,order);return;} \
    unaligned_acquire();memcpy((void *)p,&value,sizeof(value));unaligned_release(); \
} \
t vita_atomic_exchange##n(volatile void *p,t value,int order) { \
    t prior; \
    if (!((uintptr_t)p&(sizeof(t)-1))) return __atomic_exchange_n((volatile t *)p,value,order); \
    unaligned_acquire();memcpy(&prior,(const void *)p,sizeof(prior));memcpy((void *)p,&value,sizeof(value));unaligned_release();return prior; \
} \
bool vita_atomic_cas##n(volatile void *p,void *expected,t desired,int success,int failure) { \
    t prior,wanted;bool matched;memcpy(&wanted,expected,sizeof(wanted)); \
    if (!((uintptr_t)p&(sizeof(t)-1))) { \
        matched=__atomic_compare_exchange_n((volatile t *)p,&wanted,desired,false,success,failure); \
        if (!matched) {memcpy(expected,&wanted,sizeof(wanted));} return matched; \
    } \
    unaligned_acquire();memcpy(&prior,(const void *)p,sizeof(prior));matched=prior==wanted; \
    if (matched) memcpy((void *)p,&desired,sizeof(desired));else memcpy(expected,&prior,sizeof(prior)); \
    unaligned_release();return matched; \
} \
t vita_atomic_and##n(volatile void *p,t value,int order) { \
    t prior,next; \
    if (!((uintptr_t)p&(sizeof(t)-1))) return __atomic_fetch_and((volatile t *)p,value,order); \
    unaligned_acquire();memcpy(&prior,(const void *)p,sizeof(prior));next=prior&value;memcpy((void *)p,&next,sizeof(next));unaligned_release();return prior; \
} \
t vita_atomic_or##n(volatile void *p,t value,int order) { \
    t prior,next; \
    if (!((uintptr_t)p&(sizeof(t)-1))) return __atomic_fetch_or((volatile t *)p,value,order); \
    unaligned_acquire();memcpy(&prior,(const void *)p,sizeof(prior));next=prior|value;memcpy((void *)p,&next,sizeof(next));unaligned_release();return prior; \
}
VITA_ATOMIC_WIDTH(2,uint16_t)
VITA_ATOMIC_WIDTH(4,uint32_t)
#undef VITA_ATOMIC_WIDTH

/* kaboggs: generic entry points retain the old ABI and use the same
 * aligned paths and unaligned lock as the new sized entry points. */
void vita_atomic_load_generic(size_t size,const void *p,void *result,int order)
{
    if (size==2) {uint16_t v=vita_atomic_load2(p,order);memcpy(result,&v,2);return;}
    if (size==4) {uint32_t v=vita_atomic_load4(p,order);memcpy(result,&v,4);return;}
    unaligned_acquire();memcpy(result,p,size);unaligned_release();
}
void vita_atomic_store_generic(size_t size,void *p,const void *value,int order)
{
    if (size==2) {uint16_t v;memcpy(&v,value,2);vita_atomic_store2(p,v,order);return;}
    if (size==4) {uint32_t v;memcpy(&v,value,4);vita_atomic_store4(p,v,order);return;}
    unaligned_acquire();memcpy(p,value,size);unaligned_release();
}
void vita_atomic_exchange_generic(size_t size,void *p,const void *value,void *result,int order)
{
    if (size==2) {uint16_t v,prior;memcpy(&v,value,2);prior=vita_atomic_exchange2(p,v,order);memcpy(result,&prior,2);return;}
    if (size==4) {uint32_t v,prior;memcpy(&v,value,4);prior=vita_atomic_exchange4(p,v,order);memcpy(result,&prior,4);return;}
    unaligned_acquire();memcpy(result,p,size);memcpy(p,value,size);unaligned_release();
}
bool vita_atomic_cas_generic(size_t size,void *p,void *expected,const void *desired,int success,int failure)
{
    bool matched;
    if (size==2) {uint16_t v;memcpy(&v,desired,2);return vita_atomic_cas2(p,expected,v,success,failure);}
    if (size==4) {uint32_t v;memcpy(&v,desired,4);return vita_atomic_cas4(p,expected,v,success,failure);}
    unaligned_acquire();matched=memcmp(p,expected,size)==0;
    if (matched) memcpy(p,desired,size);else memcpy(expected,p,size);
    unaligned_release();return matched;
}
