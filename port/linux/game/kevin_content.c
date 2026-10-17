/* Vita content integration additions by kaboggs.
 * Campaign pickup locations adapted from Kevin's New3DS integration.
 * Uses loaded authored tags, original object simulation and checkpoint state.
 */
#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "game/players_runtime.h"
#include "main/main.h"
#include "networking/network_game_globals.h"
#include "interface/ui_widget.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "saved games/game_state.h"
#include "tag_files/tag_groups.h"
#include "kevin_content.h"
#include "kevin_content_game.h"
#include "kevin_gametype_rules.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

struct kevin_pickup { const char *map, *tag; real_point3d point; };
static const struct kevin_pickup pickups[] = {
    {"c20", "weapons\\flamethrower\\flamethrower", {22.54f,-38.67f,-4.45f}},
    {"c40", "weapons\\fuel rod gun\\fuel rod", {333.85f,-656.33f,80.45f}},
    {"d20", "weapons\\flamethrower\\flamethrower", {54.32f,-34.90f,-25.83f}},
    {"d20", "weapons\\fuel rod gun\\fuel rod", {31.14f,-49.65f,-22.86f}}
};
struct kevin_content_state { unsigned long placed; long objects[NUMBEROF(pickups)]; };
static struct kevin_content_state *state;
static volatile int requested, result;

/* kaboggs: kevin_content_initialize - checkpoint-owned placement state. */
void kevin_content_initialize(void)
{
    state = game_state_malloc("Kevin content", NULL, sizeof(*state));
    kevin_content_new_map();
}

/* kaboggs: kevin_content_new_map - do not carry object handles between maps. */
void kevin_content_new_map(void)
{
    unsigned int i;
    if (state) {
        state->placed = 0;
        for (i=0; i<NUMBEROF(pickups); ++i) state->objects[i]=NONE;
    }
    __atomic_store_n(&requested, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&result, 0, __ATOMIC_RELEASE);
    kevin_fleet_reset();
}

/* kaboggs: kevin_content_request - panel never modifies objects on its thread. */
void kevin_content_request(int item)
{
    if (item>=1 && item<=8) __atomic_store_n(&requested, item, __ATOMIC_RELEASE);
}

/* kaboggs: kevin_content_result - 1 success, negative reason, 0 no result yet. */
int kevin_content_result(void) { return __atomic_load_n(&result, __ATOMIC_ACQUIRE); }

/* kaboggs: valid_placement - reject an unconnected/outside object immediately. */
long kevin_content_place_team(long definition, const real_point3d *point, short team, real facing)
{
    struct object_placement_data placement;
    struct object_datum *object;
    struct location location;
    long index;
    scenario_location_from_point(&location, point);
    if (location.cluster_index==NONE) return NONE;
    object_placement_data_new(&placement, definition, NONE);
    placement.position=*point;
    placement.owner_team_index=team;
    placement.forward.i=cosf(facing); placement.forward.j=sinf(facing); placement.forward.k=0.f;
    placement.flags|=FLAG(_new_object_never_automatically_delete_bit);
    index=object_new(&placement);
    if (index==NONE) return NONE;
    object=object_try_and_get(index);
    if (!object || !TEST_FLAG(object->object.flags, _object_connected_to_map_bit) ||
        TEST_FLAG(object->object.flags, _object_outside_of_map_bit) || object->object.location.cluster_index==NONE) {
        if (object) object_delete(index);
        return NONE;
    }
    SET_FLAG(object->object.flags, _object_deleted_when_deactivated_bit, FALSE);
    return index;
}

static long place(long definition, const real_point3d *point)
{
    return kevin_content_place_team(definition,point,NONE,0.f);
}

/* kaboggs: ground_near_player - bounded ground and vehicle clearance search. */
boolean kevin_content_ground_near(const real_point3d *anchor, boolean vehicle, real_point3d *point)
{
    static const signed char offsets[][2]={{0,0},{6,0},{0,6},{-6,0},{0,-6},{8,8},{8,-8},{-8,8},{-8,-8}};
    static const signed char edges[][2]={{2,0},{-2,0},{0,2},{0,-2}};
    unsigned int i, j;
    unsigned long flags=FLAG(_collision_test_front_facing_surfaces_bit)|
        FLAG(_collision_test_back_facing_surfaces_bit)|FLAG(_collision_test_structure_bit);
    for (i=0; i<NUMBEROF(offsets); ++i) {
        real_point3d start=*anchor;
        real_vector3d down={0.f,0.f,-8.f};
        struct collision_result hit;
        struct object_iterator iterator;
        struct object_datum *other;
        boolean clear=TRUE;
        start.x+=offsets[i][0]; start.y+=offsets[i][1]; start.z+=3.f;
        if (!collision_test_vector(flags,&start,&down,NONE,&hit) ||
            hit.type!=_collision_result_structure || hit.plane.n.k<.9f) continue;
        if (vehicle) {
            for (j=0; j<NUMBEROF(edges); ++j) {
                real_point3d edge=hit.point;
                struct collision_result edge_hit;
                edge.x+=edges[j][0]; edge.y+=edges[j][1]; edge.z+=2.f;
                if (!collision_test_vector(flags,&edge,&down,NONE,&edge_hit) ||
                    edge_hit.type!=_collision_result_structure || edge_hit.plane.n.k<.9f ||
                    fabsf(edge_hit.point.z-hit.point.z)>.5f) {clear=FALSE; break;}
            }
            if (!clear) continue;
        }
        *point=hit.point; point->z+=vehicle?.5f:.15f;
        /* A vehicle needs empty space above the pad as well as solid ground. */
        if (vehicle) {
            real_vector3d up={0.f,0.f,3.f};
            struct collision_result ceiling;
            if (collision_test_vector(flags,point,&up,NONE,&ceiling)) continue;
        }
        object_iterator_new(&iterator,_object_mask_vehicle|_object_mask_biped|
            (vehicle?0:_object_mask_weapon),0);
        while ((other=object_iterator_next(&iterator))!=NULL) {
            real dx=other->object.position.x-point->x, dy=other->object.position.y-point->y;
            real dz=other->object.position.z-point->z;
            if (dx*dx+dy*dy<16.f && dz*dz<16.f) {clear=FALSE; break;}
        }
        if (clear) return TRUE;
    }
    return FALSE;
}

