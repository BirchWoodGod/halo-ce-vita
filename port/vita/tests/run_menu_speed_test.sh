#!/bin/bash
# The Halo 3 menus' scene packs never stall the game thread, and are ready
# before they are wanted (port/linux/game/scene_packs.c, menu_functions.c's
# h3 prebuild, menu_scene_objects.c), on the Linux harness, with
# HALO_TEST_PROFILE's profile:
#
#   cold      no pack built, no prebuild (HALO_MENU_PREBUILD=0), the packs'
#             own reads and writes slowed to a slow memory card's
#             (HALO_TEST_SLOW_IO=2048 KB/s): SELECT PROFILE, the campaign,
#             multiplayer and Forge lobbies each build and load their pack
#             on the worker; the game thread's part of each load under 5 ms,
#             no work of the packs' on the game thread of 50 ms or more (they
#             are logged from 16 ms: "scene packs: game thread: ..."), the
#             longest frame under 100 ms (a load on the game thread made
#             992 ms frames; leaving the multiplayer lobby, which hosts its
#             game, 1018 ms: the Xbox's second of sleep as its server went,
#             network_server_manager.c); every scene made, the card's Spartan
#             shown, the multiplayer scene's flag flying, the lobby's game
#             hosted and closed as it is left
#   prebuild  the main menu at rest builds every lobby's pack and the card's
#             in the background (Blood Gulch's two packs in one read) and
#             loads them; the visits after find them loaded: no build or
#             load while a lobby or SELECT PROFILE is open, every scene made
#   cached    a30 played first (its decompressed copy in the cache
#             partition), then the main menu's prebuild: menu_campaign read
#             from that copy in place, not inflated
#
#   run_menu_speed_test.sh [CASE...]   (default: cold prebuild cached)
#   HALO_TEST_VITA         the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA_MENUS   a folder with the game's maps folder (the Xbox maps
#                          and Halo PC's bitmaps.map and loc.map; skipped
#                          without a30 and bloodgulch)
#   HALO_TEST_PROFILE      a profile's blam.sav (its folder's SaveMeta.xbx
#                          beside it; skipped without)
#   HALO_TEST_OUT          where the logs go (deleted after a pass when the test
#                          made it; HALO_TEST_KEEP=1 keeps them)
#
# It runs in a network namespace of its own (unshare -rn, as the netns tests)
# with a LAN link of its own (a dummy one, 10.77.0.2): the multiplayer lobby
# hosts its System Link game there as on a Vita on Wi-Fi - with no network
# it says No Wi-Fi and hosts nothing - and no other harness on the machine
# holds the game's port (one did: "could not host a game", the lobby an error
# box). Without unprivileged user namespaces it runs as it is.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
if [ "${HALO_NETNS_INSIDE:-}" != 1 ] && unshare -rn true 2> /dev/null; then
	export HALO_NETNS_INSIDE=1
	exec unshare -rn --fork "$0" "$@"
fi
if [ "${HALO_NETNS_INSIDE:-}" = 1 ]; then
	ip link set lo up
	ip link add halo0 type dummy && ip addr add 10.77.0.2/24 dev halo0 && ip link set halo0 up
fi
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_menu_speed_test.$$}
export HALO_MENUS_FOLDER=${HALO_MENUS_FOLDER:-$root/port/vita/app0/menus/}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-cold prebuild cached}
status=0
fail() { echo "FAIL: $1"; status=1; }
expect() { # NAME TEXT PATTERN
	if grep -aqE "$3" "$out/$1/run.log"; then
		echo "  ok: $1: $2"
	else
		fail "$1: $2 ($out/$1/run.log)"
	fi
}

if [ ! -x "$binary" ]; then
	echo "no harness at $binary"
	exit 2
fi
if [ ! -r "$data/maps/a30.map" ] || [ ! -r "$data/maps/bloodgulch.map" ] || [ ! -r "$data/maps/bitmaps.map" ]; then
	echo "SKIP: no a30, bloodgulch and bitmaps.map in $data/maps (HALO_TEST_DATA_MENUS)"
	exit 0
fi
if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
	echo "SKIP: no HALO_TEST_PROFILE"
	exit 0
fi
mkdir -p "$out"

# run NAME SECONDS PAD [VAR=value...]: the harness at the Halo 3 main menu
# with the test profile (a save folder of the case's: save_from's copy when
# set; init.txt's line init_line)
run() {
	local name=$1 seconds=$2 pad=$3 dir="$out/$1"
	shift 3
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save/u/UDATA/869079979327"
	[ -n "${save_from:-}" ] && cp -a --reflink=auto "$save_from"/. "$dir/save/"
	cp "$HALO_TEST_PROFILE" "$dir/save/u/UDATA/869079979327/blam.sav"
	[ -r "$(dirname "$HALO_TEST_PROFILE")/SaveMeta.xbx" ] &&
		cp "$(dirname "$HALO_TEST_PROFILE")/SaveMeta.xbx" "$dir/save/u/UDATA/869079979327/"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	[ -n "${init_line:-}" ] && printf '%s\n' "$init_line" > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_UI_LOG=1 HALO_MENU_STYLE=halo3 HALO_FRAME_TIMING=10 "HALO_TEST_PAD=$pad" "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
}

checked() {
	local log="$out/$1/run.log" code
	code=$(cat "$out/$1/exit" 2>/dev/null)
	if [ "$code" != 0 ]; then
		fail "$1: the harness exited with $code ($log)"
		return 1
	fi
	if grep -av "render assertion skipped" "$log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
		fail "$1: an assertion or exception ($log)"
		return 1
	fi
	return 0
}

