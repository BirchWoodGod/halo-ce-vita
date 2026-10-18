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
#   h3touch  taps (HALO_TEST_TAPS, as the Vita's touch screen's): FORGE on the
#            main menu opens the Forge lobby, the lobby's Leave closes it
#   h3resume a profile's saved game (a10 on Normal, saved and quit): the lobby
#            says at Last Checkpoint, and on Heroic that it replaces it
#   h3pause  a level's map gets the in-game set (ingame.txt): in The Silent
#            Cartographer, Start opens the Halo 3 pause screen; SAVE AND QUIT
#            saves and goes back to the Halo 3 main menu
#   h3campaign  a profile with every level (HALO_TEST_PROFILE, never in the
#            repository; skipped without): CAMPAIGN, the profile, the Halo 3
#            campaign lobby; Mission: The Silent Cartographer, Difficulty:
#            Heroic, Start Game: b30 loads on Heroic
#   h3mp     the Halo 3 multiplayer lobby (menus/halo3/multiplayer.xml) stands
#            for the Xbox's Multiplayer screen; internet play off: NETWORK is
#            SYSTEM LINK, choosing ONLINE opens the screen saying how to turn it
#            on; START GAME hosts a system link game (the profile, then the
#            Xbox's map list)
#   h3browser  internet play on (its brokers unreachable: TEST-NET addresses)
#            with 16 made-up games of both platforms (HALO_TEST_BROWSER_GAMES,
#            HALO_TEST_BROWSER_PC): BROWSE GAMES opens the Halo 3 browser, a
#            locked game the Halo 3 password screen, whose JOIN GAME says the
#            game is no longer listed
#   h3create  START GAME on ONLINE: Create Game; its GAME TYPE picker, the
#            name typed (HALO_TEST_TEXT_INPUT), START GAME as Server Setup's
#   h3mptouch  taps: MULTIPLAYER, the lobby's MAP, BLOOD GULCH in its picker,
#            the lobby's Back
#   h3coop   co-op from the Halo 3 campaign lobby (HALO_TEST_PROFILE; skipped
#            without): NETWORK, ONLINE CO-OP with internet play off (the
#            screen saying how to turn it on), SYSTEM LINK CO-OP, START GAME:
#            the level hosted as co-op (as the Xbox's difficulty list's Y
#            hosted it) and the game's lobby, the waiting screen, open
#   h3scenes the lobbies' cameras (scene= in menus/halo3; menus.scenes,
#            HALO_MENU_SCENES): CAMPAIGN (HALO_TEST_PROFILE's profile; a new
#            one's campaign starts at once without), back, MULTIPLAYER, back,
#            FORGE, back: each lobby's camera written into its point of
#            ui.map and glided to, ring_loop asleep meanwhile, the main menu
#            waking it; then the same with HALO_MENU_SCENES=0: none
#   h3characters  the lobbies' characters (scene_object= in menus/halo3; the
#            menu_spartan scene pack, built from the maps folder's Blood Gulch
#            the first time: port/linux/game/scene_packs.c,
#            menu_scene_objects.c): the campaign lobby's two Spartans, the
#            multiplayer and Forge lobbies' one, each made as its lobby opens
#            and deleted as the main menu comes back (HALO_TEST_PROFILE's
#            profile); the Customization screen's preview (the console's "pack
#            preview": shown in a colour, recoloured, hidden); then with
#            HALO_SCENE_PACKS=0, and with no multiplayer map in the maps
#            folder: no character, nothing else changed
#   h3coopguide  the settings panel's Host co-op campaign
#            (HALO_SYSTEM_LINK_TEST=campaign): the Campaign screen opens, the
#            Halo 3 campaign lobby behind it set to SYSTEM LINK CO-OP
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
#   HALO_TEST_SHOTS       a folder for each case's frames (HALO_SCREENSHOT_DIR, every
#                         60 frames, in <folder>/<case>); none by default
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
cases=${*:-fresh level xbox pause h3probe h3classic h3xbox h3broken h3style h3online h3touch h3campaign h3resume h3pause h3mp h3browser h3create h3mptouch h3coop h3coopguide h3scenes h3characters}
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
	# (Menu style Halo CE unless the case says otherwise: the classic cases' pads walk
	# Halo CE's menus; Halo 3 has been the default since 1.1.1)
	local name=$1 folder=$2 seconds=$3 pad=$4
	shift 4
	rm -rf "$out/$name"
	mkdir -p "$out/$name/data" "$out/$name/save"
	# (a player profile of the tests', as the Vita keeps it: HALO_TEST_PROFILE's blam.sav)
	if [ -n "${profile_from:-}" ]; then
		mkdir -p "$out/$name/save/u/UDATA/869079979327"
		cp "$profile_from" "$out/$name/save/u/UDATA/869079979327/blam.sav"
		# (and its save folder's SaveMeta.xbx, beside it: without it the game finds no profile)
		[ -r "$(dirname "$profile_from")/SaveMeta.xbx" ] &&
			cp "$(dirname "$profile_from")/SaveMeta.xbx" "$out/$name/save/u/UDATA/869079979327/"
	fi
	ln -sfn "$(cd "$folder" && pwd)/maps" "$out/$name/data/maps"
	[ -n "${init_line:-}" ] && printf '%s\n' "$init_line" > "$out/$name/data/init.txt"
	local shots=()
	if [ -n "${HALO_TEST_SHOTS:-}" ]; then
		rm -rf "$HALO_TEST_SHOTS/$name"
		mkdir -p "$HALO_TEST_SHOTS/$name"
		shots=("HALO_SCREENSHOT_DIR=$HALO_TEST_SHOTS/$name" HALO_SCREENSHOT_EVERY=60)
	fi
	(cd "$out/$name" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" \
		HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_UI_LOG=1 HALO_MENU_STYLE=classic "HALO_TEST_PAD=$pad" "${shots[@]}" "$@" timeout -k 5 $((seconds + 90)) "$binary" > "$out/$name/run.log" 2>&1)
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
# (internet play on, nothing reached: its brokers, STUN and relays TEST-NET addresses)
h3net="HALO_NET_ONLINE=true HALO_NET_BROKERS=198.51.100.1:1883 HALO_NET_STUN=198.51.100.1:3478"
h3net="$h3net HALO_NET_RELAYS=198.51.100.1:47320 HALO_NET_ALLOW_UPNP=false"
# (the main menu: down to MULTIPLAYER, A: the lobby, its NETWORK row focused)
h3mppad="wait:150:3000 down:150:700 a wait:150:3000"

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
	h3touch)
		# (FORGE's row on the main menu, then the Forge lobby's Leave: icon and label)
		run h3touch "$menus_data" 20 "wait:150:12000" HALO_MENU_STYLE=halo3 "HALO_TEST_TAPS=4000:60,318 8000:140,455"
		check_h3 h3touch halo3
		grep -aq "ui: screen pc.h3.forge.lobby" "$out/h3touch/run.log" || fail h3touch "a tap on FORGE did not open the Forge lobby"
		[ "$(grep -ac 'ui: screen pc.h3.main.screen' "$out/h3touch/run.log")" -ge 2 ] ||
			fail h3touch "a tap on the lobby's Leave did not go back to the main menu" ;;
	h3resume)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3resume"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (the lobby: Start Game on The Pillar of Autumn, Normal; in the level, saved and quit to the
		# main menu; CAMPAIGN again, the profile, the lobby: its caption; Difficulty, down, A: Heroic)
		profile_from=$HALO_TEST_PROFILE run h3resume "$menus_data" 170 "wait:150:3000 a wait:150:3000 a wait:150:3000 down:150:600 down:150:600 a wait:150:70000 a wait:150:3000 a wait:150:4000 wait:150:2000 down:150:700 a wait:150:2500 down:150:700 a wait:150:6000 wait:150:4000" \
			HALO_MENU_STYLE=halo3 "HALO_TEST_COMMANDS=L240:@skip;L420:game_save_totally_unsafe;L600:@quit"
		check_h3 h3resume halo3
		log=$out/h3resume/run.log
		grep -aq "menus: h3 campaign caption: The Pillar of Autumn on Normal.at Last Checkpoint" "$log" ||
			fail h3resume "the lobby did not say the saved game goes on from its checkpoint"
		grep -aq "menus: h3 campaign caption: The Pillar of Autumn on Heroic.at Mission Start (replaces the saved game)" "$log" ||
			fail h3resume "another difficulty did not warn that it replaces the saved game" ;;
	h3pause)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3pause"; echo "SKIP: no HALO_TEST_PROFILE (a blam.sav with every level)"; continue
		fi
		# (the lobby: Mission, The Silent Cartographer, Start Game; in the level - a10's start
		# is the cryo tube, no pausing - Start: the Halo 3 pause screen; down to SAVE AND QUIT, A)
		profile_from=$HALO_TEST_PROFILE run h3pause "$menus_data" 170 "wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:2500 down:150:600 down:150:600 a wait:150:60000 start:150:4000 wait:150:4000 down:150:1500 wait:150:4000 down:150:800 down:150:800 a:150:4000 wait:150:20000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_COMMANDS=L150:@skip
		log=$out/h3pause/run.log
		echo "--- h3pause"
		grep -aq "menus: the in-game screens: .* added to b30.map" "$log" || fail h3pause "the in-game screens were not built into b30.map"
		grep -aq "menus: Halo 3 screen h3/pause/screen for ui.shell.solo_game.pause_game.pause_game" "$log" ||
			fail h3pause "the Halo 3 pause screen did not stand for the level's"
		grep -aq "ui: screen ui.shell.error.warning_saving_file_screen_ingame" "$log" ||
			fail h3pause "SAVE AND QUIT did not save"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.main.screen' ||
			fail h3pause "SAVE AND QUIT did not go back to the Halo 3 main menu"
		grep -aqiE "assert|exception|halt" "$log" && fail h3pause "an assertion, exception or halt" ;;
	h3campaign)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3campaign"; echo "SKIP: no HALO_TEST_PROFILE (a blam.sav with every level)"; continue
		fi
		# (the main menu: down to CAMPAIGN, A; the profile, A; the lobby: Mission, down three, A;
		# Difficulty - the picker opens on Normal -, down one, A; Start Game)
		profile_from=$HALO_TEST_PROFILE run h3campaign "$menus_data" 75 "wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:2500 down:150:700 a wait:150:2500 down:150:600 a wait:150:2500 down:150:700 a wait:150:15000" \
			HALO_MENU_STYLE=halo3
		check_h3 h3campaign halo3
		log=$out/h3campaign/run.log
		grep -aq "menus: Halo 3 screen h3/campaign/screen for ui.shell.main_menu.solo_level_select.solo_level_select_screen" "$log" ||
			fail h3campaign "the Halo 3 campaign lobby did not stand for the level select"
		grep -aq "menus: h3 campaign: The Silent Cartographer on Normal" "$log" || fail h3campaign "the mission was not set"
		grep -aq "menus: h3 campaign: The Silent Cartographer on Heroic" "$log" || fail h3campaign "the difficulty was not set"
		grep -aq "menus: h3 campaign: start levels.b30.b30 on Heroic" "$log" || fail h3campaign "Start Game did not start b30 on Heroic"
		grep -aq "memory: loaded levels.b30.b30" "$log" || fail h3campaign "b30 did not load" ;;
	h3mp)
		# (NETWORK: the picker on SYSTEM LINK, up to ONLINE, A: the screen saying internet play is
		# off; B, B: the lobby; down three to START GAME, A: the profile screen, A to join, the
		# profile chosen: the map list)
		run h3mp "$menus_data" 55 "$h3mppad a wait:150:3000 up:150:600 a wait:150:3000 b wait:150:3000 b wait:150:3000 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:5000" \
			HALO_MENU_STYLE=halo3
		check_h3 h3mp halo3
		log=$out/h3mp/run.log
		grep -aq "menus: Halo 3 screen h3/mp/screen for ui.shell.main_menu.multiplayer_type_select.multiplayer_type_select_screen" "$log" ||
			fail h3mp "the Halo 3 lobby did not stand for the Multiplayer screen"
		grep -aq "menus: h3 mp lobby: NETWORK: SYSTEM LINK, GAME: SLAYER, MAP: BATTLE CREEK" "$log" ||
			fail h3mp "the lobby's rows were not SYSTEM LINK, SLAYER, BATTLE CREEK"
		grep -aq "ui: screen pc.h3.network.screen" "$log" || fail h3mp "NETWORK did not open its picker"
		grep -aq "ui: screen pc.h3.mp.offline" "$log" || fail h3mp "ONLINE did not say internet play is off"
		grep -aq "menus: h3 mp: host a system link game" "$log" || fail h3mp "START GAME did not host a system link game"
		grep -aq "menus: Multiplayer > System Link: the game's server started; the map list" "$log" ||
			fail h3mp "the server did not start (or the log named another screen)"
		grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.connected.connected_map_select_wrapper" "$log" ||
			fail h3mp "the profile did not lead to the map list"
		grep -aq "ui: screen pc.mp.screen" "$log" && fail h3mp "OpenCE's Multiplayer screen opened with Menu style Halo 3" ;;
	h3browser)
		# (down five to BROWSE GAMES, A; down to the second game - locked -, A: its password;
		# A: typed, down, A: JOIN GAME)
		run h3browser "$menus_data" 45 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:4000 down:150:600 a wait:150:4000 a wait:150:2000 down:150:600 a wait:150:4000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_TEST_BROWSER_GAMES=16 HALO_TEST_BROWSER_PC=1 HALO_TEST_TEXT_INPUT=secret
		check_h3 h3browser halo3
		log=$out/h3browser/run.log
		grep -aq "menus: h3 mp lobby: NETWORK: ONLINE" "$log" || fail h3browser "NETWORK was not ONLINE with internet play on"
		grep -aq "ui: screen pc.h3.browser.screen" "$log" || fail h3browser "BROWSE GAMES did not open the Halo 3 browser"
		grep -aq "menus: the server browser's game 2 of 16 chosen: Test game 02" "$log" ||
			fail h3browser "the browser did not list the 16 games, the fullest first"
		grep -aq "menus: Halo 3 screen h3/password/screen for pc.join.password.screen" "$log" ||
			fail h3browser "the locked game did not open the Halo 3 password screen"
		grep -aq "menus: the public game Test game 02 is no longer listed" "$log" ||
			fail h3browser "JOIN GAME did not try the game with its password"
		grep -aq "ui: screen pc.browser.screen\|ui: screen pc.join.password" "$log" &&
			fail h3browser "OpenCE's browser or password screen opened with Menu style Halo 3" ;;
	h3create)
		# (down three to START GAME, A: Create Game; GAME TYPE, A: the picker, down, A: SLAYER PRO;
		# down two to NAME, A: typed; down four to START GAME, A)
		run h3create "$menus_data" 50 "$h3mppad down:150:500 down:150:500 down:150:500 a wait:150:4000 a wait:150:3000 down:150:600 a wait:150:4000 down:150:500 down:150:500 a wait:150:2000 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:5000" \
			HALO_MENU_STYLE=halo3 $h3net "HALO_TEST_TEXT_INPUT=Vita Night"
		check_h3 h3create halo3
		log=$out/h3create/run.log
		grep -aq "ui: screen pc.h3.setup.screen" "$log" || fail h3create "START GAME did not open Create Game"
		grep -aq "menus: h3 mp: game type Slayer Pro" "$log" || fail h3create "the GAME TYPE picker did not set the game type"
		grep -aq "menus: Server Setup: Vita Night, 16 players, public; the profile, then the map list" "$log" ||
			fail h3create "START GAME did not set up the game as Server Setup does"
		grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.connected.4way_profile_select.4way_start2join_screen" "$log" ||
			fail h3create "START GAME did not open the profile" ;;
	h3mptouch)
		# (MULTIPLAYER on the main menu; the lobby's MAP; BLOOD GULCH in the picker; the lobby's Back)
		run h3mptouch "$menus_data" 30 "wait:150:20000" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_TAPS=4000:60,298 8000:60,105 12000:100,263 16000:140,455"
		check_h3 h3mptouch halo3
		log=$out/h3mptouch/run.log
		grep -aq "ui: screen pc.h3.map.screen" "$log" || fail h3mptouch "a tap on MAP did not open its picker"
		grep -aq "menus: h3 mp: map Blood Gulch" "$log" || fail h3mptouch "a tap on BLOOD GULCH did not choose it"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.main.screen' ||
			fail h3mptouch "a tap on the lobby's Back did not go back to the main menu" ;;
	h3coop)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3coop"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (CAMPAIGN, the profile: the lobby, MISSION focused; up to NETWORK, A: the picker on LOCAL;
		# down, A: ONLINE CO-OP, internet play off: the screen saying so; B: the picker; down, A:
		# SYSTEM LINK CO-OP; down three to START GAME, A: hosted, the game's lobby)
		profile_from=$HALO_TEST_PROFILE run h3coop "$menus_data" 50 "wait:150:3000 a wait:150:3000 a wait:150:3000 up:150:600 a wait:150:2500 down:150:600 a wait:150:2500 b wait:150:2500 down:150:600 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:8000" \
			HALO_MENU_STYLE=halo3
		check_h3 h3coop halo3
		log=$out/h3coop/run.log
		grep -aq "ui: screen pc.h3.campnet.screen" "$log" || fail h3coop "NETWORK did not open its picker"
		grep -aq "menus: h3 campaign: ONLINE CO-OP chosen, internet play is off" "$log" &&
			grep -aq "ui: screen pc.h3.mp.offline" "$log" ||
			fail h3coop "ONLINE CO-OP did not say internet play is off"
		grep -aq "menus: h3 campaign: network SYSTEM LINK CO-OP" "$log" || fail h3coop "SYSTEM LINK CO-OP was not chosen"
		grep -aq "menus: h3 campaign caption: .* on Normal Co-op at Mission Start" "$log" ||
			fail h3coop "the caption did not say co-op"
		grep -aq "menus: h3 campaign: host co-op levels.*on Normal (system link)" "$log" ||
			fail h3coop "START GAME did not host co-op"
		grep -aq "co-op: hosting levels.* on difficulty 1 from the campaign's menus" "$log" ||
			fail h3coop "the level was not hosted as co-op"
		grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.connected.pregame.connected_pregame_screen" "$log" ||
			fail h3coop "the game's lobby (the waiting screen) did not open"
		grep -aq "menus: h3 campaign: co-op was not hosted" "$log" && fail h3coop "co-op was not hosted"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'connected_pregame_screen' ||
			fail h3coop "the game's lobby did not stay open (the co-op game ended)" ;;
	h3scenes)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3scenes"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (CAMPAIGN, the profile: the campaign lobby; B, B: the main menu; down, A: MULTIPLAYER;
		# B; down, A: FORGE; B)
		scenes_pad="wait:150:3000 a wait:150:3000 a wait:150:4000 b wait:150:2500 b wait:150:3000 down:150:700 a wait:150:4000 b wait:150:3000 down:150:700 a wait:150:4000 b wait:150:3000"
		profile_from=$HALO_TEST_PROFILE run h3scenes "$menus_data" 45 "$scenes_pad" HALO_MENU_STYLE=halo3
		profile_from=$HALO_TEST_PROFILE run h3scenes_off "$menus_data" 45 "$scenes_pad" HALO_MENU_STYLE=halo3 HALO_MENU_SCENES=0
		check_h3 h3scenes halo3
		log=$out/h3scenes/run.log
		grep -aE "menus: h3 scene|host command \(menu scene\)" "$log"
		for scene in "h3/campaign/screen: camera point load_campaign at -15.00 5.00 0.00, yaw -10.0 pitch 30.0, over 0 ticks" \
			"h3/mp/screen: camera point multiplayer at -40.00 -1.80 15.50, yaw 0.0 pitch 0.0, over 0 ticks" \
			"h3/forge/lobby: camera point new_campaign at -10.00 -20.00 5.00, yaw 40.0 pitch 20.0, over 0 ticks"; do
			grep -aqF "menus: h3 scene: $scene" "$log" || fail h3scenes "no scene: $scene"
		done
		for command in "(sleep -1 ring_loop)" "(camera_set load_campaign 0)" "(camera_set multiplayer 0)" \
			"(camera_set new_campaign 0)" "(camera_set settings 0)" "(wake ring_loop)"; do
			grep -aqF "host command (menu scene): $command" "$log" || fail h3scenes "no host command $command"
		done
		[ "$(grep -ac 'menus: h3 scene: h3/main/screen: the ring' "$log")" -ge 4 ] ||
			fail h3scenes "the main menu did not have the ring's flight each time"
		[ "$(grep -acF 'host command (menu scene): (wake ring_loop)' "$log")" -ge 3 ] ||
			fail h3scenes "the main menu did not wake ring_loop after each lobby"
		grep -aqiE "hs_compile|script error|unknown function|not a valid" "$log" && fail h3scenes "a script command was refused"
		[ "$(grep -ac 'ui: screen pc.h3.campaign.screen\|ui: screen pc.h3.mp.screen\|ui: screen pc.h3.forge.lobby' "$log")" -ge 3 ] ||
			fail h3scenes "the three lobbies did not open"
		check_h3 h3scenes_off halo3
		grep -aq "menus: h3 scene\|host command (menu scene)" "$out/h3scenes_off/run.log" &&
			fail h3scenes "HALO_MENU_SCENES=0 still moved the camera"
		[ "$(grep -ac 'ui: screen pc.h3.campaign.screen\|ui: screen pc.h3.mp.screen\|ui: screen pc.h3.forge.lobby' "$out/h3scenes_off/run.log")" -ge 3 ] ||
			fail h3scenes "HALO_MENU_SCENES=0: the three lobbies did not open" ;;
	h3characters)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3characters"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (as h3scenes: CAMPAIGN, the profile: the campaign lobby; B, B; down, A: MULTIPLAYER; B;
		# down, A: FORGE; B)
		characters_pad="wait:150:3000 down:150:700 a wait:150:3000 a wait:150:5000 b wait:150:2500 b wait:150:3000 down:150:700 a wait:150:5000 b wait:150:3000 down:150:700 a wait:150:5000 b wait:150:3000"
		profile_from=$HALO_TEST_PROFILE run h3characters "$menus_data" 50 "$characters_pad" HALO_MENU_STYLE=halo3
		# (the multiplayer lobby, then the preview: in colour 5, recoloured 9, hidden)
		run h3characters_preview "$menus_data" 26 "wait:150:3000 down:150:700 down:150:700 a wait:150:15000" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_COMMANDS=330:pack preview 5;420:pack preview 9;510:pack preview off"
		profile_from=$HALO_TEST_PROFILE run h3characters_off "$menus_data" 50 "$characters_pad" HALO_MENU_STYLE=halo3 HALO_SCENE_PACKS=0
		# (a maps folder of ui.map and the Halo PC files alone: no multiplayer map to build from)
		rm -rf "$out/nomap_data"
		mkdir -p "$out/nomap_data/maps"
		for map in ui bitmaps loc; do ln -s "$(readlink -f "$menus_data/maps/$map.map")" "$out/nomap_data/maps/$map.map"; done
		run h3characters_nomap "$out/nomap_data" 22 "wait:150:3000 down:150:700 down:150:700 a wait:150:8000" HALO_MENU_STYLE=halo3
		check_h3 h3characters halo3
		log=$out/h3characters/run.log
		grep -aE "scene packs:|h3 scene object" "$log"
		grep -aq "scene packs: menu_spartan: built from bloodgulch" "$log" || fail h3characters "the pack was not built"
		grep -aq "scene packs: menu_spartan: loaded from bloodgulch" "$log" || fail h3characters "the pack was not loaded"
		[ "$(grep -ac 'h3 scene object 0: characters.cyborg_mp.cyborg_mp (menu_spartan) at .* made' "$log")" -ge 3 ] ||
			fail h3characters "the three lobbies did not each have their Spartan"
		grep -aq 'h3 scene object 1: characters.cyborg_mp.cyborg_mp (menu_spartan) at .* made' "$log" ||
			fail h3characters "the campaign lobby did not have its second Spartan"
		[ "$(grep -ac 'h3 scene object 0: deleted' "$log")" -ge 3 ] ||
			fail h3characters "the main menu did not let the lobbies' Spartans go"
		[ "$(grep -ac 'ui: screen pc.h3.campaign.screen\|ui: screen pc.h3.mp.screen\|ui: screen pc.h3.forge.lobby' "$log")" -ge 3 ] ||
			fail h3characters "the three lobbies did not open"
		check_h3 h3characters_preview halo3
		log=$out/h3characters_preview/run.log
		grep -aE "h3 scene object|preview" "$log"
		grep -aq "h3 scene object 4: characters.cyborg_mp.cyborg_mp (menu_spartan) at .* colour 5: made" "$log" ||
			fail h3characters "the preview did not show a Spartan in colour 5"
		grep -aq "h3 scene object: recoloured 9" "$log" || fail h3characters "the preview was not recoloured"
		grep -aq "h3 scene object 4: deleted" "$log" || fail h3characters "the preview was not hidden"
		check_h3 h3characters_off halo3
		grep -aqE "scene packs: |h3 scene object .* made" "$out/h3characters_off/run.log" &&
			fail h3characters "HALO_SCENE_PACKS=0: a pack or a character all the same"
		[ "$(grep -ac 'ui: screen pc.h3.campaign.screen\|ui: screen pc.h3.mp.screen\|ui: screen pc.h3.forge.lobby' "$out/h3characters_off/run.log")" -ge 3 ] ||
			fail h3characters "HALO_SCENE_PACKS=0: the three lobbies did not open"
		check_h3 h3characters_nomap halo3
		log=$out/h3characters_nomap/run.log
		grep -aE "scene packs:|h3 scene object" "$log"
		grep -aq "scene packs: menu_spartan: not built: none of its maps" "$log" ||
			fail h3characters "no multiplayer map: the build did not say why it failed"
		grep -aq "h3 scene object 0: the pack menu_spartan is failed: no character" "$log" ||
			fail h3characters "no multiplayer map: the lobby did not go without its character"
		grep -aq "ui: screen pc.h3.mp.screen" "$log" || fail h3characters "no multiplayer map: the lobby did not open" ;;
	h3coopguide)
		# (3 s into the main menu the request opens the Campaign screen's profiles; the lobby
		# behind them is set to co-op. The Xbox's profiles screen opens on a new profile, whose
		# campaign starts at once, as ever: the lobby itself is h3coop's)
		run h3coopguide "$menus_data" 20 "wait:150:10000" HALO_MENU_STYLE=halo3 HALO_SYSTEM_LINK_TEST=campaign
		check_h3 h3coopguide halo3
		log=$out/h3coopguide/run.log
		grep -aq "system link: the settings panel's host co-op campaign: the Campaign screen opened" "$log" ||
			fail h3coopguide "the request did not open the Campaign screen"
		grep -aq "menus: h3 campaign: network SYSTEM LINK CO-OP (host co-op campaign)" "$log" ||
			fail h3coopguide "the lobby was not set to co-op" ;;
	*)
		echo "usage: $0 [fresh|level|xbox|pause|h3probe|h3classic|h3xbox|h3broken|h3style|h3online|h3touch|h3campaign|h3resume|h3pause|h3mp|h3browser|h3create|h3mptouch|h3coop|h3coopguide|h3scenes|h3characters]..." >&2; exit 2 ;;
	esac
done
echo "logs in $out"
[ $status = 0 ] && echo PASS
test_out_done $status
exit $status
