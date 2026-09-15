#!/bin/sh
# Linux application libraries only; libc/toolchain remain system supplied.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
prefix="$root/build/linux-deps"
cache="$root/.deps/linux-sources"
jobs=${JOBS:-4}
export CC=${CC:-cc}
# No inherited distro pkg-config paths or compiler/library search overrides.
unset CPPFLAGS CFLAGS CXXFLAGS LDFLAGS LIBS CPATH C_INCLUDE_PATH LIBRARY_PATH CMAKE_PREFIX_PATH
export PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig"
export CFLAGS='-O2 -fPIC'
export LC_ALL=C

# Exact releases / immutable revision. SHA256 locks the upstream archive bytes.
curl_version=8.22.0
sodium_version=1.0.22
openssl_version=3.5.8
zlib_version=1.3.2
smb2_revision=b3d560c02fb1268320d2fd1c17fe841b0d93b85f # libsmb2 7.0.0; same revision as Windows
curl_sha=f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7
sodium_sha=adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349
openssl_sha=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2
zlib_sha=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
smb2_sha=3f034853002bddd858662847070b26b21161b097d72be627f05785ecdba4278e

test "$(uname -s)" = Linux || { echo 'error: linux-deps requires Linux' >&2; exit 1; }
for tool in "$CC" make cmake curl tar xz sha256sum perl pkg-config flock; do
    command -v "$tool" >/dev/null || { echo "error: missing dependency build tool: $tool; run make container-create" >&2; exit 1; }
done
mkdir -p "$root/build" "$cache"
# Serialize independent make invocations, including clean/build attempts.
exec 9>"$root/build/.linux-deps.lock"
flock 9
fingerprint=$({ sha256sum "$0"; printf '%s\n' "$prefix" "$CC"; "$CC" --version; "$CC" -dumpmachine; ldd --version; } | sha256sum | cut -d ' ' -f1)
complete=true
for library in curl sodium smb2 ssl crypto z; do
    test -s "$prefix/lib/lib$library.a" || complete=false
done
for header in curl/curl.h sodium.h smb2/libsmb2.h openssl/ssl.h zlib.h; do
    test -s "$prefix/include/$header" || complete=false
done
if test "$complete" = true && test "$(cat "$prefix/.stamp" 2>/dev/null || true)" = "$fingerprint"; then
    echo 'Linux dependencies are up to date.'
    exit 0
fi
# A different compiler/glibc/prefix must never reuse old objects or libraries.
rm -rf "$prefix"
mkdir -p "$prefix/src" "$prefix/build" "$prefix/lib"
fetch() {
    archive=$1 url=$2 expected=$3
    if ! test -s "$cache/$archive"; then
        curl --fail --location --retry 3 --connect-timeout 30 --max-time 300 \
            --output "$cache/$archive.part" "$url"
        mv "$cache/$archive.part" "$cache/$archive"
    fi
    printf '%s  %s\n' "$expected" "$cache/$archive" | sha256sum -c - || {
        echo "error: checksum mismatch; remove $cache/$archive and retry (never bypass verification)" >&2
        exit 1
    }
    tar -xf "$cache/$archive" -C "$prefix/src"
}
fetch "zlib-$zlib_version.tar.gz" "https://zlib.net/fossils/zlib-$zlib_version.tar.gz" "$zlib_sha"
fetch "openssl-$openssl_version.tar.gz" "https://github.com/openssl/openssl/releases/download/openssl-$openssl_version/openssl-$openssl_version.tar.gz" "$openssl_sha"
fetch "libsodium-$sodium_version.tar.gz" "https://download.libsodium.org/libsodium/releases/libsodium-$sodium_version.tar.gz" "$sodium_sha"
fetch "curl-$curl_version.tar.xz" "https://curl.se/download/curl-$curl_version.tar.xz" "$curl_sha"
fetch "libsmb2-$smb2_revision.tar.gz" "https://codeload.github.com/sahlberg/libsmb2/tar.gz/$smb2_revision" "$smb2_sha"
(
    mkdir -p "$prefix/build/zlib"; cd "$prefix/build/zlib"
    "$prefix/src/zlib-$zlib_version/configure" --static --prefix="$prefix"
    make -j"$jobs"; make install
)
(
    mkdir -p "$prefix/build/openssl"; cd "$prefix/build/openssl"
    # Built-in providers; no external modules, global config or private CA bundle.
    "$prefix/src/openssl-$openssl_version/config" --prefix="$prefix" --libdir=lib \
        --openssldir=/etc/ssl no-shared no-module no-dso no-tests no-autoload-config
    make -j"$jobs" build_libs; make install_dev
)
(
    mkdir -p "$prefix/build/sodium"; cd "$prefix/build/sodium"
    "$prefix/src/libsodium-$sodium_version/configure" --prefix="$prefix" --disable-shared --enable-static
    make -j"$jobs"; make install
)
cmake -S "$prefix/src/libsmb2-$smb2_revision" -B "$prefix/build/smb2" \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_SHARED_LIBS=OFF -DENABLE_LIBKRB5=OFF \
    -DENABLE_GSSAPI=OFF -DENABLE_LIBDCERPC=OFF -DENABLE_EXAMPLES=OFF
cmake --build "$prefix/build/smb2" -j"$jobs"
cmake --install "$prefix/build/smb2"
cmake -S "$prefix/src/curl-$curl_version" -B "$prefix/build/curl" \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
    -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF \
    -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \
    -DCURL_USE_PKGCONFIG=OFF -DCURL_USE_CMAKECONFIG=OFF -DHTTP_ONLY=ON \
    -DCURL_USE_OPENSSL=ON -DOPENSSL_USE_STATIC_LIBS=TRUE \
    -DOPENSSL_INCLUDE_DIR="$prefix/include" -DOPENSSL_SSL_LIBRARY="$prefix/lib/libssl.a" \
    -DOPENSSL_CRYPTO_LIBRARY="$prefix/lib/libcrypto.a" \
    -DCURL_ZLIB=ON -DZLIB_INCLUDE_DIR="$prefix/include" -DZLIB_LIBRARY="$prefix/lib/libz.a" \
    -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DUSE_LIBIDN2=OFF -DCURL_USE_LIBPSL=OFF \
    -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBSSH=OFF -DUSE_NGHTTP2=OFF \
    -DCURL_USE_GSSAPI=OFF -DCURL_USE_GSASL=OFF -DCURL_ENABLE_SMB=OFF \
    -DCURL_DISABLE_KERBEROS_AUTH=ON -DCURL_DISABLE_NEGOTIATE_AUTH=ON \
    -DCURL_ENABLE_NTLM=ON -DENABLE_THREADED_RESOLVER=ON \
    -DCURL_CA_BUNDLE=none -DCURL_CA_PATH=none -DCURL_CA_FALLBACK=ON \
    -DCURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG=ON
cmake --build "$prefix/build/curl" -j"$jobs"
cmake --install "$prefix/build/curl"
printf '%s\n' "$fingerprint" > "$prefix/.stamp"
echo "Built curl $curl_version, libsodium $sodium_version, libsmb2 7.0.0 ($smb2_revision), OpenSSL $openssl_version, zlib $zlib_version"
"$prefix/bin/curl-config" --protocols --features
