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
#   iron     Iron CTF (the game's own variant: Scorpions alone, one at each
#            base), 15 bots on both teams, Heroic: a bot must drive a
#            Scorpion within 45 s of play (bots.c's tanks are worth a detour;
#            the owner's Blood Gulch game, Oct 11 2026, saw the first at 87 s)
#   vehicles vctf, vslayer, vteams and iron (HALO_TEST_BOTS 8 is the brief's)
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
#   lanes    CTF, Bot routes Lanes (HALO_BOT_ROUTES=lanes), 12 bots on both
#            teams, Heroic, not fighting (HALO_BOT_PEACEFUL=1: the flags'
#            ways alone): Blood Gulch's side lanes found (two), and the bots
#            crossing the middle of the map (the middle fifth of the direct
#            route bots_routes.c logs) by two routes at least: the attackers'
#            first crossing of each life (its offset from the direct route,
#            the mean while the bot is there), by the lane halo.log's roles
#            line gives them, two lanes' means 12 units apart at least; a
#            lane's waypoint passed (halo.log's tactics line); every bot's
#            crossings are printed
#   vantage  Snipers (every player a sniper rifle), 6 bots, Heroic, not
#            fighting: the map's vantage points logged (three at least), and a
#            bot comes to one (halo.log's arrival, within 2.5 units of a point
#            logged, its place each second) within HALO_TEST_VANTAGE_SECONDS
#            (90) and holds it ten seconds at least
#   roles    CTF, 8 bots on both teams, Heroic, fighting: each team's
#            defenders (halo.log's roles line) about their own flag stand -
#            within 25 units of it half their time alive from the 20th second
#            on, one a team at least - the attackers' time there printed too
#   precision  Slayer, 4 bots, Heroic, not fighting, on foot
#            (HALO_BOT_VEHICLES=0: a bot driving goes where it drives),
#            Bot routes Direct: the bots' precision weapons
#            (bots.c's bots_weapon_precision, from the weapons' tags): halo.log
#            names the sniper rifle a long one and the pistol a middle one, the
#            plasma pistol none; 10 s in, the first bot is given a sniper
#            rifle (HALO_TEST_COMMANDS' @weapon) and the others shotguns: the
#            first comes to a vantage point (halo.log's arrival, for its long
#            precision weapon, before the swap) and holds it 10 s (its places
#            within 2.5 units, or until the swap); 70 s in (45 s in
#            precision2) it is given a shotgun instead: holding a vantage point then, it leaves it
#            (halo.log: off it, weapon) and is 5 units from it within 30 s,
#            else walks 15 units in them; and it comes to none for a long
#            precision weapon in them (about the map again)
#   precisionlanes  precision with Bot routes Lanes
#   precision2  precision on Hang 'em High (HALO_TEST_PRECISION_MAP)
#   precisions  precision, precisionlanes and precision2
#   tactics  lanes, vantage, roles and the precision cases
#   teleporters  CTF, 8 bots on both teams, Heroic, not fighting, on foot
#            (HALO_BOT_VEHICLES=0), Bot routes Direct, every bot killed
#            every 40 s (HALO_TEST_COMMANDS' @kill: it respawns at its base
#            and sets off for the other's flag again): Blood Gulch's two
#            teleporters (from beside each base's flag out to the canyon)
#            logged, and taken as shortcuts (bots.c's bots_shortcut_weigh):
#            bots sent through one as a shortcut five times at least
#            (halo.log's moves line), two bots at least, one each way (the
#            teleporters line), never a defender (halo.log's roles line);
#            the shortcuts' census sane (sent no more than taken, taken no
#            more than weighed); every shortcut taken, sent and failed
#            printed
#   teleporters2  teleporters on Chill Out (HALO_TEST_TELEPORTER_MAP): three
#            times at least (SKIP without the map)
#   lastway  Oddball on Chiron TL34 (putput), 8 bots, not fighting: its
#            rooms are joined by teleporters alone, the last way to a goal
#            the walkable surfaces do not reach (bots_search_teleporter):
#            bots sent through as the last way three times at least (SKIP
#            without putput.map)
#   teleports  teleporters, teleporters2 and lastway
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
cases=$(echo " $cases " | sed 's/ vehicles / vctf vslayer vteams iron /; s/ new / powerups turret ladders /; s/ playlists / playlist playlistcancel /; s/ tactics / lanes vantage roles precisions /; s/ precisions / precision precisionlanes precision2 /; s/ teleports / teleporters teleporters2 lastway /')
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
	iron) echo 15 ;;
	playlist) echo 6 ;;
	playlistcancel) echo 3 ;;
	ladders) echo 8 ;;
	lanes) echo 12 ;;
	precision*) echo 4 ;;
	vantage) echo 6 ;;
	roles | squads) echo 8 ;;
	teleporters* | lastway) echo 8 ;;
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
	iron) run iron ironctf HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even & ;;
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
	lanes) run lanes ctf HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even HALO_BOT_PEACEFUL=1 HALO_BOT_ROUTES=lanes HALO_BOT_VEHICLES=0 & ;;
	vantage) run vantage snipers HALO_BOT_SKILL=heroic HALO_BOT_PEACEFUL=1 & ;;
	precision | precisionlanes | precision2)
		# (the first bot a sniper rifle 10 s in, before the vantage points
		# are made, the others shotguns - which they keep over a sniper rifle
		# lying about, worth no more to them; a shotgun for the first 70 s in,
		# 45 s on Hang 'em High, whose vantage points are nearer)
		routes=direct precision_map=$map swap=2100
		[ "$name" = precisionlanes ] && routes=lanes
		[ "$name" = precision2 ] && precision_map=${HALO_TEST_PRECISION_MAP:-hangemhigh} swap=1350
		commands="L300:@weapon 1 sniper rifle;L300:@weapon 2 shotgun;L300:@weapon 3 shotgun;L300:@weapon 4 shotgun;"
		commands="${commands}L$swap:@weapon 1 shotgun"
		run "$name" slayer MAP="$precision_map" HALO_BOT_SKILL=heroic HALO_BOT_PEACEFUL=1 HALO_BOT_VEHICLES=0 \
			HALO_BOT_ROUTES=$routes "HALO_TEST_COMMANDS=$commands" & ;;
	roles) run roles ctf HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even & ;;
	squads) run squads ctf HALO_BOT_SKILL=normal HALO_BOT_TEAMS=even HALO_BOT_ROUTES=mixed HALO_BOT_VEHICLES=0 & ;;
	teleporters | teleporters2)
		# (every bot killed every 40 s from the 40th (HALO_TEST_COMMANDS'
		# @kill): each respawns at its own base, and leaves it again for the
		# other's flag - the way the teleporters shorten)
		teleporter_map=$map
		[ "$name" = teleporters2 ] && teleporter_map=${HALO_TEST_TELEPORTER_MAP:-chillout}
		if [ ! -e "$data/maps/$teleporter_map.map" ]; then
			echo "SKIP ($name): no $teleporter_map.map in $data/maps"
			continue
		fi
		commands=""
		for tick in $(seq 1200 1200 $(( (seconds - 15) * 30 ))); do
			for player in $(seq 1 "$(case_bots "$name")"); do
				commands="${commands}L$tick:@kill $player;"
			done
		done
		run "$name" ctf MAP="$teleporter_map" HALO_BOT_SKILL=heroic HALO_BOT_TEAMS=even HALO_BOT_PEACEFUL=1 \
			HALO_BOT_VEHICLES=0 HALO_BOT_ROUTES=direct "HALO_TEST_COMMANDS=$commands" & ;;
	lastway)
		if [ ! -e "$data/maps/putput.map" ]; then
			echo "SKIP (lastway): no putput.map (Chiron TL34) in $data/maps"
			continue
		fi
		run lastway oddball MAP=putput HALO_BOT_SKILL=heroic HALO_BOT_PEACEFUL=1 & ;;
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
	python3 - "$log" "$(case_bots "$name")" "$name" "$map" "$budget" "${HALO_TEST_VANTAGE_SECONDS:-90}" <<'EOF' || status=1
