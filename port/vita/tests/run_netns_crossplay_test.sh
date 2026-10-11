#!/bin/bash
# Crossplay against real PC builds (docs/crossplay.md, task T8 and section
# 10): OpenCE, ChupathingyCE and DamnationCE Linux builds of pinned commits
# (build_pc_crossplay.sh), and our Vita-path client (configure.py
# --linux-net-vita, crossplay on: HALO_CROSSPLAY=1), headless, on one Linux
# machine with no internet, in network namespaces as run_netns_online_test.sh
# makes them: each machine behind a NAT router of its own (Linux masquerade),
# or two on one LAN bridge; a namespace standing for the internet holds the
# repository's MQTT broker (mqtt_test_broker.py, MQTT 5 with the 3.1.1
# fallback), its STUN server (stun_test_server.py) and a DNS server that
# answers every name with "no such name" and logs it.
#
# Nothing can leave: the namespaces have no route out, and on top of that
# every router drops (and counts) whatever it would send anywhere but the
# test's internet (198.51.100.0/24, the routers' 10.10.0.0/16), and names
# resolve only through the test's DNS (/etc/resolv.conf, nsswitch.conf and
# hosts are the test's own, in a mount namespace). Every PC copy runs with its
# own brokers.txt (the test's broker), the test's STUN server, the game list
# (halo.milenko.org) and the ChupathingyCE legacy table fetch turned off, no
# UPnP, no update check, no Discord. A run fails if a router dropped anything
# or a copy looked up a name: a default left pointing at the real internet.
# Needs unprivileged user namespaces (unshare -rnm), ip, iptables, python3;
# no root, no display (SDL's dummy drivers, HALO_NULL_RENDERER).
#
#   run_netns_crossplay_test.sh MODE
#
# The harness's own checks (PC builds only; they must pass):
#   pcself      OpenCE host and OpenCE client: on one LAN (system link), and
#               over the internet (the host public, the client from the
#               server browser through the test broker)
#   chupaopence ChupathingyCE host with an OpenCE client and OpenCE host with
#               a ChupathingyCE client on a LAN, and an OpenCE host with a
#               ChupathingyCE client opening its invite link over the internet
#               (ChupathingyCE's network test neither lists its game nor
#               browses, and its host logs only the start of its invite)
#
# Each game (ChupathingyCE's crossplay test's plan): the host plays Slayer to
# XPPC_SCORE (10), its first player killing the last every 5 s and every
# player hitting the next every 2 s; a second client joins 5 s into the game
# and stays; the first leaves at 11 s and a new copy of it comes back at 14
# (with two players the first leaving ended the game, and an OpenCE host
# crashed when the copy coming back joined its postgame: build-185, twice).
# A dedicated server's game: an OpenCE client, then the client under test.
#
# Our Vita-path client joining PC hosts (docs/crossplay.md, phase 1-2 as the
# owner scoped it: our client joins PC games and servers; a Vita never hosts
# on the crossplay wire). Each also runs its control: the same host with an
# OpenCE client, which must pass (else the harness is broken and the run
# fails). EXPECTED TO FAIL until the wire gaps are closed (T2-T7): reported
# as XFAIL with the stage where our client stopped and the log lines that say
# why, exit status 0 (XPPC_STRICT=1: a failure, for when they are the
# acceptance tests; a pass is reported as XPASS: take the mode off
# XPPC_EXPECTED_FAIL)
#   opencelan   OpenCE host, our client on its LAN (system link)
#   opencehost  OpenCE host behind NAT A, public; our client behind NAT B
#               joins it from the server browser, and another opens its
#               invite link (halo://join/..., on the command line)
#   chupahost   ChupathingyCE host (a player's game) on a LAN (over the
#               internet: chupaserver)
#   chupaserver ChupathingyCE's dedicated server (chupathingyce-server,
#               HALO_DEDICATED), an OpenCE client and ours, on a LAN and over
#               the internet (from the server browser)
#   damnserver  DamnationCE's dedicated server (its game binary with
#               HALO_DEDICATED), an OpenCE client and ours, on a LAN and over
#               the internet (by its invite: it lists only on its game list)
#   crossrefuse OpenCE hosts our client must refuse with the right message
#               (section 6, T11): a game of 32 players, a co-op game, a map
#               our client does not have; today it refuses for the Vita line
#               or not at all, so the check is on the message
#   crossmaps   a PC host's map (docs/crossplay.md T3, crossplay_maps.h): OpenCE
#               hosts on LANs of their own, each with our client: Blood Gulch
#               (levels\test\bloodgulch\bloodgulch, version 0: the Xbox map
#               on both, joins and plays), custom_maps\<XPPC_CE_MAP> (its
#               raw checksum) with our client holding the same file and PC
#               maps on (joins and plays), and the same map with our client
#               without it (refused with the map named, joining nothing, no
#               map share query); controls: OpenCE clients on the first two.
#               Needs XPPC_CE_MAP (or HALO_TEST_CE_MAP), a Custom Edition map
#               needing no resource maps here (Invader's pcgulch.map), and
#               XPPC_CE_RESOURCES, a folder with Custom Edition's bitmaps.map,
#               sounds.map and loc.map (OpenCE wants them for any Custom
#               Edition map), both linked into the OpenCE copies' custom_maps
#               folder; else SKIP. Until T4 our
#               client stops at the version (XFAIL); the map check itself
#               runs today against a stand-in host: run_crossplay_maps_test.sh
#   all         every mode above, one after the other (expected failures as
#               XFAIL), and a summary
#
# Not here: crossmixed (crossplay is one switch, HALO_CROSSPLAY: it becomes
# "a crossplay client cannot see or join a legacy Vita host, and a legacy
# one a crossplay host's game, each cleanly": wants T4's wire) and
# crossversion (wants T10's signed table): both report SKIP. dedicatedopence and pcjoinsvita were dropped (Oct 11 2026: our
# server and Vita hosts stay off the crossplay wire).
#
# With XPPC_PROBE=1 the expected-fail modes also run our client with
# HALO_NETWORK_TEST_CROSS_LINE=1 (debug builds: it lists and joins a host on
# the other side of the Vita-only line), to see how far it gets past that
# first check (the version, the join, the settings record ...). Reported,
# never failing.
#
#   XPPC_BUILDS        a folder with build_pc_crossplay.sh's opence/, chupa/
#                      and damnation/ (their BUILD_INFO commits are printed)
#   XPPC_OPENCE, XPPC_CHUPA, XPPC_CHUPA_SERVER, XPPC_DAMNATION  the binaries
#                      instead (default: XPPC_BUILDS's)
#   HALO_TEST_VITA     our Vita-path Linux build (build/linux/halo)
#   HALO_TEST_DATA     a folder with the game's maps folder (Xbox NTSC maps,
#                      ui.map and the multiplayer maps; default ../data2276)
#   XPPC_OUR_ENV       more VAR=value settings for our client (the crossplay
#                      wire's knobs as they land)
#   XPPC_SECONDS       the longest a game runs (240); XPPC_SERVER_SECONDS a
#                      server's run (150); XPPC_SCORE Slayer's score (10)
#   XPPC_START         seconds from a host's setup to its game's start (4, as
#                      ChupathingyCE's crossplay test: an OpenCE host starts no
#                      game once a client has changed team in its lobby)
#   XPPC_CONTROL=0     leave out the expected-fail modes' controls
#   XPPC_MQTT311=1     the broker refuses MQTT 5 (every copy must fall back to
#                      3.1.1)
#   XPPC_EXPECTED_FAIL the expected-fail modes ("opencelan opencehost chupahost
#                      chupaserver damnserver crossrefuse crossmaps")
#   HALO_TEST_OUT      where the logs go (deleted after a pass unless
#                      HALO_TEST_KEEP=1; default a new folder under
#                      ~/halo-test-out)
#   HALO_TEST_CPUS     CPUs for the copies (taskset list, default all)
#
# Pass criteria are ChupathingyCE's crossplay test's (tools/crossplay_test.py,
# CC0, by Milenko), checked by crossplay_check.py: discover, version, join,
# settings, play (both players on both machines), spawn (positions agree
# within 2 world units), kills (every death on the host seen on the client
# within 2 s), client_damage, score (both agree at the game's end), end of
# game, rejoin, stability; and ours before a game: discover, signal (the
# brokers' signalling reached the host), version, join, settings. An
# expected failure is reported at its first failing stage, with the log
# lines that say why; the brokers' topics (hceu: OpenCE's, hcev: ours) are
# listed for the internet modes.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
export HALO_MENUS_FOLDER=${HALO_MENUS_FOLDER:-$root/port/vita/app0/menus/}
expected_fail=${XPPC_EXPECTED_FAIL:-opencelan opencehost chupahost chupaserver damnserver crossrefuse crossmaps}
mode=${1:-}

