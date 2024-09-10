# NAME

gitworm - Jormungandr project packaging sandboxing for git repositories.

# SYNOPSIS

- **gitworm** [-SUr] [-C \<path\>] [-t \<toolchain\>] [-b \<bsys\>] [-u \<sysroot\>] \<tree-ish\> [\<arguments\>...]
- **gitworm** -h

# DESCRIPTION

Execute a build system driver script (bsys) inside a sandbox dedicated for source code builds.

In opposition to orm(1), which is meant to be used for iterative
builds and fast iterations, gitworm(1)'s purpose is automation and testing.

It allows you to directly extract the git repository located at **path**
(or, by default, the current working directoty) and **sysroot** in the sandbox from package archives.
The archives are atomically extracted in the sandbox, when the sandbox disappears with the process,
all extracted files are automatically destroyed, making the process more resilient than manual removals.

It relies on git-archive(1) and libarchive(3) for the extractions processes.
As the repository image extraction process happens in the sandbox, if libarchive resorts to external executables,
then it will use the sandbox's binary executables, so be aware of support for both system and sandbox.

# OPTIONS

- -S : By default, the **srcdir** is mounted read-only in the sandbox. But if the build system doesn't support out-of-tree builds, this option mounts it with write access.
       Note that most modern build systems do support out-of-tree builds, and you should create your driver to support this. This option is only available as a last resort.
- -U : By default, the **sysroot** is mounted read-only in the sandbox. If you are creating a system image on-the-fly,
       you might want to directly export some libraries and headers. This option allows mounting **sysroot** with write access.
- -r : Usurpate 0:0 (root's) user and group id instead of the default 1000:1000 credentials in the sandbox.
- -C \<path\> : Specify the **path** to the git repository to extract as the source directory. Current working directory by default.
- -t \<toolchain\> : Specify the **toolchain** mounted read-only as the sandbox root directory.
- -b \<bsys\> : Specify the **bsys** made available read-only in **/var/bsys** and executed if not interactive (no **-i**).
- -u \<sysroot\> : Specify the **sysroot**, for the compiler to link against live dynamic libraries. Read-only by default.
- -s \<srcdir\> : Specify the **srcdir**, source code directory. Read-only by default.
- \<tree-ish\> : Argument forwarded to git-archive(1), tree or commit to extract.
- \<arguments\> : Arguments forwarded to the **bsys**

# ENVIRONMENT

- GIT\_EXEC\_PATH : Path to wherever your core Git programs are installed.
- ORM\_DEFAULT\_TOOLCHAIN : Set the default **toolchain** to use.
- ORM\_DEFAULT\_BSYS : Set the default **bsys** to use.
- ORM\_SYSROOT : Set the default **sysroot** to mount.

# AUTHOR
Valentin Debon (valentin.debon@heylelos.org)

# SEE ALSO
orm(1), lndworm(1), jormungandr(7).
