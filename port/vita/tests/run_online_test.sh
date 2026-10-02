#!/bin/sh
# Internet play's short codes, public lobby and ad hoc bridge, end to end on
# one Linux machine: two copies of the Linux build (build/linux/halo, from
# this tree) play a system link game whose host the joiner finds only
# through internet play, with port/vita/tests/mqtt_test_broker.py standing
# in for the public MQTT brokers (no internet needed). Each copy has its own
# loopback address and broadcasts to an address nobody listens on, so the
# local network's game search cannot find the host: the game shows up only
# once the tunnel reaches it.
#
#   run_online_test.sh code     the joiner is given the host's code ABCD-EFGH
#   run_online_test.sh lobby    the host is listed; the joiner browses the
#                               public lobby and joins the first game's code
#   run_online_test.sh adhoc    no brokers: both are in an emulated ad hoc
#                               group (HALO_NET_ADHOC_EMULATE) whose bridge
#                               (p2p_adhoc.c) finds the other and tunnels
#
#   HALO_TEST_DATA   a folder with the game's maps folder (default
#                    ../data2276 beside this tree)
#   HALO_TEST_SECONDS  how long both run (default 75)
#   HALO_TEST_CPUS   taskset CPU lists for the two copies (default
#                    "24-27 28-31")
# Passes if the joiner's log shows the game played with two players.
set -e
mode=${1:-code}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
halo=$root/build/linux/halo
data=${HALO_TEST_DATA:-$root/../data2276}
seconds=${HALO_TEST_SECONDS:-75}
cpus=${HALO_TEST_CPUS:-"24-27 28-31"}
cpu_a=${cpus%% *}
cpu_b=${cpus##* }
out=${TMPDIR:-/tmp}/halo_online_test.$$
port=$((18000 + $$ % 1000))
mkdir -p "$out/a" "$out/b" "$out/saves-a" "$out/saves-b"
ln -s "$(cd "$data" && pwd)/maps" "$out/a/maps"
ln -s "$(cd "$data" && pwd)/maps" "$out/b/maps"
pids=
cleanup() { for pid in $pids; do kill "$pid" 2>/dev/null || true; done; }
trap cleanup EXIT

common="HALO_NULL_RENDERER=1 HALO_NO_AUDIO=1 HALO_UPDATE_AUTO=false HALO_EXIT_AFTER=$seconds
	HALO_NET_ONLINE=true HALO_NET_STUN= HALO_NET_ALLOW_UPNP=false HALO_DISCORD_APPLICATION="
case $mode in
code|lobby)
	python3 "$here/mqtt_test_broker.py" --port $port > "$out/broker.log" 2>&1 &
	pids="$pids $!"
	common="$common HALO_NET_BROKERS=127.0.0.1:$port"
	;;
adhoc)
	common="$common HALO_NET_BROKERS= HALO_NET_ADHOC=true"
	;;
*)
	echo "usage: $0 code|lobby|adhoc" >&2
	exit 2
	;;
esac
host_env="HALO_DATA_ROOT=$out/a HALO_SAVE_ROOT=$out/saves-a HALO_NET_ADDRESS=127.0.0.210 HALO_NET_BROADCAST=127.0.0.250
	HALO_NETWORK_TEST=host:bloodgulch HALO_NETWORK_TEST_START=30 HALO_TEST_INPUT=bot:1"
join_env="HALO_DATA_ROOT=$out/b HALO_SAVE_ROOT=$out/saves-b HALO_NET_ADDRESS=127.0.0.211 HALO_NET_BROADCAST=127.0.0.250
	HALO_TEST_INPUT=bot:2"
[ "$mode" = lobby ] && host_env="$host_env HALO_NET_LOBBY_PUBLIC=true HALO_NET_LOBBY_NAME=online-test"
if [ "$mode" = adhoc ]; then
	host_env="$host_env HALO_NET_ADHOC_EMULATE=127.0.0.210,127.0.0.211"
	join_env="$join_env HALO_NET_ADHOC_EMULATE=127.0.0.211,127.0.0.210 HALO_NETWORK_TEST=join"
fi

(cd "$out/a" && exec timeout -k 5 $((seconds + 10)) env $common $host_env taskset -c "$cpu_a" "$halo" > "$out/a/run.log" 2>&1) &
host_pid=$!
pids="$pids $host_pid"
case $mode in
code)
	# the host's code, once it hosts
	code=
	for i in $(seq 1 60); do
		code=$(sed -n 's/.*others join with the code \([A-Z0-9]\{4\}-[A-Z0-9]\{4\}\).*/\1/p' "$out/a/run.log" | head -1)
		[ -n "$code" ] && break
		sleep 1
	done
	[ -n "$code" ] || { echo "FAIL the host never showed a code"; tail -20 "$out/a/run.log"; exit 1; }
	echo "host's code: $code"
	join_env="$join_env HALO_NETWORK_TEST=join-code:$code"
	;;
lobby)
	join_env="$join_env HALO_NETWORK_TEST=join-public"
	;;
esac
(cd "$out/b" && exec timeout -k 5 $((seconds + 10)) env $common $join_env taskset -c "$cpu_b" "$halo" > "$out/b/run.log" 2>&1) &
join_pid=$!
pids="$pids $join_pid"
# (the game does not quit by itself without a window: timeout stops it)
wait $join_pid || true
wait $host_pid || true
status=0
echo "--- host"; grep -E "Internet play|ad hoc|network test: (hosting|starting)" "$out/a/run.log" | head -20
echo "--- joiner"; grep -E "Internet play|ad hoc|network test: (join|the public)" "$out/b/run.log" | head -20
if grep -q "network test: tick .* player 2:.*| playing" "$out/b/run.log"; then
	echo "PASS ($mode): the joiner played the host's game with two players"
	grep "network test: tick" "$out/b/run.log" | tail -1 | cut -c1-200
else
	echo "FAIL ($mode): the joiner never played the host's game"
	status=1
fi
[ -n "$HALO_TEST_KEEP" ] && echo "logs in $out" || rm -rf "$out"
exit $status
