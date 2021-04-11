#ifndef __BENCHMARK_CLIENT_H
#define __BENCHMARK_CLIENT_H

#include <time.h>
#include <pthread.h>

#include "rpc.h"
#include "utils.h"
#include "thread.h"

#define MAX_CLIENT_THREADS 128
#define MAX_PATH_LEN	128

struct client_config_t {
	char server_path[MAX_PATH_LEN];
	unsigned int nr_threads;
	unsigned int nr_connections; // per thread
	unsigned int nr_requests; // per connection
	int duration;
};

struct client_status_t {
	struct timespec start_time;
	struct timespec end_time;
	unsigned long sent;
	unsigned long received;
	unsigned long total;
};

#define CONN_OPEN 1
#define CONN_DONE 2

struct client_connection_t {
	int state;
	int fd;
	struct {
		struct request_t msg;
		int left;
	} sendbuf;
	struct {
		struct response_t msg;
		int left;
	} recvbuf;
	struct client_status_t status;
};

struct client_thread_t {
	int init;
	struct thread_info_t *ti;
	struct client_context_t *ctx;
	struct client_connection_t *conns;
};

struct client_context_t {
	int stopping;
	struct client_config_t cfg;
	struct client_thread_t client_threads[MAX_CLIENT_THREADS];
	struct barrier_t start_barrier;
	uint64_t deadline;
};

void *client_worker(
	void *opaque,
	struct thread_info_t *ti
);

extern struct client_context_t *prepare_client(struct client_config_t *cfg);
extern int start_client(struct client_context_t *ctx);
extern int join_client(struct client_context_t *ctx);
extern void cleanup_client(struct client_context_t *ctx);

#endif // __BENCHMARK_CLIENT_H
