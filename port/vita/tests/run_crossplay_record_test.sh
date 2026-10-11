#!/bin/bash
# The game settings record on the crossplay wire, end to end: two copies of
# this tree's Linux build with crossplay on (HALO_CROSSPLAY=true: OpenCE's
# 13,120-byte record, port/linux/game/network_game_record.c), the host a
# stand-in for a PC host whose gametype has PC options
# (HALO_NETWORK_TEST_VARIANT_OPTIONS, network_test.c; HALO_TEST_CROSSPLAY_HOST=1,
# a debug build's leave to host on the crossplay wire, which a Vita never
# does: network_wire.h), in netns lan mode (run_netns_online_test.sh: system
# link on one LAN, as a PC host's; codes and relays are the legacy wire's). The host
# plays Team Slayer on Blood Gulch with every vehicle the map places
# (team_slayer+vehicles) and options: red's vehicle set none, blue's custom
# (a Warthog, two Ghosts, nothing else), the FRIENDS radar and a two-minute
# time limit. The joiner must:
# - log the host's options as sent ("game: the host's PC options: ...");
# - place the vehicles the host placed: its census of them (network test's,
#   every 10 s) the host's, at most a Warthog and two Ghosts, no Scorpion;
# - play with the host, and see the game end on the time limit (the host's
#   game over by about two minutes of game time, 3,600 ticks).
# The same with crossplay off is netns code and lan modes themselves (the
# legacy record).
#   HALO_TEST_VITA   the build (default build/linux/halo of this tree)
#   HALO_TEST_OUT, HALO_TEST_KEEP   as run_netns_online_test.sh's
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/crossplay_record_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
options="time_limit=2,radar_players=1,vehicle_set=1:255,counts_blue=1.2.0.0.0.0"
# (a score no game reaches in the time: the scripted kills, one each 20 s,
# do not end it first)
HALO_TEST_OUT="$out" HALO_TEST_KEEP=1 HALO_TEST_SECONDS=${HALO_TEST_SECONDS:-210} HALO_TEST_SCORE=50 \
	HALO_TEST_HOST_GAME=host:bloodgulch:team_slayer+vehicles \
	HALO_TEST_ENV="HALO_CROSSPLAY=true HALO_TEST_CROSSPLAY_HOST=1 HALO_NETWORK_TEST_VARIANT_OPTIONS=$options ${HALO_TEST_ENV:-}" \
	"$here/run_netns_online_test.sh" lan
status=$?
fail() { echo "FAIL $*"; status=1; }
[ $status = 0 ] || echo "FAIL the netns lan run (above)"
host=$out/host/run.log
joiner=$out/joiner/run.log
if [ -f "$host" ] && [ -f "$joiner" ]; then
	grep -aq "network test: the gametype's PC options from HALO_NETWORK_TEST_VARIANT_OPTIONS$" "$host" ||
		fail "the host did not take the options"
	line=$(grep -a "game: the host's PC options:" "$joiner" | head -1)
	echo "joiner: ${line#*game: }"
	case $line in
	*"time limit 2 min"*"radar 1, vehicle sets 1/255, counts 0.0.0.0.0.0/1.2.0.0.0.0"*"co-op switches 0") ;;
	*) fail "the joiner did not get the host's options" ;;
	esac
	grep -aq "refused the host's game settings" "$joiner" && fail "the joiner refused the host's settings"
	python3 - "$host" "$joiner" <<'EOF' || status=1
import re, sys
def census(path):
    seen = {}
    for line in open(path, errors="replace"):
        # (the first game's: until it is over)
        if re.search(r"network test: tick \d+ .*\| game over", line):
            break
        m = re.search(r"network test: vehicles at tick (\d+):(.*)", line)
        if m and int(m.group(1)) >= 300:
            seen[int(m.group(1)) // 300] = sorted((n.rsplit("\\", 1)[-1], int(c))
                for n, c in re.findall(r" (\S+)\([0-9a-f]+\) (\d+)", m.group(2)))
    return seen
host, joiner = census(sys.argv[1]), census(sys.argv[2])
ok = True
if not host or not joiner:
    print("FAIL no vehicle census (host %d, joiner %d)" % (len(host), len(joiner))); sys.exit(1)
print("host's vehicles:", next(iter(host.values())), "| joiner's:", next(iter(joiner.values())))
for when, kinds in joiner.items():
    total = sum(c for _, c in kinds)
    names = " ".join(n for n, _ in kinds)
    if "scorpion" in names or total > 3 or total == 0:
        print("FAIL the joiner's vehicles at %d s are not blue's custom set: %s" % (when * 10, kinds)); ok = False; break
same = [w for w in joiner if w in host]
if not same or any(host[w] != joiner[w] for w in same):
    print("FAIL the joiner's vehicles are not the host's: %s vs %s" % (
        [joiner[w] for w in same][:2], [host[w] for w in same][:2])); ok = False
sys.exit(0 if ok else 1)
EOF
	over=$(grep -a "network test: tick [0-9]* .*| game over" "$host" | head -1 | grep -oE "tick [0-9]+" | grep -oE "[0-9]+")
	echo "host's game over at tick ${over:-never}"
	[ -n "$over" ] && [ "$over" -ge 3550 ] && [ "$over" -le 3950 ] || fail "the game did not end on its two-minute limit"
	grep -aq "network test: tick [0-9]* .*| game over" "$joiner" || fail "the joiner did not see the game end"
else
	fail "no logs"
fi
[ $status = 0 ] && echo "PASS (crossplay record)" || echo "FAIL (crossplay record)"
test_out_done $status
exit $status
