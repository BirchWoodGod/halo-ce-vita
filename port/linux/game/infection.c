/*
INFECTION.C

Infection (port): Team Slayer whose dead humans join the zombies. At the
round's start (the first player spawned) one player in every
players_per_first_zombie (8) is chosen at random to be a zombie, the rest
are humans; a human killed, by anyone or anything, comes back a zombie; a
player joining the game in progress is a zombie. The zombies hold the
skull alone (the Oddball ball: a blow, nothing fired, no grenades, nothing
picked up) and run faster (zombie_speed_percent); the humans keep their
loadouts. The last humans standing win when the time runs out
(time_limit_minutes), the zombies when no human is left. Zombies are the
red team in green, humans the blue team.

Built on Slayer (the owner's choice: game_engine_slayer.c plays it, team
play, its scores, its spawns and the maps' Slayer equipment): an Infection
game type is a Slayer variant with Infection's magic and settings in its
union (game_engine.h struct infection_settings), and its engine Slayer's
with these callbacks on top (infection_engine_get, game_engine_initialize).
Every setting is in that one struct, with its defaults, for a game type
editor. The built-in game type (build_game_variant_infection: the game's
own lists' last, playlist_profile.c) plays by this machine's infection.*
settings (the Vita's settings panel, Multiplayer; a host's own), a game
type saved with its own settings by those.

Host authoritative: only the host (a local game's machine, a network
game's host, the dedicated server) chooses the first zombies, turns a
human, gives the skull, ends the round; its clients take every player's
side, the round's state and the settings from the game type's state it
sends (infection_write_network_state, after Slayer's) and check it before
they take it. A client's own word decides nothing.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_engine_slayer.h"
#include "game/players.h"
#include "interface/hud_messaging.h"
#include "items/items.h"
#include "items/weapons.h"
#include "math/real_math.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "text/unicode.h"
#include "units/units.h"

#include "infection.h"
#include "../src/lang.h"

#include <stdlib.h>

/* ---------- the platform's and the game's */

void platform_log(char const *format, ...);
const char *config_string(char const *name);
long config_integer(char const *name);
boolean network_game_distributed_client(void);
void unit_network_forget_weapon(long unit_index, short slot);
long get_ball_definition_index(void);

/* ---------- constants */

enum
{
	/* a player's side (none: no player in that slot) */
	_infection_side_none = 0,
	_infection_side_human,
	_infection_side_zombie,
	NUMBER_OF_INFECTION_SIDES,

	/* the round */
	_infection_round_waiting = 0,
	_infection_round_active,
	NUMBER_OF_INFECTION_ROUNDS,

	INFECTION_NETWORK_STATE_VERSION = 1,
	INFECTION_MAXIMUM_PLAYERS = HALO_PORT_MAXIMUM_NETWORK_PLAYERS,
};

/* ---------- structures */

/* Infection's part of the game type's state, after Slayer's */
struct infection_network_state
{
	byte version;
	byte round;
	/* the most first zombies the round had (none: still waiting) */
	byte had_zombies;
	byte pad;
	struct infection_settings settings;
	/* each player's side, by absolute index */
	byte sides[INFECTION_MAXIMUM_PLAYERS];
};

typedef char verify_infection_network_state_size[
	sizeof(struct infection_network_state) == 4 + sizeof(struct infection_settings) + INFECTION_MAXIMUM_PLAYERS ?
	1 : -1];

static struct
{
	struct game_engine engine;
	/* the settings played by (the host's, a client's from the host) */
	struct infection_settings settings;
	short round;
	boolean had_zombies;
	/* the human last standing told of (an absolute index, NONE none) */
	long last_human_told;
	/* each player's side, and when a zombie was last given its skull */
	byte sides[INFECTION_MAXIMUM_PLAYERS];
	long skull_given_time[INFECTION_MAXIMUM_PLAYERS];
	/* the round's end logged, and a problem */
	boolean end_logged;
	boolean problem_logged;
	/* the game over seen here (every machine) */
	boolean over_logged;
} infection_globals;

/* ---------- settings */

void infection_settings_default(
	struct infection_settings *settings)
{
	csmemset(settings, 0, sizeof(*settings));
	settings->players_per_first_zombie = 8;
	settings->zombie_speed_percent = 115;
	settings->time_limit_minutes = 6;
	settings->zombie_weapon = _infection_zombie_weapon_skull;
	settings->zombie_respawn_seconds = 3;
	settings->zombie_shields = TRUE;
	settings->zombie_tough = FALSE;
}

