/*
BOTS_PLAYLIST.C

The bots' playlist (bots.playlist; the Vita's settings panel: Multiplayer,
Bot playlist, shown while Bots is on): a local (split screen) game with bots
goes on to its next map and gametype by itself after the scores, as
matchmaking does, game after game, until the players quit. The idea is the
MCC mod "HALO: CE BOT BOOTCAMP"'s Match Composer; none of its files or code.

- The lists: "small" (the Xbox's smaller maps: Chill Out, Prisoner, Hang 'Em
  High, Wizard, Damnation, Longest, Rat Race, Battle Creek, Derelict, Chiron
  TL-34), "big" (Blood Gulch, Sidewinder, Boarding Action, and Halo PC's big
  maps where the level list has them: Death Island, Ice Fields, Danger
  Canyon, Infinity, Timberland, Gephyrophobia), "all" (both), each map with
  the gametypes the bots play there (bots.c: Slayer, Team Slayer, CTF, King
  of the Hill, Oddball, Race; none whose goal the map's walkable surfaces do
  not reach, run_bots_test.sh's KNOWN: no CTF on Wizard or Prisoner, no King
  on Hang 'Em High); or "custom", bots.playlist_file (bots_playlist.txt in
  the data folder: ux0:data/haloce-vita/ on the Vita), a game a line:

      # map gametype
      bloodgulch ctf+vehicles
      chillout slayer
      my_custom_map team_slayer

  The file is the player's, so it is read as untrusted text: 8 KB and 200
  lines at most, a line 120 characters (a longer one is skipped whole), and
  64 games; a map is one of the Xbox's thirteen in the maps folder or a
  multiplayer map the level list has (custom_edition_maps_multiplayer_listed:
  in the folder, turned on) that this machine can load, by its file's name
  (letters, digits, '_' and '-', 25 characters); a gametype one of
  bots_playlist_gametypes' names, "+vehicles" after it for every vehicle the
  map places. A line that is none of these is skipped and logged with why;
  with no game left the playlist does nothing (as Off).
- The order: each round of the list shuffled (the seed logged at the start:
  the same seed, the same order), no map twice in a row where the list has
  another (nor the game just played's again).
- The flow, the server's own: at the scores (the postgame's carnage report)
  the playlist waits a while (BOTS_PLAYLIST_SCORES_SECONDS) and goes back to
  the lobby as the host's A does there (network_game_server_reset_to_pregame;
  the A goes sooner, B quits as ever); as the server leaves the scores, its
  next game is put in the stage the lobby is set up from (the engine's own
  playlist's: game_engine_port_set_stage); the lobby's screen is then the
  pregame screen, not the map choice (ui_widget.c), with the game's time
  limit (BOTS_PLAYLIST_TIME_LIMIT minutes, the PC options': no game goes on
  for ever where the bots cannot score), the bots back in it
  (network_server_manager.c) and the countdown started. B in that lobby or
  its countdown stops the playlist until the players leave Split Screen: the
  countdown stops and the map choice is up, as after a game with the
  playlist off (the B taken from the menus: the map choice, put up with it
  pressed, left Split Screen).
- Never in a system link or internet game (network_game_is_splitscreen_local),
  nor with no bots wanted, nor in co-op (no game engine): nothing of the
  network protocol changes. Off does nothing at all.

halo.log says each step: the list (its games and seed), each line skipped,
the next game, the countdown, the game started, the stop.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_engine_playlist.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/player_ui.h"
#include "interface/ui_widget.h"
#include "main/main.h"
#include "networking/network_game_globals.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "networking/network_server_manager_internal.h"
#include "tag_files/tag_files.h"
#include "text/unicode.h"

#include "custom_edition_cache.h"
#include "custom_edition_maps.h"
#include "bots.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the platform layer's */
const char *config_string(char const *name);
void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
/* xinput_sdl.c's (the tests') */
void test_input_hold_b(int hold);
#endif

/* ---------- constants */

/* the most games in a list */
#define BOTS_PLAYLIST_MAXIMUM_GAMES 64
/* the custom file read: its bytes, lines and a line's characters at most */
#define BOTS_PLAYLIST_FILE_BYTES (8 * 1024)
#define BOTS_PLAYLIST_FILE_LINES 200
#define BOTS_PLAYLIST_LINE_CHARACTERS 120
/* a map's file name (custom_edition_maps.c's longest: its level name in the
game engine's 63 characters) and the custom file's name */
#define BOTS_PLAYLIST_MAP_NAME_CHARACTERS 25
#define BOTS_PLAYLIST_FILE_NAME_CHARACTERS 48
/* the scores shown before the playlist goes back to the lobby */
#define BOTS_PLAYLIST_SCORES_SECONDS 12.0f
/* the lobby up a moment before its countdown starts */
#define BOTS_PLAYLIST_LOBBY_SECONDS 1.0f
/* a playlist game's time limit (minutes) */
#define BOTS_PLAYLIST_TIME_LIMIT 10

