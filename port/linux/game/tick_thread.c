/* tick_thread.c

HALO_TICK_THREAD=1: the game tick on a thread of its own, overlapping the
frame's rendering. main_loop starts the tick (game_time_update) right before
it renders the frame and joins it after the frame is presented, so the
render draws the state the previous tick left while the next tick runs.
The tick and the render share the game's state without locks, as they do
on Xita: an object may be drawn mid-update. On the Vita the tick runs on
the third core (the game's thread has the first, the render worker the
second). */

#include <pthread.h>
#include <stdlib.h>

#include "platform.h"
#include "render_epoch.h"
#include "tick_thread.h"

void sound_render(void);
/* the effects' lights' upkeep, which the render may not do while a tick
runs (object_lights.c) */
void lights_update_unattached(void);
void lights_stress_update(void);
/* the decals' upkeep, likewise (rasterizer_xbox.c) */
void rasterizer_decals_update_for_frame(void);
void decals_stress_update(void);
void particle_systems_stress_update(void);

void vita_host_pin_current_thread(int core) __attribute__((weak));
void vita_host_sleep_us(unsigned long microseconds) __attribute__((weak));
int halo_trace_active(void) __attribute__((weak));
/* (the Vita's device, d3d8_gxm.c: waits for the frames the GPU has not drawn yet) */
void halo_render_wait_for_gpu(void) __attribute__((weak));

static int enabled = -1;
static pthread_t thread;
static volatile unsigned long started, finished;
static volatile float pending_delta;
static volatile unsigned long long last_tick_us;
/* set while the main thread waits in halo_tick_thread_join: the frame is
rendered and presented, and nothing but the tick touches the game */
static volatile int main_joining;
/* the finished update's elapsed ticks, published to the render at the join
(game_time.c) */
static volatile short finished_elapsed;
extern volatile short halo_render_elapsed_ticks;
short halo_game_time_last_elapsed(void);
/* ... and the fraction of a tick it left over */
static volatile float finished_fraction = 1.0f;
extern volatile float halo_render_tick_fraction;
float game_time_get_tick_fraction(void);
/* the poses the render draws while the next tick runs (render_interpolation.c) */
void render_tick_poses_capture(void);
void render_tick_poses_tick_started(void);
void render_tick_poses_publish(void);
/* the sound manager's share of the ticks (main.c reports it with the render split) */
volatile unsigned long long halo_tick_sound_us;
volatile unsigned long halo_tick_sound_ticks;
unsigned long long vita_host_time_us(void);

static void pause_briefly(void)
{
	if (vita_host_sleep_us)
		vita_host_sleep_us(50);
	else
		sched_yield();
}

/* (harness) HALO_TICK_SLOWDOWN=<factor>: the tick takes that many times as
long, a sleep for the rest (the Vita's tick is 10-20 times the harness's),
for the pacing of frames and ticks on a fast machine (main.c); each game
tick slowed apart from the update's other work, so the pacing sees them in
the Vita's proportions */
extern volatile unsigned long long halo_game_ticks_us;

static float tick_slowdown(void)
{
	static float slowdown = -1.0f;

	if (slowdown < 0.0f)
	{
		const char *setting = getenv("HALO_TICK_SLOWDOWN");

		slowdown = setting ? (float)atof(setting) : 0.0f;
	}
	return slowdown;
}

void halo_game_tick_slowdown(unsigned long long tick_us)
{
	if (tick_slowdown() > 1.0f && vita_host_sleep_us)
		vita_host_sleep_us((unsigned long)((tick_slowdown() - 1.0f) * (float)tick_us));
}

