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
#   invasion Invasion (CTF, the flags at the other team's base: a team runs
#            the one at its own base to the other's), the bots on both teams
#            and not fighting (HALO_BOT_PEACEFUL=1): a capture is asked, not
#            kills
#   race     Team Race: a bot on to a second checkpoint at least (the bots
#            drive the race's Warthogs to the checkpoints, else run; kills
#            are not asked, the bots in their vehicles fighting little)
#   rockets  Rockets (Slayer, rocket launchers)
#   teamball Team Oddball
#   vctf     CTF with every vehicle the map places (the variant's default
#            vehicle set: Blood Gulch's Warthogs, Ghosts and Scorpions;
#            "ctf+vehicles"), the bots on both teams, Heroic; the player
#            stops a Warthog at its base 1 s in, for 20 s
#            (HALO_NETWORK_TEST_PLAYER_DRIVES), and a teammate bot must get
#            in beside it
#   vslayer  Slayer likewise ("slayer+vehicles"): every bot for itself, a
#            driver's seat alone (no one rides with an enemy)
#   vteams   Team Slayer likewise ("team_slayer+vehicles"), Legendary
#   vehicles vctf, vslayer and vteams (HALO_TEST_BOTS 8 is the brief's)
#   powerups Slayer, 12 bots, Heroic: the hurt go for the health packs (Blood
#            Gulch's two on the bases' roofs) - one taken at least, or a
#            body restored (halo.log's moves line, bots.c)
#   turret   Slayer with every vehicle on Extinction (a Custom Edition map,
#            from HALO_TEST_CE_MAPS with Halo PC's resource maps, as
#            run_ce_map_memory_test.sh finds them; SKIP without): its gun
#            towers stand far from where anyone plays, so the bots are set
#            about one every ten seconds (HALO_TEST_COMMANDS' @pos) and fight
#            there; a bot must man it and fire from it (the census's
#            turrets), and get off again
#   ladders  Oddball on Boarding Action, not fighting (HALO_BOT_PEACEFUL=1),
#            8 bots: the ball's spawn is on another of the ship's floors,
#            which the walkable surfaces do not join: the ladders (bots.c
#            finds sixteen) must be climbed once at least
#   new      powerups, turret and ladders
#   playlist the bots' playlist (port/linux/game/bots_playlist.c): a Slayer
#            game on Chill Out to 3, then Everything (bots.playlist all,
#            HALO_TEST_BOT_PLAYLIST_SEED 3) game after game by itself, each
#            to 3 or a minute (HALO_TEST_BOT_PLAYLIST_SCORE, _MINUTES), the
#            scores 4 s (HALO_TEST_BOT_PLAYLIST_SCORES), 6 bots, Heroic, for
#            HALO_TEST_PLAYLIST_SECONDS (780): HALO_TEST_PLAYLIST_GAMES (7)
#            playlist games at least started, each the map and gametype it
#            set up (the map unloaded after it), no map twice in a row, the
#            bots in every game and walking (a team gametype with both
#            teams), and memory steady over the map changes: halo.log's
#            "memory: unloaded ui" between games, from the third on, the C
#            heap rising 96 KB a change at most on average (1 MB in all), the
#            texture pool at its first segment (4 MB) and video memory as
#            free (1 MB)
#   playlistcancel  Custom (bots.playlist custom): a bots_playlist.txt with
#            good lines and bad (no gametype, extra words, maps not there or
#            not multiplayer, paths, format strings, a gametype the bots do
#            not play, a name too long, a 5000-character line, a NUL, bytes
#            beyond ASCII, a UTF-8 mark, CR LF, and past 8 KB a line that
#            must not be read): its four games and every bad line skipped
#            with the reason; B two seconds into the second playlist game's
#            countdown (HALO_TEST_BOT_PLAYLIST_CANCEL=2): the playlist stops,
#            no second game starts and Split Screen is not left
#   playlists  playlist and playlistcancel
#
# The vehicle cases: the bots must take the vehicles - their census in
# halo.log every ten seconds (bots.c: riding, driving, gunning, the distance
# driven, in and out and why) - two bots in at least, two at once at some
# census, the bots' vehicles driven 150 units at least, and some bot out
# again; in vctf a bot in a Warthog's gunner's or passenger's seat beside a
# teammate driving, and (on Blood Gulch, whose bases have room for it) one
# beside the player. The census and the bots' tick cost are printed.
#
# CTF and Invasion: each team must spawn at its own base, at the start and
# after (each life's first place nearer its own team's flag stand, which the
# bots log; issue #40).
#
# Every case must exit by itself with no assertion, exception or halt, have
# its bots join the lobby (named, on machines of their own) and play: each
# bot walks (60 world units at least in three minutes) and goes about the
# map (its places spread over 25 units), the bots kill (more kills than
# bots) and die (most bots at least once), and the score moves (but in CTF,
# whose score is captures). Against: every bot on the other team from the
# player, who dies again and again (the scripted player hardly fights back). The bots'
# tick cost (us/tick, a bot) and path searches are printed from halo.log; a bot's
# tick may cost HALO_TEST_BOT_BUDGET_US (30) at most on average (the harness is
# some ten times the Vita's speed: the Vita's budget, 300 us a bot, a tenth of a
# frame for fifteen).
#
#   maps     every stock multiplayer map in HALO_TEST_DATA's maps folder
#            (beavercreek, bloodgulch, ..., wizard: those there) with every
#            game the bots play (slayer, team_slayer, ctf, king, oddball,
#            invasion, team_race, rockets, team_oddball;
#            HALO_TEST_BOTS_GAMES), a shorter game each (HALO_TEST_SECONDS,
#            default 120 here), HALO_TEST_JOBS of them at once (default a
#            quarter of the processors). Each game must exit cleanly with its
#            bots roaming (each walks 40 units at least, and the bots spread
#            over a third of the map's size, its roaming places' extent, at
#            least half of them; 25 units at most is asked), and play the
#            game: kills in Slayer, a flag taken (or captured, or a bot at
#            the other team's stand) in CTF, a hill held (a score) in King of
#            the Hill, the ball taken in Oddball and Team Oddball, a capture in
#            Invasion, a checkpoint touched in Team Race, kills in Rockets;
#            but for the games listed
#            (KNOWN) whose goal the maps' walkable surfaces do not reach.
#            CTF and Invasion games are half as long again, and the bots do
#            not fight in them (HALO_BOT_PEACEFUL=1: the flags' ways alone; kills are not
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
#   HALO_TEST_CE_MAPS, HALO_TEST_CE_RESOURCES  the turret case's Extinction and Halo PC's resource maps
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
cases=$(echo " $cases " | sed 's/ vehicles / vctf vslayer vteams /; s/ new / powerups turret ladders /; s/ playlists / playlist playlistcancel /')
ce_maps=${HALO_TEST_CE_MAPS:-$root/../custom-maps-dl}
ce_resources=${HALO_TEST_CE_RESOURCES:-}
if [ -z "$ce_resources" ]; then
	ce_resources="$data/maps"
	[ -e "$ce_resources/bitmaps.map" ] ||
		ce_resources="$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1/maps/custom_edition"
