/*
FORGE_NETGAME.INC

A layout's markers made the map's netgame flags (included by forge.c, after
forge_spawns.inc, for its statics): Forge's teleporters work as the map's
own do, by the engine's own rule, not one of ours. Halo CE's teleporters are
netgame flags (scenario_definitions.h _netgame_flag_teleporter_source and
_target), a sender and the receiver of its team index; each tick the engine
(game_engine.c game_engine_update_teleporter) sends a player whose unit
stands within half a world unit of a sender to the first receiver of that
index - blocked while a unit stands there, the player's facing turned by the
two flags' facings, the flash and the sound -, and keeps it from being sent
back until it steps off. A unit in a vehicle is never sent (its position is
its seat's, not the world's), as on the map's own.

So a Forge game's, or a map variant's played, teleporters are given to the
engine as flags of its own: the scenario's netgame flags block is pointed at
a copy of the map's own flags with the layout's teleporters after them
(forge_netgame_flags_build), each one's team index its channel past
FORGE_NETGAME_TELEPORTER_TEAM so that it never pairs with the map's own
teleporters; nothing else of the scenario is touched. Built before the game
type reads the flags (game.c, forge_netgame_flags_prepare, before
game_engine_initialize_for_new_map) and again whenever the layout's
teleporters change (forge_update, before the tick runs: the block never moves
under it). The map's own order is kept, its flags first, so the indices the
game type took from them stay right; a player's sender index (teleporter_index:
the one it stands on and is not sent from again) is looked up again after a
change. Any game that is not a Forge game nor a map variant played, and any
other map, has the map's own block back, as it was: its game logic is not
changed.

A teleporter is drawn as its marker (forge_markers.inc) for everyone in a
Forge game or a map variant played, the monitor and the Spartan alike, in
its channel's colour: the sender with a second ring and a filled centre, the
receiver a plain ring, its chevron where a player sent there faces.
*/

/* ---------- constants */

/* the team index of the layout's teleporters' channel 0 (a map's own are
0 to 15 at most) */
#define FORGE_NETGAME_TELEPORTER_TEAM 64
/* the most flags the block holds: the map's own (Blood Gulch has 30-odd),
then the layout's */
#define FORGE_NETGAME_MAXIMUM_FLAGS 512
/* (the log's teleport: a unit moved further than this in a frame, onto a
receiver) */
#define FORGE_NETGAME_TELEPORT_JUMP 3.f
#define FORGE_NETGAME_TELEPORT_NEAR 1.f

/* ---------- globals */

static struct
{
	/* the scenario whose block points at flags (NULL none), and that block
	as the map had it */
	struct scenario *scenario;
	struct tag_block original;
	/* to be built again (a map loaded), and the layout's teleporters as
	they were built (a hash of each one's kind, channel, place and facing:
	any change builds them again) */
	boolean dirty;
	unsigned long built_hash;
	/* the block's flags: the map's own, then the layout's */
	struct scenario_netgame_flag flags[FORGE_NETGAME_MAXIMUM_FLAGS];
	long count;
	long own_count;
	/* (the log's teleports) each player's unit where it was the last frame */
	real_point3d last_position[FORGE_SPAWN_PLAYERS];
	long last_unit[FORGE_SPAWN_PLAYERS];
	long teleports;
} forge_netgame;

/* ---------- private code */

/* whether this game gives its layout's markers to the engine: a Forge game
or a map variant played, of the loaded map */
static boolean forge_netgame_wanted(
	void)
{
	return forge_globals.game_active;
}

static boolean forge_spawn_is_teleporter(
	struct forge_spawn const *spawn)
{
	return spawn->kind == _forge_spawn_teleporter_sender || spawn->kind == _forge_spawn_teleporter_receiver;
}

/* how many teleporters the layout has (of a kind, NONE any), on a channel
(NONE any) */
static short forge_teleporters_count(
	short kind,
	short channel)
{
	short index;
	short count = 0;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_MAXIMUM_SPAWNS; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];

		if (forge_spawn_is_teleporter(spawn) && (kind == NONE || spawn->kind == kind) &&
			(channel == NONE || spawn->channel == channel))
		{
			count++;
		}
	}
	return count;
}

