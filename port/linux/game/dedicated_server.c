/*
DEDICATED_SERVER.C

The dedicated server (dedicated_server.h; port/linux/DEDICATED_SERVER.md):
the game built as a host with no window, sound or player of its own
(`ninja linux-server`, HALO_DEDICATED_SERVER, on the Vitas' side of the
Vita-only line: HALO_NET_AS_VITA), which Vitas join by its code, from the
server browser, or on its LAN. Called from the main loop every frame
(main.c), in network_test_update's place.

- Hosting: as the automated tests' host does (network_test.c: the pregame
  screen's fast setup, then the map and gametype), but with no player: the
  server's own machine only hosts (network_server_manager.c lets its lobby
  count down without one, once sv_minplayers players are in, after
  sv_start_delay seconds). A game over, its scores are shown sv_postgame
  seconds, and the lobby takes the map cycle's next game (sv_mapcycle).
  A game nobody is left in ends after sv_end_empty seconds, and the server
  waits in its lobby, a few frames a second, for the next player.
- Commands: Halo PC's dedicated server's (haloceded) names where they mean
  the same: sv_name, sv_maxplayers, sv_password, sv_public, sv_mapcycle_add,
  sv_map, sv_kick, sv_ban, sv_say, sv_players... ("help" lists them). From
  init.txt at the start and from the standard input
  (posix_dedicated_server.c); never a script, a shell or a file a client
  names, and no remote console.
- Security: the joins an address makes are counted (DEDICATED_JOINS_PER_MINUTE);
  bans are bans.txt's lines (network_distributed.c), which the server keeps
  across restarts; map downloads only of the maps the operator allows
  (sv_map_download); internet play's limits on a peer's packets and on the
  peers of one address are p2p.c's.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "main/main.h"
#include "interface/player_ui.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "networking/network_server_manager_internal.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "tag_files/tag_files.h"
#include "custom_edition_maps.h"
#include "dedicated_server.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* internet play's codes and lobby (port/linux/src/p2p.c) */
#include "../src/p2p.h"

#ifdef HALO_DEDICATED_SERVER

/* the platform layer's (port/linux/src/port_config.c, posix_dedicated_server.c) */
const char *config_string(char const *name);
int config_boolean(char const *name);
double config_real(char const *name);
long config_integer(char const *name);
int config_set_override(const char *name, const char *text);
void platform_log(char const *format, ...);
void dedicated_platform_print(const char *format, ...);
void dedicated_platform_console_start(void);
int dedicated_platform_next_command(char *text, int size, int *from_file);
const char *dedicated_platform_command_file(int *found);
const char *dedicated_platform_folder(void);
int dedicated_platform_quit_requested(void);
/* main.c's */
short main_get_solo_level_from_name(char const *name);

/* ---------- constants */

enum
{
	/* the map cycle's most games */
	MAXIMUM_CYCLE_ENTRIES = 64,
	MAP_NAME_SIZE = 32,
	GAMETYPE_NAME_SIZE = 24,
	COMMAND_SIZE = 256,
	MAXIMUM_ARGUMENTS = 8,
	/* commands taken a frame (the file's all at once: they set the server up) */
	MAXIMUM_CONSOLE_COMMANDS_PER_FRAME = 4,
	/* joins (connections to the game) an address may make a minute; its
	allowance comes back one each JOIN_REFILL_SECONDS */
	DEDICATED_JOINS_PER_MINUTE = 6,
	JOIN_REFILL_SECONDS = 60 / DEDICATED_JOINS_PER_MINUTE,
	MAXIMUM_JOIN_ADDRESSES = 128,
	/* frames a second with machines connected (the tick rate), and without */
	ACTIVE_FRAME_CAP = 30,
	IDLE_FRAME_CAP = 10,
	/* bans.txt's lines the ban commands read */
	MAXIMUM_BAN_LINES = 1024,
	BAN_LINE_SIZE = 512,
};

/* the server's states (network_server_message_handler.c's) */
enum
{
	_server_pregame = 0,
	_server_ingame,
	_server_postgame,
};

#define BANS_FILE "d:\\bans.txt"

/* ---------- structures */

struct cycle_entry
{
	char map[MAP_NAME_SIZE];
	char gametype[GAMETYPE_NAME_SIZE];
};

struct join_address
{
	unsigned long address;
	long allowance;
	unsigned long refill_time;
};

/* ---------- globals */

static struct
{
	boolean started;
	real uptime;
	real menu_seconds;
	/* hosting: the server made (fast setup), its map and gametype set in
	the lobby, and how long the lobby has been open */
	boolean hosting;
	boolean map_set;
	real lobby_seconds;
	real map_checked_seconds;
	real retry_seconds;
	/* the cycle and the game played of it; a game named by sv_map, played
	next (once) */
	struct cycle_entry cycle[MAXIMUM_CYCLE_ENTRIES];
	long cycle_count;
	long cycle_index;
	struct cycle_entry next_game;
	boolean has_next_game;
	struct cycle_entry playing;
	char level_path[128];
	/* a game: seconds played, its scores shown, its end asked for (the time
	limit's, sv_end_game's), and with nobody in it; back in the lobby the
	cycle goes on (or, restart, plays it again) */
	real game_seconds;
	real postgame_seconds;
	real empty_seconds;
	boolean end_asked;
	boolean back_to_lobby;
	boolean restart;
	short last_state;
	/* the players seen, for the joined and left lines */
	char seen_names[HALO_PORT_MAXIMUM_NETWORK_PLAYERS][16];
	char code[P2P_CODE_SIZE];
	/* settings (the commands') */
	long minimum_players;
	long start_delay;
	long postgame_seconds_setting;
	long end_empty_seconds;
	long time_limit_minutes;
	long score_limit;
	boolean map_download;
	boolean coop;
	struct join_address joins[MAXIMUM_JOIN_ADDRESSES];
} dedicated =
{
	.minimum_players = 1,
	.start_delay = 10,
	.postgame_seconds_setting = 10,
	.end_empty_seconds = 30,
};

