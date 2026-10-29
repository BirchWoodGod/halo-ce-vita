#!/bin/bash
# The server browser under a flood of retained listings (port/linux/src/
# p2p_lobby.c's queue): a public broker sends every slot it keeps at once as
# a browser subscribes, and the browser reads three or more of them
# (insider.15/16's logs, Oct 2026: "1386 listings heard ... 27 games shown
# ... dropped 981 past the queue", most of the games never shown). Three
# brokers (mqtt_test_broker.py) here, each keeping HALO_TEST_FLOOD_STALE
# hosts' stale copies (an hour old: three quarters the same on every broker),
# HALO_TEST_FLOOD_OTHER of another network version and HALO_TEST_FLOOD_LIVE
# live games (a quarter on the crossplay wire, OpenCE's, where the build
# reads it; the last broker holding an older listing of every third), each
# signed with its host's own key (p2p_lobby_test.c's "listings"), 500 in all
# by default. The harness (HALO_TEST_VITA) browses the public games (the
# network test's join-public, for a game no host has) inside a network
# namespace of its own with nothing but loopback (unshare -rn: nothing
# reached but the test's brokers). Its browser's summary must show every
# live game and drop none past the queue, and its frames
# (HALO_FRAME_TIMING, HALO_FRAME_CAP=60) stay under 33 ms while the listings
# come and are checked.
#
#   run_browser_flood_test.sh
#   HALO_TEST_VITA          the harness (default build/linux/halo of this tree;
#                           a gxm-null build, configure.py --linux-d3d gxm-null,
#                           runs the Vita's render path)
#   HALO_TEST_DATA          a folder with the game's maps folder (default
#                           ../data2276)
#   HALO_TEST_FLOOD_LIVE (60), HALO_TEST_FLOOD_STALE (400),
#   HALO_TEST_FLOOD_OTHER (40)   each broker's listings
#   HALO_TEST_SECONDS       how long the harness runs (45)
#   HALO_TEST_OUT           where the logs go (deleted after a pass when the test
#                           made it; HALO_TEST_KEEP=1 keeps them)
# Needs unprivileged user namespaces (unshare -rn), ip, python3, and clang
# that targets i686 (the generator: as run_p2p_lobby_test.sh builds it).
set -u
# (Bots online, bots.online: full by default, which fills a game hosted
# online with bots; off here unless a run asks: run_netns_online_test.sh hostbots)
export HALO_BOTS_ONLINE=${HALO_BOTS_ONLINE:-off}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
live=${HALO_TEST_FLOOD_LIVE:-60}
stale=${HALO_TEST_FLOOD_STALE:-400}
other=${HALO_TEST_FLOOD_OTHER:-40}
seconds=${HALO_TEST_SECONDS:-45}
. "$here/test_out.sh"
test_out_begin "${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_browser_flood_test.$$}"
trap 'test_out_done $?' EXIT
if [ ! -x "$binary" ]; then
	echo "SKIP: no harness at $binary (HALO_TEST_VITA)"
	exit 0
fi
if [ ! -r "$data/maps/bloodgulch.map" ]; then
	echo "SKIP: no maps in $data (HALO_TEST_DATA)"
	exit 0
fi
mkdir -p "$out"
status=0
fail() { echo "FAIL: $*"; status=1; }

# the listings, for the harness's side: the Vitas' (hcev, and the crossplay
# wire's) where it plays as a Vita (HALO_NET_AS_VITA: its label in it), else
# the PCs'
net_flags=
grep -aq "hcev-lobby-1" "$binary" && net_flags=-DHALO_NET_AS_VITA=1
clang=${CLANG:-$(sed -n 's/^linux_cc = //p' "$root/build.ninja" | head -1)}
[ -n "$clang" ] || clang=clang
game_flags="-ffunction-sections -fdata-sections -fms-extensions -fshort-wchar -fcommon -fno-strict-aliasing -fwrapv -g -std=gnu11 -D_GNU_SOURCE
	-DHALO_LINUX_PLATFORM_LAYER $net_flags -w -include port/linux/include/halo_linux_prefix.h
	-include build/linux/platform_msvc_semantics.h -Iport/linux/src -Iport/linux/include -Iport/third_party/monocypher
	-Isource -Isource/cseries -idirafter port/include/xdk"
crypto="port/linux/src/p2p_crypto.c port/third_party/monocypher/monocypher.c port/third_party/monocypher/monocypher-ed25519.c
	port/linux/src/lang.c port/linux/src/crossplay_guard.c"
