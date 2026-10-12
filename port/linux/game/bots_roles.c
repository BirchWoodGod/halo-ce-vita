/*
BOTS_ROLES.C

(port) The offline bots' roles (bots.c's bots, in every Bot routes mode),
and what the bots do with their routes (bots_routes.c) and the map's
vantage points (bots_vantage.c): each bot's part, steered from bots.c's
goal, path, walk and fight by the hooks of bots_tactics.h.

The roles, once a second, of each team's bots (in a game without teams, of
all of them) by their order in the team, so not every bot rushes the flag:
- CTF: attackers, with four bots or more every third a defender and one a
  sniper (two with ten; three bots a team are all attackers, a third of them
  at home was too many). A defender keeps about its own flag stand: its
  posts (bots_vantage.c: the stand, the best vantage spot near it that sees
  it, the direct route a fifth of the way out) in turn every fifteen to
  twenty-five seconds, watching the way the enemy comes; its flag taken or
  lying away from home, it goes after it as every bot does (bots.c's CTF
  goals come first). No defenders in Invasion (the flags at the other base).
- King of the Hill: attackers to the hill, with four bots or more every
  third a defender over it from a vantage point that sees it (and the hill
  itself when none does), and a sniper.
- Oddball: attackers, a sniper with four or more. Race: attackers alone.
- Slayer: roamers (bots.c's roaming, after the enemy), a sniper with five
  or more (two with ten).
The attackers and roamers are the ones Lanes and Mixed spread over the
lanes (bots_routes_lane_for_rank), by their order among them.

Vantage points: a sniper, King's defenders, and a bot holding a precision
weapon (bots.c's bots_weapon_precision, from the weapon's tag: a zoom, one
bullet a shot, little error; or a heavy one, a zoom and one slow
projectile) go to a vantage point (the best by its score and nearness
that no other bot has, so no two stand on one; not the one it held last,
nor one it found no way to; a third of a team's bots at most at once, half
when it holds a long precision weapon) and
hold it a while, looking from one place it sees to another (bots_tactics_
look); fighting there, it keeps to the spot, and through a long precision
weapon's scope it sees half as far again. By the weapon:
- a long precision weapon (two zoom levels or a scope of 4x and up: the
  sniper rifle) is held as a sniper holds it, 35 to 50 seconds, and taken
  to another 10 to 20 seconds after (8 to 15 after a fight close up; at
  once when taken up after a pistol's or a rocket's time at one); a CTF
  defender holding one takes a vantage point that sees its own flag stand
  (40 units from it at most), else keeps to its posts; King of the Hill's
  attackers keep to the hill whatever they hold.
- a middle one (a zoom alone: the pistol), a lighter liking: a roamer
  (Slayer) alone, to a vantage point within 35 units of it, one bot a team
  at once (not counted in the third: it keeps no sniper from one), 12 to 20
  seconds, then about the map 30 to 45 seconds; the enemy within six units,
  it leaves (the pistol fights well there).
- a heavy one (the rocket launcher), as before, 20 to 30 seconds.
King's defenders hold theirs 25 to 40 seconds. A bot leaves when an enemy
comes within nine units (and fights), when its weapon is nearly empty, when
it is hurt (its body below a third: the health packs come first then), when
the precision weapon it went for is dropped or swapped for another (a long
or heavy one for one of those; a middle one for any precision weapon: a
pistol's bot that takes up a sniper rifle stays, and holds it as a sniper
rifle's from then), when the game's objective
comes first (the flag or the ball carried, its own flag to bring back or
its carrier to chase: bots.c's CTF goals), or after its time; then the
game's goals a while (a sniper 10 to 20 seconds, the others 30 to 45; 3
after its weapon changed, 10 after the objective) before another. One it
does not come to within a minute, or finds no way to, is left be a minute. halo.log has each bot's arrival at
and leaving of a vantage point, the roles when they change and, each
minute, the tactics line (lanes taken, waypoints passed and given up,
vantage points chosen, reached and left and why, defenders' posts). The
precision weapons the bots find on the map are each logged once a map.

(debug) HALO_BOT_ROLES=0: neither roles nor vantage points (the lanes still).
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "networking/network_game_globals.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"

#include "bots.h"
#include "bots_tactics.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* game_engine.c's (port): the nav points a player is shown */
long game_engine_port_player_goals(long player_index, real_point3d *positions, long maximum);

/* ---------- constants */

#define BOTS_TACTICS_PI 3.14159265f
/* how near its vantage point a bot is there */
#define BOTS_VANTAGE_ARRIVED 2.0f
/* an enemy this near: off the vantage point, to fight (a middle precision
weapon's bot: nearer) */
#define BOTS_VANTAGE_ENEMY_NEAR 9.0f
#define BOTS_VANTAGE_ENEMY_NEAR_MID 6.0f
/* a middle precision weapon's bot: a vantage point this near it at most */
#define BOTS_VANTAGE_MID_REACH 35.0f
/* how much further a bot sees through a long precision weapon's scope at
its vantage point */
#define BOTS_VANTAGE_SCOPE_SIGHT 1.5f
/* the most teams the roles are counted by */
#define BOTS_TACTICS_TEAMS 16

