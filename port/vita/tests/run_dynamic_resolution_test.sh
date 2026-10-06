#!/bin/sh
# Builds and runs the desktop test of the dynamic resolution's controller
# (port/vita/platform/dynamic_resolution.c; dynamic_resolution_test.c).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/dynamic_resolution_test.$$
mkdir -p "$out"
cc=${CC:-gcc}
$cc -m32 -g -O1 -Wall -Wextra -I"$root/port/vita/include" "$here/dynamic_resolution_test.c" -o "$out/dynamic_resolution_test"
status=0
"$out/dynamic_resolution_test" || status=$?
rm -rf "$out"
exit $status
