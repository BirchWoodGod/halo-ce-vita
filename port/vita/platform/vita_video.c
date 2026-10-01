/*
VITA_VIDEO.C

The display side of port/linux/src/sdl_platform.c for the Vita, and the
desktop-only entry points the shared platform layer still names. The
Direct3D device (d3d8_gxm.c) draws through the GXM renderer.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "vita_host.h"
#include "vita_gxm.h"

BOOL platform_vita_sdl_initialize(void);

/* GXM, the display and the renderer (port/vita/host/vita_gxm.c) */
BOOL platform_video_initialize(unsigned long width, unsigned long height)
{
	static int result = -1;
	unsigned long size = 0;
	void *arena;

	(void)width;
	(void)height;
	if (result >= 0)
		return result == 0;
	if (!platform_vita_sdl_initialize())
		platform_log("SDL is not up: no controls or sound");
	arena = vita_host_arena(&size);
	result = arena ? vgxm_initialize(arena, size) : -1;
	if (result != 0)
		result = 1;
	return result == 0;
}

void platform_video_drawable_size(int *width, int *height)
{
	*width = 960;
	*height = 544;
}

void platform_video_swap(void)
{
}

BOOL platform_screen_mode(long *width, long *height)
{
	(void)width;
	(void)height;
	return FALSE;
}

void platform_ui_pointer_set_active(BOOL active)
{
	(void)active;
}

BOOL platform_ui_pointer_read(struct platform_ui_pointer *pointer)
{
	(void)pointer;
	return FALSE;
}

void platform_video_window_size(int *width, int *height)
{
	*width = 960;
	*height = 544;
}