/* ---------- globals */

struct bots_tactics_bot bots_tactics_bots[BOTS_MAXIMUM];
struct bots_tactics_counts bots_tactics_counts;

static struct
{
	long game;
	boolean assault;
	/* the map's lanes and vantage points made, since when, the work's time */
	boolean prepared;
	long prepare_ticks;
	unsigned long long prepare_us;
	unsigned long long prepare_most_us;
	/* when the roles are next given; their last line (halo.log's, when it
	changes) */
	long roles_tick;
	char roles_line[600];
	/* each team's bots */
	long team_bots[BOTS_TACTICS_TEAMS];
} bots_tactics;

/* ---------- small helpers */

boolean bots_tactics_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		char const *setting = getenv("HALO_BOT_ROLES");

		enabled = setting && setting[0] && !atoi(setting) ? 0 : 1;
	}
	return enabled != 0;
}

long bots_tactics_game(void)
{
	return bots_tactics.game;
}

unsigned long bots_tactics_random(struct bots_tactics_bot *state)
{
	/* (xorshift32) */
	unsigned long x = state->random ? state->random : 0x9E3779B9UL;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	x &= 0xFFFFFFFFUL;
	state->random = x;
	return x;
}

/* seconds, from `low` to `high`, in ticks */
static long bots_tactics_random_ticks(struct bots_tactics_bot *state, long low, long high)
{
	return TICKS_PER_SECOND * low + (long)(bots_tactics_random(state) % (unsigned long)(TICKS_PER_SECOND * (high - low) + 1));
}

static real bots_tactics_distance2d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y;

	return (real)sqrt(dx * dx + dy * dy);
}

static char const *bots_role_name(short role)
{
	static char const *const names[NUMBER_OF_BOTS_ROLES] = { "attacker", "defender", "sniper", "roamer" };

	return role >= 0 && role < NUMBER_OF_BOTS_ROLES ? names[role] : "?";
}

/* the unit's current weapon, NONE without one */
static long bots_tactics_weapon(long unit_index)
{
	struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);

	if (!unit || unit->unit.current_weapon_index < 0 || unit->unit.current_weapon_index >= MAXIMUM_WEAPONS_PER_UNIT)
		return NONE;
	return unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
}

/* the precision of the unit's current weapon (bots.c's, from its tag) */
static short bots_tactics_precision(long unit_index)
{
	return bots_weapon_precision(bots_tactics_weapon(unit_index));
}

/* whether the precision weapon held still keeps the bot at the vantage
point it went to for the precision `wanted`: a long or heavy one's for a
long or heavy one, a middle one's for any precision weapon */
static boolean bots_tactics_still_precise(short wanted, short held)
{
	if (wanted == _bots_precision_mid)
		return held == _bots_precision_mid || held == _bots_precision_long;
	return held == _bots_precision_long || held == _bots_precision_heavy;
}

/* the precision weapon the bot would hold a vantage point for, by its role
and the game: none when its part keeps it elsewhere (King's attackers in
the hill; a CTF defender but with a long one, over its stand; a middle one
a roamer's alone) */
static short bots_tactics_weapon_wants(struct bots_tactics_bot const *state, short precision)
{
	if (precision == _bots_precision_none ||
		(bots_tactics.game == game_engine_king && state->role == _bots_role_attacker))
	{
		return _bots_precision_none;
	}
	switch (precision)
	{
	case _bots_precision_long:
		return _bots_precision_long;
	case _bots_precision_heavy:
		return state->role != _bots_role_defender ? _bots_precision_heavy : _bots_precision_none;
	case _bots_precision_mid:
		return state->role == _bots_role_roamer ? _bots_precision_mid : _bots_precision_none;
	}
	return _bots_precision_none;
}

static char const *bots_precision_name(short precision)
{
	return precision == _bots_precision_long ? "long precision" : precision == _bots_precision_mid ? "middle precision" :
		precision == _bots_precision_heavy ? "heavy" : "none";
}

static char const *bots_vantage_left_name(short why)
{
	static char const *const names[NUMBER_OF_BOTS_VANTAGE_LEFT] =
		{ "time", "enemy near", "ammunition", "hurt", "weapon", "objective", "other" };

	return why >= 0 && why < NUMBER_OF_BOTS_VANTAGE_LEFT ? names[why] : "?";
}

