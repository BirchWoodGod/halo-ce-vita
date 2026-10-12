/* bots_tactics.h: (port) the offline bots' routes, vantage points and roles
(port/linux/game/bots_routes.c, bots_vantage.c, bots_roles.c), for bots.c,
whose bots they steer: a bot's goal (a defender's post, a vantage point to
hold) and the way its path searches go there (a lane's waypoint). Nothing
here is game state: each map's lanes and vantage points are made again from
the level as the bots' first ticks of a game run, and each bot's part from
its index, its team and its own seeded random numbers, so the bots do the
same from run to run (HALO_FIXED_TICK). */
#ifndef HALO_BOTS_TACTICS_H
#define HALO_BOTS_TACTICS_H

/* ---------- the setting (bots.routes, the Vita's settings panel's Bot
routes) */

enum
{
	/* every bot the shortest way (the bots before: all down the middle) */
	_bots_routes_direct = 0,
	/* the attackers spread over the map's lanes: the direct route and up to
	two side lanes */
	_bots_routes_lanes,
	/* most the direct route, every third attacker a side lane */
	_bots_routes_mixed,
	NUMBER_OF_BOTS_ROUTES,
};

/* a bot's part in its team (bots_roles.c) */
enum
{
	/* the game's objective (the flag, the hill, the ball, the race) */
	_bots_role_attacker = 0,
	/* CTF: about its team's flag stand; King of the Hill: over the hill from
	a vantage point */
	_bots_role_defender,
	/* a vantage point held, scanning, then another */
	_bots_role_sniper,
	/* Slayer: about the map, after the enemy */
	_bots_role_roamer,
	NUMBER_OF_BOTS_ROLES,
};

/* the most waypoints a lane goes by, side lanes, vantage points kept a map,
places they are scored by (the flag stands, the hills, the ball's spawns,
the direct route), and points of the direct route kept */
#define BOTS_LANE_WAYPOINTS 2
#define BOTS_SIDE_LANES 2
#define BOTS_VANTAGE_MAXIMUM 6
#define BOTS_PLACES_MAXIMUM 16
#define BOTS_ROUTE_POINTS 17

/* ---------- what bots.c tells */

/* a map's start and end (bots.c's bots_initialize_for_new_map and
bots_dispose_from_old_map): all forgotten; the walkable graph's memory let go */
void bots_tactics_initialize_for_new_map(void);
void bots_tactics_dispose_from_old_map(void);
/* once a bots' tick, before the bots: the setting, the map's lanes and
vantage points made a bounded step a tick, and the teams' roles */
void bots_tactics_update(void);
/* a bot made for a player (bots_bot_reset: its index, its player), and a bot
dead (its vantage point let go, its lane's waypoints forgotten) */
void bots_tactics_bot_reset(long bot_index, long player_index);
void bots_tactics_bot_dead(long bot_index);

/* bots_choose_goal: a defender's post or a vantage point the bot goes to (and
holds), with its surface; FALSE for none (the game's goals as before) */
boolean bots_tactics_goal(long bot_index, long player_index, real_point3d const *position, real_point3d *goal,
	long *surface_index);
/* bots_search_ask, on foot to the goal given (an objective, a roaming place,
an enemy last seen): the waypoint of the bot's lane its search goes to
instead, into *goal and *surface_index; FALSE for the goal itself */
boolean bots_routes_search_goal(long bot_index, real_point3d const *start, real_point3d *goal, long *surface_index);
/* bots_follow_path, each tick: TRUE when the bot has come to its lane's
waypoint (or gave it up): a new path, on to the goal */
boolean bots_routes_waypoint_reached(long bot_index, real_point3d const *position);
/* bots_search_take: a search's answer for the goal it went to, reached or not
and its surfaces all tried (spent): TRUE when that was a waypoint or a
vantage point now given up (a new search, on to the goal) */
boolean bots_tactics_search_answer(long bot_index, real_point3d const *goal, boolean reached, boolean spent);
/* bots_bot_walk, standing (not moving): where the bot looks, at a vantage
point or a post (towards the places it watches), turned into *yaw and *pitch */
void bots_tactics_look(long bot_index, real_point3d const *position, long now, real *yaw, real *pitch);
/* bots_bot_update, fighting on foot: at its vantage point the bot stays
there (*moving, *direction and *throttle changed), and leaves it when the
enemy comes this near */
void bots_tactics_fight(long bot_index, real_point3d const *position, real target_distance, boolean *moving,
	real *direction, real *throttle);
/* bots_report, each minute: halo.log's tactics line */
void bots_tactics_report(long now);

/* ---------- between the three files */

/* a bot's part: its role and lane, its lane's waypoints, its vantage point */
struct bots_tactics_bot
{
	long player_index;
	unsigned long random;
	/* its team (0 in a game without teams), its role, its lane (0 the direct
	route, 1 and 2 the side lanes) and its place among its team's attackers */
	long team;
	short role;
	short lane;
	short rank;
	/* lanes: the waypoint its searches go by (of its lane's, NONE for
	none), since when; the waypoints passed this life; the lane given up this
	life (a waypoint it found no way to, or did not come to) */
	short waypoint;
	long waypoint_tick;
	boolean passed[BOTS_LANE_WAYPOINTS];
	boolean lane_given_up;
	/* vantage: the point it goes to (NONE for none), since when, when it got
	there (0 before) and for how long it holds it; none until then (after
	one), the last held, and those it found no way to, not until then */
	short vantage;
	long vantage_tick;
	long hold_tick;
	long hold_ticks;
	long rest_until_tick;
	short last_vantage;
	unsigned long failed_vantages;
	long failed_until_tick;
	/* looking about there: at which watched place, until when */
	short look_place;
	long look_until_tick;
	/* a defender's post (of its team's), until when */
	short post;
	long post_until_tick;
};

