/*
FORGE_SPAWNS.INC

Forge's player spawn points (included by forge.c, after forge_markers.inc,
for its statics), Halo 3's: the object browser's SPAWNING category places a
RESPAWN POINT (where players come back) or an INITIAL SPAWN (where they
start a round), each with a TEAM (any, red, blue) and a GAME TYPE (all, or
one of slayer, CTF, oddball, king of the hill, race, infection) and a
facing. The owner (Oct 22 2026): "spawn points as a forge object is
important".

A spawn point is no object of the map's: it is drawn as the characters'
spawn markers are (forge_markers.inc: the see-through marker in its team's
colour, flat on the ground: a ring, a chevron where the player faces, an
initial spawn's a second ring and a filled centre), for the monitor only, never for the Spartan or in
a map variant played; taken (R), carried and let go like a character's
marker, its properties opened from it (Square while holding it: TEAM, GAME
TYPE, the kind, YAW), deleted with Y (held or under the crosshair). It
counts in the objects' budget, at most FORGE_MAXIMUM_SPAWNS of them. The
map's own spawns are drawn grey beside them (read only).

Saved in the map variant as version 4's player spawn record
(forge_files.h); read back as the map loads, before anyone spawns, so the
round's first spawns use them; a new round keeps them. Online the host's
(forge_net.inc): placed, moved, changed and deleted by the host alone, a
joiner's edit a request it checks (inside the world, the budget, every
number in range: forge_net_protocol.c), every machine given them as
entries (FORGE_NET_FLAG_PLAYER_SPAWN) to draw.

Played (players.c player_spawn, through forge_spawn_location): in a Forge
game or a map variant played, a player (a bot too) spawns at one of the
layout's points that fits - its game type, and in a team game its team or
any -, the engine's own rating choosing among them
(game_engine_get_starting_location_rating: the game type's, an enemy or a
vehicle close, teammates near, randomness as find_best_starting_location_
index weighs it); a round's first spawn of a player takes the initial
spawns if any fits, else the respawn points. When none fits, the map's own
spawns are used, as before; when some fit but every one is blocked (an
enemy on it), the player waits, as the engine has one wait, for
FORGE_SPAWN_WAIT_TICKS at most before the map's are used.
*/

/* ---------- constants */

/* how long a player whose fitting spawn points are all blocked waits before
the map's own are used (game ticks: 3 s) */
#define FORGE_SPAWN_WAIT_TICKS 90

/* the GAME TYPE property's choices, in the window's order: all, then each
game type (its bit, forge_files.h FORGE_FILE_GAME_TYPE_*) */
static struct
{
	unsigned short bits;
	char const *name;
	char const *log_name;
} const forge_spawn_game_types[] =
{
	{ 0, N_("ALL"), "all" },
	{ FORGE_FILE_GAME_TYPE_SLAYER, N_("SLAYER"), "slayer" },
	{ FORGE_FILE_GAME_TYPE_CTF, N_("CTF"), "ctf" },
	{ FORGE_FILE_GAME_TYPE_ODDBALL, N_("ODDBALL"), "oddball" },
	{ FORGE_FILE_GAME_TYPE_KING, N_("KING OF THE HILL"), "king" },
	{ FORGE_FILE_GAME_TYPE_RACE, N_("RACE"), "race" },
	{ FORGE_FILE_GAME_TYPE_INFECTION, N_("INFECTION"), "infection" },
};

/* (forge_spawn_kind_names and its log's names: forge.c's, the browser's) */

/* ---------- private code */

static struct forge_spawn *forge_spawn_of_id(
	long id)
{
	short index;

	if (id == NONE || id == 0)
		return NULL;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_MAXIMUM_SPAWNS; index++)
	{
		if (forge_globals.spawns[index].id == id)
			return &forge_globals.spawns[index];
	}
	return NULL;
}

