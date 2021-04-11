#ifndef __BENCHMARK_UTILS_H
#define __BENCHMARK_UTILS_H

#include <assert.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

struct barrier_t {
	int init;
	pthread_barrier_t barrier;
};

static inline int barrier_init(struct barrier_t *bar, int n) {
	assert(!bar->init);
	if (pthread_barrier_init(&bar->barrier, NULL, n)) {
		return -1;
	}
	bar->init = 1;
	return 0;
}

static inline void barrier_destroy(struct barrier_t *bar) {
	if (bar->init) {
		pthread_barrier_destroy(&bar->barrier);
	}
}

int bind_sock(const char *path);

static inline int setnonblock(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1) {
		perror("fcntl-getfl");
		return -1;
	}
	flags |= O_NONBLOCK;
	return fcntl(fd, F_SETFL, flags);
}

static inline uint64_t as_nanoseconds(const struct timespec* ts) {
    return ts->tv_sec * (uint64_t)1000000000L + ts->tv_nsec;
}

static inline uint64_t cur_nanoseconds(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return as_nanoseconds(&ts);
}

static int uniform_rand(int low, int high) {
    double candidate = rand()/(1.0 + RAND_MAX);
    int range = high - low + 1;
    int scaled = (candidate * range) + low;
    return scaled;
}

#endif // __BENCHMARK_UTILS_H
