#!/bin/bash
# Online play end to end on one Linux machine, with no internet: two copies
# of the Linux build, each in its own network namespace behind its own NAT
# router (Linux masquerade), reach each other only through internet play:
# MQTT signalling (port/vita/tests/mqtt_test_broker.py) and STUN
# (stun_test_server.py) run on a third namespace standing for the internet,
# at 198.51.100.1. Nothing can reach a public broker or STUN server: the
# namespaces have no route out. Needs unprivileged user namespaces
# (unshare -rn), ip and iptables; no root.
#
#   run_netns_online_test.sh MODE
#
#   code     the host (Blood Gulch, slayer then slayer on Chill Out: a map
#            change) shows a code; the joiner joins it, plays, leaves at
#            HALO_TEST_REJOIN seconds and joins again
#   lobby    the host is listed in the public lobby; the joiner browses it
#            and joins the first game
#   pc       the host is a Vita build, the joiner a PC build: by code (it
#            must find nothing: Vitas signal on their own topics) and on
#            one LAN with the host (it must never list or join the game)
#   pchost   the other way round: a PC build hosts, a Vita build on its LAN
#            must never list or join the game
#   lan      online off, both copies on one LAN (system link over Wi-Fi)
#   adhoc    online off, ad hoc on: the two machines' only link to each other
#            is an emulated ad hoc group (HALO_NET_ADHOC_EMULATE)
#   many     online off, the host and HALO_TEST_JOINERS joiners (3) on one
#            LAN; the host takes HALO_TEST_MAX_PLAYERS players (16, the Play
#            page's Max players): the joiners past that must be told the
#            game is full, the others play (HALO_TEST_HOST_ENV: more for the
#            host, e.g. HALO_TICK_SLOWDOWN=19 for a Vita-like host)
#   solo     online off (the Vita's default): one copy hosts Blood Gulch
#            alone; there must be no p2p thread, and the game must run
#   coop     co-op over the network: the host hosts a campaign level
#            (HALO_TEST_COOP_LEVEL, a10 by default) as the Vita's settings
#            panel does (HALO_NET_COOP_LEVEL), the joiner joins its code
#            (online; HALO_TEST_COOP_LAN=1: system link on one LAN, online
#            off); each copy runs its HALO_TEST_COOP_HOST_COMMANDS /
#            HALO_TEST_COOP_JOIN_COMMANDS (main.c's HALO_TEST_COMMANDS: skip
#            votes, loading zones, kills, game_won); the logs are checked by
#            the caller
#   coopmenu co-op from the campaign's menus, over system link on one LAN
#            (online off), every press scripted (HALO_TEST_PAD): the host
#            goes Campaign, a new profile, The Pillar of Autumn, Heroic and Y
#            (Play co-op); the joiner opens the System Link screen as the
#            settings panel's Join a game does (HALO_SYSTEM_LINK_TEST=join)
#            and joins the game it lists. Checked here: the host's lobby
#            opened, the game listed as co-op, both played the level, the
#            cutscene skipped by both votes, and after the host's game_won
#            (HALO_TEST_COOP_WIN, tick 1200) both played The Truth and
#            Reconciliation. HALO_TEST_COOP_LIST_WAIT: the joiner's wait in
#            the list (ms, default 15000)
#
#   HALO_TEST_VITA   the Linux build on the Vitas' side (configure.py
#                    --linux-net-vita): build/linux/halo of this tree
#   HALO_TEST_PC     a Linux build without it (pc mode's joiner)
#   HALO_TEST_DATA   a folder with the game's maps folder
#   HALO_TEST_DATA_HOST, HALO_TEST_DATA_JOINER   the host's and the joiner's
#                    own (map sharing: a joiner without the host's map)
#   HALO_TEST_HOST_GAME  the host's game instead of Blood Gulch and Chill Out
#                    (host:<map>[:<variant>]; code/lobby/adhoc: no map change
#                    is then looked for)
#   HALO_TEST_JOIN_ENV   more VAR=value settings for the joiner
#   HALO_TEST_SECONDS  how long the copies run (default 180)
#   HALO_TEST_REJOIN   seconds into its game the joiner leaves (code; 0 never)
#   HALO_TEST_OUT    where the logs go (kept)
#   HALO_TEST_CPUS   taskset CPU lists for the two copies ("0-7 8-15")
#   HALO_TEST_ENV    more VAR=value settings for both copies (profiling)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)

