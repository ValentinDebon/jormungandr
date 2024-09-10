/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "isdir.h"

#include <stdlib.h> /* EXIT_FAILURE */
#include <sys/stat.h> /* stat */
#include <err.h> /* err */

bool
isdir(const char *path) {
	struct stat st;

	if (stat(path, &st) != 0) {
		err(EXIT_FAILURE, "stat '%s'", path);
	}

	return (st.st_mode & S_IFMT) == S_IFDIR;
}
