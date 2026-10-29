#!/bin/bash
# MCC's PC multiplayer maps (bloodgulch, dangercanyon, deathisland,
# gephyrophobia, icefields, infinity, timberland in MCC's halo1/maps),
# converted by port/tools/mcc_map_convert.py (--prefix pc_, --mcc-maps,
# --sound-maps: docs/mcc_cea_maps.md), played in local games with the
# offline bots in the Linux harness (gxm-null), with PC map vehicles
# "allowed" (game.pc_map_vehicles, HALO_PC_MAP_VEHICLES:
# port/linux/game/custom_edition_objects.c). Each map's Slayer game must
# load and play HALO_TEST_SECONDS (75) with no assertion or exception; a
# map that has Banshees (Blood Gulch, Death Island, Gephyrophobia,
# Infinity) must place them, and this machine's player, seated in one
# (HALO_NETWORK_TEST_PLAYER_VEHICLE), must fly it while riding: 10 units or
# more from where it got in and 1 or more above, or 2 or more above (the
# scripted player is no pilot: a Banshee that meets a wall, as Infinity's
# do in its bases' hangars, climbs and gets no further); a map with none must
# place none; and every map's fuel rods (Halo PC's plasma_cannon) must lie
# about. Then Blood Gulch's rules: "xbox" (the default) places no Banshee
# and no Rocket Warthog, "pc" its Rocket Warthogs but not its Banshees
# (only allowed there).
#
#   run_mcc_pc_maps_test.sh
#   HALO_TEST_VITA          the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA          a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_MCC_PC_MAPS   the converted maps (pc_<map>.map), or
#   HALO_TEST_MCC_PC_SOURCE MCC's halo1/maps and an Invader build
#   HALO_TEST_INVADER       (HALO_TEST_INVADER) to convert them with first
#                           (the Xbox maps of HALO_TEST_DATA give the sounds)
#   HALO_TEST_CE_RESOURCES  Halo PC's bitmaps.map, sounds.map and loc.map
#                           (default the data's maps folder, else Steam's MCC
#                           custom_edition folder)
#   HALO_TEST_MCC_ONLY      a space-separated list of the maps to run (all)
#   HALO_TEST_SECONDS       seconds of play (default 75)
#   HALO_TEST_JOBS          games at once (default a quarter of the CPUs)
#   HALO_TEST_OUT           where the logs go (deleted after a pass when the
#                           test made it; HALO_TEST_KEEP=1 keeps it)
# No map is in the repository: without converted maps it says SKIP and passes.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
seconds=${HALO_TEST_SECONDS:-75}
jobs=${HALO_TEST_JOBS:-$(( ($(nproc) + 3) / 4 ))}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_mcc_pc_maps_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
status=0
fail() { echo "FAIL: $1"; status=1; }
# the stock maps' Banshees (MCC's, October 2026); a map not named here is
# not checked for them
banshee_maps="bloodgulch deathisland gephyrophobia infinity"
no_banshee_maps="dangercanyon icefields timberland"

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -e "$data/maps/ui.map" ]; then
	echo "no maps in $data/maps (HALO_TEST_DATA)"
	exit 2
fi
if ! unshare -rn true 2> /dev/null; then
	echo "SKIP: no unprivileged network namespaces (unshare -rn)"
	exit 0
fi
resources=${HALO_TEST_CE_RESOURCES:-}
if [ -z "$resources" ]; then
	resources="$data/maps"
	[ -e "$resources/bitmaps.map" ] ||
		resources="$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1/maps/custom_edition"
fi
for name in bitmaps sounds loc; do
	if [ ! -e "$resources/$name.map" ]; then
		echo "SKIP: no Halo PC $name.map (HALO_TEST_CE_RESOURCES)"
		exit 0
	fi
done

