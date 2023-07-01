# Jormungandr

Jormungandr is a set of sandboxing and containerization tools for source development.
Its main purpose is to isolate source code compilation, analysis, patching, etc...
And ease automation of said isolated tasks.

Sandboxes created by jormungandr do not have a security role,
only isolation and resource management. Sandboxes revolve around the following concepts:
- The **toolchain**, is the mounted sandbox system's root directory.
- The **sysroot**, is the host system's root directory.
- The **destdir**, is where staged-installation should be performed.
- The **objdir**, is where artifacts should be produced.
- The **srcdir**, is the source code directory.

The **toolchain**, is always mounted read-only,
and should follow the toolchains' requirements (TBD).
The **sysroot** is by default mounted read-only but can be mutable.
The **destdir** and **objdir** are always mounted read/write.
The **srcdir** is by default mounted read-only and should nearly NEVER be mounted read/write.

For example, jormungandr's sandboxes help you accertain that your build system does not
modify your source code during the build sequence by making it read-only.
Which is always a good programming practice.

Jormungandr sources and binaries are distributed under the Affero GNU Public License version 3.0, see LICENSE.
