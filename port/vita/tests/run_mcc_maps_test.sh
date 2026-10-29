#!/bin/bash
# Halo: CE Anniversary maps rebuilt as Custom Edition caches
# (port/tools/mcc_map_convert.py, docs/mcc_cea_maps.md) in the Linux harness
# (gxm-null). Each converted campaign map is loaded with its first cinematics
# skipped and played for HALO_TEST_MCC_SECONDS (120) of fixed ticks: it must
# load, its ticks must reach HALO_TEST_MCC_TICKS (3000), and no assertion or
# exception may be logged. Then the first map's campaign must go on when it
# is won: (game_won) on it must load the next level's map of its prefix
# (custom_edition_maps_campaign_next_level), when the folder has one.
#
#   run_mcc_maps_test.sh
#   HALO_TEST_VITA         the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA         a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_MCC_MAPS     the converted maps (<prefix><level>.map), or
#   HALO_TEST_MCC_MOD      a mod's folder (its maps/*.map: MCC's cache version
#   HALO_TEST_INVADER      13) and an Invader build to convert them with first
#                          (HALO_TEST_MCC_PREFIX, default "mod_")
#   HALO_TEST_CE_RESOURCES Halo PC's bitmaps.map, sounds.map and loc.map
#                          (default the data's maps folder, else Steam's MCC
#                          halo1/maps/custom_edition)
#   HALO_TEST_MCC_ONLY     a space-separated list of the maps to run (all)
#   HALO_TEST_OUT          where the logs go (deleted after a pass when the
#                          test made it; HALO_TEST_KEEP=1 keeps it)
# None of these maps are in the repository: without them it says SKIP and
# passes.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
seconds=${HALO_TEST_MCC_SECONDS:-120}
minimum_ticks=${HALO_TEST_MCC_TICKS:-3000}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_mcc_maps_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -e "$data/maps/ui.map" ]; then
	echo "no maps in $data/maps (HALO_TEST_DATA)"
	exit 2
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
maps=${HALO_TEST_MCC_MAPS:-}
if [ -z "$maps" ] && [ -n "${HALO_TEST_MCC_MOD:-}" ] && [ -n "${HALO_TEST_INVADER:-}" ]; then
	maps="$out/converted"
	python3 -I "$root/port/tools/mcc_map_convert.py" --invader "$HALO_TEST_INVADER" --output "$maps" \
		--prefix "${HALO_TEST_MCC_PREFIX:-mod_}" "$HALO_TEST_MCC_MOD"/maps/*.map > "$out/convert.log" 2>&1 ||
		fail "a map was not converted ($out/convert.log)"
fi
if [ -z "$maps" ] || ! ls "$maps"/*.map > /dev/null 2>&1; then
	echo "SKIP: no converted maps (HALO_TEST_MCC_MAPS, or HALO_TEST_MCC_MOD and HALO_TEST_INVADER)"
	test_out_done 0
	exit 0
fi
maps=$(cd "$maps" && pwd)

# run NAME LEVEL SECONDS COMMANDS: the harness, fixed ticks, the Vita's settings
run() {
	local name=$1 level=$2 run_seconds=$3 commands=$4
	local dir="$out/$name"
	rm -rf "$dir"
	mkdir -p "$dir/data/maps" "$dir/save"
	for file in "$(cd "$data" && pwd)"/maps/*.map "$maps"/*.map; do
		ln -sfn "$file" "$dir/data/maps/"
	done
	for file in "$maps"/*.bmp "$maps"/*.txt; do
		[ -e "$file" ] && ln -sfn "$file" "$dir/data/maps/"
	done
	for name_ in bitmaps sounds loc; do
		ln -sfn "$(cd "$resources" && pwd)/$name_.map" "$dir/data/maps/$name_.map"
	done
	printf 'game_difficulty_set hard\nmap_name %s\n' "$level" > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_EXIT_AFTER="$run_seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false HALO_CUSTOM_EDITION=1 \
		HALO_INTERPOLATION=false HALO_STATIC_SCENERY=1 HALO_SCENERY_UPDATE_DIVISOR=4 HALO_MIN_OBJECT_PIXELS=8 \
		HALO_MODEL_LOD_SCALE=0.5 HALO_LIGHTING_REFRESH_DIVISOR=3 HALO_SOUND_OBSTRUCTION_TICKS=3 \
		HALO_FIXED_DT=1 HALO_FIXED_TICK=1 HALO_TICK_THREAD=0 HALO_FRAME_CAP=0 HALO_FRAME_TIMING=300 \
		HALO_TEST_COMMANDS="$commands" \
		timeout -k 5 $((run_seconds + 180)) "$binary" > "$dir/run.log" 2>&1)
	local code=$?
	rm -rf "$dir/save"
	[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
	if cat "$dir/run.log" "$dir/data/debug.txt" 2>/dev/null | grep -av "render assertion skipped" |
		grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
		fail "$name: an assertion or exception ($dir)"
	fi
}

first=
for file in "$maps"/*.map; do
	map=$(basename "$file" .map)
	case " bitmaps sounds loc " in *" $map "*) continue ;; esac
	if [ -n "${HALO_TEST_MCC_ONLY:-}" ]; then
		case " $HALO_TEST_MCC_ONLY " in *" $map "*) ;; *) continue ;; esac
	fi
	[ -n "$first" ] || first=$map
	run "$map" "custom_maps\\$map" "$seconds" \
		'30:(set cheat_deathless_player true);60:@skip;300:@skip;600:@skip;900:@skip;1200:@skip'
	log="$out/$map/run.log"
	grep -aq "memory: loaded custom_maps\\\\$map" "$log" || { fail "$map was not loaded ($out/$map)"; continue; }
	ticks=$(grep -a "frame-timing: game tick" "$log" | tail -1 | sed -E 's/.*game tick ([0-9]+).*/\1/')
	[ "${ticks:-0}" -ge "$minimum_ticks" ] || fail "$map reached tick ${ticks:-0}, not $minimum_ticks ($log)"
	echo "$map: loaded, tick ${ticks:-0}; $(grep -a 'objects: ' "$log" | tail -1 | sed 's/.*objects: //')"
done

# the campaign goes on when its map is won
if [ -n "$first" ]; then
	prefix=${first%???}
	level=${first:${#first}-3}
	levels="a10 a30 a50 b30 b40 c10 c20 c40 d20 d40"
	next=$(echo "$levels" | tr ' ' '\n' | grep -A1 -x "$level" | sed -n 2p)
	if [ -n "$next" ] && [ -e "$maps/$prefix$next.map" ]; then
		run won "custom_maps\\$first" 60 'M300:(game_won);'
		grep -aq "memory: loaded custom_maps\\\\$prefix$next" "$out/won/run.log" ||
			fail "$first won did not go on to $prefix$next ($out/won)"
	fi
fi

[ "$status" = 0 ] && echo "PASS"
test_out_done "$status"
exit "$status"
