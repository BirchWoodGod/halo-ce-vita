#!/bin/bash
# The render and the tick's vehicle seats, nav points and motion sensor
# (fix/render-seat-races). With the tick on a thread of its own
# (HALO_TICK_THREAD, the Vita's) the render draws while the tick takes the
# player out of a vehicle. The HUD (the weapon and unit interfaces, the
# weapon HUD index, the multitexture overlays), the particles' first person
# test, the motion sensor's vehicle blips and unit_get_camera_position
# checked the unit's parent and seat (or the vehicle's driver and gunner),
# then read them again and looked up unit NONE once the tick had cleared
# them; the nav point arrows did the same with a nav point a script
# deactivated. Each now reads the link once and checks it.
#
# b30 on the Linux harness (gxm-null: the Vita's renderer) with the Vita's
# settings: the insertion skipped, the beach's Covenant erased, the player
# deathless, the motion sensor showing every unit (so the parked Warthog's
# blip asks who drives it), and the Warthog "jeep" created. A test command
# (on the game thread, between ticks) seats player0 in it every 100 ticks,
# and a script woken by the tick (a sleep_until's condition runs in the
# tick's hs_update) takes them out every 7, so every exit is the tick's:
#
#   driver     the driver's seat (no weapon: the seat paths of the HUD)
#   gunner     the gunner's seat (the vehicle's weapon on the HUD)
#   passenger  a passenger's seat (the player's own weapon)
#   nav        on foot, a nav point on the Warthog given every 15 ticks by a
#              test command and taken away every 7 by a script on the tick
#
# Each case must exit by itself with no crash, assertion or exception; the
# seat cases must log ("@seat") the player in the seat after an enter and
# on foot before the next, five times at least each (the tick's exits did
# happen). The window between a check and its second read is a few
# instructions, so a pass here says the paths run, not that a race was met:
# the harness inherits the environment, and a build with a delay where a
# read used to be repeated (an environment variable's sleep, kept out of
# the tree) crashed every seat path before the fix and none after.
#
#   run_seat_race_test.sh [CASE...]   (default: driver gunner passenger nav)
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_SECONDS  seconds each case plays (default 90)
#   HALO_TEST_OUT    where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_seat_race_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
seconds=${HALO_TEST_SECONDS:-90}
cases=${*:-driver gunner passenger nav}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -e "$data/maps/b30.map" ]; then
	echo "no b30.map in $data/maps (HALO_TEST_DATA)"
	exit 2
fi

# run NAME COMMANDS: the harness on b30 with the Vita's settings (as triage's run.sh), the test commands run
run() {
	local name=$1 commands=$2
	local dir="$out/$name"
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	printf 'map_name levels\\b30\\b30\n' > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_INTERPOLATION=false HALO_INTERPOLATE_FIRST_PERSON=1 HALO_STATIC_SCENERY=1 HALO_SCENERY_UPDATE_DIVISOR=4 \
		HALO_MIN_OBJECT_PIXELS=8 HALO_MODEL_LOD_SCALE=0.5 HALO_LIGHTING_REFRESH_DIVISOR=3 HALO_SOUND_OBSTRUCTION_TICKS=3 \
		HALO_TEST_COMMANDS="$commands" timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
	# (the save folder fills with the game state: only the log is kept)
	rm -rf "$dir/save"
}

# (the setup; a sleeping console script loses its string constants by the
# time it wakes, so the scripts on the tick name no seat, only test commands do)
setup='L60:@skip;L200:(ai_erase_all);L205:(set cheat_deathless_player true);L210:(set debug_motion_sensor_draw_all_units true);L240:(object_create_anew jeep)'
# (the last enter 5 s before the end; 160 test commands at most: 50 cycles)
last=$((seconds * 30 - 150))
[ "$last" -gt 5200 ] && last=5200

# commands CASE: the case's test commands
commands() {
	local c=$setup t
	case $1 in
	nav)
		c="$c;L275:(sleep_until (begin (deactivate_nav_point_object (player0) jeep) false) 7)"
		for t in $(seq 270 15 $((last < 2400 ? last : 2400))); do
			c="$c;L$t:(activate_nav_point_object default_red (player0) jeep 0.5)"
		done ;;
	*)
		c="$c;L275:(sleep_until (begin (unit_exit_vehicle (player0)) false) 7)"
		for t in $(seq 300 100 $last); do
			c="$c;L$((t - 2)):@seat;L$t:(unit_enter_vehicle (player0) jeep \"w-$1\");L$((t + 3)):@seat"
		done ;;
	esac
	echo "$c"
}

for c in $cases; do
	case $c in
	driver|gunner|passenger|nav) run "$c" "$(commands $c)" & ;;
	*) echo "unknown case $c"; exit 2 ;;
	esac
done
wait

for c in $cases; do
	log="$out/$c/run.log"
	code=$(cat "$out/$c/exit" 2>/dev/null)
	if [ "$code" != 0 ]; then
		fail "$c: the harness exited with $code ($(grep -a -m1 'segmentation fault' "$log"); $log)"
		continue
	fi
	if grep -av "render assertion skipped" "$log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
		fail "$c: an assertion or exception ($log)"
		continue
	fi
	if ! grep -aq "test command at tick 240: (object_create_anew jeep)" "$log"; then
		fail "$c: the Warthog was not made ($log)"
		continue
	fi
	if [ "$c" = nav ]; then
		given=$(grep -ac "test command at tick [0-9]*: (activate_nav_point_object" "$log")
		[ "$given" -ge 20 ] && echo "  ok: nav: $given nav points given and taken away on the tick, no crash" ||
			fail "nav: only $given nav points given ($log)"
		continue
	fi
	seated=$(grep -ac "seat: player 0 in " "$log")
	afoot=$(grep -ac "seat: player 0 on foot" "$log")
	if [ "$seated" -ge 5 ] && [ "$afoot" -ge 5 ]; then
		echo "  ok: $c: in the seat $seated times after an enter, on foot $afoot times before the next (the tick's exits), no crash"
	else
		fail "$c: in the seat $seated times, on foot $afoot times: the player did not get in and out ($log)"
	fi
done

echo "logs: $out"
if [ "$status" = 0 ]; then
	echo "PASS"
fi
test_out_done $status
exit $status
