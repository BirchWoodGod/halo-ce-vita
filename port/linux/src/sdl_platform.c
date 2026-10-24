/*
SDL_PLATFORM.C

The SDL3 window, OpenGL context and event loop behind the Linux build.

The window is created with the Direct3D device (d3d8_gl.c) on the game's
main thread, which is also the only thread that pumps events. Keyboard and
mouse state gathered here feeds the controller emulation in xinput_sdl.c
and the debug keyboard that the game's console reads.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "gl.h"
#include "port_config.h"
#include "p2p.h"
#include "xiso.h"
#include "ce_installer.h"
#include "posix.h"
#include "pc_text_entry.h"
#include "halo_port_limits.h"
#include "../../vita/include/vita_version.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Window *platform_window;
static SDL_GLContext platform_gl_context;
static SDL_ThreadID platform_event_thread;
static BOOL platform_sdl_started = FALSE;

static struct platform_input_state input_state;
/* keys pressed since the last read, so a press and release between two
reads still counts as a press (input injected on Android, or a slow frame) */
static unsigned char keys_pressed[SDL_SCANCODE_COUNT];
/* the same for the mouse's buttons (a click shorter than a frame fires) */
static unsigned char mouse_pressed[PLATFORM_MOUSE_BUTTON_COUNT];
#ifndef HALO_NOT_DESKTOP
/* the menus' pointer (platform_ui_pointer_set_active), under input_lock */
static struct platform_ui_pointer ui_pointer;
static float ui_pointer_wheel;
#endif
static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;

/* debug keyboard queue */
#define KEYSTROKE_QUEUE_SIZE 64
static struct platform_keystroke keystroke_queue[KEYSTROKE_QUEUE_SIZE];
static unsigned long keystroke_head, keystroke_count;

#ifndef HALO_NOT_DESKTOP
/* updater.c's: the desktop self-updater */
void updater_start(void);
void updater_poll(SDL_Window *window);
#endif
#if !defined(HALO_NOT_DESKTOP) && defined(__linux__)
static void platform_ce_installer(void);
#endif

#ifndef HALO_NOT_DESKTOP
#include "../include/halo_button_icons.h"

/* the device the player used last, for the button icons on auto
(port/linux/include/halo_button_icons.h, button_glyphs.c): a key, a mouse
button, the wheel or the mouse moved, or a gamepad's button, trigger or
stick pushed well past its rest; -1 before any (a gamepad there at the
start counts, else the keyboard) */
static volatile int input_device_last = -1;

/* (sticks and triggers: past half, so that a resting stick's drift never
takes the icons from the keyboard) */
#define INPUT_DEVICE_AXIS_THRESHOLD 16000

static int gamepad_input_device(SDL_JoystickID id)
{
	SDL_Gamepad *gamepad = SDL_GetGamepadFromID(id);

	switch (gamepad ? SDL_GetGamepadType(gamepad) : SDL_GAMEPAD_TYPE_UNKNOWN)
	{
	case SDL_GAMEPAD_TYPE_PS3:
	case SDL_GAMEPAD_TYPE_PS4:
	case SDL_GAMEPAD_TYPE_PS5:
		return _halo_input_device_playstation_gamepad;
	default:
		return _halo_input_device_xbox_gamepad;
	}
}

void platform_input_device_used(int device)
{
	static char const *const names[] = { "the keyboard and mouse", "an Xbox gamepad", "a PlayStation gamepad" };

	if (device != input_device_last && device >= 0 && device <= _halo_input_device_playstation_gamepad)
	{
		platform_log("input: last used %s", names[device]);
		input_device_last = device;
	}
}

int platform_input_device_last(void)
{
	int device = input_device_last;

	return device < 0 ? _halo_input_device_keyboard : device;
}
#endif

/* the window's title: the PC build of the Vita port says so */
#if defined(HALO_PORT_VITA_NETWORK) && !defined(HALO_VITA)
#define PLATFORM_WINDOW_TITLE "Halo CE Vita (PC build)"
#else
#define PLATFORM_WINDOW_TITLE "Halo"
#endif

BOOL platform_sdl_initialize(void)
{
	if (platform_sdl_started)
		return TRUE;
	/* a copy of the game started to open an invite link hands it to the
	one already running, and goes */
	if (p2p_hand_off_invite())
		exit(EXIT_SUCCESS);
	SDL_SetHint(SDL_HINT_APP_NAME, "Halo");
#ifdef HALO_ANDROID
	/* landscape only; the back key arrives as a key event (xinput_sdl.c)
	instead of closing the activity */
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
	/* touching the screen must not aim or fire (the mouse drives the
	controller emulation in xinput_sdl.c) */
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	/* (GXM owns the display: port/vita/host/vita_gxm.c) */
	if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
#else
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
#endif
	{
		platform_log("SDL_Init failed: %s", SDL_GetError());
		return FALSE;
	}
	platform_sdl_started = TRUE;
#if defined(HALO_PORT_VITA_NETWORK) && !defined(HALO_VITA) && !defined(HALO_DEDICATED_SERVER)
	/* (the PC build of the Vita port: which one a report came from) */
	platform_log("Halo CE Vita " HALO_VITA_VERSION_SHOWN " starting");
#endif
#ifndef HALO_NOT_DESKTOP
	/* found (or offered to the player, platform_offer_game_data) before the
	game's window opens */
	platform_data_root();
#ifdef __linux__
	platform_ce_installer();
#endif
	/* (a new version looked for meanwhile, updater_poll asking about it) */
	updater_start();
#endif
	return TRUE;
}

#if !defined(HALO_NOT_DESKTOP) && defined(__linux__)
/* ---------- the Custom Edition installer (posix_ce_installer.c) */

static int platform_ce_installer_progress(void *context, const char *file, unsigned long long done,
	unsigned long long total)
{
	static Uint64 logged;
	Uint64 now = SDL_GetTicks();

	(void)context;
	if (file[0] && (!logged || now - logged >= 2000))
	{
		platform_log("Custom Edition installer: %s, %llu%%", file, total ? done * 100 / total : 100ULL);
		logged = now;
	}
	return 0;
}

/* a Halo Custom Edition installer (halocesetup*.exe) in the data root or its
maps folder: the resource maps missing from the maps folder (bitmaps.map,
sounds.map, loc.map) taken from it before the game starts, so its first
ui.map has the PC menus (the Vita does it on a thread of its own:
port/vita/host/vita_ce_installer.c) */
static void platform_ce_installer(void)
{
	const char *root = platform_data_root(), *maps_root = getenv("HALO_MAPS_ROOT");
	char maps[1024], on_disk[256], installer[1024], error[512];
	unsigned long long size;
	int missing, result;
	Uint64 started;

	if (maps_root && *maps_root)
		snprintf(maps, sizeof(maps), "%s", maps_root);
	else if (posix_find_entry_case_insensitive(root, "maps", on_disk, sizeof(on_disk)))
		snprintf(maps, sizeof(maps), "%s/%s", root, on_disk);
	else
		return;
	missing = ce_installer_missing(maps);
	if (!missing || (!ce_installer_find(root, installer, sizeof(installer), &size) &&
		!ce_installer_find(maps, installer, sizeof(installer), &size)))
		return;
	platform_log("Custom Edition installer: taking the resource maps missing from %s out of %s", maps, installer);
	started = SDL_GetTicks();
	result = ce_installer_extract(installer, maps, missing, platform_ce_installer_progress, NULL, error, sizeof(error));
	if (result == CE_INSTALLER_DONE)
		platform_log("Custom Edition installer: bitmaps.map, sounds.map and loc.map in %s in %llu ms; the installer "
			"can be deleted", maps, (unsigned long long)(SDL_GetTicks() - started));
	else
		platform_log("Custom Edition installer: %s", error);
}
#endif

#ifndef HALO_NOT_DESKTOP
/* ---------- first start without game data (xbox_files.c) */

struct data_extraction
{
	pthread_mutex_t lock;
	char image[1024];
	char destination[1024];
	char file[256];
	unsigned long long done;
	unsigned long long total;
	BOOL finished;
	BOOL succeeded;
	char error[512];
};

static void data_extraction_progress(void *context, const char *file, unsigned long long done,
	unsigned long long total)
{
	struct data_extraction *extraction = context;

	pthread_mutex_lock(&extraction->lock);
	snprintf(extraction->file, sizeof(extraction->file), "%s", file);
	extraction->done = done;
	extraction->total = total;
	pthread_mutex_unlock(&extraction->lock);
}

static void *data_extraction_thread(void *context)
{
	struct data_extraction *extraction = context;
	BOOL succeeded = xiso_extract_maps(extraction->image, extraction->destination, data_extraction_progress,
		extraction, extraction->error, sizeof(extraction->error)) != 0;

	pthread_mutex_lock(&extraction->lock);
	extraction->succeeded = succeeded;
	extraction->finished = TRUE;
	pthread_mutex_unlock(&extraction->lock);
	return NULL;
}

