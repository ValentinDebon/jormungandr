#ifndef ORM_H
#define ORM_H

#include <sys/types.h> /* gid_t, uid_t */

struct orm_sandbox_description {
	const char *root;
	const char *sysroot, *destdir, *objdir, *srcdir;
	unsigned int asroot : 1, rosysroot : 1, rosrcdir : 1;
	size_t tmpsz;
};

int
orm_sandbox(const struct orm_sandbox_description *description, uid_t olduid, gid_t oldgid);

/* ORM_H */
#endif
