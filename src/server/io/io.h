#ifndef __INTERNAL_IO_H
#define __INTERNAL_IO_H

#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>

#include "include/server.h"
int Z_accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
ssize_t Z_recv(int sockfd, void *buf, size_t len, int flags);
ssize_t Z_send(int sockfd, const void *buf, size_t len, int flags);

int Z_epoll_create1(int fl);
int Z_close(int fd);
int Z_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int Z_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
void Z_perror(const char *s);
int Z_ioctl(int fd, unsigned long request, unsigned long cmd);

static inline int epoll_set_conn_state(
	struct server_context_t *ctx,
	struct server_connection_t *conn,
	int new_state
) {
	int op;
	int old_state = conn->epoll_state;

	if (new_state == old_state) {
		return 0;
	}
	if (!new_state) {
		op = EPOLL_CTL_DEL;
	} else if (!old_state) {
		op = EPOLL_CTL_ADD;
	} else {
		op = EPOLL_CTL_MOD;
	}
	struct epoll_event evt = {
		.data.ptr = conn,
		.events = new_state,
	};
	conn->epoll_state = new_state;
	// debug("conn %d: state change 0x%x -> 0x%x", conn->fd, old_state, new_state);
	return Z_epoll_ctl(conn->epoll_fd, op, conn->fd, &evt);
}

#endif // __INTERNAL_IO_H
