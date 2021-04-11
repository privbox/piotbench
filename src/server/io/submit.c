#define NEED_DEBUG 0
#include "include/debug.h"
#include "include/kerncall.h"
#include "include/server.h"

#include "io.h"

struct submit_arg_t {
	struct server_context_t *ctx;
	struct thread_info_t *ti;
};

static long __submitter_worker(struct submit_arg_t *arg) {
	struct server_context_t *ctx = arg->ctx;
	struct thread_info_t *ti = arg->ti;
	int *stopping = &ctx->stopping;
	long err = 0;
	unsigned long iter = 0;

	struct queue_root *inbox = ctx->queues.submitter_inbox[ti->group_info.current];

	while (!*stopping && iter++ < 10000000l && !err) {
		struct queue_head *q = queue_get(inbox);
		if (!q)
			continue;
		struct server_buffer_t *buff = container_of(q, struct server_buffer_t, q);
		struct server_connection_t *conn = buff->conn;
		if (!trylock(&conn->lock)) {
			queue_put(q, inbox);
			debug("conn %d: recycle response %d", conn->fd, buff->msg.res.id);
			continue;
		}

		conn->processed++;
		if (conn->closed) {
			debug("conn %d: submitter dispose id %d", conn->fd, buff->msg.req.id);
			queue_put(q, ctx->queues.empty_buffers);
			unlock(&conn->lock);

			if (conn->processed == conn->received) {
				// Last in-flight buffer
				debug("conn %d: submitter cleanup", conn->fd);
				queue_put(&conn->q, ctx->queues.empty_connections);
				continue;
			}
		} else {
			debug("conn %d: submit response %d", conn->fd, buff->msg.res.id);
			queue_put(&buff->q, &conn->send_queue);
			if (epoll_set_conn_state(ctx, conn, conn->epoll_state | EPOLLOUT)) {
				Z_perror("epoll_set_conn_state");
				err = -1;
			}
			unlock(&conn->lock);
		}
	}
	debug("submit done");
	return err;
}

void *submitter_worker(void *opaque, struct thread_info_t *ti) {
	struct server_context_t *ctx = opaque;
	struct submit_arg_t arg = {
		.ctx = ctx,
		.ti = ti,
	};
	long ret = 0;

	while (!ctx->stopping && !ret) {
		if (KERNCALL_COND(ctx->cfg, submit))
			ret = kerncall_spawn(
				(uintptr_t) __submitter_worker,
				(unsigned long) &arg
			);
		else
			ret = __submitter_worker(&arg);
	}
	return (void *) ret;
}