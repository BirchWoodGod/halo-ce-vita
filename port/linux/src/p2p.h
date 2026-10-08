/*
P2P.H

Internet play (p2p.c): machines that shared an invite reach each other's
system link games as if they were on one LAN. xnet.c routes the game's
traffic for them through here; sdl_platform.c passes invite links in and
out through the clipboard.

Addresses and ports are in network byte order.
*/

#ifndef __HALO_LINUX_P2P_H
#define __HALO_LINUX_P2P_H

/* starts internet play, if network.online is set, when the game starts
its networking; local_address is the address the game's sockets are
reached at (network.address, else 127.0.0.1) */
void p2p_initialize(unsigned long local_address);

/* on the desktop, before anything else: if this process was started with
an invite link (halo://join/...) and another copy of the game is running,
passes the link to it and returns nonzero (this one should quit) */
int p2p_hand_off_invite(void);

/* joins the game an invite link or code leads to; text may hold other
words around it. Returns nonzero if it held an invite */
int p2p_join_invite(const char *text);

/* whether internet play (or ad hoc play) runs: its thread started */
int p2p_running(void);

/* this machine's identifier, which its XNADDR carries (6 bytes) */
const unsigned char *p2p_identifier(void);
/* the address the game reaches the machine with this identifier at, if it
is an internet play peer (reached yet or not) */
int p2p_peer_address(const unsigned char *identifier, unsigned long *address);

/* a destination the game sends to or connects to (stream: a TCP socket;
socket: the game's UDP socket connected to it, else -1): 1 if it is a peer's
address, rewritten to the local address standing in for it; -1 if it is a
peer's (or was) but the peer cannot be reached now (the game's traffic must
not go to the address itself); 0 if it is not a peer's */
int p2p_outgoing(int stream, int socket, unsigned long *address, unsigned short *port);
/* a source the game received from, accepted from or is connected to: if
it is one standing in for a peer, the peer's address */
int p2p_incoming(int stream, unsigned long *address, unsigned short *port);
/* the local addresses standing in for this port (a broadcast's) on every
peer; returns their count */
int p2p_broadcast_targets(unsigned short port, unsigned long *addresses, unsigned short *ports, int maximum_count);
/* a datagram the game sends from a socket with the local port source_port
(bound: not 0) to address and port, sealed onto the tunnel at once (no
stand-in carries it; one is made for the answers when they come): 1 if it
is a peer's address (sent, or lost as a datagram may be); -1 if it is a
peer's (or was) but the peer cannot be reached now; 0 if it is not a
peer's */
int p2p_send_datagram(unsigned short source_port, unsigned long address, unsigned short port, const void *data,
	int size);
/* the same for a broadcast to port: to every peer the tunnel reaches;
returns their count */
int p2p_broadcast_datagram(unsigned short source_port, unsigned short port, const void *data, int size);

/* whether a source the game received from, or accepted from, before
p2p_incoming rewrote it, is a peer's virtual address (or one that was a
peer's): the game's own socket was sent to, or connected to, directly from
it, as only a spoofed source is (a peer's traffic arrives from its
stand-ins); the game drops it */
int p2p_spoofed_source(unsigned long address);

/* the game's socket has this local port: bound, given one, or listening
(stream and listening: it is hosting). Peers reach only these ports (a
stream's only while it listens), and datagram ports it sent them from */
void p2p_socket_port(int socket, int stream, int listening, unsigned short port);
/* a socket that peers are not to reach (bound to 127.0.0.1 alone) has this
local port: traffic from it is not a peer's whose stand-in had the port */
void p2p_port_taken(int stream, unsigned short port);
/* the game closes a socket, a datagram one of this local port (0: none, or
a stream's) */
void p2p_socket_closed(int socket, unsigned short datagram_port);

/* text for the clipboard (a new invite link), once; NULL if none. Called
from the main thread */
const char *p2p_take_clipboard_text(void);

