# Building Navi8or

## Linux

The release ABI baseline is Ubuntu 22.04 x86-64. Navi8or owns pinned curl,
OpenSSL, zlib, libsodium and libsmb2 archives; libc, DNS configuration and the
runtime CA store remain system facilities. Versions/checksums live in
`scripts/build-linux-deps.sh`.

Rootless Podman workflow:

```sh
make container-create
make container-build
make container-shell
# Inside:
make check location-test
```

The persistent container mounts the checkout at /src. Use a separate
BUILD_CONTAINER per checkout. Do not mix host/container dependency builds:
the fingerprint includes compiler, libc and install prefix; incompatibility
rebuilds dependencies.

On Ubuntu 22.04 itself, install tools with
`sh scripts/install-container-deps.sh` (requires root), then `make`.
Ordinary builds retain debug information and produce ignored `nav`.

## Windows

Install MinGW-w64, CMake, make, autoconf/automake/libtool, curl, patch, tar and xz.

```sh
make TARGET=windows windows-deps
make TARGET=windows
make TARGET=windows windows-inspect
make TARGET=windows windows-error-test
```

The executable is `build/windows/nav.exe`. Application dependencies are static;
curl uses Schannel/system trust. Win32 console input, UTF-8 and VT output are
required. Pins live in `scripts/build-windows-deps.sh`. Do not use MSVC.

## Version and packages

Root VERSION is canonical. Both platforms generate build/generated/nav_version.h.
Changing VERSION updates the next build; do not edit generated headers.

```sh
make release-linux
make release-windows
make release
make release-check
```

Linux release targets enter the canonical Podman container unless already on
Ubuntu 22.04 at the neutral /src build root. CI bind-mounts its checkout at /src
to avoid embedding runner-home paths in dependency metadata. Windows requires MinGW.
Release builds use separate objects, optimization, section garbage collection
and stripped staged binaries; development flags are unchanged.

Outputs in build/release: platform packages, a combined LGPL relinking kit,
SHA256SUMS and separate debug files. Staging uses explicit inputs, never arbitrary
dist contents. Archive entry order, timestamps, modes and owners are normalized
with SOURCE_DATE_EPOCH (default latest commit time). Reproducibility assumes
identical source/dependency bytes and toolchains, not arbitrary compilers.

make dist-check creates a filtered source archive, extracts it and runs check.
It excludes generated artifacts and no longer requires historical reference docs.
