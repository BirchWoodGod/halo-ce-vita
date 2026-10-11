/*
PATH_STATE_VERIFY.C

(port, debug) HALO_AI_PATH_STATE_VERIFY=1: path_state_new clears a path
search's header alone (path.c; the node list, heap and hash table are set up
by the search as it uses them), and fills the rest with
PATH_STATE_VERIFY_POISON; path_state_find then runs each such search a second
time from a copy of the state cleared the original way (every byte zero but
the header) and compares the two: the answer, the header, the nodes made, the
heap's live entries and the hash table. The game goes on with the first
answer. A difference is logged ("path state verify mismatch", the first 20)
and every 300 ticks "path-state-verify" counts the checks and differences so
far, so a run that checked nothing is told from one that found nothing. The
searches run on the tick's thread (and the offline bots' helper thread), so
the counts are kept with atomic adds.
*/

/* ---------- headers */

#include <stdio.h>
#include <stdlib.h>

#include "cseries.h"
#include "path_state_verify.h"

void platform_log(const char *format, ...);
long game_time_get(void);

/* ---------- globals */

static int verify_enabled = -1;
static unsigned long verify_checks[NUMBER_OF_PATH_STATE_VERIFY_KINDS];
static unsigned long verify_mismatches[NUMBER_OF_PATH_STATE_VERIFY_KINDS];
static long verify_reported_time;

static char const *const verify_kind_names[NUMBER_OF_PATH_STATE_VERIFY_KINDS] =
{
	"path searches",
	"nearby firing position answers",
};

/* ---------- public code */

int path_state_verify_enabled(
	void)
{
	if (verify_enabled < 0)
	{
		char const *setting = getenv("HALO_AI_PATH_STATE_VERIFY");

		verify_enabled = setting && atoi(setting) != 0;
	}

	return verify_enabled;
}

void path_state_verify_result(
	int kind,
	int same,
	char const *what)
{
	long now = game_time_get();

	__sync_fetch_and_add(&verify_checks[kind], 1);
	if (!same && __sync_fetch_and_add(&verify_mismatches[kind], 1) < 20)
	{
		platform_log("path state verify mismatch (%s): %s", verify_kind_names[kind], what ? what : "");
	}
	if (now - verify_reported_time >= 300 || now < verify_reported_time)
	{
		int index;
		char line[512];
		int length = 0;

		verify_reported_time = now;
		for (index = 0; index < NUMBER_OF_PATH_STATE_VERIFY_KINDS; index++)
		{
			length += snprintf(line + length, sizeof(line) - length, "%s%s %lu checked, %lu different",
				index ? ", " : "", verify_kind_names[index], verify_checks[index], verify_mismatches[index]);
		}
		platform_log("path-state-verify (tick %ld): %s", now, line);
	}
}
