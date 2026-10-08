/*
PORT_CONFIG.C

The native ports' settings (port_config.h), parsed with tomlc17
(port/third_party/tomlc17). Every setting is in the table below with its
type, default, the HALO_* environment variable that overrides it and the
comment written into a new file. The file is read once, on the first
question; unknown keys and values of the wrong type are reported in the log
and the defaults used instead. The file is changed only where it must be,
so that the player's edits and comments stay: a setting new in this version
added in its section, a setting the game writes (config_write_boolean) on
its line, and a file that does not read as TOML put right, the file as it
was kept beside it (config_repair). Nothing that would not read back is
ever written.
*/

#include "platform.h"
#include "port_config.h"
#include "tomlc17.h"
#ifdef HALO_NOT_DESKTOP
#include "posix.h"
#endif

#include <SDL3/SDL.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* bumped when a setting held in the environment changes mid-game (the
Vita's settings panel): the port's quality knobs that cache their
variable read it again when this differs from what they last saw */
volatile unsigned long halo_settings_generation;

/* ---------- the settings */

enum config_type
{
	_config_boolean,
	_config_integer,
	_config_real,
	_config_string,
};

/* how the setting's environment variable sets it */
enum config_environment
{
	/* the variable's text is the value ("0", "false", "no" and "off" are
	false for a boolean) */
	_environment_value,
	/* the variable being set at all makes it true */
	_environment_set_is_true,
	/* the variable being set at all makes it false */
	_environment_set_is_false,
};

/* the builds a setting means something in, and is written for */
enum
{
	_platform_desktop = 1,
	_platform_android = 2,
	_platform_all = _platform_desktop | _platform_android,
};

struct config_setting
{
	const char *name;
	enum config_type type;
	/* as it is written in the file */
	const char *default_value;
	const char *environment;
	enum config_environment environment_style;
	unsigned platforms;
	const char *comment;
};

/* co-op's defaults: on the Vitas (and the Linux build standing in for one,
HALO_NET_AS_VITA) the host runs the campaign's AI and scripts for everyone
at a Vita's speed, so four players and no extra enemies */
#if defined(HALO_VITA) || defined(HALO_NET_AS_VITA)
#define COOP_PLAYERS_DEFAULT "4"
#define COOP_ENEMIES_MODE_DEFAULT "\"none\""
#else
#define COOP_PLAYERS_DEFAULT "16"
#define COOP_ENEMIES_MODE_DEFAULT "\"per_player\""
#endif

