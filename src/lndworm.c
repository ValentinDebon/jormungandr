/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdio.h> /* fprintf */
#include <stdlib.h> /* exit, getenv */
#include <stdbool.h> /* bool */
#include <stdnoreturn.h> /* noreturn */
#include <sys/wait.h> /* waitpid, ... */
#include <sys/mount.h> /* mount */
#include <sys/stat.h> /* stat */
#include <string.h> /* strdup, memcpy */
#include <libgen.h> /* dirname, basename */
#include <alloca.h> /* alloca */
#include <fcntl.h> /* fcntl, open */
#include <err.h> /* warn, warnx, err */

#include <orm.h>

#include "cmdpath.h"

struct lndworm_args {
	const char *sysxcmd, *srcxcmd, *outccmd;
	const char *toolchain, *bsys;
	const char *sysroot, *src;
	unsigned int pkgobj : 1;
	unsigned int asroot : 1, rwsysroot : 1, rwsrcdir : 1;
};

struct lndworm_pkgfmt {
	const char * const extension;
	char * const xcmd, * const ccmd;
};

static const struct lndworm_pkgfmt pkgfmts[] = {
	{
		.extension = "tar.gz",
		.xcmd = "tar -xzf -",
		.ccmd = "tar -cz .",
	},
	{
		.extension = "tar.xz",
		.xcmd = "tar -xJf -",
		.ccmd = "tar -cJ .",
	},
	{
		.extension = "tar.bz2",
		.xcmd = "tar -xjf -",
		.ccmd = "tar -cj .",
	},
	{
		.extension = "tar.comp",
		.xcmd = "tar -xZf -",
		.ccmd = "tar -cZ .",
	},
	{
		.extension = "tar",
		.xcmd = "tar -xf -",
		.ccmd = "tar -c .",
	},
};

static const char *
lndworm_extension(const char *path) {
	const char *name = strrchr(path, '/');
	const char *extension;
	char *end;

	if (name == NULL) {
		name = path;
	} else {
		name++;
	}

	extension = strchr(name, '.');

	if (extension != NULL) {
		/* Skip potential numbers in name,
		 * such as a version, a date, etc... */
		while (strtoul(extension + 1, &end, 10), *end == '.') {
			extension = end;
		}

		if (extension[1] == '\0') {
			extension = NULL;
		} else {
			extension++;
		}
	}

	return extension;
}

static const struct lndworm_pkgfmt *
lndworm_pkgfmt_find_for_extension(const char *extension) {
	unsigned int i = 0;

	while (i < sizeof (pkgfmts) / sizeof (*pkgfmts)
		&& strcmp(pkgfmts[i].extension, extension) != 0) {
		i++;
	}

	if (i == sizeof (pkgfmts) / sizeof (*pkgfmts)) {
		return NULL;
	}

	return pkgfmts + i;
}

static bool
lndworm_isdir(const char *path) {
	struct stat st;

	if (stat(path, &st) != 0) {
		err(EXIT_FAILURE, "stat '%s'", path);
	}

	return (st.st_mode & S_IFMT) == S_IFDIR;
}

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
lndworm_exec_xcmd(const char *xcmd, const char *wd, int fd) {

	if (dup2(fd, STDIN_FILENO) < 0) {
		err(EXIT_FAILURE, "dup2");
	}

	/* Remove FD_CLOEXEC from dup'ed fd. */
	if (fcntl(STDIN_FILENO, F_SETFD, 0) != 0) {
		err(EXIT_FAILURE, "fcntl F_SETFD");
	}

	if (chdir(wd) != 0) {
		err(EXIT_FAILURE, "chdir '%s'", wd);
	}

	execlp("sh", "sh", "-c", xcmd, NULL);
	err(EXIT_FAILURE, "execp sh -c '%s'", xcmd);
}

