#!/bin/bash
# Far-reaching texture coordinates on the Vita (d3d8_gxm.c
# halo_d3d_texcoord_extent, nv2a_psh_cg.c): a30's ground and cliffs drew
# as coarse blocks and streaks of texels on the Vita (Oct 21 2026), their
# detail maps repeated 61 440 to 153 600 texels a unit of the base
# coordinates. The Linux harness (gxm-null: the Vita's renderer, which
# compiles nothing) collects the programs the draws make (HALO_SHADER_COLLECT):
#
# 1. a30 at its opening: some fragment programs fetch wrapped coordinates
#    ("- floor(xT"), the ground's and the cliffs' stages.
# 2. a30 with HALO_WRAP_TEXELS=0: none do.
# 3. a30 with the wrapped programs refused by the "compiler"
#    (HALO_NULL_REFUSE): the refusal is logged, and the draws go back to the
#    programs of case 2.
# 4. Blood Gulch: none do (its ground spans 30 720 texels a unit at most).
#
#   run_wrapped_coordinates_test.sh
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_OUT    where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_wrapped_coordinates_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
for map in a30 bloodgulch; do
	if [ ! -e "$data/maps/$map.map" ]; then
		echo "no $map.map in $data/maps (HALO_TEST_DATA)"
		exit 2
	fi
done

# run NAME MAP_PATH SECONDS COMMANDS [ENV...]: the harness on the map, its programs collected
run() {
	local name=$1 map=$2 seconds=$3 commands=$4
	shift 4
	local dir="$out/$name"
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save" "$dir/programs"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	printf 'map_name %s\n' "$map" > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_SHADER_COLLECT="$dir/programs" HALO_TEST_COMMANDS="$commands" "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	local code=$?
	rm -rf "$dir/save"
	[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
}

# wrapped NAME: how many of the run's fragment programs fetch wrapped coordinates
wrapped() {
	grep -l -- "- floor(xT" "$out/$1/programs/"*.fp.cg 2> /dev/null | wc -l
}

# (outside the crash pod, on the grass and toward the cliffs)
a30='L200:@skip;L500:@camera -10 32 62.2 270 -15;L600:@camera -10 32 62.2 0 -12;L700:@camera -10 32 62.2 180 -12;L800:@camera -10 32 62.2 90 -12'
run a30 'levels\a30\a30' 50 "$a30"
run a30-off 'levels\a30\a30' 50 "$a30" HALO_WRAP_TEXELS=0
run a30-refused 'levels\a30\a30' 50 "$a30" HALO_NULL_REFUSE='- floor(xT'
run bloodgulch 'levels\test\bloodgulch\bloodgulch' 30 '100:@camera 60 -110 3 45 -12;250:@camera 40 -60 3 270 -12'

count=$(wrapped a30)
[ "$count" -ge 2 ] && echo "  ok: a30: $count programs fetch wrapped coordinates" ||
	fail "a30: $count programs fetch wrapped coordinates, 2 or more expected"
count=$(wrapped a30-off)
[ "$count" = 0 ] && echo "  ok: a30 with HALO_WRAP_TEXELS=0: none" || fail "a30 with HALO_WRAP_TEXELS=0: $count"
if grep -aq "(wrapped coordinates) does not compile" "$out/a30-refused/run.log"; then
	echo "  ok: a30 refused: the refusal logged"
else
	fail "a30 refused: no refusal logged ($out/a30-refused/run.log)"
fi
missing=$(comm -23 <(ls "$out/a30-off/programs") <(ls "$out/a30-refused/programs") | wc -l)
[ "$missing" = 0 ] && echo "  ok: a30 refused: the draws went back to the plain programs" ||
	fail "a30 refused: $missing of the plain programs never made"
count=$(wrapped bloodgulch)
[ "$count" = 0 ] && echo "  ok: Blood Gulch: none" || fail "Blood Gulch: $count programs fetch wrapped coordinates"

echo "logs: $out"
if [ "$status" = 0 ]; then
	echo "PASS"
fi
test_out_done $status
exit $status