#define BOTS_PLAYLIST_LEVEL_FORMAT "levels\\test\\%s\\%s"
#define BOTS_PLAYLIST_PREGAME_SCREEN \
	"ui\\shell\\main_menu\\multiplayer_type_select\\split_screen\\pregame\\splitscreen_pregame_wrapper_normal"
#define BOTS_PLAYLIST_MAP_CHOICE_SCREEN \
	"ui\\shell\\main_menu\\multiplayer_type_select\\split_screen\\splitscreen_map_select_postgame_wrapper"

enum
{
	_bots_playlist_off = 0,
	_bots_playlist_small,
	_bots_playlist_big,
	_bots_playlist_all,
	_bots_playlist_custom,
	NUMBER_OF_BOTS_PLAYLISTS
};

/* where the playlist is */
enum
{
	/* playing (or nothing to do) */
	_bots_playlist_state_idle = 0,
	/* at the scores, waiting to go back to the lobby */
	_bots_playlist_state_scores,
	/* the next game in the stage: the lobby comes */
	_bots_playlist_state_set,
	/* the lobby up: its countdown to start */
	_bots_playlist_state_lobby,
	/* the countdown on */
	_bots_playlist_state_countdown,
};

/* network_server_manager.c's server states (network_game_server_get_state) */
enum
{
	_bots_playlist_server_pregame = 0,
	_bots_playlist_server_ingame,
	_bots_playlist_server_postgame,
};

/* the built-in lists a game is in */
enum
{
	_bots_playlist_list_small = 1 << 0,
	_bots_playlist_list_big = 1 << 1,
};

/* ---------- structures */

struct bots_playlist_gametype
{
	char const *name;
	struct game_variant *(*build)(struct game_variant *variant);
	/* as the lobby shows it (the variant's 11 characters) */
	wchar_t const *title;
};

struct bots_playlist_game
{
	char map[BOTS_PLAYLIST_MAP_NAME_CHARACTERS + 1];
	short gametype;
	boolean vehicles;
};

/* ---------- globals */

/* the gametypes the bots play (bots.c), by the names the custom file takes */
static struct bots_playlist_gametype const bots_playlist_gametypes[] =
{
	{ "slayer", build_game_variant_slayer, L"Slayer" },
	{ "team_slayer", build_game_variant_team_slayer, L"Team Slayer" },
	{ "ctf", build_game_variant_ctf, L"CTF" },
	{ "king", build_game_variant_king, L"King" },
	{ "team_king", build_game_variant_team_king, L"Team King" },
	{ "oddball", build_game_variant_oddball, L"Oddball" },
	{ "team_oddball", build_game_variant_team_oddball, L"Team Ball" },
	{ "race", build_game_variant_race, L"Race" },
	{ "team_race", build_game_variant_team_race, L"Team Race" },
	{ "rockets", build_game_variant_rockets, L"Rockets" },
	{ "snipers", build_game_variant_snipers, L"Snipers" },
	{ "invasion", build_game_variant_invasion, L"Invasion" },
};

/* the Xbox's thirteen multiplayer maps (custom_edition_maps.c's) */
static char const *const bots_playlist_xbox_maps[] =
{
	"beavercreek", "sidewinder", "damnation", "ratrace", "prisoner", "hangemhigh", "chillout",
	"carousel", "boardingaction", "bloodgulch", "wizard", "putput", "longest",
};

