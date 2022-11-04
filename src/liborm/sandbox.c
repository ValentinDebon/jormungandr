#include <orm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mount.h>
#include <unistd.h>
#include <alloca.h>
#include <sched.h>
#include <fcntl.h>

static int
remount_bind(const char *src, const char *dst, const char *root, size_t rootlen, bool rdonly) {
	const size_t dstlen = strlen(dst);
	char path[rootlen + dstlen + 1];

	memcpy(path, root, rootlen);
	memcpy(path + rootlen, dst, dstlen + 1);

	if (mount(src, path, "", MS_REC | MS_BIND, NULL) != 0) {
		return -1;
	}

	if (rdonly) {
		if (mount("", path, "", MS_PRIVATE, NULL) != 0) {
			return -1;
		}

		if (mount("", path, "", MS_REMOUNT | MS_RDONLY | MS_BIND, NULL) != 0) {
			return -1;
		}
	}

	return 0;
}

static int
mount_workdir(const char *src, const char *dst, const char *root, size_t rootlen, const void *tmpfsdata, bool rdonly) {

	if (src != NULL) {
		return remount_bind(src, dst, root, rootlen, rdonly);
	}

	if (!rdonly) {
		const size_t dstlen = strlen(dst);
		char path[rootlen + dstlen + 1];

		memcpy(path, root, rootlen);
		memcpy(path + rootlen, dst, dstlen + 1);

		if (mount("tmpfs", path, "tmpfs", MS_NOSUID | MS_NODEV, tmpfsdata) != 0) {
			return -1;
		}
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

int
orm_sandbox(const struct orm_sandbox_description *description, uid_t olduid, gid_t oldgid) {
	const size_t rootlen = strlen(description->root);
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
	 * New NS requires a NEWUSER if we want new user capabilties (CAP_SYS_ADMIN...) in the new filesystem,
	 * then to be authorized to use procfs fstype in mount, we need a new PID namespace, which is only really applied when creating new processes.
	 */
	if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
		return -1;
	}

	/* Remount toolroot read-only */
	if (remount_bind(description->root, "/", description->root, rootlen, 1) != 0) {
		return -1;
	}

	/* Mount-bind host system's directories */
	if (remount_bind("/dev", "/dev", description->root, rootlen, 0) != 0) {
		return -1;
	}

	if (remount_bind("/proc", "/proc", description->root, rootlen, 0) != 0) {
		return -1;
	}

	if (remount_bind("/sys", "/sys", description->root, rootlen, 0) != 0) {
		return -1;
	}

	/* Mount description's directories */
	if (mount_workdir(description->sysroot, "/var/sysroot", description->root, rootlen, tmpfsdata, description->rosysroot) != 0) {
		return -1;
	}

	if (mount_workdir(description->destdir, "/var/dest", description->root, rootlen, tmpfsdata, 0) != 0) {
		return -1;
	}

	if (mount_workdir(description->objdir, "/var/obj", description->root, rootlen, tmpfsdata, 0) != 0) {
		return -1;
	}

	if (mount_workdir(description->srcdir, "/var/src", description->root, rootlen, tmpfsdata, description->rosrcdir) != 0) {
		return -1;
	}

	/* We don't need any host system file, change root */
	if (chroot(description->root) != 0) {
		return -1;
	}

	/* Mount temporary files volatile */
	if (mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, tmpfsdata) != 0) {
		return -1;
	}

	/* Map user and group ids */
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

	/* Finalize changes */
	if (clearenv() != 0) {
		return -1;
	}

	if (putenv("HOME=/var/home") != 0
		|| putenv("PATH=/usr/bin:/usr/sbin") != 0
		|| (description->asroot
			? (putenv("LOGNAME=root") != 0 || putenv("USER=root"))
			: (putenv("LOGNAME=user") != 0 || putenv("USER=user")))) {
		return -1;
	}

	if (chdir("/var/home") != 0) {
		return -1;
	}

	return 0;
}
