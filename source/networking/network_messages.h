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
	/* port: game chat (port/linux/game/chat.c), in the lobby and in game: a
	player's quick chat phrase or typed line to the host, and the host's
	relay of it, named from the connection it came over (network version
	19) */
	_message_client_chat,
	_message_server_chat,
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
