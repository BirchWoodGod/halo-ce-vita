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
#   h3characters  (PARKED: not in the default cases. The lobbies' Spartans are
#            out of the lobby screens for now, the owner's call: their
#            scene_object= attributes are removed, the code and the packs
#            kept - menus/halo3/README.txt says how to bring them back; run
#            this case again then. The preview's Spartan is h3customize's.)
#            the lobbies' characters (scene_object= in menus/halo3; the
#            menu_spartan scene pack, built from the maps folder's Blood Gulch
#            the first time: port/linux/game/scene_packs.c,
#            menu_scene_objects.c): the campaign lobby's two Spartans, the
#            multiplayer and Forge lobbies' one, each made as its lobby opens
#            and deleted as the main menu comes back (HALO_TEST_PROFILE's
#            profile); the Customization screen's preview (the console's "pack
#            preview": shown in a colour, recoloured, hidden); then with
#            HALO_SCENE_PACKS=0, and with no multiplayer map in the maps
#            folder: no character, nothing else changed
#   h3profiles  the Halo 3 SELECT PROFILE (menus/halo3/profiles.xml) stands for
#            the Xbox's campaign profile screen: a first start (no profile)
#            shows CREATE NEW PROFILE alone, a name of spaces refused, then
#            Rookie made (blam.sav, its name) and the lobby opened; with
#            HALO_TEST_PROFILE's profile too: Triangle with that profile's name
#            refused (taken), Rookie made, played, then Square and its
#            question's DELETE PROFILE delete it, the other profile's file left
#            byte for byte; with Menu style Halo CE, the Xbox's screen
#   h3proftouch  taps: CAMPAIGN, SELECT PROFILE's New Profile (its icon and
#            label): Rookie made, the lobby; the lobby's Back: SELECT PROFILE
#   h3customize  CUSTOMIZATION (menus/halo3/customize.xml), with
#            HALO_TEST_PROFILE's profile: the name typed, the colour, the stick
#            layout, the look sensitivity and invert look changed, Circle saves
#            them; blam.sav and SaveMeta.xbx read back (the other fields kept),
#            then the harness started again on that save: the editor shows
#            them, and leaving it saves nothing; the preview (the
#            menu_spartan pack's Spartan in the editor's window, in front of
#            its own camera): shown in the profile's colour, made again in
#            RED as COLOR changes, never the swatch meanwhile, let go with the
#            editor; with HALO_SCENE_PACKS=0 the swatch alone
#   h3nameplate  the Halo 3 nameplate (the main menu's, the lobbies' and the
#            pause screen's, port h3 nameplate) with HALO_TEST_PROFILE's profile
#            and no profile chosen yet (a start): the profile's name and its
#            colour's bar, not the system's user name in green; CUSTOMIZATION
#            renames it and makes it RED, Circle saves: back at the main menu
#            the nameplate says the new name on the red bar at once
#   h3mpcustom  the Halo 3 multiplayer lobby's MAP picker with a custom map (the
#            mod manager's test content: mygulch.map, Blood Gulch's copy under
#            another name): after the Xbox's thirteen, MYGULCH, the rows
#            scrolling to it, its "?" picture and description as the Xbox's list
#            shows them; chosen, the lobby says MYGULCH and START GAME (system
#            link) hosts the game on it (its pregame: one machine cannot start
#            a game of one player on the Linux harness); then started again on
#            that save with mygulch turned off: the picker lists the thirteen
#            only and the lobby is back on Battle Creek
#   h3damaged  a profile of a 0-byte blam.sav and one of garbage: SELECT
#            PROFILE says each is damaged and plays neither, CUSTOMIZATION
#            edits neither, CREATE NEW PROFILE still makes one; with
#            HALO_TEST_PROFILE's profile and a damaged savegame.bin, the lobby
#            says the saved game is damaged and starts at Mission Start
#   h3oddrows the public lists' odd listings (HALO_TEST_BROWSER_ODD: the owner's
#            photo of beta.7's browser, Oct 18 2026) in the Halo 3 browser and
#            OpenCE's (Menu style Halo CE): a name that names nothing shows its
#            host's player's, one of other alphabets "?" for what the font
#            lacks, co-op its difficulty and level (or "-"), every missing field
#            "-", and a name or map wider than its column cut with "..."
#   h3coopguide  the settings panel's Host co-op campaign
#            (HALO_SYSTEM_LINK_TEST=campaign): the Campaign screen opens, the
#            Halo 3 campaign lobby behind it set to SYSTEM LINK CO-OP
#   h3mods   the Halo 3 MOD MANAGER (menus/halo3/mods.xml, the main menu's) over
#            content made at test time (mods_data: a modded Xbox map made from
#            Blood Gulch, a corrupt file, a Custom Edition header with nothing
#            after it, an MCC mod whose map is corrupt, with links out of its
#            folder): each listed with its kind, size and the game's check
#            (the corrupt ones failed, said why, no crash); mygulch turned off
#            (out of the map lists, the bots' playlist and map sharing) and on
#            again; its details; Square on broken.map, Circle keeps it, Square
#            and YES deletes it (only it: the maps folder's other files, the
#            links' targets kept); the mod's folder deleted, the links in it
#            gone, what they pointed at kept
#   h3modspicker  MOD MANAGER, mygulch off, then MULTIPLAYER, START GAME: the
#            Xbox's map list (the one custom maps are offered in) leaves it out;
#            again with it off (HALO_MAPS_DISABLED) and turned on in MOD
#            MANAGER: the list offers it
#   h3modstouch  taps: MOD MANAGER on the main menu, mygulch's row (off), the
#            screen's Back
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
cases=${*:-fresh level xbox pause h3probe h3classic h3xbox h3broken h3style h3online h3touch h3campaign h3resume h3pause h3mp h3browser h3create h3mptouch h3coop h3coopguide h3scenes h3profiles h3proftouch h3customize h3nameplate h3damaged h3mods h3modspicker h3modstouch h3mpcustom h3oddrows}
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
	# (or a whole save folder of the tests', as another case's run left it)
	[ -n "${save_from:-}" ] && cp -r "$save_from"/. "$out/$name/save/"
	ln -sfn "$(cd "$folder" && pwd)/maps" "$out/$name/data/maps"
	# (a mods folder of the case's: mods_data's, copied with its links as links)
	[ -n "${mods_from:-}" ] && cp -a "$mods_from" "$out/$name/data/mods"
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
# (a profile's file, blam.sav: name, colour, flags, the levels' flags, the last level, the
# controller settings - source/saved games/player_profile.h - as "name color stick buttons look
# invert flight autocenter vibration_off levels")
profile_fields() {
	python3 - "$1" <<'PY'
import struct, sys
data = open(sys.argv[1], 'rb').read()
name = data[0:24].decode('utf-16-le').split('\0')[0]
color, flags = struct.unpack('<hH', data[24:28])
levels = data[28:38].hex()
last, = struct.unpack('<h', data[38:40])
buttons, stick, look, invert, vibration, flight, autocenter, help_off = data[40:48]
print('%s|%d|%d|%d|%d|%d|%d|%d|%d|%s|%d' % (name, color, stick, buttons, look, invert, flight, autocenter, vibration,
	levels, last))
PY
}
# (no Xbox profile screen opened: the campaign's, the settings' and their edit screens, the
# Xbox's keyboard's new profile)
h3_no_xbox_profiles() { # NAME
	grep -aE "ui: screen ui.shell.main_menu.(load_campaign_player_profile|player_profiles_select|settings_select|new_campaign)" \
		"$out/$1/run.log" && fail "$1" "an Xbox profile screen opened with Menu style Halo 3"
}
# (internet play on, nothing reached: its brokers, STUN and relays TEST-NET addresses)
h3net="HALO_NET_ONLINE=true HALO_NET_BROKERS=198.51.100.1:1883 HALO_NET_STUN=198.51.100.1:3478"
h3net="$h3net HALO_NET_RELAYS=198.51.100.1:47320 HALO_NET_ALLOW_UPNP=false"
# (the main menu: down to MULTIPLAYER, A: the lobby, its NETWORK row focused)
h3mppad="wait:150:3000 down:150:700 a wait:150:3000"

