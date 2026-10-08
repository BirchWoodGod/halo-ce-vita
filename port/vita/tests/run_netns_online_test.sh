#!/bin/bash
# Online play end to end on one Linux machine, with no internet: two copies
# of the Linux build, each in its own network namespace behind its own NAT
# router (Linux masquerade), reach each other only through internet play:
# MQTT signalling (port/vita/tests/mqtt_test_broker.py) and STUN
# (stun_test_server.py) run on a third namespace standing for the internet,
# at 198.51.100.1, with a relay (port/relay) on 47320 that every copy is told
# of (HALO_NET_RELAYS): the online modes must still connect directly (a relay
# is used only when there is no direct path: relay mode). Nothing can reach a
# public broker, STUN server or relay: the namespaces have no route out.
# Needs unprivileged user namespaces (unshare -rn), ip, iptables and a C
# compiler (CC, default cc: the relay); no root.
#
#   run_netns_online_test.sh MODE
#
#   code     the host (Blood Gulch, slayer then slayer on Chill Out: a map
#            change) shows a code; the joiner joins it, plays, leaves at
#            HALO_TEST_REJOIN seconds and joins again
#   lobby    the host's game is public: listed, signed, in the server browser
#            (p2p_lobby.c); the joiner browses it and joins the first game by
#            its listing; the host's listing is closed (a tombstone, then the
#            slot cleared) when it quits
#   relay    code, with both routers' NAT a symmetric one (every destination a
#            new random port: MASQUERADE --random-fully), so hole punching
#            cannot connect them: the two must connect through the relay
#            alone (never directly) and play; the relay's log must name no
#            address, and its bytes per second are reported
#   lobbypw  the same with a password (HALO_TEST_LOBBY_PASSWORD, default
#            "hunter2"): the game is listed locked, and the joiner opens it
#            with the password
#   pc       the host is a Vita build, the joiner a PC build: by code (it
#            must find nothing: Vitas signal on their own topics) and on
#            one LAN with the host (it must never list or join the game)
#   pchost   the other way round: a PC build hosts, a Vita build on its LAN
#            must never list or join the game
#   spoof    code, and a machine on the host's LAN sends its game ports
#            datagrams claiming the joiner's virtual address: dropped
#   coopmenuonline co-op from the campaign's menus, online: X in the waiting
#            screen makes it public (network.coop_public, private unless
#            chosen); the joiner finds it in the server browser
#   menus    lobby, joined from OpenCE's multiplayer screens (menu_tags.c):
#            the joiner (its maps folder HALO_TEST_DATA_MENUS, with the Halo
#            PC bitmaps.map and loc.map) presses Multiplayer, INTERNET (the
#            server browser), A on the host's row, then on the System Link
#            screen the profile and the game (HALO_TEST_PAD)
#   menuspw  the same with a password (HALO_TEST_LOBBY_PASSWORD), typed on
#            the password screen (HALO_TEST_TEXT_INPUT)
#   menushost the host creates its game from the menus: Multiplayer, CREATE
#            GAME INTERNET, Server Setup (its name typed, Max players,
#            public), START GAME, the profile, the map and the gametype; the
#            joiner joins it from the server browser (network test)
#   lan      online off, both copies on one LAN (system link over Wi-Fi)
#   adhoc    online off, ad hoc on: the two machines' only link to each other
#            is an emulated ad hoc group (HALO_NET_ADHOC_EMULATE)
#   many     online off, the host and HALO_TEST_JOINERS joiners (3) on one
#            LAN; the host takes HALO_TEST_MAX_PLAYERS players (16, the Play
#            page's Max players): the joiners past that must be told the
#            game is full, the others play (HALO_TEST_HOST_ENV: more for the
#            host, e.g. HALO_TICK_SLOWDOWN=19 for a Vita-like host;
#            HALO_TEST_JOIN_ENV: more for every joiner; HALO_TEST_JOIN_STAGGER:
#            seconds between joiners, default 1)
#   solo     online off (the Vita's default): one copy hosts Blood Gulch
#            alone; there must be no p2p thread, and the game must run
#   coop     co-op over the network: the host hosts a campaign level
#            (HALO_TEST_COOP_LEVEL, a10 by default; or a Custom Edition
#            campaign map's level name, 'custom_maps\<name>', its map in
#            HALO_TEST_DATA's maps folder with the Custom Edition resource
#            maps, HALO_TEST_ENV=HALO_CUSTOM_EDITION=1, a joiner without it
#            in HALO_TEST_DATA_JOINER downloading it with
#            HALO_TEST_JOIN_ENV=HALO_MAP_SHARE_ANSWER=yes) as the Vita's settings
#            panel does (HALO_NET_COOP_LEVEL), the joiner joins its code
#            (online; HALO_TEST_COOP_LAN=1: system link on one LAN, online
#            off); each copy runs its HALO_TEST_COOP_HOST_COMMANDS /
#            HALO_TEST_COOP_JOIN_COMMANDS (main.c's HALO_TEST_COMMANDS: skip
#            votes, loading zones, kills, game_won); the rest of the logs
#            are checked by the caller. Each joiner must see every player
#            alive at once for 15 s. HALO_TEST_COOP_JOINERS: how many join
#            (1; up to network.coop_players, 4 on the Vitas),
#            HALO_TEST_COOP_STAGGER seconds apart
#            (1; past the host's start, HALO_NETWORK_TEST_START 20, a late
#            join); HALO_TEST_COOP_HOST_ENV: more for the host
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
#            the list (ms, default 15000); HALO_TEST_COOP_JOINERS: how many
#            join (1; 3 fills the Vitas' four: the short countdown, not the
#            one that leaves room for more)
#   busyport hosting while another program holds the game's ports (another
#            copy of the game, or a stuck one, on the same machine), online
#            off, three machines each with a port of its own taken until the
#            first try has failed: Campaign's co-op (Y on the difficulty) and
#            the System Link list's Y (Create Game) with the server's port
#            5150 taken, the harness's host (player_ui_fast_setup_network_server)
#            with the client's 5151. Each try must fail without a crash, the
#            player told so in an error dialog and left in the menus (the
#            list still looking for games), and the next try,
#            once the port is free, must host: the co-op lobby opens, the
#            map list opens, the harness's game is joined (a fourth machine
#            on its LAN) and played
#   dedicated the dedicated server (ninja linux-server, HALO_TEST_SERVER;
#            port/linux/DEDICATED_SERVER.md) behind the host's router, its
#            UDP port 2302 forwarded there, hosts a cycle of two maps
#            (Blood Gulch, then Chill Out: sv_timelimit 1) with no player of
#            its own; HALO_TEST_JOINERS Vita builds (3; 2 to 4), each behind
#            a NAT of its own, join it: the first and third by its code, the
#            second from the server browser (listed as dedicated). The first
#            leaves at HALO_TEST_REJOIN seconds (50) and joins again. All play
#            both maps, and the server's console (its standard input) is
#            used: sv_players, sv_say
#   dedicatedpc the same server, and PC builds (HALO_TEST_PC): by its code
#            (nothing found), browsing (never listed), on its LAN (never
#            joined), and on its LAN trying to join it anyway
#            (HALO_NETWORK_TEST_CROSS_LINE=1, as a modified PC build would):
#            refused by the server (the Vitas' join token); a Vita build on
#            its LAN plays, so the LAN test proves something
#   dedicatedcoop the server hosts co-op (sv_coop a30, public), two Vita builds
#            join (by code, from the browser) and play the level with the
#            server running its AI and scripts; once they have left, the
#            server ends the empty round (sv_end_empty) and waits in its lobby
#   dedicatedban the server and a Vita joiner by code: sv_ban on the server's
#            console drops it, it is refused joining again (bans.txt), the
#            server restarted (a new code) still refuses it, and sv_unban
#            lets it in again
#
#   HALO_TEST_SERVER the dedicated server (build/linux/halo-server of this tree)
#   HALO_TEST_SYMMETRIC_NAT=all|joiners  every router's NAT a symmetric one
#                    (relay mode's), or every one but the host's (the
#                    server's); HALO_TEST_SERVER_PUBLIC=1 gives the server its
#                    forwarded address (sv_public_address 10.10.1.2:2302)
#   HALO_TEST_SERVER_STATS=1  the server's CPU and memory every 5 s
#                    (server/stats.log); HALO_TEST_SERVER_INIT its init.txt
#                    instead of the test's; HALO_TEST_JOIN_DELAY seconds it
#                    waits empty before the joiners start (dedicated)
#   HALO_TEST_VITA   the Linux build on the Vitas' side (configure.py
#                    --linux-net-vita): build/linux/halo of this tree
#   HALO_TEST_VITA_JOINER  (code, lobby) the joiners' build, when another
#                    (an older one: mixed versions)
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
#   HALO_TEST_NETEM  a home connection's link: netem settings (tc-netem(8):
#                    "delay 40ms 5ms loss 1% rate 8mbit") for what each
#                    side's router sends to the internet
#   HALO_TEST_NETEM_HOST, HALO_TEST_NETEM_JOIN   the host's or the joiner's
#                    own instead (its upload)
#   HALO_TEST_SECOND_JOINER=1  (code) a second joiner behind a third NAT joins
#                    the code as the first does (map sharing: two downloads at
#                    once); its maps folder HALO_TEST_DATA_JOINER2, its CPUs the
#                    third of HALO_TEST_CPUS; the caller checks its log
#   HALO_TEST_MQTT311  1: the broker refuses MQTT 5, as a 3.1.1 broker does
#                    (the copies must connect again with 3.1.1)
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
[ "$mode" = busyport ] && seconds=${HALO_TEST_SECONDS:-100}
[ "$mode" = dedicated ] && seconds=${HALO_TEST_SECONDS:-260}
[ "$mode" = dedicatedpc ] && seconds=${HALO_TEST_SECONDS:-120}
[ "$mode" = dedicatedban ] && seconds=${HALO_TEST_SECONDS:-150}
[ "$mode" = dedicatedcoop ] && seconds=${HALO_TEST_SECONDS:-200}
rejoin=${HALO_TEST_REJOIN:-0}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_netns_test.$$}
cpus=${HALO_TEST_CPUS:-"0-7 8-15"}
cpu_a=${cpus%% *}
cpu_b=${cpus#* }
cpu_c=${cpu_b#* }
cpu_b=${cpu_b%% *}
mkdir -p "$out"
pids=
# (config_kept: config.toml beside the build as it was before a mode that
# changes it, put back at the end: coopmenuonline)
config_kept=
cleanup() {
	for pid in $pids; do kill "$pid" 2>/dev/null; done; wait 2>/dev/null
	if [ -n "$config_kept" ]; then
		config=$(dirname "$vita")/config.toml
		if [ -f "$config_kept" ]; then cp "$config_kept" "$config"; else rm -f "$config"; fi
	fi
}
trap cleanup EXIT

# ---- the network: "internet" here (198.51.100.1: broker and STUN), a NAT
# router and a machine for each side; with "lan", both machines on one LAN
# behind the first router (pc mode's second half)
# (a process holding a new network namespace; its pid in held)
holder() {
	unshare -n sleep 100000 > /dev/null 2>&1 & held=$!; pids="$pids $held"
	# (once unshare has made it: a link moved to the process before stays in
	# this namespace, and the machine then has no address, under load)
	while [ "$(readlink /proc/$held/ns/net)" = "$(readlink /proc/self/ns/net)" ]; do sleep 0.02; done
}
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
	# (relay mode: a symmetric NAT, a new random port for every destination)
	# (HALO_TEST_SYMMETRIC_NAT: all, every side's; joiners, all but the host's)
	local symmetric=
	[ "$mode" = relay ] && symmetric=--random-fully
	[ "${HALO_TEST_SYMMETRIC_NAT:-}" = all ] && symmetric=--random-fully
	[ "${HALO_TEST_SYMMETRIC_NAT:-}" = joiners ] && [ "$name" != host ] && symmetric=--random-fully
	in_ns "$router" iptables -t nat -A POSTROUTING -o "w_$name" -j MASQUERADE $symmetric
	# (a datagram from outside that no mapping expects is dropped unseen, as a
	# home router does; else the kernel keeps a record of it as a flow to the
	# router itself, and the machine's own datagram to that peer then gets a
	# new port: hole punching fails on Linux's own NAT, not on the game)
	in_ns "$router" iptables -A INPUT -i "w_$name" -p udp -m conntrack --ctstate NEW -j DROP
	# (a home connection: its delay, loss and upload rate)
	local netem=${HALO_TEST_NETEM:-}
	[ "$name" = host ] && netem=${HALO_TEST_NETEM_HOST:-$netem}
	[ "$name" = join ] && netem=${HALO_TEST_NETEM_JOIN:-$netem}
	if [ -n "$netem" ]; then
		in_ns "$router" tc qdisc add dev "w_$name" root netem limit 10000 $netem ||
			{ echo "netem ($netem) could not be set on $name's link"; exit 2; }
	fi
	in_ns "$machine" ip link set lo up
	in_ns "$machine" ip addr add "$lan.2/24" broadcast "$lan.255" dev "m_$name"
	in_ns "$machine" ip link set "m_$name" up
	in_ns "$machine" ip route add default via "$lan.1"
	eval "${name}_router=$router ${name}_machine=$machine"
}
side host 10.10.1 192.168.1
side join 10.10.2 192.168.2
[ "${HALO_TEST_SECOND_JOINER:-0}" = 1 ] && side join2 10.10.3 192.168.3
python3 "$here/mqtt_test_broker.py" --host 198.51.100.1 --port 1883 \
	$([ "${HALO_TEST_MQTT311:-0}" = 1 ] && echo --mqtt311) > "$out/broker.log" 2>&1 & pids="$pids $!"
python3 "$here/stun_test_server.py" --host 198.51.100.1 --port 3478 > "$out/stun.log" 2>&1 & pids="$pids $!"
# the relay, built from this tree
relay_pid=
if ${CC:-cc} -std=c11 -O2 -I"$root/port/third_party/monocypher" -o "$out/halo-relay" "$root/port/relay/main.c" \
	"$root/port/relay/relay.c" "$root/port/third_party/monocypher/monocypher.c" 2> "$out/relay.build.log"; then
	"$out/halo-relay" --bind 198.51.100.1 > "$out/relay.log" 2>&1 & relay_pid=$!; pids="$pids $relay_pid"
else
	echo "the relay did not build (relay.build.log)"; [ "$mode" = relay ] && exit 2
fi
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
	joiner2) folder=${HALO_TEST_DATA_JOINER2:-$data} ;;
	esac
	ln -sfn "$(cd "$folder" && pwd)/maps" "$out/$name/data/maps"
	rm -f "$out/$name/data/init.txt"
	(cd "$out/$name" && exec nsenter -t "$ns" -n env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen \
		HALO_DATA_ROOT="$out/$name/data" HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 \
		HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 \
		HALO_UPDATE_AUTO=false HALO_DISCORD_APPLICATION= HALO_NET_ALLOW_UPNP=false \
		HALO_NET_BROKERS=198.51.100.1:1883 HALO_NET_STUN=198.51.100.1:3478 HALO_NET_RELAYS=198.51.100.1:47320 \
		"$@" ${HALO_TEST_ENV:-} \
		$([ "$name" = joiner ] || [ "$name" = joiner2 ] && echo "${HALO_TEST_JOIN_ENV:-}") \
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
code|lobby|lobbypw|relay)
	extra=
	password=${HALO_TEST_LOBBY_PASSWORD:-hunter2}
	[ "$mode" = lobby ] && extra="HALO_NET_HOST_PUBLIC=true"
	[ "$mode" = lobbypw ] && extra="HALO_NET_HOST_PUBLIC=true HALO_NET_LOBBY_PASSWORD=$password"
	run_copy host "$host_machine" "$vita" "$cpu_a" $host_env $extra; host_pid=$last_pid
	code=$(wait_code)
	[ -n "$code" ] || { fail "the host never showed a code"; tail -20 "$out/host/run.log"; exit 1; }
	echo "host's code: $code"
	case $mode in
	code|relay) join_mode="join-code:$code" ;;
	lobby) join_mode=join-public ;;
	lobbypw) join_mode="join-public:$password" ;;
	esac
	run_copy joiner "$join_machine" "${HALO_TEST_VITA_JOINER:-$vita}" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=$join_mode \
		HALO_NETWORK_TEST_REJOIN=$rejoin HALO_TEST_INPUT=bot:2; join_pid=$last_pid
	join2_pid=
	if [ "${HALO_TEST_SECOND_JOINER:-0}" = 1 ]; then
		run_copy joiner2 "$join2_machine" "${HALO_TEST_VITA_JOINER:-$vita}" "$cpu_c" HALO_NET_ONLINE=true HALO_NETWORK_TEST=$join_mode \
			HALO_TEST_INPUT=bot:3; join2_pid=$last_pid
	fi
	wait $join_pid $host_pid $join2_pid 2>/dev/null
	grep -aE "Internet play|network test: (hosting|starting|map|game|the next|join|leav|the public)" "$out/host/run.log" | head -30 > "$out/host.summary"
	grep -aE "Internet play|network test: (join|leav|the public|search)" "$out/joiner/run.log" | grep -v "join: opening" | head -30 > "$out/joiner.summary"
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
	if [ "$mode" = relay ]; then
		# (through the relay alone: never directly; the relay paired them, and
		# names no address in its log)
		kill -TERM "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null
		echo "--- relay"; grep -v "^halo-relay: listening" "$out/relay.log" | head -12
		grep -aq "Internet play: connected to host [0-9a-f]* at 198\.51\.100\.1:47320, through the relay" "$out/joiner/run.log" ||
			fail "the joiner did not connect to the host through the relay"
		grep -aq "Internet play: connected to player [0-9a-f]* at 198\.51\.100\.1:47320, through the relay" "$out/host/run.log" ||
			fail "the host did not reach the joiner through the relay"
		grep -aqE "Internet play: (connected to .*, directly|.* is now reached directly)" "$out"/host/run.log "$out"/joiner/run.log &&
			fail "a direct path connected (the NATs were to stop it)"
		grep -q "allocation [0-9]* ready" "$out/relay.log" || fail "the relay paired no allocation"
		grep -qE "(10\.10\.|192\.168\.|198\.51\.100\.)[0-9]" <(grep -v "^halo-relay: listening" "$out/relay.log") &&
			fail "the relay's log names an address"
		bytes=$(sed -n 's/^summary: .* packets (\([0-9]*\) B) relayed.*/\1/p' "$out/relay.log" | tail -1)
		echo "relayed ${bytes:-0} B in ${seconds} s: $(( ${bytes:-0} * 8 / seconds / 1000 )) kbit/s both ways together"
		[ "${bytes:-0}" -gt 0 ] || fail "the relay carried nothing"
	elif [ "$mode" != code ]; then
		# (the listing on the host's slot, at least once; republished every 30 s
		# and when the game changes, at most every 5 s)
		sends=$(grep -c 'publish .* hcev/3/lobby/s/[0-9a-f]\{32\} [1-9][0-9]* B retained qos1' "$out/broker.log")
		echo "listing published $sends times in $seconds s"
		[ "$sends" -ge 1 ] || fail "the host never published its listing"
		[ "$sends" -le $((seconds / 5 + 10)) ] || fail "the host published its listing $sends times"
		grep -aq 'network test: the public games list "' "$out/joiner/run.log" || fail "the joiner never listed the host's game"
		grep -q 'publish .* hcev/3/lobby/s/[0-9a-f]\{32\} 0 B retained' "$out/broker.log" ||
			fail "the host's slot was never cleared (its tombstone and clearing when it quit, or its will)"
		grep -q 'publish .* hceu/' "$out/broker.log" && fail "a Vita build published on the PCs' topics"
		if [ "$mode" = lobbypw ]; then
			grep -aq 'network test: the public games list ".*\[pw\]' "$out/joiner/run.log" ||
				fail "the game with a password was not listed locked"
			grep -aq "network test: joining the public game with its password" "$out/joiner/run.log" ||
				fail "the joiner did not join the locked game with its password"
		fi
	fi
	;;
