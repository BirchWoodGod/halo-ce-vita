/*
NETWORK_TEST.C

Automated system link sessions for testing the netcode without the menus
(debug.network_test in config.toml, HALO_NETWORK_TEST):

- "host:<map>[:<variant>[,<variant>...]]" hosts a game on that
  multiplayer map (bloodgulch, ...) with one of the built-in game variants
  (slayer by default; game_engine_get_variant_by_name's, invasion, rockets,
  snipers: network_test_variant_by_name), as the pregame
  screen's fast setup does, and starts it debug.network_test_start seconds
  later; with more variants, once a game is over (debug.network_test_score
  makes it short) the next, as the host's button on the scores does
  ("<variant>@<map>" plays that game on another map: a map change;
  "<variant>+vehicles" the variant with every vehicle the map places;
  "saved=<name>" the player's saved game type of that name, the Halo 3
  game type editor's, its rules logged once the game runs); a
  map with a path is that level (custom_maps\pcgulch: a Custom Edition map
  named as a PC host names it, OpenCE's, for the crossplay wire's map
  checks: port/vita/tests/run_crossplay_maps_test.sh);
- "local:<map>[:<variant>...]" the same with a local (split screen) game
  of one player, which starts only where a local game may have one (the
  Vita's rules, HALO_PORT_VITA_NETWORK);
  debug.network_test_local_after: a "host:" machine hosts its game's lobby
  for that many seconds (never starting it), then goes back as the main menu
  does and plays a local game on the map instead, as a player who hosted
  and then picked Split Screen does (internet play must host nothing of the
  local game: p2p.c); debug.network_test_invite_file: a hosting machine
  writes its invite link to that file of the data folder once it hosts for
  internet play (a test's joiner opens it later, as a link kept from
  before);
- "join" searches for games and joins the first it finds, as picking it in
  the system link list does;
- "join-public" first browses internet play's public games (the server
  browser, p2p_lobby.c) and joins the first game listed there (not this
  machine's) by its listing, then searches as "join" does: the host's game
  shows in the list once the tunnel reaches it ("join-public:PASSWORD": a
  locked game, with that password; a game listed with a password is joined
  only so). "join-code:ABCD-EFGH" joins that code the same way;
  debug.network_test_rejoin has a joining machine leave the game that many
  seconds in and join again, once; debug.network_test_join_file has it
  wait, the game found, until that file is there.

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
debug.network_test_end (the host ends the game that many seconds in, as a
time limit does: any game type's end, its scores and carnage report),
debug.network_test_shoot (every so often each machine's player hits the
next with their weapon's projectile: a client's through its report to the
host) and debug.network_test_vehicle (the host seats the last player as a
vehicle's driver that many seconds in, takes them out 15 seconds on, and 5
seconds later stands them at a vehicle's driver's entrance, where a joining
machine's player holds the action button, as getting in does)
and debug.network_test_pickup (the last player - the first with
HALO_NETWORK_TEST_PICKUP_PLAYER=first - stands on a weapon lying
about that many seconds in, and a joining machine's player holds the action
button a second later, to pick it up). HALO_NETWORK_TEST_PLAYER_DRIVES
(seconds; a "local:" game) seats this machine's player as the nearest
Warthog's driver (one no one drives, within 30 units) that many seconds in, the scripted player letting go of
everything for 20 seconds (the vehicle stops: the offline bots get in beside
it, run_bots_test.sh), then driving it about as it walks;
HALO_NETWORK_TEST_PLAYER_VEHICLE names another (a part of its tag's name:
"banshee", run_mcc_pc_maps_test.sh), the nearest anywhere on the map.

Called from the main loop every frame (main.c).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "main/main.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "saved games/playlist_profile.h"
#include "saved games/saved_game_files.h"
#include "interface/ui_widget.h"
#include "networking/network_game_globals.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_engine_playlist.h"
#include "game/players.h"
#include "game/player_control.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "items/projectile_definitions.h"
#include "items/items.h"
#include "objects/damage.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_files.h"
#include "camera/observer.h"
#include "cutscene/cinematics.h"
#include "cache/cache_files.h"

/* debug.network_test_leave_lobby: the map precached once the lobby is left,
and the client's state in a lobby (network_client_message_handler.c's
_network_game_client_state_pregame) */
#define NETWORK_TEST_CLIENT_STATE_PREGAME 2
#define LEAVE_LOBBY_OTHER_MAP "levels\\test\\bloodgulch\\bloodgulch"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* internet play's codes and lobby (port/linux/src/p2p.c) */
#include "../src/p2p.h"
#include "../src/p2p_friends.h"
/* the crossplay guardrails (docs/crossplay.md 6, T11) */
#include "../src/crossplay_guard.h"
#include "network_wire.h"
#include "forge.h"

/* the platform layer's (port/linux/src/port_config.c) */
const char *config_string(char const *name);
double config_real(char const *name);
long config_integer(char const *name);
void platform_log(char const *format, ...);
/* menu_functions.c's: a variant's settings, as the Halo 3 game type
editor's log has them */
void menu_functions_gametype_rules(struct game_variant const *variant, char *text, int size);
boolean menu_functions_gametype_file_good(long profile_index);
/* damage.c's */
void damage_kill_object_for_player(long object_index, long player_index);
/* network_distributed.c's */
void network_distributed_statistics(long *sent, long *received, long *corrections);
void network_distributed_byte_statistics(long *sent_bytes, long *received_bytes);
void network_distributed_item_statistics(long *creates, long *deletes, long *failures, long *removed);
void network_damage_statistics(long *sent_reports, long *dealt_reports, long *rejected_reports, long *replayed_events);
/* network_actors.c's */
void network_actors_client_statistics(long *tracked, long *most, long *untracked, long *rejected);
/* xinput_sdl.c's */
void test_input_hold_action(int hold);
void test_input_hold_fire(int hold);
void test_input_hold_back(int hold);
void test_input_hold_still(int hold);
/* game_engine.c's */
long game_engine_in_game_score_draw_count(void);
void game_engine_in_game_score_ping_counts(long *other_pings, long *unknown_pings);
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
	/* debug.network_test_local_after: the seconds the hosted game's lobby
	lasts before a local game takes its place, and how long it has; and
	debug.network_test_invite_file, written once */
	real local_after;
	real hosted_seconds;
	char invite_file[256];
	boolean invite_written;
	/* debug.network_test_join_file: a joining machine joins the game it
	found once this file is there (the test's moment: a dedicated server's
	scores, run_netns_online_test.sh dedicatedreload), logged once */
	char join_file[256];
	boolean join_file_logged;
	char map_name[64];
	char variant_name[64];
	/* ("saved=<name>": the game time the game's rules were last logged at,
	so that each game's are logged once) */
	long rules_logged_time;
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
	/* debug.network_test_end: the seconds a game lasts (0: its own end) */
	real end_seconds;
	/* debug.network_test_kill: the game time of the last scripted kill (0:
	none yet: the game's start), the game time last seen (a smaller one: the
	next game), and whether the first player has had this game's grenades */
	long kill_time;
	long kill_seen_time;
	boolean kill_grenades_given;
	real shoot_interval;
	real vehicle_time;
	real pickup_time;
	/* debug.network_test_scores: every so often the scoreboard held open
	(Back), and the times it was */
	real scores_interval;
	boolean scores_held;
	long scores_shown;
	char pickup_weapon[64];
	long score_to_win;
	long logged_time;
	/* join-public, join-code: the code to join (empty: browse for a public
	game, joined by its listing's id, with the password given) */
	boolean by_code;
	boolean code_joined;
	/* join-friend: a friend's joinable game, joined from their presence
	(p2p_friends.h); join-invite: a friend's invite, accepted at the menus
	(friends_game.c, HALO_TEST_FRIENDS_ANSWER), then searched for as a code's
	host is */
	boolean by_friend;
	boolean by_invite;
	boolean invite_taken;
	char code[P2P_CODE_SIZE];
	char lobby_id[P2P_LOBBY_ID_SIZE];
	char lobby_password[P2P_LOBBY_PASSWORD_SIZE];
	/* join-public: only the game listed with this name (debug.network_test_public_name;
	empty: the first) */
	char lobby_name[64];
	boolean lobby_join_asked, lobby_locked;
	real browse_seconds;
	/* debug.network_test_rejoin: a joining machine leaves the game that many
	seconds in, and joins again (once) */
	real rejoin_time;
	real ingame_seconds;
	boolean left;
	boolean rejoined;
	/* debug.network_test_leave_lobby: a joining machine leaves the host's
	lobby that many seconds after joining it, as Back does, and precaches
	another map (as the next lobby's would), then joins again (once): a map
	copy under way is let go of (cache_copy_end) */
	real leave_lobby_time;
	real lobby_seconds;
	boolean lobby_left;
	/* debug.network_test_retry: the joins left to try after one that ended
	before its game began, and the seconds since it ended */
	long retries;
	real retry_seconds;
	/* debug.network_test_second_player: the seconds after joining a second
	local player is asked for, and whether it was */
	real second_player_time;
	real second_player_seconds;
	boolean second_player_asked;
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

