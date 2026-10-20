/*
CROSSPLAY_GUARD.C

The client's guardrails on the crossplay wire (crossplay_guard.h,
docs/crossplay.md 6, T11).
*/

#include "crossplay_guard.h"
#include "lang.h"

#include <pthread.h>
#include <stdio.h>
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

int crossplay_guard_experimental(int coop, int players, int maximum)
{
	return crossplay_guard_judge(coop, players, maximum) != CROSSPLAY_GUARD_OK;
}

int crossplay_guard_stock_multiplayer_map(const char *map, int size)
{
	static const char *const levels[] = { "beavercreek", "bloodgulch", "boardingaction", "carousel", "chillout",
		"damnation", "hangemhigh", "longest", "prisoner", "putput", "ratrace", "sidewinder", "wizard" };
	char name[32];
	int index, start = 0, length = 0, test_folder;

	if (!map || size <= 0)
		return 0;
	for (index = 0; index < size && map[index]; index++)
	{
		if (map[index] == '\\' || map[index] == '/')
			start = index + 1;
	}
	length = index - start;
	if (length <= 0 || length >= (int)sizeof(name))
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = map[start + index];

		name[index] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
	}
	name[length] = 0;
	/* (a path: the Xbox's own, levels\test\<name>\<name>; Halo PC's
	custom_maps\<name> is a Custom Edition map of that name) */
	test_folder = start == 0 || (start > 12 && !strncmp(map, "levels", 6) && (map[6] == '\\' || map[6] == '/') &&
		!strncmp(map + 7, "test", 4));
	if (!test_folder)
		return 0;
	for (index = 0; index < (int)(sizeof(levels) / sizeof(levels[0])); index++)
	{
		if (!strcmp(name, levels[index]))
			return 1;
	}
	return 0;
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
#ifdef HALO_RELEASE
	/* (the automated tests' answer, never a release build's: the player's) */
	return -1;
#else
	const char *answer = getenv("HALO_CROSSPLAY_LARGE_ANSWER");

	if (!answer || !answer[0])
		return -1;
	return !strcmp(answer, "yes") || !strcmp(answer, "1");
#endif
}

/* (network_wire.h's NETWORK_WIRE_ADVERTISED_VITA_HOST_FLAG, and
ChupathingyCE's DELTA_ADVERTISED_FLAG, delta.h of 7d6d8f24) */
enum
{
	ADVERTISED_VITA_HOST_FLAG = 0x40,
	ADVERTISED_DELTA_FLAG = 0x04,
};

int crossplay_guard_platform_of_advertisement(int crossplay, int other_version, int flags)
{
	if (other_version)
		return CROSSPLAY_PLATFORM_PC;
	if (!crossplay || (flags & ADVERTISED_VITA_HOST_FLAG))
		return CROSSPLAY_PLATFORM_VITA;
	if (flags & ADVERTISED_DELTA_FLAG)
		return CROSSPLAY_PLATFORM_CHUPA;
	return CROSSPLAY_PLATFORM_OPENCE;
}

int crossplay_guard_platform_of_listing(int crossplay, int vita_host, int dedicated)
{
	if (!crossplay || vita_host)
		return CROSSPLAY_PLATFORM_VITA;
	return dedicated ? CROSSPLAY_PLATFORM_CHUPA : CROSSPLAY_PLATFORM_PC;
}

const char *crossplay_guard_tag(int platform)
{
	static const char *const tags[NUMBER_OF_CROSSPLAY_PLATFORMS] = { "VITA", "PC", "OPENCE", "CHUPA", "DAMNATION" };

	return platform >= 0 && platform < NUMBER_OF_CROSSPLAY_PLATFORMS ? tags[platform] : tags[CROSSPLAY_PLATFORM_PC];
}