/* the hosted game's players and the most it takes, which Discord shows
(0, 0: not hosting; until the game says, the machines the tunnel reaches
are shown). The game's server calls it as they change (calling it with the
same counts again costs little) */
void p2p_set_game_player_counts(int count, int maximum);

/* the hosted game's details as the server browser lists them (p2p_lobby.c;
printable ASCII is kept; NULL leaves one as it was): the game's server calls
it as they change (calling it with the same again costs little). The
Vita's listings also carry the score to win (0: none), a co-op game's
difficulty (0 to 3; -1: not co-op), whether the map is a Halo PC (Custom
Edition) one, and the players' names (each ended by a newline) */
void p2p_set_game_listing(const char *name, const char *map, const char *gametype, int engine_type, int open,
	int in_progress, int has_teams);
void p2p_set_game_listing_details(int score_limit, int coop_difficulty, int pc_map, const char *player_names);

/* the sizes of a Discord user's id and name as kept (with their end), and
the text kept of either as told: only the characters allowed (digits in an
id; letters, digits, "_", "." and "-" in a name), no longer than that */
enum
{
	P2P_DISCORD_ID_SIZE = 24,
	P2P_DISCORD_NAME_SIZE = 40,
};
void p2p_discord_sanitize(char *destination, int size, const char *source, int name);
/* the Discord user signed in to the client on this machine, as its
READY told (empty if none, or internet play is off) */
void p2p_discord_identity(char *id, int id_size, char *name, int name_size);
/* this machine's hardware id, as hex (empty if none), a host told it when
joining (a keyed hash of what the machine is known by: p2p.c); and the hex
kept of one told: lowercase hex digits only, P2P_HARDWARE_ID_BYTES' worth */
enum
{
	P2P_HARDWARE_ID_BYTES = 16,
	P2P_HARDWARE_ID_SIZE = 2 * P2P_HARDWARE_ID_BYTES + 1,
};
void p2p_hardware_id(char *hex, int size);
void p2p_hardware_id_sanitize(char *destination, int size, const char *source);

/* the real address (network byte order) of the internet play peer of this
virtual address (network byte order): where its packets come from; 0 if
it is no peer's */
unsigned long p2p_peer_endpoint_address(unsigned long virtual_address);

/* ---------- short codes and the server browser (p2p_signal.c,
p2p_lobby.c), for a machine with no clipboard (the Vita's settings panel)
and for strangers. Every call takes p2p's lock (briefly) and may come from
any thread; nothing waits on the network or on a password's key. Plain ints
and chars only: the Vita's host side (another ABI) calls these too. */

enum
{
	/* "ABCD-EFGH" and a terminator */
	P2P_CODE_SIZE = 10,
	/* a listed game's host (its key's hash) in hex, and a terminator */
	P2P_LOBBY_ID_SIZE = 33,
	P2P_LOBBY_NAME_SIZE = 33,
	P2P_LOBBY_MAP_SIZE = 33,
	P2P_LOBBY_GAMETYPE_SIZE = 25,
	P2P_LOBBY_RULES_SIZE = 48,
	P2P_LOBBY_PLAYERS_SIZE = 64,
	/* a password's most characters (and a terminator) */
	P2P_LOBBY_PASSWORD_SIZE = 33,
};

/* the server browser's API as the Vita's settings panel knows it: 2, the
signed listings of OpenCE's server browser (1 was the code-based lobby) */
#define P2P_LOBBY_API 2

/* the letters and digits of a code: no 0, 1, I or O, which read alike */
#define P2P_CODE_ALPHABET "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"