/* a built-in game variant by its name: the game's own names
(game_engine_get_variant_by_name), and the game list's others it does not
know (the playlist's builders, game_engine_playlist.h): Invasion (CTF with
the flags at the other team's base), Rockets, Snipers, Team Rally, ... */
static void network_test_variant_by_name(struct game_variant *variant, char const *name)
{
	static struct
	{
		char const *name;
		struct game_variant *(*build)(struct game_variant *variant);
	} const others[] =
	{
		{ "invasion", build_game_variant_invasion },
		{ "rockets", build_game_variant_rockets },
		{ "snipers", build_game_variant_snipers },
		{ "ctf_pro", build_game_variant_ctf_pro },
		{ "team_rally", build_game_variant_team_rally },
		{ "crazy_king", build_game_variant_crazy_king },
		{ "juggernaut", build_game_variant_juggernaut },
	};
	size_t index;
	/* "<variant>+vehicles": the variant with every vehicle the map places
	(the default vehicle set, as the dedicated server's Big Team has it:
	game_engine_make_big_team_variant; the game's own sets place Warthogs
	alone), for the offline bots' vehicles (run_bots_test.sh) */
	char base[64];
	size_t length = strlen(name);
	boolean all_vehicles = length > 9 && !strcmp(name + length - 9, "+vehicles");

	if (all_vehicles)
	{
		snprintf(base, sizeof(base), "%.*s", (int)(length - 9), name);
		name = base;
	}
	/* ("forge": a network Forge game, forge.c's variant; the host's game
made one by network_test_forge once its map is set) */
	if (!strcmp(name, "forge"))
	{
		forge_variant_build(variant);
		return;
	}
	/* ("saved=<name>": the player's saved game type of that name - the
	Halo 3 game type editor's, z:\saved\playlists -, read as the lobby's
	GAME reads it; none, Slayer) */
	if (!strncmp(name, "saved=", 6))
	{
		long indices[100];
		word count = NUMBEROF(indices);
		word index;

		playlist_profiles_enumerate_available_to_local_player_index(0, &count, indices);
		for (index = 0; index < count; index++)
		{
			wchar_t display[128];
			char text[128];
			char same[128];
			char wanted[128];
			int length;

			if (indices[index] == NONE || !playlist_profile_get_display_name(indices[index], display))
				continue;
			for (length = 0; display[length] && length < (int)sizeof(text) - 1; length++)
				text[length] = (char)(display[length] < 0x80 ? display[length] : '?');
			text[length] = 0;
			/* ('_' for a space: a name in a test's space-split settings) */
			snprintf(wanted, sizeof(wanted), "%s", name + 6);
			snprintf(same, sizeof(same), "%s", text);
			for (length = 0; same[length]; length++)
			{
				if (same[length] == ' ')
					same[length] = '_';
			}
			for (length = 0; wanted[length]; length++)
			{
				if (wanted[length] == ' ')
					wanted[length] = '_';
			}
			if (_stricmp(same, wanted))
				continue;
			/* (a file damaged or edited since: not played, as the lobby's GAME leaves it out) */
			if (!TEST_FLAG(indices[index], _saved_game_file_index_read_only_bit) &&
				!menu_functions_gametype_file_good(indices[index]))
			{
				platform_log("network test: the saved game type %s is damaged: slayer", text);
				break;
			}
			if (playlist_profile_get(indices[index], variant))
			{
				platform_log("network test: the saved game type %s", text);
				return;
			}
		}
		platform_log("network test: no saved game type %s: slayer", name + 6);
		build_game_variant_slayer(variant);
		return;
	}
	for (index = 0; index < NUMBEROF(others); index++)
	{
		if (!strcmp(name, others[index].name))
		{
			others[index].build(variant);
			break;
		}
	}
	if (index == NUMBEROF(others))
		game_engine_get_variant_by_name(variant, name);
	if (all_vehicles)
		variant->universal_variant.vehicle_set = 0;
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
	else if (!strncmp(setting, "join-friend:", 12))
	{
		/* (join-friend:<password>: a friend's game with a password, typed) */
		network_test.mode = _network_test_join;
		network_test.by_code = TRUE;
		network_test.by_friend = TRUE;
		snprintf(network_test.lobby_password, sizeof(network_test.lobby_password), "%s", setting + 12);
	}
	else if (!strcmp(setting, "join-friend") || !strcmp(setting, "join-invite"))
	{
		network_test.mode = _network_test_join;
		network_test.by_code = TRUE;
		network_test.by_friend = setting[5] == 'f';
		network_test.by_invite = setting[5] == 'i';
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
	network_test.end_seconds = (real)config_real("debug.network_test_end");
	network_test.shoot_interval = (real)config_real("debug.network_test_shoot");
	network_test.vehicle_time = (real)config_real("debug.network_test_vehicle");
	network_test.pickup_time = (real)config_real("debug.network_test_pickup");
	network_test.scores_interval = (real)config_real("debug.network_test_scores");
	snprintf(network_test.pickup_weapon, sizeof(network_test.pickup_weapon), "%s",
		config_string("debug.network_test_pickup_weapon"));
	network_test.score_to_win = (long)config_integer("debug.network_test_score");
	network_test.rejoin_time = (real)config_real("debug.network_test_rejoin");
	network_test.leave_lobby_time = (real)config_real("debug.network_test_leave_lobby");
	network_test.retries = (long)config_integer("debug.network_test_retry");
	network_test.second_player_time = (real)config_real("debug.network_test_second_player");
	snprintf(network_test.lobby_name, sizeof(network_test.lobby_name), "%s",
		config_string("debug.network_test_public_name"));
	network_test.local_after = network_test.local ? 0.0f : (real)config_real("debug.network_test_local_after");
	snprintf(network_test.invite_file, sizeof(network_test.invite_file), "%s",
		config_string("debug.network_test_invite_file"));
	snprintf(network_test.join_file, sizeof(network_test.join_file), "%s",
		config_string("debug.network_test_join_file"));
	if (network_test.mode != _network_test_off)
		platform_log("network test: %s", setting);
}

/* debug.network_test_join_file: whether the test has made it (checked
twice a second) */
/* the System Link list as the LAN browsers show it, each game's tag
(crossplay_guard.h: VITA, VITA+PC, OPENCE, CHUPA, PC) with it, as it
changes: the tests read it (run_netns_crossplay_test.sh browserboth) */
static void network_test_log_listed(
	void)
{
	static char said[512];
	struct network_listed_game listed[16];
	char line[512];
	short count = network_game_client_listed_games(listed, (short)NUMBEROF(listed));
	short index;
	int length = 0;

	line[0] = 0;
	for (index = 0; index < count && length < (int)sizeof(line) - 1; index++)
	{
		char badge[48];

		crossplay_guard_host_badge(badge, (int)sizeof(badge), listed[index].platform, listed[index].marks,
			listed[index].crossplay, 0, CROSSPLAY_GUARD_OK, network_wire_this_platform() == NETWORK_PLATFORM_VITA_PC);
		length += snprintf(line + length, sizeof(line) - (size_t)length, "%s[%s] \"%s\"", index ? ", " : "", badge,
			listed[index].name);
	}
	if (count && strcmp(line, said))
	{
		snprintf(said, sizeof(said), "%s", line);
		platform_log("network test: the System Link list: %s", line);
	}
}

static boolean network_test_join_file_there(
	void)
{
	static unsigned long checked;
	static boolean there;
	unsigned long now = system_milliseconds();
	FILE *file;

	if (there || (checked && now - checked < 500))
		return there;
	checked = now ? now : 1;
	file = fopen(network_test.join_file, "r");
	if (file)
	{
		fclose(file);
		there = TRUE;
		platform_log("network test: %s is there: joining", network_test.join_file);
	}
	else if (!network_test.join_file_logged)
	{
		network_test.join_file_logged = TRUE;
		platform_log("network test: waiting for %s to join", network_test.join_file);
	}
	return there;
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

/* HALO_NETWORK_TEST_VARIANT_OPTIONS: the gametype's PC options the host
sends on the crossplay wire (network_game_record.c), as a PC host's would:
"key=value,..." of time_limit (minutes), friendly_fire, friendly_fire_penalty,
vehicle_respawn_time, auto_team_balance, radar_players, vehicle_set
(red:blue, 254 PC, 255 custom), counts_red and counts_blue (six counts,
by dots: warthog.ghost.scorpion.rocket warthog.banshee.turret), loadout,
primary_weapon, secondary_weapon, no_map_weapons; the rest the variant's
defaults (game_engine.h's enums give the values) */
static void network_test_variant_options(
	struct game_variant const *variant)
{
	char const *text = getenv("HALO_NETWORK_TEST_VARIANT_OPTIONS");
	struct game_variant_options options;
	char copy[512];
	char *item;
	char *rest;

	if (!text || !text[0] || !global_network_game_server_get())
		return;
	game_variant_options_default(variant, &options);
	snprintf(copy, sizeof(copy), "%s", text);
	for (item = strtok_r(copy, ",", &rest); item; item = strtok_r(NULL, ",", &rest))
	{
		char *value = strchr(item, '=');
		long number;

		if (!value)
			continue;
		*value++ = 0;
		number = strtol(value, NULL, 0);
		if (!strcmp(item, "time_limit")) options.time_limit = (short)number;
		else if (!strcmp(item, "friendly_fire")) options.friendly_fire = (short)number;
		else if (!strcmp(item, "friendly_fire_penalty")) options.friendly_fire_penalty = (short)number;
		else if (!strcmp(item, "vehicle_respawn_time")) options.vehicle_respawn_time = (short)number;
		else if (!strcmp(item, "auto_team_balance")) options.auto_team_balance = (boolean)number;
		else if (!strcmp(item, "radar_players")) options.radar_players = (byte)number;
		else if (!strcmp(item, "loadout")) options.loadout = (byte)number;
		else if (!strcmp(item, "primary_weapon")) options.primary_weapon = (byte)number;
		else if (!strcmp(item, "secondary_weapon")) options.secondary_weapon = (byte)number;
		else if (!strcmp(item, "no_map_weapons")) options.no_map_weapons = (boolean)number;
		else if (!strcmp(item, "vehicle_set"))
		{
			char *colon = strchr(value, ':');

			options.vehicle_set[0] = (byte)number;
			options.vehicle_set[1] = (byte)(colon ? strtol(colon + 1, NULL, 0) : number);
		}
		else if (!strcmp(item, "counts_red") || !strcmp(item, "counts_blue"))
		{
			short side = !strcmp(item, "counts_blue");
			short type;
			char *cursor = value;

			for (type = 0; type < NUMBER_OF_VARIANT_VEHICLES && *cursor; type++)
			{
				options.vehicle_counts[side][type] = (byte)strtol(cursor, &cursor, 0);
				if (*cursor == '.')
					cursor++;
			}
		}
	}
	platform_log("network test: the gametype's PC options from HALO_NETWORK_TEST_VARIANT_OPTIONS%s",
		game_variant_options_valid(&options) ? "" : " are out of range: not sent");
	network_game_server_port_set_variant_options(global_network_game_server_get(), &options);
}

/* every ten seconds of a game, the weapons and equipment this machine has,
by kind (tag name and index: the players' lines name the weapons they carry
by index), lying about and carried: what a game type gives (the dedicated
server's SWAT: magnums alone, no grenades) as a machine sees it */
static void network_test_log_items(
	void)
{
	enum
	{
		MAXIMUM_KINDS = 32,
	};
	static long logged_time = -1;
	struct
	{
		long definition_index;
		long lying;
		long carried;
	} kinds[MAXIMUM_KINDS];
	long kind_count = 0;
	struct object_iterator iterator;
	char line[2048];
	int length = 0;
	long index;

	if (logged_time >= 0 && game_time_get() >= logged_time && game_time_get() - logged_time < 10 * TICKS_PER_SECOND)
		return;
	logged_time = game_time_get();
	object_iterator_new(&iterator, _object_mask_weapon | _object_mask_equipment, 0);
	while (object_iterator_next(&iterator))
	{
		struct item_datum *item = item_get(iterator.index);

		for (index = 0; index < kind_count && kinds[index].definition_index != item->definition_index; index++)
			;
		if (index == kind_count)
		{
			if (kind_count == MAXIMUM_KINDS)
				continue;
			kinds[kind_count].definition_index = item->definition_index;
			kinds[kind_count].lying = kinds[kind_count].carried = 0;
			kind_count++;
		}
		if (item->object.parent_object_index == NONE &&
			TEST_FLAG(item->object.flags, _object_connected_to_map_bit) &&
			!TEST_FLAG(item->item.flags, _item_attached_to_unit_bit))
		{
			kinds[index].lying++;
		}
		else
			kinds[index].carried++;
	}
	line[0] = 0;
	for (index = 0; index < kind_count; index++)
	{
		network_test_append(line, (int)sizeof(line), &length, " %s(%lx) %ld+%ld",
			tag_get_name(kinds[index].definition_index), (unsigned long)kinds[index].definition_index & 0xFFFF,
			kinds[index].lying, kinds[index].carried);
	}
	platform_log("network test: items at tick %ld (lying+carried):%s", game_time_get(), kind_count ? line : " none");
	/* (and the vehicles, by kind) */
	kind_count = 0;
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);

		for (index = 0; index < kind_count && kinds[index].definition_index != object->definition_index; index++)
			;
		if (index == kind_count)
		{
			if (kind_count == MAXIMUM_KINDS)
				continue;
			kinds[kind_count].definition_index = object->definition_index;
			kinds[kind_count].lying = 0;
			kind_count++;
		}
		kinds[index].lying++;
	}
	length = 0;
	line[0] = 0;
	for (index = 0; index < kind_count; index++)
	{
		network_test_append(line, (int)sizeof(line), &length, " %s(%lx) %ld",
			tag_get_name(kinds[index].definition_index), (unsigned long)kinds[index].definition_index & 0xFFFF,
			kinds[index].lying);
	}
	platform_log("network test: vehicles at tick %ld:%s", game_time_get(), kind_count ? line : " none");
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

		long objects = 0, units = 0, ai_units = 0;
		long tracked, most, untracked, rejected;
		struct object_iterator iterator;
		struct object_datum *object;

		network_coop_client_pace_statistics(&holds, &most_lead);
		/* (the census: this machine's objects, its units and those of them
		AI's, which a client has as its host's; the AI units it tracks to
		drive, the most this game, those it had no room for and the states it
		refused: docs/crossplay.md 6.2) */
		object_iterator_new(&iterator, _object_mask_all, 0);
		while ((object = object_iterator_next(&iterator)) != NULL)
		{
			objects++;
			if (TEST_FLAG(_object_mask_unit, object->object.type))
			{
				units++;
				if (unit_get(iterator.index)->unit.player_index == NONE &&
					!TEST_FLAG(object->object.damage_flags, _object_dead_bit))
				{
					ai_units++;
				}
			}
		}
		network_actors_client_statistics(&tracked, &most, &untracked, &rejected);
		platform_log("network test: co-op tick %ld | bsp %d | bsps %d | cinematic %d skip %s %d/%d%s | watching %lx | held %ld"
			" lead %ld | objects %ld units %ld living ai %ld | ai tracked %ld most %ld untracked %ld rejected %ld",
			game_time_get(), (int)global_structure_bsp_index_get(),
			(int)global_scenario_get()->structure_bsp_references.count, (int)cinematic_in_progress(),
			offered ? "offered" : "no", (int)votes, (int)voters, voted ? " voted" : "", (unsigned long)watched,
			holds, most_lead, objects, units, ai_units, tracked, most, untracked, rejected);
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
		/* (no shot from a weapon whose first trigger's magazine has no
		rounds: a player's could not be fired, and the host refuses its
		hits) */
		if (weapon->weapon.triggers.count > 0)
		{
			struct weapon_trigger_definition *first = TAG_BLOCK_GET_ELEMENT(&weapon->weapon.triggers, 0,
				struct weapon_trigger_definition);
			struct weapon_datum *datum = (struct weapon_datum *)object_get(weapon_index);

			if (first->rounds_per_shot > 0 && first->magazine_index >= 0 &&
				first->magazine_index < weapon->weapon.magazines.count &&
				first->magazine_index < (short)NUMBEROF(datum->weapon.magazines) &&
				datum->weapon.magazines[first->magazine_index].rounds_loaded <= 0 &&
				datum->weapon.magazines[first->magazine_index].rounds_total <= 0)
			{
				continue;
			}
		}
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
		/* (a shot that this machine sees no line to - a hill, a wall between -
		is not one a player could land: not made, so that the host's check of
		a shot's line of sight, network_damage.c, sees honest shots alone) */
		if (!melee)
		{
			real_point3d eye;
			real_vector3d line;

			unit_get_camera_position(player->unit_index, &eye);
			line.i = target_object->object.bounding_sphere_center.x - eye.x;
			line.j = target_object->object.bounding_sphere_center.y - eye.y;
			line.k = target_object->object.bounding_sphere_center.z - eye.z;
			if (collision_test_vector_obstructed(FLAG(_collision_test_front_facing_surfaces_bit) |
				FLAG(_collision_test_structure_bit), &eye, &line, player->unit_index))
			{
				continue;
			}
		}
		/* (a shot reaches no further than its projectile flies, with a margin
		for how far the target is from where the shot started) */
		if (melee)
			;
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

#ifndef HALO_RELEASE
/* (test builds) HALO_NETWORK_TEST_AIM=1: this machine's players face the
nearest other living player and hold the trigger: an honest player who
fights the whole game, real shots, reloads and ammunition, around corners
and through the open (the host's checks of their hits must take what hit:
network_damage.c).

HALO_NETWORK_TEST_CHEAT=<kind>,... (a joiner): a modified client, for the
host's anti-cheat (network_anticheat.c) to drop and keep out: "damage" its
hits ten times as hard as its weapon deals, once a second; "wall" its hits
on the other player wherever they are, through walls and hills, every tick
it can; "fly" its player held 4 world units above where it would be;
"ammo" its weapon kept full (the host's copy runs dry) and its shots landed
at the weapon's own rate (5 a second at most) whenever it sees the other
player within reach, its trigger pressed for each, as a player firing a
weapon that never empties would: each such hit, made as an honest shot is
(network_test_shoot), one the host refuses only as of an empty weapon (with
the host's HALO_NETWORK_TEST_DRAIN, network_test_drain) */
static struct
{
	boolean read;
	boolean aim;
	boolean damage;
	boolean wall;
	boolean fly;
	boolean ammo;
	boolean ammo_logged;
	boolean holding;
	long fly_unit;
	real_point3d fly_position;
} network_test_cheat;

/* the nearest other living player's unit to this one (NONE: none) */
static long network_test_nearest_enemy(
	long unit_index)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long nearest = NONE;
	real best = 0.0f;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		real dx, dy, dz, distance;

		if (player->unit_index == NONE || player->unit_index == unit_index ||
			TEST_FLAG(object_get(player->unit_index)->object.damage_flags, _object_dead_bit))
		{
			continue;
		}
		dx = object_get(player->unit_index)->object.position.x - object_get(unit_index)->object.position.x;
		dy = object_get(player->unit_index)->object.position.y - object_get(unit_index)->object.position.y;
		dz = object_get(player->unit_index)->object.position.z - object_get(unit_index)->object.position.z;
		distance = dx * dx + dy * dy + dz * dz;
		if (nearest == NONE || distance < best)
		{
			nearest = player->unit_index;
			best = distance;
		}
	}
	return nearest;
}