static const struct config_setting config_settings[] =
{
	{ "display.fullscreen", _config_boolean, "true", "HALO_FULLSCREEN", _environment_value, _platform_desktop,
		"Start fullscreen, drawing at the display's resolution and shape; false\n"
		"starts in a window, which draws the Xbox's 640x480. F11 switches." },
	{ "display.window_scale", _config_integer, "2", "HALO_WINDOW_SCALE", _environment_value, _platform_desktop,
		"The window's size as a multiple of 640x480 (it can be resized)." },
	{ "display.screen_width", _config_integer, "0", "HALO_SCREEN_WIDTH", _environment_value, _platform_android,
		"Columns of the 480-line picture: 0 for the display's shape, 640 for the\n"
		"Xbox's 4:3." },
	{ "display.vsync", _config_boolean, "true", "HALO_NO_VSYNC", _environment_set_is_false, _platform_all,
		"Wait for the display between frames; false draws as fast as possible." },
	{ "display.interpolation", _config_boolean, "true", "HALO_INTERPOLATION", _environment_value, _platform_all,
		"Draw a frame for every display refresh, blending between the game's 30\n"
		"ticks a second; false keeps the original 30 frames a second." },

	{ "audio.enabled", _config_boolean, "true", "HALO_NO_AUDIO", _environment_set_is_false, _platform_all,
		"Play sound." },
	{ "audio.volume", _config_real, "1.0", "HALO_VOLUME", _environment_value, _platform_all,
		"The volume of everything, 0.0 to 1.0." },

	{ "input.mouse_sensitivity", _config_real, "1.0", "HALO_MOUSE_SENSITIVITY", _environment_value, _platform_desktop,
		"How far the view turns for the mouse's movement." },
	{ "input.invert_mouse", _config_boolean, "false", "HALO_MOUSE_INVERT", _environment_set_is_true, _platform_desktop,
		"Moving the mouse forward looks down." },

	{ "game.console_log", _config_string, "\"important\"", "HALO_CONSOLE_LOG", _environment_value, _platform_all,
		"What the game's console shows on screen of what it logs: \"important\"\n"
		"(bans, players dropped for cheating, what refuses a command, and the\n"
		"asserts that stop the game), \"all\" (every line, the game's own\n"
		"chatter too), or \"none\" (the asserts that stop the game only). What\n"
		"a command prints shows whatever this is, and debug.txt has every line." },
	{ "game.custom_edition", _config_boolean, "false", "HALO_CUSTOM_EDITION", _environment_set_is_true, _platform_all,
		"Experimental: Halo Custom Edition (PC) maps in the maps folder are offered in\n"
		"the multiplayer map list and run (docs/custom_edition_caches.md). Modded Xbox\n"
		"maps need no setting." },

	{ "game.language", _config_string, "\"\"", "HALO_LANGUAGE", _environment_value, _platform_all,
		"The language the game asks the Xbox for: \"ja\", \"de\", \"fr\", \"es\" or \"it\";\n"
		"empty for English; \"auto\" (the Vita's) the system's. The game data decides\n"
		"what is translated: a PAL disc's maps_es (maps_fr ...) folder beside maps is\n"
		"read in that language. Read once, at start." },

	{ "paths.data", _config_string, "\"\"", "HALO_DATA_ROOT", _environment_value, _platform_desktop,
		"The folder holding the game data's maps folder; empty looks in the\n"
		"working directory and its assets folder. Windows paths are easiest in\n"
		"single quotes: 'C:\\Games\\Halo'." },
	{ "paths.saves", _config_string, "\"\"", "HALO_SAVE_ROOT", _environment_value, _platform_desktop,
		"Where saved games and profiles go; empty for the usual place\n"
		"(~/.local/share/halo-linux, or %APPDATA%\\halo on Windows)." },

	{ "network.address", _config_string, "\"\"", "HALO_NET_ADDRESS", _environment_value, _platform_all,
		"This machine's IPv4 address for system link, for a machine on several\n"
		"networks; empty chooses one." },
	{ "network.broadcast", _config_string, "\"\"", "HALO_NET_BROADCAST", _environment_value, _platform_all,
		"Comma-separated IPv4 addresses system link sends its announcements to\n"
		"instead of the local network's broadcast address (for VPNs); empty for\n"
		"the local network." },
	{ "network.netcode", _config_string, "\"distributed\"", "HALO_NETCODE", _environment_value, _platform_all,
		"The netcode: \"distributed\" (port/linux/NETCODE.md) predicts each player's\n"
		"own moves and lets the host decide the rest. It is the only one network\n"
		"version 9 plays; the setting is kept so older settings files load." },
	{ "network.online", _config_boolean, "true", "HALO_NET_ONLINE", _environment_value, _platform_all,
		"Internet play: hosting makes an invite link (logged, and put on the\n"
		"clipboard) that lets whoever has it join over the internet; opening a\n"
		"link (or copying one before switching to the game) joins. Only people\n"
		"with the invite can join. Off keeps system link to the local network." },
	{ "network.join_from_clipboard", _config_boolean, "true", "HALO_NET_JOIN_FROM_CLIPBOARD", _environment_value,
		_platform_all,
		"Join the game of an invite link found on the clipboard when the game\n"
		"comes to the front." },
	{ "network.tunnel_port", _config_integer, "0", "HALO_NET_TUNNEL_PORT", _environment_value, _platform_all,
		"The UDP port internet play uses; 0 picks one. A fixed one can be\n"
		"forwarded on the router, for networks whose NAT stops connections." },
	{ "network.allow_upnp", _config_boolean, "true", "HALO_NET_ALLOW_UPNP", _environment_value, _platform_all,
		"Let internet play ask the router (UPnP) to forward its port, for\n"
		"networks whose NAT stops connections: when a player joins this\n"
		"machine's game, and when joining a game takes too long. False never\n"
		"asks." },
	{ "network.brokers_file", _config_string,
#ifdef HALO_VITA
		"\"app0:brokers.txt\"",
#else
		"\"brokers.txt\"",
#endif
		"HALO_NET_BROKERS_FILE", _environment_value, _platform_all,
		"The file of the public MQTT brokers through which the machines of an\n"
		"invite find each other (its messages are encrypted) and the server\n"
		"browser's listings go, beside this file unless a full path (on the\n"
		"Vita, the one in the game's package): one host:port on each line, up\n"
		"to 4. Updates replace brokers.txt: keep a list of your own under\n"
		"another name." },
	{ "network.signalling_brokers", _config_string, "\"\"", "HALO_NET_BROKERS", _environment_value, _platform_all,
		"Comma-separated host:port brokers in place of network.brokers_file's\n"
		"(the automated tests' own); empty for the file's." },
	{ "network.relays_file", _config_string,
#ifdef HALO_VITA
		"\"app0:relays.txt\"",
#else
		"\"relays.txt\"",
#endif
		"HALO_NET_RELAYS_FILE", _environment_value, _platform_all,
		"The file of the relays (port/relay) that carry internet play between\n"
		"two machines whose NATs keep them from reaching each other directly,\n"
		"beside this file unless a full path (on the Vita, the one in the\n"
		"game's package, empty: none): one host:port on each line, up to 2. The\n"
		"relay passes the tunnel's packets on, still encrypted; a direct\n"
		"connection is always tried first and preferred." },
	{ "network.relays", _config_string, "\"\"", "HALO_NET_RELAYS", _environment_value, _platform_all,
		"Comma-separated host:port relays in place of network.relays_file's;\n"
		"empty for the file's." },
	{ "network.allow_relay", _config_boolean, "true", "HALO_NET_ALLOW_RELAY", _environment_value, _platform_all,
		"Let internet play go through a relay (this machine's, or the other\n"
		"one's) when no direct connection can be made. False never uses or\n"
		"offers one." },
	{ "network.coop_level", _config_string, "\"\"", "HALO_NET_COOP_LEVEL", _environment_value, _platform_all,
		"Co-op over the network: a campaign level's short name (\"a10\" ...\n"
		"\"d40\") makes every game this machine hosts co-op on that level, its\n"
		"next round the campaign's next level; empty hosts the lobby's\n"
		"multiplayer game. For tests: the Vita hosts co-op from the Campaign\n"
		"menu (Y on the difficulty screen)." },
	{ "network.coop_difficulty", _config_integer, "1", "HALO_NET_COOP_DIFFICULTY", _environment_value, _platform_all,
		"The difficulty of the co-op games this machine hosts (network.coop_level):\n"
		"0 easy, 1 normal, 2 heroic, 3 legendary." },
	{ "network.coop_players", _config_integer, COOP_PLAYERS_DEFAULT, "HALO_NET_COOP_PLAYERS", _environment_value,
		_platform_all,
		"The most players a co-op game this machine hosts takes (2 to the\n"
		"build's maximum). The host runs the campaign's AI and scripts for\n"
		"everyone: the Vita's default is 4." },
	{ "network.coop_enemies_mode", _config_string, COOP_ENEMIES_MODE_DEFAULT, "HALO_NET_COOP_ENEMIES_MODE",
		_environment_value, _platform_all,
		"Online co-op's extra enemies: \"none\", \"per_player\" (each squad of\n"
		"enemies grows by coop_enemies for each player past the first) or\n"
		"\"multiplier\" (each is coop_enemies_multiplier times as large, for any\n"
		"number of players). Server Setup's EXTRA ENEMIES in co-op writes its\n"
		"choice here." },
	{ "network.coop_enemies", _config_integer, "50", "HALO_NET_COOP_ENEMIES", _environment_value, _platform_all,
		"Online co-op's extra enemies per player, a percentage: for each player\n"
		"past the first, each squad of enemies a level places gets this much of\n"
		"itself more (100: as many again; 25 to 200). Server Setup's PER PLAYER\n"
		"in co-op writes its choice here." },
	{ "network.coop_enemies_multiplier", _config_integer, "2", "HALO_NET_COOP_ENEMIES_MULTIPLIER", _environment_value,
		_platform_all,
		"Online co-op's static multiplier of its enemies: each squad of enemies\n"
		"a level places is this many times as large (2 to 32). Server Setup's\n"
		"MULTIPLIER in co-op writes its choice here." },
	{ "network.stun_servers", _config_string, "\"stun.l.google.com:19302,stun.cloudflare.com:3478\"",
		"HALO_NET_STUN", _environment_value, _platform_all,
		"Public STUN servers that tell this machine its internet address;\n"
		"comma-separated host:port." },
	{ "network.resolver_cache_file", _config_string, "\"dns_cache.txt\"", "HALO_NET_RESOLVER_CACHE",
		_environment_value, _platform_all,
		"Where internet play keeps the last good address of each broker, STUN\n"
		"server and relay it looked up (only the names in these settings and\n"
		"their files), to use while looking one up fails (beside this file\n"
		"unless a full path); empty keeps them in memory only. Safe to delete." },
	{ "network.adhoc", _config_boolean, "false", "HALO_NET_ADHOC", _environment_value, _platform_all,
		"Ad hoc play: system link with the machines of this machine's ad hoc\n"
		"group (the Vita's wireless group without a router, joined from its\n"
		"settings panel), carried as internet play carries it, with or without\n"
		"network.online; nothing goes to the internet." },
	{ "network.host_public", _config_boolean, "true", "HALO_NET_HOST_PUBLIC", _environment_value, _platform_all,
		"Whether a game this machine hosts for internet play is public (listed in\n"
		"everyone's server browser: anyone can find and join it) or, false,\n"
		"private (only those with its code or invite link can join); the Vita's\n"
		"settings panel says for each game (OpenCE's setting)." },
	{ "network.coop_public", _config_boolean, "false", "HALO_NET_COOP_PUBLIC", _environment_value, _platform_all,
		"Whether a co-op game this machine hosts for internet play is public\n"
		"(listed in everyone's server browser) or, false, private (joined by\n"
		"its code or invite link): its own, which network.host_public (the\n"
		"other games') never changes. X on the waiting screen of co-op hosted\n"
		"from the campaign's menus writes its choice here (OpenCE's setting)." },
	{ "network.public_lobby", _config_boolean, "true", "HALO_NET_PUBLIC_LOBBY", _environment_value, _platform_all,
		"The server browser: public games are listed through the signalling\n"
		"brokers, and the settings panel's Browse public games shows them.\n"
		"False lists no game of this machine's and shows none." },
	{ "network.lobby_name", _config_string, "\"\"", "HALO_NET_LOBBY_NAME", _environment_value, _platform_all,
		"The name the server browser and the System Link list show for this\n"
		"machine's games (printable ASCII, the first 15 characters in the\n"
		"System Link list); empty for the game's own name (else \"Halo\"). The\n"
		"Vita's settings panel (Multiplayer, Play, Lobby name) sets it." },
	{ "network.max_players", _config_integer, "0", "HALO_NET_MAX_PLAYERS", _environment_value, _platform_all,
		"The most players a game this machine hosts takes (2 to the build's\n"
		"maximum; 0 for the build's maximum). A co-op game takes\n"
		"network.coop_players. The Vita's settings panel (Multiplayer, Play,\n"
		"Max players) sets it." },
	{ "bots.count", _config_integer, "0", "HALO_BOTS", _environment_value, _platform_all,
		"Computer players (bots) in a local (split screen) multiplayer game:\n"
		"0 to 15 join its lobby with the players, as far as the game has room.\n"
		"Not in system link or internet games. The Vita's settings panel\n"
		"(Multiplayer, Bots) sets it." },
	{ "bots.skill", _config_string, "\"normal\"", "HALO_BOT_SKILL", _environment_value, _platform_all,
		"How well the bots play: \"easy\", \"normal\", \"heroic\" or \"legendary\"\n"
		"(how soon they react, how well they aim, how far they see)." },
	{ "bots.teams", _config_string, "\"even\"", "HALO_BOT_TEAMS", _environment_value, _platform_all,
		"In team games: \"even\" puts the bots on both teams, evening them with\n"
		"the players; \"against\" puts them all on the team the players are not on." },
	{ "network.lobby_password", _config_string, "\"\"", "HALO_NET_LOBBY_PASSWORD", _environment_value, _platform_all,
		"A password for the public games this machine hosts: the server browser\n"
		"lets only those who know it join (the code and invite link still do);\n"
		"empty for none." },
	{ "network.map_downloads", _config_string, "\"ask\"", "HALO_MAP_SHARE_FROM", _environment_value, _platform_all,
		"A host's custom map, in a game joined without it: \"ask\" asks whether\n"
		"to download it (in a game joined from the public lobby, with a warning:\n"
		"its host is a stranger), \"private\" asks except in public lobby games,\n"
		"\"never\" never offers one." },
	{ "network.latency_meter", _config_boolean, "true", "HALO_LATENCY_METER", _environment_value, _platform_all,
		"Show the round trip to the host in a network game, in milliseconds,\n"
		"at the screen's top right (the host: its slowest player's), and\n"
		"\"Connection problem\" when the host has sent nothing for two seconds." },
	{ "network.player_name", _config_string, "\"\"", "HALO_NET_PLAYER_NAME", _environment_value, _platform_all,
		"The name a player goes by in network games when their profile has none\n"
		"(the default profiles); the Vita's user name on the Vita; empty for\n"
		"\"Player\". A profile's own name always comes first." },
	{ "discord.application_id", _config_string, "\"1553978809840050229\"", "HALO_DISCORD_APPLICATION",
		_environment_value, _platform_desktop,
		"The Discord application internet play invites go through while the\n"
		"Discord desktop client runs; empty for none." },

	{ "update.auto", _config_boolean, "true", "HALO_UPDATE_AUTO", _environment_value, _platform_all,
		"Look for a new version when the game starts, and offer to update to it;\n"
		"false never looks (the game's \"Do not ask again\" writes false here)." },

	{ "debug.network_test", _config_string, "\"\"", "HALO_NETWORK_TEST", _environment_value, _platform_all,
		"Automated system link sessions for testing (port/linux/game/network_test.c):\n"
		"\"host:<map>\" hosts a game on that map, \"join\" joins the first game found,\n"
		"\"join-public\" joins the first game of internet play's public lobby,\n"
		"\"join-code:ABCD-EFGH\" the game of that code; empty for none." },
	{ "debug.network_test_start", _config_real, "15.0", "HALO_NETWORK_TEST_START", _environment_value, _platform_all,
		"Seconds after hosting that an automated test game starts." },
	{ "debug.network_test_kill", _config_real, "0.0", "HALO_NETWORK_TEST_KILL", _environment_value, _platform_all,
		"Every this many seconds an automated test host kills its last player; 0 never." },
	{ "debug.network_test_shoot", _config_real, "0.0", "HALO_NETWORK_TEST_SHOOT", _environment_value, _platform_all,
		"Every this many seconds each automated test player hits the next with\n"
		"their weapon; 0 never." },
	{ "debug.network_test_vehicle", _config_real, "0.0", "HALO_NETWORK_TEST_VEHICLE", _environment_value, _platform_all,
		"This many seconds into an automated test game the host seats its last\n"
		"player as a vehicle's driver (and out 15 seconds on); 0 never." },
	{ "debug.network_test_pickup", _config_real, "0.0", "HALO_NETWORK_TEST_PICKUP", _environment_value, _platform_all,
		"This many seconds into an automated test game the host stands its last\n"
		"player on a weapon, which a joining player then picks up; 0 never." },
	{ "debug.network_test_scores", _config_real, "0.0", "HALO_NETWORK_TEST_SCORES", _environment_value, _platform_all,
		"Every this many seconds of an automated test game each machine's player\n"
		"holds Back (the scoreboard) for half of it; 0 never." },
	{ "debug.network_test_pickup_weapon", _config_string, "\"\"", "HALO_NETWORK_TEST_PICKUP_WEAPON", _environment_value,
		_platform_all,
		"The weapon network_test_pickup stands the player on: the first whose tag\n"
		"name has this in it (\"sniper\", say); empty any." },
	{ "debug.network_test_score", _config_integer, "0", "HALO_NETWORK_TEST_SCORE", _environment_value, _platform_all,
		"The score that wins an automated test game (a short game, to test the next\n"
		"one of debug.network_test's list); 0 the variant's own." },
	{ "debug.network_test_rejoin", _config_real, "0.0", "HALO_NETWORK_TEST_REJOIN", _environment_value, _platform_all,
		"Seconds into an automated test game after which a joining machine leaves\n"
		"it (as quitting from the pause menu does) and joins again, once; 0 never." },
	{ "debug.network_test_retry", _config_integer, "0", "HALO_NETWORK_TEST_RETRY", _environment_value, _platform_all,
		"Times a joining machine of an automated test whose join ended before its\n"
		"game began (a map download cut off, refused) joins again; 0 never." },
	{ "debug.network_test_public_name", _config_string, "\"\"", "HALO_NETWORK_TEST_PUBLIC_NAME", _environment_value,
		_platform_all,
		"An automated test's join-public joins only the public game listed with\n"
		"this name (several servers listed); empty the first." },
	{ "debug.telnet_console", _config_boolean, "false", "HALO_TELNET_CONSOLE", _environment_set_is_true, _platform_all,
		"Listen on 127.0.0.1 port 23 (telnet) for a script console that runs what\n"
		"it is sent as the game's console does, with no password; false none." },
	{ "debug.telnet_console_port", _config_integer, "2323", "HALO_TELNET_CONSOLE_PORT", _environment_value,
		_platform_all,
		"The port of the script console (telnet_console); the Xbox's was 23, which\n"
		"only the administrator can listen on." },
	{ "debug.network_latency", _config_real, "0.0", "HALO_NETWORK_LATENCY", _environment_value, _platform_all,
		"Milliseconds everything received is held back (a round trip between two\n"
		"machines of twice it), to test the netcode as over the internet; 0 none." },
	{ "debug.network_loss", _config_real, "0.0", "HALO_NETWORK_LOSS", _environment_value, _platform_all,
		"Percent of datagrams received that are dropped, for the same; 0 none." },
	{ "debug.network_corrupt", _config_real, "0.0", "HALO_NETWORK_CORRUPT", _environment_value, _platform_all,
		"Percent of the datagrams received that are damaged at random, to test\n"
		"that nothing a machine sends can crash the game; 0 none." },
	{ "debug.network_corrupt_stream", _config_real, "0.0", "HALO_NETWORK_CORRUPT_STREAM", _environment_value,
		_platform_all,
		"Percent of the reads of streams that are damaged at random, for the\n"
		"same (a damaged stream is closed, so a little goes a long way); 0 none." },
	{ "debug.network_corrupt_after", _config_real, "0.0", "HALO_NETWORK_CORRUPT_AFTER", _environment_value,
		_platform_all,
		"Seconds after the start before anything is damaged, so that a game can\n"
		"be set up and started first (a host's messages to its own client are\n"
		"damaged too)." },
	{ "debug.test_input", _config_string, "\"\"", "HALO_TEST_INPUT", _environment_value, _platform_all,
		"\"bot:<seed>\" plays controller 1 with a scripted pattern (automated\n"
		"network tests); empty for none." },
	{ "debug.update_answer", _config_string, "\"\"", "HALO_UPDATE_ANSWER", _environment_value, _platform_desktop,
		"The answer to the new version question, for automated tests: \"yes\",\n"
		"\"no\" or \"never\" (do not ask again, confirmed); empty asks." },
	{ "debug.exit_after", _config_real, "0.0", "HALO_EXIT_AFTER", _environment_value, _platform_all,
		"Quit this many seconds after the window opens; 0 never." },
	{ "debug.hidden_window", _config_boolean, "false", "HALO_HIDDEN_WINDOW", _environment_set_is_true, _platform_desktop,
		"Keep the window hidden (and never fullscreen)." },
	{ "debug.null_renderer", _config_boolean, "false", "HALO_NULL_RENDERER", _environment_set_is_true, _platform_all,
		"Run without a window, drawing nothing." },
	{ "debug.gl_debug", _config_boolean, "false", "HALO_GL_DEBUG", _environment_set_is_true, _platform_all,
		"Report OpenGL errors in the log." },
	{ "debug.gpu_stats", _config_boolean, "false", "HALO_GPU_STATS", _environment_set_is_true, _platform_all,
		"Log the renderer's draw counts once a second." },
	{ "debug.gpu_trace_frame", _config_integer, "-1", "HALO_GPU_TRACE", _environment_value, _platform_all,
		"Log every draw of this frame; -1 none." },
	{ "debug.gpu_trace_constants", _config_boolean, "false", "HALO_GPU_TRACE_CONSTANTS", _environment_set_is_true, _platform_all,
		"With gpu_trace_frame, also the vertex shader constants." },
	{ "debug.gpu_skip_vertex_shaders", _config_string, "\"\"", "HALO_GPU_SKIP_VS", _environment_value, _platform_all,
		"Comma-separated ids of vertex shaders not to draw with." },
	{ "debug.gpu_dump_shaders", _config_string, "\"\"", "HALO_GPU_DUMP_SHADERS", _environment_value, _platform_all,
		"A folder to write the generated GLSL to; empty none." },
	{ "debug.gpu_debug_expression", _config_string, "\"\"", "HALO_GPU_DEBUG_EXPR", _environment_value, _platform_all,
		"A GLSL expression every pixel shader shows instead of its result." },
	{ "debug.gpu_debug_texture0", _config_boolean, "false", "HALO_GPU_DEBUG_T0", _environment_set_is_true, _platform_all,
		"Pixel shaders show their first texture." },
	{ "debug.gpu_debug_flat", _config_boolean, "false", "HALO_GPU_DEBUG_FLAT", _environment_set_is_true, _platform_all,
		"Pixel shaders show their vertex colour." },
	{ "debug.screenshot_directory", _config_string, "\"\"", "HALO_SCREENSHOT_DIR", _environment_value, _platform_all,
		"A folder to save frames to (with screenshot_every); empty none." },
	{ "debug.screenshot_every", _config_integer, "0", "HALO_SCREENSHOT_EVERY", _environment_value, _platform_all,
		"Save every this many frames to screenshot_directory; 0 none." },
	{ "debug.texture_dump_directory", _config_string, "\"\"", "HALO_TEXTURE_DUMP", _environment_value, _platform_all,
		"A folder to write every texture to as it is uploaded; empty none." },
	{ "debug.texture_log", _config_boolean, "false", "HALO_TEXTURE_LOG", _environment_set_is_true, _platform_all,
		"Log texture uploads." },
	{ "debug.texture_no_cache", _config_boolean, "false", "HALO_TEXTURE_NO_CACHE", _environment_set_is_true, _platform_all,
		"Upload textures again every time they are used." },
	{ "debug.sample_seconds", _config_real, "0.0", "HALO_SAMPLE", _environment_value, _platform_android,
		"Log where every game thread is this often, in seconds (read by the\n"
		"app, port/android/host/host_debug.c); 0 never." },
};

