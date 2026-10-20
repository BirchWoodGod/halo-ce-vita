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
#               its hand, turns and tilts it (rotate mode: L + the right stick),
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
#   packs       other maps' objects (scene packs, built from the player's own
#               campaign maps): the lobby's EXTRA OBJECTS (Covenant vehicles
#               and dropships on by default, Covenant AI turned on), START
#               FORGE refused while a pack builds, then started; a Banshee, a
#               Pelican and an Elite placed through the browser (the AI
#               category); saved: the map variant names its packs; loaded
#               again in a new run with Covenant AI off in the lobby: the
#               variant brings it, its objects placed; the Pelican's seats
#   smallmap    Chill Out: the Warthog (the map's own tags) and the Banshee (a
#               pack's) in the browser, both placed
#   aiteam      the AI's team (the properties' AI TEAM, here the console's
#               "forge aiteam"): an Elite of the Covenant AI pack placed by the
#               monitor in Blood Gulch, the player then the Spartan beside it -
#               on the player's team it never hurts the player; on the
#               Covenant's it shoots the player's shield and body down
#   aioff       the AI's brain off (AI: OFF): an Elite of the Covenant's stands
#               where it was put and hurts nobody; the Forge menu's ALL AI (the
#               console's "forge allai") turns it on again and it fights
#   round       NEW ROUND: a Warthog, an Elite and an overshield placed; the
#               Warthog shoved away and wrecked, the Elite killed, the
#               overshield gone; a new round (the engine's map reset): all three
#               back whole at their placed spots, the player respawned, the
#               monitor where it flew
#   aisave      a map variant with the AI's settings (a Flood Elite with its AI
#               off, an Elite of the player's team, a Warthog) saved as version
#               2 (forge_files_test --dump), then started again from the
#               settings panel's route: its characters' teams and AI as saved
#   allai       the Forge menu's ALL AI: an Elite, a grunt, a jackal and a
#               hunter fight the Spartan; ALL AI (the console's "forge allai",
#               as the menu's row) turns them all off: for 10 s none moves (a
#               few centimetres at most), aims elsewhere or fires a shot; the
#               editor turns the Elite and it keeps the facing it was given; a
#               character placed then has its AI off too (it follows the
#               setting); one turned on by hand makes the row MIXED; ALL AI
#               turns them all off, then on, and they fight again
#   allaimenu   the Forge menu's row by the pad: ALL AI: MIXED, A (OFF), A
#               (ON), screenshots of each
#   flood       the Flood AI pack (built from The Library): an infection form
#               (a swarm of its own), a carrier and two combat forms placed
#               by the Spartan; they attack him; the carrier bursts and its
#               infection forms (the game's, not the layout's) live; ALL AI
#               turns those off too
#   clearai     the AI counter and CLEAR DEAD AI / CLEAR ALL AI (the
#               console's "forge clearai", as the Forge menu's rows): the Flood
#               AI pack's characters fill the AI budget (32), a carrier
#               destroyed (its infection forms the game's, never counted), an
#               infection form and two combat forms killed: the HUD counts 28
#               alive and 4 back next round, a 33rd refused with "Clear dead AI
#               to place more"; CLEAR DEAD AI frees their places, another
#               placed; NEW ROUND brings back only what the layout still has;
#               CLEAR ALL AI empties it, and a map variant saved after has only
#               the Warthog (forge_files_test --dump)
#   clearaimenu the Forge menu's rows by the pad: CLEAR DEAD AI (1), CLEAR ALL
#               AI asked first (B keeps them, A clears them), screenshots
#   rotate      rotate mode (L held while holding an object), by the console's
#               pad (forge press, forge stick, forge look as the mouse): a
#               boulder held facing the camera, the snapping stepped to 45
#               (L + D-pad right, twice), one flick of the right stick across
#               (yaw), one up (pitch: its top away), one across with R held too
#               (roll): its forward and up checked at each; R in rotate mode
#               never drops it; the exact angles (its properties' YAW, PITCH,
#               ROLL, RESET ROTATION: the console's "forge property"); the
#               mouse's look as the keyboard gives it (a yaw of two steps, a
#               roll); dropped, saved, a new round (the same facing again), and
#               the map variant loaded in another run (the same facing)
#   floodcov    the Flood and the Covenant fight each other (the monitor
#               watching): both sides hurt or killed
#   sentinels   the Sentinel AI pack: sentinels (the Sentinels' team) fight
#               the Flood: the Flood hurt or killed
#   marines     the Marine AI pack (built from Halo): marines (the humans'
#               team, the player's allies as in the campaign) beside the
#               Spartan never hurt him (marinesally); with the Covenant there,
#               they shoot the Covenant
#   aivariant   a map variant with a character of each AI pack (one with its AI
#               off) saved; it names the four packs; started again with no
#               pack chosen: it brings them, places all four as saved
#   packmaps    (not by default) every multiplayer map with every pack on
#               (HALO_TEST_FORGE_PACKS): what each loads or leaves out, the
#               tag cache they hold and leave; a table in its log
#   cepelican   the flyable Pelican (app0:packs/ce_pelican.txt: built from the
#               player's Halo Custom Edition map Extinction, scene_pack_ce.c),
#               with HALO_TEST_CE_MAPS' extinction.map and the Halo PC resource
#               maps: the lobby's EXTRA OBJECTS lists Extinction Pelican, on, and
#               ready once built; START FORGE loads it with the others; placed
#               (the browser's Extinction Pelican), ridden, flown up and back down
#               with passengers; skipped without extinction.map
#   pelican     the campaign's Pelican flown (app0:packs/dropships.txt's tune
#               flyable_pelican: scene_pack.c), with no extinction.map: the
#               dropships pack built from the player's b30 (or a50) tuned and
#               ready in EXTRA OBJECTS; the Pelican placed; every seat but
#               the cargo one a player's (the pilot's and ten riders'); the
#               player walks up to its nose and the action button seats him
#               as its pilot, again gets him out (on foot by the nose), and in
#               again; three passengers aboard; flown up and back down
#   cepelicanoff the same lobby without extinction.map: Extinction Pelican
#               unavailable, the page says why; Forge starts without it, saying
#               nothing in the game
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
#   run_forge_test.sh [CASE...]      (default: v1 budget fly b30 off files variants
#                                     edit fileshare fstouch panel packs smallmap
#                                     aiteam aioff round aisave allai allaimenu flood
#                                     clearai clearaimenu rotate floodcov sentinels marines
#                                     aivariant cepelican cepelicanoff pelican)
#   aipacks     (not by default) the AI packs' tag cache on Blood Gulch, the
#               tightest map: each AI pack with the packs on by default, and
#               every pack at once (the Extinction Pelican's extinction.map from
#               HALO_TEST_CE_MAPS when there); a table in its log
#   HALO_TEST_CE_MAPS     Custom Edition maps (default ../custom-maps-dl beside
#                         the tree: extinction.map for cepelican)
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
cases=${*:-v1 budget fly b30 off files variants edit fileshare fstouch panel packs smallmap aiteam aioff round aisave allai allaimenu aicopy flood clearai clearaimenu rotate floodcov sentinels marines aivariant pelican aimapfallback}
ce_maps=${HALO_TEST_CE_MAPS:-$root/../custom-maps-dl}
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
# itself; down past EXTRA OBJECTS, START FORGE. In the level: forward and up, Square, A
# on VEHICLES, down twice, A: a Warthog in the monitor's hand; L + right stick
# right (turned), L + up (tilted away), R (dropped); Square, R (the next
# category), down, A: a weapon, R (dropped: it falls); Square, R twice
# (SCENERY), A: a boulder, R (dropped: scenery stays where it is let go), R
# (grabbed again, under the crosshair), left (carried), R (dropped);
# Triangle (deleted);
# D-pad up alone (rs: nothing for the monitor), Select + D-pad up (back+rs:
# the Spartan), forward, Select alone then D-pad up alone (the scoreboard,
# then zoom: neither flips), Select + D-pad up (the monitor); Start, down
# ten times (past SAVE MAP VARIANT, UNDO, REDO, SNAP, ALL AI, CLEAR DEAD AI,
# CLEAR ALL AI, NEW ROUND), A (LEAVE FORGE), A (leave))
v1pad="wait:150:3000 @00-main-menu:0:300 down:150:600 down:150:600 a:150:2500 @01-lobby:0:300"
v1pad="$v1pad a:150:2000 @02-map-picker:0:300 down:150:700 a:150:1500 @02b-wizard-variants:0:300 a:150:2000"
v1pad="$v1pad a:150:2000 up:150:700 a:150:1500 a:150:2000"
v1pad="$v1pad @03-lobby-blood-gulch:0:300 down:150:600 down:150:600 a:150:1500 level:150:2500 @04-monitor:0:300"
v1pad="$v1pad lup:1200:300 a:600:600 @05-flown:0:300 x:150:1500 @06-browser:0:300 a:150:1200"
v1pad="$v1pad @07-browser-vehicles:0:300 down:150:600 down:150:600 a:150:1500 @08-warthog-held:0:300"
v1pad="$v1pad lt+rright:900:300 lt+rup:500:600 @09-turned-pushed:0:300 rt:150:2500 @10-dropped:0:300"
v1pad="$v1pad x:150:1200 rt:150:1200 @11-browser-weapons:0:300 down:150:600 a:150:1500 rt:150:1500"
v1pad="$v1pad x:150:1200 rt:150:800 rt:150:1200 @12-browser-scenery:0:300 a:150:1500 rt:150:1000"
v1pad="$v1pad @13-boulder-dropped:0:300 rt:150:1000 lleft:700:600 @14-boulder-carried:0:300 rt:150:1500"
v1pad="$v1pad y:150:1500 @15-deleted:0:300 rs:150:1500 back+rs:150:2500 @16-spartan:0:300 lup:800:1500"
v1pad="$v1pad back:150:600 rs:150:1200 back+rs:150:2500"
v1pad="$v1pad @17-monitor-again:0:300 start:150:2000 @18-forge-menu:0:300 down:150:600 down:150:600 down:150:600"
v1pad="$v1pad down:150:600 down:150:600 down:150:600 down:150:600 down:150:600 down:150:600 down:150:600 a:150:1500"
v1pad="$v1pad @19-leave-forge:0:300 a:150:9000 @20-main-menu-again:0:300"

