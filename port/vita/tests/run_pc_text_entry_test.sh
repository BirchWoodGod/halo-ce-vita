#!/bin/sh
# Builds and runs the test of the PC build's text box on its own
# (port/linux/src/pc_text_entry.c; pc_text_entry_test.c), as a test build and
# as a release build, then checks that the game's request in a release build
# opens the box and is never cancelled for want of the tests' answers
# (port/linux/game/menu_functions.c typing_begin, as the preprocessor gives
# it to a release PC build). No SDK needed. The box in the game itself, typed
# into through SDL's events: run_menus_test.sh pctext.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/pc_text_entry_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
trap 'test_out_done $?' EXIT
cc=${CC:-gcc}
flags="-g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -I$root/port/linux/src"
$cc $flags "$here/pc_text_entry_test.c" "$root/port/linux/src/pc_text_entry.c" -o "$out/test" -lpthread
$cc $flags -DHALO_RELEASE=1 "$here/pc_text_entry_test.c" "$root/port/linux/src/pc_text_entry.c" -o "$out/test_release" -lpthread
status=0
"$out/test" || status=1
"$out/test_release" || status=1
python3 - "$root" "$out" "$cc" <<'PY' || status=1
import re, subprocess, sys
root, out, cc = sys.argv[1:4]
source = open(root + '/port/linux/game/menu_functions.c').read()
start = source.index('static void typing_begin(')
body = source[start:source.index('\n}\n', start) + 3]
open(out + '/typing_begin.c', 'w').write(body)
failures = 0
for build, defines in (('release', ['-DHALO_RELEASE=1']), ('test', [])):
    text = subprocess.run([cc, '-E', '-P', *defines, out + '/typing_begin.c'], capture_output=True, text=True,
                          check=True).stdout
    # (no answers - always in a release build -: the box asked for, before any cancel)
    asked = re.search(r'if \(!answers\)\s*\{\s*pc_text_entry_request\(\);', text)
    release_null = re.search(r'char const \*answers = NULL;', text)
    ok = asked is not None and (build != 'release' or (release_null is not None and 'getenv' not in text))
    print('%s typing_begin, a %s PC build: with no test answers the text box is asked for%s' %
          ('PASS' if ok else 'FAIL', build, ', the tests\' answers never read' if build == 'release' else ''))
    failures += not ok
sys.exit(1 if failures else 0)
PY
# (HALO_TEST_RELEASE: a release build's binary - configure.py --release -: the box in it, and none
# of the tests' typing hooks)
if [ -n "${HALO_TEST_RELEASE:-}" ]; then
	for text in 'the text box asked for' 'text entry: "%s" opened'; do
		if strings "$HALO_TEST_RELEASE" | grep -qF "$text"; then echo "PASS release binary: has \"$text\""
		else echo "FAIL release binary: no \"$text\""; status=1; fi
	done
	for text in HALO_TEST_TEXT_INPUT HALO_TEST_KEYS; do
		if strings "$HALO_TEST_RELEASE" | grep -qF "$text"; then echo "FAIL release binary: reads $text"; status=1
		else echo "PASS release binary: never reads $text"; fi
	done
fi
exit $status
