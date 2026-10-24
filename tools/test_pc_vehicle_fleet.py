"""Exercise the real automatic-spawn scheduler with a small fake engine.

No game assets. These tests cover local scope, bounded creation, adopting
native vehicles, missing tags, pickup/death respawns and blocked pads. They
do not establish collision clearance or vehicle physics on a Vita.
"""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

ENGINE = r'''
#ifndef TEST_ENGINE_H
#define TEST_ENGINE_H
#include <stdint.h>
#include <stddef.h>
typedef float real; typedef int boolean; typedef unsigned char byte;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define TICKS_PER_SECOND 30
#define VALID_INDEX(i,n) ((unsigned)(i)<(unsigned)(n))
#define TEST_FLAG(v,b) (((v)>>(b))&1u)
#define TAG_BLOCK_GET_ELEMENT(b,i,t) ((t *)(b)->address+(i))
typedef struct {real x,y,z;} real_point3d;
struct tag_block {long count; void *address;};
struct scenario_object_datum {short unused[4];real_point3d position;struct {real yaw,pitch,roll;} rotation;short pad[2];uint32_t tail;};
struct scenario_netgame_flag {real_point3d position;real facing;short type,team_index;};
struct player_starting_location {real_point3d position;real facing;short team_index;};
struct scenario {struct tag_block netgame_flags,players,vehicles;};
struct object_datum {long definition_index;struct {real_point3d position;unsigned damage_flags;long parent_object_index;} object;};
struct object_iterator {unsigned mask;long index,next;};
struct game_variant {struct {unsigned long flags;} universal_variant;};
enum {_game_connection_local=0,_game_connection_network_server=2,_object_mask_vehicle=1,_object_mask_biped=2,_object_mask_weapon=4,_object_dead_bit=2,_netgame_flag_ctf_flag=0};
boolean game_engine_running(void);int game_connection(void);boolean network_game_is_splitscreen_local(void);
struct game_variant *game_engine_get_variant(void);const char *main_get_map_name(void);long game_time_get(void);
struct scenario *global_scenario_get(void);long tag_loaded(long,const char *);
extern long global_scenario_index; const char *tag_get_name(long);
struct object_datum *object_try_and_get(long);void object_delete(long);
void object_iterator_new(struct object_iterator *,unsigned,unsigned);
struct object_datum *object_iterator_next(struct object_iterator *);
void error(int,const char *,...);
enum {_error_silent=0};
#endif
'''