case $mode in
pcself|chupaopence|opencelan|opencehost|chupahost|chupaserver|damnserver|crossrefuse) ;;
crossmaps)
	ce_map=${XPPC_CE_MAP:-${HALO_TEST_CE_MAP:-}}
	ce_resources=${XPPC_CE_RESOURCES:-}
	[ -n "$ce_map" ] && [ -f "$ce_map" ] ||
		{ echo "SKIP (crossmaps): wants XPPC_CE_MAP (a Custom Edition map that needs no resource maps)"; exit 0; }
	for f in bitmaps sounds loc; do
		[ -f "$ce_resources/$f.map" ] ||
			{ echo "SKIP (crossmaps): wants XPPC_CE_RESOURCES (a folder with Custom Edition's $f.map, for OpenCE)"; exit 0; }
	done ;;
crossmixed)
	echo "SKIP (crossmixed): wants T4's wire switch (HALO_CROSSPLAY=1 speaking OpenCE's wire); then: a crossplay"
	echo "client cannot see or join a legacy Vita host, and a legacy client a crossplay one, each told why"
	exit 0 ;;
crossversion)
	echo "SKIP (crossversion): wants T10's signed crossplay table (HALO_CROSSPLAY_TABLE, a test key)"
	exit 0 ;;
dedicatedopence|pcjoinsvita)
	echo "SKIP ($mode): dropped Oct 11 2026 (our servers and Vita hosts stay off the crossplay wire)"
	exit 0 ;;
all)
	summary=
	status=0
	mkdir -p "$HOME/halo-test-out"
	all_log=$(mktemp "$HOME/halo-test-out/xppc-all.XXXX")
	for m in pcself chupaopence opencelan opencehost chupahost chupaserver damnserver crossrefuse crossmaps crossmixed crossversion; do
		echo "=== $m"
		HALO_TEST_OUT=${HALO_TEST_OUT:+$HALO_TEST_OUT/$m} "$0" "$m" | tee "$all_log"
		rc=${PIPESTATUS[0]}
		verdict=$(grep -aoE "^(PASS|FAIL|XFAIL|XPASS|SKIP|BROKEN) \($m\)" "$all_log" | tail -1)
		stage=$(grep -aoE "^XFAIL \($m\): .*" "$all_log" | tail -1)
		summary="$summary${verdict:-FAIL ($m) (status $rc)}${stage:+ - ${stage#XFAIL ($m): }}"$'\n'
		[ "$rc" = 0 ] || status=1
	done
	rm -f "$all_log"
	echo "=== summary"
	printf '%s' "$summary"
	exit $status ;;
*)
	sed -n '/^#   run_netns_crossplay_test.sh MODE/,/^#   all /p' "$0" | sed 's/^# \{0,1\}//' >&2
	exit 2 ;;
esac

if [ "${XPPC_NETNS_INSIDE:-}" != 1 ]; then
	export XPPC_NETNS_INSIDE=1
	exec unshare -rnm --fork "$0" "$@"
fi

builds=${XPPC_BUILDS:-}
opence=${XPPC_OPENCE:-${builds:+$builds/opence/src/build/linux/halo}}
chupa=${XPPC_CHUPA:-${builds:+$builds/chupa/src/build/linux/halo}}
chupa_server=${XPPC_CHUPA_SERVER:-${builds:+$builds/chupa/src/build/server-x86/chupathingyce-server}}
damnation=${XPPC_DAMNATION:-${builds:+$builds/damnation/src/build/linux/halo}}
vita=${HALO_TEST_VITA:-$root/build/linux/halo}
data=${HALO_TEST_DATA:-$root/../data2276}
seconds=${XPPC_SECONDS:-240}
server_seconds=${XPPC_SERVER_SECONDS:-150}
start_delay=${XPPC_START:-4}
cpus=${HALO_TEST_CPUS:-0-$(($(nproc) - 1))}
# (the network test's timings: ChupathingyCE's crossplay test's)
kill_every=5 shoot_every=2 score_to_win=${XPPC_SCORE:-10} late_at=5 leave_at=11 back_at=14 second_game_play=30 quiet_limit=45 no_game_limit=90