import re, sys, math
log, bots, name, game_map, budget = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], float(sys.argv[5])
vantage_seconds = float(sys.argv[6])
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
# (the teleporter cases: bots.c's shortcuts through the map's teleporters,
# and the last way on Chiron TL34; halo.log's moves line's census, the
# teleporters line, each shortcut taken and each bot sent)
if name in ("teleporters", "teleporters2", "lastway"):
    text = open(log, errors="replace").read().splitlines()
    found = [line.split("bots: ", 1)[1].strip() for line in text if re.search(r"bots: teleporter \d+ at \(", line)]
    print("  %d teleporters: %s" % (len(found), "; ".join(found)))
    census = re.search(r"teleporters (\d+) \(shortcuts: weighed (\d+), taken (\d+), through (\d+), failed (\d+), dropped (\d+); "
        r"through as the last way (\d+), by chance (\d+)\)", moves[-1]) if moves else None
    if not census:
        fail("no teleporters' census in halo.log's moves line")
        sys.exit(1)
    count, weighed, taken, through, failed_count, dropped, last_way, chance = [int(v) for v in census.groups()]
    print("  shortcuts: weighed %d, taken %d, through %d, failed %d, dropped %d; through as the last way %d, by chance %d" %
        (weighed, taken, through, failed_count, dropped, last_way, chance))
    per = [line.split("teleporters at tick", 1)[1].strip() for line in text if "bots: teleporters at tick" in line]
    last_tick = per[-1].split(":", 1)[0] if per else ""
    sent_by = {}
    for line in per:
        if line.split(":", 1)[0] == last_tick:
            for m in re.finditer(r"(\d+) sent (\d+) \(shortcut (\d+), last way (\d+), chance (\d+)\) failed (\d+)", line):
                sent_by[int(m.group(1))] = [int(v) for v in m.groups()[1:]]
    print("  by teleporter: %s" % ", ".join("%d sent %d (shortcut %d, last way %d, chance %d) failed %d" % tuple([k] + v)
        for k, v in sorted(sent_by.items())))
    events = [line.split("bots: ", 1)[1].strip() for line in text if re.search(r"bots: (bot \d+ (to|through|off) teleporter|teleporter \d+ failed)", line)]
    for line in events[:40]:
        print("  %s" % line)
    if len(events) > 40:
        print("  ... %d more" % (len(events) - 40))
    if count != len(found):
        fail("the census counts %d teleporters, halo.log logged %d" % (count, len(found)))
    if name == "lastway":
        if count < 10:
            fail("%d teleporters found on Chiron TL34" % count)
        if last_way < 3:
            fail("bots sent through as the last way %d times (3 asked)" % last_way)
        sys.exit(1 if failed else 0)
    if count < 2:
        fail("%d teleporters found on %s" % (count, "Blood Gulch" if name == "teleporters" else "the map"))
    if through > taken or taken > weighed:
        fail("the census is not sane: weighed %d, taken %d, through %d" % (weighed, taken, through))
    # (each bot sent, to the census's moves line: the last's count)
    last_moves = max(i for i, line in enumerate(text) if "bots: moves at tick" in line)
    sent = [(int(m.group(1)), int(m.group(2))) for line in text[:last_moves] for m in [re.search(r"bots: bot (\d+) through teleporter (\d+)", line)] if m]
    if len(sent) != through:
        fail("%d bots sent logged, the census counts %d" % (len(sent), through))
    least = 5 if name == "teleporters" else 3
    if through < least:
        fail("bots sent through a teleporter as a shortcut %d times (%d asked)" % (through, least))
    if len(set(bot for bot, teleporter in sent)) < 2:
        fail("one bot alone took the teleporters")
    if name == "teleporters" and len(set(teleporter for bot, teleporter in sent)) < 2:
        fail("the bots took one of Blood Gulch's teleporters alone (one each way asked)")
    # (never a defender: halo.log's last roles line)
    defenders = set()
    for line in text:
        if "bots: roles (" in line:
            defenders = set(int(m.group(1)) for m in re.finditer(r"bot (\d+) team \d+ defender", line))
    takers = set(int(m.group(1)) for line in text for m in [re.search(r"bots: bot (\d+) to teleporter", line)] if m)
    print("  takers %s, defenders %s" % (sorted(takers), sorted(defenders)))
    if takers & defenders:
        fail("a defender took a teleporter: bots %s" % sorted(takers & defenders))
    sys.exit(1 if failed else 0)
