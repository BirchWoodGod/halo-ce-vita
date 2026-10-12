#!/bin/bash
# Your own legs in first person (display.first_person_legs,
# HALO_FIRST_PERSON_LEGS; the Vita's settings panel: Graphics > Display >
# First-person legs), from OpenCE (port/linux/game/first_person_legs.c): the
# player's own body drawn from the waist down when looking down, set back
# behind the eye as far as keeps the open waist out of the view.
#
# On the Linux harness (gxm-null: the Vita's renderer) with the Vita's
# settings, the view turned down by the test command "@look -80" (and zoomed
# by "@look -80 0"), "@legs" logging the frames the legs went to render_model
# in and how far they were set back, HALO_FIRST_PERSON_LEGS_LOG=1 a line each
# time they start or stop being drawn and why:
#
#   a10-off     the setting off: never drawn, no line at all
#   a10-on      the setting unset (On by default): out of the cryotube (hidden in it: a vehicle), on foot,
#               looking down: drawn, set back further than looking ahead,
#               the mesh read for the waist under 400 KB
#   a10-live    off at the start, then on (@set, as the settings panel sets
#               it), then off: drawn only while on
#   a10-menu    off at the start, set on (@set) before the first frame in
#               first person, as in the menus: drawn (the setting read at
#               startup does not win over the panel's)
#   b30-vehicle on foot, drawn; in the Warthog's seat: not drawn; out: drawn
#   bloodgulch  multiplayer: drawn; zoomed: not drawn ("zoomed"); unzoomed:
#               drawn; then another map (hangemhigh): the waist and mesh
#               forgotten (the log starts again) and drawn there too
#   extinction  a Custom Edition map (when its files are on hand), every
#               model drawn a part's own nodes at a time
#               (HALO_CE_PART_PALETTE_NODES=2, custom_edition_geometry.c):
#               the waist found through the parts' node palettes, drawn
#
# Each run must exit by itself with no assertion or exception.
#
#   run_first_person_legs_test.sh [CASE...]
#   HALO_TEST_VITA          the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA          a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_CE_MAPS       Custom Edition maps (default ../custom-maps-dl beside
#                           the tree; extinction.map)
#   HALO_TEST_CE_RESOURCES  Halo PC's bitmaps.map, sounds.map and loc.map
#                           (default the data's maps folder, else Steam's MCC
#                           halo1/maps/custom_edition)
#   HALO_TEST_CPUS          the processors the runs are held to (taskset; default all)
#   HALO_TEST_OUT           where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
ce_maps=${HALO_TEST_CE_MAPS:-$root/../custom-maps-dl}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_first_person_legs_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-a10-off a10-on a10-live a10-menu b30-vehicle bloodgulch extinction}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
for map in a10 b30 bloodgulch hangemhigh; do
	if [ ! -e "$data/maps/$map.map" ]; then
		echo "no $map.map in $data/maps (HALO_TEST_DATA)"
		exit 2
	fi
done
resources=${HALO_TEST_CE_RESOURCES:-}
if [ -z "$resources" ]; then
	resources="$data/maps"
	[ -e "$resources/bitmaps.map" ] ||
		resources="$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1/maps/custom_edition"
fi
pin=()
[ -n "${HALO_TEST_CPUS:-}" ] && pin=(taskset -c "$HALO_TEST_CPUS")

# run NAME SECONDS INIT COMMANDS [ENV...]: the harness on the map init.txt
# loads (INIT, printf's %b), with the Vita's settings (as triage's run.sh),
# the test commands run; Custom Edition's files linked beside the Xbox maps
# for extinction
run() {
	local name=$1 seconds=$2 init=$3 commands=$4
	shift 4
	local dir="$out/$name" file
	rm -rf "$dir"
	mkdir -p "$dir/data/maps" "$dir/save"
	for file in "$(cd "$data" && pwd)"/maps/*.map; do
		ln -sfn "$file" "$dir/data/maps/"
	done
	if [ "$name" = extinction ]; then
		ln -sfn "$(cd "$ce_maps" && pwd)/extinction.map" "$dir/data/maps/"
		for file in bitmaps sounds loc; do
			ln -sfn "$(cd "$resources" && pwd)/$file.map" "$dir/data/maps/"
		done
	fi
	printf '%b\n' "$init" > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_INTERPOLATION=false HALO_INTERPOLATE_FIRST_PERSON=1 HALO_STATIC_SCENERY=1 HALO_SCENERY_UPDATE_DIVISOR=4 \
		HALO_MIN_OBJECT_PIXELS=8 HALO_MODEL_LOD_SCALE=0.5 HALO_LIGHTING_REFRESH_DIVISOR=3 \
		HALO_SOUND_OBSTRUCTION_TICKS=3 HALO_FIRST_PERSON_LEGS_LOG=1 HALO_TEST_COMMANDS="$commands" "$@" \
		"${pin[@]}" timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
	# (the save folder fills with the game state: only the log is kept)
	rm -rf "$dir/save"
}

# frames NAME TICK: the frames the legs were drawn in, as @legs at that tick
# logged them (the first such @legs of the run; empty: none)
frames() {
	awk -v tick="$2" '
		$0 ~ "test command at tick " tick ": @legs$" { want = 1; next }
		want && /test command: first-person legs drawn in/ { sub(/.*drawn in /, ""); sub(/ frames.*/, ""); print; exit }
	' "$out/$1/run.log"
}