static void unquoted(char *destination, long size, char const *text);

/* ---------- output */

static void say(
	char const *format,
	...)
{
	char line[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	dedicated_platform_print("server: %s", line);
}

/* ---------- addresses */

/* a game's address (host byte order) as the internet sees it: an internet
play peer's (100.64.0.0/10) its real one, as p2p.c knows it (0 if none) */
static unsigned long real_address(
	unsigned long address)
{
	if ((address & 0xFFC00000) == 0x64400000)
	{
		unsigned long network = (address >> 24) | ((address >> 8) & 0xFF00) | ((address << 8) & 0xFF0000) |
			(address << 24);
		unsigned long real = p2p_peer_endpoint_address(network);

		return (real >> 24) | ((real >> 8) & 0xFF00) | ((real << 8) & 0xFF0000) | (real << 24);
	}
	return address;
}

static void address_text(
	unsigned long address,
	char *text,
	long size)
{
	if (address)
		snprintf(text, size, "%lu.%lu.%lu.%lu", (address >> 24) & 255, (address >> 16) & 255, (address >> 8) & 255,
			address & 255);
	else
		snprintf(text, size, "unknown");
}

/* a.b.c.d (each 0 to 255, nothing else) as an address (host byte order) */
static boolean parse_address(
	char const *text,
	unsigned long *address)
{
	unsigned long parts[4];
	long index;

	for (index = 0; index < 4; index++)
	{
		long digits = 0;

		parts[index] = 0;
		while (*text >= '0' && *text <= '9' && digits < 3)
		{
			parts[index] = parts[index] * 10 + (unsigned long)(*text++ - '0');
			digits++;
		}
		if (!digits || parts[index] > 255)
			return FALSE;
		if (index < 3 && *text++ != '.')
			return FALSE;
	}
	if (*text)
		return FALSE;
	*address = parts[0] << 24 | parts[1] << 16 | parts[2] << 8 | parts[3];
	return TRUE;
}

/* ---------- names */

/* a map's name as the operator gives it: letters, digits, "_" and "-",
nothing that is a path */
static boolean map_name_valid(
	char const *name)
{
	long length = 0;

	for (; name[length]; length++)
	{
		char character = name[length];

		if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '_' || character == '-'))
		{
			return FALSE;
		}
	}
	return length > 0 && length < MAP_NAME_SIZE;
}

static void lowercase(
	char *text)
{
	for (; *text; text++)
		if (*text >= 'A' && *text <= 'Z')
			*text = (char)(*text - 'A' + 'a');
}

/* a gametype of the game's own (game_engine_get_variant_by_name), by its
name or Halo PC's: FALSE if none */
static boolean gametype_variant(
	char const *name,
	struct game_variant *variant)
{
	static struct
	{
		char const *alias;
		char const *name;
	} const aliases[] =
	{
		{ "ffa", "slayer" },
		{ "ffa_slayer", "slayer" },
		{ "teamslayer", "team_slayer" },
		{ "tslayer", "team_slayer" },
		{ "koth", "king" },
		{ "team_koth", "team_king" },
		{ "iron_ctf", "ironctf" },
		{ "teamrace", "team_race" },
		{ "teamoddball", "team_oddball" },
	};
	char lower[GAMETYPE_NAME_SIZE];
	long index;

	if (csstrlen(name) >= sizeof(lower))
		return FALSE;
	csstrcpy(lower, name);
	lowercase(lower);
	for (index = 0; index < (long)NUMBEROF(aliases); index++)
		if (!csstrcmp(lower, aliases[index].alias))
			csstrcpy(lower, aliases[index].name);
	game_engine_get_variant_by_name(variant, lower);
	return variant->game_engine_index != 0;
}

/* the level's name of a multiplayer map (Xbox or Custom Edition: both are
levels\test\<name>\<name>, custom_edition_maps.c) */
static void level_path(
	char const *map,
	char *path,
	long size)
{
	snprintf(path, size, "levels\\test\\%s\\%s", map, map);
}

/* whether this machine can play the map; why not, said */
static boolean map_playable(
	char const *map,
	char const *command)
{
	char path[128];
	char missing[128];
	short loadable;

	level_path(map, path, sizeof(path));
	loadable = custom_edition_maps_loadable(path, missing, sizeof(missing));
	if (loadable == _custom_edition_maps_loadable)
		return TRUE;
	if (loadable == _custom_edition_maps_needs_pc_maps)
		say("%s: %s is a Halo PC (Custom Edition) map: game.custom_edition = true in config.toml plays it", command,
			map);
	else if (loadable == _custom_edition_maps_needs_resource_maps)
		say("%s: %s needs resource maps not in the maps folder: %s", command, map, missing);
	else
		say("%s: there is no multiplayer map %s in the maps folder", command, map);
	return FALSE;
}

/* a player's name in ASCII */
static void player_name(
	struct network_player const *player,
	char *text,
	long size)
{
	long index;

	for (index = 0; index < (long)NUMBEROF(player->name) && player->name[index] && index < size - 1; index++)
		text[index] = player_name_character_ascii(player->name[index]);
	text[index] = 0;
}

/* ---------- the server's game */

static struct network_game *server_game(
	void)
{
	struct network_game_server *server = global_network_game_server_get();

	return server ? network_game_server_get_game(server) : NULL;
}

static short server_state(
	void)
{
	struct network_game_server *server = global_network_game_server_get();

	return server ? (short)network_game_server_get_state(server, NULL) : NONE;
}

