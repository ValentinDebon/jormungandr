#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

#include <orm.h>

struct orm_args {
	const char *toolchain, *bsys;
	const char *sysroot, *destdir, *objdir, *srcdir;
};

static void noreturn
orm_exec(int n, char *args[]) {
	char *shell = getenv("SHELL"), *argv[4 + n];
	int argc = 0;

	if (shell == NULL) {
		shell = "/bin/sh";
	}

	argv[argc++] = shell;
	argv[argc++] = "-s";
	argv[argc++] = "--";
	while (n != 0) {
		argv[argc++] = *args++;
		n--;
	}
	argv[argc] = NULL;

	execv(shell, argv);

	err(EXIT_FAILURE, "execv '%s'", shell);
}

static void noreturn
orm_usage(const char *progname, int status) {

	fprintf(stderr, "usage: %s [-h] [-t <toolchain>] [-b <bsys>] [-u <sysroot>] [-d <destdir>] [-o <objdir>] [-s <srcdir>]\n", progname);

	exit(status);
}

static struct orm_args
orm_parse_args(int argc, char *argv[]) {
	struct orm_args args = {
		.toolchain = "default", .bsys = "default",
		.sysroot = NULL, .destdir = NULL,
		.objdir = NULL, .srcdir = NULL,
	};
	int c;

	while (c = getopt(argc, argv, ":ht:b:u:d:o:s:"), c != -1) {
		switch (c) {
		case 'h':
			orm_usage(*argv, EXIT_SUCCESS);
		case 't':
			args.toolchain = optarg;
			break;
		case 'b':
			args.bsys = optarg;
			break;
		case 'u':
			args.sysroot = optarg;
			break;
		case 'd':
			args.destdir = optarg;
			break;
		case 'o':
			args.objdir = optarg;
			break;
		case 's':
			args.srcdir = optarg;
			break;
		case ':':
			warnx("Option -%c requires an operand", optopt);
			orm_usage(*argv, EXIT_FAILURE);
		case '?':
			warnx("Unrecognized option -%c", optopt);
			orm_usage(*argv, EXIT_FAILURE);
		}
	}

	if (args.srcdir == NULL) {
		warnx("Missing source directory");
		orm_usage(*argv, EXIT_FAILURE);
	}

	return args;
}

int
main(int argc, char *argv[]) {
	const struct orm_args args = orm_parse_args(argc, argv);
	struct orm_sandbox_description description = {
		.root = args.toolchain,
		.sysroot = args.sysroot, .destdir = args.destdir,
		.objdir = args.objdir, .srcdir = args.srcdir,
		.asroot = 1, .rosysroot = 1, .rosrcdir = 1,
	};
	char *root, *bsys;
	int fd;

	if (orm_toolchain_path(args.toolchain, &root) != 0) {
		err(EXIT_FAILURE, "Unable to find toolchain '%s'", args.toolchain);
	}
	description.root = root;

	if (orm_bsys_path(args.bsys, &bsys) != 0) {
		err(EXIT_FAILURE, "Unable to find bsys '%s'", args.bsys);
	}

	fd = open(bsys, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		err(EXIT_FAILURE, "Unable to open '%s'", bsys);
	}

	if (orm_sandbox(&description, getuid(), getgid()) != 0) {
		err(EXIT_FAILURE, "Unable to enter toolbox");
	}

	if (dup2(fd, STDIN_FILENO) != 0) {
		err(EXIT_FAILURE, "dup2");
	}

	orm_exec(argc - optind, argv + optind);
}
