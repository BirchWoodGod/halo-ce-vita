/*
GAME_CHAT.C

The in-game chat: lines to everyone in the game (ALL), to the player's team
(TEAM) and to the party (PARTY, port/linux/src/p2p_party.h), said from a
quick chat wheel or typed, shown at the bottom left over the game and
fading after a few seconds, as Halo PC's chat.

Where: in play, SELECT + D-pad left opens the wheel (Back + D-pad left on a
PC's pad, F1 + Left on its keyboard: game_chat_pad, from vita_pad.c and
xinput_sdl.c). Up and down choose a phrase (Need backup!, Flag taken!, Nice
shot!, GG... game_chat_protocol.c's, our own words) or "Type a message",
left and right or L and R the channel, Cross sends (Triangle types at once:
the Vita's system keyboard, the PC build's text box), Circle closes. While
it is open the recent lines are all shown, and holding the combo shows them
alone (let go, they fade again). The game sees none of the buttons while
the wheel is open; the sticks stay its own. Outside a network game the
combo is the Party page's, as before (vita_settings.c).

On the wire (game_chat_protocol.h): OpenCE's text chat, so OpenCE players on
PC and the Vitas read each other's lines in one game. A client sends its
player's line to the host (kind 87, over its stream); the host checks it
(one of that machine's players, its rate: five at once, one each 1.5 s
after; its text; no link), and sends it on (88) to each machine with a
player who hears it (a team's line to the sender's team's machines, a
vehicle's to those riding with the sender) and shows it if one of its own
players does; a client shows only the host's lines, its own too, over the
host's stream alone. The host's settings (89: chat allowed, links refused)
go to every client as they change and every 3 s: a client that has none is
in a game of a host without chat (an older build, a ChupathingyCE or older
OpenCE) and says so rather than send. The host relays what it is sent: it
never censors (each player's filter is their own).

What each machine shows, it checks again: the text cleaned
(game_chat_protocol.c), a muted player's lines hidden (Chat > Mute players,
the PLAYERS screen's MUTE CHAT: by name, this machine's alone, never told),
the slur filter's words masked as **** (port/linux/src/chat_filter.c, its
words port/vita/app0/chat/filter.txt; Chat filter, on by default, affects
only what this player sees) and a player whose lines it masks three times
in five minutes muted here. The party's lines come in from the party's own
messages and pass the same filter. Chat Off hides every line and sends
none. A quick chat phrase goes as its English text and shows in the
reader's language (lang.c).

Nothing is logged of what a line says in a release build (that a line was
shown or sent, and what was dropped and why); a test build logs the lines,
for the harness (HALO_TEST_CHAT: a scripted player's lines, mutes, a flood
and malformed messages; run_netns_online_test.sh chat,
run_netns_crossplay_test.sh crosschat).

Threads: the netcode's handlers, the main loop's update, the controllers'
read and the overlay's draw each take game_chat_lock; nothing is sent to
the party or the netcode by the draw.
*/

#include "cseries.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/interface.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "network_distributed.h"
#include "game_chat.h"
#include "game_chat_protocol.h"
#include "../src/chat_filter.h"
#include "../src/game_chat_share.h"
#include "../src/lang.h"
#include "../src/p2p_party.h"
#include "../src/system_link_shortcut.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned long system_milliseconds(void);
void platform_log(char const *format, ...);
int config_boolean(char const *name);
char *config_file_read(char const *path, size_t *size);
unsigned char *halo_app0_file_read(char const *path, unsigned long *size);
void halo_app0_file_free(unsigned char *data);
void draw_quad(rectangle2d *rectangle, pixel32 color);
extern int halo_main_menu_loaded(void);
extern volatile unsigned long halo_settings_generation;
extern char halo_screenshot_name[64];

/* ---------- constants */

enum
{
	/* the lines kept (the history), shown at once, and shown while the
	wheel is open or the combo held */
	GAME_CHAT_LINES_KEPT = 24,
	GAME_CHAT_LINES_SHOWN = 6,
	GAME_CHAT_LINES_HISTORY = 12,
	/* a shown line's text: the party's are longer than the game's */
	GAME_CHAT_SHOWN_TEXT_SIZE = P2P_PARTY_CHAT_SIZE,
	GAME_CHAT_SHOWN_NAME_SIZE = 24,
	/* a line shown, then faded out (milliseconds) */
	GAME_CHAT_LINE_MILLISECONDS = 8000,
	GAME_CHAT_FADE_MILLISECONDS = 1500,
	/* the combo held past this, the wheel only shows the history (let go,
	it closes) */
	GAME_CHAT_PEEK_MILLISECONDS = 450,
	/* the party's lines read this often */
	GAME_CHAT_PARTY_POLL_MILLISECONDS = 250,
	/* the panel's players published this often */
	GAME_CHAT_PLAYERS_MILLISECONDS = 500,
	/* the host: a notice to a flooding machine at most this often */
	GAME_CHAT_FLOOD_NOTICE_MILLISECONDS = 5000,
	/* the most rows a line wraps to */
	GAME_CHAT_ROWS_PER_LINE = 3,
	GAME_CHAT_MACHINES = HALO_PORT_MAXIMUM_NETWORK_MACHINES,
	/* the wheel's entries: the phrases, then "Type a message" */
	GAME_CHAT_WHEEL_ENTRIES = GAME_CHAT_PRESET_COUNT + 1,
	GAME_CHAT_WHEEL_CHANNELS = 3,
};

/* a shown line's colour */
enum
{
	_chat_color_neutral,
	_chat_color_red,
	_chat_color_blue,
	_chat_color_self,
	_chat_color_party,
	_chat_color_notice,
};

/* what came of a line refused before it was sent (said to the player) */
enum
{
	_chat_drop_malformed,
	_chat_drop_flood,
	_chat_drop_player,
	_chat_drop_link,
	_chat_drop_not_stream,
	NUMBER_OF_CHAT_DROPS
};

/* the quick chat's phrases (game_chat_protocol.c's, in its order), marked
for the translations: a phrase is sent as its English and shown in each
reader's language */
static char const *const game_chat_phrases[GAME_CHAT_PRESET_COUNT] =
{
	N_("Need backup!"),
	N_("Enemy spotted!"),
	N_("Flag taken!"),
	N_("Our flag is taken!"),
	N_("Follow me"),
	N_("On my way"),
	N_("Cover me"),
	N_("Nice shot!"),
	N_("Thanks!"),
	N_("Sorry!"),
	N_("Yes"),
	N_("No"),
	N_("GG"),
};

/* (the host's notices, which a client shows in its language: the
translations' keys) */
static char const *const game_chat_host_words[] =
{
	N_("You're sending messages too fast"),
	N_("Links aren't allowed in this game"),
};

/* ---------- structures */

struct game_chat_shown
{
	char name[GAME_CHAT_SHOWN_NAME_SIZE];
	char text[GAME_CHAT_SHOWN_TEXT_SIZE];
	short channel;
	short color;
	unsigned long time;
	/* its rows as last laid out (for a column that wide: 0 none yet), so a
	frame measures no text it measured before */
	short layout_width;
	short prefix_width;
	short rows;
	short starts[GAME_CHAT_ROWS_PER_LINE];
	short counts[GAME_CHAT_ROWS_PER_LINE];
	/* each row's right, from the column's left */
	short ends[GAME_CHAT_ROWS_PER_LINE];
};

/* ---------- globals */

volatile int halo_game_chat_active;
volatile int halo_chat_players_serial;
volatile int halo_chat_players_count = -1;
volatile int halo_chat_players_muted[HALO_CHAT_PLAYERS_MAXIMUM];
volatile int halo_chat_players_local[HALO_CHAT_PLAYERS_MAXIMUM];
char halo_chat_players_names[HALO_CHAT_PLAYERS_MAXIMUM][HALO_CHAT_PLAYER_NAME_SIZE];
volatile int halo_chat_mute_request;
char halo_chat_mute_request_name[HALO_CHAT_PLAYER_NAME_SIZE];

static pthread_mutex_t game_chat_lock = PTHREAD_MUTEX_INITIALIZER;

static struct
{
	/* the filter's words read (or tried) */
	boolean filter_read;
	/* the settings, as last read */
	unsigned long settings_seen;
	boolean settings_known;
	boolean enabled;
	boolean filter_enabled;

	struct game_chat_shown lines[GAME_CHAT_LINES_KEPT];
	short line_count;
	short newest;

	struct game_chat_mutes mutes;
	struct game_chat_trips trips;
	struct game_chat_rate send_rate;
	struct game_chat_rate host_rates[GAME_CHAT_MACHINES];
	unsigned long host_flood_noticed[GAME_CHAT_MACHINES];
	long drops[NUMBER_OF_CHAT_DROPS];
	unsigned long drops_logged;

	/* (a client) the host's settings, and (the host) when they last went */
	boolean config_known;
	unsigned char config_flags;
	unsigned long config_sent;

	/* in a network game now, and since when (the tests' script's clock) */
	boolean in_game;
	boolean hosting;
	unsigned long in_game_since;

