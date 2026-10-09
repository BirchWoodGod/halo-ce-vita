#!/bin/bash
# A first start without the movies (README: they are optional), on the Linux
# harness (any build of this tree: the Linux build plays no movie, so every
# movie is missing, as on a Vita without ux0:data/haloce-vita/movies/), every
# press scripted (HALO_TEST_PAD), the screens named in the log (HALO_UI_LOG).
# Each case starts fresh: its own copy of the harness (so its own config.toml
# beside it), an empty data folder (no bink folder, no settings) and an empty
# save folder (no profiles, no playlists).
#
#   fresh    the start-up filesystem checks made to take 6 s
#            (HALO_TEST_FILESYSTEM_CHECK_MS, as on a Vita's memory card): the
#            main menu comes up once they end and takes the first press made
#            after it is up - before, it came up at once and dropped every
#            press while they ran (on the Vita: "the main menu ignores all
#            input"); then down/A/B through the main menu's screens
#   panel    the checks made 6 s long, and the Vita settings panel's Join a
#            game asked for 3 s in (HALO_SYSTEM_LINK_TEST=join), while they
#            still run: the System Link screen it opens stays when they end
#            (the main menu that waited for them is not put over it), and
#            A A A then reaches the list of games - before, the main menu
#            replaced it and the presses went into Campaign
#            (run_netns_online_test.sh coopmenu failed so, 2 runs in 4)
#   attract  the main menu left alone past the attract mode's countdown (75 s),
#            with no attract movie: no movie is tried (the menu's music is not
#            stopped and started over, the intro's is the only failed open),
#            and the menu still takes a press after it
#   attractretry the three attract movies there (empty bink files), each open
#            failing for now (HALO_TEST_MOVIE_FAILS_FOR_NOW=1: as the Vita's
#            video player that did not start for want of memory, Oct 8,
#            beta.2: "sceAvPlayerInit failed" for each movie, 75 s apart):
#            the main menu left alone 165 s tries one movie, once, says the
#            next is in 300 s, and plays its music on (stopped once, started
#            twice); the menu still takes a press after it
#
# Each must log no assertion or exception, and exit by itself.
#
#   run_no_movies_test.sh [CASE...]   (default: fresh panel attract)
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_OUT    where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them; the harness copies are removed)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_no_movies_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-fresh panel attract attractretry}
status=0
fail() { echo "FAIL ($1): $2"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -e "$data/maps/ui.map" ]; then
	echo "no maps in $data/maps (HALO_TEST_DATA)"
	exit 2
fi
mkdir -p "$out"