/* (test builds) HALO_NETWORK_TEST_RAY_BENCH=1: once in a game, the host's
anti-cheat rays timed where its first player stands (network_anticheat.c,
NETCODE.md "A modified client"): 2000 of a move's (0.2 world units, the
map's structure as a shot meets it) and 500 of a shot's (20 world units),
in microseconds a ray, logged ("network test: rays") */
static void network_test_ray_bench(
	void)
{
	static boolean done;
	unsigned long long vita_host_time_us(void);
	unsigned long collision_flags = FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_ignore_two_sided_surfaces_bit) | FLAG(_collision_test_ignore_invisible_surfaces_bit) |
		FLAG(_collision_test_ignore_breakable_surfaces_bit) | FLAG(_collision_test_structure_bit);
	struct data_iterator iterator;
	struct player_datum *player;
	long unit_index = NONE;
	real_point3d center;
	unsigned long long started, short_us, long_us;
	long index, obstructed = 0;

	if (done || !getenv("HALO_NETWORK_TEST_RAY_BENCH") || game_time_get() < 10 * TICKS_PER_SECOND)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL && unit_index == NONE)
		unit_index = player->unit_index;
	if (unit_index == NONE)
		return;
	done = TRUE;
	center = object_get(unit_index)->object.bounding_sphere_center;
	started = vita_host_time_us();
	for (index = 0; index < 2000; index++)
	{
		real angle = (real)index * 0.7f;
		real_vector3d vector;

		vector.i = 0.2f * (real)cos(angle);
		vector.j = 0.2f * (real)sin(angle);
		vector.k = 0.02f * (real)sin(angle * 3.0f);
		obstructed += collision_test_vector_obstructed(collision_flags, &center, &vector, unit_index) != 0;
	}
	short_us = vita_host_time_us() - started;
	started = vita_host_time_us();
	for (index = 0; index < 500; index++)
	{
		real angle = (real)index * 0.7f;
		real_vector3d vector;

		vector.i = 20.0f * (real)cos(angle);
		vector.j = 20.0f * (real)sin(angle);
		vector.k = 1.0f * (real)sin(angle * 3.0f);
		obstructed += collision_test_vector_obstructed(collision_flags, &center, &vector, unit_index) != 0;
	}
	long_us = vita_host_time_us() - started;
	platform_log("network test: rays: a move's (0.2 wu) %.3f us, a shot's (20 wu) %.3f us; %ld of 2500 obstructed",
		(double)short_us / 2000.0, (double)long_us / 500.0, obstructed);
}

