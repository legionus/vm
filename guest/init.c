#include <sys/mount.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/reboot.h>

#define SETSIG(sa, sig, fun, flags)			\
		do {					\
			sa.sa_handler = fun;		\
			sa.sa_flags = flags;		\
			sigemptyset(&sa.sa_mask);	\
			sigaction(sig, &sa, NULL);	\
		} while(0)

const char *prog_env[] = {
	"PATH=/sbin:/usr/sbin:/usr/local/sbin:/bin:/usr/bin:/usr/local/bin:/virt/bin",
	"TERM=linux",
	"HOME=/virt/home",
	"PS1=[shell \\W]# ",
	NULL
};

static const char *shell_prog[]   = { "/bin/bash", NULL };
static const char *sandbox_prog[] = { "/virt/sandbox.sh", NULL };

static pid_t run(const char *cmd[])
{
	const char *exe = cmd[0];
	pid_t pid;

	fprintf(stderr, "Executing %s ...\n", exe);

	if ((pid = fork()) == -1) {
		fprintf(stderr, "Error: fork failed with %d (errno=%d)\n", pid, errno);
		return pid;
	}
	if (pid == 0) {
		execve(exe, (char *const *) cmd, (char *const *) prog_env);
		fprintf(stderr, "Error: execv (errno=%d): %s\n", errno, exe);
		_exit(EXIT_FAILURE);
	}
	return pid;
}

static int run_wait(const char *prog[])
{
	int status = 0;
	pid_t pid = run(prog);

	if (pid == -1)
		return EXIT_FAILURE;
	if (waitpid(pid, &status, 0) == -1) {
		fprintf(stderr, "Error: waitpid (errno=%d)\n", errno);
		return EXIT_FAILURE;
	}
	if (!status)
		return EXIT_SUCCESS;
	if (WIFSIGNALED(status))
		fprintf(stderr, "Error: %s: terminated by signal %d",
				prog[0], WTERMSIG(status));
	else if (WIFEXITED(status))
		fprintf(stderr, "Error: %s: terminated with exit code %d",
				prog[0], WEXITSTATUS(status));
	return EXIT_FAILURE;
}

static void chld_handler(int sig __attribute__ ((__unused__)))
{
	int rc, status;
	int saved_errno = errno;

	while ((rc = waitpid(-1, &status, WNOHANG))) {
		if (rc < 0 && errno == ECHILD) {
			break;
		}
	}

	errno = saved_errno;
}

static void do_mounts(void)
{
	fprintf(stderr, "Mounting...\n");

	mount("hostfs", "/host", "9p", MS_RDONLY, "trans=virtio,version=9p2000.L");
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	mount("proc", "/proc", "proc", 0, NULL);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	mkdir("/dev/pts", 0755);
	mount("devpts", "/dev/pts", "devpts", 0, NULL);
	mount("tmpfs", "/tmp", "tmpfs", 0, NULL);
}

static void set_winsize(void)
{
	char cmdline[BUFSIZ];
	char *start, *endptr = NULL;
	int fd;
	unsigned long val;
	struct winsize wsz;

	if ((fd = open("/proc/cmdline", O_RDONLY)) == -1 ||
	    read(fd, cmdline, sizeof(cmdline)) == -1)
		return;
	close(fd);

	if ((start = strstr(cmdline, "winsize=")) == NULL)
		return;
	start += 8;

	errno = 0;
	val = strtoul(start, &endptr, 0);

	if (val == 0 || val > USHRT_MAX || errno != 0)
		return;
	wsz.ws_row = (unsigned short) val;

	if (!endptr || *endptr != 'x')
		return;

	errno = 0;
	val = strtoul(endptr + 1, NULL, 0);

	if (val == 0 || val > USHRT_MAX || errno != 0)
		return;
	wsz.ws_col = (unsigned short) val;

	fprintf(stderr, "Resizing window to %ux%u...\n",
			wsz.ws_row, wsz.ws_col);

	ioctl(0, TIOCSWINSZ, &wsz);
}

int main(void)
{
	struct sigaction sa;
	int run_shell = 1;

	do_mounts();
	setsid();
	ioctl(0, TIOCSCTTY, 1);
	set_winsize();

	SETSIG(sa, SIGINT,  SIG_IGN, SA_RESTART);
	SETSIG(sa, SIGTSTP, SIG_IGN, SA_RESTART);
	SETSIG(sa, SIGQUIT, SIG_IGN, SA_RESTART);
	SETSIG(sa, SIGCHLD, chld_handler, SA_RESTART);

	if (!access(sandbox_prog[0], R_OK) && !run_wait(sandbox_prog))
		run_shell = 0;

	if (run_shell)
		run_wait(shell_prog);

	sync();
	reboot(RB_AUTOBOOT);

	fprintf(stderr, "Init failed (errno=%d)\n", errno);

	return 0;
}
