/*
GAME_ENGINE.H

header included in hcex build.
*/

#ifndef __GAME_ENGINE_H
#define __GAME_ENGINE_H
#pragma once

#include "math/real_math.h"

/* ---------- constants */

enum
{
	_game_engine_disable_dynamic_light_bit = 0,
	_game_engine_disable_integrated_lights_bit,
	_game_engine_5_or_more_players_bit,
};

enum
{
	_game_variant_draw_object_in_motion_sensor_bit = 0,
	_game_variant_allow_friendly_navpoints_bit,
	_game_variant_infinite_grenades_bit,
	_game_variant_no_shields_bit,
	_game_variant_always_invisible_bit,
};

enum game_engine_type
{
	game_engine_none = 0,
	game_engine_ctf,
	game_engine_slayer,
	game_engine_oddball,
	game_engine_king,
	game_engine_race,
	game_engine_terminator,
	game_engine_stub,
	number_of_game_engines,
	first_usable_game_engine_index = game_engine_ctf,
	last_usable_game_engine_index = game_engine_race,
};

enum get_score_type
{
	_get_score_individual = 0,
	_get_score_team,
};

/* ---------- macros */

/* ---------- structures */

struct game_globals;
struct player_starting_location;
struct weapon_datum;
union real_argb_color;
union real_point2d;
union real_point3d;
union real_rgb_color;

struct universal_variant
{
	boolean teams;
	byte pad0;
	byte pad1;
	byte pad2;
	unsigned long flags;
	long goal_radar;
	boolean odd_man_out;
	byte pad4;
	byte pad5;
	byte pad6;
	long respawn_time_growth;
	long respawn_time;
	long suicide_penalty;
	long lives;
	real health;
	long score_to_win;
	long weapon_set;
	long vehicle_set;
};

struct ctf_variant
{
	boolean assault;
	boolean reset_on_capture;
	boolean flag_must_reset;
	boolean flag_at_home_to_score;
	long single_flag_time;
};

/* port: Infection (port/linux/game/infection.c): Team Slayer whose dead
humans join the zombies, the last human standing winning. Its settings are
one struct with its defaults (infection_settings_default), kept in Slayer's
part of the variant past Slayer's own three switches: the variant's 0x68
bytes go as they are to every machine of a network game, into the game
types' files (playlist_profile.c) and through a game type editor, so an
Infection game type is a Slayer variant with INFECTION_VARIANT_MAGIC there.
An older build reads it as Team Slayer (and is refused from an Infection
game: infection_net.c). */
enum
{
	/* 'INFC' (no other game's union holds it there: Oddball's speed with the
	ball is 0 to 2, CTF's single flag time a few minutes' ticks) */
	INFECTION_VARIANT_MAGIC = 0x43464E49,
	INFECTION_VARIANT_VERSION = 1,
};

enum
{
	/* the zombies' one weapon: the skull (the Oddball ball: a blow, nothing
	fired), which every multiplayer map's globals have */
	_infection_zombie_weapon_skull = 0,
	NUMBER_OF_INFECTION_ZOMBIE_WEAPONS,
};

/* (each field a byte, its range infection_settings_clamp's) */
struct infection_settings
{
	/* one first zombie for each this many players (2 to 16; 8) */
	byte players_per_first_zombie;
	/* the zombies' speed, a percentage of a player's (100 to 150; 115) */
	byte zombie_speed_percent;
	/* minutes: the humans left then win (0: none, to 30; 6) */
	byte time_limit_minutes;
	/* _infection_zombie_weapon_... */
	byte zombie_weapon;
	/* seconds before a dead zombie comes back (3 to 15; 3) */
	byte zombie_respawn_seconds;
	/* the zombies' shields: TRUE as a player's, FALSE none (TRUE) */
	byte zombie_shields;
	/* TRUE: the zombies take half the damage a player does, as Oddball's
	toughness trait (FALSE: the skull's blow kills at once already) */
	byte zombie_tough;
	/* (zeros: room for later settings) */
	byte reserved[9];
};

typedef char verify_infection_settings_size[sizeof(struct infection_settings) == 0x10 ? 1 : -1];

struct slayer_variant
{
	boolean no_death_bonus;
	boolean no_kill_penalty;
	boolean kill_in_order;
	/* port: Infection's version (INFECTION_VARIANT_VERSION), its magic and
	its settings; zeros in every other Slayer variant */
	byte infection_version;
	unsigned long infection_magic;
	struct infection_settings infection;
};

struct king_variant
{
	boolean moving_hill;
};

/* the balls of an oddball game */
enum
{
	MAXIMUM_ODDBALLS = 16,
};

