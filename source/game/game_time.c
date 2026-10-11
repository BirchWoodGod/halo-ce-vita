/*
GAME_TIME.C

symbols in this file:
000A4D00 0020:
	_game_time_initialized (0000)
000A4D20 0030:
	_game_time_initialize (0000)
000A4D50 0050:
	_game_time_initialize_for_new_map (0000)
000A4DA0 0020:
	_game_time_dispose_from_old_map (0000)
000A4DC0 0010:
	_game_time_dispose (0000)
000A4DD0 0040:
	_game_time_end (0000)
000A4E10 0040:
	_game_time_get (0000)
000A4E50 0040:
	_game_time_get_elapsed (0000)
000A4E90 0040:
	_local_time_get (0000)
000A4ED0 0040:
	_local_time_get_elapsed (0000)
000A4F10 0040:
	_game_predicting (0000)
000A4F50 0050:
	_game_in_progress (0000)
000A4FA0 0040:
	_game_time_get_paused (0000)
000A4FE0 0050:
	_game_time_set_paused (0000)
000A5030 0040:
	_game_time_get_speed (0000)
000A5070 0050:
	_game_time_set_speed (0000)
000A50C0 0010:
	_game_time_statistics_new (0000)
000A50D0 01e0:
	_game_time_statistics_frame (0000)
000A52B0 00e0:
	_game_time_start (0000)
000A5390 0360:
	_game_time_update (0000)
0025C480 0012:
	??_C@_0BC@ENHAGKAA@game?5time?5globals?$AA@ (0000)
0025C494 0035:
	??_C@_0DF@KKMNPDPK@game_time_globals?5?$CG?$CG?5?$CBgame_time_@ (0000)
0025C4CC 0020:
	??_C@_0CA@INLHHDGN@c?3?2halo?2SOURCE?2game?2game_time?4c?$AA@ (0000)
0025C4EC 0012:
	??_C@_0BC@NOJBGBLH@game_time_globals?$AA@ (0000)
0025C500 0034:
	??_C@_0DE@PPGKJIJK@game_time_globals?5?$CG?$CG?5game_time_g@ (0000)
0025C534 001b:
	??_C@_0BL@DECMFGMD@?$CBgame_time_globals?9?$DOactive?$AA@ (0000)
0025C550 0021:
	??_C@_0CB@OIFBECDL@ticks_elapsed?5?$DM?$DN?5maximum_actions@ (0000)
0025C578 004c:
	??_C@_0EM@FOLKNDLG@game_time_globals?9?$DOleftover_dt?$DO?$DN@ (0000)
0025C5C8 004c:
	??_C@_0EM@FBBKPKLD@update?5server?5is?5too?5far?5ahead?5o@ (0000)
0043EE04 002c:
	_bss_0043ee04 (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries_windows.h"
#include "real_math.h"
#include "game.h"
#include "player_queues_new.h"
#include "networking/network_client_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_server_manager.h"
#include "saved games/game_state.h"
/* port/linux/game/network_distributed.c's */
void network_distributed_tick(void);
/* (debug) HALO_TICK_HASH's simulation oracle, network_distributed.c's */
void halo_tick_hash_after_tick(void);

/* ---------- constants */

enum
{
	SOME_LARGE_NUMBER_OF_TICKS = 1000, // 0x03E8
};

/* ---------- macros */

/* ---------- structures */

struct game_time_globals_struct
{
	boolean initialized;
	boolean active;
	boolean paused;
	/* (these three unused: kept for the game state's layout) */
	short monitor_state;
	short monitor_counter;
	short monitor_latency;
	long local_time;
	short last_local_time_elapsed;
	long server_time;
	real speed;
	real leftover_dt;
};

/* ---------- prototypes */

/* ---------- globals */

static struct game_time_globals_struct *game_time_globals;

/* ---------- public code */

boolean game_time_initialized(
	void)
{
	return (game_time_globals && game_time_globals->initialized);
}

void game_time_initialize(
	void)
{
	game_time_globals = (struct game_time_globals_struct *)game_state_malloc("game time globals", NULL, sizeof(*game_time_globals));
	memset(game_time_globals, 0, sizeof(*game_time_globals));

	return;
}

void game_time_initialize_for_new_map(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 131, game_time_globals && !game_time_globals->initialized);
	memset(game_time_globals, 0, sizeof(*game_time_globals));
	game_time_globals->initialized = TRUE;

	return;
}

