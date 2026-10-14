#!/bin/bash
# The crossplay join's map check (docs/crossplay.md T3; port/linux/game/
# crossplay_maps.c, custom_edition_maps_crossplay_check): with crossplay on,
# this machine joins only PC hosts, whose game settings name a map as OpenCE
# does since its network version 29: custom_maps\<name> and its version (its
# hash's first 32 bits) for a Custom Edition map, the Xbox's own maps by
# their level paths with 0. PC hosts have no map download: a map missing here, another copy or
# one that cannot load is refused, naming it, and never asked of the host.
#
# 1. unit: crossplay_maps_test.c (names, hostile ones among them, and the
#    verdict for every kind of file), built as map_share_test.c is, with
#    AddressSanitizer and UBSan; with the maps below, the Custom Edition
#    map's hash (its first 32 bits what an OpenCE host of 29 sends) is the
#    one worked out here apart.
# 2. stand-in (HALO_TEST_CE_MAP given, and not CROSSPLAY_MAPS_UNIT_ONLY=1): a
#    PC host's map naming end to end, before T4's wire lets our client reach
#    a real PC host (run_netns_crossplay_test.sh crossmaps is that, expected
#    to fail until then). Two copies of this tree's Linux build, both with
#    crossplay on (HALO_CROSSPLAY=true: OpenCE's settings record), in netns
#    lan mode (run_netns_online_test.sh; the host the debug build's PC-host stand-in, HALO_TEST_CROSSPLAY_HOST=1); the host names its map as an
#    OpenCE host does (network_test.c: a map with a path is that level;
#    custom_edition_maps_network_identity on the crossplay wire: the raw
#    checksum). Cases:
#      stock      the host on Blood Gulch (levels\test\bloodgulch\bloodgulch,
#                 0): the joiner plays
#      ce         the host on custom_maps\<map> (HALO_TEST_CE_MAP), the joiner
#                 with the same file and PC maps on: plays, the version the
#                 host's
#      cemissing  the joiner without the map: refused, the map named, no map
#                 share query sent (map sharing is this port's alone)
#      cediffer   the joiner with another copy (one byte of its header's
#                 checksum changed, so its hash): refused as another version
#      cexbox     the host on custom_maps\bloodgulch (Halo PC's Blood Gulch,
#                 the CE map copied under that name), the joiner with the
#                 Xbox's bloodgulch.map: refused, said to be an Xbox map
#    and the versions of OpenCE's network version 29 (the host on
#    custom_maps\<map>, map.version its hash's first 32 bits, worked out
#    here apart), the joiner's file found by name and version:
#      cever      the joiner with only <map>_<8 hex digits of the version>.map
#                 (as OpenCE saves a download beside another version): plays
#                 it, the loader taking that file
#      cebeside   the joiner with another version as <map>.map and the host's
#                 as <map>_<digits>.map: plays the host's
#      ceverwrong the joiner with another version under the host's version's
#                 file name, <map>_<digits>.map, and none else: refused, never
#                 loaded
#    CROSSPLAY_MAPS_CASES picks some ("stock ce cemissing cediffer cexbox
#    cever cebeside ceverwrong").
#   HALO_TEST_CE_MAP   a Custom Edition multiplayer map whose resource maps
#                      are not needed (Invader's pcgulch.map); the stand-in
#                      part is skipped without it
#   HALO_TEST_VITA, HALO_TEST_DATA, HALO_TEST_CPUS   as run_netns_online_test.sh's
#   HALO_TEST_OUT, HALO_TEST_KEEP                    where the logs go
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/crossplay_maps_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
data=${HALO_TEST_DATA:-$root/../data2276}
ce_map=${HALO_TEST_CE_MAP:-}
status=0
fail() { echo "FAIL $*"; status=1; }