void infection_settings_clamp(
	struct infection_settings *settings)
{
	settings->players_per_first_zombie = (byte)PIN(settings->players_per_first_zombie, 2, 16);
	settings->zombie_speed_percent = (byte)PIN(settings->zombie_speed_percent, 100, 150);
	settings->time_limit_minutes = (byte)MIN(settings->time_limit_minutes, 30);
	if (settings->zombie_weapon >= NUMBER_OF_INFECTION_ZOMBIE_WEAPONS)
		settings->zombie_weapon = _infection_zombie_weapon_skull;
	settings->zombie_respawn_seconds = (byte)PIN(settings->zombie_respawn_seconds, 3, 15);
	settings->zombie_shields = settings->zombie_shields ? TRUE : FALSE;
	settings->zombie_tough = settings->zombie_tough ? TRUE : FALSE;
	csmemset(settings->reserved, 0, sizeof(settings->reserved));
}

static long infection_setting(char const *variable, char const *name, long fallback)
{
	char const *value = getenv(variable);

	if (value && value[0])
		return atol(value);
	return name ? config_integer(name) : fallback;
}

/* this machine's settings for the built-in game type (infection.*, the
Vita's settings panel's rows; HALO_INFECTION_* first, for the tests) */
void infection_settings_configured(
	struct infection_settings *settings)
{
	infection_settings_default(settings);
	settings->players_per_first_zombie = (byte)PIN(infection_setting("HALO_INFECTION_ZOMBIES",
		"infection.players_per_zombie", 8), 0, 255);
	settings->zombie_speed_percent = (byte)PIN(infection_setting("HALO_INFECTION_SPEED", "infection.zombie_speed", 115),
		0, 255);
	settings->time_limit_minutes = (byte)PIN(infection_setting("HALO_INFECTION_TIME", "infection.time_limit", 6), 0, 255);
	settings->zombie_respawn_seconds = (byte)PIN(infection_setting("HALO_INFECTION_RESPAWN", "infection.zombie_respawn",
		3), 0, 255);
	settings->zombie_shields = infection_setting("HALO_INFECTION_SHIELDS", "infection.zombie_shields", 1) != 0;
	settings->zombie_tough = infection_setting("HALO_INFECTION_TOUGH", "infection.zombie_tough", 0) != 0;
	infection_settings_clamp(settings);
}

boolean infection_variant(
	struct game_variant const *variant)
{
	return variant && variant->game_engine_index == game_engine_slayer &&
		variant->game_engine_variant.slayer.infection_magic == INFECTION_VARIANT_MAGIC &&
		variant->game_engine_variant.slayer.infection_version != 0;
}

struct game_variant *build_game_variant_infection(
	struct game_variant *variant)
{
	struct game_variant result;

	csmemset(&result, 0, sizeof(result));
	result.game_engine_index = game_engine_slayer;
	/* (the motion tracker, friends' nav points; shields; the map's own
	equipment, as Team Slayer) */
	SET_FLAG(result.universal_variant.flags, _game_variant_draw_object_in_motion_sensor_bit, TRUE);
	SET_FLAG(result.universal_variant.flags, _game_variant_allow_friendly_navpoints_bit, TRUE);
	result.universal_variant.teams = TRUE;
	result.universal_variant.goal_radar = 0;
	result.universal_variant.health = 1.0f;
	/* (a zombie back in 3 s: infection_settings' respawn, the least the game
	gives) */
	result.universal_variant.respawn_time = 90;
	result.universal_variant.suicide_penalty = 0;
	/* (the round ends on its own rules: infection_should_end_game) */
	result.universal_variant.score_to_win = 50;
	/* (no vehicles: on foot, as Halo 3's Infection) */
	result.universal_variant.vehicle_set = 1;
	result.game_engine_variant.slayer.no_death_bonus = TRUE;
	result.game_engine_variant.slayer.no_kill_penalty = TRUE;
	result.game_engine_variant.slayer.infection_version = INFECTION_VARIANT_VERSION;
	result.game_engine_variant.slayer.infection_magic = INFECTION_VARIANT_MAGIC;
	infection_settings_configured(&result.game_engine_variant.slayer.infection);
	/* (the game's own: playlist_profile.c's default bit) */
	result.flags = 1;
	*variant = result;
	return variant;
}

/* ---------- the sides */

static boolean infection_host(void)
{
	return !network_game_distributed_client();
}

static long infection_absolute(long player_index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);

	return absolute_index >= 0 && absolute_index < INFECTION_MAXIMUM_PLAYERS ? absolute_index : NONE;
}

boolean infection_active(
	void)
{
	return game_engine != NULL && game_engine == &infection_globals.engine;
}

static short infection_side(long player_index)
{
	long absolute_index = infection_absolute(player_index);

	return absolute_index != NONE ? infection_globals.sides[absolute_index] : _infection_side_none;
}