/* the built-in lists' games: each map with games the bots play there (the
maps test's games that reach their goal); Halo PC's maps where the level
list has them */
static struct
{
	char const *map;
	char const *gametype;
	unsigned char lists;
} const bots_playlist_builtin[] =
{
	{ "chillout", "slayer", _bots_playlist_list_small },
	{ "chillout", "king", _bots_playlist_list_small },
	{ "prisoner", "slayer", _bots_playlist_list_small },
	{ "prisoner", "team_slayer", _bots_playlist_list_small },
	{ "hangemhigh", "slayer", _bots_playlist_list_small },
	{ "hangemhigh", "ctf", _bots_playlist_list_small },
	{ "wizard", "slayer", _bots_playlist_list_small },
	{ "wizard", "team_slayer", _bots_playlist_list_small },
	{ "damnation", "team_slayer", _bots_playlist_list_small },
	{ "damnation", "ctf", _bots_playlist_list_small },
	{ "longest", "team_slayer", _bots_playlist_list_small },
	{ "longest", "oddball", _bots_playlist_list_small },
	{ "ratrace", "king", _bots_playlist_list_small },
	{ "ratrace", "slayer", _bots_playlist_list_small },
	{ "beavercreek", "ctf", _bots_playlist_list_small },
	{ "beavercreek", "team_slayer", _bots_playlist_list_small },
	{ "carousel", "oddball", _bots_playlist_list_small },
	{ "carousel", "king", _bots_playlist_list_small },
	{ "putput", "slayer", _bots_playlist_list_small },
	{ "bloodgulch", "ctf+vehicles", _bots_playlist_list_big },
	{ "bloodgulch", "team_slayer+vehicles", _bots_playlist_list_big },
	{ "bloodgulch", "king", _bots_playlist_list_big },
	{ "bloodgulch", "team_race", _bots_playlist_list_big },
	{ "sidewinder", "ctf+vehicles", _bots_playlist_list_big },
	{ "sidewinder", "team_slayer+vehicles", _bots_playlist_list_big },
	{ "boardingaction", "team_slayer", _bots_playlist_list_big },
	{ "boardingaction", "slayer", _bots_playlist_list_big },
	{ "deathisland", "ctf+vehicles", _bots_playlist_list_big },
	{ "icefields", "ctf+vehicles", _bots_playlist_list_big },
	{ "dangercanyon", "ctf+vehicles", _bots_playlist_list_big },
	{ "infinity", "ctf+vehicles", _bots_playlist_list_big },
	{ "timberland", "ctf+vehicles", _bots_playlist_list_big },
	{ "gephyrophobia", "ctf+vehicles", _bots_playlist_list_big },
};

static char const *const bots_playlist_names[NUMBER_OF_BOTS_PLAYLISTS] =
{
	"off", "small", "big", "all", "custom",
};

static struct
{
	/* the list, and what it was made from */
	boolean made;
	short made_mode;
	char made_file[BOTS_PLAYLIST_FILE_NAME_CHARACTERS + 1];
	struct bots_playlist_game games[BOTS_PLAYLIST_MAXIMUM_GAMES];
	short count;
	/* this round's order, and how far into it */
	short order[BOTS_PLAYLIST_MAXIMUM_GAMES];
	short position;
	unsigned long random;
	/* B in a countdown: off until the players leave Split Screen */
	boolean stopped;
	short state;
	real seconds;
	/* the game set up for the lobby, its variant; whether the lobby's game
	is it (its options played: the time limit) */
	short next;
	boolean set_up;
	struct game_variant variant;
	char last_map[BOTS_PLAYLIST_MAP_NAME_CHARACTERS + 1];
	/* games the playlist has started this session */
	long played;
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
	boolean test_holding_b;
#endif
} bots_playlist;

/* ---------- private code */

/* a setting: the environment's first (the Vita's settings panel sets it
live), else config.toml's */
static char const *bots_playlist_setting(char const *variable, char const *name)
{
	char const *value = getenv(variable);

	return value && value[0] ? value : config_string(name);
}

#ifndef HALO_RELEASE
/* (the tests') a number from the environment, else `otherwise` */
static long bots_playlist_test_number(char const *variable, long otherwise)
{
	char const *value = getenv(variable);

	return value && value[0] ? atol(value) : otherwise;
}
#endif

static short bots_playlist_mode(void)
{
	char const *name = bots_playlist_setting("HALO_BOT_PLAYLIST", "bots.playlist");
	short mode;

	for (mode = 0; mode < NUMBER_OF_BOTS_PLAYLISTS; mode++)
	{
		if (!csstrcasecmp(name, bots_playlist_names[mode]))
			return mode;
	}
	if (!csstrcasecmp(name, "everything"))
		return _bots_playlist_all;
	if (!csstrcasecmp(name, "file"))
		return _bots_playlist_custom;
	return _bots_playlist_off;
}

/* the custom file's name: a file of the data folder, no folders ("." and
"-" inside, letters, digits and "_"), else the default */
static void bots_playlist_file_name(char *name, size_t size)
{
	char const *setting = bots_playlist_setting("HALO_BOT_PLAYLIST_FILE", "bots.playlist_file");
	size_t length = strlen(setting);
	size_t index;
	boolean good = length > 0 && length <= BOTS_PLAYLIST_FILE_NAME_CHARACTERS && setting[0] != '.';

	for (index = 0; good && index < length; index++)
	{
		char c = setting[index];

		good = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '_' || c == '-' || c == '.';
	}
	if (good && strstr(setting, ".."))
		good = FALSE;
	if (!good)
	{
		platform_log("bots playlist: bots.playlist_file is not a file name of the data folder; bots_playlist.txt read");
		setting = "bots_playlist.txt";
	}
	snprintf(name, size, "%s", setting);
}

