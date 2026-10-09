#!/bin/sh
# Builds and runs the desktop test of the controller ports' merge
# (port/vita/host/vita_ctrl_ports.c over a fake ctrl layer, and the stick
# clicks in vita_controls.c; vita_ctrl_ports_test.c). No SDK needed.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/vita_ctrl_ports_test.$$
mkdir -p "$out"
cc=${CC:-gcc}
$cc -m32 -g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -fsanitize=address,undefined \
	-I"$root/port/vita/include" "$here/vita_ctrl_ports_test.c" -o "$out/vita_ctrl_ports_test"
status=0
"$out/vita_ctrl_ports_test" || status=$?
rm -rf "$out"
exit $status