run() { # NAME SECONDS PAD [VAR=value...]
	local name=$1 seconds=$2 pad=$3
	shift 3
	rm -rf "$out/$name"
	mkdir -p "$out/$name/data" "$out/$name/save" "$out/$name/bin"
	ln -sfn "$(cd "$data" && pwd)/maps" "$out/$name/data/maps"
	# (bink_files: empty movie files the game finds)
	for file in ${bink_files:-}; do mkdir -p "$out/$name/data/bink"; : > "$out/$name/data/bink/$file"; done
	cp "$binary" "$out/$name/bin/halo"
	(cd "$out/$name" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" \
		HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_UI_LOG=1 "HALO_TEST_PAD=$pad" "$@" timeout -k 5 $((seconds + 90)) "$out/$name/bin/halo" > "$out/$name/run.log" 2>&1)
	echo $? > "$out/$name/exit"
	rm -rf "$out/$name/bin"
}

# whether the first A pressed after the main menu came up opened a screen
# (before the next press): the menu took it
first_press_taken() { # LOG
	awk '
		/ui: screen ui.shell.main_menu.main_menu$/ && !menu { menu = 1; next }
		menu && !pressed && /test pad: a$/ { pressed = 1; next }
		pressed && /ui: screen / { taken = 1; exit }
		pressed && /test pad: / { exit }
		END { exit taken ? 0 : 1 }' "$1"
}

check() { # NAME
	local name=$1 log=$out/$1/run.log debug=$out/$1/data/debug.txt
	echo "--- $name"
	grep -aE "ui: screen|test pad: a$" "$log" | head -12
	grep -aE "main menu|movie|bink" "$debug" 2>/dev/null
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name/exit")"
	grep -aqiE "assert|exception|halt" "$log" && fail "$name" "an assertion, exception or halt"
	[ -e "$out/$name/data/bink" ] && [ -z "${bink_files:-}" ] && fail "$name" "a bink folder in the data folder (the test wants none)"
	grep -aq "ui: screen ui.shell.main_menu.main_menu" "$log" || fail "$name" "the main menu did not come up"
	first_press_taken "$log" || fail "$name" "the first press after the main menu came up did not reach a screen"
}

for case in $cases; do
	case $case in
	fresh)
		# (the steps start a second after the main menu's scene is up, while
		# the checks still run: those presses are dropped, as the intro drops
		# them; then each A opens a screen and B comes back)
		pad="down:150:1000 a:150:2500 b:150:1500"
		pad="$pad $pad $pad $pad $pad $pad $pad"
		run fresh 45 "$pad" HALO_TEST_FILESYSTEM_CHECK_MS=6000
		check fresh
		grep -aq "the main menu waits for the filesystem checks" "$out/fresh/data/debug.txt" ||
			fail fresh "the main menu did not wait for the filesystem checks"
		grep -aq "the filesystem checks are done; the main menu comes up" "$out/fresh/data/debug.txt" ||
			fail fresh "the main menu did not come up after the filesystem checks"
		[ "$(grep -ac 'ui: screen ui.shell.main_menu.multiplayer_type_select' "$out/fresh/run.log")" -ge 2 ] ||
			fail fresh "Multiplayer did not open twice"
		;;
	panel)
		run panel 35 "wait:150:9000 a a a wait:150:4000" HALO_SYSTEM_LINK_TEST=join HALO_TEST_FILESYSTEM_CHECK_MS=6000
		log=$out/panel/run.log debug=$out/panel/data/debug.txt
		echo "--- panel"
		grep -aE "ui: screen|system link: the|test pad: a$" "$log" | head -12
		grep -aE "checks|main menu" "$debug" 2>/dev/null
		[ "$(cat "$out/panel/exit")" = 0 ] || fail panel "exit $(cat "$out/panel/exit")"
		grep -aqiE "assert|exception|halt" "$log" && fail panel "an assertion, exception or halt"
		grep -aq "the settings panel's join a game: the System Link screen opened" "$log" ||
			fail panel "the System Link screen did not open"
		grep -aq "the filesystem checks are done; the screen opened meanwhile stays" "$debug" ||
			fail panel "the System Link screen was not opened while the checks ran, or did not stay"
		awk '/System Link screen opened/ { opened = 1; next } opened && /ui: screen ui.shell.main_menu.main_menu$/ { bad = 1 }
			END { exit bad ? 0 : 1 }' "$log" && fail panel "the main menu was put over the System Link screen"
		grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.connected.server_list.server_list_screen" "$log" ||
			fail panel "A A A did not reach the list of games"
		grep -aq "new_campaign" "$log" && fail panel "the presses went into Campaign"
		;;
	attract)
		# (the countdown runs from the menu's last press: 75 s untouched)
		run attract 100 "wait:150:82000 down:150:1000 a:150:3000 b:150:2000"
		check attract
		debug=$out/attract/data/debug.txt
		grep -aqE "unable to locate any movie for movie #[0-2]" "$debug" ||
			fail attract "the countdown did not run out (no attract movie looked for)"
		[ "$(grep -ac 'failed to open bink file' "$debug")" = 1 ] ||
			fail attract "a movie other than the intro was tried: $(grep -ac 'failed to open bink file' "$debug") failed opens"
		grep -aq "stopping main menu music" "$debug" && fail attract "the menu's music was stopped for an attract movie"
		[ "$(grep -ac 'starting main menu music' "$debug")" = 1 ] ||
			fail attract "the menu's music was started $(grep -ac 'starting main menu music' "$debug") times"
		;;
	attractretry)
		bink_files="attract1.bik attract2.bik attract3.bik" run attractretry 175 \
			"wait:150:165000 down:150:1000 a:150:3000 b:150:2000" HALO_TEST_MOVIE_FAILS_FOR_NOW=1
		bink_files=x check attractretry
		log=$out/attractretry/run.log debug=$out/attractretry/data/debug.txt
		grep -aE "attract mode|skipping" "$log"
		tries=$(grep -ac 'skipping "d:.bink.attract[1-3].bik" (failed for now' "$log")
		[ "$tries" = 1 ] || fail attractretry "$tries attract movies tried in 165 s (1 wanted: the next 300 s after a failure)"
		[ "$(grep -ac 'attract mode: the movie failed for now; the next in 300 s' "$log")" = 1 ] ||
			fail attractretry "the failure's wait was not said once"
		[ "$(grep -ac 'stopping main menu music' "$debug")" = 1 ] ||
			fail attractretry "the menu's music was stopped $(grep -ac 'stopping main menu music' "$debug") times (once wanted)"
		[ "$(grep -ac 'starting main menu music' "$debug")" = 2 ] ||
			fail attractretry "the menu's music was started $(grep -ac 'starting main menu music' "$debug") times (twice wanted)"
		;;
	*)
		echo "usage: $0 [fresh|panel|attract|attractretry]..." >&2; exit 2 ;;
	esac
done
echo "logs in $out"
[ $status = 0 ] && echo PASS
test_out_done $status
exit $status
