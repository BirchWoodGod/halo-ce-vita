#!/bin/bash
# The Halo content folder (paths.content, HALO_CONTENT_ROOT: the player's own
# Halo: The Master Chief Collection folder, halo1 in it; no switch:
# port/linux/game/mcc_content.c, docs/mcc_cea_maps.md).
#
# 1. Without any MCC file, always: mcc_content_test.c (MCC's maps' names,
#    Steam's libraryfolders.vdf, its truncations, the loaders' table by a
#    map file's header), under AddressSanitizer
#    and undefined-behaviour trapping when the compiler has them.
# 2. With the player's own MCC halo1 folder (HALO_TEST_MCC_ROOT, default
#    Steam's; SKIP without one) and the Xbox maps (HALO_TEST_DATA), on the
#    Linux harness: MCC's maps linked read only into a content folder of the
#    test's (<out>/mcc/halo1/maps, never copied), which HALO_CONTENT_ROOT
#    names:
#      campaign  each of HALO_TEST_MCC_LEVELS (b30 d40) started as the campaign
#                list starts it (mods\mcc_<level>), its cinematics skipped,
#                played HALO_TEST_MCC_SECONDS (60): it must load from MCC's
#                folder, say its sounds are silent, tick on, and log no
#                assertion or exception
#      won       mcc_b30 won goes on to mcc_b40, MCC's next level
#      mp        mcc_bloodgulch, an offline game with HALO_TEST_MCC_BOTS (6)
#                bots (the network test's local: game, in a namespace with no
#                network), HALO_TEST_MCC_MP_SECONDS (60): the bots must score
#      xbox      no content folder (HALO_CONTENT_ROOT=none): the Xbox maps
#                alone, mods\mcc_b30 does not load, no MCC map is listed
#      shadow    a file of the maps folder named mcc_bloodgulch.map (an Xbox
#                map) is never listed nor played for MCC's name
#   HALO_TEST_SHOTS: a folder for the runs' frames (every 60 frames; the GL
#   build's).
# 3. menu: MCC's own menu files, with the MCC folder alone or beside the Xbox
#    maps (the Xbox install "hidden": a data folder with no maps in it):
#      pcfiles    the Xbox's ui.map and the content folder, no bitmaps.map,
#                 loc.map or sounds.map in the maps folder: the Halo PC files
#                 the Halo 3 menu reads are the content folder's
#                 halo1/maps/custom_edition/ ones (not halo1/maps/bitmaps.map,
#                 another file of the same name): the Halo 3 main menu comes up
#      priority   both ui.maps there: the Xbox's is the menus' map; with
#                 HALO_MENU_SOURCE=mcc (paths.menu_source) MCC's, and the
#                 campaign still starts the Xbox's a10; with "xbox" and no
#                 Xbox ui.map the menus' map is missing, not MCC's
#      mccmenu    no Xbox file at all: MCC's ui.map is the menus' map
#                 (menu_tags_loaded after it), the classic menu's six rows
#                 each reached and named (CAMPAIGN, MULTIPLAYER, PROFILES,
#                 SETTINGS, CREDITS, QUIT), a picture drawn; SETTINGS (one
#                 of Halo PC's own functions) says the menu is not supported
#                 with MCC's menu file yet, logs the function, and the menu
#                 goes on, no assertion
#      b30        mods\mcc_b30 started by the console from that menu, played
#      campaign   CAMPAIGN, a new profile, the difficulty, with no Xbox map:
#                 MCC's a10 starts (mods\mcc_a10)
#
# 4. auto: the PC finding the player's MCC by itself (paths.content "auto"),
#    HOME a folder of the test's with a made-up Steam: its libraryfolders.vdf
#    names a second library whose steamapps/common/Halo The Master Chief
#    Collection is a link to the real one (read only, never copied):
#      autobg     no Xbox file, no data folder set (a copy of the harness in a
#                 folder of its own, as a player's unpacked PC build): MCC
#                 found in the second library, no disc image asked for,
#                 mcc_bloodgulch offline with bots HALO_TEST_MCC_MP_SECONDS
#                 (60): the bots must score, no assertion
#      autogone   paths.content a folder that is gone, the Xbox maps there:
#                 said, and MCC found by itself
#      autonone   no Steam in HOME: "Halo MCC not found" said (the main
#                 menu's notice), the Xbox maps alone
#
#   run_mcc_content_test.sh [unit|harness|menu|auto]   (default all)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
mcc=${HALO_TEST_MCC_ROOT:-$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1}
levels=${HALO_TEST_MCC_LEVELS:-b30 d40}
seconds=${HALO_TEST_MCC_SECONDS:-60}
mp_seconds=${HALO_TEST_MCC_MP_SECONDS:-60}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_mcc_content_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
status=0
fail() { echo "FAIL: $1"; status=1; }
parts=${*:-unit harness menu auto}