	/* the wheel */
	boolean wheel_open;
	short wheel_channel;
	short wheel_entry;
	unsigned long wheel_opened;
	/* the combo still held since it opened the wheel; held past the peek's
	time (the history alone) */
	boolean combo_held;
	boolean peek;
	unsigned int pad_previous;
	/* a line being typed, to that channel */
	boolean typing;
	short typing_channel;

	/* the party's lines read, and its size (0: in none), read with them */
	int party_size;
	unsigned long party_newest;
	unsigned long party_polled;
	boolean party_seen;

	unsigned long players_published;

	/* the tests' script (HALO_TEST_CHAT): the next step */
	short script_step;
	unsigned int script_pad[48];
	/* (the script's "open": the wheel held open for a screenshot, the
	real controllers' presses - a test bot's - not taken meanwhile) */
	boolean script_hold;
	short script_pad_count;
	short script_pad_at;
} game_chat;

/* ---------- the settings */

static boolean game_chat_setting(char const *variable, char const *config_name)
{
	char const *value = getenv(variable);

	if (value && *value)
		return strcmp(value, "0") && strcmp(value, "false") && strcmp(value, "off") ? TRUE : FALSE;
	return config_boolean(config_name) ? TRUE : FALSE;
}

/* (locked) Chat (HALO_CHAT, network.chat) and Chat filter (HALO_CHAT_FILTER,
network.chat_filter), read again when the panel changes something */
static void game_chat_settings(void)
{
	if (game_chat.settings_known && game_chat.settings_seen == halo_settings_generation)
		return;
	game_chat.settings_known = TRUE;
	game_chat.settings_seen = halo_settings_generation;
	game_chat.enabled = game_chat_setting("HALO_CHAT", "network.chat");
	game_chat.filter_enabled = game_chat_setting("HALO_CHAT_FILTER", "network.chat_filter");
}

/* (locked) the filter's words, read once: HALO_CHAT_FILTER_FILE, else
app0's chat/filter.txt (the PC build's port/vita/app0 beside it) */
static void game_chat_filter_read(void)
{
	char const *path = getenv("HALO_CHAT_FILTER_FILE");
	int words = 0;

	if (game_chat.filter_read)
		return;
	game_chat.filter_read = TRUE;
	chat_filter_reset();
	if (path && *path)
	{
		size_t size = 0;
		char *data = config_file_read(path, &size);

		if (data)
		{
			words = chat_filter_load(data, (long)size);
			free(data);
		}
	}
	else
	{
		unsigned long size = 0;
		unsigned char *data = halo_app0_file_read("chat/filter.txt", &size);

		if (data)
		{
			words = chat_filter_load((char const *)data, (long)size);
			halo_app0_file_free(data);
		}
	}
	{
		int preset;

		/* (the phrases marked here are the protocol's, in its order) */
		for (preset = 0; preset < GAME_CHAT_PRESET_COUNT; preset++)
			if (strcmp(game_chat_phrases[preset], game_chat_preset_english(preset)))
				platform_log("chat: phrase %d is not the protocol's", preset);
	}
	if (words > 0)
		platform_log("chat: the filter's words read: %d", chat_filter_word_count());
	else
		platform_log("chat: the filter has no words: chat/filter.txt is missing or empty");
}

/* ---------- the game */

static boolean game_chat_network_game(void)
{
	short connection = game_connection();

	return (connection == _game_connection_network_client || connection == _game_connection_network_server) &&
		game_in_progress() && !halo_main_menu_loaded();
}

static boolean game_chat_host(void)
{
	return game_connection() == _game_connection_network_server;
}

/* the first local player's absolute index (who speaks); NONE */
static short game_chat_speaker(void)
{
	short local_player_index = local_player_get_next(NONE);
	long player_index = local_player_index != NONE ? local_player_get_player_index(local_player_index) : NONE;

	return player_index != NONE ? (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) : NONE;
}

