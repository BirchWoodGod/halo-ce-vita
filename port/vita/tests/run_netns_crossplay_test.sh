#!/bin/bash
# Crossplay against real PC builds (docs/crossplay.md, task T8 and section
# 10): OpenCE, ChupathingyCE and DamnationCE Linux builds of pinned commits
# (build_pc_crossplay.sh), and our Vita-path client and host (configure.py
# --linux-net-vita; no switch since Oct 19 2026: each game on its own wire,
# a host's chosen by HALO_NET_HOST_CROSSPLAY), headless, on one Linux
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
# fails). Acceptance tests since the wire switch (T4/T5, network_wire.h):
# opencelan, opencehost, chupahost, chupaserver and damnserver must pass, and
# since the guardrails (T11, crossplay_guard.h) crossrefuse and crossbig. A
# mode named in XPPC_EXPECTED_FAIL (none by default) is reported as XFAIL
# with the stage where our client stopped and the log lines that say why,
# exit status 0 (XPPC_STRICT=1: a failure; a pass is reported as XPASS: take
# the mode off XPPC_EXPECTED_FAIL). Every game an OpenCE host makes by its
# fast setup takes 128 players, a large PC game, which our client joins
# after asking once: the answer is HALO_CROSSPLAY_LARGE_ANSWER (XPPC_LARGE_ANSWER,
# default yes)
#   opencelan   OpenCE host, our client on its LAN (system link)
#   opencehost  OpenCE host behind NAT A, public; our client behind NAT B
#               joins it from the server browser, and another opens its
#               invite link (halo://join/..., on the command line)
#   crosstls    opencehost's server browser join with our client's way to
#               the broker's plain port (1883) reset by its routers (as EMQX's
#               and Mosquitto's were on Oct 11 2026), the broker's MQTT over
#               TLS port (8883, a certificate from an authority made for this
#               run, in the test's folder, never a real key) open: our client
#               must fall back to TLS, see the OpenCE host's game listed over
#               plain, signal it and play (the same broker behind both ports)
#   chupahost   ChupathingyCE host (a player's game) on a LAN (over the
#               internet: chupaserver)
#   chupaserver ChupathingyCE's dedicated server (chupathingyce-server,
#               HALO_DEDICATED), an OpenCE client and ours, on a LAN and over
#               the internet (from the server browser)
#   damnserver  DamnationCE's dedicated server (its game binary with
#               HALO_DEDICATED), an OpenCE client and ours, on a LAN and over
#               the internet (by its invite: it lists only on its game list)
#   crossmixed  one build, both wires, no setting changed: our Vita host for
#               Vitas (the legacy wire) and for Vitas and PC (the crossplay
#               wire, on a LAN and from the browser, tagged VITA+PC), and an
#               OpenCE host on a LAN, each joined by our client and played
#   crossrefuse what our client refuses in a PC game (section 6, T11), on
#               OpenCE hosts on LANs of their own: a second local player
#               (split screen, HALO_NETWORK_TEST_SECOND_PLAYER: not added,
#               said, the game played on), and a map our client lacks
#               (wizard: T3's refusal, crossplay_maps.c; checked only when
#               this build has it, else reported as waiting for T3). A PC
#               co-op game is not refused since Oct 11 2026: crosscoop
#   crosscoop   PC co-op (section 6.2): an OpenCE, a ChupathingyCE and a
#               DamnationCE host each start a network co-op game on a
#               campaign level (XPPC_COOP_LEVEL, b30: its Map screen's co-op,
#               normal difficulty, OpenCE's extra enemies per player as its
#               default) on a LAN of its own, and our client joins it,
#               answering yes to the co-op question (asked exactly once),
#               plays (a bot) with the host's player, has the host's AI units
#               (driven by their actor states), takes the host's checkpoint
#               (game_save on the host's telnet console) and its BSP switch
#               (switch_bsp), and ends with no AI unit untracked, no actor
#               state refused and no crash; the OpenCE game again with four
#               players (our client and two OpenCE clients); and our client
#               answering no on an OpenCE host: left, no tick of it played.
#               Control: an OpenCE client of an OpenCE co-op host. The census
#               lines (objects, units, AI units tracked) are summarised
#   crosscoopcensus  measurements for section 6.2 (not in all): for each
#               campaign level of XPPC_COOP_LEVELS (every one in the maps
#               folder), an OpenCE co-op host with our client, alone (2
#               players) and with two OpenCE clients (4), the host's extra
#               enemies XPPC_COOP_ENEMIES (per_player: OpenCE's default, 50%
#               a player; multiplier:N), its BSPs walked through on its
#               console; our client's most objects, units, living AI units and
#               AI units tracked per game in a table. Fails on an AI unit
#               untracked, a state refused or a crash. XPPC_COOP_PARALLEL
#               games at once (3)
#   crosscoopfuzz  the co-op handlers fuzzed in a live PC co-op game (not in
#               all; HALO_TEST_VITA an AddressSanitizer build with
#               HALO_NET_FUZZ_TWIN: net_fuzz_twin.h): OpenCE, ChupathingyCE
#               and DamnationCE co-op hosts, our client twinning one in
#               XPPC_FUZZ_RATE (2) of the host's AI and co-op messages and
#               the batches that carry them (HALO_NET_FUZZ_KINDS=16,64-77),
#               and on OpenCE one in 3 of every message; passes with no
#               sanitizer memory error and no crash, our client running to
#               its end (the game itself may go wrong: twins lie)
#   crossbig    large PC games (section 6: allowed, Experimental, asked
#               once; never refused by their players): an OpenCE host of
#               128 players (its fast setup's) with our client answering yes,
#               on a LAN (asked in the lobby) and from the server browser
#               (asked there, the listing badged EXPERIMENTAL, the lobby not
#               asking again): asked exactly once, then the whole game
#               played; and answering no on a LAN: no tick of its game
#               played, the game left
#   crossmaps   a PC host's map (docs/crossplay.md T3, crossplay_maps.h): OpenCE
#               hosts on LANs of their own, each with our client: Blood Gulch
#               (levels\test\bloodgulch\bloodgulch, version 0: the Xbox map
#               on both, joins and plays), custom_maps\<XPPC_CE_MAP> (its
#               version, the hash's first 32 bits, since OpenCE's 29) with
#               our client holding the same file as <name>.map, as
#               <name>_<digits>.map, and beside another version as
#               <name>.map, PC maps on (joins and plays the host's version),
#               with another version only (refused as such), and without it
#               (refused with the map named, joining nothing, no map share
#               query); controls: OpenCE clients on the first two.
#               Needs XPPC_CE_MAP (or HALO_TEST_CE_MAP), a Custom Edition map
#               needing no resource maps here (Invader's pcgulch.map), and
#               XPPC_CE_RESOURCES, a folder with Custom Edition's bitmaps.map,
#               sounds.map and loc.map (OpenCE wants them for any Custom
#               Edition map), both linked into the OpenCE copies' custom_maps
#               folder; else SKIP. Until T4 our
#               client stops at the version (XFAIL); the map check itself
#               runs today against a stand-in host: run_crossplay_maps_test.sh
#   crossctf    Capture the Flag (the network test's ctf variant: every game
#               above is Slayer's, the host never reaching its second game):
#               an OpenCE and a ChupathingyCE host on LANs of their own, our
#               client joining each and playing the game to its end with the
#               flags' objects on both sides: discover, version, join,
#               settings, play, kills, the end of the game, stability, and the
#               scoreboard at the end the same on both (the network test's CTF
#               game ends within seconds, before its kills could score or the
#               spawn, damage and rejoin steps have time). Control: an OpenCE
#               client
#   crossold    an older PC host (XPPC_OLD_BUILDS's opence/ and chupa/: of
#               network version 28, OpenCE b1ef7f7e and ChupathingyCE
#               35fc7e8b, or 26) since the crossplay wire joins 29 alone
#               (OpenCE 8102e711, ChupathingyCE 7d6d8f24): on a LAN our client sees
#               the game, joins none of it and tells the player the host
#               runs an older OpenCE or ChupathingyCE, naming both versions;
#               over the internet (OpenCE's, from the server browser) it never
#               lists the game (its listing's version). SKIP without
#               XPPC_OLD_BUILDS
#   pcjoinsvita PC joins Vita (T15, T16): our host for Vitas and PC at a Vita's
#               pace (HALO_TICK_SLOWDOWN, XPPC_SLOWDOWN 19); OpenCE clients
#               on its LAN, from OpenCE's browser, and by our host's join link
#               (halo://join/..., its invite file) through both NATs; each
#               game played to its end (crossplay_check's steps)
#   pccap       the Vitas' 16 on that host: 8 OpenCE clients and 7 of ours on
#               one LAN, then a 17th (OpenCE), which joins nothing (not in all)
#   browserboth one server browser, both kinds, tagged: an OpenCE host, our
#               Vita host for Vitas and ours for Vitas and PC, all public; our
#               clients see [PC ...], [VITA], [VITA+PC] and each joins one
#               from the same list on its wire; Browser shows Vita games
#               lists the Vitas' alone; on one LAN the System Link list's tags,
#               the Vitas' games first
#   crosspassword  a password on our host for Vitas and PC: a wrong one
#               refused, the right one plays; OpenCE's browser leaves it alone
#   pcversion   OpenCE of 28 (XPPC_OLD_BUILDS) and of 29 (XPPC_OPENCE29) do
#               not join our 32 host; our client refuses a host of 33 (our
#               build standing in for a PC host, HALO_TEST_ANNOUNCE_VERSION) on
#               a LAN (a newer version) and drops its listing online
#   net32       OpenCE's network version 32 (docs/opence-net32-spec.md;
#               XPPC_BUILDS's opence of 6ec42856): its aim assist policy
#               (kind 90) taken by our client from OpenCE hosts blocking and
#               allowing; sent by our host to an OpenCE client at each ready,
#               as the game starts and when it changes (HALO_TEST_AIM_ASSIST_TOGGLE),
#               seen on the wire (a sniffer on the host's LAN port: 12
#               bytes, count 1, reserved 0); our client refusing forged
#               copies (HALO_TEST_AIM_ASSIST_FORGE: in a datagram, of 13 bytes,
#               count 2, value 2, reserved 1) from our host and playing on;
#               an OpenCE client whose client port is held (it binds another)
#               playing on our host, answered at its port; an OpenCE host
#               deleting every object (its console's object_destroy_all) in a
#               CTF game, our client following it without an assertion
#   net29       the older version the crossplay wire still joins
#               (XPPC_OPENCE29, an OpenCE of 29): our client on its LAN
#               host, and our host announcing 29 (HALO_TEST_CROSSPLAY_HOST_VERSION)
#               with its clients; played by 29's rules
#   crossfuzzhost  the host side fuzzed (not in all; HALO_TEST_VITA an ASan
#               HALO_NET_FUZZ_TWIN build): our host for Vitas and PC twins one
#               in XPPC_FUZZ_RATE (2) of its OpenCE clients' messages and
#               datagrams; no sanitizer memory error, no crash
#   crosscoophost  co-op hosted by our Vita for Vitas and PC (experimental):
#               an OpenCE client and one of ours beside the host's player
#   all         every mode above, one after the other (expected failures as
#               XFAIL), and a summary
#
# Not here: crossversion (wants T10's signed table): reports SKIP.
# dedicatedopence was dropped (our dedicated servers stay off the crossplay
# wire).
#
# With XPPC_PROBE=1 the expected-fail modes also run our client with
# HALO_NETWORK_TEST_CROSS_LINE=1 (debug builds: it lists and joins a host on
# the other side of the Vita-only line), to see how far it gets past that
# first check (the version, the join, the settings record ...). Reported,
# never failing.
#
#   XPPC_BUILDS        a folder with build_pc_crossplay.sh's opence/, chupa/
#                      and damnation/ (their BUILD_INFO commits are printed).
#                      A build of another network version than our crossplay
#                      wire's (DamnationCE 7be58b2b is 26) is not played: its
#                      games are left out with a note (crossold checks how
#                      such a host is refused)
#   XPPC_OLD_BUILDS    crossold's: build_pc_crossplay.sh's opence/ (and
#                      chupa/) of an older network version
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
#   XPPC_EXPECTED_FAIL the expected-fail modes (none)
#   XPPC_LARGE_ANSWER  our client's answer about a large PC game (yes)
#   XPPC_COOP_LEVEL    crosscoop's campaign level (b30); XPPC_COOP_SECONDS how
#                      long its games run (240); XPPC_COOP_HOST_ENV more
#                      VAR=value for its hosts (HALO_TEST_INPUT=look:3: a10's
#                      cryo tube wants its host to look around)
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
# (a sanitizer build's stack trace runs llvm-symbolizer, which asks the
# debuginfod servers DEBUGINFOD_URLS names: a name lookup the leak check
# rightly fails; the copies symbolize from their own debug information)
unset DEBUGINFOD_URLS
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
export HALO_MENUS_FOLDER=${HALO_MENUS_FOLDER:-$root/port/vita/app0/menus/}
expected_fail=${XPPC_EXPECTED_FAIL:-}
mode=${1:-}

