/* load_profile.h

A long frame's loading, saving and file calls, by part (load_profile.c;
on unless HALO_LOAD_PROFILE=0, and HALO_LOAD_PROFILE=1 adds its own
load-profile line). A place that loads takes halo_load_profile_now() before
and passes it to halo_load_profile_add() after; the start is 0 when the
profile is off, and add() does nothing then. */

#ifndef __HALO_LOAD_PROFILE_H
#define __HALO_LOAD_PROFILE_H

enum
{
	_halo_load_game_state_save,
	_halo_load_game_state_revert,
	_halo_load_persistent_header,
	_halo_load_persistent_read,
	_halo_load_persistent_write,
	_halo_load_new_map,
	_halo_load_game_load,
	_halo_load_switch_bsp,
	_halo_load_bsp_clear,
	_halo_load_bsp_read,
	_halo_load_bsp_relocate,
	_halo_load_bsp_vertex_buffers,
	_halo_load_cache_read_game_thread,
	_halo_load_cache_read_other,
	_halo_load_cache_write,
	_halo_load_cache_wait,
	_halo_load_sound_cache_wait,
	_halo_load_texture_cache_wait,
	_halo_load_persistent_after_load,
	_halo_load_file_game_thread,
	_halo_load_file_other,
	_halo_load_count
};

int halo_load_profile_check(void);
unsigned long long halo_load_profile_now(void);
void halo_load_profile_add(int kind, unsigned long long started_us, unsigned long long bytes);
/* a file call (xbox_files.c): its kind is the calling thread's (the game
thread's, or another's), and the frame keeps the slowest of each, by name */
void halo_load_profile_file_call(unsigned long long started_us, const char *call, const char *path,
	unsigned long long bytes);
/* the frame's parts so far, for the frame-hitch line (main.c): "texture
cache wait 468.3 ms (24x) | ..."; 0 when there are none */
int halo_load_profile_describe(char *line, int size);
/* the game thread, once a frame */
void halo_load_profile_frame_end(void);

#endif
