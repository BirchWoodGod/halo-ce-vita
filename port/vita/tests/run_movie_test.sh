#!/bin/bash
# The movies on the Linux harness, with a made-up picture (HALO_TEST_MOVIE,
# port/linux/src/bink_null.c: colour bars that move a pixel a frame): the
# intro plays at the first start, then the main menu comes up. With the
# Vita's device (configure.py --linux-d3d gxm-null) the frames go where the
# Vita's go (HALO_MOVIE_PATH, port/vita/platform/bink_vita.c) and the GPU
# programs the movie draws with are collected and looked up in shaders.pak
# (the owner's Vita has no shader compiler: a program missing there is a
# black movie with its sound playing, 1.1.0 to 1.1.1-insider.11); with the
# OpenGL renderer the game's own quad draws the picture and a screenshot
# shows the bars.
#
#   intro    a 640x480 movie, 150 frames. gxm-null: "movie: path uncached",
#            the frame drawn at the present with the picture's texels moving,
#            the frames let go at the end, every program collected in
#            shaders.pak; GL: the game's texture path, and a screenshot during
#            the movie has the bars (white, yellow, cyan, green, magenta, red,
#            blue, black from the left, give or take the pixels they moved)
#   wide     a 960x544 movie (the Vita's own screen size), gxm-null: in the
#            renderer's frames, letterboxed on the harness's 4:3 screen
#   cdram    HALO_MOVIE_PATH=cdram (gxm-null): the path logged as cdram
#   legacy   HALO_MOVIE_PATH=legacy (gxm-null): the game's texture and quad, as
#            before; the programs it needs that shaders.pak lacks are said
#            (not a failure: the path is the owner's diagnostic switch)
#   skip     A pressed during the intro stops it: the frames let go, the main
#            menu up
#
# Each must log no assertion or exception, and exit by itself.
#
#   run_movie_test.sh [CASE...]   (default: intro wide cdram legacy skip on
#                                  gxm-null, intro skip on GL)
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_OUT    where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_movie_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
status=0
fail() { echo "FAIL ($1): $2"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -e "$data/maps/ui.map" ]; then
	echo "no maps in $data/maps (HALO_TEST_DATA)"
	exit 2
fi
# (the Vita's device: its renderer's strings are in the program)
if grep -aq "gxm-null: movie drawn at the present" "$binary"; then
	device=gxm-null
	cases=${*:-intro wide cdram legacy skip}
else
	device=gl
	cases=${*:-intro skip}
fi
echo "device: $device"
mkdir -p "$out"

