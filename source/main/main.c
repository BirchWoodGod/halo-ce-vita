/*
MAIN.C

symbols in this file:
000EF6F0 0010:
	_main_get_seconds_elapsed (0000)
000EF700 0080:
	_gamepad_button_is_down (0000)
000EF780 0010:
	_game_connection_set (0000)
000EF790 0010:
	_game_connection (0000)
000EF7A0 0010:
	_main_disallow_persistent_storage (0000)
000EF7B0 0060:
	_main_set_map_name (0000)
000EF810 0010:
	_main_defer_map_map_change (0000)
000EF820 0030:
	_main_set_multiplayer_map_name (0000)
000EF850 0010:
	_main_get_map_name (0000)
000EF860 0010:
	_main_get_multiplayer_map_name (0000)
000EF870 0020:
	_main_set_difficulty (0000)
000EF890 0010:
	_main_get_difficulty (0000)
000EF8A0 0040:
	_sort_desired_local_player_controllers (0000)
000EF8E0 01d0:
	_code_000ef8e0 (0000)
000EFAB0 0020:
	_main_reset_map (0000)
000EFAD0 0020:
	_main_revert_map (0000)
000EFAF0 0020:
	_main_skip_cinematic (0000)
000EFB10 0010:
	_main_save_map_nonsafe (0000)
000EFB20 0010:
	_main_saving_map (0000)
000EFB30 0010:
	_main_save_cancel (0000)
000EFB40 0040:
	_main_save_map_safe (0000)
000EFB80 0010:
	_main_won_map (0000)
000EFB90 0010:
	_main_lost_map (0000)
000EFBA0 0020:
	_main_respawn (0000)
000EFBC0 0020:
	_main_save_core (0000)
000EFBE0 0050:
	_main_save_core_name (0000)
000EFC30 0020:
	_main_load_core (0000)
000EFC50 0020:
	_main_load_core_at_startup (0000)
000EFC70 0050:
	_main_load_core_name (0000)
000EFCC0 0050:
	_main_load_core_name_at_startup (0000)
000EFD10 0060:
	_main_switch_structure_bsp (0000)
000EFD70 0030:
	_main_skip (0000)
000EFDA0 0040:
	_main_queue_map_name (0000)
000EFDE0 0050:
	_code_000efde0 (0000)
000EFE30 0020:
	_main_goto_main_menu (0000)
000EFE50 0050:
	_main_menu_precache_resources (0000)
000EFEA0 0020:
	_main_menu_unload (0000)
000EFEC0 0010:
	_main_menu_ensure_player_queues_exist (0000)
000EFED0 0010:
	_main_menu_fade_active (0000)
000EFEE0 0010:
	_main_menu_switch_to_single_player (0000)
000EFEF0 0010:
	_main_set_game_connection_to_film_playback (0000)
000EFF00 0170:
	_main_get_solo_level_from_name (0000)
000F0070 0010:
	_main_get_current_solo_level (0000)
000F0080 0020:
	_main_get_solo_level_name (0000)
000F00A0 0010:
	_main_run_demos (0000)
000F00B0 0070:
	_code_000f00b0 (0000)
000F0120 01f0:
	_compute_window_bounds (0000)
000F0310 0040:
	_main_get_window_count (0000)
000F0350 00d0:
	_code_000f0350 (0000)
000F0420 0180:
	_code_000f0420 (0000)
000F05A0 0020:
	_code_000f05a0 (0000)
000F05C0 0030:
	_code_000f05c0 (0000)
000F05F0 00b0:
	_code_000f05f0 (0000)
000F06A0 0020:
	_code_000f06a0 (0000)
000F06C0 00e0:
	_code_000f06c0 (0000)
000F07A0 0030:
	_code_000f07a0 (0000)
000F07D0 0040:
	_code_000f07d0 (0000)
000F0810 0040:
	_code_000f0810 (0000)
000F0850 0060:
	_code_000f0850 (0000)
000F08B0 0020:
	_code_000f08b0 (0000)
000F08D0 0020:
	_code_000f08d0 (0000)
000F08F0 0040:
	_code_000f08f0 (0000)
000F0930 0010:
	_code_000f0930 (0000)
000F0940 0220:
	_code_000f0940 (0000)
000F0B60 0050:
	_code_000f0b60 (0000)
000F0BB0 0030:
	_code_000f0bb0 (0000)
000F0BE0 0010:
	_main_framerate_throttle_enabled (0000)
000F0BF0 05a0:
	_code_000f0bf0 (0000)
000F1190 0210:
	_main_rasterizer_throttle (0000)
000F13A0 0020:
	_code_000f13a0 (0000)
000F13C0 0020:
	_main_taking_screenshot (0000)
000F13E0 00b0:
	_main_movie_start (0000)
000F1490 0020:
	_main_movie_stop (0000)
000F14B0 0010:
	_main_stop_time (0000)
000F14C0 0010:
	_main_start_time (0000)
000F14D0 0010:
	_main_crash (0000)
000F14E0 0010:
	_main_print_version (0000)
000F14F0 00c0:
	_main_vertical_blank_interrupt_handler (0000)
000F15B0 0070:
	_main_save_current_solo_map (0000)
000F1620 00c0:
	_main_load_last_solo_map (0000)
000F16E0 0040:
	_main_save_map_no_timeout (0000)
000F1720 00e0:
	_main_load_ui_scenario (0000)
000F1800 0090:
	_main_menu_load (0000)
000F1890 0020:
	_main_roll_credits (0000)
000F18B0 0130:
	_main_pregame_render (0000)
000F19E0 01b0:
	_set_window_camera_values (0000)
000F1B90 0090:
	_main_present_frame (0000)
000F1C20 00c0:
	_code_000f1c20 (0000)
000F1CE0 0050:
	_code_000f1ce0 (0000)
000F1D30 01f0:
	_code_000f1d30 (0000)
000F1F20 02a0:
	_main_framerate_render (0000)
000F21C0 02a0:
	_halt_and_catch_fire (0000)
000F2460 0040:
	_main_loop_of_death (0000)
000F24A0 01c0:
	_code_000f24a0 (0000)
000F2660 0670:
	_main_loop (0000)
002795A8 003a:
	??_C@_0DK@GMPCKHNN@button_index?$DO?$DN0?5?$CG?$CG?5button_index?$DM@ (0000)
002795E4 001b:
	??_C@_0BL@EKABHJPH@c?3?2halo?2SOURCE?2main?2main?4c?$AA@ (0000)
00279600 0037:
	??_C@_0DH@DGKBOONK@?$CIgamepad_index?$DO?$DN0?$CJ?5?$CG?$CG?5?$CIgamepad_i@ (0000)
00279638 0013:
	??_C@_0BD@OOOKAGFE@j?$DMMAXIMUM_GAMEPADS?$AA@ (0000)
00279650 0049:
	??_C@_0EJ@DIAKEACK@?$CIdesired_controllers?$FLi?$FN?$DO?$DN0?$CJ?5?$CG?$CG?5?$CI@ (0000)
0027969C 002c:
	??_C@_0CM@EOEJOPGF@game_connection?$CI?$CJ?5?$DN?$DN?5_game_conne@ (0000)
002796C8 0009:
	??_C@_08FLLKDKH@core?4bin?$AA@ (0000)
002796D4 003b:
	??_C@_0DL@LKJEADLD@warning?0?5core?5file?5name?5will?5be?5@ (0000)
00279710 002c:
	??_C@_0CM@BPJFOCNH@tried?5to?5switch?5to?5invalid?5struc@ (0000)
0027973C 002c:
	??_C@_0CM@IOHNOMHF@tried?5to?5switch?5to?5current?5struc@ (0000)
00279768 0030:
	??_C@_0DA@FGIPPFOE@cannot?5skip?5more?5than?515?5frames?5@ (0000)
00279798 0029:
	??_C@_0CJ@GNBMIDNC@scenario?9?$DOtype?$DN?$DN_scenario_type_m@ (0000)
002797C4 000e:
	??_C@_0O@OJNPMJKK@num_players?$DO0?$AA@ (0000)
002797D4 0039:
	??_C@_0DJ@LGIIGICN@horizontal_index?$DO?$DN0?5?$CG?$CG?5horizonta@ (0000)
00279810 0033:
	??_C@_0DD@NNOBFMAG@vertical_index?$DO?$DN0?5?$CG?$CG?5vertical_in@ (0000)
00279844 0019:
	??_C@_0BJ@LDIFOEOI@player_index?$DMnum_players?$AA@ (0000)
00279860 0017:
	??_C@_0BH@PGKHPIFN@main_new_map?$CI?$CJ?5failed?4?$AA@ (0000)
00279878 0014:
	??_C@_0BE@HPJEDLPC@game_load?$CI?$CJ?5failed?4?$AA@ (0000)
00279890 0041:
	??_C@_0EB@NLDLOJCN@manual?5skipping?5doesn?8t?5work?5out@ (0000)
002798D4 000c:
	??_C@_0M@NBLPJHAH@unsafe?5save?$AA@ (0000)
002798E0 0017:
	??_C@_0BH@JAFGAFKC@gave?5up?5trying?5to?5save?$AA@ (0000)
002798F8 001f:
	??_C@_0BP@JOHDPJOL@?$DLcore_load_name_at_startup?5?$CFs?6?$AA@ (0000)
00279918 0004:
	??_C@_03CCGKKFHG@a?$CLt?$AA@ (0000)
0027991C 000d:
	??_C@_0N@HMFDKLFI@map_name?5?$CFs?6?$AA@ (0000)
0027992C 0003:
	??_C@_02BKFDOEMK@wt?$AA@ (0000)
00279930 000f:
	??_C@_0P@JFNDGJMB@d?3?2?$CFs_init?4txt?$AA@ (0000)
00279940 001e:
	??_C@_0BO@EFIKKGLE@?$CFs_slow_?$CFd_?$CFd_?$CFd_?$CFd_?$CFd_?$CFd?4bin?$AA@ (0000)
00279960 0008:
	__real@3fa26e978d4fdf3c (0000)
00279968 0013:
	??_C@_0BD@BANPCAEP@?5des?5?$CFd?5targ?$CF6I64d?$AA@ (0000)
0027997C 000d:
	??_C@_0N@ONKBMNKN@?5MAINTAIN?5?$CFd?$AA@ (0000)
0027998C 000d:
	??_C@_0N@MLBLHBF@?5RESTORE?5?5?$CFd?$AA@ (0000)
0027999C 000d:
	??_C@_0N@KHJNGILI@?5FAILDOWN?5?$CFd?$AA@ (0000)
002799AC 000d:
	??_C@_0N@KAJPDIPB@?$CI?$CFs?$CF2d?1?$CF2d?$CJ?5?$AA@ (0000)
002799BC 0003:
	??_C@_02LEBOADDI@dn?$AA@ (0000)
002799C0 000c:
	??_C@_0M@FONKJGHM@?$CIok?5?5?5?$CF2d?$CJ?5?$AA@ (0000)
002799CC 0006:
	??_C@_05MPKPLMMK@ignor?$AA@ (0000)
002799D4 0009:
	??_C@_08NGBFCLJE@?$CI?$CFs?$CF2d?$CJ?5?$AA@ (0000)
002799E0 0006:
	??_C@_05KGKFECIJ@fail?5?$AA@ (0000)
002799E8 003a:
	??_C@_0DK@KGLGNIMB@last?$CF6I64d?5init?$CF6I64d?5achv?$CF6I64d@ (0000)
00279A24 0019:
	??_C@_0BJ@GBFFHJIM@?$CF6I64d?$CItarg?$CF6I64d?5?$CFs?$CF2d?$CJ?$AA@ (0000)
00279A40 0009:
	??_C@_08EEAEDAMO@LAPSED?5?5?$AA@ (0000)
00279A4C 0009:
	??_C@_08KMBEHCPI@SYNCED?5?5?$AA@ (0000)
00279A58 0009:
	??_C@_08PLAGFMEO@THROTTLE?$AA@ (0000)
00279A68 004a:
	??_C@_0EK@OBDHCGCK@stuck?5waiting?5for?5VBLANK?5callbac@ (0000)
00279AB4 0006:
	??_C@_05PAONDCEJ@movie?$AA@ (0000)
00279ABC 0019:
	??_C@_0BJ@MFOKCFOP@main_globals?4movie?$DN?$DNNULL?$AA@ (0000)
00279AD8 002a:
	??_C@_0CK@IJIKLLCA@chucky?5was?5here?$CB?5?5NULL?5belongs?5t@ (0000)
00279B04 0031:
	??_C@_0DB@HPIOCFDH@halobeta?5xbox?501?401?414?42342?5Jan?5@ (0000)
00279B38 0038:
	??_C@_0DI@IKBOMHNJ@Couldn?8t?5create?5a?5file?5to?5write?5@ (0000)
00279B70 0011:
	??_C@_0BB@HAHOOKLD@z?3?2last_solo?4txt?$AA@ (0000)
00279B84 0028:
	??_C@_0CI@KPDFMEMF@?$CBmain_globals?4main_menu_scenario@ (0000)
00279BAC 000d:
	??_C@_0N@CBIELCEF@levels?2ui?2ui?$AA@ (0000)
00279BBC 0023:
	??_C@_0CD@IMDNHNHI@congratulations?0?5you?5won?5the?5gam@ (0000)
00279BE0 0014:
	??_C@_0BE@BDDOCJAN@movie?2frame?$CF06d?4tga?$AA@ (0000)
00279BF4 0019:
	??_C@_0BJ@CKCGDFO@error?5opening?5saved?5film?$AA@ (0000)
00279C10 0015:
	??_C@_0BF@LPIHHLNO@?$CFdscreenshot?$CFd?$CFd?4tif?$AA@ (0000)
00279C28 003b:
	??_C@_0DL@LMMABMNB@halobeta?5xbox?501?401?414?42342?5buil@ (0000)
00279C64 001f:
	??_C@_0BP@HEAODFIJ@old?5tags?2internal?5system?5plain?$AA@ (0000)
00279C84 0012:
	??_C@_0BC@FLAPBDJB@end?5of?5saved?5film?$AA@ (0000)
00279C98 0018:
	??_C@_0BI@HBMJOOEK@the?5game?5host?5went?5down?$AA@ (0000)
00307818 0034:
	_global_difficulty_level (0000)
	_player_spawn_count (0004)
	_global_frame_rate_throttle (0006)
	_global_screenshot_size (0008)
00455750 0a63:
	_bss_00455750 (0000)
	_debug_force_frame_rate_update (06d0)
	_debug_no_drawing (06d1)
	_debug_game_save (06d2)
	_debug_frame_rate (06d3)
	_display_framerate (06d4)
	_display_vblank_deltas (06d5)
	_display_precache_progress (06d6)
	_global_screenshot_count (06d8)
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "cseries/profile.h"
#include "cseries/cseries_windows.h"
#include "main.h"
#ifdef HALO_LINUX
#include "frame_timing.h"
#include "tick_thread.h"
#include "view_distance.h"
#include "render_epoch.h"
void platform_log(const char *format, ...);
#endif
#include "real_math.h"
#include "game.h"
#include "game_engine.h"
#include "game/cheats.h"
#include "game/player_control.h"
#include "game/player_control_runtime.h"
#include "game/players.h"
#include "game/local_players.h"
#include "game/player_queues_new.h"
#include "integer_math.h"
#include "main/main_runtime.h"
#include "input.h"
#include "shell.h"
#include "event_manager.h"
#include "telnet_console.h"
#include "ui_widget.h"
#include "editor_stubs.h"
#include "console.h"
#include "debug_keys.h"
#include "scenario.h"
#include "scenario/scenario_definitions.h"
#include "cache/cache_files.h"
#include "cache/predicted_resources.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps_internal.h"
#include "bitmaps/tiff_file.h"
#include "interface/hud.h"
#include "interface/hud_definitions.h"
#include "interface/attract_mode.h"
#include "interface/interface.h"
#include "interface/terminal.h"
#include "saved games/player_profile.h"
#include "saved games/game_state.h"
#include "sound/sound_manager.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_debug.h"
#include "rasterizer/rasterizer_debug_options.h"
#include "bink/bink_playback.h"
#include "main/d3d_intimacy.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h" /* port: a co-op game's level won */
#include "network_coop.h" /* port: port/linux/game/network_coop.c */
#include "camera/director.h"
#include "camera/observer.h"
#include "objects/objects.h"
#include "cutscene/cinematics.h"
#include "effects/player_effects.h"
#include "physics/collision_usage.h"
#include "physics/collision_debug.h"
#include "hs/hs.h"
#ifdef HALO_LINUX
#include "load_profile.h"
#endif
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "tag_files/files.h"

/* ---------- constants */

enum
{
	_bitmap_format_x8r8g8b8 = 10,
};

enum
{
	_text_justification_left = 0,
	_text_justification_right,
};

/* ---------- macros */

/* ---------- structures */

/*
struct _main_globals
{
	long long last_time_clocks; // 0x0
	unsigned long last_time_msec; // 0x8
	long long last_render_clocks; // 0x10
	long long last_vblank_index; // 0x18
	long long last_initial_vblank_index; // 0x20
	long long last_achievable_vblank_index; // 0x28
	long long last_present_vblank_index; // 0x30
	unsigned char did_time_overflow_occur; // 0x38
	float seconds_elapsed; // 0x3C
	short connection; // 0x40
	unsigned short screenshot_identifier; // 0x42
	bitmap_data *movie; // 0x44
	long recording_start_tick; // 0x48
	long recording_stop_tick; // 0x4C
	long recording_frame_index; // 0x50
	float recording_dt; // 0x54
	unsigned char reset_map; // 0x58
	unsigned char rename_map; // 0x59
	unsigned char revert_map; // 0x5A
	unsigned char skip_cinematic; // 0x5B
	unsigned char save_map; // 0x5C
	unsigned char save_map_safely; // 0x5D
	unsigned char save_map_timeout; // 0x5E
	unsigned char saving_map; // 0x5F
	long ticks_until_next_save_check; // 0x60
	long ticks_unable_to_save; // 0x64
	unsigned long map_change_load_timer; // 0x68
	short safe_intervals; // 0x6C
	unsigned char won_map; // 0x6E
	unsigned char lost_map; // 0x6F
	unsigned char respawn; // 0x70
	unsigned char save_core; // 0x71
	unsigned char load_core; // 0x72
	unsigned char load_core_at_startup; // 0x73
	short switch_to_structure_bsp_index; // 0x74
	unsigned char main_menu_scenario_loaded; // 0x76
	unsigned char want_to_be_at_main_menu; // 0x77
	unsigned char run_xdemos; // 0x78
	unsigned char fade_to_dashboard; // 0x79
	unsigned char exit_to_dashboard; // 0x7A
	unsigned char want_to_exit; // 0x7B
	long idle_timeout; // 0x7C
	long idle_last_interesting; // 0x80
	long idle_last_activity; // 0x84
	unsigned char playback_last_recording; // 0x88
	unsigned char halt_time_scale; // 0x89
	unsigned char restart_time; // 0x8A
	unsigned char load_last_solo_level; // 0x8B
	unsigned char cutscene_skip; // 0x8C
	short skip_ticks; // 0x8E
	short loss_timer; // 0x90
	short respawn_timer; // 0x92
	unsigned char queue_map; // 0x94
	unsigned char pad0[3]; // 0x95
	unsigned char solo_try_and_load_from_persistent_storage; // 0x98
	char soloplayer_map_name[256]; // 0x99
	char multiplayer_map_name[256]; // 0x199
	char queued_map_name[256]; // 0x299
	unsigned char want_to_connect; // 0x399
	char connect_address[32]; // 0x39A
	char connect_password[9]; // 0x3BA
	short vblank_interval_current; // 0x3C4
	short vblank_interval_minimum; // 0x3C6
	unsigned char vblank_interval_held; // 0x3C8
	short vblank_failure_count[6]; // 0x3CA
	long long vblank_last_failure_time[6]; // 0x3D8
	unsigned long *vblank_flip_counter; // 0x408
	short vblank_flip_delta_next_index; // 0x40C
	short vblank_flip_deltas[15]; // 0x40E
};
*/

