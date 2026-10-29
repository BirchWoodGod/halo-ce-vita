#!/bin/bash
# Infection (port/linux/game/infection.c) with the offline bots on the Linux
# harness: a local (split screen) game, started as the network test's
# "local:<map>:infection" does (port/linux/game/network_test.c: one player on
# controller 1 played by the scripted test input), with 7 bots (8 players:
# one first zombie), on Prisoner (HALO_TEST_INFECTION_MAP). Each second the
# network test logs every player's place, weapons, grenades and team (t0 the
# zombies, t1 the humans); infection.c logs the round, every player turned and
# the end.
#
#   spread   the humans' bots fight no one (HALO_BOT_PEACEFUL_HUMANS=1): the
#            zombie bots hunt them down with the skull, and the infection
#            spreads to every player; the zombies win before the time
#   survive  no bot fights (HALO_BOT_PEACEFUL=1: the zombies hunt but strike
#            no one) and a minute's round: the humans still alive win on time
#   fight    every bot fights (Heroic): the zombies chase and strike, the humans
#            shoot; the infection spreads (two turned at least) and the round
#            ends, the winner the side the counts say
#
# Every case: the round begins with one first zombie (one in 8, at random), a
# zombie holds the skull alone (the Oddball ball, the same weapon for every
# zombie, never a human's) and no grenades, the game over is logged with its
# winner, the winner's side has players (the zombies all of them when they
# win), and the copy exits by itself with no assertion, exception or halt.
#
#   run_infection_test.sh [CASE...]   (default: spread survive fight)
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_OUT    where the logs go (deleted after a pass when the test made
#                    it; HALO_TEST_KEEP=1 keeps them)
# The cases run at once, each in a network namespace of its own (unshare -rn).
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_infection_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-spread survive fight}
map=${HALO_TEST_INFECTION_MAP:-prisoner}
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

run() { # NAME SECONDS [VAR=value...]
	local name=$1 seconds=$2
	shift 2
	rm -rf "$out/$name"
	mkdir -p "$out/$name/data" "$out/$name/save"
	ln -sfn "$(cd "$data" && pwd)/maps" "$out/$name/data/maps"
	(cd "$out/$name" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
		env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" \
		HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=$((seconds + 20)) \
		HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false \
		HALO_NET_ONLINE=false HALO_NETWORK_TEST="local:$map:infection" HALO_NETWORK_TEST_START=5 \
		HALO_NETWORK_TEST_SCORE=500 HALO_TEST_INPUT=bot:1 HALO_BOTS=7 HALO_BOT_TEAMS=even HALO_BOT_SKILL=heroic \
		HALO_INFECTION_ZOMBIES=8 HALO_INFECTION_SPEED=115 HALO_INFECTION_RESPAWN=3 "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$out/$name/run.log" 2>&1)
	echo $? > "$out/$name/exit"
}

pids=""
for name in $cases; do
	case $name in
	spread) run spread 200 HALO_INFECTION_TIME=5 HALO_BOT_PEACEFUL_HUMANS=1 & ;;
	survive) run survive 110 HALO_INFECTION_TIME=1 HALO_BOT_PEACEFUL=1 & ;;
	fight) run fight 260 HALO_INFECTION_TIME=4 & ;;
	*) echo "unknown case $name"; exit 2 ;;
	esac
	pids="$pids $!"
done
wait $pids

