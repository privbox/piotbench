#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>

int bind_sock(const char *path) {
	int fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return fd;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, path);
	unlink(path);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(fd);
		return -1;
	}
	if (listen(fd, 1024)) {
		perror("listen");
		close(fd);
		return -1;
	}
	return fd;
}