/* (each frame in a level) the aim and the cheats */
static void network_test_aim_and_cheat(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	boolean fired = FALSE;
	boolean ammo_shot = FALSE;

	if (!network_test_cheat.read)
	{
		char const *cheats = getenv("HALO_NETWORK_TEST_CHEAT");

		network_test_cheat.read = TRUE;
		network_test_cheat.aim = getenv("HALO_NETWORK_TEST_AIM") && atoi(getenv("HALO_NETWORK_TEST_AIM"));
		if (cheats && game_connection() != _game_connection_network_server)
		{
			network_test_cheat.damage = strstr(cheats, "damage") != NULL;
			network_test_cheat.wall = strstr(cheats, "wall") != NULL;
			network_test_cheat.fly = strstr(cheats, "fly") != NULL;
			network_test_cheat.ammo = strstr(cheats, "ammo") != NULL;
			platform_log("network test: playing as a modified client: %s", cheats);
		}
		network_test_cheat.fly_unit = NONE;
	}
	if (!network_test_cheat.aim && !network_test_cheat.damage && !network_test_cheat.wall && !network_test_cheat.fly &&
		!network_test_cheat.ammo)
	{
		return;
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct unit_datum *unit;
		long target_index;
		long weapon_index;

		if (player->local_player_index == NONE || player->unit_index == NONE ||
			TEST_FLAG(object_get(player->unit_index)->object.damage_flags, _object_dead_bit))
		{
			continue;
		}
		unit = unit_get(player->unit_index);
		target_index = network_test_nearest_enemy(player->unit_index);
		weapon_index = unit->unit.current_weapon_index != NONE ?
			unit->unit.weapon_object_indices[unit->unit.current_weapon_index] : NONE;
		if ((network_test_cheat.aim || network_test_cheat.ammo) && target_index != NONE)
		{
			real_vector3d facing;
			real_point3d from, to;

			unit_get_camera_position(player->unit_index, &from);
			to = object_get(target_index)->object.bounding_sphere_center;
			facing.i = to.x - from.x;
			facing.j = to.y - from.y;
			facing.k = to.z - from.z;
			if (normalize3d(&facing) > 0.0f)
				player_control_set_facing(player->local_player_index, &facing);
			/* (the ammo cheat's trigger: pressed for its shots alone, below,
			so that a weapon that fires once a press fires each, and the
			host's copy runs dry) */
			if (network_test_cheat.aim)
				fired = TRUE;
		}
		/* (a modified client's weapon never empties) */
		if (network_test_cheat.ammo && weapon_index != NONE)
		{
			struct weapon_datum *weapon = (struct weapon_datum *)object_try_and_get_and_verify_type(weapon_index,
				_object_mask_weapon);

			if (weapon)
			{
				struct weapon_definition *definition = weapon_definition_get(weapon->definition_index);
				real rate = 5.0f;
				long interval;

				weapon->weapon.magazines[0].rounds_loaded = MAX(weapon->weapon.magazines[0].rounds_loaded, 30);
				weapon->weapon.age = 0.0f;
				/* (the trigger's own rate, 5 a second at most: the host's
				bucket takes twice a weapon's rate, so these are refused only
				as of an empty weapon) */
				if (definition->weapon.triggers.count > 0)
				{
					struct weapon_trigger_definition *trigger = TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0,
						struct weapon_trigger_definition);
					real most = MAX(trigger->initial_rate_of_fire, trigger->final_rate_of_fire);

					if (most > 0.0f && most < rate)
						rate = most;
				}
				interval = (long)(TICKS_PER_SECOND / rate + 0.999f);
				if (target_index != NONE && interval > 0 && game_time_get() % interval == 0)
				{
					ammo_shot = TRUE;
					fired = TRUE;
				}
				if (!network_test_cheat.ammo_logged)
				{
					network_test_cheat.ammo_logged = TRUE;
					platform_log("network test: the ammo cheat's weapon %s: a shot every %ld ticks",
						tag_get_name(weapon->definition_index) ? tag_get_name(weapon->definition_index) : "?", interval);
				}
			}
		}
		/* (held in the air: put back each frame where the host's corrections
		took it from) */
		if (network_test_cheat.fly && unit->object.parent_object_index == NONE)
		{
			if (network_test_cheat.fly_unit != player->unit_index)
			{
				network_test_cheat.fly_unit = player->unit_index;
				network_test_cheat.fly_position = unit->object.position;
				network_test_cheat.fly_position.z += 4.0f;
			}
			object_set_position(player->unit_index, &network_test_cheat.fly_position, NULL, NULL);
			unit->object.translational_velocity.k = 0.0f;
		}
		/* (hits no honest client reports: through walls, or ten times as hard
		as the weapon deals) */
		if ((network_test_cheat.damage && game_time_get() % TICKS_PER_SECOND == 0) ||
			(network_test_cheat.wall && game_time_get() % 3 == 0))
		{
			if (target_index != NONE && weapon_index != NONE)
			{
				struct weapon_definition *weapon = weapon_definition_get(object_get(weapon_index)->definition_index);
				struct weapon_trigger_definition *trigger = weapon->weapon.triggers.count > 0 ?
					TAG_BLOCK_GET_ELEMENT(&weapon->weapon.triggers, 0, struct weapon_trigger_definition) : NULL;
				long damage_index = trigger && trigger->projectile.index != NONE ?
					projectile_definition_get(trigger->projectile.index)->projectile.impact_damage.index : NONE;

				if (damage_index != NONE)
				{
					struct damage_data damage;
					real_vector3d direction;

					damage_data_new(&damage, damage_index);
					damage.owner_player_index = iterator.datum_index;
					damage.owner_object_index = player->unit_index;
					damage.owner_team_index = unit->object.owner_team_index;
					damage.origin = object_get(target_index)->object.bounding_sphere_center;
					damage.epicenter = damage.origin;
					direction.i = damage.origin.x - unit->object.position.x;
					direction.j = damage.origin.y - unit->object.position.y;
					direction.k = damage.origin.z - unit->object.position.z;
					normalize3d(&direction);
					damage.direction = direction;
					damage.scale = network_test_cheat.damage && game_time_get() % TICKS_PER_SECOND == 0 ? 10.0f : 1.0f;
					scenario_location_from_point(&damage.location, &damage.epicenter);
					object_cause_damage(&damage, target_index, NONE, NONE, NONE, NULL);
				}
			}
		}
	}
	if (fired != network_test_cheat.holding)
	{
		network_test_cheat.holding = fired;
		test_input_hold_fire(fired);
	}
	/* (the ammo cheat's shots: as the scripted shooter's, seen, within
	reach) */
	if (ammo_shot)
		network_test_shoot();
}

