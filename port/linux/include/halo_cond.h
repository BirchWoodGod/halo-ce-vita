/* halo_cond.h

(port) A condition variable the Vita's threads can trust: on the Vita (and
the gxm-null harness, whose vita_host_wake_* are POSIX semaphores) a
waiter counted in `waiters` sleeps on a kernel semaphore, timed (at most
HALO_COND_SLICE_US a sleep, so a wake-up ever lost costs no more); elsewhere
(Linux, Windows, Android) a pthread condition variable.

Why (Oct 25 2026, the campaign's 1-2 s freezes): vitasdk's
pthread_cond_timedwait now and then slept through a broadcast until its
deadline (tick_thread.c), and pthread_cond_wait, a cancellation point,
crashes a thread of sceKernelCreateThread's that has no pthread data
(vita_settings.c save_wake). The mutex stays a pthread one: not a
cancellation point, safe on any thread.

The rules are a condition variable's: the waiter holds the mutex, tests its
predicate in a loop, and may wake spuriously (a slice that ran out, or a
signal meant for a waiter gone); a broadcast or signal follows the change
it announces, made under the mutex. A waiter counts itself while it holds
the mutex, before letting it go, so a broadcast after the change either
finds it counted or the waiter's test saw the change.

The semaphore is made at the first wait or signal (the static initializer
has none); halo_cond_destroy deletes it. The counts and the semaphore's id
are of explicitly aligned types: under the Vita build's -fmax-type-align=1
a plain field reached through a pointer took vita_stubs.c's generic
atomics. */

#ifndef HALO_COND_H
#define HALO_COND_H

#include <pthread.h>

typedef volatile int halo_cond_int __attribute__((aligned(4)));

typedef struct __attribute__((aligned(8))) halo_cond
{
	pthread_cond_t condition;
	halo_cond_int waiters;
	/* the semaphore: -1 none yet */
	halo_cond_int wake;
} halo_cond_t;

#define HALO_COND_INITIALIZER { PTHREAD_COND_INITIALIZER, 0, -1 }
#define HALO_COND_SLICE_US 100000UL

int vita_host_wake_create(const char *name) __attribute__((weak));
int vita_host_wake_wait(int id, unsigned long timeout_us) __attribute__((weak));
void vita_host_wake_signal(int id) __attribute__((weak));
void vita_host_wake_delete(int id) __attribute__((weak));

/* whether the semaphores serve (the Vita's, the harness's) */
static inline int halo_cond_semaphores(void)
{
	return vita_host_wake_create && vita_host_wake_wait && vita_host_wake_signal && vita_host_wake_delete;
}

/* the semaphore, made once (two threads racing: one keeps its own) */
static inline int halo_cond_wake(halo_cond_t *cond)
{
	int wake = __atomic_load_n(&cond->wake, __ATOMIC_ACQUIRE), made, expected = -1;

	if (wake >= 0)
		return wake;
	made = vita_host_wake_create("halo cond");
	if (made < 0)
		return -1;
	if (__atomic_compare_exchange_n(&cond->wake, &expected, made, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return made;
	vita_host_wake_delete(made);
	return expected;
}

static inline void halo_cond_init(halo_cond_t *cond)
{
	pthread_cond_init(&cond->condition, NULL);
	cond->waiters = 0;
	cond->wake = -1;
}

static inline void halo_cond_destroy(halo_cond_t *cond)
{
	if (halo_cond_semaphores() && cond->wake >= 0)
		vita_host_wake_delete(cond->wake);
	cond->wake = -1;
	pthread_cond_destroy(&cond->condition);
}

/* (the semaphores) one sleep of at most timeout_us (0: a slice), the mutex
let go meanwhile; 1 when the time ran out */
static inline int halo_cond_sleep(halo_cond_t *cond, pthread_mutex_t *mutex, unsigned long timeout_us)
{
	int wake = halo_cond_wake(cond), result;

	if (!timeout_us || timeout_us > HALO_COND_SLICE_US)
		timeout_us = HALO_COND_SLICE_US;
	__atomic_add_fetch(&cond->waiters, 1, __ATOMIC_SEQ_CST);
	pthread_mutex_unlock(mutex);
	result = wake >= 0 ? vita_host_wake_wait(wake, timeout_us) : 1;
	if (result != 0)
	{
		/* (no signal taken: uncounted, unless a broadcast has uncounted
		it already - its signal then a spurious wake-up later) */
		int waiters = __atomic_load_n(&cond->waiters, __ATOMIC_SEQ_CST);

		while (waiters > 0 &&
			!__atomic_compare_exchange_n(&cond->waiters, &waiters, waiters - 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
			;
	}
	pthread_mutex_lock(mutex);
	return result == 1;
}

static inline void halo_cond_wait(halo_cond_t *cond, pthread_mutex_t *mutex)
{
	if (!halo_cond_semaphores())
	{
		pthread_cond_wait(&cond->condition, mutex);
		return;
	}
	halo_cond_sleep(cond, mutex, 0);
}

static inline void halo_cond_broadcast(halo_cond_t *cond)
{
	int waiters;

	if (!halo_cond_semaphores())
	{
		pthread_cond_broadcast(&cond->condition);
		return;
	}
	if (!__atomic_load_n(&cond->waiters, __ATOMIC_SEQ_CST))
		return;
	waiters = __atomic_exchange_n(&cond->waiters, 0, __ATOMIC_SEQ_CST);
	if (waiters > 0)
	{
		int wake = halo_cond_wake(cond);

		while (wake >= 0 && waiters-- > 0)
			vita_host_wake_signal(wake);
	}
}

static inline void halo_cond_signal(halo_cond_t *cond)
{
	int waiters;

	if (!halo_cond_semaphores())
	{
		pthread_cond_signal(&cond->condition);
		return;
	}
	waiters = __atomic_load_n(&cond->waiters, __ATOMIC_SEQ_CST);
	while (waiters > 0)
	{
		if (__atomic_compare_exchange_n(&cond->waiters, &waiters, waiters - 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		{
			int wake = halo_cond_wake(cond);

			if (wake >= 0)
				vita_host_wake_signal(wake);
			return;
		}
	}
}

#ifdef HALO_COND_TIMED
#include <errno.h>
#include <time.h>

/* pthread_cond_timedwait's: until the realtime deadline; ETIMEDOUT when it
has passed */
static inline int halo_cond_timedwait(halo_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *deadline)
{
	struct timespec now;
	long long left_us;

	if (!halo_cond_semaphores())
		return pthread_cond_timedwait(&cond->condition, mutex, deadline);
	clock_gettime(CLOCK_REALTIME, &now);
	left_us = (long long)(deadline->tv_sec - now.tv_sec) * 1000000LL + (deadline->tv_nsec - now.tv_nsec) / 1000L;
	if (left_us <= 0)
		return ETIMEDOUT;
	/* (a slice that ran out short of the deadline: a spurious wake-up, the
	caller tests and waits again) */
	if (halo_cond_sleep(cond, mutex, left_us < 1 ? 1 : (unsigned long)left_us) && left_us <= (long long)HALO_COND_SLICE_US)
		return ETIMEDOUT;
	return 0;
}
#endif

#endif
