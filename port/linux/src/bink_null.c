/*
BINK_NULL.C

The Bink video SDK entry points bink_playback.c uses. There is no Bink
decoder in the Linux build (the RAD SDK is proprietary and the reconstructed
libs/binkxbox is incomplete), so BinkOpen reports that a movie cannot be
opened and the game skips it, exactly as it does for a missing movie file
(HALO_TEST_MOVIE, below, plays a made-up picture instead, for tests).

The prototypes match the declarations in bink_playback.c; the RAD SDK's
RADEXPLINK is __stdcall.
*/

#include "platform.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(__stdcall *rad_memory_allocate_proc)(unsigned long size);
typedef void (__stdcall *rad_memory_free_proc)(void *memory);
typedef void *(__stdcall *bink_sound_system_open_proc)(unsigned long param);
typedef struct BINK *HBINK;

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

/* (debug) HALO_TEST_MOVIE_FAILS_FOR_NOW=1: each open fails as the Vita's
does when its video player does not start (memory), for now, not for want
of the file (run_no_movies_test.sh attractretry) */
static int open_failed_for_now;

int halo_movie_open_failed_for_now(void)
{
	return open_failed_for_now;
}

/* (test) HALO_TEST_MOVIE=<width>x<height>[:<frames>]: every movie opens
and plays a made-up picture that size (colour bars that move a pixel a
frame, a frame counter's bars below), <frames> long (300 by default), so
the movie's texture, its quad and its GPU programs run on the harness as
on the Vita (gxm-null: the programs collected with HALO_SHADER_COLLECT, the
movie path logged; GL: a screenshot shows the picture). Never in release
builds. */
struct BINK
{
	unsigned long Width;
	unsigned long Height;
	unsigned long Frames;
	unsigned long FrameNum;
	unsigned long LastFrameNum;
};

static struct BINK test_movie;

/* (the renderer's movie frames: d3d8_gxm.c; d3d8_gl.c has none) */
void *halo_d3d_movie_buffer(unsigned long width, unsigned long height, long *pitch, int cdram_first);
void halo_d3d_movie_buffer_done(void);
int halo_d3d_movie_draw(float left, float top, float right, float bottom);
void halo_d3d_movie_release(void);
static int test_movie_open;
/* (the open movie's frames in the renderer's memory; the legacy path logged) */
static int test_movie_native, test_movie_logged;

static int test_movie_setting(unsigned long *width, unsigned long *height, unsigned long *frames)
{
#ifndef HALO_RELEASE
	const char *setting = getenv("HALO_TEST_MOVIE");
	unsigned long w = 0, h = 0, f = 300;
	const char *colon;

	if (!setting || !*setting || sscanf(setting, "%lux%lu", &w, &h) != 2 || !w || !h || w > 2048 || h > 2048)
		return 0;
	colon = strchr(setting, ':');
	if (colon && atol(colon + 1) > 2)
		f = (unsigned long)atol(colon + 1);
	*width = w & ~1UL;
	*height = h;
	*frames = f;
	return 1;
#else
	(void)width; (void)height; (void)frames;
	return 0;
#endif
}

HBINK __stdcall BinkOpen(const char *name, unsigned long flags)
{
#ifndef HALO_RELEASE
	const char *setting = getenv("HALO_TEST_MOVIE_FAILS_FOR_NOW");
#else
	const char *setting = NULL; /* (a test's, never in release builds) */
#endif
	unsigned long width, height, frames;

	(void)flags;
	if (test_movie_setting(&width, &height, &frames))
	{
		memset(&test_movie, 0, sizeof(test_movie));
		test_movie.Width = width;
		test_movie.Height = height;
		test_movie.Frames = frames;
		test_movie_open = 1;
		open_failed_for_now = 0;
		platform_log("test movie: \"%s\" plays a %lux%lu picture, %lu frames (HALO_TEST_MOVIE)", name ? name : "",
			width, height, frames);
		return &test_movie;
	}
	open_failed_for_now = setting && atoi(setting) != 0;
	platform_log("Bink video is not supported; skipping \"%s\"%s", name ? name : "",
		open_failed_for_now ? " (failed for now: HALO_TEST_MOVIE_FAILS_FOR_NOW)" : "");
	return NULL;
}