run() { # NAME SECONDS PAD [VAR=value...]
	local name=$1 seconds=$2 pad=$3
	shift 3
	rm -rf "$out/$name"
	mkdir -p "$out/$name/data" "$out/$name/save" "$out/$name/bin" "$out/$name/shaders" "$out/$name/shots"
	ln -sfn "$(cd "$data" && pwd)/maps" "$out/$name/data/maps"
	cp "$binary" "$out/$name/bin/halo"
	(cd "$out/$name" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" \
		HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_UI_LOG=1 "HALO_TEST_PAD=$pad" HALO_SHADER_COLLECT="$out/$name/shaders" \
		HALO_SCREENSHOT_DIR="$out/$name/shots" HALO_SCREENSHOT_EVERY=60 "$@" \
		timeout -k 5 $((seconds + 90)) "$out/$name/bin/halo" > "$out/$name/run.log" 2>&1)
	echo $? > "$out/$name/exit"
	rm -rf "$out/$name/bin"
}

common() { # NAME
	local name=$1 log=$out/$1/run.log
	echo "--- $name"
	grep -aE "movie|test pad: a$" "$log" | grep -av "root: \|settings: " | sed 's/^halo-linux: //' | head -12
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name/exit")"
	grep -aqiE "assert|exception|halt" "$log" && fail "$name" "an assertion, exception or halt"
	grep -aq "test movie: .* plays a" "$log" || fail "$name" "no movie opened"
	grep -aq "test movie: closed" "$log" || fail "$name" "the movie did not end"
	grep -aq "ui: screen ui.shell.main_menu.main_menu" "$log" || fail "$name" "the main menu did not come up after the movie"
}

# the renderer's frames: drawn at the present, their texels changing, let go
native() { # NAME PATH
	local name=$1 path=$2 log=$out/$1/run.log
	grep -aq "movie: path $path: " "$log" || fail "$name" "the frames did not take the $path path"
	[ "$(grep -ac "gxm-null: movie drawn at the present" "$log")" -ge 2 ] || fail "$name" "the movie was not drawn at the present"
	[ "$(grep -a "gxm-null: movie drawn at the present" "$log" | sed 's/.*texel //' | sort -u | wc -l)" -ge 2 ] ||
		fail "$name" "the frames drawn did not change"
	grep -aq "movie: renderer frames let go" "$log" || fail "$name" "the renderer's frames were not let go"
	grep -aq "path legacy" "$log" && fail "$name" "a frame went to the game's texture"
}

# every program collected is in shaders.pak (LACKING: say, not fail)
programs() { # NAME [say]
	local name=$1 result
	result=$(python3 "$root/tools/vita_shader_pack.py" --check "$root/port/vita/app0/shaders.pak" --sources "$out/$name/shaders" 2>&1)
	echo "$result"
	ls "$out/$name/shaders" | grep -q "\.fp\.cg$" || fail "$name" "no programs collected"
	if echo "$result" | grep -q "lacking"; then
		[ "${2:-}" = say ] || fail "$name" "programs the movie draws with are not in shaders.pak"
	fi
}

# a screenshot during the movie has the eight bars in order (each bar's
# middle column, a third of the way down)
bars() { # NAME
	local name=$1 shot
	shot=$(ls "$out/$name/shots"/*.bmp 2>/dev/null | sed -n 3p)
	[ -n "$shot" ] || { fail "$name" "no screenshot"; return; }
	python3 - "$shot" <<'EOF' || fail "$name" "the screenshot $shot does not show the bars"
import struct, sys
data = open(sys.argv[1], "rb").read()
offset, = struct.unpack_from("<I", data, 10)
width, height = struct.unpack_from("<ii", data, 18)
bits, = struct.unpack_from("<H", data, 28)
step = bits // 8
row = (width * step + 3) & ~3
y = abs(height) * 2 // 3 if height > 0 else abs(height) // 3  # (bottom-up rows)
names = []
for bar in range(8):
    x = width * (2 * bar + 1) // 16
    b, g, r = data[offset + y * row + x * step: offset + y * row + x * step + 3]
    names.append("".join("1" if c > 128 else "0" for c in (r, g, b)))
# white yellow cyan green magenta red blue black, rotated by the pixels moved
order = ["111", "110", "011", "010", "101", "100", "001", "000"]
print("bars in %s: %s" % (sys.argv[1].rsplit("/", 1)[-1], " ".join(names)))
for start in range(8):
    if names == order[start:] + order[:start]:
        sys.exit(0)
sys.exit(1)
EOF
}

for case in $cases; do
	case $case in
	intro)
		run intro 16 "wait" HALO_TEST_MOVIE=640x480:150
		common intro
		if [ $device = gxm-null ]; then
			native intro uncached
			programs intro
		else
			grep -aq "test movie: path legacy" "$out/intro/run.log" || fail intro "the OpenGL build did not take the game's texture"
			bars intro
		fi
		;;
	wide)
		run wide 16 "wait" HALO_TEST_MOVIE=960x544:150
		common wide
		native wide uncached
		grep -aq "960x544 from .* over 0.000,0.12" "$out/wide/run.log" || fail wide "the 16:9 movie was not letterboxed on the 4:3 screen"
		programs wide
		;;
	cdram)
		run cdram 16 "wait" HALO_TEST_MOVIE=640x480:150 HALO_MOVIE_PATH=cdram
		common cdram
		native cdram cdram
		;;
	legacy)
		run legacy 16 "wait" HALO_TEST_MOVIE=640x480:150 HALO_MOVIE_PATH=legacy
		common legacy
		grep -aq "test movie: path legacy (HALO_MOVIE_PATH)" "$out/legacy/run.log" || fail legacy "the game's texture was not used"
		grep -aq "gxm-null: movie drawn at the present" "$out/legacy/run.log" && fail legacy "the renderer drew the movie"
		programs legacy say
		;;
	skip)
		# (the intro may not be skipped while the start-up checks run: A
		# pressed a few times after them)
		run skip 20 "wait:150:5000 a:150:500 a:150:500 a:150:500" HALO_TEST_MOVIE=640x480:100000
		echo "--- skip"
		log=$out/skip/run.log
		grep -aE "movie|test pad: a$" "$log" | grep -av "root: \|settings: " | sed 's/^halo-linux: //' | head -8
		[ "$(cat "$out/skip/exit")" = 0 ] || fail skip "exit $(cat "$out/skip/exit")"
		grep -aqiE "assert|exception|halt" "$log" && fail skip "an assertion, exception or halt"
		grep -aq "test movie: closed at frame" "$log" || fail skip "the movie was not stopped"
		[ $device = gxm-null ] && ! grep -aq "movie: renderer frames let go" "$log" && fail skip "the renderer's frames were not let go"
		grep -aq "ui: screen ui.shell.main_menu.main_menu" "$log" || fail skip "the main menu did not come up"
		;;
	*)
		echo "unknown case $case"
		status=2
		;;
	esac
done

[ $status = 0 ] && echo "PASS ($device: $cases)"
test_out_done $status
exit $status
