#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <unistd.h>
#include <err.h>

#include <orm.h>

struct orm_args {
	const char *toolroot, *bsys;
	const char *sysroot, *destdir, *objdir, *srcdir;
};

static void noreturn
orm_usage(const char *progname, int status) {

	fprintf(stderr, "usage: %s [-h] [-T <toolroot>] [-S <sysroot>] [-d <destdir>] [-o <objdir>] [-s <srcdir>] [-b <bsys>]\n", progname);

	exit(status);
}

static struct orm_args
orm_parse_args(int argc, char **argv) {
	struct orm_args args = {
		.toolroot = NULL, .bsys = "default",
		.sysroot = NULL, .destdir = NULL,
		.objdir = NULL, .srcdir = NULL,
	};
	int c;

	while (c = getopt(argc, argv, ":hT:b:S:d:o:s:"), c != -1) {
		switch (c) {
		case 'h':
			orm_usage(*argv, EXIT_SUCCESS);
		case 'T':
			args.toolroot = optarg;
			break;
		case 'b':
			args.bsys = optarg;
			break;
		case 'S':
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

	if (args.toolroot == NULL) {
		warnx("Missing toolroot");
		orm_usage(*argv, EXIT_FAILURE);
	}

	if (args.srcdir == NULL) {
		warnx("Missing source directory");
		orm_usage(*argv, EXIT_FAILURE);
	}

	return args;
}

int
main(int argc, char **argv) {
	const struct orm_args args = orm_parse_args(argc, argv);
	struct orm_sandbox_description description = {
		.root = args.toolroot,
		.sysroot = args.sysroot, .destdir = args.destdir,
		.objdir = args.objdir, .srcdir = args.srcdir,
		.asroot = 1, .rosysroot = 1, .rosrcdir = 1,
	};

	if (orm_sandbox(&description, getuid(), getgid()) != 0) {
		err(EXIT_FAILURE, "Unable to enter toolbox");
	}

	argv += optind - 1;
	*argv = "sh";
	execv("/bin/sh", argv);
	err(EXIT_FAILURE, "execv");

	return EXIT_SUCCESS;
}