# the mod manager's content, made at test time in FOLDER (never in the
# repository): FOLDER/maps, links to the menus data's maps folder's files, a
# modded Xbox map (Blood Gulch's copy, its header named mygulch, as
# run_netns_online_test.sh makes one), a corrupt file (broken.map) and a
# Custom Edition header with nothing after it (cefake.map); FOLDER/mods, a mod
# (TestMod: its CampaignInfo.json, a corrupt maps/a10.map, links out of it to
# FOLDER/outside); FOLDER/outside, what those links point at
mods_data() { # FOLDER
	local folder=$1 file
	rm -rf "$folder"
	mkdir -p "$folder/maps" "$folder/mods/TestMod/maps" "$folder/mods/TestMod/assets" "$folder/outside"
	for file in "$menus_data"/maps/*; do
		ln -s "$(readlink -f "$file")" "$folder/maps/$(basename "$file")"
	done
	cp --reflink=auto "$(readlink -f "$menus_data/maps/bloodgulch.map")" "$folder/maps/mygulch.map"
	printf 'mygulch' | dd of="$folder/maps/mygulch.map" bs=1 seek=32 conv=notrunc status=none
	dd if=/dev/zero of="$folder/maps/mygulch.map" bs=1 seek=39 count=25 conv=notrunc status=none
	head -c 70000 /dev/urandom > "$folder/maps/broken.map"
	python3 -c 'import sys; h = bytearray(2048); h[0:4] = b"daeh"; h[4:8] = (609).to_bytes(4, "little"); h[0x60] = 1; open(sys.argv[1], "wb").write(h)' \
		"$folder/maps/cefake.map"
	head -c 5000 /dev/urandom > "$folder/mods/TestMod/maps/a10.map"
	printf '{"Title": "Test Mod"}' > "$folder/mods/TestMod/CampaignInfo.json"
	echo kept > "$folder/outside/sentinel.txt"
	ln -s "$folder/outside" "$folder/mods/TestMod/assets/outside"
	ln -s "$folder/outside/sentinel.txt" "$folder/mods/TestMod/assets/linked.txt"
}
# (the main menu: down four to MOD MANAGER, A: its list, broken.map first)
h3modspad="wait:150:3000 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:4000"
# (MOD MANAGER, mygulch's row - two down -, A; B: the main menu, on MOD MANAGER; up three to MULTIPLAYER, A;
# down three to START GAME, A: the profile, A to join, ...: the Xbox's map list)
h3pickerpad="$h3modspad down:150:500 down:150:500 a wait:150:2000 b wait:150:3000 up:150:500 up:150:500 up:150:500 a wait:150:3000 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:5000"

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
		# main menu, CAMPAIGN focused; CAMPAIGN again, the profile, the lobby: its caption; Difficulty,
		# down, A: Heroic)
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
		# (parked with the lobbies' Spartans: it needs their scene_object= back in the XML)
		if ! grep -q 'scene_object=' "$HALO_MENUS_FOLDER"/halo3/campaign.xml; then
			echo "--- h3characters"; echo "SKIP: parked (the lobbies have no scene_object=; menus/halo3/README.txt)"; continue
		fi
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3characters"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (as h3scenes: CAMPAIGN (first since the main menu lost START SOLO GAME), the profile: the
		# campaign lobby; B, B; down, A: MULTIPLAYER; B; down, A: FORGE; B)
		characters_pad="wait:150:3000 a wait:150:3000 a wait:150:5000 b wait:150:2500 b wait:150:3000 down:150:700 a wait:150:5000 b wait:150:3000 down:150:700 a wait:150:5000 b wait:150:3000"
		profile_from=$HALO_TEST_PROFILE run h3characters "$menus_data" 50 "$characters_pad" HALO_MENU_STYLE=halo3
		# (the multiplayer lobby, then the preview: in colour 5, recoloured 9, hidden)
		run h3characters_preview "$menus_data" 26 "wait:150:3000 down:150:700 a wait:150:15000" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_COMMANDS=330:pack preview 5;420:pack preview 9;510:pack preview off"
		profile_from=$HALO_TEST_PROFILE run h3characters_off "$menus_data" 50 "$characters_pad" HALO_MENU_STYLE=halo3 HALO_SCENE_PACKS=0
		# (a maps folder of ui.map and the Halo PC files alone: no multiplayer map to build from)
		rm -rf "$out/nomap_data"
		mkdir -p "$out/nomap_data/maps"
		for map in ui bitmaps loc; do ln -s "$(readlink -f "$menus_data/maps/$map.map")" "$out/nomap_data/maps/$map.map"; done
		run h3characters_nomap "$out/nomap_data" 22 "wait:150:3000 down:150:700 a wait:150:8000" HALO_MENU_STYLE=halo3
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
	h3profiles)
		# (a first start: CAMPAIGN; Cross on CREATE NEW PROFILE: spaces, refused; Cross again: Rookie,
		# made and played, the lobby; Circle: SELECT PROFILE again, Rookie there)
		run h3profiles "$menus_data" 30 "$h3pad a wait:150:3000 a wait:150:2500 a wait:150:4000 b wait:150:3000" \
			HALO_MENU_STYLE=halo3 "HALO_TEST_TEXT_INPUT=  |Rookie"
		check_h3 h3profiles halo3
		log=$out/h3profiles/run.log
		grep -aq "menus: Halo 3 screen h3/profiles/screen for ui.shell.main_menu.load_campaign_player_profile" "$log" ||
			fail h3profiles "SELECT PROFILE did not stand for the Xbox's campaign profile screen"
		grep -aq "menus: h3 profiles: 0 profiles (campaign)" "$log" &&
			grep -aq "menus: h3 profiles: CREATE NEW PROFILE (1 of 1)" "$log" ||
			fail h3profiles "a first start did not show CREATE NEW PROFILE alone"
		grep -aq 'menus: h3 profiles: the name "" was refused: A profile.s name needs a letter or a number.' "$log" ||
			fail h3profiles "a name of spaces was not refused"
		grep -aq "menus: h3 profiles: created Rookie" "$log" && grep -aq "menus: h3 profiles: chose Rookie (campaign)" "$log" ||
			fail h3profiles "CREATE NEW PROFILE did not make and choose Rookie"
		grep -aq "ui: screen pc.h3.campaign.screen" "$log" || fail h3profiles "the new profile did not go on to the lobby"
		grep -aq "menus: h3 profiles: 1 profile (campaign): Rookie" "$log" || fail h3profiles "Rookie was not listed after"
		file=$(ls "$out"/h3profiles/save/u/UDATA/*/blam.sav 2>/dev/null | head -1)
		[ -n "$file" ] && [ "$(profile_fields "$file" | cut -d'|' -f1)" = Rookie ] ||
			fail h3profiles "Rookie's blam.sav was not written"
		h3_no_xbox_profiles h3profiles
		# (Menu style Halo CE: the Xbox's campaign profile screen, as ever)
		profile_from=${HALO_TEST_PROFILE:-} run h3profiles_classic "$menus_data" 20 "wait:150:3000 a wait:150:4000" HALO_MENU_STYLE=classic
		grep -aq "ui: screen ui.shell.main_menu.load_campaign_player_profile" "$out/h3profiles_classic/run.log" ||
			fail h3profiles "Menu style Halo CE did not open the Xbox's campaign profile screen"
		grep -aq "ui: screen pc.h3" "$out/h3profiles_classic/run.log" && fail h3profiles "a Halo 3 screen opened with Menu style Halo CE"
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "SKIP (h3profiles): no HALO_TEST_PROFILE: its name taken, the delete"; continue
		fi
		# (the test profile's: CAMPAIGN; Triangle: its own name, taken; Triangle: Rookie, made and played;
		# Circle: SELECT PROFILE on Rookie; Square: the question on CANCEL, down, DELETE PROFILE)
		taken=$(profile_fields "$HALO_TEST_PROFILE" | cut -d'|' -f1)
		profile_from=$HALO_TEST_PROFILE run h3profiles_delete "$menus_data" 40 "$h3pad a wait:150:3000 y wait:150:2500 y wait:150:4000 b wait:150:3000 x wait:150:2500 down:150:700 a wait:150:4000" \
			HALO_MENU_STYLE=halo3 "HALO_TEST_TEXT_INPUT=$taken|Rookie"
		check_h3 h3profiles_delete halo3
		log=$out/h3profiles_delete/run.log
		grep -aq "menus: h3 profiles: the name \"$taken\" was refused: Another profile has that name." "$log" ||
			fail h3profiles "the test profile's name was not refused as taken"
		grep -aq "menus: h3 profiles: 2 profiles (campaign): .*Rookie" "$log" || fail h3profiles "Rookie was not listed beside the test profile"
		grep -aq "ui: screen pc.h3.profdel.screen" "$log" || fail h3profiles "Square did not ask before deleting"
		grep -aq "menus: h3 profiles: deleted Rookie" "$log" || fail h3profiles "DELETE PROFILE did not delete Rookie"
		grep -a "menus: h3 profiles: [0-9]* profiles\? (campaign)" "$log" | tail -1 | grep -q "1 profile (campaign): $taken" ||
			fail h3profiles "the list after the delete was not the test profile alone"
		cmp -s "$HALO_TEST_PROFILE" "$out/h3profiles_delete/save/u/UDATA/869079979327/blam.sav" ||
			fail h3profiles "the test profile's blam.sav changed"
		h3_no_xbox_profiles h3profiles_delete ;;
	h3proftouch)
		# (CAMPAIGN's row on the main menu; SELECT PROFILE's New Profile; the lobby's Back)
		run h3proftouch "$menus_data" 22 "wait:150:14000" HALO_MENU_STYLE=halo3 HALO_TEST_TEXT_INPUT=Rookie \
			"HALO_TEST_TAPS=4000:60,278 8000:400,430 12000:140,455"
		check_h3 h3proftouch halo3
		log=$out/h3proftouch/run.log
		grep -aq "ui: screen pc.h3.profiles.screen" "$log" || fail h3proftouch "a tap on CAMPAIGN did not open SELECT PROFILE"
		grep -aq "menus: h3 profiles: created Rookie" "$log" && grep -aq "ui: screen pc.h3.campaign.screen" "$log" ||
			fail h3proftouch "a tap on New Profile did not make one and go on to the lobby"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.profiles.screen' ||
			fail h3proftouch "a tap on the lobby's Back did not go back to SELECT PROFILE" ;;
	h3customize)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3customize"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (CUSTOMIZATION: the profile, Cross; PLAYER NAME, Cross: typed; COLOR, right three times: RED;
		# STICK LAYOUT, right: SOUTHPAW; down two to LOOK SENSITIVITY, right twice; INVERT LOOK, Cross:
		# YES; Circle: saved; Circle)
		custom_pad="$h3pad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:3000 a wait:150:2000"
		custom_pad="$custom_pad down:150:600 right:150:500 right:150:500 right:150:500 down:150:600 right:150:600"
		custom_pad="$custom_pad down:150:600 down:150:600 right:150:500 right:150:500 down:150:600 a wait:150:1500 b wait:150:3000 b wait:150:3000"
		profile_from=$HALO_TEST_PROFILE run h3customize "$menus_data" 45 "$custom_pad" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_TEXT_INPUT=Vita Chief"
		check_h3 h3customize halo3
		log=$out/h3customize/run.log
		grep -aq "menus: Halo 3 screen h3/custprof/screen for ui.shell.main_menu.settings_select.player_setup.player_profile_select_screen" "$log" ||
			fail h3customize "CUSTOMIZATION did not stand for the Xbox's profile settings"
		grep -aq "ui: screen pc.h3.custom.screen" "$log" || fail h3customize "the editor did not open"
		grep -aq "menus: h3 customize: saved Vita Chief: color 2, stick 1, buttons 0, look 5, invert 1" "$log" ||
			fail h3customize "Circle did not save the changes"
		h3_no_xbox_profiles h3customize
		# (the preview: the scene packs' Spartan in the editor's window, in the profile's colour - its
		# RANDOM shown white -, made again in RED as COLOR changes, let go with the editor)
		grep -aE "h3 customize: .*preview|h3 scene object 4|h3 scene: h3/custom" "$log"
		grep -aq "menus: h3 scene: h3/custom/screen: camera point multiplayer" "$log" ||
			fail h3customize "the editor's own camera was not set"
		grep -aq "menus: h3 customize: preview Spartan asked for in colour 0" "$log" ||
			fail h3customize "the preview's Spartan was not asked for"
		grep -aq "menus: h3 scene object 4: characters.cyborg_mp.cyborg_mp (menu_spartan) at .* colour 0: made" "$log" &&
			grep -aq "menus: h3 customize: the preview shows the Spartan" "$log" ||
			fail h3customize "the preview did not show the Spartan"
		grep -aq "menus: h3 customize: preview Spartan recoloured 2" "$log" &&
			grep -aq "menus: h3 scene object 4: characters.cyborg_mp.cyborg_mp (menu_spartan) at .* colour 2: made" "$log" ||
			fail h3customize "the preview was not recoloured RED"
		grep -aq "menus: h3 customize: the preview shows the swatch" "$log" &&
			fail h3customize "the preview fell back to the swatch while it was recoloured"
		grep -aq "menus: h3 customize: preview Spartan hidden" "$log" && grep -aq "menus: h3 scene object 4: deleted" "$log" ||
			fail h3customize "the preview's Spartan was not let go with the editor"
		# (the profile's file: the fields changed, the others as they were; renamed, its folder too)
		file=$(ls "$out"/h3customize/save/u/UDATA/*/blam.sav 2>/dev/null | head -1)
		before=$(profile_fields "$HALO_TEST_PROFILE")
		after=$([ -n "$file" ] && profile_fields "$file")
		echo "profile before: $before"
		echo "profile after:  $after"
		[ "$(echo "$after" | cut -d'|' -f1-6)" = "Vita Chief|2|1|0|5|1" ] || fail h3customize "blam.sav does not hold the changes"
		[ "$(echo "$after" | cut -d'|' -f7-11)" = "$(echo "$before" | cut -d'|' -f7-11)" ] ||
			fail h3customize "blam.sav's other fields changed"
		[ -n "$file" ] && [ "$(tr -d '\0' < "$(dirname "$file")/SaveMeta.xbx")" = "Vita Chief" ] ||
			fail h3customize "SaveMeta.xbx does not name the profile Vita Chief"
		# (started again on that save: the editor shows the changes; Circle saves nothing; and with
		# HALO_SCENE_PACKS=0 no Spartan: the window keeps the colour's swatch)
		rm -rf "$out/h3customize_saved"
		cp -r "$out/h3customize/save" "$out/h3customize_saved"
		save_from=$out/h3customize_saved run h3customize_again "$menus_data" 25 \
			"$h3pad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:3000 b wait:150:3000" HALO_MENU_STYLE=halo3 \
			HALO_SCENE_PACKS=0
		check_h3 h3customize_again halo3
		grep -aqE "menus: h3 customize: the preview shows the Spartan|h3 scene object 4: .* made" "$out/h3customize_again/run.log" &&
			fail h3customize "HALO_SCENE_PACKS=0: the preview showed a Spartan all the same"
		grep -aq "menus: h3 customize: editing Vita Chief: color 2, stick 1, buttons 0, look 5, invert 1" "$out/h3customize_again/run.log" ||
			fail h3customize "the changes were not there after a restart"
		grep -aq "menus: h3 customize: Vita Chief: no changes" "$out/h3customize_again/run.log" ||
			fail h3customize "leaving the editor unchanged saved anyway" ;;
	h3nameplate)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3nameplate"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (the main menu at a start: the nameplate; then CUSTOMIZATION as h3customize - the name typed,
		# COLOR right three times: RED -, Circle: saved, Circle: the main menu again, its nameplate)
		plate_name=$(profile_fields "$HALO_TEST_PROFILE" | cut -d'|' -f1)
		plate_color=$(profile_fields "$HALO_TEST_PROFILE" | cut -d'|' -f2)
		plate_pad="$h3pad @00-plate-at-start:0:300 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000"
		plate_pad="$plate_pad a wait:150:3000 a wait:150:2000 down:150:600 right:150:500 right:150:500 right:150:500"
		plate_pad="$plate_pad b wait:150:3000 b wait:150:3000 up:150:500 up:150:500 up:150:500 up:150:500 up:150:500 wait:150:1000"
		plate_pad="$plate_pad @01-plate-customized:0:300"
		rm -rf "$out/h3nameplate_shots"
		mkdir -p "$out/h3nameplate_shots"
		profile_from=$HALO_TEST_PROFILE run h3nameplate "$menus_data" 40 "$plate_pad" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_TEXT_INPUT=Vita Chief" HALO_NET_PLAYER_NAME=user549 HALO_SCREENSHOT_DIR="$out/h3nameplate_shots"
		check_h3 h3nameplate halo3
		log=$out/h3nameplate/run.log
		grep -a "menus: nameplate" "$log"
		# (a start, no profile chosen: the profile's name and colour, its frame the colour's - 0, the
		# green, for a profile of no colour -; never the user name)
		frame=$((plate_color < 0 || plate_color >= 18 ? 0 : plate_color + 1))
		grep -a "menus: nameplate:" "$log" | head -1 | grep -q "menus: nameplate: $plate_name, colour $plate_color (frame $frame)$" ||
			fail h3nameplate "at the start the nameplate did not show $plate_name's name and colour"
		grep -aq "menus: nameplate: user549" "$log" && fail h3nameplate "the nameplate showed the user name"
		grep -aq "menus: h3 customize: saved Vita Chief: color 2" "$log" || fail h3nameplate "CUSTOMIZATION did not save"
		# (after the save, at once: the new name on the red bar, frame 3)
		grep -aq "menus: nameplate: Vita Chief, colour 2 (frame 3)$" "$log" ||
			fail h3nameplate "the nameplate did not change to the saved name and colour"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.main.screen' || fail h3nameplate "not back at the main menu"
		[ -n "${HALO_TEST_SHOTS:-}" ] && mkdir -p "$HALO_TEST_SHOTS/h3nameplate" &&
			cp "$out"/h3nameplate_shots/*.bmp "$HALO_TEST_SHOTS/h3nameplate/" 2>/dev/null ;;
	h3damaged)
		# (two damaged profiles: Broken, a 0-byte blam.sav; Garbage, 512 bytes of garbage)
		rm -rf "$out/h3damaged_save"
		mkdir -p "$out/h3damaged_save/u/UDATA/000000000001" "$out/h3damaged_save/u/UDATA/000000000002"
		: > "$out/h3damaged_save/u/UDATA/000000000001/blam.sav"
		printf 'B\0r\0o\0k\0e\0n\0' > "$out/h3damaged_save/u/UDATA/000000000001/SaveMeta.xbx"
		head -c 512 /dev/urandom > "$out/h3damaged_save/u/UDATA/000000000002/blam.sav"
		printf 'G\0a\0r\0b\0a\0g\0e\0' > "$out/h3damaged_save/u/UDATA/000000000002/SaveMeta.xbx"
		# (CAMPAIGN: Cross on each, refused; CREATE NEW PROFILE, Cross: Rookie, the lobby; Circle twice:
		# the main menu, where no profile is in use; CUSTOMIZATION, on the first listed - a damaged
		# one: Rookie, made last, is listed last -: Cross, refused)
		save_from=$out/h3damaged_save run h3damaged "$menus_data" 45 "$h3pad a wait:150:3000 a wait:150:2000 down:150:700 a wait:150:2000 down:150:700 a wait:150:4000 b wait:150:3000 b wait:150:3000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2500 b wait:150:2000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_TEXT_INPUT=Rookie
		check_h3 h3damaged halo3
		log=$out/h3damaged/run.log
		for damaged in Broken Garbage; do
			grep -aq "menus: h3 profiles: $damaged is damaged: not chosen" "$log" ||
				fail h3damaged "$damaged (a damaged profile) was not refused"
		done
		grep -aq "menus: h3 profiles: .* is damaged: not edited" "$log" || fail h3damaged "CUSTOMIZATION edited a damaged profile"
		grep -aq "menus: h3 profiles: created Rookie" "$log" && grep -aq "ui: screen pc.h3.campaign.screen" "$log" ||
			fail h3damaged "CREATE NEW PROFILE did not make one and go on to the lobby"
		grep -aq "menus: h3 profiles: chose \(Broken\|Garbage\)" "$log" && fail h3damaged "a damaged profile was played"
		h3_no_xbox_profiles h3damaged
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "SKIP (h3damaged): no HALO_TEST_PROFILE: the damaged saved game"; continue
		fi
		# (the test profile with a saved game of garbage, as large as one: CAMPAIGN, the profile, the lobby)
		rm -rf "$out/h3damaged_game_save"
		mkdir -p "$out/h3damaged_game_save/u/UDATA/869079979327"
		head -c 16777216 /dev/urandom > "$out/h3damaged_game_save/u/UDATA/869079979327/savegame.bin"
		profile_from=$HALO_TEST_PROFILE save_from=$out/h3damaged_game_save run h3damaged_game "$menus_data" 25 "$h3pad a wait:150:3000 a wait:150:5000" \
			HALO_MENU_STYLE=halo3
		check_h3 h3damaged_game halo3
		log=$out/h3damaged_game/run.log
		grep -aq "menus: h3 campaign: the saved game is damaged: it can't be loaded" "$log" ||
			fail h3damaged "the lobby did not say the saved game is damaged"
		grep -aq "menus: h3 campaign caption: .* at Mission Start" "$log" || fail h3damaged "the lobby did not start at Mission Start"
		cmp -s "$HALO_TEST_PROFILE" "$out/h3damaged_game/save/u/UDATA/869079979327/blam.sav" ||
			fail h3damaged "the test profile's blam.sav changed" ;;
	h3oddrows)
		# (the Halo 3 lobby on ONLINE: down five to BROWSE GAMES, A: the Halo 3 browser; OpenCE's:
		# Multiplayer, SERVER BROWSER)
		run h3oddrows "$menus_data" 35 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:8000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_TEST_BROWSER_ODD=1
		run h3oddrows_classic "$menus_data" 35 "wait:150:2000 down:150:800 a wait:150:4000 a wait:150:8000" \
			HALO_MENU_STYLE=classic $h3net HALO_TEST_BROWSER_ODD=1
		check_h3 h3oddrows halo3
		for name in h3oddrows h3oddrows_classic; do
			log=$out/$name/run.log
			coop="Co-op · Legendary"
			[ $name = h3oddrows_classic ] && coop="Co-op (Legendary)"
			for row in "Spartan | Slayer | Blood Gulch | 8/128" "Jos?? ??? Ma??ana | CTF | Chill Out | 3/16" \
				"true legendary run (cut) | $coop | The Pillar of Autumn (cut) | 3/4" "teto | Co-op | - | 1/4" \
				"BIG TEAM 24*7 (cut) | - | - | -" "HALO UNIVERSAL CUSTOM SERVER 24/ (cut) | Team Slayer | bigassv2,104 | 24/32"; do
				grep -aqF "$row" "$log" || fail h3oddrows "$name: no row \"$row\""
			done
			grep -aE "menus: browser row [0-9]+: " "$log" | grep -aqE "row [0-9]+:  ?\|" && fail h3oddrows "$name: a row without a name"
		done
		grep -aqF "xXxNoScopeLegendsxXxForeverMore (cut)" "$out/h3oddrows/run.log" ||
			fail h3oddrows "the long single-word name was not cut to its column" ;;
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
	h3mods)
		# (the list; mygulch off and on; its details; broken.map: Square, Circle keeps it, Square,
		# down to YES, A; cefake.map focused then: down two to TestMod, Square, down, A; B)
		mods_data "$out/h3mods-data"
		mods_from="$out/h3mods-data/mods" run h3mods "$out/h3mods-data" 75 "$h3modspad down:150:500 down:150:500 a wait:150:1500 a wait:150:1500 y wait:150:2500 b wait:150:2000 up:150:500 up:150:500 x wait:150:2000 b wait:150:1500 x wait:150:2000 down:150:500 a wait:150:3000 down:150:500 down:150:500 x wait:150:2000 down:150:500 a wait:150:3000 b wait:150:2000" \
			HALO_MENU_STYLE=halo3
		check_h3 h3mods halo3
		log=$out/h3mods/run.log
		grep -aq "ui: screen pc.h3.mods.screen" "$log" || fail h3mods "MOD MANAGER did not open its screen"
		grep -aq "menus: h3 mods: 4 entries" "$log" || fail h3mods "the four made-up entries were not listed"
		grep -aq "menus: h3 mods: mygulch.map: Xbox map, on, [0-9]* KB, passed, works in Multiplayer" "$log" ||
			fail h3mods "the modded Xbox map was not listed as one that plays in Multiplayer"
		grep -aq "menus: h3 mods: broken.map: Unknown file, on, 69 KB, failed: Not a Halo map" "$log" ||
			fail h3mods "the corrupt file was not shown as failing the check"
		grep -aq "menus: h3 mods: cefake.map: .*, failed: " "$log" ||
			fail h3mods "the empty Custom Edition header was not shown as failing the check"
		grep -aq "menus: h3 mods: TestMod: MCC mod, on, [0-9]* KB, failed: a10.map: " "$log" ||
			fail h3mods "the mod with a corrupt map was not shown as failing the check"
		grep -aq "mod manager: mygulch turned off: map lists no, bots playlist no, map sharing no" "$log" ||
			fail h3mods "mygulch off was still in a map list, the bots' playlist or map sharing"
		grep -aq "mod manager: mygulch turned on: map lists yes, bots playlist yes, map sharing yes" "$log" ||
			fail h3mods "mygulch on again was not back in the map lists, the bots' playlist and map sharing"
		grep -aq "ui: screen pc.h3.mods.details" "$log" || fail h3mods "Triangle did not open the details"
		[ "$(grep -ac 'ui: screen pc.h3.mods.confirm' "$log")" -ge 3 ] || fail h3mods "Square did not ask before deleting"
		grep -aq "mod manager: the map broken.map deleted" "$log" || fail h3mods "YES did not delete broken.map"
		[ "$(grep -ac 'menus: h3 mods: delete broken.map?' "$log")" = 2 ] ||
			fail h3mods "broken.map was not asked about twice (Circle, then YES)"
		[ -e "$out/h3mods-data/maps/broken.map" ] && fail h3mods "broken.map is still there"
		[ -e "$out/h3mods-data/maps/mygulch.map" ] && [ -e "$out/h3mods-data/maps/cefake.map" ] &&
			[ -L "$out/h3mods-data/maps/bitmaps.map" ] && [ -e "$out/h3mods-data/maps/bitmaps.map" ] ||
			fail h3mods "a file of the maps folder other than broken.map went"
		grep -aq "mod manager: the mod folder TestMod deleted" "$log" || fail h3mods "the mod's folder was not deleted"
		[ -e "$out/h3mods/data/mods/TestMod" ] && fail h3mods "the mod's folder is still there"
		[ -d "$out/h3mods/data/mods" ] || fail h3mods "the mods folder itself went"
		[ "$(cat "$out/h3mods-data/outside/sentinel.txt" 2>/dev/null)" = kept ] ||
			fail h3mods "a file a link in the mod pointed at was deleted (the link was followed)"
		grep -aq "menus: h3 mods: Deleted TestMod" "$log" || fail h3mods "the screen did not say the mod was deleted" ;;
	h3modspicker)
		mods_data "$out/h3mods-data"
		run h3modspicker "$out/h3mods-data" 50 "$h3pickerpad" HALO_MENU_STYLE=halo3
		run h3modspicker_on "$out/h3mods-data" 50 "$h3pickerpad" HALO_MENU_STYLE=halo3 HALO_MAPS_DISABLED=mygulch
		for name in h3modspicker h3modspicker_on; do
			check_h3 $name halo3
			grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.connected.connected_map_select_wrapper" \
				"$out/$name/run.log" || fail $name "START GAME did not lead to the Xbox's map list"
		done
		grep -aq "mod manager: mygulch turned off" "$out/h3modspicker/run.log" || fail h3modspicker "mygulch was not turned off"
		grep -aq "custom edition: the multiplayer level list: 13 Xbox levels, then no custom map" "$out/h3modspicker/data/debug.txt" ||
			fail h3modspicker "the Xbox's map list offered mygulch, turned off"
		grep -aq "custom edition: the map 'mygulch' is turned off" "$out/h3modspicker/data/debug.txt" ||
			fail h3modspicker "the map list did not say mygulch is off"
		grep -aq "mod manager: mygulch turned on" "$out/h3modspicker_on/run.log" || fail h3modspicker "mygulch was not turned on"
		grep -aq "custom edition: the multiplayer level list: 13 Xbox levels, then mygulch" "$out/h3modspicker_on/data/debug.txt" ||
			fail h3modspicker "the Xbox's map list did not offer mygulch, turned on again" ;;
	h3mpcustom)
		# (MULTIPLAYER: the lobby on SYSTEM LINK; down two to MAP, A: the picker; down thirteen: past the
		# last row it scrolls to MYGULCH; A: chosen; down to START GAME, A: the profile, A to join, ...:
		# the Xbox's map list on mygulch, A; the game type, Start: the hosted game's lobby)
		mods_data "$out/h3mods-data"
		mpcustom_pad="$h3mppad down:150:500 down:150:500 a wait:150:2000 @00-picker:0:300"
		for row in $(seq 1 13); do mpcustom_pad="$mpcustom_pad down:150:400"; done
		mpcustom_pad="$mpcustom_pad @01-picker-custom:0:300 a wait:150:2000 @02-lobby-custom:0:300 down:150:500 a wait:150:3000"
		mpcustom_pad="$mpcustom_pad a wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:3000 start wait:150:4000 @03-pregame:0:300"
		rm -rf "$out/h3mpcustom_shots"
		mkdir -p "$out/h3mpcustom_shots"
		run h3mpcustom "$out/h3mods-data" 55 "$mpcustom_pad" HALO_MENU_STYLE=halo3 HALO_SCREENSHOT_DIR="$out/h3mpcustom_shots"
		# (again on that save, mygulch off: MULTIPLAYER, MAP's picker, B)
		rm -rf "$out/h3mpcustom_saved"
		cp -r "$out/h3mpcustom/save" "$out/h3mpcustom_saved"
		save_from=$out/h3mpcustom_saved run h3mpcustom_off "$out/h3mods-data" 30 \
			"$h3mppad down:150:500 down:150:500 a wait:150:2000 b wait:150:2000" HALO_MENU_STYLE=halo3 HALO_MAPS_DISABLED=mygulch
		check_h3 h3mpcustom halo3
		check_h3 h3mpcustom_off halo3
		log=$out/h3mpcustom/run.log
		grep -aE "menus: h3 mp|menus: h3 maps|join: the host's map" "$log" | tail -8
		grep -aq "menus: h3 mp: the map picker: 14 maps (1 custom)" "$log" ||
			fail h3mpcustom "the picker did not list the custom map after the thirteen"
		grep -aq "menus: h3 maps: Mygulch (14 of 14)" "$log" || fail h3mpcustom "the rows did not scroll to MYGULCH"
		grep -aq "menus: h3 mp: map Mygulch (a custom map: levels.test.mygulch.mygulch)" "$log" ||
			fail h3mpcustom "choosing MYGULCH did not set the lobby's map to it"
		grep -aq "menus: h3 mp lobby: NETWORK: SYSTEM LINK, GAME: SLAYER, MAP: MYGULCH" "$log" ||
			fail h3mpcustom "the lobby did not say MYGULCH"
		grep -aq "join: the host's map is levels.test.mygulch.mygulch" "$log" ||
			fail h3mpcustom "START GAME did not host the game on mygulch"
		grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.connected.pregame.connected_pregame_screen" "$log" ||
			fail h3mpcustom "the hosted game's lobby did not open"
		log=$out/h3mpcustom_off/run.log
		grep -aq "menus: h3 mp: the last map used, levels.test.mygulch.mygulch, is not in the map list (off or gone)" "$log" ||
			fail h3mpcustom "mygulch off: the lobby did not leave it"
		grep -aq "menus: h3 mp lobby: NETWORK: SYSTEM LINK, GAME: SLAYER, MAP: BATTLE CREEK" "$log" ||
			fail h3mpcustom "mygulch off: the lobby is not on Battle Creek"
		grep -aq "menus: h3 mp: the map picker: 13 maps (0 custom)" "$log" ||
			fail h3mpcustom "mygulch off: the picker still listed it"
		[ -n "${HALO_TEST_SHOTS:-}" ] && mkdir -p "$HALO_TEST_SHOTS/h3mpcustom" &&
			cp "$out"/h3mpcustom_shots/*.bmp "$HALO_TEST_SHOTS/h3mpcustom/" 2>/dev/null ;;
	h3modstouch)
		# (MOD MANAGER's row on the main menu; mygulch's row, the third; the screen's Back: icon and label)
		mods_data "$out/h3mods-data"
		run h3modstouch "$out/h3mods-data" 25 "wait:150:16000" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_TAPS=4000:60,358 8000:200,142 12000:560,452"
		check_h3 h3modstouch halo3
		log=$out/h3modstouch/run.log
		grep -aq "ui: screen pc.h3.mods.screen" "$log" || fail h3modstouch "a tap on MOD MANAGER did not open it"
		grep -aq "menus: h3 mods: mygulch.map turned off" "$log" || fail h3modstouch "a tap on mygulch's row did not turn it off"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.main.screen' ||
			fail h3modstouch "a tap on Back did not go back to the main menu" ;;
	*)
		echo "usage: $0 [fresh|level|xbox|pause|h3probe|h3classic|h3xbox|h3broken|h3style|h3online|h3touch|h3campaign|h3resume|h3pause|h3mp|h3browser|h3create|h3mptouch|h3coop|h3coopguide|h3scenes|h3characters|h3profiles|h3proftouch|h3customize|h3nameplate|h3damaged|h3mods|h3modspicker|h3modstouch|h3mpcustom|h3oddrows]..." >&2; exit 2 ;;
	esac
done
echo "logs in $out"
[ $status = 0 ] && echo PASS
test_out_done $status
exit $status