case $mode in
pcself|chupaopence|opencelan|opencehost|crosstls|chupahost|chupaserver|damnserver|crossrefuse|crossbig|crossmixed|crosscoop|crossctf) ;;
pcjoinsvita|pccap|browserboth|crosspassword|crosscoophost|net32) ;;
net29)
	[ -n "${XPPC_OPENCE29:-}" ] && [ -x "$XPPC_OPENCE29" ] ||
		{ echo "SKIP (net29): wants XPPC_OPENCE29 (an OpenCE of network version 29's binary: build_pc_crossplay.sh opence ... 8102e711)"; exit 0; } ;;
pcversion)
	[ -n "${XPPC_OLD_BUILDS:-}" ] && [ -x "$XPPC_OLD_BUILDS/opence/src/build/linux/halo" ] ||
		{ echo "SKIP (pcversion): wants XPPC_OLD_BUILDS (an OpenCE of 28; XPPC_OPENCE29 an OpenCE of 29's binary too)"; exit 0; } ;;
crossfuzzhost)
	grep -aq "net fuzz twin: one message in" "${HALO_TEST_VITA:-$root/build/linux/halo}" 2>/dev/null ||
		{ echo "SKIP (crossfuzzhost): HALO_TEST_VITA is not a HALO_NET_FUZZ_TWIN build (AddressSanitizer)"; exit 0; } ;;
crossold)
	[ -n "${XPPC_OLD_BUILDS:-}" ] && [ -x "$XPPC_OLD_BUILDS/opence/src/build/linux/halo" ] ||
		{ echo "SKIP (crossold): wants XPPC_OLD_BUILDS (an older OpenCE build: build_pc_crossplay.sh opence ... 6399ed9d)"; exit 0; } ;;
crosscoopcensus) ;;
crosscoopfuzz)
	grep -aq "net fuzz twin: one message in" "${HALO_TEST_VITA:-$root/build/linux/halo}" 2>/dev/null ||
		{ echo "SKIP (crosscoopfuzz): HALO_TEST_VITA is not a HALO_NET_FUZZ_TWIN build (AddressSanitizer)"; exit 0; } ;;
crossmaps)
	ce_map=${XPPC_CE_MAP:-${HALO_TEST_CE_MAP:-}}
	ce_resources=${XPPC_CE_RESOURCES:-}
	[ -n "$ce_map" ] && [ -f "$ce_map" ] ||
		{ echo "SKIP (crossmaps): wants XPPC_CE_MAP (a Custom Edition map that needs no resource maps)"; exit 0; }
	for f in bitmaps sounds loc; do
		[ -f "$ce_resources/$f.map" ] ||
			{ echo "SKIP (crossmaps): wants XPPC_CE_RESOURCES (a folder with Custom Edition's $f.map, for OpenCE)"; exit 0; }
	done ;;
crossversion)
	echo "SKIP (crossversion): wants T10's signed crossplay table (HALO_CROSSPLAY_TABLE, a test key)"
	exit 0 ;;
dedicatedopence)
	echo "SKIP ($mode): our dedicated servers stay off the crossplay wire"
	exit 0 ;;
all)
	summary=
	status=0
	mkdir -p "$HOME/halo-test-out"
	all_log=$(mktemp "$HOME/halo-test-out/xppc-all.XXXX")
	for m in pcself chupaopence opencelan opencehost crosstls chupahost chupaserver damnserver crossctf crossmaps crossmixed crossrefuse crossbig crosscoop crossold pcjoinsvita browserboth crosspassword pcversion net32 net29 crossversion; do
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
old_opence=${XPPC_OLD_BUILDS:+$XPPC_OLD_BUILDS/opence/src/build/linux/halo}
old_chupa=${XPPC_OLD_BUILDS:+$XPPC_OLD_BUILDS/chupa/src/build/linux/halo}
# (pcversion: an OpenCE of a newer network version than the crossplay wire's,
# build_pc_crossplay.sh's opence of 6ec42856, 32; no mode uses it since our
# wire joins 32 too: kept for the next raise)
new_opence=${XPPC_NEW_OPENCE:-}
# (pcversion, net29: an OpenCE of the older version the crossplay wire still
# joins, 29: build_pc_crossplay.sh's opence of 8102e711)
opence29=${XPPC_OPENCE29:-}
# (an OpenCE client or host of ChupathingyCE's version, for the games of
# ChupathingyCE's whose controls want one: OpenCE of 29 when XPPC_BUILDS's is
# of 32 and XPPC_OPENCE29 is there, else XPPC_BUILDS's)
opence_chupa=opence
[ -n "$opence29" ] && [ -x "$opence29" ] && opence_chupa=opence29
# the network version a PC build announces: the NETWORK_VERSION file beside
# its BUILD_INFO if there is one; else by its commit, for the builds whose
# source is not to be read (OpenCE is AGPL-3.0-or-later from 4b09168e: its
# version is docs/opence-net32-spec.md's, never read from its tree); else its
# source's halo_port_limits.h (the CC0 builds before it, beside its binary:
# build_pc_crossplay.sh's src). Empty: not known, played
pc_version() { # pc_version BINARY
	local build commit
	build=$(dirname "$1")/../../..
	if [ -f "$build/NETWORK_VERSION" ]; then
		tr -dc 0-9 < "$build/NETWORK_VERSION"
		return
	fi
	commit=$(sed -n 's/^commit: //p' "$build/BUILD_INFO" 2>/dev/null)
	case $commit in
	6ec42856*) echo 32; return ;;
	esac
	sed -n 's/^#define HALO_PORT_NETWORK_VERSION \([0-9]*\).*/\1/p' \
		"$(dirname "$1")/../../port/linux/include/halo_port_limits.h" 2>/dev/null | head -1
}
# the versions our crossplay wire joins (network_wire.h: each game by its
# host's), "29 and 32" as our client logs them; the one our host announces
our_version=$(sed -n 's/^#define HALO_PORT_CROSSPLAY_NETWORK_VERSION \([0-9]*\).*/\1/p' "$root/port/linux/include/network_wire.h")
our_older_version=$(sed -n 's/^#define HALO_PORT_CROSSPLAY_OLDER_NETWORK_VERSION \([0-9]*\).*/\1/p' "$root/port/linux/include/network_wire.h")
our_versions="${our_older_version:+$our_older_version and }$our_version"
plays_ours() { # plays_ours BINARY NAME: 0 if the build's version is one ours joins (or not known); else says so
	local version
	version=$(pc_version "$1")
	[ -z "$version" ] || [ "$version" = "$our_version" ] || [ "$version" = "$our_older_version" ] && return 0
	echo "--- $2 left out: the build announces network version $version, the crossplay wire joins $our_versions (crossold: refused)"
	return 1
}
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
case $mode in chupaopence|chupahost|crosscoop|crossctf) need ChupathingyCE "$chupa" ;; esac
[ "$mode" = chupaserver ] && need "ChupathingyCE's server" "$chupa_server"
case $mode in damnserver|crosscoop|crosscoopfuzz) need DamnationCE "$damnation" ;; esac
[ "$mode" = crosscoopfuzz ] && need ChupathingyCE "$chupa"
[ "$mode" = net29 ] && need "OpenCE of 29" "$opence29"
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
# (crosstls: every broker's TLS port too, with a certificate for each
# broker's address from an authority made here, in the test's folder; our
# copies told of it)
broker_tls=
if [ "$mode" = crosstls ]; then
	mkdir -p "$out/tls"
	openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 -subj "/CN=Halo Broker Test CA" \
		-addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" \
		-keyout "$out/tls/ca_key.pem" -out "$out/tls/ca.pem" 2>/dev/null &&
	openssl req -newkey rsa:2048 -nodes -subj "/CN=broker.halo.test" -keyout "$out/tls/broker_key.pem" \
		-out "$out/tls/broker.csr" 2>/dev/null &&
	printf 'subjectAltName=DNS:broker.halo.test%s\nbasicConstraints=CA:FALSE\n' \
		"$(for i in $(seq 10 40); do printf ',IP:198.51.100.%d' "$i"; done)" > "$out/tls/broker.ext" &&
	openssl x509 -req -in "$out/tls/broker.csr" -CA "$out/tls/ca.pem" -CAkey "$out/tls/ca_key.pem" -CAcreateserial \
		-days 2 -extfile "$out/tls/broker.ext" -out "$out/tls/broker.pem" 2>/dev/null ||
		{ echo "BROKEN ($mode): the test's authority could not be made (openssl)"; exit 2; }
	broker_tls="--tls-port 8883 --tls-cert $out/tls/broker.pem --tls-key $out/tls/broker_key.pem"
	XPPC_OUR_ENV="${XPPC_OUR_ENV:-} HALO_NET_BROKERS_CA_FILE=$out/tls/ca.pem"
fi
broker() { # broker -> broker_address: a broker of its own (198.51.100.10+), for one pair
	broker_address=198.51.100.$((10 + brokers))
	ip addr add "$broker_address/32" dev lo
	python3 "$here/mqtt_test_broker.py" --host "$broker_address" --port 1883 $broker_tls \
		$([ "${XPPC_MQTT311:-0}" = 1 ] && echo --mqtt311) > "$out/broker-$broker_address.log" 2>&1 & pids="$pids $!"
	brokers=$((brokers + 1))
}

