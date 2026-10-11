#!/bin/bash
# The screen's targets when video memory (CDRAM) runs short, on the Linux
# harness (gxm-null: the Vita's renderer, its texture pool and its model of
# CDRAM; d3d8_gxm.c screen_settings_apply, vgxm_memory.h). Each case loads
# b30 at a fixed 50% render resolution with most of CDRAM counted as taken
# (HALO_CDRAM_RESERVE_MB) and changes the settings as the Vita's panel does
# ("@set", main.c):
#
#   dynamic  to Dynamic: its targets (made at the full size, 100%) find no
#            room, so they are made at the largest scale that fits (88% or
#            75%, not 50%: no "waits for a restart"), with no texture pool
#            segment moved out of CDRAM for them and never a screen without
#            targets; once the map is gone (back to the main menu) the full
#            size is tried again
#   fixed    to a fixed 100% (unchanged from before): no room, so the screen
#            keeps 50% and the change waits for a restart
#   figure   the kernel's free CDRAM figure (the hardware's, in 1 MB steps)
#            counting 40 MB the model does not (HALO_CDRAM_HIDDEN_MB, as the
#            owner's Vita refused blocks the model had room for): the
#            texture pool keeps to it and the kernel refuses nothing; with
#            the model alone (HALO_CDRAM_FIGURE=model, as before) the kernel
#            refuses texture pool segments
#
#   run_dynamic_cdram_test.sh [case...]   (all three by default)
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree: configure.py --linux-d3d gxm-null)
#   HALO_TEST_DATA   a folder with the game's maps folder (ui, b30)
#   HALO_TEST_OUT    where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_dynamic_cdram_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if ! grep -aq "HALO_CDRAM_HIDDEN_MB" "$binary"; then
	echo "SKIP: $binary is not a gxm-null harness of this tree (configure.py --linux-d3d gxm-null)"
	exit 0
fi
if [ ! -e "$data/maps/ui.map" ] || [ ! -e "$data/maps/b30.map" ]; then
	echo "no maps (ui, b30) in $data/maps (HALO_TEST_DATA)"
	exit 2
fi
mkdir -p "$out"

run() { # NAME SECONDS COMMANDS [VAR=value...]
	local name=$1 seconds=$2 commands=$3
	shift 3
	rm -rf "$out/$name"
	mkdir -p "$out/$name/data" "$out/$name/save"
	ln -sfn "$(cd "$data" && pwd)/maps" "$out/$name/data/maps"
	(cd "$out/$name" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" \
		HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" \
		HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false \
		HALO_NET_ONLINE=false HALO_DISPLAY_WIDTH=848 HALO_RENDER_SCALE=0.5 HALO_DYNAMIC_RES_MIN=0.625 \
		"HALO_TEST_COMMANDS=M30:map_name levels\\b30\\b30;$commands" "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$out/$name/run.log" 2>&1)
	echo $? > "$out/$name/exit"
}

common_checks() { # NAME
	local log=$out/$1/run.log
	[ "$(cat "$out/$1/exit")" = 0 ] || fail "$1: the harness exited with $(cat "$out/$1/exit") ($log)"
	grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" "$log" && fail "$1: an assertion or exception ($log)"
	grep -aq "NO TARGETS" "$log" && fail "$1: the screen was left without targets ($log)"
	grep -aq "memory: loaded levels.b30.b30" "$log" || fail "$1: b30 was not loaded ($log)"
}

cases=${*:-dynamic fixed figure}
for name in $cases; do
	case $name in
	dynamic) run dynamic 40 "L240:@set HALO_RENDER_SCALE dynamic;M400:@menu" HALO_CDRAM_RESERVE_MB=60 & ;;
	fixed) run fixed 30 "L240:@set HALO_RENDER_SCALE 1" HALO_CDRAM_RESERVE_MB=60 & ;;
	figure)
		run figure 45 "" HALO_CDRAM_HIDDEN_MB=40 HALO_SHADER_TOUR=2 &
		run figure-model 45 "" HALO_CDRAM_HIDDEN_MB=40 HALO_SHADER_TOUR=2 HALO_CDRAM_FIGURE=model & ;;
	*) echo "unknown case $name"; exit 2 ;;
	esac
done
wait

for name in $cases; do
	case $name in
	dynamic)
		common_checks dynamic
		log=$out/dynamic/run.log
		# (the change: the lines after the test command up to the map's unload)
		change=$(sed -n '/@set HALO_RENDER_SCALE dynamic/,/memory: unloaded b30/p' "$log")
		made=$(printf '%s\n' "$change" | grep -a "no CDRAM for the screen's targets at 100%: made at" | head -1 |
			sed 's/.*made at \([0-9]*\)%.*/\1/')
		if [ -z "$made" ]; then
			fail "dynamic: the targets did not go to a smaller ceiling ($log)"
		elif [ "$made" -le 50 ] || [ "$made" -ge 100 ]; then
			fail "dynamic: the targets made at $made% (above 50%, below 100% expected)"
		else
			echo "  dynamic: no room at 100%, made at $made%"
		fi
		printf '%s\n' "$change" | grep -aq "waits for a restart" && fail "dynamic: the change waited for a restart ($log)"
		printf '%s\n' "$change" | grep -aq "cdram: a texture pool part" &&
			fail "dynamic: a texture pool segment was moved out for the targets ($log)"
		after=$(sed -n '/memory: unloaded b30/,$p' "$log")
		printf '%s\n' "$after" | grep -aqE "screen: 848x480 at [0-9]+%: [0-9]+ screen-sized targets made again" ||
			fail "dynamic: the full size not tried again once the map was gone ($log)"
		;;
	fixed)
		common_checks fixed
		log=$out/fixed/run.log
		change=$(sed -n '/@set HALO_RENDER_SCALE 1/,$p' "$log")
		printf '%s\n' "$change" | grep -aq "screen: 848x480 at 50% (the change waits for a restart: no memory for the new size)" ||
			fail "fixed: a fixed 100% with no room did not keep 50% and wait for a restart, as before ($log)"
		printf '%s\n' "$change" | grep -aq "made at" && fail "fixed: a fixed scale went to a smaller ceiling ($log)"
		[ "$status" = 0 ] && echo "  fixed: no room at 100%, 50% kept, the change waits for a restart"
		;;
	figure)
		common_checks figure
		common_checks figure-model
		log=$out/figure/run.log
		refused=$(grep -ac "cannot allocate texture pool" "$log")
		refused_model=$(grep -ac "cannot allocate texture pool" "$out/figure-model/run.log")
		pool=$(grep -a "memory: unloaded\|census: texture pool" "$log" | tail -1)
		echo "  figure: texture pool segments the kernel refused: $refused with its figure, $refused_model with the model's alone"
		[ "$refused" = 0 ] || fail "figure: with the kernel's figure the kernel still refused $refused texture pool segments ($log)"
		[ "$refused_model" -gt 0 ] ||
			echo "  (the model's alone had no refusal either: the pool did not fill CDRAM in this run)"
		grep -aq "follows what is freed\|HALO_CDRAM_HIDDEN_MB" "$log" || fail "figure: the hidden CDRAM not set up ($log)"
		;;
	esac
done
if [ "$status" = 0 ]; then
	echo "PASS: $cases ($out)"
fi
test_out_done $status
exit $status
