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
#             (a quarter as many); the lobbies' scenes' packs, menu_campaign
#             (a30), menu_multiplayer (bloodgulch) and menu_monitor (c20), each
#             built, loaded with no correction and fuzzed (an eighth as many); the AI packs (covenant_ai, flood_ai,
#             sentinel_ai, marine_ai) each built, loaded with no correction
#             and fuzzed (an eighth as many each)
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
#   cetool    packs from Halo Custom Edition maps (scene_pack_ce.c), the tool
#             alone: ce_pelican (Extinction's Pelican) built from
#             extinction.map and the Halo PC resource maps and loaded with no
#             correction, then fuzzed as any pack (a quarter of
#             HALO_TEST_FUZZ); damaged sources (HALO_TEST_CEFUZZ builds, 200 by
#             default: the map's bytes changed as the builder reads them -
#             words of the roots of the pack's tags, of its tag index, bytes of
#             its model data, pixels and samples, of the tag data and header):
#             each build refused or its pack refused or loaded clean twice,
#             never a crash; the recipe's map of another version, and missing:
#             refused with the reason; a player model's recipe (unit
#             multiplayer) built from Covenant_V_Marines_Beta_5.map, a
#             protected map, when it is there
#   cepelican the flyable Pelican in Blood Gulch: Forge places it from the
#             pack, the player rides it, four passengers board its seats, it
#             takes off, climbs, flies, comes down and lands, the passengers
#             aboard throughout; a screenshot strip
#   cemodels  player models from Custom Edition maps (player_models.c): the
#             maps folder's looked through (the Extinction Spartan found, the
#             stock cyborg's maps not), a profile's choice kept in
#             t:\player_models.txt, a Forge game (the settings panel's
#             shortcut) with the model loaded and the player spawning as it;
#             with no choice, the Spartan
#
# Each harness run must exit by itself, with no assertion or exception.
#
#   run_scene_pack_test.sh [CASE...]   (default: tool damaged stale banshee ai cetool cepelican cemodels)
#   HALO_TEST_VITA         the harness (default build/linux/halo of this tree)
#   HALO_TEST_PACK_TOOL    the tool (default build/linux/scene_pack_test)
#   HALO_TEST_DATA_MENUS   a folder with the game's maps folder (the Xbox maps,
#                          never in the repository: skipped without bloodgulch
#                          and b40)
#   HALO_TEST_CE_MAPS      Custom Edition maps (default ../custom-maps-dl beside
#                          the tree: extinction.map, Covenant_V_Marines_Beta_5.map;
#                          the ce cases are skipped without extinction)
#   HALO_TEST_CE_RESOURCES Halo PC's bitmaps.map, sounds.map and loc.map
#                          (default the data's maps folder)
#   HALO_TEST_SHOTS        a folder the cepelican and cemodels screenshots are
#                          copied to
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
cases=${*:-tool damaged stale banshee ai cetool cepelican cemodels}
ce_maps=${HALO_TEST_CE_MAPS:-$root/../custom-maps-dl}
ce_resources=${HALO_TEST_CE_RESOURCES:-$data/maps}
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

