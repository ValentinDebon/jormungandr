/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "bsysexec.h"

#include <stdlib.h> /* alloca, EXIT_FAILURE */
#include <string.h> /* strlen, memcpy */
#include <unistd.h> /* execv */
#include <err.h> /* err */

noreturn void
bsysexec(const char *bsysname, char **args, int count) {
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
