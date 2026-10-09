/*
PERCEPTION_VERIFY.C

(port, debug) HALO_AI_PERCEPTION_VERIFY=1: each piece of work the AI's
perception does another way (actor_perception.c) is followed by the original
way, on the same values, and the two compared; the game goes on with the
new way's answer. A difference is logged ("perception verify mismatch", the
first 20 of each kind) and every 300 ticks "perception-verify" counts the
checks and differences so far, so a run that checked nothing is told from
one that found nothing. Tick thread only (the work is done there only).
*/

/* ---------- headers */

#include <stdio.h>
#include <stdlib.h>

#include "cseries.h"
#include "objects/objects.h"
#include "perception_verify.h"

void platform_log(const char *format, ...);
long game_time_get(void);

/* ---------- constants */

#define VERIFY_WALK_MAXIMUM 512

/* ---------- globals */

int perception_verify_setting = -1;
static unsigned long verify_checks[NUMBER_OF_PERCEPTION_VERIFY_KINDS];
static unsigned long verify_mismatches[NUMBER_OF_PERCEPTION_VERIFY_KINDS];
static long verify_reported_time;

static char const *const verify_kind_names[NUMBER_OF_PERCEPTION_VERIFY_KINDS] =
{
	"refresh walks",
	"refresh walks resumed after a change",
	"refresh object types",
	"status reachable weights",
};

/* the walk being checked */
static struct
{
	short cluster_index;
	int collideable;
	int resumed;
	long count;
	long object_indices[VERIFY_WALK_MAXIMUM];
} verify_walk;

/* ---------- public code */

int perception_verify_read_setting(
	void)
{
	char const *setting = getenv("HALO_AI_PERCEPTION_VERIFY");

	perception_verify_setting = setting && atoi(setting) != 0;
	return perception_verify_setting;
}

void perception_verify_result(
	int kind,
	int same,
	char const *what)
{
	long now = game_time_get();

	verify_checks[kind]++;
	if (kind == _perception_verify_refresh_resumed)
	{
		verify_walk.resumed = TRUE;
	}
	if (!same && verify_mismatches[kind]++ < 20)
	{
		platform_log("perception verify mismatch (%s): %s", verify_kind_names[kind], what ? what : "");
	}
	if (now - verify_reported_time >= 300 || now < verify_reported_time)
	{
		int index;
		char line[768];
		int length = 0;

		verify_reported_time = now;
		for (index = 0; index < NUMBER_OF_PERCEPTION_VERIFY_KINDS; index++)
		{
			length += snprintf(line + length, sizeof(line) - length, "%s%s %lu checked, %lu different",
				index ? ", " : "", verify_kind_names[index], verify_checks[index], verify_mismatches[index]);
		}
		platform_log("perception-verify (tick %ld): %s", now, line);
	}
}

void perception_verify_refresh_cluster_begin_checked(
	short cluster_index,
	int collideable)
{
	verify_walk.cluster_index = cluster_index;
	verify_walk.collideable = collideable;
	verify_walk.resumed = FALSE;
	verify_walk.count = 0;
}

void perception_verify_refresh_object_checked(
	long object_index)
{
	if (verify_walk.count < VERIFY_WALK_MAXIMUM)
	{
		verify_walk.object_indices[verify_walk.count] = object_index;
	}
	verify_walk.count++;
}

void perception_verify_refresh_cluster_end_checked(
	void)
{
	long reference_index;
	long object_index;
	long count = 0;
	int same = TRUE;
	char what[160];

	/* (a walk that went on after a change saw the list as it was then, before
	and after: nothing to compare it with now) */
	if (verify_walk.resumed)
	{
		return;
	}
	for (object_index = verify_walk.collideable ?
			cluster_get_first_collideable_object(&reference_index, verify_walk.cluster_index) :
			cluster_get_first_noncollideable_object(&reference_index, verify_walk.cluster_index);
		object_index != NONE;
		object_index = verify_walk.collideable ?
			cluster_get_next_collideable_object(&reference_index) :
			cluster_get_next_noncollideable_object(&reference_index))
	{
		if (count >= verify_walk.count ||
			(count < VERIFY_WALK_MAXIMUM && verify_walk.object_indices[count] != object_index))
		{
			same = FALSE;
		}
		count++;
	}
	if (count != verify_walk.count)
	{
		same = FALSE;
	}
	snprintf(what, sizeof(what), "cluster %d (%s): %ld objects tested, %ld in the list walked again",
		verify_walk.cluster_index, verify_walk.collideable ? "collideable" : "noncollideable", verify_walk.count, count);
	perception_verify_result(_perception_verify_refresh_walk, same, what);
}
