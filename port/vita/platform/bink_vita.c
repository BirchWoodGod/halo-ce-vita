/*
BINK_VITA.C

The Bink video SDK entry points bink_playback.c uses (as bink_null.c on
Linux), answered by the Vita's video player (port/vita/host/vita_movie.c)
from H.264 copies of the disc's movies: d:\bink\<name>.bik plays
ux0:data/haloce-vita/movies/<name>.mp4. A missing copy is skipped, as the
game skips a missing movie. The movie's sound plays on its own audio port,
so the game is told there is no DirectSound for Bink.
*/

#include "platform.h"
#include "vita_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(__stdcall *rad_memory_allocate_proc)(unsigned long size);
typedef void (__stdcall *rad_memory_free_proc)(void *memory);
typedef void *(__stdcall *bink_sound_system_open_proc)(unsigned long param);

/* bink_playback.c's view of the handle */
typedef struct BINK
{
	unsigned long Width;
	unsigned long Height;
	unsigned long Frames;
	unsigned long FrameNum;
	unsigned long LastFrameNum;
} *HBINK;

static struct BINK movie_handle;
static int movie_open;

void __stdcall RADSetMemory(rad_memory_allocate_proc allocate, rad_memory_free_proc release)
{
	(void)allocate;
	(void)release;
}

void *__stdcall BinkOpenDirectSound(unsigned long param)
{
	(void)param;
	return NULL;
}

long __stdcall BinkSetSoundSystem(bink_sound_system_open_proc open, unsigned long param)
{
	(void)open;
	(void)param;
	return 0;
}

void __stdcall BinkSetIOSize(unsigned long io_size)
{
	(void)io_size;
}

HBINK __stdcall BinkOpen(const char *name, unsigned long flags)
{
	char path[256];
	const char *base, *dot;
	size_t length;
	unsigned long width = 0, height = 0;

	(void)flags;
	if (!name)
		return NULL;
	/* (debug) HALO_NO_MOVIES=1: every movie skipped */
	if (getenv("HALO_NO_MOVIES") && atoi(getenv("HALO_NO_MOVIES")))
		return NULL;
	/* d:\bink\intro.bik (or introfr.bik...) -> movies/intro.mp4 */
	base = strrchr(name, '\\');
	base = base ? base + 1 : name;
	dot = strrchr(base, '.');
	length = dot ? (size_t)(dot - base) : strlen(base);
	if (length > 64)
		length = 64;
	snprintf(path, sizeof(path), "ux0:data/haloce-vita/movies/%.*s.mp4", (int)length, base);
	if (vita_movie_open(path, &width, &height) != 0)
		return NULL;
	memset(&movie_handle, 0, sizeof(movie_handle));
	movie_handle.Width = width;
	movie_handle.Height = height;
	/* (the end is told by FrameNum reaching Frames-1: set when the player
	finishes) */
	movie_handle.Frames = 0x7fffffffUL;
	movie_open = 1;
	return &movie_handle;
}

void __stdcall BinkClose(HBINK bink)
{
	(void)bink;
	if (movie_open)
		vita_movie_close();
	movie_open = 0;
}

/* 0 when a frame is due (or the movie ended: the game then sees the last
frame and stops); never a wait the game could spin on forever */
long __stdcall BinkWait(HBINK bink)
{
	int state = movie_open ? vita_movie_poll() : -1;

	if (state < 0 && bink)
	{
		bink->FrameNum = bink->Frames - 1;
		return 0;
	}
	/* (not yet: the game spins on this, so the decoder's threads are given
	the core for a moment rather than starved by the spin) */
	if (state == 0)
		vita_host_sleep_us(1000);
	return state == 1 ? 0 : 1;
}

long __stdcall BinkDoFrame(HBINK bink)
{
	(void)bink;
	return 0;
}

void __stdcall BinkNextFrame(HBINK bink)
{
	if (bink && bink->FrameNum < bink->Frames - 2)
	{
		bink->LastFrameNum = bink->FrameNum;
		bink->FrameNum++;
	}
}

long __stdcall BinkCopyToBuffer(HBINK bink, void *destination, long destination_pitch,
	unsigned long destination_height, unsigned long destination_x, unsigned long destination_y,
	unsigned long flags)
{
	(void)destination_x;
	(void)destination_y;
	(void)flags;
	if (bink && destination)
		vita_movie_copy(destination, destination_pitch, bink->Width,
			destination_height < bink->Height ? destination_height : bink->Height);
	return 0;
}

void __stdcall BinkGetSummary(HBINK bink, void *summary) { (void)bink; (void)summary; }
void __stdcall BinkGetRealtime(HBINK bink, void *realtime, unsigned long frame_count) { (void)bink; (void)realtime; (void)frame_count; }
