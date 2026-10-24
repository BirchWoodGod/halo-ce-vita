/* SPDX-License-Identifier: GPL-3.0-only
 * Local bot profile checks by kaboggs. No game files or console required. */
#include <assert.h>
#include "../../linux/game/local_bot_profile_rules.h"

int main(void)
{
    struct local_bot_profile_rules p;
    uint32_t seed;
    int a=0,b=0,both=0;
    for (seed=1;seed<=100000;++seed) {
        int i,j,star=0,kode=0;
        local_bot_profiles_make(&p,15,10,seed);
        for (i=0;i<15;++i) {
            if (p.kind[i]==1) {++star;assert(p.color[i]==6);}
            if (p.kind[i]==2) {++kode;assert(p.color[i]==12);}
            assert(p.guest[i]>=0 && p.guest[i]<40);
            assert(p.color[i]>=0 && p.color[i]<18);
            for (j=0;j<i;++j) {
                assert(p.guest[i]!=p.guest[j]);
                assert(p.color[i]!=p.color[j]);
            }
        }
        assert(star<=1 && kode<=1);
        a+=star; b+=kode; both+=star && kode;
    }
    assert(a>9000 && a<11000 && b>9000 && b<11000);
    assert(both>700 && both<1300);
    for (seed=1;seed<=1000;++seed) {
        local_bot_profiles_make(&p,1,100,seed);
        assert(p.kind[0]==1 || p.kind[0]==2);
        local_bot_profiles_make(&p,15,0,seed);
        assert(p.kind[0]==0 && p.kind[14]==0);
    }
    return 0;
}
