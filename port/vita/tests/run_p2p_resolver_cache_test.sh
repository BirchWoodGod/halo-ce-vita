#!/bin/sh
# The resolver cache (port/linux/src/p2p_resolver_cache.c: the last good
# address of each broker, STUN server and relay, for when looking one up
# fails), as the Vita build compiles it (HALO_VITA, the game's ABI on i686):
# p2p_resolver_cache_test.c, then again with ASan and UBSan.
# Needs clang that targets i686 (CLANG, default the Linux build's compiler
# from build.ninja; the sanitized build FUZZ_CLANG, default clang).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/p2p_resolver_cache_test.$$
clang=${CLANG:-$(sed -n 's/^linux_cc = //p' "$root/build.ninja" | head -1)}
[ -n "$clang" ] || clang=clang
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT
cd "$root"
game_flags="-ffunction-sections -fdata-sections -fms-extensions -fshort-wchar -fcommon -fno-strict-aliasing -fwrapv -g -std=gnu11 -D_GNU_SOURCE
	-DHALO_LINUX_PLATFORM_LAYER -DHALO_VITA -Wall -Wno-unused-function -include port/linux/include/halo_linux_prefix.h
	-Iport/linux/src -Iport/linux/include -Isource -Isource/cseries
	-idirafter port/include/xdk"
status=0
$clang --target=i686-linux-gnu -m32 -malign-double -freg-struct-return -O2 $game_flags -Wl,--gc-sections \
	-o "$out/p2p_resolver_cache_test" "$here/p2p_resolver_cache_test.c" -lpthread
"$out/p2p_resolver_cache_test" || status=1
${FUZZ_CLANG:-clang} -m32 -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined $game_flags \
	-Wl,--unresolved-symbols=ignore-in-object-files \
	-o "$out/p2p_resolver_cache_test_san" "$here/p2p_resolver_cache_test.c" -lpthread
"$out/p2p_resolver_cache_test_san" || status=1
exit $status
