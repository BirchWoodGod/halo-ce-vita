#!/bin/sh
# The Vita updater's tests (port/vita/include/vita_update_verify.h,
# vita_update_download.h), each built 32-bit as the Vita and the Linux game
# are, with AddressSanitizer and UBSan, with a throwaway signing key made
# here by tools/release/make_update_key.py (never a real one):
#   update_download_test.c     the signature, manifest, version, title, size,
#                              URL and redirect checks; again built with the
#                              placeholder key (refuses all)
#   update_download_fuzz.c     every truncation and UPDATE_FUZZ_ITERATIONS
#                              (default 30000) changes of a signed manifest,
#                              random inputs to every reader; with
#                              UPDATE_FUZZ_SECONDS=n also n seconds of
#                              libFuzzer (64-bit; clang)
#   update_download_harness.c  the whole path (download, checks, the verified
#                              VPK kept, the start's check of it) against
#                              update_download_server.py on 127.0.0.1, over
#                              plain HTTP and over TLS with an authority made
#                              here (built into this test only:
#                              POSIX_HTTPS_TEST_ROOTS), releases signed by
#                              tools/release/sign_update.py (so the Python
#                              signer and Monocypher's check agree), a folder
#                              standing for ux0:data/haloce-vita: a verified
#                              VPK kept at update/halo-<version>.vpk, nothing
#                              kept of a refusal, the rest untouched
#   CC  the compiler (default gcc)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/update_download_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
cc=${CC:-gcc}
mkdir -p "$out"
server_pids=
cleanup() {
	rc=$?
	for pid in $server_pids; do kill "$pid" 2>/dev/null || true; done
	test_out_done $rc
}
trap cleanup EXIT
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
mono=$root/port/third_party/monocypher
flags="-m32 -g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -I$root/port/vita/include -I$root/port/linux/src -I$mono"
export ASAN_OPTIONS=detect_leaks=1:exitcode=99 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:exitcode=98
status=0
fail() { echo "FAIL $*"; status=1; }
pass() { echo "PASS $*"; }

# ---------- the keys (throwaway, in the test's folder)
python3 "$root/tools/release/make_update_key.py" --private "$out/keys/test.key" > /dev/null
python3 "$root/tools/release/make_update_key.py" --private "$out/keys/other.key" > /dev/null
seed=$(grep -v '^#' "$out/keys/test.key")
public=$(sed -n 's/^# public key: //p' "$out/keys/test.key")
printf '#define HALO_UPDATE_PUBLIC_KEY "%s"\n#define HALO_TEST_SEED "%s"\n' "$public" "$seed" > "$out/test_key.h"
keyflags="-include $out/test_key.h"

echo "--- update_download_test"
verify_sources="$root/port/vita/host/vita_update_verify.c $root/port/linux/src/update_check.c $mono/monocypher.c $mono/monocypher-ed25519.c"
$cc $flags $sanitize $keyflags "$here/update_download_test.c" $verify_sources -o "$out/update_download_test"
"$out/update_download_test" || status=1
echo "--- update_download_test (placeholder key)"
$cc $flags $sanitize -DHALO_TEST_PLACEHOLDER "$here/update_download_test.c" $verify_sources -o "$out/update_download_placeholder"
"$out/update_download_placeholder" || status=1

echo "--- update_download_fuzz"
$cc $flags $sanitize $keyflags "$here/update_download_fuzz.c" $verify_sources -o "$out/update_download_fuzz"
"$out/update_download_fuzz" || status=1
seconds=${UPDATE_FUZZ_SECONDS:-0}
if [ "$seconds" != 0 ]; then
	found=${UPDATE_FUZZ_OUT:-$out/found}
	mkdir -p "$found/corpus"
	clang -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -DUPDATE_FUZZ_LIBFUZZER -g -O1 \
		-I"$root/port/vita/include" -I"$root/port/linux/src" -I"$mono" $keyflags "$here/update_download_fuzz.c" \
		$verify_sources -o "$out/update_download_libfuzzer"
	"$out/update_download_libfuzzer" -max_total_time="$seconds" -max_len=4096 -print_final_stats=1 \
		-artifact_prefix="$found/" "$found/corpus" > "$found/fuzz.log" 2>&1 || status=1
	grep -E 'stat::number_of_executed_units|cov:.*ft:' "$found/fuzz.log" | tail -2 || true
	ls "$found" | grep -E '^(crash|leak|timeout|oom)-' && status=1
fi

