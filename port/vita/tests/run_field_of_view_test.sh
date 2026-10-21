#!/bin/bash
# The player's Field of view (display.field_of_view, HALO_FIELD_OF_VIEW; the
# Vita's settings panel: Video > Screen > Field of view;
# port/linux/game/field_of_view.c), on the Linux harness: gxm-null (the
# Vita's renderer) for the numbers, the GL build for the pictures too
# (HALO_SCREENSHOT_DIR: shots/NAME.bmp, kept with HALO_TEST_KEEP=1; the Vita's
# display shape, HALO_GL_VITA_DISPLAY=1).
# "@fov" logs each player window's fields of view and where the unit's aim
# and its aim assist's target land against the view's centre (the
# crosshair); the setting changed as the panel changes it ("@set").
#
#   wide     16:9 (848 columns), Blood Gulch, the same spot and facing: 70
#            is the stock view (vertical 48.11, 76.52 across, 61.52 at
#            4:3), 85 and 100 wider (75.83 and 90.74 at 4:3), the
#            first-person weapon at the stock 61.52 throughout; the sniper
#            rifle zoomed (2x, then 8x) the same view at 70 and 100; back
#            unzoomed at 100: wide again
#   narrow   the same at 4:3 (640 columns): 61.52, 75.83, 90.74 across
#   aim      b30, an Elite in front (AI off), faced (@aim face) at 70 and at
#            100: the aim at the centre, the Elite the aim assist's target,
#            at the centre too, at both
#
# Each run must exit by itself with no assertion or exception.
#
#   run_field_of_view_test.sh [CASE...]
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_OUT    where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_field_of_view_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-wide narrow aim}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
for map in bloodgulch b30; do
	if [ ! -e "$data/maps/$map.map" ]; then
		echo "no $map.map in $data/maps (HALO_TEST_DATA)"
		exit 2
	fi
done

# run NAME SECONDS INIT COMMANDS [ENV...]
run() {
	local name=$1 seconds=$2 init=$3 commands=$4
	shift 4
	local dir="$out/$name"
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save" "$dir/shots"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	printf '%b\n' "$init" > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_INTERPOLATION=false HALO_GL_VITA_DISPLAY=1 HALO_FIELD_OF_VIEW=70 HALO_SCREENSHOT_DIR="$dir/shots" \
		HALO_TEST_COMMANDS="$commands" "$@" \
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

# fov NAME N FIELD: the Nth @fov line's field: vertical,
# horizontal, at43, weapon, aimx, aimy, target ("at", "none", "off"), tx, ty
fov() {
	grep -a "test command: field of view: window 0 " "$out/$1/run.log" | sed -n "$2p" | sed -E \
		's/.*setting [0-9]+: vertical ([0-9.]+), horizontal ([0-9.]+) .*at 4:3 ([0-9.]+); first-person weapon at 4:3 ([0-9.]+) .*aim [a-z-]+ (-?[0-9.]+) (-?[0-9.]+) from the centre; target ([a-z-]+) (-?[0-9.]+) (-?[0-9.]+)$/\1 \2 \3 \4 \5 \6 \7 \8 \9/' |
		awk -v field="$3" '{
			n = split("vertical horizontal at43 weapon aimx aimy target tx ty", names, " ")
			for (i = 1; i <= n; i++) if (names[i] == field) print $i
		}'
}

# near A B [TOLERANCE]: A within TOLERANCE (0.05) of B
near() { awk -v a="$1" -v b="$2" -v t="${3:-0.05}" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(a != "" && d <= t) }'; }

expect() {
	local name=$1 text=$2
	shift 2
	if "$@"; then
		echo "  ok: $name: $text"
	else
		fail "$name: $text ($out/$name/run.log)"
	fi
}

bloodgulch='game_variant slayer\nmap_name levels\\test\\bloodgulch\\bloodgulch'
# (the same spot and facing each run: between the bases, looking at the
# red base's Warthog; then the sniper rifle, zoomed 2x and 8x)
spot='L150:@pos 0 50 -100 1;L155:@look -2 -1 155.8'
views="$spot;L200:@fov;L201:@shot 70;L205:@set HALO_FIELD_OF_VIEW 85;L230:@fov;L231:@shot 85"
views="$views;L235:@set HALO_FIELD_OF_VIEW 100;L260:@fov;L261:@shot 100;L265:@weapon 0 sniper;L290:@look -2 0 155.8"
views="$views;L320:@fov;L321:@shot zoom-100;L325:@set HALO_FIELD_OF_VIEW 70;L350:@fov;L351:@shot zoom-70"
views="$views;L355:@look -2 1 155.8;L380:@fov;L385:@set HALO_FIELD_OF_VIEW 100;L410:@fov;L415:@look -2 -1 155.8"
views="$views;L450:@fov;L451:@shot sniper-100"
# (b30: the Elite in front, its AI off, as run_gyro_assist_test.sh)
b30='map_name levels\\b30\\b30'
elite="L60:@skip;L205:(set cheat_deathless_player true);L1200:forge on;L1205:forge spawn elite minor plasma rifle"
elite="$elite;L1207:forge distance 4;L1208:forge aiteam covenant;L1209:forge brain off;L1210:forge drop;L1215:forge off"
elite="$elite;L1240:@aim face;L1260:@fov;L1261:@shot aim-70;L1265:@set HALO_FIELD_OF_VIEW 100;L1270:@aim face"
elite="$elite;L1290:@fov;L1291:@shot aim-100"