/* copies the maps, showing how far it has got; closing the window quits */
static BOOL data_extract(const char *image, const char *destination, char *error, int error_size)
{
	static struct data_extraction extraction;
	SDL_Window *window;
	SDL_Renderer *renderer = NULL;
	pthread_t thread;
	BOOL finished = FALSE;

	memset(&extraction, 0, sizeof(extraction));
	pthread_mutex_init(&extraction.lock, NULL);
	snprintf(extraction.image, sizeof(extraction.image), "%s", image);
	snprintf(extraction.destination, sizeof(extraction.destination), "%s", destination);
	if (pthread_create(&thread, NULL, data_extraction_thread, &extraction) != 0)
	{
		snprintf(error, (size_t)error_size, "Could not start the extraction.");
		return FALSE;
	}
	/* (waited for through extraction.finished; the Windows port's threads
	cannot be joined) */
	pthread_detach(thread);
	window = SDL_CreateWindow("Halo", 640, 150, 0);
	if (window)
	{
		renderer = SDL_CreateRenderer(window, NULL);
		if (renderer)
			SDL_SetRenderVSync(renderer, 1);
	}
	while (!finished)
	{
		SDL_Event event;
		char file[256];
		unsigned long long done, total;

		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
			{
				platform_log("extraction cancelled");
				exit(EXIT_SUCCESS);
			}
		}
		pthread_mutex_lock(&extraction.lock);
		finished = extraction.finished;
		snprintf(file, sizeof(file), "%s", extraction.file);
		done = extraction.done;
		total = extraction.total;
		pthread_mutex_unlock(&extraction.lock);
		if (renderer)
		{
			char line[320];
			SDL_FRect bar = { 20.0f, 100.0f, 600.0f, 24.0f };
			float fraction = total ? (float)((double)done / (double)total) : 0.0f;

			SDL_SetRenderDrawColor(renderer, 12, 16, 20, 255);
			SDL_RenderClear(renderer);
			SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
			SDL_SetRenderScale(renderer, 2.0f, 2.0f);
			SDL_RenderDebugText(renderer, 10.0f, 10.0f, "Extracting the maps folder...");
			SDL_SetRenderScale(renderer, 1.0f, 1.0f);
			snprintf(line, sizeof(line), "%s  (%llu of %llu MB)", file, done >> 20, total >> 20);
			SDL_RenderDebugText(renderer, 20.0f, 70.0f, line);
			SDL_SetRenderDrawColor(renderer, 60, 66, 72, 255);
			SDL_RenderFillRect(renderer, &bar);
			bar.w *= fraction;
			SDL_SetRenderDrawColor(renderer, 90, 160, 90, 255);
			SDL_RenderFillRect(renderer, &bar);
			SDL_RenderPresent(renderer);
		}
		SDL_Delay(16);
	}
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (window)
		SDL_DestroyWindow(window);
	if (!extraction.succeeded)
		snprintf(error, (size_t)error_size, "%s", extraction.error);
	return extraction.succeeded;
}

struct data_image_choice
{
	SDL_AtomicInt done;
	char path[1024];
};

static void SDLCALL data_image_chosen(void *userdata, const char * const *files, int filter)
{
	struct data_image_choice *choice = userdata;

	(void)filter;
	if (files && files[0])
		snprintf(choice->path, sizeof(choice->path), "%s", files[0]);
	SDL_SetAtomicInt(&choice->done, 1);
}

/* the disc image the player picks; FALSE if they pick none */
static BOOL data_choose_image(char *path, int size)
{
	static const SDL_DialogFileFilter filters[] =
	{
		{ "Xbox disc images", "iso;xiso" },
		{ "All files", "*" },
	};
	static struct data_image_choice choice;

	memset(&choice, 0, sizeof(choice));
	SDL_ShowOpenFileDialog(data_image_chosen, &choice, NULL, filters, 2, NULL, false);
	/* the dialog answers through events (and on some systems another
	thread) */
	while (!SDL_GetAtomicInt(&choice.done))
		SDL_WaitEventTimeout(NULL, 50);
	if (!choice.path[0])
		return FALSE;
	snprintf(path, (size_t)size, "%s", choice.path);
	return TRUE;
}

BOOL platform_offer_game_data(const char *destination)
{
	static const SDL_MessageBoxButtonData buttons[] =
	{
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
	};
	char message[1400];

	/* not for runs nobody is watching */
	if (config_boolean("debug.hidden_window") || config_real("debug.exit_after") > 0.0 ||
		!SDL_Init(SDL_INIT_VIDEO))
	{
		return FALSE;
	}
	snprintf(message, sizeof(message),
		"Halo's game data (its maps folder) was not found.\n\n"
		"Extract the maps folder from an Xbox disc image (.iso) of Halo: Combat Evolved? "
		"It is copied to %s/maps (about 2 GB).\n\n"
		"(Or put the maps folder there yourself, or set paths.data in config.toml.)",
		destination);
	for (;;)
	{
		SDL_MessageBoxData question = { SDL_MESSAGEBOX_INFORMATION, NULL, "Halo", message, 2, buttons, NULL };
		char image[1024];
		char error[512];
		int answer = 0;

		if (!SDL_ShowMessageBox(&question, &answer) || answer != 1)
		{
			platform_log("no game data: quitting");
			exit(EXIT_SUCCESS);
		}
		/* no image picked: ask again */
		if (!data_choose_image(image, sizeof(image)))
			continue;
		platform_log("extracting the maps folder from %s to %s", image, destination);
		if (data_extract(image, destination, error, sizeof(error)))
		{
			platform_log("extracted the maps folder");
			return TRUE;
		}
		platform_log("extraction failed: %s", error);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo", error, NULL);
	}
}
#endif

/* (frames between ticks: port/linux/game/render_interpolation.c) the
setting as the frame being made has it: read again only as a frame starts
(halo_interpolation_latch), so the tick on its thread and the frame drawn
alongside it see the same */
static int interpolation_enabled = -1;

int halo_interpolation_enabled(void)
{
	if (interpolation_enabled < 0)
		interpolation_enabled = config_boolean("display.interpolation");
	return interpolation_enabled;
}

/* main.c, as a frame starts (no tick running): the Vita's settings panel
changes HALO_INTERPOLATION mid-game, and takes effect at once */
void halo_interpolation_latch(void)
{
	extern volatile unsigned long halo_settings_generation;
	static unsigned long settings_seen;

	if (interpolation_enabled < 0)
	{
		settings_seen = halo_settings_generation;
		interpolation_enabled = config_boolean("display.interpolation");
	}
	else if (settings_seen != halo_settings_generation)
	{
		const char *setting = getenv("HALO_INTERPOLATION");
		int enabled = setting && (!strcmp(setting, "true") || !strcmp(setting, "1"));

		settings_seen = halo_settings_generation;
		if (setting && enabled != interpolation_enabled)
		{
			platform_log("frame interpolation %s (settings panel)", enabled ? "on" : "off");
			interpolation_enabled = enabled;
		}
	}
}

#if !defined(HALO_NOT_DESKTOP) && !defined(HALO_GXM_NULL)
/* whether the window opens fullscreen (display.fullscreen), never when it
is hidden */
static BOOL platform_fullscreen_setting(void)
{
	return !config_boolean("debug.hidden_window") && config_boolean("display.fullscreen");
}

/* whether the game is, or is to be, fullscreen, and if so the size in
pixels of the display it fills (d3d8_gl.c draws at that resolution) */
BOOL platform_screen_mode(long *width, long *height)
{
	SDL_DisplayID display;
	const SDL_DisplayMode *mode;

	if (platform_window ? !(SDL_GetWindowFlags(platform_window) & SDL_WINDOW_FULLSCREEN) :
		!platform_fullscreen_setting() || !platform_sdl_initialize())
	{
		return FALSE;
	}
	display = platform_window ? SDL_GetDisplayForWindow(platform_window) : SDL_GetPrimaryDisplay();
	mode = display ? SDL_GetDesktopDisplayMode(display) : NULL;
	if (!mode)
		return FALSE;
	*width = (long)(mode->w * mode->pixel_density + 0.5f);
	*height = (long)(mode->h * mode->pixel_density + 0.5f);
	return TRUE;
}