fi
budget=${HALO_TEST_BOT_BUDGET_US:-30}
# the bots a case plays with
case_bots() { # NAME
	case $1 in
	powerups) echo 12 ;;
	turret) echo 6 ;;
	playlist) echo 6 ;;
	playlistcancel) echo 3 ;;
	ladders) echo 8 ;;
	*) echo "$bots" ;;
	esac
}
map=${HALO_TEST_BOTS_MAP:-bloodgulch}
seconds=${HALO_TEST_SECONDS:-180}
case " $cases " in *" maps "*) seconds=${HALO_TEST_SECONDS:-120} ;; esac
playlist_seconds=${HALO_TEST_PLAYLIST_SECONDS:-780}
playlist_games=${HALO_TEST_PLAYLIST_GAMES:-7}
stock_maps="beavercreek bloodgulch boardingaction chillout carousel damnation dangercanyon deathisland
	gephyrophobia hangemhigh icefields infinity longest prisoner putput ratrace sidewinder timberland wizard"
games=${HALO_TEST_BOTS_GAMES:-slayer team_slayer ctf king oddball invasion team_race rockets team_oddball}
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
	local name=$1 variant=$2 game_map=$map file
	shift 2
	case ${1:-} in MAP=*) game_map=${1#MAP=}; shift ;; esac
	[ "${HALO_TEST_RECHECK:-0}" = 1 ] && return 0
	rm -rf "$out/$name"
	mkdir -p "$out/$name/data" "$out/$name/save"
	# (the playlist's custom file, in the data folder)
	[ -n "${playlist_file:-}" ] && cp "$playlist_file" "$out/$name/data/bots_playlist.txt"
	if [ -e "$data/maps/$game_map.map" ]; then
		ln -sfn "$(cd "$data" && pwd)/maps" "$out/$name/data/maps"
	else
		# (a Custom Edition map: the Xbox maps, it and Halo PC's resource maps)
		mkdir -p "$out/$name/data/maps"
		for file in "$(cd "$data" && pwd)"/maps/*.map; do
			ln -sfn "$file" "$out/$name/data/maps/"
		done
		ln -sfn "$(cd "$ce_maps" && pwd)/$game_map.map" "$out/$name/data/maps/$game_map.map"
		for file in bitmaps sounds loc; do
			ln -sfn "$(cd "$ce_resources" && pwd)/$file.map" "$out/$name/data/maps/$file.map"
		done
		set -- HALO_CUSTOM_EDITION=1 "$@"
	fi
	(cd "$out/$name" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
		env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" \
		HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=$((seconds + 20)) \
		HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false \
		HALO_NET_ONLINE=false HALO_NETWORK_TEST="local:$game_map:$variant" HALO_NETWORK_TEST_START=5 \
		HALO_NETWORK_TEST_SCORE=500 HALO_TEST_INPUT=bot:1 HALO_BOTS="$(case_bots "$name")" "$@" \
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
	case $g in ctf | invasion) seconds=$((seconds * 3 / 2)) ;; esac
	case $g in
	team_slayer | team_race | team_oddball) run "$1" "$g" MAP="$m" HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even ;;
	ctf | invasion) run "$1" "$g" MAP="$m" HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even HALO_BOT_PEACEFUL=1 ;;
	*) run "$1" "$g" MAP="$m" HALO_BOT_SKILL=heroic ;;
	esac
}

# the playlistcancel case's bots_playlist.txt: four good lines (a UTF-8 mark
# first, a comment, upper case and spaces, a tab and a CR LF), the bad lines
# check's playlistcancel part expects skipped, comments to past 8 KB, and a
# line there that must not be read
write_playlist_file() { # FILE
	python3 - "$1" <<'EOF'
import sys
lines = [
    b"\xef\xbb\xbf# the bots' custom playlist (run_bots_test.sh)",
    b"chillout slayer",
    b"  Prisoner   TEAM_SLAYER   # a comment after",
    b"bloodgulch ctf+vehicles",
    b"",
    b"wizard",
    b"wizard slayer extra",
    b"nosuchmap slayer",
    b"a10 slayer",
    b"ui slayer",
    b"../../etc/passwd slayer",
    b"..\\..\\maps\\bloodgulch slayer",
    b"bloodgulch juggernaut",
    b"bloodgulch %s%s%n%x",
    b"a" * 40 + b" slayer",
    b"x" * 5000,
    b"long\x00est slayer",
    b"\xff\xfe\x80 slayer",
    b"damnation +vehicles",
    b"hangemhigh\tctf\r",
]
while sum(len(line) + 1 for line in lines) < 8400:
    lines.append(b"# " + b"-" * 68)
lines.append(b"boardingaction slayer")
open(sys.argv[1], "wb").write(b"\n".join(lines) + b"\n")
EOF
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
	invasion) run invasion invasion HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even HALO_BOT_PEACEFUL=1 & ;;
	race) run race team_race HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even & ;;
	rockets) run rockets rockets HALO_BOT_SKILL=heroic & ;;
	teamball) run teamball team_oddball HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even & ;;
	vctf) run vctf ctf+vehicles HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even HALO_NETWORK_TEST_PLAYER_DRIVES=1 & ;;
	vslayer) run vslayer slayer+vehicles HALO_BOT_SKILL=heroic & ;;
	vteams) run vteams team_slayer+vehicles HALO_BOT_SKILL=legendary HALO_BOT_TEAMS=even & ;;
	powerups) run powerups slayer HALO_BOT_SKILL=heroic & ;;
	turret)
		if [ ! -e "$ce_maps/extinction.map" ] || [ ! -e "$ce_resources/bitmaps.map" ] ||
			[ ! -e "$ce_resources/sounds.map" ] || [ ! -e "$ce_resources/loc.map" ]; then
			echo "SKIP (turret): no extinction.map in $ce_maps (HALO_TEST_CE_MAPS) or no Halo PC resource maps"
			continue
		fi
		# (Extinction's gun tower at (-444.5 -165.2 11.1), on a platform: the
		# bots set about it every ten seconds from the game's 20th second)
		commands=""
		for tick in $(seq 600 300 $(( (seconds + 5) * 30 ))); do
			commands="${commands}L$tick:@pos 1 -440 -160 11.6;L$tick:@pos 2 -449 -160 11.6;L$tick:@pos 3 -444.5 -158 11.6;"
			commands="${commands}L$tick:@pos 4 -440 -170 11.6;L$tick:@pos 5 -449 -170 11.6;"
		done
		run turret slayer+vehicles MAP=extinction HALO_BOT_SKILL=heroic "HALO_TEST_COMMANDS=$commands" & ;;
	ladders) run ladders oddball MAP=boardingaction HALO_BOT_SKILL=heroic HALO_BOT_PEACEFUL=1 & ;;
	playlist)
		(seconds=$playlist_seconds
			run playlist slayer MAP=chillout HALO_BOT_SKILL=heroic HALO_NETWORK_TEST_SCORE=3 HALO_BOT_PLAYLIST=all \
				HALO_TEST_BOT_PLAYLIST_SEED=3 HALO_TEST_BOT_PLAYLIST_SCORE=3 HALO_TEST_BOT_PLAYLIST_MINUTES=1 \
				HALO_TEST_BOT_PLAYLIST_SCORES=4) & ;;
	playlistcancel)
		mkdir -p "$out"
		write_playlist_file "$out/bots_playlist.txt"
		(seconds=200 playlist_file=$out/bots_playlist.txt
			run playlistcancel slayer MAP=chillout HALO_BOT_SKILL=heroic HALO_NETWORK_TEST_SCORE=2 \
				HALO_BOT_PLAYLIST=custom HALO_TEST_BOT_PLAYLIST_SEED=11 HALO_TEST_BOT_PLAYLIST_SCORE=2 \
				HALO_TEST_BOT_PLAYLIST_MINUTES=1 HALO_TEST_BOT_PLAYLIST_SCORES=3 HALO_TEST_BOT_PLAYLIST_CANCEL=2) & ;;
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
	case $name in playlist | playlistcancel) check_playlist "$name"; return ;; esac
	# (bots.playlist off, as in every other case: the playlist does nothing)
	grep -aq "bots playlist" "$log" && fail "$name" "the playlist logged with bots.playlist off"
	python3 - "$log" "$(case_bots "$name")" "$name" "$map" "$budget" <<'EOF' || status=1
import re, sys, math
log, bots, name, game_map, budget = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], float(sys.argv[5])
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
# (every case: a bot's tick within the budget, halo.log's last bots line)
reports = [line for line in open(log, errors="replace") if "bots: tick " in line and "us/tick" in line]
if reports:
    cost = float(re.search(r"\(([\d.]+) a bot\)", reports[-1]).group(1))
    print("  a bot's tick %.1f us (budget %.0f)" % (cost, budget))
    if cost > budget:
        fail("a bot's tick cost %.1f us, over the budget of %.0f" % (cost, budget))
moves = [line for line in open(log, errors="replace") if "bots: moves at tick" in line]
census = [line for line in open(log, errors="replace") if "bots: vehicles at tick" in line]
if moves:
    print("  moves: %s" % moves[-1].split(": ", 2)[-1].strip())
def moves_number(pattern):
    m = re.search(pattern, moves[-1]) if moves else None
    return int(m.group(1)) if m else 0
# (powerups: the hurt bots to the health packs)
if name == "powerups":
    sought, taken = moves_number(r"health (\d+)/"), moves_number(r"health \d+/(\d+)")
    restored = moves_number(r"bodies restored (\d+)")
    if not moves:
        fail("no moves line in halo.log")
    elif sought < 1:
        fail("no bot went for a health pack")
    elif taken < 1 and restored < 1:
        fail("no bot took a health pack (%d went for one)" % sought)
# (the turret on Extinction: manned, fired from, left)
if name == "turret":
    m = re.search(r"turrets (\d+), manned (\d+) times, ([\d.]+) s of their fire, (\d+) kills", census[-1]) if census else None
    exits = len([line for line in open(log, errors="replace") if re.search(r"bots: bot \d+ out of .*\((no enemy before it|hurt)\)", line)])
    print("  turrets: %s" % (m.group(0) if m else "no census"))
    if not m or int(m.group(1)) < 1:
        fail("no turret found")
    elif int(m.group(2)) < 1:
        fail("no bot manned the turret")
    elif float(m.group(3)) <= 0.0:
        fail("no bot fired from the turret")
    if m and int(m.group(2)) >= 1 and exits < 1:
        fail("no bot got off the turret (no enemy, or hurt)")
    sys.exit(1 if failed else 0)
# (the ladders on Boarding Action: climbed)
if name == "ladders":
    count, climbs = moves_number(r"ladders (\d+)"), moves_number(r"climbed (\d+)")
    if count < 1:
        fail("no ladders found")
    elif climbs < 1:
        fail("no bot climbed a ladder (%d gone to)" % moves_number(r"gone to (\d+)"))
    sys.exit(1 if failed else 0)
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
# CTF and Invasion: each team spawns at its own base (the game's rule, its
# starting locations' team), at the start and after: each life's first place
# nearer its own team's stand (the scenario's flag of its team, which the
# bots log; in Invasion the flags sit at the other base, the spawns do not)
if name in ("ctf", "invasion", "vctf"):
    stands, team_of, alive, spawns, wrong = None, {}, {}, 0, []
    for line in open(log, errors="replace"):
        m = re.search(r"bots: CTF.*the stands: team 0 \(([-\d.]+) ([-\d.]+) [-\d.]+\) team 1 \(([-\d.]+) ([-\d.]+)", line)
        if m:
            stands = ((float(m.group(1)), float(m.group(2))), (float(m.group(3)), float(m.group(4))))
        if "network test: tick " not in line:
            continue
        tick = int(re.search(r"tick (\d+)", line).group(1))
        seen = set()
        for m in re.finditer(r"player (\d+): \(([-\d.]+) ([-\d.]+) [-\d.]+\)[^|]*? s-?\d+ k\d+ d\d+ f\d+ t(-?\d+) m\d+", line):
            i, x, y, t = int(m.group(1)), float(m.group(2)), float(m.group(3)), int(m.group(4))
            seen.add(i)
            if not alive.get(i) and stands and t in (0, 1):
                spawns += 1
                own = math.hypot(x - stands[t][0], y - stands[t][1])
                other = math.hypot(x - stands[1 - t][0], y - stands[1 - t][1])
                if own >= other:
                    wrong.append("tick %d player %d (team %d) at (%.1f %.1f)" % (tick, i, t, x, y))
            alive[i] = True
        for m in re.finditer(r"player (\d+): dead", line):
            alive[int(m.group(1))] = False
    print("  %d spawns, %d at the other team's base" % (spawns, len(wrong)))
    if stands is None:
        fail("no CTF stands in the log")
    for text in wrong[:5]:
        fail("spawned at the other team's base: %s" % text)
# (Invasion is played without fighting: the flags' ways alone, a capture
# asked; the flag starts at its takers' own base, so taking it is nothing)
if name == "invasion":
    if not scores or max(scores) <= 0:
        fail("no capture")
    sys.exit(1 if failed else 0)
# (Team Race: on from checkpoint to checkpoint, not round in circles at one:
# Sidewinder's second, on a ledge, issue #40; the race's score is a player's
# laps and checkpoints)
if name == "race" and (not scores or max(scores) < 2):
    fail("no bot touched a second checkpoint")
if name != "race" and kills <= bots:
    fail("the bots killed only %d times" % kills)
if name != "race" and died * 2 < bots:
    fail("only %d bots died" % died)
# (CTF's score is captures, which three minutes may not see)
if name not in ("ctf", "vctf") and (not scores or max(scores) <= 0):
    fail("no score")
# (the vehicle cases: the bots' census of their vehicles)
if name.startswith("v"):
    census = [line for line in open(log, errors="replace") if "bots: vehicles at tick" in line]
    most = most_driving = most_gunning = 0
    for line in census:
        m = re.search(r"(\d+) of \d+ bots riding \((\d+) driving, (\d+) gunning, (\d+) passengers", line)
        if m:
            most = max(most, int(m.group(1)))
            most_driving = max(most_driving, int(m.group(2)))
            most_gunning = max(most_gunning, int(m.group(3)) + int(m.group(4)))
    last_census = census[-1] if census else ""
    def count(pattern, kind=int):
        m = re.search(pattern, last_census)
        return kind(m.group(1)) if m else kind(0)
    driven, entries, exits = count(r"driven (\d+) units"), count(r"in (\d+) \("), count(r"out (\d+) \(")
    beside = count(r"gunner (\d+)") + count(r"passenger (\d+)")
    print("  vehicles: %s" % (last_census.split("bots: ", 1)[-1].strip() if last_census else "no census"))
    print("  at most %d bots riding at once (%d driving, %d gunning or passengers)" % (most, most_driving, most_gunning))
    if entries < 2:
        fail("the bots got into vehicles %d times" % entries)
    if most < 2:
        fail("at most %d bots rode at once" % most)
    if driven < 150:
        fail("the bots drove only %d units" % driven)
    if exits < 1:
        fail("no bot got out of a vehicle")
    print("  %d times a bot got in beside a teammate driving" % beside)
    if name == "vctf" and beside < 1:
        fail("no bot rode beside a teammate driving")
    # (vctf: the scripted player stops a Warthog at its base a few seconds
    # in, HALO_NETWORK_TEST_PLAYER_DRIVES, for its teammates to get in)
    if name == "vctf":
        with_player = sum(1 for line in open(log, errors="replace") if re.search(r"bots: bot \d+ in .*a player driving", line))
        print("  %d times a bot got in beside the player driving" % with_player)
        if with_player < 1 and game_map == "bloodgulch":
            fail("no bot got into the Warthog the player drove")
sys.exit(1 if failed else 0)
EOF
}

# the playlist cases (port/linux/game/bots_playlist.c's halo.log lines; the
# network test's player lines each second, by game)
check_playlist() { # NAME
	local name=$1 log=$out/$1/run.log
	python3 - "$log" "$(case_bots "$name")" "$name" "$playlist_games" "$budget" <<'EOF' || status=1
import re, sys, math
log, bots, name, wanted_games, budget = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4]), float(sys.argv[5])
lines = [line.rstrip("\n") for line in open(log, errors="replace")]
failed = False
def fail(text):
    global failed
    print("FAIL (%s): %s" % (name, text))
    failed = True
def has(text):
    return any(text in line for line in lines)

if name == "playlistcancel":
    # the custom file: its games, and each bad line skipped with its reason
    expected = {
        6: "a map and a gametype are asked", 7: "more than a map and a gametype",
        8: "no such map in the maps folder", 9: "not a multiplayer map of the level list",
        10: "not a multiplayer map of the level list", 11: "not a file's", 12: "not a file's",
        13: "not a gametype the bots play", 14: "not a gametype the bots play", 15: "the map's name is too long",
        16: "longer than 120 characters", 17: "not plain text", 18: "not plain text",
        19: "not a gametype the bots play",
    }
    skipped = {}
    for line in lines:
        m = re.search(r"bots playlist: bots_playlist\.txt line (\d+) skipped \((.*?)\)", line)
        if m:
            skipped[int(m.group(1))] = m.group(2)
    for number, reason in sorted(expected.items()):
        if number not in skipped:
            fail("bots_playlist.txt line %d not skipped (%s asked)" % (number, reason))
        elif reason not in skipped[number]:
            fail("bots_playlist.txt line %d skipped for %s, not %s" % (number, skipped[number], reason))
    for number in sorted(set(skipped) - set(expected)):
        fail("bots_playlist.txt line %d skipped (%s): a good line" % (number, skipped[number]))
    games = [line for line in lines if re.search(r"bots playlist: \d+ games: ", line)]
    listed = games[-1].split(" games: ", 1)[1] if games else ""
    print("  custom: %s; %d lines skipped" % (listed, len(skipped)))
    if listed != "chillout slayer, prisoner team_slayer, bloodgulch ctf+vehicles, hangemhigh ctf":
        fail("the custom games are %r" % listed)
    if not has("bots_playlist.txt read only to line"):
        fail("the file was read past 8 KB")
    if not has("bots playlist: game 1 started"):
        fail("the first playlist game did not start")
    if not has("bots playlist: (test) B held") or not has("bots playlist: stopped (B"):
        fail("B in the second game's countdown did not stop the playlist")
    if has("bots playlist: game 2 started") or has("bots playlist: next game 3"):
        fail("a playlist game started after B")
    if has("bots playlist: Split Screen left"):
        fail("B left Split Screen (the lobby, not the main menu, is asked)")
    sys.exit(1 if failed else 0)

# the playlist: the games set up and started, each one's player lines
games = []          # [number, map, gametype, started, map unloaded after it, {player: [(x, y)]}, last stats, teams]
current = None
memory = []
order_ok = True
for line in lines:
    m = re.search(r"bots playlist: next game (\d+): (\S+) (\S+)", line)
    if m:
        games.append({"number": int(m.group(1)), "map": m.group(2), "gametype": m.group(3), "started": False,
            "unloaded": None, "places": {}, "last": {}, "teams": {}})
        continue
    m = re.search(r"bots playlist: game (\d+) started", line)
    if m:
        if games and games[-1]["number"] == int(m.group(1)):
            games[-1]["started"] = True
            current = games[-1]
        else:
            fail("game %s started, not the one set up" % m.group(1))
        continue
    if "bots playlist: the scores" in line:
        current = None
        continue
    m = re.search(r"memory: unloaded (\S+): C heap (\d+) KB.*?texture pool (\d+) KB held.*?video memory (\d+) KB free", line)
    if m:
        if m.group(1) == "ui":
            memory.append((int(m.group(2)), int(m.group(3)), int(m.group(4))))
        else:
            for game in reversed(games):
                if game["started"]:
                    if game["unloaded"] is None:
                        game["unloaded"] = m.group(1)
                    break
        continue
    if current is None or "network test: tick " not in line:
        continue
    for m in re.finditer(r"player (\d+): \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)[^|]*? s(-?\d+) k(\d+) d(\d+) f\d+ t(-?\d+) m(\d+)", line):
        index = int(m.group(1))
        current["places"].setdefault(index, []).append((float(m.group(2)), float(m.group(3))))
        current["last"][index] = (int(m.group(5)), int(m.group(6)), int(m.group(7)), int(m.group(9)))
        current["teams"][index] = int(m.group(8))
    for m in re.finditer(r"player (\d+): dead s(-?\d+) k(\d+) d(\d+) f\d+ t(-?\d+) m(\d+)", line):
        index = int(m.group(1))
        current["last"][index] = (int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(6)))
started = [game for game in games if game["started"]]
print("  %d playlist games started" % len(started))
if len(started) < wanted_games:
    fail("%d playlist games started, not %d" % (len(started), wanted_games))
previous = "chillout"
for game in started:
    bot_players = [i for i, v in game["last"].items() if v[3] >= 113]
    walks = []
    for i in bot_players:
        track = game["places"].get(i, [])
        walks.append(sum(math.hypot(x1 - x0, y1 - y0) for (x0, y0), (x1, y1) in zip(track, track[1:])
            if math.hypot(x1 - x0, y1 - y0) < 8.0))
    walks.sort()
    median = walks[len(walks) // 2] if walks else 0.0
    kills = sum(game["last"][i][1] for i in game["last"])
    teams = set(game["teams"].values())
    print("  game %d: %-15s %-21s unloaded %-15s bots %d, walked %5.1f (median), kills %d, teams %s" % (game["number"],
        game["map"], game["gametype"], game["unloaded"] or "-", len(bot_players), median, kills, sorted(teams)))
    if game["map"] == previous:
        fail("game %d: %s twice in a row" % (game["number"], game["map"]))
    previous = game["map"]
    if game["unloaded"] is not None and game["unloaded"] != game["map"]:
        fail("game %d: %s set up, %s played" % (game["number"], game["map"], game["unloaded"]))
    if len(bot_players) != bots:
        fail("game %d: %d bots played, not %d" % (game["number"], len(bot_players), bots))
    # (10: a one-minute game on a small map, the bots fighting more than
    # walking - Battle Creek Team Slayer once gave 14.0 under another job's load)
    if median < 10.0:
        fail("game %d: the bots walked %.1f units (median)" % (game["number"], median))
    team_game = game["gametype"].split("+")[0] in ("team_slayer", "ctf", "team_king", "team_oddball", "team_race", "invasion")
    if team_game and len(teams & {0, 1}) < 2:
        fail("game %d: a team gametype with the teams %s" % (game["number"], sorted(teams)))
    if not team_game and len(teams) < 2:
        fail("game %d: a free-for-all with everyone on team %s" % (game["number"], sorted(teams)))
# memory between games (the menus' level unloaded): from the third on, as the third
print("  memory between games (C heap KB, texture pool KB held, video memory KB free): %s" %
    " ".join("%d/%d/%d" % entry for entry in memory))
if len(memory) < wanted_games:
    fail("%d unloads of the menus between games, not %d" % (len(memory), wanted_games))
if len(memory) >= 3:
    heap, pool, video = memory[2]
    # (the C heap creeps a few tens of KB a change on the harness with or
    # without the playlist - the network test's own game list, Oct 11 2026:
    # 26 to 187 KB a change - as the session's caches fill; a map's data
    # left behind would be megabytes)
    if len(memory) > 3:
        rise = (memory[-1][0] - heap) / float(len(memory) - 3)
        print("  the C heap from the third change on: %+d KB, %.0f KB a change" % (memory[-1][0] - heap, rise))
        if rise > 96 or memory[-1][0] - heap > 1024:
            fail("the C heap rose %d KB over %d changes from the third" % (memory[-1][0] - heap, len(memory) - 3))
    for index, (h, p, v) in enumerate(memory[2:], 3):
        if p > 4096:
            fail("the texture pool held %d KB at the %dth change" % (p, index))
        if v < video - 1024:
            fail("video memory %d KB free at the %dth change, %d KB less than at the third" % (v, index, video - v))
reports = [line for line in lines if "bots: tick " in line and "us/tick" in line]
if reports:
    cost = float(re.search(r"\(([\d.]+) a bot\)", reports[-1]).group(1))
    print("  a bot's tick %.1f us (budget %.0f)" % (cost, budget))
    if cost > budget:
        fail("a bot's tick cost %.1f us, over the budget of %.0f" % (cost, budget))
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
	grep -aq "bots playlist" "$log" && fail "$name" "the playlist logged with bots.playlist off"
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
objective = {"slayer": "kills %d" % kills, "team_slayer": "kills %d" % kills, "rockets": "kills %d" % kills,
    "ctf": "flag taken %d (stand reached %d), captures %d" % (flags, stands, max(best, 0)), "king": "best score %d" % best,
    "invasion": "flag taken %d, captures %d" % (flags, max(best, 0)),
    "team_race": "best score %d (laps and checkpoints)" % best,
    "oddball": "ball taken %d, best score %d" % (balls, best),
    "team_oddball": "ball taken %d, best score %d" % (balls, best)}[game]
def share(count):
    return 100.0 * count / searches if searches else 0.0
print("  %-26s walked %4.0f (least %4.0f) spread %3.0f of %3.0f  searches %5d failed %4.1f%% short %4.1f%% unavoided %4.1f%%"
    "  %4.1f us a bot  %s" % (name, median(walks), walks[0] if walks else 0, median(spreads), extent, searches,
    share(failures), share(short), share(unavoided), cost, objective))
if len(bot_players) != bots:
    failed.append("%d bots played, not %d" % (len(bot_players), bots))
if median(spreads) < min(25.0, extent / 3.0):
    failed.append("the bots spread over %.0f units, the map %.0f" % (median(spreads), extent))
# the games whose goal the walkable surfaces (the AI's data the bots' path
# searches read, built by tool with each map) do not reach from the ground the
# bots play on: the bots go as near as they can, jump, and play on (roaming
# and fighting are still checked)
KNOWN = {
    "boardingaction-ctf": "each ship's rooms are islands of walkable surfaces, the flags in others",
    "boardingaction-invasion": "each ship's rooms are islands of walkable surfaces, the flags in others",
    "boardingaction-oddball": "each ship's rooms are islands of walkable surfaces, the ball's spawn in another",
    "boardingaction-team_oddball": "each ship's rooms are islands of walkable surfaces, the ball's spawn in another",
    "damnation-team_race": "the first checkpoint is on the walkway over the middle, which the bots' paths end under",
    "damnation-oddball": "the ball's spawn is picked at random, some on ledges no walkable surface reaches",
    "damnation-team_oddball": "the ball's spawn is picked at random, some on ledges no walkable surface reaches",
    "hangemhigh-king": "the hill is on a ledge no walkable surface reaches",
    "hangemhigh-oddball": "the ball spawns on a ledge no walkable surface reaches",
    "hangemhigh-team_oddball": "the ball spawns on a ledge no walkable surface reaches",
    "prisoner-ctf": "the flags are on the upper floor, which no walkable surface reaches",
    "prisoner-invasion": "the flags are on the upper floor, which no walkable surface reaches",
    "prisoner-oddball": "the ball spawns on the upper floor",
    "prisoner-team_oddball": "the ball spawns on the upper floor",
    "putput-king": "the hill is in a room the walkable surfaces and teleporters do not reach",
    "putput-invasion": "the bases are rooms and teleporters apart, most searches short: a flag taken, never run home in time (CTF captures none either)",
    "wizard-ctf": "the flags are on corner platforms no walkable surface reaches",
    "wizard-invasion": "the flags are on corner platforms no walkable surface reaches",
    "wizard-king": "the hill is on a platform no walkable surface reaches",
    "wizard-oddball": "the ball spawns on a platform no walkable surface reaches",
    "wizard-team_oddball": "the ball spawns on a platform no walkable surface reaches",
}
# (a game whose goal is out of reach: the bots near it, jumping and coming
# back to it, walk less; half the walk is asked there)
least_walk = 20.0 if name in KNOWN else 40.0
if walks and walks[0] < least_walk:
    failed.append("a bot walked only %.0f units" % walks[0])
objective_failed = None
if game in ("slayer", "team_slayer", "rockets") and kills <= 0:
    failed.append("no kills")
# (a bot at the other team's stand would have taken the flag had it been
# there: on the big maps three minutes of fighting may not see one taken)
if game == "ctf" and flags <= 0 and best <= 0 and stands <= 0:
    objective_failed = "no flag taken, no bot at the other team's stand"
if game == "king" and best <= 0:
    objective_failed = "no hill held"
if game in ("oddball", "team_oddball") and balls <= 0 and best <= 0:
    objective_failed = "no ball taken"
# (Invasion: the flag a team takes lies at its own base, so taking it is
# nothing: a capture, the flag run to the other base, is asked)
if game == "invasion" and best <= 0:
    objective_failed = "no capture"
# (Team Race: a checkpoint touched, the bots driving the race's vehicles or
# on foot)
if game == "team_race" and best <= 0:
    objective_failed = "no checkpoint touched"
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