case " $parts " in *" unit "*)
	cc=${CC:-gcc}
	sanitize="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
	echo 'int main(void){return 0;}' | $cc $sanitize -x c - -o "$out/probe" 2>/dev/null || sanitize=
	$cc -g -O1 -Wall -Wextra -Werror $sanitize -I"$root/port/linux/game" -I"$root/port/linux/src" \
		"$here/mcc_content_test.c" "$root/port/linux/game/mcc_content.c" "$root/port/linux/src/mcc_paths.c" \
		-o "$out/mcc_content_test" || fail "mcc_content_test did not build"
	[ -x "$out/mcc_content_test" ] && { "$out/mcc_content_test" || fail "mcc_content_test"; }
	;;
esac

case " $parts " in *" harness "*)
	if [ ! -x "$binary" ]; then
		echo "SKIP (harness): no harness at $binary (HALO_TEST_VITA)"
	elif [ ! -e "$data/maps/ui.map" ]; then
		echo "SKIP (harness): no Xbox maps in $data/maps (HALO_TEST_DATA)"
	elif [ ! -r "$mcc/maps/bitmaps.map" ]; then
		echo "SKIP (harness): no MCC halo1 folder at $mcc (HALO_TEST_MCC_ROOT)"
	else
		# (MCC's maps, linked read only, never copied)
		mkdir -p "$out/mcc/halo1/maps"
		for file in "$mcc"/maps/*.map; do
			ln -sfn "$file" "$out/mcc/halo1/maps/"
		done
		# run NAME SECONDS [VAR=value...]: the harness, the data folder's
		# maps the Xbox's, init.txt and the commands the case's
		run() {
			local name=$1 run_seconds=$2 dir="$out/$1"
			shift 2
			rm -rf "$dir"
			mkdir -p "$dir/data/maps" "$dir/save"
			for file in "$(cd "$data" && pwd)"/maps/*.map; do
				ln -sfn "$file" "$dir/data/maps/"
			done
			[ -n "${init_line:-}" ] && printf 'game_difficulty_set hard\n%s\n' "$init_line" > "$dir/data/init.txt"
			[ -n "${extra_map:-}" ] && ln -sfn "$extra_map" "$dir/data/maps/$extra_map_name"
			local shots=()
			if [ -n "${HALO_TEST_SHOTS:-}" ]; then
				mkdir -p "$HALO_TEST_SHOTS/$name"
				shots=("HALO_SCREENSHOT_DIR=$HALO_TEST_SHOTS/$name" HALO_SCREENSHOT_EVERY=60)
			fi
			(cd "$dir" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
				env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
				HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$run_seconds" \
				HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
				HALO_CONTENT_ROOT="${content_root:-$out/mcc}" HALO_FRAME_TIMING=300 "${shots[@]}" "$@" \
				timeout -k 5 $((run_seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
			local code=$?
			[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
			if cat "$dir/run.log" "$dir/data/debug.txt" 2>/dev/null | grep -av "render assertion skipped" |
				grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
				fail "$name: an assertion or exception ($dir)"
			fi
		}
		ticks_of() { grep -a "frame-timing: game tick" "$1" | tail -1 | sed -E 's/.*game tick ([0-9]+).*/\1/'; }
		campaign_commands='30:(set cheat_deathless_player true);60:@skip;300:@skip;600:@skip;900:@skip;1200:@skip'

		for level in $levels; do
			init_line="map_name mods\\mcc_$level" run "mcc_$level" "$seconds" \
				HALO_TICK_THREAD=0 "HALO_TEST_COMMANDS=$campaign_commands"
			log="$out/mcc_$level/run.log"
			debug="$out/mcc_$level/data/debug.txt"
			ticks=$(ticks_of "$log")
			if ! grep -aq "memory: loaded mods\\\\mcc_$level" "$log" ||
				! grep -aq "custom edition: loading 'd:\\\\mcc\\\\halo1\\\\maps\\\\$level.map'" "$debug"; then
				fail "mcc_$level was not loaded from MCC's folder ($out/mcc_$level)"
			elif ! grep -aq "mcc: '$level' is MCC's own campaign map: its sounds are in MCC's FMOD banks" "$debug"; then
				fail "mcc_$level: its silent sounds were not said"
			elif [ "${ticks:-0}" -lt $((seconds * 20)) ]; then
				fail "mcc_$level reached tick ${ticks:-0} in $seconds s"
			else
				echo "mcc_$level: loaded from MCC's folder, tick $ticks in $seconds s;" \
					"$(grep -a 'objects: ' "$log" | tail -1 | sed 's/.*objects: \([0-9]*\) in the game.*/\1 objects/')"
			fi
		done

		# won: on to MCC's next level
		init_line='map_name mods\mcc_b30' run won 40 HALO_TICK_THREAD=0 'HALO_TEST_COMMANDS=M300:(game_won);'
		if grep -aq "memory: loaded mods\\\\mcc_b40" "$out/won/run.log"; then
			echo "won mcc_b30: on to mcc_b40"
		else
			fail "mcc_b30 won did not go on to mcc_b40 ($out/won)"
		fi

		# mp: offline with bots on MCC's Blood Gulch
		init_line= run mp "$mp_seconds" HALO_TICK_THREAD=1 \
			HALO_NETWORK_TEST="local:mcc_bloodgulch:slayer" HALO_NETWORK_TEST_START=5 HALO_NETWORK_TEST_SCORE=500 \
			HALO_TEST_INPUT=bot:1 HALO_BOTS="${HALO_TEST_MCC_BOTS:-6}"
		log="$out/mp/run.log"
		# (the kills of every player, the network test's last tick line's kN)
		kills=$(grep -a "network test: tick" "$log" | tail -1 | grep -aoE " k[0-9]+" | tr -d ' k' | awk '{s+=$1} END {print s+0}')
		if ! grep -aq "custom edition: loading 'd:\\\\mcc\\\\halo1\\\\maps\\\\bloodgulch.map'" "$out/mp/data/debug.txt"; then
			fail "mp: MCC's bloodgulch was not loaded ($out/mp)"
		elif ! grep -aq "network test: starting the game" "$log"; then
			fail "mp: the game did not start ($out/mp)"
		elif [ "${kills:-0}" -lt 1 ]; then
			fail "mp: no kill in $mp_seconds s ($out/mp)"
		else
			echo "mp: mcc_bloodgulch, $(grep -ac 'network test: player [0-9]* is named' "$log") players, kills ${kills:-0};" \
				"$(grep -a 'mcc: [0-9]* multiplayer maps' "$out/mp/data/debug.txt" | tail -1 | sed 's/.*mcc: //')"
		fi

		# xbox: no content folder, the Xbox maps alone
		content_root=none init_line='map_name mods\mcc_b30' run xbox 25 HALO_TICK_THREAD=0
		if grep -aq "memory: loaded mods" "$out/xbox/run.log" || grep -aq "^.*mcc: [0-9]* .* maps" "$out/xbox/data/debug.txt" ||
			! grep -aq "content: no Halo content folder (paths.content is \"none\")" "$out/xbox/run.log"; then
			fail "no content folder: an MCC map was listed or loaded ($out/xbox)"
		else
			echo "no content folder: the Xbox maps alone, mods\\mcc_b30 not loaded"
		fi

		# shadow: the maps folder's mcc_bloodgulch.map (an Xbox map) is not
		# MCC's Blood Gulch, nor listed
		extra_map="$(cd "$data" && pwd)/maps/bloodgulch.map" extra_map_name=mcc_bloodgulch.map init_line= \
			run shadow 40 HALO_TICK_THREAD=1 \
			HALO_NETWORK_TEST="local:mcc_bloodgulch:slayer" HALO_NETWORK_TEST_START=5 HALO_TEST_INPUT=bot:1 HALO_BOTS=1
		if ! grep -aq "the maps folder's 'mcc_bloodgulch' is not listed" "$out/shadow/data/debug.txt" ||
			! grep -aq "custom edition: loading 'd:\\\\mcc\\\\halo1\\\\maps\\\\bloodgulch.map'" "$out/shadow/data/debug.txt"; then
			fail "shadow: the maps folder's mcc_bloodgulch.map was listed or played ($out/shadow)"
		else
			echo "shadow: the maps folder's mcc_bloodgulch.map left out; MCC's played"
		fi
	fi
	;;