struct oddball_variant
{
	boolean random_start;
	boolean ball_spawn_delay;
	long speed_with_ball;
	long trait_with_ball;
	long trait_without_ball;
	long oddball_ball_type;
	long ball_spawn_count;
};

struct race_variant
{
	long race_type;
	long team_scoring;
};

union game_engine_variant
{
	struct ctf_variant ctf;
	struct slayer_variant slayer;
	struct king_variant king;
	struct oddball_variant oddball;
	struct race_variant race;
};

struct game_variant
{
	wchar_t human_readable_game_description[12];
	long game_engine_index;
	struct universal_variant universal_variant;
	union game_engine_variant game_engine_variant;
	word flags;
};

typedef char verify_game_variant_size[sizeof(struct game_variant) == 0x68 ? 1 : -1];
typedef char verify_game_engine_variant_size[sizeof(union game_engine_variant) == 0x18 ? 1 : -1];

/* port: the PC version's gametype options that the Xbox's variant has not
(OpenCE's, c9ee319a, 6b1d81c7: its gametype editor's). The network game
carries them on the crossplay wire (network_game_manager.h,
port/linux/game/network_game_record.c, which checks a host's), and the game
plays by them (game_variant_options_get). This build has no editor for
them: a game hosted here plays the defaults (game_variant_options_default),
the Xbox game's play. */
enum
{
	_friendly_fire_on = 0,
	_friendly_fire_off,
	_friendly_fire_shields_only,
	_friendly_fire_explosives_only,
	NUMBER_OF_FRIENDLY_FIRE_MODES
};

enum
{
	/* other players on the motion tracker: all (the variant's draw object
	in motion sensor bit set), friends only, none */
	_radar_players_all = 0,
	_radar_players_friends,
	_radar_players_none,
	NUMBER_OF_RADAR_PLAYERS
};

enum
{
	_loadout_category = 0,
	_loadout_custom,
	NUMBER_OF_LOADOUTS
};

enum
{
	_loadout_weapon_none = 0,
	_loadout_weapon_random,
	_loadout_weapon_assault_rifle,
	_loadout_weapon_pistol,
	_loadout_weapon_shotgun,
	_loadout_weapon_sniper_rifle,
	_loadout_weapon_rocket_launcher,
	_loadout_weapon_plasma_pistol,
	_loadout_weapon_plasma_rifle,
	_loadout_weapon_needler,
	NUMBER_OF_LOADOUT_WEAPONS
};

enum
{
	_variant_vehicle_warthog = 0,
	_variant_vehicle_ghost,
	_variant_vehicle_scorpion,
	_variant_vehicle_rocket_warthog,
	_variant_vehicle_banshee,
	_variant_vehicle_gun_turret,
	NUMBER_OF_VARIANT_VEHICLES,
	MAXIMUM_VARIANT_VEHICLE_COUNT = 4,
	/* a team's vehicles: those of a vehicle set (universal_variant's
	vehicle_set values), else every one the map places, as Halo PC places
	them (PC), else its counts */
	VARIANT_VEHICLE_SET_PC = 0xFE,
	VARIANT_VEHICLE_SET_CUSTOM = 0xFF
};

/* (the starting equipment, generic or the map's, is the variant's own:
_game_variant_generic_starting_equipment_bit) */
struct game_variant_options
{
	/* minutes; 0: none */
	short time_limit;
	short friendly_fire;
	/* seconds added to a team killer's respawn */
	short friendly_fire_penalty;
	/* seconds; 0: never (the Xbox game's) */
	short vehicle_respawn_time;
	boolean auto_team_balance;
	byte radar_players;
	/* red's and blue's (free for all: red's) */
	byte vehicle_set[2];
	byte vehicle_counts[2][NUMBER_OF_VARIANT_VEHICLES];
	/* the weapons: the weapon set's (category), else each player starts
	with these (custom: _loadout_weapon_none, _random, or a weapon) */
	byte loadout;
	byte primary_weapon;
	byte secondary_weapon;
	/* no weapons spawn on the map (its grenades and powerups do): for
	a loadout of none, melee only */
	boolean no_map_weapons;
};

typedef char verify_game_variant_options_size[sizeof(struct game_variant_options) == 0x1C ? 1 : -1];

