/* Vita combat effects additions by kaboggs. No game data needed. */
#include <assert.h>
#include <math.h>
#include "../../linux/game/kevin_fx.h"

int main(void)
{
    float step;
    int ticks=0;
    float progress=0.f;
    assert(kevin_trace_kind("weapons\\assault rifle\\bullet")==1);
    assert(kevin_trace_kind("weapons\\sniper rifle\\sniper")==2);
    assert(kevin_trace_kind(NULL)==0);
    assert(kevin_trace_kind("effects\\waterfall")==0);
    assert(kevin_combat_fx_name("effects\\decals\\blood splatter"));
    assert(kevin_combat_fx_name("effects\\decals\\scorch"));
    assert(kevin_combat_fx_name("weapons\\flamethrower\\effects\\fire"));
    assert(!kevin_combat_fx_name("effects\\waterfall"));
    assert(kevin_trace_width(1,.005f,0.f)>=.025f);
    assert(kevin_trace_width(1,.005f,10000.f)<=.12f);
    assert(kevin_trace_width(2,.01f,10000.f)<=.2f);
    assert(kevin_trace_width(1,.5f,1.f)==.5f);
    assert(kevin_trace_width(0,.005f,100.f)==.005f);
    assert(kevin_trace_step(1.f/30.f,0.f,.0125f,0,1,0)==0.f);
    while (progress<1.f && ticks<10) {
        step=kevin_trace_step(1.f/30.f,0.f,.0125f,0,0,1);
        progress+=step/.0125f;
        ++ticks;
    }
    assert(ticks>=3 && ticks<=4);
    assert(kevin_trace_step(1.f/30.f,.1f,.5f,0,0,1)==1.f/30.f);
    assert(kevin_trace_step(1.f/30.f,0.f,.0125f,1,0,1)==1.f/30.f);
    return 0;
}
