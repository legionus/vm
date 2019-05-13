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
#define SANDBOX_EXEC "/virt/sandbox.sh"
#define SHELL_EXEC   "/bin/sh"

static const char *sandbox_prog[] = { SANDBOX_EXEC, NULL };
static const char *shell_prog[]   = { SHELL_EXEC,   NULL };

static const char *const run_env[] = {
	"TERM=linux",
	"HOME=/virt/home",
	"PS1=[shell]# ",
	NULL
};

extern long init_module(void *, unsigned long, const char *);

static sig_atomic_t got_ECHILD;

static void
chld_handler(int sig __attribute__ ((__unused__)))
{
	int     rc, status;
	int     saved_errno = errno;

	while ((rc = waitpid(-1, &status, WNOHANG)))
	{
		if (rc < 0 && errno == ECHILD)
		{
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
	pid_t   pid;

#ifdef DEBUG
	error(0, 0, "Executing %s", exe);
#endif
	if ((pid = fork()) == -1)
	{
		error(0, errno, "fork");
		return -1;
	}
	if (pid == 0)
	{
		execve(exe, (char *const *) cmd, (char *const *) run_env);
		error(0, errno, "%s: execve:", exe);
		_exit(EXIT_FAILURE);
	}
	return pid;
}

static int
run_wait(const char *prog[])
{
	int     status = 0;
	pid_t   pid = run(prog);

	if (pid == -1)
		return EXIT_FAILURE;

	if (waitpid(pid, &status, 0) == -1)
	{
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
			fprintf(stderr, "mount failed [/host]: %s\n", strerror(errno));
	}

	if (mount("proc",     "/proc", "proc",     0UL, NULL)) {
		if (errno != EBUSY) {
			fprintf(stderr, "mount failed [/proc]: %s\n", strerror(errno));
		}
	}

	if (mount("sysfs",    "/sys",  "sysfs",    0UL, NULL)) {
		if (errno != EBUSY) {
			fprintf(stderr, "mount failed [/sys]: %s\n", strerror(errno));
		}
	}

	if (mount("devtmpfs", "/dev",  "devtmpfs", 0UL, NULL)) {
		if (errno != EBUSY) {
			fprintf(stderr, "mount failed [/dev]: %s\n", strerror(errno));
		}
	}

	if (access("/dev/pts", R_OK)) {
		mkdir("/dev/pts", 0755);
	}

	if (mount("devpts", "/dev/pts", "devpts",  0UL, NULL)) {
		if (errno != EBUSY) {
			fprintf(stderr, "mount failed [/dev/pts]: %s\n", strerror(errno));
		}
	}

	if (mount("tmpfs",    "/tmp",  "tmpfs",    0UL, NULL)) {
		if (errno != EBUSY) {
			fprintf(stderr, "mount failed [/tmp]: %s\n", strerror(errno));
		}
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
		perror("open");
		return NULL;
	}

	if (fstat(fd, &st) == -1) {
		perror("fstat");
		close(fd);
		return NULL;
	}

	image_size = (size_t) st.st_size;

	if ((image = mmap(NULL, image_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
		perror("mmap");
		close(fd);
		return NULL;
	}

	*image_size_p = image_size;

	close(fd);
	return image;
}

static int
load_modules(void)
{
	char path[MAXPATHLEN];

	FILE *f = NULL;
	int rc = 0;

	size_t image_size;
	void *image;
	const char image_params[] = "";

	if (access(MODULES_FILE, R_OK) != 0) {
		printf("%s not found. Nothing to do ...\n", MODULES_FILE);
		return 0;
	}

	if ((f = fopen(MODULES_FILE, "r")) == NULL) {
		perror("fopen");
		return 1;
	}

	while (fscanf(f, "%s", path) != EOF) {
		image_size = 0;

		image = mmap_module(path, &image_size);
		if (!image) {
			continue;
		}

		if (init_module(image, image_size, image_params) != 0) {
			if (errno != EEXIST) {
				fprintf(stderr, "Error: init_module: %s: %s\n", path, moderror(errno));

				munmap(image, image_size);

				rc = 1;
				break;
			}
		}

		munmap(image, image_size);
	}

	if (ferror(f)) {
		perror("fscanf");
		rc = 1;
	}

	fclose(f);
	return rc;
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

	if (load_modules())
		return 1;

	do_mounts();

	setsid();
	ioctl(0, TIOCSCTTY, 1);

	if (!access(SANDBOX_EXEC, R_OK)) {
		prog = sandbox_prog;
	}

	run_wait(prog);
	sysreboot();

	error(EXIT_FAILURE, errno, "init");
	return 0;
}