# most NAME TICK: how far the legs were set back at most, by then
most() {
	awk -v tick="$2" '
		$0 ~ "test command at tick " tick ": @legs$" { want = 1; next }
		want && /test command: first-person legs drawn in/ { sub(/.*at most /, ""); sub(/\).*/, ""); print; exit }
	' "$out/$1/run.log"
}

# last NAME TICK: how far the last pose drawn by then was set back
last() {
	awk -v tick="$2" '
		$0 ~ "test command at tick " tick ": @legs$" { want = 1; next }
		want && /test command: first-person legs drawn in/ { sub(/.*\(set back /, ""); sub(/,.*/, ""); print; exit }
	' "$out/$1/run.log"
}

# checked NAME: the run exited by itself, with no assertion or exception
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

# expect NAME TEXT CONDITION: CONDITION (a shell test) passed, or TEXT fails
expect() {
	local name=$1 text=$2
	shift 2
	if "$@"; then
		echo "  ok: $name: $text"
	else
		fail "$name: $text ($out/$name/run.log)"
	fi
}

a10='map_name levels\\a10\\a10'
b30='map_name levels\\b30\\b30'
bloodgulch='game_variant slayer\nmap_name levels\\test\\bloodgulch\\bloodgulch'
extinction='game_variant slayer\nmap_name levels\\test\\extinction\\extinction'
# (a10: the opening skipped, the player taken out of the cryotube, which
# waits for the look-around the scripted player never does)
a10_out='L200:@skip;L250:(unit_exit_vehicle (player0))'
# (b30: the insertion skipped; the Pelican lands and the player is on foot
# from about tick 900; the Covenant erased, the player deathless)
b30_setup='L60:@skip;L200:(ai_erase_all);L205:(set cheat_deathless_player true);L240:(object_create_anew jeep)'

started=()
for c in $cases; do
	case $c in
	a10-off) run "$c" 40 "$a10" "$a10_out;L300:@look -80;L600:@legs" HALO_FIRST_PERSON_LEGS=0 & ;;
	a10-on)
		# (the cryotube's exit takes until about tick 500: the view is turned
		# after it)
		run "$c" 50 "$a10" "$a10_out;L240:@legs;L600:@look -80;L700:@legs;L800:@look 0;L900:@legs" & ;;
	a10-live)
		run "$c" 45 "$a10" "$a10_out;L300:@look -80;L380:@legs;L400:@set HALO_FIRST_PERSON_LEGS 1;L420:@look -80;L600:@legs;L700:@set HALO_FIRST_PERSON_LEGS 0;L720:@legs;L900:@legs" HALO_FIRST_PERSON_LEGS=0 & ;;
	a10-menu)
		run "$c" 40 "$a10" "L2:@set HALO_FIRST_PERSON_LEGS 1;$a10_out;L600:@look -80;L700:@legs" HALO_FIRST_PERSON_LEGS=0 & ;;
	b30-vehicle)
		run "$c" 65 "$b30" "$b30_setup;L1100:@look -80;L1150:@legs;L1250:@legs;L1260:(unit_enter_vehicle (player0) jeep \"w-driver\");L1290:@seat;L1300:@legs;L1450:@legs;L1460:(unit_exit_vehicle (player0));L1560:@seat;L1570:@look -80;L1650:@legs" \
			HALO_FIRST_PERSON_LEGS=1 & ;;
	bloodgulch)
		# (the game's time starts again on the next map: L900 runs there)
		run "$c" 65 "$bloodgulch" "L150:@look -80;L210:@legs;L300:@look -80 0;L360:@legs;L420:@legs;L450:@look -80 -1;L510:@legs;L600:(map_name levels\\test\\hangemhigh\\hangemhigh);L900:@legs" \
			HALO_FIRST_PERSON_LEGS=1 & ;;
	extinction)
		if [ ! -e "$ce_maps/extinction.map" ] || [ ! -e "$resources/bitmaps.map" ] || [ ! -e "$resources/sounds.map" ] ||
			[ ! -e "$resources/loc.map" ]; then
			echo "SKIP extinction: no extinction.map (HALO_TEST_CE_MAPS) or Halo PC's bitmaps, sounds and loc maps (HALO_TEST_CE_RESOURCES)"
			continue
		fi
		run "$c" 50 "$extinction" "L150:@look -80;L270:@legs" HALO_FIRST_PERSON_LEGS=1 HALO_CUSTOM_EDITION=1 \
			HALO_CE_PART_PALETTE_NODES=2 & ;;
	*) echo "unknown case $c"; exit 2 ;;
	esac
	started+=("$c")
done
wait