#endif
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
/* SDL for the controls and sound, on the thread that pumps its events
(port/vita/platform/vita_video.c calls this as the display comes up) */
BOOL platform_vita_sdl_initialize(void)
{
	if (!platform_sdl_initialize())
		return FALSE;
	platform_event_thread = SDL_GetCurrentThreadID();
	return TRUE;
}
#else
/* (the Vita draws with GXM: port/vita/platform/vita_video.c) */
BOOL platform_video_initialize(unsigned long width, unsigned long height)
{
	int scale = (int)config_integer("display.window_scale");
	int version;

	if (platform_window)
		return TRUE;
	if (!platform_sdl_initialize())
		return FALSE;
	if (scale < 1)
		scale = 1;

#ifdef HALO_ANDROID
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
	if (config_boolean("debug.gl_debug"))
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#if !defined(HALO_ANDROID) && !defined(_WIN32)
	/* Mesa's GL thread: the renderer makes thousands of GL calls a frame
	and never waits for their results, so handing them to a thread of
	their own takes a fifth of the main thread's time off it. It leaves an
	explicit mesa_glthread setting alone and other drivers ignore it. */
	setenv("mesa_glthread", "true", 0);
#endif

#ifdef HALO_ANDROID
	platform_window = SDL_CreateWindow("Halo", (int)(width * scale), (int)(height * scale),
		SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
#else
	/* fullscreen at the desktop's resolution unless display.fullscreen is
	false, where the game draws the display's shape at its resolution
	(d3d8_gl.c); the window size is the windowed mode F11 switches to and
	from, where it draws 640x480 */
	platform_window = SDL_CreateWindow(PLATFORM_WINDOW_TITLE, (int)(width * scale), (int)(height * scale),
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
		(config_boolean("debug.hidden_window") ? SDL_WINDOW_HIDDEN : 0) |
		(platform_fullscreen_setting() ? SDL_WINDOW_FULLSCREEN : 0));
#endif
	if (!platform_window)
	{
		platform_log("SDL_CreateWindow failed: %s", SDL_GetError());
		return FALSE;
	}
	platform_gl_context = SDL_GL_CreateContext(platform_window);
#ifdef HALO_ANDROID
	/* ES 3.2 where the driver has it, otherwise the renderer makes do with
	3.0 plus extensions */
	if (!platform_gl_context)
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		platform_gl_context = SDL_GL_CreateContext(platform_window);
	}
#endif
	if (!platform_gl_context)
	{
		platform_log("cannot create an OpenGL context: %s", SDL_GetError());
		return FALSE;
	}
	SDL_GL_MakeCurrent(platform_window, platform_gl_context);
	if (!gl_functions_load())
		return FALSE;
	version = SDL_GL_SetSwapInterval(config_boolean("display.vsync") ? 1 : 0);
	(void)version;
	platform_event_thread = SDL_GetCurrentThreadID();
	platform_log("OpenGL %s on %s", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));
#ifndef HALO_NOT_DESKTOP
	platform_mouse_capture(TRUE);
#endif
	return TRUE;
}

void platform_video_drawable_size(int *width, int *height)
{
	SDL_GetWindowSizeInPixels(platform_window, width, height);
}

void platform_video_swap(void)
{
	SDL_GL_SwapWindow(platform_window);
}
#endif

/* Mouse aim needs SDL's relative mode: the cursor hidden and held, and
each motion's distance reported however far the mouse goes. Some SDL builds
cannot give it: on X11 it rests on XInput2, and the 32-bit SDL the Linux
package ships was built without it (no 32-bit libXi), so the mode failed
and the cursor stayed free: the view turned only until the cursor met the
screen's edge, then not at all (the owner's report, insider.17: "I can't look
around"). There the pointer is held here instead: hidden, kept in the
window (SDL's mouse grab, which needs no XInput2) and put back at its centre
whenever it strays from it, the motions between still counted as they
come (mouse_emulation_recenter). */
#ifndef HALO_NOT_DESKTOP
static BOOL mouse_emulating;

static BOOL mouse_relative_unavailable(void)
{
#ifndef HALO_RELEASE
	/* (tests only) HALO_TEST_NO_RELATIVE_MOUSE=1: as an SDL without the
	mode (run_mouse_look_test.sh) */
	const char *test = getenv("HALO_TEST_NO_RELATIVE_MOUSE");

	if (test && atoi(test))
		return TRUE;
#endif
	return FALSE;
}

static void mouse_emulation(BOOL on)
{
	static BOOL said;

	if (on == mouse_emulating)
		return;
	mouse_emulating = on;
	SDL_SetWindowMouseGrab(platform_window, on ? true : false);
	if (on)
	{
		int width, height;

		if (!said)
		{
			said = TRUE;
			platform_log("mouse: SDL's relative mode is not available (%s): the pointer is held at the window's "
				"centre instead", mouse_relative_unavailable() ? "the tests' HALO_TEST_NO_RELATIVE_MOUSE" : SDL_GetError());
		}
		SDL_HideCursor();
		SDL_GetWindowSize(platform_window, &width, &height);
		SDL_WarpMouseInWindow(platform_window, width * 0.5f, height * 0.5f);
	}
	else
	{
		SDL_ShowCursor();
	}
}

/* (the event loop, a motion while held) the pointer back at the window's
centre once it is a quarter of the window from it, before it can reach an
edge; the warp's own motion SDL reports as none */
static void mouse_emulation_recenter(float x, float y)
{
	int width, height;

	if (!mouse_emulating || SDL_GetKeyboardFocus() != platform_window)
		return;
	SDL_GetWindowSize(platform_window, &width, &height);
	if (SDL_fabsf(x - width * 0.5f) > width * 0.25f || SDL_fabsf(y - height * 0.5f) > height * 0.25f)
		SDL_WarpMouseInWindow(platform_window, width * 0.5f, height * 0.5f);
}

void platform_mouse_capture(BOOL capture)
{
	if (!platform_window)
		return;
	if (!capture)
	{
		mouse_emulation(FALSE);
		SDL_SetWindowRelativeMouseMode(platform_window, false);
		return;
	}
	/* (set again, not left as it was: SDL tries the mode only when the
	window has the keyboard's focus, and keeps asking for it when it did
	not get it - the window takes the focus later - without saying it
	failed; this is called again as the focus comes) */
	SDL_SetWindowRelativeMouseMode(platform_window, false);
	if (mouse_relative_unavailable() ||
		(!SDL_SetWindowRelativeMouseMode(platform_window, true) && SDL_GetKeyboardFocus() == platform_window))
	{
		SDL_SetWindowRelativeMouseMode(platform_window, false);
		mouse_emulation(TRUE);
	}
	else
	{
		mouse_emulation(FALSE);
	}
}
#else
void platform_mouse_capture(BOOL capture)
{
	if (platform_window)
		SDL_SetWindowRelativeMouseMode(platform_window, capture ? true : false);
}
#endif

/* ---------- keyboard translation */

/* Windows virtual key code for an SDL scancode (the Xbox debug keyboard
reports virtual keys) */
static BYTE virtual_key_from_scancode(SDL_Scancode scancode)
{
	if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
		return (BYTE)('A' + (scancode - SDL_SCANCODE_A));
	if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
		return (BYTE)('1' + (scancode - SDL_SCANCODE_1));
	if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12)
		return (BYTE)(0x70 + (scancode - SDL_SCANCODE_F1));
	if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9)
		return (BYTE)(0x61 + (scancode - SDL_SCANCODE_KP_1));
	switch (scancode)
	{
	case SDL_SCANCODE_0: return '0';
	case SDL_SCANCODE_KP_0: return 0x60;
	case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x0d;
	case SDL_SCANCODE_ESCAPE: return 0x1b;
	case SDL_SCANCODE_BACKSPACE: return 0x08;
	case SDL_SCANCODE_TAB: return 0x09;
	case SDL_SCANCODE_SPACE: return 0x20;
	case SDL_SCANCODE_MINUS: return 0xbd;
	case SDL_SCANCODE_EQUALS: return 0xbb;
	case SDL_SCANCODE_LEFTBRACKET: return 0xdb;
	case SDL_SCANCODE_RIGHTBRACKET: return 0xdd;
	case SDL_SCANCODE_BACKSLASH: return 0xdc;
	case SDL_SCANCODE_SEMICOLON: return 0xba;
	case SDL_SCANCODE_APOSTROPHE: return 0xde;
	case SDL_SCANCODE_GRAVE: return 0xc0;
	case SDL_SCANCODE_COMMA: return 0xbc;
	case SDL_SCANCODE_PERIOD: return 0xbe;
	case SDL_SCANCODE_SLASH: return 0xbf;
	case SDL_SCANCODE_CAPSLOCK: return 0x14;
	case SDL_SCANCODE_PRINTSCREEN: return 0x2c;
	case SDL_SCANCODE_SCROLLLOCK: return 0x91;
	case SDL_SCANCODE_PAUSE: return 0x13;
	case SDL_SCANCODE_INSERT: return 0x2d;
	case SDL_SCANCODE_HOME: return 0x24;
	case SDL_SCANCODE_PAGEUP: return 0x21;
	case SDL_SCANCODE_DELETE: return 0x2e;
	case SDL_SCANCODE_END: return 0x23;
	case SDL_SCANCODE_PAGEDOWN: return 0x22;
	case SDL_SCANCODE_RIGHT: return 0x27;
	case SDL_SCANCODE_LEFT: return 0x25;
	case SDL_SCANCODE_DOWN: return 0x28;
	case SDL_SCANCODE_UP: return 0x26;
	case SDL_SCANCODE_NUMLOCKCLEAR: return 0x90;
	case SDL_SCANCODE_KP_DIVIDE: return 0x6f;
	case SDL_SCANCODE_KP_MULTIPLY: return 0x6a;
	case SDL_SCANCODE_KP_MINUS: return 0x6d;
	case SDL_SCANCODE_KP_PLUS: return 0x6b;
	case SDL_SCANCODE_KP_PERIOD: return 0x6e;
	case SDL_SCANCODE_LCTRL: return 0xa2;
	case SDL_SCANCODE_RCTRL: return 0xa3;
	case SDL_SCANCODE_LSHIFT: return 0xa0;
	case SDL_SCANCODE_RSHIFT: return 0xa1;
	case SDL_SCANCODE_LALT: return 0xa4;
	case SDL_SCANCODE_RALT: return 0xa5;
	default: return 0;
	}
}

