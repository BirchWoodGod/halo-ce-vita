#!/bin/sh
# The network fuzz targets (net_fuzz_*.c): what other machines and the
# public signalling brokers send, read by
#   p2p        internet play's tunnel, streams (KCP), STUN, invites, codes
#              (port/linux/src/p2p.c)
#   signal     MQTT 5 and 3.1.1, JOIN/ACCEPT, code records, the server
#              browser's slots and queries (p2p_signal.c)
#   messages   the game's message decoder (source/networking/
#              network_messages.c, source/memory/data_packet*.c)
#   map_share  map sharing's message rules (port/linux/game/map_share_protocol.c)
# Each is built 32-bit as the game is, with AddressSanitizer and UBSan, and
# runs its checks and the cases kept in net_fuzz_cases/<target> (inputs that
# found bugs, and seeds); any out-of-bounds access, overflow or failed
# assertion fails the run.
#   NET_FUZZ_SECONDS=n   also fuzzes each target for n seconds with libFuzzer
#                        (clang's i386 runtime, its COMDAT groups taken out
#                        when it does not link as it is); what it finds goes
#                        to NET_FUZZ_OUT (default a folder under TMPDIR)
#   NET_FUZZ_TARGETS     the targets (default all four)
# Needs the Linux build configured in this tree (build.ninja: its compiler and
# the game's flags; build/linux/halo_msvc_semantics.h) and clang that targets
# i686 (CLANG, default the Linux build's compiler).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/net_fuzz_test.$$
clang=${CLANG:-$(sed -n 's/^linux_cc = //p' "$root/build.ninja" | head -1)}
[ -n "$clang" ] || clang=clang
targets=${NET_FUZZ_TARGETS:-"p2p signal messages map_share"}
seconds=${NET_FUZZ_SECONDS:-0}
mkdir -p "$out"
cd "$root"
sanitize="-fsanitize=address,undefined -fno-sanitize=alignment,function -fno-sanitize-recover=all -fno-omit-frame-pointer"
# the platform layer's flags (run_vita_p2p_test.sh), with HALO_VITA
platform_flags="--target=i686-linux-gnu -m32 -fms-extensions -fshort-wchar -malign-double -fcommon -fno-pic
	-fno-strict-aliasing -fwrapv -freg-struct-return -ffunction-sections -fdata-sections -O1 -g -std=gnu11 -D_GNU_SOURCE
	-DHALO_LINUX_PLATFORM_LAYER -DHALO_VITA -w -include port/linux/include/halo_linux_prefix.h
	-Iport/linux/src -Iport/linux/include -Iport/linux/game -Iport/third_party/kcp -Iport/third_party/monocypher
	-Isource -Isource/cseries
	-idirafter port/include/xdk"
# (one line: the commands are built with eval, for the game's quoted include
# paths)
platform_flags=$(echo $platform_flags)
# the game's own (network_messages.c's in build.ninja), DEBUG: its assertions fatal
game_flags=$(awk '/^build build\/linux\/obj\/source\/networking\/network_messages.o:/ { found = 1 }
	found && /^  cflags =/ { line = 1 } line { printf "%s ", $0; if ($0 !~ /\$$/) exit }' build.ninja |
	sed 's/^ *cflags = //; s/\$ */ /g; s/-O2/-O1/; s/-march=native//')
[ -n "$game_flags" ] || { echo "FAIL no Linux build configured in $root (build.ninja)"; exit 1; }
game_flags="$game_flags -ffunction-sections -fdata-sections -Iport/linux/src"

