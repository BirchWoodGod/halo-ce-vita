#!/bin/bash
# The offline bots (port/linux/game/bots.c) on the Linux harness: a local
# (split screen) game on Blood Gulch (HALO_TEST_BOTS_MAP), started as the
# network test's "local:"
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
#   maps     every stock multiplayer map in HALO_TEST_DATA's maps folder
#            (beavercreek, bloodgulch, ..., wizard: those there) with every
#            game the bots play (slayer, team_slayer, ctf, king, oddball;
#            HALO_TEST_BOTS_GAMES), a shorter game each (HALO_TEST_SECONDS,
#            default 120 here), HALO_TEST_JOBS of them at once (default a
#            quarter of the processors). Each game must exit cleanly with its
#            bots roaming (each walks 40 units at least, and the bots spread
#            over a third of the map's size, its roaming places' extent, at
#            least half of them; 25 units at most is asked), and play the
#            game: kills in Slayer, a flag taken (or captured, or a bot at
#            the other team's stand) in CTF, a hill held (a score) in King of
#            the Hill, the ball taken in Oddball; but for the games listed
#            (KNOWN) whose goal the maps' walkable surfaces do not reach.
#            CTF games are half as long again, and the bots do not fight in
#            them (HALO_BOT_PEACEFUL=1: the flags' ways alone; kills are not
#            asked there).
#            A table of every game follows: how far the bots walked and
#            spread, their path searches and how many failed, the objective
#            and the bots' tick cost.
#
#   run_bots_test.sh [CASE...]   (default: slayer teams)
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_BOTS   how many bots (default 6)
#   HALO_TEST_BOTS_MAP  the map of the single cases (default bloodgulch)
#   HALO_TEST_BOTS_MAPS  the maps case's maps (default every stock map there)
#   HALO_TEST_SECONDS  seconds of play (default 180; 120 for the maps case)
#   HALO_TEST_OUT    where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
#   HALO_TEST_RECHECK=1  the logs a kept HALO_TEST_OUT holds checked again, nothing played
# The cases run at once, each in a network namespace of its own (unshare -rn:
# a local game opens the game's ports too).
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_bots_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
bots=${HALO_TEST_BOTS:-6}
cases=${*:-slayer teams}
map=${HALO_TEST_BOTS_MAP:-bloodgulch}
seconds=${HALO_TEST_SECONDS:-180}
case " $cases " in *" maps "*) seconds=${HALO_TEST_SECONDS:-120} ;; esac
stock_maps="beavercreek bloodgulch boardingaction chillout carousel damnation dangercanyon deathisland
	gephyrophobia hangemhigh icefields infinity longest prisoner putput ratrace sidewinder timberland wizard"
games=${HALO_TEST_BOTS_GAMES:-slayer team_slayer ctf king oddball}
jobs=${HALO_TEST_JOBS:-$(( ($(nproc) + 3) / 4 ))}
status=0
fail() { echo "FAIL ($1): $2"; status=1; }

if [ ! -e "$data/maps/$map.map" ]; then
	echo "SKIP: no $map.map in $data/maps (HALO_TEST_DATA)"
	exit 0
fi
if ! unshare -rn true 2> /dev/null; then
	echo "SKIP: no unprivileged network namespaces (unshare -rn)"
	exit 0
fi
mkdir -p "$out"