static short forge_spawns_of_kind(
	short kind)
{
	short index;
	short count = 0;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_MAXIMUM_SPAWNS; index++)
		count += forge_globals.spawns[index].kind == kind ? 1 : 0;
	return count;
}

/* the GAME TYPE choice of a spawn's bits (NONE: several, from a file) */
static short forge_spawn_game_type_index(
	unsigned short bits)
{
	short index;

	for (index = 0; index < (short)NUMBEROF(forge_spawn_game_types); index++)
	{
		if (forge_spawn_game_types[index].bits == bits)
			return index;
	}
	return NONE;
}

static char const *forge_spawn_game_type_log_name(
	unsigned short bits)
{
	short index = forge_spawn_game_type_index(bits);

	return index != NONE ? forge_spawn_game_types[index].log_name : "several";
}

static char const *forge_spawn_team_log_name(
	signed char team)
{
	return team == 0 ? "red" : team == 1 ? "blue" : "any";
}

/* a spawn's readable name ("Respawn Point, Red"), translated */
static void forge_spawn_name(
	struct forge_spawn const *spawn,
	char *name,
	int size)
{
	char const *kind = T(forge_spawn_kind_names[spawn->kind % NUMBER_OF_FORGE_SPAWN_KINDS]);

	if (spawn->team == 0)
		snprintf(name, (size_t)size, T("%s, Red"), kind);
	else if (spawn->team == 1)
		snprintf(name, (size_t)size, T("%s, Blue"), kind);
	else
		snprintf(name, (size_t)size, "%s", kind);
}

static void forge_spawn_log(
	char const *what,
	struct forge_spawn const *spawn)
{
	platform_log("forge: %s the %s #%ld (team %s, game type %s) at %.2f %.2f %.2f facing %.2f %.2f", what,
		forge_spawn_kind_log_names[spawn->kind % NUMBER_OF_FORGE_SPAWN_KINDS], spawn->id,
		forge_spawn_team_log_name(spawn->team), forge_spawn_game_type_log_name(spawn->game_types), spawn->home.position.x,
		spawn->home.position.y, spawn->home.position.z, spawn->home.forward.i, spawn->home.forward.j);
}

/* a spawn point added to the layout (id 0: a new id; a network game's
client takes the host's): NULL when there are the most */
static struct forge_spawn *forge_spawn_add(
	long id,
	short kind,
	signed char team,
	unsigned short game_types,
	struct forge_transform const *home)
{
	struct forge_spawn *spawn;

	if (forge_globals.spawn_count < 0 || forge_globals.spawn_count >= FORGE_MAXIMUM_SPAWNS)
		return NULL;
	spawn = &forge_globals.spawns[forge_globals.spawn_count++];
	csmemset(spawn, 0, sizeof(*spawn));
	spawn->id = id ? id : ++forge_globals.next_id;
	spawn->kind = (short)(kind == _forge_spawn_initial ? _forge_spawn_initial : _forge_spawn_respawn);
	spawn->team = (signed char)(team == 0 || team == 1 ? team : NONE);
	spawn->game_types = (unsigned short)(game_types & FORGE_FILE_SPAWN_GAME_TYPES_MASK);
	spawn->home = *home;
	/* (upright, facing along the ground) */
	spawn->home.forward.k = 0.f;
	if (normalize3d(&spawn->home.forward) < 0.0001f)
		spawn->home.forward = *global_forward3d;
	spawn->home.up = *global_up3d;
	return spawn;
}

static void forge_spawn_remove(
	struct forge_spawn *spawn)
{
	short index = (short)(spawn - forge_globals.spawns);

	if (index < 0 || index >= forge_globals.spawn_count)
		return;
	if (forge_globals.held_marker_id == spawn->id)
		forge_globals.held_marker_id = 0;
	csmemmove(&forge_globals.spawns[index], &forge_globals.spawns[index + 1],
		(size_t)(forge_globals.spawn_count - index - 1) * sizeof(forge_globals.spawns[0]));
	forge_globals.spawn_count--;
}