/* kaboggs: manual_spawn - keep real authored definitions and their effects. */
static int manual_spawn(int item, const struct object_datum *player)
{
    long definition=NONE;
    real_point3d point;
    boolean vehicle=item>=3;
    switch (item) {
    case 1: definition=tag_loaded('weap',"weapons\\flamethrower\\flamethrower"); break;
    case 2:
        definition=tag_loaded('weap',"weapons\\plasma_cannon\\plasma_cannon");
        if (definition==NONE) definition=tag_loaded('weap',"weapons\\fuel rod gun\\fuel rod");
        break;
    case 3:
        definition=tag_loaded('vehi',"vehicles\\banshee\\banshee_mp");
        if (definition==NONE) definition=tag_loaded('vehi',"vehicles\\banshee\\banshee");
        break;
    case 4: definition=tag_loaded('vehi',"vehicles\\rwarthog\\rwarthog"); break;
    case 5: definition=tag_loaded('vehi',"vehicles\\warthog\\mp_warthog");
        if (definition==NONE) definition=tag_loaded('vehi',"vehicles\\warthog\\warthog"); break;
    case 6: definition=tag_loaded('vehi',"vehicles\\ghost\\ghost_mp");
        if (definition==NONE) definition=tag_loaded('vehi',"vehicles\\ghost\\ghost"); break;
    case 7: definition=tag_loaded('vehi',"vehicles\\scorpion\\scorpion_mp");
        if (definition==NONE) definition=tag_loaded('vehi',"vehicles\\scorpion\\scorpion"); break;
    case 8: definition=tag_loaded('vehi',"vehicles\\c gun turret\\c gun turret_mp");
        if (definition==NONE) definition=tag_loaded('vehi',"vehicles\\c gun turret\\c gun turret"); break;
    }
    if (definition==NONE) return -2;
    if (!kevin_content_ground_near(&player->object.position, vehicle, &point)) return -3;
    return place(definition,&point)==NONE?-4:1;
}

/* kaboggs: kevin_content_update - local tick-only additions, no network mutation. */
void kevin_content_update(void)
{
    const char *base, *option;
    struct player_datum *player;
    struct object_datum *unit;
    long player_index;
    unsigned int i;
    int request;
    boolean enabled;
    if (!state || !game_in_progress() || main_menu_is_active()) return;
    request=__atomic_exchange_n(&requested,0,__ATOMIC_ACQ_REL);
    if (!(game_connection()==_game_connection_local ||
          (game_connection()==_game_connection_network_server && network_game_is_splitscreen_local()))) {
        if (request) __atomic_store_n(&result,-1,__ATOMIC_RELEASE);
        return;
    }
    player_index=local_player_get_player_index(0);
    player=player_index==NONE?NULL:player_try_and_get(player_index);
    unit=player && player->unit_index!=NONE?object_try_and_get(player->unit_index):NULL;
    if (!unit) {
        if (request) __atomic_store_n(&result,-5,__ATOMIC_RELEASE);
        return;
    }
    if (request) {
        int status=manual_spawn(request,unit);
        __atomic_store_n(&result,status,__ATOMIC_RELEASE);
        error(_error_silent,"Kevin content: requested item %d result %d (1 placed, -2 missing tag, -3 no clear ground)",request,status);
    }
    option=getenv("HALO_KEVIN_CAMPAIGN_EXTRAS");
    enabled=option && atoi(option)!=0;
    base=kevin_content_map_base();
    if (!base) return;
    kevin_fleet_update();
    for (i=0; i<NUMBEROF(pickups); ++i) {
        const struct kevin_pickup *pickup=&pickups[i];
        real dx,dy,dz;
        long definition, index;
        if (strcmp(base,pickup->map) || (state->placed & (1UL<<i))) continue;
        if (!enabled) continue;
        dx=unit->object.position.x-pickup->point.x;
        dy=unit->object.position.y-pickup->point.y;
        dz=unit->object.position.z-pickup->point.z;
        if (dx*dx+dy*dy+dz*dz>2500.f) continue;
        definition=tag_loaded('weap',pickup->tag);
        if (definition==NONE) continue;
        index=place(definition,&pickup->point);
        if (index!=NONE) {
            state->objects[i]=index; state->placed|=1UL<<i;
            error(_error_silent,"Kevin campaign pickup: %s %s",base,pickup->tag);
        }
    }
}
