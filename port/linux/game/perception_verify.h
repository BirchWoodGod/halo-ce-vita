/*
PERCEPTION_VERIFY.H

(port, debug) HALO_AI_PERCEPTION_VERIFY=1: the work the AI's perception
does another way (actor_perception.c) is checked against the original way, every
time: perception_verify.c
*/

#ifndef __PERCEPTION_VERIFY_H
#define __PERCEPTION_VERIFY_H
#pragma once

/* ---------- constants */

/* what was checked */
enum
{
	/* a visible cluster's list walked ahead by the timeslice refresh, against the list walked again */
	_perception_verify_refresh_walk,
	/* a walk that went on from a reference after a list changed (counted; a change is not expected) */
	_perception_verify_refresh_resumed,
	NUMBER_OF_PERCEPTION_VERIFY_KINDS
};

/* ---------- globals */

extern int perception_verify_setting;

/* ---------- prototypes */

/* whether the checks are on (read once) */
int perception_verify_read_setting(void);
#define perception_verify_enabled() \
	(perception_verify_setting >= 0 ? perception_verify_setting : perception_verify_read_setting())
/* one check's outcome; a difference is logged (the first 20 of each kind),
and every 300 ticks a line counts the checks and differences so far */
void perception_verify_result(int kind, int same, char const *what);

/* actor_perception_refresh's cluster walks: a list's walk begins, an object
it tests, the walk went on after a change, the walk ends (compared then) */
void perception_verify_refresh_cluster_begin_checked(short cluster_index, int collideable);
void perception_verify_refresh_object_checked(long object_index);
void perception_verify_refresh_cluster_end_checked(void);
#define perception_verify_refresh_cluster_begin(cluster_index, collideable) \
	do { if (perception_verify_enabled()) perception_verify_refresh_cluster_begin_checked((cluster_index), (collideable)); } while (0)
#define perception_verify_refresh_object(object_index) \
	do { if (perception_verify_enabled()) perception_verify_refresh_object_checked(object_index); } while (0)
#define perception_verify_refresh_resumed() \
	do { if (perception_verify_enabled()) perception_verify_result(_perception_verify_refresh_resumed, 1, NULL); } while (0)
#define perception_verify_refresh_cluster_end() \
	do { if (perception_verify_enabled()) perception_verify_refresh_cluster_end_checked(); } while (0)

#endif
