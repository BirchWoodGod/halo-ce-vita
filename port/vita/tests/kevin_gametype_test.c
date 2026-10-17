/* Saved gametype compatibility and selection checks by kaboggs. */
#include "../../linux/game/kevin_gametype_rules.h"
#include <assert.h>
int main(void)
{
    int v,w,native;uint32_t flags,encoded;
    const uint32_t unrelated=0xA0123456u;
    for (v=0;v<8;++v) for (w=0;w<14;++w) {
        flags=kevin_vehicle_choice_set(unrelated,v,&native);
        assert(native>=0 && native<=4);
        assert(kevin_vehicle_choice_get(flags,native)==v);
        encoded=kevin_weapon_choice_set(flags,w,&native);
        assert(native>=0 && native<=10);
        assert(kevin_weapon_choice_get(encoded,native)==w);
        assert(kevin_vehicle_mode(encoded)==kevin_vehicle_mode(flags));
        assert((encoded&~(15u<<24))==(unrelated&~(15u<<24)));
    }
    flags=kevin_vehicle_choice_set(0,5,&native);
    assert(native==0 && kevin_vehicle_mode_mask(kevin_vehicle_mode(flags))==63);
    flags=kevin_vehicle_choice_set(flags,1,&native);
    assert(native==1 && !kevin_vehicle_mode(flags));
    flags=kevin_weapon_choice_set(0,12,&native);
    assert(native==0 && kevin_weapon_mode_mask(kevin_weapon_mode(flags))==(1u<<KEVIN_FLAME));
    assert(kevin_vehicle_map_allowed("bloodgulch"));
    assert(kevin_vehicle_map_allowed("timberland"));
    assert(!kevin_vehicle_map_allowed("chillout"));
    assert(!kevin_vehicle_map_allowed("a10"));
    assert(!kevin_vehicle_map_allowed(NULL));
    /* Shipped Team Ball's reserved filler bits are not custom selections. */
    flags=UINT32_C(0xffffffe3);
    assert(!kevin_vehicle_mode(flags) && !kevin_weapon_mode(flags));
    assert(kevin_vehicle_choice_get(flags,1)==1);
    flags=kevin_vehicle_choice_set(flags,5,&native);
    assert(native==0 && kevin_vehicle_mode(flags)==KEVIN_VEHICLE_ALL);
    assert(!kevin_weapon_mode(flags));
    assert((flags&UINT32_C(0x00ffffff))==UINT32_C(0x00ffffe3));
    return 0;
}
