/* Automatic local gametype vehicle and PC pickup spawns by kaboggs.
 * Uses each team's authored vehicle/CTF pads, loaded real tags and bounded
 * ground/clearance checks. No fabricated assets or changes to network peers.
 * Multiplayer state is deliberately outside campaign checkpoint allocation. */
#include "kevin_content_game.h"
#include "kevin_gametype_rules.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "main/main.h"
#include "networking/network_game_globals.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include <string.h>
#include <math.h>

struct fleet_pad { real_point3d point; real facing; boolean valid; };
struct fleet_slot { long object, due; boolean owned; };
static struct {
    struct fleet_pad bases[2], pads[2];
    struct fleet_slot slots[2][KEVIN_ITEM_TYPES];
    long last_tick;
    unsigned cursor, announced;
    boolean anchored;
} fleet;

static const char *const definitions[KEVIN_ITEM_TYPES]={
    "vehicles\\warthog\\mp_warthog", "vehicles\\ghost\\ghost_mp",
    "vehicles\\scorpion\\scorpion_mp", "vehicles\\rwarthog\\rwarthog",
    "vehicles\\banshee\\banshee_mp", "vehicles\\c gun turret\\c gun turret_mp",
    "weapons\\flamethrower\\flamethrower",
    "weapons\\plasma_cannon\\plasma_cannon"
};

/* kaboggs: shared by the remapper and spawner; match loaded definitions. */
static long definition_get(unsigned type)
{
    long group=type<KEVIN_VEHICLE_TYPES?'vehi':'weap';
    long result=tag_loaded(group,definitions[type]);
    if (result==NONE && type==KEVIN_HOG) result=tag_loaded(group,"vehicles\\warthog\\warthog");
    if (result==NONE && type==KEVIN_GHOST) result=tag_loaded(group,"vehicles\\ghost\\ghost");
    if (result==NONE && type==KEVIN_TANK) result=tag_loaded(group,"vehicles\\scorpion\\scorpion");
    if (result==NONE && type==KEVIN_BANSHEE) result=tag_loaded(group,"vehicles\\banshee\\banshee");
    if (result==NONE && type==KEVIN_TURRET) result=tag_loaded(group,"vehicles\\c gun turret\\c gun turret");
    if (result==NONE && type==KEVIN_FUEL) result=tag_loaded(group,"weapons\\fuel rod gun\\fuel rod");
    return result;
}

static boolean local_mode(void)
{
    return game_engine_running() && (game_connection()==_game_connection_local ||
        (game_connection()==_game_connection_network_server && network_game_is_splitscreen_local()));
}

/* The main menu's map name is the last SOLO selection (often a10), even
 * during multiplayer. Use the scenario that was actually loaded instead. */
const char *kevin_content_map_base(void)
{
    const char *map=global_scenario_index==NONE?NULL:tag_get_name(global_scenario_index);
    const char *base=map?strrchr(map,'\\'):NULL;
    return base?base+1:map;
}

boolean kevin_gametype_vehicle_allowed(long definition)
{
    unsigned type,mask;
    if (!local_mode() || !kevin_vehicle_map_allowed(kevin_content_map_base())) return FALSE;
    mask=kevin_vehicle_mode_mask(kevin_vehicle_mode(game_engine_get_variant()->universal_variant.flags));
    for (type=0;type<KEVIN_VEHICLE_TYPES;++type)
        if ((mask&(1u<<type)) && definition==definition_get(type)) return TRUE;
    return FALSE;
}

boolean kevin_gametype_weapon_allowed(long definition)
{
    unsigned type,mask;
    if (!local_mode()) return FALSE;
    mask=kevin_weapon_mode_mask(kevin_weapon_mode(game_engine_get_variant()->universal_variant.flags));
    for (type=KEVIN_FLAME;type<KEVIN_ITEM_TYPES;++type)
        if ((mask&(1u<<type)) && definition==definition_get(type)) return TRUE;
    return FALSE;
}

void kevin_fleet_reset(void)
{
    unsigned side,type;
    memset(&fleet,0,sizeof(fleet));fleet.last_tick=-1;
    for (side=0;side<2;++side) for (type=0;type<KEVIN_ITEM_TYPES;++type)
        fleet.slots[side][type].object=NONE;
}

