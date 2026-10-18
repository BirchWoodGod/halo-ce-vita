#!/bin/bash
# Scene packs (port/linux/game/scene_pack.c, scene_packs.c; recipes in
# port/vita/app0/packs) on the Linux harness:
#
#   tool      build/linux/scene_pack_test alone: menu_spartan built from
#             bloodgulch and loaded as the game loads a pack, then fuzzed
#             (HALO_TEST_FUZZ iterations, 2000 by default: cut short, grown,
#             bytes changed with and without their CRCs made right, pointers
#             and counts made wrong, header fields changed): every copy
#             refused or, still a pack, loaded clean twice, never a crash;
#             covenant_vehicles (with its sounds) built from b40 and fuzzed
#             (a quarter as many)
#   damaged   a pack built by the game, then damaged three ways between two
#             runs (a byte of its tags changed, its CRCs made right; cut
#             short; a byte changed, CRCs left): each refused, deleted, built
#             again and loaded
#   stale     the recipe changed between two runs: the pack built again
#   banshee   the Covenant's vehicles from b40 in Blood Gulch: Forge places a
#             Banshee from the pack, the player rides it, flies it up and
#             fires (its projectiles counted)
#   ai        Covenant characters from b40 in Blood Gulch: Forge places an
#             elite from the pack, which shoots the player (shield and body
#             go down)
#
# Each harness run must exit by itself, with no assertion or exception.
#
#   run_scene_pack_test.sh [CASE...]   (default: tool damaged stale banshee ai)
#   HALO_TEST_VITA         the harness (default build/linux/halo of this tree)
#   HALO_TEST_PACK_TOOL    the tool (default build/linux/scene_pack_test)
#   HALO_TEST_DATA_MENUS   a folder with the game's maps folder (the Xbox maps,
#                          never in the repository: skipped without bloodgulch
#                          and b40)
#   HALO_TEST_OUT          where the logs go (deleted after a pass when the test
#                          made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
tool=$(readlink -f "${HALO_TEST_PACK_TOOL:-$root/build/linux/scene_pack_test}")
data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_scene_pack_test.$$}
recipes=$root/port/vita/app0/packs
export HALO_MENUS_FOLDER=${HALO_MENUS_FOLDER:-$root/port/vita/app0/menus/}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-tool damaged stale banshee ai}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ] || [ ! -x "$tool" ]; then
	echo "no harness at $binary or no tool at $tool"
	exit 2
fi
if [ ! -r "$data/maps/bloodgulch.map" ] || [ ! -r "$data/maps/b40.map" ]; then
	echo "SKIP: no bloodgulch and b40 in $data/maps (HALO_TEST_DATA_MENUS)"
	exit 0
fi
mkdir -p "$out"

# run NAME SECONDS COMMANDS PAD [VAR=value...]: the harness from the main menu
# (the commands' "95:map_name ..." starts the map), its save folder kept
# between runs of the same NAME unless FRESH=1
run() {
	local name=$1 seconds=$2 commands=$3 pad=$4 dir="$out/$1"
	shift 4
	[ "${FRESH:-1}" = 1 ] && rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save" "$dir/shots"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_SCREENSHOT_DIR="$dir/shots" HALO_TEST_COMMANDS="$commands" "HALO_TEST_PAD=$pad" "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
}

checked() {
	local log="$out/$1/run.log" code
	code=$(cat "$out/$1/exit" 2>/dev/null)
	if [ "$code" != 0 ]; then
		fail "$1: the harness exited with $code ($log)"
		return 1
	fi
	if grep -av "render assertion skipped" "$log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
		fail "$1: an assertion or exception ($log)"
		return 1
	fi
	return 0
}

expect() { # NAME TEXT PATTERN [LOG]
	if grep -aqE "$3" "${4:-$out/$1/run.log}"; then
		echo "  ok: $1: $2"
	else
		fail "$1: $2 (${4:-$out/$1/run.log})"
	fi
}

bloodgulch='90:game_variant slayer;95:map_name levels\test\bloodgulch\bloodgulch'

