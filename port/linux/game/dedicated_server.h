/*
DEDICATED_SERVER.H

The dedicated server (dedicated_server.c, port/linux/DEDICATED_SERVER.md):
a headless host with no local player, built only into `ninja linux-server`
(HALO_DEDICATED_SERVER). What the game's own code asks it.
*/

#ifndef __DEDICATED_SERVER_H
#define __DEDICATED_SERVER_H

#ifdef HALO_DEDICATED_SERVER

/* the server's work each frame (main.c, in network_test_update's place) */
void dedicated_server_update(boolean main_menu_loaded, real seconds);
/* (network_server_manager.c) the lobby may count down to its game: the
server has set its map and gametype */
boolean dedicated_server_lobby_ready(void);
/* ... the lobby's countdown (milliseconds; sv_start_delay) */
long dedicated_server_countdown_milliseconds(void);
/* ... the players a game needs to start (sv_minplayers) */
long dedicated_server_minimum_players(void);
/* ... connections an address may make (joins), a minute, and whether this
one may (counted) */
boolean dedicated_server_join_allowed(unsigned long address);
/* (map_share.c) whether the operator lets joiners download this map
(sv_map_download) */
boolean dedicated_server_map_shareable(char const *level_name);
/* (main.c) frames a second: fewer while no one is connected */
long dedicated_server_frame_cap(void);

/* (network_server_manager.c, for the server) the remote machines joined to
the game, and the players of the remote machines */
long network_game_server_dedicated_machine_count(void);
/* drops the machine of the player at that index of the game's player list
(and bans it: its line in bans.txt); FALSE (said) if there is none */
boolean network_game_server_dedicated_drop_player(long player_index, boolean ban);
/* (network_distributed.c) a line in every machine's console, in game */
boolean network_distributed_say(char const *text);

#endif

#endif
