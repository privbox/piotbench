#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>
#define NEED_DEBUG 1
#include "include/debug.h"
#include "include/kerncall.h"
#include "include/server.h"
#include "include/thread.h"
#include "include/utils.h"
#include "include/command.h"

#define SERVER_PARAM_UINT(field_name, desc, default_) \
	PARAM_UINT(struct server_config_t, field_name, desc, default_)
#define SERVER_PARAM_STR(field_name, desc, default_) \
	PARAM_STR(struct server_config_t, field_name, desc, default_)

struct param_t server_params[] = {
	SERVER_PARAM_STR(
		socket_path,
		"Server socket path",
		"/tmp/bench-server"
	),
	SERVER_PARAM_UINT(
		load.compute_dur,
		"Duration in nsec for compute load function",
		100
	),
	SERVER_PARAM_UINT(
		load.max_io_size,
		"Max IO size for send/recv syscalls",
		128
	),
	SERVER_PARAM_UINT(
		load.readahead,
		"Max readahead for each connection",
		1000
	),
	SERVER_PARAM_UINT(
		threads.io,
		"Number of IO threads",
		1
	),
	SERVER_PARAM_UINT(
		threads.compute,
		"Number of compute threads",
		1
	),
	SERVER_PARAM_UINT(
		threads.submit,
		"Number of submit threads",
		1
	),
	SERVER_PARAM_UINT(
		threads.accept,
		"Number of accept threads",
		1
	),
	SERVER_PARAM_UINT(
		alloc.sessions,
		"Session objects to pre-allocate",
		100
	),
	SERVER_PARAM_UINT(
		alloc.buffers,
		"Buffer objects to pre-allocate",
		100000
	),
	SERVER_PARAM_UINT(
		kerncall.global,
		"Run threads in kerncall (global setting)",
		1
	),
	SERVER_PARAM_UINT(
		kerncall.io,
		"Run IO threads in kerncall",
		0
	),
	SERVER_PARAM_UINT(
		kerncall.accept,
		"Run accept threads in kerncall",
		0
	),
	SERVER_PARAM_UINT(
		kerncall.submit,
		"Run submit threads in kerncall",
		0
	),
	LAST_PARAM,
};

static int bind_server_socket(struct server_context_t *ctx) {
	int fd = bind_sock(ctx->cfg.socket_path);
	if (fd >= 0) {
		ctx->io.listen_fd = fd;
		return 0;
	}
	return -1;
}

static int alloc_one_queue(struct queue_root **qptr) {
	struct queue_root *q = alloc_queue_root();
	if (!q)
		return -1;
	*qptr = q;
	return 0;
}

static int alloc_n_queues(struct queue_root *queues[], unsigned int n) {
	for (unsigned int i = 0; i < n; i++) {
		if (alloc_one_queue(&queues[i]))
			return -1;
	}
	return 0;
}

static int alloc_server_queues(struct server_context_t *ctx) {
	return (
		alloc_one_queue(&ctx->queues.empty_connections) ||
		alloc_one_queue(&ctx->queues.empty_buffers) ||
		alloc_n_queues(ctx->queues.compute_inbox, ctx->cfg.threads.compute) ||
		alloc_n_queues(ctx->queues.submitter_inbox, ctx->cfg.threads.submit)
	);
}

static int cleanup_one_queue(struct queue_root **qptr) {
	// FIXME elements?
	free(*qptr);
	return 0;
}

static int cleanup_n_queues(struct queue_root *queues[], unsigned int n) {
	for (unsigned int i = 0; i < n; i++) {
		if (cleanup_one_queue(&queues[i]))
			return -1;
	}
	return 0;
}
static int cleanup_server_queues(struct server_context_t *ctx) {
	return (
		cleanup_one_queue(&ctx->queues.empty_connections) ||
		cleanup_one_queue(&ctx->queues.empty_buffers) ||
		cleanup_n_queues(ctx->queues.compute_inbox, ctx->cfg.threads.compute) ||
		cleanup_n_queues(ctx->queues.submitter_inbox, ctx->cfg.threads.submit)
	);
}

static int setup_server_prealloc(struct server_context_t *ctx) {
	for (int i = 0; i < ctx->cfg.alloc.sessions; i++) {
		struct server_connection_t *conn = aligned_alloc(64, sizeof (struct server_connection_t));
		if (!conn) {
			perror("aligned_alloc");
			return -1;
		}
		queue_put(&conn->q, ctx->queues.empty_connections);
	}
	for (int i = 0; i < ctx->cfg.alloc.buffers; i++) {
		struct server_buffer_t *buff = aligned_alloc(64, sizeof (struct server_buffer_t));
		if (!buff) {
			perror("aligned_alloc");
			return -1;
		}
		queue_put(&buff->q, ctx->queues.empty_buffers);
	}
	return 0;
}

static int free_server_prealloc(struct server_context_t *ctx) {
	struct queue_head *q;
	while ((q = queue_get(ctx->queues.empty_connections))) {
		free(container_of(q, struct server_connection_t, q));
	}
	while ((q = queue_get(ctx->queues.empty_buffers))) {
		free(container_of(q, struct server_buffer_t, q));
	}
	return 0;
}

