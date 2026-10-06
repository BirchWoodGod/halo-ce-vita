#!/bin/sh
# Builds and runs the desktop test of port/vita/host/vita_shader_cache.c
# (issue #28): the shader cache's and the shipped pack's checks; cache files
# made into a pack by tools/vita_shader_pack.py and read back; the VPK's
# port/vita/app0/shaders.pak checked against this build's compiler
# settings; and the pack of the first format (v1.0.3-beta.6 and before:
# git show 83206427:port/vita/app0/shaders.pak) refused.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/vita_shader_cache_test.$$
mkdir -p "$out/collect/progs"
trap 'rm -rf "$out"' EXIT
cc -O1 -Wall -Wextra -I"$root/port/vita/include" "$here/vita_shader_cache_test.c" "$root/port/vita/host/vita_shader_cache.c" \
	-o "$out/test"
"$out/test"
"$out/test" write-cache "$out/collect"
python3 "$root/tools/vita_shader_pack.py" --sources "$out/collect" --programs "$out/collect/progs" --output "$out/tool.pak"
"$out/test" check-pack "$out/tool.pak" "$out/collect"
"$out/test" shipped "$root/port/vita/app0/shaders.pak"
if git -C "$root" cat-file -e 83206427:port/vita/app0/shaders.pak 2>/dev/null; then
	git -C "$root" show 83206427:port/vita/app0/shaders.pak > "$out/old.pak"
	"$out/test" reject "$out/old.pak"
fi
echo "vita_shader_cache_test: all passed"