esac

case " $parts " in *" menu "*)
	menus_folder=${HALO_MENUS_FOLDER:-$root/port/vita/app0/menus/}
	if [ ! -x "$binary" ]; then
		echo "SKIP (menu): no harness at $binary (HALO_TEST_VITA)"
	elif [ ! -r "$mcc/maps/ui.map" ] || [ ! -r "$mcc/maps/custom_edition/bitmaps.map" ]; then
		echo "SKIP (menu): no MCC halo1 folder with maps/ui.map and maps/custom_edition at $mcc (HALO_TEST_MCC_ROOT)"
	elif [ ! -r "$menus_folder/menus.txt" ]; then
		echo "SKIP (menu): no menus folder at $menus_folder (HALO_MENUS_FOLDER)"
	else
		# (MCC's maps and its custom_edition folder, linked read only, never copied)
		mkdir -p "$out/mccmenu_content/halo1/maps"
		for file in "$mcc"/maps/*.map "$mcc"/maps/custom_edition; do
			ln -sfn "$file" "$out/mccmenu_content/halo1/maps/"
		done
		# mrun NAME SECONDS XBOX(yes|no) [VAR=value...]: the harness with the Xbox maps
		# (yes: HALO_TEST_DATA's) or a data folder with none (no); the content folder MCC's
		mrun() {
			local name=$1 run_seconds=$2 xbox=$3 dir="$out/$1"
			shift 3
			rm -rf "$dir"
			mkdir -p "$dir/data/maps" "$dir/save" "$dir/shots"
			if [ "$xbox" = yes ]; then
				for file in "$(cd "$data" && pwd)"/maps/*.map; do
					ln -sfn "$file" "$dir/data/maps/"
				done
			fi
			[ -n "${init_line:-}" ] && printf 'game_difficulty_set hard\n%s\n' "$init_line" > "$dir/data/init.txt"
			(cd "$dir" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
				env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
				HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$run_seconds" \
				HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
				HALO_CONTENT_ROOT="$out/mccmenu_content" HALO_MENUS_FOLDER="$menus_folder" HALO_UI_LOG=2 \
				HALO_TICK_THREAD=1 HALO_FRAME_TIMING=300 HALO_SCREENSHOT_DIR="$dir/shots" HALO_SCREENSHOT_EVERY=60 "$@" \
				timeout -k 5 $((run_seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
			local code=$?
			[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
			if cat "$dir/run.log" "$dir/data/debug.txt" 2>/dev/null | grep -av "render assertion skipped" |
				grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
				fail "$name: an assertion or exception ($dir)"
			fi
			if [ -n "${HALO_TEST_SHOTS:-}" ]; then
				mkdir -p "$HALO_TEST_SHOTS/$name"
				cp "$dir"/shots/* "$HALO_TEST_SHOTS/$name/" 2> /dev/null
			fi
		}
		# the last frame of a run is not one colour (a menu or a level drawn)
		picture_drawn() {
			python3 - "$1" <<'PY'
import glob, sys
frames = sorted(glob.glob(sys.argv[1] + "/shots/*.bmp"))
if not frames:
    sys.exit(1)
data = open(frames[-1], "rb").read()
sys.exit(0 if len(set(data[54::37])) > 40 else 1)
PY
		}
		ticks_of_log() { grep -a "frame-timing: game tick" "$1" | tail -1 | sed -E 's/.*game tick ([0-9]+).*/\1/'; }
		menu_log() { echo "$out/$1/run.log"; }
		menu_debug() { echo "$out/$1/data/debug.txt"; }

		# pcfiles: the Xbox's ui.map, the content folder's custom_edition Halo PC files
		mrun pcfiles 20 yes HALO_MENU_STYLE=halo3 "HALO_TEST_PAD=wait:150:3000"
		if ! grep -aq "menus: the Halo PC pictures and text read" "$(menu_log pcfiles)" ||
			! grep -aq "menus: Halo 3 screen h3/main/screen for ui.shell.main_menu.main_menu" "$(menu_log pcfiles)" ||
			! grep -aq "ui: screen pc.h3.main.screen" "$(menu_log pcfiles)"; then
			fail "pcfiles: the Halo 3 menu did not come up from the content folder's custom_edition files ($out/pcfiles)"
		elif ! grep -aq "content: Halo PC file bitmaps.map: not in the maps folder, the Halo content folder's custom_edition one" "$(menu_log pcfiles)" ||
			! grep -aq "content: Halo PC file loc.map: not in the maps folder, the Halo content folder's custom_edition one" "$(menu_log pcfiles)"; then
			fail "pcfiles: bitmaps.map and loc.map were not the content folder's custom_edition ones ($out/pcfiles)"
		elif ! picture_drawn "$out/pcfiles"; then
			fail "pcfiles: no picture drawn ($out/pcfiles)"
		else
			echo "pcfiles: the Xbox's ui.map and the Halo 3 menu, its Halo PC files MCC's custom_edition ones"
		fi

		# priority: both ui.maps there
		mrun priority 20 yes HALO_MENU_STYLE=classic "HALO_TEST_PAD=wait:150:3000"
		if ! grep -aq "menus: the menu map is the Xbox's ui.map" "$(menu_debug priority)" ||
			grep -aq "custom edition: loading 'd:.mcc.halo1.maps.ui.map'" "$(menu_debug priority)"; then
			fail "priority: with both, the Xbox's ui.map was not the menus' ($out/priority)"
		else
			echo "priority: both ui.maps there, the Xbox's is the menus'"
		fi
		mrun forced 40 yes HALO_MENU_SOURCE=mcc HALO_MENU_STYLE=classic \
			"HALO_TEST_PAD=wait:150:3000 a wait:150:4000 a wait:150:4000 a wait:150:5000 wait:150:25000"
		if ! grep -aq "menus: the menu map is MCC's halo1.maps.ui.map (paths.menu_source is mcc)" "$(menu_debug forced)" ||
			! grep -aq "custom edition: loading 'd:.mcc.halo1.maps.ui.map'" "$(menu_debug forced)"; then
			fail "forced: paths.menu_source=mcc did not make MCC's ui.map the menus' ($out/forced)"
		elif ! grep -aq "memory: loaded levels.a10.a10" "$(menu_log forced)"; then
			fail "forced: Campaign, the difficulty did not start the Xbox's a10 ($out/forced)"
		else
			echo "forced: paths.menu_source=mcc with the Xbox's files there: MCC's menus, the Xbox's a10 started from them"
		fi
		mrun xboxonly 15 no HALO_MENU_SOURCE=xbox HALO_MENU_STYLE=classic
		if grep -aq "custom edition: loading 'd:.mcc.halo1.maps.ui.map'" "$(menu_debug xboxonly)" ||
			! grep -aq "menus: the menu map is the Xbox's ui.map (paths.menu_source is xbox, but there is no ui.map" "$(menu_debug xboxonly)"; then
			fail "xboxonly: paths.menu_source=xbox with no Xbox ui.map used MCC's ($out/xboxonly)"
		else
			echo "xboxonly: paths.menu_source=xbox: MCC's ui.map not read"
		fi

		# mccmenu: no Xbox file; the six rows, the handler message
		# (A on each row and B back: CAMPAIGN, MULTIPLAYER and PROFILES open a screen,
		# SETTINGS one of Halo PC's own functions, which fails, with the menu as it was; CREDITS
		# and QUIT open theirs)
		mrun mccmenu 60 no HALO_MENU_STYLE=classic \
			"HALO_TEST_PAD=wait:150:3000 a wait:150:3500 b wait:150:2500 down:150:700 a wait:150:3500 b wait:150:2500 down:150:700 a wait:150:3500 b wait:150:2500 down:150:700 a wait:150:3500 down:150:700 a wait:150:3500 b wait:150:2500 down:150:700 a wait:150:3000 b wait:150:2500"
		log=$(menu_log mccmenu)
		debug=$(menu_debug mccmenu)
		missing=
		for row in load_camp multiplayer profile settings credits quit_game; do
			grep -aq "ui: handler of ui.shell.main_menu.main_menu_item_$row: event 0" "$log" || missing="$missing $row"
		done
		if ! grep -aq "menus: the menu map is MCC's halo1.maps.ui.map (no Xbox ui.map in the maps folder)" "$debug" ||
			! grep -aq "custom edition: loading 'd:.mcc.halo1.maps.ui.map'" "$debug" ||
			! grep -aq "ui: screen ui.shell.main_menu.main_menu" "$log"; then
			fail "mccmenu: MCC's ui.map was not the menus' with no Xbox file ($out/mccmenu)"
		elif [ -n "$missing" ]; then
			fail "mccmenu: main menu rows not reached:$missing ($out/mccmenu)"
		elif ! grep -aq "MCC's menu file: This menu isn't supported with MCC's menu file yet" "$log" ||
			! grep -aq "menus: MCC's ui.map: widget 'ui.shell.main_menu.main_menu_item_settings' ran function [0-9]*, one of Halo PC's own" "$debug"; then
			fail "mccmenu: SETTINGS did not say it is not supported with MCC's menu file ($out/mccmenu)"
		elif [ "$(grep -ac "ui: screen ui.shell.main_menu.main_menu$" "$log")" -lt 3 ]; then
			fail "mccmenu: the menu did not go on after the unsupported entry ($out/mccmenu)"
		elif ! picture_drawn "$out/mccmenu"; then
			fail "mccmenu: no picture drawn ($out/mccmenu)"
		else
			echo "mccmenu: MCC's ui.map alone: the six rows reached (CAMPAIGN MULTIPLAYER PROFILES SETTINGS CREDITS QUIT), SETTINGS says it is not supported yet, the menu goes on"
		fi

		# b30: started from that menu (the console's map_name, as the menus' tests start a level)
		mrun b30 "$seconds" no HALO_MENU_STYLE=classic \
			'HALO_TEST_COMMANDS=150:map_name mods\mcc_b30;L450:(set cheat_deathless_player true);L600:@skip;L900:@skip'
		ticks=$(ticks_of_log "$(menu_log b30)")
		if ! grep -aq "memory: loaded levels.ui.ui" "$(menu_log b30)" ||
			! grep -aq "menus: the menu map is MCC's" "$(menu_debug b30)"; then
			fail "b30: MCC's menus alone did not come up first ($out/b30)"
		elif ! grep -aq "memory: loaded mods.mcc_b30" "$(menu_log b30)"; then
			fail "b30: mods\\mcc_b30 was not started from MCC's menus alone ($out/b30)"
		elif [ "${ticks:-0}" -lt $((seconds * 10)) ]; then
			fail "b30: reached tick ${ticks:-0} in $seconds s ($out/b30)"
		else
			echo "b30: MCC's menus alone, then mods\\mcc_b30 started and played to tick $ticks"
		fi

		# campaign: the classic menu starts MCC's a10 with no Xbox map
		mrun campaign 70 no HALO_MENU_STYLE=classic \
			"HALO_TEST_PAD=wait:150:3000 a wait:150:4000 a wait:150:4000 a wait:150:5000 wait:150:40000"
		if ! grep -aq "memory: loaded mods.mcc_a10" "$(menu_log campaign)"; then
			fail "campaign: CAMPAIGN, the difficulty did not start MCC's a10 with no Xbox map ($out/campaign)"
		else
			echo "campaign: CAMPAIGN, a new profile, the difficulty: MCC's a10 started (mods\\mcc_a10), no Xbox map"
		fi
	fi
	;;