void game_time_dispose_from_old_map(
	void)
{
	if (game_time_globals)
	{
		game_time_globals->initialized = FALSE;
		game_time_globals->active = FALSE;
	}

	return;
}

void game_time_dispose(
	void)
{
	return;
}

void game_time_end(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 196, game_time_globals);
	game_time_globals->active = FALSE;

	return;
}

void game_time_set_distributed(
	long time)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 0, game_time_globals && game_time_globals->initialized);

	game_time_globals->local_time = time;
	game_time_globals->server_time = time;
	game_time_globals->leftover_dt = 0.f;

	return;
}

long game_time_get(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 463, game_time_globals && game_time_globals->initialized);

	return game_time_globals->local_time;
}

/* (port) with the tick on its own thread (tick_thread.c), game_time_update
runs while the frame renders, and resets the elapsed ticks to 0 first: the
render's once-a-frame countdowns (screen flashes, camera shake and
impulses, HUD flashes, cinematic titles) read 0 nearly every frame - a
hunter's hit left the screen red for a minute. The render reads instead
the elapsed ticks of the update whose state it draws, which the tick
thread publishes when it finishes (halo_tick_thread_join) */
extern int halo_epoch_threaded;
int halo_epoch_on_mutator(void);
volatile short halo_render_elapsed_ticks;

static short elapsed_for_caller(void)
{
	if (halo_epoch_threaded && !halo_epoch_on_mutator())
		return halo_render_elapsed_ticks;
	return game_time_globals->last_local_time_elapsed;
}

/* the same for the fraction of a tick the game clock had run past the
state the render draws (render_interpolation.c's first-person weapon): the
running tick's game_time_update has already rewritten leftover_dt, so the
render takes the finished update's, published at the join */
volatile float halo_render_tick_fraction = 1.0f;

real halo_render_tick_fraction_get(void)
{
	if (halo_epoch_threaded && !halo_epoch_on_mutator())
		return halo_render_tick_fraction;
	return game_time_get_tick_fraction();
}

short game_time_get_elapsed(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 471, game_time_globals && game_time_globals->initialized);

	return elapsed_for_caller();
}

/* Published to the render thread after an update finishes. */
short halo_game_time_last_elapsed(void)
{
	return game_time_globals ? game_time_globals->last_local_time_elapsed : 0;
}

long local_time_get(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 479, game_time_globals && game_time_globals->initialized);

	return game_time_globals->local_time;
}

short local_time_get_elapsed(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 487, game_time_globals && game_time_globals->initialized);

	return elapsed_for_caller();
}

boolean game_predicting(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 495, game_time_globals && game_time_globals->initialized);

	return FALSE;
}

boolean game_in_progress(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 505, game_time_globals);

	if (game_time_globals->initialized)
	{
		if (game_time_globals->active)
		{
			return TRUE;
		}

		/* bug? */
		if (game_time_globals->paused)
		{
			return TRUE;
		}
		else
		{
			return FALSE;
		}
	}

	return FALSE;
}

boolean game_time_get_paused(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 533, game_time_globals);

	return game_time_globals->paused;
}

void game_time_set_paused(
	boolean paused)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 541, game_time_globals);

	if (game_time_globals->initialized)
	{
		game_time_globals->active = !paused;
	}

	game_time_globals->paused = paused;

	return;
}

/* how far the clock has run into the next tick, 0 to 1: the native ports
draw frames between ticks (port/linux/game/render_interpolation.c) */
real game_time_get_tick_fraction(
	void)
{
	real fraction;

	if (!game_time_globals || !game_time_globals->active || game_time_globals->paused)
		return 1.0f;
	fraction = game_time_globals->leftover_dt * game_time_globals->speed * TICKS_PER_SECOND;
	return PIN(fraction, 0.0f, 1.0f);
}

real game_time_get_speed(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 555, game_time_globals);

	/* port: a client of another's game runs at the host's speed (its own
	set before it joined too: cheats_network_client_enforce) */
	if (network_game_distributed_client())
		return 1.0f;
	return game_time_globals->speed;
}

void game_time_set_speed(
	real speed)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 562, game_time_globals);

	game_time_globals->speed = speed;

	return;
}

/* port: the game's own speed put back; whether it was another
(cheats_network_client_enforce) */
boolean game_time_reset_speed(
	void)
{
	boolean changed;

	if (!game_time_globals)
		return FALSE;
	changed = game_time_globals->speed != 1.0f;
	game_time_globals->speed = 1.0f;

	return changed;
}

