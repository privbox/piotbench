#include <sys/epoll.h>
#include <errno.h>
#include <unistd.h>

#define NEED_DEBUG 1
#include "include/debug.h"
#include "include/kerncall.h"
#include "include/server.h"

#include "io.h"

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _b : _a; })

static int finish_connection(
	struct server_context_t *ctx,
	struct server_connection_t *conn
) {
	if (!trylock(&conn->lock)) {
		debug("conn %d: termination delayed, submitter has lock", conn->fd);
		return 0;
	}

	if (conn->sendbuf)
		queue_put(&conn->sendbuf->q, ctx->queues.empty_buffers);
	if (conn->recvbuf)
		queue_put(&conn->recvbuf->q, ctx->queues.empty_buffers);

	// remove from epoll
	Z_close(conn->fd);

	// Drain send queue;
	struct queue_head *q;
	do {
		q = queue_get(&conn->send_queue);
		if (q) {
			conn->sent++;
			queue_put(q, ctx->queues.empty_buffers);
		}
	} while (q);
	conn->closed = 1;

	// Check if there are in flight messages
	if (conn->sent == conn->received) {
		debug("conn %d: finish(released) stats: received %lu, sent %lu", conn->fd, conn->received, conn->sent);
		queue_put(&conn->q, ctx->queues.empty_connections);
		return 0;
	} else {
		// Otherwise make sure cleanup is in submitter
		debug("conn %d: finish(delegated) stats: received %lu, sent %lu", conn->fd, conn->received, conn->sent);
		unlock(&conn->lock);
		return 0;
	}
}

static int handle_request(
	struct server_context_t *ctx,
	struct server_connection_t *conn
) {
	while (1) {
		struct server_buffer_t *buff = conn->recvbuf;

		if (!buff) {
			if (conn->received - conn->sent > ctx->cfg.load.readahead)
				return 0;
			struct queue_head *q = queue_get(ctx->queues.empty_buffers);
			if (!q) {
				debug("conn %d: no buffer available, skipping", conn->fd);
				return 0;
			}
			buff = container_of(q, struct server_buffer_t, q);
			buff->conn = conn;
			buff->left = sizeof (struct request_t);
			buff->ptr = (unsigned char *) &buff->msg.req.id;
			conn->recvbuf = buff;
		}
		size_t io_size = min(buff->left, ctx->cfg.load.max_io_size);
		int len = Z_recv(conn->fd, buff->ptr, io_size, MSG_DONTWAIT);
		if (len == -1 && errno == EAGAIN)
			return 0;
		else if (len <= 0)
			return finish_connection(ctx, conn);
		// debug("len = %d", len);
		buff->left -= len;
		buff->ptr += len;
		if (!buff->left) {
			// debug("conn %d: received message %d", conn->fd, buff->msg.req.id);
			lock(&conn->lock);
			conn->received++;
			conn->recvbuf = NULL;
			queue_put(&buff->q, ctx->queues.compute_inbox[conn->computeidx]);
			unlock(&conn->lock);
		}
	}
}

static int handle_response(
	struct server_context_t *ctx,
	struct server_connection_t *conn
) {
	while (1) {
		struct server_buffer_t *buff = conn->sendbuf;
		// dump_conn(conn);
		if (!buff) {
			struct queue_head *q = queue_get(&conn->send_queue);
			if (!q) {
				// debug("No response ready, skipping");
				return 0;
			}
			buff = container_of(q, struct server_buffer_t, q);
			buff->conn = conn;
			buff->left = sizeof (struct response_t);
			buff->ptr = (unsigned char *) &buff->msg.res.id;
			conn->sendbuf = buff;
		}
		// dump_conn(conn);
		size_t io_size = min(buff->left, ctx->cfg.load.max_io_size);
		int len = Z_send(conn->fd, buff->ptr, io_size, MSG_DONTWAIT);
		if (len == -1) {
			if (errno == EAGAIN)
				return 0;
			else
				return finish_connection(ctx, conn);
		}
		// debug("len = %d", len);

		buff->left -= len;
		buff->ptr += len;
		if (len < io_size)
			return 0;
		if (buff->left)
			continue;

		// debug("conn %d: sent message %d", conn->fd, buff->msg.req.id);
		conn->sent++;
		conn->sendbuf = NULL;
		queue_put(&buff->q, ctx->queues.empty_buffers);

		lock(&conn->lock);
		int err = 0;
		if (conn->processed == conn->sent)
			err = epoll_set_conn_state(ctx, conn, conn->epoll_state & ~EPOLLOUT);
		unlock(&conn->lock);

		if (err) {
			debug("err = %d", err);
			return err;
		}
	}
}

#define MAX_EVENTS	10

struct io_arg_t {
	struct server_context_t *ctx;
	struct thread_info_t *ti;
};

static long __io_worker(struct io_arg_t *arg) {
	struct server_context_t *ctx = arg->ctx;
	struct thread_info_t *ti = arg->ti;
	int *stopping = &ctx->stopping;
	int epollfd = ctx->io.epoll_fds[ti->group_info.current];
	unsigned int iter = 0;

	while (!*stopping && iter++ < 1000) {
		struct epoll_event events[MAX_EVENTS];
		int nevents = Z_epoll_wait(epollfd, events, MAX_EVENTS, 1000);
		if (nevents == -1) {
			if (errno  == EINTR) {
				debug("epoll intr");
				if (*stopping) {
					return 0;
				} else {
					continue;
				}
			} else {
				Z_perror("epoll_wait");
				return -1;
			}
		} else if (nevents == 0) {
			return 0;
		}

		for (unsigned int i = 0; i < nevents; i++) {
			struct server_connection_t *conn = events[i].data.ptr;
			if (!conn) {
				debug("bad conn");
				return -1;
			}
			if (events[i].events & EPOLLERR) {
				debug("conn %d: err event", conn->fd);
				finish_connection(ctx, conn);
			} else {
				// Send responses first to avoid read-starvation
				if (events[i].events & EPOLLOUT) {
					// debug("conn %d: output event", conn->fd);
					if (handle_response(ctx, conn)) {
						Z_perror("handle_response");
						continue; // NOOP
					}
				}
				if (events[i].events & EPOLLIN) {
					// debug("conn %d: input event", conn->fd);
					if (handle_request(ctx, conn)) {
						Z_perror("handle_request");
						continue;
					}
				}
			}
		}
	}
	return 0;
}

void *io_worker(void *opaque, struct thread_info_t *ti) {
	struct server_context_t *ctx = opaque;
	struct io_arg_t arg = {
		.ctx = ctx,
		.ti = ti,
	};
	long ret = 0;
	while (!ctx->stopping && !ret) {
		if (KERNCALL_COND(ctx->cfg, io)) {
			ret = kerncall_spawn(
				(uintptr_t) __io_worker,
				(unsigned long) &arg
			);
			asm(".align 32");
		}
		else
			ret = __io_worker(&arg);
	}
	return (void *) ret;
}