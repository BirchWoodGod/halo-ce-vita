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

void vita_host_pin_current_thread(int core) __attribute__((weak));
void vita_host_sleep_us(unsigned long microseconds) __attribute__((weak));
int halo_trace_active(void) __attribute__((weak));

static int enabled = -1;
static pthread_t thread;
static volatile unsigned long started, finished;
static volatile float pending_delta;
static volatile unsigned long long last_tick_us;
/* set while the main thread waits in halo_tick_thread_join: the frame is
rendered and presented, and nothing but the tick touches the game */
static volatile int main_joining;
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

			if (halo_trace_active && halo_trace_active())
				platform_log("trace: tick begin");
			game_time_update(delta);
			if (halo_trace_active && halo_trace_active())
				platform_log("trace: tick updated");
			/* the sound manager's frame update, with the game state it
			reads (main_game_render skips it when threaded) */
			{
				unsigned long long sound_before = vita_host_time_us ? vita_host_time_us() : 0;

				sound_render();
				if (vita_host_time_us)
					halo_tick_sound_us += vita_host_time_us() - sound_before;
				halo_tick_sound_ticks++;
			}
			last_tick_us = vita_host_time_us ? vita_host_time_us() - before : 0;
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
	__atomic_store_n(&started, started + 1, __ATOMIC_RELEASE);
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
	halo_epoch_end();
}

void halo_tick_wait_for_render(void)
{
	if (enabled <= 0 || !halo_epoch_on_mutator())
		return;
	if (halo_trace_active && halo_trace_active())
		platform_log("trace: tick waits for the render");
	while (!__atomic_load_n(&main_joining, __ATOMIC_ACQUIRE))
		pause_briefly();
}

unsigned long long halo_tick_thread_last_us(void)
{
	return last_tick_us;
}