started=()
for c in $cases; do
	case $c in
	wide) run "$c" 25 "$bloodgulch" "$views" HALO_DISPLAY_WIDTH=848 & ;;
	narrow) run "$c" 25 "$bloodgulch" "$views" HALO_DISPLAY_WIDTH=640 & ;;
	aim) run "$c" 55 "$b30" "$elite" HALO_DISPLAY_WIDTH=848 & ;;
	*) echo "unknown case $c"; exit 2 ;;
	esac
	started+=("$c")
done
wait

for c in "${started[@]}"; do
	checked "$c" || continue
	case $c in
	wide|narrow)
		if [ $c = wide ]; then width=76.52; else width=61.52; fi
		expect "$c" "70: the stock view (vertical $(fov $c 1 vertical), across $(fov $c 1 horizontal))" \
			sh -c "$(declare -f near); near '$(fov $c 1 vertical)' 48.11 && near '$(fov $c 1 horizontal)' $width && near '$(fov $c 1 at43)' 61.52"
		expect "$c" "85: $(fov $c 2 at43) at 4:3, 100: $(fov $c 3 at43)" \
			sh -c "$(declare -f near); near '$(fov $c 2 at43)' 75.83 && near '$(fov $c 3 at43)' 90.74"
		expect "$c" "the first-person weapon at the stock 61.52 (70: $(fov $c 1 weapon), 85: $(fov $c 2 weapon), 100: $(fov $c 3 weapon))" \
			sh -c "$(declare -f near); near '$(fov $c 1 weapon)' 61.52 && near '$(fov $c 2 weapon)' 61.52 && near '$(fov $c 3 weapon)' 61.52"
		expect "$c" "zoomed 2x: the same view at 100 and 70 ($(fov $c 4 vertical), $(fov $c 5 vertical); stock 22.73)" \
			sh -c "$(declare -f near); near '$(fov $c 4 vertical)' 22.73 0.01 && near '$(fov $c 5 vertical)' 22.73 0.01"
		expect "$c" "zoomed 8x: the same view at 70 and 100 ($(fov $c 6 vertical), $(fov $c 7 vertical))" \
			sh -c "$(declare -f near); near '$(fov $c 6 vertical)' '$(fov $c 7 vertical)' 0.01 && near '$(fov $c 6 vertical)' 5.58"
		expect "$c" "unzoomed again at 100: wide again ($(fov $c 8 at43))" \
			sh -c "$(declare -f near); near '$(fov $c 8 at43)' 90.74"
		bad=0
		for n in 1 2 3 4 5 6 7 8; do
			near "$(fov $c $n aimx)" 0 0.5 && near "$(fov $c $n aimy)" 0 0.5 || bad=1
		done
		expect "$c" "the aim at the view's centre (the crosshair) at every field of view and zoom" test $bad = 0 ;;
	aim)
		expect "$c" "70: the Elite the target, at $(fov $c 1 tx) $(fov $c 1 ty) from the centre; the aim at the centre" \
			sh -c "$(declare -f near); [ '$(fov $c 1 target)' = at ] && near '$(fov $c 1 tx)' 0 30 && near '$(fov $c 1 ty)' 0 40 && near '$(fov $c 1 aimx)' 0 0.5 && near '$(fov $c 1 aimy)' 0 0.5"
		expect "$c" "100: the Elite still the target, at $(fov $c 2 tx) $(fov $c 2 ty) from the centre; the aim at the centre" \
			sh -c "$(declare -f near); [ '$(fov $c 2 target)' = at ] && near '$(fov $c 2 tx)' 0 30 && near '$(fov $c 2 ty)' 0 40 && near '$(fov $c 2 aimx)' 0 0.5 && near '$(fov $c 2 aimy)' 0 0.5 && near '$(fov $c 2 at43)' 90.74" ;;
	esac
	shots=$(ls "$out/$c/shots" 2>/dev/null | tr '\n' ' ')
	[ -n "$shots" ] && echo "  $c: screenshots in $out/$c/shots: $shots"
done

test_out_done $status
exit $status