# (OpenCE 6ec42856 faults by itself once a network test's game is over, a
# host and its clients alike, OpenCE's own control games too (Oct 19 2026:
# docs/crossplay.md 10.9): crossplay_check.py notes that rather than failing
# stability on it, for those copies alone; never for ours)
case $(sed -n 's/^commit: //p' "$(dirname "$opence")/../../../BUILD_INFO" 2>/dev/null) in
6ec42856*) export XPPC_POSTGAME_FAULT_KINDS="opence" ;;
esac
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
	echo "$kind" > "$dir/kind"
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
			HALO_NET_ALLOW_RELAY=false HALO_CROSSPLAY_LARGE_ANSWER=${XPPC_LARGE_ANSWER:-yes} ${XPPC_OUR_ENV:-}" ;;
	*)
		case $kind in
		opence) binary=$opence ;;
		chupa) binary=$chupa ;;
		oldopence) binary=$old_opence ;;
		oldchupa) binary=$old_chupa ;;
		newopence) binary=$new_opence ;;
		opence29) binary=$opence29 ;;
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
			HALO_NETWORK_TEST_INVITE_FILE=invite.txt ${host_extra:-}" ;;
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
# mark_stop NAME: where each copy's logs end as the harness stops them
# (stop_offsets: run.log's and debug.txt's sizes), so that what a PC build
# logs after it (OpenCE 6ec42856 faults as it is stopped once its game is
# over, its own control games too: docs/crossplay.md 10.9) is told apart from
# a crash in the game (crossplay_check.py); ours are held to everything
mark_stop() {
	local copy
	for copy in "$out/$1"/*/; do
		[ -f "$copy/run.log" ] || continue
		echo "$(stat -c %s "$copy/run.log" 2>/dev/null || echo 0) $(stat -c %s "$copy/data/debug.txt" 2>/dev/null || echo 0)" \
			> "$copy/stop_offsets"
	done
}
# pair NAME TRANSPORT HOSTKIND CLIENTKIND [CLIENTENV]: a host and its client
# (and the copy that comes back after the first leaves), in the background;
# checked by check_pair. A server's: SERVERKIND and two clients, the first
# an OpenCE one (pair_server)
# block_plain_mqtt KIND: (crosstls) a client of KIND (block_plain) has its
# router reset its connections to a broker's plain port, 1883
block_plain_mqtt() {
	[ -n "${block_plain:-}" ] && [ "$1" = "$block_plain" ] &&
		in_ns "$side_router" iptables -I FORWARD 1 -p tcp --dport 1883 -j REJECT --reject-with tcp-reset
	return 0
}
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
		block_plain_mqtt "$clientkind"
		subnet=$((subnet + 1)); side "$subnet"; late_ns=$side_machine; late_ip=192.168.$subnet.2
		block_plain_mqtt "$clientkind"
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
				# (ours writes it whole only to its invite file: a bearer token,
				# never in its log)
				invite=$(cat "$out/$name/host/run.log" "$out/$name/host/data/invite.txt" 2>/dev/null |
					grep -aoE 'halo://join/[0-9a-f]{64,}' | head -1)
				[ -n "$invite" ] && break
				sleep 1
			done
			echo "$name: the host's invite: ${invite:+${invite:0:24}...}${invite:-none in its log}"
		fi
		# (hold_client_port: the client port, 5151, held by another program
		# in each client's namespace, as a second copy on one computer has it:
		# the client binds another, and the host must answer it there)
		if [ -n "${hold_client_port:-}" ]; then
			for ns in $client_ns $late_ns; do
				nsenter -t "$ns" -n python3 -I -c 'import socket, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("0.0.0.0", 5151)); time.sleep(100000)' &
				mine="$mine $!"
			done
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
		mark_stop "$name"
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
		copy_args=$invite run_copy "$name/client" "$c1_ns" $opence_chupa "$c1_ip" "$broker_address" $(client_env $opence_chupa "$transport" $((41 + pairs)))
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

# coop_game NAME HOSTKIND "CLIENTKINDS" [OURENV]: a network co-op game on
# XPPC_COOP_LEVEL that HOSTKIND hosts on a LAN of its own (its network test's
# "coop" variant: the Map screen's co-op), each of CLIENTKINDS joining it
# (client, client2, ...: ours first when it is one); once the first client
# has played 20 s beside the host's player, the host saves a checkpoint, and
# 25 s later switches the BSP (its telnet console), noted in
# NAME/commands.log. In the background; coop_check reads it
coop_level=${XPPC_COOP_LEVEL:-b30}
coop_seconds=${XPPC_COOP_SECONDS:-240}
coop_game() {
	local name=$1 hostkind=$2 kinds=$3 extra=${4:-} n host_ns host_ip i kind
	local client_nss= client_ips=
	subnet=$((subnet + 1)); n=$subnet
	side "$n"; host_ns=$side_machine; host_ip=192.168.$n.2
	i=0
	for kind in $kinds; do
		i=$((i + 1))
		lan_machine "$n" $((i + 2)); client_nss="$client_nss $held"; client_ips="$client_ips 192.168.$n.$((i + 2))"
	done
	mkdir -p "$out/$name"
	echo "$name: $hostkind co-op host on ${coop_game_level:-$coop_level}${coop_host_extra:+ ($coop_host_extra)}, clients:$kinds" |
		tee "$out/$name/what"
	(
		run_seconds=$coop_seconds
		trap 'kill $mine 2>/dev/null; wait 2>/dev/null; exit 0' TERM
		run_copy "$name/host" "$host_ns" "$hostkind" "$host_ip" 198.51.100.1 HALO_NET_ONLINE=false \
			"HALO_NETWORK_TEST=host:levels\\${coop_game_level:-$coop_level}\\${coop_game_level:-$coop_level}:coop" \
			HALO_NETWORK_TEST_START=$start_delay HALO_NET_LOBBY_NAME=XpCoop HALO_TELNET_CONSOLE=1 HALO_TEST_INPUT=bot:3 \
			${coop_host_extra:-} ${XPPC_COOP_HOST_ENV:-}
		mine=$last_pid; host_pid=$last_pid
		for i in $(seq 1 60); do
			grep -aqE "network test: (player 0 is named|starting the game)" "$out/$name/host/run.log" 2>/dev/null && break
			sleep 1
		done
		i=0
		set -- $client_nss
		local ips=($client_ips) copy
		for kind in $kinds; do
			i=$((i + 1)); copy=client; [ $i -gt 1 ] && copy=client$i
			run_copy "$name/$copy" "$1" "$kind" "${ips[$((i - 1))]}" 198.51.100.1 HALO_NET_ONLINE=false HALO_NETWORK_TEST=join \
				HALO_TEST_INPUT=bot:$((50 + i)) $([ "$kind" = ours ] && echo "$extra")
			mine="$mine $last_pid"
			shift
			sleep 2
		done
		# (the host's checkpoint and BSP switch, once the first client has
		# played beside the host's player: two players alive in its log)
		played=0 saved= switched=
		while kill -0 "$host_pid" 2>/dev/null; do
			sleep 1
			played=$(grep -a "network test: tick" "$out/$name/client/run.log" 2>/dev/null |
				grep -aEc "player [0-9]+: \(.* player [0-9]+: \(")
			if [ -z "$saved" ] && [ "${played:-0}" -ge 20 ]; then
				saved=$SECONDS
				coop_console "$host_ns" "game_save" && echo "game_save sent after $played s of play" >> "$out/$name/commands.log"
			fi
			if [ -n "$saved" ] && [ -z "$switched" ] && [ $((SECONDS - saved)) -ge 25 ]; then
				switched=$SECONDS
				# (the host's other BSP: our client logs the one it has, the
				# host's, which it follows; the PC builds log none)
				bsp=$(grep -a "network test: co-op tick" "$out/$name/client/run.log" 2>/dev/null | tail -1 |
					sed -n 's/.*| bsp \([0-9]*\) .*/\1/p')
				target=$([ "${bsp:-0}" = 0 ] && echo 1 || echo 0)
				coop_console "$host_ns" "switch_bsp $target" &&
					echo "switch_bsp $target sent (the host on bsp ${bsp:-?})" >> "$out/$name/commands.log"
			fi
			# (the census: every BSP of the level in turn, 25 s each, by the
			# count our client logs)
			if [ "${coop_bsp_walk:-0}" = 1 ] && [ -n "$switched" ] && [ $((SECONDS - switched)) -ge 25 ]; then
				bsps=$(grep -a "network test: co-op tick" "$out/$name/client/run.log" 2>/dev/null | tail -1 |
					sed -n 's/.*| bsps \([0-9]*\) .*/\1/p')
				target=$((${target:-0} + 1))
				if [ -n "$bsps" ] && [ "$target" -lt "$bsps" ]; then
					switched=$SECONDS
					coop_console "$host_ns" "switch_bsp $target" && echo "switch_bsp $target sent (walk)" >> "$out/$name/commands.log"
				fi
			fi
		done
		mark_stop "$name"
		kill $mine 2>/dev/null; wait 2>/dev/null
	) > "$out/$name/pair.log" 2>&1 &
	pids="$pids $!"
	pair_pids="${pair_pids:-} $!"
	pairs=$((pairs + 1))
}
# coop_console NETNS COMMAND: a line to the host's script console
# (debug.telnet_console, 127.0.0.1:2323 in its namespace)
coop_console() {
	nsenter -t "$1" -n timeout 5 bash -c 'exec 3<>/dev/tcp/127.0.0.1/2323 || exit 1; sleep 0.5; printf "%s\r\n" "$1" >&3;
		sleep 1; exec 3>&-' coop_console "$2"
}
# coop_check NAME [no]: our client in that co-op game (coop_game's): its
# verdict in coop_result, its census in coop_census
coop_check() {
	local name=$1 answer=${2:-yes} log debug asked played census ai most untracked rejected saved bsp_host bsp_ours
	log=$out/$name/client/run.log debug=$out/$name/client/data/debug.txt
	asked=$(grep -ac "crossplay: asking about the co-op PC game" "$log" 2>/dev/null)
	played=$(grep -a "network test: tick" "$log" 2>/dev/null | grep -aEc "player [0-9]+: \(.* player [0-9]+: \(")
	census=$(grep -a "network test: co-op tick" "$log" 2>/dev/null | sed -n 's/.*| objects \([0-9]*\) units \([0-9]*\) living ai \([0-9]*\) | ai tracked \([0-9]*\) most \([0-9]*\) untracked \([0-9]*\) rejected \([0-9]*\).*/\1 \2 \3 \4 \5 \6 \7/p')
	coop_census=$(echo "$census" | awk 'NF == 7 { if ($1 > o) o = $1; if ($2 > u) u = $2; if ($3 > a) a = $3; if ($5 > m) m = $5;
		un = $6; re = $7 } END { printf "objects %d units %d living-ai %d ai-tracked-most %d untracked %d rejected %d",
		o, u, a, m, un, re }')
	ai=$(echo "$coop_census" | sed -n 's/.*living-ai \([0-9]*\).*/\1/p')
	most=$(echo "$coop_census" | sed -n 's/.*ai-tracked-most \([0-9]*\).*/\1/p')
	untracked=$(echo "$coop_census" | sed -n 's/.*untracked \([0-9]*\).*/\1/p')
	rejected=$(echo "$coop_census" | sed -n 's/.*rejected \([0-9]*\).*/\1/p')
	if [ "$answer" = no ]; then
		local left ticks
		left=$(grep -ac "leaving the co-op PC game .*the player said no" "$debug" 2>/dev/null)
		# (the host's game's ticks: a network co-op game's census lines; the
		# bot pressing A in the main menu afterwards starts a campaign of its
		# own, whose ticks are not the host's)
		ticks=$(grep -ac "network test: co-op tick [0-9]* " "$log" 2>/dev/null)
		echo "--- $name: asked ${asked:-0} times, left ${left:-0}, played ${ticks:-0} ticks"
		if [ "${asked:-0}" -ge 1 ] && [ "${left:-0}" -ge 1 ] && [ "${ticks:-0}" = 0 ]; then
			coop_result=pass
		else
			coop_result=asked-$asked-left-$left-ticks-$ticks
		fi
		return
	fi
	saved=$(grep -ac "co-op: the host's checkpoint" "$debug" 2>/dev/null)
	# (the BSP the host switched to, and whether our client followed it)
	bsp_host=$(sed -n 's/^switch_bsp \([0-9]*\) sent.*/\1/p' "$out/$name/commands.log" 2>/dev/null | head -1)
	bsp_ours=0
	[ -n "$bsp_host" ] && bsp_ours=$(grep -a "network test: co-op tick" "$log" 2>/dev/null | grep -ac "| bsp $bsp_host |")
	echo "--- $name: asked ${asked:-0} times; ${played:-0} s with two players alive; $coop_census; host checkpoints taken" \
		"${saved:-0}; on the host's switched BSP ${bsp_host:-?} for ${bsp_ours:-0} s"
	sed 's/^/  host: /' "$out/$name/commands.log" 2>/dev/null
	if [ "${asked:-0}" != 1 ]; then
		coop_result=asked-${asked:-0}-times
	elif [ "${played:-0}" -lt 30 ]; then
		coop_result=played-${played:-0}-s
	elif [ "${ai:-0}" = 0 ] || [ "${most:-0}" = 0 ]; then
		coop_result=no-host-ai-seen
	elif [ "${untracked:-0}" != 0 ] || [ "${rejected:-0}" != 0 ]; then
		coop_result=ai-untracked-$untracked-rejected-$rejected
	elif [ "${saved:-0}" = 0 ]; then
		coop_result=no-checkpoint
	elif [ -z "$bsp_host" ] || [ "${bsp_ours:-0}" -lt 5 ]; then
		coop_result=bsp-switch-not-followed
	elif cat "$log" "$debug" 2>/dev/null | grep -aqE "ERROR: AddressSanitizer|egmentation fault|stack trace|SIGSEGV|SIGABRT|EXCEPTION halt"; then
		coop_result=crash-or-sanitizer
	else
		coop_result=pass
	fi
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
	pair lan-chupa-opence lan chupa $opence_chupa
	pair lan-opence-chupa lan $opence_chupa chupa
	pair invite-opence-chupa invite $opence_chupa chupa
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
	# (OpenCE 6ec42856's host logs only the start of its invite link, as
	# ChupathingyCE's does: none to open, so that join is not tested here;
	# docs/crossplay.md 10.9)
	if grep -aq "the host's invite: none in its log" "$out/invite-opence-ours/pair.log" 2>/dev/null &&
		grep -aq "halo://join/[0-9a-f]*\.\.\. (the rest is left out of the log)" "$out/invite-opence-ours/host/run.log" 2>/dev/null; then
		echo "--- invite-opence-ours: not tested: the OpenCE host logs only the start of its invite link"
		our_results=$(for r in $our_results; do case $r in invite-opence-ours:*) ;; *) printf '%s ' "$r" ;; esac; done)
	fi
	echo "--- the brokers: what each kind of client published and subscribed to (hceu: OpenCE's topics, hcev: ours)"; broker_topics
	;;