/* whether a weapon is nearly empty: what is left of it half a magazine at
most (one with a magazine that loads rounds) */
static boolean bots_tactics_weapon_low(long weapon_index)
{
	struct weapon_datum *weapon;
	struct weapon_definition *definition;
	struct weapon_magazine_definition *magazine;

	if (weapon_index == NONE)
		return FALSE;
	weapon = weapon_get(weapon_index);
	definition = weapon_definition_get(weapon->definition_index);
	if (definition->weapon.magazines.count <= 0)
		return FALSE;
	magazine = TAG_BLOCK_GET_ELEMENT(&definition->weapon.magazines, 0, struct weapon_magazine_definition);
	/* (a battery's weapon, the plasma pistol's, loads no rounds) */
	if (magazine->rounds_loaded_maximum <= 0)
		return FALSE;
	return weapon->weapon.magazines[0].rounds_loaded + weapon->weapon.magazines[0].rounds_total <=
		MAX(1, magazine->rounds_loaded_maximum / 2);
}

/* ---------- the roles */

/* the role of a team's bot by its order: the pattern, as far as the counts
for the game and the team's size allow */
static short bots_roles_for(long game, short rank, long team_size, long *defenders, long *snipers)
{
	short role = game == game_engine_slayer ? _bots_role_roamer : _bots_role_attacker;
	long most_defenders = 0, most_snipers = 0;

	switch (game)
	{
	case game_engine_ctf:
		most_defenders = bots_tactics.assault || team_size < 4 ? 0 : team_size / 3;
		most_snipers = team_size >= 4 ? 1 + (team_size >= 10) : 0;
		break;
	case game_engine_king:
		most_defenders = team_size < 4 ? 0 : team_size / 3;
		most_snipers = team_size >= 4 ? 1 : 0;
		break;
	case game_engine_oddball:
		most_snipers = team_size >= 4 ? 1 : 0;
		break;
	case game_engine_slayer:
		most_snipers = team_size >= 5 ? 1 + (team_size >= 10) : 0;
		break;
	default:
		return role;
	}
	/* (attacker, defender, attacker, sniper, ... in turn) */
	if (rank % 4 == 1 && *defenders < most_defenders)
	{
		(*defenders)++;
		return _bots_role_defender;
	}
	if ((rank % 4 == 3 || (rank % 4 == 1 && !most_defenders)) && *snipers < most_snipers)
	{
		(*snipers)++;
		return _bots_role_sniper;
	}
	return role;
}

/* every bot's role and lane, once a second; halo.log's roles line when they
change */
static void bots_roles_assign(void)
{
	boolean teams = game_engine_has_teams();
	long defenders[BOTS_TACTICS_TEAMS], snipers[BOTS_TACTICS_TEAMS], ranks[BOTS_TACTICS_TEAMS], attackers[BOTS_TACTICS_TEAMS];
	char line[600];
	int length = 0;
	long index;

	csmemset(bots_tactics.team_bots, 0, sizeof(bots_tactics.team_bots));
	csmemset(defenders, 0, sizeof(defenders));
	csmemset(snipers, 0, sizeof(snipers));
	csmemset(ranks, 0, sizeof(ranks));
	csmemset(attackers, 0, sizeof(attackers));
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_tactics_bot *state = &bots_tactics_bots[index];
		struct player_datum *player = state->player_index != NONE ? player_try_and_get(state->player_index) : NULL;

		state->team = NONE;
		if (!player)
			continue;
		state->team = teams ? PIN(player->team_index, 0, BOTS_TACTICS_TEAMS - 1) : 0;
		bots_tactics.team_bots[state->team]++;
	}
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_tactics_bot *state = &bots_tactics_bots[index];
		short role, lane = 0;

		if (state->team == NONE)
			continue;
		role = bots_tactics_enabled() ? bots_roles_for(bots_tactics.game, (short)ranks[state->team]++,
			bots_tactics.team_bots[state->team], &defenders[state->team], &snipers[state->team]) :
			(bots_tactics.game == game_engine_slayer ? _bots_role_roamer : _bots_role_attacker);
		if (role == _bots_role_attacker || role == _bots_role_roamer)
		{
			state->rank = (short)attackers[state->team]++;
			lane = bots_routes_lane_for_rank(state->rank);
		}
		if (role != state->role)
		{
			bots_vantage_release(index, "role");
			state->role = role;
			state->post_until_tick = 0;
		}
		if (lane != state->lane)
		{
			state->lane = lane;
			state->waypoint = NONE;
			csmemset(state->passed, 0, sizeof(state->passed));
			state->lane_given_up = FALSE;
		}
		if (length < (int)sizeof(line) - 48)
		{
			length += snprintf(line + length, sizeof(line) - (size_t)length, "%sbot %ld team %ld %s", length ? ", " : "",
				index + 1, state->team, bots_role_name(role));
			if (role == _bots_role_attacker || role == _bots_role_roamer)
				length += snprintf(line + length, sizeof(line) - (size_t)length, " (%s)", bots_routes_lane_name(lane));
		}
	}
	line[length] = 0;
	if (length && strcmp(line, bots_tactics.roles_line))
	{
		strcpy(bots_tactics.roles_line, line);
		platform_log("bots: roles (routes %s): %s", bots_routes_mode() == _bots_routes_lanes ? "lanes" :
			bots_routes_mode() == _bots_routes_mixed ? "mixed" : "direct", line);
	}
}

