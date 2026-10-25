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
/* the last update's begin and end (us), for the join's hitch line */
static volatile unsigned long long tick_began_us, tick_finished_us;
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
instead of waking every 50 us as it did.

On the Vita (and the gxm-null harness: vita_host_wake_*, d3d8_gxm.c's
waits' kind) each of the two threads sleeps on a semaphore of its own: the
tick's (its wait for work and for the render) and the game thread's (its
join). A sleeper sets its flag (tick_waiter_sleeping), tests once more,
then waits; a publish (started, finished, main_joining), sequentially
consistent, then looks at the other side's flag and signals when it
sleeps - either the test sees the publish or the publisher sees the
sleeper. A signal nobody waited for is a spurious wake-up later; every
wait tests again in a loop. The waits are timed, 100 ms.

Not a pthread condition variable there (insider.21-26, Oct 21-25 2026: the
campaign's "freezes in the encounters", the owner's and the testers'): the
game's thread, joining the tick, now and then slept through the tick's
broadcast in vitasdk's pthread_cond_timedwait until its deadline - time()
+ 2, so 1-2 s, every such frame-hitch ending at the same fraction of a
second in a session ("tick_join 1919.3" at 357.286, "1183.7" at 380.286,
and in the menus and multiplayer too), the tick's own run 70-170 ms of it,
every core idle, no tick-hitch logged. The lost wake-up check did not see
it: it counted a wait lost only with no publish since, and the broadcast
missed was such a publish.

Elsewhere (Linux, Windows, Android) the waits sleep on a pthread condition
variable; each publish is made under tick_wake_lock with a broadcast, so a
wait that tests under the lock cannot miss one.

Either way a wait that ends with its work there long after the publish (a
wait that ran out of time, its publish over 50 ms before) is a lost
wake-up, counted and logged - none is expected (the harness's long runs
look for "lost wake-up" in the log). */
static pthread_mutex_t tick_wake_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tick_wake = PTHREAD_COND_INITIALIZER;
/* (under the lock) publishes made, and the lost wake-ups found */
static unsigned long tick_wake_publishes, tick_lost_wakes;
/* the last publish's time (us), for the lost wake-ups' check */
static volatile unsigned long long tick_published_us;

int vita_host_wake_create(const char *name) __attribute__((weak));
int vita_host_wake_wait(int id, unsigned long timeout_us) __attribute__((weak));
void vita_host_wake_signal(int id) __attribute__((weak));

/* the threads that sleep on a semaphore (the Vita's waits): the tick's
thread and the game's thread, each published to by the other alone. Whether
each sleeps, and its semaphore: global ints, so the atomics on them are the
processor's own (a structure's field, of an alignment the game's build
does not assume - -fmax-type-align=1 -, made __atomic_exchange_n a call to
vita_stubs.c's generic lock, not atomic with the inline operations on the
same word) */
enum { TICK_WAITER, GAME_WAITER, NUMBER_OF_TICK_WAITERS };
static volatile int tick_waiter_sleeping[NUMBER_OF_TICK_WAITERS];
static int tick_waiter_wake[NUMBER_OF_TICK_WAITERS] = { -1, -1 };
/* the semaphores made (halo_tick_thread_enabled, before the thread) */
static int tick_wake_semaphores;
#define TICK_WAKE_TIMEOUT_US 100000

static unsigned long long tick_clock_us(void)
{
	return vita_host_time_us ? vita_host_time_us() : 0;
}

static void tick_publish_begin(void)
{
	if (!tick_wake_semaphores)
		pthread_mutex_lock(&tick_wake_lock);
}

/* (after the publish's store) the other side woken: `waiter`, the thread
that waits for this publish */
static void tick_publish_end(int waiter)
{
	tick_published_us = tick_clock_us();
	if (tick_wake_semaphores)
	{
		/* (the publish before this seen before the sleeper's flag is read;
		the flag cleared by its one publisher - a sleeper that set it again
		meanwhile is woken by the signal all the same, and a signal for a
		sleeper gone is a spurious wake-up later) */
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		if (__atomic_load_n(&tick_waiter_sleeping[waiter], __ATOMIC_SEQ_CST))
		{
			__atomic_store_n(&tick_waiter_sleeping[waiter], 0, __ATOMIC_SEQ_CST);
			vita_host_wake_signal(tick_waiter_wake[waiter]);
		}
		return;
	}
	tick_wake_publishes++;
	pthread_cond_broadcast(&tick_wake);
	pthread_mutex_unlock(&tick_wake_lock);
}

/* (a wait that ended with its work there) a lost wake-up when the time ran
out with the publish long before */
static void tick_wait_lost_check(int timed_out, const char *what)
{
	unsigned long long published = tick_published_us, now = tick_clock_us();
	unsigned long lost;

	if (!timed_out || !now || now - published < 50000)
		return;
	lost = __atomic_add_fetch(&tick_lost_wakes, 1, __ATOMIC_RELAXED);
	if (lost <= 8)
		platform_log("tick thread: lost wake-up %lu (%s): the wait ran out with its work there, published %.1f ms "
			"before", lost, what, (now - published) / 1000.0);
}

/* until ready(): `spin` tests, then asleep; what names the wait in the
log's lost wake-ups; `waiter`, the calling thread's */
static void tick_wait(int (*ready)(void), unsigned long spin, const char *what, int waiter)
{
	/* (the deadline is on the realtime clock, which the condition variable
	times by; should the platform's pthreads time it otherwise - a wait
	that ends at once, or never -, the waits go untimed: a wait ended far
	too soon, twice, says so) */
	static int untimed, early;
	unsigned long spins = 0;

	while (!ready())
	{
		int said_untimed = 0, timed_out = 0;

		if (++spins < spin)
			continue;
		if (tick_wake_semaphores)
		{
			int result;

			__atomic_store_n(&tick_waiter_sleeping[waiter], 1, __ATOMIC_SEQ_CST);
			__atomic_thread_fence(__ATOMIC_SEQ_CST);
			if (ready())
			{
				__atomic_store_n(&tick_waiter_sleeping[waiter], 0, __ATOMIC_SEQ_CST);
				break;
			}
			result = vita_host_wake_wait(tick_waiter_wake[waiter], TICK_WAKE_TIMEOUT_US);
			__atomic_store_n(&tick_waiter_sleeping[waiter], 0, __ATOMIC_SEQ_CST);
			if (result < 0 && vita_host_sleep_us)
				vita_host_sleep_us(50);
			if (result == 1 && ready())
				tick_wait_lost_check(1, what);
			continue;
		}
		pthread_mutex_lock(&tick_wake_lock);
		while (!ready())
		{
			unsigned long long before = tick_clock_us();
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
			if (before && tick_clock_us() - before < 500000 && ++early >= 2)
			{
				untimed = said_untimed = 1;
				continue;
			}
			timed_out = 1;
		}
		pthread_mutex_unlock(&tick_wake_lock);
		if (said_untimed)
			platform_log("tick thread: the timed waits end too soon here: untimed from now on");
		tick_wait_lost_check(timed_out, what);
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

		tick_wait(tick_requested, 500, "the tick waiting for work", TICK_WAITER);
		delta = pending_delta;
		tick_began_us = tick_clock_us();
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
		tick_finished_us = tick_clock_us();
		tick_publish_begin();
		__atomic_store_n(&finished, finished + 1, __ATOMIC_RELEASE);
		tick_publish_end(GAME_WAITER);
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

			/* (the Vita's waits: semaphores, made before the thread; the
			condition variable without them) */
			if (vita_host_wake_create && vita_host_wake_wait && vita_host_wake_signal &&
				!(getenv("HALO_TICK_WAKE_CONDITION") && atoi(getenv("HALO_TICK_WAKE_CONDITION"))))
			{
				tick_waiter_wake[TICK_WAITER] = vita_host_wake_create("halo tick wake");
				tick_waiter_wake[GAME_WAITER] = vita_host_wake_create("halo tick join");
				tick_wake_semaphores = tick_waiter_wake[TICK_WAITER] >= 0 && tick_waiter_wake[GAME_WAITER] >= 0;
				if (!tick_wake_semaphores)
					platform_log("tick thread: no semaphores (%d, %d): its waits on a condition variable",
						tick_waiter_wake[TICK_WAITER], tick_waiter_wake[GAME_WAITER]);
				else
					platform_log("tick thread: its waits and the join's on semaphores");
			}

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
	tick_publish_end(TICK_WAITER);
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
	unsigned long long join_began = tick_clock_us();

	/* (main_joining also ends the tick's halo_tick_wait_for_render) */
	tick_publish_begin();
	__atomic_store_n(&main_joining, 1, __ATOMIC_RELEASE);
	tick_publish_end(TICK_WAITER);
	tick_wait(tick_done, 500, "the game's thread joining the tick", GAME_WAITER);
	if (join_began)
	{
		/* a join of over 150 ms (a freeze the averages hide) logged with
		where it went: the tick's update still running (its steps in the
		tick-hitch and tick-run-hitch lines, HALO_TICK_PROFILE), or done
		and the wake-up late */
		unsigned long long joined = tick_clock_us();

		if (joined - join_began > 150000)
		{
			unsigned long long began = tick_began_us, done = tick_finished_us;

			platform_log("tick-join-hitch: the game's thread waited %.1f ms for the tick: its update %.1f ms "
				"(begun %.1f ms before the join), done %.1f ms before the wait ended%s",
				(joined - join_began) / 1000.0, last_tick_us / 1000.0,
				began < join_began ? (join_began - began) / 1000.0 : 0.0,
				done < joined ? (joined - done) / 1000.0 : 0.0,
				done < joined && joined - done > 50000 ? ": a late wake-up" : "");
		}
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
		tick_wait(main_is_joining, 0, "the tick waiting for the render", TICK_WAITER);
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