/* a random number of the playlist's (xorshift; its seed logged) */
static unsigned long bots_playlist_random(void)
{
	unsigned long x = bots_playlist.random ? bots_playlist.random : 0x9E3779B9UL;

	x ^= (x << 13) & 0xFFFFFFFFUL;
	x ^= x >> 17;
	x ^= (x << 5) & 0xFFFFFFFFUL;
	bots_playlist.random = x & 0xFFFFFFFFUL;
	return bots_playlist.random;
}

static short bots_playlist_gametype_index(char const *name, boolean *vehicles)
{
	char base[32];
	size_t length = strlen(name);
	short index;

	*vehicles = FALSE;
	if (length > 9 && !csstrcasecmp(name + length - 9, "+vehicles"))
	{
		*vehicles = TRUE;
		length -= 9;
	}
	if (length == 0 || length >= sizeof(base))
		return NONE;
	memcpy(base, name, length);
	base[length] = 0;
	for (index = 0; index < (short)NUMBEROF(bots_playlist_gametypes); index++)
	{
		if (!csstrcasecmp(base, bots_playlist_gametypes[index].name))
			return index;
	}
	return NONE;
}

static boolean bots_playlist_xbox_map(char const *name)
{
	short index;

	for (index = 0; index < (short)NUMBEROF(bots_playlist_xbox_maps); index++)
	{
		if (!csstrcasecmp(name, bots_playlist_xbox_maps[index]))
			return TRUE;
	}
	return FALSE;
}

/* whether this machine plays the map named `name` (a file name): its
characters, one of the Xbox's maps in the maps folder or a multiplayer map
the level list has, and loadable as it is now; why not in `why` */
static boolean bots_playlist_map_playable(char const *name, char const **why)
{
	char level[64];
	char path[256];
	char missing[128];
	size_t length = strlen(name);
	size_t index;

	if (length == 0 || length > BOTS_PLAYLIST_MAP_NAME_CHARACTERS)
	{
		*why = "the map's name is too long";
		return FALSE;
	}
	for (index = 0; index < length; index++)
	{
		char c = name[index];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
		{
			*why = "the map's name is not a file's (letters, digits, _ and -)";
			return FALSE;
		}
	}
	if (!custom_edition_cache_map_file_path(name, path, sizeof(path)))
	{
		*why = "no such map in the maps folder";
		return FALSE;
	}
	if (!bots_playlist_xbox_map(name) && !custom_edition_maps_multiplayer_listed(name))
	{
		*why = "not a multiplayer map of the level list (a campaign map, or turned off)";
		return FALSE;
	}
	snprintf(level, sizeof(level), BOTS_PLAYLIST_LEVEL_FORMAT, name, name);
	if (custom_edition_maps_loadable(level, missing, sizeof(missing)) != _custom_edition_maps_loadable)
	{
		*why = "this machine cannot load it now (PC maps off, or resource maps missing)";
		return FALSE;
	}
	*why = NULL;
	return TRUE;
}

static boolean bots_playlist_add(char const *map, char const *gametype, char const **why)
{
	struct bots_playlist_game *game;
	boolean vehicles;
	short gametype_index = bots_playlist_gametype_index(gametype, &vehicles);
	char *c;

	if (gametype_index == NONE)
	{
		*why = "not a gametype the bots play";
		return FALSE;
	}
	if (!bots_playlist_map_playable(map, why))
		return FALSE;
	if (bots_playlist.count >= BOTS_PLAYLIST_MAXIMUM_GAMES)
	{
		*why = "the list is full (64 games)";
		return FALSE;
	}
	game = &bots_playlist.games[bots_playlist.count++];
	snprintf(game->map, sizeof(game->map), "%s", map);
	/* (the file's name in lower case, as the level list names the Xbox's) */
	for (c = game->map; *c; c++)
	{
		if (*c >= 'A' && *c <= 'Z')
			*c = (char)(*c - 'A' + 'a');
	}
	game->gametype = gametype_index;
	game->vehicles = vehicles;
	*why = NULL;
	return TRUE;
}

/* a line's text for the log: printable characters, 40 at most */
static void bots_playlist_printable(char const *text, char *printable, size_t size)
{
	size_t length = 0;

	for (; *text && length + 1 < size && length < 40; text++)
		printable[length++] = *text >= ' ' && *text <= '~' ? *text : '?';
	printable[length] = 0;
}

