/*
PATH_STATE_VERIFY.H

(port, debug) HALO_AI_PATH_STATE_VERIFY=1: the AI path searches that start
without clearing their whole state (path.c: path_state_new) are checked
against a search from a fully cleared state, every time: path_state_verify.c
*/

#ifndef __PATH_STATE_VERIFY_H
#define __PATH_STATE_VERIFY_H
#pragma once

/* ---------- constants */

/* what path_state_new leaves uncleared is filled with this byte while the
checks are on, so that a search reading any of it before writing it answers
differently from the search from a cleared state */
#define PATH_STATE_VERIFY_POISON 0xA5

/* ---------- prototypes */

/* whether the checks are on (read once) */
int path_state_verify_enabled(void);
/* one search's outcome; a difference is logged (the first 20), and every 300
ticks a line counts the checks and differences so far */
void path_state_verify_result(int same, char const *what);

#endif
