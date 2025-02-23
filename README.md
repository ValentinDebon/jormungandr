# Jormungandr

Jormungandr is a set of containerization tools for source development.
Its main purpose is to isolate source code compilation, analysis, patching, etc...
And eases automation of said isolated tasks.

Jormungandr's sandboxes help you ascertain that your build system does not
modify your source code during the build sequence by making it read-only.
By performing builds mostly in-memory, it also increases performances
and reduces disks usage and thus aging.

## Why?

Jormungandr was primarily invented to easily perform one-time builds.
However, it quickly seemed it could be extended as a set of tools for
various development purposes, from fast one-time-session builds
to continuous integration.

It was meant to be usable in a UNIX-like development environment.
A root-less, simple tool with simple semantics, few side effects, and reproducibility.

## Build

Jormungandr is made to work on **GNU/Linux** systems,
depending on Linux-specific namespaces for sandboxing.

First, install its dependencies, on a Debian-based distribution:
```sh
sudo apt install libarchive-dev
```

Then, configure, build and install:
```sh
./configure
make install
```

By default, no **bsys** and no **toolchain** are installed.
Jormungandr **does not yet provide toolchains**, you must manually either tailor
your system to be suitable as a toolchain (cf. [Toolchains Requirements](docs/toolchains-requirements.md)),
or create one yourself (compile one or create one with a Docker and run jormungandr inside said Docker).

At runtime, `git` is required for some features.
See the documentation and manual pages for more informations.

## Copying

Jormungandr sources, binaries and documentations are distributed under the Affero GNU Public License version 3.0, see LICENSE.
