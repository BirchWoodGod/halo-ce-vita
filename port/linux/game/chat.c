/*
CHAT.C

Game chat in network games: quick chat phrases and typed lines, in the
lobby and in the game, with a player's own mute list and the settings
panel's Game chat (HALO_CHAT: on, quick, off; on by default). The rules
every field from the network is checked by are chat_protocol.c's; this is
the game's side. Started from OpenCE PR #72 ("Text chat in games",
smokeyllama), which a host's relaying of a line, named by the host, comes
from; that PR was closed for want of moderation (dedicated servers), which
the limits and the mutes here stand in for.

- A player's line goes to the host as _message_client_chat over this
  machine's own connection to it (network_messages.c), whatever the game's
  state: lobby, game, scores. The line names nobody.
- The host takes a line only over a joined machine's connection, so the
  machine is known, and names its sender itself, from that machine's own
  players in the game's record (the one the request's controller names,
  else its first): a joiner cannot speak as another. It checks the line
  (chat_request_valid), drops a machine's lines past its limit (CHAT_BURST,
  then one every CHAT_REFILL_MILLISECONDS) and its relaying past its own
  (CHAT_HOST_BURST), and passes the line on as _message_server_chat to
  every joined machine, its own local machine included, or, team chat in a
  game with teams, to those with a player of the sender's team.
- The host's own Game chat is its game's (chat_host_notice): Off, it
  passes on no one's lines, Quick chat only, no typed ones, and the sender
  is told so by a notice (_chat_kind_notice, its number only). A player the
  host muted is muted for the game: the host passes on none of their lines.
- Every machine checks the host's line again (chat_relay_valid: a host is
  a stranger too), shows no more than the host's own limit lets through,
  and drops those of the players its player muted, and, with Game chat
  Quick chat only, the typed ones (a quick chat phrase travels as its
  number and shows as this machine's own text), or every one with Off.
- A mute (chat_mutes) holds until Halo is closed (games left and joined),
  by the player's machine and controller in the game's record, so a new
  name does not shake it off, and by name, so leaving and joining again
  does not either.
- What is shown: the last lines, at the window's left above the motion
  sensor, for twelve seconds each (chat_draw, from interface.c's overlays:
  the lobby and the game), "Name: text" ("[Team] Name: text" in green for
  team chat); and a line in halo.log ("chat: ...").

The Vita's menu (Back + Y: the phrases, typing on the system keyboard, team
or all, muting a player: port/vita/host/vita_settings.c) reaches here
through chat_link.h. Elsewhere, (debug) HALO_TEST_CHAT scripts it: steps
"SECONDS:ACTION[:ARGUMENT]" between '|', the seconds since chat first
became available (a lobby joined or hosted): quick:N, teamquick:N, say:TEXT,
team:TEXT, raw:TEXT (sent without this machine's own cleaning, its "\xNN"s
as those bytes: the host's checks), flood:N (N lines at once, past this
machine's own limit: the host's limit), mute:NAME, unmute:NAME ("*": every
other player), mode:on|quick|off (Game chat changed, as the panel does).
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/interface.h"
#include "math/integer_math.h"
#include "networking/network_client_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_messages.h"
#include "networking/network_server_manager_internal.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"

#include "chat.h"
#include "chat_protocol.h"
#include "../src/chat_link.h"

#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* the lines shown at once, and how long each stays (milliseconds, its
	last CHAT_FADE_MILLISECONDS fading) */
	CHAT_LOG_LINES = 6,
	CHAT_SHOW_MILLISECONDS = 12000,
	CHAT_FADE_MILLISECONDS = 1500,
	/* a line of the log: "[Team] " and a name of twelve, ": ", the text */
	CHAT_LINE_BYTES = 8 + CHAT_NAME_BYTES + 2 + CHAT_TEXT_BYTES,
	/* the log's lines are broken at a space before this many characters
	(the terminal font is about 8 pixels a character: a 4:3 window's half
	and more) */
	CHAT_WRAP_CHARACTERS = 60,
	/* a machine's dropped lines the host logs one by one; after that,
	every CHAT_DROP_LOG_EVERY'th */
	CHAT_DROP_LOGS = 4,
	CHAT_DROP_LOG_EVERY = 50,
	/* HALO_TEST_CHAT's steps */
	CHAT_TEST_STEPS = 32,
};

/* network_client_manager.c's client states, in its order */
enum
{
	_client_searching,
	_client_joining,
	_client_pregame,
	_client_ingame,
	_client_postgame,
};

/* network_server_manager.c's server states */
enum
{
	_server_state_pregame,
	_server_state_ingame,
	_server_state_postgame,
};

