#!/bin/bash
# Split screen on the Linux harness (any build of this tree; the gxm-null one
# runs the Vita's renderer): network_test's local game on Blood Gulch at the
# Vita's 848x480 with test controllers (debug.test_controllers), the
# scripted player playing each, the screens named in the log (HALO_UI_LOG):
#
#   two    two players: the screen splits in two; 25 s in, controller 2 is
#          unplugged (a PS TV's DualShock switched off) and its half says to
#          reconnect it, then plugged in again; its START then opens its own
#          pause menu (2p_pause_game) and B closes it
#   three  three players: the screen splits in three
#   four   four players: the screen splits in four; controller 4 unplugged
#          and plugged in again, controller 3's START and B
#
# Each must log no assertion, exception or halt, and exit by itself. The
# frame timing (HALO_FRAME_TIMING) of the game's last 30 s is printed: on
# the harness only the game thread's own work (the gxm-null renderer draws
# nothing), a measure of the CPU side of each extra window.
#
#   run_split_screen_test.sh [CASE...]   (default: two three four)
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_OUT    where the logs go (kept)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_split_screen_test.$$}
cases=${*:-two three four}
status=0
fail() { echo "FAIL ($1): $2"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -e "$data/maps/bloodgulch.map" ]; then
	echo "no bloodgulch.map in $data/maps (HALO_TEST_DATA)"
	exit 2
fi
mkdir -p "$out"

run() { # NAME CONTROLLERS SECONDS PAD [VAR=value...]
	local name=$1 controllers=$2 seconds=$3 pad=$4
	shift 4
	rm -rf "$out/$name"
	mkdir -p "$out/$name/data" "$out/$name/save"
	ln -sfn "$(cd "$data" && pwd)/maps" "$out/$name/data/maps"
	(cd "$out/$name" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" \
		HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_DISPLAY_WIDTH=848 HALO_UI_LOG=1 HALO_FRAME_TIMING=300 HALO_NETWORK_TEST=local:bloodgulch \
		HALO_TEST_CONTROLLERS="$controllers" HALO_TEST_INPUT=bot:7 "HALO_TEST_PAD=$pad" "$@" \
		timeout -k 5 $((seconds + 90)) "$binary" > "$out/$name/run.log" 2>&1)
	echo $? > "$out/$name/exit"
}

check() { # NAME PLAYERS
	local name=$1 players=$2 log=$out/$1/run.log
	echo "--- $name"
	grep -aE "network test: .*players on this machine|split screen:|test pad: [0-9]|ui: screen ui.shell.(error|multiplayer_game)" "$log" | head -12
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name/exit")"
	grep -aqiE "assert|exception|halt" "$log" "$out/$name/data/debug.txt" 2>/dev/null &&
		fail "$name" "an assertion, exception or halt"
	grep -aq "network test: $players players on this machine" "$log" || fail "$name" "not $players players"
	grep -aq "split screen: $players windows" "$log" || fail "$name" "the screen did not split in $players"
	# (the frame timing's last lines: the game under way)
	grep -a "frame-timing:" "$log" | tail -3 | sed -E 's/.*\| frame ([0-9.]+) ms \(max ([0-9.]+)\) \| ticks [0-9.]+ ms\/frame ([0-9.]+) ms\/tick \| render ([0-9.]+).*/frame-timing: frame \1 ms (max \2), tick \3 ms, render \4 ms/'
}

check_unplug() { # NAME CONTROLLER
	local name=$1 controller=$2 log=$out/$1/run.log
	awk "/test pad: $controller.unplug/{f=1} f" "$log" | grep -aq "ui: screen ui.shell.error.error_nonmodal_halfscreen" ||
		fail "$name" "controller $controller's half did not say to reconnect it"
	awk "/test pad: $controller.plug/{f=1} f" "$log" | grep -aq "ui: screen ui.shell.multiplayer_game.pause_game" ||
		fail "$name" "a START after plugging in did not open the pause menu"
}

for case in $cases; do
	case $case in
	two)
		run two 2 70 "wait:150:25000 2.unplug:150:8000 2.plug:150:5000 2.start:150:4000 2.b:150:2000"
		check two 2
		check_unplug two 2 ;;
	three)
		run three 3 60 "wait:150:2000"
		check three 3 ;;
	four)
		run four 4 70 "wait:150:25000 4.unplug:150:8000 4.plug:150:5000 3.start:150:4000 3.b:150:2000"
		check four 4
		awk '/test pad: 4.unplug/{f=1} f' "$out/four/run.log" | grep -aq "ui: screen ui.shell.error" ||
			fail four "controller 4's window did not say to reconnect it"
		awk '/test pad: 3.start/{f=1} f' "$out/four/run.log" | grep -aq "ui: screen ui.shell.multiplayer_game.pause_game" ||
			fail four "controller 3's START did not open its pause menu" ;;
	*)
		echo "usage: $0 [two|three|four]..." >&2; exit 2 ;;
	esac
done
echo "logs in $out"
[ $status = 0 ] && echo PASS
exit $status
