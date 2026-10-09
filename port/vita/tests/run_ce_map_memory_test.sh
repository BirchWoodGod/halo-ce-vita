#!/bin/bash
# Memory across map changes (Oct 8 2026: on the owner's Vita a session of
# Yoyorast Island, carousel and Extinction ran the C heap out, and the menus'
# map was then refused as "damaged"). The Linux harness (gxm-null: the
# Vita's renderer, its texture pool and video memory model) goes round
# Custom Edition map -> main menu -> Xbox map -> main menu -> a second
# Custom Edition map -> main menu, HALO_TEST_CE_CYCLES times (5), each map
# for a few seconds, through HALO_TEST_COMMANDS' "M" commands (main.c: each
# in the map loaded after the one before). Every map logs its memory when it
# is unloaded ("memory: unloaded <map>: C heap ..., window ... free;
# texture pool ..., video memory ... free", xbox_memory.c); from the second
# round on (the first fills the caches that last the session: the texture
# pool, the programs, the frame interpolation's buffers), each map must
# leave the C heap within 256 KB of what it left the round before, the
# window as free and video memory as free (no less than 1 MB). Every map
# must load, and no assertion or exception be logged.
#
#   run_ce_map_memory_test.sh
#   HALO_TEST_VITA          the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA          a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_CE_MAPS       Custom Edition maps (default ../custom-maps-dl beside
#                           the tree: the first two besides the resource maps,
#                           Covenant_V_Marines_Beta_5 and extinction preferred)
#   HALO_TEST_CE_RESOURCES  Halo PC's bitmaps.map, sounds.map and loc.map
#                           (default the data's maps folder, else Steam's MCC
#                           halo1/maps/custom_edition)
#   HALO_TEST_CE_CYCLES     rounds (5)
#   HALO_TEST_OUT           where the log goes (kept)
# Without Custom Edition maps or resource maps (none are in the repository)
# it says SKIP and passes.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
ce_maps=${HALO_TEST_CE_MAPS:-$root/../custom-maps-dl}
cycles=${HALO_TEST_CE_CYCLES:-5}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_ce_map_memory_test.$$}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -e "$data/maps/ui.map" ] || [ ! -e "$data/maps/carousel.map" ]; then
	echo "no maps (ui, carousel) in $data/maps (HALO_TEST_DATA)"
	exit 2
fi
resources=${HALO_TEST_CE_RESOURCES:-}
if [ -z "$resources" ]; then
	resources="$data/maps"
	[ -e "$resources/bitmaps.map" ] ||
		resources="$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1/maps/custom_edition"
fi
for name in bitmaps sounds loc; do
	if [ ! -e "$resources/$name.map" ]; then
		echo "SKIP: no Halo PC $name.map (HALO_TEST_CE_RESOURCES)"
		exit 0
	fi
done
# two Custom Edition maps: the preferred ones first
picked=()
for name in Covenant_V_Marines_Beta_5 extinction; do
	[ -e "$ce_maps/$name.map" ] && picked+=("$name")
