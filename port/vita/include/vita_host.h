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

/* a memory block of its own (user memory, outside the window), size a
multiple of 64 KB; NULL when there is no room, else its id is stored
through uid for vita_host_block_free */
void *vita_host_block_alloc(const char *name, unsigned long size, int *uid);
void vita_host_block_free(int uid);

/* the game reads a file (counted: vita_gxm.c's clean-up of old shader
caches removes files only while the game reads none) */
void vita_host_file_read_note(void);

/* one line to ux0:data/haloce-vita/log.txt and the debug output */
void vita_host_log(const char *line);

/* logs the free user, CDRAM and physically contiguous memory */
void vita_host_log_memory(const char *when);

/* the calling thread onto the given core (0-2) */
void vita_host_pin_current_thread(int core);
unsigned long vita_host_thread_id(void);

/* a thread of its own on the given core (0-2); 0 on success */
int vita_host_thread_start(const char *name, void (*function)(void *), void *argument, int core);
/* the same at a priority (64..191, lower runs first); core -1: any core */
int vita_host_thread_start_priority(const char *name, void (*function)(void *), void *argument, int core,
	int priority);

/* ---------- the fourth core (port/vita/host/vita_fourth_core.c) */

/* Fourth core helpers' level (HALO_CPU3_AUX, read at start-up): 0 off,
1 audio, 2 all async */
int vita_host_fourth_core_level(void);
/* the calling thread onto the fourth core (affinity 0x80000) if Fourth core
helpers is at `level` or above and the system allows it (a kernel plugin
such as CapUnlocker): 3 when it moved, -1 when it stays where it was;
halo.log says which, naming it `role` */
int vita_host_fourth_core_join(const char *role, int level);
/* the same whatever the setting (HALO_AUDIO_CORE=3) */
int vita_host_fourth_core_move(const char *role);
/* the calling thread's run time logged with the frame timing, as `role` */
void vita_host_thread_watch(const char *role);
/* with each frame-timing line: the watched threads' and the cores' times */
void vita_host_thread_times_report(unsigned long frames);
/* the frame-hitch line's cores and threads, sampled at each frame's end (main.c) */
int vita_host_frame_cores(char *line, int size, int describe);

/* each core's busy share of the last second, 0-100, or 255 unknown (the
fourth core's last) */
void vita_host_cpu_usage(unsigned char busy[4]);

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
/* the open movie's display shape (width / height), which an MP4 may give
apart from its size in pixels; 0 with no movie open */
float vita_movie_display_aspect(void);
/* the shape an MP4 file's video track is to be shown at
(vita_movie_aspect.c): its track header's display size, else its pixels'
aspect, else width / height; *source names which */
float vita_movie_file_aspect(const char *path, unsigned long width, unsigned long height, const char **source);
/* the shape a movie is shown at, from vita_movie_file_aspect's answer and
the player's aspect ratio (0: none): the file's, when it gives one; the
player's only when the file gives nothing but its size */
float vita_movie_choose_aspect(float file_aspect, const char **source, unsigned long width, unsigned long height,
	float player_aspect);
/* the picture size in the MP4's sample description (1), or 0 without one */
int vita_movie_file_picture_size(const char *path, unsigned long *width, unsigned long *height);
/* the part of a decoded frame that is the picture: the decoder pads it to
whole 16-pixel macroblocks (640x360 decodes to 640x368), which the file's
picture size takes off again */
void vita_movie_visible_size(unsigned long decoded_width, unsigned long decoded_height, unsigned long picture_width,
	unsigned long picture_height, unsigned long *width, unsigned long *height);
/* how far apart the rows of a decoded frame's luma plane are, in bytes,
judged from the picture (room: the bytes readable from luma on); *decided
is 0 for a picture too flat to tell, and the width padded to 16 returned */
unsigned long vita_movie_detect_pitch(const unsigned char *luma, unsigned long room, unsigned long width,
	unsigned long height, int *decided);

/* ---------- the controls (port/vita/host/vita_input.c) */

/* the SCE_CTRL_* button bits */
#define VITA_BUTTON_SELECT 0x00000001UL
/* (a paired DualShock's stick clicks: vita_ctrl_ports.h) */
#define VITA_BUTTON_L3 0x00000002UL
#define VITA_BUTTON_R3 0x00000004UL
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
	/* the touch zones held (a bit per VITA_ZONE_*, vita_controls.h) */
	unsigned long touch;
	/* the angles the Vita turned since the last read (radians about its x,
	y and z axes, filtered: vita_controls.h, gyro aiming) */
	float gyro[3];
};

void vita_host_pad_read(struct vita_host_pad *pad);
/* the gyroscope's line on the settings panel's Gyro settings page: the rates now
(degrees a second), whether the bias was learnt */
void vita_gyro_status(char *text, int size);
/* the settings panel (vita_settings.c): nonzero when it took the buttons
(open, or SELECT+START held), and the game should see none */
int vita_settings_input(const struct vita_host_pad *pad);
/* the panel's settings.txt and the release defaults into the environment */
void vita_settings_load(void);
void vita_settings_message(const char *title, const char *text);
/* the language files read (app0:lang) and the Vita's system language
chosen for the port's text, before anything is shown (lang.c) */
void vita_settings_language_init(void);
/* the Custom Edition installer's maps (vita_ce_installer.c): taken on a
thread of their own when an installer is there and they are missing (0
started, else -1); the state for Modded maps: 0 nothing to do, 1 an
installer (its file name) and maps missing, 2 taking them */
int vita_ce_installer_start(void);
int vita_ce_installer_state(char *name, int size);
/* ad hoc play (vita_net.c): joins an ad hoc group through the system's
network check dialog, on a thread of its own. mode 0 "connect" (the room's
group, made if there is none), 1 make one, 2 pick one nearby; room 1-4.
0 if it started */
int vita_adhoc_connect(int mode, int room);
void vita_adhoc_leave(void);
/* 0 not in a group, 1 joining (the dialog is up: the game should see no
buttons), 2 in a group, -1 the last attempt failed; a line saying so */
int vita_adhoc_state(char *text, int size);
/* the system's keyboard (vita_ime.c), for a line of text the settings
panel asks for (the Play page's lobby name and password): opens it over the
game with this title and text (ASCII, at most maximum_length characters;
password: the characters hidden). 0 if it opened */
int vita_ime_open(const char *title, const char *text, int maximum_length, int password);
/* while it is open, 0; once closed, 1 with the text typed in text (its
printable ASCII characters; others are left out), or -1 if it was
cancelled or failed. The game should see no buttons while it is open */
int vita_ime_poll(char *text, int size);
/* (debug) HALO_ADHOC_PROBE=1: logs what the Vita's ad hoc libraries do
(vita_net.c) */
void vita_net_adhoc_probe(void);
/* (debug) HALO_NET_SELFTEST=1: logs the socket layer's loopback and
broadcast behaviour (vita_net.c) */
void vita_net_selftest(void);

#endif
