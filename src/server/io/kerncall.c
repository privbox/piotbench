#ifdef HAVE_KERNCALL
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>

#include <linux/piot.h>

#define NEED_DEBUG 1
#include "include/debug.h"
#include "include/kerncall.h"
#include "io.h"

#define PIOT_PATH "/sys/kernel/debug/piot"

static __thread int kerncall_fd = -2;

struct kerncall_wrapper_arg {
	int (*entrypoint)(unsigned long);
	unsigned long arg;
	unsigned long fd;
	unsigned long ret;
};

static void kerncall_entrypoint(unsigned long arg) {
	struct kerncall_wrapper_arg *kcarg = (void *) arg;
	kcarg->ret = kcarg->entrypoint(kcarg->arg);
	Z_ioctl(kcarg->fd, PIOT_IOCRET, 0);
}

int kerncall_setup(void)
{
	struct piot_iocinfo info;
	int fd = open(PIOT_PATH, O_RDWR);
	if (fd < 0)
		return -1;
	int ret = Z_ioctl(fd, PIOT_IOCINFO, (uintptr_t) &info);
	close(fd);
	if (ret)
		return ret;
	kerncall_gate = (void *) info.kern_gate;
	kerncall_avail = 1;
	return 0;
}

long kerncall_spawn(uintptr_t ptr, unsigned long arg)
{
	if (kerncall_fd == -2) {
		kerncall_fd = open(PIOT_PATH, O_RDWR);
		debug("Init kerncall_fd: %d", kerncall_fd);
	}
	if (kerncall_fd == -1) {
		return -1;
	}

	struct kerncall_wrapper_arg kcarg = {
		.entrypoint = (void *) ptr,
		.arg = arg,
		.fd = kerncall_fd,
	};
	struct piot_iocspawn spawn = {
		.ip = (uintptr_t) kerncall_entrypoint,
		.arg = (uint64_t) &kcarg,
	};
	int err = Z_ioctl(kerncall_fd, PIOT_IOCSPAWN, (uintptr_t) &spawn);
	if (err)
		return err;
	return kcarg.ret;
}
#endif
void *kerncall_gate;
int kerncall_avail = 0;