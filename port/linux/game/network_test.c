/*
NETWORK_TEST.C

Automated system link sessions for testing the netcode without the menus
(debug.network_test in config.toml, HALO_NETWORK_TEST):

- "host:<map>[:<variant>[,<variant>...]]" hosts a game on that
  multiplayer map (bloodgulch, ...) with one of the built-in game variants
  (slayer by default; game_engine_get_variant_by_name), as the pregame
  screen's fast setup does, and starts it debug.network_test_start seconds
  later; with more variants, once a game is over (debug.network_test_score
  makes it short) the next, as the host's button on the scores does
  ("<variant>@<map>" plays that game on another map: a map change);
- "local:<map>[:<variant>...]" the same with a local (split screen) game
  of one player, which starts only where a local game may have one (the
  Vita's rules, HALO_PORT_VITA_NETWORK); with debug.test_controllers
  (HALO_TEST_CONTROLLERS=2 to 4) a player for each test controller, as
  pressing START on each does (split screen, the scripted player playing
  them all with debug.test_input);
- "join" searches for games and joins the first it finds, as picking it in
  the system link list does;
- "join-public" first browses internet play's public games (the server
  browser, p2p_lobby.c) and joins the first game listed there (not this
  machine's) by its listing, then searches as "join" does: the host's game
  shows in the list once the tunnel reaches it ("join-public:PASSWORD": a
  locked game, with that password; a game listed with a password is joined
  only so). "join-code:ABCD-EFGH" joins that code the same way;
  debug.network_test_rejoin has a joining machine leave the game that many
  seconds in and join again, once.

Once the game runs, every second each machine logs where every player's
unit is, so the machines' views of the game can be compared.

- "watch" hosts and joins nothing (the menus do, pressed by HALO_TEST_PAD:
  co-op from the campaign's menus), and reports the games played.

Co-op (network.coop_level, HALO_NET_COOP_LEVEL=a10): "host:<level>" hosts
a game the server makes co-op on that campaign level, as the Vita's
settings panel has it (network_server_manager.c), and starts it; once a
level is won and the scores shown, the host starts the next round (the
campaign's next level). Each second each machine also logs the co-op state:
its structure BSP, the cinematic and the skip vote, and whom a dead local
player watches. HALO_TEST_COMMANDS (main.c) presses skip ("@vote"), stands
players in loading zones ("@bsp") and kills them ("@kill").

Scripted play for the netcode's parts the bots' wandering does not reach:
debug.network_test_kill (the host kills the last player every so often),
debug.network_test_shoot (every so often each machine's player hits the
next with their weapon's projectile: a client's through its report to the
host) and debug.network_test_vehicle (the host seats the last player as a
vehicle's driver that many seconds in, takes them out 15 seconds on, and 5
seconds later stands them at a vehicle's driver's entrance, where a joining
machine's player holds the action button, as getting in does)
and debug.network_test_pickup (the last player stands on a weapon lying
about that many seconds in, and a joining machine's player holds the action
button a second later, to pick it up).

Called from the main loop every frame (main.c).
*/

#include "cseries.h"
#include "main/main.h"
#include "interface/player_ui.h"
#include "interface/ui_widget.h"
#include "networking/network_game_globals.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "items/projectile_definitions.h"
#include "items/items.h"
#include "objects/damage.h"
#include "scenario/scenario.h"
#include "tag_files/tag_files.h"
#include "camera/observer.h"
#include "cutscene/cinematics.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* internet play's codes and lobby (port/linux/src/p2p.c) */
#include "../src/p2p.h"

/* the platform layer's (port/linux/src/port_config.c) */
const char *config_string(char const *name);
double config_real(char const *name);
long config_integer(char const *name);
void platform_log(char const *format, ...);
/* damage.c's */
void damage_kill_object_for_player(long object_index, long player_index);
/* network_distributed.c's */
void network_distributed_statistics(long *sent, long *received, long *corrections);
void network_distributed_byte_statistics(long *sent_bytes, long *received_bytes);
void network_distributed_item_statistics(long *creates, long *deletes, long *failures, long *removed);
void network_damage_statistics(long *sent_reports, long *dealt_reports, long *rejected_reports, long *replayed_events);
/* xinput_sdl.c's */
void test_input_hold_action(int hold);
/* network co-op's (port/linux/game/network_coop.c, coop_spectate.c) */
boolean network_coop_active(void);
boolean network_coop_skip_vote_status(short *votes, short *voters, boolean *voted);
void network_coop_client_pace_statistics(long *holds, long *most_lead);
long coop_spectate_unit(short local_player_index);
/* network_server_manager.c's */
word network_game_server_get_state(struct network_game_server *server, short *substate);

enum
{
	_network_test_off,
	_network_test_host,
	_network_test_join,
	/* "watch": no game hosted or joined (the menus do it: HALO_TEST_PAD),
	only the reports of a game played */
	_network_test_watch,
};

static struct
{
	boolean checked;
	short mode;
	/* "local:": a local (split screen) game, not a system link one */
	boolean local;
	char map_name[64];
	char variant_name[64];
	/* ... the variant of this game, of variant_name's list; and the seconds
	the last game's scores have been shown */
	short variant_index;
	real postgame_seconds;
	boolean game_over;
	real start_delay;
	real menu_seconds;
	boolean set_up;
	real setup_seconds;
	boolean started;
	boolean joined;
	boolean map_set;
	/* the map set (its level name), set again if the lobby's map choice
	changes it before the game starts (after a game, the scores' choice) */
	char map_path[128];
	real map_checked_seconds;
	boolean player_added;
	real joined_seconds;
	boolean team_set;
	real kill_interval;
	real shoot_interval;
	real vehicle_time;
	real pickup_time;
	char pickup_weapon[64];
	long score_to_win;
	long logged_time;
	/* join-public, join-code: the code to join (empty: browse for a public
	game, joined by its listing's id, with the password given) */
	boolean by_code;
	boolean code_joined;
	char code[P2P_CODE_SIZE];
	char lobby_id[P2P_LOBBY_ID_SIZE];
	char lobby_password[P2P_LOBBY_PASSWORD_SIZE];
	boolean lobby_join_asked, lobby_locked;
	real browse_seconds;
	/* debug.network_test_rejoin: a joining machine leaves the game that many
	seconds in, and joins again (once) */
	real rejoin_time;
	real ingame_seconds;
	boolean left;
	boolean rejoined;
	/* debug.network_test_retry: the joins left to try after one that ended
	before its game began, and the seconds since it ended */
	long retries;
	real retry_seconds;
} network_test;

