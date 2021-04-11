#ifndef __BENCHMARK_DEBUG_H
#define __BENCHMARK_DEBUG_H

#ifndef NEED_DEBUG
#define NEED_DEBUG 0
#endif
#include "include/lock.h"

#if NEED_DEBUG

#define _GNU_SOURCE
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#define gettid() ((int) syscall(SYS_gettid))

extern lock_t debug_mutex;


#define debug(fmt...)									\
	do {										\
		lock(&debug_mutex);					\
		fprintf(stderr, "[%s:%d//%d] ", __func__, __LINE__, gettid());	\
		fprintf(stderr, fmt);							\
		fprintf(stderr, "\n");							\
		unlock(&debug_mutex);					\
	} while (0)
#else // NEED_DEBUG
#define debug(fmt...) ((void) 0)
#endif // NEED_DEBUG
#endif