static CHAR ascii_from_key(SDL_Keycode key, SDL_Keymod modifiers)
{
	BOOL shift = (modifiers & SDL_KMOD_SHIFT) != 0;
	static const char shifted_digits[] = ")!@#$%^&*(";

	if (key >= 'a' && key <= 'z')
		return (CHAR)((shift ^ ((modifiers & SDL_KMOD_CAPS) != 0)) ? key - 32 : key);
	if (key >= '0' && key <= '9')
		return (CHAR)(shift ? shifted_digits[key - '0'] : key);
	if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
		return '\r';
	if (key == SDLK_BACKSPACE)
		return '\b';
	if (key == SDLK_TAB)
		return '\t';
	if (key == SDLK_ESCAPE)
		return 0x1b;
	if (key >= 32 && key < 127)
	{
		if (!shift)
			return (CHAR)key;
		switch (key)
		{
		case '-': return '_';
		case '=': return '+';
		case '[': return '{';
		case ']': return '}';
		case '\\': return '|';
		case ';': return ':';
		case '\'': return '"';
		case ',': return '<';
		case '.': return '>';
		case '/': return '?';
		case '`': return '~';
		default: return (CHAR)key;
		}
	}
	return 0;
}

static void queue_keystroke(const SDL_KeyboardEvent *event)
{
	struct platform_keystroke *keystroke;
	BYTE flags = 0;

	if (keystroke_count == KEYSTROKE_QUEUE_SIZE)
	{
		keystroke_head = (keystroke_head + 1) % KEYSTROKE_QUEUE_SIZE;
		keystroke_count--;
	}
	keystroke = &keystroke_queue[(keystroke_head + keystroke_count) % KEYSTROKE_QUEUE_SIZE];
	if (event->mod & SDL_KMOD_CTRL) flags |= 0x01;
	if (event->mod & SDL_KMOD_SHIFT) flags |= 0x02;
	if (event->mod & SDL_KMOD_ALT) flags |= 0x04;
	if (event->mod & SDL_KMOD_CAPS) flags |= 0x08;
	if (event->mod & SDL_KMOD_NUM) flags |= 0x10;
	if (!event->down) flags |= 0x40;
	if (event->repeat) flags |= 0x80;
	keystroke->virtual_key = virtual_key_from_scancode(event->scancode);
	keystroke->ascii = event->down ? ascii_from_key(event->key, event->mod) : 0;
	keystroke->flags = flags;
	keystroke_count++;
}

BOOL platform_next_keystroke(struct platform_keystroke *keystroke)
{
	BOOL result = FALSE;

	pthread_mutex_lock(&input_lock);
	if (keystroke_count)
	{
		*keystroke = keystroke_queue[keystroke_head];
		keystroke_head = (keystroke_head + 1) % KEYSTROKE_QUEUE_SIZE;
		keystroke_count--;
		result = TRUE;
	}
	pthread_mutex_unlock(&input_lock);
	return result;
}

/* ---------- internet play's invite links (p2p.c) */

#ifdef HALO_ANDROID
/* SDL declares it for Android builds only, which the guest is not
(guest/runtime/guest_sdl.c passes it to the host) */
bool SDL_ShowAndroidToast(const char *message, int duration, int gravity, int xoffset, int yoffset);
#endif

/* puts a new invite on the clipboard, and joins one found there when the
game comes to the front */
static void platform_invite_clipboard(BOOL look)
{
	/* the last clipboard text looked at, so each invite is joined once */
	static char seen[256];
	const char *invite = p2p_take_clipboard_text();

	if (invite)
	{
#ifdef HALO_VITA
		/* There is no desktop clipboard on the Vita. Export the invite
		to a file VitaShell can show or transfer to another player. */
		char path[512];
		FILE *file;

		snprintf(path, sizeof(path), "ux0:data/haloce-vita/host_invite.txt");
		file = fopen(path, "w");
		if (file)
		{
			fprintf(file, "%s\n", invite);
			if (fclose(file) == 0)
				platform_log("Internet play: invite written to %s", path);
			else
				platform_log("Internet play: could not finish writing %s", path);
		}
		else
			platform_log("Internet play: could not write %s", path);
#else
		SDL_SetClipboardText(invite);
#endif
		snprintf(seen, sizeof(seen), "%s", invite);
#ifndef HALO_VITA
		platform_log("Internet play: the invite link is on the clipboard");
#endif
#ifdef HALO_ANDROID
		SDL_ShowAndroidToast("Hosting: the invite link is on the clipboard", 1, -1, 0, 0);
#endif
	}
	if (look && config_boolean("network.join_from_clipboard"))
	{
		char *text = SDL_GetClipboardText();

		if (text && strcmp(text, seen) && strlen(text) < sizeof(seen))
		{
			snprintf(seen, sizeof(seen), "%s", text);
			if (p2p_join_invite(text))
			{
#ifdef HALO_ANDROID
				SDL_ShowAndroidToast("Joining the invite on the clipboard", 1, -1, 0, 0);
#endif
			}
		}
		SDL_free(text);
	}
}

/* ---------- messages for the player */

/* a message waiting for the event pump to show it (on the window's thread,
between frames) */
static pthread_mutex_t platform_message_lock = PTHREAD_MUTEX_INITIALIZER;
static char platform_message_title[80];
static char platform_message_text[600];
static BOOL platform_message_pending;

/* (Menu style Halo 3: port/linux/game/menu_functions.c h3_dialog_port_hook)
the game's own Halo 3 dialog takes the player's messages and questions while
it can show them: 1 taken - a question's answer then comes from
platform_question_answered -, 0 the platform's own box. A question withdrawn
is told with text NULL */
int (*platform_dialog_hook)(int question, const char *title, const char *text);

void platform_show_message_box(const char *title, const char *message);

/* shows the player a message in a box of its own (the network code's: a
host of another version), and logs it */
void platform_show_message(const char *title, const char *message)
{
	platform_log("%s: %s", title, message);
	if (platform_dialog_hook && message && platform_dialog_hook(0, title, message))
		return;
	platform_show_message_box(title, message);
}

/* (platform_show_message's, and the game's when its dialog cannot show it
after all) the platform's own box */
void platform_show_message_box(const char *title, const char *message)
{
	/* (a run nobody watches: the log only) */
	if (config_boolean("debug.hidden_window") || config_boolean("debug.null_renderer"))
		return;
	pthread_mutex_lock(&platform_message_lock);
	snprintf(platform_message_title, sizeof(platform_message_title), "%s", title);
	snprintf(platform_message_text, sizeof(platform_message_text), "%s", message);
	platform_message_pending = TRUE;
	pthread_mutex_unlock(&platform_message_lock);
}

/* ---------- a question and a progress line for the player (map sharing:
port/linux/game/map_share.c) */

static char platform_question_title[80];
static char platform_question_text[600];
/* shown (pending for the event pump), and its answer: -1 none yet, 0 no,
1 yes */
static BOOL platform_question_pending;
static int platform_question_result = -1;
/* a question asked and neither answered nor withdrawn
(platform_question_asking: one more waits its turn) */
static BOOL platform_question_open;
/* the open question is the game's dialog's (platform_dialog_hook), its
answer platform_question_answered's */
static BOOL platform_question_by_game;

void platform_ask_question_box(const char *title, const char *text);

/* asks the player a yes or no question (NULL withdraws one); the answer
comes later, platform_question_answer. A run nobody watches answers no. */
void platform_ask_question(const char *title, const char *text)
{
	if (!text)
	{
		BOOL by_game;

		pthread_mutex_lock(&platform_message_lock);
		by_game = platform_question_by_game;
		platform_question_by_game = FALSE;
		pthread_mutex_unlock(&platform_message_lock);
		if (by_game && platform_dialog_hook)
			platform_dialog_hook(1, NULL, NULL);
#ifdef HALO_VITA
		{
			extern void vita_settings_question(const char *title, const char *text);
			vita_settings_question(NULL, NULL);
		}
#endif
		pthread_mutex_lock(&platform_message_lock);
		platform_question_pending = FALSE;
		platform_question_result = -1;
		platform_question_open = FALSE;
		pthread_mutex_unlock(&platform_message_lock);
		return;
	}
	platform_log("%s: %s (yes or no?)", title, text);
	if (platform_dialog_hook && platform_dialog_hook(1, title, text))
	{
		pthread_mutex_lock(&platform_message_lock);
		platform_question_pending = FALSE;
		platform_question_result = -1;
		platform_question_open = TRUE;
		platform_question_by_game = TRUE;
		pthread_mutex_unlock(&platform_message_lock);
		return;
	}
	platform_ask_question_box(title, text);
}

/* (the game's dialog) the open question's answer: 1 yes, 0 no */
void platform_question_answered(int yes)
{
	pthread_mutex_lock(&platform_message_lock);
	if (platform_question_open && platform_question_by_game)
		platform_question_result = yes ? 1 : 0;
	pthread_mutex_unlock(&platform_message_lock);
}