/* (test builds, the host) HALO_NETWORK_TEST_DRAIN=1: the host's copies of
its clients' weapons kept dry - no rounds, loaded or not, and a battery
spent -, as those of a client that has fired them all are; an honest
client's then fire no more, and a modified client's ("ammo") hits are its
weapon's that never empties, for the host's empty weapon check
(network_damage.c distributed_weapon_armed) */
static void network_test_drain(
	void)
{
	static boolean logged;
	struct data_iterator iterator;
	struct player_datum *player;

	if (game_connection() != _game_connection_network_server || !getenv("HALO_NETWORK_TEST_DRAIN") ||
		!atoi(getenv("HALO_NETWORK_TEST_DRAIN")))
	{
		return;
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct unit_datum *unit;
		short slot;

		if (player->local_player_index != NONE || player->unit_index == NONE)
			continue;
		unit = unit_get(player->unit_index);
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long weapon_index = unit->unit.weapon_object_indices[slot];
			struct weapon_datum *weapon = weapon_index != NONE ?
				(struct weapon_datum *)object_try_and_get_and_verify_type(weapon_index, _object_mask_weapon) : NULL;
			short magazine;

			if (!weapon)
				continue;
			for (magazine = 0; magazine < (short)NUMBEROF(weapon->weapon.magazines); magazine++)
			{
				weapon->weapon.magazines[magazine].rounds_loaded = 0;
				weapon->weapon.magazines[magazine].rounds_total = 0;
			}
			weapon->weapon.age = 1.0f;
			if (!logged)
			{
				logged = TRUE;
				platform_log("network test: the host keeps its clients' weapons dry");
			}
		}
	}
}
#endif

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
	boolean enter,
	boolean first_player)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *last = NULL;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		/* (HALO_NETWORK_TEST_PLAYER_DRIVES: this machine's first player, the
		Warthog nearest it; "last" names it below) */
		if (first_player && player->local_player_index == NONE)
			continue;
		last = player;
		if (first_player)
			break;
	}
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
	/* (this machine's player in a vehicle already: left be) */
	if (first_player && object_get(last->unit_index)->object.parent_object_index != NONE)
		return;
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

			/* (HALO_NETWORK_TEST_PLAYER_DRIVES: a Warthog no one drives, near:
			not one across the map, at the other team's base; or the vehicle
			HALO_NETWORK_TEST_PLAYER_VEHICLE names, anywhere) */
			if (first_player)
			{
				char const *kind = getenv("HALO_NETWORK_TEST_PLAYER_VEHICLE");
				boolean named = kind && kind[0];

				if (unit_get(vehicles.index)->unit.driver_object_index != NONE ||
					!strstr(tag_get_name(object_get(vehicles.index)->definition_index), named ? kind : "warthog") ||
					(!named && distance > 30.0f * 30.0f))
				{
					continue;
				}
			}
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
				platform_log("network test: the %s player drives vehicle %lx", first_player ? "first" : "last", nearest_index);
				return;
			}
		}
		platform_log("network test: the %s player cannot drive vehicle %lx", first_player ? "first" : "last", nearest_index);
	}
}

