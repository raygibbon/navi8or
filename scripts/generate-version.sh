#!/bin/sh
# Deterministic, validated and atomic. Preserve mtime when content is unchanged.
set -eu
version_file=${1:-VERSION}
version_header=${2:-build/generated/nav_version.h}
mkdir -p "$(dirname "$version_header")"
version_temporary=$(mktemp "$version_header.tmp.XXXXXX")
trap 'rm -f "$version_temporary"' EXIT HUP INT TERM
LC_ALL=C awk '
    NR == 1 && /^[0-9]+\.[0-9]+\.[0-9]+([-+][A-Za-z0-9.-]+)?$/ { version = $0; next }
    { invalid = 1 }
    END {
        if (invalid || NR != 1 || version == "") {
            print "VERSION must contain one semantic version line" > "/dev/stderr"
            exit 1
        }
        print "/* Generated from VERSION. Do not edit. */"
        print "#ifndef NAV_VERSION_H"
        print "#define NAV_VERSION_H"
        print "#define NAV_APP_NAME \"Navi8or\""
        printf "#define NAV_VERSION \"%s\"\n", version
        print "#define NAV_APP_IDENTITY NAV_APP_NAME \" \" NAV_VERSION"
        print "#endif"
    }
' "$version_file" > "$version_temporary"
if ! cmp -s "$version_temporary" "$version_header"; then
    mv -f "$version_temporary" "$version_header"
fi
