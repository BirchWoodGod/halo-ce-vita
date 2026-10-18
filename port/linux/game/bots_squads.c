/*
BOTS_SQUADS.C

(port) The offline bots' squads (bots.c's bots, in team games): the owner,
Oct 17 2026, "they seem to just charge towards the flag", "the bots need to
work as a team, some kind of squad logic". Before, each bot fought alone:
it saw its own enemies, and every attacker ran at the enemy flag by itself,
past the side lanes' last waypoint straight in, one after another.

- Squads: each team's attackers (or Slayer's roamers: bots_roles.c), by
  their order, two to a squad, the first its leader; an odd one out joins
  the squad before it (three: the third keeps with the leader). A squad
  takes one lane (bots_routes_lane_for_squad; Mixed: one team's first squad
  round the first side lane, the other's down the middle, so the two do not
  meet head on in the side lane).
- Pace (CTF): an attacker on its way in to the enemy flag at home, 20 to 70
  units from the enemy stand, ahead of its partner (alive, 6 units or more
  further back, 8 to 30 units away) holds where it is for it, 6 seconds a
  life at most; the two go into the base together.
- Following (Slayer's roamers): a squad's second keeps with its leader,
  going to it when more than 9 units away.
- Callouts: a bot that sees an enemy calls it out to its team (twice a
  second at most), and one hurt with no enemy seen calls where it is. The
  enemy a teammate called the last 2 seconds is the one a bot shoots first
  (bots.c's target score: as if 3 units nearer); and a teammate's call the
  last 3 seconds draws a roamer that is not fighting (35 units at most)
  there for 6 seconds.
- Escorts (bots.c's bots_ctf_goal): the enemy flag carried by a teammate,
  its partner and the two bots nearest it go along with the carrier; the
  others to their own flag stand, where the capture is made.

Measured (run_bots_test.sh squads and ctf, Blood Gulch CTF, 6-minute games,
against HALO_BOT_SQUADS=0): see the commit that brought this in. Cut, for
losing flags: a staging point on the squad's lane waited at for the partner
(on the direct route it gathered every squad at one spot; on a side lane the
partner seldom came), a CTF attacker answering its teammates' calls (6 s off
the objective each: 0 to 2 flags taken in six minutes against 7 to 10), and
going back to meet a partner far behind or dead (6 flags against 20).

halo.log's squads line each minute (after the tactics line): the squads,
callouts, help given, follows, holds for a partner.

(debug) HALO_BOT_SQUADS=0: none of this (callouts, help, pace, following
off; escorts as before: everyone; each attacker its own lane, as before).
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"

#include "bots.h"
#include "bots_tactics.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

#define BOTS_SQUAD_CALLOUTS 16
/* the teams counted (bots_roles.c's BOTS_TACTICS_TEAMS) */
#define BOTS_SQUAD_TEAMS 16
/* a call heard this long, and a called enemy shot first this long */
#define BOTS_CALLOUT_TICKS (TICKS_PER_SECOND * 3)
#define BOTS_CALLOUT_FOCUS_TICKS (TICKS_PER_SECOND * 2)
#define BOTS_CALLOUT_EVERY_TICKS (TICKS_PER_SECOND / 2)
/* how far a call draws a roamer; for how long; nearer than this, it is
there */
#define BOTS_HELP_REACH 35.0f
#define BOTS_HELP_TICKS (TICKS_PER_SECOND * 6)
#define BOTS_HELP_THERE 4.0f
/* a second further from its leader than this goes to it; where it goes,
again when the leader is this far from it */
#define BOTS_FOLLOW_DISTANCE 9.0f
#define BOTS_FOLLOW_MOVED 4.0f
/* the pace: the enemy flag at home within this of its stand; held between
these from the enemy stand; the partner with it within this, behind it by
this at least, and near enough to wait for within this; a life's holding */
#define BOTS_PACE_FLAG_HOME 3.0f
#define BOTS_PACE_BASE 20.0f
#define BOTS_PACE_FAR 70.0f
#define BOTS_PACE_TOGETHER 8.0f
#define BOTS_PACE_GAP 6.0f
#define BOTS_PACE_REACH 30.0f
#define BOTS_PACE_TICKS (TICKS_PER_SECOND * 6)