static long player_count(
	void)
{
	struct network_game *game = server_game();
	long count = 0;
	long index;

	for (index = 0; game && index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
		if (network_player_is_valid(&game->players[index]))
			count++;
	return count;
}

/* the game the lobby plays next: sv_map's, else the cycle's */
static struct cycle_entry const *next_entry(
	void)
{
	static struct cycle_entry const fallback = { "bloodgulch", "slayer" };

	if (dedicated.has_next_game)
		return &dedicated.next_game;
	if (dedicated.cycle_count)
		return &dedicated.cycle[dedicated.cycle_index % dedicated.cycle_count];
	return &fallback;
}

/* sets the lobby's map and gametype (the server's lobby: pregame) */
static boolean apply_entry(
	struct cycle_entry const *entry)
{
	struct network_game_server *server = global_network_game_server_get();
	struct game_variant variant;

	if (!server || !gametype_variant(entry->gametype, &variant))
		return FALSE;
	level_path(entry->map, dedicated.level_path, sizeof(dedicated.level_path));
	network_game_server_change_map_name(server, dedicated.level_path);
	if (dedicated.score_limit > 0)
		variant.universal_variant.score_to_win = dedicated.score_limit;
	player_ui_set_game_variant(&variant);
	network_game_server_change_game_variant(server, &variant);
	dedicated.playing = *entry;
	say("the next game: %s on %s", entry->gametype, entry->map);
	return TRUE;
}

/* ends the game being played (sv_end_game, the time limit, nobody left) */
static boolean end_game(
	char const *why,
	boolean restart)
{
	if (server_state() != _server_ingame || !game_engine_running() || !game_engine_can_score())
		return FALSE;
	if (!dedicated.end_asked)
	{
		dedicated.end_asked = TRUE;
		dedicated.restart = restart;
		say("the game ends (%s)", why);
		game_engine_end_game();
	}
	return TRUE;
}

/* the players who came and went, said */
static void note_players(
	void)
{
	struct network_game *game = server_game();
	long index;

	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		char name[16] = "";

		if (game && network_player_is_valid(&game->players[index]))
			player_name(&game->players[index], name, sizeof(name));
		if (strcmp(name, dedicated.seen_names[index]))
		{
			if (dedicated.seen_names[index][0])
				say("player #%ld %s left", index, dedicated.seen_names[index]);
			if (name[0])
			{
				char address[32];

				address_text(real_address(network_game_server_machine_address(game->players[index].machine_index)),
					address, sizeof(address));
				say("player #%ld %s joined from %s", index, name, address);
			}
			csstrcpy(dedicated.seen_names[index], name);
		}
	}
}

/* ---------- commands */

static void command_help(
	void)
{
	static char const *const lines[] =
	{
		"commands (init.txt and this console; haloceded's names):",
		"  sv_name <name>                   the name the server browser lists",
		"  sv_maxplayers <2-16>             the most players",
		"  sv_password [password]           a password (none without one)",
		"  sv_public <0|1>                  listed in the server browser, or by code only",
		"  sv_mapcycle_add <map> <gametype> a game for the cycle (gametypes: slayer, team_slayer, ctf,",
		"                                   ironctf, king, team_king, oddball, team_oddball, race,",
		"                                   team_race, rally, elimination, stalker, accumulation)",
		"  sv_mapcycle_del <#>              takes a game out of the cycle",
		"  sv_mapcycle_clear, sv_mapcycle   empties, lists the cycle",
		"  sv_map <map> <gametype>          plays that game now (the cycle goes on after it)",
		"  sv_map_next, sv_map_reset        the cycle's next game now; the game again",
		"  sv_end_game                      ends the game (its scores, then the next)",
		"  sv_timelimit <minutes>           a game's length (0: until its score)",
		"  sv_scorelimit <score>            the score that wins (0: the gametype's)",
		"  sv_minplayers <n>                players a game needs to start (1)",
		"  sv_start_delay <seconds>         the lobby's countdown once they are in (10)",
		"  sv_postgame <seconds>            the scores shown after a game (10)",
		"  sv_end_empty <seconds>           a game nobody is in ends after this (30; 0 never)",
		"  sv_coop <level> [difficulty 0-3] co-op on a campaign level (a10...) instead of the cycle",
		"  sv_map_download <0|1>            joiners may download the cycle's custom maps (0)",
		"  sv_port <port>                   internet play's UDP port (init.txt only; -port)",
		"  sv_public_address <ip>[:port]    the address the internet reaches this server at (init.txt only)",
		"  sv_relay <host:port>             a relay for players no direct path reaches (init.txt only)",
		"  sv_players, sv_status            the players (#, name, address); the server",
		"  sv_kick <#|name>                 drops a player (they may join again)",
		"  sv_ban <#|name>                  drops a player and bans their address and hardware id",
		"  sv_ban_ip <a.b.c.d>              bans an address",
		"  sv_banlist, sv_unban <#>         bans.txt's bans; takes one out",
		"  sv_say <text>                    a line on every player's screen (in game)",
		"  quit                             stops the server",
	};
	long index;

	for (index = 0; index < (long)NUMBEROF(lines); index++)
		dedicated_platform_print("%s", lines[index]);
}

static void command_status(
	void)
{
	struct network_game *game = server_game();
	short state = server_state();
	long minutes = (long)(dedicated.uptime / 60.0f);

	say("%s, up %ldh%02ldm; %s", config_string("network.lobby_name")[0] ? config_string("network.lobby_name") : "(no name)",
		minutes / 60, minutes % 60,
		state == _server_pregame ? "in the lobby" : state == _server_ingame ? "playing" :
		state == _server_postgame ? "showing the scores" : "starting");
	if (dedicated.code[0])
		say("code %s; %s%s", dedicated.code, config_boolean("network.host_public") ? "public" : "private (by code only)",
			config_string("network.lobby_password")[0] ? ", with a password" : "");
	if (game)
		say("%s on %s, %ld of %d players", dedicated.coop ? "co-op" : dedicated.playing.gametype,
			dedicated.coop ? config_string("network.coop_level") : dedicated.playing.map, player_count(),
			(int)game->maximum_players);
}

