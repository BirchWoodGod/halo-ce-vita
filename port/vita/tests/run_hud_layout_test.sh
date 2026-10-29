#!/bin/bash
# HUD layout in both aspect ratios (Oct 8 2026: on the owner's Vita, Firefight
# Airlock's ODST visor drew at half its size in 4:3 and 16:9, a quarter of the
# screen in the middle; its 1024x1024 bitmaps lie outside ui\, so the Custom
# Edition loader drew them from their second level, and the HUD placed them by
# that level's size). The Linux harness (gxm-null: the Vita's renderer) logs
# each HUD quad the first time it is drawn at a place and size (HALO_HUD_LOG=1,
# hud_draw.c: "hud quad: <bitmap> x0 y0 x1 y1 (screen <width>)"), and:
#
# 1. b30 (the Xbox's HUD) at 640 and 848 columns: at 640 the corner elements
#    sit on the Xbox's title-safe frame (48 columns from the sides, 36 rows from
#    the top and bottom: the shield panel's right edge at 592, the motion
#    sensor's left at 48 and bottom at 444); at 848 the frame widens in
#    proportion (63 columns: 785, 63), the rows stay; the crosshair is centred.
# 2. With firefight-airlock.map and Halo PC's resource maps (none are in the
#    repository; else SKIP for this part): the visor (odst_hud\odst final, a
#    1024-pixel frame at 0.625 and 0.475) fills the 640 columns and 486 rows in
#    4:3, and the 848 columns in 16:9 (hud_full_screen_element_widen).
# 3. Halo: CE Anniversary's HUD (Oct 10 2026: on the owner's Vita, Ruby's
#    Rebalanced Halo: CE's HUD drew twice its size and pushed in from the
#    corners; MCC lays it out on a canvas twice the Xbox's, its bitmaps four
#    times the Xbox's with Halo PC's half HUD scale): the mod's b30 played
#    natively from the mods folder (HALO_TEST_MCC_MOD, default Ruby's in
#    Steam's workshop folder; else SKIP for this part), and when
#    HALO_TEST_MCC_MAPS names a folder of maps port/tools/mcc_map_convert.py
#    made, its *b30.map too, at 640 and 848 columns: drawn at the Xbox b30's
#    size and place (the shield panel 128x32 on the frame, the crosshair 66
#    wide and centred, the digits 12 rows), the motion sensor 128 wide, 10 in
#    from the frame's corner (MCC's own margin, 20 on its canvas); its
#    meters (MCC's "use xbox shading") keep their channels, not swapped as
#    Halo PC's (swapped, they drew only full or empty).
#
#   run_hud_layout_test.sh
#   HALO_TEST_VITA          the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA          a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_CE_MAPS       a folder with firefight-airlock.map (default
#                           ../custom-maps-dl beside the tree)
#   HALO_TEST_CE_RESOURCES  Halo PC's bitmaps.map, sounds.map and loc.map
#                           (default the data's maps folder, else Steam's MCC
#                           halo1/maps/custom_edition)
#   HALO_TEST_MCC_MOD       a Halo: CE Anniversary mod's folder (its maps\b30.map)
#   HALO_TEST_MCC_MAPS      a folder of maps converted from it (optional)
#   HALO_TEST_OUT           where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
ce_maps=${HALO_TEST_CE_MAPS:-$root/../custom-maps-dl}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_hud_layout_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -e "$data/maps/b30.map" ]; then
	echo "no b30.map in $data/maps (HALO_TEST_DATA)"
	exit 2
fi

