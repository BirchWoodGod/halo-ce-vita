/*
CHAT_PROTOCOL.H

The rules of game chat (chat.c): a player's quick chat phrase or typed
line goes to the host over the game's own connection to it, and the host,
which names who said it from that connection's own players (a joiner never
names anyone), passes it on to every machine, or to those with a player of
the sender's team. This unit holds what can be checked without the game,
so that it is tested and fuzzed on its own (port/vita/tests/chat_test.c,
net_fuzz_chat.c): the messages' fields, what is kept of their text and of a
player's name, the links refused, and the flood limit.

Every field comes from the network and is checked here before use, on the
host and again on every machine that shows it (a host is a stranger too):
- a quick chat phrase travels as its number (CHAT_PHRASES), and every
  machine shows its own text for it, so Quick chat only never shows a
  stranger's words;
- typed text is printable ASCII only, at most CHAT_MAXIMUM_TEXT_LENGTH
  characters; control characters go, the game's text codes ('|', which
  starts one in draw_string.c: '|n' a new line, and a '|' at the end reads
  past the string) become '/', runs of spaces become one; a line with a
  link in it (an address, a web site's name or an IP address, its dots
  written as "(.)", "[dot]" and the like too) is refused;
- names are kept as the host's own record has them, printable ASCII, at
  most 11 characters (a player's name, players.h);
- a machine's lines are limited (chat_bucket: CHAT_BURST at once, then one
  every CHAT_REFILL_MILLISECONDS), and the host's relaying as a whole too;
- the host's own Game chat is its game's: Off, it passes on no line, Quick
  chat only, no typed one (chat_host_notice), and it tells the sender why
  with a notice, a relay of _chat_kind_notice to that machine alone, which
  carries a number (CHAT_NOTICE_*) and no words;
- a player muted (chat_mutes) is known by the machine and controller the
  game has it at, so a new name does not hear them again, and by name, so
  one who leaves and joins again stays muted; on the host a mute is its
  game's: no one hears that player.

The link check is from OpenCE PR #72 ("Text chat in games", smokeyllama:
chat_text_has_link, chat_text_clean).
*/

#ifndef __CHAT_PROTOCOL_H
#define __CHAT_PROTOCOL_H

#include <stdint.h>

/* ---------- constants */

/* the longest typed line, and the bytes it travels in (its end included,
rounded up to four) */
#define CHAT_MAXIMUM_TEXT_LENGTH 80
#define CHAT_TEXT_BYTES 84
/* a player's name as the game keeps it (players.h: twelve wide characters,
its end included), and as chat shows it */
#define CHAT_NAME_CHARACTERS 12
#define CHAT_NAME_BYTES 16
/* the most phrases there may be (the table below has CHAT_PHRASE_COUNT) */
#define CHAT_PHRASE_COUNT 8

/* a machine's lines: CHAT_BURST at once, then one every
CHAT_REFILL_MILLISECONDS (the host drops the rest; a joiner keeps to the
same, telling its player how long to wait) */
#define CHAT_BURST 3
#define CHAT_REFILL_MILLISECONDS 2000
/* the host's relaying as a whole, whoever sends: CHAT_HOST_BURST at once,
then one every CHAT_HOST_REFILL_MILLISECONDS */
#define CHAT_HOST_BURST 12
#define CHAT_HOST_REFILL_MILLISECONDS 250
/* what a machine shows of what its host relays: the same as the host's own
limit (a host past it is flooding) */
#define CHAT_SHOWN_BURST CHAT_HOST_BURST
#define CHAT_SHOWN_REFILL_MILLISECONDS CHAT_HOST_REFILL_MILLISECONDS

/* a message's kind */
enum
{
	_chat_kind_quick,
	_chat_kind_typed,
	/* (a relay alone: the host's word to one machine, its phrase a
	CHAT_NOTICE_*) */
	_chat_kind_notice,
	NUMBER_OF_CHAT_KINDS
};

/* the host's notices, by their number on the wire (a number keeps its
notice for good) */
enum
{
	/* the host's Game chat is Off: it passes on nothing */
	_chat_notice_host_off,
	/* the host's Game chat is Quick chat only: no typed lines */
	_chat_notice_host_quick,
	NUMBER_OF_CHAT_NOTICES
};

/* the players a machine's player may mute at once (for the run of the
game) */
#define CHAT_MUTED_PLAYERS 32

/* a message's flags */
enum
{
	/* to the sender's team only (in a game with teams: else everyone) */
	_chat_flag_team_bit,
	NUMBER_OF_CHAT_FLAGS
};

#define CHAT_VALID_FLAGS ((1 << NUMBER_OF_CHAT_FLAGS) - 1)