static void command_players(
	void)
{
	struct network_game *game = server_game();
	long index;
	long count = 0;

	for (index = 0; game && index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		struct network_player const *player = &game->players[index];
		char name[16];
		char address[32];
		char const *hardware_id;

		if (!network_player_is_valid(player))
			continue;
		player_name(player, name, sizeof(name));
		address_text(real_address(network_game_server_machine_address(player->machine_index)), address,
			sizeof(address));
		hardware_id = network_game_server_machine_hardware_id(player->machine_index);
		say("#%-3ld %-12s team %d  machine %d  %s  hwid %s", index, name, (int)player->team_index,
			(int)player->machine_index, address, hardware_id && hardware_id[0] ? hardware_id : "none");
		count++;
	}
	if (!count)
		say("no players");
}

/* bans.txt's lines (empty ones left out), read; how many */
static long read_bans(
	char lines[][BAN_LINE_SIZE],
	long maximum)
{
	FILE *file = fopen(BANS_FILE, "r");
	char line[BAN_LINE_SIZE];
	long count = 0;

	if (!file)
		return 0;
	while (count < maximum && fgets(line, sizeof(line), file))
	{
		line[strcspn(line, "\r\n")] = 0;
		if (line[0])
			csstrcpy(lines[count++], line);
	}
	fclose(file);
	return count;
}

static void command_banlist(
	void)
{
	static char lines[MAXIMUM_BAN_LINES][BAN_LINE_SIZE];
	long count = read_bans(lines, MAXIMUM_BAN_LINES);
	long index;

	for (index = 0; index < count; index++)
		say("ban #%ld: %s", index, lines[index]);
	if (!count)
		say("no bans (bans.txt in the server's folder)");
}

static void command_unban(
	char const *argument)
{
	static char lines[MAXIMUM_BAN_LINES][BAN_LINE_SIZE];
	long count = read_bans(lines, MAXIMUM_BAN_LINES);
	char *end;
	long number = argument ? strtol(argument, &end, 10) : -1;
	FILE *file;
	long index;

	if (!argument || *end || number < 0 || number >= count)
	{
		say("sv_unban: give a ban's number (sv_banlist lists them)");
		return;
	}
	file = fopen(BANS_FILE, "w");
	if (!file)
	{
		say("sv_unban: cannot write bans.txt");
		return;
	}
	for (index = 0; index < count; index++)
		if (index != number)
			fprintf(file, "%s\n", lines[index]);
	fclose(file);
	say("unbanned: %s", lines[number]);
}

static void command_ban_address(
	char const *argument)
{
	struct network_game *game = server_game();
	unsigned long address;
	char text[32];
	char when[32] = "";
	time_t now = time(NULL);
	struct tm *local = localtime(&now);
	FILE *file;
	long index;

	if (!argument || !parse_address(argument, &address))
	{
		say("sv_ban_ip: give an address, a.b.c.d");
		return;
	}
	address_text(address, text, sizeof(text));
	if (local)
		strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", local);
	file = fopen(BANS_FILE, "a");
	if (!file)
	{
		say("sv_ban_ip: cannot write bans.txt");
		return;
	}
	/* (bans.txt's line, as network_distributed.c writes it) */
	fprintf(file, "%s\tip=%s\thwid=none\tdiscord_username=none\tdiscord_id=none\tplayers=\treason=banned by the "
		"server's operator\n", when, text);
	fclose(file);
	say("banned %s", text);
	/* (and its players dropped now) */
	for (index = 0; game && index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		if (network_player_is_valid(&game->players[index]) &&
			real_address(network_game_server_machine_address(game->players[index].machine_index)) == address)
		{
			network_game_server_dedicated_drop_player(index, FALSE);
		}
	}
}

/* a player by number (sv_players') or name (as the console's kick and ban) */
static void command_drop(
	char const *argument,
	boolean ban)
{
	char *end;
	long number;

	if (!argument || !argument[0])
	{
		say("%s: give a player's number (sv_players) or name", ban ? "sv_ban" : "sv_kick");
		return;
	}
	number = strtol(argument, &end, 10);
	if (!*end && argument[0] != '-')
	{
		if (network_game_server_dedicated_drop_player(number, ban))
			say("%s player #%ld", ban ? "banned" : "kicked", number);
		return;
	}
	{
		char name[64];

		unquoted(name, sizeof(name), argument);
		if (ban ? network_game_server_ban_player(name) : network_game_server_kick_player(name))
			say("%s %s", ban ? "banned" : "kicked", name);
	}
}

/* an integer setting's argument within its bounds: FALSE (said) if not */
static boolean integer_argument(
	char const *command,
	char const *argument,
	long minimum,
	long maximum,
	long *value)
{
	char *end;
	long number = argument ? strtol(argument, &end, 10) : 0;

	if (!argument || *end || number < minimum || number > maximum)
	{
		say("%s: give a number from %ld to %ld", command, minimum, maximum);
		return FALSE;
	}
	*value = number;
	return TRUE;
}

/* printable ASCII only, at most size - 1 of it */
static void printable(
	char *destination,
	long size,
	char const *source)
{
	long length = 0;

	for (; source && *source && length < size - 1; source++)
		if (*source >= 32 && *source < 127)
			destination[length++] = *source;
	destination[length] = 0;
}

/* the words of a command: separated by spaces, a quoted one as it is;
how many */
static long split(
	char *line,
	char **words,
	long maximum)
{
	long count = 0;
	char *cursor = line;

	while (*cursor && count < maximum)
	{
		while (*cursor == ' ' || *cursor == '\t')
			cursor++;
		if (!*cursor)
			break;
		if (*cursor == '"')
		{
			words[count++] = ++cursor;
			while (*cursor && *cursor != '"')
				cursor++;
		}
		else
		{
			words[count++] = cursor;
			while (*cursor && *cursor != ' ' && *cursor != '\t')
				cursor++;
		}
		if (*cursor)
			*cursor++ = 0;
	}
	return count;
}

