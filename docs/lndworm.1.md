# NAME

lndworm - Jormungandr project packaging sandboxing.

# SYNOPSIS

- **lndworm** [-ASUr] [-a \<output archive format\> [-f \<output compression filter\>]] [-t \<toolchain\>] [-b \<bsys\>] [-u \<sysroot\>] [-s \<src\>] \<output\> [\<arguments\>...]
- **lndworm** -h

# DESCRIPTION

Execute a build system driver script (bsys) inside a sandbox dedicated for source code builds.

In opposition to orm(1), which is meant to be used for iterative
builds and fast iterations, lndworm(1)'s purpose is automation and packaging.

It allows you to directly extract **src** and **sysroot** in the sandbox from package archives,
and outputs a newly created archive with either the sandbox's **destdir** or **objdir**.
The archives are atomically extracted in the sandbox, when the sandbox disappears with the process,
all extracted files are automatically destroyed, making the process more resilient than manual removals.

As it relies on the host system's libarchive(3) for the extractions and creation processes,
it supports all archive formats and filters supported by your host system's library version.
Note some libarchive formats/filters depend on binary executables installed on the machine.
As the extraction process happens in the sandbox, if libarchive resorts to external executables,
then it will use the sandbox's binary executables, so be aware of support for both system and sandbox.

# OPTIONS

- -A : By default, **destdir** is used to create the output package archive, if this option is specified, use **objdir** instead.
- -S : By default, the **src** is mounted read-only in the sandbox. But if the build system doesn't support out-of-tree builds, this option mounts it with write access.
       Note that most modern build systems do support out-of-tree builds, and you should create your driver to support this. This option is only available as a last resort.
- -U : By default, the **sysroot** is mounted read-only in the sandbox. If you are creating a system image on-the-fly,
       you might want to directly export some libraries and headers. This option allows mounting **sysroot** with write access.
- -r : Usurpate 0:0 (root's) user and group id instead of the default 1000:1000 credentials in the sandbox.
- -a : Override output archive format detection from the output file name. Allows more fine-grained format specifications, see libarchive(3) for supported values.
- -f : If **-a** was specified, used to specify the desired filter used in combination with the specified format, see libarchive(3) for supported values.
- -t \<toolchain\> : Specify the **toolchain** mounted read-only as the sandbox root directory.
- -b \<bsys\> : Specify the **bsys** made available read-only in **/var/bsys** and executed if not interactive (no **-i**).
- -u \<sysroot\> : Specify the **sysroot**, a directory or an archive, for the compiler to link against live dynamic libraries. Read-only by default.
- -s \<src\> : Specify the **srcdir**, a source code directory or archive. Read-only by default.
- \<arguments\> : Arguments forwarded to the **bsys**

# ENVIRONMENT

- ORM\_SRCDIR\_COMMAND : Set the default **src** resolution command.
- ORM\_DEFAULT\_TOOLCHAIN : Set the default **toolchain** to use.
- ORM\_DEFAULT\_BSYS : Set the default **bsys** to use.
- ORM\_SYSROOT : Set the default **sysroot** to mount.

# AUTHOR
Valentin Debon (valentin.debon@heylelos.org)

# SEE ALSO
orm(1), gitworm(1), jormungandr(7).
