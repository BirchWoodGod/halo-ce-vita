#!/bin/sh
# The host's anti-cheat (network_anticheat_test.c, port/linux/game/
# network_anticheat.c): its scores, decay, drops, what never drops (a poor
# link's refusals, a LAN's datagrams), the machines kept out and let in, the
# host's KICK and BAN. Built as the game is (build.ninja's flags for
# network_anticheat.c, DEBUG), plain and with AddressSanitizer and UBSan.
# Needs the Linux build configured in this tree (build.ninja) and clang that
# targets i686 (CLANG, default the Linux build's compiler).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/network_anticheat_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
clang=${CLANG:-$(sed -n 's/^linux_cc = //p' "$root/build.ninja" | head -1)}
[ -n "$clang" ] || clang=clang
mkdir -p "$out"
cd "$root"
sanitize="-fsanitize=address,undefined -fno-sanitize=alignment,function -fno-sanitize-recover=all -fno-omit-frame-pointer"
game_flags=$(awk '/^build build\/linux\/obj\/port\/linux\/game\/network_anticheat.o:/ { found = 1 }
	found && /^  cflags =/ { line = 1 } line { printf "%s ", $0; if ($0 !~ /\$$/) exit }' build.ninja |
	sed 's/^ *cflags = //; s/\$ */ /g; s/-O2/-O1/; s/-march=native//')
[ -n "$game_flags" ] || { echo "FAIL no Linux build configured in $root (build.ninja)"; exit 1; }
game_flags="$game_flags -Iport/linux/src"
status=0
for variant in plain sanitized; do
	flags=
	[ "$variant" = sanitized ] && flags=$sanitize
	mkdir -p "$out/$variant"
	for source in port/vita/tests/network_anticheat_test.c port/linux/game/network_anticheat.c; do
		eval "\"$clang\" $game_flags $flags -c \"$source\" -o \"$out/$variant/$(basename "$source" .c).o\""
	done
	$clang -m32 $flags -no-pie "$out/$variant/network_anticheat_test.o" "$out/$variant/network_anticheat.o" -lm \
		-o "$out/$variant/network_anticheat_test"
	[ "$variant" = sanitized ] && echo "--- with ASan and UBSan"
	"$out/$variant/network_anticheat_test" || status=1
done
test_out_done $status
exit $status