need() { # need NAME PATH: the binary must be there
	[ -n "$2" ] && [ -x "$2" ] || { echo "BROKEN ($mode): no $1 (${2:-unset}): XPPC_BUILDS or its own variable"; exit 2; }
}
need OpenCE "$opence"
case $mode in chupaopence|chupahost) need ChupathingyCE "$chupa" ;; esac
[ "$mode" = chupaserver ] && need "ChupathingyCE's server" "$chupa_server"
[ "$mode" = damnserver ] && need DamnationCE "$damnation"
case $mode in pcself|chupaopence) ;; *) need "our Vita-path build" "$vita" ;; esac
[ -f "$data/maps/bloodgulch.map" ] && [ -f "$data/maps/ui.map" ] ||
	{ echo "BROKEN ($mode): no maps in $data/maps (HALO_TEST_DATA)"; exit 2; }
data=$(cd "$data" && pwd)

out=${HALO_TEST_OUT:-$HOME/halo-test-out/xppc-netns.$mode.$$}
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
status=0
pids=
cleanup() {
	local rc=$?
	# (only what this run started: the pairs' shells, the copies, the servers)
	for pid in $pids; do kill "$pid" 2>/dev/null; done
	wait 2>/dev/null
	test_out_done $rc
}
trap cleanup EXIT
trap 'exit 1' INT TERM

# ---- names: only the test's DNS, which knows none
cat > "$out/resolv.conf" <<'EOF'
nameserver 198.51.100.1
options timeout:1 attempts:1
EOF
printf 'hosts: files dns\n' > "$out/nsswitch.conf"
printf '127.0.0.1 localhost\n::1 localhost\n' > "$out/hosts"
for f in resolv.conf nsswitch.conf hosts; do
	mount --bind "$out/$f" "/etc/$f" || { echo "BROKEN ($mode): cannot bind /etc/$f"; exit 2; }
done

# ---- the network (run_netns_online_test.sh's)
holder() {
	unshare -n sleep 100000 > /dev/null 2>&1 & held=$!; pids="$pids $held"
	while [ "$(readlink /proc/$held/ns/net)" = "$(readlink /proc/self/ns/net)" ]; do sleep 0.02; done
}
in_ns() { local pid=$1; shift; nsenter -t "$pid" -n "$@"; }
ip link set lo up
ip addr add 198.51.100.1/24 dev lo
sysctl -qw net.ipv4.ip_forward=1
routers=
side() { # side N -> side_router side_machine: a router (10.10.N.2) and its LAN 192.168.N.0/24 (machine .2)
	local n=$1 router machine
	holder; router=$held; holder; machine=$held
	ip link add "o_$n" type veth peer name "w_$n"
	ip link set "w_$n" netns "$router"
	ip addr add "10.10.$n.1/24" dev "o_$n"; ip link set "o_$n" up
	in_ns "$router" ip link set lo up
	in_ns "$router" ip addr add "10.10.$n.2/24" dev "w_$n"
	in_ns "$router" ip link set "w_$n" up
	in_ns "$router" ip route add default via "10.10.$n.1"
	in_ns "$router" ip link add name "b_$n" type bridge
	in_ns "$router" ip addr add "192.168.$n.1/24" broadcast "192.168.$n.255" dev "b_$n"
	in_ns "$router" ip link set "b_$n" up
	in_ns "$router" sysctl -qw net.ipv4.ip_forward=1
	in_ns "$router" iptables -t nat -A POSTROUTING -o "w_$n" -j MASQUERADE
	in_ns "$router" iptables -A INPUT -i "w_$n" -p udp -m conntrack --ctstate NEW -j DROP
	# (the leak guard: anything for elsewhere than the test's internet dropped
	# and counted)
	in_ns "$router" iptables -A FORWARD -o "w_$n" -d 198.51.100.0/24 -j ACCEPT
	in_ns "$router" iptables -A FORWARD -o "w_$n" -d 10.10.0.0/16 -j ACCEPT
	# (by kind, so that a leak says what it was)
	local leak
	# (a peer's LAN address, which internet play tries too (its host
	# candidates): the test's own, never routed past its internet, and not a
	# leak; counted apart)
	in_ns "$router" iptables -A FORWARD -o "w_$n" -d 192.168.0.0/16 -m comment --comment "peer-lan-candidate" -j DROP
	for leak in "tunnel-virtual-100.64/10:-d 100.64.0.0/10" "dns:-p udp --dport 53" "stun-3478:-p udp --dport 3478" \
		"stun-19302:-p udp --dport 19302" "mqtt-1883:-p tcp --dport 1883" "https-443:-p tcp --dport 443" \
		"http-80:-p tcp --dport 80" "ssdp-1900:-p udp --dport 1900" "other-tcp:-p tcp" "other-udp:-p udp" "other:"; do
		in_ns "$router" iptables -A FORWARD -o "w_$n" ${leak#*:} -m comment --comment "leak-${leak%%:*}" -j DROP
	done
	routers="$routers $router:$n"
	side_router=$router
	lan_machine "$n" 2; side_machine=$held
}
lan_machine() { # lan_machine N HOST: a machine 192.168.N.HOST on side N's LAN -> held
	local n=$1 h=$2 router
	router=$(for r in $routers; do [ "${r#*:}" = "$n" ] && echo "${r%%:*}"; done)
	holder
	in_ns "$router" ip link add "l${n}_$h" type veth peer name "m${n}_$h"
	in_ns "$router" ip link set "m${n}_$h" netns "$held"
	in_ns "$router" ip link set "l${n}_$h" master "b_$n"
	in_ns "$router" ip link set "l${n}_$h" up
	in_ns "$held" ip link set lo up
	in_ns "$held" ip addr add "192.168.$n.$h/24" broadcast "192.168.$n.255" dev "m${n}_$h"
	in_ns "$held" ip link set "m${n}_$h" up
	in_ns "$held" ip route add default via "192.168.$n.1"
}

python3 "$here/stun_test_server.py" --host 198.51.100.1 --port 3478 > "$out/stun.log" 2>&1 & pids="$pids $!"
# the DNS: every question answered "no such name", and logged
python3 -I - > "$out/dns.log" 2>&1 <<'PYTHON' & pids="$pids $!"
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("198.51.100.1", 53))
while True:
    query, sender = s.recvfrom(4096)
    if len(query) < 12:
        continue
    name, i = [], 12
    while i < len(query) and query[i]:
        name.append(query[i + 1:i + 1 + query[i]].decode("ascii", "replace"))
        i += 1 + query[i]
    print("dns: %s from %s" % (".".join(name), sender[0]), flush=True)
    # (the question back, flagged an answer with "no such name")
    reply = query[:2] + bytes([0x81, 0x83]) + query[4:6] + b"\0\0\0\0\0\0" + query[12:i + 5]
    s.sendto(reply, sender)