struct _main_globals
{
	unsigned long frame_start_milliseconds;
	byte reserved04[4];
	union
	{
		__int64 rasterizer_target_index;
		struct
		{
			unsigned long rasterizer_frame_index;
			unsigned long rasterizer_vertical_blank_index;
		};
	};
	__int64 rasterizer_initial_index;
	__int64 rasterizer_throttle_start_index;
	__int64 rasterizer_throttle_end_index;
	real seconds_elapsed;
	short connection;
	unsigned short screenshot_identifier;
	struct bitmap_data *movie;
	long recording_start_tick;
	long recording_stop_tick;
	long recording_frame_index;
	real recording_dt;
	boolean reset_map;
	boolean defer_map_change;
	boolean revert_map;
	boolean skip_cinematic;
	boolean saving_map;
	boolean save_map_safely;
	boolean save_map_timeout;
	boolean save_map_completed;
	long ticks_until_next_save_check;
	long ticks_unable_to_save;
	unsigned long map_change_load_timer;
	short safe_intervals;
	boolean won_map;
	boolean lost_map;
	boolean respawn;
	boolean save_core;
	boolean load_core;
	boolean load_core_at_startup;
	short switch_to_structure_bsp_index;
	boolean main_menu_scenario_loaded;
	boolean want_to_be_at_main_menu;
	boolean run_xdemos;
	boolean playback_last_recording;
	boolean halt_time_scale;
	boolean restart_time;
	boolean load_last_solo_level;
	boolean skip;
	short skip_ticks;
	short loss_timer;
	short respawn_timer;
	boolean queue_map;
	byte __unknown121[0x3];
	boolean allow_persistent_storage;
	char soloplayer_map_name[256];
	char multiplayer_map_name[256];
	char queued_map_name[256];
	char core_name[64];
	byte reserved3B5;
	short vblank_interval_current;
	short vblank_interval_requested;
	boolean vblank_interval_held;
	byte reserved3BB;
	short vblank_failure_counts[6];
	__int64 vblank_last_failure_indices[6];
	volatile unsigned int *d3d_flip_count;
	short vblank_flip_delta_index;
	short vblank_flip_deltas[15];
	char vblank_debug_string[0x200];
	byte reserved61C[4];
};

typedef char main_hud_globals_font_tag_index_offset_assert[
	offsetof(struct hud_globals_definition, messaging.single_player_font.index) == 0x54 ? 1 : -1];

typedef char main_globals_size_assert[
	sizeof(struct _main_globals) == 0x620 ? 1 : -1];
typedef char main_globals_frame_start_milliseconds_offset_assert[
	offsetof(struct _main_globals, frame_start_milliseconds) == 0x00 ? 1 : -1];
typedef char main_globals_rasterizer_target_index_offset_assert[
	offsetof(struct _main_globals, rasterizer_target_index) == 0x08 ? 1 : -1];
typedef char main_globals_rasterizer_initial_index_offset_assert[
	offsetof(struct _main_globals, rasterizer_initial_index) == 0x10 ? 1 : -1];
typedef char main_globals_rasterizer_throttle_start_index_offset_assert[
	offsetof(struct _main_globals, rasterizer_throttle_start_index) == 0x18 ? 1 : -1];
typedef char main_globals_rasterizer_throttle_end_index_offset_assert[
	offsetof(struct _main_globals, rasterizer_throttle_end_index) == 0x20 ? 1 : -1];
typedef char main_globals_seconds_elapsed_offset_assert[
	offsetof(struct _main_globals, seconds_elapsed) == 0x28 ? 1 : -1];
typedef char main_globals_connection_offset_assert[
	offsetof(struct _main_globals, connection) == 0x2C ? 1 : -1];
typedef char main_globals_movie_offset_assert[
	offsetof(struct _main_globals, movie) == 0x30 ? 1 : -1];
typedef char main_globals_defer_map_change_offset_assert[
	offsetof(struct _main_globals, defer_map_change) == 0x45 ? 1 : -1];
typedef char main_globals_reset_map_offset_assert[
	offsetof(struct _main_globals, reset_map) == 0x44 ? 1 : -1];
typedef char main_globals_revert_map_offset_assert[
	offsetof(struct _main_globals, revert_map) == 0x46 ? 1 : -1];
typedef char main_globals_skip_cinematic_offset_assert[
	offsetof(struct _main_globals, skip_cinematic) == 0x47 ? 1 : -1];
typedef char main_globals_saving_map_offset_assert[
	offsetof(struct _main_globals, saving_map) == 0x48 ? 1 : -1];
typedef char main_globals_save_map_safely_offset_assert[
	offsetof(struct _main_globals, save_map_safely) == 0x49 ? 1 : -1];
typedef char main_globals_won_map_offset_assert[
	offsetof(struct _main_globals, won_map) == 0x5A ? 1 : -1];
typedef char main_globals_halt_time_scale_offset_assert[
	offsetof(struct _main_globals, halt_time_scale) == 0x66 ? 1 : -1];
typedef char main_globals_respawn_timer_offset_assert[
	offsetof(struct _main_globals, respawn_timer) == 0x6E ? 1 : -1];
typedef char main_globals_loss_timer_offset_assert[
	offsetof(struct _main_globals, loss_timer) == 0x6C ? 1 : -1];
typedef char main_globals_allow_persistent_storage_offset_assert[
	offsetof(struct _main_globals, allow_persistent_storage) == 0x74 ? 1 : -1];
typedef char main_globals_soloplayer_map_name_offset_assert[
	offsetof(struct _main_globals, soloplayer_map_name) == 0x75 ? 1 : -1];
typedef char main_globals_multiplayer_map_name_offset_assert[
	offsetof(struct _main_globals, multiplayer_map_name) == 0x175 ? 1 : -1];
typedef char main_globals_core_name_offset_assert[
	offsetof(struct _main_globals, core_name) == 0x375 ? 1 : -1];
typedef char main_globals_vblank_interval_current_offset_assert[
	offsetof(struct _main_globals, vblank_interval_current) == 0x3B6 ? 1 : -1];
typedef char main_globals_vblank_interval_requested_offset_assert[
	offsetof(struct _main_globals, vblank_interval_requested) == 0x3B8 ? 1 : -1];
typedef char main_globals_vblank_interval_held_offset_assert[
	offsetof(struct _main_globals, vblank_interval_held) == 0x3BA ? 1 : -1];
typedef char main_globals_vblank_failure_counts_offset_assert[
	offsetof(struct _main_globals, vblank_failure_counts) == 0x3BC ? 1 : -1];
typedef char main_globals_vblank_last_failure_indices_offset_assert[
	offsetof(struct _main_globals, vblank_last_failure_indices) == 0x3C8 ? 1 : -1];
typedef char main_globals_d3d_flip_count_offset_assert[
	offsetof(struct _main_globals, d3d_flip_count) == 0x3F8 ? 1 : -1];
typedef char main_globals_vblank_flip_delta_index_offset_assert[
	offsetof(struct _main_globals, vblank_flip_delta_index) == 0x3FC ? 1 : -1];
typedef char main_globals_vblank_flip_deltas_offset_assert[
	offsetof(struct _main_globals, vblank_flip_deltas) == 0x3FE ? 1 : -1];
typedef char main_globals_vblank_debug_string_offset_assert[
	offsetof(struct _main_globals, vblank_debug_string) == 0x41C ? 1 : -1];

struct game_options
{
	unsigned long flags;
	short code_version;
	short difficulty;
	unsigned long random_seed;
	char map_name[256];
};

typedef char game_options_size_assert[
	sizeof(struct game_options) == 0x10C ? 1 : -1];

#pragma pack(push, 1)
struct _screenshot_and_framerate_globals
{
	short count;
	byte reserved002[6];
	struct render_window windows[MAXIMUM_WINDOWS + 1];
	real framerate_samples[8];
	word framerate_flags;
	char framerate_sample_index;
	boolean framerate_active;
	char framerate_counter;
	boolean framerate_reset;
	boolean halt_recursion_lock;
};
#pragma pack(pop)

typedef char screenshot_and_framerate_globals_size_assert[
	sizeof(struct _screenshot_and_framerate_globals) == 0x38B ? 1 : -1];

#ifdef HALO_LINUX
void network_test_update(boolean main_menu_loaded, real seconds);
/* the Vita settings panel's Host a game and Join a game
(port/linux/game/system_link_shortcut.c) */
void system_link_shortcut_update(boolean main_menu_loaded);
#endif

/* ---------- prototypes */

static long sort_desired_local_player_controllers(
	short const *a,
	short const *b);
static void main_setup_connection(
	void);
static void main_initialize_time(
	void);
static void main_skip_private(
	void);
static void main_update_time(
	void);
static void main_frame_rate_debug(
	void);

static void main_new_map(
	struct game_options *options);
extern void scripted_camera_set(
	word camera_point_index0,
	word camera_point_index1,
	long transition_time);

/* ---------- globals */

short global_difficulty_level = 1;
short player_spawn_count = 1;
boolean global_frame_rate_throttle = TRUE;
short global_screenshot_size = 1;

static char const *scenario_paths[10] =
{
	"levels\\a10\\a10",
	"levels\\a30\\a30",
	"levels\\a50\\a50",
	"levels\\b30\\b30",
	"levels\\b40\\b40",
	"levels\\c10\\c10",
	"levels\\c20\\c20",
	"levels\\c40\\c40",
	"levels\\d20\\d20",
	"levels\\d40\\d40"
};

static struct _main_globals main_globals = { 0 };
/* Keep the original globals layout; these clocks belong to pending requests. */
static long main_loss_last_tick;
static long main_respawn_last_tick;
boolean debug_force_frame_rate_update = FALSE;
boolean debug_no_drawing = FALSE;
boolean debug_game_save = FALSE;
boolean debug_frame_rate = FALSE;
boolean display_framerate = FALSE;
boolean display_vblank_deltas = FALSE;
boolean display_precache_progress = FALSE;
struct _screenshot_and_framerate_globals global_screenshot_count = { 0 };

/* ---------- public code */

real main_get_seconds_elapsed(
	void)
{
	return main_globals.seconds_elapsed;
}

boolean gamepad_button_is_down(
	short button_index)
{
	short gamepad_index;
	boolean result = FALSE;

	match_assert("c:\\halo\\SOURCE\\main\\main.c", 245,
		button_index>=0 && button_index<NUMBER_OF_GAMEPAD_BUTTONS);
	gamepad_index = 0;

	do
	{
		if (input_has_gamepad(gamepad_index))
			break;

		gamepad_index++;
	}
	while (gamepad_index < MAXIMUM_GAMEPADS);

	if (gamepad_index < MAXIMUM_GAMEPADS)
	{
		struct gamepad_state const *gamepad = input_get_gamepad_state(gamepad_index);

		result = gamepad->buttons[button_index] > 0;
	}

	return result;
}

static void create_local_players(
	void)
{
	short used_controllers[MAXIMUM_GAMEPADS];
	short default_controllers[MAXIMUM_GAMEPADS];
	short desired_controllers[MAXIMUM_GAMEPADS];
	long i;
	long j;
	long player;
	short gamepad_index;

	if (main_globals.main_menu_scenario_loaded)
	{
		player = player_new(0, NONE, 0, NULL);
		local_player_set_player_index(
			0,
			player);
	}
	else
	{
		csmemset(used_controllers, NONE, sizeof(used_controllers));
		csmemset(desired_controllers, NONE, sizeof(desired_controllers));
		default_controllers[0] = 0;
		default_controllers[1] = 1;
		default_controllers[2] = 2;
		default_controllers[3] = 3;

		match_assert(
			"c:\\halo\\SOURCE\\main\\main.c",
			741,
			game_connection() == _game_connection_local);
		for (i = 0; i < player_spawn_count; i++)
		{
			gamepad_index =
				player_ui_get_single_player_local_player_controller(i);
			desired_controllers[i] = gamepad_index;
			if (desired_controllers[i] == NONE)
				desired_controllers[i] = default_controllers[i];
			match_assert(
				"c:\\halo\\SOURCE\\main\\main.c",
				755,
				(desired_controllers[i]>=0) && (desired_controllers[i]<MAXIMUM_GAMEPADS));

			if (used_controllers[desired_controllers[i]] != NONE)
			{
				for (j = 0; j < MAXIMUM_GAMEPADS; j++)
				{
					if (used_controllers[j] == NONE)
					{
						desired_controllers[i] = (short)j;
						used_controllers[(short)j] = (short)j;
						break;
					}
				}
				match_assert(
					"c:\\halo\\SOURCE\\main\\main.c",
					768,
					j<MAXIMUM_GAMEPADS);
			}
			else
			{
				used_controllers[desired_controllers[i]] = desired_controllers[i];
			}
		}

		qsort(
			desired_controllers,
			MAXIMUM_GAMEPADS,
			sizeof(*desired_controllers),
			(int (__cdecl *)(void const *, void const *))sort_desired_local_player_controllers);
		for (i = 0; i < player_spawn_count; i++)
		{
			gamepad_index = desired_controllers[i];
			match_assert(
				"c:\\halo\\SOURCE\\main\\main.c",
				784,
				(gamepad_index>=0) && (gamepad_index<MAXIMUM_GAMEPADS));
			player = player_new(0, NONE, gamepad_index, NULL);
			local_player_set_player_index(
				gamepad_index,
				player);
		}
	}

	return;
}

void game_connection_set(
	short connection)
{
	main_globals.connection = connection;
	return;
}

short game_connection(
	void)
{
	return main_globals.connection;
}

#ifdef HALO_LINUX
/* (port) whether the arrays data.c holds to the Xbox's sizes are held now:
a local game, unless HALO_XBOX_PARTICLE_LIMITS=0 (data.c datum_new_limit) */
int halo_local_limits_active(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		extern char *getenv(const char *name);
		extern int atoi(const char *text);
		const char *setting = getenv("HALO_XBOX_PARTICLE_LIMITS");

		enabled = !setting || atoi(setting) != 0;
	}
	return enabled && game_connection() == _game_connection_local;
}
#endif

void main_disallow_persistent_storage(
	void)
{
	main_globals.allow_persistent_storage = FALSE;
	return;
}

void main_defer_map_map_change(
	void)
{
	main_globals.defer_map_change = FALSE;
	return;
}

char *main_get_map_name(
	void)
{
	return main_globals.soloplayer_map_name;
}

char *main_get_multiplayer_map_name(
	void)
{
	return main_globals.multiplayer_map_name;
}

short main_get_difficulty(
	void)
{
	return global_difficulty_level;
}

boolean main_saving_map(
	void)
{
	return main_globals.saving_map;
}

#ifdef HALO_LINUX
static void main_checkpoint_cancelled(char const *by);
static void main_map_request_log(char const *what);
#endif

void main_save_cancel(
	void)
{
#ifdef HALO_LINUX
	main_checkpoint_cancelled("game_save_cancel");
#endif
	main_globals.saving_map = FALSE;
	return;
}

void main_won_map(
	void)
{
	main_globals.saving_map = FALSE;
	main_globals.won_map = TRUE;
	return;
}

void main_lost_map(
	void)
{
#ifdef HALO_LINUX
	if (!main_globals.lost_map)
		main_map_request_log("game lost (back to the last checkpoint in 3 s)");
	main_checkpoint_cancelled("a lost game");
#endif
	if (!main_globals.lost_map)
	{
		main_globals.loss_timer = 0;
		main_loss_last_tick = game_time_get();
	}
	main_globals.saving_map = FALSE;
	main_globals.lost_map = TRUE;
	return;
}

void main_stop_time(
	void)
{
	main_globals.halt_time_scale = FALSE;
	main_globals.restart_time = FALSE;
	return;
}

void main_start_time(
	void)
{
	main_globals.restart_time = TRUE;
	return;
}

void main_reset_map(
	void)
{
#ifdef HALO_LINUX
	if (!main_globals.reset_map)
		main_map_request_log("map reset");
#endif
	main_globals.switch_to_structure_bsp_index = NONE;
	main_globals.saving_map = FALSE;
	main_globals.reset_map = TRUE;
	main_globals.lost_map = FALSE;
	return;
}

void main_revert_map(
	void)
{
#ifdef HALO_LINUX
	if (!main_globals.revert_map)
		main_map_request_log("revert to the last checkpoint");
	main_checkpoint_cancelled("a revert");
#endif
	main_globals.switch_to_structure_bsp_index = NONE;
	main_globals.saving_map = FALSE;
	main_globals.revert_map = TRUE;
	main_globals.lost_map = FALSE;
	return;
}

void main_skip_cinematic(
	void)
{
#ifdef HALO_LINUX
	main_checkpoint_cancelled("a cinematic skip");
#endif
	main_globals.switch_to_structure_bsp_index = NONE;
	main_globals.saving_map = FALSE;
	main_globals.skip_cinematic = TRUE;
	return;
}

#ifdef HALO_LINUX
static void main_checkpoint_asked(char const *how);
#endif

void main_save_map_nonsafe(
	void)
{
#ifdef HALO_LINUX
	main_checkpoint_asked("game_save_totally_unsafe");
#endif
	main_globals.saving_map = TRUE;
	main_globals.save_map_safely = FALSE;
	return;
}

void main_respawn(
	boolean in_multiplayer)
{
	if (!main_globals.respawn)
	{
		main_globals.respawn_timer = 0;
		main_respawn_last_tick = game_time_get();
	}
	main_globals.respawn = TRUE;
	if (in_multiplayer)
		main_globals.respawn_timer = 92;
	return;
}

void main_save_core(
	void)
{
	main_globals.save_core = TRUE;
	csstrcpy(main_globals.core_name, "core.bin");
	return;
}

void main_save_core_name(
	char const *core_name)
{
	match_vwarn("c:\\halo\\SOURCE\\main\\main.c", 0x3a5, csstrlen(core_name) < 64, "warning, core file name will be truncated to 63 characters");
	csstrncpy(main_globals.core_name, core_name, 63);
	main_globals.save_core = TRUE;
	return;
}

void main_load_core(
	void)
{
	main_globals.load_core = TRUE;
	csstrcpy(main_globals.core_name, "core.bin");
	return;
}

void main_load_core_at_startup(
	void)
{
	main_globals.load_core_at_startup = TRUE;
	csstrcpy(main_globals.core_name, "core.bin");
	return;
}

