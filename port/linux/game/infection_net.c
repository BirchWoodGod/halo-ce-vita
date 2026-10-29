/*
INFECTION_NET.C

The lobby's check of the game's rules (infection_net.h): a host of an
Infection game asks each machine that joins it whether it plays Infection,
and refuses one that does not say so in time (an older build, which would
play it as Team Slayer without the zombies' sides) from that game alone.
The host's own machine, a local game's bots and a split screen game are
never asked; a game of another type asks no one, and a machine asked once
is not asked again while it stays.

A dedicated server refuses no one (port/linux/DEDICATED_SERVER.md: the
official servers' Infection is played by every build): a machine that does
not answer, or answers without Infection, plays its Infection as the Team
Slayer it knows - the host decides every side, death and spawn, and its
units come with their side's team and colours; the game type's state it is
sent is Slayer's part alone, the only one it reads
(game_rules_net_machine_plays_infection, network_distributed.c). A machine
hosting from the menus refuses it as before.
*/

#include "cseries.h"
#include "game/game_engine.h"
#include "networking/network_messages.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_game_protocol.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "networking/network_server_manager_internal.h"

#include "infection_net.h"
#include "network_wire.h"

#include <stdlib.h>

/* the platform's and the game's */
void platform_log(char const *format, ...);
boolean network_game_client_send_to_server(struct network_game_client *client, void *message);

/* (network_messages.c's packet table: the classes of the lobby's) */
#define GAME_RULES_NET_CLASS_CLIENT_PREGAME 3
#define GAME_RULES_NET_CLASS_SERVER_PREGAME 2

static struct
{
	boolean hosting;
	uint32_t session;
	uint32_t rules;
	struct
	{
		boolean known;
		boolean answered;
		boolean refused;
		/* (the dedicated server) taken without Infection, as Team Slayer */
		boolean legacy;
		long machine_index;
		unsigned long joined_time;
		unsigned long hello_time;
	} machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
} game_rules_net;

/* the rules this build plays (HALO_GAME_RULES_NET=0: none, as an older
build, which never answers: the tests') */
static boolean game_rules_net_capable(void)
{
	char const *setting = getenv("HALO_GAME_RULES_NET");

	return !(setting && setting[0] == '0');
}

/* the rules the hosted game needs (0: none, nothing asked) */
static uint32_t game_rules_net_needed(struct network_game_server *server)
{
	struct network_game *game;

	if (!server || network_wire_crossplay() || network_game_is_splitscreen_local())
		return 0;
	game = network_game_server_get_game(server);
	if (!game)
		return 0;
	return infection_variant(&game->variant) ? GAME_RULES_INFECTION : 0;
}

static boolean game_rules_net_send(long machine_index, struct game_rules_net_message const *message)
{
	void *encoded = create_network_game_message(_message_server_game_rules, message, sizeof(*message));

	return encoded && network_game_server_port_send_to_machine(machine_index, encoded);
}

static void game_rules_net_forget(void)
{
	csmemset(game_rules_net.machines, 0, sizeof(game_rules_net.machines));
}

void game_rules_net_frame(
	void)
{
	struct network_game_server *server = global_network_game_server_get();
	uint32_t needed = game_rules_net_needed(server);
	long machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	unsigned long now;
	short count, index;

	if (!needed)
	{
		/* (the game gone, or another type: a new hosting asks again) */
		if (game_rules_net.hosting && !server)
		{
			game_rules_net.hosting = FALSE;
			game_rules_net_forget();
		}
		return;
	}
	now = system_milliseconds();
	if (!game_rules_net.hosting)
	{
		game_rules_net.hosting = TRUE;
		game_rules_net.session = (uint32_t)now ^ ((uint32_t)rand() << 16) ^ 0x494E4643u;
		game_rules_net_forget();
	}
	if (game_rules_net.rules != needed)
	{
		game_rules_net.rules = needed;
		platform_log("game rules net: the game hosted is Infection: each machine joining is asked whether it plays it");
	}
	if (network_game_client_get_local_machine_index() == NONE)
		return;
	count = network_game_server_port_joined_machines(machines, (short)NUMBEROF(machines));
	/* (machines gone: forgotten, their slots another's later) */
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; index++)
	{
		short at;
		boolean still = FALSE;

		if (!game_rules_net.machines[index].known)
			continue;
		for (at = 0; at < count; at++)
			still |= machines[at] == game_rules_net.machines[index].machine_index;
		if (!still)
			csmemset(&game_rules_net.machines[index], 0, sizeof(game_rules_net.machines[index]));
	}
	for (index = 0; index < count; index++)
	{
		long machine_index = machines[index];

		if (machine_index == network_game_client_get_local_machine_index() ||
			!VALID_INDEX(machine_index, HALO_PORT_MAXIMUM_NETWORK_MACHINES))
		{
			continue;
		}
		if (!game_rules_net.machines[machine_index].known)
		{
			csmemset(&game_rules_net.machines[machine_index], 0, sizeof(game_rules_net.machines[machine_index]));
			game_rules_net.machines[machine_index].known = TRUE;
			game_rules_net.machines[machine_index].machine_index = machine_index;
			game_rules_net.machines[machine_index].joined_time = now;
		}
		if (game_rules_net.machines[machine_index].answered || game_rules_net.machines[machine_index].refused ||
			game_rules_net.machines[machine_index].legacy)
		{
			continue;
		}
		if (now - game_rules_net.machines[machine_index].joined_time >= GAME_RULES_HELLO_MILLISECONDS)
		{
#ifdef HALO_DEDICATED_SERVER
			platform_log("game rules net: machine %ld did not answer in %d ms (a build without Infection): it plays "
				"the Infection game as Team Slayer", machine_index, GAME_RULES_HELLO_MILLISECONDS);
			game_rules_net.machines[machine_index].legacy = TRUE;
			continue;
#endif
			platform_log("game rules net: machine %ld did not answer in %d ms (a build without Infection): "
				"refused from the Infection game", machine_index, GAME_RULES_HELLO_MILLISECONDS);
			game_rules_net.machines[machine_index].refused = TRUE;
			network_game_server_port_refuse_machine(machine_index, _rejection_code_version_too_old);
			continue;
		}
		if (!game_rules_net.machines[machine_index].hello_time ||
			now - game_rules_net.machines[machine_index].hello_time >= GAME_RULES_HELLO_RESEND_MILLISECONDS)
		{
			struct game_rules_net_message hello;

			csmemset(&hello, 0, sizeof(hello));
			hello.command = _game_rules_net_hello;
			hello.version = GAME_RULES_NET_VERSION;
			hello.rules = needed;
			hello.session = game_rules_net.session;
			if (game_rules_net_send(machine_index, &hello))
				game_rules_net.machines[machine_index].hello_time = now;
		}
	}
}

