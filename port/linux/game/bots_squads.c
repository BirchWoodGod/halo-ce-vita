/*
BOTS_SQUADS.C

(port) The offline bots' squads (bots.c's bots, in team games): the owner,
Oct 17 2026, "they seem to just charge towards the flag", "the bots need to
work as a team, some kind of squad logic". Before, each bot fought alone:
it saw its own enemies, and every attacker ran at the enemy flag by itself,
past the side lanes' last waypoint straight in, one after another.

- Squads: each team's attackers (or Slayer's roamers: bots_roles.c), by
  their order, two to a squad, the first its leader; an odd one out alone.
  A squad's two take one lane (bots_routes_lane_for_rank: by the squad).
- Staging (CTF, a squad on a side lane: Bot routes Lanes or Mixed): an
  attacker on its way to the enemy flag at home goes first to its squad's
  staging point - its side lane's waypoint nearest the enemy base - and
  waits for its partner there (9 units), 6 seconds at most (3 alone, or its
  partner dead), answering no calls meanwhile, then the two go in together.
  (A staging point on the direct route, 62% of the way, gathered every
  squad of a Direct game at one spot: 11 flags taken in six minutes against
  19 without, the base reached alone 72% of the time against 20%.) Once a life: a bot that is already
  past it, or comes back, goes on.
- Following (not CTF: Slayer, King, Oddball): a squad's second keeps with
  its leader, going to it when more than 9 units away.
- Callouts: a bot that sees an enemy calls it out to its team (twice a
  second at most), and one hurt with no enemy seen calls where it is. A
  teammate's call the last 3 seconds draws an attacker or roamer that is not
  fighting (20 units at most, a roamer's 35; a CTF attacker its partner's
  alone) there for 6 seconds; and the
  enemy a teammate called the last 2 seconds is the one a bot shoots first
  (bots.c's target score: as if 3 units nearer).
- Escorts (bots.c's bots_ctf_goal): the enemy flag carried by a teammate,
  its partner and the two bots nearest it go along with the carrier; the
  others to their own flag stand, where the capture is made.
halo.log's squads line each minute (after the tactics line): the squads,
callouts, help given, follows, stagings (together, alone, waits' seconds).

(debug) HALO_BOT_SQUADS=0: none of this (callouts, help, staging, following
off; escorts as before: everyone).
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
/* a call heard this long, and a called enemy shot first this long */
#define BOTS_CALLOUT_TICKS (TICKS_PER_SECOND * 3)
#define BOTS_CALLOUT_FOCUS_TICKS (TICKS_PER_SECOND * 2)
#define BOTS_CALLOUT_EVERY_TICKS (TICKS_PER_SECOND / 2)
/* how far a call draws an attacker, and a roamer; for how long; nearer than
this, it is there */
#define BOTS_HELP_REACH 20.0f
#define BOTS_HELP_REACH_ROAMER 35.0f
#define BOTS_HELP_TICKS (TICKS_PER_SECOND * 6)
#define BOTS_HELP_THERE 4.0f
/* a second further from its leader than this goes to it; where it goes,
again when the leader is this far from it */
#define BOTS_FOLLOW_DISTANCE 9.0f
#define BOTS_FOLLOW_MOVED 4.0f
/* the staging point on the direct route (of the way to the enemy base); at
it within this; its partner near within this; the wait, and alone */
#define BOTS_STAGE_ALONG 0.62f
#define BOTS_STAGE_ARRIVED 4.0f
#define BOTS_STAGE_PARTNER 9.0f
#define BOTS_STAGE_WAIT_TICKS (TICKS_PER_SECOND * 6)
#define BOTS_STAGE_ALONE_TICKS (TICKS_PER_SECOND * 3)
/* the objective is the enemy flag at home within this of its stand */
#define BOTS_STAGE_FLAG_HOME 3.0f

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
	/* the staging points, a team's a lane's, found once the routes are
	made (valid: 1 found, -1 none) */
	signed char stage_valid[2][1 + BOTS_SIDE_LANES];
	real_point3d stage_points[2][1 + BOTS_SIDE_LANES];
	long stage_surfaces[2][1 + BOTS_SIDE_LANES];
	/* halo.log's squads line */
	long squads;
	long callouts_made;
	long helps;
	long follows;
	long staged_together;
	long staged_alone;
	long staged_past;
	long stage_wait_ticks;
} bots_squads;

