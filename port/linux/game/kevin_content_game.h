/* Game-ABI-only content integration declarations by kaboggs. */
#ifndef KEVIN_CONTENT_GAME_H
#define KEVIN_CONTENT_GAME_H
#include "cseries/cseries.h"
#include "math/real_math.h"
boolean kevin_content_ground_near(const real_point3d *,boolean,real_point3d *);
long kevin_content_place_team(long,const real_point3d *,short,real);
void kevin_fleet_reset(void);
const char *kevin_content_map_base(void);
void kevin_fleet_update(void);
boolean kevin_gametype_vehicle_allowed(long);
boolean kevin_gametype_weapon_allowed(long);
#endif