/* a message decoded and checked: its command and version, no rule this
build does not know, no reserved bits */
static boolean game_rules_net_decode(unsigned short *message, short message_size, short expected_type,
	long packet_class, struct game_rules_net_message *decoded)
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
	return decoded->command > 0 && decoded->command < NUMBER_OF_GAME_RULES_NET_COMMANDS &&
		decoded->version == GAME_RULES_NET_VERSION && decoded->reserved == 0;
}

void game_rules_net_client_handle_message(
	struct network_game_client *client,
	unsigned short *message,
	short message_size)
{
	struct game_rules_net_message hello, answer;
	void *encoded;

	if (!game_rules_net_decode(message, message_size, _message_server_game_rules, GAME_RULES_NET_CLASS_SERVER_PREGAME,
		&hello) || hello.command != _game_rules_net_hello)
	{
		network_event("game rules net: a host's message that could not be decoded or checked");
		return;
	}
	/* (a build that does not play them stays silent, as an older one; the
	host's own machine is never asked) */
	if (!game_rules_net_capable())
		return;
	csmemset(&answer, 0, sizeof(answer));
	answer.command = _game_rules_net_answer;
	answer.version = GAME_RULES_NET_VERSION;
	answer.rules = GAME_RULES_KNOWN;
	answer.session = hello.session;
	platform_log("game rules net: the host's game needs rules %x: this build plays %x", (unsigned)hello.rules,
		(unsigned)GAME_RULES_KNOWN);
	encoded = create_network_game_message(_message_client_game_rules, &answer, sizeof(answer));
	if (!encoded || !network_game_client_send_to_server(client, encoded))
		network_event("game rules net: the answer could not be sent");
}

void game_rules_net_server_handle_message(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	unsigned short *message,
	short message_size)
{
	struct game_rules_net_message answer;
	long machine_index = NONE;

	if (!game_rules_net_decode(message, message_size, _message_client_game_rules, GAME_RULES_NET_CLASS_CLIENT_PREGAME,
		&answer) || answer.command != _game_rules_net_answer)
	{
		network_event("game rules net: an answer that could not be decoded or checked");
		return;
	}
	network_game_server_get_client_machine(server, machine, &machine_index);
	if (!game_rules_net.hosting || !VALID_INDEX(machine_index, HALO_PORT_MAXIMUM_NETWORK_MACHINES) ||
		!game_rules_net.machines[machine_index].known || game_rules_net.machines[machine_index].refused ||
		game_rules_net.machines[machine_index].answered)
	{
		return;
	}
	if (answer.session != game_rules_net.session)
	{
		network_event("game rules net: an answer to another hosting");
		return;
	}
	if ((answer.rules & game_rules_net.rules) != game_rules_net.rules)
	{
#ifdef HALO_DEDICATED_SERVER
		platform_log("game rules net: machine %ld plays rules %x, not the game's %x: it plays the Infection game as "
			"Team Slayer", machine_index, (unsigned)answer.rules, (unsigned)game_rules_net.rules);
		game_rules_net.machines[machine_index].legacy = TRUE;
		return;
#endif
		platform_log("game rules net: machine %ld plays rules %x, not the game's %x: refused from it", machine_index,
			(unsigned)answer.rules, (unsigned)game_rules_net.rules);
		game_rules_net.machines[machine_index].refused = TRUE;
		network_game_server_port_refuse_machine(machine_index, _rejection_code_version_too_old);
		return;
	}
	game_rules_net.machines[machine_index].answered = TRUE;
	platform_log("game rules net: machine %ld plays Infection", machine_index);
}

int game_rules_net_machine_plays_infection(
	long machine_index)
{
	return game_rules_net.hosting && VALID_INDEX(machine_index, HALO_PORT_MAXIMUM_NETWORK_MACHINES) &&
		game_rules_net.machines[machine_index].known && game_rules_net.machines[machine_index].answered;
}
