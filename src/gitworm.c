/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdio.h> /* fprintf */
#include <stdlib.h> /* exit, getenv, ... */
#include <stdnoreturn.h> /* noreturn */
#include <sys/wait.h> /* waitpid, ... */
#include <string.h> /* strdup, memcpy, ... */
#include <libgen.h> /* dirname */
#include <unistd.h> /* getopt */
#include <fcntl.h> /* open, ... */
#include <err.h> /* warn, warnx, err */

#include <orm.h>

#include "common/bsysexec.h"
#include "common/extract.h"
#include "common/isdir.h"

struct gitworm_args {
	const char *path;
	const char *toolchain, *bsys, *sysroot;
	unsigned int asroot : 1, rwsysroot : 1, rwsrcdir : 1;
};

noreturn static void
gitworm_archive(const char *path, const char *treeish, int fd) {

	if (path != NULL && chdir(path) != 0) {
		err(EXIT_FAILURE, "chdir");
	}

	if (dup2(fd, STDOUT_FILENO) < 0) {
		err(EXIT_FAILURE, "dup");
	}

	const char *execpath = getenv("GIT_EXEC_PATH");
	if (execpath == NULL) {
		execpath = CONFIG_DEFAULT_GIT_EXEC_PATH;
	}
	const size_t execpathlen = strlen(execpath);

	static const char argv0[] = "git-archive";
	char name[execpathlen + sizeof (argv0) + 1];

	*(char *)mempcpy(name, execpath, execpathlen) = '/';
	memcpy(name + execpathlen + 1, argv0, sizeof (argv0));

	execl(name, argv0, "--format=tar", "--", treeish, NULL);
	err(-1, "exec %s", argv0);
}

static void
gitworm_wait(pid_t pid) {
	int wstatus;

	pid = waitpid(pid, &wstatus, 0);
	if (pid < 0) {
		err(EXIT_FAILURE, "waitpid");
	}

	if (wstatus != 0) {
		if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 0) {
			errx(EXIT_FAILURE, "Process %d exited with status %d", pid, WEXITSTATUS(wstatus));
		} else if (WIFSIGNALED(wstatus)) {
			errx(EXIT_FAILURE, "Process %d killed by signal %d", pid, WTERMSIG(wstatus));
		} else if (WCOREDUMP(wstatus)) {
			errx(EXIT_FAILURE, "Process %d dumped core", pid);
		}
	}
}

noreturn static void
gitworm_exec(const struct gitworm_args *args, int argc, char **argv, pid_t pid, int fd) {
	struct orm_sandbox_description description = {
		.asroot = args->asroot,
	};

	/* Open or describe sysroot. */
	int sysrootfd;
	if (!isdir(args->sysroot)) {
		sysrootfd = open(args->sysroot, O_RDONLY | O_CLOEXEC);
		if (sysrootfd < 0) {
			err(EXIT_FAILURE, "open '%s'", args->sysroot);
		}
	} else {
		description.sysroot = args->sysroot;
		description.rosysroot = !args->rwsysroot;
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

	/* Extract sysroot archive if not mounted directory. */
	if (description.sysroot == NULL) {
		extract("/var/sysroot", !args->rwsysroot, sysrootfd);
	}

	/* Extract srcdir from pipe. */
	extract("/var/src", !args->rwsrcdir, fd);

	gitworm_wait(pid);

	bsysexec(bsysname, argv + optind, argc - optind);
}

noreturn static void
gitworm_usage(const char *progname, int status) {

	fprintf(stderr,
		"usage: %1$s [-SUr] [-C <path>] [-t <toolchain>] [-b <bsys>] [-u <sysroot>] <tree-ish> [<arguments>...]\n"
		"       %1$s -h\n",
		progname);

	exit(status);
}

static struct gitworm_args
gitworm_parse_args(int argc, char **argv) {
	struct gitworm_args args = {
		.toolchain = getenv("ORM_DEFAULT_TOOLCHAIN"),
		.bsys = getenv("ORM_DEFAULT_BSYS"),
		.sysroot = getenv("ORM_SYSROOT"),
	};
	int c;

	while ((c = getopt(argc, argv, ":hSUrC:t:b:u:")) >= 0) {
		switch (c) {
		case 'h': gitworm_usage(*argv, EXIT_SUCCESS);
		case 'S': args.rwsrcdir = 1; break;
		case 'U': args.rwsysroot = 1; break;
		case 'r': args.asroot = 1; break;
		case 'C': args.path = optarg; break;
		case 't': args.toolchain = optarg; break;
		case 'b': args.bsys = optarg; break;
		case 'u': args.sysroot = optarg; break;
		case ':':
			warnx("Option -%c requires an operand", optopt);
			gitworm_usage(*argv, EXIT_FAILURE);
		case '?':
			warnx("Unrecognized option -%c", optopt);
			gitworm_usage(*argv, EXIT_FAILURE);
		}
	}

	if (optind == argc) {
		warnx("Missing tree or commit name");
		gitworm_usage(*argv, EXIT_FAILURE);
	}

	if (args.sysroot == NULL) {
		args.sysroot = "/";
	}

	if (args.toolchain == NULL) {
		args.toolchain = "default";
	}

	if (args.bsys == NULL) {
		args.bsys = "default";
	}

	return args;
}

int
main(int argc, char *argv[]) {
	const struct gitworm_args args = gitworm_parse_args(argc, argv);
	const char * const treeish = argv[optind++];

	int pipefd[2];
	if (pipe2(pipefd, O_CLOEXEC) < 0) {
		err(EXIT_FAILURE, "pipe");
	}

	const pid_t pid = fork();
	if (pid < 0) {
		err(EXIT_FAILURE, "fork");
	}

	if (pid == 0) {
		gitworm_archive(args.path, treeish, pipefd[1]);
	}

	close(pipefd[1]);

	gitworm_exec(&args, argc, argv, pid, pipefd[0]);
}
