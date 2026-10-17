/* Vita bot profile additions by kaboggs; adapted from his New3DS profiles. */
#ifndef KEVIN_BOT_PROFILES_H
#define KEVIN_BOT_PROFILES_H
#include <stdint.h>

struct kevin_bot_profiles { int kind[15], guest[15], color[15]; };

/* kaboggs: kevin_profile_random - isolated visual/profile RNG, not gameplay's seed. */
static __inline uint32_t kevin_profile_random(uint32_t *seed)
{
    uint32_t value=*seed?*seed:0x9e3779b9u;
    value^=value<<13; value^=value>>17; value^=value<<5;
    *seed=value;
    return value;
}

/* kaboggs: kevin_profiles_make - independent encounter rolls, unique slots/names/colors. */
static __inline void kevin_profiles_make(struct kevin_bot_profiles *out, int count, int chance, uint32_t seed)
{
    int guests[40], colors[16], i, a=-1, b=-1, color=0;
    for (i=0;i<15;++i) {out->kind[i]=0;out->guest[i]=i;out->color[i]=i;}
    if (count<1 || count>15 || chance<0 || chance>100) return;
    if ((int)(kevin_profile_random(&seed)%100)<chance) a=(int)(kevin_profile_random(&seed)%(uint32_t)count);
    if ((int)(kevin_profile_random(&seed)%100)<chance) b=(int)(kevin_profile_random(&seed)%(uint32_t)count);
    if (a>=0 && a==b) {
        if (count>1) b=(a+1+(int)(kevin_profile_random(&seed)%(uint32_t)(count-1)))%count;
        else if (kevin_profile_random(&seed)&1u) a=-1;
        else b=-1;
    }
    for (i=0;i<40;++i) guests[i]=i;
    for (i=39;i>0;--i) {int j=(int)(kevin_profile_random(&seed)%(uint32_t)(i+1));int v=guests[i];guests[i]=guests[j];guests[j]=v;}
    for (i=0;i<18;++i) if (i!=6 && i!=12) colors[color++]=i;
    for (i=15;i>0;--i) {int j=(int)(kevin_profile_random(&seed)%(uint32_t)(i+1));int v=colors[i];colors[i]=colors[j];colors[j]=v;}
    color=0;
    for (i=0;i<count;++i) {
        out->kind[i]=i==a?1:i==b?2:0;
        out->guest[i]=guests[i];
        out->color[i]=i==a?6:i==b?12:colors[color++];
    }
}
#endif
