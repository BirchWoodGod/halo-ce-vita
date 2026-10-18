#!/bin/bash
# Forge, version 1 (port/linux/game/forge.c; its screens halo3/forge_lobby.xml
# and halo3/forge.xml; the design: docs/superpowers/specs/2026-10-18-forge-design.md)
# on the Linux harness (the desktop GL build, for its screenshots):
#
#   v1          the whole loop by the controller (HALO_TEST_PAD, Menu style
#               Halo 3, the Halo PC files): the main menu's FORGE, the Forge
#               lobby, its map picker (Wizard, then Blood Gulch), START FORGE;
#               in Blood Gulch the player is the monitor: flies, opens the
#               object browser (Square), opens VEHICLES, places a Warthog in
#               its hand, turns it and pushes it away (L + the right stick),
#               drops it, places a weapon from the next category (R) - a
#               flamethrower, not the game type's rocket launcher - and a
#               boulder from SCENERY, grabs it under the crosshair, carries
#               it, drops it, deletes it, becomes the Spartan (Select + D-pad
#               up; D-pad up alone does nothing for the monitor) and the
#               monitor again (Select alone and D-pad up alone flip nothing), opens the Forge menu (Start) and leaves Forge, asked
#               first, back at the Halo 3 main menu
#   budget      the budget (256 objects): the console's monitor in Blood
#               Gulch fills 260 boulders; 256 placed, the rest refused with
#               a message; one deleted, another placed
#   b30         the beach: the console's monitor; AI characters (each given
#               a free actor, ai_attach_free) and a Ghost; the characters'
#               budget (32): 34 marines asked for, 32 placed, refused after
#   off         no Forge: a multiplayer game of Blood Gulch, D-pad up
#               (zoom) and Square pressed: no "forge:" line at all
#   files       the map variant files' writer, validator and names, and the
#               fuzzer over the validator (run_forge_files_test.sh: ASan and
#               UBSan; no harness)
#   variants    map variants saved and loaded (phase b), one store over five
#               runs: save - START FORGE, a Warthog placed, the Forge menu's
#               SAVE MAP VARIANT, the name typed (HALO_TEST_TEXT_INPUT), a
#               boulder placed, saved again under the same name (the file
#               replaced), left; the harness test's own files added (a good
#               one, one with a tag Blood Gulch lacks, one of another map, a
#               damaged one: forge_files_test --write); load - the lobby's MAP
#               VARIANT picker lists them (the last two damaged), the saved one
#               chosen (MAP, the map, then its variants: Halo 3's way),
#               START FORGE places its 2 objects; missing - the one with the
#               missing tag: 5 placed, 1 skipped; damaged - the damaged ones
#               are not offered, B back, START FORGE plays none; full - a save
#               that fails half way (HALO_TEST_FORGE_WRITE_FAIL, as a full
#               card), over the session's own file and as a new one: said,
#               the old file as it was, nothing else left
#   edit        the Forge menu's UNDO, REDO and SNAP, and an object's
#               properties (Square while holding it): a Warthog placed with
#               team red, respawn 45 s, not placed at the start; placing it
#               undone and redone (its properties back), snapping on, moved
#               (dropped on the grid), the move undone; saved, and the file
#               read back (forge_files_test --dump) with its properties
#   fileshare   the main menu's FILE SHARE over the test's files: listed by
#               map, a map variant renamed (typed), deleted (asked first), a
#               damaged one refused for Forge and deleted, GAME VARIANTS
#               listed, A opens the Forge lobby on the map with the variant
#   fstouch     FILE SHARE by taps (HALO_TEST_TAPS): the main menu's row,
#               Rename, a row (the Forge lobby), the lobby's Leave, Back
#   panel       Forge from the settings panel's Forge and File Share page, for
#               Menu style Halo CE (its request, HALO_SYSTEM_LINK_TEST=forge:):
#               Blood Gulch with the test's good map variant starts and places
#               it; with the damaged one, nothing starts and the message says
#               why (the panel's page itself: run_vita_settings_test.sh)
#
# Each must exit by itself, with no assertion or exception. The screenshots
# (HALO_SCREENSHOT_DIR, each run's shots folder) are kept with
# HALO_TEST_KEEP=1, or copied to HALO_TEST_SHOTS when it is set.
#
#   run_forge_test.sh [CASE...]      (default: v1 budget b30 off files variants
#                                     edit fileshare fstouch panel)
#   HALO_TEST_VITA        the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA_MENUS  a folder with the game's maps folder (the Xbox maps)
#                         and the Halo PC bitmaps.map and loc.map (never in the
#                         repository; v1 is skipped without them)
#   HALO_MENUS_FOLDER     the menus' XML (default this tree's port/vita/app0/menus)
#   HALO_TEST_OUT         where the logs go (deleted after a pass when the test
#                         made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_forge_test.$$}
export HALO_MENUS_FOLDER=${HALO_MENUS_FOLDER:-$root/port/vita/app0/menus/}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-v1 budget b30 off files variants edit fileshare fstouch panel}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -r "$data/maps/bloodgulch.map" ]; then
	echo "SKIP: no maps in $data/maps (HALO_TEST_DATA_MENUS)"
	exit 0
