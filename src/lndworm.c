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

#include <archive.h>
#include <archive_entry.h>
#include <orm.h>

#include "common/bsysexec.h"
#include "common/cmdpath.h"
#include "common/extract.h"
#include "common/isdir.h"

struct lndworm_args {
	const char *format, *filter;
	const char *toolchain, *bsys;
	const char *sysroot, *src;
	unsigned int pkgobj : 1;
	unsigned int asroot : 1, rwsysroot : 1, rwsrcdir : 1;
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

static void
lndworm_archive_copy_from_disk(const char *sourcepath, struct archive *out) {
	static char buffer[CONFIG_ARCHIVE_OUTPUT_BLOCK_SIZE];
	const int fd = open(sourcepath, O_RDONLY);
	ssize_t copied;

	if (fd < 0) {
		err(EXIT_FAILURE, "open '%s'", sourcepath);
	}

	while (copied = read(fd, buffer, sizeof (buffer)), copied > 0) {
		la_ssize_t written;
		size_t total = 0;

		while (written = archive_write_data(out, buffer + total, copied - total),
			written >= 0 && (total += written) != copied);

		if (written < 0) {
			errx(EXIT_FAILURE, "archive_write_data: %s", archive_error_string(out));
		}
	}

	if (copied < 0) {
		err(EXIT_FAILURE, "read '%s'", sourcepath);
	}

	close(fd);
}

static void
lndworm_archive_create(const char *input, const char *format, const char *filter, const char *output, int fd) {
	struct archive * const out = archive_write_new(), * const in = archive_read_disk_new();

	if (archive_read_disk_set_symlink_physical(in) != ARCHIVE_OK) {
		errx(EXIT_FAILURE, "archive_read_disk_set_symlink_physical: %s", archive_error_string(in));
	}

	if (archive_read_disk_set_standard_lookup(in) != ARCHIVE_OK) {
		errx(EXIT_FAILURE, "archive_read_disk_set_standard_lookup: %s", archive_error_string(in));
	}

	if (archive_read_disk_open(in, input) != ARCHIVE_OK) {
		errx(EXIT_FAILURE, "archive_read_disk_open: %s", archive_error_string(in));
	}

	if (format != NULL) {
		if (archive_write_set_format_by_name(out, format) != ARCHIVE_OK) {
			errx(EXIT_FAILURE, "archive_write_set_format_by_name: %s", archive_error_string(out));
		}
		if (filter != NULL && archive_write_add_filter_by_name(out, filter) != ARCHIVE_OK) {
			errx(EXIT_FAILURE, "archive_write_add_filter_by_name: %s", archive_error_string(out));
		}
	} else {
		if (archive_write_set_format_filter_by_ext(out, output) != ARCHIVE_OK) {
			errx(EXIT_FAILURE, "archive_write_set_format_filter_by_ext: %s", archive_error_string(out));
		}
	}

	if (archive_write_open_fd(out, fd) != ARCHIVE_OK) {
		errx(EXIT_FAILURE, "archive_write_open_fd: %s", archive_error_string(out));
	}

	struct archive_entry *entry;
	int status = archive_read_next_header(in, &entry);
	if (status != ARCHIVE_OK || archive_read_disk_descend(in) != ARCHIVE_OK) {
		errx(EXIT_FAILURE, "archive_read_next_header"
			": Unable to descend into '%s': %s", output, archive_error_string(in));
	}

	const size_t inputlen = strlen(input);
	while (status = archive_read_next_header(in, &entry), status == ARCHIVE_OK) {
		const char * const sourcepath = archive_entry_sourcepath(entry);

		archive_entry_copy_pathname(entry, sourcepath + inputlen);

		status = archive_write_header(out, entry);
		if (status != ARCHIVE_OK) {
			errx(EXIT_FAILURE, "archive_write_header: %s", archive_error_string(out));
		}

		if (archive_read_disk_can_descend(in)) {
			status = archive_read_disk_descend(in);
			if (status != ARCHIVE_OK) {
				errx(EXIT_FAILURE, "archive_read_disk_descend: %s", archive_error_string(in));
			}
		} else {
			lndworm_archive_copy_from_disk(sourcepath, out);
		}
	}

	archive_read_close(in);
	archive_write_close(out);

	if (status != ARCHIVE_EOF) {
		errx(EXIT_FAILURE, "archive_read_next_header: %s", archive_error_string(in));
	}

	archive_read_free(in);
	archive_write_free(out);
}

noreturn static void
lndworm_exec(const struct lndworm_args *args,
	int argc, char **argv, const char *output, int fd) {
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

	/* Open or describe srcdir. */
	int srcfd;
	if (!isdir(args->src)) {
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
		extract("/var/sysroot", !args->rwsysroot, sysrootfd);
	}

	/* Extract src archive if not mounted directory. */
	if (description.srcdir == NULL) {
		extract("/var/src", !args->rwsrcdir, srcfd);
	}

	const pid_t pid = fork();
	if (pid < 0) {
		err(EXIT_FAILURE, "fork");
	}

	if (pid == 0) {
		bsysexec(bsysname, argv + optind, argc - optind);
	}

	if (lndworm_wait(pid) != 0) {
		exit(EXIT_FAILURE);
	}

	lndworm_archive_create(args->pkgobj ? "/var/obj" : "/var/dest", args->format, args->filter, output, fd);
	exit(EXIT_SUCCESS);
}

static int
lndworm_run(const struct lndworm_args *args,
	int argc, char **argv, const char *output, int fd) {

	const pid_t pid = fork();
	if (pid < 0) {
		warn("fork");
		return -1;
	}

	if (pid == 0) {
		lndworm_exec(args, argc, argv, output, fd);
	}

	if (lndworm_wait(pid) != 0) {
		return -1;
	}

	return 0;
}

noreturn static void
lndworm_usage(const char *progname, int status) {

	fprintf(stderr,
		"usage: %1$s [-ASUr] [-a <output archive format> [-f <output compression filter>]]"
			" [-t <toolchain>] [-b <bsys>] [-u <sysroot>] [-s <src>] <output> [<arguments>...]\n"
		"       %1$s -h\n",
		progname);

	exit(status);
}

static struct lndworm_args
lndworm_parse_args(int argc, char **argv) {
	struct lndworm_args args = {
		.toolchain = getenv("ORM_DEFAULT_TOOLCHAIN"),
		.bsys = getenv("ORM_DEFAULT_BSYS"),
		.sysroot = getenv("ORM_SYSROOT"),
	};
	int c;

	while ((c = getopt(argc, argv, ":hASUra:f:t:b:u:s:")) >= 0) {
		switch (c) {
		case 'h': lndworm_usage(*argv, EXIT_SUCCESS);
		case 'A': args.pkgobj = 1; break;
		case 'S': args.rwsrcdir = 1; break;
		case 'U': args.rwsysroot = 1; break;
		case 'r': args.asroot = 1; break;
		case 'a': args.format = optarg; break;
		case 'f': args.filter = optarg; break;
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

	if (args.filter != NULL && args.format == NULL) {
		warnx("Cannot specify filter without format");
		lndworm_usage(*argv, EXIT_FAILURE);
	}

	if (args.sysroot == NULL) {
		args.sysroot = "/";
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
	const char * const output = argv[optind++];
	int fd;

	/* Avoid interactivity in all subsequent processes,
	 * no need for dup2 here as STDIN_FILENO is always zero. */
	close(STDIN_FILENO);
	if (open("/dev/null", O_RDONLY) < 0) {
		err(EXIT_FAILURE, "open /dev/null");
	}

	/* Open the output file now, as later on, processes will
	 * be in the sandbox, and won't be able to access the host's filesystem. */
	fd = open(output, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0) {
		err(EXIT_FAILURE, "open '%s'", output);
	}

	if (lndworm_run(&args, argc, argv, output, fd) != 0) {
		/* In case of error, we must cleanup the created package,
		 * which means the toplevel parent process must never
		 * enter the sandbox, and be resilient to errors. */
		unlink(output);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