static int spawn_server_threads(struct server_context_t *ctx) {
	// Compute threads
	ctx->threads.compute = thread_group_create(
		"compute",
		ctx->cfg.threads.compute,
		compute_worker,
		ctx
	);
	if (!ctx->threads.compute) {
		perror("thread_group_create/compute");
		return -1;
	}
	// Submitter threads
	ctx->threads.submit = thread_group_create(
		"submit",
		ctx->cfg.threads.submit,
		submitter_worker,
		ctx
	);
	if (!ctx->threads.submit) {
		perror("thread_group_create/submit");
		return -1;
	}
	// IO threads
	ctx->threads.io = thread_group_create(
		"IO",
		ctx->cfg.threads.io,
		io_worker,
		ctx
	);
	if (!ctx->threads.io) {
		perror("thread_group_create/io");
		return -1;
	}
	// Accept threads
	ctx->threads.accept = thread_group_create(
		"accept",
		ctx->cfg.threads.accept,
		accept_worker,
		ctx
	);
	if (!ctx->threads.accept) {
		perror("thread_group_create/accept");
		return -1;
	}
	return 0;
}

static int setup_server_epoll(struct server_context_t *ctx) {
	ctx->io.nr_io = ctx->cfg.threads.io;
	ctx->io.next = 0;
	for (unsigned int i = 0; i < ctx->io.nr_io; i++) {
		int fd = epoll_create1(0);
		if (fd < 0) {
			perror("epoll_create0");
			return -1;
		}
		ctx->io.epoll_fds[i] = fd;
	}
	return 0;
}

static int cleanup_server_epoll(struct server_context_t *ctx) {
	for (unsigned int i = 0; i < ctx->io.nr_io; i++) {
		if (ctx->io.epoll_fds >= 0)
			close(ctx->io.epoll_fds[0]);
	}
	return 0;
}

static int setup_server_io(struct server_context_t *ctx) {
	// Bind socket
	if (bind_server_socket(ctx)) {
		perror("bind_server_socket");
		return -1;
	}

	// Set up epoll
	if (setup_server_epoll(ctx)) {
		perror("setup_server_epoll");
		return -1;
	}
	return 0;
}

struct server_context_t *create_server(struct server_config_t *cfg) {
	// Allocate ctx
	struct server_context_t *ctx = aligned_alloc(64, sizeof (struct server_context_t));
	if (!ctx) {
		perror("aligned_alloc");
		goto out;
	}
	// debug("ctx size is %d", sizeof (struct server_context_t));
	memset(ctx, 0, sizeof (struct server_context_t));
	ctx->cfg = *cfg;
	ctx->io.listen_fd = -1;
	for (unsigned int i = 0; i < SERVER_MAX_THREADS; i++) {
		ctx->io.epoll_fds[i] = -1;
	}

	if (setup_server_io(ctx)) {
		perror("setup_server_io");
		goto out_cleanup;
	}

	// Allocate queues
	if (alloc_server_queues(ctx)) {
		perror("alloc_server_queues");
		goto out_cleanup;
	}

	// Alloc queue elements
	if (setup_server_prealloc(ctx)) {
		perror("alloc_server_messages");
		goto out_cleanup;
	}

	// Spawn threads
	if (spawn_server_threads(ctx)) {
		perror("spawn_server_threads");
		goto out_cleanup;
	}
	return ctx;
out_cleanup:
	destroy_server(ctx);
out:
	return NULL;
}

static void cleanup_server_io(struct server_context_t *ctx) {
	cleanup_server_epoll(ctx);
	if (ctx->io.listen_fd >= 0)
		close(ctx->io.listen_fd);
}

static void cleanup_server_threads(struct server_context_t *ctx) {
	if (ctx->threads.submit)
		thread_group_join(ctx->threads.submit, NULL);
	if (ctx->threads.io)
		thread_group_join(ctx->threads.io, NULL);
	if (ctx->threads.accept)
		thread_group_join(ctx->threads.accept, NULL);
	if (ctx->threads.compute)
		thread_group_join(ctx->threads.compute, NULL);
}

int destroy_server(struct server_context_t *ctx) {
	ctx->stopping = 1;
	cleanup_server_threads(ctx);
	free_server_prealloc(ctx);
	cleanup_server_queues(ctx);
	cleanup_server_io(ctx);
	free(ctx);
	return 0;
}

static int *stop = NULL;

static void sigint_handler(int signr) {
	if (!*stop) {
		debug("got signal, setting flag!");
		*stop = 1;
	} else {
		debug("got repead signal, exiting!");
		exit(1);
	}
}

int run_server(struct server_config_t *cfg) {
	struct server_context_t *ctx = create_server(cfg);
	stop = &ctx->stopping;	
	signal(SIGINT, sigint_handler);
	while (!ctx->stopping) {
		sleep(1);
	}
	destroy_server(ctx);
	return 0;
}

int main(int argc, char **argv) {
	struct command_t server_command = {
		.progname = argv[0],
		.description = "PIOT benchmark server",
		.params = server_params,
	};
	struct server_config_t cfg;
	if (parse_command_args(argc, argv, &cfg, &server_command)) {
		return 1;
	}
	signal(SIGPIPE, SIG_IGN);
	
	kerncall_setup();

	if (
		!kerncall_avail &&
		(cfg.kerncall.accept || cfg.kerncall.io || cfg.kerncall.submit)
	) {
		debug("warning: kerncall requested but not available");
	}
	return run_server(&cfg);
}