if [ "${HALO_NETNS_INSIDE:-}" != 1 ]; then
	export HALO_NETNS_INSIDE=1
	exec unshare -rn --fork "$0" "$@"
fi

mode=${1:-code}
vita=${HALO_TEST_VITA:-$root/build/linux/halo}
pc=${HALO_TEST_PC:-}
data=${HALO_TEST_DATA:-$root/../data2276}
seconds=${HALO_TEST_SECONDS:-180}
rejoin=${HALO_TEST_REJOIN:-0}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_netns_test.$$}
cpus=${HALO_TEST_CPUS:-"0-7 8-15"}
cpu_a=${cpus%% *}
cpu_b=${cpus##* }
mkdir -p "$out"
pids=
cleanup() { for pid in $pids; do kill "$pid" 2>/dev/null; done; wait 2>/dev/null; }
trap cleanup EXIT

# ---- the network: "internet" here (198.51.100.1: broker and STUN), a NAT
# router and a machine for each side; with "lan", both machines on one LAN
# behind the first router (pc mode's second half)
# (a process holding a new network namespace; its pid in held)
holder() { unshare -n sleep 100000 > /dev/null 2>&1 & held=$!; pids="$pids $held"; }
in_ns() { local pid=$1; shift; nsenter -t "$pid" -n "$@"; }
ip link set lo up
ip addr add 198.51.100.1/32 dev lo
sysctl -qw net.ipv4.ip_forward=1
side() { # side NAME WAN_SUBNET LAN_SUBNET -> sets ${NAME}_router ${NAME}_machine
	local name=$1 wan=$2 lan=$3 router machine
	holder; router=$held; holder; machine=$held
	ip link add "o_$name" type veth peer name "w_$name"
	ip link set "w_$name" netns "$router"
	ip addr add "$wan.1/24" dev "o_$name"; ip link set "o_$name" up
	in_ns "$router" ip link set lo up
	in_ns "$router" ip addr add "$wan.2/24" dev "w_$name"
	in_ns "$router" ip link set "w_$name" up
	in_ns "$router" ip route add default via "$wan.1"
	# (the LAN: a bridge, which more machines can join: lan_machine)
	in_ns "$router" ip link add name "b_$name" type bridge
	in_ns "$router" ip addr add "$lan.1/24" broadcast "$lan.255" dev "b_$name"
	in_ns "$router" ip link set "b_$name" up
	in_ns "$router" ip link add "l_$name" type veth peer name "m_$name"
	in_ns "$router" ip link set "m_$name" netns "$machine"
	in_ns "$router" ip link set "l_$name" master "b_$name"
	in_ns "$router" ip link set "l_$name" up
	in_ns "$router" sysctl -qw net.ipv4.ip_forward=1
	in_ns "$router" iptables -t nat -A POSTROUTING -o "w_$name" -j MASQUERADE
	# (a datagram from outside that no mapping expects is dropped unseen, as a
	# home router does; else the kernel keeps a record of it as a flow to the
	# router itself, and the machine's own datagram to that peer then gets a
	# new port: hole punching fails on Linux's own NAT, not on the game)
	in_ns "$router" iptables -A INPUT -i "w_$name" -p udp -m conntrack --ctstate NEW -j DROP
	in_ns "$machine" ip link set lo up
	in_ns "$machine" ip addr add "$lan.2/24" broadcast "$lan.255" dev "m_$name"
	in_ns "$machine" ip link set "m_$name" up
	in_ns "$machine" ip route add default via "$lan.1"
	eval "${name}_router=$router ${name}_machine=$machine"
}
side host 10.10.1 192.168.1
side join 10.10.2 192.168.2
python3 "$here/mqtt_test_broker.py" --host 198.51.100.1 --port 1883 > "$out/broker.log" 2>&1 & pids="$pids $!"
python3 "$here/stun_test_server.py" --host 198.51.100.1 --port 3478 > "$out/stun.log" 2>&1 & pids="$pids $!"
sleep 1

# ---- the copies
run_copy() { # run_copy NAME NETNS_PID BINARY CPUS [ENV...]
	local name=$1 ns=$2 binary=$3 cores=$4
	shift 4
	mkdir -p "$out/$name/data" "$out/$name/save"
	local folder=$data
	case $name in
	host) folder=${HALO_TEST_DATA_HOST:-$data} ;;
	joiner) folder=${HALO_TEST_DATA_JOINER:-$data} ;;
	esac
	ln -sfn "$(cd "$folder" && pwd)/maps" "$out/$name/data/maps"
	rm -f "$out/$name/data/init.txt"
	(cd "$out/$name" && exec nsenter -t "$ns" -n env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen \
		HALO_DATA_ROOT="$out/$name/data" HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 \
		HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 \
		HALO_UPDATE_AUTO=false HALO_DISCORD_APPLICATION= HALO_NET_ALLOW_UPNP=false \
		HALO_NET_BROKERS=198.51.100.1:1883 HALO_NET_STUN=198.51.100.1:3478 "$@" ${HALO_TEST_ENV:-} \
		$([ "$name" = joiner ] && echo "${HALO_TEST_JOIN_ENV:-}") \
		taskset -c "$cores" timeout -k 5 $((seconds + 60)) "$binary" > "$out/$name/run.log" 2>&1) &
	pids="$pids $!"
	last_pid=$!
}
host_env="HALO_NET_ONLINE=true HALO_NETWORK_TEST=${HALO_TEST_HOST_GAME:-host:bloodgulch:slayer,slayer@chillout} HALO_NETWORK_TEST_START=20
	HALO_NETWORK_TEST_SCORE=${HALO_TEST_SCORE:-3} HALO_NETWORK_TEST_KILL=20 HALO_TEST_INPUT=bot:1"