menus|menuspw)
	password=${HALO_TEST_LOBBY_PASSWORD:-hunter2}
	menus_data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
	extra="HALO_NET_HOST_PUBLIC=true"
	[ "$mode" = menuspw ] && extra="$extra HALO_NET_LOBBY_PASSWORD=$password"
	run_copy host "$host_machine" "$vita" "$cpu_a" $host_env HALO_NETWORK_TEST_START=${HALO_TEST_MENUS_START:-75} \
		HALO_NET_LOBBY_NAME=VitaHost $extra; host_pid=$last_pid
	code=$(wait_code)
	[ -n "$code" ] || { fail "the host never showed a code"; tail -20 "$out/host/run.log"; exit 1; }
	echo "host's code: $code"
	# (Multiplayer, INTERNET: the server browser; a while for the listing, A on
	# its row; menuspw: the password screen, A types it, down, JOIN GAME; then
	# the System Link screen: A joins, A picks the profile, A, a while for the
	# host's game to be reached, A joins it)
	pad="wait:150:3000 down a wait:150:2000 a wait:150:${HALO_TEST_MENUS_LIST_WAIT:-12000} a"
	[ "$mode" = menuspw ] && pad="$pad wait:150:3000 a wait:150:1500 down a"
	pad="$pad wait:150:6000 a a a wait:150:${HALO_TEST_COOP_LIST_WAIT:-15000} a"
	HALO_TEST_DATA_JOINER=$menus_data run_copy joiner "$join_machine" "${HALO_TEST_VITA_JOINER:-$vita}" "$cpu_b" \
		HALO_NET_ONLINE=true HALO_NETWORK_TEST=watch HALO_UI_LOG=1 HALO_TEST_INPUT=bot:2 "HALO_TEST_PAD=$pad" \
		"HALO_TEST_TEXT_INPUT=$password"; join_pid=$last_pid
	wait $join_pid $host_pid 2>/dev/null
	jl=$out/joiner/run.log
	echo "--- joiner"; grep -aE "menus:|ui: screen|system link: (joining|in another)" "$jl" | head -20
	grep -aq "menus: OpenCE's multiplayer screens: .* added" "$jl" || fail "the joiner did not add OpenCE's screens"
	grep -aq "ui: screen pc.browser.screen" "$jl" || fail "the server browser did not open"
	grep -aq "menus: joining the public game VitaHost" "$jl" || fail "the joiner did not join the host's game from the browser"
	if [ "$mode" = menuspw ]; then
		grep -aq "ui: screen pc.join.password.screen" "$jl" || fail "the password screen did not open"
		grep -aq "menus: joining the public game VitaHost with its password" "$jl" || fail "the password was not given"
	fi
	grep -aq "system link: in another's lobby\|system link: in a network game" "$jl" || fail "the joiner did not get into the host's game"
	two=$(grep -a "network test: tick" "$jl" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:")
	echo "joiner's seconds with two players playing: $two"
	[ "$two" -ge 30 ] || fail "the joiner played the host's game for $two s with two players (30 wanted)"
	;;
menushost)
	menus_data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
	# (Multiplayer, CREATE GAME INTERNET: Server Setup; A on SERVER NAME types
	# it; down to START GAME; the System Link screen's profile (A A A), the map
	# list (A), the gametype (A): the lobby; START starts the game)
	pad="wait:150:3000 down a wait:150:2000 down down down a wait:150:2000 a wait:150:1500 down down left down down a"
	pad="$pad wait:150:5000 a a a wait:150:5000 a wait:150:4000 a"
	HALO_TEST_DATA_HOST=$menus_data run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=true \
		HALO_NETWORK_TEST=watch HALO_UI_LOG=1 HALO_TEST_INPUT=bot:1 HALO_NET_HOST_PUBLIC=false "HALO_TEST_PAD=$pad" \
		HALO_TEST_TEXT_INPUT=MenuHost; host_pid=$last_pid
	code=$(wait_code)
	[ -n "$code" ] || { fail "the host never showed a code"; grep -aE "menus:|ui: screen|test pad" "$out/host/run.log" | tail -20; exit 1; }
	echo "host's code: $code"
	run_copy joiner "$join_machine" "${HALO_TEST_VITA_JOINER:-$vita}" "$cpu_b" HALO_NET_ONLINE=true \
		HALO_NETWORK_TEST=join-public HALO_TEST_INPUT=bot:2; join_pid=$last_pid
	wait $join_pid $host_pid 2>/dev/null
	hl=$out/host/run.log jl=$out/joiner/run.log
	echo "--- host"; grep -aE "menus:|ui: screen|system link: (hosting|in a)" "$hl" | head -20
	grep -aq "menus: Server Setup: MenuHost, 16 players, public" "$hl" || fail "Server Setup did not set the game up"
	grep -aq "menus: Create Game > Internet: the game's server started" "$hl" || fail "the game was not created"
	grep -aq 'network test: the public games list ".*MenuHost' "$jl" || fail "the joiner never listed the host's game"
	two=$(grep -a "network test: tick" "$jl" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:")
	echo "joiner's seconds with two players playing: $two"
	[ "$two" -ge 30 ] || fail "the joiner played the host's game for $two s with two players (30 wanted)"
	;;
pc)
	[ -n "$pc" ] || { echo "pc mode needs HALO_TEST_PC (a build without --linux-net-vita)"; exit 2; }
	run_copy host "$host_machine" "$vita" "$cpu_a" $host_env HALO_NET_HOST_PUBLIC=true HALO_NET_LOBBY_NAME=VitaHost
	host_pid=$last_pid
	code=$(wait_code)
	[ -n "$code" ] || { fail "the host never showed a code"; exit 1; }
	echo "host's code: $code"
	# browsing the server browser, from a network of its own (it must not list
	# the Vita's game: the Vita's listings are on the Vitas' topics, signed
	# under their label; alone there, so that the other PCs' games are not on
	# its LAN)
	side browse 10.10.3 192.168.3
	run_copy pc_browse "$browse_machine" "$pc" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-public \
		HALO_NET_HOST_PUBLIC=false HALO_EXIT_AFTER=60 HALO_TEST_INPUT=bot:2; pc0=$last_pid
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
	wait $pc0 $pc1 $pc2 2>/dev/null
	kill $host_pid 2>/dev/null; wait $host_pid 2>/dev/null
	echo "--- pc browsing"; grep -aE "Internet play|network test" "$out/pc_browse/run.log" | grep -v tick | head -8
	grep -aq 'network test: the public games list "VitaHost"' "$out/pc_browse/run.log" &&
		fail "the PC build listed the Vita's public game"
	grep -q 'publish .* hcev/3/lobby/s/[0-9a-f]\{32\} [1-9][0-9]* B retained' "$out/broker.log" ||
		fail "the Vita host never listed its game (so the browsing test proves nothing)"
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
	run_copy host "$host_machine" "$pc" "$cpu_a" $host_env HALO_EXIT_AFTER=90 HALO_NET_HOST_PUBLIC=true \
		HALO_NET_LOBBY_NAME=PCHost; host_pid=$last_pid
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
	# a Vita browsing the server browser from a network of its own: the PC's
	# public game must not show
	side browse 10.10.3 192.168.3
	run_copy vita_browse "$browse_machine" "$vita" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-public \
		HALO_NET_HOST_PUBLIC=false HALO_EXIT_AFTER=70 HALO_TEST_INPUT=bot:2; vita2=$last_pid
	wait $vita1 $vita2 $host_pid 2>/dev/null
	echo "--- vita browsing"; grep -aE "Internet play|network test" "$out/vita_browse/run.log" | grep -v tick | head -8
	grep -aq 'network test: the public games list "PCHost"' "$out/vita_browse/run.log" &&
		fail "the Vita build listed the PC's public game"
	grep -q 'publish .* hceu/3/lobby/s/[0-9a-f]\{32\} [1-9][0-9]* B retained' "$out/broker.log" ||
		fail "the PC host never listed its game (so the browsing test proves nothing)"
	echo "--- vita on the PC's LAN"; grep -aE "network test" "$out/vita_lan/run.log" | grep -v tick | head -8
	grep -aq "network test: joining$" "$out/vita_lan/run.log" && fail "the Vita build joined the PC's game"
	grep -aq "ignoring a host that is not a Vita" "$out/vita_lan/data/debug.txt" ||
		fail "the Vita on the LAN never heard the PC's advertisement (so the test proves nothing)"
	;;
