/* Game-ABI-only content integration declarations by kaboggs. */
#ifndef PC_CONTENT_GAME_H
#define PC_CONTENT_GAME_H
#include "cseries/cseries.h"
#include "math/real_math.h"
boolean pc_content_ground_near(const real_point3d *,boolean,real_point3d *);
long pc_content_place_team(long,const real_point3d *,short,real);
void pc_vehicle_fleet_reset(void);
const char *pc_content_map_base(void);
void pc_vehicle_fleet_update(void);
boolean pc_gametype_vehicle_allowed(long);
boolean pc_gametype_weapon_allowed(long);
#endif