/* the custom file's games (each line it skips logged): the file's first
BOTS_PLAYLIST_FILE_BYTES read whole, then taken a line at a time, each byte
looked at (a line with a NUL or a byte that is not printable ASCII, but a
tab and the CR of a CR LF, is skipped) */
static void bots_playlist_read_file(char const *file_name)
{
	static char text[BOTS_PLAYLIST_FILE_BYTES + 1];
	char path[BOTS_PLAYLIST_FILE_NAME_CHARACTERS + 8];
	char line[BOTS_PLAYLIST_LINE_CHARACTERS + 1];
	long size;
	long offset = 0;
	boolean truncated;
	short line_number = 0;
	short skipped = 0;
	FILE *file;

	snprintf(path, sizeof(path), "d:\\%s", file_name);
	file = fopen(path, "rb");
	if (!file)
	{
		platform_log("bots playlist: no %s in the data folder: no custom games", file_name);
		return;
	}
	size = (long)fread(text, 1, BOTS_PLAYLIST_FILE_BYTES, file);
	truncated = size == BOTS_PLAYLIST_FILE_BYTES && fgetc(file) != EOF;
	fclose(file);
	if (size < 0)
		size = 0;
	text[size] = 0;
	/* (a UTF-8 file's mark, at its start) */
	if (size >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
		(unsigned char)text[2] == 0xBF)
	{
		offset = 3;
	}
	while (offset < size && line_number < BOTS_PLAYLIST_FILE_LINES)
	{
		char printable[48];
		char const *why = NULL;
		char *map;
		char *gametype;
		char *extra;
		char *rest;
		long start = offset;
		long length;
		long index;
		boolean plain = TRUE;

		while (offset < size && text[offset] != '\n')
			offset++;
		length = offset - start;
		line_number++;
		/* (the last line cut off where the file was cut: not read) */
		if (offset >= size && truncated)
		{
			platform_log("bots playlist: %s read only to line %d (%d bytes at most)", file_name, line_number - 1,
				BOTS_PLAYLIST_FILE_BYTES);
			break;
		}
		offset++;
		if (length > 0 && text[start + length - 1] == '\r')
			length--;
		for (index = 0; index < length; index++)
		{
			unsigned char c = (unsigned char)text[start + index];

			if ((c < ' ' && c != '\t') || c > '~')
				plain = FALSE;
		}
		if (length > BOTS_PLAYLIST_LINE_CHARACTERS)
			why = "longer than 120 characters";
		else if (!plain)
			why = "not plain text (a NUL, a control character or a byte beyond ASCII)";
		if (why)
		{
			platform_log("bots playlist: %s line %d skipped (%s)", file_name, line_number, why);
			skipped++;
			continue;
		}
		memcpy(line, text + start, (size_t)length);
		line[length] = 0;
		rest = strchr(line, '#');
		if (rest)
			*rest = 0;
		map = strtok_r(line, " \t", &rest);
		if (!map)
			continue;
		gametype = strtok_r(NULL, " \t", &rest);
		extra = gametype ? strtok_r(NULL, " \t", &rest) : NULL;
		if (!gametype)
			why = "a map and a gametype are asked";
		else if (extra)
			why = "more than a map and a gametype";
		if (why || !bots_playlist_add(map, gametype, &why))
		{
			memcpy(line, text + start, (size_t)length);
			line[length] = 0;
			bots_playlist_printable(line, printable, sizeof(printable));
			platform_log("bots playlist: %s line %d skipped (%s): %s", file_name, line_number, why, printable);
			skipped++;
		}
	}
	if (offset < size && line_number >= BOTS_PLAYLIST_FILE_LINES)
	{
		platform_log("bots playlist: %s read only to line %d (%d lines at most)", file_name, line_number,
			BOTS_PLAYLIST_FILE_LINES);
	}
	platform_log("bots playlist: %s: %d games, %d lines skipped", file_name, bots_playlist.count, skipped);
}