/* the packet classes of the two messages (network_messages.c's table) */
enum
{
	_packet_class_server_pregame = 2,
	_packet_class_client_pregame = 3,
};

/* the log's lines */
enum
{
	_chat_style_all,
	_chat_style_team,
	/* this machine's own word to its player (a wait, a mute) */
	_chat_style_notice,
};

/* ---------- structures */

struct chat_line
{
	char text[CHAT_LINE_BYTES];
	short style;
	unsigned long time;
};

struct chat_test_step
{
	unsigned long at;
	char action[12];
	char argument[CHAT_TEXT_BYTES];
};

/* (what goes on the wire, as network_messages.c's definitions have it) */
typedef char chat_request_size_assert[sizeof(struct chat_request_message) == 4 * 2 + CHAT_TEXT_BYTES ? 1 : -1];
typedef char chat_relay_size_assert[
	sizeof(struct chat_relay_message) == 6 * 2 + CHAT_NAME_CHARACTERS * 2 + CHAT_TEXT_BYTES ? 1 : -1];
typedef char chat_name_size_assert[sizeof(((struct network_player *)0)->name) == CHAT_NAME_CHARACTERS * 2 ? 1 : -1];

/* ---------- prototypes */

void platform_log(char const *format, ...);
int setenv(const char *name, const char *value, int overwrite);
unsigned long system_milliseconds(void);
boolean network_game_client_send_to_server(struct network_game_client *client, void *message);

/* ---------- globals */

volatile int halo_chat_status[HALO_CHAT_STATUS_COUNT];
char halo_chat_player_names[HALO_CHAT_PLAYERS][HALO_CHAT_NAME_SIZE];
volatile int halo_chat_player_muted[HALO_CHAT_PLAYERS];
volatile int halo_chat_request;
volatile int halo_chat_request_value;
volatile int halo_chat_request_team;
char halo_chat_request_text[HALO_CHAT_TEXT_SIZE];

static struct
{
	/* the log, newest at newest_line, line_count of them */
	struct chat_line lines[CHAT_LOG_LINES];
	short newest_line;
	short line_count;

	/* a lobby or a game to chat in, and since when (test steps) */
	boolean available;
	boolean ever_available;
	unsigned long available_time;

	/* this machine's own lines, and what it shows of its host's */
	struct chat_bucket sent;
	struct chat_bucket shown;
	unsigned long shown_dropped;

