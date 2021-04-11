#include <unistd.h>

#define NEED_DEBUG 1
#include "include/client.h"
#include "include/command.h"
#include "include/debug.h"


#define CLIENT_PARAM_UINT(field_name, desc, default_) \
	PARAM_UINT(struct client_config_t, field_name, desc, default_)
#define CLIENT_PARAM_INT(field_name, desc, default_) \
	PARAM_INT(struct client_config_t, field_name, desc, default_)
#define CLIENT_PARAM_STR(field_name, desc, default_) \
	PARAM_STR(struct client_config_t, field_name, desc, default_)

struct param_t client_params[] = {
	CLIENT_PARAM_STR(
		server_path,
		"Server socket path",
		"/tmp/bench-server"
	),
	CLIENT_PARAM_UINT(
		nr_threads,
		"Number of threads to spawn",
		1
	),
	CLIENT_PARAM_UINT(
		nr_connections,
		"Number of connections per thread",
		20
	),
	CLIENT_PARAM_UINT(
		nr_requests,
		"Number of requests per connection",
		100000
	),
	CLIENT_PARAM_INT(
		duration,
		"Total test duration (-1 for no limit)",
		-1
	),
	LAST_PARAM,
};

struct client_progress_t {
	unsigned long threads_active;
	unsigned long conns_active;
	unsigned long sent_total;
	unsigned long received_total;
	struct timespec timestamp;
};

static int init_client_conn(
	struct client_context_t *ctx,
	unsigned int thread_idx,
	unsigned int conn_idx
) {
	struct client_thread_t *cthread = &ctx->client_threads[thread_idx];
	struct client_connection_t *conn = &cthread->conns[conn_idx];

	conn->fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (conn->fd < 0) {
		perror("socket");
		goto out;
	}

	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};
	strncpy(addr.sun_path, ctx->cfg.server_path, 108);
	if (connect(conn->fd, (const struct sockaddr *) &addr, sizeof (struct sockaddr_un))) {
		perror("connect");
		goto out_close;
	}
	if (setnonblock(conn->fd)) {
		perror("setnonblock");
		goto out_close;
	}
	conn->state = CONN_OPEN;
	conn->status.total = ctx->cfg.nr_requests;
	return 0;

out_close:
	close(conn->fd);
out:
	return -1;
}

static void cleanup_client_conn(struct client_connection_t *conn) {
	if (conn->fd > 0)
		close(conn->fd);
	conn->state = 0;
}

static void cleanup_client_thread(struct client_thread_t *cthread) {
	for (unsigned int i = 0; i < cthread->ctx->cfg.nr_connections; i++) {
		if (cthread->conns[i].state == CONN_OPEN) {
			cleanup_client_conn(&cthread->conns[i]);
		}
	}
	free(cthread->conns);
	cthread->init = 0;
}

static int init_client_thread(struct client_context_t *ctx, unsigned int idx) {
	struct client_thread_t *cthread = &ctx->client_threads[idx];
	cthread->ctx = ctx;
	cthread->conns = calloc(ctx->cfg.nr_connections, sizeof (struct client_connection_t));
	if (!cthread->conns){
		perror("calloc");
		goto out_err;
	}

	for (unsigned int i = 0; i < ctx->cfg.nr_connections; i++) {
		if (init_client_conn(ctx, idx, i)) {
			perror("init_client_conn");
			goto out_cleanup;
		}
	}

	cthread->init = 1;
	return 0;

out_cleanup:
	cleanup_client_thread(cthread);
out_err:
	return -1;
}

void cleanup_client(struct client_context_t *ctx) {
	barrier_destroy(&ctx->start_barrier);
	for (unsigned int i = 0; i < ctx->cfg.nr_threads; i++) {
		if (ctx->client_threads[i].init) {
			cleanup_client_thread(&ctx->client_threads[i]);
		}
	}
	free(ctx);
}

