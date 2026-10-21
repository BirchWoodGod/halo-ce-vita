/*
GAME_CHAT_PROTOCOL.H

The in-game chat's rules (game_chat_protocol.c), with no game types, so that
the desktop tests and the network fuzz target build them alone
(port/vita/tests/chat_test.c, net_fuzz_chat.c).

On the wire the chat is OpenCE's text chat (its network_chat.c, kinds 87-89
of the distributed messages, docs/opence-net32-spec.md 8), so OpenCE players
on PC and the Vitas read each other's lines in one game, on either wire
(the Vitas' own and the crossplay one): a build without the chat drops the
kinds as unknown, so the network version stays 18 (29 and 32 on the
crossplay wire).

- 87 a line up: a client's player's line to the host, over the client's
  stream (reliably), count 1, one 68-byte entry: the sender's absolute
  player index (one of that machine's), the line's flags (team only,
  vehicle only), 2 pad bytes, 64 bytes of text ended by a NUL (63
  characters at most).
- 88 a line down: the host's relayed line to each machine that hears it
  (the same entry: the sender as the host found it), or a notice (the
  notice flag: its text alone, no sender), over the host's stream alone.
- 89 the host's chat settings, to every client as they change and every
  3 seconds, a 4-byte entry: its first byte's flags (chat allowed, links
  allowed). A client that has none has a host without the chat.

Everything that comes in is checked here before the game sees it: the
count, the size, the flags (bits not known dropped), the text cleaned to
printable ASCII ('|', the game's text codes' start, and the markup
characters out), no control character, nothing read past the entry.
*/

#ifndef __GAME_CHAT_PROTOCOL_H
#define __GAME_CHAT_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- constants */

enum
{
	/* the distributed messages' kinds (OpenCE's numbers) */
	GAME_CHAT_KIND_UP = 87,
	GAME_CHAT_KIND_DOWN = 88,
	GAME_CHAT_KIND_CONFIG = 89,

	/* a line's text with its end, and the most characters it holds (the
	wire's: OpenCE's) */
	GAME_CHAT_TEXT_SIZE = 64,
	GAME_CHAT_TEXT_LENGTH = GAME_CHAT_TEXT_SIZE - 1,
	/* the entries' sizes on the wire */
	GAME_CHAT_LINE_ENTRY_SIZE = 68,
	GAME_CHAT_CONFIG_ENTRY_SIZE = 4,

	/* a line's flags */
	GAME_CHAT_LINE_TEAM_BIT = 1 << 0,
	GAME_CHAT_LINE_VEHICLE_BIT = 1 << 1,
	GAME_CHAT_LINE_NOTICE_BIT = 1 << 2,
	GAME_CHAT_LINE_FLAGS = GAME_CHAT_LINE_TEAM_BIT | GAME_CHAT_LINE_VEHICLE_BIT | GAME_CHAT_LINE_NOTICE_BIT,

	/* the host's settings' flags */
	GAME_CHAT_CONFIG_ALLOWED_BIT = 1 << 0,
	GAME_CHAT_CONFIG_LINKS_BIT = 1 << 1,

	/* the host: a machine's lines, CHAT_BURST at once and one more each
	refill period after (OpenCE's: a PC client never hits ours) */
	GAME_CHAT_HOST_BURST = 5,
	GAME_CHAT_HOST_REFILL_MILLISECONDS = 1500,
	/* this machine's own lines, fewer and slower than any host takes, so
	that a host never drops one of ours */
	GAME_CHAT_SEND_BURST = 3,
	GAME_CHAT_SEND_REFILL_MILLISECONDS = 2000,
	/* the host's settings, sent again this often */
	GAME_CHAT_CONFIG_MILLISECONDS = 3000,

	/* the lines of one sender the filter masks before they are muted here,
	in this many milliseconds */
	GAME_CHAT_FILTER_TRIPS = 3,
	GAME_CHAT_FILTER_TRIP_MILLISECONDS = 5 * 60 * 1000,

	/* the players muted here at once (by name), and a name's room */
	GAME_CHAT_MUTES = 32,
	GAME_CHAT_NAME_SIZE = 16,
};

/* who a line is to (the wheel's ALL, TEAM and PARTY; a PC player's lines to
their vehicle are shown, never sent from here) */
enum game_chat_channel
{
	GAME_CHAT_CHANNEL_ALL,
	GAME_CHAT_CHANNEL_TEAM,
	GAME_CHAT_CHANNEL_PARTY,
	GAME_CHAT_CHANNEL_VEHICLE,
	/* (shown only) a notice: the host's, or this machine's own word */
	GAME_CHAT_CHANNEL_NOTICE,
	GAME_CHAT_CHANNEL_COUNT
};

