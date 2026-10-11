#!/bin/sh
# Builds and runs the desktop test of the read-only bit the game clears on
# the files it deletes (port/linux/src/posix_files.c; posix_files_test.c),
# twice: as Linux builds it and as the Vita does (-D__vita__, the card's
# calls stood in for; the SDK's psp2 headers are used for the types), with
# chmod refusing as the Vita's newlib does.
#   VITASDK   the SDK (default ~/vitasdk)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
sdk=${VITASDK:-$HOME/vitasdk}
out=${TMPDIR:-/tmp}/posix_files_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out/include" "$out/linux" "$out/vita"
ln -s "$sdk/arm-vita-eabi/include/psp2" "$out/include/psp2"
ln -s "$sdk/arm-vita-eabi/include/psp2common" "$out/include/psp2common"
ln -s "$sdk/arm-vita-eabi/include/vitasdk" "$out/include/vitasdk"
cc=${CC:-gcc}
flags="-g -O1 -Wall -D_GNU_SOURCE -I$root/port/linux/src -Wl,--wrap=chmod"
$cc $flags "$here/posix_files_test.c" "$root/port/linux/src/posix_files.c" -o "$out/posix_files_test_linux" -lpthread
$cc $flags -D__vita__ -I"$out/include" "$here/posix_files_test.c" "$root/port/linux/src/posix_files.c" \
	-o "$out/posix_files_test_vita" -lpthread
status=0
"$out/posix_files_test_linux" "$out/linux" || status=$?
"$out/posix_files_test_vita" "$out/vita" || status=$?
test_out_done $status
exit $status
