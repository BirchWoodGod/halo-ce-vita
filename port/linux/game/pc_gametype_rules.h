/* Personal gametype rules by kaboggs. Saved with the native game variant.
 * The original weapon/vehicle set values and wire layouts are unchanged.
 * Bits 24..27 were unused in universal_variant.flags; only local games apply
 * these additions. Other builds ignore them and retain the base native set. */
#ifndef PC_GAMETYPE_RULES_H
#define PC_GAMETYPE_RULES_H
#include <stdint.h>
#include <string.h>

enum { PC_VEHICLE_ALL=1, PC_VEHICLE_BANSHEES=2, PC_VEHICLE_ROCKET_HOGS=3 };
enum { PC_WEAPON_BOTH=1, PC_WEAPON_FLAME=2, PC_WEAPON_FUEL=3 };
enum { PC_HOG=0, PC_GHOST, PC_TANK, PC_ROCKET_HOG, PC_BANSHEE, PC_TURRET,
       PC_VEHICLE_TYPES, PC_FLAME=6, PC_FUEL=7, PC_ITEM_TYPES=8 };

/* The shipped Team Ball profile fills reserved bits with ones. It predates
 * this extension and must never opt in to PC spawns merely by being loaded. */
static __inline uint32_t pc_variant_flags(uint32_t flags)
{
    return flags==UINT32_C(0xffffffe3)?flags&UINT32_C(0x00ffffff):flags;
}
static __inline unsigned pc_vehicle_mode(uint32_t flags) {return (pc_variant_flags(flags)>>24)&3u;}
static __inline unsigned pc_weapon_mode(uint32_t flags) {return (pc_variant_flags(flags)>>26)&3u;}

/* kaboggs: encode/decode retain every unrelated option and valid native
 * values. The custom vehicle-only choices suppress original vehicles. */
static __inline uint32_t pc_vehicle_choice_set(uint32_t flags,int choice,int *native)
{
    flags=pc_variant_flags(flags);
    flags &= ~(3u<<24);
    if (choice>=5 && choice<=7) {
        flags |= (uint32_t)(choice-4)<<24;
        *native=choice==5?0:1;
    } else *native=choice>=0 && choice<=4?choice:0;
    return flags;
}
static __inline int pc_vehicle_choice_get(uint32_t flags,int native)
{
    unsigned mode=pc_vehicle_mode(flags);
    return mode?(int)mode+4:(native>=0 && native<=4?native:0);
}
static __inline uint32_t pc_weapon_choice_set(uint32_t flags,int choice,int *native)
{
    flags=pc_variant_flags(flags);
    flags &= ~(3u<<26);
    if (choice>=11 && choice<=13) {
        flags |= (uint32_t)(choice-10)<<26;
        *native=0;
    } else *native=choice>=0 && choice<=10?choice:0;
    return flags;
}
static __inline int pc_weapon_choice_get(uint32_t flags,int native)
{
    unsigned mode=pc_weapon_mode(flags);
    return mode?(int)mode+10:(native>=0 && native<=10?native:0);
}

/* kaboggs: the supported stock layouts have vehicle pads and outdoor
 * room. Never turn a small indoor arena into a vehicle map by tag presence. */
static __inline int pc_vehicle_map_allowed(const char *name)
{
    static const char *const names[]={"bloodgulch","sidewinder","deathisland",
        "dangercanyon","icefields","infinity","timberland","gephyrophobia"};
    unsigned i;
    for (i=0;name && i<sizeof(names)/sizeof(names[0]);++i)
        if (!strcmp(name,names[i])) return 1;
    return 0;
}
static __inline unsigned pc_vehicle_mode_mask(unsigned mode)
{
    return mode==PC_VEHICLE_ALL?63u:mode==PC_VEHICLE_BANSHEES?(1u<<PC_BANSHEE):
        mode==PC_VEHICLE_ROCKET_HOGS?(1u<<PC_ROCKET_HOG):0;
}
static __inline unsigned pc_weapon_mode_mask(unsigned mode)
{
    return mode==PC_WEAPON_BOTH?((1u<<PC_FLAME)|(1u<<PC_FUEL)):
        mode==PC_WEAPON_FLAME?(1u<<PC_FLAME):mode==PC_WEAPON_FUEL?(1u<<PC_FUEL):0;
}
#endif
