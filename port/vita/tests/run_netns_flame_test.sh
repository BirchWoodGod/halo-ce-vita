#!/bin/bash
# The tester's flaming helmet (port/linux/src/p2p_flame.inc, its proofs in
# the game's own messages: port/linux/game/flame_net.c; drawn by
# port/linux/game/flaming_helmet.c), end to end. The network scenarios run
# run_netns_online_test.sh's modes in namespaces (local broker, STUN and
# relay; nothing reaches a public service), each copy with a friend identity
# made here and the same tester list, signed with a TEST key the build
# trusts (configure.py --testers-test-key, its private half
# HALO_TEST_TESTERS_KEY; never the owner's):
#
#   allowed   online (code): the host and the first joiner are listed testers
#             wearing it; the second joiner is not listed and forces the
#             claim and its answers (HALO_TEST_FLAME_FORCE=1). Each listed
#             player's flame checked and drawn by the others; the forced one
#             by nobody, the host refusing to relay its proofs.
#   modified  online: the host is not listed and is a modified one
#             (HALO_TEST_FLAME_FORGE_HOST=1): it claims the flame for every
#             player and answers every ask itself with a listed key it does
#             not hold, relaying unchecked; the first joiner is a listed
#             tester. The joiners refuse every forged proof, draw no other
#             player's flame.
#   lan       System Link (many, two joiners on the host's LAN, online off):
#             as allowed.
#   adhoc     ad hoc (an emulated ad hoc group, online off): the host listed,
#             the joiner not listed and forcing it: the joiner checks and
#             draws the host's, the host refuses the joiner's.
#   coop      co-op campaign online (a10): host and joiner both listed: each
#             checks and draws the other's (the Master Chief's).
#   offline   no network (no namespaces): a listed tester's own flame drawn
#             offline in multiplayer (Blood Gulch) and in the campaign (a10);
#             an unlisted machine's not, even with the option on.
#
#   run_netns_flame_test.sh [allowed|modified|lan|adhoc|coop|offline|all]
#
# HALO_TEST_TESTERS_KEY: the test key file (tools/release/make_update_key.py)
# whose public half the build was configured with; HALO_TEST_VITA the build
# (default build/linux/halo); HALO_TEST_DATA the maps' folder; HALO_TEST_OUT,
# HALO_TEST_KEEP as the online test's. Needs unshare -rn, ip, iptables.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
key=${HALO_TEST_TESTERS_KEY:?the test key file the build trusts (configure.py --testers-test-key)}
binary=${HALO_TEST_VITA:-$root/build/linux/halo}
data=${HALO_TEST_DATA:-$root/../data2276}
which=${1:-all}
status=0
fail() { echo "FAIL ($scenario): $*"; status=1; }

# machine index of a copy in its game (its log's "friends: in a game, machine N")
machine_of() { sed -n 's/.*friends: in a game, machine \([0-9]*\).*/\1/p' "$1" | head -1; }
verified() { grep -aq "flame: machine $2's flaming helmet verified" "$1"; }
# (a copy drew machine N's player's flame, or its own: flaming_helmet.c's HALO_TEST_FLAME_LOG lines)
drawn() { grep -aq "flame: drawn on player [0-9]* (machine $2)" "$1"; }
drawn_own() { grep -aq "flame: drawn on player [0-9]* (machine [0-9]*, this machine's own)" "$1"; }

# identities NAMES... -- LISTED...: each copy's friends/identity.dat in
# $out/<copy>/save and the list (signed with the test key) in each save folder
identities() {
	python3 - "$out" "$key" "$@" <<'EOF'
import hashlib, os, sys
sys.path.insert(0, os.path.join(os.environ["ROOT"], "tools/release"))
import ed25519_rfc8032 as ed
out, key_file, rest = sys.argv[1], sys.argv[2], sys.argv[3:]
cut = rest.index("--")
names, listed = rest[:cut], rest[cut + 1:]
lines = []
for name in names:
    seed = hashlib.sha256(f"flame-test-{name}".encode()).digest()
    body = b"HFID" + bytes([1]) + seed + (1).to_bytes(8, "big")
    os.makedirs(f"{out}/{name}/save/friends", exist_ok=True)
    open(f"{out}/{name}/save/friends/identity.dat", "wb").write(body + hashlib.sha256(body).digest())
    if name in listed:
        lines.append(f"tester {ed.public_key(seed).hex()} 0 {name}")
text = "halo-ce-vita-testers 1\nversion 1\n" + "".join(l + "\n" for l in lines)
seed = bytes.fromhex([l for l in open(key_file) if l.strip() and not l.startswith("#")][0].strip())
data = text.encode() + b"signature " + ed.sign(seed, text.encode()).hex().encode() + b"\n"
for name in names:
    open(f"{out}/{name}/save/testers.txt", "wb").write(data)
EOF
}

