/*
VOICE.C

Voice chat in network games, as they are played (voice_link.h has the
settings; voice_protocol.c the rules every field from the network is
checked by; port/linux/src/voice_audio.c the microphone, the codec and
playback). It works as game chat's relaying does (chat.c), on the
distributed netcode's unreliable messages instead of the game's reliable
connection:

- A player's voice goes to the host in 20 ms Opus frames, those of each
  tick in one message (_distributed_message_voice), after the tick's game
  messages, in the room they leave in its datagram or a datagram of its own:
  a frame lost is not sent again and nothing waits for one. The frames name
  only which of the machine's controllers talks.
- The host takes frames only from a machine in the game (the distributed
  netcode's own check: a joined machine that has loaded) and names their
  talker itself, from that machine's own players in the game's record (the
  one at the frame's controller, else its first): a joiner cannot talk as
  another. It checks every frame (voice_frames_read: bounded, an Opus packet
  of one 20 ms mono frame), drops a machine's frames past its limit
  (VOICE_MACHINE_BURST, then one every VOICE_MACHINE_REFILL_MILLISECONDS:
  one talker's pace), its relaying past its own, and talkers past
  VOICE_MAXIMUM_TALKERS at once, and passes them on after its tick
  (_distributed_message_voice_relay) to every other machine in the game, or,
  in a game with teams, to those with a player of the talker's team. It
  plays them itself as a machine would.
- The host's Voice in my games (HALO_VOICE_HOST) is its game's: Off, or
  Private games only (the default) with its game listed in the public
  games, it passes on no voice, and tells a machine that talks so (game
  chat's notice, at most every VOICE_NOTICE_MILLISECONDS). A player the host
  muted (game chat's Mute players) is muted for its game: none of their
  voice is passed on.
- Every machine checks the host's frames again (a host is a stranger too),
  plays no more than the host's own limit lets through, and drops those of
  the players its player muted (game chat's mutes: voice and chat alike),
  and everything with its own Voice chat Off.
- Bad connections: a client whose round trip to the host is past
  VOICE_CONGESTED_ROUND_TRIP_TICKS, or that has heard nothing from it for
  VOICE_HOST_SILENT_MILLISECONDS, stops sending (and says so where the
  microphone's sign is) until it is better; the host sends no voice to a
  machine whose round trip is that long. The game's own messages always go
  first in a tick's datagrams.
- What is shown (voice_draw, at the window's right): this machine's
  microphone while it sends ("Talking", in red), "Open mic" while open mic
  listens, and the name of each player heard in the last
  VOICE_TALKING_SHOWN_MILLISECONDS.

(debug) HALO_TEST_VOICE scripts it: steps "SECONDS:ACTION[:ARGUMENT]"
between '|', the seconds since voice first became available (a game
played): talk:SECONDS (push to talk held that long), mode:ptt|open|off,
host:private|on|off, level:low|medium|high, mute:NAME, unmute:NAME ("*":
every other player, through game chat's mutes). HALO_TEST_VOICE_MIC
(voice_audio.c) stands in for the microphone.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "interface/interface.h"
#include "networking/network_client_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_messages.h"
#include "networking/network_server_manager_internal.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "network_distributed.h"

#include "chat.h"
#include "chat_protocol.h"
#include "voice.h"
#include "voice_protocol.h"
#include "../src/chat_link.h"
#include "../src/voice_link.h"

#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* a talker's name shown this long after its last frame */
	VOICE_TALKING_SHOWN_MILLISECONDS = 300,
	/* a link too slow for voice: its round trip (ticks), or the host not
	heard from this long */
	VOICE_CONGESTED_ROUND_TRIP_TICKS = 20,
	VOICE_HOST_SILENT_MILLISECONDS = 1500,
	/* the host's notice that its voice is off, to a machine, at most this
	often */
	VOICE_NOTICE_MILLISECONDS = 20000,
	/* the frames the host passes on after a tick, and the bytes of each
	message it sends (more go in another) */
	VOICE_RELAY_QUEUE_FRAMES = 64,
	VOICE_RELAY_MESSAGE_BYTES = 600,
	/* a client's messages to the host after a tick (frames past them, after
	a hitch, dropped) */
	VOICE_MESSAGES_PER_TICK = 2,
	/* halo.log's statistics, and the drops logged one by one */
	VOICE_STATISTICS_MILLISECONDS = 10000,
	VOICE_DROP_LOGS = 4,
	/* HALO_TEST_VOICE's steps */
	VOICE_TEST_STEPS = 32,
};

/* network_client_manager.c's client state of a game being played */
enum
{
	_client_ingame = 3,
};

/* ---------- structures */