/* the variant at the index of the list (copied to name), FALSE past its end */
static boolean network_test_variant(
	short index,
	char *name,
	size_t size)
{
	char const *variant = network_test.variant_name;

	for (; index > 0 && variant; index--)
	{
		variant = strchr(variant, ',');
		if (variant)
			variant++;
	}
	if (!variant || !*variant)
		return FALSE;
	if (name)
	{
		/* (up to an "@map", the game's map if not the first's) */
		size_t length = strcspn(variant, ",@");

		snprintf(name, size, "%.*s", (int)length, variant);
	}
	return TRUE;
}

/* the map of the game at the index of the variant list: "<variant>@<map>"
names one, else the first game's */
static void network_test_variant_map(
	short index,
	char *map_name,
	size_t size)
{
	char const *variant = network_test.variant_name;
	char const *at;
	size_t length;

	snprintf(map_name, size, "%s", network_test.map_name);
	for (; index > 0 && variant; index--)
	{
		variant = strchr(variant, ',');
		if (variant)
			variant++;
	}
	if (!variant)
		return;
	length = strcspn(variant, ",");
	at = memchr(variant, '@', length);
	if (at && at + 1 < variant + length)
		snprintf(map_name, size, "%.*s", (int)(variant + length - at - 1), at + 1);
}

static void network_test_read_settings(
	void)
{
	char const *setting = config_string("debug.network_test");

	network_test.checked = TRUE;
	if ((!strncmp(setting, "host:", 5) || !strncmp(setting, "local:", 6)) && setting[5] && setting[6])
	{
		char *colon;

		network_test.mode = _network_test_host;
		network_test.local = setting[0] == 'l';
		snprintf(network_test.map_name, sizeof(network_test.map_name), "%s", strchr(setting, ':') + 1);
		snprintf(network_test.variant_name, sizeof(network_test.variant_name), "slayer");
		colon = strchr(network_test.map_name, ':');
		if (colon)
		{
			*colon = 0;
			snprintf(network_test.variant_name, sizeof(network_test.variant_name), "%s", colon + 1);
		}
	}
	else if (!strcmp(setting, "join"))
	{
		network_test.mode = _network_test_join;
	}
	else if (!strcmp(setting, "watch"))
	{
		network_test.mode = _network_test_watch;
	}
	else if (!strncmp(setting, "join-public", 11) || !strncmp(setting, "join-code:", 10))
	{
		network_test.mode = _network_test_join;
		network_test.by_code = TRUE;
		if (setting[5] == 'c')
			snprintf(network_test.code, sizeof(network_test.code), "%s", setting + 10);
		else if (setting[11] == ':')
			snprintf(network_test.lobby_password, sizeof(network_test.lobby_password), "%s", setting + 12);
	}
	network_test.start_delay = (real)config_real("debug.network_test_start");
	network_test.kill_interval = (real)config_real("debug.network_test_kill");
	network_test.shoot_interval = (real)config_real("debug.network_test_shoot");
	network_test.vehicle_time = (real)config_real("debug.network_test_vehicle");
	network_test.pickup_time = (real)config_real("debug.network_test_pickup");
	snprintf(network_test.pickup_weapon, sizeof(network_test.pickup_weapon), "%s",
		config_string("debug.network_test_pickup_weapon"));
	network_test.score_to_win = (long)config_integer("debug.network_test_score");
	network_test.rejoin_time = (real)config_real("debug.network_test_rejoin");
	network_test.retries = (long)config_integer("debug.network_test_retry");
	if (network_test.mode != _network_test_off)
		platform_log("network test: %s", setting);
}

/* appends to a line, cut short when it is full */
static void network_test_append(
	char *line,
	int size,
	int *length,
	char const *format,
	...)
{
	va_list arguments;
	int written;

	if (*length >= size - 1)
		return;
	va_start(arguments, format);
	written = vsnprintf(line + *length, (size_t)(size - *length), format, arguments);
	va_end(arguments);
	if (written > 0)
		*length = MIN(*length + written, size - 1);
}

