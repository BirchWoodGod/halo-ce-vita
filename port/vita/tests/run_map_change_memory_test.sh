#!/bin/bash
# The C heap across many map changes (Oct 13 2026). The harness's bot
# playlist (run_bots_test.sh's playlist case: local split-screen bot games,
# game after game, the main menu's map between) crept ~50 KB a map change,
# for good: the renderer's texture entries (vita_textures.c, ~150 a
# multiplayer map, each an address no map before had used) and its pixel
# shader keys (d3d8_gxm.c's fragment entries, ~90 a map) were kept for the
# session (triage/heap-creep-1.1.1.md). Now the texture entries go with
# each map's textures and the keys are capped (HALO_FRAGMENT_CACHE_CAP: here
# 64, small, so that the cap is reached in a few games; 1024 by default).
#
# The Linux harness (gxm-null: the Vita's renderer over a null GPU) plays
# HALO_TEST_MAP_CHANGES map changes (20: a game and the menu after it are
# two) of the "Everything" playlist, short games (to 3 kills or a minute),
# and logs the C heap at every load of the main menu's map ("memory: loaded
# levels\ui\ui: C heap N KB in use", xbox_memory.c). From the second
# menu (the first game made what lasts the session: the frame
# interpolation's buffers, the programs) to the last, the C heap may grow
# HALO_TEST_HEAP_GROWTH_KB (160) at most (58 KB now); the old code grew 534 KB in
# eleven games. Every texture entry must be freed with its map ("textures:
# N entries ... freed"), every game must load, and no assertion or exception
# be logged.
#
#   run_map_change_memory_test.sh
#   HALO_TEST_VITA          the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA          a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_MAP_CHANGES   map changes (20)
#   HALO_TEST_HEAP_GROWTH_KB  the C heap's growth allowed (160)
#   HALO_TEST_OUT           where the log goes (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps it)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
changes=${HALO_TEST_MAP_CHANGES:-20}
bound=${HALO_TEST_HEAP_GROWTH_KB:-160}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_map_change_memory_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -e "$data/maps/ui.map" ] || [ ! -e "$data/maps/chillout.map" ]; then
	echo "no maps (ui, chillout) in $data/maps (HALO_TEST_DATA)"
	exit 2
fi
games=$(( (changes + 1) / 2 ))
# (a game: loading, up to a minute, the scores, the lobby's countdown)
seconds=$(( games * 80 + 40 ))
rm -rf "$out/run"
mkdir -p "$out/run/data" "$out/run/save" "$out/run/bin"
ln -sfn "$(cd "$data" && pwd)/maps" "$out/run/data/maps"
cp "$binary" "$out/run/bin/halo"
(cd "$out/run" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
	env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/run/data" \
	HALO_SAVE_ROOT="$out/run/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
	HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
	HALO_NETWORK_TEST=local:chillout:slayer HALO_NETWORK_TEST_START=5 HALO_NETWORK_TEST_SCORE=3 \
	HALO_TEST_INPUT=bot:1 HALO_BOTS=6 HALO_BOT_SKILL=heroic HALO_BOT_PLAYLIST=all HALO_TEST_BOT_PLAYLIST_SEED=3 \
	HALO_TEST_BOT_PLAYLIST_SCORE=3 HALO_TEST_BOT_PLAYLIST_MINUTES=1 HALO_TEST_BOT_PLAYLIST_SCORES=4 \
	HALO_FRAGMENT_CACHE_CAP=64 \
	timeout -k 5 $((seconds + 120)) "$out/run/bin/halo" > "$out/run/run.log" 2>&1)
code=$?
rm -rf "$out/run/bin"
log="$out/run/run.log"
[ "$code" = 0 ] || fail "the harness exited with $code ($log)"
# (a render's "assertion skipped (torn read?)" is the threaded render's, logged and passed over, as the other tests take it)
grep -av "render assertion skipped" "$log" | grep -aE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" | head -3 | sed 's/^/  /'
grep -av "render assertion skipped" "$log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" &&
	fail "an assertion or exception ($log)"
# (the menu's loads: the first is the start, each one after a game)
menus=$(grep -ac "memory: loaded levels\\\\ui\\\\ui: C heap" "$log")
[ "$menus" -gt "$games" ] || fail "$((menus - 1)) games of $games came back to the menu ($log)"
freed=$(grep -ac "textures: [0-9]* entries .* freed with the map" "$log")
[ "$freed" -ge "$games" ] || fail "texture entries were freed with $freed maps, not every map ($log)"
awk -v bound="$bound" '
	/memory: loaded levels\\ui\\ui: C heap / {
		heap = $0; sub(/.*C heap /, "", heap); sub(/ KB.*/, "", heap)
		menu++
		if (menu == 2) first = heap
		if (menu >= 2) last = heap
		line = line " " heap
	}
	END {
		printf "  C heap at the menu (KB):%s\n", line
		if (menu < 3) exit 0
		printf "  from the second menu to the last: %+d KB over %d map changes (%d allowed)\n", last - first, 2 * (menu - 2), bound
		if (last - first > bound) { printf "FAIL: the C heap grew %d KB over %d map changes\n", last - first, 2 * (menu - 2); exit 1 }
	}' "$log" || status=1
if [ "$status" = 0 ]; then
	echo "PASS: $((menus - 1)) bot playlist games, $(( (menus - 1) * 2 )) map changes: the C heap kept within $bound KB, texture entries freed with every map ($log)"
fi
test_out_done $status
exit $status
