#ifndef __BENCHMARK_SERVER_H
#define __BENCHMARK_SERVER_H

#include "rpc.h"
#include "thread.h"
#include "queue.h"

#define SERVER_MAX_THREADS 128
#define MAX_PATH_LEN	128

#define _KERNCALL_COND(cfg, local)					\
	(cfg.kerncall.global || cfg.kerncall.local)
#define KERNCALL_COND(cfg, local)				\
	(kerncall_avail && _KERNCALL_COND(cfg, local))

struct server_config_t {
	char socket_path[MAX_PATH_LEN];
	struct {
		unsigned int compute_dur; // in usec
		unsigned int max_io_size;
		unsigned int readahead;
	} load;
	struct {
		unsigned int io;
		unsigned int accept;
		unsigned int submit;
		unsigned int compute;
	} threads;
	struct {
		unsigned int sessions;
		unsigned int buffers;
	} alloc;
	struct {
		unsigned int global;
		unsigned int io;
		unsigned int accept;
		unsigned int submit;
	} kerncall;
};

struct server_queues_t {
	struct queue_root *empty_connections;
	struct queue_root *empty_buffers;
	struct queue_root *compute_inbox[SERVER_MAX_THREADS];
	struct queue_root *submitter_inbox[SERVER_MAX_THREADS];
};

struct server_threads_t {
	struct thread_group_t *compute;
	struct thread_group_t *submit;
	struct thread_group_t *io;
	struct thread_group_t *accept;
};

struct server_io_t {
	int listen_fd;
	size_t nr_io;
	int epoll_fds[SERVER_MAX_THREADS];
	int next;
};

struct server_context_t {
	struct server_config_t cfg;
	struct server_queues_t queues;
	struct server_threads_t threads;
	struct server_io_t io;
	int stopping;
};

struct server_buffer_t {
	struct queue_head q;
	struct message_t msg;
	struct server_connection_t *conn;
	size_t left;
	unsigned char *ptr;
};

struct server_connection_t {
	struct queue_head q;
	int fd;
	int closed;
	int ioidx;
	int submitidx;
	int computeidx;
	int epoll_fd;
	int epoll_state;
	struct server_buffer_t *recvbuf;
	struct server_buffer_t *sendbuf;
	struct queue_root send_queue;
	unsigned long received;
	unsigned long processed;  // FIXME atomic send counter
	unsigned long sent;
	lock_t lock;
};

extern void *compute_worker(void *opaque, struct thread_info_t *ti);
extern void *submitter_worker(void *opaque, struct thread_info_t *ti);
extern void *io_worker(void *opaque, struct thread_info_t *ti);
extern void *accept_worker(void *opaque, struct thread_info_t *ti);

int epoll_conn_finish(struct server_context_t *ctx, struct server_connection_t *conn);

extern struct server_context_t *create_server(struct server_config_t *cfg);
extern int destroy_server(struct server_context_t *ctx);

#endif // __BENCHMARK_SERVER_H