/* every player's unit, as this machine sees it */
static void network_test_log_players(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	char line[4096];
	int length = 0;

	line[0] = 0;
	/* (each player's name, once a game: for tests that name a player, such
	as the host's ban command) */
	{
		static wchar_t named[HALO_PORT_MAXIMUM_NETWORK_PLAYERS][12];

		if (game_time_get() < 2 * TICKS_PER_SECOND)
			csmemset(named, 0, sizeof(named));
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
			char name[13];
			short index;

			if (absolute_index >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS ||
				!csmemcmp(named[absolute_index], player->name, sizeof(named[absolute_index])))
			{
				continue;
			}
			csmemcpy(named[absolute_index], player->name, sizeof(named[absolute_index]));
			for (index = 0; index < 12 && player->name[index]; index++)
				name[index] = player->name[index] >= 32 && player->name[index] < 127 ? (char)player->name[index] : '?';
			name[index] = 0;
			platform_log("network test: player %ld is named %s", absolute_index, name);
		}
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->unit_index != NONE)
		{
			struct object_datum *object = object_get(player->unit_index);
			struct unit_datum *unit = unit_get(player->unit_index);
			short slot;

			/* (riding: where its vehicle is) */
			struct object_datum *placed = object->object.parent_object_index != NONE ?
				object_get(object->object.parent_object_index) : object;

			network_test_append(line, (int)sizeof(line), &length, " player %ld: (%.3f %.3f %.3f) h%.2f/%.2f%s%s g%d/%d w",
				(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index), placed->object.position.x,
				placed->object.position.y, placed->object.position.z, object->object.body_vitality,
				object->object.shield_vitality, placed != object ? " riding" : "",
				TEST_FLAG(unit->unit.flags, _unit_active_camouflaged_bit) ? " camo" : "",
				unit->unit.grenade_counts[0], unit->unit.grenade_counts[1]);
			/* where it aims (yaw and pitch, degrees), its animation state and
			how hard it is moving */
			network_test_append(line, (int)sizeof(line), &length, " a%.0f/%.0f f%.0f l%.0f/%.0f as%d/%d st%d thr%.2f",
				atan2(unit->unit.aiming_vector.j, unit->unit.aiming_vector.i) * 57.29578,
				asin(PIN(unit->unit.aiming_vector.k, -1.0f, 1.0f)) * 57.29578,
				atan2(object->object.forward.j, object->object.forward.i) * 57.29578,
				atan2(unit->unit.looking_vector.j, unit->unit.looking_vector.i) * 57.29578,
				asin(PIN(unit->unit.looking_vector.k, -1.0f, 1.0f)) * 57.29578,
				(int)unit->unit.animation.aiming_screen_index, (int)unit->unit.animation.looking_screen_index,
				(int)unit->unit.animation.state,
				sqrt(unit->unit.throttle.i * unit->unit.throttle.i + unit->unit.throttle.j * unit->unit.throttle.j));
			for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
			{
				long weapon_index = unit->unit.weapon_object_indices[slot];

				if (weapon_index != NONE)
				{
					struct weapon_datum *weapon = weapon_get(weapon_index);

					network_test_append(line, (int)sizeof(line), &length, " %lx:%d",
						(unsigned long)weapon->definition_index & 0xFFFF, weapon->weapon.magazines[0].rounds_total +
						weapon->weapon.magazines[0].rounds_loaded);
				}
			}
		}
		else
		{
			network_test_append(line, (int)sizeof(line), &length, " player %ld: dead",
				(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index));
		}
		/* the game type's score and the kills and deaths */
		network_test_append(line, (int)sizeof(line), &length, " s%ld k%d d%d f%d t%ld m%d",
			game_engine && game_engine->get_player_score ?
				game_engine->get_player_score(iterator.datum_index, _get_score_individual) : -1L,
			player->statistics.kills[0], player->statistics.deaths, player->statistics.friendly_fire_kills, (long)player->team_index,
			(int)player->network_player_data.machine_index);
	}
	{
		long sent, received, corrections;

		struct object_iterator objects;
		long ground_items = 0;

		object_iterator_new(&objects, _object_mask_weapon | _object_mask_equipment, 0);
		while (object_iterator_next(&objects))
		{
			struct item_datum *item = item_get(objects.index);

			if (item->object.parent_object_index == NONE &&
				TEST_FLAG(item->object.flags, _object_connected_to_map_bit) &&
				!TEST_FLAG(item->item.flags, _item_attached_to_unit_bit))
			{
				ground_items++;
			}
		}
		network_distributed_statistics(&sent, &received, &corrections);
		long creates, deletes, failures, removed;

		long sent_reports, dealt_reports, rejected_reports, replayed_events;

		network_distributed_item_statistics(&creates, &deletes, &failures, &removed);
		network_damage_statistics(&sent_reports, &dealt_reports, &rejected_reports, &replayed_events);
		/* this machine's player and where its camera is (a player that never
		spawns leaves it where it began) */
		long local_player_index = local_player_get_player_index(0);
		struct observer_result const *camera = observer_get_camera(0);

		platform_log("network test: tick %ld%s | items %ld (+%ld -%ld !%ld x%ld) | %s to %ld | sent %ld received %ld corrected %ld"
			" | hits %ld dealt %ld rejected %ld replayed %ld | local %ld camera (%.1f %.1f %.1f) respawn %ld",
			game_time_get(), line, ground_items, creates, deletes, failures, removed,
			game_engine_can_score() ? "playing" : "game over",
			game_engine_running() ? (long)game_engine_get_variant()->universal_variant.score_to_win : 0L,
			sent, received, corrections,
			sent_reports, dealt_reports, rejected_reports, replayed_events,
			local_player_index == NONE ? -1L : (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(local_player_index),
			camera ? camera->position.x : 0.0f, camera ? camera->position.y : 0.0f, camera ? camera->position.z : 0.0f,
			local_player_index == NONE ? 0L : (long)player_get(local_player_index)->respawn_timer);
		{
			long sent_bytes, received_bytes;

			network_distributed_byte_statistics(&sent_bytes, &received_bytes);
			platform_log("network test: bytes at tick %ld: sent %ld received %ld", game_time_get(), sent_bytes, received_bytes);
		}
	}
	/* co-op: the structure BSP, the cinematic, the skip vote, whom a dead
	local player watches */
	if (network_coop_active())
	{
		short votes = 0, voters = 0;
		boolean voted = FALSE;
		boolean offered = network_coop_skip_vote_status(&votes, &voters, &voted);
		long watched = coop_spectate_unit(0);
		long holds, most_lead;

		network_coop_client_pace_statistics(&holds, &most_lead);
		platform_log("network test: co-op tick %ld | bsp %d | cinematic %d skip %s %d/%d%s | watching %lx | held %ld lead %ld",
			game_time_get(), (int)global_structure_bsp_index_get(), (int)cinematic_in_progress(),
			offered ? "offered" : "no", (int)votes, (int)voters, voted ? " voted" : "", (unsigned long)watched,
			holds, most_lead);
	}
}

