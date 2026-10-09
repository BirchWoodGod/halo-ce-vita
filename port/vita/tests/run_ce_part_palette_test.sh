#!/bin/bash
# Custom Edition models of more nodes than the renderer skins at once (Halo PC
# draws models of up to 64 nodes; the vertex shader's constants hold 43, and
# such a model refused the map). Such a model is now drawn a part's own nodes
# at a time, after DamnationCE (xshxdex98/DamnationCE, CC0): each part's
# vertices name the part's nodes (its own table when the model's parts have
# local nodes, else the nodes its vertices name, made one by the loader), and
# before the part is drawn the renderer is given those nodes' matrices alone
# (custom_edition_geometry.c custom_edition_part_palette, rasterizer_xbox.c
# rasterizer_model_part_skinning). None of the maps on hand has such a model,
# so the test makes every model of 2 nodes or more one
# (HALO_CE_PART_PALETTE_NODES=2) on the Linux harness (gxm-null: the Vita's
# renderer) and checks, from the load's log and HALO_CE_PALETTE_LOG=1:
#
# 1. each Custom Edition map loads with parts drawn with their own nodes, none
#    of their compressed vertices naming a node past its part's, and the
#    renderer gives such parts their nodes (draws counted) for the run;
# 2. without the setting, the map loads with no part drawn so (its models have
#    fewer than 44 nodes) and no model refused for its node count;
# 3. an Xbox map (b30) never takes that path.
#
#   run_ce_part_palette_test.sh
#   HALO_TEST_VITA          the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA          a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_CE_MAPS       Custom Edition maps (default ../custom-maps-dl beside
#                           the tree: the first two besides the resource maps)
#   HALO_TEST_CE_MAP        one more Custom Edition map file (optional)
#   HALO_TEST_CE_RESOURCES  Halo PC's bitmaps.map, sounds.map and loc.map
#                           (default the data's maps folder, else Steam's MCC
#                           halo1/maps/custom_edition)
#   HALO_TEST_OUT           where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
ce_maps=${HALO_TEST_CE_MAPS:-$root/../custom-maps-dl}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_ce_part_palette_test.$$}
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
resources=${HALO_TEST_CE_RESOURCES:-}
if [ -z "$resources" ]; then
	resources="$data/maps"
	[ -e "$resources/bitmaps.map" ] ||
		resources="$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1/maps/custom_edition"
fi
maps=()
if [ -d "$ce_maps" ]; then
	for file in "$ce_maps"/*.map; do
		[ -e "$file" ] || continue
		case "$(basename "$file")" in bitmaps.map|sounds.map|loc.map) continue ;; esac
		[ "${#maps[@]}" -lt 2 ] && maps+=("$(cd "$(dirname "$file")" && pwd)/$(basename "$file")")
	done
fi
if [ -n "${HALO_TEST_CE_MAP:-}" ] && [ -e "$HALO_TEST_CE_MAP" ]; then
	maps+=("$(cd "$(dirname "$HALO_TEST_CE_MAP")" && pwd)/$(basename "$HALO_TEST_CE_MAP")")
fi

# run NAME SECONDS INIT_LINES [ENV...]: the harness on a map from init.txt
# (the maps of extra_maps linked beside the Xbox maps)
extra_maps=()
run() {
	local name=$1 seconds=$2 init=$3
	shift 3
	local dir="$out/$name"
	rm -rf "$dir"
	mkdir -p "$dir/data/maps" "$dir/save"
	for file in "$(cd "$data" && pwd)"/maps/*.map; do
		ln -sfn "$file" "$dir/data/maps/"
	done
	for file in "${extra_maps[@]}"; do
		ln -sfn "$file" "$dir/data/maps/"
	done
	printf '%b\n' "$init" > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_CE_PALETTE_LOG=1 "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	local code=$?
	# (the save folder fills with the game state: only the logs are kept)
	rm -rf "$dir/save"
	[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
	grep -av "render assertion skipped" "$dir/run.log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" &&
		fail "$name: an assertion or exception ($dir/run.log)"
}

# logs NAME: the run's output and debug.txt
logs() {
	cat "$out/$1/run.log" "$out/$1/data/debug.txt" 2>/dev/null
}

# the most draws given a part's own nodes the run counted (0: none)
palette_draws() {
	logs "$1" | grep -a "part palettes: [0-9]* draws" | sed 's/.*part palettes: \([0-9]*\) draws.*/\1/' | sort -n | tail -1
}