PYTHON
brokers=0
broker() { # broker -> broker_address: a broker of its own (198.51.100.10+), for one pair
	broker_address=198.51.100.$((10 + brokers))
	ip addr add "$broker_address/32" dev lo
	python3 "$here/mqtt_test_broker.py" --host "$broker_address" --port 1883 \
		$([ "${XPPC_MQTT311:-0}" = 1 ] && echo --mqtt311) > "$out/broker-$broker_address.log" 2>&1 & pids="$pids $!"
	brokers=$((brokers + 1))
}

# the builds' commits
for b in opence chupa damnation; do
	[ -n "$builds" ] && [ -f "$builds/$b/BUILD_INFO" ] &&
		echo "$b: $(sed -n 's/^describe: //p' "$builds/$b/BUILD_INFO") $(sed -n 's/^commit: //p' "$builds/$b/BUILD_INFO")"
done | tee "$out/builds.txt"
echo "ours: $vita ($(git -C "$root" rev-parse --short HEAD 2>/dev/null))" | tee -a "$out/builds.txt"

# ---- the copies
# run_copy NAME NETNS KIND ADDRESS BROKER [ENV...] -> last_pid: a copy in its
# own folder ($out/NAME: run.log, data/debug.txt, save, home, bin)
run_copy() {
	local name=$1 ns=$2 kind=$3 address=$4 broker=$5 binary dir=$out/$1
	shift 5
	mkdir -p "$dir/data" "$dir/save" "$dir/home" "$dir/bin"
	ln -sfn "${copy_maps:-$data/maps}" "$dir/data/maps"
	# (crossmaps: a PC copy's Custom Edition maps, beside its maps folder)
	[ -n "${copy_custom_maps:-}" ] && [ "$kind" != ours ] && ln -sfn "$copy_custom_maps" "$dir/data/custom_maps"
	# (playlists for the dedicated servers)
	printf 'bloodgulch slayer\n' > "$dir/data/xp-playlist.txt"
	printf '%s:1883\n' "$broker" > "$dir/bin/brokers.txt"
	local common="HOME=$dir/home XDG_CONFIG_HOME=$dir/home/.config XDG_DATA_HOME=$dir/home/.local/share
		XDG_CACHE_HOME=$dir/home/.cache HALO_DATA_ROOT=$dir/data HALO_SAVE_ROOT=$dir/save HALO_NO_AUDIO=1
		HALO_NO_VSYNC=1 HALO_UPDATE_AUTO=false HALO_UPDATE_CHECK=false HALO_DISCORD_APPLICATION= HALO_CRASH_REPORTS=no
		HALO_NET_ALLOW_UPNP=false HALO_NET_JOIN_FROM_CLIPBOARD=false HALO_NET_STUN=198.51.100.1:3478
		HALO_NET_BROKERS_FILE=$dir/bin/brokers.txt"
	local own
	case $kind in
	ours)
		# (a copy of the binary of its own too: it writes config.toml beside
		# itself, which copies running at once would share, in the build)
		cp "$vita" "$dir/bin/halo"
		binary=$dir/bin/halo
		own="SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_FRAME_CAP=30
			HALO_TICK_THREAD=1 HALO_EXIT_AFTER=$run_seconds HALO_NET_BROKERS=$broker:1883 HALO_NET_RELAYS=198.51.100.1:47320
			HALO_NET_ALLOW_RELAY=false HALO_CROSSPLAY=1 ${XPPC_OUR_ENV:-}" ;;
	*)
		case $kind in
		opence) binary=$opence ;;
		chupa) binary=$chupa ;;
		chupaserver) binary=$chupa_server ;;
		damnation) binary=$damnation ;;
		esac
		# (a copy of the binary of its own: config.toml is the binary's folder's)
		cp "$binary" "$dir/bin/halo"
		binary=$dir/bin/halo
		own="SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
			HALO_NULL_RENDERER=1 HALO_HIDDEN_WINDOW=1 HALO_NET_BROWSER=
			HALO_NET_LIST_GAMES=false HALO_NET_REPORT_GAMES=false HALO_NET_REPORT_EVENTS=false
			HALO_LEGACY_TABLE=$dir/no-legacy-table.json HALO_DEDICATED_LINK=false HALO_DEDICATED_CONSOLE=false" ;;
	esac
	(cd "$dir/data" && exec nsenter -t "$ns" -n env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY $common $own "$@" \
		taskset -c "$cpus" timeout -k 5 "$run_seconds" "$binary" ${copy_args:-} > "$dir/run.log" 2>&1) &
	last_pid=$!
}
# host_env KIND TRANSPORT: the settings of a host of that kind
host_env() {
	local kind=$1 transport=$2
	local online=false public=false
	[ "$transport" != lan ] && online=true public=true
	case $kind in
	chupaserver|damnation-server)
		echo "HALO_NET_ONLINE=$online HALO_NET_HOST_PUBLIC=$public HALO_DEDICATED=xp-playlist.txt
			HALO_DEDICATED_NAME=XpServer HALO_DEDICATED_MINIMUM_PLAYERS=1 HALO_DEDICATED_PUBLIC=$public" ;;
	*)
		echo "HALO_NET_ONLINE=$online HALO_NET_HOST_PUBLIC=$public HALO_NETWORK_TEST_PUBLIC=$public
			HALO_NETWORK_TEST=host:${host_game:-bloodgulch}:${host_variants:-slayer,ctf}
			HALO_NETWORK_TEST_START=$start_delay HALO_NETWORK_TEST_KILL=$kill_every
			HALO_NETWORK_TEST_SHOOT=$shoot_every HALO_NETWORK_TEST_SCORE=$score_to_win HALO_NET_LOBBY_NAME=XpHost
			${host_extra:-}" ;;
	esac
}
# client_env KIND TRANSPORT BOT
client_env() {
	local kind=$1 transport=$2 bot=$3
	if [ "$transport" = lan ]; then
		echo "HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_TEST_INPUT=bot:$bot HALO_NETWORK_TEST_SHOOT=$shoot_every"
	elif [ "$transport" = invite ]; then
		# (the host's invite link on the command line, as a browser or the
		# desktop opens one; then the game it leads to, which the tunnel lists
		# under System Link, joined)
		echo "HALO_NET_ONLINE=true HALO_NETWORK_TEST=join HALO_NET_HOST_PUBLIC=false HALO_TEST_INPUT=bot:$bot
			HALO_NETWORK_TEST_SHOOT=$shoot_every"
	elif [ "$kind" = ours ]; then
		echo "HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-public HALO_NET_HOST_PUBLIC=false HALO_TEST_INPUT=bot:$bot
			HALO_NETWORK_TEST_SHOOT=$shoot_every"
	else
		echo "HALO_NET_ONLINE=true HALO_NETWORK_TEST=browse HALO_NET_HOST_PUBLIC=false HALO_TEST_INPUT=bot:$bot
			HALO_NETWORK_TEST_SHOOT=$shoot_every"
	fi
}
# the seconds of play a copy has logged in its first game, and its games
game_seconds() { # game_seconds LOG GAME(1, 2): seconds logged in that game
	awk -v want="$2" '/network test: tick / { split($0, a, "network test: tick "); split(a[2], b, " "); t = b[1] + 0;
		if (last == "" || t < last) g++; last = t; if (g == want) n++ } END { print n + 0 }' "$1" 2>/dev/null || echo 0
}
games_played() {
	awk '/network test: tick / { split($0, a, "network test: tick "); split(a[2], b, " "); t = b[1] + 0;
		if (last == "" || t < last) g++; last = t } END { print g + 0 }' "$1" 2>/dev/null || echo 0
}