mkdir -p "$out"
maps=${HALO_TEST_MCC_PC_MAPS:-}
if [ -z "$maps" ] && [ -n "${HALO_TEST_MCC_PC_SOURCE:-}" ] && [ -n "${HALO_TEST_INVADER:-}" ]; then
	maps="$out/converted"
	sources=()
	for name in bloodgulch dangercanyon deathisland gephyrophobia icefields infinity timberland; do
		[ -e "$HALO_TEST_MCC_PC_SOURCE/$name.map" ] && sources+=("$HALO_TEST_MCC_PC_SOURCE/$name.map")
	done
	[ ${#sources[@]} -gt 0 ] && { python3 -I "$root/port/tools/mcc_map_convert.py" --invader "$HALO_TEST_INVADER" \
		--output "$maps" --prefix pc_ --mcc-maps "$HALO_TEST_MCC_PC_SOURCE" --sound-maps "$data/maps" "${sources[@]}" \
		> "$out/convert.log" 2>&1 || fail "a map was not converted ($out/convert.log)"; }
fi
if [ -z "$maps" ] || ! ls "$maps"/pc_*.map > /dev/null 2>&1; then
	echo "SKIP: no converted maps (HALO_TEST_MCC_PC_MAPS, or HALO_TEST_MCC_PC_SOURCE and HALO_TEST_INVADER)"
	test_out_done 0
	exit 0
fi
maps=$(cd "$maps" && pwd)

# run NAME MAP [VAR=value...]: a local Slayer game with four peaceful bots
run() {
	local name=$1 map=$2 file
	shift 2
	local dir="$out/$name"
	rm -rf "$dir"
	mkdir -p "$dir/data/maps" "$dir/save"
	for file in "$(cd "$data" && pwd)"/maps/*.map; do
		ln -sfn "$file" "$dir/data/maps/"
	done
	ln -sfn "$maps/$map.map" "$dir/data/maps/$map.map"
	for file in bitmaps sounds loc; do
		ln -sfn "$(cd "$resources" && pwd)/$file.map" "$dir/data/maps/$file.map"
	done
	(cd "$dir" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
		env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=$((seconds + 20)) \
		HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false \
		HALO_NET_ONLINE=false HALO_CUSTOM_EDITION=1 HALO_NETWORK_TEST="local:$map:slayer" HALO_NETWORK_TEST_START=5 \
		HALO_NETWORK_TEST_SCORE=500 HALO_TEST_INPUT=bot:1 HALO_BOTS=4 HALO_BOT_PEACEFUL=1 "$@" \
		timeout -k 5 $((seconds + 150)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
	rm -rf "$dir/save"
}

# count NAME PATTERN: the vehicles of tags matching PATTERN the game began
# with (the network test's census at tick 30)
count() {
	grep -a "network test: vehicles at tick 30:" "$out/$1/run.log" | head -1 | grep -oE "$2\([0-9a-f]+\) [0-9]+" |
		awk '{ n += $2 } END { print n + 0 }'
}

# checked NAME MAP: the game loaded, played, and logged no assertion
checked() {
	local log="$out/$1/run.log"
	[ "$(cat "$out/$1/exit" 2> /dev/null)" = 0 ] || { fail "$1: the harness exited with $(cat "$out/$1/exit" 2> /dev/null) ($log)"; return 1; }
	grep -aq "network test: map $2\$" "$log" && grep -aq "network test: tick 900 player 0" "$log" ||
		{ fail "$1: $2 was not played ($log)"; return 1; }
	if cat "$log" "$out/$1/data/debug.txt" 2> /dev/null | grep -av "render assertion skipped" |
		grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
		fail "$1: an assertion or exception ($out/$1)"
	fi
	return 0
}

# flown NAME: the first player's ride from its seat in the vehicle: the
# farthest it got (ground distance) and the most it rose
flown() {
	grep -a "network test: tick [0-9]* player 0: ([^)]*) [^ ]* riding" "$out/$1/run.log" |
		sed -E 's/.*player 0: \(([-0-9.]+) ([-0-9.]+) ([-0-9.]+)\).*/\1 \2 \3/' |
		awk 'NR == 1 { x = $1; y = $2; z = $3 } { d = sqrt(($1 - x) ^ 2 + ($2 - y) ^ 2); if (d > far) far = d; if ($3 - z > up) up = $3 - z }
			END { printf "%.1f %.1f %d\n", far, up, NR }'
}

# the games, $jobs at once
names=""
for file in "$maps"/pc_*.map; do
	map=$(basename "$file" .map)
	stock=${map#pc_}
	if [ -n "${HALO_TEST_MCC_ONLY:-}" ]; then
		case " $HALO_TEST_MCC_ONLY " in *" $map "* | *" $stock "*) ;; *) continue ;; esac
	fi
	names="$names $map"