#define NUMBER_OF_CONFIG_SETTINGS (sizeof(config_settings) / sizeof(config_settings[0]))

#ifdef HALO_NOT_DESKTOP
#define CONFIG_PLATFORM _platform_android
#else
#define CONFIG_PLATFORM _platform_desktop
#endif

struct config_value
{
	int boolean;
	long integer;
	double real;
	char *string;
};

static struct config_value config_values[NUMBER_OF_CONFIG_SETTINGS];
static int config_loaded = 0;
static pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- the file */

static void config_path(char *path, size_t size)
{
#ifdef HALO_NOT_DESKTOP
	/* the data folder, which the app names (port/android/host/host_main.c) */
	const char *root = getenv("HALO_DATA_ROOT");

	snprintf(path, size, "%s/config.toml", root && *root ? root : ".");
#elif defined(HALO_DEDICATED_SERVER)
	/* the dedicated server's folder (-path, which the server sets as the data
	root: posix_dedicated_server.c), not the executable's, which a server's
	service may not write to */
	const char *root = getenv("HALO_DATA_ROOT");

	snprintf(path, size, "%s/config.toml", root && *root ? root : ".");
#else
	/* the executable's folder, with its separator */
	const char *base = SDL_GetBasePath();

	snprintf(path, size, "%sconfig.toml", base ? base : "");
#endif
}

