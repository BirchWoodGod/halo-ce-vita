/*
BINK_VITA.C

The Bink video SDK entry points bink_playback.c uses (as bink_null.c on
Linux), answered by the Vita's video player (port/vita/host/vita_movie.c)
from H.264 copies of the disc's movies: d:\bink\<name>.bik plays
ux0:data/haloce-vita/movies/<name>.mp4. A missing copy is skipped, as the
game skips a missing movie. The movie's sound plays on its own audio port,
so the game is told there is no DirectSound for Bink.

HALO_MOVIE_PATH (env.txt, read at each movie's start) picks where a frame
goes and what draws it:
  uncached (the default)  memory of the renderer's own, user RAM mapped
          uncached for the GPU, a ring of frames; drawn at the present, at
          the display's resolution, by the renderer's built-in blit
          programs (d3d8_gxm.c halo_d3d_movie_*);
  cdram   the same in CDRAM;
  legacy  the game's movie texture (a linear X8R8G8B8 texture in the
          game's memory window, decoded into the texture pool each frame)
          drawn by the game's own quad - whose fragment program
          (e038da4e6676a9c4) must be in shaders.pak or compiled on the
          device: on a Vita without the shader compiler it was not, and the
          movies were black with their sound playing (1.1.0 on).
The first two fall back to legacy when the renderer has no memory for them.
halo.log says which path a movie took, where its frames are and of what
memory ("movie: path ...").
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

/* HALO_MOVIE_PATH (above) */
enum
{
	_movie_path_uncached,
	_movie_path_cdram,
	_movie_path_legacy
};
static int movie_path;
/* the open movie's frames went to the renderer's memory (halo_d3d_movie_*),
and whether the path taken was logged */
static int movie_native, movie_path_logged;

void *halo_d3d_movie_buffer(unsigned long width, unsigned long height, long *pitch, int cdram_first);
void halo_d3d_movie_buffer_done(void);
int halo_d3d_movie_draw(float left, float top, float right, float bottom);
void halo_d3d_movie_release(void);

static int movie_path_setting(void)
{
	const char *setting = getenv("HALO_MOVIE_PATH");

	if (setting && !strcmp(setting, "legacy"))
		return _movie_path_legacy;
	if (setting && !strcmp(setting, "cdram"))
		return _movie_path_cdram;
	return _movie_path_uncached;
}

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

/* the last BinkOpen failed for now (halo_movie_open_failed_for_now) */
static int open_failed_for_now;

int halo_movie_open_failed_for_now(void)
{
	return open_failed_for_now;
}

HBINK __stdcall BinkOpen(const char *name, unsigned long flags)
{
	char path[256];
	const char *base, *dot;
	size_t length;
	unsigned long width = 0, height = 0;

	(void)flags;
	open_failed_for_now = 0;
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
	{
		open_failed_for_now = vita_movie_open_failed_for_now();
		return NULL;
	}
	memset(&movie_handle, 0, sizeof(movie_handle));
	movie_handle.Width = width;
	movie_handle.Height = height;
	/* (the end is told by FrameNum reaching Frames-1: set when the player
	finishes) */
	movie_handle.Frames = 0x7fffffffUL;
	movie_open = 1;
	movie_path = movie_path_setting();
	movie_native = 0;
	movie_path_logged = 0;
	return &movie_handle;
}

float halo_movie_display_aspect(void)
{
	return movie_open ? vita_movie_display_aspect() : 0.0f;
}

void __stdcall BinkClose(HBINK bink)
{
	(void)bink;
	if (movie_open)
		vita_movie_close();
	movie_open = 0;
	movie_native = 0;
	/* (the renderer's frames let go, its worker and the GPU done with them) */
	halo_d3d_movie_release();
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
	unsigned long height;
	char message[256];

	(void)destination_x;
	(void)destination_y;
	(void)flags;
	if (!bink)
		return 0;
	height = destination_height < bink->Height ? destination_height : bink->Height;
	if (movie_path != _movie_path_legacy)
	{
		long pitch = 0;
		void *frame = halo_d3d_movie_buffer(bink->Width, bink->Height, &pitch, movie_path == _movie_path_cdram);

		if (frame)
		{
			vita_movie_copy(frame, pitch, bink->Width, bink->Height);
			halo_d3d_movie_buffer_done();
			movie_native = 1;
			return 0;
		}
	}
	movie_native = 0;
	if (!movie_path_logged)
	{
		movie_path_logged = 1;
		snprintf(message, sizeof(message), "movie: path legacy%s: the game's texture at %p (rows %ld bytes apart, %s), "
			"decoded into the texture pool each frame and drawn by the game's quad (its fragment program from shaders.pak "
			"or the shader compiler)", movie_path == _movie_path_legacy ? " (HALO_MOVIE_PATH)" : " (no renderer memory)",
			destination, destination_pitch, destination && platform_is_contiguous(destination) ?
			"in the game's memory window: user RAM, cached" : "not in the game's memory window");
		vita_host_log(message);
	}
	if (destination)
		vita_movie_copy(destination, destination_pitch, bink->Width, height);
	return 0;
}

int halo_movie_draw_native(float left, float top, float right, float bottom)
{
	return movie_open && movie_native && halo_d3d_movie_draw(left, top, right, bottom);
}

void __stdcall BinkGetSummary(HBINK bink, void *summary) { (void)bink; (void)summary; }
void __stdcall BinkGetRealtime(HBINK bink, void *realtime, unsigned long frame_count) { (void)bink; (void)realtime; (void)frame_count; }