/* a new teleporter's channel: a sender's the first channel with no sender;
a receiver's the first channel with a sender and no receiver, else the
first with neither */
static short forge_teleporter_new_channel(
	short kind)
{
	short channel;

	if (kind == _forge_spawn_teleporter_receiver)
	{
		for (channel = 0; channel < FORGE_TELEPORTER_CHANNELS; channel++)
		{
			if (forge_teleporters_count(_forge_spawn_teleporter_sender, channel) &&
				!forge_teleporters_count(_forge_spawn_teleporter_receiver, channel))
			{
				return channel;
			}
		}
	}
	for (channel = 0; channel < FORGE_TELEPORTER_CHANNELS; channel++)
	{
		if (!forge_teleporters_count(kind, channel) && (kind == _forge_spawn_teleporter_sender ||
			!forge_teleporters_count(_forge_spawn_teleporter_sender, channel)))
		{
			return channel;
		}
	}
	return 0;
}

/* the layout's markers changed: the flags built again at the next update */
static void forge_netgame_changed(
	void)
{
	forge_netgame.dirty = TRUE;
}

/* the map's own block back where it was ours (the same tags still loaded) */
static void forge_netgame_restore(
	struct scenario *scenario)
{
	if (scenario && scenario->netgame_flags.address == (void *)forge_netgame.flags)
	{
		scenario->netgame_flags = forge_netgame.original;
		platform_log("forge: the map's own %ld netgame flags back", (long)scenario->netgame_flags.count);
	}
	forge_netgame.scenario = NULL;
	forge_netgame.count = 0;
}

/* each player's sender (the one it stands on, not to be sent from again)
looked up in the block as it is now */
static void forge_netgame_players_resync(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (!player_data)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		player->teleporter_index = NONE;
		if (player->unit_index != NONE && object_try_and_get(player->unit_index) &&
			object_get(player->unit_index)->object.parent_object_index == NONE)
		{
			player->teleporter_index = find_netgame_flag(&object_get(player->unit_index)->object.position, 1.0f, 0.0f,
				_netgame_flag_teleporter_source, NONE);
		}
	}
}

/* the block built: the map's own flags, then the layout's teleporters, each
on the ground under its marker */
static void forge_netgame_flags_build(
	void)
{
	struct scenario *scenario;
	struct tag_block const *own;
	long index;
	short added = 0;

	forge_netgame.dirty = FALSE;
	if (global_scenario_index == NONE || (scenario = global_scenario_get()) == NULL)
		return;
	/* (the map's own: as it had them, the first time; ours since) */
	if (scenario->netgame_flags.address != (void *)forge_netgame.flags)
		forge_netgame.original = scenario->netgame_flags;
	own = &forge_netgame.original;
	if (!forge_netgame_wanted() || !forge_teleporters_count(NONE, NONE))
	{
		forge_netgame_restore(scenario);
		return;
	}
	forge_netgame.count = 0;
	for (index = 0; index < own->count && forge_netgame.count < FORGE_NETGAME_MAXIMUM_FLAGS; index++)
		forge_netgame.flags[forge_netgame.count++] = *TAG_BLOCK_GET_ELEMENT(own, index, struct scenario_netgame_flag);
	forge_netgame.own_count = forge_netgame.count;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_MAXIMUM_SPAWNS; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];
		struct scenario_netgame_flag *flag;

		if (!forge_spawn_is_teleporter(spawn) || forge_netgame.count >= FORGE_NETGAME_MAXIMUM_FLAGS)
			continue;
		flag = &forge_netgame.flags[forge_netgame.count++];
		csmemset(flag, 0, sizeof(*flag));
		flag->position = spawn->home.position;
		/* (the marker stands its clearance over the ground; the flag on it,
		where a unit standing there has its position) */
		flag->position.z -= FORGE_MARKER_CLEARANCE;
		flag->facing = arctangent(spawn->home.forward.j, spawn->home.forward.i);
		flag->type = spawn->kind == _forge_spawn_teleporter_sender ? _netgame_flag_teleporter_source :
			_netgame_flag_teleporter_target;
		flag->team_index = (short)(FORGE_NETGAME_TELEPORTER_TEAM + PIN(spawn->channel, 0, FORGE_TELEPORTER_CHANNELS - 1));
		added++;
	}
	scenario->netgame_flags.count = forge_netgame.count;
	scenario->netgame_flags.address = forge_netgame.flags;
	forge_netgame.scenario = scenario;
	forge_netgame_players_resync();
	platform_log("forge: netgame flags: the map's %ld and %d teleporters (%d senders, %d receivers)", forge_netgame.own_count,
		added, forge_teleporters_count(_forge_spawn_teleporter_sender, NONE),
		forge_teleporters_count(_forge_spawn_teleporter_receiver, NONE));
}