/* ---------- what bots.c tells */

void bots_tactics_initialize_for_new_map(void)
{
	long index;

	bots_routes_reset();
	bots_vantage_dispose();
	csmemset(&bots_tactics_counts, 0, sizeof(bots_tactics_counts));
	bots_tactics.prepared = FALSE;
	bots_tactics.prepare_ticks = 0;
	bots_tactics.prepare_us = 0;
	bots_tactics.prepare_most_us = 0;
	bots_tactics.roles_tick = 0;
	bots_tactics.roles_line[0] = 0;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		csmemset(&bots_tactics_bots[index], 0, sizeof(bots_tactics_bots[index]));
		bots_tactics_bots[index].player_index = NONE;
		bots_tactics_bots[index].team = NONE;
		bots_tactics_bots[index].vantage = NONE;
		bots_tactics_bots[index].last_vantage = NONE;
		bots_tactics_bots[index].waypoint = NONE;
	}
}

void bots_tactics_dispose_from_old_map(void)
{
	bots_routes_reset();
	bots_vantage_dispose();
	bots_tactics.prepared = FALSE;
}

void bots_tactics_update(void)
{
	long now = game_time_get();

	bots_tactics.game = game_engine_get_variant()->game_engine_index;
	bots_tactics.assault = bots_tactics.game == game_engine_ctf && game_engine_get_variant()->game_engine_variant.ctf.assault;
	/* the map's lanes and vantage points, a bounded step a tick (the work a
	tick fixed, not timed: the same ticks from run to run) */
	if (!bots_tactics.prepared)
	{
		unsigned long long started = vita_host_time_us(), spent;

		if (!bots_routes_map.ready)
			bots_routes_step();
		else if (bots_vantage_step())
		{
			bots_tactics.prepared = TRUE;
			bots_routes_release_graph();
			platform_log("bots: tactics made in %ld ticks: %.2f ms in all, at most %.0f us a tick (routes %s, %ld side lanes)",
				bots_tactics.prepare_ticks + 1, (double)bots_tactics.prepare_us / 1000.0,
				(double)bots_tactics.prepare_most_us, bots_routes_mode() == _bots_routes_direct ? "direct" :
				bots_routes_mode() == _bots_routes_lanes ? "lanes" : "mixed", bots_routes_map.lane_count);
			bots_tactics.roles_tick = 0;
		}
		spent = vita_host_time_us() - started;
		bots_tactics.prepare_ticks++;
		bots_tactics.prepare_us += spent;
		if (spent > bots_tactics.prepare_most_us)
			bots_tactics.prepare_most_us = spent;
	}
	if (now >= bots_tactics.roles_tick || now < bots_tactics.roles_tick - TICKS_PER_SECOND)
	{
		bots_tactics.roles_tick = now + TICKS_PER_SECOND;
		bots_routes_read_setting();
		bots_roles_assign();
	}
}

void bots_tactics_bot_reset(long bot_index, long player_index)
{
	struct bots_tactics_bot *state;

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM)
		return;
	state = &bots_tactics_bots[bot_index];
	bots_vantage_release(bot_index, "reset");
	csmemset(state, 0, sizeof(*state));
	state->player_index = player_index;
	state->random = (0x9E3779B1UL * (unsigned long)(bot_index + 7) ^ (unsigned long)network_game_get_random_seed() ^
		0x5BD1E995UL) & 0xFFFFFFFFUL;
	state->team = NONE;
	state->vantage = NONE;
	state->last_vantage = NONE;
	state->waypoint = NONE;
	state->post = (short)(bot_index % 3);
	state->role = NONE;
	/* (its role and lane at once) */
	bots_tactics.roles_tick = 0;
}

void bots_tactics_bot_dead(long bot_index)
{
	struct bots_tactics_bot *state;

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM)
		return;
	state = &bots_tactics_bots[bot_index];
	if (state->vantage != NONE)
		bots_tactics_counts.vantage_leaves[_bots_vantage_left_other]++;
	bots_vantage_release(bot_index, "dead");
	state->waypoint = NONE;
	csmemset(state->passed, 0, sizeof(state->passed));
	state->lane_given_up = FALSE;
	state->post_until_tick = 0;
}