/* the list for the mode, and the first round's order (its seed logged) */
static void bots_playlist_make(short mode)
{
	char games[256];
	long length = 0;
	short index;

	bots_playlist.count = 0;
	bots_playlist.position = 0;
	bots_playlist.made = TRUE;
	bots_playlist.made_mode = mode;
	bots_playlist_file_name(bots_playlist.made_file, sizeof(bots_playlist.made_file));
	bots_playlist.random = (unsigned long)(vita_host_time_us() ^ (vita_host_time_us() >> 32)) & 0xFFFFFFFFUL;
#ifndef HALO_RELEASE
	bots_playlist.random = (unsigned long)bots_playlist_test_number("HALO_TEST_BOT_PLAYLIST_SEED",
		(long)bots_playlist.random) & 0xFFFFFFFFUL;
#endif
	if (!bots_playlist.random)
		bots_playlist.random = 1;
	platform_log("bots playlist: %s, seed %lu", bots_playlist_names[mode], bots_playlist.random);
	if (mode == _bots_playlist_custom)
		bots_playlist_read_file(bots_playlist.made_file);
	else
	{
		unsigned char lists = mode == _bots_playlist_small ? _bots_playlist_list_small :
			mode == _bots_playlist_big ? _bots_playlist_list_big :
			(_bots_playlist_list_small | _bots_playlist_list_big);

		for (index = 0; index < (short)NUMBEROF(bots_playlist_builtin); index++)
		{
			char const *why;

			if (!(bots_playlist_builtin[index].lists & lists))
				continue;
			/* (Halo PC's maps, and Xbox maps the folder has not: left out
			quietly but for a line saying so) */
			if (!bots_playlist_add(bots_playlist_builtin[index].map, bots_playlist_builtin[index].gametype, &why) &&
				bots_playlist_xbox_map(bots_playlist_builtin[index].map))
			{
				platform_log("bots playlist: %s %s left out: %s", bots_playlist_builtin[index].map,
					bots_playlist_builtin[index].gametype, why);
			}
		}
	}
	games[0] = 0;
	for (index = 0; index < bots_playlist.count && length < (long)sizeof(games) - 48; index++)
	{
		length += snprintf(games + length, sizeof(games) - (size_t)length, "%s%s %s%s", index ? ", " : "",
			bots_playlist.games[index].map, bots_playlist_gametypes[bots_playlist.games[index].gametype].name,
			bots_playlist.games[index].vehicles ? "+vehicles" : "");
	}
	platform_log("bots playlist: %d games: %s%s", bots_playlist.count, games, index < bots_playlist.count ? ", ..." : "");
	/* (the first round shuffled as the first game is asked for) */
	bots_playlist.position = bots_playlist.count;
}

/* a new round: the list shuffled, no map twice in a row where another can
come between (nor the last game's map first) */
static void bots_playlist_shuffle(void)
{
	short count = bots_playlist.count;
	short index;

	for (index = 0; index < count; index++)
		bots_playlist.order[index] = index;
	for (index = count - 1; index > 0; index--)
	{
		short other = (short)(bots_playlist_random() % (unsigned long)(index + 1));
		short kept = bots_playlist.order[index];

		bots_playlist.order[index] = bots_playlist.order[other];
		bots_playlist.order[other] = kept;
	}
	for (index = 0; index < count; index++)
	{
		char const *before = index ? bots_playlist.games[bots_playlist.order[index - 1]].map : bots_playlist.last_map;
		short other;

		if (csstrcasecmp(bots_playlist.games[bots_playlist.order[index]].map, before))
			continue;
		for (other = index + 1; other < count; other++)
		{
			if (csstrcasecmp(bots_playlist.games[bots_playlist.order[other]].map, before))
			{
				short kept = bots_playlist.order[index];

				bots_playlist.order[index] = bots_playlist.order[other];
				bots_playlist.order[other] = kept;
				break;
			}
		}
	}
	bots_playlist.position = 0;
}

/* the next game of the list this machine can play now, NONE for none */
static short bots_playlist_next_game(void)
{
	short tries;

	for (tries = 0; tries < bots_playlist.count; tries++)
	{
		struct bots_playlist_game *game;
		char const *why;

		if (bots_playlist.position >= bots_playlist.count)
			bots_playlist_shuffle();
		game = &bots_playlist.games[bots_playlist.order[bots_playlist.position++]];
		/* (a map gone from the folder, or turned off, since the list was made) */
		if (bots_playlist_map_playable(game->map, &why))
			return (short)(game - bots_playlist.games);
		platform_log("bots playlist: %s left out: %s", game->map, why);
	}
	return NONE;
}

static void bots_playlist_build_variant(struct bots_playlist_game const *game, struct game_variant *variant)
{
	struct bots_playlist_gametype const *gametype = &bots_playlist_gametypes[game->gametype];

	csmemset(variant, 0, sizeof(*variant));
	gametype->build(variant);
	/* "+vehicles": every vehicle the map places (the default vehicle set,
	network_test.c's) */
	if (game->vehicles)
		variant->universal_variant.vehicle_set = 0;
#ifndef HALO_RELEASE
	/* (the tests: short games) */
	variant->universal_variant.score_to_win =
		bots_playlist_test_number("HALO_TEST_BOT_PLAYLIST_SCORE", variant->universal_variant.score_to_win);
#endif
	csmemset(variant->human_readable_game_description, 0, sizeof(variant->human_readable_game_description));
	ustrncpy(variant->human_readable_game_description, gametype->title,
		NUMBEROF(variant->human_readable_game_description) - 1);
}

/* the playlist plays in this game: a local game with bots wanted, the
setting on, not stopped */
static boolean bots_playlist_plays(void)
{
	return !bots_playlist.stopped && bots_playlist_mode() != _bots_playlist_off &&
		global_network_game_server_get() && network_game_is_splitscreen_local() && bots_wanted_count() > 0;
}

