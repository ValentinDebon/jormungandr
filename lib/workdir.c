/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <orm.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static int
orm_mkdirs(char *path) {

	while (mkdir(path, 0700) != 0) {
		char *separator;

		if (errno == EEXIST) {
			break;
		}

		if (errno != ENOENT) {
			return -1;
		}

		separator = strrchr(path, '/');
		*separator = '\0';

		if (orm_mkdirs(path) != 0) {
			return -1;
		}

		*separator = '/';
	}

	return 0;
}

int
orm_workdir(const char *workspace, const char *name, int flags, char **pathp) {
	char *cache, *path;

	if (*workspace == '\0' || *workspace == '.' || strchr(workspace, '/') != NULL) {
		errno = EINVAL;
		return -1;
	}

	if (flags & ORM_WORKDIR_PERSISTENT) {
		cache = getenv("XDG_CACHE_HOME");
		if (cache == NULL || *cache != '/') {
			static const char cachedefault[] = "/.cache";
			char * const home = getenv("HOME");

			if (home == NULL || *home != '/') {
				errno = EINVAL;
				return -1;
			}

			const size_t homelen = strlen(home);
			cache = alloca(homelen + sizeof (cachedefault));
			memcpy(mempcpy(cache, home, homelen), cachedefault, sizeof (cachedefault));
		}
	} else {
		cache = getenv("XDG_RUNTIME_DIR");
		if (cache == NULL || *cache != '/') {
			errno = EINVAL;
			return -1;
		}
	}

	const size_t cachelen = strlen(cache),
		workspacelen = strlen(workspace),
		namelen = strlen(name);
	static const char prefix[] = "/jormungandr/";
	char buffer[cachelen + sizeof (prefix) + workspacelen + 1 + namelen];

	path = mempcpy(buffer, cache, cachelen);
	path = mempcpy(path, prefix, sizeof (prefix) - 1);
	path = mempcpy(path, workspace, workspacelen);
	*path++ = '/';
	memcpy(path, name, namelen + 1);

	if (orm_mkdirs(buffer) != 0) {
		return -1;
	}

	path = realpath(buffer, NULL);
	if (path == NULL) {
		return -1;
	}

	*pathp = path;

	return 0;
}
