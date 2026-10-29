/*
NETWORK_MESSAGES.H

header included in hcex build.
*/

#ifndef __NETWORK_MESSAGES_H
#define __NETWORK_MESSAGES_H
#pragma once

/* ---------- headers */

#include "cseries.h"

/* ---------- constants */

#define NETWORK_JOIN_GAME_TOKEN_SIZE 16

enum network_game_message_type
{
	_message_client_broadcast_game_search = 0,
	_message_client_ping,
	_message_server_game_advertise,
	_message_server_pong,
	_message_server_machine_accepted,
	_message_server_machine_rejected,
	_message_server_game_settings_update,
	_message_server_pregame_countdown,
	_message_server_begin_game,
	_message_server_graceful_game_exit_pregame,
	_message_server_pregame_keep_alive,
	_message_server_postgame_keep_alive,
	_message_client_join_game_request,
	_message_client_add_player_request_pregame,
	_message_client_remove_player_request_pregame,
	_message_client_settings_request,
	_message_client_player_settings_request,
	_message_client_game_start_request,
	_message_client_graceful_game_exit_pregame,
	_message_client_map_is_precached_pregame,
	_message_server_game_update,
	_message_server_add_player_ingame,
	_message_server_remove_player_ingame,
	_message_server_game_over,
	_message_client_loaded,
	_message_client_game_update,
	_message_client_add_player_request_ingame,
	_message_client_remove_player_request_ingame,
	_message_client_host_crashed_cry_for_help,
	_message_client_join_new_host,
	_message_server_switch_to_pregame,
	_message_server_graceful_game_exit_postgame,
	_message_client_remove_player_request_postgame,
	_message_client_switch_to_pregame,
	_message_client_graceful_game_exit_postgame,
	/* port: map sharing (port/linux/game/map_share.c), in the lobby: a
	joiner's query, start, acknowledgement or cancel; the host's offer,
	refusal or end; the map's bytes. A host of a build without them ignores
	the joiner's (an unknown packet type), and is then told apart by its
	silence. */
	_message_client_map_download,
	_message_server_map_download_answer,
	_message_server_map_download_data,
	/* port: network Forge (port/linux/game/forge_net_protocol.h), an
	optional capability: the host's hello to a machine joining a Forge game
	(that it is Forge, its packs, its map variant), and the machine's answer
	(that it plays it). A machine of a build without them drops the host's
	as an unknown packet type and so never answers: the host then refuses it
	from the Forge game alone. Never sent on the crossplay wire */
	_message_client_forge,
	_message_server_forge,
	/* port: the game's rules a joiner plays (port/linux/game/infection_net.h),
	an optional capability as network Forge's: the host of an Infection game
	asks each machine that joins, and refuses one that does not answer (an
	older build drops the host's as an unknown packet type) from that game
	alone. Never sent on the crossplay wire */
	_message_client_game_rules,
	_message_server_game_rules,
	/* port: the tester's flaming helmet's proofs (port/linux/game/flame_net.h,
	port/linux/src/p2p_flame.inc), both ways, in any state of any network
	game: a joiner's to the host, the host's (its own or relayed) to a
	joiner. A build without them drops them as unknown packet types. Never
	sent on the crossplay wire */
	_message_client_flame,
	_message_server_flame,
	/* port: a joiner's party's team token (port/linux/game/party_net.h),
	before its request to add its player: a host that fills its game with
	bots puts a party's members on one team. A build without it drops it as
	an unknown packet type. Never sent on the crossplay wire */
	_message_client_party,
	NUMBER_OF_NETWORK_GAME_MESSAGE_TYPES,
};

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes */

/* ---------- globals */

/* ---------- public code */

void initialize_network_game_packets(
	void);

void network_event(
	char *format,
	...);

void *create_network_game_message(
	enum network_game_message_type message_type,
	const void *message_struct,
	short message_struct_size);

boolean decode_network_game_message(
	void *message_struct,
	const void *encoded_message,
	short *encoded_message_size,
	short *packet_type,
	short *packet_version,
	long expected_packet_class);

#endif // __NETWORK_MESSAGES_H
