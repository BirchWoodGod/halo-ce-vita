/*
VITA_MOVIE.C

The game's Bink movies (the intro before the first mission, the credits,
the attract videos) played by the Vita's own video player (SceAvPlayer)
from H.264 copies made once from the disc's .bik files (the Bink codec is
proprietary; ffmpeg converts it). The video frames come out in NV12 (a Y
plane, then interleaved chroma, both 16-aligned) and are converted for the
game's X8R8G8B8 movie texture; the sound goes to a BGM port from a thread
of its own. One movie at a time, as the game plays them.

Any size the decoder gives (up to 960x544, the Vita's own screen) is taken:
the frame's rows are found apart by the width padded to 16, or - for a width
that is not a multiple of 64, where the hardware decoder may pad further
(an 848x480 copy showed black on the Vita while 640 and 960 wide ones
played) - by what the picture says (vita_movie_detect_pitch), and the rows
the decoder adds to fill its last 16-pixel macroblock (640x360 decodes to
640x368) are cut by the picture size the file gives. bink_playback.c scales
the picture to fit the screen at its display shape.
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
	/* the picture's size, as the game is told it (the decoded frame less
	the decoder's padding rows) */
	unsigned long width, height;
	/* the display shape (vita_movie_display_aspect) */
	float aspect;
	/* the bytes between two of the frame's luma rows, and whether that was
	settled (vita_movie_copy) */
	unsigned long pitch;
	int pitch_settled;
	unsigned int pitch_frames;
	/* frames copied */
	unsigned int copies;
} movie;

/* the decoder's frame memory (movie_allocate_frame): how much of it can be
read from a frame's address on */
#define MAXIMUM_FRAME_BLOCKS 32
static struct
{
	unsigned long base, size;
} frame_blocks[MAXIMUM_FRAME_BLOCKS];

static unsigned long frame_room(const void *address)
{
	unsigned long index;

	for (index = 0; index < MAXIMUM_FRAME_BLOCKS; index++)
		if (frame_blocks[index].size && (unsigned long)address >= frame_blocks[index].base &&
			(unsigned long)address < frame_blocks[index].base + frame_blocks[index].size)
			return frame_blocks[index].base + frame_blocks[index].size - (unsigned long)address;
	return 0;
}

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
	unsigned long allocated = ALIGN(size, 0x100000);
	char message[128];

	(void)argument;
	if (alignment <= 0x100000)
		block = sceKernelAllocMemBlock("movie frame", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, allocated, NULL);
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
		allocated = ALIGN(size, alignment);
		block = sceKernelAllocMemBlock("movie frame", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, allocated, &options);
		snprintf(message, sizeof(message), "movie: a %u byte frame in video memory (contiguous main memory: 0x%08x): 0x%08x",
			(unsigned)size, (unsigned)first, (unsigned)block);
		vita_host_log(message);
	}
	if (block < 0)
		return NULL;
	sceKernelGetMemBlockBase(block, &base);
	{
		unsigned long index;

		for (index = 0; index < MAXIMUM_FRAME_BLOCKS; index++)
			if (!frame_blocks[index].size)
			{
				frame_blocks[index].base = (unsigned long)base;
				frame_blocks[index].size = allocated;
				break;
			}
	}
	return base;
}

static void movie_free_frame(void *argument, void *pointer)
{
	SceUID block;

	(void)argument;
	{
		unsigned long index;

		for (index = 0; index < MAXIMUM_FRAME_BLOCKS; index++)
			if (frame_blocks[index].base == (unsigned long)pointer)
				frame_blocks[index].size = 0;
	}
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
	movie.pitch = ALIGN(movie.width, 16);
	/* (a width that is a multiple of 64 is known to come as assumed: 640
	and 960 wide movies played on the Vita) */
	movie.pitch_settled = !movie.frame_valid || movie.width % 64 == 0;
	{
		/* the decoder's padding rows off (640x360 decodes to 640x368) */
		unsigned long picture_width, picture_height;

		if (vita_movie_file_picture_size(path, &picture_width, &picture_height))
			vita_movie_visible_size(movie.width, movie.height, picture_width, picture_height, &movie.width, &movie.height);
	}
	*width = movie.width;
	*height = movie.height;
	{
		/* the shape it is shown at (issue #6: movies converted 16:9 for
		another port, 640x480 pixels flagged 16:9, played 4:3): the file's
		display size, else (no track header) the player's own idea of it
		when that differs from the pixels', else the pixels'.
		HALO_MOVIE_ASPECT=<w:h or a number> forces one, e.g. 16:9 for a
		squeezed copy without the flag; "pixels" keeps the size's */
		const char *source = "its size";
		const char *setting = getenv("HALO_MOVIE_ASPECT");
		float pixels = (float)movie.width / (float)movie.height;

		movie.aspect = vita_movie_file_aspect(path, movie.width, movie.height, &source);
		movie.aspect = vita_movie_choose_aspect(movie.aspect, &source, movie.width, movie.height,
			movie.frame_valid ? movie.frame.details.video.aspectRatio : 0.0f);
		if (setting && *setting)
		{
			float forced = 0.0f;
			const char *colon = strchr(setting, ':');

			if (!strcmp(setting, "pixels"))
				forced = pixels;
			else if (colon && atof(colon + 1) > 0.0)
				forced = (float)(atof(setting) / atof(colon + 1));
			else
				forced = (float)atof(setting);
			if (forced >= 0.5f && forced <= 4.0f)
			{
				movie.aspect = forced;
				source = "HALO_MOVIE_ASPECT";
			}
		}
		char decoded[48] = "";

		if (movie.frame_valid && (movie.width != movie.frame.details.video.width || movie.height != movie.frame.details.video.height))
			snprintf(decoded, sizeof(decoded), ", decoded as %ux%u", (unsigned)movie.frame.details.video.width,
				(unsigned)movie.frame.details.video.height);
		snprintf(message, sizeof(message), "movie: playing %s (%lux%lu%s%s), shown at %.3f:1 (from %s)", path, movie.width,
			movie.height, decoded, movie.frame_valid ? "" : ", no frame yet", (double)movie.aspect, source);
	}
	vita_host_log(message);
	return 0;
}

