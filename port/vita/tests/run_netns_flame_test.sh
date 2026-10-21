#!/bin/bash
# The tester's flaming helmet online (port/linux/src/p2p_flame.inc), end to
# end on one Linux machine with no internet: run_netns_online_test.sh's code
# mode (local broker, STUN and relay in namespaces; nothing reaches a public
# service) with a host and two joiners, each with a friend identity made
# here and the same tester list, signed with a TEST key the build trusts
# (configure.py --testers-test-key, its private half HALO_TEST_TESTERS_KEY;
# never the owner's):
#
#   allowed   the host and the first joiner are listed testers wearing it;
#             the second joiner is not listed and forces the claim and its
#             answers (HALO_TEST_FLAME_FORCE=1). Each listed player's flame
#             must be checked by the host and both others; nobody may draw
#             the forced one, and the host must refuse to relay its proofs.
#   modified  the host is not listed and is a modified one
#             (HALO_TEST_FLAME_FORGE_HOST=1): it claims the flame for every
#             player and answers every ask itself with a listed key it does
#             not hold, relaying unchecked; the first joiner is a listed
#             tester, the second is not. The joiners must refuse every forged
#             proof (and nobody's flame be drawn but what a real proof shows).
#
#   run_netns_flame_test.sh [allowed|modified|both]
#
# HALO_TEST_TESTERS_KEY: the test key file (tools/release/make_update_key.py)
# whose public half the build was configured with; HALO_TEST_VITA the build
# (default build/linux/halo); HALO_TEST_OUT, HALO_TEST_KEEP as the online
# test's. Needs unshare -rn, ip, iptables (run_netns_online_test.sh).
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
key=${HALO_TEST_TESTERS_KEY:?the test key file the build trusts (configure.py --testers-test-key)}
which=${1:-both}
status=0
fail() { echo "FAIL ($scenario): $*"; status=1; }

# machine index of a copy in its game (its log's "friends: in a game, machine N")
machine_of() { sed -n 's/.*friends: in a game, machine \([0-9]*\).*/\1/p' "$1" | head -1; }
verified() { grep -aq "flame: machine $2's flaming helmet verified" "$1"; }

run_scenario() {
	scenario=$1
	local out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/flame_netns}.$scenario.$$
	mkdir -p "$out"
	# identities (the friends' identity.dat, p2p_friends.c's) and the list
	python3 - "$out" "$key" "$scenario" <<'EOF'
import hashlib, os, subprocess, sys
sys.path.insert(0, os.path.join(os.environ.get("ROOT", "."), "tools/release"))
import ed25519_rfc8032 as ed
out, key_file, scenario = sys.argv[1:4]
listed = {"allowed": ("host", "joiner"), "modified": ("joiner",)}[scenario]
lines = []
for index, name in enumerate(("host", "joiner", "joiner2")):
    seed = hashlib.sha256(f"flame-test-{name}".encode()).digest()
    body = b"HFID" + bytes([1]) + seed + (1).to_bytes(8, "big")
    folder = f"{out}/{name}/save/friends"
    os.makedirs(folder, exist_ok=True)
    open(f"{folder}/identity.dat", "wb").write(body + hashlib.sha256(body).digest())
    public = ed.public_key(seed)
    open(f"{out}/{name}.public", "w").write(public.hex())
    if name in listed:
        lines.append(f"tester {public.hex()} 0 {name}")
text = "halo-ce-vita-testers 1\nversion 1\n" + "".join(l + "\n" for l in lines)
seed = bytes.fromhex([l for l in open(key_file) if l.strip() and not l.startswith("#")][0].strip())
data = text.encode() + b"signature " + ed.sign(seed, text.encode()).hex().encode() + b"\n"
for name in ("host", "joiner", "joiner2"):
    open(f"{out}/{name}/save/testers.txt", "wb").write(data)