static real distance_squared(const real_point3d *a,const real_point3d *b)
{
    real x=a->x-b->x,y=a->y-b->y,z=a->z-b->z;
    return x*x+y*y+z*z;
}

/* kaboggs: CTF bases are stable in Slayer too. Vehicle pads are selected
 * from the scenario's real placements, nearest each base, never player view. */
static void anchors_find(void)
{
    struct scenario *scenario=global_scenario_get();
    long i;unsigned side;
    fleet.anchored=TRUE;
    for (i=0;i<scenario->netgame_flags.count;++i) {
        const struct scenario_netgame_flag *flag=TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags,i,struct scenario_netgame_flag);
        if (flag->type==_netgame_flag_ctf_flag && VALID_INDEX(flag->team_index,2)) {
            struct fleet_pad *pad=&fleet.bases[flag->team_index];
            pad->point=flag->position;pad->facing=flag->facing;pad->valid=TRUE;
        }
    }
    for (i=0;i<scenario->players.count;++i) {
        const struct player_starting_location *start=TAG_BLOCK_GET_ELEMENT(&scenario->players,i,struct player_starting_location);
        if (VALID_INDEX(start->team_index,2) && !fleet.bases[start->team_index].valid) {
            struct fleet_pad *pad=&fleet.bases[start->team_index];
            pad->point=start->position;pad->facing=start->facing;pad->valid=TRUE;
        }
    }
    for (side=0;side<2;++side) {
        real best=-1.f;
        if (!fleet.bases[side].valid) continue;
        for (i=0;i<scenario->vehicles.count;++i) {
            const struct scenario_object_datum *placement=(const struct scenario_object_datum *)
                ((const byte *)scenario->vehicles.address+i*0x78);
            real distance=distance_squared(&placement->position,&fleet.bases[side].point);
            unsigned other=1-side;
            if (fleet.bases[other].valid && distance>
                distance_squared(&placement->position,&fleet.bases[other].point)) continue;
            if (best<0.f || distance<best) {
                fleet.pads[side].point=placement->position;
                fleet.pads[side].facing=placement->rotation.yaw;
                fleet.pads[side].valid=TRUE;best=distance;
            }
        }
        error(_error_silent,"Kevin gametype: %s side %u base %d vehicle pad %d at %.2f %.2f %.2f",
            kevin_content_map_base(),side,fleet.bases[side].valid,fleet.pads[side].valid,
            fleet.pads[side].point.x,fleet.pads[side].point.y,fleet.pads[side].point.z);
    }
}

/* kaboggs: adopt a native vehicle near the base rather than duplicating
 * it. Once tracked, a live vehicle stays tracked wherever somebody drives it. */
static long existing_vehicle(long definition,unsigned side)
{
    struct object_iterator iterator;struct object_datum *object;
    long best=NONE;real distance_best=-1.f;
    object_iterator_new(&iterator,_object_mask_vehicle,0);
    while ((object=object_iterator_next(&iterator))!=NULL) {
        real distance;unsigned s,t;boolean used=FALSE;
        if (object->definition_index!=definition || TEST_FLAG(object->object.damage_flags,_object_dead_bit)) continue;
        for (s=0;s<2;++s) for (t=0;t<KEVIN_VEHICLE_TYPES;++t)
            if (fleet.slots[s][t].object==iterator.index) used=TRUE;
        if (used) continue;
        distance=distance_squared(&object->object.position,&fleet.bases[side].point);
        if (fleet.bases[1-side].valid && distance>
            distance_squared(&object->object.position,&fleet.bases[1-side].point)) continue;
        if (distance_best<0.f || distance<distance_best) {best=iterator.index;distance_best=distance;}
    }
    return best;
}

static boolean occupied(long vehicle)
{
    struct object_iterator iterator;struct object_datum *object;
    object_iterator_new(&iterator,_object_mask_biped,0);
    while ((object=object_iterator_next(&iterator))!=NULL)
        if (object->object.parent_object_index==vehicle) return TRUE;
    return FALSE;
}

/* kaboggs: at most one slot every half second; twelve vehicles maximum
 * including adopted natives, plus four added pickups. Respawn after 30 s
 * minimum, and postpone occupied/blocked locations rather than stacking. */