/* (platform_ask_question's, and the game's when its dialog cannot ask it
after all) the platform's own box */
void platform_ask_question_box(const char *title, const char *text)
{
	pthread_mutex_lock(&platform_message_lock);
	platform_question_by_game = FALSE;
	pthread_mutex_unlock(&platform_message_lock);
#ifdef HALO_VITA
	{
		extern void vita_settings_question(const char *title, const char *text);

		pthread_mutex_lock(&platform_message_lock);
		platform_question_result = -1;
		platform_question_open = TRUE;
		pthread_mutex_unlock(&platform_message_lock);
		vita_settings_question(title, text);
	}
#else
	pthread_mutex_lock(&platform_message_lock);
	if (config_boolean("debug.hidden_window") || config_boolean("debug.null_renderer"))
	{
		platform_question_pending = FALSE;
		platform_question_result = 0;
	}
	else
	{
		snprintf(platform_question_title, sizeof(platform_question_title), "%s", title);
		snprintf(platform_question_text, sizeof(platform_question_text), "%s", text);
		platform_question_pending = TRUE;
		platform_question_result = -1;
		platform_question_open = TRUE;
	}
	pthread_mutex_unlock(&platform_message_lock);
#endif
}

int platform_question_answer(void)
{
	int result;
#ifdef HALO_VITA
	extern int vita_settings_question_answer(void);

	pthread_mutex_lock(&platform_message_lock);
	if (platform_question_by_game)
		result = platform_question_result;
	else
	{
		pthread_mutex_unlock(&platform_message_lock);
		result = vita_settings_question_answer();
		pthread_mutex_lock(&platform_message_lock);
	}
#else
	pthread_mutex_lock(&platform_message_lock);
	result = platform_question_result;
#endif
	if (result >= 0)
		platform_question_open = FALSE;
	pthread_mutex_unlock(&platform_message_lock);
	return result;
}

int platform_question_asking(void)
{
	BOOL open;

	pthread_mutex_lock(&platform_message_lock);
	open = platform_question_open;
	pthread_mutex_unlock(&platform_message_lock);
	return open;
}

#ifndef HALO_VITA
/* (on the event pump's thread: a desktop's message box waits for the
answer) */
static void platform_ask_pending_question(void)
{
	char title[sizeof(platform_question_title)];
	char text[sizeof(platform_question_text)];
	const SDL_MessageBoxButtonData buttons[] = {
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
	};
	SDL_MessageBoxData box;
	BOOL pending;
	int button = 0;

	pthread_mutex_lock(&platform_message_lock);
	pending = platform_question_pending;
	platform_question_pending = FALSE;
	memcpy(title, platform_question_title, sizeof(title));
	memcpy(text, platform_question_text, sizeof(text));
	pthread_mutex_unlock(&platform_message_lock);
	if (!pending)
		return;
	memset(&box, 0, sizeof(box));
	box.flags = SDL_MESSAGEBOX_INFORMATION;
	box.title = title;
	box.message = text;
	box.numbuttons = (int)(sizeof(buttons) / sizeof(buttons[0]));
	box.buttons = buttons;
#ifndef HALO_NOT_DESKTOP
	box.window = platform_window;
#endif
	if (!SDL_ShowMessageBox(&box, &button))
		button = 0;
	pthread_mutex_lock(&platform_message_lock);
	platform_question_result = button == 1;
	pthread_mutex_unlock(&platform_message_lock);
}
#endif

/* shows a progress line over the game (NULL hides it); the Vita's circle
cancels, platform_progress_cancelled. Elsewhere it is logged at most every
few seconds, and leaving the lobby cancels. */
void platform_show_progress(const char *title, const char *text)
{
#ifdef HALO_VITA
	extern void vita_settings_progress(const char *title, const char *text);

	vita_settings_progress(title, text);
#else
	static Uint64 logged_ticks;
	Uint64 now = SDL_GetTicks();

	if (text && (!logged_ticks || now - logged_ticks >= 5000))
	{
		char line[600];
		char *cursor;

		snprintf(line, sizeof(line), "%s", text);
		for (cursor = line; *cursor; cursor++)
			if (*cursor == '\n')
				*cursor = ' ';
		platform_log("%s: %s", title ? title : "", line);
		logged_ticks = now;
	}
	if (!text)
		logged_ticks = 0;
#endif
}

int platform_progress_cancelled(void)
{
#ifdef HALO_VITA
	extern int vita_settings_progress_cancelled(void);

	return vita_settings_progress_cancelled();
#else
	return 0;
#endif
}

static void platform_show_pending_message(void)
{
	char title[sizeof(platform_message_title)];
	char text[sizeof(platform_message_text)];
	BOOL pending;

	pthread_mutex_lock(&platform_message_lock);
	pending = platform_message_pending;
	platform_message_pending = FALSE;
	memcpy(title, platform_message_title, sizeof(title));
	memcpy(text, platform_message_text, sizeof(text));
	pthread_mutex_unlock(&platform_message_lock);
	if (!pending)
		return;
#ifdef HALO_VITA
	{
		extern void vita_settings_message(const char *title, const char *text);
		vita_settings_message(title, text);
	}
#elif defined(HALO_NOT_DESKTOP)
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title, text, NULL);
#else
	{
		/* (a box cannot show above a fullscreen game) */
		int fullscreen = (SDL_GetWindowFlags(platform_window) & SDL_WINDOW_FULLSCREEN) != 0;

		if (fullscreen)
			SDL_SetWindowFullscreen(platform_window, false);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title, text, platform_window);
		if (fullscreen)
			SDL_SetWindowFullscreen(platform_window, true);
	}
#endif
}

/* ---------- the text box (pc_text_entry.h) */

#ifndef HALO_VITA
/* the keys the text box took, kept from the game until they are let go
(their repeats too: the Enter that typed a name must not also press A on
the menu behind it) */
static unsigned char keys_taken[SDL_SCANCODE_COUNT];

/* (under input_lock, the event loop) a key pressed while the text box is
open: its editing keys handed to it, nothing to the game; TRUE when taken */
static BOOL text_entry_key(const SDL_KeyboardEvent *key)
{
	BOOL control = (key->mod & SDL_KMOD_CTRL) != 0;

	if (key->scancode >= SDL_SCANCODE_COUNT)
		return FALSE;
	if (!key->down)
	{
		if (!keys_taken[key->scancode])
			return FALSE;
		keys_taken[key->scancode] = 0;
		input_state.keys[key->scancode] = 0;
		return TRUE;
	}
	if (!pc_text_entry_active())
		return keys_taken[key->scancode] != 0;
	/* (F11 and F12 stay the window's and the mouse's) */
	if (key->scancode == SDL_SCANCODE_F11 || key->scancode == SDL_SCANCODE_F12)
		return FALSE;
	keys_taken[key->scancode] = 1;
	switch (key->scancode)
	{
	case SDL_SCANCODE_BACKSPACE: pc_text_entry_key(PC_TEXT_KEY_BACKSPACE, key->repeat); break;
	case SDL_SCANCODE_DELETE: pc_text_entry_key(PC_TEXT_KEY_DELETE, key->repeat); break;
	case SDL_SCANCODE_LEFT: pc_text_entry_key(PC_TEXT_KEY_LEFT, key->repeat); break;
	case SDL_SCANCODE_RIGHT: pc_text_entry_key(PC_TEXT_KEY_RIGHT, key->repeat); break;
	case SDL_SCANCODE_HOME: pc_text_entry_key(PC_TEXT_KEY_HOME, key->repeat); break;
	case SDL_SCANCODE_END: pc_text_entry_key(PC_TEXT_KEY_END, key->repeat); break;
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER:
		pc_text_entry_key(PC_TEXT_KEY_ACCEPT, key->repeat);
		break;
	case SDL_SCANCODE_ESCAPE: pc_text_entry_key(PC_TEXT_KEY_CANCEL, key->repeat); break;
	case SDL_SCANCODE_V:
	case SDL_SCANCODE_INSERT:
		/* (Ctrl+V, Shift+Insert: the clipboard's text, as typed) */
		if ((key->scancode == SDL_SCANCODE_V && control) ||
			(key->scancode == SDL_SCANCODE_INSERT && (key->mod & SDL_KMOD_SHIFT)))
		{
			char *clipboard = SDL_GetClipboardText();

			if (clipboard)
			{
				platform_log("text entry: pasted %d bytes", (int)strlen(clipboard));
				pc_text_entry_type(clipboard);
				SDL_free(clipboard);
			}
		}
		break;
	default:
		break;
	}
	return TRUE;
}

/* (the event loop) SDL's text input on while the box is open (its
events, and an input method's window where the system has one), off
otherwise */
static void text_entry_input(void)
{
	static BOOL started;
	BOOL open = pc_text_entry_active();

	if (open == started || !platform_window)
		return;
	started = open;
	if (open)
		SDL_StartTextInput(platform_window);
	else
		SDL_StopTextInput(platform_window);
}