spoof)
	# a joiner plays the host's code; meanwhile a machine on the host's LAN
	# (its router's namespace, with a raw socket) sends the host's game
	# ports datagrams whose source is the joiner's virtual address, as only
	# the tunnel's stand-ins may: the host must drop them (p2p_spoofed_source)
	# and the game go on
	run_copy host "$host_machine" "$vita" "$cpu_a" $host_env; host_pid=$last_pid
	code=$(wait_code)
	[ -n "$code" ] || { fail "the host never showed a code"; exit 1; }
	run_copy joiner "$join_machine" "$vita" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-code:$code \
		HALO_TEST_INPUT=bot:2; join_pid=$last_pid
	joiner_id=
	for i in $(seq 1 90); do
		joiner_id=$(sed -n 's/.*Internet play: connected to player \([0-9a-f]\{12\}\).*/\1/p' "$out/host/run.log" | head -1)
		[ -n "$joiner_id" ] && break
		sleep 1
	done
	[ -n "$joiner_id" ] || fail "the joiner never reached the host"
	sleep 20
	# (the joiner's virtual address on the host: p2p.c's virtual_address_for)
	in_ns "$host_router" python3 - "$joiner_id" <<'PYTHON'
import hashlib, socket, struct, sys, time
digest = hashlib.sha256(bytes.fromhex(sys.argv[1])).digest()
value = digest[0] << 16 | digest[1] << 8 | digest[2]
while True:
    address = 0x64400000 | (value & 0x3FFFFF)
    if address & 255 not in (0, 255):
        break
    value += 1
