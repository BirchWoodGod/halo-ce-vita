#!/bin/sh
# A quit player's stale handle (stale_player_test.c, network_game_manager.c's
# network_game_player_forget, units.c's unit_record_damage): the objects
# forget him when his slot is reused, and damage naming a handle no player
# has is recorded with none, rather than the process ended. Built as the
# game is (build.ninja's flags for each file, DEBUG) with AddressSanitizer
# and UBSan.
# Needs the Linux build configured in this tree (build.ninja) and clang that
# targets i686 (CLANG, default the Linux build's compiler).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/stale_player_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
clang=${CLANG:-$(sed -n 's/^linux_cc = //p' "$root/build.ninja" | head -1)}
[ -n "$clang" ] || clang=clang
mkdir -p "$out"
cd "$root"
sanitize="-fsanitize=address,undefined -fno-sanitize=alignment,function -fno-sanitize-recover=all -fno-omit-frame-pointer"
# game_flags OBJECT: build.ninja's cflags for build/linux/obj/OBJECT
game_flags() {
	awk -v edge="build build/linux/obj/$1:" 'index($0, edge) == 1 { found = 1 }
		found && /^  cflags =/ { line = 1 } line { printf "%s ", $0; if ($0 !~ /\$$/) exit }' build.ninja |
		sed 's/^ *cflags = //; s/\$ *//g; s/-O2/-O1/; s/-march=native//'
}
flags=$(game_flags source/networking/network_game_manager.o)
[ -n "$flags" ] || { echo "FAIL no Linux build configured in $root (build.ninja)"; exit 1; }
status=0
eval "\"$clang\" $flags -ffunction-sections -fdata-sections -Iport/linux/src $sanitize \
	-c port/vita/tests/stale_player_test.c -o \"$out/stale_player_test.o\""
for source in source/units/units.c source/game/players.c source/memory/data.c; do
	object=${source%.c}.o
	flags=$(game_flags "$object")
	eval "\"$clang\" $flags -ffunction-sections -fdata-sections -Iport/linux/src $sanitize \
		-c \"$source\" -o \"$out/$(basename "$object")\""
done
# (the rest of the files' references, never called here, are only warned of)
$clang -m32 $sanitize -no-pie -Wl,--gc-sections -Wl,--warn-unresolved-symbols "$out/stale_player_test.o" \
	"$out/units.o" "$out/players.o" "$out/data.o" -lm -lpthread -o "$out/stale_player_test" 2>"$out/link.txt"
"$out/stale_player_test" || status=1
test_out_done $status
exit $status
