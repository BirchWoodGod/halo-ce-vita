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
#   h3probe  Menu style Halo 3 (HALO_MENU_STYLE=halo3): the Halo 3 set's screen
#            stands for the Xbox's main menu (menus/halo3)
#   h3classic  Menu style Halo CE: the Xbox's main menu, no Halo 3 screen
#   h3xbox   Halo 3 without the Halo PC files: the Xbox's main menu (the
#            Halo PC files are required, as for OpenCE's screens: the owner,
#            Oct 18 2026)
#   h3broken a Halo 3 PNG missing: the set left out, the Xbox's main menu
#   h3style  Halo CE at first, Halo 3 chosen while Multiplayer is open: back on
#            the main menu, the Halo 3 screen
#   h3online the main menu's live servers: five made-up games
#            (HALO_TEST_BROWSER_GAMES), the three with the most players shown
#   pause   Campaign from the main menu (a new profile, Normal), the level's
#           pause menu, Quit to Main Menu: a game map's widget runs its
#           function and is back at the main menu, and asked about deleting
#           a profile (HALO_TEST_WIDGET_FUNCTION=76, never run) the main
#           menu's widget may, a game map's may not
#           (ui_widget_event_handler_functions.c ui_widget_function_denied)
#
# Each must log no "is not a tag index" (a tag of ours read as the empty
# one), no assertion or exception, and exit by itself.
#
#   run_menus_test.sh [CASE...]      (default: fresh level xbox pause)
#   HALO_TEST_VITA        the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA        a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_DATA_MENUS  the same with Halo PC's bitmaps.map and loc.map in
#                         its maps folder (never in the repository; skipped
#                         when missing)
#   HALO_MENUS_FOLDER     the menus' XML (default this tree's
#                         port/vita/app0/menus: the game looks beside its
#                         binary, <tree>/build/linux, and a copy of the
#                         harness elsewhere found none and kept the Xbox's
#                         Multiplayer screen)
#   HALO_TEST_OUT         where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
menus_data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_menus_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-fresh level xbox pause h3probe h3classic h3xbox h3broken h3style h3online}
status=0
fail() { echo "FAIL ($1): $2"; status=1; }

if [ ! -r "$menus_data/maps/bitmaps.map" ] || [ ! -r "$menus_data/maps/loc.map" ]; then
	echo "SKIP: no Halo PC bitmaps.map and loc.map in $menus_data/maps (HALO_TEST_DATA_MENUS)"
	exit 0
fi
export HALO_MENUS_FOLDER=${HALO_MENUS_FOLDER:-$root/port/vita/app0/menus/}
if [ ! -r "$HALO_MENUS_FOLDER/menus.txt" ]; then
	echo "SKIP: no menus.txt in $HALO_MENUS_FOLDER (HALO_MENUS_FOLDER)"
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
	grep -aq "menus: .*menus.txt cannot be read" "$log" && fail "$name" "the menus' XML was not found (HALO_MENUS_FOLDER)"
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

# (pause: Campaign, a new profile, Normal, the level started; its cinematic
# skipped, the pause menu, down to Quit to Main Menu, A)
pause_pad=${HALO_TEST_PAUSE_PAD:-"a:150:3000 start:150:3000 wait:150:6000 a:150:45000 start:150:3000 down:150:800 down:150:800 down:150:800 a:150:3000 wait:150:8000"}
check_pause() {
	local log=$out/pause/run.log
	echo "--- pause"
	grep -aE "ui: screen|widgets: |test pad: " "$log" | head -40
	[ "$(cat "$out/pause/exit")" = 0 ] || fail pause "exit $(cat "$out/pause/exit")"
	grep -aqiE "assert|exception|halt" "$log" && fail pause "an assertion, exception or halt"
	grep -aq "widgets: function 76 from the main menu's widget allowed" "$log" ||
		fail pause "the main menu's widgets may not delete a profile"
	grep -aq "widgets: function 76 from a game map's widget refused" "$log" ||
		fail pause "a game map's widget may delete a profile"
	[ "$(grep -ac 'ui: screen ui.shell.main_menu.main_menu' "$log")" -ge 2 ] ||
		fail pause "the pause menu's Quit to Main Menu did not go back to the main menu"
}

