/* Vita atomic ABI and concurrency checks by kaboggs. */
#include "../host/vita_atomics.h"
#include <assert.h>
#include <pthread.h>
#include <string.h>

static unsigned char packed[12] __attribute__((aligned(4)));
static uint32_t aligned;

static void *increment(void *argument)
{
    unsigned int i;
    volatile void *p=argument;
    for (i=0;i<10000;++i) {
        uint32_t expected=vita_atomic_load4(p,__ATOMIC_ACQUIRE);
        while (!vita_atomic_cas4(p,&expected,expected+1,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {}
    }
    return NULL;
}

int main(void)
{
    pthread_t threads[4];
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
        vita_atomic_store4(p,0,__ATOMIC_SEQ_CST);
        for (i=0;i<4;++i) assert(!pthread_create(&threads[i],NULL,increment,p));
        for (i=0;i<4;++i) assert(!pthread_join(threads[i],NULL));
        assert(vita_atomic_load4(p,__ATOMIC_SEQ_CST)==40000);
    }
    assert(packed[0]==0x7a && packed[5]==0x7a);
    return 0;
}