gt() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a + 0 > b + 0) }'; }
for c in "${started[@]}"; do
	checked "$c" || continue
	log="$out/$c/run.log"
	case $c in
	a10-off)
		expect "$c" "never drawn (@legs: $(frames "$c" 600) frames), no line of the legs'" \
			test "$(frames "$c" 600)" = 0 -a "$(grep -ac "first-person legs: player" "$log")" = 0 ;;
	a10-on)
		f240=$(frames "$c" 240) f700=$(frames "$c" 700) f900=$(frames "$c" 900)
		down=$(last "$c" 700) ahead=$(last "$c" 900)
		bytes=$(grep -a "first-person legs: player 0 drawn" "$log" | head -1 | sed -n 's/.*mesh \([0-9]*\) bytes.*/\1/p')
		expect "$c" "hidden in the cryotube (a vehicle) before the exit ($f240 frames by tick 240)" \
			sh -c "[ '$f240' = 0 ] && grep -aq 'first-person legs: player 0 in a vehicle' '$log'"
		expect "$c" "drawn on foot ($f700 -> $f900 frames by ticks 700, 900)" gt "${f900:-0}" "${f700:-0}"
		expect "$c" "set back further looking down (${down:-?}) than ahead (${ahead:-?}), the least 0.04" \
			sh -c "awk -v a='${down:-0}' -v b='${ahead:-0}' 'BEGIN { exit !(a + 0 > b + 0.02 && b + 0 >= 0.039) }'"
		expect "$c" "the mesh read for the waist: ${bytes:-?} bytes (under 400 KB)" \
			test -n "$bytes" -a "${bytes:-0}" -gt 0 -a "${bytes:-0}" -lt 400000 ;;
	a10-live)
		f380=$(frames "$c" 380) f600=$(frames "$c" 600) f720=$(frames "$c" 720) f900=$(frames "$c" 900)
		expect "$c" "off: not drawn ($f380 frames by tick 380)" test "$f380" = 0
		expect "$c" "set on mid-game: drawn ($f600 frames by tick 600)" gt "${f600:-0}" 0
		expect "$c" "set off again: no more frames ($f720 by tick 720, $f900 by 900)" test -n "$f720" -a "$f720" = "$f900" ;;
	a10-menu)
		f700=$(frames "$c" 700)
		expect "$c" "set on before the first frame in first person: drawn ($f700 frames by tick 700)" gt "${f700:-0}" 0 ;;
	b30-vehicle)
		f1150=$(frames "$c" 1150) f1250=$(frames "$c" 1250) f1300=$(frames "$c" 1300) f1450=$(frames "$c" 1450)
		f1650=$(frames "$c" 1650)
		expect "$c" "seated in the Warthog by the test" grep -aq "seat: player 0 in " "$log"
		expect "$c" "drawn on foot ($f1150 -> $f1250 frames)" gt "${f1250:-0}" "${f1150:-0}"
		expect "$c" "in the Warthog: not drawn ($f1300 -> $f1450 frames)" test -n "$f1300" -a "$f1300" = "$f1450"
		expect "$c" "out again: drawn ($f1450 -> $f1650 frames)" gt "${f1650:-0}" "${f1450:-0}" ;;
	bloodgulch)
		f210=$(frames "$c" 210) f360=$(frames "$c" 360) f420=$(frames "$c" 420) f510=$(frames "$c" 510)
		f2=$(frames "$c" 900)
		expect "$c" "drawn ($f210 frames by tick 210)" gt "${f210:-0}" 0
		expect "$c" "zoomed: not drawn ($f360 -> $f420 frames), logged so" \
			sh -c "[ -n '$f360' ] && [ '$f360' = '$f420' ] && grep -aq 'first-person legs: player 0 zoomed' '$log'"
		expect "$c" "unzoomed: drawn again ($f420 -> $f510 frames)" gt "${f510:-0}" "${f420:-0}"
		expect "$c" "another map: the legs' state forgotten (a second first line), drawn there ($f510 -> ${f2:-?} frames)" \
			sh -c "[ \$(grep -ac 'first-person legs: player 0 drawn .*; 0 frames drawn before' '$log') -ge 2 ] && awk -v a='${f2:-0}' -v b='${f510:-0}' 'BEGIN { exit !(a + 0 > b + 0) }'" ;;
	extinction)
		f270=$(frames "$c" 270) m270=$(most "$c" 270)
		expect "$c" "parts drawn with their own nodes" grep -aq "drawn a part's nodes at a time\|drawn with their own nodes" "$out/$c/run.log" "$out/$c/data/debug.txt"
		expect "$c" "drawn ($f270 frames), set back ${m270:-?} looking down (the waist found through the palettes)" \
			sh -c "awk -v a='${f270:-0}' 'BEGIN { exit !(a + 0 > 0) }' && awk -v a='${m270:-0}' 'BEGIN { exit !(a + 0 > 0.05) }'" ;;
	esac
done

echo "logs: $out"
if [ "$status" = 0 ]; then
	echo "PASS"
fi
test_out_done $status
exit $status
