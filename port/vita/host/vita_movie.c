/*
VITA_MOVIE.C

The game's Bink movies (the intro before the first mission, the credits,
the attract videos) played by the Vita's own video player (SceAvPlayer)
from H.264 copies made once from the disc's .bik files (the Bink codec is
proprietary; ffmpeg converts it). The video frames come out in NV12 (a Y
plane, then interleaved chroma, both 16-aligned) and are converted for the
game's X8R8G8B8 movie texture; the sound goes to a BGM port from a thread
of its own. One movie at a time, as the game plays them.
*/

#include <psp2/audioout.h>
#include <psp2/avplayer.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>

#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "vita_host.h"

#define ALIGN(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))

static struct
{
	SceAvPlayerHandle player;
	int open;
	volatile int audio_running;
	SceUID audio_thread;
	int audio_port;
	unsigned int audio_grain;
	/* the latest video frame, and whether it was taken by the game */
	SceAvPlayerFrameInfo frame;
	int frame_valid;
	int frame_pending;
	unsigned long width, height;
} movie;

/* ---------- the player's memory */

static void *movie_allocate(void *argument, uint32_t alignment, uint32_t size)
{
	(void)argument;
	return memalign(alignment < 16 ? 16 : alignment, size);
}

static void movie_free(void *argument, void *pointer)
{
	(void)argument;
	free(pointer);
}

/* the decoder's frames: physically contiguous memory (the hardware decoder
writes it; the CPU reads it for the conversion). The Vita refuses an
explicit alignment for the contiguous main-memory type (INVALID_ARGUMENT,
which Vita3K accepts): that type is asked for in whole megabytes, which it
aligns itself, and video memory with a 256 KB alignment is the fallback, as
the Vita's other video players allocate it */
static void *movie_allocate_frame(void *argument, uint32_t alignment, uint32_t size)
{
	SceKernelAllocMemBlockOpt options;
	SceUID block;
	void *base = NULL;
	char message[128];

	(void)argument;
	if (alignment <= 0x100000)
		block = sceKernelAllocMemBlock("movie frame", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, ALIGN(size, 0x100000), NULL);
	else
		block = -1;
	if (block < 0)
	{
		SceUID first = block;

		if (alignment < 0x40000)
			alignment = 0x40000;
		memset(&options, 0, sizeof(options));
		options.size = sizeof(options);
		options.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
		options.alignment = alignment;
		block = sceKernelAllocMemBlock("movie frame", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, ALIGN(size, alignment), &options);
		snprintf(message, sizeof(message), "movie: a %u byte frame in video memory (contiguous main memory: 0x%08x): 0x%08x",
			(unsigned)size, (unsigned)first, (unsigned)block);
		vita_host_log(message);
	}
	if (block < 0)
		return NULL;
	sceKernelGetMemBlockBase(block, &base);
	return base;
}

static void movie_free_frame(void *argument, void *pointer)
{
	SceUID block;

	(void)argument;
	block = sceKernelFindMemBlockByAddr(pointer, 0);
	if (block >= 0)
		sceKernelFreeMemBlock(block);
}

/* ---------- sound */

static int audio_thread(SceSize arguments_size, void *arguments)
{
	(void)arguments_size;
	(void)arguments;
	while (movie.audio_running)
	{
		SceAvPlayerFrameInfo frame;

		if (movie.player && sceAvPlayerGetAudioData(movie.player, &frame) && frame.pData)
		{
			unsigned int channels = frame.details.audio.channelCount ? frame.details.audio.channelCount : 2;
			unsigned int grain = frame.details.audio.size / (2 * channels);

			if (movie.audio_port < 0 || grain != movie.audio_grain)
			{
				if (movie.audio_port >= 0)
					sceAudioOutReleasePort(movie.audio_port);
				movie.audio_grain = grain;
				movie.audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, (int)grain,
					(int)frame.details.audio.sampleRate, channels == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
				if (movie.audio_port < 0)
				{
					char message[96];

					snprintf(message, sizeof(message), "movie: no audio port (%u samples at %u Hz): 0x%08x", grain,
						(unsigned)frame.details.audio.sampleRate, (unsigned)movie.audio_port);
					vita_host_log(message);
				}
			}
			if (movie.audio_port >= 0)
				sceAudioOutOutput(movie.audio_port, frame.pData);
		}
		else
			sceKernelDelayThread(1000);
	}
	return sceKernelExitDeleteThread(0);
}

