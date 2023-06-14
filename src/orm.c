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
	const char *toolchain, *bsys;
	const char *workspace, *sysroot;
	const char *destdir, *objdir, *srcdir;
	const char *workdir;
	unsigned int asroot : 1, rwsysroot : 1, rwsrcdir : 1;
	unsigned int persistent : 1, interactive : 1;
};

/**
 * Finds out and prints the absolute path of the given
 * workdir in the resolved (or given) workspace.
 * @param args Command line options.
 * @return Never
 */
noreturn static void
orm_print_workdir(const struct orm_args *args) {
	char *workdir;
	int flags = 0;

	if (args->persistent) {
		flags |= ORM_WORKDIR_PERSISTENT;
	}

	if (orm_workdir(args->workspace, args->workdir, flags, &workdir) != 0) {
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
		.asroot = args->asroot, .rosysroot = !args->rwsysroot,
		.rosrcdir = !args->rwsrcdir,
	};

	/* Use persistent-cache if requested. */
	int flags = 0;
	if (args->persistent) {
		flags |= ORM_WORKDIR_PERSISTENT;
	}

	if (description.objdir == NULL) {
		char *objdir;
		if (orm_workdir(args->workspace, "obj", flags, &objdir) != 0) {
			err(EXIT_FAILURE, "Unable to lookup objdir");
		}
		description.objdir = objdir;
	}

	if (description.destdir == NULL) {
		char *destdir;
		if (orm_workdir(args->workspace, "dest", flags, &destdir) != 0) {
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
		"usage: %1$s [-PSUir] [-t <toolchain>] [-b <bsys>] [-w <workspace>] [-u <sysroot>] [-d <destdir>] [-o <objdir>] [-s <srcdir>] [<arguments>...]\n"
		"       %1$s [-P] [-w <workspace>] [-s <srcdir>] -p <workdir>\n"
		"       %1$s -h\n",
		progname);

	exit(status);
}

static struct orm_args
orm_parse_args(int argc, char **argv) {
	struct orm_args args = {
		.toolchain = getenv("ORM_DEFAULT_TOOLCHAIN"),
		.bsys = getenv("ORM_DEFAULT_BSYS"),
		.workspace = getenv("ORM_WORKSPACE"),
		.sysroot = getenv("ORM_SYSROOT"),
	};
	int c;

	while ((c = getopt(argc, argv, ":hPSUirt:b:w:u:d:o:s:p:")) >= 0) {
		switch (c) {
		case 'h': orm_usage(*argv, EXIT_SUCCESS);
		case 'P': args.persistent = 1; break;
		case 'S': args.rwsrcdir = 1; break;
		case 'U': args.rwsysroot = 1; break;
		case 'i': args.interactive = 1; break;
		case 'r': args.asroot = 1; break;
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

	/* No need to resolve a srcdir if we are just
	 * here for a workdir and workspace is already specified.
	 * This allows this synopsis to run without executing srccmd. */
	if (args.workdir == NULL || args.workspace == NULL) {
		/* Resolve the given (or not) source directory into
		 * an absolute path, either with cmdpath() or realpath(3). */
		if (args.srcdir == NULL) {
			const char *srccmd = getenv("ORM_SRCDIR_COMMAND");

			if (srccmd == NULL) {
				srccmd = CONFIG_DEFAULT_SRCDIR_COMMAND;
			}

			args.srcdir = cmdpath(srccmd);
		} else {
			args.srcdir = realpath(args.srcdir, NULL);
		}

		if (args.srcdir == NULL) {
			warn("Unable to lookup srcdir");
			orm_usage(*argv, EXIT_FAILURE);
		}
	}

	/* Resolve the workspace, as a convenience,
	 * if none is specified, the srcdir's basename is used. */
	if (args.workspace == NULL) {
		args.workspace = strdup(basename(strdupa(args.srcdir)));
	}

	/* Thus we need to check the workspace is a valid candidate... */
	if (args.workspace == NULL || *args.workspace == '\0'
		|| *args.workspace == '.' || strcmp(args.workspace, "/") == 0) {
		warnx("Unable to find out workspace name");
		orm_usage(*argv, EXIT_FAILURE);
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