crosstls)
	# (our client's way to the plain port reset by its routers; the OpenCE
	# host's open: it lists over plain, our client browses and signals over
	# TLS, through the one broker)
	control net-opence-opence && pair net-opence-opence net opence opence
	test_names="net-opence-ours-tls"
	block_plain=ours pair net-opence-ours-tls net opence ours
	run_our_modes
	tls_ok=1
	for copy in client late back; do
		log=$out/net-opence-ours-tls/$copy/run.log
		[ -f "$log" ] || continue
		grep -a "Internet play: broker 198.51.100" "$log" | head -3 | sed "s/^/  $copy: /"
		grep -aqE "Internet play: broker 198\.51\.100\.[0-9]+: .*; trying it over TLS \(port 8883\) at once" "$log" &&
			grep -aqE "Internet play: broker 198\.51\.100\.[0-9]+ ready \(MQTT [0-9.]+ over TLS \(TLS.*\) at 198\.51\.100\.[0-9]+:8883; carries the public games\)" "$log" ||
			{ echo "  our $copy did not fall back to the broker's TLS port"; tls_ok=0; }
	done
	tls_connects=$(cat "$out"/broker-*.log 2>/dev/null | grep -c "^connect hce[uv]-[0-9a-f]* mqtt[45] .* tls$")
	plain_connects=$(cat "$out"/broker-*.log 2>/dev/null | grep "^connect hce" | grep -vc " tls$")
	echo "  the brokers' connections: $tls_connects over TLS (ours), $plain_connects plain (OpenCE's)"
	{ [ "$tls_connects" -ge 1 ] && [ "$plain_connects" -ge 2 ]; } || tls_ok=0
	[ $tls_ok = 1 ] || our_results="$our_results net-opence-ours-tls:tls"
	echo "--- the brokers' topics"; broker_topics
	;;
chupahost)
	# (a player's ChupathingyCE game on a LAN only: its network test neither
	# lists the game nor logs its invite whole; over the internet see
	# chupaserver)
	control lan-chupa-opence && pair lan-chupa-opence lan chupa $opence_chupa
	test_names="lan-chupa-ours"
	pair lan-chupa-ours lan chupa ours
	probe lan-chupa-ours-probe && pair lan-chupa-ours-probe lan chupa ours "$cross_line"
	run_our_modes
	echo "--- the brokers' topics"; broker_topics
	;;
chupaserver|damnserver)
	kind=$mode
	if [ "$mode" = damnserver ] && ! plays_ours "$damnation" "DamnationCE's server"; then
		echo "SKIP ($mode): DamnationCE's build is of network version $(pc_version "$damnation"), not $our_versions"
		echo "logs in $out"
		exit 0
	fi
	# (over the internet: ChupathingyCE's server is listed in the server
	# browser (HALO_DEDICATED_PUBLIC); DamnationCE's only on its game list
	# (halo.milenko.org, off here), so its clients open its invite)
	internet=net
	[ "$mode" = damnserver ] && internet=invite
	for transport in lan $internet; do
		if control "$transport-$mode-opence"; then
			pair_server "$transport-$mode-opence" "$transport" "$kind" $opence_chupa; : > "$out/$transport-$mode-opence/server"
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
crossmixed)
	# no switch (Oct 19 2026): each game on its own wire, one build of ours
	# on both - our Vita host for Vitas (the legacy wire) and for Vitas and
	# PC (the crossplay wire), each joined by our client from the same build
	# and settings, on a LAN and from the server browser; and an OpenCE host
	# joined by that client too, on a LAN, no setting changed
	pair mixed-lan-vita lan ours ours
	host_extra="HALO_NET_HOST_CROSSPLAY=true" pair mixed-lan-vitapc lan ours ours
	host_extra="HALO_NET_HOST_CROSSPLAY=true" pair mixed-net-vitapc net ours ours
	pair mixed-lan-opence lan opence ours
	wait_pairs
	for name in mixed-lan-vita mixed-lan-vitapc mixed-net-vitapc mixed-lan-opence; do
		hostdebug=$out/$name/host/data/debug.txt clientdebug=$out/$name/client/data/debug.txt
		hostlog=$out/$name/host/run.log clientlog=$out/$name/client/run.log
		case $name in
		mixed-lan-vita) want="the legacy Vita wire" ;;
		*) want="the crossplay wire" ;;
		esac
		wire_said=$(cat "$clientlog" "$clientdebug" 2>/dev/null | grep -ac "network: $want (joining a game from the list")
		# (the legacy wire is the one with no game: no change to log, its join's line)
		[ "$name" = mixed-lan-vita ] && wire_said=$(cat "$clientlog" "$clientdebug" 2>/dev/null |
			grep -ac "joining a host of network version 18[[:space:]]*$")
		host_said=1
		case $name in *vitapc) host_said=$(cat "$hostlog" "$hostdebug" 2>/dev/null |
			grep -ac "network: the crossplay wire (hosting for Vitas and PC, a Vita's game)") ;; esac
		if [ "${wire_said:-0}" = 0 ] || [ "${host_said:-0}" = 0 ]; then
			result="wire-$wire_said-host-$host_said"
		elif ! check_pair "$name"; then
			result=${check_stage:-play}
		else
			result=pass
		fi
		echo "--- $name: client on $want: ${wire_said:-0}; $result"
		our_results="$our_results $name:$result"
	done
	# (the listing's tag: the Vita's crossplay game listed VITA+PC, its name
	# without the [VITA] the PC browsers see)
	rows=$(grep -ah "network test: browser row" "$out/mixed-net-vitapc/client/run.log" 2>/dev/null | sort -u | head -3)
	echo "$rows" | sed 's/^/  /'
	echo "$rows" | grep -q '\[VITA+PC\] "XpHost"' || our_results="$our_results mixed-net-vitapc-tag:row-tag"
	echo "--- the brokers' topics"; broker_topics
	;;
pcjoinsvita)
	# PC joins Vita (docs/crossplay.md T15, T16): our host (a Vita's pace:
	# HALO_TICK_SLOWDOWN, XPPC_SLOWDOWN, 19) hosting for Vitas and PC, OpenCE
	# clients joining it on its LAN, from OpenCE's server browser (our listing
	# on hceu, format 1, named [VITA] XpHost) and by our host's join link
	# (halo://join/..., through both NATs, no relay); each game played to its
	# end as a PC host's is
	control lan-opence-opence && pair lan-opence-opence lan opence opence
	pcjv="HALO_NET_HOST_CROSSPLAY=true HALO_TICK_SLOWDOWN=${XPPC_SLOWDOWN:-19}"
	test_names="lan-ours-opence net-ours-opence invite-ours-opence"
	host_extra=$pcjv pair lan-ours-opence lan ours opence
	host_extra=$pcjv pair net-ours-opence net ours opence
	host_extra=$pcjv pair invite-ours-opence invite ours opence
	run_our_modes
	for name in lan-ours-opence net-ours-opence invite-ours-opence; do
		hostlog=$out/$name/host/run.log hostdebug=$out/$name/host/data/debug.txt
		wire=$(cat "$hostlog" "$hostdebug" 2>/dev/null | grep -ac "network: the crossplay wire (hosting for Vitas and PC")
		echo "  $name: our host on the crossplay wire: ${wire:-0}; $(grep -ah 'the game is public' "$hostlog" | head -1 |
			sed 's/^.*Internet play: //')"
		[ "${wire:-0}" -ge 1 ] || our_results="$our_results $name-wire:host-wire"
	done
	grep -ah "listed\|browse" "$out/net-ours-opence/client/run.log" 2>/dev/null | grep -a "XpHost" | head -2 | sed 's/^/  OpenCE: /'
	echo "--- the brokers' topics"; broker_topics
	;;
pccap)
	# the Vitas' 16 on our host for Vitas and PC (T15): 8 OpenCE clients and
	# 7 of ours join it on one LAN (16 players with its own), at a Vita's
	# pace (HALO_TICK_SLOWDOWN 19); a 17th (OpenCE) finds it full and joins
	# nothing
	subnet=$((subnet + 1)); n=$subnet
	side "$n"; host_ns=$side_machine
	mkdir -p "$out/cap"
	run_seconds=$seconds
	run_copy cap/host "$host_ns" ours "192.168.$n.2" 198.51.100.1 HALO_NET_ONLINE=false HALO_NETWORK_TEST=host:bloodgulch:slayer \
		HALO_NETWORK_TEST_START=60 HALO_NETWORK_TEST_KILL=$kill_every HALO_NETWORK_TEST_SCORE=50 HALO_NET_LOBBY_NAME=XpCap \
		HALO_NET_HOST_CROSSPLAY=true HALO_TICK_SLOWDOWN=${XPPC_SLOWDOWN:-19}
	cap_pids=$last_pid
	sleep 8
	for i in $(seq 1 16); do
		kind=opence; [ $((i % 2)) = 0 ] && [ $i -le 14 ] && kind=ours
		lan_machine "$n" $((i + 2))
		run_copy "cap/c$i" "$held" "$kind" "192.168.$n.$((i + 2))" 198.51.100.1 HALO_NET_ONLINE=false HALO_NETWORK_TEST=join \
			HALO_TEST_INPUT=bot:$((60 + i))
		cap_pids="$cap_pids $last_pid"
		# (the 17th player last, once the 16 are in)
		[ $i = 15 ] && sleep 20
		sleep 2
	done
	sleep 60
	for p in $cap_pids; do kill "$p" 2>/dev/null; done
	wait $cap_pids 2>/dev/null
	hostlog=$out/cap/host/run.log
	most=$(grep -aoE "network test: player [0-9]+ is named" "$hostlog" | awk '{ print $4 }' | sort -n | tail -1)
	full=$(grep -ac "network test: tick" "$out/cap/c16/run.log" 2>/dev/null)
	joined16=$(grep -aEc "network test: (joining|tick)" "$out/cap/c16/run.log" 2>/dev/null)
	echo "--- pccap: the host's last player index ${most:-none} (15: 16 players); the 17th's ticks played ${full:-0}"
	echo "  the host's game: $(grep -ac "network test: tick" "$hostlog") s of play logged; each client's:" \
		$(for i in $(seq 1 15); do grep -ac "network test: tick" "$out/cap/c$i/run.log" 2>/dev/null; done | sort -n | head -1) "s at least"
	if [ "${most:-0}" != 15 ]; then
		our_results="$our_results pccap:players-${most:-none}"
	elif [ "${full:-0}" != 0 ]; then
		our_results="$our_results pccap:seventeenth-played"
	else
		our_results="$our_results pccap:pass"
	fi
	;;
browserboth)
	# one server browser, both kinds (the owner, Oct 19 2026), each tagged:
	# an OpenCE host, our Vita host for Vitas and our Vita host for Vitas and
	# PC, all public through one broker; three of our clients (no setting
	# changed) each join one of them from the same list, and a fourth with
	# Browser shows Vita games lists only the Vitas'. Then on one LAN the
	# same three hosts, our client's System Link list tagged
	broker
	shared_broker=$broker_address
	mkdir -p "$out/both"
	run_seconds=$seconds
	both_pids=
	for spec in "pc opence XpPcHost" "vita ours XpVitaHost" "vitapc ours XpVitaPc"; do
		set -- $spec
		subnet=$((subnet + 1)); side "$subnet"
		extra=
		[ "$1" = vitapc ] && extra=HALO_NET_HOST_CROSSPLAY=true
		run_copy "both/host-$1" "$side_machine" "$2" "192.168.$subnet.2" "$shared_broker" $(host_env "$2" net) \
			HALO_NET_LOBBY_NAME=$3 HALO_NETWORK_TEST_SCORE=50 $extra
		both_pids="$both_pids $last_pid"
	done
	sleep 15
	for spec in "join-pc tag:PC" "join-vita XpVitaHost" "join-vitapc XpVitaPc" "filter-vita -"; do
		set -- $spec
		subnet=$((subnet + 1)); side "$subnet"
		extra="HALO_NETWORK_TEST_PUBLIC_NAME=$2"
		[ "$1" = filter-vita ] && extra="HALO_NET_BROWSER_FILTER=vita HALO_NETWORK_TEST_PUBLIC_NAME=nothing-joined"
		run_copy "both/$1" "$side_machine" ours "192.168.$subnet.2" "$shared_broker" $(client_env ours net 70) $extra
		both_pids="$both_pids $last_pid"
	done
	# the LAN
	subnet=$((subnet + 1)); n=$subnet
	side "$n"
	run_copy both/lan-pc "$side_machine" opence "192.168.$n.2" 198.51.100.1 $(host_env opence lan) HALO_NET_LOBBY_NAME=XpLanPc
	both_pids="$both_pids $last_pid"
	i=3
	for spec in "lan-vita" "lan-vitapc"; do
		lan_machine "$n" $i; i=$((i + 1))
		extra=
		[ "$spec" = lan-vitapc ] && extra=HALO_NET_HOST_CROSSPLAY=true
		run_copy "both/$spec" "$held" ours "192.168.$n.$((i - 1))" 198.51.100.1 $(host_env ours lan) HALO_NET_LOBBY_NAME=Xp$spec $extra
		both_pids="$both_pids $last_pid"
	done
	sleep 10
	lan_machine "$n" $i
	run_copy both/lan-client "$held" ours "192.168.$n.$i" 198.51.100.1 HALO_NET_ONLINE=false HALO_NETWORK_TEST=join \
		HALO_NETWORK_TEST_JOIN_FILE=never-made HALO_TEST_INPUT=bot:77
	both_pids="$both_pids $last_pid"
	sleep 120
	for p in $both_pids; do kill "$p" 2>/dev/null; done
	wait $both_pids 2>/dev/null
	result=pass
	rows=$(grep -ah "network test: browser row" "$out/both/join-pc/run.log" 2>/dev/null | sed 's/^.*browser row //' | sort -u)
	echo "--- browserboth: the server browser's rows (one of our clients)"; echo "$rows" | sed 's/^/  /'
	echo "$rows" | grep -q '^\[PC[A-Z ]*\] "' || result=pc-row
	echo "$rows" | grep -q '^\[VITA\] "XpVitaHost"' || result=vita-row
	echo "$rows" | grep -q '^\[VITA+PC\] "XpVitaPc"' || result=vitapc-row
	for spec in "join-pc tag:PC crossplay" "join-vita XpVitaHost legacy" "join-vitapc XpVitaPc crossplay"; do
		set -- $spec
		log=$out/both/$1/run.log debug=$out/both/$1/data/debug.txt
		ticks=$(grep -a "network test: tick" "$log" 2>/dev/null | grep -aEc "player [0-9]+: \(.* player [0-9]+: \(")
		wire=$(cat "$log" "$debug" 2>/dev/null | grep -ac "network: the $3")
		[ "$3" = legacy ] && wire=$(cat "$log" "$debug" 2>/dev/null | grep -ac "joining a host of network version 18[[:space:]]*$")
		echo "  $1: joined $2, ${ticks:-0} s with two players alive, on its wire: ${wire:-0}"
		{ [ "${ticks:-0}" -ge 20 ] && [ "${wire:-0}" -ge 1 ]; } || result="$1-ticks-$ticks-wire-$wire"
	done
	frows=$(grep -ah "network test: browser row" "$out/both/filter-vita/run.log" 2>/dev/null | sed 's/^.*browser row //' | sort -u)
	echo "  Browser shows Vita games:"; echo "$frows" | sed 's/^/    /'
	{ echo "$frows" | grep -q 'XpVitaHost' && echo "$frows" | grep -q 'XpVitaPc' && ! echo "$frows" | grep -q '^\[PC'; } ||
		result=filter
	lan=$(grep -ah "network test: the System Link list:" "$out/both/lan-client/run.log" 2>/dev/null | tail -1 |
		sed 's/^.*System Link list: //')
	echo "  the System Link list: ${lan:-none}"
	{ echo "$lan" | grep -q '\[OPENCE\]' && echo "$lan" | grep -q '\[VITA\] "Xplan-vita"' &&
		echo "$lan" | grep -q '\[VITA+PC\] "Xplan-vitapc"'; } || result=lan-tags
	# (the Vitas' games first, then the PCs': no Vita game after the OpenCE one)
	echo "$lan" | grep -q '\[OPENCE\].*\[VITA' && result=lan-order
	our_results="$our_results browserboth:$result"
	echo "--- the brokers' topics"; broker_topics
	;;
