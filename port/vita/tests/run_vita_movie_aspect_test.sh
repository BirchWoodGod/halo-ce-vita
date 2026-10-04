#!/bin/sh
# Builds and runs the desktop test of port/vita/host/vita_movie_aspect.c on
# MP4s made with ffmpeg: square pixels 4:3 and 16:9, 640x480 flagged 16:9
# (ffmpeg -aspect), the same with the moov box first (faststart), a
# 16:9 flag set only on the H.264 stream's pixels (setsar), and 960x544
# with setdar=16/9 (issue #8: a track header within 1% of the pixels', which
# must still win over a player that says 4:3).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/vita_movie_aspect_test.$$
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT
make() { name=$1; shift; ffmpeg -loglevel error -f lavfi -i testsrc=size=640x480:rate=30:duration=1 "$@" -c:v libx264 -pix_fmt yuv420p "$out/$name.mp4"; }
make square43
make flag169 -aspect 16:9
make flag169fast -aspect 16:9 -movflags +faststart
make sar169 -vf setsar=4/3
ffmpeg -loglevel error -f lavfi -i testsrc=size=854x480:rate=30:duration=1 -c:v libx264 -pix_fmt yuv420p "$out/square169.mp4"
ffmpeg -loglevel error -f lavfi -i testsrc=size=640x480:rate=30:duration=1 -vf "scale=960:544,setdar=16/9" -c:v libx264 \
	-profile:v high -level 4.0 -pix_fmt yuv420p -movflags +faststart "$out/dar169_960.mp4"
cc -O1 -Wall -I"$root/port/vita/include" "$here/vita_movie_aspect_test.c" "$root/port/vita/host/vita_movie_aspect.c" -lm -o "$out/test"
"$out/test" "$out/square43.mp4:640x480=1.333" "$out/flag169.mp4:640x480=1.778" "$out/flag169fast.mp4:640x480=1.778" \
	"$out/sar169.mp4:640x480=1.778" "$out/square169.mp4:854x480=1.779" "$out/missing.mp4:640x480=1.333" \
	"$out/dar169_960.mp4:960x544=1.778" "$out/dar169_960.mp4:960x544@1.333=1.778" "$out/square43.mp4:640x480@1.333=1.333" \
	"$out/missing.mp4:640x480@1.778=1.778"
