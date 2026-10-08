#!/bin/sh
# Builds and runs the tests of the Custom Edition installer extraction
# (port/linux/src/posix_ce_installer.c, with port/third_party/libmspack),
# each with AddressSanitizer and UBSan, 32-bit as the Vita and the Linux
# game are where the compiler can (else as the host is):
#   ce_installer_test.c  made-up installers: extracted, refused, every
#                        truncation and header byte changed
#   ce_installer_fuzz.c  CE_FUZZ_ITERATIONS (default 20000) changed
#                        installers and LZX streams from a fixed seed; with
#                        CE_FUZZ_SECONDS=n, also n seconds of libFuzzer
#                        (64-bit; clang), from the seeds in CE_FUZZ_SEEDS if
#                        given (a folder), what it finds in CE_FUZZ_OUT
#   tools/ce_installer_extract.c  built alone, as its header says
# With CE_INSTALLER=<a Halo Custom Edition installer> (the player's own; not
# in this repository), also: the three maps out of it with the tool, their
# SHA-256 against the English 1.00 installer's (and, with
# CE_INSTALLER_COMPARE=<a folder holding the three>, against those), then
# copies of it cut short and damaged (in CE_INSTALLER_WORK, default a folder
# under TMPDIR: 180 MB at a time), which must fail cleanly.
#   CC  the compiler (default clang if there is one, else cc)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/ce_installer_test.$$
cc=${CC:-$(command -v clang >/dev/null 2>&1 && echo clang || echo cc)}
mkdir -p "$out"
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
# (libmspack's own configuration: config.h's few)
mspack="-I$root/port/third_party/libmspack -DHAVE_INTTYPES_H=1"
sources="$root/port/linux/src/posix_ce_installer.c $root/port/linux/src/lang.c $root/port/third_party/libmspack/cabd.c
	$root/port/third_party/libmspack/lzxd.c $root/port/third_party/libmspack/mszipd.c
	$root/port/third_party/libmspack/qtmd.c $root/port/third_party/libmspack/system.c"
flags="-g -O1 -Wall -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -I$root/port/linux/src $mspack"
bits=-m32
echo 'int main(void) { return 0; }' > "$out/probe.c"
$cc $bits $sanitize "$out/probe.c" -o "$out/probe" 2>/dev/null && "$out/probe" || bits=
export ASAN_OPTIONS=detect_leaks=1:exitcode=99 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:exitcode=98
status=0

$cc $bits $sanitize $flags "$here/ce_installer_test.c" $sources -o "$out/ce_installer_test"
(cd "$out" && TMPDIR="$out" ./ce_installer_test) || status=1
$cc $bits $sanitize $flags "$here/ce_installer_fuzz.c" $sources -o "$out/ce_installer_fuzz"
(cd "$out" && TMPDIR="$out" ./ce_installer_fuzz) || status=1
$cc $bits $sanitize $flags "$root/tools/ce_installer_extract.c" $sources -o "$out/ce_installer_extract"

seconds=${CE_FUZZ_SECONDS:-0}
if [ "$seconds" != 0 ]; then
	found=${CE_FUZZ_OUT:-$out/found}
	mkdir -p "$found/corpus"
	clang -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -DCE_FUZZ_LIBFUZZER $flags -Wno-unused-function \
		"$here/ce_installer_fuzz.c" $sources -o "$out/ce_installer_libfuzzer"
	(cd "$out" && TMPDIR="$out" ./ce_installer_libfuzzer -max_total_time="$seconds" -max_len=262144 \
		-print_final_stats=1 -artifact_prefix="$found/" "$found/corpus" ${CE_FUZZ_SEEDS:+"$CE_FUZZ_SEEDS"} \
		> "$found/fuzz.log" 2>&1) || status=1
	grep -E 'stat::number_of_executed_units|stat::peak_rss|cov:.*ft:' "$found/fuzz.log" | tail -3 || true
	ls "$found" | grep -E '^(crash|leak|timeout|oom)-' && status=1
fi