static void forge_spawns_clear(
	void)
{
	forge_globals.spawn_count = 0;
	forge_globals.spawns_spawned = 0;
	csmemset(forge_globals.spawn_wait_since, 0, sizeof(forge_globals.spawn_wait_since));
}

/* (the map loaded for a Forge game or a map variant played, before anyone
spawns) the layout's player spawns: a version 4 file's only (an older one's
records of that kind were never written, and are skipped as ever) */
static void forge_spawns_from_layout(
	struct forge_file_layout const *layout)
{
	short index;
	short taken = 0;

	if (forge_file_layout_version(layout) < 4)
		return;
	for (index = 0; index < layout->record_count && index < FORGE_FILE_MAXIMUM_RECORDS; index++)
	{
		struct forge_file_record const *record = &layout->records[index];
		struct forge_transform home;
		float angles[3];

		if (record->kind != _forge_record_player_spawn)
			continue;
		home.position.x = record->position[0];
		home.position.y = record->position[1];
		home.position.z = record->position[2];
		angles[0] = record->angles[0];
		angles[1] = angles[2] = 0.f;
		forge_facing_from_angles(angles, &home.forward, &home.up);
		if (forge_spawn_add(0, (record->flags & FORGE_RECORD_SPAWN_INITIAL) ? _forge_spawn_initial : _forge_spawn_respawn,
			record->team, record->game_types, &home))
		{
			taken++;
		}
	}
	if (taken)
	{
		platform_log("forge: map variant \"%s\": %d player spawn points (%d initial, %d respawn)", layout->title, taken,
			forge_spawns_of_kind(_forge_spawn_initial), forge_spawns_of_kind(_forge_spawn_respawn));
	}
}

/* (forge_variant_capture, a save) the spawn points as version 4's records
after the objects'; how many could not be (the file full) */
static short forge_spawns_capture(
	struct forge_file_layout *layout)
{
	short index;
	short skipped = 0;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_MAXIMUM_SPAWNS; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];
		struct forge_file_record *record;
		float angles[3];
		short axis;

		for (axis = 0; axis < 3; axis++)
		{
			real coordinate = axis == 0 ? spawn->home.position.x : axis == 1 ? spawn->home.position.y : spawn->home.position.z;

			if (!(coordinate > -FORGE_FILE_MAXIMUM_COORDINATE + 1.f && coordinate < FORGE_FILE_MAXIMUM_COORDINATE - 1.f))
				break;
		}
		if (axis < 3 || layout->record_count >= FORGE_FILE_MAXIMUM_RECORDS)
		{
			skipped++;
			continue;
		}
		record = &layout->records[layout->record_count];
		csmemset(record, 0, sizeof(*record));
		record->path_index = FORGE_FILE_NO_PATH;
		record->kind = _forge_record_player_spawn;
		record->team = spawn->team == 0 || spawn->team == 1 ? spawn->team : -1;
		record->game_types = (unsigned short)(spawn->game_types & FORGE_FILE_SPAWN_GAME_TYPES_MASK);
		record->flags = spawn->kind == _forge_spawn_initial ? FORGE_RECORD_SPAWN_INITIAL : 0;
		record->position[0] = spawn->home.position.x;
		record->position[1] = spawn->home.position.y;
		record->position[2] = spawn->home.position.z;
		forge_angles_from_facing(&spawn->home.forward, global_up3d, angles);
		record->angles[0] = angles[0];
		layout->record_count++;
		layout->flags |= FORGE_FILE_FLAG_NETGAME_SPAWNS;
	}
	return skipped;
}

