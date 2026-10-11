/*
LINE_OF_SIGHT_VERIFY.C

(port, debug) HALO_AI_LINE_OF_SIGHT_VERIFY=1: each shortcut the AI's lines of
sight take, in the collision tests they share with the rest of the game
(cluster_object_types.c), is followed by the original way, on the same
values, and the two compared; the game goes on with the shortcut's answer. A difference is logged ("line of
sight verify mismatch", the first 20 of each kind) and every 300 ticks
"line-of-sight-verify" counts the checks and differences so far, so a run
that checked nothing is told from one that found nothing. Tick thread only
(the shortcuts are taken there only).
*/

/* ---------- headers */

#include <stdio.h>
#include <stdlib.h>

#include "cseries.h"
#include "line_of_sight_verify.h"

void platform_log(const char *format, ...);
long game_time_get(void);

/* ---------- globals */

static int verify_enabled = -1;
static unsigned long verify_checks[NUMBER_OF_LINE_OF_SIGHT_VERIFY_KINDS];
static unsigned long verify_mismatches[NUMBER_OF_LINE_OF_SIGHT_VERIFY_KINDS];
static long verify_reported_time;

static char const *const verify_kind_names[NUMBER_OF_LINE_OF_SIGHT_VERIFY_KINDS] =
{
	"cluster object tables",
};

/* ---------- public code */

int line_of_sight_verify_enabled(
	void)
{
	if (verify_enabled < 0)
	{
		char const *setting = getenv("HALO_AI_LINE_OF_SIGHT_VERIFY");

		verify_enabled = setting && atoi(setting) != 0;
	}

	return verify_enabled;
}

void line_of_sight_verify_result(
	int kind,
	int same,
	char const *what)
{
	long now = game_time_get();

	verify_checks[kind]++;
	if (!same && verify_mismatches[kind]++ < 20)
	{
		platform_log("line of sight verify mismatch (%s): %s", verify_kind_names[kind], what ? what : "");
	}
	if (now - verify_reported_time >= 300 || now < verify_reported_time)
	{
		int index;
		char line[512];
		int length = 0;

		verify_reported_time = now;
		for (index = 0; index < NUMBER_OF_LINE_OF_SIGHT_VERIFY_KINDS; index++)
		{
			length += snprintf(line + length, sizeof(line) - length, "%s%s %lu checked, %lu different",
				index ? ", " : "", verify_kind_names[index], verify_checks[index], verify_mismatches[index]);
		}
		platform_log("line-of-sight-verify (tick %ld): %s", now, line);
	}
}
