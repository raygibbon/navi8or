#!/bin/sh
# Bootstrap tools only. Application libraries live in /src/build/linux-deps.
set -eu
. /etc/os-release
test "$ID" = ubuntu && test "$VERSION_ID" = 22.04
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends build-essential pkg-config cmake \
    autoconf automake libtool perl git curl ca-certificates patch tar xz-utils \
    python3 file binutils util-linux
