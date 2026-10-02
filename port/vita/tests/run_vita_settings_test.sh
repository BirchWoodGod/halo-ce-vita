#!/bin/sh
# Builds and runs the desktop test of the settings panel's multiplayer page
# (port/vita/host/vita_settings.c; vita_settings_test.c). Only the SDK's
# psp2 headers are used, for the types (the C library is the host's).
#   VITASDK   the SDK (default ~/vitasdk)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
sdk=${VITASDK:-$HOME/vitasdk}
out=${TMPDIR:-/tmp}/vita_settings_test.$$
mkdir -p "$out/include"
ln -s "$sdk/arm-vita-eabi/include/psp2" "$out/include/psp2"
ln -s "$sdk/arm-vita-eabi/include/psp2common" "$out/include/psp2common"
ln -s "$sdk/arm-vita-eabi/include/vitasdk" "$out/include/vitasdk" 2>/dev/null || true
cc=${CC:-gcc}
$cc -m32 -g -O1 -Wall -Wno-unused-function -D_GNU_SOURCE -I"$out/include" -I"$root/port/linux/src" -I"$root/port/vita/include" \
	"$here/vita_settings_test.c" -o "$out/vita_settings_test"
status=0
(cd "$out" && ./vita_settings_test) || status=$?
rm -rf "$out"
exit $status