pairs=0
subnet=0
# pair NAME TRANSPORT HOSTKIND CLIENTKIND [CLIENTENV]: a host and its client
# (and the copy that comes back after the first leaves), in the background;
# checked by check_pair. A server's: SERVERKIND and two clients, the first
# an OpenCE one (pair_server)
pair() {
	local name=$1 transport=$2 hostkind=$3 clientkind=$4 extra=${5:-}
	local host_ns client_ns late_ns host_ip client_ip late_ip broker_address=198.51.100.1 n
	subnet=$((subnet + 1)); n=$subnet
	side "$n"; host_ns=$side_machine; host_ip=192.168.$n.2
	if [ "$transport" = lan ]; then
		lan_machine "$n" 3; client_ns=$held; client_ip=192.168.$n.3
		lan_machine "$n" 4; late_ns=$held; late_ip=192.168.$n.4
	else
		broker
		subnet=$((subnet + 1)); side "$subnet"; client_ns=$side_machine; client_ip=192.168.$subnet.2
		subnet=$((subnet + 1)); side "$subnet"; late_ns=$side_machine; late_ip=192.168.$subnet.2
	fi
	mkdir -p "$out/$name"
	echo "$name: $hostkind host, $clientkind client, $transport" | tee "$out/$name/what"
	(
		run_seconds=$seconds
		trap 'kill $mine 2>/dev/null; wait 2>/dev/null; exit 0' TERM
		run_copy "$name/host" "$host_ns" "$hostkind" "$host_ip" "$broker_address" $(host_env "$hostkind" "$transport")
		mine=$last_pid; host_pid=$last_pid
		# (the client once the host's own player is in: else the client's is
		# player 0, the network test's "first player" whose kills score, and
		# with its leaving the scores are spread and the game never ends)
		for i in $(seq 1 60); do
			grep -aqE "network test: (player 0 is named|starting the game)" "$out/$name/host/run.log" 2>/dev/null && break
			sleep 1
		done
		invite=
		if [ "$transport" = invite ]; then
			for i in $(seq 1 60); do
				invite=$(grep -aoE 'halo://join/[0-9a-f]{64,}' "$out/$name/host/run.log" 2>/dev/null | head -1)
				[ -n "$invite" ] && break
				sleep 1
			done
			echo "$name: the host's invite: ${invite:-none in its log}"
		fi
		copy_args=$invite copy_maps=${client_maps:-} run_copy "$name/client" "$client_ns" "$clientkind" "$client_ip" "$broker_address" \
			$(client_env "$clientkind" "$transport" $((17 + pairs))) $extra ${client_extra:-}
		mine="$mine $last_pid"; client_pid=$last_pid
		back_pid= late_pid= started=$SECONDS seen= seen_at=$SECONDS
		while kill -0 "$host_pid" 2>/dev/null; do
			sleep 1
			first=$(game_seconds "$out/$name/host/run.log" 1)
			games=$(games_played "$out/$name/host/run.log")
			now=$((SECONDS - started))
			[ "$first.$games.$(game_seconds "$out/$name/host/run.log" 2)" != "$seen" ] &&
				{ seen="$first.$games.$(game_seconds "$out/$name/host/run.log" 2)"; seen_at=$SECONDS; }
			# (a second client joins 5 s into the first game and stays: with
			# two players, the first leaving would end the game, and an OpenCE
			# host crashed when the copy coming back joined its postgame)
			if [ -z "$late_pid" ] && [ "$first" -ge "$late_at" ]; then
				copy_args=$invite copy_maps=${client_maps:-} run_copy "$name/late" "$late_ns" "$clientkind" "$late_ip" "$broker_address" \
					$(client_env "$clientkind" "$transport" $((29 + pairs))) $extra ${client_extra:-}
				mine="$mine $last_pid"; late_pid=$last_pid
			fi
			# (the client leaves 11 s into the first game, if it is in it, and
			# a new copy comes back at 14)
			if [ -n "$client_pid" ] && [ "$first" -ge "$leave_at" ] && [ -z "$back_pid" ] &&
				[ "$(games_played "$out/$name/client/run.log")" -ge 1 ]; then
				echo "$name: the client leaves $first s into game 1"
				kill "$client_pid" 2>/dev/null; wait "$client_pid" 2>/dev/null; client_pid=
				while [ "$(game_seconds "$out/$name/host/run.log" 1)" -lt "$back_at" ] && kill -0 "$host_pid" 2>/dev/null; do
					sleep 1
				done
				copy_args=$invite copy_maps=${client_maps:-} run_copy "$name/back" "$client_ns" "$clientkind" "$client_ip" "$broker_address" \
					$(client_env "$clientkind" "$transport" $((37 + pairs))) $extra ${client_extra:-}
				mine="$mine $last_pid"; back_pid=$last_pid
			fi
			{ [ "$games" -ge 2 ] && [ "$(game_seconds "$out/$name/host/run.log" 2)" -ge "$second_game_play" ]; } && break
			{ [ "$games" -ge 1 ] && [ $((SECONDS - seen_at)) -ge "$quiet_limit" ]; } && break
			{ [ "$games" = 0 ] && [ "$now" -ge "$no_game_limit" ]; } && break
		done
		kill $mine 2>/dev/null; wait 2>/dev/null
	) > "$out/$name/pair.log" 2>&1 &
	pids="$pids $!"
	pair_pids="${pair_pids:-} $!"
	pairs=$((pairs + 1))
}
pair_server() { # pair_server NAME TRANSPORT SERVERKIND SECONDCLIENTKIND [CLIENTENV]
	local name=$1 transport=$2 serverkind=$3 clientkind=$4 extra=${5:-}
	local server_ns c1_ns c2_ns server_ip c1_ip c2_ip broker_address=198.51.100.1 n
	subnet=$((subnet + 1)); n=$subnet
	side "$n"; server_ns=$side_machine; server_ip=192.168.$n.2
	if [ "$transport" = lan ]; then
		lan_machine "$n" 3; c1_ns=$held; c1_ip=192.168.$n.3
		lan_machine "$n" 4; c2_ns=$held; c2_ip=192.168.$n.4
	else
		broker
		subnet=$((subnet + 1)); side "$subnet"; c1_ns=$side_machine; c1_ip=192.168.$subnet.2
		subnet=$((subnet + 1)); side "$subnet"; c2_ns=$side_machine; c2_ip=192.168.$subnet.2
	fi
	mkdir -p "$out/$name"
	echo "$name: $serverkind, an OpenCE client and a $clientkind one, $transport" | tee "$out/$name/what"
	(
		run_seconds=$server_seconds
		trap 'kill $mine 2>/dev/null; wait 2>/dev/null; exit 0' TERM
		local kind=$serverkind
		[ "$serverkind" = damnserver ] && kind=damnation
		run_copy "$name/host" "$server_ns" "$kind" "$server_ip" "$broker_address" \
			$(host_env "$([ "$serverkind" = damnserver ] && echo damnation-server || echo chupaserver)" "$transport")
		mine=$last_pid
		sleep 6
		invite=
		if [ "$transport" = invite ]; then
			for i in $(seq 1 60); do
				invite=$(grep -aoE 'halo://join/[0-9a-f]{64,}' "$out/$name/host/run.log" 2>/dev/null | head -1)
				[ -n "$invite" ] && break
				sleep 1
			done
			echo "$name: the server's invite: ${invite:-none in its log}"
		fi
		copy_args=$invite run_copy "$name/client" "$c1_ns" opence "$c1_ip" "$broker_address" $(client_env opence "$transport" $((41 + pairs)))
		mine="$mine $last_pid"
		sleep 4
		copy_args=$invite run_copy "$name/client2" "$c2_ns" "$clientkind" "$c2_ip" "$broker_address" \
			$(client_env "$clientkind" "$transport" $((43 + pairs))) $extra
		mine="$mine $last_pid"
		wait
	) > "$out/$name/pair.log" 2>&1 &
	pids="$pids $!"
	pair_pids="${pair_pids:-} $!"
	pairs=$((pairs + 1))
}
wait_pairs() { for p in $pair_pids; do wait "$p" 2>/dev/null; done; pair_pids=; }