/* B in the countdown (any controller's, as it goes down) */
static boolean bots_playlist_b_pressed(void)
{
	short gamepad_index;

	for (gamepad_index = 0; gamepad_index < 4; gamepad_index++)
	{
		struct gamepad_state const *state = input_get_gamepad_state(gamepad_index);

		if (state && state->buttons[_gamepad_analog_button_b] == 1)
			return TRUE;
	}
	return FALSE;
}

/* B: the countdown stopped, the map choice up (as after a game with the
playlist off), the playlist off until the players leave Split Screen */
static void bots_playlist_stop(void)
{
	struct network_game_server *server = global_network_game_server_get();
	struct game_variant_options options;

	/* (the B taken from the menus' events: the map choice put up below saw
	it pressed and left Split Screen) */
	event_manager_flush();
	bots_playlist.stopped = TRUE;
	bots_playlist.state = _bots_playlist_state_idle;
	bots_playlist.set_up = FALSE;
	if (server)
	{
		network_game_server_port_stop_countdown(server);
		/* (the game's own options again: no playlist time limit) */
		game_variant_options_default(&bots_playlist.variant, &options);
		network_game_server_port_set_variant_options(server, &options);
	}
	ui_widgets_close_all();
	ui_widget_load_by_name_or_tag(BOTS_PLAYLIST_MAP_CHOICE_SCREEN, NONE, NULL, NONE, NONE, NONE, NONE);
	platform_log("bots playlist: stopped (B in the lobby): the map choice, the playlist off until Split Screen is left");
}

/* ---------- public code */

void bots_playlist_before_pregame(void)
{
	char const *current;
	short mode = bots_playlist_mode();
	short game_index;
	struct bots_playlist_game *game;
	char level[64];

	bots_playlist.next = NONE;
	bots_playlist.set_up = FALSE;
	if (!bots_playlist_plays() || !game_engine_running())
		return;
	if (!bots_playlist.made || bots_playlist.made_mode != mode)
		bots_playlist_make(mode);
	/* (the game just played: not again first) */
	current = main_get_multiplayer_map_name();
	if (current && current[0])
		snprintf(bots_playlist.last_map, sizeof(bots_playlist.last_map), "%s", tag_name_strip_path(current));
	game_index = bots_playlist_next_game();
	if (game_index == NONE)
	{
		platform_log("bots playlist: no game to play: the players pick the next");
		bots_playlist.state = _bots_playlist_state_idle;
		return;
	}
	game = &bots_playlist.games[game_index];
	bots_playlist_build_variant(game, &bots_playlist.variant);
	snprintf(level, sizeof(level), BOTS_PLAYLIST_LEVEL_FORMAT, game->map, game->map);
	game_engine_port_set_stage(level, &bots_playlist.variant);
	player_ui_set_game_variant(&bots_playlist.variant);
	snprintf(bots_playlist.last_map, sizeof(bots_playlist.last_map), "%s", game->map);
	bots_playlist.next = game_index;
	bots_playlist.state = _bots_playlist_state_set;
	platform_log("bots playlist: next game %ld: %s %s%s", bots_playlist.played + 1, game->map,
		bots_playlist_gametypes[game->gametype].name, game->vehicles ? "+vehicles" : "");
}

int bots_playlist_pregame_ui(void)
{
	struct network_game_server *server = global_network_game_server_get();
	struct game_variant_options options;
	struct bots_playlist_game *game;
	char level[64];

	if (bots_playlist.state != _bots_playlist_state_set || bots_playlist.next == NONE || !server ||
		!bots_playlist_plays() || network_game_server_get_state(server, NULL) != _bots_playlist_server_pregame)
	{
		return FALSE;
	}
	game = &bots_playlist.games[bots_playlist.next];
	snprintf(level, sizeof(level), BOTS_PLAYLIST_LEVEL_FORMAT, game->map, game->map);
	/* (as the map choice does: the next map precached in the lobby) */
	main_set_multiplayer_map_name(level);
	game_variant_options_default(&bots_playlist.variant, &options);
	options.time_limit = BOTS_PLAYLIST_TIME_LIMIT;
#ifndef HALO_RELEASE
	options.time_limit = (short)bots_playlist_test_number("HALO_TEST_BOT_PLAYLIST_MINUTES", options.time_limit);
#endif
	network_game_server_port_set_variant_options(server, &options);
	if (!ui_widget_load_by_name_or_tag(BOTS_PLAYLIST_PREGAME_SCREEN, NONE, NULL, NONE, NONE, NONE, NONE))
	{
		platform_log("bots playlist: the lobby's screen did not load: the map choice");
		bots_playlist.state = _bots_playlist_state_idle;
		return FALSE;
	}
	bots_playlist.state = _bots_playlist_state_lobby;
	bots_playlist.seconds = 0.0f;
	bots_playlist.set_up = TRUE;
	return TRUE;
}

