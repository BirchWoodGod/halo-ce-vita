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
/* on the tick thread: waits until the main thread has rendered and
presented the frame (it then only waits for the tick), for a change the
render cannot read halfway - a structure bsp switch; elsewhere returns at
once */
void halo_tick_wait_for_render(void);
/* the last tick's duration on the thread, microseconds */
unsigned long long halo_tick_thread_last_us(void);

#endif