# check_pair NAME [server]: crossplay_check.py's steps; 0 every step passed.
# Its stage (the first failing one) in check_stage
check_pair() {
	local name=$1 kind=${2:-direction} args rc
	args="--host $out/$name/host --client $out/$name/client"
	if [ "$kind" = server ]; then
		args="$args --client $out/$name/client2"
	elif [ -d "$out/$name/back" ] && [ -d "$out/$name/late" ]; then
		args="$args --client $out/$name/late --client $out/$name/back --rejoin"
	elif [ -d "$out/$name/late" ]; then
		args="$args --client $out/$name/late"
	fi
	python3 "$here/crossplay_check.py" "$kind" "$name" $args --score "$score_to_win" --json "$out/$name/check.json" \
		> "$out/$name/check.txt"
	rc=$?
	cat "$out/$name/check.txt"
	check_stage=$(sed -n "s/^  $name stage: //p" "$out/$name/check.txt")
	return $rc
}
# what our client did, from the logs: the broker topics it and the host used
broker_topics() { # broker_topics NAME: the topic families each copy published on
	local log
	for log in "$out"/broker-*.log; do
		[ -f "$log" ] || continue
		awk '$1 == "publish" || $1 == "subscribe" { topic = ($3 == "(will)") ? $4 : $3; split(topic, part, "/");
			print $1, part[1] "/" part[2] "/" (part[3] == "lobby" ? "lobby/..." : "<key>"), $2 }' "$log" |
			sed 's/ \(hce[uv]\)-[0-9a-f]*$/ by \1 client/' | sort | uniq -c | sed "s|^|  $(basename "$log" .log): |"
	done
}

# ---- the modes
our_results=  # "NAME:STAGE" of our client's runs
controls_ok=1
run_our_modes() { # run_our_modes: the pairs are started; checks them
	wait_pairs
	local name
	for name in $control_names; do
		echo "--- control $name"
		if ! check_pair "$name" $([ -f "$out/$name/server" ] && echo server); then
			controls_ok=0
			echo "BROKEN ($mode): the control $name failed (stage: ${check_stage:-?}): the harness, not our client"
		fi
	done
	for name in $test_names; do
		echo "--- $name"
		if check_pair "$name" $([ -f "$out/$name/server" ] && echo server); then
			our_results="$our_results $name:pass"
		else
			our_results="$our_results $name:${check_stage:-?}"
		fi
	done
	for name in $probe_names; do
		echo "--- probe $name (HALO_NETWORK_TEST_CROSS_LINE=1: past the Vita-only line)"
		check_pair "$name" $([ -f "$out/$name/server" ] && echo server) > /dev/null
		echo "probe $name: got to ${check_stage:-every step passed}"
		grep -a "evidence" "$out/$name/check.txt" | head -8
	done
}
control_names= test_names= probe_names=
control() { [ "${XPPC_CONTROL:-1}" = 1 ] && control_names="$control_names $1"; [ "${XPPC_CONTROL:-1}" = 1 ]; }
probe() { [ "${XPPC_PROBE:-0}" = 1 ] && probe_names="$probe_names $1"; [ "${XPPC_PROBE:-0}" = 1 ]; }
cross_line="HALO_NETWORK_TEST_CROSS_LINE=1"

case $mode in
pcself)
	pair lan-opence-opence lan opence opence
	pair net-opence-opence net opence opence
	wait_pairs
	for name in lan-opence-opence net-opence-opence; do
		echo "--- $name"; check_pair "$name" || status=1
	done
	;;