crosspassword)
	# a password on our Vita's game for Vitas and PC: listed locked on
	# OpenCE's topics (its token sealed with OpenCE's Argon2id, three passes:
	# p2p_lobby_test checks the key byte for byte); OpenCE's browser leaves a
	# locked game alone; our client with a wrong password is refused, with
	# the right one plays
	pw="HALO_NET_HOST_CROSSPLAY=true HALO_NET_LOBBY_PASSWORD=xpsecret"
	host_extra=$pw client_extra="HALO_NETWORK_TEST=join-public:wrongpw" pair pw-wrong net ours ours
	host_extra=$pw client_extra="HALO_NETWORK_TEST=join-public:xpsecret" pair pw-right net ours ours
	host_extra=$pw pair pw-opence net ours opence
	wait_pairs
	wrong=$(grep -ac "the password is not that game's" "$out/pw-wrong/client/run.log" "$out/pw-wrong/client/data/debug.txt" 2>/dev/null |
		awk -F: '{ s += $NF } END { print s + 0 }')
	wrong_played=$(grep -ac "network test: player 1 is named" "$out/pw-wrong/host/run.log" 2>/dev/null)
	echo "--- pw-wrong: refused ${wrong:-0} times; the host's second player: ${wrong_played:-0}"
	{ [ "${wrong:-0}" -ge 1 ] && [ "${wrong_played:-0}" = 0 ]; } && our_results="$our_results pw-wrong:pass" ||
		our_results="$our_results pw-wrong:refused-$wrong-played-$wrong_played"
	echo "--- pw-right"
	check_pair pw-right && our_results="$our_results pw-right:pass" || our_results="$our_results pw-right:${check_stage:-?}"
	opence_joined=$(grep -ac "network test: player 1 is named" "$out/pw-opence/host/run.log" 2>/dev/null)
	locked=$(grep -ac "password" "$out/pw-opence/client/run.log" 2>/dev/null)
	echo "--- pw-opence: OpenCE's browser joined ${opence_joined:-0} (none: a locked game left alone); its lines on passwords: ${locked:-0}"
	[ "${opence_joined:-0}" = 0 ] && our_results="$our_results pw-opence:pass" || our_results="$our_results pw-opence:joined"
	;;
pcversion)
	# versions (docs/crossplay.md 5.2, 10.9): our Vita's game for Vitas and
	# PC announces 32; an OpenCE of 28 (XPPC_OLD_BUILDS) and of 29
	# (XPPC_OPENCE29, 8102e711) do not join it (their own checks: the host
	# sees no version in a join); our client refuses a host of a version
	# newer than it joins (33: our build standing in for a PC host,
	# HALO_TEST_CROSSPLAY_HOST=1, announcing 33: HALO_TEST_ANNOUNCE_VERSION) on a
	# LAN, telling the player it is newer, and drops its listing online
	ver_names="ver-ours-old28 ver-new33-ours ver-net-new33-ours"
	host_extra=HALO_NET_HOST_CROSSPLAY=true pair ver-ours-old28 lan ours oldopence
	if [ -x "${opence29:-}" ]; then
		ver_names="$ver_names ver-ours-old29"
		host_extra=HALO_NET_HOST_CROSSPLAY=true pair ver-ours-old29 lan ours opence29
	fi
	host_extra="HALO_TEST_CROSSPLAY_HOST=1 HALO_TEST_ANNOUNCE_VERSION=33" pair ver-new33-ours lan ours ours
	host_extra="HALO_TEST_CROSSPLAY_HOST=1 HALO_TEST_ANNOUNCE_VERSION=33" pair ver-net-new33-ours net ours ours
	wait_pairs
	for name in $ver_names; do
		hostlog=$out/$name/host/run.log clientlog=$out/$name/client/run.log clientdebug=$out/$name/client/data/debug.txt
		hosted=$(grep -ac "network test: hosting" "$hostlog" 2>/dev/null)
		second=$(grep -ac "network test: player 1 is named" "$hostlog" 2>/dev/null)
		said=
		case $name in
		ver-new33-ours) said=$(cat "$clientlog" "$clientdebug" 2>/dev/null |
			grep -ac "not joining a host of network version 33 (crossplay: this machine joins $our_versions): a newer PC build") ;;
		ver-net-new33-ours) said=$(grep -ac "dropped [0-9]* of another version" "$clientlog" 2>/dev/null) ;;
		*) said=1 ;;
		esac
		echo "--- $name: hosted ${hosted:-0}, a second player ${second:-0}, refusal said ${said:-0}"
		grep -ahE "network version|another version" "$clientlog" "$clientdebug" 2>/dev/null | sed 's/^[0-9.]* [0-9:]*  //' |
			sort | uniq -c | head -2 | sed 's/^/  /'
		if [ "${hosted:-0}" -ge 1 ] && [ "${second:-0}" = 0 ] && [ "${said:-0}" -ge 1 ]; then
			our_results="$our_results $name:pass"
		else
			our_results="$our_results $name:hosted-$hosted-second-$second-said-$said"
		fi
	done
	;;
net32)
	# OpenCE's network version 32 (docs/opence-net32-spec.md 6-7, 9.4)
	# - the aim assist policy, our client: OpenCE hosts blocking and allowing
	host_extra=HALO_NET_BLOCK_MOUSE_AIM_ASSIST=1 pair pol-opence-block lan opence ours
	host_extra=HALO_NET_BLOCK_MOUSE_AIM_ASSIST=0 pair pol-opence-allow lan opence ours
	# - our host's policy on the wire to an OpenCE client: a sniffer on the
	# host's port of its LAN (unencrypted on a LAN), the policy turned over
	# 25 s into the game
	host_extra="HALO_NET_HOST_CROSSPLAY=true HALO_TEST_AIM_ASSIST_TOGGLE=25" pair pol-ours-opence lan ours opence
	sniff_n=$subnet
	sniff_router=$(for r in $routers; do [ "${r#*:}" = "$sniff_n" ] && echo "${r%%:*}"; done)
	in_ns "$sniff_router" python3 -I - "l${sniff_n}_2" > "$out/pol-ours-opence/kind90.txt" 2>&1 <<'PYTHON' & pids="$pids $!"
import re, socket, struct, sys, time
# every TCP segment through the host's port: a distributed message of kind
# 90 (its header 12 bytes, type 2, in either byte order; kind, count, tick,
# block_mouse, reserved)
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
s.bind((sys.argv[1], 0))
pattern = re.compile(rb"(\x00\xc8|\xc8\x00)\x5a(.)(.{4})(..)(..)", re.S)
print("sniffing %s" % sys.argv[1], flush=True)
while True:
    frame = s.recv(65535)
    if len(frame) < 34 or frame[12:14] != b"\x08\x00":
        continue
    ip = frame[14:]
    header = (ip[0] & 15) * 4
    if ip[9] != 6:
        continue
    total = struct.unpack(">H", ip[2:4])[0]
    tcp = ip[header:total]
    data = tcp[(tcp[12] >> 4) * 4:]
    for m in pattern.finditer(data):
        print("kind90 %s:%d -> %s:%d count %d block %d reserved %d at %.3f" % (socket.inet_ntoa(ip[12:16]),
            struct.unpack(">H", tcp[0:2])[0], socket.inet_ntoa(ip[16:20]), struct.unpack(">H", tcp[2:4])[0],
            m.group(2)[0], struct.unpack("<H", m.group(4))[0], struct.unpack("<H", m.group(5))[0], time.time()), flush=True)