# expect VIEWER SUBJECT: VIEWER's copy checked and drew SUBJECT copy's flame;
# refuse VIEWER SUBJECT: it did neither
expect() {
	local vl=$out/$1/run.log m
	m=$(machine_of "$out/$2/run.log")
	[ -n "$m" ] || { fail "$2 was in no game"; return; }
	verified "$vl" "$m" || fail "$1 did not check $2's flame (machine $m)"
	drawn "$vl" "$m" || fail "$1 did not draw $2's flame (machine $m)"
}
refuse() {
	local vl=$out/$1/run.log m
	m=$(machine_of "$out/$2/run.log")
	[ -n "$m" ] || { fail "$2 was in no game"; return; }
	verified "$vl" "$m" && fail "$1 took $2's flame (machine $m)"
	drawn "$vl" "$m" && fail "$1 drew $2's flame (machine $m)"
}

summary() {
	local name
	for name in "$@"; do
		echo "  $name (machine $(machine_of "$out/$name/run.log")):"
		grep -a "flame:" "$out/$name/run.log" | grep -v "tester list\|particle" | sed 's/^.*flame:/    flame:/' |
			sed 's/drawn on player [0-9]* /drawn on player N /' | sort | uniq -c | sort -rn | head -8
	done
}

online() { # online MODE: run_netns_online_test.sh MODE with the flame settings
	HALO_TEST_OUT=$out HALO_TEST_KEEP=1 HALO_TEST_VITA=$binary HALO_TEST_DATA=$data \
		HALO_TEST_SECONDS=${HALO_TEST_SECONDS:-150} \
		HALO_TEST_HOST_ENV="${host_env:-} HALO_TEST_FLAME_LOG=1" \
		HALO_TEST_COOP_HOST_ENV="${host_env:-} HALO_TEST_FLAME_LOG=1" \
		HALO_TEST_JOIN_ENV="HALO_FLAMING_HELMET=1 HALO_TEST_FLAME_LOG=1" \
		"$here/run_netns_online_test.sh" "$1" > "$out/online.log" 2>&1
	echo "--- $scenario: the online test ($1) said: $(grep -c '^FAIL' "$out/online.log") failures"
	grep '^FAIL' "$out/online.log" | head -5
}