/* the text after the command's word (sv_say's, sv_name's), quotes and all */
static char const *rest_of_line(
	char const *line)
{
	while (*line == ' ' || *line == '\t')
		line++;
	while (*line && *line != ' ' && *line != '\t')
		line++;
	while (*line == ' ' || *line == '\t')
		line++;
	return line;
}

/* a quoted text's inside, else the text */
static void unquoted(
	char *destination,
	long size,
	char const *text)
{
	char kept[COMMAND_SIZE];
	long length;

	printable(kept, sizeof(kept), text);
	length = (long)csstrlen(kept);
	while (length > 0 && kept[length - 1] == ' ')
		kept[--length] = 0;
	if (length >= 2 && kept[0] == '"' && kept[length - 1] == '"')
	{
		kept[length - 1] = 0;
		printable(destination, size, kept + 1);
	}
	else
		printable(destination, size, kept);
}

static void command(
	char const *line,
	boolean from_file)
{
	char copy[COMMAND_SIZE];
	char *words[MAXIMUM_ARGUMENTS] = { 0 };
	long count;
	char const *name;
	char const *argument;
	long value;

	printable(copy, sizeof(copy), line);
	count = split(copy, words, MAXIMUM_ARGUMENTS);
	/* (nothing, or a comment: ";", "#" or "//") */
	if (!count || words[0][0] == ';' || words[0][0] == '#' || (words[0][0] == '/' && words[0][1] == '/'))
		return;
	lowercase(words[0]);
	name = words[0];
	argument = count > 1 ? words[1] : NULL;
	if (!from_file)
		platform_log("server: the console: %s", line);

	if (!csstrcmp(name, "help") || !csstrcmp(name, "sv_help") || !csstrcmp(name, "?"))
		command_help();
	else if (!csstrcmp(name, "quit") || !csstrcmp(name, "exit") || !csstrcmp(name, "sv_quit"))
	{
		say("stopping (the server browser's listing closed)");
		exit(EXIT_SUCCESS);
	}
	else if (!csstrcmp(name, "sv_name"))
	{
		char text[P2P_LOBBY_NAME_SIZE];

		unquoted(text, sizeof(text), rest_of_line(line));
		if (!text[0])
			say("sv_name: give a name");
		else
		{
			config_set_override("network.lobby_name", text);
			p2p_lobby_set_name(text);
			say("name: %s", text);
		}
	}
	else if (!csstrcmp(name, "sv_maxplayers"))
	{
		if (integer_argument(name, argument, 2, 16, &value))
		{
			char text[8];

			snprintf(text, sizeof(text), "%ld", value);
			config_set_override("network.max_players", text);
			say("most players: %ld%s", value, server_state() == _server_pregame ? "" : " (from the next game)");
		}
	}
	else if (!csstrcmp(name, "sv_password"))
	{
		char text[P2P_LOBBY_PASSWORD_SIZE];

		unquoted(text, sizeof(text), rest_of_line(line));
		config_set_override("network.lobby_password", text);
		if (dedicated.hosting)
			p2p_lobby_set_password(text);
		say(text[0] ? "password set (joining from the server browser asks for it; the code still joins)" :
			"no password");
	}
	else if (!csstrcmp(name, "sv_public"))
	{
		if (integer_argument(name, argument, 0, 1, &value))
		{
			config_set_override("network.host_public", value ? "true" : "false");
			if (dedicated.hosting)
				p2p_lobby_set_public((int)value);
			say(value ? "public: listed in the server browser" : "private: joined by its code only");
		}
	}
	else if (!csstrcmp(name, "sv_mapcycle_add"))
	{
		struct game_variant variant;

		if (count < 3)
			say("sv_mapcycle_add: give a map and a gametype (sv_mapcycle_add bloodgulch ctf)");
		else if (!map_name_valid(words[1]))
			say("sv_mapcycle_add: %s is not a map's name", words[1]);
		else if (!gametype_variant(words[2], &variant))
			say("sv_mapcycle_add: %s is not a gametype (help lists them)", words[2]);
		else if (dedicated.cycle_count == MAXIMUM_CYCLE_ENTRIES)
			say("sv_mapcycle_add: the cycle is full (%d games)", MAXIMUM_CYCLE_ENTRIES);
		else if (map_playable(words[1], name))
		{
			struct cycle_entry *entry = &dedicated.cycle[dedicated.cycle_count++];

			lowercase(words[1]);
			lowercase(words[2]);
			csstrcpy(entry->map, words[1]);
			csstrncpy(entry->gametype, words[2], sizeof(entry->gametype) - 1);
			say("cycle #%ld: %s on %s", dedicated.cycle_count - 1, entry->gametype, entry->map);
		}
	}
	else if (!csstrcmp(name, "sv_mapcycle_del"))
	{
		if (integer_argument(name, argument, 0, MAX(dedicated.cycle_count - 1, 0), &value) && dedicated.cycle_count)
		{
			long index;

			say("cycle: %s on %s taken out", dedicated.cycle[value].gametype, dedicated.cycle[value].map);
			for (index = value; index + 1 < dedicated.cycle_count; index++)
				dedicated.cycle[index] = dedicated.cycle[index + 1];
			dedicated.cycle_count--;
			if (dedicated.cycle_index > value)
				dedicated.cycle_index--;
		}
	}
	else if (!csstrcmp(name, "sv_mapcycle_clear"))
	{
		dedicated.cycle_count = 0;
		dedicated.cycle_index = 0;
		say("the cycle is empty (sv_mapcycle_add adds games; Blood Gulch slayer until then)");
	}
	else if (!csstrcmp(name, "sv_mapcycle") || !csstrcmp(name, "sv_mapcycle_begin"))
	{
		long index;

		if (!csstrcmp(name, "sv_mapcycle_begin"))
		{
			dedicated.cycle_index = 0;
			dedicated.has_next_game = FALSE;
			if (!end_game("the cycle starts over", FALSE) && server_state() == _server_pregame)
				dedicated.map_set = FALSE;
		}
		for (index = 0; index < dedicated.cycle_count; index++)
			say("cycle #%ld: %s on %s%s", index, dedicated.cycle[index].gametype, dedicated.cycle[index].map,
				index == dedicated.cycle_index % dedicated.cycle_count ? " (next)" : "");
		if (!dedicated.cycle_count)
			say("the cycle is empty (Blood Gulch slayer)");
	}
	else if (!csstrcmp(name, "sv_map"))
	{
		struct game_variant variant;

		if (count < 3)
			say("sv_map: give a map and a gametype (sv_map bloodgulch ctf)");
		else if (!map_name_valid(words[1]))
			say("sv_map: %s is not a map's name", words[1]);
		else if (!gametype_variant(words[2], &variant))
			say("sv_map: %s is not a gametype (help lists them)", words[2]);
		else if (map_playable(words[1], name))
		{
			lowercase(words[1]);
			lowercase(words[2]);
			csstrcpy(dedicated.next_game.map, words[1]);
			csstrncpy(dedicated.next_game.gametype, words[2], sizeof(dedicated.next_game.gametype) - 1);
			dedicated.has_next_game = TRUE;
			if (!end_game("sv_map", FALSE))
			{
				if (server_state() == _server_pregame)
					dedicated.map_set = FALSE;
				say("next: %s on %s", words[2], words[1]);
			}
		}
	}
	else if (!csstrcmp(name, "sv_map_next"))
	{
		if (!end_game("sv_map_next", FALSE))
			say("sv_map_next: no game is being played");
	}
	else if (!csstrcmp(name, "sv_map_reset"))
	{
		if (!end_game("sv_map_reset", TRUE))
			say("sv_map_reset: no game is being played");
	}
	else if (!csstrcmp(name, "sv_end_game"))
	{
		if (!end_game("sv_end_game", FALSE))
			say("sv_end_game: no game is being played");
	}
	else if (!csstrcmp(name, "sv_timelimit"))
	{
		if (integer_argument(name, argument, 0, 24 * 60, &dedicated.time_limit_minutes))
			say(dedicated.time_limit_minutes ? "a game lasts %ld minutes at most" : "a game lasts until its score",
				dedicated.time_limit_minutes);
	}
	else if (!csstrcmp(name, "sv_scorelimit"))
	{
		if (integer_argument(name, argument, 0, 999, &dedicated.score_limit))
			say(dedicated.score_limit ? "the score to win: %ld (from the next game)" :
				"the score to win: the gametype's (from the next game)", dedicated.score_limit);
	}
	else if (!csstrcmp(name, "sv_minplayers"))
	{
		if (integer_argument(name, argument, 1, 16, &dedicated.minimum_players))
			say("a game starts with %ld player%s", dedicated.minimum_players, dedicated.minimum_players == 1 ? "" : "s");
	}
	else if (!csstrcmp(name, "sv_start_delay"))
	{
		if (integer_argument(name, argument, 1, 120, &dedicated.start_delay))
			say("the lobby counts down %ld seconds", dedicated.start_delay);
	}
	else if (!csstrcmp(name, "sv_postgame"))
	{
		if (integer_argument(name, argument, 3, 120, &dedicated.postgame_seconds_setting))
			say("the scores are shown %ld seconds", dedicated.postgame_seconds_setting);
	}
	else if (!csstrcmp(name, "sv_end_empty"))
	{
		if (integer_argument(name, argument, 0, 3600, &dedicated.end_empty_seconds))
			say(dedicated.end_empty_seconds ? "a game nobody is in ends after %ld seconds" :
				"a game nobody is in goes on", dedicated.end_empty_seconds);
	}
	else if (!csstrcmp(name, "sv_coop"))
	{
		char difficulty[4] = "1";

		if (!argument || (main_get_solo_level_from_name(argument) == NONE && csstrncmp(argument, "custom_maps\\", 12)))
			say("sv_coop: give a campaign level (a10, a30, a50, b30, b40, c10, c20, c40, d20, d40)");
		else if (dedicated.hosting)
			say("sv_coop: init.txt only (the server hosts co-op from its start)");
		else
		{
			if (count > 2 && integer_argument(name, words[2], 0, 3, &value))
				snprintf(difficulty, sizeof(difficulty), "%ld", value);
			config_set_override("network.coop_level", argument);
			config_set_override("network.coop_difficulty", difficulty);
			dedicated.coop = TRUE;
			say("co-op on %s, difficulty %s (the host runs the AI and the scripts)", argument, difficulty);
		}
	}
	else if (!csstrcmp(name, "sv_map_download"))
	{
		if (integer_argument(name, argument, 0, 1, &value))
		{
			dedicated.map_download = (boolean)value;
			say(value ? "joiners may download the cycle's custom maps (in the lobby)" : "no map downloads");
		}
	}
	else if (!csstrcmp(name, "sv_port") || !csstrcmp(name, "sv_public_address") || !csstrcmp(name, "sv_relay"))
	{
		/* (read from the file before the game starts: posix_dedicated_server.c) */
		if (!from_file)
			say("%s: init.txt only (it takes effect at the start)", name);
	}
	else if (!csstrcmp(name, "sv_players"))
		command_players();
	else if (!csstrcmp(name, "sv_status") || !csstrcmp(name, "status"))
		command_status();
	else if (!csstrcmp(name, "sv_kick"))
		command_drop(argument ? rest_of_line(line) : NULL, FALSE);
	else if (!csstrcmp(name, "sv_ban"))
		command_drop(argument ? rest_of_line(line) : NULL, TRUE);
	else if (!csstrcmp(name, "sv_ban_ip"))
		command_ban_address(argument);
	else if (!csstrcmp(name, "sv_banlist"))
		command_banlist();
	else if (!csstrcmp(name, "sv_unban"))
		command_unban(argument);
	else if (!csstrcmp(name, "sv_say") || !csstrcmp(name, "say"))
	{
		char text[COMMAND_SIZE];

		unquoted(text, sizeof(text), rest_of_line(line));
		if (!text[0])
			say("sv_say: give the text");
		else if (!network_distributed_say(text))
			say("sv_say: only in a game (the lobby's players hear nothing of it)");
	}
	else if (!csstrcmp(name, "sv_rcon_password") || !csstrcmp(name, "rcon") || !csstrcmp(name, "sv_rcon"))
		say("%s: there is no remote console; commands are typed here, on the server's machine", name);
	else
		say("%s: not a command (help lists them)", name);
}