static struct client_context_t *init_client(struct client_config_t *cfg) {
	struct client_context_t *ctx = calloc(1, sizeof (struct client_context_t));
	if (!ctx) {
		perror("calloc");
		goto out_err;
	}
	ctx->cfg = *cfg;
	for (unsigned int i = 0; i < cfg->nr_threads; i++) {
		if (init_client_thread(ctx, i)) {
			perror("init_client_thread");
			goto out_cleanup;
		}
	}

	if (barrier_init(&ctx->start_barrier, ctx->cfg.nr_threads + 1)) {
		perror("barrier_init");
		goto out_cleanup;
	}

	if (ctx->cfg.duration == -1) {
		ctx->deadline = UINT64_MAX;
	} else {
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		ts.tv_sec += ctx->cfg.duration;
		ctx->deadline = as_nanoseconds(&ts);
	}
	debug("deadline is %lu", ctx->deadline);
	return ctx;
out_cleanup:
	cleanup_client(ctx);
out_err:
	return NULL;
}

static int start_client_threads(struct client_context_t *ctx) {
	for (unsigned int i = 0; i < ctx->cfg.nr_threads; i++) {
		char name[THREAD_NAME_MAX];
		snprintf(name, THREAD_NAME_MAX, "client:%d", i);
		struct thread_info_t *ti = create_thread(name, client_worker, &ctx->client_threads[i]);
		if (!ti) {
			return -1;
		}
		ctx->client_threads[i].ti = ti;
	}
	return 0;
}

static int join_client_threads(struct client_context_t *ctx) {
	int err = 0;
	for (unsigned int i = 0; i < ctx->cfg.nr_threads; i++) {
		struct thread_info_t *ti = ctx->client_threads[i].ti;
		if (ti)
			if (thread_join(ti) != NULL)
				err |= 1;
	}
	return err;
}

static int report_client_stats(struct client_context_t *ctx) {
	uint64_t start_time = -1ULL;
	uint64_t end_time = 0;
	uint64_t total_requests = 0;

	for (unsigned int thr = 0; thr < ctx->cfg.nr_threads; thr++) {
		for (unsigned int conn = 0; conn < ctx->cfg.nr_connections; conn++) {
			struct client_status_t *status = &ctx->client_threads[thr].conns[conn].status;
			uint64_t conn_start_time = as_nanoseconds(&status->start_time);
			uint64_t conn_end_time = as_nanoseconds(&status->end_time);
			uint64_t duration_ns = conn_end_time - conn_start_time;
			// debug("conn:%d:%d send %lld, start %lld, end %lld, dur %lld", thr, conn, status->sent, conn_start_time, conn_end_time, duration_ns);
			debug("conn:%d:%d avg req time %.1lf", thr, conn, (double) duration_ns / status->sent);

			if (start_time > conn_start_time)
				start_time = conn_start_time;
			if (end_time < conn_end_time)
				end_time = conn_end_time;

			total_requests += status->sent;
		}
	}

	debug("Total requests: %ld", total_requests);

	uint64_t duration_ns = end_time - start_time;
	double duration_sec = duration_ns / (1000000000L);
	debug("Duration in sec: %.6lf", (double) duration_ns / 1000000000L);

	uint64_t avg_dur_ns = duration_ns / total_requests;
	double avg_dur_us = ((double) avg_dur_ns) / 1000;
	double rpus = 1.0 / avg_dur_us;
	debug("Requests/usec: %lf", rpus);

	double rps = rpus * 1000000;
	debug("Requests/sec: %lf", rps);
	return 0;
}

struct client_context_t *prepare_client(struct client_config_t *cfg) {
	struct client_context_t *ctx = init_client(cfg);
	if (!ctx) {
		perror("init_client");
		return NULL;
	}
	return ctx;
}

int start_client(struct client_context_t *ctx) {
	if (start_client_threads(ctx)) {
		perror("start_client_threads");
		return -1;
	}
	pthread_barrier_wait(&ctx->start_barrier.barrier);
	debug("All threads started");
	return 0;
}

int join_client(struct client_context_t *ctx) {
	int err = join_client_threads(ctx);
	if (!err) {
		debug("all threads finished successfully");
		return 0;
	} else {
		debug("some threads failed");
		return err;
	}
}