wait_code() { # the host's code, once it hosts
	local code= i
	for i in $(seq 1 90); do
		code=$(sed -n 's/.*others join with the code \([A-Z0-9]\{4\}-[A-Z0-9]\{4\}\).*/\1/p' "$out/host/run.log" | head -1)
		[ -n "$code" ] && break
		sleep 1
	done
	echo "$code"
}
status=0
fail() { echo "FAIL ($mode): $*"; status=1; }

case $mode in
code|lobby)
	extra=
	[ "$mode" = lobby ] && extra="HALO_NET_LOBBY_PUBLIC=true"
	run_copy host "$host_machine" "$vita" "$cpu_a" $host_env $extra; host_pid=$last_pid
	code=$(wait_code)
	[ -n "$code" ] || { fail "the host never showed a code"; tail -20 "$out/host/run.log"; exit 1; }
	echo "host's code: $code"
	if [ "$mode" = code ]; then join_mode="join-code:$code"; else join_mode=join-public; fi
	run_copy joiner "$join_machine" "$vita" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=$join_mode \
		HALO_NETWORK_TEST_REJOIN=$rejoin HALO_TEST_INPUT=bot:2; join_pid=$last_pid
	wait $join_pid $host_pid 2>/dev/null
	grep -aE "Internet play|network test: (hosting|starting|map|game|the next|join|leav|the public)" "$out/host/run.log" | head -30 > "$out/host.summary"
	grep -aE "Internet play|network test: (join|leav|the public|search)" "$out/joiner/run.log" | head -30 > "$out/joiner.summary"
	echo "--- host"; cat "$out/host.summary"
	echo "--- joiner"; cat "$out/joiner.summary"
	# (seconds the joiner logged a game being played with two players in it;
	# the joiner's player index changes when it joins again)
	two_players() { grep -a "network test: tick" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:"; }
	two=$(two_players < "$out/joiner/run.log")
	echo "joiner's seconds with two players playing: $two"
	[ "$two" -ge 60 ] || fail "the joiner played the host's game for $two s with two players (60 wanted)"
	# (the map change: code mode, which runs long enough for a game to end)
	[ "$mode" = code ] && [ -z "${HALO_TEST_HOST_GAME:-}" ] && ! grep -aq "network test: map chillout" "$out/host/run.log" && fail "the host never changed map"
	if [ "$rejoin" != 0 ]; then
		grep -aq "network test: joining again" "$out/joiner/run.log" || fail "the joiner never left and joined again"
		again=$(sed -n '/network test: joining again/,$p' "$out/joiner/run.log" | two_players)
		echo "joiner's seconds with two players after joining again: $again"
		[ "$again" -ge 10 ] || fail "the joiner did not play again after leaving"
	fi
	if [ "$mode" = lobby ]; then
		sends=$(grep -c 'publish .*hcev/3/lobby/.* retained' "$out/broker.log")
		echo "lobby entry sent $sends times in $seconds s"
		[ "$sends" -le $((seconds / 10 + 5)) ] || fail "the host sent its lobby entry $sends times"
	fi
	;;
pc)
	[ -n "$pc" ] || { echo "pc mode needs HALO_TEST_PC (a build without --linux-net-vita)"; exit 2; }
	run_copy host "$host_machine" "$vita" "$cpu_a" $host_env HALO_NET_LOBBY_PUBLIC=true; host_pid=$last_pid
	code=$(wait_code)
	[ -n "$code" ] || { fail "the host never showed a code"; exit 1; }
	echo "host's code: $code"
	# by code, from another network
	run_copy pc_code "$join_machine" "$pc" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-code:$code \
		HALO_EXIT_AFTER=70 HALO_TEST_INPUT=bot:2; pc1=$last_pid
	# on the host's LAN: a second machine behind the host's router
	holder; lan=$held
	in_ns "$host_router" ip link add l2_host type veth peer name m2_host
	in_ns "$host_router" ip link set m2_host netns "$lan"
	in_ns "$host_router" ip link set l2_host master b_host
	in_ns "$host_router" ip link set l2_host up
	in_ns "$lan" ip link set lo up
	in_ns "$lan" ip addr add 192.168.1.3/24 broadcast 192.168.1.255 dev m2_host
	in_ns "$lan" ip link set m2_host up
	in_ns "$lan" ip route add default via 192.168.1.1
	run_copy pc_lan "$lan" "$pc" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_EXIT_AFTER=70 \
		HALO_TEST_INPUT=bot:2; pc2=$last_pid
	wait $pc1 $pc2 2>/dev/null
	kill $host_pid 2>/dev/null; wait $host_pid 2>/dev/null
	echo "--- pc by code"; grep -aE "Internet play|network test" "$out/pc_code/run.log" | head -12
	echo "--- pc on the LAN"; grep -aE "network test|Vita|joining" "$out/pc_lan/run.log" | head -12
	grep -aq "no game has code" "$out/pc_code/run.log" || fail "the PC build did not report the code as unknown"
	grep -aq "network test: joining$" "$out/pc_code/run.log" && fail "the PC build joined by code"
	grep -aq "network test: joining$" "$out/pc_lan/run.log" && fail "the PC build on the LAN joined the Vita's game"
	grep -aq "ignoring a Vita's host" "$out/pc_lan/data/debug.txt" ||
		fail "the PC on the LAN never heard the host's advertisement (so the test proves nothing)"
	;;