/* the folder config.toml is in, with its separator */
void config_folder(char *path, size_t size)
{
	char *end;

	config_path(path, size);
	end = strrchr(path, '/');
	if (!end)
		end = strrchr(path, '\\');
	if (end)
		end[1] = 0;
	else
		path[0] = 0;
}

/* the whole file, NUL terminated, or NULL; free() it */
static char *config_read_file(const char *path, size_t *size)
{
#ifdef HALO_NOT_DESKTOP
	FILE *file = fopen(path, "rb");
	char *text = NULL;
	long length;

	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) >= 0 && fseek(file, 0, SEEK_SET) == 0)
	{
		text = malloc((size_t)length + 1);
		if (text && fread(text, 1, (size_t)length, file) == (size_t)length)
		{
			text[length] = 0;
			*size = (size_t)length;
		}
		else
		{
			free(text);
			text = NULL;
		}
	}
	fclose(file);
	return text;
#else
	/* SDL's, for UTF-8 paths on Windows */
	void *data = SDL_LoadFile(path, size);
	char *text;

	if (!data)
		return NULL;
	text = malloc(*size + 1);
	if (text)
	{
		memcpy(text, data, *size);
		text[*size] = 0;
	}
	SDL_free(data);
	return text;
#endif
}

static int config_write_file_length(const char *path, const char *text, size_t length)
{
#ifdef HALO_NOT_DESKTOP
	FILE *file = fopen(path, "wb");
	int written;

	if (!file)
		return 0;
	written = fwrite(text, 1, length, file) == length;
	return fclose(file) == 0 && written;
#else
	return SDL_SaveFile(path, text, length);
#endif
}

