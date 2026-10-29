/* bots_nav.h: (port) the bots' map knowledge beyond the engine's path
searches (port/linux/game/bots_nav.c), for bots.c: which walkable surfaces
join (the level's walkable regions: a search never leaves its own), the
jumps and drops between them a player takes (onto a crate, a ledge or a
railing, off a balcony), and the way from one region to another by those,
the teleporters and the ladders. Nothing here is game state: each map's
regions and jumps are made again from the level as the bots' first ticks of
a game run, a bounded step a tick, so the bots do the same from run to run
(HALO_FIXED_TICK). */
#ifndef HALO_BOTS_NAV_H
#define HALO_BOTS_NAV_H

/* what a hop is: a teleporter or a ladder (bots.c's, by its index there), a
jump up onto higher ground, a drop down (stepping off, or a hop over a
railing first), a jump across a gap */
enum
{
	_bots_hop_teleporter = 0,
	_bots_hop_ladder,
	_bots_hop_jump_up,
	_bots_hop_drop,
	_bots_hop_gap,
	NUMBER_OF_BOTS_HOPS,
};

#define BOTS_NAV_MAXIMUM_HOPS 640

struct bots_nav_hop
{
	short kind;
	/* the jump is pressed at the edge (an up jump, a gap, a railing first) */
	boolean needs_jump;
	/* bots.c's teleporter (or ladder) index, NONE for a jump */
	long teleporter_index;
	/* where it is taken from (on its walkable surface) and where it comes out
	(the landing, on its own), and the edge between, for a jump */
	real_point3d entrance;
	long entrance_surface_index;
	real_point3d exit;
	long exit_surface_index;
	real_point3d edge;
	/* the walk it stands for (its own length, and a little for the time a
	jump or a teleporter takes) */
	real cost;
	/* the times bots took it and came out where it goes, and failed to; when
	it is gone for again after a failure */
	long successes;
	long failures;
	long rest_until_tick;
};

/* a map's start (all forgotten, the memory let go) and its end */
void bots_nav_reset(void);
/* the bots' biped: how high its jump goes (the tag's jump velocity against
the gravity), what crouching in the air adds, and the fall it takes no harm
from (the globals' falling damage); asked of the first bot's unit */
void bots_nav_set_biped(long biped_definition_index);
/* once a bots' tick: a bounded step of the regions, the jumps found and
tested (no step once done) */
void bots_nav_update(void);
boolean bots_nav_ready(void);
/* (HALO_TICK_PROFILE) the step being made: 0 none yet ... 5 done */
short bots_nav_phase(void);
/* bots.c's teleporters and ladders, once its map is prepared: hops too */
void bots_nav_add_teleporter(long teleporter_index, boolean ladder, real_point3d const *entrance,
	long entrance_surface_index, real_point3d const *exit, long exit_surface_index);
/* whether a surface is a ramp: not one of the AI's walkable surfaces, but
one the biped walks on (facing up, no steeper than it climbs), joined to the
regions (bots.c opens them to its searches) */
boolean bots_nav_ramp(long surface_index);
/* the region of a walkable surface (NONE: not walkable, or not known yet) */
long bots_nav_region(long surface_index);
/* the way from a point on one walkable surface to a point on another: NONE
when both are in one region (a search walks there) or either is not known;
-2 when no way is known (no hop joins them); else the first hop to take, its
walk's estimate in *cost */
#define BOTS_NAV_NO_WAY (-2L)
long bots_nav_route(long start_surface_index, real_point3d const *start, long goal_surface_index,
	real_point3d const *goal, long now, real *cost);
/* whether a goal's surface is one a bot on start's can get to (TRUE when
not known) */
boolean bots_nav_reachable(long start_surface_index, long goal_surface_index, long now);
/* the map's hops */
long bots_nav_hop_count(void);
struct bots_nav_hop *bots_nav_hop_get(long hop_index);
/* a hop taken: came out where it goes, or not (left be a while; for the map
after three failures with no success) */
void bots_nav_hop_result(long hop_index, boolean success, long now);
boolean bots_nav_hop_usable(long hop_index, long now);
/* the jumps and drops near a walk, for a shortcut: those whose entrance is
in the region given, in turn (*cursor from 0; NONE past the last) */
long bots_nav_next_jump_from(long region, long *cursor);
/* the jump a biped makes (world units): its height, crouched */
real bots_nav_jump_height(void);
/* halo.log's nav line */
void bots_nav_report(long now);

#endif
