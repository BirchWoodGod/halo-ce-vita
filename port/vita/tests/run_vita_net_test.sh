#!/bin/sh
# Builds and runs the desktop test of port/vita/host/vita_net.c over a mock
# sceNet with the Vita's semantics (vita_net_test.c, mock_scenet.c). 32-bit,
# as the VitaSDK headers' structures are; only the SDK's psp2 headers are
# used (the C library is the host's).
#   VITASDK         the SDK (default ~/vitasdk)
#   VITA_NET_TEST_LOG=1   show vita_net.c's log lines
#   HALO_NET_TRACE=1      with the trace on
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
sdk=${VITASDK:-$HOME/vitasdk}
out=${TMPDIR:-/tmp}/vita_net_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out/include"
ln -s "$sdk/arm-vita-eabi/include/psp2" "$out/include/psp2"
ln -s "$sdk/arm-vita-eabi/include/psp2common" "$out/include/psp2common"
ln -s "$sdk/arm-vita-eabi/include/vitasdk" "$out/include/vitasdk" 2>/dev/null || true
[ -e "$sdk/arm-vita-eabi/include/vitasdk.h" ] && ln -s "$sdk/arm-vita-eabi/include/vitasdk.h" "$out/include/vitasdk.h"
cc=${CC:-gcc}
$cc -m32 -pthread -g -O1 -Wall -Wno-unused-function -D_GNU_SOURCE -I"$out/include" -I"$root/port/linux/src" -I"$root/port/vita/include" \
	"$here/vita_net_test.c" "$here/mock_scenet.c" -o "$out/vita_net_test"
status=0
"$out/vita_net_test" || status=$?
test_out_done $status
exit $status