static int client_nr_threads_done(struct client_context_t *ctx) {
	int nr_done = 0;
	for (int i = 0; i < ctx->cfg.nr_threads; i++) {
		nr_done += ctx->client_threads[i].ti->returned;
	}
	return nr_done;
}

static int client_threads_done(struct client_context_t *ctx) {
	return client_nr_threads_done(ctx) == ctx->cfg.nr_threads;
}

static int report_progress(struct client_context_t *ctx, struct client_progress_t *progress) {
	unsigned long total = ctx->cfg.nr_threads * ctx->cfg.nr_connections * ctx->cfg.nr_requests;
	struct client_progress_t new_progress = { 0 };
	clock_gettime(CLOCK_MONOTONIC, &new_progress.timestamp);

	for (int tidx = 0; tidx < ctx->cfg.nr_threads; tidx++) {
		for (int cidx = 0; cidx < ctx->cfg.nr_connections; cidx++) {
			if (ctx->client_threads[tidx].conns[cidx].status.received < ctx->cfg.nr_requests)
				new_progress.conns_active++;
			new_progress.sent_total += ctx->client_threads[tidx].conns[cidx].status.sent;
			new_progress.received_total += ctx->client_threads[tidx].conns[cidx].status.received;
		}
	}
	new_progress.threads_active = ctx->cfg.nr_threads - client_nr_threads_done(ctx);

	unsigned int time_delta = as_nanoseconds(&new_progress.timestamp) - as_nanoseconds(&progress->timestamp);
	unsigned int sent_delta = new_progress.sent_total - progress->sent_total;
	unsigned int received_delta = new_progress.received_total - progress->received_total;
	double sent_krps = 0.0;
	double received_krps = 0.0;
	unsigned int seconds_left = 0;
	if (as_nanoseconds(&progress->timestamp)) {
		double scale_factor = 1000000.0 / time_delta;
		sent_krps = sent_delta * scale_factor;
		received_krps = received_delta * scale_factor;
		seconds_left = (int) ((double) (total - new_progress.received_total)) / (received_krps * 1000.0);
	}

	printf(
		"\rSent: [%lu/%lu]@%.2lfKr/s, Received: [%lu/%lu]@%.2lfKr/s, Threads: [%lu/%lu], Conns: [%lu/%lu] ETA: %02d:%02d ",
		new_progress.sent_total,
		total,
		sent_krps,
		new_progress.received_total,
		total,
		received_krps,
		new_progress.threads_active,
		(unsigned long) ctx->cfg.nr_threads,
		new_progress.conns_active,
		(unsigned long) ctx->cfg.nr_threads * ctx->cfg.nr_connections,
		(seconds_left / 60) % 60,
		seconds_left % 60
	);
	fflush(stdout);
	*progress = new_progress;
	return 0;
}

int run_client(struct client_config_t *cfg) {
	int err = 0;

	struct client_context_t *ctx = prepare_client(cfg);
	if (!ctx) {
		perror("prepare_client");
		err = -1;
		goto out;
	}

	err = start_client(ctx);
	if (err) {
		perror("start_client");
		goto out_cleanup;
	}

	struct client_progress_t progress = { 0 };
	while (!client_threads_done(ctx)) {
		report_progress(ctx, &progress);
		if (cur_nanoseconds() >= ctx->deadline) {
			debug("stopping!\n");
			ctx->stopping = 1;
		}
		sleep(1);
	}

	err = join_client(ctx);
	if (err) {
		perror("join_client");
		goto out_cleanup;
	}
	report_client_stats(ctx);
out_cleanup:
	cleanup_client(ctx);
out:
	return err;
}

int main(int argc, char **argv) {
	struct command_t client_command = {
		.progname = argv[0],
		.description = "PIOT benchmark client",
		.params = client_params,
	};
	struct client_config_t cfg;
	if (parse_command_args(argc, argv, &cfg, &client_command)) {
		return 1;
	}
	return run_client(&cfg);
}
