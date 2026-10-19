#!/bin/sh
# Friends (port/linux/src/p2p_friends.c, p2p_crypto.c and Monocypher, as the
# Vita build compiles them: HALO_VITA, the game's ABI on i686):
#   run_p2p_friends_test.sh          the checks (p2p_friends_test.c: three
#                                    machines and a broker in one process),
#                                    then again with ASan and UBSan
#   run_p2p_friends_test.sh seed DIR   DIR/friends made as a Vita's with two
#                                    friends, a request, a request sent and a
#                                    blocked player (run_menus_test.sh's cases)
#   run_p2p_friends_test.sh fuzz [RUNS]   every message the brokers can carry
#                                    to the inbox and the mailbox, and both
#                                    files, fuzzed with ASan and UBSan
#                                    (p2p_friends_fuzz.c; default 300,000
#                                    inputs)
# Needs the Linux build configured in this tree (build/linux: the platform
# layer's generated semantics header) and clang that targets i686 (CLANG,
# default the Linux build's compiler from build.ninja; the sanitized builds
# FUZZ_CLANG, default clang).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/p2p_friends_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
clang=${CLANG:-$(sed -n 's/^linux_cc = //p' "$root/build.ninja" | head -1)}
[ -n "$clang" ] || clang=clang
mkdir -p "$out"
trap 'test_out_done $?' EXIT
cd "$root"
game_flags="-ffunction-sections -fdata-sections -fms-extensions -fshort-wchar -fcommon -fno-strict-aliasing -fwrapv -g -std=gnu11 -D_GNU_SOURCE
	-DHALO_LINUX_PLATFORM_LAYER -DHALO_VITA -w -include port/linux/include/halo_linux_prefix.h
	-include build/linux/platform_msvc_semantics.h -Iport/linux/src -Iport/linux/include -Iport/third_party/monocypher
	-Isource -Isource/cseries -idirafter port/include/xdk"
crypto="port/linux/src/p2p_crypto.c port/third_party/monocypher/monocypher.c port/third_party/monocypher/monocypher-ed25519.c
	port/linux/src/lang.c"
mode=${1:-test}
if [ "$mode" = seed ]; then
	# (run_menus_test.sh's FRIENDS cases: a save folder's friends, made in $2)
	$clang --target=i686-linux-gnu -m32 -malign-double -freg-struct-return -O2 $game_flags -Wl,--gc-sections \
		-Wl,--unresolved-symbols=ignore-in-object-files -o "$out/p2p_friends_seed" "$here/p2p_friends_test.c" $crypto -lpthread
	mkdir -p "$2"
	FRIENDS_TEST_SEED_DIR="$2" "$out/p2p_friends_seed"
	exit $?
fi
if [ "$mode" = fuzz ]; then
	runs=${2:-300000}
	${FUZZ_CLANG:-clang} -m32 -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined $game_flags \
		-Wl,--unresolved-symbols=ignore-in-object-files -o "$out/p2p_friends_fuzz" "$here/p2p_friends_fuzz.c" $crypto \
		-lpthread
	mkdir -p "$out/fuzz"
	(cd "$out/fuzz" && FRIENDS_TEST_ROOT="$out/fuzz" "$out/p2p_friends_fuzz" "$runs")
	exit $?
fi
status=0
$clang --target=i686-linux-gnu -m32 -malign-double -freg-struct-return -O2 $game_flags -Wl,--gc-sections \
	-Wl,--unresolved-symbols=ignore-in-object-files -o "$out/p2p_friends_test" "$here/p2p_friends_test.c" $crypto -lpthread
mkdir -p "$out/plain"
FRIENDS_TEST_ROOT="$out/plain" "$out/p2p_friends_test" > "$out/plain.log" 2>&1 || status=1
grep -v '^  log:' "$out/plain.log"
${FUZZ_CLANG:-clang} -m32 -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined $game_flags \
	-Wl,--unresolved-symbols=ignore-in-object-files \
	-o "$out/p2p_friends_test_san" "$here/p2p_friends_test.c" $crypto -lpthread
mkdir -p "$out/san"
echo "--- with ASan and UBSan"
FRIENDS_TEST_ROOT="$out/san" "$out/p2p_friends_test_san" > "$out/san.log" 2>&1 || status=1
grep -v '^  log:' "$out/san.log"
exit $status
