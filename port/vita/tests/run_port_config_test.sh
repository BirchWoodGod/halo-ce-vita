#!/bin/sh
# Builds and runs the desktop test of the settings file, config.toml
# (port/linux/src/port_config.c; port_config_test.c), twice: as the Vita
# builds it (HALO_NOT_DESKTOP) and as the desktop does (SDL's file calls
# stood in for; the host's SDL3 headers are used for the types).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/port_config_test.$$
mkdir -p "$out/vita" "$out/desktop"
cc=${CC:-gcc}
flags="-g -O1 -Wall -Wno-unused-function -D_GNU_SOURCE -I$root/port/linux/src -I$root/port/third_party/tomlc17"
$cc $flags -DHALO_NOT_DESKTOP=1 -DHALO_VITA=1 "$here/port_config_test.c" "$root/port/linux/src/posix_files.c" \
	"$root/port/third_party/tomlc17/tomlc17.c" -o "$out/port_config_test_vita" -lpthread
$cc $flags "$here/port_config_test.c" "$root/port/third_party/tomlc17/tomlc17.c" -o "$out/port_config_test_desktop" -lpthread
status=0
"$out/port_config_test_vita" "$out/vita" "$here/config_110_vita.toml" || status=$?
"$out/port_config_test_desktop" "$out/desktop" "$here/config_110_vita.toml" || status=$?
rm -rf "$out"
exit $status
