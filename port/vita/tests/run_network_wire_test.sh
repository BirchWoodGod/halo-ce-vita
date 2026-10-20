#!/bin/sh
# The network wire (port/linux/src/network_wire.c; network_wire_test.c): a
# wire chosen per game, who can join a hosted game, this build's platform,
# the browsers' filter; for a Vita's build (HALO_VITA), the port's PC build
# (HALO_NET_AS_VITA) and its release, the dedicated server's and a build that
# does not play as a Vita, each a run of its own. Needs a C compiler (CC, default cc).
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
$cc $flags -DHALO_VITA=1 -o "$out/network_wire_test_vita" "$here/network_wire_test.c"
$cc $flags -DHALO_NET_AS_VITA=1 -o "$out/network_wire_test" "$here/network_wire_test.c"
$cc $flags -DHALO_NET_AS_VITA=1 -DHALO_RELEASE=1 -o "$out/network_wire_test_release" "$here/network_wire_test.c"
$cc $flags -DHALO_NET_AS_VITA=1 -DHALO_DEDICATED_SERVER=1 -o "$out/network_wire_test_server" "$here/network_wire_test.c"
$cc $flags -o "$out/network_wire_test_pc" "$here/network_wire_test.c"
for which in _vita "" _release _server _pc; do
	echo "--- network_wire_test$which"
	"$out/network_wire_test$which" || status=1
done
[ $status = 0 ] && echo "PASS network wire" || echo "FAIL network wire"
test_out_done $status
exit $status
