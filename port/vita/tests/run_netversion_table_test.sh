#!/bin/sh
# The signed crossplay version table (port/linux/src/netversion_table.c;
# network_wire.c uses it, netversion_fetch.c fetches it) and the owner's tool
# for it (tools/release/sign_netversions.py), with a throwaway key made in the
# test's folder (never the owner's):
#   sign_netversions.py: a table made (join 40 by 39's rules, refuse 50 with
#   a reason), shown, a row removed (the version bumped); refused: another
#   key's change, an older --version, a key not the release key without
#   --test, 18, a built-in version joined, rules this source lacks (unless
#   --allow-newer-rules), a reason out of its set; the C reader taking the
#   tool's table;
#   netversion_table_test.c with ASan and UBSan (signature, form, size, the
#   newest version winning).
# Needs clang that targets i686 (FUZZ_CLANG, default clang) and python3.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/netversion_table_test.$$
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
table=$out/release/netversions.txt
tool="python3 tools/release/sign_netversions.py --test --key $out/keys/test.key --table $table"
$tool --join 40:39 > "$out/tool.log" 2>&1 || fail "sign_netversions.py --join"
$tool --refuse 50 "OpenCE 50 changed the record" >> "$out/tool.log" 2>&1 || fail "sign_netversions.py --refuse"
$tool --join 45:38 >> "$out/tool.log" 2>&1 || fail "sign_netversions.py --join 45:38"
grep -q "^version 3$" "$table" || fail "three changes: version 3"
[ -f "$table.sig" ] && [ "$(wc -c < "$table.sig")" = 129 ] || fail "the signature beside it: 128 digits and a newline"
$tool --remove 45 >> "$out/tool.log" 2>&1 || fail "sign_netversions.py --remove"
grep -q "^version 4$" "$table" && ! grep -q "^join 45" "$table" || fail "removed: version 4, no 45"
[ "$(sed -n 3,4p "$table")" = "join 40 rules 39
refuse 50 OpenCE 50 changed the record" ] || fail "the rows in order: join 40 rules 39, refuse 50 ..."
python3 tools/release/sign_netversions.py --table "$table" --public "$public" --show > "$out/show.log" 2>&1 ||
	fail "sign_netversions.py --show"
grep -q "version 4, 2 rows, signature good" "$out/show.log" || fail "--show says the version, rows, signature"
grep -q "join 40 rules 39: joined, played by 39's rules" "$out/show.log" || fail "--show says what 40 is"
grep -q "the crossplay wire joins: 29, 32, 38, 39, 40" "$out/show.log" || fail "--show says what is joined"
refused() { # WHAT ARGUMENTS...: the tool must refuse it and leave the table
	what=$1
	shift
	if "$@" > "$out/refused.log" 2>&1; then fail "$what taken"; fi
}
refused "a table signed with another key changed" python3 tools/release/sign_netversions.py --test \
	--key "$out/keys/other.key" --table "$table" --join 41:39
refused "an older --version" $tool --version 2 --join 41:39
refused "the same --version" $tool --version 4 --join 41:39
refused "a key that is not the release key without --test" python3 tools/release/sign_netversions.py \
	--key "$out/keys/test.key" --table "$table" --join 41:39
refused "--join 18" $tool --join 18:32
refused "--join of a built-in version" $tool --join 39:38
refused "--join with rules this source lacks" $tool --join 41:41
refused "--refuse 18" $tool --refuse 18 "the Vitas"
refused "--refuse 32, what Vitas host" $tool --refuse 32 "no"
refused "a reason with a quote" $tool --refuse 41 'say "no"'
refused "a reason too long" $tool --refuse 41 "0123456789012345678901234567890123456789012345678901234567890123456789"
refused "a reason ending in a space" $tool --refuse 41 "trailing "
refused "--remove of no row" $tool --remove 77
grep -q "^version 4$" "$table" || fail "the refused changes left the table as it was"
$tool --join 41:41 --allow-newer-rules >> "$out/tool.log" 2>&1 || fail "--allow-newer-rules"
grep -q "^join 41 rules 41$" "$table" && grep -q "^version 5$" "$table" || fail "join 41 rules 41 with --allow-newer-rules"
$tool --remove 41 >> "$out/tool.log" 2>&1 || fail "--remove 41"
cp "$table" "$out/tampered.txt"
cp "$table.sig" "$out/tampered.txt.sig"
sed -i 's/rules 39/rules 38/' "$out/tampered.txt"
if python3 tools/release/sign_netversions.py --table "$out/tampered.txt" --public "$public" --show > /dev/null 2>&1; then
	fail "--show took a tampered table"
fi
[ $status = 0 ] && echo "PASS sign_netversions.py"

# ---------- the reader, with ASan and UBSan
flags="-ffunction-sections -fdata-sections -fms-extensions -fshort-wchar -fcommon -fno-strict-aliasing -fwrapv -g -std=gnu11
	-D_GNU_SOURCE -DHALO_LINUX_PLATFORM_LAYER -DHALO_VITA -w -include port/linux/include/halo_linux_prefix.h
	-Iport/linux/src -Iport/linux/include -Iport/third_party/monocypher -Isource -Isource/cseries -idirafter port/include/xdk"
${FUZZ_CLANG:-clang} -m32 -O1 -fsanitize=address,undefined -fno-sanitize-recover=all $flags \
	-DHALO_NETVERSIONS_TEST_KEY="\"$public\"" -Wl,--unresolved-symbols=ignore-in-object-files \
	-o "$out/netversion_table_test" "$here/netversion_table_test.c" port/linux/src/netversion_table.c \
	port/linux/src/p2p_crypto.c port/third_party/monocypher/monocypher.c port/third_party/monocypher/monocypher-ed25519.c
NETVERSIONS_TEST_SEED=$seed NETVERSIONS_TEST_FILE="$table" NETVERSIONS_TEST_FILE_VERSION=6 \
	"$out/netversion_table_test" || status=1
exit $status