static int config_write_file(const char *path, const char *text)
{
	return config_write_file_length(path, text, strlen(text));
}

int config_file_write(const char *path, const char *text, size_t size)
{
	return config_write_file_length(path, text, size);
}

/* whether there is a file (or anything) at path */
static int config_file_exists(const char *path)
{
#ifdef HALO_NOT_DESKTOP
	struct posix_file_information information;

	return posix_stat(path, &information) == 0;
#else
	return SDL_GetPathInfo(path, NULL);
#endif
}

struct config_text
{
	char *buffer;
	size_t length, capacity;
};

static void config_append_length(struct config_text *text, const char *string, size_t length)
{
	if (text->length + length + 1 > text->capacity)
	{
		size_t capacity = (text->capacity ? text->capacity : 4096) * 2 + length;
		char *buffer = realloc(text->buffer, capacity);

		if (!buffer)
			return;
		text->buffer = buffer;
		text->capacity = capacity;
	}
	memcpy(text->buffer + text->length, string, length);
	text->length += length;
	text->buffer[text->length] = 0;
}

static void config_append(struct config_text *text, const char *string)
{
	config_append_length(text, string, strlen(string));
}

/* string put into text at offset, what was there moved along */
static void config_insert(struct config_text *text, size_t offset, const char *string)
{
	size_t length = strlen(string);
	size_t before = text->length;

	config_append_length(text, string, length);
	if (text->length != before + length)
		return;
	memmove(text->buffer + offset + length, text->buffer + offset, before - offset);
	memcpy(text->buffer + offset, string, length);
}

/* the first length characters of text, as a string of their own */
static char *config_copy(const char *text, size_t length)
{
	char *copy = malloc(length + 1);

	if (copy)
	{
		memcpy(copy, text, length);
		copy[length] = 0;
	}
	return copy;
}

/* one setting as the file holds it: its comment, and its key at the
default */
static void config_append_setting(struct config_text *text, const struct config_setting *setting)
{
	const char *dot = strchr(setting->name, '.');
	const char *line;
	char buffer[256];

	config_append(text, "\n");
	for (line = setting->comment; *line;)
	{
		size_t length = strcspn(line, "\n");

		snprintf(buffer, sizeof(buffer), "# %.*s\n", (int)length, line);
		config_append(text, buffer);
		line += length;
		if (*line)
			line++;
	}
#ifndef HALO_NOT_DESKTOP
	/* (Android apps have no environment to set) */
	switch (setting->environment_style)
	{
	case _environment_value:
		snprintf(buffer, sizeof(buffer), "# (for one run: %s=<value>)\n", setting->environment);
		break;
	case _environment_set_is_true:
		snprintf(buffer, sizeof(buffer), "# (for one run: %s=1 makes it true)\n", setting->environment);
		break;
	case _environment_set_is_false:
		snprintf(buffer, sizeof(buffer), "# (for one run: %s=1 makes it false)\n", setting->environment);
		break;
	}
	config_append(text, buffer);
#endif
	snprintf(buffer, sizeof(buffer), "%s = %s\n", dot + 1, setting->default_value);
	config_append(text, buffer);
}

/* whether the setting other is in name's section ("network" of
"network.online") */
static int config_same_section(const char *name, const char *other)
{
	const char *dot = strchr(name, '.');
	size_t length = dot ? (size_t)(dot - name) : strlen(name);

	return !strncmp(name, other, length) && other[length] == '.';
}

/* the file with every setting of this build at its default: each section
once, with all its settings in the table's order. (A section's header
written twice makes the whole file unreadable, "table defined more than
once": 1.1's first files had [game] and [debug] twice, the table then
having a debug and a game setting at its start) */
static char *config_default_text(void)
{
	struct config_text text = { NULL, 0, 0 };
	size_t index;

#ifdef HALO_NOT_DESKTOP
	config_append(&text,
		"# Halo settings\n"
		"#\n"
		"# The game writes this file with the defaults when it is missing: delete\n"
		"# it to go back to them.\n");
#else
	config_append(&text,
		"# Halo settings\n"
		"#\n"
		"# The game writes this file with the defaults when it is missing: delete\n"
		"# it to go back to them. Each setting can also be set for one run with\n"
		"# the environment variable named with it, which wins over this file.\n");
#endif
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		const char *dot = strchr(setting->name, '.');
		char buffer[64];
		size_t other;

		if (!(setting->platforms & CONFIG_PLATFORM) || !dot)
			continue;
		/* (the section written already, with this setting) */
		for (other = 0; other < index; other++)
		{
			if ((config_settings[other].platforms & CONFIG_PLATFORM) &&
				config_same_section(setting->name, config_settings[other].name))
			{
				break;
			}
		}
		if (other < index)
			continue;
		snprintf(buffer, sizeof(buffer), "\n[%.*s]\n", (int)(dot - setting->name), setting->name);
		config_append(&text, buffer);
		for (other = index; other < NUMBER_OF_CONFIG_SETTINGS; other++)
		{
			if ((config_settings[other].platforms & CONFIG_PLATFORM) &&
				config_same_section(setting->name, config_settings[other].name))
			{
				config_append_setting(&text, &config_settings[other]);
			}
		}
	}
	return text.buffer;
}

/* the line's key, if it is "key = ..." (after spaces), in key */
static int config_line_key(const char *line, const char *end, const char *key)
{
	size_t length = strlen(key);

	while (line < end && (*line == ' ' || *line == '\t'))
		line++;
	if ((size_t)(end - line) <= length || strncmp(line, key, length) != 0)
		return 0;
	line += length;
	while (line < end && (*line == ' ' || *line == '\t'))
		line++;
	return line < end && *line == '=';
}

/* the section the line opens, if it is "[section]": spaces allowed before
it and inside the brackets ("[ network ]"), anything after. "[[...]]", an
array of tables, opens none of ours; a name too long for section is "\1",
no setting's */
static int config_line_section(const char *line, const char *end, char *section, size_t size)
{
	const char *close;
	size_t length;

	while (line < end && (*line == ' ' || *line == '\t'))
		line++;
	if (line >= end || *line != '[' || (line + 1 < end && line[1] == '['))
		return 0;
	close = memchr(line, ']', (size_t)(end - line));
	if (!close)
		return 0;
	for (line++; line < close && (*line == ' ' || *line == '\t'); line++)
		;
	for (length = (size_t)(close - line); length && (line[length - 1] == ' ' || line[length - 1] == '\t'); length--)
		;
	if (length >= size)
	{
		snprintf(section, size, "\1");
		return 1;
	}
	memcpy(section, line, length);
	section[length] = 0;
	return 1;
}