pchost)
	# the other way round: a PC hosts, a Vita on its LAN searches
	[ -n "$pc" ] || { echo "pchost mode needs HALO_TEST_PC (a build without --linux-net-vita)"; exit 2; }
	run_copy host "$host_machine" "$pc" "$cpu_a" $host_env HALO_EXIT_AFTER=90; host_pid=$last_pid
	holder; lan=$held
	in_ns "$host_router" ip link add l2_host type veth peer name m2_host
	in_ns "$host_router" ip link set m2_host netns "$lan"
	in_ns "$host_router" ip link set l2_host master b_host
	in_ns "$host_router" ip link set l2_host up
	in_ns "$lan" ip link set lo up
	in_ns "$lan" ip addr add 192.168.1.3/24 broadcast 192.168.1.255 dev m2_host
	in_ns "$lan" ip link set m2_host up
	in_ns "$lan" ip route add default via 192.168.1.1
	run_copy vita_lan "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_EXIT_AFTER=80 \
		HALO_TEST_INPUT=bot:2; vita1=$last_pid
	wait $vita1 $host_pid 2>/dev/null
	echo "--- vita on the PC's LAN"; grep -aE "network test" "$out/vita_lan/run.log" | grep -v tick | head -8
	grep -aq "network test: joining$" "$out/vita_lan/run.log" && fail "the Vita build joined the PC's game"
	grep -aq "ignoring a host that is not a Vita" "$out/vita_lan/data/debug.txt" ||
		fail "the Vita on the LAN never heard the PC's advertisement (so the test proves nothing)"
	;;