/* ---------- the server's hooks */

boolean dedicated_server_lobby_ready(
	void)
{
	return dedicated.hosting && dedicated.map_set && !dedicated.back_to_lobby;
}

long dedicated_server_countdown_milliseconds(
	void)
{
	return dedicated.start_delay * 1000 - 1;
}

long dedicated_server_minimum_players(
	void)
{
	return dedicated.minimum_players;
}

boolean dedicated_server_join_allowed(
	unsigned long address)
{
	unsigned long now = (unsigned long)time(NULL);
	struct join_address *slot = NULL;
	struct join_address *oldest = &dedicated.joins[0];
	long index;

	address = real_address(address);
	if (!address)
		return TRUE;
	for (index = 0; index < MAXIMUM_JOIN_ADDRESSES; index++)
	{
		struct join_address *entry = &dedicated.joins[index];

		if (entry->address == address)
		{
			slot = entry;
			break;
		}
		if (entry->refill_time < oldest->refill_time)
			oldest = entry;
	}
	if (!slot)
	{
		/* (the address least lately counted makes room: full again by now) */
		slot = oldest;
		slot->address = address;
		slot->allowance = DEDICATED_JOINS_PER_MINUTE;
		slot->refill_time = now;
	}
	while (slot->allowance < DEDICATED_JOINS_PER_MINUTE && now - slot->refill_time >= JOIN_REFILL_SECONDS)
	{
		slot->allowance++;
		slot->refill_time += JOIN_REFILL_SECONDS;
	}
	if (slot->allowance >= DEDICATED_JOINS_PER_MINUTE)
		slot->refill_time = now;
	if (slot->allowance <= 0)
	{
		char text[32];

		address_text(address, text, sizeof(text));
		platform_log("server: %s joins too often (%d a minute); refused", text, DEDICATED_JOINS_PER_MINUTE);
		return FALSE;
	}
	slot->allowance--;
	return TRUE;
}

