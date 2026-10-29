/* bots.h: computer players for offline multiplayer (port/linux/game/bots.c)

A bot is a player of the local (split screen) game whose controls come from
code instead of a controller: it is in the lobby, the scores and the game's
rules as any player is, spawns and respawns, and scores. Each bot is the one
player of a machine of its own at the top of the machine indices
(BOTS_FIRST_MACHINE up), which no real machine takes: a split screen game has
only its own machine, 0. The bots have no view and no controller: nothing is
drawn for them. */
#ifndef HALO_BOTS_H
#define HALO_BOTS_H

struct player_action;
struct network_player;
struct game_variant_options;

/* the most bots in one game (with one player, the Xbox's 16) */
#define BOTS_MAXIMUM 15
/* the first bot's machine index (the last machine indices of the session) */
#define BOTS_FIRST_MACHINE (HALO_PORT_MAXIMUM_NETWORK_MACHINES - BOTS_MAXIMUM)

enum
{
	/* bots.teams: in a team game, the bots on both teams (evening them
	with the players), or all on the team the players are not on */
	_bots_teams_even = 0,
	_bots_teams_against,
};

/* the settings (bots.count, bots.skill, bots.teams: the Vita's settings
panel changes them at any time, so they are read each time) */
long bots_wanted_count(void);
short bots_skill(void);
short bots_teams(void);
/* (bots.online, HALO_BOTS_ONLINE: "full", the default, "off" or a count)
the most bots a game this machine hosts online fills its lobby with
(network_server_manager.c): 0 unless the connection is Online */
long bots_online_fill(void);

/* a machine (or a player of one) is a bot */
int bots_machine_is_bot(long machine_index);
int bots_player_is_bot(long player_index);
/* the network player for bot number `bot_index` (0 up): its machine,
controller, name and no team or colour yet */
void bots_network_player(long bot_index, struct network_player *player);

/* the game tick (players_update_before_game): every bot's controls this
tick, into the actions of the players by their index */
void bots_update_actions(struct player_action *actions);

/* a map's start and end (players.c) */
void bots_initialize_for_new_map(void);
void bots_dispose_from_old_map(void);

/* the bots' playlist (port/linux/game/bots_playlist.c, bots.playlist): a
local game with bots goes on to its next map and gametype by itself after
the scores. Every frame (main.c), with whether the main menu's level is
loaded and the frame's seconds; */
void bots_playlist_update(int main_menu_loaded, float seconds);
/* the server leaving its scores for the lobby (network_server_manager.c):
the next game into the stage the lobby is set up from */
void bots_playlist_before_pregame(void);
/* the lobby's screen after the scores (ui_widget.c): nonzero when the
playlist put up its own (the lobby, the countdown on), in place of the
map choice */
int bots_playlist_pregame_ui(void);
/* the game on the level `map_name` the lobby starts: nonzero when it is one
the playlist set up, with its options (network_game_manager.c: the time
limit) */
int bots_playlist_options(char const *map_name, struct game_variant_options *options);

#endif