lan)
	# online off, both on the host's LAN: system link as on a Wi-Fi network
	# (the Vita's default), the baseline for the others
	holder; lan=$held
	in_ns "$host_router" ip link add l2_host type veth peer name m2_host
	in_ns "$host_router" ip link set m2_host netns "$lan"
	in_ns "$host_router" ip link set l2_host master b_host
	in_ns "$host_router" ip link set l2_host up
	in_ns "$lan" ip link set lo up
	in_ns "$lan" ip addr add 192.168.1.3/24 broadcast 192.168.1.255 dev m2_host
	in_ns "$lan" ip link set m2_host up
	in_ns "$lan" ip route add default via 192.168.1.1
	run_copy host "$host_machine" "$vita" "$cpu_a" $host_env HALO_NET_ONLINE=false; host_pid=$last_pid
	sleep 5
	run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_INPUT=bot:2
	join_pid=$last_pid
	wait $join_pid $host_pid 2>/dev/null
	two=$(grep -a "network test: tick" "$out/joiner/run.log" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:")
	echo "joiner's seconds with two players playing: $two"
	[ "$two" -ge 60 ] || fail "the joiner played the host's game for $two s with two players (60 wanted)"
	grep -aq "Internet play: network thread started" "$out/joiner/run.log" && fail "the p2p thread started with online off"
	;;
adhoc)
	# a link between the two machines alone stands for the ad hoc group
	# (posix_net.c emulates the Vita's ad hoc sockets over UDP: the game's own
	# broadcasts stay on each machine's LAN, so it finds the other only through
	# the ad hoc bridge, p2p_adhoc.c); online off
	ip link add a_host type veth peer name a_join
	ip link set a_host netns "$host_machine"
	ip link set a_join netns "$join_machine"
	in_ns "$host_machine" ip addr add 10.99.0.1/30 dev a_host
	in_ns "$host_machine" ip link set a_host up
	in_ns "$join_machine" ip addr add 10.99.0.2/30 dev a_join
	in_ns "$join_machine" ip link set a_join up
	run_copy host "$host_machine" "$vita" "$cpu_a" $host_env HALO_NET_ONLINE=false HALO_NET_ADHOC=true \
		HALO_NET_ADHOC_EMULATE=10.99.0.1,10.99.0.2; host_pid=$last_pid
	sleep 5
	run_copy joiner "$join_machine" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NET_ADHOC=true \
		HALO_NET_ADHOC_EMULATE=10.99.0.2,10.99.0.1 HALO_NETWORK_TEST=join HALO_NETWORK_TEST_REJOIN=$rejoin \
		HALO_TEST_INPUT=bot:2; join_pid=$last_pid
	wait $join_pid $host_pid 2>/dev/null
	echo "--- host"; grep -aE "ad hoc|Internet play|network test: (hosting|map|game|the next)" "$out/host/run.log" | head -20
	echo "--- joiner"; grep -aE "ad hoc|Internet play|network test: (join|leav)" "$out/joiner/run.log" | head -20
	two=$(grep -a "network test: tick" "$out/joiner/run.log" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:")
	echo "joiner's seconds with two players playing: $two"
	[ "$two" -ge 60 ] || fail "the joiner played the host's game for $two s with two players (60 wanted)"
	grep -q CONNECT "$out/broker.log" && fail "ad hoc play reached the signalling broker"
	;;
coop)
	level=${HALO_TEST_COOP_LEVEL:-a10}
	coop_host="HALO_NET_COOP_LEVEL=$level HALO_NETWORK_TEST=host:$level HALO_NETWORK_TEST_START=20 HALO_TEST_INPUT=bot:1:look"
	if [ "${HALO_TEST_COOP_LAN:-0}" = 1 ]; then
		holder; lan=$held
		in_ns "$host_router" ip link add l2_host type veth peer name m2_host
		in_ns "$host_router" ip link set m2_host netns "$lan"
		in_ns "$host_router" ip link set l2_host master b_host
		in_ns "$host_router" ip link set l2_host up
		in_ns "$lan" ip link set lo up
		in_ns "$lan" ip addr add 192.168.1.3/24 broadcast 192.168.1.255 dev m2_host
		in_ns "$lan" ip link set m2_host up
		in_ns "$lan" ip route add default via 192.168.1.1
		run_copy host "$host_machine" "$vita" "$cpu_a" $coop_host HALO_NET_ONLINE=false \
			"HALO_TEST_COMMANDS=${HALO_TEST_COOP_HOST_COMMANDS:-}"; host_pid=$last_pid
		sleep 5
		run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join \
			HALO_TEST_INPUT=bot:2:look "HALO_TEST_COMMANDS=${HALO_TEST_COOP_JOIN_COMMANDS:-}"; join_pid=$last_pid
	else
		run_copy host "$host_machine" "$vita" "$cpu_a" $coop_host HALO_NET_ONLINE=true \
			"HALO_TEST_COMMANDS=${HALO_TEST_COOP_HOST_COMMANDS:-}"; host_pid=$last_pid
		code=$(wait_code)
		[ -n "$code" ] || { fail "the host never showed a code"; exit 1; }
		echo "host's code: $code"
		run_copy joiner "$join_machine" "$vita" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-code:$code \
			HALO_TEST_INPUT=bot:2:look "HALO_TEST_COMMANDS=${HALO_TEST_COOP_JOIN_COMMANDS:-}"; join_pid=$last_pid
	fi
	wait $join_pid $host_pid 2>/dev/null
	grep -a "co-op" "$out/host/data/debug.txt" | head -40 > "$out/host.summary"
	grep -a "co-op" "$out/joiner/data/debug.txt" | head -40 > "$out/joiner.summary"
	echo "--- host"; cat "$out/host.summary"
	echo "--- joiner"; cat "$out/joiner.summary"
	two=$(grep -a "network test: tick" "$out/joiner/run.log" | grep -aEc "player [0-9]+: \(.* player [0-9]+: \(")
	echo "joiner's seconds with both players alive: $two"
	;;
