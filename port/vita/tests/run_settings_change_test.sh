#!/bin/bash
# A change in the settings panel never stalls the game thread (BlazeRed,
# 1.1.0-beta.6: the game froze changing the performance preset or turning
# the gyro on or off). Two parts:
#
#   panel   the panel's own work (vita_settings.c, the desktop test of it:
#           run_vita_settings_test.sh's test_save_off_thread): with every
#           settings.txt save taking 300 ms, as the testers' memory cards did
#           (halo.log: a hitch of 104-549 ms after each save on the game
#           thread), a press on Profile or Gyro aiming returns at once, the
#           file is written by its own thread, presses in a row together, a
#           row set as it is not at all
#   game    the game's side, on the Linux harness (the gxm-null build, the
#           Vita's Direct3D device: configure.py --linux-d3d gxm-null; the GL
#           one works too): b30, the profiles' rows set as the panel sets
#           them (Quality, Performance, Balanced: every row at once, the
#           settings generation the readers watch) and the gyro on and off;
#           each frame's game thread work (HALO_FRAME_TIMING=1) at a change
#           and the three after it within 5 ms of the level's median, and no
#           frame-hitch line there
#
#   run_settings_change_test.sh [panel] [game]   (default: both)
#   HALO_TEST_VITA         the Linux build (default build/linux/halo of this tree)
#   HALO_TEST_DATA_MENUS   a folder with the game's maps folder (b30; the game
#                          part is skipped without it)
#   HALO_TEST_OUT          where the logs go (deleted after a pass when the test
#                          made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_settings_change_test.$$}
export HALO_MENUS_FOLDER=${HALO_MENUS_FOLDER:-$root/port/vita/app0/menus/}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-panel game}
status=0
fail() { echo "FAIL: $1"; status=1; }
mkdir -p "$out"

# the profiles' rows (vita_settings.c profile_variables, profile_values)
rows="HALO_RENDER_SCALE HALO_MODEL_LOD_SCALE HALO_MIN_OBJECT_PIXELS HALO_SCENERY_UPDATE_DIVISOR
HALO_LIGHTING_REFRESH_DIVISOR HALO_SOUND_OBSTRUCTION_TICKS HALO_SUN_RAYS HALO_VITA_SHADOWS HALO_MAX_SCENE_LIGHTS
HALO_VITA_EFFECTS_QUALITY HALO_PARTICLE_RENDER_DIVISOR HALO_AI_THINK_DIVISOR HALO_SOUND_MANAGER_DIVISOR
HALO_PARTICLE_BUDGET HALO_DYNAMIC_RES_MIN"
performance="dynamic 0.5 8 4 3 6 0 0 4 1 2 0 2 reduced 0.5"
balanced="dynamic 0.75 8 4 3 3 0 0 4 2 1 0 2 reduced 0.625"
quality="dynamic 1 4 2 2 3 1 1 8 2 1 0 1 reduced 0.75"
# profile TICK NAME VALUES: the commands that set a profile's rows at that tick
profile() {
	local tick=$1 name=$2 values=($3) index=0 row commands=""
	for row in $rows; do
		commands="$commands;L$tick:@set $row ${values[$index]}"
		index=$((index + 1))
	done
	echo "$commands;L$tick:@set HALO_PROFILE $name"
}