offline_run() { # offline_run NAME INIT COMMANDS [ENV...]
	local name=$1 init=$2 commands=$3 dir=$out/$1 file
	shift 3
	mkdir -p "$dir/data/maps"
	for file in "$(cd "$data" && pwd)"/maps/*.map; do ln -sfn "$file" "$dir/data/maps/"; done
	printf '%b\n' "$init" > "$dir/data/init.txt"
	(cd "$dir" && env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=${HALO_TEST_OFFLINE_SECONDS:-40} \
		HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false \
		HALO_NET_ONLINE=false HALO_FLAMING_HELMET=1 HALO_TEST_FLAME_LOG=1 HALO_TEST_COMMANDS="$commands" \
		HALO_SCREENSHOT_DIR="$shots" HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1 "$@" \
		timeout -k 5 180 "$binary" > "$dir/run.log" 2>&1)
	rm -rf "$dir/save/z" "$dir/save/u"
}

run_scenario() {
	scenario=$1
	out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/flame_netns}.$scenario.$$
	mkdir -p "$out"
	host_env=
	shots=${HALO_TEST_SHOTS:-$out/shots}
	mkdir -p "$shots"
	unset HALO_TEST_ENV_joiner HALO_TEST_ENV_joiner1 HALO_TEST_ENV_joiner2 HALO_TEST_SECOND_JOINER HALO_TEST_JOINERS
	case $scenario in
	allowed)
		identities host joiner joiner2 -- host joiner
		host_env="HALO_FLAMING_HELMET=1"
		export HALO_TEST_ENV_joiner2="HALO_TEST_FLAME_FORCE=1" HALO_TEST_SECOND_JOINER=1
		online code
		expect joiner host; expect joiner2 host; expect joiner2 joiner; expect host joiner
		refuse host joiner2; refuse joiner joiner2
		grep -aq "flame: refused to relay machine $(machine_of "$out/joiner2/run.log")'s flame proof" "$out/host/run.log" ||
			fail "the host did not refuse to relay the forced proof"
		drawn_own "$out/host/run.log" || fail "the host did not draw its own flame"
		summary host joiner joiner2
		;;
	modified)
		identities host joiner joiner2 -- joiner
		host_env="HALO_FLAMING_HELMET=1 HALO_TEST_FLAME_FORGE_HOST=1"
		export HALO_TEST_SECOND_JOINER=1
		online code
		grep -aq "flame: test: the modified host forged" "$out/host/run.log" || fail "the modified host forged nothing (no test)"
		for name in joiner joiner2; do
			grep -aq "flame: machine [0-9]*'s flame claim refused" "$out/$name/run.log" ||
				fail "$name did not refuse a forged proof"
			grep -a "flame: drawn on player" "$out/$name/run.log" | grep -vq "this machine's own" &&
				fail "$name drew another player's flame"
		done
		refuse joiner host; refuse joiner joiner2; refuse joiner2 host; refuse joiner2 joiner
		summary host joiner joiner2
		;;
	lan)
		identities host joiner1 joiner2 -- host joiner1
		host_env="HALO_FLAMING_HELMET=1"
		export HALO_TEST_ENV_joiner2="HALO_TEST_FLAME_FORCE=1" HALO_TEST_JOINERS=2
		online many
		expect joiner1 host; expect joiner2 host; expect joiner2 joiner1; expect host joiner1
		refuse host joiner2; refuse joiner1 joiner2
		grep -aq "Internet play: network thread started" "$out/joiner1/run.log" && fail "System Link ran internet play"
		summary host joiner1 joiner2
		;;
	adhoc)
		identities host joiner -- host
		host_env="HALO_FLAMING_HELMET=1"
		export HALO_TEST_ENV_joiner="HALO_TEST_FLAME_FORCE=1"
		online adhoc
		expect joiner host
		refuse host joiner
		grep -aq "flame: machine $(machine_of "$out/joiner/run.log")'s flame claim refused: its key is on no tester list" \
			"$out/host/run.log" || fail "the host did not refuse the unlisted joiner's proof"
		summary host joiner
		;;
	coop)
		identities host joiner -- host joiner
		host_env="HALO_FLAMING_HELMET=1"
		online coop
		expect joiner host; expect host joiner
		drawn_own "$out/host/run.log" || fail "the host did not draw its own Master Chief's flame"
		summary host joiner
		;;
	offline)
		identities tester other campaign -- tester campaign
		offline_run tester 'game_variant slayer\nmap_name levels\\test\\bloodgulch\\bloodgulch' \
			"L200:@flamecam 1.0 30;L260:@shot offline-multiplayer" HALO_BOTS=2
		drawn_own "$out/tester/run.log" || fail "a listed tester's own flame not drawn offline (multiplayer, bots)"
		offline_run other 'game_variant slayer\nmap_name levels\\test\\bloodgulch\\bloodgulch' "" HALO_BOTS=2
		grep -aq "flame: drawn" "$out/other/run.log" && fail "an unlisted machine drew a flame (the option on)"
		grep -aq "flame: tester list" "$out/other/run.log" || fail "the unlisted machine read no list (no test)"
		HALO_TEST_OFFLINE_SECONDS=${HALO_TEST_CAMPAIGN_SECONDS:-35} offline_run campaign 'map_name levels\\a10\\a10' \
			"L200:@skip;L250:(unit_exit_vehicle (player0));L500:@flamecam 1.0 30;L560:@shot offline-campaign"
		drawn_own "$out/campaign/run.log" || fail "a listed tester's own flame not drawn in the campaign (a10)"
		ls "$shots"/offline-*.bmp > /dev/null 2>&1 || fail "no screenshot"
		;;
	esac
	if [ $status = 0 ] && [ "${HALO_TEST_KEEP:-0}" != 1 ]; then rm -rf "$out"; else echo "output kept in $out"; fi
}

export ROOT=$root
case $which in
allowed|modified|lan|adhoc|coop|offline) run_scenario "$which" ;;
all) for s in offline allowed modified lan adhoc coop; do run_scenario "$s"; done ;;
*) echo "usage: $0 [allowed|modified|lan|adhoc|coop|offline|all]"; exit 2 ;;
esac
[ $status = 0 ] && echo "PASS"
exit $status