# the Halo 3 set (menus/halo3): the screen the main menu became, or the Xbox's
check_h3() { # NAME halo3|classic
	local name=$1 log=$out/$1/run.log debug=$out/$1/data/debug.txt
	echo "--- $name"
	grep -aE "menus:|ui: screen" "$log" | head -16
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name/exit")"
	grep -aqi "is not a tag index" "$log" "$debug" 2>/dev/null && fail "$name" "a tag was read as the empty one"
	grep -aqiE "assert|exception|halt" "$log" && fail "$name" "an assertion, exception or halt"
	if [ "$2" = halo3 ]; then
		grep -aq "menus: Halo 3 screen .*h3/.*/screen for ui.shell.main_menu.main_menu" "$log" ||
			fail "$name" "the Halo 3 screen did not stand for the main menu"
		grep -aq "ui: screen pc.h3.*screen" "$log" || fail "$name" "the Halo 3 screen did not open"
	else
		grep -aq "ui: screen pc.h3" "$log" && fail "$name" "a Halo 3 screen opened with Menu style Halo CE"
		grep -aq "ui: screen ui.shell.main_menu.main_menu" "$log" || fail "$name" "the Xbox's main menu did not open"
	fi
}
h3pad="wait:150:3000"

for case in $cases; do
	case $case in
	fresh)
		run fresh "$menus_data" 40 "$pad"
		check fresh pc ;;
	level)
		init_line='map_name levels\a10\a10' run level "$menus_data" 90 "$pad" "HALO_TEST_COMMANDS=L150:@quit"
		grep -aq "ui: screen ui.shell.main_menu.main_menu" "$out/level/run.log" || fail level "the level did not go back to the main menu"
		check level pc ;;
	pause)
		run pause "$menus_data" "${HALO_TEST_PAUSE_SECONDS:-120}" "$pause_pad" "HALO_TEST_COMMANDS=L150:@skip" \
			HALO_TEST_WIDGET_FUNCTION=76
		check_pause ;;
	xbox)
		run xbox "$data" 40 "$pad"
		check xbox xbox ;;
	h3probe)
		run h3probe "$menus_data" 20 "$h3pad" HALO_MENU_STYLE=halo3
		check_h3 h3probe halo3 ;;
	h3classic)
		run h3classic "$menus_data" 20 "$h3pad" HALO_MENU_STYLE=classic
		check_h3 h3classic classic ;;
	h3xbox)
		run h3xbox "$data" 20 "$h3pad" HALO_MENU_STYLE=halo3
		check_h3 h3xbox classic
		grep -aq "menus: OpenCE" "$out/h3xbox/run.log" && fail h3xbox "OpenCE's screens were added without the Halo PC files" ;;
	h3broken)
		rm -rf "$out/h3menus"
		cp -r "$HALO_MENUS_FOLDER" "$out/h3menus"
		sed -i "s/column\.png/no_such_picture.png/" "$out/h3menus/halo3/main.xml"
		HALO_MENUS_FOLDER="$out/h3menus/" run h3broken "$menus_data" 20 "$h3pad" HALO_MENU_STYLE=halo3
		check_h3 h3broken classic
		grep -aq "menus: Halo 3 set left out" "$out/h3broken/run.log" || fail h3broken "no line said the Halo 3 set was left out" ;;
	h3style)
		run h3style "$menus_data" 30 "wait:150:2000 down:150:800 a wait:150:5000 b wait:150:4000" \
			HALO_MENU_STYLE=classic "HALO_TEST_COMMANDS=150:@set HALO_MENU_STYLE halo3"
		grep -aq "ui: screen ui.shell.main_menu.main_menu" "$out/h3style/run.log" || fail h3style "the Xbox's main menu was not first"
		check_h3 h3style halo3 ;;
	h3online)
		run h3online "$menus_data" 15 "$h3pad" HALO_MENU_STYLE=halo3 HALO_TEST_BROWSER_GAMES=5
		check_h3 h3online halo3
		grep -aq "menus: h3 servers: 3 shown of 5: Test game 02 (14/16), Test game 04 (12/16), Test game 01 (7/16)" \
			"$out/h3online/run.log" || fail h3online "the three fullest games were not shown" ;;
	*)
		echo "usage: $0 [fresh|level|xbox|pause|h3probe|h3classic|h3xbox|h3broken|h3style|h3online]..." >&2; exit 2 ;;
	esac
done
echo "logs in $out"
[ $status = 0 ] && echo PASS
test_out_done $status
exit $status
