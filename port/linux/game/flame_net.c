/*
FLAME_NET.C

The tester's flaming helmet's proofs carried in the game's own messages
(flame_net.h; what they hold and how they are checked:
port/linux/src/p2p_flame.inc): decoded here and handed to the checks, the
checks' packets encoded and sent, each frame, from the game's main loop.
Never on the crossplay wire; in a split screen game (no network) nothing.
*/

#include "cseries.h"
#include "networking/network_messages.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_game_protocol.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "networking/network_server_manager_internal.h"
#include "halo_port_limits.h"

#include "flame_net.h"
#include "network_wire.h"
#include "../src/p2p_friends.h"

#include <string.h>

boolean network_game_client_send_to_server(struct network_game_client *client, void *message);

/* (network_messages.c's packet table: the classes of the messages) */
#define FLAME_NET_CLASS_CLIENT 3
#define FLAME_NET_CLASS_SERVER 2

static boolean flame_net_decode(unsigned short *message, short message_size, short expected_type, long packet_class,
	struct flame_net_message *decoded)
{
	short packet_type = expected_type;
	short packet_version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;

	message_size -= sizeof(unsigned short);
	if (message_size <= 0 ||
		!decode_network_game_message(decoded, message + 1, &message_size, &packet_type, &packet_version, packet_class) ||
		packet_type != expected_type)
	{
		return FALSE;
	}
	return decoded->version == FLAME_NET_VERSION && decoded->size > 0 && decoded->size <= FLAME_NET_BYTES;
}

void flame_net_client_handle_message(struct network_game_client *client, unsigned short *message, short message_size)
{
	struct flame_net_message decoded;

	(void)client;
	if (!flame_net_decode(message, message_size, _message_server_flame, FLAME_NET_CLASS_SERVER, &decoded))
	{
		network_event("flame net: a host's message that could not be decoded");
		return;
	}
	p2p_flame_heard(-1, decoded.bytes, decoded.size);
}

void flame_net_server_handle_message(struct network_game_server *server,
	struct network_game_server_client_machine *machine, unsigned short *message, short message_size)
{
	struct flame_net_message decoded;
	long machine_index = NONE;

	if (!flame_net_decode(message, message_size, _message_client_flame, FLAME_NET_CLASS_CLIENT, &decoded))
	{
		network_event("flame net: a joiner's message that could not be decoded");
		return;
	}
	/* (the sender's machine as this host has it) */
	network_game_server_get_client_machine(server, machine, &machine_index);
	if (!VALID_INDEX(machine_index, HALO_PORT_MAXIMUM_NETWORK_MACHINES))
		return;
	p2p_flame_heard((int)machine_index, decoded.bytes, decoded.size);
}

void flame_net_update(void)
{
	struct network_game_client *client = global_network_game_client_get();
	struct network_game_server *server = global_network_game_server_get();
	struct flame_net_message outgoing;
	int index, machine, size;

	if (!client || network_game_is_splitscreen_local() || network_wire_crossplay())
	{
		p2p_flame_sent();
		return;
	}
	p2p_flame_update();
	for (index = 0; (size = p2p_flame_outgoing(index, &machine, outgoing.bytes, FLAME_NET_BYTES)) >= 0; index++)
	{
		void *encoded;

		csmemset(outgoing.bytes + size, 0, (size_t)(FLAME_NET_BYTES - size));
		outgoing.version = FLAME_NET_VERSION;
		outgoing.size = (uint16_t)size;
		if (machine >= 0)
		{
			if (!server)
				continue;
			encoded = create_network_game_message(_message_server_flame, &outgoing, sizeof(outgoing));
			if (!encoded || !network_game_server_port_send_to_machine(machine, encoded))
				network_event("flame net: a packet to machine #%d could not be sent", machine);
		}
		else
		{
			encoded = create_network_game_message(_message_client_flame, &outgoing, sizeof(outgoing));
			if (!encoded || !network_game_client_send_to_server(client, encoded))
				network_event("flame net: a packet to the host could not be sent");
		}
	}
	p2p_flame_sent();
}
