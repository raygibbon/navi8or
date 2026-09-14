# Building Navi8or for Windows

Cross-build on Linux with the MinGW-w64 `x86_64-w64-mingw32` toolchain.
Dependency builds also require CMake, curl, tar, make, and the usual shell/build
utilities. The application build uses patch; inspection uses file and MinGW-w64
objdump.

Use the existing Ubuntu 22.04 rootless Podman container to avoid requiring
MinGW packages on the Debian/Bazzite host. Run from the checkout mounted at
`/src` in `navi8or-build` (created by `make container-create`):

```sh
podman exec --user 0:0 navi8or-build sh -ec 'apt-get update; DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends gcc-mingw-w64-x86-64'
podman exec --workdir /src navi8or-build x86_64-w64-mingw32-gcc --version
podman exec --user 0:0 --workdir /src navi8or-build make TARGET=windows windows-deps
podman exec --user 0:0 --workdir /src navi8or-build make TARGET=windows
file dist/windows/nav.exe
podman exec --workdir /src navi8or-build x86_64-w64-mingw32-objdump -p dist/windows/nav.exe
```

The source and Windows outputs stay on the host bind mount. In this rootless
container, UID/GID 0 map to the invoking host user. These commands preserve the
native Linux build and use the existing MinGW toolchain file, static archives,
and Schannel configuration. No custom image is required.

The existing backend error test can also be compiled in the container:

```sh
podman exec --workdir /src navi8or-build make TARGET=windows windows-error-test
wine build/windows/windows-error-test.exe    # optional, on a host with Wine
wine dist/windows/nav.exe --help             # optional launch check
```

Wine/UMU checks supplement compilation; native Windows console validation is
still required. See `docs/WINDOWS_RUNTIME_TEST.md` for the native checklist.

Alternatively, with MinGW already installed on the host, run from the repository root:


```sh
make TARGET=windows windows-deps
make TARGET=windows
make TARGET=windows windows-inspect
make TARGET=windows dist
```

Target dependencies (currently libcurl, libsodium, and libsmb2) are installed
under `.deps/windows/`; downloaded sources and dependency build directories
live under `.deps/sources/`. The Windows rules link explicit target static
libraries and do not query host pkg-config. Keep any custom compiler/linker
flags target-specific so host libraries are not accidentally linked.

The Windows target selects `src/platform/windows.c`,
`src/platform/secure_file_win32.c`, `src/path_win32.c`, and
`src/provider/local_win32.c`, excluding their POSIX counterparts.

Objects and the patched terminal header go under `build/windows/`. The product
name remains Navi8or, and the executable remains `nav.exe`, produced at
`dist/windows/nav.exe`. The `dist` target also copies this document as
`dist/windows/README.md` and copies the themes into `dist/windows/themes/`.

The existing `windows-inspect` target runs `file` on the executable and uses
MinGW-w64 `objdump -p` to list imported DLLs, allowing inspection of the PE
architecture and runtime imports. Successful cross-compilation and inspection
still require follow-up runtime testing on Windows.