/* whether text reads as TOML; if not, why in error (when given) */
static int config_text_parses(const char *text, char *error, size_t size)
{
	toml_result_t result = toml_parse(text, (int)strlen(text));
	int parses = result.ok;

	if (!parses && error && size)
		snprintf(error, size, "%s", result.errmsg);
	toml_free(result);
	return parses;
}

/* the settings of this build that text (the file, parsed as table) lacks,
added to it in their sections, keeping the rest as it is: a newer version's
settings appear in an older file. Returns the new text, or NULL if nothing
was missing */
static char *config_add_missing(const char *text, toml_datum_t table)
{
	char *result = NULL;
	size_t index;

	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		const char *dot = strchr(setting->name, '.');
		const char *current = result ? result : text;
		struct config_text block = { NULL, 0, 0 };
		struct config_text updated = { NULL, 0, 0 };
		char section[40], header[48];
		const char *line;
		const char *insert = NULL;

		if (!(setting->platforms & CONFIG_PLATFORM) || !dot || toml_seek(table, setting->name).type != TOML_UNKNOWN)
			continue;
		snprintf(section, sizeof(section), "%.*s", (int)(dot - setting->name), setting->name);
		snprintf(header, sizeof(header), "[%s]", section);
		/* the end of the section's last line that is not blank */
		for (line = current; *line; )
		{
			const char *start = line;
			size_t length = strcspn(line, "\n");
			char name[40];
			int opens = config_line_section(line, line + length, name, sizeof(name));

			while (*start == ' ' || *start == '\t')
				start++;
			if (insert && (opens || *start == '['))
				break;
			if (!insert && opens && !strcmp(name, section))
				insert = line + length;
			else if (insert && start < line + length && *start != '\r')
				insert = line + length;
			line += length;
			if (*line)
				line++;
		}
		if (insert)
		{
			if (*insert)
				insert++;
			config_append_setting(&block, setting);
		}
		else
		{
			/* no such section: a new one at the end */
			insert = current + strlen(current);
			config_append(&block, current[0] && insert[-1] != '\n' ? "\n\n" : "\n");
			config_append(&block, header);
			config_append(&block, "\n");
			config_append_setting(&block, setting);
		}
		if (!block.buffer)
			continue;
		config_append_length(&updated, current, (size_t)(insert - current));
		if (insert > current && insert[-1] != '\n')
			config_append(&updated, "\n");
		config_append(&updated, block.buffer);
		config_append(&updated, insert);
		free(block.buffer);
		if (updated.buffer)
		{
			free(result);
			result = updated.buffer;
			platform_log("settings: added %s (new in this version) at its default", setting->name);
		}
	}
	return result;
}

/* text with the setting name's value (as TOML writes it) changed on its
line, the rest kept as it is: the key's line in its section, or a dotted
key's ("update.auto = true") before any section; when there is none, a
line added after the section's last (a new section at the end when there
is none). NULL when out of memory or name is no "section.key" */
static char *config_text_set(const char *text, const char *name, const char *value)
{
	const char *dot = strchr(name, '.');
	char section[64], current[64], line_text[1024];
	struct config_text out = { NULL, 0, 0 };
	const char *line;
	size_t section_end = 0;
	int written = 0, in_section = 0, any_section = 0;

	if (!dot || (size_t)(dot - name) >= sizeof(section))
		return NULL;
	snprintf(section, sizeof(section), "%.*s", (int)(dot - name), name);
	for (line = text; *line;)
	{
		const char *end = line + strcspn(line, "\n");
		const char *next = *end ? end + 1 : end;
		const char *start = line;

		if (config_line_section(line, end, current, sizeof(current)))
		{
			if (in_section && !written)
			{
				snprintf(line_text, sizeof(line_text), "%s%s = %s\n",
					section_end && out.buffer[section_end - 1] != '\n' ? "\n" : "", dot + 1, value);
				config_insert(&out, section_end, line_text);
				written = 1;
			}
			in_section = !strcmp(current, section);
			any_section = 1;
		}
		else if (!written &&
			((in_section && config_line_key(line, end, dot + 1)) || (!any_section && config_line_key(line, end, name))))
		{
			snprintf(line_text, sizeof(line_text), "%s = %s%s", in_section ? dot + 1 : name, value,
				end > line && end[-1] == '\r' ? "\r\n" : "\n");
			config_append(&out, line_text);
			written = 1;
			line = next;
			continue;
		}
		config_append_length(&out, line, (size_t)(next - line));
		while (start < end && (*start == ' ' || *start == '\t' || *start == '\r'))
			start++;
		if (in_section && start < end)
			section_end = out.length;
		line = next;
	}
	if (!written)
	{
		if (in_section)
		{
			snprintf(line_text, sizeof(line_text), "%s%s = %s\n",
				section_end && out.buffer[section_end - 1] != '\n' ? "\n" : "", dot + 1, value);
			config_insert(&out, section_end, line_text);
		}
		else
		{
			if (out.length && out.buffer[out.length - 1] != '\n')
				config_append(&out, "\n");
			snprintf(line_text, sizeof(line_text), "\n[%s]\n%s = %s\n", section, dot + 1, value);
			config_append(&out, line_text);
		}
	}
	return out.buffer;
}

/* ---------- a file that does not read

Any error in a TOML file makes all of it unreadable, which left every
setting at its default without a word on screen (1.1's files: a section
written twice). It is put right where that can be done, the file as it
was kept beside it as config.toml.broken. */