boolean infection_player_is_zombie(
	long player_index)
{
	return infection_active() && player_index != NONE && infection_side(player_index) == _infection_side_zombie;
}

/* the team a side plays on: zombies red, humans blue */
static long infection_side_team(short side)
{
	return side == _infection_side_zombie ? 0 : 1;
}

/* the side into the player's team (the game's, its unit's) */
static void infection_apply_side(long player_index)
{
	struct player_datum *player = player_get(player_index);
	short side = infection_side(player_index);
	long team_index;

	if (side == _infection_side_none)
		return;
	team_index = infection_side_team(side);
	player->team_index = team_index;
	player->network_player_data.team_index = (char)team_index;
	if (player->unit_index != NONE)
	{
		struct unit_datum *unit = unit_try_and_get(player->unit_index);

		if (unit)
			unit->object.owner_team_index = (short)team_index;
	}
}

static void infection_set_side(long player_index, short side)
{
	long absolute_index = infection_absolute(player_index);

	if (absolute_index == NONE)
		return;
	infection_globals.sides[absolute_index] = (byte)side;
	infection_globals.skull_given_time[absolute_index] = NONE;
	infection_apply_side(player_index);
}

/* the players of each side in the game (not quit) */
static void infection_count(long *humans, long *zombies)
{
	struct data_iterator iterator;
	struct player_datum *player;

	*humans = *zombies = 0;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->quit_out_of_game)
			continue;
		switch (infection_side(iterator.datum_index))
		{
		case _infection_side_human: (*humans)++; break;
		case _infection_side_zombie: (*zombies)++; break;
		}
	}
}

/* ---------- what the players are told */

/* a player's name for the log (ASCII; another character a '?') */
static char const *infection_log_name(long player_index, char *name, int size)
{
	wchar_t const *wide = player_get(player_index)->name;
	int length;

	for (length = 0; length < size - 1 && length < 12 && wide[length]; length++)
		name[length] = wide[length] >= 0x20 && wide[length] < 0x7F ? (char)wide[length] : '?';
	name[length] = 0;
	return name;
}

/* to one player (its machine's own local players alone are shown), or to
every player (NONE) */
static void infection_tell(long player_index, wchar_t const *text)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if ((player_index == NONE || iterator.datum_index == player_index) && player->local_player_index != NONE)
			hud_print_message(player->local_player_index, text);
	}
}

static void infection_tell_name(long player_index, wchar_t const *format, wchar_t const *name)
{
	wchar_t text[128];

	usnprintf(text, NUMBEROF(text), format, name);
	text[NUMBEROF(text) - 1] = 0;
	infection_tell(player_index, text);
}

/* a player's side changed, on any machine (the host as it changes it, a
client as the host's state says so): what everyone is told */
static void infection_side_changed(long player_index, short old_side, short new_side)
{
	struct player_datum *player = player_get(player_index);

	if (new_side != _infection_side_zombie || old_side == _infection_side_zombie)
		return;
	if (old_side == _infection_side_human)
	{
		struct data_iterator iterator;

		data_iterator_new(&iterator, player_data);
		while (data_iterator_next(&iterator))
		{
			if (iterator.datum_index != player_index)
				infection_tell_name(iterator.datum_index, TW(L"%s is a zombie now"), player->name);
		}
	}
	infection_tell(player_index, TW(L"You are a zombie: infect the humans"));
}

/* the last human standing told so, once (every machine: as the sides change) */
static void infection_check_last_human(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long humans, zombies, last = NONE;

	infection_count(&humans, &zombies);
	if (humans != 1 || zombies == 0)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (!player->quit_out_of_game && infection_side(iterator.datum_index) == _infection_side_human)
			last = iterator.datum_index;
	}
	if (last == NONE || infection_absolute(last) == infection_globals.last_human_told)
		return;
	infection_globals.last_human_told = infection_absolute(last);
	infection_tell(last, TW(L"Last man standing!"));
	data_iterator_new(&iterator, player_data);
	while (data_iterator_next(&iterator))
	{
		if (iterator.datum_index != last)
			infection_tell_name(iterator.datum_index, TW(L"%s is the last human"), player_get(last)->name);
	}
	{
		char name[16];

		platform_log("infection: %s is the last human", infection_log_name(last, name, sizeof(name)));
	}
}

/* ---------- the host's rules */

