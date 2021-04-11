#include <sys/epoll.h>
#include <errno.h>
#include <unistd.h>

#include "include/client.h"
#define NEED_DEBUG 0
#include "include/debug.h"

#define MAX_EVENTS	10

static int epoll_set_conn(int epollfd, struct client_connection_t *conn, int events) {
	struct epoll_event ev = {
		.events = events,
		.data.ptr = conn,
	};
	epoll_ctl(epollfd, EPOLL_CTL_DEL, conn->fd, NULL);
	if (!events)
		return 0;
	return epoll_ctl(epollfd, EPOLL_CTL_ADD, conn->fd, &ev);
}

static int init_client_thread_epoll(struct client_thread_t *cthread) {
	int fd = epoll_create1(0);
	if (fd < 0) {
		perror("epoll_create1");
		return -1;
	}

	struct epoll_event ev = {
		.events = EPOLLIN,
		.data.fd = cthread->ti->wakefd,
	};
	if (epoll_ctl(fd, EPOLL_CTL_ADD, cthread->ti->wakefd, &ev)) {
		perror("epoll_ctl");
		close(fd);
		return -1;
	}
	for (unsigned int i = 0; i < cthread->ctx->cfg.nr_connections; i++) {
		if (epoll_set_conn(fd, &cthread->conns[i], EPOLLIN | EPOLLOUT)) {
			perror("epoll_set_conn");
			close(fd);
			return -1;
		}
	}

	return fd;
}

static int client_conn_handle_input(
	struct client_thread_t *cthread,
	struct client_connection_t *conn
) {
	while (1) {
		/* init! receive new response */
		if (!conn->recvbuf.left) {
			debug("received response %d", conn->recvbuf.msg.id);
			/* Check if we're done: */
			if (conn->status.received == conn->status.total)
				return 0;
			conn->recvbuf.left = sizeof (struct response_t);
		}

		unsigned char *recvptr = (unsigned char *) &conn->recvbuf.msg.id;
		recvptr += (sizeof (struct response_t) - conn->recvbuf.left);
		int received = recv(conn->fd, recvptr, conn->recvbuf.left, 0);
		if (received == -1) {
			if (errno == EAGAIN)
				break;
			else {
				perror("client_conn_handle_input");
				return -1;
			}
		}
		debug("received = %d", received);
		conn->recvbuf.left -= received;
		if (!conn->recvbuf.left)
			conn->status.received++;
	}
	return 0;
}

static int client_conn_handle_output(
	struct client_thread_t *cthread,
	struct client_connection_t *conn
) {
	while (1) {
		/* init! create a new request */
		if (!conn->sendbuf.left) {
			if (cthread->ctx->stopping) {
				conn->status.total = conn->status.sent;
				return 0;
			}
			/* Check if we're done: */
			conn->sendbuf.left = sizeof (struct request_t);
			conn->sendbuf.msg.id = conn->status.sent;
			// for (unsigned int i = 0; i < REQUEST_LENGTH; i++) {
			// 	conn->sendbuf.msg.data[i] = rand();
			// }
		}

		unsigned char *sendptr = (unsigned char *) &conn->sendbuf.msg.id;
		sendptr += (sizeof (struct request_t) - conn->sendbuf.left);
		int written = send(conn->fd, sendptr, conn->sendbuf.left, 0);
		if (written == -1) {
			if (errno == EAGAIN)
				break;
			else {
				perror("client_conn_handle_output");
				return -1;
			}
		}
		// debug("written = %d", written);
		conn->sendbuf.left -= written;
		if (!conn->sendbuf.left) {
			debug("sent message %d", conn->status.sent);
			conn->status.sent++;
			if (conn->status.sent == conn->status.total)
				return 0;
		}
	}
	return 0;
}

static int handle_client_epoll_event(
	struct client_thread_t *cthread,
	struct client_connection_t *conn,
	int epollfd,
	struct epoll_event *evt
) {
	if (evt->events & EPOLLOUT) {
		debug("conn %d: output event", conn->fd);
		if (!conn->status.sent) {
			if (clock_gettime(CLOCK_MONOTONIC, &conn->status.start_time)) {
				perror("clock_gettime - start time");
				return -1;
			}
		}
		if (client_conn_handle_output(cthread, conn)) {
			perror("client_conn_handle_output");
			return -1;
		}
		if (conn->status.sent == conn->status.total) {
			if (epoll_set_conn(epollfd, conn, EPOLLIN)) {
				perror("epoll_set_conn");
				return -1;
			}
		}
	}

	if (evt->events & EPOLLIN) {
		debug("conn %d: input event", conn->fd);
		if (client_conn_handle_input(cthread, conn)) {
			perror("client_conn_handle_input");
			return -1;
		}
		if (conn->status.received == conn->status.total) {
			if (clock_gettime(CLOCK_MONOTONIC, &conn->status.end_time)) {
				perror("clock_gettime");
				return -1;
			}
			if (epoll_set_conn(epollfd, conn, 0)) {
				perror("epoll_set_conn");
				return -1;
			}
			close(conn->fd);
			conn->fd = -1;
		}
	}
	return 0;
}

static int thread_has_active_connections(struct client_thread_t *cthread) {
	int active = 0;
	for (int i = 0; i < cthread->ctx->cfg.nr_connections; i++) {
		if (cthread->conns[i].status.received < cthread->conns[i].status.total)
			active++;
	}
	return active;
}

void *client_worker(
	void *opaque,
	struct thread_info_t *ti
) {
	long ret = 0;
	struct client_thread_t *cthread = opaque;

	pthread_barrier_wait(&cthread->ctx->start_barrier.barrier);

	int epollfd = init_client_thread_epoll(cthread);
	if (epollfd < 0) {
		perror("init_client_thread_epoll");
		ret = -1;
		goto done;
	}

	while (thread_has_active_connections(cthread)) {
		struct epoll_event events[MAX_EVENTS];
		int nevents = epoll_wait(epollfd, events, MAX_EVENTS, 100);
		if (nevents == -1) {
			if (errno == EINTR)
				continue;
			perror("epoll_wait");
			ret = -1;
			break;
		}

		for (unsigned int i = 0; i < nevents; i++) {
			if (events[i].data.fd == ti->wakefd) {
				debug("stop signal received");
				break;
			}

			struct client_connection_t *conn = events[i].data.ptr;
			if (handle_client_epoll_event(cthread, conn, epollfd, &events[i])) {
				perror("handle_client_epoll_event");
				ret = -1;
				break;
			}
		}
	}

	close(epollfd);
done:
	return (void *) ret;
}