/* whether a client's clock waits for the host's first game update, which
brings the host's time (the host ticks only once every machine has
loaded) */
boolean game_time_held(
	void)
{
	struct network_game_client *client;

	if (game_connection() != _game_connection_network_client)
		return FALSE;
	client = global_network_game_client_get();
	return client && !network_game_client_server_has_started_game(client);
}

void game_time_start(
	void)
{
	short connection;

	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 162, game_time_globals && game_time_globals->initialized);
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 163, !game_time_globals->active);

	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 562, game_time_globals);

	game_time_globals->speed = 1.f;
	game_time_globals->leftover_dt = 0;
	game_time_globals->active = TRUE;

	connection = game_connection();

	switch (connection)
	{
	case _game_connection_local:
	case _game_connection_network_server:
	{
		update_server_start();
		break;
	}
	case _game_connection_network_client:
	case _game_connection_film_playback:
	{
		update_client_start();
		break;
	}
	}

	return;
}


#ifdef HALO_LINUX
/* (HALO_TICK_PROFILE) a game time update of over 100 ms - the frame's
ticks with the update queues, the distributed netcode's per-tick work and
the frame's particles and sounds - is logged by step ("tick-run-hitch"),
to name what the main thread waits a second for in a hitch */
#include <stdlib.h>
enum { _run_update_queues, _run_game_ticks, _run_distributed, _run_game_frame, NUMBER_OF_RUN_STEPS };
static unsigned long long run_last, run_us[NUMBER_OF_RUN_STEPS];
/* the game ticks' time since start-up, the update's other work apart (read
by main.c at the join) */
volatile unsigned long long halo_game_ticks_us;
/* (harness) HALO_TICK_SLOWDOWN (tick_thread.c) */
void halo_game_tick_slowdown(unsigned long long tick_us);
static unsigned long run_ticks;
static int run_enabled = -1;
unsigned long long vita_host_time_us(void) __attribute__((weak));
void platform_log(const char *format, ...);

static void halo_run_mark(int step)
{
	unsigned long long now;

	if (run_enabled < 0)
	{
		const char *setting = getenv("HALO_TICK_PROFILE");

		run_enabled = setting && atoi(setting) != 0 && vita_host_time_us;
	}
	if (run_enabled <= 0)
		return;
	now = vita_host_time_us();
	if (step >= 0)
	{
		run_us[step] += now - run_last;
		if (step == _run_game_ticks)
			run_ticks++;
	}
	run_last = now;
}

static void halo_run_report(void)
{
	unsigned long long total = 0;
	int step;

	if (run_enabled <= 0)
		return;
	for (step = 0; step < NUMBER_OF_RUN_STEPS; step++)
		total += run_us[step];
	if (total > 100000)
		platform_log("tick-run-hitch: %.1f ms: update queues %.1f, %lu ticks %.1f, distributed %.1f, game frame %.1f",
			total / 1000.0, run_us[_run_update_queues] / 1000.0, run_ticks, run_us[_run_game_ticks] / 1000.0,
			run_us[_run_distributed] / 1000.0, run_us[_run_game_frame] / 1000.0);
	for (step = 0; step < NUMBER_OF_RUN_STEPS; step++)
		run_us[step] = 0;
	run_ticks = 0;
}
#define HALO_RUN_MARK(step) halo_run_mark(step)
#define HALO_RUN_REPORT() halo_run_report()
#else
#define HALO_RUN_MARK(step) ((void)0)
#define HALO_RUN_REPORT() ((void)0)
#endif