for c in $cases; do
	echo "--- $c"
	case $c in
	tool)
		mkdir -p "$out/tool"
		"$tool" build "$recipes/menu_spartan.txt" "$data/maps" "$out/tool/menu_spartan.pack" > "$out/tool/build.log" 2>&1 ||
			fail "tool: menu_spartan was not built ($out/tool/build.log)"
		grep -aE "^built|^loaded|groups" "$out/tool/build.log"
		expect tool "menu_spartan loads with no correction" "pack tags .*passed.* 0 corrections" "$out/tool/build.log"
		TMPDIR="$out/tool" "$tool" fuzz "$out/tool/menu_spartan.pack" "${HALO_TEST_FUZZ:-2000}" 1 > "$out/tool/fuzz.log" 2>&1 ||
			fail "tool: menu_spartan's fuzzing failed ($out/tool/fuzz.log)"
		tail -11 "$out/tool/fuzz.log"
		"$tool" build "$recipes/covenant_vehicles.txt" "$data/maps" "$out/tool/covenant_vehicles.pack" > "$out/tool/build2.log" 2>&1 ||
			fail "tool: covenant_vehicles was not built ($out/tool/build2.log)"
		grep -aE "^built" "$out/tool/build2.log"
		TMPDIR="$out/tool" "$tool" fuzz "$out/tool/covenant_vehicles.pack" $(( ${HALO_TEST_FUZZ:-2000} / 4 )) 2 > "$out/tool/fuzz2.log" 2>&1 ||
			fail "tool: covenant_vehicles' fuzzing failed ($out/tool/fuzz2.log)"
		tail -1 "$out/tool/fuzz2.log"
		rm -f "$out/tool/"*.pack ;;
	damaged)
		run damaged 14 "$bloodgulch;L60:pack load menu_spartan" ""
		checked damaged && expect damaged "built and loaded" "scene packs: menu_spartan: loaded"
		pack="$out/damaged/save/t/packs/menu_spartan.pack"
		for damage in tags cut raw; do
			[ -r "$pack" ] || { fail "damaged: no pack to damage"; break; }
			python3 - "$pack" "$damage" <<'EOF'
