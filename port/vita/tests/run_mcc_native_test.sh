#!/bin/bash
# Halo: CE Anniversary maps (MCC's version 13) played natively from a mod's
# folder in the data root's mods folder (custom_edition_mods.c,
# docs/mcc_cea_maps.md), in the Linux harness (gxm-null), with no converter:
# the mod's files are linked (read only) into mods/<name>, as a player copies
# its folder to ux0:data/haloce-vita/mods/<name>/. Each of its maps is loaded as the
# campaign's level list loads it (mods\<key>_<map>), its first cinematics
# skipped, and played for HALO_TEST_MCC_SECONDS (120) of fixed ticks: it must
# load, its ticks must reach HALO_TEST_MCC_TICKS (3000), and no assertion or
# exception may be logged. Then the first map's campaign must go on when it is
# won ((game_won) loads the next map its campaign lists), and a version 13 map
# anywhere but a mod's folder (its first map linked into the maps folder, as a
# Custom Edition campaign map would be) must be refused.
#
#   run_mcc_native_test.sh
#   HALO_TEST_VITA         the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA         a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_MCC_MOD      a mod's folder (its maps/*.map, CampaignInfo.json ...:
#                          default Ruby's Rebalanced Halo: CE in Steam's
#                          workshop folder, Workshop 2915693904)
#   HALO_TEST_MCC_NAME     the mod's folder name in the mods folder (ruby)
#   HALO_TEST_MCC_ONLY     a space-separated list of the maps to run (all)
#   HALO_TEST_MCC_ENV      more VAR=value settings for every run (the memory
#                          a Vita has: HALO_USER_FREE_MB=..., HALO_CDRAM_RESERVE_MB=...)
#   HALO_TEST_OUT          where the logs go (deleted after a pass when the
#                          test made it; HALO_TEST_KEEP=1 keeps it)
# No mod's files are in the repository: without one it says SKIP and passes.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
mod=${HALO_TEST_MCC_MOD:-$HOME/.local/share/Steam/steamapps/workshop/content/976730/2915693904}
name=${HALO_TEST_MCC_NAME:-ruby}
seconds=${HALO_TEST_MCC_SECONDS:-120}
minimum_ticks=${HALO_TEST_MCC_TICKS:-3000}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_mcc_native_test.$$}
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
if ! ls "$mod"/maps/*.map > /dev/null 2>&1; then
	echo "SKIP: no mod's maps in $mod/maps (HALO_TEST_MCC_MOD)"
	exit 0
fi
mod=$(cd "$mod" && pwd)
# the mod's key, as custom_edition_mods.c makes it of its folder's name
key=$(printf '%s' "$name" | tr 'A-Z' 'a-z' | tr -cd 'a-z0-9-' | cut -c1-12)
mkdir -p "$out"

# run NAME LEVEL SECONDS COMMANDS: the harness, fixed ticks, the Vita's
# settings, the mod's files linked into the mods folder
run() {
	local run_name=$1 level=$2 run_seconds=$3 commands=$4
	local dir="$out/$run_name"
	rm -rf "$dir"
	mkdir -p "$dir/data/maps" "$dir/data/mods/$name" "$dir/save"
	for file in "$(cd "$data" && pwd)"/maps/*.map; do
		ln -sfn "$file" "$dir/data/maps/"
	done
	# (the mod's folder made here, its files and folders linked into it: the
	# game writes a mod's sound cache files into its folder's cache folder,
	# custom_edition_sounds.c, never into Steam's)
	for entry in "$mod"/*; do
		ln -sfn "$entry" "$dir/data/mods/$name/"
	done
	[ -n "${extra_map:-}" ] && ln -sfn "$extra_map" "$dir/data/maps/$extra_map_name"
	printf 'game_difficulty_set hard\nmap_name %s\n' "$level" > "$dir/data/init.txt"
	# shellcheck disable=SC2086
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_EXIT_AFTER="$run_seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false HALO_CUSTOM_EDITION=1 \
		HALO_INTERPOLATION=false HALO_STATIC_SCENERY=1 HALO_SCENERY_UPDATE_DIVISOR=4 HALO_MIN_OBJECT_PIXELS=8 \
		HALO_MODEL_LOD_SCALE=0.5 HALO_LIGHTING_REFRESH_DIVISOR=3 HALO_SOUND_OBSTRUCTION_TICKS=3 \
		HALO_FIXED_DT=1 HALO_FIXED_TICK=1 HALO_TICK_THREAD=0 HALO_FRAME_CAP=0 HALO_FRAME_TIMING=300 \
		HALO_TEST_COMMANDS="$commands" ${HALO_TEST_MCC_ENV:-} \
		timeout -k 5 $((run_seconds + 180)) "$binary" > "$dir/run.log" 2>&1)
	local code=$?
	rm -rf "$dir/save"
	[ "$code" = 0 ] || fail "$run_name: the harness exited with $code ($dir/run.log)"
	if cat "$dir/run.log" "$dir/data/debug.txt" 2>/dev/null | grep -av "render assertion skipped" |
		grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
		fail "$run_name: an assertion or exception ($dir)"
	fi
}

first=
for file in "$mod"/maps/*.map; do
	stem=$(basename "$file" .map | tr 'A-Z' 'a-z')
	map=${key}_$stem
	if [ -n "${HALO_TEST_MCC_ONLY:-}" ]; then
		case " $HALO_TEST_MCC_ONLY " in *" $stem "*|*" $map "*) ;; *) continue ;; esac
	fi
	[ -n "$first" ] || first=$map
	run "$map" "mods\\$map" "$seconds" \
		'30:(set cheat_deathless_player true);60:@skip;300:@skip;600:@skip;900:@skip;1200:@skip'
	log="$out/$map/run.log"
	grep -aq "memory: loaded mods\\\\$map" "$log" || { fail "$map was not loaded ($out/$map)"; continue; }
	ticks=$(grep -a "frame-timing: game tick" "$log" | tail -1 | sed -E 's/.*game tick ([0-9]+).*/\1/')
	[ "${ticks:-0}" -ge "$minimum_ticks" ] || fail "$map reached tick ${ticks:-0}, not $minimum_ticks ($log)"
	echo "$map: loaded, tick ${ticks:-0}; $(grep -a "custom edition: Halo: CE Anniversary:" "$out/$map/data/debug.txt" | tail -1 |
		sed -E 's/.*Anniversary: ([0-9]+) script.* ([0-9]+) object names moved.* ([0-9]+) past.*/\1 nodes, \2 names moved, \3 dropped/');" \
		"$(grep -a "^halo-linux: memory: loaded mods" "$log" | tail -1 | sed -E 's/.*(C heap [0-9]+ KB).*/\1/')"
	echo "  $(grep -a 'objects: ' "$log" | tail -1 | sed 's/.*objects: //')"