/* ---------- small helpers */

static boolean bots_squads_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		char const *setting = getenv("HALO_BOT_SQUADS");

		enabled = setting && setting[0] && !atoi(setting) ? 0 : 1;
	}
	return enabled != 0 && game_engine_has_teams();
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

/* a team's staging point on a lane (0 the direct route): the side lane's
waypoint nearest the enemy stand, or on the direct route BOTS_STAGE_ALONG
of the way from its own base; FALSE for none (no routes, no stands) */
static boolean bots_squads_stage_point(long team, short lane, real_point3d *point, long *surface_index)
{
	real_point3d own, enemy;

	if (team < 0 || team > 1 || lane < 0 || lane > BOTS_SIDE_LANES || !bots_routes_map.ready)
		return FALSE;
	if (!bots_squads.stage_valid[team][lane])
	{
		real_point3d found;
		boolean any = FALSE;

		bots_squads.stage_valid[team][lane] = -1;
		if (!bots_vantage_stand(team, &own) || !bots_vantage_stand(1 - team, &enemy))
			return FALSE;
		if (lane > 0)
		{
			real_point3d waypoint;
			real best = REAL_MAX;
			long index;

			for (index = 0; bots_routes_lane_waypoint(lane - 1, index, &waypoint); index++)
			{
				real distance = bots_squads_distance2d(&waypoint, &enemy);

				if (distance < best)
				{
					best = distance;
					found = waypoint;
					any = TRUE;
				}
			}
		}
		else if (bots_routes_map.route_point_count >= 2)
		{
			/* (the route runs from the first base to the second: from the
			end nearer the team's own stand) */
			long last = bots_routes_map.route_point_count - 1;
			boolean reversed = bots_squads_distance2d(&bots_routes_map.route_points[0], &own) >
				bots_squads_distance2d(&bots_routes_map.route_points[last], &own);
			long index = (long)(BOTS_STAGE_ALONG * last + 0.5f);

			found = bots_routes_map.route_points[reversed ? last - index : index];
			any = TRUE;
		}
		if (!any)
			return FALSE;
		bots_squads.stage_surfaces[team][lane] = bots_routes_ground(&found, &bots_squads.stage_points[team][lane]);
		if (bots_squads.stage_surfaces[team][lane] == NONE)
			return FALSE;
		bots_squads.stage_valid[team][lane] = 1;
	}
	if (bots_squads.stage_valid[team][lane] < 0)
		return FALSE;
	*point = bots_squads.stage_points[team][lane];
	*surface_index = bots_squads.stage_surfaces[team][lane];
	return TRUE;
}

/* ---------- what bots_roles.c tells */

void bots_squads_reset(void)
{
	csmemset(&bots_squads, 0, sizeof(bots_squads));
}

void bots_squads_assign(void)
{
	boolean enabled = bots_squads_enabled();
	long index, other;

	bots_squads.squads = 0;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_tactics_bot *state = &bots_tactics_bots[index];
		short squad = NONE;

		if (enabled && state->team != NONE &&
			(state->role == _bots_role_attacker || state->role == _bots_role_roamer))
		{
			squad = (short)(state->rank / 2);
		}
		if (squad != state->squad)
		{
			state->staged = FALSE;
			state->stage_tick = 0;
		}
		state->squad = squad;
		state->partner = NONE;
		state->leader = squad != NONE && state->rank % 2 == 0;
	}
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_tactics_bot *state = &bots_tactics_bots[index];

		if (state->squad == NONE)
			continue;
		bots_squads.squads += state->leader;
		for (other = index + 1; other < BOTS_MAXIMUM; other++)
		{
			struct bots_tactics_bot *mate = &bots_tactics_bots[other];

			if (mate->squad == state->squad && mate->team == state->team)
			{
				state->partner = other;
				mate->partner = index;
				break;
			}
		}
		/* (alone in its squad: its own leader) */
		if (state->partner == NONE)
			state->leader = TRUE;
	}
}

