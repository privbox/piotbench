#define _GNU_SOURCE
#include <sys/eventfd.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#define NEED_DEBUG 1
#include "include/debug.h"
#include "include/thread.h"

static void *privsancall(void *(*func)(void *, struct thread_info_t *), void *arg, struct thread_info_t *ti) {
	unsigned long ret, fn = (uintptr_t) func, a1 = (uintptr_t) arg, a2 = (uintptr_t) ti;

	__asm__ __volatile__ (
		"call *%3\n"
		".p2align 4"
		: "=a"(ret) : "D"(a1), "S"(a2), "r"(fn)
		: "rdx", "rcx", "r8", "r9", "memory"
	);

	return (void *) ret;
}

static void *thread_wrapper(void *opaque) {
	struct thread_info_t *ti = opaque;
	pthread_setname_np(ti->thread, ti->name);
	debug("%s started", ti->name);
	ti->ret = privsancall(ti->func, ti->arg, ti);
	ti->returned = 1;
	debug("%s finished with 0x%llx", ti->name, (unsigned long long) ti->ret);
	return NULL;
}

static int spawn_thread(struct thread_info_t *ti) {
	return pthread_create(&ti->thread, NULL, thread_wrapper, ti);
}

static struct thread_group_info_t DEFAULT_GROUP_INFO = {
	.total = 1,
	.current = 1,
};

static struct thread_info_t *__create_thread(
	const char *name,
	void * (*start_routine)(void *, struct thread_info_t *),
	void *arg,
	struct thread_group_info_t *group_info
) {
	struct thread_info_t *ti = calloc(1, sizeof (struct thread_info_t));
	if (!ti) {
		perror("calloc");
		goto out;
	}
	ti->func = start_routine;
	ti->arg = arg;
	ti->returned = 0;
	strncpy(ti->name, name, THREAD_NAME_MAX);
	ti->wakefd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (ti->wakefd < 0) {
		perror("eventfd");
		goto out_free;
	}

	ti->group_info = *group_info;

	if (!spawn_thread(ti))
		return ti;

out_close:
	close(ti->wakefd);
out_free:
	free(ti);
out:
	return NULL;
}

struct thread_info_t *create_thread(
	const char *name,
	void * (*start_routine)(void *, struct thread_info_t *),
	void *arg
) {
	return __create_thread(name, start_routine, arg, &DEFAULT_GROUP_INFO);
}

void *thread_join(struct thread_info_t *ti) {
	int val = 1;
	void *ret;
	write(ti->wakefd, &val, sizeof (val));
	pthread_join(ti->thread, &ret);
	close(ti->wakefd);
	free(ti);
	debug("thread %s joined", ti->name);
	return ti->ret;
}

struct thread_group_t *thread_group_create(
	const char *name_prefix,
	size_t n,
	void *(*start_routine)(void *, struct thread_info_t *),
	void *arg
) {
	char name[THREAD_NAME_MAX];
	struct thread_group_t *tg = calloc(1, sizeof(struct thread_group_t));
	if (!tg) {
		perror("calloc");
		return NULL;
	}
	tg->n = n;

	struct thread_group_info_t group_info = {
		.total = n,
	};
	for (unsigned int i = 0; i < n; i++) {
		snprintf(name, THREAD_NAME_MAX, "%s:%d", name_prefix, i);
		group_info.current = i;
		tg->threads[i] = __create_thread(name, start_routine, arg, &group_info);
		if (!tg->threads[i]) {
			perror("create_thread//FIXME");
			// FIXME
			free(tg);
			return NULL;
		}
	}
	return tg;
}


void thread_group_join(struct thread_group_t *tg, void *ret[]) {
	for (unsigned int i = 0; i < tg->n; i++) {
		if (!tg->threads[i])
			continue;

		void *r = thread_join(tg->threads[i]);
		if (ret)
			ret[i] = r;
	}
}
