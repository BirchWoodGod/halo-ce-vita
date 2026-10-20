#!/bin/bash
# The PC build's mouse look (port/linux/src/sdl_platform.c, xinput_sdl.c): in
# play the menus' pointer is off and a motion of the mouse turns the view,
# 100 pixels right 12.6 degrees of yaw and 40 down 5.0 of pitch at the
# default sensitivity (0.0022 radians a pixel). The motions are SDL's own
# events pushed into its queue ("@mouse dx dy", main.c: the event loop takes
# them as a real mouse's), the facing read back by "@aim angles"
# (player_control.c), which also says the pointer and capture state.
#
#   campaign  The Silent Cartographer (init.txt), Menu style Halo 3
#   classic   the same with Menu style Halo CE
#   held      an SDL without relative mouse mode (HALO_TEST_NO_RELATIVE_MOUSE:
#             the 32-bit SDL the Linux package ships has no XInput2, so none on
#             X11 - the owner's "I can't look around", insider.17): the pointer
#             held at the window's centre instead, and the mouse aiming
#   mp, mpclassic  a local Team Slayer game on Blood Gulch with a bot
#             (HALO_NETWORK_TEST local, HALO_TEST_PROFILE's profile; skipped
#             without), Menu style Halo 3 and Halo CE; the pause opened and
#             closed first (Start, then B): the pointer on, then off again, and
#             the mouse aiming after it
#
# Each run must exit by itself with no assertion or exception.
#
#   run_mouse_look_test.sh [CASE...]
#   HALO_TEST_VITA        the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA_MENUS  a folder with the game's maps folder and Halo PC's
#                         bitmaps.map and loc.map (the Halo 3 menus' set)
#   HALO_TEST_PROFILE     a blam.sav (the multiplayer cases)
#   HALO_TEST_OUT         where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_mouse_look_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-campaign classic held mp mpclassic}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -r "$data/maps/b30.map" ] || [ ! -r "$data/maps/bloodgulch.map" ]; then
	echo "no b30.map and bloodgulch.map in $data/maps (HALO_TEST_DATA_MENUS)"
	exit 2
fi
mkdir -p "$out"
trap 'test_out_done $status' EXIT

# run NAME SECONDS COMMANDS PAD INIT [VAR=value...]
run() {
	local name=$1 seconds=$2 commands=$3 pad=$4 init=$5 dir="$out/$1"
	shift 5
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	[ -n "$init" ] && printf '%b\n' "$init" > "$dir/data/init.txt"
	if [ -n "${profile_from:-}" ]; then
		mkdir -p "$dir/save/u/UDATA/869079979327"
		cp "$profile_from" "$dir/save/u/UDATA/869079979327/blam.sav"
		[ -r "$(dirname "$profile_from")/SaveMeta.xbx" ] && cp "$(dirname "$profile_from")/SaveMeta.xbx" "$dir/save/u/UDATA/869079979327/"
	fi
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_MENUS_FOLDER="$root/port/vita/app0/menus/" HALO_UI_LOG=1 HALO_TEST_COMMANDS="$commands" "HALO_TEST_PAD=$pad" \
		"$@" timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
	rm -rf "$dir/save"
}

# looked NAME [held]: the run ended well, the pointer was off in play, and each
# motion turned the view as far as it should
looked() {
	local name=$1 log="$out/$1/run.log" angles
	echo "--- $name"
	grep -aE "mouse: |aim test: |ui: screen" "$log" | tail -12
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name: the harness exited with $(cat "$out/$name/exit") ($log)"
	grep -av "render assertion skipped" "$log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" &&
		fail "$name: an assertion or exception ($log)"
	grep -a "mouse: pointer" "$log" | grep -aqv "mouse: pointer 0 released 0" &&
		fail "$name: in play the menus' pointer was on or the mouse released"
	if [ "${2:-}" = held ]; then
		grep -aq "mouse: SDL's relative mode is not available" "$log" || fail "$name: the pointer was not held instead"
		grep -a "mouse: pointer" "$log" | grep -aqv "relative 0 held 1" && fail "$name: the pointer was not held in play"
	else
		grep -a "mouse: pointer" "$log" | grep -aqv "relative 1 held 0" && fail "$name: the mouse was not in relative mode in play"
	fi
	angles=$(grep -a "aim test: angles" "$log" | sed -E 's/.*angles //' | tr '\n' ' ')
	python3 - "$name" $angles <<'PY' || status=1
import sys
name, values = sys.argv[1], [float(v) for v in sys.argv[2:]]
if len(values) != 6:
    print('FAIL: %s: %d angles read, not 3' % (name, len(values) // 2)); sys.exit(1)
yaw = [values[0], values[2], values[4]]; pitch = [values[1], values[3], values[5]]
turn = (yaw[0] - yaw[1] + 180.0) % 360.0 - 180.0
tilt = pitch[1] - pitch[2]
ok = abs(turn - 12.605) < 0.5 and abs(tilt - 5.042) < 0.5
print('%s %s: 100 px right turned %.3f degrees (12.605), 40 px down %.3f (5.042)' % ('PASS' if ok else 'FAIL', name, turn, tilt))
sys.exit(0 if ok else 1)
PY
}

# (the facing, 100 pixels right, the facing, 40 down, the facing)
look() { # TICK
	echo "L$1:@aim angles;L$(($1 + 10)):@mouse 100 0;L$(($1 + 30)):@aim angles;L$(($1 + 40)):@mouse 0 40;L$(($1 + 60)):@aim angles"
}
b30='map_name levels\\b30\\b30'
pause_pad="level:150:3000 start:150:3000 b:150:3000"
mp_env="HALO_NETWORK_TEST=local:bloodgulch:team_slayer HALO_NETWORK_TEST_PROFILE=1 HALO_NETWORK_TEST_START=3 HALO_BOTS=1"

for case in $cases; do
	case $case in
	campaign|classic)
		style=halo3
		[ "$case" = classic ] && style=classic
		run "$case" 60 "$(look 900)" "" "$b30" HALO_MENU_STYLE=$style
		looked "$case" ;;
	held)
		run held 60 "$(look 900)" "" "$b30" HALO_MENU_STYLE=halo3 HALO_TEST_NO_RELATIVE_MOUSE=1
		looked held held ;;
	mp|mpclassic)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- $case"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		style=halo3
		[ "$case" = mpclassic ] && style=classic
		profile_from=$HALO_TEST_PROFILE run "$case" 70 "$(look 450)" "$pause_pad" "" HALO_MENU_STYLE=$style $mp_env
		looked "$case"
		log="$out/$case/run.log"
		sed -n '/test pad: start/,$p' "$log" | grep -aq "mouse: the menus' pointer on" ||
			fail "$case: the pause did not turn the menus' pointer on"
		sed -n '/test pad: b/,$p' "$log" | grep -aq "mouse: the menus' pointer off" ||
			fail "$case: closing the pause did not turn the pointer off" ;;
	*)
		echo "unknown case $case"; status=1 ;;
	esac
done
[ $status = 0 ] && echo "PASS: mouse look"
exit $status