source = socket.inet_ntoa(struct.pack('>I', address))
raw = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_RAW)
def send(source, port, index):
    # a message header (its size; to the host's port a packet of a type
    # that is none, to a client's a distributed message: which the host logs
    # with their source as it drops them) and 40 bytes
    kind = 3 << 2 if port == 5150 else 2 << 2
    payload = struct.pack('>H', 42 << 4 | kind) + bytes(range(index % 200, index % 200 + 39)) + bytes([99])
    udp = struct.pack('>HHHH', port, port, 8 + len(payload), 0) + payload
    header = struct.pack('>BBHHHBBH4s4s', 0x45, 0, 20 + len(udp), index, 0, 64, 17, 0,
        socket.inet_aton(source), socket.inet_aton('192.168.1.2'))
    raw.sendto(header + udp, ('192.168.1.2', 0))
# (the game's ports: the host's 5150, a client's 5151)
for index in range(200):
    for port in (5150, 5151):
        send(source, port, index)
# and a few from an address of the LAN, which the host logs: they arrive
# (a stray datagram is logged once a second at most)
time.sleep(2)
for index in range(3):
    for port in (5150, 5151):
        send('192.168.1.77', port, index)
print('spoofed 400 datagrams from', source)
PYTHON
	wait $join_pid $host_pid 2>/dev/null
	grep -a "spoofed" "$out/host/run.log" | head -3
	grep -aEq "(not the host @ |sender= ')192\.168\.1\.77" "$out/host/data/debug.txt" ||
		fail "the host never logged the LAN machine's datagrams (so the test proves nothing)"
	grep -aEq "(not the host @ |sender= ')100\." "$out/host/data/debug.txt" && fail "the host's game took a spoofed datagram"
	grep -aq "dropped traffic to the game's port claiming to come from a peer's address" "$out/host/run.log" ||
		fail "the host did not drop the spoofed datagrams"
	two=$(grep -a "network test: tick" "$out/joiner/run.log" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:")
	echo "joiner's seconds with two players playing: $two"
	[ "$two" -ge 60 ] || fail "the joiner played the host's game for $two s with two players (60 wanted)"
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
	joiners=${HALO_TEST_COOP_JOINERS:-1}
	stagger=${HALO_TEST_COOP_STAGGER:-1}
	# (the network test's game is first on the level's map by its file name,
	# which the co-op setting then makes co-op)
	coop_host="HALO_NET_COOP_LEVEL=$level HALO_NETWORK_TEST=host:${level##*\\} HALO_NETWORK_TEST_START=20 HALO_TEST_INPUT=bot:1:look"
	# (more joiners, online: each behind a NAT of its own; system link: each
	# on the host's LAN)
	if [ "${HALO_TEST_COOP_LAN:-0}" = 1 ]; then
		run_copy host "$host_machine" "$vita" "$cpu_a" $coop_host HALO_NET_ONLINE=false \
			"HALO_TEST_COMMANDS=${HALO_TEST_COOP_HOST_COMMANDS:-}" ${HALO_TEST_COOP_HOST_ENV:-}; host_pid=$last_pid
		sleep 5
	else
		run_copy host "$host_machine" "$vita" "$cpu_a" $coop_host HALO_NET_ONLINE=true \
			"HALO_TEST_COMMANDS=${HALO_TEST_COOP_HOST_COMMANDS:-}" ${HALO_TEST_COOP_HOST_ENV:-}; host_pid=$last_pid
		code=$(wait_code)
		[ -n "$code" ] || { fail "the host never showed a code"; exit 1; }
		echo "host's code: $code"
	fi
	join_pids= names=
	for i in $(seq 1 "$joiners"); do
		name=joiner; [ "$i" -gt 1 ] && name=joiner$i
		names="$names $name"
		if [ "${HALO_TEST_COOP_LAN:-0}" = 1 ]; then
			holder; lan=$held
			in_ns "$host_router" ip link add "l$((i + 1))_host" type veth peer name "m$((i + 1))_host"
			in_ns "$host_router" ip link set "m$((i + 1))_host" netns "$lan"
			in_ns "$host_router" ip link set "l$((i + 1))_host" master b_host
			in_ns "$host_router" ip link set "l$((i + 1))_host" up
			in_ns "$lan" ip link set lo up
			in_ns "$lan" ip addr add "192.168.1.$((i + 2))/24" broadcast 192.168.1.255 dev "m$((i + 1))_host"
			in_ns "$lan" ip link set "m$((i + 1))_host" up
			in_ns "$lan" ip route add default via 192.168.1.1
			run_copy "$name" "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join \
				HALO_TEST_INPUT=bot:$((i + 1)):look "HALO_TEST_COMMANDS=${HALO_TEST_COOP_JOIN_COMMANDS:-}"
		else
			machine=$join_machine
			if [ "$i" -gt 1 ]; then side "j$i" "10.10.$((10 + i))" "192.168.$((10 + i))"; eval "machine=\$j${i}_machine"; fi
			run_copy "$name" "$machine" "$vita" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-code:$code \
				HALO_TEST_INPUT=bot:$((i + 1)):look "HALO_TEST_COMMANDS=${HALO_TEST_COOP_JOIN_COMMANDS:-}"
		fi
		join_pids="$join_pids $last_pid"
		[ "$i" -lt "$joiners" ] && sleep "$stagger"
	done
	wait $join_pids $host_pid 2>/dev/null
	grep -a "co-op" "$out/host/data/debug.txt" | head -40 > "$out/host.summary"
	grep -a "co-op" "$out/joiner/data/debug.txt" | head -40 > "$out/joiner.summary"
	echo "--- host"; cat "$out/host.summary"
	echo "--- joiner"; cat "$out/joiner.summary"
	two=$(grep -a "network test: tick" "$out/joiner/run.log" | grep -aEc "player [0-9]+: \(.* player [0-9]+: \(")
	echo "joiner's seconds with both players alive: $two"
	# (every joiner played, one alone too: the seconds all of them were alive
	# at once. On a10 the host leaves the cryo tube first, on Easy and Normal
	# once the bot has looked around and pressed X, and only then does a
	# joiner spawn: HALO_TEST_SECONDS 180 leaves about a minute)
	pattern=$(for i in $(seq 0 "$joiners"); do printf 'player [0-9]+: \\(.*'; done)
	for name in $names; do
		all=$(grep -a "network test: tick" "$out/$name/run.log" | grep -aEc "$pattern")
		echo "$name's seconds with all $((joiners + 1)) players alive: $all"
		[ "$all" -ge 15 ] || fail "$name saw all $((joiners + 1)) players alive for $all s (15 wanted)"
	done
	# (a co-op game is private unless chosen: network.coop_public, OpenCE's)
	if [ "${HALO_TEST_COOP_LAN:-0}" != 1 ] && grep -q 'publish .* hcev/3/lobby/s/[0-9a-f]\{32\} [1-9][0-9]* B retained' "$out/broker.log"; then
		fail "the co-op game was listed in the public games (it is private unless chosen)"
	fi
	;;
coopmenuonline)
	# co-op from the campaign's menus, online: the host goes Campaign, a new
	# profile, The Pillar of Autumn, Heroic and Y (Play co-op), and in the
	# waiting screen X makes the game public (network.coop_public); the
	# joiner finds it in the server browser and joins it (network test).
	# X also writes coop_public = true to config.toml beside the build,
	# which would make every later co-op game of that build public (coop's
	# check that it is private unless chosen): the file is put back after
	config_kept=$out/config.toml.kept
	cp "$(dirname "$vita")/config.toml" "$config_kept" 2>/dev/null
	run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=true HALO_NETWORK_TEST=watch HALO_UI_LOG=1 \
		"HALO_TEST_PAD=a:150:3000 start:150:3000 wait:150:6000 down:150:800 y wait:150:6000 x" \
		"HALO_TEST_COMMANDS=L60:@vote"; host_pid=$last_pid
	code=$(wait_code)
	[ -n "$code" ] || { fail "the host never showed a code"; grep -aE "ui: screen|co-op|test pad" "$out/host/run.log" | tail; exit 1; }
	echo "host's code: $code"
	run_copy joiner "$join_machine" "$vita" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-public \
		HALO_TEST_INPUT=bot:2:look "HALO_TEST_COMMANDS=L60:@vote"; join_pid=$last_pid
	wait $join_pid $host_pid 2>/dev/null
	hl=$out/host/run.log jl=$out/joiner/run.log
	echo "--- host"; grep -aE "ui: screen|co-op: (public|private)|test pad: x" "$hl" | head -12
	echo "--- joiner"; grep -aE "network test: (the public|join)" "$jl" | head -6
	grep -aq "co-op: public: listed in the server browser" "$hl" || fail "X did not make the co-op game public"
	grep -q 'publish .* hcev/3/lobby/s/[0-9a-f]\{32\} [1-9][0-9]* B retained' "$out/broker.log" ||
		fail "the public co-op game was never listed"
	grep -aq 'network test: the public games list "' "$jl" || fail "the joiner never listed the co-op game"
	both=$(grep -a "network test: tick" "$jl" | grep -aEc "player [0-9]+: \(.* player [0-9]+: \(")
	echo "joiner's seconds with both players alive: $both"
	[ "$both" -ge 15 ] || fail "both players were alive for $both s (15 wanted)"
	;;