float halo_movie_display_aspect(void)
{
	return test_movie_open ? (float)test_movie.Width / (float)test_movie.Height : 0.0f;
}

void __stdcall BinkClose(HBINK bink)
{
	if (bink == &test_movie && test_movie_open)
	{
		platform_log("test movie: closed at frame %lu", test_movie.FrameNum);
		test_movie_open = 0;
		test_movie_native = 0;
		test_movie_logged = 0;
		halo_d3d_movie_release();
	}
}

long __stdcall BinkDoFrame(HBINK bink) { (void)bink; return 0; }

void __stdcall BinkNextFrame(HBINK bink)
{
	if (bink && bink->FrameNum < bink->Frames - 1)
	{
		bink->LastFrameNum = bink->FrameNum;
		bink->FrameNum++;
	}
}

long __stdcall BinkWait(HBINK bink) { (void)bink; return 0; }

/* the made-up picture: eight colour bars (white, yellow, cyan, green,
magenta, red, blue, black) in the top three quarters, moving right a pixel
a frame, and the frame number's 12 bits as white or grey blocks below */
static void test_movie_picture(const struct BINK *bink, void *destination, long pitch, unsigned long height)
{
	static const uint32_t bars[8] = {
		0xffffffffu, 0xffffff00u, 0xff00ffffu, 0xff00ff00u, 0xffff00ffu, 0xffff0000u, 0xff0000ffu, 0xff000000u
	};
	unsigned long x, y, width = bink->Width;

	for (y = 0; y < height; y++)
	{
		uint32_t *row = (uint32_t *)((unsigned char *)destination + y * pitch);

		for (x = 0; x < width; x++)
		{
			if (y < height * 3 / 4)
				row[x] = bars[(((x + bink->FrameNum) % width) * 8 / width) & 7];
			else
				row[x] = (bink->FrameNum >> (11 - (x * 12 / width)) & 1) ? 0xffffffffu : 0xff404040u;
		}
	}
}

/* (as the Vita's bink_vita.c: HALO_MOVIE_PATH uncached (the default) or
cdram puts the frames in the renderer's memory where it has such
(d3d8_gxm.c, the gxm-null harness; the OpenGL renderer has none), legacy
in the game's movie texture) */

long __stdcall BinkCopyToBuffer(HBINK bink, void *destination, long destination_pitch,
	unsigned long destination_height, unsigned long destination_x, unsigned long destination_y,
	unsigned long flags)
{
	const char *path = getenv("HALO_MOVIE_PATH");
	int legacy = path && !strcmp(path, "legacy");
	unsigned long height;

	(void)destination_x; (void)destination_y; (void)flags;
	if (bink != &test_movie || !test_movie_open)
		return 0;
	height = destination_height < bink->Height ? destination_height : bink->Height;
	if (!legacy)
	{
		long pitch = 0;
		void *frame = halo_d3d_movie_buffer(bink->Width, bink->Height, &pitch, path && !strcmp(path, "cdram"));

		if (frame)
		{
			test_movie_picture(bink, frame, pitch, bink->Height);
			halo_d3d_movie_buffer_done();
			test_movie_native = 1;
			return 0;
		}
	}
	test_movie_native = 0;
	if (!test_movie_logged++)
		platform_log("test movie: path legacy%s: the game's texture at %p (rows %ld bytes apart)",
			legacy ? " (HALO_MOVIE_PATH)" : " (no renderer memory)", destination, destination_pitch);
	if (destination)
		test_movie_picture(bink, destination, destination_pitch, height);
	return 0;
}

int halo_movie_draw_native(float left, float top, float right, float bottom)
{
	return test_movie_open && test_movie_native && halo_d3d_movie_draw(left, top, right, bottom);
}

void __stdcall BinkGetSummary(HBINK bink, void *summary) { (void)bink; (void)summary; }
void __stdcall BinkGetRealtime(HBINK bink, void *realtime, unsigned long frame_count) { (void)bink; (void)realtime; (void)frame_count; }
