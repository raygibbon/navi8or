#!/bin/sh
# Persistent rootless container; never build an image or copy sources.
set -eu
fail() { echo "error: $*" >&2; exit 1; }
action=${1:-build}
case "$action" in create|build|clean|shell|remove) ;; *) fail "expected create, build, clean, shell or remove" ;; esac
podman=${PODMAN:-podman}
container=${BUILD_CONTAINER:-navi8or-build}
test -n "$container" || fail 'set BUILD_CONTAINER to your existing Ubuntu 22.04 container name (list with podman ps -a)'
# make runs recipes in the current repository, including when invoked with -C.
root=$(pwd -P)
test -f "$root/Makefile" && test -f "$root/src/main.c" || fail 'run this command from the Navi8or repository root'
test "$($podman info --format '{{.Host.Security.Rootless}}')" = true || fail 'this workflow requires rootless Podman, without sudo'
if ! $podman container exists "$container"; then
    test "$action" = create || fail "container '$container' does not exist; run make container-create first"
    $podman run -d --name "$container" --userns=host \
        -v "$PWD:/src:z" -w /src "${BUILD_IMAGE:-ubuntu:22.04}" sleep infinity
fi
mount=$($podman inspect --format '{{range .Mounts}}{{if eq .Destination "/src"}}{{.Type}}{{printf "\t"}}{{.Source}}{{printf "\t"}}{{.RW}}{{end}}{{end}}' "$container")
expected=$(printf 'bind\t%s\ttrue' "$root")
test "$mount" = "$expected" || fail "container '$container' /src is not a writable bind mount of '$root'; use a different BUILD_CONTAINER or run container-remove from its original checkout, then container-create here"
if test "$action" = remove; then
    $podman rm -f "$container"
    exit 0
fi
running=$($podman inspect --format '{{.State.Running}}' "$container")
test "$running" = true || $podman start "$container" >/dev/null
$podman exec "$container" sh -c '. /etc/os-release; test "$ID" = ubuntu && test "$VERSION_ID" = 22.04' || fail 'build container must run Ubuntu 22.04'
if test "$action" = create; then
    $podman exec --user 0:0 --workdir /src "$container" sh scripts/install-container-deps.sh
    exit 0
fi
workdir=/src
uid=$(id -u)
gid=$(id -g)
# In a rootless container, UID/GID 0 map to the invoking host user.
user=0:0
# Verify both the mount and actual UID mapping before touching build outputs.
probe=$(mktemp -d "$root/.container-probe.XXXXXX")
trap 'rm -rf "$probe"' EXIT HUP INT TERM
inside="$workdir/${probe##*/}"
$podman exec --user "$user" --workdir "$workdir" "$container" sh -c 'umask 022; printf ok > "$1/owner"' sh "$inside" || fail 'container cannot write to the host source mount; check permissions and SELinux labeling'
test "$(cat "$probe/owner")" = ok || fail 'container source mount does not match the host repository'
test "$(stat -c '%u:%g' "$probe/owner")" = "$uid:$gid" || fail 'container UID/GID mapping would produce incorrectly owned host files'
rm -rf "$probe"
trap - EXIT HUP INT TERM
case "$action" in
    build)
        # Force a fresh Linux build: host objects may have incompatible ABIs.
        # Preserve the separate Windows output tree.
        $podman exec --user "$user" --workdir "$workdir" "$container" sh -ec '
            if test -d build; then find build -mindepth 1 -maxdepth 1 ! -name windows -exec rm -rf {} +; fi
            rm -f nav
            exec make TARGET=native
        '
        ;;
    clean)
        $podman exec --user "$user" --workdir "$workdir" "$container" sh -ec '
            if test -d build; then find build -mindepth 1 -maxdepth 1 ! -name windows -exec rm -rf {} +; fi
            rm -f nav
        '
        ;;
    shell)
        exec $podman exec -it --user "$user" --workdir "$workdir" "$container" /bin/bash
        ;;
esac
