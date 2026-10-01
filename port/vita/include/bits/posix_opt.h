/* VitaSDK's bits/posix_opt.h without its #include <limits.h>: newlib's stdio would
   otherwise define LONG_MAX and the rest before the game's cseries.h declares them */
#ifndef _POSIX_OPT_H
#define _POSIX_OPT_H 1


#define _POSIX_THREADS                          200112L
#define _POSIX_READER_WRITER_LOCKS              200112L
#define _POSIX_SPIN_LOCKS                       200112L
#define _POSIX_BARRIERS                         200112L
#define _POSIX_THREAD_SAFE_FUNCTIONS            200112L
#define _POSIX_THREAD_ATTR_STACKSIZE            200112L
#define _POSIX_THREAD_ATTR_STACKADDR            -1
#define _POSIX_THREAD_PRIO_INHERIT              -1
#define _POSIX_THREAD_PRIO_PROTECT              -1
#define _POSIX_THREAD_PROCESS_SHARED            -1
#define _POSIX_PRIORITY_SCHEDULING              1
#define _POSIX_TIMEOUTS                         1
#define _UNIX98_THREAD_MUTEX_ATTRIBUTES         1
#define _POSIX_MONOTONIC_CLOCK                  200112L
#define _POSIX_TIMERS                           1


#define _POSIX_THREAD_DESTRUCTOR_ITERATIONS     4
#define PTHREAD_DESTRUCTOR_ITERATIONS           _POSIX_THREAD_DESTRUCTOR_ITERATIONS

#define _POSIX_THREAD_KEYS_MAX                  128
#define PTHREAD_KEYS_MAX                        _POSIX_THREAD_KEYS_MAX

#define PTHREAD_STACK_MIN                       32*1024
#define _POSIX_THREAD_THREADS_MAX               64

#define PTHREAD_THREADS_MAX                     _POSIX_THREAD_THREADS_MAX

#define _POSIX_SEM_NSEMS_MAX                    256

#define SEM_NSEMS_MAX                           _POSIX_SEM_NSEMS_MAX

#define _POSIX_SEM_VALUE_MAX                    32767

#define SEM_VALUE_MAX                           _POSIX_SEM_VALUE_MAX

#define INCLUDE_NP                              1

#endif /* posix_opt.h */
