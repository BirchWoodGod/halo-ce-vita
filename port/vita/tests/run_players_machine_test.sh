#!/bin/sh
# The players a machine has (players_machine_test.c, source/game/players.c):
# a machine's fifth player, or one of a machine outside the table, refused
# rather than the process ended. Built as the game is (build.ninja's flags
# for players.c, DEBUG) with AddressSanitizer and UBSan.
# Needs the Linux build configured in this tree (build.ninja) and clang that
# targets i686 (CLANG, default the Linux build's compiler).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/players_machine_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
clang=${CLANG:-$(sed -n 's/^linux_cc = //p' "$root/build.ninja" | head -1)}
[ -n "$clang" ] || clang=clang
mkdir -p "$out"
cd "$root"
sanitize="-fsanitize=address,undefined -fno-sanitize=alignment,function -fno-sanitize-recover=all -fno-omit-frame-pointer"
game_flags=$(awk '/^build build\/linux\/obj\/source\/game\/players.o:/ { found = 1 }
	found && /^  cflags =/ { line = 1 } line { printf "%s ", $0; if ($0 !~ /\$$/) exit }' build.ninja |
	sed 's/^ *cflags = //; s/\$ */ /g; s/-O2/-O1/; s/-march=native//')
[ -n "$game_flags" ] || { echo "FAIL no Linux build configured in $root (build.ninja)"; exit 1; }
game_flags="$game_flags -ffunction-sections -fdata-sections -Iport/linux/src"
status=0
for source in port/vita/tests/players_machine_test.c source/memory/data.c; do
	eval "\"$clang\" $game_flags $sanitize -c \"$source\" -o \"$out/$(basename "$source" .c).o\""
done
# (the rest of players.c's references, never called here, are only warned of)
$clang -m32 $sanitize -no-pie -Wl,--gc-sections -Wl,--warn-unresolved-symbols "$out/players_machine_test.o" \
	"$out/data.o" -lm -lpthread -o "$out/players_machine_test" 2>"$out/link.txt"
"$out/players_machine_test" || status=1
test_out_done $status
exit $status