PYTHON
	# - our client refusing forged copies from our host, and playing on
	host_extra="HALO_NET_HOST_CROSSPLAY=true HALO_TEST_AIM_ASSIST_FORGE=1 HALO_TEST_AIM_ASSIST_TOGGLE=25" \
		pair pol-ours-ours lan ours ours
	# - the source port (spec 6.3, 6.4): an OpenCE client whose client port
	# another program holds, on our host
	hold_client_port=1 host_extra=HALO_NET_HOST_CROSSPLAY=true pair port-ours-opence lan ours opence
	# - a CTF game whose host deletes every object, the flags with them
	# (spec 9.4), 3 s into its game, on its console
	host_variants=ctf host_extra=HALO_TELNET_CONSOLE=1 pair ctfdel-opence-ours lan opence ours
	ctfdel_ns=$side_machine
	(
		for i in $(seq 1 90); do
			grep -aq "network test: starting the game\|network test: tick 1 " "$out/ctfdel-opence-ours/host/run.log" 2>/dev/null && break
			sleep 1
		done
		sleep 3
		coop_console "$ctfdel_ns" "object_destroy_all" && echo "object_destroy_all sent" >> "$out/ctfdel-opence-ours/commands.log"
	) & pids="$pids $!"
	test_names="pol-opence-block pol-opence-allow pol-ours-opence pol-ours-ours port-ours-opence"
	run_our_modes
	for name in pol-opence-block pol-opence-allow pol-ours-ours; do
		logs="$out/$name/client/run.log $out/$name/client/data/debug.txt $out/$name/late/run.log $out/$name/late/data/debug.txt"
		logs="$logs $out/$name/back/run.log $out/$name/back/data/debug.txt"
		blocked=$(cat $logs 2>/dev/null | grep -ac "aim assist policy from the host: mouse aim assist blocked")
		allowed=$(cat $logs 2>/dev/null | grep -ac "aim assist policy from the host: mouse aim assist allowed")
		echo "--- $name: our client took the host's policy: blocked $blocked, allowed $allowed"
		case $name in
		pol-opence-block) [ "${blocked:-0}" -ge 1 ] && [ "${allowed:-0}" = 0 ] || our_results="$our_results $name-policy:blocked-$blocked-allowed-$allowed" ;;
		pol-opence-allow) [ "${allowed:-0}" -ge 1 ] && [ "${blocked:-0}" = 0 ] || our_results="$our_results $name-policy:blocked-$blocked-allowed-$allowed" ;;
		pol-ours-ours)
			[ "${allowed:-0}" -ge 1 ] && [ "${blocked:-0}" -ge 1 ] || our_results="$our_results $name-policy:blocked-$blocked-allowed-$allowed"
			for why in "in a datagram" "its size" "its count" "its value" "its reserved bytes"; do
				refused=$(cat $logs 2>/dev/null | grep -ac "aim assist policy ignored: $why")
				echo "  forged, $why: refused $refused"
				[ "${refused:-0}" -ge 1 ] || our_results="$our_results $name-forged:$(echo "$why" | tr ' ' '-')"
			done ;;
		esac
	done
	# (our host: sent at each ready, as the game started, at the change)
	hostlogs="$out/pol-ours-opence/host/run.log $out/pol-ours-opence/host/data/debug.txt"
	ready=$(cat $hostlogs 2>/dev/null | grep -ac "aim assist policy sent to a client as it is ready")
	all_allowed=$(cat $hostlogs 2>/dev/null | grep -ac "aim assist policy sent to every client: mouse aim assist allowed")
	all_blocked=$(cat $hostlogs 2>/dev/null | grep -ac "aim assist policy sent to every client: mouse aim assist blocked")
	wire0=$(grep -ac "^kind90 192\.168\.$sniff_n\.2:.* count 1 block 0 reserved 0" "$out/pol-ours-opence/kind90.txt" 2>/dev/null)
	wire1=$(grep -ac "^kind90 192\.168\.$sniff_n\.2:.* count 1 block 1 reserved 0" "$out/pol-ours-opence/kind90.txt" 2>/dev/null)
	other=$(grep -a "^kind90" "$out/pol-ours-opence/kind90.txt" 2>/dev/null | grep -avc "count 1 block [01] reserved 0")
	echo "--- pol-ours-opence: our host sent the policy at $ready readies, to every client allowed $all_allowed and blocked" \
		"$all_blocked times; on the wire from it: block 0 $wire0, block 1 $wire1, otherwise $other"
	grep -a "^kind90" "$out/pol-ours-opence/kind90.txt" 2>/dev/null | head -8 | sed 's/^/  /'
	{ [ "${ready:-0}" -ge 2 ] && [ "${all_allowed:-0}" -ge 1 ] && [ "${all_blocked:-0}" -ge 1 ] &&
		[ "${wire0:-0}" -ge 2 ] && [ "${wire1:-0}" -ge 1 ]; } ||
		our_results="$our_results pol-ours-opence-sent:ready-$ready-all-$all_allowed-$all_blocked-wire-$wire0-$wire1"
	# (the held port: the client bound another, our host answered it there)
	answered=$(grep -ahc "datagrams come from port [0-9]*: answered there" "$out/port-ours-opence/host/run.log" \
		"$out/port-ours-opence/host/data/debug.txt" 2>/dev/null | awk '{ s += $1 } END { print s + 0 }')
	echo "--- port-ours-opence: our host answered a client at the port its datagrams came from: $answered"
	grep -ah "datagrams come from port" "$out/port-ours-opence/host/run.log" "$out/port-ours-opence/host/data/debug.txt" 2>/dev/null |
		head -3 | sed 's/^/  /'
	[ "${answered:-0}" -ge 1 ] || our_results="$our_results port-ours-opence-answered:none"
	# (the deleted flags: the command sent, our client on through it with no
	# assertion, its flags gone once)
	# (our copies: the client, the late one, the one back)
	ctflogs= ctfdebugs=
	for copy in client late back; do
		ctflogs="$ctflogs $out/ctfdel-opence-ours/$copy/run.log"; ctfdebugs="$ctfdebugs $out/ctfdel-opence-ours/$copy/data/debug.txt"
	done
	sent=$(grep -ac "object_destroy_all sent" "$out/ctfdel-opence-ours/commands.log" 2>/dev/null)
	ticks=$(cat $ctflogs 2>/dev/null | grep -ac "network test: tick [0-9]* ")
	flags_before=$(cat $ctflogs 2>/dev/null | grep -a "network test: items at tick" | grep -ac "flag")
	flags_gone=$(cat $ctfdebugs 2>/dev/null | grep -ac "network: a flag or ball deleted on the host's word")
	asserted=$(cat $ctflogs $ctfdebugs 2>/dev/null | grep -aEc "ASSERT|assertion|EXCEPTION|halt_and_catch_fire|egmentation fault")
	echo "--- ctfdel-opence-ours: object_destroy_all sent $sent; our copies $ticks ticks, item lines with a flag $flags_before," \
		"flags deleted on the host's word $flags_gone; assertions or faults $asserted"
	cat $ctfdebugs 2>/dev/null | grep -a "network: a flag or ball deleted" | sed 's/^[0-9.]* [0-9:]*  //' | head -4 | sed 's/^/  /'
	cat $ctflogs 2>/dev/null | grep -a "network test: items at tick" | cut -c1-150 | head -4 | sed 's/^/  /'
	if [ "${sent:-0}" = 0 ]; then
		controls_ok=0; echo "BROKEN ($mode): the OpenCE host's console took no command"
	elif [ "${asserted:-0}" != 0 ] || [ "${ticks:-0}" -lt 10 ] || [ "${flags_gone:-0}" = 0 ]; then
		our_results="$our_results ctfdel-opence-ours:asserted-$asserted-ticks-$ticks-flags-gone-$flags_gone"
	else
		our_results="$our_results ctfdel-opence-ours:pass"
	fi
	;;
net29)
	# the older version (OpenCE 8102e711, 29), each game by its host's rules:
	# our client on its LAN host; our host announcing 29 with its clients
	control lan-opence29-opence29 && pair lan-opence29-opence29 lan opence29 opence29
	test_names="lan-opence29-ours lan-ours29-opence29"
	pair lan-opence29-ours lan opence29 ours
	host_extra="HALO_NET_HOST_CROSSPLAY=true HALO_TEST_CROSSPLAY_HOST_VERSION=29" pair lan-ours29-opence29 lan ours opence29
	run_our_modes
	for name in lan-opence29-ours lan-ours29-opence29; do
		case $name in
		lan-opence29-ours) said=$(cat "$out/$name/client/run.log" "$out/$name/client/data/debug.txt" 2>/dev/null |
			grep -ac "network: the crossplay wire (joining a game from the list, a PC's game), network version 29's rules") ;;
		*) said=$(cat "$out/$name/host/run.log" "$out/$name/host/data/debug.txt" 2>/dev/null |
			grep -ac "network: the crossplay wire (hosting for Vitas and PC, a Vita's game), network version 29's rules") ;;
		esac
		echo "--- $name: played by 29's rules: $said"
		[ "${said:-0}" -ge 1 ] || our_results="$our_results $name-rules:not-29"
	done
	;;
crossfuzzhost)
	# the host's side fuzzed in a live game (docs/crossplay.md 7.4; not in
	# all): our host an AddressSanitizer + UBSan HALO_NET_FUZZ_TWIN build
	# (HALO_TEST_VITA), hosting for Vitas and PC, twinning one in
	# XPPC_FUZZ_RATE (2) of what its OpenCE clients send (their join, settings
	# requests, inputs, predictions, hit reports, the distributed batches),
	# on a LAN and over the internet (the browser); passes with no sanitizer
	# memory error and no crash, the host running to its end
	fz="HALO_NET_HOST_CROSSPLAY=true HALO_NET_FUZZ_RATE=${XPPC_FUZZ_RATE:-2} HALO_NET_FUZZ_DUMP=fuzz-dump.bin
		ASAN_OPTIONS=detect_leaks=0:halt_on_error=0:log_path=asan UBSAN_OPTIONS=print_stacktrace=1:log_path=ubsan"
	host_extra=$fz pair fuzz-lan lan ours opence
	host_extra=$fz pair fuzz-net net ours opence
	wait_pairs
	for name in fuzz-lan fuzz-net; do
		dir=$out/$name/host
		twins=$(grep -ahoE "net fuzz twin: [0-9]+ twins" "$dir/run.log" "$dir/data/debug.txt" 2>/dev/null | tail -1)
		asan=$(cat "$dir"/data/asan* 2>/dev/null | grep -ac "ERROR: AddressSanitizer")
		crash=$(grep -aEc "egmentation fault|SIGSEGV|SIGABRT|EXCEPTION halt|stack trace" "$dir/run.log" 2>/dev/null)
		ticks=$(grep -ac "network test: tick" "$dir/run.log" 2>/dev/null)
		ub=$(cat "$dir"/data/ubsan* 2>/dev/null | grep -a "runtime error" | sed 's/:[0-9]*:[0-9]*:/:/' | sort | uniq -c | sort -rn | head -5)
		echo "--- $name: ${twins:-no twin count}; AddressSanitizer errors ${asan:-0}; crashes ${crash:-0}; host ticks ${ticks:-0}"
		[ -n "$ub" ] && echo "$ub" | sed 's/^/  UBSan: /'
		if [ "${asan:-0}" = 0 ] && [ "${crash:-0}" = 0 ] && [ "${ticks:-0}" -ge 30 ]; then
			our_results="$our_results $name:pass"
		else
			our_results="$our_results $name:asan-$asan-crash-$crash-ticks-$ticks"
		fi
	done
	;;
crosscoophost)
	# co-op hosted by our Vita for Vitas and PC (T15: PC clients in a Vita's
	# co-op, experimental): our host's network co-op on XPPC_COOP_LEVEL (b30)
	# on the crossplay wire, an OpenCE client and one of ours; each must play
	# beside the host's player, the host its AI, nobody crash
	# (XPPC_COOPHOST_WIRE=legacy: the same game for Vitas alone, ours only - the
	# harness's own control)
	coophost_kinds="opence ours"
	coophost_wire=HALO_NET_HOST_CROSSPLAY=true
	[ "${XPPC_COOPHOST_WIRE:-}" = legacy ] && coophost_kinds="ours ours" coophost_wire=HALO_NET_HOST_CROSSPLAY=false
	coop_host_extra="$coophost_wire HALO_NET_COOP_PLAYERS=3 HALO_NET_COOP_LEVEL=$coop_level HALO_NETWORK_TEST=host:$coop_level HALO_NETWORK_TEST_START=20 HALO_TEST_INPUT=bot:1:look" coop_game coophost-ours ours "$coophost_kinds"
	wait_pairs
	hostlog=$out/coophost-ours/host/run.log hostdebug=$out/coophost-ours/host/data/debug.txt
	wire=$(cat "$hostlog" "$hostdebug" 2>/dev/null | grep -ac "network: the crossplay wire (hosting for Vitas and PC")
	result=pass
	for c in client client2; do
		log=$out/coophost-ours/$c/run.log
		played=$(grep -a "network test: tick" "$log" 2>/dev/null | grep -aEc "player [0-9]+: \(.* player [0-9]+: \(")
		echo "--- coophost-ours $c ($( [ $c = client ] && echo OpenCE || echo ours)): ${played:-0} s with two players alive"
		[ "${played:-0}" -ge 30 ] || result="$c-played-${played:-0}"
	done
	cat "$out"/coophost-ours/*/run.log "$out"/coophost-ours/*/data/debug.txt 2>/dev/null |
		grep -aqE "ERROR: AddressSanitizer|egmentation fault|SIGSEGV|SIGABRT|EXCEPTION halt" && result=crash
	[ "${wire:-0}" -ge 1 ] || [ "${XPPC_COOPHOST_WIRE:-}" = legacy ] || result="host-wire"
	echo "  our host on the crossplay wire: ${wire:-0}"
	our_results="$our_results coophost-ours:$result"
	;;