int bots_playlist_set_up(char const *map_name)
{
	char level[64];

	if (!bots_playlist.set_up || bots_playlist.next == NONE || !map_name || !network_game_is_splitscreen_local())
		return FALSE;
	snprintf(level, sizeof(level), BOTS_PLAYLIST_LEVEL_FORMAT, bots_playlist.games[bots_playlist.next].map,
		bots_playlist.games[bots_playlist.next].map);
	return !csstrcasecmp(level, map_name);
}

void bots_playlist_update(int main_menu_loaded, float seconds)
{
	struct network_game_server *server = global_network_game_server_get();
	float scores_seconds = BOTS_PLAYLIST_SCORES_SECONDS;

	/* (Split Screen left: all anew the next time, the file read again) */
	if (!server || !network_game_is_splitscreen_local())
	{
		if (bots_playlist.made || bots_playlist.stopped || bots_playlist.state != _bots_playlist_state_idle)
		{
			if (bots_playlist.played)
				platform_log("bots playlist: Split Screen left after %ld games", bots_playlist.played);
			bots_playlist.made = FALSE;
			bots_playlist.stopped = FALSE;
			bots_playlist.state = _bots_playlist_state_idle;
			bots_playlist.next = NONE;
			bots_playlist.set_up = FALSE;
			bots_playlist.played = 0;
			bots_playlist.last_map[0] = 0;
		}
		return;
	}
#ifndef HALO_RELEASE
	scores_seconds = (float)bots_playlist_test_number("HALO_TEST_BOT_PLAYLIST_SCORES", (long)scores_seconds);
#endif
	switch (bots_playlist.state)
	{
	case _bots_playlist_state_idle:
	case _bots_playlist_state_scores:
		/* the scores shown a while: back to the lobby, as the host's A
		there (before_pregame sets the next game up) */
		if (bots_playlist_plays() && game_engine_running() && game_engine_showing_postgame() &&
			network_game_server_get_state(server, NULL) == _bots_playlist_server_postgame)
		{
			if (bots_playlist.state != _bots_playlist_state_scores)
			{
				bots_playlist.state = _bots_playlist_state_scores;
				bots_playlist.seconds = 0.0f;
				platform_log("bots playlist: the scores; the next game in %.0f s", scores_seconds);
			}
			bots_playlist.seconds += seconds;
			if (bots_playlist.seconds >= scores_seconds)
				network_game_server_reset_to_pregame(server);
		}
		else
			bots_playlist.state = _bots_playlist_state_idle;
		break;
	case _bots_playlist_state_set:
		/* (the lobby's screen comes with the client's switch: pregame_ui) */
		if (network_game_server_get_state(server, NULL) != _bots_playlist_server_pregame)
			bots_playlist.state = _bots_playlist_state_idle;
		break;
	case _bots_playlist_state_lobby:
	case _bots_playlist_state_countdown:
		if (network_game_server_playing(server))
		{
			bots_playlist.played++;
			bots_playlist.state = _bots_playlist_state_idle;
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
			if (bots_playlist.test_holding_b)
			{
				test_input_hold_b(0);
				bots_playlist.test_holding_b = FALSE;
			}
#endif
			platform_log("bots playlist: game %ld started", bots_playlist.played);
			break;
		}
		if (!main_menu_loaded || network_game_server_get_state(server, NULL) != _bots_playlist_server_pregame)
			break;
		bots_playlist.seconds += seconds;
		/* B in the lobby or its countdown: the playlist stops */
		if (bots_playlist_b_pressed())
		{
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
			if (bots_playlist.test_holding_b)
			{
				test_input_hold_b(0);
				bots_playlist.test_holding_b = FALSE;
			}
#endif
			bots_playlist_stop();
			break;
		}
		if (bots_playlist.state == _bots_playlist_state_lobby)
		{
			/* (the bots in, every machine with a player: the countdown) */
			if (bots_playlist.seconds >= BOTS_PLAYLIST_LOBBY_SECONDS && network_game_server_port_start_countdown(server))
			{
				bots_playlist.state = _bots_playlist_state_countdown;
				bots_playlist.seconds = 0.0f;
				platform_log("bots playlist: the countdown (B stops the playlist)");
			}
			break;
		}
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
		/* (the tests: HALO_TEST_BOT_PLAYLIST_CANCEL=N holds B two seconds into
		the Nth playlist game's countdown) */
		if (!bots_playlist.test_holding_b && bots_playlist.seconds >= 2.0f &&
			bots_playlist_test_number("HALO_TEST_BOT_PLAYLIST_CANCEL", 0) == bots_playlist.played + 1)
		{
			test_input_hold_b(1);
			bots_playlist.test_holding_b = TRUE;
			platform_log("bots playlist: (test) B held");
		}
#endif
		break;
	}
}