/* (each update) the log's teleports: a player's unit that jumped onto one
of the layout's receivers since the last frame */
static void forge_netgame_teleports_watch(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (!forge_netgame.scenario || !player_data)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		short absolute = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		real_point3d position;
		long index;

		if (absolute < 0 || absolute >= FORGE_SPAWN_PLAYERS)
			continue;
		if (player->unit_index == NONE || !object_try_and_get(player->unit_index))
		{
			forge_netgame.last_unit[absolute] = NONE;
			continue;
		}
		position = object_get(player->unit_index)->object.position;
		if (forge_netgame.last_unit[absolute] == player->unit_index &&
			distance_squared3d(&position, &forge_netgame.last_position[absolute]) >
				FORGE_NETGAME_TELEPORT_JUMP * FORGE_NETGAME_TELEPORT_JUMP)
		{
			for (index = forge_netgame.own_count; index < forge_netgame.count; index++)
			{
				struct scenario_netgame_flag const *flag = &forge_netgame.flags[index];

				if (flag->type == _netgame_flag_teleporter_target &&
					distance_squared3d(&position, &flag->position) < FORGE_NETGAME_TELEPORT_NEAR * FORGE_NETGAME_TELEPORT_NEAR)
				{
					forge_netgame.teleports++;
					platform_log("forge: player %d teleported on channel %d: %.2f %.2f %.2f to %.2f %.2f %.2f (%ld)", absolute,
						flag->team_index - FORGE_NETGAME_TELEPORTER_TEAM, forge_netgame.last_position[absolute].x,
						forge_netgame.last_position[absolute].y, forge_netgame.last_position[absolute].z, position.x, position.y,
						position.z, forge_netgame.teleports);
					break;
				}
			}
		}
		forge_netgame.last_unit[absolute] = player->unit_index;
		forge_netgame.last_position[absolute] = position;
	}
}

/* the layout's teleporters' hash: their kinds, channels, places and
facings, in order */
static unsigned long forge_netgame_hash(
	void)
{
	unsigned long hash = 2166136261UL;
	short index;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_MAXIMUM_SPAWNS; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];
		real values[5];
		unsigned char const *bytes = (unsigned char const *)values;
		unsigned int at;

		if (!forge_spawn_is_teleporter(spawn))
			continue;
		values[0] = (real)(spawn->kind * 100 + spawn->channel);
		values[1] = spawn->home.position.x;
		values[2] = spawn->home.position.y;
		values[3] = spawn->home.position.z;
		values[4] = arctangent(spawn->home.forward.j, spawn->home.forward.i);
		for (at = 0; at < sizeof(values); at++)
			hash = (hash ^ bytes[at]) * 16777619UL;
	}
	return hash;
}

/* (forge_update, each frame before the tick) the flags built again when the
layout's teleporters changed; the teleports watched */
static void forge_netgame_update(
	void)
{
	unsigned long hash = forge_netgame_hash();

	if (forge_netgame.dirty || hash != forge_netgame.built_hash)
	{
		forge_netgame.built_hash = hash;
		forge_netgame_flags_build();
	}
	forge_netgame_teleports_watch();
}

/* ---------- public code */

/* (game.c, a map's game starting, before the game type reads the flags) */
void forge_netgame_flags_prepare(
	void)
{
	short index;

	for (index = 0; index < FORGE_SPAWN_PLAYERS; index++)
		forge_netgame.last_unit[index] = NONE;
	forge_netgame.built_hash = forge_netgame_hash();
	forge_netgame_flags_build();
}
