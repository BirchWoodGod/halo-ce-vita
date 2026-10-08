/*
CHAT.H

Game chat in network games, the lobby and the game itself (chat.c; its
rules: chat_protocol.h).
*/

#ifndef __CHAT_H
#define __CHAT_H
#pragma once

#include "cseries.h"

struct network_game_client;
struct network_game_server;
struct network_game_server_client_machine;

/* each frame, from the main loop (between ticks): the menu's requests
(chat_link.h), what the menu lists, the test script (HALO_TEST_CHAT) */
void chat_update(void);

/* the chat lines, over everything else in the window (interface.c's
fullscreen overlays: the lobby's and the game's) */
void chat_draw(void);

/* (network_server_message_handler.c) a joiner's _message_client_chat,
over its connection */
void chat_server_handle_request(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	word *message,
	short message_size);

/* (network_client_message_handler.c) the host's _message_server_chat */
void chat_client_handle_relay(
	struct network_game_client *client,
	word *message,
	short message_size);

#endif // __CHAT_H