import sys, zlib, struct
path, damage = sys.argv[1], sys.argv[2]
data = bytearray(open(path, 'rb').read())
tags_offset, tags_size = struct.unpack_from('<II', data, 20)
data_offset, data_size = struct.unpack_from('<II', data, 28)
if damage == 'cut':
    data = data[:len(data) * 2 // 3]
else:
    # a word of the tag table's first tag's root pointer: made to point past the pack
    at = tags_offset + 36 + 20
    struct.pack_into('<I', data, at, 0x5C000000 + tags_size + 64)
    if damage == 'tags':
        struct.pack_into('<I', data, 44, zlib.crc32(bytes(data[tags_offset:tags_offset + tags_size])))
        struct.pack_into('<I', data, 124, 0)
        struct.pack_into('<I', data, 124, zlib.crc32(bytes(data[:128])))
open(path, 'wb').write(data)
EOF
			FRESH=0 run damaged 14 "$bloodgulch;L60:pack load menu_spartan" ""
			cp "$out/damaged/run.log" "$out/damaged/run-$damage.log"
			checked damaged && {
				expect damaged "$damage: refused" "scene packs: menu_spartan: refused|scene packs: menu_spartan: building it \(" "$out/damaged/run-$damage.log"
				expect damaged "$damage: built again and loaded" "scene packs: menu_spartan: loaded" "$out/damaged/run-$damage.log"
			}
			grep -aE "scene packs: menu_spartan: (refused|building|loaded)" "$out/damaged/run-$damage.log" | sed 's/^/    /'
		done ;;
	stale)
		mkdir -p "$out/stale_recipes"
		cp "$recipes/"*.txt "$out/stale_recipes/"
		run stale 14 "$bloodgulch;L60:pack load menu_spartan" "" HALO_PACKS_FOLDER="$out/stale_recipes/"
		checked stale && expect stale "built and loaded" "scene packs: menu_spartan: loaded"
		FRESH=0 run stale 14 "$bloodgulch;L60:pack load menu_spartan" "" HALO_PACKS_FOLDER="$out/stale_recipes/"
		checked stale && {
			grep -aq "scene packs: menu_spartan: building" "$out/stale/run.log" && fail "stale: built again with nothing changed"
			expect stale "the same recipe: the pack used as it is" "scene packs: menu_spartan: loaded"
		}
		echo "strip bitm characters\\cyborg\\bitmaps\\cubemap mirror surface" >> "$out/stale_recipes/menu_spartan.txt"
		FRESH=0 run stale 14 "$bloodgulch;L60:pack load menu_spartan" "" HALO_PACKS_FOLDER="$out/stale_recipes/"
		checked stale && expect stale "the recipe changed: built again" "menu_spartan: building it \(its recipe or builder changed\)" ;;
	banshee)
		cmds="$bloodgulch;L100:pack load covenant_vehicles;L110:forge on;L112:forge camera 60 -100 0.8 130 -5;L115:forge list vehicles"
		cmds="$cmds;L118:forge spawn banshee;L120:forge distance 6;L122:forge drop;L140:forge off;L170:pack ride;L175:pack where"
		for t in 220 280 340 400 460 520; do cmds="$cmds;L$t:pack census;L$((t+1)):pack where"; done
		cmds="$cmds;L180:@shot 00-banshee;L460:@shot 01-banshee-flying"
		run banshee 40 "$cmds" "wait:150:500 level:150:5000 lup+rup:3000:0 lup:2000:0 rt+lup:6000:0"
		if checked banshee; then
			expect banshee "the pack loaded beside Blood Gulch" "scene packs: covenant_vehicles: loaded"
			expect banshee "the Banshee, Wraith and Shade in Forge's palette" "forge: vehicles 0: Banshee"
			expect banshee "a Banshee placed" "forge: dropped Banshee"
			expect banshee "the player in it" "scene packs: ride: vehicles.banshee.banshee entered"
			high=$(grep -a "where: the vehicle at" "$out/banshee/run.log" | awk '{ if ($10 + 0 > max) max = $10 + 0 } END { print int(max) }')
			[ "${high:-0}" -ge 10 ] && echo "  ok: banshee: flown up ($high world units)" || fail "banshee: not flown up (highest $high)"
			fired=$(grep -a "census:" "$out/banshee/run.log" | awk '{ if ($5 + 0 > max) max = $5 + 0 } END { print max + 0 }')
			[ "${fired:-0}" -ge 1 ] && echo "  ok: banshee: fired ($fired projectiles at once)" || fail "banshee: no projectile fired"
		fi ;;
	ai)
		cmds="$bloodgulch;L100:pack load covenant_ai;L110:forge on;L112:forge camera 60 -100 1.6 130 0;L118:forge list characters"
		cmds="$cmds;L120:forge spawn elite major plasma rifle;L121:forge distance 7;L122:forge drop;L140:forge camera 60 -100 0.8 130 -5;L145:forge off"
		for t in 160 220 280 340 400 460 520; do cmds="$cmds;L$t:pack census;L$((t+1)):pack where"; done
		cmds="$cmds;L300:@shot 00-elite"
		run ai 30 "$cmds" ""
		if checked ai; then
			expect ai "the pack loaded beside Blood Gulch" "scene packs: covenant_ai: loaded"
			expect ai "its characters in Forge's palette" "forge: palette characters: 10"
			expect ai "an elite placed" "forge: spawned Elite Major Plasma Rifle"
			expect ai "it shoots: the player's shield goes down" "where: the player at .* shield 0\.[0-8]"
			expect ai "and then the player's body" "where: the player at .* body 0\.[0-8]"
		fi ;;
	*)
		echo "usage: $0 [tool|damaged|stale|banshee|ai]..." >&2; exit 2 ;;
	esac
done
echo "logs in $out"
[ $status = 0 ] && echo PASS
test_out_done $status
exit $status