# (variants: the main menu's FORGE, the lobby; START FORGE (the map, no
# variant); a Warthog from VEHICLES, dropped; Start, SAVE MAP VARIANT (its
# name typed), B; a boulder from SCENERY, dropped; Start, SAVE MAP VARIANT
# (the same name: the file replaced), down to LEAVE FORGE, leave)
lobby="wait:150:3000 down:150:600 down:150:600 a:150:2500"
savepad="$lobby down:150:600 down:150:600 a:150:1500 level:150:2500 x:150:1500 a:150:1200 down:150:600 down:150:600"
savepad="$savepad a:150:1500 rt:150:2000 start:150:2000 down:150:600 down:150:600 a:150:2500 @00-saved:0:300 b:150:1500"
savepad="$savepad x:150:1500 rt:150:800 rt:150:800 rt:150:1200 a:150:1500 rt:150:2000 start:150:2000 down:150:600"
savepad="$savepad down:150:600 a:150:2500 @01-saved-again:0:300 down:150:600 down:150:600 down:150:600 down:150:600"
savepad="$savepad a:150:1500 a:150:9000"
# (the lobby's MAP: the maps (Blood Gulch chosen), A: its variants (the map
# itself first), down N times, A; START FORGE)
pick() {
	local p="$lobby a:150:2000 a:150:1500 @00-picker:0:300" i
	for i in $(seq 1 "$1"); do p="$p down:150:700"; done
	echo "$p @01-picked:0:300 a:150:2000 @02-lobby:0:300 down:150:600 down:150:600 a:150:1500 level:150:3000 @03-placed:0:300"
}
# (full: Sample Gulch chosen and started; Start, SAVE MAP VARIANT twice:
# over its file, then as a new one, both failing)
fullpad="$(pick 3) start:150:2000 down:150:600 down:150:600 a:150:2500 @04-full:0:300 a:150:2500 @05-full-new:0:300"
# (fileshare: FILE SHARE; down (Sample Gulch), Square (renamed), Triangle,
# A (deleted); down (damaged.hmv), A (refused), Triangle, A (deleted);
# right (GAME VARIANTS), left; A on Missing Tag (the Forge lobby), B, B)
fspad="wait:150:3000 down:150:600 down:150:600 down:150:600 a:150:2500 @00-fileshare:0:300"
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
editpad="$lobby down:150:600 down:150:600 a:150:1500 level:150:2500 x:150:1500 a:150:1200 down:150:600 down:150:600"
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
	run edit 90 "" "" "$editpad" HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store" "HALO_TEST_TEXT_INPUT=Edit Test" \
		HALO_TEST_FORGE_PACKS=none
	"$out/forge_files_test" --dump "$store/forge/bloodgulch/edit_test.hmv" > "$out/edit/dump.txt" 2>&1
}

