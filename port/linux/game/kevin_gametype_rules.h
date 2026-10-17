/* Personal gametype rules by kaboggs. Saved with the native game variant.
 * The original weapon/vehicle set values and wire layouts are unchanged.
 * Bits 24..27 were unused in universal_variant.flags; only local games apply
 * these additions. Other builds ignore them and retain the base native set. */
#ifndef KEVIN_GAMETYPE_RULES_H
#define KEVIN_GAMETYPE_RULES_H
#include <stdint.h>
#include <string.h>

enum { KEVIN_VEHICLE_ALL=1, KEVIN_VEHICLE_BANSHEES=2, KEVIN_VEHICLE_ROCKET_HOGS=3 };
enum { KEVIN_WEAPON_BOTH=1, KEVIN_WEAPON_FLAME=2, KEVIN_WEAPON_FUEL=3 };
enum { KEVIN_HOG=0, KEVIN_GHOST, KEVIN_TANK, KEVIN_ROCKET_HOG, KEVIN_BANSHEE, KEVIN_TURRET,
       KEVIN_VEHICLE_TYPES, KEVIN_FLAME=6, KEVIN_FUEL=7, KEVIN_ITEM_TYPES=8 };

/* The shipped Team Ball profile fills reserved bits with ones. It predates
 * this extension and must never opt in to PC spawns merely by being loaded. */
static __inline uint32_t kevin_variant_flags(uint32_t flags)
{
    return flags==UINT32_C(0xffffffe3)?flags&UINT32_C(0x00ffffff):flags;
}
static __inline unsigned kevin_vehicle_mode(uint32_t flags) {return (kevin_variant_flags(flags)>>24)&3u;}
static __inline unsigned kevin_weapon_mode(uint32_t flags) {return (kevin_variant_flags(flags)>>26)&3u;}

/* kaboggs: encode/decode retain every unrelated option and valid native
 * values. The custom vehicle-only choices suppress original vehicles. */
static __inline uint32_t kevin_vehicle_choice_set(uint32_t flags,int choice,int *native)
{
    flags=kevin_variant_flags(flags);
    flags &= ~(3u<<24);
    if (choice>=5 && choice<=7) {
        flags |= (uint32_t)(choice-4)<<24;
        *native=choice==5?0:1;
    } else *native=choice>=0 && choice<=4?choice:0;
    return flags;
}
static __inline int kevin_vehicle_choice_get(uint32_t flags,int native)
{
    unsigned mode=kevin_vehicle_mode(flags);
    return mode?(int)mode+4:(native>=0 && native<=4?native:0);
}
static __inline uint32_t kevin_weapon_choice_set(uint32_t flags,int choice,int *native)
{
    flags=kevin_variant_flags(flags);
    flags &= ~(3u<<26);
    if (choice>=11 && choice<=13) {
        flags |= (uint32_t)(choice-10)<<26;
        *native=0;
    } else *native=choice>=0 && choice<=10?choice:0;
    return flags;
}
static __inline int kevin_weapon_choice_get(uint32_t flags,int native)
{
    unsigned mode=kevin_weapon_mode(flags);
    return mode?(int)mode+10:(native>=0 && native<=10?native:0);
}

/* kaboggs: the supported stock layouts have vehicle pads and outdoor
 * room. Never turn a small indoor arena into a vehicle map by tag presence. */
static __inline int kevin_vehicle_map_allowed(const char *name)
{
    static const char *const names[]={"bloodgulch","sidewinder","deathisland",
        "dangercanyon","icefields","infinity","timberland","gephyrophobia"};
    unsigned i;
    for (i=0;name && i<sizeof(names)/sizeof(names[0]);++i)
        if (!strcmp(name,names[i])) return 1;
    return 0;
}
static __inline unsigned kevin_vehicle_mode_mask(unsigned mode)
{
    return mode==KEVIN_VEHICLE_ALL?63u:mode==KEVIN_VEHICLE_BANSHEES?(1u<<KEVIN_BANSHEE):
        mode==KEVIN_VEHICLE_ROCKET_HOGS?(1u<<KEVIN_ROCKET_HOG):0;
}
static __inline unsigned kevin_weapon_mode_mask(unsigned mode)
{
    return mode==KEVIN_WEAPON_BOTH?((1u<<KEVIN_FLAME)|(1u<<KEVIN_FUEL)):
        mode==KEVIN_WEAPON_FLAME?(1u<<KEVIN_FLAME):mode==KEVIN_WEAPON_FUEL?(1u<<KEVIN_FUEL):0;
}
#endif
