#!/bin/sh
# Builds and runs the desktop test of the PC builds' keyboard and mouse
# bindings (port/linux/src/key_bindings.c: the MCC defaults, names, parse,
# swap on a taken key, conflicts, migration; key_bindings_test.c). No SDK,
# no SDL: the keys' names are stood in for.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/key_bindings_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
trap 'test_out_done $?' EXIT
cc=${CC:-gcc}
$cc -g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -fsanitize=address,undefined \
	"$here/key_bindings_test.c" "$root/port/linux/src/key_bindings.c" -o "$out/key_bindings_test"
"$out/key_bindings_test"
# (config.toml's defaults, port_config.c's table, are key_bindings.c's)
python3 - "$root" <<'PY'
import re, sys
root = sys.argv[1]
config = dict(re.findall(r'\{ "controls\.keyboard\.(\w+)", _config_string, "\\"([^"\\]*)\\""',
                         open(root + '/port/linux/src/port_config.c').read()))
bindings = dict(re.findall(r'\{ "(\w+)", "[^"]*", "([^"]*)" \}', open(root + '/port/linux/src/key_bindings.c').read()))
failures = 0
for key, default in bindings.items():
    ok = config.get(key) == default
    print('%s config.toml default of %s: %s' % ('PASS' if ok else 'FAIL', key, config.get(key)))
    failures += not ok
if set(config) != set(bindings):
    print('FAIL the actions differ: %s' % sorted(set(config) ^ set(bindings)))
    failures += 1
sys.exit(1 if failures else 0)
PY
