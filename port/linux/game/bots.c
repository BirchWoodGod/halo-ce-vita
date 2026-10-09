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

Skill (bots.skill): easy, normal, heroic, legendary - how soon it reacts,
how far and how wide it sees, how large its aim error is and how fast that
settles, how fast it turns, how tight its shots must be before it fires.

The simulation: a bot decides from the game state at the tick and its own
random numbers (seeded per bot and game), so the bots do the same in two
runs of the same input (HALO_TICK_HASH's fixed ticks). Nothing of a bot is in
the game state: multiplayer games are never saved. Bots are in local games
only, which no other machine sees: no message of the network protocol
changes (an internet or system link game with bots would need its clients
told which players are bots: a later network version).

The path searches - the part a bot's tick spends most of its time in - run
on a helper thread of their own where there is one to spare (the Vita's
fourth core, Fourth core helpers at All async; elsewhere the thread runs on
the cores the system gives it), on the level's static data only: a search
asked at one tick is taken at a later tick, and the tick never waits for
one (with HALO_FIXED_TICK, for a hash run, they run in the tick instead, so
the bots stay the same from run to run). halo.log has each minute of play's
bot line: their ticks' time (us/tick), the searches and how many failed.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "items/items.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "items/projectile_definitions.h"
#include "items/equipment.h"
#include "items/equipment_definitions.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "physics/collision_bsp_definitions.h"
#include "networking/network_game_globals.h"
#include "ai/path.h"
#include "devices/devices.h"
#include "devices/device_definitions.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"

#include "bots.h"

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
/* game_engine.c's (port): the nav points a player is shown */
long game_engine_port_player_goals(long player_index, real_point3d *positions, long maximum);

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
/* the most teleporters a map's bots use (Chiron TL34 has 30) */
#define BOTS_MAXIMUM_TELEPORTERS 48
/* a pathfinding surface's flag: walkable (path.c's) */
#define BOTS_PATHFINDING_WALKABLE_BIT 6

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
};

/* a path search's state (the helper thread's hand-off) */
enum
{
	_bots_search_idle = 0,
	_bots_search_waiting,
	_bots_search_running,
	_bots_search_done,
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
};

static struct bots_skill_definition const bots_skills[NUMBER_OF_BOTS_SKILLS] =
{
	{ "easy", 0.75f, 22.0f, 100.0f, 12.0f, 1.4f, 8.0f, 2.2f, 0.3f, 0.04f, 0.00f, 6, 14 },
	{ "normal", 0.45f, 32.0f, 120.0f, 7.0f, 0.9f, 13.0f, 1.8f, 0.7f, 0.10f, 0.05f, 9, 9 },
	{ "heroic", 0.30f, 42.0f, 140.0f, 4.0f, 0.6f, 20.0f, 1.5f, 0.8f, 0.16f, 0.15f, 12, 6 },
	{ "legendary", 0.18f, 55.0f, 160.0f, 2.2f, 0.35f, 30.0f, 1.25f, 1.0f, 0.22f, 0.25f, 16, 4 },
};

/* a path search, for the helper thread (or the tick) */
struct bots_search
{
	volatile long state;
	/* the asking: from where, to where, by whom */
	long bot_index;
	long unit_index;
	real_point3d start_point;
	long start_surface_index;
	real_point3d goal_point;
	long goal_surface_index;
	/* asked at this tick, for this goal (the bot's goal serial) */
	long asked_tick;
	long goal_serial;
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
	long unreachable_item_index;
	long unreachable_item_until_tick;
	/* whether it moves, checked once a second */
	real_point3d stuck_position;
	long stuck_tick;
	short stuck_count;
	long unstick_until_tick;
	real unstick_direction;

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
};

/* ---------- globals */

static struct
{
	boolean map_ready;
	long roam_point_count;
	real_point3d roam_points[BOTS_MAXIMUM_ROAM_POINTS];
	long roam_surfaces[BOTS_MAXIMUM_ROAM_POINTS];
	/* the teleporters: where each is entered (its ground and surface) and
	where it comes out */
	long teleporter_count;
	struct
	{
		real_point3d entrance;
		long entrance_surface_index;
		real_point3d exit;
	} teleporters[BOTS_MAXIMUM_TELEPORTERS];
	struct bots_bot bots[BOTS_MAXIMUM];
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
	long flag_grabs;
	long ball_grabs;
	/* CTF: the times a bot stood at the other team's flag stand (with the
	flag there or not) */
	long stand_visits;
	long last_report_tick;
} bots_globals;