void bots_squads_bot_dead(long bot_index)
{
	struct bots_tactics_bot *state = bots_squads_bot(bot_index);

	if (!state)
		return;
	state->staged = FALSE;
	state->stage_tick = 0;
	state->help_until_tick = 0;
	state->follow_surface = NONE;
}

void bots_squads_report(long now)
{
	if (!bots_squads_enabled())
		return;
	platform_log("bots: squads at tick %ld: %ld squads; callouts %ld, gone to help %ld, followed the leader %ld; "
		"staged together %ld, alone %ld, already past %ld (waited %.0f s in all)", now, bots_squads.squads,
		bots_squads.callouts_made, bots_squads.helps, bots_squads.follows, bots_squads.staged_together,
		bots_squads.staged_alone, bots_squads.staged_past, (double)bots_squads.stage_wait_ticks / TICKS_PER_SECOND);
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
	real reach;

	if (!state || state->squad == NONE || !bots_squads_enabled())
		return FALSE;
	/* (waiting at its staging point: it holds it, its partner on the way) */
	if (state->stage_tick != 0 && !state->staged)
		return FALSE;
	/* (on its way still, or there looking about) */
	if (now < state->help_until_tick)
	{
		*goal = state->help_point;
		*surface_index = state->help_surface;
		return TRUE;
	}
	reach = state->role == _bots_role_roamer ? BOTS_HELP_REACH_ROAMER : BOTS_HELP_REACH;
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
		if (distance > reach || distance < BOTS_HELP_THERE)
			continue;
		/* (a CTF attacker answers its partner alone: the others' calls,
		the defenders' at home above all, drew the attackers back and none
		came to the enemy flag) */
		if (bots_tactics_game() == game_engine_ctf && state->role == _bots_role_attacker &&
			callout->caller != state->partner)
		{
			continue;
		}
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

	if (!state || state->squad == NONE || state->leader || !bots_squads_enabled() ||
		bots_tactics_game() == game_engine_ctf)
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

boolean bots_squads_stage_goal(long bot_index, real_point3d const *position, real_point3d const *objective,
	real_point3d *goal, long *surface_index)
{
	struct bots_tactics_bot *state = bots_squads_bot(bot_index);
	struct bots_tactics_bot *partner;
	real_point3d enemy, stage, partner_position;
	long stage_surface, now = game_time_get();
	boolean partner_alive, partner_near;

	if (!state || state->squad == NONE || state->staged || state->role != _bots_role_attacker ||
		!bots_squads_enabled() || bots_tactics_game() != game_engine_ctf || state->team < 0 || state->team > 1 ||
		!bots_vantage_stand(1 - state->team, &enemy) ||
		bots_squads_distance2d(objective, &enemy) > BOTS_STAGE_FLAG_HOME ||
		state->lane == 0 || state->lane_given_up ||
		!bots_squads_stage_point(state->team, state->lane, &stage, &stage_surface))
	{
		return FALSE;
	}
	/* (nearer the enemy base than its staging point already: on in) */
	if (state->stage_tick == 0 &&
		bots_squads_distance2d(position, &enemy) < bots_squads_distance2d(&stage, &enemy) - BOTS_STAGE_ARRIVED)
	{
		state->staged = TRUE;
		bots_squads.staged_past++;
		return FALSE;
	}
	if (bots_squads_distance2d(position, &stage) > BOTS_STAGE_ARRIVED && state->stage_tick == 0)
	{
		*goal = stage;
		*surface_index = stage_surface;
		return TRUE;
	}
	if (state->stage_tick == 0)
		state->stage_tick = now;
	partner = bots_squads_bot(state->partner);
	partner_alive = partner && bots_squads_on_foot(partner, &partner_position);
	partner_near = partner_alive && bots_squads_distance2d(&partner_position, &stage) <= BOTS_STAGE_PARTNER;
	if (partner_near || now - state->stage_tick >= (partner_alive ? BOTS_STAGE_WAIT_TICKS : BOTS_STAGE_ALONE_TICKS))
	{
		state->staged = TRUE;
		bots_squads.stage_wait_ticks += now - state->stage_tick;
		if (partner_near)
			bots_squads.staged_together++;
		else
			bots_squads.staged_alone++;
		return FALSE;
	}
	*goal = stage;
	*surface_index = stage_surface;
	return TRUE;
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