void main_load_core_name(
	char const *core_name)
{
	match_vwarn("c:\\halo\\SOURCE\\main\\main.c", 0x3c9, csstrlen(core_name) < 64, "warning, core file name will be truncated to 63 characters");
	csstrncpy(main_globals.core_name, core_name, 63);
	main_globals.load_core = TRUE;
	return;
}

void main_load_core_name_at_startup(
	char const *core_name)
{
	match_vwarn("c:\\halo\\SOURCE\\main\\main.c", 0x3d7, csstrlen(core_name) < 64, "warning, core file name will be truncated to 63 characters");
	csstrncpy(main_globals.core_name, core_name, 63);
	main_globals.load_core_at_startup = TRUE;
	return;
}

void main_switch_structure_bsp(
	short structure_bsp_index)
{
	struct scenario *scenario = global_scenario_get();
	if (structure_bsp_index < 0 || structure_bsp_index >= scenario->structure_bsp_references.count)
	{
		console_warning("tried to switch to invalid structure-bsp %d", structure_bsp_index);
		return;
	}
	if (structure_bsp_index == global_structure_bsp_index)
	{
		console_warning("tried to switch to current structure-bsp %d", structure_bsp_index);
		return;
	}
	main_globals.switch_to_structure_bsp_index = structure_bsp_index;
	hud_load(TRUE);
	return;
}

void main_goto_main_menu(
	void)
{
	main_globals.switch_to_structure_bsp_index = NONE;
	main_globals.saving_map = FALSE;
	main_globals.want_to_be_at_main_menu = TRUE;
	return;
}

static void main_queue_map_private(
	void)
{
	real progress;
	if (cache_files_precache_in_progress() && cache_files_precache_map_status(&progress) == 1)
		cache_files_precache_map_end();
	if (!cache_files_precache_in_progress())
	{
		cache_files_precache_map_begin(main_globals.queued_map_name, FALSE);
		main_globals.queue_map = FALSE;
	}
	return;
}

void main_menu_precache_resources(
	void)
{
	struct scenario *scenario = global_scenario_get();
	if (scenario)
	{
		match_assert("c:\\halo\\SOURCE\\main\\main.c", 0x46d, scenario->type==_scenario_type_main_menu);
		predicted_resources_precache(&scenario->predicted_ui_resources);
	}
	return;
}

void main_menu_unload(
	void)
{
	ui_stop_main_menu_music();
	main_menu_active(FALSE);
	main_globals.main_menu_scenario_loaded = FALSE;
	return;
}

void main_menu_load(
	void)
{
	if (!main_globals.main_menu_scenario_loaded)
		main_load_ui_scenario(FALSE);
	main_screen_shell_load();
	main_menu_precache_resources();
	update_server_delete();
	update_server_new();
	update_server_start();
	game_time_dispose_from_old_map();
	game_time_initialize_for_new_map();
	game_time_start();
	hs_runtime_dispose_from_old_map();
	hs_runtime_initialize_for_new_map();
	main_globals.want_to_be_at_main_menu = FALSE;

	return;
}

static void screenshot_record(
	struct bitmap_data *screen,
	struct file_reference *file)
{
	char const *error_message = tiff_export(file, screen);

	if (error_message)
		error(_error_silent, error_message);
	return;
}

void set_window_camera_values(
	struct render_window *window,
	struct observer_result const *observer)
{
	if (observer)
	{
		window->rasterizer_camera.position = observer->position;
		window->rasterizer_camera.forward = observer->forward;
		window->rasterizer_camera.up = observer->up;
		window->rasterizer_camera.vertical_field_of_view =
			2.0f * arctangent(
				0.75f * render_camera_get_adjusted_field_of_view_tangent(
					observer->field_of_view),
				1.0f);

		if (window->local_player_index != NONE &&
			!console_is_active() &&
			!game_time_get_paused() &&
			director_get_perspective(window->local_player_index) !=
				_director_perspective_neutral)
		{
			real_matrix4x3 effect_matrix;
			real_matrix4x3 view_matrix;

			player_effect_get_camera_effect_matrix(
				window->local_player_index,
				&effect_matrix);
			matrix4x3_from_point_and_vectors(
				&view_matrix,
				&observer->position,
				&observer->forward,
				&observer->up);
			matrix4x3_multiply(&view_matrix, &effect_matrix, &view_matrix);
			matrix4x3_to_point_and_vectors(
				&view_matrix,
				&window->rasterizer_camera.position,
				&window->rasterizer_camera.forward,
				&window->rasterizer_camera.up);
		}
	}
	else
	{
		window->rasterizer_camera.position = *global_origin3d;
		window->rasterizer_camera.forward = *global_forward3d;
		window->rasterizer_camera.up = *global_up3d;
		window->rasterizer_camera.vertical_field_of_view =
			2.0f * arctangent(
				0.75f * render_camera_get_adjusted_field_of_view_tangent(
					DEGREES_TO_RADIANS(80.0f)),
				1.0f);
	}

	window->rasterizer_camera.mirrored = FALSE;
	window->rasterizer_camera.z_near = rasterizer_globals.near_clip_distance;
	window->rasterizer_camera.z_far = rasterizer_globals.far_clip_distance;
	if (!debug_render_freeze)
		window->render_camera = window->rasterizer_camera;

	return;
}

void main_present_frame(
	void)
{
	struct file_reference reference;
	char path[512];

	render_frame_present(NULL, main_globals.movie);
	if (global_screenshot_count.count <= 0 && main_globals.movie)
	{
		_snprintf(
			path,
			NUMBEROF(path),
			"movie\\frame%06d.tga",
			main_globals.recording_frame_index++);
		file_reference_create_from_path(&reference, path, FALSE);
		screenshot_record(main_globals.movie, &reference);
	}

	return;
}

void main_menu_ensure_player_queues_exist(
	void)
{
	update_server_delete();
	update_server_new();
	update_server_start();
	return;
}

boolean main_menu_fade_active(
	void)
{
	return main_globals.map_change_load_timer != 0;
}

void main_menu_switch_to_single_player(
	void)
{
	main_globals.defer_map_change = TRUE;
	return;
}

void main_set_game_connection_to_film_playback(
	void)
{
	main_globals.playback_last_recording = TRUE;
	return;
}

short main_get_solo_level_from_name(
	char const *name)
{
	char lower_name[128] = { 0 };
	short level;

	csstrncpy(lower_name, name, NUMBEROF(lower_name) - 1);
	lower_name[NUMBEROF(lower_name) - 1] = 0;
	strlwr(lower_name);

	if (strstr(lower_name, "a10"))
		level = 0;
	else if (strstr(lower_name, "a30"))
		level = 1;
	else if (strstr(lower_name, "a50"))
		level = 2;
	else if (strstr(lower_name, "b30"))
		level = 3;
	else if (strstr(lower_name, "b40"))
		level = 4;
	else if (strstr(lower_name, "c10"))
		level = 5;
	else if (strstr(lower_name, "c20"))
		level = 6;
	else if (strstr(lower_name, "c40"))
		level = 7;
	else if (strstr(lower_name, "d20"))
		level = 8;
	else
		level = strstr(lower_name, "d40") ? 9 : NONE;

	return level;
}

short main_get_current_solo_level(
	void)
{
	return main_get_solo_level_from_name(main_globals.soloplayer_map_name);
}

char const *main_get_solo_level_name(
	short level)
{
	char const *result = NULL;

	if (level >= 0 && level < NUMBEROF(scenario_paths))
		result = scenario_paths[level];

	return result;
}

void main_run_demos(
	void)
{
	main_globals.run_xdemos = TRUE;
	return;
}

static void compute_subframe_counts(
	long num_players,
	long *horizontal_count,
	long *vertical_count)
{
	long horizontal = 1;
	long vertical = 1;
	boolean split_horizontally = TRUE;

	match_assert(
		"c:\\halo\\SOURCE\\main\\main.c",
		1308,
		num_players>0);
	while (horizontal*vertical<num_players)
	{
		boolean add_column = split_horizontally ? horizontal<vertical : horizontal<=vertical;

		if (add_column)
		{
			horizontal++;
		}
		else
		{
			horizontal = 1;
			vertical++;
		}
	}

	*horizontal_count = horizontal;
	*vertical_count = vertical;
	return;
}

void compute_window_bounds(
	long player_index,
	long num_players,
	rectangle2d *pixel_bounds,
	rectangle2d *safe_frame_bounds)
{
	long vertical_count;
	long horizontal_count;
	long horizontal_index;
	long vertical_index;
	long subframe_width;
	long subframe_height;
	short safe_frame_inset;
	boolean first_player_spans_two_columns;

	match_assert(
		"c:\\halo\\SOURCE\\main\\main.c",
		1359,
		player_index<num_players);
	safe_frame_inset = num_players>1 ? 4 : 0;
	first_player_spans_two_columns = FALSE;
	compute_subframe_counts(
		num_players,
		&horizontal_count,
		&vertical_count);

	if (horizontal_count*vertical_count>num_players)
	{
		if (!player_index)
			first_player_spans_two_columns = TRUE;
		else
			player_index++;
	}

	vertical_index = player_index/horizontal_count;
	horizontal_index = player_index-vertical_index*horizontal_count;
	match_assert(
		"c:\\halo\\SOURCE\\main\\main.c",
		1390,
		vertical_index>=0 && vertical_index<vertical_count);
	match_assert(
		"c:\\halo\\SOURCE\\main\\main.c",
		1391,
		horizontal_index>=0 && horizontal_index<horizontal_count);

	subframe_height =
		(rasterizer_globals.reserved04.frame_bounds.y1-
			rasterizer_globals.reserved04.frame_bounds.y0)/vertical_count;
	subframe_width =
		(rasterizer_globals.reserved04.frame_bounds.x1-
			rasterizer_globals.reserved04.frame_bounds.x0)/horizontal_count;
	subframe_width *= first_player_spans_two_columns ? 2 : 1;

	safe_frame_bounds->x0 =
		subframe_width*horizontal_index+
		rasterizer_globals.reserved04.frame_bounds.x0;
	safe_frame_bounds->x1 =
		subframe_width*(horizontal_index+1)+
		rasterizer_globals.reserved04.frame_bounds.x0;
	safe_frame_bounds->y0 =
		subframe_height*vertical_index+
		rasterizer_globals.reserved04.frame_bounds.y0;
	safe_frame_bounds->y1 =
		subframe_height*(vertical_index+1)+
		rasterizer_globals.reserved04.frame_bounds.y0;
	*pixel_bounds = *safe_frame_bounds;

	safe_frame_bounds->x0 += horizontal_index*safe_frame_inset;
	safe_frame_bounds->x1 -= (horizontal_index==0)*safe_frame_inset;
	safe_frame_bounds->y0 += vertical_index*safe_frame_inset;
	safe_frame_bounds->y1 -= (vertical_index==0)*safe_frame_inset;

	if (!horizontal_index)
		pixel_bounds->x0 = rasterizer_globals.reserved04.screen_bounds.x0;
	if ((first_player_spans_two_columns!=FALSE)+horizontal_index+1==horizontal_count)
		pixel_bounds->x1 = rasterizer_globals.reserved04.screen_bounds.x1;
	if (!vertical_index)
		pixel_bounds->y0 = rasterizer_globals.reserved04.screen_bounds.y0;
	if (vertical_index+1==vertical_count)
		pixel_bounds->y1 = rasterizer_globals.reserved04.screen_bounds.y1;

	return;
}

short main_get_window_count(
	void)
{
	boolean single_window = game_engine_force_single_screen() || cinematic_in_progress();

	return single_window ? 1 : PIN(local_player_count(), 1, MAXIMUM_WINDOWS);
}

#ifdef HALO_LINUX
static void main_checkpoint_log_new_map(void);
#endif

static void main_new_map(
	struct game_options *options)
{
#ifdef HALO_LINUX
	unsigned long long new_map_started = halo_load_profile_now();
	unsigned long long game_load_started;
	boolean loaded;

#endif
	input_flush();
#ifdef HALO_LINUX
	platform_log("new map: loading");
	main_checkpoint_log_new_map();
	game_load_started = halo_load_profile_now();
	loaded = game_load(options);
	halo_load_profile_add(_halo_load_game_load, game_load_started, 0);
	if (loaded)
#else
	if (game_load(options))
#endif
	{
#ifdef HALO_LINUX
		platform_log("new map: loaded, initializing");
#endif
		game_initialize_for_new_map();
	}
#ifdef HALO_LINUX
	/* port: a custom map that cannot be loaded (memory, a protected map, a
	missing resource map): the player is told why, and the menu loaded,
	rather than the game stopped (port/linux/game/custom_edition_cache.c) */
	else if (custom_edition_cache_load_failure_show(options->map_name))
	{
		errors_clear();
		error(_error_silent, "game_load() failed.");
		platform_log("new map: %s could not be loaded; back to the main menu", options->map_name);
		main_globals.want_to_be_at_main_menu = TRUE;
		main_menu_load();

		return;
	}
#endif
	else
	{
		error(_error_immediate, "game_load() failed.");
	}

	if (!errors_handle())
	{
#ifdef HALO_LINUX
		platform_log("new map: players");
#endif
		create_local_players();
		game_time_start();
	}
	else
	{
		error(_error_immediate, "main_new_map() failed.");
	}
#ifdef HALO_LINUX
	platform_log("new map: initial pulse");
#endif
	game_initial_pulse();
#ifdef HALO_LINUX
	platform_log("new map: ready");
#endif

	main_globals.reset_map = FALSE;
	main_globals.defer_map_change = FALSE;
	main_globals.revert_map = FALSE;
	main_globals.skip_cinematic = FALSE;
	main_globals.saving_map = FALSE;
	main_globals.won_map = FALSE;
	main_globals.lost_map = FALSE;
	main_globals.respawn = FALSE;
	main_globals.save_core = FALSE;
	main_globals.switch_to_structure_bsp_index = NONE;
	main_globals.load_core = main_globals.load_core_at_startup;
	main_globals.load_core_at_startup = FALSE;

	/* port: never a network game (co-op on a campaign level): the campaign
	save is single player's, and a network game neither resumes nor writes
	it (pause_game_quit_to_main_menu) */
	if (main_globals.allow_persistent_storage && main_globals.connection == _game_connection_local)
		game_state_try_and_load_from_persistent_storage();
	ui_widgets_disable_pause_game(30);
#ifdef HALO_LINUX
	halo_load_profile_add(_halo_load_new_map, new_map_started, 0);
#endif

	return;
}

static void main_change_map_name(
	void)
{
	if (main_globals.main_menu_scenario_loaded == TRUE)
	{
		if (!main_globals.map_change_load_timer)
		{
			if (ui_main_menu_music_active() == TRUE)
			{
				main_globals.map_change_load_timer =
					main_globals.frame_start_milliseconds + 1000;
				main_screen_shell_begin_fade(1000);
				ui_widgets_inhibit_processing(TRUE);
				ui_widgets_set_fade_value(0.0f);
			}
		}
		else
		{
			unsigned long remaining_milliseconds =
				main_globals.map_change_load_timer - main_globals.frame_start_milliseconds;

			ui_widgets_set_fade_value(
				1.0f - (real)remaining_milliseconds * 0.001f);
		}
	}
	else
	{
		main_globals.map_change_load_timer = 0;
	}

	if (main_globals.frame_start_milliseconds >= main_globals.map_change_load_timer)
	{
		ui_widgets_set_fade_value(-1.0f);
		ui_stop_main_menu_music();
		main_menu_active(FALSE);
		main_globals.main_menu_scenario_loaded = FALSE;
		ui_widgets_inhibit_processing(FALSE);

		if (game_in_progress() && main_globals.connection == _game_connection_local)
		{
			struct game_options options;
			short local_player_index;

			game_options_new(&options);
			csstrncpy(
				options.map_name,
				main_globals.soloplayer_map_name,
				NUMBEROF(options.map_name) - 1);
			options.map_name[NUMBEROF(options.map_name) - 1] = 0;
			options.difficulty = global_difficulty_level;
			game_dispose_from_old_map();
			game_precache_new_map(options.map_name, TRUE);
			game_unload();
			main_new_map(&options);

			for (local_player_index = 0;
				local_player_index < player_spawn_count;
				local_player_index++)
			{
				player_profile_save_last_level_played(local_player_index);
			}
		}

		main_globals.map_change_load_timer = 0;
	}

	return;
}

void main_set_multiplayer_map_name(
	char const *map_name)
{
	csstrncpy(main_globals.multiplayer_map_name, map_name, NUMBEROF(main_globals.multiplayer_map_name) - 1);
	main_globals.multiplayer_map_name[NUMBEROF(main_globals.multiplayer_map_name) - 1] = 0;
	cache_files_give_time_to_precache(main_globals.multiplayer_map_name);
	return;
}

void main_set_difficulty(
	short difficulty)
{
	if (difficulty >= _game_difficulty_level_easy && difficulty < NUMBER_OF_GAME_DIFFICULTY_LEVELS)
		global_difficulty_level = difficulty;
	return;
}

static long sort_desired_local_player_controllers(
	short const *a,
	short const *b)
{
	short value_a = *a;
	short value_b = *b;
	if (value_a == NONE && value_b != value_a)
		return 1;
	if (value_a != NONE && value_b == NONE)
		return -1;
	if (value_a > value_b)
		return 1;
	return (value_a >= value_b) - 1;
}

#ifdef HALO_LINUX
/* (port) The checkpoints in the log: who asked for one, how long it waited
and what game_safe_to_save refused meanwhile (game.c), and when it gave up.
A level's scripts ask with game_save (gives up after 240 checks, 8 s at
30 frames a second) or game_save_no_timeout (waits as long as it takes,
and keeps a later request waiting with it): without these lines a log
said nothing between "checkpoint taken" lines, whether no checkpoint was
asked for or one waited minutes for a moment safe enough (GitHub #10) */
void platform_log(const char *format, ...);
char const *game_unsafe_to_save_reason(char *buffer, long size);
static unsigned long checkpoint_log_lines;
static long checkpoint_wait_checks;

/* (the most checkpoint lines a map logs: the count was for the session, and
a long session spent it before the level that needed it - BlazeRed17's
d20 and d40 used up all 96 before the Warthog run of d40, GitHub #10) */
#define CHECKPOINT_LOG_LINES 160

static void main_checkpoint_log_new_map(
	void)
{
	checkpoint_log_lines = 0;
}

static void main_checkpoint_asked(
	char const *how)
{
	if (checkpoint_log_lines++ < CHECKPOINT_LOG_LINES)
		platform_log("checkpoint: %s asked for by %s at tick %ld", how, hs_runtime_get_executing_thread_name(),
			game_in_progress() ? (long)game_time_get() : 0L);
	checkpoint_wait_checks = 0;
}

/* a checkpoint asked for and not yet taken, dropped */
static void main_checkpoint_cancelled(
	char const *by)
{
	if (main_globals.saving_map && checkpoint_log_lines++ < CHECKPOINT_LOG_LINES)
		platform_log("checkpoint: the one asked for cancelled by %s after %ld frames", by, checkpoint_wait_checks);
}

