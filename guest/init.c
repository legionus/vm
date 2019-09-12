#define _GNU_SOURCE
#define DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <errno.h>
#include <error.h>

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/reboot.h>

#include <linux/reboot.h>

/* Set a signal handler. */
#define SETSIG(sa, sig, fun, flags) \
		do { \
			sa.sa_handler = fun; \
			sa.sa_flags = flags; \
			sigemptyset(&sa.sa_mask); \
			sigaction(sig, &sa, NULL); \
		} while(0)

#define MODULES_FILE "/virt/etc/modules"
#define ENV_FILE     "/virt/etc/environ"
#define SANDBOX_EXEC "/virt/sandbox.sh"
#define SHELL_EXEC   "/bin/sh"

static const char *sandbox_prog[] = { SANDBOX_EXEC, NULL };
static const char *shell_prog[]   = { SHELL_EXEC,   NULL };

static char **env;

static const char *default_env[] = {
	"PATH=/sbin:/usr/sbin:/usr/local/sbin:/lib/initrd/bin:/bin:/usr/bin:/usr/local/bin",
	"TERM=linux",
	"HOME=/virt/home",
	"PS1=[shell]# ",
	NULL
};
#ifndef _GNU_SOURCE
extern char **environ;
#endif
extern long init_module(void *, unsigned long, const char *);

static sig_atomic_t got_ECHILD;

static void
chld_handler(int sig __attribute__ ((__unused__)))
{
	int rc, status;
	int saved_errno = errno;

	while ((rc = waitpid(-1, &status, WNOHANG))) {
		if (rc < 0 && errno == ECHILD) {
			got_ECHILD = 1;
			break;
		}
	}

	errno = saved_errno;
}

static  pid_t
run(const char *cmd[])
{
	const char *exe = cmd[0];
	pid_t pid;

#ifdef DEBUG
	error(0, 0, "Executing %s", exe);
#endif
	if ((pid = fork()) == -1) {
		error(0, errno, "fork");
		return -1;
	}
	if (pid == 0) {
		execve(exe, (char *const *) cmd, env);
		error(0, errno, "%s: execve:", exe);
		_exit(EXIT_FAILURE);
	}
	return pid;
}

static int
run_wait(const char *prog[])
{
	int status = 0;
	pid_t pid = run(prog);

	if (pid == -1)
		return EXIT_FAILURE;

	if (waitpid(pid, &status, 0) == -1) {
		error(0, errno, "waitpid");
		return EXIT_FAILURE;
	}

	if (!status)
		return EXIT_SUCCESS;

	if (WIFEXITED(status))
		error(0, 0, "%s: terminated with exit code %d",
		      prog[0], WEXITSTATUS(status));
	else if (WIFSIGNALED(status))
		error(0, 0, "%s: terminated by signal %d",
		      prog[0], WEXITSTATUS(status));
	else
		error(0, 0, "%s: terminated, status=%d", prog[0], status);

	return EXIT_FAILURE;
}

static void
do_mounts(void)
{
	if (!access("/host", R_OK)) {
		if (mount("hostfs", "/host", "9p", MS_RDONLY, "trans=virtio,version=9p2000.L"))
			error(0, errno, "mount failed [/host]");
	}

	if (mount("proc",     "/proc", "proc",     0UL, NULL)) {
		if (errno != EBUSY)
			error(0, errno, "mount failed [/proc]");
	}

	if (mount("sysfs",    "/sys",  "sysfs",    0UL, NULL)) {
		if (errno != EBUSY)
			error(0, errno, "mount failed [/sys]");
	}

	if (mount("devtmpfs", "/dev",  "devtmpfs", 0UL, NULL)) {
		if (errno != EBUSY)
			error(0, errno, "mount failed [/dev]");
	}

	if (access("/dev/pts", R_OK)) {
		mkdir("/dev/pts", 0755);
	}

	if (mount("devpts", "/dev/pts", "devpts",  0UL, NULL)) {
		if (errno != EBUSY)
			error(0, errno, "mount failed [/dev/pts]");
	}

	if (mount("tmpfs",    "/tmp",  "tmpfs",    0UL, NULL)) {
		if (errno != EBUSY)
			error(0, errno, "mount failed [/tmp]");
	}
}