/* a spawn point's property stepped (the properties window's rows: team,
the respawn's row its game type, the place at start's its kind, yaw): the
host's and a local game's (a client asks the host: forge_property_apply).
held: its facing the marker's as it is held */
static void forge_spawn_property_apply(
	struct forge_spawn *spawn,
	short property,
	short step,
	boolean held)
{
	switch (property)
	{
	case _forge_property_team:
		/* (any, red, blue) */
		spawn->team = (signed char)(((spawn->team + 1 + step + 3) % 3) - 1);
		break;
	case _forge_property_respawn:
	{
		short count = (short)NUMBEROF(forge_spawn_game_types);
		short index = forge_spawn_game_type_index(spawn->game_types);

		/* (several, from a file: ALL first) */
		index = index == NONE ? (short)(step > 0 ? 0 : count - 1) : (short)((index + step + count) % count);
		spawn->game_types = forge_spawn_game_types[index].bits;
		break;
	}
	case _forge_property_place_at_start:
		spawn->kind = (short)(spawn->kind == _forge_spawn_initial ? _forge_spawn_respawn : _forge_spawn_initial);
		break;
	case _forge_property_yaw:
	case _forge_property_reset_rotation:
	{
		/* (the yaw alone: a player spawns upright) */
		real_vector3d *forward = held ? &forge_globals.held_forward : &spawn->home.forward;
		real stepping = (real)(forge_rotate_snaps[forge_globals.rotate_snap] > 0 ? forge_rotate_snaps[forge_globals.rotate_snap] :
			FORGE_ANGLE_STEP);
		real degrees = property == _forge_property_reset_rotation ? 0.f :
			RADIANS_TO_DEGREES(arctangent(forward->j, forward->i)) / stepping;
		real whole = forge_round(degrees);

		if (property != _forge_property_reset_rotation)
		{
			if (step > 0)
				degrees = (degrees - whole < -0.01f ? whole : whole + 1.f) * stepping;
			else
				degrees = (degrees - whole > 0.01f ? whole : whole - 1.f) * stepping;
		}
		forward->i = cosine(DEGREES_TO_RADIANS(degrees));
		forward->j = sine(DEGREES_TO_RADIANS(degrees));
		forward->k = 0.f;
		if (held)
			forge_globals.held_up = *global_up3d;
		break;
	}
	default:
		return;
	}
	forge_spawn_log("properties of", spawn);
}

static void forge_spawn_properties_get(
	struct forge_spawn const *spawn,
	struct forge_properties *properties)
{
	real_vector3d const *forward = spawn->id == forge_globals.held_marker_id ? &forge_globals.held_forward :
		&spawn->home.forward;
	short game_type = forge_spawn_game_type_index(spawn->game_types);

	forge_spawn_name(spawn, properties->name, sizeof(properties->name));
	properties->spawn = TRUE;
	properties->spawn_kind = spawn->kind;
	properties->team = spawn->team;
	properties->yaw = (short)forge_round(RADIANS_TO_DEGREES(arctangent(forward->j, forward->i)));
	snprintf(properties->game_type_name, sizeof(properties->game_type_name), "%s",
		game_type != NONE ? T(forge_spawn_game_types[game_type].name) : T("SEVERAL"));
}

/* where a spawn point made or let go stands: on the ground under the
point, a little over it (as a character's marker) */
static void forge_spawn_grounded(
	real_point3d *position)
{
	real_vector3d down;
	struct collision_result collision;

	down.i = 0.f;
	down.j = 0.f;
	down.k = -4.f;
	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit), position,
		&down, NONE, &collision))
	{
		position->z += down.k * collision.t + FORGE_MARKER_CLEARANCE;
	}
}