/* ---------- structures */

/* _message_client_chat (network_messages.c: four shorts, then the text's
bytes): a player's line, to the host */
struct chat_request_message
{
	int16_t kind;
	/* the phrase (_chat_kind_quick) */
	int16_t phrase;
	int16_t flags;
	/* which of the machine's players said it (its controller) */
	int16_t local_player;
	/* (_chat_kind_typed) ended, the rest zero */
	char text[CHAT_TEXT_BYTES];
};

/* _message_server_chat (six shorts, the name's twelve, the text's bytes):
a line as the host passes it on */
struct chat_relay_message
{
	int16_t kind;
	int16_t phrase;
	int16_t flags;
	/* the sender's team (NONE: no teams), and its place in the game's
	player list */
	int16_t team;
	int16_t player;
	int16_t pad;
	/* the sender's name, as the host's record of the game has it */
	uint16_t name[CHAT_NAME_CHARACTERS];
	char text[CHAT_TEXT_BYTES];
};

/* a flood limit: the time saved up (a line costs one refill's), at most
the burst's */
struct chat_bucket
{
	uint32_t credit_milliseconds;
	uint32_t last_milliseconds;
	int started;
};

/* a player muted: by name, and by the machine and controller the game had
them at (machine NONE: by name alone) */
struct chat_mute
{
	char name[CHAT_NAME_BYTES];
	int16_t machine;
	int16_t controller;
};

struct chat_mutes
{
	struct chat_mute entries[CHAT_MUTED_PLAYERS];
	int count;
};

/* ---------- prototypes */

/* the phrase's text (English), or NULL past the table */
char const *chat_phrase_text(int phrase);
int chat_phrase_count(void);

/* the text as it may be shown: printable ASCII, '|' as '/', no spaces at
either end nor two together, at most CHAT_MAXIMUM_TEXT_LENGTH characters,
ended, in destination (size bytes); its length. The source is read up to
source_size bytes or its end, whichever comes first */
int chat_text_clean(char *destination, int size, char const *source, int source_size);

/* whether the text has a link in it (OpenCE PR #72's chat_text_has_link) */
int chat_text_has_link(char const *text);

/* a player's name (CHAT_NAME_CHARACTERS wide characters at most, to its
end) as chat shows it: printable ASCII, a Latin letter with a mark as its
plain letter and others '?' (as the host's ban command reads a name, which
the host keeps names apart by: players.c's player_name_character_ascii),
'|' as '/', no spaces at either end but those within kept, so that two
names the host told apart stay apart; in destination (size bytes); its
length ("?" if nothing is left) */
int chat_name_clean(char *destination, int size, uint16_t const *name, int count);

/* a notice's text (English), or NULL past the table */
char const *chat_notice_text(int notice);

/* (the host) what its own Game chat (HALO_CHAT_MODE_*) does with a request
of the kind: -1 passes it on, else the notice the sender gets instead */
int chat_host_notice(int host_mode, int kind);

/* whether a request may be passed on; its text as it is to be shown
(typed) into text (CHAT_TEXT_BYTES) */
int chat_request_valid(struct chat_request_message const *request, char *text);

/* whether a relayed line may be shown; its name and text as they are to
be shown into name (CHAT_NAME_BYTES) and text (CHAT_TEXT_BYTES) */
int chat_relay_valid(struct chat_relay_message const *relay, char *name, char *text);

/* a player muted (1), heard again (1), or already so (0); name as chat
shows it, machine -1 if not known. Hearing again takes every entry of the
name or of the machine's controller */
int chat_mutes_set(struct chat_mutes *mutes, char const *name, int machine, int controller, int mute);
/* whether a line of the player of this name, at this machine and
controller (machine -1: not known), is muted */
int chat_mutes_match(struct chat_mutes const *mutes, char const *name, int machine, int controller);
/* each frame of a game, each other player: the name now at a muted
machine and controller (the name follows the player; another with a name a
muted player left is not muted for it), and one muted by name alone known
by its place again. A machine gone (machine -1: every one, the game left):
its players muted by name alone from now on */
void chat_mutes_player(struct chat_mutes *mutes, int machine, int controller, char const *name);
void chat_mutes_forget_machine(struct chat_mutes *mutes, int machine);

/* whether one more line may go through the limit now (it is then counted):
burst at once, then one every refill_milliseconds */
int chat_bucket_take(struct chat_bucket *bucket, uint32_t now_milliseconds, int burst, uint32_t refill_milliseconds);
/* how long until it takes one again (0: now) */
uint32_t chat_bucket_wait(struct chat_bucket const *bucket, uint32_t now_milliseconds, int burst,
	uint32_t refill_milliseconds);

#endif