done

if [ -n "$first" ]; then
	# the campaign goes on when its map is won, to the next its campaign lists
	run won "mods\\$first" 60 'M300:(game_won);'
	next=$(grep -a "won 'mods" "$out/won/data/debug.txt" | tail -1 | sed -E "s/.*on to 'mods\\\\([^']*)'.*/\\1/")
	if [ -z "$next" ] || ! grep -aq "memory: loaded mods\\\\$next" "$out/won/run.log"; then
		fail "$first won did not go on to its campaign's next map ($out/won)"
	else
		echo "won $first: on to $next"
	fi

	# a version 13 map anywhere but a mod's folder is refused (the first one
	# linked into the maps folder, where map sharing writes downloads)
	stem=${first#"${key}_"}
	extra_map="$mod/maps/$(cd "$mod/maps" && ls | grep -ix "$stem.map" | head -1)" extra_map_name="${key}x.map" \
		run outside "custom_maps\\${key}x" 30 ''
	if grep -aq "memory: loaded custom_maps" "$out/outside/run.log" ||
		! grep -aq "is a Halo: CE Anniversary map (build .*): this build plays one only from a mod's folder" "$out/outside/data/debug.txt"; then
		fail "a Halo: CE Anniversary map in the maps folder was not refused ($out/outside)"
	else
		echo "${key}x (a version 13 map in the maps folder): refused"
	fi
fi

[ "$status" = 0 ] && echo "PASS"
test_out_done "$status"
exit "$status"
