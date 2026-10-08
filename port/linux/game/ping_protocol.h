/*
PING_PROTOCOL.H

The rules of the scoreboard's pings in a network game (network_distributed.c,
_distributed_message_pings; latency_meter.c shows them): every two seconds
the host sends every client each player's ping as the host measures it (the
distributed netcode's round trip to that player's machine, timed in
milliseconds; its own players' 0), so that every machine's scoreboard has
everyone's. A client never reports its own: what the host sends is the
host's measure alone. This unit holds what can be checked without the game,
so that it is fuzzed on its own (port/vita/tests/net_fuzz_ping.c).

Every field comes from the network and is checked here before use (a host
is a stranger too):
- a message names each player at most once, by an index below the game's
  most players, its pad 0, its milliseconds at most PING_MAXIMUM_MILLISECONDS
  or PING_UNKNOWN, no more entries than players; one that breaks any of it
  is refused whole, nothing kept;
- a client takes one message every PING_MINIMUM_INTERVAL_MILLISECONDS at
  most (the host sends one every PING_INTERVAL_MILLISECONDS), the rest
  dropped unread;
- what was told is shown for PING_TOLD_MILLISECONDS, then forgotten (a
  host that stops telling leaves nothing stale).

From upstream OpenCE's scoreboard (d1c7243c: _distributed_message_pings,
four bytes a player), its number 19 kept.
*/

#ifndef __PING_PROTOCOL_H
#define __PING_PROTOCOL_H

#include <stdint.h>

/* ---------- constants */

/* a ping not known, and the longest told (milliseconds) */
#define PING_UNKNOWN 0xFFFFu
#define PING_MAXIMUM_MILLISECONDS 9999
/* how often the host tells them, the least a client takes them apart, and
how long a client shows what it was told */
#define PING_INTERVAL_MILLISECONDS 2000
#define PING_MINIMUM_INTERVAL_MILLISECONDS 500
#define PING_TOLD_MILLISECONDS 6000

/* ---------- structures */

/* a player's ping, as the host measures it (little-endian, as every
machine is) */
struct ping_entry
{
	uint8_t player_index;
	uint8_t pad;
	uint16_t milliseconds;
};

typedef char ping_entry_size_assert[sizeof(struct ping_entry) == 4 ? 1 : -1];

/* (a client) when it last took a message */
struct ping_receiver
{
	uint32_t last;
	int taken;
};

/* ---------- prototypes/PING_PROTOCOL.C */

/* (the host) a player's ping for a message: NONE (-1, not known) as
PING_UNKNOWN, the rest within 0 and PING_MAXIMUM_MILLISECONDS */
uint16_t ping_entry_value(long milliseconds);
/* (a client) a message's count entries, checked: into table (players of
them, PING_UNKNOWN for each player the message does not name) the number
taken, or -1 for a message refused (table untouched) */
int ping_entries_read(void const *entries, int count, uint16_t *table, int players);
/* (a client) whether a message coming now is taken (and if so, now noted):
one every PING_MINIMUM_INTERVAL_MILLISECONDS (a receiver zeroed has taken
none) */
int ping_message_due(struct ping_receiver *receiver, uint32_t now);

#endif
