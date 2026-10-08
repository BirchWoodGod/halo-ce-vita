#!/bin/sh
# The port's translations: the desktop test of port/linux/src/lang.c and of
# the overlay font's Latin-1 glyphs (lang_test.c), then tools/lang_check.py
# on the shipped language files (port/vita/app0/lang): every string the
# code marks (T, N_, TW, the PC menus' XML) has an entry in each, with the
# same % conversions, in characters the panel draws. The settings panel's
# own rows are checked by run_vita_settings_test.sh, in Spanish too.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/lang_test.$$
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT
cc=${CC:-gcc}
# (16-bit wchar_t, as the game is built: lang_text_wide)
$cc -m32 -g -O1 -Wall -Wextra -fshort-wchar -I"$root/port/linux/src" "$here/lang_test.c" "$root/port/linux/src/lang.c" \
	-o "$out/lang_test"
status=0
(cd "$out" && ./lang_test) || status=$?
python3 "$root/tools/lang_check.py" --root "$root" || status=1
exit $status
