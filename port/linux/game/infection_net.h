/*
INFECTION_NET.H

The game's rules a machine plays, asked in a network game's lobby
(infection_net.c): a host whose game type is one only newer builds play
(Infection: an older build plays it as Team Slayer, its zombies' sides and
the host's state unknown to it) says hello to each machine that joins (the
Bungie message _message_server_game_rules, past network Forge's); a machine
that plays the rules answers (_message_client_game_rules) with the rules it
plays, a bit each. One that does not answer in GAME_RULES_HELLO_MILLISECONDS
- an older build, which drops a message type it does not know - or answers
without the game's rule is refused from that game alone (the rejection an
older build shows as its own "version too old"), and plays any other. No
network version changes; a game of the other game types sends none of it.
Never on the crossplay wire (a PC host or client has none of it).
*/

#ifndef HALO_INFECTION_NET_H
#define HALO_INFECTION_NET_H

#include <stdint.h>

/* the protocol's version */
#define GAME_RULES_NET_VERSION 1
/* the rules a machine plays (its answer's bits) */
#define GAME_RULES_INFECTION 0x00000001u
#define GAME_RULES_KNOWN GAME_RULES_INFECTION
/* how long a joiner has to answer the host's hello, and how often the host
says it again until it does */
#define GAME_RULES_HELLO_MILLISECONDS 6000
#define GAME_RULES_HELLO_RESEND_MILLISECONDS 1000

enum
{
	_game_rules_net_hello = 1,
	_game_rules_net_answer,
	NUMBER_OF_GAME_RULES_NET_COMMANDS
};

/* both ways (16 bytes: two shorts, three longs on the wire) */
struct game_rules_net_message
{
	uint16_t command;
	uint16_t version;
	/* the host's: the rules its game needs; the joiner's: the rules it plays */
	uint32_t rules;
	/* the host's hosting (the hello's, echoed by the answer) */
	uint32_t session;
	uint32_t reserved;
};

typedef char game_rules_net_message_size_assert[sizeof(struct game_rules_net_message) == 16 ? 1 : -1];

struct network_game_client;
struct network_game_server;
struct network_game_server_client_machine;

/* (main.c, each frame) the host's hellos and refusals */
void game_rules_net_frame(
	void);
/* network_client_message_handler.c: the host's hello, answered */
void game_rules_net_client_handle_message(
	struct network_game_client *client,
	unsigned short *message,
	short message_size);
/* network_server_message_handler.c: a joiner's answer */
void game_rules_net_server_handle_message(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	unsigned short *message,
	short message_size);
/* (tests: HALO_GAME_RULES_NET=0 makes a machine one that does not answer,
as an older build) */

#endif