/* (the browser's SPAWNING) a spawn point of the kind placed in front of the
camera, facing the way it looks, and held (a network game's client asks the
host for it, there to take) */
static void forge_spawn_choose(
	short kind)
{
	real_point3d position;
	real_vector3d forward;
	struct forge_transform home;
	struct forge_spawn *spawn;
	real distance;

	if (!forge_camera_get(&position, &forward))
		return;
	if (forge_objects_counted() >= FORGE_MAXIMUM_OBJECTS || forge_globals.spawn_count >= FORGE_MAXIMUM_SPAWNS)
	{
		if (forge_globals.spawn_count >= FORGE_MAXIMUM_SPAWNS)
			forge_notice(T("Spawn points full: %d. Delete one to place another."), FORGE_MAXIMUM_SPAWNS);
		else
			forge_notice(T("Budget full: %d objects. Delete one to place another."), FORGE_MAXIMUM_OBJECTS);
		return;
	}
	distance = MIN(4.f, forge_structure_distance(&position, &forward) - 1.f);
	distance = MAX(distance, FORGE_MINIMUM_HOLD_DISTANCE);
	home.position.x = position.x + forward.i * distance;
	home.position.y = position.y + forward.j * distance;
	home.position.z = position.z + forward.k * distance;
	forge_spawn_grounded(&home.position);
	home.forward = forward;
	home.forward.k = 0.f;
	if (normalize3d(&home.forward) < 0.0001f)
		home.forward = *global_forward3d;
	home.up = *global_up3d;
	if (forge_net_client())
	{
		forge_net_client_spawn_point(kind, &home);
		return;
	}
	spawn = forge_spawn_add(0, kind, NONE, 0, &home);
	if (!spawn)
		return;
	forge_spawn_log("placed", spawn);
	/* (held as a marker is: carried along the crosshair) */
	forge_globals.held_marker_id = spawn->id;
	forge_globals.held_marker_position = spawn->home.position;
	forge_globals.held_forward = spawn->home.forward;
	forge_globals.held_up = *global_up3d;
	forge_globals.held_distance = distance;
	{
		char name[FORGE_NAME_SIZE];

		forge_spawn_name(spawn, name, sizeof(name));
		forge_notice(T("Placed %s"), name);
	}
}

/* a spawn point deleted (Y on its marker): a network game's client asks the
host */
static void forge_spawn_delete(
	long id)
{
	struct forge_spawn *spawn = forge_spawn_of_id(id);
	char name[FORGE_NAME_SIZE];

	if (!spawn)
		return;
	if (forge_net_client())
	{
		if (forge_globals.held_marker_id == id)
			forge_globals.held_marker_id = 0;
		forge_net_client_spawn_delete(id);
		return;
	}
	forge_spawn_name(spawn, name, sizeof(name));
	forge_spawn_log("deleted", spawn);
	forge_notice(T("Deleted %s"), name);
	forge_spawn_remove(spawn);
	forge_globals.deleted_count++;
}

/* the game type played, as a spawn's bit (0: none known - only spawns for
all fit) */
static unsigned short forge_spawn_game_bit(
	void)
{
	struct game_variant *variant;

	if (!game_engine_running())
		return 0;
	if (infection_active())
		return FORGE_FILE_GAME_TYPE_INFECTION;
	variant = game_engine_get_variant();
	switch (variant ? variant->game_engine_index : NONE)
	{
	case game_engine_ctf: return FORGE_FILE_GAME_TYPE_CTF;
	case game_engine_slayer: return FORGE_FILE_GAME_TYPE_SLAYER;
	case game_engine_oddball: return FORGE_FILE_GAME_TYPE_ODDBALL;
	case game_engine_king: return FORGE_FILE_GAME_TYPE_KING;
	case game_engine_race: return FORGE_FILE_GAME_TYPE_RACE;
	}
	return 0;
}

/* ---------- public code */

short forge_spawn_location(
	long player_index,
	struct player_starting_location *location)
{
	struct player_datum *player;
	unsigned short game_bit;
	boolean teams;
	short absolute;
	boolean first;
	short pass;
	short fitting = 0;
	long now = game_time_get();