/* the host: a human to the zombies (killed, or the first zombies) */
static void infection_turn(long player_index, char const *why)
{
	short old_side = infection_side(player_index);
	long humans, zombies;

	if (old_side == _infection_side_zombie)
		return;
	infection_set_side(player_index, _infection_side_zombie);
	infection_globals.had_zombies = TRUE;
	infection_count(&humans, &zombies);
	{
		char name[16];

		platform_log("infection: %s is a zombie (%s): %ld humans, %ld zombies",
			infection_log_name(player_index, name, sizeof(name)), why, humans, zombies);
	}
	infection_side_changed(player_index, old_side, _infection_side_zombie);
	infection_check_last_human();
}

/* the host: the round's first zombies, at random among the players in the
game (one for each players_per_first_zombie, one at least, never all) */
static void infection_choose_first_zombies(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long candidates[INFECTION_MAXIMUM_PLAYERS];
	long count = 0, wanted, chosen;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (!player->quit_out_of_game && infection_side(iterator.datum_index) == _infection_side_human &&
			count < INFECTION_MAXIMUM_PLAYERS)
		{
			candidates[count++] = iterator.datum_index;
		}
	}
	if (count < 2)
		return;
	wanted = (count + infection_globals.settings.players_per_first_zombie - 1) /
		infection_globals.settings.players_per_first_zombie;
	wanted = PIN(wanted, 1, count - 1);
	for (chosen = 0; chosen < wanted; chosen++)
	{
		long pick = (long)(unsigned short)seed_random_range(get_global_random_seed_address(), 0, (short)(count - chosen)) %
			(count - chosen);
		long player_index = candidates[pick];

		candidates[pick] = candidates[count - chosen - 1];
		infection_turn(player_index, "a first zombie, at random");
	}
}

/* the host's round under way at the first spawn: everyone in the game a
human, then the first zombies chosen */
static void infection_begin_round(void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (infection_globals.round != _infection_round_waiting)
		return;
	infection_globals.round = _infection_round_active;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (infection_side(iterator.datum_index) == _infection_side_none)
			infection_set_side(iterator.datum_index, _infection_side_human);
	}
	platform_log("infection: the round begins (one first zombie for each %d players, zombies %d%% speed, %d minutes)",
		(int)infection_globals.settings.players_per_first_zombie, (int)infection_globals.settings.zombie_speed_percent,
		(int)infection_globals.settings.time_limit_minutes);
	infection_choose_first_zombies();
	data_iterator_new(&iterator, player_data);
	while (data_iterator_next(&iterator))
	{
		if (infection_side(iterator.datum_index) == _infection_side_human)
			infection_tell(iterator.datum_index, TW(L"You are a human: survive"));
	}
}

static long infection_skull_definition(void)
{
	return get_ball_definition_index();
}

static boolean infection_weapon_is_skull(long weapon_index)
{
	struct weapon_datum *weapon = weapon_index != NONE ?
		(struct weapon_datum *)object_try_and_get_and_verify_type(weapon_index, _object_mask_weapon) : NULL;

	return weapon && weapon->definition_index == infection_skull_definition();
}

/* a problem said once a game */
static void infection_log_once(char const *text)
{
	if (infection_globals.problem_logged)
		return;
	infection_globals.problem_logged = TRUE;
	platform_log("%s", text);
}

/* the host: a zombie's unit holds the skull alone and no grenades */
static void infection_arm_zombie(long player_index)
{
	struct player_datum *player = player_get(player_index);
	struct unit_datum *unit;
	struct object_placement_data placement;
	long definition_index = infection_skull_definition();
	long absolute_index = infection_absolute(player_index);
	boolean has_skull = FALSE;
	short slot;
	long weapon_index;

	if (player->unit_index == NONE || absolute_index == NONE)
		return;
	unit = unit_get(player->unit_index);
	unit->unit.grenade_counts[0] = 0;
	unit->unit.grenade_counts[1] = 0;
	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		weapon_index = unit->unit.weapon_object_indices[slot];
		if (weapon_index == NONE)
			continue;
		if (infection_weapon_is_skull(weapon_index) && !has_skull)
		{
			has_skull = TRUE;
			continue;
		}
		unit_network_forget_weapon(player->unit_index, slot);
		object_delete(weapon_index);
	}
	if (has_skull)
		return;
	infection_globals.skull_given_time[absolute_index] = game_time_get();
	if (definition_index == NONE)
	{
		infection_log_once("infection: this map's globals have no skull (the Oddball ball): the zombies hold nothing");
		return;
	}
	object_placement_data_new(&placement, definition_index, NONE);
	placement.position = unit->object.position;
	weapon_index = object_new(&placement);
	if (weapon_index == NONE)
	{
		infection_log_once("infection: a zombie's skull could not be made (no room for objects)");
		return;
	}
	if (getenv("HALO_INFECTION_TRACE"))
		platform_log("infection: trace: player %ld given skull %lx", absolute_index, (unsigned long)weapon_index);
	if (!unit_add_weapon_to_inventory(player->unit_index, weapon_index, _unit_add_weapon_starting))
	{
		infection_log_once("infection: a zombie could not take its skull");
		object_delete(weapon_index);
	}
}

