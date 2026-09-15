# Static libsmb2 relinking

This separate kit accompanies Navi8or binary releases. libsmb2 and its use are
covered by LGPL-2.1-or-later; full terms are in libsmb2/LICENCE-LGPL-2.1.txt.
The complete pinned upstream source includes all copyright and license notices.
Application objects and other required archives are supplied for both platforms.
No LTO is used. You may modify for personal use and reverse engineer for debugging
those modifications. The project MIT license is included without extra restrictions.

Use Ubuntu 22.04 x86-64 with build-essential/CMake for Linux. For Windows use
the MinGW-w64 x86-64 GCC cross compiler and CMake. Existing relink scripts accept
an absolute path to a replacement libsmb2.a, or use the supplied archive:

```sh
sh linux/relink.sh
sh windows/relink.sh
sh linux/relink.sh /absolute/path/to/modified/libsmb2.a
sh windows/relink.sh /absolute/path/to/modified/windows/libsmb2.a
```

From the kit root, rebuild the Linux library after modifying libsmb2/:

```sh
cmake -S libsmb2 -B smb-linux -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF -DENABLE_LIBKRB5=OFF -DENABLE_GSSAPI=OFF \
  -DENABLE_LIBDCERPC=OFF -DENABLE_EXAMPLES=OFF
cmake --build smb-linux
# Locate libsmb2.a beneath smb-linux and pass its absolute path to linux/relink.sh.
```

For Windows apply the included dated compatibility changes first:

```sh
patch -d libsmb2 -p1 < patches/windows-compat.patch
cmake -S libsmb2 -B smb-windows -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  '-DCMAKE_C_FLAGS=-D_WINDOWS -D_WIN32_WINNT=0x0601 -Dasprintf=asprintf -Dvasprintf=vasprintf' \
  -DBUILD_SHARED_LIBS=OFF -DENABLE_LIBKRB5=OFF -DENABLE_GSSAPI=OFF \
  -DENABLE_LIBDCERPC=OFF -DENABLE_EXAMPLES=OFF
cmake --build smb-windows
# Pass the resulting archive's absolute path to windows/relink.sh.
```

Navi8or's Windows build applies exactly these changes; the Linux library is
unmodified. Rebuild/relink scripts and objects do not restrict replacement with
an interface-compatible modified library. Copy themes/ from the user package
beside the resulting executable. Exact source and project build scripts are also
available from the matching GitHub tag, but the kit is sufficient for relinking
without rebuilding Navi8or or fetching the library from a third-party server.
