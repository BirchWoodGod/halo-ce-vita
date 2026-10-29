/*
PARTY_NET.C

A party's team token in the game's own messages (party_net.h): sent by a
joiner in a party before it asks for its player, kept by the host per
machine for network_server_manager.c's team of a person joining a game it
fills with bots.
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

#include "party_net.h"
#include "network_wire.h"
#include "../src/p2p_party.h"

#include <string.h>

boolean network_game_client_send_to_server(struct network_game_client *client, void *message);

/* (network_messages.c's packet table: the class of a joiner's messages) */
#define PARTY_NET_CLASS_CLIENT 3

static struct
{
	boolean known;
	uint8_t token[PARTY_NET_TOKEN_BYTES];
} party_net_machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];

/* (network_client_manager.c's client states) */
enum
{
	PARTY_NET_CLIENT_STATE_PREGAME = 2,
	PARTY_NET_CLIENT_STATE_INGAME = 3,
};

/* the token last told this game's host (told: forgotten while this machine
is in no game's lobby or play, so a new game's host is told again) */
static struct
{
	boolean told;
	uint8_t token[PARTY_NET_TOKEN_BYTES];
} party_net_client;

void party_net_client_send(struct network_game_client *client)
{
	struct party_net_message outgoing;
	void *encoded;

	if (!client || network_game_is_splitscreen_local() || network_wire_crossplay())
		return;
	csmemset(&outgoing, 0, sizeof(outgoing));
	if (!p2p_party_team_token(outgoing.token, PARTY_NET_TOKEN_BYTES))
		return;
	party_net_client.told = TRUE;
	csmemcpy(party_net_client.token, outgoing.token, PARTY_NET_TOKEN_BYTES);
	outgoing.version = PARTY_NET_VERSION;
	outgoing.size = PARTY_NET_TOKEN_BYTES;
	encoded = create_network_game_message(_message_client_party, &outgoing, sizeof(outgoing));
	if (!encoded || !network_game_client_send_to_server(client, encoded))
		network_event("party net: the party's token could not be sent to the host");
}

void party_net_server_handle_message(struct network_game_server *server,
	struct network_game_server_client_machine *machine, unsigned short *message, short message_size)
{
	struct party_net_message decoded;
	short packet_type = _message_client_party;
	short packet_version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;
	long machine_index = NONE;

	message_size -= sizeof(unsigned short);
	if (message_size <= 0 ||
		!decode_network_game_message(&decoded, message + 1, &message_size, &packet_type, &packet_version,
			PARTY_NET_CLASS_CLIENT) ||
		packet_type != _message_client_party || decoded.version != PARTY_NET_VERSION ||
		decoded.size != PARTY_NET_TOKEN_BYTES)
	{
		network_event("party net: a joiner's message that could not be decoded");
		return;
	}
	/* (the sender's machine as this host has it) */
	network_game_server_get_client_machine(server, machine, &machine_index);
	if (!VALID_INDEX(machine_index, HALO_PORT_MAXIMUM_NETWORK_MACHINES))
		return;
	if (!party_net_machines[machine_index].known ||
		csmemcmp(party_net_machines[machine_index].token, decoded.token, PARTY_NET_TOKEN_BYTES))
	{
		network_event("party net: machine #%ld is in a party", machine_index);
	}
	party_net_machines[machine_index].known = TRUE;
	csmemcpy(party_net_machines[machine_index].token, decoded.token, PARTY_NET_TOKEN_BYTES);
}

void party_net_server_machine_joined(long machine_index)
{
	if (VALID_INDEX(machine_index, HALO_PORT_MAXIMUM_NETWORK_MACHINES))
		csmemset(&party_net_machines[machine_index], 0, sizeof(party_net_machines[machine_index]));
}

int party_net_machine_token(long machine_index, uint8_t *token)
{
	if (!VALID_INDEX(machine_index, HALO_PORT_MAXIMUM_NETWORK_MACHINES) || !party_net_machines[machine_index].known)
		return 0;
	if (token)
		csmemcpy(token, party_net_machines[machine_index].token, PARTY_NET_TOKEN_BYTES);
	return 1;
}

void party_net_update(void)
{
	struct network_game_client *client = global_network_game_client_get();
	uint8_t token[PARTY_NET_TOKEN_BYTES];
	short state = client ? network_game_client_get_state(client, NULL) : NONE;

#ifdef HALO_DEDICATED_SERVER
	/* (no party, no player: nothing to tell) */
	return;
#endif

	if (state != PARTY_NET_CLIENT_STATE_PREGAME && state != PARTY_NET_CLIENT_STATE_INGAME)
	{
		party_net_client.told = FALSE;
		return;
	}
	/* (each frame: a party formed in a game - its leader's, whose members
	follow it in at once - is told before a member's player is added) */
	if (network_game_is_splitscreen_local() || network_wire_crossplay() ||
		!p2p_party_team_token(token, PARTY_NET_TOKEN_BYTES))
	{
		return;
	}
	if (party_net_client.told && !csmemcmp(party_net_client.token, token, PARTY_NET_TOKEN_BYTES))
	{
		return;
	}
	network_event("party net: the party's token told to the host");
	party_net_client_send(client);
}