/* who sent the level back (GitHub #10: "the timer ran out on the Warthog
run and it sent me to the start of the level"): a script's game_lost (d40's
cutscene_lose when the timer runs out), game_revert or map_reset, every
player dead, or the pause menu (which logs its own line first). The revert
itself logs which checkpoint it loaded, or that there was none (game_state.c) */
static void main_map_request_log(
	char const *what)
{
	static unsigned long lines;
	char const *by = hs_runtime_get_executing_thread_name();

	if (!csstrcmp(by, "[unknown]"))
	{
		/* (not a script: players.c's every-player-dead check, the pause menu
		or the escape key) */
		by = players_globals && players_globals->all_dead ? "every player dead" : "the game";
	}
	if (lines++ < 160)
		platform_log("%s asked for by %s at tick %ld", what, by, game_in_progress() ? (long)game_time_get() : 0L);
}

static void main_checkpoint_waiting(
	char const *what)
{
	char buffer[256];
	char const *reason = game_unsafe_to_save_reason(buffer, sizeof(buffer));

	if (checkpoint_log_lines++ < CHECKPOINT_LOG_LINES)
		platform_log("checkpoint: %s after %ld frames: not safe: %s", what, checkpoint_wait_checks,
			reason ? reason : "(safe now)");
}
#endif

void main_save_map_safe(
	void)
{
	if (!main_globals.saving_map || main_globals.save_map_timeout)
	{
#ifdef HALO_LINUX
		main_checkpoint_asked("game_save");
#endif
		main_globals.saving_map = TRUE;
		main_globals.save_map_safely = TRUE;
		main_globals.save_map_timeout = TRUE;
		main_globals.ticks_until_next_save_check = 0;
		main_globals.ticks_unable_to_save = 0;
		main_globals.safe_intervals = 0;
	}
	return;
}

void main_save_map_no_timeout(
	void)
{
	if (!main_globals.saving_map || main_globals.save_map_timeout)
	{
#ifdef HALO_LINUX
		main_checkpoint_asked("game_save_no_timeout");
#endif
		main_globals.saving_map = TRUE;
		main_globals.save_map_safely = TRUE;
		main_globals.ticks_until_next_save_check = 0;
		main_globals.ticks_unable_to_save = 0;
		main_globals.safe_intervals = 0;
	}
	main_globals.save_map_timeout = FALSE;
	return;
}

void main_skip(
	short ticks)
{
	if (ticks <= 15)
	{
		main_globals.skip_ticks = ticks;
		main_globals.skip = TRUE;
	}
	else
	{
		error(_error_silent, "cannot skip more than 15 frames (half a second)");
	}
	return;
}

void main_queue_map_name(
	char const *map_name)
{
	if (map_name)
	{
		csstrncpy(main_globals.queued_map_name, map_name, NUMBEROF(main_globals.queued_map_name) - 1);
		main_globals.queue_map = TRUE;
	}
	else
	{
		main_globals.queued_map_name[0] = 0;
		main_globals.queue_map = FALSE;
	}
	return;
}

boolean main_taking_screenshot(
	void)
{
	return global_screenshot_count.count > 0 || main_globals.movie != NULL;
}

void main_movie_start(
	real frames_per_second)
{
	match_assert("c:\\halo\\SOURCE\\main\\main.c", 2715, main_globals.movie==NULL);

	main_globals.movie = bitmap_2d_new(640, 480, 0, 10);
	if (main_globals.movie)
	{
		directory_create_or_delete_contents("movie");
		main_globals.recording_frame_index = 0;
		if (frames_per_second > 0.0001f)
			main_globals.recording_dt = 1.0f / frames_per_second;
		else
			main_globals.recording_dt = 1.0f / 30.0f;
		game_time_set_speed(1.0f);
	}

	return;
}

void main_movie_stop(
	void)
{
	if (main_globals.movie)
	{
		bitmap_delete(main_globals.movie);
		main_globals.movie = NULL;
	}
	return;
}

void main_print_version(
	void)
{
	console_printf(FALSE, "halobeta xbox 01.01.14.2342 Jan 14 2002 12:49:20");
	return;
}

void main_vertical_blank_interrupt_handler(
	unsigned long context)
{
	rasterizer_globals.frame_and_vertical_blank_index++;
	if (!main_globals.d3d_flip_count)
	{
		rasterizer_globals.previous_frame_and_vertical_blank_index =
			rasterizer_globals.frame_and_vertical_blank_index;
	}
	else if (*main_globals.d3d_flip_count != rasterizer_globals.d3d_flip_count)
	{
		main_globals.vblank_flip_deltas[main_globals.vblank_flip_delta_index] =
			(short)((short)rasterizer_globals.frame_and_vertical_blank_index -
				(short)rasterizer_globals.previous_frame_and_vertical_blank_index);
		main_globals.vblank_flip_delta_index =
			(short)((main_globals.vblank_flip_delta_index + 1) % 15);
		rasterizer_globals.previous_frame_and_vertical_blank_index =
			rasterizer_globals.frame_and_vertical_blank_index;
		rasterizer_globals.d3d_flip_count = *main_globals.d3d_flip_count;
	}

	input_vertical_blank_interrupt();
	return;
}

void main_save_current_solo_map(
	char const *map_name)
{
	FILE *file;

	if ((short)main_get_solo_level_from_name(map_name) != NONE)
	{
		file = fopen("z:\\last_solo.txt", "w");
		if (file)
		{
			fwrite(map_name, 1, csstrlen(map_name) + 1, file);
			fclose(file);
		}
		else
		{
			error(_error_silent, "Couldn't create a file to write the current solo map to");
		}
	}

	return;
}

void main_load_last_solo_map(
	void)
{
	char map_name[256];
	boolean valid_map_name = FALSE;

	if (main_globals.load_last_solo_level && !bink_playback_active())
	{
		FILE *file;

		file = fopen("z:\\last_solo.txt", "r");

		if (file)
		{
			long character_count;

			character_count = fread(map_name, 1, 255, file);
			fclose(file);
			map_name[MIN(character_count, 255)] = 0;
			valid_map_name = main_get_solo_level_from_name(map_name) != NONE;
		}

		if (valid_map_name)
			main_set_map_name(map_name);
		else
			main_set_map_name(scenario_paths[0]);
		main_globals.defer_map_change = FALSE;
		main_globals.load_last_solo_level = FALSE;
	}

	return;
}

void main_load_ui_scenario(
	boolean precache_resources)
{
	struct game_options options;

	game_precache_new_map("levels\\ui\\ui", TRUE);
	match_assert("c:\\halo\\SOURCE\\main\\main.c", 1092,
		!main_globals.main_menu_scenario_loaded);
	game_options_new(&options);
	csstrncpy(options.map_name, "levels\\ui\\ui", NUMBEROF(options.map_name) - 1);
	options.map_name[NUMBEROF(options.map_name) - 1] = 0;
	game_precache_new_map(options.map_name, TRUE);
	game_dispose_from_old_map();
	game_unload();
	game_engine_dispose();
	game_set_game_variant(NULL);
	main_globals.main_menu_scenario_loaded = TRUE;
	main_new_map(&options);
	director_script_camera(TRUE);
	scripted_camera_set(0, 0, NONE);
	main_menu_active(TRUE);
	main_globals.load_last_solo_level = TRUE;
	if (precache_resources)
		main_menu_precache_resources();

	return;
}

void main_roll_credits(
	void)
{
	error(_error_silent, "congratulations, you won the game!");
	main_menu_load();
	game_end_credits_start();
	return;
}

void main_pregame_render(
	void)
{
	/* Name, type and function scope from the 2003 PC demo PDB and the HCEX PDB (static local
	   struct render_window window of main_pregame_render). Neither PDB records the block: placing it
	   at the top of the function is unattested. January corroborates: .bss +0, the 0xAC-byte window
	   padded to 0xB0 before main_globals, referenced only by this function. */
	static struct render_window window;

	collision_log_continue_period(TRUE);
	sound_render();
	{
		real_point3d position = { 0.0f, 0.0f, 0.0f };
		real_vector3d forward = { 0.0f, 0.0f, 1.0f };
		real_vector3d up = { 0.0f, 1.0f, 0.0f };

		window.local_player_index = NONE;
		window.console_window = TRUE;
		window.rasterizer_camera.position = position;
		window.rasterizer_camera.forward = forward;
		window.rasterizer_camera.up = up;
		window.rasterizer_camera.mirrored = FALSE;
		window.rasterizer_camera.vertical_field_of_view =
			2.0f * arctangent(
				0.75f * render_camera_get_adjusted_field_of_view_tangent(
					DEGREES_TO_RADIANS(80.0f)),
				1.0f);
		compute_window_bounds(
			0,
			1,
			&window.rasterizer_camera.viewport_bounds,
			&window.rasterizer_camera.window_bounds);
		window.rasterizer_camera.z_near = 0.01f;
		window.rasterizer_camera.z_far = 1.0f;
		window.render_camera = window.rasterizer_camera;
		render_frame_pregame(
			&window,
			main_globals.movie);
	}
	collision_log_end_period();

	return;
}

/* port: a network co-op host reverts to its last saved state with its
clock kept going forward, and the clients follow through the co-op syncs
(network_coop.c). FALSE without a saved state, which would reset the map
on the host alone. */
static boolean main_coop_host_revert(
	void)
{
	long now = game_time_get();

	if (!game_state_port_saved_game_valid())
		return FALSE;
	game_state_revert();
	network_coop_reverted(now);
	ui_widgets_disable_pause_game(30);
	return TRUE;
}

static boolean main_coop_host(
	void)
{
	return game_connection() == _game_connection_network_server && network_coop_active();
}

static void main_revert_map_private(
	void)
{
	/* (a network client never reverts on its own: its game is the host's) */
	if (main_coop_host())
		main_coop_host_revert();
	else if (game_connection() != _game_connection_network_client)
	{
		game_state_revert();
		ui_widgets_disable_pause_game(30);
	}
	main_globals.revert_map = FALSE;
	return;
}

static void main_skip_cinematic_private(
	void)
{
	/* port: only a local game or a network co-op host reverts to skip; a
	network game's other machines would be left out of step */
	boolean skippable = cinematic_can_be_skipped();
	boolean skipped = FALSE;

	if (skippable && main_coop_host())
	{
		skipped = main_coop_host_revert();
		if (skipped)
			network_coop_skip_done();
	}
	else if (skippable && game_connection() == _game_connection_local)
	{
		game_state_revert();
		ui_widgets_disable_pause_game(30);
		skipped = TRUE;
	}
	if (skipped)
		main_globals.revert_map = FALSE;
	else if (main_coop_host())
	{
		error(_error_silent, "co-op: cutscene not skipped (skippable %d, saved state %d)",
			skippable, game_state_port_saved_game_valid());
	}
	main_globals.skip_cinematic = FALSE;
	return;
}

static void main_skip_private(
	void)
{
	if (main_globals.skip_ticks && cinematic_in_progress())
	{
		real saved_speed = game_time_get_speed();

		game_time_set_speed(1.0f);
		while (main_globals.skip_ticks-- > 0)
			game_time_update(1.0f / TICKS_PER_SECOND);
		game_time_set_speed(saved_speed);
	}
	else
	{
		error(
			_error_silent,
			"manual skipping doesn't work outside of cinemtatic start/stop...");
	}

	main_globals.skip_ticks = 0;
	main_globals.skip = FALSE;

	return;
}

static void main_saving_map_private(
	void)
{
	game_state_save();
	hud_autosave(FALSE);
	main_globals.save_map_completed = FALSE;
	return;
}

static void main_save_map_private(
	void)
{
	if (!game_time_get_paused())
	{
		boolean save_map_safely = main_globals.save_map_safely;
		boolean save_map = FALSE;

		if (save_map_safely)
		{
#ifdef HALO_LINUX
			checkpoint_wait_checks++;
#endif
			if (main_globals.ticks_unable_to_save++ >= 240 &&
				main_globals.save_map_timeout)
			{
#ifdef HALO_LINUX
				main_checkpoint_waiting("given up");
#endif
				if (debug_game_save)
					console_printf(FALSE, "gave up trying to save");
				main_globals.saving_map = FALSE;
				return;
			}
			else if (main_globals.ticks_until_next_save_check-- <= 0)
			{
				if (game_safe_to_save())
				{
					if (main_globals.safe_intervals++ >= 3)
						save_map = TRUE;
				}
				else
				{
					main_globals.safe_intervals = 0;
#ifdef HALO_LINUX
					/* (every 30 s of waiting: a checkpoint without a
					timeout can wait for the rest of the level) */
					if (checkpoint_wait_checks >= 900 && checkpoint_wait_checks % 900 < 10)
						main_checkpoint_waiting("waiting");
#endif
				}
				main_globals.ticks_until_next_save_check = 10;
			}
		}
		else
		{
			if (debug_game_save)
				console_printf(FALSE, "unsafe save");
			save_map = TRUE;
		}

		if (save_map)
		{
#ifdef HALO_LINUX
			if (save_map_safely && checkpoint_wait_checks >= 300 && checkpoint_log_lines++ < CHECKPOINT_LOG_LINES)
				platform_log("checkpoint: safe after %ld frames", checkpoint_wait_checks);
#endif
			/* port: remember where the players are, for network co-op respawns */
			players_note_checkpoint();
			hud_autosave(TRUE);
			main_globals.save_map_completed = TRUE;
			main_globals.saving_map = FALSE;
		}
	}

	return;
}

static void main_switch_to_structure_bsp_private(
	void)
{
	scenario_switch_structure_bsp(main_globals.switch_to_structure_bsp_index);
	main_globals.switch_to_structure_bsp_index = NONE;
	hud_load(0);
	return;
}

/* The original post-increment test expires on its 92nd 30 Hz update.
   Render-only frames must not advance it. Saturate so blocked co-op
   respawns can retry indefinitely without overflowing the short counter. */
static boolean main_death_timer_expired(
	short *timer,
	long *last_tick,
	boolean advance)
{
	long current_tick = game_time_get();
	unsigned long elapsed_ticks = 0;

	if (current_tick < *last_tick)
	{
		/* A checkpoint/core load can move the simulation clock backwards. */
		*timer = 0;
	}
	else
	{
		elapsed_ticks = (unsigned long)current_tick - (unsigned long)*last_tick;
	}
	*last_tick = current_tick;

	if (!advance)
		return FALSE;

	*timer = (short)MIN(92, (unsigned long)*timer + elapsed_ticks);
	return *timer >= 92;
}

static void main_lost_map_private(
	void)
{
	if (main_death_timer_expired(
		&main_globals.loss_timer, &main_loss_last_tick,
		!game_time_get_paused()))
	{
		main_globals.lost_map = FALSE;
		main_globals.loss_timer = 0;
		/* port: in network co-op, everyone dying respawns the players where
		they were at the last checkpoint, without a revert. A mission the
		scripts failed (game_lost, with players still alive: d40's timer,
		a50's Keyes) does revert, or its failure cutscene would never end. */
		if (game_connection() != _game_connection_network_server)
			game_state_revert();
		else if (players_are_all_dead() || !main_coop_host_revert())
			players_respawn_at_checkpoint();
	}
	return;
}

static void main_respawn_private(
	void)
{
	if (main_death_timer_expired(
		&main_globals.respawn_timer, &main_respawn_last_tick,
		!game_time_get_paused() && !cinematic_in_progress()) &&
		players_respawn_coop())
	{
		main_globals.respawn = FALSE;
		main_globals.respawn_timer = 0;
	}
	return;
}

static void main_load_core_private(
	void)
{
	game_state_load_core(main_globals.core_name);
	main_globals.load_core = FALSE;
	return;
}

static void main_save_core_private(
	void)
{
	game_state_save_core(main_globals.core_name);
	main_globals.save_core = FALSE;
	return;
}

static void main_run_demos_private(
	void)
{
	main_globals.run_xdemos = FALSE;
	xbox_demos_launch();
	return;
}

static void main_frame_rate_debug(
	void)
{
	char sample_index;

	if (global_screenshot_count.framerate_reset)
	{
		if (debug_frame_rate)
		{
			sample_index = global_screenshot_count.framerate_sample_index;
		}
		else
		{
			global_screenshot_count.framerate_reset = FALSE;
			csmemset(
				global_screenshot_count.framerate_samples,
				0,
				sizeof(global_screenshot_count.framerate_samples));
			sample_index = 0;
			global_screenshot_count.framerate_flags = 0;
			global_screenshot_count.framerate_sample_index = sample_index;
			global_screenshot_count.framerate_active = FALSE;
			global_screenshot_count.framerate_counter = 0;
			global_screenshot_count.framerate_reset = FALSE;
		}
	}
	else
	{
		sample_index = global_screenshot_count.framerate_sample_index;
	}

	if (debug_frame_rate)
	{
		global_screenshot_count.framerate_samples[sample_index] =
			main_globals.seconds_elapsed;
		SET_FLAG(
			global_screenshot_count.framerate_flags,
			sample_index,
			main_globals.seconds_elapsed > 1.08/TICKS_PER_SECOND);

		sample_index++;
		sample_index = (char)(sample_index % (long)NUMBEROF(global_screenshot_count.framerate_samples));
		global_screenshot_count.framerate_sample_index = sample_index;
		global_screenshot_count.framerate_reset = TRUE;

		if (global_screenshot_count.framerate_active)
		{
			if (global_screenshot_count.framerate_sample_index == 0)
			{
				if (global_screenshot_count.framerate_flags == 0)
				{
					if (++global_screenshot_count.framerate_counter >= 60)
					{
						global_screenshot_count.framerate_counter = 0;
						global_screenshot_count.framerate_active = FALSE;
						return;
					}
				}
				else
				{
					global_screenshot_count.framerate_counter = 0;
					return;
				}
			}
		}
		else if (global_screenshot_count.framerate_flags == 0xFF)
		{
			SYSTEMTIME system_time;
			char core_name[256];
			char init_file_name[260];
			char const *scenario_name =
				tag_name_strip_path(tag_get_name(global_scenario_index));
			FILE *file;

			GetSystemTime(&system_time);
			sprintf(
				core_name,
				"%s_slow_%d_%d_%d_%d_%d_%d.bin",
				scenario_name,
				system_time.wMonth,
				system_time.wDay,
				system_time.wYear,
				system_time.wHour,
				system_time.wMinute,
				system_time.wSecond);
			game_state_save_core(core_name);
			sprintf(init_file_name, "d:\\%s_init.txt", scenario_name);

			file = fopen(init_file_name, "r");
			if (!file)
			{
				file = fopen(init_file_name, "wt");
				fprintf(file, "map_name %s\n", scenario_name);
			}
			else
			{
				fclose(file);
				file = fopen(init_file_name, "a+t");
			}

			fprintf(file, ";core_load_name_at_startup %s\n", core_name);
			fflush(file);
			fclose(file);
			global_screenshot_count.framerate_active = TRUE;
		}
	}

	return;
}