/* each of this machine's players hits the next player with their weapon's
projectile, as its impact would, when it is within the projectile's reach
(as a real shot is: the host checks it) */
static void network_test_shoot(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct data_iterator targets;
		struct player_datum *target = NULL;
		struct player_datum *candidate;
		struct unit_datum *unit;
		long weapon_index;
		struct weapon_definition *weapon;
		struct weapon_trigger_definition *trigger;
		long damage_index = NONE;
		real reach = 0.0f;
		boolean melee;
		struct damage_data damage;
		struct object_datum *target_object;
		real_vector3d direction;

		if (player->local_player_index == NONE || player->unit_index == NONE)
			continue;
		data_iterator_new(&targets, player_data);
		while ((candidate = (struct player_datum *)data_iterator_next(&targets)) != NULL)
		{
			if (candidate != player && candidate->unit_index != NONE)
			{
				target = candidate;
				break;
			}
		}
		unit = unit_get(player->unit_index);
		if (!target || unit->unit.current_weapon_index == NONE ||
			TEST_FLAG(object_get(target->unit_index)->object.damage_flags, _object_dead_bit))
		{
			continue;
		}
		weapon_index = unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
		if (weapon_index == NONE)
			continue;
		weapon = weapon_definition_get(object_get(weapon_index)->definition_index);
		if (weapon->weapon.triggers.count > 0)
		{
			trigger = TAG_BLOCK_GET_ELEMENT(&weapon->weapon.triggers, 0, struct weapon_trigger_definition);
			if (trigger->projectile.index != NONE)
			{
				struct projectile_definition *projectile = projectile_definition_get(trigger->projectile.index);

				damage_index = projectile->projectile.impact_damage.index;
				/* (how far it flies: its range, or at its speed for as long as
				its timer runs; none for no bound, network_damage.c) */
				if (projectile->projectile.maximum_range > 0.0f)
					reach = projectile->projectile.maximum_range;
				else if (projectile->projectile.detonation_timer_starts == 0)
				{
					reach = projectile->projectile.timer_upper_bound * TICKS_PER_SECOND *
						MAX(projectile->projectile.initial_velocity, projectile->projectile.final_velocity);
				}
			}
		}
		/* (else its melee: a blow's epicenter is its striker's, as
		unit_cause_player_melee_damage has it) */
		melee = damage_index == NONE;
		if (melee)
			damage_index = weapon->weapon.melee_attack_damage.index;
		if (damage_index == NONE)
			continue;
		target_object = object_get(target->unit_index);
		/* (a blow reaches only a target at hand, as the host checks) */
		if (melee)
		{
			real dx = target_object->object.position.x - unit->object.position.x;
			real dy = target_object->object.position.y - unit->object.position.y;
			real dz = target_object->object.position.z - unit->object.position.z;

			if (dx * dx + dy * dy + dz * dz > 1.5f * 1.5f)
				continue;
		}
		/* (a shot reaches no further than its projectile flies, with a margin
		for how far the target is from where the shot started) */
		else if (reach > 0.0f)
		{
			real dx = target_object->object.position.x - unit->object.position.x;
			real dy = target_object->object.position.y - unit->object.position.y;
			real dz = target_object->object.position.z - unit->object.position.z;

			if (dx * dx + dy * dy + dz * dz > 0.9f * reach * 0.9f * reach)
				continue;
		}
		damage_data_new(&damage, damage_index);
		damage.owner_player_index = iterator.datum_index;
		damage.owner_object_index = player->unit_index;
		damage.owner_team_index = unit->object.owner_team_index;
		damage.origin = melee ? unit->object.bounding_sphere_center : target_object->object.position;
		damage.epicenter = melee ? unit->object.bounding_sphere_center : target_object->object.position;
		direction.i = target_object->object.position.x - unit->object.position.x;
		direction.j = target_object->object.position.y - unit->object.position.y;
		direction.k = target_object->object.position.z - unit->object.position.z;
		normalize3d(&direction);
		damage.direction = direction;
		damage.scale = 1.0f;
		scenario_location_from_point(&damage.location, &damage.epicenter);
		object_cause_damage(&damage, target->unit_index, NONE, NONE, NONE, NULL);
		platform_log("network test: player %ld shoots player %ld",
			(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index), (long)(target - (struct player_datum *)player_data->data));
	}
}

/* the host seats the last player as the nearest vehicle's driver, or out */
/* the host brings the players far from the first (on foot) near it, so
that they are within their weapons' reach (network_test_shoot shoots only
so near, as a player does); the host moving a client's player, as a
teleporter does */
static void network_test_gather(
	boolean leave_last)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *first = NULL;
	struct player_datum *last = NULL;
	short count = 0;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (!first)
			first = player;
		last = player;
	}
	if (!first || first->unit_index == NONE)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum const *center = object_get(first->unit_index);
		struct object_datum *unit;
		real_point3d position;
		real dx, dy, dz;

		if (player == first || player->unit_index == NONE || (leave_last && player == last))
			continue;
		count++;
		unit = object_get(player->unit_index);
		if (unit->object.parent_object_index != NONE || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			continue;
		dx = unit->object.position.x - center->object.position.x;
		dy = unit->object.position.y - center->object.position.y;
		dz = unit->object.position.z - center->object.position.z;
		if (dx * dx + dy * dy + dz * dz <= 20.0f * 20.0f)
			continue;
		/* (beside it, where the map is open: at its feet and its head) */
		{
			static real const offsets[][2] = { { 1.5f, 0.0f }, { -1.5f, 0.0f }, { 0.0f, 1.5f }, { 0.0f, -1.5f },
				{ 1.5f, 1.5f }, { -1.5f, -1.5f }, { 1.5f, -1.5f }, { -1.5f, 1.5f } };
			short try_index;

			for (try_index = 0; try_index < (short)NUMBEROF(offsets); try_index++)
			{
				short index = (short)((count + try_index) % (short)NUMBEROF(offsets));
				struct location feet, head;
				real_point3d above;

				position = center->object.position;
				position.x += offsets[index][0];
				position.y += offsets[index][1];
				position.z += 0.2f;
				above = position;
				above.z += 0.7f;
				scenario_location_from_point(&feet, &position);
				scenario_location_from_point(&head, &above);
				if (feet.cluster_index != NONE && head.cluster_index != NONE)
					break;
			}
			if (try_index >= (short)NUMBEROF(offsets))
				continue;
		}
		object_set_position(player->unit_index, &position, NULL, NULL);
		platform_log("network test: the host brings player %ld near the first",
			(long)(player - (struct player_datum *)player_data->data));
	}
}