(cd "$root" && $clang --target=i686-linux-gnu -m32 -malign-double -freg-struct-return -O2 $game_flags -Wl,--gc-sections \
	-o "$out/p2p_lobby_test" "$here/p2p_lobby_test.c" $crypto -lpthread) || { echo "FAIL: the generator did not build"; exit 1; }
"$out/p2p_lobby_test" listings "$out/retained" "$live" "$stale" "$other" 3 || { echo "FAIL: no listings made"; exit 1; }

# the brokers and the harness, in a namespace with loopback alone
mkdir -p "$out/data" "$out/save"
ln -sfn "$(cd "$data" && pwd)/maps" "$out/data/maps"
cat > "$out/inside.sh" <<'EOF'
set -u
out=$1 here=$2 binary=$3 seconds=$4
ip link set lo up
pids=
for broker in 0 1 2; do
	python3 "$here/mqtt_test_broker.py" --port $((18831 + broker)) --retained-file "$out/retained.$broker" \
		> "$out/broker$broker.log" 2>&1 &
	pids="$pids $!"
done
sleep 1
(cd "$out" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/data" \
	HALO_SAVE_ROOT="$out/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=60 HALO_FRAME_TIMING=30 HALO_EXIT_AFTER="$seconds" \
	HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false \
	HALO_DISCORD_APPLICATION= HALO_NET_ALLOW_UPNP=false HALO_MENU_STYLE=classic HALO_NET_ONLINE=true \
	HALO_NET_BROKERS=127.0.0.1:18831,127.0.0.1:18832,127.0.0.1:18833 HALO_NET_STUN=127.0.0.1:3478 \
	HALO_NET_RELAYS=127.0.0.1:47320 HALO_NETWORK_TEST=join-public HALO_NETWORK_TEST_PUBLIC_NAME="no such game" \
	timeout -k 5 $((seconds + 60)) "$binary" > "$out/run.log" 2>&1)
echo $? > "$out/exit"
kill $pids 2>/dev/null
wait 2>/dev/null
EOF
unshare -rn bash "$out/inside.sh" "$out" "$here" "$binary" "$seconds"

log=$out/run.log
echo "--- the browser"
grep -aE "Internet play: (browsing|browser \((summary|closing)\)|stopped browsing)" "$log" | head -12
[ "$(cat "$out/exit" 2>/dev/null)" = 0 ] || fail "the harness's exit $(cat "$out/exit" 2>/dev/null)"
grep -aqiE "assert|exception|halt" "$log" && fail "an assertion, exception or halt"
for broker in 0 1 2; do
	grep -q "subscribe .*lobby/s/" "$out/broker$broker.log" || fail "broker $broker: the browser never subscribed to the slots"
done
summary=$(grep -a "Internet play: browser (summary)" "$log" | head -1)
shown=$(echo "$summary" | sed -n 's/.* \([0-9]*\) games\? shown.*/\1/p')
heard=$(echo "$summary" | sed -n 's/.*: \([0-9]*\) listings\? heard.*/\1/p')
[ -n "$summary" ] || fail "no browser summary"
[ "${shown:-0}" = "$live" ] || fail "the browser showed ${shown:-no} games ($live live)"
[ "${heard:-0}" -ge $((3 * (live + stale + other))) ] ||
	fail "the browser heard ${heard:-no} listings ($((3 * (live + stale + other))) retained on the brokers)"
grep -a "Internet play: browser (" "$log" | grep -aq "past the queue" &&
	fail "listings dropped past the queue: $(grep -a "Internet play: browser (" "$log" | grep -ao "dropped [0-9]* past the queue" | head -3 | tr '\n' ' ')"
# (the frames from the browsing's start: the longest, and the average)
frames=$(awk '/Internet play: browsing the public games/ { on = 1 } on && /frame-timing:/ {
	if (match($0, /max [0-9.]+/)) { m = substr($0, RSTART + 4, RLENGTH - 4) + 0; if (m > most) most = m }
	if (match($0, /\| frame [0-9.]+/)) { sum += substr($0, RSTART + 8, RLENGTH - 8); n++ } }
	END { if (n) printf "%.2f %.2f %d", most, sum / n, n }' "$log")
set -- $frames
if [ -n "${1:-}" ] && awk "BEGIN { exit !($1 < 33) }"; then
	echo "  ok: the longest frame while browsing $1 ms (frames $2 ms on average, $3 reports of 30)"
else
	fail "the longest frame while browsing ${1:-unknown} ms (33 at most)"
fi
[ "$status" = 0 ] && echo "PASS: $live live games of $((live + stale + other)) listings on each of 3 brokers, all shown"
exit $status