static void main_won_map_private(
	void)
{
	short level;
	short local_player_index;

	/* port: when a network co-op level is won, the round ends for everyone as
	in multiplayer, back to the lobby, and the next round is the campaign's
	next level (The Maw's: The Pillar of Autumn). A level not in the campaign
	repeats. */
	if (game_connection() == _game_connection_network_server && network_coop_active())
	{
		struct network_game *game = network_game_get_game();

		main_globals.won_map = FALSE;
		level = game ? main_get_solo_level_from_name(game->map.name) : NONE;
		player_profile_save_level_completed(0);
		network_game_server_port_cooperative_won(level != NONE ?
			main_get_solo_level_name((level + 1) % NUMBER_OF_SINGLE_PLAYER_LEVELS) : NULL);
		return;
	}
	main_globals.want_to_be_at_main_menu = TRUE;
	main_globals.won_map = FALSE;
	level = main_get_solo_level_from_name(main_globals.soloplayer_map_name) + 1;
	if (level >= 10)
		level = NONE;
	for (local_player_index = 0; local_player_index < player_spawn_count; local_player_index++)
		player_profile_save_level_completed(local_player_index);
	ui_set_next_level(level);
	return;
}

void main_set_map_name(
	char const *map_name)
{
	main_globals.want_to_be_at_main_menu = FALSE;
	csstrncpy(main_globals.soloplayer_map_name, map_name, NUMBEROF(main_globals.soloplayer_map_name) - 1);
	main_globals.soloplayer_map_name[NUMBEROF(main_globals.soloplayer_map_name) - 1] = 0;
	main_globals.allow_persistent_storage = TRUE;
	if ((game_in_editor() || game_in_progress()) && main_globals.connection == _game_connection_local)
		main_globals.defer_map_change = TRUE;
	return;
}

static void main_exit(
	void)
{
	switch (main_globals.connection)
	{
	case _game_connection_network_server:
		dispose_global_network_game_client();
		dispose_global_network_game_server();
		break;
	case _game_connection_network_client:
		dispose_global_network_game_client();
		break;
	}

	game_dispose_from_old_map();
	game_dispose();
	debug_keys_dispose();
	console_dispose();

	return;
}

static void main_reset_time(
	void)
{
	main_globals.frame_start_milliseconds = system_milliseconds();
	main_globals.rasterizer_target_index = rasterizer_globals.vertical_blank_index;
	return;
}

static boolean main_framerate_throttle_enabled(
	void)
{
	return rasterizer_globals.framerate_throttle;
}

#ifdef HALO_LINUX
/* The native ports draw a frame whenever the display can show one, paced
by vsync, and frames fall between the 30 Hz ticks
(port/linux/game/render_interpolation.c): no vertical blank throttle, and
the frame is timed with the performance counter. The same without
interpolation when display.vsync is off (HALO_NO_VSYNC=1): on the Vita the
Xbox throttle busy-waited 10-20 ms a frame on an emulated vertical blank
whose thread the busy cores starved. */
int config_boolean(const char *name);
static boolean halo_frame_unthrottled(
	void)
{
	static int unthrottled = -1;

	if (unthrottled < 0)
		unthrottled = !config_boolean("display.vsync");
	return halo_interpolation_enabled() || unthrottled;
}

/* (harness) HALO_FIXED_DT=1: a repeatable run - every frame is a
thirtieth of a second of game time whatever the clock says, the texture
cache waits for every texture it starts loading and the local random seed
is fixed. With HALO_TICK_THREAD=0 a run then repeats frame for frame, so
two builds' draw hashes (HALO_DRAW_HASH, the null renderer) compare. */
int halo_repeatable_run(void)
{
	extern char *getenv(const char *name);
	extern int atoi(const char *text);
	static int repeatable = -1;

	if (repeatable < 0)
	{
		const char *setting = getenv("HALO_FIXED_DT");

		repeatable = setting && atoi(setting) != 0;
	}
	return repeatable;
}

/* HALO_TICK_CATCH_UP: 1 (the default) runs as many ticks a frame as the
time elapsed asks for (up to two), 0 at most one (main_update_time_unthrottled);
read again when the settings panel changes it */
static boolean main_tick_catch_up(
	void)
{
	extern volatile unsigned long halo_settings_generation;
	extern char *getenv(const char *name);
	extern int atoi(const char *text);
	static unsigned long settings_seen;
	static int catch_up = -1;

	if (catch_up < 0 || settings_seen != halo_settings_generation)
	{
		const char *setting = getenv("HALO_TICK_CATCH_UP");

		settings_seen = halo_settings_generation;
		catch_up = !setting || atoi(setting) != 0;
	}
	return catch_up != 0;
}

/* (port) HALO_NET_CATCH_UP_TICKS: the most ticks a frame of a distributed
network game runs to catch up with real time (main_update_time_unthrottled);
2 by default, as a local game; 30 the Xbox's network game */
static long main_network_catch_up_ticks(
	void)
{
	extern char *getenv(const char *name);
	extern int atoi(const char *text);
	static long ticks = -1;

	if (ticks < 0)
	{
		const char *setting = getenv("HALO_NET_CATCH_UP_TICKS");

		ticks = setting && atoi(setting) > 0 ? atoi(setting) : 2;
		if (ticks > TICKS_PER_SECOND)
			ticks = TICKS_PER_SECOND;
	}
	return ticks;
}

/* (port) HALO_TICK_OVERLAP: tick pacing for a game that cannot keep up.
With the tick on its own thread (tick_thread.c) a frame takes the longer of
its render and its update, the update's ticks one after another while the
render draws. A frame that runs two ticks to catch up with real time gains
game time only while the render covers them: once a tick takes longer than
its 33 ms share of real time and the update is longer than the render, a
second tick makes the frame nearly twice as long for little more game time
- only the update's per-frame work (particles, sounds, the netcode's: ~7 ms
on the Vita) is shared by its ticks (the Vita in a Custom Edition AI war:
50 ms ticks, a 30 ms render, two ticks a frame: 9 fps at 18.6 ticks a
second; one tick a frame: 17.5 fps at 17.5 ticks a second). So, from the
ticks', the per-frame work's and the render's times averaged over the last
frames, a frame runs one tick while that plays at most a fifth slower than
two would (a game already slower than real time, which then plays at up to
twice the frame rate) and while it is quicker.

1 (on everywhere) or 0 (off everywhere); unset, on for a multiplayer game
this machine plays alone (a local game, or a host no other machine has
joined: the Vita's way to play a Custom Edition map), where only the frame
rate is at stake, and off for the campaign and for network games with
other machines, whose pace stays real time as long as two ticks a frame
can keep it. Each tick's simulation is the same either way. */
static struct
{
	unsigned long long started_us;
	unsigned long long render_us;
	unsigned long long ticks_us_at_start;
	real tick_ms;
	real frame_work_ms;
	real render_ms;
	long frames;
	boolean one_tick;
	long changes_logged;
} main_tick_pacing;

/* network_distributed.c */
boolean network_distributed_host_alone(void);
/* game_time.c */
extern volatile unsigned long long halo_game_ticks_us;

static void main_tick_pacing_tick_started(
	void)
{
	extern unsigned long long vita_host_time_us(void) __attribute__((weak));

	main_tick_pacing.started_us = vita_host_time_us ? vita_host_time_us() : 0;
	main_tick_pacing.render_us = 0;
	main_tick_pacing.ticks_us_at_start = halo_game_ticks_us;
}

/* the frame is presented: its render's time, from the tick's start */
static void main_tick_pacing_presented(
	void)
{
	extern unsigned long long vita_host_time_us(void) __attribute__((weak));

	if (main_tick_pacing.started_us && vita_host_time_us)
		main_tick_pacing.render_us = vita_host_time_us() - main_tick_pacing.started_us;
}

/* the update is joined: its ticks' time each and its per-frame work's */
static void main_tick_pacing_joined(
	void)
{
	extern volatile short halo_render_elapsed_ticks;
	short ticks = halo_render_elapsed_ticks;

	if (ticks > 0 && main_tick_pacing.render_us)
	{
		real update_ms = (real)halo_tick_thread_last_us() / 1000.0f;
		real ticks_ms = (real)(halo_game_ticks_us - main_tick_pacing.ticks_us_at_start) / 1000.0f;
		real tick_ms = ticks_ms / (real)ticks;
		real frame_work_ms = update_ms > ticks_ms ? update_ms - ticks_ms : 0.0f;
		real render_ms = (real)main_tick_pacing.render_us / 1000.0f;

		if (main_tick_pacing.frames++ == 0)
		{
			main_tick_pacing.tick_ms = tick_ms;
			main_tick_pacing.frame_work_ms = frame_work_ms;
			main_tick_pacing.render_ms = render_ms;
		}
		else
		{
			main_tick_pacing.tick_ms += (tick_ms - main_tick_pacing.tick_ms) * 0.125f;
			main_tick_pacing.frame_work_ms += (frame_work_ms - main_tick_pacing.frame_work_ms) * 0.125f;
			main_tick_pacing.render_ms += (render_ms - main_tick_pacing.render_ms) * 0.125f;
		}
	}
	main_tick_pacing.started_us = 0;
}

static boolean main_tick_pacing_one_tick(
	void)
{
	extern char *getenv(const char *name);
	extern int atoi(const char *text);
	static int overlap = -2;
	boolean one_tick = FALSE;

	if (overlap == -2)
	{
		const char *setting = getenv("HALO_TICK_OVERLAP");

		overlap = setting ? atoi(setting) != 0 : -1;
	}
	/* (with some hysteresis, so a frame rate near the boundary does not
	switch back and forth) */
	if (overlap != 0 && halo_tick_thread_enabled() && main_tick_pacing.frames >= 8 &&
		main_tick_pacing.tick_ms > (main_tick_pacing.one_tick ? 0.95f : 1.0f) * 1000.0f / TICKS_PER_SECOND)
	{
		boolean wanted = overlap > 0;

		/* (unset: a multiplayer game nobody else plays in) */
		if (overlap < 0)
		{
			wanted = game_engine_running() &&
				(main_globals.connection == _game_connection_local ||
					(main_globals.connection == _game_connection_network_server &&
						(network_game_is_splitscreen_local() || network_distributed_host_alone())));
		}
		if (wanted)
		{
			/* (ticks a second either way: a frame lasts the longer of the
			render and the update) */
			real one = main_tick_pacing.frame_work_ms + main_tick_pacing.tick_ms;
			real two = main_tick_pacing.frame_work_ms + 2.0f * main_tick_pacing.tick_ms;

			if (one < main_tick_pacing.render_ms)
				one = main_tick_pacing.render_ms;
			if (two < main_tick_pacing.render_ms)
				two = main_tick_pacing.render_ms;
			one_tick = 1.0f / one >= (main_tick_pacing.one_tick ? 0.75f : 0.8f) * 2.0f / two;
		}
	}
	if (one_tick != main_tick_pacing.one_tick)
	{
		main_tick_pacing.one_tick = one_tick;
		if (main_tick_pacing.changes_logged < 40)
		{
			main_tick_pacing.changes_logged++;
			platform_log("tick pacing: %s a frame (tick %.1f ms, the update's other work %.1f ms, render %.1f ms)",
				one_tick ? "one tick" : "up to two ticks", main_tick_pacing.tick_ms,
				main_tick_pacing.frame_work_ms, main_tick_pacing.render_ms);
		}
	}
	return one_tick;
}

static void main_update_time_unthrottled(
	void)
{
	static LARGE_INTEGER previous_counter;
	LARGE_INTEGER counter;
	LARGE_INTEGER frequency;
	real seconds_elapsed = 0.0f;

	QueryPerformanceCounter(&counter);
	QueryPerformanceFrequency(&frequency);
	if (previous_counter.QuadPart && frequency.QuadPart)
	{
		seconds_elapsed = (real)((double)(counter.QuadPart - previous_counter.QuadPart) /
			(double)frequency.QuadPart);
	}
	previous_counter = counter;

	if (main_globals.movie)
	{
		seconds_elapsed = main_globals.recording_dt;
	}
	else
	{
		seconds_elapsed = PIN(seconds_elapsed, 0.0f, 1.0f);
		/* (port) a split screen game - the Vita's solo multiplayer: a host
		with its own client on one machine and no other - is paced as a
		local game, at most two ticks a frame. As a network server it took
		up to a second's ticks (30) a frame to catch up with real time; a
		frame slowed by a heavy moment then ran more ticks, which made the
		next frame slower still, until frames of 1.3-1.7 s (Blood Gulch on
		the Vita). With no other machine there is nobody to keep up with. */
		if (main_globals.connection == _game_connection_local ||
			(main_globals.connection == _game_connection_network_server && network_game_is_splitscreen_local()))
		{
			if (debug_force_frame_rate_update)
				seconds_elapsed = CEILING(seconds_elapsed, 0.03333333507180214f);
			else
				seconds_elapsed = CEILING(seconds_elapsed, 0.06666667014360428f);
			/* (port) HALO_TICK_CATCH_UP=0 (the Vita's settings panel: Heavy
			scenes, Slow down): a frame elapses at most one tick, so when a
			tick takes longer than a frame's share the game plays slower
			than real time instead of running two ticks a frame and waiting
			for both (the frame rate halves then). Each tick is the same
			either way; only the pacing changes. Local games only (the
			connection test above: players of a networked game stay in
			step), and not in a cinematic, whose sound runs in real time. */
			if (seconds_elapsed > 0.03333333507180214f && !main_tick_catch_up() && game_in_progress() &&
				!cinematic_in_progress())
			{
				seconds_elapsed = 0.03333333507180214f;
			}
		}
		/* (port) a distributed network game - System Link and online play's
		netcode (NETCODE.md) - the same: its machines tick on their own
		clocks and nobody waits for anybody, but as a network server or
		client each took up to a second's ticks (30) a frame to catch up
		with real time, the spiral the split screen game had above. A host
		whose tick nears a tick's 33 ms - the Vita's, with a client, whose
		players it simulates and whose objects and corrections it sends
		every tick - fell behind, ran more ticks the next frame, and slower
		still: 1-5 fps for the host, and for its clients the host's players
		and objects frozen, then jumping a second on (the host's ticks
		and their updates came in bursts). Now at most two ticks a frame
		(HALO_NET_CATCH_UP_TICKS): a machine that cannot keep up plays
		slower than real time for a moment, as the campaign does; the
		others take its updates as they come (each stamped with its tick).
		Every network game is distributed now - System Link, online play
		and ad hoc alike (protocol 9 has no lockstep path) - so every one is
		paced so; a client left more than a second behind the host takes
		the host's clock at its next update (network_client_manager.c). */
		else if (main_globals.connection == _game_connection_network_server ||
			main_globals.connection == _game_connection_network_client)
		{
			seconds_elapsed = CEILING(seconds_elapsed, (real)main_network_catch_up_ticks() * 0.03333333507180214f);
			/* (port) HALO_TICK_CATCH_UP=0 for a host with no other machine
			in its game (a System Link game hosted to play a multiplayer
			map alone, as the Vita does): nobody to keep up with, so paced
			as the local game above */
			if (main_globals.connection == _game_connection_network_server &&
				seconds_elapsed > 0.03333333507180214f && !main_tick_catch_up() && game_in_progress() &&
				!cinematic_in_progress() && network_distributed_host_alone())
			{
				seconds_elapsed = 0.03333333507180214f;
			}
			/* (a co-op client too far ahead of a slow host waits for it) */
			if (main_globals.connection == _game_connection_network_client && network_coop_client_hold())
				seconds_elapsed = 0.0f;
		}
		/* (port) one tick a frame while two would not play faster
		(HALO_TICK_OVERLAP, above) */
		if (seconds_elapsed > 0.03333333507180214f && main_tick_pacing_one_tick())
			seconds_elapsed = 0.03333333507180214f;
	}
	{
		int halo_repeatable_run(void);

		if (halo_repeatable_run())
			seconds_elapsed = 1.0f / 30.0f;
	}

	{
		/* (debug) HALO_FIXED_TICK=1: every frame elapses exactly one tick,
		whatever the real time, so a run's simulation does not depend on the
		machine's speed (tick_hash.c compares runs tick by tick) */
		int halo_fixed_tick(void);

		if (halo_fixed_tick() && !main_globals.movie)
			seconds_elapsed = 1.0f / TICKS_PER_SECOND;
	}

	main_globals.frame_start_milliseconds = system_milliseconds();
	main_globals.seconds_elapsed = seconds_elapsed;
	profile_seconds_elapsed(seconds_elapsed);
	main_globals.rasterizer_initial_index =
		rasterizer_globals.frame_and_vertical_blank_index;
}