# 3. an Xbox map
run b30 30 'map_name levels\\b30\\b30'
draws=$(palette_draws b30)
if [ -z "$draws" ]; then
	echo "  ok: b30: no draw given a part's own nodes"
else
	fail "b30: $draws draws given a part's own nodes ($out/b30/run.log)"
fi

missing=
[ "${#maps[@]}" -gt 0 ] || missing="Custom Edition maps (HALO_TEST_CE_MAPS, HALO_TEST_CE_MAP)"
for name in bitmaps sounds loc; do
	[ -e "$resources/$name.map" ] || missing="$missing $name.map (HALO_TEST_CE_RESOURCES)"
done
if [ -n "$missing" ]; then
	echo "SKIP (the Custom Edition part): no $missing"
else
	for map in "${maps[@]}"; do
		base=$(basename "$map" .map)
		extra_maps=("$map")
		for name in bitmaps sounds loc; do
			extra_maps+=("$(cd "$resources" && pwd)/$name.map")
		done
		init="game_variant slayer\nmap_name levels\\\\test\\\\$base\\\\$base"

		# 1. every model of 2 nodes or more drawn a part's nodes at a time
		name="$base-palettes"
		run "$name" 45 "$init" HALO_CUSTOM_EDITION=1 HALO_CE_PART_PALETTE_NODES=2
		summary=$(logs "$name" | grep -a "model parts converted" | tail -1)
		parts=$(echo "$summary" | sed -n 's/.*converted (\([0-9]*\) drawn with their own nodes.*/\1/p')
		past=$(echo "$summary" | sed -n 's/.*vertices, \([0-9]*\) naming a node past them.*/\1/p')
		if [ -z "$parts" ]; then
			fail "$name: the map's models were not converted ($out/$name)"
		elif [ "$parts" -gt 0 ] && [ "$past" = 0 ]; then
			echo "  ok: $name: $parts parts drawn with their own nodes, no vertex naming a node past its part's"
		else
			fail "$name: $parts parts drawn with their own nodes, $past vertices naming a node past them ($summary)"
		fi
		draws=$(palette_draws "$name")
		if [ -n "$draws" ] && [ "$draws" -ge 64 ]; then
			echo "  ok: $name: the renderer gave $draws draws (or more) their part's own nodes"
		else
			fail "$name: ${draws:-no} draws given a part's own nodes ($out/$name/run.log)"
		fi

		# 2. as the map is
		name="$base-default"
		run "$name" 30 "$init" HALO_CUSTOM_EDITION=1
		summary=$(logs "$name" | grep -a "model parts converted" | tail -1)
		parts=$(echo "$summary" | sed -n 's/.*converted (\([0-9]*\) drawn with their own nodes.*/\1/p')
		if logs "$name" | grep -aq "this build draws models of"; then
			fail "$name: a model refused for its nodes ($out/$name)"
		elif [ -z "$parts" ]; then
			fail "$name: the map's models were not converted ($out/$name)"
		elif [ "$parts" = 0 ]; then
			echo "  ok: $name: loaded, no part drawn with its own nodes"
		else
			# (a map with a model of 44 nodes or more: drawn so, as it must be)
			logs "$name" | grep -a "is drawn a part's nodes at a time" | sed 's/^/    /'
			echo "  ok: $name: loaded, $parts parts of models of 44 nodes or more drawn with their own nodes"
		fi
	done
fi

echo "logs: $out"
if [ "$status" = 0 ]; then
	echo "PASS"
fi
test_out_done $status
exit $status
