#include <time.h>
#define NEED_DEBUG 1
#include "include/debug.h"
#include "include/server.h"
#include "include/utils.h"


void compute_request(unsigned long nsec, struct request_t *req, struct response_t *res) {
	struct timespec start_time;
	if (clock_gettime(CLOCK_MONOTONIC, &start_time))
		return;

	while (1) {
		SHA1(req->data, REQUEST_LENGTH, res->sha);

		struct timespec cur_time;
		if (clock_gettime(CLOCK_MONOTONIC, &cur_time))
			return;

		if ((as_nanoseconds(&cur_time) - as_nanoseconds(&start_time)) >= nsec)
			break;
	}

	res->id = req->id;
}

void *compute_worker(void *opaque, struct thread_info_t *ti) {
	struct server_context_t *ctx = opaque;
	struct queue_root *inbox = ctx->queues.compute_inbox[ti->group_info.current];
	while(!ctx->stopping) {
		struct queue_head *q = queue_get(inbox);
		if (!q)
			continue;
		struct server_buffer_t *buff = container_of(q, struct server_buffer_t, q);
		struct message_t *msg = &buff->msg;
		compute_request(ctx->cfg.load.compute_dur, &msg->req, &msg->res);
		// debug("conn %d: compute id %d ", buff->conn->fd, msg->res.id);
		queue_put(&buff->q, ctx->queues.submitter_inbox[buff->conn->submitidx]);
	}
	return NULL;
}
