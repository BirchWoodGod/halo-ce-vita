#!/bin/bash
# A map file that cannot be played in the maps folder (port/linux/game/map_checks.c,
# menu_functions.c, main.c), on the Linux harness, Menu style Halo 3, with
# HALO_TEST_PROFILE's profile; the test's own maps folder (links to the data's
# maps) with beavercreek.map 0 bytes (Ben's insider.17 report, Oct 2026) and
# wizard.map cut short past its header (its header good: it fails as it loads):
#
#   forge   the Forge lobby (its packs and the map's blocks on): its map picker
#           shows Battle Creek as damaged, with the reason, and A does not
#           choose it; Wizard chosen and START FORGE: it fails to load, and the
#           game is back at the main menu - the main menu's map's first load
#           after it taken as failed too (HALO_TEST_UI_LOAD_FAILS, as Ben's
#           was): tried again, loaded; FORGE again: the lobby is not on Wizard,
#           and the picker shows Wizard as damaged now
#   mp      the multiplayer lobby, whose map is Battle Creek by default: on
#           Sidewinder instead, the live lobby open on SYSTEM LINK with it
#           (its network a namespace of its own, unshare -rn); the map picker
#           shows Battle Creek as damaged and does not choose it; Wizard
#           chosen: its precache fails, said; back to the main menu and
#           MULTIPLAYER again: the lobby opens live, its game type read
#
#   campaign  d20.map (Keyes) 0 bytes too: the campaign lobby's mission
#           picker shows why Keyes can't be played, and A does not choose it
#
#   run_broken_maps_test.sh [CASE...]   (default: forge mp campaign)
#   HALO_TEST_VITA         the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA_MENUS   a folder with the game's maps folder (the Xbox maps and
#                          Halo PC's bitmaps.map and loc.map)
#   HALO_TEST_PROFILE      a profile's blam.sav (default
#                          ../vita-save-backups/20261017-halo-profile/blam-unlocked.sav)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
profile=${HALO_TEST_PROFILE:-$root/../vita-save-backups/20261017-halo-profile/blam-unlocked.sav}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_broken_maps_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-forge mp campaign}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -r "$data/maps/wizard.map" ] || [ ! -r "$data/maps/bitmaps.map" ] || [ ! -r "$data/maps/loc.map" ]; then
	echo "SKIP: no maps in $data/maps (HALO_TEST_DATA_MENUS)"
	exit 0
fi
if [ ! -r "$profile" ]; then
	echo "SKIP: no profile ($profile, HALO_TEST_PROFILE)"
	exit 0
fi

