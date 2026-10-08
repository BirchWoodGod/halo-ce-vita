#!/bin/bash
# Frame interpolation keeps real time through hitches (source/main/main.c,
# main_interpolation_catch_up): the gxm-null Linux build plays a level with
# frame interpolation on and the tick on its thread, while the game thread
# stalls 250 ms every second (HALO_TEST_HITCH, as a settings change remaking
# the screen's targets does on the Vita); the game's ticks a second, from the
# frame-timing lines after the level has loaded, must stay at 30 (29 or more
# in every window). Without the catch-up the run gave 23.6.
#
#   run_hitch_pacing_test.sh
#
#   HALO_TEST_VITA   the gxm-null Linux build (configure.py --linux-d3d gxm-null):
#                    build/linux/halo of this tree
#   HALO_TEST_DATA   a folder with the game's maps folder
#   HALO_TEST_MAP    the level (default b30)
#   HALO_TEST_SECONDS  how long it runs (default 100)
#   HALO_TEST_HITCH  every:length in ms (default 1000:250)
#   HALO_TEST_ENV    more VAR=value settings (HALO_INTERPOLATION=false shows
#                    the game falling behind)
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=${HALO_TEST_VITA:-$root/build/linux/halo}
data=${HALO_TEST_DATA:?a folder with the maps folder}
map=${HALO_TEST_MAP:-b30}
seconds=${HALO_TEST_SECONDS:-100}
out=$(mktemp -d "${TMPDIR:-/tmp}/hitch_pacing_test.XXXXXX")
mkdir -p "$out/data" "$out/save"
ln -sfn "$(cd "$data" && pwd)/maps" "$out/data/maps"
printf 'map_name levels\\%s\\%s\n' "$map" "$map" > "$out/data/init.txt"
env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/data" HALO_SAVE_ROOT="$out/save" \
	HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 \
	HALO_NO_AUDIO=1 HALO_NET_ONLINE=0 HALO_UPDATE_AUTO=0 HALO_TICK_THREAD=1 HALO_INTERPOLATION=true \
	HALO_FRAME_TIMING=300 HALO_TEST_HITCH=${HALO_TEST_HITCH:-1000:250} ${HALO_TEST_ENV:-} \
	timeout -k 5 $((seconds + 120)) "$binary" > "$out/run.log" 2>&1
code=$?
# (the windows after the first two: the load, the level's first seconds)
rates=$(sed -n 's/.*frame-timing:.* \([0-9.]*\) ticks\/s .*/\1/p' "$out/run.log" | tail -n +3)
status=0
if [ $code -ne 0 ] || [ -z "$rates" ]; then
	echo "FAIL: the run ended with $code, $(echo "$rates" | wc -w) windows"
	tail -5 "$out/run.log"
	status=1
else
	echo "ticks a second, window by window: $(echo $rates)"
	if echo "$rates" | awk '$1 < 29.0 { bad = 1 } END { exit bad ? 0 : 1 }'; then
		echo "FAIL: the game fell behind real time through the hitches"
		status=1
	else
		echo "PASS: 30 ticks a second through a ${HALO_TEST_HITCH:-1000:250} ms hitch"
	fi
	echo "hitches: $(grep -c 'hitch:' "$out/run.log"), stalls not caught up: $(grep -c 'stall not caught up' "$out/run.log")"
fi
rm -rf "$out"
exit $status