#endif
static void main_update_time(
	void)
{
	unsigned long end_milliseconds;
	long milliseconds_elapsed;
	short requested_rate;
	short requested_interval;
	short elapsed_game_ticks;
	short slot;
	boolean framerate_throttle;
	__int64 target_index;
	__int64 minimum_target_index;
	short short_target_index;
	real seconds_elapsed;

#ifdef HALO_LINUX
	if (halo_frame_unthrottled())
	{
		main_update_time_unthrottled();
		return;
	}
#endif
	end_milliseconds = system_milliseconds();
	minimum_target_index = MAX(
		main_globals.rasterizer_target_index,
		main_globals.rasterizer_throttle_end_index);
	target_index = minimum_target_index;
	framerate_throttle = main_framerate_throttle_enabled();

	if (framerate_throttle)
	{
		csstrcpy(main_globals.vblank_debug_string, "");
		if (global_frame_rate_throttle && rasterizer_globals.framerate_throttle_target >= 0)
		{
			requested_interval = (short)(
				60 / (rasterizer_globals.framerate_throttle_target ? rasterizer_globals.framerate_throttle_target : 30));
			main_globals.vblank_interval_requested = requested_interval;

			if (rasterizer_globals.framerate_throttle_debug)
			{
				short best_interval;
				short current_interval;

				elapsed_game_ticks = game_time_get_elapsed();

				_snprintf(
					main_globals.vblank_debug_string + csstrlen(main_globals.vblank_debug_string),
					sizeof(main_globals.vblank_debug_string) - csstrlen(main_globals.vblank_debug_string),
					"last%6I64d init%6I64d achv%6I64d pres%6I64d g%d cur%d... ",
					main_globals.rasterizer_target_index,
					main_globals.rasterizer_initial_index,
					main_globals.rasterizer_throttle_start_index,
					main_globals.rasterizer_throttle_end_index,
					elapsed_game_ticks,
					main_globals.vblank_interval_current);

				best_interval = 5;
				short_target_index = (short)minimum_target_index;
				for (slot = 5; slot > 0; slot--)
				{
					short displayed_failure_count;
					short target_age;
					short slot_bucket;
					boolean ignore_failure;
					char const *label;
					__int64 last_failure_index;
					__int64 target_age_raw;

					ignore_failure = FALSE;
					displayed_failure_count = main_globals.vblank_failure_counts[slot];
					if (displayed_failure_count >= 99)
						displayed_failure_count = 99;

					last_failure_index = main_globals.vblank_last_failure_indices[slot];
					target_age_raw = short_target_index - last_failure_index;
					if (target_age_raw < 99)
						target_age = (short)target_age_raw;
					else
						target_age = 99;

					slot_bucket = (short)((slot + 1) / 2);
					current_interval = main_globals.vblank_interval_current;
					if ((short)((current_interval + 1) / 2) > slot_bucket &&
						(short)(current_interval / 2) <= slot_bucket &&
						elapsed_game_ticks > slot_bucket)
					{
						ignore_failure = TRUE;
					}

					if (main_globals.rasterizer_throttle_start_index >
						main_globals.rasterizer_initial_index + slot)
					{
						if (!ignore_failure)
						{
							long failure_count;

							failure_count = main_globals.vblank_failure_counts[slot];
							main_globals.vblank_failure_counts[slot] =
								(short)(failure_count + 1);
							main_globals.vblank_last_failure_indices[slot] = minimum_target_index;
						}

						_snprintf(
							main_globals.vblank_debug_string + csstrlen(main_globals.vblank_debug_string),
							sizeof(main_globals.vblank_debug_string) - csstrlen(main_globals.vblank_debug_string),
							"(%s%2d) ",
							ignore_failure ? "ignor" : "fail ",
							displayed_failure_count);
					}
					else
					{
						if (minimum_target_index >= last_failure_index + 15)
						{
							main_globals.vblank_failure_counts[slot] = 0;
							_snprintf(
								main_globals.vblank_debug_string + csstrlen(main_globals.vblank_debug_string),
								sizeof(main_globals.vblank_debug_string) - csstrlen(main_globals.vblank_debug_string),
								"(ok   %2d) ",
								target_age);
						}
						else
						{
							label = main_globals.vblank_failure_counts[slot] >= 4 ? "dn" : "wt";
							_snprintf(
								main_globals.vblank_debug_string + csstrlen(main_globals.vblank_debug_string),
								sizeof(main_globals.vblank_debug_string) - csstrlen(main_globals.vblank_debug_string),
								"(%s%2d/%2d) ",
								label,
								displayed_failure_count,
								target_age);
						}
					}

					if (slot >= main_globals.vblank_interval_requested &&
						main_globals.vblank_failure_counts[slot] < 4)
					{
						best_interval = slot;
					}
				}

				if (best_interval == 0)
					requested_rate = 999;
				else
					requested_rate = (short)(60 / best_interval);

				if (main_globals.vblank_interval_current < best_interval)
				{
					_snprintf(
						main_globals.vblank_debug_string + csstrlen(main_globals.vblank_debug_string),
						sizeof(main_globals.vblank_debug_string) - csstrlen(main_globals.vblank_debug_string),
						" FAILDOWN %d",
						requested_rate);
				}
				else if (best_interval < main_globals.vblank_interval_current)
				{
					_snprintf(
						main_globals.vblank_debug_string + csstrlen(main_globals.vblank_debug_string),
						sizeof(main_globals.vblank_debug_string) - csstrlen(main_globals.vblank_debug_string),
						" RESTORE  %d",
						requested_rate);
				}
				else
				{
					_snprintf(
						main_globals.vblank_debug_string + csstrlen(main_globals.vblank_debug_string),
						sizeof(main_globals.vblank_debug_string) - csstrlen(main_globals.vblank_debug_string),
						" MAINTAIN %d",
						requested_rate);
				}

				_snprintf(
					main_globals.vblank_debug_string + csstrlen(main_globals.vblank_debug_string),
					sizeof(main_globals.vblank_debug_string) - csstrlen(main_globals.vblank_debug_string),
					" des %d targ%6I64d",
					best_interval,
					minimum_target_index + best_interval);
				main_globals.vblank_interval_current = best_interval;
			}
			else
			{
				main_globals.vblank_interval_current = requested_interval;
			}

			target_index += main_globals.vblank_interval_current;
		}
	}
	else
	{
		milliseconds_elapsed = (long)(real)(
			(long)end_milliseconds - (long)main_globals.frame_start_milliseconds);
		main_globals.vblank_interval_held = FALSE;
		if (milliseconds_elapsed < 33)
		{
			profile_idle_start();
			if (global_frame_rate_throttle)
				Sleep(33 - milliseconds_elapsed);
			profile_idle_end();
		}
		else
		{
			profile_lapsed_msec(milliseconds_elapsed - 33);
		}
	}

	end_milliseconds = system_milliseconds();
	if ((__int64)rasterizer_globals.frame_and_vertical_blank_index > target_index)
		target_index = (__int64)rasterizer_globals.frame_and_vertical_blank_index;

	if (framerate_throttle)
	{
		seconds_elapsed =
			(real)(target_index - (__int64)main_globals.rasterizer_target_index) *
			0.01666666753590107f;
	}
	else
	{
		seconds_elapsed =
			(real)(end_milliseconds - main_globals.frame_start_milliseconds) *
			0.001000000047497451f;
	}

	if (main_globals.movie)
	{
		seconds_elapsed = main_globals.recording_dt;
	}
	else
	{
		seconds_elapsed = PIN(seconds_elapsed, 0.0f, 1.0f);
		if (main_globals.connection == _game_connection_local)
		{
			if (debug_force_frame_rate_update)
				seconds_elapsed = CEILING(seconds_elapsed, 0.03333333507180214f);
			else
				seconds_elapsed = CEILING(seconds_elapsed, 0.06666667014360428f);
		}
	}

	main_globals.frame_start_milliseconds = end_milliseconds;
	main_globals.rasterizer_target_index = target_index;
	main_globals.seconds_elapsed = seconds_elapsed;
	profile_seconds_elapsed(seconds_elapsed);
	main_globals.rasterizer_initial_index =
		rasterizer_globals.frame_and_vertical_blank_index;

	return;
}

void main_rasterizer_throttle(
	void)
{
	__int64 entry_index;
	__int64 target_index;
	__int64 end_index;
	__int64 index_delta;
	__int64 elapsed_index;
	unsigned long start_milliseconds;
	short lapsed_frames;
	boolean did_throttle;
	boolean precache_in_progress;
	boolean synchronized;
	char const *description;

	entry_index = rasterizer_globals.frame_and_vertical_blank_index;
	did_throttle = FALSE;
	main_globals.rasterizer_throttle_start_index =
		rasterizer_globals.frame_and_vertical_blank_index + 1;
#ifdef HALO_LINUX
	if (rasterizer_globals.framerate_throttle && !halo_frame_unthrottled())
#else
	if (rasterizer_globals.framerate_throttle)
#endif
	{
		target_index = main_globals.rasterizer_target_index;
		target_index--;
		if ((__int64)rasterizer_globals.frame_and_vertical_blank_index < target_index)
		{
			start_milliseconds = system_milliseconds();
			precache_in_progress = cache_files_precache_in_progress();
			did_throttle = TRUE;
			profile_idle_start();
			while ((__int64)rasterizer_globals.frame_and_vertical_blank_index < target_index)
			{
				if (precache_in_progress)
				{
					Sleep(1);
				}
				if (system_milliseconds() > start_milliseconds + 1000)
				{
					console_warning(
						"stuck waiting for VBLANK callback! disabling rasterizer framerate control");
					rasterizer_globals.framerate_throttle = FALSE;
					break;
				}
			}
			profile_idle_end();
		}
	}

#ifdef HALO_LINUX
	if (halo_frame_unthrottled() && !halo_interpolation_enabled())
	{
		/* (port) unthrottled without interpolation: the picture changes only
		with the 30 Hz ticks, so frames are capped at 30 a second by sleeping
		(a light view would otherwise loop at hundreds a second, drawing the
		same image) - display.frame_cap, 0 for none */
		extern unsigned long long vita_host_time_us(void) __attribute__((weak));
		extern void vita_host_sleep_us(unsigned long microseconds) __attribute__((weak));
		extern char *getenv(const char *name);
		extern long atol(const char *text);
		static unsigned long long previous_us;
		static long cap = -1;
		static unsigned long settings_seen;
		extern volatile unsigned long halo_settings_generation;

		if (cap < 0 || settings_seen != halo_settings_generation)
		{
			settings_seen = halo_settings_generation;
			const char *setting = getenv("HALO_FRAME_CAP");
			cap = setting ? atol(setting) : 30;
		}
		if (cap > 0 && vita_host_time_us && vita_host_sleep_us)
		{
			unsigned long long period = 1000000ull / (unsigned long long)cap;
			unsigned long long now = vita_host_time_us();

			/* (port) capped at the tick rate, a frame ends half a tick
			after the last tick boundary, not a fixed period after the frame
			before. The game runs as many ticks as whole ticks have passed
			since the last frame (game_time_update's leftover): frames a
			period apart sat wherever their phase drifted to, and near a
			tick boundary the sleep's jitter (and anything else that moved a
			frame by a millisecond) gave a frame no tick, so the same picture
			twice, and the next two - the uneven motion of the weapon and
			the view at a steady 30 a second. Half a tick away from either
			boundary, each frame runs one. HALO_FRAME_PHASE_LOCK=0: the fixed
			period again. */
			{
				extern real game_time_get_tick_fraction(void);
				static int phase_lock = -1;

				if (phase_lock < 0)
				{
					const char *setting = getenv("HALO_FRAME_PHASE_LOCK");

					phase_lock = !setting || atol(setting) != 0;
				}
				/* (only for a frame that fit in the period: one that took
				longer waited for nothing before, and would now wait up to
				half a tick more whenever its end fell early in a tick - a
				steady 40 ms frame ran at 24 a second instead of 25) */
				if (phase_lock && cap == TICKS_PER_SECOND && previous_us && now - previous_us < period)
				{
					real fraction = game_time_get_tick_fraction();

					/* (1: no game running, or paused; the test also keeps
					the period within half a tick to a tick and a half) */
					if (fraction >= 0.0f && fraction < 1.0f)
						period = (unsigned long long)((1.5f - fraction) * (1000000.0f / TICKS_PER_SECOND));
				}
			}
			if (previous_us && now - previous_us < period)
			{
				vita_host_sleep_us((unsigned long)(period - (now - previous_us)));
				now = vita_host_time_us();
			}
			previous_us = now;
		}
	}
#endif
	end_index = rasterizer_globals.frame_and_vertical_blank_index + 1;
	synchronized =
		main_globals.vblank_interval_current == main_globals.vblank_interval_requested;
	main_globals.rasterizer_throttle_end_index = end_index;
	index_delta = end_index - main_globals.rasterizer_target_index;
	lapsed_frames = (short)PIN(index_delta, 0, 0x7FFF);

	if (did_throttle)
	{
		elapsed_index = rasterizer_globals.frame_and_vertical_blank_index - entry_index;
		description = "THROTTLE";
	}
	else
	{
		elapsed_index = lapsed_frames;
		description = lapsed_frames == 0 ? "SYNCED  " : "LAPSED  ";
	}

	_snprintf(
		main_globals.vblank_debug_string + csstrlen(main_globals.vblank_debug_string),
		sizeof(main_globals.vblank_debug_string) - csstrlen(main_globals.vblank_debug_string),
		"%6I64d(targ%6I64d %s%2d)",
		entry_index,
		main_globals.rasterizer_target_index,
		description,
		elapsed_index);
	main_globals.vblank_interval_held =
		main_globals.vblank_interval_current > 0 && lapsed_frames == 0;
	profile_lapsed_frames(
		lapsed_frames,
		synchronized,
		main_globals.vblank_debug_string);

	return;
}

static void main_setup_connection(
	void)
{
	struct game_options options;

	if (main_globals.playback_last_recording)
		main_globals.want_to_be_at_main_menu = FALSE;

	if (main_globals.want_to_be_at_main_menu)
	{
		main_menu_load();
		return;
	}

	if (main_globals.playback_last_recording)
	{
		main_globals.connection = _game_connection_film_playback;
		error(_error_silent, "error opening saved film");
		main_globals.want_to_be_at_main_menu = TRUE;
		main_menu_load();
	}
	else
	{
		main_globals.connection = _game_connection_local;
		game_options_new(&options);
		csstrncpy(
			options.map_name,
			main_globals.soloplayer_map_name,
			NUMBEROF(options.map_name) - 1);
		options.map_name[NUMBEROF(options.map_name) - 1] = 0;
		options.difficulty = global_difficulty_level;
#ifdef HALO_LINUX
		platform_log("new map: precaching %s", options.map_name);
#endif
		game_precache_new_map(options.map_name, TRUE);
#ifdef HALO_LINUX
		platform_log("new map: disposing of the old map");
#endif
		game_dispose_from_old_map();
		main_new_map(&options);
	}

	return;
}

static void main_initialize_time(
	void)
{
	main_globals.frame_start_milliseconds = system_milliseconds();
	main_globals.rasterizer_target_index = 0;
	rasterizer_set_vblank_callback(main_vertical_blank_interrupt_handler);
	main_globals.vblank_flip_delta_index = 0;
	csmemset(main_globals.vblank_flip_deltas, 0, sizeof(main_globals.vblank_flip_deltas));
	main_globals.d3d_flip_count = d3d_find_flipcount();
	return;
}

static void main_reset_map_private(
	void)
{
	if (!game_time_get_paused())
	{
		scenario_switch_structure_bsp(0);
		game_dispose_from_old_map();
		input_flush();
		game_initialize_for_new_map();
		create_local_players();
		game_time_start();
		game_initial_pulse();
		ui_widgets_disable_pause_game(30);

		main_globals.reset_map = FALSE;
#ifdef HALO_LINUX
		platform_log("map reset: the level started over");
#endif
	}

	return;
}

static void screenshot_render(
	struct render_window *windows)
{
	char path[512];
	struct file_reference reference;
	point2d screenshot_page_index;
	point2d screenshot_index;
	struct bitmap_data *bitmap;
	long screenshot_width;
	long screenshot_height;

	global_screenshot_size = PIN(global_screenshot_size, 1, 3);
	screenshot_width =
		rasterizer_globals.reserved04.screen_bounds.x1 -
		rasterizer_globals.reserved04.screen_bounds.x0;
	screenshot_height =
		rasterizer_globals.reserved04.screen_bounds.y1 -
		rasterizer_globals.reserved04.screen_bounds.y0;
	bitmap = bitmap_2d_new(
		global_screenshot_size * screenshot_width,
		global_screenshot_size * screenshot_height,
		0,
		_bitmap_format_x8r8g8b8);

	if (bitmap && bitmap->base_address)
	{
		console_printf(TRUE, "");
		console_close();

		for (screenshot_page_index.y = 0;
			screenshot_page_index.y < global_screenshot_count.count;
			screenshot_page_index.y++)
		{
			for (screenshot_page_index.x = 0;
				screenshot_page_index.x < global_screenshot_count.count;
				screenshot_page_index.x++)
			{
				for (screenshot_index.y = 0;
					screenshot_index.y < global_screenshot_size;
					screenshot_index.y++)
				{
					for (screenshot_index.x = 0;
						screenshot_index.x < global_screenshot_size;
						screenshot_index.x++)
					{
						if (global_screenshot_count.count > 1 ||
							global_screenshot_size > 1)
						{
							render_frame(
								windows,
								1,
								&screenshot_page_index,
								&screenshot_index,
								bitmap,
								0.0f);
							render_frame_present(&screenshot_index, bitmap);
						}
						else
						{
							render_frame(windows, 1, NULL, NULL, bitmap, 0.0f);
							render_frame_present(NULL, bitmap);
						}
					}
				}

				sprintf(
					path,
					"%dscreenshot%d%d.tif",
					main_globals.screenshot_identifier,
					screenshot_page_index.y,
					screenshot_page_index.x);
				file_reference_create_from_path(&reference, path, FALSE);
				screenshot_record(bitmap, &reference);
			}
		}

		main_globals.screenshot_identifier++;
		bitmap_delete(bitmap);
	}

	global_screenshot_count.count = 0;
	return;
}

void main_framerate_render(
	void)
{
#ifdef HALO_VITA
	{
		/* (port) the Vita's settings panel has an "FPS counter" switch,
		HALO_FRAMERATE_COUNTER: it has the last word over init.txt's
		display_framerate, read again after each change in the panel */
		extern char *getenv(const char *name);
		extern int atoi(const char *text);
		extern volatile unsigned long halo_settings_generation;
		static unsigned long settings_seen = (unsigned long)-1;

		if (settings_seen != halo_settings_generation)
		{
			const char *setting = getenv("HALO_FRAMERATE_COUNTER");

			settings_seen = halo_settings_generation;
			if (setting)
				display_framerate = atoi(setting) != 0;
		}
	}
#endif
	if (display_framerate)
	{
		long font_tag_index;

		font_tag_index = hud_globals->messaging.single_player_font.index;
		if (font_tag_index != NONE)
		{
			real frame_seconds;
			real frame_rate_real;
			long frame_rate;
			rectangle2d bounds;
#ifdef HALO_LINUX
			/* room for three digits under C99 snprintf, which (unlike MSVC's
			_snprintf) keeps a byte of the count for the terminator */
			char frame_rate_string[8];
#else
			char frame_rate_string[4];
#endif

			bounds = render.camera.window_bounds;
#ifdef HALO_LINUX
			/* The native ports draw at the display's refresh rate, up to
			hundreds of frames a second: show frames per second averaged over
			half a second, counting each frame once (split screen draws this
			once per view), with no 100 cap. */
			{
				static unsigned long counted_frame_start = 0;
				static real window_seconds = 0.0f;
				static long window_frames = 0;
				static long average_frame_rate = 0;

				if (main_globals.frame_start_milliseconds != counted_frame_start)
				{
					counted_frame_start = main_globals.frame_start_milliseconds;
					window_seconds += main_globals.seconds_elapsed;
					window_frames++;
					if (window_seconds >= 0.5f)
					{
						average_frame_rate = fast_ftol(window_frames / window_seconds);
						window_seconds = 0.0f;
						window_frames = 0;
					}
				}
				frame_rate = PIN(average_frame_rate, 0, 999);
				(void)frame_seconds;
				(void)frame_rate_real;
			}
#else
			frame_seconds = MAX(main_globals.seconds_elapsed, 0.01f);
			frame_rate_real = 1.0f / frame_seconds;
			frame_rate = fast_ftol(frame_rate_real);
			if (main_globals.vblank_interval_held)
				frame_rate = 60 / main_globals.vblank_interval_current;
#endif

			_snprintf(
				frame_rate_string,
				NUMBEROF(frame_rate_string) - 1,
				"%d",
				(short)frame_rate);
			frame_rate_string[NUMBEROF(frame_rate_string) - 1] = 0;
			bounds.x0 = (short)(bounds.x1 - 50);
			bounds.y0 = (short)(bounds.y1 - 50);
			draw_string_set_format(NONE, _text_justification_left, 0);
			draw_string_set_color(
				(short)frame_rate >= 30 ? global_real_argb_green : global_real_argb_red);
			draw_string_set_font(font_tag_index);
			rasterizer_draw_string(&bounds, NULL, NULL, 0, frame_rate_string);
		}
	}

	if (display_vblank_deltas)
	{
		long font_tag_index;

		font_tag_index = hud_globals->messaging.single_player_font.index;
		if (font_tag_index != NONE)
		{
			short index;
			rectangle2d bounds;
			char delta_string[4];

			bounds = render.camera.window_bounds;
			bounds.x0 = (short)(bounds.x1 - 50);
			bounds.y0 = (short)(bounds.y1 - 50);
			index = (short)((main_globals.vblank_flip_delta_index + 14) % 15);
			while (index != main_globals.vblank_flip_delta_index)
			{
				bounds.y0 -= 20;
				bounds.y1 -= 20;
				_snprintf(
					delta_string,
					NUMBEROF(delta_string) - 1,
					"%d",
					main_globals.vblank_flip_deltas[index]);
				delta_string[NUMBEROF(delta_string) - 1] = 0;
				draw_string_set_format(NONE, _text_justification_right, 0);
				draw_string_set_font(font_tag_index);
				draw_string_set_color(
					main_globals.vblank_flip_deltas[index] == 2
						? global_real_argb_white
						: global_real_argb_red);
				rasterizer_draw_string(&bounds, NULL, NULL, 0, delta_string);
				index = (short)((index + 14) % 15);
			}
		}
	}

	if (display_precache_progress && cache_files_precache_in_progress())
	{
		long font_tag_index;

		font_tag_index = hud_globals->messaging.single_player_font.index;
		if (font_tag_index != NONE)
		{
			real progress;
			long progress_percent;
			rectangle2d bounds;
			char progress_string[4];

			if (cache_files_precache_map_status(&progress) == 0)
			{
				real progress_percent_real;

				bounds = render.camera.window_bounds;
				progress_percent_real = progress * 100.0f;
				progress_percent = fast_ftol(progress_percent_real);
				_snprintf(
					progress_string,
					NUMBEROF(progress_string) - 1,
					"%d",
					(short)progress_percent);
				progress_string[NUMBEROF(progress_string) - 1] = 0;
				bounds.x0 = (short)(bounds.x1 - 50);
				bounds.y0 = (short)(bounds.y1 - 100);
				draw_string_set_format(NONE, _text_justification_left, 0);
				draw_string_set_color(global_real_argb_purple);
				draw_string_set_font(font_tag_index);
				rasterizer_draw_string(&bounds, NULL, NULL, 0, progress_string);
			}
		}
	}

	return;
}

