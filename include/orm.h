#ifndef ORM_H
#define ORM_H

#include <sys/types.h>

struct orm_sandbox_description {
	const char *root;
	const char *sysroot, *destdir, *objdir, *srcdir;
	unsigned int asroot : 1, rosysroot : 1, rosrcdir : 1;
	size_t tmpsz;
};

extern int orm_sandbox(const struct orm_sandbox_description *description, uid_t olduid, gid_t oldgid);

extern int orm_toolchain_path(const char *toolchain, char **pathp);
extern int orm_bsys_path(const char *bsys, char **pathp);

/* ORM_H */
#endif