coopmenu)
	holder; lan=$held
	in_ns "$host_router" ip link add l2_host type veth peer name m2_host
	in_ns "$host_router" ip link set m2_host netns "$lan"
	in_ns "$host_router" ip link set l2_host master b_host
	in_ns "$host_router" ip link set l2_host up
	in_ns "$lan" ip link set lo up
	in_ns "$lan" ip addr add 192.168.1.3/24 broadcast 192.168.1.255 dev m2_host
	in_ns "$lan" ip link set m2_host up
	in_ns "$lan" ip route add default via 192.168.1.1
	# (the main menu, Campaign, the new profile's name (START: Done), Heroic, Y)
	run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=false HALO_NETWORK_TEST=watch HALO_UI_LOG=1 \
		"HALO_TEST_PAD=a:150:3000 start:150:3000 wait:150:6000 down:150:800 y" \
		"HALO_TEST_COMMANDS=L60:@vote;L${HALO_TEST_COOP_WIN:-1200}:game_won"; host_pid=$last_pid
	sleep 5
	# (the System Link screen: A joins, A picks the profile, A again; the list,
	# a while for the host's game to be heard, then A joins it)
	run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=watch HALO_UI_LOG=1 \
		HALO_SYSTEM_LINK_TEST=join "HALO_TEST_PAD=wait:150:6000 a a a wait:150:${HALO_TEST_COOP_LIST_WAIT:-15000} a" \
		"HALO_TEST_COMMANDS=L60:@vote"; join_pid=$last_pid
	wait $join_pid $host_pid 2>/dev/null
	hl=$out/host/run.log jl=$out/joiner/run.log hd=$out/host/data/debug.txt jd=$out/joiner/data/debug.txt
	echo "--- host"; grep -aE "ui: screen|co-op:|test pad: y" "$hl" | head -12
	echo "--- joiner"; grep -aE "ui: screen|system link: (joining|in another)|test command" "$jl" | head -12
	grep -aq "co-op: hosting levels.a10.a10 on difficulty 2 from the campaign's menus" "$hl" ||
		fail "the host did not host The Pillar of Autumn on Heroic from the campaign's menus"
	grep -aq "ui: screen .*connected_pregame_screen" "$hl" || fail "the host's lobby (the waiting screen) did not open"
	grep -aq "system link: in another's lobby" "$jl" || fail "the joiner did not join the host's game"
	grep -aq "co-op: the partner is in: the countdown starts" "$hd" || fail "the partner's joining did not start the countdown"
	# (the first level: the joiner's ticks before the next level's restart)
	first() { awk '/network test: tick [0-9]/ { split($0, a, " tick "); t = a[2] + 0; if (t < last - 300) exit; last = t } { print }' "$1"; }
	next_level() { awk '/network test: tick [0-9]/ { split($0, a, " tick "); t = a[2] + 0; if (t < last - 300) n = 1; last = t } n' "$1"; }
	both=$(first "$jl" | grep -a "network test: tick" | grep -aEc "player [0-9]+: \(.* player [0-9]+: \(")
	echo "joiner's seconds on the first level with both players alive: $both"
	[ "$both" -ge 15 ] || fail "both players were alive on the first level for $both s (15 wanted)"
	grep -aq "skipping the cutscene (2 of 2 voted)" "$hd" || fail "the cutscene was not skipped by both votes"
	grep -aq "skip pressed (offered 1" "$jl" || fail "the joiner's vote was not offered"
	grep -aq "precaching map 'levels.a30.a30'" "$jd" || fail "the joiner did not load the next level"
	again=$(next_level "$jl" | grep -a "network test: tick" | grep -aEc "player [0-9]+: \(.* player [0-9]+: \(")
	echo "joiner's seconds on the next level with both players alive: $again"
	[ "$again" -ge 10 ] || fail "both players were alive on the next level for $again s (10 wanted)"
	;;
