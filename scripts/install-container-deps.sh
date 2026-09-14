#!/bin/sh
# Run only inside Ubuntu 22.04; installations persist in the container.
set -eu
. /etc/os-release
test "$ID" = ubuntu && test "$VERSION_ID" = 22.04
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends build-essential pkg-config cmake \
    curl ca-certificates python3 file binutils libcurl4-openssl-dev libsodium-dev libssl-dev

# Jammy does not package libsmb2. Reuse the revision pinned for Windows,
# installed here for Linux, without changing Windows scripts or libraries.
# Only this unavailable third-party library is static; curl/sodium/libc stay dynamic.
revision=b3d560c02fb1268320d2fd1c17fe841b0d93b85f
marker=/usr/local/share/navi8or-libsmb2-revision
if test "$(cat "$marker" 2>/dev/null || true)" != "$revision" || \
    ! test -s /usr/local/lib/libsmb2.a || ! pkg-config --exists libsmb2; then
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT HUP INT TERM
    curl --fail --location --retry 3 --connect-timeout 30 -o "$work/libsmb2.tar.gz" \
        "https://codeload.github.com/sahlberg/libsmb2/tar.gz/$revision"
    tar -xzf "$work/libsmb2.tar.gz" -C "$work"
    cmake -S "$work/libsmb2-$revision" -B "$work/build" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DBUILD_SHARED_LIBS=OFF -DENABLE_LIBKRB5=OFF -DENABLE_GSSAPI=OFF \
        -DENABLE_LIBDCERPC=OFF -DENABLE_EXAMPLES=OFF
    cmake --build "$work/build" -j2
    cmake --install "$work/build"
    mkdir -p /usr/local/share
    printf '%s\n' "$revision" > "$marker"
fi
pkg-config --modversion libcurl libsodium libsmb2
