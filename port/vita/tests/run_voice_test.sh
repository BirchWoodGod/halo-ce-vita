#!/bin/sh
# Builds and runs the desktop test of voice chat's rules
# (port/linux/game/voice_protocol.c; voice_test.c) and its codec settings,
# 32-bit as the game is, with AddressSanitizer and UBSan when the compiler
# has them, with the vendored libopus (port/third_party/opus, built as the
# game builds it: tools/linux_build.py's OPUS_FLAGS).
#   CC                      the compiler (default gcc)
#   VOICE_TEST_SANITIZE=0   without AddressSanitizer/UBSan
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/voice_test.$$
mkdir -p "$out/opus"
trap 'rm -rf "$out"' EXIT
cc=${CC:-gcc}
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all"
if [ "${VOICE_TEST_SANITIZE:-1}" = 0 ] || ! echo 'int main(void){return 0;}' | $cc -m32 $sanitize -x c - -o "$out/probe" 2>/dev/null; then
	sanitize=
fi
opus="$root/port/third_party/opus"
opus_flags="-DOPUS_BUILD -DFIXED_POINT -DDISABLE_FLOAT_API -fwrapv -DVAR_ARRAYS -I$opus/include -I$opus/celt -I$opus/silk -I$opus/silk/fixed -I$opus/src"
# (the codec with the sanitizers too: the test feeds its decoder what the
# host's checks pass)
for source in "$opus"/celt/*.c "$opus"/silk/*.c "$opus"/silk/fixed/*.c "$opus"/src/*.c; do
	echo "$source"
done | xargs -P "$(nproc 2>/dev/null || echo 4)" -I{} sh -c \
	'name=$(echo "{}" | tr / _); '"$cc"' -m32 -g -O2 -w '"$sanitize $opus_flags"' -c "{}" -o "'"$out"'/opus/$name.o"'
$cc -m32 -g -O1 -Wall -Wextra -Werror $sanitize -I"$root/port/linux/game" -I"$opus/include" \
	"$here/voice_test.c" "$root/port/linux/game/voice_protocol.c" "$root/port/linux/game/chat_protocol.c" \
	"$out"/opus/*.o -lm -o "$out/voice_test"
echo "voice_test: ${sanitize:-no sanitizers}"
"$out/voice_test"
