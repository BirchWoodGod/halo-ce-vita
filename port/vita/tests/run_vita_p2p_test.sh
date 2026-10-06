#!/bin/sh
# Internet play (port/linux/src/p2p*.c, as the Vita build compiles them)
# over the Vita's socket layer (port/vita/host/vita_net.c) on a mock sceNet
# with the Vita's semantics: two processes, a host and a joiner, find each
# other by short code through mqtt_test_broker.py, then pass a datagram
# each way and a stream message each way through the tunnel
# (vita_p2p_test.c).
#   run_vita_p2p_test.sh [code]   by short code, through the test broker
#   run_vita_p2p_test.sh adhoc    in an ad hoc group: the dialog scripted,
#                                 PDP mocked over UDP, ad hoc play's bridge Needs the Linux build configured and built in this
# tree (build/linux: the platform layer's generated semantics header) and
# clang that targets i686 (CLANG, default the Linux build's compiler from
# build.ninja).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
sdk=${VITASDK:-$HOME/vitasdk}
out=${TMPDIR:-/tmp}/vita_p2p_test.$$
clang=${CLANG:-$(sed -n 's/^linux_cc = //p' "$root/build.ninja" | head -1)}
[ -n "$clang" ] || clang=clang
mkdir -p "$out/include"
ln -s "$sdk/arm-vita-eabi/include/psp2" "$out/include/psp2"
ln -s "$sdk/arm-vita-eabi/include/psp2common" "$out/include/psp2common"
ln -s "$sdk/arm-vita-eabi/include/vitasdk" "$out/include/vitasdk" 2>/dev/null || true
cd "$root"
# the game's ABI, as the platform layer is built, with HALO_VITA
game_flags="--target=i686-linux-gnu -m32 -fms-extensions -fshort-wchar -malign-double -fcommon -fno-pic
	-fno-strict-aliasing -fwrapv -freg-struct-return -ffunction-sections -fdata-sections -O2 -g -std=gnu11 -D_GNU_SOURCE -DHALO_LINUX_PLATFORM_LAYER
	-DHALO_VITA -w -include port/linux/include/halo_linux_prefix.h -include build/linux/platform_msvc_semantics.h
	-Iport/linux/src -Iport/linux/include -Iport/third_party/kcp -Isource -Isource/cseries -idirafter port/include/xdk"
for source in port/linux/src/p2p.c port/linux/src/p2p_signal.c port/linux/src/p2p_crypto.c port/linux/src/p2p_adhoc.c \
	port/third_party/kcp/ikcp.c; do
	$clang $game_flags -c $source -o "$out/$(basename $source .c).o"
done
cc=${CC:-gcc}
$cc -m32 -pthread -g -O1 -w -D_GNU_SOURCE -I"$out/include" -I"$root/port/linux/src" -I"$root/port/vita/include" \
	-c port/vita/host/vita_net.c -o "$out/vita_net.o"
$cc -m32 -pthread -g -O1 -w -D_GNU_SOURCE -I"$out/include" -c "$here/mock_scenet.c" -o "$out/mock_scenet.o"
$cc -m32 -pthread -g -O1 -Wall -D_GNU_SOURCE -I"$root/port/linux/src" -c "$here/vita_p2p_test.c" -o "$out/vita_p2p_test.o"
# (the XDK headers' inline functions are kept in each object: dropped unused)
$cc -m32 -pthread -no-pie -Wl,--gc-sections -o "$out/vita_p2p_test" "$out"/*.o
mode=${1:-code}
status=0
if [ "$mode" = adhoc ]; then
	# two "Vitas" in one ad hoc group: 127.0.0.220 and .221 for the PDP mock
	export VITA_NET_TEST_LOG=1 MOCK_THREADS=1 HALO_NET_ADHOC=true
	MOCK_ADHOC_ADDRESS=127.0.0.220 MOCK_PDP_GROUP=127.0.0.221 MOCK_LOCAL_ADDRESS=0.0.0.0 \
		timeout -k 2 60 "$out/vita_p2p_test" adhoc-host > "$out/host.log" 2>&1 &
	host=$!
	sleep 1
	MOCK_ADHOC_ADDRESS=127.0.0.221 MOCK_PDP_GROUP=127.0.0.220 MOCK_LOCAL_ADDRESS=0.0.0.0 \
		timeout -k 2 60 "$out/vita_p2p_test" adhoc-join > "$out/join.log" 2>&1 || status=1
else
	port=$((19000 + $$ % 1000))
	python3 "$here/mqtt_test_broker.py" --port $port > "$out/broker.log" 2>&1 &
	broker=$!
	trap 'kill $broker 2>/dev/null || true' EXIT
	sleep 0.5
	# (the address each "console" reports for itself, which internet play
	# offers the other: both are on this computer)
	export TEST_BROKER=127.0.0.1:$port VITA_NET_TEST_LOG=1 MOCK_LOCAL_ADDRESS=127.0.0.1
	timeout -k 2 60 "$out/vita_p2p_test" host > "$out/host.log" 2>&1 &
	host=$!
	code=
	for i in $(seq 1 30); do
		code=$(sed -n 's/^CODE //p' "$out/host.log")
		[ -n "$code" ] && break
		sleep 0.5
	done
	if [ -z "$code" ]; then
		echo "FAIL the host never showed a code"
		status=1
	else
		timeout -k 2 60 "$out/vita_p2p_test" join "$code" > "$out/join.log" 2>&1 || status=1
	fi
fi
wait $host || status=1
echo "--- host"; grep -E '^(PASS|FAIL)|connected|code|ad ?hoc' "$out/host.log"
echo "--- joiner"; grep -E '^(PASS|FAIL)|connected|code|ad ?hoc' "$out/join.log" 2>/dev/null
if grep -q '^FAIL' "$out/host.log" "$out/join.log" 2>/dev/null; then status=1; fi
[ $status = 0 ] && echo "PASS ($mode) internet play over the Vita's socket layer" || echo "FAIL ($mode; logs in $out)"
[ $status = 0 ] && rm -rf "$out"
exit $status
