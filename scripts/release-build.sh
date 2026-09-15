#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
platform=${1:?expected linux or windows}
case "$platform" in
linux|check)
    test "$(uname -m)" = x86_64 || { echo 'Linux release requires x86-64' >&2; exit 1; }
    baseline=false
    if test -f /etc/os-release; then
        . /etc/os-release
        # A neutral root also prevents dependency build metadata embedding HOME.
        if test "$ID" = ubuntu && test "$VERSION_ID" = 22.04 && test "$(pwd -P)" = /src; then baseline=true; fi
    fi
    if test "$baseline" = false; then
        runner=${PODMAN:-podman}
        container=${BUILD_CONTAINER:-navi8or-build}
        test -n "$runner" || runner=podman
        test -n "$container" || container=navi8or-build
        if ! "$runner" container exists "$container"; then
            BUILD_CONTAINER="$container" PODMAN="$runner" sh scripts/container-dev.sh create
        fi
        mount=$("$runner" inspect --format '{{range .Mounts}}{{if eq .Destination "/src"}}{{.Source}}{{end}}{{end}}' "$container")
        test "$mount" = "$(pwd -P)" || { echo 'release container must mount this checkout at /src' >&2; exit 1; }
        test "$("$runner" inspect --format '{{.State.Running}}' "$container")" = true || "$runner" start "$container" >/dev/null
        release_epoch=${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct 2>/dev/null || printf '0')}
        exec "$runner" exec --user 0:0 --workdir /src --env SOURCE_DATE_EPOCH="$release_epoch" \
            --env PYTHONDONTWRITEBYTECODE=1 "$container" sh scripts/release-build.sh "$platform"
    fi
    make TARGET=native USE_LINUX_DEPS=1 BUILD_MODE=release APP_BINARY=build/release/bin/nav all
    if test "$platform" = check; then
        make TARGET=native USE_LINUX_DEPS=1 BUILD_MODE=release APP_BINARY=build/release/bin/nav release-profile-test
        exec python3 scripts/release.py check
    fi
    ;;
windows)
    make TARGET=windows windows-deps
    make TARGET=windows BUILD_MODE=release APP_BINARY=build/release/bin/nav.exe all
    ;;
*) echo 'expected linux or windows' >&2; exit 1 ;;
esac
python3 scripts/release.py package "$platform"
