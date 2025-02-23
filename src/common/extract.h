/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef COMMON_EXTRACT_H
#define COMMON_EXTRACT_H

struct archive;

extern void archive_copy_to_disk(struct archive *in, struct archive *out);

extern void extract_prepare(int fd, struct archive **outp, struct archive **inp);

extern void extract_finish(const char *output, unsigned int ro, int fd, int status, struct archive *out, struct archive *in);

extern void extract(const char *output, unsigned int ro, int fd);

/* COMMON_EXTRACT_H */
#endif
