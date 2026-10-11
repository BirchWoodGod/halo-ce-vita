#!/bin/sh
# The network wire switch (port/linux/src/network_wire.c; network_wire_test.c):
# Crossplay off, on, with ad hoc, and the dedicated server's build, each a run
# of its own (the switch is read once a run). Needs a C compiler (CC, default
# cc).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/network_wire_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
cc=${CC:-cc}
flags="-g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -Iport/linux/src -idirafter port/linux/include -include stddef.h"
status=0
cd "$root"
$cc $flags -o "$out/network_wire_test" "$here/network_wire_test.c"
$cc $flags -DHALO_DEDICATED_SERVER=1 -o "$out/network_wire_test_server" "$here/network_wire_test.c"
$cc $flags -DHALO_RELEASE=1 -o "$out/network_wire_test_release" "$here/network_wire_test.c"
for which in off on adhoc; do
	echo "--- $which"
	"$out/network_wire_test" "$which" || status=1
done
echo "--- on, a release build"
"$out/network_wire_test_release" on || status=1
echo "--- the dedicated server's build"
"$out/network_wire_test_server" || status=1
[ $status = 0 ] && echo "PASS network wire switch" || echo "FAIL network wire switch"
test_out_done $status
exit $status
