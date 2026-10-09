/*
PATH_STATE_VERIFY.H

(port, debug) HALO_AI_PATH_STATE_VERIFY=1: the AI path searches that start
without clearing their whole state (path.c: path_state_new) are checked
against a search from a fully cleared state, and the nearby firing position
tests that search only when a position is near enough to need it
(actor_firing_position.c) against the test that searches first, every time:
path_state_verify.c
*/

#ifndef __PATH_STATE_VERIFY_H
#define __PATH_STATE_VERIFY_H
#pragma once

/* ---------- constants */

/* what path_state_new leaves uncleared is filled with this byte while the
checks are on, so that a search reading any of it before writing it answers
differently from the search from a cleared state */
#define PATH_STATE_VERIFY_POISON 0xA5

/* what was checked */
enum
{
	/* a search from a partly cleared state (path.c) */
	_path_state_verify_search,
	/* actor_nearby_firing_positions' answer with its search made when first
	needed (actor_firing_position.c) */
	_path_state_verify_nearby_firing_positions,
	NUMBER_OF_PATH_STATE_VERIFY_KINDS
};

/* ---------- prototypes */

/* whether the checks are on (read once) */
int path_state_verify_enabled(void);
/* one check's outcome; a difference is logged (the first 20 of each kind),
and every 300 ticks a line counts the checks and differences so far */
void path_state_verify_result(int kind, int same, char const *what);

#endif