static void network_test_vehicle(
	boolean enter)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *last = NULL;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		last = player;
	if (!last || last->unit_index == NONE)
		return;
	if (!enter)
	{
		if (object_get(last->unit_index)->object.parent_object_index != NONE)
		{
			unit_exit_seat_end(last->unit_index);
			platform_log("network test: the last player leaves the vehicle");
		}
		return;
	}
	{
		struct object_iterator vehicles;
		long nearest_index = NONE;
		real nearest_distance = 0.0f;
		real_point3d const *position = &object_get(last->unit_index)->object.position;
		short seat_index;

		object_iterator_new(&vehicles, _object_mask_vehicle, 0);
		while (object_iterator_next(&vehicles))
		{
			real_point3d const *vehicle_position = &object_get(vehicles.index)->object.position;
			real distance = distance_squared3d(position, vehicle_position);

			if (nearest_index == NONE || distance < nearest_distance)
			{
				nearest_index = vehicles.index;
				nearest_distance = distance;
			}
		}
		if (nearest_index == NONE)
		{
			platform_log("network test: no vehicle");
			return;
		}
		for (seat_index = 0; seat_index < unit_definition_get(object_get(nearest_index)->definition_index)->unit.seats.count; seat_index++)
		{
			if (unit_seat_is_driver(nearest_index, seat_index) &&
				unit_enter_seat(last->unit_index, nearest_index, seat_index))
			{
				platform_log("network test: the last player drives vehicle %lx", nearest_index);
				return;
			}
		}
		platform_log("network test: the last player cannot drive vehicle %lx", nearest_index);
	}
}

/* both machines stand the last player at the driver's entrance of the
nearest vehicle no one rides (a joining machine's player then holds the
action button, as a player getting in does) */
static void network_test_vehicle_approach(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *last = NULL;
	struct object_iterator vehicles;
	long nearest_index = NONE;
	real nearest_distance = 0.0f;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		last = player;
	if (!last || last->unit_index == NONE || object_get(last->unit_index)->object.parent_object_index != NONE)
		return;
	object_iterator_new(&vehicles, _object_mask_vehicle, 0);
	while (object_iterator_next(&vehicles))
	{
		struct unit_datum *vehicle = unit_get(vehicles.index);
		/* (the same vehicle on every machine: the lowest index) */
		real distance = (real)DATUM_INDEX_TO_ABSOLUTE_INDEX(vehicles.index);

		if (vehicle->unit.driver_object_index != NONE || TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit))
			continue;
		if (nearest_index == NONE || distance < nearest_distance)
		{
			nearest_index = vehicles.index;
			nearest_distance = distance;
		}
	}
	if (nearest_index != NONE)
	{
		short seat_index;

		for (seat_index = 0; seat_index < unit_definition_get(object_get(nearest_index)->definition_index)->unit.seats.count;
			seat_index++)
		{
			real_point3d entrance;
			real_point3d seat;

			if (unit_seat_is_driver(nearest_index, seat_index) &&
				unit_get_seat_entrance_point(last->unit_index, nearest_index, seat_index, &entrance, &seat, NULL))
			{
				object_set_position(last->unit_index, &entrance, NULL, NULL);
				platform_log("network test: the last player stands at vehicle %lx's driver's entrance (%.2f %.2f %.2f)",
					nearest_index, entrance.x, entrance.y, entrance.z);
				return;
			}
		}
	}
	platform_log("network test: no vehicle to get into");
}

/* the host gives the last player a second weapon, lying about */
static void network_test_second_weapon(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *last = NULL;
	struct object_iterator weapons;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		last = player;
	if (!last || last->unit_index == NONE)
		return;
	object_iterator_new(&weapons, _object_mask_weapon, 0);
	while (object_iterator_next(&weapons))
	{
		struct object_datum *weapon = object_get(weapons.index);
		struct unit_datum *unit = unit_get(last->unit_index);
		long current_index = unit->unit.weapon_object_indices[0];

		if (weapon->object.parent_object_index == NONE && TEST_FLAG(weapon->object.flags, _object_connected_to_map_bit) &&
			(current_index == NONE || object_get(current_index)->definition_index != weapon->definition_index) &&
			unit_add_weapon_to_inventory(last->unit_index, weapons.index, TRUE))
		{
			platform_log("network test: the last player takes a second weapon (%lx)", weapon->definition_index);
			/* (and camouflage, as a powerup gives) */
			player_handle_powerup(DATUM_INDEX_NEW(last - (struct player_datum *)player_data->data, last->identifier),
				_player_powerup_active_camouflage, 10 * TICKS_PER_SECOND);
			return;
		}
	}
}

/* both machines stand the last player on the first weapon lying about that
it does not carry (the same object on both: a client's own player is where
it has it, within a tolerance) */
static void network_test_pickup(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *last = NULL;
	struct object_iterator weapons;
	long nearest_index = NONE;
	real nearest_distance = 0.0f;
	struct unit_datum *unit;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		last = player;
	if (!last || last->unit_index == NONE)
		return;
	unit = unit_get(last->unit_index);
	object_iterator_new(&weapons, _object_mask_weapon, 0);
	while (object_iterator_next(&weapons))
	{
		struct object_datum *weapon = object_get(weapons.index);
		short slot;
		boolean carried = FALSE;
		real distance;

		if (weapon->object.parent_object_index != NONE ||
			!TEST_FLAG(weapon->object.flags, _object_connected_to_map_bit))
		{
			continue;
		}
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long carried_index = unit->unit.weapon_object_indices[slot];

			carried |= carried_index != NONE && object_get(carried_index)->definition_index == weapon->definition_index;
		}
		if (network_test.pickup_weapon[0] && !strstr(tag_get_name(weapon->definition_index), network_test.pickup_weapon))
			continue;
		distance = (real)DATUM_INDEX_TO_ABSOLUTE_INDEX(weapons.index);
		if (!carried && (nearest_index == NONE || distance < nearest_distance))
		{
			nearest_index = weapons.index;
			nearest_distance = distance;
		}
	}
	if (nearest_index != NONE)
	{
		real_point3d position = object_get(nearest_index)->object.position;

		position.z += 0.1f;
		object_set_position(last->unit_index, &position, NULL, NULL);
		platform_log("network test: the last player stands on weapon %lx (%lx)", nearest_index,
			object_get(nearest_index)->definition_index);
	}
}

