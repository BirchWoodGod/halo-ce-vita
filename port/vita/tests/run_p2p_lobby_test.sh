#!/bin/sh
# The server browser's listings, signatures and passwords
# (port/linux/src/p2p_lobby.c, p2p_crypto.c and Monocypher, as the Vita
# build compiles them: HALO_VITA, the game's ABI on i686), and their reader
# fuzzed (p2p_lobby_fuzz.c):
#   run_p2p_lobby_test.sh          the checks (p2p_lobby_test.c), then again
#                                  with ASan and UBSan
#   run_p2p_lobby_test.sh fuzz [RUNS]   the listing reader and the browser's
#                                  taking of listings fuzzed with ASan and
#                                  UBSan (default 2,000,000 inputs)
#   run_p2p_lobby_test.sh signal [RUNS]   what a signalling broker sends
#                                  (p2p_signal.c's MQTT 5 and 3.1.1 reading)
#                                  fuzzed the same way (p2p_signal_fuzz.c)
# Needs the Linux build configured in this tree (build/linux: the platform
# layer's generated semantics header) and clang that targets i686 (CLANG,
# default the Linux build's compiler from build.ninja; the sanitized builds
# FUZZ_CLANG, default clang).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/p2p_lobby_test.$$
clang=${CLANG:-$(sed -n 's/^linux_cc = //p' "$root/build.ninja" | head -1)}
[ -n "$clang" ] || clang=clang
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT
cd "$root"
# the game's ABI, as the platform layer is built, with HALO_VITA
game_flags="-ffunction-sections -fdata-sections -fms-extensions -fshort-wchar -fcommon -fno-strict-aliasing -fwrapv -g -std=gnu11 -D_GNU_SOURCE
	-DHALO_LINUX_PLATFORM_LAYER -DHALO_VITA -w -include port/linux/include/halo_linux_prefix.h
	-include build/linux/platform_msvc_semantics.h -Iport/linux/src -Iport/linux/include -Iport/third_party/monocypher
	-Isource -Isource/cseries -idirafter port/include/xdk"
crypto="port/linux/src/p2p_crypto.c port/third_party/monocypher/monocypher.c port/third_party/monocypher/monocypher-ed25519.c"
# (the player's lines in the language chosen: none here, English)
crypto="$crypto port/linux/src/lang.c"
mode=${1:-test}
if [ "$mode" = fuzz ]; then
	# (the platform layer is 32-bit only, and the host's clang has no 32-bit
	# libFuzzer: p2p_lobby_fuzz.c's own engine, with ASan and UBSan)
	runs=${2:-2000000}
	${FUZZ_CLANG:-clang} -m32 -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined $game_flags \
		-Wl,--unresolved-symbols=ignore-in-object-files -o "$out/p2p_lobby_fuzz" "$here/p2p_lobby_fuzz.c" $crypto \
		-lpthread
	"$out/p2p_lobby_fuzz" "$runs"
	exit $?
fi
if [ "$mode" = signal ]; then
	# what a broker sends (p2p_signal.c) fuzzed, with ASan and UBSan
	runs=${2:-2000000}
	${FUZZ_CLANG:-clang} -m32 -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined $game_flags \
		-Wl,--unresolved-symbols=ignore-in-object-files -o "$out/p2p_signal_fuzz" "$here/p2p_signal_fuzz.c" $crypto \
		-lpthread
	"$out/p2p_signal_fuzz" "$runs"
	exit $?
fi
status=0
$clang --target=i686-linux-gnu -m32 -malign-double -freg-struct-return -O2 $game_flags -Wl,--gc-sections -o "$out/p2p_lobby_test" \
	"$here/p2p_lobby_test.c" $crypto -lpthread
"$out/p2p_lobby_test" || status=1
# again with the sanitizers (the host's clang: the i686 one may have no runtime)
# (the XDK headers' inline functions, never called, are left unresolved)
${FUZZ_CLANG:-clang} -m32 -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined $game_flags \
	-Wl,--unresolved-symbols=ignore-in-object-files \
	-o "$out/p2p_lobby_test_san" "$here/p2p_lobby_test.c" $crypto -lpthread
"$out/p2p_lobby_test_san" || status=1
exit $status
