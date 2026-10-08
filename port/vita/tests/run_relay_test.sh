#!/bin/sh
# The relay of internet play (port/relay: relay.c, main.c), on the host:
#   - built as it ships (its Makefile's warnings as errors, hardened);
#   - its checks (port/relay/tests/relay_test.c: cookies, pairing,
#     forwarding, the sides' binding, limits, rates, lapsing, no
#     amplification), plain and with ASan and UBSan;
#   - its packet reader fuzzed with libFuzzer, ASan and UBSan
#     (relay_fuzz.c) for RELAY_FUZZ_SECONDS (default 60) from the cases kept
#     in port/relay/tests/fuzz_cases, which are replayed first; what it finds
#     goes to RELAY_FUZZ_OUT (default a folder under TMPDIR, kept on failure).
# Needs clang with libFuzzer (CLANG, default clang).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
relay=$root/port/relay
out=${RELAY_FUZZ_OUT:-${TMPDIR:-/tmp}/relay_test.$$}
clang=${CLANG:-clang}
seconds=${RELAY_FUZZ_SECONDS:-60}
mkdir -p "$out"
sources="$relay/relay.c $root/port/third_party/monocypher/monocypher.c"
flags="-std=c11 -g -Wall -Wextra -Werror -I$root/port/third_party/monocypher"

make -s -C "$relay" CC="$clang" halo-relay
"$relay/halo-relay" --help > /dev/null
rm -f "$relay/halo-relay"
"$clang" $flags -O2 -o "$out/relay_test" "$relay/tests/relay_test.c" $sources
"$out/relay_test"
"$clang" $flags -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -o "$out/relay_test_asan" \
	"$relay/tests/relay_test.c" $sources
"$out/relay_test_asan"
"$clang" $flags -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -o "$out/relay_fuzz" \
	"$relay/tests/relay_fuzz.c" $sources
"$out/relay_fuzz" "$relay"/tests/fuzz_cases/* > "$out/replay.log" 2>&1 || { tail -30 "$out/replay.log"; exit 1; }
echo "relay fuzz: the kept cases pass"
if [ "$seconds" -gt 0 ]; then
	mkdir -p "$out/corpus"
	(cd "$out" && ./relay_fuzz -max_total_time="$seconds" -max_len=4096 -print_final_stats=1 corpus \
		"$relay/tests/fuzz_cases" > fuzz.log 2>&1) || { tail -40 "$out/fuzz.log"; echo "relay fuzz: FAILED ($out)"; exit 1; }
	grep -E "^stat::number_of_executed_units" "$out/fuzz.log" | sed 's/^/relay fuzz: /'
fi
rm -rf "$out"
echo "relay test: PASS"
