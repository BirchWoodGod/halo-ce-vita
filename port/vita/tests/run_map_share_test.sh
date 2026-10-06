#!/bin/sh
# Builds and runs the desktop test of map sharing's rules
# (port/linux/game/map_share_protocol.c; map_share_test.c), 32-bit as the
# game is, with the real SHA-256 (port/linux/src/p2p_crypto.c, built as
# run_vita_p2p_test.sh builds it: from a configured tree, for
# build/linux/platform_msvc_semantics.h) and zlib's CRC-32 to compare with.
#   CLANG                 the game's compiler (default: build.ninja's)
#   MAP_SHARE_TEST_SANITIZE=0   without AddressSanitizer/UBSan
#   MAP_SHARE_TEST_XBOX_MAP, MAP_SHARE_TEST_CE_MAP   real maps (map_share_test.c)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/map_share_test.$$
clang=${CLANG:-$(sed -n 's/^linux_cc = //p' "$root/build.ninja" | head -1)}
[ -n "$clang" ] || clang=clang
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT
cd "$root"
game_flags="--target=i686-linux-gnu -m32 -fms-extensions -fshort-wchar -malign-double -fcommon -fno-pic
	-fno-strict-aliasing -fwrapv -freg-struct-return -ffunction-sections -fdata-sections -O2 -g -std=gnu11 -D_GNU_SOURCE -DHALO_LINUX_PLATFORM_LAYER
	-w -include port/linux/include/halo_linux_prefix.h -include build/linux/platform_msvc_semantics.h
	-Iport/linux/src -Iport/linux/include -Iport/third_party/kcp -Isource -Isource/cseries -idirafter port/include/xdk"
$clang $game_flags -c port/linux/src/p2p_crypto.c -o "$out/p2p_crypto.o"
cc=${CC:-gcc}
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
if [ "${MAP_SHARE_TEST_SANITIZE:-1}" = 0 ] || ! echo 'int main(void){return 0;}' | $cc -m32 $sanitize -x c - -o "$out/probe" 2>/dev/null; then
	sanitize=
fi
$cc -m32 -g -O1 -Wall -Wextra -Wno-unused-parameter $sanitize -Iport/linux/game \
	"$here/map_share_test.c" port/linux/game/map_share_protocol.c "$out/p2p_crypto.o" -lz -no-pie -Wl,--gc-sections -o "$out/map_share_test"
echo "map_share_test: ${sanitize:-no sanitizers}"
"$out/map_share_test"
