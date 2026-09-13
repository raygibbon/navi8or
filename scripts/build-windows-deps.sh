#!/bin/sh
# Target libraries stay outside the source distribution; no host pkg-config use.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix=${WINDOWS_DEPS_PREFIX:-"$root/.deps/windows"}
work="$root/.deps/sources"
jobs=${JOBS:-4}
for tool in x86_64-w64-mingw32-gcc x86_64-w64-mingw32-ar x86_64-w64-mingw32-ranlib cmake curl tar make sed; do
    command -v "$tool" >/dev/null || { echo "error: missing Windows dependency-build tool: $tool" >&2; exit 1; }
done
mkdir -p "$work" "$prefix"
fetch() {
    test -s "$work/$1" || curl --fail --location --output "$work/$1" "$2"
}
fetch libsodium-1.0.20.tar.gz https://download.libsodium.org/libsodium/releases/libsodium-1.0.20.tar.gz
fetch curl-8.12.1.tar.xz https://curl.se/download/curl-8.12.1.tar.xz
fetch libsmb2.tar.gz https://codeload.github.com/sahlberg/libsmb2/tar.gz/b3d560c02fb1268320d2fd1c17fe841b0d93b85f
if ! test -e "$prefix/lib/libsodium.a"; then
    tar -xf "$work/libsodium-1.0.20.tar.gz" -C "$work"
    (cd "$work/libsodium-1.0.20" && ./configure --host=x86_64-w64-mingw32 --prefix="$prefix" --disable-shared --enable-static && make -j"$jobs" && make install)
fi
if ! test -e "$prefix/lib/libcurl.a"; then
    tar -xf "$work/curl-8.12.1.tar.xz" -C "$work"
    cmake -S "$work/curl-8.12.1" -B "$work/curl-build" \
        -DCMAKE_TOOLCHAIN_FILE="$root/cmake/mingw-w64.cmake" -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF \
        -DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF -DCURL_USE_LIBPSL=OFF \
        -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DUSE_LIBIDN2=OFF \
        -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBSSH=OFF -DUSE_NGHTTP2=OFF
    cmake --build "$work/curl-build" -j"$jobs"
    cmake --install "$work/curl-build"
fi
if ! test -e "$prefix/lib/libsmb2.a" || test "$0" -nt "$prefix/lib/libsmb2.a"; then
    tar -xf "$work/libsmb2.tar.gz" -C "$work"
    # C99 inline functions need internal linkage in this header under MinGW.
    sed -i -e 's/^inline int writev(/static inline int writev(/' \
        -e 's/^inline int readv(/static inline int readv(/' \
        "$work/libsmb2-b3d560c02fb1268320d2fd1c17fe841b0d93b85f/lib/compat.h"
    # Select libsmb2's Windows socket ABI and use MinGW's stdio functions
    # instead of the pinned source's conflicting asprintf/vasprintf shims.
    cmake -S "$work/libsmb2-b3d560c02fb1268320d2fd1c17fe841b0d93b85f" -B "$work/smb-build" \
        -DCMAKE_TOOLCHAIN_FILE="$root/cmake/mingw-w64.cmake" -DCMAKE_INSTALL_PREFIX="$prefix" \
        '-DCMAKE_C_FLAGS=-D_WINDOWS -Dasprintf=asprintf -Dvasprintf=vasprintf' \
        -DBUILD_SHARED_LIBS=OFF -DENABLE_LIBKRB5=OFF -DENABLE_GSSAPI=OFF -DENABLE_LIBDCERPC=OFF
    cmake --build "$work/smb-build" -j"$jobs"
    cmake --install "$work/smb-build"
fi