/* ---------- the movie */

int vita_movie_open(const char *path, unsigned long *width, unsigned long *height)
{
	SceAvPlayerInitData initialize;
	unsigned int wait;
	char message[256];

	if (movie.open)
		vita_movie_close();
	memset(&movie, 0, sizeof(movie));
	movie.audio_port = -1;
	{
		/* (the player's module, once) */
		static int loaded;

		if (!loaded)
		{
			sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
			loaded = 1;
		}
	}
	{
		/* (no movie file: the game skips the movie, as for a missing .bik) */
		FILE *file = fopen(path, "rb");

		if (!file)
		{
			snprintf(message, sizeof(message), "movie: %s not found (convert the disc's .bik with ffmpeg: see port/vita/README)", path);
			vita_host_log(message);
			return -1;
		}
		fclose(file);
	}
	memset(&initialize, 0, sizeof(initialize));
	initialize.memoryReplacement.allocate = movie_allocate;
	initialize.memoryReplacement.deallocate = movie_free;
	initialize.memoryReplacement.allocateTexture = movie_allocate_frame;
	initialize.memoryReplacement.deallocateTexture = movie_free_frame;
	initialize.basePriority = 0xA0;
	initialize.numOutputVideoFrameBuffers = 2;
	initialize.autoStart = 1;
	movie.player = sceAvPlayerInit(&initialize);
	/* (the handle is an address: an error is a 0x80xxxxxx code) */
	if (!movie.player || ((unsigned)movie.player >> 24) == 0x80)
	{
		snprintf(message, sizeof(message), "movie: sceAvPlayerInit failed: 0x%08x", (unsigned)movie.player);
		vita_host_log(message);
		return -1;
	}
	if (sceAvPlayerAddSource(movie.player, path) < 0)
	{
		snprintf(message, sizeof(message), "movie: cannot open %s", path);
		vita_host_log(message);
		sceAvPlayerClose(movie.player);
		movie.player = 0;
		return -1;
	}
	movie.open = 1;
	/* (debug) HALO_MOVIE_AUDIO=0: no sound thread */
	if (!getenv("HALO_MOVIE_AUDIO") || atoi(getenv("HALO_MOVIE_AUDIO")) != 0)
	{
		movie.audio_running = 1;
		movie.audio_thread = sceKernelCreateThread("movie audio", audio_thread, 0x40, 0x10000, 0, 0, NULL);
		if (movie.audio_thread >= 0)
			sceKernelStartThread(movie.audio_thread, 0, NULL);
	}
	/* the first frame gives the size (the game sizes its texture by it) */
	for (wait = 0; wait < 2000 && !movie.frame_valid; wait++)
	{
		if (sceAvPlayerGetVideoData(movie.player, &movie.frame) && movie.frame.pData)
		{
			movie.frame_valid = 1;
			movie.frame_pending = 1;
		}
		else
			sceKernelDelayThread(1000);
	}
	movie.width = movie.frame_valid ? movie.frame.details.video.width : 640;
	movie.height = movie.frame_valid ? movie.frame.details.video.height : 480;
	*width = movie.width;
	*height = movie.height;
	snprintf(message, sizeof(message), "movie: playing %s (%lux%lu%s)", path, movie.width, movie.height,
		movie.frame_valid ? "" : ", no frame yet");
	vita_host_log(message);
	return 0;
}

/* 1: a frame is waiting to be copied; 0: not yet time; -1: the movie ended */
int vita_movie_poll(void)
{
	if (!movie.open)
		return -1;
	if (movie.frame_pending)
		return 1;
	if (sceAvPlayerGetVideoData(movie.player, &movie.frame) && movie.frame.pData)
	{
		movie.frame_valid = 1;
		movie.frame_pending = 1;
		return 1;
	}
	return sceAvPlayerIsActive(movie.player) ? 0 : -1;
}

