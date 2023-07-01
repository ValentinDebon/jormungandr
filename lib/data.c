/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <orm.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

static int
orm_data_access(const char *datadir, const char *location, size_t locationsize, char **pathp) {
	const size_t datadirlen = strlen(datadir);
	char path[datadirlen + locationsize], *real;

	memcpy(mempcpy(path, datadir, datadirlen), location, locationsize);

	real = realpath(path, NULL);
	if (real == NULL) {
		return -1;
	}

	*pathp = real;

	return 0;
}

static int
orm_data_path(const char *prefix, const char *name, char **pathp) {

	if (*name == '\0' || *name == '.' || strchr(name, '/') != NULL) {
		errno = EINVAL;
		return -1;
	}

	const size_t prefixlen = strlen(prefix), namelen = strlen(name);
	char location[prefixlen + namelen + 1];
	memcpy(mempcpy(location, prefix, prefixlen), name, namelen + 1);

	char *datadir = getenv("XDG_DATA_HOME");
	if (datadir == NULL || *datadir != '/') {
		static const char datadefault[] = "/.local/share";
		char * const home = getenv("HOME");

		if (home == NULL || *home != '/') {
			errno = EINVAL;
			return -1;
		}

		const size_t homelen = strlen(home);
		datadir = alloca(homelen + sizeof (datadefault));
		memcpy(mempcpy(datadir, home, homelen), datadefault, sizeof (datadefault));
	}

	char *path = NULL;
	if (orm_data_access(datadir, location, sizeof (location), &path) != 0) {
		char *dirs = getenv("XDG_DATA_DIRS");

		if (dirs == NULL) {
			dirs = strdupa("/usr/local/share/:/usr/share/");
		} else {
			dirs = strdupa(dirs);
		}

		while (datadir = strsep(&dirs, ":"), datadir != NULL
			&& orm_data_access(datadir, location, sizeof (location), &path) != 0);
	}

	if (path == NULL) {
		return -1;
	}

	*pathp = path;

	return 0;
}

int
orm_bsys_path(const char *bsys, char **pathp) {
	return orm_data_path("/jormungandr/bsys/", bsys, pathp);
}

int
orm_toolchain_path(const char *toolchain, char **pathp) {
	return orm_data_path("/jormungandr/toolchain/", toolchain, pathp);
}