/* port: declared, not left implicit: an undeclared call reads its result as a whole
int, where these narrower results fill only part of the register (an x86 build with
link-time optimisation read the rest as garbage: every init.txt command was refused as
"playing in another's game"; the Vita's ARM ABI widens them, but see tools/vita_build.py) */
boolean unit_get_seat_entrance_point(long unit_index, long parent_unit_index, short seat_index, real_point3d *entrance_point, real_point3d *seat_point, real_point3d *hint_point);

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

/* port: declared, not left implicit: an undeclared call reads its result as a whole
int, where these narrower results fill only part of the register (an x86 build with
link-time optimisation read the rest as garbage: every init.txt command was refused as
"playing in another's game"; the Vita's ARM ABI widens them, but see tools/vita_build.py) */
boolean player_handle_powerup(long player_index, short powerup_type, short duration);

/* the host gives the last player a second weapon, lying about */
/* the player network_test_pickup stands on a weapon: the last, or with
HALO_NETWORK_TEST_PICKUP_PLAYER=first the first (a local game's own player
with HALO_BOTS: its HUD shows the prompt, the button icons' screenshots) */
static struct player_datum *network_test_pickup_player(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *last = NULL;
	char const *which = getenv("HALO_NETWORK_TEST_PICKUP_PLAYER");

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		last = player;
		if (which && !strcmp(which, "first"))
			break;
	}

	return last;
}

static void network_test_second_weapon(
	void)
{
	struct player_datum *last = network_test_pickup_player();
	struct object_iterator weapons;

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
	struct player_datum *last = network_test_pickup_player();
	struct object_iterator weapons;
	long nearest_index = NONE;
	real nearest_distance = 0.0f;
	struct unit_datum *unit;

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
		platform_log("network test: the last player stands on weapon %lx (%lx) at (%.3f %.3f %.3f)", nearest_index,
			object_get(nearest_index)->definition_index, position.x, position.y, position.z);
	}
}

/* (debug: HALO_NETWORK_TEST_PROFILE) the first player profile on the
memory card made the local player's, as SELECT PROFILE's choice makes it:
the screens that edit the player's profile in a game (the Halo 3
multiplayer pause's SETTINGS) have one to edit */
static void network_test_use_profile(
	void)
{
	long indices[8];
	word count = (word)NUMBEROF(indices);
	struct player_profile profile;

	if (!getenv("HALO_NETWORK_TEST_PROFILE") || !atoi(getenv("HALO_NETWORK_TEST_PROFILE")))
		return;
	player_profiles_enumerate_available_to_local_player_index(0, &count, indices, FALSE);
	if (count > 0 && player_profile_get(indices[0], &profile))
	{
		player_ui_set_active_player_profile(0, indices[0], &profile);
		platform_log("network test: the first profile played");
	}
	else
		platform_log("network test: no profile to play");
}

