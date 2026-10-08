#!/bin/sh
# Builds and runs the desktop test of game chat's rules
# (port/linux/game/chat_protocol.c; chat_test.c), 32-bit as the game is,
# with AddressSanitizer and UBSan when the compiler has them.
#   CC                 the compiler (default gcc)
#   CHAT_TEST_SANITIZE=0   without AddressSanitizer/UBSan
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/chat_test.$$
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT
cc=${CC:-gcc}
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all"
if [ "${CHAT_TEST_SANITIZE:-1}" = 0 ] || ! echo 'int main(void){return 0;}' | $cc -m32 $sanitize -x c - -o "$out/probe" 2>/dev/null; then
	sanitize=
fi
$cc -m32 -g -O1 -Wall -Wextra -Werror $sanitize -I"$root/port/linux/game" \
	"$here/chat_test.c" "$root/port/linux/game/chat_protocol.c" -o "$out/chat_test"
echo "chat_test: ${sanitize:-no sanitizers}"
"$out/chat_test"