	/* the host: each machine's lines (by its slot), and its relaying */
	struct chat_bucket machine_buckets[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	unsigned long machine_dropped[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	/* ... and refused by the host's Game chat or its mutes (logged apart
	from a flood's) */
	unsigned long machine_refused[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	struct chat_bucket relayed;

	/* the players this machine's player muted (on the host: muted for the
	game) */
	struct chat_mutes mutes;

	/* the lines drawn in a lobby, and in a game, since chat became
	available (said once each in halo.log) */
	boolean drawn[2];

	struct chat_test_step test_steps[CHAT_TEST_STEPS];
	short test_step_count;
	short test_next_step;
	boolean test_read;
} chat_globals;

/* ---------- private code */

static short chat_mode(
	void)
{
	char const *setting = getenv("HALO_CHAT");

	if (setting && !strcmp(setting, "off"))
		return HALO_CHAT_MODE_OFF;
	if (setting && !strcmp(setting, "quick"))
		return HALO_CHAT_MODE_QUICK;
	return HALO_CHAT_MODE_ON;
}

/* this machine's client of a network game in its lobby, its game or its
scores (not split screen), or NULL */
static struct network_game_client *chat_client(
	void)
{
	struct network_game_client *client = global_network_game_client_get();
	short state;

	if (!client || network_game_is_splitscreen_local())
		return NULL;
	state = network_game_client_get_state(client, NULL);
	return state >= _client_pregame && state <= _client_postgame ? client : NULL;
}

/* (the host) the joined machine's slot, or NONE */
static long chat_machine_slot(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine)
{
	long slot;

	for (slot = 0; slot < HALO_PORT_MAXIMUM_NETWORK_MACHINES; slot++)
	{
		if (network_game_server_get_client_machine_at_index(server, slot) == machine)
			return slot;
	}
	return NONE;
}

static boolean chat_game_has_teams(
	struct network_game const *game)
{
	return game && game->variant.universal_variant.teams;
}

static short chat_player_count(
	struct network_game const *game)
{
	short count = game ? game->player_count : 0;

	return count < 0 ? 0 : count > HALO_PORT_MAXIMUM_NETWORK_PLAYERS ? HALO_PORT_MAXIMUM_NETWORK_PLAYERS : count;
}

/* this machine's record of the game's player at index, valid, or NULL */
static struct network_player *chat_game_player(
	struct network_game *game,
	long index)
{
	struct network_player *player;

	if (!game || index < 0 || index >= chat_player_count(game))
		return NULL;
	player = &game->players[index];
	return network_player_is_valid(player) ? player : NULL;
}

/* whether the player (NULL: not in this machine's record) of this name is
muted */
static boolean chat_is_muted(
	char const *name,
	struct network_player const *player)
{
	return chat_mutes_match(&chat_globals.mutes, name, player ? player->machine_index : NONE,
		player ? player->controller_index : NONE) != 0;
}

/* whether this machine hosts the game */
static boolean chat_hosting(
	void)
{
	return global_network_game_server_get() != NULL;
}

/* a line into the log, broken at spaces into lines of the log's width */
static void chat_log_add(
	short style,
	char const *text)
{
	unsigned long now = system_milliseconds();
	long length = (long)strlen(text);
	long start = 0;

	while (start < length)
	{
		long end = length;
		struct chat_line *line;

		if (end - start > CHAT_WRAP_CHARACTERS)
		{
			long index;

			end = start + CHAT_WRAP_CHARACTERS;
			for (index = end; index > start + CHAT_WRAP_CHARACTERS / 2; index--)
			{
				if (text[index] == ' ')
				{
					end = index;
					break;
				}
			}
		}
		chat_globals.newest_line = (short)((chat_globals.newest_line + 1) % CHAT_LOG_LINES);
		line = &chat_globals.lines[chat_globals.newest_line];
		/* (a continued line indented) */
		snprintf(line->text, sizeof(line->text), "%s%.*s", start ? "  " : "", (int)(end - start), text + start);
		line->style = style;
		line->time = now;
		if (chat_globals.line_count < CHAT_LOG_LINES)
			chat_globals.line_count++;
		start = end;
		while (start < length && text[start] == ' ')
			start++;
	}
}

static void chat_notice(
	char const *text)
{
	chat_log_add(_chat_style_notice, text);
	platform_log("chat: (to this player) %s", text);
}

/* the controller of this machine's first player in the game (0 if none) */
static short chat_local_controller(
	struct network_game_client *client)
{
	struct network_game *game = network_game_client_get_game(client);
	short count = chat_player_count(game);
	short index;

	for (index = 0; index < count; index++)
	{
		struct network_player *player = &game->players[index];

		if (network_player_is_valid(player) && network_game_player_is_local(player))
			return player->controller_index >= 0 && player->controller_index < 4 ? player->controller_index : 0;
	}
	return 0;
}

/* a line of this machine's player to the host; force: past this machine's
own limit (the test's flood), unclean: the text as it is (the test's) */
static boolean chat_send(
	short kind,
	short phrase,
	boolean team,
	char const *text,
	boolean force,
	boolean unclean)
{
	struct network_game_client *client = chat_client();
	struct chat_request_message request;
	short mode = chat_mode();
	unsigned long now = system_milliseconds();
	void *message;

	if (!client || mode == HALO_CHAT_MODE_OFF || (kind == _chat_kind_typed && mode != HALO_CHAT_MODE_ON))
		return FALSE;
	csmemset(&request, 0, sizeof(request));
	request.kind = kind;
	request.phrase = phrase;
	request.flags = team ? 1 << _chat_flag_team_bit : 0;
	request.local_player = chat_local_controller(client);
	if (kind == _chat_kind_typed)
	{
		if (unclean)
			csstrncpy(request.text, text, sizeof(request.text) - 1);
		else if (!chat_text_clean(request.text, sizeof(request.text), text, CHAT_TEXT_BYTES))
			return FALSE;
		else if (chat_text_has_link(request.text))
		{
			chat_notice("Links can't be sent in chat");
			return FALSE;
		}
	}
	if (!force && !chat_bucket_take(&chat_globals.sent, now, CHAT_BURST, CHAT_REFILL_MILLISECONDS))
	{
		char wait[48];
		unsigned long seconds =
			(chat_bucket_wait(&chat_globals.sent, now, CHAT_BURST, CHAT_REFILL_MILLISECONDS) + 999) / 1000;

		snprintf(wait, sizeof(wait), "Wait %lu s to chat again", seconds ? seconds : 1);
		chat_notice(wait);
		return FALSE;
	}
	message = create_network_game_message(_message_client_chat, &request, sizeof(request));
	if (!message || !network_game_client_send_to_server(client, message))
	{
		platform_log("chat: the line could not be sent");
		return FALSE;
	}
	platform_log("chat: sent a %s%s line (%s)", team ? "team " : "", kind == _chat_kind_quick ? "quick chat" : "typed",
		network_game_client_get_state(client, NULL) == _client_pregame ? "lobby" : "game");
	return TRUE;
}

static void chat_mute(
	char const *name,
	boolean mute)
{
	struct network_game_client *client = chat_client();
	struct network_game *game = client ? network_game_client_get_game(client) : NULL;
	struct network_player const *muted = NULL;
	char clean[CHAT_NAME_BYTES];
	char text[CHAT_NAME_BYTES + 48];
	short count = chat_player_count(game);
	short index;

	/* (the name as the menu listed it, chat_name_clean's: printable ASCII,
	its spaces as they are; the player of that name in the game, by where
	the game has them) */
	for (index = 0; index < CHAT_NAME_BYTES - 1 && name[index]; index++)
	{
		if (name[index] < 0x20 || name[index] > 0x7E)
			return;
		clean[index] = name[index];
	}
	clean[index] = 0;
	if (!clean[0])
		return;
	for (index = 0; index < count && !muted; index++)
	{
		struct network_player const *player = chat_game_player(game, index);
		char player_name[CHAT_NAME_BYTES];

		if (!player || network_game_player_is_local((struct network_player *)player))
			continue;
		chat_name_clean(player_name, sizeof(player_name), (uint16_t const *)player->name, CHAT_NAME_CHARACTERS);
		if (!strcmp(player_name, clean))
			muted = player;
	}
	if (!chat_mutes_set(&chat_globals.mutes, clean, muted ? muted->machine_index : NONE,
		muted ? muted->controller_index : NONE, mute))
	{
		return;
	}
	snprintf(text, sizeof(text), !mute ? "%s can be heard again" : chat_hosting() ? "%s muted for everyone" : "%s muted",
		clean);
	chat_notice(text);
}

/* (debug) HALO_TEST_CHAT's steps, read once */
static void chat_test_read(
	void)
{
	char const *script = getenv("HALO_TEST_CHAT");

	chat_globals.test_read = TRUE;
	while (script && *script && chat_globals.test_step_count < CHAT_TEST_STEPS)
	{
		struct chat_test_step *step = &chat_globals.test_steps[chat_globals.test_step_count];
		char const *end = strchr(script, '|');
		char const *colon;
		long length = end ? (long)(end - script) : (long)strlen(script);
		long action_length;
		long out = 0;
		long index;

		csmemset(step, 0, sizeof(*step));
		step->at = (unsigned long)(atof(script) * 1000.0);
		colon = memchr(script, ':', (size_t)length);
		if (colon)
		{
			char const *argument = memchr(colon + 1, ':', (size_t)(length - (colon + 1 - script)));

			action_length = (argument ? argument : script + length) - (colon + 1);
			if (action_length > (long)sizeof(step->action) - 1)
				action_length = sizeof(step->action) - 1;
			memcpy(step->action, colon + 1, (size_t)action_length);
			/* (the argument, its "\xNN"s as those bytes) */
			for (index = argument ? (long)(argument + 1 - script) : length;
				index < length && out < (long)sizeof(step->argument) - 1; index++)
			{
				if (script[index] == '\\' && index + 3 < length + 0 && script[index + 1] == 'x')
				{
					char hex[3] = { script[index + 2], script[index + 3], 0 };

					step->argument[out++] = (char)strtol(hex, NULL, 16);
					index += 3;
				}
				else
				{
					step->argument[out++] = script[index];
				}
			}
			chat_globals.test_step_count++;
		}
		script = end ? end + 1 : NULL;
	}
	if (chat_globals.test_step_count)
		platform_log("chat: test script of %d steps", chat_globals.test_step_count);
}

static void chat_test_update(
	void)
{
	unsigned long elapsed;

	if (!chat_globals.test_read)
		chat_test_read();
	if (!chat_globals.ever_available)
		return;
	elapsed = system_milliseconds() - chat_globals.available_time;
	while (chat_globals.test_next_step < chat_globals.test_step_count &&
		chat_globals.test_steps[chat_globals.test_next_step].at <= elapsed)
	{
		struct chat_test_step const *step = &chat_globals.test_steps[chat_globals.test_next_step++];

		{
			char shown[CHAT_TEXT_BYTES];

			chat_text_clean(shown, sizeof(shown), step->argument, sizeof(step->argument));
			platform_log("chat: test step %s %s", step->action, shown);
		}
		if (!strcmp(step->action, "quick") || !strcmp(step->action, "teamquick"))
			chat_send(_chat_kind_quick, (short)atoi(step->argument), step->action[0] == 't', NULL, FALSE, FALSE);
		else if (!strcmp(step->action, "say") || !strcmp(step->action, "team"))
			chat_send(_chat_kind_typed, 0, step->action[0] == 't', step->argument, FALSE, FALSE);
		else if (!strcmp(step->action, "raw"))
			chat_send(_chat_kind_typed, 0, FALSE, step->argument, TRUE, TRUE);
		else if (!strcmp(step->action, "flood"))
		{
			long count = atol(step->argument);
			long index;

			for (index = 0; index < count && index < 1000; index++)
			{
				char text[32];

				snprintf(text, sizeof(text), "flood %ld", index + 1);
				chat_send(_chat_kind_typed, 0, FALSE, text, TRUE, FALSE);
			}
		}
		else if ((!strcmp(step->action, "mute") || !strcmp(step->action, "unmute")) && !strcmp(step->argument, "*"))
		{
			/* (every other player the menu lists) */
			int index;

			for (index = 0; index < halo_chat_status[HALO_CHAT_STATUS_PLAYERS] && index < HALO_CHAT_PLAYERS; index++)
				chat_mute(halo_chat_player_names[index], step->action[0] == 'm');
		}
		else if (!strcmp(step->action, "mute") || !strcmp(step->action, "unmute"))
			chat_mute(step->argument, step->action[0] == 'm');
		else if (!strcmp(step->action, "mode") &&
			(!strcmp(step->argument, "on") || !strcmp(step->argument, "quick") || !strcmp(step->argument, "off")))
		{
			setenv("HALO_CHAT", step->argument, 1);
		}
	}
}

/* what the menu lists: the game's other players, and whether each is muted */
static void chat_link_status(
	struct network_game_client *client,
	short mode)
{
	struct network_game *game = client ? network_game_client_get_game(client) : NULL;
	short count = chat_player_count(game);
	short listed = 0;
	short index;

	/* (the mutes follow the game's players: a new name, a machine gone) */
	if (!client)
		chat_mutes_forget_machine(&chat_globals.mutes, NONE);
	for (index = 0; index < chat_globals.mutes.count; index++)
	{
		short machine = chat_globals.mutes.entries[index].machine;
		short other;

		for (other = 0; machine != NONE && other < count; other++)
		{
			struct network_player *player = chat_game_player(game, other);

			if (player && player->machine_index == machine && !network_game_player_is_local(player))
				break;
		}
		if (machine != NONE && other == count)
		{
			chat_mutes_forget_machine(&chat_globals.mutes, machine);
			index = -1;
		}
	}
	for (index = 0; index < count; index++)
	{
		struct network_player *player = chat_game_player(game, index);
		char name[CHAT_NAME_BYTES];

		if (!player || network_game_player_is_local(player))
			continue;
		chat_name_clean(name, sizeof(name), (uint16_t const *)player->name, CHAT_NAME_CHARACTERS);
		chat_mutes_player(&chat_globals.mutes, player->machine_index, player->controller_index, name);
		if (listed < HALO_CHAT_PLAYERS)
		{
			csmemcpy(halo_chat_player_names[listed], name, HALO_CHAT_NAME_SIZE);
			halo_chat_player_muted[listed] = chat_is_muted(name, player);
			listed++;
		}
	}
	halo_chat_status[HALO_CHAT_STATUS_PLAYERS] = listed;
	halo_chat_status[HALO_CHAT_STATUS_TEAMS] = chat_game_has_teams(game);
	halo_chat_status[HALO_CHAT_STATUS_MODE] = mode;
	halo_chat_status[HALO_CHAT_STATUS_HOST] = client && chat_hosting();
	halo_chat_status[HALO_CHAT_STATUS_WAIT] = (int)chat_bucket_wait(&chat_globals.sent, system_milliseconds(),
		CHAT_BURST, CHAT_REFILL_MILLISECONDS);
	__atomic_store_n(&halo_chat_status[HALO_CHAT_STATUS_AVAILABLE], client && mode != HALO_CHAT_MODE_OFF,
		__ATOMIC_RELEASE);
}

/* (the host) the machine's slot's limit and the host's own, a line counted */
static boolean chat_server_allows(
	long slot)
{
	unsigned long now = system_milliseconds();

	if (!chat_bucket_take(&chat_globals.machine_buckets[slot], now, CHAT_BURST, CHAT_REFILL_MILLISECONDS) ||
		!chat_bucket_take(&chat_globals.relayed, now, CHAT_HOST_BURST, CHAT_HOST_REFILL_MILLISECONDS))
	{
		unsigned long dropped = ++chat_globals.machine_dropped[slot];

		if (dropped <= CHAT_DROP_LOGS || dropped % CHAT_DROP_LOG_EVERY == 0)
			platform_log("chat: the host dropped a line from machine slot %ld: too many (%lu dropped)", slot, dropped);
		return FALSE;
	}
	return TRUE;
}

/* (the host) whether the machine has a player of the team */
static boolean chat_machine_has_team(
	struct network_game const *game,
	long machine_id,
	short team)
{
	short count = chat_player_count(game);
	short index;

	for (index = 0; index < count; index++)
	{
		struct network_player const *player = &game->players[index];

		if (player->machine_index == machine_id && player->team_index == team &&
			network_player_is_valid((struct network_player *)player))
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------- public code */

void chat_update(
	void)
{
	struct network_game_client *client = chat_client();
	short mode = chat_mode();
	int request = __atomic_load_n(&halo_chat_request, __ATOMIC_ACQUIRE);

	if ((client != NULL) != chat_globals.available)
	{
		chat_globals.available = client != NULL;
		if (client)
		{
			platform_log("chat: available (%s)", mode == HALO_CHAT_MODE_ON ? "on" : mode == HALO_CHAT_MODE_QUICK ?
				"quick chat only" : "off");
			if (!chat_globals.ever_available)
				chat_globals.available_time = system_milliseconds();
			chat_globals.ever_available = TRUE;
			chat_globals.drawn[0] = chat_globals.drawn[1] = FALSE;
		}
		else
		{
			/* (a game left: its lines, and the host's counts, go; the mutes
			stay for the run of the game) */
			csmemset(chat_globals.lines, 0, sizeof(chat_globals.lines));
			chat_globals.line_count = 0;
			csmemset(&chat_globals.shown, 0, sizeof(chat_globals.shown));
			csmemset(chat_globals.machine_buckets, 0, sizeof(chat_globals.machine_buckets));
			csmemset(chat_globals.machine_dropped, 0, sizeof(chat_globals.machine_dropped));
			csmemset(chat_globals.machine_refused, 0, sizeof(chat_globals.machine_refused));
			csmemset(&chat_globals.relayed, 0, sizeof(chat_globals.relayed));
		}
	}

	if (request != HALO_CHAT_REQUEST_NONE)
	{
		char text[HALO_CHAT_TEXT_SIZE];

		csmemcpy(text, halo_chat_request_text, sizeof(text));
		text[sizeof(text) - 1] = 0;
		switch (request)
		{
		case HALO_CHAT_REQUEST_QUICK:
			chat_send(_chat_kind_quick, (short)halo_chat_request_value, halo_chat_request_team != 0, NULL, FALSE, FALSE);
			break;
		case HALO_CHAT_REQUEST_TYPED:
			chat_send(_chat_kind_typed, 0, halo_chat_request_team != 0, text, FALSE, FALSE);
			break;
		case HALO_CHAT_REQUEST_MUTE:
		case HALO_CHAT_REQUEST_UNMUTE:
			chat_mute(text, request == HALO_CHAT_REQUEST_MUTE);
			break;
		}
		__atomic_store_n(&halo_chat_request, HALO_CHAT_REQUEST_NONE, __ATOMIC_RELEASE);
	}

	chat_test_update();
	chat_link_status(client, mode);
}

void chat_draw(
	void)
{
	long font_index;
	struct font_header *font;
	unsigned long now = system_milliseconds();
	short line_height;
	short bottom;
	short count;
	short line_index;

	if (!chat_globals.line_count || !chat_globals.available || chat_mode() == HALO_CHAT_MODE_OFF)
		return;
	font_index = interface_get_tag_index(_interface_font_terminal);
	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0)
		return;
	/* (newest at the bottom, above the motion sensor) */
	bottom = (short)(render.camera.window_bounds.y1 -
		(render.camera.window_bounds.y1 - render.camera.window_bounds.y0) * 27 / 100);
	line_index = chat_globals.newest_line;
	for (count = 0; count < chat_globals.line_count; count++)
	{
		struct chat_line const *line = &chat_globals.lines[line_index];
		unsigned long age = now - line->time;
		real_argb_color color;
		rectangle2d bounds;

		/* (older lines are older still) */
		if (age >= CHAT_SHOW_MILLISECONDS)
			break;
		switch (line->style)
		{
		case _chat_style_team:
			color.alpha = 1.0f; color.red = 0.55f; color.green = 1.0f; color.blue = 0.55f;
			break;
		case _chat_style_notice:
			color.alpha = 1.0f; color.red = 0.75f; color.green = 0.75f; color.blue = 0.75f;
			break;
		default:
			color.alpha = 1.0f; color.red = 1.0f; color.green = 1.0f; color.blue = 1.0f;
			break;
		}
		if (age > CHAT_SHOW_MILLISECONDS - CHAT_FADE_MILLISECONDS)
			color.alpha = (real)(CHAT_SHOW_MILLISECONDS - age) / (real)CHAT_FADE_MILLISECONDS;
		bounds.x0 = (short)(render.camera.window_bounds.x0 + 12);
		bounds.x1 = render.camera.window_bounds.x1;
		bounds.y1 = bottom;
		bounds.y0 = (short)(bottom - line_height);
		offset_rectangle2d(&bounds, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
		draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);
		rasterizer_draw_string(&bounds, NULL, NULL, 0, line->text);
		bottom = (short)(bottom - line_height);
		line_index = (short)((line_index + CHAT_LOG_LINES - 1) % CHAT_LOG_LINES);
	}
	if (count)
	{
		struct network_game_client *client = chat_client();
		boolean in_game = client && network_game_client_get_state(client, NULL) != _client_pregame;

		if (!chat_globals.drawn[in_game])
		{
			chat_globals.drawn[in_game] = TRUE;
			platform_log("chat: the lines drawn (%s)", in_game ? "game" : "lobby");
		}
	}
}

void chat_server_handle_request(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	word *message,
	short message_size)
{
	struct chat_request_message request;
	struct chat_relay_message relay;
	struct network_game *game = network_game_server_get_game(server);
	struct network_player *sender = NULL;
	short packet_type = _message_client_chat;
	short packet_version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;
	char text[CHAT_TEXT_BYTES];
	char name[CHAT_NAME_BYTES];
	long machine_id;
	long slot;
	short count = chat_player_count(game);
	short index;
	boolean team_only;
	void *relay_message;
	long notice;

	message_size -= sizeof(word);
	csmemset(&request, 0, sizeof(request));
	if (message_size <= 0 ||
		!decode_network_game_message(&request, message + 1, &message_size, &packet_type, &packet_version,
			_packet_class_client_pregame))
	{
		platform_log("chat: the host dropped a line that could not be decoded");
		return;
	}
	slot = chat_machine_slot(server, machine);
	if (slot == NONE || !network_game_server_get_client_machine(server, machine, &machine_id) || machine_id == NONE)
		return;
	if (!chat_request_valid(&request, text))
	{
		unsigned long dropped = ++chat_globals.machine_dropped[slot];

		if (dropped <= CHAT_DROP_LOGS || dropped % CHAT_DROP_LOG_EVERY == 0)
			platform_log("chat: the host dropped a line from machine slot %ld: not valid (kind %d, phrase %d, flags %d)",
				slot, request.kind, request.phrase, request.flags);
		return;
	}
	/* (named from the machine's own players: the one at the request's
	controller, else its first) */
	for (index = 0; index < count; index++)
	{
		struct network_player *player = &game->players[index];

		if (player->machine_index != machine_id || !network_player_is_valid(player))
			continue;
		if (!sender || player->controller_index == request.local_player)
			sender = player;
		if (player->controller_index == request.local_player)
			break;
	}
	if (!sender)
	{
		platform_log("chat: the host dropped a line from machine slot %ld: it has no player", slot);
		return;
	}
	chat_name_clean(name, sizeof(name), (uint16_t const *)sender->name, CHAT_NAME_CHARACTERS);

	/* (the host's Game chat is its game's: Off, or Quick chat only and a
	typed line, the sender is told so, within the machine's limit) */
	notice = chat_host_notice(chat_mode(), request.kind);
	if (notice != NONE)
	{
		unsigned long dropped = ++chat_globals.machine_refused[slot];

		if (dropped <= CHAT_DROP_LOGS || dropped % CHAT_DROP_LOG_EVERY == 0)
			platform_log("chat: the host dropped a line from machine slot %ld: %s", slot, chat_notice_text(notice));
		if (chat_bucket_take(&chat_globals.machine_buckets[slot], system_milliseconds(), CHAT_BURST,
			CHAT_REFILL_MILLISECONDS))
		{
			csmemset(&relay, 0, sizeof(relay));
			relay.kind = _chat_kind_notice;
			relay.phrase = (short)notice;
			relay.team = NONE;
			relay_message = create_network_game_message(_message_server_chat, &relay, sizeof(relay));
			if (relay_message)
				network_game_server_send_message_to_client_machine(server, machine, relay_message);
		}
		return;
	}
	/* (a player the host muted is muted for its game) */
	if (chat_is_muted(name, sender))
	{
		unsigned long dropped = ++chat_globals.machine_refused[slot];

		if (dropped <= CHAT_DROP_LOGS || dropped % CHAT_DROP_LOG_EVERY == 0)
			platform_log("chat: the host dropped a line from %s: muted by the host", name);
		return;
	}
	if (!chat_server_allows(slot))
		return;

	team_only = (request.flags & (1 << _chat_flag_team_bit)) && chat_game_has_teams(game);
	csmemset(&relay, 0, sizeof(relay));
	relay.kind = request.kind;
	relay.phrase = request.kind == _chat_kind_quick ? request.phrase : 0;
	relay.flags = team_only ? 1 << _chat_flag_team_bit : 0;
	relay.team = chat_game_has_teams(game) && sender->team_index >= 0 && sender->team_index < 16 ? sender->team_index : NONE;
	relay.player = (short)(sender - game->players);
	csmemcpy(relay.name, sender->name, sizeof(relay.name));
	csstrncpy(relay.text, text, sizeof(relay.text) - 1);
	platform_log("chat: the host passes on %s's %s%s line", name, team_only ? "team " : "",
		request.kind == _chat_kind_quick ? "quick chat" : "typed");

	relay_message = create_network_game_message(_message_server_chat, &relay, sizeof(relay));
	if (!relay_message)
		return;
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; index++)
	{
		struct network_game_server_client_machine *other = network_game_server_get_client_machine_at_index(server, index);
		long other_id;

		if (!other || !network_game_server_client_machine_is_joined_to_game(server, other))
			continue;
		/* (not a machine still loading a game in progress: it hears none of
		the game's messages until it has) */
		if (network_game_server_get_state(server, NULL) == _server_state_ingame &&
			!network_game_server_client_machine_is_loaded(server, other))
		{
			continue;
		}
		if (team_only && (!network_game_server_get_client_machine(server, other, &other_id) ||
			!chat_machine_has_team(game, other_id, relay.team)))
		{
			continue;
		}
		network_game_server_send_message_to_client_machine(server, other, relay_message);
	}
}

void chat_client_handle_relay(
	struct network_game_client *client,
	word *message,
	short message_size)
{
	struct chat_relay_message relay;
	short packet_type = _message_server_chat;
	short packet_version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;
	short mode = chat_mode();
	char name[CHAT_NAME_BYTES];
	char text[CHAT_TEXT_BYTES];
	char line[CHAT_LINE_BYTES];
	boolean team;

	message_size -= sizeof(word);
	csmemset(&relay, 0, sizeof(relay));
	if (message_size <= 0 ||
		!decode_network_game_message(&relay, message + 1, &message_size, &packet_type, &packet_version,
			_packet_class_server_pregame))
	{
		platform_log("chat: a line from the host that could not be decoded");
		return;
	}
	if (!chat_relay_valid(&relay, name, text))
	{
		platform_log("chat: a line from the host that is not valid (kind %d, phrase %d)", relay.kind, relay.phrase);
		return;
	}
	if (!chat_bucket_take(&chat_globals.shown, system_milliseconds(), CHAT_SHOWN_BURST, CHAT_SHOWN_REFILL_MILLISECONDS))
	{
		if (++chat_globals.shown_dropped <= CHAT_DROP_LOGS || chat_globals.shown_dropped % CHAT_DROP_LOG_EVERY == 0)
			platform_log("chat: too many lines from the host (%lu not shown)", chat_globals.shown_dropped);
		return;
	}
	/* (nothing kept for a game this machine is not in) */
	if (mode == HALO_CHAT_MODE_OFF || !chat_globals.available)
		return;
	if (relay.kind == _chat_kind_notice)
	{
		chat_notice(chat_notice_text(relay.phrase));
		return;
	}
	if (mode == HALO_CHAT_MODE_QUICK && relay.kind != _chat_kind_quick)
		return;
	/* (muted: by where this machine's record of the game has the player
	the host names, or by the name) */
	if (chat_is_muted(name, chat_game_player(client ? network_game_client_get_game(client) : NULL, relay.player)))
	{
		platform_log("chat: a line from %s (muted)", name);
		return;
	}
	team = (relay.flags & (1 << _chat_flag_team_bit)) != 0;
	snprintf(line, sizeof(line), "%s%s: %s", team ? "[Team] " : "", name,
		relay.kind == _chat_kind_quick ? chat_phrase_text(relay.phrase) : text);
	chat_log_add(team ? _chat_style_team : _chat_style_all, line);
	platform_log("chat: %s", line);
}