crossrefuse)
	# OpenCE hosts on LANs of their own, each with our client (section 6,
	# T11): a co-op game (a10), refused before any join with the reason
	# named; a Slayer game where our client asks for a second local player
	# (split screen: one player per Vita in PC games), refused and said, the
	# game played on; and a map our client lacks (wizard: its maps folder
	# without it), T3's refusal (crossplay_maps.c, feat/xp-map-identity),
	# looked for only when this build has its text
	pair refuse-split lan opence ours HALO_NETWORK_TEST_SECOND_PLAYER=3
	t3=0
	grep -aq "PC hosts can't send maps" "$vita" && t3=1
	if [ $t3 = 1 ]; then
		mkdir -p "$out/maps-nowizard"
		for f in "$data"/maps/*.map; do
			[ "$(basename "$f")" = wizard.map ] || ln -sfn "$f" "$out/maps-nowizard/$(basename "$f")"
		done
		client_maps=$out/maps-nowizard host_game=wizard pair refuse-map lan opence ours
	fi
	wait_pairs
	for name in refuse-split refuse-map; do
		clientlog=$out/$name/client/run.log clientdebug=$out/$name/client/data/debug.txt
		if [ "$name" = refuse-map ] && [ $t3 = 0 ]; then
			echo "--- refuse-map: not checked: this build has no T3 map refusals (feat/xp-map-identity's crossplay_maps.c)"
			continue
		fi
		joined=$(grep -ac "network test: joining" "$clientlog" 2>/dev/null)
		said=$(grep -ahiE "not a Vita|Vita-only|cannot join|refus|not joining|not adding|isn.t in your maps" "$clientlog" \
			"$clientdebug" 2>/dev/null | grep -v "/" | sed 's/^[0-9.]* [0-9:]*  //; s/^halo-linux: //' | sort -u | head -3)
		case $name in
		refuse-split)
			# (each copy of our client: not added, said once; never asked of
			# the host; the game played: crossplay_check.py's steps)
			got=$(cat "$out/$name"/*/run.log 2>/dev/null | grep -ac "crossplay: not adding local player 1")
			asked=$(cat "$out/$name"/*/run.log 2>/dev/null | grep -ac "network test: asking for a second local player")
			requested=$(cat "$out/$name"/*/data/debug.txt 2>/dev/null |
				grep -ac "requesting a player addition (controller index #1)")
			echo "--- $name: asked ${asked:-0}, refused-and-said ${got:-0}, requested of the host ${requested:-0}"
			if [ "${asked:-0}" = 0 ]; then
				result=never-asked-for-a-second-player
			elif [ "${got:-0}" != "${asked:-0}" ] || [ "${requested:-0}" != 0 ]; then
				result=second-player-said-$got-requested-$requested
			elif ! check_pair "$name"; then
				result=${check_stage:-play}
			else
				result=pass
			fi ;;
		refuse-map)
			got=$(grep -ahiE "(missing|do not have|don.t have|no map|couldn.t (find|load)|isn.t in your maps folder).*" \
				"$clientlog" "$clientdebug" 2>/dev/null | grep -i wizard | grep -v "/" | head -2)
			# (ticks played after the host's settings named its map: its game)
			ticks=$(sed -n "/join: the host's map is .*wizard/,\$ p" "$clientlog" 2>/dev/null | grep -ac "network test: tick [0-9]* ")
			echo "--- $name: our client played ${ticks:-0} ticks of the host's game; the refusal: ${got:-none}"
			if [ -n "$got" ] && [ "${ticks:-0}" = 0 ]; then
				result=pass
			elif [ "${ticks:-0}" != 0 ]; then
				result=played-instead-of-refusing
			else
				result=refusal-message
			fi ;;
		esac
		[ -n "$said" ] && echo "$said" | sed 's/^/  it said: /'
		our_results="$our_results $name:$result"
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
	# (an OpenCE host of network version 29 names a Custom Edition map by
	# its own name, custom_maps\<name>, and sends its version, its hash's
	# first 32 bits. Our client with <name>.map; with only
	# <name>_<8 hex digits>.map (as OpenCE saves a download beside another
	# version); with another version of <name>.map (one byte of its header's
	# checksum changed, so its hash); and with that other version as
	# <name>.map beside the host's as <name>_<digits>.map)
	ce_digits=$(python3 -I - "$(readlink -f "$ce_map")" <<'EOF'
import hashlib, sys
data = open(sys.argv[1], "rb").read()
whole = hashlib.blake2b(digest_size=32)
whole.update(b"OpenCE map 1")
whole.update(len(data).to_bytes(4, "little"))
for offset in range(0, len(data), 0x40000):
    whole.update(hashlib.blake2b(data[offset:offset + 0x40000], digest_size=32).digest())
print(whole.hexdigest()[:8])
EOF
)
	[ "$ce_digits" = 00000000 ] && ce_digits=00000001
	ce_version=$(printf '0x%08X' "0x$ce_digits")
	mkdir -p "$out/maps-with-ce-hashed" "$out/maps-with-ce-differ" "$out/maps-with-ce-beside"
	for f in "$data"/maps/*.map; do
		ln -sfn "$f" "$out/maps-with-ce-hashed/$(basename "$f")"
		ln -sfn "$f" "$out/maps-with-ce-differ/$(basename "$f")"
		ln -sfn "$f" "$out/maps-with-ce-beside/$(basename "$f")"
	done
	ln -sfn "$(readlink -f "$ce_map")" "$out/maps-with-ce-hashed/${ce_name}_$ce_digits.map"
	ln -sfn "$(readlink -f "$ce_map")" "$out/maps-with-ce-beside/${ce_name}_$ce_digits.map"
	cp "$(readlink -f "$ce_map")" "$out/maps-with-ce-differ/$ce_name.map"
	python3 -I - "$out/maps-with-ce-differ/$ce_name.map" <<'EOF'
import sys
with open(sys.argv[1], "r+b") as f:
    f.seek(0x64); b = f.read(1); f.seek(0x64); f.write(bytes([b[0] ^ 0x5A]))
EOF
	ln -sfn "$out/maps-with-ce-differ/$ce_name.map" "$out/maps-with-ce-beside/$ce_name.map"
	control xmap-stock-opence && pair xmap-stock-opence lan opence opence
	control xmap-ce-opence && host_game="custom_maps\\$ce_name" host_variants=slayer pair xmap-ce-opence lan opence opence
	test_names="xmap-stock-ours xmap-ce-ours xmap-cehashed-ours xmap-cebeside-ours"
	host_variants=slayer pair xmap-stock-ours lan opence ours
	client_maps=$out/maps-with-ce client_extra=HALO_CUSTOM_EDITION=1 host_game="custom_maps\\$ce_name" host_variants=slayer \
		pair xmap-ce-ours lan opence ours
	client_maps=$out/maps-with-ce-hashed client_extra=HALO_CUSTOM_EDITION=1 host_game="custom_maps\\$ce_name" host_variants=slayer \
		pair xmap-cehashed-ours lan opence ours
	client_maps=$out/maps-with-ce-beside client_extra=HALO_CUSTOM_EDITION=1 host_game="custom_maps\\$ce_name" host_variants=slayer \
		pair xmap-cebeside-ours lan opence ours
	client_extra=HALO_CUSTOM_EDITION=1 host_game="custom_maps\\$ce_name" host_variants=slayer pair xmap-cemissing-ours lan opence ours
	client_maps=$out/maps-with-ce-differ client_extra=HALO_CUSTOM_EDITION=1 host_game="custom_maps\\$ce_name" host_variants=slayer \
		pair xmap-cediffer-ours lan opence ours
	run_our_modes
	# (the map by its own name with its version, and our client's file of
	# that version found for it: <name>.map, or <name>_<digits>.map)
	for name in xmap-ce-ours xmap-cehashed-ours xmap-cebeside-ours; do
		named=$(grep -aoE "join: the host's map is [^ ]* \(copy 0x[0-9A-F]{8}\)" "$out/$name/client/run.log" 2>/dev/null |
			head -1 | sed 's/^join: //')
		file=$ce_name.map
		[ "$name" = xmap-ce-ours ] || file=${ce_name}_$ce_digits.map
		hashed=$(grep -aoE "crossplay: [^ ]*$file hashed: version $ce_digits" "$out/$name/client/data/debug.txt" 2>/dev/null | head -1)
		echo "--- $name: ${named:-no map named}; ${hashed:-$file not found of the version of the host}"
		case $named in
		*"custom_maps"?"$ce_name (copy $ce_version)") [ -n "$hashed" ] || our_results="$our_results $name:version-not-found" ;;
		*) our_results="$our_results $name:map-not-named-$(echo "$named" | tr ' ' '_')" ;;
		esac
	done
	# (another version: refused as such, never joined)
	name=xmap-cediffer-ours
	want="Your $ce_name.map isn't the same version as the PC host's"
	got=$(grep -ahF "$want" "$out/$name/client/run.log" "$out/$name/client/data/debug.txt" 2>/dev/null | head -1)
	joined=$(sed -n "/join: the host's map is custom_maps.$ce_name[_ ]/,\$ p" "$out/$name/client/run.log" 2>/dev/null |
		grep -ac "network test: tick [0-9]* ")
	echo "--- $name: our client played ${joined:-0} ticks; the refusal: ${got:-none}"
	if [ -n "$got" ] && [ "${joined:-0}" = 0 ]; then
		our_results="$our_results $name:pass"
	else
		our_results="$our_results $name:refusal-${got:+said}-ticks-${joined:-0}"
	fi
	# (the map our client lacks: refused, the map named, never asked of the
	# host, never joined)
	name=xmap-cemissing-ours
	want="The PC host is playing the map $ce_name, which isn't in your maps folder"
	got=$(grep -ahF "$want" "$out/$name/client/run.log" "$out/$name/client/data/debug.txt" 2>/dev/null | head -1)
	# (ticks played after the host's settings named its map: its game)
	joined=$(sed -n "/join: the host's map is custom_maps.$ce_name[_ ]/,\$ p" "$out/$name/client/run.log" 2>/dev/null |
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
crosscoop)
	# PC co-op (section 6.2, the owner's Oct 11 2026): each PC build hosts a
	# network co-op game; our client answers yes (asked once), plays, has the
	# host's AI, its checkpoint and its BSP switch; four players on OpenCE;
	# answering no, none of it played
	control coop-opence-opence && coop_game coop-opence-opence opence opence
	coop_game coop-opence-ours opence ours
	coop_game coop-chupa-ours chupa ours
	coop_damnation=coop-damnation-ours
	plays_ours "$damnation" "DamnationCE's co-op game" && coop_game coop-damnation-ours damnation ours || coop_damnation=
	coop_game coop4-opence-ours opence "ours opence opence"
	coop_game coop-opence-no opence ours HALO_CROSSPLAY_LARGE_ANSWER=no
	wait_pairs
	for name in $control_names; do
		played=$(grep -a "network test: tick" "$out/$name/client/run.log" 2>/dev/null | grep -aEc "player [0-9]+: \(.* player [0-9]+: \(")
		echo "--- control $name: ${played:-0} s with two players alive"
		[ "${played:-0}" -ge 30 ] || { controls_ok=0; echo "BROKEN ($mode): the control $name played ${played:-0} s"; }
	done
	for name in coop-opence-ours coop-chupa-ours $coop_damnation coop4-opence-ours coop-opence-no; do
		grep -aq "network test: hosting" "$out/$name/host/run.log" ||
			{ controls_ok=0; echo "BROKEN ($mode): the host of $name never hosted"; }
		coop_check "$name" $([ "$name" = coop-opence-no ] && echo no)
		our_results="$our_results $name:$coop_result"
	done
	# (the four-player game: every copy saw all four alive at once)
	name=coop4-opence-ours
	all=$(grep -a "network test: tick" "$out/$name/client/run.log" 2>/dev/null |
		grep -aEc "player [0-9]+: \(.*player [0-9]+: \(.*player [0-9]+: \(.*player [0-9]+: \(")
	echo "--- $name: our client's seconds with all four players alive: ${all:-0}"
	if [ "${all:-0}" -lt 15 ]; then
		our_results=$(echo "$our_results" | sed "s/ $name:pass/ $name:four-players-alive-${all:-0}-s/")
	fi
	# (the record of each: difficulty and co-op switches, OpenCE's network
	# version 19's cooperative_flags; its network test leaves PLAYER
	# COLLISIONS on, which only Server Setup turns off)
	grep -ah "the host's co-op game on difficulty" "$out"/coop*-ours/client/run.log 2>/dev/null | sort | uniq -c |
		sed 's/^/  /'
	;;
crosscoopcensus)
	# section 6.2's numbers: our client's census in OpenCE co-op games on the
	# campaign's levels, 2 and 4 players, the host's extra enemies as set
	levels=${XPPC_COOP_LEVELS:-}
	if [ -z "$levels" ]; then
		for l in a10 a30 a50 b30 b40 c10 c20 c40 d20 d40; do [ -f "$data/maps/$l.map" ] && levels="$levels $l"; done
	fi
	enemies=${XPPC_COOP_ENEMIES:-per_player}
	case $enemies in
	per_player) enemies_env="HALO_NET_COOP_ENEMIES_MODE=per_player" ;;
	multiplier:*) enemies_env="HALO_NET_COOP_ENEMIES_MODE=multiplier HALO_NET_COOP_ENEMIES_MULTIPLIER=${enemies#multiplier:}" ;;
	none) enemies_env="HALO_NET_COOP_ENEMIES_MODE=none" ;;
	*) echo "BROKEN ($mode): XPPC_COOP_ENEMIES is per_player, none or multiplier:N"; exit 2 ;;
	esac
	parallel=${XPPC_COOP_PARALLEL:-3}
	names= running=0
	for l in $levels; do
		for players in 2 4; do
			name=census-$l-${players}p
			names="$names $name"
			kinds=ours; [ $players = 4 ] && kinds="ours opence opence"
			coop_bsp_walk=1 coop_game_level=$l coop_host_extra="$enemies_env" coop_game "$name" opence "$kinds"
			running=$((running + 1))
			[ $running -ge "$parallel" ] && { wait_pairs; running=0; }
		done
	done
	wait_pairs
	echo "--- census: the host's extra enemies $enemies; our client's most in each game (docs/crossplay.md 6.2)"
	printf '  %-18s %8s %6s %10s %11s %9s %8s %5s %s\n' game objects units living-ai ai-tracked untracked refused bsps played-s
	for name in $names; do
		log=$out/$name/client/run.log
		row=$(grep -a "network test: co-op tick" "$log" 2>/dev/null | sed -n 's/.*| bsp \([0-9]*\) .*| objects \([0-9]*\) units \([0-9]*\) living ai \([0-9]*\) | ai tracked [0-9]* most \([0-9]*\) untracked \([0-9]*\) rejected \([0-9]*\).*/\1 \2 \3 \4 \5 \6 \7/p' |
			awk '{ b[$1] = 1; if ($2 > o) o = $2; if ($3 > u) u = $3; if ($4 > a) a = $4; if ($5 > m) m = $5; un = $6; re = $7 }
				END { n = 0; for (k in b) n++; printf "%d %d %d %d %d %d %d", o, u, a, m, un, re, n }')
		played=$(grep -a "network test: tick" "$log" 2>/dev/null | grep -aEc "player [0-9]+: \(.* player [0-9]+: \(")
		set -- $row
		printf '  %-18s %8s %6s %10s %11s %9s %8s %5s %s\n' "$name" "${1:-?}" "${2:-?}" "${3:-?}" "${4:-?}" "${5:-?}" "${6:-?}" \
			"${7:-?}" "${played:-0}"
		result=pass
		if cat "$log" "$out/$name/client/data/debug.txt" 2>/dev/null |
			grep -aqE "ERROR: AddressSanitizer|egmentation fault|stack trace|SIGSEGV|SIGABRT|EXCEPTION halt"; then
			result=crashed
			grep -ahE "EXCEPTION halt|egmentation fault" "$log" "$out/$name/client/data/debug.txt" 2>/dev/null | head -2 |
				sed 's/^/    /'
		elif [ "${played:-0}" -lt 30 ]; then
			result=played-${played:-0}-s
		elif [ "${5:-1}" != 0 ] || [ "${6:-1}" != 0 ]; then
			result=untracked-${5:-?}-refused-${6:-?}
		fi
		our_results="$our_results $name:$result"
	done
	;;