coopmenu)
	joiners=${HALO_TEST_COOP_JOINERS:-1}
	players=$((joiners + 1))
	# (the main menu, Campaign, the new profile's name (START: Done), Heroic, Y)
	run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=false HALO_NETWORK_TEST=watch HALO_UI_LOG=1 \
		"HALO_TEST_PAD=a:150:3000 start:150:3000 wait:150:6000 down:150:800 y" \
		"HALO_TEST_COMMANDS=L60:@vote;L${HALO_TEST_COOP_WIN:-1200}:game_won"; host_pid=$last_pid
	sleep 5
	# (each joiner on the host's LAN: the System Link screen, A joins, A picks
	# the profile, A again; the list, a while for the host's game to be heard,
	# then A joins it)
	join_pids= names=
	for i in $(seq 1 "$joiners"); do
		name=joiner; [ "$i" -gt 1 ] && name=joiner$i
		names="$names $name"
		holder; lan=$held
		in_ns "$host_router" ip link add "l$((i + 1))_host" type veth peer name "m$((i + 1))_host"
		in_ns "$host_router" ip link set "m$((i + 1))_host" netns "$lan"
		in_ns "$host_router" ip link set "l$((i + 1))_host" master b_host
		in_ns "$host_router" ip link set "l$((i + 1))_host" up
		in_ns "$lan" ip link set lo up
		in_ns "$lan" ip addr add "192.168.1.$((i + 2))/24" broadcast 192.168.1.255 dev "m$((i + 1))_host"
		in_ns "$lan" ip link set "m$((i + 1))_host" up
		in_ns "$lan" ip route add default via 192.168.1.1
		run_copy "$name" "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=watch HALO_UI_LOG=1 \
			HALO_SYSTEM_LINK_TEST=join "HALO_TEST_PAD=wait:150:6000 a a a wait:150:${HALO_TEST_COOP_LIST_WAIT:-15000} a" \
			"HALO_TEST_COMMANDS=L60:@vote"; join_pids="$join_pids $last_pid"
	done
	wait $join_pids $host_pid 2>/dev/null
	hl=$out/host/run.log jl=$out/joiner/run.log hd=$out/host/data/debug.txt jd=$out/joiner/data/debug.txt
	echo "--- host"; grep -aE "ui: screen|co-op:|test pad: y" "$hl" | head -12
	echo "--- joiner"; grep -aE "ui: screen|system link: (joining|in another)|test command" "$jl" | head -12
	grep -aq "co-op: hosting levels.a10.a10 on difficulty 2 from the campaign's menus" "$hl" ||
		fail "the host did not host The Pillar of Autumn on Heroic from the campaign's menus"
	grep -aq "ui: screen .*connected_pregame_screen" "$hl" || fail "the host's lobby (the waiting screen) did not open"
	grep -aq "co-op: the partner is in: the countdown starts" "$hd" || fail "the partner's joining did not start the countdown"
	# (the countdown from its last start to the loading: 15 s while a game of
	# four, network.coop_players on the Vitas, has room, 6 s once it is full)
	wait_s=$(awk '/co-op: the partner is in: the countdown starts/ { t = $2 } /signalling client machines to begin loading/ && t {
		split(t, a, ":"); split($2, b, ":"); print (b[1] * 3600 + b[2] * 60 + b[3]) - (a[1] * 3600 + a[2] * 60 + a[3]); exit }' "$hd")
	echo "the lobby's countdown: ${wait_s:-?} s"
	if [ "$players" = 4 ]; then
		[ -n "$wait_s" ] && [ "$wait_s" -le 8 ] || fail "the full lobby's countdown took ${wait_s:-?} s (6 wanted)"
	fi
	# (the first level: the joiner's ticks before the next level's restart)
	first() { awk '/network test: tick [0-9]/ { split($0, a, " tick "); t = a[2] + 0; if (t < last - 300) exit; last = t } { print }' "$1"; }
	next_level() { awk '/network test: tick [0-9]/ { split($0, a, " tick "); t = a[2] + 0; if (t < last - 300) n = 1; last = t } n' "$1"; }
	pattern=$(for i in $(seq 1 "$players"); do printf 'player [0-9]+: \\(.*'; done)
	for name in $names; do
		grep -aq "system link: in another's lobby" "$out/$name/run.log" || fail "$name did not join the host's game"
		grep -aq "skip pressed (offered 1" "$out/$name/run.log" || fail "$name's vote was not offered"
		both=$(first "$out/$name/run.log" | grep -a "network test: tick" | grep -aEc "$pattern")
		echo "$name's seconds on the first level with all $players players alive: $both"
		[ "$both" -ge 15 ] || fail "all $players players were alive on the first level for $both s (15 wanted)"
		grep -aq "precaching map 'levels.a30.a30'" "$out/$name/data/debug.txt" || fail "$name did not load the next level"
		again=$(next_level "$out/$name/run.log" | grep -a "network test: tick" | grep -aEc "$pattern")
		echo "$name's seconds on the next level with all $players players alive: $again"
		[ "$again" -ge 10 ] || fail "all $players players were alive on the next level for $again s (10 wanted)"
	done
	# (skipped once more than half the machines voted)
	grep -aqE "skipping the cutscene \([0-9]+ of $players voted\)" "$hd" || fail "the cutscene was not skipped by the votes"
	;;
