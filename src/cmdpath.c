#include "cmdpath.h"

#include <stdio.h> /* popen, ... */
#include <stdlib.h> /* malloc, realpath */
#include <string.h> /* strdupa */
#include <limits.h> /* PATH_MAX */
#include <ctype.h> /* isspace */
#include <errno.h> /* ENOENT */

/**
 * Run a command and returns the returned path in a string.
 * @param command Command to run using popen(3).
 * @return On success, the absolute path of the source directory,
 *   must be free(3)'d. On error, NULL, setting errno appropriately.
 */
char *
cmdpath(const char *command) {
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
