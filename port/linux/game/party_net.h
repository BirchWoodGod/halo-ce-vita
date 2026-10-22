/*
PARTY_NET.H

A party's members on one team (party_net.c): a joiner in a party
(port/linux/src/p2p_party.h) tells the host its party's team token
(_message_client_party, past the flaming helmet's), just before each request
to add its player, so that the host has it when it picks the player's team.
A host that fills its game with bots (network_server_manager.c: a dedicated
server's sv_bots_fill, a 1.1.1 host's Bots online) puts a joiner whose token
is a player's in the game on that player's team, and a bot leaves that team
for it. Nothing else reads the token.

The token is the first PARTY_NET_TOKEN_BYTES of SHA-256 of a label and the
party's id (p2p_party_team_token): the party's members have the same one,
nobody else can make it from what the game shows, and it says nothing of
the party. A joiner that lies about it can choose its own team only, which
a player in a lobby can do anyway. A build without the message (1.1.0, an
older host) drops it as an unknown packet type; never sent on the crossplay
wire, nor in a split screen game.
*/

#ifndef HALO_PARTY_NET_H
#define HALO_PARTY_NET_H

#include <stdint.h>

#define PARTY_NET_VERSION 1
#define PARTY_NET_TOKEN_BYTES 8

/* a joiner's to its host (12 bytes: two shorts and the token on the wire) */
struct party_net_message
{
	uint16_t version;
	uint16_t size;
	uint8_t token[PARTY_NET_TOKEN_BYTES];
};

typedef char party_net_message_size_assert[sizeof(struct party_net_message) == 12 ? 1 : -1];

struct network_game_client;
struct network_game_server;
struct network_game_server_client_machine;

/* (network_client_manager.c, before a request to add its first player) the
party's token sent to the host, when this machine is in a party */
void party_net_client_send(struct network_game_client *client);
/* (friends_game.c, each frame) a joiner in a game whose party's token is
new since it last told its host (a party formed after it joined: a leader
who joined first, then invited) tells it again, once a second at most */
void party_net_update(void);
/* (network_server_message_handler.c) a joiner's message: its machine's
token kept */
void party_net_server_handle_message(struct network_game_server *server,
	struct network_game_server_client_machine *machine, unsigned short *message, short message_size);
/* (a machine accepted into the game) the token of the machine that had its
index before forgotten */
void party_net_server_machine_joined(long machine_index);
/* the token a machine (the game's index) told, nonzero if it told one */
int party_net_machine_token(long machine_index, uint8_t *token);

#endif