chupaopence)
	# (over the internet one way: ChupathingyCE's network test neither lists
	# its host's game nor browses, and its host logs only the start of its
	# invite; so an OpenCE host with a ChupathingyCE client opening its invite.
	# ChupathingyCE's server, which lists and logs its invite whole, is
	# chupaserver's)
	pair lan-chupa-opence lan chupa opence
	pair lan-opence-chupa lan opence chupa
	pair invite-opence-chupa invite opence chupa
	wait_pairs
	for name in lan-chupa-opence lan-opence-chupa invite-opence-chupa; do
		echo "--- $name"; check_pair "$name" || status=1
	done
	;;
opencelan)
	control lan-opence-opence && pair lan-opence-opence lan opence opence
	test_names=lan-opence-ours; pair lan-opence-ours lan opence ours
	probe lan-opence-ours-probe && pair lan-opence-ours-probe lan opence ours "$cross_line"
	run_our_modes
	;;
opencehost)
	control net-opence-opence && pair net-opence-opence net opence opence
	test_names="net-opence-ours invite-opence-ours"
	pair net-opence-ours net opence ours
	pair invite-opence-ours invite opence ours
	probe net-opence-ours-probe && pair net-opence-ours-probe net opence ours "$cross_line"
	run_our_modes
	echo "--- the brokers: what each kind of client published and subscribed to (hceu: OpenCE's topics, hcev: ours)"; broker_topics
	;;
chupahost)
	# (a player's ChupathingyCE game on a LAN only: its network test neither
	# lists the game nor logs its invite whole; over the internet see
	# chupaserver)
	control lan-chupa-opence && pair lan-chupa-opence lan chupa opence
	test_names="lan-chupa-ours"
	pair lan-chupa-ours lan chupa ours
	probe lan-chupa-ours-probe && pair lan-chupa-ours-probe lan chupa ours "$cross_line"
	run_our_modes
	echo "--- the brokers' topics"; broker_topics
	;;
chupaserver|damnserver)
	kind=$mode
	# (over the internet: ChupathingyCE's server is listed in the server
	# browser (HALO_DEDICATED_PUBLIC); DamnationCE's only on its game list
	# (halo.milenko.org, off here), so its clients open its invite)
	internet=net
	[ "$mode" = damnserver ] && internet=invite
	for transport in lan $internet; do
		if control "$transport-$mode-opence"; then
			pair_server "$transport-$mode-opence" "$transport" "$kind" opence; : > "$out/$transport-$mode-opence/server"
		fi
		test_names="$test_names $transport-$mode-ours"
		pair_server "$transport-$mode-ours" "$transport" "$kind" ours; : > "$out/$transport-$mode-ours/server"
	done
	if probe "lan-$mode-ours-probe"; then
		pair_server "lan-$mode-ours-probe" lan "$kind" ours "$cross_line"; : > "$out/lan-$mode-ours-probe/server"
	fi
	run_our_modes
	echo "--- the brokers' topics"; broker_topics
	;;
