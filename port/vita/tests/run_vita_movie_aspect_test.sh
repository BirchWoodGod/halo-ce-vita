#!/bin/sh
# Builds and runs the desktop test of port/vita/host/vita_movie_aspect.c on
# MP4s made with ffmpeg: square pixels 4:3 and 16:9, 640x480 flagged 16:9
# (ffmpeg -aspect), the same with the moov box first (faststart), a
# 16:9 flag set only on the H.264 stream's pixels (setsar), and 960x544
# with setdar=16/9 (issue #8: a track header within 1% of the pixels', which
# must still win over a player that says 4:3); the picture sizes the files
# give (640x360 decodes to 640x368 on the Vita: the padding rows are cut) and
# what is kept of decoded frames; the decoder row pitch found in frames of
# the test pattern and of a flat white picture laid out 848, 864, 896 and
# 1024 bytes apart.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/vita_movie_aspect_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
trap 'test_out_done $?' EXIT
make() { name=$1; shift; ffmpeg -loglevel error -f lavfi -i testsrc=size=640x480:rate=30:duration=1 "$@" -c:v libx264 -pix_fmt yuv420p "$out/$name.mp4"; }
make square43
make flag169 -aspect 16:9
make flag169fast -aspect 16:9 -movflags +faststart
make sar169 -vf setsar=4/3
ffmpeg -loglevel error -f lavfi -i testsrc=size=854x480:rate=30:duration=1 -c:v libx264 -pix_fmt yuv420p "$out/square169.mp4"
ffmpeg -loglevel error -f lavfi -i testsrc=size=640x480:rate=30:duration=1 -vf "scale=960:544,setdar=16/9" -c:v libx264 \
	-profile:v high -level 4.0 -pix_fmt yuv420p -movflags +faststart "$out/dar169_960.mp4"
ffmpeg -loglevel error -f lavfi -i testsrc=size=640x360:rate=30:duration=1 -c:v libx264 -profile:v high -level 4.0 \
	-pix_fmt yuv420p "$out/square169_640.mp4"
ffmpeg -loglevel error -f lavfi -i testsrc=size=848x480:rate=30:duration=1 -c:v libx264 -profile:v high -level 4.0 \
	-pix_fmt yuv420p "$out/square169_848.mp4"
ffmpeg -loglevel error -f lavfi -i testsrc=size=848x480:rate=30:duration=1 -frames:v 1 -f rawvideo -pix_fmt gray "$out/pattern848.gray"
ffmpeg -loglevel error -f lavfi -i testsrc2=size=704x400:rate=30:duration=1 -frames:v 1 -f rawvideo -pix_fmt gray "$out/pattern704.gray"
ffmpeg -loglevel error -f lavfi -i color=white:size=848x480 -frames:v 1 -f rawvideo -pix_fmt gray "$out/white848.gray"
cc -O1 -Wall -I"$root/port/vita/include" "$here/vita_movie_aspect_test.c" "$root/port/vita/host/vita_movie_aspect.c" -lm -o "$out/test"
"$out/test" "$out/square43.mp4:640x480=1.333" "$out/flag169.mp4:640x480=1.778" "$out/flag169fast.mp4:640x480=1.778" \
	"$out/sar169.mp4:640x480=1.778" "$out/square169.mp4:854x480=1.779" "$out/missing.mp4:640x480=1.333" \
	"$out/dar169_960.mp4:960x544=1.778" "$out/dar169_960.mp4:960x544@1.333=1.778" "$out/square43.mp4:640x480@1.333=1.333" \
	"$out/missing.mp4:640x480@1.778=1.778" "$out/square169_640.mp4:640x368=1.778" "$out/square169_848.mp4:848x480=1.767" \
	"size:$out/square169_640.mp4=640x360" "size:$out/square169_848.mp4=848x480" "size:$out/flag169.mp4=640x480" \
	"size:$out/dar169_960.mp4=960x544" "size:$out/missing.mp4=0x0" "visible:640x368,640x360=640x360" \
	"visible:848x480,848x480=848x480" "visible:640x480,0x0=640x480" "visible:720x480,640x480=720x480" \
	"visible:960x544,967x544=960x544" "visible:1920x1088,1920x1080=1920x1080" \
	"pitch:$out/pattern848.gray:848x480@848=848" "pitch:$out/pattern848.gray:848x480@864=864" \
	"pitch:$out/pattern848.gray:848x480@896=896" "pitch:$out/pattern848.gray:848x480@1024=1024" \
	"pitch:$out/pattern704.gray:704x400@704=704" "pitch:$out/pattern704.gray:704x400@768=768" \
	"pitch:$out/white848.gray:848x480@848=0" "pitch:$out/white848.gray:848x480@896=896"
