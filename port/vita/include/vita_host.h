/*
VITA_HOST.H

The Vita side of the port (port/vita/host, compiled by VitaSDK's GCC with the
SDK's ABI) as the platform layer sees it (compiled by clang with the game's
ABI). Parameters are 32-bit scalars and pointers only, so both ABIs agree.
*/

#ifndef __HALO_VITA_HOST_H
#define __HALO_VITA_HOST_H

/* the contiguous memory window (platform.h), mapped for the GPU once GXM
is up; its size is stored through size */
void *vita_host_arena(unsigned long *size);

/* one line to ux0:data/haloce-vita/log.txt and the debug output */
void vita_host_log(const char *line);

/* logs the free user, CDRAM and physically contiguous memory */
void vita_host_log_memory(const char *when);

/* the calling thread onto the given core (0-2) */
void vita_host_pin_current_thread(int core);

/* a thread of its own on the given core (0-2); 0 on success */
int vita_host_thread_start(const char *name, void (*function)(void *), void *argument, int core);

/* each core's busy share of the last second, 0-100, or 255 unknown */
void vita_host_cpu_usage(unsigned char busy[3]);

/* microseconds since the process started */
unsigned long long vita_host_time_us(void);

/* the calling thread asleep for this long */
void vita_host_sleep_us(unsigned long microseconds);

/* ---------- movies (port/vita/host/vita_movie.c): the Vita's video player
for the game's Bink movies, from H.264 copies */

/* 0 on success, with the video's size */
int vita_movie_open(const char *path, unsigned long *width, unsigned long *height);
/* 1: a frame is waiting; 0: not yet; -1: the movie ended */
int vita_movie_poll(void);
/* the waiting frame as rows of X8R8G8B8 */
void vita_movie_copy(void *destination, long pitch, unsigned long width, unsigned long height);
void vita_movie_close(void);

/* ---------- the controls (port/vita/host/vita_input.c) */

/* the SCE_CTRL_* button bits */
#define VITA_BUTTON_SELECT 0x00000001UL
#define VITA_BUTTON_START 0x00000008UL
#define VITA_BUTTON_UP 0x00000010UL
#define VITA_BUTTON_RIGHT 0x00000020UL
#define VITA_BUTTON_DOWN 0x00000040UL
#define VITA_BUTTON_LEFT 0x00000080UL
#define VITA_BUTTON_L 0x00000100UL
#define VITA_BUTTON_R 0x00000200UL
#define VITA_BUTTON_TRIANGLE 0x00001000UL
#define VITA_BUTTON_CIRCLE 0x00002000UL
#define VITA_BUTTON_CROSS 0x00004000UL
#define VITA_BUTTON_SQUARE 0x00008000UL

struct vita_host_pad
{
	unsigned long buttons;
	unsigned char lx, ly, rx, ry;
};

void vita_host_pad_read(struct vita_host_pad *pad);
/* the settings panel (vita_settings.c): nonzero when it took the buttons
(open, or SELECT+START held), and the game should see none */
int vita_settings_input(const struct vita_host_pad *pad);
/* the panel's settings.txt and the release defaults into the environment */
void vita_settings_load(void);

#endif
