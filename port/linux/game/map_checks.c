/*
MAP_CHECKS.C

The Xbox's levels in the maps folder, looked at for the menus' map lists
(map_checks.h). Each look is file calls - the path looked for in the maps
folder, the file opened, its header read - and on the Vita each waits for
the memory card: the Forge lobby asked whether its map's file was there
every frame (forge_map_available, an fopen), and its map picker for all
thirteen, and on Ben's insider.17 Vita the lobby ran at 10 frames a second,
all of it the game thread's file calls (Oct 2026). So a map is looked at
once a run, the answer kept: the maps folder changes only by a map share
download, the mod manager or the settings panel, which say so
(custom_edition_maps_look_again -> map_checks_forget), and by a map copied
over FTP while the game runs, which the next run sees.
*/

#include "cseries.h"
#include "cache/cache_files.h"
#include "tag_files/tag_files.h"

#include "custom_edition_cache.h"
#include "custom_edition_maps.h"
#include "map_checks.h"
#include "../src/lang.h"

#include <stdio.h>
#include <string.h>

void platform_log(char const *format, ...);
int halo_file_identity(const char *xbox_path, unsigned long *size, unsigned long *seconds, unsigned long *nanoseconds);

/* (an Xbox map's header, cache_files_windows.c's struct cache_file_header) */
#define MAP_CHECK_HEADER_SIZE 0x800UL
#define MAP_CHECKS_MAXIMUM 32
#define MAP_CHECK_NAME_SIZE 32
#define MAP_CHECK_WHY_SIZE 160

static struct
{
	struct
	{
		char name[MAP_CHECK_NAME_SIZE];
		short state;
		char why[MAP_CHECK_WHY_SIZE];
	} entries[MAP_CHECKS_MAXIMUM];
	short count;
	unsigned long looks;
} map_checks;

/* a look at the file: its state, and a reason; `keep` FALSE when the file
did not read just now (looked at again next time) */
static short map_check_look(char const *name, char *why, long why_size, boolean *keep)
{
	char path[256];
	char problem[320];
	unsigned long size = 0, seconds, nanoseconds;
	boolean unreadable = FALSE;
	int length;

	*keep = TRUE;
	why[0] = 0;
	map_checks.looks++;
	length = snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), name);
	if (length < 0 || length >= (int)sizeof(path) || halo_file_identity(path, &size, &seconds, &nanoseconds) != 0)
	{
		snprintf(why, (size_t)why_size, T("%s.map isn't in your maps folder"), name);
		return _map_check_missing;
	}
	/* (a Halo PC map of an Xbox level's name loads as a Custom Edition map
	when PC maps are on: custom_edition_maps_loadable's to say) */
	if (size >= MAP_CHECK_HEADER_SIZE && custom_edition_cache_is_custom_edition(name))
	{
		return _map_check_playable;
	}
	if (size < MAP_CHECK_HEADER_SIZE)
	{
		platform_log("map checks: %s.map is cut short (%lu bytes)", name, size);
		snprintf(why, (size_t)why_size, T("%s.map is damaged - copy it again"), name);
		return _map_check_unusable;
	}
	if (cache_files_xbox_map_problem_read(name, problem, sizeof(problem), &unreadable))
	{
		if (unreadable)
		{
			/* (a whole file that did not answer: not its verdict) */
			platform_log("map checks: %s.map did not read just now (%s): looked at again later", name, problem);
			*keep = FALSE;
			return _map_check_playable;
		}
		platform_log("map checks: %s.map can't be played: %s", name, problem);
		snprintf(why, (size_t)why_size, T("%s.map is damaged - copy it again"), name);
		return _map_check_unusable;
	}
	return _map_check_playable;
}

short map_check(char const *level_name, char *why, long why_size)
{
	char const *name = tag_name_strip_path(level_name ? level_name : "");
	char reason[MAP_CHECK_WHY_SIZE];
	short index, state;
	boolean keep;

	if (why && why_size > 0)
		why[0] = 0;
	if (!name[0] || strlen(name) >= MAP_CHECK_NAME_SIZE || !custom_edition_maps_is_xbox_level(name))
		return _map_check_playable;
	for (index = 0; index < map_checks.count; index++)
	{
		if (!csstrcasecmp(map_checks.entries[index].name, name))
		{
			if (why && why_size > 0)
				snprintf(why, (size_t)why_size, "%s", map_checks.entries[index].why);
			return map_checks.entries[index].state;
		}
	}
	state = map_check_look(name, reason, sizeof(reason), &keep);
	if (keep && map_checks.count < MAP_CHECKS_MAXIMUM)
	{
		index = map_checks.count++;
		snprintf(map_checks.entries[index].name, sizeof(map_checks.entries[index].name), "%s", name);
		map_checks.entries[index].state = state;
		snprintf(map_checks.entries[index].why, sizeof(map_checks.entries[index].why), "%s", reason);
	}
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "%s", reason);
	return state;
}

boolean map_check_playable(char const *level_name)
{
	return map_check(level_name, NULL, 0) == _map_check_playable;
}

void map_checks_forget(void)
{
	map_checks.count = 0;
}

void map_checks_unusable(char const *level_name)
{
	char const *name = tag_name_strip_path(level_name ? level_name : "");
	short index;

	if (!name[0] || strlen(name) >= MAP_CHECK_NAME_SIZE || !custom_edition_maps_is_xbox_level(name))
		return;
	for (index = 0; index < map_checks.count && csstrcasecmp(map_checks.entries[index].name, name); index++)
		;
	if (index == map_checks.count)
	{
		if (map_checks.count >= MAP_CHECKS_MAXIMUM)
			return;
		map_checks.count++;
	}
	snprintf(map_checks.entries[index].name, sizeof(map_checks.entries[index].name), "%s", name);
	map_checks.entries[index].state = _map_check_unusable;
	snprintf(map_checks.entries[index].why, sizeof(map_checks.entries[index].why), T("%s.map is damaged - copy it again"),
		name);
	platform_log("map checks: %s.map failed to load: unusable in the map lists", name);
}

unsigned long map_checks_looks(void)
{
	return map_checks.looks;
}
