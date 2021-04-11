#ifndef __BENCHMARK_THREAD_H
#define __BENCHMARK_THREAD_H

#include <pthread.h>

#include "queue.h"

#define THREAD_NAME_MAX 64
#define THREAD_GROUP_MAX 64


struct thread_group_info_t {
	unsigned int total;
	unsigned int current;
};

struct thread_info_t {
	pthread_t thread;
	char name[THREAD_NAME_MAX];
	void *(*func)(void *, struct thread_info_t *);
	void *arg;
	void *ret;
	int wakefd;
	int returned;
	struct thread_group_info_t group_info;
};

struct thread_info_t *create_thread(
	const char *name,
	void *(*start_routine)(void *, struct thread_info_t *),
	void *arg
);

struct thread_group_t *thread_group_create(
	const char *name_prefix,
	size_t n,
	void *(*start_routine)(void *, struct thread_info_t *),
	void *arg
);

struct thread_group_t {
	size_t n;
	struct thread_info_t *threads[THREAD_GROUP_MAX];
};

void *thread_join(struct thread_info_t *ti);
void thread_group_join(struct thread_group_t *tg, void *ret[]);

#endif // __BENCHMARK_THREAD_H
