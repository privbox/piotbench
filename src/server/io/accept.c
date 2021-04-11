#include <sys/epoll.h>
#include <errno.h>
#include <unistd.h>
#define NEED_DEBUG 1
#include "include/debug.h"
#include "include/kerncall.h"
#include "include/server.h"

#include "io.h"

static int handle_accept(struct server_context_t *ctx) {
	struct queue_head *q = queue_get(ctx->queues.empty_connections);
	if (!q)
		return 0;

	int fd = Z_accept4(ctx->io.listen_fd, NULL, NULL, SOCK_NONBLOCK);;
	if (fd < 0) {
		Z_perror("accept");
		return -1;
	}

	struct server_connection_t *conn = container_of(q, struct server_connection_t, q);
	conn->sent = conn->received = conn->processed = 0;

	conn->fd = fd;
	conn->ioidx = ctx->io.next++ % ctx->io.nr_io;
	conn->submitidx = ctx->io.next % ctx->cfg.threads.submit;
	conn->computeidx = ctx->io.next % ctx->cfg.threads.compute;
	conn->epoll_fd = ctx->io.epoll_fds[conn->ioidx];
	init_queue_root(&conn->send_queue);
	conn->recvbuf = NULL;
	conn->epoll_state = 0;
	conn->closed = 0;
	lock_init(&conn->lock);
	debug("created session %d on io %d compute %d submit %d (%p)" , conn->fd, conn->ioidx, conn->computeidx, conn->submitidx, conn);
	return epoll_set_conn_state(ctx, conn, EPOLLIN);
}

struct accept_arg_t {
	int epollfd;
	struct server_context_t *ctx;
	struct thread_info_t *ti;
};

static long __accept_worker(struct accept_arg_t *arg) {
	struct server_context_t *ctx = arg->ctx;
	int epollfd = arg->epollfd;
	int *stopping = &ctx->stopping;
	struct epoll_event evt;

	unsigned int iter = 0;

	while (!*stopping && iter++ < 1000) {
		int nevents = Z_epoll_wait(epollfd, &evt, 1, 1000);
		if (nevents == -1 && errno != EINTR) {
			Z_perror("epoll_wait");
			return -1;
		} else if (nevents == 1 && evt.events & EPOLLIN) {
			if (handle_accept(ctx)) {
				Z_perror("handle_accept");
				return -1;
			}
		} else if (nevents == 0) {
			return 0;
		}
	}
	return 0;
}

void *accept_worker(void *opaque, struct thread_info_t *ti) {
	struct server_context_t *ctx = opaque;
	int listenfd = ctx->io.listen_fd;
	long ret = 0;

	int epollfd = Z_epoll_create1(0);
	if (epollfd < 0) {
		Z_perror("epoll_create1");
		ret = -1;
		goto out;
	}

	struct epoll_event evt = {
		.data.fd = listenfd,
		.events = EPOLLIN,
	};
	ret = Z_epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &evt);
	if (ret) {
		Z_perror("epoll_ctl");
		goto out_close;
	}

	struct accept_arg_t arg = {
		.ctx = ctx,
		.ti = ti,
		.epollfd = epollfd,
	};

	while (!ctx->stopping && !ret) {
		if (KERNCALL_COND(ctx->cfg, accept))
			ret = kerncall_spawn(
				(uintptr_t) __accept_worker,
				(unsigned long) &arg
			);
		else
			ret = __accept_worker(&arg);
	}

out_close:
	close(epollfd);
out:
	return (void *) ret;
}