#!/bin/sh
# What the fingers did in Forge's monitor (port/linux/game/forge_touch.c;
# forge_touch_test.c), with ASan and UBSan. Needs a C compiler (CC, default
# cc) with the sanitizers. What the gestures do to objects:
# run_forge_test.sh touch.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/forge_touch_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
cc=${CC:-cc}
status=0
$cc -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_GNU_SOURCE \
	-I"$root/port/linux/game" -o "$out/forge_touch_test" "$here/forge_touch_test.c" "$root/port/linux/game/forge_touch.c"
"$out/forge_touch_test" || status=1
test_out_done $status
exit $status
