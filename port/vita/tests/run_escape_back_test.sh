#!/bin/bash
# The PC build's Escape (port/linux/src/xinput_sdl.c keyboard_gamepad): B in
# the menus (back, as Backspace), Start in play (the pause menu); Space Start
# in the menus. The key is pushed into SDL's queue as the keyboard's own
# ("@key Escape", HALO_TEST_COMMANDS: sdl_platform.c platform_test_key).
#
#   h3       Menu style Halo 3: MULTIPLAYER, then Escape: back on the main menu
#   classic  Menu style Halo CE: Multiplayer (OpenCE's screen), Escape: back
#   play     a local Team Slayer game with a bot (HALO_NETWORK_TEST local,
#            HALO_TEST_PROFILE's profile; skipped without), Menu style Halo 3:
#            Escape opens the multiplayer pause, Escape again closes it; then
#            with Menu style Halo CE (the Xbox's pause)
#
# Each run must exit by itself with no assertion or exception.
#
#   run_escape_back_test.sh [CASE...]
#   HALO_TEST_VITA        the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA_MENUS  a folder with the game's maps folder and Halo PC's
#                         bitmaps.map and loc.map
#   HALO_TEST_PROFILE     a blam.sav (the play case)
#   HALO_TEST_OUT         where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_escape_back_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-h3 classic play}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -r "$data/maps/bitmaps.map" ] || [ ! -r "$data/maps/bloodgulch.map" ]; then
	echo "no bitmaps.map and bloodgulch.map in $data/maps (HALO_TEST_DATA_MENUS)"
	exit 2
fi
mkdir -p "$out"
trap 'test_out_done $status' EXIT

# run NAME SECONDS COMMANDS PAD [VAR=value...]
run() {
	local name=$1 seconds=$2 commands=$3 pad=$4 dir="$out/$1"
	shift 4
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
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

ended() { # NAME
	local log="$out/$1/run.log"
	echo "--- $1"
	grep -aE "ui: screen|test key|mouse: the menus" "$log" | tail -12
	[ "$(cat "$out/$1/exit")" = 0 ] || fail "$1: the harness exited with $(cat "$out/$1/exit") ($log)"
	grep -av "render assertion skipped" "$log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" &&
		fail "$1: an assertion or exception ($log)"
}

# after NAME PATTERN: the first screen opened after the test's Escape
after() {
	sed -n '/test key: Escape/,$p' "$out/$1/run.log" | grep -a "ui: screen" | head -1 | grep -aq "$2"
}

for case in $cases; do
	case $case in
	h3)
		# (MULTIPLAYER, the second row; Escape 15 s in)
		run h3 26 "450:@key Escape" "wait:150:3000 down:150:700 a wait:150:3000" HALO_MENU_STYLE=halo3
		ended h3
		grep -aq "ui: screen pc.h3.mp.screen" "$out/h3/run.log" || fail "h3: MULTIPLAYER did not open"
		after h3 "pc.h3.main.screen" || fail "h3: Escape on MULTIPLAYER did not go back to the main menu" ;;
	classic)
		run classic 26 "450:@key Escape" "wait:150:3000 down:150:700 a wait:150:3000" HALO_MENU_STYLE=classic
		ended classic
		grep -aq "ui: screen pc.mp.screen" "$out/classic/run.log" || fail "classic: Multiplayer did not open"
		after classic "ui.shell.main_menu.main_menu" || fail "classic: Escape on Multiplayer did not go back to the main menu" ;;
	play)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- play"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		for style in halo3 classic; do
			profile_from=$HALO_TEST_PROFILE run "play_$style" 60 "L300:@key Escape;L420:@key Escape" "" HALO_MENU_STYLE=$style \
				HALO_NETWORK_TEST=local:bloodgulch:team_slayer HALO_NETWORK_TEST_PROFILE=1 HALO_NETWORK_TEST_START=3 HALO_BOTS=1
			ended "play_$style"
			log="$out/play_$style/run.log"
			pause='pc.h3.mppause.screen'
			[ $style = classic ] && pause='ui.shell.multiplayer_game.pause_game.1p_pause_game'
			after "play_$style" "$pause" || fail "play_$style: Escape in play did not open the pause menu"
			sed -n '/test key: Escape/,$p' "$log" | grep -a "test key: Escape\|mouse: the menus" | tr '\n' '|' |
				grep -aq "test key: Escape|.*pointer on|.*test key: Escape|.*pointer off" ||
				fail "play_$style: the second Escape did not close the pause menu"
		done ;;
	*)
		echo "unknown case $case"; status=1 ;;
	esac
done
[ $status = 0 ] && echo "PASS: Escape"
exit $status
