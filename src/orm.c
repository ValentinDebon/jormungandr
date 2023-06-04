#include <stdio.h> /* puts, fprintf */
#include <stdlib.h> /* realpath, ... */
#include <stdnoreturn.h> /* noreturn */
#include <string.h> /* strdup, memcpy, ... */
#include <unistd.h> /* getopt, ... */
#include <libgen.h> /* dirname, basename */
#include <alloca.h> /* alloca */
#include <err.h> /* err, errx, warnx */

#include <orm.h>

#include "cmdpath.h"

struct orm_args {
	const char *srccmd;
	const char *toolchain, *bsys;
	const char *workspace, *sysroot;
	const char *destdir, *objdir, *srcdir;
	const char *workdir;
	unsigned int rwsysroot : 1, rwsrcdir : 1;
	unsigned int persistent : 1, interactive : 1;
};

/**
 * Returns a candidate workspace name from a srcdir path.
 * @param srcdir Path to the source directory used.
 * @return On success, a new workspace, which must be _free(3)_'d.
 *   On error, returns NULL and sets errno appropriately.
 */
static char *
orm_workspace(const char *srcdir) {
	return strdup(basename(strdupa(srcdir)));
}

/**
 * Finds out and prints the absolute path of the given
 * workdir in the resolved (or given) workspace.
 * @param args Command line options.
 * @return Never
 */
noreturn static void
orm_print_workdir(const struct orm_args *args) {
	char *workspace, *workdir;
	int flags = 0;

	if (args->workspace == NULL) {
		char *srcdir;

		if (args->srcdir == NULL) {
			srcdir = cmdpath(args->srccmd);
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

/**
 * Execute the given bsys, or the user's shell if available.
 * @param bsysname The base name of the bsys, or NULL if interactive.
 * @param args Arguments to forward to the command.
 * @param count Number of arguments in args.
 * @return Never
 */
noreturn static void
orm_exec(const char *bsysname, char **args, int count) {
	char *argv[3 + count];
	int argc = 0;

	if (bsysname != NULL) {
		static const char bsysdir[] = "/var/bsys/";
		const size_t bsysnamelen = strlen(bsysname);
		char * const bsys = alloca(sizeof (bsysdir) + bsysnamelen);

		memcpy(bsys, bsysdir, sizeof (bsysdir) - 1);
		memcpy(bsys + sizeof (bsysdir) - 1, bsysname, bsysnamelen + 1);

		argv[argc++] = bsys;
	} else {
		char * const shell = getenv("SHELL");

		if (shell != NULL && access(shell, X_OK) == 0) {
			argv[argc++] = shell;
		} else {
			argv[argc++] = "/bin/sh";
		}
		argv[argc++] = "-i";
	}

	while (count != 0) {
		argv[argc++] = *args++;
		count--;
	}
	argv[argc] = NULL;

	execv(*argv, argv);

	err(EXIT_FAILURE, "execv '%s'", *argv);
}

/**
 * Setup the sandbox and execute the bsys, or go interactive.
 * @param args Command line options.
 * @param argc Forwarded from main().
 * @param argv Forwarded from main().
 * @return Never
 */
noreturn static void
orm_run(const struct orm_args *args, int argc, char **argv) {
	struct orm_sandbox_description description = {
		.sysroot = args->sysroot, .destdir = args->destdir,
		.objdir = args->objdir, .srcdir = args->srcdir,
		.asroot = 1, .rosysroot = !args->rwsysroot,
		.rosrcdir = !args->rwsrcdir,
	};

	/* Resolve the given (or not) source directory into
	 * an absolute path, either using cmdpath() or realpath(3) directly. */
	if (description.srcdir == NULL) {
		description.srcdir = cmdpath(args->srccmd);
	} else {
		description.srcdir = realpath(description.srcdir, NULL);
	}

	if (description.srcdir == NULL) {
		err(EXIT_FAILURE, "Unable to lookup srcdir");
	}

	/* Resolve the workspace, as a convenience,
	 * if none is specified, the srcdir's basename is used. */
	char *workspace;
	if (args->workspace == NULL) {
		workspace = orm_workspace(description.srcdir);
	} else {
		workspace = strdup(args->workspace);
	}

	/* Thus we need to check the workspace is a valid candidate... */
	if (workspace == NULL || *workspace == '\0' || *workspace == '.' || strcmp(workspace, "/") == 0) {
		errx(EXIT_FAILURE, "Unable to find out workspace name");
	}

	/* Use persistent-cache if requested. */
	int flags = 0;
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

	/* Resolve the toolchain's path. */
	char *root;
	if (orm_toolchain_path(args->toolchain, &root) != 0) {
		err(EXIT_FAILURE, "Unable to find toolchain '%s'", args->toolchain);
	}
	description.root = root;

	/* If bsys is a path (relative or not), use it directly,
	 * else, search the user's configurations for our bsys. */
	char *bsyspath;
	if (strchr(args->bsys, '/') != NULL) {
		bsyspath = strdup(args->bsys);
	} else if (orm_bsys_path(args->bsys, &bsyspath) != 0) {
		err(EXIT_FAILURE, "Unable to find bsys '%s'", args->bsys);
	}
	description.bsysdir = dirname(strdupa(bsyspath));

	char *bsysname;
	if (!args->interactive) {
		bsysname = basename(strdupa(bsyspath));
	} else {
		bsysname = NULL;
	}

	/* Enter the sandbox, as we don't need anything from the system now. */
	if (orm_sandbox(&description, getuid(), getgid()) != 0) {
		err(EXIT_FAILURE, "Unable to enter toolbox");
	}

	/* Execute either the bsys or the shell. */
	orm_exec(bsysname, argv + optind, argc - optind);
}

noreturn static void
orm_usage(const char *progname, int status) {

	fprintf(stderr,
		"usage: %1$s [-PSUi] [-t <toolchain>] [-b <bsys>] [-w <workspace>] [-u <sysroot>] [-d <destdir>] [-o <objdir>] [-s <srcdir>] [<arguments>...]\n"
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

	while (c = getopt(argc, argv, ":hPSUit:b:w:u:d:o:s:p:"), c >= 0) {
		switch (c) {
		case 'h': orm_usage(*argv, EXIT_SUCCESS);
		case 'P': args.persistent = 1; break;
		case 'S': args.rwsrcdir = 1; break;
		case 'U': args.rwsysroot = 1; break;
		case 'i': args.interactive = 1; break;
		case 't': args.toolchain = optarg; break;
		case 'b': args.bsys = optarg; break;
		case 'w': args.workspace = optarg; break;
		case 'u': args.sysroot = optarg; break;
		case 'd': args.destdir = optarg; break;
		case 'o': args.objdir = optarg; break;
		case 's': args.srcdir = optarg; break;
		case 'p': args.workdir = optarg; break;
		case ':':
			warnx("Option -%c requires an operand", optopt);
			orm_usage(*argv, EXIT_FAILURE);
		case '?':
			warnx("Unrecognized option -%c", optopt);
			orm_usage(*argv, EXIT_FAILURE);
		}
	}

	if (args.srccmd == NULL) {
		args.srccmd = CONFIG_DEFAULT_SRCDIR_COMMAND;
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