/* ---------- structures */

struct bots_callout
{
	long team;
	long caller;
	long enemy_player_index;
	real_point3d point;
	long surface_index;
	long tick;
};

static struct
{
	struct bots_callout callouts[BOTS_SQUAD_CALLOUTS];
	long next;
	/* halo.log's squads line */
	long squads;
	long callouts_made;
	long helps;
	long follows;
	long paces;
} bots_squads;

/* ---------- small helpers */

boolean bots_squads_on(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		char const *setting = getenv("HALO_BOT_SQUADS");

		enabled = setting && setting[0] && !atoi(setting) ? 0 : 1;
	}
	return enabled != 0;
}

static boolean bots_squads_enabled(void)
{
	return bots_squads_on() && game_engine_has_teams();
}

static real bots_squads_distance2d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y;

	return (real)sqrt(dx * dx + dy * dy);
}

static struct bots_tactics_bot *bots_squads_bot(long bot_index)
{
	return bot_index >= 0 && bot_index < BOTS_MAXIMUM ? &bots_tactics_bots[bot_index] : NULL;
}

/* where a bot's unit is, on foot and alive: FALSE in a vehicle, dead, or
none */
static boolean bots_squads_on_foot(struct bots_tactics_bot const *state, real_point3d *position)
{
	struct player_datum *player = state->player_index != NONE ? player_try_and_get(state->player_index) : NULL;
	struct object_datum *object;

	if (!player || player->unit_index == NONE)
		return FALSE;
	object = object_try_and_get_and_verify_type(player->unit_index, _object_mask_unit);
	if (!object || object->object.parent_object_index != NONE ||
		TEST_FLAG(object->object.damage_flags, _object_dead_bit))
	{
		return FALSE;
	}
	*position = object->object.position;
	return TRUE;
}

/* ---------- what bots_roles.c tells */

void bots_squads_reset(void)
{
	csmemset(&bots_squads, 0, sizeof(bots_squads));
}

short bots_squads_squad_for(short rank, long attackers)
{
	short squad = (short)(rank / 2);

	if (rank > 0 && rank % 2 == 0 && rank == attackers - 1)
		squad--;
	return squad;
}

void bots_squads_assign(void)
{
	boolean enabled = bots_squads_enabled();
	long attackers[BOTS_SQUAD_TEAMS];
	long index, other;

	csmemset(attackers, 0, sizeof(attackers));
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_tactics_bot const *state = &bots_tactics_bots[index];

		if (state->team >= 0 && state->team < BOTS_SQUAD_TEAMS &&
			(state->role == _bots_role_attacker || state->role == _bots_role_roamer))
		{
			attackers[state->team]++;
		}
	}
	bots_squads.squads = 0;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_tactics_bot *state = &bots_tactics_bots[index];
		short squad = NONE;

		if (enabled && state->team >= 0 && state->team < BOTS_SQUAD_TEAMS &&
			(state->role == _bots_role_attacker || state->role == _bots_role_roamer))
		{
			squad = bots_squads_squad_for(state->rank, attackers[state->team]);
		}
		state->squad = squad;
		state->partner = NONE;
		state->leader = FALSE;
	}
	/* (a squad's leader its first by rank; the leader's partner its
	second, the others' the leader) */
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_tactics_bot *state = &bots_tactics_bots[index];
		long leader = index;

		if (state->squad == NONE)
			continue;
		for (other = 0; other < BOTS_MAXIMUM; other++)
		{
			struct bots_tactics_bot const *mate = &bots_tactics_bots[other];

			if (mate->squad == state->squad && mate->team == state->team && mate->rank < bots_tactics_bots[leader].rank)
				leader = other;
		}
		if (leader != index)
		{
			state->partner = leader;
			continue;
		}
		state->leader = TRUE;
		bots_squads.squads++;
		for (other = 0; other < BOTS_MAXIMUM; other++)
		{
			struct bots_tactics_bot const *mate = &bots_tactics_bots[other];

			if (other != index && mate->squad == state->squad && mate->team == state->team &&
				(state->partner == NONE || mate->rank < bots_tactics_bots[state->partner].rank))
			{
				state->partner = other;
			}
		}
	}
}

