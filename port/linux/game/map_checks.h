/*
MAP_CHECKS.H

The Xbox's levels in the maps folder, looked at for the menus' map lists
once a run (map_checks.c): whether each is there, and whether its file can
be played - a file cut short (a 0-byte copy), damaged, another map's or
Halo PC's is shown as unusable in the pickers and cannot be chosen or
started, with a short reason.
*/

#ifndef __MAP_CHECKS_H
#define __MAP_CHECKS_H

enum
{
	_map_check_playable,
	/* (no file of that name in the maps folder) */
	_map_check_missing,
	/* (a file that cannot be played: cut short, damaged, another map's) */
	_map_check_unusable
};

/* one of the Xbox's levels (its level path, levels\test\...\name, or its
file's name): _map_check_playable, _missing or _unusable, and for the last
two a short reason for the menus in `why` ("beavercreek.map is damaged -
copy it again"). Looked at once a run: the answer kept until
map_checks_forget; a file that did not read just now is looked at again 5 s
later. Not an Xbox level's name: playable (another check's) */
short map_check(char const *level_name, char *why, long why_size);
/* map_check's answer is _map_check_playable */
boolean map_check_playable(char const *level_name);
/* every answer forgotten: the maps folder changed (a map share download,
the mod manager, the settings panel's delete), or a load failed */
void map_checks_forget(void);
/* (cache_files.c) the Xbox level's file failed to load for its own fault -
cut short past its header, damaged -: unusable from now on in the lists */
void map_checks_unusable(char const *level_name);
/* (debug, the tests) how many looks at the maps folder's files the checks
made this run */
unsigned long map_checks_looks(void);

#endif