void game_time_update(
	real time_delta_sec)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 205, game_time_globals);

	if (game_time_globals->active)
	{
		long connection;
		long ticks_elapsed;
		real ticks_per_second = game_time_globals->speed*TICKS_PER_SECOND;

		/* The native builds draw several frames per tick
		(port/linux/game/render_interpolation.c). A frame that runs no tick has
		elapsed no game time: without this, the ticks of the last frame that
		ran some would count again on every frame after it, and whatever
		advances by game_time_get_elapsed() once a frame (chapter titles, HUD
		messages and flashes, screen flashes, camera shake and impulses) would
		run as many times too fast as there are frames per tick. On the Xbox
		every frame ran at least one tick. */
		game_time_globals->last_local_time_elapsed = 0;

		if (ticks_per_second > 0.f)
		{
			boolean discard_leftover_time;
			real game_time;
			real ticks_elapsed_real;

			connection = game_connection();
			switch (connection)
			{
			case _game_connection_film_playback:
				connection = TICKS_PER_SECOND;
				discard_leftover_time = FALSE;
				goto calculate_elapsed_ticks;
			case _game_connection_network_client:
			case _game_connection_network_server:
				/* (the distributed netcode: every machine ticks on its own
				clock, and the host waits for nobody) */
				connection = TICKS_PER_SECOND;
				break;
			case _game_connection_local:
				connection = 7;
				break;
			default:
				connection = 7;
				break;
			}
			discard_leftover_time = TRUE;

		calculate_elapsed_ticks:
			game_time = time_delta_sec + game_time_globals->leftover_dt;
			ticks_elapsed_real = (real)floor(game_time*ticks_per_second);
			ticks_elapsed = (long)(ticks_elapsed_real <= (real)SOME_LARGE_NUMBER_OF_TICKS ?
				ticks_elapsed_real : (real)SOME_LARGE_NUMBER_OF_TICKS);
			if (ticks_elapsed > connection)
			{
				ticks_elapsed = connection;
				if (discard_leftover_time)
					game_time = ticks_elapsed_real/ticks_per_second;
			}

			game_time_globals->leftover_dt = game_time - ticks_elapsed_real/ticks_per_second;
			if (game_time_globals->leftover_dt < 0.f)
				game_time_globals->leftover_dt = 0.f;
			match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 306,
				game_time_globals->leftover_dt>=0.f && game_time_globals->leftover_dt<100.f);

			if (ticks_elapsed > 0)
			{
				long final_local_time;
				long maximum_possible_server_time;

				final_local_time = game_time_globals->local_time + ticks_elapsed;
				HALO_RUN_MARK(-1);
				switch (game_connection())
				{
				case _game_connection_local:
					update_client_local_ticks(ticks_elapsed);
					break;
				case _game_connection_network_server:
					network_game_server_update_ticks(global_network_game_server_get(), (short)ticks_elapsed);
					break;
				}
				HALO_RUN_MARK(_run_update_queues);

				/* (a client of the distributed netcode ticks on its own clock,
				with its own input and the latest the host relayed, from the
				host's first game update, which brings the host's time: the
				host ticks only once every machine has loaded) */
				if (game_connection() == _game_connection_network_client)
				{
					maximum_possible_server_time = game_time_held() ?
						game_time_globals->server_time : final_local_time;
				}
				else
					maximum_possible_server_time = update_client_get_maximum_possible_server_time();
				if (maximum_possible_server_time > game_time_globals->server_time)
				{
					long final_server_time = MIN(maximum_possible_server_time, final_local_time);
					long server_updates = final_server_time - game_time_globals->server_time;
					long update_index;

					for (update_index = 0; update_index < server_updates; update_index++)
					{
						HALO_RUN_MARK(-1);
#ifdef HALO_LINUX
						{
							/* (main.c's tick pacing: the ticks' own time, apart
							from the update's per-frame work) */
							unsigned long long tick_started = vita_host_time_us ? vita_host_time_us() : 0;

							game_tick();
							if (tick_started)
							{
								halo_game_tick_slowdown(vita_host_time_us() - tick_started);
								halo_game_ticks_us += vita_host_time_us() - tick_started;
							}
						}
#else
						game_tick();
#endif
						HALO_RUN_MARK(_run_game_ticks);
						render_interpolation_tick();
						game_time_globals->server_time++;
						game_time_globals->local_time++;
						/* the distributed netcode's per-tick state */
						network_distributed_tick();
						HALO_RUN_MARK(_run_distributed);
						/* (debug) HALO_TICK_HASH: the simulation oracle */
						halo_tick_hash_after_tick();
					}
				}

				/* (none while a client's clock waits: a frame that runs no tick
				reports none) */
				game_time_globals->last_local_time_elapsed = game_time_held() ? 0 : (short)ticks_elapsed;
			}
		}

		HALO_RUN_MARK(-1);
		game_frame(game_time_get_speed()*time_delta_sec);
		HALO_RUN_MARK(_run_game_frame);
		HALO_RUN_REPORT();
	}
	else
	{
		game_time_globals->last_local_time_elapsed = 0;
	}

	return;
}

/* ---------- private code */
