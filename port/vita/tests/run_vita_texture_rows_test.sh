#!/bin/sh
# Builds and runs the desktop test of the Vita's linear texture rows
# (port/vita/platform/vita_textures.c's texture_decode, included by
# vita_texture_rows_test.c, compiled as the gxm-null harness compiles it):
# the movie's frame at 960x544 and other sizes decoded with every malloc over
# 1 MB refused (issue #38: a 960x544 movie black on v1.1.0-beta.1).
# CC: a clang that targets i686 (default clang).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/vita_texture_rows_test.$$
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT
cc=${CC:-clang}
python3 "$root/tools/linux_msvc_semantics.py" --output "$out/platform_msvc_semantics.h" "$root/port/include/xdk/xdk_d3d8.h" \
	> /dev/null 2>&1 || : > "$out/platform_msvc_semantics.h"
cd "$root"
$cc --target=i686-linux-gnu -m32 -fms-extensions -fshort-wchar -malign-double -fcommon -fno-strict-aliasing -fwrapv \
	-O1 -g -std=gnu11 -D_GNU_SOURCE -DHALO_LINUX_PLATFORM_LAYER -DHALO_GXM_NULL=1 -Wall -Wno-unused-function \
	-Wno-unknown-pragmas -Wno-microsoft-anon-tag -Wno-pragma-pack -Wno-ignored-attributes -Wno-duplicate-decl-specifier \
	-Wno-missing-braces -Wno-unused-variable -Wno-ignored-pragmas \
	-include port/linux/include/halo_linux_prefix.h -include "$out/platform_msvc_semantics.h" \
	-Iport/linux/src -Iport/linux/include -Isource -Isource/cseries -idirafter port/include/xdk -Iport/vita/include \
	-ffunction-sections -fdata-sections -Wl,--gc-sections \
	"$here/vita_texture_rows_test.c" -lm -o "$out/test"
"$out/test"