static void __attribute__ ((noreturn))
sysreboot(void)
{
	if ((reboot(LINUX_REBOOT_CMD_RESTART)) == -1)
		error(EXIT_FAILURE, errno, "reboot");

	for (;;) pause();
}

static const char *
moderror(int err)
{
	switch (err) {
		case ENOEXEC:
			return "Invalid module format";
		case ENOENT:
			return "Unknown symbol in module";
		case ESRCH:
			return "Module has wrong symbol version";
		case EINVAL:
			return "Invalid parameters";
	}
	return strerror(err);
}

static void *
mmap_module(const char *filename, size_t *image_size_p)
{
	void *image = NULL;
	struct stat st;
	size_t image_size;
	int fd;

	if ((fd = open(filename, O_RDONLY)) == -1) {
		error(0, errno, "open");
		return NULL;
	}

	if (fstat(fd, &st) == -1) {
		error(0, errno, "fstat");
		close(fd);
		return NULL;
	}

	image_size = (size_t) st.st_size;

	if ((image = mmap(NULL, image_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
		error(0, errno, "mmap");
		close(fd);
		return NULL;
	}

	*image_size_p = image_size;

	close(fd);
	return image;
}

static void
load_modules(void)
{
	char path[MAXPATHLEN];

	FILE *f = NULL;

	size_t image_size;
	void *image;
	const char image_params[] = "";

	if (access(MODULES_FILE, R_OK) != 0) {
		printf("%s not found. Nothing to do ...\n", MODULES_FILE);
		return;
	}

	if ((f = fopen(MODULES_FILE, "r")) == NULL)
		error(EXIT_FAILURE, errno, "fopen");

	while (fscanf(f, "%s", path) != EOF) {
		image_size = 0;

		image = mmap_module(path, &image_size);
		if (!image)
			continue;

		if (init_module(image, image_size, image_params) != 0) {
			if (errno != EEXIST)
				error(EXIT_FAILURE, 0, "init_module: %s: %s\n", path, moderror(errno));
		}

		munmap(image, image_size);
	}

	if (ferror(f))
		error(EXIT_FAILURE, errno, "fscanf");

	fclose(f);
}

static void
load_env(void)
{
	char *envvar = NULL;
	char *line = NULL;
	size_t len = 0;
	ssize_t nread;

	FILE *f = NULL;

	if (access(ENV_FILE, R_OK) != 0) {
		printf("%s not found. Nothing to do ...\n", ENV_FILE);
		return;
	}

	if ((f = fopen(ENV_FILE, "r")) == NULL)
		error(EXIT_FAILURE, errno, "fopen");

	while ((nread = getline(&line, &len, f)) != -1) {
		if (nread == 0 || line[0] == '\n')
			continue;

		if (line[nread-1] == '\n')
			nread--;

		envvar = strndup(line, (size_t) nread);

		if (!envvar)
			error(EXIT_FAILURE, errno, "strndup");

		putenv(envvar);
	}

	free(line);
	fclose(f);

	env = environ;
}

int
main(void)
{
	const char **prog = shell_prog;
	struct sigaction sa;

	SETSIG(sa, SIGINT,  SIG_IGN,      SA_RESTART);
	SETSIG(sa, SIGTSTP, SIG_IGN,      SA_RESTART);
	SETSIG(sa, SIGQUIT, SIG_IGN,      SA_RESTART);
	SETSIG(sa, SIGCHLD, chld_handler, SA_RESTART);

	env = (char **) default_env;

	load_modules();
	load_env();
	do_mounts();

	setsid();
	ioctl(0, TIOCSCTTY, 1);

	if (!access(SANDBOX_EXEC, R_OK))
		prog = sandbox_prog;

	run_wait(prog);
	sysreboot();

	error(EXIT_FAILURE, errno, "init");
	return 0;
}
