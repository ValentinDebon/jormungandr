/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "extract.h"

#include <stdlib.h> /* EXIT_FAILURE */
#include <string.h> /* strlen, memcpy, ... */
#include <sys/mount.h> /* mount, ... */
#include <err.h> /* err */

#include <archive.h>
#include <archive_entry.h>

void
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
extract_prepare(int fd, struct archive **outp, struct archive **inp) {
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

	*outp = out;
	*inp = in;
}

void
extract_finish(const char *output, unsigned int ro, int fd,
	int status, struct archive *out, struct archive *in) {

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

void
extract(const char *output, unsigned int ro, int fd) {
	struct archive *out, *in;

	extract_prepare(fd, &out, &in);

	int status;
	struct archive_entry *entry;
	const size_t outputlen = strlen(output);
	while (status = archive_read_next_header(in, &entry), status == ARCHIVE_OK) {
		const char *inpathname = archive_entry_pathname(entry);
		while (*inpathname == '/') {
			inpathname++;
		}
		const size_t inpathnamelen = strlen(inpathname);

		char outpathname[outputlen + 1 + inpathnamelen + 1];
		*(char *)mempcpy(outpathname, output, outputlen) = '/';
		memcpy(outpathname + outputlen + 1, inpathname, inpathnamelen + 1);

		archive_entry_copy_pathname(entry, outpathname);

		status = archive_write_header(out, entry);
		if (status != ARCHIVE_OK) {
			errx(EXIT_FAILURE, "archive_write_header: %s", archive_error_string(out));
		}

		archive_copy_to_disk(in, out);
	}

	extract_finish(output, ro, fd, status, out, in);
}