if [ -n "$CE_INSTALLER" ]; then
	work=${CE_INSTALLER_WORK:-$out/work}
	mkdir -p "$work/maps"
	"$out/ce_installer_extract" "$CE_INSTALLER" "$work/maps" > /dev/null 2>&1 || { echo "FAIL extraction from $CE_INSTALLER"; status=1; }
	# (the English 1.00 installer's, halocesetup_en_1.00.exe, SHA-256
	# 011b03b61634561d7c1b6090e7229fe0a2df40de2c2a14b45ac8f0455d5d990f; the
	# same as Halo MCC's halo1/maps/custom_edition copies)
	expected="bitmaps.map 317658e0ebd4aa189bac68e994fe0b515928f44d17b62ba676d8850aa329dd3d
sounds.map 81658a8d9b127b2771558a35e664ec3f1a2b370f271fe957426d945001ceb5d2
loc.map 1321e17b5a86be84b1930b6eb2e7fdfe9285275d5d54cbe3d4a3635e2bf04041"
	echo "$expected" | while read -r name sum; do
		got=$(sha256sum "$work/maps/$name" 2>/dev/null | cut -d' ' -f1)
		if [ "$got" = "$sum" ]; then echo "  $name: the English 1.00 installer's"
		else echo "  $name: $got (not the English 1.00 installer's)"; fi
		if [ -n "$CE_INSTALLER_COMPARE" ]; then
			theirs=$(sha256sum "$CE_INSTALLER_COMPARE/$name" | cut -d' ' -f1)
			[ "$got" = "$theirs" ] && echo "  $name: the same as $CE_INSTALLER_COMPARE's" ||
				{ echo "FAIL $name differs from $CE_INSTALLER_COMPARE's"; exit 1; }
		fi
	done || status=1
	rm -f "$work/maps/"*.map
	# damaged copies: each must fail (exit 1) without a fault (99: ASan, 98:
	# UBSan) or a part-written map left (the maps before the damage, whole and
	# checked, stay), or extract all three (a change it did not read).
	# The offsets are the English 1.00 installer's: its program header's
	# pointer to the PE header (the cabinet is then looked for), the
	# cabinet (at 554420) - its folder count, folder 1's data offset,
	# bitmaps.map's size and name - and a byte of folders 1, 3 and 4's data
	# (bitmaps.map's, loc.map's and sounds.map's)
	size=$(stat -L -c %s "$CE_INSTALLER")
	for case in cut:1000 cut:554500 cut:600000 cut:$((size / 2)) cut:$((size - 1)) \
		byte:60:0x7f byte:554446:0xff byte:554464:0x00 byte:556589:0x7f byte:556607:0x78 \
		byte:18865351:0x55 byte:91013551:0xaa byte:124698737:0x00; do
		kind=${case%%:*}; rest=${case#*:}
		copy="$work/damaged.exe"
		if [ "$kind" = cut ]; then
			head -c "$rest" "$CE_INSTALLER" > "$copy"
		else
			offset=${rest%%:*}; value=${rest#*:}
			cp "$CE_INSTALLER" "$copy"
			printf "$(printf '\\%03o' "$value")" | dd of="$copy" bs=1 seek="$offset" conv=notrunc 2>/dev/null
		fi
		result=0
		"$out/ce_installer_extract" "$copy" "$work/maps" > "$work/damaged.txt" 2>&1 || result=$?
		partial=$(ls "$work/maps" | grep -c '\.extract$' || true)
		kept=$(ls "$work/maps" | grep -v '\.extract$' | tr '\n' ' ')
		reason=$(tail -1 "$work/damaged.txt" | tr -d '\r')
		if [ $result = 1 ] && [ "$partial" = 0 ]; then echo "  $case: refused: $reason${kept:+ (kept: $kept)}"
		elif [ $result = 0 ]; then echo "  $case: extracted (the change missed what it reads)"
		else echo "FAIL $case: exit $result, $partial part-written"; tail -3 "$work/damaged.txt"; status=1; fi
		rm -f "$work/maps/"*
	done
	rm -f "$work/damaged.exe" "$work/damaged.txt"
	rmdir "$work/maps" 2>/dev/null || true
	[ -z "$CE_INSTALLER_WORK" ] || rmdir "$work" 2>/dev/null || true
fi

[ $status = 0 ] && echo "PASS Custom Edition installer extraction" || echo "FAIL (in $out)"
[ $status = 0 ] && rm -rf "$out"
exit $status