/* ---------- the engine's callbacks */

static boolean infection_initialize_for_new_map(void)
{
	csmemset(infection_globals.sides, 0, sizeof(infection_globals.sides));
	csmemset(infection_globals.skull_given_time, 0xFF, sizeof(infection_globals.skull_given_time));
	infection_globals.round = _infection_round_waiting;
	infection_globals.had_zombies = FALSE;
	infection_globals.last_human_told = NONE;
	infection_globals.end_logged = FALSE;
	infection_globals.problem_logged = FALSE;
	infection_globals.over_logged = FALSE;
	return slayer_engine.initialize_for_new_map ? slayer_engine.initialize_for_new_map() : TRUE;
}

static void infection_player_added(long player_index)
{
	if (slayer_engine.player_added)
		slayer_engine.player_added(player_index);
	if (player_index == NONE || !infection_host())
		return;
	/* (a slot a player who quit had: forgotten) */
	infection_set_side(player_index, _infection_side_none);
	/* (one joining the round under way: a zombie, as Halo 3's) */
	if (infection_globals.round == _infection_round_active)
	{
		infection_set_side(player_index, _infection_side_human);
		if (infection_globals.had_zombies)
			infection_turn(player_index, "joined the game in progress");
	}
}

static void infection_prespawn_player_update(long player_index)
{
	if (slayer_engine.prespawn_player_update)
		slayer_engine.prespawn_player_update(player_index);
	if (!infection_host())
		return;
	infection_begin_round();
	if (infection_side(player_index) == _infection_side_none)
		infection_set_side(player_index, _infection_side_human);
	infection_apply_side(player_index);
}

/* each tick, every machine: a zombie's speed, the others' a player's */
static void infection_player_update_each_tick(long player_index)
{
	struct player_datum *player = player_get(player_index);

	player->speed_multiplier = infection_side(player_index) == _infection_side_zombie ?
		infection_globals.settings.zombie_speed_percent / 100.0f : 1.0f;
	infection_apply_side(player_index);
}

static void infection_update(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long humans, zombies;
	long now = game_time_get();

	if (slayer_engine.update)
		slayer_engine.update();
	/* (every machine: the game's end as it has it, for its log) */
	if (!infection_globals.over_logged && !game_engine_in_play())
	{
		infection_globals.over_logged = TRUE;
		infection_count(&humans, &zombies);
		platform_log("infection: game over here: the %s win (%ld humans, %ld zombies)",
			humans > 0 ? "humans" : "zombies", humans, zombies);
	}
	/* (every machine: no grenades in a zombie's hands, its own predicted
	unit's too) */
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->unit_index != NONE && infection_side(iterator.datum_index) == _infection_side_zombie)
		{
			struct unit_datum *unit = unit_get(player->unit_index);

			unit->unit.grenade_counts[0] = 0;
			unit->unit.grenade_counts[1] = 0;
		}
	}
	if (!infection_host() || infection_globals.round != _infection_round_active)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = infection_absolute(iterator.datum_index);
		short side = infection_side(iterator.datum_index);

		if (absolute_index == NONE || player->quit_out_of_game)
			continue;
		if (side == _infection_side_zombie)
		{
			/* (a zombie back soon: its respawn, at most the setting's) */
			if (player->unit_index == NONE)
			{
				player->respawn_timer = MIN(player->respawn_timer,
					(long)infection_globals.settings.zombie_respawn_seconds * TICKS_PER_SECOND);
			}
			/* (the skull dropped or never had: another, a second on) */
			else
			{
				struct unit_datum *unit = unit_get(player->unit_index);
				short slot;
				boolean has_skull = FALSE, has_other = FALSE;

				for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
				{
					if (unit->unit.weapon_object_indices[slot] == NONE)
						continue;
					if (infection_weapon_is_skull(unit->unit.weapon_object_indices[slot]))
						has_skull = TRUE;
					else
						has_other = TRUE;
				}
				if (has_other || (!has_skull && (infection_globals.skull_given_time[absolute_index] == NONE ||
					now - infection_globals.skull_given_time[absolute_index] >= TICKS_PER_SECOND)))
				{
					infection_arm_zombie(iterator.datum_index);
				}
			}
		}
		/* (a human holds no skull) */
		else if (side == _infection_side_human && player->unit_index != NONE)
		{
			struct unit_datum *unit = unit_get(player->unit_index);
			short slot;

			for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
			{
				long weapon_index = unit->unit.weapon_object_indices[slot];

				if (weapon_index != NONE && infection_weapon_is_skull(weapon_index))
				{
					unit_network_forget_weapon(player->unit_index, slot);
					object_delete(weapon_index);
				}
			}
		}
	}
	/* (a skull on the ground: a dead zombie's, gone) */
	{
		struct object_iterator objects;
		long definition_index = infection_skull_definition();

		object_iterator_new(&objects, _object_mask_weapon, 0);
		while (definition_index != NONE && object_iterator_next(&objects))
		{
			struct weapon_datum *weapon = weapon_get(objects.index);

			if (weapon->definition_index == definition_index && weapon->object.parent_object_index == NONE &&
				!TEST_FLAG(weapon->item.flags, _item_attached_to_unit_bit))
			{
				if (getenv("HALO_INFECTION_TRACE"))
					platform_log("infection: trace: a skull on the ground deleted (%lx)", (unsigned long)objects.index);
				object_delete(objects.index);
			}
		}
	}
	/* (no zombie left - they all quit - with humans to play: one chosen
	again; its unit dies, and it comes back a zombie) */
	infection_count(&humans, &zombies);
	if (infection_globals.had_zombies && zombies == 0 && humans >= 2)
	{
		long candidates[INFECTION_MAXIMUM_PLAYERS];
		long count = 0;

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (!player->quit_out_of_game && infection_side(iterator.datum_index) == _infection_side_human)
				candidates[count++] = iterator.datum_index;
		}
		if (count > 0)
		{
			long player_index = candidates[(long)(unsigned short)seed_random_range(get_global_random_seed_address(), 0,
				(short)count) % count];

			infection_turn(player_index, "the zombies left: a new one, at random");
			if (player_get(player_index)->unit_index != NONE)
				unit_kill_no_statistics(player_get(player_index)->unit_index);
		}
	}
	/* (a round of one player waiting for a second: the first zombie once
	there are two) */
	else if (!infection_globals.had_zombies && humans >= 2)
		infection_choose_first_zombies();
}

