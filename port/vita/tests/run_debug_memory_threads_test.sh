#!/bin/sh
# Builds and runs the test of the game's allocator from several threads at
# once (debug_memory_threads_test.c: source/cseries/debug_memory.c, compiled
# as the Linux harness compiles the game, with assertions on): a Custom
# Edition map's bitmaps rebuilt on the cache file thread while the game's
# thread allocated lost blocks from the allocator's list, and a joining Vita
# crashed as the map loaded. Several rounds, each a few seconds of threads
# allocating, reallocating and freeing as fast as they can.
#   CC                         a clang that targets i686 (default clang)
#   HALO_TEST_ROUNDS           rounds (5)
#   HALO_TEST_THREADS          threads a round (8)
#   HALO_TEST_ROUND_SECONDS    seconds a round (2)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/debug_memory_threads_test.$$
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT
cc=${CC:-clang}
rounds=${HALO_TEST_ROUNDS:-5}
threads=${HALO_TEST_THREADS:-8}
seconds=${HALO_TEST_ROUND_SECONDS:-2}
cd "$root"
# (the game's file-scope struct tags, from the headers the allocator sees)
python3 tools/linux_msvc_semantics.py --output "$out/msvc_semantics.h" --tags port/include/xdk --tags source/cseries \
	--tags source/memory --tags source/math > /dev/null
# (the game's flags: gnu89, the prefix header, assertions on)
$cc --target=i686-linux-gnu -m32 -fms-extensions -fshort-wchar -malign-double -fcommon -fno-strict-aliasing -fwrapv \
	-fno-delete-null-pointer-checks -freg-struct-return -O2 -g -std=gnu89 -D__STRICT_ANSI__ -w -DDEBUG -Dxbox \
	-include port/linux/include/halo_linux_prefix.h -include "$out/msvc_semantics.h" \
	-Iport/linux/include -Isource -Isource/cseries -Isource/memory -Isource/math -idirafter port/include/xdk \
	-ffunction-sections -fdata-sections -Wl,--gc-sections \
	"$here/debug_memory_threads_test.c" -lpthread -o "$out/test"
round=1
while [ "$round" -le "$rounds" ]; do
	"$out/test" "$threads" "$seconds" || { echo "FAIL: round $round of $rounds"; exit 1; }
	round=$((round + 1))
done
echo "PASS: $rounds rounds"