# ---- 1. unit
clang=${CLANG:-$(sed -n 's/^linux_cc = //p' "$root/build.ninja" 2>/dev/null | head -1)}
[ -n "$clang" ] || clang=clang
(
	set -e
	cd "$root"
	game_flags="--target=i686-linux-gnu -m32 -fms-extensions -fshort-wchar -malign-double -fcommon -fno-pic
		-fno-strict-aliasing -fwrapv -freg-struct-return -ffunction-sections -fdata-sections -O2 -g -std=gnu11 -D_GNU_SOURCE
		-DHALO_LINUX_PLATFORM_LAYER -w -include port/linux/include/halo_linux_prefix.h
		-include build/linux/platform_msvc_semantics.h -Iport/linux/src -Iport/linux/include -Iport/third_party/kcp
		-Iport/third_party/monocypher -Isource -Isource/cseries -idirafter port/include/xdk"
	$clang $game_flags -c port/linux/src/p2p_crypto.c -o "$out/p2p_crypto.o"
	$clang $game_flags -c port/third_party/monocypher/monocypher.c -o "$out/monocypher.o"
	$clang $game_flags -c port/third_party/monocypher/monocypher-ed25519.c -o "$out/monocypher-ed25519.o"
	cc=${CC:-gcc}
	sanitize="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
	echo 'int main(void){return 0;}' | $cc -m32 $sanitize -x c - -o "$out/probe" 2>/dev/null || sanitize=
	mkdir -p "$out/zlib"
	printf '#include <stdlib.h>\n#define debug_malloc(size, clear, file, line) calloc(1, (size))\n#define debug_free(pointer, file, line) free(pointer)\n' > "$out/zlib/cseries.h"
	for source in adler32 crc32 deflate infblock infcodes inffast inflate inftrees infutil trees zutil; do
		$cc -m32 -O2 -w -I"$out/zlib" -c source/memory/zlib/$source.c -o "$out/zlib/$source.o"
	done
	$cc -m32 -g -O1 -Wall -Wextra -Wno-unused-parameter -Wno-multichar $sanitize -Iport/linux/game -Isource \
		"$here/crossplay_maps_test.c" port/linux/game/crossplay_maps.c port/linux/game/map_share_protocol.c \
		port/linux/game/cache_file_formats.c "$out/p2p_crypto.o" "$out/monocypher.o" "$out/monocypher-ed25519.o" \
		"$out"/zlib/*.o -no-pie -Wl,--gc-sections -o "$out/crossplay_maps_test"
	echo "crossplay_maps_test: ${sanitize:-no sanitizers}"
) || fail "the unit test did not build"
# (a Custom Edition map's hash, whose first 32 bits OpenCE sends as its version, worked out here
# apart from crossplay_maps.c: map_share_files.c's construction)
map_hash() { # map_hash FILE -> 64 hex digits
	python3 -I - "$1" <<'EOF'
import hashlib, sys
data = open(sys.argv[1], "rb").read()
whole = hashlib.blake2b(digest_size=32)
whole.update(b"OpenCE map 1")
whole.update(len(data).to_bytes(4, "little"))
for offset in range(0, len(data), 0x40000):
    whole.update(hashlib.blake2b(data[offset:offset + 0x40000], digest_size=32).digest())
print(whole.hexdigest())
EOF
}
ce_hash=
[ -n "$ce_map" ] && [ -f "$ce_map" ] && ce_hash=$(map_hash "$ce_map")
if [ -x "$out/crossplay_maps_test" ]; then
	CROSSPLAY_MAPS_TEST_CE_MAP=$ce_map CROSSPLAY_MAPS_TEST_CE_HASH=$ce_hash CROSSPLAY_MAPS_TEST_XBOX_MAP=$data/maps/bloodgulch.map \
		"$out/crossplay_maps_test" || status=1
fi

# ---- 2. the stand-in for a PC host, end to end
if [ "${CROSSPLAY_MAPS_UNIT_ONLY:-0}" = 1 ]; then
	echo "(CROSSPLAY_MAPS_UNIT_ONLY=1: the stand-in's netns cases skipped)"
elif [ -z "$ce_map" ] || [ ! -f "$ce_map" ]; then
	echo "(no HALO_TEST_CE_MAP: the stand-in's netns cases skipped)"
else
	ce_name=$(basename "$ce_map" .map)
	# (the version an OpenCE host of 29 sends: the hash's first 32 bits, 0 as 1)
	ce_digits=${ce_hash:0:8}
	[ "$ce_digits" = 00000000 ] && ce_digits=00000001
	checksum=$(printf '0x%08X' "0x$ce_digits")
	# a maps folder: the data's maps (links), then extras: "with" the CE map,
	# "differ" another copy of it, "pcbloodgulch" the CE map as bloodgulch.map
	# another version of the map: a copy, one byte of its header's checksum
	# changed (so its hash too)
	differ_copy() { # differ_copy PATH
		cp "$ce_map" "$1"
		python3 -I - "$1" <<'EOF'
import sys
with open(sys.argv[1], "r+b") as f:
    f.seek(0x64); b = f.read(1); f.seek(0x64); f.write(bytes([b[0] ^ 0x5A]))
EOF
	}
	maps_folder() { # maps_folder NAME [with|differ|pcbloodgulch|verfile|beside|verwrong]
		local d=$out/data-$1
		mkdir -p "$d/maps"
		for f in "$data"/maps/*.map; do ln -sfn "$(readlink -f "$f")" "$d/maps/$(basename "$f")"; done
		case ${2:-} in
		with) ln -sfn "$(readlink -f "$ce_map")" "$d/maps/$ce_name.map" ;;
		differ) differ_copy "$d/maps/$ce_name.map" ;;
		pcbloodgulch)
			rm -f "$d/maps/bloodgulch.map"
			ln -sfn "$(readlink -f "$ce_map")" "$d/maps/bloodgulch.map" ;;
		# (the host's version under its version's file name, alone or beside
		# another version as <map>.map; another version under that name)
		verfile) ln -sfn "$(readlink -f "$ce_map")" "$d/maps/${ce_name}_$ce_digits.map" ;;
		beside)
			ln -sfn "$(readlink -f "$ce_map")" "$d/maps/${ce_name}_$ce_digits.map"
			differ_copy "$d/maps/$ce_name.map" ;;
		verwrong) differ_copy "$d/maps/${ce_name}_$ce_digits.map" ;;
		esac
		echo "$d"
	}
	run_case() { # run_case NAME HOSTGAME HOSTMAPS JOINERMAPS
		local name=$1 game=$2 host_data=$3 joiner_data=$4
		echo "=== $name: the host on '${game#host:}', the joiner's maps: $(basename "$joiner_data")"
		HALO_TEST_OUT="$out/$name" HALO_TEST_KEEP=1 HALO_TEST_SECONDS=${CROSSPLAY_MAPS_SECONDS:-150} \
			HALO_TEST_REJOIN=0 HALO_TEST_HOST_GAME="$game" HALO_TEST_DATA_HOST="$host_data" HALO_TEST_DATA_JOINER="$joiner_data" \
			HALO_TEST_ENV="HALO_CROSSPLAY=true HALO_TEST_CROSSPLAY_HOST=1 HALO_CUSTOM_EDITION=1 ${HALO_TEST_ENV:-}" \
			"$here/run_netns_online_test.sh" lan > "$out/$name.log" 2>&1
		case_status=$?
		tail -3 "$out/$name.log" | sed 's/^/  netns: /'
		host_log=$out/$name/host/run.log
		joiner_log=$out/$name/joiner/run.log
		[ -f "$host_log" ] && [ -f "$joiner_log" ] || { fail "$name: no logs"; return; }
		grep -a "join: the host's map is" "$joiner_log" | head -1 | sed 's/^.*join: /  joiner: /'
		grep -ah "crossplay: the PC host's\|map: the PC host's" "$joiner_log" "$out/$name/joiner/data/debug.txt" 2>/dev/null |
			sed 's/^.*\(crossplay\|map\): /  joiner: /' | sort -u | head -3
		# (never a map share message toward a PC host: the joiner asks
		# nothing, the host is asked nothing)
		if grep -aqE "map share: asking the host|asking the host for its map|asking the player to turn on PC maps" "$joiner_log"; then
			fail "$name: the joiner asked the host for its map (map sharing toward a PC host)"
		fi
		grep -aqE "map share: .*(query|asks)" "$host_log" && fail "$name: the host was asked for its map"
	}
	played() { # played NAME: the joiner played a game on the host's map
		grep -aq "network test: tick [0-9]* " "$out/$1/joiner/run.log" 2>/dev/null
	}
	refused() { # refused NAME PATTERN: the joiner said why, and never joined the game
		local name=$1 pattern=$2
		grep -aqE "$pattern" "$out/$name/joiner/run.log" "$out/$name/joiner/data/debug.txt" 2>/dev/null ||
			fail "$name: the joiner did not say /$pattern/"
		played "$name" && fail "$name: the joiner played instead of refusing"
	}
	host_maps=$(maps_folder host with)
	cases=${CROSSPLAY_MAPS_CASES:-stock ce cemissing cediffer cexbox cever cebeside ceverwrong}
	for c in $cases; do
		case $c in
		stock)
			run_case stock host:bloodgulch:slayer "$host_maps" "$(maps_folder joiner-stock)"
			grep -aq "join: the host's map is levels.test.bloodgulch.bloodgulch (copy 0x00000000)" "$out/stock/joiner/run.log" ||
				fail "stock: the host did not name Blood Gulch as an OpenCE host does"
			played stock || fail "stock: the joiner did not play"
			[ "$case_status" = 0 ] || fail "stock: netns lan mode failed (its log: $out/stock.log)" ;;
		ce)
			run_case ce "host:custom_maps\\$ce_name:slayer" "$host_maps" "$(maps_folder joiner-ce with)"
			grep -aq "join: the host's map is custom_maps.$ce_name (copy $checksum)" "$out/ce/joiner/run.log" ||
				fail "ce: the host did not name custom_maps\\$ce_name with its version $checksum"
			grep -aq "the PC host's Custom Edition map '$ce_name' ($checksum) is this machine's" "$out/ce/joiner/data/debug.txt" ||
				fail "ce: the joiner did not find its copy the host's"
			played ce || fail "ce: the joiner did not play" ;;
		cemissing)
			run_case cemissing "host:custom_maps\\$ce_name:slayer" "$host_maps" "$(maps_folder joiner-missing)"
			refused cemissing "The PC host is playing the map $ce_name, which isn't in your maps folder" ;;
		cediffer)
			run_case cediffer "host:custom_maps\\$ce_name:slayer" "$host_maps" "$(maps_folder joiner-differ differ)"
			refused cediffer "Your $ce_name.map isn't the same version as the PC host's" ;;
		cexbox)
			run_case cexbox 'host:custom_maps\bloodgulch:slayer' "$(maps_folder host-pcbloodgulch pcbloodgulch)" \
				"$(maps_folder joiner-xbox)"
			refused cexbox "The PC host is playing the PC \(Custom Edition\) map Blood Gulch, but your bloodgulch.map is an Xbox map" ;;
		cever|cebeside)
			run_case $c "host:custom_maps\\$ce_name:slayer" "$host_maps" \
				"$(maps_folder joiner-$c $([ $c = cever ] && echo verfile || echo beside))"
			grep -aq "join: the host's map is custom_maps.$ce_name (copy $checksum)" "$out/$c/joiner/run.log" ||
				fail "$c: the host did not name custom_maps\\$ce_name with its version $checksum"
			grep -aq "crossplay: .*${ce_name}_$ce_digits.map hashed: version $ce_digits" "$out/$c/joiner/data/debug.txt" ||
				fail "$c: the joiner did not find its ${ce_name}_$ce_digits.map of the host's version"
			played $c || fail "$c: the joiner did not play" ;;
		ceverwrong)
			run_case ceverwrong "host:custom_maps\\$ce_name:slayer" "$host_maps" "$(maps_folder joiner-ceverwrong verwrong)"
			refused ceverwrong "The PC host is playing the map $ce_name, which isn't in your maps folder" ;;
		*) fail "unknown case $c" ;;
		esac
	done
fi
[ $status = 0 ] && echo "PASS (crossplay maps)" || echo "FAIL (crossplay maps: $out)"
test_out_done $status
exit $status
