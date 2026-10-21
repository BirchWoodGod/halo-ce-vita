/*
CARNAGE_REPORT.C

The Halo 3-style carnage report's facts (carnage_report.h): the game's own
record of the game just over, read for the screen (halo3/carnage.xml,
menu_functions.c "port h3 carnage ...") - the same ranking, places, scores
and statistics the game's post-game report has (game_engine.c
game_engine_post_rasterize_post_game draws kills, assists and deaths), and
the statistics it keeps without showing them: suicides, betrayals (a
teammate killed: game_statistics.c), the game type's own. On a machine that
joined a game all of it is what the host sent (network_distributed.c sends
every player's statistics, game_engine_write_network_state the scores):
every machine shows the host's numbers, and nothing a client counts itself.
*/

#include "cseries.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "input/input.h"
#include "carnage_report.h"
#include "../src/lang.h"

#include <stdio.h>
#include <string.h>

void platform_log(char const *format, ...);
struct widget_instance *ui_widget_port_active_screen(short local_player_index);
/* game_engine.c's (port) */
long game_engine_postgame_ranking(long *player_indices, long *places, boolean *tied, long maximum);
boolean game_engine_postgame_team_won(long team_index);

/* ---------- globals */

static struct
{
	volatile boolean shown;
	/* the scores' buttons held back until the press that closed a screen
	is let go */
	boolean release_pending;
	/* what was last said in halo.log (a hash of the rows) */
	unsigned long said_hash;
} carnage_report_globals;

/* ---------- private code */

/* the game's player colours of a team's nameplate: red, blue, then green,
yellow, purple, orange, brown, pink (the eighteen's order:
tools/halo3_menu_art.py PLAYER_COLOURS) */
static short carnage_team_color(long team)
{
	static short const colors[CARNAGE_REPORT_MAXIMUM_TEAMS] = { 2, 3, 6, 5, 8, 11, 14, 7 };

	return team >= 0 && team < CARNAGE_REPORT_MAXIMUM_TEAMS ? colors[team] : NONE;
}

static char const *carnage_team_name(long team)
{
	static char const *const names[CARNAGE_REPORT_MAXIMUM_TEAMS] = { N_("RED TEAM"), N_("BLUE TEAM"),
		N_("GREEN TEAM"), N_("YELLOW TEAM"), N_("PURPLE TEAM"), N_("ORANGE TEAM"), N_("BROWN TEAM"), N_("PINK TEAM") };

	/* (Infection's two teams by their sides, as the classic postgame names
	them: red the zombies, blue the humans, infection.c) */
	if (infection_active() && (team == 0 || team == 1))
		return team == 0 ? T("ZOMBIES") : T("HUMANS");
	return team >= 0 && team < CARNAGE_REPORT_MAXIMUM_TEAMS ? T(names[team]) : "";
}

/* a score as the game type formats it, its padding left out (" :14", a
time under a minute: "0:14") */
static void carnage_score_text(char *text)
{
	char const *start = text;

	while (*start == ' ')
		start++;
	if (*start == ':')
	{
		memmove(text + 1, start, strlen(start) + 1);
		text[0] = '0';
	}
	else if (start != text)
		memmove(text, start, strlen(start) + 1);
}

/* wide text (a player's name, a score) as UTF-8 */
static void carnage_utf8(char *text, int size, wchar_t const *wide, int length)
{
	int out = 0, index;

	for (index = 0; index < length && wide[index] && out < size - 4; index++)
	{
		unsigned int character = (unsigned short)wide[index];

		if (character < 0x80)
			text[out++] = (char)character;
		else if (character < 0x800)
		{
			text[out++] = (char)(0xC0 | (character >> 6));
			text[out++] = (char)(0x80 | (character & 0x3F));
		}
		else
		{
			text[out++] = (char)(0xE0 | (character >> 12));
			text[out++] = (char)(0x80 | ((character >> 6) & 0x3F));
			text[out++] = (char)(0x80 | (character & 0x3F));
		}
	}
	text[out] = 0;
}

static void carnage_player_row(struct carnage_report_row *row, long player_index, long place, boolean tied)
{
	struct player_datum *player = player_get(player_index);
	union multiplayer_statistics const *statistics = &player->statistics.multiplayer_statistics;
	wchar_t score[256];
	long type = game_engine ? game_engine->type : NONE;

	memset(row, 0, sizeof(*row));
	row->team = (short)player->team_index;
	row->color = player->network_player_data.primary_color_index;
	row->place = place;
	row->tied = tied;
	row->local = player->local_player_index != NONE;
	row->quit = player->quit_out_of_game;
	carnage_utf8(row->name, (int)sizeof(row->name), player->name, (int)NUMBEROF(player->name));
	score[0] = 0;
	if (game_engine->format_player_score)
	{
		game_engine->format_player_score(player_index, score);
		score[NUMBEROF(score) - 1] = 0;
	}
	carnage_utf8(row->score, (int)sizeof(row->score), score, (int)NUMBEROF(score));
	carnage_score_text(row->score);
	row->score_value = game_engine->get_player_score ? game_engine->get_player_score(player_index, _get_score_individual) : 0;
	row->kills = player->statistics.kills[0];
	row->deaths = player->statistics.deaths;
	row->assists = player->statistics.assists[0];
	row->suicides = player->statistics.suicides;
	row->betrayals = player->statistics.friendly_fire_kills;
	switch (type)
	{
	case game_engine_ctf:
		row->objective[0] = statistics->ctf_statistics.flag_grabs;
		row->objective[1] = statistics->ctf_statistics.flag_returns;
		break;
	case game_engine_oddball:
		row->objective[0] = statistics->oddball_statistics.kills_as_ball_carrier;
		row->objective[1] = statistics->oddball_statistics.ball_carrier_kills;
		break;
	case game_engine_race:
		row->objective[0] = statistics->race_statistics.laps ? statistics->race_statistics.best_lap_time : NONE;
		break;
	}
}