boolean dedicated_server_map_shareable(
	char const *level_name)
{
	char const *name = level_name ? tag_name_strip_path(level_name) : "";
	long index;

	if (!dedicated.map_download || !name[0])
		return FALSE;
	/* (the cycle's maps and the game sv_map named: those the operator chose) */
	if (!csstrcasecmp(name, dedicated.playing.map))
		return TRUE;
	for (index = 0; index < dedicated.cycle_count; index++)
		if (!csstrcasecmp(name, dedicated.cycle[index].map))
			return TRUE;
	return FALSE;
}

long dedicated_server_frame_cap(
	void)
{
	short state = server_state();

	return state == _server_ingame || network_game_server_dedicated_machine_count() > 0 ? ACTIVE_FRAME_CAP :
		IDLE_FRAME_CAP;
}

/* ---------- each frame */

void dedicated_server_update(
	boolean main_menu_loaded,
	real seconds)
{
	struct network_game_server *server;
	char line[COMMAND_SIZE];
	int from_file;
	long taken;
	short state;

	/* (the clock's: a frame's seconds are the game's, held back after a
	long one) */
	{
		static unsigned long started;
		unsigned long now = system_milliseconds();

		if (!started)
			started = now ? now : 1;
		dedicated.uptime = (real)(now - started) / 1000.0f;
	}
	if (dedicated_platform_quit_requested())
	{
		say("stopping (a signal; the server browser's listing closed)");
		exit(EXIT_SUCCESS);
	}
	if (config_real("debug.exit_after") > 0.0 && dedicated.uptime >= config_real("debug.exit_after"))
	{
		platform_log("exiting after debug.exit_after");
		exit(EXIT_SUCCESS);
	}

	/* the start: the main menu up (its maps found), init.txt's commands, then
	the console */
	if (!dedicated.started)
	{
		int found;
		char const *file;

		if (!main_menu_loaded)
			return;
		dedicated.started = TRUE;
		say("Halo CE for PS Vita dedicated server, network version %d; folder %s", HALO_PORT_NETWORK_VERSION,
			dedicated_platform_folder());
		file = dedicated_platform_command_file(&found);
		say(found ? "commands from %s" : "no %s: the defaults (Blood Gulch slayer, public)", file);
		while (dedicated_platform_next_command(line, sizeof(line), &from_file))
			command(line, from_file);
		if (!dedicated.cycle_count && !dedicated.coop)
			say("the cycle is empty: Blood Gulch slayer (sv_mapcycle_add adds games)");
		dedicated_platform_console_start();
		say("type help for the commands");
	}
	for (taken = 0; taken < MAXIMUM_CONSOLE_COMMANDS_PER_FRAME &&
		dedicated_platform_next_command(line, sizeof(line), &from_file); taken++)
	{
		command(line, from_file);
	}

	server = global_network_game_server_get();
	state = server_state();

	/* hosting: from the main menu, where the game starts and comes back to
	if its server goes (the network lost) */
	if (!server)
	{
		if (!main_menu_loaded)
			return;
		if (dedicated.hosting)
		{
			dedicated.hosting = FALSE;
			say("the game's server stopped; hosting again");
			dedicated.menu_seconds = 0.0f;
		}
		dedicated.menu_seconds += seconds;
		if (dedicated.menu_seconds < 2.0f)
			return;
		player_ui_fast_setup_network_server();
		if (!global_network_game_server_get())
		{
			say("could not host (the game's port 5150 taken by another program?); trying again in 5 s");
			dedicated.menu_seconds = -3.0f;
			return;
		}
		dedicated.hosting = TRUE;
		dedicated.map_set = FALSE;
		dedicated.lobby_seconds = 0.0f;
		dedicated.map_checked_seconds = 0.0f;
		dedicated.back_to_lobby = FALSE;
		/* (the settings' password and visibility, as the commands left them) */
		p2p_lobby_set_public(config_boolean("network.host_public"));
		return;
	}

	/* the code, once internet play hosts (and if it changes) */
	{
		char code[P2P_CODE_SIZE];

		if (p2p_hosting_code(code, sizeof(code)) && strcmp(code, dedicated.code))
		{
			csstrcpy(dedicated.code, code);
			say("hosting; Vitas join with the code %s, or from the server browser%s", code,
				config_boolean("network.host_public") ? "" : " (not: sv_public 0)");
		}
	}
	note_players();

	switch (state)
	{
	case _server_pregame:
		if (!main_menu_loaded)
			break;
		/* (back from a game: the cycle's next, or the same again) */
		if (dedicated.back_to_lobby)
		{
			dedicated.back_to_lobby = FALSE;
			if (dedicated.has_next_game && !dedicated.restart &&
				!strcmp(dedicated.playing.map, dedicated.next_game.map) &&
				!strcmp(dedicated.playing.gametype, dedicated.next_game.gametype))
			{
				/* (sv_map's game played: the cycle goes on after it) */
				dedicated.has_next_game = FALSE;
			}
			/* (a game of the cycle over, or cut short by sv_map's: its next) */
			else if (!dedicated.restart && dedicated.cycle_count)
				dedicated.cycle_index = (dedicated.cycle_index + 1) % dedicated.cycle_count;
			dedicated.restart = FALSE;
			dedicated.map_set = FALSE;
			dedicated.lobby_seconds = 0.0f;
			dedicated.map_checked_seconds = 0.0f;
			/* (as picking the next game's map does: the scores' map choice
			holds the countdown) */
			network_game_server_pause_countdown(server, FALSE);
		}
		dedicated.lobby_seconds += seconds;
		dedicated.game_seconds = 0.0f;
		dedicated.empty_seconds = 0.0f;
		dedicated.postgame_seconds = 0.0f;
		dedicated.end_asked = FALSE;
		/* (co-op: the server's co-op setting sets the level itself,
		network_server_manager.c) */
		if (!dedicated.map_set && dedicated.coop)
			dedicated.map_set = TRUE;
		else if (!dedicated.map_set && dedicated.lobby_seconds >= 1.0f)
		{
			dedicated.map_set = apply_entry(next_entry());
			if (!dedicated.map_set)
			{
				say("the game %s on %s cannot be set up; the cycle's next", next_entry()->gametype, next_entry()->map);
				dedicated.has_next_game = FALSE;
				if (dedicated.cycle_count)
					dedicated.cycle_index = (dedicated.cycle_index + 1) % dedicated.cycle_count;
			}
		}
		/* (the lobby's own widgets may put the map back: set again) */
		else if (dedicated.map_set && !dedicated.coop && dedicated.level_path[0] &&
			dedicated.lobby_seconds - dedicated.map_checked_seconds >= 1.0f)
		{
			dedicated.map_checked_seconds = dedicated.lobby_seconds;
			if (strcmp(main_get_multiplayer_map_name(), dedicated.level_path))
				network_game_server_change_map_name(server, dedicated.level_path);
		}
		break;
	case _server_ingame:
		if (dedicated.last_state != _server_ingame)
		{
			say("the game starts: %s on %s, %ld player%s", dedicated.coop ? "co-op" : dedicated.playing.gametype,
				dedicated.coop ? config_string("network.coop_level") : dedicated.playing.map, player_count(),
				player_count() == 1 ? "" : "s");
		}
		if (!game_engine_running() && !dedicated.coop)
			break;
		/* (the scores, shown a while: then the lobby, for the next game) */
		if (!dedicated.coop && !game_engine_can_score())
		{
			if (game_engine_showing_postgame())
			{
				dedicated.postgame_seconds += seconds;
				if (dedicated.postgame_seconds >= (real)dedicated.postgame_seconds_setting && !dedicated.back_to_lobby)
				{
					dedicated.back_to_lobby = TRUE;
					network_game_server_reset_to_pregame(server);
				}
			}
			break;
		}
		dedicated.game_seconds += seconds;
		if (dedicated.time_limit_minutes > 0 && dedicated.game_seconds >= (real)(dedicated.time_limit_minutes * 60))
			end_game("the time limit", FALSE);
		if (network_game_server_dedicated_machine_count() == 0)
		{
			dedicated.empty_seconds += seconds;
			if (dedicated.end_empty_seconds > 0 && dedicated.empty_seconds >= (real)dedicated.end_empty_seconds)
			{
				if (dedicated.coop)
				{
					/* (co-op has no game engine to end: back to the lobby,
					the level again for the next to join) */
					if (!dedicated.back_to_lobby)
					{
						say("nobody is left: back to the lobby");
						dedicated.back_to_lobby = TRUE;
						dedicated.restart = TRUE;
						network_game_server_reset_to_pregame(server);
					}
				}
				else
					end_game("nobody is left", TRUE);
			}
		}
		else
			dedicated.empty_seconds = 0.0f;
		break;
	case _server_postgame:
		/* (co-op: a level won; the server's next round is the next level) */
		dedicated.postgame_seconds += seconds;
		if (dedicated.postgame_seconds >= (real)dedicated.postgame_seconds_setting && !dedicated.back_to_lobby)
		{
			dedicated.back_to_lobby = TRUE;
			network_game_server_reset_to_pregame(server);
		}
		break;
	}
	dedicated.last_state = state;
}

#endif