static boolean game_chat_player_local(short player_index)
{
	short local_player_index;

	for (local_player_index = local_player_get_next(NONE); local_player_index != NONE;
		local_player_index = local_player_get_next(local_player_index))
	{
		long index = local_player_get_player_index(local_player_index);

		if (index != NONE && (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(index) == player_index)
			return TRUE;
	}
	return FALSE;
}

/* a player's name in ASCII ('?' for what is not), at most 11 characters */
static void game_chat_player_name(struct player_datum const *player, char *name, int size)
{
	int length;

	for (length = 0; length < size - 1 && length < (int)NUMBEROF(player->name) && player->name[length]; length++)
		name[length] = player_name_character_ascii(player->name[length]);
	name[length] = 0;
}

/* the vehicle a player rides in, the outermost; NONE for none */
static long game_chat_player_vehicle(struct player_datum const *player)
{
	long object_index = player->unit_index;
	long vehicle_index = NONE;
	short depth;

	for (depth = 0; object_index != NONE && depth < 8; depth++)
	{
		struct object_datum *object = object_try_and_get(object_index);

		if (!object || object->object.parent_object_index == NONE)
			break;
		vehicle_index = object->object.parent_object_index;
		object_index = vehicle_index;
	}
	return vehicle_index;
}

/* ---------- the lines shown */

static char const *game_chat_channel_word(short channel)
{
	switch (channel)
	{
	case GAME_CHAT_CHANNEL_TEAM: return "team";
	case GAME_CHAT_CHANNEL_PARTY: return "party";
	case GAME_CHAT_CHANNEL_VEHICLE: return "vehicle";
	case GAME_CHAT_CHANNEL_NOTICE: return "notice";
	default: return "all";
	}
}

/* (locked) a line added, the oldest giving way */
static struct game_chat_shown *game_chat_add(short channel, short color, char const *name, char const *text)
{
	struct game_chat_shown *line;

	game_chat.newest = (short)((game_chat.newest + 1) % GAME_CHAT_LINES_KEPT);
	if (game_chat.line_count < GAME_CHAT_LINES_KEPT)
		game_chat.line_count++;
	line = &game_chat.lines[game_chat.newest];
	memset(line, 0, sizeof(*line));
	snprintf(line->name, sizeof(line->name), "%s", name ? name : "");
	snprintf(line->text, sizeof(line->text), "%s", text ? text : "");
	line->channel = channel;
	line->color = color;
	line->time = system_milliseconds();
	if (!line->time)
		line->time = 1;
	return line;
}

/* (locked) a notice: the host's, or this machine's word */
static void game_chat_notice(char const *text)
{
	game_chat_add(GAME_CHAT_CHANNEL_NOTICE, _chat_color_notice, NULL, text);
#ifndef HALO_RELEASE
	platform_log("chat: notice: %s", text);
#else
	platform_log("chat: a notice shown");
#endif
}

/* (locked) the text as this player sees it: a phrase in their language, the
filter's words masked; how many it masked */
static int game_chat_text_for_screen(char *shown, int size, char const *text)
{
	int preset = game_chat_preset_find(text);

	snprintf(shown, (size_t)size, "%s", preset >= 0 ? T(game_chat_phrases[preset]) : text);
	if (!game_chat.filter_enabled)
		return 0;
	game_chat_filter_read();
	return chat_filter_mask(shown);
}

/* (locked) a sender's line the filter masked: counted, and the third in a
while mutes them here */
static void game_chat_filter_tripped(char const *name)
{
	char notice[96];

	if (!game_chat_trip(&game_chat.trips, name, system_milliseconds()))
		return;
	game_chat_mute_set(&game_chat.mutes, name, TRUE, TRUE);
	snprintf(notice, sizeof(notice), T("%s was muted: the chat filter caught them %d times"), name,
		GAME_CHAT_FILTER_TRIPS);
	game_chat_notice(notice);
	platform_log("chat: a player muted here by the filter (%d lines masked)", GAME_CHAT_FILTER_TRIPS);
}

/* (locked) a line from a player of the game (absolute index), shown */
static void game_chat_show_player_line(short player_index, short channel, char const *text)
{
	struct player_datum *player = distributed_player(player_index);
	char name[GAME_CHAT_NAME_SIZE];
	char shown[GAME_CHAT_SHOWN_TEXT_SIZE];
	short color;
	boolean own;
	int masked;

	if (!player)
		return;
	game_chat_settings();
	if (!game_chat.enabled)
		return;
	game_chat_player_name(player, name, 12);
	own = game_chat_player_local(player_index);
	if (!own && game_chat_mute_find(&game_chat.mutes, name) >= 0)
	{
#ifndef HALO_RELEASE
		platform_log("chat: a line from %s hidden: muted here", name);
#else
		platform_log("chat: a line from a muted player hidden");
#endif
		return;
	}
	masked = game_chat_text_for_screen(shown, sizeof(shown), text);
	if (own)
		color = _chat_color_self;
	else if (game_engine_has_teams())
		color = player->team_index == 0 ? _chat_color_red : _chat_color_blue;
	else
		color = _chat_color_neutral;
	/* (a name too: never shown as the filter's words) */
	if (game_chat.filter_enabled)
		chat_filter_mask(name);
	game_chat_add(channel, color, name, shown);
#ifndef HALO_RELEASE
	platform_log("chat: shown (%s): %s: %s%s", game_chat_channel_word(channel), name, shown,
		masked ? " [filtered]" : "");
#else
	platform_log("chat: a line shown (%s)%s", game_chat_channel_word(channel), masked ? ", filtered" : "");
#endif
	if (masked && !own)
	{
		char key_name[GAME_CHAT_NAME_SIZE];

		game_chat_player_name(player, key_name, 12);
		game_chat_filter_tripped(key_name);
	}
}

/* (locked) the party's new lines, shown as the party's channel */
static void game_chat_party_poll(unsigned long now)
{
	struct p2p_party_chat_line lines[P2P_PARTY_CHAT_LINES];
	int count, index;

	if (game_chat.party_polled && now - game_chat.party_polled < GAME_CHAT_PARTY_POLL_MILLISECONDS)
		return;
	game_chat.party_polled = now ? now : 1;
	{
		char status[96];

		game_chat.party_size = p2p_party_status(status, sizeof(status));
	}
	if (p2p_party_chat_newest() == game_chat.party_newest)
		return;
	count = p2p_party_chat_lines(lines, P2P_PARTY_CHAT_LINES, game_chat.party_newest);
	for (index = 0; index < count; index++)
	{
		char shown[GAME_CHAT_SHOWN_TEXT_SIZE];
		char name[GAME_CHAT_SHOWN_NAME_SIZE];
		int masked;

		game_chat.party_newest = lines[index].number;
		/* (the first look: what was said before is not new; nor what is said
		in the menus, which the Party page and screen show) */
		if (!game_chat.party_seen || !game_in_progress() || halo_main_menu_loaded())
			continue;
		game_chat_settings();
		if (!game_chat.enabled)
			continue;
		masked = game_chat_text_for_screen(shown, sizeof(shown), lines[index].text);
		snprintf(name, sizeof(name), "%s", lines[index].notice ? "" : lines[index].name);
		if (game_chat.filter_enabled)
			chat_filter_mask(name);
		game_chat_add(lines[index].notice ? GAME_CHAT_CHANNEL_NOTICE : GAME_CHAT_CHANNEL_PARTY,
			lines[index].notice ? _chat_color_notice : lines[index].self ? _chat_color_self : _chat_color_party,
			lines[index].notice ? NULL : name, shown);
#ifndef HALO_RELEASE
		/* (the party test's words, as the party's own overlay said them) */
		if (lines[index].notice)
			platform_log("party: chat shown over the game: %s", shown);
		else
			platform_log("party: chat shown over the game: %s: %s%s", name, shown, masked ? " [filtered]" : "");
#else
		platform_log("chat: a party line shown%s", masked ? ", filtered" : "");
#endif
	}
	game_chat.party_seen = TRUE;
}

/* ---------- the host */

/* (locked, the host) a message of one entry to a client machine, reliably */
static void game_chat_send_down(long machine_index, unsigned char player_index, unsigned char flags, char const *text)
{
	struct
	{
		struct distributed_message_header header;
		unsigned char entry[GAME_CHAT_LINE_ENTRY_SIZE];
	} message;

	csmemset(&message, 0, sizeof(message));
	game_chat_line_entry(message.entry, player_index, flags, text);
	distributed_send_to_machine_reliably(machine_index, &message, GAME_CHAT_KIND_DOWN, 1, (word)sizeof(message));
}

/* (locked, the host) a notice to one client machine */
static void game_chat_host_notice(long machine_index, char const *text)
{
	game_chat_send_down(machine_index, 0, GAME_CHAT_LINE_NOTICE_BIT, text);
}

/* (locked, the host) a player's line sent on to every machine that hears it
(and the sender's), and shown here if one of this machine's players hears
it or said it */
static void game_chat_host_send(short player_index, short channel, char const *text)
{
	struct player_datum *sender = distributed_player(player_index);
	boolean has_teams = game_engine_has_teams();
	long sender_vehicle;
	long machine_indices[GAME_CHAT_MACHINES];
	short count = distributed_client_machines(machine_indices, GAME_CHAT_MACHINES);
	short index;
	short local_player_index;
	short sent = 0;

	if (!sender || sender->quit_out_of_game)
		return;
	sender_vehicle = game_chat_player_vehicle(sender);
	for (index = 0; index < count; index++)
	{
		struct data_iterator iterator;
		struct player_datum *player;
		boolean hears = distributed_machine_has_player(machine_indices[index], player_index);

		data_iterator_new(&iterator, player_data);
		while (!hears && (player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			hears = !player->quit_out_of_game &&
				distributed_machine_has_player(machine_indices[index], (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index)) &&
				game_chat_hears(channel, has_teams, sender->team_index, player->team_index, sender_vehicle,
					game_chat_player_vehicle(player));
		}
		if (hears)
		{
			game_chat_send_down(machine_indices[index], distributed_player_to_byte(player_index),
				game_chat_channel_flags(channel), text);
			sent++;
		}
	}
	for (local_player_index = local_player_get_next(NONE); local_player_index != NONE;
		local_player_index = local_player_get_next(local_player_index))
	{
		long local_index = local_player_get_player_index(local_player_index);

		if (local_index != NONE &&
			((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(local_index) == player_index ||
				game_chat_hears(channel, has_teams, sender->team_index, player_get(local_index)->team_index,
					sender_vehicle, game_chat_player_vehicle(player_get(local_index)))))
		{
			game_chat_show_player_line(player_index, channel, text);
			break;
		}
	}
	platform_log("chat: host: a line (%s) of player %d sent on to %d machines", game_chat_channel_word(channel),
		player_index, sent);
}

/* (locked, the host) a line dropped: counted, said in the log once in a
while (never what it said) */
static void game_chat_host_drop(long machine_index, short why)
{
	static char const *const reasons[NUMBER_OF_CHAT_DROPS] =
		{ "malformed", "too fast (flood)", "not that machine's player", "a link", "not over its stream" };
	unsigned long now = system_milliseconds();

	game_chat.drops[why]++;
	if (why == _chat_drop_flood && machine_index >= 0 && machine_index < GAME_CHAT_MACHINES &&
		(!game_chat.host_flood_noticed[machine_index] ||
			now - game_chat.host_flood_noticed[machine_index] >= GAME_CHAT_FLOOD_NOTICE_MILLISECONDS))
	{
		game_chat.host_flood_noticed[machine_index] = now ? now : 1;
		game_chat_host_notice(machine_index, game_chat_host_words[0]);
	}
	if (game_chat.drops[why] <= 5 || !game_chat.drops_logged || now - game_chat.drops_logged >= 1000)
	{
		game_chat.drops_logged = now ? now : 1;
		platform_log("chat: host: dropped a line from machine %ld: %s (%ld so far: %ld malformed, %ld floods, "
			"%ld not its player, %ld links)", machine_index, reasons[why], game_chat.drops[why],
			game_chat.drops[_chat_drop_malformed], game_chat.drops[_chat_drop_flood], game_chat.drops[_chat_drop_player],
			game_chat.drops[_chat_drop_link]);
	}
}

/* (locked, the host, in a game) its settings to every client as they
change, and every GAME_CHAT_CONFIG_MILLISECONDS: the chat allowed, links
not (OpenCE's default) */
static void game_chat_host_config(unsigned long now)
{
	unsigned char flags = GAME_CHAT_CONFIG_ALLOWED_BIT;
	long machine_indices[GAME_CHAT_MACHINES];
	short count, index;
	struct
	{
		struct distributed_message_header header;
		unsigned char entry[GAME_CHAT_CONFIG_ENTRY_SIZE];
	} message;

	if (game_chat.config_known && game_chat.config_flags == flags && now - game_chat.config_sent < GAME_CHAT_CONFIG_MILLISECONDS)
		return;
	game_chat.config_known = TRUE;
	game_chat.config_flags = flags;
	game_chat.config_sent = now;
	count = distributed_client_machines(machine_indices, GAME_CHAT_MACHINES);
	for (index = 0; index < count; index++)
	{
		csmemset(&message, 0, sizeof(message));
		message.entry[0] = flags;
		distributed_send_to_machine_reliably(machine_indices[index], &message, GAME_CHAT_KIND_CONFIG, 1,
			(word)sizeof(message));
	}
}

/* ---------- the messages */

void game_chat_handle_message(long machine_index, int kind, int count, void const *entries, unsigned long size,
	int from_stream)
{
	struct game_chat_line line;
	int parsed;

	pthread_mutex_lock(&game_chat_lock);
	switch (kind)
	{
	case GAME_CHAT_KIND_UP:
	{
		struct player_datum *sender;
		short channel;

		/* (the host's alone, from a client machine, over its stream) */
		if (!game_chat_host() || machine_index < 0 || machine_index >= GAME_CHAT_MACHINES)
			break;
		if (!from_stream)
		{
			game_chat_host_drop(machine_index, _chat_drop_not_stream);
			break;
		}
		parsed = game_chat_parse_line(entries, size, count, &line);
		/* (a client never sends a notice) */
		if (parsed != GAME_CHAT_PARSE_OK || (line.flags & GAME_CHAT_LINE_NOTICE_BIT))
		{
			game_chat_host_drop(machine_index, _chat_drop_malformed);
			break;
		}
		if (line.player_index >= MAXIMUM_TRACKED_PLAYERS ||
			!distributed_machine_has_player(machine_index, line.player_index) ||
			(sender = distributed_player(line.player_index)) == NULL || sender->quit_out_of_game)
		{
			game_chat_host_drop(machine_index, _chat_drop_player);
			break;
		}
		if (!game_chat_rate_take(&game_chat.host_rates[machine_index], system_milliseconds(), GAME_CHAT_HOST_BURST,
			GAME_CHAT_HOST_REFILL_MILLISECONDS))
		{
			game_chat_host_drop(machine_index, _chat_drop_flood);
			break;
		}
		if (game_chat_text_has_link(line.text))
		{
			game_chat_host_drop(machine_index, _chat_drop_link);
			game_chat_host_notice(machine_index, game_chat_host_words[1]);
			break;
		}
		channel = (short)game_chat_channel_from_flags(line.flags);
		game_chat_host_send(line.player_index, channel, line.text);
		break;
	}
	case GAME_CHAT_KIND_DOWN:
		/* (a client's, from the host, over the host's stream alone: a
		datagram from its address could be anyone's) */
		if (game_connection() != _game_connection_network_client || machine_index != NONE || !from_stream)
			break;
		parsed = game_chat_parse_line(entries, size, count, &line);
		if (parsed != GAME_CHAT_PARSE_OK)
		{
			platform_log("chat: a line from the host refused (%d)", parsed);
			break;
		}
		if (line.flags & GAME_CHAT_LINE_NOTICE_BIT)
		{
			char shown[GAME_CHAT_SHOWN_TEXT_SIZE];

			/* (the host's word: ours in the player's language, then the
			filter's words masked as in any line) */
			game_chat_settings();
			snprintf(shown, sizeof(shown), "%s", lang_text(line.text));
			if (game_chat.filter_enabled)
			{
				game_chat_filter_read();
				chat_filter_mask(shown);
			}
			if (game_chat.enabled)
				game_chat_notice(shown);
			break;
		}
		if (distributed_player_from_byte(line.player_index) == NONE)
			break;
		game_chat_show_player_line(line.player_index, (short)game_chat_channel_from_flags(line.flags), line.text);
		break;
	case GAME_CHAT_KIND_CONFIG:
	{
		unsigned char flags;

		if (game_connection() != _game_connection_network_client || machine_index != NONE || !from_stream)
			break;
		if (game_chat_parse_config(entries, size, count, &flags) != GAME_CHAT_PARSE_OK)
			break;
		if (!game_chat.config_known || game_chat.config_flags != flags)
			platform_log("chat: the host's chat: %s, links %s", flags & GAME_CHAT_CONFIG_ALLOWED_BIT ? "on" : "off",
				flags & GAME_CHAT_CONFIG_LINKS_BIT ? "allowed" : "refused");
		game_chat.config_known = TRUE;
		game_chat.config_flags = flags;
		break;
	}
	default:
		break;
	}
	pthread_mutex_unlock(&game_chat_lock);
}

/* ---------- saying */

/* (locked) a line said; checked here as the host would check it */
static int game_chat_say_locked(int channel, char const *typed)
{
	char text[GAME_CHAT_SHOWN_TEXT_SIZE];
	short speaker;

	game_chat_settings();
	if (!game_chat.enabled)
	{
		game_chat_notice(T("Chat is off (Settings > Multiplayer > Chat)"));
		return FALSE;
	}
	if (channel == GAME_CHAT_CHANNEL_PARTY)
	{
		int outcome;

		if (!game_chat_text_clean(text, P2P_PARTY_CHAT_SIZE, typed, typed ? (int)strlen(typed) : 0))
			return FALSE;
		outcome = p2p_party_chat_send(text);
		if (outcome != P2P_PARTY_DONE)
		{
			game_chat_notice(p2p_party_outcome_text(outcome));
			return FALSE;
		}
#ifndef HALO_RELEASE
		platform_log("chat: sent (party): %s", text);
#else
		platform_log("chat: a line sent (party)");
#endif
		return TRUE;
	}
	if (channel != GAME_CHAT_CHANNEL_ALL && channel != GAME_CHAT_CHANNEL_TEAM)
		return FALSE;
	if (!game_chat_text_clean(text, GAME_CHAT_TEXT_SIZE, typed, typed ? (int)strlen(typed) : 0))
		return FALSE;
	if (!game_chat_network_game() || (speaker = game_chat_speaker()) == NONE)
	{
		game_chat_notice(T("Chat to ALL and TEAM works in network games"));
		return FALSE;
	}
	if (!game_chat_host() && (!game_chat.config_known || !(game_chat.config_flags & GAME_CHAT_CONFIG_ALLOWED_BIT)))
	{
		game_chat_notice(game_chat.config_known ? T("Chat is off in this game") :
			T("This game's host has no chat (an older version)"));
		return FALSE;
	}
	if (game_chat_text_has_link(text) && !(!game_chat_host() && (game_chat.config_flags & GAME_CHAT_CONFIG_LINKS_BIT)))
	{
		game_chat_notice(T("Links aren't allowed in this game"));
		return FALSE;
	}
	if (!game_chat_rate_take(&game_chat.send_rate, system_milliseconds(), GAME_CHAT_SEND_BURST,
		GAME_CHAT_SEND_REFILL_MILLISECONDS))
	{
		game_chat_notice(T("Slow down: wait a moment before the next message"));
		return FALSE;
	}
#ifndef HALO_RELEASE
	platform_log("chat: sent (%s): %s", game_chat_channel_word((short)channel), text);
#else
	platform_log("chat: a line sent (%s)", game_chat_channel_word((short)channel));
#endif
	if (game_chat_host())
	{
		game_chat_host_send(speaker, (short)channel, text);
	}
	else
	{
		struct
		{
			struct distributed_message_header header;
			unsigned char entry[GAME_CHAT_LINE_ENTRY_SIZE];
		} message;

		csmemset(&message, 0, sizeof(message));
		game_chat_line_entry(message.entry, distributed_player_to_byte(speaker), game_chat_channel_flags(channel), text);
		distributed_send(&message, GAME_CHAT_KIND_UP, 1, (word)sizeof(message), _distributed_to_host_reliably);
	}
	return TRUE;
}

int game_chat_say(int channel, char const *text)
{
	int said;

	pthread_mutex_lock(&game_chat_lock);
	said = game_chat_say_locked(channel, text);
	pthread_mutex_unlock(&game_chat_lock);
	return said;
}

/* ---------- typing */

static char const *game_chat_channel_name(short channel)
{
	switch (channel)
	{
	case GAME_CHAT_CHANNEL_TEAM: return T("TEAM");
	case GAME_CHAT_CHANNEL_PARTY: return T("PARTY");
	default: return T("ALL");
	}
}

/* (locked) the keyboard asked for a line to the channel: the Vita's system
keyboard, the PC build's text box (system_link_shortcut.h) */
static void game_chat_type_begin(short channel)
{
	if (__atomic_load_n(&halo_text_input_state, __ATOMIC_ACQUIRE) != HALO_TEXT_INPUT_IDLE)
	{
		game_chat_notice(T("The keyboard is busy"));
		return;
	}
	game_chat.typing = TRUE;
	game_chat.typing_channel = channel;
	snprintf(halo_text_input_title, sizeof(halo_text_input_title), T("Message to %s"), game_chat_channel_name(channel));
	halo_text_input_text[0] = 0;
	halo_text_input_maximum = channel == GAME_CHAT_CHANNEL_PARTY ? P2P_PARTY_CHAT_LENGTH : GAME_CHAT_TEXT_LENGTH;
	halo_text_input_password = 0;
#if !defined(HALO_VITA) && !defined(HALO_RELEASE)
	/* (tests: the line typed, HALO_TEST_CHAT_TYPED) */
	if (getenv("HALO_TEST_CHAT_TYPED"))
	{
		snprintf(halo_text_input_text, sizeof(halo_text_input_text), "%s", getenv("HALO_TEST_CHAT_TYPED"));
		__atomic_store_n(&halo_text_input_state, HALO_TEXT_INPUT_DONE, __ATOMIC_RELEASE);
		platform_log("chat: typing: answered by the test");
		return;
	}
#endif
	__atomic_store_n(&halo_text_input_state, HALO_TEXT_INPUT_REQUESTED, __ATOMIC_RELEASE);
	platform_log("chat: typing a line (%s)", game_chat_channel_word(channel));
}

/* (locked) the line typed, once the keyboard answered */
static void game_chat_type_poll(void)
{
	int state;
	char typed[HALO_TEXT_INPUT_SIZE];

	if (!game_chat.typing)
		return;
	state = __atomic_load_n(&halo_text_input_state, __ATOMIC_ACQUIRE);
	if (state != HALO_TEXT_INPUT_DONE && state != HALO_TEXT_INPUT_CANCELLED)
		return;
	game_chat.typing = FALSE;
	snprintf(typed, sizeof(typed), "%s", halo_text_input_text);
	__atomic_store_n(&halo_text_input_state, HALO_TEXT_INPUT_IDLE, __ATOMIC_RELEASE);
	if (state == HALO_TEXT_INPUT_DONE && typed[0])
		game_chat_say_locked(game_chat.typing_channel, typed);
	else
		platform_log("chat: typing cancelled");
}

/* ---------- the wheel */

static void game_chat_wheel_close(char const *why)
{
	if (!game_chat.wheel_open)
		return;
	game_chat.wheel_open = FALSE;
	game_chat.peek = FALSE;
	platform_log("chat: the wheel closed (%s)", why);
}

/* (locked) whether the chat can be opened here now: in play, in a network
game or with a party (whose channel alone it then has) */
static boolean game_chat_available(void)
{
	game_chat_settings();
	return game_chat.enabled && game_in_progress() && !halo_main_menu_loaded() && !cinematic_in_progress() &&
		(game_chat_network_game() || game_chat.party_size > 1);
}

static int game_chat_pad_locked(unsigned int buttons, unsigned long now)
{
	unsigned int pressed = buttons & ~game_chat.pad_previous;

	game_chat.pad_previous = buttons;
	/* (the combo's left is the combo's, not the channel's) */
	if (buttons & GAME_CHAT_PAD_COMBO)
		pressed &= ~(unsigned int)GAME_CHAT_PAD_LEFT;
	if (!game_chat.wheel_open)
	{
		if (!(pressed & GAME_CHAT_PAD_COMBO) || game_chat.typing)
			return game_chat.typing;
		if (!game_chat_available())
			return FALSE;
		game_chat.wheel_open = TRUE;
		game_chat.wheel_opened = now;
		game_chat.combo_held = TRUE;
		game_chat.peek = FALSE;
		game_chat.wheel_entry = 0;
		if (game_chat.wheel_channel < 0 || game_chat.wheel_channel >= GAME_CHAT_WHEEL_CHANNELS)
			game_chat.wheel_channel = GAME_CHAT_CHANNEL_ALL;
		/* (no network game: the party's) */
		if (!game_chat_network_game())
			game_chat.wheel_channel = GAME_CHAT_CHANNEL_PARTY;
		platform_log("chat: the wheel opened (%s)", game_chat_channel_word(game_chat.wheel_channel));
		return TRUE;
	}
	if (!game_chat_available())
	{
		game_chat_wheel_close("not in play");
		return FALSE;
	}
	if (game_chat.combo_held)
	{
		if (buttons & GAME_CHAT_PAD_COMBO)
		{
			if (!game_chat.peek && now - game_chat.wheel_opened >= GAME_CHAT_PEEK_MILLISECONDS)
			{
				game_chat.peek = TRUE;
				platform_log("chat: the history shown (the combo held)");
			}
			return TRUE;
		}
		game_chat.combo_held = FALSE;
		if (game_chat.peek)
		{
			game_chat_wheel_close("the combo let go");
			return TRUE;
		}
	}
	if (pressed & (GAME_CHAT_PAD_COMBO | GAME_CHAT_PAD_BACK))
	{
		game_chat_wheel_close(pressed & GAME_CHAT_PAD_BACK ? "back" : "the combo again");
		return TRUE;
	}
	if (pressed & GAME_CHAT_PAD_UP)
		game_chat.wheel_entry = (short)((game_chat.wheel_entry + GAME_CHAT_WHEEL_ENTRIES - 1) % GAME_CHAT_WHEEL_ENTRIES);
	if (pressed & GAME_CHAT_PAD_DOWN)
		game_chat.wheel_entry = (short)((game_chat.wheel_entry + 1) % GAME_CHAT_WHEEL_ENTRIES);
	if (pressed & (GAME_CHAT_PAD_LEFT | GAME_CHAT_PAD_PREVIOUS))
		game_chat.wheel_channel = (short)((game_chat.wheel_channel + GAME_CHAT_WHEEL_CHANNELS - 1) % GAME_CHAT_WHEEL_CHANNELS);
	if (pressed & (GAME_CHAT_PAD_RIGHT | GAME_CHAT_PAD_NEXT))
		game_chat.wheel_channel = (short)((game_chat.wheel_channel + 1) % GAME_CHAT_WHEEL_CHANNELS);
	if ((pressed & GAME_CHAT_PAD_TYPE) ||
		((pressed & GAME_CHAT_PAD_ACCEPT) && game_chat.wheel_entry == GAME_CHAT_PRESET_COUNT))
	{
		short channel = game_chat.wheel_channel;

		game_chat_wheel_close("typing");
		game_chat_type_begin(channel);
		return TRUE;
	}
	if (pressed & GAME_CHAT_PAD_ACCEPT)
	{
		short channel = game_chat.wheel_channel;
		short entry = game_chat.wheel_entry;

		game_chat_wheel_close("a phrase sent");
		game_chat_say_locked(channel, game_chat_preset_english(entry));
	}
	return TRUE;
}

int game_chat_pad(unsigned int buttons, unsigned long now)
{
	int taken;

	pthread_mutex_lock(&game_chat_lock);
#ifndef HALO_RELEASE
	if (game_chat.script_hold)
	{
		pthread_mutex_unlock(&game_chat_lock);
		return TRUE;
	}
#endif
	taken = game_chat_pad_locked(buttons, now);
	pthread_mutex_unlock(&game_chat_lock);
	return taken;
}

int game_chat_wheel_open(void)
{
	return game_chat.wheel_open;
}

/* ---------- mutes */

int game_chat_mute_name(char const *name, int muted)
{
	int now_muted;

	pthread_mutex_lock(&game_chat_lock);
	game_chat_mute_set(&game_chat.mutes, name, muted, FALSE);
	now_muted = game_chat_mute_find(&game_chat.mutes, name) >= 0;
	/* (unmuted: the filter's count of them starts again) */
	if (!muted)
	{
		char key[GAME_CHAT_NAME_SIZE];
		int index;

		game_chat_name_key(key, name);
		for (index = 0; index < game_chat.trips.count; index++)
			if (!strcmp(game_chat.trips.entries[index].name, key))
				game_chat.trips.entries[index].trips = 0;
	}
	pthread_mutex_unlock(&game_chat_lock);
#ifndef HALO_RELEASE
	platform_log("chat: %s %s here", name, now_muted ? "muted" : "unmuted");
#else
	platform_log("chat: a player %s here", now_muted ? "muted" : "unmuted");
#endif
	return now_muted;
}

int game_chat_name_muted(char const *name)
{
	int muted;

	pthread_mutex_lock(&game_chat_lock);
	muted = game_chat_mute_find(&game_chat.mutes, name) >= 0;
	pthread_mutex_unlock(&game_chat_lock);
	return muted;
}

/* (locked) the game's players for the panel's Chat page, and its request */
static void game_chat_players_publish(unsigned long now)
{
	int request = __atomic_load_n(&halo_chat_mute_request, __ATOMIC_ACQUIRE);
	int count = 0;

	if (request)
	{
		int index = (request & 0xFF) - 1;
		char name[HALO_CHAT_PLAYER_NAME_SIZE];

		memcpy(name, halo_chat_mute_request_name, sizeof(name));
		name[sizeof(name) - 1] = 0;
		/* (the player the panel showed at that index, still: by its name) */
		if (index >= 0 && index < HALO_CHAT_PLAYERS_MAXIMUM && !strcmp(name, halo_chat_players_names[index]) &&
			!halo_chat_players_local[index])
		{
			game_chat_mute_set(&game_chat.mutes, name, (request & HALO_CHAT_MUTE_REQUEST_MUTE) != 0, FALSE);
#ifndef HALO_RELEASE
			platform_log("chat: %s %s here (the settings panel)", name,
				request & HALO_CHAT_MUTE_REQUEST_MUTE ? "muted" : "unmuted");
#endif
		}
		__atomic_store_n(&halo_chat_mute_request, 0, __ATOMIC_RELEASE);
		game_chat.players_published = 0;
	}
	if (game_chat.players_published && now - game_chat.players_published < GAME_CHAT_PLAYERS_MILLISECONDS)
		return;
	game_chat.players_published = now ? now : 1;
	__atomic_add_fetch(&halo_chat_players_serial, 1, __ATOMIC_ACQ_REL);
	if (game_chat.in_game)
	{
		struct data_iterator iterator;
		struct player_datum *player;

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL &&
			count < HALO_CHAT_PLAYERS_MAXIMUM)
		{
			if (player->quit_out_of_game)
				continue;
			game_chat_player_name(player, halo_chat_players_names[count], 12);
			halo_chat_players_local[count] =
				game_chat_player_local((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index));
			halo_chat_players_muted[count] = game_chat_mute_find(&game_chat.mutes, halo_chat_players_names[count]) >= 0;
			count++;
		}
		halo_chat_players_count = count;
	}
	else
		halo_chat_players_count = -1;
	__atomic_add_fetch(&halo_chat_players_serial, 1, __ATOMIC_ACQ_REL);
}

/* ---------- the tests' script */

#ifndef HALO_RELEASE
/* (locked) HALO_TEST_CHAT: "SECONDS:VERB[:ARGUMENT];..." from when this
machine's network game began: all:TEXT, team:TEXT, party:TEXT,
preset:N (all), wheel:DOWNS:RIGHTS (the wheel opened by its combo, down and
right pressed, Cross: as a player), type:TEXT (the keyboard's line to ALL),
mute:NAME, unmute:NAME, flood:N (N lines sent past this machine's own
rate, for the host to drop), malformed (lines the host must drop: two in
one message, none, cut short, unknown flags, a notice, another machine's
player, over a datagram), shot:NAME (a screenshot, main.c's @shot), open (the wheel left open), close, teams
(each player's team logged) */
static void game_chat_test_raw(unsigned char player_index, unsigned char flags, short count, word entry_size,
	short destination, char const *text)
{
	struct
	{
		struct distributed_message_header header;
		unsigned char entries[2 * GAME_CHAT_LINE_ENTRY_SIZE];
	} message;

	csmemset(&message, 0, sizeof(message));
	game_chat_line_entry(message.entries, player_index, 0, text);
	message.entries[1] = flags;
	game_chat_line_entry(message.entries + GAME_CHAT_LINE_ENTRY_SIZE, player_index, 0, text);
	distributed_send(&message, GAME_CHAT_KIND_UP, count, (word)(sizeof(message.header) + entry_size), destination);
}

static void game_chat_script(unsigned long now)
{
	char const *script = getenv("HALO_TEST_CHAT");
	char step[160];
	short index = 0;
	unsigned long at;

	/* (the wheel's presses, one each frame; a "wheel" step's the real
	controllers' - a test bot's - held off until they are done) */
	if (game_chat.script_pad_at < game_chat.script_pad_count)
	{
		game_chat_pad_locked(game_chat.script_pad[game_chat.script_pad_at++], now);
		if (game_chat.script_pad_at == game_chat.script_pad_count && !game_chat.wheel_open)
			game_chat.script_hold = FALSE;
		return;
	}
	if (!script || !*script || !game_chat.in_game)
		return;
	while (*script)
	{
		int length = 0;
		char *verb;
		char *argument;

		while (*script && *script != ';' && length < (int)sizeof(step) - 1)
			step[length++] = *script++;
		step[length] = 0;
		if (*script == ';')
			script++;
		if (index++ != game_chat.script_step)
			continue;
		at = strtoul(step, &verb, 10) * 1000UL;
		if (now - game_chat.in_game_since < at)
			return;
		game_chat.script_step++;
		if (*verb != ':')
			return;
		verb++;
		argument = strchr(verb, ':');
		if (argument)
			*argument++ = 0;
		else
			argument = "";
		platform_log("chat: test: %s %s", verb, argument);
		if (!strcmp(verb, "all"))
			game_chat_say_locked(GAME_CHAT_CHANNEL_ALL, argument);
		else if (!strcmp(verb, "team"))
			game_chat_say_locked(GAME_CHAT_CHANNEL_TEAM, argument);
		else if (!strcmp(verb, "party"))
			game_chat_say_locked(GAME_CHAT_CHANNEL_PARTY, argument);
		else if (!strcmp(verb, "preset"))
			game_chat_say_locked(GAME_CHAT_CHANNEL_ALL, game_chat_preset_english(atoi(argument)));
		else if (!strcmp(verb, "type"))
			game_chat_type_begin(GAME_CHAT_CHANNEL_ALL);
		else if (!strcmp(verb, "mute") || !strcmp(verb, "unmute"))
		{
			game_chat_mute_set(&game_chat.mutes, argument, !strcmp(verb, "mute"), FALSE);
			platform_log("chat: %s %s here", argument, !strcmp(verb, "mute") ? "muted" : "unmuted");
		}
		else if (!strcmp(verb, "wheel"))
		{
			/* (the combo pressed and let go, DOWNS downs, RIGHTS rights, Cross) */
			int downs = atoi(argument);
			char const *rights_text = strchr(argument, ':');
			int rights = rights_text ? atoi(rights_text + 1) : 0;
			short count = 0;

			game_chat.script_pad[count++] = GAME_CHAT_PAD_COMBO;
			game_chat.script_pad[count++] = 0;
			while (downs-- > 0 && count < 40)
			{
				game_chat.script_pad[count++] = GAME_CHAT_PAD_DOWN;
				game_chat.script_pad[count++] = 0;
			}
			while (rights-- > 0 && count < 46)
			{
				game_chat.script_pad[count++] = GAME_CHAT_PAD_RIGHT;
				game_chat.script_pad[count++] = 0;
			}
			game_chat.script_pad[count++] = GAME_CHAT_PAD_ACCEPT;
			game_chat.script_pad[count++] = 0;
			game_chat.script_pad_count = count;
			game_chat.script_pad_at = 0;
			game_chat.script_hold = TRUE;
		}
		else if (!strcmp(verb, "open"))
		{
			/* (the wheel opened and left open: a screenshot's) */
			game_chat.script_pad[0] = GAME_CHAT_PAD_COMBO;
			game_chat.script_pad[1] = 0;
			game_chat.script_pad[2] = GAME_CHAT_PAD_DOWN;
			game_chat.script_pad[3] = 0;
			game_chat.script_pad[4] = GAME_CHAT_PAD_DOWN;
			game_chat.script_pad[5] = 0;
			game_chat.script_pad_count = 6;
			game_chat.script_pad_at = 0;
			game_chat.script_hold = TRUE;
		}
		else if (!strcmp(verb, "close"))
		{
			game_chat.script_hold = FALSE;
			game_chat_wheel_close("the test");
		}
		else if (!strcmp(verb, "teams"))
		{
			/* (each player's team, as this machine has them) */
			struct data_iterator iterator;
			struct player_datum *player;
			char line[256];
			int length = 0;

			data_iterator_new(&iterator, player_data);
			while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL &&
				length < (int)sizeof(line) - 32)
			{
				char name[GAME_CHAT_NAME_SIZE];

				if (player->quit_out_of_game)
					continue;
				game_chat_player_name(player, name, 12);
				length += snprintf(line + length, sizeof(line) - (size_t)length, " %s t%ld", name, player->team_index);
			}
			line[length] = 0;
			platform_log("chat: teams:%s", line);
		}
		else if (!strcmp(verb, "flood"))
		{
			int lines = atoi(argument);
			int line;
			short speaker = game_chat_speaker();

			for (line = 0; line < lines && line < 64 && speaker != NONE; line++)
			{
				char text[32];

				snprintf(text, sizeof(text), "flood %d", line + 1);
				game_chat_test_raw(distributed_player_to_byte(speaker), 0, 1, GAME_CHAT_LINE_ENTRY_SIZE,
					_distributed_to_host_reliably, text);
			}
		}
		else if (!strcmp(verb, "malformed"))
		{
			short speaker = game_chat_speaker();
			unsigned char own = speaker != NONE ? distributed_player_to_byte(speaker) : 0;

			game_chat_test_raw(own, 0, 2, 2 * GAME_CHAT_LINE_ENTRY_SIZE, _distributed_to_host_reliably, "two at once");
			game_chat_test_raw(own, 0, 0, 0, _distributed_to_host_reliably, "none");
			game_chat_test_raw(own, 0, 1, 20, _distributed_to_host_reliably, "cut short");
			game_chat_test_raw(own, 0x80, 1, GAME_CHAT_LINE_ENTRY_SIZE, _distributed_to_host_reliably, "flags");
			game_chat_test_raw(own, GAME_CHAT_LINE_NOTICE_BIT, 1, GAME_CHAT_LINE_ENTRY_SIZE,
				_distributed_to_host_reliably, "a forged notice");
			game_chat_test_raw((unsigned char)(own == 0 ? 1 : 0), 0, 1, GAME_CHAT_LINE_ENTRY_SIZE,
				_distributed_to_host_reliably, "as the host's player");
			game_chat_test_raw(own, 0, 1, GAME_CHAT_LINE_ENTRY_SIZE, _distributed_to_host, "over a datagram");
		}
		else if (!strcmp(verb, "shot"))
			snprintf(halo_screenshot_name, sizeof(halo_screenshot_name), "%s", argument);
		return;
	}
}
#endif

/* ---------- the main loop */

void game_chat_update(void)
{
	unsigned long now = system_milliseconds();
	boolean in_game;

	pthread_mutex_lock(&game_chat_lock);
	game_chat_settings();
	in_game = game_chat_network_game();
	if (in_game != game_chat.in_game)
	{
		game_chat.in_game = in_game;
		game_chat.hosting = in_game && game_chat_host();
		game_chat.in_game_since = now;
		game_chat.script_step = 0;
		game_chat.script_pad_count = game_chat.script_pad_at = 0;
		if (in_game)
		{
			short index;

			/* (a game's own: its host's settings come again, its lines start
			afresh; the party's stay) */
			game_chat.config_known = FALSE;
			game_chat.config_sent = 0;
			for (index = 0; index < GAME_CHAT_MACHINES; index++)
			{
				csmemset(&game_chat.host_rates[index], 0, sizeof(game_chat.host_rates[index]));
				game_chat.host_flood_noticed[index] = 0;
			}
			for (index = 0; index < GAME_CHAT_LINES_KEPT; index++)
			{
				if (game_chat.lines[index].channel != GAME_CHAT_CHANNEL_PARTY)
					game_chat.lines[index].time = 0;
			}
			game_chat_filter_read();
			platform_log("chat: in a network game (%s); chat %s, filter %s", game_chat.hosting ? "hosting" : "a client",
				game_chat.enabled ? "on" : "off", game_chat.filter_enabled ? "on" : "off");
#ifndef HALO_RELEASE
			{
				struct data_iterator iterator;
				struct player_datum *player;
				char line[256];
				int length = 0;

				data_iterator_new(&iterator, player_data);
				while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL &&
					length < (int)sizeof(line) - 32)
				{
					char name[GAME_CHAT_NAME_SIZE];

					game_chat_player_name(player, name, 12);
					length += snprintf(line + length, sizeof(line) - (size_t)length, " %s t%ld", name, player->team_index);
				}
				line[length] = 0;
				platform_log("chat: players:%s", line);
			}
#endif
		}
		else
		{
			game_chat_wheel_close("the game ended");
			game_chat.config_known = FALSE;
		}
	}
	/* (a host's game without chat on another build: hosting now) */
	if (in_game && game_chat.hosting != game_chat_host())
		game_chat.hosting = game_chat_host();
	if (in_game && game_chat.hosting)
		game_chat_host_config(now);
	game_chat_party_poll(now);
	__atomic_store_n(&halo_game_chat_active, game_chat_available(), __ATOMIC_RELEASE);
	game_chat_type_poll();
	game_chat_players_publish(now);
#ifndef HALO_RELEASE
	game_chat_script(now);
#endif
	pthread_mutex_unlock(&game_chat_lock);
}