run() { # NAME VARIANT [MAP=map] [VAR=value...]
	local name=$1 variant=$2 game_map=$map
	shift 2
	case ${1:-} in MAP=*) game_map=${1#MAP=}; shift ;; esac
	[ "${HALO_TEST_RECHECK:-0}" = 1 ] && return 0
	rm -rf "$out/$name"
	mkdir -p "$out/$name/data" "$out/$name/save"
	ln -sfn "$(cd "$data" && pwd)/maps" "$out/$name/data/maps"
	(cd "$out/$name" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
		env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" \
		HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=$((seconds + 20)) \
		HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false \
		HALO_NET_ONLINE=false HALO_NETWORK_TEST="local:$game_map:$variant" HALO_NETWORK_TEST_START=5 \
		HALO_NETWORK_TEST_SCORE=500 HALO_TEST_INPUT=bot:1 HALO_BOTS="$bots" "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$out/$name/run.log" 2>&1)
	echo $? > "$out/$name/exit"
}

# the maps case's games (MAP-GAME), at most $jobs at once
matrix=""
if [ -n "${HALO_TEST_BOTS_MAPS:-}" ]; then
	matrix_maps=$HALO_TEST_BOTS_MAPS
else
	matrix_maps=""
	for m in $stock_maps; do
		[ -e "$data/maps/$m.map" ] && matrix_maps="$matrix_maps $m"
	done
fi
for name in $cases; do
	[ "$name" = maps ] || continue
	for m in $matrix_maps; do
		for g in $games; do
			matrix="$matrix $m-$g"
		done
	done
done
run_matrix() { # MAP-GAME
	local m=${1%-*} g=${1##*-}
	# (CTF half as long again, and without fighting: a flag is a map's
	# length away, a minute's walk each way on Sidewinder, and three minutes
	# of fighting mid-map may see no bot through; the ctf case fights)
	[ "$g" = ctf ] && seconds=$((seconds * 3 / 2))
	case $g in
	team_slayer) run "$1" "$g" MAP="$m" HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even ;;
	ctf) run "$1" "$g" MAP="$m" HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even HALO_BOT_PEACEFUL=1 ;;
	*) run "$1" "$g" MAP="$m" HALO_BOT_SKILL=heroic ;;
	esac
}

pids=""
single=""
for name in $cases; do
	case $name in
	slayer) run slayer slayer HALO_BOT_SKILL=normal & ;;
	teams) run teams team_slayer HALO_BOT_SKILL=legendary HALO_BOT_TEAMS=even & ;;
	against) run against team_slayer HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=against & ;;
	ctf) run ctf ctf HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even & ;;
	king) run king king HALO_BOT_SKILL=easy & ;;
	oddball) run oddball oddball & ;;
	maps) continue ;;
	*) echo "unknown case $name"; exit 2 ;;
	esac
	pids="$pids $!"
	single="$single $name"
done
running=0
for name in $matrix; do
	if [ $running -ge "$jobs" ]; then
		wait -n
		running=$((running - 1))
	fi
	run_matrix "$name" &
	running=$((running + 1))
done
wait

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

# a game of the maps case: its row of the table, and its checks
check_matrix() { # NAME
	local name=$1 log=$out/$1/run.log
	[ -e "$log" ] || { fail "$name" "no log"; return; }
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name/exit")"
	grep -av "render assertion skipped" "$log" | grep -aqiE "assert|exception|halt" &&
		fail "$name" "an assertion, exception or halt"
	python3 - "$log" "$bots" "$name" <<'EOF' || status=1
import re, sys, math
log, bots, name = sys.argv[1], int(sys.argv[2]), sys.argv[3]
game = name.rsplit("-", 1)[1]
places, last, extent, report = {}, {}, 0.0, ""
for line in open(log, errors="replace"):
    m = re.search(r"bots: \d+ places to roam on this map, over (\d+) units", line)
    if m:
        extent = float(m.group(1))
    if "bots: tick " in line:
        report = line
    if "network test: tick " not in line:
        continue
    for m in re.finditer(r"player (\d+): \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)[^|]*? s(-?\d+) k(\d+) d(\d+) f\d+ t-?\d+ m(\d+)", line):
        i = int(m.group(1))
        places.setdefault(i, []).append((float(m.group(2)), float(m.group(3))))
        last[i] = (int(m.group(5)), int(m.group(6)), int(m.group(7)), int(m.group(8)))
    for m in re.finditer(r"player (\d+): dead s(-?\d+) k(\d+) d(\d+) f\d+ t-?\d+ m(\d+)", line):
        i = int(m.group(1))
        last[i] = (int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5)))
failed = []
bot_players = [i for i, v in last.items() if v[3] >= 113]
walks, spreads = [], []
for i in bot_players:
    track = places.get(i, [])
    steps = [math.hypot(x1 - x0, y1 - y0) for (x0, y0), (x1, y1) in zip(track, track[1:])]
    walks.append(sum(step for step in steps if step < 8.0))  # (not a respawn)
    xs = [p[0] for p in track] or [0.0]
    ys = [p[1] for p in track] or [0.0]
    spreads.append(math.hypot(max(xs) - min(xs), max(ys) - min(ys)))
