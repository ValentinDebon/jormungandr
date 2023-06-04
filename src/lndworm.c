#include <stdio.h> /* fprintf */
#include <stdlib.h> /* exit, getenv */
#include <stdnoreturn.h> /* noreturn */
#include <sys/wait.h> /* waitpid, ... */
#include <string.h> /* strdup, memcpy */
#include <libgen.h> /* dirname, basename */
#include <alloca.h> /* alloca */
#include <fcntl.h> /* fcntl, open */
#include <err.h> /* warn, warnx, err */

#include <orm.h>

#include "cmdpath.h"

struct lndworm_args {
	const char *srccmd, *fmtcmd;
	const char *toolchain, *bsys;
	const char *sysroot, *srcdir;
	unsigned int pkgobj : 1;
	unsigned int rwsysroot : 1, rwsrcdir : 1;
};

static int
lndworm_wait(pid_t pid) {
	int wstatus;

	pid = waitpid(pid, &wstatus, 0);
	if (pid < 0) {
		warn("waitpid");
		return -1;
	}

	if (wstatus != 0) {
		if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 0) {
			warnx("Process %d exited with status %d", pid, WEXITSTATUS(wstatus));
		} else if (WIFSIGNALED(wstatus)) {
			warnx("Process %d killed by signal %d", pid, WTERMSIG(wstatus));
		} else if (WCOREDUMP(wstatus)) {
			warnx("Process %d dumped core", pid);
		}
		return -1;
	}

	return 0;
}

noreturn static void
lndworm_exec_bsys(const char *bsysname, char **args, int count) {
	static const char bsysdir[] = "/var/bsys/";
	const size_t bsysnamelen = strlen(bsysname);
	char * const bsys = alloca(sizeof (bsysdir) + bsysnamelen);
	char *argv[2 + count];
	int argc = 0;

	memcpy(bsys, bsysdir, sizeof (bsysdir) - 1);
	memcpy(bsys + sizeof (bsysdir) - 1, bsysname, bsysnamelen + 1);

	argc = 0;
	argv[argc++] = bsys;
	while (count != 0) {
		argv[argc++] = *args++;
		count--;
	}
	argv[argc] = NULL;

	execv(*argv, argv);
	err(EXIT_FAILURE, "execv '%s'", *argv);
}

noreturn static void
lndworm_exec_pkg(const char *fmtcmd, unsigned int pkgobj, int fd) {
	const char * const pkgdir = pkgobj ? "/var/obj" : "/var/dest";

	if (dup2(fd, STDOUT_FILENO) < 0) {
		err(EXIT_FAILURE, "dup2");
	}

	/* Remove FD_CLOEXEC from dup'ed fd. */
	if (fcntl(STDOUT_FILENO, F_SETFD, 0) != 0) {
		err(EXIT_FAILURE, "fcntl F_SETFD");
	}

	if (chdir(pkgdir) != 0) {
		err(EXIT_FAILURE, "chdir '%s'", pkgdir);
	}

	execlp("sh", "sh", "-c", fmtcmd, NULL);
	err(EXIT_FAILURE, "execp sh -c '%s'", fmtcmd);
}

noreturn static void
lndworm_pkg(const struct lndworm_args *args, int argc, char **argv, int fd) {
	struct orm_sandbox_description description = {
		.sysroot = args->sysroot, .srcdir = args->srcdir,
		.asroot = 1, .rosysroot = !args->rwsysroot,
		.rosrcdir = !args->rwsrcdir,
	};

	/* Find out srcdir. */
	if (description.srcdir == NULL) {
		description.srcdir = cmdpath(args->srccmd);
	} else {
		description.srcdir = realpath(description.srcdir, NULL);
	}

	if (description.srcdir == NULL) {
		err(EXIT_FAILURE, "Unable to lookup srcdir");
	}

	/* Find out toolchain root path. */
	char *root;
	if (orm_toolchain_path(args->toolchain, &root) != 0) {
		err(EXIT_FAILURE, "Unable to find toolchain '%s'", args->toolchain);
	}
	description.root = root;

	/* Find out bsys. */
	char *bsyspath, *bsysname;
	if (strchr(args->bsys, '/') != NULL) {
		bsyspath = strdup(args->bsys);
	} else if (orm_bsys_path(args->bsys, &bsyspath) != 0) {
		err(EXIT_FAILURE, "Unable to find bsys '%s'", args->bsys);
	}
	description.bsysdir = dirname(strdupa(bsyspath));
	bsysname = basename(strdupa(bsyspath));

	/* Enter the sandbox. */
	if (orm_sandbox(&description, getuid(), getgid()) != 0) {
		err(EXIT_FAILURE, "Unable to enter toolbox");
	}

	const pid_t pid = fork();
	if (pid < 0) {
		err(EXIT_FAILURE, "fork");
	}

	if (pid == 0) {
		lndworm_exec_bsys(bsysname, argv + optind, argc - optind);
	}

	if (lndworm_wait(pid) != 0) {
		exit(EXIT_FAILURE);
	}

	lndworm_exec_pkg(args->fmtcmd, args->pkgobj, fd);
}

