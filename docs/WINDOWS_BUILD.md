# Building Navi8or for Windows

Cross-build on Linux with the MinGW-w64 `x86_64-w64-mingw32` toolchain.
Dependency builds also require CMake, curl, tar, make, and the usual shell/build
utilities. The application build uses patch; inspection uses file and MinGW-w64
objdump.

Run from the repository root:

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
