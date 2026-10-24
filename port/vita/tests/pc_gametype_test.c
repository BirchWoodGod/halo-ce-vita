/* Saved gametype compatibility and selection checks by kaboggs. */
#include "../../linux/game/pc_gametype_rules.h"
#include <assert.h>
int main(void)
{
    int v,w,native;uint32_t flags,encoded;
    const uint32_t unrelated=0xA0123456u;
    for (v=0;v<8;++v) for (w=0;w<14;++w) {
        flags=pc_vehicle_choice_set(unrelated,v,&native);
        assert(native>=0 && native<=4);
        assert(pc_vehicle_choice_get(flags,native)==v);
        encoded=pc_weapon_choice_set(flags,w,&native);
        assert(native>=0 && native<=10);
        assert(pc_weapon_choice_get(encoded,native)==w);
        assert(pc_vehicle_mode(encoded)==pc_vehicle_mode(flags));
        assert((encoded&~(15u<<24))==(unrelated&~(15u<<24)));
    }
    flags=pc_vehicle_choice_set(0,5,&native);
    assert(native==0 && pc_vehicle_mode_mask(pc_vehicle_mode(flags))==63);
    flags=pc_vehicle_choice_set(flags,1,&native);
    assert(native==1 && !pc_vehicle_mode(flags));
    flags=pc_weapon_choice_set(0,12,&native);
    assert(native==0 && pc_weapon_mode_mask(pc_weapon_mode(flags))==(1u<<PC_FLAME));
    assert(pc_vehicle_map_allowed("bloodgulch"));
    assert(pc_vehicle_map_allowed("timberland"));
    assert(!pc_vehicle_map_allowed("chillout"));
    assert(!pc_vehicle_map_allowed("a10"));
    assert(!pc_vehicle_map_allowed(NULL));
    /* Shipped Team Ball's reserved filler bits are not custom selections. */
    flags=UINT32_C(0xffffffe3);
    assert(!pc_vehicle_mode(flags) && !pc_weapon_mode(flags));
    assert(pc_vehicle_choice_get(flags,1)==1);
    flags=pc_vehicle_choice_set(flags,5,&native);
    assert(native==0 && pc_vehicle_mode(flags)==PC_VEHICLE_ALL);
    assert(!pc_weapon_mode(flags));
    assert((flags&UINT32_C(0x00ffffff))==UINT32_C(0x00ffffe3));
    return 0;
}
