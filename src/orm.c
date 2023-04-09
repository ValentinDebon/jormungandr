#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <err.h>

#include <orm.h>

struct orm_args {
	const char *srccmd;
	const char *toolchain, *bsys;
	const char *workspace, *sysroot;
	const char *destdir, *objdir, *srcdir;
	const char *workdir;
	unsigned int rwsysroot : 1, rwsrcdir : 1;
	unsigned int persistent : 1;
};

static char *
orm_srcdir(const char *command) {
	FILE * const fp = popen(command, "r");
	char *line, *path;
	ssize_t len;
	size_t n;

	if (fp == NULL) {
		return NULL;
	}

	n = PATH_MAX;
	line = malloc(n);
	if (line == NULL) {
		return NULL;
	}

	len = getline(&line, &n, fp);
	pclose(fp);

	if (len < 0) {
		errno = ENOENT;
		return NULL;
	}

	while (len > 0 && isspace(line[--len])) {
		line[len] = '\0';
	}

	path = strdupa(line);

	return realpath(path, line);
}

static char *
orm_workspace(const char *srcdir) {
	return strdup(basename(strdupa(srcdir)));
}

static void noreturn
orm_print_workdir(const struct orm_args *args) {
	char *workspace, *workdir;
	int flags = 0;

	if (args->workspace == NULL) {
		char *srcdir;

		if (args->srcdir == NULL) {
			srcdir = orm_srcdir(args->srccmd);
		} else {
			srcdir = realpath(srcdir, NULL);
		}

		if (srcdir == NULL) {
			err(EXIT_FAILURE, "Unable to lookup srcdir");
		}

		workspace = orm_workspace(srcdir);
	} else {
		workspace = strdup(args->workspace);
	}

	if (args->persistent) {
		flags |= ORM_WORKDIR_PERSISTENT;
	}

	if (orm_workdir(workspace, args->workdir, flags, &workdir) != 0) {
		err(EXIT_FAILURE, "Unable to lookup workdir '%s'", args->workdir);
	}

	puts(workdir);

	exit(EXIT_SUCCESS);
}

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

static void
orm_run(const struct orm_args *args, int argc, char **argv) {
	struct orm_sandbox_description description = {
		.sysroot = args->sysroot, .destdir = args->destdir,
		.objdir = args->objdir, .srcdir = args->srcdir,
		.asroot = 1, .rosysroot = !args->rwsysroot,
		.rosrcdir = !args->rwsrcdir,
	};
	char *workspace, *root, *bsys;
	int flags = 0, fd;

	/* Project paths. */

	if (description.srcdir == NULL) {
		description.srcdir = orm_srcdir(args->srccmd);
	} else {
		description.srcdir = realpath(description.srcdir, NULL);
	}

	if (description.srcdir == NULL) {
		err(EXIT_FAILURE, "Unable to lookup srcdir");
	}

	if (args->workspace == NULL) {
		workspace = orm_workspace(description.srcdir);
	} else {
		workspace = strdup(args->workspace);
	}

	if (workspace == NULL || *workspace == '\0' || *workspace == '.' || strcmp(workspace, "/") == 0) {
		errx(EXIT_FAILURE, "Unable to find out workspace name");
	}

	if (args->persistent) {
		flags |= ORM_WORKDIR_PERSISTENT;
	}

	if (description.objdir == NULL) {
		char *objdir;
		if (orm_workdir(workspace, "obj", flags, &objdir) != 0) {
			err(EXIT_FAILURE, "Unable to lookup objdir");
		}
		description.objdir = objdir;
	}

	if (description.destdir == NULL) {
		char *destdir;
		if (orm_workdir(workspace, "dest", flags, &destdir) != 0) {
			err(EXIT_FAILURE, "Unable to lookup destdir");
		}
		description.destdir = destdir;
	}

	/* Toolchain and build system. */

	if (orm_toolchain_path(args->toolchain, &root) != 0) {
		err(EXIT_FAILURE, "Unable to find toolchain '%s'", args->toolchain);
	}
	description.root = root;

	if (strchr(args->bsys, '/') != NULL) {
		bsys = strdup(args->bsys);
	} else if (orm_bsys_path(args->bsys, &bsys) != 0) {
		err(EXIT_FAILURE, "Unable to find bsys '%s'", args->bsys);
	}

	fd = open(bsys, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		err(EXIT_FAILURE, "Unable to open '%s'", bsys);
	}

	if (dup2(fd, STDIN_FILENO) != 0) {
		err(EXIT_FAILURE, "dup2");
	}

	/* Sandbox. */

	if (orm_sandbox(&description, getuid(), getgid()) != 0) {
		err(EXIT_FAILURE, "Unable to enter toolbox");
	}

	orm_exec(argc - optind, argv + optind);
}

static void noreturn
orm_usage(const char *progname, int status) {

	fprintf(stderr,
		"usage: %1$s [-PSU] [-t <toolchain>] [-b <bsys>] [-w <workspace>] [-u <sysroot>] [-d <destdir>] [-o <objdir>] [-s <srcdir>]\n"
		"       %1$s [-P] [-w <workspace>] [-s <srcdir>] -p <workdir>\n"
		"       %1$s -h\n",
		progname);

	exit(status);
}

static struct orm_args
orm_parse_args(int argc, char **argv) {
	struct orm_args args = {
		.srccmd = getenv("ORM_SRCDIR_COMMAND"),
		.toolchain = getenv("ORM_DEFAULT_TOOLCHAIN"),
		.bsys = getenv("ORM_DEFAULT_BSYS"),
		.workspace = getenv("ORM_WORKSPACE"),
		.sysroot = getenv("ORM_SYSROOT"),
	};
	int c;

	while (c = getopt(argc, argv, ":hPSUt:b:w:u:d:o:s:p:"), c != -1) {
		switch (c) {
		case 'h':
			orm_usage(*argv, EXIT_SUCCESS);
		case 'P':
			args.persistent = 1;
			break;
		case 'S':
			args.rwsrcdir = 1;
			break;
		case 'U':
			args.rwsysroot = 1;
			break;
		case 't':
			args.toolchain = optarg;
			break;
		case 'b':
			args.bsys = optarg;
			break;
		case 'w':
			args.workspace = optarg;
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
		case 'p':
			args.workdir = optarg;
			break;
		case ':':
			warnx("Option -%c requires an operand", optopt);
			orm_usage(*argv, EXIT_FAILURE);
		case '?':
			warnx("Unrecognized option -%c", optopt);
			orm_usage(*argv, EXIT_FAILURE);
		}
	}

	if (args.srccmd == NULL) {
		args.srccmd = "git rev-parse --show-toplevel";
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
	const struct orm_args args = orm_parse_args(argc, argv);

	if (args.workdir != NULL) {
		orm_print_workdir(&args);
	}

	orm_run(&args, argc, argv);
}
