/*
BOTS.C

Computer players (bots) for offline multiplayer: a local (split screen)
game's lobby takes as many as the settings ask for (bots.count, the Vita's
settings panel: Multiplayer, Bots; network_server_manager.c adds them), and
each plays as a player does - spawns, respawns, scores, is in the scores and
the game's rules (Slayer, CTF, King of the Hill, Oddball, Race) - with its
controls from here instead of a controller: the game tick takes every bot's
action from bots_update_actions (players_update_before_game), where a
player's comes from its pad. A bot has no view: nothing is drawn for it.

What a bot does each tick, in a player's terms:

- It looks for enemies (every few ticks, bots in turn): the nearest player
  of the other team (anyone in a game without teams) within its sight's
  reach and field of view whom a line of sight reaches (the AI's own sight
  test: the level, vehicles, scenery and machines block it), nearer for one
  in active camouflage; and it notices who shot it.
- It reacts after a moment (its skill's reaction time), then turns towards
  the target at a limited speed, with an aim error that settles while it
  keeps the target in sight (and leads a moving target by its weapon's
  projectile speed), and fires in bursts while its aim is on the target
  (released between bursts, so semi-automatic weapons fire too); it throws
  a grenade now and then at middle range and strikes with its weapon close
  up. It strafes while fighting, jumps now and then, and keeps its distance.
- Otherwise it goes where the game's nav points show its team (and only
  that player): the flag to take or the base to bring it to, the hill, the
  ball or its carrier, the race's next point (game_engine_port_player_goals);
  in Slayer, where an enemy was last seen, else about the map (the player
  starting locations, the weapon and item spawns), now and then picking up
  a weapon it walks onto (the action button, as a player holds X).
- Powerups (port): hurt - its body below its skill's mark, lower in a fight
  (where an easy bot never leaves it) - it goes for a health pack lying on
  the map (45 units at most, 15 in a fight, backing to it as it fights),
  before the game's goals but for the flag or the ball it carries; an
  overshield or camouflage near (12 units in a game with an objective) the
  same. A pack is an object only while it has spawned, so a bot never goes
  for one the game has yet to bring back; one another bot nearer goes for
  is that bot's; one it finds no way to, or stood at and did not get, it
  leaves be a while (a few remembered at once). The map's powerups are
  found once a second for every bot.
- It walks the paths the campaign's AI finds: the engine's own pathfinding
  over the level's walkable surfaces (source/ai/path.c), which every stock
  multiplayer map carries (tool builds it with the structure BSP: Blood
  Gulch has 2807 walkable surfaces of 4916; only the AI's placements,
  encounters and firing positions, are missing). A bot that stops moving
  jumps and sidesteps, then finds another path. The multiplayer maps were
  never made for the AI, and the bots' searches read their own copy of the
  walkable surfaces: those with no room above them (a floor going on under
  a wall: Longest's bases) closed a few a tick from the game's start, and
  those under scenery or a machine the path's obstacle avoidance once found
  no way past (Sidewinder's blast door between the bases) closed from then
  on. A goal on ground the walkable surfaces do not reach is gone to
  through a teleporter (Chiron TL34's rooms), else as near as they go and
  on with jumps; an item on a ledge is left be. Bots in each other's way
  give way by their order.
- Vehicles (port: the bots' own; bots.vehicles 0, the settings panel's Bot
  vehicles Off, HALO_BOT_VEHICLES=0, leaves them on foot):
  a bot whose goal is a long way off (35 units: Blood Gulch, Sidewinder)
  goes for an empty Warthog, Ghost or Scorpion on its way there, as driver,
  or for a Warthog's gunner's (else passenger's) seat when a teammate, bot
  or player, drives it (the seats as the campaign's AI weighs them,
  action_vehicle.c); a bot driver waits while a teammate it saw coming gets
  in. Port: a vehicle is worth what its kind is, as players weigh them - a
  Scorpion a walk of 50 units, back to it too when it is near (Blood Gulch's
  stand behind the bases, which bots leaving for the other flag never went
  back for), a Ghost 40, a Warthog 30 and only on the way - and a bot fresh
  from its base goes back for its base's vehicles; a bot much nearer to a
  seat a teammate bot set off for takes it over.
  It gets in where and when a player could (still at the seat's
  entrance: players.c's player_examine_nearby_vehicle), into the seat it
  chose, as the campaign's AI does (unit_enter_seat); a vehicle on its side
  it rights first, with the action button. A driver drives the same path
  searches over the walkable surfaces, as wide as the vehicle's own
  pathfinding radius (actor_moving.c's), steering as a player does, by
  where it looks: on to the game's objective while it fights, a driver that
  is also the gunner (Ghost, Scorpion) fighting from it otherwise, a
  Warthog's driver going round the enemy for its gunner (or at a lone enemy,
  to run it down); gunners and passengers shoot. One bot to a seat (a seat a
  bot goes for is its own), so no two go for the same one. Stuck, a driver
  backs off and turns, then goes on; scenery or a machine it drives into is
  learnt to be in the way (Sidewinder's blast door), and the level it drives
  into closed to the vehicles' searches alone (the banks a biped walks up;
  port: what is before it looked for past its front and low at its treads,
  and closed as far about as it is wide: a Scorpion's 2.2 units).
  A bot gets out when its vehicle lies on its side, sticks fast, has no
  driver, or near its goal when that wants it on foot (a flag, a hill, the
  ball), and leaves a vehicle that failed it alone a while. halo.log has
  the game's vehicles (port), each entry and exit, and every ten seconds'
  census of them (bots riding, driving, gunning, the distance driven,
  getting in and out and why, kills riding; port: the entries by kind, the
  first's tick, the seconds riding, the seats taken over and the vehicles
  given up on the way).
  Turrets (port): a vehicle that does not move - the vehicle type turret,
  or one with a gunner's seat and no driver's (Custom Edition maps' gun
  towers and Shades; no stock map has one) - is manned by a bot on foot
  within 12 units of it with an enemy seen in the last five seconds no
  further than 60 from it, the turret's seat gone for as any other; it
  fights from it (a gun whose trigger charges held until charged) and gets
  off when no enemy has been before it six seconds, when the gun does not
  turn to the one it fights (out of its arc) for two, or when it is being
  killed on it, and leaves that turret be a while.
- Ladders (port): Halo has climbable surfaces (collision_bsp's flag; a
  biped that jumps onto one it faces goes up it: bipeds.c) - Boarding
  Action has sixteen ladders, Wizard four, Longest two - and the walkable
  surfaces never
  go up one, so a ladder (its foot and the landing at its top found once a
  map) is to the searches as a teleporter: gone to when the goal is beyond
  the ground the search walks, climbed facing it with a jump now and then,
  then on to the landing. One no bot gets up after three tries (Damnation's
  climbable pipes) is left be. There is no other climbing: a stuck bot
  jumps and sidesteps as before (halo.log's moves line counted what stood
  before stuck bots: low walls a jump clears were a few in a hundred).
- Teleporters as shortcuts (port; bots_shortcut_weigh): a bot on foot whose
  goal the walkable surfaces do reach walked there before, through no
  teleporter however much shorter (Blood Gulch's, from beside each base's
  flag out to the middle of the canyon; Battle Creek's between the bases).
  Now, as a search to its goal answers, it weighs the map's teleporters
  (each source flag with the first target of its channel, the one the game
  sends a player to): the walk to an entrance and on from its exit against
  the walk the search found; the best, 80% of the walk at most and 15 units
  shorter, is weighed again with the walk from its exit measured by a search
  of its own, and taken: the bot goes onto the entrance and on from where
  it comes out. Never driving, never a defender, nor a bot on a side lane;
  to the objective, a roaming place or an enemy last seen alone (items,
  powerups and vehicles come first - a bot fresh from its base with a
  vehicle near weighs none for its first ten seconds - a fight stops its
  clock); two bots to a teleporter at most at once. One that fails a bot (not sent from its
  entrance, or not come to in time) is left be thirty seconds, for the map
  after three failures and none sent.
- Regions, jumps and getting unstuck (port: bots_nav.c). The searches walk
  the AI's walkable surfaces alone; the bots know the level's regions (those
  surfaces joined across their edges, with the ramps and sills a biped walks
  on that the AI's leave out - opened to the bots' searches too - and none
  with no room above), and the hops between them: the teleporters, the
  ladders, and the jumps and drops a player takes (onto a crate or a ledge,
  over a railing, off a balcony), found from the regions' open edges and
  tested with rays, as high as the biped jumps crouched and as low as it
  falls unharmed. A goal in another region is gone to by the hops (a search
  over them), each taken at its entrance (a jump pressed at the edge, the
  feet tucked; a drop walked off); one no hop reaches is given up a while
  (an item, the game's goals, an enemy's place, a roaming place - none
  chosen again while it has no way), where a bot once walked as near as its
  search came and on into the wall between. Within a region a jump or a
  drop is taken as a shortcut when it is much shorter than the walk. A bot
  barely moving at a low wall jumps it at once; stuck a second, it jumps a
  low wall, walks round a tall one to the open side, then backs off aslant
  the other way, and gives its goal up a while the third time. In a fight
  its strafe blocked turns the other way (it was stuck before, its path and
  goal dropped). halo.log's play line has the goals with no way, the
  strafes blocked, the walls jumped and the shortcuts; the nav line the
  routes asked and the hops taken and failed.
- Roles, routes and vantage points (port: bots_roles.c, bots_routes.c and
  bots_vantage.c, steering the bot through bots_tactics.h's hooks here: its
  goal, its searches' goal, its look and its fight). Each team's bots have
  roles by gametype and team size: in CTF with four or more every third a
  defender, about its own flag stand (its posts in turn: the stand, a spot
  near it that sees it, the way out), and a sniper; in King of the Hill
  likewise a defender over the hill from a vantage point; a sniper in
  Oddball and Slayer (roamers there); the rest attackers. Bot routes (bots.routes,
  the settings panel's Bot routes, HALO_BOT_ROUTES): Direct, the default, is
  the bots as before - everyone the shortest way, down the middle; Lanes
  spreads each team's attackers over the direct route and the map's two side
  lanes in turn, Mixed every third of them on a side lane: such a bot on foot
  to a goal a long way off searches its way to its lane's next waypoint
  first, and gives the lane up when it finds no way there. Vantage points: a
  sniper, a bot holding a precision weapon (bots_weapon_precision, from the
  weapon's tag: a sniper rifle; a pistol, a lighter liking, one near; a
  rocket launcher), and King's defenders hold one a while (a third of a team
  at once, half with sniper rifles), looking from one place it sees to
  another, and leave it when an enemy comes near, when out of ammunition,
  hurt, when the weapon is dropped or swapped, when the flag or the ball
  comes first, or after a while. The lanes and the vantage points are
  found as each game's bots first tick, a bounded step a tick (the map's
  walkable surfaces as a graph, the AI's sight test), and logged.

Skill (bots.skill): easy, normal, heroic, legendary - how soon it reacts,
how far and how wide it sees, how large its aim error is and how fast that
settles, how fast it turns, how tight its shots must be before it fires.

Squads (port, team games: bots_squads.c): two attackers or roamers to a
squad, on one lane; in CTF an attacker ahead of its partner holds for it
before the enemy base, so the two go in together; in Slayer the second
follows its leader; the enemies a bot sees are called out to its team,
drawing their fire (and Slayer's roamers near); and only the flag carrier's
partner and the two bots nearest it escort it.

The simulation: a bot decides from the game state at the tick and its own
random numbers (seeded per bot and game), so the bots do the same in two
runs of the same input (HALO_TICK_HASH's fixed ticks). Nothing of a bot is in
the game state: multiplayer games are never saved. Bots are in local games,
which no other machine sees, and (port) in the dedicated server's games
(sv_bots_fill: network_server_manager.c), where the host alone runs them
and its clients see players of machines of their own, driven by the input
the host relays (update_server_set_ticked_action: what each bot played
that tick) and the units it sends, as any remote player: no message of the
network protocol changes, and every build, 1.1.0's too, plays with them.

The path searches - the part a bot's tick spends most of its time in - run
on a helper thread of their own where there is one to spare (the Vita's
fourth core, Fourth core helpers at All async; elsewhere the thread runs on
the cores the system gives it), on the level's static data only: a search
asked at one tick is taken at a later tick, and the tick never waits for
one (with HALO_FIXED_TICK, for a hash run, they run in the tick instead, so
the bots stay the same from run to run). halo.log has each minute of play's
bot line: their ticks' time (us/tick), the searches and how many failed;
port: and its moves line: the times a bot stood stuck and what was before
it, the powerups gone for and taken, the bodies restored, the ladders and
their climbs, the teleporters' shortcuts weighed, taken, gone through, failed
and dropped (and the bots sent as the last way or by chance), with its
teleporters line (each teleporter's bots sent and failures); and its tactics
line (bots_roles.c): the lanes taken and given up, the vantage points held
and left, the defenders' posts. The map's teleporters are logged once.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "units/vehicles.h"
#include "units/vehicle_definitions.h"
#include "items/items.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "items/projectile_definitions.h"
#include "items/equipment.h"
#include "items/equipment_definitions.h"
#include "physics/collisions.h"
#include "physics/breakable_surfaces.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "physics/collision_bsp_definitions.h"
#include "networking/network_game_globals.h"
#include "game/player_queues_new.h"
#include "ai/path.h"
#include "devices/devices.h"
#include "devices/device_definitions.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "cache/cache_files.h"

#include "bots.h"
#include "bots_tactics.h"
#include "bots_nav.h"
#include "infection.h"
#include "network_connection.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>

/* the platform layer's */
const char *config_string(char const *name);
long config_integer(char const *name);
void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);
#ifdef HALO_VITA
int vita_host_fourth_core_join(const char *role, int level);
void vita_host_thread_watch(const char *role);
#endif
/* render_epoch.c's: bumped as each map (and game state) is loaded */
extern unsigned long halo_map_generation;
/* point_leaf_cache.c's: bumped as the structure's bsp changes */
extern unsigned long halo_structure_bsp_generation;
/* game_engine.c's (port): the nav points a player is shown */
long game_engine_port_player_goals(long player_index, real_point3d *positions, long maximum);
/* units.c's: where a unit stands to get into a seat (the seat's entrance),
the seat, and the entrance's hint */
boolean unit_get_seat_entrance_point(long unit_index, long target_unit_index, short seat_index,
	real_point3d *entry_position, real_point3d *exit_position, real_point3d *seat_transform);

/* ---------- constants */

#define BOTS_PI 3.14159265f
#define BOTS_DEGREES (BOTS_PI / 180.0f)
/* the most places a bot roams to (player starting locations, item spawns) */
#define BOTS_MAXIMUM_ROAM_POINTS 96
/* the path searches asked for at once, whose answers wait for their bots
(a search's state is 80 KB: the engine's 1024 nodes, heap and table) */
#define BOTS_SEARCH_SLOTS 3
/* the bots' size as the path searches know it (a biped's radius) */
#define BOTS_PATH_RADIUS 0.25f
/* the most teleporters a map's bots use (Chiron TL34 has 30), and ladders
(port: Boarding Action has 12) */
#define BOTS_MAXIMUM_TELEPORTERS 64
/* how near a path's step a bot is when it has reached it (across) */
#define BOTS_STEP_REACHED 0.35f
/* a pathfinding surface's flag: walkable (path.c's) */
#define BOTS_PATHFINDING_WALKABLE_BIT 6
/* a collision surface's flag: climbable (collision_bsp.c's) */
#define BOTS_COLLISION_SURFACE_CLIMBABLE_BIT 2
/* vehicles: a bot goes for one when its goal is further than this, the
vehicle no further than its type's reach (bots_vehicle_worths: a Warthog's
30, a Ghost's 40, a Scorpion's 50) */
#define BOTS_VEHICLE_GOAL_DISTANCE 35.0f
/* port: the seconds after it spawns that a bot is fresh from its base: a
vehicle there is gone for further out of its way (bots_vehicle_consider) */
#define BOTS_VEHICLE_FRESH_SECONDS 10
/* port: how much nearer a bot is to a seat another bot goes for before it
takes the seat over (the other let go of it: bots_vehicle_consider) */
#define BOTS_VEHICLE_TAKE_OVER 15.0f
/* how near a driven path's step a vehicle is when it has reached it */
#define BOTS_DRIVE_STEP_REACHED 2.5f
/* how near its goal a bot riding gets out, when the goal wants it on foot */
#define BOTS_VEHICLE_GOAL_EXIT 10.0f
/* port: a biped's knees and chest above its feet (a unit is three metres:
the Master Chief stands 0.7), the heights a stuck bot looks for a wall at;
a wall below the chest is one a jump clears */
#define BOTS_KNEE_HEIGHT 0.15f
#define BOTS_CHEST_HEIGHT 0.4f
/* port: the items a bot remembers it found no way to, at once (one before:
a weapon set aside put back the powerup set aside before it, gone for again
every second - Sidewinder's overshield and camouflage under the bridge) */
#define BOTS_UNREACHABLE_ITEMS 8
/* port: turrets: a bot on foot mans one no further than this, the enemy it
saw no further from the turret than this; and the most turrets kept */
#define BOTS_TURRET_REACH 12.0f
#define BOTS_TURRET_RANGE 60.0f
#define BOTS_MAXIMUM_TURRETS 16
/* port: teleporters as shortcuts (bots_shortcut_weigh): taken when the way
through (the walk to the entrance, and from the exit to the goal) is no more
than this share of the walk, and this many units shorter at least; a bot
weighs them again no sooner than this after (and not for the same goal);
at most this many bots go for one teleporter at once */
#define BOTS_SHORTCUT_SHARE 0.8f
#define BOTS_SHORTCUT_GAIN 15.0f
#define BOTS_SHORTCUT_WEIGH_SECONDS 5
#define BOTS_SHORTCUT_BOTS 2
/* port: a teleporter that failed a bot (it stood on the entrance and was
not sent, or did not come to it in time) left be this long; one that
failed three times and never sent a bot, left be for the map */
#define BOTS_SHORTCUT_REST_SECONDS 30

enum
{
	_bots_skill_easy = 0,
	_bots_skill_normal,
	_bots_skill_heroic,
	_bots_skill_legendary,
	NUMBER_OF_BOTS_SKILLS,
};

/* where a bot is going */
enum
{
	_bots_goal_none = 0,
	_bots_goal_objective,
	_bots_goal_enemy,
	_bots_goal_roam,
	/* a better weapon, or a powerup, to pick up */
	_bots_goal_item,
	/* a vehicle's seat to get into (or a vehicle on its side to right) */
	_bots_goal_vehicle,
};

/* port: what stands before a stuck bot (bots_stuck_obstacle) */
enum
{
	_bots_obstacle_none = 0,
	_bots_obstacle_low,
	_bots_obstacle_tall,
};

/* port: the powerups a bot goes for (a goal _bots_goal_item's), and those
it took (halo.log's moves line) */
enum
{
	_bots_powerup_none = 0,
	_bots_powerup_health,
	_bots_powerup_overshield,
	_bots_powerup_camouflage,
	NUMBER_OF_BOTS_POWERUPS,
};

/* a vehicle's seat, as a bot takes it */
enum
{
	_bots_seat_driver = 0,
	_bots_seat_gunner,
	_bots_seat_passenger,
	NUMBER_OF_BOTS_SEATS,
};

/* why a bot got out of a vehicle (halo.log's census) */
enum
{
	_bots_exit_none = 0,
	_bots_exit_flipped,
	_bots_exit_stuck,
	_bots_exit_goal,
	_bots_exit_driverless,
	/* thrown out, or the vehicle gone */
	_bots_exit_other,
	/* port: a turret's: no enemy before it a while, or none its gun turns
	to; and its gunner hurt (being killed on it) */
	_bots_exit_no_enemy,
	_bots_exit_hurt,
	NUMBER_OF_BOTS_EXITS,
};

/* the vehicle types (vehicles.c's vehicle_type) a bot drives: on the ground */
enum
{
	_bots_vehicle_type_human_tank = 0,
	_bots_vehicle_type_human_jeep = 1,
	_bots_vehicle_type_alien_scout = 4,
	/* port: a gun on a mount, which does not move */
	_bots_vehicle_type_turret = 6,
};

/* a path search's state (the helper thread's hand-off) */
enum
{
	_bots_search_idle = 0,
	_bots_search_waiting,
	_bots_search_running,
	_bots_search_done,
};

/* port: what a path search is for: the bot's path, the walk from a
teleporter's exit to the bot's goal (measured, not walked: bots_shortcut_
weigh), or the bot's path to the teleporter's entrance it takes */
enum
{
	_bots_search_for_path = 0,
	_bots_search_for_exit,
	_bots_search_for_entrance,
	/* port: the bot's path to a hop's entrance (bots_nav.c: a jump, a drop,
	a teleporter or a ladder on the way to a region its search does not
	walk to) */
	_bots_search_for_hop,
};

/* port: a bot's teleporter shortcut (bots_shortcut_weigh): none, the walk
from its exit being measured, or on the way to its entrance */
enum
{
	_bots_shortcut_none = 0,
	_bots_shortcut_measuring,
	_bots_shortcut_going,
};

/* port: how a bot came to go through a teleporter (halo.log's census) */
enum
{
	_bots_through_shortcut = 0,
	_bots_through_last_way,
	_bots_through_by_chance,
	NUMBER_OF_BOTS_THROUGH,
};

/* ---------- structures */

struct bots_skill_definition
{
	char const *name;
	/* seconds from first seeing an enemy to firing */
	real reaction_seconds;
	/* how far it sees an enemy (world units), and how wide (degrees) */
	real sight_distance;
	real field_of_view_degrees;
	/* the aim error on taking a target (degrees), and the seconds it takes
	to settle to a third */
	real aim_error_degrees;
	real aim_settle_seconds;
	/* the most it turns a tick (degrees) */
	real turn_degrees_per_tick;
	/* fires while the aim is within this many times the target's size */
	real fire_cone;
	/* how much of a moving target's lead it takes (0 none, 1 all) */
	real lead;
	/* the chance a second of throwing a grenade at middle range, and of
	jumping while fighting */
	real grenade_chance;
	real jump_chance;
	/* the ticks of a burst, and of a pause between bursts */
	short burst_ticks;
	short pause_ticks;
	/* port: the body's vitality (0 to 1) below which it goes for a health
	pack, out of a fight and in one (0: never); and how far it goes for an
	overshield or camouflage */
	real heal_below;
	real heal_below_fighting;
	real powerup_reach;
};

/* (port: toned down Oct 17 2026, the owner finding them too strong: each
reacts later, its aim further off and slower to settle, leads less, fires
looser, shorter bursts; easy, normal, heroic, legendary were 0.75/0.45/
0.30/0.18 s, 12/7/4/2.2 degrees settling in 1.4/0.9/0.6/0.35 s) */
static struct bots_skill_definition const bots_skills[NUMBER_OF_BOTS_SKILLS] =
{
	{ "easy", 0.90f, 20.0f, 100.0f, 14.0f, 1.8f, 7.0f, 2.4f, 0.2f, 0.03f, 0.00f, 5, 16, 0.35f, 0.00f, 15.0f },
	{ "normal", 0.60f, 30.0f, 110.0f, 9.5f, 1.3f, 11.0f, 2.0f, 0.5f, 0.08f, 0.05f, 7, 12, 0.50f, 0.25f, 25.0f },
	{ "heroic", 0.42f, 38.0f, 130.0f, 6.0f, 0.9f, 16.0f, 1.7f, 0.65f, 0.12f, 0.12f, 9, 9, 0.65f, 0.35f, 30.0f },
	{ "legendary", 0.28f, 48.0f, 150.0f, 3.5f, 0.6f, 24.0f, 1.4f, 0.85f, 0.18f, 0.20f, 12, 6, 0.80f, 0.45f, 40.0f },
};

/* the vehicle tag's fields a bot drives by (vehicle_definitions.h keeps the
tag opaque; the places are those actor_moving.c's fragment pins for the
campaign's drivers) */
struct bots_vehicle_definition
{
	byte unit[0x2F4];
	short vehicle_type;
	short pad2f6;
	byte unknown2f8[0x94];
	real ai_pathfinding_radius;
};
typedef char bots_vehicle_type_offset_assert[
	offsetof(struct bots_vehicle_definition, vehicle_type) == 0x2F4 ? 1 : -1];
typedef char bots_vehicle_pathfinding_radius_offset_assert[
	offsetof(struct bots_vehicle_definition, ai_pathfinding_radius) == 0x38C ? 1 : -1];

/* a path search, for the helper thread (or the tick) */
struct bots_search
{
	volatile long state;
	/* the asking: from where, to where, by whom (the unit, or the vehicle
	it drives: what the search ignores in the way), how wide, and the object
	at the goal the path goes up to (a vehicle to get into), NONE for none */
	long bot_index;
	long unit_index;
	real radius;
	long target_object_index;
	real_point3d start_point;
	long start_surface_index;
	real_point3d goal_point;
	long goal_surface_index;
	/* asked at this tick, for this goal (the bot's goal serial); port: and
what for (_bots_search_for_path ...) */
	long asked_tick;
	long goal_serial;
	short purpose;
	/* the answer */
	boolean found;
	struct path_state state_memory;
};

struct bots_bot
{
	long player_index;
	long unit_index;
	unsigned long random;

	/* where it looks (radians) */
	real yaw;
	real pitch;

	/* its target */
	long target_player_index;
	long target_seen_tick;
	long target_first_seen_tick;
	real_point3d target_last_position;
	long target_last_surface_index;
	real aim_error_yaw;
	real aim_error_pitch;
	long next_look_tick;
	/* the last time it was hurt, and its vitality then */
	real last_vitality;
	long hurt_tick;

	/* where it is going */
	short goal_kind;
	long goal_serial;
	real_point3d goal_point;
	long goal_surface_index;
	long goal_tick;
	/* the goal must be stood on (a flag stand, a flag to return): the last
	bit walked straight */
	boolean goal_exact;
	short roam_index;
	/* the roaming place is towards an enemy (looked at again sooner) */
	boolean hunting;
	/* the item it fetches (_bots_goal_item), and when it last pressed the
	action button for it */
	long item_index;
	long item_press_tick;
	/* when it first pressed it for this item */
	long item_first_press_tick;
	/* port: the item a powerup (_bots_powerup_none for a weapon), given up
	on at this tick; when the bot first stood at it, and how far it was from
	it last tick (a powerup gone while it stood there: taken); the powerup it
	went for last (NONE once done with); when it next
	looks for one; its body's vitality last tick (a health pack's: up) */
	short item_powerup;
	long powerup_index;
	long item_deadline_tick;
	long item_arrived_tick;
	real item_last_distance;
	long powerup_check_tick;
	real last_body_vitality;
	/* the path there (steps of the engine's path), and when it was found */
	struct path_result path;
	long path_tick;
	long path_failures;
	/* off a surface closed to the searches (beside a door): away from what
	closed it, until then */
	long escape_until_tick;
	real escape_direction;
	/* its path goes to a teleporter (its goal is beyond, on ground the
	search does not walk to): onto it at the path's end, since then */
	boolean via_teleporter;
	real_point3d via_point;
	long via_tick;
	/* port: the teleporter (or ladder) it goes to; on a ladder: since when
	it climbs, 0 before, and since when it is over the top */
	long via_index;
	long via_climb_tick;
	long via_top_tick;
	boolean ladder_jump;
	/* port: a teleporter as a shortcut to a goal it could walk to
	(bots_shortcut_weigh): how far along (_bots_shortcut_none ...), which
	teleporter, for which goal (its serial, and where it was), the walk it
	was weighed against (as little as it is, for a search that stopped
	short), the way to its entrance as weighed (and whether a search to it
	has said how far it is: taken once it has) and the walk from its exit,
	measured; when it gives up going there, since when it stands on the
	entrance (0 off it); and when it next weighs the teleporters, the goal it
	last weighed them for */
	short shortcut_phase;
	long shortcut_index;
	long shortcut_serial;
	real_point3d shortcut_goal;
	real shortcut_walk;
	boolean shortcut_walk_short;
	real shortcut_entrance;
	boolean shortcut_entrance_known;
	real shortcut_beyond;
	long shortcut_tick;
	long shortcut_deadline_tick;
	long shortcut_on_tick;
	long shortcut_weigh_tick;
	long shortcut_weighed_serial;
	/* its path ends short of the goal (the search found no way there: a
	platform the walkable surfaces do not reach, Wizard's flags'); at its
	end, straight on with jumps until then, and the game's goals left a
	while after */
	boolean path_short;
	long climb_until_tick;
	long climb_start_tick;
	real climb_best_distance;
	long objective_rest_until_tick;
	/* where it was last tick (a teleporter's jump) */
	real_point3d last_position;
	/* an item it found no way to (on a ledge), not fetched again until then */
	long unreachable_item_indices[BOTS_UNREACHABLE_ITEMS];
	long unreachable_item_until_ticks[BOTS_UNREACHABLE_ITEMS];
	long unreachable_item_next;
	/* whether it moves, checked once a second */
	real_point3d stuck_position;
	long stuck_tick;
	short stuck_count;
	long unstick_until_tick;
	real unstick_direction;
	/* port: since when it gets past, and whether with a jump */
	long unstick_from_tick;
	boolean unstick_jump;

	/* fighting: the side it strafes to and until when; bursts */
	real strafe_sign;
	long strafe_until_tick;
	long burst_until_tick;
	long pause_until_tick;
	long grenade_tick;
	long melee_tick;
	long item_check_tick;

	/* the buttons held last tick (a press is a tick held, then released) */
	unsigned long previous_control_flags;

	/* its path search (a slot of bots_globals.searches), NONE for none */
	long search_slot;

	/* the game's object it held last tick (halo.log's objective count): 0
	none, 1 a flag, 2 a ball */
	short held_objective;
	/* CTF: at the other team's stand last tick */
	boolean at_enemy_stand;

	/* vehicles: the vehicle and seat it goes for (NONE for none: a seat a
	bot goes for is its own), to right it instead (on its side), since when,
	where its seat's entrance is (and where the vehicle was then), when it
	last pressed the action button there and how often; when it next looks
	for one, none until then (just out of one), and one that failed it, not
	again until then */
	long vehicle_index;
	short vehicle_seat_index;
	boolean vehicle_flip;
	long vehicle_tick;
	real_point3d vehicle_entrance;
	real_point3d vehicle_entrance_from;
	long vehicle_press_tick;
	short vehicle_presses;
	long vehicle_check_tick;
	long vehicle_rest_until_tick;
	/* port: when it gives up on the vehicle it goes for (the walk there and
	a while: bots_vehicle_approach); since when its unit lives (spawned) */
	long vehicle_deadline_tick;
	long spawn_tick;
	long vehicle_avoid_index;
	long vehicle_avoid_until_tick;
	/* port: the vehicle it goes for is a turret (its gunner's seat, with no
	driver); when it next looks for one */
	boolean vehicle_turret;
	long turret_check_tick;
	/* riding: the vehicle (NONE on foot), its seat, since when, where it was
	last tick (the census's distance); whether it moves while driven
	(checked once a second), backing off until then; since when it has
	had no driver, or lain on its side; why the bot is getting out */
	long ride_vehicle_index;
	short ride_seat;
	long ride_tick;
	real_point3d ride_last_position;
	real_point3d ride_stuck_position;
	long ride_stuck_tick;
	short ride_stuck_count;
	boolean ride_driving;
	boolean ride_reversing;
	real ride_direction;
	long ride_reverse_until_tick;
	real ride_reverse_yaw;
	long ride_driverless_tick;
	long ride_flipped_tick;
	short ride_exit;
	/* port: it rides a turret; since when its gun has not turned to the
	enemy (out of its arc), 0 while it has */
	boolean ride_turret;
	long ride_turret_off_tick;
	/* since when it holds a turret's charging trigger, 0 while it does not */
	long ride_charge_tick;
	/* its kills counted so far (the census's kills riding) */
	long kills_seen;
	/* (debug: HALO_BOT_TRACE) its path searches */
	long searches;
	/* port: the weapons' definitions it held last tick (halo.log's play
	line: the weapons picked up) */
	long held_definitions[MAXIMUM_WEAPONS_PER_UNIT];
	/* port: the hop it goes for (bots_nav.c), NONE for none, and for which
	goal (its serial); since when it takes it at the entrance (0 before),
	when it jumped (0 before); the buttons the hop wants this tick; a
	teleporter or ladder hop handed to the teleporters' way (NONE for none),
	and the region it goes to */
	long hop_index;
	long hop_serial;
	long hop_take_tick;
	long hop_jump_tick;
	boolean hop_press_jump;
	boolean hop_crouch;
	long hop_via;
	/* port: the goal it last weighed the jumps and drops for, as shortcuts */
	long jump_weighed_serial;
	/* port: a goal it found no way to (the hops' regions: none joins them),
	not gone for again until then: an enemy's place, a roaming place (nor
	any near it) */
	long hunt_rest_until_tick;
	real_point3d avoid_point;
	long avoid_until_tick;
	/* port: how long it has stood still while it walks (ticks), and when it
	next looks at what stands before it; getting past: the way round it
	tried (a side, the time) */
	short slow_ticks;
	long probe_tick;
	long crouch_from_tick;
	long crouch_until_tick;
	long fight_block_tick;
	real_point3d fight_block_position;
};

/* ---------- globals */

static struct
{
	boolean map_ready;
	long roam_point_count;
	real_point3d roam_points[BOTS_MAXIMUM_ROAM_POINTS];
	long roam_surfaces[BOTS_MAXIMUM_ROAM_POINTS];
	/* the teleporters: where each is entered (its ground and surface) and
	where it comes out; port: and the ladders (bots_prepare_ladders), entered
	at their foot and come out of at their top's landing, climbed facing
	them (the climb's direction) up to the top's height */
	long teleporter_count;
	struct
	{
		real_point3d entrance;
		long entrance_surface_index;
		real_point3d exit;
		boolean ladder;
		real climb_direction;
		real top_z;
		/* the times bots climbed it, and failed to (a climbable surface no
		biped goes up, Damnation's: left be after three, none up it); port: a
		teleporter's, the times bots went for it as a shortcut and it failed
		them (bots_shortcut_fail) */
		long climbs;
		long failures;
		/* port: a teleporter's channel (its source flag's team index), the
		exit's ground and surface (where a search measures the walk on from:
		bots_shortcut_weigh), when it is gone for again after a failure, and
		the times it sent a bot, by how the bot came to it (halo.log) */
		short channel;
		real_point3d exit_ground;
		long exit_surface_index;
		long rest_until_tick;
		long through[NUMBER_OF_BOTS_THROUGH];
	} teleporters[BOTS_MAXIMUM_TELEPORTERS];
	struct bots_bot bots[BOTS_MAXIMUM];
	/* the skill setting, read once a tick (bots_update_actions) for every
	bot (a lookup of the environment and the settings, twice a bot a tick
	before) */
	short skill;
	/* CTF: the flags (weapon_is_flag) among the weapons, in the objects'
	order, found once a tick when a bot first asks (every bot walked every
	weapon for them every tick before); nothing a bot does in its tick makes
	or deletes an object */
	boolean flags_found;
	boolean flags_overflow;
	long flag_count;
	long flag_indices[8];
	/* the path searches (allocated once a game has bots) */
	struct bots_search *search_slots;

	/* the statistics of halo.log's line */
	unsigned long long tick_us;
	unsigned long long search_us;
	long ticks;
	long bot_ticks;
	long searches;
	long search_failures;
	/* searches that stopped short of the goal (a path to as near as they
	came), and those that ran out of nodes */
	long search_partials;
	long search_overflows;
	/* paths taken without the obstacle avoidance, which found no way */
	long search_unavoided;
	/* the paths whose smoothing went nowhere, taken unsmoothed */
	long search_unsmoothed;
	long flag_grabs;
	long ball_grabs;
	/* CTF: the times a bot stood at the other team's flag stand (with the
	flag there or not) */
	long stand_visits;
	long last_report_tick;
	/* vehicles: the bots' getting in, out (by why), the vehicles they righted,
	the distance their vehicles were driven, and the last census */
	long vehicle_entries;
	long vehicle_entries_by_seat[NUMBER_OF_BOTS_SEATS];
	long vehicle_exits[NUMBER_OF_BOTS_EXITS];
	long vehicle_flips;
	real driven_distance;
	/* the kills of bots riding (driving, and in the other seats), and the
	ticks a gunner or passenger bot held the trigger */
	long riding_kills[2];
	long riding_fire_ticks;
	/* the path searches bots driving asked for */
	long driving_searches;
	/* port: the bots' getting in by the vehicle's type (bots_vehicle_kind:
	tanks, jeeps, scouts - the Scorpion, the Warthog, the Ghost), the tick
	of the game's first (0 for none yet), and the ticks bots rode, all of
	them together */
	long vehicle_entries_by_kind[3];
	long first_entry_tick;
	long riding_ticks;
	/* port: the seats a bot took over from a teammate further from them,
	and the vehicles given up on the way (too long, or no way in) */
	long vehicle_take_overs;
	long vehicle_giveups;
	long last_census_tick;
	/* port: the game's vehicles logged (its first census) */
	boolean vehicles_listed;
	/* port: the bots' moves (halo.log's moves line, each minute): the times
	a bot stood stuck while it walked, by what was before it - a low wall
	(the level, scenery or a machine at its knees, nothing at its chest: a
	jump's height), a tall one, or nothing (another bot, a slope it slid
	on) - and the goals given up after three */
	long stuck_events;
	long stuck_low;
	long stuck_tall;
	long stuck_clear;
	long stuck_giveups;
	/* the level's climbable surfaces (a ladder: collision_bsp's climbable
	flag, which a biped walks up as it faces it: bipeds.c) */
	long climbable_surfaces;
	/* port: the ladders made of them, and the times a bot climbed one */
	long ladder_count;
	long ladder_tries;
	long ladder_climbs;
	/* port: the teleporters as shortcuts (bots_shortcut_weigh): the map's
	teleporters (not the ladders), the walks from an exit measured, the
	shortcuts taken, the bots sent through by how they came (a shortcut, the
	last way to a goal the walkable surfaces do not reach, by chance), those
	that failed (bots_shortcut_fail), and those dropped on the way (another
	goal, a vehicle, the entrance further than it was weighed) */
	long teleporter_shortcuts;
	long shortcut_measures;
	long shortcut_takes;
	long shortcut_through[NUMBER_OF_BOTS_THROUGH];
	long shortcut_failures;
	long shortcut_drops;
	/* port: the powerups bots went for and took (by kind), and the times a
	bot's body was restored (a health pack, gone for or walked over) */
	long powerups_sought[NUMBER_OF_BOTS_POWERUPS];
	long powerups_taken[NUMBER_OF_BOTS_POWERUPS];
	long bodies_restored;
	/* port: turrets: the bots' getting on one, their kills from one, and the
	ticks they held a turret's trigger */
	long turret_entries;
	long turret_kills;
	long turret_fire_ticks;
	/* the turrets on the map, found with the powerups (bots_powerups_find:
	once a second, a walk of the vehicles), in the objects' order */
	long turret_count;
	long turret_indices[BOTS_MAXIMUM_TURRETS];
	/* port: the bots' play (halo.log's play line, each minute): the times
	stuck while fighting and while walking with no path; grenades thrown,
	jumps (fighting, and otherwise: getting unstuck, a ledge, a ladder);
	weapons picked up, and of them power weapons (bots_weapon_power); the
	bot-ticks alive, holding a power weapon, and holding two weapons */
	struct
	{
		long stuck_fighting;
		long stuck_pathless;
		long grenades_thrown;
		long jumps_fighting;
		long jumps_moving;
		long weapon_pickups;
		long power_pickups;
		long alive_ticks;
		long power_ticks;
		long two_weapon_ticks;
		/* port: goals found to have no way to them (no hop joins their
		region to the bot's), the walk blocked in a fight (the strafe
		turned), a low wall jumped as it came to it */
		long goals_unreachable;
		long fight_blocks;
		long walk_jumps;
		/* port: jumps and drops taken as shortcuts (bots_jump_shortcut) */
		long jump_shortcuts;
	} play;
} bots_globals;

/* port: the powerups lying on the map - health packs, overshields, active
camouflage - found once a second for every bot (a walk of the equipment; a
bot walked every item each second for one before), in the objects' order;
an item is there only while it has spawned (taken, it is gone until the
game spawns it again), so a bot never goes for one that is not there */
#define BOTS_MAXIMUM_POWERUPS 32
static struct
{
	unsigned long map_generation;
	long found_tick;
	long count;
	struct
	{
		long object_index;
		short kind;
		real_point3d position;
		/* port: the walkable surface under it (bots_nav.c: whether a bot
		can get there), NONE for none */
		long surface_index;
	} items[BOTS_MAXIMUM_POWERUPS];
} bots_powerups;

/* the walkable surfaces the bots' searches read: a copy of the structure's
pathfinding surfaces, less those learnt to be in the way (scenery and
machines) and those with no room above them */
static struct
{
	boolean active;
	struct structure_bsp const *source;
	struct structure_bsp structure;
	byte *walkable;
	/* port: the vehicles' copy, their searches': the same, less what a
	vehicle stuck at (a bank a biped walks up, a ledge's foot) */
	struct structure_bsp vehicle_structure;
	byte *vehicle_walkable;
	long vehicle_closed_count;
	/* the objects whose surfaces are closed */
	long learned[32];
	long learned_count;
	long closed_count;
	/* the surfaces looked at for room above them, and those closed */
	long scan_index;
	boolean scan_done;
	long roofed_count;
	unsigned long long scan_us;
	long scan_ticks;
	/* port: the ramps opened to the bots' searches (bots_nav.c: surfaces a
	biped walks on that the AI's walkable surfaces leave out), and how many */
	boolean ramps_opened;
	long ramps_open_count;
} bots_paths;

/* the helper thread */
static struct
{
	boolean started;
	boolean synchronous;
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t wake;
	volatile long pending;
	volatile long generation;
	volatile unsigned long long helper_us;
} bots_helper = { FALSE, FALSE, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0 };

/* (HALO_TICK_PROFILE) the bots' tick by part, and the rays they cast, for
halo.log's bots line */
enum
{
	_bots_part_look = 0,
	_bots_part_fight,
	_bots_part_goal,
	_bots_part_take,
	_bots_part_walk,
	_bots_part_rest,
	_bots_part_scan,
	NUMBER_OF_BOTS_PARTS,
};

static struct
{
	int enabled;
	unsigned long long part_us[NUMBER_OF_BOTS_PARTS];
	/* rays down to the ground and lines of sight: cast, and their time */
	long below_rays;
	long sight_rays;
	unsigned long long below_us;
	unsigned long long sight_us;
} bots_profile = { -1 };

static int bots_profile_enabled(void)
{
	if (bots_profile.enabled < 0)
	{
		char const *setting = getenv("HALO_TICK_PROFILE");

		bots_profile.enabled = setting && atoi(setting) ? 1 : 0;
	}
	return bots_profile.enabled;
}

/* a part's time to here from *started, which moves on to now */
static void bots_profile_part(int part, unsigned long long *started)
{
	unsigned long long now;

	if (!bots_profile.enabled)
		return;
	now = vita_host_time_us();
	bots_profile.part_us[part] += now - *started;
	*started = now;
}

/* ---------- settings */

static long bots_setting_integer(char const *variable, char const *name)
{
	char const *value = getenv(variable);

	return value && value[0] ? atol(value) : config_integer(name);
}

static char const *bots_setting_string(char const *variable, char const *name)
{
	char const *value = getenv(variable);

	return value && value[0] ? value : config_string(name);
}

long bots_wanted_count(void)
{
	long count = bots_setting_integer("HALO_BOTS", "bots.count");

	return count < 0 ? 0 : count > BOTS_MAXIMUM ? BOTS_MAXIMUM : count;
}

short bots_skill(void)
{
	char const *name = bots_setting_string("HALO_BOT_SKILL", "bots.skill");
	short skill;

	for (skill = 0; skill < NUMBER_OF_BOTS_SKILLS; skill++)
	{
		if (!strcmp(name, bots_skills[skill].name))
			return skill;
	}
	/* (a number, 0 to 3) */
	if (name[0] >= '0' && name[0] <= '3' && !name[1])
		return (short)(name[0] - '0');
	return _bots_skill_normal;
}

long bots_online_fill(void)
{
	char const *value = bots_setting_string("HALO_BOTS_ONLINE", "bots.online");
	long count;

	/* (Online alone: not Same Wi-Fi, System Link or ad hoc) */
	if (network_connection_running() != NETWORK_CONNECTION_ONLINE)
		return 0;
	if (!strcmp(value, "full"))
		return BOTS_MAXIMUM;
	if (!strcmp(value, "off"))
		return 0;
	count = atol(value);
	return count < 0 ? 0 : count > BOTS_MAXIMUM ? BOTS_MAXIMUM : count;
}

short bots_teams(void)
{
	return strcmp(bots_setting_string("HALO_BOT_TEAMS", "bots.teams"), "against") ? _bots_teams_even :
		_bots_teams_against;
}

int bots_machine_is_bot(long machine_index)
{
	return machine_index >= BOTS_FIRST_MACHINE && machine_index < BOTS_FIRST_MACHINE + BOTS_MAXIMUM;
}

int bots_player_is_bot(long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

	return player && player->local_player_index == NONE &&
		bots_machine_is_bot(player->network_player_data.machine_index);
}

void bots_network_player(long bot_index, struct network_player *player)
{
	/* (the Marines of Halo's campaign; eleven characters at most) */
	static char const *const names[BOTS_MAXIMUM] =
	{
		"Johnson", "Jenkins", "Mendoza", "Bisenti", "Stacker", "Polaski", "Chips", "Kojo",
		"Rawley", "Riley", "Wellsley", "Hall", "Fitzgerald", "Locklear", "Mitchell",
	};
	char const *name = names[bot_index % BOTS_MAXIMUM];
	long index;

	csmemset(player, 0, sizeof(*player));
	for (index = 0; index < (long)NUMBEROF(player->name) - 1 && name[index]; index++)
		player->name[index] = (wchar_t)name[index];
	player->name[index] = 0;
	player->primary_color_index = NONE;
	player->icon_index = 0;
	player->machine_index = (char)(BOTS_FIRST_MACHINE + bot_index);
	player->controller_index = 0;
	player->team_index = 0;
	player->player_list_index = NONE;
}

/* ---------- small helpers */

static boolean bots_peaceful(void)
{
	static int peaceful = -1;

	if (peaceful < 0)
	{
		char const *setting = getenv("HALO_BOT_PEACEFUL");

		peaceful = setting && atoi(setting) ? 1 : 0;
	}
	return peaceful != 0;
}

/* port: (debug) HALO_BOT_PEACEFUL_HUMANS=1: in Infection, the humans' bots
fight no one (the zombies' blows alone, for run_infection_test.sh) */
static boolean bots_peaceful_player(long player_index)
{
	static int humans = -1;

	if (bots_peaceful())
		return TRUE;
	if (humans < 0)
	{
		char const *setting = getenv("HALO_BOT_PEACEFUL_HUMANS");

		humans = setting && atoi(setting) ? 1 : 0;
	}
	return humans && infection_active() && !infection_player_is_zombie(player_index);
}

/* whether bots take vehicles (bots.vehicles, the Vita's settings panel's
Bot vehicles; HALO_BOT_VEHICLES=0: never, the bots on foot as before; read
each time, as the panel changes it at any time) */
static boolean bots_vehicles_enabled(void)
{
	return bots_setting_integer("HALO_BOT_VEHICLES", "bots.vehicles") != 0;
}

static unsigned long bots_random(struct bots_bot *bot)
{
	/* (xorshift32) */
	unsigned long x = bot->random ? bot->random : 0x9E3779B9UL;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	x &= 0xFFFFFFFFUL;
	bot->random = x;
	return x;
}

/* 0 to 1 */
static real bots_random_real(struct bots_bot *bot)
{
	return (real)(bots_random(bot) & 0xFFFFFF) / (real)0x1000000;
}

/* -1 to 1 */
static real bots_random_signed(struct bots_bot *bot)
{
	return bots_random_real(bot) * 2.0f - 1.0f;
}

static real bots_angle_difference(real a, real b)
{
	real difference = (real)fmod(a - b, 2.0f * BOTS_PI);

	if (difference > BOTS_PI)
		difference -= 2.0f * BOTS_PI;
	else if (difference < -BOTS_PI)
		difference += 2.0f * BOTS_PI;
	return difference;
}

static real bots_distance2d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y;

	return (real)sqrt(dx * dx + dy * dy);
}

static real bots_distance3d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;

	return (real)sqrt(dx * dx + dy * dy + dz * dz);
}

/* the answers of bots_surface_below's rays kept by the point they were cast
from, while the bots' tick runs (the stands, the nav points and the flags
lying still that the bots ask for every tick): a structure test's answer is a
function of the bsp, its flags, the breakable surfaces' state and the ray
alone (collisions.c's obstruction memo is kept so), and this one tests no
objects nor water, so a point asked again (the same bits) is answered as it
was while the bsp (halo_structure_bsp_generation), the map and the breakable
surfaces' flags (compared as each bots' tick starts; nothing in it breaks
one) are as they were */
#define BOTS_GROUND_MEMO_ENTRIES 64
static struct
{
	boolean active;
	unsigned long map_generation;
	unsigned long structure_bsp_generation;
	byte breakable_surface_flags[MAXIMUM_BREAKABLE_SURFACES_PER_MAP / 8];
	struct
	{
		boolean used;
		unsigned long point[3];
		long surface_index;
		real_point3d ground;
	} entries[BOTS_GROUND_MEMO_ENTRIES];
} bots_ground_memo;

/* at a bots' tick's start: the answers kept dropped if the level changed */
static void bots_ground_memo_begin(void)
{
	byte const *flags = breakable_surface_flags_get();
	long index;

	if (bots_ground_memo.map_generation != halo_map_generation ||
		bots_ground_memo.structure_bsp_generation != halo_structure_bsp_generation ||
		memcmp(bots_ground_memo.breakable_surface_flags, flags, sizeof(bots_ground_memo.breakable_surface_flags)))
	{
		for (index = 0; index < BOTS_GROUND_MEMO_ENTRIES; index++)
			bots_ground_memo.entries[index].used = FALSE;
		bots_ground_memo.map_generation = halo_map_generation;
		bots_ground_memo.structure_bsp_generation = halo_structure_bsp_generation;
		memcpy(bots_ground_memo.breakable_surface_flags, flags, sizeof(bots_ground_memo.breakable_surface_flags));
	}
	bots_ground_memo.active = TRUE;
}

/* the surface of the level under a point (a step up included), and the
ground point there; NONE over nothing */
static long bots_surface_below(real_point3d const *point, real_point3d *ground)
{
	struct collision_result collision;
	real_point3d start = *point;
	real_vector3d down = { 0.0f, 0.0f, -6.0f };
	unsigned long long started;
	unsigned long bits[3];
	long entry_index = NONE;
	boolean hit;

	if (bots_ground_memo.active)
	{
		unsigned long hash = 2166136261UL;
		long index;

		memcpy(bits, point, sizeof(bits));
		for (index = 0; index < 3; index++)
			hash = (hash ^ bits[index]) * 16777619UL;
		entry_index = (long)((hash >> 8) % BOTS_GROUND_MEMO_ENTRIES);
		if (bots_ground_memo.entries[entry_index].used && !memcmp(bots_ground_memo.entries[entry_index].point, bits, sizeof(bits)))
		{
			if (bots_ground_memo.entries[entry_index].surface_index != NONE && ground)
				*ground = bots_ground_memo.entries[entry_index].ground;
			return bots_ground_memo.entries[entry_index].surface_index;
		}
	}
	started = bots_profile.enabled > 0 ? vita_host_time_us() : 0;
	start.z += 0.4f;
	hit = collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		&start, &down, NONE, &collision) && collision.type == _collision_result_structure;
	if (started)
	{
		bots_profile.below_rays++;
		bots_profile.below_us += vita_host_time_us() - started;
	}
	if (entry_index != NONE)
	{
		bots_ground_memo.entries[entry_index].used = TRUE;
		memcpy(bots_ground_memo.entries[entry_index].point, bits, sizeof(bits));
		bots_ground_memo.entries[entry_index].surface_index = hit ? collision.surface_index : NONE;
		bots_ground_memo.entries[entry_index].ground = collision.point;
	}
	if (hit)
	{
		if (ground)
			*ground = collision.point;
		return collision.surface_index;
	}
	return NONE;
}

/* the surface a unit stands on (a biped's support surface), and its point */
static long bots_unit_surface(long unit_index, real_point3d *point)
{
	struct object_datum *object = object_get(unit_index);
	struct biped_datum *biped = biped_try_and_get(unit_index);

	*point = object->object.position;
	if (object->object.parent_object_index != NONE)
		return NONE;
	if (biped && biped->biped.support_surface_index != NONE)
		return biped->biped.support_surface_index;
	return bots_surface_below(&object->object.position, point);
}

/* the vehicle the unit rides in, NONE for none */
static long bots_unit_vehicle(long unit_index)
{
	struct object_datum *object = object_try_and_get(unit_index);

	if (!object || object->object.parent_object_index == NONE ||
		!object_try_and_get_and_verify_type(object->object.parent_object_index, _object_mask_vehicle))
	{
		return NONE;
	}
	return object->object.parent_object_index;
}

static struct bots_vehicle_definition *bots_vehicle_definition(long vehicle_index)
{
	return (struct bots_vehicle_definition *)tag_get(VEHICLE_DEFINITION_TAG, object_get(vehicle_index)->definition_index);
}

/* how wide a vehicle is to the path searches (its definition's AI
pathfinding radius, as the campaign's drivers search: actor_path_input_new) */
static real bots_vehicle_path_radius(long vehicle_index)
{
	real radius = bots_vehicle_definition(vehicle_index)->ai_pathfinding_radius;

	return radius > 0.1f ? MIN(radius, 2.0f) : 1.0f;
}

/* a vehicle a bot drives: on the ground (a Warthog, a Scorpion, a Ghost;
not a turret, nor one that flies, which the walkable surfaces' paths are
not for) */
static boolean bots_vehicle_drivable(long vehicle_index)
{
	short type = bots_vehicle_definition(vehicle_index)->vehicle_type;

	return type == _bots_vehicle_type_human_tank || type == _bots_vehicle_type_human_jeep ||
		type == _bots_vehicle_type_alien_scout;
}

/* port: the kind of a vehicle a bot drives (bots_vehicle_worths' index):
0 a tank (the Scorpion), 1 a jeep (the Warthog, the rocket Warthog), 2 a
scout (the Ghost) */
static short bots_vehicle_kind(long vehicle_index)
{
	short type = bots_vehicle_definition(vehicle_index)->vehicle_type;

	return type == _bots_vehicle_type_human_tank ? 0 : type == _bots_vehicle_type_alien_scout ? 2 : 1;
}

/* port: what an empty vehicle's driver's seat is worth to a bot on foot, by
its kind - as players weigh them, a Scorpion worth a walk back to it, a
Ghost some way, a Warthog only on the way (the campaign's AI weighs none:
its encounters are handed theirs): how far the bot walks to it (reach), and
no further than this share of the way to its goal (but for one close); one
this close is gone for in any direction (fresh from its base, nearer
BOTS_VEHICLE_FRESH_SECONDS, ten units further, fifteen at least); else the
vehicle no further from the goal than the bot is and this slack; and how
much nearer it counts than it is (the choice's score). A Warthog's are the
rules bots had before (a reach of 30, 0.6 of the way, 5 of slack) */
static struct
{
	real reach;
	real share;
	real close;
	real slack;
	real bonus;
} const bots_vehicle_worths[3] =
{
	{ 50.0f, 1.0f, 20.0f, 20.0f, 12.0f },
	{ 30.0f, 0.6f, 0.0f, 5.0f, 0.0f },
	{ 40.0f, 0.8f, 10.0f, 10.0f, 5.0f },
};

/* port: the seat of a turret a bot mans: a vehicle that does not move, its
gun its gunner's - the vehicle type turret, or one with a gunner's seat and
no driver's (a Shade, a Custom Edition map's gun tower) - its first gunner's
seat (a turret's single seat may be its driver's too), else its first
driver's on a turret; NONE for a vehicle that is no turret. Its definition's
alone (a tag's seats: a few flags read) */
static short bots_turret_seat(long vehicle_index)
{
	struct unit_definition *definition = unit_definition_get(object_get(vehicle_index)->definition_index);
	boolean turret = bots_vehicle_definition(vehicle_index)->vehicle_type == _bots_vehicle_type_turret;
	short seat_index, gunner = NONE, driver = NONE;

	for (seat_index = 0; seat_index < definition->unit.seats.count; seat_index++)
	{
		if (unit_seat_is_gunner(vehicle_index, seat_index) && gunner == NONE)
			gunner = seat_index;
		if (unit_seat_is_driver(vehicle_index, seat_index) && driver == NONE)
			driver = seat_index;
	}
	if (!turret && (gunner == NONE || driver != NONE))
		return NONE;
	return gunner != NONE ? gunner : driver;
}

/* the unit in a vehicle's seat, NONE for none (unit_can_enter_seat's look
at the vehicle's riders) */
static long bots_seat_occupant(long vehicle_index, short seat_index)
{
	long child_index = object_get(vehicle_index)->object.first_child_object_index;

	while (child_index != NONE)
	{
		struct object_datum *child = object_get(child_index);

		if (TEST_FLAG(_object_mask_unit, child->object.type) && unit_get(child_index)->unit.parent_seat_index == seat_index)
			return child_index;
		child_index = child->object.next_object_index;
	}
	return NONE;
}

/* whether anyone rides in the vehicle */
static boolean bots_vehicle_has_riders(long vehicle_index)
{
	long child_index = object_get(vehicle_index)->object.first_child_object_index;

	while (child_index != NONE && !TEST_FLAG(_object_mask_unit, object_get(child_index)->object.type))
		child_index = object_get(child_index)->object.next_object_index;
	return child_index != NONE;
}

static short bots_seat_kind(long vehicle_index, short seat_index)
{
	if (unit_seat_is_driver(vehicle_index, seat_index))
		return _bots_seat_driver;
	if (unit_seat_is_gunner(vehicle_index, seat_index))
		return _bots_seat_gunner;
	return _bots_seat_passenger;
}

/* the unit's current weapon, NONE without one */
static long bots_current_weapon(struct unit_datum *unit)
{
	if (unit->unit.current_weapon_index < 0 || unit->unit.current_weapon_index >= MAXIMUM_WEAPONS_PER_UNIT)
		return NONE;
	return unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
}

/* whether a weapon has no rounds left (one with magazines) */
static boolean bots_weapon_empty(long weapon_index)
{
	struct weapon_datum *weapon = weapon_get(weapon_index);
	struct weapon_definition *definition = weapon_definition_get(weapon->definition_index);

	if (definition->weapon.magazines.count <= 0)
		return FALSE;
	return weapon->weapon.magazines[0].rounds_loaded + weapon->weapon.magazines[0].rounds_total <= 0;
}

/* how full the weapon's magazine is, 0 to 1 (1 for one without) */
static real bots_weapon_loaded(long weapon_index)
{
	struct weapon_datum *weapon = weapon_get(weapon_index);
	struct weapon_definition *definition = weapon_definition_get(weapon->definition_index);
	struct weapon_magazine_definition *magazine;

	if (definition->weapon.magazines.count <= 0)
		return 1.0f;
	magazine = TAG_BLOCK_GET_ELEMENT(&definition->weapon.magazines, 0, struct weapon_magazine_definition);
	if (magazine->rounds_loaded_maximum <= 0)
		return 1.0f;
	return (real)weapon->weapon.magazines[0].rounds_loaded / (real)magazine->rounds_loaded_maximum;
}

/* the speed of the weapon's projectile (world units a tick; 0 for one
that hits at once or none) */
static real bots_projectile_speed_compute(long definition_index)
{
	struct weapon_definition *definition = weapon_definition_get(definition_index);

	if (definition->weapon.triggers.count > 0)
	{
		struct weapon_trigger_definition *trigger =
			TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);

		if (trigger->projectile.index != NONE)
		{
			struct projectile_definition *projectile = projectile_definition_get(trigger->projectile.index);

			return 0.5f * (projectile->projectile.initial_velocity + projectile->projectile.final_velocity);
		}
	}
	return 0.0f;
}

/* how much a bot wants a weapon (the stock weapons, by their tags' names),
-1 for the game's own (a flag, a ball) */
static short bots_weapon_value_compute(long definition_index)
{
	static struct
	{
		char const *name;
		short value;
	} const values[] =
	{
		{ "rocket", 9 }, { "sniper", 8 }, { "shotgun", 8 }, { "fuel rod", 7 }, { "plasma_cannon", 7 },
		{ "plasma pistol", 3 }, { "pistol", 7 }, { "assault rifle", 6 }, { "needler", 5 }, { "plasma rifle", 5 },
		{ "flamethrower", 4 }, { "flag", -1 }, { "ball", -1 }, { "skull", -1 },
	};
	char const *name = tag_get_name(definition_index);
	long index;

	if (!name)
		return 4;
	for (index = 0; index < (long)NUMBEROF(values); index++)
	{
		if (strstr(name, values[index].name))
			return values[index].value;
	}
	return 4;
}

/* how a bot fires the weapon: the ticks between presses of a weapon fired a
press at a time (a latched trigger: the pistols, the sniper rifle, the
rocket launcher; or one that charges while held: the plasma pistol), 0 for
one held down in bursts */
static short bots_weapon_tap_ticks_compute(long definition_index)
{
	struct weapon_definition *definition = weapon_definition_get(definition_index);
	struct weapon_trigger_definition *trigger;
	real rate;

	if (definition->weapon.triggers.count <= 0)
		return 0;
	trigger = TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);
	if (!TEST_FLAG(trigger->flags, _weapon_trigger_latched_bit) && trigger->charging_time <= 0.0f)
		return 0;
	rate = trigger->initial_rate_of_fire > 0.0f ? trigger->initial_rate_of_fire : 3.0f;
	return (short)PIN((long)((real)TICKS_PER_SECOND / rate + 0.5f), 2, TICKS_PER_SECOND * 2);
}

/* the distance a bot keeps from its enemy with the weapon */
static real bots_weapon_range_compute(long definition_index)
{
	char const *name = tag_get_name(definition_index);

	if (!name)
		return 6.0f;
	if (strstr(name, "shotgun") || strstr(name, "flamethrower"))
		return 2.0f;
	if (strstr(name, "sniper"))
		return 18.0f;
	if (strstr(name, "rocket") || strstr(name, "fuel rod") || strstr(name, "plasma_cannon"))
		return 12.0f;
	if (strstr(name, "plasma pistol") || strstr(name, "needler"))
		return 5.0f;
	if (strstr(name, "pistol"))
		return 9.0f;
	return 7.0f;
}

/* port: what kind of a precision weapon it is, from its tag alone (not its
name: a Custom Edition map's own rifles too), bots_tactics.h's
_bots_precision_...: it zooms, its first trigger fires one projectile a
shot (no spread of pellets), and
- its projectile is a bullet, BOTS_PRECISION_SPEED or faster (world units
  a tick: the stock pistol's 10, the sniper rifle's 33; the plasma bolts'
  0.7 to 1.3), aimed true, its least error BOTS_PRECISION_ERROR at most (a
  degree: the pistol's 0.2, the sniper rifle's 0.5; the assault rifle's 2):
  a scope of two zoom levels or more, or BOTS_PRECISION_SCOPE times and up,
  makes a long one (the sniper rifle: two levels, 2x and 8x), a zoom alone
  a middle one (the pistol: 2x);
- or it is slower than BOTS_HEAVY_SPEED (the rocket's 0.37): a heavy one
  (the rocket launcher), which the bots holding it took to the vantage
  points before too.
The plasma pistol, the needler, the plasma and assault rifles and the
flamethrower (none zooms) and the shotgun (fifteen pellets) are none.
(Blood Gulch's, HALO_BOT_TRACE: each weapon's numbers in halo.log.) */
#define BOTS_PRECISION_SPEED 6.0f
#define BOTS_PRECISION_ERROR 0.0175f
#define BOTS_PRECISION_SCOPE 4.0f
#define BOTS_HEAVY_SPEED 1.0f
static short bots_weapon_precision_compute(long definition_index)
{
	struct weapon_definition *definition = weapon_definition_get(definition_index);
	struct weapon_trigger_definition *trigger;
	struct projectile_definition *projectile;
	real speed;

	if (definition->weapon.triggers.count <= 0)
		return _bots_precision_none;
	trigger = TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);
	if (trigger->projectile.index == NONE)
		return _bots_precision_none;
	projectile = projectile_definition_get(trigger->projectile.index);
	speed = 0.5f * (projectile->projectile.initial_velocity + projectile->projectile.final_velocity);
	if (getenv("HALO_BOT_TRACE"))
		platform_log("bots: trace weapon %s: zoom %d x%.1f-%.1f, %d a shot, projectile %s speed %.2f, error %.3f-%.3f",
			tag_get_name(definition_index), (int)definition->weapon.zoom_level_count,
			definition->weapon.zoom_magnification_minimum, definition->weapon.zoom_magnification_maximum,
			(int)trigger->projectiles_per_shot, tag_get_name(trigger->projectile.index), speed,
			trigger->projectile_error_angle_lower_bound, trigger->projectile_error_angle_upper_bound);
	if (definition->weapon.zoom_level_count < 1 || trigger->projectiles_per_shot > 1)
		return _bots_precision_none;
	if (speed >= BOTS_PRECISION_SPEED && trigger->projectile_error_angle_lower_bound <= BOTS_PRECISION_ERROR)
	{
		return definition->weapon.zoom_level_count >= 2 ||
			definition->weapon.zoom_magnification_maximum >= BOTS_PRECISION_SCOPE ? _bots_precision_long : _bots_precision_mid;
	}
	return speed > 0.0f && speed < BOTS_HEAVY_SPEED ? _bots_precision_heavy : _bots_precision_none;
}

/* what the bots know of a weapon definition (its value, the distance kept
with it, how it fires, its projectile's speed), made once a map: the
functions above read only its tag, its name and its trigger, which a map
never changes, and each answers the same for it every time (a bot asked them
of its weapon every tick: a dozen name searches and a few tag lookups) */
#define BOTS_WEAPON_KINDS 32
struct bots_weapon_kind
{
	long definition_index;
	short value;
	short tap_ticks;
	real range;
	real projectile_speed;
	/* port: the ticks its trigger charges (a turret's gun that fires only
	charged, Extinction's gun tower's), 0 for none */
	short charge_ticks;
	/* port: what kind of a precision weapon (bots_tactics.h) */
	short precision;
	/* port: a power weapon (a rocket launcher, a sniper rifle, a shotgun, a
	fuel rod gun: bots_weapon_power) */
	boolean power;
};

static struct
{
	unsigned long map_generation;
	long count;
	struct bots_weapon_kind kinds[BOTS_WEAPON_KINDS];
	/* (the one made when the table is full) */
	struct bots_weapon_kind spare;
} bots_weapon_kinds;

static void bots_weapon_kinds_flush(void)
{
	bots_weapon_kinds.count = 0;
	bots_weapon_kinds.map_generation = halo_map_generation;
}

static struct bots_weapon_kind const *bots_weapon_kind(long definition_index)
{
	struct bots_weapon_kind *kind;
	long index;

	if (bots_weapon_kinds.map_generation != halo_map_generation)
		bots_weapon_kinds_flush();
	for (index = 0; index < bots_weapon_kinds.count; index++)
	{
		if (bots_weapon_kinds.kinds[index].definition_index == definition_index)
			return &bots_weapon_kinds.kinds[index];
	}
	kind = bots_weapon_kinds.count < BOTS_WEAPON_KINDS ? &bots_weapon_kinds.kinds[bots_weapon_kinds.count++] :
		&bots_weapon_kinds.spare;
	kind->definition_index = definition_index;
	kind->value = bots_weapon_value_compute(definition_index);
	kind->tap_ticks = bots_weapon_tap_ticks_compute(definition_index);
	kind->range = bots_weapon_range_compute(definition_index);
	kind->projectile_speed = bots_projectile_speed_compute(definition_index);
	kind->precision = bots_weapon_precision_compute(definition_index);
	if (kind->precision != _bots_precision_none && kind != &bots_weapon_kinds.spare)
		platform_log("bots: weapon %s: %s", tag_get_name(definition_index), kind->precision == _bots_precision_long ?
			"precision, long (a vantage point)" : kind->precision == _bots_precision_mid ? "precision, middle range" :
			"heavy (a vantage point)");
	kind->charge_ticks = 0;
	{
		char const *name = tag_get_name(definition_index);

		kind->power = name && (strstr(name, "rocket") || strstr(name, "sniper") || strstr(name, "shotgun") ||
			strstr(name, "fuel rod") || strstr(name, "plasma_cannon"));
	}
	{
		struct weapon_definition *definition = weapon_definition_get(definition_index);

		if (definition->weapon.triggers.count > 0)
		{
			real charging = TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition)->charging_time;

			kind->charge_ticks = (short)PIN((long)(charging * TICKS_PER_SECOND + 0.5f), 0, TICKS_PER_SECOND * 5);
		}
	}
	return kind;
}

static short bots_weapon_value(long definition_index)
{
	return bots_weapon_kind(definition_index)->value;
}

static real bots_projectile_speed(long weapon_index)
{
	return bots_weapon_kind(weapon_get(weapon_index)->definition_index)->projectile_speed;
}

static short bots_weapon_tap_ticks(long weapon_index)
{
	return bots_weapon_kind(weapon_get(weapon_index)->definition_index)->tap_ticks;
}

static real bots_weapon_range(long weapon_index)
{
	return weapon_index != NONE ? bots_weapon_kind(weapon_get(weapon_index)->definition_index)->range : 6.0f;
}

/* port: whether a weapon definition is a power weapon (one players go for:
the rocket launcher, the sniper rifle, the shotgun, the fuel rod gun) */
static boolean bots_weapon_power(long definition_index)
{
	return bots_weapon_kind(definition_index)->power;
}

/* port: (bots_tactics.h) */
short bots_weapon_precision(long weapon_index)
{
	return weapon_index != NONE ? bots_weapon_kind(weapon_get(weapon_index)->definition_index)->precision :
		_bots_precision_none;
}

/* port: whether the bot found no way to the item a while ago (left be
thirty seconds), and the item so remembered (the oldest of those kept
forgotten) */
static boolean bots_item_unreachable(struct bots_bot const *bot, long item_index)
{
	long index;

	for (index = 0; index < BOTS_UNREACHABLE_ITEMS; index++)
	{
		if (bot->unreachable_item_indices[index] == item_index && game_time_get() < bot->unreachable_item_until_ticks[index])
			return TRUE;
	}
	return FALSE;
}

static void bots_item_set_unreachable(struct bots_bot *bot, long item_index)
{
	long slot;

	/* (one remembered already: kept longer) */
	for (slot = 0; slot < BOTS_UNREACHABLE_ITEMS; slot++)
	{
		if (bot->unreachable_item_indices[slot] == item_index && game_time_get() < bot->unreachable_item_until_ticks[slot])
		{
			bot->unreachable_item_until_ticks[slot] = game_time_get() + TICKS_PER_SECOND * 30;
			return;
		}
	}
	slot = bot->unreachable_item_next;

	bot->unreachable_item_indices[slot] = item_index;
	bot->unreachable_item_until_ticks[slot] = game_time_get() + TICKS_PER_SECOND * 30;
	bot->unreachable_item_next = (slot + 1) % BOTS_UNREACHABLE_ITEMS;	if (getenv("HALO_BOT_TRACE"))
	{
		struct object_datum *item = object_try_and_get(item_index);

		platform_log("bots: trace bot %ld leaves %s be", (long)(bot - bots_globals.bots) + 1,
			item ? tag_get_name(item->definition_index) : "?");
	}
}

/* an item on the ground worth fetching near the bot: a weapon better than
the one it holds; NONE for none (port: the powerups are bots_powerup_goal's) */
static long bots_find_item(struct bots_bot *bot, struct unit_datum *unit, real_point3d const *position,
	real maximum_distance)
{
	struct object_iterator iterator;
	long best_index = NONE;
	real best_score = REAL_MAX;
	long current = bots_current_weapon(unit);
	short current_value = current != NONE ? bots_weapon_value(weapon_get(current)->definition_index) : 0;

	object_iterator_new(&iterator, _object_mask_weapon, 0);
	while (object_iterator_next(&iterator))
	{
		struct item_datum *item = item_get(iterator.index);
		real distance, score;
		short value;
		short slot;
		boolean held = FALSE;

		if (item->object.parent_object_index != NONE ||
			bots_item_unreachable(bot, iterator.index) ||
			!TEST_FLAG(item->object.flags, _object_connected_to_map_bit) ||
			TEST_FLAG(item->item.flags, _item_attached_to_unit_bit))
		{
			continue;
		}
		distance = bots_distance3d(&item->object.position, position);
		if (distance > maximum_distance)
			continue;
		value = bots_weapon_value(item->definition_index);
		/* (one it holds already: only its ammunition, which walking over
		takes) */
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long other = unit->unit.weapon_object_indices[slot];

			if (other != NONE && weapon_get(other)->definition_index == item->definition_index)
				held = TRUE;
		}
		if (held || value <= current_value)
			continue;
		score = distance - (real)(value - current_value) * 3.0f;
		if (score < best_score)
		{
			best_score = score;
			best_index = iterator.index;
		}
	}
	return best_index;
}

/* port: the bots' kind of an equipment item's powerup, _bots_powerup_none
for one they do not go for (a grenade, double speed) */
static short bots_powerup_kind(long definition_index)
{
	switch (equipment_definition_get(definition_index)->equipment.powerup_type)
	{
	case _equipment_powerup_health: return _bots_powerup_health;
	case _equipment_powerup_overshield: return _bots_powerup_overshield;
	case _equipment_powerup_active_camouflage: return _bots_powerup_camouflage;
	}
	return _bots_powerup_none;
}

/* port: the powerups lying on the map found again, and the turrets, once a
second (at a bots' tick's start, from the game state alone: the same in two
runs) */
static void bots_powerups_find(void)
{
	struct object_iterator iterator;
	long now = game_time_get();

	if (bots_powerups.map_generation == halo_map_generation && bots_powerups.found_tick != NONE &&
		now >= bots_powerups.found_tick && now - bots_powerups.found_tick < TICKS_PER_SECOND)
	{
		return;
	}
	bots_powerups.map_generation = halo_map_generation;
	bots_powerups.found_tick = now;
	bots_powerups.count = 0;
	object_iterator_new(&iterator, _object_mask_equipment, 0);
	while (object_iterator_next(&iterator) && bots_powerups.count < BOTS_MAXIMUM_POWERUPS)
	{
		struct item_datum *item = item_get(iterator.index);
		short kind;

		if (item->object.parent_object_index != NONE || !TEST_FLAG(item->object.flags, _object_connected_to_map_bit) ||
			TEST_FLAG(item->item.flags, _item_attached_to_unit_bit))
		{
			continue;
		}
		kind = bots_powerup_kind(item->definition_index);
		if (kind == _bots_powerup_none)
			continue;
		bots_powerups.items[bots_powerups.count].object_index = iterator.index;
		bots_powerups.items[bots_powerups.count].kind = kind;
		bots_powerups.items[bots_powerups.count].position = item->object.position;
		bots_powerups.items[bots_powerups.count].surface_index = bots_surface_below(&item->object.position, NULL);
		bots_powerups.count++;
	}
	/* (and the turrets: none on a stock map, whose bots never look again) */
	{
		long previous_count = bots_globals.turret_count;

		bots_globals.turret_count = 0;
		object_iterator_new(&iterator, _object_mask_vehicle, 0);
		while (object_iterator_next(&iterator) && bots_globals.turret_count < BOTS_MAXIMUM_TURRETS)
		{
			if (bots_turret_seat(iterator.index) == NONE)
				continue;
			/* (halo.log: each turret, as the count changes) */
			if (bots_globals.turret_count >= previous_count)
			{
				struct object_datum *turret = object_get(iterator.index);

				platform_log("bots: a turret, %s at (%.1f %.1f %.1f), seat %d", tag_get_name(turret->definition_index),
					turret->object.position.x, turret->object.position.y, turret->object.position.z,
					(int)bots_turret_seat(iterator.index));
			}
			bots_globals.turret_indices[bots_globals.turret_count++] = iterator.index;
		}
	}
}

/* ---------- the map */

static void bots_add_roam_point(real_point3d const *point)
{
	real_point3d ground;
	long surface_index;

	if (bots_globals.roam_point_count >= BOTS_MAXIMUM_ROAM_POINTS)
		return;
	surface_index = bots_surface_below(point, &ground);
	if (surface_index == NONE)
		return;
	bots_globals.roam_points[bots_globals.roam_point_count] = ground;
	bots_globals.roam_surfaces[bots_globals.roam_point_count] = surface_index;
	bots_globals.roam_point_count++;
}

static void bots_search_drop(struct bots_search *search);

/* the bots' walkable surfaces let go (no search reading them: those asked
are dropped, a running one waited for) */
static void bots_paths_release(void)
{
	long index;

	for (index = 0; bots_globals.search_slots && index < BOTS_SEARCH_SLOTS; index++)
		bots_search_drop(&bots_globals.search_slots[index]);
	for (index = 0; index < BOTS_MAXIMUM; index++)
		bots_globals.bots[index].search_slot = NONE;
	pthread_mutex_lock(&bots_helper.lock);
	bots_paths.active = FALSE;
	bots_paths.source = NULL;
	pthread_mutex_unlock(&bots_helper.lock);
	bots_paths.learned_count = 0;
	bots_paths.closed_count = 0;
	bots_paths.scan_index = 0;
	bots_paths.scan_done = FALSE;
	bots_paths.roofed_count = 0;
	bots_paths.scan_us = 0;
	bots_paths.scan_ticks = 0;
	if (bots_paths.walkable)
		free(bots_paths.walkable);
	bots_paths.walkable = NULL;
	path_pathfinding_surfaces_generation++;
	if (bots_paths.vehicle_walkable)
		free(bots_paths.vehicle_walkable);
	bots_paths.vehicle_walkable = NULL;
	bots_paths.vehicle_closed_count = 0;
	bots_paths.ramps_opened = FALSE;
	bots_paths.ramps_open_count = 0;
}

/* port: the ramps a biped walks up (bots_nav.c), opened to the bots'
searches (not the vehicles'), as the regions join them: the regions said a
goal up one was a search's to walk to, which never left the AI's walkable
surfaces (Chiron TL34's rooms, joined by their ramps and sills, were a
hundred regions). The surfaces with no room above them looked at again
from the start, the ramps with them */
static boolean bots_paths_copy(void);

static void bots_paths_open_ramps(void)
{
	long surface_index, count;

	if (bots_paths.ramps_opened || !bots_nav_ready() || !bots_paths_copy())
		return;
	bots_paths.ramps_opened = TRUE;
	count = bots_paths.structure.pathfinding_surfaces.count;
	for (surface_index = 0; surface_index < count; surface_index++)
	{
		if (bots_nav_ramp(surface_index) && bots_nav_region(surface_index) != NONE &&
			!TEST_FLAG(bots_paths.walkable[surface_index], BOTS_PATHFINDING_WALKABLE_BIT))
		{
			bots_paths.walkable[surface_index] |= (byte)FLAG(BOTS_PATHFINDING_WALKABLE_BIT);
			bots_paths.ramps_open_count++;
		}
	}
	if (bots_paths.ramps_open_count)
	{
		path_pathfinding_surfaces_generation++;
		bots_paths.scan_index = 0;
		bots_paths.scan_done = FALSE;
	}
	platform_log("bots: %ld ramps opened to the searches", bots_paths.ramps_open_count);
}

/* the surface's corners (at most BOTS_SURFACE_CORNERS), from its ring of
edges; 0 for one not the bsp's */
#define BOTS_SURFACE_CORNERS 16
static long bots_surface_corners(struct collision_bsp const *bsp, long surface_index, real_point3d *corners)
{
	struct collision_surface const *surface;
	long edge_index, count = 0;

	if (surface_index < 0 || surface_index >= bsp->surfaces.count)
		return 0;
	surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index, struct collision_surface);
	edge_index = surface->first_edge_index;
	do
	{
		struct collision_edge const *edge;
		boolean right;

		if (edge_index < 0 || edge_index >= bsp->edges.count)
			return 0;
		edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
		right = edge->surface_indices[1] == surface_index;
		if (!VALID_INDEX(edge->vertex_indices[right], bsp->vertices.count))
			return 0;
		corners[count++] = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, edge->vertex_indices[right], struct collision_vertex)->point;
		edge_index = edge->edge_indices[right];
	} while (edge_index != surface->first_edge_index && count < BOTS_SURFACE_CORNERS);
	return count;
}

/* whether the object stands across the surface: a line over it at a biped's
knees or chest, from its middle to a little past an edge's, meets it, or
its middle is under it */
static boolean bots_surface_blocked(long object_index, real_point3d const *corners, long corner_count,
	real_point3d const *middle)
{
	static real const heights[] = { 0.35f, 1.0f };
	unsigned long flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit);
	struct collision_result collision;
	real_point3d start = *middle;
	real_vector3d up = { 0.0f, 0.0f, 1.6f };
	long height, corner;

	start.z += 0.05f;
	if (collision_test_vector(flags, &start, &up, NONE, &collision) && collision.type == _collision_result_object &&
		collision.object_index == object_index)
	{
		return TRUE;
	}
	for (height = 0; height < (long)NUMBEROF(heights); height++)
	{
		start.z = middle->z + heights[height];
		for (corner = 0; corner < corner_count; corner++)
		{
			real_point3d const *a = &corners[corner], *b = &corners[(corner + 1) % corner_count];
			real_vector3d vector;
			real length, past;

			vector.i = 0.5f * (a->x + b->x) - start.x;
			vector.j = 0.5f * (a->y + b->y) - start.y;
			vector.k = 0.5f * (a->z + b->z) + heights[height] - start.z;
			/* (on a little past the edge: a door may stand on it) */
			length = (real)sqrt(vector.i * vector.i + vector.j * vector.j + vector.k * vector.k);
			if (length < 0.01f)
				continue;
			past = (length + 0.4f) / length;
			vector.i *= past;
			vector.j *= past;
			vector.k *= past;
			if (collision_test_vector(flags, &start, &vector, NONE, &collision) &&
				collision.type == _collision_result_object && collision.object_index == object_index)
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* the bots' copy of the structure's walkable surfaces, made (the searches
on the helper thread take it from then on); FALSE without one */
static boolean bots_paths_copy(void)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	long count;

	if (!structure || structure->pathfinding_surfaces.count <= 0 || structure->collision_bsp.count <= 0 ||
		(bots_paths.source && bots_paths.source != structure))
	{
		return FALSE;
	}
	if (bots_paths.walkable)
		return TRUE;
	count = structure->pathfinding_surfaces.count;
	bots_paths.walkable = (byte *)malloc((size_t)count);
	bots_paths.vehicle_walkable = (byte *)malloc((size_t)count);
	if (!bots_paths.walkable || !bots_paths.vehicle_walkable)
	{
		free(bots_paths.walkable);
		free(bots_paths.vehicle_walkable);
		bots_paths.walkable = bots_paths.vehicle_walkable = NULL;
		return FALSE;
	}
	csmemcpy(bots_paths.walkable, structure->pathfinding_surfaces.address, (size_t)count);
	csmemcpy(bots_paths.vehicle_walkable, structure->pathfinding_surfaces.address, (size_t)count);
	bots_paths.structure = *structure;
	bots_paths.structure.pathfinding_surfaces.address = bots_paths.walkable;
	/* (path.c: the helper's cache of surfaces' edges made again) */
	path_pathfinding_surfaces_generation++;
	bots_paths.vehicle_structure = *structure;
	bots_paths.vehicle_structure.pathfinding_surfaces.address = bots_paths.vehicle_walkable;
	pthread_mutex_lock(&bots_helper.lock);
	bots_paths.source = structure;
	bots_paths.active = TRUE;
	pthread_mutex_unlock(&bots_helper.lock);
	return TRUE;
}

/* a walkable surface of the bots' copy: its corners, its middle and how far
its corners are from it; FALSE for one not walkable (or not the bsp's) */
static boolean bots_paths_walkable_surface(struct collision_bsp const *bsp, long surface_index,
	real_point3d *corners, long *corner_count, real_point3d *middle, real *reach)
{
	long corner;

	/* (a byte the helper may be reading: walkable or not, either is an
	answer) */
	if (surface_index >= bots_paths.structure.pathfinding_surfaces.count ||
		!TEST_FLAG(bots_paths.walkable[surface_index], BOTS_PATHFINDING_WALKABLE_BIT))
	{
		return FALSE;
	}
	*corner_count = bots_surface_corners(bsp, surface_index, corners);
	if (*corner_count < 3)
		return FALSE;
	middle->x = middle->y = middle->z = 0.0f;
	for (corner = 0; corner < *corner_count; corner++)
	{
		middle->x += corners[corner].x / (real)*corner_count;
		middle->y += corners[corner].y / (real)*corner_count;
		middle->z += corners[corner].z / (real)*corner_count;
	}
	*reach = 0.0f;
	for (corner = 0; corner < *corner_count; corner++)
		*reach = MAX(*reach, bots_distance3d(middle, &corners[corner]));
	return TRUE;
}

/* the walkable surfaces an object stands across closed to the bots' searches */
static void bots_paths_close_object(long object_index, char const *why)
{
	struct object_datum *object = object_get(object_index);
	struct collision_bsp const *bsp;
	unsigned long long started = vita_host_time_us();
	long surface_index, closed = 0;

	if (bots_paths.learned_count >= (long)NUMBEROF(bots_paths.learned) || !bots_paths_copy())
		return;
	bots_paths.learned[bots_paths.learned_count++] = object_index;
	bsp = TAG_BLOCK_GET_ELEMENT(&bots_paths.structure.collision_bsp, 0, struct collision_bsp);
	for (surface_index = 0; surface_index < bsp->surfaces.count; surface_index++)
	{
		real_point3d corners[BOTS_SURFACE_CORNERS], middle;
		real reach;
		long corner_count;

		if (bots_paths_walkable_surface(bsp, surface_index, corners, &corner_count, &middle, &reach) &&
			bots_distance3d(&middle, &object->object.bounding_sphere_center) <
				object->object.bounding_sphere_radius + reach + 0.5f &&
			bots_surface_blocked(object_index, corners, corner_count, &middle))
		{
			bots_paths.walkable[surface_index] &= (byte)~FLAG(BOTS_PATHFINDING_WALKABLE_BIT);
			bots_paths.vehicle_walkable[surface_index] &= (byte)~FLAG(BOTS_PATHFINDING_WALKABLE_BIT);
			closed++;
		}
	}
	bots_paths.closed_count += closed;
	if (closed)
		path_pathfinding_surfaces_generation++;
	platform_log("bots: %s %s at (%.1f %.1f %.1f): %ld walkable surfaces closed (%.1f ms)", why,
		tag_get_name(object->definition_index), object->object.bounding_sphere_center.x,
		object->object.bounding_sphere_center.y, object->object.bounding_sphere_center.z, closed,
		(double)(vita_host_time_us() - started) / 1000.0);
}

/* port: a vehicle stuck with the level before it (a bank a biped walks up,
the foot of a ledge, which the walkable surfaces join): the walkable surfaces
just before it closed to the vehicles' searches alone, as far about as the
vehicle is wide (1.5 units at least: a Scorpion's 2.2, which drove at a
base's back door strip by strip), those under it and its goal's left open */
static void bots_paths_close_for_vehicles(real_point3d const *point, real_point3d const *vehicle_point,
	real_point3d const *goal_point, real vehicle_radius)
{
	struct collision_bsp const *bsp;
	long surface_index, closed = 0;
	real about = MAX(1.5f, vehicle_radius);

	if (!bots_paths_copy())
		return;
	bsp = TAG_BLOCK_GET_ELEMENT(&bots_paths.vehicle_structure.collision_bsp, 0, struct collision_bsp);
	for (surface_index = 0; surface_index < bsp->surfaces.count; surface_index++)
	{
		real_point3d corners[BOTS_SURFACE_CORNERS], middle;
		real reach;
		long corner_count;
		struct collision_surface const *surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index, struct collision_surface);
		struct collision_edge const *edge;

		if (surface_index >= bots_paths.vehicle_structure.pathfinding_surfaces.count ||
			!TEST_FLAG(bots_paths.vehicle_walkable[surface_index], BOTS_PATHFINDING_WALKABLE_BIT) ||
			!VALID_INDEX(surface->first_edge_index, bsp->edges.count))
		{
			continue;
		}
		/* (a corner far off: the surface is, the corners of the near ones
		alone worked out) */
		edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, surface->first_edge_index, struct collision_edge);
		if (!VALID_INDEX(edge->vertex_indices[0], bsp->vertices.count) ||
			bots_distance2d(&TAG_BLOCK_GET_ELEMENT(&bsp->vertices, edge->vertex_indices[0], struct collision_vertex)->point, point) >
				8.0f ||
			!bots_paths_walkable_surface(bsp, surface_index, corners, &corner_count, &middle, &reach))
		{
			continue;
		}
		if (bots_distance2d(&middle, point) < about && fabs(middle.z - point->z) < 2.5f &&
			bots_distance2d(&middle, vehicle_point) > 1.2f && bots_distance2d(&middle, goal_point) > 3.0f)
		{
			bots_paths.vehicle_walkable[surface_index] &= (byte)~FLAG(BOTS_PATHFINDING_WALKABLE_BIT);
			closed++;
		}
	}
	bots_paths.vehicle_closed_count += closed;
	platform_log("bots: a vehicle stuck at (%.1f %.1f %.1f): %ld walkable surfaces before it closed to vehicles (%ld in all)",
		vehicle_point->x, vehicle_point->y, vehicle_point->z, closed, bots_paths.vehicle_closed_count);
}

/* the walkable surfaces with no room above them closed, a few a tick from
the game's start: a floor that goes on under a wall (Longest's bases: the
floor's strips under the walls joined the rooms either side, and a team's
bots walked into the wall a whole game). A surface is closed whose middle,
or most of the points halfway from it to its corners, have the level less
than half a biped's height above them */
#define BOTS_PATHS_SCAN_A_TICK 32
static void bots_paths_scan(void)
{
	struct collision_bsp const *bsp;
	long scanned;
	unsigned long long started;

	if (bots_paths.scan_done || !bots_paths_copy())
		return;
	started = vita_host_time_us();
	bots_paths.scan_ticks++;
	bsp = TAG_BLOCK_GET_ELEMENT(&bots_paths.structure.collision_bsp, 0, struct collision_bsp);
	for (scanned = 0; scanned < BOTS_PATHS_SCAN_A_TICK; scanned++)
	{
		long surface_index = bots_paths.scan_index++;
		real_point3d corners[BOTS_SURFACE_CORNERS], middle;
		real reach;
		long corner_count, sample, covered = 0;

		if (surface_index >= bsp->surfaces.count || surface_index >= bots_paths.structure.pathfinding_surfaces.count)
		{
			bots_paths.scan_done = TRUE;
			bots_paths.scan_us += vita_host_time_us() - started;
			platform_log("bots: %ld walkable surfaces with no room above closed (%ld ticks, %.1f ms, %.0f us a tick at most "
				"%d surfaces)", bots_paths.roofed_count, bots_paths.scan_ticks, (double)bots_paths.scan_us / 1000.0,
				(double)bots_paths.scan_us / (double)bots_paths.scan_ticks, BOTS_PATHS_SCAN_A_TICK);
			return;
		}
		if (!bots_paths_walkable_surface(bsp, surface_index, corners, &corner_count, &middle, &reach))
			continue;
		/* (the middle first: covered, the surface is; else most of the
		points between it and the corners) */
		for (sample = -1; sample < corner_count; sample++)
		{
			unsigned long flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
				FLAG(_collision_test_back_facing_surfaces_bit);
			real_vector3d up = { 0.0f, 0.0f, 0.35f };
			struct collision_result collision;
			real_point3d point = middle;

			if (sample >= 0)
			{
				point.x = 0.5f * (middle.x + corners[sample].x);
				point.y = 0.5f * (middle.y + corners[sample].y);
				point.z = 0.5f * (middle.z + corners[sample].z);
			}
			point.z += 0.05f;
			if (collision_test_vector(flags, &point, &up, NONE, &collision))
			{
				if (sample < 0)
				{
					covered = corner_count;
					break;
				}
				covered++;
			}
		}
		if (covered * 2 > corner_count)
		{
			bots_paths.walkable[surface_index] &= (byte)~FLAG(BOTS_PATHFINDING_WALKABLE_BIT);
			bots_paths.vehicle_walkable[surface_index] &= (byte)~FLAG(BOTS_PATHFINDING_WALKABLE_BIT);
			bots_paths.roofed_count++;
			path_pathfinding_surfaces_generation++;
		}
	}
	bots_paths.scan_us += vita_host_time_us() - started;
}

static boolean bots_paths_learned(long object_index)
{
	long index;

	for (index = 0; index < bots_paths.learned_count; index++)
	{
		if (bots_paths.learned[index] == object_index)
			return TRUE;
	}
	return FALSE;
}

/* a path's obstacle avoidance found no way past where the bot is: the
scenery and machines about it (not the bipeds and vehicles, which move) are
in the way, their surfaces closed to the searches from now on (Sidewinder's
blast door between the bases is scenery: the search, over the structure
alone, goes through it, the shortest way, and the avoidance then finds no
way past; more than half of the searches across that map failed so). A
rock or a tree the avoidance goes around is never in the way */
/* whether the point is on or in the object (by its bounding sphere) */
static boolean bots_object_holds(struct object_datum *object, real_point3d const *point)
{
	return bots_distance2d(&object->object.bounding_sphere_center, point) < object->object.bounding_sphere_radius + 0.4f &&
		fabs(object->object.bounding_sphere_center.z - point->z) < object->object.bounding_sphere_radius + 1.0f;
}

/* the scenery or machine (with a collision model) the point is on or in,
the nearest; NONE for none */
static long bots_object_at(real_point3d const *point)
{
	struct object_iterator iterator;
	long best_index = NONE;
	real best_distance = REAL_MAX;

	object_iterator_new(&iterator, _object_mask_scenery | _object_mask_machine, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);
		real distance;

		if (object_definition_get(object->definition_index)->object.collision_model.index == NONE ||
			!bots_object_holds(object, point))
		{
			continue;
		}
		distance = bots_distance3d(&object->object.bounding_sphere_center, point);
		if (distance < best_distance)
		{
			best_distance = distance;
			best_index = iterator.index;
		}
	}
	return best_index;
}

/* whether one of the map's game places (a flag's stand, a teleporter, a
hill's corner, a ball's spawn) is on or in the object: never in the way */
static boolean bots_object_marks_place(struct object_datum *object)
{
	struct scenario *scenario = global_scenario_get();
	long index;

	for (index = 0; scenario && index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);

		if (bots_object_holds(object, &flag->position))
			return TRUE;
	}
	return FALSE;
}

/* port: whether the point stands on the object (the level or another object
under it first) */
static boolean bots_object_supports(long object_index, real_point3d const *point)
{
	struct collision_result collision;
	real_point3d start = *point;
	real_vector3d down = { 0.0f, 0.0f, -1.2f };

	start.z += 0.4f;
	return collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit), &start, &down, NONE, &collision) &&
		collision.type == _collision_result_object && collision.object_index == object_index;
}

static long bots_paths_learn(real_point3d const *position, real_point3d const *goal)
{
	struct object_iterator iterator;
	long learned = 0;

	object_iterator_new(&iterator, _object_mask_scenery | _object_mask_machine, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);
		struct object_definition *definition = object_definition_get(object->definition_index);

		/* (not what the bot or its goal stands on or in: a flag's stand, a
		teleporter's base; port: what the bot stands beside, within its
		bounding sphere but on the level, is learnt - a bot pressed against
		Sidewinder's blast door before it was learnt never learnt it, and the
		flag carriers and their escorts stood there all game) */
		if (definition->object.collision_model.index == NONE ||
			bots_distance2d(&object->object.bounding_sphere_center, position) > object->object.bounding_sphere_radius + 4.5f ||
			fabs(object->object.bounding_sphere_center.z - position->z) > object->object.bounding_sphere_radius + 2.0f ||
			(bots_object_holds(object, position) && bots_object_supports(iterator.index, position)) ||
			bots_object_holds(object, goal) ||
			bots_paths_learned(iterator.index) || bots_object_marks_place(object))
		{
			continue;
		}
		bots_paths_close_object(iterator.index, "in the way:");
		learned++;
	}
	return learned;
}

/* the way off a closed surface: away from the nearest of what closed it */
static boolean bots_paths_escape_direction(real_point3d const *position, real *direction)
{
	long index, nearest = NONE;
	real nearest_distance = REAL_MAX;

	for (index = 0; index < bots_paths.learned_count; index++)
	{
		struct object_datum *object = object_try_and_get(bots_paths.learned[index]);
		real distance;

		if (!object)
			continue;
		distance = bots_distance2d(&object->object.bounding_sphere_center, position);
		if (distance < nearest_distance && distance < object->object.bounding_sphere_radius + 4.0f)
		{
			nearest_distance = distance;
			nearest = index;
		}
	}
	if (nearest == NONE)
		return FALSE;
	{
		struct object_datum *object = object_get(bots_paths.learned[nearest]);

		*direction = (real)atan2(position->y - object->object.bounding_sphere_center.y,
			position->x - object->object.bounding_sphere_center.x);
	}
	return TRUE;
}

/* the bots' searches' walkable surfaces, the structure's to begin with,
less those of a machine its definition calls a pathfinding obstacle (not an
elevator, nor a door open now that is none open) */
static void bots_prepare_paths(void)
{
	struct object_iterator iterator;

	bots_paths_release();
	object_iterator_new(&iterator, _object_mask_machine, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);
		struct machine_definition *definition = machine_definition_get(object->definition_index);

		if (!TEST_FLAG(definition->machine.flags, _machine_is_pathfinding_obstacle_bit) ||
			TEST_FLAG(definition->machine.flags, _machine_is_elevator_bit) ||
			(device_get_position(iterator.index) > 0.5f &&
				TEST_FLAG(definition->machine.flags, _machine_is_not_pathfinding_obstacle_when_open_bit)))
		{
			continue;
		}
		bots_paths_close_object(iterator.index, "a machine in the way:");
	}
}

/* port: the level's ladders, among the teleporters (bots_search_teleporter
goes up one as through a teleporter): its climbable surfaces (collision_bsp's
climbable flag) that stand upright, those one over another (a ladder's
pieces) and the two faces of one taken together; its foot the ground at its
bottom on the side a biped stands on (the level's surface there a walkable
one, as near the bottom as a step), its top's landing the ground beyond its
top (or before it) up at its top's height. One whose foot or landing is not
found is none. Made once a map */
#define BOTS_MAXIMUM_LADDER_PIECES 128
static void bots_prepare_ladders(struct collision_bsp const *bsp)
{
	struct
	{
		real_point3d middle;
		real_vector3d normal;
		real low, high;
	} pieces[BOTS_MAXIMUM_LADDER_PIECES];
	long piece_count = 0, surface_index, index;

	for (surface_index = 0; surface_index < bsp->surfaces.count; surface_index++)
	{
		real_point3d corners[BOTS_SURFACE_CORNERS], middle = { 0.0f, 0.0f, 0.0f };
		real_vector3d a, b, normal;
		real low = REAL_MAX, high = -REAL_MAX, length;
		long corner, count;

		if (!TEST_FLAG(TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index, struct collision_surface)->flags,
			BOTS_COLLISION_SURFACE_CLIMBABLE_BIT))
		{
			continue;
		}
		count = bots_surface_corners(bsp, surface_index, corners);
		if (count < 3)
			continue;
		for (corner = 0; corner < count; corner++)
		{
			middle.x += corners[corner].x / (real)count;
			middle.y += corners[corner].y / (real)count;
			middle.z += corners[corner].z / (real)count;
			low = MIN(low, corners[corner].z);
			high = MAX(high, corners[corner].z);
		}
		vector_from_points3d(&corners[0], &corners[1], &a);
		vector_from_points3d(&corners[0], &corners[2], &b);
		cross_product3d(&a, &b, &normal);
		normal.k = 0.0f;
		length = magnitude3d(&normal);
		/* (upright, and a metre high at least) */
		if (length < 0.0001f || high - low < 0.3f)
			continue;
		normal.i /= length;
		normal.j /= length;
		/* (a piece over or under one, or its other face: the one ladder) */
		for (index = 0; index < piece_count; index++)
		{
			if (bots_distance2d(&pieces[index].middle, &middle) < 0.5f &&
				fabs(pieces[index].normal.i * normal.i + pieces[index].normal.j * normal.j) > 0.9f &&
				low <= pieces[index].high + 0.1f && high >= pieces[index].low - 0.1f)
			{
				pieces[index].low = MIN(pieces[index].low, low);
				pieces[index].high = MAX(pieces[index].high, high);
				break;
			}
		}
		if (index < piece_count || piece_count >= BOTS_MAXIMUM_LADDER_PIECES)
			continue;
		pieces[piece_count].middle = middle;
		pieces[piece_count].normal = normal;
		pieces[piece_count].low = low;
		pieces[piece_count].high = high;
		piece_count++;
	}
	bots_globals.ladder_count = 0;
	for (index = 0; index < piece_count && bots_globals.teleporter_count < BOTS_MAXIMUM_TELEPORTERS; index++)
	{
		long teleporter = bots_globals.teleporter_count;
		real_point3d foot = { 0.0f, 0.0f, 0.0f }, landing = { 0.0f, 0.0f, 0.0f }, ground;
		long foot_surface = NONE, landing_surface = NONE;
		real side, foot_side = 0.0f;
		boolean same_side = FALSE;

		for (side = -1.0f; side <= 1.0f; side += 2.0f)
		{
			real_point3d point;
			long surface;

			point.x = pieces[index].middle.x + pieces[index].normal.i * side * 0.4f;
			point.y = pieces[index].middle.y + pieces[index].normal.j * side * 0.4f;
			point.z = pieces[index].low + 0.3f;
			surface = bots_surface_below(&point, &ground);
			if (foot_surface == NONE && surface != NONE && fabs(ground.z - pieces[index].low) < 0.35f)
			{
				foot_surface = surface;
				foot = ground;
				foot_side = side;
			}
		}
		/* (the landing beyond the top first, as a ladder up a wall's face
		goes over it; else on the same side, a ladder up to a ledge; port: a
		little further on too, and a floor below the ladder's top - a ladder
		that goes on above the floor it comes up to, as a rail: Prisoner's
		four, which had no landing and so were no ladders) */
		for (side = -1.0f; side <= 1.0f && foot_surface != NONE; side += 2.0f)
		{
			real reach;

			for (reach = 0.6f; reach < 1.25f && landing_surface == NONE; reach += 0.4f)
			{
				real_point3d point;
				long surface;
				real way = side * foot_side;

				point.x = pieces[index].middle.x + pieces[index].normal.i * way * reach;
				point.y = pieces[index].middle.y + pieces[index].normal.j * way * reach;
				point.z = pieces[index].high + 0.5f;
				surface = bots_surface_below(&point, &ground);
				if (surface != NONE && ground.z < pieces[index].high + 0.5f && ground.z > pieces[index].high - 1.2f &&
					ground.z > pieces[index].low + 1.0f)
				{
					landing_surface = surface;
					landing = ground;
					same_side = way == foot_side;
				}
			}
		}
		if (getenv("HALO_BOT_TRACE"))
		{
			platform_log("bots: trace ladder at (%.1f %.1f) z %.1f to %.1f normal (%.2f %.2f): foot %s (%.1f %.1f %.1f), "
				"landing %s (%.1f %.1f %.1f)", pieces[index].middle.x, pieces[index].middle.y, pieces[index].low,
				pieces[index].high, pieces[index].normal.i, pieces[index].normal.j, foot_surface != NONE ? "yes" : "no",
				foot.x, foot.y, foot.z, landing_surface != NONE ? "yes" : "no", landing.x, landing.y, landing.z);
		}
		if (foot_surface == NONE || landing_surface == NONE)
			continue;
		/* (no shortcut: bots_shortcut_weigh takes teleporters alone) */
		csmemset(&bots_globals.teleporters[teleporter], 0, sizeof(bots_globals.teleporters[teleporter]));
		bots_globals.teleporters[teleporter].exit_surface_index = NONE;
		bots_globals.teleporters[teleporter].entrance = foot;
		bots_globals.teleporters[teleporter].entrance_surface_index = foot_surface;
		bots_globals.teleporters[teleporter].exit = landing;
		bots_globals.teleporters[teleporter].ladder = TRUE;
		/* (facing it from its foot) */
		bots_globals.teleporters[teleporter].climb_direction =
			(real)atan2(-pieces[index].normal.j * foot_side, -pieces[index].normal.i * foot_side);
		/* (port: a landing on the foot's side - a ladder up through a hole
		in the floor above, Prisoner's - is stepped back onto from above its
		floor; one beyond, over the top) */
		bots_globals.teleporters[teleporter].top_z = same_side ? MIN(pieces[index].high, landing.z + 0.3f) : pieces[index].high;
		bots_globals.teleporters[teleporter].climbs = 0;
		bots_globals.teleporters[teleporter].failures = 0;
		bots_globals.teleporter_count++;
		bots_globals.ladder_count++;
	}
}

/* the places a bot roams to: the player starting locations, the weapon and
item spawns, the hills (once the level's BSP is in) */
static void bots_prepare_map(void)
{
	struct scenario *scenario = global_scenario_get();
	long index;

	bots_globals.map_ready = TRUE;
	bots_globals.roam_point_count = 0;
	bots_prepare_paths();
	/* port: the map's weapons known at once (halo.log names its precision
	weapons: bots_weapon_precision), half the table at most (a vehicle's gun
	is a weapon too) */
	{
		struct tag_iterator tags;
		long tag_index;

		tag_iterator_new(&tags, WEAPON_DEFINITION_TAG);
		while (bots_weapon_kinds.count < BOTS_WEAPON_KINDS / 2 && (tag_index = tag_iterator_next(&tags)) != NONE)
			bots_weapon_kind(tag_index);
	}
	if (!scenario)
		return;
	for (index = 0; index < scenario->netgame_equipment.count; index++)
	{
		struct scenario_netgame_equipment *equipment =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, index, struct scenario_netgame_equipment);

		bots_add_roam_point(&equipment->position);
	}
	/* (the teleporters: a source flag's channel is its team index, and the
	target flag of that channel is where it comes out; port: the first of
	the channel's targets, in the flags' order, as the game sends a player
	to it alone - game_engine.c's game_engine_update_teleporter takes the
	first find_netgame_flags finds - so a channel with more exits than one
	sends every player to its first, and so the bots know it. A two-way
	teleporter is two sources, each with the other's channel; a one-way one
	a source alone, its target no entrance. Each logged, a shortcut's
	exit's ground with it) */
	bots_globals.teleporter_count = 0;
	bots_globals.teleporter_shortcuts = 0;
	for (index = 0; index < scenario->netgame_flags.count && bots_globals.teleporter_count < BOTS_MAXIMUM_TELEPORTERS; index++)
	{
		struct scenario_netgame_flag *source =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);
		long target_index, exits = 0;
		long teleporter = bots_globals.teleporter_count;
		boolean paired = FALSE;

		if (source->type != _netgame_flag_teleporter_source)
			continue;
		for (target_index = 0; target_index < scenario->netgame_flags.count; target_index++)
		{
			struct scenario_netgame_flag *target =
				TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, target_index, struct scenario_netgame_flag);

			if (target->type != _netgame_flag_teleporter_target || target->team_index != source->team_index)
				continue;
			exits++;
			if (paired)
				continue;
			paired = TRUE;
			csmemset(&bots_globals.teleporters[teleporter], 0, sizeof(bots_globals.teleporters[teleporter]));
			bots_globals.teleporters[teleporter].entrance_surface_index =
				bots_surface_below(&source->position, &bots_globals.teleporters[teleporter].entrance);
			bots_globals.teleporters[teleporter].exit = target->position;
			bots_globals.teleporters[teleporter].ladder = FALSE;
			bots_globals.teleporters[teleporter].channel = source->team_index;
			bots_globals.teleporters[teleporter].exit_surface_index =
				bots_surface_below(&target->position, &bots_globals.teleporters[teleporter].exit_ground);
		}
		if (!paired || bots_globals.teleporters[teleporter].entrance_surface_index == NONE)
		{
			platform_log("bots: teleporter at (%.1f %.1f %.1f) channel %d: %s", source->position.x, source->position.y,
				source->position.z, (int)source->team_index, paired ? "on no ground (left out)" : "no exit (left out)");
			continue;
		}
		bots_globals.teleporter_count++;
		if (bots_globals.teleporters[teleporter].exit_surface_index != NONE)
			bots_globals.teleporter_shortcuts++;
		platform_log("bots: teleporter %ld at (%.1f %.1f %.1f) to (%.1f %.1f %.1f) channel %d, %.0f units%s%s",
			teleporter + 1, source->position.x, source->position.y, source->position.z,
			bots_globals.teleporters[teleporter].exit.x, bots_globals.teleporters[teleporter].exit.y,
			bots_globals.teleporters[teleporter].exit.z, (int)source->team_index,
			(double)bots_distance3d(&source->position, &bots_globals.teleporters[teleporter].exit),
			exits > 1 ? " (the first of the channel's exits, as the game sends a player)" : "",
			bots_globals.teleporters[teleporter].exit_surface_index == NONE ? " (its exit on no ground: no shortcut)" : "");
	}
	for (index = 0; index < scenario->players.count; index += 2)
	{
		struct player_starting_location *location =
			TAG_BLOCK_GET_ELEMENT(&scenario->players, index, struct player_starting_location);

		bots_add_roam_point(&location->position);
	}
	/* (debug: HALO_BOT_TRACE, the map's starting locations and netgame
	flags) */
	if (getenv("HALO_BOT_TRACE"))
	{
		for (index = 0; index < scenario->players.count; index++)
		{
			struct player_starting_location *location =
				TAG_BLOCK_GET_ELEMENT(&scenario->players, index, struct player_starting_location);

			platform_log("bots: trace starting location %ld at (%.1f %.1f %.1f) team %d games %d %d %d %d", index,
				location->position.x, location->position.y, location->position.z, (int)location->team_index,
				(int)location->game_types[0], (int)location->game_types[1], (int)location->game_types[2],
				(int)location->game_types[3]);
		}
		for (index = 0; index < scenario->netgame_flags.count; index++)
		{
			struct scenario_netgame_flag *flag =
				TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);

			platform_log("bots: trace netgame flag %ld at (%.1f %.1f %.1f) type %d team %d", index,
				flag->position.x, flag->position.y, flag->position.z, (int)flag->type, (int)flag->team_index);
		}
	}
	{
		real_point3d low = { REAL_MAX, REAL_MAX, REAL_MAX }, high = { -REAL_MAX, -REAL_MAX, -REAL_MAX };

		for (index = 0; index < bots_globals.roam_point_count; index++)
		{
			low.x = MIN(low.x, bots_globals.roam_points[index].x);
			low.y = MIN(low.y, bots_globals.roam_points[index].y);
			high.x = MAX(high.x, bots_globals.roam_points[index].x);
			high.y = MAX(high.y, bots_globals.roam_points[index].y);
		}
		platform_log("bots: %ld places to roam on this map, over %.0f units; %ld teleporters", bots_globals.roam_point_count,
			bots_globals.roam_point_count > 0 ? (double)bots_distance2d(&low, &high) : 0.0, bots_globals.teleporter_count);
	}
	/* port: (the level's ladders: climbable surfaces - Boarding Action's
	sides, Prisoner's, Sidewinder's - which the walkable surfaces never go up;
	the bots climb them as they go through teleporters) */
	{
		struct structure_bsp *structure = global_structure_bsp_get();

		bots_globals.climbable_surfaces = 0;
		if (structure && structure->collision_bsp.count > 0)
		{
			struct collision_bsp const *bsp = TAG_BLOCK_GET_ELEMENT(&structure->collision_bsp, 0, struct collision_bsp);

			for (index = 0; index < bsp->surfaces.count; index++)
			{
				if (TEST_FLAG(TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, index, struct collision_surface)->flags,
					BOTS_COLLISION_SURFACE_CLIMBABLE_BIT))
				{
					bots_globals.climbable_surfaces++;
					/* (debug: HALO_BOT_TRACE, each one's middle, size and slope) */
					if (getenv("HALO_BOT_TRACE"))
					{
						real_point3d corners[BOTS_SURFACE_CORNERS], middle = { 0.0f, 0.0f, 0.0f };
						long corner, count = bots_surface_corners(bsp, index, corners);
						real low = REAL_MAX, high = -REAL_MAX;
						real_vector3d a, b, normal;

						for (corner = 0; corner < count; corner++)
						{
							middle.x += corners[corner].x / (real)count;
							middle.y += corners[corner].y / (real)count;
							middle.z += corners[corner].z / (real)count;
							low = MIN(low, corners[corner].z);
							high = MAX(high, corners[corner].z);
						}
						normal.i = normal.j = normal.k = 0.0f;
						if (count >= 3)
						{
							vector_from_points3d(&corners[0], &corners[1], &a);
							vector_from_points3d(&corners[0], &corners[2], &b);
							cross_product3d(&a, &b, &normal);
							normalize3d(&normal);
						}
						platform_log("bots: trace climbable surface %ld at (%.1f %.1f %.1f) z %.1f to %.1f normal (%.2f %.2f %.2f)",
							index, middle.x, middle.y, middle.z, low, high, normal.i, normal.j, normal.k);
					}
				}
			}
			bots_prepare_ladders(bsp);
			/* port: (the teleporters and the ladders, hops between the
			regions: bots_nav.c) */
			for (index = 0; index < bots_globals.teleporter_count; index++)
			{
				real_point3d exit_ground;
				long exit_surface_index = bots_globals.teleporters[index].ladder ?
					bots_surface_below(&bots_globals.teleporters[index].exit, &exit_ground) :
					bots_globals.teleporters[index].exit_surface_index;

				if (!bots_globals.teleporters[index].ladder)
					exit_ground = bots_globals.teleporters[index].exit_ground;
				bots_nav_add_teleporter(index, bots_globals.teleporters[index].ladder,
					&bots_globals.teleporters[index].entrance, bots_globals.teleporters[index].entrance_surface_index,
					&exit_ground, exit_surface_index);
			}
			platform_log("bots: %ld climbable surfaces of %ld, %ld ladders", bots_globals.climbable_surfaces,
				(long)bsp->surfaces.count, bots_globals.ladder_count);
		}
	}
	/* (CTF: the teams' stands, the scenario's netgame flags, for the tests:
	run_bots_test.sh checks that each team spawns at its own) */
	if (game_engine_get_variant()->game_engine_index == game_engine_ctf)
	{
		real_point3d stands[2] = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

		for (index = 0; index < scenario->netgame_flags.count; index++)
		{
			struct scenario_netgame_flag *flag =
				TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);

			if (flag->type == _netgame_flag_ctf_flag && (flag->team_index == 0 || flag->team_index == 1))
				stands[flag->team_index] = flag->position;
		}
		platform_log("bots: CTF%s, the stands: team 0 (%.1f %.1f %.1f) team 1 (%.1f %.1f %.1f)",
			game_engine_get_variant()->game_engine_variant.ctf.assault ? " (assault: each team's flag at the other's)" : "",
			stands[0].x, stands[0].y, stands[0].z, stands[1].x, stands[1].y, stands[1].z);
	}
}

void bots_initialize_for_new_map(void)
{
	long index;

	csmemset(bots_globals.bots, 0, sizeof(bots_globals.bots));
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		bots_globals.bots[index].player_index = NONE;
		bots_globals.bots[index].search_slot = NONE;
	}
	bots_globals.map_ready = FALSE;
	bots_globals.roam_point_count = 0;
	bots_globals.tick_us = 0;
	bots_globals.search_us = 0;
	bots_globals.ticks = 0;
	bots_globals.bot_ticks = 0;
	bots_globals.searches = 0;
	bots_globals.search_failures = 0;
	bots_globals.search_partials = 0;
	bots_globals.search_overflows = 0;
	bots_globals.search_unavoided = 0;
	bots_globals.search_unsmoothed = 0;
	bots_globals.flag_grabs = 0;
	bots_globals.ball_grabs = 0;
	bots_globals.stand_visits = 0;
	csmemset(bots_profile.part_us, 0, sizeof(bots_profile.part_us));
	bots_profile.below_rays = 0;
	bots_profile.sight_rays = 0;
	bots_profile.below_us = 0;
	bots_profile.sight_us = 0;
	/* (the first line a minute into the game) */
	bots_globals.last_report_tick = 0;
	bots_globals.vehicle_entries = 0;
	csmemset(bots_globals.vehicle_entries_by_seat, 0, sizeof(bots_globals.vehicle_entries_by_seat));
	csmemset(bots_globals.vehicle_exits, 0, sizeof(bots_globals.vehicle_exits));
	bots_globals.vehicle_flips = 0;
	bots_globals.driven_distance = 0.0f;
	bots_globals.riding_kills[0] = bots_globals.riding_kills[1] = 0;
	bots_globals.riding_fire_ticks = 0;
	bots_globals.driving_searches = 0;
	csmemset(bots_globals.vehicle_entries_by_kind, 0, sizeof(bots_globals.vehicle_entries_by_kind));
	bots_globals.first_entry_tick = 0;
	bots_globals.vehicle_take_overs = 0;
	bots_globals.vehicle_giveups = 0;
	bots_globals.riding_ticks = 0;
	bots_globals.last_census_tick = 0;
	bots_globals.vehicles_listed = FALSE;
	bots_globals.stuck_events = 0;
	bots_globals.stuck_low = 0;
	bots_globals.stuck_tall = 0;
	bots_globals.stuck_clear = 0;
	bots_globals.stuck_giveups = 0;
	bots_globals.climbable_surfaces = 0;
	bots_globals.ladder_count = 0;
	bots_globals.ladder_climbs = 0;
	bots_globals.ladder_tries = 0;
	bots_globals.teleporter_shortcuts = 0;
	bots_globals.shortcut_measures = 0;
	bots_globals.shortcut_takes = 0;
	csmemset(bots_globals.shortcut_through, 0, sizeof(bots_globals.shortcut_through));
	bots_globals.shortcut_failures = 0;
	bots_globals.shortcut_drops = 0;
	csmemset(bots_globals.powerups_sought, 0, sizeof(bots_globals.powerups_sought));
	csmemset(bots_globals.powerups_taken, 0, sizeof(bots_globals.powerups_taken));
	bots_globals.bodies_restored = 0;
	bots_globals.turret_entries = 0;
	bots_globals.turret_kills = 0;
	bots_globals.turret_fire_ticks = 0;
	bots_globals.turret_count = 0;
	csmemset(&bots_globals.play, 0, sizeof(bots_globals.play));
	bots_powerups.count = 0;
	bots_powerups.found_tick = NONE;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		bots_globals.bots[index].vehicle_index = NONE;
		bots_globals.bots[index].vehicle_avoid_index = NONE;
		bots_globals.bots[index].ride_vehicle_index = NONE;
	}
	bots_helper.helper_us = 0;
	/* port: (the routes, vantage points and roles: bots_tactics.h) */
	bots_tactics_initialize_for_new_map();
	/* port: (the regions and hops: bots_nav.c) */
	bots_nav_reset();
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		bots_globals.bots[index].hop_index = NONE;
		bots_globals.bots[index].hop_via = NONE;
	}
}

void bots_dispose_from_old_map(void)
{
	/* (a search still running finishes on the old level's data, which is
	still loaded; the answers are dropped) */
	bots_paths_release();
	bots_globals.map_ready = FALSE;
	bots_tactics_dispose_from_old_map();
	bots_nav_reset();
}

/* ---------- path searches

A search's state changes under the helper's lock (the game's structures are
compiled for any alignment, -fmax-type-align=1 on the Vita, where an atomic
operation on them would be a library call); the search itself runs outside
it. The tick holds the lock only to look at or change a state. */

static long bots_search_state(struct bots_search *search)
{
	long state;

	pthread_mutex_lock(&bots_helper.lock);
	state = search->state;
	pthread_mutex_unlock(&bots_helper.lock);
	return state;
}

static void bots_search_set_state(struct bots_search *search, long state)
{
	pthread_mutex_lock(&bots_helper.lock);
	search->state = state;
	pthread_mutex_unlock(&bots_helper.lock);
}

static void bots_search_run(struct bots_search *search)
{
	struct path_input input;
	unsigned long long started = vita_host_time_us();

	path_input_new(&input, search->radius, FALSE, search->unit_index);
	/* (a vehicle to get into: the path goes up to it, not round it) */
	if (search->target_object_index != NONE)
		path_input_set_target_object(&input, search->target_object_index);
	path_input_set_start(&input, &search->start_point, search->start_surface_index);
	path_state_new(&input, &search->state_memory, NULL);
	/* (the walkable surfaces less those closed by what is in the way) */
	pthread_mutex_lock(&bots_helper.lock);
	if (bots_paths.active && search->state_memory.structure == bots_paths.source)
	{
		search->state_memory.structure = search->radius > BOTS_PATH_RADIUS ? &bots_paths.vehicle_structure :
			&bots_paths.structure;
	}
	pthread_mutex_unlock(&bots_helper.lock);
	/* (a path to as near the goal as the search reaches: a long way across a
	large level is more surfaces than a search holds, and the bot searches
	again from there) */
	path_state_destination(&search->state_memory, &search->goal_point, search->goal_surface_index, 1000.0f);
	path_state_find(&search->state_memory);
	search->found = TRUE;
	pthread_mutex_lock(&bots_helper.lock);
	bots_helper.helper_us += vita_host_time_us() - started;
	pthread_mutex_unlock(&bots_helper.lock);
}

static void *bots_helper_main(void *argument)
{
	(void)argument;
#ifdef HALO_VITA
	vita_host_fourth_core_join("bot paths", 2);
#endif
	/* (path.c: this thread's searches are not the tick's) */
	path_search_thread_is_helper();
	for (;;)
	{
		long index;

		pthread_mutex_lock(&bots_helper.lock);
		while (!bots_helper.pending)
			pthread_cond_wait(&bots_helper.wake, &bots_helper.lock);
		bots_helper.pending = 0;
		pthread_mutex_unlock(&bots_helper.lock);
		for (index = 0; index < BOTS_SEARCH_SLOTS; index++)
		{
			struct bots_search *search = &bots_globals.search_slots[index];
			boolean mine = FALSE;

			pthread_mutex_lock(&bots_helper.lock);
			if (search->state == _bots_search_waiting)
			{
				search->state = _bots_search_running;
				mine = TRUE;
			}
			pthread_mutex_unlock(&bots_helper.lock);
			if (mine)
			{
				bots_search_run(search);
				bots_search_set_state(search, _bots_search_done);
			}
		}
	}
	return NULL;
}

static void bots_helper_start(void)
{
	char const *fixed = getenv("HALO_FIXED_TICK");
	char const *threaded = getenv("HALO_BOT_THREAD");

	bots_helper.started = TRUE;
	bots_globals.search_slots = (struct bots_search *)calloc(BOTS_SEARCH_SLOTS, sizeof(struct bots_search));
	/* (HALO_FIXED_TICK: the same bots in every run, the searches in the tick;
	HALO_BOT_THREAD=0 likewise, to compare) */
	bots_helper.synchronous = (fixed && atoi(fixed)) || (threaded && !atoi(threaded));
	if (!bots_helper.synchronous)
	{
		pthread_attr_t attributes;

		pthread_attr_init(&attributes);
		pthread_attr_setstacksize(&attributes, 256 * 1024);
		if (pthread_create(&bots_helper.thread, &attributes, bots_helper_main, NULL))
			bots_helper.synchronous = TRUE;
		pthread_attr_destroy(&attributes);
	}
	platform_log("bots: path searches %s", bots_helper.synchronous ? "in the tick" : "on a helper thread");
}

/* port: (teleporters as shortcuts, below) */
static boolean bots_shortcut_same_goal(struct bots_bot *bot, real_point3d const *position);
static void bots_shortcut_drop(struct bots_bot *bot, char const *why);

/* asks for a path from where the bot's unit is to its goal */
static void bots_vehicle_forget(struct bots_bot *bot);
static void bots_vehicle_avoid(struct bots_bot *bot, long vehicle_index, long ticks);

/* port: a goal the bot finds no way to (bots_nav.c: no hop joins its
region to the bot's): given up, and not gone for again a while - an item
left be, the game's goals rested from, an enemy's place, a roaming place
(nor any near it) avoided */
static void bots_goal_unreachable(struct bots_bot *bot, long now)
{
	bots_globals.play.goals_unreachable++;
	if (getenv("HALO_BOT_TRACE"))
		platform_log("bots: trace bot %ld finds no way to its goal %d at (%.1f %.1f %.1f) region %ld, from (%.1f %.1f %.1f) region %ld",
			(long)(bot - bots_globals.bots) + 1, (int)bot->goal_kind, bot->goal_point.x, bot->goal_point.y, bot->goal_point.z,
			bots_nav_region(bot->goal_surface_index), bot->last_position.x, bot->last_position.y, bot->last_position.z,
			bots_nav_region(bots_surface_below(&bot->last_position, NULL)));
	switch (bot->goal_kind)
	{
	case _bots_goal_item:
		bots_item_set_unreachable(bot, bot->item_index);
		break;
	case _bots_goal_objective:
		bot->objective_rest_until_tick = now + TICKS_PER_SECOND * 10;
		break;
	case _bots_goal_enemy:
		bot->hunt_rest_until_tick = now + TICKS_PER_SECOND * 4;
		break;
	case _bots_goal_vehicle:
		if (bot->vehicle_index != NONE)
			bots_vehicle_avoid(bot, bot->vehicle_index, TICKS_PER_SECOND * 30);
		bots_vehicle_forget(bot);
		break;
	default:
		break;
	}
	bot->avoid_point = bot->goal_point;
	bot->avoid_until_tick = now + TICKS_PER_SECOND * 30;
	bot->goal_kind = _bots_goal_none;
	bot->path.valid = FALSE;
	bot->path_tick = now;
	bot->hop_index = NONE;
}

/* port: the hop the bot's search goes to first, its goal in another region
(bots_nav.c: a jump, a drop, a teleporter or a ladder); the one it has kept
while it is still on the way to it. FALSE when no way joins them (the goal
given up) */
static boolean bots_hop_choose(struct bots_bot *bot, long start_surface_index, real_point3d const *start)
{
	long now = game_time_get();
	struct bots_nav_hop const *hop = bots_nav_hop_get(bot->hop_index);
	long hop_index;
	real cost;

	if (hop && bot->hop_serial == bot->goal_serial && bots_nav_hop_usable(bot->hop_index, now) &&
		bots_nav_region(start_surface_index) == bots_nav_region(hop->entrance_surface_index))
	{
		return TRUE;
	}
	bot->hop_index = NONE;
	bot->hop_take_tick = 0;
	if (!bots_nav_ready())
		return TRUE;
	hop_index = bots_nav_route(start_surface_index, start, bot->goal_surface_index, &bot->goal_point, now, &cost);
	if (hop_index == NONE)
		return TRUE;
	if (hop_index == BOTS_NAV_NO_WAY)
	{
		bots_goal_unreachable(bot, now);
		return FALSE;
	}
	bot->hop_index = hop_index;
	bot->hop_serial = bot->goal_serial;
	bot->hop_take_tick = 0;
	bot->hop_jump_tick = 0;
	if (getenv("HALO_BOT_TRACE"))
	{
		hop = bots_nav_hop_get(hop_index);
		platform_log("bots: trace bot %ld to hop %ld (kind %d) at (%.1f %.1f %.1f) for goal (%.1f %.1f %.1f), the way %.0f",
			(long)(bot - bots_globals.bots) + 1, hop_index, (int)hop->kind, hop->entrance.x, hop->entrance.y, hop->entrance.z,
			bot->goal_point.x, bot->goal_point.y, bot->goal_point.z, (double)cost);
	}
	return TRUE;
}

static void bots_search_ask(struct bots_bot *bot, long unit_index)
{
	struct bots_search *search = NULL;
	real_point3d start;
	long start_surface_index;
	long slot;
	/* port: a bot driving searches from where its vehicle is, as wide as
	the vehicle, the vehicle the search's own object */
	long vehicle_index = bots_unit_vehicle(unit_index);

	/* (one at a time a bot; a free slot, else again next tick) */
	if (bot->search_slot != NONE || !bots_globals.search_slots)
		return;
	for (slot = 0; slot < BOTS_SEARCH_SLOTS && !search; slot++)
	{
		if (bots_search_state(&bots_globals.search_slots[slot]) == _bots_search_idle)
			search = &bots_globals.search_slots[slot];
	}
	if (!search)
		return;
	start_surface_index = vehicle_index != NONE ? vehicle_find_pathfinding_surface_index(vehicle_index, &start) :
		bots_unit_surface(unit_index, &start);
	if (start_surface_index == NONE || bot->goal_surface_index == NONE)
		return;
	/* (on a surface closed to the searches, whose way out may be closed
	too: away from what closed it first, a second) */
	if (vehicle_index == NONE && bots_paths.walkable && start_surface_index < bots_paths.structure.pathfinding_surfaces.count &&
		!TEST_FLAG(bots_paths.walkable[start_surface_index], BOTS_PATHFINDING_WALKABLE_BIT) &&
		bots_paths_escape_direction(&start, &bot->escape_direction))
	{
		bot->escape_until_tick = game_time_get() + TICKS_PER_SECOND;
		bot->path.valid = FALSE;
		bot->path_tick = game_time_get();
		return;
	}
	/* port: a goal in another region: the way there by the hops, the first
	hop's entrance searched for (none: the goal given up); not while a
	teleporter shortcut is under way (its goal is in the bot's region) */
	if (vehicle_index == NONE && bot->shortcut_phase == _bots_shortcut_none && !bot->via_teleporter &&
		!bots_hop_choose(bot, start_surface_index, &start))
	{
		return;
	}
	if (vehicle_index != NONE)
		bot->hop_index = NONE;
	bot->search_slot = (long)(search - bots_globals.search_slots);
	search->bot_index = (long)(bot - bots_globals.bots);
	search->unit_index = vehicle_index != NONE ? vehicle_index : unit_index;
	search->radius = vehicle_index != NONE ? bots_vehicle_path_radius(vehicle_index) : BOTS_PATH_RADIUS;
	search->target_object_index = bot->goal_kind == _bots_goal_vehicle ? bot->vehicle_index : NONE;
	search->start_point = start;
	search->start_surface_index = start_surface_index;
	search->goal_point = bot->goal_point;
	search->goal_surface_index = bot->goal_surface_index;
	search->purpose = _bots_search_for_path;
	/* port: a teleporter as a shortcut (bots_shortcut_weigh), for this goal
	on foot alone: the walk from its exit to the goal measured (the bot
	walks its path meanwhile), then the bot's path to its entrance */
	if (bot->shortcut_phase != _bots_shortcut_none &&
		(vehicle_index != NONE || !bots_shortcut_same_goal(bot, &start)))
	{
		bots_shortcut_drop(bot, vehicle_index != NONE ? "riding" : "another goal");
	}
	if (bot->shortcut_phase == _bots_shortcut_measuring)
	{
		search->purpose = _bots_search_for_exit;
		search->start_point = bots_globals.teleporters[bot->shortcut_index].exit_ground;
		search->start_surface_index = bots_globals.teleporters[bot->shortcut_index].exit_surface_index;
	}
	else if (bot->shortcut_phase == _bots_shortcut_going)
	{
		search->purpose = _bots_search_for_entrance;
		search->goal_point = bots_globals.teleporters[bot->shortcut_index].entrance;
		search->goal_surface_index = bots_globals.teleporters[bot->shortcut_index].entrance_surface_index;
	}
	/* port: to the hop's entrance (bots_nav.c) */
	else if (bot->hop_index != NONE && bots_nav_hop_get(bot->hop_index))
	{
		search->purpose = _bots_search_for_hop;
		search->goal_point = bots_nav_hop_get(bot->hop_index)->entrance;
		search->goal_surface_index = bots_nav_hop_get(bot->hop_index)->entrance_surface_index;
	}
	/* port: Bot routes Lanes or Mixed (bots_routes.c): on foot, to the
	waypoint of the bot's lane on its way */
	else if (vehicle_index == NONE && (bot->goal_kind == _bots_goal_objective || bot->goal_kind == _bots_goal_roam ||
		bot->goal_kind == _bots_goal_enemy))
	{
		bots_routes_search_goal(search->bot_index, &start, &search->goal_point, &search->goal_surface_index);
	}
	search->asked_tick = game_time_get();
	search->goal_serial = bot->goal_serial;
	search->found = FALSE;
	bots_globals.searches++;
	if (vehicle_index != NONE)
		bots_globals.driving_searches++;
	bot->searches++;
	if (bots_helper.synchronous)
	{
		unsigned long long started = vita_host_time_us();

		bots_search_run(search);
		bots_globals.search_us += vita_host_time_us() - started;
		search->state = _bots_search_done;
		return;
	}
	pthread_mutex_lock(&bots_helper.lock);
	search->state = _bots_search_waiting;
	bots_helper.pending = 1;
	pthread_cond_signal(&bots_helper.wake);
	pthread_mutex_unlock(&bots_helper.lock);
}

/* (debug) HALO_BOT_PATH_LOG=1: each failed search's ask and answer, and for
one that reached its goal, the obstacles its path's avoidance met */
static void bots_search_log_failure(struct bots_bot *bot, struct bots_search *search, boolean reached)
{
	struct path_state *state = &search->state_memory;
	short reached_node = reached ? path_node_from_hash_table(state, search->goal_surface_index) : NONE;
	static struct path_debug_storage *debug;
	struct path_result again;
	long step;

	platform_log("bots: path failed bot %ld goal %d from (%.1f %.1f %.1f) s%ld to (%.1f %.1f %.1f) s%ld dist %.1f: "
		"nodes %d heap %d closest %.1f at (%.1f %.1f %.1f) reached %d",
		(long)(bot - bots_globals.bots) + 1, (int)bot->goal_kind,
		search->start_point.x, search->start_point.y, search->start_point.z, search->start_surface_index,
		search->goal_point.x, search->goal_point.y, search->goal_point.z, search->goal_surface_index,
		bots_distance3d(&search->start_point, &search->goal_point), (int)state->node_count, (int)state->heap_count,
		state->closest_distance, state->closest_point.x, state->closest_point.y, state->closest_point.z,
		(int)reached_node);
	if (reached_node == NONE)
		return;
	if (!debug)
		debug = (struct path_debug_storage *)calloc(1, sizeof(*debug));
	if (!debug)
		return;
	csmemset(debug, 0, sizeof(*debug));
	state->debug = debug;
	path_state_build_path(state, &again);
	state->debug = NULL;
	platform_log("bots:   depth %d build %d raw %d smoothed %d avoided %d", (int)state->node_list[reached_node].depth,
		(int)debug->path_build_result, (int)debug->raw_step_count, (int)debug->smoothed_step_count,
		(int)debug->avoided_step_count);
	for (step = 0; step < debug->avoidance_path_count && step < 4; step++)
	{
		struct obstacles *obstacles = &debug->avoidance_obstacles[step];
		long disc;

		platform_log("bots:   step %ld to (%.1f %.1f): %d obstacles", step, debug->smoothed_steps[step].point.x,
			debug->smoothed_steps[step].point.y, (int)obstacles->disc_count);
		for (disc = 0; disc < obstacles->disc_count && disc < 8; disc++)
		{
			struct object_datum *object = object_try_and_get(obstacles->discs[disc].object_index);

			platform_log("bots:     (%.1f %.1f) radius %.2f %s", obstacles->discs[disc].center.x,
				obstacles->discs[disc].center.y, obstacles->discs[disc].radius,
				object ? tag_get_name(object->definition_index) : "?");
		}
	}
}

/* the goal is on ground the search does not walk to from where the bot is
(Chiron TL34's rooms, joined by teleporters alone): a teleporter the search
reached whose exit is nearer the goal than the search came (two units at
least, so that a bot never goes round in teleporters), the one whose way
there and exit's distance to the goal are least; the path to it built.
TRUE when there is one */
static boolean bots_search_teleporter(struct bots_bot *bot, struct bots_search *search, struct path_result *path)
{
	struct path_state *state = &search->state_memory;
	long index, best = NONE;
	real best_cost = REAL_MAX;

	for (index = 0; index < bots_globals.teleporter_count; index++)
	{
		short node_index = path_node_from_hash_table(state, bots_globals.teleporters[index].entrance_surface_index);
		real beyond = bots_distance3d(&bots_globals.teleporters[index].exit, &search->goal_point);
		real cost;

		/* port: (a ladder no bot got up, tried three times: none; and a
		teleporter that failed bots three times as a shortcut and never sent
		one: bots_shortcut_fail) */
		if (bots_globals.teleporters[index].ladder && bots_globals.teleporters[index].failures >= 3 &&
			!bots_globals.teleporters[index].climbs)
		{
			continue;
		}
		if (!bots_globals.teleporters[index].ladder && bots_globals.teleporters[index].failures >= 3 &&
			!bots_globals.teleporters[index].through[_bots_through_shortcut] &&
			!bots_globals.teleporters[index].through[_bots_through_last_way] &&
			!bots_globals.teleporters[index].through[_bots_through_by_chance])
		{
			continue;
		}
		if (node_index == NONE || beyond > state->closest_distance - 2.0f)
			continue;
		cost = state->node_list[node_index].path_distance_from_origin + beyond;
		if (cost < best_cost)
		{
			best_cost = cost;
			best = index;
		}
	}
	if (best == NONE)
		return FALSE;
	state->destination.point = bots_globals.teleporters[best].entrance;
	state->destination.surface_index = bots_globals.teleporters[best].entrance_surface_index;
	if (!path_state_build_path(state, path) || path->step_count <= 0)
	{
		/* (the bot on the teleporter already, as it comes out: onto it) */
		if (bots_distance2d(&search->start_point, &bots_globals.teleporters[best].entrance) > 1.0f)
			return FALSE;
		csmemset(path, 0, sizeof(*path));
	}
	bot->via_teleporter = TRUE;
	bot->via_point = bots_globals.teleporters[best].entrance;
	bot->via_tick = game_time_get();
	bot->via_index = best;
	bot->via_climb_tick = 0;
	bot->via_top_tick = 0;
	if (bots_globals.teleporters[best].ladder)
	{
		bots_globals.ladder_tries++;
		if (getenv("HALO_BOT_TRACE"))
			platform_log("bots: trace bot %ld to the ladder at (%.1f %.1f %.1f), for (%.1f %.1f %.1f)", (long)(bot - bots_globals.bots) + 1,
				bot->via_point.x, bot->via_point.y, bot->via_point.z, search->goal_point.x, search->goal_point.y, search->goal_point.z);
	}
	return TRUE;
}

/* ---------- port: teleporters as shortcuts

A goal the walkable surfaces reach was walked to the whole way, through no
teleporter however much shorter it was (Blood Gulch's, from beside each
base's flag out to the middle of the canyon, half the way to the other base;
Battle Creek's between the bases). Now a bot on foot weighs the map's
teleporters as a search to its goal answers: the walk (the search's own, the
surfaces' entry points apart; as near as it came and on in a straight line,
for a search that stopped short) against the way through one - the walk to
its entrance (the same search's, when it came to it; else a straight line,
as short as it can be) and from its exit on to the goal (a straight line
first). The one whose way through is least, 80% of the walk at most and 15
units shorter at least, is weighed again with the walk from its exit
measured: a search from the exit's ground to the goal (the bot walks its
path meanwhile; nothing of it is walked). Still as short, the bot takes it
(an entrance weighed by a straight line, once its first search there says
how far it is, and it is still short enough): its searches go to the
entrance, it walks onto it as onto the last way's (bots_search_teleporter:
the path's end, then onto the flag), and goes on from where it comes out
with a new search (no teleporter weighed again for that goal). Not for every
bot, nor every goal: on foot alone (a vehicle does not go through: a driver
drives on, and getting out is the vehicle's to weigh); to the game's
objective, a roaming place or an enemy last seen (an item or a vehicle is
near, and comes first; a fight goes on first, its clock stopped; a bot fresh
from its base with a vehicle near weighs them once its ten fresh seconds are
over, its base's vehicles weighed first); never a defender (bots_roles.c: it
keeps about its flag, or its hill), nor a bot on a side lane (bots_routes.c:
the lanes spread a team out, the teleporter would gather it again); two bots
at most go for one teleporter at once, and a bot weighs them once a goal,
five seconds apart at least. A teleporter that fails a bot - it stood on the
entrance two seconds and was not sent (another player on the exit blocks it:
game_engine.c), or did not come to it in time (the walk there at a slow
pace, and eight seconds), or its search finds no way to the entrance
(Damnation's upper one, from below) - is left be thirty seconds by every
bot, and one that failed three times and never sent a bot, for the map (the
last way too, as a ladder no bot got up). halo.log has the map's
teleporters, each shortcut taken, each bot sent and each failure, and each
minute's census in the moves line and the teleporters line (each
teleporter's bots sent, by how they came to it: a shortcut, the last way, or
by chance) */

/* whether bots take teleporters as shortcuts (HALO_BOT_SHORTCUTS=0: never,
the bots as before, for a comparison) */
static boolean bots_shortcuts_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		char const *setting = getenv("HALO_BOT_SHORTCUTS");

		enabled = setting && setting[0] && !atoi(setting) ? 0 : 1;
	}
	return enabled != 0;
}

/* the goals a shortcut is taken to: the game's objective (a vantage point
too), a roaming place, an enemy last seen */
static boolean bots_shortcut_goal_kind(struct bots_bot const *bot)
{
	return bot->goal_kind == _bots_goal_objective || bot->goal_kind == _bots_goal_roam ||
		bot->goal_kind == _bots_goal_enemy;
}

/* whether the way through (to the entrance and from the exit) is short
enough against the walk */
static boolean bots_shortcut_pays(real walk, real through)
{
	return through <= walk * BOTS_SHORTCUT_SHARE && walk - through >= BOTS_SHORTCUT_GAIN;
}

/* whether the bot's goal is still the one its shortcut was taken for: the
same, or one of the kinds a shortcut is taken to that moved a little (an
enemy last seen further on, the ball's carrier, another roaming place
beside it), or moved further and the teleporter still clearly short of the
way there in straight lines (the walk there as short as it can be: the
enemy chased on through it); the shortcut then for it */
static boolean bots_shortcut_same_goal(struct bots_bot *bot, real_point3d const *position)
{
	real_point3d const *entrance = &bots_globals.teleporters[bot->shortcut_index].entrance;
	real_point3d const *exit = &bots_globals.teleporters[bot->shortcut_index].exit_ground;

	if (!bots_shortcut_goal_kind(bot))
		return FALSE;
	if (bot->goal_serial == bot->shortcut_serial)
		return TRUE;
	if (bots_distance3d(&bot->goal_point, &bot->shortcut_goal) > 8.0f &&
		!bots_shortcut_pays(bots_distance3d(position, &bot->goal_point),
			bots_distance3d(position, entrance) + bots_distance3d(exit, &bot->goal_point)))
	{
		return FALSE;
	}
	bot->shortcut_serial = bot->goal_serial;
	bot->shortcut_goal = bot->goal_point;
	return TRUE;
}

/* (debug: HALO_BOT_SHORTCUT_LOG) a goal's kind */
static char const *bots_goal_kind_name(short kind)
{
	return kind == _bots_goal_objective ? " (objective)" : kind == _bots_goal_roam ? " (roaming)" :
		kind == _bots_goal_enemy ? " (an enemy)" : "";
}

/* the times a teleporter sent a bot, however it came to it */
static long bots_teleporter_sent(long index)
{
	long kind, sent = 0;

	for (kind = 0; kind < NUMBER_OF_BOTS_THROUGH; kind++)
		sent += bots_globals.teleporters[index].through[kind];
	return sent;
}

/* whether a teleporter is one to go for as a shortcut now: a teleporter
(not a ladder) whose exit is on the level's ground, not left be */
static boolean bots_shortcut_usable(long index, long now)
{
	return !bots_globals.teleporters[index].ladder && bots_globals.teleporters[index].exit_surface_index != NONE &&
		now >= bots_globals.teleporters[index].rest_until_tick &&
		!(bots_globals.teleporters[index].failures >= 3 && !bots_teleporter_sent(index));
}

/* the bots going for a teleporter as a shortcut (or weighing it), but one */
static long bots_shortcut_claimers(long index, struct bots_bot const *except)
{
	long bot_index, count = 0;

	for (bot_index = 0; bot_index < BOTS_MAXIMUM; bot_index++)
	{
		struct bots_bot const *other = &bots_globals.bots[bot_index];

		if (other != except && other->player_index != NONE && other->shortcut_phase != _bots_shortcut_none &&
			other->shortcut_index == index)
		{
			count++;
		}
	}
	return count;
}

/* how far a search walked to a surface it came to, and on to the point on
it (the surfaces' entry points apart: the raw way, a little longer than the
smoothed path walked, as long for every way weighed); a negative for one it
did not come to */
static real bots_search_walk(struct path_state const *state, long surface_index, real_point3d const *point)
{
	short node_index = surface_index != NONE ? path_node_from_hash_table((struct path_state *)state, surface_index) : NONE;

	if (node_index == NONE || node_index < 0 || node_index >= state->node_count)
		return -1.0f;
	return state->node_list[node_index].path_distance_from_origin +
		bots_distance3d(&state->node_list[node_index].entry_point, point);
}

/* the shortcut let go (another goal, riding, another teleporter, its
searches to the entrance unanswered): the bot's path to its goal again, the
teleporters not weighed again for this goal */
static void bots_shortcut_drop(struct bots_bot *bot, char const *why)
{
	if (bot->shortcut_phase == _bots_shortcut_none)
		return;
	if (bot->shortcut_phase == _bots_shortcut_going && bot->shortcut_entrance_known)
	{
		bots_globals.shortcut_drops++;
		platform_log("bots: bot %ld off teleporter %ld (%s) after %.0f s", (long)(bot - bots_globals.bots) + 1,
			bot->shortcut_index + 1, why, (double)(game_time_get() - bot->shortcut_tick) / TICKS_PER_SECOND);
	}
	if (bot->shortcut_phase == _bots_shortcut_going)
	{
		if (bot->via_teleporter && bot->via_index == bot->shortcut_index)
			bot->via_teleporter = FALSE;
		bot->path.valid = FALSE;
	}
	bot->shortcut_phase = _bots_shortcut_none;
	bot->shortcut_weighed_serial = bot->goal_serial;
}

/* the teleporter failed the bot (not sent from its entrance, or not come to
in time): left be a while by every bot (for the map after three failures,
none sent), the bot on to its goal on foot */
static void bots_shortcut_fail(struct bots_bot *bot, char const *why, long now)
{
	long index = bot->shortcut_index;

	bots_globals.teleporters[index].failures++;
	bots_globals.teleporters[index].rest_until_tick = now + TICKS_PER_SECOND * BOTS_SHORTCUT_REST_SECONDS;
	bots_globals.shortcut_failures++;
	platform_log("bots: teleporter %ld failed bot %ld (%s) after %.0f s: left be %s", index + 1,
		(long)(bot - bots_globals.bots) + 1, why, (double)(now - bot->shortcut_tick) / TICKS_PER_SECOND,
		bots_shortcut_usable(index, bots_globals.teleporters[index].rest_until_tick) ? "a while" : "for the map");
	if (bot->via_teleporter && bot->via_index == index)
		bot->via_teleporter = FALSE;
	bot->shortcut_phase = _bots_shortcut_none;
	bot->shortcut_weighed_serial = bot->goal_serial;
	bot->path.valid = FALSE;
	bot->path_tick = now - TICKS_PER_SECOND;
}

/* whether bots take vehicles and one a bot may drive is near (25 units:
its base's), not destroyed nor closed to players */
static boolean bots_shortcut_vehicle_near(real_point3d const *position)
{
	struct object_iterator iterator;

	if (!bots_vehicles_enabled())
		return FALSE;
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (object_iterator_next(&iterator))
	{
		struct unit_datum *vehicle = unit_get(iterator.index);

		if (!TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) &&
			!TEST_FLAG(vehicle->unit.flags, _unit_not_enterable_by_player_bit) && bots_vehicle_drivable(iterator.index) &&
			bots_distance2d(&vehicle->object.position, position) < 25.0f)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* port: a search for the bot's path answered, the goal reached in it: the
jumps and drops of the bot's region (bots_nav.c) weighed against the walk,
as a player drops off a balcony instead of walking round to the stairs, or
jumps up a ledge: the walk to its entrance (the search's), the jump, and on
from its landing (a straight line, half as long again: a walk there is no
shorter); taken when that is 70% of the walk at most and 8 units shorter.
Once a goal; the hop is the bot's next search's goal (bots_hop_choose keeps
it), and from where it lands it searches on */
static void bots_jump_shortcut(struct bots_bot *bot, struct bots_search *search)
{
	struct path_state const *state = &search->state_memory;
	long now = game_time_get(), cursor = 0, hop_index, best = NONE;
	long region = bots_nav_region(search->start_surface_index);
	real walk, best_through = REAL_MAX;

	if (bot->jump_weighed_serial == bot->goal_serial || region == NONE || bot->hop_index != NONE ||
		search->purpose != _bots_search_for_path || search->radius > BOTS_PATH_RADIUS ||
		bots_distance3d(&search->goal_point, &bot->goal_point) > 0.5f ||
		(bot->goal_kind != _bots_goal_objective && bot->goal_kind != _bots_goal_roam && bot->goal_kind != _bots_goal_enemy &&
			bot->goal_kind != _bots_goal_item))
	{
		return;
	}
	bot->jump_weighed_serial = bot->goal_serial;
	walk = bots_search_walk(state, search->goal_surface_index, &search->goal_point);
	if (walk < 16.0f)
		return;
	while ((hop_index = bots_nav_next_jump_from(region, &cursor)) != NONE)
	{
		struct bots_nav_hop const *hop = bots_nav_hop_get(hop_index);
		real entrance, through;

		if (!bots_nav_hop_usable(hop_index, now) || bots_nav_region(hop->exit_surface_index) != region)
			continue;
		entrance = bots_search_walk(state, hop->entrance_surface_index, &hop->entrance);
		if (entrance < 0.0f)
			continue;
		through = entrance + hop->cost + bots_distance3d(&hop->exit, &search->goal_point) * 1.5f;
		if (through <= walk * 0.7f && walk - through >= 8.0f && through < best_through)
		{
			best = hop_index;
			best_through = through;
		}
	}
	if (best == NONE)
		return;
	bot->hop_index = best;
	bot->hop_serial = bot->goal_serial;
	bot->hop_take_tick = 0;
	bot->hop_jump_tick = 0;
	bots_globals.play.jump_shortcuts++;
	if (getenv("HALO_BOT_TRACE"))
		platform_log("bots: trace bot %ld takes hop %ld (kind %d) as a shortcut: %.0f against the walk %.0f",
			(long)(bot - bots_globals.bots) + 1, best, (int)bots_nav_hop_get(best)->kind, (double)best_through, (double)walk);
	/* (its next search to the hop's entrance, at once) */
	bot->path_tick = now - TICKS_PER_SECOND * 4 - 1;
}

/* a search for the bot's path answered: the teleporters weighed against the
walk it found (above), the best one's exit measured next. Its path stands
meanwhile */
static void bots_shortcut_weigh(struct bots_bot *bot, struct bots_search *search, boolean reached)
{
	struct path_state const *state = &search->state_memory;
	long bot_index = (long)(bot - bots_globals.bots);
	struct bots_tactics_bot const *tactics = &bots_tactics_bots[bot_index];
	long now = game_time_get(), index, best = NONE;
	real walk, best_through = REAL_MAX, best_entrance = 0.0f;
	boolean best_known = FALSE;

	if (!bots_globals.teleporter_shortcuts || !bots_shortcuts_enabled() || bot->shortcut_phase != _bots_shortcut_none ||
		search->purpose != _bots_search_for_path || search->radius > BOTS_PATH_RADIUS || !bots_shortcut_goal_kind(bot) ||
		bot->goal_serial == bot->shortcut_weighed_serial || now < bot->shortcut_weigh_tick)
	{
		return;
	}
	/* (the search went to the goal itself, not a lane's waypoint; never a
	defender, nor a bot on a side lane; a bot fresh from its base with a
	vehicle about weighs them once the vehicles are weighed, its base's
	first: bots_vehicle_consider, a teleporter beside its flag taken at once
	would leave them all behind - Blood Gulch's) */
	if (bots_distance3d(&search->goal_point, &bot->goal_point) > 0.5f || tactics->role == _bots_role_defender ||
		(tactics->lane != 0 && !tactics->lane_given_up) ||
		(now - bot->spawn_tick < TICKS_PER_SECOND * BOTS_VEHICLE_FRESH_SECONDS && bots_shortcut_vehicle_near(&search->start_point)))
	{
		return;
	}
	bot->shortcut_weigh_tick = now + TICKS_PER_SECOND * BOTS_SHORTCUT_WEIGH_SECONDS;
	bot->shortcut_weighed_serial = bot->goal_serial;
	/* (the walk: to the goal, else as near as the search came and on in a
	straight line - one stopped short, or out of nodes: Blood Gulch's from
	base to base takes more surfaces than a search holds; a search spent,
	the goal on ground it does not walk to, is the last way's, bots_search_
	teleporter) */
	if (reached)
		walk = bots_search_walk(state, search->goal_surface_index, &search->goal_point);
	else if ((state->heap_count > 1 || state->node_count >= PATH_NODE_LIST_SIZE) && state->closest_node_index >= 0 &&
		state->closest_node_index < state->node_count)
		walk = state->node_list[state->closest_node_index].path_distance_from_origin + state->closest_distance;
	else
		return;
	if (walk < BOTS_SHORTCUT_GAIN * 2.0f)
		return;
	for (index = 0; index < bots_globals.teleporter_count; index++)
	{
		real entrance, through;
		boolean known;

		if (!bots_shortcut_usable(index, now) || bots_shortcut_claimers(index, bot) >= BOTS_SHORTCUT_BOTS)
			continue;
		/* (an exit where the bot is: no way on) */
		if (bots_distance3d(&bots_globals.teleporters[index].exit_ground, &search->start_point) < 8.0f)
			continue;
		entrance = bots_search_walk(state, bots_globals.teleporters[index].entrance_surface_index,
			&bots_globals.teleporters[index].entrance);
		known = entrance >= 0.0f;
		if (!known)
			entrance = bots_distance3d(&search->start_point, &bots_globals.teleporters[index].entrance);
		through = entrance + bots_distance3d(&bots_globals.teleporters[index].exit_ground, &search->goal_point);
		/* (debug) HALO_BOT_SHORTCUT_LOG=1: each teleporter weighed */
		if (getenv("HALO_BOT_SHORTCUT_LOG"))
		{
			platform_log("bots: shortcut bot %ld from (%.1f %.1f %.1f) to (%.1f %.1f %.1f)%s: the walk %.0f%s, teleporter %ld "
				"%.0f to it%s and %.0f on: %s", bot_index + 1, search->start_point.x, search->start_point.y,
				search->start_point.z, search->goal_point.x, search->goal_point.y, search->goal_point.z,
				bots_goal_kind_name(bot->goal_kind), (double)walk, reached ? "" : " (short)", index + 1, (double)entrance,
				known ? "" : " (straight)", (double)(through - entrance), bots_shortcut_pays(walk, through) ? "pays" : "no");
		}
		if (bots_shortcut_pays(walk, through) && through < best_through)
		{
			best = index;
			best_through = through;
			best_entrance = entrance;
			best_known = known;
		}
	}
	if (best == NONE)
		return;
	bot->shortcut_phase = _bots_shortcut_measuring;
	bot->shortcut_index = best;
	bot->shortcut_serial = bot->goal_serial;
	bot->shortcut_goal = bot->goal_point;
	bot->shortcut_walk = walk;
	bot->shortcut_walk_short = !reached;
	bot->shortcut_entrance = best_entrance;
	bot->shortcut_entrance_known = best_known;
	bot->shortcut_tick = now;
	/* (the measuring search asked next tick; given up if not answered in
	ten seconds) */
	bot->shortcut_deadline_tick = now + TICKS_PER_SECOND * 10;
	bot->path_tick = now - TICKS_PER_SECOND * 4 - 1;
}

/* the shortcut taken: halo.log's line, and the time it has to come to the
entrance (the walk there at a slow pace, a unit and a half a second, and
eight seconds) */
static void bots_shortcut_take(struct bots_bot *bot, long now)
{
	bot->shortcut_tick = now;
	bot->shortcut_deadline_tick = now + (long)(TICKS_PER_SECOND * (8.0f + bot->shortcut_entrance / 1.5f));
	bots_globals.shortcut_takes++;
	platform_log("bots: bot %ld to teleporter %ld for (%.1f %.1f %.1f): %.0f units to it and %.0f from its exit, the walk %.0f%s",
		(long)(bot - bots_globals.bots) + 1, bot->shortcut_index + 1, bot->goal_point.x, bot->goal_point.y,
		bot->goal_point.z, (double)bot->shortcut_entrance, (double)bot->shortcut_beyond, (double)bot->shortcut_walk,
		bot->shortcut_walk_short ? " at least" : "");
}

/* the walk from the teleporter's exit to the goal measured: the shortcut
taken when the way through is still short enough */
static void bots_shortcut_measured(struct bots_bot *bot, struct bots_search *search)
{
	struct path_state const *state = &search->state_memory;
	long now = game_time_get();
	real beyond;

	if (bot->shortcut_phase != _bots_shortcut_measuring || search->goal_serial != bot->shortcut_serial)
		return;
	bots_globals.shortcut_measures++;
	/* (to the goal; a search out of nodes, as near as it came and on in a
	straight line, as the walk it is weighed against was) */
	beyond = state->node_count > 0 ? bots_search_walk(state, search->goal_surface_index, &search->goal_point) : -1.0f;
	if (beyond < 0.0f && state->node_count >= PATH_NODE_LIST_SIZE && state->closest_node_index >= 0 &&
		state->closest_node_index < state->node_count)
	{
		beyond = state->node_list[state->closest_node_index].path_distance_from_origin + state->closest_distance;
	}
	/* (debug) HALO_BOT_SHORTCUT_LOG=1: the walk from the exit */
	if (getenv("HALO_BOT_SHORTCUT_LOG"))
	{
		platform_log("bots: shortcut bot %ld from teleporter %ld's exit (%.1f %.1f %.1f) s%ld: %.0f on (nodes %d heap %d closest %.1f)",
			(long)(bot - bots_globals.bots) + 1, bot->shortcut_index + 1, search->start_point.x, search->start_point.y,
			search->start_point.z, search->start_surface_index, (double)beyond, (int)state->node_count,
			(int)state->heap_count, (double)state->closest_distance);
	}
	if (beyond < 0.0f || !bots_shortcut_pays(bot->shortcut_walk, bot->shortcut_entrance + beyond))
	{
		bot->shortcut_phase = _bots_shortcut_none;
		return;
	}
	/* (its searches to the entrance next: the shortcut taken now, or once
	the first says how far the entrance is, when weighed by a straight line) */
	bot->shortcut_phase = _bots_shortcut_going;
	bot->shortcut_beyond = beyond;
	bot->shortcut_on_tick = 0;
	bot->path_tick = now - TICKS_PER_SECOND * 4 - 1;
	if (bot->shortcut_entrance_known)
		bots_shortcut_take(bot, now);
	else
		bot->shortcut_deadline_tick = now + TICKS_PER_SECOND * 10;
}

/* a search to the teleporter's entrance answered: the walk there, weighed
by a straight line, checked (no longer short enough, the shortcut not taken
after all); TRUE when the path to it (or towards it) stands */
static boolean bots_shortcut_entrance_answer(struct bots_bot *bot, struct bots_search *search, boolean reached)
{
	if (bot->shortcut_phase != _bots_shortcut_going || search->goal_serial != bot->shortcut_serial)
		return FALSE;
	/* (the search spent, the entrance not among the surfaces the bot walks
	to: a failure, Damnation's upper one from below; a search that stopped
	short is a way towards it, the next from nearer) */
	if (!reached && search->state_memory.heap_count <= 1 && search->state_memory.node_count < PATH_NODE_LIST_SIZE)
	{
		bots_shortcut_fail(bot, "no way to its entrance", game_time_get());
		return FALSE;
	}
	if (!reached)
		return TRUE;
	if (!bot->shortcut_entrance_known)
	{
		real entrance = bots_search_walk(&search->state_memory, search->goal_surface_index, &search->goal_point);

		/* (further than the straight line said, and no longer short enough:
		not taken after all) */
		if (!bots_shortcut_pays(bot->shortcut_walk, entrance + bot->shortcut_beyond))
		{
			bot->shortcut_phase = _bots_shortcut_none;
			return FALSE;
		}
		bot->shortcut_entrance_known = TRUE;
		bot->shortcut_entrance = entrance;
		bots_shortcut_take(bot, game_time_get());
	}
	return TRUE;
}

/* the bot moved further than a step at once: sent by a teleporter when it
was at one's entrance and is at its exit (halo.log's census, by how it came
to it); its shortcut done, or dropped for another teleporter */
static void bots_teleporter_landed(struct bots_bot *bot, real_point3d const *from, real_point3d const *to, long now)
{
	long index, kind;

	for (index = 0; index < bots_globals.teleporter_count; index++)
	{
		if (bots_globals.teleporters[index].ladder || bots_distance3d(&bots_globals.teleporters[index].exit, to) > 2.0f ||
			bots_distance2d(&bots_globals.teleporters[index].entrance, from) > 1.5f ||
			fabs(bots_globals.teleporters[index].entrance.z - from->z) > 2.0f)
		{
			continue;
		}
		break;
	}
	if (index >= bots_globals.teleporter_count)
		return;
	if (bot->shortcut_phase == _bots_shortcut_going && bot->shortcut_index == index)
		kind = _bots_through_shortcut;
	else if (bot->via_teleporter && bot->via_index == index)
		kind = _bots_through_last_way;
	else
		kind = _bots_through_by_chance;
	bots_globals.teleporters[index].through[kind]++;
	bots_globals.shortcut_through[kind]++;
	if (kind == _bots_through_shortcut)
	{
		platform_log("bots: bot %ld through teleporter %ld after %.0f s, to (%.1f %.1f %.1f)", (long)(bot - bots_globals.bots) + 1,
			index + 1, (double)(now - bot->shortcut_tick) / TICKS_PER_SECOND, bot->goal_point.x, bot->goal_point.y,
			bot->goal_point.z);
		bot->shortcut_phase = _bots_shortcut_none;
		bot->shortcut_weighed_serial = bot->goal_serial;
	}
	else
		bots_shortcut_drop(bot, "another teleporter");
}

/* the search's way to its destination (or as near as it came): each
surface's entry point (path_state_build_path's raw steps, at most 64), the
endpoint last. TRUE with steps */
static boolean bots_path_raw_steps(struct path_state *state, struct path_step *raw_steps, short *raw_step_count,
	boolean *steps_finish_path, real_point3d *endpoint)
{
	short node_index, child_index = NONE;

	*steps_finish_path = TRUE;
	*raw_step_count = 0;
	if (!state->destination_valid || state->node_count <= 0)
		return FALSE;
	node_index = path_node_from_hash_table(state, state->destination.surface_index);
	*endpoint = state->destination.point;
	if (node_index == NONE)
	{
		node_index = state->closest_node_index;
		*endpoint = state->closest_point;
	}
	if (node_index < 0 || node_index >= state->node_count)
		return FALSE;
	*raw_step_count = (short)MIN(state->node_list[node_index].depth + 1, 64);
	while (node_index != NONE)
	{
		struct path_node *node;

		if (node_index < 0 || node_index >= state->node_count)
			return FALSE;
		node = &state->node_list[node_index];
		if (node->depth >= 64)
			*steps_finish_path = FALSE;
		else if (node->depth >= 0 && node->depth < *raw_step_count)
		{
			raw_steps[node->depth].surface_index = node->surface_index;
			raw_steps[node->depth].point = child_index == NONE ? *endpoint : state->node_list[child_index].entry_point;
		}
		else
			return FALSE;
		child_index = node_index;
		node_index = node->parent_node_index;
	}
	return *raw_step_count > 0;
}

/* the path as path_state_build_path makes it, the steps smoothed, but not
taken round the objects about (the obstacle avoidance found no way round:
the other bots crowding a narrow way out of a base, Longest's, all waiting
on each other); the bots push past each other, and one that stops moving
sidesteps and jumps. TRUE with a path */
static boolean bots_build_path_unavoided(struct path_state *state, struct path_result *path)
{
	struct path_step raw_steps[64];
	struct path_step smoothed_steps[MAXIMUM_SMOOTHED_PATH_STEPS];
	short raw_step_count, smoothed_step_count = 0;
	boolean steps_finish_path;
	real_point3d endpoint;

	csmemset(path, 0, sizeof(*path));
	if (!bots_path_raw_steps(state, raw_steps, &raw_step_count, &steps_finish_path, &endpoint))
		return FALSE;
	path_smooth(state, raw_step_count, raw_steps, &smoothed_step_count, smoothed_steps, &steps_finish_path);
	if (smoothed_step_count <= 0)
		return FALSE;
	smoothed_step_count = MIN(smoothed_step_count, MAXIMUM_SMOOTHED_PATH_STEPS);
	path->valid = TRUE;
	path->endpoint.point = endpoint;
	path->endpoint.surface_index = smoothed_steps[smoothed_step_count - 1].surface_index;
	path->steps_finish_path = steps_finish_path;
	path->step_count = (char)smoothed_step_count;
	path->step_index = 0;
	csmemcpy(path->steps, smoothed_steps, smoothed_step_count * sizeof(struct path_step));
	return TRUE;
}

/* a path that goes nowhere: short of its end, every step where the bot
stands. The smoothing (path_smooth) gave one at the foot of a steep bank:
all four of its steps the same point a few centimetres from the bot, which
it took as reached at once and searched again, the same, while it slid
about (Sidewinder's banks up to the side ledges, under Team Race's flags,
where the bots went round in circles: issue #40) */
static boolean bots_path_stalls(struct path_result const *path, real_point3d const *start)
{
	return path->valid && !path->steps_finish_path && path->step_count > 0 &&
		bots_distance2d(&path->steps[path->step_count - 1].point, start) <= BOTS_STEP_REACHED;
}

/* a stalling path's steps instead the search's own, unsmoothed: the next
surfaces' entry points beyond where the bot stands (up the bank), four at
most. TRUE when made */
static boolean bots_path_unsmoothed(struct path_state *state, real_point3d const *start, struct path_result *path)
{
	struct path_step raw_steps[64];
	short raw_step_count, step, count = 0;
	boolean steps_finish_path;
	real_point3d endpoint;
	struct path_result result;

	if (!bots_path_raw_steps(state, raw_steps, &raw_step_count, &steps_finish_path, &endpoint))
		return FALSE;
	csmemset(&result, 0, sizeof(result));
	for (step = 0; step < raw_step_count && count < (short)NUMBEROF(result.steps); step++)
	{
		if (!count && bots_distance2d(&raw_steps[step].point, start) <= BOTS_STEP_REACHED)
			continue;
		result.steps[count++] = raw_steps[step];
	}
	if (!count)
		return FALSE;
	result.valid = TRUE;
	result.steps_finish_path = steps_finish_path && step >= raw_step_count;
	result.step_count = (char)count;
	result.endpoint.point = endpoint;
	result.endpoint.surface_index = result.steps[count - 1].surface_index;
	*path = result;
	return TRUE;
}

/* a search's answer made a path: the way to the goal (or as near as the
search came), else through a teleporter when the goal is on ground the
search does not walk to; when the path's obstacle avoidance finds no way,
again with what the goal or the bot stands on (a flag's stand, a
teleporter's base) no obstacle, and failing that, the scenery and machines
about the bot are learnt to be in the way (the next searches go round them)
and this once the path is taken without the avoidance. TRUE with a path */
static boolean bots_search_build(struct bots_bot *bot, struct bots_search *search, boolean reached,
	struct path_result *path)
{
	struct path_state *state = &search->state_memory;
	long object_index;

	if (!search->found || state->node_count <= 0)
		return FALSE;
	/* (the search spent: every surface the bot walks to tried, the goal
	not among them) */
	if (!reached && state->heap_count <= 1 && bots_search_teleporter(bot, search, path))
		return TRUE;
	if (path_state_build_path(state, path) && path->step_count > 0)
		return TRUE;
	object_index = bots_object_at(&search->goal_point);
	if (object_index == NONE)
		object_index = bots_object_at(&search->start_point);
	if (object_index != NONE)
	{
		state->input.ignore_target_object_index = object_index;
		csmemset(path, 0, sizeof(*path));
		if (path_state_build_path(state, path) && path->step_count > 0)
			return TRUE;
	}
	/* (something new learnt: a new search, round it; port: not from a
	vehicle's search, whose width, not the scenery, finds no way where a
	biped walks) */
	if (search->radius <= BOTS_PATH_RADIUS && bots_paths_learn(&search->start_point, &search->goal_point) > 0)
		return FALSE;
	if (!bots_build_path_unavoided(state, path))
		return FALSE;
	bots_globals.search_unavoided++;
	return TRUE;
}

/* a search's answer taken (in the tick: the path's smoothing and the
objects it goes around are the tick's), into the bot's path */
static void bots_search_take(struct bots_bot *bot, boolean alive)
{
	struct bots_search *search;

	if (bot->search_slot == NONE)
		return;
	search = &bots_globals.search_slots[bot->search_slot];
	if (bots_search_state(search) != _bots_search_done)
		return;
	/* port: the walk from a teleporter's exit measured (no path of the
	bot's: bots_shortcut_measured) */
	if (alive && search->goal_serial == bot->goal_serial && search->purpose == _bots_search_for_exit)
		bots_shortcut_measured(bot, search);
	/* port: a path to a teleporter's entrance, the shortcut since dropped:
	none, a new search soon */
	else if (alive && search->goal_serial == bot->goal_serial && search->purpose == _bots_search_for_entrance &&
		bot->shortcut_phase != _bots_shortcut_going)
	{
		bot->path.valid = FALSE;
		bot->path_tick = game_time_get() - TICKS_PER_SECOND;
	}
	else if (alive && search->goal_serial == bot->goal_serial)
	{
		struct path_state *state = &search->state_memory;
		struct path_result path;
		unsigned long long started = vita_host_time_us();
		boolean reached = state->node_count > 0 &&
			path_node_from_hash_table(state, search->goal_surface_index) != NONE;
		boolean entrance = search->purpose == _bots_search_for_entrance;
		boolean to_hop = search->purpose == _bots_search_for_hop;

		/* port: (no way to the hop's entrance after all - its surface closed
		to the searches since: the hop failed, the way again next) */
		if (to_hop && !reached && state->heap_count <= 1 && state->node_count < PATH_NODE_LIST_SIZE)
		{
			bots_nav_hop_result(bot->hop_index, FALSE, game_time_get());
			bot->hop_index = NONE;
			bot->path.valid = FALSE;
			bot->path_tick = game_time_get() - TICKS_PER_SECOND;
			bot->search_slot = NONE;
			bots_search_set_state(search, _bots_search_idle);
			bots_globals.search_failures++;
			return;
		}

		/* port: (to a teleporter's entrance, the shortcut: the walk there
		checked, or the shortcut dropped and a new search soon) */
		if (entrance && !bots_shortcut_entrance_answer(bot, search, reached))
		{
			bot->path.valid = FALSE;
			bot->path_tick = game_time_get() - TICKS_PER_SECOND;
			bot->search_slot = NONE;
			bots_search_set_state(search, _bots_search_idle);
			return;
		}
		if (state->node_count >= PATH_NODE_LIST_SIZE)
			bots_globals.search_overflows++;
		if (!reached)
		{
			bots_globals.search_partials++;
			if (getenv("HALO_BOT_PATH_LOG") && atoi(getenv("HALO_BOT_PATH_LOG")) >= 2)
				platform_log("bots: path short bot %ld goal %d from (%.1f %.1f %.1f) to (%.1f %.1f %.1f) s%ld: nodes %d "
					"heap %d closest %.1f at (%.1f %.1f %.1f)", (long)(bot - bots_globals.bots) + 1, (int)bot->goal_kind,
					search->start_point.x, search->start_point.y, search->start_point.z, search->goal_point.x,
					search->goal_point.y, search->goal_point.z, search->goal_surface_index, (int)state->node_count,
					(int)state->heap_count, state->closest_distance, state->closest_point.x, state->closest_point.y,
					state->closest_point.z);
		}
		csmemset(&path, 0, sizeof(path));
		bot->via_teleporter = FALSE;
		if (bots_search_build(bot, search, reached, &path) ||
			(entrance && bots_distance2d(&search->start_point, &search->goal_point) < 1.0f))
		{
			if (bots_path_stalls(&path, &search->start_point) &&
				bots_path_unsmoothed(&search->state_memory, &search->start_point, &path))
			{
				bots_globals.search_unsmoothed++;
			}
			/* port: (to a teleporter's entrance, the shortcut: onto it at
			the path's end, as onto the last way's; on it already, onto it) */
			if (entrance && reached)
			{
				bot->via_teleporter = TRUE;
				bot->via_point = search->goal_point;
				bot->via_tick = game_time_get();
				bot->via_index = bot->shortcut_index;
				bot->via_climb_tick = 0;
				bot->via_top_tick = 0;
			}
			bot->path = path;
			bot->path.step_index = 0;
			bot->path_failures = 0;
			/* (port: towards a teleporter's entrance it has yet to come to,
			no climbing on to the goal: the next search from nearer) */
			bot->path_short = !reached && !bot->via_teleporter && !entrance && !to_hop;
			if (!bot->path_short)
				bot->climb_until_tick = 0;
		}
		else
		{
			bot->path.valid = FALSE;
			bot->path_failures++;
			bots_globals.search_failures++;
			if (getenv("HALO_BOT_PATH_LOG"))
				bots_search_log_failure(bot, search, reached);
		}
		/* port: (a lane's waypoint or a vantage point it finds no way to:
		given up, a new search) */
		if (bots_tactics_search_answer(search->bot_index, &search->goal_point, reached, state->heap_count <= 1))
			bot->path.valid = FALSE;
		/* (an item it finds no way to: another, and not this one a while) */
		if (bot->goal_kind == _bots_goal_item && !reached && !bot->via_teleporter)
		{
			bots_item_set_unreachable(bot, bot->item_index);
			bot->goal_kind = _bots_goal_none;
			bot->path.valid = FALSE;
		}
		bot->path_tick = game_time_get();
		/* port: the teleporters weighed against the walk found (a path's
		search alone) */
		if (bot->path.valid)
			bots_shortcut_weigh(bot, search, reached);
		/* port: the jumps and drops as shortcuts (no teleporter taken) */
		if (bot->path.valid && reached && bot->shortcut_phase == _bots_shortcut_none)
			bots_jump_shortcut(bot, search);
		bots_globals.search_us += vita_host_time_us() - started;
	}
	bot->search_slot = NONE;
	bots_search_set_state(search, _bots_search_idle);
}

/* a search let go, its answer dropped (waiting for it if the helper is on
it: a search takes well under a millisecond) */
static void bots_search_drop(struct bots_search *search)
{
	for (;;)
	{
		pthread_mutex_lock(&bots_helper.lock);
		if (search->state != _bots_search_running)
		{
			search->state = _bots_search_idle;
			pthread_mutex_unlock(&bots_helper.lock);
			return;
		}
		pthread_mutex_unlock(&bots_helper.lock);
		sched_yield();
	}
}

/* ---------- perception */

/* whether the bot sees the point from its eyes (the AI's sight test) */
static boolean bots_line_of_sight(long unit_index, real_point3d const *eye, real_point3d const *point, long target_unit_index)
{
	struct collision_result collision;
	real_vector3d vector;
	unsigned long long started = bots_profile.enabled > 0 ? vita_host_time_us() : 0;
	boolean hit;
	long vehicle_index = bots_unit_vehicle(unit_index);

	vector.i = point->x - eye->x;
	vector.j = point->y - eye->y;
	vector.k = point->z - eye->z;
	/* port: riding, its own vehicle (a Warthog's gun before its gunner's
	eyes) and the riders in it are no wall: the ray ignores the vehicle, and
	with it everything riding it (object_test_vector) */
	hit = collision_test_vector(_collision_test_for_line_of_sight_flags, eye, &vector,
		vehicle_index != NONE ? vehicle_index : unit_index, &collision);
	if (started)
	{
		bots_profile.sight_rays++;
		bots_profile.sight_us += vita_host_time_us() - started;
	}
	if (!hit)
		return TRUE;
	/* (the target's vehicle: seen) */
	if (collision.type == _collision_result_object && target_unit_index != NONE &&
		(collision.object_index == target_unit_index ||
			collision.object_index == object_get(target_unit_index)->object.parent_object_index))
	{
		return TRUE;
	}
	return FALSE;
}

static boolean bots_is_enemy(struct player_datum *player, struct player_datum *other)
{
	if (player == other || other->unit_index == NONE)
		return FALSE;
	if (game_engine_has_teams() && player->team_index == other->team_index)
		return FALSE;
	return TRUE;
}

/* the enemy the bot sees best now, NONE for none: of the enemies in its
sight's reach and view, the one of the least score (nearest, most ahead, the
one it fights favoured) a line of sight reaches, the first in the players'
order of those of that score. The enemies are scored first and their lines
of sight cast from the least score up, until one reaches its enemy, which is
the answer: cast in the players' order, as before, every enemy scoring less
than the best seen so far had its line cast, and the best was the same one
(the lines of sight change nothing in the game but the collision tests'
marker stamps). A score that is not a number (none is: the players'
positions are) is compared as before, in the players' order */
struct bots_target_candidate
{
	long player_index;
	long unit_index;
	real score;
	real_point3d point;
};

/* an enemy's score as a target (its point the bounding sphere's centre);
FALSE for one out of reach or view, or not an enemy */
static boolean bots_target_score(struct bots_bot *bot, struct player_datum *player, struct player_datum *other,
	long player_index, real_point3d const *eye, struct bots_skill_definition const *skill, boolean hurt,
	real sight_scale, struct bots_target_candidate *candidate)
{
	struct object_datum *object;
	struct unit_datum *other_unit;
	real distance, reach, bearing;
	real half_view = 0.5f * skill->field_of_view_degrees * BOTS_DEGREES;

	if (!bots_is_enemy(player, other))
		return FALSE;
	object = object_get(other->unit_index);
	other_unit = unit_get(other->unit_index);
	if (TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		return FALSE;
	candidate->point = object->object.bounding_sphere_center;
	distance = bots_distance3d(eye, &candidate->point);
	/* (port: further through a scope at a vantage point: bots_tactics.h) */
	reach = skill->sight_distance * sight_scale;
	/* (active camouflage: seen only close) */
	if (TEST_FLAG(other_unit->unit.flags, _unit_active_camouflaged_bit))
		reach *= 0.3f;
	/* (the one it fights: a little further) */
	if (player_index == bot->target_player_index)
		reach *= 1.25f;
	if (distance > reach)
		return FALSE;
	bearing = (real)fabs(bots_angle_difference((real)atan2(candidate->point.y - eye->y, candidate->point.x - eye->x),
		bot->yaw));
	/* (out of view: unless close by, it shot the bot, or it is the one
	the bot fights) */
	if (bearing > half_view && distance > 3.0f && !hurt && player_index != bot->target_player_index)
		return FALSE;
	candidate->player_index = player_index;
	candidate->unit_index = other->unit_index;
	candidate->score = distance + bearing * 4.0f - (player_index == bot->target_player_index ? 6.0f : 0.0f);
	/* port: (the enemy a teammate called out just now: the squad fights
	the same one, bots_squads.c) */
	if (bots_squads_focus((long)(bot - bots_globals.bots), player_index))
		candidate->score -= 3.0f;
	return TRUE;
}

static long bots_find_target(struct bots_bot *bot, struct player_datum *player, struct unit_datum *unit,
	real_point3d const *eye)
{
	struct bots_skill_definition const *skill = &bots_skills[bots_globals.skill];
	struct data_iterator iterator;
	struct player_datum *other;
	boolean hurt = game_time_get() - bot->hurt_tick < TICKS_PER_SECOND * 2;
	real sight_scale = bots_tactics_sight_scale((long)(bot - bots_globals.bots));
	struct bots_target_candidate candidates[BOTS_MAXIMUM + MAXIMUM_LOCAL_PLAYERS];
	long candidate_count = 0, index;
	boolean ordered = TRUE;

	(void)unit;
	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL && ordered)
	{
		if (candidate_count >= (long)NUMBEROF(candidates))
			ordered = FALSE;
		else if (bots_target_score(bot, player, other, iterator.datum_index, eye, skill, hurt, sight_scale, &candidates[candidate_count]))
		{
			real score = candidates[candidate_count].score;

			if (score != score)
				ordered = FALSE;
			else if (score < REAL_MAX)
				candidate_count++;
		}
	}
	if (!ordered)
	{
		/* (more enemies than kept, or a score not a number: the players'
		order, each scoring less than the best so far) */
		struct bots_target_candidate candidate;
		long best_index = NONE;
		real best_score = REAL_MAX;

		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (!bots_target_score(bot, player, other, iterator.datum_index, eye, skill, hurt, sight_scale, &candidate) ||
				candidate.score >= best_score ||
				!bots_line_of_sight(player->unit_index, eye, &candidate.point, candidate.unit_index))
			{
				continue;
			}
			best_score = candidate.score;
			best_index = candidate.player_index;
		}
		return best_index;
	}
	while (candidate_count > 0)
	{
		long least = 0;

		/* (the least score, the first of it in the players' order) */
		for (index = 1; index < candidate_count; index++)
		{
			if (candidates[index].score < candidates[least].score)
				least = index;
		}
		if (bots_line_of_sight(player->unit_index, eye, &candidates[least].point, candidates[least].unit_index))
			return candidates[least].player_index;
		/* (out, the others kept in their order) */
		candidate_count--;
		for (index = least; index < candidate_count; index++)
			candidates[index] = candidates[index + 1];
	}
	return NONE;
}

/* ---------- moving */

static void bots_set_goal(struct bots_bot *bot, short kind, real_point3d const *point, long surface_index)
{
	/* (the same goal, near enough: the path stands) */
	if (bot->goal_kind == kind && bot->goal_surface_index == surface_index &&
		bots_distance3d(&bot->goal_point, point) < 1.5f)
	{
		return;
	}
	bot->goal_kind = kind;
	bot->goal_point = *point;
	bot->goal_surface_index = surface_index;
	bot->goal_tick = game_time_get();
	bot->item_powerup = _bots_powerup_none;
	bot->goal_serial++;
	bot->path.valid = FALSE;
	bot->path_failures = 0;
	bot->climb_until_tick = 0;
}

/* port: whether a roaming place is one to go to: not by a goal given up a
while ago, and one the bot can get to (bots_nav.c) */
static boolean bots_roam_point_open(struct bots_bot const *bot, long index, long start_surface_index)
{
	if (game_time_get() < bot->avoid_until_tick && bots_distance3d(&bots_globals.roam_points[index], &bot->avoid_point) < 4.0f)
		return FALSE;
	return bots_nav_reachable(start_surface_index, bots_globals.roam_surfaces[index], game_time_get());
}

/* the next place to roam to: most often towards an enemy (the roaming place
nearest a living enemy, as a player knows where the other team's base is
and reads the motion tracker), else anywhere */
static void bots_pick_roam_goal(struct bots_bot *bot, real_point3d const *position)
{
	long tries;
	long start_surface_index = bots_surface_below(position, NULL);

	if (bots_globals.roam_point_count <= 0)
		return;
	bot->hunting = FALSE;
	if (bots_random_real(bot) < 0.65f)
	{
		struct player_datum *player = player_get(bot->player_index);
		struct data_iterator iterator;
		struct player_datum *other;
		long enemies[BOTS_MAXIMUM + MAXIMUM_LOCAL_PLAYERS];
		long enemy_count = 0;

		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL &&
			enemy_count < (long)NUMBEROF(enemies))
		{
			if (bots_is_enemy(player, other))
				enemies[enemy_count++] = other->unit_index;
		}
		if (enemy_count > 0)
		{
			struct object_datum *enemy = object_get(enemies[bots_random(bot) % (unsigned long)enemy_count]);
			long index, nearest = NONE;
			real nearest_distance = REAL_MAX;

			for (index = 0; index < bots_globals.roam_point_count; index++)
			{
				real distance = bots_distance2d(&bots_globals.roam_points[index], &enemy->object.position) +
					bots_random_real(bot) * 6.0f;

				if (distance < nearest_distance && bots_distance2d(&bots_globals.roam_points[index], position) > 4.0f &&
					bots_roam_point_open(bot, index, start_surface_index))
				{
					nearest_distance = distance;
					nearest = index;
				}
			}
			if (nearest != NONE)
			{
				bot->roam_index = (short)nearest;
				bot->hunting = TRUE;
				bots_set_goal(bot, _bots_goal_roam, &bots_globals.roam_points[nearest], bots_globals.roam_surfaces[nearest]);
				return;
			}
		}
	}
	for (tries = 0; tries < 12; tries++)
	{
		short index = (short)(bots_random(bot) % (unsigned long)bots_globals.roam_point_count);

		/* (not where it is; port: nor where it cannot get) */
		if ((bots_distance2d(&bots_globals.roam_points[index], position) < 6.0f ||
			!bots_roam_point_open(bot, index, start_surface_index)) && tries < 11)
		{
			continue;
		}
		bot->roam_index = index;
		bots_set_goal(bot, _bots_goal_roam, &bots_globals.roam_points[index], bots_globals.roam_surfaces[index]);
		return;
	}
}

/* CTF: where a team's flag is at home, which is also where that team
captures the other's flag: its own stand (the scenario's netgame flag of its
team), but in an assault game (Invasion: ctf.assault) the other team's
stand - the flags change bases (ctf_engine_initialize_for_new_map's flag
slots), so a team takes the flag lying at its own base and runs it to the
other's (the bots took it and stood on the stand they took it from, issue
#40). FALSE for none */
static boolean bots_ctf_stand(long team_index, real_point3d *position)
{
	struct scenario *scenario = global_scenario_get();
	long index;

	if (game_engine_get_variant()->game_engine_index == game_engine_ctf &&
		game_engine_get_variant()->game_engine_variant.ctf.assault)
	{
		team_index = team_index ? 0 : 1;
	}
	for (index = 0; scenario && index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);

		if (flag->type == _netgame_flag_ctf_flag && flag->team_index == team_index)
		{
			*position = flag->position;
			return TRUE;
		}
	}
	return FALSE;
}

/* the flags among the weapons (weapon_is_flag: CTF's flags), in the objects'
order: found by the first bot to ask in a tick, the same for the others;
FALSE when there are more than kept (the weapons are walked then) */
static boolean bots_ctf_flags(void)
{
	if (!bots_globals.flags_found)
	{
		struct object_iterator iterator;

		bots_globals.flags_found = TRUE;
		bots_globals.flags_overflow = FALSE;
		bots_globals.flag_count = 0;
		object_iterator_new(&iterator, _object_mask_weapon, 0);
		while (object_iterator_next(&iterator))
		{
			if (!weapon_is_flag(iterator.index))
				continue;
			if (bots_globals.flag_count >= (long)NUMBEROF(bots_globals.flag_indices))
			{
				bots_globals.flags_overflow = TRUE;
				break;
			}
			bots_globals.flag_indices[bots_globals.flag_count++] = iterator.index;
		}
	}
	return !bots_globals.flags_overflow;
}

/* the next flag (bots_ctf_flags) from *position on, or the next weapon
when they were too many; NONE after the last */
static long bots_ctf_next_flag(struct object_iterator *iterator, long *position)
{
	if (bots_globals.flags_overflow)
		return object_iterator_next(iterator) ? iterator->index : NONE;
	return *position < bots_globals.flag_count ? bots_globals.flag_indices[(*position)++] : NONE;
}

/* port: whether the bot goes along with its team's carrier of the enemy
flag (its unit): its squad partner, or one of the two bots of its team on
foot nearest it (bots_squads.c; all of them without squads) */
static boolean bots_ctf_escorts(struct bots_bot *bot, long carrier_unit_index, real_point3d const *position)
{
	struct object_datum *carrier = object_get(carrier_unit_index);
	struct player_datum *player = player_get(bot->player_index);
	real distance = bots_distance3d(&carrier->object.position, position);
	long index, nearer = 0;

	if (!game_engine_has_teams() || !bots_squads_escorts_limited() ||
		bots_squads_partner_of((long)(bot - bots_globals.bots), unit_get(carrier_unit_index)->unit.player_index))
	{
		return TRUE;
	}
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_bot const *other = &bots_globals.bots[index];
		struct player_datum *other_player;
		struct object_datum *object;

		if (other == bot || other->player_index == NONE)
			continue;
		other_player = player_try_and_get(other->player_index);
		if (!other_player || other_player->unit_index == NONE || other_player->unit_index == carrier_unit_index ||
			other_player->team_index != player->team_index)
		{
			continue;
		}
		object = object_get(other_player->unit_index);
		if (object->object.parent_object_index == NONE && !TEST_FLAG(object->object.damage_flags, _object_dead_bit) &&
			bots_distance3d(&carrier->object.position, &object->object.position) < distance)
		{
			nearer++;
		}
	}
	return nearer < 2;
}

/* CTF's own goals: carrying the enemy flag, its team's stand (bots_ctf_stand:
the HUD's nav point is beside it, and a capture needs the carrier on it),
or, where a capture needs its own flag home (the variant's flag at home to
score; not CTF's or Invasion's own), its own flag's carrier while its own
flag is carried off; its own flag
lying away from home, there (touching it returns it); its own flag carried
off, after the carrier; the enemy flag carried by a teammate, along with the
carrier (not waiting at the empty stand: two teams each holding the other's
flag, where a capture needs the team's own flag home, stood there all game).
TRUE when set */
static boolean bots_ctf_goal(struct bots_bot *bot, struct player_datum *player, real_point3d const *position)
{
	struct unit_datum *unit = unit_get(player->unit_index);
	long weapon_index = bots_current_weapon(unit);
	real_point3d stand, ground;
	long surface_index;
	struct object_iterator iterator;
	long chase_index = NONE, escort_index = NONE;
	long flag_index, flag_position = 0;

	if (game_engine_get_variant()->game_engine_index != game_engine_ctf ||
		!bots_ctf_stand(player->team_index, &stand))
	{
		return FALSE;
	}
	bots_ctf_flags();
	if (weapon_index != NONE && weapon_is_flag(weapon_index))
	{
		/* (its own flag carried off where a capture needs it home - the
		variant's flag at home to score, as ctf_engine_player_update reads
		it: after its carrier; else straight to the capture) */
		struct ctf_variant const *rules = &game_engine_get_variant()->game_engine_variant.ctf;

		object_iterator_new(&iterator, _object_mask_weapon, 0);
		while (rules->flag_at_home_to_score && rules->single_flag_time == 0 &&
			(flag_index = bots_ctf_next_flag(&iterator, &flag_position)) != NONE)
		{
			struct weapon_datum *flag = weapon_get(flag_index);

			if (weapon_is_flag(flag_index) && flag->object.owner_team_index == player->team_index &&
				flag->object.parent_object_index != NONE &&
				object_try_and_get_and_verify_type(flag->object.parent_object_index, _object_mask_unit))
			{
				surface_index = bots_unit_surface(flag->object.parent_object_index, &ground);
				if (surface_index != NONE)
				{
					bots_set_goal(bot, _bots_goal_objective, &ground, surface_index);
					return TRUE;
				}
			}
		}
		surface_index = bots_surface_below(&stand, &ground);
		if (surface_index == NONE)
			return FALSE;
		bots_set_goal(bot, _bots_goal_objective, &stand, surface_index);
		bot->goal_exact = TRUE;
		return TRUE;
	}
	object_iterator_new(&iterator, _object_mask_weapon, 0);
	flag_position = 0;
	while ((flag_index = bots_ctf_next_flag(&iterator, &flag_position)) != NONE)
	{
		struct weapon_datum *flag = weapon_get(flag_index);

		if (!weapon_is_flag(flag_index))
			continue;
		if (flag->object.parent_object_index != NONE)
		{
			struct object_datum *carrier = object_try_and_get_and_verify_type(flag->object.parent_object_index,
				_object_mask_unit);

			if (carrier && carrier != (struct object_datum *)unit)
			{
				if (flag->object.owner_team_index == player->team_index)
					chase_index = flag->object.parent_object_index;
				else
					escort_index = flag->object.parent_object_index;
			}
			continue;
		}
		if (flag->object.owner_team_index != player->team_index ||
			bots_distance3d(&flag->object.position, &stand) < 1.5f ||
			bots_distance3d(&flag->object.position, position) > 40.0f)
		{
			continue;
		}
		surface_index = bots_surface_below(&flag->object.position, &ground);
		if (surface_index == NONE)
			continue;
		bots_set_goal(bot, _bots_goal_objective, &flag->object.position, surface_index);
		bot->goal_exact = TRUE;
		return TRUE;
	}
	/* port: the enemy flag carried by a teammate: its squad partner and the
	two bots nearest it go along; the others to their own stand, where the
	capture is made (bots_squads.c) */
	if (chase_index == NONE && escort_index != NONE && !bots_ctf_escorts(bot, escort_index, position))
	{
		surface_index = bots_surface_below(&stand, &ground);
		if (surface_index == NONE)
			return FALSE;
		bots_set_goal(bot, _bots_goal_objective, &ground, surface_index);
		return TRUE;
	}
	if (chase_index == NONE)
		chase_index = escort_index;
	if (chase_index != NONE)
	{
		surface_index = bots_unit_surface(chase_index, &ground);
		if (surface_index != NONE && bots_distance3d(&ground, position) > 2.0f)
		{
			bots_set_goal(bot, _bots_goal_objective, &ground, surface_index);
			return TRUE;
		}
		/* (beside the carrier: about it, not standing) */
		if (surface_index != NONE)
			return FALSE;
	}
	return FALSE;
}

/* port: vehicles' seats and the bots going for them */

/* the bot (other than `except`) going for the vehicle's seat (NONE: to right
it), NULL for none */
static struct bots_bot *bots_seat_claimer(long vehicle_index, short seat_index, struct bots_bot const *except)
{
	long index;

	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_bot *other = &bots_globals.bots[index];

		if (other != except && other->player_index != NONE && other->unit_index != NONE &&
			other->vehicle_index == vehicle_index && other->vehicle_seat_index == seat_index)
		{
			return other;
		}
	}
	return NULL;
}

/* a teammate of the player drives the vehicle, or a teammate bot goes for
its driver's seat (*bot_driver: a bot, which waits for those getting in) */
static boolean bots_vehicle_friendly_driver(long vehicle_index, struct player_datum *player, boolean *bot_driver)
{
	struct unit_datum *vehicle = unit_get(vehicle_index);
	long player_index = NONE;

	*bot_driver = FALSE;
	if (!game_engine_has_teams())
		return FALSE;
	if (vehicle->unit.driver_object_index != NONE)
		player_index = unit_get(vehicle->unit.driver_object_index)->unit.player_index;
	else
	{
		short seat_index;
		struct unit_definition *definition = unit_definition_get(vehicle->definition_index);

		for (seat_index = 0; seat_index < definition->unit.seats.count && player_index == NONE; seat_index++)
		{
			struct bots_bot *claimer;

			if (bots_seat_kind(vehicle_index, seat_index) == _bots_seat_driver &&
				(claimer = bots_seat_claimer(vehicle_index, seat_index, NULL)) != NULL)
			{
				player_index = claimer->player_index;
			}
		}
	}
	if (player_index == NONE || !player_try_and_get(player_index) || player_get(player_index) == player ||
		player_get(player_index)->team_index != player->team_index)
	{
		return FALSE;
	}
	*bot_driver = bots_player_is_bot(player_index);
	return TRUE;
}

/* port: a vehicle the bot went for let go (its seat free for another bot) */
static void bots_vehicle_forget(struct bots_bot *bot)
{
	bot->vehicle_index = NONE;
	bot->vehicle_seat_index = NONE;
	bot->vehicle_flip = FALSE;
	bot->vehicle_turret = FALSE;
	bot->vehicle_presses = 0;
	if (bot->goal_kind == _bots_goal_vehicle)
		bot->goal_kind = _bots_goal_none;
}

/* port: a vehicle the bot gave up on, not gone for again a while */
static void bots_vehicle_avoid(struct bots_bot *bot, long vehicle_index, long ticks)
{
	bot->vehicle_avoid_index = vehicle_index;
	bot->vehicle_avoid_until_tick = game_time_get() + ticks;
}

/* port: going for a vehicle: the goal its seat's entrance (worked out again
once the vehicle has moved), or the vehicle itself to right it; FALSE when
the vehicle is gone, dead or righted (the bot goes for it no more) */
static boolean bots_vehicle_goal(struct bots_bot *bot, long unit_index)
{
	struct object_datum *vehicle = object_try_and_get_and_verify_type(bot->vehicle_index, _object_mask_vehicle);
	real_point3d point, ground, seat, hint;
	long surface_index;
	boolean driver_is_bot;

	if (!vehicle || TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit))
	{
		bots_vehicle_forget(bot);
		return FALSE;
	}
	if (bot->vehicle_flip && vehicle->object.up.k > 0.5f)
	{
		/* (righted: a seat of it looked for at once) */
		bots_globals.vehicle_flips++;
		bots_vehicle_forget(bot);
		bot->vehicle_check_tick = 0;
		return FALSE;
	}
	/* (its seat taken meanwhile, by a player say, or a Warthog's driver gone
	from beside the seat it went for: another looked for at once) */
	if (!bot->vehicle_flip && (bots_seat_occupant(bot->vehicle_index, bot->vehicle_seat_index) != NONE ||
		(bots_seat_kind(bot->vehicle_index, bot->vehicle_seat_index) != _bots_seat_driver && !bot->vehicle_turret &&
			!bots_vehicle_friendly_driver(bot->vehicle_index, player_get(bot->player_index), &driver_is_bot))))
	{
		bots_vehicle_forget(bot);
		bot->vehicle_check_tick = 0;
		return FALSE;
	}
	if (bot->goal_kind == _bots_goal_vehicle && bots_distance3d(&vehicle->object.position, &bot->vehicle_entrance_from) < 0.5f)
		return TRUE;
	if (bot->vehicle_flip)
		bot->vehicle_entrance = vehicle->object.position;
	else if (!unit_get_seat_entrance_point(unit_index, bot->vehicle_index, bot->vehicle_seat_index, &bot->vehicle_entrance,
		&seat, &hint))
	{
		bots_vehicle_forget(bot);
		return FALSE;
	}
	bot->vehicle_entrance_from = vehicle->object.position;
	point = bot->vehicle_entrance;
	surface_index = bots_surface_below(&point, &ground);
	if (surface_index == NONE)
	{
		bots_vehicle_avoid(bot, bot->vehicle_index, TICKS_PER_SECOND * 20);
		bots_vehicle_forget(bot);
		return FALSE;
	}
	bots_set_goal(bot, _bots_goal_vehicle, &ground, surface_index);
	return TRUE;
}

/* port: a powerup to go for - a health pack when the bot's body is hurt
(below its skill's mark: lower in a fight, where only the less hurt of the
skilled leave it for one near; never for an easy bot in a fight), the most
hurt going furthest; an overshield or camouflage near (nearer in a fight,
and in a game with an objective) - the best of those lying on the map
(bots_powerups), on ground below it, that no other bot nearer to it goes
for, nor one it found no way to. The one it goes for kept while it lies
there, the bot still wants it (a body not whole for a health pack) and its
time is not up (the walk there and a little); one it stood at two seconds
and did not get (its body whole by then, a pack another took first) left
be a while, as one it finds no way to (bots_search_take). TRUE while it
goes for one */
static boolean bots_powerup_goal(struct bots_bot *bot, struct unit_datum *unit, real_point3d const *position, boolean fighting)
{
	struct bots_skill_definition const *skill = &bots_skills[bots_globals.skill];
	long now = game_time_get();
	real body = unit->object.body_vitality;
	real heal_below, best_score = REAL_MAX;
	boolean objective_game, holding;
	long index, best = NONE;
	real_point3d ground;
	long surface_index;

	/* (the goal changed under it - a path's end short of it, three times
	stuck, a teleporter's way that went nowhere - the powerup left be a
	while, as one it found no way to: it was gone for every second before) */
	if (bot->powerup_index != NONE &&
		(bot->goal_kind != _bots_goal_item || bot->item_index != bot->powerup_index || bot->item_powerup == _bots_powerup_none))
	{
		struct object_datum *item = object_try_and_get_and_verify_type(bot->powerup_index, _object_mask_equipment);

		if (item && item->object.parent_object_index == NONE && bots_distance3d(&item->object.position, position) > 1.5f)
			bots_item_set_unreachable(bot, bot->powerup_index);
		bot->powerup_index = NONE;
	}
	if (bot->goal_kind == _bots_goal_item && bot->item_powerup != _bots_powerup_none)
	{
		struct item_datum *item = (struct item_datum *)object_try_and_get_and_verify_type(bot->item_index, _object_mask_equipment);
		real distance;

		if (!item || item->object.parent_object_index != NONE)
		{
			/* (gone as the bot stood at it: taken) */
			if (bot->item_last_distance < 1.5f)
				bots_globals.powerups_taken[bot->item_powerup]++;
			if (getenv("HALO_BOT_TRACE"))
				platform_log("bots: trace bot %ld powerup %d gone, %.1f away, body %.2f", (long)(bot - bots_globals.bots) + 1,
					(int)bot->item_powerup, bot->item_last_distance, body);
			bot->goal_kind = _bots_goal_none;
			bot->item_powerup = _bots_powerup_none;
			return FALSE;
		}
		distance = bots_distance3d(&item->object.position, position);
		bot->item_last_distance = distance;
		if (distance < 0.8f && !bot->item_arrived_tick)
			bot->item_arrived_tick = now;
		if (now < bot->item_deadline_tick && (!bot->item_arrived_tick || now - bot->item_arrived_tick < TICKS_PER_SECOND * 2) &&
			(bot->item_powerup != _bots_powerup_health || body < 1.0f))
		{
			return TRUE;
		}
		/* (a health pack's body whole as it stood at it: taken, the pack
		gone the tick after) */
		if (bot->item_powerup == _bots_powerup_health && body >= 1.0f && distance < 1.5f)
		{
			bots_globals.powerups_taken[_bots_powerup_health]++;
			bot->goal_kind = _bots_goal_none;
			bot->item_powerup = _bots_powerup_none;
			return FALSE;
		}
		if (getenv("HALO_BOT_TRACE"))
			platform_log("bots: trace bot %ld powerup %d given up, %.1f away, body %.2f, %s", (long)(bot - bots_globals.bots) + 1,
				(int)bot->item_powerup, distance, body, now >= bot->item_deadline_tick ? "late" :
				bot->item_arrived_tick ? "stood at it" : "whole");
		bots_item_set_unreachable(bot, bot->item_index);
		bot->goal_kind = _bots_goal_none;
		bot->item_powerup = _bots_powerup_none;
		return FALSE;
	}
	if (now < bot->powerup_check_tick || bots_powerups.count <= 0)
		return FALSE;
	bot->powerup_check_tick = now + TICKS_PER_SECOND;
	heal_below = fighting ? skill->heal_below_fighting : skill->heal_below;
	objective_game = game_engine_get_variant()->game_engine_index != game_engine_slayer;
	/* port: (holding a vantage point, an overshield or camouflage only
	beside it: bots_tactics.h) */
	holding = bots_tactics_holding((long)(bot - bots_globals.bots));
	for (index = 0; index < bots_powerups.count; index++)
	{
		long object_index = bots_powerups.items[index].object_index;
		short kind = bots_powerups.items[index].kind;
		real distance = bots_distance3d(&bots_powerups.items[index].position, position);
		real reach, score;
		long other_index;

		if (bots_item_unreachable(bot, object_index))
			continue;
		/* port: (where the bot cannot get: Wizard's middle, Damnation's
		ledges, before their jumps were known) */
		if (!bots_nav_reachable(bots_surface_below(position, NULL), bots_powerups.items[index].surface_index, now))
			continue;
		/* (taken since the list was made) */
		if (!object_try_and_get_and_verify_type(object_index, _object_mask_equipment) ||
			object_get(object_index)->object.parent_object_index != NONE)
		{
			continue;
		}
		if (kind == _bots_powerup_health)
		{
			if (body >= heal_below)
				continue;
			/* (the more hurt, the further: 15 units in a fight, 45 out of
			one, scored nearer by how hurt it is) */
			reach = fighting ? 15.0f : 45.0f;
			score = distance - (heal_below - body) * 40.0f;
		}
		else
		{
			reach = fighting ? 10.0f : objective_game ? MIN(skill->powerup_reach, 12.0f) : skill->powerup_reach;
			if (holding)
				reach = MIN(reach, 6.0f);
			score = distance - 6.0f;
		}
		if (distance > reach || score >= best_score)
			continue;
		/* (another bot going for it from nearer: its) */
		for (other_index = 0; other_index < BOTS_MAXIMUM; other_index++)
		{
			struct bots_bot const *other = &bots_globals.bots[other_index];

			if (other != bot && other->player_index != NONE && other->goal_kind == _bots_goal_item &&
				other->item_index == object_index && other->item_last_distance < distance)
			{
				break;
			}
		}
		if (other_index < BOTS_MAXIMUM)
			continue;
		best_score = score;
		best = index;
	}
	if (best == NONE)
		return FALSE;
	surface_index = bots_surface_below(&bots_powerups.items[best].position, &ground);
	if (surface_index == NONE)
		return FALSE;
	bots_set_goal(bot, _bots_goal_item, &ground, surface_index);
	bot->item_index = bots_powerups.items[best].object_index;
	bot->item_powerup = bots_powerups.items[best].kind;
	bot->powerup_index = bot->item_index;
	bot->item_last_distance = bots_distance3d(&bots_powerups.items[best].position, position);
	bot->item_arrived_tick = 0;
	bots_globals.powerups_sought[bot->item_powerup]++;
	if (getenv("HALO_BOT_TRACE"))
		platform_log("bots: trace bot %ld goes for powerup %d at (%.1f %.1f %.1f), %.1f away, body %.2f%s",
			(long)(bot - bots_globals.bots) + 1, (int)bot->item_powerup, bots_powerups.items[best].position.x,
			bots_powerups.items[best].position.y, bots_powerups.items[best].position.z, bot->item_last_distance, body,
			fighting ? ", fighting" : "");
	/* (the walk there - two units a second, the way half as long again -
	and ten seconds more) */
	bot->item_deadline_tick = now + TICKS_PER_SECOND * 10 + (long)(bot->item_last_distance * 0.75f * TICKS_PER_SECOND);
	return TRUE;
}

/* where the bot goes: the game's nav points for it, the enemy last seen, or
about the map (port: or a vehicle it goes for, or a powerup) */
static void bots_choose_goal(struct bots_bot *bot, long player_index, real_point3d const *position, boolean fighting)
{
	real_point3d goals[8];
	long goal_count;
	long now = game_time_get();

	bot->goal_exact = FALSE;
	/* port: off its vantage point when it is time, whatever it goes for
	(bots_tactics.h) */
	bots_tactics_check((long)(bot - bots_globals.bots), player_index);
	if (bot->vehicle_index != NONE && bots_vehicle_goal(bot, player_get(player_index)->unit_index))
		return;
	/* port: an Infection zombie goes for the nearest human, wherever it is
	(as Halo 3's zombies know where the humans are), no item, powerup or
	vehicle on the way: the skull is all it holds (infection.c) */
	if (infection_player_is_zombie(player_index))
	{
		real_point3d human, ground;
		long surface_index;

		if (infection_bot_hunt(player_index, position, &human) &&
			(surface_index = bots_surface_below(&human, &ground)) != NONE)
		{
			/* (the path kept while the human is near where it went, three
			seconds at most: a search a tick for a running human was many) */
			if (bot->goal_kind == _bots_goal_enemy && bots_distance3d(&bot->goal_point, &ground) < 5.0f &&
				now - bot->goal_tick < TICKS_PER_SECOND * 3)
			{
				return;
			}
			bots_set_goal(bot, _bots_goal_enemy, &ground, surface_index);
			bot->hunting = TRUE;
			return;
		}
	}
	/* port: a health pack when hurt, an overshield or camouflage near: before
	the game's goals, but for the flag or the ball it carries */
	if (!bot->held_objective && bots_powerup_goal(bot, unit_get(player_get(player_index)->unit_index), position, fighting))
		return;
	/* (a while from the game's goals, which it found no way to) */
	if (now < bot->objective_rest_until_tick)
		goal_count = 0;
	else
	{
		real_point3d post;
		long post_surface_index;

		if (bots_ctf_goal(bot, player_get(player_index), position))
		{
			/* port: (off its vantage point: the flag first) */
			bots_tactics_objective((long)(bot - bots_globals.bots));
			return;
		}
		/* port: a roamer: a teammate's call near, or the squad's leader
		away (bots_squads.c): there, as to an enemy last seen */
		if (!bot->held_objective && now >= bot->hunt_rest_until_tick &&
			(bots_squads_help_goal((long)(bot - bots_globals.bots), position, &post, &post_surface_index) ||
			bots_squads_follow_goal((long)(bot - bots_globals.bots), position, &post, &post_surface_index)))
		{
			bots_set_goal(bot, _bots_goal_enemy, &post, post_surface_index);
			return;
		}
		/* port: a vantage point to hold, a defender's post (bots_tactics.h:
		the bot's role, its weapon); the ball carried, off it */
		if (bot->held_objective)
			bots_tactics_objective((long)(bot - bots_globals.bots));
		else if (bots_tactics_goal((long)(bot - bots_globals.bots), player_index, position, &post, &post_surface_index))
		{
			bots_set_goal(bot, _bots_goal_objective, &post, post_surface_index);
			return;
		}
		goal_count = game_engine_port_player_goals(player_index, goals, NUMBEROF(goals));
	}
	if (goal_count > 0)
	{
		long index, nearest = 0;
		real nearest_distance = REAL_MAX;
		real_point3d ground;
		long surface_index;

		for (index = 0; index < goal_count; index++)
		{
			real distance = bots_distance3d(&goals[index], position);

			if (distance < nearest_distance)
			{
				nearest_distance = distance;
				nearest = index;
			}
		}
		/* port: CTF, the enemy flag at home: held for its squad partner
		coming up behind (bots_squads.c) */
		if (bots_squads_pace_goal((long)(bot - bots_globals.bots), position, &goals[nearest], &ground, &surface_index))
		{
			bots_set_goal(bot, _bots_goal_objective, &ground, surface_index);
			return;
		}
		/* (a nav point floats above its place) */
		surface_index = bots_surface_below(&goals[nearest], &ground);
		if (surface_index != NONE)
		{
			bots_set_goal(bot, _bots_goal_objective, &ground, surface_index);
			/* (a hill is to be stood in, not beside: Chiron TL34's is a
			metre across, and a bot a metre from its middle held nothing) */
			bot->goal_exact = game_engine_get_variant()->game_engine_index == game_engine_king;
			return;
		}
	}
	if (!fighting && bot->target_player_index != NONE && bot->target_last_surface_index != NONE &&
		now - bot->target_seen_tick < TICKS_PER_SECOND * 8 && now >= bot->hunt_rest_until_tick)
	{
		bots_set_goal(bot, _bots_goal_enemy, &bot->target_last_position, bot->target_last_surface_index);
		return;
	}
	/* a better weapon or a powerup near (looked for once a second; given up
	after a while) */
	if (bot->goal_kind == _bots_goal_item)
	{
		struct item_datum *item = (struct item_datum *)object_try_and_get_and_verify_type(bot->item_index,
			_object_mask_weapon | _object_mask_equipment);

		if (item && item->object.parent_object_index == NONE && now - bot->goal_tick < TICKS_PER_SECOND * 15 &&
			(bot->item_first_press_tick < bot->goal_tick || now - bot->item_first_press_tick < TICKS_PER_SECOND * 3))
		{
			return;
		}
		/* (one it did not get, standing on it or not: left be a while, Rat
		Race's team's bots stood on one a whole game) */
		if (item && item->object.parent_object_index == NONE)
		{
			bots_item_set_unreachable(bot, bot->item_index);
		}
		bot->goal_kind = _bots_goal_none;
		bot->item_check_tick = now + TICKS_PER_SECOND * 2;
	}
	if (now >= bot->item_check_tick)
	{
		long item_index = bots_find_item(bot, unit_get(player_get(player_index)->unit_index), position, fighting ? 8.0f : 20.0f);

		bot->item_check_tick = now + TICKS_PER_SECOND;
		if (item_index != NONE)
		{
			real_point3d ground;
			long surface_index = bots_surface_below(&object_get(item_index)->object.position, &ground);

			if (surface_index != NONE)
			{
				bots_set_goal(bot, _bots_goal_item, &ground, surface_index);
				bot->item_index = item_index;
				return;
			}
		}
	}
	if (bot->goal_kind != _bots_goal_roam || bots_distance2d(&bot->goal_point, position) < 1.5f ||
		now - bot->goal_tick > TICKS_PER_SECOND * (bot->hunting ? 12 : 40))
	{
		bots_pick_roam_goal(bot, position);
	}
}

/* port: whether a bot whose path stopped short goes on in a straight line,
jumping: its goal in its own region (a search out of nodes), or where the
regions are not known */
static boolean bots_climb_allowed(struct bots_bot *bot, long unit_index)
{
	real_point3d ground;
	long surface_index = bots_unit_surface(unit_index, &ground);
	long region = bots_nav_region(surface_index), goal_region = bots_nav_region(bot->goal_surface_index);

	return region == NONE || goal_region == NONE || region == goal_region;
}

/* port: a hop begun at its entrance: a teleporter or a ladder handed to
their way (bots_follow_path's via), a jump taken here */
static void bots_hop_begin(struct bots_bot *bot, long now)
{
	struct bots_nav_hop const *hop = bots_nav_hop_get(bot->hop_index);

	if (hop->kind == _bots_hop_teleporter || hop->kind == _bots_hop_ladder)
	{
		bot->via_teleporter = TRUE;
		bot->via_point = hop->entrance;
		bot->via_tick = now;
		bot->via_index = hop->teleporter_index;
		bot->via_climb_tick = 0;
		bot->via_top_tick = 0;
		if (hop->kind == _bots_hop_ladder)
			bots_globals.ladder_tries++;
		bot->hop_via = bot->hop_index;
		bot->hop_index = NONE;
		return;
	}
	bot->hop_take_tick = now;
	bot->hop_jump_tick = 0;
}

/* port: the hop ended: where the bot came out told (its region the hop's
landing's: taken), a new path from there */
static void bots_hop_end(struct bots_bot *bot, long hop_index, long unit_index, long now)
{
	struct bots_nav_hop const *hop = bots_nav_hop_get(hop_index);
	real_point3d ground;
	long surface_index = bots_unit_surface(unit_index, &ground);
	boolean taken = hop && bots_nav_region(surface_index) != NONE &&
		bots_nav_region(surface_index) == bots_nav_region(hop->exit_surface_index);

	bots_nav_hop_result(hop_index, taken, now);
	if (getenv("HALO_BOT_TRACE") && hop)
		platform_log("bots: trace bot %ld hop %ld (kind %d) %s at (%.1f %.1f %.1f), landing (%.1f %.1f %.1f)",
			(long)(bot - bots_globals.bots) + 1, hop_index, (int)hop->kind, taken ? "taken" : "failed", ground.x, ground.y,
			ground.z, hop->exit.x, hop->exit.y, hop->exit.z);
	bot->hop_index = NONE;
	bot->hop_take_tick = 0;
	bot->path.valid = FALSE;
	bot->path_tick = now - TICKS_PER_SECOND;
	bot->stuck_position = ground;
	bot->stuck_tick = now;
}

/* port: taking a jump hop: on towards its landing; at the edge the jump
(an up jump, a gap, a railing first; the feet tucked in the air: crouched
from the jump's rise, as a player crouch-jumps onto a ledge), a drop walked
off; landed, or three seconds on, the hop ended. TRUE while under way */
static boolean bots_hop_take(struct bots_bot *bot, long unit_index, real_point3d const *position, real *direction)
{
	long now = game_time_get();
	struct bots_nav_hop const *hop = bots_nav_hop_get(bot->hop_index);
	struct biped_datum *biped = biped_try_and_get(unit_index);
	boolean airborne = biped && TEST_FLAG(biped->biped.flags, _biped_airborne_bit);
	real to_edge;

	if (!hop)
	{
		bot->hop_index = NONE;
		return FALSE;
	}
	to_edge = bots_distance2d(&hop->edge, position);
	*direction = (real)atan2(hop->exit.y - position->y, hop->exit.x - position->x);
	if (!bot->hop_jump_tick)
	{
		/* (at the edge, or a moment on: up; a drop: walked off, or hopped
		over its railing) */
		if (hop->needs_jump && (to_edge < 0.3f || now - bot->hop_take_tick > TICKS_PER_SECOND / 2) && !airborne)
		{
			bot->hop_press_jump = TRUE;
			bot->hop_jump_tick = now;
			if (hop->kind == _bots_hop_jump_up || bots_distance2d(&hop->edge, &hop->exit) > 0.8f)
			{
				bot->crouch_from_tick = now + 3;
				bot->crouch_until_tick = now + 18;
			}
		}
		else if (!hop->needs_jump && (airborne || bots_distance2d(&hop->entrance, position) > bots_distance2d(&hop->entrance, &hop->edge) + 0.2f))
			bot->hop_jump_tick = now;
	}
	if (bot->hop_jump_tick && now - bot->hop_jump_tick > 5 && !airborne)
	{
		bots_hop_end(bot, bot->hop_index, unit_index, now);
		return FALSE;
	}
	if (now - bot->hop_take_tick > TICKS_PER_SECOND * 3)
	{
		bots_hop_end(bot, bot->hop_index, unit_index, now);
		return FALSE;
	}
	return TRUE;
}

/* the direction the bot walks (radians), following its path; FALSE when
it has nowhere to go */
static boolean bots_follow_path(struct bots_bot *bot, long unit_index, real_point3d const *position, real *direction)
{
	long now = game_time_get();

	if (bot->goal_kind == _bots_goal_none)
		return FALSE;
	/* port: at its lane's waypoint (bots_routes.c): a new path, on to the
	goal (the search asked for the waypoint dropped) */
	if (bots_routes_waypoint_reached((long)(bot - bots_globals.bots), position))
	{
		bot->goal_serial++;
		bot->path.valid = FALSE;
		bot->path_tick = now - TICKS_PER_SECOND;
	}
	if (now < bot->escape_until_tick)
	{
		*direction = bot->escape_direction;
		return TRUE;
	}
	/* port: taking a hop (bots_nav.c) */
	if (bot->hop_index != NONE && bot->hop_take_tick && bots_hop_take(bot, unit_index, position, direction))
		return TRUE;
	/* (at the end of a path short of its goal: straight on, jumping, while
	it comes nearer (the walkable surfaces miss ramps a biped walks up:
	Prisoner's, Wizard's); then the game's goals left a while) */
	if (bot->climb_until_tick)
	{
		real distance = bots_distance3d(&bot->goal_point, position);

		if (distance < bot->climb_best_distance - 0.5f && now - bot->climb_start_tick < TICKS_PER_SECOND * 20)
		{
			bot->climb_best_distance = distance;
			bot->climb_until_tick = now + TICKS_PER_SECOND * 3;
		}
		if (now < bot->climb_until_tick && distance > 1.0f)
		{
			*direction = (real)atan2(bot->goal_point.y - position->y, bot->goal_point.x - position->x);
			return TRUE;
		}
		bot->climb_until_tick = 0;
		if (distance > 1.0f)
		{
			if (bot->goal_kind == _bots_goal_objective)
				bot->objective_rest_until_tick = now + TICKS_PER_SECOND * 10;
			bot->goal_kind = _bots_goal_none;
			return FALSE;
		}
	}
	/* port: (at a ladder's foot: up it, facing it, until it is up to the
	top's height - a biped walks up a climbable surface it faces, bipeds.c
	- then on to the landing beyond, five and two seconds at most; a new
	path from there. Once at its foot, no new search: one's answer started
	the climb again from the foot) */
	if (bot->via_teleporter && bot->via_index >= 0 && bot->via_index < bots_globals.teleporter_count &&
		bots_globals.teleporters[bot->via_index].ladder && (bot->via_climb_tick || !bot->path.valid))
	{
		long ladder = bot->via_index;

		if (!bot->via_climb_tick && now - bot->via_tick < TICKS_PER_SECOND * 6 &&
			bots_distance2d(&bot->via_point, position) > 0.3f)
		{
			*direction = (real)atan2(bot->via_point.y - position->y, bot->via_point.x - position->x);
			return TRUE;
		}
		if (!bot->via_climb_tick)
			bot->via_climb_tick = now;
		if (!bot->via_top_tick && position->z < bots_globals.teleporters[ladder].top_z - 0.25f &&
			now - bot->via_climb_tick < TICKS_PER_SECOND * 8)
		{
			*direction = bots_globals.teleporters[ladder].climb_direction;
			/* (a jump onto it, now and then: walked into, it holds a
			biped at its foot) */
			if (!((now - bot->via_climb_tick) % 15))
				bot->ladder_jump = TRUE;
			return TRUE;
		}
		if (!bot->via_top_tick)
		{
			bot->via_top_tick = now;
			/* port: (turned at once to the landing, a player's flick: walked
			backwards off a ladder, a biped barely moves and falls back) */
			bot->yaw = (real)atan2(bots_globals.teleporters[ladder].exit.y - position->y,
				bots_globals.teleporters[ladder].exit.x - position->x);
			if (position->z >= bots_globals.teleporters[ladder].top_z - 0.25f)
			{
				bots_globals.ladder_climbs++;
				bots_globals.teleporters[ladder].climbs++;
			}
			else
				bots_globals.teleporters[ladder].failures++;
			if (getenv("HALO_BOT_TRACE"))
				platform_log("bots: trace bot %ld off the ladder at (%.1f %.1f %.1f), top %.1f, after %.1f s", (long)(bot - bots_globals.bots) + 1,
					position->x, position->y, position->z, bots_globals.teleporters[ladder].top_z,
					(real)(now - bot->via_climb_tick) / TICKS_PER_SECOND);
		}
		/* port: (a hop onto the landing, once turned to it) */
		if (now - bot->via_top_tick == 3)
			bot->ladder_jump = TRUE;
		if (now - bot->via_top_tick < TICKS_PER_SECOND * 2 &&
			bots_distance2d(&bots_globals.teleporters[ladder].exit, position) > 0.4f &&
			position->z >= bots_globals.teleporters[ladder].top_z - 0.5f)
		{
			*direction = (real)atan2(bots_globals.teleporters[ladder].exit.y - position->y,
				bots_globals.teleporters[ladder].exit.x - position->x);
			return TRUE;
		}
		bot->via_teleporter = FALSE;
		bot->path.valid = FALSE;
		bot->path_tick = now - TICKS_PER_SECOND;
		bots_search_ask(bot, unit_index);
	}
	/* (a new path now and then: the objects about move, and the goal) */
	if ((!bot->path.valid || now - bot->path_tick > TICKS_PER_SECOND * 4) &&
		now - bot->path_tick > TICKS_PER_SECOND / 3)
	{
		bots_search_ask(bot, unit_index);
	}
	if (bot->path.valid)
	{
		while (bot->path.step_index < bot->path.step_count)
		{
			struct path_step *step = &bot->path.steps[(long)bot->path.step_index];

			if (bots_distance2d(&step->point, position) > BOTS_STEP_REACHED)
				break;
			bot->path.step_index++;
		}
		if (bot->path.step_index >= bot->path.step_count)
		{
			bot->path.valid = FALSE;
			/* port: (at a hop's entrance: taken from next tick) */
			if (bot->hop_index != NONE && bot->hop_serial == bot->goal_serial && !bot->hop_take_tick &&
				bots_distance2d(&bots_nav_hop_get(bot->hop_index)->entrance, position) < 1.0f)
			{
				bots_hop_begin(bot, now);
				return bots_hop_take(bot, unit_index, position, direction);
			}
			/* (as near as the search came, the goal on above or beyond:
			on towards it; port: not when the goal is in another region,
			which a straight line jumping never reached: the wall between
			was most of the bots' stuck moments) */
			if (bot->path.steps_finish_path && bot->path_short && bots_distance3d(&bot->goal_point, position) > 1.5f &&
				!bot->climb_until_tick && bots_climb_allowed(bot, unit_index))
			{
				bot->climb_start_tick = now;
				bot->climb_until_tick = now + TICKS_PER_SECOND * 3;
				bot->climb_best_distance = bots_distance3d(&bot->goal_point, position);
				*direction = (real)atan2(bot->goal_point.y - position->y, bot->goal_point.x - position->x);
				return TRUE;
			}
			/* (the whole way walked: there, or at the teleporter) */
			if (bot->path.steps_finish_path && !bot->via_teleporter)
				return FALSE;
			if (!bot->via_teleporter)
				bots_search_ask(bot, unit_index);
		}
		else
		{
			struct path_step *step = &bot->path.steps[(long)bot->path.step_index];

			*direction = (real)atan2(step->point.y - position->y, step->point.x - position->x);
			return TRUE;
		}
	}
	/* (at the teleporter: onto it, a few seconds at most; port: a ladder's
	foot, up it next tick, above) */
	if (bot->via_teleporter && !bots_globals.teleporters[bot->via_index].ladder)
	{
		if (now - bot->via_tick < TICKS_PER_SECOND * 4 && bots_distance2d(&bot->via_point, position) > 0.1f)
		{
			*direction = (real)atan2(bot->via_point.y - position->y, bot->via_point.x - position->x);
			return TRUE;
		}
		bot->via_teleporter = FALSE;
		bots_search_ask(bot, unit_index);
	}
	/* port: (a hop's entrance: on to it, and taken there) */
	if (bot->hop_index != NONE && bot->hop_serial == bot->goal_serial && !bot->hop_take_tick)
	{
		struct bots_nav_hop const *hop = bots_nav_hop_get(bot->hop_index);

		if (bots_distance2d(&hop->entrance, position) < 0.6f)
		{
			bots_hop_begin(bot, now);
			if (bot->hop_index != NONE)
				return bots_hop_take(bot, unit_index, position, direction);
		}
		else if (bots_distance2d(&hop->entrance, position) < 4.0f)
		{
			*direction = (real)atan2(hop->entrance.y - position->y, hop->entrance.x - position->x);
			return TRUE;
		}
	}
	/* (no path yet, or none found: straight there) */
	if (bots_distance2d(&bot->goal_point, position) < 1.0f)
		return FALSE;
	*direction = (real)atan2(bot->goal_point.y - position->y, bot->goal_point.x - position->x);
	return TRUE;
}

/* ---------- a bot's tick */

static void bots_bot_reset(struct bots_bot *bot, long player_index, long bot_index)
{
	long serial = bot->goal_serial;

	if (bot->search_slot != NONE && bots_globals.search_slots)
		bots_search_drop(&bots_globals.search_slots[bot->search_slot]);
	csmemset(bot, 0, sizeof(*bot));
	bot->search_slot = NONE;
	bot->player_index = player_index;
	bot->unit_index = NONE;
	bot->target_player_index = NONE;
	bot->target_last_surface_index = NONE;
	bot->goal_surface_index = NONE;
	bot->goal_serial = serial + 1;
	bot->random = 0x2545F491UL * (unsigned long)(bot_index + 1) ^ (unsigned long)network_game_get_random_seed();
	bot->strafe_sign = 1.0f;
	bot->vehicle_index = NONE;
	bot->vehicle_seat_index = NONE;
	bot->vehicle_avoid_index = NONE;
	bot->ride_vehicle_index = NONE;
	bot->powerup_index = NONE;
	bot->hop_index = NONE;
	bot->hop_via = NONE;
	bots_tactics_bot_reset(bot_index, player_index);
}

/* a bot's tick: what it knows of itself, and the controls it builds */
struct bots_tick
{
	struct bots_skill_definition const *skill;
	struct player_datum *player;
	struct unit_datum *unit;
	struct object_datum *object;
	long now;
	/* the unit's eyes (its head marker), found when first needed: the look
	(every third tick) and the fight (bots_tick_eye) */
	boolean eye_found;
	real_point3d eye;
	real_point3d position;
	long weapon_index;
	/* the controls: buttons, where it walks, how hard */
	unsigned long flags;
	boolean moving;
	real move_direction;
	real throttle_scale;
	/* it has a target in sight, this far away */
	boolean fighting;
	real target_distance;
	struct player_action *action;
};

/* the unit's eyes: its head marker, as unit_get_head_position finds it
(the marker's matrix from the object's node matrices, which nothing in the
bot's tick changes, so it is the same found now or at the tick's start) */
static real_point3d const *bots_tick_eye(struct bots_tick *tick)
{
	if (!tick->eye_found)
	{
		unit_get_head_position(tick->player->unit_index, &tick->eye);
		tick->eye_found = TRUE;
	}
	return &tick->eye;
}

/* looking for enemies (every third tick, the bots in turn; (debug)
HALO_BOT_PEACEFUL=1: never, for the moving alone), and forgetting the one
gone a while */
static void bots_bot_look(struct bots_bot *bot, struct bots_tick *tick)
{
	if (tick->now >= bot->next_look_tick && !bots_peaceful_player(bot->player_index))
	{
		long target = bots_find_target(bot, tick->player, tick->unit, bots_tick_eye(tick));

		bot->next_look_tick = tick->now + 3;
		if (target != NONE)
		{
			struct player_datum *target_player = player_get(target);
			struct object_datum *target_object = object_get(target_player->unit_index);
			real_point3d ground;
			long surface_index;

			/* (a new target: the aim's error as it first turns to it) */
			if (target != bot->target_player_index || tick->now - bot->target_seen_tick > TICKS_PER_SECOND * 2)
			{
				real error = tick->skill->aim_error_degrees * BOTS_DEGREES;

				bot->target_first_seen_tick = tick->now;
				bot->aim_error_yaw = bots_random_signed(bot) * error;
				bot->aim_error_pitch = bots_random_signed(bot) * error * 0.6f;
			}
			bot->target_player_index = target;
			bot->target_seen_tick = tick->now;
			bot->target_last_position = target_object->object.position;
			surface_index = bots_unit_surface(target_player->unit_index, &ground);
			if (surface_index != NONE)
				bot->target_last_surface_index = surface_index;
		}
	}
	if (bot->target_player_index != NONE)
	{
		struct player_datum *target_player = player_try_and_get(bot->target_player_index);

		if (!target_player || target_player->unit_index == NONE ||
			tick->now - bot->target_seen_tick > TICKS_PER_SECOND * 10)
		{
			bot->target_player_index = NONE;
		}
	}
}

/* fighting the target seen in the last half second: aim (turning at its
speed, leading, an error that settles), fire, a grenade, a blow, strafing */
static void bots_bot_fight(struct bots_bot *bot, struct bots_tick *tick)
{
	struct bots_skill_definition const *skill = tick->skill;
	struct player_datum *target_player = player_get(bot->target_player_index);
	struct object_datum *target_object = object_get(target_player->unit_index);
	real_point3d aim_point = target_object->object.bounding_sphere_center;
	long reaction_ticks = (long)(skill->reaction_seconds * TICKS_PER_SECOND);
	long now = tick->now;
	real desired_yaw, desired_pitch, dx, dy, dz, horizontal, turn, yaw_off, pitch_off, cone;
	real settle = (real)exp(-1.0f / (skill->aim_settle_seconds * TICKS_PER_SECOND) * 1.1f);

	tick->fighting = TRUE;
	bots_tick_eye(tick);
	tick->target_distance = bots_distance3d(&tick->eye, &aim_point);
	/* (leading a moving target by the projectile's flight) */
	if (tick->weapon_index != NONE && skill->lead > 0.0f)
	{
		real speed = bots_projectile_speed(tick->weapon_index);

		if (speed > 0.01f)
		{
			real ticks = MIN(tick->target_distance / speed, 30.0f);

			aim_point.x += target_object->object.translational_velocity.i * ticks * skill->lead;
			aim_point.y += target_object->object.translational_velocity.j * ticks * skill->lead;
			aim_point.z += target_object->object.translational_velocity.k * ticks * skill->lead;
		}
	}
	dx = aim_point.x - tick->eye.x;
	dy = aim_point.y - tick->eye.y;
	dz = aim_point.z - tick->eye.z;
	horizontal = (real)sqrt(dx * dx + dy * dy);
	/* the aim error settles while the target stays in sight, with a tremble */
	bot->aim_error_yaw = bot->aim_error_yaw * settle +
		bots_random_signed(bot) * skill->aim_error_degrees * 0.04f * BOTS_DEGREES;
	bot->aim_error_pitch = bot->aim_error_pitch * settle +
		bots_random_signed(bot) * skill->aim_error_degrees * 0.03f * BOTS_DEGREES;
	desired_yaw = (real)atan2(dy, dx) + bot->aim_error_yaw;
	desired_pitch = (real)atan2(dz, horizontal) + bot->aim_error_pitch;
	turn = skill->turn_degrees_per_tick * BOTS_DEGREES;
	yaw_off = bots_angle_difference(desired_yaw, bot->yaw);
	pitch_off = desired_pitch - bot->pitch;
	bot->yaw += PIN(yaw_off, -turn, turn);
	bot->pitch += PIN(pitch_off, -turn, turn);

	/* fire: once it has reacted, while its aim is on the target (its size
	at that distance) */
	yaw_off = (real)fabs(bots_angle_difference((real)atan2(dy, dx), bot->yaw));
	pitch_off = (real)fabs((real)atan2(dz, horizontal) - bot->pitch);
	cone = (real)atan(0.35f / MAX(tick->target_distance, 0.5f)) * skill->fire_cone + 0.01f;
	if (now - bot->target_first_seen_tick >= reaction_ticks && now - bot->target_seen_tick <= 3 &&
		yaw_off < cone && pitch_off < cone * 1.5f && tick->weapon_index != NONE)
	{
		short tap_ticks = bots_weapon_tap_ticks(tick->weapon_index);

		if (tap_ticks > 0)
		{
			/* (a press a shot, released between: as fast as the weapon fires,
			a little slower for the less skilled) */
			if (now >= bot->burst_until_tick)
			{
				tick->flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
				bot->burst_until_tick = now + tap_ticks + (long)(bots_random(bot) % (1 + skill->pause_ticks / 3));
			}
		}
		else if (now < bot->burst_until_tick)
			tick->flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
		else if (now >= bot->pause_until_tick)
		{
			/* (bursts, with pauses between) */
			bot->burst_until_tick = now + skill->burst_ticks + (long)(bots_random(bot) % 4);
			bot->pause_until_tick = bot->burst_until_tick + skill->pause_ticks + (long)(bots_random(bot) % 4);
			tick->flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
		}
	}
	/* a blow close up (port: an Infection zombie's skull, from a little
	further and more often: its one way to fight) */
	if (infection_player_is_zombie(bot->player_index))
	{
		if (tick->target_distance < 1.6f && now >= bot->melee_tick)
		{
			tick->flags |= FLAG(_unit_control_use_equipment_bit);
			bot->melee_tick = now + TICKS_PER_SECOND * 2 / 3;
		}
	}
	else if (tick->target_distance < 1.1f && now >= bot->melee_tick)
	{
		tick->flags |= FLAG(_unit_control_use_equipment_bit);
		bot->melee_tick = now + TICKS_PER_SECOND;
	}
	/* a grenade at middle range, now and then */
	if (tick->target_distance > 6.0f && tick->target_distance < 22.0f && now >= bot->grenade_tick &&
		(tick->unit->unit.grenade_counts[0] > 0 || tick->unit->unit.grenade_counts[1] > 0) &&
		bots_random_real(bot) < skill->grenade_chance / TICKS_PER_SECOND * 3.0f &&
		now - bot->target_first_seen_tick >= reaction_ticks)
	{
		tick->flags |= FLAG(_unit_control_throw_grenade_bit);
		/* (no grenade readied: NONE, -1, not an index) */
		if (tick->unit->unit.current_grenade_index < 0 || tick->unit->unit.current_grenade_index > 1 ||
			tick->unit->unit.grenade_counts[(long)tick->unit->unit.current_grenade_index] <= 0)
			tick->action->desired_grenade_index = tick->unit->unit.grenade_counts[0] > 0 ? 0 : 1;
		bot->grenade_tick = now + TICKS_PER_SECOND * 4;
	}
	/* strafing, to a side for a while; nearer or further to keep its
	weapon's distance; a jump now and then */
	if (now >= bot->strafe_until_tick)
	{
		bot->strafe_sign = bots_random_real(bot) < 0.5f ? -1.0f : 1.0f;
		bot->strafe_until_tick = now + TICKS_PER_SECOND / 3 + (long)(bots_random(bot) % (TICKS_PER_SECOND));
	}
	{
		real to_target = (real)atan2(dy, dx);
		real range = bots_weapon_range(tick->weapon_index);
		real forward = tick->target_distance > range * 1.3f ? 0.8f : tick->target_distance < range * 0.6f ? -0.5f : 0.0f;
		real side = bot->strafe_sign;

		/* port: an Infection zombie runs at its target, weaving a little */
		if (infection_player_is_zombie(bot->player_index))
		{
			forward = 1.0f;
			side *= tick->target_distance > 6.0f ? 0.35f : 0.1f;
		}

		tick->move_direction = (real)atan2((real)sin(to_target) * forward + (real)sin(to_target + BOTS_PI * 0.5f) * side,
			(real)cos(to_target) * forward + (real)cos(to_target + BOTS_PI * 0.5f) * side);
		tick->moving = TRUE;
		tick->throttle_scale = 1.0f;
	}
	if (bots_random_real(bot) < skill->jump_chance / TICKS_PER_SECOND)
		tick->flags |= FLAG(_unit_control_jump_bit);
}

/* walking to the goal, looking where it goes; there, a look about (and the
last bit straight onto a spot or an item); a reload when no one is about */
static void bots_bot_walk(struct bots_bot *bot, struct bots_tick *tick)
{
	real direction;

	if (bots_follow_path(bot, tick->player->unit_index, &tick->position, &direction))
	{
		real turn = 12.0f * BOTS_DEGREES;
		real off = bots_angle_difference(direction, bot->yaw);

		tick->move_direction = direction;
		tick->moving = TRUE;
		bot->yaw += PIN(off, -turn, turn);
		bot->pitch += PIN(-bot->pitch, -0.05f, 0.05f);
		if (bot->climb_until_tick && tick->now < bot->climb_until_tick && (bot->climb_until_tick - tick->now) % 20 == 0)
			tick->flags |= FLAG(_unit_control_jump_bit);
		if (bot->ladder_jump)
		{
			tick->flags |= FLAG(_unit_control_jump_bit);
			bot->ladder_jump = FALSE;
		}
	}
	else
	{
		bot->yaw += 2.0f * BOTS_DEGREES;
		if (bot->goal_kind == _bots_goal_roam || bot->goal_kind == _bots_goal_enemy)
			bot->goal_kind = _bots_goal_none;
		/* (on to a spot that must be stood on) */
		if (bot->goal_kind == _bots_goal_objective && bot->goal_exact &&
			bots_distance2d(&bot->goal_point, &tick->position) > 0.15f)
		{
			tick->move_direction = (real)atan2(bot->goal_point.y - tick->position.y, bot->goal_point.x - tick->position.x);
			tick->moving = TRUE;
			tick->throttle_scale = 0.5f;
		}
		/* (at an item: on to it) */
		if (bot->goal_kind == _bots_goal_item)
		{
			struct object_datum *item = object_try_and_get_and_verify_type(bot->item_index, _object_mask_item);

			if (item && bots_distance2d(&item->object.position, &tick->position) > 0.3f)
			{
				tick->move_direction = (real)atan2(item->object.position.y - tick->position.y,
					item->object.position.x - tick->position.x);
				tick->moving = TRUE;
				tick->throttle_scale = 0.6f;
			}
		}
	}
	if (tick->weapon_index != NONE && bots_weapon_loaded(tick->weapon_index) < 0.4f &&
		tick->now - bot->target_seen_tick > TICKS_PER_SECOND * 2)
	{
		tick->flags |= FLAG(_unit_control_weapon_reload_bit);
	}
	/* port: standing at a vantage point or a defender's post: looking from
	one place it watches to another (bots_tactics.h) */
	if (!tick->moving)
		bots_tactics_look((long)(bot - bots_globals.bots), &tick->position, tick->now, &bot->yaw, &bot->pitch);
}

/* the bots in each other's way (a team's bots leaving their base by the same
door, Longest's, jammed in it a whole game, each going round the others):
the one with the higher index steps back from the other, while the other
moves, so that the first goes through */
static void bots_bot_yield(struct bots_bot *bot, long bot_index, struct bots_tick *tick)
{
	long index;

	if (!tick->moving || tick->fighting || tick->object->object.parent_object_index != NONE)
		return;
	for (index = 0; index < bot_index; index++)
	{
		struct bots_bot *other = &bots_globals.bots[index];
		struct object_datum *object;
		real dx, dy, distance, velocity;

		if (other->player_index == NONE || other->unit_index == NONE)
			continue;
		object = (struct object_datum *)object_try_and_get_and_verify_type(other->unit_index, _object_mask_unit);
		if (!object || object->object.parent_object_index != NONE)
			continue;
		dx = object->object.position.x - tick->position.x;
		dy = object->object.position.y - tick->position.y;
		distance = (real)sqrt(dx * dx + dy * dy);
		velocity = (real)sqrt(object->object.translational_velocity.i * object->object.translational_velocity.i +
			object->object.translational_velocity.j * object->object.translational_velocity.j);
		if (distance > 1.1f || distance < 0.01f || fabs(object->object.position.z - tick->position.z) > 1.5f ||
			velocity < 0.02f ||
			(dx * (real)cos(tick->move_direction) + dy * (real)sin(tick->move_direction)) < 0.3f * distance)
		{
			continue;
		}
		tick->move_direction = (real)atan2(-dy, -dx);
		tick->throttle_scale = 0.6f;
		return;
	}
}

/* weapons: the other one when this one is empty; at the weapon it fetches,
the action button (X) held, as a player holds it to swap a weapon (the swap:
players_update_before_game's player_handle_weapon_swap), let go now and
then, so that a second swap can follow a first */
static void bots_bot_weapons(struct bots_bot *bot, struct bots_tick *tick)
{
	if (tick->weapon_index != NONE && bots_weapon_empty(tick->weapon_index))
	{
		short slot;

		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long other = tick->unit->unit.weapon_object_indices[slot];

			if (slot != tick->unit->unit.current_weapon_index && other != NONE && !bots_weapon_empty(other))
			{
				tick->action->desired_weapon_index = slot;
				break;
			}
		}
	}
	if (bot->goal_kind == _bots_goal_item)
	{
		struct object_datum *item = object_try_and_get_and_verify_type(bot->item_index, _object_mask_weapon);

		if (item && item->object.parent_object_index == NONE &&
			bots_distance2d(&item->object.position, &tick->position) < 0.8f &&
			fabs(item->object.position.z - tick->position.z) < 1.2f)
		{
			if (tick->now - bot->item_press_tick > TICKS_PER_SECOND)
				bot->item_press_tick = tick->now;
			if (bot->item_first_press_tick < bot->goal_tick)
				bot->item_first_press_tick = tick->now;
			if (tick->now - bot->item_press_tick < TICKS_PER_SECOND / 2)
				tick->flags |= FLAG(_unit_control_swap_weapons_bit);
		}
	}
}

/* port: what stands before a stuck bot, the way it walks (halo.log's
moves line): a ray at its knees and one at its chest, half a unit on, against
the level, scenery and machines (not the other bots); a low wall when the
first meets one and the second none */
static short bots_stuck_obstacle(struct bots_bot *bot, struct bots_tick *tick)
{
	real_vector3d vector = { (real)cos(tick->move_direction) * 0.6f, (real)sin(tick->move_direction) * 0.6f, 0.0f };
	unsigned long flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit);
	struct collision_result collision;
	real_point3d knee = tick->position, chest = tick->position;
	boolean low, high;

	(void)bot;
	knee.z += BOTS_KNEE_HEIGHT;
	chest.z += BOTS_CHEST_HEIGHT;
	low = collision_test_vector(flags, &knee, &vector, tick->player->unit_index, &collision);
	high = collision_test_vector(flags, &chest, &vector, tick->player->unit_index, &collision);
	return high ? _bots_obstacle_tall : low ? _bots_obstacle_low : _bots_obstacle_none;
}

/* port: whether the way a bot would step aside to is open: a ray at its
chest a unit to that side (the level, scenery, machines) */
static boolean bots_side_open(struct bots_tick *tick, real direction)
{
	real_vector3d vector = { (real)cos(direction) * 1.0f, (real)sin(direction) * 1.0f, 0.0f };
	struct collision_result collision;
	real_point3d chest = tick->position;

	chest.z += BOTS_CHEST_HEIGHT;
	return !collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit), &chest, &vector, tick->player->unit_index, &collision);
}

/* port: the goal given up after three times stuck: not gone for again a
while (as one it finds no way to), another roaming place */
static void bots_goal_give_up(struct bots_bot *bot, struct bots_tick *tick)
{
	long now = tick->now;

	bots_globals.stuck_giveups++;
	switch (bot->goal_kind)
	{
	case _bots_goal_item:
		bots_item_set_unreachable(bot, bot->item_index);
		break;
	case _bots_goal_objective:
		bot->objective_rest_until_tick = now + TICKS_PER_SECOND * 5;
		break;
	case _bots_goal_enemy:
		bot->hunt_rest_until_tick = now + TICKS_PER_SECOND * 3;
		break;
	default:
		break;
	}
	bot->avoid_point = bot->goal_point;
	bot->avoid_until_tick = now + TICKS_PER_SECOND * 20;
	bot->goal_kind = _bots_goal_none;
	bot->hop_index = NONE;
	bot->stuck_count = 0;
	if (bots_globals.roam_point_count > 0)
		bots_pick_roam_goal(bot, &tick->position);
}

/* stuck: not moving for a second while it walks - port: what stands before
it looked at, and got past as a player does: a low wall (a crate, a step, a
railing) jumped, the feet tucked; a tall one walked round, to the side that
is open; again, backed off and round the other way, jumping; the third
time the goal given up a while. A low wall met as it walks is jumped at
once (five ticks barely moving, a look ahead), before it is stuck. In a
fight the walk is the strafe's: blocked half a second, the strafe turns
the other way (it was stuck there, its goal and path dropped, before) */
static void bots_bot_unstick(struct bots_bot *bot, struct bots_tick *tick)
{
	long now = tick->now;
	struct biped_datum *biped = biped_try_and_get(tick->player->unit_index);
	boolean airborne = biped && TEST_FLAG(biped->biped.flags, _biped_airborne_bit);
	boolean ladder = bot->via_teleporter && bot->via_climb_tick && !bot->via_top_tick;
	boolean hop = bot->hop_index != NONE && bot->hop_take_tick;
	real speed = (real)sqrt(tick->object->object.translational_velocity.i * tick->object->object.translational_velocity.i +
		tick->object->object.translational_velocity.j * tick->object->object.translational_velocity.j);

	if (tick->fighting)
	{
		if (now - bot->fight_block_tick >= TICKS_PER_SECOND / 2)
		{
			if (tick->moving && !airborne && bots_distance2d(&tick->position, &bot->fight_block_position) < 0.25f)
			{
				bots_globals.play.fight_blocks++;
				bot->strafe_sign = -bot->strafe_sign;
				bot->strafe_until_tick = now + TICKS_PER_SECOND / 2 + (long)(bots_random(bot) % (TICKS_PER_SECOND / 2));
			}
			bot->fight_block_tick = now;
			bot->fight_block_position = tick->position;
		}
		/* (the walk's clock from the fight's end) */
		bot->stuck_position = tick->position;
		bot->stuck_tick = now;
		bot->stuck_count = 0;
		bot->slow_ticks = 0;
		bot->unstick_until_tick = 0;
		return;
	}
	if (tick->moving && !airborne && !ladder && !hop && now >= bot->unstick_until_tick &&
		tick->object->object.parent_object_index == NONE)
	{
		bot->slow_ticks = speed < 0.02f ? bot->slow_ticks + 1 : 0;
		if (bot->slow_ticks >= 5 && now >= bot->probe_tick)
		{
			bot->probe_tick = now + TICKS_PER_SECOND / 3;
			if (bots_stuck_obstacle(bot, tick) == _bots_obstacle_low)
			{
				tick->flags |= FLAG(_unit_control_jump_bit);
				bot->crouch_from_tick = now + 3;
				bot->crouch_until_tick = now + 16;
				bots_globals.play.walk_jumps++;
			}
		}
	}
	else
		bot->slow_ticks = 0;
	if (now - bot->stuck_tick >= TICKS_PER_SECOND)
	{
		if (tick->moving && bots_distance2d(&tick->position, &bot->stuck_position) < 0.4f &&
			tick->object->object.parent_object_index == NONE && !ladder && !hop)
		{
			short obstacle = bots_stuck_obstacle(bot, tick);
			real side = bots_random_real(bot) < 0.5f ? -1.0f : 1.0f;

			bots_globals.stuck_events++;
			if (!bot->path.valid)
				bots_globals.play.stuck_pathless++;
			if (obstacle == _bots_obstacle_low)
				bots_globals.stuck_low++;
			else if (obstacle == _bots_obstacle_tall)
				bots_globals.stuck_tall++;
			else
				bots_globals.stuck_clear++;
			bot->stuck_count++;
			if (getenv("HALO_BOT_STUCK_LOG"))
				platform_log("bots: stuck bot %ld at (%.1f %.1f %.1f) dir %.0f goal %d (%.1f %.1f %.1f) %.1f away path %s %d/%d%s%s%s obstacle %d count %d",
					(long)(bot - bots_globals.bots) + 1, tick->position.x, tick->position.y, tick->position.z,
					tick->move_direction / BOTS_DEGREES, (int)bot->goal_kind, bot->goal_point.x, bot->goal_point.y,
					bot->goal_point.z, bots_distance3d(&bot->goal_point, &tick->position), bot->path.valid ? "yes" : "no",
					(int)bot->path.step_index, (int)bot->path.step_count, bot->path_short ? " short" : "",
					bot->via_teleporter ? " via" : "", bot->climb_until_tick ? " climbing" : "",
					(int)obstacle, (int)bot->stuck_count);
			if (getenv("HALO_BOT_STUCK_LOG") && bot->path.valid)
				platform_log("bots: stuck bot %ld: hop %ld (kind %d)%s, step (%.1f %.1f %.1f), path tick %ld ago, shortcut %d",
					(long)(bot - bots_globals.bots) + 1, bot->hop_index,
					bot->hop_index != NONE ? (int)bots_nav_hop_get(bot->hop_index)->kind : -1, bot->via_teleporter ? " via" : "",
					bot->path.steps[(long)bot->path.step_index].point.x, bot->path.steps[(long)bot->path.step_index].point.y,
					bot->path.steps[(long)bot->path.step_index].point.z, now - bot->path_tick, (int)bot->shortcut_phase);
			bot->path.valid = FALSE;
			bot->unstick_jump = FALSE;
			if (bot->stuck_count >= 3)
				bots_goal_give_up(bot, tick);
			else if (bot->stuck_count == 1 && obstacle == _bots_obstacle_low)
			{
				/* (a low wall: over it, the feet tucked) */
				bot->unstick_direction = tick->move_direction;
				bot->unstick_until_tick = now + TICKS_PER_SECOND / 2;
				bot->unstick_jump = TRUE;
			}
			else
			{
				/* (round it, to the open side: the first time sidestepping,
				the second backing off aslant, the other way, jumping) */
				if (bots_side_open(tick, tick->move_direction + side * BOTS_PI * 0.5f))
					;
				else if (bots_side_open(tick, tick->move_direction - side * BOTS_PI * 0.5f))
					side = -side;
				if (bot->stuck_count == 1)
				{
					bot->unstick_direction = tick->move_direction + side * BOTS_PI * 0.5f;
					bot->unstick_until_tick = now + TICKS_PER_SECOND * 2 / 3;
					bot->unstick_jump = obstacle == _bots_obstacle_none;
				}
				else
				{
					bot->unstick_direction = tick->move_direction + BOTS_PI - side * BOTS_PI * 0.3f;
					bot->unstick_until_tick = now + TICKS_PER_SECOND * 2 / 3;
					bot->unstick_jump = TRUE;
				}
			}
			bot->unstick_from_tick = now;
		}
		else
			bot->stuck_count = 0;
		bot->stuck_position = tick->position;
		bot->stuck_tick = now;
	}
	if (now < bot->unstick_until_tick)
	{
		tick->move_direction = bot->unstick_direction;
		tick->moving = TRUE;
		if (bot->unstick_jump && now - bot->unstick_from_tick == 2)
		{
			tick->flags |= FLAG(_unit_control_jump_bit);
			bot->crouch_from_tick = now + 3;
			bot->crouch_until_tick = now + 16;
		}
	}
}

/* ---------- vehicles (port)

The bots' own: the campaign's AI gets in, drives and guns through its actors
(action_vehicle.c, actor_moving.c), which a bot - a player, with no actor -
has not; a bot gets in and drives with a player's controls, as its seats and
ways are weighed there. */

/* port: a teammate bot going for a seat the bot is much nearer to
(BOTS_VEHICLE_TAKE_OVER), which it takes over: a Scorpion a bot set off for
from 45 units away goes to the bot that spawns beside it. Teammates alone (a
free-for-all's bots keep what they go for); the other bot, on foot as any
going for a seat is, lets it go (bots_vehicle_consider) and looks again in a
while; it can never take it back, being further from it */
static boolean bots_seat_take_over(struct bots_bot const *bot, struct bots_bot const *other, long vehicle_index,
	real distance)
{
	struct object_datum *unit = object_try_and_get(other->unit_index);

	return unit && game_engine_has_teams() &&
		player_get(other->player_index)->team_index == player_get(bot->player_index)->team_index &&
		bots_distance2d(&unit->object.position, &object_get(vehicle_index)->object.position) >
			distance + BOTS_VEHICLE_TAKE_OVER;
}

/* on foot: a vehicle to go for, looked for once a second (port: at once as
it spawns) while the bot's goal is a long way off
(BOTS_VEHICLE_GOAL_DISTANCE) and no fight is close, nor does it carry the
flag or the ball: an empty Warthog, Ghost or Scorpion standing still on its
way, its driver's seat, as its kind is worth (bots_vehicle_worths: a
Scorpion 50 units off, or 20 in any direction, 30 fresh from its base; a
Warthog 30 off, most of the way to the goal and not further from it than
the bot is); a Warthog a teammate drives (a bot, which waits; a player,
stopped), its gunner's seat, else a passenger's; or an empty vehicle on its
side close by, to right. A seat it can enter as a player does
(unit_can_enter_seat: no enemy in the vehicle; an animation for the seat),
none another bot goes for (port: but a teammate's much further from it,
bots_seat_take_over); the nearest, a Scorpion's before a Ghost's before a
Warthog's driver's seat by their worth, a gunner's seat before a
passenger's, as the campaign's AI weighs them
(action_vehicle_evaluate_seat). The walk there given up on after 25 s, or
longer for one far (bots_vehicle_approach) */
static void bots_vehicle_consider(struct bots_bot *bot, struct bots_tick *tick)
{
	struct object_iterator iterator;
	long now = tick->now;
	long unit_index = tick->player->unit_index;
	real goal_distance, best_score = REAL_MAX, best_distance = 0.0f;
	long best_index = NONE;
	short best_seat = NONE;
	struct bots_bot *best_claimer = NULL;
	boolean fresh;

	if (bot->vehicle_index != NONE || now < bot->vehicle_check_tick || now < bot->vehicle_rest_until_tick)
		return;
	/* port: an Infection zombie hunts on foot (infection.c) */
	if (infection_player_is_zombie(bot->player_index))
		return;
	bot->vehicle_check_tick = now + TICKS_PER_SECOND;
	/* (the setting looked at once a second: a configuration lookup) */
	if (!bots_vehicles_enabled())
		return;
	if (bot->goal_kind == _bots_goal_none || bot->goal_kind == _bots_goal_item || bot->held_objective ||
		(tick->fighting && tick->target_distance < 15.0f))
	{
		return;
	}
	goal_distance = bots_distance2d(&bot->goal_point, &tick->position);
	if (goal_distance < BOTS_VEHICLE_GOAL_DISTANCE)
		return;
	fresh = now - bot->spawn_tick < TICKS_PER_SECOND * BOTS_VEHICLE_FRESH_SECONDS;
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (object_iterator_next(&iterator))
	{
		struct unit_datum *vehicle = unit_get(iterator.index);
		struct unit_definition *definition;
		real distance, speed, close, reach;
		short seat_index, kind;
		boolean friendly_driver, bot_driver;

		if (TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) ||
			TEST_FLAG(vehicle->unit.flags, _unit_not_enterable_by_player_bit) ||
			(iterator.index == bot->vehicle_avoid_index && now < bot->vehicle_avoid_until_tick) ||
			!bots_vehicle_drivable(iterator.index))
		{
			continue;
		}
		kind = bots_vehicle_kind(iterator.index);
		close = fresh ? MAX(bots_vehicle_worths[kind].close + 10.0f, 15.0f) : bots_vehicle_worths[kind].close;
		reach = MIN(bots_vehicle_worths[kind].reach, MAX(close, goal_distance * bots_vehicle_worths[kind].share));
		distance = bots_distance2d(&vehicle->object.position, &tick->position);
		if (distance > reach || (distance > close &&
			bots_distance2d(&vehicle->object.position, &bot->goal_point) > goal_distance + bots_vehicle_worths[kind].slack))
		{
			continue;
		}
		/* (on its side, no one in it: righted first, as a player does) */
		if (vehicle->object.up.k < 0.5f)
		{
			if (!bots_vehicle_has_riders(iterator.index) && distance < 15.0f && distance + 10.0f < best_score &&
				!bots_seat_claimer(iterator.index, NONE, bot))
			{
				best_score = distance + 10.0f;
				best_index = iterator.index;
				best_seat = NONE;
				best_claimer = NULL;
				best_distance = distance;
			}
			continue;
		}
		speed = magnitude3d(&vehicle->object.translational_velocity);
		friendly_driver = bots_vehicle_friendly_driver(iterator.index, tick->player, &bot_driver);
		definition = unit_definition_get(vehicle->definition_index);
		for (seat_index = 0; seat_index < definition->unit.seats.count; seat_index++)
		{
			short seat_kind = bots_seat_kind(iterator.index, seat_index);
			real score = distance;
			struct bots_bot *claimer;

			if (seat_kind == _bots_seat_driver)
			{
				if (vehicle->unit.driver_object_index != NONE || speed > 0.03f)
					continue;
				score -= bots_vehicle_worths[kind].bonus;
			}
			else
			{
				if (!friendly_driver || (!bot_driver && speed > 0.03f) ||
					bots_vehicle_definition(iterator.index)->vehicle_type != _bots_vehicle_type_human_jeep)
				{
					continue;
				}
				score += seat_kind == _bots_seat_gunner ? -3.5f : 3.0f;
			}
			if (score >= best_score || bots_seat_occupant(iterator.index, seat_index) != NONE)
				continue;
			claimer = bots_seat_claimer(iterator.index, seat_index, bot);
			if ((claimer && !bots_seat_take_over(bot, claimer, iterator.index, distance)) ||
				!unit_has_animation_to_enter_seat(unit_index, iterator.index, seat_index) ||
				!unit_can_enter_seat(unit_index, iterator.index, seat_index, NULL))
			{
				continue;
			}
			best_score = score;
			best_index = iterator.index;
			best_seat = seat_index;
			best_claimer = claimer;
			best_distance = distance;
		}
	}
	if (best_index == NONE)
		return;
	if (best_claimer)
	{
		if (best_claimer->goal_kind == _bots_goal_vehicle)
		{
			best_claimer->goal_serial++;
			best_claimer->path.valid = FALSE;
		}
		bots_vehicle_forget(best_claimer);
		best_claimer->vehicle_check_tick = now + TICKS_PER_SECOND * 3;
		bots_globals.vehicle_take_overs++;
	}
	bot->vehicle_index = best_index;
	bot->vehicle_seat_index = best_seat;
	bot->vehicle_flip = best_seat == NONE;
	bot->vehicle_tick = now;
	/* (the walk there: some 2 units a second, and a while; 25 s at least) */
	bot->vehicle_deadline_tick = now + TICKS_PER_SECOND * (long)MAX(25.0f, best_distance / 1.6f + 8.0f);
	bot->vehicle_presses = 0;
	bot->vehicle_press_tick = 0;
	bots_vehicle_goal(bot, unit_index);
}

/* port: on foot, a turret to man (bots_turret_seat), looked for once a
second while an enemy it saw in the last five seconds is about, nor does it
carry the flag or the ball, go for a powerup or a vehicle: the nearest
turret (of the map's: bots_powerups_find) no further than BOTS_TURRET_REACH,
the enemy no further from it than BOTS_TURRET_RANGE, upright, alive and
enterable, its seat empty, no other bot's, and one it can enter as a player
does (unit_can_enter_seat: no enemy on it; an animation for the seat); not
one it got off a while ago. Then it goes for the seat as for any vehicle's
(bots_vehicle_approach) */
static void bots_turret_consider(struct bots_bot *bot, struct bots_tick *tick)
{
	long now = tick->now;
	long unit_index = tick->player->unit_index;
	long index, best_index = NONE;
	short best_seat = NONE;
	real best_distance = BOTS_TURRET_REACH;

	if (bots_globals.turret_count <= 0 || bot->vehicle_index != NONE || now < bot->turret_check_tick ||
		infection_player_is_zombie(bot->player_index) ||
		now < bot->vehicle_rest_until_tick || bot->held_objective || bot->target_player_index == NONE ||
		now - bot->target_seen_tick > TICKS_PER_SECOND * 5 ||
		(bot->goal_kind == _bots_goal_item && bot->item_powerup != _bots_powerup_none))
	{
		return;
	}
	bot->turret_check_tick = now + TICKS_PER_SECOND;
	if (!bots_vehicles_enabled())
		return;
	for (index = 0; index < bots_globals.turret_count; index++)
	{
		long vehicle_index = bots_globals.turret_indices[index];
		struct unit_datum *vehicle = (struct unit_datum *)object_try_and_get_and_verify_type(vehicle_index, _object_mask_vehicle);
		real distance;
		short seat_index;

		if (!vehicle || TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) ||
			TEST_FLAG(vehicle->unit.flags, _unit_not_enterable_by_player_bit) || vehicle->object.up.k < 0.5f ||
			(vehicle_index == bot->vehicle_avoid_index && now < bot->vehicle_avoid_until_tick))
		{
			continue;
		}
		distance = bots_distance3d(&vehicle->object.position, &tick->position);
		if (distance >= best_distance ||
			bots_distance3d(&vehicle->object.position, &bot->target_last_position) > BOTS_TURRET_RANGE)
		{
			continue;
		}
		seat_index = bots_turret_seat(vehicle_index);
		if (seat_index == NONE || bots_seat_occupant(vehicle_index, seat_index) != NONE ||
			bots_seat_claimer(vehicle_index, seat_index, bot) ||
			!unit_has_animation_to_enter_seat(unit_index, vehicle_index, seat_index) ||
			!unit_can_enter_seat(unit_index, vehicle_index, seat_index, NULL))
		{
			continue;
		}
		best_distance = distance;
		best_index = vehicle_index;
		best_seat = seat_index;
	}
	if (best_index == NONE)
		return;
	bot->vehicle_index = best_index;
	bot->vehicle_seat_index = best_seat;
	bot->vehicle_flip = FALSE;
	bot->vehicle_turret = TRUE;
	bot->vehicle_tick = now;
	bot->vehicle_deadline_tick = now + TICKS_PER_SECOND * 25;
	bot->vehicle_presses = 0;
	bot->vehicle_press_tick = 0;
	bots_vehicle_goal(bot, unit_index);
}

/* on foot, near the vehicle it goes for: the last bit straight to the
seat's entrance (the path brings it near), round the vehicle when its middle
is in the way (action_vehicle_find_destination's way: to beside it), then
still, and in (below); a vehicle on its side, gone up to and righted with
the action button, as a player rights one (a press, not held). Given up on
after a while on the way, or trying to no end */
static void bots_vehicle_approach(struct bots_bot *bot, struct bots_tick *tick)
{
	struct object_datum *vehicle;
	real_point3d const *target = &bot->vehicle_entrance;
	real_point3d const *position = &tick->position;
	real distance, reach;
	long now = tick->now;

	if (bot->vehicle_index == NONE || bot->goal_kind != _bots_goal_vehicle)
		return;
	vehicle = object_try_and_get_and_verify_type(bot->vehicle_index, _object_mask_vehicle);
	if (!vehicle)
	{
		bots_vehicle_forget(bot);
		return;
	}
	if (now > bot->vehicle_deadline_tick || bot->vehicle_presses > 10)
	{
		/* (port: halo.log's; every bot leaving it be a while too was
		tried, Oct 2026: on Sidewinder, whose Scorpions stick where some
		bots get in and others do not, the bots rode them less) */
		bots_globals.vehicle_giveups++;
		platform_log("bots: bot %ld gave up on %s (%s), %.1f away", (long)(bot - bots_globals.bots) + 1,
			tag_get_name(vehicle->definition_index), bot->vehicle_presses > 10 ? "no way in" : "too long on the way",
			bots_distance2d(&bot->vehicle_entrance, &tick->position));
		bots_vehicle_avoid(bot, bot->vehicle_index, TICKS_PER_SECOND * 30);
		bots_vehicle_forget(bot);
		return;
	}
	distance = bots_distance2d(target, position);
	/* (port: a turret's entrance may lie against its mount, never quite
	reached: nearer than a seat's reach, unit_find_nearby_seat's metre) */
	reach = bot->vehicle_flip ? vehicle->object.bounding_sphere_radius * 0.8f : bot->vehicle_turret ? 0.85f : 0.45f;
	if (distance > 4.0f || fabs(target->z - position->z) > 2.5f)
		return;
	if (distance > reach)
	{
		real_point3d aim = *target;
		real to_target_i = target->x - position->x, to_target_j = target->y - position->y;
		real to_vehicle_i = vehicle->object.position.x - position->x, to_vehicle_j = vehicle->object.position.y - position->y;
		real length_squared = to_target_i * to_target_i + to_target_j * to_target_j;
		real t = (to_target_i * to_vehicle_i + to_target_j * to_vehicle_j) / MAX(length_squared, 0.0001f);
		/* (how wide the vehicle is this way: its middle to the entrance) */
		real radius = bots_distance2d(&vehicle->object.position, target);

		if (!bot->vehicle_flip && distance > 1.2f && t > 0.0f && t < 1.2f &&
			fabs(to_target_i * to_vehicle_j - to_target_j * to_vehicle_i) / (real)sqrt(MAX(length_squared, 0.0001f)) < radius * 0.9f)
		{
			real side_i = -to_target_j, side_j = to_target_i;
			real side_length = (real)sqrt(side_i * side_i + side_j * side_j);

			if (side_i * to_vehicle_i + side_j * to_vehicle_j > 0.0f)
			{
				side_i = -side_i;
				side_j = -side_j;
			}
			aim.x = vehicle->object.position.x + side_i / MAX(side_length, 0.0001f) * radius * 1.1f;
			aim.y = vehicle->object.position.y + side_j / MAX(side_length, 0.0001f) * radius * 1.1f;
		}
		tick->move_direction = (real)atan2(aim.y - position->y, aim.x - position->x);
		tick->moving = TRUE;
		tick->throttle_scale = distance < 1.5f ? 0.45f : 1.0f;
		bot->yaw += PIN(bots_angle_difference(tick->move_direction, bot->yaw), -12.0f * BOTS_DEGREES, 12.0f * BOTS_DEGREES);
		return;
	}
	tick->moving = FALSE;
	bot->yaw += PIN(bots_angle_difference((real)atan2(vehicle->object.position.y - position->y,
		vehicle->object.position.x - position->x), bot->yaw), -12.0f * BOTS_DEGREES, 12.0f * BOTS_DEGREES);
	/* (still, and the vehicle not turning: player_examine_nearby_vehicle's
	rule) */
	if (magnitude_squared3d(&tick->object->object.translational_velocity) >= 0.01f ||
		magnitude_squared3d(&vehicle->object.angular_velocity) >= 0.01f || now - bot->vehicle_press_tick <= 10)
	{
		return;
	}
	bot->vehicle_press_tick = now;
	bot->vehicle_presses++;
	if (bot->vehicle_flip)
	{
		tick->flags |= FLAG(_unit_control_action_bit);
		return;
	}
	/* its own seat: the action button takes the seat nearest the player
	(unit_find_nearby_seat), and a Warthog's three are close together - a bot
	going for the driver's seat got the passenger's, and sat waiting for a
	driver - so the bot gets into the seat it chose, as the campaign's AI
	does (action_vehicle_perform: unit_enter_seat), where a player could:
	within a metre of the seat's entrance or the seat (unit_find_nearby_seat's
	reach), the vehicle upright and enterable, a seat that wants a driver
	only with one, and none in it nor an enemy in the vehicle
	(unit_can_enter_seat, in unit_enter_seat) */
	{
		long unit_index = tick->player->unit_index;
		struct unit_datum *vehicle_unit = unit_get(bot->vehicle_index);
		struct unit_definition *definition = unit_definition_get(vehicle_unit->definition_index);
		struct unit_seat *seat;
		real_point3d entrance, seat_point;

		if (bot->vehicle_seat_index < 0 || bot->vehicle_seat_index >= definition->unit.seats.count)
			return;
		seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, bot->vehicle_seat_index, struct unit_seat);
		if (vehicle->object.up.k < 0.5f || TEST_FLAG(vehicle_unit->unit.flags, _unit_not_enterable_by_player_bit) ||
			(TEST_FLAG(seat->flags, 9) && vehicle_unit->unit.driver_object_index == NONE) ||
			!unit_get_seat_entrance_point(unit_index, bot->vehicle_index, bot->vehicle_seat_index, &entrance, &seat_point, NULL) ||
			MIN(bots_distance3d(&entrance, &tick->object->object.bounding_sphere_center),
				bots_distance3d(&seat_point, &tick->object->object.bounding_sphere_center)) >= 1.0f)
		{
			return;
		}
		unit_enter_seat(unit_index, bot->vehicle_index, bot->vehicle_seat_index);
	}
}

/* a teammate bot on its way to a seat of the vehicle, near (the driver
waits for it, a while) */
static boolean bots_vehicle_boarding(struct bots_bot *bot, long vehicle_index)
{
	struct object_datum *vehicle = object_get(vehicle_index);
	long index;

	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_bot *other = &bots_globals.bots[index];
		struct object_datum *unit;

		if (other == bot || other->player_index == NONE || other->unit_index == NONE ||
			other->vehicle_index != vehicle_index || other->vehicle_flip ||
			game_time_get() - other->vehicle_tick > TICKS_PER_SECOND * 12)
		{
			continue;
		}
		unit = object_try_and_get(other->unit_index);
		if (unit && bots_distance2d(&unit->object.position, &vehicle->object.position) < 25.0f)
			return TRUE;
	}
	return FALSE;
}

/* driving: the direction to its goal along its path (the searches as on
foot, from where the vehicle is, as wide as it is: bots_search_ask), a step
passed once the vehicle is near it; FALSE with nowhere to go (there) */
static boolean bots_drive_path(struct bots_bot *bot, struct bots_tick *tick, real *direction)
{
	long now = tick->now;
	real_point3d const *position = &tick->position;

	if (bot->goal_kind == _bots_goal_none)
		return FALSE;
	if ((!bot->path.valid || now - bot->path_tick > TICKS_PER_SECOND * 4) && now - bot->path_tick > TICKS_PER_SECOND / 3)
		bots_search_ask(bot, tick->player->unit_index);
	if (bot->path.valid)
	{
		while (bot->path.step_index < bot->path.step_count &&
			bots_distance2d(&bot->path.steps[(long)bot->path.step_index].point, position) < BOTS_DRIVE_STEP_REACHED)
		{
			bot->path.step_index++;
		}
		if (bot->path.step_index < bot->path.step_count)
		{
			struct path_step *step = &bot->path.steps[(long)bot->path.step_index];

			*direction = (real)atan2(step->point.y - position->y, step->point.x - position->x);
			return TRUE;
		}
		bot->path.valid = FALSE;
		/* (the whole way driven: there; a roaming place, another) */
		if (bot->path.steps_finish_path && !bot->path_short && !bot->via_teleporter)
		{
			if (bot->goal_kind != _bots_goal_objective)
				bot->goal_kind = _bots_goal_none;
			return FALSE;
		}
		bots_search_ask(bot, tick->player->unit_index);
	}
	/* (no path yet, or none found: straight there) */
	if (bots_distance2d(&bot->goal_point, position) < BOTS_DRIVE_STEP_REACHED * 1.5f)
	{
		if (bot->goal_kind != _bots_goal_objective)
			bot->goal_kind = _bots_goal_none;
		return FALSE;
	}
	*direction = (real)atan2(bot->goal_point.y - position->y, bot->goal_point.x - position->x);
	return TRUE;
}

/* the driver's seat: steering as a player steers, by where it looks (the
vehicle turns to its driver's facing: vehicles.c), the throttle on (eased in
a sharp turn); along its path to its goal, or with an enemy in sight: a
driver that is also the gunner (a Ghost's, a Scorpion's) fights as on foot,
the vehicle on at an enemy far off (a Ghost strafes one near, a Scorpion
stands), a Warthog's driver goes round the enemy for its gunner, or with no
gunner at a lone one (to run it down). Stuck (not moving for a second while
it drives), it backs off a moment every other second, then goes on; it
waits as it gets in, and while a teammate bot comes to a seat */
static void bots_bot_drive(struct bots_bot *bot, struct bots_tick *tick, long vehicle_index, boolean gunner)
{
	struct unit_datum *vehicle = unit_get(vehicle_index);
	long now = tick->now;
	real direction = bot->yaw;
	boolean fighting = bot->target_player_index != NONE && now - bot->target_seen_tick <= TICKS_PER_SECOND / 2;

	if (now - bot->ride_stuck_tick >= TICKS_PER_SECOND)
	{
		real moved = bots_distance2d(&vehicle->object.position, &bot->ride_stuck_position);

		if (bot->ride_driving && moved < 0.6f)
		{
			/* (what it drives into: scenery or a machine - Sidewinder's
			blast door, which the walkable surfaces go through - is learnt to
			be in the way, its surfaces closed to every bot's searches, as a
			bot on foot learns it: bots_paths_learn) */
			struct collision_result collision;
			unsigned long flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
				FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
				FLAG(_collision_test_objects_machines_bit);
			/* (port: past its front: a Scorpion's is 2.2 units from its
			middle, whose 2.5 found nothing before it) */
			real length = MAX(2.5f, vehicle->object.bounding_sphere_radius + 0.6f);
			real scale = bot->ride_reversing ? -length : length;
			real_vector3d vector = { vehicle->object.forward.i * scale, vehicle->object.forward.j * scale,
				vehicle->object.forward.k * scale };
			boolean hit = collision_test_vector(flags, &vehicle->object.bounding_sphere_center, &vector, vehicle_index, &collision);

			/* (port: and low, at its wheels or treads: a ledge below its
			middle) */
			if (!hit)
			{
				real_point3d low = vehicle->object.position;

				low.z += 0.25f;
				hit = collision_test_vector(flags, &low, &vector, vehicle_index, &collision);
			}

			if (hit && collision.type == _collision_result_object &&
				object_definition_get(object_get(collision.object_index)->definition_index)->object.collision_model.index != NONE &&
				!bots_paths_learned(collision.object_index) && !bots_object_marks_place(object_get(collision.object_index)))
			{
				bots_paths_close_object(collision.object_index, "in a vehicle's way:");
			}
			/* (the level before it, a second time: the way closed to vehicles) */
			else if (hit && collision.type == _collision_result_structure && !bot->ride_reversing && bot->ride_stuck_count >= 1)
			{
				real_point3d before = collision.point;

				before.x += vehicle->object.forward.i * 0.5f;
				before.y += vehicle->object.forward.j * 0.5f;
				bots_paths_close_for_vehicles(&before, &vehicle->object.position, &bot->goal_point,
					vehicle->object.bounding_sphere_radius);
			}
			/* (stuck a second time: the scenery and machines about it learnt
			too, as a bot on foot learns them when its path finds no way) */
			if (bot->ride_stuck_count >= 1)
				bots_paths_learn(&vehicle->object.position, &bot->goal_point);
			if (getenv("HALO_BOT_TRACE"))
			{
				platform_log("bots: trace bot %ld stuck %d driving at (%.1f %.1f %.1f) heading %.0f yaw %.0f: %s %s %s",
					(long)(bot - bots_globals.bots) + 1, (int)bot->ride_stuck_count + 1, vehicle->object.position.x,
					vehicle->object.position.y, vehicle->object.position.z,
					atan2(vehicle->object.forward.j, vehicle->object.forward.i) / BOTS_DEGREES, bot->yaw / BOTS_DEGREES,
					bot->ride_reversing ? "behind" : "ahead", !hit ? "clear" : collision.type == _collision_result_structure ? "the level" : "",
					hit && collision.type == _collision_result_object ? tag_get_name(object_get(collision.object_index)->definition_index) : "");
			}
			bot->ride_stuck_count++;
			if (!(bot->ride_stuck_count % 2))
			{
				/* (backing off, the wheels turned the other way: the nose
				comes round to where it was going, a three-point turn; the
				driver steers by its facing, vehicles.c) */
				real heading = (real)atan2(vehicle->object.forward.j, vehicle->object.forward.i);
				real off = bots_angle_difference(bot->ride_direction, heading);

				bot->ride_reverse_until_tick = now + TICKS_PER_SECOND * 3 / 2;
				bot->ride_reverse_yaw = heading - PIN(off, -BOTS_PI / 3.0f, BOTS_PI / 3.0f);
				bot->path.valid = FALSE;
			}
		}
		else if (moved > 2.0f)
			bot->ride_stuck_count = 0;
		bot->ride_stuck_position = vehicle->object.position;
		bot->ride_stuck_tick = now;
	}
	bot->ride_driving = FALSE;
	bot->ride_reversing = FALSE;
	if (now - bot->ride_tick < TICKS_PER_SECOND || bots_vehicle_boarding(bot, vehicle_index))
	{
		if (fighting && gunner)
		{
			bots_bot_fight(bot, tick);
			tick->moving = FALSE;
		}
		return;
	}
	if (now < bot->ride_reverse_until_tick)
	{
		bot->yaw += PIN(bots_angle_difference(bot->ride_reverse_yaw, bot->yaw), -15.0f * BOTS_DEGREES, 15.0f * BOTS_DEGREES);
		tick->moving = TRUE;
		tick->move_direction = bot->yaw + BOTS_PI;
		bot->ride_driving = TRUE;
		bot->ride_reversing = TRUE;
		return;
	}
	/* (the game's objective first, as on foot: a Ghost goes on to it as it
	fights, strafing; a Scorpion and a Warthog's driver drive on to it, the
	Scorpion firing at what comes before its gun, below) */
	if (fighting && gunner && bot->goal_kind == _bots_goal_objective)
	{
		short type = bots_vehicle_definition(vehicle_index)->vehicle_type;

		if (bots_drive_path(bot, tick, &direction))
		{
			if (type == _bots_vehicle_type_alien_scout)
			{
				bots_bot_fight(bot, tick);
				tick->moving = TRUE;
				tick->move_direction = direction;
				tick->throttle_scale = 1.0f;
				bot->ride_driving = TRUE;
				return;
			}
			fighting = FALSE;
		}
	}
	else if (fighting && bot->goal_kind == _bots_goal_objective)
		fighting = FALSE;
	if (fighting)
	{
		struct object_datum *target = object_get(player_get(bot->target_player_index)->unit_index);
		real to_target = (real)atan2(target->object.position.y - tick->position.y, target->object.position.x - tick->position.x);
		real distance = bots_distance2d(&target->object.position, &tick->position);

		if (gunner)
		{
			bots_bot_fight(bot, tick);
			if (bots_vehicle_definition(vehicle_index)->vehicle_type == _bots_vehicle_type_human_tank)
			{
				tick->moving = distance > 20.0f;
				tick->move_direction = bot->yaw;
			}
			else if (distance > 12.0f)
			{
				tick->moving = TRUE;
				tick->move_direction = bot->yaw;
			}
			bot->ride_driving = tick->moving;
			return;
		}
		if (now >= bot->strafe_until_tick)
		{
			bot->strafe_sign = bots_random_real(bot) < 0.5f ? -1.0f : 1.0f;
			bot->strafe_until_tick = now + TICKS_PER_SECOND * 3 + (long)(bots_random(bot) % (TICKS_PER_SECOND * 3));
		}
		direction = vehicle->unit.gunner_object_index != NONE && distance < 18.0f ?
			to_target + bot->strafe_sign * BOTS_PI * (distance < 10.0f ? 0.6f : 0.4f) : to_target;
	}
	else if (!bots_drive_path(bot, tick, &direction))
	{
		tick->moving = FALSE;
		return;
	}
	{
		real heading = (real)atan2(vehicle->object.forward.j, vehicle->object.forward.i);

		bot->ride_direction = direction;
		bot->yaw += PIN(bots_angle_difference(direction, bot->yaw), -15.0f * BOTS_DEGREES, 15.0f * BOTS_DEGREES);
		bot->pitch += PIN(-bot->pitch, -0.05f, 0.05f);
		tick->moving = TRUE;
		tick->move_direction = bot->yaw;
		tick->throttle_scale = fabs(bots_angle_difference(direction, heading)) > BOTS_PI * 0.5f ? 0.6f : 1.0f;
		bot->ride_driving = TRUE;
	}
	/* (the gun the driver's, an enemy in sight before it as it drives on:
	fired) */
	if (gunner && bot->target_player_index != NONE && now - bot->target_seen_tick <= TICKS_PER_SECOND / 2 &&
		now - bot->target_first_seen_tick >= (long)(tick->skill->reaction_seconds * TICKS_PER_SECOND))
	{
		struct object_datum *target = object_get(player_get(bot->target_player_index)->unit_index);

		if (fabs(bots_angle_difference((real)atan2(target->object.position.y - tick->position.y,
			target->object.position.x - tick->position.x), bot->yaw)) < 8.0f * BOTS_DEGREES)
		{
			tick->flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
		}
	}
}

/* whether the goal wants the bot on foot: the game's own (a flag to take or
bring home, a hill, the ball), but a race's checkpoints, driven through */
static boolean bots_goal_on_foot(struct bots_bot *bot)
{
	return bot->goal_kind == _bots_goal_objective && game_engine_get_variant()->game_engine_index != game_engine_race;
}

/* port: on a turret: fighting from it the enemy it sees (the turret turns
to where its gunner looks: unit aiming), looking about round the way it
faces otherwise (towards the enemy last seen); off (the reason returned,
_bots_exit_none to stay) when no enemy has been before it for six seconds
(since it got on, at the least), when the gun has not turned to
the one it fights for two (out of its arc: the turret's aim more than 25
degrees off it), or when it is being killed on it (hurt in the last second,
its shields down and its body below its skill's mark for a health pack in a
fight, a little above) */
static short bots_bot_turret(struct bots_bot *bot, struct bots_tick *tick, long vehicle_index)
{
	struct unit_datum *vehicle = unit_get(vehicle_index);
	long now = tick->now;

	if (bot->target_player_index != NONE && now - bot->target_seen_tick <= TICKS_PER_SECOND / 2)
	{
		struct object_datum *target = object_get(player_get(bot->target_player_index)->unit_index);
		real_vector3d to_target;
		real length, aim;

		bots_bot_fight(bot, tick);
		tick->moving = FALSE;
		/* (a gun whose trigger charges, fired as it says: held until charged,
		then let go - a press a shot fired nothing from Extinction's gun
		tower) */
		if (tick->weapon_index != NONE && bots_weapon_kind(weapon_get(tick->weapon_index)->definition_index)->charge_ticks > 0)
		{
			short charge_ticks = bots_weapon_kind(weapon_get(tick->weapon_index)->definition_index)->charge_ticks;

			if (!bot->ride_charge_tick && TEST_FLAG(tick->flags, _unit_control_weapon_primary_trigger_bit))
				bot->ride_charge_tick = now;
			if (bot->ride_charge_tick && now - bot->ride_charge_tick <= charge_ticks + 3)
				tick->flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
			else
			{
				tick->flags &= ~FLAG(_unit_control_weapon_primary_trigger_bit);
				bot->ride_charge_tick = 0;
			}
		}
		if (TEST_FLAG(tick->flags, _unit_control_weapon_primary_trigger_bit))
			bots_globals.turret_fire_ticks++;
		vector_from_points3d(&vehicle->object.bounding_sphere_center, &target->object.bounding_sphere_center, &to_target);
		length = magnitude3d(&to_target);
		aim = length > 0.01f ? dot_product3d(&to_target, &vehicle->unit.aiming_vector) / length : 1.0f;
		if (aim < (real)cos(25.0f * BOTS_DEGREES))
		{
			if (!bot->ride_turret_off_tick)
				bot->ride_turret_off_tick = now;
			else if (now - bot->ride_turret_off_tick > TICKS_PER_SECOND * 2)
				return _bots_exit_no_enemy;
		}
		else
			bot->ride_turret_off_tick = 0;
	}
	else
	{
		/* (towards where the enemy was last seen, else about) */
		real heading = (real)atan2(vehicle->object.forward.j, vehicle->object.forward.i);
		real look = bot->target_player_index != NONE ?
			(real)atan2(bot->target_last_position.y - tick->position.y, bot->target_last_position.x - tick->position.x) :
			heading + (real)sin((real)(now + (long)(bot - bots_globals.bots) * 37) / 40.0f) * 1.0f;

		bot->yaw += PIN(bots_angle_difference(look, bot->yaw), -6.0f * BOTS_DEGREES, 6.0f * BOTS_DEGREES);
		bot->pitch += PIN(-bot->pitch, -0.05f, 0.05f);
		bot->ride_turret_off_tick = 0;
	}
	if (now - MAX(bot->ride_tick, bot->target_seen_tick) > TICKS_PER_SECOND * 6)
		return _bots_exit_no_enemy;
	if (now - bot->hurt_tick < TICKS_PER_SECOND && tick->object->object.shield_vitality <= 0.05f &&
		tick->object->object.body_vitality < tick->skill->heal_below_fighting + 0.1f)
	{
		return _bots_exit_hurt;
	}
	return _bots_exit_none;
}

/* riding: just in, its seat counted; then out (the action button pressed,
as a player gets out: bipeds.c) when the vehicle lies on its side, sticks
fast, is left without a driver a while (but by a gunner still fighting), or
near a goal that wants it on foot; else the driver drives (bots_bot_drive),
and a gunner (with the vehicle's gun) or a passenger (with its own) fights
what it sees, looking about otherwise */
static void bots_bot_ride(struct bots_bot *bot, struct bots_tick *tick, long vehicle_index)
{
	struct unit_datum *vehicle = unit_get(vehicle_index);
	long unit_index = tick->player->unit_index;
	long now = tick->now;
	boolean driver = vehicle->unit.driver_object_index == unit_index;
	boolean gunner = vehicle->unit.gunner_object_index == unit_index;
	short exit = _bots_exit_none;

	if (bot->ride_vehicle_index != vehicle_index)
	{
		bot->ride_vehicle_index = vehicle_index;
		bot->ride_seat = bots_seat_kind(vehicle_index, tick->unit->unit.parent_seat_index);
		bot->ride_tick = now;
		bot->ride_last_position = vehicle->object.position;
		bot->ride_stuck_position = vehicle->object.position;
		bot->ride_stuck_tick = now;
		bot->ride_stuck_count = 0;
		bot->ride_driving = FALSE;
		bot->ride_reverse_until_tick = 0;
		bot->ride_driverless_tick = 0;
		bot->ride_flipped_tick = 0;
		bot->ride_exit = _bots_exit_none;
		bot->ride_turret = bots_turret_seat(vehicle_index) == tick->unit->unit.parent_seat_index;
		bot->ride_turret_off_tick = 0;
		bot->ride_charge_tick = 0;
		if (bot->ride_turret)
			bots_globals.turret_entries++;
		/* (debug: HALO_BOT_TRACE, a turret's gun) */
		if (bot->ride_turret && getenv("HALO_BOT_TRACE") && bots_current_weapon(vehicle) != NONE)
		{
			long gun_index = bots_current_weapon(vehicle);
			struct weapon_definition *gun = weapon_definition_get(weapon_get(gun_index)->definition_index);
			struct weapon_trigger_definition *trigger = gun->weapon.triggers.count > 0 ?
				TAG_BLOCK_GET_ELEMENT(&gun->weapon.triggers, 0, struct weapon_trigger_definition) : NULL;

			platform_log("bots: trace turret gun %s: magazines %d triggers %d flags %lx rate %.2f charging %.2f gunner %s",
				tag_get_name(weapon_get(gun_index)->definition_index), (int)gun->weapon.magazines.count,
				(int)gun->weapon.triggers.count, trigger ? trigger->flags : 0, trigger ? trigger->initial_rate_of_fire : 0.0f,
				trigger ? trigger->charging_time : 0.0f, vehicle->unit.gunner_object_index == unit_index ? "it" : "not it");
		}
		platform_log("bots: bot %ld in %s seat %d (%s%s), went for seat %d%s, at tick %ld", (long)(bot - bots_globals.bots) + 1,
			tag_get_name(vehicle->definition_index), (int)tick->unit->unit.parent_seat_index,
			bot->ride_seat == _bots_seat_driver ? "driver" : bot->ride_seat == _bots_seat_gunner ? "gunner" : "passenger",
			bot->ride_turret ? ", a turret" : "",
			bot->vehicle_index == vehicle_index ? (int)bot->vehicle_seat_index : NONE,
			vehicle->unit.driver_object_index != NONE && vehicle->unit.driver_object_index != unit_index ?
				(bots_player_is_bot(unit_get(vehicle->unit.driver_object_index)->unit.player_index) ? ", a bot driving" :
					", a player driving") : "", now);
		if (bots_vehicle_drivable(vehicle_index))
			bots_globals.vehicle_entries_by_kind[bots_vehicle_kind(vehicle_index)]++;
		if (!bots_globals.first_entry_tick)
			bots_globals.first_entry_tick = MAX(now, 1);
		bots_vehicle_forget(bot);
		bot->goal_kind = _bots_goal_none;
		bot->goal_serial++;
		bot->path.valid = FALSE;
		bots_globals.vehicle_entries++;
		bots_globals.vehicle_entries_by_seat[bot->ride_seat]++;
	}
	if (driver)
	{
		real moved = bots_distance2d(&vehicle->object.position, &bot->ride_last_position);

		if (moved < 3.0f)
			bots_globals.driven_distance += moved;
	}
	bot->ride_last_position = vehicle->object.position;
	/* (no items while riding) */
	bot->item_check_tick = now + TICKS_PER_SECOND;
	if (tick->unit->unit.animation.state == _unit_state_entering_seat)
		return;
	if (gunner)
	{
		long weapon_index = bots_current_weapon(vehicle);

		if (weapon_index != NONE)
			tick->weapon_index = weapon_index;
	}

	if (TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit))
		exit = _bots_exit_other;
	if (vehicle->object.up.k < 0.3f)
	{
		if (!bot->ride_flipped_tick)
			bot->ride_flipped_tick = now;
		else if (now - bot->ride_flipped_tick > TICKS_PER_SECOND)
			exit = _bots_exit_flipped;
	}
	else
		bot->ride_flipped_tick = 0;
	if (!driver && !bot->ride_turret && vehicle->unit.driver_object_index == NONE)
	{
		if (!bot->ride_driverless_tick)
			bot->ride_driverless_tick = now;
		else if (now - bot->ride_driverless_tick > TICKS_PER_SECOND * 3 && now - bot->target_seen_tick > TICKS_PER_SECOND * 2)
			exit = _bots_exit_driverless;
	}
	else
		bot->ride_driverless_tick = 0;
	/* (the goal looked at again every fourth tick, the bots in turn) */
	if (!((now + (long)(bot - bots_globals.bots)) % 4) || bot->goal_kind == _bots_goal_none)
		bots_choose_goal(bot, bot->player_index, &tick->position, FALSE);
	bots_search_take(bot, TRUE);
	/* (stuck fast; or stuck twice near a goal that wants it on foot: a
	base's doors, which no vehicle goes through) */
	if (driver && (bot->ride_stuck_count >= 5 || (bot->ride_stuck_count >= 2 && bots_goal_on_foot(bot) &&
		bots_distance2d(&bot->goal_point, &tick->position) < 2.5f * BOTS_VEHICLE_GOAL_EXIT)))
	{
		exit = _bots_exit_stuck;
	}
	if (exit == _bots_exit_none && bots_goal_on_foot(bot) && !bot->ride_turret &&
		bots_distance2d(&bot->goal_point, &tick->position) < BOTS_VEHICLE_GOAL_EXIT)
	{
		exit = _bots_exit_goal;
	}
	/* port: a turret's gunner: fighting from it, and off as it says */
	if (exit == _bots_exit_none && bot->ride_turret)
		exit = bots_bot_turret(bot, tick, vehicle_index);
	if (exit != _bots_exit_none)
	{
		if (bot->ride_exit == _bots_exit_none)
			bot->ride_exit = exit;
		if (now - bot->vehicle_press_tick > TICKS_PER_SECOND / 2 && !unit_is_busy(unit_index))
		{
			tick->flags |= FLAG(_unit_control_action_bit);
			bot->vehicle_press_tick = now;
		}
		return;
	}
	bot->ride_exit = _bots_exit_none;
	if (bot->ride_turret)
		;
	else if (driver)
		bots_bot_drive(bot, tick, vehicle_index, gunner);
	else if (bot->target_player_index != NONE && now - bot->target_seen_tick <= TICKS_PER_SECOND / 2)
	{
		bots_bot_fight(bot, tick);
		tick->moving = FALSE;
		if (TEST_FLAG(tick->flags, _unit_control_weapon_primary_trigger_bit))
			bots_globals.riding_fire_ticks++;
	}
	else
	{
		/* (looking about, round the way the vehicle goes) */
		real heading = (real)atan2(vehicle->object.forward.j, vehicle->object.forward.i);
		real look = heading + (real)sin((real)(now + (long)(bot - bots_globals.bots) * 37) / 45.0f) * 1.3f;

		bot->yaw += PIN(bots_angle_difference(look, bot->yaw), -6.0f * BOTS_DEGREES, 6.0f * BOTS_DEGREES);
		bot->pitch += PIN(-bot->pitch, -0.05f, 0.05f);
	}
	/* (in a seat, no grenade, blow or jump) */
	tick->flags &= ~(FLAG(_unit_control_throw_grenade_bit) | FLAG(_unit_control_use_equipment_bit) |
		FLAG(_unit_control_jump_bit));
}

/* on foot again after riding: why it got out counted; no vehicle a while
(longer at its goal), and the one that failed it (on its side, stuck) not
again for longer; a new goal and path from where it stands */
static void bots_vehicle_got_out(struct bots_bot *bot, struct bots_tick *tick)
{
	short exit = bot->ride_exit != _bots_exit_none ? bot->ride_exit : _bots_exit_other;

	bots_globals.vehicle_exits[exit]++;
	{
		struct object_datum *vehicle = object_try_and_get(bot->ride_vehicle_index);

		platform_log("bots: bot %ld out of %s (%s) after %.1f s%s", (long)(bot - bots_globals.bots) + 1,
			vehicle ? tag_get_name(vehicle->definition_index) : "?",
			exit == _bots_exit_flipped ? "on its side" : exit == _bots_exit_stuck ? "stuck" : exit == _bots_exit_goal ?
				"at its goal" : exit == _bots_exit_driverless ? "no driver" : exit == _bots_exit_no_enemy ? "no enemy before it" :
				exit == _bots_exit_hurt ? "hurt" : "other",
			(double)(tick->now - bot->ride_tick) / TICKS_PER_SECOND,
			!vehicle ? ", the vehicle gone" : TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) ? ", the vehicle destroyed" :
				vehicle->object.up.k < 0.3f ? ", thrown out (the vehicle over)" : "");
	}
	bot->vehicle_rest_until_tick = tick->now + TICKS_PER_SECOND * (exit == _bots_exit_goal ? 20 : 6);
	if (exit == _bots_exit_flipped || exit == _bots_exit_stuck)
		bots_vehicle_avoid(bot, bot->ride_vehicle_index, TICKS_PER_SECOND * 20);
	/* port: (a turret got off: not again a while, and none at once) */
	if (bot->ride_turret)
	{
		bots_vehicle_avoid(bot, bot->ride_vehicle_index, TICKS_PER_SECOND * 15);
		bot->ride_turret = FALSE;
	}
	bot->ride_vehicle_index = NONE;
	bot->ride_exit = _bots_exit_none;
	bot->goal_kind = _bots_goal_none;
	bot->goal_serial++;
	bot->path.valid = FALSE;
	bot->path_tick = tick->now - TICKS_PER_SECOND;
	bot->last_position = tick->position;
	bot->stuck_position = tick->position;
	bot->stuck_tick = tick->now;
	bot->stuck_count = 0;
}

/* port: halo.log's line of the game's vehicles, at its first census: how
many of each (by tag; the gametype's vehicle set decides which the map
places: game_engine.c), and with HALO_BOT_TRACE where each stands */
static void bots_vehicle_list(void)
{
	struct object_iterator iterator;
	long definitions[8], counts[8], kinds = 0, total = 0, index;
	char line[400];
	int length = 0;

	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *vehicle = object_get(iterator.index);

		total++;
		for (index = 0; index < kinds && definitions[index] != vehicle->definition_index; index++)
			;
		if (index == kinds && kinds < (long)NUMBEROF(definitions))
		{
			definitions[kinds] = vehicle->definition_index;
			counts[kinds++] = 0;
		}
		if (index < kinds)
			counts[index]++;
		if (getenv("HALO_BOT_TRACE"))
			platform_log("bots: trace vehicle %s at (%.1f %.1f %.1f), bounding radius %.2f, pathfinding radius %.2f",
				tag_get_name(vehicle->definition_index), vehicle->object.position.x, vehicle->object.position.y,
				vehicle->object.position.z, vehicle->object.bounding_sphere_radius,
				bots_vehicle_definition(iterator.index)->ai_pathfinding_radius);
	}
	line[0] = 0;
	for (index = 0; index < kinds && length < (int)sizeof(line) - 80; index++)
		length += snprintf(line + length, sizeof(line) - (size_t)length, "%s%s %ld", index ? ", " : " (",
			tag_get_name(definitions[index]), counts[index]);
	platform_log("bots: the game's vehicles: %ld%s%s", total, line, kinds ? ")" : "");
}

/* halo.log's census of the bots' vehicles, every ten seconds: the bots
riding and in which seats, those going for one, the vehicles and those with
riders, the distance the bots drove them, the bots' getting in (by seat) and
out (by why), those killed riding and the vehicles righted */
static void bots_vehicle_census(void)
{
	long now = game_time_get();
	long riding[NUMBER_OF_BOTS_SEATS] = { 0, 0, 0 };
	long alive = 0, going = 0, vehicles = 0, occupied = 0, exits = 0, index;
	struct object_iterator iterator;

	if (bots_globals.last_census_tick && now - bots_globals.last_census_tick < TICKS_PER_SECOND * 10)
		return;
	if (!bots_globals.vehicles_listed)
		bots_vehicle_list();
	bots_globals.vehicles_listed = TRUE;
	bots_globals.last_census_tick = now;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_bot *bot = &bots_globals.bots[index];
		struct player_datum *player = bot->player_index != NONE ? player_try_and_get(bot->player_index) : NULL;

		if (!player || player->unit_index == NONE)
			continue;
		alive++;
		if (bots_unit_vehicle(player->unit_index) != NONE && bot->ride_vehicle_index != NONE)
			riding[bot->ride_seat]++;
		else if (bot->vehicle_index != NONE)
			going++;
	}
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (object_iterator_next(&iterator))
	{
		vehicles++;
		if (bots_vehicle_has_riders(iterator.index))
			occupied++;
	}
	for (index = 0; index < NUMBER_OF_BOTS_EXITS; index++)
		exits += bots_globals.vehicle_exits[index];
	if (getenv("HALO_BOT_TRACE"))
	{
		char line[200];
		int length = 0;

		for (index = 0; index < BOTS_MAXIMUM && length < (int)sizeof(line) - 16; index++)
		{
			if (bots_globals.bots[index].player_index != NONE)
				length += snprintf(line + length, sizeof(line) - (size_t)length, " %ld:%ld", index + 1, bots_globals.bots[index].searches);
		}
		line[length] = 0;
		platform_log("bots: trace searches by bot%s", line);
	}
	platform_log("bots: vehicles at tick %ld: %ld of %ld bots riding (%ld driving, %ld gunning, %ld passengers), %ld going to one; "
		"vehicles %ld, %ld with riders; driven %.0f units; in %ld (driver %ld, gunner %ld, passenger %ld), out %ld (flipped %ld, "
		"stuck %ld, at the goal %ld, no driver %ld, other %ld, no enemy %ld, hurt %ld), righted %ld; kills riding %ld (driving %ld), "
		"%.1f s of gunners' and passengers' fire; %ld path searches driving; turrets %ld, manned %ld times, %.1f s of their fire, %ld kills from one; "
		"in by kind: tank %ld, jeep %ld, scout %ld; first in at tick %ld; %.0f s riding; %ld seats taken over, %ld given up on the way%s", now,
		riding[_bots_seat_driver] + riding[_bots_seat_gunner] + riding[_bots_seat_passenger], alive,
		riding[_bots_seat_driver], riding[_bots_seat_gunner], riding[_bots_seat_passenger], going, vehicles, occupied,
		(double)bots_globals.driven_distance, bots_globals.vehicle_entries,
		bots_globals.vehicle_entries_by_seat[_bots_seat_driver], bots_globals.vehicle_entries_by_seat[_bots_seat_gunner],
		bots_globals.vehicle_entries_by_seat[_bots_seat_passenger], exits, bots_globals.vehicle_exits[_bots_exit_flipped],
		bots_globals.vehicle_exits[_bots_exit_stuck], bots_globals.vehicle_exits[_bots_exit_goal],
		bots_globals.vehicle_exits[_bots_exit_driverless], bots_globals.vehicle_exits[_bots_exit_other],
		bots_globals.vehicle_exits[_bots_exit_no_enemy], bots_globals.vehicle_exits[_bots_exit_hurt],
		bots_globals.vehicle_flips, bots_globals.riding_kills[0] + bots_globals.riding_kills[1], bots_globals.riding_kills[0],
		(double)bots_globals.riding_fire_ticks / TICKS_PER_SECOND, bots_globals.driving_searches,
		bots_globals.turret_count, bots_globals.turret_entries, (double)bots_globals.turret_fire_ticks / TICKS_PER_SECOND,
		bots_globals.turret_kills, bots_globals.vehicle_entries_by_kind[0], bots_globals.vehicle_entries_by_kind[1],
		bots_globals.vehicle_entries_by_kind[2], bots_globals.first_entry_tick, (double)bots_globals.riding_ticks / TICKS_PER_SECOND,
		bots_globals.vehicle_take_overs, bots_globals.vehicle_giveups, bots_vehicles_enabled() ? "" : " (bots take no vehicles: HALO_BOT_VEHICLES=0)");
}

/* (debug) HALO_BOT_TRACE=<seconds>: each bot's state that often */
static void bots_bot_trace(struct bots_bot *bot, long bot_index, struct bots_tick *tick)
{
	static long trace = -1;
	long now = tick->now;

	if (trace < 0)
	{
		char const *setting = getenv("HALO_BOT_TRACE");

		trace = setting ? atol(setting) * TICKS_PER_SECOND : 0;
	}
	if (trace <= 0 || now % trace != bot_index)
		return;
	platform_log("bots: trace %ld bot %ld at (%.1f %.1f %.1f) goal %d (%.1f %.1f %.1f) s%ld path %s %d/%d%s "
		"target %ld seen %ld ago%s stuck %d yaw %.0f thr %.2f/%.2f flags %lx search %ld fail %ld",
		now, bot_index + 1, tick->position.x, tick->position.y, tick->position.z, (int)bot->goal_kind,
		bot->goal_point.x, bot->goal_point.y, bot->goal_point.z, bot->goal_surface_index,
		bot->path.valid ? "yes" : "no", (int)bot->path.step_index, (int)bot->path.step_count,
		bot->path.steps_finish_path ? " (all)" : "",
		bot->target_player_index == NONE ? -1L : (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(bot->target_player_index),
		now - bot->target_seen_tick, tick->fighting ? " fighting" : "", (int)bot->stuck_count,
		bot->yaw / BOTS_DEGREES, tick->action->throttle.i, tick->action->throttle.j, tick->action->control_flags,
		bot->search_slot, bot->path_failures);
	/* port: the vehicle it rides, or goes for */
	if (bot->ride_vehicle_index != NONE && object_try_and_get(bot->ride_vehicle_index))
	{
		platform_log("bots: trace riding %s seat %d for %ld ticks, stuck %d up %.2f exit %d",
			tag_get_name(object_get(bot->ride_vehicle_index)->definition_index), (int)bot->ride_seat, now - bot->ride_tick,
			(int)bot->ride_stuck_count, object_get(bot->ride_vehicle_index)->object.up.k, (int)bot->ride_exit);
	}
	else if (bot->vehicle_index != NONE && object_try_and_get(bot->vehicle_index))
	{
		platform_log("bots: trace going for %s seat %d%s, entrance (%.1f %.1f %.1f) %.1f away, %d presses",
			tag_get_name(object_get(bot->vehicle_index)->definition_index), (int)bot->vehicle_seat_index,
			bot->vehicle_flip ? " (to right it)" : "", bot->vehicle_entrance.x, bot->vehicle_entrance.y,
			bot->vehicle_entrance.z, bots_distance2d(&bot->vehicle_entrance, &tick->position), (int)bot->vehicle_presses);
	}
	if (bot->path.valid)
	{
		char steps[200];
		int length = 0;
		long step;

		for (step = 0; step < bot->path.step_count && length < (int)sizeof(steps) - 40; step++)
			length += snprintf(steps + length, sizeof(steps) - (size_t)length, " (%.1f %.1f %.1f)",
				bot->path.steps[step].point.x, bot->path.steps[step].point.y, bot->path.steps[step].point.z);
		platform_log("bots: trace path%s%s", steps, bot->via_teleporter ? " to a teleporter" : "");
	}
	if (bot->goal_kind == _bots_goal_item && object_try_and_get(bot->item_index))
	{
		struct object_datum *item = object_get(bot->item_index);

		platform_log("bots: trace item %s at (%.2f %.2f %.2f)", tag_get_name(item->definition_index),
			item->object.position.x, item->object.position.y, item->object.position.z);
	}
	if (tick->weapon_index != NONE)
	{
		struct weapon_datum *weapon = weapon_get(tick->weapon_index);
		short slot;

		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long other = tick->unit->unit.weapon_object_indices[slot];

			if (slot != tick->unit->unit.current_weapon_index && other != NONE)
				platform_log("bots: trace other weapon %s", tag_get_name(object_get(other)->definition_index));
		}
		platform_log("bots: trace weapon %s slot %d loaded %d total %d fired %ld ago, aim off %.1f/%.1f deg dist %.1f",
			tag_get_name(weapon->definition_index), (int)tick->unit->unit.current_weapon_index,
			(int)weapon->weapon.magazines[0].rounds_loaded, (int)weapon->weapon.magazines[0].rounds_total,
			now - weapon->weapon.game_time_last_fired, bot->aim_error_yaw / BOTS_DEGREES,
			bot->aim_error_pitch / BOTS_DEGREES, tick->target_distance);
	}
}

/* port: (teleporters as shortcuts) each tick on foot: a shortcut given up
on in time (the measure never answered, the entrance not come to), and the
teleporter failed when the bot stands on its entrance and is not sent; a
fight stops the clock */
static void bots_shortcut_update(struct bots_bot *bot, struct bots_tick *tick)
{
	real_point3d const *entrance;

	if (bot->shortcut_phase == _bots_shortcut_none)
		return;
	if (tick->fighting)
		bot->shortcut_deadline_tick++;
	if (bot->shortcut_phase == _bots_shortcut_measuring)
	{
		if (tick->now > bot->shortcut_deadline_tick)
			bot->shortcut_phase = _bots_shortcut_none;
		return;
	}
	entrance = &bots_globals.teleporters[bot->shortcut_index].entrance;
	if (bots_distance2d(entrance, &tick->position) < 0.6f && fabs(entrance->z - tick->position.z) < 1.5f)
	{
		if (!bot->shortcut_on_tick)
			bot->shortcut_on_tick = tick->now;
		else if (tick->now - bot->shortcut_on_tick > TICKS_PER_SECOND * 2)
		{
			bots_shortcut_fail(bot, "stood on it, not sent", tick->now);
			return;
		}
	}
	else
		bot->shortcut_on_tick = 0;
	/* (not taken yet, its searches to the entrance yet to say how far it
	is: let go, no failure of the teleporter's) */
	if (tick->now > bot->shortcut_deadline_tick && !bot->shortcut_entrance_known)
		bots_shortcut_drop(bot, "no answer");
	else if (tick->now > bot->shortcut_deadline_tick)
		bots_shortcut_fail(bot, "not come to in time", tick->now);
}

/* port: the bot's play this tick counted (halo.log's play line): the
grenade and the jump pressed, the weapons it holds - one it did not hold
last tick picked up (not as it spawns: what it spawns with) - and whether
one is a power weapon, and two held */
static void bots_play_count(struct bots_bot *bot, struct bots_tick *tick)
{
	short slot, held = 0;
	boolean power = FALSE;

	if (TEST_FLAG(tick->flags, _unit_control_throw_grenade_bit))
		bots_globals.play.grenades_thrown++;
	if (TEST_FLAG(tick->flags, _unit_control_jump_bit))
	{
		if (tick->fighting)
			bots_globals.play.jumps_fighting++;
		else
			bots_globals.play.jumps_moving++;
	}
	bots_globals.play.alive_ticks++;
	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		long weapon_index = tick->unit->unit.weapon_object_indices[slot];
		long definition_index = weapon_index != NONE ? object_get(weapon_index)->definition_index : NONE;
		short other;

		if (definition_index == NONE)
			continue;
		held++;
		if (bots_weapon_value(definition_index) >= 0 && bots_weapon_power(definition_index))
			power = TRUE;
		if (tick->now == bot->spawn_tick)
			continue;
		for (other = 0; other < MAXIMUM_WEAPONS_PER_UNIT; other++)
		{
			if (bot->held_definitions[other] == definition_index)
				break;
		}
		if (other >= MAXIMUM_WEAPONS_PER_UNIT && bots_weapon_value(definition_index) >= 0)
		{
			bots_globals.play.weapon_pickups++;
			if (bots_weapon_power(definition_index))
				bots_globals.play.power_pickups++;
		}
	}
	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		long weapon_index = tick->unit->unit.weapon_object_indices[slot];

		bot->held_definitions[slot] = weapon_index != NONE ? object_get(weapon_index)->definition_index : NONE;
	}
	if (power)
		bots_globals.play.power_ticks++;
	if (held >= 2)
		bots_globals.play.two_weapon_ticks++;
}

static void bots_bot_update(struct bots_bot *bot, long bot_index, struct player_action *action)
{
	struct bots_tick tick;
	real vitality;
	unsigned long long started = bots_profile.enabled > 0 ? vita_host_time_us() : 0;
	long vehicle_index;

	csmemset(&tick, 0, sizeof(tick));
	tick.skill = &bots_skills[bots_globals.skill];
	tick.player = player_get(bot->player_index);
	tick.now = game_time_get();
	tick.throttle_scale = 1.0f;
	tick.action = action;
	csmemset(action, 0, sizeof(*action));
	action->desired_weapon_index = NONE;
	action->desired_grenade_index = NONE;
	action->desired_zoom_level = NONE;
	action->desired_facing.yaw = bot->yaw;
	action->desired_facing.pitch = bot->pitch;

	if (tick.player->unit_index == NONE)
	{
		/* (dead: it forgets its fight, its path and its search; the game
		respawns it) */
		bots_search_take(bot, FALSE);
		bot->unit_index = NONE;
		bot->target_player_index = NONE;
		bot->path.valid = FALSE;
		bot->goal_kind = _bots_goal_none;
		bot->previous_control_flags = 0;
		/* port: (and the vehicle it went for, or rode; its vantage point
		and its lane's waypoints) */
		bots_vehicle_forget(bot);
		bot->ride_vehicle_index = NONE;
		bots_tactics_bot_dead(bot_index);
		/* port: (and its teleporter shortcut, its hop) */
		bot->shortcut_phase = _bots_shortcut_none;
		bot->hop_index = NONE;
		bot->hop_via = NONE;
		bot->crouch_until_tick = 0;
		return;
	}
	tick.unit = unit_get(tick.player->unit_index);
	tick.object = object_get(tick.player->unit_index);
	if (bot->unit_index != tick.player->unit_index)
	{
		/* (spawned: it looks the way its unit faces) */
		bot->unit_index = tick.player->unit_index;
		bot->yaw = (real)atan2(tick.object->object.forward.j, tick.object->object.forward.i);
		bot->pitch = 0.0f;
		bot->last_vitality = tick.object->object.body_vitality + tick.object->object.shield_vitality;
		bot->stuck_position = tick.object->object.position;
		bot->stuck_tick = tick.now;
		bot->stuck_count = 0;
		bot->goal_kind = _bots_goal_none;
		bot->next_look_tick = tick.now + (bot_index % 3);
		bot->climb_until_tick = 0;
		bot->path_short = FALSE;
		bot->via_teleporter = FALSE;
		bot->shortcut_phase = _bots_shortcut_none;
		bot->escape_until_tick = 0;
		bot->last_position = tick.object->object.position;
		bots_vehicle_forget(bot);
		bot->ride_vehicle_index = NONE;
		/* port: (its base's vehicles looked at as it spawns) */
		bot->spawn_tick = tick.now;
		/* port: (the biped's jump, for the hops: bots_nav.c) */
		if (biped_try_and_get(tick.player->unit_index))
			bots_nav_set_biped(tick.object->definition_index);
		bot->hop_index = NONE;
		bot->hop_via = NONE;
		bot->slow_ticks = 0;
		bot->vehicle_check_tick = 0;
		bot->last_body_vitality = tick.object->object.body_vitality;
		bot->item_powerup = _bots_powerup_none;
		bot->powerup_index = NONE;
	}
	/* port: riding, it is where its vehicle is (a rider's own position is
	its seat's) */
	vehicle_index = bots_unit_vehicle(tick.player->unit_index);
	tick.position = vehicle_index != NONE ? object_get(vehicle_index)->object.position : tick.object->object.position;
	if (vehicle_index == NONE && bot->ride_vehicle_index != NONE)
		bots_vehicle_got_out(bot, &tick);
	/* (the time riding, and kills riding: the census's) */
	if (vehicle_index != NONE && bot->ride_vehicle_index == vehicle_index)
		bots_globals.riding_ticks++;
	if (vehicle_index != NONE && bot->ride_vehicle_index == vehicle_index && tick.player->statistics.kills[0] > bot->kills_seen)
	{
		bots_globals.riding_kills[bot->ride_seat != _bots_seat_driver] += tick.player->statistics.kills[0] - bot->kills_seen;
		if (bot->ride_turret)
			bots_globals.turret_kills += tick.player->statistics.kills[0] - bot->kills_seen;
	}
	bot->kills_seen = tick.player->statistics.kills[0];
	/* (through a teleporter: a new path from where it came out; port: which
	one and how it came to it, halo.log's census) */
	if (bots_distance3d(&tick.position, &bot->last_position) > 3.0f && tick.object->object.parent_object_index == NONE)
		bots_teleporter_landed(bot, &bot->last_position, &tick.position, tick.now);
	if (bots_distance3d(&tick.position, &bot->last_position) > 3.0f && bot->goal_kind != _bots_goal_none &&
		tick.object->object.parent_object_index == NONE)
	{
		bot->via_teleporter = FALSE;
		bot->path.valid = FALSE;
		bot->path_tick = tick.now - TICKS_PER_SECOND;
		bot->stuck_position = tick.position;
	}
	bot->last_position = tick.position;

	/* hurt: it turns to look for who did it */
	vitality = tick.object->object.body_vitality + tick.object->object.shield_vitality;
	if (vitality < bot->last_vitality - 0.01f)
		bot->hurt_tick = tick.now;
	bot->last_vitality = vitality;
	/* port: its body restored (a body heals no other way: a health pack) */
	if (tick.object->object.body_vitality > bot->last_body_vitality + 0.2f)
		bots_globals.bodies_restored++;
	bot->last_body_vitality = tick.object->object.body_vitality;

	bots_profile_part(_bots_part_rest, &started);
	bots_bot_look(bot, &tick);
	bots_profile_part(_bots_part_look, &started);
	/* port: the enemy it sees called out to its team, or, hurt with none
	seen, where it is (bots_squads.c) */
	if (bot->target_player_index != NONE && tick.now - bot->target_seen_tick <= 1)
		bots_squads_callout(bot_index, bot->target_player_index, &bot->target_last_position, bot->target_last_surface_index);
	else if (tick.now - bot->hurt_tick <= 1 && vehicle_index == NONE)
	{
		real_point3d ground;
		long surface_index = bots_surface_below(&tick.position, &ground);

		if (surface_index != NONE)
			bots_squads_callout(bot_index, NONE, &ground, surface_index);
	}
	tick.weapon_index = bots_current_weapon(tick.unit);
	/* (the flag or the ball taken: halo.log's count) */
	{
		short held = 0;

		/* (the ball is a weapon_is_flag too) */
		if (tick.weapon_index != NONE && !infection_player_is_zombie(bot->player_index) &&
			(weapon_is_flag(tick.weapon_index) || bots_weapon_value(weapon_get(tick.weapon_index)->definition_index) < 0))
		{
			held = game_engine_get_variant()->game_engine_index == game_engine_ctf ? 1 : 2;
		}
		if (held && held != bot->held_objective)
		{
			if (held == 1)
				bots_globals.flag_grabs++;
			else
				bots_globals.ball_grabs++;
		}
		bot->held_objective = held;
	}
	/* (CTF: at the other team's stand, once a visit; halo.log's count) */
	if (game_engine_get_variant()->game_engine_index == game_engine_ctf && (tick.now % 10) == (bot_index % 10))
	{
		real_point3d stand;
		boolean at = bots_ctf_stand(tick.player->team_index ? 0 : 1, &stand) &&
			bots_distance3d(&stand, &tick.position) < 1.5f;

		if (at && !bot->at_enemy_stand)
			bots_globals.stand_visits++;
		bot->at_enemy_stand = at;
	}
	bots_profile_part(_bots_part_rest, &started);
	/* port: riding a vehicle: driving, gunning or riding along, and out
	(the lines profile's walk) */
	if (vehicle_index != NONE)
	{
		/* port: (a teleporter shortcut is on foot alone) */
		bots_shortcut_drop(bot, "riding");
		bots_bot_ride(bot, &tick, vehicle_index);
	}
	else
	{
		if (bot->target_player_index != NONE && tick.now - bot->target_seen_tick <= TICKS_PER_SECOND / 2)
			bots_bot_fight(bot, &tick);
		bots_profile_part(_bots_part_fight, &started);

		/* the goal, and the path to it (while fighting, only the game's
		objective, a weapon close by or a vehicle it goes for: towards it,
		still facing the enemy); port: a vehicle on the way to a goal far
		off */
		if (!tick.fighting || bot->goal_kind == _bots_goal_objective || bot->goal_kind == _bots_goal_item ||
			bot->goal_kind == _bots_goal_vehicle)
		{
			bots_choose_goal(bot, bot->player_index, &tick.position, tick.fighting);
		}
		/* port: hurt in a fight: a health pack near, backed towards as it
		fights */
		else if (!bot->held_objective)
			bots_powerup_goal(bot, tick.unit, &tick.position, TRUE);
		bots_vehicle_consider(bot, &tick);
		bots_turret_consider(bot, &tick);
		bots_profile_part(_bots_part_goal, &started);
		bots_search_take(bot, TRUE);
		/* port: (its teleporter shortcut: in time, or failed) */
		bots_shortcut_update(bot, &tick);
		bots_profile_part(_bots_part_take, &started);
		if (!tick.fighting)
		{
			bots_bot_walk(bot, &tick);
			bots_bot_yield(bot, bot_index, &tick);
		}
		else if (bot->goal_kind == _bots_goal_objective || bot->goal_kind == _bots_goal_item ||
			bot->goal_kind == _bots_goal_vehicle)
		{
			real direction;
			struct object_datum *item;

			if (bots_follow_path(bot, tick.player->unit_index, &tick.position, &direction))
				tick.move_direction = direction;
			/* port: (at a powerup: on to it, still fighting) */
			else if (bot->goal_kind == _bots_goal_item &&
				(item = object_try_and_get_and_verify_type(bot->item_index, _object_mask_item)) != NULL &&
				bots_distance2d(&item->object.position, &tick.position) > 0.3f)
			{
				tick.move_direction = (real)atan2(item->object.position.y - tick.position.y,
					item->object.position.x - tick.position.x);
				tick.moving = TRUE;
			}
		}
		/* port: fighting at its vantage point: there still, or off it with
		the enemy near (bots_tactics.h) */
		if (tick.fighting)
		{
			bots_tactics_fight(bot_index, &tick.position, tick.target_distance, &tick.moving, &tick.move_direction,
				&tick.throttle_scale);
		}
		bots_vehicle_approach(bot, &tick);
		bots_profile_part(_bots_part_walk, &started);
		bots_bot_weapons(bot, &tick);
		bots_bot_unstick(bot, &tick);
	}
	bots_profile_part(_bots_part_walk, &started);

	if (tick.moving)
	{
		real relative = bots_angle_difference(tick.move_direction, bot->yaw);

		action->throttle.i = (real)cos(relative) * tick.throttle_scale;
		action->throttle.j = (real)sin(relative) * tick.throttle_scale;
	}
	bot->pitch = PIN(bot->pitch, -1.4f, 1.4f);
	bot->yaw = bots_angle_difference(bot->yaw, 0.0f);
	action->desired_facing.yaw = bot->yaw;
	action->desired_facing.pitch = bot->pitch;

	/* port: a hop's jump, and the feet tucked in the air (a crouch jump: a
	hop's, or over a low wall); a teleporter or ladder hop come to its end,
	told (bots_nav.c) */
	if (bot->hop_press_jump)
	{
		tick.flags |= FLAG(_unit_control_jump_bit);
		bot->hop_press_jump = FALSE;
	}
	if (tick.now >= bot->crouch_from_tick && tick.now < bot->crouch_until_tick && vehicle_index == NONE)
		tick.flags |= FLAG(_unit_control_crouch_modifier_bit);
	if (bot->hop_via != NONE && !bot->via_teleporter)
	{
		struct bots_nav_hop const *hop = bots_nav_hop_get(bot->hop_via);
		real_point3d ground;
		long surface_index = vehicle_index == NONE ? bots_unit_surface(tick.player->unit_index, &ground) : NONE;

		if (hop)
		{
			bots_nav_hop_result(bot->hop_via, bots_nav_region(surface_index) != NONE &&
				bots_nav_region(surface_index) == bots_nav_region(hop->exit_surface_index), tick.now);
			if (getenv("HALO_BOT_TRACE"))
				platform_log("bots: trace bot %ld through hop %ld (kind %d) at (%.1f %.1f %.1f) region %ld, its exit's %ld",
					bot_index + 1, bot->hop_via, (int)hop->kind, tick.position.x, tick.position.y, tick.position.z,
					bots_nav_region(surface_index), bots_nav_region(hop->exit_surface_index));
		}
		bot->hop_via = NONE;
	}
	/* a press is one tick held and the next released (the jump, the grenade,
	the blow, the reload, the action button: port, into and out of a
	vehicle), as a player's buttons are; the trigger and the
	swap button may be held */
	{
		unsigned long pressed = FLAG(_unit_control_jump_bit) | FLAG(_unit_control_throw_grenade_bit) |
			FLAG(_unit_control_use_equipment_bit) | FLAG(_unit_control_weapon_reload_bit) |
			FLAG(_unit_control_action_bit);

		tick.flags &= ~(bot->previous_control_flags & pressed);
		bot->previous_control_flags = tick.flags;
	}
	action->control_flags = tick.flags;
	action->primary_trigger = TEST_FLAG(tick.flags, _unit_control_weapon_primary_trigger_bit) ? 1.0f : 0.0f;
	bots_play_count(bot, &tick);
	bots_bot_trace(bot, bot_index, &tick);
	bots_profile_part(_bots_part_rest, &started);
}

static void bots_report(void)
{
	long now = game_time_get();
	long count = 0, index;
	long fired = 0, hit = 0;
	char line[600];
	int length = 0;

	if (bots_globals.last_report_tick != NONE && now - bots_globals.last_report_tick < TICKS_PER_SECOND * 60)
		return;
	bots_globals.last_report_tick = now;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_bot *bot = &bots_globals.bots[index];
		struct player_datum *player = bot->player_index != NONE ? player_try_and_get(bot->player_index) : NULL;

		if (!player)
			continue;
		count++;
		fired += player->statistics.shots_fired;
		hit += player->statistics.shots_hit;
		if (length < (int)sizeof(line) - 40)
			length += snprintf(line + length, sizeof(line) - (size_t)length, " %ld:%d/%d", index + 1,
				(int)player->statistics.kills[0], (int)player->statistics.deaths);
	}
	line[length] = 0;
	if (!count || !bots_globals.ticks)
		return;
	platform_log("bots: tick %ld, %ld bots (%s): %.0f us/tick (%.1f a bot), searches %ld (%ld failed, %ld short, %ld full, %ld unavoided, %ld unsmoothed, tick %.0f us, "
		"helper %.0f us), shots %ld hit %ld, flag taken %ld (enemy stand reached %ld), ball taken %ld, kills/deaths%s", now, count,
		bots_skills[bots_skill()].name,
		(double)bots_globals.tick_us / (double)bots_globals.ticks,
		bots_globals.bot_ticks ? (double)bots_globals.tick_us / (double)bots_globals.bot_ticks : 0.0,
		bots_globals.searches, bots_globals.search_failures, bots_globals.search_partials, bots_globals.search_overflows,
		bots_globals.search_unavoided, bots_globals.search_unsmoothed,
		(double)bots_globals.search_us / (double)bots_globals.ticks,
		(double)bots_helper.helper_us / (double)bots_globals.ticks, fired, hit, bots_globals.flag_grabs,
		bots_globals.stand_visits, bots_globals.ball_grabs, line);
	platform_log("bots: moves at tick %ld: stuck %ld (a low wall %ld, a tall one %ld, nothing %ld), goals given up %ld; "
		"powerups gone for/taken: health %ld/%ld, overshield %ld/%ld, camouflage %ld/%ld; bodies restored %ld; "
		"ladders %ld, gone to %ld, climbed %ld; teleporters %ld (shortcuts: weighed %ld, taken %ld, through %ld, "
		"failed %ld, dropped %ld; through as the last way %ld, by chance %ld)",
		now, bots_globals.stuck_events, bots_globals.stuck_low, bots_globals.stuck_tall, bots_globals.stuck_clear,
		bots_globals.stuck_giveups, bots_globals.powerups_sought[_bots_powerup_health],
		bots_globals.powerups_taken[_bots_powerup_health], bots_globals.powerups_sought[_bots_powerup_overshield],
		bots_globals.powerups_taken[_bots_powerup_overshield], bots_globals.powerups_sought[_bots_powerup_camouflage],
		bots_globals.powerups_taken[_bots_powerup_camouflage], bots_globals.bodies_restored, bots_globals.ladder_count,
		bots_globals.ladder_tries, bots_globals.ladder_climbs, bots_globals.teleporter_count - bots_globals.ladder_count,
		bots_globals.shortcut_measures, bots_globals.shortcut_takes, bots_globals.shortcut_through[_bots_through_shortcut],
		bots_globals.shortcut_failures, bots_globals.shortcut_drops, bots_globals.shortcut_through[_bots_through_last_way],
		bots_globals.shortcut_through[_bots_through_by_chance]);
	/* port: (each teleporter's bots sent, by how they came to it, and its
	failures as a shortcut: the map's teleporters' census; a line more for
	every ten or so - Chiron TL34's thirty) */
	if (bots_globals.teleporter_count > bots_globals.ladder_count)
	{
		length = 0;
		for (index = 0; index < bots_globals.teleporter_count; index++)
		{
			if (!bots_globals.teleporters[index].ladder)
			{
				length += snprintf(line + length, sizeof(line) - (size_t)length,
					"%s %ld sent %ld (shortcut %ld, last way %ld, chance %ld) failed %ld", length ? ";" : "", index + 1,
					bots_teleporter_sent(index), bots_globals.teleporters[index].through[_bots_through_shortcut],
					bots_globals.teleporters[index].through[_bots_through_last_way],
					bots_globals.teleporters[index].through[_bots_through_by_chance], bots_globals.teleporters[index].failures);
			}
			if (length > 0 && (length > (int)sizeof(line) - 100 || index + 1 >= bots_globals.teleporter_count))
			{
				line[MIN(length, (int)sizeof(line) - 1)] = 0;
				platform_log("bots: teleporters at tick %ld:%s", now, line);
				length = 0;
			}
		}
	}
	platform_log("bots: play at tick %ld: stuck fighting %ld, walking with no path %ld; grenades %ld; jumps fighting %ld, "
		"moving %ld; weapons picked up %ld (power %ld); power weapon held %.1f%%, two weapons %.1f%% of the time; "
		"goals with no way %ld, strafes blocked %ld, low walls jumped %ld, jump shortcuts %ld",
		now, bots_globals.play.stuck_fighting, bots_globals.play.stuck_pathless, bots_globals.play.grenades_thrown,
		bots_globals.play.jumps_fighting, bots_globals.play.jumps_moving, bots_globals.play.weapon_pickups, bots_globals.play.power_pickups,
		bots_globals.play.alive_ticks ? 100.0 * bots_globals.play.power_ticks / bots_globals.play.alive_ticks : 0.0,
		bots_globals.play.alive_ticks ? 100.0 * bots_globals.play.two_weapon_ticks / bots_globals.play.alive_ticks : 0.0,
		bots_globals.play.goals_unreachable, bots_globals.play.fight_blocks, bots_globals.play.walk_jumps,
		bots_globals.play.jump_shortcuts);
	bots_nav_report(now);
	/* port: (the lanes, vantage points and posts: bots_tactics.h) */
	bots_tactics_report(now);
	if (bots_profile.enabled > 0)
	{
		double ticks = (double)bots_globals.ticks;

		platform_log("bots: tick %ld split (us/tick): look %.1f fight %.1f goal %.1f take %.1f walk %.1f rest %.1f scan %.1f | "
			"rays a tick: down %.2f (%.1f us) sight %.2f (%.1f us)", now,
			bots_profile.part_us[_bots_part_look] / ticks, bots_profile.part_us[_bots_part_fight] / ticks,
			bots_profile.part_us[_bots_part_goal] / ticks, bots_profile.part_us[_bots_part_take] / ticks,
			bots_profile.part_us[_bots_part_walk] / ticks, bots_profile.part_us[_bots_part_rest] / ticks,
			bots_profile.part_us[_bots_part_scan] / ticks, bots_profile.below_rays / ticks,
			bots_profile.below_us / ticks, bots_profile.sight_rays / ticks, bots_profile.sight_us / ticks);
	}
}

/* where bots play: a local (split screen) game's, and a network game's
host's - the dedicated server's (port/linux/game/dedicated_server.c,
sv_bots_fill) or a 1.1.1 host's online (bots.online): the host alone runs
them (network_server_manager.c's bot fill); its clients see players driven
by the input and the units the host sends, as any other machine's */
static boolean bots_play_here(void)
{
	return network_game_is_splitscreen_local() || global_network_game_server_get() != NULL;
}

void bots_update_actions(struct player_action *actions)
{
	struct data_iterator iterator;
	struct player_datum *player;
	unsigned long long started;
	long count = 0;

	if (!game_engine_running() || !bots_play_here())
		return;
	started = vita_host_time_us();
	bots_profile_enabled();
	bots_globals.skill = bots_skill();
	bots_globals.flags_found = FALSE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long bot_index;
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct bots_bot *bot;

		if (player->local_player_index != NONE || !bots_machine_is_bot(player->network_player_data.machine_index))
			continue;
		/* (the helper thread and the roaming places once a game has bots) */
		if (!bots_helper.started)
			bots_helper_start();
		if (!bots_globals.map_ready)
			bots_prepare_map();
		if (!count)
		{
			unsigned long long scan_started = bots_profile.enabled > 0 ? vita_host_time_us() : 0;

			bots_ground_memo_begin();
			bots_powerups_find();
			bots_paths_scan();
			/* port: the map's lanes and vantage points (a bounded step a
			tick), the roles (bots_tactics.h) */
			bots_tactics_update();
			/* port: the regions and the jumps (a bounded step a tick), the
			ramps opened to the searches */
			bots_nav_update();
			bots_paths_open_ramps();
			bots_profile_part(_bots_part_scan, &scan_started);
		}
		bot_index = player->network_player_data.machine_index - BOTS_FIRST_MACHINE;
		bot = &bots_globals.bots[bot_index];
		if (bot->player_index != iterator.datum_index)
			bots_bot_reset(bot, iterator.datum_index, bot_index);
		if (absolute_index >= 0 && absolute_index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
		{
			bots_bot_update(bot, bot_index, &actions[absolute_index]);
			/* (a network game host's bots: what its clients are sent of them
			is what they played, network_distributed.c) */
			if (!network_game_is_splitscreen_local())
				update_server_set_ticked_action((short)absolute_index, &actions[absolute_index]);
		}
		count++;
	}
	bots_ground_memo.active = FALSE;
	if (count)
	{
		bots_globals.tick_us += vita_host_time_us() - started;
		bots_globals.ticks++;
		bots_globals.bot_ticks += count;
		bots_report();
		bots_vehicle_census();
	}
}
