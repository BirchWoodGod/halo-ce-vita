/*
HUD_SOUNDS.C

symbols in this file:
000C6430 0160:
	_hud_play_sound (0000)
00270240 0026:
	??_C@_0CG@LACJPAPB@c?3?2halo?2SOURCE?2interface?2hud_sou@ (0000)
*/

/* ---------- headers */

#include "cseries.h"

#include "interface/hud.h"
#include "objects/objects.h"
#include "sound/sound_definitions.h"
#include "sound/game_sound.h"
#include "sound/sound_manager.h"
#include "tag_files/tag_groups.h"
#ifdef HALO_LINUX
#include "tick_thread.h"
#endif

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct hud_sound_definition
{
	struct tag_reference sound;
	unsigned long state_flags;
	real scale;
	/* Reserved by the tag format; the original block accessor proves the 0x38-byte element size. */
	byte reserved[32];
};

/* ---------- prototypes */

/* ---------- globals */

#ifdef HALO_LINUX
/* (port) The HUD is drawn on the main thread while the tick runs on its own
(HALO_TICK_THREAD), and a looping warning sound (shields recharging, low
health) is a game looping sound: starting one there took a slot of the tick's
looping sound array under it (the log's "new of object looping sounds on the
render thread while a tick runs (unsafe)"), and stopping one set a flag the
tick's game_sound_update rewrites. While a tick runs, the start or stop is
queued and made at the join, after the tick; the handle reads "pending" until
then. A pending handle a checkpoint caught counts as no sound. */
enum
{
	HUD_LOOPING_SOUND_PENDING = -2,
	MAXIMUM_HUD_LOOPING_SOUND_REQUESTS = 16
};

static struct
{
	long *handle; /* NULL for a stop */
	long definition_index;
	real scale;
	long stop_index;
} hud_looping_sound_requests[MAXIMUM_HUD_LOOPING_SOUND_REQUESTS];
static short hud_looping_sound_request_count = 0;

static void hud_looping_sound_requests_make(
	void)
{
	short request_index;

	for (request_index = 0; request_index < hud_looping_sound_request_count; request_index++)
	{
		if (hud_looping_sound_requests[request_index].handle)
		{
			/* (unless it was stopped or the game state replaced meanwhile) */
			if (*hud_looping_sound_requests[request_index].handle == HUD_LOOPING_SOUND_PENDING)
			{
				*hud_looping_sound_requests[request_index].handle = unattached_looping_sound_start(
					hud_looping_sound_requests[request_index].definition_index,
					NONE,
					hud_looping_sound_requests[request_index].scale);
			}
		}
		else
		{
			unattached_looping_sound_stop(hud_looping_sound_requests[request_index].stop_index);
		}
	}
	hud_looping_sound_request_count = 0;

	return;
}

/* TRUE when queued for the join; FALSE when no tick runs (or the queue is
full) and the caller makes it now */
static boolean hud_looping_sound_request(
	long *handle,
	long definition_index,
	real scale,
	long stop_index)
{
	short request_index;

	if (handle)
	{
		for (request_index = 0; request_index < hud_looping_sound_request_count; request_index++)
		{
			if (hud_looping_sound_requests[request_index].handle == handle)
				return TRUE;
		}
	}
	if (hud_looping_sound_request_count >= MAXIMUM_HUD_LOOPING_SOUND_REQUESTS)
		return FALSE;
	if (!hud_looping_sound_request_count && !halo_tick_thread_defer(hud_looping_sound_requests_make))
		return FALSE;
	hud_looping_sound_requests[hud_looping_sound_request_count].handle = handle;
	hud_looping_sound_requests[hud_looping_sound_request_count].definition_index = definition_index;
	hud_looping_sound_requests[hud_looping_sound_request_count].scale = scale;
	hud_looping_sound_requests[hud_looping_sound_request_count].stop_index = stop_index;
	hud_looping_sound_request_count++;

	return TRUE;
}
#endif

/* ---------- public code */

void hud_play_sound(
	short local_player_index,
	unsigned long state_flags,
	struct tag_block const *sounds,
	long *sound_indices,
	word *played_flags)
{
	long absolute_sound_index = 0;
	short sound_index = 0;

	if (sounds->count > 0)
		do
		{
			struct hud_sound_definition const *sound =
				TAG_BLOCK_GET_ELEMENT(sounds, absolute_sound_index, struct hud_sound_definition);

			if (state_flags & sound->state_flags)
			{
				switch (sound->sound.group_tag)
				{
				default:
					match_assert("c:\\halo\\SOURCE\\interface\\hud_sounds.c", 47, !"unreachable");
					break;

				case SOUND_DEFINITION_TAG:
					if (sound_indices[absolute_sound_index] == NONE ||
						!TEST_FLAG(*played_flags, absolute_sound_index))
					{
						if (sound_indices[absolute_sound_index] != NONE)
							sound_stop_impulse(sound_indices[absolute_sound_index]);
						sound_indices[absolute_sound_index] =
							unspatialized_impulse_sound_new(sound->sound.index, sound->scale);
					}
					break;

				case LOOPING_SOUND_DEFINITION_TAG:
#ifdef HALO_LINUX
					if (sound_indices[absolute_sound_index] == NONE ||
						sound_indices[absolute_sound_index] == HUD_LOOPING_SOUND_PENDING)
					{
						if (hud_looping_sound_request(&sound_indices[absolute_sound_index],
							sound->sound.index, sound->scale, NONE))
						{
							sound_indices[absolute_sound_index] = HUD_LOOPING_SOUND_PENDING;
						}
						else
						{
							sound_indices[absolute_sound_index] =
								unattached_looping_sound_start(sound->sound.index, NONE, sound->scale);
						}
					}
#else
					if (sound_indices[absolute_sound_index] == NONE)
						sound_indices[absolute_sound_index] =
							unattached_looping_sound_start(sound->sound.index, NONE, sound->scale);
#endif
					break;
				}

				SET_FLAG(*played_flags, absolute_sound_index, TRUE);
			}
			else
			{
				if (sound_indices[absolute_sound_index] != NONE)
				{
					switch (sound->sound.group_tag)
					{
					default:
						match_assert("c:\\halo\\SOURCE\\interface\\hud_sounds.c", 64, !"unreachable");
						break;
					case SOUND_DEFINITION_TAG:
						break;
					case LOOPING_SOUND_DEFINITION_TAG:
#ifdef HALO_LINUX
						if (sound_indices[absolute_sound_index] != HUD_LOOPING_SOUND_PENDING &&
							!hud_looping_sound_request(NULL, NONE, 0.f, sound_indices[absolute_sound_index]))
						{
							unattached_looping_sound_stop(sound_indices[absolute_sound_index]);
						}
#else
						unattached_looping_sound_stop(sound_indices[absolute_sound_index]);
#endif
						break;
					}

					sound_indices[absolute_sound_index] = NONE;
					SET_FLAG(*played_flags, absolute_sound_index, FALSE);
				}
			}

			sound_index++;
			absolute_sound_index = sound_index;
		}
		while (absolute_sound_index < sounds->count);

	return;
}

/* ---------- private code */