/* off its vantage point, for why: the game's goals a while before another */
static void bots_tactics_leave(long bot_index, short why)
{
	struct bots_tactics_bot *state = &bots_tactics_bots[bot_index];
	boolean scoped = state->role == _bots_role_sniper || state->vantage_weapon == _bots_precision_long;
	short weapon = state->vantage_weapon;
	long now = game_time_get();

	bots_tactics_counts.vantage_leaves[why]++;
	platform_log("bots: bot %ld (%s) off vantage point %d (%s) after %.0f s, held %.0f s%s%s", bot_index + 1,
		bots_role_name(state->role), (int)state->vantage + 1, bots_vantage_left_name(why),
		(double)(now - state->vantage_tick) / TICKS_PER_SECOND,
		state->hold_tick ? (double)(now - state->hold_tick) / TICKS_PER_SECOND : 0.0,
		weapon != _bots_precision_none ? ", there for its " : "", weapon != _bots_precision_none ? bots_precision_name(weapon) : "");
	/* (debug: HALO_BOT_TRACE, each bot off its vantage point, why, its weapon) */
	if (getenv("HALO_BOT_TRACE"))
	{
		struct player_datum *player = player_try_and_get(state->player_index);
		long weapon_index = player ? bots_tactics_weapon(player->unit_index) : NONE;

		platform_log("bots: trace bot %ld (%s) off vantage point %d (%s) after %ld ticks, held %ld, weapon %s %d/%d", bot_index + 1,
			bots_role_name(state->role), (int)state->vantage + 1, bots_vantage_left_name(why), game_time_get() - state->vantage_tick,
			state->hold_tick ? game_time_get() - state->hold_tick : 0L,
			weapon_index != NONE ? tag_get_name(weapon_get(weapon_index)->definition_index) : "none",
			weapon_index != NONE ? (int)weapon_get(weapon_index)->weapon.magazines[0].rounds_loaded : 0,
			weapon_index != NONE ? (int)weapon_get(weapon_index)->weapon.magazines[0].rounds_total : 0);
	}
	state->rest_weapon = weapon;
	bots_vantage_release(bot_index, "left");
	/* (the game's goals a while: a sniper's, or a long precision weapon's
	bot, soon back at it, sooner after a fight close up; at once after its
	weapon changed, which may be taken up again; a while after the
	objective) */
	if (why == _bots_vantage_left_weapon)
		state->rest_until_tick = now + TICKS_PER_SECOND * 3;
	else if (why == _bots_vantage_left_objective)
		state->rest_until_tick = now + TICKS_PER_SECOND * 10;
	else if (scoped)
		state->rest_until_tick = now + (why == _bots_vantage_left_enemy ? bots_tactics_random_ticks(state, 8, 15) :
			bots_tactics_random_ticks(state, 10, 20));
	else
		state->rest_until_tick = now + bots_tactics_random_ticks(state, 30, 45);
}

/* why the bot leaves its vantage point now, NONE to stay */
static short bots_tactics_leave_reason(long bot_index, struct player_datum *player, long now)
{
	struct bots_tactics_bot *state = &bots_tactics_bots[bot_index];
	struct object_datum *object = object_try_and_get(player->unit_index);
	long weapon_index = bots_tactics_weapon(player->unit_index);

	/* (its weapon first: the reason that says most) */
	if (state->vantage_weapon != _bots_precision_none)
	{
		short precision = bots_weapon_precision(weapon_index);

		if (!bots_tactics_still_precise(state->vantage_weapon, precision))
			return _bots_vantage_left_weapon;
		/* (a sniper rifle taken up at, or on the way to, a pistol's or a
		rocket launcher's vantage point: held as a sniper rifle's from now) */
		if (precision == _bots_precision_long && state->vantage_weapon != _bots_precision_long)
		{
			state->vantage_weapon = _bots_precision_long;
			state->hold_ticks = (state->hold_tick ? now - state->hold_tick : 0) + bots_tactics_random_ticks(state, 35, 50);
			platform_log("bots: bot %ld (%s) %s vantage point %d for its long precision now, for %.0f s", bot_index + 1,
				bots_role_name(state->role), state->hold_tick ? "holds" : "goes to", (int)state->vantage + 1,
				(double)(state->hold_ticks - (state->hold_tick ? now - state->hold_tick : 0)) / TICKS_PER_SECOND);
		}
	}
	if (bots_tactics_weapon_low(weapon_index))
		return _bots_vantage_left_ammunition;
	if (object && object->object.body_vitality < 0.34f)
		return _bots_vantage_left_hurt;
	if (state->hold_tick && now - state->hold_tick >= state->hold_ticks)
		return _bots_vantage_left_time;
	if (!state->hold_tick && now - state->vantage_tick > TICKS_PER_SECOND * 60)
	{
		SET_FLAG(state->failed_vantages, state->vantage, TRUE);
		state->failed_until_tick = now + TICKS_PER_SECOND * 60;
		bots_tactics_counts.vantage_failures++;
		return _bots_vantage_left_other;
	}
	return NONE;
}