esac

case " $parts " in *" auto "*)
	mcc_folder=$(cd "$mcc/.." 2> /dev/null && pwd)
	if [ ! -x "$binary" ]; then
		echo "SKIP (auto): no harness at $binary (HALO_TEST_VITA)"
	elif [ ! -r "$mcc/maps/bloodgulch.map" ] || [ ! -r "$mcc/maps/ui.map" ]; then
		echo "SKIP (auto): no MCC halo1 folder with maps/bloodgulch.map and maps/ui.map at $mcc (HALO_TEST_MCC_ROOT)"
	else
		# (the made-up Steam: the Steam folder's own library has no MCC, the
		# second one a link to the real MCC folder)
		home="$out/auto_home"
		mkdir -p "$home/.local/share/Steam/steamapps/common" "$out/auto_lib2/steamapps/common" "$out/auto_empty_home"
		printf '"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n\t"1"\n\t{\n\t\t"path"\t\t"%s"\n\t\t"apps"\n\t\t{\n\t\t\t"976730"\t\t"1"\n\t\t}\n\t}\n}\n' \
			"$home/.local/share/Steam" "$out/auto_lib2" > "$home/.local/share/Steam/steamapps/libraryfolders.vdf"
		ln -sfn "$mcc_folder" "$out/auto_lib2/steamapps/common/Halo The Master Chief Collection"
		found_root="$out/auto_lib2/steamapps/common/Halo The Master Chief Collection"
		# (a copy of the harness in a folder of its own, where the PC build
		# keeps its config.toml and, with no maps folder, its log)
		mkdir -p "$out/auto_bin"
		cp "$binary" "$out/auto_bin/halo"
		# arun NAME SECONDS HOME XBOX(yes|no) [VAR=value...]: the harness with
		# paths.content unset unless given, HOME the made-up one
		arun() {
			local name=$1 run_seconds=$2 run_home=$3 xbox=$4 dir="$out/$1"
			shift 4
			rm -rf "$dir"
			mkdir -p "$dir/run" "$dir/save"
			local data=()
			if [ "$xbox" = yes ]; then
				mkdir -p "$dir/data/maps"
				for file in "$(cd "$data_folder" && pwd)"/maps/*.map; do
					ln -sfn "$file" "$dir/data/maps/"
				done
				data=("HALO_DATA_ROOT=$dir/data")
			fi
			rm -f "$out/auto_bin/debug.txt"
			(cd "$dir/run" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
				env -u HALO_CONTENT_ROOT -u HALO_DATA_ROOT HOME="$run_home" SDL_AUDIODRIVER=dummy \
				SDL_VIDEODRIVER=offscreen HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 \
				HALO_EXIT_AFTER="$run_seconds" HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 \
				HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false HALO_FRAME_TIMING=300 "${data[@]}" "$@" \
				timeout -k 5 $((run_seconds + 120)) "$out/auto_bin/halo" > "$dir/run.log" 2>&1)
			local code=$?
			[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
			[ -f "$out/auto_bin/debug.txt" ] && mv "$out/auto_bin/debug.txt" "$dir/debug.txt"
			[ -f "$dir/data/debug.txt" ] && mv "$dir/data/debug.txt" "$dir/debug.txt"
			if cat "$dir/run.log" "$dir/debug.txt" 2>/dev/null | grep -av "render assertion skipped" |
				grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
				fail "$name: an assertion or exception ($dir)"
			fi
		}
		data_folder=$data

		# autobg: no Xbox file, MCC found by itself; Blood Gulch with bots
		arun autobg "$mp_seconds" "$home" no HALO_TICK_THREAD=1 \
			HALO_NETWORK_TEST="local:mcc_bloodgulch:slayer" HALO_NETWORK_TEST_START=5 HALO_NETWORK_TEST_SCORE=500 \
			HALO_TEST_INPUT=bot:1 HALO_BOTS="${HALO_TEST_MCC_BOTS:-6}"
		log="$out/autobg/run.log"
		kills=$(grep -a "network test: tick" "$log" | tail -1 | grep -aoE " k[0-9]+" | tr -d ' k' | awk '{s+=$1} END {print s+0}')
		if ! grep -aqF "content: Halo MCC found (Steam library $out/auto_lib2): $found_root" "$log" ||
			! grep -aqF "no maps/ folder: Halo MCC is at $found_root" "$log" ||
			! grep -aqF "content: the Halo content folder: $found_root" "$log"; then
			fail "autobg: MCC was not found by itself in the second Steam library ($out/autobg)"
		elif grep -aq "no game data: quitting" "$log"; then
			fail "autobg: the Xbox disc image was asked for ($out/autobg)"
		elif ! grep -aq "custom edition: loading 'd:\\\\mcc\\\\halo1\\\\maps\\\\bloodgulch.map'" "$out/autobg/debug.txt"; then
			fail "autobg: MCC's bloodgulch was not loaded ($out/autobg)"
		elif [ "${kills:-0}" -lt 1 ]; then
			fail "autobg: no kill in $mp_seconds s ($out/autobg)"
		else
			echo "autobg: MCC found by itself (second Steam library), no Xbox file: mcc_bloodgulch $mp_seconds s," \
				"$(grep -ac 'network test: player [0-9]* is named' "$log") players, kills $kills"
		fi

		# autogone: paths.content gone, the Xbox maps there: MCC found anyway
		arun autogone 20 "$home" yes HALO_CONTENT_ROOT="$out/gone_mcc" HALO_TICK_THREAD=1
		log="$out/autogone/run.log"
		if ! grep -aqF "content: paths.content is $out/gone_mcc, but no Halo MCC folder is there" "$log" ||
			! grep -aqF "content: the Halo content folder: $found_root" "$log"; then
			fail "autogone: a gone paths.content did not fall back to MCC found by itself ($out/autogone)"
		else
			echo "autogone: paths.content gone: said, MCC found by itself"
		fi

		# autonone: no Steam: said, the Xbox maps alone
		arun autonone 20 "$out/auto_empty_home" yes HALO_TICK_THREAD=1 HALO_MENU_STYLE=halo3
		log="$out/autonone/run.log"
		if ! grep -aq "content: Halo MCC not found (paths.content; " "$log" ||
			grep -aq "content: the Halo content folder: " "$log" ||
			! grep -aq "memory: loaded levels.ui.ui" "$log"; then
			fail "autonone: no MCC was not said, or the menus did not come up ($out/autonone)"
		else
			echo "autonone: no Steam: \"Halo MCC not found\" said, the Xbox maps' menus up"
		fi
	fi
	;;
esac

[ "$status" = 0 ] && echo "PASS"
test_out_done "$status"
exit "$status"
