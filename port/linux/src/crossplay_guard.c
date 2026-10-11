/*
CROSSPLAY_GUARD.C

The client's guardrails on the crossplay wire (crossplay_guard.h,
docs/crossplay.md 6, T11).
*/

#include "crossplay_guard.h"
#include "lang.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

enum
{
	/* the letters and digits of a name kept (a game's name is 15
	characters and its end on every side) */
	ANSWER_KEY_SIZE = 16,
	ANSWERS = 4,
};

struct answer
{
	char key[ANSWER_KEY_SIZE];
	int yes;
	unsigned long time;
	int used;
};

/* (the browser answers on its thread, the game's client on its own: the
Vita's settings panel runs on its input thread) */
static pthread_mutex_t answers_lock = PTHREAD_MUTEX_INITIALIZER;
static struct answer answers[ANSWERS];

int crossplay_guard_large(int players, int maximum)
{
	return players > CROSSPLAY_GUARD_LARGE_PLAYERS || maximum > CROSSPLAY_GUARD_LARGE_PLAYERS;
}

int crossplay_guard_judge(int coop, int players, int maximum)
{
	if (coop)
		return CROSSPLAY_GUARD_COOP;
	if (crossplay_guard_large(players, maximum))
		return CROSSPLAY_GUARD_LARGE;
	return CROSSPLAY_GUARD_OK;
}

/* a name's key: its ASCII letters (lower case) and digits, at most 15; 0 if
it has none */
static int answer_key(const char *name, char key[ANSWER_KEY_SIZE])
{
	int length = 0;

	for (; name && *name && length < ANSWER_KEY_SIZE - 1; name++)
	{
		unsigned char c = (unsigned char)*name;

		if (c >= 'A' && c <= 'Z')
			key[length++] = (char)(c - 'A' + 'a');
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			key[length++] = (char)c;
	}
	key[length] = 0;
	return length > 0;
}

void crossplay_guard_answer(const char *name, int yes, unsigned long now)
{
	char key[ANSWER_KEY_SIZE];
	int index, slot = -1;

	if (!answer_key(name, key))
		return;
	pthread_mutex_lock(&answers_lock);
	/* (its own slot, else a free one, else the oldest) */
	for (index = 0; slot < 0 && index < ANSWERS; index++)
	{
		if (answers[index].used && !strcmp(answers[index].key, key))
			slot = index;
	}
	for (index = 0; slot < 0 && index < ANSWERS; index++)
	{
		if (!answers[index].used)
			slot = index;
	}
	if (slot < 0)
	{
		slot = 0;
		for (index = 1; index < ANSWERS; index++)
		{
			if (now - answers[index].time > now - answers[slot].time)
				slot = index;
		}
	}
	memcpy(answers[slot].key, key, sizeof(key));
	answers[slot].yes = yes != 0;
	answers[slot].time = now;
	answers[slot].used = 1;
	pthread_mutex_unlock(&answers_lock);
}

int crossplay_guard_answered(const char *name, unsigned long now)
{
	char key[ANSWER_KEY_SIZE];
	int index, result = -1;

	if (!answer_key(name, key))
		return -1;
	pthread_mutex_lock(&answers_lock);
	for (index = 0; index < ANSWERS; index++)
	{
		if (!answers[index].used || strcmp(answers[index].key, key))
			continue;
		if (now - answers[index].time < CROSSPLAY_GUARD_ANSWER_MILLISECONDS)
			result = answers[index].yes;
		else
			answers[index].used = 0;
		break;
	}
	pthread_mutex_unlock(&answers_lock);
	return result;
}

void crossplay_guard_forget(void)
{
	pthread_mutex_lock(&answers_lock);
	memset(answers, 0, sizeof(answers));
	pthread_mutex_unlock(&answers_lock);
}

int crossplay_guard_test_answer(void)
{
	const char *answer = getenv("HALO_CROSSPLAY_LARGE_ANSWER");

	if (!answer || !answer[0])
		return -1;
	return !strcmp(answer, "yes") || !strcmp(answer, "1");
}

const char *crossplay_guard_badge(int crossplay, int dedicated)
{
	if (crossplay)
		return dedicated ? T("PC SERVER") : T("PC");
	return dedicated ? T("SERVER") : T("VITA");
}

const char *crossplay_guard_large_title(void)
{
	return T("Halo: large PC game");
}

const char *crossplay_guard_large_question(void)
{
	return T("Large PC game: more than 16 players can make the Vita lag. Join anyway?");
}

const char *crossplay_guard_coop_refusal(void)
{
	return T("This is a PC co-op game. Co-op games on PC hosts can't be joined with Crossplay on: they run more AI "
		"than the Vita can hold.");
}