	if (!forge_globals.game_active || forge_globals.spawn_count <= 0 || player_index == NONE)
		return _forge_spawn_location_map;
	player = player_get(player_index);
	absolute = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	if (absolute < 0 || absolute >= FORGE_SPAWN_PLAYERS)
		return _forge_spawn_location_map;
	game_bit = forge_spawn_game_bit();
	teams = game_engine_running() && game_engine_has_teams();
	first = !(forge_globals.spawns_spawned & (1UL << absolute));
	/* (a round's first spawn: its initial spawns, else its respawn points) */
	for (pass = first ? 0 : 1; pass < 2; pass++)
	{
		short kind = pass == 0 ? _forge_spawn_initial : _forge_spawn_respawn;
		struct player_starting_location candidate;
		short best = NONE;
		real best_rating = 0.f;
		short pass_fitting = 0;
		short index;

		for (index = 0; index < forge_globals.spawn_count && index < FORGE_MAXIMUM_SPAWNS; index++)
		{
			struct forge_spawn const *spawn = &forge_globals.spawns[index];
			real rating;

			if (spawn->kind != kind || (spawn->game_types && !(spawn->game_types & game_bit)) ||
				(teams && spawn->team != NONE && spawn->team != player->team_index))
			{
				continue;
			}
			pass_fitting++;
			csmemset(&candidate, 0, sizeof(candidate));
			candidate.position = spawn->home.position;
			candidate.facing = arctangent(spawn->home.forward.j, spawn->home.forward.i);
			candidate.team_index = (short)(teams && spawn->team != NONE ? spawn->team : player->team_index);
			/* (the game type the spawn fits: the one played) */
			candidate.game_types[0] = (short)(game_engine_running() ? game_engine_get_variant()->game_engine_index : 0);
			/* (the engine's rating, randomised as find_best_starting_location_
			index does) */
			rating = game_engine_get_starting_location_rating(player_index, &candidate) *
				(real)pow((double)real_random_range(0.f, 1.f), 0.5);
			if (rating > best_rating)
			{
				best_rating = rating;
				best = index;
			}
		}
		fitting += pass_fitting;
		if (best != NONE)
		{
			struct forge_spawn const *spawn = &forge_globals.spawns[best];

			csmemset(location, 0, sizeof(*location));
			location->position = spawn->home.position;
			location->facing = arctangent(spawn->home.forward.j, spawn->home.forward.i);
			location->team_index = (short)player->team_index;
			location->game_types[0] = (short)(game_engine_running() ? game_engine_get_variant()->game_engine_index : 0);
			forge_globals.spawns_spawned |= 1UL << absolute;
			forge_globals.spawn_wait_since[absolute] = 0;
			platform_log("forge: player %d (team %d) spawns at the %s #%ld (team %s, game type %s), %d of %d fitting, at %.2f %.2f "
				"%.2f", absolute, (int)player->team_index, forge_spawn_kind_log_names[spawn->kind], spawn->id,
				forge_spawn_team_log_name(spawn->team), forge_spawn_game_type_log_name(spawn->game_types), pass_fitting,
				forge_globals.spawn_count, spawn->home.position.x, spawn->home.position.y, spawn->home.position.z);
			return _forge_spawn_location_found;
		}
	}
	/* (none fits: the map's own spawns; some fit but each is blocked: a
	wait, the map's after FORGE_SPAWN_WAIT_TICKS) */
	if (fitting > 0)
	{
		if (!forge_globals.spawn_wait_since[absolute])
			forge_globals.spawn_wait_since[absolute] = now ? now : 1;
		if (now - forge_globals.spawn_wait_since[absolute] < FORGE_SPAWN_WAIT_TICKS)
			return _forge_spawn_location_wait;
		platform_log("forge: player %d: its %d spawn points blocked for %d s: the map's own spawn", absolute, fitting,
			FORGE_SPAWN_WAIT_TICKS / 30);
	}
	else
	{
		platform_log("forge: player %d (team %d): none of the %d spawn points fits (game type %s): the map's own spawn",
			absolute, (int)player->team_index, forge_globals.spawn_count, forge_spawn_game_type_log_name(game_bit));
	}
	forge_globals.spawns_spawned |= 1UL << absolute;
	forge_globals.spawn_wait_since[absolute] = 0;
	return _forge_spawn_location_map;
}
