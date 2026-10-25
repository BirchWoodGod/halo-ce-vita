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
/* (game_engine.c) the game's one weapon, if it has one (SWAT's magnum,
Snipers' sniper rifle: _game_engine_sole_weapon_, game_engine.h): every
weapon made that one */
short dedicated_server_sole_weapon(void);
/* (main.c) frames a second: fewer while no one is connected */
long dedicated_server_frame_cap(void);
/* (network_server_manager.c) bots fill the game to its most players less
two, an even total (sv_bots_fill) */
boolean dedicated_server_bots_fill(void);
/* ... and while nobody is in (sv_bots_when_empty, 1 by default): the server
plays its cycle with bots alone */
boolean dedicated_server_bots_when_empty(void);

/* (network_server_manager.c, for the server) the remote machines joined to
the game, and the players of the remote machines */
long network_game_server_dedicated_machine_count(void);
/* drops the machine of the player at that index of the game's player list
(and bans it: its line in bans.txt); FALSE (said) if there is none */
boolean network_game_server_dedicated_drop_player(long player_index, boolean ban);
/* (network_distributed.c) a line in every machine's console, in game */
boolean network_distributed_say(char const *text);

#endif

/* (any host: the dedicated server, a 1.1.1 host online - network_server_manager.c's
bot fill) */
/* the bots in the game (sv_bots_fill; a host's Bots online too) */
long network_game_server_dedicated_bot_count(void);
/* (a machine joining, a player being added, the game full) a bot's slot
taken: TRUE if the game has a free slot now */
struct network_game_server;
struct network_player;
boolean network_game_server_port_bots_make_room(struct network_game_server *server);
/* ... a bot of that team's, when it has one (a person joining it) */
boolean network_game_server_port_bots_make_room_on_team(struct network_game_server *server, long team);
/* (any host's bot fill: a dedicated server's, a 1.1.1 host's Bots online)
the team of a person's player joining a team game it fills: its party's
(party_net.h), else the team with fewer people, then fewer players; NONE
when the fill keeps no teams (no fill, free for all, Infection) */
long network_game_server_port_person_team(struct network_game_server *server, long machine_index);
/* ... a person's player added (the team a tie between the teams takes a
bot from) */
void network_game_server_port_person_added(struct network_game_server *server, struct network_player const *player);

#endif
