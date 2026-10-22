/* tick_thread.c

HALO_TICK_THREAD=1: the game tick on a thread of its own, overlapping the
frame's rendering. main_loop starts the tick (game_time_update) right before
it renders the frame and joins it after the frame is presented, so the
render draws the state the previous tick left while the next tick runs.
The tick and the render share the game's state without locks, as they do
on Xita: an object may be drawn mid-update. On the Vita the tick runs on
the third core (the game's thread has the first, the render worker the
second). */

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

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
#ifdef HALO_VITA
/* (port/vita/host/vita_fourth_core.c: its run time in halo.log) */
void vita_host_thread_watch(const char *role);
#endif
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

/* (port) The waits between the game's thread and the tick (Sierra-III's
handoff, Oct 21 2026, for the battery): a short spin for the near misses,
then asleep until the other side's publish wakes it, so an idle core rests
instead of waking every 50 us as it did. Each publish (started, finished,
main_joining) is made under tick_wake_lock with a broadcast, so a wait that
tests under the lock cannot miss one. pthread condition variables are safe
on both threads here, the Vita's too: the tick is a pthread, and the game's
thread (vita_main.c game_thread, sceKernelCreateThread's) has its pthread
data, having called pthread_create itself (tls_check, then
halo_tick_thread_enabled) - and it already waits on one at each present
(d3d8_gxm.c). A thread of sceKernelCreateThread's that never called
pthread_create must not wait here (vita_settings.c save_wake).

The waits are timed (1-2 s): one that runs out of time with its work there
and no publish since is a lost wake-up, counted and logged - none is
expected (the harness's long runs look for "lost wake-up" in the log). */
static pthread_mutex_t tick_wake_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tick_wake = PTHREAD_COND_INITIALIZER;
/* (under the lock) publishes made, and the lost wake-ups found */
static unsigned long tick_wake_publishes, tick_lost_wakes;

static void tick_publish_begin(void)
{
	pthread_mutex_lock(&tick_wake_lock);
}

static void tick_publish_end(void)
{
	tick_wake_publishes++;
	pthread_cond_broadcast(&tick_wake);
	pthread_mutex_unlock(&tick_wake_lock);
}

/* until ready(): `spin` tests, then asleep; what names the wait in the
log's lost wake-ups */
static void tick_wait(int (*ready)(void), unsigned long spin, const char *what)
{
	/* (the deadline is on the realtime clock, which the condition variable
	times by; should the platform's pthreads time it otherwise - a wait
	that ends at once, or never -, the waits go untimed: a wait ended far
	too soon, twice, says so) */
	static int untimed, early;
	unsigned long spins = 0;

	while (!ready())
	{
		unsigned long lost = 0;
		int said_untimed = 0;

		if (++spins < spin)
			continue;
		pthread_mutex_lock(&tick_wake_lock);
		while (!ready())
		{
			unsigned long seen = tick_wake_publishes;
			unsigned long long before = vita_host_time_us ? vita_host_time_us() : 0;
			struct timespec deadline;
			int result;

			if (untimed)
			{
				pthread_cond_wait(&tick_wake, &tick_wake_lock);
				continue;
			}
			/* (1-2 s on: time() is the C library's realtime clock, which
			the condition variable times by, and the game's build hides
			clock_gettime) */
			deadline.tv_sec = time(NULL) + 2;
			deadline.tv_nsec = 0;
			result = pthread_cond_timedwait(&tick_wake, &tick_wake_lock, &deadline);
			if (result != ETIMEDOUT)
				continue;
			if (before && vita_host_time_us() - before < 500000 && ++early >= 2)
			{
				untimed = said_untimed = 1;
				continue;
			}
			if (ready() && tick_wake_publishes == seen)
				lost = ++tick_lost_wakes;
		}
		pthread_mutex_unlock(&tick_wake_lock);
		if (said_untimed)
			platform_log("tick thread: the timed waits end too soon here: untimed from now on");
		if (lost && lost <= 8)
			platform_log("tick thread: lost wake-up %lu (%s): the wait ran out with its work there", lost, what);
	}
}

static int tick_requested(void)
{
	return __atomic_load_n(&started, __ATOMIC_ACQUIRE) != finished;
}

static int tick_done(void)
{
	return __atomic_load_n(&finished, __ATOMIC_ACQUIRE) == started;
}

static int main_is_joining(void)
{
	return __atomic_load_n(&main_joining, __ATOMIC_ACQUIRE) != 0;
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
#ifdef HALO_VITA
	vita_host_thread_watch("tick");
#endif
	halo_epoch_register_mutator();
	for (;;)
	{
		float delta;

		tick_wait(tick_requested, 500, "the tick waiting for work");
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
		tick_publish_begin();
		__atomic_store_n(&finished, finished + 1, __ATOMIC_RELEASE);
		tick_publish_end();
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
	tick_publish_begin();
	__atomic_store_n(&started, started + 1, __ATOMIC_RELEASE);
	tick_publish_end();
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
	/* (main_joining also ends the tick's halo_tick_wait_for_render) */
	tick_publish_begin();
	__atomic_store_n(&main_joining, 1, __ATOMIC_RELEASE);
	tick_publish_end();
	tick_wait(tick_done, 500, "the game's thread joining the tick");
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
		tick_wait(main_is_joining, 0, "the tick waiting for the render");
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