#ifndef HALO_RELEASE
/* (tests only, never in a release build) HALO_TEST_KEYS="steps": the
keyboard's typing for the text box, through the same events a real
keyboard sends (pushed into SDL's queue for this loop to take), each step
after the one before it and a pause (TEST_KEYS_PAUSE_MS), separated by
spaces:
	typing        waits for the text box to open (then a pause)
	closed        waits for it to close
	text:TEXT     SDL_EVENT_TEXT_INPUT's text, %XX a byte (%20 a space,
	              %C3%A9 an é)
	key:NAME      a key pressed and let go (platform_test_key: Return,
	              Escape, BackSpace, Left, Home, ctrl+V ...)
	clip:TEXT     the clipboard's text (%XX as text:)
	wait:MS       a pause
	@NAME         the next frame saved as NAME.bmp (HALO_SCREENSHOT_DIR)
	until/TEXT    waits for a screen whose name has TEXT in it
	asking        waits for the menus to ask for a key to bind
	hold:NAME:MS  a key, mouse button ("Mouse%20Right") or wheel notch
	              ("Wheel%20Up") held for MS milliseconds
halo.log says each step as it is taken ("test keys: ..."). */
#define TEST_KEYS_PAUSE_MS 250

#ifndef HALO_NOT_DESKTOP
/* (below: the menus' asking for a key, the tests' holds) */
static int binding_capture_state;
BOOL platform_test_input_hold(const char *name, int milliseconds);
#endif

BOOL platform_test_key(const char *name);

static void test_keys_decode(char *out, int size, const char *text)
{
	int length = 0;

	while (*text && length < size - 1)
	{
		unsigned int byte;

		if (text[0] == '%' && text[1] && text[2] && sscanf(text + 1, "%2x", &byte) == 1)
		{
			out[length++] = (char)byte;
			text += 3;
		}
		else
			out[length++] = *text++;
	}
	out[length] = 0;
}

static void test_keys(void)
{
	static int parsed, count, next;
	static char steps[64][128];
	/* (the texts pushed: SDL keeps the pointer until the event is taken) */
	static char texts[64][128];
	static Uint64 at;
	Uint64 now = SDL_GetTicks();
	const char *step;

	if (!parsed)
	{
		const char *setting = getenv("HALO_TEST_KEYS");
		char copy[4096];
		char *token, *end;

		parsed = 1;
		if (!setting)
			return;
		snprintf(copy, sizeof(copy), "%s", setting);
		for (token = strtok_r(copy, " \t\r\n", &end); token && count < 64; token = strtok_r(NULL, " \t\r\n", &end))
			snprintf(steps[count++], sizeof(steps[0]), "%s", token);
		at = now;
	}
	while (next < count && now >= at)
	{
		step = steps[next];
		if (!strcmp(step, "typing") && !pc_text_entry_active())
			return;
		if (!strcmp(step, "closed") && pc_text_entry_active())
			return;
#ifndef HALO_NOT_DESKTOP
		/* ("until/TEXT": until the screen open is one whose name has TEXT in
		it, as HALO_TEST_PAD's; "asking": until the menus ask for a key to
		bind, controls_menu.c) */
		if (!strncmp(step, "until/", 6))
		{
			char const *ui_widget_port_active_screen_name(void);

			if (!strstr(ui_widget_port_active_screen_name(), step + 6))
				return;
		}
		if (!strcmp(step, "asking") && binding_capture_state != 1)
			return;
#endif
		platform_log("test keys: %s", step);
		at = now + TEST_KEYS_PAUSE_MS;
		if (!strncmp(step, "text:", 5))
		{
			SDL_Event event;

			test_keys_decode(texts[next], sizeof(texts[0]), step + 5);
			memset(&event, 0, sizeof(event));
			event.type = SDL_EVENT_TEXT_INPUT;
			event.text.timestamp = SDL_GetTicksNS();
			event.text.windowID = platform_window ? SDL_GetWindowID(platform_window) : 0;
			event.text.text = texts[next];
			SDL_PushEvent(&event);
		}
		else if (!strncmp(step, "key:", 4))
			platform_test_key(step + 4);
		else if (!strncmp(step, "clip:", 5))
		{
			test_keys_decode(texts[next], sizeof(texts[0]), step + 5);
			if (!SDL_SetClipboardText(texts[next]))
				platform_log("test keys: the clipboard was not set: %s", SDL_GetError());
		}
		else if (!strncmp(step, "wait:", 5))
			at = now + (Uint64)atoi(step + 5);
#ifndef HALO_NOT_DESKTOP
		/* ("hold:NAME:MS": a key, mouse button or wheel notch held that
		long: "hold:Mouse%20Right:200", %20 a space) */
		else if (!strncmp(step, "hold:", 5))
		{
			char name[64];
			char const *colon = strrchr(step + 5, ':');

			test_keys_decode(name, sizeof(name), step + 5);
			if (colon && strrchr(name, ':'))
				*strrchr(name, ':') = 0;
			platform_test_input_hold(name, colon ? atoi(colon + 1) : 150);
		}
#endif
		else if (step[0] == '@' && step[1])
		{
			extern char halo_screenshot_name[64];

			snprintf(halo_screenshot_name, 64, "%s", step + 1);
		}
		next++;
		if (next == count)
			platform_log("test keys: done");
		return;
	}
}
#endif
#endif

/* ---------- the menus' asking for a key to bind (halo_key_bindings.h) */

#ifndef HALO_NOT_DESKTOP
#include "../include/halo_key_bindings.h"

/* (under input_lock) 0 none, 1 waiting for a key, 2 one taken (its result
and input below, until polled) */
static int binding_capture_state;
static int binding_capture_result, binding_capture_input;
/* the mouse's buttons the asking took, kept from the game until let go */
static unsigned char mouse_taken[PLATFORM_MOUSE_BUTTON_COUNT];

void platform_binding_capture_begin(void)
{
	pthread_mutex_lock(&input_lock);
	binding_capture_state = 1;
	binding_capture_result = 0;
	pthread_mutex_unlock(&input_lock);
	platform_log("controls: press a key to bind");
}

void platform_binding_capture_end(void)
{
	pthread_mutex_lock(&input_lock);
	binding_capture_state = 0;
	pthread_mutex_unlock(&input_lock);
}

int platform_binding_capturing(void)
{
	return binding_capture_state != 0;
}

int platform_binding_capture_poll(int *input)
{
	int result = 0;

	pthread_mutex_lock(&input_lock);
	if (binding_capture_state == 2)
	{
		result = binding_capture_result;
		*input = binding_capture_input;
		binding_capture_state = 0;
	}
	pthread_mutex_unlock(&input_lock);
	return result;
}

/* (under input_lock, the event loop) an input taken by the asking: 1
an input, 2 cancelled (Escape), 3 cleared (Delete) */
static void binding_capture_take(int result, int input)
{
	char name[64];

	binding_capture_state = 2;
	binding_capture_result = result;
	binding_capture_input = input;
	halo_bind_input_name(input, name, sizeof(name));
	platform_log("controls: %s", result == 2 ? "cancelled (Escape)" : result == 3 ? "cleared (Delete)" : name);
}

/* (the event loop) a key pressed while the menus ask for one: TRUE when
taken (F11, F12 and the console's key stay theirs) */
static BOOL binding_capture_key(const SDL_KeyboardEvent *key)
{
	if (binding_capture_state != 1 || !key->down || key->scancode >= SDL_SCANCODE_COUNT ||
		key->scancode == SDL_SCANCODE_F11 || key->scancode == SDL_SCANCODE_F12 || key->scancode == SDL_SCANCODE_GRAVE)
	{
		return FALSE;
	}
	if (key->repeat)
		return TRUE;
	/* (kept from the game and the menus until let go: text_entry_key) */
	keys_taken[key->scancode] = 1;
	if (key->scancode == SDL_SCANCODE_ESCAPE)
		binding_capture_take(2, -1);
	else if (key->scancode == SDL_SCANCODE_DELETE)
		binding_capture_take(3, -1);
	else
		binding_capture_take(1, (int)key->scancode);
	return TRUE;
}

/* (the event loop) a mouse button or the wheel, likewise */
static BOOL binding_capture_mouse(const SDL_Event *event)
{
	if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button < PLATFORM_MOUSE_BUTTON_COUNT &&
		mouse_taken[event->button.button])
	{
		mouse_taken[event->button.button] = 0;
		return TRUE;
	}
	if (binding_capture_state != 1)
		return FALSE;
	if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
	{
		if (event->button.button < PLATFORM_MOUSE_BUTTON_COUNT)
			mouse_taken[event->button.button] = 1;
		binding_capture_take(1, HALO_BIND_MOUSE + event->button.button);
		return TRUE;
	}
	if (event->type == SDL_EVENT_MOUSE_WHEEL && event->wheel.y != 0.0f)
	{
		binding_capture_take(1, event->wheel.y > 0.0f ? HALO_BIND_WHEEL_UP : HALO_BIND_WHEEL_DOWN);
		return TRUE;
	}
	return event->type == SDL_EVENT_MOUSE_BUTTON_UP;
}
#endif