for c in $cases; do
	case $c in
	panel)
		mkdir -p "$out/panel"
		TMPDIR="$out/panel" "$here/run_vita_settings_test.sh" > "$out/panel/run.log" 2>&1
		code=$?
		grep -a "the press returns at once\|presses): \|writes for five\|the value it has" "$out/panel/run.log" |
			sort -u | sed 's/^/  /'
		if [ $code -ne 0 ] || grep -aq "^FAIL" "$out/panel/run.log"; then
			fail "panel: the settings panel's test failed ($out/panel/run.log)"
		elif ! grep -aq "^PASS Gyro aiming On: the press returns at once" "$out/panel/run.log"; then
			fail "panel: no off-thread save check in $out/panel/run.log"
		else
			echo "  ok: panel: a change returns at once; settings.txt written off the game thread"
		fi ;;
	game)
		if [ ! -x "$binary" ]; then
			fail "game: no harness at $binary (HALO_TEST_VITA)"
			continue
		fi
		if [ ! -r "$data/maps/b30.map" ]; then
			echo "SKIP: game: no b30.map in $data/maps (HALO_TEST_DATA_MENUS)"
			continue
		fi
		changes="600 750 900 1050 1100 1150 1200"
		commands="L60:@skip$(profile 600 quality "$quality")$(profile 750 performance "$performance")"
		commands="$commands$(profile 900 balanced "$balanced");L1050:@set HALO_GYRO on;L1100:@set HALO_GYRO off"
		commands="$commands;L1150:@set HALO_GYRO on;L1200:@set HALO_GYRO off"
		dir="$out/game"
		mkdir -p "$dir/data" "$dir/save"
		ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
		printf 'map_name levels\\b30\\b30\n' > "$dir/data/init.txt"
		(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
			HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=55 HALO_FULLSCREEN=0 \
			HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
			HALO_INTERPOLATION=false HALO_FRAME_TIMING=1 "HALO_TEST_COMMANDS=$commands" \
			timeout -k 5 175 "$binary" > "$dir/run.log" 2>&1)
		code=$?
		if [ $code -ne 0 ]; then
			fail "game: the harness exited with $code ($dir/run.log)"
			continue
		fi
		grep -aq "gxm-null:" "$dir/run.log" && echo "  (the gxm-null build: the Vita's Direct3D device)"
		count=$(grep -ac "test command at tick .*@set" "$dir/run.log")
		[ "$count" -eq 52 ] || fail "game: $count settings set, not 52 ($dir/run.log)"
		# (each frame's game thread work: the ticks' share, the render and
		# the rest, not the frame limit's wait)
		sed -n 's/.*frame-timing: game tick \([0-9]*\) frames 1 .*| ticks \([0-9.]*\) ms\/frame.*| render \([0-9.]*\) | throttle+present [0-9.]* | other \([0-9.]*\).*/\1 \2 \3 \4/p' \
			"$dir/run.log" | awk '{ printf "%d %.2f\n", $1, $2 + $3 + $4 }' > "$dir/work.txt"
		median=$(awk '$1 >= 300 && $1 < 600 { print $2 }' "$dir/work.txt" | sort -n | awk '{ a[NR] = $1 } END { print NR ? a[int((NR + 1) / 2)] : "" }')
		if [ -z "$median" ]; then
			fail "game: no frame-timing lines ($dir/run.log)"
			continue
		fi
		for tick in $changes; do
			worst=$(awk -v t="$tick" '$1 >= t && $1 <= t + 3 { if ($2 > m) m = $2 } END { printf "%.2f", m }' "$dir/work.txt")
			if awk -v w="$worst" -v m="$median" 'BEGIN { exit !(w <= m + 5.0) }'; then
				echo "  ok: game: tick $tick: the change's frames' work at most $worst ms (the level's median $median)"
			else
				fail "game: tick $tick: a frame's work $worst ms at the change (the level's median $median)"
			fi
		done
		# (no frame-hitch line from a change's command to 5 ticks after it)
		hitches=$(awk '/test command at tick [0-9]+: @set/ { split($0, a, "tick "); t = a[2] + 0; watching = 1 }
			watching && /frame-hitch:/ { print }
			watching && /frame-timing: game tick/ { split($0, b, "game tick "); if (b[2] + 0 > t + 5) watching = 0 }' \
			"$dir/run.log")
		if [ -n "$hitches" ]; then
			fail "game: a frame-hitch at a change: $hitches"
		else
			echo "  ok: game: no frame-hitch at a change"
		fi ;;
	*)
		fail "no case $c" ;;
	esac
done
[ $status = 0 ] && echo PASS
test_out_done $status
exit $status