done
for file in "$ce_maps"/*.map; do
	[ ${#picked[@]} -ge 2 ] && break
	[ -e "$file" ] || continue
	name=$(basename "$file" .map)
	case " ${picked[*]} bitmaps sounds loc " in *" $name "*) continue ;; esac
	picked+=("$name")
done
if [ ${#picked[@]} -lt 2 ]; then
	echo "SKIP: fewer than two Custom Edition maps in $ce_maps (HALO_TEST_CE_MAPS)"
	exit 0
fi

rm -rf "$out/run"
mkdir -p "$out/run/data/maps" "$out/run/save" "$out/run/bin"
for file in "$(cd "$data" && pwd)"/maps/*.map; do
	ln -sfn "$file" "$out/run/data/maps/"
done
for name in "${picked[@]}"; do
	ln -sfn "$(cd "$ce_maps" && pwd)/$name.map" "$out/run/data/maps/$name.map"
done
for name in bitmaps sounds loc; do
	ln -sfn "$(cd "$resources" && pwd)/$name.map" "$out/run/data/maps/$name.map"
done
cp "$binary" "$out/run/bin/halo"
commands=
for cycle in $(seq "$cycles"); do
	for map in "levels\\test\\${picked[0]}\\${picked[0]}" @menu 'levels\test\carousel\carousel' @menu \
		"levels\\test\\${picked[1]}\\${picked[1]}" @menu; do
		case $map in
		@menu) commands="${commands}M240:@menu;" ;;
		*) commands="${commands}M240:map_name $map;" ;;
		esac
	done
done
seconds=$((cycles * 6 * 14 + 30))
(cd "$out/run" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/run/data" \
	HALO_SAVE_ROOT="$out/run/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
	HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
	HALO_CUSTOM_EDITION=1 HALO_WINDOW_FLOOR_KB=22528 "HALO_TEST_COMMANDS=$commands" \
	timeout -k 5 $((seconds + 120)) "$out/run/bin/halo" > "$out/run/run.log" 2>&1)
code=$?
rm -rf "$out/run/bin"
log="$out/run/run.log"
[ "$code" = 0 ] || fail "the harness exited with $code ($log)"
grep -aE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" "$log" | head -3 | sed 's/^/  /'
grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" "$log" && fail "an assertion or exception ($log)"
if grep -aq "could not be loaded" "$log"; then
	grep -a "could not be loaded" "$log" | head -3 | sed 's/^/  /'
	fail "a map was not loaded"
fi
expected=$((cycles * 6))
unloads=$(grep -ac "memory: unloaded " "$log")
# (the menu's own unload too: one more than the maps)
[ "$unloads" -ge "$expected" ] || fail "$unloads maps unloaded of $expected: the cycle did not finish ($log)"
# each map's unload against its unload the round before (its first in each), from the second round on
awk -v first="${picked[0]}" '
	# (the round: the first map named again)
	/test command at tick [0-9]+: map_name / && index($0, "\\" first "\\") { round++ }
	/memory: unloaded / && round >= 2 {
		line = $0
		sub(/.*memory: unloaded /, "", line)
		map = line; sub(/:.*/, "", map)
		heap = line; sub(/.*C heap /, "", heap); sub(/ KB.*/, "", heap)
		window = line; sub(/.*KB in use, /, "", window); sub(/ KB free.*/, "", window)
		video = line; sub(/.*video memory /, "", video); sub(/ KB free.*/, "", video)
		if (seen[map] && last_round[map] < round) {
			if (heap - last_heap[map] > 256)
				{ printf "FAIL: %s left the C heap %d KB fuller than the round before (%d -> %d KB)\n", map, heap - last_heap[map], last_heap[map], heap; bad = 1 }
			if (last_window[map] - window > 0)
				{ printf "FAIL: %s left the window %d KB less free (%d -> %d KB)\n", map, last_window[map] - window, last_window[map], window; bad = 1 }
			if (last_video[map] - video > 1024)
				{ printf "FAIL: %s left video memory %d KB less free (%d -> %d KB)\n", map, last_video[map] - video, last_video[map], video; bad = 1 }
		}
		if (!seen[map]) first_heap[map] = heap
		seen[map] = 1; count[map]++
		if (last_round[map] < round) { last_heap[map] = heap; last_window[map] = window; last_video[map] = video }
		last_round[map] = round
	}
	END {
		for (map in seen)
			printf "  %s: C heap %d -> %d KB over %d unloads from the second round, window %d KB free, video memory %d KB free\n", map, first_heap[map], last_heap[map], count[map], last_window[map], last_video[map]
		exit bad
	}' "$log" || status=1
if [ "$status" = 0 ]; then
	echo "PASS: ${picked[0]} -> menu -> carousel -> menu -> ${picked[1]} -> menu, $cycles rounds: memory back where it was after each map ($log)"
fi
exit $status