# (the precision cases: the bot given the sniper rifle, which holds a vantage
# point)
precision_subject = None
for line in open(log, errors="replace"):
    m = re.search(r"test command: player \d+ \(machine (\d+)\) given .*sniper rifle: held", line)
    if m:
        precision_subject = int(m.group(1))
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
    # (the tactics cases: a defender keeps about its stand, a sniper holds
    # its vantage points; bots_roles.c's roles line)
    kept = (name.startswith("precision") and machine == precision_subject) or name in ("lanes", "vantage", "roles") and any(
        re.search(r"bot %d team \d+ (defender|sniper)" % (machine - 112), line) for line in open(log, errors="replace")
        if "bots: roles (" in line)
    if walked < 60.0 and not kept:
        fail("%s walked only %.1f units" % (named.get(i, "?"), walked))
    if spread < 25.0 and not kept:
        fail("%s stayed within %.1f units" % (named.get(i, "?"), spread))
scores = [v[0] for v in last.values()]
print("  bots: %d kills, %d deaths; best score %d" % (kills, deaths, max(scores) if scores else 0))
# (the tactics cases: bots_routes.c, bots_vantage.c, bots_roles.c)
lines = open(log, errors="replace").read().splitlines()
tactics = [line for line in lines if "bots: tactics at tick" in line]
if tactics:
    print("  tactics: %s" % tactics[-1].split("bots: ", 1)[-1].strip())