void bots_squads_bot_dead(long bot_index)
{
	struct bots_tactics_bot *state = bots_squads_bot(bot_index);

	if (!state)
		return;
	state->help_until_tick = 0;
	state->follow_surface = NONE;
	state->pace_tick = 0;
	state->pace_ticks = 0;
}

void bots_squads_report(long now)
{
	if (!bots_squads_enabled())
		return;
	platform_log("bots: squads at tick %ld: %ld squads; callouts %ld, gone to help %ld, followed the leader %ld, "
		"held for the partner %ld", now, bots_squads.squads, bots_squads.callouts_made, bots_squads.helps,
		bots_squads.follows, bots_squads.paces);
}

/* ---------- what bots.c tells */

void bots_squads_callout(long bot_index, long enemy_player_index, real_point3d const *point, long surface_index)
{
	struct bots_tactics_bot *state = bots_squads_bot(bot_index);
	long now = game_time_get();
	long index, slot = NONE;

	if (!state || state->team == NONE || surface_index == NONE || !bots_squads_enabled() ||
		now - state->callout_tick < BOTS_CALLOUT_EVERY_TICKS)
	{
		return;
	}
	state->callout_tick = now;
	/* (the caller's call of the same enemy again, else the oldest) */
	for (index = 0; index < BOTS_SQUAD_CALLOUTS && slot == NONE; index++)
	{
		struct bots_callout const *callout = &bots_squads.callouts[index];

		if (callout->tick && callout->caller == bot_index && callout->enemy_player_index == enemy_player_index)
			slot = index;
	}
	if (slot == NONE)
	{
		slot = bots_squads.next;
		bots_squads.next = (bots_squads.next + 1) % BOTS_SQUAD_CALLOUTS;
		bots_squads.callouts_made++;
	}
	bots_squads.callouts[slot].team = state->team;
	bots_squads.callouts[slot].caller = bot_index;
	bots_squads.callouts[slot].enemy_player_index = enemy_player_index;
	bots_squads.callouts[slot].point = *point;
	bots_squads.callouts[slot].surface_index = surface_index;
	bots_squads.callouts[slot].tick = now;
}

boolean bots_squads_focus(long bot_index, long enemy_player_index)
{
	struct bots_tactics_bot *state = bots_squads_bot(bot_index);
	long now = game_time_get();
	long index;

	if (!state || state->team == NONE || enemy_player_index == NONE || !bots_squads_enabled())
		return FALSE;
	for (index = 0; index < BOTS_SQUAD_CALLOUTS; index++)
	{
		struct bots_callout const *callout = &bots_squads.callouts[index];

		if (callout->tick && callout->team == state->team && callout->caller != bot_index &&
			callout->enemy_player_index == enemy_player_index && now - callout->tick <= BOTS_CALLOUT_FOCUS_TICKS)
		{
			return TRUE;
		}
	}
	return FALSE;
}

boolean bots_squads_help_goal(long bot_index, real_point3d const *position, real_point3d *goal, long *surface_index)
{
	struct bots_tactics_bot *state = bots_squads_bot(bot_index);
	long now = game_time_get();
	long index, best = NONE;

	/* (a roamer alone: an attacker drawn off its objective took fewer
	flags, the file's head) */
	if (!state || state->squad == NONE || state->role != _bots_role_roamer || !bots_squads_enabled())
		return FALSE;
	/* (on its way still, or there looking about) */
	if (now < state->help_until_tick)
	{
		*goal = state->help_point;
		*surface_index = state->help_surface;
		return TRUE;
	}
	for (index = 0; index < BOTS_SQUAD_CALLOUTS; index++)
	{
		struct bots_callout const *callout = &bots_squads.callouts[index];
		real distance;

		if (!callout->tick || callout->team != state->team || callout->caller == bot_index ||
			now - callout->tick > BOTS_CALLOUT_TICKS)
		{
			continue;
		}
		distance = bots_squads_distance2d(position, &callout->point);
		if (distance > BOTS_HELP_REACH || distance < BOTS_HELP_THERE)
			continue;
		if (best == NONE || callout->tick > bots_squads.callouts[best].tick)
			best = index;
	}
	if (best == NONE)
		return FALSE;
	state->help_point = bots_squads.callouts[best].point;
	state->help_surface = bots_squads.callouts[best].surface_index;
	state->help_until_tick = now + BOTS_HELP_TICKS;
	bots_squads.helps++;
	*goal = state->help_point;
	*surface_index = state->help_surface;
	return TRUE;
}