crossrefuse)
	# three OpenCE hosts on LANs of their own, each with our client: a game
	# bigger than a Vita takes (an OpenCE host's fast setup, as its network
	# test hosts, takes 128 players: HALO_PORT_MAXIMUM_NETWORK_PLAYERS;
	# OpenCE has no setting for fewer), a co-op game (a10), and a map our
	# client lacks (wizard: its maps folder without it). The right refusals
	# (section 6, T11's messages, to be written) are looked for loosely: the
	# reason named
	pair refuse-big lan opence ours
	host_game='levels\a10\a10' host_variants=coop pair refuse-coop lan opence ours
	mkdir -p "$out/maps-nowizard"
	for f in "$data"/maps/*.map; do
		[ "$(basename "$f")" = wizard.map ] || ln -sfn "$f" "$out/maps-nowizard/$(basename "$f")"
	done
	client_maps=$out/maps-nowizard host_game=wizard pair refuse-map lan opence ours
	wait_pairs
	refused=0
	for name in refuse-big refuse-coop refuse-map; do
		case $name in
		refuse-big) want='too (big|many players)|more than 16 players|PC game too big' ;;
		refuse-coop) want='co-?op.*(PC|crossplay|refus|cannot)|(refus|cannot).*co-?op' ;;
		refuse-map) want='(missing|do not have|don.t have|no map|couldn.t (find|load)).*(wizard|Wizard)' ;;
		esac
		got=$(grep -ahiE "$want" "$out/$name/client/run.log" "$out/$name/client/data/debug.txt" 2>/dev/null |
			grep -v "/" | head -2)
		joined=$(grep -ac "network test: joining" "$out/$name/client/run.log" 2>/dev/null)
		said=$(grep -ahiE "not a Vita|Vita-only|cannot join|refus|not joining" "$out/$name/client/run.log" "$out/$name/client/data/debug.txt" \
			2>/dev/null | grep -v "/" | sed 's/^[0-9.]* [0-9:]*  //; s/^halo-linux: //' | sort -u | head -3)
		echo "--- $name: our client joined ${joined:-0} times; the right refusal: ${got:-none}"
		[ -n "$said" ] && echo "$said" | sed 's/^/  it said: /'
		if [ -n "$got" ] && [ "${joined:-0}" = 0 ]; then
			our_results="$our_results $name:pass"
		elif [ "${joined:-0}" != 0 ]; then
			our_results="$our_results $name:joined-instead-of-refusing"
		else
			# (no spaces: the stages are a list of words)
			stage=refusal-message
			grep -aqiE "not joining a host of network version" "$out/$name/client/data/debug.txt" "$out/$name/client/run.log" \
				2>/dev/null && stage="$stage(refused-for-the-network-version-instead)"
			grep -aqiE "not a Vita|Vita-only" "$out/$name/client/run.log" "$out/$name/client/data/debug.txt" 2>/dev/null &&
				stage="$stage(and-as-not-a-Vita-while-the-host-was-in-its-lobby)"
			our_results="$our_results $name:$stage"
		fi
		# (the host must have hosted, or the check proves nothing)
		grep -aq "network test: hosting" "$out/$name/host/run.log" ||
			{ controls_ok=0; echo "BROKEN ($mode): the OpenCE host of $name never hosted"; }
	done
	;;
crossmaps)
	# three OpenCE hosts on LANs of their own (docs/crossplay.md T3): Blood
	# Gulch (the Xbox map both sides have), and a Custom Edition map as OpenCE
	# names it (custom_maps\<name>, its raw checksum) with our client holding
	# the same file, then lacking it. The OpenCE copies have the map in their
	# custom_maps folder; ours in its maps folder, with PC maps on
	ce_name=$(basename "$ce_map" .map)
	mkdir -p "$out/custom_maps" "$out/maps-with-ce"
	ln -sfn "$(readlink -f "$ce_map")" "$out/custom_maps/$ce_name.map"
	for f in bitmaps sounds loc; do ln -sfn "$(readlink -f "$ce_resources/$f.map")" "$out/custom_maps/$f.map"; done
	for f in "$data"/maps/*.map; do ln -sfn "$f" "$out/maps-with-ce/$(basename "$f")"; done
	ln -sfn "$(readlink -f "$ce_map")" "$out/maps-with-ce/$ce_name.map"
	export copy_custom_maps=$out/custom_maps
	control xmap-stock-opence && pair xmap-stock-opence lan opence opence
	control xmap-ce-opence && host_game="custom_maps\\$ce_name" host_variants=slayer pair xmap-ce-opence lan opence opence
	test_names="xmap-stock-ours xmap-ce-ours"
	host_variants=slayer pair xmap-stock-ours lan opence ours
	client_maps=$out/maps-with-ce client_extra=HALO_CUSTOM_EDITION=1 host_game="custom_maps\\$ce_name" host_variants=slayer \
		pair xmap-ce-ours lan opence ours
	client_extra=HALO_CUSTOM_EDITION=1 host_game="custom_maps\\$ce_name" host_variants=slayer pair xmap-cemissing-ours lan opence ours
	run_our_modes
	# (the map our client lacks: refused, the map named, never asked of the
	# host, never joined)
	name=xmap-cemissing-ours
	want="The PC host is playing the map $ce_name, which isn't in your maps folder"
	got=$(grep -ahF "$want" "$out/$name/client/run.log" "$out/$name/client/data/debug.txt" 2>/dev/null | head -1)
	# (ticks played after the host's settings named its map: its game)
	joined=$(sed -n "/join: the host's map is custom_maps.$ce_name /,\$ p" "$out/$name/client/run.log" 2>/dev/null |
		grep -ac "network test: tick [0-9]* ")
	asked=$(grep -acE "map share: asking the host|asking the host for its map" "$out/$name/client/run.log" 2>/dev/null)
	echo "--- $name: our client played ${joined:-0} ticks, asked the host for the map ${asked:-0} times; the refusal: ${got:-none}"
	if [ "${asked:-0}" != 0 ]; then
		our_results="$our_results $name:asked-a-PC-host-for-its-map"
	elif [ -n "$got" ] && [ "${joined:-0}" = 0 ]; then
		our_results="$our_results $name:pass"
	elif [ "${joined:-0}" != 0 ]; then
		our_results="$our_results $name:joined-instead-of-refusing"
	else
		stage=refusal-message
		grep -aqiE "not joining a host of network version" "$out/$name/client/data/debug.txt" "$out/$name/client/run.log" \
			2>/dev/null && stage="$stage(refused-for-the-network-version-instead)"
		our_results="$our_results $name:$stage"
	fi
	grep -aq "network test: hosting" "$out/$name/host/run.log" && ! grep -aq "map missing" "$out/$name/host/run.log" ||
		{ controls_ok=0; echo "BROKEN ($mode): the OpenCE host of $name never hosted its map"; }
	;;
esac

# ---- nothing left the test's network
echo "--- leaks"
leaked=0
for r in $routers; do
	count=$(in_ns "${r%%:*}" iptables -L FORWARD -v -n -x 2>/dev/null |
		awk '$3 == "DROP" && !/peer-lan-candidate/ { s += $1 } END { print s + 0 }')
	candidates=$(in_ns "${r%%:*}" iptables -L FORWARD -v -n -x 2>/dev/null | awk '/peer-lan-candidate/ { print $1 }')
	[ "${candidates:-0}" != 0 ] &&
		echo "router 10.10.${r#*:}.2: $candidates datagrams to a peer's LAN address (internet play's host candidates; dropped, not a leak)"
	if [ "$count" != 0 ]; then
		echo "router 10.10.${r#*:}.2 dropped $count packets bound outside the test's internet:" \
			$(in_ns "${r%%:*}" iptables -L FORWARD -v -n -x | awk '$3 == "DROP" && $1 > 0 && !/peer-lan-candidate/ {
				for (i = 1; i <= NF; i++) if ($i ~ /^leak-/) printf "%s %s; ", $1, substr($i, 6) }')
		leaked=1
	fi
done
if [ -s "$out/dns.log" ]; then
	echo "names looked up (answered: no such name):"; sort "$out/dns.log" | uniq -c | head -20
	leaked=1
fi
mqtt=$(cat "$out"/broker-*.log 2>/dev/null | grep -ao 'connect [^ ]* mqtt[0-9]*' | awk '{ print $3 }' | sort | uniq -c | tr '\n' ' ')
[ -n "$mqtt" ] && echo "broker connections by protocol: $mqtt(mqtt5, or mqtt4 = 3.1.1)"
if [ "$leaked" = 1 ]; then
	echo "FAIL ($mode): a copy reached for something outside the test's network (a default not overridden)"
	status=1
else
	echo "no packet left the test's network, no name was looked up"
fi

# ---- the verdict
case $mode in
pcself|chupaopence)
	[ $status = 0 ] && echo "PASS ($mode)" || echo "FAIL ($mode)"
	;;
*)
	failed=
	for r in $our_results; do [ "${r#*:}" = pass ] || failed="$failed $r"; done
	stages=$(for r in $our_results; do printf '%s ' "${r%%:*} stopped at ${r#*:};"; done)
	xfail=0
	case " $expected_fail " in *" $mode "*) [ "${XPPC_STRICT:-0}" = 1 ] || xfail=1 ;; esac
	if [ "$controls_ok" = 0 ]; then
		echo "BROKEN ($mode): a control failed, so our client's results say nothing: $stages"
		status=1
	elif [ $status != 0 ]; then
		:
	elif [ -z "$failed" ]; then
		if [ $xfail = 1 ]; then
			echo "XPASS ($mode): our client passed every step: take $mode off XPPC_EXPECTED_FAIL (an acceptance test now)"
		else
			echo "PASS ($mode)"
		fi
	elif [ $xfail = 1 ]; then
		echo "XFAIL ($mode): $stages"
	else
		echo "FAIL ($mode): $stages"
		status=1
	fi
	;;
esac
echo "logs in $out"
exit $status
