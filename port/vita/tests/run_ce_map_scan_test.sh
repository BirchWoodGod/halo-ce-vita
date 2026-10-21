#!/bin/bash
# The maps folder as players fill it, at the start and in the map picker
# (Oct 21 2026: M4J0R_l-l0LL4ND's Vita went down at each start after maps from
# the internet were added - among them "h3 foundry.map" and a folder of its
# development files put in maps/). On the Linux harness (gxm-null), Menu style
# Halo 3, Custom Edition on, the test's own maps folder: the data's maps, then
#   - a Custom Edition map (HALO_TEST_CE_MAP) as "h3 foundry.map" (a space),
#   - a folder "Foundry Supplementary Dev Files" with a map and a folder in it,
#   - a folder named dirmap.map,
#   - damaged copies of the map: cut at nine points, the header's file size,
#     tag data offset and size, scenario type and version set huge or zero,
#     the tag index's pointer and count garbage, bytes of the tag data and of
#     the whole file overwritten at random, 32-bit fields made huge.
# Two runs: idle at the main menu, and the multiplayer lobby's map picker
# scrolled to its end and a custom map chosen. Each must exit cleanly with
# no assertion or exception; the folder named as a map is left out, said.
#
#   run_ce_map_scan_test.sh
#   HALO_TEST_VITA         the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA_MENUS   a folder with the game's maps folder
#   HALO_TEST_PROFILE      a profile's blam.sav
#   HALO_TEST_CE_MAP       a Custom Edition map (default ../custom-maps-dl/extinction.map); never committed
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
profile=${HALO_TEST_PROFILE:-$root/../vita-save-backups/20261017-halo-profile/blam-unlocked.sav}
ce_map=${HALO_TEST_CE_MAP:-$root/../custom-maps-dl/extinction.map}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_ce_map_scan_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
for need in "$data/maps/ui.map" "$data/maps/bitmaps.map" "$profile" "$ce_map"; do
	if [ ! -r "$need" ]; then
		echo "SKIP: $need is not there"
		exit 0
	fi
done

mkdir -p "$out/maps/Foundry Supplementary Dev Files/sub folder" "$out/maps/dirmap.map"
for map in "$data"/maps/*; do
	ln -sfn "$(readlink -f "$map")" "$out/maps/$(basename "$map")"
done
ln -sfn "$(readlink -f "$ce_map")" "$out/maps/h3 foundry.map"
ln -sfn "$(readlink -f "$ce_map")" "$out/maps/Foundry Supplementary Dev Files/inner.map"
echo "notes" > "$out/maps/Foundry Supplementary Dev Files/readme.txt"
python3 -I - "$(readlink -f "$ce_map")" "$out/maps" <<'EOF'
import random, struct, sys
source, folder = sys.argv[1], sys.argv[2]
data = open(source, 'rb').read()
tag_offset, tag_size = struct.unpack_from('<II', data, 0x10)
def put(name, content):
    with open('%s/%s.map' % (folder, name), 'wb') as f:
        f.write(content)
for cut in (0, 16, 0x800, 0x801, tag_offset + 0x10, tag_offset + 0x30, tag_offset + tag_size // 2, len(data) // 2, len(data) - 1):
    put('fzcut%x' % cut, data[:cut])
def field(offset, value, name):
    copy = bytearray(data)
    struct.pack_into('<I', copy, offset, value & 0xffffffff)
    put('fz' + name, bytes(copy))
for offset, value, name in ((0x08, 0x7fffffff, 'sizebig'), (0x08, 0, 'size0'), (0x10, 0xfffffff0, 'tagofsbig'),
        (0x10, len(data) - 8, 'tagofsend'), (0x14, 0x7fffffff, 'tagsizebig'), (0x14, 0, 'tagsize0'), (0x60, 0xffff, 'type'),
        (0x04, 7, 'version'), (tag_offset, 0xffffffff, 'indexptr'), (tag_offset + 12, 0x7fffffff, 'indexcount'),
        (tag_offset + 12, 0x8000, 'indexcount8k')):
    field(offset, value, name)
rng = random.Random(1021)
for n in range(4):
    copy = bytearray(data)
    low, high = (tag_offset, tag_offset + tag_size) if n < 2 else (0x800, len(data))
    for _ in range(2000):
        copy[rng.randrange(low, high)] = rng.randrange(256)
    put('fzrandom%d' % n, bytes(copy))
for n in range(2):
    copy = bytearray(data)
    for _ in range(500):
        struct.pack_into('<I', copy, rng.randrange(tag_offset, tag_offset + tag_size - 4) & ~3,
            rng.choice((0xffffffff, 0x7fffffff, 0x10000, 0x40000000, 0x80000000)))
    put('fzhuge%d' % n, bytes(copy))
EOF

# run NAME SECONDS PAD
run() {
	local name=$1 seconds=$2 pad=$3 dir="$out/$1"
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save/u/UDATA/869079979327"
	ln -sfn "$out/maps" "$dir/data/maps"
	cp "$profile" "$dir/save/u/UDATA/869079979327/blam.sav"
	(cd "$dir" && exec unshare -rn env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false HALO_UI_LOG=1 \
		HALO_MENU_STYLE=halo3 HALO_MENUS_FOLDER="$root/port/vita/app0/menus/" HALO_CUSTOM_EDITION=1 "HALO_TEST_PAD=$pad" \
		timeout -k 5 $((seconds + 180)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
}
checked() {
	local code
	code=$(cat "$out/$1/exit" 2>/dev/null)
	[ "$code" = 0 ] || fail "$1: the harness exited with $code ($out/$1/run.log)"
	grep -av "render assertion skipped" "$out/$1/run.log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" &&
		fail "$1: an assertion or exception ($out/$1/run.log)"
	return 0
}
expect() {
	if grep -aqE "$3" "$out/$1/run.log"; then echo "  ok: $1: $2"; else fail "$1: $2 ($out/$1/run.log)"; fi
}
steps() { local n=$1 key=$2 p="" i; for i in $(seq "$n"); do p="$p $key:150:300"; done; echo "$p"; }

echo "--- idle"
run idle 60 "wait:150:1000"
checked idle
expect idle "the folder named as a map left out, said" "player models: dirmap.map: a folder, or not there, not a map file: left out"
expect idle "the map with a space read" "player models: .*of h3 foundry.map|player models: h3 foundry.map:"
expect idle "the start reached the main menu" "ui: screen pc.h3.main.screen"

echo "--- picker"
run picker 90 "wait:150:3000 down:150:700 a wait:150:6000 down:150:500 down:150:500 a:150:2000 up:150:600 a:150:1000 $(steps 60 down) a:150:2000 wait:150:2000 b:150:3000"
checked picker
expect picker "the map picker lists the custom maps" "menus: h3 mp: the map picker: [0-9]+ maps \([1-9][0-9]* custom\)"
expect picker "a custom map chosen" "menus: h3 mp: map .*a custom map"

test_out_done $status
[ $status = 0 ] && echo "PASS"
exit $status