walks.sort()
spreads.sort()
def median(values):
    return values[len(values) // 2] if values else 0.0
def number(pattern, kind=int):
    m = re.search(pattern, report)
    return kind(m.group(1)) if m else kind(0)
searches, failures, short = number(r"searches (\d+)"), number(r"\((\d+) failed"), number(r"(\d+) short")
unavoided = number(r"(\d+) unavoided")
cost = number(r"\(([\d.]+) a bot\)", float)
flags, balls = number(r"flag taken (\d+)"), number(r"ball taken (\d+)")
stands = number(r"enemy stand reached (\d+)")
kills = sum(last[i][1] for i in bot_players)
best = max([v[0] for v in last.values()] or [0])
objective = {"slayer": "kills %d" % kills, "team_slayer": "kills %d" % kills,
    "ctf": "flag taken %d (stand reached %d), captures %d" % (flags, stands, max(best, 0)), "king": "best score %d" % best,
    "oddball": "ball taken %d, best score %d" % (balls, best)}[game]
def share(count):
    return 100.0 * count / searches if searches else 0.0
print("  %-26s walked %4.0f (least %4.0f) spread %3.0f of %3.0f  searches %5d failed %4.1f%% short %4.1f%% unavoided %4.1f%%"
    "  %4.1f us a bot  %s" % (name, median(walks), walks[0] if walks else 0, median(spreads), extent, searches,
    share(failures), share(short), share(unavoided), cost, objective))
if len(bot_players) != bots:
    failed.append("%d bots played, not %d" % (len(bot_players), bots))
if walks and walks[0] < 40.0:
    failed.append("a bot walked only %.0f units" % walks[0])
if median(spreads) < min(25.0, extent / 3.0):
    failed.append("the bots spread over %.0f units, the map %.0f" % (median(spreads), extent))
# the games whose goal the walkable surfaces (the AI's data the bots' path
# searches read, built by tool with each map) do not reach from the ground the
# bots play on: the bots go as near as they can, jump, and play on (roaming
# and fighting are still checked)
KNOWN = {
    "boardingaction-ctf": "each ship's rooms are islands of walkable surfaces, the flags in others",
    "damnation-oddball": "the ball's spawn is picked at random, some on ledges no walkable surface reaches",
    "hangemhigh-king": "the hill is on a ledge no walkable surface reaches",
    "hangemhigh-oddball": "the ball spawns on a ledge no walkable surface reaches",
    "prisoner-ctf": "the flags are on the upper floor, which no walkable surface reaches",
    "prisoner-oddball": "the ball spawns on the upper floor",
    "putput-king": "the hill is in a room the walkable surfaces and teleporters do not reach",
    "wizard-ctf": "the flags are on corner platforms no walkable surface reaches",
    "wizard-king": "the hill is on a platform no walkable surface reaches",
    "wizard-oddball": "the ball spawns on a platform no walkable surface reaches",
}
objective_failed = None
if game in ("slayer", "team_slayer") and kills <= 0:
    failed.append("no kills")
# (a bot at the other team's stand would have taken the flag had it been
# there: on the big maps three minutes of fighting may not see one taken)
if game == "ctf" and flags <= 0 and best <= 0 and stands <= 0:
    objective_failed = "no flag taken, no bot at the other team's stand"
if game == "king" and best <= 0:
    objective_failed = "no hill held"
if game == "oddball" and balls <= 0 and best <= 0:
    objective_failed = "no ball taken"
if objective_failed and name in KNOWN:
    print("KNOWN (%s): %s: %s" % (name, objective_failed, KNOWN[name]))
elif objective_failed:
    failed.append(objective_failed)
elif name in KNOWN:
    print("NOTE (%s): played its goal, listed as one the walkable surfaces do not reach" % name)
for text in failed:
    print("FAIL (%s): %s" % (name, text))
sys.exit(1 if failed else 0)
EOF
}

for name in $single; do
	check "$name"
done
if [ -n "$matrix" ]; then
	echo "--- maps ($seconds s a game, CTF $((seconds * 3 / 2)) s, $bots bots)"
	for name in $matrix; do
		check_matrix "$name"
	done
fi
[ $status = 0 ] && echo "PASS"
echo "logs in $out"
test_out_done $status
exit $status