#if !defined(HALO_RELEASE) && !defined(HALO_NOT_DESKTOP)
/* (tests only: main.c's "@hold NAME MS", HALO_TEST_KEYS's hold:NAME:MS) a
key, a mouse button ("Mouse Left") or the wheel ("Wheel Up", "Wheel Down":
a notch) pressed now and let go MS milliseconds later, through SDL's queue
as the real ones arrive; FALSE for a name nothing has */
static struct
{
	int input;
	Uint64 up_at;
} test_holds[16];

static void test_input_event(int input, BOOL down)
{
	SDL_Event event;

	memset(&event, 0, sizeof(event));
	if (input >= HALO_BIND_WHEEL_UP)
	{
		if (!down)
			return;
		event.type = SDL_EVENT_MOUSE_WHEEL;
		event.wheel.timestamp = SDL_GetTicksNS();
		event.wheel.windowID = platform_window ? SDL_GetWindowID(platform_window) : 0;
		event.wheel.y = input == HALO_BIND_WHEEL_DOWN ? -1.0f : 1.0f;
		event.wheel.integer_y = input == HALO_BIND_WHEEL_DOWN ? -1 : 1;
	}
	else if (input >= HALO_BIND_MOUSE)
	{
		event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
		event.button.timestamp = SDL_GetTicksNS();
		event.button.windowID = platform_window ? SDL_GetWindowID(platform_window) : 0;
		event.button.button = (Uint8)(input - HALO_BIND_MOUSE);
		event.button.down = down ? true : false;
		event.button.clicks = 1;
		event.button.x = 320.0f;
		event.button.y = 240.0f;
	}
	else
	{
		event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
		event.key.timestamp = SDL_GetTicksNS();
		event.key.windowID = platform_window ? SDL_GetWindowID(platform_window) : 0;
		event.key.scancode = (SDL_Scancode)input;
		event.key.key = SDL_GetKeyFromScancode((SDL_Scancode)input, SDL_KMOD_NONE, false);
		event.key.down = down ? true : false;
	}
	SDL_PushEvent(&event);
}

BOOL platform_test_input_hold(const char *name, int milliseconds)
{
	static int names_given;
	int input, index;

	if (!names_given)
	{
		names_given = 1;
		halo_bind_set_key_names((int (*)(const char *))SDL_GetScancodeFromName,
			(const char *(*)(int))SDL_GetScancodeName);
	}
	input = halo_bind_input_from_name(name);
	if (input == HALO_BIND_NONE || input == HALO_BIND_WHEEL)
	{
		platform_log("test hold: no key or button \"%s\"", name);
		return FALSE;
	}
	platform_log("test hold: %s for %d ms", name, milliseconds);
	test_input_event(input, TRUE);
	for (index = 0; index < 16; index++)
	{
		if (!test_holds[index].up_at)
		{
			test_holds[index].input = input;
			test_holds[index].up_at = SDL_GetTicks() + (Uint64)(milliseconds > 0 ? milliseconds : 1);
			break;
		}
	}
	return TRUE;
}

/* (each pump) the holds whose time is up let go */
static void test_holds_update(void)
{
	int index;

	for (index = 0; index < 16; index++)
	{
		if (test_holds[index].up_at && SDL_GetTicks() >= test_holds[index].up_at)
		{
			test_holds[index].up_at = 0;
			test_input_event(test_holds[index].input, FALSE);
		}
	}
}
#endif

/* ---------- events */

void platform_pump_events(void)
{
	/* debug.exit_after (seconds) ends the game that long after the window
	opens, as closing it does (tools/pgo_train.py) */
	static Uint64 exit_ticks = (Uint64)-1;
	SDL_Event event;
	static BOOL looked_at_clipboard;
	BOOL look_at_clipboard = !looked_at_clipboard;

#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	/* no window: the thread that brought the display up pumps */
	if (!platform_sdl_started || SDL_GetCurrentThreadID() != platform_event_thread)
		return;
#else
	if (!platform_window || SDL_GetCurrentThreadID() != platform_event_thread)
		return;
#endif
	if (exit_ticks == (Uint64)-1)
	{
		double seconds = config_real("debug.exit_after");

		exit_ticks = seconds > 0.0 ? SDL_GetTicks() + (Uint64)(seconds * 1000.0) : 0;
	}
	if (exit_ticks && SDL_GetTicks() >= exit_ticks)
	{
		platform_log("exiting after debug.exit_after");
		exit(EXIT_SUCCESS);
	}
	platform_show_pending_message();
#ifndef HALO_VITA
	platform_ask_pending_question();
#endif
#ifndef HALO_NOT_DESKTOP
	updater_poll(platform_window);
#endif
#ifndef HALO_VITA
#ifndef HALO_RELEASE
	test_keys();
#endif
	text_entry_input();
#endif
#if !defined(HALO_RELEASE) && !defined(HALO_NOT_DESKTOP)
	test_holds_update();
#endif
	pthread_mutex_lock(&input_lock);
	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
#ifndef HALO_VITA
		case SDL_EVENT_TEXT_INPUT:
			/* (the text box's: SDL sends it only while the box has text
			input on) */
			if (event.text.text)
				pc_text_entry_type(event.text.text);
			break;
#endif
		case SDL_EVENT_QUIT:
			pthread_mutex_unlock(&input_lock);
			platform_log("window closed");
			exit(EXIT_SUCCESS);
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
#ifndef HALO_NOT_DESKTOP
			/* (the menus asking for a key to bind: theirs alone) */
			if (binding_capture_key(&event.key))
			{
				platform_input_device_used(_halo_input_device_keyboard);
				break;
			}
#endif
#ifndef HALO_VITA
			/* (the text box open: its keys, none for the game) */
			if (text_entry_key(&event.key))
			{
#ifndef HALO_NOT_DESKTOP
				if (event.key.down)
					platform_input_device_used(_halo_input_device_keyboard);
#endif
				break;
			}
#endif
			if (event.key.scancode < SDL_SCANCODE_COUNT)
			{
				input_state.keys[event.key.scancode] = event.key.down;
				if (event.key.down)
					keys_pressed[event.key.scancode] = 1;
			}
			queue_keystroke(&event.key);
#ifndef HALO_NOT_DESKTOP
			if (event.key.down)
				platform_input_device_used(_halo_input_device_keyboard);
#endif
			/* F12 releases or recaptures the mouse */
			if (event.key.down && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F12)
			{
				input_state.mouse_released = !input_state.mouse_released;
				platform_mouse_capture(!input_state.mouse_released && !input_state.ui_pointer);
			}
#ifndef HALO_NOT_DESKTOP
			/* F11 switches between fullscreen and the window (SDL keeps the
			window's size and place while fullscreen) */
			if (event.key.down && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F11)
			{
				SDL_SetWindowFullscreen(platform_window,
					(SDL_GetWindowFlags(platform_window) & SDL_WINDOW_FULLSCREEN) ? false : true);
			}
#endif
			break;
		case SDL_EVENT_MOUSE_MOTION:
#ifndef HALO_NOT_DESKTOP
			if (SDL_fabsf(event.motion.xrel) + SDL_fabsf(event.motion.yrel) >= 3.0f)
				platform_input_device_used(_halo_input_device_keyboard);
			/* in the menus the mouse moves the pointer, not the view */
			if (input_state.ui_pointer)
			{
				ui_pointer.x = event.motion.x;
				ui_pointer.y = event.motion.y;
				ui_pointer.moved = TRUE;
				break;
			}
#endif
			input_state.mouse_dx += event.motion.xrel;
			input_state.mouse_dy += event.motion.yrel;
#ifndef HALO_NOT_DESKTOP
			mouse_emulation_recenter(event.motion.x, event.motion.y);
#endif
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
#ifndef HALO_NOT_DESKTOP
			if (binding_capture_mouse(&event))
				break;
#endif
#ifndef HALO_VITA
			/* (the text box open: no clicks for the menus behind it) */
			if (event.button.down && pc_text_entry_active())
				break;
#endif
#ifndef HALO_NOT_DESKTOP
			if (event.button.down)
				platform_input_device_used(_halo_input_device_keyboard);
			/* clicks in the menus go to the pointer; a button held down
			when the menu closes stays up until pressed again, so the click
			that resumes the game does not also fire */
			if (input_state.ui_pointer)
			{
				if (event.button.down && event.button.button == SDL_BUTTON_LEFT)
				{
					ui_pointer.left_clicks++;
					ui_pointer.click_x = event.button.x;
					ui_pointer.click_y = event.button.y;
				}
				else if (event.button.down && event.button.button == SDL_BUTTON_RIGHT)
				{
					ui_pointer.right_clicks++;
				}
				break;
			}
#endif
			if (event.button.button < PLATFORM_MOUSE_BUTTON_COUNT)
			{
				input_state.mouse_buttons[event.button.button] = event.button.down;
				if (event.button.down)
					mouse_pressed[event.button.button] = 1;
			}
			break;
		case SDL_EVENT_MOUSE_WHEEL:
#ifndef HALO_VITA
			if (pc_text_entry_active())
				break;
#endif
#ifndef HALO_NOT_DESKTOP
			if (binding_capture_mouse(&event))
				break;
#endif
#ifndef HALO_NOT_DESKTOP
			platform_input_device_used(_halo_input_device_keyboard);
			if (input_state.ui_pointer)
			{
				/* whole notches: smooth-scrolling wheels send fractions */
				ui_pointer_wheel += event.wheel.y;
				while (ui_pointer_wheel >= 1.0f)
				{
					ui_pointer.wheel_steps++;
					ui_pointer_wheel -= 1.0f;
				}
				while (ui_pointer_wheel <= -1.0f)
				{
					ui_pointer.wheel_steps--;
					ui_pointer_wheel += 1.0f;
				}
				break;
			}
#endif
			input_state.mouse_wheel += event.wheel.y;
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			memset(input_state.keys, 0, sizeof(input_state.keys));
			memset(input_state.mouse_buttons, 0, sizeof(input_state.mouse_buttons));
			input_state.focused = FALSE;
			break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			input_state.focused = TRUE;
			look_at_clipboard = TRUE;
#ifndef HALO_NOT_DESKTOP
			if (!input_state.mouse_released && !input_state.ui_pointer)
				platform_mouse_capture(TRUE);
#endif
			break;
		case SDL_EVENT_GAMEPAD_ADDED:
			SDL_OpenGamepad(event.gdevice.which);
#ifndef HALO_NOT_DESKTOP
			if (input_device_last < 0)
				platform_input_device_used(gamepad_input_device(event.gdevice.which));
#endif
			break;
#ifndef HALO_NOT_DESKTOP
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			platform_input_device_used(gamepad_input_device(event.gbutton.which));
			break;
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			if (event.gaxis.value >= INPUT_DEVICE_AXIS_THRESHOLD || event.gaxis.value <= -INPUT_DEVICE_AXIS_THRESHOLD)
				platform_input_device_used(gamepad_input_device(event.gaxis.which));
			break;
#endif
		case SDL_EVENT_GAMEPAD_REMOVED:
			/* port: closed (SDL keeps a gamepad open until it is closed,
			even once the controller has gone), from OpenCE's leak fixes */
			{
				SDL_Gamepad *gamepad = SDL_GetGamepadFromID(event.gdevice.which);

				if (gamepad)
					SDL_CloseGamepad(gamepad);
			}
			break;
		default:
			break;
		}
	}
	pthread_mutex_unlock(&input_lock);
	looked_at_clipboard = TRUE;
	platform_invite_clipboard(look_at_clipboard);
}

