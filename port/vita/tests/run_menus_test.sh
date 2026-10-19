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
#            SYSTEM LINK; choosing ONLINE starts internet play live (its status
#            Connecting...: TEST-NET brokers), SYSTEM LINK stops it; START GAME
#            hosts a system link game (the profile, then the Xbox's map list)
#   h3browser  internet play on (its brokers unreachable: TEST-NET addresses)
#            with 16 made-up games of both platforms (HALO_TEST_BROWSER_GAMES,
#            HALO_TEST_BROWSER_PC): BROWSE GAMES, the profile, the Halo 3
#            games browser with the online list's games (CUSTOM GAMES), a
#            locked game the Halo 3 password screen, whose JOIN GAME says the
#            game is no longer listed
#   h3create  START GAME on ONLINE: Create Game; its GAME TYPE picker, the
#            name typed (HALO_TEST_TEXT_INPUT), START GAME as Server Setup's
#   h3mptouch  taps: MULTIPLAYER, the lobby's MAP, BLOOD GULCH in its picker,
#            the lobby's Back
#   h3coop   co-op from the Halo 3 campaign lobby (HALO_TEST_PROFILE; skipped
#            without): NETWORK, ONLINE CO-OP with internet play off (it
#            goes online live), SYSTEM LINK CO-OP (back to Same Wi-Fi), START
#            GAME:
#            the level hosted as co-op (as the Xbox's difficulty list's Y
#            hosted it) and the game's lobby, the waiting screen, open
#   h3scenes the lobbies' cameras (scene= in menus/halo3; menus.scenes,
#            HALO_MENU_SCENES): CAMPAIGN (HALO_TEST_PROFILE's profile; a new
#            one's campaign starts at once without), back, MULTIPLAYER, back,
#            FORGE, back: each lobby's camera written into its point of
#            ui.map and glided to, ring_loop asleep meanwhile, the main menu
#            waking it; then the same with HALO_MENU_SCENES=0: none
#   h3lobbyscenes  the lobbies' scenes (scene_object= in menus/halo3:
#            menu_scene_objects.c; the menu_campaign, menu_multiplayer and
#            menu_monitor scene packs, built from the maps folder's own maps the
#            first time: scene_packs.c), HALO_TEST_PROFILE's profile: CAMPAIGN's
#            lobby a Warthog held mid-jump (frozen: its physics off) with the
#            Master Chief seated as its driver and two Marines as its gunner
#            and passenger, MULTIPLAYER's three white Spartans (a rocket
#            launcher in a held airborne frame, the flag, a sniper rifle
#            crouching), FORGE's 343 Guilty Spark looking at a Warthog; each
#            made as its lobby opens, no effect or particle made meanwhile (no
#            dust), let go as the main menu comes back; the Customization
#            screen's preview (the console's "pack preview": shown in a colour,
#            recoloured, hidden); then with HALO_SCENE_PACKS=0, Menu style Halo
#            CE, no campaign map (the multiplayer scene alone) and no map at
#            all: no scene where its pack cannot be, nothing else changed, no
#            crash
#   h3profiles  the Halo 3 SELECT PROFILE (menus/halo3/profiles.xml) stands for
#            the Xbox's campaign profile screen: a first start (no profile)
#            shows CREATE NEW PROFILE alone, a name of spaces refused, then
#            Rookie made (blam.sav, its name) and the lobby opened; back on the
#            list, the card's Spartan (the scene packs' menu_spartan, in
#            Rookie's colour, in the card's window), let go with the screen; with
#            HALO_TEST_PROFILE's profile too: Triangle with that profile's name
#            refused (taken), Rookie made, played, then Square and its
#            question's DELETE PROFILE delete it, the other profile's file left
#            byte for byte; with Menu style Halo CE, the Xbox's screen
#   h3proftouch  taps: CAMPAIGN, SELECT PROFILE's New Profile (its icon and
#            label): Rookie made, the lobby; the lobby's Back: SELECT PROFILE
#   h3customize  CUSTOMIZATION (menus/halo3/customize.xml), with
#            HALO_TEST_PROFILE's profile: its list's card shows the profile's
#            Spartan (as SELECT PROFILE's); the name typed, the colour, the stick
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
#   h3models  CUSTOMIZATION's MODEL (port/linux/game/player_models.c) with the
#            Custom Edition maps of HALO_TEST_CE_MAPS (extinction.map,
#            Covenant_V_Marines_Beta_5.map) and HALO_TEST_PROFILE's profile:
#            the maps folder looked through (two models), MODEL stepped to the
#            Extinction Spartan (its pack built the first time, the preview
#            making it in the profile's colour), to the protected map's, back;
#            Circle keeps the choice in t:\player_models.txt (blam.sav as it
#            was); started again, the editor shows it; skipped without them
#   h3modelsmany  CUSTOMIZATION's MODEL stepped through fourteen models
#            (fourteen links to HALO_TEST_CE_MAPS' extinction.map under names
#            of their own, a pack each), each resting: every one shown, each
#            passed let go (scene_packs_release), none refused for want of
#            room; then eight quick presses: the models passed not loaded
#   h3damaged  a profile of a 0-byte blam.sav and one of garbage: SELECT
#            PROFILE says each is damaged and plays neither, CUSTOMIZATION
#            edits neither, CREATE NEW PROFILE still makes one; with
#            HALO_TEST_PROFILE's profile and a damaged savegame.bin, the lobby
#            says the saved game is damaged and starts at Mission Start
#   browserprofile  the online browsers ask for the profile first, as SYSTEM LINK and CREATE
#            GAME do (the owner, insider.10: "the profile switch picker didn't show up when
#            I click server browser"): Menu style Halo CE's Multiplayer, SERVER BROWSER: the
#            profile screen, then OpenCE's server browser (B: the profile again, B: Multiplayer);
#            the Halo 3 lobby's BROWSE GAMES on ONLINE: the profile, then the games browser;
#            internet play off: SERVER BROWSER says how to turn it on, no profile
#   h3players  the Halo 3 PLAYERS screen (menus/halo3/players.xml; the owner: "Can I move the
#            cursor over the player like on Halo 3 and add friend?"), HALO_TEST_PROFILE's
#            profile: SYSTEM LINK CO-OP hosted from the campaign lobby, the game's lobby;
#            Triangle: PLAYERS, this machine's player and three made up
#            (HALO_TEST_PLAYERS_FAKE=3); Cross on one: its options; ADD FRIEND with friends
#            not online says go online first; this machine's own player has no options;
#            Circle: the game's lobby again (the friend request itself: run_netns_online_test.sh
#            friendsplayers)
#   h3friends the Halo 3 FRIENDS screen (menus/halo3/friends.xml; port/linux/src/p2p_friends.c)
#            over a save's friends made at test time (run_p2p_friends_test.sh seed: two
#            friends, a request, a request sent, a blocked player; shown online,
#            HALO_TEST_FRIENDS_SHOW=online), internet play off: FRIENDS on the main menu, the
#            code and every entry listed; ADD FRIEND types a code (queued until online); APPEAR
#            OFFLINE and FRIENDS CAN JOIN switched; Square declines the request (nothing sent);
#            Cross on the joinable friend says go online first; Triangle's REMOVE FRIEND
#            removes them; the list written and read back by a second start
#   h3party  the Halo 3 PARTY screen (menus/halo3/party.xml; port/linux/src/p2p_party.h):
#            FRIENDS' PARTY row opens it (internet play off: no party, go online
#            first), B goes back to FRIENDS; a friend's options offline have no
#            INVITE TO PARTY (the party's own steps: run_netns_online_test.sh party)
#   h3friendspanel the lobbies' FRIENDS panel: the multiplayer lobby shows who is online
#            and the requests, Triangle opens the FRIENDS screen, B back; the Forge lobby too
#   h3oddrows the public lists' odd listings (HALO_TEST_BROWSER_ODD: the owner's
#            photo of beta.7's browser, Oct 18 2026) in the Halo 3 games browser
#            and OpenCE's (Menu style Halo CE): a name that names nothing shows
#            its host's player's, one of other alphabets "?" for what the font
#            lacks, co-op its difficulty and level (or "-"), every missing field
#            "-", and a name or map wider than its column cut with "..."
#   profilepaths every multiplayer entry path asks for the profile first: Halo CE's SERVER
#            BROWSER, SYSTEM LINK, DIRECT LINK (a code: the System Link screen) and CREATE GAME
#            SYSTEM LINK open the Xbox's profile screen; Halo 3's START GAME (SYSTEM LINK, ONLINE's
#            Create Game, AD HOC), BROWSE GAMES (SYSTEM LINK, ONLINE, AD HOC) and JOIN WITH CODE
#            open SELECT PROFILE (halo3/join.xml)
#   browserrows OpenCE's server browser (Menu style Halo CE) with 16 made-up games
#            of varied names (HALO_TEST_BROWSER_NAMES), PC ones among them: every row
#            named, and the glyph cache never going round each frame (the owner's
#            photo of insider.10, Oct 18 2026: only the first row's name drawn on
#            the Vita, each row below one column fewer)
#   h3lan    the Halo 3 LAN browser (halo3/network.xml) with 14 made-up games of both
#            platforms (HALO_TEST_BROWSER_GAMES, HALO_TEST_BROWSER_PC): BROWSE
#            GAMES on SYSTEM LINK, the profile, then the LAN browser in place of
#            the Xbox's System Link list, no game locked; down to the third
#            game, A: it is not listed by the game (a made-up one), and says
#            so; Y sorts by name, Square looks again, a tap on the first game's
#            row (under VITA GAMES) presses A on it, a tap on Back goes back to the profile; then with no games: "No games found on your network"
#   h3groups the browsers' groups (the owner, Oct 19 2026): Halo 3's games
#            browser (SYSTEM LINK) and OpenCE's (online), 14 made-up games of
#            both platforms: VITA GAMES first, then PC GAMES, each with its
#            count; the first game the fullest Vita game, never a header; the
#            D-pad's right and left: Vita games, PC games, all games
#            (network.browser_filter), the first game of each; at the Vita's
#            picture (HALO_GL_VITA_DISPLAY), its frames with HALO_TEST_SHOTS
#   h3adhoc  ad hoc play from the Halo 3 lobby: with the connection Ad hoc
#            (HALO_NET_ADHOC) the lobby opens on AD HOC; BROWSE GAMES out of the
#            ad hoc group opens the joining dialog, the system's dialog asked
#            for, joined (HALO_ADHOC_TEST=join), then the profile and the LAN
#            browser (AD HOC GAMES, "No games found near you"); START GAME with
#            the join failing (HALO_ADHOC_TEST=fail) says so, A asks again, B
#            goes back to the lobby; with the connection Same Wi-Fi, NETWORK:
#            AD HOC switches to ad hoc play live (the system's dialog asked
#            for), BROWSE GAMES opens the ad hoc LAN browser, and SYSTEM LINK
#            leaves ad hoc play again
#   h3adhoccoop  AD HOC CO-OP from the Halo 3 campaign lobby (HALO_TEST_PROFILE;
#            skipped without), the connection Ad hoc and the join answered
#            (HALO_ADHOC_TEST=join): START GAME, the joining dialog, then the
#            level hosted as co-op and the game's lobby open by themselves; with
#            the connection Same Wi-Fi AD HOC CO-OP switches to ad hoc play live
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
cases=${*:-fresh level xbox pause h3probe h3classic h3xbox h3broken h3style h3online h3touch h3campaign h3resume h3pause h3mp h3browser h3create h3mptouch h3coop h3coopguide h3scenes h3profiles h3proftouch h3customize h3models h3nameplate h3damaged h3mods h3modspicker h3modstouch h3mpcustom h3lan h3groups h3adhoc h3adhoccoop h3oddrows h3lobbyscenes h3modelsmany h3friends h3friendspanel h3party browserprofile h3players browserrows profilepaths}
ce_maps=${HALO_TEST_CE_MAPS:-$root/../custom-maps-dl}
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
h3net="$h3net HALO_NET_RELAYS=198.51.100.1:47320 HALO_NET_ALLOW_UPNP=false HALO_DISCORD_APPLICATION="
# (FRIENDS on the main menu: down six, A)
h3friendspad="wait:150:3000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000"
# a save folder's friends (run_p2p_friends_test.sh seed: made at test time, never in the repository)
friends_seed() { # FOLDER
	rm -rf "$1"
	TMPDIR="$out" sh "$here/run_p2p_friends_test.sh" seed "$1" > "$out/friends_seed.log" 2>&1 ||
		{ fail friends "the friends could not be made (friends_seed.log)"; cat "$out/friends_seed.log"; }
}
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
		# (NETWORK: the picker on SYSTEM LINK, up to ONLINE, A: online, live (its brokers TEST-NET
		# addresses: connecting); NETWORK again, down to SYSTEM LINK, A: internet play stopped;
		# down three to START GAME, A: the profile screen, A to join, the profile chosen: the map
		# list)
		run h3mp "$menus_data" 60 "$h3mppad a wait:150:3000 up:150:600 a wait:150:5000 a wait:150:3000 down:150:600 a wait:150:3000 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:5000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_NET_ONLINE=false
		check_h3 h3mp halo3
		log=$out/h3mp/run.log
		grep -aq "menus: Halo 3 screen h3/mp/screen for ui.shell.main_menu.multiplayer_type_select.multiplayer_type_select_screen" "$log" ||
			fail h3mp "the Halo 3 lobby did not stand for the Multiplayer screen"
		grep -aq "menus: h3 mp lobby: NETWORK: SYSTEM LINK, GAME: SLAYER, MAP: BATTLE CREEK" "$log" ||
			fail h3mp "the lobby's rows were not SYSTEM LINK, SLAYER, BATTLE CREEK"
		grep -aq "ui: screen pc.h3.network.screen" "$log" || fail h3mp "NETWORK did not open its picker"
		grep -aq "network: switching from Same Wi-Fi to Online" "$log" && grep -aq "Internet play: network thread started" "$log" ||
			fail h3mp "ONLINE did not start internet play, live"
		grep -aq 'network: status "Connecting..."' "$log" || fail h3mp "the status under NETWORK did not say Connecting..."
		grep -aq "menus: h3 mp lobby: NETWORK: ONLINE" "$log" || fail h3mp "the lobby did not show ONLINE"
		grep -aq "network: switching from Online to Same Wi-Fi" "$log" &&
			grep -aq "Internet play: stopped (its thread, sockets and peers gone)" "$log" ||
			fail h3mp "SYSTEM LINK did not stop internet play"
		grep -aq "ui: screen pc.h3.mp.offline" "$log" && fail h3mp "ONLINE said internet play is off"
		grep -aq "menus: h3 mp: host a system link game" "$log" || fail h3mp "START GAME did not host a system link game"
		grep -aq "menus: Multiplayer > System Link: the game's server started; the map list" "$log" ||
			fail h3mp "the server did not start (or the log named another screen)"
		grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.connected.connected_map_select_wrapper" "$log" ||
			fail h3mp "the profile did not lead to the map list"
		grep -aq "ui: screen pc.mp.screen" "$log" && fail h3mp "OpenCE's Multiplayer screen opened with Menu style Halo 3" ;;
	h3browser)
		# (down five to BROWSE GAMES, A: the profile (A joins, A the profile, A): the games
		# browser; A on the first game - the fullest Vita game, locked -: its password; A: typed,
		# down, A: JOIN GAME)
		run h3browser "$menus_data" 60 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000 wait:150:600 a wait:150:4000 a wait:150:2000 down:150:600 a wait:150:4000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_TEST_BROWSER_GAMES=16 HALO_TEST_BROWSER_PC=1 HALO_TEST_TEXT_INPUT=secret
		check_h3 h3browser halo3
		log=$out/h3browser/run.log
		grep -aq "menus: h3 mp lobby: NETWORK: ONLINE" "$log" || fail h3browser "NETWORK was not ONLINE with internet play on"
		grep -aq "ui: screen pc.h3.lan.screen" "$log" && grep -aq "menus: the LAN browser (system link and online)" "$log" ||
			fail h3browser "BROWSE GAMES did not open the Halo 3 games browser with the online games"
		grep -aq "menus: the server browser's game 1 of 16 chosen: Test game 02" "$log" ||
			fail h3browser "the browser did not list the 16 games, the fullest Vita game first"
		grep -aq "menus: Halo 3 screen h3/password/screen for pc.join.password.screen" "$log" ||
			fail h3browser "the locked game did not open the Halo 3 password screen"
		grep -aq "menus: the public game Test game 02 is no longer listed" "$log" ||
			fail h3browser "JOIN GAME did not try the game with its password"
		grep -aq "ui: screen pc.browser.screen\|ui: screen pc.join.password" "$log" &&
			fail h3browser "OpenCE's browser or password screen opened with Menu style Halo 3" ;;
	h3create)
		# (down three to START GAME, A: Create Game; GAME TYPE, A: the picker, down, A: SLAYER PRO;
		# down two to NAME, A: typed; down five to START GAME (past WHO CAN JOIN), A)
		run h3create "$menus_data" 50 "$h3mppad down:150:500 down:150:500 down:150:500 a wait:150:4000 a wait:150:3000 down:150:600 a wait:150:4000 down:150:500 down:150:500 a wait:150:2000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:5000" \
			HALO_MENU_STYLE=halo3 $h3net "HALO_TEST_TEXT_INPUT=Vita Night"
		check_h3 h3create halo3
		log=$out/h3create/run.log
		grep -aq "ui: screen pc.h3.setup.screen" "$log" || fail h3create "START GAME did not open Create Game"
		grep -aq "menus: h3 mp: game type Slayer Pro" "$log" || fail h3create "the GAME TYPE picker did not set the game type"
		grep -aq "menus: Server Setup: Vita Night, 16 players, public, Vitas; the profile, then the map list" "$log" ||
			fail h3create "START GAME did not set up the game as Server Setup does"
		grep -aqE "ui: screen (ui.shell.main_menu.multiplayer_type_select.connected.4way_profile_select.4way_start2join_screen|pc.h3.join.screen)" "$log" ||
			fail h3create "START GAME did not open the profile" ;;
	h3groups)
		# (the owner, Oct 19 2026: the Vitas' games first, then the PCs', each under its header;
		# the D-pad's right and left: Vita games, PC games, all games, kept as the setting.
		# Halo 3's games browser on SYSTEM LINK (down five to BROWSE GAMES, A: the profile, A A A),
		# 14 made-up games, half the PCs'; then OpenCE's browser (Menu style Halo CE) online. Each
		# at the Vita's picture, its frames kept when HALO_TEST_SHOTS is set)
		groups_pad="right:150:4000 right:150:4000 right:150:4000 left:150:4000 left:150:4000 wait:150:3000"
		run h3groups "$menus_data" 60 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000 down:150:800 $groups_pad" \
			HALO_MENU_STYLE=halo3 HALO_TEST_BROWSER_GAMES=14 HALO_TEST_BROWSER_PC=1 HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1
		run h3groups_classic "$menus_data" 50 "wait:150:2000 down:150:800 a wait:150:4000 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:4000 $groups_pad" \
			HALO_MENU_STYLE=classic $h3net HALO_TEST_BROWSER_GAMES=14 HALO_TEST_BROWSER_PC=1 HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1
		check_h3 h3groups halo3
		for name in h3groups h3groups_classic; do
			log=$out/$name/run.log
			echo "--- $name"; grep -aE "menus: the browser's groups|menus: the server browser (shows|'s game)" "$log" | head -16
			[ "$(cat "$out/$name/exit")" = 0 ] || fail $name "exit $(cat "$out/$name/exit")"
			# (the filters in turn: all, Vita, PC, all, then back: PC, Vita; all games grouped
			# VITA GAMES then PC GAMES, each filter's its own group alone. Counts as the
			# made-up games', or more: the System Link list may show another's game on this
			# machine's network)
			order=$(grep -a "menus: the browser's groups" "$log" | sed 's/.*groups (\([^)]*\)).*/\1/' | uniq | tr '\n' ';')
			[ "$order" = "All games;Vita games;PC games;All games;PC games;Vita games;" ] ||
				fail $name "the filters were not all, Vita, PC, all, PC, Vita: $order"
			grep -a "menus: the browser's groups (All games)" "$log" |
				grep -avqE "\): VITA GAMES \(([7-9]|[1-9][0-9])\), PC GAMES \(([7-9]|[1-9][0-9])\)$" &&
				fail $name "all games were not grouped VITA GAMES, then PC GAMES"
			grep -a "menus: the browser's groups (Vita games)" "$log" | grep -aq "PC GAMES" &&
				fail $name "Vita games showed a PC game"
			grep -a "menus: the browser's groups (PC games)" "$log" | grep -aq "VITA GAMES" &&
				fail $name "PC games showed a Vita game"
			grep -aq "menus: the server browser shows the Vitas' games" "$log" && grep -aq "menus: the server browser shows the PCs' games" "$log" &&
				grep -aq "menus: the server browser shows every game" "$log" || fail $name "the D-pad did not step the filter"
			# (the first game chosen: the fullest Vita game; down once, the next Vita game; with
			# PC games alone, the fullest PC game; never a header)
			grep -aqE "menus: the server browser's game 1 of 1[4-9] chosen: Test game 02" "$log" ||
				fail $name "the first game was not the fullest Vita game"
			grep -aqE "menus: the server browser's game 1 of ([7-9]|1[0-9]) chosen: Test game 09" "$log" ||
				fail $name "PC games alone: the first game was not the fullest PC game"
			grep -aq "chosen: VITA GAMES\|chosen: PC GAMES" "$log" && fail $name "a header row was chosen"
		done
		grep -aqE "menus: the server browser's game 2 of 1[4-9] chosen: Test game 04" "$out/h3groups/run.log" ||
			fail h3groups "down from the first game did not choose the next Vita game"
		if [ -n "${HALO_TEST_SHOTS:-}" ]; then echo "frames in $HALO_TEST_SHOTS/h3groups*"; fi ;;
	h3mptouch)
		# (MULTIPLAYER on the main menu; the lobby's MAP; BLOOD GULCH in the picker; the lobby's Back)
		run h3mptouch "$menus_data" 30 "wait:150:20000" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_TAPS=4000:60,298 8000:60,118 12000:100,263 16000:140,455"
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
		# down, A: ONLINE CO-OP, online live (its brokers TEST-NET addresses: connecting); NETWORK
		# again, down, A: SYSTEM LINK CO-OP, internet play stopped; down three to START GAME, A:
		# hosted, the game's lobby)
		profile_from=$HALO_TEST_PROFILE run h3coop "$menus_data" 55 "wait:150:3000 a wait:150:3000 a wait:150:3000 up:150:600 a wait:150:2500 down:150:600 a wait:150:4000 a wait:150:2500 down:150:600 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:8000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_NET_ONLINE=false
		check_h3 h3coop halo3
		log=$out/h3coop/run.log
		grep -aq "ui: screen pc.h3.campnet.screen" "$log" || fail h3coop "NETWORK did not open its picker"
		grep -aq "menus: h3 campaign: network ONLINE CO-OP" "$log" &&
			grep -aq "network: switching from Same Wi-Fi to Online" "$log" ||
			fail h3coop "ONLINE CO-OP did not go online, live"
		grep -aq "network: switching from Online to Same Wi-Fi" "$log" || fail h3coop "SYSTEM LINK CO-OP did not leave online"
		grep -aq "menus: h3 campaign: network SYSTEM LINK CO-OP" "$log" || fail h3coop "SYSTEM LINK CO-OP was not chosen"
		grep -aq "menus: h3 campaign caption: .* on Normal Co-op at Mission Start" "$log" ||
			fail h3coop "the caption did not say co-op"
		grep -aq "menus: h3 campaign: host co-op levels.*on Normal (system link)" "$log" ||
			fail h3coop "START GAME did not host co-op"
		grep -aq "co-op: hosting levels.* on difficulty 1 from the campaign's menus" "$log" ||
			fail h3coop "the level was not hosted as co-op"
		grep -aqE "ui: screen (ui.shell.main_menu.multiplayer_type_select.connected.pregame.connected_pregame_screen|pc.h3.pregame.screen)" "$log" ||
			fail h3coop "the game's lobby (the waiting screen) did not open"
		grep -aq "menus: h3 campaign: co-op was not hosted" "$log" && fail h3coop "co-op was not hosted"
		grep -a "ui: screen" "$log" | tail -1 | grep -qE 'connected_pregame_screen|pc.h3.pregame.screen' ||
			fail h3coop "the game's lobby did not stay open (the co-op game ended)" ;;
	h3players)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3players"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (CAMPAIGN, the profile: the lobby; up to NETWORK, A, down twice: SYSTEM LINK CO-OP; down three
		# to START GAME, A: hosted, the game's lobby; Triangle: PLAYERS, this machine's player and three
		# made up (HALO_TEST_PLAYERS_FAKE); down to the first other one, Cross: its options, Back
		# focused; down, Cross: ADD FRIEND, friends not online (online off): go online first; Cross on
		# this machine's own player: no options; Circle: the game's lobby again)
		players_pad="wait:150:3000 a wait:150:3000 a wait:150:3000 up:150:600 a wait:150:2500 down:150:600 down:150:600 a wait:150:2500"
		players_pad="$players_pad down:150:600 down:150:600 down:150:600 a wait:150:8000 y wait:150:2500 @00-players:0:300"
		players_pad="$players_pad down:150:700 a wait:150:2000 @01-options:0:300 down:150:700 a wait:150:2000 @02-added:0:300"
		players_pad="$players_pad up:150:700 a wait:150:2000 b wait:150:3000"
		# (co-op's most players its own, network.coop_players: 4 here; the lobby's PLAYERS says it)
		profile_from=$HALO_TEST_PROFILE run h3players "$menus_data" 60 "$players_pad" \
			HALO_MENU_STYLE=halo3 HALO_TEST_PLAYERS_FAKE=3 HALO_NET_COOP_PLAYERS=4
		check_h3 h3players halo3
		log=$out/h3players/run.log
		grep -a "menus: h3 players\|PLAYERS screen" "$log" | head -12
		grep -aqE "ui: screen (ui.shell.main_menu.multiplayer_type_select.connected.pregame.connected_pregame_screen|pc.h3.pregame.screen)" "$log" ||
			fail h3players "the game's lobby (the waiting screen) did not open"
		grep -aq "menus: the PLAYERS screen opened (the game's lobby)" "$log" &&
			grep -aq "ui: screen pc.h3.players.screen" "$log" || fail h3players "Triangle in the game's lobby did not open PLAYERS"
		grep -aq "menus: h3 players: 4 players, this machine 0" "$log" || fail h3players "PLAYERS did not list the four players"
		grep -aqE "menus: h3 pregame: .*; players: [^;]*; 1/4$" "$log" ||
			fail h3players "the co-op lobby's PLAYERS did not say 1/4 (network.coop_players)"
		grep -aq "menus: h3 players: options for Spartan 1 (machine 10)" "$log" &&
			grep -aq "ui: screen pc.h3.players.options" "$log" || fail h3players "Cross on a player did not open its options"
		grep -aq "menus: h3 players: Go online first" "$log" || fail h3players "ADD FRIEND offline did not say go online first"
		grep -aq "menus: h3 players: A player of this PS Vita" "$log" ||
			fail h3players "this machine's own player was given options"
		grep -a "ui: screen" "$log" | tail -1 | grep -qE 'connected_pregame_screen|pc.h3.pregame.screen' ||
			fail h3players "Circle did not go back to the game's lobby" ;;
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
		for scene in "h3/campaign/screen: camera point load_campaign at -15.00 5.00 0.00, yaw -20.0 pitch 25.0, over 0 ticks, field of view 50" \
			"h3/mp/screen: camera point multiplayer at -40.00 -1.80 15.50, yaw 0.0 pitch 0.0, over 0 ticks, field of view 55" \
			"h3/forge/lobby: camera point new_campaign at -10.00 -20.00 5.00, yaw 40.0 pitch 20.0, over 0 ticks, field of view 40"; do
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
	h3lobbyscenes)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3lobbyscenes"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (as h3scenes: CAMPAIGN, the profile: the campaign lobby; B, B; down, A: MULTIPLAYER; B;
		# down, A: FORGE; B - each lobby long enough for its packs' first build)
		lobby_pad="wait:150:3000 a wait:150:3000 a wait:150:6000 b wait:150:2500 b wait:150:3000 down:150:700 a wait:150:6000 b wait:150:3000 down:150:700 a wait:150:8000 b wait:150:3000"
		profile_from=$HALO_TEST_PROFILE run h3lobbyscenes "$menus_data" 60 "$lobby_pad" HALO_MENU_STYLE=halo3
		# (the multiplayer lobby, then the preview: in colour 5, recoloured 9, hidden)
		run h3lobbyscenes_preview "$menus_data" 26 "wait:150:3000 down:150:700 a wait:150:15000" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_COMMANDS=330:pack preview 5;420:pack preview 9;510:pack preview off"
		profile_from=$HALO_TEST_PROFILE run h3lobbyscenes_off "$menus_data" 60 "$lobby_pad" HALO_MENU_STYLE=halo3 HALO_SCENE_PACKS=0
		profile_from=$HALO_TEST_PROFILE run h3lobbyscenes_classic "$menus_data" 30 "wait:150:3000 a wait:150:6000 b wait:150:3000" HALO_MENU_STYLE=classic
		# (a maps folder of ui.map, the Halo PC files and Blood Gulch: no campaign map; and of ui.map and
		# the Halo PC files alone: no map to build any pack from)
		for folder in nocampaign nomap; do
			rm -rf "$out/${folder}_data"
			mkdir -p "$out/${folder}_data/maps"
			maps="ui bitmaps loc"
			[ $folder = nocampaign ] && maps="$maps bloodgulch"
			for map in $maps; do ln -s "$(readlink -f "$menus_data/maps/$map.map")" "$out/${folder}_data/maps/$map.map"; done
		done
		profile_from=$HALO_TEST_PROFILE run h3lobbyscenes_nocampaign "$out/nocampaign_data" 60 "$lobby_pad" HALO_MENU_STYLE=halo3
		profile_from=$HALO_TEST_PROFILE run h3lobbyscenes_nomap "$out/nomap_data" 60 "$lobby_pad" HALO_MENU_STYLE=halo3
		check_h3 h3lobbyscenes halo3
		log=$out/h3lobbyscenes/run.log
		grep -aE "scene packs: menu_|h3 scene object" "$log"
		for pack in "menu_campaign: built from a30" "menu_campaign: loaded from a30" "menu_multiplayer: built from bloodgulch" \
			"menu_multiplayer: loaded from bloodgulch" "menu_monitor: built from c20" "menu_monitor: loaded from c20"; do
			grep -aq "scene packs: $pack" "$log" || fail h3lobbyscenes "no \"$pack\""
		done
		for made in "objects: h3/campaign/screen: made" "objects: h3/mp/screen: made" "objects: h3/forge/lobby: made" \
			"object 0: vehicles.warthog.warthog (menu_campaign) at .* facing 268 pitch 22" \
			"object 1: characters.cyborg.cyborg seated as W-driver" \
			"object 2: characters.marine_armored.marine_armored seated as W-gunner" \
			"object 3: characters.marine_armored.marine_armored seated as W-passenger" \
			"object 0: weapons.rocket launcher.rocket launcher in its hands" \
			"object 0: characters.cyborg_mp.cyborg_mp (menu_multiplayer) at .* colour 0: made" \
			"object 0: animation stand missile airborne, held" \
			"object 1: weapons.flag.flag in its hands" \
			"object 2: weapons.sniper rifle.sniper rifle in its hands" \
			"object 2: animation crouch rifle idle, looped" \
			"object 1: characters.monitor.monitor (menu_monitor) at .* made"; do
			grep -aq "h3 scene $made" "$log" || fail h3lobbyscenes "no \"$made\""
		done
		[ "$(grep -ac 'h3 scene objects: let go after .* (at most 0 effects and 0 particles meanwhile)' "$log")" -ge 3 ] ||
			fail h3lobbyscenes "the three scenes were not let go each, or an effect or particle was made (dust)"
		grep -aE "h3 scene objects: let go" "$log"
		grep -aqE "h3 scene object.*(could not|no seat|has no|no animation)" "$log" &&
			fail h3lobbyscenes "a scene's object, weapon, seat or animation was missing"
		[ "$(grep -ac 'ui: screen pc.h3.campaign.screen\|ui: screen pc.h3.mp.screen\|ui: screen pc.h3.forge.lobby' "$log")" -ge 3 ] ||
			fail h3lobbyscenes "the three lobbies did not open"
		check_h3 h3lobbyscenes_preview halo3
		log=$out/h3lobbyscenes_preview/run.log
		grep -aE "h3 scene object|preview" "$log"
		grep -aq "h3 scene object 6: characters.cyborg_mp.cyborg_mp (menu_spartan) at .* colour 5: made" "$log" ||
			fail h3lobbyscenes "the preview did not show a Spartan in colour 5"
		grep -aq "h3 scene object: recoloured 9" "$log" || fail h3lobbyscenes "the preview was not recoloured"
		grep -aq "h3 scene object 6: deleted" "$log" || fail h3lobbyscenes "the preview was not hidden"
		check_h3 h3lobbyscenes_off halo3
		grep -aqE "scene packs: |h3 scene object.* made" "$out/h3lobbyscenes_off/run.log" &&
			fail h3lobbyscenes "HALO_SCENE_PACKS=0: a pack or a scene all the same"
		[ "$(grep -ac 'ui: screen pc.h3.campaign.screen\|ui: screen pc.h3.mp.screen\|ui: screen pc.h3.forge.lobby' "$out/h3lobbyscenes_off/run.log")" -ge 3 ] ||
			fail h3lobbyscenes "HALO_SCENE_PACKS=0: the three lobbies did not open"
		check_h3 h3lobbyscenes_classic classic
		grep -aqE "scene packs: menu_|h3 scene object" "$out/h3lobbyscenes_classic/run.log" &&
			fail h3lobbyscenes "Menu style Halo CE: a scene all the same"
		check_h3 h3lobbyscenes_nocampaign halo3
		log=$out/h3lobbyscenes_nocampaign/run.log
		grep -aE "scene packs: menu_|h3 scene objects" "$log"
		grep -aq "scene packs: menu_campaign: not built: none of its maps" "$log" ||
			fail h3lobbyscenes "no campaign map: the build did not say why it failed"
		grep -aq "h3 scene objects: h3/campaign/screen: the pack menu_campaign is failed: no scene" "$log" ||
			fail h3lobbyscenes "no campaign map: the campaign lobby did not go without its scene"
		grep -aq "h3 scene objects: h3/forge/lobby: the pack menu_.* is failed: no scene" "$log" ||
			fail h3lobbyscenes "no campaign map: the Forge lobby did not go without its scene"
		grep -aq "h3 scene objects: h3/mp/screen: made" "$log" ||
			fail h3lobbyscenes "no campaign map: the multiplayer lobby did not have its scene"
		grep -aq "h3 scene objects: h3/\(campaign/screen\|forge/lobby\): made" "$log" &&
			fail h3lobbyscenes "no campaign map: a campaign scene all the same"
		check_h3 h3lobbyscenes_nomap halo3
		log=$out/h3lobbyscenes_nomap/run.log
		grep -aq "h3 scene objects: .*: made" "$log" && fail h3lobbyscenes "no map: a scene all the same"
		[ "$(grep -ac 'h3 scene objects: .*: the pack menu_.* is failed: no scene' "$log")" -ge 3 ] ||
			fail h3lobbyscenes "no map: the three lobbies did not each go without their scene"
		[ "$(grep -ac 'ui: screen pc.h3.campaign.screen\|ui: screen pc.h3.mp.screen\|ui: screen pc.h3.forge.lobby' "$log")" -ge 3 ] ||
			fail h3lobbyscenes "no map: the three lobbies did not open" ;;
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
		# (the card's Spartan: none for CREATE NEW PROFILE, Rookie's in its colour once listed, let go
		# with the screen)
		grep -aE "menus: h3 profiles: the card|h3 scene object 6" "$log"
		grep -aq "menus: h3 profiles: the card's Spartan asked for in colour" "$log" &&
			grep -aq "menus: h3 profiles: the card shows the Spartan" "$log" ||
			fail h3profiles "the card did not show Rookie's Spartan"
		grep -aq "menus: h3 profiles: the card's Spartan hidden" "$log" ||
			fail h3profiles "the card's Spartan was not let go (CREATE NEW PROFILE, or the screen gone)"
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
		# past MODEL; STICK LAYOUT, right: SOUTHPAW; down two to LOOK SENSITIVITY, right twice; INVERT LOOK, Cross:
		# YES; Circle: saved; Circle)
		custom_pad="$h3pad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:3000 a wait:150:2000"
		custom_pad="$custom_pad down:150:600 right:150:500 right:150:500 right:150:500 down:150:600 down:150:600 right:150:600"
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
		# (CUSTOMIZATION's profile list: the card shows the focused profile's Spartan, as SELECT
		# PROFILE's, before the editor's own preview)
		grep -aq "menus: h3 profiles: [0-9]* profiles* (customization)" "$log" &&
			grep -aq "menus: h3 profiles: the card's Spartan asked for in colour" "$log" &&
			grep -aq "menus: h3 profiles: the card shows the Spartan" "$log" ||
			fail h3customize "CUSTOMIZATION's profile list did not show the profile's Spartan"
		# (the preview: the scene packs' Spartan in the editor's window, in the profile's colour - its
		# RANDOM shown white -, made again in RED as COLOR changes, let go with the editor)
		grep -aE "h3 customize: .*preview|h3 scene object 6|h3 scene: h3/custom" "$log"
		grep -aq "menus: h3 scene: h3/custom/screen: camera point multiplayer" "$log" ||
			fail h3customize "the editor's own camera was not set"
		grep -aq "menus: h3 customize: preview Spartan asked for in colour 0" "$log" ||
			fail h3customize "the preview's Spartan was not asked for"
		grep -aq "menus: h3 scene object 6: characters.cyborg_mp.cyborg_mp (menu_spartan) at .* colour 0: made" "$log" &&
			grep -aq "menus: h3 customize: the preview shows the Spartan" "$log" ||
			fail h3customize "the preview did not show the Spartan"
		grep -aq "menus: h3 customize: preview Spartan recoloured 2" "$log" &&
			grep -aq "menus: h3 scene object 6: characters.cyborg_mp.cyborg_mp (menu_spartan) at .* colour 2: made" "$log" ||
			fail h3customize "the preview was not recoloured RED"
		grep -aq "menus: h3 customize: the preview shows the swatch" "$log" &&
			fail h3customize "the preview fell back to the swatch while it was recoloured"
		grep -aq "menus: h3 customize: preview Spartan hidden" "$log" && grep -aq "menus: h3 scene object 6: deleted" "$log" ||
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
		grep -aqE "menus: h3 customize: the preview shows the Spartan|h3 scene object 6: .* made" "$out/h3customize_again/run.log" &&
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
	h3models)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ] || [ ! -r "$ce_maps/extinction.map" ] || [ ! -r "$menus_data/maps/sounds.map" ]; then
			echo "--- h3models"; echo "SKIP: no HALO_TEST_PROFILE, extinction.map (HALO_TEST_CE_MAPS) or sounds.map"; continue
		fi
		# (a maps folder of the data's and the Custom Edition maps, links)
		rm -rf "$out/models_data"
		mkdir -p "$out/models_data/maps"
		for file in "$(cd "$menus_data" && pwd)"/maps/*.map; do ln -sfn "$file" "$out/models_data/maps/"; done
		for file in extinction Covenant_V_Marines_Beta_5; do
			[ -r "$ce_maps/$file.map" ] && ln -sfn "$(cd "$ce_maps" && pwd)/$file.map" "$out/models_data/maps/$file.map"
		done
		# (CUSTOMIZATION: the profile, Cross; down two to MODEL; right: the Extinction Spartan, built and
		# shown; right: the protected map's; left: the Spartan (Extinction) again; Circle; Circle)
		models_pad="$h3pad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:3000"
		models_pad="$models_pad down:150:600 down:150:600 right:150:500 wait:150:8000 right:150:500 wait:150:8000"
		models_pad="$models_pad left:150:500 wait:150:4000 b wait:150:3000 b wait:150:3000"
		profile_from=$HALO_TEST_PROFILE run h3models "$out/models_data" 55 "$models_pad" HALO_MENU_STYLE=halo3
		check_h3 h3models halo3
		log=$out/h3models/run.log
		grep -aq "menus: h3 customize: 2 player models in the maps folder's Custom Edition maps; this profile's: -1" "$log" ||
			fail h3models "the editor did not find the two models"
		grep -aq "menus: h3 customize: model SPARTAN (EXTINCTION)" "$log" || fail h3models "MODEL did not step to the Extinction Spartan"
		grep -aq "scene packs: pm_[0-9a-f]*: built from extinction" "$log" || fail h3models "its pack was not built"
		grep -aq "menus: h3 scene object 6: characters.spartan.spartan_mp (pm_[0-9a-f]*) at .* made" "$log" &&
			grep -aq "menus: h3 customize: model Spartan (Extinction): loaded" "$log" ||
			fail h3models "the preview did not show the Extinction Spartan"
		grep -aq "menus: h3 scene object 6: protected.Covenant_V_Marines_Beta_5.bipd [0-9]* (pm_[0-9a-f]*) at .* made" "$log" ||
			fail h3models "the protected map's model was not shown"
		grep -aq "player models: 869079979327 chose Spartan (Extinction)" "$log" &&
			grep -aq "menus: h3 customize: birch: model 0 saved" "$log" || fail h3models "Circle did not keep the choice"
		grep -q "^869079979327 extinction 97888771 c5372723$" "$out/h3models/save/t/player_models.txt" ||
			fail h3models "t:\\player_models.txt does not hold the choice"
		cmp -s "$HALO_TEST_PROFILE" "$out/h3models/save/u/UDATA/869079979327/blam.sav" ||
			fail h3models "blam.sav changed (the model is kept apart from it)"
		rm -rf "$out/h3models_saved"
		cp -r "$out/h3models/save" "$out/h3models_saved"
		save_from=$out/h3models_saved run h3models_again "$out/models_data" 25 \
			"$h3pad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:5000 b wait:150:3000" \
			HALO_MENU_STYLE=halo3
		grep -aq "menus: h3 customize: 2 player models in the maps folder's Custom Edition maps; this profile's: 0" \
			"$out/h3models_again/run.log" || fail h3models "started again, the editor did not show the choice"
		;;
	h3modelsmany)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ] || [ ! -r "$ce_maps/extinction.map" ] || [ ! -r "$menus_data/maps/sounds.map" ]; then
			echo "--- h3modelsmany"; echo "SKIP: no HALO_TEST_PROFILE, extinction.map (HALO_TEST_CE_MAPS) or sounds.map"; continue
		fi
		# (a maps folder of the data's and fourteen links to extinction.map under names of their own:
		# fourteen models, each a pack of its own)
		rm -rf "$out/manymodels_data"
		mkdir -p "$out/manymodels_data/maps"
		for file in "$(cd "$menus_data" && pwd)"/maps/*.map; do ln -sfn "$file" "$out/manymodels_data/maps/"; done
		for index in 01 02 03 04 05 06 07 08 09 10 11 12 13 14; do
			ln -sfn "$(cd "$ce_maps" && pwd)/extinction.map" "$out/manymodels_data/maps/model$index.map"
		done
		# (CUSTOMIZATION: the profile, Cross; down two to MODEL; right fourteen times, each model left to
		# rest and show; then right eight times quickly, and a rest)
		many_pad="$h3pad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:3000"
		many_pad="$many_pad down:150:600 down:150:600"
		for index in 01 02 03 04 05 06 07 08 09 10 11 12 13 14; do many_pad="$many_pad right:150:3500"; done
		many_pad="$many_pad left:80:80 left:80:80 left:80:80 left:80:80 left:80:80 left:80:80 left:80:80 left:80:4000 b wait:150:3000"
		profile_from=$HALO_TEST_PROFILE run h3modelsmany "$out/manymodels_data" 85 "$many_pad" HALO_MENU_STYLE=halo3
		check_h3 h3modelsmany halo3
		log=$out/h3modelsmany/run.log
		grep -aE "scene packs: pm_.*(loaded|let go|refused|waits)|h3 customize: model .*: (loaded|failed)" "$log" | cut -c1-160
		[ "$(grep -a 'menus: h3 scene object 6: characters.spartan.spartan_mp (pm_[0-9a-f]*) at .* made' "$log" |
			sed 's/.*(\(pm_[0-9a-f]*\)).*/\1/' | sort -u | wc -l)" -ge 14 ] ||
			fail h3modelsmany "the fourteen models were not each shown"
		grep -aqE "scene packs: pm_.*: (refused|waits)|h3 customize: model .*: failed" "$log" &&
			fail h3modelsmany "a model's pack was refused, or waited for room (the packs passed were kept)"
		[ "$(grep -ac 'scene packs: pm_[0-9a-f]*: let go' "$log")" -ge 13 ] ||
			fail h3modelsmany "the models passed were not let go"
		# (the quick presses, back from the fourteenth to the sixth: none of the seven passed loaded again)
		for index in 07 08 09 10 11 12 13; do
			[ "$(grep -ac "scene packs: pm_[0-9a-f]*: loaded from model$index " "$log")" = 1 ] ||
				fail h3modelsmany "model$index, passed quickly, was loaded again"
		done ;;
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
		# (the Halo 3 lobby: down five to BROWSE GAMES, A: the profile (A joins, A the profile, A):
		# the games browser; OpenCE's: Multiplayer, SERVER BROWSER, the profile first (A A A))
		run h3oddrows "$menus_data" 40 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:8000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_TEST_BROWSER_ODD=1
		run h3oddrows_classic "$menus_data" 45 "wait:150:2000 down:150:800 a wait:150:4000 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:8000" \
			HALO_MENU_STYLE=classic $h3net HALO_TEST_BROWSER_ODD=1
		check_h3 h3oddrows halo3
		for name in h3oddrows h3oddrows_classic; do
			log=$out/$name/run.log
			coop="Co-op · Legendary"
			[ $name = h3oddrows_classic ] && coop="Co-op (Legendary)"
			for row in "Spartan | Slayer | Blood Gulch | 8/128" "Jos?? ??? Ma??ana | CTF | Chill Out | 3/16" \
				"true legendary run (cut) | $coop | The Pillar of Autumn (cut) | 3/4" "teto | Co-op | - | 1/4" \
				"BIG TEAM 24*7 | - | - | -" "HALO UNIVERSAL CUSTOM SERVER 24/ (cut) | Team Slayer | bigassv2,104 | 24/32"; do
				grep -aqF "$row" "$log" || fail h3oddrows "$name: no row \"$row\""
			done
			grep -aE "menus: browser row [0-9]+: " "$log" | grep -aqE "row [0-9]+:  ?\|" && fail h3oddrows "$name: a row without a name"
		done
		grep -aqF "xXxNoScopeLegendsxXxForeverMore (cut)" "$out/h3oddrows/run.log" ||
			fail h3oddrows "the long single-word name was not cut to its column" ;;
	profilepaths)
		# (every multiplayer entry path asks for the profile before its games, map list or lobby:
		# Menu style Halo CE's Xbox profile screen, Halo 3's SELECT PROFILE; the owner, insider.12)
		d="down:150:500"
		cmp="wait:150:3000 down:150:800 a wait:150:3000"
		run pp_c_browser "$menus_data" 22 "$cmp a wait:150:4000" HALO_MENU_STYLE=classic $h3net
		run pp_c_lan "$menus_data" 22 "$cmp $d a wait:150:4000" HALO_MENU_STYLE=classic HALO_NET_ONLINE=false
		run pp_c_code "$menus_data" 26 "$cmp $d $d a wait:150:3000 a wait:150:2000 $d a wait:150:5000" HALO_MENU_STYLE=classic \
			$h3net HALO_TEST_TEXT_INPUT=ABCD-EFGH
		run pp_c_create_lan "$menus_data" 22 "$cmp $d $d $d $d a wait:150:4000" HALO_MENU_STYLE=classic HALO_NET_ONLINE=false
		run pp_h_start_link "$menus_data" 22 "$h3mppad $d $d $d a wait:150:4000" HALO_MENU_STYLE=halo3 HALO_NET_ONLINE=false
		run pp_h_browse_link "$menus_data" 22 "$h3mppad $d $d $d $d $d a wait:150:4000" HALO_MENU_STYLE=halo3 HALO_NET_ONLINE=false
		run pp_h_start_online "$menus_data" 30 "$h3mppad $d $d $d a wait:150:4000 $d $d $d $d $d $d $d a wait:150:5000" \
			HALO_MENU_STYLE=halo3 $h3net "HALO_TEST_TEXT_INPUT=Vita Night"
		run pp_h_browse_online "$menus_data" 22 "$h3mppad $d $d $d $d $d a wait:150:4000" HALO_MENU_STYLE=halo3 $h3net
		run pp_h_code_online "$menus_data" 28 "$h3mppad $d $d $d $d $d $d a wait:150:3000 a wait:150:2000 $d a wait:150:5000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_TEST_TEXT_INPUT=ABCD-EFGH
		run pp_h_start_adhoc "$menus_data" 28 "$h3mppad $d $d $d a wait:150:8000" HALO_MENU_STYLE=halo3 HALO_NET_ADHOC=true \
			HALO_ADHOC_TEST=join
		run pp_h_browse_adhoc "$menus_data" 28 "$h3mppad $d $d $d $d $d a wait:150:8000" HALO_MENU_STYLE=halo3 HALO_NET_ADHOC=true \
			HALO_ADHOC_TEST=join
		echo "--- profilepaths"
		for name in pp_c_browser pp_c_lan pp_c_code pp_c_create_lan pp_h_start_link pp_h_browse_link pp_h_start_online \
			pp_h_browse_online pp_h_code_online pp_h_start_adhoc pp_h_browse_adhoc; do
			log=$out/$name/run.log
			case $name in pp_c_*) want='ui: screen ui.shell.main_menu.multiplayer_type_select.connected.4way_profile_select.4way_start2join_screen' ;;
			*) want='ui: screen pc.h3.join.screen' ;; esac
			if grep -aq "$want" "$log"; then
				echo "$name: the profile screen opened ($(grep -a "ui: screen" "$log" | tail -1 | sed 's/.*ui: screen //'))"
			else
				fail profilepaths "$name: no profile screen ($(grep -a "ui: screen" "$log" | tail -3 | sed 's/.*ui: screen //' | tr '\n' ' '))"
			fi
			grep -aqiE "assert|exception|halt" "$log" && fail profilepaths "$name: an assertion, exception or halt"
		done ;;
	browserrows)
		# (OpenCE's server browser, Menu style Halo CE: Multiplayer, SERVER BROWSER, the profile (A joins, A
		# the profile, A), with 16
		# made-up games of names of every sort, PC ones among them)
		# (the frame counter on, as on the owner's Vita: its digits are the HUD's tall font, whose
		# rows in the glyph cache leave room for fewer of the browser's glyphs)
		# (R twice: pages down, the PC GAMES group's rows under the Vitas')
		init_line='display_framerate 1' run browserrows "$menus_data" 36 "wait:150:2000 down:150:800 a wait:150:4000 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000 rt:150:4000 rt:150:4000" \
			HALO_MENU_STYLE=classic $h3net HALO_TEST_BROWSER_GAMES=16 HALO_TEST_BROWSER_NAMES=1 HALO_TEST_BROWSER_PC=1
		log=$out/browserrows/run.log
		echo "--- browserrows"
		grep -aE "menus: browser row [0-9]+: |text: the glyph cache" "$log" | sort -u | head -16
		[ "$(cat "$out/browserrows/exit")" = 0 ] || fail browserrows "exit $(cat "$out/browserrows/exit")"
		rows=$(grep -aE "menus: browser row [0-9]+: " "$log" | sed 's/.*browser row \([0-9]*\):.*/\1/' | sort -un | wc -l)
		[ "$rows" -ge 12 ] || fail browserrows "$rows rows filled (12 wanted)"
		grep -aE "menus: browser row [0-9]+: " "$log" | grep -aqE "row [0-9]+:  ?\|" && fail browserrows "a row without a name"
		for name in "DONT SKIP CINE." "Krazy's COOP" "~Yeti_Kraken~" "#1 xQuJoKe [V]" "the quick brown fox jumps over"; do
			grep -aqF "$name" "$log" || fail browserrows "no row named \"$name\""
		done
		# (every glyph of the screen kept: the cache going round each frame drew the later rows'
		# text as nothing on the Vita)
		grep -aq "text: the glyph cache .* goes round every frame" "$log" &&
			fail browserrows "the glyph cache went round every frame: the later rows' text would be missing on the Vita" ;;
	h3lan)
		# (down five to BROWSE GAMES, A: the profile (A joins, A the profile, A); the LAN browser:
		# down two, A; Y; Square; a tap on Back)
		run h3lan "$menus_data" 60 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000 down:150:600 down:150:600 a wait:150:2000 y wait:150:2000 x wait:150:20000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_BROWSER_GAMES=14 HALO_TEST_BROWSER_PC=1 "HALO_TEST_TAPS=46000:150,122 50000:552,452"
		run h3lan_empty "$menus_data" 36 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:12000" \
			HALO_MENU_STYLE=halo3
		check_h3 h3lan halo3
		log=$out/h3lan/run.log
		grep -aq "menus: h3 mp: browse the system link games; the profile, then the LAN browser" "$log" ||
			fail h3lan "BROWSE GAMES on SYSTEM LINK did not ask for the LAN browser"
		grep -aq "menus: the Halo 3 LAN browser for the System Link list" "$log" && grep -aq "ui: screen pc.h3.lan.screen" "$log" ||
			fail h3lan "the profile did not lead to the Halo 3 LAN browser"
		grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.connected.server_list" "$log" &&
			fail h3lan "the Xbox's System Link list opened with Menu style Halo 3"
		grep -aq "menus: the LAN browser (system link)" "$log" || fail h3lan "the LAN browser was not the system link one"
		grep -aq "menus: the LAN browser lists 14: Test game 02 (Hang 'Em High, King, 14/16, VITA+PC), Test game 04 (" "$log" ||
			fail h3lan "the LAN browser did not list the 14 games, the Vitas' first, the fullest first"
		grep -aq "menus: the browser's groups (All games): VITA GAMES (7), PC GAMES (7)" "$log" ||
			fail h3lan "the LAN browser did not group the games: VITA GAMES, then PC GAMES"
		grep -aq "menus: the server browser's game 3 of 14 chosen" "$log" || fail h3lan "down two did not choose the third game"
		grep -aq "menus: the LAN browser's game Test game .* is no longer listed" "$log" ||
			fail h3lan "A on a game the System Link list does not show did not say it is not listed"
		grep -aq "menus: the server browser sorts by name" "$log" || fail h3lan "Y did not sort the games by name"
		grep -aq "menus: the LAN browser looks again" "$log" || fail h3lan "Square did not look again"
		grep -aq "menus: the LAN browser's game Test game 00 is no longer listed" "$log" ||
			fail h3lan "a tap on the first game's row (sorted by name, under VITA GAMES) did not press A on it"
		grep -a "ui: screen" "$log" | tail -1 | grep -qE '4way_start2join_screen|pc.h3.join.screen' ||
			fail h3lan "a tap on Back did not go back to the profile"
		check_h3 h3lan_empty halo3
		grep -aq 'menus: the server browser says "No games found on your network"' "$out/h3lan_empty/run.log" ||
			fail h3lan "with no games the LAN browser did not say none were found" ;;
	h3adhoc)
		# (the connection Ad hoc: BROWSE GAMES, the joining dialog, joined: the profile (A joins,
		# A the profile, A), the LAN browser)
		run h3adhoc "$menus_data" 40 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:6000 a wait:150:2000 a wait:150:2000 a wait:150:8000" \
			HALO_MENU_STYLE=halo3 HALO_NET_ADHOC=true HALO_ADHOC_TEST=join
		# (the join failing: START GAME, the joining dialog; A again; B)
		run h3adhoc_fail "$menus_data" 30 "$h3mppad down:150:500 down:150:500 down:150:500 a wait:150:5000 a wait:150:5000 b wait:150:3000" \
			HALO_MENU_STYLE=halo3 HALO_NET_ADHOC=true HALO_ADHOC_TEST=fail
		# (the connection Same Wi-Fi: NETWORK, down to AD HOC, A: ad hoc, live, the system's dialog
		# asked for (joined); down five to BROWSE GAMES, A: the profile (A joins, A the profile, A):
		# the ad hoc LAN browser; B, B: the lobby; NETWORK, up to SYSTEM LINK, A: Same Wi-Fi again)
		run h3adhoc_live "$menus_data" 65 "$h3mppad a wait:150:3000 down:150:600 a wait:150:5000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000 b wait:150:3000 b wait:150:3000 up:150:500 up:150:500 up:150:500 up:150:500 up:150:500 a wait:150:3000 up:150:600 a wait:150:4000" \
			HALO_MENU_STYLE=halo3 HALO_ADHOC_TEST=join
		check_h3 h3adhoc halo3
		log=$out/h3adhoc/run.log
		grep -aq "menus: h3 mp lobby: NETWORK: AD HOC" "$log" || fail h3adhoc "the lobby did not open on AD HOC with the connection Ad hoc"
		grep -aq "ui: screen pc.h3.adhoc.joining" "$log" || fail h3adhoc "BROWSE GAMES out of the group did not open the joining dialog"
		grep -aq "ad hoc: the system's dialog would join the room's group here (HALO_ADHOC_TEST=join)" "$log" ||
			fail h3adhoc "the joining dialog did not ask for the system's dialog"
		grep -aq "menus: h3 adhoc: in the ad hoc group: browse the games" "$log" ||
			fail h3adhoc "joined, BROWSE GAMES did not go on by itself"
		grep -aq "menus: the LAN browser (ad hoc)" "$log" || fail h3adhoc "the ad hoc LAN browser did not open"
		grep -aq 'menus: the server browser says "No games found near you"' "$log" ||
			fail h3adhoc "the ad hoc LAN browser did not say no games were found near"
		check_h3 h3adhoc_fail halo3
		log=$out/h3adhoc_fail/run.log
		grep -aq 'menus: h3 adhoc: the joining dialog says "The PS Vita did not join the ad hoc group.' "$log" ||
			fail h3adhoc "a failed join was not said"
		[ "$(grep -ac 'menus: h3 adhoc: the system.s dialog asked for' "$log")" -ge 2 ] ||
			fail h3adhoc "A after a failed join did not ask again"
		grep -aq "menus: h3 mp: host an ad hoc game" "$log" && fail h3adhoc "START GAME went on without the group"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.mp.screen' || fail h3adhoc "B did not go back to the lobby"
		check_h3 h3adhoc_live halo3
		log=$out/h3adhoc_live/run.log
		grep -aq "network: switching from Same Wi-Fi to Ad hoc" "$log" && grep -aq "ad hoc: on" "$log" ||
			fail h3adhoc "AD HOC did not switch to ad hoc play, live"
		grep -aq "ad hoc: the system's dialog would join the room's group here" "$log" ||
			fail h3adhoc "AD HOC did not ask for the system's dialog"
		grep -aq "menus: the LAN browser (ad hoc)" "$log" || fail h3adhoc "BROWSE GAMES did not open the ad hoc LAN browser"
		grep -aq "network: switching from Ad hoc to Same Wi-Fi" "$log" && grep -aq "ad hoc: off" "$log" &&
			grep -aq "ad hoc: the group left" "$log" || fail h3adhoc "SYSTEM LINK did not leave ad hoc play"
		grep -aq "ui: screen pc.h3.adhoc.restart" "$log" && fail h3adhoc "a restart dialog opened" ;;
	h3adhoccoop)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3adhoccoop"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (CAMPAIGN, the profile: the lobby on LOCAL, MISSION focused; up to NETWORK, A: the
		# picker; down three, A: AD HOC CO-OP; down three to START GAME, A: the joining dialog,
		# then co-op hosted by itself)
		profile_from=$HALO_TEST_PROFILE run h3adhoccoop "$menus_data" 45 "wait:150:3000 a wait:150:3000 a wait:150:3000 up:150:600 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:10000" \
			HALO_MENU_STYLE=halo3 HALO_NET_ADHOC=true HALO_ADHOC_TEST=join
		profile_from=$HALO_TEST_PROFILE run h3adhoccoop_live "$menus_data" 30 "wait:150:3000 a wait:150:3000 a wait:150:3000 up:150:600 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:4000" \
			HALO_MENU_STYLE=halo3 HALO_ADHOC_TEST=join
		check_h3 h3adhoccoop halo3
		log=$out/h3adhoccoop/run.log
		grep -aq "menus: h3 campaign: network AD HOC CO-OP" "$log" || fail h3adhoccoop "AD HOC CO-OP was not chosen"
		grep -aq "ui: screen pc.h3.adhoc.joining" "$log" || fail h3adhoccoop "START GAME out of the group did not open the joining dialog"
		grep -aq "menus: h3 adhoc: in the ad hoc group: host co-op" "$log" || fail h3adhoccoop "joined, co-op was not hosted by itself"
		grep -aq "menus: h3 campaign: host co-op levels.*(ad hoc)" "$log" && grep -aq "co-op: hosting levels.* from the campaign's menus" "$log" ||
			fail h3adhoccoop "the level was not hosted as co-op"
		grep -a "ui: screen" "$log" | tail -1 | grep -qE 'connected_pregame_screen|pc.h3.pregame.screen' ||
			fail h3adhoccoop "the game's lobby (the waiting screen) did not open"
		check_h3 h3adhoccoop_live halo3
		grep -aq "network: switching from Same Wi-Fi to Ad hoc" "$out/h3adhoccoop_live/run.log" ||
			fail h3adhoccoop "AD HOC CO-OP with Same Wi-Fi did not switch to ad hoc play, live" ;;
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
		# (a game hosted without Server Setup's Max players: the Xbox's 16, not the build's 128)
		grep -aqE "menus: h3 pregame: .*; players: [^;]*; 1/16$" "$log" ||
			fail h3mpcustom "the system link lobby's PLAYERS did not say 1/16"
		grep -aq "/128" "$log" && fail h3mpcustom "a lobby said the build's 128 players"
		grep -aqE "ui: screen (ui.shell.main_menu.multiplayer_type_select.connected.pregame.connected_pregame_screen|pc.h3.pregame.screen)" "$log" ||
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
	h3friends)
		# (FRIENDS, the seventh item; ADD FRIEND: a code typed; APPEAR OFFLINE; FRIENDS CAN JOIN;
		# PARTY passed; Delta's request: Square declines; Bravo (focused then): Cross joins
		# (offline: go online first), Triangle, down to REMOVE FRIEND, A; B)
		friends_seed "$out/h3friends-save"
		save_from="$out/h3friends-save" run h3friends "$menus_data" 45 "$h3friendspad a wait:150:2500 down:150:500 a wait:150:1500 down:150:500 a wait:150:1500 down:150:500 down:150:500 x wait:150:2500 a wait:150:2000 y wait:150:2500 down:150:500 a wait:150:2500 b wait:150:2000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_FRIENDS_SHOW=online "HALO_TEST_TEXT_INPUT=abcd efgh-jklm"
		check_h3 h3friends halo3
		log=$out/h3friends/run.log
		grep -aq "ui: screen pc.h3.friends.screen" "$log" || fail h3friends "FRIENDS did not open its screen"
		grep -aq "menus: h3 friends: code [A-Z0-9]\{4\}-[A-Z0-9]\{4\}-[A-Z0-9]\{4\}; Go online to see your friends; 5 entries" "$log" ||
			fail h3friends "the code, the status and the five entries were not listed"
		grep -aq "menus: h3 friends: request Delta (.*): Wants to be your friend" "$log" || fail h3friends "the request was not listed"
		grep -aq "menus: h3 friends: friend Bravo (.*): Slayer on Blood Gulch \[JOIN\]" "$log" ||
			fail h3friends "the joinable friend was not listed with its game"
		grep -aq "menus: h3 friends: friend Charlie (.*): Co-op: The Pillar of Autumn, Legendary" "$log" ||
			fail h3friends "the co-op friend was not listed with its level and difficulty"
		grep -aq "menus: h3 friends: request sent .*: Request sent" "$log" || fail h3friends "the request sent was not listed"
		grep -aq "menus: h3 friends: blocked Echo" "$log" || fail h3friends "the blocked player was not listed"
		grep -aq "friends: a friend request to ABCD-EFGH-JKLM (sent once online)" "$log" ||
			fail h3friends "ADD FRIEND did not queue the typed code's request"
		grep -aq "menus: h3 friends: Friend request sent" "$log" || fail h3friends "ADD FRIEND did not say it was sent"
		grep -aq "menus: h3 friends: Appearing offline" "$log" || fail h3friends "APPEAR OFFLINE did not switch on"
		grep -aq "menus: h3 friends: Friends join only by your invites" "$log" || fail h3friends "FRIENDS CAN JOIN did not switch off"
		grep -aq "friends: declined Delta's friend request (nothing sent)" "$log" || fail h3friends "Square did not decline"
		grep -aq "menus: h3 friends: Go online first" "$log" || fail h3friends "Join offline did not say go online first"
		grep -aq "ui: screen pc.h3.friends.options" "$log" || fail h3friends "Triangle did not open the options"
		grep -aq "friends: removed Bravo" "$log" || fail h3friends "REMOVE FRIEND did not remove Bravo"
		# (a second start: the list as left)
		save_from="$out/h3friends/save" run h3friends_again "$menus_data" 25 "$h3friendspad wait:150:2000 b wait:150:2000" HALO_MENU_STYLE=halo3
		check_h3 h3friends_again halo3
		grep -aq "friends: this machine's friend code is .* (1 friends, 0 requests, 2 sent, 1 blocked)" "$out/h3friends_again/run.log" ||
			fail h3friends "the list was not written and read back as left"
		[ "$(grep -a "this machine's friend code is" "$log" | sed 's/.*code is \([^ ]*\).*/\1/')" = \
			"$(grep -a "this machine's friend code is" "$out/h3friends_again/run.log" | sed 's/.*code is \([^ ]*\).*/\1/')" ] ||
			fail h3friends "the code changed between starts" ;;
	h3party)
		# (FRIENDS; down three to PARTY, Cross: the PARTY screen (internet play off: no party, how to
		# make one); Square (QUICK CHAT) and Triangle (type): not in a party, said; B: FRIENDS again
		# (on ADD FRIEND); down five to Bravo, Triangle: the options, offline no INVITE TO PARTY; B, B)
		friends_seed "$out/h3friends-save"
		save_from="$out/h3friends-save" run h3party "$menus_data" 45 "$h3friendspad down:150:500 down:150:500 down:150:500 a wait:150:3000 x wait:150:1500 y wait:150:1500 b wait:150:2000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 y wait:150:2500 b wait:150:2000 b wait:150:2000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_FRIENDS_SHOW=online
		check_h3 h3party halo3
		log=$out/h3party/run.log
		grep -a "menus: h3 party\|PARTY screen" "$log" | head -6
		grep -aq "menus: h3 friends: the PARTY screen next" "$log" || fail h3party "Cross on PARTY did not ask for the PARTY screen"
		grep -aq "menus: the PARTY screen opened" "$log" && grep -aq "ui: screen pc.h3.party.screen" "$log" ||
			fail h3party "the PARTY screen did not open"
		grep -aq "menus: h3 party: Go online to make a party" "$log" || fail h3party "the PARTY screen did not say go online"
		[ "$(grep -ac "menus: h3 party: Not in a party" "$log")" -ge 2 ] ||
			fail h3party "QUICK CHAT and TYPE out of a party did not say so"
		grep -aq "ui: screen pc.h3.party.quick" "$log" && fail h3party "QUICK CHAT opened out of a party"
		grep -a "ui: screen" "$log" | grep -A1 "pc.h3.party.screen" | tail -1 | grep -q "pc.h3.friends.screen" ||
			fail h3party "B did not go back to FRIENDS"
		grep -aq "ui: screen pc.h3.friends.options" "$log" || fail h3party "Triangle did not open a friend's options" ;;
	h3friendspanel)
		# (MULTIPLAYER: the lobby's panel; Triangle: FRIENDS; B, B: the main menu, MULTIPLAYER focused; down
		# to FORGE: its lobby's panel; Triangle; B)
		friends_seed "$out/h3friends-save"
		save_from="$out/h3friends-save" run h3friendspanel "$menus_data" 40 "$h3mppad wait:150:2500 y wait:150:2500 b wait:150:2000 b wait:150:2500 down:150:500 a wait:150:4000 y wait:150:2500 b wait:150:2000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_FRIENDS_SHOW=online
		check_h3 h3friendspanel halo3
		log=$out/h3friendspanel/run.log
		grep -aq "ui: screen pc.h3.mp.screen" "$log" || fail h3friendspanel "MULTIPLAYER did not open the lobby"
		grep -aq "menus: h3 friends panel: 2 ONLINE, 1 NEW, 3 shown" "$log" ||
			fail h3friendspanel "the lobby's panel did not show the two friends online and the request"
		[ "$(grep -ac "ui: screen pc.h3.friends.screen" "$log")" -ge 2 ] ||
			fail h3friendspanel "Triangle did not open FRIENDS from both lobbies"
		grep -aq "ui: screen pc.h3.forge.lobby" "$log" || fail h3friendspanel "the Forge lobby did not open" ;;
	browserprofile)
		# (Halo CE: Multiplayer, SERVER BROWSER (its first item): the profile (A joins, A the profile,
		# A), the server browser; B, B)
		run browserprofile "$menus_data" 40 "wait:150:3000 down:150:800 a wait:150:3000 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:4000 b wait:150:2500 b wait:150:2500" \
			HALO_MENU_STYLE=classic $h3net
		# (Halo 3: MULTIPLAYER, down five to BROWSE GAMES on ONLINE: the profile, then the games browser)
		run browserprofile_h3 "$menus_data" 40 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000" \
			HALO_MENU_STYLE=halo3 $h3net
		# (internet play off: SERVER BROWSER says so; no profile screen)
		run browserprofile_off "$menus_data" 25 "wait:150:3000 down:150:800 a wait:150:3000 a wait:150:4000" HALO_MENU_STYLE=classic
		check_h3 browserprofile classic
		check_h3 browserprofile_h3 halo3
		log=$out/browserprofile/run.log
		screens=$(grep -a "ui: screen" "$log" | sed 's/.*ui: screen //' | tr '\n' '|')
		echo "$screens"
		case $screens in
		*'pc\mp\screen|ui\shell\main_menu\multiplayer_type_select\connected\4way_profile_select\4way_start2join_screen|pc\browser\screen|ui\shell\main_menu\multiplayer_type_select\connected\4way_profile_select\4way_start2join_screen|pc\mp\screen'*) ;;
		*) fail browserprofile "SERVER BROWSER did not open the profile screen, then the server browser, B going back through both" ;;
		esac
		grep -aq "menus: Server Browser: the profile chosen; the server browser" "$log" ||
			fail browserprofile "the server browser did not stand for the System Link list after the profile"
		log=$out/browserprofile_h3/run.log
		grep -aqE "ui: screen (ui.shell.main_menu.multiplayer_type_select.connected.4way_profile_select.4way_start2join_screen|pc.h3.join.screen)" "$log" &&
			grep -aq "ui: screen pc.h3.lan.screen" "$log" || fail browserprofile "BROWSE GAMES on ONLINE did not ask for the profile first"
		awk '/4way_start2join_screen|pc.h3.join.screen/ { profile = 1 } /ui: screen pc.h3.lan.screen/ { if (!profile) exit 1; found = 1 }
			END { exit !found }' "$log" || fail browserprofile "the games browser opened before the profile"
		log=$out/browserprofile_off/run.log
		grep -aq "ui: screen pc.mp.offline" "$log" || fail browserprofile "internet play off: SERVER BROWSER did not say how to turn it on"
		grep -aq "4way_start2join_screen\|menus: Server Browser: the profile" "$log" &&
			fail browserprofile "internet play off: the profile screen opened anyway" ;;
	*)
		echo "usage: $0 [fresh|level|xbox|pause|h3probe|h3classic|h3xbox|h3broken|h3style|h3online|h3touch|h3campaign|h3resume|h3pause|h3mp|h3browser|h3create|h3mptouch|h3coop|h3coopguide|h3scenes|h3characters|h3profiles|h3proftouch|h3customize|h3models|h3nameplate|h3damaged|h3mods|h3modspicker|h3modstouch|h3mpcustom|h3lan|h3adhoc|h3adhoccoop|h3oddrows|h3lobbyscenes|h3modelsmany|h3friends|h3friendspanel|browserprofile|h3players|browserrows|profilepaths]..." >&2; exit 2 ;;
	esac
done
echo "logs in $out"
[ $status = 0 ] && echo PASS
test_out_done $status
exit $status