struct game_engine
{
	char const *name;
	long type;
	void (*dispose)(void);
	boolean (*initialize_for_new_map)(void);
	void (*dispose_from_old_map)(void);
	void (*player_added)(long player_index);
	void (*game_ending)(void);
	void (*game_starting)(void);
	void (*statistics_append)(long statistic);
	void (*handle_client_message)(void *message);
	void (*handle_server_message)(void *message);
	void (*pregame_post_rasterize)(void);
	void (*post_rasterize_objects)(void);
	void (*player_update_each_tick)(
		long player_index);
	void (*objective_weapon_update)(
		long item_index,
		struct weapon_datum *weapon);
	boolean (*picking_up)(
		long weapon_index,
		long player_index);
	void (*weapon_dropped)(
		long weapon_index);
	void (*update)(void);
	long (*get_player_score)(
		long player_index,
		enum get_score_type score_type);
	wchar_t *(*format_player_score)(
		long player_index,
		wchar_t *string);
	wchar_t *(*format_score_name)(
		wchar_t *string);
	wchar_t *(*format_team_name)(
		long team_index,
		wchar_t *string);
	boolean (*allow_pick_up)(
		long unit_index,
		long weapon_index);
	void (*player_damaged_player)(
		long damaging_player_index,
		long dead_player_index,
		boolean damage_type);
	void (*player_killed_player)(
		long killing_player_index,
		long killing_object_index,
		long dead_player_index,
		boolean friendly_fire);
	boolean (*format_message)(
		long player_index,
		long message,
		long message_data,
		wchar_t *buffer,
		long buffer_size);
	real (*starting_location_rating)(
		long player_index,
		struct player_starting_location const *starting_location);
	void (*prespawn_player_update)(
		long player_index);
	void (*player_update)(
		long player_index);
	void (*team_index_override)(void);
	boolean (*player_can_see_goal)(
		long player_index,
		long goal_index);
	boolean (*test_flag)(
		long flag);
	boolean (*test_trait)(
		long trait,
		long value);
	long (*did_player_win)(
		long player_index);
};

typedef char verify_game_engine_size[sizeof(struct game_engine) == 0x88 ? 1 : -1];
typedef char verify_game_engine_player_update_each_tick_offset[
	offsetof(struct game_engine, player_update_each_tick) == 0x34 ? 1 : -1];
typedef char verify_game_engine_objective_weapon_update_offset[
	offsetof(struct game_engine, objective_weapon_update) == 0x38 ? 1 : -1];
typedef char verify_game_engine_format_player_score_offset[
	offsetof(struct game_engine, format_player_score) == 0x4C ? 1 : -1];
typedef char verify_game_engine_format_score_name_offset[
	offsetof(struct game_engine, format_score_name) == 0x50 ? 1 : -1];
typedef char verify_game_engine_format_team_name_offset[
	offsetof(struct game_engine, format_team_name) == 0x54 ? 1 : -1];
typedef char verify_game_engine_format_message_offset[
	offsetof(struct game_engine, format_message) == 0x64 ? 1 : -1];
typedef char verify_game_engine_player_update_offset[
	offsetof(struct game_engine, player_update) == 0x70 ? 1 : -1];

/* port: the most a game type's state for the distributed netcode's clients
may take (port/linux/game/network_distributed.c's MAXIMUM_GAME_STATE_SIZE,
less the postgame state game_engine_write_network_state puts first) */
#define GAME_ENGINE_MAXIMUM_NETWORK_STATE_SIZE (0xF00 - 4)

/* ---------- prototypes/GAME_ENGINE.C */

void game_engine_playlist_initialize(
	void);

long game_globals_get_weapon(
	struct game_globals *game_globals,
	long weapon_list_index);

void game_engine_playlist_begin(
	void);

boolean game_engine_get_current_stage(
	struct game_variant *variant,
	char *map_name);
#ifdef HALO_LINUX
/* port: the next game the server's lobby is set up from as it goes back to
it from the scores (network_game_server_setup_game_from_playlist): the
offline bots' playlist's (port/linux/game/bots_playlist.c) */
void game_engine_port_set_stage(
	char const *map_name,
	struct game_variant const *variant);
#endif

long list_index_to_weapon_definition_index(
	long weapon_list_index);

long weapon_definition_index_to_list_index(
	long weapon_definition_index);

void game_engine_state_message(
	long player_index,
	long state_message,
	long state_message_player_index);

void game_engine_player_depower_active_camo(
	long player_index);

long game_engine_get_team_score(
	long team_index);

long players_in_game(
	void);

real get_blink_alpha(
	void);

long game_engine_player_get_team_index(
	long player_index);

void game_engine_update_player_always_invis(
	long player_index);

boolean game_engine_player_has_flag(
	long player_index);

void game_show_score(
	long player_index,
	long score);

void get_postgame_hilite_colors(
	union real_argb_color *winner_color,
	union real_argb_color *normal_color,
	union real_argb_color *hilite_color);

boolean game_engine_running(
	void);