extern struct bots_tactics_bot bots_tactics_bots[];

/* the map's walkable graph, while the lanes and vantage points are made
(bots_routes.c; let go once made): a node a walkable surface of the
structure (its middle, area, how flat, reached from the first base), and its
neighbours, the walkable surfaces across its edges */
struct bots_graph_node
{
	real_point3d middle;
	real area;
	real normal_k;
	long surface_index;
	/* the cost of the way from the first base (REAL_MAX: not reached) */
	real cost;
	/* across the direct route: how far from it, on which side, and how far
	along it (0 at the first base, 1 at the second) */
	real route_distance;
	real route_side;
	real route_along;
};

struct bots_graph
{
	long node_count;
	struct bots_graph_node *nodes;
	long *link_first;
	long *links;
	long link_count;
};

extern struct bots_graph bots_graph;

/* the map's places the bots care about (bots_vantage.c): the vantage points
are scored by what they see of them, and look at them */
struct bots_place
{
	real_point3d point;
	real weight;
	char const *name;
};

/* what is known of the map once made (bots_routes.c): ready, the bases, the
direct route (points along it, its length), and whether it has lanes */
struct bots_routes_map
{
	boolean ready;
	boolean has_bases;
	real_point3d bases[2];
	long base_surfaces[2];
	long route_point_count;
	real_point3d route_points[BOTS_ROUTE_POINTS];
	real route_length;
	long lane_count;
};

extern struct bots_routes_map bots_routes_map;

/* bots_routes.c's: the setting (read once a second: bots_routes_read_setting);
the map's routes made a step a tick (TRUE once made), the graph let go
(once the vantage points are made too), all forgotten (a new map) */
short bots_routes_mode(void);
void bots_routes_read_setting(void);
boolean bots_routes_step(void);
boolean bots_routes_graph_made(void);
void bots_routes_release_graph(void);
void bots_routes_reset(void);
/* the lane (0 the direct route) of the bot of this rank among its team's
attackers, by the setting; a lane's name; a side lane's waypoint (FALSE past
its last) */
short bots_routes_lane_for_rank(short rank);
char const *bots_routes_lane_name(short lane);
boolean bots_routes_lane_waypoint(long lane_index, long waypoint, real_point3d *point);
/* a search to the bot's waypoint found no way: the lane given up (TRUE when
the goal was its waypoint) */
boolean bots_routes_search_failed(long bot_index, real_point3d const *goal);
/* the direct route's polyline: how far along (0 to 1) and
how far off it (signed: the side) a point is */
void bots_routes_place_on_route(real_point3d const *point, real *along, real *offset);
/* a point's ground: the walkable surface under it (NONE for none), and the
point there */
long bots_routes_ground(real_point3d const *point, real_point3d *ground);
/* (debug: HALO_BOT_ROLES=0, neither roles nor vantage points: the bots as
before but for their routes) */
boolean bots_tactics_enabled(void);
/* the gametype's engine (game_engine_ctf ...), read once a tick */
long bots_tactics_game(void);
/* a bot's next random number (its own, seeded by its index and the game's) */
unsigned long bots_tactics_random(struct bots_tactics_bot *state);

/* bots_vantage.c's: the map's vantage points made after the routes, a step a
tick (TRUE when done); its memory let go; and a bot's vantage point let go */
void bots_vantage_begin(void);
boolean bots_vantage_step(void);
void bots_vantage_dispose(void);
void bots_vantage_release(long bot_index, char const *why);
/* the vantage point for a bot of the role: the best not another's, near
`position` (NONE for none); for a defender, one over its team's stand (CTF)
or the hill (King: the game's goal, `objective`) */
short bots_vantage_choose(long bot_index, real_point3d const *position, real_point3d const *objective);
/* a vantage point's ground and surface; a defender's post of its team */
void bots_vantage_point(short vantage, real_point3d *point, long *surface_index);
boolean bots_vantage_post(long team, short post, real_point3d *point, long *surface_index);
long bots_vantage_post_count(long team);
long bots_vantage_count(void);
/* the places a vantage point sees (a bit a place), and the places */
unsigned long bots_vantage_sees(short vantage);
long bots_places_count(void);
struct bots_place const *bots_place_get(long index);
/* the statistics of halo.log's tactics line */
struct bots_tactics_counts
{
	long lanes_taken;
	long waypoints_passed;
	long lanes_given_up;
	long vantages_chosen;
	long vantages_reached;
	long vantage_leaves[5];
	long vantage_failures;
	long posts_taken;
};

extern struct bots_tactics_counts bots_tactics_counts;

/* why a bot left its vantage point */
enum
{
	_bots_vantage_left_time = 0,
	_bots_vantage_left_enemy,
	_bots_vantage_left_ammunition,
	_bots_vantage_left_hurt,
	_bots_vantage_left_other,
};

/* the platform layer's */
const char *config_string(char const *name);
void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);

#endif