float vita_movie_display_aspect(void)
{
	return movie.open ? movie.aspect : 0.0f;
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
	const unsigned char *luma, *chroma, *block_chroma;
	unsigned long stride, aligned_height, frame_width, room, block, x, y;
	/* (the cached copy of 16 rows, vita_movie_copy) */
	static unsigned char *cached;
	static unsigned long cached_size;

	unsigned long long copy_from = vita_host_time_us();

	if (!movie.open || !movie.frame_valid)
		return;
	movie.frame_pending = 0;
	{
		/* (debug) HALO_MOVIE_DUMP=n: the n-th frame's raw NV12 (all of the
		decoder's memory from the frame on, up to 4 MB) and the converted
		rows to ux0:data/haloce-vita/movie_*.raw */
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
				unsigned long bytes = frame_room(movie.frame.pData);

				if (!bytes)
					bytes = movie.pitch * ALIGN(movie.frame.details.video.height, 16) * 3 / 2;
				fwrite(movie.frame.pData, 1, bytes < 0x400000 ? bytes : 0x400000, file);
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
	frame_width = movie.frame.details.video.width;
	aligned_height = ALIGN(movie.frame.details.video.height, 16);
	room = frame_room(movie.frame.pData);
	if (!movie.pitch_settled)
	{
		/* (see the top: a width not a multiple of 64 has its row pitch
		found from the picture, once there is one to tell by) */
		int decided;
		unsigned long assumed = ALIGN(frame_width, 16);
		unsigned long found = vita_movie_detect_pitch(movie.frame.pData, room ? room : assumed * aligned_height * 3 / 2,
			frame_width, movie.frame.details.video.height, &decided);
		char message[160];

		if (decided || ++movie.pitch_frames >= 300)
		{
			movie.pitch = found;
			movie.pitch_settled = 1;
			snprintf(message, sizeof(message), "movie: the decoder's rows are %lu bytes apart (%s; %lu bytes of frame memory)",
				found, !decided ? "assumed: no picture to tell by" : found == assumed ? "as assumed" : "found from the picture",
				room);
			vita_host_log(message);
		}
	}
	stride = movie.pitch;
	/* (never past the decoder's memory) */
	if (room && stride * aligned_height * 3 / 2 > room)
		stride = ALIGN(frame_width, 16);
	if (width > frame_width)
		width = frame_width;
	if (width > 2048)
		width = 2048;
	if (height > movie.frame.details.video.height)
		height = movie.frame.details.video.height;
	{
		/* (the log) the first and the 90th frame's luma: a decoder giving
		empty frames shows as one value */
		if (movie.copies == 0 || movie.copies == 89)
		{
			const unsigned char *frame = movie.frame.pData;
			unsigned int low = 255, high = 0, sum = 0, count = 0;
			char message[128];

			for (y = 0; y < height; y += height / 8 ? height / 8 : 1)
				for (x = 0; x < width; x += 16)
				{
					unsigned int value = frame[y * stride + x];

					low = value < low ? value : low;
					high = value > high ? value : high;
					sum += value;
					count++;
				}
			snprintf(message, sizeof(message), "movie: frame %u luma %u..%u (mean %u), %lux%lu from %ux%u rows %lu apart",
				movie.copies + 1, low, high, count ? sum / count : 0, width, height, (unsigned)frame_width,
				(unsigned)movie.frame.details.video.height, stride);
			vita_host_log(message);
		}
		movie.copies++;
	}
	/* the decoder's frame is uncached memory: read in bulk into a cached
	copy (byte loads from it cost ~50 ms a frame), 16 rows at a time so the
	copy stays in the cache while it is converted (a whole 848x480 frame
	is more than the 512 KB L2) */
	if (16 * stride > cached_size)
	{
		free(cached);
		cached = memalign(64, 16 * stride * 3 / 2);
		cached_size = cached ? 16 * stride : 0;
	}
	chroma = (const unsigned char *)movie.frame.pData + stride * aligned_height;
	for (block = 0; block < height; block += 16)
	{
		unsigned long rows = height - block < 16 ? height - block : 16;

		if (cached)
		{
			memcpy(cached, (const unsigned char *)movie.frame.pData + block * stride, rows * stride);
			memcpy(cached + 16 * stride, chroma + (block / 2) * stride, ((rows + 1) / 2) * stride);
			luma = cached;
			block_chroma = cached + 16 * stride;
		}
		else
		{
			luma = (const unsigned char *)movie.frame.pData + block * stride;
			block_chroma = chroma + (block / 2) * stride;
		}
		for (y = block; y < block + rows; y++)
		{
			const unsigned char *luma_row = luma + (y - block) * stride;
			const unsigned char *chroma_row = block_chroma + ((y - block) / 2) * stride;
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
