/* SPDX-License-Identifier: GPL-3.0-only
 * Vita atomic ABI and concurrency checks by kaboggs. */
#include "../host/vita_atomics.h"
#include <assert.h>
#include <pthread.h>
#include <string.h>

static unsigned char packed[12] __attribute__((aligned(4)));
static uint32_t aligned;
static uint16_t aligned_short;

struct increment_task { void *object; int width, generic; };

static void *increment(void *argument)
{
    unsigned int i;
    struct increment_task *task=argument;
    void *p=task->object;
    for (i=0;i<10000;++i) {
        if (task->width==2) {
            uint16_t expected, desired;
            if (task->generic) {
                vita_atomic_load_generic(2,p,&expected,__ATOMIC_ACQUIRE);
                do {desired=expected+1;}
                while (!vita_atomic_cas_generic(2,p,&expected,&desired,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
            } else {
                expected=vita_atomic_load2(p,__ATOMIC_ACQUIRE);
                while (!vita_atomic_cas2(p,&expected,expected+1,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {}
            }
        } else {
            uint32_t expected, desired;
            if (task->generic) {
                vita_atomic_load_generic(4,p,&expected,__ATOMIC_ACQUIRE);
                do {desired=expected+1;}
                while (!vita_atomic_cas_generic(4,p,&expected,&desired,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
            } else {
                expected=vita_atomic_load4(p,__ATOMIC_ACQUIRE);
                while (!vita_atomic_cas4(p,&expected,expected+1,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {}
            }
        }
    }
    return NULL;
}

int main(void)
{
    pthread_t threads[4];
    struct increment_task tasks[4];
    int i,pass;
    uint32_t expected,value,prior;
    uint16_t short_expected;
    memset(packed,0x7a,sizeof(packed));
    vita_atomic_store2(packed+1,0x1234,__ATOMIC_RELEASE);
    assert(vita_atomic_load2(packed+1,__ATOMIC_ACQUIRE)==0x1234);
    assert(vita_atomic_or2(packed+1,0x8000,__ATOMIC_ACQ_REL)==0x1234);
    assert(vita_atomic_and2(packed+1,0xff00,__ATOMIC_ACQ_REL)==0x9234);
    short_expected=0x9200;
    assert(vita_atomic_cas2(packed+1,&short_expected,0x4321,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
    assert(vita_atomic_exchange2(packed+1,0xabcd,__ATOMIC_SEQ_CST)==0x4321);
    short_expected=0x1111;
    assert(!vita_atomic_cas2(packed+1,&short_expected,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
    assert(short_expected==0xabcd);
    assert(packed[0]==0x7a && packed[3]==0x7a);
    for (pass=0;pass<2;++pass) {
        void *p=pass?(void *)&aligned:(void *)(packed+1);
        vita_atomic_store4(p,7,__ATOMIC_SEQ_CST);
        expected=2;
        assert(!vita_atomic_cas4(p,&expected,9,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
        assert(expected==7 && vita_atomic_load4(p,__ATOMIC_SEQ_CST)==7);
        value=11;
        vita_atomic_exchange_generic(4,p,&value,&prior,__ATOMIC_SEQ_CST);
        assert(prior==7);
        vita_atomic_load_generic(4,p,&prior,__ATOMIC_SEQ_CST);
        assert(prior==11);
        value=0xf0f0;
        vita_atomic_store_generic(4,p,&value,__ATOMIC_RELEASE);
        assert(vita_atomic_or4(p,0x0f0f,__ATOMIC_SEQ_CST)==0xf0f0);
        assert(vita_atomic_and4(p,0xff00,__ATOMIC_SEQ_CST)==0xffff);
        vita_atomic_store4(p,0,__ATOMIC_SEQ_CST);
        for (i=0;i<4;++i) {
            tasks[i]=(struct increment_task){p,4,i&1};
            assert(!pthread_create(&threads[i],NULL,increment,&tasks[i]));
        }
        for (i=0;i<4;++i) assert(!pthread_join(threads[i],NULL));
        assert(vita_atomic_load4(p,__ATOMIC_SEQ_CST)==40000);
    }
    for (pass=0;pass<2;++pass) {
        void *p=pass?(void *)&aligned_short:(void *)(packed+1);
        short_expected=0;
        vita_atomic_store_generic(2,p,&short_expected,__ATOMIC_RELEASE);
        for (i=0;i<4;++i) {
            tasks[i]=(struct increment_task){p,2,i&1};
            assert(!pthread_create(&threads[i],NULL,increment,&tasks[i]));
        }
        for (i=0;i<4;++i) assert(!pthread_join(threads[i],NULL));
        assert(vita_atomic_load2(p,__ATOMIC_ACQUIRE)==40000);
    }
    /* Generic-only fallback widths must preserve the same ABI semantics. */
    for (pass=1;pass<=8;++pass) {
        unsigned char expected_bytes[8], desired_bytes[8], actual[8];
        memset(expected_bytes,0x11,sizeof(expected_bytes));
        memset(desired_bytes,0x22,sizeof(desired_bytes));
        vita_atomic_store_generic(pass,packed+1,expected_bytes,__ATOMIC_RELEASE);
        vita_atomic_exchange_generic(pass,packed+1,desired_bytes,actual,__ATOMIC_ACQ_REL);
        assert(!memcmp(actual,expected_bytes,pass));
        assert(!vita_atomic_cas_generic(pass,packed+1,expected_bytes,desired_bytes,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
        assert(!memcmp(expected_bytes,desired_bytes,pass));
        assert(vita_atomic_cas_generic(pass,packed+1,expected_bytes,actual,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
        vita_atomic_load_generic(pass,packed+1,expected_bytes,__ATOMIC_ACQUIRE);
        assert(!memcmp(expected_bytes,actual,pass));
    }
    assert(packed[0]==0x7a && packed[9]==0x7a);
    return 0;
}