# the players' lines while the game is played (each second's: weapons,
# grenades and team of each), checked and summed up by python
check_lines() { # NAME WINNER
	python3 - "$out/$1/run.log" "$2" <<'EOF'
import re, sys
log, winner = sys.argv[1], sys.argv[2]
# (a second's line: "player N: (x y z) h.../... g<frag>/<plasma> w ... thr<t> [weapon:rounds...]
# s<score> k<kills> d<deaths> f<betrayals> t<team> m<machine>", or "player N: dead s... t...")
tail = re.compile(r' s-?\d+ k\d+ d\d+ f\d+ t(\d+) m\d+\s*$')
grenades = re.compile(r' g(\d+)/(\d+) ')
weapon = re.compile(r' ([0-9a-f]+):(\d+)')
seconds = []
# (the Infection game's seconds alone: from its round's start to its end, not
# the network test's next game)
playing = False
for line in open(log, errors='replace'):
	if 'infection: the round begins' in line:
		playing = True
	if 'infection: game over here' in line:
		break
	if not playing or 'network test: tick' not in line or '| playing' not in line:
		continue
	players = []
	for segment in re.split(r'(?=player \d+: )', line.split('|')[0])[1:]:
		number = segment.split(':', 1)[0].split()[1]
		team = tail.search(segment)
		if not team:
			continue
		body = segment[:team.start()]
		alive = ': dead' not in segment[:len(number) + 12]
		g = grenades.search(body) if alive else None
		ids = [w for w, _ in weapon.findall(body.split(' thr', 1)[-1])] if alive else []
		players.append((number, team.group(1), alive, g and (int(g.group(1)) + int(g.group(2))), ids))
	seconds.append(players)
skulls, problems = set(), []
for players in seconds:
	for number, team, alive, grenade_count, ids in players:
		if not alive or team != '0':
			continue
		if grenade_count:
			problems.append('zombie %s with grenades' % number)
		if len(ids) > 1:
			problems.append('zombie %s with %d weapons' % (number, len(ids)))
		skulls.update(ids)
if len(skulls) > 1:
	problems.append('the zombies held %d kinds of weapon (%s)' % (len(skulls), ' '.join(sorted(skulls))))
for players in seconds:
	for number, team, alive, grenade_count, ids in players:
		if alive and team == '1' and skulls & set(ids):
			problems.append('human %s holding the skull' % number)
teams = [p[1] for p in (seconds[-1] if seconds else [])]
print('the last second played: %d players, %d zombies, %d humans; the zombies\' weapon %s' %
	(len(teams), teams.count('0'), teams.count('1'), ' '.join(sorted(skulls)) or 'none seen'))
# (the last human turned between the last second logged and the end)
if winner == 'zombies' and teams.count('1') > 1:
	problems.append('the zombies won with %d humans in the last second played' % teams.count('1'))
if winner == 'humans' and not teams.count('1'):
	problems.append('the humans won with none in the last second played')
for problem in sorted(set(problems))[:10]:
	print('PROBLEM ' + problem)
sys.exit(1 if problems else 0)
EOF
}

for name in $cases; do
	log=$out/$name/run.log
	[ -s "$log" ] || { fail "$name" "no log"; continue; }
	echo "--- $name"
	grep -a "infection:" "$log" | grep -v "trace" | sed 's/^halo-linux: //' | head -40
	code=$(cat "$out/$name/exit" 2>/dev/null)
	[ "$code" = 0 ] || fail "$name" "exited with $code"
	{ grep -aqE "halt_and_catch_fire|segmentation fault" "$log" || grep -aqE "EXCEPTION|ASSERT" "$out/$name/data/debug.txt"; } \
		2>/dev/null && fail "$name" "an assertion, exception or halt (debug.txt)"
	grep -aq "infection: the round begins (one first zombie for each 8 players" "$log" ||
		fail "$name" "the round never began"
	first=$(grep -ac "is a zombie (a first zombie, at random)" "$log")
	[ "$first" = 1 ] || fail "$name" "$first first zombies (one in 8 players: 1 wanted)"
	turned=$(grep -acE "is a zombie \((killed by a zombie|died)\)" "$log")
	over=$(grep -a "infection: the round is over" "$log" | head -1)
	here_over=$(grep -a "infection: game over here: the [a-z]* win" "$log" | head -1)
	[ -n "$over" ] || fail "$name" "the round never ended"
	[ -n "$here_over" ] || fail "$name" "the game over was not logged"
	winner=$(echo "$here_over" | sed -n 's/.*game over here: the \([a-z]*\) win.*/\1/p')
	echo "$name: $turned turned, the $winner won"
	case $name in
	spread)
		[ "$turned" -ge 7 ] || fail "$name" "$turned turned (every human: 7 wanted)"
		[ "$winner" = zombies ] || fail "$name" "the $winner won (the zombies wanted)"
		echo "$over" | grep -q "no human left: the zombies win" || fail "$name" "the round did not end with no human left"
		grep -aq "infection: .* is the last human" "$log" || fail "$name" "the last human was not told"
		;;
	survive)
		[ "$turned" = 0 ] || fail "$name" "$turned turned (no one strikes: none wanted)"
		[ "$winner" = humans ] || fail "$name" "the $winner won (the humans wanted)"
		echo "$over" | grep -q "time: the humans win" || fail "$name" "the round did not end on time"
		;;
	fight)
		[ "$turned" -ge 2 ] || fail "$name" "$turned turned (2 at least wanted)"
		case $over in
		*"the zombies win"*) [ "$winner" = zombies ] || fail "$name" "the round said the zombies, the game the $winner" ;;
		*"the humans win"*) [ "$winner" = humans ] || fail "$name" "the round said the humans, the game the $winner" ;;
		esac
		;;
	esac
	check_lines "$name" "$winner" || fail "$name" "the players' lines (above)"
done
[ $status = 0 ] && echo "PASS: Infection with bots ($cases)"
test_out_done $status
exit $status