fi
menus_files=1
[ -r "$data/maps/bitmaps.map" ] && [ -r "$data/maps/loc.map" ] || menus_files=0

# run NAME SECONDS INIT COMMANDS PAD [VAR=value...]: the harness, from the
# main menu or on the map init.txt loads
run() {
	local name=$1 seconds=$2 init=$3 commands=$4 pad=$5 dir="$out/$1"
	shift 5
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save" "$dir/shots"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	[ -n "$init" ] && printf '%b\n' "$init" > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy HALO_DATA_ROOT="$dir/data" HALO_SAVE_ROOT="$dir/save" \
		HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 \
		HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false HALO_UI_LOG=1 \
		HALO_SCREENSHOT_DIR="$dir/shots" HALO_TEST_COMMANDS="$commands" "HALO_TEST_PAD=$pad" "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
	rm -rf "$dir/save"
}

# the test's own map variant files (forge_files_test --write: good.hmv,
# missing.hmv, wrongmap.hmv, damaged.hmv, of Blood Gulch) put in STORE's
# Blood Gulch folder
samples() {
	local store=$1
	mkdir -p "$store/forge/bloodgulch"
	"$out/forge_files_test" --write "$store/forge/bloodgulch" > /dev/null
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

# expect NAME TEXT PATTERN: a line of the log matches PATTERN (grep -E)
expect() {
	if grep -aqE "$3" "$out/$1/run.log"; then
		echo "  ok: $1: $2"
	else
		fail "$1: $2 ($out/$1/run.log)"
	fi
}

# count NAME TEXT PATTERN MINIMUM: at least MINIMUM lines match PATTERN
count() {
	local found
	found=$(grep -acE "$3" "$out/$1/run.log")
	if [ "$found" -ge "$4" ]; then
		echo "  ok: $1: $2 ($found)"
	else
		fail "$1: $2 ($found of $4, $out/$1/run.log)"
	fi
}

bloodgulch='game_variant slayer\nmap_name levels\\test\\bloodgulch\\bloodgulch'
b30='map_name levels\\b30\\b30'

# (v1: the main menu: down to FORGE; the lobby: Map, down, A: Wizard, its
# variants (Halo 3's), A: Wizard itself; Map, up, A: Blood Gulch, A: the map
# itself; down, START FORGE. In the level: forward and up, Square, A
# on VEHICLES, down twice, A: a Warthog in the monitor's hand; L + right stick
# right (turned), L + up (pushed away), R (dropped); Square, R (the next
# category), down, A: a weapon, R (dropped: it falls); Square, R twice
# (SCENERY), A: a boulder, R (dropped: scenery stays where it is let go), R
# (grabbed again, under the crosshair), left (carried), R (dropped);
# Triangle (deleted);
# D-pad up alone (rs: nothing for the monitor), Select + D-pad up (back+rs:
# the Spartan), forward, Select alone then D-pad up alone (the scoreboard,
# then zoom: neither flips), Select + D-pad up (the monitor); Start, down
# six times (past SAVE MAP VARIANT, UNDO, REDO, SNAP), A (LEAVE FORGE), A
# (leave))
v1pad="wait:150:3000 @00-main-menu:0:300 down:150:600 down:150:600 down:150:600 a:150:2500 @01-lobby:0:300"
v1pad="$v1pad a:150:2000 @02-map-picker:0:300 down:150:700 a:150:1500 @02b-wizard-variants:0:300 a:150:2000"
v1pad="$v1pad a:150:2000 up:150:700 a:150:1500 a:150:2000"
v1pad="$v1pad @03-lobby-blood-gulch:0:300 down:150:600 a:150:1500 level:150:2500 @04-monitor:0:300"
v1pad="$v1pad lup:1200:300 a:600:600 @05-flown:0:300 x:150:1500 @06-browser:0:300 a:150:1200"
v1pad="$v1pad @07-browser-vehicles:0:300 down:150:600 down:150:600 a:150:1500 @08-warthog-held:0:300"
v1pad="$v1pad lt+rright:900:300 lt+rup:500:600 @09-turned-pushed:0:300 rt:150:2500 @10-dropped:0:300"
v1pad="$v1pad x:150:1200 rt:150:1200 @11-browser-weapons:0:300 down:150:600 a:150:1500 rt:150:1500"
v1pad="$v1pad x:150:1200 rt:150:800 rt:150:1200 @12-browser-scenery:0:300 a:150:1500 rt:150:1000"
v1pad="$v1pad @13-boulder-dropped:0:300 rt:150:1000 lleft:700:600 @14-boulder-carried:0:300 rt:150:1500"
v1pad="$v1pad y:150:1500 @15-deleted:0:300 rs:150:1500 back+rs:150:2500 @16-spartan:0:300 lup:800:1500"
v1pad="$v1pad back:150:600 rs:150:1200 back+rs:150:2500"
v1pad="$v1pad @17-monitor-again:0:300 start:150:2000 @18-forge-menu:0:300 down:150:600 down:150:600 down:150:600"
v1pad="$v1pad down:150:600 down:150:600 down:150:600 a:150:1500"
v1pad="$v1pad @19-leave-forge:0:300 a:150:9000 @20-main-menu-again:0:300"

# (variants: the main menu's FORGE, the lobby; START FORGE (the map, no
# variant); a Warthog from VEHICLES, dropped; Start, SAVE MAP VARIANT (its
# name typed), B; a boulder from SCENERY, dropped; Start, SAVE MAP VARIANT
# (the same name: the file replaced), down to LEAVE FORGE, leave)
lobby="wait:150:3000 down:150:600 down:150:600 down:150:600 a:150:2500"
savepad="$lobby down:150:600 a:150:1500 level:150:2500 x:150:1500 a:150:1200 down:150:600 down:150:600"
savepad="$savepad a:150:1500 rt:150:2000 start:150:2000 down:150:600 down:150:600 a:150:2500 @00-saved:0:300 b:150:1500"
savepad="$savepad x:150:1500 rt:150:800 rt:150:800 rt:150:1200 a:150:1500 rt:150:2000 start:150:2000 down:150:600"
savepad="$savepad down:150:600 a:150:2500 @01-saved-again:0:300 down:150:600 down:150:600 down:150:600 down:150:600"
savepad="$savepad a:150:1500 a:150:9000"
# (the lobby's MAP: the maps (Blood Gulch chosen), A: its variants (the map
# itself first), down N times, A; START FORGE)
pick() {
	local p="$lobby a:150:2000 a:150:1500 @00-picker:0:300" i
	for i in $(seq 1 "$1"); do p="$p down:150:700"; done
	echo "$p @01-picked:0:300 a:150:2000 @02-lobby:0:300 down:150:600 a:150:1500 level:150:3000 @03-placed:0:300"
}
# (full: Sample Gulch chosen and started; Start, SAVE MAP VARIANT twice:
# over its file, then as a new one, both failing)
fullpad="$(pick 3) start:150:2000 down:150:600 down:150:600 a:150:2500 @04-full:0:300 a:150:2500 @05-full-new:0:300"
# (fileshare: FILE SHARE; down (Sample Gulch), Square (renamed), Triangle,
# A (deleted); down (damaged.hmv), A (refused), Triangle, A (deleted);
# right (GAME VARIANTS), left; A on Missing Tag (the Forge lobby), B, B)
fspad="wait:150:3000 down:150:600 down:150:600 down:150:600 down:150:600 a:150:2500 @00-fileshare:0:300"
fspad="$fspad down:150:700 x:150:2000 @01-renamed:0:300 y:150:1500 @02-delete-asked:0:300 a:150:2000 @03-deleted:0:300"
fspad="$fspad down:150:700 a:150:1500 @04-damaged-refused:0:300 y:150:1500 a:150:2000 right:150:1200"
fspad="$fspad @05-game-variants:0:300 left:150:1200 a:150:2500 @06-forge-lobby:0:300 b:150:2000 b:150:2000"
fspad="$fspad @07-main-menu:0:300"

# (edit: START FORGE; a Warthog from VEHICLES, held; Square (its
# properties): A (team red), down, A (respawn 45 s), down, A (not placed at
# start), B; R (dropped: placed); Start, down to UNDO, A (undone: gone),
# down, A (redone: back with its properties), down, A (SNAP on), B; a
# boulder from SCENERY, R (dropped on the grid), R (grabbed), left
# (carried), R (dropped: moved, on the grid); Start, down to UNDO, A (the
# move undone), B; Start, SAVE MAP VARIANT (typed), B)
editpad="$lobby down:150:600 a:150:1500 level:150:2500 x:150:1500 a:150:1200 down:150:600 down:150:600"
editpad="$editpad a:150:1500 x:150:1500 @00-properties:0:300 a:150:800 down:150:600 a:150:800 down:150:600 a:150:800"
editpad="$editpad @01-properties-set:0:300 b:150:1200 rt:150:2000 start:150:2000 down:150:600 down:150:600 down:150:600"
editpad="$editpad a:150:1500 @02-undone:0:300 down:150:600 a:150:1500 @03-redone:0:300 down:150:600 a:150:1200"
editpad="$editpad @04-snap:0:300 b:150:1500 x:150:1500 rt:150:800 rt:150:800 rt:150:1200 a:150:1500 rt:150:1500"
editpad="$editpad rt:150:1500 lleft:600:600 rt:150:2000 @05-moved:0:300 start:150:2000"
editpad="$editpad down:150:600 down:150:600 down:150:600 a:150:1500 @06-move-undone:0:300 b:150:1500 start:150:2000"
editpad="$editpad down:150:600 down:150:600 a:150:2500 @07-saved:0:300 b:150:1500"

edit_runs() {
	local store="$out/edit-store"
	rm -rf "$store"
	mkdir -p "$store"
	run edit 90 "" "" "$editpad" HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store" "HALO_TEST_TEXT_INPUT=Edit Test"
	"$out/forge_files_test" --dump "$store/forge/bloodgulch/edit_test.hmv" > "$out/edit/dump.txt" 2>&1
}

variants_runs() {
	local store="$out/variants-store"
	rm -rf "$store"
	mkdir -p "$store"
	run variants-save 90 "" "" "$savepad" HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store" \
		"HALO_TEST_TEXT_INPUT=Gulch Test|Gulch Test"
	samples "$store"
	md5sum "$store/forge/bloodgulch/good.hmv" > "$out/variants-good.md5"
	run variants-load 50 "" "" "$(pick 1)" HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store"
	run variants-missing 50 "" "" "$(pick 2)" HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store"
	run variants-damaged 50 "" "" "$lobby a:150:2000 a:150:1500 down:150:700 down:150:700 down:150:700 @00-damaged:0:300 b:150:1500 b:150:1500 down:150:600 a:150:1500 level:150:2500 @01-none:0:300" \
		HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store"
	run variants-full 60 "" "" "$fullpad" HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store" HALO_TEST_FORGE_WRITE_FAIL=1 \
		"HALO_TEST_TEXT_INPUT=Sample Gulch|Brand New"
	find "$store" -type f | sort > "$out/variants-files.txt"
	mkdir -p "$out/variants"
	echo 0 > "$out/variants/exit"
}

fileshare_runs() {
	local store="$out/fileshare-store"
	rm -rf "$store"
	samples "$store"
	run fileshare 60 "" "" "$fspad" HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store" "HALO_TEST_TEXT_INPUT=Renamed Gulch"
	find "$store" -type f | sort > "$out/fileshare-files.txt"
}

panel_runs() {
	local store="$out/panel-store"
	rm -rf "$store"
	samples "$store"
	run panel 55 "" "" "wait:150:1000 level:150:3000 @00-panel-forge:0:300" HALO_MENU_STYLE=classic HALO_SAVE_ROOT="$store" \
		HALO_SYSTEM_LINK_TEST=forge:9:good.hmv
	run panel-damaged 30 "" "" "wait:150:15000" HALO_MENU_STYLE=classic HALO_SAVE_ROOT="$store" \
		HALO_SYSTEM_LINK_TEST=forge:9:damaged.hmv
	mkdir -p "$out/panel"
	echo 0 > "$out/panel/exit"
}

fstouch_runs() {
	local store="$out/fstouch-store"
	rm -rf "$store"
	samples "$store"
	# (FILE SHARE's row; Rename's label; the second row: A, the Forge
	# lobby; the lobby's Leave; File Share's Back)
	run fstouch 40 "" "" "wait:150:30000" HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store" "HALO_TEST_TEXT_INPUT=Touched" \
		"HALO_TEST_TAPS=4000:60,362 8000:220,430 12000:120,128 17000:140,455 22000:395,430"
	find "$store" -type f | sort > "$out/fstouch-files.txt"
}

# (the test's own files' writer and reader: forge_files_test, built once
# before the cases run side by side)
mkdir -p "$out"
if ! ${CC:-cc} -O1 -D_GNU_SOURCE -I"$root/port/linux/game" -o "$out/forge_files_test" "$here/forge_files_test.c" \
	"$root/port/linux/game/forge_files.c"; then
	echo "FAIL: forge_files_test does not build"
	exit 1
fi

# (at most HALO_TEST_JOBS harnesses at once, 2 by default: other tests share the machine)
jobs=${HALO_TEST_JOBS:-2}
for c in $cases; do
	while [ "$(jobs -rp | wc -l)" -ge "$jobs" ]; do
		wait -n
	done
	case $c in
	v1)
		if [ "$menus_files" = 0 ]; then
			echo "SKIP: v1: no Halo PC bitmaps.map and loc.map in $data/maps"
			continue
		fi
		run "$c" 110 "" "" "$v1pad" HALO_MENU_STYLE=halo3 & ;;
	budget)
		run "$c" 50 "$bloodgulch" "L200:forge on;L210:forge fill 260 boulder;L230:forge budget;L240:@shot 00-budget-full;L260:forge delete;L270:forge budget;L280:forge spawn boulder;L285:forge drop;L290:forge budget;L300:@shot 01-budget-after" "" & ;;
	b30)
		run "$c" 90 "$b30" "L60:@skip;L205:(set cheat_deathless_player true);L1200:@shot 00-b30-player;L1210:forge on;L1220:forge camera -63 39 3.2 -50 -22;L1225:forge list characters;L1240:@shot 01-b30-monitor;L1250:forge spawn hunter;L1252:forge distance 4;L1258:@shot 02-b30-hunter-held;L1260:forge drop;L1280:forge spawn marine_armored assault rifle major;L1282:forge distance 3.5;L1285:forge drop;L1300:forge spawn ghost;L1302:forge distance 4;L1303:forge turn 90;L1305:forge drop;L1320:forge spawn elite commander;L1322:forge distance 5;L1325:forge drop;L1340:@shot 03-b30-spawned;L1420:@shot 04-b30-ai;L1430:forge camera -40 60 6 -50 -22;L1440:forge fill 34 marine_armored assault rifle major;L1450:forge budget;L1470:@shot 05-b30-ai-budget;L1520:forge where;L1600:forge off;L1605:@shot 06-b30-player" "" & ;;
	off)
		run "$c" 30 "$bloodgulch" "L300:@shot 00-off" "level:150:2000 rs:150:1500 x:150:1500 rs:150:1500" & ;;
	files)
		mkdir -p "$out/files"
		(TMPDIR="$out/files" "$here/run_forge_files_test.sh" > "$out/files/run.log" 2>&1; echo $? > "$out/files/exit") & ;;
	variants)
		if [ "$menus_files" = 0 ]; then
			echo "SKIP: variants: no Halo PC bitmaps.map and loc.map in $data/maps"
			continue
		fi
		variants_runs & ;;
	fileshare)
		if [ "$menus_files" = 0 ]; then
			echo "SKIP: fileshare: no Halo PC bitmaps.map and loc.map in $data/maps"
			continue
		fi
		fileshare_runs & ;;
	edit)
		if [ "$menus_files" = 0 ]; then
			echo "SKIP: edit: no Halo PC bitmaps.map and loc.map in $data/maps"
			continue
		fi
		edit_runs & ;;
	fstouch)
		if [ "$menus_files" = 0 ]; then
			echo "SKIP: fstouch: no Halo PC bitmaps.map and loc.map in $data/maps"
			continue
		fi
		fstouch_runs & ;;
	panel)
		panel_runs & ;;
	*) echo "unknown case $c"; exit 2 ;;
	esac