/* the quick chat's phrases, our own words (sent as their English text, which
every machine shows in its own language: game_chat_preset_find) */
enum
{
	GAME_CHAT_PRESET_COUNT = 13
};

/* why a message was refused (game_chat_parse_*): none, or the reason */
enum
{
	GAME_CHAT_PARSE_OK,
	GAME_CHAT_PARSE_KIND,
	GAME_CHAT_PARSE_COUNT,
	GAME_CHAT_PARSE_SIZE,
	GAME_CHAT_PARSE_FLAGS,
	GAME_CHAT_PARSE_EMPTY,
	GAME_CHAT_PARSE_PLAYER,
	GAME_CHAT_PARSE_REASONS
};

/* ---------- structures */

/* a line as it came, checked (the text cleaned) */
struct game_chat_line
{
	unsigned char player_index;
	unsigned char flags;
	char text[GAME_CHAT_TEXT_SIZE];
};

/* a token bucket (the rate a machine or this one sends at) */
struct game_chat_rate
{
	int started;
	int tokens;
	unsigned long refilled;
};

/* the players muted on this machine: by name, in any case (a player who
leaves and joins again under it still is); the auto-mutes the filter made
are marked */
struct game_chat_mute
{
	char name[GAME_CHAT_NAME_SIZE];
	int automatic;
};

struct game_chat_mutes
{
	int count;
	struct game_chat_mute entries[GAME_CHAT_MUTES];
};

/* each sender's lines the filter masked lately (by name) */
struct game_chat_trips
{
	int count;
	struct
	{
		char name[GAME_CHAT_NAME_SIZE];
		int trips;
		unsigned long first;
	} entries[GAME_CHAT_MUTES];
};

/* ---------- prototypes */

/* a line's text for the wire or the screen: printable ASCII, '|' (the game's
text codes) and the markup characters ('%' '^' '<' '>' '\' '`') out, a
character it cannot take (a control character, a byte past ASCII) a '?',
an accented Latin letter typed in UTF-8 its plain one (the keyboard's
Spanish: "Buen tiro, compañero" is "Buen tiro, companero"; '¿' and '¡' out),
no spaces at its ends nor two together, at most destination_size - 1
characters, read to the source's end or source_size (what another machine
sent need not be ended); its length */
int game_chat_text_clean(char *destination, int destination_size, const char *source, int source_size);
/* whether the text has a link: a scheme, "www.", an address of four
numbers, or a name and a top-level domain, its dots also written "(dot)",
"[dot]" or " dot " (OpenCE's chat_text_has_link) */
int game_chat_text_has_link(const char *text);

/* a bucket's token taken now, if it has one: burst at once, one more each
refill_milliseconds */
int game_chat_rate_take(struct game_chat_rate *rate, unsigned long now, int burst, unsigned long refill_milliseconds);

/* a line's channel from its flags, and back */
int game_chat_channel_from_flags(unsigned char flags);
unsigned char game_chat_channel_flags(int channel);
/* whether a listener hears a line to that channel: all, everyone; the
team's, the same team (everyone in a game without teams); a vehicle's,
those in the sender's vehicle (none, NONE: -1) */
int game_chat_hears(int channel, int has_teams, long sender_team, long listener_team, long sender_vehicle,
	long listener_vehicle);

/* the entries of a message of the chat's kinds after its header: checked,
the text cleaned; GAME_CHAT_PARSE_OK or why not */
int game_chat_parse_line(const void *entries, unsigned long size, int count, struct game_chat_line *line);
int game_chat_parse_config(const void *entries, unsigned long size, int count, unsigned char *flags);
/* a line's entry for the wire (68 bytes: the text cleaned, the pad zero) */
void game_chat_line_entry(unsigned char *entry, unsigned char player_index, unsigned char flags, const char *text);

/* the quick chat phrases: the English (as sent), and the phrase a text is
(its English exactly), -1 for none */
const char *game_chat_preset_english(int preset);
int game_chat_preset_find(const char *text);

/* a name in the form mutes compare (lower case, at most
GAME_CHAT_NAME_SIZE - 1) */
void game_chat_name_key(char *key, const char *name);
/* whether a name is muted here; muted or unmuted (automatic: the filter's);
the count now */
int game_chat_mute_find(const struct game_chat_mutes *mutes, const char *name);
int game_chat_mute_set(struct game_chat_mutes *mutes, const char *name, int muted, int automatic);
/* a sender's line the filter masked, counted: nonzero when it is the trip
that mutes them (GAME_CHAT_FILTER_TRIPS in GAME_CHAT_FILTER_TRIP_MILLISECONDS) */
int game_chat_trip(struct game_chat_trips *trips, const char *name, unsigned long now);

#ifdef __cplusplus
}
#endif

#endif
