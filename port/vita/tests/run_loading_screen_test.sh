#!/bin/bash
# The loading screen's picture and chant, from the player's own default.xbe
# (progress_bar.c: the port carries neither; it reads them from the retail
# executable). insider.19 and every build since v1.1.0-beta.1 showed it black
# and silent on the Vita: the executable sits above the data root there
# (ux0:data/haloce-vita/default.xbe, data root ux0:data/haloce-vita/data), and
# the d:\..\default.xbe it was looked for as resolved to the data root itself
# once 61d518c5e made platform_translate_path drop "..". It is now read from
# the folder above with platform_read_file_in_parent_folder (xbox_files.c).
#
# Each case loads a10 from a fresh save folder on the Linux harness (the map
# precached, so the loading screen is up for its copy), screenshots every
# third frame and the mix dumped (HALO_AUDIO_DUMP), and must exit by itself
# with no assertion, exception or crash:
#
#   root       default.xbe in the data root (d:\default.xbe)
#   above      default.xbe in the folder above the data root (the Vita's place)
#   maps       default.xbe next to a maps folder elsewhere (HALO_MAPS_ROOT, the
#              Xita installer's copy of the game)
#   synthetic  in the data root, an executable made by loading_screen_check.py
#              with the retail one's structure and its own picture and tone
#              (no retail byte): the search alone, without a retail file
#   missing    none anywhere
#   garbage    a 2.4 MB "XBEH" file of random bytes in the data root and one
#              cut short above it
#
# The first three use the retail executable (HALO_TEST_XBE) when there is
# one, else the synthetic one (said). A picture's case must log where it came
# from, "loading screen: up, the retail picture, the chant playing", draw a
# lit frame (the retail picture's blue), and play the chant in the load's
# first seconds; missing and garbage must log the picture black and the
# screen silent, draw no lit frame during the load and play nothing then.
# "During the load" is its first 5 s, frames by their files' times and sound
# by the dump's (a10's copy keeps the loading screen up about 9 s on the
# harness; the level's own frames after it are blue as well). The brightest
# frame of those of each case is kept as <case>.png in the output.
#
#   run_loading_screen_test.sh [CASE...]   (default: all)
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_XBE    the player's retail default.xbe (never in the repository)
#   HALO_TEST_OUT    where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
xbe=${HALO_TEST_XBE:-}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_loading_screen_test.$$}
check="python3 -I $here/loading_screen_check.py"
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-root above maps synthetic missing garbage}
status=0
fail() { echo "FAIL ($1): $2"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -e "$data/maps/a10.map" ]; then
	echo "no a10.map in $data/maps (HALO_TEST_DATA)"
	exit 2
fi
mkdir -p "$out"
$check synthetic "$out/synthetic.xbe" || exit 2
if [ -n "$xbe" ] && [ -f "$xbe" ]; then
	echo "retail executable: $xbe"
	retail=$xbe
else
	echo "no retail default.xbe (HALO_TEST_XBE): root, above and maps use the synthetic one"
	retail=$out/synthetic.xbe
fi

