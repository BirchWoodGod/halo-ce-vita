#!/bin/sh
# Builds and runs the desktop test of the plans for blends GXM cannot take
# as they are (port/vita/platform/blend_constant.c; blend_constant_test.c).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/blend_constant_test.$$
mkdir -p "$out"
cc=${CC:-gcc}
$cc -m32 -g -O1 -Wall -Wextra -I"$root/port/vita/include" "$here/blend_constant_test.c" -o "$out/blend_constant_test" -lm
status=0
"$out/blend_constant_test" || status=$?
rm -rf "$out"
exit $status