struct voice_message
{
	struct distributed_message_header header;
	byte entries[VOICE_RELAY_MESSAGE_BYTES + VOICE_FRAME_HEADER_BYTES + VOICE_MAXIMUM_FRAME_BYTES];
};

struct voice_relay_entry
{
	struct voice_frame frame;
	/* the talker's machine (none of its own voice back), and team (NONE: to
	every machine) */
	long machine_index;
	short team;
};

struct voice_test_step
{
	unsigned long at;
	char action[12];
	char argument[CHAT_NAME_BYTES + 4];
};

/* ---------- prototypes */

void platform_log(char const *format, ...);
int setenv(const char *name, const char *value, int overwrite);
unsigned long system_milliseconds(void);
char const *config_string(char const *name);
long config_integer(char const *name);
/* (internet play, p2p_lobby.c) whether this machine's game is listed in the
public games */
int p2p_lobby_listed(void);

/* voice_audio.c's (voice_link.h) */
void voice_audio_update(int active, int capture, int open_mic, int level_dbfs, int volume);
int voice_audio_take_frame(unsigned char *data, int *size, unsigned short *sequence);
void voice_audio_play_frame(int talker, unsigned short sequence, const unsigned char *data, int size);
void voice_audio_forget_talker(int talker);
int voice_audio_sending(void);

/* ---------- globals */

static struct
{
	/* a game to talk in (Voice chat not Off), and since when (test steps) */
	boolean available;
	boolean ever_available;
	unsigned long available_time;
	/* this machine's link too slow to send on (said once each way) */
	boolean congested;
	/* this machine hosts a game it passes voice on in */
	boolean relaying;
	/* the microphone live (said in halo.log as it changes) */
	boolean sending;

