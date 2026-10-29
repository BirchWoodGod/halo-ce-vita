#!/bin/sh
# Builds and runs the desktop test of the settings panel's Halo 3 fonts
# (port/vita/host/vita_overlay_font.c; vita_overlay_font_test.c), on the
# fonts tools/halo3_menu_art.py makes (Pillow). No SDK needed.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/vita_overlay_font_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out/fonts"
python3 "$root/tools/halo3_menu_art.py" "$out/fonts"
cc=${CC:-gcc}
$cc -m32 -g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -I"$root/port/vita/include" \
	"$here/vita_overlay_font_test.c" -o "$out/vita_overlay_font_test"
status=0
"$out/vita_overlay_font_test" "$out/fonts" || status=$?
test_out_done $status
exit $status