/* the value text of the setting's line in text (as config_text_set finds
it, the first), or NULL */
static char *config_text_value(const char *text, const char *name)
{
	const char *dot = strchr(name, '.');
	char section[64], current[64];
	const char *line;
	int in_section = 0, any_section = 0;

	if (!dot || (size_t)(dot - name) >= sizeof(section))
		return NULL;
	snprintf(section, sizeof(section), "%.*s", (int)(dot - name), name);
	for (line = text; *line;)
	{
		const char *end = line + strcspn(line, "\n");
		const char *next = *end ? end + 1 : end;

		if (config_line_section(line, end, current, sizeof(current)))
		{
			in_section = !strcmp(current, section);
			any_section = 1;
		}
		else if ((in_section && config_line_key(line, end, dot + 1)) || (!any_section && config_line_key(line, end, name)))
		{
			const char *value = (const char *)memchr(line, '=', (size_t)(end - line)) + 1;

			while (value < end && (*value == ' ' || *value == '\t'))
				value++;
			while (end > value && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
				end--;
			return config_copy(value, (size_t)(end - value));
		}
		line = next;
	}
	return NULL;
}

/* whether value (TOML) reads on its own as a value of the setting's type */
static int config_value_fits(const struct config_setting *setting, const char *value)
{
	struct config_text document = { NULL, 0, 0 };
	toml_result_t result;
	int fits = 0;

	config_append(&document, "value = ");
	config_append(&document, value);
	config_append(&document, "\n");
	if (!document.buffer)
		return 0;
	result = toml_parse(document.buffer, (int)document.length);
	if (result.ok)
	{
		toml_datum_t datum = toml_seek(result.toptab, "value");

		switch (setting->type)
		{
		case _config_boolean: fits = datum.type == TOML_BOOLEAN; break;
		case _config_integer: fits = datum.type == TOML_INT64; break;
		case _config_real: fits = datum.type == TOML_FP64 || datum.type == TOML_INT64; break;
		case _config_string: fits = datum.type == TOML_STRING; break;
		}
	}
	toml_free(result);
	free(document.buffer);
	return fits;
}

/* text with each section that is there more than once made one: the
lines of the later ones moved to the end of the first. NULL if no section
is there twice */
static char *config_merge_sections(const char *text)
{
	struct config_block
	{
		const char *start, *body, *end;
		char name[64];
	} *blocks = NULL;
	size_t count = 0, capacity = 0, index, other;
	const char *line, *preamble_end = NULL;
	struct config_text out = { NULL, 0, 0 };
	int twice = 0;

	for (line = text; *line;)
	{
		const char *end = line + strcspn(line, "\n");
		const char *next = *end ? end + 1 : end;
		char name[64];

		if (config_line_section(line, end, name, sizeof(name)))
		{
			if (count == capacity)
			{
				struct config_block *grown = realloc(blocks, (capacity ? capacity * 2 : 16) * sizeof(*blocks));

				if (!grown)
				{
					free(blocks);
					return NULL;
				}
				blocks = grown;
				capacity = capacity ? capacity * 2 : 16;
			}
			if (count)
				blocks[count - 1].end = line;
			else
				preamble_end = line;
			blocks[count].start = line;
			blocks[count].body = next;
			snprintf(blocks[count].name, sizeof(blocks[count].name), "%s", name);
			count++;
		}
		line = next;
	}
	if (count)
		blocks[count - 1].end = line;
	for (index = 0; index < count && !twice; index++)
	{
		for (other = 0; other < index && !twice; other++)
			twice = !strcmp(blocks[index].name, blocks[other].name);
	}
	if (!twice)
	{
		free(blocks);
		return NULL;
	}
	config_append_length(&out, text, (size_t)(preamble_end - text));
	for (index = 0; index < count; index++)
	{
		for (other = 0; other < index && strcmp(blocks[index].name, blocks[other].name); other++)
			;
		if (other < index)
			continue;
		config_append_length(&out, blocks[index].start, (size_t)(blocks[index].end - blocks[index].start));
		for (other = index + 1; other < count; other++)
		{
			if (strcmp(blocks[index].name, blocks[other].name))
				continue;
			if (out.length && out.buffer[out.length - 1] != '\n')
				config_append(&out, "\n");
			config_append_length(&out, blocks[other].body, (size_t)(blocks[other].end - blocks[other].body));
			platform_log("config.toml: [%s] was there more than once: made one", blocks[index].name);
		}
	}
	free(blocks);
	return out.buffer;
}

/* the defaults with each setting text has a value for that reads on its
own (the first, if it is there twice) */
static char *config_salvage(const char *text, int *read)
{
	char *result = config_default_text();
	size_t index;

	*read = 0;
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS && result; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		char *value;

		if (!(setting->platforms & CONFIG_PLATFORM) || !strchr(setting->name, '.'))
			continue;
		value = config_text_value(text, setting->name);
		if (value && config_value_fits(setting, value))
		{
			char *updated = config_text_set(result, setting->name, value);

			if (updated)
			{
				free(result);
				result = updated;
				(*read)++;
			}
		}
		else if (value)
		{
			platform_log("config.toml: %s = %s does not read; it is back at its default, %s", setting->name, value,
				setting->default_value);
		}
		free(value);
	}
	return result;
}

/* text (the file at path, size bytes) written to config.toml.broken (or
.broken1 ... .broken9 when that is there), its name in backup */
static int config_backup(const char *path, const char *text, size_t size, char *backup, size_t backup_size)
{
	int number;

	for (number = 0; number < 10; number++)
	{
		if (number)
			snprintf(backup, backup_size, "%s.broken%d", path, number);
		else
			snprintf(backup, backup_size, "%s.broken", path);
		if (!config_file_exists(backup))
			break;
	}
	return config_write_file_length(backup, text, size);
}

/* the file at path (text, size bytes), which does not read (error), put
right: its sections that are there twice made one, or else written again
from the defaults with every value of it that reads. The repaired text,
which reads, (written to path when the file as it was could be kept
beside it), or NULL */
static char *config_repair(const char *path, const char *text, size_t size, const char *error)
{
	char *repaired = config_merge_sections(text);
	char how[96], backup[1100];
	int read = 0;

	snprintf(how, sizeof(how), "its sections that were there twice made one");
	if (!repaired || !config_text_parses(repaired, NULL, 0))
	{
		free(repaired);
		repaired = config_salvage(text, &read);
		snprintf(how, sizeof(how), "written again with the defaults and the %d settings of it that read", read);
	}
	if (!repaired || !config_text_parses(repaired, NULL, 0))
	{
		free(repaired);
		return NULL;
	}
	if (!config_backup(path, text, size, backup, sizeof(backup)))
		platform_log("config.toml: %s; read as %s, the file left as it is (%s cannot be written)", error, how, backup);
	else if (!config_write_file(path, repaired))
		platform_log("config.toml: %s; read as %s (kept as %s), but it cannot be written", error, how, backup);
	else
		platform_log("config.toml: %s; repaired: %s. The file as it was is %s", error, how, backup);
	return repaired;
}

/* ---------- values */

static int config_text_is_false(const char *text)
{
	char lower[8];
	size_t index;

	for (index = 0; index + 1 < sizeof(lower) && text[index]; index++)
		lower[index] = (char)tolower((unsigned char)text[index]);
	lower[index] = 0;
	return !strcmp(lower, "0") || !strcmp(lower, "false") || !strcmp(lower, "no") || !strcmp(lower, "off");
}

static void config_set_from_text(struct config_value *value, enum config_type type, const char *text)
{
	switch (type)
	{
	case _config_boolean:
		value->boolean = !config_text_is_false(text);
		break;
	case _config_integer:
		value->integer = strtol(text, NULL, 10);
		break;
	case _config_real:
		value->real = strtod(text, NULL);
		break;
	case _config_string:
		free(value->string);
		value->string = strdup(text);
		break;
	}
}

/* the value in the file, if it is there and of the setting's type */
static void config_set_from_file(struct config_value *value, const struct config_setting *setting,
	toml_datum_t table)
{
	toml_datum_t datum = toml_seek(table, setting->name);
	int wrong_type = 0;

	if (datum.type == TOML_UNKNOWN)
		return;
	switch (setting->type)
	{
	case _config_boolean:
		if (datum.type == TOML_BOOLEAN)
			value->boolean = datum.u.boolean;
		else
			wrong_type = 1;
		break;
	case _config_integer:
		if (datum.type == TOML_INT64)
			value->integer = (long)datum.u.int64;
		else
			wrong_type = 1;
		break;
	case _config_real:
		if (datum.type == TOML_FP64)
			value->real = datum.u.fp64;
		else if (datum.type == TOML_INT64)
			value->real = (double)datum.u.int64;
		else
			wrong_type = 1;
		break;
	case _config_string:
		if (datum.type == TOML_STRING)
		{
			free(value->string);
			value->string = strdup(datum.u.s);
		}
		else
		{
			wrong_type = 1;
		}
		break;
	}
	if (wrong_type)
	{
		static const char *const expected[] = { "true or false", "a whole number", "a number", "a quoted string" };

		platform_log("config.toml line %d: %s should be %s; using %s", datum.lineno, setting->name,
			expected[setting->type], setting->default_value);
	}
}