many)
	# online off, the host and HALO_TEST_JOINERS joiners (default 3) on the
	# host's LAN (system link); the host takes HALO_TEST_MAX_PLAYERS players
	# (the Play page's Max players; default 16). Every joiner past that is
	# told the game is full; the others play. The host's frame timing
	# (HALO_FRAME_TIMING) and net detail go in its log for the caller.
	joiners=${HALO_TEST_JOINERS:-3}
	most=${HALO_TEST_MAX_PLAYERS:-16}
	stagger=${HALO_TEST_JOIN_STAGGER:-1}
	run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=false HALO_NET_LOBBY_NAME="Many test" \
		HALO_NET_MAX_PLAYERS=$most HALO_NETWORK_TEST=${HALO_TEST_HOST_GAME:-host:bloodgulch:slayer} \
		HALO_NETWORK_TEST_START=$((20 + joiners * stagger + 10)) HALO_NETWORK_TEST_SCORE=${HALO_TEST_SCORE:-50} \
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
			HALO_NET_PLAYER_NAME="Joiner$i" HALO_TEST_INPUT=bot:$((i + 1)) ${HALO_TEST_JOIN_ENV:-}; join_pids="$join_pids $last_pid"
		sleep "$stagger"
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
	grep -aq "joining the game 'Many test'" "$out/joiner1/data/debug.txt" ||
		fail "the first joiner did not see the host's lobby name in its list"
	expected=$((joiners < most - 1 ? joiners : most - 1))
	[ "$played" -ge "$expected" ] || fail "$played joiners played ($expected wanted)"
	[ "$full" -eq $((joiners - expected)) ] || fail "$full joiners were told the game is full ($((joiners - expected)) wanted)"
	;;