def tracks():
    # each player's places each second, by tick (the network test's lines): {player: [(tick, x, y, team, machine)]}
    result = {}
    for line in lines:
        if "network test: tick " not in line:
            continue
        tick = int(re.search(r"tick (\d+)", line).group(1))
        for m in re.finditer(r"player (\d+): \(([-\d.]+) ([-\d.]+) [-\d.]+\)[^|]*? s-?\d+ k\d+ d\d+ f\d+ t(-?\d+) m(\d+)", line):
            result.setdefault(int(m.group(1)), []).append((tick, float(m.group(2)), float(m.group(3)), int(m.group(4)), int(m.group(5))))
    return result
def roles_of():
    # halo.log's last roles line: {bot number: (team, role, lane)}
    result = {}
    for line in lines:
        if "bots: roles (" in line:
            result = {}
            for m in re.finditer(r"bot (\d+) team (\d+) (\w+)(?: \(([\w ]+)\))?", line.split("): ", 1)[1]):
                result[int(m.group(1))] = (int(m.group(2)), m.group(3), m.group(4) or "")
    return result
if name == "lanes":
    route = None
    for line in lines:
        m = re.search(r"bots: routes: walkable graph.*the direct route [\d.]+ units:(.*)", line)
        if m:
            route = [(float(a), float(b)) for a, b in re.findall(r"\(([-\d.]+) ([-\d.]+)\)", m.group(1))]
    lanes = [line for line in lines if re.search(r"bots: routes: lane \d+ by", line)]
    print("  %d side lanes: %s" % (len(lanes), "; ".join(l.split("routes: ", 1)[1].strip() for l in lanes)))
    if len(lanes) < 2:
        fail("%d side lanes found on Blood Gulch, not 2" % len(lanes))
    passed = re.search(r"waypoints passed (\d+)", tactics[-1]) if tactics else None
    if not passed or int(passed.group(1)) < 1:
        fail("no lane's waypoint passed")
    if not route or len(route) < 2:
        fail("no direct route in halo.log")
        sys.exit(1)
    lengths = [math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(route, route[1:])]
    total = sum(lengths)
    def place(x, y):
        best = None
        walked = 0.0
        for (ax, ay), (bx, by), length in zip(route, route[1:], lengths):
            dx, dy = bx - ax, by - ay
            u = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (length * length))) if length > 0 else 0.0
            cx, cy = ax + dx * u, ay + dy * u
            d = math.hypot(x - cx, y - cy)
            side = 1.0 if dx * (y - ay) - dy * (x - ax) >= 0 else -1.0
            if best is None or d < best[0]:
                best = (d, (walked + length * u) / total, d * side)
            walked += length
        return best[1], best[2]
    roles = roles_of()
    # each attacker's crossings of the middle, and the first of each life
    # (from its base: the way its lane takes it)
    crossings, firsts = {}, {}
    for player, track in tracks().items():
        if track[0][4] < 113:
            continue
        bot = track[0][4] - 112
        inside, previous, first_done = [], None, False
        for tick, x, y, team, machine in track + [(None, None, None, None, None)]:
            respawn = x is not None and previous is not None and math.hypot(x - previous[0], y - previous[1]) > 8.0
            along = offset = None
            if x is not None and not respawn:
                along, offset = place(x, y)
            if along is not None and 0.4 <= along <= 0.6:
                inside.append(offset)
                previous = (x, y)
                continue
            if len(inside) >= 2:
                crossings.setdefault(bot, []).append(sum(inside) / len(inside))
                if not first_done:
                    firsts.setdefault(bot, []).append(sum(inside) / len(inside))
                    first_done = True
            inside = []
            if respawn:
                first_done = False
            previous = (x, y) if x is not None else None
    by_lane = {}
    for bot, offsets in sorted(crossings.items()):
        team, role, lane = roles.get(bot, (None, "?", ""))
        print("  bot %d (%s%s): crossed the middle %d times, offsets %s; the first of each life %s" % (bot, role,
            " " + lane if lane else "", len(offsets), " ".join("%+.0f" % o for o in offsets),
            " ".join("%+.0f" % o for o in firsts.get(bot, []))))
        if role in ("attacker", "roamer"):
            by_lane.setdefault(lane, []).extend(firsts.get(bot, []))
    means = dict((lane, sum(v) / len(v)) for lane, v in by_lane.items() if v)
    print("  the attackers' first crossings by lane (mean offset from the direct route): %s" %
        ", ".join("%s %+.1f (%d)" % (lane, means[lane], len(by_lane[lane])) for lane in sorted(means)))
    if len(means) < 2 or max(means.values()) - min(means.values()) < 12.0:
        fail("the attackers crossed the middle by one route (their lanes' mean offsets %s)" %
            ", ".join("%s %+.1f" % (lane, means[lane]) for lane in sorted(means)))
    sys.exit(1 if failed else 0)