void network_test_update(
	boolean main_menu_loaded,
	real seconds)
{
	if (!network_test.checked)
		network_test_read_settings();
	if (network_test.mode == _network_test_off)
		return;

	/* the game running: report (from the start of each game: the next
	game's time starts over) */
	if (game_in_progress() && game_time_get() < network_test.logged_time)
		network_test.logged_time = 0;
	if (game_in_progress() && !main_menu_loaded && game_time_get() - network_test.logged_time >= TICKS_PER_SECOND)
	{
		network_test.logged_time = game_time_get();
		network_test_log_players();
		if (network_test.shoot_interval > 0.0f &&
			game_time_get() % MAX(1, (long)(network_test.shoot_interval * TICKS_PER_SECOND)) < TICKS_PER_SECOND)
		{
			network_test_shoot();
		}
		/* (the host brings the players near a second before they shoot;
		not the last while the vehicle test has it) */
		if (network_test.mode == _network_test_host && network_test.shoot_interval > 0.0f &&
			(game_time_get() + TICKS_PER_SECOND) % MAX(1, (long)(network_test.shoot_interval * TICKS_PER_SECOND)) <
				TICKS_PER_SECOND)
		{
			long vehicle_time = (long)(network_test.vehicle_time * TICKS_PER_SECOND);

			network_test_gather(network_test.vehicle_time > 0.0f && game_time_get() >= vehicle_time - 2 * TICKS_PER_SECOND);
		}
		if (network_test.pickup_time > 0.0f)
		{
			long pickup_time = (long)(network_test.pickup_time * TICKS_PER_SECOND);

			/* (two weapons first: picking up a third swaps) */
			if (network_test.mode == _network_test_host && game_time_get() >= pickup_time - 3 * TICKS_PER_SECOND &&
				game_time_get() - (pickup_time - 3 * TICKS_PER_SECOND) < TICKS_PER_SECOND)
			{
				network_test_second_weapon();
			}

			if (game_time_get() >= pickup_time && game_time_get() - pickup_time < TICKS_PER_SECOND)
			{
				network_test_pickup();
			}
			/* (standing there for four seconds, the button held from a second
			on) */
			if (network_test.mode == _network_test_join)
			{
				boolean hold = game_time_get() >= pickup_time &&
					game_time_get() < pickup_time + 4 * TICKS_PER_SECOND;

				test_input_hold_action(hold);
			}
		}
		if (network_test.mode == _network_test_host && network_test.vehicle_time > 0.0f)
		{
			long enter_time = (long)(network_test.vehicle_time * TICKS_PER_SECOND);

			if (game_time_get() >= enter_time && game_time_get() - enter_time < TICKS_PER_SECOND)
				network_test_vehicle(TRUE);
			if (game_time_get() >= enter_time + 15 * TICKS_PER_SECOND &&
				game_time_get() - enter_time - 15 * TICKS_PER_SECOND < TICKS_PER_SECOND)
			{
				network_test_vehicle(FALSE);
			}
		}
		/* ... and 20 seconds on, the last player getting in as a player does:
		at the entrance, the button held for three seconds */
		if (network_test.vehicle_time > 0.0f)
		{
			long approach_time = (long)(network_test.vehicle_time * TICKS_PER_SECOND) + 20 * TICKS_PER_SECOND;

			if (game_time_get() >= approach_time && game_time_get() - approach_time < TICKS_PER_SECOND)
				network_test_vehicle_approach();
			if (network_test.mode == _network_test_join && network_test.pickup_time <= 0.0f)
			{
				test_input_hold_action(game_time_get() >= approach_time + TICKS_PER_SECOND &&
					game_time_get() < approach_time + 4 * TICKS_PER_SECOND);
			}
		}
		/* debug.network_test_kill: the host kills the last player every so
		often, to test deaths and respawns reaching the clients */
		if (network_test.mode == _network_test_host && network_test.kill_interval > 0.0f &&
			game_time_get() % MAX(1, (long)(network_test.kill_interval * TICKS_PER_SECOND)) < TICKS_PER_SECOND)
		{
			struct data_iterator iterator;
			struct player_datum *player;
			struct player_datum *last = NULL;

			struct player_datum *first = NULL;
			long first_index = NONE;

			data_iterator_new(&iterator, player_data);
			while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
			{
				if (!first)
				{
					first = player;
					first_index = iterator.datum_index;
				}
				last = player;
			}
			if (last && last != first && last->unit_index != NONE && first->unit_index != NONE)
			{
				/* killed by the first player: a kill that scores */
				platform_log("network test: the first player kills the last");
				damage_kill_object_for_player(last->unit_index, first_index);
				/* and picks up a weapon lying about, and two grenades of each kind */
				{
					struct object_iterator objects;
					struct unit_datum *unit = unit_get(first->unit_index);
					long held_index = unit->unit.weapon_object_indices[0];
					long held_definition_index = held_index != NONE ? weapon_get(held_index)->definition_index : NONE;

					object_iterator_new(&objects, _object_mask_weapon, 0);
					while (object_iterator_next(&objects))
					{
						struct weapon_datum *weapon = weapon_get(objects.index);

						if (weapon->object.parent_object_index == NONE &&
							weapon->definition_index != held_definition_index)
						{
							if (unit_add_weapon_to_inventory(first->unit_index, objects.index, TRUE))
								platform_log("network test: the first player picks up a weapon");
							break;
						}
					}
					unit->unit.grenade_counts[0] = 2;
					unit->unit.grenade_counts[1] = 2;
				}
			}
		}
	}

	/* the next game of the list: once the scores have been shown a while,
	the host's button (the bots may press it first) */
	if (network_test.mode == _network_test_host && network_test.started && game_engine_running() &&
		!main_menu_loaded && !game_engine_can_score())
	{
		network_test.game_over = TRUE;
		if (game_engine_showing_postgame() && global_network_game_server_get())
		{
			network_test.postgame_seconds += seconds;
			if (network_test.postgame_seconds >= 3.0f && network_test_variant(network_test.variant_index + 1, NULL, 0))
			{
				network_test.postgame_seconds = 0.0f;
				network_game_server_reset_to_pregame(global_network_game_server_get());
			}
		}
	}
	/* co-op: a level won ends the round (the server's postgame, with no
	game engine); the scores shown a while, the host goes back to the lobby
	and starts the next level */
	if (network_test.mode == _network_test_host && network_test.started && !main_menu_loaded &&
		global_network_game_server_get() && config_string("network.coop_level")[0] &&
		network_game_server_get_state(global_network_game_server_get(), NULL) == 2)
	{
		network_test.postgame_seconds += seconds;
		if (!network_test.game_over)
			platform_log("network test: co-op level won: %s", global_scenario_get() ? tag_get_name(global_scenario_index) : "?");
		network_test.game_over = TRUE;
		if (network_test.postgame_seconds >= 3.0f)
		{
			network_test.postgame_seconds = -1000.0f;
			network_game_server_reset_to_pregame(global_network_game_server_get());
		}
	}
	/* (a joining machine's player: the other team again in the next game's
	lobby, whose variant may have teams where the last had none) */
	if (network_test.mode == _network_test_join && network_test.team_set && game_engine_running() &&
		!main_menu_loaded && !game_engine_can_score())
	{
		network_test.game_over = TRUE;
	}
	/* debug.network_test_rejoin: leave the game, as quitting from the pause
	menu does, then join again from the main menu */
	if (network_test.mode == _network_test_join && network_test.rejoin_time > 0.0f && !network_test.left &&
		game_in_progress() && game_engine_running() && !main_menu_loaded)
	{
		network_test.ingame_seconds += seconds;
		if (network_test.ingame_seconds >= network_test.rejoin_time)
		{
			network_test.left = TRUE;
			platform_log("network test: leaving the game");
			main_goto_main_menu();
		}
	}
	if (network_test.mode == _network_test_join && network_test.left && !network_test.rejoined && main_menu_loaded)
	{
		network_test.rejoined = TRUE;
		network_test.set_up = FALSE;
		network_test.joined = FALSE;
		network_test.player_added = FALSE;
		network_test.team_set = FALSE;
		network_test.code_joined = FALSE;
		network_test.lobby_join_asked = FALSE;
		network_test.lobby_id[0] = 0;
		network_test.joined_seconds = 0.0f;
		network_test.menu_seconds = 0.0f;
		network_test.game_over = FALSE;
		platform_log("network test: joining again");
	}
	/* debug.network_test_retry: a join that ended before its game (the
	client gone: a map download cut off, the link lost) is tried again a few
	seconds on */
	if (network_test.mode == _network_test_join && network_test.retries > 0 && network_test.set_up &&
		main_menu_loaded && !global_network_game_client_get())
	{
		network_test.retry_seconds += seconds;
		if (network_test.retry_seconds >= 5.0f)
		{
			network_test.retries--;
			network_test.retry_seconds = 0.0f;
			network_test.set_up = FALSE;
			network_test.joined = FALSE;
			network_test.player_added = FALSE;
			network_test.team_set = FALSE;
			network_test.code_joined = FALSE;
			network_test.joined_seconds = 0.0f;
			network_test.menu_seconds = 0.0f;
			platform_log("network test: joining again (the last join ended before its game)");
		}
	}
	if (network_test.mode == _network_test_join && network_test.game_over && main_menu_loaded)
	{
		network_test.game_over = FALSE;
		network_test.team_set = FALSE;
		network_test.joined_seconds = 0.0f;
	}
	/* ... back in the lobby, set up as the first was */
	if (network_test.mode == _network_test_host && network_test.game_over && main_menu_loaded)
	{
		network_test.game_over = FALSE;
		network_test.postgame_seconds = 0.0f;
		if (config_string("network.coop_level")[0])
		{
			/* (co-op: the next level, the server's next round) */
			network_test.started = FALSE;
			network_test.setup_seconds = 0.0f;
			network_test.menu_seconds = 0.0f;
			platform_log("network test: the next co-op level");
		}
		else if (network_test_variant(network_test.variant_index + 1, NULL, 0))
		{
			network_test.variant_index++;
			network_test.started = FALSE;
			network_test.map_set = FALSE;
			network_test.map_path[0] = 0;
			network_test.map_checked_seconds = 0.0f;
			network_test.setup_seconds = 0.0f;
			network_test.menu_seconds = 0.0f;
			/* (as picking the next game's map does: the scores' map choice
			holds the countdown) */
			if (global_network_game_server_get())
				network_game_server_pause_countdown(global_network_game_server_get(), FALSE);
			platform_log("network test: the next game");
		}
	}

	if (!main_menu_loaded)
		return;
	network_test.menu_seconds += seconds;
	/* (the main menu settling first) */
	if (network_test.menu_seconds < 2.0f)
		return;

	switch (network_test.mode)
	{
	case _network_test_host:
		if (!network_test.set_up)
		{
			network_test.set_up = TRUE;
			main_set_multiplayer_map_name(network_test.map_name);
			if (network_test.local)
			{
				/* as Multiplayer, Split Screen does (split_screen_game_initialize) */
				ui_widgets_close_all();
				dispose_global_network_game_server();
				dispose_global_network_game_client();
				network_game_accept_remote_connections(FALSE);
				game_engine_playlist_initialize();
				if (create_global_network_game_server())
				{
					game_engine_playlist_begin();
					game_connection_set(_game_connection_network_server);
				}
				if (!global_network_game_client_get())
					create_global_network_game_client();
				ui_widget_load_by_name_or_tag(
					"ui\\shell\\main_menu\\multiplayer_type_select\\split_screen\\pregame\\splitscreen_pregame_wrapper_normal",
					NONE, NULL, NONE, NONE, NONE, NONE);
				platform_log("network test: a local game on %s", network_test.map_name);
			}
			else
			{
				player_ui_fast_setup_network_server();
				platform_log("network test: hosting %s", network_test.map_name);
			}
		}
		else if (!network_test.started)
		{
			network_test.setup_seconds += seconds;
			/* the map (fast setup clears it), and a player for controller 1, as
			pressing A in the lobby adds one */
			/* (co-op: the server sets the level and its gametype itself,
			network.coop_level) */
			if (!network_test.map_set && config_string("network.coop_level")[0])
			{
				network_test.map_set = TRUE;
				platform_log("network test: co-op on %s (the server's co-op setting)", config_string("network.coop_level"));
			}
			if (!network_test.map_set && network_test.setup_seconds >= 1.0f && global_network_game_server_get())
			{
				char path[128];
				char map_name[64];

				struct game_variant variant;

				network_test_variant_map(network_test.variant_index, map_name, sizeof(map_name));
				snprintf(path, sizeof(path), "levels\\test\\%s\\%s", map_name, map_name);
				network_game_server_change_map_name(global_network_game_server_get(), path);
				snprintf(network_test.map_path, sizeof(network_test.map_path), "%s", path);
				platform_log("network test: map %s", map_name);
				/* the variant, as picking the game settings does */
				{
					char variant_name[64];

					network_test_variant(network_test.variant_index, variant_name, sizeof(variant_name));
					variant = *game_engine_get_variant_by_name(&variant, variant_name);
					platform_log("network test: game %d, %s", network_test.variant_index + 1, variant_name);
				}
				/* debug.network_test_score: a short game, to test the next */
				if (network_test.score_to_win > 0)
					variant.universal_variant.score_to_win = network_test.score_to_win;
				player_ui_set_game_variant(&variant);
				network_game_server_change_game_variant(global_network_game_server_get(), &variant);
				network_test.map_set = TRUE;
			}
			else if (network_test.map_set && network_test.map_path[0] && global_network_game_server_get() &&
				network_test.setup_seconds - network_test.map_checked_seconds >= 1.0f &&
				(network_test.map_checked_seconds = network_test.setup_seconds,
					strcmp(main_get_multiplayer_map_name(), network_test.map_path)))
			{
				platform_log("network test: map %s again (the lobby changed it to %s)", network_test.map_path,
					main_get_multiplayer_map_name());
				network_game_server_change_map_name(global_network_game_server_get(), network_test.map_path);
			}
			if (!network_test.player_added && network_test.setup_seconds >= 2.0f && global_network_game_client_get())
			{
				/* (and the test controllers' players: split screen) */
				short controller;
				short controllers = (short)PIN(config_integer("debug.test_controllers"), 1, MAXIMUM_LOCAL_PLAYERS);

				network_test.player_added = TRUE;
				for (controller = 0; controller < controllers; controller++)
				{
					network_test.player_added &=
						network_game_client_add_player(global_network_game_client_get(), controller);
				}
				if (controllers > 1)
					platform_log("network test: %d players on this machine (split screen)", controllers);
			}
			if (network_test.setup_seconds >= network_test.start_delay)
			{
				network_test.started = TRUE;
				network_game_client_request_immediate_start();
				platform_log("network test: starting the game");
			}
		}
		break;
	case _network_test_join:
		if (!network_test.set_up)
		{
			network_test.set_up = TRUE;
			dispose_global_network_game_client();
			dispose_global_network_game_server();
			if (create_global_network_game_client())
			{
				game_connection_set(_game_connection_network_client);
				/* (the player joined to multiplayer first, as a player picking
				their profile: the pregame screen then asks for them every
				frame until they are in the settings) */
				player_ui_local_player_joined_multiplayer_game(0);
				platform_log("network test: searching for games");
			}
		}
		else if (network_test.by_code && !network_test.code_joined)
		{
			/* the server browser's first game (once a second), or the code given */
			network_test.browse_seconds += seconds;
			if (!network_test.code[0] && network_test.browse_seconds >= 1.0f)
			{
				struct p2p_lobby_entry entry;
				int index;

				network_test.browse_seconds = 0.0f;
				p2p_lobby_browse(TRUE);
				for (index = 0; !network_test.lobby_id[0] && p2p_lobby_entry(index, &entry); index++)
				{
					if (entry.compatible && !entry.own)
					{
						platform_log("network test: the public games list \"%s\"%s: %s; %s", entry.name,
							entry.locked ? " [pw]" : "", entry.rules, entry.players_line);
						snprintf(network_test.lobby_id, sizeof(network_test.lobby_id), "%s", entry.id);
						network_test.lobby_locked = entry.locked != 0;
						break;
					}
				}
				/* (joined by its listing, as the browser does: again if the
				password was wrong or the game went) */
				if (network_test.lobby_id[0] && !network_test.lobby_join_asked)
				{
					network_test.lobby_join_asked = p2p_lobby_join(network_test.lobby_id, network_test.lobby_password);
					platform_log("network test: joining the public game%s", network_test.lobby_locked ?
						" with its password" : "");
					if (!network_test.lobby_join_asked)
						network_test.lobby_id[0] = 0;
				}
				else if (network_test.lobby_id[0])
				{
					int state = p2p_lobby_join_state();

					platform_log("network test: the public game's join: %s", state == P2P_LOBBY_JOIN_JOINING ?
						"joining its invite" : state == P2P_LOBBY_JOIN_UNLOCKING ? "opening it with the password" :
						state == P2P_LOBBY_JOIN_WRONG_PASSWORD ? "wrong password" :
						state == P2P_LOBBY_JOIN_GONE ? "gone" : "idle");
					if (state == P2P_LOBBY_JOIN_JOINING)
					{
						network_test.code_joined = TRUE;
						p2p_lobby_browse(FALSE);
					}
					if (state != P2P_LOBBY_JOIN_UNLOCKING)
					{
						network_test.lobby_join_asked = FALSE;
						network_test.lobby_id[0] = 0;
					}
				}
			}
			if (network_test.code[0])
			{
				network_test.code_joined = TRUE;
				/* (a public game's as the settings panel joins it: its code, whose
				record must be the listed host's) */
				platform_log("network test: joining code %s: %s", network_test.code,
					p2p_join_code(network_test.code) ? "looking it up" : "not a code");
			}
		}
		else if (!network_test.joined && network_game_client_join_first_available_game())
		{
			network_test.joined = TRUE;
			ui_widgets_close_all();
			ui_widget_load_by_name_or_tag(
				"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen",
				NONE, NULL, NONE, NONE, NONE, NONE);
			platform_log("network test: joining");
		}
		else if (network_test.joined && !network_test.player_added)
		{
			network_test.joined_seconds += seconds;
			if (network_test.joined_seconds >= 3.0f && global_network_game_client_get())
				network_test.player_added = TRUE;
		}
		/* (the other team from the host's player: a team game needs both) */
		else if (network_test.player_added && !network_test.team_set)
		{
			network_test.joined_seconds += seconds;
			if (network_test.joined_seconds >= 5.0f)
				network_test.team_set = network_game_client_set_team(NONE);
		}
		break;
	}
}