# the test's maps folder: the data's maps, beavercreek.map empty, wizard.map
# its first 256 KB (never the data's own files)
mkdir -p "$out/maps"
for map in "$data"/maps/*; do
	ln -sfn "$(readlink -f "$map")" "$out/maps/$(basename "$map")"
done
rm -f "$out/maps/beavercreek.map" "$out/maps/wizard.map" "$out/maps/d20.map"
: > "$out/maps/beavercreek.map"
: > "$out/maps/d20.map"
head -c 262144 "$(readlink -f "$data/maps/wizard.map")" > "$out/maps/wizard.map"

# run NAME SECONDS NETWORK PAD [VAR=value...]: NETWORK 1 runs it in a network
# namespace of its own with a Wi-Fi of a dummy link
run() {
	local name=$1 seconds=$2 network=$3 pad=$4 dir="$out/$1"
	local wrap=()
	shift 4
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save/u/UDATA/869079979327"
	ln -sfn "$out/maps" "$dir/data/maps"
	cp "$profile" "$dir/save/u/UDATA/869079979327/blam.sav"
	[ -r "$(dirname "$profile")/SaveMeta.xbx" ] && cp "$(dirname "$profile")/SaveMeta.xbx" "$dir/save/u/UDATA/869079979327/"
	[ "$network" = 1 ] && wrap=(unshare -rn sh -c 'ip link set lo up && ip link add wifi0 type dummy &&
		ip addr add 10.98.0.1/24 dev wifi0 && ip link set wifi0 up && exec "$@"' sh)
	(cd "$dir" && exec "${wrap[@]}" env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false HALO_UI_LOG=1 \
		HALO_MENU_STYLE=halo3 HALO_MENUS_FOLDER="$root/port/vita/app0/menus/" "HALO_TEST_PAD=$pad" "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
}

# expect NAME TEXT PATTERN / refuse NAME TEXT PATTERN: a line matches / none does
expect() {
	if grep -aqE "$3" "$out/$1/run.log"; then echo "  ok: $1: $2"; else fail "$1: $2 ($out/$1/run.log)"; fi
}
refuse() {
	if grep -aqE "$3" "$out/$1/run.log"; then
		fail "$1: $2: $(grep -aE "$3" "$out/$1/run.log" | head -1)"
	else
		echo "  ok: $1: $2"
	fi
}
checked() {
	local code
	code=$(cat "$out/$1/exit" 2>/dev/null)
	[ "$code" = 0 ] || fail "$1: the harness exited with $code ($out/$1/run.log)"
	grep -av "render assertion skipped" "$out/$1/run.log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" &&
		fail "$1: an assertion or exception ($out/$1/run.log)"
	return 0
}

steps() { local n=$1 key=$2 p="" i; for i in $(seq "$n"); do p="$p $key:150:300"; done; echo "$p"; }

for case in $cases; do
	echo "--- $case"
	case $case in
	forge)
		# (FORGE; MAP: up nine to Battle Creek, A (refused), down ten to Wizard, A, A
		# (the map itself); START FORGE: back at the main menu; FORGE again, MAP:
		# the lobby's map, up one to Wizard)
		pad="wait:150:3000 down:150:600 down:150:600 a:150:3000 a:150:2000 $(steps 9 up) a:150:1000 $(steps 10 down)"
		pad="$pad a:150:1500 a:150:2000 down:150:500 down:150:500 a:150:300 wait:150:12000 down:150:600 down:150:600"
		pad="$pad a:150:3000 down:150:600 a:150:2000 up:150:1000 wait:150:2000"
		run forge 70 0 "$pad" HALO_TEST_UI_LOAD_FAILS=1
		checked forge
		expect forge "the picker shows Battle Creek damaged, its reason" \
			"menus: forge maps: Battle Creek: can't be played: beavercreek.map is damaged - copy it again"
		expect forge "A does not choose Battle Creek" "menus: forge maps: Battle Creek can't be chosen"
		expect forge "Wizard chosen and started" "menus: forge lobby: start levels.test.wizard.wizard"
		expect forge "Wizard fails to load, said" "Couldn't load Wizard: your wizard.map is cut short or damaged"
		expect forge "the main menu's map tried again" "new map: the main menu's map did not load: the map copy stopped, tried again"
		if sed -n '/Couldn.t load Wizard/,$p' "$out/forge/run.log" | grep -aq "ui: screen pc.h3.main.screen"; then
			echo "  ok: forge: back at the Halo 3 main menu"
		else
			fail "forge: not back at the main menu after Wizard failed"
		fi
		refuse forge "the main menu loads" "Couldn't load the main menu|could not be loaded again"
		expect forge "Wizard unusable in the lists from then on" "map checks: wizard.map failed to load: unusable in the map lists"
		if sed -n '/Couldn.t load Wizard/,$p' "$out/forge/run.log" | grep -aq "menus: forge lobby: Wizard$"; then
			fail "forge: the lobby opened on Wizard again"
		else
			echo "  ok: forge: the lobby opens on another map"
		fi
		expect forge "the picker shows Wizard damaged now" "menus: forge maps: Wizard: can't be played: wizard.map is damaged"
		;;
	mp)
		if ! unshare -rn true 2> /dev/null; then
			echo "SKIP: mp: no unprivileged network namespaces (unshare -rn)"; continue
		fi
		# (MULTIPLAYER; MAP (down two from NETWORK), up one to Battle Creek, A
		# (refused); down ten to Wizard, A; B: the main menu; MULTIPLAYER again)
		pad="wait:150:3000 down:150:700 a wait:150:6000 down:150:500 down:150:500 a:150:2000 up:150:600 a:150:1000"
		pad="$pad $(steps 10 down) a:150:4000 wait:150:2000 b:150:3000 wait:150:2000 a wait:150:8000"
		run mp 60 1 "$pad"
		checked mp
		expect mp "Battle Creek, the lobby's map, can't be: Sidewinder instead" \
			"menus: h3 mp: Battle Creek can't be played \\(beavercreek.map is damaged - copy it again\\): the lobby's map is Sidewinder"
		expect mp "the live lobby opens on Sidewinder" "menus: h3 live lobby: open on SYSTEM LINK \\(Slayer on Sidewinder\\)"
		expect mp "the picker shows Battle Creek damaged" "menus: h3 maps: Battle Creek .*can't be played: beavercreek.map is damaged"
		expect mp "A does not choose Battle Creek" "menus: h3 maps: Battle Creek can't be chosen"
		expect mp "Wizard chosen: its precache fails, said" "Couldn't load Wizard: your wizard.map is cut short or damaged"
		expect mp "MULTIPLAYER again: the lobby not on Wizard" \
			"menus: h3 mp: Wizard can't be played \\(wizard.map is damaged - copy it again\\): the lobby's map is Sidewinder"
		refuse mp "no game type unread" "the game type could not be read"
		refuse mp "Battle Creek never precached" "map: beavercreek could not be precached"
		if [ "$(grep -ac "menus: h3 live lobby: open on SYSTEM LINK" "$out/mp/run.log")" -ge 2 ]; then
			echo "  ok: mp: MULTIPLAYER again: the lobby opens live"
		else
			fail "mp: the lobby did not open live again ($out/mp/run.log)"
		fi
		;;
	campaign)
		# (CAMPAIGN, the profile, MISSION: down eight to Keyes, A (refused), B)
		run campaign 40 0 "wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:2500 $(steps 8 down) a:150:1500 b:150:2000"
		checked campaign
		expect campaign "the picker says why Keyes can't be played" \
			"menus: h3 missions: Keyes: can't be played: d20.map is damaged - copy it again"
		expect campaign "A does not choose Keyes" "menus: h3 campaign: Keyes can't be chosen"
		refuse campaign "the mission stays" "menus: h3 campaign: Keyes on"
		;;
	*)
		fail "no case $case" ;;
	esac
done

[ "$status" = 0 ] && echo "PASS" || echo "logs in $out"
test_out_done $status
exit $status