if name == "vantage":
    points = []
    for line in lines:
        m = re.search(r"bots: vantage point (\d+) at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)", line)
        if m:
            points.append((float(m.group(2)), float(m.group(3))))
    arrivals = [line for line in lines if re.search(r"bots: bot \d+ \(\w+\) at vantage point", line)]
    print("  %d vantage points, %d arrivals" % (len(points), len(arrivals)))
    if len(points) < 3:
        fail("%d vantage points logged on Blood Gulch, not 3 at least" % len(points))
    first_tick = None
    best = (0, None, None)
    for player, track in tracks().items():
        if track[0][4] < 113:
            continue
        if first_tick is None or track[0][0] < first_tick:
            first_tick = track[0][0]
        run_start, run_point, run_length = None, None, 0
        for tick, x, y, team, machine in track:
            near = [i for i, (px, py) in enumerate(points) if math.hypot(x - px, y - py) < 2.5]
            if near and near[0] == run_point:
                run_length += 1
            else:
                run_start, run_point, run_length = (tick if near else None), (near[0] if near else None), (1 if near else 0)
            if run_point is not None and run_length > best[0] and run_start is not None:
                best = (run_length, run_start, (track[0][4] - 112, run_point + 1))
    if best[1] is None:
        fail("no bot came to a vantage point")
    else:
        print("  bot %d held vantage point %d for %d s from %.0f s" % (best[2][0], best[2][1], best[0], (best[1] - first_tick) / 30.0))
        if best[0] < 10:
            fail("no bot held a vantage point 10 s (%d at most)" % best[0])
        held_from = None
        for player, track in tracks().items():
            if track[0][4] < 113:
                continue
            run_point, run_length = None, 0
            for tick, x, y, team, machine in track:
                near = [i for i, (px, py) in enumerate(points) if math.hypot(x - px, y - py) < 2.5]
                run_length = run_length + 1 if near and near[0] == run_point else (1 if near else 0)
                run_point = near[0] if near else None
                if run_length == 10:
                    started = (tick - first_tick) / 30.0 - 9
                    held_from = started if held_from is None else min(held_from, started)
        if held_from is None or held_from > vantage_seconds:
            fail("no bot came to a vantage point and held it 10 s within %.0f s" % vantage_seconds)
    sys.exit(1 if failed else 0)