echo "--- update_download_harness (build)"
mkdir -p "$out/mbedtls"
ls "$root"/port/third_party/mbedtls/library/*.c | xargs -P "$(nproc)" -I{} sh -c \
	'$0 -m32 -O1 -w -I"$1/include" -I"$1/library" -c "{}" -o "$2/$(basename "{}" .c).o"' \
	"$cc" "$root/port/third_party/mbedtls" "$out/mbedtls"
# (the tests' authority, and localhost's certificate from it; a second
# certificate of its own, from no authority)
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 -subj "/CN=Halo Update Test CA" \
	-addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" \
	-keyout "$out/ca_key.pem" -out "$out/ca.pem" 2>/dev/null
openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -subj "/CN=localhost" \
	-keyout "$out/tls_key.pem" -out "$out/tls.csr" 2>/dev/null
printf 'subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\n' > "$out/tls.ext"
openssl x509 -req -in "$out/tls.csr" -CA "$out/ca.pem" -CAkey "$out/ca_key.pem" -CAcreateserial -days 2 \
	-extfile "$out/tls.ext" -out "$out/tls.pem" 2>/dev/null
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 -subj "/CN=localhost" \
	-addext "subjectAltName=DNS:localhost" -keyout "$out/self_key.pem" -out "$out/self.pem" 2>/dev/null
python3 - "$out/ca.pem" "$out/test_roots.h" <<'PY'
import sys
lines = open(sys.argv[1]).read().strip().splitlines()
with open(sys.argv[2], "w") as out:
    out.write("static const char posix_https_test_roots_pem[] =\n")
    for line in lines:
        out.write('\t"%s\\n"\n' % line)
    out.write(";\n")
PY
harness_sources="$here/update_download_harness.c $root/port/vita/host/vita_update_download.c
	$root/port/linux/src/posix_https.c $root/port/linux/src/posix_net.c $verify_sources"
printf '#define POSIX_HTTPS_TEST_ROOTS "%s"\n' "$out/test_roots.h" > "$out/test_roots_define.h"
$cc $flags $sanitize $keyflags -include "$out/test_roots_define.h" -I"$root/port/third_party/mbedtls/include" \
	$harness_sources "$out"/mbedtls/*.o -lpthread -o "$out/harness"
$cc $flags $sanitize -I"$root/port/third_party/mbedtls/include" $harness_sources "$out"/mbedtls/*.o -lpthread \
	-o "$out/harness_placeholder"

# ---------- the releases (tools/release/sign_update.py, as the owner signs)
mkdir -p "$out/releases" "$out/template"
python3 - "$root" "$out" <<'PY'
import os, shutil, struct, subprocess, sys, zipfile
root, out = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.join(root, "tools/release"))
import update_signing as common

def sfo(title):
    keys = [b"APP_VER", b"TITLE", b"TITLE_ID"]
    values = [b"01.10", b"Halo CE", title.encode()]
    key_table = 20 + 16 * len(keys)
    key_blob = b"".join(k + b"\0" for k in keys)
    key_blob += b"\0" * (-len(key_blob) % 4)
    data_table = key_table + len(key_blob)
    entries, data, key_offset = b"", b"", 0
    for key, value in zip(keys, values):
        value += b"\0"
        size = (len(value) + 3) & ~3
        entries += struct.pack("<HHIII", key_offset, 0x0204, len(value), size, len(data))
        data += value + b"\0" * (size - len(value))
        key_offset += len(key) + 1
    return struct.pack("<4sIIII", b"\0PSF", 0x101, key_table, data_table, len(keys)) + entries + key_blob + data

def vpk(path, version, title="HCEV00001"):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("sce_sys/param.sfo", sfo(title))
        archive.writestr("eboot.bin", ("NEW EBOOT " + version + "\n").encode() * 3000)
        info = zipfile.ZipInfo("shaders.pak")
        info.compress_type = zipfile.ZIP_STORED
        archive.writestr(info, os.urandom(70000))

def release(version, key="test.key", title="HCEV00001"):
    folder = os.path.join(out, "releases", "v" + version)
    os.makedirs(folder)
    vpk(os.path.join(folder, "halo.vpk"), version, title)
    subprocess.run([sys.executable, os.path.join(root, "tools/release/sign_update.py"), "--key",
                    os.path.join(out, "keys", key), "--vpk", os.path.join(folder, "halo.vpk"), "--version", version,
                    "--out", folder], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return folder

def sign_text(folder, text):
    seed = common.read_private_key(os.path.join(out, "keys", "test.key"))
    open(os.path.join(folder, "halo-update.txt"), "wb").write(text.encode())
    open(os.path.join(folder, "halo-update.txt.sig"), "w").write(common.ed25519.sign(seed, text.encode()).hex() + "\n")

good = release("1.1.0-beta.4")
release("1.1.0-beta.5")                                  # a newer one (the older kept is removed)
release("1.1.0-rc.1")                                    # a release candidate
release("1.1.0-beta.2")                                  # older than the running build
replay = os.path.join(out, "releases", "v1.1.0-beta.16") # beta.4's files under another tag
shutil.copytree(good, replay)
tampered = release("1.1.0-beta.6")                       # the VPK changed after signing
data = bytearray(open(os.path.join(tampered, "halo.vpk"), "rb").read())
data[len(data) // 2] ^= 1
open(os.path.join(tampered, "halo.vpk"), "wb").write(data)
release("1.1.0-beta.7", key="other.key")                 # another key's signature
changed = release("1.1.0-beta.8")                        # the manifest changed after signing
text = open(os.path.join(changed, "halo-update.txt")).read()
open(os.path.join(changed, "halo-update.txt"), "w").write(text.replace("title HCEV00001", "title HCEV00002"))
big = release("1.1.0-beta.11")                           # signed, but 70 MB
size, sha = common.file_digest(os.path.join(big, "halo.vpk"))
sign_text(big, common.manifest_text("1.1.0-beta.11", "HCEV00001", 70000000, sha))
release("1.1.0-beta.12", title="OTHER0001")              # another game's VPK, signed
nosig = release("1.1.0-beta.13")                         # no signature published
os.remove(os.path.join(nosig, "halo-update.txt.sig"))
lying = release("1.1.0-beta.15")                         # the manifest's SHA-512 is another file's
size, sha = common.file_digest(os.path.join(lying, "halo.vpk"))
sign_text(lying, common.manifest_text("1.1.0-beta.15", "HCEV00001", size, "0" * 128))

# the data folder as a Vita has it
template = os.path.join(out, "template")
for name, data in [("settings.txt", b"HALO_UPDATE_CHANNEL=experimental\n"), ("config.toml", b"[game]\n"),
                   ("maps/bloodgulch.map", os.urandom(4096)), ("saves/campaign.sav", b"save"),
                   ("movies/intro.mp4", b"movie"), ("update_check.txt", b"cache")]:
    os.makedirs(os.path.dirname(os.path.join(template, name)), exist_ok=True)
    open(os.path.join(template, name), "wb").write(data)
PY

python3 "$here/update_download_server.py" "$out/releases" "$out/port" "$out/requests" & server_pids="$server_pids $!"
python3 "$here/update_download_server.py" "$out/releases" "$out/tls_port" "$out/tls_requests" --tls "$out/tls.pem" \
	"$out/tls_key.pem" & server_pids="$server_pids $!"
python3 "$here/update_download_server.py" "$out/releases" "$out/self_port" "$out/self_requests" --tls "$out/self.pem" \
	"$out/self_key.pem" & server_pids="$server_pids $!"
for i in $(seq 50); do [ -s "$out/port" ] && [ -s "$out/tls_port" ] && [ -s "$out/self_port" ] && break; sleep 0.1; done
http="http://127.0.0.1:$(cat "$out/port")"
https="https://localhost:$(cat "$out/tls_port")"
self="https://localhost:$(cat "$out/self_port")"

# ---------- scenarios
# tree DIR: one line per file, its path and SHA-256 (the updater's own
# folder left out)
tree() { (cd "$1" && find . -type f ! -path './update/*' | LC_ALL=C sort | xargs -r sha256sum); }
data_before=$(tree "$out/template")
# scenario NAME: a fresh copy of the data folder in $data
scenario() {
	data=$out/s/$1
	mkdir -p "$out/s"
	cp -r "$out/template" "$data"
}
# run ACTION BASE VERSION [CURRENT]: the harness's result line in $result, all of it in $log
run() {
	action=$1
	shift
	log=$("$out/harness" "$action" "$data" "$@" 2>&1) || { echo "$log"; fail "$action crashed"; }
	result=$(printf '%s\n' "$log" | sed -n 's/^result //p' | tail -1)
}
expect() { # CODE WHAT [text in the result]
	case "$result" in
	"$1"*) if [ -n "$3" ] && ! printf '%s' "$result" | grep -q -- "$3"; then echo "$log"; fail "$2: $result"; else pass "$2 ($result)"; fi ;;
	*) echo "$log"; fail "$2: $result" ;;
	esac
}
# nothing_kept WHAT: no VPK of any kind, no download under way, the rest untouched
nothing_kept() {
	[ "$(tree "$data")" = "$data_before" ] || fail "$1: the data folder changed"
	if [ -d "$data/update" ] && [ -n "$(ls -A "$data/update")" ]; then ls "$data/update"; fail "$1: files left in update/"; fi
}
# kept_only WHAT VERSION: update/ holds exactly that release's three files, the VPK the release's
kept_only() {
	[ "$(tree "$data")" = "$data_before" ] || fail "$1: the data folder changed"
	[ "$(ls "$data/update" | LC_ALL=C sort | tr '\n' ' ')" = "halo-$2.txt halo-$2.txt.sig halo-$2.vpk " ] ||
		{ ls "$data/update"; fail "$1: update/ holds other files"; }
	cmp -s "$data/update/halo-$2.vpk" "$out/releases/v$2/halo.vpk" || fail "$1: the kept VPK is not the release's"
}

echo "--- update_download_harness"
scenario good
run fetch "$http/dl" 1.1.0-beta.4
expect "0 $data/update/halo-1.1.0-beta.4.vpk" "a signed update downloaded, verified, kept at update/halo-<version>.vpk"
kept_only good 1.1.0-beta.4
{ printf '%s\n' "$log" | grep -q "signed by the update key" && printf '%s\n' "$log" | grep -q "install .* with VitaShell"; } ||
	fail "no log lines of the signature and the path"
run kept "$http/dl" x
expect "1 1.1.0-beta.4 $data/update/halo-1.1.0-beta.4.vpk" "the start finds it, checked again"
kept_only "kept check" 1.1.0-beta.4
before=$(grep -c "/dl/v1.1.0-beta.4/halo.vpk " "$out/requests")
run fetch "$http/dl" 1.1.0-beta.4
expect "0 " "fetched again: the kept one taken without downloading"
[ "$(grep -c "/dl/v1.1.0-beta.4/halo.vpk " "$out/requests")" = "$before" ] || fail "the VPK was downloaded again"
run fetch "$http/dl" 1.1.0-beta.5
expect "0 " "a newer release replaces the older kept one"
kept_only "newer" 1.1.0-beta.5
run fetch "$http/dl" 1.1.0-rc.1
expect "0 " "a release candidate after the betas"
kept_only "rc" 1.1.0-rc.1
run kept "$http/dl" x 1.1.0-rc.1
expect "0" "the start, once that version runs: nothing to install"
nothing_kept "installed, removed at start"

scenario tamperkept
run fetch "$http/dl" 1.1.0-beta.4
printf 'x' >> "$data/update/halo-1.1.0-beta.4.vpk"
run kept "$http/dl" x
expect "0" "a kept VPK changed afterwards: refused at start"
nothing_kept "changed kept VPK removed"
scenario fakename
run fetch "$http/dl" 1.1.0-beta.4
for f in vpk txt txt.sig; do mv "$data/update/halo-1.1.0-beta.4.$f" "$data/update/halo-9.9.9.$f"; done
run kept "$http/dl" x
expect "0" "a kept VPK renamed to another version: refused at start"
nothing_kept "renamed kept VPK removed"
scenario stray
mkdir -p "$data/update" && printf 'not ours' > "$data/update/halo-9.9.9.vpk"
run kept "$http/dl" x
expect "0" "a VPK without a signed manifest: never pointed at"
nothing_kept "stray VPK removed"

scenario redirect
run fetch "$http/redirect2" 1.1.0-beta.4
expect "0 " "followed loopback redirects (two hops)"
kept_only redirect 1.1.0-beta.4
scenario evil
run fetch "$http/evil" 1.1.0-beta.4
expect "-1 " "a redirect to another host refused" "not allowed (evil.example)"
nothing_kept evil
scenario older
run fetch "$http/dl" 1.1.0-beta.2
expect "-1 " "an older signed version refused (no downgrade)" "not newer"
nothing_kept older
scenario replay
run fetch "$http/dl" 1.1.0-beta.16
expect "-1 " "a manifest replayed under a newer tag refused" "not 1.1.0-beta.16"
nothing_kept replay
scenario tampered
run fetch "$http/dl" 1.1.0-beta.6
expect "-1 " "a VPK changed after signing refused" "not the file the manifest signs"
nothing_kept tampered
scenario lying
run fetch "$http/dl" 1.1.0-beta.15
expect "-1 " "a VPK whose SHA-512 is not the manifest's refused" "not the file the manifest signs"
nothing_kept lying
scenario wrongkey
run fetch "$http/dl" 1.1.0-beta.7
expect "-1 " "another key's signature refused" "not the update key"
nothing_kept wrongkey
scenario changed
run fetch "$http/dl" 1.1.0-beta.8
expect "-1 " "a manifest changed after signing refused" "not the update key"
nothing_kept changed
scenario big
run fetch "$http/dl" 1.1.0-beta.11
expect "-1 " "a signed manifest over 64 MB refused" "not taken"
nothing_kept big
grep -q "v1.1.0-beta.11/halo.vpk" "$out/requests" && fail "the 70 MB VPK was asked for"
scenario title
run fetch "$http/dl" 1.1.0-beta.12
expect "-1 " "another game's signed release refused" "title OTHER0001"
nothing_kept title
scenario nosig
run fetch "$http/dl" 1.1.0-beta.13
expect "-1 " "a release without a signature refused" "answered 404"
nothing_kept nosig
scenario offline
run fetch "http://127.0.0.1:1/dl" 1.1.0-beta.4
expect "-6 " "no server: no connection" "cannot connect"
nothing_kept offline
scenario notag
run fetch "$http/dl" 1.1.0-alpha.1
expect "-1 " "a version that is no tag refused"
nothing_kept notag
scenario keepold
run fetch "$http/dl" 1.1.0-beta.4
run fetch "$http/dl" 1.1.0-beta.6
expect "-1 " "a refused newer release" "not the file the manifest signs"
kept_only "a refusal keeps the verified one there was" 1.1.0-beta.4

scenario nospace
run nospace "$http/dl" 1.1.0-beta.4
expect "-5 " "too little space refused" "MB free"
nothing_kept nospace
scenario cancel
run cancel "$http/dl" 1.1.0-beta.4
expect "-2 " "stopped during the download"
nothing_kept cancel
scenario placeholder
log=$("$out/harness_placeholder" fetch "$data" "$http/dl" 1.1.0-beta.4 2>&1)
result=$(printf '%s\n' "$log" | sed -n 's/^result //p')
expect "-3 " "the placeholder key's build downloads nothing" "no update key in this build"
nothing_kept placeholder
scenario resume
run fetch "$http/cut" 1.1.0-beta.4
expect "0 " "a download cut short resumes and is kept"
grep "/cut/v1.1.0-beta.4/halo.vpk range=bytes=" "$out/requests" > /dev/null || fail "the second request had no Range"
kept_only resume 1.1.0-beta.4

# ---------- the download alone: TLS, redirects, framing
d=$out/s/download
mkdir -p "$d"
data=$d
run download x "$https/dl/v1.1.0-beta.4/halo.vpk" "$d/tls.vpk" 0
expect "1 " "over TLS with a certificate of the tests' authority"
cmp -s "$d/tls.vpk" "$out/releases/v1.1.0-beta.4/halo.vpk" || fail "the TLS download differs"
run download x "$self/dl/v1.1.0-beta.4/halo.vpk" "$d/self.vpk" 0
expect "0 " "a certificate of no authority refused" "certificate refused"
[ -s "$out/self_requests" ] && fail "a request reached the server whose certificate was refused"
run download x "$https/redirect/v1.1.0-beta.4/halo.vpk" "$d/r.vpk" 0
expect "0 " "https redirected to plain http refused" "not allowed"
run download x "$https/evil/v1.1.0-beta.4/halo.vpk" "$d/e.vpk" 0
expect "0 " "https redirected to another host refused" "not allowed (evil.example)"
run download x "$http/chunked/v1.1.0-beta.4/halo.vpk" "$d/c.vpk" 0
expect "0 " "a chunked download refused" "chunked"
run download x "$http/big/v1.1.0-beta.4/halo.vpk" "$d/b.vpk" 0
expect "0 " "over 64 MB refused before the body" "too large"
size=$(stat -c %s "$out/releases/v1.1.0-beta.4/halo.vpk")
run download x "$http/dl/v1.1.0-beta.4/halo.vpk" "$d/s.vpk" 1000
expect "0 " "another size than the signed one refused" "not 1000"
head -c 1000 "$out/releases/v1.1.0-beta.4/halo.vpk" > "$d/w.vpk"
run download x "$http/wrong-range/v1.1.0-beta.4/halo.vpk" "$d/w.vpk" "$size"
expect "0 " "a resume answered with another part refused" "another part"
head -c 1000 "$out/releases/v1.1.0-beta.4/halo.vpk" > "$d/ok.vpk"
run download x "$http/dl/v1.1.0-beta.4/halo.vpk" "$d/ok.vpk" "$size"
expect "1 " "a partial file resumed"
cmp -s "$d/ok.vpk" "$out/releases/v1.1.0-beta.4/halo.vpk" || fail "the resumed file differs"

if grep -v "agent=HaloCEVita/test$" "$out/requests" "$out/tls_requests" | grep -q .; then
	fail "a request carried another User-Agent"
fi

[ $status = 0 ] && echo PASS || echo FAIL
exit $status