done
wait

for c in $cases; do
	[ -f "$out/$c/exit" ] || continue
	if [ "$c" = files ]; then
		if [ "$(cat "$out/files/exit")" = 0 ]; then
			echo "  ok: files: $(grep -a "^fuzz:" "$out/files/run.log"); $(grep -a "checks," "$out/files/run.log")"
		else
			fail "files: the map variant files' test failed ($out/files/run.log)"
			tail -20 "$out/files/run.log"
		fi
		continue
	fi
	if [ "$c" = panel ]; then
		checked panel
		checked panel-damaged
		expect panel "the panel's request started Forge on Blood Gulch (the Halo CE menus)" \
			"system link: the settings panel's Forge: started"
		expect panel "with the map variant, placed" "forge: map variant \"Sample Gulch\" applied: 5 placed, 0 skipped"
		expect panel "the monitor" "forge: the Forge game of levels.test.bloodgulch.bloodgulch starts"
		expect panel-damaged "a damaged variant: nothing started, said" \
			"system link: the settings panel's Forge: not started: The map variant is damaged and can't be used."
		if grep -aq "forge: a Forge game of" "$out/panel-damaged/run.log"; then
			fail "panel-damaged: a Forge game was asked for"
		fi
		continue
	fi
	if [ "$c" = variants ]; then
		for r in variants-save variants-load variants-missing variants-damaged variants-full; do
			checked "$r"
		done
		store="$out/variants-store/forge/bloodgulch"
		expect variants-save "START FORGE with no variant" "menus: forge lobby: start levels.test.bloodgulch.bloodgulch$"
		expect variants-save "the Forge menu has SAVE MAP VARIANT, the name typed" "menus: typing \"Map variant name\": typed"
		expect variants-save "saved with the Warthog" "forge: map variant \"Gulch Test\" saved as bloodgulch/gulch_test.hmv, 1 objects"
		expect variants-save "saved again with the boulder, the file replaced" \
			"forge store: bloodgulch/gulch_test.hmv written: \"Gulch Test\", 2 objects, [0-9]+ bytes \(replacing the old one\)"
		[ -f "$store/gulch_test.hmv" ] && echo "  ok: variants-save: gulch_test.hmv in the store" ||
			fail "variants-save: no gulch_test.hmv in $store"
		expect variants-load "the picker read the Blood Gulch folder" "menus: 5 map variants of bloodgulch"
		expect variants-load "MAP, then the map's variants (Halo 3's), the map itself first" \
			"menus: forge variants: Blood Gulch: the original \\(1 of 4\\)"
		expect variants-load "a damaged file refused by the validator" "forge store: bloodgulch/damaged.hmv refused: its checksum is wrong"
		expect variants-load "another map's file refused" "forge store: bloodgulch/wrongmap.hmv refused: of another map"
		expect variants-load "Gulch Test chosen" "menus: forge lobby: Blood Gulch, map variant Gulch Test \(gulch_test.hmv\)"
		expect variants-load "the lobby says it" "menus: forge lobby caption: Gulch Test on Blood Gulch"
		expect variants-load "its two objects placed as the game starts" \
			"forge: map variant \"Gulch Test\" applied: 2 placed, 0 skipped"
		expect variants-missing "the missing tag said" "forge: the map variant's vehicles.banshee.banshee_mp is not on this map"
		expect variants-missing "skipped and counted, not fatal" \
			"forge: map variant \"Missing Tag\" applied: 5 placed, 1 skipped \(1 missing tags"
		expect variants-damaged "the damaged ones not offered (the map and its three good variants)" \
			"menus: forge variants: Blood Gulch: Sample Gulch \\(4 of 4\\)"
		expect variants-damaged "B: back to the maps" "menus: forge lobby: Blood Gulch's variants"
		expect variants-damaged "START FORGE then plays none" "menus: forge lobby: start levels.test.bloodgulch.bloodgulch$"
		if grep -aq "forge: map variant .* applied" "$out/variants-damaged/run.log"; then
			fail "variants-damaged: a variant was applied"
		fi
		expect variants-full "Sample Gulch played" "forge: map variant \"Sample Gulch\" applied: 5 placed"
		expect variants-full "a failed write over its own file said" \
			"forge: the map variant was not saved: Couldn't write it \(is the card full\?\)"
		[ "$(grep -ac "forge: the map variant was not saved" "$out/variants-full/run.log")" -ge 2 ] &&
			echo "  ok: variants-full: the new one's failed write said too" || fail "variants-full: not two failed saves"
		md5sum -c --quiet "$out/variants-good.md5" > /dev/null 2>&1 && echo "  ok: variants-full: good.hmv as it was" ||
			fail "variants-full: good.hmv changed by a failed save"
		if grep -qE "\.tmp$|\.old$|brand_new" "$out/variants-files.txt"; then
			fail "variants-full: a failed save left a file: $(grep -E "\.tmp$|\.old$|brand_new" "$out/variants-files.txt")"
		else
			echo "  ok: variants-full: nothing left by the failed saves"
		fi
		continue
	fi
	checked "$c" || continue
	log="$out/$c/run.log"
	case $c in
	v1)
		expect "$c" "FORGE opened the Forge lobby" "ui: screen pc.h3.forge.lobby"
		expect "$c" "the map picker opened" "ui: screen pc.h3.forge.maps.screen"
		expect "$c" "Wizard chosen" "menus: forge lobby: Wizard"
		expect "$c" "then Blood Gulch, the lobby's caption" "menus: forge lobby caption: Edit Objects on Blood Gulch"
		expect "$c" "START FORGE started Blood Gulch" "menus: forge lobby: start levels.test.bloodgulch.bloodgulch"
		expect "$c" "the level's map got Forge's screens" "menus: the in-game screens: .* added to bloodgulch.map"
		expect "$c" "the Forge game started, the player the monitor" "forge: the Forge game of levels.test.bloodgulch.bloodgulch starts"
		expect "$c" "the object browser opened by Square" "ui: screen pc.h3.forge.browser.screen"
		expect "$c" "VEHICLES opened, the Warthog chosen" "menus: forge browser: VEHICLES: Warthog \\(3 of 3\\)"
		expect "$c" "a Warthog placed in the monitor's hand" "forge: spawned Warthog \\(warthog, vehicles\\)"
		expect "$c" "R went to the next category" "menus: forge browser: WEAPONS: "
		expect "$c" "a weapon placed" "forge: spawned [^(]* \\([^,]*, weapons\\)"
		expect "$c" "a flamethrower, not the game type's rocket launcher" "forge: dropped Flamethrower \\(flamethrower\\)"
		expect "$c" "SCENERY's boulder placed" "forge: spawned Boulder \\(boulder, scenery\\)"
		expect "$c" "the object under the crosshair grabbed" "forge: grabbed Boulder"
		count "$c" "objects dropped" "forge: dropped " 4
		expect "$c" "deleted" "forge: deleted Boulder"
		expect "$c" "Select + D-pad up: the Spartan, where the monitor was" "forge: player mode at .* can be hurt"
		count "$c" "Select + D-pad up: the monitor (at the start and again)" "forge: monitor mode at " 2
		count "$c" "the combo kept from the game, twice" "forge: the monitor and Spartan combo" 2
		# (D-pad up alone, and Select alone, flip nothing: one Spartan, two monitors)
		found=$(grep -ac "forge: player mode at" "$log")
		[ "$found" = 1 ] && echo "  ok: $c: D-pad up alone and Select alone flipped nothing" ||
			fail "$c: $found flips to the Spartan, not 1"
		found=$(grep -ac "forge: monitor mode at" "$log")
		[ "$found" = 2 ] || fail "$c: $found flips to the monitor, not 2"
		expect "$c" "Start opened the Forge menu" "ui: screen pc.h3.forge.pause.screen"
		expect "$c" "LEAVE FORGE asked first" "ui: screen pc.h3.forge.leave.screen"
		expect "$c" "left without saving" "menus: forge: leaving to the main menu"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.main.screen' &&
			echo "  ok: $c: back at the Halo 3 main menu" || fail "$c: not back at the Halo 3 main menu"
		# (the monitor flew: where it started and where the Warthog was placed differ)
		first=$(grep -a "forge: monitor mode at" "$log" | head -1 | awk '{print $6, $7, $8}')
		placed=$(grep -a "forge: spawned Warthog" "$log" | head -1 | sed 's/.* at \([^ ]* [^ ]* [^ ]*\) facing.*/\1/')
		[ -n "$first" ] && [ -n "$placed" ] && echo "  ok: $c: the monitor started at $first, placed the Warthog at $placed" ||
			fail "$c: no monitor or Warthog position"
		# (L + the right stick turned it: its facing as dropped is not its facing as placed)
		spawned=$(grep -a "forge: spawned Warthog" "$log" | head -1 | sed 's/.*facing \([^ ]* [^ ]*\) .*/\1/')
		dropped=$(grep -a "forge: dropped Warthog" "$log" | head -1 | sed 's/.*facing \([^ ]* [^ ]*\) .*/\1/')
		if [ -n "$spawned" ] && [ -n "$dropped" ] && [ "$spawned" != "$dropped" ]; then
			echo "  ok: $c: turned (facing $spawned, then $dropped)"
		else
			fail "$c: the Warthog was not turned ($spawned, $dropped)"
		fi
		# (no saved game written by leaving)
		grep -aq "game_state_save_to_persistent_storage\|saved the game" "$log" && fail "$c: a game was saved" ;;
	budget)
		expect "$c" "256 of 260 placed" "forge: filled 256 of 260 Boulder"
		expect "$c" "the rest refused, said on the screen" "forge: Budget full: 256 objects"
		expect "$c" "the budget full" "forge: budget 256/256 objects"
		expect "$c" "one deleted" "forge: deleted Boulder"
		expect "$c" "another placed" "forge: spawned Boulder .*budget 256/256 objects" ;;
	b30)
		expect "$c" "the beach has characters" "forge: palette characters: [1-9]"
		expect "$c" "a Hunter given a free actor" "forge: hunter given a free actor \\([0-9a-f]{8}\\)"
		expect "$c" "a marine given a free actor" "forge: marine_armored given a free actor \\([0-9a-f]{8}\\)"
		expect "$c" "an Elite given a free actor" "forge: elite special given a free actor \\([0-9a-f]{8}\\)"
		expect "$c" "a Ghost spawned" "forge: spawned Ghost \\(ghost, vehicles\\)"
		expect "$c" "the AI budget refused past 32" "forge: AI budget full: 32 characters"
		expect "$c" "32 characters at most" "forge: budget [0-9]+/256 objects, 32/32 AI"
		if grep -aq "given a free actor (ffffffff)" "$log"; then
			fail "$c: a character got no actor"
		fi ;;
	fileshare)
		expect "$c" "FILE SHARE opened File Share" "ui: screen pc.h3.fileshare.screen"
		expect "$c" "the files listed" "menus: 4 map variants$"
		expect "$c" "Sample Gulch chosen" "menus: fileshare: map variants: Sample Gulch \(bloodgulch\) \(2 of 4\)"
		expect "$c" "renamed by Square, the name typed" "forge store: bloodgulch/good.hmv written: \"Renamed Gulch\""
		expect "$c" "Triangle asked first" "ui: screen pc.h3.fileshare.delete.screen"
		expect "$c" "A deleted it" "forge store: bloodgulch/good.hmv deleted"
		expect "$c" "a damaged file refused for Forge" "menus: fileshare: This file is damaged and can't be used."
		expect "$c" "and deleted" "forge store: bloodgulch/damaged.hmv deleted"
		expect "$c" "GAME VARIANTS listed" "menus: fileshare: game variants: Slayer"
		expect "$c" "A opened the Forge lobby with the variant" "menus: forge lobby caption: Missing Tag on Blood Gulch"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.main.screen' &&
			echo "  ok: $c: back at the main menu" || fail "$c: not back at the main menu"
		[ "$(grep -c "\.hmv$" "$out/fileshare-files.txt")" = 2 ] && echo "  ok: $c: two files left (missing, wrongmap)" ||
			fail "$c: $(cat "$out/fileshare-files.txt")" ;;
	edit)
		expect "$c" "Square while holding opened the properties" "ui: screen pc.h3.forge.properties.screen"
		expect "$c" "team red, respawn 45 s, not placed at start" "forge: properties: team 0, respawn 45 s, not placed at start"
		expect "$c" "placing the Warthog undone" "forge: undid Place Warthog \\(0 of 1 steps\\)"
		expect "$c" "and redone" "forge: redid Place Warthog \\(1 of 1 steps\\)"
		expect "$c" "snapping turned on" "forge: Snap: On"
		expect "$c" "the boulder grabbed" "forge: grabbed Boulder"
		expect "$c" "the move undone" "forge: undid Move Boulder \\(2 of 3 steps\\)"
		# (dropped snapped, both times: on the 0.25 grid along the ground)
		grep -a "forge: dropped Boulder" "$log" | sed 's/.* at \([^ ]*\) \([^ ]*\) .*/\1 \2/' > "$out/edit/drops.txt"
		if [ "$(wc -l < "$out/edit/drops.txt")" = 2 ] && awk '{ for (i = 1; i <= 2; i++) { v = ($i < 0 ? -$i : $i) * 4; f = v - int(v); if (f > 0.02 && f < 0.98) exit 1 } }' "$out/edit/drops.txt"; then
			echo "  ok: $c: dropped on the grid ($(tr '\n' ' ' < "$out/edit/drops.txt"))"
		else
			fail "$c: not dropped on the grid ($(tr '\n' ' ' < "$out/edit/drops.txt"))"
		fi
		expect "$c" "saved" "forge: map variant \"Edit Test\" saved as bloodgulch/edit_test.hmv, 2 objects"
		if grep -aq "record 0: vehicles.warthog.warthog kind 0 team 0 respawn 45 flags 0 " "$out/edit/dump.txt"; then
			echo "  ok: $c: the file keeps the Warthog's properties"
		else
			fail "$c: the file lacks the properties: $(cat "$out/edit/dump.txt")"
		fi ;;
	fstouch)
		expect "$c" "a tap on FILE SHARE opened it" "ui: screen pc.h3.fileshare.screen"
		expect "$c" "a tap on Rename renamed the first" "forge store: bloodgulch/missing.hmv written: \"Touched\""
		expect "$c" "a tap on a row opened the Forge lobby" "ui: screen pc.h3.forge.lobby"
		[ "$(grep -ac 'ui: screen pc.h3.fileshare.screen' "$log")" -ge 2 ] &&
			echo "  ok: $c: the lobby's Leave went back to File Share" || fail "$c: not back at File Share"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.main.screen' &&
			echo "  ok: $c: Back went to the main menu" || fail "$c: not back at the main menu" ;;
	off)
		if grep -aq "forge:" "$log"; then
			fail "$c: a forge line with no Forge"
		else
			echo "  ok: $c: no forge line"
		fi ;;
	esac
done

if [ -n "${HALO_TEST_SHOTS:-}" ]; then
	mkdir -p "$HALO_TEST_SHOTS"
	for c in $cases variants-save variants-load variants-missing variants-damaged variants-full panel-damaged; do
		for shot in "$out/$c"/shots/*.bmp; do
			[ -f "$shot" ] && cp "$shot" "$HALO_TEST_SHOTS/$c-$(basename "$shot")"
		done
		[ -f "$out/$c/run.log" ] && cp "$out/$c/run.log" "$HALO_TEST_SHOTS/$c-run.log"
	done
	echo "screenshots: $HALO_TEST_SHOTS"
fi
echo "logs: $out"
if [ "$status" = 0 ]; then
	echo "PASS"
fi
test_out_done $status
exit $status