#if !defined(HALO_NOT_DESKTOP) && !defined(HALO_GXM_NULL)
/* ---------- the menus' pointer */

/* While a menu is up the mouse is released, its pointer shows (centered when
the menu opens) and its motion, clicks and wheel go to the menus
(halo_ui_pointer_update, d3d8_gl.c) instead of the controller and the aim. */
void platform_ui_pointer_set_active(BOOL active)
{
	if (!platform_window || (active != FALSE) == (input_state.ui_pointer != FALSE))
		return;
	pthread_mutex_lock(&input_lock);
	input_state.ui_pointer = active;
	memset(&ui_pointer, 0, sizeof(ui_pointer));
	ui_pointer_wheel = 0.0f;
	input_state.mouse_dx = 0.0f;
	input_state.mouse_dy = 0.0f;
	input_state.mouse_wheel = 0.0f;
	memset(input_state.mouse_buttons, 0, sizeof(input_state.mouse_buttons));
	pthread_mutex_unlock(&input_lock);
	platform_log("mouse: the menus' pointer %s", active ? "on" : "off (the mouse aims)");
	platform_mouse_capture(!active && !input_state.mouse_released);
	if (active)
	{
		int width, height;

		SDL_GetWindowSize(platform_window, &width, &height);
		SDL_WarpMouseInWindow(platform_window, width * 0.5f, height * 0.5f);
		pthread_mutex_lock(&input_lock);
		ui_pointer.x = width * 0.5f;
		ui_pointer.y = height * 0.5f;
		pthread_mutex_unlock(&input_lock);
	}
}

/* what the pointer did since the last call; FALSE when it is not active */
BOOL platform_ui_pointer_read(struct platform_ui_pointer *pointer)
{
	BOOL active;

	pthread_mutex_lock(&input_lock);
	active = input_state.ui_pointer;
	*pointer = ui_pointer;
	ui_pointer.moved = FALSE;
	ui_pointer.left_clicks = 0;
	ui_pointer.right_clicks = 0;
	ui_pointer.wheel_steps = 0;
	pthread_mutex_unlock(&input_lock);
	return active;
}

void platform_video_window_size(int *width, int *height)
{
	SDL_GetWindowSize(platform_window, width, height);
}

#endif
#ifndef HALO_VITA
/* (main.c's "@aim angles") whether the mouse aims now: the menus' pointer,
F12's release, SDL's relative mode, the window's focus */
void platform_mouse_state_log(void)
{
	BOOL pointer, released, focused;

	pthread_mutex_lock(&input_lock);
	pointer = input_state.ui_pointer;
	released = input_state.mouse_released;
	focused = input_state.focused;
	pthread_mutex_unlock(&input_lock);
#ifndef HALO_NOT_DESKTOP
	platform_log("mouse: pointer %d released %d relative %d held %d focused %d", (int)pointer, (int)released,
		platform_window ? (int)SDL_GetWindowRelativeMouseMode(platform_window) : -1, (int)mouse_emulating, (int)focused);
#else
	platform_log("mouse: pointer %d released %d relative %d focused %d", (int)pointer, (int)released,
		platform_window ? (int)SDL_GetWindowRelativeMouseMode(platform_window) : -1, (int)focused);
#endif
}
#endif

#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
/* (tests only, main.c's "@mouse dx dy") a motion of the mouse pushed into
SDL's queue, as a real mouse's arrives: platform_pump_events takes it */
void platform_test_mouse_motion(float dx, float dy)
{
	SDL_Event event;

	memset(&event, 0, sizeof(event));
	event.type = SDL_EVENT_MOUSE_MOTION;
	event.motion.timestamp = SDL_GetTicksNS();
	event.motion.windowID = platform_window ? SDL_GetWindowID(platform_window) : 0;
	event.motion.x = 320.0f;
	event.motion.y = 240.0f;
	event.motion.xrel = dx;
	event.motion.yrel = dy;
	if (!SDL_PushEvent(&event))
		platform_log("test mouse: the motion was not queued: %s", SDL_GetError());
}
#endif

#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
/* (tests only: main.c's "@key name", HALO_TEST_KEYS) a key pressed and let
go, pushed into SDL's queue as the keyboard's own events arrive, for the
event loop to take: a key by SDL's name ("Escape", "Return", "Left", "A"),
"ctrl+" or "shift+" before it held with it. FALSE for a name SDL does not
know */
BOOL platform_test_key(const char *name)
{
	SDL_Event event;
	SDL_Keymod modifiers = SDL_KMOD_NONE;
	SDL_Scancode scancode;

	for (;;)
	{
		if (!SDL_strncasecmp(name, "ctrl+", 5))
			modifiers |= SDL_KMOD_LCTRL, name += 5;
		else if (!SDL_strncasecmp(name, "shift+", 6))
			modifiers |= SDL_KMOD_LSHIFT, name += 6;
		else
			break;
	}
	scancode = SDL_GetScancodeFromName(name);
	if (scancode == SDL_SCANCODE_UNKNOWN)
	{
		platform_log("test key: no key \"%s\"", name);
		return FALSE;
	}
	memset(&event, 0, sizeof(event));
	event.type = SDL_EVENT_KEY_DOWN;
	event.key.timestamp = SDL_GetTicksNS();
	event.key.windowID = platform_window ? SDL_GetWindowID(platform_window) : 0;
	event.key.scancode = scancode;
	event.key.key = SDL_GetKeyFromScancode(scancode, modifiers, false);
	event.key.mod = modifiers;
	event.key.down = true;
	SDL_PushEvent(&event);
	event.type = SDL_EVENT_KEY_UP;
	event.key.down = false;
	SDL_PushEvent(&event);
	platform_log("test key: %s%s%s", modifiers & SDL_KMOD_CTRL ? "ctrl+" : "", modifiers & SDL_KMOD_SHIFT ? "shift+" : "",
		SDL_GetScancodeName(scancode));
	return TRUE;
}
#endif

void platform_input_read(struct platform_input_state *state, BOOL consume_motion)
{
	pthread_mutex_lock(&input_lock);
	*state = input_state;
	if (consume_motion)
	{
		int scancode;

		for (scancode = 0; scancode < SDL_SCANCODE_COUNT; scancode++)
		{
			state->keys[scancode] |= keys_pressed[scancode];
			keys_pressed[scancode] = 0;
		}
		for (scancode = 0; scancode < PLATFORM_MOUSE_BUTTON_COUNT; scancode++)
		{
			if (!state->ui_pointer)
				state->mouse_buttons[scancode] |= mouse_pressed[scancode];
			mouse_pressed[scancode] = 0;
		}
	}
	if (consume_motion)
	{
		input_state.mouse_dx = 0;
		input_state.mouse_dy = 0;
		input_state.mouse_wheel = 0;
	}
	pthread_mutex_unlock(&input_lock);
}
