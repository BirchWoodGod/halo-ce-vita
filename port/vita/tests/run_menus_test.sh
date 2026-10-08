#!/bin/bash
# OpenCE's multiplayer screens in the game's menus (port/linux/game/menu_tags.c),
# on the Linux harness (any build of this tree; a --release one checks the
# release builds' tag accessors too, tag_groups.h), every press scripted
# (HALO_TEST_PAD), the screens named in the log (HALO_UI_LOG):
#
#   fresh   a first start (no saves): the main menu, down and back, then
#           Multiplayer opens OpenCE's screen; B back, and Multiplayer again;
#           down to Create Game Internet (Server Setup, online off: the
#           screen that says internet play is off), B, B
#   level   a campaign level first (a10, left with Save and Quit), then the
#           main menu and Multiplayer twice as above: ui.map's tags and ours
#           are added again
#   xbox    without the Halo PC files: the Xbox's Multiplayer screen, nothing
#           added
#   split   SPLIT SCREEN with four controllers (debug.test_controllers): each
#           presses START on the Xbox's Select Profile screen and A on its
#           profile, then player 1 goes through the map, the gametype and the
#           pregame screens into the game, which splits in four
#   split1  SPLIT SCREEN with one controller (a Vita): the screen that says
#           it needs a PS TV's controllers, B back; then A on it plays alone
#           (the Xbox's Select Profile screen)
#
# Each must log no "is not a tag index" (a tag of ours read as the empty
# one), no assertion or exception, and exit by itself.
#
#   run_menus_test.sh [CASE...]      (default: fresh level xbox split split1)
#   HALO_TEST_VITA        the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA        a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_DATA_MENUS  the same with Halo PC's bitmaps.map and loc.map in
#                         its maps folder (never in the repository; skipped
#                         when missing)
#   HALO_TEST_OUT         where the logs go (kept)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
menus_data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_menus_test.$$}
cases=${*:-fresh level xbox split split1}
status=0
fail() { echo "FAIL ($1): $2"; status=1; }

if [ ! -e "$menus_data/maps/bitmaps.map" ] || [ ! -e "$menus_data/maps/loc.map" ]; then
	echo "SKIP: no Halo PC bitmaps.map and loc.map in $menus_data/maps (HALO_TEST_DATA_MENUS)"
	exit 0
fi
mkdir -p "$out"

run() { # NAME DATA SECONDS PAD [VAR=value...]
	local name=$1 folder=$2 seconds=$3 pad=$4
	shift 4
	rm -rf "$out/$name"
	mkdir -p "$out/$name/data" "$out/$name/save"
	ln -sfn "$(cd "$folder" && pwd)/maps" "$out/$name/data/maps"
	[ -n "${init_line:-}" ] && printf '%s\n' "$init_line" > "$out/$name/data/init.txt"
	(cd "$out/$name" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" \
		HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_UI_LOG=1 "HALO_TEST_PAD=$pad" "$@" timeout -k 5 $((seconds + 90)) "$binary" > "$out/$name/run.log" 2>&1)
	echo $? > "$out/$name/exit"
}

# (the main menu: down and up; Multiplayer, back, Multiplayer again; Create
# Game Internet (the fourth item), back, back)
pad="wait:150:2000 down:150:800 up:150:800 down:150:2500 a wait:150:3000 b wait:150:2500 a wait:150:3000"
pad="$pad down:150:600 down:150:600 down:150:600 a wait:150:2500 b wait:150:2000 b wait:150:2000"

check() { # NAME SCREENS_WANTED
	local name=$1 log=$out/$1/run.log debug=$out/$1/data/debug.txt
	echo "--- $name"
	grep -aE "menus:|ui: screen" "$log" | head -16
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name/exit")"
	grep -aqi "is not a tag index" "$log" "$debug" 2>/dev/null && fail "$name" "a tag was read as the empty one"
	grep -aqiE "assert|exception|halt" "$log" && fail "$name" "an assertion, exception or halt"
	if [ "$2" = pc ]; then
		[ "$(grep -ac 'ui: screen pc.mp.screen' "$log")" -ge 3 ] || fail "$name" "OpenCE's Multiplayer screen did not open twice"
		grep -aq "ui: screen pc.mp.offline" "$log" || fail "$name" "Create Game Internet did not say internet play is off"
		grep -aq "menus: the Halo PC pictures and text read" "$log" || fail "$name" "the Halo PC pictures were not read"
	else
		grep -aq "menus: OpenCE" "$log" && fail "$name" "OpenCE's screens were added without the Halo PC files"
		[ "$(grep -ac 'ui: screen ui.shell.main_menu.multiplayer_type_select.multiplayer_type_select_screen' "$log")" -ge 2 ] ||
			fail "$name" "the Xbox's Multiplayer screen did not open twice"
	fi
}

# (Multiplayer, then down to SPLIT SCREEN, the seventh item)
to_split="wait:150:2000 down:150:2500 a wait:150:3000 down:150:600 down:150:600 down:150:600 down:150:600 down:150:600"
to_split="$to_split down:150:600 wait:150:1000 a wait:150:3000"

check_split() { # NAME
	local name=$1 log=$out/$1/run.log debug=$out/$1/data/debug.txt
	echo "--- $name"
	grep -aE "menus: Split|ui: screen|split screen:|system link: playing" "$log" | head -16
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name/exit")"
	grep -aqi "is not a tag index" "$log" "$debug" 2>/dev/null && fail "$name" "a tag was read as the empty one"
	grep -aqiE "assert|exception|halt" "$log" && fail "$name" "an assertion, exception or halt"
}

for case in $cases; do
	case $case in
	fresh)
		run fresh "$menus_data" 40 "$pad"
		check fresh pc ;;
	level)
		init_line='map_name levels\a10\a10' run level "$menus_data" 90 "$pad" "HALO_TEST_COMMANDS=L150:@quit"
		grep -aq "ui: screen ui.shell.main_menu.main_menu" "$out/level/run.log" || fail level "the level did not go back to the main menu"
		check level pc ;;
	xbox)
		run xbox "$data" 40 "$pad"
		check xbox xbox ;;
	split)
		run split "$menus_data" 90 "$to_split start:150:1000 2.start:150:1000 3.start:150:1000 4.start:150:2000 a:150:1000 2.a:150:1000 3.a:150:1000 4.a:150:2000 a wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:3000" \
			HALO_TEST_CONTROLLERS=4 HALO_DISPLAY_WIDTH=848
		check_split split
		grep -aq "menus: Split Screen: 4 controllers connected" "$out/split/run.log" || fail split "the four controllers were not counted"
		grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.split_screen.pregame" "$out/split/run.log" ||
			fail split "the split screen pregame screen did not open"
		grep -aq "split screen: 4 windows" "$out/split/run.log" || fail split "the game did not split in four" ;;
	split1)
		run split1 "$menus_data" 40 "$to_split b wait:150:2500 a wait:150:3000 a wait:150:3000"
		check_split split1
		[ "$(grep -ac 'ui: screen pc.mp.controllers' "$out/split1/run.log")" -ge 2 ] ||
			fail split1 "the screen that says split screen needs controllers did not open twice"
		grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.split_screen.4way_profile_select" "$out/split1/run.log" ||
			fail split1 "A did not go on to play alone" ;;
	*)
		echo "usage: $0 [fresh|level|xbox|split|split1]..." >&2; exit 2 ;;
	esac
done
echo "logs in $out"
[ $status = 0 ] && echo PASS
exit $status
