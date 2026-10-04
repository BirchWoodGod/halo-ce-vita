/* tick_thread.h: the game tick on its own thread (port/linux/game/tick_thread.c) */
#ifndef HALO_TICK_THREAD_H
#define HALO_TICK_THREAD_H

void game_time_update(float delta);

/* whether the tick runs on its thread (HALO_TICK_THREAD=1); starts it */
int halo_tick_thread_enabled(void);
/* runs game_time_update(delta) on the tick thread; join before touching
the game's state from the main thread */
void halo_tick_thread_start(float delta);
void halo_tick_thread_join(void);
/* on the main thread while a tick is in flight: queues a call into the
game state to be made at the join, after the tick, in the order asked
(returns 1); 0 when no tick runs, and the caller makes it now */
int halo_tick_thread_defer(void (*call)(void));
/* on the tick thread: waits until the main thread has rendered and
presented the frame (it then only waits for the tick), for a change the
render cannot read halfway - a structure bsp switch; then, on any thread,
until the GPU has drawn every frame recorded (the Vita's device) */
void halo_tick_wait_for_render(void);
/* the last tick's duration on the thread, microseconds */
unsigned long long halo_tick_thread_last_us(void);

#endif