# run NAME DATA [VAR=value...]: a10 loaded with DATA as the data root
run() {
	local name=$1 root_folder=$2
	shift 2
	mkdir -p "$out/$name/save" "$out/$name/shots" "$out/$name/bin"
	printf 'map_name levels\\a10\\a10\n' > "$root_folder/init.txt"
	cp "$binary" "$out/$name/bin/halo"
	(cd "$out/$name" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$root_folder" \
		HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=14 HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_SCREENSHOT_EVERY=3 HALO_SCREENSHOT_DIR="$out/$name/shots" HALO_AUDIO_DUMP="$out/$name/audio.f32" "$@" \
		timeout -k 5 120 "$out/$name/bin/halo" > "$out/$name/run.log" 2>&1)
	echo $? > "$out/$name/exit"
	rm -rf "$out/$name/bin" "$out/$name/save"
}

common() { # NAME
	local name=$1 log=$out/$1/run.log
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name/exit") ($log)"
	grep -av "render assertion skipped" "$log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire|segmentation fault" &&
		fail "$name" "an assertion, exception or crash ($log)"
	grep -aq "new map: ready" "$log" || fail "$name" "a10 did not load"
	grep -a "loading screen" "$log" | sed 's/^halo-linux: /  /'
}

picture() { # NAME FROM
	local name=$1 from=$2 log=$out/$1/run.log
	common "$name"
	grep -aqF "loading screen: the retail picture from $from" "$log" || fail "$name" "the picture was not read from $from"
	grep -aqF "loading screen: the retail chant (" "$log" || fail "$name" "no chant read"
	grep -aq "loading screen: up, the retail picture, the chant playing" "$log" ||
		fail "$name" "the loading screen did not come up with the picture and the chant"
	grep -aq "picture stays black" "$log" && fail "$name" "said the picture stays black"
	set -- $($check frames "$out/$name/shots" 5)
	echo "  brightest frame in the first 5 s: $1: lit $2, blue $3"
	[ "$1" != none ] && $check png "$out/$name/shots/$1" "$out/$name.png"
	awk -v lit="$2" 'BEGIN { exit lit > 0.05 ? 0 : 1 }' || fail "$name" "no lit frame (the picture not drawn)"
	loudest=$($check audio "$out/$name/audio.f32" 5)
	echo "  loudest quarter second in the first 5 s: $loudest"
	awk -v rms="$loudest" 'BEGIN { exit rms > 0.004 ? 0 : 1 }' || fail "$name" "the chant was not heard during the load"
}

black() { # NAME
	local name=$1 log=$out/$1/run.log
	common "$name"
	grep -aq "no retail default.xbe with the loading picture in the data root, above it or next to the maps folder; the picture stays black" "$log" ||
		fail "$name" "did not say the picture stays black"
	grep -aq "loading screen: the retail picture from" "$log" && fail "$name" "a picture was found"
	if grep -aq "loading screen: up" "$log"; then
		grep -aq "loading screen: up, black (no picture), silent (no chant)" "$log" ||
			fail "$name" "the loading screen came up with a picture or a chant"
	fi
	loudest=$($check audio "$out/$name/audio.f32" 5)
	echo "  loudest quarter second in the first 5 s: $loudest"
	awk -v rms="$loudest" 'BEGIN { exit rms < 0.002 ? 0 : 1 }' || fail "$name" "sound during the load"
	set -- $($check frames "$out/$name/shots" 5)
	echo "  brightest frame in the first 5 s: $1: lit $2, blue $3"
	[ "$1" != none ] && $check png "$out/$name/shots/$1" "$out/$name.png"
	awk -v lit="$2" 'BEGIN { exit lit < 0.005 ? 0 : 1 }' || fail "$name" "a lit frame during the load"
}

for case in $cases; do
	echo "--- $case"
	dir=$out/$case
	rm -rf "$dir"
	mkdir -p "$dir"
	case $case in
	root)
		mkdir -p "$dir/data"
		ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
		cp "$retail" "$dir/data/default.xbe"
		run root "$dir/data"
		picture root 'd:\default.xbe'
		;;
	above)
		mkdir -p "$dir/game/data"
		ln -sfn "$(cd "$data" && pwd)/maps" "$dir/game/data/maps"
		cp "$retail" "$dir/game/default.xbe"
		run above "$dir/game/data"
		picture above "$dir/game/default.xbe"
		;;
	maps)
		mkdir -p "$dir/data" "$dir/xita/haloce"
		ln -sfn "$(cd "$data" && pwd)/maps" "$dir/xita/haloce/maps"
		cp "$retail" "$dir/xita/haloce/default.xbe"
		run maps "$dir/data" HALO_MAPS_ROOT="$dir/xita/haloce/maps"
		picture maps "$dir/xita/haloce/default.xbe"
		;;
	synthetic)
		mkdir -p "$dir/data"
		ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
		cp "$out/synthetic.xbe" "$dir/data/default.xbe"
		run synthetic "$dir/data"
		picture synthetic 'd:\default.xbe'
		;;
	missing)
		mkdir -p "$dir/game/data"
		ln -sfn "$(cd "$data" && pwd)/maps" "$dir/game/data/maps"
		run missing "$dir/game/data"
		black missing
		;;
	garbage)
		mkdir -p "$dir/game/data"
		ln -sfn "$(cd "$data" && pwd)/maps" "$dir/game/data/maps"
		$check garbage "$dir/game/data/default.xbe" 2535420
		head -c 300 "$out/synthetic.xbe" > "$dir/game/default.xbe"
		run garbage "$dir/game/data"
		black garbage
		grep -aqF "loading screen: d:\\default.xbe (2535424 bytes) has no loading picture" "$out/garbage/run.log" ||
			fail garbage "the random file was not said to have no picture"
		grep -aqF "loading screen: $dir/game/default.xbe (300 bytes) has no loading picture" "$out/garbage/run.log" ||
			fail garbage "the cut-short file was not said to have no picture"
		;;
	*)
		echo "usage: $0 [root|above|maps|synthetic|missing|garbage]..." >&2; exit 2 ;;
	esac
done
echo "logs in $out"
[ $status = 0 ] && echo PASS
test_out_done $status
exit $status
