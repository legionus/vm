#include <sys/mount.h>
#include <sys/types.h>
#include <unistd.h>

static const char *init_prog[] = {
	"/virt/init.sh",
	NULL
};

int
main(void)
{
	if (mount("hostfs", "/host", "9p", MS_RDONLY, "trans=virtio,version=9p2000.L"))
		_exit(1);

	setsid();
	ioctl(0, TIOCSCTTY, 1);

	execve(init_prog[0], (char *const *) init_prog, NULL);
	_exit(2);

	return 0;
}
