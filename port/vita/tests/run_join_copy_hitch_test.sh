#!/bin/bash
# A join that starts a map copy never freezes the game thread (insider.10,
# Oct 2026: a tester joining a public Damnation game had frames of 1 to 3.5 s
# in the lobby while the map was copied into the cache partition - the game
# thread's file calls waited behind the copy's 4 MB writes on the memory
# card, the maps folder was looked through as it joined, and leaving the
# lobby waited for the copy's write in progress).
#
# run_netns_online_test.sh's code mode, a Vita-like memory card on the
# joiner (xbox_files.c's HALO_IO_THROTTLE_KBPS: reads at 10 MB/s, writes at
# 750 KB/s as the tester's card wrote, one request at a time) and a maps
# folder with HALO_TEST_CUSTOMS more maps (links to an Xbox map under other
# names, with a description each, as a player's custom maps are): the host
# hosts Damnation, which the joiner's cache partition does not hold (its
# ui.map copied there by a first start). The joiner joins it by its code,
# leaves the lobby HALO_TEST_LEAVE_LOBBY seconds later, in the copy,
# precaching Blood Gulch as the next lobby would (network_test.c), and joins
# again; the copy is made again and the game played, and after it the host
# chooses its next maps from its lobby. Every frame of the joiner's game
# thread in the host's lobby, from its arrival to the game's start, must be
# under HALO_TEST_HITCH_MS (100); the leave must have come in a copy.
#
#   run_join_copy_hitch_test.sh
#
#   HALO_TEST_VITA   the Linux build (build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (../data2276)
#   HALO_TEST_OUT    the output folder (kept with HALO_TEST_KEEP=1)
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
data=${HALO_TEST_DATA:-$root/../data2276}
customs=${HALO_TEST_CUSTOMS:-60}
leave=${HALO_TEST_LEAVE_LOBBY:-10}
limit=${HALO_TEST_HITCH_MS:-100}
out=${HALO_TEST_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/join_copy_hitch_test.XXXXXX")}
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out/joinerdata/maps"
for map in "$data"/maps/*; do ln -sfn "$(cd "$(dirname "$map")" && pwd)/$(basename "$map")" "$out/joinerdata/maps/"; done
for index in $(seq -w 1 "$customs"); do
	ln -sfn "$(cd "$data/maps" && pwd)/putput.map" "$out/joinerdata/maps/custom$index.map"
	echo "A custom map, number $index" > "$out/joinerdata/maps/custom$index.txt"
done
# (the joiner's cache partition holds the menus' ui.map already, copied by
# a first start offline at full speed: at the card's rate its copy took the
# first 45 s)
mkdir -p "$out/net/joiner/save" "$out/prefill/data"
ln -sfn "$out/joinerdata/maps" "$out/prefill/data/maps"
(cd "$out/prefill" && env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/prefill/data" \
	HALO_SAVE_ROOT="$out/net/joiner/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=15 HALO_FULLSCREEN=0 \
	HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_NET_ONLINE=false HALO_UPDATE_AUTO=false HALO_TICK_THREAD=1 \
	timeout 120 "${HALO_TEST_VITA:-$root/build/linux/halo}" > "$out/prefill/run.log" 2>&1)
grep -aq "starting precaching of map 'ui'" "$out/prefill/data/debug.txt" ||
	{ echo "FAIL (join copy hitch): the first start did not copy ui.map"; test_out_done 1; exit 1; }
HALO_TEST_OUT=$out/net HALO_TEST_KEEP=1 HALO_TEST_HOST_GAME=host:damnation:slayer \
	HALO_TEST_HOST_ENV="HALO_NETWORK_TEST_START=${HALO_TEST_START:-150} ${HALO_TEST_HOST_ENV:-}" \
	HALO_TEST_SECONDS=${HALO_TEST_SECONDS:-270} HALO_TEST_DATA_JOINER=$out/joinerdata \
	HALO_TEST_JOIN_ENV="HALO_IO_THROTTLE_KBPS=10000 HALO_IO_THROTTLE_WRITE_KBPS=750
		HALO_NETWORK_TEST_LEAVE_LOBBY=$leave ${HALO_TEST_JOIN_ENV:-}" \
	"$here/run_netns_online_test.sh" code > "$out/netns.log" 2>&1
netns=$?
jl=$out/net/joiner/run.log jd=$out/net/joiner/data/debug.txt
status=0
fail() { echo "FAIL (join copy hitch): $*"; status=1; }
grep -aE "^(PASS|FAIL)" "$out/netns.log"
[ $netns -eq 0 ] || fail "the network test failed ($netns): $out/netns.log"
# (the lobbies: from each arrival in the host's lobby, the first join's and
# the one after each game, to the game's start)
lobby=$(awk '/system link: in another.s lobby/ { on = 1 } on && /system link: in a network game/ { on = 0 } on' "$jl")
hitches=$(grep -a "frame-hitch: " <<< "$lobby")
worst=$(sed -n 's/.*frame-hitch: \([0-9.]*\) ms.*/\1/p' <<< "$hitches" | sort -n | tail -1)
copies=$(grep -ac "starting precaching of map 'damnation'" "$jd")
echo "the joiner's lobby: $(grep -ac . <<< "$lobby") lines, $(grep -ac . <<< "$hitches") frames over 100 ms" \
	"(the longest ${worst:-none} ms), damnation copied $copies times, Blood Gulch $(grep -ac "starting precaching of map 'bloodgulch'" "$jd")"
grep -a "frame-hitch" <<< "$lobby" | cut -c1-300
grep -aq "network test: leaving the lobby in a map copy" "$jl" || fail "the joiner did not leave the lobby in a map copy"
grep -aq "network test: left the lobby; precaching" "$jl" || fail "the joiner did not precache another map once it left"
[ "$copies" -ge 2 ] || fail "damnation was not copied again after the lobby was left ($copies copies)"
grep -aq "system link: in a network game" "$jl" || fail "the joiner never loaded the game"
if [ -n "$worst" ] && awk -v w="$worst" -v l="$limit" 'BEGIN { exit !(w >= l) }'; then
	fail "a frame of the joiner's lobby took $worst ms (under $limit wanted)"
fi
[ $status -eq 0 ] && echo "PASS (join copy hitch): the lobby's frames under $limit ms through the map copy and its end"
test_out_done $status
exit $status
