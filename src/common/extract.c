/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "extract.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <err.h>

#include <archive.h>
#include <archive_entry.h>

static void
archive_copy_to_disk(struct archive *in, struct archive *out) {
	const void *buffer;
	size_t size;
	off_t off;

	int status;
	while (status = archive_read_data_block(in, &buffer, &size, &off), status == ARCHIVE_OK) {
		status = archive_write_data_block(out, buffer, size, off);
		if (status != ARCHIVE_OK) {
			errx(EXIT_FAILURE, "archive_write_data_block: %s", archive_error_string(out));
		}
	}

	if (status != ARCHIVE_EOF) {
		errx(EXIT_FAILURE, "archive_read_data_block: %s", archive_error_string(in));
	}
}

void
extract(const char *output, unsigned int ro, int fd) {
	struct archive * const out = archive_write_disk_new(), * const in = archive_read_new();

	if (archive_write_disk_set_options(out, ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME) != ARCHIVE_OK) {
		errx(EXIT_FAILURE, "archive_write_disk_set_options: %s", archive_error_string(out));
	}

	if (archive_write_disk_set_standard_lookup(out) != ARCHIVE_OK) {
		errx(EXIT_FAILURE, "archive_write_disk_set_standard_lookup: %s", archive_error_string(out));
	}

	if (archive_read_support_filter_all(in) != ARCHIVE_OK) {
		errx(EXIT_FAILURE, "archive_read_support_filter_all: %s", archive_error_string(in));
	}

	if (archive_read_support_format_all(in) != ARCHIVE_OK) {
		errx(EXIT_FAILURE, "archive_read_support_format_all: %s", archive_error_string(in));
	}

	if (archive_read_open_fd(in, fd, CONFIG_ARCHIVE_INPUT_BLOCK_SIZE) != ARCHIVE_OK) {
		errx(EXIT_FAILURE, "archive_read_open_fd: %s", archive_error_string(in));
	}

	/* Remove FD_CLOEXEC from fd. */
	if (fcntl(fd, F_SETFD, 0) != 0) {
		err(EXIT_FAILURE, "fcntl F_SETFD");
	}

	int status;
	struct archive_entry *entry;
	const size_t outputlen = strlen(output);
	while (status = archive_read_next_header(in, &entry), status == ARCHIVE_OK) {
		const char *sourcepath = archive_entry_pathname(entry);
		while (*sourcepath == '/') {
			sourcepath++;
		}
		const size_t sourcepathlen = strlen(sourcepath);

		char pathname[outputlen + 1 + sourcepathlen + 1];
		*(char *)mempcpy(pathname, output, outputlen) = '/';
		memcpy(pathname + outputlen + 1, sourcepath, sourcepathlen + 1);

		archive_entry_copy_pathname(entry, pathname);

		status = archive_write_header(out, entry);
		if (status != ARCHIVE_OK) {
			errx(EXIT_FAILURE, "archive_write_header: %s", archive_error_string(out));
		}

		archive_copy_to_disk(in, out);
	}

	archive_read_close(in);
	archive_write_close(out);

	if (status != ARCHIVE_EOF) {
		errx(EXIT_FAILURE, "archive_read_next_header: %s", archive_error_string(in));
	}

	archive_read_free(in);
	archive_write_free(out);

	if (ro && mount("", output, "", MS_REMOUNT | MS_RDONLY | MS_BIND, NULL) != 0) {
		err(EXIT_FAILURE, "mount '%s' ro", output);
	}

	close(fd);
}