static void
lndworm_extract(const char *xcmd, const char *path, unsigned int ro, int fd) {
	const pid_t pid = fork();
	if (pid < 0) {
		err(EXIT_FAILURE, "fork");
	}

	if (pid == 0) {
		lndworm_exec_xcmd(xcmd, path, fd);
	}

	if (lndworm_wait(pid) != 0) {
		exit(EXIT_FAILURE);
	}

	if (ro && mount("", path, "", MS_REMOUNT | MS_RDONLY | MS_BIND, NULL) != 0) {
		err(EXIT_FAILURE, "mount '%s' ro", path);
	}
}

noreturn static void
lndworm_exec_ccmd(const char *ccmd, const char *wd, int fd) {

	if (dup2(fd, STDOUT_FILENO) < 0) {
		err(EXIT_FAILURE, "dup2");
	}

	/* Remove FD_CLOEXEC from dup'ed fd. */
	if (fcntl(STDOUT_FILENO, F_SETFD, 0) != 0) {
		err(EXIT_FAILURE, "fcntl F_SETFD");
	}

	if (chdir(wd) != 0) {
		err(EXIT_FAILURE, "chdir '%s'", wd);
	}

	execlp("sh", "sh", "-c", ccmd, NULL);
	err(EXIT_FAILURE, "execp sh -c '%s'", ccmd);
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
lndworm_pkg(const struct lndworm_args *args, int argc, char **argv, int fd) {
	struct orm_sandbox_description description = {
		.asroot = args->asroot,
	};

	/* Open or describe sysroot. */
	int sysrootfd;
	if (args->sysxcmd != NULL) {
		sysrootfd = open(args->sysroot, O_RDONLY | O_CLOEXEC);
		if (sysrootfd < 0) {
			err(EXIT_FAILURE, "open '%s'", args->sysroot);
		}
	} else {
		description.sysroot = args->sysroot;
		description.rosysroot = !args->rwsysroot;
	}

	/* Open or describe srcdir. */
	int srcfd;
	if (args->srcxcmd != NULL) {
		srcfd = open(args->src, O_RDONLY | O_CLOEXEC);
		if (srcfd < 0) {
			err(EXIT_FAILURE, "open '%s'", args->src);
		}
	} else {
		description.srcdir = args->src;
		description.rosrcdir = !args->rwsrcdir;
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
		lndworm_extract(args->sysxcmd, "/var/sysroot", !args->rwsysroot, sysrootfd);
		close(sysrootfd);
	}

	/* Extract src archive if not mounted directory. */
	if (description.srcdir == NULL) {
		lndworm_extract(args->srcxcmd, "/var/src", !args->rwsrcdir, srcfd);
		close(srcfd);
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

	lndworm_exec_ccmd(args->outccmd,
		args->pkgobj ? "/var/obj" : "/var/dest", fd);
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
		"usage: %1$s [-ASUr] [-g <sysroot format>] [-q <src format>] [-f <output format>]"
			" [-t <toolchain>] [-b <bsys>] [-u <sysroot>] [-s <src>] <output> [<arguments>...]\n"
		"       %1$s -h\n",
		progname);

	exit(status);
}

static struct lndworm_args
lndworm_parse_args(int argc, char **argv) {
	struct lndworm_args args = {
		.sysxcmd = getenv("ORM_SYSROOT_EXTRACT_COMMAND"),
		.srcxcmd = getenv("ORM_SRCDIR_EXTRACT_COMMAND"),
		.outccmd = getenv("ORM_OUTPUT_CREATE_COMMAND"),
		.toolchain = getenv("ORM_DEFAULT_TOOLCHAIN"),
		.bsys = getenv("ORM_DEFAULT_BSYS"),
		.sysroot = getenv("ORM_SYSROOT"),
	};
	const char *sysext = NULL, *srcext = NULL, *outext = NULL;
	int c;

	while ((c = getopt(argc, argv, ":hASUrg:q:f:t:b:u:s:")) >= 0) {
		switch (c) {
		case 'h': lndworm_usage(*argv, EXIT_SUCCESS);
		case 'A': args.pkgobj = 1; break;
		case 'S': args.rwsrcdir = 1; break;
		case 'U': args.rwsysroot = 1; break;
		case 'r': args.asroot = 1; break;
		case 'g': sysext = optarg; break;
		case 'q': srcext = optarg; break;
		case 'f': outext = optarg; break;
		case 't': args.toolchain = optarg; break;
		case 'b': args.bsys = optarg; break;
		case 'u': args.sysroot = optarg; break;
		case 's': args.src = optarg; break;
		case ':':
			warnx("Option -%c requires an operand", optopt);
			lndworm_usage(*argv, EXIT_FAILURE);
		case '?':
			warnx("Unrecognized option -%c", optopt);
			lndworm_usage(*argv, EXIT_FAILURE);
		}
	}

	if (optind == argc) {
		warnx("Missing output name");
		lndworm_usage(*argv, EXIT_FAILURE);
	}

	if (args.sysroot == NULL) {
		args.sysroot = "/";
	}

	if (!lndworm_isdir(args.sysroot)) {
		if (sysext != NULL || args.sysxcmd == NULL) {
			const struct lndworm_pkgfmt *pkgfmt;

			if (sysext == NULL) {
				sysext = lndworm_extension(args.sysroot);
				if (sysext == NULL) {
					warnx("Unable to infer sysroot format from '%s'", args.sysroot);
					lndworm_usage(*argv, EXIT_FAILURE);
				}
			}

			pkgfmt = lndworm_pkgfmt_find_for_extension(sysext);
			if (pkgfmt == NULL) {
				warnx("Invalid sysroot format '%s'", sysext);
				lndworm_usage(*argv, EXIT_FAILURE);
			}

			args.sysxcmd = pkgfmt->xcmd;
		}
	} else {
		if (sysext != NULL) {
			warnx("Extraneous sysroot format for directory '%s'", args.sysroot);
			lndworm_usage(*argv, EXIT_FAILURE);
		}
		args.sysxcmd = NULL;
	}

	if (args.src == NULL) {
		const char *srccmd = getenv("ORM_SRCDIR_COMMAND");

		if (srccmd == NULL) {
			srccmd = CONFIG_DEFAULT_SRCDIR_COMMAND;
		}

		args.src = cmdpath(srccmd);
	} else {
		args.src = realpath(args.src, NULL);
	}

	if (args.src == NULL) {
		warn("Unable to lookup src path");
		lndworm_usage(*argv, EXIT_FAILURE);
	}

	if (!lndworm_isdir(args.src)) {
		if (srcext != NULL || args.srcxcmd == NULL) {
			const struct lndworm_pkgfmt *pkgfmt;

			if (srcext == NULL) {
				srcext = lndworm_extension(args.src);
				if (srcext == NULL) {
					warnx("Unable to infer src format from '%s'", args.src);
					lndworm_usage(*argv, EXIT_FAILURE);
				}
			}

			pkgfmt = lndworm_pkgfmt_find_for_extension(srcext);
			if (pkgfmt == NULL) {
				warnx("Invalid src format '%s'", srcext);
				lndworm_usage(*argv, EXIT_FAILURE);
			}

			args.srcxcmd = pkgfmt->xcmd;
		}
	} else {
		if (srcext != NULL) {
			warnx("Extraneous src format for directory '%s'", args.src);
			lndworm_usage(*argv, EXIT_FAILURE);
		}
		args.srcxcmd = NULL;
	}

	if (outext != NULL || args.outccmd == NULL) {
		const struct lndworm_pkgfmt *pkgfmt;

		if (outext == NULL) {
			const char * const path = argv[optind];
			outext = lndworm_extension(path);
			if (outext == NULL) {
				warnx("Unable to infer output format from '%s'", path);
				lndworm_usage(*argv, EXIT_FAILURE);
			}
		}

		pkgfmt = lndworm_pkgfmt_find_for_extension(outext);
		if (pkgfmt == NULL) {
			warnx("Invalid output format '%s'", outext);
			lndworm_usage(*argv, EXIT_FAILURE);
		}

		args.outccmd = pkgfmt->ccmd;
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
