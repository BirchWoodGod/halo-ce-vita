#!/bin/bash
# The Vita's Gyro aim assist switch (HALO_GYRO_ASSIST; the settings panel's
# Controls > Gyro settings): On, the stick's aim assist acts on the gyro's
# turns too; Off with the gyro on (any mode but off), no aim assist at all
# for that player - no friction or adhesion on the gyro's turns nor on the
# stick's, no autoaim bending its shots (source/game/player_control.c,
# source/game/aim_assist.c); with the gyro off the switch is ignored.
#
# On the Linux harness the Vita's gyro is stood in for by HALO_TEST_GYRO
# (port/linux/src/xinput_sdl.c: the mouse look as the gyro in that mode, the
# switch read live), turned by "@aim gyro", the pad held by "@aim pad", the
# player turned to an enemy by "@aim face" and the aim assist's counts
# logged (and cleared) by "@aim log" (player_control_aim_test_command). On
# the beach of b30 an Elite is put in front of the player by the console's
# Forge monitor, its AI off; then each phase: the right stick nudged (the
# stick's friction and adhesion), the gyro turned a little (its friction and
# adhesion), the trigger held (the autoaim of the shots), each with the
# player turned to the Elite first:
#
#   gyro    HALO_TEST_GYRO=on: the switch On (all of it acts: stick, gyro
#           friction, shots autoaimed), then Off by @set as the panel sets
#           it (none acts, the target still under the reticle, the shots'
#           autoaim left out), then On again (all of it again)
#   nogyro  no gyro, the switch Off: Halo's own aim assist (stick, shots)
#   hold    HALO_TEST_GYRO=hold, the switch Off: no aim assist (the mode on
#           in the settings counts, held or not)
#
# Each run must exit by itself with no assertion or exception.
#
#   run_gyro_assist_test.sh [CASE...]
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (b30.map)
#   HALO_TEST_OUT    where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_gyro_assist_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-gyro nogyro hold}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -r "$data/maps/b30.map" ]; then
	echo "no b30.map in $data/maps (HALO_TEST_DATA)"
	exit 2
fi

# run NAME SECONDS COMMANDS [VAR=value...]: the harness on b30
run() {
	local name=$1 seconds=$2 commands=$3 dir="$out/$1"
	shift 3
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	printf '%b\n' 'map_name levels\\b30\\b30' > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
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

# the Elite, in front of the player, its AI off (from tick 1200, as
# run_forge_test.sh's b30 case: the beach)
setup="L60:@skip;L205:(set cheat_deathless_player true);L1200:forge on;L1205:forge spawn elite minor plasma rifle"
setup="$setup;L1207:forge distance 4;L1208:forge aiteam covenant;L1209:forge brain off;L1210:forge drop;L1215:forge off"

# phase TICK: the stick nudged, the gyro turned, the trigger held, each
# after facing the Elite; the counts logged at TICK+60
phase() {
	local t=$1 c="" step
	c="$c;L$t:@aim log;L$((t + 2)):@aim face;L$((t + 6)):@aim pad 0 0 0.3 0 0 4"
	c="$c;L$((t + 14)):@aim face"
	for step in 18 19 20 21 22 23 24 25; do
		c="$c;L$((t + step)):@aim gyro 0.003 0"
	done
	c="$c;L$((t + 30)):@aim face;L$((t + 34)):@aim pad 0 0 0 0 1 6;L$((t + 60)):@aim log"
	echo "$c"
}

# counts NAME N: the Nth "@aim log" line's counts (1: the first) as
# "off targeted stick gyro_friction gyro_adhesion autoaimed autoaim_skipped"
counts() {
	grep -a "aim assist: player 0 " "$out/$1/run.log" | sed -n "$2p" |
		sed -E 's/.* off ([0-9]+) targeted ([0-9]+) stick ([0-9]+) gyro_friction ([0-9]+) gyro_adhesion ([0-9]+) autoaimed ([0-9]+) autoaim_skipped ([0-9]+).*/\1 \2 \3 \4 \5 \6 \7/'
}

# assisted NAME N GYRO: the Nth log's phase had the aim assist (GYRO 1: on
# the gyro's turns too)
assisted() {
	local values off targeted stick friction adhesion autoaimed skipped
	values=$(counts "$1" "$2")
	read -r off targeted stick friction adhesion autoaimed skipped <<< "$values"
	if [ -z "${skipped:-}" ]; then
		fail "$1: no counts for phase $2 ($out/$1/run.log)"
		return
	fi
	if [ "$off" = 0 ] && [ "$targeted" -gt 0 ] && [ "$stick" -gt 0 ] && [ "$autoaimed" -gt 0 ] && [ "$skipped" = 0 ] &&
		{ [ "$3" = 0 ] || [ "$friction" -gt 0 ]; }; then
		echo "  ok: $1: phase $2 assisted ($values)"
	else
		fail "$1: phase $2 should have the aim assist (gyro $3): off targeted stick gyro_friction gyro_adhesion autoaimed autoaim_skipped = $values"
	fi
}

# unassisted NAME N: the Nth log's phase had none (the target there all the same)
unassisted() {
	local values off targeted stick friction adhesion autoaimed skipped
	values=$(counts "$1" "$2")
	read -r off targeted stick friction adhesion autoaimed skipped <<< "$values"
	if [ -z "${skipped:-}" ]; then
		fail "$1: no counts for phase $2 ($out/$1/run.log)"
		return
	fi
	if [ "$off" = 1 ] && [ "$targeted" -gt 0 ] && [ "$stick" = 0 ] && [ "$friction" = 0 ] && [ "$adhesion" = 0 ] &&
		[ "$autoaimed" = 0 ] && [ "$skipped" -gt 0 ]; then
		echo "  ok: $1: phase $2 unassisted ($values)"
	else
		fail "$1: phase $2 should have no aim assist: off targeted stick gyro_friction gyro_adhesion autoaimed autoaim_skipped = $values"
	fi
}

for name in $cases; do
	case $name in
	gyro)
		run gyro 75 "$setup$(phase 1240);L1310:@set HALO_GYRO_ASSIST 0$(phase 1320);L1390:@set HALO_GYRO_ASSIST 1$(phase 1400)" \
			HALO_TEST_GYRO=on HALO_GYRO_ASSIST=1 & ;;
	nogyro)
		run nogyro 60 "$setup$(phase 1240)" HALO_GYRO_ASSIST=0 & ;;
	hold)
		run hold 60 "$setup$(phase 1240)" HALO_TEST_GYRO=hold HALO_GYRO_ASSIST=0 & ;;
	*)
		echo "unknown case $name"
		status=1 ;;
	esac
done
wait

for name in $cases; do
	case $name in
	gyro)
		echo "gyro: the gyro on, the switch On, Off, On"
		if checked gyro; then
			assisted gyro 2 1
			unassisted gyro 4
			assisted gyro 6 1
		fi ;;
	nogyro)
		echo "nogyro: no gyro, the switch Off (ignored)"
		checked nogyro && assisted nogyro 2 0 ;;
	hold)
		echo "hold: the gyro While holding (not held), the switch Off"
		checked hold && unassisted hold 2 ;;
	esac
done

test_out_done $status
exit $status