/* the walkable surfaces the bots' searches read: a copy of the structure's
pathfinding surfaces, less those learnt to be in the way (scenery and
machines) and those with no room above them */
static struct
{
	boolean active;
	struct structure_bsp const *source;
	struct structure_bsp structure;
	byte *walkable;
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

/* the surface of the level under a point (a step up included), and the
ground point there; NONE over nothing */
static long bots_surface_below(real_point3d const *point, real_point3d *ground)
{
	struct collision_result collision;
	real_point3d start = *point;
	real_vector3d down = { 0.0f, 0.0f, -6.0f };

	start.z += 0.4f;
	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		&start, &down, NONE, &collision) && collision.type == _collision_result_structure)
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
static real bots_projectile_speed(long weapon_index)
{
	struct weapon_definition *definition = weapon_definition_get(weapon_get(weapon_index)->definition_index);

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
static short bots_weapon_value(long definition_index)
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
static short bots_weapon_tap_ticks(long weapon_index)
{
	struct weapon_definition *definition = weapon_definition_get(weapon_get(weapon_index)->definition_index);
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
static real bots_weapon_range(long weapon_index)
{
	char const *name = weapon_index != NONE ? tag_get_name(weapon_get(weapon_index)->definition_index) : NULL;

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

/* an item on the ground worth fetching near the bot: a weapon better than
the one it holds, or a powerup; NONE for none */
static long bots_find_item(struct bots_bot *bot, struct unit_datum *unit, real_point3d const *position,
	real maximum_distance)
{
	struct object_iterator iterator;
	long best_index = NONE;
	real best_score = REAL_MAX;
	long current = bots_current_weapon(unit);
	short current_value = current != NONE ? bots_weapon_value(weapon_get(current)->definition_index) : 0;

	object_iterator_new(&iterator, _object_mask_weapon | _object_mask_equipment, 0);
	while (object_iterator_next(&iterator))
	{
		struct item_datum *item = item_get(iterator.index);
		real distance, score;
		short value;

		if (item->object.parent_object_index != NONE ||
			(iterator.index == bot->unreachable_item_index && game_time_get() < bot->unreachable_item_until_tick) ||
			!TEST_FLAG(item->object.flags, _object_connected_to_map_bit) ||
			TEST_FLAG(item->item.flags, _item_attached_to_unit_bit))
		{
			continue;
		}
		distance = bots_distance3d(&item->object.position, position);
		if (distance > maximum_distance)
			continue;
		if (item->object.type == _object_type_weapon)
		{
			short slot;
			boolean held = FALSE;

			value = bots_weapon_value(item->definition_index);
			/* (one it holds already: only its ammunition, which walking
			over takes) */
			for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
			{
				long other = unit->unit.weapon_object_indices[slot];

				if (other != NONE && weapon_get(other)->definition_index == item->definition_index)
					held = TRUE;
			}
			if (held || value <= current_value)
				continue;
			score = distance - (real)(value - current_value) * 3.0f;
		}
		else
		{
			struct equipment_definition *definition = equipment_definition_get(item->definition_index);

			if (definition->equipment.powerup_type != _equipment_powerup_overshield &&
				definition->equipment.powerup_type != _equipment_powerup_active_camouflage &&
				!(definition->equipment.powerup_type == _equipment_powerup_health &&
					unit->object.body_vitality < 0.6f))
			{
				continue;
			}
			score = distance - 6.0f;
		}
		if (score < best_score)
		{
			best_score = score;
			best_index = iterator.index;
		}
	}
	return best_index;
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
	if (!bots_paths.walkable)
		return FALSE;
	csmemcpy(bots_paths.walkable, structure->pathfinding_surfaces.address, (size_t)count);
	bots_paths.structure = *structure;
	bots_paths.structure.pathfinding_surfaces.address = bots_paths.walkable;
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
			closed++;
		}
	}
	bots_paths.closed_count += closed;
	platform_log("bots: %s %s at (%.1f %.1f %.1f): %ld walkable surfaces closed (%.1f ms)", why,
		tag_get_name(object->definition_index), object->object.bounding_sphere_center.x,
		object->object.bounding_sphere_center.y, object->object.bounding_sphere_center.z, closed,
		(double)(vita_host_time_us() - started) / 1000.0);
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
			bots_paths.roofed_count++;
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
		teleporter's base) */
		if (definition->object.collision_model.index == NONE ||
			bots_distance2d(&object->object.bounding_sphere_center, position) > object->object.bounding_sphere_radius + 4.5f ||
			fabs(object->object.bounding_sphere_center.z - position->z) > object->object.bounding_sphere_radius + 2.0f ||
			bots_object_holds(object, position) || bots_object_holds(object, goal) ||
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

/* the places a bot roams to: the player starting locations, the weapon and
item spawns, the hills (once the level's BSP is in) */
static void bots_prepare_map(void)
{
	struct scenario *scenario = global_scenario_get();
	long index;

	bots_globals.map_ready = TRUE;
	bots_globals.roam_point_count = 0;
	bots_prepare_paths();
	if (!scenario)
		return;
	for (index = 0; index < scenario->netgame_equipment.count; index++)
	{
		struct scenario_netgame_equipment *equipment =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, index, struct scenario_netgame_equipment);

		bots_add_roam_point(&equipment->position);
	}
	/* (the teleporters: a source flag's channel is its team index, and the
	target flag of that channel is where it comes out) */
	bots_globals.teleporter_count = 0;
	for (index = 0; index < scenario->netgame_flags.count && bots_globals.teleporter_count < BOTS_MAXIMUM_TELEPORTERS; index++)
	{
		struct scenario_netgame_flag *source =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);
		long target_index;

		if (source->type != _netgame_flag_teleporter_source)
			continue;
		for (target_index = 0; target_index < scenario->netgame_flags.count; target_index++)
		{
			struct scenario_netgame_flag *target =
				TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, target_index, struct scenario_netgame_flag);
			long teleporter = bots_globals.teleporter_count;

			if (target->type != _netgame_flag_teleporter_target || target->team_index != source->team_index)
				continue;
			bots_globals.teleporters[teleporter].entrance_surface_index =
				bots_surface_below(&source->position, &bots_globals.teleporters[teleporter].entrance);
			bots_globals.teleporters[teleporter].exit = target->position;
			if (bots_globals.teleporters[teleporter].entrance_surface_index != NONE)
				bots_globals.teleporter_count++;
			break;
		}
	}
	for (index = 0; index < scenario->players.count; index += 2)
	{
		struct player_starting_location *location =
			TAG_BLOCK_GET_ELEMENT(&scenario->players, index, struct player_starting_location);

		bots_add_roam_point(&location->position);
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
	bots_globals.flag_grabs = 0;
	bots_globals.ball_grabs = 0;
	bots_globals.stand_visits = 0;
	/* (the first line a minute into the game) */
	bots_globals.last_report_tick = 0;
	bots_helper.helper_us = 0;
}

void bots_dispose_from_old_map(void)
{
	/* (a search still running finishes on the old level's data, which is
	still loaded; the answers are dropped) */
	bots_paths_release();
	bots_globals.map_ready = FALSE;
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

	path_input_new(&input, BOTS_PATH_RADIUS, FALSE, search->unit_index);
	path_input_set_start(&input, &search->start_point, search->start_surface_index);
	path_state_new(&input, &search->state_memory, NULL);
	/* (the walkable surfaces less those closed by what is in the way) */
	pthread_mutex_lock(&bots_helper.lock);
	if (bots_paths.active && search->state_memory.structure == bots_paths.source)
		search->state_memory.structure = &bots_paths.structure;
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

/* asks for a path from where the bot's unit is to its goal */
static void bots_search_ask(struct bots_bot *bot, long unit_index)
{
	struct bots_search *search = NULL;
	real_point3d start;
	long start_surface_index;
	long slot;

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
	start_surface_index = bots_unit_surface(unit_index, &start);
	if (start_surface_index == NONE || bot->goal_surface_index == NONE)
		return;
	/* (on a surface closed to the searches, whose way out may be closed
	too: away from what closed it first, a second) */
	if (bots_paths.walkable && start_surface_index < bots_paths.structure.pathfinding_surfaces.count &&
		!TEST_FLAG(bots_paths.walkable[start_surface_index], BOTS_PATHFINDING_WALKABLE_BIT) &&
		bots_paths_escape_direction(&start, &bot->escape_direction))
	{
		bot->escape_until_tick = game_time_get() + TICKS_PER_SECOND;
		bot->path.valid = FALSE;
		bot->path_tick = game_time_get();
		return;
	}
	bot->search_slot = (long)(search - bots_globals.search_slots);
	search->bot_index = (long)(bot - bots_globals.bots);
	search->unit_index = unit_index;
	search->start_point = start;
	search->start_surface_index = start_surface_index;
	search->goal_point = bot->goal_point;
	search->goal_surface_index = bot->goal_surface_index;
	search->asked_tick = game_time_get();
	search->goal_serial = bot->goal_serial;
	search->found = FALSE;
	bots_globals.searches++;
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
	return TRUE;
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
	short node_index, child_index = NONE;
	boolean steps_finish_path = TRUE;
	real_point3d endpoint;

	csmemset(path, 0, sizeof(*path));
	if (!state->destination_valid || state->node_count <= 0)
		return FALSE;
	node_index = path_node_from_hash_table(state, state->destination.surface_index);
	endpoint = state->destination.point;
	if (node_index == NONE)
	{
		node_index = state->closest_node_index;
		endpoint = state->closest_point;
	}
	if (node_index < 0 || node_index >= state->node_count)
		return FALSE;
	raw_step_count = (short)MIN(state->node_list[node_index].depth + 1, 64);
	while (node_index != NONE)
	{
		struct path_node *node;

		if (node_index < 0 || node_index >= state->node_count)
			return FALSE;
		node = &state->node_list[node_index];
		if (node->depth >= 64)
			steps_finish_path = FALSE;
		else if (node->depth >= 0 && node->depth < raw_step_count)
		{
			raw_steps[node->depth].surface_index = node->surface_index;
			raw_steps[node->depth].point = child_index == NONE ? endpoint : state->node_list[child_index].entry_point;
		}
		else
			return FALSE;
		child_index = node_index;
		node_index = node->parent_node_index;
	}
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
	/* (something new learnt: a new search, round it) */
	if (bots_paths_learn(&search->start_point, &search->goal_point) > 0)
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
	if (alive && search->goal_serial == bot->goal_serial)
	{
		struct path_state *state = &search->state_memory;
		struct path_result path;
		unsigned long long started = vita_host_time_us();
		boolean reached = state->node_count > 0 &&
			path_node_from_hash_table(state, search->goal_surface_index) != NONE;

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
		if (bots_search_build(bot, search, reached, &path))
		{
			bot->path = path;
			bot->path.step_index = 0;
			bot->path_failures = 0;
			bot->path_short = !reached && !bot->via_teleporter;
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
		/* (an item it finds no way to: another, and not this one a while) */
		if (bot->goal_kind == _bots_goal_item && !reached && !bot->via_teleporter)
		{
			bot->unreachable_item_index = bot->item_index;
			bot->unreachable_item_until_tick = game_time_get() + TICKS_PER_SECOND * 30;
			bot->goal_kind = _bots_goal_none;
			bot->path.valid = FALSE;
		}
		bot->path_tick = game_time_get();
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

	vector.i = point->x - eye->x;
	vector.j = point->y - eye->y;
	vector.k = point->z - eye->z;
	if (!collision_test_vector(_collision_test_for_line_of_sight_flags, eye, &vector, unit_index, &collision))
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

/* the enemy the bot sees best now, NONE for none */
static long bots_find_target(struct bots_bot *bot, struct player_datum *player, struct unit_datum *unit,
	real_point3d const *eye)
{
	struct bots_skill_definition const *skill = &bots_skills[bots_skill()];
	struct data_iterator iterator;
	struct player_datum *other;
	long best_index = NONE;
	real best_score = REAL_MAX;
	real half_view = 0.5f * skill->field_of_view_degrees * BOTS_DEGREES;
	boolean hurt = game_time_get() - bot->hurt_tick < TICKS_PER_SECOND * 2;

	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum *object;
		struct unit_datum *other_unit;
		real distance, reach, bearing, score;
		real_point3d point;

		if (!bots_is_enemy(player, other))
			continue;
		object = object_get(other->unit_index);
		other_unit = unit_get(other->unit_index);
		if (TEST_FLAG(object->object.damage_flags, _object_dead_bit))
			continue;
		point = object->object.bounding_sphere_center;
		distance = bots_distance3d(eye, &point);
		reach = skill->sight_distance;
		/* (active camouflage: seen only close) */
		if (TEST_FLAG(other_unit->unit.flags, _unit_active_camouflaged_bit))
			reach *= 0.3f;
		/* (the one it fights: a little further) */
		if (iterator.datum_index == bot->target_player_index)
			reach *= 1.25f;
		if (distance > reach)
			continue;
		bearing = (real)fabs(bots_angle_difference((real)atan2(point.y - eye->y, point.x - eye->x), bot->yaw));
		/* (out of view: unless close by, it shot the bot, or it is the one
		the bot fights) */
		if (bearing > half_view && distance > 3.0f && !hurt && iterator.datum_index != bot->target_player_index)
			continue;
		score = distance + bearing * 4.0f - (iterator.datum_index == bot->target_player_index ? 6.0f : 0.0f);
		if (score >= best_score)
			continue;
		if (!bots_line_of_sight(player->unit_index, eye, &point, other->unit_index))
			continue;
		best_score = score;
		best_index = iterator.datum_index;
	}
	(void)unit;
	return best_index;
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
	bot->goal_serial++;
	bot->path.valid = FALSE;
	bot->path_failures = 0;
	bot->climb_until_tick = 0;
}

/* the next place to roam to: most often towards an enemy (the roaming place
nearest a living enemy, as a player knows where the other team's base is
and reads the motion tracker), else anywhere */
static void bots_pick_roam_goal(struct bots_bot *bot, real_point3d const *position)
{
	long tries;

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

				if (distance < nearest_distance && bots_distance2d(&bots_globals.roam_points[index], position) > 4.0f)
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
	for (tries = 0; tries < 6; tries++)
	{
		short index = (short)(bots_random(bot) % (unsigned long)bots_globals.roam_point_count);

		/* (not where it is) */
		if (bots_distance2d(&bots_globals.roam_points[index], position) < 6.0f && tries < 5)
			continue;
		bot->roam_index = index;
		bots_set_goal(bot, _bots_goal_roam, &bots_globals.roam_points[index], bots_globals.roam_surfaces[index]);
		return;
	}
}

/* CTF: its own team's flag stand (the scenario's netgame flag), FALSE for
none */
static boolean bots_ctf_stand(long team_index, real_point3d *position)
{
	struct scenario *scenario = global_scenario_get();
	long index;

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

/* CTF's own goals: carrying the enemy flag, its team's stand (the HUD's nav
point is beside it, and a capture needs the carrier on it), or its own
flag's carrier while its own flag is carried off; its own flag
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

	if (game_engine_get_variant()->game_engine_index != game_engine_ctf ||
		!bots_ctf_stand(player->team_index, &stand))
	{
		return FALSE;
	}
	if (weapon_index != NONE && weapon_is_flag(weapon_index))
	{
		/* (its own flag carried off: no capture till it is home, after its
		carrier) */
		object_iterator_new(&iterator, _object_mask_weapon, 0);
		while (object_iterator_next(&iterator))
		{
			struct weapon_datum *flag = weapon_get(iterator.index);

			if (weapon_is_flag(iterator.index) && flag->object.owner_team_index == player->team_index &&
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
	while (object_iterator_next(&iterator))
	{
		struct weapon_datum *flag = weapon_get(iterator.index);

		if (!weapon_is_flag(iterator.index))
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

/* where the bot goes: the game's nav points for it, the enemy last seen, or
about the map */
static void bots_choose_goal(struct bots_bot *bot, long player_index, real_point3d const *position, boolean fighting)
{
	real_point3d goals[8];
	long goal_count;
	long now = game_time_get();

	bot->goal_exact = FALSE;
	/* (a while from the game's goals, which it found no way to) */
	if (now < bot->objective_rest_until_tick)
		goal_count = 0;
	else
	{
		if (bots_ctf_goal(bot, player_get(player_index), position))
			return;
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
		now - bot->target_seen_tick < TICKS_PER_SECOND * 8)
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
			bot->unreachable_item_index = bot->item_index;
			bot->unreachable_item_until_tick = now + TICKS_PER_SECOND * 30;
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

/* the direction the bot walks (radians), following its path; FALSE when
it has nowhere to go */
static boolean bots_follow_path(struct bots_bot *bot, long unit_index, real_point3d const *position, real *direction)
{
	long now = game_time_get();

	if (bot->goal_kind == _bots_goal_none)
		return FALSE;
	if (now < bot->escape_until_tick)
	{
		*direction = bot->escape_direction;
		return TRUE;
	}
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

			if (bots_distance2d(&step->point, position) > 0.35f)
				break;
			bot->path.step_index++;
		}
		if (bot->path.step_index >= bot->path.step_count)
		{
			bot->path.valid = FALSE;
			/* (as near as the search came, the goal on above or beyond:
			on towards it) */
			if (bot->path.steps_finish_path && bot->path_short && bots_distance3d(&bot->goal_point, position) > 1.5f &&
				!bot->climb_until_tick)
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
	/* (at the teleporter: onto it, a few seconds at most) */
	if (bot->via_teleporter)
	{
		if (now - bot->via_tick < TICKS_PER_SECOND * 4 && bots_distance2d(&bot->via_point, position) > 0.1f)
		{
			*direction = (real)atan2(bot->via_point.y - position->y, bot->via_point.x - position->x);
			return TRUE;
		}
		bot->via_teleporter = FALSE;
		bots_search_ask(bot, unit_index);
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
}

/* a bot's tick: what it knows of itself, and the controls it builds */
struct bots_tick
{
	struct bots_skill_definition const *skill;
	struct player_datum *player;
	struct unit_datum *unit;
	struct object_datum *object;
	long now;
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

/* looking for enemies (every third tick, the bots in turn; (debug)
HALO_BOT_PEACEFUL=1: never, for the moving alone), and forgetting the one
gone a while */
static void bots_bot_look(struct bots_bot *bot, struct bots_tick *tick)
{
	if (tick->now >= bot->next_look_tick && !bots_peaceful())
	{
		long target = bots_find_target(bot, tick->player, tick->unit, &tick->eye);

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
	/* a blow close up */
	if (tick->target_distance < 1.1f && now >= bot->melee_tick)
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
		if (tick->unit->unit.grenade_counts[(long)tick->unit->unit.current_grenade_index] <= 0)
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

/* stuck: not moving for a second while it walks - a jump and a sidestep,
then another path; somewhere else after three */
static void bots_bot_unstick(struct bots_bot *bot, struct bots_tick *tick)
{
	long now = tick->now;

	if (now - bot->stuck_tick >= TICKS_PER_SECOND)
	{
		if (tick->moving && bots_distance2d(&tick->position, &bot->stuck_position) < 0.4f &&
			tick->object->object.parent_object_index == NONE)
		{
			bot->stuck_count++;
			bot->unstick_until_tick = now + TICKS_PER_SECOND / 2;
			bot->unstick_direction = tick->move_direction + (bots_random_real(bot) < 0.5f ? -1.0f : 1.0f) * BOTS_PI * 0.5f;
			bot->path.valid = FALSE;
			if (bot->stuck_count >= 3)
			{
				bot->goal_kind = _bots_goal_none;
				bot->stuck_count = 0;
				if (bots_globals.roam_point_count > 0)
					bots_pick_roam_goal(bot, &tick->position);
			}
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
		if (now == bot->unstick_until_tick - TICKS_PER_SECOND / 2)
			tick->flags |= FLAG(_unit_control_jump_bit);
	}
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

static void bots_bot_update(struct bots_bot *bot, long bot_index, struct player_action *action)
{
	struct bots_tick tick;
	real vitality;

	csmemset(&tick, 0, sizeof(tick));
	tick.skill = &bots_skills[bots_skill()];
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
		bot->escape_until_tick = 0;
		bot->last_position = tick.object->object.position;
	}
	unit_get_head_position(tick.player->unit_index, &tick.eye);
	tick.position = tick.object->object.position;
	/* (through a teleporter: a new path from where it came out) */
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

	bots_bot_look(bot, &tick);
	tick.weapon_index = bots_current_weapon(tick.unit);
	/* (the flag or the ball taken: halo.log's count) */
	{
		short held = 0;

		/* (the ball is a weapon_is_flag too) */
		if (tick.weapon_index != NONE &&
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
	if (bot->target_player_index != NONE && tick.now - bot->target_seen_tick <= TICKS_PER_SECOND / 2)
		bots_bot_fight(bot, &tick);

	/* the goal, and the path to it (while fighting, only the game's
	objective or a weapon close by: towards it, still facing the enemy) */
	if (!tick.fighting || bot->goal_kind == _bots_goal_objective || bot->goal_kind == _bots_goal_item)
		bots_choose_goal(bot, bot->player_index, &tick.position, tick.fighting);
	bots_search_take(bot, TRUE);
	if (!tick.fighting)
	{
		bots_bot_walk(bot, &tick);
		bots_bot_yield(bot, bot_index, &tick);
	}
	else if (bot->goal_kind == _bots_goal_objective || bot->goal_kind == _bots_goal_item)
	{
		real direction;

		if (bots_follow_path(bot, tick.player->unit_index, &tick.position, &direction))
			tick.move_direction = direction;
	}
	bots_bot_weapons(bot, &tick);
	bots_bot_unstick(bot, &tick);

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

	/* a press is one tick held and the next released (the jump, the grenade,
	the blow, the reload), as a player's buttons are; the trigger and the
	swap button may be held */
	{
		unsigned long pressed = FLAG(_unit_control_jump_bit) | FLAG(_unit_control_throw_grenade_bit) |
			FLAG(_unit_control_use_equipment_bit) | FLAG(_unit_control_weapon_reload_bit);

		tick.flags &= ~(bot->previous_control_flags & pressed);
		bot->previous_control_flags = tick.flags;
	}
	action->control_flags = tick.flags;
	action->primary_trigger = TEST_FLAG(tick.flags, _unit_control_weapon_primary_trigger_bit) ? 1.0f : 0.0f;
	bots_bot_trace(bot, bot_index, &tick);
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
	platform_log("bots: tick %ld, %ld bots (%s): %.0f us/tick (%.1f a bot), searches %ld (%ld failed, %ld short, %ld full, %ld unavoided, tick %.0f us, "
		"helper %.0f us), shots %ld hit %ld, flag taken %ld (enemy stand reached %ld), ball taken %ld, kills/deaths%s", now, count,
		bots_skills[bots_skill()].name,
		(double)bots_globals.tick_us / (double)bots_globals.ticks,
		bots_globals.bot_ticks ? (double)bots_globals.tick_us / (double)bots_globals.bot_ticks : 0.0,
		bots_globals.searches, bots_globals.search_failures, bots_globals.search_partials, bots_globals.search_overflows,
		bots_globals.search_unavoided,
		(double)bots_globals.search_us / (double)bots_globals.ticks,
		(double)bots_helper.helper_us / (double)bots_globals.ticks, fired, hit, bots_globals.flag_grabs,
		bots_globals.stand_visits, bots_globals.ball_grabs, line);
}

void bots_update_actions(struct player_action *actions)
{
	struct data_iterator iterator;
	struct player_datum *player;
	unsigned long long started;
	long count = 0;

	if (!game_engine_running() || !network_game_is_splitscreen_local())
		return;
	started = vita_host_time_us();
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
			bots_paths_scan();
		bot_index = player->network_player_data.machine_index - BOTS_FIRST_MACHINE;
		bot = &bots_globals.bots[bot_index];
		if (bot->player_index != iterator.datum_index)
			bots_bot_reset(bot, iterator.datum_index, bot_index);
		if (absolute_index >= 0 && absolute_index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
			bots_bot_update(bot, bot_index, &actions[absolute_index]);
		count++;
	}
	if (count)
	{
		bots_globals.tick_us += vita_host_time_us() - started;
		bots_globals.ticks++;
		bots_globals.bot_ticks += count;
		bots_report();
	}
}