crosscoopfuzz)
	# the co-op handlers meet mutated twins of a PC co-op host's real
	# messages, in a running game (net_fuzz_twin.h): no sanitizer error, no
	# crash, our client to its end
	rate=${XPPC_FUZZ_RATE:-2}
	fuzz_env() { echo "HALO_NET_FUZZ_RATE=$1 HALO_NET_FUZZ_SEED=$2 HALO_NET_FUZZ_DUMP=fuzzdump.txt ${3:+HALO_NET_FUZZ_KINDS=$3}"; }
	coop_game fuzz-opence-ours opence ours "$(fuzz_env "$rate" 11 16,64-77)"
	coop_game fuzz-chupa-ours chupa ours "$(fuzz_env "$rate" 12 16,64-77)"
	coop_game fuzz-damnation-ours damnation ours "$(fuzz_env "$rate" 13 16,64-77)"
	coop_game fuzz4-opence-ours opence "ours opence opence" "$(fuzz_env "$rate" 14 16,64-77)"
	coop_game fuzzall-opence-ours opence ours "$(fuzz_env 3 15)"
	wait_pairs
	for name in fuzz-opence-ours fuzz-chupa-ours fuzz-damnation-ours fuzz4-opence-ours fuzzall-opence-ours; do
		log=$out/$name/client/run.log debug=$out/$name/client/data/debug.txt
		twins=$(grep -ahoE "net fuzz twin: [0-9]+ twins" "$log" 2>/dev/null | tail -1 | grep -oE "[0-9]+")
		asan=$(grep -ac "ERROR: AddressSanitizer" "$log" 2>/dev/null)
		ubsan=$(grep -aE "runtime error" "$log" 2>/dev/null | sed 's/^.*\/\(source\|port\)/\1/; s/: runtime error.*//' | sort | uniq -c |
			tr '\n' ';')
		ticks=$(grep -ac "network test: co-op tick" "$log" 2>/dev/null)
		crashed=$(cat "$log" "$debug" 2>/dev/null | grep -acE "egmentation fault|SIGSEGV|SIGABRT|stack trace|Aborted|EXCEPTION halt")
		echo "--- $name: twins ${twins:-0}; co-op seconds ${ticks:-0}; AddressSanitizer errors ${asan:-0}; crash lines" \
			"${crashed:-0}; UBSan: ${ubsan:-none}"
		result=pass
		if [ "${asan:-0}" != 0 ]; then
			result=addresssanitizer
			grep -a -A12 "ERROR: AddressSanitizer" "$log" | head -16 | sed 's/^/  /'
		elif [ "${crashed:-0}" != 0 ]; then
			result=crashed
		elif [ "${twins:-0}" -lt 16 ] || [ "${ticks:-0}" -lt 30 ]; then
			result=too-few-twins-${twins:-0}-or-co-op-seconds-${ticks:-0}
		fi
		our_results="$our_results $name:$result"
	done
	;;
crossctf)
	# Capture the Flag on PC hosts, to the game's end (seconds long in the
	# network test: no time for the spawn, damage and rejoin steps, and its
	# kills do not score), the scoreboard at the end the same on both
	control ctf-opence-opence && host_variants=ctf pair ctf-opence-opence lan opence opence
	host_variants=ctf pair ctf-opence-ours lan opence ours
	host_variants=ctf pair ctf-chupa-ours lan chupa ours
	wait_pairs
	for name in $control_names ctf-opence-ours ctf-chupa-ours; do
		check_pair "$name" > /dev/null
		failed=$(python3 -I - "$out/$name/check.json" <<'EOF'
import json, sys
steps = json.load(open(sys.argv[1]))["steps"]
import re
failed = [s for s in ("discover", "signal", "version", "join", "settings", "play", "kills", "end_of_game",
    "stability") if not steps.get(s, {}).get("pass")]
# (the score step's scoreboards: host's and client's alike, whoever won)
boards = re.findall(r"(host|client) (\{[^}]*\})", steps.get("score", {}).get("detail", ""))
if len(boards) < 2 or len({b for _, b in boards}) != 1:
    failed.append("scoreboard")
print(" ".join(failed))
EOF
)
		echo "--- $name: ${failed:+failed: $failed}${failed:-played to the end, the scoreboards alike}"
		grep -aE "^  $name (play|kills|score|end_of_game|stability):" "$out/$name/check.txt" | sed 's/^/  /'
		case " $control_names " in
		*" $name "*) [ -z "$failed" ] || { controls_ok=0; echo "BROKEN ($mode): the control $name failed: $failed"; } ;;
		*) [ -n "$failed" ] && our_results="$our_results $name:$(echo "$failed" | tr ' ' ',')" ||
			our_results="$our_results $name:pass" ;;
		esac
	done
	;;
crossold)
	# older PC hosts (XPPC_OLD_BUILDS: 28 or 26) since the crossplay wire
	# joins 29 alone: seen, not joined, the player told why; over the internet never
	# listed (OpenCE's listing carries its version)
	old_names="old-lan-opence old-net-opence"
	pair old-lan-opence lan oldopence ours
	pair old-net-opence net oldopence ours
	if [ -x "$old_chupa" ]; then
		old_names="$old_names old-lan-chupa"
		pair old-lan-chupa lan oldchupa ours
	fi
	wait_pairs
	old_version=$(pc_version "$old_opence")
	for name in $old_names; do
		hostlog=$out/$name/host/run.log clientlog=$out/$name/client/run.log
		clientdebug=$out/$name/client/data/debug.txt
		hosted=$(grep -ac "network test: hosting" "$hostlog" 2>/dev/null)
		players=$(grep -ac "network test: player 1 is named" "$hostlog" 2>/dev/null)
		# (the network test's join, not its tick lines: the main menu's own
		# scenario logs those before any game)
		joined=$(grep -ac "network test: joining" "$clientlog" 2>/dev/null)
		said=$(cat "$clientlog" "$clientdebug" 2>/dev/null |
			grep -ac "not joining a host of network version ${old_version:-[0-9]*} (crossplay: this machine joins $our_versions): an older PC build")
		told=$(cat "$clientlog" "$clientdebug" 2>/dev/null | grep -ac "runs an older version of OpenCE or ChupathingyCE")
		listed=$(grep -ac "the public games list" "$clientlog" 2>/dev/null)
		result=pass
		if [ "${hosted:-0}" = 0 ]; then
			controls_ok=0; echo "BROKEN ($mode): the older host of $name never hosted"
		fi
		case $name in
		old-net-*)
			# (heard, and dropped for its version: the browser's summary)
			dropped=$(grep -ac "dropped [0-9]* of another version" "$clientlog" 2>/dev/null)
			{ [ "${listed:-0}" = 0 ] && [ "${dropped:-0}" -ge 1 ] && [ "${players:-0}" = 0 ] && [ "${joined:-0}" = 0 ]; } ||
				result="listed-$listed-dropped-$dropped-players-$players-joined-$joined" ;;
		*)
			{ [ "${said:-0}" -ge 1 ] && [ "${told:-0}" -ge 1 ] && [ "${players:-0}" = 0 ] && [ "${joined:-0}" = 0 ]; } ||
				result="said-$said-told-$told-players-$players-joined-$joined" ;;
		esac
		echo "--- $name (network version ${old_version:-?}): $result"
		grep -ahE "not joining|older version of OpenCE|of another version|the public games list" "$clientlog" "$clientdebug" 2>/dev/null |
			sed 's/^[0-9.]* [0-9:]*  //; s/^halo-linux: //' | sort | uniq -c | head -4 | sed 's/^/  /'
		our_results="$our_results $name:$result"
	done
	echo "--- the brokers' topics"; broker_topics
	;;
crossbig)
	# large PC games (section 6, the owner's Oct 11 2026): an OpenCE host's
	# fast setup takes 128 players, a maximum above 16: our client joins it
	# after asking once (HALO_CROSSPLAY_LARGE_ANSWER answers), on a LAN (the
	# lobby asks) and from the server browser (the browser asks; the lobby
	# then not), and plays the whole game; answering no, it plays none of it
	test_names="big-lan-yes big-net-yes"
	pair big-lan-yes lan opence ours HALO_CROSSPLAY_LARGE_ANSWER=yes
	pair big-net-yes net opence ours HALO_CROSSPLAY_LARGE_ANSWER=yes
	pair big-lan-no lan opence ours HALO_CROSSPLAY_LARGE_ANSWER=no
	run_our_modes
	results=
	for r in $our_results; do
		name=${r%%:*} result=${r#*:}
		clientlog=$out/$name/client/run.log clientdebug=$out/$name/client/data/debug.txt
		asked=$(grep -ac "crossplay: asking about the large PC game" "$clientlog" 2>/dev/null)
		experimental=$(grep -ac "joining the large PC game .*experimental\|playing the large PC game" "$clientdebug" 2>/dev/null)
		badged=$(grep -ac "the public games list .* badge PC EXPERIMENTAL" "$clientlog" 2>/dev/null)
		echo "--- $name: asked ${asked:-0} times; experimental lines ${experimental:-0}; listed with the badge ${badged:-0}"
		if [ "$result" = pass ] && [ "${asked:-0}" != 1 ]; then
			result=asked-$asked-times
		elif [ "$result" = pass ] && [ "$name" = big-net-yes ] && [ "${badged:-0}" = 0 ]; then
			result=listing-not-badged-experimental
		elif [ "$result" = pass ] && [ "${experimental:-0}" = 0 ]; then
			result=not-said-experimental
		fi
		results="$results $name:$result"
	done
	our_results=$results
	name=big-lan-no
	clientlog=$out/$name/client/run.log clientdebug=$out/$name/client/data/debug.txt
	ticks=$(grep -ac "network test: tick [0-9]* " "$clientlog" 2>/dev/null)
	asked=$(grep -ac "crossplay: asking about the large PC game" "$clientlog" 2>/dev/null)
	left=$(grep -ac "leaving the large PC game .*the player said no" "$clientdebug" 2>/dev/null)
	echo "--- $name: asked ${asked:-0} times, left ${left:-0}, played ${ticks:-0} ticks"
	if [ "${asked:-0}" -ge 1 ] && [ "${left:-0}" -ge 1 ] && [ "${ticks:-0}" = 0 ]; then
		our_results="$our_results $name:pass"
	else
		our_results="$our_results $name:asked-$asked-left-$left-ticks-$ticks"
	fi
	grep -aq "network test: hosting" "$out/$name/host/run.log" ||
		{ controls_ok=0; echo "BROKEN ($mode): the OpenCE host of $name never hosted"; }
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
