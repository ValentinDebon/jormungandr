#include <orm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mount.h>
#include <limits.h>
#include <unistd.h>
#include <alloca.h>
#include <sched.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>

static int
remount_bind_path(const char *path, const char *src, unsigned long flags) {

	if (mount(src, path, "", MS_REC | MS_BIND, NULL) != 0) {
		return -1;
	}

	if (!!(flags & MS_RDONLY)) {
		if (mount("", path, "", MS_PRIVATE, NULL) != 0) {
			return -1;
		}

		if (mount("", path, "", MS_REMOUNT | MS_RDONLY | MS_BIND, NULL) != 0) {
			return -1;
		}
	}

	return 0;
}

static inline int
remount_bind(const char *root, const char *dst, const char *src, unsigned long flags) {
	const size_t rootlen = strlen(root), dstlen = strlen(dst);
	char path[rootlen + dstlen + 1];

	memcpy(mempcpy(path, root, rootlen), dst, dstlen + 1);

	return remount_bind_path(path, src, flags);
}

static int
mount_workdir(const char *root, const char *dst, const char *src, const void *tmpfsdata, unsigned long flags) {
	const size_t rootlen = strlen(root), dstlen = strlen(dst);
	char path[rootlen + dstlen + 1];

	memcpy(mempcpy(path, root, rootlen), dst, dstlen + 1);

	if (src != NULL) {
		return remount_bind_path(path, src, flags);
	}

	if (!(flags & MS_RDONLY) && mount("tmpfs", path, "tmpfs", MS_NOSUID | MS_NODEV, tmpfsdata) != 0) {
		return -1;
	}

	return 0;
}

static int
procfs_write_buffer(const char *path, const char *buffer, size_t length) {
	int fd, ret;

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		return -1;
	}

	ret = -!(write(fd, buffer, length) == length);
	close(fd);

	return ret;
}

static int
procfs_write(const char *path, const char *string) {
	return procfs_write_buffer(path, string, strlen(string));
}

static int
procfs_id_map(const char *path, id_t old, id_t new) {
	char line[3 * 2 * sizeof (id_t) + 7];
	int length;

	length = snprintf(line, sizeof (line), "%d %d 1\n", new, old);
	if (length < 0) {
		return -1;
	}

	return procfs_write_buffer(path, line, length);
}

static int
passwd_setup(uid_t uid, gid_t gid) {
	FILE * const fp = fopen("/etc/passwd", "r");
	struct passwd pwbuf, *pw;
	char buf[2048];
	int ret;

	while (ret = fgetpwent_r(fp, &pwbuf, buf, sizeof (buf), &pw),
		ret == 0 && pw != NULL && pw->pw_uid != uid);

	fclose(fp);

	if (ret != 0) {
		errno = ret;
		return -1;
	}

	if (pw != NULL) {
		if (setenv("HOME", pw->pw_dir, 1) != 0) {
			return -1;
		}

		if (setenv("SHELL", pw->pw_shell, 1) != 0) {
			return -1;
		}

		if (setenv("LOGNAME", pw->pw_name, 1) != 0) {
			return -1;
		}

		if (setenv("USER", pw->pw_name, 1) != 0) {
			return -1;
		}
	}

	return ret;
}

int
orm_sandbox(const struct orm_sandbox_description *description, uid_t olduid, gid_t oldgid) {
	const void *tmpfsdata;

	if (description->tmpsz != 0) {
		static const char format[] = "size=%zu";
		const size_t size = sizeof (format) + sizeof (description->tmpsz) * 3;
		char * const str = alloca(size);
		snprintf(str, size, format, description->tmpsz);
		tmpfsdata = str;
	} else {
		tmpfsdata = NULL;
	}

	/* Create new user namespace
	 * New NS requires a NEWUSER if we want new user capabilties (CAP_SYS_ADMIN...) in the new filesystem.
	 * To be authorized to use procfs fstype in mount, we need a new PID namespace,
	 * which is only really applied when creating new processes. */
	if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
		return -1;
	}

	/* Remount toolchain root read-only. */
	if (remount_bind(description->root, "/", description->root, MS_RDONLY) != 0) {
		return -1;
	}

	/* Mount-bind host system's directories. */
	if (remount_bind(description->root, "/dev", "/dev", 0) != 0) {
		return -1;
	}

	if (remount_bind(description->root, "/proc", "/proc", 0) != 0) {
		return -1;
	}

	if (remount_bind(description->root, "/sys", "/sys", 0) != 0) {
		return -1;
	}

	/* Mount description's directories. */
	if (mount_workdir(description->root, "/var/sysroot", description->sysroot, tmpfsdata, description->rosysroot ? MS_RDONLY : 0) != 0) {
		return -1;
	}

	if (mount_workdir(description->root, "/var/bsys", description->bsysdir, tmpfsdata, MS_RDONLY) != 0) {
		return -1;
	}

	if (mount_workdir(description->root, "/var/dest", description->destdir, tmpfsdata, 0) != 0) {
		return -1;
	}

	if (mount_workdir(description->root, "/var/obj", description->objdir, tmpfsdata, 0) != 0) {
		return -1;
	}

	if (mount_workdir(description->root, "/var/src", description->srcdir, tmpfsdata, description->rosrcdir ? MS_RDONLY : 0) != 0) {
		return -1;
	}

	/* Enter toolbox filesystem. */
	if (chroot(description->root) != 0) {
		return -1;
	}

	/* Mount temporary files volatile. */
	if (mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, tmpfsdata) != 0) {
		return -1;
	}

	/* Map user and group ids. */
	id_t newuid, newgid;

	if (description->asroot) {
		newuid = 0;
		newgid = 0;
	} else {
		newuid = 100;
		newgid = 100;
	}

	if (procfs_id_map("/proc/self/uid_map", olduid, newuid) != 0) {
		return -1;
	}

	/* Only a process with CAP_SETGID in the parent namespace is allowed
	 * to maintain setgroups to "allow" in a new namespace, "deny" setgroups from now on. */
	if (procfs_write("/proc/self/setgroups", "deny") != 0) {
		return -1;
	}

	if (procfs_id_map("/proc/self/gid_map", oldgid, newgid) != 0) {
		return -1;
	}

	/* Setup environment variables. */
	if (clearenv() != 0) {
		return -1;
	}

	if (putenv("PATH=/usr/bin:/usr/sbin") != 0) {
		return -1;
	}

	if (passwd_setup(newuid, newgid) != 0) {
		return -1;
	}

	/* Change working directory to either the new user's home directory or /. */
	const char *workdir = getenv("HOME");

	if (workdir == NULL) {
		workdir = "/";
	}

	if (chdir(workdir) != 0) {
		return -1;
	}

	return 0;
}