/* a vantage point for the bot to go to and hold (its role's, a sniper's or
King's defender's; or its precision weapon's, `weapon_wants`): the best free
one (bots_vantage_choose), a defender's over the hill or its own stand, a
pistol's near; while its team has fewer holding one than a third of its bots
(half for a long precision weapon's, a pistol's one apart). None: looked for
again in a few seconds */
static void bots_tactics_seek(long bot_index, long player_index, real_point3d const *position, boolean by_role,
	short weapon_wants)
{
	struct bots_tactics_bot *state = &bots_tactics_bots[bot_index];
	long now = game_time_get();
	boolean king_defender = state->role == _bots_role_defender && bots_tactics.game == game_engine_king;
	real_point3d hill, *objective = NULL;
	long index, holders = 0, middle_holders = 0, most;
	short vantage;

	if (king_defender)
	{
		real_point3d goals[8];

		if (game_engine_port_player_goals(player_index, goals, NUMBEROF(goals)) <= 0)
			return;
		hill = goals[0];
		objective = &hill;
	}
	else if (!by_role && state->role == _bots_role_defender && bots_tactics.game == game_engine_ctf)
	{
		/* (a CTF defender's sniper rifle: over its own stand, else its posts) */
		if (!bots_vantage_stand(state->team, &hill))
			return;
		objective = &hill;
	}
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		/* (a pistol's bot, one a team, is not counted: it keeps no sniper
		from a vantage point) */
		if (index != bot_index && bots_tactics_bots[index].vantage != NONE && bots_tactics_bots[index].team == state->team)
		{
			if (bots_tactics_bots[index].vantage_weapon == _bots_precision_mid)
				middle_holders++;
			else
				holders++;
		}
	}
	most = weapon_wants == _bots_precision_long ? MAX(1, (bots_tactics.team_bots[state->team] + 1) / 2) :
		MAX(1, bots_tactics.team_bots[state->team] / 3);
	vantage = (weapon_wants == _bots_precision_mid ? !middle_holders : holders < most) ?
		bots_vantage_choose(bot_index, position, objective,
			weapon_wants == _bots_precision_mid ? BOTS_VANTAGE_MID_REACH : REAL_MAX) : NONE;
	if (vantage == NONE)
	{
		state->rest_until_tick = now + TICKS_PER_SECOND * 5;
		state->rest_weapon = weapon_wants;
		return;
	}
	state->vantage = vantage;
	state->vantage_weapon = weapon_wants;
	state->vantage_tick = now;
	state->hold_tick = 0;
	state->hold_ticks = state->role == _bots_role_sniper || weapon_wants == _bots_precision_long ?
		bots_tactics_random_ticks(state, 35, 50) : king_defender ? bots_tactics_random_ticks(state, 25, 40) :
		weapon_wants == _bots_precision_mid ? bots_tactics_random_ticks(state, 12, 20) : bots_tactics_random_ticks(state, 20, 30);
	bots_tactics_counts.vantages_chosen++;
}