boolean game_engine_showing_postgame(
	void);
boolean game_engine_in_play(
	void);

boolean game_engine_get_state_message(
	long player_index,
	wchar_t *message,
	long maximum_length);

boolean game_engine_force_single_screen(
	void);

void game_engine_dispose(
	void);

void game_engine_dispose_from_old_map(
	void);

void game_engine_game_ending(
	void);

void game_engine_game_starting(
	void);

void game_engine_statistics_append(
	long statistic);

void game_engine_handle_client_message(
	void *message);

void game_engine_handle_server_message(
	void *message);

void game_engine_post_rasterize_objects(
	void);

void game_engine_post_rasterize(
	void);

void game_engine_nonplayer_post_rasterize(
	void);
void game_engine_update(
	void);

void game_engine_update_non_deterministic(
	real delta_seconds);

boolean match_game_type(
	long game_type,
	long count,
	short const *game_types);
void game_engine_flag_reset(
	long weapon_index,
	union real_point3d const *position);

void game_engine_initialize(
	struct game_variant *variant);

void game_engine_initialize_for_new_map(
	void);
void game_engine_player_added(
	long player_index);

real game_engine_get_distance_rating_for_spawn(
	long player_index,
	union real_point3d const *position);

void game_engine_variant_cleanup(
	struct game_variant *variant);

boolean game_engine_display_team_indicators(
	void);

boolean game_engine_can_score(
	void);

real game_engine_get_starting_location_rating(
	long player_index,
	struct player_starting_location const *starting_location);
boolean game_engine_should_spawn_player(
	long player_index);
void game_engine_client_respawn_countdown(
	long player_index);
void game_engine_postspawn_player_update(
	long player_index);

boolean game_engine_allow_pick_up(
	long unit_index,
	long weapon_index);

boolean game_engine_picking_up(
	long unit_index,
	long weapon_index);

boolean game_engine_test_flag(
	long flag);

boolean game_engine_test_trait(
	long trait,
	long value);

void game_engine_prespawn_player_update(
	long player_index);

long game_engine_did_player_win(
	long player_index);
long game_engine_did_player_win_default(
	long player_index);

struct game_variant *game_engine_get_variant(
	void);

struct game_variant *game_engine_get_variant_by_name(
	struct game_variant *variant,
	char const *name);

boolean game_engine_get_goal_in_use(
	short goal_index);
real_point3d *game_engine_get_goal_position(
	real_point3d *position,
	short goal_index);

void game_engine_set_goal_position(
	short goal_index,
	union real_point3d const *position,
	real height,
	char const *name,
	long player_index,
	short team_index,
	long ignore_player_index);

boolean game_engine_has_teams(
	void);

boolean game_engine_allow_pause(
	void);

boolean game_engine_allow_dynamic_lighting(
	void);

boolean game_engine_infinite_grenades(
	long player_index);

boolean game_engine_has_shield(
	long player_index);

boolean game_engine_draw_object_in_motion_sensor(
	long unit_index);

boolean game_engine_hud_draw_motion_sensor(
	long player_index);

boolean game_engine_player_has_stealth_weapon(
	long player_index);

void game_engine_weapon_fired(
	long player_index);

short game_engine_player_get_custom_motion_sensor_positions(
	long player_index,
	union real_point2d *positions,
	byte *goal_indices,
	short maximum_count);

void game_engine_render_nav_points(
	short local_player_index);

union real_rgb_color *game_engine_player_get_change_color(
	union real_rgb_color *change_color,
	long player_index);

long find_netgame_flags(
	union real_point3d const *position,
	real radius,
	real height,
	short type,
	short team_index,
	long maximum_count,
	long *flag_indices);

long find_netgame_flag(
	union real_point3d const *position,
	real radius,
	real height,
	short type,
	short team_index);

boolean game_engine_should_end_game(
	void);

void game_engine_clear_goal_position(
	short goal_index);

long get_flag_definition_index(
	void);

long get_ball_definition_index(
	void);

void game_engine_override_map_name(
	char const *map_name);

void game_engine_override_game_variant(
	struct game_variant const *variant);

void game_engine_switch_to_postgame(
	void);

void game_engine_load_stage(
	char const *map_name);

void game_engine_end_game(
	void);

void ticks_to_unicode_time_string(
	long ticks,
	unsigned long character_count,
	wchar_t *string);

void game_engine_player_damaged_player(
	long damaging_player_index,
	long dead_player_index,
	boolean damage_type);

boolean game_engine_player_is_out_of_lives(
	long player_index);

boolean game_engine_man_out(
	long player_index);

boolean game_engine_hud_draw_messages(
	long player_index);

