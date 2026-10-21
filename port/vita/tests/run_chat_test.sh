#!/bin/sh
# The in-game chat's rules and its slur filter, on the desktop
# (port/linux/game/game_chat_protocol.c, port/linux/src/chat_filter.c,
# chat_test.c, with the shipped list port/vita/app0/chat/filter.txt): built
# as the Vita builds them (32-bit), then again with AddressSanitizer and
# UndefinedBehaviorSanitizer. The list is checked against the tool that
# writes it when an OpenCE checkout is there (OPENCE, default
# ~/github/vitaprojects/opence/opence-contrib).
#   CC        the 32-bit build's compiler (default gcc)
#   SAN_CC    the sanitized build's (default clang)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/chat_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
trap 'test_out_done $?' EXIT
cc=${CC:-gcc}
san_cc=${SAN_CC:-clang}
sources="$here/chat_test.c $root/port/linux/game/game_chat_protocol.c $root/port/linux/src/chat_filter.c"
includes="-I$root/port/linux/game -I$root/port/linux/src"
list=$root/port/vita/app0/chat/filter.txt
status=0
$cc -m32 -g -O1 -Wall -Wextra -Werror $includes $sources -o "$out/chat_test"
"$out/chat_test" "$list" > "$out/chat_test.log" 2>&1 || status=1
grep -E "^(the |FAIL|PASS|[0-9]+ checks)" "$out/chat_test.log" | head -40
$san_cc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all $includes $sources -o "$out/chat_test_san"
echo "--- with AddressSanitizer and UndefinedBehaviorSanitizer"
"$out/chat_test_san" "$list" > "$out/chat_test_san.log" 2>&1 || status=1
tail -2 "$out/chat_test_san.log"
opence=${OPENCE:-$HOME/github/vitaprojects/opence/opence-contrib}
if [ -d "$opence/.git" ] || [ -f "$opence/.git" ]; then
	python3 "$root/tools/chat_filter_words.py" --opence "$opence" --check || status=1
	# (the English words it masks: those expected alone, every one a slur's form)
	if git -C "$opence" show '4b09168e^:port/third_party/english-words/words_alpha.txt.gz' 2>/dev/null |
		gunzip > "$out/words_alpha.txt" 2>/dev/null && [ -s "$out/words_alpha.txt" ]; then
		"$out/chat_test" "$list" --dictionary "$out/words_alpha.txt" "$here/data/chat_filter_dictionary.txt" ||
			status=1
	fi
else
	echo "(no OpenCE checkout at $opence: the list not checked against its source)"
fi
[ $status = 0 ] && echo "PASS (run_chat_test.sh)" || echo "FAIL (run_chat_test.sh)"
exit $status