static long config_setting_index(const char *name)
{
	size_t index;

	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		if (!strcmp(config_settings[index].name, name))
			return (long)index;
	}
	return -1;
}

/* keys in the file that are no setting, likely misspelt */
static void config_report_unknown_keys(toml_datum_t table)
{
	int section_index;

	for (section_index = 0; section_index < table.u.tab.size; section_index++)
	{
		toml_datum_t section = table.u.tab.value[section_index];
		int key_index;

		if (section.type != TOML_TABLE)
		{
			platform_log("config.toml line %d: unknown setting %s", section.lineno, table.u.tab.key[section_index]);
			continue;
		}
		for (key_index = 0; key_index < section.u.tab.size; key_index++)
		{
			char name[128];

			snprintf(name, sizeof(name), "%s.%s", table.u.tab.key[section_index], section.u.tab.key[key_index]);
			if (config_setting_index(name) < 0)
				platform_log("config.toml line %d: unknown setting %s", section.u.tab.value[key_index].lineno, name);
		}
	}
}

static void config_load(void)
{
	char path[1024];
	size_t size = 0;
	char *text;
	size_t index;

	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const char *default_value = config_settings[index].default_value;

		if (config_settings[index].type == _config_string)
		{
			/* written as a TOML basic string without escapes */
			size_t length = strlen(default_value);

			config_values[index].string = length >= 2 ? config_copy(default_value + 1, length - 2) : strdup("");
		}
		else
		{
			config_set_from_text(&config_values[index], config_settings[index].type, default_value);
		}
	}

	config_path(path, sizeof(path));
	text = config_read_file(path, &size);
	if (text)
	{
		toml_result_t result = toml_parse(text, (int)size);

		if (!result.ok)
		{
			char error[sizeof(result.errmsg)];
			char *repaired;

			snprintf(error, sizeof(error), "%s", result.errmsg);
			toml_free(result);
			repaired = config_repair(path, text, size, error);
			if (repaired)
			{
				free(text);
				text = repaired;
				size = strlen(text);
			}
			result = toml_parse(text, (int)size);
		}
		if (result.ok)
		{
			char *completed;
			char error[sizeof(result.errmsg)];

			for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
				config_set_from_file(&config_values[index], &config_settings[index], result.toptab);
			config_report_unknown_keys(result.toptab);
			platform_log("settings: %s", path);
			completed = config_add_missing(text, result.toptab);
			/* (never a file that does not read back) */
			if (completed && !config_text_parses(completed, error, sizeof(error)))
				platform_log("settings: the settings new in this version not added to %s (%s)", path, error);
			else if (completed && !config_write_file(path, completed))
				platform_log("settings: cannot write %s", path);
			free(completed);
		}
		else
		{
			platform_log("config.toml: %s; using the defaults (the file is left as it is)", result.errmsg);
		}
		toml_free(result);
		free(text);
	}
	else if (config_file_exists(path))
	{
		/* (not written over: it may be the player's, there but unreadable now) */
		platform_log("settings: cannot read %s; using the defaults (the file is left as it is)", path);
	}
	else
	{
		char *defaults = config_default_text();

		if (defaults && config_write_file(path, defaults))
			platform_log("settings: wrote the defaults to %s", path);
		else
			platform_log("settings: cannot write %s; using the defaults", path);
		free(defaults);
	}

	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		const char *environment = getenv(setting->environment);

		if (!environment)
			continue;
		switch (setting->environment_style)
		{
		case _environment_value:
			config_set_from_text(&config_values[index], setting->type, environment);
			break;
		case _environment_set_is_true:
			config_values[index].boolean = 1;
			break;
		case _environment_set_is_false:
			config_values[index].boolean = 0;
			break;
		}
	}
}

static const struct config_value *config_value(const char *name, enum config_type type)
{
	static const struct config_value none = { 0, 0, 0.0, "" };
	long index;

	pthread_mutex_lock(&config_lock);
	if (!config_loaded)
	{
		config_load();
		config_loaded = 1;
	}
	pthread_mutex_unlock(&config_lock);
	index = config_setting_index(name);
	if (index < 0 || config_settings[index].type != type)
	{
		platform_log("settings: no %s setting %s", type == _config_string ? "string" : "such", name);
		return &none;
	}
	return &config_values[index];
}

/* ---------- writing a setting */

/* sets a boolean setting, for now and in config.toml: its line there is
changed (or added), the rest of the file kept as it is */
int config_write_boolean(const char *name, int value)
{
	long index = config_setting_index(name);
	char path[1024], error[200];
	size_t size = 0;
	char *text, *updated = NULL;
	int succeeded = 0;

	if (index < 0 || config_settings[index].type != _config_boolean || !strchr(name, '.'))
		return 0;
	/* (the file read first, as the other settings are) */
	config_boolean(name);
	pthread_mutex_lock(&config_lock);
	config_values[index].boolean = value != 0;
	config_path(path, sizeof(path));
	text = config_read_file(path, &size);
	/* (a file gone since the start is written whole again; one there that
	cannot be read is left alone) */
	if (!text && !config_file_exists(path))
		text = config_default_text();
	if (text)
		updated = config_text_set(text, name, value ? "true" : "false");
	if (!updated)
		platform_log("settings: cannot write %s to %s", name, path);
	/* (never a file that does not read back) */
	else if (!config_text_parses(updated, error, sizeof(error)))
		platform_log("settings: %s not written to %s, which would not read (%s)", name, path, error);
	else
		succeeded = config_write_file(path, updated);
	pthread_mutex_unlock(&config_lock);
	free(updated);
	free(text);
	return succeeded;
}

/* sets a setting for this run only, from text as its environment variable
would (the dedicated server's commands: port/linux/game/dedicated_server.c);
the file is left alone. A string's old text is kept, not freed: another
thread may still be reading it (a few bytes, a command at a time). 1 if the
setting exists */
int config_set_override(const char *name, const char *text)
{
	long index = config_setting_index(name);

	if (index < 0 || !text)
		return 0;
	/* (the file read first, so that it does not overwrite this later) */
	config_value(name, config_settings[index].type);
	pthread_mutex_lock(&config_lock);
	if (config_settings[index].type == _config_string)
	{
		char *copy = strdup(text);

		if (copy)
			config_values[index].string = copy;
	}
	else
		config_set_from_text(&config_values[index], config_settings[index].type, text);
	halo_settings_generation++;
	pthread_mutex_unlock(&config_lock);
	return 1;
}

/* ---------- public code */

int config_boolean(const char *name)
{
	return config_value(name, _config_boolean)->boolean;
}

long config_integer(const char *name)
{
	return config_value(name, _config_integer)->integer;
}

double config_real(const char *name)
{
	return config_value(name, _config_real)->real;
}

char *config_file_read(const char *path, size_t *size)
{
	return config_read_file(path, size);
}

const char *config_string(const char *name)
{
	const char *string = config_value(name, _config_string)->string;

	return string ? string : "";
}