boolean bots_tactics_goal(long bot_index, long player_index, real_point3d const *position, real_point3d *goal,
	long *surface_index)
{
	struct bots_tactics_bot *state;
	struct player_datum *player;
	long now = game_time_get();

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM || !bots_tactics_enabled() || !bots_tactics.prepared ||
		bots_tactics.game == game_engine_race || bots_tactics.game == game_engine_none)
	{
		return FALSE;
	}
	state = &bots_tactics_bots[bot_index];
	player = player_try_and_get(player_index);
	if (!player || player->unit_index == NONE || state->team == NONE)
		return FALSE;
	/* a vantage point it goes to, or holds */
	if (state->vantage != NONE)
	{
		short why = bots_tactics_leave_reason(bot_index, player, now);

		if (why != NONE)
			bots_tactics_leave(bot_index, why);
		else
		{
			bots_vantage_point(state->vantage, goal, surface_index);
			if (!state->hold_tick && bots_tactics_distance2d(position, goal) < BOTS_VANTAGE_ARRIVED &&
				fabs(position->z - goal->z) < 1.5f)
			{
				state->hold_tick = now;
				bots_tactics_counts.vantages_reached++;
				platform_log("bots: bot %ld (%s) at vantage point %d (%.1f %.1f %.1f), for %.0f s%s%s", bot_index + 1,
					bots_role_name(state->role), (int)state->vantage + 1, goal->x, goal->y, goal->z,
					(double)state->hold_ticks / TICKS_PER_SECOND,
					state->vantage_weapon != _bots_precision_none ? ", for its " : "",
					state->vantage_weapon != _bots_precision_none ? bots_precision_name(state->vantage_weapon) : "");
			}
			return TRUE;
		}
	}
	/* one to go to: a sniper's, a precision weapon's (a sniper rifle's, a
	pistol's near) or a rocket launcher's, a defender's over the hill (a CTF
	defender's with a sniper rifle over its stand) */
	{
		boolean king_defender = state->role == _bots_role_defender && bots_tactics.game == game_engine_king;
		boolean by_role = state->role == _bots_role_sniper || king_defender;
		short weapon_wants = by_role ? _bots_precision_none :
			bots_tactics_weapon_wants(state, bots_tactics_precision(player->unit_index));

		/* (resting a while from a vantage point, the game's goals first;
		but a sniper rifle taken up since one held for another weapon, a
		pistol or a rocket launcher, goes to one at once) */
		if (now < state->rest_until_tick &&
			!(weapon_wants == _bots_precision_long && state->rest_weapon != _bots_precision_long))
		{
			weapon_wants = _bots_precision_none;
			by_role = FALSE;
		}
		if (by_role || weapon_wants != _bots_precision_none)
			bots_tactics_seek(bot_index, player_index, position, by_role, weapon_wants);
		if (state->vantage != NONE)
		{
			bots_vantage_point(state->vantage, goal, surface_index);
			return TRUE;
		}
	}
	/* CTF's defenders: about the stand, post to post */
	if (state->role == _bots_role_defender && bots_tactics.game == game_engine_ctf && !bots_tactics.assault &&
		bots_vantage_post_count(state->team) > 0)
	{
		if (now >= state->post_until_tick)
		{
			state->post = (short)((state->post + 1 + (short)(bots_tactics_random(state) % 2)) % bots_vantage_post_count(state->team));
			state->post_until_tick = now + bots_tactics_random_ticks(state, 15, 25);
			bots_tactics_counts.posts_taken++;
		}
		return bots_vantage_post(state->team, state->post, goal, surface_index);
	}
	return FALSE;
}

boolean bots_tactics_search_answer(long bot_index, real_point3d const *goal, boolean reached, boolean spent)
{
	struct bots_tactics_bot *state;
	real_point3d point;
	long surface_index;

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM || reached || !spent)
		return FALSE;
	if (bots_routes_search_failed(bot_index, goal))
		return TRUE;
	state = &bots_tactics_bots[bot_index];
	if (state->vantage == NONE)
		return FALSE;
	bots_vantage_point(state->vantage, &point, &surface_index);
	if (fabs(point.x - goal->x) + fabs(point.y - goal->y) + fabs(point.z - goal->z) > 0.5f)
		return FALSE;
	SET_FLAG(state->failed_vantages, state->vantage, TRUE);
	state->failed_until_tick = game_time_get() + TICKS_PER_SECOND * 60;
	bots_tactics_counts.vantage_failures++;
	bots_tactics_leave(bot_index, _bots_vantage_left_other);
	state->rest_until_tick = game_time_get() + TICKS_PER_SECOND * 5;
	return TRUE;
}

void bots_tactics_look(long bot_index, real_point3d const *position, long now, real *yaw, real *pitch)
{
	struct bots_tactics_bot *state;
	unsigned long watched = 0;
	long count = bots_places_count(), index;
	struct bots_place const *place;
	real desired_yaw, desired_pitch, off, dx, dy, dz;

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM || !bots_tactics.prepared || count <= 0)
		return;
	state = &bots_tactics_bots[bot_index];
	if (state->vantage != NONE && state->hold_tick)
		watched = bots_vantage_sees(state->vantage);
	else if (state->role == _bots_role_defender && bots_tactics.game == game_engine_ctf && state->post_until_tick)
		watched = FLAG(count) - 1;
	if (!watched)
		return;
	if (now >= state->look_until_tick || !TEST_FLAG(watched, state->look_place))
	{
		/* (the next place watched, in turn) */
		for (index = 1; index <= count; index++)
		{
			long next = (state->look_place + index) % count;

			if (TEST_FLAG(watched, next) && bots_tactics_distance2d(&bots_place_get(next)->point, position) > 3.0f)
			{
				state->look_place = (short)next;
				break;
			}
		}
		state->look_until_tick = now + TICKS_PER_SECOND * 2 + (long)(bots_tactics_random(state) % (TICKS_PER_SECOND * 2));
	}
	place = bots_place_get(state->look_place);
	dx = place->point.x - position->x;
	dy = place->point.y - position->y;
	dz = place->point.z - (position->z + 0.6f);
	if (dx * dx + dy * dy < 1.0f)
		return;
	desired_yaw = (real)atan2(dy, dx);
	desired_pitch = (real)atan2(dz, sqrt(dx * dx + dy * dy));
	off = (real)fmod(desired_yaw - *yaw, 2.0f * BOTS_TACTICS_PI);
	if (off > BOTS_TACTICS_PI)
		off -= 2.0f * BOTS_TACTICS_PI;
	else if (off < -BOTS_TACTICS_PI)
		off += 2.0f * BOTS_TACTICS_PI;
	/* (turning at six degrees a tick) */
	*yaw += PIN(off, -0.105f, 0.105f);
	*pitch += PIN(desired_pitch - *pitch, -0.05f, 0.05f);
}