variants_runs() {
	local store="$out/variants-store"
	rm -rf "$store"
	mkdir -p "$store"
	run variants-save 90 "" "" "$savepad" HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store" HALO_TEST_FORGE_PACKS=none \
		"HALO_TEST_TEXT_INPUT=Gulch Test|Gulch Test"
	samples "$store"
	md5sum "$store/forge/bloodgulch/good.hmv" > "$out/variants-good.md5"
	run variants-load 50 "" "" "$(pick 1)" HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store"
	run variants-missing 50 "" "" "$(pick 2)" HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store"
	run variants-damaged 50 "" "" "$lobby a:150:2000 a:150:1500 down:150:700 down:150:700 down:150:700 @00-damaged:0:300 b:150:1500 b:150:1500 down:150:600 down:150:600 a:150:1500 level:150:2500 @01-none:0:300" \
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

# (packs: FORGE; EXTRA OBJECTS, down to Covenant AI, A (on), B; START FORGE
# at once: refused, a pack is being built; a moment, START FORGE. In the
# level: Square, A (VEHICLES), A (the Banshee), R (dropped); Square, down
# three (the Pelican), A, R; Square, R four times (AI), A (an Elite), R;
# Start, SAVE MAP VARIANT (typed), B)
packspad="$lobby down:150:500 a:150:1500 @00-extra-objects:0:300 down:150:400 down:150:400 a:150:100 @01-ai-on:0:200"
packspad="$packspad b:150:100 down:150:100 a:150:1200 @02-refused:0:300 wait:150:6000 a:150:1500 level:150:3000"
packspad="$packspad x:150:1500 a:150:1200 @03-vehicles:0:300 a:150:1500 rt:150:2000 x:150:1200 down:150:500 down:150:500"
packspad="$packspad down:150:500 a:150:1500 lup:400:300 rt:150:2000 @04-pelican:0:300 x:150:1200 rt:150:600 rt:150:600"
packspad="$packspad rt:150:600 rt:150:1200 @05-ai:0:300 a:150:1500 rt:150:3000 @06-elite:0:300 start:150:2000"
packspad="$packspad down:150:600 down:150:600 a:150:2500 @07-saved:0:300 b:150:1500"
# (smallmap: Square, A (VEHICLES), A (the Banshee), R; Square, down six (the
# Warthog), A, R)
smallpad="wait:150:1000 level:150:3000 x:150:1500 a:150:1200 @00-vehicles:0:300 a:150:1500 rt:150:2000 x:150:1200"
smallpad="$smallpad down:150:400 down:150:400 down:150:400 down:150:400 down:150:400 down:150:400 a:150:1500 rt:150:2000"
smallpad="$smallpad @01-placed:0:300"

packs_runs() {
	local store="$out/packs-store"
	rm -rf "$store"
	mkdir -p "$store"
	run packs 120 "" "L900:forge seats pelican" "$packspad" HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store" \
		"HALO_TEST_TEXT_INPUT=Pack Test"
	"$out/forge_files_test" --dump "$store/forge/bloodgulch/pack_test.hmv" > "$out/packs/dump.txt" 2>&1
	run packs-load 70 "" "" "$(pick 1)" HALO_MENU_STYLE=halo3 HALO_SAVE_ROOT="$store"
	mkdir -p "$out/packs"
}

# (cepelican: EXTRA OBJECTS, down three to Extinction Pelican, the builds
# waited for; back, START FORGE; in the game the console places and boards
# it, the pad flies it: up, along, down)
cepelicanpad="$lobby down:150:500 a:150:1500 down:150:400 down:150:400 down:150:400 wait:150:9000 @00-extra-objects:0:300"
cepelicanpad="$cepelicanpad b:150:300 down:150:300 a:150:1500 level:150:12000 lup+rup:1500:0 lup:2000:0 rdown:2500:0"
cepelicanpad="$cepelicanpad lup:3000:0 rup:1000:0 wait:12000:0"

cepelican_runs() {
	local folder="$out/cepelican-data" file cmds t
	rm -rf "$folder"
	mkdir -p "$folder/maps"
	for file in "$(cd "$data" && pwd)"/maps/*.map; do
		ln -sfn "$file" "$folder/maps/"
	done
	ln -sfn "$(cd "$ce_maps" && pwd)/extinction.map" "$folder/maps/extinction.map"
	# (turned to face down the valley, away from the canyon's wall)
	cmds="L100:forge camera 60 -100 3 130 -5;L102:forge list vehicles;L105:forge spawn epelican;L107:forge distance 16"
	cmds="$cmds;L108:forge turn 180;L109:forge drop;L120:forge off;L150:pack ride;L155:pack board 3;L160:@shot 01-aboard"
	for t in $(seq 200 30 1200); do cmds="$cmds;L$t:pack where"; done
	cmds="$cmds;L330:@shot 02-flying;L1150:pack riders;L1160:@shot 03-down"
	data_saved=$data
	data=$folder
	run cepelican 90 "" "$cmds" "$cepelicanpad" HALO_MENU_STYLE=halo3
	data=$data_saved
}

# (pelican: EXTRA OBJECTS, down one to Dropships, its build waited for;
# back, START FORGE; in the game the console places the Pelican and puts the
# player at its pilot's entrance; the pad's action button (X) seats him,
# gets him out, seats him again, then flies it: up, along, down)
pelicanpad="$lobby down:150:500 a:150:1500 down:150:400 wait:150:6000 @00-extra-objects:0:300 b:150:300 down:150:300"
pelicanpad="$pelicanpad a:150:1500 level:150:12000 x:200:2500 @01-pilot:0:300 x:200:2500 @02-out:0:300 x:200:2500"
pelicanpad="$pelicanpad lup+rup:1500:0 lup:2000:0 rdown:2500:0 lup:3000:0 rup:1000:0 wait:12000:0"

pelican_runs() {
	local cmds t
	# (turned to face down the valley, away from the canyon's wall)
	cmds="L100:forge camera 60 -100 3 130 -5;L102:forge list vehicles;L105:forge spawn pelican;L107:forge distance 16"
	cmds="$cmds;L108:forge turn 180;L109:forge drop;L120:forge off;L300:pack approach 0;L330:pack approach 0"
	cmds="$cmds;L335:pack vehicle pelican"
	for t in $(seq 345 15 600); do cmds="$cmds;L$t:pack where"; done
	cmds="$cmds;L620:pack board 3;L640:@shot 03-aboard"
	for t in $(seq 650 30 1300); do cmds="$cmds;L$t:pack where"; done
	cmds="$cmds;L800:@shot 04-flying;L1250:pack riders;L1260:@shot 05-down"
	run pelican 100 "" "$cmds" "$pelicanpad" HALO_MENU_STYLE=halo3
}

smallmap_runs() {
	local store="$out/smallmap-store"
	rm -rf "$store"
	mkdir -p "$store"
	run smallmap 70 "" "L300:forge list vehicles" "$smallpad" HALO_MENU_STYLE=classic HALO_SAVE_ROOT="$store" \
		HALO_SYSTEM_LINK_TEST=forge:6
}

# (every multiplayer map, every pack: forge.c's maps in their order)
packmaps_runs() {
	local store="$out/packmaps-store" index
	rm -rf "$store"
	mkdir -p "$store"
	for index in 0 1 2 3 4 5 6 7 8 9 10 11 12; do
		while [ "$(jobs -rp | wc -l)" -ge "$jobs" ]; do
			wait -n
		done
		run "packmaps-$index" 45 "" "L150:forge list vehicles;L155:forge list characters" "" HALO_MENU_STYLE=classic \
			HALO_SAVE_ROOT="$store" HALO_SYSTEM_LINK_TEST=forge:$index \
			HALO_TEST_FORGE_PACKS=covenant_vehicles,dropships,covenant_ai &
	done
	wait
	mkdir -p "$out/packmaps"
	echo 0 > "$out/packmaps/exit"
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
		"HALO_TEST_TAPS=4000:60,338 8000:220,430 12000:120,128 17000:140,455 22000:395,430"
	find "$store" -type f | sort > "$out/fstouch-files.txt"
}

# (the AI cases: Forge started from the settings panel's route on Blood Gulch
# with the Covenant AI pack; the monitor at a clear spot looking down the
# valley, an Elite placed 7 world units ahead; then the Spartan where the
# monitor was, and where the player is and how hurt, once a second)
ai_setup="L300:forge camera 60 -100 1.6 130 0;L305:forge spawn elite major plasma rifle;L306:forge distance 7"
ai_spartan="L320:forge camera 60 -100 0.8 130 -5;L322:forge ai;L325:forge off"
ai_watch() { # FIRST LAST STEP: "forge where" from tick FIRST to LAST
	local t w=""
	for t in $(seq "$1" "$3" "$2"); do w="$w;L$t:forge where"; done
	echo "$w"
}
ai_run() { # NAME SECONDS COMMANDS [VAR=value...]
	local name=$1 seconds=$2 commands=$3
	shift 3
	run "$name" "$seconds" "" "$commands" "" HALO_MENU_STYLE=classic HALO_SYSTEM_LINK_TEST=forge:9 \
		HALO_TEST_FORGE_PACKS=covenant_ai "$@"
}

aiteam_runs() {
	ai_run aiteam-player 40 "$ai_setup;L307:forge aiteam player;L308:forge drop;$ai_spartan$(ai_watch 360 600 30);L605:forge ai;L606:@shot 00-player-team"
	ai_run aiteam-covenant 40 "$ai_setup;L307:forge aiteam covenant;L308:forge drop;$ai_spartan$(ai_watch 360 600 30);L605:forge ai;L606:@shot 00-covenant-team"
	mkdir -p "$out/aiteam"
	echo 0 > "$out/aiteam/exit"
}

aioff_runs() {
	# (AI off: watched 9 s; then the monitor again, ALL AI (on), the Spartan
	# again, watched)
	ai_run aioff 55 "$ai_setup;L307:forge aiteam covenant;L308:forge brain off;L309:forge drop;$ai_spartan$(ai_watch 360 600 30);L604:@shot 00-ai-off;L605:forge ai;L620:forge on;L622:forge allai;L624:forge ai;L626:forge off$(ai_watch 700 1000 30);L1005:forge ai"
}

aicopy_runs() {
	# (the Elite set to the player's team with its AI off, then copied with
	# B (Circle) while held: the copy keeps both, not the defaults)
	ai_run aicopy 30 "$ai_setup;L307:forge aiteam player;L308:forge brain off;L310:forge press b;L314:forge distance 5;L316:forge drop;L330:forge ai"
}

round_runs() {
	# (a Warthog 8 ahead, the Elite 5, an overshield 3; their placed spots
	# listed; the Warthog shoved, then wrecked, the Elite killed, the
	# overshield taken out of the world; listed; NEW ROUND (the console's,
	# as the Forge menu's); the map's time starts again at 0: listed again)
	local c="L300:forge camera 60 -100 1.6 130 0;L302:forge spawn warthog;L303:forge distance 8;L304:forge drop"
	c="$c;L306:forge spawn elite major plasma rifle;L307:forge distance 5;L309:forge drop;L311:forge spawn overshield"
	c="$c;L312:forge distance 3;L313:forge drop;L400:forge objects;L401:forge where;L405:forge shove #1 0.6 0 0.4"
	c="$c;L440:forge kill #1;L442:forge kill #2;L444:forge destroy #3;L480:forge objects;L481:@shot 00-before-round"
	c="$c;L485:forge round;L500:forge objects;L502:forge ai;L505:forge where;L510:@shot 01-after-round"
	ai_run round 45 "$c"
}

aisave_runs() {
	local store="$out/aisave-store"
	rm -rf "$store"
	mkdir -p "$store"
	local c="$ai_setup;L307:forge aiteam flood;L308:forge brain off;L309:forge drop;L311:forge spawn elite minor plasma rifle"
	c="$c;L312:forge distance 4;L313:forge aiteam player;L314:forge drop;L316:forge spawn warthog;L317:forge distance 9"
	c="$c;L318:forge drop;L400:forge ai;L405:forge save AI Test"
	ai_run aisave 30 "$c" HALO_SAVE_ROOT="$store"
	"$out/forge_files_test" --dump "$store/forge/bloodgulch/ai_test.hmv" > "$out/aisave/dump.txt" 2>&1
	run aisave-load 30 "" "L400:forge ai" "" HALO_MENU_STYLE=classic HALO_SYSTEM_LINK_TEST=forge:9:ai_test.hmv \
		HALO_TEST_FORGE_PACKS=covenant_ai HALO_SAVE_ROOT="$store"
}

# ("forge ai" and "forge where" from tick FIRST to LAST)
ai_looks() { # FIRST LAST STEP
	local t w=""
	for t in $(seq "$1" "$3" "$2"); do w="$w;L$t:forge ai;L$((t + 1)):forge where"; done
	echo "$w"
}

allai_runs() {
	# (four Covenant fight the Spartan; ALL AI (off), watched 10 s; the
	# editor turns the Elite and places another Elite; watched; the grunt's
	# AI on by hand (MIXED), ALL AI (off), ALL AI (on); watched)
	local c="L200:(set cheat_deathless_player true);L300:forge camera 60 -100 1.6 120 0;L302:forge spawn elite major plasma rifle"
	c="$c;L303:forge distance 7;L304:forge drop;L306:forge camera 60 -100 1.6 130 0;L308:forge spawn grunt minor plasma pistol"
	c="$c;L309:forge distance 6;L310:forge drop;L312:forge camera 60 -100 1.6 140 0;L314:forge spawn jackal minor;L315:forge distance 6"
	c="$c;L316:forge drop;L318:forge camera 60 -100 1.6 150 0;L320:forge spawn hunter;L321:forge distance 9;L322:forge drop"
	c="$c;L330:forge camera 60 -100 0.8 130 -5;L335:forge off$(ai_looks 360 480 60);L470:@shot 00-fighting"
	c="$c;L490:forge allai$(ai_looks 520 820 30);L700:@shot 01-all-ai-off"
	c="$c;L830:forge on;L833:forge grab #1;L835:forge turn 90;L837:forge drop;L840:forge camera 60 -100 1.6 105 0"
	c="$c;L842:forge spawn elite minor plasma rifle;L843:forge distance 6;L844:forge drop;L848:forge camera 60 -100 0.8 130 -5"
	c="$c;L850:forge off$(ai_looks 880 1000 30);L990:@shot 02-turned-and-placed"
	c="$c;L1010:forge on;L1013:forge grab #2;L1014:forge brain on;L1015:forge drop;L1018:forge ai;L1020:forge allai;L1022:forge ai"
	c="$c;L1024:forge allai;L1026:forge ai;L1030:forge camera 60 -100 0.8 130 -5;L1032:forge off$(ai_looks 1060 1360 30)"
	c="$c;L1250:@shot 03-all-ai-on"
	ai_run allai 60 "$c"
}

allaimenu_runs() {
	# (an Elite and a grunt, ALL AI off, the grunt's on again: MIXED; the
	# Forge menu by the pad: Start, down six to ALL AI, A, A)
	local c="L300:forge camera 60 -100 1.6 120 0;L302:forge spawn elite major plasma rifle;L303:forge distance 7;L304:forge drop"
	c="$c;L306:forge camera 60 -100 1.6 140 0;L308:forge spawn grunt minor plasma pistol;L309:forge distance 6;L310:forge drop"
	c="$c;L314:forge allai;L320:forge grab #2;L322:forge brain on;L324:forge drop;L326:forge ai"
	local pad="level:150:12000 start:150:2000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 down:150:800"
	pad="$pad @00-menu-mixed:0:300 a:150:1500 @01-menu-off:0:300 a:150:1500 @02-menu-on:0:300 b:150:1500"
	ai_run allaimenu 45 "$c" "HALO_TEST_PAD=$pad"
}

flood_runs() {
	# (the Spartan with an infection form, a carrier and two combat forms
	# around him, watched; the carrier destroyed if it has not burst on him: its
	# infection forms; the monitor, ALL AI off, the game's characters too)
	local c="L200:(set cheat_deathless_player true);L300:forge camera 60 -100 1.6 120 0;L302:forge spawn floodcombat elite shotgun"
	c="$c;L303:forge distance 8;L304:forge drop;L306:forge camera 60 -100 1.6 135 0;L308:forge spawn flood_infection;L309:forge distance 6"
	c="$c;L310:forge drop;L312:forge camera 60 -100 1.6 150 0;L314:forge spawn floodcarrier;L315:forge distance 9;L316:forge drop"
	c="$c;L318:forge camera 60 -100 1.6 105 0;L320:forge spawn floodcombat_human assault;L321:forge distance 10;L322:forge drop"
	c="$c;L330:forge camera 60 -100 0.8 130 -5;L335:forge off;L340:@shot 00-flood$(ai_looks 360 840 30);L420:@shot 01-flood-attacks"
	c="$c;L596:forge on;L600:forge destroy #3;L603:forge ai;L604:forge allai off;L606:forge ai;L640:forge ai"
	c="$c;L850:forge ai;L900:forge ai;L960:forge ai;L962:@shot 02-flood-ai-off"
	ai_run flood 45 "$c" HALO_TEST_FORGE_PACKS=flood_ai
}

clearai_runs() {
	# (a Warthog; ALL AI off so that they stand; 28 Flood combat forms, a
	# carrier, an infection form and two human combat forms: the AI budget
	# full; the carrier destroyed, the infection form and two combat forms
	# killed; a 33rd refused; the dead cleared; another placed; NEW ROUND (the
	# map's time starts again at 0); everything cleared; saved)
	local store="$out/clearai-store"
	rm -rf "$store"
	mkdir -p "$store"
	local c="L200:(set cheat_deathless_player true);L300:forge camera 60 -100 1.6 130 0;L302:forge spawn warthog"
	c="$c;L303:forge distance 8;L304:forge drop;L305:forge allai off;L306:forge fill 28 floodcombat elite shotgun"
	c="$c;L310:forge spawn floodcarrier;L311:forge distance 6;L312:forge drop;L314:forge spawn flood_infection"
	c="$c;L315:forge distance 5;L316:forge drop;L318:forge spawn floodcombat_human assault;L319:forge distance 7;L320:forge drop"
	c="$c;L322:forge spawn floodcombat_human shotgun;L323:forge distance 9;L324:forge drop;L326:forge budget"
	c="$c;L330:forge destroy #30;L332:forge kill #31;L334:forge kill #2;L336:forge kill #3;L360:forge budget;L361:forge ai"
	c="$c;L362:@shot 00-dead-apart;L364:forge spawn flood_infection;L370:forge clearai dead;L372:forge spawn flood_infection"
	c="$c;L373:forge distance 5;L374:forge drop;L376:forge budget;L378:forge objects;L390:forge round"
	c="$c;L520:forge budget;L522:forge objects;L530:forge clearai all;L532:forge budget;L534:forge ai;L536:forge save Clear Test"
	c="$c;L540:@shot 01-all-cleared;L560:forge clearai dead"
	ai_run clearai 60 "$c" HALO_SAVE_ROOT="$store" HALO_TEST_FORGE_PACKS=flood_ai
	"$out/forge_files_test" --dump "$store/forge/bloodgulch/clear_test.hmv" > "$out/clearai/dump.txt" 2>&1
}

clearaimenu_runs() {
	# (two Elites, one killed: the Forge menu by the pad: Start, down seven to
	# CLEAR DEAD AI, A; down, A (asked), B (kept), A (asked), A (cleared))
	local c="L300:forge camera 60 -100 1.6 120 0;L302:forge spawn elite major plasma rifle;L303:forge distance 7;L304:forge drop"
	c="$c;L306:forge camera 60 -100 1.6 140 0;L308:forge spawn grunt minor plasma pistol;L309:forge distance 6;L310:forge drop"
	c="$c;L312:forge allai off;L314:forge kill #1;L345:forge budget"
	local pad="level:150:12000 start:150:2000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500"
	pad="$pad down:150:800 @00-menu-clear-dead:0:300 a:150:1500 @01-dead-cleared:0:300 down:150:800 a:150:1500"
	pad="$pad @02-clear-all-asked:0:300 b:150:1500 @03-kept:0:300 a:150:1500 a:150:1500 @04-all-cleared:0:300 b:150:1500"
	ai_run clearaimenu 45 "$c" "HALO_TEST_PAD=$pad"
}

rotate_runs() {
	# (looking along +x, level: a boulder held 5 ahead facing the camera;
	# where it faces after each step - forge where's "held facing ... up ...")
	local store="$out/rotate-store"
	rm -rf "$store"
	mkdir -p "$store"
	local c="L300:forge camera 60 -100 1.6 0 0;L302:forge spawn boulder;L304:forge distance 5;L306:forge where"
	c="$c;L310:forge press lt+white 4;L320:forge press lt+white 4;L330:forge press lt 40;L332:forge stick 0 0 1 0 6"
	c="$c;L345:forge where;L350:forge press lt 40;L352:forge stick 0 0 0 1 6;L365:forge where;L370:forge press lt+rt 40"
	c="$c;L372:forge stick 0 0 1 0 6;L385:forge where;L388:@shot 00-rotated;L392:forge property resetrot"
	c="$c;L393:forge property yaw;L394:forge property pitch;L395:forge property roll;L396:forge property yaw -"
	c="$c;L397:forge property yaw;L400:forge where;L402:@shot 01-exact-angles"
	c="$c;L404:forge property resetrot;L410:forge press lt 30;L412:forge look -0.25 0;L425:forge where"
	c="$c;L430:forge press lt+rt 30;L432:forge look -0.1 0;L445:forge where;L450:forge property resetrot"
	c="$c;L451:forge property yaw;L452:forge property pitch;L453:forge property roll;L455:forge drop;L460:forge objects"
	c="$c;L462:forge save Rotate Test;L470:forge round;L520:forge objects;L525:@shot 02-after-round"
	ai_run rotate 50 "$c" HALO_SAVE_ROOT="$store" HALO_TEST_FORGE_PACKS=none
	run rotate-load 30 "" "L400:forge objects" "" HALO_MENU_STYLE=classic HALO_SYSTEM_LINK_TEST=forge:9:rotate_test.hmv \
		HALO_TEST_FORGE_PACKS=none HALO_SAVE_ROOT="$store"
}

floodcov_runs() {
	# (two of the Flood and two of the Covenant 12 world units apart, the
	# monitor over them)
	local c="L300:forge camera 50 -90 1.6 0 0;L302:forge spawn floodcombat elite plasma;L303:forge distance 6;L304:forge drop"
	c="$c;L306:forge spawn floodcombat_human shotgun;L307:forge distance 7;L308:forge drop;L310:forge camera 50 -90 1.6 180 0"
	c="$c;L312:forge spawn elite minor plasma rifle;L313:forge distance 6;L314:forge drop;L316:forge spawn grunt major plasma"
	c="$c;L317:forge distance 7;L318:forge drop;L320:forge camera 50 -82 12 -90 -50;L330:@shot 00-flood-covenant"
	c="$c$(ai_looks 340 1200 60);L540:@shot 01-flood-covenant-fight;L900:@shot 02-flood-covenant-after"
	ai_run floodcov 50 "$c" HALO_TEST_FORGE_PACKS=covenant_ai,flood_ai
}

sentinels_runs() {
	local c="L300:forge camera 50 -90 1.6 0 0;L302:forge spawn floodcombat elite plasma;L303:forge distance 6;L304:forge drop"
	c="$c;L306:forge spawn floodcombat_human assault;L307:forge distance 7;L308:forge drop;L310:forge camera 50 -90 2.5 180 0"
	c="$c;L312:forge spawn sentinel major;L313:forge distance 6;L314:forge drop;L316:forge camera 50 -90 2.5 170 0"
	c="$c;L318:forge spawn Sentinel;L319:forge distance 8;L320:forge drop;L322:forge camera 50 -82 12 -90 -50"
	c="$c;L330:forge list characters;L332:@shot 00-sentinels-flood$(ai_looks 340 1200 60);L460:@shot 01-sentinels-flood-fight"
	ai_run sentinels 50 "$c" HALO_TEST_FORGE_PACKS=flood_ai,sentinel_ai
}

marines_runs() {
	# (marinesally: two marines beside the Spartan, nobody else, watched;
	# marines: with an Elite and a grunt 8 world units off)
	local m="L200:(set cheat_deathless_player true);L300:forge camera 60 -100 1.6 100 0;L302:forge spawn marine\\marine assault rifle"
	m="$m;L303:forge distance 3;L304:forge drop;L306:forge camera 60 -100 1.6 160 0;L308:forge spawn marine_armored sniper rifle"
	m="$m;L309:forge distance 3;L310:forge drop"
	ai_run marinesally 40 "$m;L330:forge camera 60 -100 0.8 130 -5;L335:forge off;L340:@shot 00-marines$(ai_looks 360 900 60)" \
		HALO_TEST_FORGE_PACKS=marine_ai
	local c="$m;L312:forge camera 60 -100 1.6 120 0;L314:forge spawn elite minor plasma rifle;L315:forge distance 9;L316:forge drop"
	c="$c;L318:forge camera 60 -100 1.6 135 0;L319:forge spawn grunt minor plasma;L320:forge distance 8;L321:forge drop"
	c="$c;L330:forge camera 60 -100 0.8 130 -5;L335:forge off;L340:@shot 00-marines-covenant$(ai_looks 360 1200 60)"
	c="$c;L460:@shot 01-marines-fight"
	ai_run marines 50 "$c" HALO_TEST_FORGE_PACKS=covenant_ai,marine_ai
	mkdir -p "$out/marines"
}

aivariant_runs() {
	# (one of each AI pack's, the Flood's with its AI off, saved; started
	# again with no pack chosen)
	local store="$out/aivariant-store"
	rm -rf "$store"
	mkdir -p "$store"
	local c="L300:forge camera 60 -100 1.6 100 0;L302:forge spawn elite commander energy sword;L303:forge distance 6;L304:forge drop"
	c="$c;L306:forge camera 60 -100 1.6 120 0;L308:forge spawn floodcombat elite shotgun;L309:forge distance 7;L310:forge brain off"
	c="$c;L311:forge drop;L312:forge camera 60 -100 1.6 140 0;L314:forge spawn sentinel major;L315:forge distance 7;L316:forge drop"
	c="$c;L318:forge camera 60 -100 1.6 160 0;L320:forge spawn marine\\marine assault rifle;L321:forge distance 6;L322:forge drop"
	c="$c;L330:forge ai;L335:forge save AI Packs"
	ai_run aivariant 30 "$c" HALO_SAVE_ROOT="$store" HALO_TEST_FORGE_PACKS=covenant_ai,flood_ai,sentinel_ai,marine_ai
	"$out/forge_files_test" --dump "$store/forge/bloodgulch/ai_packs.hmv" > "$out/aivariant/dump.txt" 2>&1
	run aivariant-load 40 "" "L400:forge ai" "" HALO_MENU_STYLE=classic HALO_SYSTEM_LINK_TEST=forge:9:ai_packs.hmv \
		HALO_TEST_FORGE_PACKS=none HALO_SAVE_ROOT="$store"
}

# (the AI packs on Blood Gulch: each with the packs on by default, and all;
# with extinction.map for the Extinction Pelican when there)
aipacks_runs() {
	local folder="$out/aipacks-data" file index=0 set
	rm -rf "$folder"
	mkdir -p "$folder/maps"
	for file in "$(cd "$data" && pwd)"/maps/*.map; do
		ln -sfn "$file" "$folder/maps/"
	done
	[ -r "$ce_maps/extinction.map" ] && ln -sfn "$(cd "$ce_maps" && pwd)/extinction.map" "$folder/maps/extinction.map"
	data_saved=$data
	data=$folder
	for set in covenant_vehicles,dropships covenant_vehicles,dropships,covenant_ai \
		covenant_vehicles,dropships,flood_ai covenant_vehicles,dropships,sentinel_ai \
		covenant_vehicles,dropships,marine_ai covenant_ai,flood_ai,sentinel_ai,marine_ai \
		covenant_vehicles,dropships,covenant_ai,flood_ai,sentinel_ai,marine_ai; do
		while [ "$(jobs -rp | wc -l)" -ge "$jobs" ]; do
			wait -n
		done
		echo "$set" > "$out/aipacks-$index.set"
		run "aipacks-$index" 70 "" "L400:forge list characters" "" HALO_MENU_STYLE=classic \
			HALO_SAVE_ROOT="$out/aipacks-store-$index" HALO_SYSTEM_LINK_TEST=forge:9 HALO_TEST_FORGE_PACKS=$set &
		index=$((index + 1))
	done
	wait
	data=$data_saved
	mkdir -p "$out/aipacks"
	echo 0 > "$out/aipacks/exit"
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
		# (no packs: the map's own three vehicles, as v1 was written)
		run "$c" 110 "" "" "$v1pad" HALO_MENU_STYLE=halo3 HALO_TEST_FORGE_PACKS=none & ;;
	budget)
		run "$c" 50 "$bloodgulch" "L200:forge on;L210:forge fill 260 boulder;L230:forge budget;L240:@shot 00-budget-full;L260:forge delete;L270:forge budget;L280:forge spawn boulder;L285:forge drop;L290:forge budget;L300:@shot 01-budget-after" "" & ;;
	fly)
		run "$c" 40 "$bloodgulch" "L200:forge on;L230:forge camera 60 -120 5 0 45;L240:forge where;L250:forge stick 0 1 0 0 60;L320:forge where;L330:forge camera 60 -120 5 0 -45;L340:forge where;L350:forge stick 0 1 0 0 60;L420:forge where" "" & ;;
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
	packs)
		if [ "$menus_files" = 0 ]; then
			echo "SKIP: packs: no Halo PC bitmaps.map and loc.map in $data/maps"
			continue
		fi
		packs_runs & ;;
	smallmap)
		smallmap_runs & ;;
	aiteam)
		aiteam_runs & ;;
	aioff)
		aioff_runs & ;;
	aicopy)
		aicopy_runs & ;;
	round)
		round_runs & ;;
	aisave)
		aisave_runs & ;;
	allai)
		allai_runs & ;;
	allaimenu)
		allaimenu_runs & ;;
	flood)
		flood_runs & ;;
	clearai)
		clearai_runs & ;;
	clearaimenu)
		clearaimenu_runs & ;;
	rotate)
		rotate_runs & ;;
	floodcov)
		floodcov_runs & ;;
	sentinels)
		sentinels_runs & ;;
	marines)
		marines_runs & ;;
	aivariant)
		aivariant_runs & ;;
	aimapfallback)
		# (covenant_ai's recipe with The Silent Cartographer named first: it
		# lacks a needler elite, so the build moves on to Two Betrayals - as
		# when b30's copy is the one read in place on the Vita)
		mkdir -p "$out/aimapfallback-packs"
		cp "$root/port/vita/app0/packs/"*.txt "$out/aimapfallback-packs/"
		sed -i 's/^map b40 b30 a50$/map b30 b40 a50/' "$out/aimapfallback-packs/covenant_ai.txt"
		ai_run aimapfallback 120 "L600:forge budget" HALO_PACKS_FOLDER="$out/aimapfallback-packs" & ;;
	aipacks)
		# (its runs take the cases' slots)
		wait
		aipacks_runs ;;
	cepelican)
		if [ "$menus_files" = 0 ] || [ ! -r "$ce_maps/extinction.map" ] || [ ! -r "$data/maps/sounds.map" ]; then
			echo "SKIP: cepelican: no extinction.map in $ce_maps (HALO_TEST_CE_MAPS), or no Halo PC resource maps in $data/maps"
			continue
		fi
		cepelican_runs & ;;
	cepelicanoff)
		if [ "$menus_files" = 0 ]; then
			echo "SKIP: cepelicanoff: no Halo PC bitmaps.map and loc.map in $data/maps"
			continue
		fi
		if [ -r "$data/maps/extinction.map" ]; then
			echo "SKIP: cepelicanoff: $data/maps has extinction.map"
			continue
		fi
		run cepelicanoff 45 "" "" "$lobby down:150:500 a:150:1500 down:150:400 down:150:400 down:150:400 wait:150:4000 @00-unavailable:0:300 b:150:300 down:150:300 a:150:1500 level:150:3000" HALO_MENU_STYLE=halo3 & ;;
	pelican)
		if [ "$menus_files" = 0 ] || { [ ! -r "$data/maps/b30.map" ] && [ ! -r "$data/maps/a50.map" ]; }; then
			echo "SKIP: pelican: no Halo PC bitmaps.map and loc.map, or no b30.map or a50.map, in $data/maps"
			continue
		fi
		pelican_runs & ;;
	packmaps)
		# (the maps two at a time: the cases' slots all its own)
		wait
		packmaps_runs ;;
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
	if [ "$c" = packmaps ]; then
		printf '%-16s %-62s %s\n' map "packs loaded / left out (the tag cache held, free after them)" palette
		for index in 0 1 2 3 4 5 6 7 8 9 10 11 12; do
			r="packmaps-$index"
			checked "$r" || continue
			l="$out/$r/run.log"
			map=$(grep -a "forge: a Forge game of" "$l" | head -1 | grep -ao '[^\\]* asked for' | sed 's/ asked for//')
			packs=$(grep -a "forge: packs: loaded:" "$l" | head -1 | sed 's/.*forge: packs: //')
			free=$(grep -a "scene packs: .*: loaded from" "$l" | tail -1 | sed 's/.*held by packs, \([0-9]* KB\) free after them.*/\1 free/')
			veh=$(grep -a "forge: palette vehicles:" "$l" | tail -1 | sed 's/.*: //')
			ai=$(grep -a "forge: palette characters:" "$l" | tail -1 | sed 's/.*: //')
			printf '%-16s %-62s vehicles %s, AI %s\n' "$map" "$packs ($free)" "$veh" "$ai"
			grep -aq "forge: the Forge game of .* starts" "$l" || fail "$r: the Forge game did not start"
		done
		continue
	fi
	if [ "$c" = cepelican ]; then
		if checked cepelican; then
			log=$out/cepelican/run.log
			expect cepelican "EXTRA OBJECTS lists Extinction Pelican, built and ready" \
				"menus: forge packs: Extinction Pelican: Ready: its objects are in the object browser"
			expect cepelican "built from Extinction" "scene packs: ce_pelican: built from extinction"
			expect cepelican "loaded with the others" "forge: packs: loaded: Covenant vehicles, Dropships, Extinction Pelican; left out: none"
			expect cepelican "the browser offers it" "forge: vehicles [0-9]+: Extinction Pelican"
			expect cepelican "placed" "forge: dropped Extinction Pelican"
			expect cepelican "ridden" "scene packs: ride: vehicles.epelican.epelican entered"
			expect cepelican "three passengers" "scene packs: board: 3 passengers aboard"
			heights=$(grep -a "where: the vehicle at" "$log" | sed 's/.* at [^ ]* [^ ]* \([-0-9.]*\),.*/\1/')
			first=$(echo "$heights" | head -1)
			high=$(echo "$heights" | sort -n | tail -1)
			last=$(echo "$heights" | tail -1)
			echo "  cepelican: heights from $first up to $high, down to $last"
			awk -v f="$first" -v h="$high" -v l="$last" 'BEGIN { exit !(h - f >= 15 && h - l >= 8) }' &&
				echo "  ok: cepelican: flown up and back down" || fail "cepelican: not flown up and down ($first, $high, $last)"
			expect cepelican "the pilot and passengers aboard" "scene packs: riders: 4 in vehicles.epelican.epelican"
		fi
		continue
	fi
	if [ "$c" = pelican ]; then
		if checked pelican; then
			log=$out/pelican/run.log
			expect pelican "EXTRA OBJECTS lists Dropships, built and ready" \
				"menus: forge packs: Dropships: Ready: its objects are in the object browser"
			expect pelican "the Pelican tuned as the pack was built" \
				"scene packs: tune flyable_pelican: vehicles.pelican.pelican: its driver's seat the player's, 10 riders' seats"
			expect pelican "loaded" "forge: packs: loaded: Covenant vehicles, Dropships"
			expect pelican "placed" "forge: dropped Pelican \(pelican\)"
			count pelican "every seat but the cargo one a player's" \
				"scene packs: vehicle: seat [0-9]+: .*the player can take it" 11
			expect pelican "the pilot's seat the one by its nose" \
				"scene packs: approach: the player at seat 0's entrance of vehicles.pelican.pelican .*: the nearby seat is 0 \(free\)"
			# (where the player is, sample by sample until the passengers board:
			# on foot, the Pelican's pilot, on foot, its pilot again)
			states=$(grep -a "scene packs: \(where: the player\|board: 3\)" "$log" | sed '/board: 3/q' |
				sed -n 's/.*, in \(no vehicle\|vehicles.pelican.pelican\).*/\1/p' | uniq | tr '\n' ' ')
			[ "$states" = "no vehicle vehicles\\pelican\\pelican no vehicle vehicles\\pelican\\pelican " ] &&
				echo "  ok: pelican: the action button seats the pilot, gets him out, seats him again" ||
				fail "pelican: not in, out and in again by the action button ($states)"
			expect pelican "three passengers" "scene packs: board: 3 passengers aboard"
			heights=$(grep -a "where: the vehicle at" "$log" | sed 's/.* at [^ ]* [^ ]* \([-0-9.]*\),.*/\1/')
			first=$(echo "$heights" | head -1)
			high=$(echo "$heights" | sort -n | tail -1)
			last=$(echo "$heights" | tail -1)
			echo "  pelican: heights from $first up to $high, down to $last"
			awk -v f="$first" -v h="$high" -v l="$last" 'BEGIN { exit !(h - f >= 15 && h - l >= 8) }' &&
				echo "  ok: pelican: flown up and back down" || fail "pelican: not flown up and down ($first, $high, $last)"
			expect pelican "the pilot and passengers aboard" "scene packs: riders: 4 in vehicles.pelican.pelican"
		fi
		continue
	fi
	if [ "$c" = cepelicanoff ]; then
		if checked cepelicanoff; then
			expect cepelicanoff "the page says why it is unavailable" \
				"menus: forge packs: Extinction Pelican: Unavailable: extinction.map \(Halo Custom Edition\) is not in the maps folder"
			expect cepelicanoff "Forge starts without it" "forge: packs: ce_pelican: unavailable: extinction.map"
			expect cepelicanoff "the others loaded, nothing said of it in the game" \
				"forge: packs: loaded: Covenant vehicles, Dropships; left out: none"
		fi
		continue
	fi
	if [ "$c" = packs ]; then
		checked packs
		checked packs-load
		expect packs "EXTRA OBJECTS opened" "ui: screen pc.h3.forge.packs.screen"
		expect packs "the vehicles' packs built for the lobby" "forge: packs: covenant_vehicles wanted for the lobby"
		expect packs "Covenant AI turned on" "forge: packs: covenant_ai on"
		expect packs "START FORGE refused while it builds" "menus: forge lobby: not started: Covenant AI is being built"
		expect packs "then started, the three packs loaded" \
			"forge: packs: loaded: Covenant vehicles, Dropships, Covenant AI; left out: none"
		expect packs "the Banshee placed through the browser" "forge: spawned Banshee \\(banshee, vehicles\\)"
		expect packs "the Pelican placed through the browser" "forge: spawned Pelican \\(pelican, vehicles\\)"
		expect packs "an Elite placed through the AI category" \
			"forge: spawned Elite Commander Energy Sword \\(elite commander energy sword, characters\\)"
		expect packs "the Elite has its actor" "forge: elite special given a free actor \\([0-9a-f]{8}\\)"
		expect packs "the Pelican's seats said: the pilot's one a player takes (flyable_pelican)" "forge: seat 0: W-driver, driver"
		expect packs "saved" "forge: map variant \"Pack Test\" saved as bloodgulch/pack_test.hmv, 3 objects"
		if grep -q "^pack [0-9]: covenant_vehicles " "$out/packs/dump.txt" && grep -q "^pack [0-9]: dropships " "$out/packs/dump.txt" &&
			grep -q "^pack [0-9]: covenant_ai " "$out/packs/dump.txt"; then
			echo "  ok: packs: the map variant names its three packs"
		else
			fail "packs: the map variant's packs: $(cat "$out/packs/dump.txt")"
		fi
		expect packs-load "Covenant AI off in the lobby: the variant brings it" \
			"forge: packs: loaded: Covenant vehicles, Dropships, Covenant AI; left out: none"
		expect packs-load "the variant's three objects placed" "forge: map variant \"Pack Test\" applied: 3 placed, 0 skipped"
		continue
	fi
	if [ "$c" = aiteam ]; then
		checked aiteam-player
		checked aiteam-covenant
		expect aiteam-player "an Elite of the player's team (the free for all player's own)" \
			"forge: elite major plasma rifle: AI team player \(engine team 0\), AI on"
		expect aiteam-player "the Spartan beside it" "forge: player mode at 60.00 -100.00"
		if grep -a "forge: the player's unit at" "$out/aiteam-player/run.log" | grep -qv "body 1.00, shield 1.00"; then
			fail "aiteam-player: the player was hurt by an Elite of his own team"
		else
			echo "  ok: aiteam-player: never hurt ($(grep -ac "forge: the player's unit at" "$out/aiteam-player/run.log") looks, 8 s)"
		fi
		expect aiteam-player "the Elite alive, its brain on" "forge: ai 0: Elite Major Plasma Rifle \(id 1\) alive .* AI team player, engine team 0, AI on, actor yes"
		expect aiteam-covenant "an Elite of the Covenant's" "forge: elite major plasma rifle: AI team covenant \(engine team 3\), AI on"
		expect aiteam-covenant "it shoots: the player's shield goes down" "forge: the player's unit at .* shield 0\.[0-8]"
		expect aiteam-covenant "and then his body" "forge: the player's unit at .* body 0\.[0-9]"
		continue
	fi
	if [ "$c" = aisave ]; then
		checked aisave
		checked aisave-load
		expect aisave "saved" "forge: map variant \"AI Test\" saved as bloodgulch/ai_test.hmv, 3 objects"
		if grep -q "^version 2 " "$out/aisave/dump.txt" &&
			grep -q "^record 0: characters.elite.elite major.elite major plasma rifle kind 0 team -1 ai team 4 respawn 30 flags 3 " "$out/aisave/dump.txt" &&
			grep -q "^record 1: characters.elite.elite minor.elite minor plasma rifle kind 0 team -1 ai team 1 respawn 30 flags 1 " "$out/aisave/dump.txt" &&
			grep -q "^record 2: vehicles.warthog.warthog kind 0 team -1 ai team 0 respawn 30 flags 1 " "$out/aisave/dump.txt"; then
			echo "  ok: aisave: written as version 2 with the AI teams (Flood, the player's) and the Flood Elite's AI off"
		else
			fail "aisave: the file's AI settings: $(cat "$out/aisave/dump.txt")"
		fi
		expect aisave-load "the variant started from the panel's route" "forge: map variant \"AI Test\" applied: 3 placed, 0 skipped"
		expect aisave-load "the Flood Elite as saved: the Flood's, its AI off" \
			"forge: ai 0: Elite Major Plasma Rifle .* alive .* AI team flood, engine team 4, AI off, actor yes"
		expect aisave-load "the other of the player's team, its AI on" \
			"forge: ai 1: Elite Minor Plasma Rifle .* alive .* AI team player, engine team 0, AI on, actor yes"
		continue
	fi
	if [ "$c" = allai ]; then
		checked allai || continue
		log="$out/allai/run.log"
		expect allai "four Covenant placed" "forge: spawned Hunter \(hunter, characters\).*4/32 AI"
		n=$(awk '/forge: all AI off/ { exit } /shots by characters so far/ { v = $(NF - 5) } END { print v + 0 }' "$log")
		[ "$n" -ge 1 ] && echo "  ok: allai: they fought first ($n shots)" || fail "allai: no shot before ALL AI"
		expect allai "ALL AI turned the four off" "forge: all AI off \(4 characters\)"
		# (the 10 s after ALL AI, from a second after it: each character's
		# place within 15 cm, its aim the same, its actor braindead (mode 0),
		# no shot fired)
		awk '/test command at tick 520:/ { on = 1 } /test command at tick 830:/ { exit } on' "$log" > "$out/allai/off.log"
		if awk '/forge: ai [0-3]: / { split($0, a, " at "); split(a[2], p, " "); i = $4;
				aim = $0; sub(/.*aim /, "", aim); sub(/, facing.*/, "", aim);
				if (!(i in x0)) { x0[i] = p[1]; y0[i] = p[2]; a0[i] = aim }
				d = (p[1] - x0[i]) ^ 2 + (p[2] - y0[i]) ^ 2; if (d > dm) dm = d
				if (aim != a0[i]) moved++; if ($0 !~ /AI off/ || $0 !~ /mode 0 action 0/) awake++; n++ }
			END { printf "%d readings, %.3f wu at most, %d aims changed, %d awake\n", n, sqrt(dm), moved, awake;
				exit !(n >= 40 && dm < 0.0225 && !moved && !awake) }' "$out/allai/off.log" > "$out/allai/off.txt"; then
			echo "  ok: allai: frozen for 10 s: $(cat "$out/allai/off.txt")"
		else
			fail "allai: not frozen: $(cat "$out/allai/off.txt")"
		fi
		shots=$(grep -a "shots by characters so far" "$out/allai/off.log" | awk '{ print $(NF - 5) }' | sort -u | tr '\n' ' ')
		[ "$(echo $shots | wc -w)" = 1 ] && echo "  ok: allai: no shot fired while off (the count stays $shots)" ||
			fail "allai: shots fired while off ($shots)"
		grep -aq "ALL AI off" "$out/allai/off.log" && echo "  ok: allai: the row says OFF" || fail "allai: the row not OFF"
		# (the Elite turned by the editor keeps its new facing; the Elite
		# placed then has its AI off)
		expect allai "the editor turned the Elite" "forge: turned elite by 90 degrees"
		awk '/test command at tick 880:/ { on = 1 } /test command at tick 1010:/ { exit } on' "$log" > "$out/allai/turned.log"
		before=$(grep -a "forge: ai 0: " "$out/allai/off.log" | tail -1 | sed 's/.*facing \([^ ]* [^ ]*\) .*/\1/')
		facings=$(grep -a "forge: ai 0: " "$out/allai/turned.log" | sed 's/.*facing \([^ ]* [^ ]*\) .*/\1/' | sort -u)
		if [ "$(echo "$facings" | wc -l)" = 1 ] && [ -n "$facings" ] && [ "$facings" != "$before" ]; then
			echo "  ok: allai: the turned Elite keeps its facing ($before, turned $facings, 4 s)"
		else
			fail "allai: the turned Elite's facing: before $before, after: $(echo $facings)"
		fi
		expect allai "the Elite placed then has its AI off (ALL AI's setting)" \
			"forge: ai 4: Elite Minor Plasma Rifle \(id 5\) alive .* AI off, actor yes"
		if grep -a "forge: ai [0-4]: " "$out/allai/turned.log" | grep -q "AI on"; then
			fail "allai: a character's AI on before ALL AI turned them on"
		fi
		expect allai "one turned on by hand: MIXED" "ALL AI mixed"
		expect allai "ALL AI from MIXED: all off" "forge: all AI off \(5 characters\)"
		expect allai "ALL AI again: all on" "forge: all AI on \(5 characters\)"
		sed -n '/forge: all AI on (5 characters)/,$p' "$log" > "$out/allai/on.log"
		later=$(grep -a "shots by characters so far" "$out/allai/on.log" | tail -1 | awk '{ print $(NF - 5) }')
		first=$(echo $shots | awk '{ print $1 }')
		[ -n "$later" ] && [ "$later" -gt "${first:-0}" ] && echo "  ok: allai: they fight again ($first shots, then $later)" ||
			fail "allai: no shot after ALL AI on ($first, $later)"
		grep -a "forge: the player's unit at" "$out/allai/on.log" | grep -q "shield 0\.[0-8]" &&
			echo "  ok: allai: the player's shield goes down again" || fail "allai: the player not shot after ALL AI on"
		continue
	fi
	if [ "$c" = allaimenu ]; then
		checked allaimenu || continue
		expect allaimenu "the row MIXED" "ALL AI mixed"
		expect allaimenu "the Forge menu opened by Start" "ui: screen pc.h3.forge.pause.screen"
		expect allaimenu "A on ALL AI: MIXED turned all off" "forge: all AI off \(2 characters\)"
		expect allaimenu "A again: all on" "forge: all AI on \(2 characters\)"
		[ -f "$out/allaimenu/shots/02-menu-on.bmp" ] && echo "  ok: allaimenu: the three screenshots" ||
			fail "allaimenu: no screenshots"
		continue
	fi
	if [ "$c" = flood ]; then
		checked flood || continue
		log="$out/flood/run.log"
		expect flood "the pack built from The Library" "scene packs: flood_ai: built from c20"
		expect flood "loaded" "forge: packs: loaded: Flood AI; left out: none"
		expect flood "the infection form a swarm of its own" "forge: flood_infection given a swarm of its own \([0-9a-f]{8}\)"
		expect flood "the carrier and the combat forms free actors" "forge: floodcarrier given a free actor \([0-9a-f]{8}\)"
		expect flood "the combat forms with their variants' weapons" \
			"forge: ai 0: Flood Elite Shotgun .* alive .* engine team 4, AI on, actor yes.* weapon shotgun"
		expect flood "on the Flood's team" "forge: ai 3: Flood Human Assault Rifle .* engine team 4, AI on, actor yes.* weapon assault rifle"
		expect flood "they attack the Spartan: his shield goes down" "forge: the player's unit at .* shield 0\.[0-8]"
		expect flood "and his body" "forge: the player's unit at .* body 0\.[0-9]"
		expect flood "the carrier burst: its infection forms live (the game's)" \
			"forge: ai: [1-9][0-9]* characters not of the layout alive"
		expect flood "ALL AI off: the game's characters too" \
			"forge: ai: ([1-9][0-9]*) characters not of the layout alive \(\1 with their AI off\); ALL AI off"
		continue
	fi
	if [ "$c" = clearai ]; then
		checked clearai || continue
		log="$out/clearai/run.log"
		expect clearai "the AI budget full: 32 placed" "forge: budget 33/256 objects, 32/32 AI \\(3[12] alive, [01] dead\\)"
		# (the dead: the carrier gone, the infection form and two combat forms
		# killed, and any a fall or the carrier's blast killed: at least four;
		# the numbers that follow are worked out from them)
		line=$(grep -a "forge: budget 33/256 objects, 32/32 AI" "$log" | sed -n 2p)
		alive=$(echo "$line" | sed -n 's/.*(\([0-9]*\) alive, \([0-9]*\) dead).*/\1/p')
		dead=$(echo "$line" | sed -n 's/.*(\([0-9]*\) alive, \([0-9]*\) dead).*/\2/p')
		if [ -n "$alive" ] && [ -n "$dead" ] && [ $((alive + dead)) = 32 ] && [ "$dead" -ge 4 ] &&
			echo "$line" | grep -q "HUD: Objects 33/256, AI $alive/32, $dead back next round$"; then
			echo "  ok: clearai: $dead dead: the HUD counts the $alive alive, the dead apart ($line)"
		else
			fail "clearai: the dead not apart: $line"
			alive=0 dead=0
		fi
		expect clearai "the carrier's infection forms are the game's, not counted" \
			"forge: ai: [1-9][0-9]* characters not of the layout alive"
		expect clearai "a 33rd refused: clear the dead to place more" \
			"forge: AI budget full: 32 characters, $dead of them dead. Clear dead AI to place more."
		expect clearai "CLEAR DEAD AI took the $dead out" \
			"forge: cleared $dead dead AI; budget $((33 - dead))/256 objects, $alive/32 AI \\(0 dead\\)"
		expect clearai "then another placed" \
			"forge: spawned Flood Infection Form .*budget $((34 - dead))/256 objects, $((alive + 1))/32 AI"
		expect clearai "NEW ROUND takes the layout as it is" \
			"forge: new round 1: the layout taken: $((34 - dead)) objects \\($((alive + 1)) characters\\)"
		expect clearai "and places only that again" "forge: new round 1: $((34 - dead)) placed again, 0 skipped"
		expect clearai "after the round" \
			"forge: budget $((34 - dead))/256 objects, $((alive + 1))/32 AI \\($((alive + 1)) alive, 0 dead\\)"
		expect clearai "CLEAR ALL AI emptied it" "forge: cleared $((alive + 1)) placed AI; budget 1/256 objects, 0/32 AI \\(0 dead\\)"
		expect clearai "the HUD after" "forge: budget 1/256 objects, 0/32 AI \\(0 alive, 0 dead\\); HUD: Objects 1/256, AI 0/32$"
		expect clearai "nothing dead left to clear" "forge: No dead AI to clear"
		expect clearai "saved after: one object" "forge: map variant \"Clear Test\" saved as bloodgulch/clear_test.hmv, 1 objects"
		if grep -aq "record 0: vehicles.warthog.warthog" "$out/clearai/dump.txt" && ! grep -aq "record 1:" "$out/clearai/dump.txt"; then
			echo "  ok: clearai: the file has the Warthog alone"
		else
			fail "clearai: the file: $(cat "$out/clearai/dump.txt")"
		fi
		continue
	fi
	if [ "$c" = clearaimenu ]; then
		checked clearaimenu || continue
		expect clearaimenu "one dead of two" "forge: budget 2/256 objects, 2/32 AI \(1 alive, 1 dead\)"
		expect clearaimenu "A on CLEAR DEAD AI" "menus: forge: clear dead AI"
		expect clearaimenu "the dead Elite cleared" "forge: cleared 1 dead AI"
		expect clearaimenu "CLEAR ALL AI asked first" "ui: screen pc.h3.forge.clearai.screen"
		[ "$(grep -ac "forge: cleared 1 placed AI" "$out/clearaimenu/run.log")" = 1 ] &&
			echo "  ok: clearaimenu: B kept them, A cleared the grunt (once)" || fail "clearaimenu: CLEAR ALL AI not once"
		expect clearaimenu "none left" "forge: cleared 1 placed AI; budget 0/256 objects, 0/32 AI \(0 dead\)"
		[ -f "$out/clearaimenu/shots/04-all-cleared.bmp" ] && echo "  ok: clearaimenu: the screenshots" ||
			fail "clearaimenu: no screenshots"
		continue
	fi
	if [ "$c" = rotate ]; then
		checked rotate || continue
		checked rotate-load || continue
		log="$out/rotate/run.log"
		# (NAME N WANT: the Nth "forge where" held facing and up within 0.02 of WANT)
		facing() {
			local got
			got=$(grep -a "forge: held boulder" "$log" | sed -n "$2p" | sed 's/.*held facing \(.*\) up \(.*\) (yaw.*/\1 \2/')
			if [ -n "$got" ] && awk -v g="$got" -v w="$3" 'BEGIN { n = split(g, a, " "); split(w, b, " "); if (n != 6) exit 1;
				for (i = 1; i <= 6; i++) { d = a[i] - b[i]; if (d < -0.02 || d > 0.02) exit 1 } }'; then
				echo "  ok: rotate: $1 (facing and up $got)"
			else
				fail "rotate: $1: facing and up $got, not $3"
			fi
		}
		facing "held facing the camera, upright" 1 "-1 0 0 0 0 1"
		expect rotate "L + D-pad right: snapping 15" "forge: Rotation snap: 15"
		expect rotate "again: 45" "forge: Rotation snap: 45"
		facing "a flick across: yawed 45 to the right" 2 "-0.707 0.707 0 0 0 1"
		facing "a flick up: pitched 45, its top away from the camera" 3 "-0.5 0.707 0.5 0.707 0 0.707"
		facing "a flick across with R: rolled 45, its top to the right" 4 "-0.5 0.146 0.854 0.707 -0.5 0.5"
		expect rotate "R in rotate mode dropped nothing" "forge: held boulder .*rotation snap 45, rotating"
		expect rotate "exact angles: yaw, pitch and roll stepped by 45" \
			"forge: properties: .*yaw 45.0, pitch 45.0, roll 45.0, facing 0.500 0.500 0.707 up 0.146 -0.854 0.500"
		facing "the exact angles held" 5 "0.5 0.5 0.707 0.146 -0.854 0.5"
		facing "the mouse's look: two steps of yaw to the right" 6 "0 -1 0 0 0 1"
		facing "the mouse with R: rolled about the camera's view, its top to the right" 7 "0 -0.707 -0.707 0 -0.707 0.707"
		expect rotate "dropped, saved" "forge: map variant \"Rotate Test\" saved as bloodgulch/rotate_test.hmv, 1 objects"
		# (its placed facing: before the round, after it, and loaded)
		want="0.500 0.500 0.707 up 0.146 -0.854 0.500"
		before=$(grep -a "forge: object 0: Boulder" "$log" | head -1 | sed 's/.*placed at [^ ]* [^ ]* [^ ]* facing //')
		after=$(grep -a "forge: object 0: Boulder" "$log" | tail -1 | sed 's/.*placed at [^ ]* [^ ]* [^ ]* facing //')
		loaded=$(grep -a "forge: object 0: Boulder" "$out/rotate-load/run.log" | tail -1 | sed 's/.*placed at [^ ]* [^ ]* [^ ]* facing //')
		expect rotate "a new round" "forge: new round 1: 1 placed again, 0 skipped"
		for pair in "dropped:$before" "after the new round:$after" "loaded from the map variant:$loaded"; do
			got=${pair#*:}
			if [ -n "$got" ] && awk -v g="$got" -v w="$want" 'BEGIN { gsub("up ", "", g); gsub("up ", "", w); n = split(g, a, " ");
				split(w, b, " "); if (n != 6) exit 1; for (i = 1; i <= 6; i++) { d = a[i] - b[i]; if (d < -0.02 || d > 0.02) exit 1 } }'; then
				echo "  ok: rotate: ${pair%%:*}: facing $got"
			else
				fail "rotate: ${pair%%:*}: facing $got, not $want"
			fi
		done
		continue
	fi
	if [ "$c" = floodcov ]; then
		checked floodcov || continue
		log="$out/floodcov/run.log"
		last=$(grep -a "forge: ai [0-3]: " "$log" | tail -4)
		flood=$(echo "$last" | grep -E "Flood (Elite|Human)" | grep -cE " dead |body 0\.|body -")
		cov=$(echo "$last" | grep -E "Elite Minor|Grunt Major" | grep -cE " dead |body 0\.|shield 0\.[0-8]|body -")
		[ "$flood" -ge 1 ] && [ "$cov" -ge 1 ] && echo "  ok: floodcov: the Flood and the Covenant fought ($flood of the Flood, $cov of the Covenant hurt or dead)" ||
			fail "floodcov: no fight ($flood of the Flood, $cov of the Covenant hurt or dead)"
		count floodcov "shots fired" "shots by characters so far" 1
		continue
	fi
	if [ "$c" = sentinels ]; then
		checked sentinels || continue
		log="$out/sentinels/run.log"
		expect sentinels "the pack built from The Library" "scene packs: sentinel_ai: built from c20"
		expect sentinels "the browser's AI groups the Flood, then the Sentinels" "forge: characters [0-9]+: Sentinel \(characters.sentinel.sentinel\), sentinels"
		expect sentinels "two sentinels on the Sentinels' team" "forge: ai 3: Sentinel \(id 4\) alive .* engine team 5"
		if grep -a "forge: weapons [0-9]*: Sentinel" "$log" > /dev/null; then
			fail "sentinels: the sentinel's beam offered as a weapon"
		fi
		hurt=$(grep -a "forge: ai [01]: Flood" "$log" | tail -2 | grep -cE " dead |body 0\.|body -")
		[ "$hurt" -ge 1 ] && echo "  ok: sentinels: the sentinels attack the Flood ($hurt of 2 hurt or dead)" ||
			fail "sentinels: the Flood unhurt"
		continue
	fi
	if [ "$c" = marines ]; then
		checked marinesally || continue
		checked marines || continue
		expect marinesally "the pack built from Halo" "scene packs: marine_ai: built from a30"
		expect marinesally "the humans the player's allies" "forge: the humans are the player's allies \(team 0\)"
		expect marinesally "two marines on the humans' team" "forge: ai 1: Armored Marine Sniper Rifle .* alive .* engine team 2, AI on, actor yes.* weapon sniper rifle"
		if grep -a "forge: the player's unit at" "$out/marinesally/run.log" | grep -qv "body 1.00, shield 1.00"; then
			fail "marinesally: the player was hurt by the marines"
		else
			echo "  ok: marinesally: the marines never hurt the Spartan ($(grep -ac "forge: the player's unit at" "$out/marinesally/run.log") looks, 18 s)"
		fi
		hurt=$(grep -a "forge: ai [23]: " "$out/marines/run.log" | tail -2 | grep -cE " dead |body 0\.|shield 0\.[0-8]|body -")
		[ "$hurt" -ge 1 ] && echo "  ok: marines: the marines shoot the Covenant ($hurt of 2 hurt or dead)" ||
			fail "marines: the Covenant unhurt"
		continue
	fi
	if [ "$c" = aivariant ]; then
		checked aivariant || continue
		checked aivariant-load || continue
		expect aivariant "saved" "forge: map variant \"AI Packs\" saved as bloodgulch/ai_packs.hmv, 4 objects"
		for pack in covenant_ai flood_ai sentinel_ai marine_ai; do
			grep -q "^pack [0-9]: $pack " "$out/aivariant/dump.txt" && echo "  ok: aivariant: the variant names $pack" ||
				fail "aivariant: the variant does not name $pack: $(cat "$out/aivariant/dump.txt")"
		done
		grep -q "^record 1: characters.floodcombat elite.floodcombat elite shotgun kind 0 team -1 ai team 0 respawn 30 flags 3 " \
			"$out/aivariant/dump.txt" && echo "  ok: aivariant: the Flood elite's AI off in the file" ||
			fail "aivariant: the Flood elite's record: $(grep '^record 1' "$out/aivariant/dump.txt")"
		expect aivariant-load "no pack chosen: the variant brings the four" \
			"forge: packs: loaded: Covenant AI, Flood AI, Sentinel AI, Marine AI; left out: none"
		expect aivariant-load "all four placed" "forge: map variant \"AI Packs\" applied: 4 placed, 0 skipped"
		# (placed as saved; by the time they are listed the others may have
		# killed the frozen Flood elite, or one another)
		expect aivariant-load "the Flood elite as saved, its AI off" "forge: ai 1: Flood Elite Shotgun .* engine team 4, AI off"
		expect aivariant-load "the sentinel" "forge: ai 2: Sentinel Major .* engine team 5, AI on"
		expect aivariant-load "the marine" "forge: ai 3: Marine Assault Rifle .* engine team 2, AI on"
		continue
	fi
	if [ "$c" = aipacks ]; then
		printf '%-78s %s\n' "packs asked for" "loaded / left out (the tag cache held, free after them); AI offered"
		for index in 0 1 2 3 4 5 6; do
			r="aipacks-$index"
			checked "$r" || continue
			l="$out/$r/run.log"
			packs=$(grep -a "forge: packs: loaded:" "$l" | head -1 | sed 's/.*forge: packs: //')
			free=$(grep -a "scene packs: .*: loaded from" "$l" | tail -1 | sed 's/.*held by packs, \([0-9]* KB\) free after them.*/\1 free/')
			ai=$(grep -a "forge: palette characters:" "$l" | tail -1 | sed 's/.*: //')
			printf '%-78s %s (%s); %s AI\n' "$(cat "$out/aipacks-$index.set")" "$packs" "$free" "$ai"
		done
		continue
	fi
	if [ "$c" = smallmap ]; then
		checked smallmap
		expect smallmap "Chill Out: the Warthog (its own) in the browser" "forge: vehicles [0-9]+: Warthog \\(vehicles.warthog.warthog\\)"
		expect smallmap "and the Banshee (a pack's)" "forge: vehicles [0-9]+: Banshee \\(vehicles.banshee.banshee\\)"
		expect smallmap "the Banshee placed" "forge: spawned Banshee \\(banshee, vehicles\\)"
		expect smallmap "the Warthog placed" "forge: spawned Warthog \\(warthog, vehicles\\)"
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
	aimapfallback)
		expect "$c" "b30 lacks a tag of covenant_ai: another map next" "covenant_ai: b30 lacks a tag of it .*another of its maps next"
		expect "$c" "built from b40 instead" "scene packs: covenant_ai: built from b40"
		expect "$c" "Covenant AI loaded in Forge" "forge: packs: covenant_ai loaded" ;;
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
	fly)
		# (the monitor flies where it looks: forward looking up climbs, looking
		# down dives; the four "where" lines' heights)
		heights=$(grep -a "forge: camera at" "$out/$c/run.log" | awk '{print $7}' | tr '\n' ' ')
		set -- $heights
		if [ $# -ge 4 ] && awk -v a="$1" -v b="$2" -v c="$3" -v d="$4" 'BEGIN { exit !(b > a + 0.5 && d < c - 0.5) }'; then
			echo "  ok: $c: looking up climbed ($1 -> $2), looking down dived ($3 -> $4)"
		else
			fail "$c: the monitor did not follow its pitch (heights: $heights)"
		fi
		set -- ;;
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
		if grep -aq "record 0: vehicles.warthog.warthog kind 0 team 0 ai team 0 respawn 45 flags 0 " "$out/edit/dump.txt"; then
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
	aicopy)
		count "$c" "the original and its copy both on the player's team with their AI off" \
			"forge: ai [0-9]+: Elite Major Plasma Rifle .*AI team player, engine team 0, AI off" 2 ;;
	aioff)
		expect "$c" "an Elite of the Covenant's with its AI off" "forge: elite major plasma rifle: AI team covenant \(engine team 3\), AI off"
		# (not hurt while its AI is off: the looks before ALL AI)
		awk '/forge: player mode at/ { on = 1 } /forge: all AI on/ { exit } on' "$log" > "$out/$c/ai-off.log"
		hurt=$(grep -a "forge: the player's unit at" "$out/$c/ai-off.log" | grep -vc "body 1.00, shield 1.00")
		looks=$(grep -ac "forge: the player's unit at" "$out/$c/ai-off.log")
		[ "$looks" -ge 8 ] && [ "$hurt" = 0 ] && echo "  ok: $c: never hurt while its AI is off ($looks looks)" ||
			fail "$c: hurt $hurt times of $looks looks with its AI off"
		# (it stood where it was put: its place the same, to a centimetre or so, before and after)
		first=$(grep -a "forge: ai 0: " "$log" | head -1 | sed 's/.* at \([^ ]*\) \([^ ]*\) .*/\1 \2/')
		later=$(grep -a "forge: ai 0: " "$log" | sed -n 2p | sed 's/.* at \([^ ]*\) \([^ ]*\) .*/\1 \2/')
		if [ -n "$first" ] && [ -n "$later" ] && awk -v a="$first" -v b="$later" 'BEGIN { split(a, p); split(b, q);
			exit !((p[1] - q[1]) ^ 2 + (p[2] - q[2]) ^ 2 < 0.01) }'; then
			echo "  ok: $c: it stood still ($first, 9 s later $later)"
		else
			fail "$c: it moved with its AI off ($first, then $later)"
		fi
		expect "$c" "the Forge menu's ALL AI turned it on" "forge: all AI on \(1 characters\)"
		after=$(sed -n '/forge: all AI on/,$p' "$log" | grep -a "forge: the player's unit at" | grep -c "shield 0\.[0-8]")
		[ "$after" -ge 1 ] && echo "  ok: $c: its AI on again, it fights: the player's shield goes down" ||
			fail "$c: its AI on again, the player was not shot" ;;
	round)
		expect "$c" "the three placed" "forge: objects listed: 3 objects, 1 characters in the layout"
		expect "$c" "the Warthog wrecked (where the shove took it)" "forge: object 0: Warthog \(id 1\) destroyed at"
		expect "$c" "the Elite dead" "forge: object 1: Elite Major Plasma Rifle \(id 2\) dead at"
		expect "$c" "the overshield gone, still the layout's" "forge: object 2: Overshield \(id 3\) gone at"
		expect "$c" "NEW ROUND: the engine resets the map" "map reset: the level started over"
		expect "$c" "the layout taken as it resets" "forge: new round 1: the layout taken: 3 objects \(1 characters\)"
		expect "$c" "and placed again" "forge: new round 1: 3 placed again, 0 skipped"
		expect "$c" "the Warthog back whole" "forge: object 0: Warthog \(id [0-9]+\) alive"
		expect "$c" "the Elite alive again, with its actor" "forge: ai 1: Elite Major Plasma Rifle \(id [0-9]+\) alive .* actor yes"
		expect "$c" "the overshield back" "forge: object 2: Overshield \(id [0-9]+\) alive"
		# (each back at its placed spot: where it is after the round within a
		# world unit of where it was placed before)
		before=$(grep -a "forge: object 0: Warthog" "$log" | head -1 | sed 's/.*placed at \([^ ]*\) \([^ ]*\) .*/\1 \2/')
		after=$(grep -a "forge: object 0: Warthog" "$log" | tail -1 | sed 's/.* alive at \([^ ]*\) \([^ ]*\) .*/\1 \2/')
		wrecked=$(grep -a "forge: object 0: Warthog .* destroyed at" "$log" | head -1 | sed 's/.* destroyed at \([^ ]*\) \([^ ]*\) .*/\1 \2/')
		if awk -v b="$before" -v a="$after" -v w="$wrecked" 'BEGIN { split(b, p); split(a, q); split(w, r);
			d = (p[1] - q[1]) ^ 2 + (p[2] - q[2]) ^ 2; m = (p[1] - r[1]) ^ 2 + (p[2] - r[2]) ^ 2; exit !(d < 1 && m > 4) }'; then
			echo "  ok: $c: the Warthog wrecked at $wrecked, back at its spot $before ($after)"
		else
			fail "$c: the Warthog not back at its spot (placed $before, wrecked $wrecked, after $after)"
		fi
		if sed -n '/forge: New round/,$p' "$log" | grep -aq "forge: the player's unit at .* alive, team 0, body 1.00, shield 1.00"; then
			echo "  ok: $c: the player respawned with the round"
		else
			fail "$c: no player after the round"
		fi
		[ "$(grep -ac "forge: monitor mode at" "$log")" -ge 2 ] && echo "  ok: $c: the monitor again after the round" ||
			fail "$c: not the monitor after the round"
		expect "$c" "where it flew" "forge: camera at 60.00 -100.00 1.60" ;;
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
	for c in $cases variants-save variants-load variants-missing variants-damaged variants-full panel-damaged packs-load \
		aiteam-player aiteam-covenant aisave-load marinesally aivariant-load; do
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
