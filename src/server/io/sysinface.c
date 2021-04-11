#include <sys/types.h>
#include <sys/syscall.h>
#include <errno.h>

#include "include/kerncall.h"

#include "io.h"

#define SYSCALL_RETURN(ret)		\
	do							\
	{							\
		if(ret < 0) {			\
			errno = -ret;		\
			ret = -1;			\
		}						\
	} while (0)

static __inline unsigned long get_cs() {
	unsigned long ret;
	__asm__ __volatile__ ("mov %%cs, %0" : "=r"(ret) : : );
	return ret;
}

static __inline unsigned long get_cpl() {
	return get_cs() & 3;
}

static __inline int can_use_call() {
	return get_cpl() == 0;
	// return 0;
}


static inline long Z_syscall0(long n)
{
	long ret;
	if (!can_use_call()) {
		__asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
	} else {
		__asm__ __volatile__ ("call *%2" : "=a"(ret) : "a"(n), "m"(kerncall_gate) : "rcx", "r11", "memory");
	}
	SYSCALL_RETURN(ret);
	return ret;
}

static inline long Z_syscall1(long n, long a1)
{
	long ret;
	if (!can_use_call()) {
		__asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
	} else {
		__asm__ __volatile__ ("call *%3" : "=a"(ret) : "a"(n), "D"(a1), "m"(kerncall_gate) : "rcx", "r11", "memory");
	}
	SYSCALL_RETURN(ret);
	return ret;
}

static inline long Z_syscall2(long n, long a1, long a2)
{
	long ret;
	if (!can_use_call()) {
		__asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2)
						: "rcx", "r11", "memory");
	} else {
		__asm__ __volatile__ ("call *%4" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "m"(kerncall_gate)
							  : "rcx", "r11", "memory");
	}
	SYSCALL_RETURN(ret);
	return ret;
}

static inline long Z_syscall3(long n, long a1, long a2, long a3)
{
	long ret;
	if (!can_use_call()) {
		__asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
							  "d"(a3) : "rcx", "r11", "memory");
	} else {
		__asm__ __volatile__ ("call *%5" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
							  "d"(a3), "m"(kerncall_gate) : "rcx", "r11", "memory");
	}
	SYSCALL_RETURN(ret);
	return ret;
}

static inline long Z_syscall4(long n, long a1, long a2, long a3, long a4)
{
	long ret;
	register long r10 __asm__("r10") = a4;
	if (!can_use_call()) {
		__asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
							  "d"(a3), "r"(r10): "rcx", "r11", "memory");
	} else {
		__asm__ __volatile__ ("call *%6" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
							  "d"(a3), "r"(r10), "m"(kerncall_gate): "rcx", "r11", "memory");
	}
	SYSCALL_RETURN(ret);
	return ret;
}

static inline long Z_syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
	long ret;
	register long r10 __asm__("r10") = a4;
	register long r8 __asm__("r8") = a5;
	if (!can_use_call()) {
		__asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
							  "d"(a3), "r"(r10), "r"(r8) : "rcx", "r11", "memory");	
	} else {
		__asm__ __volatile__ ("call *%7" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
							  "d"(a3), "r"(r10), "r"(r8), "m"(kerncall_gate) : "rcx", "r11", "memory");	
	}
	SYSCALL_RETURN(ret);
	return ret;
}

static inline long Z_syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
	long ret;
	register long r10 __asm__("r10") = a4;
	register long r8 __asm__("r8") = a5;
	register long r9 __asm__("r9") = a6;

	if (!can_use_call()) {
		__asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
							  "d"(a3), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
	} else {
		__asm__ __volatile__ ("call *%8" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
							  "d"(a3), "r"(r10), "r"(r8), "r"(r9), "m"(kerncall_gate) : "rcx", "r11", "memory");
	}
	SYSCALL_RETURN(ret);
	return ret;
}

int Z_accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
	return Z_syscall4(SYS_accept, sockfd, (uintptr_t) addr, (uintptr_t) addrlen, flags);
	// return accept(sockfd, addr, addrlen);
}
ssize_t Z_recv(int sockfd, void *buf, size_t len, int flags) {
	return Z_syscall6(SYS_recvfrom, sockfd, (uintptr_t) buf, len, flags, 0, 0);
	// return recv(sockfd, buf, len, flags);
}
ssize_t Z_send(int sockfd, const void *buf, size_t len, int flags) {
	return Z_syscall6(SYS_sendto, sockfd, (uintptr_t) buf, len, flags, 0, 0);
	// return send(sockfd, buf, len, flags);
}
int Z_close(int fd) {
	return Z_syscall1(SYS_close, fd);
}
int Z_epoll_create1(int fl) {
	return Z_syscall1(SYS_epoll_create1, fl);
	// return epoll_create1(fl);
}
int Z_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
	return Z_syscall4(SYS_epoll_ctl, epfd, op, fd, (uintptr_t) event);
	// return epoll_ctl(epfd, op, fd, event);
}
int Z_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
	return Z_syscall4(SYS_epoll_wait, epfd, (uintptr_t) events, maxevents, timeout);
	// return epoll_wait(epfd, events, maxevents, timeout);
}

int Z_ioctl(int fd, unsigned long request, unsigned long cmd) {
	return Z_syscall3(SYS_ioctl, fd, request, cmd);
 // int ioctl(int fd, unsigned long request, ...);
}

void Z_perror(const char *s) {
	perror(s);
	return;
}