void bots_tactics_fight(long bot_index, real_point3d const *position, real target_distance, boolean *moving,
	real *direction, real *throttle)
{
	struct bots_tactics_bot *state;
	real_point3d point;
	long surface_index;

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM)
		return;
	state = &bots_tactics_bots[bot_index];
	if (state->vantage == NONE || !state->hold_tick)
		return;
	if (target_distance < (state->vantage_weapon == _bots_precision_mid ? BOTS_VANTAGE_ENEMY_NEAR_MID : BOTS_VANTAGE_ENEMY_NEAR))
	{
		bots_tactics_leave(bot_index, _bots_vantage_left_enemy);
		return;
	}
	bots_vantage_point(state->vantage, &point, &surface_index);
	if (bots_tactics_distance2d(position, &point) > 1.2f)
	{
		*direction = (real)atan2(point.y - position->y, point.x - position->x);
		*moving = TRUE;
		*throttle = 0.7f;
	}
	else
		*throttle *= 0.3f;
}

void bots_tactics_check(long bot_index, long player_index)
{
	struct player_datum *player;
	short why;

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM || bots_tactics_bots[bot_index].vantage == NONE ||
		(player = player_try_and_get(player_index)) == NULL || player->unit_index == NONE)
	{
		return;
	}
	why = bots_tactics_leave_reason(bot_index, player, game_time_get());
	if (why != NONE)
		bots_tactics_leave(bot_index, why);
}

boolean bots_tactics_holding(long bot_index)
{
	return bot_index >= 0 && bot_index < BOTS_MAXIMUM && bots_tactics_bots[bot_index].vantage != NONE &&
		bots_tactics_bots[bot_index].hold_tick != 0;
}

void bots_tactics_objective(long bot_index)
{
	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM || bots_tactics_bots[bot_index].vantage == NONE)
		return;
	bots_tactics_leave(bot_index, _bots_vantage_left_objective);
}

real bots_tactics_sight_scale(long bot_index)
{
	struct bots_tactics_bot const *state;
	struct player_datum *player;

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM)
		return 1.0f;
	state = &bots_tactics_bots[bot_index];
	if (state->vantage == NONE || !state->hold_tick || state->player_index == NONE ||
		(player = player_try_and_get(state->player_index)) == NULL || player->unit_index == NONE)
	{
		return 1.0f;
	}
	return bots_tactics_precision(player->unit_index) == _bots_precision_long ? BOTS_VANTAGE_SCOPE_SIGHT : 1.0f;
}

static long bots_tactics_leaves_total(void)
{
	long index, total = 0;

	for (index = 0; index < NUMBER_OF_BOTS_VANTAGE_LEFT; index++)
		total += bots_tactics_counts.vantage_leaves[index];
	return total;
}

void bots_tactics_report(long now)
{
	if (!bots_tactics.prepared)
		return;
	platform_log("bots: tactics at tick %ld: routes %s (%ld side lanes): lanes taken %ld, waypoints passed %ld, given up %ld; "
		"vantage points %ld: chosen %ld, reached %ld, left %ld (time %ld, enemy near %ld, ammunition %ld, hurt %ld, weapon %ld, "
		"objective %ld, other %ld), no way %ld; defenders' posts taken %ld%s", now,
		bots_routes_mode() == _bots_routes_direct ? "direct" : bots_routes_mode() == _bots_routes_lanes ? "lanes" : "mixed",
		bots_routes_map.lane_count, bots_tactics_counts.lanes_taken, bots_tactics_counts.waypoints_passed,
		bots_tactics_counts.lanes_given_up, bots_vantage_count(), bots_tactics_counts.vantages_chosen,
		bots_tactics_counts.vantages_reached,
		bots_tactics_leaves_total(),
		bots_tactics_counts.vantage_leaves[_bots_vantage_left_time], bots_tactics_counts.vantage_leaves[_bots_vantage_left_enemy],
		bots_tactics_counts.vantage_leaves[_bots_vantage_left_ammunition], bots_tactics_counts.vantage_leaves[_bots_vantage_left_hurt],
		bots_tactics_counts.vantage_leaves[_bots_vantage_left_weapon], bots_tactics_counts.vantage_leaves[_bots_vantage_left_objective],
		bots_tactics_counts.vantage_leaves[_bots_vantage_left_other], bots_tactics_counts.vantage_failures,
		bots_tactics_counts.posts_taken, bots_tactics_enabled() ? "" : " (no roles nor vantage points: HALO_BOT_ROLES=0)");
}
