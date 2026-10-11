#!/bin/sh
# The Server Browser's list (port/linux/game/browser_list.c;
# browser_list_test.c), with ASan and UBSan. Needs a C compiler (CC,
# default cc) with the sanitizers.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/browser_list_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
cc=${CC:-cc}
status=0
$cc -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_GNU_SOURCE \
	-I"$root/port/linux/game" -o "$out/browser_list_test" "$here/browser_list_test.c"
"$out/browser_list_test" || status=1
test_out_done $status
exit $status