	/* the host: each machine's frames, its relaying, its talkers */
	struct chat_bucket machine_buckets[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	unsigned long machine_noticed[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	struct chat_bucket relayed;
	struct voice_talkers talkers;
	struct voice_relay_entry queue[VOICE_RELAY_QUEUE_FRAMES];
	short queue_count;

	/* what this machine plays of its host's */
	struct chat_bucket played;
	/* each player's last frame heard (system_milliseconds), and muted then */
	unsigned long heard[VOICE_MAXIMUM_PLAYERS];
	boolean muted[VOICE_MAXIMUM_PLAYERS];

	/* statistics since the last log */
	unsigned long statistics_time;
	unsigned long sent_frames;
	unsigned long sent_bytes;
	unsigned long unsent_frames;
	unsigned long received_frames;
	unsigned long received_bytes;
	unsigned long received_muted;
	unsigned long relayed_frames;
	unsigned long relayed_bytes;
	unsigned long dropped_invalid;
	unsigned long dropped_flood;
	unsigned long dropped_talkers;
	unsigned long dropped_muted;
	unsigned long dropped_off;
	unsigned long dropped_link;
	unsigned long dropped_logged;

	/* HALO_TEST_VOICE */
	struct voice_test_step test_steps[VOICE_TEST_STEPS];
	short test_step_count;
	short test_next_step;
	boolean test_read;
	unsigned long test_talk_until;
} voice_globals;

/* ---------- private code */

/* a setting: its variable (the Vita's panel sets it as it goes), else
config.toml's */
static char const *voice_setting(
	char const *variable,
	char const *config_name)
{
	char const *value = getenv(variable);

	if (!value || !*value)
		value = config_string(config_name);
	return value ? value : "";
}

static short voice_mode(
	void)
{
	char const *setting = voice_setting("HALO_VOICE", "network.voice");

	if (!strcmp(setting, "off"))
		return HALO_VOICE_MODE_OFF;
	if (!strcmp(setting, "open"))
		return HALO_VOICE_MODE_OPEN;
	return HALO_VOICE_MODE_PUSH;
}

static short voice_host_mode(
	void)
{
	char const *setting = voice_setting("HALO_VOICE_HOST", "network.voice_host");

	if (!strcmp(setting, "off"))
		return HALO_VOICE_HOST_OFF;
	if (!strcmp(setting, "on"))
		return HALO_VOICE_HOST_ON;
	return HALO_VOICE_HOST_PRIVATE;
}

static int voice_volume(
	void)
{
	/* (an integer in config.toml) */
	char const *setting = getenv("HALO_VOICE_VOLUME");
	long volume = setting && *setting ? atol(setting) : config_integer("network.voice_volume");

	return volume < 0 ? 0 : volume > 100 ? 100 : (int)volume;
}

/* open mic's threshold (dBFS) */
static int voice_level(
	void)
{
	char const *setting = voice_setting("HALO_VOICE_LEVEL", "network.voice_level");

	if (!strcmp(setting, "low"))
		return -50;
	if (!strcmp(setting, "high"))
		return -32;
	return -42;
}

/* this machine's client of a network game being played (not split screen
alone), or NULL */
static struct network_game_client *voice_client(
	void)
{
	struct network_game_client *client = global_network_game_client_get();

	if (!client || network_game_is_splitscreen_local() || !game_in_progress())
		return NULL;
	return network_game_client_get_state(client, NULL) == _client_ingame ? client : NULL;
}

static boolean voice_game_has_teams(
	struct network_game const *game)
{
	return game && game->variant.universal_variant.teams;
}

static short voice_player_count(
	struct network_game const *game)
{
	short count = game ? game->player_count : 0;

	return count < 0 ? 0 : count > VOICE_MAXIMUM_PLAYERS ? VOICE_MAXIMUM_PLAYERS : count;
}

static struct network_player *voice_game_player(
	struct network_game *game,
	long index)
{
	struct network_player *player;

	if (!game || index < 0 || index >= voice_player_count(game))
		return NULL;
	player = &game->players[index];
	return network_player_is_valid(player) ? player : NULL;
}

/* whether this machine's game (the host's) lets voice through now: its
Voice in my games, and whether its game is listed in the public games */
static boolean voice_host_allows(
	void)
{
	short mode = voice_host_mode();

	if (mode == HALO_VOICE_HOST_OFF)
		return FALSE;
	return mode == HALO_VOICE_HOST_ON || !p2p_lobby_listed();
}

/* the controller of this machine's first player in the game (0 if none),
and that player (NULL if none) */
static struct network_player *voice_local_player(
	struct network_game *game)
{
	short count = voice_player_count(game);
	short index;

	for (index = 0; index < count; index++)
	{
		struct network_player *player = &game->players[index];

		if (network_player_is_valid(player) && network_game_player_is_local(player))
			return player;
	}
	return NULL;
}

static boolean voice_machine_has_team(
	struct network_game const *game,
	long machine_index,
	short team)
{
	short count = voice_player_count(game);
	short index;

	for (index = 0; index < count; index++)
	{
		struct network_player const *player = &game->players[index];

		if (player->machine_index == machine_index && player->team_index == team &&
			network_player_is_valid((struct network_player *)player))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void voice_drop_log(
	char const *why,
	long machine_index)
{
	if (++voice_globals.dropped_logged <= VOICE_DROP_LOGS)
		platform_log("voice: the host dropped frames from machine %ld: %s", machine_index, why);
}

/* a talker's frame heard on this machine: played, its name shown */
static void voice_play(
	short player_index,
	struct voice_frame const *frame)
{
	if (player_index < 0 || player_index >= VOICE_MAXIMUM_PLAYERS)
		return;
	voice_audio_play_frame(player_index, frame->sequence, frame->data, frame->size);
	voice_globals.heard[player_index] = system_milliseconds();
	if (!voice_globals.heard[player_index])
		voice_globals.heard[player_index] = 1;
}

/* (the host) a talker's frame taken or dropped: its machine's limit, the
host's, the talkers at once; passed on after the tick, and played here */
static void voice_host_take(
	struct network_game *game,
	long machine_index,
	struct network_player *talker,
	struct voice_frame *frame)
{
	unsigned long now = system_milliseconds();
	short player_index = (short)(talker - game->players);
	boolean team_only = voice_game_has_teams(game);
	struct voice_relay_entry *entry;

	if (!voice_globals.relaying)
	{
		voice_globals.dropped_off++;
		/* (the talker is told, now and then: game chat's notice) */
		if (machine_index >= 0 && machine_index < HALO_PORT_MAXIMUM_NETWORK_MACHINES &&
			!network_game_player_is_local(talker) &&
			(!voice_globals.machine_noticed[machine_index] ||
				now - voice_globals.machine_noticed[machine_index] >= VOICE_NOTICE_MILLISECONDS))
		{
			voice_globals.machine_noticed[machine_index] = now;
			chat_server_notice_machine(machine_index, _chat_notice_host_voice_off);
		}
		return;
	}
	if (chat_player_muted(talker))
	{
		voice_globals.dropped_muted++;
		return;
	}
	if (machine_index >= 0 && machine_index < HALO_PORT_MAXIMUM_NETWORK_MACHINES &&
		!chat_bucket_take(&voice_globals.machine_buckets[machine_index], now, VOICE_MACHINE_BURST,
			VOICE_MACHINE_REFILL_MILLISECONDS))
	{
		voice_globals.dropped_flood++;
		voice_drop_log("too many", machine_index);
		return;
	}
	if (!voice_talkers_admit(&voice_globals.talkers, player_index, now))
	{
		voice_globals.dropped_talkers++;
		return;
	}
	if (!chat_bucket_take(&voice_globals.relayed, now, VOICE_HOST_BURST, VOICE_HOST_REFILL_MILLISECONDS))
	{
		voice_globals.dropped_flood++;
		return;
	}
	frame->who = (uint8_t)player_index;
	frame->flags = team_only ? 1 << _voice_flag_team_bit : 0;
	if (voice_globals.queue_count < VOICE_RELAY_QUEUE_FRAMES)
	{
		entry = &voice_globals.queue[voice_globals.queue_count++];
		entry->frame = *frame;
		entry->machine_index = machine_index;
		entry->team = team_only ? talker->team_index : NONE;
	}
	/* (the host hears it too, as a machine would: not its own player's,
	nor another team's in a game with teams) */
	if (voice_globals.available && !network_game_player_is_local(talker))
	{
		struct network_player *local = voice_local_player(game);

		if (!team_only || (local && voice_machine_has_team(game, local->machine_index, talker->team_index)))
			voice_play(player_index, frame);
	}
}

/* (the host) the tick's frames to each machine that hears them */
static void voice_host_relay(
	struct network_game *game)
{
	long machine_indices[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	short machine_count;
	short machine;

	if (!voice_globals.queue_count)
		return;
	machine_count = distributed_client_machines(machine_indices, HALO_PORT_MAXIMUM_NETWORK_MACHINES);
	for (machine = 0; machine < machine_count; machine++)
	{
		long machine_index = machine_indices[machine];
		struct voice_message message;
		word size = 0;
		short count = 0;
		short index;

		/* (a machine on a link too slow: none, so the game's messages get
		what it has) */
		if (distributed_machine_round_trip_ticks(machine_index) > (real)VOICE_CONGESTED_ROUND_TRIP_TICKS)
		{
			voice_globals.dropped_link += voice_globals.queue_count;
			continue;
		}
		for (index = 0; index < voice_globals.queue_count; index++)
		{
			struct voice_relay_entry const *entry = &voice_globals.queue[index];
			int written;

			if (entry->machine_index == machine_index ||
				(entry->team != NONE && !voice_machine_has_team(game, machine_index, entry->team)))
			{
				continue;
			}
			if (count == VOICE_MAXIMUM_RELAY_FRAMES || size + VOICE_FRAME_HEADER_BYTES + entry->frame.size >
				VOICE_RELAY_MESSAGE_BYTES)
			{
				distributed_send_whole(&message, _distributed_message_voice_relay, count,
					(word)(sizeof(message.header) + size), machine_index);
				voice_globals.relayed_bytes += sizeof(message.header) + size;
				size = 0;
				count = 0;
			}
			written = voice_frame_write(message.entries + size, (int)(sizeof(message.entries) - size), &entry->frame);
			if (!written)
				continue;
			size = (word)(size + written);
			count++;
			voice_globals.relayed_frames++;
		}
		if (count)
		{
			distributed_send_whole(&message, _distributed_message_voice_relay, count,
				(word)(sizeof(message.header) + size), machine_index);
			voice_globals.relayed_bytes += sizeof(message.header) + size;
		}
	}
	voice_globals.queue_count = 0;
}

/* whether this machine's link to its host is too slow to send voice on */
static boolean voice_client_congested(
	void)
{
	long age = distributed_latest_host_time_age_ms();

	return distributed_own_round_trip_ticks() > (real)VOICE_CONGESTED_ROUND_TRIP_TICKS ||
		(age != NONE && age > VOICE_HOST_SILENT_MILLISECONDS);
}

/* (debug) HALO_TEST_VOICE's steps, read once */
static void voice_test_read(
	void)
{
	char const *script = getenv("HALO_TEST_VOICE");

	voice_globals.test_read = TRUE;
	while (script && *script && voice_globals.test_step_count < VOICE_TEST_STEPS)
	{
		struct voice_test_step *step = &voice_globals.test_steps[voice_globals.test_step_count];
		char const *end = strchr(script, '|');
		long length = end ? (long)(end - script) : (long)strlen(script);
		char const *colon = memchr(script, ':', (size_t)length);

		csmemset(step, 0, sizeof(*step));
		step->at = (unsigned long)(atof(script) * 1000.0);
		if (colon)
		{
			char const *argument = memchr(colon + 1, ':', (size_t)(length - (colon + 1 - script)));
			long action_length = (argument ? argument : script + length) - (colon + 1);
			long argument_length = argument ? script + length - (argument + 1) : 0;

			if (action_length > (long)sizeof(step->action) - 1)
				action_length = sizeof(step->action) - 1;
			if (argument_length > (long)sizeof(step->argument) - 1)
				argument_length = sizeof(step->argument) - 1;
			memcpy(step->action, colon + 1, (size_t)action_length);
			if (argument)
				memcpy(step->argument, argument + 1, (size_t)argument_length);
			voice_globals.test_step_count++;
		}
		script = end ? end + 1 : NULL;
	}
	if (voice_globals.test_step_count)
		platform_log("voice: test script of %d steps", voice_globals.test_step_count);
}

static void voice_test_update(
	void)
{
	unsigned long elapsed;

	if (!voice_globals.test_read)
		voice_test_read();
	if (!voice_globals.ever_available)
		return;
	elapsed = system_milliseconds() - voice_globals.available_time;
	while (voice_globals.test_next_step < voice_globals.test_step_count &&
		voice_globals.test_steps[voice_globals.test_next_step].at <= elapsed)
	{
		struct voice_test_step const *step = &voice_globals.test_steps[voice_globals.test_next_step];
		char const *argument = step->argument;

		/* (a mute waits for chat to take the one before) */
		if ((!strcmp(step->action, "mute") || !strcmp(step->action, "unmute")) &&
			__atomic_load_n(&halo_chat_request, __ATOMIC_ACQUIRE) != HALO_CHAT_REQUEST_NONE)
		{
			break;
		}
		voice_globals.test_next_step++;
		platform_log("voice: test step %s %s", step->action, argument);
		if (!strcmp(step->action, "talk"))
			voice_globals.test_talk_until = system_milliseconds() + (unsigned long)(atof(argument) * 1000.0);
		else if (!strcmp(step->action, "mode") && (!strcmp(argument, "ptt") || !strcmp(argument, "open") ||
			!strcmp(argument, "off")))
		{
			setenv("HALO_VOICE", argument, 1);
		}
		else if (!strcmp(step->action, "host") && (!strcmp(argument, "private") || !strcmp(argument, "on") ||
			!strcmp(argument, "off")))
		{
			setenv("HALO_VOICE_HOST", argument, 1);
		}
		else if (!strcmp(step->action, "level") && (!strcmp(argument, "low") || !strcmp(argument, "medium") ||
			!strcmp(argument, "high")))
		{
			setenv("HALO_VOICE_LEVEL", argument, 1);
		}
		else if (!strcmp(step->action, "mute") || !strcmp(step->action, "unmute"))
		{
			/* (game chat's mutes: one player by name, or "*" each listed) */
			boolean mute = step->action[0] == 'm';

			if (!strcmp(argument, "*"))
			{
				int index;

				for (index = 0; index < halo_chat_status[HALO_CHAT_STATUS_PLAYERS] && index < HALO_CHAT_PLAYERS; index++)
				{
					/* (one a frame: chat takes its request at its next) */
					csmemcpy(halo_chat_request_text, halo_chat_player_names[index], HALO_CHAT_NAME_SIZE);
					halo_chat_request_text[HALO_CHAT_NAME_SIZE - 1] = 0;
					__atomic_store_n(&halo_chat_request, mute ? HALO_CHAT_REQUEST_MUTE : HALO_CHAT_REQUEST_UNMUTE,
						__ATOMIC_RELEASE);
					chat_update();
				}
			}
			else
			{
				csstrncpy(halo_chat_request_text, argument, HALO_CHAT_TEXT_SIZE - 1);
				__atomic_store_n(&halo_chat_request, mute ? HALO_CHAT_REQUEST_MUTE : HALO_CHAT_REQUEST_UNMUTE,
					__ATOMIC_RELEASE);
			}
		}
	}
}

static void voice_statistics(
	boolean force)
{
	unsigned long now = system_milliseconds();
	unsigned long elapsed = now - voice_globals.statistics_time;
	real seconds;

	if (!force && elapsed < VOICE_STATISTICS_MILLISECONDS)
		return;
	seconds = elapsed ? (real)elapsed / 1000.0f : 1.0f;
	if (voice_globals.sent_frames || voice_globals.unsent_frames)
	{
		platform_log("voice: sent %lu frames to the host (%lu bytes, %.1f kbps), %lu not sent (a slow link)",
			voice_globals.sent_frames, voice_globals.sent_bytes, voice_globals.sent_bytes * 8.0f / 1000.0f / seconds,
			voice_globals.unsent_frames);
	}
	if (voice_globals.received_frames)
	{
		platform_log("voice: received %lu frames from the host (%lu bytes, %.1f kbps), %lu of players muted here",
			voice_globals.received_frames, voice_globals.received_bytes,
			voice_globals.received_bytes * 8.0f / 1000.0f / seconds, voice_globals.received_muted);
	}
	if (voice_globals.relayed_frames || voice_globals.dropped_invalid || voice_globals.dropped_flood ||
		voice_globals.dropped_talkers || voice_globals.dropped_muted || voice_globals.dropped_off ||
		voice_globals.dropped_link)
	{
		platform_log("voice: the host passed on %lu frames (%lu bytes, %.1f kbps out); dropped %lu not valid, "
			"%lu too many, %lu past %d talkers, %lu muted, %lu voice off, %lu to slow links",
			voice_globals.relayed_frames, voice_globals.relayed_bytes,
			voice_globals.relayed_bytes * 8.0f / 1000.0f / seconds, voice_globals.dropped_invalid,
			voice_globals.dropped_flood, voice_globals.dropped_talkers, VOICE_MAXIMUM_TALKERS,
			voice_globals.dropped_muted, voice_globals.dropped_off, voice_globals.dropped_link);
	}
	voice_globals.sent_frames = voice_globals.sent_bytes = voice_globals.unsent_frames = 0;
	voice_globals.received_frames = voice_globals.received_bytes = voice_globals.received_muted = 0;
	voice_globals.relayed_frames = voice_globals.relayed_bytes = 0;
	voice_globals.dropped_invalid = voice_globals.dropped_flood = voice_globals.dropped_talkers = 0;
	voice_globals.dropped_muted = voice_globals.dropped_off = voice_globals.dropped_link = 0;
	voice_globals.statistics_time = now;
}

/* ---------- public code */

void voice_update(
	void)
{
	struct network_game_client *client = voice_client();
	short mode = voice_mode();
	boolean available = client != NULL && mode != HALO_VOICE_MODE_OFF;
	boolean hosting = client != NULL && global_network_game_server_get() != NULL;
	unsigned long now = system_milliseconds();
	boolean talk;
	boolean capture;
	short index;

	voice_test_update();
	if (available != voice_globals.available)
	{
		voice_globals.available = available;
		if (available)
		{
			platform_log("voice: available (%s%s)", mode == HALO_VOICE_MODE_OPEN ? "open mic" : "push to talk",
				hosting ? voice_host_allows() ? ", passed on in this game" : ", this game's voice is off" : "");
			if (!voice_globals.ever_available)
				voice_globals.available_time = now;
			voice_globals.ever_available = TRUE;
		}
		else
		{
			platform_log("voice: not available");
		}
		/* (what the last game's voice did; the next one's counted from now) */
		if (!available)
			voice_statistics(TRUE);
		voice_globals.statistics_time = now;
		csmemset(voice_globals.heard, 0, sizeof(voice_globals.heard));
		csmemset(&voice_globals.played, 0, sizeof(voice_globals.played));
	}
	/* (the host's relaying holds while it hosts a game being played,
	whatever its own Voice chat) */
	{
		boolean relaying = hosting && voice_host_allows();

		if (relaying != voice_globals.relaying)
		{
			if (hosting)
				platform_log("voice: this game's voice is %s", relaying ? "passed on" : "off");
			voice_globals.relaying = relaying;
		}
		if (!hosting)
		{
			csmemset(voice_globals.machine_buckets, 0, sizeof(voice_globals.machine_buckets));
			csmemset(voice_globals.machine_noticed, 0, sizeof(voice_globals.machine_noticed));
			csmemset(&voice_globals.relayed, 0, sizeof(voice_globals.relayed));
			voice_talkers_reset(&voice_globals.talkers);
			voice_globals.queue_count = 0;
		}
	}

	/* (this machine sends only with push to talk held, or open mic on; not
	on a link too slow, nor as the host of a game whose voice is off) */
	talk = __atomic_load_n(&halo_voice_talk_held, __ATOMIC_ACQUIRE) || now < voice_globals.test_talk_until;
	capture = available && (mode == HALO_VOICE_MODE_OPEN || talk);
	if (capture && hosting && !voice_globals.relaying)
		capture = FALSE;
	if (available && !hosting)
	{
		boolean congested = voice_client_congested();

		if (congested != voice_globals.congested)
		{
			platform_log("voice: %s", congested ? "the link to the host is too slow: not sending" :
				"the link to the host is better: sending again");
			voice_globals.congested = congested;
		}
		if (congested)
			capture = FALSE;
	}
	else
	{
		voice_globals.congested = FALSE;
	}
	voice_audio_update(available, capture, mode == HALO_VOICE_MODE_OPEN, voice_level(), voice_volume());

	/* (a talker muted since: let go at once) */
	if (available)
	{
		struct network_game *game = network_game_client_get_game(client);

		for (index = 0; index < VOICE_MAXIMUM_PLAYERS; index++)
		{
			struct network_player *player = voice_game_player(game, index);
			boolean muted = player && chat_player_muted(player);

			if (muted && !voice_globals.muted[index])
				voice_audio_forget_talker(index);
			voice_globals.muted[index] = muted;
		}
	}
	{
		boolean sending = capture && voice_audio_sending();

		if (sending != voice_globals.sending)
		{
			platform_log("voice: the microphone is %s", sending ? "live (sending)" : "off");
			voice_globals.sending = sending;
		}
		__atomic_store_n(&halo_voice_status[HALO_VOICE_STATUS_SENDING], sending, __ATOMIC_RELEASE);
	}
	__atomic_store_n(&halo_voice_status[HALO_VOICE_STATUS_AVAILABLE], available, __ATOMIC_RELEASE);
	if (available || voice_globals.relaying)
		voice_statistics(FALSE);
}

void voice_network_tick(
	void)
{
	struct network_game_client *client = voice_client();
	struct network_game *game;
	short connection = game_connection();

	if (!client)
	{
		voice_globals.queue_count = 0;
		return;
	}
	game = network_game_client_get_game(client);
	if (voice_globals.available)
	{
		struct network_player *local = voice_local_player(game);
		short messages = 0;
		boolean more = TRUE;

		while (more)
		{
			struct voice_message message;
			word size = 0;
			short count = 0;

			while (count < VOICE_MAXIMUM_FRAMES_PER_MESSAGE)
			{
				struct voice_frame frame;
				int frame_size = 0;
				unsigned short sequence = 0;
				int written;

				if (!voice_audio_take_frame(frame.data, &frame_size, &sequence))
				{
					more = FALSE;
					break;
				}
				if (frame_size < 1 || frame_size > VOICE_MAXIMUM_FRAME_BYTES)
					continue;
				frame.size = (uint8_t)frame_size;
				frame.sequence = sequence;
				frame.who = local && local->controller_index >= 0 && local->controller_index < 4 ?
					(uint8_t)local->controller_index : 0;
				frame.flags = voice_game_has_teams(game) ? 1 << _voice_flag_team_bit : 0;
				/* (the host's own: taken as from its own machine) */
				if (connection == _game_connection_network_server)
				{
					if (local)
						voice_host_take(game, local->machine_index, local, &frame);
					continue;
				}
				/* (a slow link, or past this tick's messages: not sent) */
				if (voice_globals.congested || messages >= VOICE_MESSAGES_PER_TICK)
				{
					voice_globals.unsent_frames++;
					continue;
				}
				written = voice_frame_write(message.entries + size, (int)(sizeof(message.entries) - size), &frame);
				if (!written)
					continue;
				size = (word)(size + written);
				count++;
			}
			if (count)
			{
				distributed_send_whole(&message, _distributed_message_voice, count,
					(word)(sizeof(message.header) + size), NONE);
				voice_globals.sent_frames += count;
				voice_globals.sent_bytes += sizeof(message.header) + size;
				messages++;
			}
		}
	}
	if (connection == _game_connection_network_server)
		voice_host_relay(game);
	else
		voice_globals.queue_count = 0;
}

word voice_minimum_entry_size(
	void)
{
	return VOICE_FRAME_HEADER_BYTES + 1;
}

void voice_handle_frames(
	long machine_index,
	byte const *entries,
	byte const *end,
	short count)
{
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;
	struct voice_frame frames[VOICE_MAXIMUM_FRAMES_PER_MESSAGE];
	int frame_count;
	int index;

	if (!game || !voice_client() || machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES)
		return;
	frame_count = voice_frames_read(entries, end, count, frames, VOICE_MAXIMUM_FRAMES_PER_MESSAGE);
	if (frame_count < 0)
	{
		voice_globals.dropped_invalid++;
		voice_drop_log("not valid", machine_index);
		return;
	}
	for (index = 0; index < frame_count; index++)
	{
		struct network_player *talker = NULL;
		short player_count = voice_player_count(game);
		short player;

		/* (named from the machine's own players: the one at the frame's
		controller, else its first) */
		for (player = 0; player < player_count; player++)
		{
			struct network_player *candidate = &game->players[player];

			if (candidate->machine_index != machine_index || !network_player_is_valid(candidate) ||
				network_game_player_is_local(candidate))
			{
				continue;
			}
			if (!talker || candidate->controller_index == frames[index].who)
				talker = candidate;
			if (candidate->controller_index == frames[index].who)
				break;
		}
		if (!talker)
		{
			voice_globals.dropped_invalid++;
			voice_drop_log("it has no player", machine_index);
			return;
		}
		voice_host_take(game, machine_index, talker, &frames[index]);
	}
}

void voice_handle_relay(
	byte const *entries,
	byte const *end,
	short count)
{
	struct network_game_client *client = voice_client();
	struct network_game *game = client ? network_game_client_get_game(client) : NULL;
	struct voice_frame frames[VOICE_MAXIMUM_RELAY_FRAMES];
	unsigned long now = system_milliseconds();
	int frame_count;
	int index;

	/* (the host's relaying reaches its own machine only through
	voice_host_take) */
	if (!game || !voice_globals.available || game_connection() != _game_connection_network_client)
		return;
	frame_count = voice_frames_read(entries, end, count, frames, VOICE_MAXIMUM_RELAY_FRAMES);
	if (frame_count < 0)
	{
		if (++voice_globals.dropped_logged <= VOICE_DROP_LOGS)
			platform_log("voice: frames from the host that are not valid");
		return;
	}
	for (index = 0; index < frame_count; index++)
	{
		struct voice_frame const *frame = &frames[index];
		struct network_player *talker = voice_game_player(game, frame->who);

		/* (a player in this machine's record of the game, not its own) */
		if (!talker || network_game_player_is_local(talker))
			continue;
		if (!chat_bucket_take(&voice_globals.played, now, VOICE_PLAYED_BURST, VOICE_PLAYED_REFILL_MILLISECONDS))
			continue;
		voice_globals.received_frames++;
		voice_globals.received_bytes += VOICE_FRAME_HEADER_BYTES + frame->size;
		if (chat_player_muted(talker))
		{
			voice_globals.received_muted++;
			continue;
		}
		voice_play(frame->who, frame);
	}
}

void voice_draw(
	void)
{
	struct network_game_client *client;
	struct network_game *game;
	long font_index;
	struct font_header *font;
	unsigned long now = system_milliseconds();
	short line_height;
	short top;
	short index;
	char const *own = NULL;
	real_argb_color own_color;

	if (!voice_globals.available)
		return;
	client = voice_client();
	game = client ? network_game_client_get_game(client) : NULL;
	if (!game)
		return;
	font_index = interface_get_tag_index(_interface_font_terminal);
	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0)
		return;
	/* (this machine's microphone: live, open mic listening, or held back) */
	own_color.alpha = 1.0f;
	if (__atomic_load_n(&halo_voice_status[HALO_VOICE_STATUS_SENDING], __ATOMIC_ACQUIRE))
	{
		own = "Talking (mic on)";
		own_color.red = 1.0f; own_color.green = 0.3f; own_color.blue = 0.3f;
	}
	else if (voice_globals.congested && (voice_mode() == HALO_VOICE_MODE_OPEN ||
		__atomic_load_n(&halo_voice_talk_held, __ATOMIC_ACQUIRE)))
	{
		own = "Voice paused: slow connection";
		own_color.red = 1.0f; own_color.green = 0.8f; own_color.blue = 0.3f;
	}
	else if (voice_mode() == HALO_VOICE_MODE_OPEN)
	{
		own = "Open mic";
		own_color.red = 0.75f; own_color.green = 0.75f; own_color.blue = 0.75f;
	}
	top = (short)(render.camera.window_bounds.y0 + (render.camera.window_bounds.y1 - render.camera.window_bounds.y0) * 22 / 100);
	if (own)
	{
		rectangle2d bounds;

		bounds.x0 = render.camera.window_bounds.x0;
		bounds.x1 = (short)(render.camera.window_bounds.x1 - 12);
		bounds.y0 = top;
		bounds.y1 = (short)(top + line_height);
		offset_rectangle2d(&bounds, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
		draw_string_set_draw_mode(font_index, NONE, 1, 0, &own_color);
		rasterizer_draw_string(&bounds, NULL, NULL, 0, own);
		top = (short)(top + line_height);
	}
	for (index = 0; index < VOICE_MAXIMUM_PLAYERS; index++)
	{
		struct network_player *player;
		char name[CHAT_NAME_BYTES];
		char line[CHAT_NAME_BYTES + 8];
		real_argb_color color;
		rectangle2d bounds;

		if (!voice_globals.heard[index] || now - voice_globals.heard[index] > VOICE_TALKING_SHOWN_MILLISECONDS)
			continue;
		player = voice_game_player(game, index);
		if (!player || voice_globals.muted[index])
			continue;
		chat_name_clean(name, sizeof(name), (uint16_t const *)player->name, CHAT_NAME_CHARACTERS);
		snprintf(line, sizeof(line), "%s ((o", name);
		color.alpha = 1.0f; color.red = 0.55f; color.green = 0.85f; color.blue = 1.0f;
		bounds.x0 = render.camera.window_bounds.x0;
		bounds.x1 = (short)(render.camera.window_bounds.x1 - 12);
		bounds.y0 = top;
		bounds.y1 = (short)(top + line_height);
		offset_rectangle2d(&bounds, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
		draw_string_set_draw_mode(font_index, NONE, 1, 0, &color);
		rasterizer_draw_string(&bounds, NULL, NULL, 0, line);
		top = (short)(top + line_height);
	}
}