HARNESS = r'''
#include "test_engine.h"
#include "pc_content_game.h"
#include "pc_gametype_rules.h"
#include <assert.h>
#include <string.h>
static struct game_variant variant;
static struct scenario scenario;
static struct scenario_netgame_flag flags[2];
static struct {struct scenario_object_datum object;byte pad[80];} vehicles[2];
static struct object_datum objects[128];static unsigned kinds[128];
static int alive[128],created,deleted,blocked,ground_calls,connection=2,local=1;
static unsigned missing;static long tick;static const char *map="bloodgulch";
long global_scenario_index=1;
boolean game_engine_running(void){return TRUE;}
int game_connection(void){return connection;}
boolean network_game_is_splitscreen_local(void){return local;}
struct game_variant *game_engine_get_variant(void){return &variant;}
/* Reproduce the Vita: the solo menu still selects a10 during MP matches. */
const char *main_get_map_name(void){return "levels\\a10\\a10";}
const char *tag_get_name(long index){assert(index==1);return map;}
long game_time_get(void){return tick;}
struct scenario *global_scenario_get(void){return &scenario;}
void error(int level,const char *format,...){(void)level;(void)format;}
long tag_loaded(long group,const char *name){
 int type=-1;(void)group;
 if(strstr(name,"rwarthog"))type=3;
 else if(strstr(name,"banshee"))type=4;
 else if(strstr(name,"scorpion"))type=2;
 else if(strstr(name,"ghost"))type=1;
 else if(strstr(name,"warthog"))type=0;
 else if(strstr(name,"c gun turret"))type=5;
 else if(strstr(name,"flamethrower"))type=6;
 else if(strstr(name,"plasma_cannon")||strstr(name,"fuel rod"))type=7;
 return type<0 || (missing&(1u<<type))?NONE:100+type;
}
struct object_datum *object_try_and_get(long index){return index>=0 && index<128 && alive[index]?&objects[index]:NULL;}
void object_delete(long index){assert(object_try_and_get(index));alive[index]=0;++deleted;}
void object_iterator_new(struct object_iterator *i,unsigned mask,unsigned ignored){(void)ignored;i->mask=mask;i->next=0;}
struct object_datum *object_iterator_next(struct object_iterator *i){
 while(i->next<128){long n=i->next++;if(alive[n]&&(kinds[n]&i->mask)){i->index=n;return &objects[n];}}
 return NULL;
}
boolean pc_content_ground_near(const real_point3d *anchor,boolean vehicle,real_point3d *out){
 (void)vehicle;++ground_calls;*out=*anchor;return !blocked;
}
long pc_content_place_team(long definition,const real_point3d *point,short side,real facing){
 int i;(void)side;(void)facing;
 for(i=0;i<128;++i)if(!alive[i]){
  memset(&objects[i],0,sizeof(objects[i]));objects[i].definition_index=definition;
  objects[i].object.position=*point;objects[i].object.parent_object_index=NONE;
  kinds[i]=definition<106?_object_mask_vehicle:_object_mask_weapon;alive[i]=1;++created;return i;
 }
 assert(0);return NONE;
}
static void reset(unsigned mode){
 unsigned side;memset(alive,0,sizeof(alive));memset(&scenario,0,sizeof(scenario));
 memset(flags,0,sizeof(flags));memset(vehicles,0,sizeof(vehicles));
 assert(sizeof(vehicles[0])==0x78);
 for(side=0;side<2;++side){flags[side].position.x=side?50.f:-50.f;flags[side].team_index=(short)side;
  vehicles[side].object.position=flags[side].position;vehicles[side].object.position.y=10.f;}
 scenario.netgame_flags.count=2;scenario.netgame_flags.address=flags;
 scenario.vehicles.count=2;scenario.vehicles.address=vehicles;
 variant.universal_variant.flags=mode;tick=0;created=deleted=blocked=ground_calls=0;
 connection=2;local=1;missing=0;global_scenario_index=1;map="bloodgulch";pc_vehicle_fleet_reset();
}
static void advance(int seconds){long end=tick+seconds*TICKS_PER_SECOND;
 for(;tick<end;++tick){int before=created;pc_vehicle_fleet_update();assert(created-before<=1);}}
int main(void){
 real_point3d point;
 reset(1u<<24);
 point=flags[0].position;pc_content_place_team(100,&point,0,0);
 point=flags[1].position;pc_content_place_team(100,&point,1,0);
 advance(15);assert(created==12);advance(120);assert(created==12);
 assert(pc_gametype_vehicle_allowed(104));assert(!pc_gametype_vehicle_allowed(999));
 reset(2u<<24);advance(10);assert(created==2);
 objects[0].object.damage_flags=1u<<_object_dead_bit;
 advance(20);assert(created==2 && deleted==0);advance(25);assert(created==3 && deleted==1);
 reset(2u<<24);map="levels\\test\\bloodgulch\\bloodgulch";advance(10);
 assert(created==2 && pc_gametype_vehicle_allowed(104));
 reset(1u<<24);map="levels\\test\\deathisland\\deathisland";advance(15);
 assert(created==12 && pc_gametype_vehicle_allowed(103));
 reset(1u<<24);global_scenario_index=NONE;advance(10);assert(created==0);
 reset(1u<<24);missing=1u<<PC_ROCKET_HOG;advance(30);assert(created==10);
 reset(1u<<24);blocked=1;advance(20);assert(created==0 && ground_calls<=40);
 reset(1u<<24);local=0;advance(20);assert(created==0 && !pc_gametype_vehicle_allowed(104));
 reset(1u<<24);connection=3;advance(20);assert(created==0);
 reset(1u<<24);map="chillout";advance(20);assert(created==0);
 reset(1u<<26);map="chillout";advance(10);assert(created==4 && pc_gametype_weapon_allowed(106));
 objects[0].object.parent_object_index=99;
 advance(20);assert(created==4);advance(25);assert(created==5);
 reset(0);advance(30);assert(created==0);
 return 0;
}
'''


def test_real_fleet_scheduler(tmp_path):
    compiler = shutil.which('cc') or shutil.which('gcc')
    assert compiler, 'A C compiler is required for the scheduler test'
    (tmp_path / 'test_engine.h').write_text(ENGINE)
    headers = ['cseries/cseries.h','math/real_math.h','cseries/errors.h','game/game.h',
               'game/game_engine.h','main/main.h','networking/network_game_globals.h',
               'objects/objects.h','scenario/scenario.h','scenario/scenario_definitions.h',
               'tag_files/tag_groups.h','tag_files/tag_files.h']
    for name in headers:
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "test_engine.h"\n')
    harness = tmp_path / 'fleet_test.c'
    harness.write_text(HARNESS)
    output = tmp_path / 'fleet_test.exe'
    subprocess.run([compiler, '-std=c99','-O1','-Wall','-Wextra','-Werror','-Wno-multichar',
                    '-I'+str(tmp_path),'-I'+str(ROOT/'port/linux/game'),
                    str(ROOT/'port/linux/game/pc_vehicle_fleet.c'),str(harness),'-lm','-o',str(output)],check=True)
    subprocess.run([str(output)],check=True,timeout=10)