/* ---------- public code */

char const *carnage_report_objective_name(long type, int index)
{
	switch (type)
	{
	case game_engine_ctf:
		return index == 0 ? N_("GRABS") : index == 1 ? N_("RETURNS") : NULL;
	case game_engine_oddball:
		/* (kills with the ball; ball carriers killed) */
		return index == 0 ? N_("BALL KILLS") : index == 1 ? N_("CARRIERS") : NULL;
	case game_engine_race:
		return index == 0 ? N_("BEST LAP") : NULL;
	}
	return NULL;
}

void carnage_report_objective_text(struct carnage_report const *report, struct carnage_report_row const *row, int index,
	char *text, int size)
{
	long value = row->objective[index];

	if (report->type == game_engine_race && index == 0)
	{
		if (value == NONE || row->team_row)
			snprintf(text, size, "-");
		else
			snprintf(text, size, "%ld:%02ld.%ld", value / (TICKS_PER_SECOND * 60), (value / TICKS_PER_SECOND) % 60,
				(value % TICKS_PER_SECOND) * 10 / TICKS_PER_SECOND);
		return;
	}
	snprintf(text, size, "%ld", value);
}

boolean carnage_report_read(struct carnage_report *report)
{
	long player_indices[CARNAGE_REPORT_MAXIMUM_ROWS];
	long places[CARNAGE_REPORT_MAXIMUM_ROWS];
	boolean tied[CARNAGE_REPORT_MAXIMUM_ROWS];
	/* (off the stack: 128 players' rows; the menus' thread's alone) */
	static struct carnage_report_row players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long count, index;

	memset(report, 0, sizeof(*report));
	report->winning_team = NONE;
	report->winning_row = NONE;
	if (!game_engine || !player_data)
		return FALSE;
	report->type = game_engine->type;
	report->teams = game_engine_has_teams();
	count = game_engine_postgame_ranking(player_indices, places, tied, HALO_PORT_MAXIMUM_NETWORK_PLAYERS);
	for (index = 0; index < count; index++)
	{
		carnage_player_row(&players[index], player_indices[index], places[index], tied[index]);
		players[index].won = !tied[index] && places[index] == 0;
	}
	/* (players placed alike: by name, the same order on every machine) */
	for (index = 1; index < count; index++)
	{
		long other = index;

		while (other > 0 && players[other - 1].place == players[other].place &&
			strcmp(players[other - 1].name, players[other].name) > 0)
		{
			struct carnage_report_row swap = players[other - 1];

			players[other - 1] = players[other];
			players[other] = swap;
			other--;
		}
	}
	report->player_count = count;
	if (!report->teams)
	{
		for (index = 0; index < count; index++)
		{
			report->rows[index] = players[index];
			report->rows[index].team = NONE;
			if (players[index].won)
				report->winning_row = index;
		}
		report->row_count = count;
		return TRUE;
	}

	/* a team game: each team's row (its score, its players' sums), its
	players under it in their order; the winning team first, then by score */
	{
		struct carnage_report_row teams[CARNAGE_REPORT_MAXIMUM_TEAMS];
		short order[CARNAGE_REPORT_MAXIMUM_TEAMS];
		long team, row = 0;
		short team_count = 0;

		for (team = 0; team < CARNAGE_REPORT_MAXIMUM_TEAMS; team++)
		{
			struct carnage_report_row *entry = &teams[team];
			wchar_t score[256];
			boolean present = FALSE;

			memset(entry, 0, sizeof(*entry));
			for (index = 0; index < count; index++)
			{
				if (players[index].team != team)
					continue;
				present = TRUE;
				entry->kills += players[index].kills;
				entry->deaths += players[index].deaths;
				entry->assists += players[index].assists;
				entry->suicides += players[index].suicides;
				entry->betrayals += players[index].betrayals;
				entry->objective[0] += players[index].objective[0] > 0 ? players[index].objective[0] : 0;
				entry->objective[1] += players[index].objective[1] > 0 ? players[index].objective[1] : 0;
				entry->local |= players[index].local;
			}
			if (!present)
				continue;
			entry->team_row = TRUE;
			entry->team = (short)team;
			entry->color = carnage_team_color(team);
			entry->won = game_engine_postgame_team_won(team);
			snprintf(entry->name, sizeof(entry->name), "%s", carnage_team_name(team));
			score[0] = 0;
			if (game_engine->format_team_name)
			{
				game_engine->format_team_name(team, score);
				score[NUMBEROF(score) - 1] = 0;
			}
			carnage_utf8(entry->score, (int)sizeof(entry->score), score, (int)NUMBEROF(score));
			carnage_score_text(entry->score);
			entry->score_value = game_engine_get_team_score(team);
			order[team_count++] = (short)team;
		}
		/* (the winner, then the higher score, then the lower index) */
		for (index = 1; index < team_count; index++)
		{
			long other = index;

			while (other > 0)
			{
				struct carnage_report_row const *a = &teams[order[other - 1]], *b = &teams[order[other]];
				short swap;

				if (a->won > b->won || (a->won == b->won && a->score_value >= b->score_value))
					break;
				swap = order[other - 1];
				order[other - 1] = order[other];
				order[other] = swap;
				other--;
			}
		}
		/* (a place shared by teams of one score, no team the winner) */
		for (index = 0; index < team_count; index++)
			if (teams[order[index]].won && report->winning_team == NONE)
				report->winning_team = (short)order[index];
		for (index = 0; index < team_count; index++)
		{
			struct carnage_report_row *entry = &teams[order[index]];

			entry->place = index;
			if (index > 0 && teams[order[index - 1]].score_value == entry->score_value && !entry->won &&
				!teams[order[index - 1]].won)
			{
				entry->place = teams[order[index - 1]].place;
				entry->tied = teams[order[index - 1]].tied = TRUE;
			}
		}
		for (index = 0; index < team_count && row < CARNAGE_REPORT_MAXIMUM_ROWS; index++)
		{
			long player;

			team = order[index];
			report->rows[row++] = teams[team];
			for (player = 0; player < count && row < CARNAGE_REPORT_MAXIMUM_ROWS; player++)
			{
				if (players[player].team != team)
					continue;
				report->rows[row] = players[player];
				report->rows[row].color = carnage_team_color(team);
				/* (a team game's winners are the team's) */
				report->rows[row].won = teams[team].won;
				row++;
			}
		}
		report->team_count = team_count;
		report->row_count = row;
	}
	return TRUE;
}