busyport)
	# (a program on each machine holding its port: TCP and UDP 5150, the
	# server's, or UDP 5151, the client's; let go once the copy's first try
	# has failed)
	busy() { # busy NETNS_PID PORT[/udp] -> busy_pid
		in_ns "$1" python3 -c '
import socket, sys, time
port, _, proto = sys.argv[1].partition("/")
held = []
for kind in ((socket.SOCK_DGRAM,) if proto == "udp" else (socket.SOCK_STREAM, socket.SOCK_DGRAM)):
	s = socket.socket(socket.AF_INET, kind)
	s.bind(("0.0.0.0", int(port)))
	if kind == socket.SOCK_STREAM:
		s.listen(1)
	held.append(s)
time.sleep(100000)' "$2" > /dev/null 2>&1 & busy_pid=$!; pids="$pids $busy_pid"
		sleep 0.5
	}
	side list 10.10.3 192.168.3
	busy "$host_machine" 5150; busy_host=$busy_pid
	busy "$join_machine" 5151/udp; busy_join=$busy_pid
	busy "$list_machine" 5150; busy_list=$busy_pid
	# Campaign, the new profile's name (START: Done), Heroic, Y: refused;
	# A closes the error, and Y again once the port is free
	run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=false HALO_NETWORK_TEST=watch HALO_UI_LOG=1 \
		"HALO_TEST_PAD=a:150:3000 start:150:3000 wait:150:6000 down:150:800 y wait:150:4000 a wait:150:15000 y"
	host_pid=$last_pid
	# the harness's host, trying again every 5 s, and a machine on its LAN
	# joining its game
	run_copy joiner "$join_machine" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=host:bloodgulch \
		HALO_NETWORK_TEST_START=10 HALO_TEST_INPUT=bot:1 HALO_UI_LOG=1; harness_pid=$last_pid
	holder; lan=$held
	in_ns "$join_router" ip link add l2_join type veth peer name m2_join
	in_ns "$join_router" ip link set m2_join netns "$lan"
	in_ns "$join_router" ip link set l2_join master b_join
	in_ns "$join_router" ip link set l2_join up
	in_ns "$lan" ip link set lo up
	in_ns "$lan" ip addr add 192.168.2.3/24 broadcast 192.168.2.255 dev m2_join
	in_ns "$lan" ip link set m2_join up
	in_ns "$lan" ip route add default via 192.168.2.1
	run_copy harness_joiner "$lan" "$vita" "$cpu_a" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_INPUT=bot:2
	harness_join_pid=$last_pid
	# the System Link list (as the settings panel's Join a game opens it), Y
	# (Create Game): refused; A closes the error, and Y again
	run_copy list "$list_machine" "$vita" "${cpu_c:-$cpu_b}" HALO_NET_ONLINE=false HALO_NETWORK_TEST=watch HALO_UI_LOG=1 \
		HALO_SYSTEM_LINK_TEST=join "HALO_TEST_PAD=wait:150:6000 a a a wait:150:3000 y wait:150:4000 a wait:150:15000 y"
	list_pid=$last_pid
	# (each port let go 2 s after its machine's first refusal)
	free_after_refusal() { # LOG HOLDER_PID COPY_PID
		while kill -0 "$3" 2>/dev/null && ! grep -aq "network: could not host a game" "$1" 2>/dev/null; do
			sleep 1
		done
		sleep 2
		kill "$2" 2>/dev/null
	}
	free_after_refusal "$out/host/run.log" "$busy_host" "$host_pid" & pids="$pids $!"
	free_after_refusal "$out/joiner/run.log" "$busy_join" "$harness_pid" & pids="$pids $!"
	free_after_refusal "$out/list/run.log" "$busy_list" "$list_pid" & pids="$pids $!"
	wait $host_pid $harness_pid $harness_join_pid $list_pid 2>/dev/null
	for name in host joiner list; do
		log=$out/$name/run.log
		echo "--- $name"
		grep -aE "network: could not|network test: (could|hosting|starting)|co-op: hosting|ui: screen .*(error|pregame|map_select|server_list)|system link: (looking|hosting)" \
			"$log" | head -12
		grep -aq "segmentation fault" "$log" && fail "$name crashed"
		grep -aq "network: could not host a game" "$log" || fail "$name's host with its port taken was not refused"
		grep -aq "ui: screen ui.shell.error.error_modal_fullscreen" "$log" || fail "$name was not told it could not host"
	done
	# (any difficulty: the list opens again behind the error, on its first
	# choice)
	grep -aq "co-op: hosting levels.a10.a10 on difficulty [0-3] from the campaign's menus" "$out/host/run.log" ||
		fail "co-op was not hosted from the campaign's menus once the port was free"
	sed -n '/network: could not host/,$p' "$out/host/run.log" | grep -aq "ui: screen .*connected_pregame_screen" ||
		fail "the co-op lobby did not open once the port was free"
	grep -aq "network test: hosting bloodgulch" "$out/joiner/run.log" || fail "the harness did not host once the port was free"
	two=$(grep -a "network test: tick" "$out/harness_joiner/run.log" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:")
	echo "the harness's game: the joiner's seconds with two players playing: $two"
	[ "$two" -ge 20 ] || fail "the harness's game was played for $two s with two players (20 wanted)"
	# (the list looked for games again after the refusal, before A closed the
	# error and opened it again, then hosted)
	searches=$(awk '/failed to initiate a multiplayer game server/ { on = 1; next } on && /network client disposed/ { exit }
		on && /sent out a broadcast game search packet/ { n++ } END { print n + 0 }' "$out/list/data/debug.txt")
	echo "the list's searches after the refusal: $searches"
	[ "$searches" -ge 1 ] || fail "the System Link list stopped looking for games after the refusal"
	sed -n '/network: could not host/,$p' "$out/list/run.log" | grep -aq "system link: hosting" ||
		fail "the System Link list's Y did not host once the port was free"
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
dedicated|dedicatedpc|dedicatedban|dedicatedcoop)
	server=${HALO_TEST_SERVER:-$root/build/linux/halo-server}
	[ -x "$server" ] || { echo "$mode needs the dedicated server (ninja linux-server, HALO_TEST_SERVER)"; exit 2; }
	[ "$mode" = dedicatedpc ] && [ -z "$pc" ] && { echo "dedicatedpc needs HALO_TEST_PC (a build without --linux-net-vita)"; exit 2; }
	# (the server's router forwards it internet play's port, as its operator
	# would: port/linux/DEDICATED_SERVER.md)
	in_ns "$host_router" iptables -t nat -A PREROUTING -i w_host -p udp --dport 2302 -j DNAT --to-destination 192.168.1.2:2302
	# run_server NAME: the server in the host's machine, its folder
	# $out/NAME/data (maps, init.txt), its console a pipe ($out/NAME/console)
	run_server() {
		local name=$1 sdir=$out/$1
		mkdir -p "$sdir/data"
		ln -sfn "$(cd "${HALO_TEST_DATA_HOST:-$data}" && pwd)/maps" "$sdir/data/maps"
		[ -p "$sdir/console" ] || mkfifo "$sdir/console"
		(cd "$sdir" && exec nsenter -t "$host_machine" -n env HALO_EXIT_AFTER="$seconds" HALO_UPDATE_AUTO=false \
			HALO_NET_ALLOW_UPNP=false HALO_NET_BROKERS=198.51.100.1:1883 HALO_NET_STUN=198.51.100.1:3478 \
			HALO_NET_RELAYS=198.51.100.1:47320 ${HALO_TEST_SERVER_ENV:-} \
			taskset -c "$cpu_a" timeout -k 5 $((seconds + 60)) "$server" -path "$sdir/data" \
			< "$sdir/console" > "$sdir/run.log" 2>&1) &
		pids="$pids $!"
		server_pid=$!
		# (the console held open, so the server does not read its end)
		eval "exec {console_fd}>\"$sdir/console\""
		# (HALO_TEST_SERVER_STATS=1: the server's CPU time and memory every 5 s,
		# $out/NAME/stats.log: seconds, user and system clock ticks, VmRSS and
		# VmHWM in kB, threads)
		if [ "${HALO_TEST_SERVER_STATS:-0}" = 1 ]; then
			(
				spid=
				for i in $(seq 1 50); do spid=$(pgrep -P "$server_pid" | head -1); [ -n "$spid" ] && break; sleep 0.1; done
				[ -n "$spid" ] || exit 0
				start=$(date +%s)
				while [ -r "/proc/$spid/stat" ]; do
					read -r -a stat < "/proc/$spid/stat"
					rss=$(awk '/^VmRSS/ { print $2 }' "/proc/$spid/status" 2>/dev/null)
					hwm=$(awk '/^VmHWM/ { print $2 }' "/proc/$spid/status" 2>/dev/null)
					threads=$(awk '/^Threads/ { print $2 }' "/proc/$spid/status" 2>/dev/null)
					echo "$(( $(date +%s) - start )) ${stat[13]} ${stat[14]} ${rss:-0} ${hwm:-0} ${threads:-0}"
					sleep 5
				done
			) > "$sdir/stats.log" 2>/dev/null &
			pids="$pids $!"
		fi
	}
	console() { echo "$*" >&"$console_fd"; echo "server console: $*"; }
	server_code() { # the server's code once it hosts (its log: $1)
		local code= i
		for i in $(seq 1 90); do
			code=$(sed -n 's/.*Vitas join with the code \([A-Z0-9]\{4\}-[A-Z0-9]\{4\}\).*/\1/p' "$1" | tail -1)
			[ -n "$code" ] && break
			sleep 1
		done
		echo "$code"
	}
	lan_machine() { # lan_machine N: a machine on the server's LAN (192.168.1.N) -> held
		holder; lan=$held
		in_ns "$host_router" ip link add "l${1}_host" type veth peer name "m${1}_host"
		in_ns "$host_router" ip link set "m${1}_host" netns "$lan"
		in_ns "$host_router" ip link set "l${1}_host" master b_host
		in_ns "$host_router" ip link set "l${1}_host" up
		in_ns "$lan" ip link set lo up
		in_ns "$lan" ip addr add "192.168.1.$1/24" broadcast 192.168.1.255 dev "m${1}_host"
		in_ns "$lan" ip link set "m${1}_host" up
		in_ns "$lan" ip route add default via 192.168.1.1
	}
	mkdir -p "$out/server/data"
	cat > "$out/server/data/init.txt" <<'INIT'
# the netns test's server (run_netns_online_test.sh)
sv_name "Netns Dedicated"
sv_maxplayers 8
sv_public 1
sv_mapcycle_add bloodgulch slayer
sv_mapcycle_add chillout slayer
sv_timelimit 1
sv_start_delay 5
sv_postgame 5
sv_end_empty 20
sv_port 2302
INIT
	if [ "$mode" = dedicatedcoop ]; then
		cat > "$out/server/data/init.txt" <<'INIT'
sv_name "Netns Co-op"
sv_coop a30 1
sv_public 1
sv_start_delay 5
sv_end_empty 10
INIT
	fi
	[ "${HALO_TEST_SERVER_PUBLIC:-0}" = 1 ] && echo "sv_public_address 10.10.1.2:2302" >> "$out/server/data/init.txt"
	# (HALO_TEST_SERVER_INIT: another init.txt, e.g. one map for measuring)
	[ -n "${HALO_TEST_SERVER_INIT:-}" ] && cp "$HALO_TEST_SERVER_INIT" "$out/server/data/init.txt"
	run_server server
	code=$(server_code "$out/server/run.log")
	[ -n "$code" ] || { fail "the server never showed a code"; tail -30 "$out/server/run.log"; exit 1; }
	echo "server's code: $code"
	sl=$out/server/run.log sd=$out/server/data/debug.txt
	case $mode in
	dedicated)
		joiners=${HALO_TEST_JOINERS:-3}
		rejoin=${HALO_TEST_REJOIN:-50}
		# (HALO_TEST_JOIN_DELAY: seconds the server waits empty first, its
		# idle cost measured: HALO_TEST_SERVER_STATS)
		sleep "${HALO_TEST_JOIN_DELAY:-0}"
		join_pids= names=
		for i in $(seq 1 "$joiners"); do
			name=joiner; [ "$i" -gt 1 ] && name=joiner$i
			names="$names $name"
			machine=$join_machine
			if [ "$i" -gt 1 ]; then side "d$i" "10.10.$((20 + i))" "192.168.$((20 + i))"; eval "machine=\$d${i}_machine"; fi
			how=join-code:$code again=0
			[ "$i" = 2 ] && how=join-public
			[ "$i" = 1 ] && again=$rejoin
			run_copy "$name" "$machine" "${HALO_TEST_VITA_JOINER:-$vita}" "$cpu_b" HALO_NET_ONLINE=true \
				HALO_NETWORK_TEST=$how HALO_NETWORK_TEST_REJOIN=$again HALO_NET_PLAYER_NAME=Vita$i \
				HALO_EXIT_AFTER=$((seconds - 10)) HALO_TEST_INPUT=bot:$((i + 1)); join_pids="$join_pids $last_pid"
			sleep 2
		done
		# (the console once the first joiner is back from its leave, HALO_TEST_REJOIN)
		sleep $((rejoin > 0 ? rejoin + 50 : 60))
		console sv_players
		console sv_say hello from the netns test
		console sv_status
		wait $join_pids 2>/dev/null
		kill -TERM "$server_pid" 2>/dev/null; wait "$server_pid" 2>/dev/null
		echo "--- server"; grep -aE "^server: (hosting|the next game|the game|player|#|Halo CE|name|cycle|code)" "$sl" | head -40
		grep -aq "^server: the game starts: slayer on bloodgulch" "$sl" || fail "the server never started Blood Gulch"
		grep -aq "^server: the game starts: slayer on chillout" "$sl" || fail "the server never went on to Chill Out (its cycle)"
		grep -aq "^server: the game ends (the time limit)" "$sl" || fail "sv_timelimit never ended a game"
		grep -aqE "^server: #[0-9]+ " "$sl" || fail "sv_players listed no player"
		grep -aq "Server: hello from the netns test" "$sd" || fail "sv_say was not sent"
		grep -q 'publish .* hcev/3/lobby/s/[0-9a-f]\{32\} [1-9][0-9]* B retained' "$out/broker.log" || fail "the server never listed its game"
		grep -aq 'network test: the public games list "Netns Dedicated" \[dedicated\]' "$out/joiner2/run.log" 2>/dev/null ||
			fail "the server browser did not list the server's game as dedicated"
		for name in $names; do
			n=$(grep -a "network test: tick" "$out/$name/run.log" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:")
			echo "$name's seconds playing with another player: $n"
			[ "$n" -ge 40 ] || fail "$name played the server's game with another player for $n s (40 wanted)"
			grep -aq "the host: Server: hello from the netns test" "$out/$name/data/debug.txt" || fail "$name was not told sv_say's line"
			# (the server has no player: none of the joiners' lists has one more
			# than the joiners)
			grep -a "network test: tick" "$out/$name/run.log" | grep -a "| playing" |
				grep -aEq "(player [0-9]+:.*){$((joiners + 1))}" && fail "$name saw more players than the joiners (the server has none)"
		done
		maps=$(grep -ac "precaching map 'levels.test.chillout.chillout'" "$out/joiner2/data/debug.txt")
		[ "$maps" -ge 1 ] || fail "joiner2 never loaded Chill Out"
		if [ "$rejoin" != 0 ]; then
			grep -aq "network test: joining again" "$out/joiner/run.log" || fail "the first joiner never left and joined again"
			again=$(sed -n '/network test: joining again/,$p' "$out/joiner/run.log" | grep -a "network test: tick" | grep -a "| playing" | grep -ac "player")
			echo "joiner's seconds playing after joining again: $again"
			[ "$again" -ge 10 ] || fail "the first joiner did not play again after leaving"
			[ "$(grep -ac '^server: player #[0-9]* Vita1 joined' "$sl")" -ge 2 ] || fail "the server did not see Vita1 join twice"
			grep -aq "^server: player #[0-9]* Vita1 left" "$sl" || fail "the server did not see Vita1 leave"
		fi
		;;
	dedicatedpc)
		# a Vita on the server's LAN (online off) so that the PCs' refusal there
		# proves something
		lan_machine 3
		run_copy vita_lan "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_EXIT_AFTER=90 \
			HALO_TEST_INPUT=bot:2; vita_pid=$last_pid
		# a PC by the code, from a network of its own
		run_copy pc_code "$join_machine" "$pc" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-code:$code \
			HALO_EXIT_AFTER=70 HALO_TEST_INPUT=bot:3; pc1=$last_pid
		# a PC browsing, from another network
		side browse 10.10.3 192.168.3
		run_copy pc_browse "$browse_machine" "$pc" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-public \
			HALO_NET_HOST_PUBLIC=false HALO_EXIT_AFTER=60 HALO_TEST_INPUT=bot:4; pc2=$last_pid
		# a PC on the server's LAN, as built, and one that tries anyway
		lan_machine 4
		run_copy pc_lan "$lan" "$pc" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_EXIT_AFTER=70 \
			HALO_TEST_INPUT=bot:5; pc3=$last_pid
		lan_machine 5
		run_copy pc_cross "$lan" "$pc" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_EXIT_AFTER=70 \
			HALO_NETWORK_TEST_CROSS_LINE=1 HALO_TEST_INPUT=bot:6; pc4=$last_pid
		wait $vita_pid $pc1 $pc2 $pc3 $pc4 2>/dev/null
		kill -TERM "$server_pid" 2>/dev/null; wait "$server_pid" 2>/dev/null
		n=$(grep -a "network test: tick" "$out/vita_lan/run.log" | grep -a "| playing" | grep -ac "player [0-9]*:")
		echo "the Vita on the server's LAN played $n s"
		[ "$n" -ge 20 ] || fail "the Vita on the server's LAN played $n s (20 wanted: else the PCs' refusal proves nothing)"
		echo "--- pc by code"; grep -aE "Internet play|network test" "$out/pc_code/run.log" | grep -v tick | head -6
		grep -aq "no game has code" "$out/pc_code/run.log" || fail "the PC build did not report the server's code as unknown"
		grep -aq 'network test: the public games list "Netns Dedicated"' "$out/pc_browse/run.log" &&
			fail "the PC build listed the server's game"
		grep -aq "network test: joining$" "$out/pc_lan/run.log" && fail "the PC build on the LAN joined the server's game"
		grep -aq "ignoring a Vita's host" "$out/pc_lan/data/debug.txt" ||
			fail "the PC on the LAN never heard the server's advertisement (so the test proves nothing)"
		echo "--- pc trying anyway"; grep -aE "network test: (join|search)" "$out/pc_cross/run.log" | head -4
		grep -aq "tried to join game with a bad join token" "$sd" || fail "the server did not refuse the PC's join (its token)"
		# (never in the game: not added, and no game map loaded; a copy at the
		# main menu logs its ticks too)
		grep -aq "server added machine @ 192.168.1.5:" "$sd" && fail "the server added the PC that tried anyway"
		grep -aq "precaching map 'levels.test" "$out/pc_cross/data/debug.txt" && fail "the PC that tried anyway loaded the game"
		;;
	dedicatedcoop)
		run_copy joiner "$join_machine" "$vita" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-code:$code \
			HALO_NET_PLAYER_NAME=Vita1 HALO_EXIT_AFTER=$((seconds - 50)) HALO_TEST_INPUT=bot:2:look; j1=$last_pid
		sleep 5
		side d2 10.10.22 192.168.22
		run_copy joiner2 "$d2_machine" "$vita" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-public \
			HALO_NET_PLAYER_NAME=Vita2 HALO_EXIT_AFTER=$((seconds - 50)) HALO_TEST_INPUT=bot:3:look; j2=$last_pid
		wait $j1 $j2 2>/dev/null
		# (both gone: the round ends after sv_end_empty, 10 s)
		for i in $(seq 1 30); do grep -aq "^server: nobody is left" "$sl" && break; sleep 1; done
		sleep 5
		console sv_status
		sleep 2
		kill -TERM "$server_pid" 2>/dev/null; wait "$server_pid" 2>/dev/null
		echo "--- server"; grep -aE "^server: (co-op|the game|player|nobody|.*in the lobby)" "$sl" | head -12
		grep -aq "^server: the game starts: co-op on a30" "$sl" || fail "the server did not start co-op on a30"
		grep -aq 'network test: the public games list "Netns Co-op" \[dedicated\]' "$out/joiner2/run.log" ||
			fail "the co-op game was not in the server browser (sv_public 1)"
		for name in joiner joiner2; do
			both=$(grep -a "network test: tick" "$out/$name/run.log" | grep -aEc "player [0-9]+: \(.* player [0-9]+: \(")
			echo "$name's seconds with both players alive: $both"
			[ "$both" -ge 15 ] || fail "$name saw both players alive for $both s (15 wanted)"
		done
		grep -aq "co-op: skipping the cutscene" "$sd" || fail "the cutscene was not skipped by the players' votes"
		grep -aq "^server: nobody is left: back to the lobby" "$sl" || fail "the empty co-op round did not end"
		grep -aq "^server: .*in the lobby" "$sl" || fail "the server was not waiting in its lobby"
		;;
	dedicatedban)
		run_copy joiner "$join_machine" "$vita" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-code:$code \
			HALO_NETWORK_TEST_RETRY=20 HALO_NET_PLAYER_NAME=Banned HALO_TEST_INPUT=bot:2 HALO_EXIT_AFTER=$((seconds - 10))
		join_pid=$last_pid
		# (playing first)
		for i in $(seq 1 90); do grep -aq "^server: the game starts" "$sl" && break; sleep 1; done
		sleep 15
		console sv_players
		console sv_ban 0
		sleep 25
		console sv_banlist
		grep -aq "^server: banned player #0" "$sl" || fail "sv_ban did not ban the player"
		grep -aq "ip=10.10.2.2" "$out/server/data/bans.txt" 2>/dev/null || fail "bans.txt has no line with the joiner's address"
		# (kept out by its address while the server runs, and by bans.txt)
		grep -aqE "refusing a machine @ .*: (banned \(bans.txt\)|dropped from this game)" "$sd" ||
			fail "the banned joiner was not refused joining again"
		# the server restarted: the ban still holds
		kill -TERM "$server_pid" 2>/dev/null; wait "$server_pid" 2>/dev/null
		grep -aq "^server: stopping (a signal" "$sl" || fail "the server did not stop on SIGTERM"
		grep -q 'publish .* hcev/3/lobby/s/[0-9a-f]\{32\} 0 B retained' "$out/broker.log" ||
			fail "the stopped server's listing was not cleared"
		eval "exec {console_fd}>&-"
		mv "$out/server/run.log" "$out/server/run.first.log"
		mv "$sd" "$out/server/data/debug.first.txt"
		# (the first joiner gone: the second is on its machine, its port)
		kill "$join_pid" 2>/dev/null; wait "$join_pid" 2>/dev/null
		run_server server
		code2=$(server_code "$sl")
		echo "the restarted server's code: $code2"
		run_copy joiner2 "$join_machine" "$vita" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-code:$code2 \
			HALO_NETWORK_TEST_RETRY=20 HALO_NET_PLAYER_NAME=Banned HALO_TEST_INPUT=bot:2 HALO_EXIT_AFTER=110
		join2_pid=$last_pid
		for i in $(seq 1 60); do grep -aqE "refusing a machine @ .*: banned" "$sd" 2>/dev/null && break; sleep 1; done
		grep -aqE "refusing a machine @ .*: banned \(bans.txt\)" "$sd" || fail "the restarted server did not refuse the banned joiner"
		console sv_banlist
		console sv_unban 0
		wait $join2_pid 2>/dev/null
		grep -aq "^server: unbanned: " "$sl" || fail "sv_unban did not take the ban out"
		n=$(grep -a "network test: tick" "$out/joiner2/run.log" | grep -a "| playing" | grep -ac "player [0-9]*:")
		echo "the unbanned joiner played $n s"
		[ "$n" -ge 15 ] || fail "the unbanned joiner did not play ($n s)"
		;;
	esac
	if grep -aqiE "segmentation|fatal signal" "$out"/server/run*.log; then
		grep -aiE "segmentation|fatal signal" "$out"/server/run*.log | head -3
		fail "the server crashed"
	fi
	;;
*)
	echo "usage: $0 code|relay|lobby|lobbypw|menus|menuspw|menushost|lan|pc|pchost|adhoc|many|solo|coop|coopmenu|coopmenuonline|busyport|dedicated|dedicatedpc|dedicatedban|dedicatedcoop" >&2
	exit 2
	;;
esac
# (a relay was there for every online copy: a direct path must still have been
# taken, but in relay mode)
if [ "$mode" != relay ]; then
	for log in "$out"/*/run.log; do
		# (a copy of before relays, HALO_TEST_VITA_JOINER: not told)
		grep -aq "Internet play: connected to host" "$log" && grep -aq "Internet play: the relay " "$log" || continue
		grep -aq "Internet play: connected to host .*, directly" "$log" ||
			fail "$(basename "$(dirname "$log")") did not connect to the host directly"
		grep -aqE "Internet play: (connected to .*|.* is now reached) through the relay" "$log" &&
			fail "$(basename "$(dirname "$log")") went through the relay although a direct path was there"
	done
fi
grep -aiE "segmentation|assert|crash|fatal" "$out"/*/run.log | grep -v "assertions" | head -5
[ $status = 0 ] && echo "PASS ($mode)"
echo "logs in $out"
exit $status