/* "watch" with debug.network_test_score, this machine hosting the game its
menus made (the Halo 3 live lobby's tests): a short game as a host's own -
its kills (debug.network_test_kill), that score winning, and after the
scores a while the game back in its lobby */
static boolean network_test_watch_short_game(void)
{
	return network_test.mode == _network_test_watch && network_test.score_to_win > 0 &&
		global_network_game_server_get() != NULL;
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
#ifndef HALO_RELEASE
	if (game_in_progress() && !main_menu_loaded)
	{
		network_test_aim_and_cheat();
		network_test_drain();
		network_test_ray_bench();
	}
#endif
	if (game_in_progress() && !main_menu_loaded && game_time_get() - network_test.logged_time >= TICKS_PER_SECOND)
	{
		network_test.logged_time = game_time_get();
		/* (a saved game type played - or HALO_NETWORK_TEST_RULES=1: a
		joiner's, the host's game type as it came -: the rules the game
		plays by, its engine's variant, logged once a game) */
		if (game_engine_running() && (!strncmp(network_test.variant_name, "saved=", 6) ||
			(getenv("HALO_NETWORK_TEST_RULES") && !strcmp(getenv("HALO_NETWORK_TEST_RULES"), "1"))) &&
			(network_test.rules_logged_time == 0 || game_time_get() < network_test.rules_logged_time))
		{
			char rules[512];

			network_test.rules_logged_time = MAX(game_time_get(), 1);
			menu_functions_gametype_rules(game_engine_get_variant(), rules, sizeof(rules));
			platform_log("network test: the game plays by: %s", rules);
		}
		if (network_test_watch_short_game() && game_engine_running() &&
			game_engine_get_variant()->universal_variant.score_to_win != network_test.score_to_win)
		{
			game_engine_get_variant()->universal_variant.score_to_win = network_test.score_to_win;
			platform_log("network test: the menus' game played to %ld", network_test.score_to_win);
		}
		network_test_log_players();
		network_test_log_items();
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
				network_test_vehicle(TRUE, FALSE);
			if (game_time_get() >= enter_time + 15 * TICKS_PER_SECOND &&
				game_time_get() - enter_time - 15 * TICKS_PER_SECOND < TICKS_PER_SECOND)
			{
				network_test_vehicle(FALSE, FALSE);
			}
		}
		/* HALO_NETWORK_TEST_PLAYER_DRIVES: this machine's player drives a
		Warthog, still for 20 seconds (the offline bots get in beside it) */
		if (network_test.local && getenv("HALO_NETWORK_TEST_PLAYER_DRIVES"))
		{
			long drive_time = (long)(atof(getenv("HALO_NETWORK_TEST_PLAYER_DRIVES")) * TICKS_PER_SECOND);

			if (drive_time > 0 && game_time_get() >= drive_time && game_time_get() - drive_time < TICKS_PER_SECOND)
				network_test_vehicle(TRUE, TRUE);
			test_input_hold_still(drive_time > 0 && game_time_get() >= drive_time &&
				game_time_get() < drive_time + 20 * TICKS_PER_SECOND);
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
		often, to test deaths and respawns reaching the clients, and so that
		a short game (debug.network_test_score) ends: the interval after the
		last kill (or the game's start), at the first second both players
		are alive from then on. (Not only in the one second at each multiple
		of the interval: a player dead then lost the whole interval, and the
		first player's own grenades kill it now and then, a suicide taking
		its point back; three of those in a game and the first game of
		run_netns_online_test.sh code outlasted the run, its map change never
		reached, about one run in five.) */
		if ((network_test.mode == _network_test_host || network_test_watch_short_game()) &&
			network_test.kill_interval > 0.0f && game_time_get() < network_test.kill_seen_time)
		{
			network_test.kill_time = 0;
			network_test.kill_grenades_given = FALSE;
		}
		network_test.kill_seen_time = game_time_get();
		if ((network_test.mode == _network_test_host || network_test_watch_short_game()) &&
			network_test.kill_interval > 0.0f &&
			game_time_get() - network_test.kill_time >= MAX(1, (long)(network_test.kill_interval * TICKS_PER_SECOND)))
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
				network_test.kill_time = game_time_get();
				/* and picks up a weapon lying about, and (once a game: the
				scripted player throws them every seven seconds, and they
				are what kills it) two grenades of each kind */
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
					if (!network_test.kill_grenades_given)
					{
						unit->unit.grenade_counts[0] = 2;
						unit->unit.grenade_counts[1] = 2;
						network_test.kill_grenades_given = TRUE;
					}
				}
			}
		}
	}

	/* debug.network_test_end: the host ends the game, as a time limit does */
	if (network_test.mode == _network_test_host && network_test.started && network_test.end_seconds > 0.0f &&
		game_engine_running() && !main_menu_loaded && game_engine_can_score() &&
		game_time_get() >= (long)(network_test.end_seconds * TICKS_PER_SECOND))
	{
		platform_log("network test: the game ended at %.0f s (debug.network_test_end)", network_test.end_seconds);
		game_engine_end_game();
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
	/* (watch, the menus' short game: the scores a while, then its lobby) */
	if (network_test_watch_short_game() && game_engine_running() && !main_menu_loaded && !game_engine_can_score() &&
		game_engine_showing_postgame())
	{
		network_test.postgame_seconds += seconds;
		if (network_test.postgame_seconds >= 3.0f)
		{
			network_test.postgame_seconds = 0.0f;
			platform_log("network test: the menus' game over: back to its lobby");
			network_game_server_reset_to_pregame(global_network_game_server_get());
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
	/* debug.network_test_scores: the scoreboard held open (Back) for the
	first half of every interval of a game, as a player looking at the
	scores does (issue #36: a crash with it open online), and let go
	outside a game and on its carnage report (where Back leaves for the
	menus) */
	if (network_test.scores_interval > 0.0f)
	{
		long interval = MAX(2, (long)(network_test.scores_interval * TICKS_PER_SECOND));
		boolean hold = game_in_progress() && game_engine_running() && !main_menu_loaded &&
			!game_engine_force_single_screen() && game_time_get() % interval < interval / 2;

		if (hold != network_test.scores_held)
		{
			network_test.scores_held = hold;
			test_input_hold_back(hold);
			if (hold)
			{
				network_test.scores_shown++;
				long other_pings, unknown_pings;

				platform_log("network test: scoreboard held (%ld; drawn in %ld frames so far)", network_test.scores_shown,
					game_engine_in_game_score_draw_count());
				/* (its Ping column: other machines' players' pings, a
				client's from the host's table; and "-") */
				game_engine_in_game_score_ping_counts(&other_pings, &unknown_pings);
				platform_log("network test: scoreboard pings so far: %ld rows with another machine's player's ping, %ld with \"-\"",
					other_pings, unknown_pings);
			}
		}
	}
	/* debug.network_test_leave_lobby: leave the lobby before the game, as
	Back does, precache another map, and join again */
	if (network_test.mode == _network_test_join && network_test.leave_lobby_time > 0.0f && !network_test.lobby_left &&
		network_test.joined && global_network_game_client_get() &&
		network_game_client_get_state(global_network_game_client_get(), NULL) == NETWORK_TEST_CLIENT_STATE_PREGAME)
	{
		network_test.lobby_seconds += seconds;
		if (network_test.lobby_seconds >= network_test.leave_lobby_time)
		{
			network_test.lobby_left = TRUE;
			platform_log("network test: leaving the lobby%s", cache_files_precache_in_progress() ? " in a map copy" : "");
			ui_widgets_close_all();
			dispose_global_network_game_server();
			dispose_global_network_game_client();
			main_set_multiplayer_map_name(LEAVE_LOBBY_OTHER_MAP);
			platform_log("network test: left the lobby; precaching %s", LEAVE_LOBBY_OTHER_MAP);
			network_test.set_up = FALSE;
			network_test.joined = FALSE;
			network_test.player_added = FALSE;
			network_test.team_set = FALSE;
			network_test.code_joined = FALSE;
			network_test.lobby_join_asked = FALSE;
			network_test.lobby_id[0] = 0;
			network_test.joined_seconds = 0.0f;
		}
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

	/* debug.network_test_invite_file: the invite, once hosting (the test
	keeps it for a joiner that opens it later) */
	if (network_test.mode == _network_test_host && network_test.invite_file[0] && !network_test.invite_written)
	{
		char invite[256];

		if (p2p_hosting_invite(invite, sizeof(invite)))
		{
			FILE *file = fopen(network_test.invite_file, "w");

			network_test.invite_written = TRUE;
			if (file)
			{
				fprintf(file, "%s\n", invite);
				fclose(file);
			}
			platform_log("network test: the invite written to %s%s", network_test.invite_file, file ? "" : " (failed)");
		}
	}
	/* debug.network_test_local_after: the hosted lobby given up for a local
	(Split Screen) game, as a player backing out to the main menu and
	picking Split Screen does */
	if (network_test.mode == _network_test_host && network_test.local_after > 0.0f && !network_test.local &&
		network_test.set_up && main_menu_loaded)
	{
		network_test.hosted_seconds += seconds;
		if (network_test.hosted_seconds >= network_test.local_after)
		{
			ui_widgets_close_all();
			dispose_global_network_game_client();
			dispose_global_network_game_server();
			network_game_accept_remote_connections(FALSE);
			network_test.local = TRUE;
			network_test.set_up = FALSE;
			network_test.started = FALSE;
			network_test.map_set = FALSE;
			network_test.map_path[0] = 0;
			network_test.map_checked_seconds = 0.0f;
			network_test.player_added = FALSE;
			network_test.setup_seconds = 0.0f;
			network_test.menu_seconds = 0.0f;
			platform_log("network test: the hosted game given up for a local one");
		}
	}

	/* (a second local player asked for, as a second controller's A on the
	pregame screen or in the game: split screen in a network game; its own
	clock from the join) */
	if (network_test.mode == _network_test_join && network_test.joined && network_test.second_player_time > 0.0f &&
		!network_test.second_player_asked && global_network_game_client_get())
	{
		network_test.second_player_seconds += seconds;
		if (network_test.second_player_seconds >= network_test.second_player_time)
		{
			network_test.second_player_asked = TRUE;
			platform_log("network test: asking for a second local player (controller 2): %s",
				network_game_client_add_player(global_network_game_client_get(), 1) ? "asked or held" : "failed");
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
			network_test_use_profile();
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
				/* (debug.network_test_local_after: its lobby kept, a joiner's
				countdown held, as Create Game's is) */
				if (global_network_game_server_get() && network_test.local_after > 0.0f)
					network_game_server_pause_countdown(global_network_game_server_get(), TRUE);
				if (global_network_game_server_get())
					platform_log("network test: hosting %s", network_test.map_name);
				else
				{
					/* (not hosted: the game's ports taken, by another program
					or another copy of the game; tried again in 5 s, as a
					player would, from the main menu it went back to) */
					platform_log("network test: could not host %s; trying again in 5 s", network_test.map_name);
					network_test.set_up = FALSE;
					network_test.menu_seconds = -3.0f;
				}
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
				/* (a map with a path is that level, as OpenCE's network test has
				it: custom_maps\pcgulch names a Custom Edition map as a PC host
				does, the crossplay wire's stand-in for one, crossplay_maps.h) */
				if (strchr(map_name, '\\'))
					snprintf(path, sizeof(path), "%s", map_name);
				else
					snprintf(path, sizeof(path), "levels\\test\\%s\\%s", map_name, map_name);
				network_game_server_change_map_name(global_network_game_server_get(), path);
				snprintf(network_test.map_path, sizeof(network_test.map_path), "%s", path);
				platform_log("network test: map %s", map_name);
				/* the variant, as picking the game settings does */
				{
					char variant_name[64];

					network_test_variant(network_test.variant_index, variant_name, sizeof(variant_name));
					network_test_variant_by_name(&variant, variant_name);
					platform_log("network test: game %d, %s", network_test.variant_index + 1, variant_name);
				}
				/* debug.network_test_score: a short game, to test the next */
				if (network_test.score_to_win > 0)
					variant.universal_variant.score_to_win = network_test.score_to_win;
				player_ui_set_game_variant(&variant);
				network_game_server_change_game_variant(global_network_game_server_get(), &variant);
				network_test_variant_options(&variant);
				network_test.map_set = TRUE;
				/* ("forge": the hosted game a network Forge game of the map,
				HALO_NETWORK_TEST_FORGE_VARIANT its map variant) */
				{
					char variant_name[64];

					network_test_variant(network_test.variant_index, variant_name, sizeof(variant_name));
					if (!strcmp(variant_name, "forge"))
					{
						char why[160];
						char const *variant_file = getenv("HALO_NETWORK_TEST_FORGE_VARIANT");

						if (!forge_net_host_begin(path, variant_file, why, (int)sizeof(why)))
							platform_log("network test: the Forge game could not be hosted: %s", why);
					}
					else
					{
						forge_net_host_end();
					}
				}
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
				network_test.player_added = network_game_client_add_player(global_network_game_client_get(), 0);
			/* (debug.network_test_local_after: the hosted game's lobby is
			never started, the local game is) */
			if (network_test.setup_seconds >= network_test.start_delay &&
				(network_test.local || network_test.local_after <= 0.0f))
			{
				network_test.started = TRUE;
				network_game_client_request_immediate_start();
				platform_log("network test: starting the game");
			}
		}
		break;
	case _network_test_join:
		/* (join-invite: at the menus until the invite's host is reached) */
		if (network_test.by_invite && !network_test.invite_taken)
		{
			unsigned char host[6];

			if (!main_menu_loaded || !p2p_joined_host(host, FALSE))
				break;
			network_test.invite_taken = TRUE;
			network_test.code_joined = TRUE;
			platform_log("network test: the friend's invite accepted and its host reached; searching");
		}
		if (!network_test.set_up)
		{
			network_test.set_up = TRUE;
			network_test_use_profile();
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
			/* (join-friend: the first friend whose game is joinable, once a second) */
			if (network_test.by_friend)
			{
				if (network_test.browse_seconds >= 1.0f)
				{
					struct p2p_friend_entry entries[P2P_FRIEND_ENTRIES_MAXIMUM];
					int count = p2p_friends_list(entries, P2P_FRIEND_ENTRIES_MAXIMUM), index;

					network_test.browse_seconds = 0.0f;
					for (index = 0; index < count; index++)
					{
						int outcome;

						if (entries[index].kind != P2P_FRIEND_KIND_FRIEND || !entries[index].joinable)
							continue;
						outcome = p2p_friends_act(entries[index].id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_JOIN);
						platform_log("network test: joining friend %s (%s): %s", entries[index].name, entries[index].activity,
							p2p_friends_outcome_text(outcome));
						/* (a game with a password: asked for it, the test's typed) */
						if (outcome == P2P_FRIENDS_PASSWORD && network_test.lobby_password[0])
						{
							outcome = p2p_friends_join_password(entries[index].id, network_test.lobby_password);
							platform_log("network test: the friend's game's password typed: %s",
								p2p_friends_outcome_text(outcome));
						}
						network_test.code_joined = outcome == P2P_FRIENDS_DONE;
						break;
					}
				}
			}
			else if (!network_test.code[0] && network_test.browse_seconds >= 1.0f)
			{
				struct p2p_lobby_entry entry;
				int index;

				network_test.browse_seconds = 0.0f;
				p2p_lobby_browse(TRUE);
				/* (every row the browser shows, once each as it changes: its tag
				and badges, as the browsers draw them - the tests read them:
				run_netns_crossplay_test.sh browserboth) */
				for (index = 0; p2p_lobby_entry(index, &entry); index++)
				{
					static char said[16][P2P_LOBBY_ID_SIZE + 64];
					char line[P2P_LOBBY_ID_SIZE + 64];
					int slot;

					snprintf(line, sizeof(line), "%s %s %s", entry.id, entry.tag, entry.badge);
					for (slot = 0; slot < 16 && strcmp(said[slot], line); slot++)
						;
					if (slot < 16)
						continue;
					memmove(said[1], said[0], sizeof(said[0]) * 15);
					snprintf(said[0], sizeof(said[0]), "%s", line);
					platform_log("network test: browser row [%s] \"%s\" (%d/%d, %s, %s)", entry.badge, entry.name,
						entry.players, entry.maximum, entry.map, entry.crossplay ? "crossplay wire" : "the Vitas' wire");
				}
				for (index = 0; !network_test.lobby_id[0] && p2p_lobby_entry(index, &entry); index++)
				{
					if (entry.compatible && !entry.own &&
						(!network_test.lobby_name[0] || !strcmp(entry.name, network_test.lobby_name) ||
						/* ("tag:<tag>": the first game of that tag, crossplay_guard.h's -
						a PC host's name is its own: run_netns_crossplay_test.sh) */
						(!strncmp(network_test.lobby_name, "tag:", 4) && !strcmp(entry.tag, network_test.lobby_name + 4))))
					{
						platform_log("network test: the public games list \"%s\"%s%s%s%s: %s; %s", entry.name,
							entry.locked ? " [pw]" : "", entry.dedicated ? " [dedicated]" : "",
							entry.badge[0] ? " badge " : "", entry.badge, entry.rules, entry.players_line);
						/* (as the browsers do, crossplay_guard.h: a large or co-op
						PC game asked about once, the answer
						HALO_CROSSPLAY_LARGE_ANSWER's) */
						if (entry.platform != CROSSPLAY_PLATFORM_VITA && (entry.experimental || entry.coop) &&
							crossplay_guard_answered(entry.name, system_milliseconds()) < 0)
						{
							int answer = crossplay_guard_test_answer();

							platform_log("crossplay: asking about the %s PC game %s (%d of %d players): %s",
								entry.coop ? "co-op" : "large", entry.name, entry.players, entry.maximum,
								answer > 0 ? "join anyway" : "no");
							crossplay_guard_answer(entry.name, answer > 0, system_milliseconds());
						}
						if (entry.platform != CROSSPLAY_PLATFORM_VITA && (entry.experimental || entry.coop) &&
							crossplay_guard_answered(entry.name, system_milliseconds()) == 0)
						{
							continue;
						}
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
		/* (debug.network_test_join_file: not before the test makes it) */
		else if (!network_test.joined && network_test.join_file[0] && !network_test_join_file_there())
			network_test_log_listed();
		else if (!network_test.joined && (network_test_log_listed(), network_game_client_join_first_available_game()))
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
