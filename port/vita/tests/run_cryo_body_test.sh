#!/bin/bash
# The Master Chief's body in a10's cryotube (issue #29: on the Xbox, looking
# down from the tube during the opening's look-around shows his chest, knees
# and boots; on the Vita nothing). The body there is not the player's biped,
# which the render passes over in first person on the Xbox as here, but part
# of the cryotube's own model: its super-high detail level (3000 pixels, only
# reached from inside the tube) carries the armour below the camera, the
# lower level does not. The Vita's "Model detail" setting (HALO_MODEL_LOD_SCALE,
# 0.5 by default) chose the tube's detail level as if it covered half its
# pixels and dropped to the level without the body; a model whose bounding
# sphere holds the camera now keeps the game's own level (models.c,
# model_sphere_holds_camera). The Linux harness (gxm-null: the Vita's
# renderer) logs each model's detail level (HALO_MODEL_LOD_LOG=1, models.c:
# "model detail: <model> level <n> (game <n>, <pixels> pixels, scale <s>...)"),
# and:
#
# 1. a10 at the Vita's 0.5 and at 0.35, the opening cinematic skipped: the
#    cryotube draws at level 4 (super-high, with the body), the game's level,
#    with the camera inside it, and at no lower level.
# 2. b30 at 0.5: the scale still lowers the detail of the models around (some
#    model draws below the game's level), the exemption is for the model
#    around the camera alone.
#
#   run_cryo_body_test.sh
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_OUT    where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_cryo_body_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
for map in a10 b30; do
	if [ ! -e "$data/maps/$map.map" ]; then
		echo "no $map.map in $data/maps (HALO_TEST_DATA)"
		exit 2
	fi
done

# run NAME MAP SECONDS COMMANDS [ENV...]: the harness on the map with the Vita's settings
# (as triage's run.sh), the test commands run
run() {
	local name=$1 map=$2 seconds=$3 commands=$4
	shift 4
	local dir="$out/$name"
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	printf 'map_name levels\\%s\\%s\n' "$map" "$map" > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_INTERPOLATION=false HALO_INTERPOLATE_FIRST_PERSON=1 HALO_STATIC_SCENERY=1 HALO_SCENERY_UPDATE_DIVISOR=4 \
		HALO_MIN_OBJECT_PIXELS=8 HALO_LIGHTING_REFRESH_DIVISOR=3 HALO_SOUND_OBSTRUCTION_TICKS=3 \
		HALO_MODEL_LOD_LOG=1 HALO_TEST_COMMANDS="$commands" "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	local code=$?
	# (the save folder fills with the game state: only the log is kept)
	rm -rf "$dir/save"
	[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
	grep -av "render assertion skipped" "$dir/run.log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" && fail "$name: an assertion or exception ($dir/run.log)"
}

# levels NAME MODEL: the "level game" pairs the model drew at, one a line
levels() {
	grep -a "model detail: $2 level" "$out/$1/run.log" | sed 's/.* level \([0-9]\) (game \([0-9]\).*/\1 \2/' | sort -u
}

# 1. the cryotube from inside, at the Vita's model detail settings
tube='levels\\a10\\devices\\cryotube\\cryotube'
for scale in 0.5 0.35; do
	name="a10-$scale"
	run "$name" a10 45 'L200:@skip' HALO_MODEL_LOD_SCALE=$scale
	if ! grep -aq "test command at tick 200: @skip" "$out/$name/run.log"; then
		fail "$name: the opening cinematic was not skipped ($out/$name/run.log)"
		continue
	fi
	drawn=$(levels "$name" "$tube" | tr '\n' ';')
	if [ -z "$drawn" ]; then
		fail "$name: the cryotube was not drawn ($out/$name/run.log)"
	elif levels "$name" "$tube" | awk '$1 != 4 || $2 != 4 { bad = 1 } END { exit bad }'; then
		echo "  ok: $name: the cryotube draws at level 4, the game's, with the body (level game: $drawn)"
	else
		fail "$name: the cryotube drew below its super-high level (level game: $drawn)"
	fi
	grep -aq "model detail: $tube level 4 .*camera inside: game level kept, scaled [0-3])" "$out/$name/run.log" &&
		echo "  ok: $name: the camera inside the tube kept the level the scale would have lowered" ||
		fail "$name: no 'camera inside' line for the cryotube at a lowered scaled level ($out/$name/run.log)"
done

# 2. the scale still lowers the detail elsewhere
name=b30-0.5
run "$name" b30 50 '30:(set cheat_deathless_player true);300:@tv beach_lz_rock' HALO_MODEL_LOD_SCALE=0.5
lowered=$(grep -a "model detail: " "$out/$name/run.log" | grep -av "camera inside" |
	sed 's/.* level \([0-9]\) (game \([0-9]\).*/\1 \2/' | awk '$1 < $2' | wc -l)
if [ "$lowered" -gt 0 ]; then
	echo "  ok: $name: the scale lowered $lowered model levels below the game's"
else
	fail "$name: no model drew below the game's level at 0.5 ($out/$name/run.log)"
fi

echo "logs: $out"
if [ "$status" = 0 ]; then
	echo "PASS"
fi
test_out_done $status
exit $status