# a maps folder of the data's maps, the Custom Edition maps and Halo PC's
# resource maps (links), for the ce cases: FALSE without extinction.map
ce_folder() {
	local folder="$out/cedata" file
	[ -r "$ce_maps/extinction.map" ] && [ -r "$ce_resources/bitmaps.map" ] && [ -r "$ce_resources/sounds.map" ] &&
		[ -r "$ce_resources/loc.map" ] || return 1
	rm -rf "$folder"
	mkdir -p "$folder/maps"
	for file in "$(cd "$data" && pwd)"/maps/*.map; do
		ln -sfn "$file" "$folder/maps/"
	done
	for file in extinction Covenant_V_Marines_Beta_5; do
		[ -r "$ce_maps/$file.map" ] && ln -sfn "$(cd "$ce_maps" && pwd)/$file.map" "$folder/maps/$file.map"
	done
	for file in bitmaps sounds loc; do
		ln -sfn "$(cd "$ce_resources" && pwd)/$file.map" "$folder/maps/$file.map"
	done
	return 0
}

shots_keep() { # NAME
	if [ -n "${HALO_TEST_SHOTS:-}" ] && [ -d "$out/$1/shots" ]; then
		mkdir -p "$HALO_TEST_SHOTS/$1"
		cp "$out/$1/shots/"* "$HALO_TEST_SHOTS/$1/" 2>/dev/null
	fi
}

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
		# (the dropships: the campaign's Pelican tuned to be flown, flyable_pelican,
		# and loaded with no correction; a tune line of no tuning, or of a tag
		# the recipe does not hold, refused)
		if [ -r "$data/maps/b30.map" ] || [ -r "$data/maps/a50.map" ]; then
			"$tool" build "$recipes/dropships.txt" "$data/maps" "$out/tool/dropships.pack" > "$out/tool/dropships.log" 2>&1 ||
				fail "tool: dropships was not built ($out/tool/dropships.log)"
			expect tool "the dropships' Pelican tuned" \
				"tune flyable_pelican: vehicles.pelican.pelican: its driver's seat the player's, 10 riders' seats" "$out/tool/dropships.log"
			expect tool "dropships loads with no correction" "pack tags .*passed.* 0 corrections" "$out/tool/dropships.log"
		fi
		printf 'pack t\nmap b30\ntag vehi vehicles\\pelican\\pelican\ntune fly vehi vehicles\\pelican\\pelican\n' > "$out/tool/tune1.txt"
		printf 'pack t\nmap b30\ntag vehi vehicles\\pelican\\pelican\ntune flyable_pelican vehi vehicles\\banshee\\banshee\n' > "$out/tool/tune2.txt"
		"$tool" build "$out/tool/tune1.txt" "$data/maps" "$out/tool/tune1.pack" > "$out/tool/tune1.log" 2>&1
		expect tool "a tune line of no tuning refused" "recipe refused: the recipe's line 4 names a tuning that is not one" "$out/tool/tune1.log"
		"$tool" build "$out/tool/tune2.txt" "$data/maps" "$out/tool/tune2.pack" > "$out/tool/tune2.log" 2>&1
		expect tool "a tune of a tag the recipe does not hold refused" "recipe refused: the recipe tunes a tag it does not hold" "$out/tool/tune2.log"
		if [ -r "$data/maps/b40.map" ]; then
			printf 'pack t\nmap b40\ntag vehi vehicles\\banshee\\banshee\ntune flyable_pelican vehi vehicles\\banshee\\banshee\n' > "$out/tool/tune3.txt"
			"$tool" build "$out/tool/tune3.txt" "$data/maps" "$out/tool/tune3.pack" > "$out/tool/tune3.log" 2>&1 ||
				fail "tool: the tuned Banshee's pack was not built ($out/tool/tune3.log)"
			expect tool "a vehicle not the campaign's Pelican left as it is" \
				"tune flyable_pelican: vehicles.banshee.banshee left as it is: it is not the campaign's Pelican" "$out/tool/tune3.log"
			expect tool "and its pack loads with no correction" "pack tags .*passed.* 0 corrections" "$out/tool/tune3.log"
		fi
		# (the Halo 3 lobbies' scenes' packs: built from a30, bloodgulch and c20, each loaded with no
		# correction and fuzzed, an eighth as many)
		seed=3
		for pack in menu_campaign menu_multiplayer menu_monitor; do
			"$tool" build "$recipes/$pack.txt" "$data/maps" "$out/tool/$pack.pack" > "$out/tool/$pack.log" 2>&1 ||
				fail "tool: $pack was not built ($out/tool/$pack.log)"
			grep -aE "^built" "$out/tool/$pack.log"
			expect tool "$pack loads with no correction" "pack tags .*passed.* 0 corrections" "$out/tool/$pack.log"
			seed=$((seed + 1))
			TMPDIR="$out/tool" "$tool" fuzz "$out/tool/$pack.pack" $(( ${HALO_TEST_FUZZ:-2000} / 8 )) $seed > "$out/tool/$pack.fuzz.log" 2>&1 ||
				fail "tool: $pack's fuzzing failed ($out/tool/$pack.fuzz.log)"
			tail -1 "$out/tool/$pack.fuzz.log"
		done
		# (the AI packs: each built, loaded with no correction, fuzzed - an
		# eighth as many each)
		seed=10
		for pack in covenant_ai flood_ai sentinel_ai marine_ai; do
			if ! "$tool" build "$recipes/$pack.txt" "$data/maps" "$out/tool/$pack.pack" > "$out/tool/build-$pack.log" 2>&1; then
				fail "tool: $pack was not built ($out/tool/build-$pack.log)"
				continue
			fi
			grep -aE "^built" "$out/tool/build-$pack.log"
			expect tool "$pack loads with no correction" "pack tags .*passed.* 0 corrections" "$out/tool/build-$pack.log"
			seed=$((seed + 1))
			TMPDIR="$out/tool" "$tool" fuzz "$out/tool/$pack.pack" $(( ${HALO_TEST_FUZZ:-2000} / 8 )) $seed \
				> "$out/tool/fuzz-$pack.log" 2>&1 || fail "tool: $pack's fuzzing failed ($out/tool/fuzz-$pack.log)"
			tail -1 "$out/tool/fuzz-$pack.log"
			rm -f "$out/tool/$pack.pack"
		done
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
			expect ai "its characters in Forge's palette" "forge: palette characters: 14"
			expect ai "an elite placed" "forge: spawned Elite Major Plasma Rifle"
			expect ai "it shoots: the player's shield goes down" "where: the player at .* shield 0\.[0-8]"
			expect ai "and then the player's body" "where: the player at .* body 0\.[0-8]"
		fi ;;
	cetool)
		if ! ce_folder; then
			echo "SKIP: cetool: no extinction.map in $ce_maps (HALO_TEST_CE_MAPS) or Halo PC resource maps in $ce_resources"
			continue
		fi
		mkdir -p "$out/cetool"
		"$tool" build "$recipes/ce_pelican.txt" "$out/cedata/maps" "$out/cetool/ce_pelican.pack" > "$out/cetool/build.log" 2>&1 ||
			fail "cetool: ce_pelican was not built ($out/cetool/build.log)"
		grep -aE "^built|^loaded|groups|scene packs: extinction" "$out/cetool/build.log"
		expect cetool "Extinction's tags checked as the game's Custom Edition loader checks them" \
			"tag check: the map 'extinction' tags \(3620 tags" "$out/cetool/build.log"
		expect cetool "ce_pelican loads with no correction" "pack tags .*passed.* 0 corrections" "$out/cetool/build.log"
		expect cetool "its first tag the Pelican, its models made this game's" "groups: vehi 1 mode" "$out/cetool/build.log"
		TMPDIR="$out/cetool" "$tool" fuzz "$out/cetool/ce_pelican.pack" $(( ${HALO_TEST_FUZZ:-2000} / 4 )) 3 > "$out/cetool/fuzz.log" 2>&1 ||
			fail "cetool: ce_pelican's fuzzing failed ($out/cetool/fuzz.log)"
		tail -1 "$out/cetool/fuzz.log"
		TMPDIR="$out/cetool" "$tool" cefuzz "$recipes/ce_pelican.txt" "$out/cedata/maps" "${HALO_TEST_CEFUZZ:-200}" 5 \
			> "$out/cetool/cefuzz.log" 2>&1 || fail "cetool: damaged sources ($out/cetool/cefuzz.log)"
		grep -aE "^cefuzz|the build refused|failures" "$out/cetool/cefuzz.log" | sed 's/^/  /'
		# (the recipe's map of another version; not in the folder)
		sed 's/^cemap extinction 97888771 c5372723/cemap extinction 97888771 c5372724/' "$recipes/ce_pelican.txt" > "$out/cetool/other.txt"
		"$tool" build "$out/cetool/other.txt" "$out/cedata/maps" "$out/cetool/other.pack" > "$out/cetool/other.log" 2>&1 &&
			fail "cetool: a pack built from another version of the map"
		expect cetool "another version of the map: refused, said" "your extinction.map is another version" "$out/cetool/other.log"
		sed 's/^cemap extinction /cemap nosuchmap /' "$recipes/ce_pelican.txt" > "$out/cetool/missing.txt"
		"$tool" build "$out/cetool/missing.txt" "$out/cedata/maps" "$out/cetool/missing.pack" > "$out/cetool/missing.log" 2>&1 &&
			fail "cetool: a pack built from a map that is not there"
		expect cetool "no such map: refused, said" "nosuchmap.map \(Halo Custom Edition\) is not in the maps folder" "$out/cetool/missing.log"
		if [ -r "$out/cedata/maps/Covenant_V_Marines_Beta_5.map" ]; then
			printf 'pack pm_test\ncemap Covenant_V_Marines_Beta_5 111421130 fada2fca\nunit multiplayer\n' > "$out/cetool/model.txt"
			"$tool" build "$out/cetool/model.txt" "$out/cedata/maps" "$out/cetool/model.pack" > "$out/cetool/model.log" 2>&1 ||
				fail "cetool: the protected map's player model was not built ($out/cetool/model.log)"
			expect cetool "a protected map's multiplayer biped (unit multiplayer) built, loaded with no correction" \
				"pack tags .*passed.* 0 corrections" "$out/cetool/model.log"
			expect cetool "its first tag the biped" "groups: bipd 1 mode" "$out/cetool/model.log"
		fi
		rm -f "$out/cetool/"*.pack ;;
	cepelican)
		if ! ce_folder; then
			echo "SKIP: cepelican: no extinction.map in $ce_maps (HALO_TEST_CE_MAPS) or Halo PC resource maps"
			continue
		fi
		cmds="$bloodgulch;L100:pack load ce_pelican;L110:forge on;L112:forge camera 60 -100 3 130 -5;L115:forge list vehicles"
		cmds="$cmds;L118:forge spawn epelican;L120:forge distance 16;L122:forge drop;L140:forge off;L170:pack ride"
		cmds="$cmds;L175:pack board 4;L178:pack riders;L195:@shot 01-aboard"
		for t in $(seq 200 30 1900); do cmds="$cmds;L$t:pack where"; done
		cmds="$cmds;L265:@shot 02-takeoff;L330:@shot 03-climb;L400:@shot 04-cruise;L480:@shot 05-descend;L1780:@shot 06-landed"
		cmds="$cmds;L1790:pack riders;L1800:forge on;L1805:forge camera 28 -78 16 -45 -20;L1840:@shot 07-outside"
		data_saved=$data
		data=$out/cedata
		run cepelican 76 "$cmds" "wait:150:500 level:150:5000 lup+rup:900:0 lup:900:0 rdown:1800:0 lup:1200:0 rdown:1200:0 lup:2000:0 rup:1400:0 wait:36000:0"
		data=$data_saved
		if checked cepelican; then
			log=$out/cepelican/run.log
			expect cepelican "the pack built from Extinction and loaded beside Blood Gulch" "scene packs: ce_pelican: loaded from extinction"
			expect cepelican "Forge's palette lists it as the Extinction Pelican" "forge: vehicles [0-9]+: Extinction Pelican"
			expect cepelican "placed" "forge: dropped Extinction Pelican"
			expect cepelican "the player at its controls" "scene packs: ride: vehicles.epelican.epelican entered"
			expect cepelican "four passengers in its seats" "scene packs: board: 4 passengers aboard"
			heights=$(grep -a "where: the vehicle at" "$log" | sed 's/.* at [^ ]* [^ ]* \([-0-9.]*\),.*/\1/')
			first=$(echo "$heights" | head -1)
			high=$(echo "$heights" | sort -n | tail -1)
			last=$(echo "$heights" | tail -1)
			echo "  heights: from $first up to $high, down to $last"
			awk -v f="$first" -v h="$high" 'BEGIN { exit !(h - f >= 20) }' && echo "  ok: cepelican: took off and climbed" ||
				fail "cepelican: it did not climb (from $first to $high)"
			awk -v h="$high" -v l="$last" 'BEGIN { exit !(h - l >= 10) }' && echo "  ok: cepelican: came down" ||
				fail "cepelican: it did not come down (from $high to $last)"
			grep -a "where: the vehicle at" "$log" | tail -3 | grep -aqE "velocity -?0\.0[0-2] -?0\.0[0-2] -?0\.0[0-2]" &&
				echo "  ok: cepelican: landed (at rest)" || fail "cepelican: it did not come to rest"
			[ "$(grep -ac "scene packs: riders: 5 in vehicles.epelican.epelican" "$log")" -ge 2 ] &&
				echo "  ok: cepelican: the pilot and four passengers aboard, before and after" ||
				fail "cepelican: not everyone stayed aboard"
			shots_keep cepelican
		fi ;;
	cemodels)
		if ! ce_folder; then
			echo "SKIP: cemodels: no extinction.map in $ce_maps (HALO_TEST_CE_MAPS) or Halo PC resource maps"
			continue
		fi
		profile=${HALO_TEST_PROFILE:-$root/../vita-save-backups/20261017-halo-profile/blam-unlocked.sav}
		if [ ! -r "$profile" ]; then
			echo "SKIP: cemodels: no profile (HALO_TEST_PROFILE)"
			continue
		fi
		data_saved=$data
		data=$out/cedata
		# (the scan; a choice made by the console: the Extinction Spartan, kept)
		rm -rf "$out/cemodels-save"
		mkdir -p "$out/cemodels-save/u/UDATA/869079979327"
		cp "$profile" "$out/cemodels-save/u/UDATA/869079979327/blam.sav"
		[ -r "$(dirname "$profile")/SaveMeta.xbx" ] && cp "$(dirname "$profile")/SaveMeta.xbx" "$out/cemodels-save/u/UDATA/869079979327/"
		run cemodels-choose 10 "60:models list;70:models choose 0;80:models list" "" HALO_SAVE_ROOT="$out/cemodels-save"
		# (pm_b494505f: the Extinction Spartan's pack, by its map's name and identity: player_models.c)
		FRESH=1 run cemodels 40 "L300:forge off;L330:pack spawn pm_b494505f 0;L345:forge on;L347:forge camera 104.5 -163 1.5 140 -8;L400:@shot 00-model" "" \
			HALO_SAVE_ROOT="$out/cemodels-save" HALO_MENU_STYLE=classic HALO_SYSTEM_LINK_TEST=forge:9
		data=$data_saved
		if checked cemodels-choose && checked cemodels; then
			expect cemodels-choose "the Extinction Spartan found" "player models: [0-9]+: Spartan \(Extinction\) - characters.spartan.spartan_mp of extinction.map"
			grep -aq "player models: .*bloodgulch.map" "$out/cemodels-choose/run.log" &&
				fail "cemodels: an Xbox map's cyborg listed as a model"
			expect cemodels-choose "chosen for the profile" "player models: 869079979327 chose Spartan \(Extinction\)"
			grep -q "^869079979327 extinction 97888771 c5372723$" "$out/cemodels-save/t/player_models.txt" &&
				echo "  ok: cemodels: kept in t:\\player_models.txt" || fail "cemodels: the choice was not kept"
			expect cemodels "the Forge game: the model loaded" "scene packs: pm_[0-9a-f]+: loaded from extinction"
			expect cemodels "the player spawns as it" "player models: the first local player plays as Spartan \(Extinction\) \(characters.spartan.spartan_mp\)"
			shots_keep cemodels
		fi ;;
	*)
		echo "usage: $0 [tool|damaged|stale|banshee|ai|cetool|cepelican|cemodels]..." >&2; exit 2 ;;
	esac
done
echo "logs in $out"
[ $status = 0 ] && echo PASS
test_out_done $status
exit $status