void carnage_report_log(struct carnage_report const *report, char const *why)
{
	unsigned long hash = 5381;
	long index;
	char line[320];

	/* (said once, and again only when a number changes) */
	for (index = 0; index < report->row_count; index++)
	{
		unsigned char const *bytes = (unsigned char const *)&report->rows[index];
		size_t byte;

		for (byte = 0; byte < sizeof(report->rows[index]); byte++)
			hash = hash * 33 + bytes[byte];
	}
	hash = hash * 33 + (unsigned long)report->row_count;
	if (hash == carnage_report_globals.said_hash)
		return;
	carnage_report_globals.said_hash = hash;
	platform_log("carnage: %s: type %ld, %s, %ld players, %ld rows, winner %s", why, report->type,
		report->teams ? "teams" : "free for all", report->player_count, report->row_count,
		report->winning_team != NONE ? carnage_team_name(report->winning_team) :
		report->winning_row != NONE ? report->rows[report->winning_row].name : "none (a tie)");
	for (index = 0; index < report->row_count; index++)
	{
		struct carnage_report_row const *row = &report->rows[index];

		snprintf(line, sizeof(line), "carnage: row %ld: %s%s place %ld%s%s team %d score %s kills %ld deaths %ld "
			"assists %ld suicides %ld betrayals %ld objective %ld %ld%s%s", index + 1, row->team_row ? "team " : "",
			row->name, row->place + 1, row->tied ? " (tied)" : "", row->won ? " won" : "", row->team, row->score,
			row->kills, row->deaths, row->assists, row->suicides, row->betrayals, row->objective[0], row->objective[1],
			row->local ? " [this machine]" : "", row->quit ? " [quit]" : "");
		platform_log("%s", line);
	}
}

void carnage_report_set_shown(boolean shown)
{
	carnage_report_globals.shown = shown;
	/* (the next game's report said whole) */
	if (!shown)
		carnage_report_globals.said_hash = 0;
}

boolean carnage_report_replaces_postgame(void)
{
	return carnage_report_globals.shown;
}

boolean carnage_report_holds_buttons(void)
{
	short index;

	for (index = 0; index < 4; index++)
	{
		if (ui_widget_port_active_screen(index))
		{
			carnage_report_globals.release_pending = TRUE;
			return TRUE;
		}
	}
	if (carnage_report_globals.release_pending)
	{
		/* (the scores' buttons: A, B, START, BACK - game_engine.c's
		test_any_gamepad_button's) */
		static short const buttons[] = { 0, 1, 12, 13 };
		short pad;

		for (pad = 0; pad < 4; pad++)
		{
			struct gamepad_state const *state = input_get_gamepad_state(pad);
			size_t button;

			if (!state)
				continue;
			for (button = 0; button < NUMBEROF(buttons); button++)
				if (state->buttons[buttons[button]])
					return TRUE;
		}
		carnage_report_globals.release_pending = FALSE;
	}
	return FALSE;
}