static void *tick_thread(void *unused)
{
	(void)unused;
	if (vita_host_pin_current_thread)
	{
		/* HALO_TICK_CORE: the core (0-2, default 2); sharing the game's
		core 0 leaves a core free, to tell scheduling starvation of the
		GPU's threads from memory contention */
		const char *setting = getenv("HALO_TICK_CORE");
		int core = setting ? atoi(setting) : 2;

		vita_host_pin_current_thread(core >= 0 && core <= 2 ? core : 2);
	}
	halo_epoch_register_mutator();
	for (;;)
	{
		unsigned long spins = 0;
		float delta;

		while (__atomic_load_n(&started, __ATOMIC_ACQUIRE) == finished)
		{
			if (++spins < 500)
				continue;
			pause_briefly();
		}
		delta = pending_delta;
		halo_epoch_begin();
		{
			unsigned long long before = vita_host_time_us ? vita_host_time_us() : 0;
			unsigned long long ticks_us_before = halo_game_ticks_us;

			if (halo_trace_active && halo_trace_active())
				platform_log("trace: tick begin");
			lights_stress_update();
			decals_stress_update();
			particle_systems_stress_update();
			game_time_update(delta);
			/* (what lights_preprocess_scene and rasterizer_frame_begin do
			unthreaded: the render deleting the retired lights crashed the
			tick walking them, and its decal deletes raced the tick's
			inserts) */
			lights_update_unattached();
			rasterizer_decals_update_for_frame();
			finished_elapsed = halo_game_time_last_elapsed();
			finished_fraction = game_time_get_tick_fraction();
			render_tick_poses_capture();
			if (halo_trace_active && halo_trace_active())
				platform_log("trace: tick updated");
			/* the sound manager's frame update, with the game state it
			reads (main_game_render skips it when threaded) */
			{
				unsigned long long sound_before = vita_host_time_us ? vita_host_time_us() : 0;

				sound_render();
				if (vita_host_time_us)
				{
					unsigned long long sound_us = vita_host_time_us() - sound_before;

					halo_tick_sound_us += sound_us;
					/* (a sound update of over 100 ms, a hitch, is named) */
					if (sound_us > 100000)
						platform_log("sound-hitch: sound_render took %.1f ms", sound_us / 1000.0);
				}
				halo_tick_sound_ticks++;
			}
			last_tick_us = vita_host_time_us ? vita_host_time_us() - before : 0;
			/* (harness) HALO_TICK_SLOWDOWN: the update's work besides its
			ticks (halo_game_tick_slowdown slows those) */
			if (tick_slowdown() > 1.0f && vita_host_sleep_us && last_tick_us)
			{
				unsigned long long ticks_us = halo_game_ticks_us - ticks_us_before;
				unsigned long long other_us = last_tick_us > ticks_us ? last_tick_us - ticks_us : 0;

				vita_host_sleep_us((unsigned long)((tick_slowdown() - 1.0f) * (float)other_us));
				last_tick_us = vita_host_time_us() - before;
			}
		}
		__atomic_store_n(&finished, finished + 1, __ATOMIC_RELEASE);
	}
	return NULL;
}

int halo_tick_thread_enabled(void)
{
	if (enabled < 0)
	{
		const char *setting = getenv("HALO_TICK_THREAD");

		enabled = setting && atoi(setting) != 0;
		if (enabled)
		{
			pthread_attr_t attributes;

			pthread_attr_init(&attributes);
			pthread_attr_setstacksize(&attributes, 16 * 1024 * 1024);
			if (pthread_create(&thread, &attributes, tick_thread, NULL) != 0)
				enabled = 0;
			halo_epoch_threaded = enabled;
			pthread_attr_destroy(&attributes);
		}
	}
	return enabled;
}

void halo_tick_thread_start(float delta)
{
	pending_delta = delta;
	render_tick_poses_tick_started();
	__atomic_store_n(&started, started + 1, __ATOMIC_RELEASE);
}

/* calls the main thread made into the game state while a tick ran, made at
the join instead (halo_tick_thread_defer) */
#define MAXIMUM_DEFERRED_CALLS 16
static void (*deferred_calls[MAXIMUM_DEFERRED_CALLS])(void);
static unsigned long deferred_call_count;

int halo_tick_thread_defer(void (*call)(void))
{
	/* (until the join: a tick that has finished still has its epoch open,
	the marks it made not yet swept) */
	if (enabled <= 0 || halo_epoch_on_mutator() ||
		(__atomic_load_n(&finished, __ATOMIC_ACQUIRE) == started && !__atomic_load_n(&halo_epoch_active, __ATOMIC_ACQUIRE)) ||
		deferred_call_count >= MAXIMUM_DEFERRED_CALLS)
		return 0;
	deferred_calls[deferred_call_count++] = call;
	{
		static unsigned long logged;

		if (logged++ < 4)
			platform_log("tick thread: a call into the game state from the main thread during a tick made at the join");
	}
	return 1;
}

void halo_tick_thread_join(void)
{
	unsigned long spins = 0;

	__atomic_store_n(&main_joining, 1, __ATOMIC_RELEASE);
	while (__atomic_load_n(&finished, __ATOMIC_ACQUIRE) != started)
	{
		if (++spins < 500)
			continue;
		pause_briefly();
	}
	__atomic_store_n(&main_joining, 0, __ATOMIC_RELEASE);
	halo_render_elapsed_ticks = finished_elapsed;
	halo_render_tick_fraction = finished_fraction;
	render_tick_poses_publish();
	halo_epoch_end();
	if (deferred_call_count)
	{
		unsigned long index, count = deferred_call_count;

		deferred_call_count = 0;
		for (index = 0; index < count; index++)
			deferred_calls[index]();
	}
}

void halo_tick_wait_for_render(void)
{
	if (enabled > 0 && halo_epoch_on_mutator())
	{
		if (halo_trace_active && halo_trace_active())
			platform_log("trace: tick waits for the render");
		while (!__atomic_load_n(&main_joining, __ATOMIC_ACQUIRE))
			pause_briefly();
	}
	/* then (with the tick on its thread or not) until the GPU has drawn
	what the render recorded: it reads the bsp's geometry in place */
	if (halo_render_wait_for_gpu)
		halo_render_wait_for_gpu();
}

unsigned long long halo_tick_thread_last_us(void)
{
	return last_tick_us;
}
