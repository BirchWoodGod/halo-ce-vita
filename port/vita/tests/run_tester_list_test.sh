#!/bin/sh
# The signed tester list (port/linux/src/tester_list.c; the flaming helmet's,
# p2p_flame.inc) and the owner's tool for it (tools/release/sign_testers.py),
# with a throwaway key made in the test's folder (never the owner's):
#   tester_list_test.c with ASan and UBSan (signature, tampering, form,
#   expiry, the newest version winning), then sign_testers.py: a list made
#   from a tester-id.txt, a key and a friends.dat, shown, a tester removed
#   (the version bumped), refused with the wrong key or an older --version,
#   and the C reader taking the tool's list.
# Needs clang that targets i686 (FUZZ_CLANG, default clang) and python3.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/tester_list_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out/keys"
trap 'test_out_done $?' EXIT
cd "$root"
status=0
fail() { echo "FAIL $*"; status=1; }

python3 tools/release/make_update_key.py --private "$out/keys/test.key" > /dev/null
python3 tools/release/make_update_key.py --private "$out/keys/other.key" > /dev/null
seed=$(grep -v '^#' "$out/keys/test.key")
public=$(sed -n 's/^# public key: //p' "$out/keys/test.key")

# ---------- the tool
tool="python3 tools/release/sign_testers.py --test --key $out/keys/test.key --list $out/testers.txt"
# (a tester's tester-id.txt, as the game writes it; a second tester by key)
python3 - "$out" <<'EOF'
import sys
sys.path.insert(0, "tools/release")
import ed25519_rfc8032 as ed
out = sys.argv[1]
for name, byte in (("alpha", 0x51), ("bravo", 0x52), ("charlie", 0x53)):
    key = ed.public_key(bytes([byte]) * 32)
    open(f"{out}/{name}.key", "w").write(key.hex())
    open(f"{out}/{name}-tester-id.txt", "w").write(f"halo-ce-vita-tester-id 1\nkey {key.hex()}\ncode X\n")
# a friends.dat holding charlie (p2p_friends.c's list_encode: the friend's record)
key = ed.public_key(bytes([0x53]) * 32)
record = key + bytes([7]) + b"Charlie".ljust(16, b"\0") + bytes(16) + bytes(8) + bytes(8) + bytes(4)
open(f"{out}/friends.dat", "wb").write(b"HFRL" + bytes([1, 1, 0, 0, 0, 0, 0]) + record + bytes(32))
EOF
$tool --add-id "$out/alpha-tester-id.txt" > "$out/tool.log" 2>&1 || fail "sign_testers.py --add-id"
$tool --add-key "$(cat "$out/bravo.key")" --label Bravo --expires 2030-01-01 >> "$out/tool.log" 2>&1 ||
	fail "sign_testers.py --add-key"
$tool --friends-dat "$out/friends.dat" --add-friend Charlie >> "$out/tool.log" 2>&1 || fail "sign_testers.py --add-friend"
grep -q "^version 3$" "$out/testers.txt" || fail "three changes: version 3"
[ "$(grep -c '^tester ' "$out/testers.txt")" = 3 ] || fail "three testers listed"
grep -q " 1893542400 Bravo$" "$out/testers.txt" || fail "--expires 2030-01-01: the end of that day, UTC"
$tool --remove Bravo >> "$out/tool.log" 2>&1 || fail "sign_testers.py --remove"
grep -q "^version 4$" "$out/testers.txt" && ! grep -q Bravo "$out/testers.txt" || fail "removed: version 4, no Bravo"
python3 tools/release/sign_testers.py --list "$out/testers.txt" --public "$public" --show > "$out/show.log" 2>&1 ||
	fail "sign_testers.py --show"
grep -q "version 4, 2 testers, signature good" "$out/show.log" || fail "--show says the version, testers, signature"
if python3 tools/release/sign_testers.py --test --key "$out/keys/other.key" --list "$out/testers.txt" --remove Charlie \
	> /dev/null 2>&1; then
	fail "a list signed with another key changed"
fi
if $tool --version 2 --remove Charlie > /dev/null 2>&1; then fail "an older --version taken"; fi
if python3 tools/release/sign_testers.py --key "$out/keys/test.key" --list "$out/testers.txt" --remove Charlie \
	> /dev/null 2>&1; then
	fail "a key that is not the release key taken without --test"
fi
grep -q "^version 4$" "$out/testers.txt" || fail "the refused changes left the list as it was"
[ $status = 0 ] && echo "PASS sign_testers.py"

# ---------- the reader, with ASan and UBSan
flags="-ffunction-sections -fdata-sections -fms-extensions -fshort-wchar -fcommon -fno-strict-aliasing -fwrapv -g -std=gnu11
	-D_GNU_SOURCE -DHALO_LINUX_PLATFORM_LAYER -DHALO_VITA -w -include port/linux/include/halo_linux_prefix.h
	-Iport/linux/src -Iport/linux/include -Iport/third_party/monocypher -Isource -Isource/cseries -idirafter port/include/xdk"
${FUZZ_CLANG:-clang} -m32 -O1 -fsanitize=address,undefined -fno-sanitize-recover=all $flags \
	-DHALO_TESTERS_TEST_KEY="\"$public\"" -Wl,--unresolved-symbols=ignore-in-object-files \
	-o "$out/tester_list_test" "$here/tester_list_test.c" port/linux/src/tester_list.c port/linux/src/p2p_crypto.c \
	port/third_party/monocypher/monocypher.c port/third_party/monocypher/monocypher-ed25519.c
TESTERS_TEST_SEED=$seed TESTERS_TEST_FILE="$out/testers.txt" TESTERS_TEST_FILE_VERSION=4 \
	TESTERS_TEST_FILE_KEY=$(cat "$out/alpha.key") "$out/tester_list_test" || status=1
exit $status
