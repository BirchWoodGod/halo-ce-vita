#!/bin/bash
# The offline bots (port/linux/game/bots.c) on the Linux harness: a local
# (split screen) game on Blood Gulch, started as the network test's "local:"
# does (port/linux/game/network_test.c: the Split Screen lobby, one player on
# controller 1 played by the scripted test input), with bots from the
# settings (HALO_BOTS), played for three minutes. Each second the network
# test logs every player's place, kills, deaths and score.
#
#   slayer   Slayer, HALO_TEST_BOTS bots (6), Normal
#   teams    Team Slayer, the bots on both teams (bots.teams even), Legendary
#   against  Team Slayer, the bots all on the team the player is not on
#            (bots.teams against): they hunt the player down
#   ctf      CTF, the bots on both teams, Heroic
#   king     King of the Hill, Easy
#   oddball  Oddball
#
# Every case must exit by itself with no assertion, exception or halt, have
# its bots join the lobby (named, on machines of their own) and play: each
# bot walks (60 world units at least in three minutes) and goes about the
# map (its places spread over 25 units), the bots kill (more kills than
# bots) and die (most bots at least once), and the score moves (but in CTF,
# whose score is captures). Against: every bot on the other team from the
# player, who dies again and again (the scripted player hardly fights back). The bots'
# tick cost (us/tick, a bot) and path searches are printed from halo.log.
#
#   run_bots_test.sh [CASE...]   (default: slayer teams)
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_BOTS   how many bots (default 6)
#   HALO_TEST_SECONDS  seconds of play (default 180)
#   HALO_TEST_OUT    where the logs go (kept)
# The cases run at once, each in a network namespace of its own (unshare -rn:
# a local game opens the game's ports too).
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_bots_test.$$}
bots=${HALO_TEST_BOTS:-6}
seconds=${HALO_TEST_SECONDS:-180}
cases=${*:-slayer teams}
status=0
fail() { echo "FAIL ($1): $2"; status=1; }

if [ ! -e "$data/maps/bloodgulch.map" ]; then
	echo "SKIP: no bloodgulch.map in $data/maps (HALO_TEST_DATA)"
	exit 0
fi
if ! unshare -rn true 2> /dev/null; then
	echo "SKIP: no unprivileged network namespaces (unshare -rn)"
	exit 0
fi
mkdir -p "$out"

run() { # NAME VARIANT [VAR=value...]
	local name=$1 variant=$2
	shift 2
	rm -rf "$out/$name"
	mkdir -p "$out/$name/data" "$out/$name/save"
	ln -sfn "$(cd "$data" && pwd)/maps" "$out/$name/data/maps"
	(cd "$out/$name" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
		env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" \
		HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=$((seconds + 20)) \
		HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false \
		HALO_NET_ONLINE=false HALO_NETWORK_TEST="local:bloodgulch:$variant" HALO_NETWORK_TEST_START=5 \
		HALO_NETWORK_TEST_SCORE=500 HALO_TEST_INPUT=bot:1 HALO_BOTS="$bots" "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$out/$name/run.log" 2>&1)
	echo $? > "$out/$name/exit"
}

pids=""
for name in $cases; do
	case $name in
	slayer) run slayer slayer HALO_BOT_SKILL=normal & ;;
	teams) run teams team_slayer HALO_BOT_SKILL=legendary HALO_BOT_TEAMS=even & ;;
	against) run against team_slayer HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=against & ;;
	ctf) run ctf ctf HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even & ;;
	king) run king king HALO_BOT_SKILL=easy & ;;
	oddball) run oddball oddball & ;;
	*) echo "unknown case $name"; exit 2 ;;
	esac
	pids="$pids $!"
done
wait $pids

