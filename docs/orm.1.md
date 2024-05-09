# NAME

orm - Jormungandr project building sandboxing.

# SYNOPSIS

- **orm** [-PSUir] [-t \<toolchain\>] [-b \<bsys\>] [-w \<workspace\>] [-u \<sysroot\>] [-d \<destdir\>] [-o \<objdir\>] [-s \<srcdir\>] [\<arguments\>...]
- **orm** [-P] [-w \<workspace\>] [-s \<srcdir\>] -p \<workdir\>
- **orm** -h

# DESCRIPTION

Execute a build system driver script (bsys) inside a sandbox dedicated for source code builds.

# OPTIONS

- -P : Use persistent build directories, by default, orm's workdirs are located in the user's session runtime directory (based on **$XDG_RUNTIME_DIR**).
       When this option is set, orm uses the user's cache directory (based on **$XDG_CACHE_HOME**) to create its default **destdir** and **objdir**.
- -S : By default, the **srcdir** is mounted read-only in the sandbox. But if the build system doesn't support out-of-tree builds, this option mounts it with write access.
       Note that most modern build systems do support out-of-tree builds, and you should create your driver to support this. This option is only available as a last resort.
- -U : By default, the **sysroot** is mounted read-only in the sandbox. If you are creating a system image on-the-fly,
       you might want to directly export some libraries and headers. This option allows mounting **sysroot** with write access.
- -i : Start an interactive shell in the sandbox, for manual experimentation, debug, etc...
- -r : Usurpate 0:0 (root's) user and group id instead of the default 1000:1000 credentials in the sandbox.
- -t \<toolchain\> : Specify the **toolchain** mounted read-only as the sandbox root directory.
- -b \<bsys\> : Specify the **bsys** made available read-only in **/var/bsys** and executed if not interactive (no **-i**).
- -w \<workspace\> : Specify the **workspace** in which orm's workdirs are created, if none specified, it is inferred from **srcdir** path.
- -u \<sysroot\> : Specify the **sysroot**, for the compiler to link against live dynamic libraries. Read-only by default.
- -d \<destdir\> : Specify the **destdir**, where files should be staged for packaging. Never read-only.
- -o \<objdir\> : Specify the **objdir**, where object files and other build artifacts are created.
- -s \<srcdir\> : Specify the **srcdir**, source code directory. Read-only by default.
- \<arguments\> : Arguments forwarded to the **bsys**, or the shell if interactive (**-i**).
- -p \<workdir\> : In this scenario, prints the absolute path of the given **workdir** associated with the current **workspace**/**srcdir** (and persistence if **-P**).

# ENVIRONMENT

- ORM\_SRCDIR\_COMMAND : Set the default **srcdir** resolution command.
- ORM\_DEFAULT\_TOOLCHAIN : Set the default **toolchain** to use.
- ORM\_DEFAULT\_BSYS : Set the default **bsys** to use.
- ORM\_WORKSPACE : Set the default **workspace** name to use.
- ORM\_SYSROOT : Set the default **sysroot** to mount.

# AUTHOR
Valentin Debon (valentin.debon@heylelos.org)

# SEE ALSO
lndworm(1), jormungandr(7).