/* ---------- drawing */

static pixel32 game_chat_pixel(real alpha, real red, real green, real blue)
{
	return ((pixel32)(PIN(alpha, 0.0f, 1.0f) * 255.0f + 0.5f) << 24) |
		((pixel32)(PIN(red, 0.0f, 1.0f) * 255.0f + 0.5f) << 16) |
		((pixel32)(PIN(green, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
		(pixel32)(PIN(blue, 0.0f, 1.0f) * 255.0f + 0.5f);
}

static void game_chat_color(short color, real alpha, real_argb_color *result)
{
	result->alpha = alpha;
	switch (color)
	{
	case _chat_color_red: result->red = 1.0f; result->green = 0.45f; result->blue = 0.40f; break;
	case _chat_color_blue: result->red = 0.45f; result->green = 0.68f; result->blue = 1.0f; break;
	case _chat_color_self: result->red = 0.70f; result->green = 0.88f; result->blue = 1.0f; break;
	case _chat_color_party: result->red = 0.55f; result->green = 1.0f; result->blue = 0.55f; break;
	case _chat_color_notice: result->red = 0.95f; result->green = 0.82f; result->blue = 0.45f; break;
	default: result->red = 1.0f; result->green = 0.88f; result->blue = 0.50f; break;
	}
}

/* text (UTF-8: the translations'; a line's own is ASCII) as the game's
16-bit text; a sequence it cannot read a '?' */
static void game_chat_widen(wchar_t *wide, int size, char const *text)
{
	unsigned char const *bytes = (unsigned char const *)text;
	int length = 0;

	while (*bytes && length < size - 1)
	{
		unsigned int code = *bytes++;
		int more = code >= 0xF0 ? 3 : code >= 0xE0 ? 2 : code >= 0xC0 ? 1 : 0;

		if (code >= 0x80 && code < 0xC0)
			code = '?';
		else if (more)
		{
			code &= 0x3F >> more;
			while (more-- > 0)
			{
				if ((*bytes & 0xC0) != 0x80)
				{
					code = '?';
					break;
				}
				code = (code << 6) | (*bytes++ & 0x3F);
			}
			if (code > 0xFFFF)
				code = '?';
		}
		wide[length++] = (wchar_t)code;
	}
	wide[length] = 0;
}

static short game_chat_width(rectangle2d const *bounds, wchar_t const *text)
{
	rectangle2d text_bounds, cursor_bounds;

	if (!text[0])
		return 0;
	draw_unicode_string_compute_bounds(bounds, text, &text_bounds, &cursor_bounds);
	return (short)(cursor_bounds.x0 - bounds->x0);
}

/* the characters of text from start that fit width (at least one; at a
space when one is there) */
static int game_chat_fit(rectangle2d const *bounds, wchar_t const *text, int start, short width)
{
	wchar_t part[GAME_CHAT_SHOWN_TEXT_SIZE + GAME_CHAT_SHOWN_NAME_SIZE + 16];
	int length = 0, low = 1, high, fit;

	while (text[start + length] && length < (int)NUMBEROF(part) - 1)
		length++;
	if (!length)
		return 0;
	memcpy(part, text + start, sizeof(wchar_t) * (size_t)length);
	part[length] = 0;
	if (game_chat_width(bounds, part) <= width)
		return length;
	for (high = length; low < high;)
	{
		int middle = (low + high + 1) / 2;

		part[middle] = 0;
		if (game_chat_width(bounds, part) <= width)
			low = middle;
		else
			high = middle - 1;
		memcpy(part, text + start, sizeof(wchar_t) * (size_t)length);
		part[length] = 0;
	}
	fit = low;
	/* (back to a space, if there is one in the row) */
	{
		int space;

		for (space = fit; space > 0 && text[start + space] != ' '; space--)
			;
		if (space > 0 && text[start + fit] != 0)
			fit = space + 1;
	}
	return fit;
}

/* a line drawn with its last row's bottom at bottom, its rows above (laid
out once for the column's width); the rows it took */
static short game_chat_draw_line(struct game_chat_shown *line, real alpha, rectangle2d const *column,
	short bottom, short line_height, long font_index)
{
	char prefix[GAME_CHAT_SHOWN_NAME_SIZE + 24];
	wchar_t wide_prefix[NUMBEROF(prefix)];
	wchar_t wide_text[GAME_CHAT_SHOWN_TEXT_SIZE];
	real_argb_color name_color, text_color;
	short width = (short)(column->x1 - column->x0);
	short row;
	rectangle2d measure = *column;

	if (line->channel == GAME_CHAT_CHANNEL_NOTICE || !line->name[0])
		prefix[0] = 0;
	else
		snprintf(prefix, sizeof(prefix), "%s%s: ", line->channel == GAME_CHAT_CHANNEL_TEAM ? T("[Team] ") :
			line->channel == GAME_CHAT_CHANNEL_PARTY ? T("[Party] ") : line->channel == GAME_CHAT_CHANNEL_VEHICLE ?
			T("[Vehicle] ") : "", line->name);
	game_chat_widen(wide_prefix, NUMBEROF(wide_prefix), prefix);
	game_chat_widen(wide_text, NUMBEROF(wide_text), line->text);
	game_chat_color(line->color, alpha, &name_color);
	if (line->channel == GAME_CHAT_CHANNEL_NOTICE)
		text_color = name_color;
	else
	{
		text_color.alpha = alpha;
		text_color.red = text_color.green = text_color.blue = 0.95f;
	}
	draw_string_set_draw_mode(font_index, NONE, 0, 0, &text_color);
	if (line->layout_width != width)
	{
		/* (the rows: the first after the name, the rest the column's width) */
		int at = 0;

		line->layout_width = width;
		line->prefix_width = game_chat_width(&measure, wide_prefix);
		if (line->prefix_width > width / 2)
			line->prefix_width = (short)(width / 2);
		line->rows = 0;
		while (wide_text[at] && line->rows < GAME_CHAT_ROWS_PER_LINE)
		{
			short room = (short)(line->rows ? width : width - line->prefix_width);
			int count = game_chat_fit(&measure, wide_text, at, room);

			if (count <= 0)
				break;
			line->starts[line->rows] = (short)at;
			line->counts[line->rows] = (short)count;
			{
				wchar_t part[GAME_CHAT_SHOWN_TEXT_SIZE];

				memcpy(part, wide_text + at, sizeof(wchar_t) * (size_t)count);
				part[count] = 0;
				line->ends[line->rows] = (short)((line->rows ? 0 : line->prefix_width) + game_chat_width(&measure, part));
			}
			line->rows++;
			at += count;
			while (wide_text[at] == ' ')
				at++;
		}
		if (!line->rows)
		{
			line->starts[0] = 0;
			line->counts[0] = 0;
			line->ends[0] = line->prefix_width;
			line->rows = 1;
		}
	}
	for (row = 0; row < line->rows; row++)
	{
		rectangle2d band, place;
		wchar_t part[GAME_CHAT_SHOWN_TEXT_SIZE];
		short top = (short)(bottom - (line->rows - row) * (line_height + 1));
		short left = (short)(column->x0 + (row ? 0 : line->prefix_width));
		short count = line->counts[row];

		if (line->starts[row] + count >= (short)NUMBEROF(part))
			count = (short)(NUMBEROF(part) - 1 - line->starts[row]);
		memcpy(part, wide_text + line->starts[row], sizeof(wchar_t) * (size_t)count);
		part[count] = 0;
		band.x0 = (short)(column->x0 - 4);
		band.y0 = top;
		band.y1 = (short)(top + line_height + 1);
		band.x1 = (short)(column->x0 + MIN(line->ends[row], width) + 4);
		draw_quad(&band, game_chat_pixel(0.38f * alpha, 0.0f, 0.0f, 0.0f));
		if (!row && prefix[0])
		{
			place = *column;
			place.y0 = top;
			place.y1 = (short)(top + line_height);
			draw_string_set_draw_mode(font_index, NONE, 0, 0, &name_color);
			rasterizer_draw_unicode_string(&place, NULL, NULL, 0, wide_prefix);
		}
		place = *column;
		place.x0 = left;
		place.y0 = top;
		place.y1 = (short)(top + line_height);
		draw_string_set_draw_mode(font_index, NONE, 0, 0, &text_color);
		rasterizer_draw_unicode_string(&place, NULL, NULL, 0, part);
	}
	return line->rows;
}

/* the wheel at the right of the screen: its channels, its phrases, the
buttons' words */
static void game_chat_draw_wheel(rectangle2d const *bounds, short line_height, long font_index)
{
	short width = (short)(bounds->x1 - bounds->x0);
	short height = (short)(bounds->y1 - bounds->y0);
	rectangle2d panel, place;
	real_argb_color color;
	wchar_t wide[160];
	char text[160];
	short top, entry, channel, left;
	short row_height = (short)(line_height + 2);
	short hint_top;

	panel.x0 = (short)(bounds->x0 + width * 58 / 100);
	panel.x1 = (short)(bounds->x0 + width * 97 / 100);
	/* (under the latency meter at the top right) */
	panel.y0 = (short)(bounds->y0 + height * 25 / 100);
	/* (its title, the channels, the entries, a gap and the buttons' two
	lines) */
	panel.y1 = (short)(panel.y0 + row_height * (GAME_CHAT_WHEEL_ENTRIES + 2) + 2 * line_height + 22);
	if (panel.y1 > bounds->y1 - 4)
	{
		panel.y0 = (short)MAX(bounds->y0 + 4, panel.y0 - (panel.y1 - (bounds->y1 - 4)));
		panel.y1 = (short)(bounds->y1 - 4);
	}
	hint_top = (short)(panel.y1 - 2 * line_height - 6);
	draw_quad(&panel, game_chat_pixel(0.72f, 0.02f, 0.05f, 0.09f));
	place = panel;
	place.y1 = (short)(panel.y0 + 2);
	draw_quad(&place, game_chat_pixel(0.9f, 0.45f, 0.70f, 1.0f));
	top = (short)(panel.y0 + 6);
	left = (short)(panel.x0 + 10);
	/* (its title, and the channels: the chosen one lit) */
	color.alpha = 1.0f;
	color.red = 0.85f; color.green = 0.92f; color.blue = 1.0f;
	draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);
	game_chat_widen(wide, NUMBEROF(wide), T("QUICK CHAT"));
	place.x0 = left; place.x1 = panel.x1; place.y0 = top; place.y1 = (short)(top + line_height);
	rasterizer_draw_unicode_string(&place, NULL, NULL, 0, wide);
	top = (short)(top + row_height);
	{
		short x = left;

		for (channel = 0; channel < GAME_CHAT_WHEEL_CHANNELS; channel++)
		{
			rectangle2d tab;
			short tab_width;

			snprintf(text, sizeof(text), " %s ", game_chat_channel_name(channel));
			game_chat_widen(wide, NUMBEROF(wide), text);
			place.x0 = x; place.x1 = panel.x1; place.y0 = top; place.y1 = (short)(top + line_height);
			draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);
			tab_width = game_chat_width(&place, wide);
			tab.x0 = x; tab.x1 = (short)(x + tab_width); tab.y0 = (short)(top - 1); tab.y1 = (short)(top + line_height + 1);
			if (channel == game_chat.wheel_channel)
				draw_quad(&tab, game_chat_pixel(0.95f, 0.30f, 0.55f, 0.95f));
			else
				draw_quad(&tab, game_chat_pixel(0.5f, 0.15f, 0.18f, 0.24f));
			color.red = color.green = color.blue = channel == game_chat.wheel_channel ? 1.0f : 0.65f;
			draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);
			rasterizer_draw_unicode_string(&place, NULL, NULL, 0, wide);
			x = (short)(x + tab_width + 6);
		}
	}
	top = (short)(top + row_height + 3);
	/* (the phrases, the chosen one lit; then Type a message) */
	for (entry = 0; entry < GAME_CHAT_WHEEL_ENTRIES && top + line_height <= hint_top - 2; entry++)
	{
		boolean chosen = entry == game_chat.wheel_entry;

		if (chosen)
		{
			rectangle2d bar;

			bar.x0 = (short)(panel.x0 + 4); bar.x1 = (short)(panel.x1 - 4);
			bar.y0 = (short)(top - 1); bar.y1 = (short)(top + line_height + 1);
			draw_quad(&bar, game_chat_pixel(0.85f, 0.20f, 0.42f, 0.78f));
		}
		if (entry < GAME_CHAT_PRESET_COUNT)
			snprintf(text, sizeof(text), "%s", T(game_chat_phrases[entry]));
		else
			snprintf(text, sizeof(text), "%s", T("Type a message..."));
		game_chat_widen(wide, NUMBEROF(wide), text);
		color.red = color.green = color.blue = chosen ? 1.0f : 0.80f;
		if (entry == GAME_CHAT_PRESET_COUNT && !chosen)
		{
			color.red = 0.65f; color.green = 0.85f; color.blue = 1.0f;
		}
		draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);
		place.x0 = (short)(left + 4); place.x1 = panel.x1; place.y0 = top; place.y1 = (short)(top + line_height);
		rasterizer_draw_unicode_string(&place, NULL, NULL, 0, wide);
		top = (short)(top + row_height);
	}
	/* (the buttons) */
