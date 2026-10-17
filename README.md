# QSapecNG

> **Note:** this README is for a fork of QSapecNG. Release 3.0.0 is a work in progress.

QSapecNG v3.0.0  
Copyright (C) 2009, Michele Caini  
Copyright (C) 2026, Wojciech Kaczmarski  
GNU General Public License v3

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY.


## What is it?

QSapecNG is a Qt-based program for symbolic analysis of linear analog circuits.
In fact, it consists of two independent parts: the SapecNG framework engine
and the GUI application QSapecNG.

It is an open source, multi-platform project, continuously enhanced by students
and researchers of the Department of Electronics and Telecommunications (DET) of
the University of Florence. It comes as a continuation of SapWin for Windows, in
order to give the project full compatibility with other platforms.

Through SapecNG/QSapecNG users can draw, solve and analyze analog circuits as
well as manage them. Since version 2.0.0~beta1 there is also full support for
sub-circuits that can be created, saved, reopened and added as part of other
circuits.

SapecNG/QSapecNG is freely available on [SourceForge.net](https://sourceforge.net/projects/qsapecng/).
A previous version of SapecNG is available too, on [SourceForge.net](https://sourceforge.net/projects/sapec-ng/).
Pre-compiled packages will also be available from the projects' pages.


## Quick start

1. Draw the circuit: pick parts in the Components list (or with their keys),
   W draws wires, mark the output with Out (O).
2. Press F8 (Run > Resolve): the transfer function appears in the Result
   panel and a Bode plot opens.
3. Run > New Plot (F9) for other plots; click a plot to place markers.


## Installation

* Windows: open the setup executable and follow the instructions.
* GNU/Linux
  - binaries: install with the package manager of your distribution
  - sources: see the section "Building and Installing" below
* macOS: open the dmg file and copy the application bundle to the Applications folder.


## Requirements for building

* CMake 3.16 or newer
* A C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
* Qt 6.2 or newer (Core, Gui, Widgets, Svg, PrintSupport)
* Qwt 6.2 or newer, built against Qt 6
* Boost 1.66 or newer (headers only)
* [NSIS](https://nsis.sourceforge.io/) (optional, Windows installer)


## Building and Installing

See [BUILDING.md](BUILDING.md) for step-by-step instructions for Linux and Windows.
In short:

```sh
cmake -S . -B build [-DQWT_ROOT_DIR=/path/to/qwt]
cmake --build build
cmake --install build        # optional
```

The executable is written to `build/bin/`.


## Options

Options are passed to cmake with `-D`:

| Option | Meaning |
|--------|---------|
| `QWT_ROOT_DIR=/path` | Qwt install prefix, if not in a standard place |
| `BOOST_ROOT=/path` | Boost headers (an unpacked Boost tree is fine) |
| `CMAKE_PREFIX_PATH=/path` | Qt 6 install prefix, if CMake can't find it |
| `CMAKE_INSTALL_PREFIX=/path` | installation prefix |
| `QSAPECNG_DEPLOY_AFTER_BUILD` | (Windows) copy Qwt and run windeployqt after building, default `ON` |


## Uninstalling

* **From sources**  
  If the build tree has not been deleted from the filesystem,
  you can execute `make uninstall` on Unix systems to remove the installed files,
  or you can build the project "uninstall" in Visual Studio.

* **From binary packages**
  - If you have installed from a (g|b)zipped binary file, you can simply delete
    every file you have unzipped.
  - If you have installed from the Windows setup file, you can find the Uninstall
    link in Start → QSapecNG → Uninstall.
  - If you have installed from a deb or rpm package, you can remove the program
    using the package manager of your GNU/Linux distribution.
  - If you have installed from the dmg file on macOS, simply drag the program icon
    from the Applications directory to the Trash.


## Note for Debian-based distributions

The sources are provided with the `debian` subfolder, and QSapecNG can be built
on a Debian-based system using the common Debian building tools (`devscripts`).

To build, execute (in the source folder):

```sh
debuild binary
dpkg -i ../qsapecng_${version}_${arch}.deb
```

where `${version}` is the current version of QSapecNG and `${arch}` is
your system architecture.

To generate the 'orig' tarball, execute `cmake --build build --target package_source`.


## Note for Slackware/Debian/RedHat-based distributions

You can use `checkinstall` instead of `make install` to install
the program, so that a Slackware/deb/rpm package will be generated
and installed.

If you have downloaded an unstable version from the repository, I suggest using
checkinstall with the following command:

```sh
checkinstall --default \
  --pkgname qsapecng \
  --pkgversion 3.0.0 \
  --pkggroup electronics
```


## Packages

There are two additional targets for the `make` command:

- `package_source`
- `package`

The first creates compressed archives with all the source files, without
the debian subfolder; the second creates a binary compressed package on
Unix systems, a setup executable on Windows (if [NSIS](https://nsis.sourceforge.io/)
is installed on the system) and a .dmg file with the application bundle on macOS.

* Debian users may execute (as root) `cpack -G DEB` instead of
  `make package` to build a .deb binary file.

* RedHat users may execute (as root) `cpack -G RPM` instead of
  `make package` to build a .rpm binary file (this command has not
  been tested).