static long infection_get_score(long player_index, enum get_score_type score_type)
{
	if (score_type == _get_score_team)
	{
		long humans, zombies;

		infection_count(&humans, &zombies);
		return infection_side(player_index) == _infection_side_zombie ? zombies : humans;
	}
	return slayer_engine.get_player_score(player_index, score_type);
}

static wchar_t *infection_format_team_name(long team_index, wchar_t *buffer)
{
	long humans, zombies;

	infection_count(&humans, &zombies);
	usnprintf(buffer, 32, L"%d", team_index == 0 ? zombies : humans);
	return buffer;
}

static boolean infection_allow_pick_up(long unit_index, long weapon_index)
{
	long player_index = unit_index != NONE ? player_index_from_unit_index(unit_index) : NONE;
	boolean skull = infection_weapon_is_skull(weapon_index);

	if (player_index == NONE)
		return slayer_engine.allow_pick_up ? slayer_engine.allow_pick_up(unit_index, weapon_index) : TRUE;
	/* (a zombie its skull alone; a human anything but the skull) */
	if (infection_side(player_index) == _infection_side_zombie)
		return skull;
	return !skull;
}

static void infection_player_killed_player(long killing_player_index, long killing_object_index, long dead_player_index,
	boolean friendly_fire)
{
	struct player_datum *dead_player = player_get(dead_player_index);

	if (slayer_engine.player_killed_player)
		slayer_engine.player_killed_player(killing_player_index, killing_object_index, dead_player_index, friendly_fire);
	if (!infection_host() || dead_player->quit_out_of_game || infection_globals.round != _infection_round_active ||
		infection_side(dead_player_index) != _infection_side_human || !infection_globals.had_zombies)
	{
		return;
	}
	if (killing_player_index != NONE && killing_player_index != dead_player_index &&
		infection_side(killing_player_index) == _infection_side_zombie)
	{
		char killer[16], dead[16];

		platform_log("infection: %s infected %s", infection_log_name(killing_player_index, killer, sizeof(killer)),
			infection_log_name(dead_player_index, dead, sizeof(dead)));
		infection_turn(dead_player_index, "killed by a zombie");
	}
	else
		infection_turn(dead_player_index, "died");
}

static boolean infection_format_message(long player_index, long message, long message_data, wchar_t *buffer,
	long buffer_count)
{
	/* (the score shown: the humans and zombies left) */
	if (message == 0x16)
	{
		long humans, zombies;

		infection_count(&humans, &zombies);
		usnprintf(buffer, buffer_count, TW(L"%d humans, %d zombies"), humans, zombies);
		return TRUE;
	}
	return slayer_engine.format_message ?
		slayer_engine.format_message(player_index, message, message_data, buffer, buffer_count) : FALSE;
}