#ifdef HALO_VITA
	snprintf(text, sizeof(text), "%s", T("Up/Down: phrase  L/R: channel  Cross: send  Triangle: type  Circle: close"));
#else
	snprintf(text, sizeof(text), "%s", T("Up/Down: phrase  LB/RB: channel  A: send  Y: type  B: close"));
#endif
	game_chat_widen(wide, NUMBEROF(wide), text);
	color.red = 0.70f; color.green = 0.76f; color.blue = 0.84f;
	draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);
	place.x0 = left; place.x1 = (short)(panel.x1 - 4); place.y0 = (short)(hint_top + line_height);
	place.y1 = (short)(place.y0 + line_height);
	{
		/* (on two rows when it is wider than the panel) */
		int fit = game_chat_fit(&place, wide, 0, (short)(place.x1 - place.x0));

		if (wide[fit])
		{
			wchar_t second[160];
			int length = 0;

			while (wide[fit + length] && length < (int)NUMBEROF(second) - 1)
			{
				second[length] = wide[fit + length];
				length++;
			}
			second[length] = 0;
			wide[fit] = 0;
			place.y0 = (short)(place.y0 - line_height);
			place.y1 = (short)(place.y1 - line_height);
			rasterizer_draw_unicode_string(&place, NULL, NULL, 0, wide);
			place.y0 = (short)(place.y0 + line_height);
			place.y1 = (short)(place.y1 + line_height);
			rasterizer_draw_unicode_string(&place, NULL, NULL, 0, second);
		}
		else
			rasterizer_draw_unicode_string(&place, NULL, NULL, 0, wide);
	}
}