if name.startswith("precision"):
    # (each line's tick: the network test's last before it, a second apart)
    events, tick = [], 0
    for line in lines:
        m = re.search(r"network test: tick (\d+)", line)
        if m:
            tick = int(m.group(1))
        else:
            events.append((tick, line))
    kinds = [line.split("bots: weapon ", 1)[1].strip() for line in lines if re.search(r"bots: weapon .*: (precision|heavy)", line)]
    print("  precision weapons: %s" % "; ".join(kinds))
    if not any(re.search(r"sniper rifle: precision, long", k) for k in kinds):
        fail("the sniper rifle not named a long precision weapon")
    if not any(re.search(r"\\pistol\\pistol: precision, middle", k) for k in kinds):
        fail("the pistol not named a middle precision weapon")
    if any("plasma pistol" in k for k in kinds):
        fail("the plasma pistol named a precision weapon")
    gave = swap = None
    for line in lines:
        m = re.search(r"test command at tick (\d+): @weapon 1 (.*)", line)
        if m and "sniper" in m.group(2):
            gave = int(m.group(1))
        elif m:
            swap = int(m.group(1))
    if precision_subject is None or precision_subject < 113 or gave is None or swap is None:
        fail("no bot was given the sniper rifle (%s), or no swap" % precision_subject)
        sys.exit(1)
    bot = precision_subject - 112
    points = {}
    for line in lines:
        m = re.search(r"bots: vantage point (\d+) at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)", line)
        if m:
            points[int(m.group(1))] = (float(m.group(2)), float(m.group(3)))
    print("  %d vantage points" % len(points))
    track = [(t, x, y) for player, tr in tracks().items() for t, x, y, team, machine in tr if machine == precision_subject]
    mine = [(t, line) for t, line in events if re.search(r"bots: bot %d \(\w+\) (at|off|holds) vantage point" % bot, line)]
    for t, line in mine:
        print("  %5.0f s %s" % (t / 30.0, line.split("bots: ", 1)[1].strip()))
    for t, line in events:
        if "for its middle precision" in line:
            print("  (a pistol's) %5.0f s %s" % (t / 30.0, line.split("bots: ", 1)[1].strip()))
    arrivals = [(t, int(m.group(1))) for t, line in mine
        for m in [re.search(r"(?:at|holds) vantage point (\d+) .*for its long precision", line)] if m and gave <= t <= swap]
    if not arrivals:
        fail("bot %d came to no vantage point with its sniper rifle in %.0f s" % (bot, (swap - gave) / 30.0))
    else:
        arrived, point = arrivals[0]
        px, py = points.get(point, (0.0, 0.0))
        held, run_start = 0, None
        for t, x, y in track:
            if t < arrived - 30:
                continue
            if math.hypot(x - px, y - py) < 2.5:
                run_start = t if run_start is None else run_start
                held = max(held, (min(t, swap) - run_start) // 30 + 1)
            else:
                run_start = None
            if t > swap:
                break
        still = run_start is not None and run_start < swap
        print("  bot %d at vantage point %d %.0f s after the sniper rifle, held it %d s%s" % (bot, point, (arrived - gave) / 30.0,
            held, " (until the swap)" if still else ""))
        if held < 10 and not (still and held >= 3):
            fail("bot %d held its vantage point %d s" % (bot, held))
    # the swap: off its vantage point (weapon) when it held one, none after
    state = None
    for t, line in mine:
        if t < swap:
            state = "off" if " off vantage point" in line else "at"
    left = [t for t, line in mine if swap <= t <= swap + 4 * 30 and "(weapon)" in line]
    print("  at the swap (%.0f s) bot %d %s; off for its weapon %s" % (swap / 30.0, bot,
        "held a vantage point" if state == "at" else "held none", "%.0f s after" % ((left[0] - swap) / 30.0) if left else "never"))
    if state == "at" and not left:
        fail("bot %d kept its vantage point with a shotgun" % bot)
    after = [t for t, line in mine if swap + 4 * 30 < t <= swap + 34 * 30 and "for its long precision" in line and
        " at vantage point" in line]
    if after:
        fail("bot %d came to a vantage point %.0f s after the swap" % (bot, (after[0] - swap) / 30.0))
    walked, away = 0.0, 0.0
    window = [(x, y) for t, x, y in track if swap <= t <= swap + 30 * 30]
    for (x0, y0), (x1, y1) in zip(window, window[1:]):
        if math.hypot(x1 - x0, y1 - y0) < 8.0:
            walked += math.hypot(x1 - x0, y1 - y0)
    held_point = None
    for t, line in mine:
        m = re.search(r" (?:at|holds) vantage point (\d+)", line)
        if t < swap and m:
            held_point = points.get(int(m.group(1)))
    if state == "at" and held_point:
        away = max([math.hypot(x - held_point[0], y - held_point[1]) for x, y in window] or [0.0])
    print("  bot %d walked %.1f units in the 30 s after the swap%s" % (bot, walked,
        ", %.1f units from its vantage point at most" % away if state == "at" else ""))
    if state == "at" and away < 5.0:
        fail("bot %d stayed by its vantage point with a shotgun" % bot)
    elif state != "at" and walked < 15.0:
        fail("bot %d walked only %.1f units after the swap" % (bot, walked))
    sys.exit(1 if failed else 0)
if name == "roles":
    stands = None
    for line in lines:
        m = re.search(r"bots: CTF.*the stands: team 0 \(([-\d.]+) ([-\d.]+) [-\d.]+\) team 1 \(([-\d.]+) ([-\d.]+)", line)
        if m:
            stands = ((float(m.group(1)), float(m.group(2))), (float(m.group(3)), float(m.group(4))))
    roles = roles_of()
    if not stands or not roles:
        fail("no CTF stands or no roles in halo.log")
        sys.exit(1)
    near = {}
    for player, track in tracks().items():
        if track[0][4] < 113:
            continue
        bot = track[0][4] - 112
        start = track[0][0] + 20 * 30
        samples = [(x, y, team) for tick, x, y, team, machine in track if tick >= start and team in (0, 1)]
        if samples:
            near[bot] = sum(1 for x, y, team in samples if math.hypot(x - stands[team][0], y - stands[team][1]) < 25.0) / float(len(samples))
    for team in (0, 1):
        defenders = [bot for bot, (t, role, lane) in roles.items() if t == team and role == "defender"]
        others = [bot for bot, (t, role, lane) in roles.items() if t == team and role != "defender"]
        print("  team %d: defenders %s near their stand %s of their time; the others %s" % (team, defenders,
            " ".join("%.0f%%" % (100 * near.get(b, 0)) for b in defenders),
            " ".join("%.0f%%" % (100 * near.get(b, 0)) for b in others)))
        if not defenders:
            fail("team %d has no defender" % team)
        elif max(near.get(b, 0) for b in defenders) < 0.5:
            fail("team %d's defenders were near their stand %.0f%% of their time at most" % (team, 100 * max(near.get(b, 0) for b in defenders)))
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
if name in ("ctf", "invasion", "vctf", "iron"):
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
if name not in ("ctf", "vctf", "iron", "roles") and (not scores or max(scores) <= 0):
    fail("no score")
# (the vehicle cases: the bots' census of their vehicles)
if name.startswith("v") or name == "iron":
    census = [line for line in open(log, errors="replace") if "bots: vehicles at tick" in line]
    most = most_driving = most_gunning = 0
    insane = []
    last_riding = 0.0
    for line in census:
        m = re.search(r"(\d+) of (\d+) bots riding \((\d+) driving, (\d+) gunning, (\d+) passengers\), (\d+) going to one; "
            r"vehicles (\d+), (\d+) with riders.*? in (\d+) \(driver (\d+), gunner (\d+), passenger (\d+)\), out (\d+) .*?"
            r"in by kind: tank (\d+), jeep (\d+), scout (\d+); first in at tick (\d+); ([\d.]+) s riding", line)
        if not m:
            insane.append("a census line not read: %s" % line.strip()[:120])
            continue
        (riding, alive, driving, gunning, passengers, going, vehicles, occupied, entries, by_driver, by_gunner, by_passenger,
            out, tanks, jeeps, scouts, first, ride_s) = [float(v) if "." in v else int(v) for v in m.groups()]
        most = max(most, riding)
        most_driving = max(most_driving, driving)
        most_gunning = max(most_gunning, gunning + passengers)
        # (the census sane: its counts within each other)
        tick = re.search(r"vehicles at tick (\d+)", line).group(1)
        if riding != driving + gunning + passengers or riding + going > alive or occupied > vehicles or \
                entries != by_driver + by_gunner + by_passenger or out > entries or tanks + jeeps + scouts > entries or \
                (entries > 0) != (first > 0) or ride_s + 0.05 < last_riding:
            insane.append("tick %s: %s" % (tick, line.split("bots: ", 1)[-1].strip()[:200]))
        last_riding = ride_s
    for text in insane[:3]:
        fail("the census is not sane: %s" % text)
    last_census = census[-1] if census else ""
    def count(pattern, kind=int):
        m = re.search(pattern, last_census)
        return kind(m.group(1)) if m else kind(0)
    driven, entries, exits = count(r"driven (\d+) units"), count(r"in (\d+) \("), count(r"out (\d+) \(")
    beside = count(r"gunner (\d+)") + count(r"passenger (\d+)")
    print("  vehicles: %s" % (last_census.split("bots: ", 1)[-1].strip() if last_census else "no census"))
    print("  at most %d bots riding at once (%d driving, %d gunning or passengers)" % (most, most_driving, most_gunning))
    # (when they got in, and in what: the entries' lines, at their tick)
    first_by = {}
    for line in open(log, errors="replace"):
        m = re.search(r"bots: bot \d+ in (\S+) seat \d+ \((\w+).*at tick (\d+)", line)
        if m and m.group(2) == "driver":
            kind = next((k for k in ("scorpion", "ghost", "warthog") if k in m.group(1)), "other")
            first_by.setdefault(kind, int(m.group(3)))
    print("  first driver in: %s; %s" % (", ".join("%s %.0f s" % (k, t / 30.0) for k, t in sorted(first_by.items())) or "none",
        re.search(r"in by kind: .*", last_census).group(0).strip() if "in by kind" in last_census else "no census"))
    # (Iron CTF: the Scorpions taken, soon - a tank is worth a walk back to
    # it, bots.c's bots_vehicle_worths; the owner's game saw none for 87 s)
    if name == "iron" and game_map == "bloodgulch" and first_by.get("scorpion", 10 ** 9) > 45 * 30:
        fail("no bot drove a Scorpion in the first 45 s of play")
    if entries < 2:
        fail("the bots got into vehicles %d times" % entries)
    if most < 2 and name != "iron":
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
    # (the run's last game, cut off as the run ends - never unloaded - is
    # not asked to have been walked)
    cut_off = game["unloaded"] is None and game is games[-1]
    if median < 10.0 and not cut_off:
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
