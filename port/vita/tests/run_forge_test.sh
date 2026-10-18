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
#
# Each must exit by itself, with no assertion or exception. The screenshots
# (HALO_SCREENSHOT_DIR, each run's shots folder) are kept with
# HALO_TEST_KEEP=1, or copied to HALO_TEST_SHOTS when it is set.
#
#   run_forge_test.sh [CASE...]      (default: v1 budget b30 off)
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
cases=${*:-v1 budget b30 off}
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

# (v1: the main menu: down to FORGE; the lobby: Map, down, A: Wizard; Map, up,
# A: Blood Gulch; down, START FORGE. In the level: forward and up, Square, A
# on VEHICLES, down twice, A: a Warthog in the monitor's hand; L + right stick
# right (turned), L + up (pushed away), R (dropped); Square, R (the next
# category), down, A: a weapon, R (dropped: it falls); Square, R twice
# (SCENERY), A: a boulder, R (dropped: scenery stays where it is let go), R
# (grabbed again, under the crosshair), left (carried), R (dropped);
# Triangle (deleted);
# D-pad up alone (rs: nothing for the monitor), Select + D-pad up (back+rs:
# the Spartan), forward, Select alone then D-pad up alone (the scoreboard,
# then zoom: neither flips), Select + D-pad up (the monitor); Start, down
# twice, A (LEAVE FORGE), A (leave))
v1pad="wait:150:3000 @00-main-menu:0:300 down:150:600 down:150:600 a:150:2500 @01-lobby:0:300"
v1pad="$v1pad a:150:2000 @02-map-picker:0:300 down:150:700 a:150:2000 a:150:2000 up:150:700 a:150:2000"
v1pad="$v1pad @03-lobby-blood-gulch:0:300 down:150:600 a:150:1500 level:150:2500 @04-monitor:0:300"
v1pad="$v1pad lup:1200:300 a:600:600 @05-flown:0:300 x:150:1500 @06-browser:0:300 a:150:1200"
v1pad="$v1pad @07-browser-vehicles:0:300 down:150:600 down:150:600 a:150:1500 @08-warthog-held:0:300"
v1pad="$v1pad lt+rright:900:300 lt+rup:500:600 @09-turned-pushed:0:300 rt:150:2500 @10-dropped:0:300"
v1pad="$v1pad x:150:1200 rt:150:1200 @11-browser-weapons:0:300 down:150:600 a:150:1500 rt:150:1500"
v1pad="$v1pad x:150:1200 rt:150:800 rt:150:1200 @12-browser-scenery:0:300 a:150:1500 rt:150:1000"
v1pad="$v1pad @13-boulder-dropped:0:300 rt:150:1000 lleft:700:600 @14-boulder-carried:0:300 rt:150:1500"
v1pad="$v1pad y:150:1500 @15-deleted:0:300 rs:150:1500 back+rs:150:2500 @16-spartan:0:300 lup:800:1500"
v1pad="$v1pad back:150:600 rs:150:1200 back+rs:150:2500"
v1pad="$v1pad @17-monitor-again:0:300 start:150:2000 @18-forge-menu:0:300 down:150:600 down:150:600 a:150:1500"
v1pad="$v1pad @19-leave-forge:0:300 a:150:9000 @20-main-menu-again:0:300"

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
	*) echo "unknown case $c"; exit 2 ;;
	esac
done
wait

for c in $cases; do
	[ -f "$out/$c/exit" ] || continue
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
	for c in $cases; do
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