void halt_and_catch_fire(
	void)
{
	short gamepad_index;
	long font_tag_index;
	struct scenario *scenario;
	struct rasterizer_frame_begin_parameters frame_parameters;
	struct rasterizer_window_begin_parameters window_parameters;

#ifdef HALO_LINUX
	if (halo_epoch_threaded)
	{
		/* with the tick on its own thread this may run there, and drawing
		the error screen from it wedged the Vita (a second producer on the
		render's command ring): the error is in the log, and a deliberate
		crash gets every thread into the core dump */
		platform_log("halt_and_catch_fire: a fatal error (see the lines above); crashing for the dump");
		*(volatile int *)8 = 0;
	}
#endif
	if (!global_screenshot_count.halt_recursion_lock)
	{
		scenario = global_scenario_try_and_get();
		global_screenshot_count.halt_recursion_lock = TRUE;
		for (gamepad_index = 0; gamepad_index < MAXIMUM_GAMEPADS; gamepad_index++)
		{
			if (input_has_gamepad(gamepad_index))
			{
				input_set_gamepad_rumbler_state(
					gamepad_index,
					0,
					0);
			}
		}

		if (scenario)
		{
			font_tag_index = interface_get_tag_index(_interface_font_terminal);
		}
		if (!scenario || font_tag_index == NONE)
		{
			font_tag_index = tag_loaded(
				FONT_GROUP_TAG,
				"old tags\\internal system plain");
		}

		while (TRUE)
		{
			rasterizer_reset_state();
			csmemset(&frame_parameters, 0, sizeof(frame_parameters));
			rasterizer_frame_begin(&frame_parameters);
			rasterizer_windows_begin();

			csmemset(&window_parameters, 0, sizeof(window_parameters));
			window_parameters.camera.position = *global_origin3d;
			window_parameters.camera.forward = *global_forward3d;
			window_parameters.camera.up = *global_up3d;
			window_parameters.camera.mirrored = FALSE;
			window_parameters.camera.vertical_field_of_view =
				2.0f * arctangent(
					0.75f * render_camera_get_adjusted_field_of_view_tangent(
						DEGREES_TO_RADIANS(80.0f)),
					1.0f);
			window_parameters.camera.z_near = rasterizer_globals.near_clip_distance;
			window_parameters.camera.viewport_bounds.x0 = 0;
#ifdef HALO_LINUX
			window_parameters.camera.viewport_bounds.x1 = (short)halo_screen_width();
#else
			window_parameters.camera.viewport_bounds.x1 = 640;
#endif
			window_parameters.camera.viewport_bounds.y0 = 0;
			window_parameters.camera.viewport_bounds.y1 = 480;
			window_parameters.camera.z_far = rasterizer_globals.far_clip_distance;
			render_camera_build_frustum(
				&window_parameters.camera,
				NULL,
				&window_parameters.frustum,
				TRUE);
			window_parameters.rasterizer_target = 0;
			window_parameters.fog.atmospheric_color = *global_real_rgb_blue;
			window_parameters.fog.atmospheric_maximum_distance = 0.0f;
			window_parameters.fog.atmospheric_minimum_distance = 0.0f;
			window_parameters.fog.planar_mode = 0;
			render.camera = window_parameters.camera;
			rasterizer_window_begin(&window_parameters);

			if (font_tag_index != NONE)
			{
				point2d cursor = { 0, 0 };
				rectangle2d bounds = rasterizer_globals.reserved04.frame_bounds;

				draw_string_set_draw_mode(
					font_tag_index,
					NONE,
					0,
					0,
					global_real_argb_white);
				draw_string_set_tab_stops(NULL, 0);
				draw_string_set_color(global_real_argb_white);
				rasterizer_draw_string(
					&bounds,
					NULL,
					&cursor,
					-4,
					"halobeta xbox 01.01.14.2342 built at: Jan 14 2002 12:49:20");
				bounds.y0 = cursor.y - 1;
				rasterizer_draw_string(
					&bounds,
					NULL,
					&cursor,
					-4,
					error_get());
			}

			rasterizer_transparent_geometry_draw(TRUE);
			rasterizer_transparent_geometry_draw(FALSE);
			rasterizer_debug_draw();
			rasterizer_window_end();
			rasterizer_windows_end();
			rasterizer_frame_end();
			rasterizer_present(NULL, NULL);
			input_update();
		}
	}

	exit(0);
	return;
}

void main_loop_of_death(
	void)
{
	while (TRUE)
	{
		input_frame_begin();
		input_update();
		shell_idle();
		event_manager_update();
		telnet_console_process();
		process_ui_widgets();
		main_pregame_render();
		main_rasterizer_throttle();
		main_present_frame();
		input_frame_end();
	}

	return;
}

#ifdef HALO_LINUX
#include <stdlib.h>
unsigned long long vita_host_time_us(void);
/* (variadic: without a prototype the doubles went to VFP registers, and the
log read zeros) */
void platform_log(const char *format, ...);
extern volatile unsigned long long halo_cache_lock_wait_us[2];
extern volatile unsigned long long halo_marker_wait_us[2];
extern volatile unsigned long halo_cache_lock_acquires;
extern volatile unsigned long long halo_tick_sound_us;
extern volatile unsigned long halo_tick_sound_ticks;
static unsigned long long halo_main_render_now(void) { return vita_host_time_us ? vita_host_time_us() : 0; }
static unsigned long long halo_main_render_windows_started, *halo_main_render_sound_us, *halo_main_render_windows_us, *halo_main_render_frames;
#endif

void main_game_render(
	double time_delta_since_tick_sec)
{
	boolean force_single_screen;
	long window_index;
	struct render_window *window;
	struct observer_result const *observer;
	long player_window_count;
	long window_count;
	short last_local_player_index;

	lock_global_random_seed();
	collision_log_continue_period(TRUE);
#ifdef HALO_LINUX
	{
		/* HALO_RENDER_PROFILE=1: sound_render and the windows' rendering,
		the two halves of this function (render.c times the scene phases) */
		static unsigned long long sound_us, windows_us, frames;
		unsigned long long before = halo_main_render_now();

		/* with the tick on its own thread, sound_render runs there after
		the tick (tick_thread.c): the sound manager's state (looping
		sounds, channels) is the tick's, and reading it from here met
		sounds still being created */
		if (!halo_epoch_threaded)
			sound_render();
		sound_us += halo_main_render_now() - before;
		halo_main_render_windows_started = halo_main_render_now();
		halo_main_render_sound_us = &sound_us;
		halo_main_render_windows_us = &windows_us;
		halo_main_render_frames = &frames;
	}
#else
	sound_render();
#endif
	force_single_screen = game_engine_force_single_screen();
	last_local_player_index = NONE;

	window_count = PIN(local_player_count(), 1, MAXIMUM_LOCAL_PLAYERS);
	player_window_count = window_count;
	if (force_single_screen || cinematic_in_progress())
	{
		window_count = 1;
		player_window_count = 1;
	}

	for (window_index = 0; window_index < player_window_count; window_index++)
	{
		window = &global_screenshot_count.windows[window_index];
		observer = NULL;

		compute_window_bounds(
			window_index,
			player_window_count,
			&window->rasterizer_camera.viewport_bounds,
			&window->rasterizer_camera.window_bounds);
		if (force_single_screen)
		{
			window->local_player_index = NONE;
		}
		else if (window_index < window_count)
		{
			if (!rasterizer_debug_options.force_all_player_views_to_default_player ||
				last_local_player_index == NONE)
			{
				if (game_connection() == _game_connection_film_playback)
				{
					last_local_player_index = 0;
				}
				else
				{
					last_local_player_index = local_player_get_next(last_local_player_index);
				}
			}

			window->local_player_index = last_local_player_index;
			observer = observer_get_camera(window->local_player_index);
#ifdef HALO_LINUX
			observer = render_interpolation_camera(window->local_player_index, observer);
#endif
		}
		else
		{
			window->local_player_index = NONE;
		}

		set_window_camera_values(window, observer);
		window->console_window = FALSE;
	}

	window = &global_screenshot_count.windows[player_window_count];
	compute_window_bounds(
		0,
		1,
		&window->rasterizer_camera.viewport_bounds,
		&window->rasterizer_camera.window_bounds);
	window->local_player_index = NONE;
	window->console_window = TRUE;
	set_window_camera_values(window, NULL);

	if (global_screenshot_count.count <= 0)
	{
		render_frame(
			global_screenshot_count.windows,
			player_window_count + 1,
			NULL,
			NULL,
			main_globals.movie,
			(real)time_delta_since_tick_sec);
	}
	else
	{
		screenshot_render(global_screenshot_count.windows);
	}

	collision_log_end_period();
	unlock_global_random_seed();
#ifdef HALO_LINUX
	if (halo_main_render_frames)
	{
		*halo_main_render_windows_us += halo_main_render_now() - halo_main_render_windows_started;
		if (++*halo_main_render_frames % 300 == 0)
		{
			static int enabled = -1;
			if (enabled < 0) { const char *e = getenv("HALO_RENDER_PROFILE"); enabled = e && atoi(e) != 0; }
			if (enabled)
				platform_log("render-split (ms/frame): sound_render %.2f windows %.2f | cache lock waits: render %.2f tick %.2f, acquires %lu/frame | marker waits: render %.2f tick %.2f | sound_render on the tick %.2f ms/tick",
					(double)*halo_main_render_sound_us / 300000.0, (double)*halo_main_render_windows_us / 300000.0,
					(double)halo_cache_lock_wait_us[0] / 300000.0, (double)halo_cache_lock_wait_us[1] / 300000.0,
					halo_cache_lock_acquires / 300,
					(double)halo_marker_wait_us[0] / 300000.0, (double)halo_marker_wait_us[1] / 300000.0,
					halo_tick_sound_ticks ? (double)halo_tick_sound_us / 1000.0 / (double)halo_tick_sound_ticks : 0.0);
				halo_tick_sound_us = 0;
				halo_tick_sound_ticks = 0;
				halo_cache_lock_wait_us[0] = halo_cache_lock_wait_us[1] = 0;
				halo_marker_wait_us[0] = halo_marker_wait_us[1] = 0;
				halo_cache_lock_acquires = 0;
			*halo_main_render_sound_us = 0; *halo_main_render_windows_us = 0;
		}
	}
#endif
	return;

}

#ifdef HALO_LINUX
/* (port, debug) HALO_TEST_COMMANDS="tick:command;tick:command...": console
commands run at the top of the main loop once the game time reaches each
tick, for testing the save paths without a controller, e.g.
"300:game_save_totally_unsafe;600:game_revert". Besides the console's
own, @skip asks for a cinematic skip (as the controller does), @quit
does the pause menu's Save and Quit, and "@camera x y z yaw pitch" puts
the debug camera there (degrees; yaw 0 looks along +x, pitch up is
positive), through d:\\camera.txt and debug_camera_load ("@pan x y z yaw
pitch yaw_rate pitch_rate": from then on turned by the rates, in degrees a
tick, every frame), "@tv name" puts every player's unit at the centre of
that scenario trigger volume (the benchmarks walk the player through a
level's encounters this way: triage/perf2-status.md), "@where" logs where
every vehicle and scenery object is, and "@shot name"
has the next frame presented saved as name.bmp in HALO_SCREENSHOT_DIR
(the desktop GL device; on the Vita its display, as name<frame>.bmp);
"@set VARIABLE value" changes a setting as the Vita's settings panel does
(vita_settings_set, through halo_test_setting_hook; elsewhere the variable, and the settings generation the
readers watch). Each runs once,
at the first frame whose game time has reached its tick. */
void game_state_save_to_persistent_storage(void);
/* (the Vita's settings panel sets it: vita_settings_load) */
int (*halo_test_setting_hook)(const char *variable, const char *value);
extern volatile unsigned long halo_settings_generation;
void platform_log(const char *format, ...);
/* (@shot: the name of the next frame's screenshot, read by the present) */
char halo_screenshot_name[64];

static void main_test_camera(char const *arguments);
static void main_test_trigger_volume(char const *name);
static void main_test_bsp_switch_trigger(char const *arguments);
/* damage.c's */
void damage_kill_object_for_player(long object_index, long player_index);

static void main_test_commands_update(
	void)
{
	static boolean pan_active;
	static float pan[7];
	static long pan_start;
	static int parsed = 0;
	static struct { long tick; char command[120]; boolean done; } commands[160];
	static short command_count;
	short index;

	if (!parsed)
	{
		const char *setting = getenv("HALO_TEST_COMMANDS");

		parsed = 1;
		while (setting && *setting && command_count < NUMBEROF(commands))
		{
			const char *end = strchr(setting, ';');
			const char *colon = strchr(setting, ':');
			size_t length;

			if (!end)
				end = setting + strlen(setting);
			if (colon && colon < end)
			{
				commands[command_count].tick = atol(setting);
				length = (size_t)(end - colon - 1);
				if (length >= sizeof(commands[0].command))
					length = sizeof(commands[0].command) - 1;
				memcpy(commands[command_count].command, colon + 1, length);
				commands[command_count].command[length] = 0;
				commands[command_count].done = FALSE;
				command_count++;
			}
			setting = *end ? end + 1 : end;
		}
	}
	if (!command_count || !game_in_progress())
		return;
	for (index = 0; index < command_count; index++)
	{
		if (commands[index].done || game_time_get() < commands[index].tick)
			continue;
		commands[index].done = TRUE;
		platform_log("test command at tick %ld: %s", (long)game_time_get(), commands[index].command);
		if (!strcmp(commands[index].command, "@skip"))
			main_skip_cinematic();
		else if (!strcmp(commands[index].command, "@quit"))
		{
			game_state_save_to_persistent_storage();
			main_goto_main_menu();
		}
		else if (!strncmp(commands[index].command, "@shot ", 6))
			csstrncpy(halo_screenshot_name, commands[index].command + 6, sizeof(halo_screenshot_name) - 1);
		else if (!strncmp(commands[index].command, "@pan ", 5))
		{
			pan_active = sscanf(commands[index].command + 5, "%f %f %f %f %f %f %f", &pan[0], &pan[1], &pan[2], &pan[3],
				&pan[4], &pan[5], &pan[6]) == 7;
			pan_start = game_time_get();
		}
		else if (!strncmp(commands[index].command, "@camera ", 8))
			main_test_camera(commands[index].command + 8);
		else if (!strcmp(commands[index].command, "@where"))
		{
			/* (@where: every vehicle's and scenery object's tag and place,
			in the log, for aiming @camera at one) */
			struct object_iterator iterator;
			struct object_datum *object;

			object_iterator_new(&iterator, _object_mask_vehicle | _object_mask_scenery, 0);
			while ((object = object_iterator_next(&iterator)) != NULL)
				platform_log("where %lx %s at %.2f %.2f %.2f", (unsigned long)iterator.index,
					tag_get_name(object->definition_index), object->object.position.x, object->object.position.y,
					object->object.position.z);
		}
		else if (!strncmp(commands[index].command, "@set ", 5))
		{
			char variable[64], value[64];

			if (sscanf(commands[index].command + 5, "%63s %63s", variable, value) == 2 &&
				!(halo_test_setting_hook && halo_test_setting_hook(variable, value)))
			{
				setenv(variable, value, 1);
				__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
			}
		}
		else if (!strncmp(commands[index].command, "@tv ", 4))
			main_test_trigger_volume(commands[index].command + 4);
		/* network co-op (port/linux/game/network_coop.c): "@vote" is this
		machine's press of skip in a cinematic; "@bsp P K" stands player P
		(its absolute index, -1 every player) in the Kth of the loaded BSP's
		switch trigger volumes; "@kill P" kills player P */
		else if (!strcmp(commands[index].command, "@vote"))
		{
			platform_log("test command: skip pressed (offered %d, skippable %d)", network_coop_skip_offered(),
				cinematic_can_be_skipped());
			if ((cinematic_can_be_skipped() || network_coop_skip_offered()) && !network_coop_vote_skip())
				main_skip_cinematic();
		}
		else if (!strncmp(commands[index].command, "@bsp ", 5))
			main_test_bsp_switch_trigger(commands[index].command + 5);
		else if (!strcmp(commands[index].command, "@bsplist"))
			main_test_bsp_switch_trigger("-2 -1");
		/* "@pos P X Y Z": player P stands there */
		else if (!strncmp(commands[index].command, "@pos ", 5))
		{
			long player_absolute_index = NONE;
			real_point3d position;
			struct data_iterator iterator;
			struct player_datum *player;

			if (sscanf(commands[index].command + 5, "%ld %f %f %f", &player_absolute_index, &position.x, &position.y,
				&position.z) == 4)
			{
				data_iterator_new(&iterator, player_data);
				while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
				{
					if (DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index) == player_absolute_index &&
						player->unit_index != NONE)
					{
						object_set_position(player->unit_index, &position, NULL, NULL);
					}
				}
			}
		}
		else if (!strncmp(commands[index].command, "@kill ", 6))
		{
			long player_absolute_index = atol(commands[index].command + 6);
			struct data_iterator iterator;
			struct player_datum *player;

			data_iterator_new(&iterator, player_data);
			while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
			{
				if (DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index) == player_absolute_index &&
					player->unit_index != NONE)
				{
					platform_log("test command: player %ld killed", player_absolute_index);
					damage_kill_object_for_player(player->unit_index, NONE);
				}
			}
		}
		else
			hs_compile_and_evaluate(commands[index].command);
	}
	if (pan_active)
	{
		/* (@pan: the camera turned by the yaw and pitch rates each tick) */
		char line[120];
		long ticks = game_time_get() - pan_start;

		snprintf(line, sizeof(line), "%f %f %f %f %f", pan[0], pan[1], pan[2], pan[3] + pan[5] * ticks, pan[4] + pan[6] * ticks);
		main_test_camera(line);
	}
}