/* a public game, as the browser shows it: every text printable ASCII */
struct p2p_lobby_entry
{
	/* what p2p_lobby_join takes: stays the game's while the list changes */
	char id[P2P_LOBBY_ID_SIZE];
	/* its name, validated as a player's name */
	char name[P2P_LOBBY_NAME_SIZE];
	/* the map's title ("Blood Gulch"; a map the Xbox did not ship: its
	scenario's name) and the gametype's ("Slayer") */
	char map[P2P_LOBBY_MAP_SIZE];
	char gametype[P2P_LOBBY_GAMETYPE_SIZE];
	/* "Slayer to 50 on Blood Gulch", "Co-op: The Pillar of Autumn, Heroic",
	then ": under way" or ": full or starting", and " (HALO PC)" */
	char rules[P2P_LOBBY_RULES_SIZE];
	/* "5 of 16: name, name... +3 more" */
	char players_line[P2P_LOBBY_PLAYERS_SIZE];
	int players, maximum, score_limit;
	/* (always: games of another network version are not listed) */
	int compatible;
	/* this machine's own game */
	int own;
	/* it has a password (p2p_lobby_join needs it) */
	int locked;
	/* on a Halo PC (Custom Edition) map */
	int pc_map;
	/* taking players; started; teams; joining it failed this run */
	int open, in_progress, has_teams, failed;
};

/* joins the game of a code: "ABCD-EFGH", with or without the dash, any
case. Returns nonzero if it was a code (internet play must be on). The
lobby's: a game listed in the public lobby, chosen there (its host is then
a stranger's, p2p_address_origin) */
int p2p_join_code(const char *code);
int p2p_join_lobby_code(const char *code);

/* how the machine the game reaches at this address (network byte order) is
reached: not through p2p (system link on the LAN), a peer joined from the
public lobby, one joined by an invite or a code (or hosting, a joiner), or
a machine of the ad hoc group */
enum
{
	P2P_ORIGIN_NONE,
	P2P_ORIGIN_PRIVATE,
	P2P_ORIGIN_PUBLIC,
	P2P_ORIGIN_ADHOC,
};
int p2p_address_origin(unsigned long address);

/* while this machine hosts with internet play on: copies its code (with
the dash) and returns nonzero */
int p2p_hosting_code(char *code, int size);
/* while this machine hosts for the internet: whether its game is listed in
everyone's server browser (public) or reached only by its code and invite
(private); going private makes a new invite, so that one seen in a listing
lets no one in (the code leads to the new one) */
void p2p_lobby_set_public(int listed);
/* the name the browser lists the game under ("": network.lobby_name, else
the game's own) */
void p2p_lobby_set_name(const char *name);
/* the game's password (NULL or "": none): its listing lets only those who
know it join from the browser (its code and invite still join it). Its key
is worked out on a thread of its own (about a second on a Vita); until then
the game is not listed. Setting or changing it makes a new invite */
void p2p_lobby_set_password(const char *password);
/* the server browser: on (the hosts are asked for their listings) or off;
REFRESH asks again; the index-th game in the order shown (the most players
first, then those not failed, the open ones, by name; this machine's own
too, marked own): nonzero if there is one */
void p2p_lobby_browse(int on);
void p2p_lobby_refresh(void);
int p2p_lobby_entry(int index, struct p2p_lobby_entry *entry);
/* joins a listed game (id: its entry's), with its password if it is locked
(the password's key is worked out on a thread of its own); nonzero if the
game is listed. How it goes: p2p_lobby_join_state */
int p2p_lobby_join(const char *id, const char *password);
enum
{
	P2P_LOBBY_JOIN_GONE = -2,
	P2P_LOBBY_JOIN_WRONG_PASSWORD = -1,
	P2P_LOBBY_JOIN_IDLE = 0,
	/* the password's key being worked out */
	P2P_LOBBY_JOIN_UNLOCKING = 1,
	/* its invite handed on: as an invite's join goes from here */
	P2P_LOBBY_JOIN_JOINING = 2,
};
int p2p_lobby_join_state(void);
/* a game the browser could not join: kept, marked failed, for this run */
void p2p_lobby_mark_failed(const char *id);
/* one line on what internet play is doing (for a menu); returns nonzero
if internet play runs */
int p2p_status(char *text, int size);
/* the same for ad hoc play (p2p_adhoc.c): in a group or not, with how many
other machines; returns nonzero if ad hoc play is on */
int p2p_adhoc_status(char *text, int size);

#endif
