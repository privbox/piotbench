#ifndef __BENCHMARK_LOCK_H
#define __BENCHMARK_LOCK_H


#if 1
typedef unsigned int lock_t;
#define MUTEX_INIT 0

static inline void lock(lock_t *lock) {
	while (1) {
		int i;
		for (i=0; i < 10000; i++) {
			__asm__ __volatile__ ("" ::: "memory");
			if (*lock == 0) {
				if (__sync_bool_compare_and_swap(lock, 0, 1)) {
					return;
				}
			}
		}
		// sched_yield();
	}
}
static inline void unlock(lock_t *lock) {
	__asm__ __volatile__ ("" ::: "memory");
	*lock = 0;
}
static inline void lock_init(lock_t *lock) {
	unlock(lock);
}

static inline int trylock(lock_t *lock) {
	__asm__ __volatile__ ("" ::: "memory");
	if (*lock == 0) {
		if (__sync_bool_compare_and_swap(lock, 0, 1)) {
			return 1;
		}
	}
	return 0;
}
#else

#include <pthread.h>

typedef pthread_mutex_t lock_t;
#define MUTEX_INIT PTHREAD_MUTEX_INITIALIZER

static inline void lock(lock_t *lock) {
	pthread_mutex_lock(lock);
}
static inline void unlock(lock_t *lock) {
	pthread_mutex_unlock(lock);
}
static inline void lock_init(lock_t *lock) {
	pthread_mutex_init(lock, NULL);
}

static inline int trylock(lock_t *lock) {
	return pthread_mutex_trylock(lock) == 0;
}
#endif


#endif // __BENCHMARK_LOCK_H
