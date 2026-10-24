/*
CARNAGE_REPORT.H

The Halo 3-style carnage report (Menu style Halo 3): what a multiplayer
game's end shows on every machine, from the game's own record of it -
the players ranked as the game's post-game report ranks them
(game_engine_postgame_ranking), their score as the game type formats it,
and the statistics the game keeps for each (kills, deaths, assists,
suicides, betrayals, the game type's own: flag grabs and returns, the
ball's kills, laps). On a machine that joined, all of it is the host's: the
distributed netcode sends every player's statistics and the game type's
scores as the host has them (network_distributed.c), which this reads as
they are. Nothing is counted here.

The screen itself is port/vita/app0/menus/halo3/carnage.xml, filled by
menu_functions.c ("port h3 carnage ...").
*/

#ifndef __CARNAGE_REPORT_H
#define __CARNAGE_REPORT_H

#include "halo_port_limits.h"

/* the most rows: every player, and a row for each team over its players */
#define CARNAGE_REPORT_MAXIMUM_TEAMS 8
#define CARNAGE_REPORT_MAXIMUM_ROWS (HALO_PORT_MAXIMUM_NETWORK_PLAYERS + CARNAGE_REPORT_MAXIMUM_TEAMS)
/* a row's objective statistics (the game type's: carnage_report.c) */
#define CARNAGE_REPORT_OBJECTIVE_STATISTICS 2

struct carnage_report_row
{
	/* a team's row (over its players: their sums), or a player's */
	boolean team_row;
	/* the team (NONE in a game without teams), the colour of the row's
	nameplate (the game's eighteen; the team's in a team game) */
	short team;
	short color;
	/* the place (0 first; a team's among the teams), shared or not */
	long place;
	boolean tied;
	/* this machine's player; a player who left the game; the winner */
	boolean local;
	boolean quit;
	boolean won;
	/* UTF-8 */
	char name[48];
	/* the score as the game type shows it ("12", "1:05"), and its value
	(the sort's) */
	char score[24];
	long score_value;
	long kills;
	long deaths;
	long assists;
	long suicides;
	long betrayals;
	/* the game type's own (carnage_report_objective_names) */
	long objective[CARNAGE_REPORT_OBJECTIVE_STATISTICS];
	/* (a player's row) the game's player (NONE on a team's row), their
	machine and local player (NONE: another machine's) */
	long player_index;
	short machine;
	short local_player_index;
};

struct carnage_report
{
	/* the game type (enum game_engine_type) and whether it had teams */
	long type;
	boolean teams;
	long player_count;
	long team_count;
	long row_count;
	/* the winning team (NONE: none, a tie), or the winning player's row */
	short winning_team;
	long winning_row;
	struct carnage_report_row rows[CARNAGE_REPORT_MAXIMUM_ROWS];
};

/* the report of the game shown now (its scores up: the game over), read
from the game's record; FALSE when there is no multiplayer game */
boolean carnage_report_read(struct carnage_report *report);
/* (the Halo 3 scoreboard, menu_functions.c "port h3 scoreboard") the game
being played, ranked as the game's own in-game score ranks it: the players
still in the game (not those who quit), the team rows over them */
boolean carnage_report_read_in_game(struct carnage_report *report);
/* the game type's objective statistics' names (English, for T()), NULL for
none */
char const *carnage_report_objective_name(long type, int index);
/* a row's objective statistic as text ("1:23.4" for a lap time) */
void carnage_report_objective_text(struct carnage_report const *report, struct carnage_report_row const *row, int index,
	char *text, int size);
/* the report in halo.log, a line a row (said again only when it changes:
the tests compare the machines' lines) */
void carnage_report_log(struct carnage_report const *report, char const *why);

/* (menu_functions.c) whether the Halo 3 report is up over this game's end,
in the game's own report's place (game_engine_post_rasterize_post_game
draws nothing then) */
void carnage_report_set_shown(boolean shown);
boolean carnage_report_replaces_postgame(void);
/* (game_engine.c) whether the scores' own buttons (A goes on, B leaves)
wait: a screen is up over them, or the press that closed one is still
held */
boolean carnage_report_holds_buttons(void);

#endif