check() { # NAME
	local name=$1 log=$out/$1/run.log
	echo "--- $name"
	[ -e "$log" ] || { fail "$name" "no log"; return; }
	grep -a "bots: tick" "$log" | sed 's/^halo-linux: //' | tail -3
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name/exit")"
	grep -av "render assertion skipped" "$log" | grep -aqiE "assert|exception|halt" &&
		fail "$name" "an assertion, exception or halt"
	python3 - "$log" "$bots" "$name" <<'EOF' || status=1
import re, sys, math
log, bots, name = sys.argv[1], int(sys.argv[2]), sys.argv[3]
named = {}
places = {}
last = {}
for line in open(log, errors="replace"):
    m = re.search(r"network test: player (\d+) is named (.*)", line)
    if m:
        named[int(m.group(1))] = m.group(2).strip()
        continue
    if "network test: tick " not in line:
        continue
    for m in re.finditer(r"player (\d+): \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)[^|]*? s(-?\d+) k(\d+) d(\d+) f\d+ t-?\d+ m(\d+)", line):
        index = int(m.group(1))
        x, y = float(m.group(2)), float(m.group(3))
        places.setdefault(index, []).append((x, y))
        last[index] = (int(m.group(5)), int(m.group(6)), int(m.group(7)), int(m.group(8)))
    for m in re.finditer(r"player (\d+): dead s(-?\d+) k(\d+) d(\d+) f\d+ t-?\d+ m(\d+)", line):
        index = int(m.group(1))
        last[index] = (int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5)))
failed = False
def fail(text):
    global failed
    print("FAIL (%s): %s" % (name, text))
    failed = True
bot_players = [i for i, v in last.items() if v[3] >= 113]
humans = [i for i, v in last.items() if v[3] < 113]
if len(bot_players) != bots:
    fail("%d bots played, not %d" % (len(bot_players), bots))
kills = deaths = died = 0
for i in sorted(bot_players):
    track = places.get(i, [])
    walked = 0.0
    for (x0, y0), (x1, y1) in zip(track, track[1:]):
        step = math.hypot(x1 - x0, y1 - y0)
        if step < 8.0:  # (not a respawn)
            walked += step
    spread = 0.0
    if track:
        xs = [p[0] for p in track]; ys = [p[1] for p in track]
        spread = math.hypot(max(xs) - min(xs), max(ys) - min(ys))
    score, k, d, machine = last[i]
    kills += k; deaths += d; died += d > 0
    print("  bot %-11s walked %6.1f spread %5.1f  score %d kills %d deaths %d" %
        (named.get(i, "?"), walked, spread, score, k, d))
    if walked < 60.0:
        fail("%s walked only %.1f units" % (named.get(i, "?"), walked))
    if spread < 25.0:
        fail("%s stayed within %.1f units" % (named.get(i, "?"), spread))
scores = [v[0] for v in last.values()]
print("  bots: %d kills, %d deaths; best score %d" % (kills, deaths, max(scores) if scores else 0))
if name == "against":
    teams = {}
    for line in open(log, errors="replace"):
        if "network test: tick " in line:
            for m in re.finditer(r"player (\d+): [^|]*? t(-?\d+) m(\d+)", line):
                teams[int(m.group(1))] = int(m.group(2))
    human_teams = set(teams[i] for i in humans if i in teams)
    if any(teams.get(i) in human_teams for i in bot_players):
        fail("a bot is on the player's team")
    human_deaths = sum(last[i][2] for i in humans)
    print("  the player died %d times" % human_deaths)
    if human_deaths < 3:
        fail("the bots killed the player only %d times" % human_deaths)
    sys.exit(1 if failed else 0)
if kills <= bots:
    fail("the bots killed only %d times" % kills)
if died * 2 < bots:
    fail("only %d bots died" % died)
# (CTF's score is captures, which three minutes may not see)
if name != "ctf" and (not scores or max(scores) <= 0):
    fail("no score")
sys.exit(1 if failed else 0)
EOF
}

for name in $cases; do
	check "$name"
done
[ $status = 0 ] && echo "PASS"
echo "logs in $out"
exit $status