/* (game_engine_get_damage_multiplier's traits, asked as (player, trait):
3 takes half the damage) a zombie's toughness */
static boolean infection_test_trait(long player_index, long trait)
{
	if (trait == 3 && infection_globals.settings.zombie_tough && infection_side(player_index) == _infection_side_zombie)
		return TRUE;
	return slayer_engine.test_trait ? slayer_engine.test_trait(player_index, trait) : FALSE;
}

/* who won: the humans while any is left, else the zombies */
static long infection_did_player_win(long player_index)
{
	long humans, zombies;
	short winner;

	infection_count(&humans, &zombies);
	if (humans == 0 && zombies == 0)
		return NONE;
	winner = humans > 0 ? _infection_side_human : _infection_side_zombie;
	return infection_side(player_index) == winner;
}

struct game_engine *infection_engine_get(
	void)
{
	struct game_variant const *variant = game_engine_get_variant();

	infection_globals.engine = slayer_engine;
	infection_globals.engine.name = "infection";
	infection_globals.engine.initialize_for_new_map = infection_initialize_for_new_map;
	infection_globals.engine.player_added = infection_player_added;
	infection_globals.engine.player_update_each_tick = infection_player_update_each_tick;
	infection_globals.engine.update = infection_update;
	infection_globals.engine.get_player_score = infection_get_score;
	infection_globals.engine.format_team_name = infection_format_team_name;
	infection_globals.engine.allow_pick_up = infection_allow_pick_up;
	infection_globals.engine.player_killed_player = infection_player_killed_player;
	infection_globals.engine.format_message = infection_format_message;
	infection_globals.engine.prespawn_player_update = infection_prespawn_player_update;
	infection_globals.engine.did_player_win = infection_did_player_win;
	infection_globals.engine.test_trait = infection_test_trait;
	/* the settings: the variant's; the built-in game type's this machine's
	(a client's are the host's, from its state) */
	infection_globals.settings = variant->game_engine_variant.slayer.infection;
	if (TEST_FLAG(variant->flags, 0))
		infection_settings_configured(&infection_globals.settings);
	infection_settings_clamp(&infection_globals.settings);
	return &infection_globals.engine;
}

/* ---------- game_engine.c's hooks */

void infection_postspawn_player_update(
	long player_index)
{
	if (!infection_active() || !infection_host())
		return;
	infection_apply_side(player_index);
	if (infection_side(player_index) == _infection_side_zombie)
		infection_arm_zombie(player_index);
}

boolean infection_should_end_game(
	void)
{
	long humans, zombies;
	long limit = infection_globals.settings.time_limit_minutes * 60L * TICKS_PER_SECOND;
	boolean end = FALSE;
	char const *why = NULL;

	if (infection_globals.round != _infection_round_active)
		return FALSE;
	infection_count(&humans, &zombies);
	if (humans == 0 && zombies == 0)
		end = TRUE, why = "everyone left";
	else if (infection_globals.had_zombies && humans == 0)
		end = TRUE, why = "no human left: the zombies win";
	else if (infection_globals.had_zombies && zombies == 0 && humans < 2)
		end = TRUE, why = "the zombies left: the humans win";
	else if (limit > 0 && game_time_get() >= limit)
		end = TRUE, why = humans > 0 ? "time: the humans win" : "time: the zombies win";
	if (end && !infection_globals.end_logged)
	{
		infection_globals.end_logged = TRUE;
		platform_log("infection: the round is over (%s): %ld humans, %ld zombies", why, humans, zombies);
	}
	return end;
}

/* the score's title in the game: humans and zombies left, the time left */
boolean infection_title_string(
	long player_index,
	wchar_t *title_string,
	long maximum_count)
{
	long humans, zombies;
	long limit = infection_globals.settings.time_limit_minutes * 60L * TICKS_PER_SECOND;

	if (!infection_active())
		return FALSE;
	infection_count(&humans, &zombies);
	if (limit > 0)
	{
		wchar_t time_string[32];

		ticks_to_unicode_time_string(MAX(limit - game_time_get(), 0), NUMBEROF(time_string), time_string);
		usnprintf(title_string, maximum_count, TW(L"Humans %d, zombies %d, %s left"), humans, zombies, time_string);
	}
	else
		usnprintf(title_string, maximum_count, TW(L"Humans %d, zombies %d"), humans, zombies);
	title_string[maximum_count - 1] = 0;
	return TRUE;
}

/* a zombie's colour: green, a human's its team's (blue) */
boolean infection_change_color(
	long player_index,
	union real_rgb_color *color)
{
	if (!infection_player_is_zombie(player_index))
		return FALSE;
	color->red = 0.30f;
	color->green = 0.62f;
	color->blue = 0.12f;
	return TRUE;
}