boolean bots_squads_follow_goal(long bot_index, real_point3d const *position, real_point3d *goal,
	long *surface_index)
{
	struct bots_tactics_bot *state = bots_squads_bot(bot_index);
	struct bots_tactics_bot *leader;
	real_point3d leader_position;

	if (!state || state->squad == NONE || state->leader || state->role != _bots_role_roamer ||
		!bots_squads_enabled())
	{
		return FALSE;
	}
	leader = bots_squads_bot(state->partner);
	if (!leader || !bots_squads_on_foot(leader, &leader_position) ||
		bots_squads_distance2d(position, &leader_position) < BOTS_FOLLOW_DISTANCE)
	{
		return FALSE;
	}
	/* (where the leader was, until it has moved on: not a new search a tick) */
	if (state->follow_surface == NONE ||
		bots_squads_distance2d(&state->follow_point, &leader_position) > BOTS_FOLLOW_MOVED)
	{
		real_point3d ground;
		long surface = bots_routes_ground(&leader_position, &ground);

		if (surface == NONE)
			return FALSE;
		state->follow_point = ground;
		state->follow_surface = surface;
		bots_squads.follows++;
	}
	*goal = state->follow_point;
	*surface_index = state->follow_surface;
	return TRUE;
}

boolean bots_squads_escorts_limited(void)
{
	return bots_squads_on();
}

boolean bots_squads_partner_of(long bot_index, long player_index)
{
	struct bots_tactics_bot *state = bots_squads_bot(bot_index);
	struct bots_tactics_bot *partner;

	if (!state || state->partner == NONE || !bots_squads_enabled())
		return FALSE;
	partner = bots_squads_bot(state->partner);
	return partner && partner->player_index == player_index;
}

/* CTF: an attacker on its way in to the enemy flag at home, ahead of its
partner (alive, on foot, further from the enemy stand than it, near enough
to come up in a few seconds), holds where it is until the partner is with
it, BOTS_PACE_TICKS a life at most, and never once in the enemy base */
boolean bots_squads_pace_goal(long bot_index, real_point3d const *position, real_point3d const *objective,
	real_point3d *goal, long *surface_index)
{
	struct bots_tactics_bot *state = bots_squads_bot(bot_index);
	struct bots_tactics_bot *partner;
	real_point3d enemy, partner_position;
	real mine, theirs, apart;
	long now = game_time_get();

	if (!state || state->squad == NONE || state->partner == NONE || state->role != _bots_role_attacker ||
		!bots_squads_enabled() || bots_tactics_game() != game_engine_ctf || state->team < 0 || state->team > 1 ||
		state->pace_ticks >= BOTS_PACE_TICKS || !bots_vantage_stand(1 - state->team, &enemy) ||
		bots_squads_distance2d(objective, &enemy) > BOTS_PACE_FLAG_HOME)
	{
		state->pace_tick = 0;
		return FALSE;
	}
	partner = bots_squads_bot(state->partner);
	mine = bots_squads_distance2d(position, &enemy);
	if (!partner || partner->role != _bots_role_attacker || mine < BOTS_PACE_BASE || mine > BOTS_PACE_FAR ||
		!bots_squads_on_foot(partner, &partner_position))
	{
		state->pace_tick = 0;
		return FALSE;
	}
	theirs = bots_squads_distance2d(&partner_position, &enemy);
	apart = bots_squads_distance2d(position, &partner_position);
	if (apart <= BOTS_PACE_TOGETHER || theirs - mine < BOTS_PACE_GAP || apart > BOTS_PACE_REACH)
	{
		state->pace_tick = 0;
		return FALSE;
	}
	/* (where it stopped: held there) */
	if (state->pace_tick == 0)
	{
		state->pace_surface = bots_routes_ground(position, &state->pace_point);
		if (state->pace_surface == NONE)
			return FALSE;
		bots_squads.paces++;
	}
	else
		state->pace_ticks += now - state->pace_tick;
	state->pace_tick = now;
	*goal = state->pace_point;
	*surface_index = state->pace_surface;
	return TRUE;
}
