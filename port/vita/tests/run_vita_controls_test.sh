#!/bin/sh
# Builds and runs the desktop test of the controls' mapping layer
# (port/vita/host/vita_controls.c: touch zones, the rear pad's hold time,
# the actions on buttons and zones; vita_controls_test.c). No SDK needed.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/vita_controls_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
cc=${CC:-gcc}
$cc -m32 -g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -I"$root/port/vita/include" \
	"$here/vita_controls_test.c" -o "$out/vita_controls_test"
status=0
"$out/vita_controls_test" || status=$?
test_out_done $status
exit $status