long infection_time_limit_minutes(
	void)
{
	return infection_active() ? infection_globals.settings.time_limit_minutes : 0;
}

boolean infection_unit_refuses_grenades(
	long unit_index)
{
	long player_index;

	if (!infection_active() || unit_index == NONE)
		return FALSE;
	player_index = player_index_from_unit_index(unit_index);
	return player_index != NONE && infection_side(player_index) == _infection_side_zombie;
}

boolean infection_zombie_shields(
	void)
{
	return infection_globals.settings.zombie_shields;
}

/* ---------- the bots (bots.c) */

/* a zombie bot hunts the nearest human it does not see (as Halo 3's
zombies know where the humans are): the human's place; FALSE for none, or a
human bot */
boolean infection_bot_hunt(
	long player_index,
	real_point3d const *position,
	real_point3d *target)
{
	struct data_iterator iterator;
	struct player_datum *player;
	real best = REAL_MAX;
	boolean found = FALSE;

	if (!infection_player_is_zombie(player_index))
		return FALSE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum *object;
		real dx, dy, dz, distance;

		if (player->quit_out_of_game || player->unit_index == NONE ||
			infection_side(iterator.datum_index) != _infection_side_human)
		{
			continue;
		}
		object = object_get(player->unit_index);
		dx = object->object.position.x - position->x;
		dy = object->object.position.y - position->y;
		dz = object->object.position.z - position->z;
		distance = dx * dx + dy * dy + dz * dz;
		if (distance < best)
		{
			best = distance;
			*target = object->object.position;
			found = TRUE;
		}
	}
	return found;
}

/* ---------- the distributed netcode */

long infection_network_state_size(
	void)
{
	return sizeof(struct infection_network_state);
}

long infection_write_network_state(
	byte *buffer,
	long size)
{
	struct infection_network_state state;

	if (size < (long)sizeof(state))
		return 0;
	csmemset(&state, 0, sizeof(state));
	state.version = INFECTION_NETWORK_STATE_VERSION;
	state.round = (byte)infection_globals.round;
	state.had_zombies = infection_globals.had_zombies ? 1 : 0;
	state.settings = infection_globals.settings;
	csmemcpy(state.sides, infection_globals.sides, sizeof(state.sides));
	csmemcpy(buffer, &state, sizeof(state));
	return sizeof(state);
}

/* a client: the host's, checked first (a value out of its range refuses
the whole state) */
boolean infection_read_network_state(
	byte const *buffer,
	long size,
	boolean first)
{
	struct infection_network_state state;
	struct infection_settings settings;
	struct data_iterator iterator;
	long index;

	if (size != (long)sizeof(state))
		return FALSE;
	csmemcpy(&state, buffer, sizeof(state));
	if (state.version != INFECTION_NETWORK_STATE_VERSION || state.round >= NUMBER_OF_INFECTION_ROUNDS ||
		state.had_zombies > 1)
	{
		return FALSE;
	}
	for (index = 0; index < INFECTION_MAXIMUM_PLAYERS; index++)
	{
		if (state.sides[index] >= NUMBER_OF_INFECTION_SIDES)
			return FALSE;
	}
	settings = state.settings;
	infection_settings_clamp(&settings);
	if (csmemcmp(&settings, &state.settings, sizeof(settings)) != 0)
		return FALSE;
	infection_globals.settings = settings;
	infection_globals.round = state.round;
	infection_globals.had_zombies = state.had_zombies;
	data_iterator_new(&iterator, player_data);
	while (data_iterator_next(&iterator))
	{
		long absolute_index = infection_absolute(iterator.datum_index);
		short old_side, new_side;

		if (absolute_index == NONE)
			continue;
		old_side = infection_globals.sides[absolute_index];
		new_side = state.sides[absolute_index];
		/* (a player the host has not yet: its own start's) */
		if (new_side == _infection_side_none)
			continue;
		infection_globals.sides[absolute_index] = (byte)new_side;
		infection_apply_side(iterator.datum_index);
		if (old_side != new_side)
		{
			char name[16];

			platform_log("infection: the host says %s is a %s", infection_log_name(iterator.datum_index, name,
				sizeof(name)), new_side == _infection_side_zombie ? "zombie" : "human");
			if (first)
			{
				if (new_side == _infection_side_zombie)
					infection_side_changed(iterator.datum_index, _infection_side_none, new_side);
			}
			else
				infection_side_changed(iterator.datum_index, old_side, new_side);
		}
	}
	infection_check_last_human();
	return TRUE;
}