static int
lndworm_run(const struct lndworm_args *args, int argc, char **argv, int fd) {

	const pid_t pid = fork();
	if (pid < 0) {
		warn("fork");
		return -1;
	}

	if (pid == 0) {
		lndworm_pkg(args, argc, argv, fd);
	}

	if (lndworm_wait(pid) != 0) {
		return -1;
	}

	return 0;
}

noreturn static void
lndworm_usage(const char *progname, int status) {

	fprintf(stderr,
		"usage: %1$s [-SUa] [-f <format>] [-t <toolchain>] [-b <bsys>] [-u <sysroot>] [-s <srcdir>] <archive> [<arguments>...]\n"
		"       %1$s -F <format command> [-SUa] [-t <toolchain>] [-b <bsys>] [-u <sysroot>] [-s <srcdir>] <archive> [<arguments>...]\n"
		"       %1$s -h\n",
		progname);

	exit(status);
}

static struct lndworm_args
lndworm_parse_args(int argc, char **argv) {
	static const struct {
		const char * const name;
		char * const command;
	} formats[] = {
		{ .name = "tar.gz", .command = "tar -cz ." },
		{ .name = "tar.xz", .command = "tar -cJ ." },
		{ .name = "tar.bz2", .command = "tar -cj ." },
		{ .name = "tar.comp", .command = "tar -cZ ." },
		{ .name = "tar", .command = "tar -c ." },
	};
	const char *format = NULL;
	struct lndworm_args args = {
		.srccmd = getenv("ORM_SRCDIR_COMMAND"),
		.toolchain = getenv("ORM_DEFAULT_TOOLCHAIN"),
		.bsys = getenv("ORM_DEFAULT_BSYS"),
		.sysroot = getenv("ORM_SYSROOT"),
	};
	int c;

	while (c = getopt(argc, argv, ":hF:SUaf:t:b:u:s:"), c >= 0) {
		switch (c) {
		case 'h': lndworm_usage(*argv, EXIT_SUCCESS);
		case 'F': args.fmtcmd = optarg; break;
		case 'S': args.rwsrcdir = 1; break;
		case 'U': args.rwsysroot = 1; break;
		case 'a': args.pkgobj = 1; break;
		case 'f': format = optarg; break;
		case 't': args.toolchain = optarg; break;
		case 'b': args.bsys = optarg; break;
		case 'u': args.sysroot = optarg; break;
		case 's': args.srcdir = optarg; break;
		case ':':
			warnx("Option -%c requires an operand", optopt);
			lndworm_usage(*argv, EXIT_FAILURE);
		case '?':
			warnx("Unrecognized option -%c", optopt);
			lndworm_usage(*argv, EXIT_FAILURE);
		}
	}

	if (args.fmtcmd != NULL && format != NULL) {
		warnx("Incompatible -F and -f options");
		lndworm_usage(*argv, EXIT_FAILURE);
	}

	if (optind == argc) {
		warnx("Missing archive name");
		lndworm_usage(*argv, EXIT_FAILURE);
	}

	if (args.srccmd == NULL) {
		args.srccmd = CONFIG_DEFAULT_SRCDIR_COMMAND;
	}

	if (args.fmtcmd == NULL) {
		unsigned int i;

		if (format == NULL) {
			const char * const path = argv[optind];
			const char * const extension = strchr(path, '.');

			if (extension == NULL) {
				warnx("Unable to infer archive format from '%s'", path);
				lndworm_usage(*argv, EXIT_FAILURE);
			}
			format = extension + 1;
		}

		i = 0;
		while (i < sizeof (formats) / sizeof (*formats)
			&& strcmp(formats[i].name, format) != 0) {
			i++;
		}
		if (i == sizeof (formats) / sizeof (*formats)) {
			warnx("Invalid format '%s'", format);
			lndworm_usage(*argv, EXIT_FAILURE);
		}

		args.fmtcmd = formats[i].command;
	}

	if (args.toolchain == NULL) {
		args.toolchain = "default";
	}

	if (args.bsys == NULL) {
		args.bsys = "default";
	}

	if (args.sysroot == NULL) {
		args.sysroot = "/";
	}

	return args;
}

int
main(int argc, char *argv[]) {
	const struct lndworm_args args = lndworm_parse_args(argc, argv);
	const char * const path = argv[optind++];
	int fd;

	/* Avoid interactivity in all subsequent processes,
	 * no need for dup2 here as STDIN_FILENO is always zero. */
	close(STDIN_FILENO);
	if (open("/dev/null", O_RDONLY) < 0) {
		err(EXIT_FAILURE, "open /dev/null");
	}

	/* Open the output file now, as later on, processes will
	 * be in the sandbox, and won't be able to access the host's filesystem. */
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0) {
		err(EXIT_FAILURE, "open '%s'", path);
	}

	if (lndworm_run(&args, argc, argv, fd) != 0) {
		/* In case of error, we must cleanup the created package,
		 * which means the toplevel parent process must never
		 * enter the sandbox, and be resilient to errors. */
		unlink(path);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