static void main_test_trigger_volume(
	char const *name)
{
	struct scenario *scenario = global_scenario_get();
	short index;

	for (index = 0; index < scenario->trigger_volumes.count; index++)
	{
		struct scenario_trigger_volume *volume =
			TAG_BLOCK_GET_ELEMENT(&scenario->trigger_volumes, index, struct scenario_trigger_volume);
		real_point3d centre;
		struct data_iterator iterator;
		struct player_datum *player;

		if (strcmp(volume->name, name))
			continue;
		if (volume->type == _scenario_trigger_volume_type_axis_aligned)
		{
			centre.x = (volume->bounds.x0 + volume->bounds.x1) / 2.0f;
			centre.y = (volume->bounds.y0 + volume->bounds.y1) / 2.0f;
			centre.z = (volume->bounds.z0 + volume->bounds.z1) / 2.0f;
		}
		else
		{
			real_matrix4x3 matrix;
			real_point3d middle = { volume->extents.i / 2.0f, volume->extents.j / 2.0f, volume->extents.k / 2.0f };

			matrix4x3_from_point_and_vectors(&matrix, &volume->position, &volume->forward, &volume->up);
			matrix4x3_transform_point(&matrix, &middle, &centre);
		}
		platform_log("test command: %s at %.2f %.2f %.2f", name, centre.x, centre.y, centre.z);
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
			if (player->unit_index != NONE)
				object_set_position(player->unit_index, &centre, NULL, NULL);
		return;
	}
	platform_log("test command: no trigger volume %s", name);
}

/* "@bsp P K": player P (-1 every player) stands in the middle of the Kth
switch trigger volume whose source is the loaded structure BSP (a
scenario's block of them, as players.c has it) */
struct main_test_bsp_switch_trigger_volume
{
	short trigger_volume_index;
	short source_structure_bsp_index;
	short destination_structure_bsp_index;
	short cutscene_flag_index;
};

static void main_test_bsp_switch_trigger(
	char const *arguments)
{
	struct scenario *scenario = global_scenario_get();
	long player_absolute_index = NONE;
	int wanted = 0;
	short index;
	int found = 0;

	sscanf(arguments, "%ld %d", &player_absolute_index, &wanted);
	for (index = 0; index < scenario->bsp_switch_trigger_volumes.count; index++)
	{
		struct main_test_bsp_switch_trigger_volume *switch_volume = TAG_BLOCK_GET_ELEMENT(
			&scenario->bsp_switch_trigger_volumes, index, struct main_test_bsp_switch_trigger_volume);
		struct scenario_trigger_volume *volume;
		real_point3d centre;
		struct data_iterator iterator;
		struct player_datum *player;

		if (player_absolute_index != -2 &&
			(switch_volume->source_structure_bsp_index != global_structure_bsp_index_get() || found++ != wanted))
		{
			continue;
		}
		volume = TAG_BLOCK_GET_ELEMENT(&scenario->trigger_volumes, switch_volume->trigger_volume_index,
			struct scenario_trigger_volume);
		if (volume->type == _scenario_trigger_volume_type_axis_aligned)
		{
			centre.x = (volume->bounds.x0 + volume->bounds.x1) / 2.0f;
			centre.y = (volume->bounds.y0 + volume->bounds.y1) / 2.0f;
			centre.z = (volume->bounds.z0 + volume->bounds.z1) / 2.0f;
		}
		else
		{
			real_matrix4x3 matrix;
			real_point3d middle = { volume->extents.i / 2.0f, volume->extents.j / 2.0f, volume->extents.k / 2.0f };

			matrix4x3_from_point_and_vectors(&matrix, &volume->position, &volume->forward, &volume->up);
			matrix4x3_transform_point(&matrix, &middle, &centre);
		}
		platform_log("test command: BSP switch trigger %d (%s, BSP %d to %d) at %.2f %.2f %.2f for player %ld", index,
			volume->name, (int)switch_volume->source_structure_bsp_index,
			(int)switch_volume->destination_structure_bsp_index, centre.x, centre.y, centre.z, player_absolute_index);
		/* ("@bsplist": every one listed, nobody moved) */
		if (player_absolute_index == -2)
			continue;
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->unit_index != NONE && (player_absolute_index == NONE ||
				DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index) == player_absolute_index))
			{
				object_set_position(player->unit_index, &centre, NULL, NULL);
			}
		}
		return;
	}
	if (player_absolute_index != -2)
		platform_log("test command: no BSP switch trigger %d from BSP %d", wanted, (int)global_structure_bsp_index_get());
}

static void main_test_camera(
	char const *arguments)
{
	float x, y, z, yaw, pitch;
	FILE *file;

	if (sscanf(arguments, "%f %f %f %f %f", &x, &y, &z, &yaw, &pitch) == 5 &&
		(file = fopen("d:\\camera.txt", "w")) != NULL)
	{
		real yaw_radians = DEGREES_TO_RADIANS(yaw), pitch_radians = DEGREES_TO_RADIANS(pitch);

		fprintf(file, "%f %f %f\n%f %f %f\n%f %f %f\n%f\n", x, y, z,
			cosine(yaw_radians) * cosine(pitch_radians), sine(yaw_radians) * cosine(pitch_radians), sine(pitch_radians),
			-cosine(yaw_radians) * sine(pitch_radians), -sine(yaw_radians) * sine(pitch_radians), cosine(pitch_radians),
			DEGREES_TO_RADIANS(70.0f));
		fclose(file);
		director_load_camera();
	}
}
#endif

#ifdef HALO_LINUX
/* (HALO_RENDER_PROFILE=1) the main thread's frame outside the render and
the present, by step, every 300 frames: the frame timing's "other" (with
the tick on its thread: the main loop's own work before the render, the
networked game's above all, and the wait for the tick after the present) */
enum
{
	_main_split_input,
	_main_split_network_start,
	_main_split_time_ui,
	_main_split_player_control,
	_main_split_network_end,
	_main_split_camera_engine,
	_main_split_join,
	NUMBER_OF_MAIN_SPLITS
};
static int main_split_enabled = -1;
static unsigned long long main_split_last, main_split_us[NUMBER_OF_MAIN_SPLITS];
static unsigned long main_split_frames;

static void main_split_mark(int step)
{
	extern unsigned long long vita_host_time_us(void) __attribute__((weak));
	unsigned long long now;

	if (main_split_enabled < 0)
	{
		const char *setting = getenv("HALO_RENDER_PROFILE");

		main_split_enabled = setting && atoi(setting) != 0;
	}
	if (main_split_enabled <= 0 || !vita_host_time_us)
		return;
	now = vita_host_time_us();
	if (step >= 0 && main_split_last)
		main_split_us[step] += now - main_split_last;
	main_split_last = now;
}

static void main_split_report(void)
{
	static const char *const names[NUMBER_OF_MAIN_SPLITS] =
		{ "input", "network_start", "time+ui", "player_control", "network_end", "camera+engine", "tick_join" };
	char line[512];
	int n = 0, index;

	if (main_split_enabled <= 0 || ++main_split_frames % 300)
		return;
	for (index = 0; index < NUMBER_OF_MAIN_SPLITS; index++)
	{
		n += snprintf(line + n, sizeof(line) - n, " %s %.2f", names[index], (double)main_split_us[index] / 1000.0 / 300.0);
		main_split_us[index] = 0;
	}
	platform_log("main-split (ms/frame):%s", line);
}
#define MAIN_SPLIT(step) main_split_mark(step)
#endif

void main_loop(
	void)
{
	boolean render_frame;
	long connection;
#ifdef HALO_LINUX
	boolean tick_running = FALSE;
	boolean simulate;
#endif

	if (!game_in_editor())
	{
		csstrncpy(main_globals.soloplayer_map_name, "levels\\b30\\b30", NUMBEROF(main_globals.soloplayer_map_name)-1);
		main_globals.soloplayer_map_name[NUMBEROF(main_globals.soloplayer_map_name)-1] = '\0';
	}

	main_globals.want_to_be_at_main_menu = !game_in_editor();
	main_globals.switch_to_structure_bsp_index = NONE;
	main_globals.halt_time_scale = TRUE;

	console_initialize();
	debug_keys_initialize();
	game_initialize();
	console_startup();
	main_setup_connection();
	main_initialize_time();

	while (TRUE)
	{
#ifdef HALO_LINUX
		main_test_commands_update();
		{
			void halo_shader_tour_update(void);

			halo_shader_tour_update();
		}
#endif
		if (!game_in_editor())
		{
			if (main_globals.switch_to_structure_bsp_index!=NONE)
			{
				main_switch_to_structure_bsp_private();
			}

			if (main_globals.lost_map)
			{
				main_lost_map_private();
			}

			if (main_globals.won_map)
			{
				main_won_map_private();
			}

			if (main_globals.respawn)
			{
				main_respawn_private();
			}

			if (main_globals.save_map_completed)
			{
				main_saving_map_private();
			}

			if (main_globals.defer_map_change)
			{
				main_change_map_name();
			}

			if (main_globals.revert_map)
			{
				main_revert_map_private();
			}

			if (main_globals.skip_cinematic)
			{
				main_skip_cinematic_private();
			}

			if (main_globals.reset_map)
			{
				main_reset_map_private();
			}

			if (main_globals.save_core)
			{
				main_save_core_private();
			}

			if (main_globals.load_core)
			{
				main_load_core_private();
			}

			if (main_globals.want_to_be_at_main_menu)
			{
				main_menu_load();
			}

			if (main_globals.load_last_solo_level)
			{
				main_load_last_solo_map();
			}

			if (main_globals.run_xdemos)
			{
				main_run_demos_private();
			}

			if (main_globals.skip)
			{
				main_skip_private();
			}

			if (main_globals.queue_map)
			{
				main_queue_map_private();
			}
		}
		else if (main_globals.reset_map)
		{
			main_reset_map_private();
		}

		profile_frame_start();
#ifdef HALO_LINUX
		halo_frame_timing(_frame_timing_frame_start, 0);
		MAIN_SPLIT(-1);
#endif
		input_frame_begin();
		input_update();
		input_abstraction_update();
		shell_idle();
		event_manager_update();
		telnet_console_process();

		if (!shell_application_is_paused())
		{
			render_frame = TRUE;

#ifdef HALO_LINUX
			/* automated system link tests (port/linux/game/network_test.c) */
			network_test_update(main_globals.main_menu_scenario_loaded, main_globals.seconds_elapsed);
			system_link_shortcut_update(main_globals.main_menu_scenario_loaded);
			MAIN_SPLIT(_main_split_input);
#endif
			connection = main_globals.connection;
			if (connection==_game_connection_network_client)
			{
				if (!network_game_client_start_frame())
				{
					display_error_when_main_menu_loaded(6);
					error(_error_silent, "the game host went down");
					network_game_abort();
				}
			}
			else if (connection==_game_connection_network_server)
			{
				if (!network_game_client_start_frame())
				{
					display_error_when_main_menu_loaded(1);
					error(_error_silent, "the game host went down");
					network_game_abort();
				}
				else if (!network_game_server_start_frame())
				{
					display_error_when_main_menu_loaded(1);
					error(_error_silent, "the game host went down");
					network_game_abort();
				}
			}
			else if (connection==_game_connection_film_playback)
			{
				break;
			}

#ifdef HALO_LINUX
			MAIN_SPLIT(_main_split_network_start);
#endif
			main_update_time();
			process_ui_widgets();
			bink_playback_update();
#ifdef HALO_LINUX
			MAIN_SPLIT(_main_split_time_ui);
#endif

			if ((!game_in_editor() && (input_key_is_down(_key_end) || input_key_is_down(_key_escape))) || editor_should_exit())
			{
				main_movie_stop();

				if (!game_engine_running())
				{
#ifdef HALO_LINUX
					if (!main_globals.reset_map)
						platform_log("escape or end key: the map starts over");
#endif
					main_reset_map();
				}
			}

			if (game_in_progress())
			{
#ifdef HALO_LINUX
				simulate = FALSE;
#endif
				terminal_update();

				if (!console_update() || main_globals.connection!=_game_connection_local)
				{
#ifdef HALO_LINUX
					simulate = TRUE;
#endif
					debug_keys_update();
					cheats_update();
					player_control_update((real)main_globals.halt_time_scale*main_globals.seconds_elapsed);
#ifdef HALO_LINUX
					MAIN_SPLIT(_main_split_player_control);
#endif

					connection = main_globals.connection;
					if (connection>_game_connection_local && connection<=_game_connection_network_server && !network_game_client_end_frame())
					{
						display_error_when_main_menu_loaded(1);
						network_game_abort();
					}
#ifdef HALO_LINUX
					MAIN_SPLIT(_main_split_network_end);
#endif

#ifdef HALO_LINUX
					/* (port: with the tick on its thread it runs alongside
					this frame's render instead, below) */
					if (!halo_tick_thread_enabled())
					{
						halo_frame_timing(_frame_timing_tick_start, 0);
						halo_view_points_capture();
						game_time_update((real)main_globals.halt_time_scale*main_globals.seconds_elapsed);
						halo_frame_timing(_frame_timing_tick_end, 0);
					}
#else
					game_time_update((real)main_globals.halt_time_scale*main_globals.seconds_elapsed);
#endif

					render_frame = main_globals.main_menu_scenario_loaded ||
						(main_globals.halt_time_scale &&
							(game_time_get_paused() || game_time_get_elapsed()>0 || game_time_get_speed()<1.0f));
#ifdef HALO_LINUX
					/* frames between ticks too (render_interpolation.c) */
					if (halo_interpolation_enabled())
						render_frame = main_globals.main_menu_scenario_loaded || main_globals.halt_time_scale;
#endif
					render_frame &= !game_engine_running() || game_time_get()>=3;

					collision_log_continue_period(1);
					director_update((real)main_globals.halt_time_scale*main_globals.seconds_elapsed);
					observer_update((real)main_globals.halt_time_scale*main_globals.seconds_elapsed);
					collision_log_end_period();
					game_engine_update_non_deterministic((real)main_globals.halt_time_scale*main_globals.seconds_elapsed);
#ifdef HALO_LINUX
					MAIN_SPLIT(_main_split_camera_engine);
#endif
				}

#ifdef HALO_LINUX
				/* (port: with the tick on its thread it runs after this
				point, so the save check waits for the tick: below) */
				if (main_globals.saving_map && !halo_tick_thread_enabled())
#else
				if (main_globals.saving_map)
#endif
				{
					main_save_map_private();
				}

#ifdef HALO_LINUX
				if (simulate && halo_tick_thread_enabled())
				{
					/* (the cameras just updated: view_distance.c) */
					halo_view_points_capture();
					halo_tick_thread_start((real)main_globals.halt_time_scale*main_globals.seconds_elapsed);
					main_tick_pacing_tick_started();
					tick_running = TRUE;
				}
#endif
				if (render_frame && !debug_no_drawing)
				{
					profile_render_start();
#ifdef HALO_LINUX
					{
						/* (debug) HALO_ELAPSED_STATS=1: the elapsed ticks the
						render sees (game_time.c), counted every 300 frames */
						static int stats = -1;
						static unsigned long seen[4], frames;

						if (stats < 0)
							stats = getenv("HALO_ELAPSED_STATS") && atoi(getenv("HALO_ELAPSED_STATS"));
						if (stats)
						{
							short elapsed = game_time_get_elapsed();

							seen[elapsed <= 0 ? 0 : elapsed >= 3 ? 3 : elapsed]++;
							if (++frames % 300 == 0)
							{
								platform_log("render elapsed ticks: 0:%lu 1:%lu 2:%lu 3+:%lu", seen[0], seen[1], seen[2], seen[3]);
								memset(seen, 0, sizeof(seen));
							}
						}
					}
					halo_frame_timing(_frame_timing_render_start, 0);
					render_interpolation_frame_begin();
					{
						/* (harness) HALO_RENDER_SLOWDOWN=<factor>: the render
						takes that many times as long (a sleep for the rest),
						as HALO_TICK_SLOWDOWN the tick (tick_thread.c) */
						extern unsigned long long vita_host_time_us(void) __attribute__((weak));
						extern void vita_host_sleep_us(unsigned long microseconds) __attribute__((weak));
						extern double atof(const char *text);
						static real slowdown = -1.0f;
						unsigned long long render_started = vita_host_time_us ? vita_host_time_us() : 0;

						if (slowdown < 0.0f)
						{
							const char *setting = getenv("HALO_RENDER_SLOWDOWN");

							slowdown = setting ? (real)atof(setting) : 0.0f;
						}
						main_game_render((double)main_globals.seconds_elapsed);
						if (slowdown > 1.0f && render_started && vita_host_sleep_us)
							vita_host_sleep_us((unsigned long)((slowdown - 1.0f) * (real)(vita_host_time_us() - render_started)));
					}
					render_interpolation_frame_end();
					halo_frame_timing(_frame_timing_render_end, 0);
#else
					main_game_render((double)main_globals.seconds_elapsed);
#endif
					profile_render_end();
				}
			}
			else
			{
				profile_render_start();
				main_pregame_render();
				profile_render_end();
			}

#ifdef HALO_LINUX
			halo_frame_timing(_frame_timing_present_start, 0);
#endif
			main_rasterizer_throttle();

			if (render_frame && !debug_no_drawing)
			{
				main_present_frame();
			}
#ifdef HALO_LINUX
			halo_frame_timing(_frame_timing_present_end, 0);
			MAIN_SPLIT(-1);
			if (tick_running)
			{
				main_tick_pacing_presented();
				halo_tick_thread_join();
				main_tick_pacing_joined();
				MAIN_SPLIT(_main_split_join);
				halo_frame_timing_tick_threaded(halo_tick_thread_last_us() * 1000ull);
				tick_running = FALSE;
			}
			else if (halo_tick_thread_enabled())
			{
				/* (a frame with no tick elapsed no game time for the next
				frame's render to count down: game_time.c) */
				extern volatile short halo_render_elapsed_ticks;
				halo_render_elapsed_ticks = 0;
			}
			/* (port) the save check comes after the tick, as on the Xbox,
			where the tick ran before it in the same frame: a save the
			tick's scripts ask for is then taken at the top of the next
			frame, before the next tick. Checked before the threaded tick
			instead, the save landed a tick late, after the script's next
			step had already run - cinematic_skip_start's
			(sleep_until (not (game_saving)) 1) (not (game_reverted))
			was past when the checkpoint was taken, so skipping a cinematic
			reverted to a checkpoint inside it and replayed it */
			if (halo_tick_thread_enabled() && game_in_progress() && main_globals.saving_map)
			{
				main_save_map_private();
			}
#endif
		}

		input_frame_end();
		profile_frame_end();
#ifdef HALO_LINUX
		halo_frame_timing(_frame_timing_frame_end, game_in_progress() ? (unsigned long)game_time_get() : 0);
		halo_load_profile_frame_end();
		main_split_report();
		{
			void halo_net_detail_report(void);

			halo_net_detail_report();
		}
#endif
		main_frame_rate_debug();

		if (main_globals.restart_time)
		{
			main_globals.restart_time = FALSE;
			main_reset_time();
			main_globals.halt_time_scale = TRUE;
		}
	}

	error(_error_silent, "end of saved film");
	main_exit();

	return;
}

/* ---------- private code */
