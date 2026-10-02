/*
SOUND_PREFERENCES.C

symbols in this file:
001BF310 0010:
	_read_sound_preferences (0000)
001BF320 0010:
	_write_sound_preferences (0000)
00317A84 001c:
	_data_00317a84 (0000)
	_sound_channel_type_flags (0014)
*/

/* ---------- headers */

#include "sound_preferences.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes */

/* ---------- globals */

struct sound_preferences default_sound_preferences =
{
	0,
	{ 10, 51, 10, 10 },
	{ 9, 46, 9, 9 },
	0,
};

short sound_channel_type_flags[4] = { 8, 9, 10, 14 };

/* ---------- public code */

#ifdef HALO_LINUX
#include <stdlib.h>
#endif

void read_sound_preferences(struct sound_preferences **preferences)
{
#ifdef HALO_LINUX
	/* (port) HALO_SOUND_CHANNELS=<n>: at most n positional (3D) sounds
	play at once instead of 46 (the Vita's settings panel: Sound voices).
	The sound manager's own priorities choose which (sound_find_channel:
	a sound preempts a lower priority one, or does not start), as on the
	Xbox when all 46 were busy; music, ambience and the unpositioned
	dialogue keep their channels. Every playing voice costs the mixer, the
	channel update and an obstruction ray on the tick, and the b30 fight
	kept 38-45 voices playing. Not audio alone: what plays feeds back into
	the game (a capped b30 run's units and actors part from the original's
	after ~40 s), so the default is the original. Read once, at start-up. */
	static struct sound_preferences capped;
	const char *setting = getenv("HALO_SOUND_CHANNELS");
	int channels = setting ? atoi(setting) : 0;

	if (channels > 0 && channels < default_sound_preferences.virtual_channel_counts[1])
	{
		capped = default_sound_preferences;
		capped.virtual_channel_counts[1] = (short)channels;
		*preferences = &capped;
		return;
	}
#endif
	*preferences = &default_sound_preferences;
}

void write_sound_preferences(void)
{
}

/* ---------- private code */