EOF
	[ -f "$out/host/save/testers.txt" ] || { fail "the identities and list could not be made"; return; }
	local host_env join2_env
	if [ "$scenario" = allowed ]; then
		host_env="HALO_FLAMING_HELMET=1"
		join2_env="HALO_TEST_FLAME_FORCE=1"
	else
		host_env="HALO_FLAMING_HELMET=1 HALO_TEST_FLAME_FORGE_HOST=1"
		join2_env=
	fi
	HALO_TEST_OUT=$out HALO_TEST_KEEP=1 HALO_TEST_SECOND_JOINER=1 HALO_TEST_SECONDS=${HALO_TEST_SECONDS:-150} \
		HALO_TEST_HOST_ENV="$host_env" HALO_TEST_JOIN_ENV="HALO_FLAMING_HELMET=1" HALO_TEST_JOIN2_ENV="$join2_env" \
		"$here/run_netns_online_test.sh" code > "$out/online.log" 2>&1
	echo "--- $scenario: the online test said: $(grep -c '^FAIL' "$out/online.log") failures"
	grep '^FAIL' "$out/online.log" | head -5
	local hl=$out/host/run.log jl=$out/joiner/run.log j2l=$out/joiner2/run.log
	local h j j2
	h=$(machine_of "$hl"); j=$(machine_of "$jl"); j2=$(machine_of "$j2l")
	echo "machines: host $h, joiner $j, joiner2 $j2"
	if [ -z "$h" ] || [ -z "$j" ] || [ -z "$j2" ]; then
		fail "not all three copies were in the game"
	elif [ "$scenario" = allowed ]; then
		verified "$hl" "$j" || fail "the host did not check the listed joiner's flame"
		verified "$jl" "$h" || fail "the joiner did not check the host's flame"
		verified "$j2l" "$h" || fail "the second joiner did not check the host's flame"
		verified "$j2l" "$j" || fail "the second joiner did not check the first joiner's flame"
		grep -aq "flame: machine $j2's player claims the flaming helmet" "$hl" ||
			fail "the forced claim never reached the host's record"
		verified "$hl" "$j2" && fail "the host drew the unlisted player's forced flame"
		verified "$jl" "$j2" && fail "the joiner drew the unlisted player's forced flame"
		grep -aq "flame: refused to relay machine $j2's flame proof" "$hl" ||
			fail "the host did not refuse to relay the forced proof"
		grep -aq "flame: machine $j2's flame claim refused: its key is on no tester list" "$hl" ||
			fail "the host did not refuse the forced claim itself"
	else
		grep -aq "flame: test: the modified host forged" "$hl" || fail "the modified host forged nothing (no test)"
		for log in "$jl" "$j2l"; do
			grep -aq "flame: machine [0-9]*'s flame claim refused: \(its signature is not the listed key's\|it is this machine's own key\)" "$log" ||
				fail "$(basename "$(dirname "$log")") did not refuse a forged proof"
		done
		verified "$jl" "$h" && fail "the joiner drew the modified host's flame"
		verified "$jl" "$j2" && fail "the joiner drew the unlisted player's flame the host claimed"
		verified "$j2l" "$h" && fail "the second joiner drew the modified host's flame"
		verified "$j2l" "$j" && fail "a forged proof of the listed joiner was taken"
	fi
	grep -ah "flame:" "$hl" "$jl" "$j2l" | sed 's/^.*flame:/flame:/' | sort | uniq -c | sort -rn | head -20
	if [ $status = 0 ] && [ "${HALO_TEST_KEEP:-0}" != 1 ]; then rm -rf "$out"; else echo "output kept in $out"; fi
}

export ROOT=$root
case $which in
allowed|modified) run_scenario "$which" ;;
both) run_scenario allowed; run_scenario modified ;;
*) echo "usage: $0 [allowed|modified|both]"; exit 2 ;;
esac
[ $status = 0 ] && echo "PASS"
exit $status
