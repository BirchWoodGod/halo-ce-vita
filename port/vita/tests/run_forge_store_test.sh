#!/bin/bash
# The Forge variant store's last good copy (port/linux/src/forge_store.c;
# forge_store_test.c): .prev kept by a save, used by a load and a list when the
# file fails its checks (damaged, zeroed, cut), .bad for the failed one.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/forge_store_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out/save/forge/bloodgulch"
clang=${CLANG:-$(sed -n 's/^linux_cc = //p' "$root/build.ninja" | head -1)}
[ -n "$clang" ] || clang=clang
status=0
cd "$root"
# (posix_files.c is built on its own, as the ninja build does)
$clang --target=i686-linux-gnu -m32 -std=gnu11 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -O1 -g -Iport/linux/src -c \
	port/linux/src/posix_files.c -o "$out/posix_files.o"
$clang --target=i686-linux-gnu -m32 -malign-double -freg-struct-return -O1 -g -ffunction-sections -fdata-sections -Wl,--gc-sections -fms-extensions -fshort-wchar -fcommon \
	-fno-strict-aliasing -fwrapv -std=gnu11 -D_GNU_SOURCE -DHALO_LINUX_PLATFORM_LAYER -w \
	-include port/linux/include/halo_linux_prefix.h -include build/linux/platform_msvc_semantics.h \
	-Iport/linux/src -Iport/linux/include -Iport/linux/game -Isource -Isource/cseries -idirafter port/include/xdk \
	-o "$out/forge_store_test" "$here/forge_store_test.c" \
	port/linux/src/forge_store.c port/linux/game/forge_files.c "$out/posix_files.o" port/linux/src/lang.c -lpthread
"$out/forge_store_test" "$out/save" || status=1
test_out_done $status
exit $status
