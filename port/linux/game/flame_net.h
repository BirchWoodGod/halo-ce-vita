/*
FLAME_NET.H

The tester's flaming helmet's proofs in the game's own messages
(port/linux/src/p2p_flame.inc says what they hold and how each machine
checks them): a joiner's to its host (_message_client_flame), the host's
to a joiner (_message_server_flame), in any network game, online, System
Link, ad hoc or a dedicated server's, in the lobby and in play. A build
without them drops them as unknown packet types (its host relays none, its
joiners answer none). Never on the crossplay wire.
*/

#ifndef HALO_FLAME_NET_H
#define HALO_FLAME_NET_H

#include <stdint.h>

#define FLAME_NET_VERSION 1
/* the largest packet (an answer: a card's header, the key, the signature) */
#define FLAME_NET_BYTES 116

/* both ways (120 bytes: two shorts and the packet's bytes on the wire) */
struct flame_net_message
{
	uint16_t version;
	uint16_t size;
	uint8_t bytes[FLAME_NET_BYTES];
};

typedef char flame_net_message_size_assert[sizeof(struct flame_net_message) == 120 ? 1 : -1];

struct network_game_client;
struct network_game_server;
struct network_game_server_client_machine;

/* (the message handlers) a host's message to this joiner, a joiner's to
this host: handed to the checks (p2p_flame_heard) */
void flame_net_client_handle_message(struct network_game_client *client, unsigned short *message, short message_size);
void flame_net_server_handle_message(struct network_game_server *server,
	struct network_game_server_client_machine *machine, unsigned short *message, short message_size);
/* (friends_game.c, each frame) the checks' work, and their packets sent */
void flame_net_update(void);

#endif