many)
	# online off, the host and HALO_TEST_JOINERS joiners (default 3) on the
	# host's LAN (system link); the host takes HALO_TEST_MAX_PLAYERS players
	# (the Play page's Max players; default 16). Every joiner past that is
	# told the game is full; the others play. The host's frame timing
	# (HALO_FRAME_TIMING) and net detail go in its log for the caller.
	joiners=${HALO_TEST_JOINERS:-3}
	most=${HALO_TEST_MAX_PLAYERS:-16}
	run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=false HALO_NET_LOBBY_NAME="Many test" \
		HALO_NET_MAX_PLAYERS=$most HALO_NETWORK_TEST=${HALO_TEST_HOST_GAME:-host:bloodgulch:slayer} \
		HALO_NETWORK_TEST_START=$((20 + joiners * 2)) HALO_NETWORK_TEST_SCORE=${HALO_TEST_SCORE:-50} \
		HALO_TEST_INPUT=bot:1 HALO_FRAME_TIMING=150 HALO_NET_PROFILE=1 ${HALO_TEST_HOST_ENV:-}; host_pid=$last_pid
	sleep 5
	join_pids=
	for i in $(seq 1 "$joiners"); do
		holder; lan=$held
		in_ns "$host_router" ip link add "l${i}_h" type veth peer name "m${i}_h"
		in_ns "$host_router" ip link set "m${i}_h" netns "$lan"
		in_ns "$host_router" ip link set "l${i}_h" master b_host
		in_ns "$host_router" ip link set "l${i}_h" up
		in_ns "$lan" ip link set lo up
		in_ns "$lan" ip addr add "192.168.1.$((10 + i))/24" broadcast 192.168.1.255 dev "m${i}_h"
		in_ns "$lan" ip link set "m${i}_h" up
		in_ns "$lan" ip route add default via 192.168.1.1
		run_copy "joiner$i" "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join \
			HALO_NET_PLAYER_NAME="Joiner$i" HALO_TEST_INPUT=bot:$((i + 1)); join_pids="$join_pids $last_pid"
		sleep 1
	done
	wait $join_pids $host_pid 2>/dev/null
	full=0 played=0
	for i in $(seq 1 "$joiners"); do
		log=$out/joiner$i/run.log
		if grep -aq "The game is full" "$log"; then
			full=$((full + 1))
		elif grep -a "network test: tick" "$log" | grep -aq "| playing"; then
			played=$((played + 1))
		fi
	done
	echo "joiners that played: $played, told the game is full: $full (max players $most)"
	grep -aE "Many test|the game takes" "$out/host/data/debug.txt" | head -3
	grep -a "frame-timing" "$out/host/run.log" | tail -4
	expected=$((joiners < most - 1 ? joiners : most - 1))
	[ "$played" -ge "$expected" ] || fail "$played joiners played ($expected wanted)"
	[ "$full" -eq $((joiners - expected)) ] || fail "$full joiners were told the game is full ($((joiners - expected)) wanted)"
	;;
solo)
	run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=false HALO_NETWORK_TEST=local:bloodgulch \
		HALO_NETWORK_TEST_START=10 HALO_TEST_INPUT=bot:1; host_pid=$last_pid
	wait $host_pid 2>/dev/null
	grep -aE "Internet play|network test" "$out/host/run.log" | head -10
	grep -aq "Internet play: network thread started" "$out/host/run.log" && fail "the p2p thread started with online off"
	ticks=$(grep -ac "network test: tick" "$out/host/run.log")
	echo "seconds of solo game logged: $ticks"
	[ "$ticks" -ge 30 ] || fail "the solo game ran $ticks s"
	;;
*)
	echo "usage: $0 code|lobby|lan|pc|pchost|adhoc|many|solo|coop|coopmenu" >&2
	exit 2
	;;
esac
grep -aiE "segmentation|assert|crash|fatal" "$out"/*/run.log | grep -v "assertions" | head -5
[ $status = 0 ] && echo "PASS ($mode)"
echo "logs in $out"
exit $status