# run NAME WIDTH SECONDS INIT_LINE [ENV...]: the harness on a map from init.txt
# (the maps of extra_maps linked beside the Xbox maps)
extra_maps=()
run() {
	local name=$1 width=$2 seconds=$3 init=$4
	shift 4
	local dir="$out/$name"
	rm -rf "$dir"
	mkdir -p "$dir/data/maps" "$dir/save"
	for file in "$(cd "$data" && pwd)"/maps/*.map; do
		ln -sfn "$file" "$dir/data/maps/"
	done
	for file in "${extra_maps[@]}"; do
		ln -sfn "$file" "$dir/data/maps/"
	done
	if [ -n "${mods_folder:-}" ]; then
		mkdir -p "$dir/data/mods"
		ln -sfn "$mods_folder" "$dir/data/mods/ruby"
	fi
	printf '%s\n' "$init" > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_HUD_LOG=1 HALO_DISPLAY_WIDTH="$width" "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	local code=$?
	# (the save folder fills with the game state: only the log is kept)
	rm -rf "$dir/save"
	[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
	grep -av "render assertion skipped" "$dir/run.log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" && fail "$name: an assertion or exception ($dir/run.log)"
	grep -aq "screen: ${width}x480" "$dir/run.log" || fail "$name: the screen was not ${width} columns wide ($dir/run.log)"
}

# quads NAME BITMAP: the quads drawn from the bitmap whose tag name ends so, "x0 y0 x1 y1" a line
quads() {
	# (a tag name may hold spaces: the last four numbers before "(screen")
	grep -a "hud quad: .*$2 -\?[0-9]" "$out/$1/run.log" | sed 's/ (screen.*//' |
		awk '{ print $(NF - 3), $(NF - 2), $(NF - 1), $NF }' | sort -u
}

# expect NAME WHAT AWK_CONDITION BITMAP: some quad of the bitmap meets the condition (on x0 y0 x1 y1)
expect() {
	local name=$1 what=$2 condition=$3 bitmap=$4
	if quads "$name" "$bitmap" | awk "{ x0 = \$1; y0 = \$2; x1 = \$3; y1 = \$4 } ($condition) { found = 1 } END { exit !found }"; then
		echo "  ok: $name: $what"
	else
		fail "$name: $what (quads of $bitmap: $(quads "$name" "$bitmap" | tr '\n' ';'))"
	fi
}

# 1. the Xbox's HUD
for width in 640 848; do
	run "b30-$width" "$width" 60 'map_name levels\b30\b30'
	margin=$((48 * width / 640))
	name="b30-$width"
	[ "$(grep -ac "hud quad" "$out/$name/run.log")" -gt 5 ] || fail "$name: no HUD drawn ($out/$name/run.log)"
	expect "$name" "the shield panel's right edge on the frame ($((width - margin)))" \
		"x1 == $((width - margin)) && y0 == 37" 'hud_unit_backgrounds'
	expect "$name" "the motion sensor at the bottom left corner of the frame ($margin, 444)" \
		"x0 == $margin && y1 == 444" 'hud_unit_backgrounds'
	expect "$name" "the crosshair centred ($((width / 2)))" \
		"x0 + x1 == $width" 'hud_reticles'
done

# 2. a Custom Edition HUD from bitmaps the loader reduced
resources=${HALO_TEST_CE_RESOURCES:-}
if [ -z "$resources" ]; then
	resources="$data/maps"
	[ -e "$resources/bitmaps.map" ] ||
		resources="$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1/maps/custom_edition"
fi
missing=
[ -e "$ce_maps/firefight-airlock.map" ] || missing="firefight-airlock.map (HALO_TEST_CE_MAPS)"
for name in bitmaps sounds loc; do
	[ -e "$resources/$name.map" ] || missing="$missing $name.map (HALO_TEST_CE_RESOURCES)"
done
if [ -n "$missing" ]; then
	echo "SKIP (the Custom Edition part): no $missing"