boolean game_engine_force_autopickup(
	long unit_index,
	long weapon_index);

void game_engine_play_multiplayer_sound(
	long sound_index);
void game_engine_update_multiplayer_sound(
	void);
void game_engine_intialize_queued_sounds(
	void);

long game_engine_remap_object_definition(long definition_index);

long game_engine_remap_vehicle(long vehicle_definition_index);
/* port: the gametype's vehicles of each team (game_variant_options): the
map's placement of a vehicle allowed, by its type and the nearer team's
spawn points (counted from game_engine_vehicle_placement_begin); every
machine places the same (OpenCE's, network version 24) */
struct scenario_object_datum;
struct tag_block;
void game_engine_vehicle_placement_begin(
	void);
boolean game_engine_vehicle_placement_allowed(
	struct scenario_object_datum const *placement,
	struct tag_block *palette);
/* port: the local player whose motion sensor is drawn (FRIENDS radar) */
void game_engine_motion_sensor_viewer(
	short local_player_index);
/* port: the PC options of a game whose host sent none (the legacy wire, a
variant built in): the Xbox game's play (port/linux/game/network_game_record.c) */
void game_variant_options_default(
	struct game_variant const *variant,
	struct game_variant_options *options);
/* port: the options the game plays by (game.c) */
struct game_variant_options const *game_variant_options_get(
	void);
/* port: whether options a host sent are ones this game can play by: every
value in its range (network_game_record.c) */
boolean game_variant_options_valid(
	struct game_variant_options const *options);
long game_engine_remap_equipment(long equipment_definition_index);
void game_show_score_team(
	long team_index,
	long score);
void game_show_score_you_ally_enemy(
	long player_index,
	long you_score,
	long ally_score,
	long enemy_score,
	long other_player_index);
void game_show_score_extended(
	long player_index,
	long score,
	long team_index);
long game_engine_remap_weapon(long weapon_definition_index);
/* port: the dedicated server's one-weapon games (game_engine.c) */
enum
{
	_game_engine_sole_weapon_none = 0,
	_game_engine_sole_weapon_pistol,
	_game_engine_sole_weapon_sniper_rifle,
};
#ifdef HALO_DEDICATED_SERVER
void game_engine_make_swat_variant(struct game_variant *variant);
void game_engine_make_snipers_variant(struct game_variant *variant, boolean teams);
void game_engine_make_big_team_variant(struct game_variant *variant);
void game_engine_variant_set_generic_starting_equipment(struct game_variant *variant, boolean generic);
#endif


boolean game_engine_allow_integrated_lights(
	long object_index);

void game_engine_player_killed(
	long killing_player_index,
	long killing_object_index,
	long dead_player_index,
	boolean friendly_fire);

/* ---------- prototypes/INFECTION.C (port/linux/game/infection.c) */

/* Infection's defaults, and its settings brought into their ranges */
void infection_settings_default(
	struct infection_settings *settings);
void infection_settings_clamp(
	struct infection_settings *settings);
/* whether a variant is an Infection game type (a Slayer one with its magic) */
boolean infection_variant(
	struct game_variant const *variant);
/* the built-in Infection game type (its settings the defaults, or this
machine's infection.* settings: infection_settings_configured) */
struct game_variant *build_game_variant_infection(
	struct game_variant *variant);
/* the engine an Infection variant plays: Slayer's, Infection's rules on top
(game_engine_initialize) */
struct game_engine *infection_engine_get(
	void);
/* whether the game played is Infection, and a player's side in it */
boolean infection_active(
	void);
boolean infection_player_is_zombie(
	long player_index);
/* game_engine.c's hooks */
void infection_postspawn_player_update(
	long player_index);
boolean infection_should_end_game(
	void);
boolean infection_title_string(
	long player_index,
	wchar_t *title_string,
	long maximum_count);
boolean infection_change_color(
	long player_index,
	union real_rgb_color *color);
long infection_time_limit_minutes(
	void);
/* units.c: a zombie takes no grenades */
boolean infection_unit_refuses_grenades(
	long unit_index);
/* the distributed netcode: Infection's part of the game type's state, after
Slayer's (game_engine_write_network_state): its size, and a client's read */
long infection_network_state_size(
	void);
long infection_write_network_state(
	byte *buffer,
	long size);
boolean infection_read_network_state(
	byte const *buffer,
	long size,
	boolean first);

/* ---------- globals */

extern struct game_engine *game_engine;

/* ---------- public code */

real game_engine_get_damage_multiplier(
	long damaging_player_index,
	long damaged_player_index);

#endif // __GAME_ENGINE_H
