/*
LINE_OF_SIGHT_VERIFY.H

(port, debug) HALO_AI_LINE_OF_SIGHT_VERIFY=1: the shortcuts the AI's lines
of sight take in the collision tests they share with the rest of the game are
checked against the original way, every time: line_of_sight_verify.c
*/

#ifndef __LINE_OF_SIGHT_VERIFY_H
#define __LINE_OF_SIGHT_VERIFY_H
#pragma once

/* ---------- constants */

/* what was checked */
enum
{
	/* a cluster's objects and their types from a table (cluster_object_types.c) */
	_line_of_sight_verify_cluster_object_types,
	NUMBER_OF_LINE_OF_SIGHT_VERIFY_KINDS
};

/* ---------- prototypes */

/* whether the checks are on (read once) */
int line_of_sight_verify_enabled(void);
/* one check's outcome; a difference is logged (the first 20 of each kind),
and every 300 ticks a line counts the checks and differences so far */
void line_of_sight_verify_result(int kind, int same, char const *what);

#endif