void game_chat_draw(void)
{
	unsigned long now = system_milliseconds();
	long font_index;
	struct font_header *font;
	short line_height, bottom, shown_most, drawn = 0;
	rectangle2d bounds, column;
	boolean history;
	short index;

	if (halo_main_menu_loaded() || !game_in_progress())
		return;
	font_index = interface_get_tag_index(_interface_font_terminal);
	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0)
		return;
	pthread_mutex_lock(&game_chat_lock);
	if (!game_chat.enabled)
	{
		pthread_mutex_unlock(&game_chat_lock);
		return;
	}
	bounds = render.camera.window_bounds;
	offset_rectangle2d(&bounds, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
	history = game_chat.wheel_open;
	if (game_chat.wheel_open && !game_chat.peek)
		game_chat_draw_wheel(&bounds, line_height, font_index);
	/* (the lines: the newest at the bottom, above the motion tracker) */
	column.x0 = (short)(bounds.x0 + 14);
	column.x1 = (short)(bounds.x0 + (bounds.x1 - bounds.x0) * (history && !game_chat.peek ? 54 : 60) / 100);
	column.y0 = bounds.y0;
	column.y1 = bounds.y1;
	bottom = (short)(bounds.y0 + (bounds.y1 - bounds.y0) * 73 / 100);
	shown_most = history ? GAME_CHAT_LINES_HISTORY : GAME_CHAT_LINES_SHOWN;
	for (index = 0; index < game_chat.line_count && drawn < shown_most; index++)
	{
		struct game_chat_shown *line =
			&game_chat.lines[(game_chat.newest - index + GAME_CHAT_LINES_KEPT) % GAME_CHAT_LINES_KEPT];
		unsigned long age = now - line->time;
		real alpha = 1.0f;
		short rows;

		if (!line->time)
			continue;
		if (!history)
		{
			if (age >= GAME_CHAT_LINE_MILLISECONDS + GAME_CHAT_FADE_MILLISECONDS)
				continue;
			if (age > GAME_CHAT_LINE_MILLISECONDS)
				alpha = 1.0f - (real)(age - GAME_CHAT_LINE_MILLISECONDS) / (real)GAME_CHAT_FADE_MILLISECONDS;
		}
		rows = game_chat_draw_line(line, alpha, &column, bottom, line_height, font_index);
		bottom = (short)(bottom - rows * (line_height + 1) - 1);
		drawn++;
		if (bottom < bounds.y0 + (bounds.y1 - bounds.y0) * 12 / 100)
			break;
	}
	pthread_mutex_unlock(&game_chat_lock);
}
