/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef COMMON_BSYSEXEC_H
#define COMMON_BSYSEXEC_H

#include <stdnoreturn.h> /* noreturn */

noreturn extern void bsysexec(const char *bsysname, char **args, int count);

/* COMMON_BSYSEXEC_H */
#endif