# libFuzzer for i386: as it is, or with its objects' COMDAT groups taken out
# (clang 22's archive refers to __x86.get_pc_thunk sections the linker
# discards)
libfuzzer=
if [ "$seconds" != 0 ]; then
	echo 'int LLVMFuzzerTestOneInput(const char *d, unsigned long n) { return 0; }' > "$out/probe.c"
	if $clang -m32 -fsanitize=fuzzer "$out/probe.c" -o "$out/probe" 2>/dev/null; then
		libfuzzer=-fsanitize=fuzzer
	else
		archive=$($clang -m32 -print-file-name=libclang_rt.fuzzer-i386.a 2>/dev/null)
		[ -f "$archive" ] || archive=$($clang -print-resource-dir)/lib/linux/libclang_rt.fuzzer-i386.a
		mkdir -p "$out/libfuzzer" && (cd "$out/libfuzzer" && ar x "$archive" && for object in *.o; do
			objcopy --remove-section=.group $(for r in ax bx cx dx si di bp; do
				echo --localize-symbol=__x86.get_pc_thunk.$r; done) "$object"; done &&
			ar rcs ../libfuzzer32.a *.o)
		libfuzzer="$out/libfuzzer32.a -lstdc++"
	fi
fi

status=0
# (p2p_crypto.c's Ed25519 and Argon2, for the server browser)
monocypher="port/third_party/monocypher/monocypher.c port/third_party/monocypher/monocypher-ed25519.c"
for target in $targets; do
	case $target in
	p2p) flags=$platform_flags; sources="port/vita/tests/net_fuzz_p2p.c port/third_party/kcp/ikcp.c $monocypher" ;;
	signal) flags=$platform_flags; sources="port/vita/tests/net_fuzz_signal.c $monocypher" ;;
	map_share) flags=$platform_flags; sources="port/vita/tests/net_fuzz_map_share.c port/linux/src/p2p_crypto.c $monocypher" ;;
	messages) flags=$game_flags; sources="port/vita/tests/net_fuzz_messages.c source/memory/data_packet_groups.c
		source/memory/data_packets.c source/memory/data_encoding.c source/memory/byte_swapping.c
		source/bungie_net/common/message_header.c" ;;
	*) echo "FAIL unknown target $target"; status=1; continue ;;
	esac
	objects=; fuzz_objects=
	for source in $sources; do
		object="$out/$target.$(basename "$source" .c)"
		extra=
		# (KCP's queue macros take offsetof from a null pointer, which UBSan
		# reports; ASan still watches it)
		case $source in *ikcp.c) extra=-fno-sanitize=undefined ;; esac
		eval "\"$clang\" $flags $sanitize $extra -c \"$source\" -o \"$object.o\""
		objects="$objects $object.o"
		if [ -n "$libfuzzer" ]; then
			eval "\"$clang\" $flags $sanitize $extra -fsanitize=fuzzer-no-link -c \"$source\" -o \"$object.fuzz.o\""
			fuzz_objects="$fuzz_objects $object.fuzz.o"
		fi
	done
	$clang -m32 $sanitize -g -c "$here/net_fuzz_main.c" -o "$out/main.o"
	# (the XDK headers' inline functions stay in the objects: the D3D ones'
	# references are never called, and only warned of)
	$clang -m32 $sanitize -no-pie -Wl,--gc-sections -Wl,--warn-unresolved-symbols $objects "$out/main.o" -lm -lpthread \
		-o "$out/$target" 2>"$out/$target.link.txt"
	echo "--- $target"
	"$out/$target" "$here/net_fuzz_cases/$target" || status=1
	if [ -n "$libfuzzer" ]; then
		found=${NET_FUZZ_OUT:-$out/found}/$target
		mkdir -p "$found/corpus"
		$clang -m32 $sanitize -no-pie -Wl,--gc-sections -Wl,--warn-unresolved-symbols $fuzz_objects $libfuzzer -lm \
			-lpthread -o "$out/$target.fuzz" 2>>"$out/$target.link.txt"
		"$out/$target.fuzz" -max_total_time="$seconds" -max_len=8192 -print_final_stats=1 \
			-artifact_prefix="$found/" "$found/corpus" "$here/net_fuzz_cases/$target" > "$found/fuzz.log" 2>&1 || status=1
		grep -E 'stat::number_of_executed_units|SUMMARY' "$found/fuzz.log" || true
		ls "$found" | grep -E '^(crash|leak|timeout|oom)-' && status=1
	fi
done
[ $status = 0 ] && echo "PASS network fuzz targets: checks and kept cases" || echo "FAIL (in $out)"
[ $status = 0 ] && rm -rf "$out"
exit $status