# (CAMPAIGN: SELECT PROFILE; A: the campaign lobby; B, B; down, A: MULTIPLAYER;
# B; down, A: FORGE; B)
visits="a wait:150:8000 a wait:150:8000 b wait:150:2500 b wait:150:3000 down:150:700 a wait:150:8000 b wait:150:3000 down:150:700 a wait:150:8000 b wait:150:3000"

scenes_made() { # NAME
	expect "$1" "SELECT PROFILE's card shows the Spartan" "menus: h3 profiles: the card shows the Spartan"
	expect "$1" "the campaign lobby's scene made" "h3 scene objects: h3/campaign/screen: made"
	expect "$1" "the multiplayer lobby's scene made" "h3 scene objects: h3/mp/screen: made"
	expect "$1" "the Forge lobby's scene made" "h3 scene objects: h3/forge/lobby: made"
	expect "$1" "the multiplayer scene's flag flies" "h3 scene object 1: its flag flies in a breeze"
	expect "$1" "the multiplayer lobby hosts its game" "menus: h3 live lobby: open on SYSTEM LINK"
	expect "$1" "leaving the lobby closes its game" "menus: h3 live lobby: closed \(the host left the lobby\)"
}

for c in $cases; do
	echo "--- $c"
	case $c in
	cold)
		run cold 75 "wait:150:3000 $visits" HALO_MENU_PREBUILD=0 HALO_TEST_SLOW_IO=2048
		checked cold || continue
		log=$out/cold/run.log
		grep -aE "scene packs: menu_.*(read and checked|loaded from)|scene packs: game thread" "$log" | cut -c1-160
		for pack in menu_spartan menu_campaign menu_multiplayer menu_monitor; do
			expect cold "$pack read and checked on the worker" "scene packs: $pack: read and checked on the worker"
			part=$(sed -n "s/.*scene packs: $pack: loaded from [^ ]* in \([0-9.]*\) ms (the game thread's part).*/\1/p" "$log" | head -1)
			if [ -n "$part" ] && awk "BEGIN { exit !($part < 5) }"; then
				echo "  ok: cold: $pack's game thread part $part ms"
			else
				fail "cold: $pack's game thread part: ${part:-none} ms ($log)"
			fi
		done
		worst=$(sed -n 's/.*scene packs: game thread: .* took \([0-9.]*\) ms.*/\1/p' "$log" | sort -n | tail -1)
		if [ -z "$worst" ] || awk "BEGIN { exit !($worst < 50) }"; then
			echo "  ok: cold: no pack work of 50 ms or more on the game thread (longest logged: ${worst:-none})"
		else
			fail "cold: the packs held the game thread $worst ms ($log)"
		fi
		# (the frames after the main menu came up: the first windows load ui.map)
		frame=$(awk '/ui: screen pc.h3.main.screen/ { on = 1 } on && /frame-timing:/ { if (match($0, /max [0-9.]+/)) print substr($0, RSTART + 4, RLENGTH - 4) }' "$log" | sort -n | tail -1)
		if [ -n "$frame" ] && awk "BEGIN { exit !($frame < 100) }"; then
			echo "  ok: cold: the longest frame at the menus $frame ms"
		else
			fail "cold: the longest frame at the menus ${frame:-unknown} ms ($log)"
		fi
		scenes_made cold ;;
	prebuild)
		run prebuild 95 "wait:150:30000 $visits"
		checked prebuild || continue
		log=$out/prebuild/run.log
		grep -aE "h3 prebuild|scene packs: menu_.*(built|loaded from)" "$log" | cut -c1-160
		expect prebuild "the prebuild asked for at the main menu at rest" "menus: h3 prebuild: the main menu at rest: .*menu_spartan.*menu_campaign"
		expect prebuild "Blood Gulch's two packs in one read" "scene packs: menu_(spartan|multiplayer): built from bloodgulch .* with other packs in the same read"
		# (everything built and loaded before SELECT PROFILE opened; nothing after)
		visit=$(grep -an "ui: screen pc.h3.profiles.screen" "$log" | head -1 | cut -d: -f1)
		for pack in menu_spartan menu_campaign menu_multiplayer menu_monitor; do
			line=$(grep -an "scene packs: $pack: loaded from" "$log" | head -1 | cut -d: -f1)
			if [ -n "$line" ] && [ -n "$visit" ] && [ "$line" -lt "$visit" ]; then
				echo "  ok: prebuild: $pack loaded at the main menu"
			else
				fail "prebuild: $pack was not loaded before the visits ($log)"
			fi
		done
		if [ -n "$visit" ] && tail -n +"$visit" "$log" | grep -aqE "scene packs: menu_.*(building it|read and checked)"; then
			fail "prebuild: a menu pack was built or read during the visits ($log)"
		else
			echo "  ok: prebuild: the visits found every pack loaded"
		fi
		scenes_made prebuild ;;
	cached)
		init_line='map_name levels\a30\a30' run cached_a30 25 ""
		checked cached_a30 || continue
		save_from=$out/cached_a30/save run cached 35 "wait:150:30000"
		checked cached || continue
		grep -aE "scene packs: menu_campaign: (building|built)" "$out/cached/run.log" | cut -c1-160
		expect cached "menu_campaign read from a30's copy in place" "scene packs: menu_campaign: building it \(.*\) from a30, read in place"
		expect cached "menu_campaign built" "scene packs: menu_campaign: built from a30" ;;
	*)
		fail "no case $c" ;;
	esac
done

echo "logs in $out"
test_out_done $status
exit $status