done
pids=""
started=0
for map in $names; do
	stock=${map#pc_}
	case " $banshee_maps " in
	*" $stock "*) run "$map" "$map" HALO_PC_MAP_VEHICLES=all HALO_NETWORK_TEST_PLAYER_DRIVES=10 \
		HALO_NETWORK_TEST_PLAYER_VEHICLE=banshee & ;;
	*) run "$map" "$map" HALO_PC_MAP_VEHICLES=all & ;;
	esac
	pids="$pids $!"
	started=$((started + 1))
	[ $((started % jobs)) = 0 ] && { wait $pids; pids=""; }
done
if [ -e "$maps/pc_bloodgulch.map" ] && { [ -z "${HALO_TEST_MCC_ONLY:-}" ] ||
	case " $HALO_TEST_MCC_ONLY " in *" pc_bloodgulch "* | *" bloodgulch "*) true ;; *) false ;; esac; }; then
	run rule-xbox pc_bloodgulch &
	pids="$pids $!"
	run rule-pc pc_bloodgulch HALO_PC_MAP_VEHICLES=pc &
	pids="$pids $!"
	rules=1
fi
wait $pids

for map in $names; do
	stock=${map#pc_}
	checked "$map" "$map" || continue
	log="$out/$map/run.log"
	banshees=$(count "$map" 'banshee_mp')
	vehicles=$(grep -a "network test: vehicles at tick 30:" "$log" | head -1 | sed 's/.*tick 30: //')
	fuel_rods=$(grep -a "network test: items at tick 30" "$log" | head -1 |
		grep -oE 'plasma_cannon\\plasma_cannon\([0-9a-f]+\) [0-9]+' | awk '{ split($2, n, "+"); s += n[1] } END { print s + 0 }')
	grep -aq "custom edition: PC map vehicles in this local game: all the map allows" "$log" ||
		fail "$map: PC map vehicles not taken as allowed ($log)"
	[ "${fuel_rods:-0}" -ge 1 ] || fail "$map: no fuel rod lying about ($log)"
	case " $banshee_maps " in
	*" $stock "*)
		[ "$banshees" -ge 1 ] || fail "$map: no Banshee placed ($vehicles)"
		grep -aq "network test: the first player drives vehicle" "$log" || fail "$map: the player got in no Banshee ($log)"
		read -r far up samples <<< "$(flown "$map")"
		awk -v f="$far" -v u="$up" 'BEGIN { exit !((f >= 10 && u >= 1) || u >= 2) }' ||
			fail "$map: the Banshee was not flown ($far units off, $up up, $samples samples riding; $log)"
		echo "$map: $banshees Banshees, $fuel_rods fuel rods; the player flew one $far units, up $up"
		;;
	*)
		case " $no_banshee_maps " in *" $stock "*) [ "$banshees" = 0 ] || fail "$map: $banshees Banshees placed" ;; esac
		echo "$map: $fuel_rods fuel rods; vehicles: $vehicles"
		;;
	esac
done
if [ -n "${rules:-}" ]; then
	for rule in xbox pc; do
		checked "rule-$rule" pc_bloodgulch || continue
		banshees=$(count "rule-$rule" 'banshee_mp')
		rockets=$(count "rule-$rule" 'rwarthog')
		warthogs=$(count "rule-$rule" 'mp_warthog')
		case $rule in
		xbox) [ "$banshees" = 0 ] && [ "$rockets" = 0 ] && [ "$warthogs" -ge 1 ] ||
			fail "Blood Gulch, xbox: $banshees Banshees, $rockets Rocket Warthogs, $warthogs Warthogs (no Banshee or Rocket Warthog wanted)" ;;
		pc) [ "$banshees" = 0 ] && [ "$rockets" -ge 1 ] ||
			fail "Blood Gulch, pc: $banshees Banshees, $rockets Rocket Warthogs (its Rocket Warthogs, not its allowed-only Banshees, wanted)" ;;
		esac
		echo "Blood Gulch, $rule: $warthogs Warthogs, $rockets Rocket Warthogs, $banshees Banshees"
	done
fi

[ "$status" = 0 ] && echo "PASS"
test_out_done "$status"
exit "$status"