else
	extra_maps=("$(cd "$ce_maps" && pwd)/firefight-airlock.map")
	for name in bitmaps sounds loc; do
		extra_maps+=("$(cd "$resources" && pwd)/$name.map")
	done
	for width in 640 848; do
		name="airlock-$width"
		run "$name" "$width" 50 'map_name levels\test\airlock\firefight-airlock' HALO_CUSTOM_EDITION=1
		grep -aq "large textures drawn from their second level" "$out/$name/run.log" "$out/$name/data/debug.txt" 2>/dev/null ||
			echo "  (no reduced-texture count logged)"
		expect "$name" "the visor fills the $width columns and 486 rows" \
			"x0 <= 1 && x1 >= $((width - 1)) && y1 - y0 >= 484" 'odst final'
	done
fi

# 3. Halo: CE Anniversary's HUD, natively and converted
mcc_mod=${HALO_TEST_MCC_MOD:-$HOME/.local/share/Steam/steamapps/workshop/content/976730/2915693904}
mcc_maps=${HALO_TEST_MCC_MAPS:-}
mcc_expect() {
	local name=$1 width=$2
	local margin=$((48 * width / 640))
	expect "$name" "the shield panel 128x32 on the frame ($((width - margin)), 37)" \
		"x1 == $((width - margin)) && y0 == 37 && x1 - x0 == 128 && y1 - y0 == 32" 'hud_unit_backgrounds'
	expect "$name" "the motion sensor 128 wide, 10 in from the frame's corner ($((margin + 10)), 434)" \
		"x0 == $((margin + 10)) && y1 == 434 && x1 - x0 == 128" 'hud_unit_backgrounds'
	expect "$name" "the crosshair 66 wide, centred ($((width / 2)))" \
		"x0 + x1 == $width && x1 - x0 == 66" 'hud_reticles'
	expect "$name" "the digits 12 rows" "y1 - y0 == 12" 'hud_counter_numbers'
	if quads "$name" 'hud_unit_backgrounds' | awk '$3 - $1 > 128 { found = 1 } END { exit !found }'; then
		fail "$name: a unit HUD background wider than the Xbox's 128 (quads: $(quads "$name" 'hud_unit_backgrounds' | tr '\n' ';'))"
	fi
}
if [ ! -e "$mcc_mod/maps/b30.map" ]; then
	echo "SKIP (the Halo: CE Anniversary part): no b30.map in $mcc_mod/maps (HALO_TEST_MCC_MOD)"
else
	extra_maps=()
	mods_folder=$(cd "$mcc_mod" && pwd)
	for width in 640 848; do
		name="mcc-native-$width"
		run "$name" "$width" 60 'map_name mods\ruby_b30' HALO_CUSTOM_EDITION=1 HALO_MCC_MODS=1
		grep -aq "the HUD is Halo: CE Anniversary's" "$out/$name/data/debug.txt" 2>/dev/null ||
			fail "$name: the HUD was not taken for Halo: CE Anniversary's ($out/$name/data/debug.txt)"
		grep -aq "HUD meters are shaded as the Xbox's" "$out/$name/data/debug.txt" 2>/dev/null ||
			fail "$name: MCC's meters were not kept in the Xbox's channels ($out/$name/data/debug.txt)"
		mcc_expect "$name" "$width"
	done
	mods_folder=
	converted=
	[ -n "$mcc_maps" ] && converted=$(ls "$mcc_maps"/*b30.map 2> /dev/null | head -1)
	if [ -z "$converted" ]; then
		echo "SKIP (the converted map): no *b30.map in HALO_TEST_MCC_MAPS"
	else
		extra_maps=("$(cd "$(dirname "$converted")" && pwd)/$(basename "$converted")")
		for name in bitmaps sounds loc; do
			[ -e "$resources/$name.map" ] && extra_maps+=("$(cd "$resources" && pwd)/$name.map")
		done
		map=$(basename "$converted" .map)
		for width in 640 848; do
			name="mcc-converted-$width"
			run "$name" "$width" 60 "map_name custom_maps\\$map" HALO_CUSTOM_EDITION=1
			mcc_expect "$name" "$width"
		done
	fi
fi

echo "logs: $out"
if [ "$status" = 0 ]; then
	echo "PASS"
fi
test_out_done $status
exit $status
