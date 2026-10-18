#!/bin/sh
# Forge's map variant files (port/linux/game/forge_files.c; forge_files_test.c):
# the writer, the validator, the names and a fuzzer over the validator
# (truncation, every bit flipped, huge counts, bad offsets, bad tag paths,
# the wrong map, random damage), with ASan and UBSan. Needs a C compiler (CC,
# default cc) with the sanitizers.
#
#   run_forge_files_test.sh [ITERATIONS]   (the fuzzer's cases, 200000 by default)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/forge_files_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
cc=${CC:-cc}
status=0
$cc -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_GNU_SOURCE \
	-I"$root/port/linux/game" -o "$out/forge_files_test" "$here/forge_files_test.c" "$root/port/linux/game/forge_files.c"
"$out/forge_files_test" "${1:-200000}" || status=1
test_out_done $status
exit $status