static int dump_pending;
static int row_buffered = 1;

static unsigned char clamp_byte(int value)
{
	return value < 0 ? 0 : value > 255 ? 255 : (unsigned char)value;
}

/* the waiting frame into rows of X8R8G8B8 (BT.601, limited range) */
void vita_movie_copy(void *destination, long pitch, unsigned long width, unsigned long height)
{
	const unsigned char *luma, *chroma;
	unsigned long stride, aligned_height, x, y;

	unsigned long long copy_from = vita_host_time_us();

	if (!movie.open || !movie.frame_valid)
		return;
	movie.frame_pending = 0;
	{
		/* (debug) HALO_MOVIE_DUMP=n: the n-th frame's raw NV12 and the
		converted rows to ux0:data/haloce-vita/movie_*.raw */
		static int dump_at = -2, copies;

		if (dump_at == -2)
		{
			const char *setting = getenv("HALO_MOVIE_DUMP");
			dump_at = setting ? atoi(setting) : -1;
		}
		if (dump_at >= 0 && copies++ == dump_at)
		{
			FILE *file = fopen("ux0:data/haloce-vita/movie_nv12.raw", "wb");

			if (file)
			{
				fwrite(movie.frame.pData, 1, ALIGN(movie.frame.details.video.width, 16) * ALIGN(movie.frame.details.video.height, 16) * 3 / 2, file);
				fclose(file);
			}
			dump_pending = 1;
		}
	}
	{
		/* (debug) HALO_MOVIE_ROWS=0: convert straight into the destination */
		static int setting_read;

		if (!setting_read)
		{
			const char *setting = getenv("HALO_MOVIE_ROWS");
			row_buffered = !setting || atoi(setting) != 0;
			setting_read = 1;
		}
	}
	stride = ALIGN(movie.frame.details.video.width, 16);
	aligned_height = ALIGN(movie.frame.details.video.height, 16);
	{
		/* the decoder's frame is uncached memory: read once in bulk into a
		cached copy (byte loads from it cost ~50 ms a frame) */
		static unsigned char *cached;
		static unsigned long cached_size;
		unsigned long frame_size = stride * aligned_height * 3 / 2;

		if (frame_size > cached_size)
		{
			free(cached);
			cached = memalign(64, frame_size);
			cached_size = cached ? frame_size : 0;
		}
		if (cached)
		{
			memcpy(cached, movie.frame.pData, frame_size);
			luma = cached;
		}
		else
			luma = movie.frame.pData;
	}
	chroma = luma + stride * aligned_height;
	if (width > movie.frame.details.video.width)
		width = movie.frame.details.video.width;
	if (width > 2048)
		width = 2048;
	if (height > movie.frame.details.video.height)
		height = movie.frame.details.video.height;
	for (y = 0; y < height; y++)
	{
		const unsigned char *luma_row = luma + y * stride;
		const unsigned char *chroma_row = chroma + (y / 2) * stride;
		/* (built in a cached row, then copied: the destination is the
		game's write-combined frame buffer) */
		static uint32_t row_buffer[2048];
		uint32_t *row = row_buffered ? row_buffer : (uint32_t *)((unsigned char *)destination + y * pitch);

		x = 0;
#ifdef __ARM_NEON
		/* 16 pixels at a time (the scalar loop below, in 6-bit fixed point
		with saturation): ~4x faster, the conversion was ~20 ms a frame and
		held the movies to 26 fps */
		for (; x + 16 <= width; x += 16)
		{
			uint8x8x2_t uv = vld2_u8(chroma_row + x);
			uint8x8x2_t luma_pair = vld2_u8(luma_row + x);
			int16x8_t u = vreinterpretq_s16_u16(vsubl_u8(uv.val[0], vdup_n_u8(128)));
			int16x8_t v = vreinterpretq_s16_u16(vsubl_u8(uv.val[1], vdup_n_u8(128)));
			int16x8_t red_part = vmulq_n_s16(v, 102);
			int16x8_t green_part = vmlaq_n_s16(vmulq_n_s16(u, -25), v, -52);
			int16x8_t blue_part = vmulq_n_s16(u, 129);
			uint8x8_t red[2], green[2], blue[2];
			uint8x16x4_t pixels;
			uint8x8x2_t zipped;
			int half;

			for (half = 0; half < 2; half++)
			{
				int16x8_t lum = vmulq_n_s16(vreinterpretq_s16_u16(vsubl_u8(luma_pair.val[half], vdup_n_u8(16))), 74);

				red[half] = vqrshrun_n_s16(vqaddq_s16(lum, red_part), 6);
				green[half] = vqrshrun_n_s16(vqaddq_s16(lum, green_part), 6);
				blue[half] = vqrshrun_n_s16(vqaddq_s16(lum, blue_part), 6);
			}
			/* (even and odd pixels back in order; bytes B G R A) */
			zipped = vzip_u8(blue[0], blue[1]);
			pixels.val[0] = vcombine_u8(zipped.val[0], zipped.val[1]);
			zipped = vzip_u8(green[0], green[1]);
			pixels.val[1] = vcombine_u8(zipped.val[0], zipped.val[1]);
			zipped = vzip_u8(red[0], red[1]);
			pixels.val[2] = vcombine_u8(zipped.val[0], zipped.val[1]);
			pixels.val[3] = vdupq_n_u8(0xff);
			vst4q_u8((uint8_t *)(row + x), pixels);
		}
#endif
		for (; x < width; x += 2)
		{
			/* (NV12: U then V per 2x2 block) */
			int u = chroma_row[x] - 128, v = chroma_row[x + 1] - 128;
			int red = 409 * v + 128, green = -100 * u - 208 * v + 128, blue = 516 * u + 128;
			int y0 = 298 * (luma_row[x] - 16), y1 = 298 * (luma_row[x + 1] - 16);

			row[x] = 0xff000000u | (uint32_t)clamp_byte((y0 + red) >> 8) << 16 |
				(uint32_t)clamp_byte((y0 + green) >> 8) << 8 | clamp_byte((y0 + blue) >> 8);
			row[x + 1] = 0xff000000u | (uint32_t)clamp_byte((y1 + red) >> 8) << 16 |
				(uint32_t)clamp_byte((y1 + green) >> 8) << 8 | clamp_byte((y1 + blue) >> 8);
		}
		if (row_buffered)
			memcpy((unsigned char *)destination + y * pitch, row, width * 4);
	}
	{
		static unsigned int timed;
		static unsigned long long total;

		total += vita_host_time_us() - copy_from;
		if (++timed % 120 == 0)
		{
			/* the first ten reports only: the menu's attract movie loops
			for as long as the game sits there */
			if (timed <= 1200)
			{
				char message[96];

				snprintf(message, sizeof(message), "movie: %u frames copied, %.2f ms each", timed, (double)total / 120000.0);
				vita_host_log(message);
			}
			total = 0;
		}
	}
	if (dump_pending)
	{
		FILE *file = fopen("ux0:data/haloce-vita/movie_rgb.raw", "wb");

		dump_pending = 0;
		if (file)
		{
			char message[96];

			for (y = 0; y < height; y++)
				fwrite((unsigned char *)destination + y * pitch, 4, width, file);
			fclose(file);
			snprintf(message, sizeof(message), "movie: dumped a frame (%lux%lu, pitch %ld, frame %ux%u)", width, height, pitch,
				(unsigned)movie.frame.details.video.width, (unsigned)movie.frame.details.video.height);
			vita_host_log(message);
		}
	}
}

void vita_movie_close(void)
{
	if (!movie.open)
		return;
	movie.audio_running = 0;
	sceKernelDelayThread(20000);
	if (movie.player)
	{
		sceAvPlayerStop(movie.player);
		sceAvPlayerClose(movie.player);
	}
	if (movie.audio_port >= 0)
		sceAudioOutReleasePort(movie.audio_port);
	memset(&movie, 0, sizeof(movie));
	movie.audio_port = -1;
	vita_host_log("movie: closed");
}
