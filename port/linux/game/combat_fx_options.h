/* Vita combat effects additions by kaboggs.
 * These helpers change only local visual ribbons, never projectile motion/damage.
 */
#ifndef COMBAT_FX_OPTIONS_H
#define COMBAT_FX_OPTIONS_H
#include <stdlib.h>
#include <string.h>

/* kaboggs: combat_fx_name - preserve authored combat details under budgets. */
static __inline int combat_fx_name(const char *name)
{
    return name && (strstr(name,"weapons\\") || strstr(name,"grenade") ||
        strstr(name,"explosion") || strstr(name,"impact") || strstr(name,"shield") ||
        strstr(name,"bullet") || strstr(name,"tracer") || strstr(name,"muzzle") ||
        strstr(name,"blood") || strstr(name,"spark") || strstr(name,"scorch") || strstr(name,"flame"));
}

/* kaboggs: combat_fx_preserved - independent of ambient particle density. */
static __inline int combat_fx_preserved(const char *name)
{
    const char *value=getenv("HALO_COMBAT_FX");
    return (value && atoi(value)!=0) && combat_fx_name(name);
}

/* kaboggs: combat_tracers_enabled - original fidelity remains selectable. */
static __inline int combat_tracers_enabled(void)
{
    const char *value=getenv("HALO_COMBAT_TRACERS");
    return value && atoi(value)!=0;
}

/* kaboggs: combat_trace_kind - match authored rifle effects, not all contrails. */
static __inline int combat_trace_kind(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name,"weapons\\assault rifle\\bullet")) return 1;
    if (!strcmp(name,"weapons\\sniper rifle\\sniper")) return 2;
    return 0;
}

/* kaboggs: combat_trace_width - bounded screen visibility at Vita resolution. */
static __inline float combat_trace_width(int kind, float authored, float distance)
{
    float minimum=0.f;
    if (kind==1) {
        minimum=distance*.0015f;
        if (minimum<.025f) minimum=.025f;
        if (minimum>.12f) minimum=.12f;
    } else if (kind==2) {
        minimum=distance*.002f;
        if (minimum>.2f) minimum=.2f;
    }
    return authored<minimum?minimum:authored;
}

/* kaboggs: combat_trace_step - keep a sub-tick rifle ribbon visible for three ticks. */
static __inline float combat_trace_step(float dt, float first_duration, float transition,
    int state, int fresh, int transitioning)
{
    if (state==0 && first_duration<=0.f && transition>0.f && transition<1.f/30.f) {
        if (fresh) return 0.f;
        if (transitioning && dt>transition/3.f) return transition/3.f;
    }
    return dt;
}
#endif
