#!/bin/sh
set -eu
test -x "$1"
"$1" --help | grep -q '^Usage: nav'