void crossplay_guard_row_badge(char *badge, int size, int platform, int crossplay, int dedicated, int verdict)
{
	int length;

	if (!badge || size <= 0)
		return;
	length = snprintf(badge, (size_t)size, "%s%s", crossplay_guard_tag(platform), dedicated ? T(" SERVER") : "");
	if (length < 0 || length >= size)
		return;
	if (platform == CROSSPLAY_PLATFORM_VITA)
		snprintf(badge + length, (size_t)(size - length), "%s", crossplay ? "+PC" : "");
	else
		snprintf(badge + length, (size_t)(size - length), "%s", crossplay_guard_badge_suffix(verdict));
}

const char *crossplay_guard_platform_line(int platform, int crossplay)
{
	switch (platform)
	{
	case CROSSPLAY_PLATFORM_VITA:
		return crossplay ? T("Hosted on a Vita: PC players can join") : T("Hosted on a Vita");
	case CROSSPLAY_PLATFORM_OPENCE:
		return T("Hosted on PC (OpenCE)");
	case CROSSPLAY_PLATFORM_CHUPA:
		return T("Hosted on PC (ChupathingyCE)");
	case CROSSPLAY_PLATFORM_DAMNATION:
		return T("Hosted on PC (DamnationCE)");
	}
	return T("Hosted on PC");
}

const char *crossplay_guard_host_tag(int platform, int marks)
{
	if (platform == CROSSPLAY_PLATFORM_VITA && (marks & CROSSPLAY_HOST_PC_BUILD))
		return "VITA (PC)";
	return crossplay_guard_tag(platform);
}

void crossplay_guard_host_badge(char *badge, int size, int platform, int marks, int crossplay, int dedicated,
	int verdict, int viewer_pc)
{
	int length;

	if (!badge || size <= 0)
		return;
	if (platform != CROSSPLAY_PLATFORM_VITA)
	{
		crossplay_guard_row_badge(badge, size, platform, crossplay, dedicated, verdict);
		return;
	}
	length = snprintf(badge, (size_t)size, "%s%s%s", crossplay_guard_host_tag(platform, marks),
		dedicated ? T(" SERVER") : "", crossplay ? "+PC" : "");
	if (length < 0 || length >= size)
		return;
	if (crossplay_guard_vitas_only(marks, crossplay, viewer_pc))
		snprintf(badge + length, (size_t)(size - length), "%s", T(" VITAS ONLY"));
}

int crossplay_guard_vita_game(int platform, int marks)
{
	return platform == CROSSPLAY_PLATFORM_VITA && !(marks & CROSSPLAY_HOST_PC_BUILD);
}

const char *crossplay_guard_host_line(int platform, int marks, int crossplay)
{
	if (platform == CROSSPLAY_PLATFORM_VITA && (marks & CROSSPLAY_HOST_PC_BUILD))
		return crossplay ? T("Hosted on a PC (Halo CE Vita): OpenCE players can join") : T("Hosted on a PC (Halo CE Vita)");
	return crossplay_guard_platform_line(platform, crossplay);
}

int crossplay_guard_vitas_only(int marks, int crossplay, int viewer_pc)
{
	return viewer_pc && !crossplay && (marks & CROSSPLAY_HOST_VITAS_ONLY);
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

const char *crossplay_guard_title(int verdict)
{
	return verdict == CROSSPLAY_GUARD_COOP ? T("Halo: PC co-op game") : crossplay_guard_large_title();
}

const char *crossplay_guard_question(int verdict, int players)
{
	if (verdict != CROSSPLAY_GUARD_COOP)
		return crossplay_guard_large_question();
	if (players > CROSSPLAY_GUARD_COOP_PLAYERS)
		return T("PC co-op game with more than 4 players: experimental on the Vita. More players and the host's extra "
			"enemies can make it lag. Join anyway?");
	return T("PC co-op game: experimental on the Vita. The host's AI and extra enemies can make it lag. Join anyway?");
}

const char *crossplay_guard_badge_suffix(int verdict)
{
	switch (verdict)
	{
	case CROSSPLAY_GUARD_COOP:
		return T(" CO-OP EXPERIMENTAL");
	case CROSSPLAY_GUARD_LARGE:
		return T(" EXPERIMENTAL");
	}
	return "";
}