void kevin_fleet_update(void)
{
    static const signed char offsets[KEVIN_ITEM_TYPES][2]={{0,0},{7,0},{-7,0},{0,7},{0,-7},{7,7},{5,0},{-5,0}};
    struct fleet_slot *slot;const struct fleet_pad *pad;struct object_datum *object;
    real_point3d anchor,point;long tick,definition,index;unsigned mask,side,type;
    boolean vehicle;
    const char *map=kevin_content_map_base();
    if (!local_mode()) return;
    if (!map) return;
    mask=kevin_weapon_mode_mask(kevin_weapon_mode(game_engine_get_variant()->universal_variant.flags));
    if (kevin_vehicle_map_allowed(map))
        mask|=kevin_vehicle_mode_mask(kevin_vehicle_mode(game_engine_get_variant()->universal_variant.flags));
    if (!mask) return;
    tick=game_time_get();
    if (tick==fleet.last_tick || tick%(TICKS_PER_SECOND/2)) return;
    fleet.last_tick=tick;
    if (!fleet.anchored) {
        error(_error_silent,"Kevin gametype: %s flags 0x%08lx enabled mask 0x%x",
            map,(unsigned long)game_engine_get_variant()->universal_variant.flags,mask);
        anchors_find();
    }
    for (index=0;index<2*KEVIN_ITEM_TYPES;++index) {
        unsigned next=fleet.cursor++%(2*KEVIN_ITEM_TYPES);
        side=next/KEVIN_ITEM_TYPES;type=next%KEVIN_ITEM_TYPES;
        if ((mask&(1u<<type)) && fleet.bases[side].valid) break;
    }
    if (index==2*KEVIN_ITEM_TYPES) return;
    slot=&fleet.slots[side][type];vehicle=type<KEVIN_VEHICLE_TYPES;
    object=slot->object==NONE?NULL:object_try_and_get(slot->object);
    if (object && !TEST_FLAG(object->object.damage_flags,_object_dead_bit) &&
        (vehicle || object->object.parent_object_index==NONE)) return;
    if (slot->object!=NONE && !slot->due) slot->due=tick+30*TICKS_PER_SECOND;
    if (slot->due>tick) return;
    if (object && vehicle && slot->owned) {
        if (occupied(slot->object)) return;
        object_delete(slot->object);
    }
    slot->object=NONE;slot->owned=FALSE;
    definition=definition_get(type);
    if (definition==NONE) {
        if (!(fleet.announced&(1u<<type))) {
            error(_error_silent,"Kevin gametype: %s missing asset %s",map,definitions[type]);
            fleet.announced|=1u<<type;
        }
        slot->due=tick+10*TICKS_PER_SECOND;return;
    }
    if (vehicle && (slot->object=existing_vehicle(definition,side))!=NONE) {slot->due=0;return;}
    pad=vehicle?&fleet.pads[side]:&fleet.bases[side];
    if (!pad->valid) {slot->due=tick+10*TICKS_PER_SECOND;return;}
    anchor=pad->point;
    anchor.x+=offsets[type][0]*cosf(pad->facing)-offsets[type][1]*sinf(pad->facing);
    anchor.y+=offsets[type][0]*sinf(pad->facing)+offsets[type][1]*cosf(pad->facing);
    if (!kevin_content_ground_near(&anchor,vehicle,&point)) {
        unsigned bit=1u<<(side*KEVIN_ITEM_TYPES+type+KEVIN_ITEM_TYPES);
        if (!(fleet.announced&bit)) {
            error(_error_silent,"Kevin gametype: %s side %u no clear ground for %s near %.2f %.2f %.2f",
                map,side,definitions[type],anchor.x,anchor.y,anchor.z);
            fleet.announced|=bit;
        }
        slot->due=tick+10*TICKS_PER_SECOND;return;
    }
    index=kevin_content_place_team(definition,&point,(short)side,pad->facing);
    if (index==NONE) {slot->due=tick+10*TICKS_PER_SECOND;return;}
    slot->object=index;slot->owned=TRUE;slot->due=0;
    error(_error_silent,"Kevin gametype: %s side %u %s at %.2f %.2f %.2f",map,side,definitions[type],point.x,point.y,point.z);
}
